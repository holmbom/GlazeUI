// ToolButton — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER: Draw ritar, c.style bär måtten och temat färgerna — inga tal och
// inga färger i den här filen. Mallen är button.cpp; skillnaden är att
// verktygsknappen INTE har någon yta i vila (remsan är ytan), aldrig tar
// fokus, och kan vara ett LÄGE (checkable) i stället för en handling.
//
// RECEPTET ÄR MENYRADENS i vila och under pekaren: ingen platta i vila, en
// SVAG platta (`buttonHover`) under pekaren och `buttonPress` vid tryck.
//
// IKRYSSAT LÄGE ÄR EN INTRYCKT PLATTA MED ACCENTKANT — och INTE den fyllda
// accentytan som menyradens öppna rubrik bär. Den håller inte när det renderas:
// ikonerna är färgade PNG:er, och en blå info-ikon på
// Slates blå `accentSurface` försvann helt. WinUI kommer undan med fylld
// accent för att dess ikoner är enfärgade glyfer som färgas om till
// `TextOnAccent`; våra kan inte färgas om. Plattan i `buttonPress` stör ingen
// kulör, och läget bärs av KANTEN i `accent` — den roll som är grindad
// ≥ 3:1 mot bakgrunden just som tillståndsmarkering
// (tests/contrast_gate.cpp, "accent/bg"). Texten står kvar i `text`.
//
// RULLGARDINEN: med menyposter delas ytan i INNEHÅLLET och
// PILENS RUTA längst till höger (`toolArrowW`, pilens tecken
// `toolArrowGlyph`). Pilen står i textens färg — den hör till etiketten — och
// den delade knappens skiljelinje i `border` hör till PLATTAN: den syns när
// plattan syns. En öppen meny ritar knappen INTRYCKT, som menyradens öppna
// rubrik och programikonens platta säger samma sak. Menyn är data i en
// `Menu`, visad med `popupAtRect` under knappen, och den platta formens
// stigupplösning är den delade i src/menu_path.h, med den TOMMA stigen som
// roten — samma som programikonens och systemfältets.

#include "glaze/widgets/tool_button.h"

#include <algorithm>

#include "glaze/app_window.h"
#include "glaze/draw.h"

#include "../menu_path.h"
#include "../prop_parse.h"

namespace glaze {

// Loggens avsändare i menystigens hjälpare (src/menu_path.h).
constexpr const char* kVem = "ToolButton";   // lang_gate:rad loggens avsändare, ingen användartext

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("toolbutton", ToolButton)

ToolButton::ToolButton(TrText t) : text_(std::move(t))
{
    // Kedjan stängdes (klick utanför, Escape, en aktivering): släck plattan
    // — men bara när INGEN presentation lever längre. Ett snabbt tryck till
    // kan öppna en ny kedja medan den gamla fortfarande står på tur att
    // rivas, och då river `showOwned` den gamla synkront mitt i öppningen;
    // dess besked får inte släcka den nya (`finish` har redan tagit bort den
    // gamla ur menyns lista när kroken körs).
    menu_.onChainClosed = [this] {
        if (open_ && !menu_.isOpen()) {
            open_ = false;
            invalidate();
        }
    };
}

bool ToolButton::setProp(const std::string& key, const std::string& value)
{
    // Inget `title`-alias: det är ett ARV från äldre filer (regel 4), och
    // en ny widget har inga äldre filer att vara snäll mot.
    if (key == "text") {
        setText(value);
        return true;
    }
    if (key == "img") {
        AssetHandle h;
        if (!propparse::parseImg(value, h))
            return false;
        setImg(h);
        return true;
    }
    if (key == "imagePos" || key == "imagepos") {
        if (value == "left")       setImagePos(ImagePos::Left);
        else if (value == "right") setImagePos(ImagePos::Right);
        else if (value == "above") setImagePos(ImagePos::Above);
        else if (value == "below") setImagePos(ImagePos::Below);
        else return false;
        return true;
    }
    if (key == "checkable") {
        setCheckable(propparse::parseBool(value));
        return true;
    }
    if (key == "checked") {
        setChecked(propparse::parseBool(value));
        return true;
    }
    if (key == "split") {
        setSplit(propparse::parseBool(value));
        return true;
    }
    if (key == "iconSize") {
        float v = 0.0f;
        if (propparse::parseNumber(value, v))
            setIconSize(v);   // negativt ⇒ sättarens logg + no-op
        else
            gfx::log("ToolButton(%s): ogiltig iconSize: %s (ignoreras)",
                     name.c_str(), value.c_str());
        return true;
    }
    return Widget::setProp(key, value);   // basen SIST — konventionen
}

void ToolButton::setIconSize(float px)
{
    if (!(px >= 0.0f)) {
        gfx::log("ToolButton(%s): setIconSize(%g) — 0 är AUTO, ignoreras",
                 name.c_str(), double(px));
        return;
    }
    if (iconSize_ == px)
        return;
    iconSize_ = px;
    invalidate();
    relayoutSiblings();   // AUTO-måtten ändrades, som av bilden
}

// Hover, tryck eller öppen meny på en aktiv knapp — det som tänder plattan.
// `pressed()` räcker inte ensam: trycket kan dras utanför knappen, och då ska
// plattan fortsätta säga "det här är knappen du håller i" — och en öppen meny
// säger "det här är knappen menyn hänger i" tills den stängs.
bool ToolButton::hot() const
{
    return isEnabled() && (hovered() || down());
}

// Inaktiv ⇒ ikonen med sänkt OPACITET (1 − `iconFade`), tonad mot vad som
// än ligger under — remsan, en panel, en gruppram (se headern).
Content ToolButton::content(const Ctx& c, gfx::Color fg) const
{
    // En SATT ikonstorlek vinner; annars är
    // storleksklassen 0 för en bild ur pixlar.
    const float k = iconSize_ > 0.0f ? iconSize_ : img_.sizeClass();

    Content ct{img_, text(), fg, imagePos_, c.style->iconGap,
               k > 0.0f ? k : c.style->iconFallback};
    if (!isEnabled())
        ct.imgOpacity = 1.0f - c.style->iconFade;
    return ct;
}

// Pilens ruta längst till höger, aldrig bredare än knappen själv — en satt
// bredd under `toolArrowW` ger pilen hela knappen och innehållet ingenting.
gfx::Rectf ToolButton::arrowRect(const Ctx& c, const gfx::Rectf& b) const
{
    const float w = std::min(c.style->toolArrowW, b.w);
    return gfx::Rectf{b.x + b.w - w, b.y, w, b.h};
}

gfx::Sizef ToolButton::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN: satt axel vinner, axel <= 0 är AUTO = innehållet +
        // stilens padding. I en `ToolBar` frågas bara BREDDEN — remsan
        // ger alla sina barn sin innerhöjd (Bar-flexen) — men knappen ska
        // ha ett ärligt mått också när den står fritt.
        //
        // Delas av alla tre ritningarna med flit, som Buttons: en kopia
        // per UI hade kunnat glida isär. Färgen spelar ingen roll för
        // mätningen, så färgargumentet är tomt.
        //
        // MED POSTER växer AUTO-bredden med pilens ruta (`toolArrowW`), så
        // innehållet behåller sin luft på båda sidor; en satt bredd vinner
        // och pilen tar sin ruta ur den.
        gfx::Sizef s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        const gfx::Sizef m = Draw::measure(c, content(c, {}));
        if (s.w <= 0.0f)
            s.w = m.w + 2.0f * c.style->toolButtonPadX +
                  (showsArrow() ? c.style->toolArrowW : 0.0f);
        if (s.h <= 0.0f) s.h = m.h + 2.0f * c.style->toolButtonPadY;
        return s;
    }
    }
}

void ToolButton::draw(const Ctx& c)
{
    const gfx::Rectf b   = c.bounds();
    const auto&      m   = *c.style;
    const auto&      col = c.theme->colors;
    const bool       på  = isEnabled();

    // Plattan finns när knappen är aktiv OCH antingen tänd eller ikryssad.
    // Fyllningen följer pekaren: tryck eller öppen meny > hover > (ikryssad
    // i vila = intryckt).
    const bool       platta = på && (checked_ || hot());
    const gfx::Color fyll   = hovered() && !down() ? col.buttonHover
                                                   : col.buttonPress;

    // Texten — samma i alla tre ritningarna. (Ikonens tonning när knappen
    // är inaktiv sköter `content`.)
    const gfx::Color fg = på ? col.text : col.textDim;

    // MED POSTER delas ytan: innehållet till vänster och pilens ruta längst
    // till höger. Utan poster — eller utan pil (`showsArrow`) — är innehållet
    // hela knappen.
    const gfx::Rectf pil  = arrowRect(c, b);
    const gfx::Rectf inne = showsArrow()
                                ? gfx::Rectf{b.x, b.y, pil.x - b.x, b.h}
                                : b;

    switch (c.ui) {
    // GLAZE — den TÄNDA knappen är en Glaze-knapp: laminerad platta med
    // bestämd kontur. I vila finns ingen platta
    // alls, och då behövs heller ingen kontur — "kontur överallt" gäller
    // YTOR, och en knapp i vila har ingen. Trycket tar bort lamineringen
    // (knappen ligger platt), precis som på Button.
    //
    // De fyra tillstånden (princip 7):
    //   vila       ingen platta — ikon och text på remsans yta
    //   hover      `buttonHover` + laminering + kontur `buttonBorder`
    //   tryck      `buttonPress` platt + kontur `buttonBorder` (också medan
    //              menyn står öppen)
    //   ikryssad   `buttonPress` (hover: `buttonHover` + laminering) med
    //              konturen i `accent` — läget bärs av KANTEN
    //   inaktiv    ingen platta (ikryssad: `buttonDisabled` + kontur
    //              `border`), text `textDim`, ikonen tonad mot ytan
    case UI::Glaze:
        if (platta) {
            Draw::rect(c, b, m.cornerRadius, fyll);
            if (hovered() && !down())
                Draw::laminate(c, b, m.cornerRadius, col.sheen, m.sheenLower,
                               col.sheenHigh, m.sheenUpper);
            Draw::frame(c, b, m.cornerRadius,
                        Pen{checked_ ? col.accent : col.buttonBorder,
                            m.borderWidth});
        } else if (checked_) {
            Draw::rect(c, b, m.cornerRadius, col.buttonDisabled,
                       Pen{col.border, m.borderWidth});
        }
        Draw::content(c, inne, content(c, fg));
        break;

    // MINIMALISTIC — "Outline": ingen yta i vila, KANTEN är formen och
    // FYLLNING BETYDER TILLSTÅND. Hover är
    // kanten plus en svag fyllning; tryck och ikryssat bär kanten i
    // `accent` (Buttons tryck) över den mörkare fyllningen.
    //
    // De fyra tillstånden (princip 7):
    //   vila       ingenting — ikon och text på remsans yta
    //   hover      `buttonHover` innanför kanten `buttonBorder`
    //   tryck      `buttonPress` innanför kanten `accent` (också medan
    //              menyn står öppen)
    //   ikryssad   `buttonPress` (hover: `buttonHover`) innanför `accent`
    //   inaktiv    ingen fyllning — familjens regel; ikryssad behåller
    //              KANTEN i `border`, så läget syns även när det inte går
    //              att ändra
    case UI::Minimalistic:
        if (platta)
            Draw::rect(c, b, m.cornerRadius, fyll,
                       Pen{checked_ || down() ? col.accent : col.buttonBorder,
                           m.borderWidth});
        else if (checked_)
            Draw::frame(c, b, m.cornerRadius, Pen{col.border, m.borderWidth});
        Draw::content(c, inne, content(c, fg));
        break;

    // SLATE — en PLATTA som bara finns när den behövs: menyradens
    // rubrik-recept under pekaren, och den intryckta
    // plattan med accentkant för det som varar. Ingen kontur i övrigt och
    // inget lyft — Slates lyft (raise*) är KNAPPENS, och en verktygsknapp
    // som lyfte sig ur remsan vid hover hade ritat en skugga in i grannarna.
    //
    // De fyra tillstånden (princip 7):
    //   vila       ingen platta
    //   hover      `buttonHover`
    //   tryck      `buttonPress` (också medan menyn står öppen)
    //   ikryssad   `buttonPress` (hover: `buttonHover`) + kanten `accent`
    //   inaktiv    ingen platta (ikryssad: `buttonDisabled` + kanten
    //              `border`), text `textDim`, ikonen tonad mot ytan
    case UI::Slate:
    default:   // okänd ritning faller till Slate
        if (platta) {
            Draw::rect(c, b, m.cornerRadius, fyll);
            if (checked_)
                Draw::frame(c, b, m.cornerRadius,
                            Pen{col.accent, m.borderWidth});
        } else if (checked_) {
            Draw::rect(c, b, m.cornerRadius, col.buttonDisabled,
                       Pen{col.border, m.borderWidth});
        }
        Draw::content(c, inne, content(c, fg));
        break;
    }

    // PILEN och den DELADE knappens skiljelinje — samma i alla tre
    // ritningarna. Linjen delar en yta och syns därför bara där en yta finns
    // (plattan, eller den ikryssade knappens intryckta yta); pilen står i
    // textens färg, centrerad i sin ruta. En knapp utan pil ritar ingen av
    // dem.
    if (!showsArrow())
        return;
    if (split_ && (platta || checked_))
        Draw::line(c, pil.x, b.y, pil.x, b.y + b.h,
                   Pen{col.border, m.borderWidth});
    const float g = m.toolArrowGlyph;
    Draw::chevron(c,
                  {pil.x + (pil.w - g) / 2.0f, pil.y + (pil.h - g) / 2.0f, g, g},
                  Dir::Down, Pen{fg, m.borderWidth});
}

// TRYCKET. Med poster är det MENYNS — var som helst på en odelad knapp, bara
// i pildelen på en delad — och då blir det inget klick (`clicked()` läser
// `menuPress_`). Ett tryck i huvuddelen av en delad knapp är ett vanligt
// tryck. Pildelen är SAMMA ruta som ritningen (`arrowRect`), projicerad — och
// utan pil (`showsArrow`) finns ingen pildel att dela knappen med.
void ToolButton::mouseDown(const Ctx& c, float mx, float)
{
    menuPress_ = false;
    if (!hasMenu())
        return;
    if (split_ && showsArrow()) {
        const gfx::Rectf r = resolve(c);
        if (Draw::localX(c, r, mx) < arrowRect(c, Draw::bounds(c, r)).x)
            return;   // huvuddelen — ett vanligt tryck
    }
    menuPress_ = true;
    // TOGGLE i menyradens ordning: `App::dispatch` har redan köat
    // popup-stängningen när trycket når oss, så `open_` är fortfarande satt
    // och vägen blir stängning — inte en omedelbar återöppning.
    if (open_) {
        closeMenu();
        return;
    }
    // `onDropDown` FÖRE öppningen — handlern får fylla menyn (systemfältets
    // `onContextMenu`). Den får också riva eller flytta knappen, och då rörs
    // den inte mer; efter en körd handler ritas fönstret om (klickets
    // garanti — handlern kan ha ändrat vad som helst).
    if (onDropDown) {
        if (!fireEvent(onDropDown))
            return;
        if (AppWindow* w = appWindow())
            w->invalidate();
    }
    openMenu();   // bara om menyn fortfarande har poster
}

// ANVÄNDARENS klick. Växlingen sker FÖRE `onClick` och fyrar `onChange`
// först — Checkboxens ordning, så båda handlers ser det nya läget.
// Programmatisk `setChecked` går aldrig hit och fyrar därför aldrig något.
void ToolButton::clicked()
{
    // MENYNS TRYCK blir inget klick: det öppnade eller stängde menyn, och en
    // växlingsbar knapp växlar inte av ett tryck på pilen.
    if (menuPress_)
        return;
    fireClick();
}

// PROGRAMMATISKT — klicket utan trycket: `menuPress_` hör till musens
// tryck och läses inte här, så ett gammalt menytryck aldrig sväljer det.
void ToolButton::performClick()
{
    if (isEnabled())
        fireClick();
}

void ToolButton::fireClick()
{
    if (checkable_) {
        checked_ = !checked_;
        invalidate();
        // onChange och onCommit för samma växling — och `onClick`
        // bara om knappen går att röra efteråt (`firePair`).
        if (!firePair(onChange, onCommit))   // nya läget läses ur sender
            return;
    }
    Widget::clicked();
}

// ------------------------------------------------------------------ menyn

void ToolButton::openMenu()
{
    if (!hasMenu() || open_ || !isEnabled())
        return;   // ingen meny, redan öppen eller inaktiverad ⇒ ingenting
    if (!appWindow() || !appWindow()->isOpen())
        return;
    // Ankaret är HELA knappens rect (fysiska px i fönstrets klientyta):
    // backenden lägger menyn under den, vänsterjusterad med knappen — också
    // för en delad knapp. `open_` sätts FÖRE öppningen av MenuBar::openMenus
    // skäl (en gammal kedjas `onChainClosed` körs synkront i `showOwned`)
    // och tas tillbaka om menyn inte kom upp.
    const Ctx          c = behaviorCtx();
    const HandlerGuard vakt(*this);
    open_ = true;
    menu_.popupAtRect(*this, resolve(c));
    // Öppningen stänger andra popuper SYNKRONT, och deras `onClose` är
    // användarkod som får riva knappen (TrayIcons kontroll efter
    // `popupAtRect`) — då rörs den inte mer.
    if (!vakt.ok())
        return;
    if (!menu_.isOpen())
        open_ = false;
    invalidate();
}

void ToolButton::closeMenu()
{
    if (!open_)
        return;
    menu_.close();   // uppskjutet
    open_ = false;
    invalidate();
}

// En inaktiverad verktygsknapp har ingen öppen meny — som färgväljarens
// popup. Stängningen är uppskjuten till pump-svansen, som all annan.
void ToolButton::enabledChanged()
{
    if (!isEnabled())
        closeMenu();
}

void ToolButton::setSplit(bool on)
{
    if (split_ == on)
        return;
    split_ = on;
    invalidate();   // skiljelinjen och trycket — bredden är densamma
}

// ------------------------------------------------------ DEN PLATTA FORMEN
// Systemfältsikonens vokabulär (hård regel 8) med den TOMMA stigen som roten
// och utan genväg — hela stigen stiger ned från menyns egna poster.

void ToolButton::itemsAdded(bool hadItems)
{
    invalidate();
    // Den FÖRSTA posten ger knappen pilens ruta, och syskonen i remsan
    // flyttar — de är inte dirty (Widget::relayoutSiblings).
    if (!hadItems)
        relayoutSiblings();
}

void ToolButton::addItem(const MenuPath& path, TrText text)
{
    const bool hade = hasMenu();
    if (detail::addMenuItem(detail::descendMenu(menu_.items, path, 0, kVem),
                            kVem, path, std::move(text), std::string()))
        itemsAdded(hade);
}

void ToolButton::addCheck(const MenuPath& path, TrText text, bool checked)
{
    const bool hade = hasMenu();
    if (detail::addMenuCheck(detail::descendMenu(menu_.items, path, 0, kVem),
                             kVem, path, std::move(text), checked))
        itemsAdded(hade);
}

void ToolButton::addRadio(const MenuPath& path, TrText text, bool checked)
{
    const bool hade = hasMenu();
    if (detail::addMenuRadio(detail::descendMenu(menu_.items, path, 0, kVem),
                             kVem, path, std::move(text), checked))
        itemsAdded(hade);
}

void ToolButton::addSeparator(const MenuPath& path)
{
    const bool hade = hasMenu();
    if (detail::addMenuSeparator(
            detail::descendMenu(menu_.items, path, 0, kVem), kVem, path))
        itemsAdded(hade);
}

void ToolButton::addSubmenu(const MenuPath& path, TrText text)
{
    const bool hade = hasMenu();
    if (detail::addMenuSubmenu(
            detail::descendMenu(menu_.items, path, 0, kVem), kVem, path,
            std::move(text)))
        itemsAdded(hade);
}

MenuItem* ToolButton::itemAtPath(const MenuPath& path)
{
    return detail::itemAtRootPath(menu_.items, path, kVem);
}

// MENYPOSTENS TILLSTÅND — den delade implementationen i src/menu_path.h,
// med den tomma stigen som roten (`itemAtPath`).
void ToolButton::setItemIcon(const MenuPath& path, ImageSource icon)
{
    detail::setMenuItemIcon(itemAtPath(path), kVem, path, std::move(icon));
}

void ToolButton::setItemEnabled(const MenuPath& path, bool on)
{
    detail::setMenuItemEnabled(itemAtPath(path), kVem, path, on);
}

void ToolButton::setItemChecked(const MenuPath& path, bool on)
{
    detail::setMenuItemChecked(itemAtPath(path), kVem, path, on);
}

void ToolButton::logMissingPath(const char* fn, const MenuPath& path)
{
    detail::logMissingMenuPath(kVem, fn, path);
}

} // namespace glaze
