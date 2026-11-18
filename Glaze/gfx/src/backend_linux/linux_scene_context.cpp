// Scen-kontexten i Linux-backenden (gfx/scene_context.h): en ANDRA GL-KONTEXT
// i fönsterkontextens DELNINGSGRUPP, aktuell på scentråden. Texturer,
// buffertar, program och synkobjekt delas i gruppen; FBO:er och VAO:er gör det
// inte, så varje renderare har sina egna.
//
// YTAN. Scen-renderaren ritar bara i render-mål och behöver ingen yta:
// kontexten görs aktuell utan (EGL_KHR_surfaceless_context — det den
// fönsterlösa displayen redan bygger på). Vägrar EGL det prövas en 1 × 1
// pbuffer; går inte den heller finns ingen scen-kontext, och anroparen ritar
// i fönstrets tråd.
//
// DEN DELADE BILDEN är scen-renderarens render-mål (FBO:n i scen-kontexten,
// texturen i gruppen) och, för fönstret, samma textur som en bild som INTE
// äger den. Ordningen mellan kontexterna:
//
//   scen → fönster   `publish` gör glFinish på scentråden: bilden är färdig
//                    på GPU:n innan fönstret får den. Fönstrets ritväg binder
//                    texturen vid varje ritning, så det nya innehållet syns
//                    (en ändring i en annan kontext gäller först efter en
//                    ny bindning).
//   fönster → scen   `retire` lägger en synkpunkt efter fönstrets kommandon
//                    och skickar dem (glFlush); `beginWrite` väntar in den i
//                    scen-kontexten innan något ritas i texturen igen.
//
// Fönstret väntar aldrig: `acquire` är ett lägesbyte och `retire` en
// synkpunkt utan väntan.
//
// LÄGET (`state_`) är atomiskt för läsningens skull; övergångarna görs av en
// sida i taget (anroparen växlar bilderna under sitt eget lås).

#include <atomic>
#include <mutex>

#include "gfx/platform.h"
#include "gfx/scene_context.h"
#include "gl_common.h"
#include "linux_backend.h"

namespace gfx {
namespace {

// Längsta väntan i `beginWrite` på fönstrets sista läsning, i nanosekunder.
// Efter den ges det upp (loggat): scentråden ska gå att stoppa.
constexpr GLuint64 kWriteWaitNs = 2'000'000'000ull;

class GlSharedTarget final : public SharedTarget {
public:
    GlSharedTarget(GlRenderer& scene, GlRenderer& window, int w, int h)
        : scene_(scene), window_(window), w_(w), h_(h)
    {
    }

    // Rivs på scentråden, med scen-kontexten aktuell.
    ~GlSharedTarget() override
    {
        if (lastRead_)
            pglDeleteSync(lastRead_);
    }

    bool create()
    {
        std::unique_ptr<RenderTarget> t =
            scene_.createRenderTarget(w_, h_, TargetDesc{});
        if (!t || !t->image().valid()) {
            log("GL: den delade bilden %d × %d gick inte att skapa", w_, h_);
            return false;
        }
        target_.reset(static_cast<GlRenderTarget*>(t.release()));
        // Fönstrets bild: samma textur, utan renderare — den äger ingenting.
        // Speglad och premultiplicerad som varje måls bild.
        image_ = std::make_unique<GlImage>(nullptr,
                                           target_->imageView().tex(), w_, h_,
                                           ImageFormat::Rgba8, true, true,
                                           false);
        return true;
    }

    int width() const override { return w_; }
    int height() const override { return h_; }

    bool beginWrite() override
    {
        const State s = state_.load();
        if (s == State::Held || s == State::Writing) {
            log("SharedTarget::beginWrite: bilden %s",
                s == State::Held ? "hålls av fönstret" : "skrivs redan");
            return false;
        }
        GLsync sista = nullptr;
        {
            const std::lock_guard<std::mutex> vakt(lock_);
            sista     = lastRead_;
            lastRead_ = nullptr;
        }
        if (sista) {
            const GLenum svar = pglClientWaitSync(sista, 0, kWriteWaitNs);
            pglDeleteSync(sista);
            if (svar != GL_ALREADY_SIGNALED &&
                svar != GL_CONDITION_SATISFIED) {
                log("SharedTarget::beginWrite: fönstrets läsning blev inte "
                    "klar (0x%04x)",
                    svar);
                return false;
            }
        }
        state_.store(State::Writing);
        return true;
    }

    RenderTarget& target() override { return *target_; }

    void publish() override
    {
        if (state_.load() != State::Writing)
            return;
        glFinish();   // bilden färdig på GPU:n — väntan är scentrådens
        state_.store(State::Published);
    }

    Image* acquire() override
    {
        State väntad = State::Published;
        if (!state_.compare_exchange_strong(väntad, State::Held))
            return nullptr;
        return image_.get();
    }

    void retire() override
    {
        if (state_.load() != State::Held)
            return;
        GLsync ny = nullptr;
        if (window_.makeCurrent()) {
            ny = pglFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            glFlush();   // synkpunkten når GPU:n — scentråden väntar på den
        }
        GLsync gammal = nullptr;
        {
            const std::lock_guard<std::mutex> vakt(lock_);
            gammal    = lastRead_;
            lastRead_ = ny;
        }
        if (gammal)
            pglDeleteSync(gammal);
        state_.store(State::Free);
    }

private:
    enum class State { Free, Writing, Published, Held };

    GlRenderer& scene_;
    GlRenderer& window_;
    int         w_, h_;

    std::unique_ptr<GlRenderTarget> target_;   // scen-kontextens
    std::unique_ptr<GlImage>        image_;    // fönstrets, äger inte texturen
    std::mutex                      lock_;
    GLsync                          lastRead_ = nullptr;
    std::atomic<State>              state_{State::Free};
};

class GlSceneContext final : public SceneContext {
public:
    explicit GlSceneContext(GlRenderer& window) : window_(window) {}

    // Rivs på scentråden.
    ~GlSceneContext() override
    {
        const EGLDisplay dpy = window_.eglDisplay();
        scene_.shutdownRenderer();
        if (pbuffer_ != EGL_NO_SURFACE && dpy != EGL_NO_DISPLAY)
            eglDestroySurface(dpy, pbuffer_);
    }

    // Kontexten skapas och görs aktuell på den anropande tråden — scentråden.
    bool init()
    {
        const EGLDisplay dpy   = window_.eglDisplay();
        const EGLConfig  cfg   = window_.eglConfig();
        const EGLContext share = window_.eglContext();
        if (dpy == EGL_NO_DISPLAY || share == EGL_NO_CONTEXT)
            return false;
        if (scene_.init(dpy, cfg, EGL_NO_SURFACE, 1, 1, false, share))
            return true;
        // Ytlöst gick inte — en pbuffer som yta.
        const EGLint attr[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        pbuffer_ = eglCreatePbufferSurface(dpy, cfg, attr);
        if (pbuffer_ == EGL_NO_SURFACE) {
            log("GL: scen-kontexten kan inte göras aktuell utan yta, och "
                "konfigen har ingen pbuffer (0x%04x) — ritytan ritar i "
                "fönstrets tråd",
                eglGetError());
            return false;
        }
        log("GL: scen-kontexten tar en pbuffer som yta");
        if (scene_.init(dpy, cfg, pbuffer_, 1, 1, false, share))
            return true;
        eglDestroySurface(dpy, pbuffer_);
        pbuffer_ = EGL_NO_SURFACE;
        return false;
    }

    // API-bindningen gäller per tråd, och kontexten är aktuell på en tråd i
    // taget.
    bool attachThread() override
    {
        eglBindAPI(EGL_OPENGL_API);
        return scene_.makeCurrent();
    }
    void detachThread() override { scene_.releaseCurrent(); }

    Renderer& renderer() override { return scene_; }

    std::unique_ptr<SharedTarget> createSharedTarget(int w, int h) override
    {
        auto t = std::make_unique<GlSharedTarget>(scene_, window_, w, h);
        if (!t->create())
            return nullptr;
        return t;
    }

private:
    GlRenderer& window_;
    GlRenderer  scene_;
    EGLSurface  pbuffer_ = EGL_NO_SURFACE;
};

} // namespace

std::unique_ptr<SceneContext> GlRenderer::createSceneContext()
{
    if (!ready())
        return nullptr;
    auto ctx = std::make_unique<GlSceneContext>(*this);
    if (!ctx->init())
        return nullptr;
    return ctx;
}

} // namespace gfx
