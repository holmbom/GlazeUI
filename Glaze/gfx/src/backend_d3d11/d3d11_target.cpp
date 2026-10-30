// Render-målen i D3D11-backenden: en textur med RTV
// och SRV, bunden med `bindTarget` — RTV:n, viewporten och UI:ts b0
// (px → NDC) får MÅLETS storlek, och scenens program räknar mot samma
// `targetW_/targetH_`. Klippstacken sparas och läggs tillbaka av fasadens
// pushTarget/popTarget (renderer.cpp); här byts bara målet.
//
// D3D11 har rad 0 överst i alla texturer, så ett måls bild är rättvänd
// (`flippedV` falskt) — men PREMULTIPLICERAD, som allt som ritats med rak
// alfa mot en genomskinlig yta.
//
// DJUP OCH MSAA: ett mål med provtagning > 1 ritas i en
// EGEN provtagen B8G8R8A8-textur och löses upp till bildens textur med
// `ResolveSubresource` när det LÄMNAS — i bindTarget, alltså både i popTarget
// och när ett nästlat mål binds ovanpå (bilden kan då ritas i det nästlade).
// Djupet är en D32F-textur med samma provtagning, bunden som DSV bredvid
// RTV:n; `clear` rensar det till 1. Vilka provtagningar som går frågas en gång
// i init (`msaaOk_`).
//
// EXTERNA MÅL (`importRenderTarget`): bilden är en textur som någon annan
// skapat på SAMMA enhet, i en RGBA8- eller BGRA8-familj. Målet skriver den
// genom en egen UNORM-vy — aldrig en sRGB-vy: färgerna är redan sRGB-kodade,
// och en kompositor som läser bilden som sRGB avkodar dem själv. Med MSAA ritas
// det i en egen provtagen yta i samma UNORM-format, som löses upp till bilden
// i FAMILJENS format när målet lämnas. Den provtagna ytan och djupet delas med
// ett annat importerat mål (`sharedAttachments`) genom att COM-referenserna
// kopieras. Importen rensar ingenting; `pushTarget` rensar som vanligt.
// `insertCompletion` är en händelsefråga som pollas utan väntan.

#include <cstdio>
#include <string>

#include "../image_util.h"
#include "d3d11_backend.h"
#include "gfx/platform.h"

namespace gfx {

namespace {

// Hör D3D11-objektet `obj` till enheten `dev`? Jämförs som COM-identitet
// (IUnknown), inte som gränssnittspekare.
bool onDevice(ID3D11DeviceChild* obj, ID3D11Device* dev)
{
    if (!obj || !dev)
        return false;
    ComPtr<ID3D11Device> objDevice;
    obj->GetDevice(&objDevice);
    ComPtr<IUnknown> a, b;
    return objDevice && SUCCEEDED(objDevice.As(&a)) &&
           SUCCEEDED(ComPtr<ID3D11Device>(dev).As(&b)) && a.Get() == b.Get();
}

// Går provtagningen `n` för färgformatet (och D32F när djup önskas)?
bool samplesSupported(ID3D11Device& dev, DXGI_FORMAT color, bool depth, int n)
{
    if (n <= 1)
        return true;
    UINT qc = 0, qd = 0;
    if (FAILED(dev.CheckMultisampleQualityLevels(color, static_cast<UINT>(n),
                                                 &qc)) ||
        qc == 0)
        return false;
    return !depth ||
           (SUCCEEDED(dev.CheckMultisampleQualityLevels(
                DXGI_FORMAT_D32_FLOAT, static_cast<UINT>(n), &qd)) &&
            qd > 0);
}

// Den provtagna färgytan med sin RTV, i formatet `fmt`.
bool createMsaaColor(ID3D11Device& dev, int w, int h, DXGI_FORMAT fmt,
                     int samples, ComPtr<ID3D11Texture2D>& tex,
                     ComPtr<ID3D11RenderTargetView>& rtv)
{
    D3D11_TEXTURE2D_DESC md{};
    md.Width              = static_cast<UINT>(w);
    md.Height             = static_cast<UINT>(h);
    md.MipLevels          = 1;
    md.ArraySize          = 1;
    md.Format             = fmt;
    md.SampleDesc.Count   = static_cast<UINT>(samples);
    md.SampleDesc.Quality = 0;
    md.Usage              = D3D11_USAGE_DEFAULT;
    md.BindFlags          = D3D11_BIND_RENDER_TARGET;
    return SUCCEEDED(dev.CreateTexture2D(&md, nullptr, &tex)) &&
           SUCCEEDED(dev.CreateRenderTargetView(tex.Get(), nullptr, &rtv));
}

// D32F-djupet med samma provtagning som färgen, med sin DSV.
bool createDepthView(ID3D11Device& dev, int w, int h, int samples,
                     ComPtr<ID3D11DepthStencilView>& dsv)
{
    D3D11_TEXTURE2D_DESC dd{};
    dd.Width              = static_cast<UINT>(w);
    dd.Height             = static_cast<UINT>(h);
    dd.MipLevels          = 1;
    dd.ArraySize          = 1;
    dd.Format             = DXGI_FORMAT_D32_FLOAT;
    dd.SampleDesc.Count   = static_cast<UINT>(samples);
    dd.SampleDesc.Quality = 0;
    dd.Usage              = D3D11_USAGE_DEFAULT;
    dd.BindFlags          = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D>       depthTex;
    D3D11_DEPTH_STENCIL_VIEW_DESC dvd{};
    dvd.Format        = DXGI_FORMAT_D32_FLOAT;
    dvd.ViewDimension = samples > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS
                                    : D3D11_DSV_DIMENSION_TEXTURE2D;
    return SUCCEEDED(dev.CreateTexture2D(&dd, nullptr, &depthTex)) &&
           SUCCEEDED(dev.CreateDepthStencilView(depthTex.Get(), &dvd, &dsv));
}

// En extern bilds format → färgfamiljen den hör till; falskt utanför
// RGBA8- och BGRA8-familjerna.
bool familyOf(DXGI_FORMAT f, TargetColorFormat& out)
{
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        out = TargetColorFormat::Rgba8Unorm;
        return true;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        out = TargetColorFormat::Bgra8Unorm;
        return true;
    default:
        return false;
    }
}

std::string sizeText(int w, int h)
{
    return std::to_string(w) + " × " + std::to_string(h);
}

// "<vad> (0x…)" — ett HRESULT i felets text.
std::string hrText(const char* what, HRESULT hr)
{
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s (0x%08lx)", what,
                  static_cast<unsigned long>(hr));
    return buf;
}

// Markören `insertCompletion` ger: en händelsefråga i kommandoströmmen.
class D3dGpuCompletion : public GpuCompletion {
public:
    D3dGpuCompletion(ComPtr<ID3D11Device> dev, ComPtr<ID3D11DeviceContext> ctx,
                     ComPtr<ID3D11Query> q)
        : dev_(std::move(dev)), ctx_(std::move(ctx)), q_(std::move(q))
    {
    }

    GpuCompletionState poll(std::string& error) override
    {
        if (state_ != GpuCompletionState::Pending)
            return state_;
        // En förlorad enhet svarar aldrig mer — terminalt.
        const HRESULT removed = dev_->GetDeviceRemovedReason();
        if (FAILED(removed)) {
            error  = hrText("grafikenheten har förlorats", removed);
            state_ = GpuCompletionState::Failed;
            return state_;
        }
        BOOL          klar = FALSE;
        const HRESULT hr =
            ctx_->GetData(q_.Get(), &klar, sizeof klar,
                          D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (FAILED(hr)) {
            error  = hrText("frågan om färdig GPU-körning misslyckades", hr);
            state_ = GpuCompletionState::Failed;
        } else if (hr == S_OK && klar) {
            state_ = GpuCompletionState::Complete;
        }
        return state_;
    }

private:
    ComPtr<ID3D11Device>        dev_;
    ComPtr<ID3D11DeviceContext> ctx_;
    ComPtr<ID3D11Query>         q_;
    GpuCompletionState          state_ = GpuCompletionState::Pending;
};

} // namespace

int D3dRenderer::supportedSamples(int requested) const
{
    // Den närmaste stödda ≤ det begärda (8, 4, 2), annars ett prov.
    for (int i = 2; i >= 0; --i)
        if ((2 << i) <= requested && msaaOk_[i])
            return 2 << i;
    return 1;
}

std::unique_ptr<RenderTarget> D3dRenderer::createRenderTarget(
    int w, int h, const TargetDesc& d)
{
    if (!device_ || !context_ || !targetDescOk(w, h, d))
        return std::make_unique<D3dRenderTarget>();
    const int samples = supportedSamples(d.samples);
    if (samples < d.samples && !samplesLogged_) {
        samplesLogged_ = true;
        log("createRenderTarget: %d prov stöds inte av adaptern — %d", d.samples,
            samples);
    }

    // BILDEN: B8G8R8A8 som retained-RT:n — samma format som allt annat UI:t
    // ritar i. Den är också RTV:ns textur utan MSAA; med MSAA bara upplösningens
    // mål, men RTV-bindningen behövs ändå för att nollställa den.
    // MED MIP-KEDJA är det hela kedjan med GENERATE_MIPS; RTV:n
    // (en nollad desc) sitter på nivå 0 och SRV:n ser alla nivåer.
    D3D11_TEXTURE2D_DESC td{};
    td.Width            = static_cast<UINT>(w);
    td.Height           = static_cast<UINT>(h);
    td.MipLevels        = d.mipmaps ? 0 : 1;
    td.ArraySize        = 1;
    td.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = d.mipmaps ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
    ComPtr<ID3D11Texture2D>          tex;
    ComPtr<ID3D11RenderTargetView>   imageRtv;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &tex)) ||
        FAILED(device_->CreateRenderTargetView(tex.Get(), nullptr,
                                               &imageRtv)) ||
        FAILED(device_->CreateShaderResourceView(tex.Get(), nullptr, &srv))) {
        log("createRenderTarget: %d × %d kunde inte skapas", w, h);
        return std::make_unique<D3dRenderTarget>();
    }

    // DEN PROVTAGNA YTAN (bara med MSAA) — det RTV:n sitter på då.
    ComPtr<ID3D11Texture2D>        msaa;
    ComPtr<ID3D11RenderTargetView> rtv = imageRtv;
    if (samples > 1) {
        // Kedjan är bildens, inte provens — den provtagna ytan har en nivå.
        rtv.Reset();
        if (!createMsaaColor(*device_.Get(), w, h, DXGI_FORMAT_B8G8R8A8_UNORM,
                             samples, msaa, rtv)) {
            log("createRenderTarget: %d × %d med %d prov kunde inte skapas", w,
                h, samples);
            return std::make_unique<D3dRenderTarget>();
        }
    }

    // DJUPET: D32F med samma provtagning.
    ComPtr<ID3D11DepthStencilView> dsv;
    if (d.depth && !createDepthView(*device_.Get(), w, h, samples, dsv)) {
        log("createRenderTarget: djupet på %d × %d kunde inte skapas", w, h);
        return std::make_unique<D3dRenderTarget>();
    }

    // Nollställt (fasadens löfte) — en ny textur har odefinierat innehåll.
    // ClearRenderTargetView är en kontextoperation och ordnad mot allt annat,
    // så den går lika bra mitt i en frame. Bilden rensas alltid (den kan
    // ritas innan målet någonsin bundits), den provtagna ytan och djupet
    // också — pushTarget rensar dem igen.
    const float noll[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context_->ClearRenderTargetView(imageRtv.Get(), noll);
    if (msaa)
        context_->ClearRenderTargetView(rtv.Get(), noll);
    if (dsv)
        context_->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    ++stats_.targetsCreated;
    return std::make_unique<D3dRenderTarget>(std::move(tex), std::move(srv),
                                             std::move(msaa), std::move(rtv),
                                             std::move(dsv), w, h, samples,
                                             d.mipmaps);
}

void D3dRenderer::bindTarget(RenderTarget* t)
{
    if (!context_)
        return;
    // Det som batchats hör till målet det ritades för.
    flush();
    // MSAA: målet som LÄMNAS löses upp till sin bild — i popTarget och när ett
    // nästlat mål binds ovanpå det, så bilden gäller direkt i båda fallen.
    // Upplösningen skrivs i målets eget format (ett importerat mål kan vara
    // RGBA).
    if (bound_ && bound_->msaaTexture())
        context_->ResolveSubresource(bound_->imageTexture(), 0,
                                     bound_->msaaTexture(), 0,
                                     bound_->resolveFormat());
    // MIP-KEDJAN räknas om när målet lämnas — efter upplösningen,
    // och först när det nya målet bundits: nivå 0 får inte sitta som RTV
    // medan GenerateMips läser den.
    const D3dImage* leaving =
        bound_ && bound_->imageView().mipmapped() ? &bound_->imageView()
                                                  : nullptr;
    if (t) {
        auto& rt   = static_cast<D3dRenderTarget&>(*t);
        bound_     = &rt;
        activeRtv_ = rt.rtv();
        activeDsv_ = rt.dsv();
        targetW_   = rt.image().width();
        targetH_   = rt.image().height();
    } else {
        // Framens eget mål — utanför en frame (beginOffscreen) inget alls.
        bound_     = nullptr;
        activeDsv_ = nullptr;
        activeRtv_ = offscreen_ ? nullptr
                                : (frameRtv_ ? frameRtv_ : rtv_.Get());
        targetW_   = width_;
        targetH_   = height_;
    }
    // Ett mål vars textur sitter kvar som SRV på t0 (från en ritning av
    // dess bild) får inte bindas som RTV samtidigt — läs/skriv-hazarden som
    // beginFrame redan nollar för retained-RT:n. flush() binder rätt SRV
    // inför varje Draw.
    ID3D11ShaderResourceView* nullSrv = nullptr;
    context_->PSSetShaderResources(0, 1, &nullSrv);
    // Ett eget programs texturer sitter kvar på t0… i
    // BÅDA stegen efter anropet — samma hazard, samma nollning.
    if (customSrvs_ > 0) {
        ID3D11ShaderResourceView* none[kMaxProgramTextures] = {};
        context_->VSSetShaderResources(0, customSrvs_, none);
        context_->PSSetShaderResources(0, customSrvs_, none);
        customSrvs_ = 0;
    }
    context_->OMSetRenderTargets(activeRtv_ ? 1 : 0,
                                 activeRtv_ ? &activeRtv_ : nullptr,
                                 activeDsv_);
    setViewport(targetW_, targetH_);
    if (leaving)
        context_->GenerateMips(leaving->srv());
}

// ---------------------------------------------------------- externa mål

std::unique_ptr<RenderTarget> D3dRenderer::importRenderTarget(
    const ExternalTargetDesc& d, RenderTarget* sharedAttachments,
    std::string& error)
{
    const auto fail = [&error](const std::string& why)
        -> std::unique_ptr<RenderTarget> {
        error = why;
        return std::make_unique<D3dRenderTarget>();
    };
    if (!device_ || !context_)
        return fail("ingen grafikenhet att importera till");
    if (d.api != NativeGraphicsApi::D3D11)
        return fail("bildens grafik-API är inte D3D11");
    if (!d.image)
        return fail("bilden saknas");
    const int  w       = d.width;
    const int  h       = d.height;
    const int  samples = d.target.samples;
    const bool samplesValid =
        samples == 1 || samples == 2 || samples == 4 || samples == 8;
    if (w <= 0 || h <= 0)
        return fail("måtten " + sizeText(w, h) + " är inte positiva");
    if (!samplesValid)
        return fail("provtagningen " + std::to_string(samples) +
                    " är inte 1, 2, 4 eller 8");
    if (d.target.mipmaps)
        return fail("mipmappar stöds inte i en extern bild");

    ID3D11Texture2D* tex = static_cast<ID3D11Texture2D*>(d.image);

    // SAMMA ENHET: en textur från en annan enhet kan inte bindas här.
    if (!onDevice(tex, device_.Get()))
        return fail("bilden hör till en annan grafikenhet än renderarens");

    D3D11_TEXTURE2D_DESC td{};
    tex->GetDesc(&td);
    if (static_cast<int>(td.Width) != w || static_cast<int>(td.Height) != h)
        return fail("bilden är " +
                    sizeText(static_cast<int>(td.Width),
                             static_cast<int>(td.Height)) +
                    " px men målet begärdes som " + sizeText(w, h));
    if (td.SampleDesc.Count != 1)
        return fail("bilden har flera prov per pixel; den ska ha ett");
    if (td.MipLevels != 1 || td.ArraySize != 1)
        return fail("bilden ska ha en nivå och ingen array");
    TargetColorFormat family{};
    if (!familyOf(td.Format, family) || family != d.color)
        return fail(std::string("bildens format hör inte till ") +
                    (d.color == TargetColorFormat::Rgba8Unorm ? "RGBA8-"
                                                              : "BGRA8-") +
                    "familjen som begärdes");
    if (!(td.BindFlags & D3D11_BIND_RENDER_TARGET))
        return fail("bilden saknar D3D11_BIND_RENDER_TARGET");
    if (!(td.BindFlags & D3D11_BIND_SHADER_RESOURCE))
        return fail("bilden saknar D3D11_BIND_SHADER_RESOURCE");

    // UNORM-formatet i familjen: vyerna, den provtagna ytan och upplösningen.
    const DXGI_FORMAT view = d.color == TargetColorFormat::Rgba8Unorm
                                 ? DXGI_FORMAT_R8G8B8A8_UNORM
                                 : DXGI_FORMAT_B8G8R8A8_UNORM;
    if (!samplesSupported(*device_.Get(), view, d.target.depth, samples))
        return fail(std::to_string(samples) +
                    " prov stöds inte av adaptern för färgformatet" +
                    (d.target.depth ? " och D32F" : ""));

    // DELNINGEN: samma mått, samma TargetDesc, samma färgformat och ett
    // tidigare importerat, giltigt mål (en import ger exakt den begärda
    // provtagningen och djup bara när det begärts).
    D3dRenderTarget* peer = nullptr;
    if (sharedAttachments) {
        peer = dynamic_cast<D3dRenderTarget*>(sharedAttachments);
        if (!peer || !peer->imported() || !peer->image().valid())
            return fail("sharedAttachments är inget giltigt importerat mål");
        // Hjälpytorna delas som COM-referenser och måste höra till samma
        // enhet som den nya bilden; ett mål importerat av en annan renderare
        // har sina på en annan.
        if (!onDevice(peer->rtv(), device_.Get()) ||
            (peer->dsv() && !onDevice(peer->dsv(), device_.Get())))
            return fail("sharedAttachments hör till en annan grafikenhet än "
                        "renderarens");
        if (peer->image().width() != w || peer->image().height() != h ||
            peer->samples() != samples || peer->hasDepth() != d.target.depth ||
            peer->resolveFormat() != view)
            return fail("sharedAttachments har andra mått, provtagning, djup "
                        "eller färgformat än det nya målet");
    }

    // BILDENS vyer: UNORM, aldrig sRGB.
    D3D11_RENDER_TARGET_VIEW_DESC rvd{};
    rvd.Format             = view;
    rvd.ViewDimension      = D3D11_RTV_DIMENSION_TEXTURE2D;
    rvd.Texture2D.MipSlice = 0;
    D3D11_SHADER_RESOURCE_VIEW_DESC svd{};
    svd.Format                    = view;
    svd.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    svd.Texture2D.MostDetailedMip = 0;
    svd.Texture2D.MipLevels       = 1;
    ComPtr<ID3D11RenderTargetView>   imageRtv;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device_->CreateRenderTargetView(tex, &rvd, &imageRtv)) ||
        FAILED(device_->CreateShaderResourceView(tex, &svd, &srv)))
        return fail("UNORM-vyerna på bilden kunde inte skapas");

    // DEN PROVTAGNA YTAN (bara med MSAA) — delad eller egen.
    ComPtr<ID3D11Texture2D>        msaa;
    ComPtr<ID3D11RenderTargetView> rtv = imageRtv;
    if (samples > 1) {
        if (peer) {
            msaa = peer->msaaTexture();
            rtv  = peer->rtv();
        } else {
            rtv.Reset();
            if (!createMsaaColor(*device_.Get(), w, h, view, samples, msaa,
                                 rtv))
                return fail("den provtagna färgytan på " + sizeText(w, h) +
                            " med " + std::to_string(samples) +
                            " prov kunde inte skapas");
        }
        if (!msaa || !rtv)
            return fail("sharedAttachments saknar den provtagna ytan");
    }

    // DJUPET: D32F med samma provtagning — delat eller eget.
    ComPtr<ID3D11DepthStencilView> dsv;
    if (d.target.depth) {
        if (peer)
            dsv = peer->dsv();
        else if (!createDepthView(*device_.Get(), w, h, samples, dsv))
            return fail("djupet på " + sizeText(w, h) +
                        " kunde inte skapas");
        if (!dsv)
            return fail("sharedAttachments saknar djupet");
    }

    // Ingen rensning: bilden är anroparens, och `pushTarget` rensar.
    ++stats_.targetsCreated;
    auto rt = std::make_unique<D3dRenderTarget>(
        ComPtr<ID3D11Texture2D>(tex), std::move(srv), std::move(msaa),
        std::move(rtv), std::move(dsv), w, h, samples, false, view);
    rt->markImported();
    error.clear();
    return rt;
}

std::unique_ptr<GpuCompletion> D3dRenderer::insertCompletion(std::string& error)
{
    if (!device_ || !context_) {
        error = "ingen grafikenhet";
        return nullptr;
    }
    ComPtr<ID3D11Query> q;
    const D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0};
    if (FAILED(device_->CreateQuery(&qd, &q))) {
        error = "händelsefrågan kunde inte skapas";
        return nullptr;
    }
    // Det som batchats hör till kommandona före markören.
    flush();
    context_->End(q.Get());
    context_->Flush();
    error.clear();
    return std::make_unique<D3dGpuCompletion>(device_, context_, std::move(q));
}

// Målets bild → CPU, rak alfa (renderer.h). Bilden är alltid ett prov per
// pixel — ett MSAA-mål löstes upp när det lämnades.
bool D3dRenderer::readTarget(RenderTarget& t, std::vector<std::uint8_t>& rgba)
{
    if (!device_ || !context_ || !readTargetOk(t))
        return false;
    auto& rt = static_cast<D3dRenderTarget&>(t);
    D3D11_TEXTURE2D_DESC td{};
    rt.imageTexture()->GetDesc(&td);
    td.Usage          = D3D11_USAGE_STAGING;
    td.BindFlags      = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags      = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &staging))) {
        log("readTarget: staging-texturen kunde inte skapas");
        return false;
    }
    context_->CopyResource(staging.Get(), rt.imageTexture());
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) {
        log("readTarget: Map(READ) misslyckades");
        return false;
    }
    const int tw = static_cast<int>(td.Width);
    const int th = static_cast<int>(td.Height);
    rgba.resize(static_cast<std::size_t>(tw) * th * 4);
    const auto* rows = static_cast<const std::uint8_t*>(m.pData);
    // B8G8R8A8 → RGBA8; ett importerat RGBA-mål har redan kanalordningen.
    const int r = rt.resolveFormat() == DXGI_FORMAT_B8G8R8A8_UNORM ? 2 : 0;
    for (int y = 0; y < th; ++y) {
        const std::uint8_t* px = rows + static_cast<std::size_t>(y) * m.RowPitch;
        std::uint8_t* out = rgba.data() + static_cast<std::size_t>(y) * tw * 4;
        for (int x = 0; x < tw; ++x) {
            out[x * 4]     = px[x * 4 + r];
            out[x * 4 + 1] = px[x * 4 + 1];
            out[x * 4 + 2] = px[x * 4 + 2 - r];
            out[x * 4 + 3] = px[x * 4 + 3];
        }
    }
    context_->Unmap(staging.Get(), 0);
    unpremultiplyRgba(rgba.data(), static_cast<std::size_t>(tw) * th);
    return true;
}

// Viewporten och UI:ts b0 (px → NDC i vertexshadern) — det BUNDNA målets
// storlek. Scenens program läser `targetW_/targetH_` i sina egna cbuffers.
void D3dRenderer::setViewport(int w, int h)
{
    targetW_ = w;
    targetH_ = h;
    const D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(w),
                            static_cast<float>(h), 0.0f, 1.0f};
    context_->RSSetViewports(1, &vp);
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(context_->Map(cbuf_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                                &m))) {
        float* f = static_cast<float*>(m.pData);
        f[0] = static_cast<float>(w);
        f[1] = static_cast<float>(h);
        f[2] = 0.0f;
        f[3] = 0.0f;
        context_->Unmap(cbuf_.Get(), 0);
    }
}

} // namespace gfx
