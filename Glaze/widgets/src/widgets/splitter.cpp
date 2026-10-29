// Splitter — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER: delaren ritar bara sitt skikt, och skiktets form är DELAD med
// dockningsytan (den interna `../sash.cpp`). Rutorna ritar sig själva.
// Geometrin bor i `layout()`, som ritningen, placeringen och musvägen alla
// läser — det man ser, det rutorna placeras mot och det man kan greppa kan
// därför inte glida isär. Inga tal och inga färger i den här filen.

#include "glaze/widgets/splitter.h"

#include <algorithm>

#include <gfx/platform.h>

#include "glaze/draw.h"

#include "../prop_parse.h"
#include "../sash.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("splitter", Splitter)

bool Splitter::setProp(const std::string& key, const std::string& value)
{
    if (key == "vertical") {
        setVertical(propparse::parseBool(value));
        return true;
    }
    if (key == "fixedPane") {
        // Enumets egna namn, gemena — den form `parseEnumTail` levererar ur
        // `glaze::Splitter::Pane::Second`.
        if (value == "first")
            setFixedPane(Pane::First);
        else if (value == "second")
            setFixedPane(Pane::Second);
        else
            gfx::log("Splitter(%s): '%s' är ingen ruta (first/second) — "
                     "ignoreras", name.c_str(), value.c_str());
        return true;   // nyckeln är känd — värdet är fel
    }
    if (key == "paneSize" || key == "firstMin" || key == "secondMin") {
        float v = 0.0f;
        if (!propparse::parseNumber(value, v)) {
            gfx::log("Splitter(%s): '%s' är inget tal för %s (ignoreras)",
                     name.c_str(), value.c_str(), key.c_str());
            return true;
        }
        if (key == "paneSize")
            setPaneSize(v);
        else if (key == "firstMin")
            setFirstMin(v);
        else
            setSecondMin(v);
        return true;
    }
    return Widget::setProp(key, value);   // basen SIST — konventionen
}

// ---------------------------------------------------------- geometrin

const Widget* Splitter::pane(std::size_t i) const
{
    return i < children().size() ? children()[i] : nullptr;
}

// TVÅ RUTOR OCH ETT SKIKT, alltid (se headern): skiktet står på den FASTA
// rutans kant, och den fasta rutans storlek är `paneSize` — eller under ett
// drag förhandsvisningens `dragSize_` — klampad mot minimimåtten
// (`sash::clampPane`, samma klampning som draget, så det man drar till är
// det man får). AUTO delar lika. Skiktet kapas mot ytan, så en delare som
// är smalare än sitt skikt ritar bara skiktet.
Splitter::Layout Splitter::layout(const Ctx& c, const gfx::Rectf& b) const
{
    const float total = vertical_ ? b.w : b.h;
    const float thick = std::min(c.style->splitterThick, std::max(0.0f, total));
    const float avail = std::max(0.0f, total - thick);
    const bool  förstaFast = fixedPane_ == Pane::First;
    const float storlek = liveDrag() ? dragSize_ : paneSize_;
    const float fast = sash::clampPane(
        storlek > 0.0f ? storlek : avail / 2.0f,
        förstaFast ? firstMin_ : secondMin_,
        förstaFast ? secondMin_ : firstMin_, avail);
    const float första = förstaFast ? fast : avail - fast;

    Layout L;
    if (vertical_) {
        L.first  = gfx::Rectf{b.x, b.y, första, b.h};
        L.sash   = gfx::Rectf{b.x + första, b.y, thick, b.h};
        L.second = gfx::Rectf{b.x + första + thick, b.y, avail - första, b.h};
    } else {
        L.first  = gfx::Rectf{b.x, b.y, b.w, första};
        L.sash   = gfx::Rectf{b.x, b.y + första, b.w, thick};
        L.second = gfx::Rectf{b.x, b.y + första + thick, b.w, avail - första};
    }
    return L;
}

// Skiktet i FYSISKA px — musvägens projektion av samma geometri. Ctx:t är
// delarens förälders (`ctxFor`), så delarens egen rekt är `resolve(c)`.
gfx::Rectf Splitter::sashRect(const Ctx& c) const
{
    const gfx::Rectf r = resolve(c);
    return Draw::project(c, r, layout(c, Draw::bounds(c, r)).sash);
}

// HETT = aktiverat OCH (draget pågår ELLER pekaren står över skiktet).
// `dragging_` läses tillsammans med `pressed()` — ett tryck som avbröts av
// en fokusförlust fick inget släpp, men `pressed()` slocknade.
bool Splitter::hot() const
{
    return isEnabled() && (liveDrag() || (hovered() && overSash_));
}

// ---------------------------------------------------------- placeringen
//
// Bar-formen: `c.area` är delarens childArea — dess EGEN rekt, fysisk —
// och rutan får sin del projicerad ur den logiska layouten. Barn efter de
// två första får en tom rekt i hörnet: de placeras inte (headern).
gfx::Rectf Splitter::placeChild(const Widget& child, const Ctx& c) const
{
    const Layout L = layout(c, Draw::bounds(c, c.area));
    if (&child == pane(0))
        return Draw::project(c, c.area, L.first);
    if (&child == pane(1))
        return Draw::project(c, c.area, L.second);
    return gfx::Rectf{c.area.x, c.area.y, 0.0f, 0.0f};
}

gfx::Sizef Splitter::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default:
        // BESLUT: INGEN autostorlek — delaren är en
        // PLACERINGSYTA som Panel och dimensioneras av layouten (oftast
        // `Dock::Fill` eller dubbelankare). Ur barnen går den inte att mäta
        // heller: rutorna STRÄCKS till sina andelar, så deras egna mått
        // säger ingenting om delarens.
        return gfx::Sizef{width, height};
    }
}

void Splitter::draw(const Ctx& c)
{
    switch (c.ui) {
    // ALLA TRE ritar skiktet med den DELADE ritningen, och det är ett
    // beslut och inte en lucka: skiktet är samma sak i delaren och i
    // dockningsytan, och dess form per UI står på ETT ställe (`sash.cpp`).
    // Delaren själv har ingen yta — rutorna ÄR ytan.
    case UI::Glaze:
    case UI::Minimalistic:
    case UI::Slate:
    default:
        sash::draw(c, layout(c, c.bounds()).sash, vertical_, hot());
        break;
    }
}

// ---------------------------------------------------------- beteendet
//
// Draget är ETT tryck: `mouseDown` på skiktet börjar det, `mouseDrag` (auto-
// capture — trycket följer pekaren även utanför delaren) flyttar
// förhandsvisningen och `mouseUp` skriver läget och fyrar `onChange`. Ingen
// explicit capture behövs.

void Splitter::mouseDown(const Ctx& c, float x, float y)
{
    // Ett drag som AVBRÖTS (fokusförlust — inget släpp) lämnar
    // `dragging_` kvar; det nollas FÖRE layouten, annars räknas träffen
    // mot den gamla förhandsvisningen.
    dragging_ = false;
    const gfx::Rectf r = resolve(c);
    const Layout     L = layout(c, Draw::bounds(c, r));
    const gfx::Rectf s = Draw::project(c, r, L.sash);
    dragging_ = s.contains(x, y);
    if (!dragging_)
        return;
    grip_ = vertical_ ? x - s.x : y - s.y;
    // Förhandsvisningen börjar där skiktet STÅR — den EFFEKTIVA storleken,
    // inte den lagrade (som kan vara AUTO), så ingenting hoppar.
    const bool först = fixedPane_ == Pane::First;
    dragSize_ = först ? (vertical_ ? L.first.w : L.first.h)
                      : (vertical_ ? L.second.w : L.second.h);
    startEff_ = dragSize_;
}

void Splitter::mouseDrag(const Ctx& c, float x, float y)
{
    if (!liveDrag())
        return;
    const gfx::Rectf r     = resolve(c);
    const gfx::Rectf b     = Draw::bounds(c, r);
    const bool       först = fixedPane_ == Pane::First;
    const float      total = vertical_ ? b.w : b.h;
    const float      thick =
        std::min(c.style->splitterThick, std::max(0.0f, total));
    const float      avail = std::max(0.0f, total - thick);

    // Den FASTA rutans yttre kant står still; skiktet följer handen.
    const float kant = först ? (vertical_ ? r.x : r.y)
                             : (vertical_ ? r.x + r.w : r.y + r.h);
    const float vill = sash::paneFromPointer(vertical_ ? x : y, kant, först,
                                             grip_, thick * c.scale, c.scale);
    const float ny   = sash::clampPane(vill, först ? firstMin_ : secondMin_,
                                       först ? secondMin_ : firstMin_, avail);
    if (ny == dragSize_)
        return;   // regel 1: en rörelse som inte flyttar skiktet ritar inget
    dragSize_ = ny;
    invalidate();   // rutorna flyttar — hela delarens yta ritas om
}

// SLÄPPET skriver läget — men bara när draget FLYTTADE skiktet, och bara
// när delaren fortfarande är aktiverad (inaktiverad mitt i draget = draget
// gäller inte; förhandsvisningen faller tillbaka av sig själv).
bool Splitter::mouseUp(const Ctx&, float, float)
{
    if (!dragging_)
        return false;
    dragging_ = false;
    if (!isEnabled() || dragSize_ == startEff_)
        return false;
    paneSize_ = dragSize_;
    invalidate();
    if (!onChange)
        return false;
    ChangeArgs e;
    onChange(this, e);
    return true;   // en handler körde — AppWindow ritar om allt
}

// Hover-delen: bara skiktet tänds, och bara det ritas om (regel 1 — en
// rörelse som inte byter del ritar ingenting).
void Splitter::mouseMove(const Ctx& c, float x, float y)
{
    const gfx::Rectf s   = sashRect(c);
    const bool       nu  = s.contains(x, y);
    if (nu == overSash_)
        return;
    overSash_ = nu;
    invalidateRect(s);
}

gfx::PointerShape Splitter::cursorAt(const Ctx& c, float x, float y) const
{
    if (liveDrag() || sashRect(c).contains(x, y))
        return sash::cursor(vertical_);
    return gfx::PointerShape::Arrow;
}

} // namespace glaze
