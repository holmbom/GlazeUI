// TextArea — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ RADER:
// Draw ritar, c.style bär måtten och temat färgerna. Allt BETEENDE bor
// också här: radlayouten, caret och markering över rader, tangenterna,
// musen och rullningen. Ritning OCH beteende räknar ur SAMMA geometri i
// LOGISKA px (`origin`, `lineAt`, `xAt`, `indexAt`); de fysiska rects
// beteendet behöver (caret-damagen, musträffen) är PROJEKTIONER av den.
//
// Det fältet DELAR med det enradiga — ytan per familj, markeringens färgpar
// och rad, caretens bredd, UTF-8-gränserna, filtren, redigeringsgenvägarna
// och ÅNGRA-HISTORIKEN — bor i den interna `../text_field.h`.
// Rullningen är Panels (overlay-remsorna, hjulet).
//
// SKRIVNA BESLUT (de långa motiveringarna står i text_area.h):
//   egen widget, inte en egenskap på TextInput · `wrap` som Label (regel 6)
//   · luften rullar med texten · caretens sida vid en hårdbrytning ·
//   autoscroll per musrörelse (ingen timer) · ingen IME/justering ·
//   ångra går genom `replaceRaw`, så den inkrementella layouten följer med.

#include "glaze/widgets/text_area.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <system_error>

#include <gfx/platform.h>

#include "glaze/app_window.h"
#include "glaze/draw.h"

#include "../font_set.h"
#include "../prop_parse.h"
#include "../text_field.h"

namespace glaze {
namespace {

// Snittet av två rektanglar — tomt (0 × 0) när de inte möts. Caretens
// blink-damage klipps mot fältets inre: en bortrullad caret ritas inte och
// ska inte heller rita om något.
gfx::Rectf snitt(const gfx::Rectf& a, const gfx::Rectf& b)
{
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.w, b.x + b.w);
    const float y1 = std::min(a.y + a.h, b.y + b.h);
    if (x1 <= x0 || y1 <= y0)
        return gfx::Rectf{};
    return gfx::Rectf{x0, y0, x1 - x0, y1 - y0};
}

} // namespace

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("textarea", TextArea)

TextArea::TextArea(std::string t)
    : text_(textfield::unifyNewlines(std::move(t))), commitBase_(text_)
{
    setClip(true);
    setBorder(true);        // `border` drar in innerBounds en ramtjocklek —
                            // fältets kant ritas av draw(), inte av Panel
    showScrollbars(true);   // överflöd ska SYNAS (ListBox-prejudikatet)
}

// Här och inte i headern: `history_` pekar på en intern typ.
TextArea::~TextArea() = default;

// ------------------------------------------------------------ tillstånd

void TextArea::setText(std::string t)
{
    t = textfield::unifyNewlines(std::move(t));
    if (text_ == t)
        return;
    text_ = std::move(t);
    // Baslinjen följer med en text som BYTS — `TextInput::setText`s regel:
    // appens text är inte användarens ändring, men ett eko av den
    // är inte heller en ny text. Historiken töms under samma vakt.
    commitBase_ = text_;
    if (history_)
        history_->clear();
    // Klampa mot den nya texten — och på TECKENGRÄNS: en position som var
    // en gräns i den gamla texten kan ligga mitt i ett åäö i den nya, och en
    // insättning där hade brutit UTF-8.
    caret_    = textfield::floorBoundary(text_, caret_);
    anchor_   = textfield::floorBoundary(text_, anchor_);
    upstream_ = false;
    goalX_    = -1.0f;
    layout_.valid = false;   // allt läggs om vid nästa mätning
    invalidate();
}

// SETTER just för att bytet ska INVALIDERA (regel 1) — hela fyllningen byts.
// Careten släcks direkt; timern stoppas av `wantsTextInput()` nästa gång
// AppWindow frågar (TextInput-prejudikatet).
void TextArea::setReadOnly(bool on)
{
    if (readOnly_ == on)
        return;
    readOnly_ = on;
    if (on)
        caretOn_ = false;
    invalidate();
}

void TextArea::setWrap(bool on)
{
    if (wrap_ == on)
        return;
    wrap_         = on;
    upstream_     = false;
    goalX_        = -1.0f;
    layout_.valid = false;
    invalidate();
}

// MAXLÄNGDEN och PLATSHÅLLAREN — `TextInput`s
// sättare rakt av: gränsen ändrar inget som syns, hinten är ritning.
void TextArea::setMaxLength(int n)
{
    if (n < 0) {
        gfx::log("TextArea(%s)::setMaxLength(%d): negativ gräns — ignoreras",
                 name.c_str(), n);
        return;
    }
    maxLength_ = n;
}

// TEXTSTORLEKEN — regeln i `textSizeValue`. Layoutens nyckel bär
// fontpekaren och radhöjden, så nästa ritning lägger om raderna själv.
void TextArea::setTextSize(Length size)
{
    const std::optional<Length> s = textSizeValue(size, "TextArea", name);   // lang_gate:rad typnamnet i loggraden
    if (!s || (s->value == textSize_.value && s->unit == textSize_.unit))
        return;
    textSize_ = *s;
    invalidate();
}

Ctx TextArea::textCtx(const Ctx& c) const
{
    return sizedText(c, textSize_);
}

void TextArea::setPlaceholder(TrText t)
{
    if (placeholder_ == t)
        return;
    placeholder_ = std::move(t);
    invalidate();
}

bool TextArea::setTextToken(const std::string& key, const TrText& value)
{
    if (key == "placeholder") {
        setPlaceholder(value);
        return true;
    }
    return Panel::setTextToken(key, value);   // basens toolTip
}

bool TextArea::setProp(const std::string& key, const std::string& value)
{
    if (key == "text") {
        setText(value);
        return true;
    }
    if (key == "readOnly" || key == "readonly") {
        // Nyckeln är C++-fältnamnet (builderns bool-rad); den gemena
        // varianten tas också, som på TextInput.
        setReadOnly(propparse::parseBool(value));
        return true;
    }
    if (key == "wrap") {
        setWrap(propparse::parseBool(value));
        return true;
    }
    if (key == "placeholder") {
        setPlaceholder(value);
        return true;
    }
    if (key == "acceptsTab") {
        setAcceptsTab(propparse::parseBool(value));
        return true;
    }
    if (key == "maxLength" || key == "maxlength") {
        // Ett heltal ≥ 0 och inget annat (TextInputs och `ToolBar::band`s
        // form) — annars en opak rad.
        int        n = 0;
        const auto r =
            std::from_chars(value.data(), value.data() + value.size(), n);
        if (r.ec != std::errc{} || r.ptr != value.data() + value.size() ||
            n < 0)
            return false;
        setMaxLength(n);
        return true;
    }
    return Panel::setProp(key, value);
}

// MARKERINGEN — `TextInput`s form: klampat och nedsnappat till en
// teckengräns, ankaret på `begin` och careten på `end`. Ingen rullning (det
// är en skriven gräns i headern) och inget event.
void TextArea::setSelection(int begin, int end)
{
    const auto plats = [this](int i) {
        return textfield::floorBoundary(
            text_, i <= 0 ? std::size_t(0) : static_cast<std::size_t>(i));
    };
    anchor_   = plats(begin);
    caret_    = plats(end);
    upstream_ = false;
    goalX_    = -1.0f;
    movedCaret();
}

void TextArea::selectAll()
{
    setSelection(0, static_cast<int>(text_.size()));
}

// COMMIT-BASLINJEN — `TextInput`s krokar: fokus som kommer sätter
// den, fokus som går committar en ändrad text och POSTAR eventet.
void TextArea::focusGained()
{
    commitBase_ = text_;
}

void TextArea::focusLost()
{
    holdPress_ = false;
    sealHistory();   // fokus tillbaka och fortsatt skrivande = ett nytt steg
    if (text_ == commitBase_)
        return;
    commitBase_ = text_;
    if (onCommit)
        postEvent([this] { fireEvent(onCommit); });   // värdet: text()
}

// `commitEdit` (widget.h) — fokusförlustens commit, men NU och synkront.
// Ingen tangent bär den, så ingen fönsterväg ritar om åt oss: en körd
// handler begär det, om vakten står sig (TextInputs `commitFromUser`).
void TextArea::commitEdit()
{
    if (text_ == commitBase_)
        return;
    commitBase_ = text_;
    if (onCommit && fireEvent(onCommit) && appWindow())
        appWindow()->invalidate();
}

// ÅNGRA/GÖR OM — `TextInput`s form: `canUndo` bär spärren
// (skrivskyddat har ingen historik), och svaret utförs i `restore`.
bool TextArea::canUndo() const
{
    return !readOnly_ && history_ && history_->canUndo();
}

bool TextArea::canRedo() const
{
    return !readOnly_ && history_ && history_->canRedo();
}

bool TextArea::undo()
{
    if (!canUndo())
        return false;
    const std::optional<textfield::Restore> r = history_->undo();
    if (!r)
        return false;
    restore(*r);
    return true;
}

bool TextArea::redo()
{
    if (!canRedo())
        return false;
    const std::optional<textfield::Restore> r = history_->redo();
    if (!r)
        return false;
    restore(*r);
    return true;
}

textfield::History& TextArea::history()
{
    if (!history_)
        history_ = std::make_unique<textfield::History>();
    return *history_;
}

void TextArea::sealHistory()
{
    if (history_)
        history_->seal();
}

std::size_t TextArea::selBegin() const
{
    return std::min(clampIdx(caret_), clampIdx(anchor_));
}

std::size_t TextArea::selEnd() const
{
    return std::max(clampIdx(caret_), clampIdx(anchor_));
}

// --------------------------------------------------------------- layouten

float TextArea::wrapWidth(const Ctx& c) const
{
    if (!wrap_)
        return -1.0f;
    const gfx::Rectf in = innerBounds(c);
    return std::max(0.0f, in.w - c.style->fieldPadX - padRight(c));
}

float TextArea::tabWidth(const Ctx& c) const
{
    return float(std::max(1, c.style->textAreaTabSpaces)) *
           Draw::measure(textCtx(c), " ").w;
}

// Raderna för [b, e), som alltid är HELA stycken. Utan brytning är ett
// stycke en rad och dess bredd mäts (det vågräta spelrummet); med brytning
// är det `Draw::wrapLines` — samma brytning som en radbruten etikett. Med
// TABBAR mäter och bryter tabbstoppens vägar (`textfield::tabbed…`);
// ett tabbfritt stycke går exakt den vanliga vägen.
void TextArea::linesFor(const Ctx& c0, float wrapW, float tabW, std::size_t b,
                        std::size_t e, std::vector<Line>& out) const
{
    const Ctx c = textCtx(c0);   // raderna mäts i fältets textstorlek
    if (wrapW < 0.0f) {
        std::size_t s = b;
        for (;;) {
            std::size_t nl = text_.find('\n', s);
            if (nl == std::string::npos || nl > e)
                nl = e;
            out.push_back(Line{s, nl,
                               textfield::tabbedWidth(
                                   c, text_.substr(s, nl - s), tabW)});
            if (nl >= e)
                break;
            s = nl + 1;
        }
        return;
    }
    for (const gfx::WrappedLine& w :
         textfield::tabbedWrapLines(c, text_.substr(b, e - b), wrapW, tabW))
        out.push_back(Line{b + w.begin, b + w.end, 0.0f});
}

const TextArea::Layout& TextArea::layout(const Ctx& c) const
{
    // NYCKELNS font och radhöjd är TEXTSTORLEKENS; brytbredden är
    // fältets geometri och räknas med `c`.
    const Ctx        t  = textCtx(c);
    const gfx::Font* f  = t.font;
    const float      lh = Draw::lineHeight(t);
    const float      ww = wrapWidth(c);
    const float      tw = tabWidth(c);
    Layout&          L  = layout_;
    if (L.valid && L.font == f && L.scale == c.scale && L.lineH == lh &&
        L.wrapW == ww && L.tabW == tw)
        return L;
    L.font  = f;
    L.scale = c.scale;
    L.lineH = lh;
    L.wrapW = ww;
    L.tabW  = tw;
    L.lines.clear();
    linesFor(c, ww, tw, 0, text_.size(), L.lines);
    L.maxW = 0.0f;
    for (const Line& l : L.lines)
        L.maxW = std::max(L.maxW, l.w);
    L.valid = true;
    return L;
}

// EN REDIGERING lägger om de stycken den rörde och FÖRSKJUTER resten — aldrig
// hela texten. [b, oldEnd) i den GAMLA texten är [b, newEnd) i den nya, och
// allt före `b` är orört. Styckena som berördes är de från styckets början
// före `b` till styckets slut efter `newEnd`; i den gamla texten slutar de på
// samma ställe förskjutet tillbaka. Deras gamla rader är exakt de som BÖRJAR
// i [stycketsBörjan, gamlaSlutet] — nästa stycke börjar först efter dess
// `\n`, och ett stycke har ingen tom rad på slutet (wrapText-kontraktet).
// Att lägga om ett stycke för sig ger samma rader som att lägga om allt:
// brytningen arbetar stycke för stycke.
void TextArea::relayout(std::size_t b, std::size_t oldEnd, std::size_t newEnd)
{
    if (!layout_.valid)
        return;
    const std::optional<Ctx> c = openCtx();
    if (!c || layout_.font != textCtx(*c).font || layout_.scale != c->scale ||
        layout_.lineH != Draw::lineHeight(textCtx(*c)) ||
        layout_.wrapW != wrapWidth(*c) || layout_.tabW != tabWidth(*c)) {
        layout_.valid = false;
        return;
    }
    std::size_t pb = 0;
    if (b > 0) {
        const std::size_t nl = text_.rfind('\n', b - 1);
        pb = nl == std::string::npos ? 0 : nl + 1;
    }
    std::size_t pe = text_.find('\n', newEnd);
    if (pe == std::string::npos)
        pe = text_.size();
    const std::size_t peGammal = pe - newEnd + oldEnd;

    std::vector<Line>& rader = layout_.lines;
    const auto         förRad = [](std::size_t v, const Line& l) {
        return v < l.b;
    };
    const auto lo = std::lower_bound(
        rader.begin(), rader.end(), pb,
        [](const Line& l, std::size_t v) { return l.b < v; });
    const auto hi = std::upper_bound(lo, rader.end(), peGammal, förRad);

    std::vector<Line> nya;
    linesFor(*c, layout_.wrapW, layout_.tabW, pb, pe, nya);
    const std::size_t vid = static_cast<std::size_t>(lo - rader.begin());
    rader.erase(lo, hi);
    rader.insert(rader.begin() + static_cast<std::ptrdiff_t>(vid), nya.begin(),
                 nya.end());
    for (std::size_t i = vid + nya.size(); i < rader.size(); ++i) {
        rader[i].b = rader[i].b - oldEnd + newEnd;
        rader[i].e = rader[i].e - oldEnd + newEnd;
    }
    if (layout_.wrapW < 0.0f) {
        layout_.maxW = 0.0f;
        for (const Line& l : rader)
            layout_.maxW = std::max(layout_.maxW, l.w);
    }
}

// -------------------------------------------------------------- geometrin

float TextArea::padRight(const Ctx& c) const
{
    return std::max(c.style->fieldPadX, c.style->scrollThick);
}

float TextArea::padBottom(const Ctx& c) const
{
    const auto& m = *c.style;
    if (wrap_)
        return m.fieldPadY;
    const float bredd = m.fieldPadX + layout(c).maxW +
                        textfield::caretWidth(c) + padRight(c);
    return bredd > innerBounds(c).w ? std::max(m.fieldPadY, m.scrollThick)
                                    : m.fieldPadY;
}

gfx::Sizef TextArea::contentExtent(const Ctx& c) const
{
    const Layout& L = layout(c);
    const auto&   m = *c.style;
    const float   w = wrap_ ? innerBounds(c).w
                            : m.fieldPadX + L.maxW + textfield::caretWidth(c) +
                                  padRight(c);
    const float   h = m.fieldPadY + float(L.lines.size()) * L.lineH +
                    padBottom(c);
    // FYSISKA px — Panels kontrakt (ListBox-prejudikatet).
    return gfx::Sizef{w * c.scale, h * c.scale};
}

// TEXTENS YTA: fältets inre minus luften runt texten — det texten, markeringen
// och careten klipps mot. Luften RULLAR med texten (den ingår i
// `contentExtent`), men den TÄCKS aldrig: en rad som rullats halvvägs ut kapas
// vid luftens kant och inte vid ramen, så fältet ser lika prydligt ut överallt
// (det enradiga fältet kapar likadant i sidled), och texten hamnar aldrig under
// remsorna — `padRight`/`padBottom` är minst remsans tjocklek.
gfx::Rectf TextArea::textClip(const Ctx& c) const
{
    const gfx::Rectf in = innerBounds(c);
    const auto&      m  = *c.style;
    return gfx::Rectf{in.x + m.fieldPadX, in.y + m.fieldPadY,
                      std::max(0.0f, in.w - m.fieldPadX - padRight(c)),
                      std::max(0.0f, in.h - m.fieldPadY - padBottom(c))};
}

gfx::Pointf TextArea::origin(const Ctx& c) const
{
    const gfx::Rectf in    = innerBounds(c);
    const gfx::Sizef range = scrollRange(c);
    return gfx::Pointf{
        in.x + c.style->fieldPadX - std::clamp(scrollX(), 0.0f, range.w),
        in.y + c.style->fieldPadY - std::clamp(scrollY(), 0.0f, range.h)};
}

int TextArea::lineAt(const Layout& L, std::size_t p, bool upstream)
{
    const auto it = std::upper_bound(
        L.lines.begin(), L.lines.end(), p,
        [](std::size_t v, const Line& l) { return v < l.b; });
    const int i = std::max(0, int(it - L.lines.begin()) - 1);
    return upstream && hardWrapAt(L, i - 1, p) ? i - 1 : i;
}

bool TextArea::hardWrapAt(const Layout& L, int i, std::size_t p)
{
    return i >= 0 && i + 1 < int(L.lines.size()) &&
           L.lines[std::size_t(i)].e == p && L.lines[std::size_t(i) + 1].b == p;
}

float TextArea::xAt(const Ctx& c, const Layout& L, int i, std::size_t p) const
{
    const Line&       l = L.lines[std::size_t(i)];
    const std::size_t q = std::clamp(p, l.b, l.e);
    return textfield::tabbedPrefix(textCtx(c), text_.substr(l.b, l.e - l.b),
                                   q - l.b, L.tabW);
}

std::size_t TextArea::indexAt(const Ctx& c, const Layout& L, int i,
                              float x) const
{
    const Line& l = L.lines[std::size_t(i)];
    return l.b + textfield::tabbedIndexAtX(textCtx(c),
                                           text_.substr(l.b, l.e - l.b), x,
                                           L.tabW);
}

// Musen → position. Ovanför första raden är första raden och nedanför sista
// den sista; x läses på raden (bortom radslutet = radslutet, och då står
// careten på den ÖVRE raden vid en hårdbrytning).
std::size_t TextArea::indexAtPoint(const Ctx& c, float mx, float my,
                                   bool& upstream) const
{
    const Layout& L = layout(c);
    upstream        = false;
    if (L.lineH <= 0.0f)
        return caret();   // degenererad font — hellre stilla än div/0
    const gfx::Rectf  r = resolve(c);
    const gfx::Pointf o = origin(c);
    const int         i = std::clamp(
        int(std::floor((Draw::localY(c, r, my) - o.y) / L.lineH)), 0,
        int(L.lines.size()) - 1);
    const std::size_t p = indexAt(c, L, i, Draw::localX(c, r, mx) - o.x);
    upstream            = hardWrapAt(L, i, p);
    return p;
}

gfx::Rectf TextArea::caretBounds(const Ctx& c) const
{
    const Layout&     L = layout(c);
    const gfx::Pointf o = origin(c);
    const int         i = lineAt(L, caret(), upstream_);
    return gfx::Rectf{o.x + xAt(c, L, i, caret()), o.y + float(i) * L.lineH,
                      textfield::caretWidth(c), L.lineH};
}

gfx::Rectf TextArea::caretRect(const Ctx& c) const
{
    return Draw::project(c, resolve(c), snitt(caretBounds(c), textClip(c)));
}

// ------------------------------------------------------------- ritningen

// Raderna plus fältets luft och ramen (innerBounds drar in `panelInset`).
// Radhöjden är textstorlekens.
float TextArea::heightForRows(const Ctx& c, int rows) const
{
    const auto& m = *c.style;
    return float(rows) * Draw::lineHeight(textCtx(c)) +
           2.0f * (m.fieldPadY + (isBorder() ? m.panelInset : 0.0f));
}

gfx::Sizef TextArea::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN — per axel, och AUTO-måttet är stilens: bredden
        // `textAreaDefaultW`, höjden så många rader som `textAreaRows` +
        // fältets luft och ramen (`heightForRows`), så de raderna ryms utan
        // att fältet rullar.
        gfx::Sizef s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        const auto& m = *c.style;
        if (s.w <= 0.0f)
            s.w = m.textAreaDefaultW;
        if (s.h <= 0.0f)
            s.h = heightForRows(c, m.textAreaRows);
        return s;
    }
    }
}

void TextArea::draw(const Ctx& c)
{
    const gfx::Rectf b = c.bounds();
    switch (c.ui) {
    // ALLA TRE ritar FÄLTET med den DELADE ritningen (`textfield::
    // drawSurface`, ../text_field.cpp), och det är ett beslut och inte en
    // lucka: ett anteckningsfält är samma sorts fält som det enradiga —
    // samma yta, samma kant som bär tillståndet, samma skrivskyddade ton och
    // samma markeringspar per familj — och formen med de fyra tillståndens
    // tabeller står på ETT ställe. Det här fältet äger RADERNA. Remsorna
    // ritar Panel ovanpå (`drawAfterChildren`), i fältets högerkant där
    // `padRight` lämnar plats åt dem.
    case UI::Glaze:
    case UI::Minimalistic:
    case UI::Slate:
    default: {
        const textfield::Selection sel = textfield::drawSurface(
            c, b, isEnabled(), hovered(), readOnly_);
        drawLines(c, sel.fill, sel.text);
        break;
    }
    }

    // Fokusringen UTANFÖR fältet — samma som det enradigas.
    if (focused() && isEnabled())
        textfield::drawFocusRing(c, b);
}

// Raderna, markeringen och careten, klippt mot TEXTENS yta (`textClip`).
// Radloopen börjar vid första SYNLIGA raden och slutar vid nederkanten —
// O(synliga), aldrig O(n).
void TextArea::drawLines(const Ctx& c, gfx::Color selFill,
                         gfx::Color selText) const
{
    const Layout&     L    = layout(c);
    const gfx::Rectf  yta  = textClip(c);
    const gfx::Pointf o    = origin(c);
    const gfx::Color  tc   = textColor(c);
    const bool        mark = focused() && hasSelection();
    const std::size_t sb   = selBegin();
    const std::size_t se   = selEnd();
    const int         n    = int(L.lines.size());
    const textfield::Selection sel{selFill, selText};
    const Ctx         t    = textCtx(c);   // texten i fältets storlek
    // En markerad RADBRYTNING (eller blanksteget en brytning åt) syns som
    // ett blankstegsbrett block efter radens text — annars hade en markerad
    // tom rad inte synts alls.
    const float blank = mark ? Draw::measure(t, " ").w : 0.0f;

    Draw::clip(c, yta);
    const int första =
        L.lineH > 0.0f ? std::max(0, int(std::floor((yta.y - o.y) / L.lineH)))
                       : n;
    for (int i = första; i < n; ++i) {
        const float y = o.y + float(i) * L.lineH;
        if (y >= yta.y + yta.h)
            break;
        const Line&       l = L.lines[std::size_t(i)];
        const std::string s = text_.substr(l.b, l.e - l.b);
        const std::size_t b = mark ? std::clamp(sb, l.b, l.e) - l.b : 0;
        const std::size_t e = mark ? std::clamp(se, l.b, l.e) - l.b : 0;
        textfield::drawRun(t, {o.x, y, yta.x + yta.w - o.x, L.lineH}, o.x, s,
                           b, e, tc, sel, L.tabW);
        if (mark && sb <= l.e && se > l.e && i + 1 < n &&
            L.lines[std::size_t(i) + 1].b > l.e)
            Draw::rect(c,
                       {o.x + textfield::tabbedWidth(t, s, L.tabW), y, blank,
                        L.lineH},
                       0.0f, selFill);
    }

    // PLATSHÅLLAREN — i ett TOMT fält, också med
    // fokus, i `textDim` och från textens origo: radbruten mot textens
    // bredd med `wrap` (blocket toppställt — recten är exakt dess höjd), annars
    // kortad på första raden. Klippet ovan gäller; rullningen räknar den inte.
    if (text_.empty() && !placeholder_.empty()) {
        const std::string& ph  = placeholder_.str();
        const float        w   = yta.x + yta.w - o.x;
        const gfx::Color   dim = c.theme->colors.textDim;
        if (wrap_)
            Draw::textWrapped(t, {o.x, o.y, w, Draw::measureWrapped(t, ph, w).h},
                              ph, dim);
        else
            Draw::textElided(t, {o.x, o.y, w, L.lineH},
                             textfield::singleLine(ph.c_str()), dim);
    }

    // Careten ritas i caretBounds — SAMMA rect som blink-damagen
    // invalidaterar (en källa).
    if (caretVisible() && isEnabled())
        Draw::rect(c, caretBounds(c), 0.0f, tc);
    Draw::endClip(c);
}

// Blinkfasen drivs av AppWindow (caret-timern). Bara caret-recten
// invalidateras — appen sover mellan blinkningarna och ritar en handfull
// pixlar per tick (regel 1). En bortrullad caret ritar inte om något.
void TextArea::caretBlink(const Ctx& c, bool on)
{
    if (caretOn_ == on)
        return;
    caretOn_ = on;
    if (!focused_)
        return;
    const gfx::Rectf r = caretRect(c);
    if (r.w > 0.0f && r.h > 0.0f)
        invalidateRect(r);
}

// ------------------------------------------------------------- beteendet

std::optional<Ctx> TextArea::openCtx() const
{
    if (!appWindow() || !appWindow()->isOpen())
        return std::nullopt;
    return behaviorCtx();
}

// Håll caretRADEN innanför fältets luft — lodrätt alltid, vågrätt utan
// brytning. Rullar bara så mycket som behövs, och invaliderar bara vid
// faktisk ändring (`setScroll`).
void TextArea::scrollToCaret(const Ctx& c)
{
    const Layout&    L     = layout(c);
    const gfx::Rectf in    = innerBounds(c);
    const auto&      m     = *c.style;
    const gfx::Sizef range = scrollRange(c);
    const int        i     = lineAt(L, caret(), upstream_);

    float       sy  = std::clamp(scrollY(), 0.0f, range.h);
    const float upp = float(i) * L.lineH;
    const float ner = m.fieldPadY + float(i + 1) * L.lineH + padBottom(c) - in.h;
    if (sy > upp)
        sy = upp;
    else if (sy < ner)
        sy = ner;

    float sx = std::clamp(scrollX(), 0.0f, range.w);
    if (!wrap_) {
        const float cx = xAt(c, L, i, caret());
        const float hö = m.fieldPadX + cx + textfield::caretWidth(c) +
                         padRight(c) - in.w;
        if (sx > cx)
            sx = cx;
        else if (sx < hö)
            sx = hö;
    }
    setScroll(std::clamp(sx, 0.0f, range.w), std::clamp(sy, 0.0f, range.h));
}

void TextArea::follow()
{
    if (const std::optional<Ctx> c = openCtx())
        scrollToCaret(*c);
}

// Caret synlig: ny blinkperiod (careten ska inte blinka bort mitt i
// navigering eller skrivande) och widgetens yta ritas om.
void TextArea::showCaret()
{
    caretOn_ = true;
    restartCaretBlink();
    invalidate();
}

// Caret FLYTTAD utan textändring — tangent, mus, `setSelection`. En flytt
// bryter historikens grupp.
void TextArea::movedCaret()
{
    sealHistory();
    showCaret();
}

// Text ändrad av användaren: careten synlig och i synfältet, och sist
// onChange (värdet läses ur sender — args bär ingen nyttolast). Förseglar
// INTE: nästa tecken ska kunna fortsätta samma steg.
void TextArea::edited()
{
    showCaret();
    follow();
    if (onChange) {
        ChangeArgs e;
        onChange(this, e);
    }
}

void TextArea::moveTo(std::size_t p, bool shift, bool upstream)
{
    caret_    = p;
    upstream_ = upstream;
    goalX_    = -1.0f;
    if (!shift)
        anchor_ = p;
    movedCaret();
    follow();
}

// Upp/Ner en VISUELL rad, PageUp/PageDown en sida. X-läget BEVARAS: det
// första steget minns var careten stod (`goalX_`), och varje följande steg
// siktar dit — en kort rad emellan flyttar inte målet. Kräver raderna, alltså
// ett öppet fönster; annars är tangenten inte vår.
bool TextArea::moveLines(int dir, bool page, bool shift)
{
    const std::optional<Ctx> c = openCtx();
    if (!c)
        return false;
    const Layout& L = layout(*c);
    if (L.lineH <= 0.0f)
        return false;
    const int i    = lineAt(L, caret(), upstream_);
    int       steg = 1;
    if (page) {
        // En sida = de rader som syns HELT mellan luften. En rad som skyms
        // mindre än en FYSISK px syns helt — caretens rasterminimum
        // (`textfield::caretWidth`) — och det är det som gör att ett fält
        // med AUTO-höjden inte tappar en rad på en flyttalsavrundning.
        const float synligt = innerBounds(*c).h - c->style->fieldPadY -
                              padBottom(*c) + 1.0f / c->scale;
        steg = std::max(1, int(std::floor(synligt / L.lineH)));
    }
    const int j = std::clamp(i + dir * steg, 0, int(L.lines.size()) - 1);
    if (goalX_ < 0.0f)
        goalX_ = xAt(*c, L, i, caret());
    const std::size_t p = indexAt(*c, L, j, goalX_);
    caret_    = p;
    upstream_ = hardWrapAt(L, j, p);
    if (!shift)
        anchor_ = p;
    if (page) {
        // Vyn följer med lika många rader, så careten står kvar på samma
        // plats på skärmen (Windows-fältets sidbläddring).
        const gfx::Sizef range = scrollRange(*c);
        setScroll(scrollX(),
                  std::clamp(std::clamp(scrollY(), 0.0f, range.h) +
                                 float(j - i) * L.lineH,
                             0.0f, range.h));
    }
    movedCaret();
    scrollToCaret(*c);
    return true;
}

// Home/End — den VISUELLA radens början och slut. End vid en hårdbrytning
// står på den ÖVRE raden: annars hade den hamnat först på nästa.
bool TextArea::moveLineEdge(bool end, bool shift)
{
    const std::optional<Ctx> c = openCtx();
    if (!c)
        return false;
    const Layout& L = layout(*c);
    const int     i = lineAt(L, caret(), upstream_);
    const Line&   l = L.lines[std::size_t(i)];
    if (end)
        moveTo(l.e, shift, hardWrapAt(L, i, l.e));
    else
        moveTo(l.b, shift);
    return true;
}

// DEN ENDA REDIGERINGSVÄGEN: steget registreras med
// markeringen FÖRE ändringen, och själva ändringen är `replaceRaw`.
void TextArea::replace(std::size_t b, std::size_t e, const std::string& ins,
                       textfield::EditKind kind)
{
    history().record(b, text_.substr(b, e - b), ins, clampIdx(anchor_),
                     caret(), kind);
    replaceRaw(b, e, ins);
}

void TextArea::replaceRaw(std::size_t b, std::size_t e, const std::string& ins)
{
    text_.replace(b, e - b, ins);
    caret_ = anchor_ = b + ins.size();
    upstream_        = false;
    goalX_           = -1.0f;
    relayout(b, e, b + ins.size());
}

// Ångra/gör om utför sitt svar genom `replaceRaw` — den INKREMENTELLA
// layouten följer med (en hel omläggning hade gett samma rader, men inte
// samma kostnad) — och sätter därefter stegets markering. `edited()` rullar
// till careten och fyrar `onChange`.
void TextArea::restore(const textfield::Restore& r)
{
    replaceRaw(r.pos, r.pos + r.len, r.text);
    anchor_   = r.anchor;
    caret_    = r.caret;
    upstream_ = false;
    goalX_    = -1.0f;
    edited();
}

// `ins` är redan FLERRADSFILTRERAD (skrivandet, inklistringen, Enters `\n`) —
// här kapas den bara till MAXLÄNGDEN, i en egen variabel så att
// radbrytningarna står kvar och historiken registrerar exakt det som sattes
// in. Ryms inget av en icke-tom
// insättning händer ingenting — Enter vid gränsen ger ingen rad.
void TextArea::insertText(const std::string& ins, textfield::EditKind kind)
{
    const std::string ryms = textfield::fitMaxLength(
        text_, selBegin(), selEnd(), ins, static_cast<std::size_t>(maxLength_));
    if ((!ins.empty() && ryms.empty()) || (ryms.empty() && !hasSelection())) {
        showCaret();   // inget ändrades — men careten ska synas
        return;
    }
    replace(selBegin(), selEnd(), ryms, kind);
    edited();
}

bool TextArea::textInput(const char* utf8)
{
    // SKRIVSKYDDAT: tecknet sväljs — fältet HAR fokus och tecknet ska inte
    // studsa vidare (TextInput-prejudikatet).
    if (readOnly_)
        return true;
    insertText(textfield::multiLine(utf8), textfield::EditKind::Typing);
    return true;
}

bool TextArea::keyDown(gfx::Key k, std::uint32_t mods)
{
    const bool shift = mods & gfx::kModShift;
    const bool ctrl  = mods & gfx::kModCtrl;   // Ctrl+Home/End — AltGr
                                               // skriver inget tecken där
    // REDIGERINGSGENVÄGARNA (A/C/X/V/Z/Y) är plattformens — Ctrl UTAN Alt,
    // på macOS Cmd utan Ctrl och Option (`textfield::editShortcut`).
    const bool kort = textfield::editShortcut(mods);

    // SKRIVSKYDDAT: allt som ÄNDRAR texten spärras, allt som LÄSER den
    // behålls — markera och kopiera är hela poängen (TextInput-kontraktet).
    // Enter har ingen rad att infoga och är då inte vår. Ctrl+Z/Ctrl+Y
    // faller igenom och konsumeras som no-op (`canUndo` är false här).
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
        case gfx::Key::Return:
            return false;
        default:
            break;
        }

    switch (k) {
    case gfx::Key::Left: {
        // Utan Shift kollapsar en markering till sin vänsterkant (som
        // Windows); Shift flyttar caret och låter ankaret stå kvar.
        const std::size_t i = !shift && hasSelection()
                                  ? selBegin()
                                  : textfield::prevBoundary(text_, caret());
        moveTo(i, shift);
        return true;
    }
    case gfx::Key::Right: {
        const std::size_t i = !shift && hasSelection()
                                  ? selEnd()
                                  : textfield::nextBoundary(text_, caret());
        moveTo(i, shift);
        return true;
    }
    case gfx::Key::Up:
        return moveLines(-1, false, shift);
    case gfx::Key::Down:
        return moveLines(+1, false, shift);
    case gfx::Key::PageUp:
        return moveLines(-1, true, shift);
    case gfx::Key::PageDown:
        return moveLines(+1, true, shift);
    case gfx::Key::Home:
        if (ctrl) {
            moveTo(0, shift);
            return true;
        }
        return moveLineEdge(false, shift);
    case gfx::Key::End:
        if (ctrl) {
            moveTo(text_.size(), shift);
            return true;
        }
        return moveLineEdge(true, shift);

    // ENTER ger en ny rad. Med Ctrl/Alt/Cmd är tangenten inte vår —
    // Ctrl+Enter är en vanlig genväg ("skicka") och ska nå appen. Radbrytningen
    // är SKRIVANDE för historiken: den hör till ordet före, och ordet efter
    // börjar ett nytt steg.
    case gfx::Key::Return:
        if (mods & (gfx::kModCtrl | gfx::kModAlt | gfx::kModCmd))
            return false;
        insertText("\n", textfield::EditKind::Typing);
        return true;

    // RADERINGEN — en markering är ett eget steg, backsteg och Delete i följd
    // är var sitt (TextInputs sorter).
    case gfx::Key::Backspace:
        if (hasSelection()) {
            replace(selBegin(), selEnd(), std::string(),
                    textfield::EditKind::Block);
            edited();
        } else if (caret() > 0) {
            replace(textfield::prevBoundary(text_, caret()), caret(),
                    std::string(), textfield::EditKind::BackDelete);
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
            replace(caret(), textfield::nextBoundary(text_, caret()),
                    std::string(), textfield::EditKind::FwdDelete);
            edited();
        } else {
            showCaret();
        }
        return true;

    // URKLIPPET (alltid UTF-8 och LF genom gfx-fasaden, som översätter till
    // OS:ets radslut). Utan genvägen faller bokstäverna igenom — själva
    // tecknet kommer som TextInput-event.
    case gfx::Key::C:
        if (!kort)
            return false;
        if (hasSelection())
            gfx::setClipboardText(
                text_.substr(selBegin(), selEnd() - selBegin()));
        return true;
    case gfx::Key::X:
        if (!kort)
            return false;
        if (hasSelection()) {
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
        // Inklistrade tabbar behålls med `acceptsTab` — annars blanksteg.
        insertText(textfield::multiLine(gfx::clipboardText().c_str(),
                                        acceptsTab_),
                   textfield::EditKind::Block);
        return true;
    case gfx::Key::A:
        if (!kort)
            return false;
        selectAll();
        follow();   // användarens markering följs — den programmatiska inte
        return true;

    // ÅNGRA och GÖR OM — tangenterna anropar de publika metoderna och är
    // KONSUMERADE också när inget finns att göra (TextInputs regel).
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

    // TAB SKRIVER EN TABB med `acceptsTab` (WinForms
    // `AcceptsTab`) — bara utan modifierare: Shift+Tab är "tillbaka" överallt
    // och Ctrl+Tab flikkontrollens, så båda flyttar fokus. Ett
    // skrivskyddat fält släpper den (inget att skriva i).
    case gfx::Key::Tab:
        if (!acceptsTab_ || readOnly_ ||
            (mods & (gfx::kModShift | gfx::kModCtrl | gfx::kModAlt |
                     gfx::kModCmd)))
            return false;
        insertText("\t", textfield::EditKind::Typing);
        return true;

    default:
        // Escape/Space m.fl. är inte våra (och Tab utan `acceptsTab` ovan):
        // Tab ska vandra fokus, Space kommer som TextInput-event (mellanslag
        // är text här).
        return false;
    }
}

void TextArea::mouseDown(const Ctx& c, float mx, float my)
{
    const bool remsa = inScrollStrip(c, mx, my);
    Panel::mouseDown(c, mx, my);   // tumgrepp/rännklick — OCH nollar
                                   // dragläget även utanför remsan
    textDrag_ = !remsa;
    if (remsa)
        return;
    bool upp = false;
    caret_ = anchor_ = indexAtPoint(c, mx, my, upp);
    upstream_        = upp;
    goalX_           = -1.0f;
    // Ett tryck som GAV fokus HÅLLS — `TextInput::mouseDown`s regel
    // och skäl: draget väntar ut dragtröskeln, så `onFocus → selectAll()`
    // står kvar för en darrig hand.
    const gfx::Rectf base = resolve(c);
    holdPress_  = pressGaveFocus();
    pressIndex_ = caret_;
    pressX_     = Draw::localX(c, base, mx);
    pressY_     = Draw::localY(c, base, my);
    movedCaret();
    scrollToCaret(c);
}

// Draget markerar från ankaret — och en pekare bortom fältets kant ger en
// position på en rad UTANFÖR synfältet, så `scrollToCaret` rullar mot den:
// det är autoscrollen, driven av musens rörelse. Ett HÅLLET tryck blir ett
// drag först bortom tröskeln och ankras då om vid trycket (TextInputs regel).
void TextArea::mouseDrag(const Ctx& c, float mx, float my)
{
    if (!textDrag_) {
        Panel::mouseDrag(c, mx, my);   // tumdraget
        return;
    }
    bool ankratOm = false;
    if (holdPress_) {
        const gfx::Rectf base = resolve(c);
        const float      dx   = Draw::localX(c, base, mx) - pressX_;
        const float      dy   = Draw::localY(c, base, my) - pressY_;
        const float      tol  = c.style->dragThreshold;
        if (dx * dx + dy * dy <= tol * tol)
            return;
        holdPress_            = false;
        const std::size_t ank = textfield::floorBoundary(
            text_, std::min(pressIndex_, text_.size()));
        ankratOm = ank != anchor_;   // en ändring också om careten står still
        anchor_  = ank;
    }
    bool              upp = false;
    const std::size_t p   = indexAtPoint(c, mx, my, upp);
    if (p == caret_ && upp == upstream_) {
        if (ankratOm)
            movedCaret();
        return;
    }
    caret_    = p;   // ankaret står kvar — markeringen växer med draget
    upstream_ = upp;
    goalX_    = -1.0f;
    movedCaret();
    scrollToCaret(c);
}

void TextArea::mouseMove(const Ctx& c, float mx, float my)
{
    Panel::mouseMove(c, mx, my);   // het tumme (invaliderar vid ändring)
}

// DUBBELKLICKET kommer efter det andra trycket, som redan placerat careten
// där pekaren stod — ordet kring den markeras.
void TextArea::mouseDoubleClick(const Ctx& c, float, float)
{
    if (!textDrag_)
        return;   // dubbelklick i en remsa är två rännklick, inget mer
    std::size_t b = 0;
    std::size_t e = 0;
    textfield::wordAt(text_, caret(), b, e);
    if (b == e)
        return;
    const Layout& L = layout(c);
    anchor_         = b;
    caret_          = e;
    upstream_       = hardWrapAt(L, lineAt(L, e, false) - 1, e);
    goalX_          = -1.0f;
    movedCaret();
    scrollToCaret(c);
}

gfx::PointerShape TextArea::cursorAt(const Ctx& c, float mx, float my) const
{
    return inScrollStrip(c, mx, my) ? gfx::PointerShape::Arrow
                                    : gfx::PointerShape::IBeam;
}

} // namespace glaze
