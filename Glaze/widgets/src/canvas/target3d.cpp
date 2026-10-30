// Ritytans 3D-mål — bildrutans ordning, cachen och
// kompositen — och del-vyerna och bilden (renderToBitmap).
// Kontraktet och skälen står i target3d.h; lagren och deras ritning i
// scene.h, målen i resources.h.

#include "target3d.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

#include <gfx/camera.h>
#include <gfx/platform.h>

#include "glaze/app.h"
#include "../font_set.h"
#include "../image_cache.h"
#include "../profiler_hooks.h"
#include "camera_math.h"
#include "drape.h"
#include "labels.h"
#include "resources.h"

namespace glaze::detail {

namespace {

// Förvalt MSAA i 3D-målet: 8× vore skarpare, men ett 4K-mål med 8× är
// ~530 MB.
constexpr int kDefaultMultisample = 4;

// MARKDJUPETS PLAN: halva sidan i det bortre planets avstånd (planet är
// 2 × far stort), och djupbiasen som skjuter det BAKÅT — det som ligger
// på z = 0 i ett World-lager rastreras ur andra trianglar än planet, och
// interpoleringen skiljer några steg i D32F; lutningen täcker en flack blick.
// Biasen ska INTE växa för att rädda det som står på marken: en lutning om
// k px låter det som ligger under marken synas k px in — de
// skärmvända hörnen tar planets djup själva (`glaze_standingZ`, target3d.h steg 3).
constexpr double kGroundPlaneReach = 1.0;
constexpr float  kGroundDepthBias  = 4.0f;
constexpr float  kGroundSlopeBias  = 1.0f;

// En vys ANKARE som andelar av måtten: vänster/överkant, mitten,
// höger/underkant (style3d.h `View`).
constexpr double kAnchorStart  = 0.0;
constexpr double kAnchorMiddle = 0.5;
constexpr double kAnchorEnd    = 1.0;

// Det som inte går att skapa eller läsa loggas en gång per process.
std::atomic<bool> g_targetLogged{false};
std::atomic<bool> g_viewLogged{false};
std::atomic<bool> g_bitmapLogged{false};
std::atomic<bool> g_bitmapImagesLogged{false};

// Målets genomskinliga start — kompositen lägger det över ritytans yta.
const gfx::Color kClear{0, 0, 0, 0};

// Steg 3: markplanet kring ögat, bara djup.
void drawGroundDepth(const PaintContext& f, const CanvasResources::GridMesh& p)
{
    const gfx::View3D& v    = *f.view3D;
    const double       half = v.farZ * kGroundPlaneReach;
    gfx::MeshDraw      d;
    d.program    = gfx::MeshProgram::Fill;
    d.vertices   = gfx::slice(*p.vb);
    d.indices    = gfx::slice(*p.ib);
    d.indexCount = p.count;
    (gfx::viewProjection(v, v.eye.x, v.eye.y, 0.0) *
     gfx::Mat4d::scale(half, half, 1.0))
        .toFloat(d.uniforms.viewProj);
    d.state.depth      = gfx::DepthTest::Always;
    d.state.depthWrite = true;
    d.state.depthBias  = kGroundDepthBias;
    d.state.slopeBias  = kGroundSlopeBias;
    d.state.blend      = gfx::Blend::DepthOnly;
    f.c->r->drawMesh(d);
}

// EN VY i sitt mål: förberedelsen UTANFÖR målet (chunkarna, bilderna,
// texten, etiketterna med vyns 3D-vy, rutnätet, markplanet och terrängens
// draperingar) och sedan bildrutans ordning i det (target3d.h). `f` bär vyns
// 3D-vy; rutnätet, `onPaint` och lagrens målare hör till huvudvyn och
// bilden (`main`), och draperingarna ritas i läget `drapeMode` (drape.h —
// huvudvyn räknar klasserna, bilden ritar komplett). Svarar med om
// etiketterna tonar och om draperingar väntar på nästa bild.
Target3DState renderView(PaintContext& f, const std::shared_ptr<SceneData>& scene,
                         gfx::RenderTarget& t, gfx::Color clear,
                         const Target3DArgs& a, bool main, DrapeMode drapeMode)
{
    CanvasResources& res = *f.res;
    // Profilerarens faser följer stegen: förberedelsen,
    // draperingarna, ritningen i målet och — inuti den men bortdragen ur
    // den — appens målning.
    std::optional<PhaseScope> fas{std::in_place, ProfilePhase::CanvasPrepare};
    LayerFrame                lf = prepareLayers(f, scene, a.labelFade, true);
    if (main)
        prepareGrid(f, a.gridSize, a.gridColor, a.gridMinPx, a.gridWidthPx);
    // TERRÄNGEN: draperingarna ritas om här, i egna mål före
    // huvudmålet — och med terräng uteblir markdjupet (Ground-lagren ligger i
    // draperingen, och ett hål visar det som ligger bakom). Utan terräng
    // ritas det bara när ett Ground-lager målar marken (`groundDrawn`).
    fas.emplace(ProfilePhase::CanvasDrapes);
    const DrapeFrame drapes = prepareDrapes(f, scene, lf, drapeMode);
    lf.drapes               = &drapes;
    const CanvasResources::GridMesh* plane =
        lf.groundDrawn && !lf.terrain ? res.groundPlane() : nullptr;
    const LayerPaint* målare = main ? &a.layerPaint : nullptr;

    fas.emplace(ProfilePhase::CanvasDraw);
    gfx::Renderer& r = *f.c->r;
    r.pushTarget(t, clear);
    drawPasses(f, *scene);   // steg 1: helskärmspassen, utan djup
    if (main)
        drawGrid(f, a.gridSize, a.gridColor, a.gridMinPx, a.gridWidthPx);
    drawTerrain(f, *scene, lf);   // steg 2: terrängen, draperad
    drawGround(f, *scene, lf, GroundPart::Geometry, målare);
    if (plane)
        drawGroundDepth(f, *plane);
    drawWorld(f, *scene, lf, målare);
    drawGround(f, *scene, lf, GroundPart::Labels);
    if (main && a.paint) {
        const PhaseScope måla(ProfilePhase::CanvasPaint, *fas);
        a.paint(f);
    }
    r.popTarget();
    return Target3DState{lf.animating(), drapes.pending() > 0};
}

// Ritläget för en 3D-kamera i rutan `box` (målets px) med Ctx:t `c`: 3D-vyn,
// kamerans kartskugga (det som mäts "i mitten" — camera_math `mapViewAt`),
// vyn och etiketternas plats — och det som är ritytans och inte vyns
// (fönstrets resurser, meshernas ljus, y-riktningen) ur ritytans ritläge
// `canvas`.
PaintContext cameraContext(const PaintContext& canvas, const Ctx& c,
                           const Camera3D& cam, const gfx::Rectf& box,
                           const SceneData& scene, int view, int labelSlot)
{
    PaintContext f;
    f.c         = &c;
    f.res       = canvas.res;
    f.world     = mapViewAt(cam, box, canvas.world.yUp);
    f.screen    = localView(box, c.scale);
    f.zoom      = f.world.pxPerUnit / c.scale;
    f.opacity   = 1.0f;   // tonen är kompositens
    f.view      = view;
    f.labelSlot = labelSlot;
    f.lighting  = canvas.lighting;
    f.view3D    = viewView3D(cam, box, scene, view);
    return f;
}

// Ritläget för en UTTRYCKLIG 3D-vy (ett headsets öga): vyn själv, kartskuggan
// ur den (camera_math `mapViewAtView` — skalan "i mitten" räknas ur samma vy
// som ritar), vyns ruta som ritytans lokala rymd och etiketternas plats. `c`
// är vyns Ctx (rutan = vyns). Resten — fönstrets resurser, ljuset och
// y-riktningen — är ritytans, som i `cameraContext`.
PaintContext viewContext(const PaintContext& canvas, const Ctx& c,
                         const gfx::View3D& view, const SceneData& scene,
                         int labelSlot)
{
    PaintContext f;
    f.c         = &c;
    f.res       = canvas.res;
    f.world     = mapViewAtView(view, sceneBox(scene, kMainView), canvas.world.yUp);
    f.screen    = localView(view.box, c.scale);
    f.zoom      = f.world.pxPerUnit / c.scale;
    f.opacity   = 1.0f;   // tonen är kompositens — här finns ingen
    f.view      = kMainView;
    f.labelSlot = labelSlot;
    f.lighting  = canvas.lighting;
    f.view3D    = view;
    return f;
}

// Ett `beginOffscreen`-block som alltid slutar: bilden och de externa målen
// ritas utanför fönstrets frame och lämnar blocket på varje väg ut.
class OffscreenBlock {
public:
    explicit OffscreenBlock(gfx::Renderer& r) : r_(r), open_(r.beginOffscreen()) {}
    ~OffscreenBlock()
    {
        if (open_)
            r_.endOffscreen();
    }
    OffscreenBlock(const OffscreenBlock&)            = delete;
    OffscreenBlock& operator=(const OffscreenBlock&) = delete;
    // Gick blocket att öppna? (Inte i en frame — renderaren loggar orsaken.)
    bool open() const { return open_; }

private:
    gfx::Renderer& r_;
    bool           open_;
};

// Ett måls pixlar i fönstret: rutan `r` ∩ ritytans `box`, från snittets
// heltalshörn till dess heltalsslut. Tomt (w = h = 0) utan snitt.
struct Pixels {
    float x0 = 0.0f, y0 = 0.0f;
    int   w = 0, h = 0;
};

Pixels pixelsOf(const gfx::Rectf& r, const gfx::Rectf& box)
{
    const float x0 = std::max(r.x, box.x);
    const float y0 = std::max(r.y, box.y);
    const float x1 = std::min(r.x + r.w, box.x + box.w);
    const float y1 = std::min(r.y + r.h, box.y + box.h);
    Pixels      p;
    p.x0 = std::floor(x0);
    p.y0 = std::floor(y0);
    if (!(x1 > x0) || !(y1 > y0))
        return p;
    p.w = static_cast<int>(std::ceil(x1) - p.x0);
    p.h = static_cast<int>(std::ceil(y1) - p.y0);
    return p;
}

// Ankarets andelar av måtten (style3d.h `View`): samma punkt på ritytan och
// på vyn.
void anchorShares(gfx::TextAnchor a, double& fx, double& fy)
{
    switch (a) {
    case gfx::TextAnchor::TopLeft:     fx = kAnchorStart;  fy = kAnchorStart;  break;
    case gfx::TextAnchor::Top:         fx = kAnchorMiddle; fy = kAnchorStart;  break;
    case gfx::TextAnchor::TopRight:    fx = kAnchorEnd;    fy = kAnchorStart;  break;
    case gfx::TextAnchor::Left:        fx = kAnchorStart;  fy = kAnchorMiddle; break;
    case gfx::TextAnchor::Center:      fx = kAnchorMiddle; fy = kAnchorMiddle; break;
    case gfx::TextAnchor::Right:       fx = kAnchorEnd;    fy = kAnchorMiddle; break;
    case gfx::TextAnchor::BottomLeft:  fx = kAnchorStart;  fy = kAnchorEnd;    break;
    case gfx::TextAnchor::Bottom:      fx = kAnchorMiddle; fy = kAnchorEnd;    break;
    case gfx::TextAnchor::BottomRight: fx = kAnchorEnd;    fy = kAnchorEnd;    break;
    }
}

} // namespace

gfx::Rectf targetBox(const gfx::Rectf& box)
{
    return gfx::Rectf{box.x - std::floor(box.x), box.y - std::floor(box.y),
                      box.w, box.h};
}

int defaultMultisample()
{
    return kDefaultMultisample;
}

bool validMultisample(int samples)
{
    return samples == 1 || samples == 2 || samples == 4 || samples == 8;
}

gfx::View3D mainView3D(const Camera3D* camera, const gfx::MapView& world,
                       const SceneData& scene)
{
    const std::optional<Box3> box = sceneBox(scene, kMainView);
    return camera ? view3D(*camera, world.box, box)
                  : gfx::toView3D(world, box ? box->min.z : 0.0,
                                  box ? box->max.z : 0.0);
}

// ------------------------------------------------------------- del-vyerna

bool validView(const View& v)
{
    return static_cast<unsigned>(v.anchor) <=
               static_cast<unsigned>(gfx::TextAnchor::BottomRight) &&
           std::isfinite(v.offset.x) && std::isfinite(v.offset.y) &&
           std::isfinite(v.width) && std::isfinite(v.height) &&
           v.width >= 0.0 && v.height >= 0.0 && validCamera3D(v.camera);
}

gfx::Rectf viewRect(const View& v, const gfx::Rectf& box, float scale)
{
    const double w  = v.width > 0.0 ? v.width * scale : box.w;
    const double h  = v.height > 0.0 ? v.height * scale : box.h;
    double       fx = kAnchorStart, fy = kAnchorStart;
    anchorShares(v.anchor, fx, fy);
    return gfx::Rectf{
        static_cast<float>(box.x + fx * (box.w - w) + v.offset.x * scale),
        static_cast<float>(box.y + fy * (box.h - h) + v.offset.y * scale),
        static_cast<float>(w), static_cast<float>(h)};
}

Camera3D viewCamera(const View& v, const Camera3D* main3D,
                    const gfx::View3D* mainView)
{
    if (!v.followRotation)
        return v.camera;
    Camera3D c = v.camera;
    if (main3D) {
        c.azimuth   = main3D->azimuth;
        c.elevation = main3D->elevation;
        c.roll      = main3D->roll;
        c.up        = main3D->up;
        return c;
    }
    return mainView ? orientedAs(c, mainView->forward, mainView->up) : c;
}

int viewAt(std::span<const View> views, const gfx::Rectf& box, float scale,
           double px, double py)
{
    for (int i = static_cast<int>(views.size()); i >= 1; --i) {
        const gfx::Rectf r = viewRect(views[static_cast<std::size_t>(i - 1)],
                                      box, scale);
        const double x0 = std::max(r.x, box.x), y0 = std::max(r.y, box.y);
        const double x1 = std::min(r.x + r.w, box.x + box.w);
        const double y1 = std::min(r.y + r.h, box.y + box.h);
        if (px >= x0 && px < x1 && py >= y0 && py < y1)
            return i;
    }
    return kMainView;
}

gfx::View3D viewView3D(const Camera3D& cam, const gfx::Rectf& rect,
                       const SceneData& scene, int view)
{
    return view3D(cam, rect, sceneBox(scene, view));
}

// ------------------------------------------------------------------ målen

Target3DState drawTarget3D(const PaintContext& wf, PaintContext& tf,
                           const std::shared_ptr<SceneData>& scene,
                           const Target3DArgs& a)
{
    const Ctx&       c      = *wf.c;
    CanvasResources& res    = *wf.res;
    const int        nViews = static_cast<int>(a.views.size());
    // Del-vyer som inte längre finns (removeView, setViews): deras mål och
    // placering släpps efter framen.
    res.dropViews(scene->id, nViews + 1);

    // HUVUDMÅLETS PIXLAR: från rutans heltalshörn till dess heltalsslut.
    const Pixels mp = pixelsOf(c.box, c.box);
    if (mp.w <= 0 || mp.h <= 0)
        return {};
    CanvasResources::Target3D& t =
        res.target3D(scene, kMainView, mp.w, mp.h, a.samples);
    if (!t.target->image().valid()) {
        if (!g_targetLogged.exchange(true))
            gfx::log("Canvas: 3D-målet (%d × %d, %d prov) gick inte att skapa — "
                     "ritytan ritar bara sin yta; loggas en gång",
                     mp.w, mp.h, a.samples);
        return {};
    }

    // CACHEN (target3d.h): ett mål ritas om bara när något det ritar kan ha
    // ändrats — med sin egen ruta.
    const auto inaktuellt = [&](const CanvasResources::Target3D& m,
                                const gfx::Rectf& r) {
        return !m.drawn || m.stamp != a.stamp || m.box.x != r.x ||
               m.box.y != r.y || m.box.w != r.w || m.box.h != r.h ||
               m.pxPerPx != c.scale || m.pxPerMm != c.pxPerMm;
    };
    const auto ritat = [&](CanvasResources::Target3D& m, const gfx::Rectf& r) {
        m.drawn   = true;
        m.stamp   = a.stamp;
        m.box     = r;
        m.pxPerPx = c.scale;
        m.pxPerMm = c.pxPerMm;
        res.noteTargetRender();
        profileCount(ProfileCount::TargetRenders);
    };

    // VYERNAS rutor och mål — den synliga delen (vyns ∩ ritytans) i hela px.
    struct ViewTarget {
        int                        view;
        CanvasResources::Target3D* t;
        gfx::Rectf                 rect;   // vyns ruta, fönstrets px
        Pixels                     px;
    };
    std::vector<ViewTarget> vyer;
    bool                    någon = inaktuellt(t, c.box);
    for (int i = 1; i <= nViews; ++i) {
        const gfx::Rectf r =
            viewRect(a.views[static_cast<std::size_t>(i - 1)], c.box, c.scale);
        const Pixels p = pixelsOf(r, c.box);
        if (p.w <= 0 || p.h <= 0)
            continue;   // helt utanför ritytan — inget att rita (ett mål från
                        // en tidigare ritning står kvar tills vyn tas bort eller kommer in)
        CanvasResources::Target3D& vt = res.target3D(scene, i, p.w, p.h, a.samples);
        if (!vt.target->image().valid()) {
            if (!g_viewLogged.exchange(true))
                gfx::log("Canvas: del-vyns mål (%d × %d, %d prov) gick inte att "
                         "skapa — vyn ritas inte; loggas en gång",
                         p.w, p.h, a.samples);
            continue;
        }
        vyer.push_back(ViewTarget{i, &vt, r, p});
        någon = någon || inaktuellt(vt, r);
    }

    Target3DState läge;
    if (någon) {
        // De märkta lagren byggs om först — lådorna (vyernas klipplan)
        // räknas ur dem. Huvudvyns 3D-vy behövs också av en vy som följer
        // dess rotation.
        rebuildDirty(*scene);
        tf.view3D  = mainView3D(a.camera, tf.world, *scene);
        tf.opacity = 1.0f;   // tonen är kompositens
    }
    if (inaktuellt(t, c.box)) {
        läge = renderView(tf, scene, *t.target, kClear, a, true, DrapeMode::Main);
        ritat(t, c.box);
    }
    for (const ViewTarget& v : vyer) {
        if (!inaktuellt(*v.t, v.rect))
            continue;
        const View&     vy  = a.views[static_cast<std::size_t>(v.view - 1)];
        const Camera3D  cam = viewCamera(vy, a.camera, &*tf.view3D);
        // Vyns ruta i dess måls px — den kan sticka ut ur målet, som bara
        // bär den synliga delen.
        const gfx::Rectf ruta{v.rect.x - v.px.x0, v.rect.y - v.px.y0, v.rect.w,
                              v.rect.h};
        PaintContext vf =
            cameraContext(wf, c, cam, ruta, *scene, v.view, v.view);
        const Target3DState vyns = renderView(vf, scene, *v.t->target,
                                              vy.background, a, false,
                                              DrapeMode::View);
        läge.labelsAnimating = läge.labelsAnimating || vyns.labelsAnimating;
        läge.drapesPending   = läge.drapesPending || vyns.drapesPending;
        ritat(*v.t, v.rect);
    }

    // KOMPOSIT — varje gång, under ritytans klipp, med tonen: huvudmålet och
    // vyerna i nummerordning.
    c.r->drawImage(t.target->image(),
                   gfx::Rectf{mp.x0, mp.y0, static_cast<float>(mp.w),
                              static_cast<float>(mp.h)},
                   wf.opacity);
    for (const ViewTarget& v : vyer)
        c.r->drawImage(v.t->target->image(),
                       gfx::Rectf{v.px.x0, v.px.y0, static_cast<float>(v.px.w),
                                  static_cast<float>(v.px.h)},
                       wf.opacity);
    return läge;
}

// ------------------------------------------------------------------ bilden

std::optional<Bitmap> renderBitmap3D(const PaintContext& canvas,
                                     const std::shared_ptr<SceneData>& scene,
                                     const Target3DArgs& a, int w, int h,
                                     gfx::Color background)
{
    const Ctx&     c = *canvas.c;
    gfx::Renderer& r = *c.r;
    std::unique_ptr<gfx::RenderTarget> t;
    {
        const OffscreenBlock block(r);
        if (!block.open())
            return std::nullopt;   // inuti en frame — renderaren loggar orsaken
        // Bildens ruta från (0, 0); allt annat är fönstrets (skalan, mm, temat).
        Ctx bc = c;
        bc.box = gfx::Rectf{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};
        rebuildDirty(*scene);
        PaintContext f = cameraContext(canvas, bc, *a.camera, bc.box, *scene,
                                       kMainView, kBitmapLabels);
        gfx::TargetDesc d;
        d.samples = r.supportedSamples(a.samples);
        d.depth   = true;
        t         = r.createRenderTarget(w, h, d);
        if (!t->image().valid()) {
            if (!g_bitmapLogged.exchange(true))
                gfx::log("Canvas::renderToBitmap: målet (%d × %d) gick inte att "
                         "skapa — ingen bild; loggas en gång",
                         w, h);
            return std::nullopt;
        }
        renderView(f, scene, *t, background, a, true, DrapeMode::Complete);
    }
    std::vector<std::uint8_t> px;
    if (!r.readTarget(*t, px))
        return std::nullopt;   // renderaren loggar orsaken
    // En bild som väntar på bildcachens uppladdningsbudget (ImageCache —
    // en ny bild i en ström av nya) ritades inte; det ska synas i loggen.
    if (c.images && c.images->uploadsDeferred() &&
        !g_bitmapImagesLogged.exchange(true))
        gfx::log("Canvas::renderToBitmap: en bild väntade på uppladdningens "
                 "budget och saknas i bilden; loggas en gång");
    Bitmap b(w, h, px.data(), 0);
    if (!b.valid())
        return std::nullopt;   // Bitmap loggar orsaken
    return b;
}

// ----------------------------------------------------------- externa mål

bool renderExternal3D(const PaintContext& canvas,
                      const std::shared_ptr<SceneData>& scene,
                      const Target3DArgs& a,
                      std::span<const ExternalCanvasView> views,
                      gfx::Color background, std::string& error)
{
    const Ctx&     c = *canvas.c;
    gfx::Renderer& r = *c.r;
    const OffscreenBlock block(r);
    if (!block.open()) {
        error = "kunde inte öppna ett offscreen-block (en bildruta pågår, eller "
                "enheten är förlorad)";
        return false;
    }
    // RESURSBUDGETEN förnyas EN gång för hela anropet — det skrivbordet gör
    // överst i varje frame (app_window.cpp `renderContent`): bildernas vräkning
    // och uppladdningsbudget, ritytans döda resurser och fältens textstorlekar.
    // Här, före all ritning i blocket och aldrig mellan målen, så det första
    // målets bilder, buffertar och atlaser lever tills sista målet ritats.
    if (c.images)
        c.images->trim(app().canvasMemoryBudget(), app().canvasUploadBudget());
    if (c.canvas)
        c.canvas->trim();
    if (c.textFonts)
        c.textFonts->trim();
    // EN scenuppdatering för hela anropet: de märkta lagren byggs om här, och
    // varje mål förbereder sedan sina lager mot de färdiga.
    rebuildDirty(*scene);
    for (std::size_t i = 0; i < views.size(); ++i) {
        // Målets Ctx med målets ruta; ritläget pekar på den under ritningen.
        Ctx vc = c;
        vc.box = views[i].view.box;
        PaintContext f = viewContext(canvas, vc, views[i].view, *scene,
                                     kExternalLabels - static_cast<int>(i));
        renderView(f, scene, *views[i].target, background, a, true,
                   DrapeMode::Complete);
    }
    return true;
}

} // namespace glaze::detail
