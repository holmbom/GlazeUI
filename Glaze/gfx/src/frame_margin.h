#pragma once
#include <algorithm>

#include "gfx/window.h"

// STORLEKSMARGINALEN UTANFÖR YTAN — det backend-neutrala. INTERN header:
// Linux-backendens två drivrutiner lägger en
// GENOMSKINLIG marginal runt klientytan (Wayland
// `xdg_surface.set_window_geometry`, X11 `_GTK_FRAME_EXTENTS`, GTK:s och
// libdecors form), och vilken storleksändring en punkt i marginalen betyder
// avgörs HÄR — EN sanning för pekarens pil och för trycket, i båda. Ren std,
// inga OS-typer: tests/geometry_gate.cpp (avsnitt KM) mäter funktionen
// headless mot ett handräknat svar (display_util.h-prejudikatet).
//
// Windows behöver den inte (DefWindowProc äger den osynliga ramen) och macOS
// inte heller (AppKit äger kanterna).

namespace gfx::frame {

// MARGINALENS MÅTT i LOGISKA px. Bredden är storleksgreppet utanför den
// synliga kanten — Windows ram är 8 px vid 96 dpi, och libdecor/GTK ligger i
// samma trakt. Hörnet räcker `kCornerLogical` px in längs kanten från
// klientens hörn (plus marginalen själv), så det diagonala greppet går att
// träffa också nära ett hörn och inte bara i marginalens lilla ruta.
inline constexpr int kMarginLogical = 8;
inline constexpr int kCornerLogical = 16;

// Storleksändringen för punkten (x, y) i KLIENTENS koordinater — origo i
// klientytans övre vänstra hörn, samma px som klientytan w × h (fysiska px i
// backenden). Marginalen är m px bred runt om, och `corner` är hörnets
// räckvidd längs kanten (kapad till halva klientytans kortaste sida, så två
// hörn aldrig överlappar på ett litet fönster).
//
//   innanför klientytan        Client — hit-test-kartan avgör
//   i marginalen               kanten, eller hörnet inom `corner`
//   utanför marginalen / m ≤ 0 Client — ingen träff (ytan slutar där)
inline HitRole marginRole(float x, float y, float w, float h, float m,
                          float corner)
{
    if (m <= 0.0f)
        return HitRole::Client;
    const bool vänster = x < 0.0f;
    const bool höger   = x >= w;
    const bool upp     = y < 0.0f;
    const bool ner     = y >= h;
    if (!vänster && !höger && !upp && !ner)
        return HitRole::Client;   // klientytan
    if (x < -m || x >= w + m || y < -m || y >= h + m)
        return HitRole::Client;   // bortom ytan
    const float c = std::max(0.0f, std::min(corner, std::min(w, h) * 0.5f));
    const bool  nV = x < c;          // nära vänsterkanten (marginalen ingår)
    const bool  nH = x >= w - c;     // nära högerkanten
    const bool  nU = y < c;          // nära överkanten
    const bool  nN = y >= h - c;     // nära nederkanten
    if ((upp && nV) || (vänster && nU))
        return HitRole::ResizeTopLeft;
    if ((upp && nH) || (höger && nU))
        return HitRole::ResizeTopRight;
    if ((ner && nV) || (vänster && nN))
        return HitRole::ResizeBottomLeft;
    if ((ner && nH) || (höger && nN))
        return HitRole::ResizeBottomRight;
    if (vänster)
        return HitRole::ResizeLeft;
    if (höger)
        return HitRole::ResizeRight;
    if (upp)
        return HitRole::ResizeTop;
    return HitRole::ResizeBottom;
}

// Är rollen en av kartans resize-zoner? Linux hoppar över dem när
// marginalen finns — zonerna ligger INNANFÖR klientytan och hade tagit de
// yttersta px av allt innehåll vid kanten.
inline bool isResizeRole(HitRole r)
{
    switch (r) {
    case HitRole::ResizeLeft:
    case HitRole::ResizeRight:
    case HitRole::ResizeTop:
    case HitRole::ResizeBottom:
    case HitRole::ResizeTopLeft:
    case HitRole::ResizeTopRight:
    case HitRole::ResizeBottomLeft:
    case HitRole::ResizeBottomRight:
        return true;
    default:
        return false;
    }
}

} // namespace gfx::frame
