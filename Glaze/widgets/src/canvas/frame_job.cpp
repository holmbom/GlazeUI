// EN BILD AV RITYTAN SOM ETT JOBB (frame_job.h).

#include "frame_job.h"

#include <cmath>
#include <utility>

#include <gfx/platform.h>

#include "../image_cache.h"
#include "../window_placement.h"
#include "camera_math.h"
#include "glaze/canvas/painter.h"
#include "glaze/widget.h"
#include "labels.h"
#include "memory_account.h"
#include "target3d.h"

namespace glaze::detail {

namespace {

// Bildens genomskinliga start — kompositen lägger den över ritytans yta.
const gfx::Color kClear{0, 0, 0, 0};

// Kompositens ton i bilden: ingen — den inaktiverade ytans ton läggs på när
// bilden ritas i fönstret.
constexpr float kOpaque = 1.0f;

// Sidans papper.
const gfx::Color kPaper{255, 255, 255, 255};

// RITNINGENS Ctx: renderaren och resurserna är anroparens, miljön jobbets.
// Fönstrets fonter följer inte med — scenens text har sina egna, i
// resursernas atlaser.
Ctx jobCtx(const CanvasFrameJob& j, const Ctx& base)
{
    Ctx c         = base;
    c.font        = nullptr;
    c.headingFont = nullptr;
    c.textFonts   = nullptr;
    c.scale       = j.scale;
    c.pxPerMm     = j.pxPerMm;
    c.ui          = j.ui;
    c.theme       = &j.theme;
    c.style       = &j.style;
    c.area        = gfx::Rectf{0.0f, 0.0f, static_cast<float>(j.width),
                               static_cast<float>(j.height)};
    c.box         = j.box;
    return c;
}

// Ritläget ur jobbet — `c` är `jobCtx`s och lever lika länge som svaret.
PaintContext jobPaint(const CanvasFrameJob& j, const Ctx& c)
{
    PaintContext pc;
    pc.c        = &c;
    pc.res      = c.canvas;
    pc.world    = j.world;
    pc.screen   = j.screen;
    pc.zoom     = j.zoom;
    pc.opacity  = kOpaque;
    pc.lighting = &j.lighting;
    pc.scene    = j.scene.get();
    return pc;
}

// 3D-målets argument som bilden och beställningen delar.
Target3DArgs jobArgs(const CanvasFrameJob& j, const CanvasPaints& målare)
{
    Target3DArgs a;
    a.stamp       = j.stamp;
    a.samples     = j.samples;
    a.camera      = j.camera3 ? &*j.camera3 : nullptr;
    a.gridSize    = j.gridSize;
    a.gridColor   = j.gridColor;
    a.gridMinPx   = j.gridMinPx;
    a.gridWidthPx = j.gridWidthPx;
    a.paint       = målare.main;
    a.layerPaint  = målare.layers;
    return a;
}

} // namespace

std::optional<Bitmap> renderCanvasBitmap(const CanvasFrameJob& j, const Ctx& base,
                                         gfx::Color background)
{
    if (!j.scene || !j.camera3 || !base.r || !base.canvas)
        return std::nullopt;
    const Ctx           c = jobCtx(j, base);
    const MemoryAccount konto(c, j.scene);
    const PaintContext  pc     = jobPaint(j, c);
    const CanvasPaints  målare = paintsOf(j.paints);
    const Target3DArgs  a      = jobArgs(j, målare);
    return renderBitmap3D(pc, j.scene, a, j.width, j.height, background);
}

// ------------------------------------------------------------------ sidan

Ctx pageCtx(const PageJob& j, const Ctx& base)
{
    Ctx sc     = base;
    sc.box     = gfx::Rectf{0.0f, 0.0f, static_cast<float>(j.width),
                            static_cast<float>(j.height)};
    sc.area    = sc.box;
    sc.scale   = j.scale;
    sc.pxPerMm = static_cast<float>(j.pxPerMm);
    sc.ui      = j.ui;
    sc.theme   = &j.theme;
    sc.style   = &j.style;
    return sc;
}

PaintContext pagePaint(const PageJob& j, const Ctx& sc)
{
    PaintContext f;
    f.c      = &sc;
    f.res    = sc.canvas;
    f.world  = gfx::MapView{j.cx, j.cy, j.pxPerMm, 0.0, 0.0, false, sc.box};
    f.screen = localView(sc.box, sc.scale);
    f.zoom   = j.pxPerMm / sc.scale;
    return f;
}

bool renderPageJob(const PageJob& j, const Ctx& c, std::vector<std::uint8_t>& rgba,
                   bool& complete)
{
    complete = true;
    if (!c.r || !c.canvas)
        return false;
    gfx::Renderer& r  = *c.r;
    const Ctx      sc = pageCtx(j, c);
    // Sidans text byggs i egna byggen, så skärmens etikettträffar står orörda.
    struct SlutPåSida {
        CanvasResources& res;
        ~SlutPåSida() { res.setPageRender(false); }
    };
    for (int varv = 0;; ++varv) {
        if (!r.beginOffscreen())
            return false;   // inuti en frame — renderaren loggar orsaken
        // Varje varv börjar som en frame: släpp det döda, öppna budgeten. Sidor i
        // följd utan en fönsterframe emellan samlar då inga mål.
        c.canvas->trim();
        if (c.images)
            c.images->trim(j.memoryBudget, j.uploadBudget);
        c.canvas->setPageRender(true);
        const SlutPåSida slut{*c.canvas};
        const std::unique_ptr<gfx::RenderTarget> mål =
            r.createRenderTarget(j.width, j.height);
        if (!mål->image().valid()) {
            r.endOffscreen();
            gfx::log("Sidans mål (%d × %d) gick inte att skapa", j.width, j.height);
            return false;
        }
        r.pushTarget(*mål, kPaper);
        PaintContext f = pagePaint(j, sc);
        {
            // Målaren tömmer sin sista sats när den går ur räckvidd — innan
            // målet poppas.
            Painter p(f, false);
            replayPaint(p, j.paints);
        }
        r.popTarget();
        r.endOffscreen();
        if (c.images && c.images->uploadsDeferred()) {
            if (varv + 1 < j.maxRounds)
                continue;   // en bild väntade — sidan ritas om
            complete = false;
        }
        return !j.read || r.readTarget(*mål, rgba);
    }
}

namespace {

constexpr double        kDefaultFrameRate = 60.0;
constexpr double        kMaxFrameRate     = 1000.0;
constexpr double        kMsPerSecond      = 1000.0;
constexpr std::uint64_t kRefreshPollMs    = 1000;
// En skärm som säger en takt under den här säger ingenting (0 och 1 är
// OS:ets "förvald").
constexpr float         kMinRefreshHz     = 2.0f;

} // namespace

double defaultFrameRate()
{
    return kDefaultFrameRate;
}

bool validFrameRate(double fps)
{
    return std::isfinite(fps) && fps > 0.0 && fps <= kMaxFrameRate;
}

double frameBudgetFor(double hz)
{
    return kMsPerSecond / (validFrameRate(hz) ? hz : kDefaultFrameRate);
}

double refreshRateAt(RefreshCache& cache, gfx::Pointf p)
{
    const std::uint64_t nu = gfx::ticksMs();
    if (cache.valid && nu - cache.at < kRefreshPollMs)
        return cache.hz;
    const std::vector<gfx::ScreenInfo> skärmar = gfx::screens();
    const gfx::ScreenInfo*             s       = screenFor(skärmar, p);
    cache.hz    = s && s->refreshHz >= kMinRefreshHz ? s->refreshHz : kDefaultFrameRate;
    cache.at    = nu;
    cache.valid = true;
    return cache.hz;
}

CanvasFrameBox canvasFrameBox(const gfx::Rectf& windowBox)
{
    CanvasFrameBox b;
    b.box    = targetBox(windowBox);
    b.x      = std::floor(windowBox.x);
    b.y      = std::floor(windowBox.y);
    b.width  = static_cast<int>(std::ceil(b.box.x + b.box.w));
    b.height = static_cast<int>(std::ceil(b.box.y + b.box.h));
    return b;
}

CanvasFrameResult renderCanvasFrame(const CanvasFrameJob& j, const Ctx& base,
                                    gfx::RenderTarget& out, FramePace* pace)
{
    CanvasFrameResult ut;
    ut.serial = j.serial;
    if (!j.scene || !base.r || !base.canvas)
        return ut;

    const Ctx           c   = jobCtx(j, base);
    CanvasResources&    res = *c.canvas;
    gfx::Renderer&      r   = *c.r;
    // Det ritningen hämtar räknas på ritytans konto.
    const MemoryAccount konto(c, j.scene);

    PaintContext       pc     = jobPaint(j, c);
    const CanvasPaints målare = paintsOf(j.paints);
    bool               visas  = true;   // bilden läggs i `out`

    if (j.target3D) {
        // 3D-MÅLEN (target3d.h): målen ritas där de är inaktuella, och
        // kompositen läggs i bilden i stället för i fönstret.
        PaintContext tc = pc;
        tc.pace         = pace;
        Target3DArgs a  = jobArgs(j, målare);
        a.labelFade     = j.labelFade;
        a.views         = j.views;
        std::vector<TargetImage> bilder;
        const Target3DState läge = renderTargets3D(pc, tc, j.scene, a, bilder);
        ut.labelsAnimating       = läge.labelsAnimating;
        ut.drapesPending         = läge.drapesPending;
        // BILDTAKTEN: ett steg i ritordning visas först när passet är klart
        // — den bild som visas står kvar till dess.
        ut.complete = !pace || pace->complete;
        visas = ut.complete || pace->order == FramePace::Order::Priority;
        if (visas) {
            r.pushTarget(out, kClear);
            for (const TargetImage& b : bilder)
                r.drawImage(*b.image, b.rect, kOpaque);
            r.popTarget();
        }
    } else {
        // 2D-VÄGEN, i bilden: förberedelsen, rutnätet, lagren med sina
        // målare, resten av uppladdningarna och `onPaint` — ritytans ordning
        // (widgets/canvas.cpp `drawScene`).
        res.dropViews(j.scene->id, kMainView);
        const LayerFrame lf = prepareLayers(pc, j.scene, j.labelFade, false);
        pc.clip             = lf.clips.canvas;
        r.pushTarget(out, kClear);
        drawGrid(pc, j.gridSize, j.gridColor, j.gridMinPx, j.gridWidthPx);
        drawGround(pc, *j.scene, lf, GroundPart::All, &målare.layers);
        ut.labelsAnimating = lf.animating();
        prepareUploads(pc, *j.scene);
        if (målare.main)
            målare.main(pc);
        r.popTarget();
    }

    ut.drawn           = visas;
    ut.needsUiThread   = res.takeTextureRefused();
    ut.uploadsDeferred = c.images && c.images->uploadsDeferred();
    ut.memory          = res.ledger().usage(j.scene->id);
    res.exportLabels(*j.scene, ut.labels);
    return ut;
}

} // namespace glaze::detail
