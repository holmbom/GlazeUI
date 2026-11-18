// Scen-kontexten i Metal-backenden (gfx/scene_context.h): fönstrets ENHET med
// en EGEN KOMMANDOKÖ, som scentråden ensam kodar i. Enheten delas — samma
// texturer syns från båda köerna — så den delade bilden är EN textur med två
// ansikten: scen-renderarens render-mål och fönsterrenderarens bild.
//
// ORDNINGEN mellan köerna hålls av att var sida väntar in sitt eget arbete
// innan bilden byter ägare:
//
//   scen → fönster   `endOffscreen` committar blocket och väntar in det, så
//                    en publicerad bild är färdigritad på GPU:n.
//   fönster → scen   `retire` sparar fönsterrenderarens senast inskickade
//                    frame, och `beginWrite` väntar in den innan scenen ritar
//                    i texturen igen.
//
// `retire` anropas därför först när framen som sist ritade bilden är
// inskickad (present) — inte mitt i den.
//
// LÄGET (`state_`) är atomiskt för läsningens skull; övergångarna görs av en
// sida i taget (anroparen växlar bilderna under sitt eget lås).

#include <atomic>
#include <mutex>

#include "gfx/platform.h"
#include "gfx/scene_context.h"
#include "metal_backend.h"

namespace gfx {
namespace {

class MetalSharedTarget final : public SharedTarget {
public:
    MetalSharedTarget(MetalRenderer& window, int w, int h)
        : window_(window), w_(w), h_(h)
    {
    }

    bool create(id<MTLDevice> device)
    {
        if (!device || w_ <= 0 || h_ <= 0)
            return false;
        MTLTextureDescriptor* td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                         width:(NSUInteger)w_
                                        height:(NSUInteger)h_
                                     mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        td.storageMode     = MTLStorageModePrivate;
        id<MTLTexture> tex = [device newTextureWithDescriptor:td];
        if (!tex) {
            log("Metal: den delade bilden %d × %d gick inte att skapa", w_, h_);
            return false;
        }
        target_ = std::make_unique<MetalRenderTarget>(tex, nil, nil, w_, h_, 1,
                                                      false);
        image_  = std::make_unique<MetalImage>(tex, w_, h_, ImageFormat::Rgba8,
                                               true, false);
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
        // Fönstrets sista läsning av bilden väntas in.
        id<MTLCommandBuffer> sista = nil;
        {
            const std::lock_guard<std::mutex> vakt(lock_);
            sista       = lastRead_;
            lastRead_   = nil;
        }
        if (sista)
            [sista waitUntilCompleted];
        state_.store(State::Writing);
        return true;
    }

    RenderTarget& target() override { return *target_; }

    // `endOffscreen` har redan väntat in blockets arbete.
    void publish() override
    {
        if (state_.load() == State::Writing)
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
        {
            const std::lock_guard<std::mutex> vakt(lock_);
            lastRead_ = window_.lastCommitted();
        }
        state_.store(State::Free);
    }

private:
    enum class State { Free, Writing, Published, Held };

    MetalRenderer& window_;
    int            w_, h_;

    std::unique_ptr<MetalRenderTarget> target_;   // scen-renderarens
    std::unique_ptr<MetalImage>        image_;    // fönsterrenderarens
    std::mutex                         lock_;
    id<MTLCommandBuffer>               lastRead_ = nil;
    std::atomic<State>                 state_{State::Free};
};

class MetalSceneContext final : public SceneContext {
public:
    explicit MetalSceneContext(MetalRenderer& window) : window_(window) {}
    ~MetalSceneContext() override { scene_.shutdownRenderer(); }

    bool init()
    {
        return scene_.initHeadless(window_.device(), window_.deviceName(),
                                   window_.adapterId());
    }

    // Kön är inte bunden till någon tråd — den får bara inte kodas i av två
    // samtidigt, och det är scentrådens ensam.
    bool attachThread() override { return scene_.device() != nil; }
    void detachThread() override {}

    Renderer& renderer() override { return scene_; }

    std::unique_ptr<SharedTarget> createSharedTarget(int w, int h) override
    {
        auto t = std::make_unique<MetalSharedTarget>(window_, w, h);
        if (!t->create(scene_.device()))
            return nullptr;
        return t;
    }

private:
    MetalRenderer& window_;
    MetalRenderer  scene_;
};

} // namespace

std::unique_ptr<SceneContext> MetalRenderer::createSceneContext()
{
    if (!device_)
        return nullptr;
    auto ctx = std::make_unique<MetalSceneContext>(*this);
    if (!ctx->init())
        return nullptr;
    return ctx;
}

} // namespace gfx
