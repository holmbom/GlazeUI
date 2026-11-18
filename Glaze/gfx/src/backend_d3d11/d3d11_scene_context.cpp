// Scen-kontexten i D3D11-backenden (gfx/scene_context.h): en ANDRA ENHET på
// fönsterenhetens adapter, med en egen omedelbar kontext som scentråden äger,
// och delade bilder som fönstrets enhet öppnar.
//
// DEN DELADE BILDEN är en B8G8R8A8-textur skapad på scen-enheten med
// SHARED_NTHANDLE | SHARED_KEYEDMUTEX och öppnad på fönsterenheten med
// `OpenSharedResource1`. En delad textur har ett prov och en nivå — MSAA och
// djup ritas i scen-renderarens egna mål och läggs in med drawImage.
//
// KEYED MUTEX bär ägandet mellan enheterna, med två nycklar:
//
//   kKeyFree (0)       ledig: scen-sidan tar den för skrivning
//   kKeyPublished (1)  publicerad: fönster-sidan tar den för visning — eller
//                      scen-sidan tar tillbaka den, om fönstret aldrig tog den
//
//   scen:    AcquireSync(0 | 1) → rita → ReleaseSync(1)
//   fönster: AcquireSync(1, 0)  → visa → ReleaseSync(0)
//
// Fönster-sidans AcquireSync har tiden 0 och väntar aldrig. `publish` väntar in
// GPU:n på scentråden (`EnqueueSetEvent`) FÖRE sin ReleaseSync, så fönstrets
// läsning aldrig ställs bakom scenens arbete på grafikkortet.
//
// LÄGET (`state_`) säger vilken nyckel som gäller och vem som håller bilden.
// Det är atomiskt för läsningens skull; övergångarna görs av en sida i taget
// (anroparen växlar bilderna under sitt eget lås).

#include <atomic>

#include "d3d11_backend.h"
#include "gfx/platform.h"
#include "gfx/scene_context.h"

namespace gfx {
namespace {

constexpr UINT64 kKeyFree      = 0;
constexpr UINT64 kKeyPublished = 1;

// Längsta väntan i `beginWrite` på att fönstrets sista läsning ska vara klar,
// och i `publish` på GPU:ns arbete med bilden. Efter den ges det upp (loggat):
// scentråden får aldrig stå för evigt, den ska gå att stoppa.
constexpr DWORD kWriteWaitMs = 2000;
constexpr DWORD kGpuWaitMs   = 5000;

class D3dSharedTarget final : public SharedTarget {
public:
    D3dSharedTarget(D3dRenderer& scene, D3dRenderer& window, int w, int h)
        : scene_(scene), window_(window), w_(w), h_(h)
    {
    }

    ~D3dSharedTarget() override
    {
        if (gpuDone_)
            CloseHandle(gpuDone_);
    }

    // Texturen på scen-enheten, handtaget, och samma textur öppnad på
    // fönsterenheten. false (+ logg) när något steg vägras.
    bool create()
    {
        ID3D11Device* sceneDev  = scene_.device();
        ID3D11Device* windowDev = window_.device();
        if (!sceneDev || !windowDev || w_ <= 0 || h_ <= 0)
            return false;

        D3D11_TEXTURE2D_DESC td{};
        td.Width            = static_cast<UINT>(w_);
        td.Height           = static_cast<UINT>(h_);
        td.MipLevels        = 1;
        td.ArraySize        = 1;
        td.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage            = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
                       D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

        ComPtr<ID3D11Texture2D>          tex;
        ComPtr<ID3D11RenderTargetView>   rtv;
        ComPtr<ID3D11ShaderResourceView> srv;
        HRESULT hr = sceneDev->CreateTexture2D(&td, nullptr, &tex);
        if (SUCCEEDED(hr))
            hr = sceneDev->CreateRenderTargetView(tex.Get(), nullptr, &rtv);
        if (SUCCEEDED(hr))
            hr = sceneDev->CreateShaderResourceView(tex.Get(), nullptr, &srv);
        if (SUCCEEDED(hr))
            hr = tex.As(&sceneMutex_);
        if (FAILED(hr)) {
            log("D3D11: den delade bilden %d × %d gick inte att skapa "
                "(0x%08lx)",
                w_, h_, hr);
            return false;
        }

        // Handtaget behövs bara tills fönsterenheten öppnat texturen.
        ComPtr<IDXGIResource1> delad;
        HANDLE                 handtag = nullptr;
        hr = tex.As(&delad);
        if (SUCCEEDED(hr))
            hr = delad->CreateSharedHandle(
                nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                nullptr, &handtag);
        ComPtr<ID3D11Device1>            windowDev1;
        ComPtr<ID3D11Texture2D>          windowTex;
        ComPtr<ID3D11ShaderResourceView> windowSrv;
        if (SUCCEEDED(hr))
            hr = windowDev->QueryInterface(IID_PPV_ARGS(&windowDev1));
        if (SUCCEEDED(hr))
            hr = windowDev1->OpenSharedResource1(handtag,
                                                 IID_PPV_ARGS(&windowTex));
        if (handtag)
            CloseHandle(handtag);
        if (SUCCEEDED(hr))
            hr = windowDev->CreateShaderResourceView(windowTex.Get(), nullptr,
                                                     &windowSrv);
        if (SUCCEEDED(hr))
            hr = windowTex.As(&windowMutex_);
        if (FAILED(hr)) {
            log("D3D11: den delade bilden %d × %d gick inte att öppna på "
                "fönstrets enhet (0x%08lx)",
                w_, h_, hr);
            return false;
        }

        gpuDone_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!gpuDone_) {
            log("D3D11: den delade bildens händelse gick inte att skapa");
            return false;
        }

        target_ = std::make_unique<D3dRenderTarget>(
            std::move(tex), std::move(srv), nullptr, std::move(rtv), nullptr,
            w_, h_, 1, false);
        image_ = std::make_unique<D3dImage>(std::move(windowTex),
                                            std::move(windowSrv), w_, h_,
                                            ImageFormat::Rgba8, true);
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
        const UINT64  nyckel = s == State::Published ? kKeyPublished : kKeyFree;
        const HRESULT hr     = sceneMutex_->AcquireSync(nyckel, kWriteWaitMs);
        if (hr != S_OK) {
            log("SharedTarget::beginWrite: bilden gick inte att ta (0x%08lx)",
                hr);
            return false;
        }
        state_.store(State::Writing);
        return true;
    }

    RenderTarget& target() override { return *target_; }

    void publish() override
    {
        if (state_.load() != State::Writing)
            return;
        waitForGpu();
        sceneMutex_->ReleaseSync(kKeyPublished);
        state_.store(State::Published);
    }

    Image* acquire() override
    {
        if (state_.load() != State::Published)
            return nullptr;
        if (windowMutex_->AcquireSync(kKeyPublished, 0) != S_OK)
            return nullptr;
        state_.store(State::Held);
        return image_.get();
    }

    void retire() override
    {
        if (state_.load() != State::Held)
            return;
        // Fönstrets ritningar av bilden skickas till GPU:n innan den lämnas —
        // scen-sidans nästa skrivning ställs efter dem.
        if (ID3D11DeviceContext* c = window_.context())
            c->Flush();
        windowMutex_->ReleaseSync(kKeyFree);
        state_.store(State::Free);
    }

private:
    enum class State { Free, Writing, Published, Held };

    // Scen-enhetens inskickade arbete väntas in: händelsen sätts när GPU:n
    // kört allt som skickats före den.
    void waitForGpu()
    {
        ID3D11DeviceContext* c = scene_.context();
        ID3D11Device*        d = scene_.device();
        ComPtr<IDXGIDevice2> dxgi;
        if (!c || !d || FAILED(d->QueryInterface(IID_PPV_ARGS(&dxgi)))) {
            if (c)
                c->Flush();
            return;
        }
        ResetEvent(gpuDone_);
        if (FAILED(dxgi->EnqueueSetEvent(gpuDone_))) {
            c->Flush();
            return;
        }
        if (WaitForSingleObject(gpuDone_, kGpuWaitMs) != WAIT_OBJECT_0 &&
            !waitLogged_) {
            waitLogged_ = true;
            log("SharedTarget::publish: GPU:n blev inte klar inom %lu ms — "
                "bilden lämnas ändå",
                kGpuWaitMs);
        }
    }

    D3dRenderer& scene_;
    D3dRenderer& window_;
    int          w_, h_;

    std::unique_ptr<D3dRenderTarget> target_;        // scen-enhetens
    std::unique_ptr<D3dImage>        image_;         // fönsterenhetens
    ComPtr<IDXGIKeyedMutex>          sceneMutex_;
    ComPtr<IDXGIKeyedMutex>          windowMutex_;
    HANDLE                           gpuDone_    = nullptr;
    bool                             waitLogged_ = false;
    std::atomic<State>               state_{State::Free};
};

class D3dSceneContext final : public SceneContext {
public:
    explicit D3dSceneContext(D3dRenderer& window) : window_(window) {}
    ~D3dSceneContext() override { scene_.shutdownRenderer(); }

    bool init(IDXGIAdapter* adapter)
    {
        return scene_.initHeadless(adapter, window_.deviceName(),
                                   window_.adapterId());
    }

    // Enhetens kontext är inte bunden till någon tråd — den får bara inte
    // användas av två samtidigt, och det är scentrådens ensam.
    bool attachThread() override { return scene_.ready(); }
    void detachThread() override {}

    Renderer& renderer() override { return scene_; }

    std::unique_ptr<SharedTarget> createSharedTarget(int w, int h) override
    {
        auto t = std::make_unique<D3dSharedTarget>(scene_, window_, w, h);
        if (!t->create())
            return nullptr;
        return t;
    }

private:
    D3dRenderer& window_;
    D3dRenderer  scene_;
};

} // namespace

std::unique_ptr<SceneContext> D3dRenderer::createSceneContext()
{
    if (!device_)
        return nullptr;
    // Fönsterenhetens adapter — också efter WARP-reserven: en delad textur
    // öppnas bara på samma kort.
    ComPtr<IDXGIDevice>  dxgi;
    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(device_.As(&dxgi)) || FAILED(dxgi->GetAdapter(&adapter))) {
        log("D3D11: scen-kontexten fick ingen adapter");
        return nullptr;
    }
    auto ctx = std::make_unique<D3dSceneContext>(*this);
    if (!ctx->init(adapter.Get()))
        return nullptr;
    return ctx;
}

} // namespace gfx
