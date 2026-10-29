#pragma once
#include <cstdint>
#include <span>
#include <string>

#include <gfx/color.h>
#include <gfx/geometry.h>

#include "glaze/widget.h"

// INTERN header (`sash.h`-prejudikatet) — PANELRUBRIKEN: ritningen av en
// panels yta, rubrikrad, titel och knappar, delad av `DockPanel` (dockad, i en
// värd eller en remsa) och `FloatingPanel` (den fria panelen). Det är SAMMA
// rubrik — samma rad (`dockHeaderH`), samma indrag, samma knappar med samma
// tända platta och samma ytor i varje UI — och den ritas därför på ETT
// ställe: två kopior hade glidit isär i just de detaljer man ser (en platta,
// en kant, en hårlinje). En app har inget ärende hit.
//
// Vad som INTE bor här, med skäl: GEOMETRIN (var raden, titeln och knapparna
// ligger) och TILLSTÅNDET (vilken knapp som är tänd eller nedtryckt). De är
// widgetens — den har dem redan för musvägen och tooltipen — och ritningen
// får samma rektar och samma flaggor som beteendet läser, så att de aldrig
// kan glida isär. Inte heller panelens egna delar: `DockPanel`s fokusring och
// `FloatingPanel`s grepp ritas av widgeten efter rubriken.

namespace glaze::detail {

// EN knapp i rubrikraden: rutan, tecknet och tillståndet. En knapp med tom
// ruta (bredd eller höjd <= 0) FINNS INTE — ingen platta och inget tecken —
// så en panel utan stängknapp eller knappnål skickar bara sina `*Rect()` som
// de är.
struct HeaderButton {
    enum class Glyph : std::uint8_t {
        Menu,       // ▾ — rubrikmenyn
        Pinned,     // knappnålen STÅENDE — en fäst panel (`Icon::Pinned`)
        Unpinned,   // knappnålen LIGGANDE — en som döljs automatiskt
        Close,      // ✕
    };
    gfx::Rectf rect{};   // en kvadrat lika hög som raden (en stående remsa:
                         // lika bred som remsan)
    Glyph      glyph = Glyph::Close;
    bool       hot   = false;   // pekaren över knappen, inget tryck pågår
    bool       down  = false;   // nedtryckt — för ▾ också medan menyn är öppen
};

struct PanelHeader {
    gfx::Rectf bounds{};   // panelens hela yta (`c.bounds()`)
    gfx::Rectf header{};   // rubrikraden — dess underkant bär hårlinjen
    gfx::Rectf title{};    // titelns yta; klipps, och tom i den stående remsan
    gfx::Color titleColor{};   // widgetens `textColor(c)`
    // Inaktiv: tecknen dimmas (`panelGlyphColor`). Plattorna släcks av
    // widgeten själv — en inaktiv panel har varken `hot` eller `down`.
    bool       enabled = true;
    // Hårlinjen under rubrikraden — av när det inte finns något innehåll
    // under den (den hopfällda panelen).
    bool       line = true;
    // Panelen STÅR FRITT på en annan yta och får kanten runt om i varje
    // familj; en dockad panel står mot ett skikt eller en fönsterkant. Glaze
    // ramar in också den dockade (`frame`, fönsterkantens roll).
    bool       freestanding = false;
    // I ritordning: plattorna, tecknen och de tända tecknens omritning följer
    // den.
    std::span<const HeaderButton> buttons{};
};

// RUBRIKEN i den form `c.ui` bestämmer: panelens yta och rubrikrad, hårlinjen,
// de tända knapparnas plattor, kanten, titeln `text` och knapptecknen.
void drawPanelHeader(const Ctx& c, const PanelHeader& p,
                     const std::string& text);

// Tecknens VILOFÄRG — och greppets, som är panelens eget men dimmas lika:
// `textDim`, och `border` när panelen är inaktiv.
gfx::Color panelGlyphColor(const Ctx& c, bool enabled);

} // namespace glaze::detail
