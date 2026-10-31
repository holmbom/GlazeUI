#include "flat_panel_logic.h"

#include <algorithm>

namespace glaze::openxr::detail {

namespace {

bool samePoint(gfx::Pointf a, gfx::Pointf b)
{
    return a.x == b.x && a.y == b.y;
}

gfx::Event mouseEvent(gfx::EventType type, gfx::Pointf at, gfx::MouseButton button = gfx::MouseButton::None)
{
    gfx::Event e{};
    e.type   = type;
    e.mouseX = at.x;
    e.mouseY = at.y;
    e.button = button;
    return e;
}

} // namespace

FlatGeometry flatGeometry(double width, double height, double thickness, double border)
{
    FlatGeometry g;
    g.surfaceWidth  = std::max(0.0, width);
    g.surfaceHeight = std::max(0.0, height);
    g.thickness     = std::max(0.0, thickness);
    // Med tjockleken 0 finns bara ytan, utan ram.
    const double rim = g.thickness > 0.0 ? std::max(0.0, border) : 0.0;
    g.boardWidth     = g.surfaceWidth + 2.0 * rim;
    g.boardHeight    = g.surfaceHeight + 2.0 * rim;
    return g;
}

ElementGeom flatTipGeom(const FlatGeometry& g)
{
    ElementGeom e;
    e.width          = g.surfaceWidth;
    e.height         = g.surfaceHeight;
    e.depth          = 0.0;
    e.travel         = kFlatTravel;
    e.plateThickness = g.thickness;
    return e;
}

gfx::Pointf flatPixel(Vec3 local, double surfaceWidth, double surfaceHeight, int pxWidth, int pxHeight)
{
    if (!(surfaceWidth > 0.0) || !(surfaceHeight > 0.0))
        return {};
    return gfx::Pointf{static_cast<float>((local.x / surfaceWidth + 0.5) * pxWidth),
                       static_cast<float>((0.5 - local.z / surfaceHeight) * pxHeight)};
}

bool flatInside(gfx::Pointf px, int pxWidth, int pxHeight)
{
    return px.x >= 0.0f && px.y >= 0.0f && px.x <= static_cast<float>(pxWidth) &&
           px.y <= static_cast<float>(pxHeight);
}

FlatRay flatRay(Vec3 origin, Vec3 direction, double surfaceWidth, double surfaceHeight,
                int pxWidth, int pxHeight)
{
    FlatRay out;
    const RayHit r = rayVsFace(origin, direction, 0.0, surfaceWidth, surfaceHeight);
    if (!r.hit)
        return out;
    out.hit    = true;
    out.inside = r.inside;
    out.t      = r.t;
    out.local  = r.local;
    out.px     = flatPixel(r.local, surfaceWidth, surfaceHeight, pxWidth, pxHeight);
    return out;
}

// ---------------------------------------------------------------- MUSEN
std::vector<gfx::Event> stepFlatMouse(FlatMouse& mouse, const FlatMouseInput& in)
{
    std::vector<gfx::Event> out;
    if (in.cancel) {
        if (mouse.down)
            out.push_back(mouseEvent(gfx::EventType::MouseCaptureLost, mouse.pos));
        if (mouse.down || mouse.inside)
            out.push_back(mouseEvent(gfx::EventType::MouseLeave, mouse.pos));
        mouse = FlatMouse{};
        return out;
    }

    if (!mouse.down) {
        if (in.present && in.inside) {
            if (!mouse.inside || !mouse.hasPos || !samePoint(mouse.pos, in.px))
                out.push_back(mouseEvent(gfx::EventType::MouseMotion, in.px));
            mouse.inside = true;
            mouse.hasPos = true;
            mouse.pos    = in.px;
            if (in.down) {
                out.push_back(mouseEvent(gfx::EventType::MouseButtonDown, in.px, gfx::MouseButton::Left));
                mouse.down = true;
            }
        } else if (mouse.inside) {
            out.push_back(mouseEvent(gfx::EventType::MouseLeave, mouse.pos));
            mouse.inside = false;
        }
        return out;
    }

    // Trycket håller: draget är låst till panelen.
    if (in.present) {
        if (!samePoint(mouse.pos, in.px))
            out.push_back(mouseEvent(gfx::EventType::MouseMotion, in.px));
        mouse.pos    = in.px;
        mouse.inside = in.inside;
    }
    if (!in.down) {
        out.push_back(mouseEvent(gfx::EventType::MouseButtonUp, mouse.pos, gfx::MouseButton::Left));
        mouse.down = false;
        if (!mouse.inside)
            out.push_back(mouseEvent(gfx::EventType::MouseLeave, mouse.pos));
    }
    return out;
}

double wheelRows(double stick, double dt)
{
    return stick * kWheelRowsPerSecond * std::max(0.0, dt);
}

gfx::Event wheelEvent(gfx::Pointf px, double rows)
{
    gfx::Event e = mouseEvent(gfx::EventType::MouseWheel, px);
    e.wheelY     = static_cast<float>(rows);
    return e;
}

} // namespace glaze::openxr::detail
