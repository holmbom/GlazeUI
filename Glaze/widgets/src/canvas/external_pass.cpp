// ExternalCanvasPass — ritytans scen i externa mål med en uttrycklig vy.
// Kontraktet står i glaze/canvas/external_pass.h; ritvägen är den som
// skärmen och `renderToBitmap` delar (target3d.cpp `renderView`), här med
// målets egen `gfx::View3D` i stället för en kamera.

#include "glaze/canvas/external_pass.h"

#include <cmath>
#include <string>
#include <utility>

#include <gfx/camera.h>
#include <gfx/image.h>
#include <gfx/renderer.h>

#include "glaze/app_window.h"
#include "../profiler_hooks.h"
#include "camera_math.h"
#include "pick3d.h"
#include "scene.h"
#include "target3d.h"

namespace glaze::detail {

namespace {

// Är vyn användbar? Fasadens egen regel (gfx/camera.h `View3D`) prövas med en
// punkt mitt i djupet längs blicken: den projiceras bara i en användbar vy,
// så regeln står på ett ställe.
bool usableView(const gfx::View3D& v)
{
    double px = 0.0, py = 0.0;
    return gfx::project(v, axisPoint(v, (v.nearZ + v.farZ) / 2.0), px, py) &&
           std::isfinite(px) && std::isfinite(py);
}

// Har ritytans fönster fortfarande renderaren passet skapades mot? Pekaren
// ensam räcker inte: en renderare som skapas efter en stängning kan få samma
// adress. Därför jämförs också enheten, som hämtas ur fönstrets NUVARANDE
// renderare. Tom text = samma renderare och samma enhet.
std::string rendererChanged(const gfx::Renderer* stored, void* storedDevice, const Ctx& c)
{
    if (c.r != stored)
        return "ritytans fönster har en annan renderare än den passet skapades mot";
    if (c.r->nativeDevice().device != storedDevice)
        return "ritytans fönster ritar på en annan grafikenhet än den passet skapades mot "
               "(fönstret har stängts och öppnats igen)";
    return {};
}

} // namespace

// Passets eget tillstånd: renderaren och enheten det skapades mot. Ritytans
// fönster kan stängas eller öppnas igen under passets liv; `render` och
// `castRay` jämför båda.
struct ExternalCanvasPass::State {
    gfx::Renderer* renderer = nullptr;
    void*          device   = nullptr;   // renderarens `nativeDevice().device` vid `create`
};

ExternalCanvasPass::ExternalCanvasPass(Canvas& canvas)
    : canvas_(canvas), state_(std::make_unique<State>())
{
}

ExternalCanvasPass::~ExternalCanvasPass() = default;

std::unique_ptr<ExternalCanvasPass> ExternalCanvasPass::create(Canvas& canvas,
                                                               std::string& error)
{
    const AppWindow* win = canvas.appWindow();
    if (!win || !win->isOpen()) {
        error = "ritytan sitter inte i ett öppet fönster (renderaren är fönstrets)";
        return nullptr;
    }
    const Ctx c = canvas.behaviorCtx();
    if (!c.r || !c.canvas) {
        error = "fönstret är mitt i öppning eller stängning";
        return nullptr;
    }
    std::unique_ptr<ExternalCanvasPass> pass(new ExternalCanvasPass(canvas));
    pass->state_->renderer = c.r;
    pass->state_->device   = c.r->nativeDevice().device;
    return pass;
}

gfx::Renderer& ExternalCanvasPass::renderer() const
{
    return *state_->renderer;
}

bool ExternalCanvasPass::current() const
{
    const AppWindow* win = canvas_.appWindow();
    if (!win || !win->isOpen())
        return false;
    const Ctx c = canvas_.behaviorCtx();
    return c.r && c.canvas && rendererChanged(state_->renderer, state_->device, c).empty();
}

bool ExternalCanvasPass::render(std::span<const ExternalCanvasView> views,
                                std::string& error)
{
    if (views.empty()) {
        error = "inga mål att rita i";
        return false;
    }
    const AppWindow* win = canvas_.appWindow();
    if (!win || !win->isOpen()) {
        error = "ritytan sitter inte längre i ett öppet fönster";
        return false;
    }
    const Ctx c = canvas_.behaviorCtx();
    if (!c.r || !c.canvas) {
        error = "fönstret är mitt i öppning eller stängning";
        return false;
    }
    if (std::string changed = rendererChanged(state_->renderer, state_->device, c);
        !changed.empty()) {
        error = std::move(changed);
        return false;
    }
    for (std::size_t i = 0; i < views.size(); ++i) {
        const ExternalCanvasView& v   = views[i];
        const char*               fel = nullptr;
        if (!v.target || !v.target->image().valid())
            fel = "ogiltigt mål";
        else if (!v.target->hasDepth())
            fel = "målet saknar djup (D32F)";
        else if (v.view.box.x != 0.0f || v.view.box.y != 0.0f ||
                 v.view.box.w != static_cast<float>(v.target->image().width()) ||
                 v.view.box.h != static_cast<float>(v.target->image().height()))
            fel = "vyns ruta är inte hela målet";
        else if (!usableView(v.view))
            fel = "vyn är oanvändbar (ändliga tal, blicken och upp inte noll "
                  "eller parallella, near < far — i perspektiv 0 < near och ett "
                  "synfält i (0, π) eller ett frustum med left < right och "
                  "down < up)";
        if (fel) {
            error = "mål " + std::to_string(i) + ": " + fel;
            return false;
        }
    }

    // Scenen i ritytans eget ritläge: fönstrets resurser, ljuset och
    // y-riktningen gäller målen; rutan och kameran är vyernas (target3d.cpp).
    Target3DArgs a = canvasTarget3DArgs(canvas_, c);
    PaintContext pc;
    canvas_.paintContext(c, pc, views[0].view.box);
    // Bilderna ritningen hämtar räknas på ritytans konto, som i skärmens och
    // bildens ritning.
    const ImageAccount konto(c, canvas_.scene_);
    return renderExternal3D(pc, canvas_.scene_, a, views, canvas_.background(c),
                            error);
}

std::vector<HitResult> ExternalCanvasPass::castRay(const Ray3& ray,
                                                   const HitOptions& options,
                                                   const gfx::View3D& view) const
{
    const ProfileScope mät(frameworkLabel(FrameworkLabel::CastRay));
    // Tätheterna är fönstrets (skärmtoleranserna är i logiska px och mm);
    // vyn är den uttryckliga. Utan ett öppet fönster med passets renderare och
    // enhet ger plockningen inga träffar; `render` rapporterar skälet.
    const AppWindow* w = canvas_.appWindow();
    if (!w || !w->isOpen())
        return {};
    const Ctx c = canvas_.behaviorCtx();
    if (!c.r || !c.canvas || !rendererChanged(state_->renderer, state_->device, c).empty())
        return {};
    const double pxPerPx = c.scale;
    const double pxPerMm = c.pxPerMm;
    return castRayInScene(*canvas_.scene_, ray, options, &view, pxPerPx, pxPerMm);
}

} // namespace glaze::detail
