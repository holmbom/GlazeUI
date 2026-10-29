// TextInput — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER: Draw ritar, c.style bär måtten och temat färgerna. ALLT
// beteende bor också här: caret/selektion, klippbord, mus → teckenindex
// och scroll. Ritning OCH beteende räknar ur SAMMA innerBounds() i LOGISKA
// px — en sanning om måtten; de fysiska rects beteendet behöver
// (caret-damagen) är PROJEKTIONER av den (Draw::project), aldrig egna
// uträkningar.
//
// Det fältet DELAR med det flerradiga (`TextArea`) bor i den interna
// `../text_field.h`: ytan per familj, markeringens färgpar och ritning,
// caretens bredd, UTF-8-teckengränserna, inmatningens filter,
// ångra-historiken, redigeringsgenvägarnas fråga och lösenordets avbildning.
//
// EN REDIGERINGSVÄG: varje ändring användaren gör går genom
// `replace`, som registrerar steget — skrivandet, raderingen, klippet och
// inklistringen. Ångra och gör om går genom `restore`. En ren FLYTT
// (`movedCaret`) förseglar historikens grupp, en REDIGERING (`edited`) gör
// det inte, och det är hela grupperingens koppling till fältet.
//
// LÖSENORDET: caret och markering bor i den RIKTIGA texten, och bara de fem
// ställen som ritar eller träffar text (`textOriginX`, `drawContent`,
// `caretBounds`, `indexFromX`, `ensureCaretVisible`) läser den VISADE —
// `shown`/`toShown`/`fromShown`.

#include "glaze/widgets/textinput.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <system_error>

#include <gfx/platform.h>

#include "../font_set.h"
#include "../prop_parse.h"
#include "../text_field.h"
#include "glaze/app_window.h"
#include "glaze/draw.h"

namespace glaze {

using textfield::nextBoundary;
using textfield::prevBoundary;

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("textinput", TextInput)

// Ur headern med flit: `history_` är en `unique_ptr` till den INTERNA
// `textfield::History`, och både konstruktorns undantagsstädning och
// destruktorn kräver hela typen — den finns bara här.
TextInput::TextInput(std::string t) : text_(std::move(t)), commitBase_(text_)
{
}

TextInput::~TextInput() = default;

// Klampningen landar på TECKENGRÄNS: bara längden räcker inte, för en caret
// på byte 3 i "abcd" kan hamna mitt i ä:et i
// "åäö" — och nästa skrivna tecken sätts in mellan dess två byte.
//
// BASLINJEN följer med: appens text är inte användarens ändring, så
// ett Enter direkt efter ska inte committa den. Men bara när texten FAKTISKT
// byts — en handler som ekar tillbaka det användaren skrev (`setText(text())`
// i `onChange`, bindningsmönstret) får inte äta användarens commit.
//
// HISTORIKEN töms av samma skäl och under samma vakt: stegen
// beskrev en annan text, men ett eko får inte äta användarens ångra.
void TextInput::setText(std::string t)
{
    if (text_ == t)
        return;
    text_       = std::move(t);
    commitBase_ = text_;
    caret_      = textfield::floorBoundary(text_, caret_);
    anchor_     = textfield::floorBoundary(text_, anchor_);
    resetHistory();
    invalidate();
}

void TextInput::resetHistory()
{
    if (history_)
        history_->clear();
}

textfield::History& TextInput::history()
{
    if (!history_)
        history_ = std::make_unique<textfield::History>();
    return *history_;
}

void TextInput::sealHistory()
{
    if (history_)
        history_->seal();
}

// ÅNGRA/GÖR OM — kontraktet står i headern. Skrivskyddat läge
// SPÄRRAS här: historiken står kvar medan fältet är låst och gäller igen när
// det låses upp. Lösenordsläget behöver ingen spärr, för där FINNS ingen
// historik — `setPassword` tömmer den och `replace` registrerar ingenting,
// och en andra, redundant spärr hade gjort just den invarianten
// omätbar: textfield_gate ser ett steg som registrerats i lösenordsläge
// som `canUndo() == true`.
bool TextInput::canUndo() const
{
    return !readOnly_ && history_ && history_->canUndo();
}

bool TextInput::canRedo() const
{
    return !readOnly_ && history_ && history_->canRedo();
}

bool TextInput::undo()
{
    if (!canUndo())
        return false;
    const std::optional<textfield::Restore> r = history_->undo();
    if (!r)
        return false;
    restore(*r);
    return true;
}

bool TextInput::redo()
{
    if (!canRedo())
        return false;
    const std::optional<textfield::Restore> r = history_->redo();
    if (!r)
        return false;
    restore(*r);
    return true;
}

// Stegets svar utförs rakt av — det är användarens egen redigering som
// spelas upp, med markeringen den hade, och `onChange` fyras. Steget
// registreras INTE igen: historiken har redan flyttat sin topp.
void TextInput::restore(const textfield::Restore& r)
{
    text_.replace(r.pos, r.len, r.text);
    anchor_ = r.anchor;
    caret_  = r.caret;
    edited();
}

// LÖSENORDSLÄGET — kontraktet står i headern. Ett byte i båda
// riktningarna tömmer historiken: på väg in får inga steg med klartext ligga
// kvar, på väg ut fanns inga.
void TextInput::setPassword(bool on)
{
    if (password_ == on)
        return;
    password_ = on;
    resetHistory();
    invalidate();
}

// MAXLÄNGDEN och PLATSHÅLLAREN — kontrakten står i
// headern. Gränsen ändrar inget som syns (texten står kvar), så den
// invaliderar inte; hinten är ritning och gör det.
void TextInput::setMaxLength(int n)
{
    if (n < 0) {
        gfx::log("TextInput(%s)::setMaxLength(%d): negativ gräns — ignoreras",
                 name.c_str(), n);
        return;
    }
    maxLength_ = n;
}

void TextInput::setPlaceholder(TrText t)
{
    if (placeholder_ == t)
        return;
    placeholder_ = std::move(t);
    invalidate();
}

// TEXTSTORLEKEN — kontraktet i headern, regeln i `textSizeValue`.
// AUTO-höjden följer med, så ytan invalideras (gammal och ny rekt).
void TextInput::setTextSize(Length size)
{
    const std::optional<Length> s = textSizeValue(size, "TextInput", name);   // lang_gate:rad typnamnet i loggraden
    if (!s || (s->value == textSize_.value && s->unit == textSize_.unit))
        return;
    textSize_ = *s;
    invalidate();
}

Ctx TextInput::textCtx(const Ctx& c) const
{
    return sizedText(c, textSize_);
}

bool TextInput::setTextToken(const std::string& key, const TrText& value)
{
    if (key == "placeholder") {
        setPlaceholder(value);
        return true;
    }
    return Widget::setTextToken(key, value);   // basens toolTip
}

// DEN VISADE TEXTEN och avbildningen dit och tillbaka.
// Visade index är BYTE i den maskerade strängen.
const std::string& TextInput::shown(const Ctx& c) const
{
    if (!password_)
        return text_;
    textfield::maskInto(masked_, text_, c.style->passwordMask);
    return masked_;
}

std::size_t TextInput::toShown(const Ctx& c, std::size_t i) const
{
    if (!password_)
        return i;
    return textfield::codepoints(text_, i) *
           textfield::maskBytes(c.style->passwordMask);
}

std::size_t TextInput::fromShown(const Ctx& c, std::size_t j) const
{
    if (!password_)
        return j;
    const std::size_t m = textfield::maskBytes(c.style->passwordMask);
    return m == 0 ? 0 : textfield::byteOfCodepoint(text_, j / m);
}

// MARKERINGEN — kontraktet står i headern. Ankaret och careten
// klampas och snappas NEDÅT till en teckengräns, samma regel som `setText`
// har för en position ur en annan text. `movedCaret` ger den synliga fasen
// och ritar om; inget event, för markeringen är inte texten.
void TextInput::setSelection(int begin, int end)
{
    const auto plats = [this](int i) {
        return textfield::floorBoundary(
            text_, i <= 0 ? std::size_t(0) : static_cast<std::size_t>(i));
    };
    anchor_ = plats(begin);
    caret_  = plats(end);
    movedCaret();
}

void TextInput::selectAll()
{
    anchor_ = 0;
    caret_  = text_.size();
    movedCaret();
}

// COMMIT-BASLINJEN. Fokus som kommer sätter den; fokus som går
// committar en ändrad text — baslinjen flyttas DIREKT, och eventet POSTAS
// (`postEvent`): kroken körs mitt i `AppWindow::setFocus`. Inget postas utan
// en handler — kompositernas fält bär sällan någon, och en post utan
// mottagare hade bara väckt pumpen.
void TextInput::focusGained()
{
    commitBase_ = text_;
}

void TextInput::focusLost()
{
    holdPress_ = false;
    sealHistory();   // fokus tillbaka och fortsatt skrivande = ett nytt steg
    if (text_ == commitBase_)
        return;
    commitBase_ = text_;
    if (onCommit)
        postEvent([this] { fireEvent(onCommit); });   // värdet: text()
}

// ENTER — samma commit, men i händelseflödet: eventet fyras direkt.
// Tangenten konsumeras inte (`keyDown` svarar false), så AppWindow ritar
// inte om fönstret åt oss — en körd handler begär det själv, men BARA om
// vakten står sig: handlern får riva fältet eller hela fönstret, och en
// pekare till fönstret tagen före den hade då dinglat.
void TextInput::commitFromUser()
{
    if (text_ == commitBase_)
        return;
    commitBase_ = text_;
    if (onCommit && fireEvent(onCommit) && appWindow())
        appWindow()->invalidate();
}

// `commitEdit` (widget.h) är Enter utan tangenten.
void TextInput::commitEdit()
{
    commitFromUser();
}

// Skrivskyddat läge. SETTER just för att bytet ska INVALIDERA (regel 1):
// `readOnly` byter hela fältfyllningen, så en tilldelning på ett visat fält
// hade annars lämnat den gamla tonen kvar tills något ANNAT råkade rita om.
// Prejudikatet är `Progress::setIndeterminate`, av exakt samma skäl.
//
// Careten släcks direkt i stället för vid nästa timer-tick: `caretOn_`
// nollas här, och timern stoppas av `wantsTextInput()`-kroken när AppWindow
// nästa gång frågar. Careten RITAS aldrig under mellantiden.
void TextInput::setReadOnly(bool on)
{
    if (readOnly_ == on)
        return;
    readOnly_ = on;
    if (on)
        caretOn_ = false;
    invalidate();
}

bool TextInput::setProp(const std::string& key, const std::string& value)
{
    if (key == "text" || key == "title") {   // title = äldre alias (regel 4-arv)
        setText(value);
        return true;
    }
    if (key == "align") {
        Align a = Align::Left;
        if (propparse::parseAlign(value, a))
            setAlign(a);
        else
            gfx::log("setProp(%s): ogiltig align: %s (ignoreras)",
                     name.c_str(), value.c_str());
        return true;
    }
    if (key == "readOnly" || key == "readonly") {
        // Nyckeln är C++-fältnamnet (den form builderns preview-väg
        // levererar för en bool-rad); gemena varianten tas också, som
        // `color`/`role` på Label.
        setReadOnly(propparse::parseBool(value));
        return true;
    }
    if (key == "password") {
        // `losen.setPassword(true);` i `.glazeui` — setter-fältet, så
        // previewn ritar fältet maskerat precis som appen (regel 8).
        setPassword(propparse::parseBool(value));
        return true;
    }
    if (key == "placeholder") {
        // `namn.setPlaceholder("Namn");` — en literal. Ett tr-id går genom
        // `setTextToken`, så tokenet och språket följer med.
        setPlaceholder(value);
        return true;
    }
    if (key == "maxLength" || key == "maxlength") {
        // Ett heltal ≥ 0 och inget annat (`ToolBar::band`s form): ett
        // negativt tal eller skräp ⇒ false ⇒ raden blir opak, aldrig en
        // gissad gräns.
        int        n = 0;
        const auto r =
            std::from_chars(value.data(), value.data() + value.size(), n);
        if (r.ec != std::errc{} || r.ptr != value.data() + value.size() ||
            n < 0)
            return false;
        setMaxLength(n);
        return true;
    }
    return Widget::setProp(key, value);
}

std::size_t TextInput::selBegin() const
{
    return std::min(clampIdx(caret_), clampIdx(anchor_));
}

std::size_t TextInput::selEnd() const
{
    return std::max(clampIdx(caret_), clampIdx(anchor_));
}

gfx::Sizef TextInput::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // Bredden sätts normalt av layouten; autohöjden följer fonten så
        // fältet aldrig klipper sin egen textrad.
        //
        // `Draw::lineHeight` och INTE `c.font->height()`: fonten talar FYSISKA
        // px, mätvägen LOGISKA — en blandad uträkning (`c.scale` bara på
        // `fieldPadY` medan fonthöjden redan är fysisk) ger en höjd som är fel
        // i båda leden. Radhöjden är TEXTSTORLEKENS, luften fältets.
        const auto& m = *c.style;
        return gfx::Sizef{width > 0.0f ? width : m.fieldDefaultW,
                          height > 0.0f ? height
                                        : Draw::lineHeight(textCtx(c)) +
                                              2.0f * m.fieldPadY};
    }
    }
}

// Ytan texten ritas i (widget-lokala LOGISKA px): paddingen i sidled, hela
// höjden (texten centreras i den). Ritningens, caretens och hit-testens
// gemensamma sanning.
gfx::Rectf TextInput::innerBounds(const Ctx& c) const
{
    const gfx::Rectf b  = Draw::bounds(c, resolve(c));
    const float      px = c.style->fieldPadX;
    return gfx::Rectf{b.x + px, b.y, b.w - 2.0f * px, b.h};
}

// Luften texten måste lämna vid högerkanten: caret + stilens caretMargin.
// Samma tal som scrollen håller careten innanför, och därför också det
// justeringen räknar bort — annars hade en högerjusterad text tryckt sin
// egen caret utanför klippet. Caretens bredd är fältets DELADE
// (`textfield::caretWidth` — aldrig under en fysisk px).
float TextInput::caretEdge(const Ctx& c) const
{
    return textfield::caretWidth(c) + c.style->caretMargin;
}

// TEXTENS VÄNSTERKANT: justeringen och scrollen i ETT tal, och den ENDA
// sanningen om var texten börjar. Offseten är klampad ≥ 0 (`luft > 0`),
// så den är noll exakt när texten inte får plats — och då gäller
// scrollen ensam, precis som före justeringen fanns. Bredden är den VISADE
// textens (masken i lösenordsläge).
float TextInput::textOriginX(const Ctx& c) const
{
    const gfx::Rectf inner = innerBounds(c);
    const float      luft =
        inner.w - Draw::measure(textCtx(c), shown(c)).w - caretEdge(c);
    float            off   = 0.0f;
    if (luft > 0.0f) {
        if (align_ == Align::Center)
            off = luft / 2.0f;
        else if (align_ == Align::Right)
            off = luft;
    }
    return inner.x + off - scrollX_;
}

void TextInput::draw(const Ctx& c)
{
    // Scrollen är widget-tillstånd men beror på innerytan — justeras
    // precis före ritning så careten alltid är i synfältet.
    ensureCaretVisible(c);

    const gfx::Rectf b = c.bounds();
    switch (c.ui) {
    // ALLA TRE ritar FÄLTET med den DELADE ritningen (`textfield::
    // drawSurface`, ../text_field.cpp), och det är ett beslut och inte en
    // lucka: ytan, kanten som bär tillståndet och markeringens färgpar är
    // samma sak i det enradiga och det flerradiga fältet, och formen per UI
    // — med de fyra tillståndens tabeller — står på ETT ställe (`sash.cpp`
    // är prejudikatet). Det här fältet äger INNEHÅLLET: en rad, justerad
    // och vågrätt scrollad.
    case UI::Glaze:
    case UI::Minimalistic:
    case UI::Slate:
    default: {
        const textfield::Selection sel = textfield::drawSurface(
            c, b, isEnabled(), hovered(), readOnly_);
        drawContent(c, sel.fill, sel.text);
        break;
    }
    }

    // Fokusringen är identisk i alla tre — den ligger UTANFÖR kontrollen och
    // hör till ramverkets tangentnavigering, inte till familjens formspråk
    // (tjockleken skiljer sig, men det är ett MÅTT och bor i Style).
    if (focused() && isEnabled())
        textfield::drawFocusRing(c, b);
}

// Text, selektion och caret — INNEHÅLLET, som är samma sak i alla tre
// ritningarna: en textrad klippt mot innerytan, tre segment när något är
// markerat, och careten. Det som skiljer familjerna är YTAN runt omkring
// och vilket FÄRGPAR selektionen använder, så just det paret skickas in.
// Själva raden ritas av fältets DELADE `textfield::drawRun` — samma
// funktion ritar varje rad i det flerradiga fältet, så de två kan inte
// glida isär i UTF-8-hanteringen eller i markeringens geometri.
void TextInput::drawContent(const Ctx& c, gfx::Color selFill,
                            gfx::Color selText) const
{
    const gfx::Rectf inner = innerBounds(c);
    const gfx::Color tc    = textColor(c);
    const Ctx        t     = textCtx(c);   // texten i fältets storlek

    // Långa texter scrollar (scrollX_) i stället för att rinna utanför.
    // LÖSENORDET: raden som ritas är den VISADE (masken), och markeringen
    // avbildas dit — den bor i den riktiga texten.
    Draw::clip(c, inner);

    // PLATSHÅLLAREN — bara i ett TOMT fält, också med
    // fokus, i `textDim`, aldrig maskerad, och bara första raden.
    // Recten lämnar careten sin luft (`caretEdge`), så hinten justeras exakt
    // som texten hade justerats och careten står i dess kant.
    if (text_.empty() && !placeholder_.empty())
        Draw::textElided(t, {inner.x, inner.y, inner.w - caretEdge(c), inner.h},
                         textfield::singleLine(placeholder_.str().c_str()),
                         c.theme->colors.textDim, align_);

    const bool        markerat = focused() && hasSelection();
    const std::size_t b        = markerat ? toShown(c, selBegin()) : 0;
    const std::size_t e        = markerat ? toShown(c, selEnd()) : 0;
    const float       x        = textOriginX(c);
    textfield::drawRun(t, inner, x, shown(c), b, e, tc,
                       textfield::Selection{selFill, selText});

    // Careten ritas i caretBounds — SAMMA rect som blink-damagen
    // invalidaterar (en källa).
    if (caretVisible() && isEnabled())
        Draw::rect(c, caretBounds(c), 0.0f, tc);

    Draw::endClip(c);
}

// Caretens rect i LOGISKA px: x ur prefixmätningen fram till caret, minus
// scroll; höjd = textraden, centrerad i innerytan (samma formel som
// textritningen). Prefixet mäts i den VISADE texten.
gfx::Rectf TextInput::caretBounds(const Ctx& c) const
{
    const gfx::Rectf inner = innerBounds(c);
    const Ctx        t     = textCtx(c);
    const float      fh    = Draw::lineHeight(t);
    const float      cx    = textOriginX(c) +
                      Draw::prefixWidth(t, shown(c), toShown(c, caret()));
    return gfx::Rectf{cx, inner.y + (inner.h - fh) / 2.0f,
                      textfield::caretWidth(c), fh};
}

// Blink-damagen talar FYSISKA px (invalidateRect-kontraktet) — samma rect
// som ritas, bara projicerad. Aldrig en andra uträkning.
gfx::Rectf TextInput::caretRect(const Ctx& c) const
{
    return Draw::project(c, resolve(c), caretBounds(c));
}

// Blinkfasen drivs av AppWindow (caret-timern). Bara caret-recten
// invalidateras — det är HELA vinsten med partiell omritning här: appen
// sover mellan blinkningarna och ritar en handfull pixlar per tick.
void TextInput::caretBlink(const Ctx& c, bool on)
{
    if (caretOn_ == on)
        return;
    caretOn_ = on;
    if (focused_)
        invalidateRect(caretRect(c));
}

// Mus → teckenindex: fasadens text-hit-test (närmaste teckengräns i x-led
// — klick i högra halvan av ett tecken lägger careten efter det).
// TextInput är text-API:ts första konsument, precis som den validerade
// basen. Träffen görs i den VISADE texten och avbildas
// tillbaka till den riktiga (lösenordet).
std::size_t TextInput::indexFromX(const Ctx& c, float mx) const
{
    const float x = Draw::localX(c, resolve(c), mx) - textOriginX(c);
    return fromShown(c, Draw::byteIndexAtX(textCtx(c), shown(c), x));
}

// Håll careten inom innerrekten: scrolla texten när careten går utanför,
// och dra tillbaka scrollen när texten krympt (inget tomrum till höger).
// Båda måtten är den VISADE textens.
void TextInput::ensureCaretVisible(const Ctx& c)
{
    const gfx::Rectf inner = innerBounds(c);
    if (inner.w <= 0.0f)
        return;

    const Ctx   t      = textCtx(c);
    const float edge   = caretEdge(c);
    const float caretX = Draw::prefixWidth(t, shown(c), toShown(c, caret()));

    if (caretX - scrollX_ < 0.0f)
        scrollX_ = caretX;
    else if (caretX - scrollX_ > inner.w - edge)
        scrollX_ = caretX - (inner.w - edge);

    const float fullW = Draw::measure(t, shown(c)).w;
    scrollX_ = std::clamp(scrollX_, 0.0f,
                          std::max(0.0f, fullW + edge - inner.w));
}

// DEN ENDA REDIGERINGSVÄGEN — steget registreras med
// markeringen FÖRE ändringen, så ångra kan återställa den. I lösenordsläge
// registreras ingenting; historiken allokeras då aldrig.
void TextInput::replace(std::size_t b, std::size_t e, const std::string& ins,
                        textfield::EditKind kind)
{
    if (!password_)
        history().record(b, text_.substr(b, e - b), ins, clampIdx(anchor_),
                         caret(), kind);
    text_.replace(b, e - b, ins);
    caret_ = anchor_ = b + ins.size();
}

void TextInput::insertFiltered(const char* utf8, textfield::EditKind kind)
{
    // Enradigt fält: inmatad/inklistrad text klipps vid första radbrytet och
    // rensas från övriga kontrolltecken (det delade filtret). Markeringen
    // ersätts i SAMMA anrop — skrivande över en markering är ett steg.
    // MAXLÄNGDEN kapar insättningen på kodpunktsgräns, med markeringen
    // räknad som frigjord; ryms INGET av något icke-tomt händer ingenting —
    // markeringen raderas inte. (En insättning som redan var tom ersätter
    // markeringen som vanligt.)
    const std::string rå  = textfield::singleLine(utf8);
    const std::string ins = textfield::fitMaxLength(
        text_, selBegin(), selEnd(), rå, static_cast<std::size_t>(maxLength_));
    if ((!rå.empty() && ins.empty()) || (ins.empty() && !hasSelection())) {
        showCaret();   // inget ändrades — men careten ska synas
        return;
    }
    replace(selBegin(), selEnd(), ins, kind);
    edited();
}

// Caret synlig: ny blinkperiod (careten ska inte blinka bort mitt i
// navigering eller skrivande) och widgetens yta ritas om.
void TextInput::showCaret()
{
    caretOn_ = true;
    restartCaretBlink();
    invalidate();
}

// Caret FLYTTAD utan textändring — tangent, mus, `setSelection`/`selectAll`.
// En flytt bryter historikens grupp: skriver man vidare någon annanstans
// är det ett nytt steg.
void TextInput::movedCaret()
{
    sealHistory();
    showCaret();
}

// Text ändrad av användaren: careten synlig, plus onChange (värdet läses ur
// sender — args bär ingen nyttolast, C#-mönstret). Förseglar INTE: nästa
// tecken ska kunna fortsätta samma steg.
void TextInput::edited()
{
    showCaret();
    if (onChange) {
        ChangeArgs e;
        onChange(this, e);
    }
}

bool TextInput::textInput(const char* utf8)
{
    // SKRIVSKYDDAT: tecknet sväljs. Att returnera true och inte false är
    // avsiktligt — fältet HAR fokus och tecknet ska inte studsa vidare
    // till någon annan (en genväg som råkar vara ett tecken hör inte
    // hemma i ett fokuserat textfält bara för att det är låst).
    if (readOnly_)
        return true;
    insertFiltered(utf8, textfield::EditKind::Typing);
    return true;
}

bool TextInput::keyDown(gfx::Key k, std::uint32_t mods)
{
    const bool shift = mods & gfx::kModShift;
    // REDIGERINGSGENVÄGARNA (A/C/X/V/Z/Y) är plattformens — Ctrl UTAN Alt
    // (AltGr är Ctrl+Alt på Windows och skriver ett tecken), på macOS Cmd
    // utan Ctrl och Option (`textfield::editShortcut`).
    const bool kort = textfield::editShortcut(mods);

    // SKRIVSKYDDAT: allt som ÄNDRAR texten spärras, allt som
    // LÄSER den behålls. Navigering (Left/Right/Home/End, med och utan
    // Shift), Ctrl+A och Ctrl+C går alltså igenom nedan — det är markera
    // och kopiera som är hela poängen med lägret. Ctrl+Z/Ctrl+Y faller
    // också igenom: de konsumeras som no-op (`canUndo` är false här).
    if (readOnly_)
        switch (k) {
        case gfx::Key::Backspace:
        case gfx::Key::Delete:
            return true;                    // vår tangent, men no-op
        case gfx::Key::X:
        case gfx::Key::V:
            if (kort)
                return true;                // klipp/klistra spärras
            break;
        default:
            break;
        }

    switch (k) {
    case gfx::Key::Left: {
        // Utan Shift kollapsar en selektion till sin vänsterkant (som
        // Windows); Shift flyttar caret och låter ankaret stå kvar.
        std::size_t i = caret();
        if (!shift && hasSelection())
            i = selBegin();
        else
            i = prevBoundary(text_, i);
        caret_ = i;
        if (!shift)
            anchor_ = i;
        movedCaret();
        return true;
    }
    case gfx::Key::Right: {
        std::size_t i = caret();
        if (!shift && hasSelection())
            i = selEnd();
        else
            i = nextBoundary(text_, i);
        caret_ = i;
        if (!shift)
            anchor_ = i;
        movedCaret();
        return true;
    }
    case gfx::Key::Home:
        caret_ = 0;
        if (!shift)
            anchor_ = 0;
        movedCaret();
        return true;
    case gfx::Key::End:
        caret_ = text_.size();
        if (!shift)
            anchor_ = caret_;
        movedCaret();
        return true;

    // RADERINGEN — en markering är ett eget steg (`Block`), backsteg och
    // Delete i följd är var sitt steg.
    case gfx::Key::Backspace:
        if (hasSelection()) {
            replace(selBegin(), selEnd(), std::string(),
                    textfield::EditKind::Block);
            edited();
        } else if (caret() > 0) {
            replace(prevBoundary(text_, caret()), caret(), std::string(),
                    textfield::EditKind::BackDelete);
            edited();
        } else {
            showCaret();   // inget att radera — men tangenten är vår
        }
        return true;
    case gfx::Key::Delete:
        if (hasSelection()) {
            replace(selBegin(), selEnd(), std::string(),
                    textfield::EditKind::Block);
            edited();
        } else if (caret() < text_.size()) {
            replace(caret(), nextBoundary(text_, caret()), std::string(),
                    textfield::EditKind::FwdDelete);
            edited();
        } else {
            showCaret();
        }
        return true;

    // Klippbordet (alltid UTF-8 genom gfx-fasaden). Utan genvägen faller
    // bokstäverna igenom — själva tecknet kommer som TextInput-event. I
    // LÖSENORDSLÄGE är kopiera och klipp ut AVSTÄNGDA: tangenten är vår
    // och gör ingenting, och klipp ut raderar inte heller.
    case gfx::Key::C:
        if (!kort)
            return false;
        if (hasSelection() && !password_)
            gfx::setClipboardText(
                text_.substr(selBegin(), selEnd() - selBegin()));
        return true;
    case gfx::Key::X:
        if (!kort)
            return false;
        if (hasSelection() && !password_) {
            gfx::setClipboardText(
                text_.substr(selBegin(), selEnd() - selBegin()));
            replace(selBegin(), selEnd(), std::string(),
                    textfield::EditKind::Block);
            edited();
        }
        return true;
    case gfx::Key::V:
        if (!kort)
            return false;
        insertFiltered(gfx::clipboardText().c_str(),
                       textfield::EditKind::Block);
        return true;
    case gfx::Key::A:
        if (!kort)
            return false;
        selectAll();
        return true;

    // ÅNGRA och GÖR OM — tangenterna ANROPAR de publika
    // metoderna, en väg. KONSUMERADE också när det inte finns något att göra
    // (tom historik, skrivskyddat, lösenord): ett fokuserat fälts Ctrl+Z ska
    // aldrig nå en Redigera-menys accelerator.
    case gfx::Key::Z:
        if (!kort)
            return false;
        if (shift)
            redo();
        else
            undo();
        return true;
    case gfx::Key::Y:
        if (!kort)
            return false;
        redo();
        return true;

    // ENTER committar men är inte vår: tangenten bubblar vidare som
    // vanligt, med texten redan sparad (kontraktet vid `onCommit`). Den
    // förseglar också historikens grupp — det man skriver efter en commit är
    // ett nytt steg.
    case gfx::Key::Return:
        sealHistory();
        commitFromUser();
        return false;

    default:
        // Tab/Escape/Space m.fl. är inte våra: Tab ska vandra fokus,
        // Space kommer som TextInput-event (mellanslag är text här).
        return false;
    }
}

// Ett tryck som GAV fokus HÅLLS: trycket minns var det började, och
// draget väntar ut dragtröskeln — annars hade en darrig hand mellan tryck och
// släpp gjort `onFocus → selectAll()` till en markering från textens början
// till pekaren, för den postade handlern körs efter trycket men före
// rörelsen.
void TextInput::mouseDown(const Ctx& c, float mx, float)
{
    caret_ = anchor_ = indexFromX(c, mx);
    holdPress_  = pressGaveFocus();
    pressIndex_ = caret_;
    pressX_     = Draw::localX(c, resolve(c), mx);
    movedCaret();
}

// Ett HÅLLET tryck blir ett drag först bortom tröskeln, och då ANKRAS det om
// vid trycket: draget markerar från där man tryckte, vad en handler än
// gjorde med markeringen emellan. Positionen klampas mot texten, som en
// handler också kan ha bytt. Omankringen ÄR en ändring av markeringen och
// ritas om också när careten står still (pekaren bortom textens slut).
void TextInput::mouseDrag(const Ctx& c, float mx, float)
{
    bool ändrad = false;
    if (holdPress_) {
        if (std::abs(Draw::localX(c, resolve(c), mx) - pressX_) <=
            c.style->dragThreshold)
            return;
        holdPress_            = false;
        const std::size_t ank = textfield::floorBoundary(
            text_, std::min(pressIndex_, text_.size()));
        ändrad  = ank != anchor_;
        anchor_ = ank;
    }
    const std::size_t i = indexFromX(c, mx);
    if (i != caret_) {
        caret_ = i;   // ankaret står kvar — selektionen växer med draget
        ändrad = true;
    }
    if (ändrad)
        movedCaret();
}

} // namespace glaze
