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

#include "../image_util.h"
#include "d3d11_backend.h"
#include "gfx/platform.h"

namespace gfx {

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
        D3D11_TEXTURE2D_DESC md = td;
        md.MipLevels          = 1;   // kedjan är bildens, inte provens
        md.MiscFlags          = 0;
        md.SampleDesc.Count   = static_cast<UINT>(samples);
        md.SampleDesc.Quality = 0;
        md.BindFlags          = D3D11_BIND_RENDER_TARGET;
        rtv.Reset();
        if (FAILED(device_->CreateTexture2D(&md, nullptr, &msaa)) ||
            FAILED(device_->CreateRenderTargetView(msaa.Get(), nullptr,
                                                   &rtv))) {
            log("createRenderTarget: %d × %d med %d prov kunde inte skapas", w,
                h, samples);
            return std::make_unique<D3dRenderTarget>();
        }
    }

    // DJUPET: D32F med samma provtagning.
    ComPtr<ID3D11DepthStencilView> dsv;
    if (d.depth) {
        D3D11_TEXTURE2D_DESC dd = td;
        dd.MipLevels          = 1;
        dd.MiscFlags          = 0;
        dd.Format             = DXGI_FORMAT_D32_FLOAT;
        dd.SampleDesc.Count   = static_cast<UINT>(samples);
        dd.SampleDesc.Quality = 0;
        dd.BindFlags          = D3D11_BIND_DEPTH_STENCIL;
        ComPtr<ID3D11Texture2D> depthTex;
        D3D11_DEPTH_STENCIL_VIEW_DESC dvd{};
        dvd.Format        = DXGI_FORMAT_D32_FLOAT;
        dvd.ViewDimension = samples > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS
                                        : D3D11_DSV_DIMENSION_TEXTURE2D;
        if (FAILED(device_->CreateTexture2D(&dd, nullptr, &depthTex)) ||
            FAILED(device_->CreateDepthStencilView(depthTex.Get(), &dvd,
                                                   &dsv))) {
            log("createRenderTarget: djupet på %d × %d kunde inte skapas", w,
                h);
            return std::make_unique<D3dRenderTarget>();
        }
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
    if (bound_ && bound_->msaaTexture())
        context_->ResolveSubresource(bound_->imageTexture(), 0,
                                     bound_->msaaTexture(), 0,
                                     DXGI_FORMAT_B8G8R8A8_UNORM);
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
    for (int y = 0; y < th; ++y) {
        const std::uint8_t* px = rows + static_cast<std::size_t>(y) * m.RowPitch;
        std::uint8_t* out = rgba.data() + static_cast<std::size_t>(y) * tw * 4;
        for (int x = 0; x < tw; ++x) {
            out[x * 4]     = px[x * 4 + 2];   // B8G8R8A8 → RGBA8
            out[x * 4 + 1] = px[x * 4 + 1];
            out[x * 4 + 2] = px[x * 4];
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
