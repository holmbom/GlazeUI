// Bilder i D3D11-backenden: WIC (windowscodecs, OS-komponent) avkodar
// PNG → 32-bitars RGBA (`gfx::decodeImage`, trådsäker) → `createImage`
// → DEFAULT-textur + SRV. Ritas i shader-mode 5 (RGBA rakt av — samma väg
// som compositen av retained-RT:n) i den vanliga batch-pipelinen, så bilder
// samsas med all annan ritning utan egna Draw-anrop. Windows förblir
// beroendefritt. BARA PNG: signaturen prövas innan WIC
// får datat, och filvarianten läser filen och går samma väg — WIC hade annars
// tagit JPEG, BMP och GIF, vilket Linux aldrig gjort.

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

#include <wincodec.h>

#include "../image_util.h"
#include "d3d11_backend.h"
#include "gfx/platform.h"

namespace gfx {
namespace {

// COM behövs för WIC-fabriken — per anrop och TRÅD, så avkodningen går att
// köra i en arbetartråd. Trådens COM kan redan vara initierat med annan
// modell (RPC_E_CHANGED_MODE) — då kör vi vidare utan att balansera.
struct ComInit {
    ComInit() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComInit()
    {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
    HRESULT hr;
};

ComPtr<IWICImagingFactory> wicFactory()
{
    ComPtr<IWICImagingFactory> f;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&f));
    return f;
}

// Första framen → rak RGBA8, rad 0 överst, täta rader. false vid varje fel
// (utdata orörda). Konverterar oavsett källformat (palett, gråskala, ...).
bool decodeFrame(IWICBitmapDecoder* decoder, std::vector<std::uint8_t>& out,
                 int& outW, int& outH)
{
    ComPtr<IWICBitmapFrameDecode> frame;
    if (!decoder || FAILED(decoder->GetFrame(0, &frame)))
        return false;
    ComPtr<IWICBitmapSource> rgba;
    if (FAILED(WICConvertBitmapSource(GUID_WICPixelFormat32bppRGBA,
                                      frame.Get(), &rgba)))
        return false;
    UINT w = 0, h = 0;
    if (FAILED(rgba->GetSize(&w, &h)) || w == 0 || h == 0 ||
        w > INT_MAX / 4 || h > INT_MAX)
        return false;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4);
    if (FAILED(rgba->CopyPixels(nullptr, w * 4,
                                static_cast<UINT>(pixels.size()),
                                pixels.data())))
        return false;
    out  = std::move(pixels);
    outW = static_cast<int>(w);
    outH = static_cast<int>(h);
    return true;
}

} // namespace

// Fildata → pixlar, utan renderer (image.h: trådsäker). BARA PNG: signaturen
// prövas innan WIC får datat.
bool decodeImage(const void* data, std::size_t size,
                 std::vector<std::uint8_t>& rgba, int& w, int& h)
{
    if (!hasPngSignature(data, size)) {
        log("decodeImage: %zu byte är inte PNG — bara PNG avkodas (gfx/image.h)",
            size);
        return false;
    }
    ComInit com;
    auto    factory = wicFactory();
    ComPtr<IWICStream>        stream;
    ComPtr<IWICBitmapDecoder> dec;
    // WIC:s strömgränssnitt tar en icke-const pekare men läser bara.
    const bool ok =
        size <= ULONG_MAX && factory &&
        SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(
            const_cast<BYTE*>(static_cast<const BYTE*>(data)),
            static_cast<DWORD>(size))) &&
        SUCCEEDED(factory->CreateDecoderFromStream(
            stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec)) &&
        decodeFrame(dec.Get(), rgba, w, h);
    if (!ok)
        log("decodeImage: kunde inte avkoda %zu byte", size);
    return ok;
}

std::unique_ptr<Image> D3dRenderer::loadImage(const std::string& path)
{
    // Returnerar alltid ett objekt — valid() avgör (som loadFont). Filen
    // läses och går genom decodeImage: samma PNG-prov som minnesvarianten.
    std::ifstream f(std::filesystem::path(toWide(path)), std::ios::binary);
    if (!f) {
        log("loadImage: kunde inte läsa %s", path.c_str());
        return std::make_unique<D3dImage>();
    }
    const std::vector<std::uint8_t> data(
        (std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;
    if (!decodeImage(data.data(), data.size(), px, w, h)) {   // loggar själv
        log("loadImage: %s gav ingen bild", path.c_str());
        return std::make_unique<D3dImage>();
    }
    return createImage(w, h, ImageFormat::Rgba8, px.data(), 0);
}

std::unique_ptr<Image> D3dRenderer::loadImage(const void* data,
                                              std::size_t size)
{
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;
    if (!decodeImage(data, size, px, w, h))   // loggar själv
        return std::make_unique<D3dImage>();
    return createImage(w, h, ImageFormat::Rgba8, px.data(), 0);
}

// ------------------------------------------------------ bilder ur pixlar

std::unique_ptr<Image> D3dRenderer::createImage(int w, int h, ImageFormat f,
                                                const void* px,
                                                std::size_t stride,
                                                bool mipmaps)
{
    std::size_t rowBytes = 0;
    if (!device_ || !context_ || !imageDescOk(w, h, f, stride, rowBytes) ||
        rowBytes > UINT_MAX)
        return std::make_unique<D3dImage>();

    // Utan pixlar: NOLLOR — D3D11 lovar inget om en textur utan initdata.
    std::vector<std::uint8_t> zeros;
    const void*               src = px;
    if (!src) {
        zeros.assign(rowBytes * static_cast<std::size_t>(h), 0);
        src = zeros.data();
    }

    // DEFAULT-användning: GPU-residenta och uppdaterbara med
    // UpdateSubresource (updateImage) — samma val som scenens buffertar.
    // MIP-KEDJAN: MipLevels 0 är hela kedjan, och GenerateMips
    // kräver att texturen också går att binda som RTV.
    D3D11_TEXTURE2D_DESC td{};
    td.Width            = static_cast<UINT>(w);
    td.Height           = static_cast<UINT>(h);
    td.MipLevels        = mipmaps ? 0 : 1;
    td.ArraySize        = 1;
    td.Format           = f == ImageFormat::R8 ? DXGI_FORMAT_R8_UNORM
                                               : DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_DEFAULT;
    td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
    if (mipmaps) {
        td.BindFlags |= D3D11_BIND_RENDER_TARGET;
        td.MiscFlags  = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    }
    // Initdata måste täcka VARJE nivå — med kedjan skrivs nivå 0 efteråt och
    // resten räknas ur den.
    const D3D11_SUBRESOURCE_DATA init{src, static_cast<UINT>(rowBytes), 0};
    ComPtr<ID3D11Texture2D>          tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device_->CreateTexture2D(&td, mipmaps ? nullptr : &init,
                                        &tex)) ||
        FAILED(device_->CreateShaderResourceView(tex.Get(), nullptr, &srv))) {
        log("createImage: %d × %d kunde inte skapas", w, h);
        return std::make_unique<D3dImage>();
    }
    if (mipmaps) {
        context_->UpdateSubresource(tex.Get(), 0, nullptr, src,
                                    static_cast<UINT>(rowBytes), 0);
        context_->GenerateMips(srv.Get());
    }
    if (px)
        stats_.bytesUploaded += static_cast<std::uint64_t>(w) * h *
                                bytesPerPixel(f);
    return std::make_unique<D3dImage>(std::move(tex), std::move(srv), w, h, f,
                                      false, mipmaps);
}

int D3dRenderer::maxTextureSize() const
{
    // Funktionsnivåns tak (D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION från 11_0;
    // init tar 11_0, 10_1 eller 10_0).
    if (!device_)
        return D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    return device_->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_0
               ? D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION
               : D3D10_REQ_TEXTURE2D_U_OR_V_DIMENSION;
}

bool D3dRenderer::updateImage(Image& img, int x, int y, int w, int h,
                              const void* px, std::size_t stride)
{
    std::size_t rowBytes = 0;
    if (!context_ || !imageUpdateOk(img, x, y, w, h, px, stride, rowBytes) ||
        rowBytes > UINT_MAX)
        return false;
    // Bilden kommer alltid ur den här backenden — casten är säker per
    // konstruktion, som drawText:s Font.
    auto& im = static_cast<D3dImage&>(img);
    // Det som BATCHATS men inte ritats än läser texturen först vid flushen.
    // Flusha därför först: då ligger de anropen före UpdateSubresource i den
    // omedelbara kontexten, och det som redan ritats behåller det gamla
    // innehållet (renderer.h).
    flush();
    const D3D11_BOX box{static_cast<UINT>(x), static_cast<UINT>(y), 0,
                        static_cast<UINT>(x + w), static_cast<UINT>(y + h), 1};
    context_->UpdateSubresource(im.texture(), 0, &box, px,
                                static_cast<UINT>(rowBytes), 0);
    // Kedjan ur den nya nivå 0 — ordnad efter skrivningen i samma
    // kontext, och före varje ritning som batchas efter.
    if (im.mipmapped())
        context_->GenerateMips(im.srv());
    stats_.bytesUploaded += static_cast<std::uint64_t>(w) * h *
                            bytesPerPixel(img.format());
    return true;
}

// PNG-encode (glaze-render): encode-spegeln av loadImage — WIC-encodern
// (samma windowscodecs, redan länkat). PNG-encoderns nativa format är
// 32bppBGRA — swizzla RGBA→BGRA deterministiskt i stället för att lita
// på SetPixelFormat-förhandlingen.
bool encodePng(const std::string& path, const std::vector<std::uint8_t>& rgba,
               int w, int h)
{
    if (w <= 0 || h <= 0 ||
        rgba.size() < static_cast<std::size_t>(w) * h * 4) {
        log("encodePng: ogiltiga mått/otillräcklig pixeldata (%dx%d)", w, h);
        return false;
    }

    std::vector<std::uint8_t> bgra(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < bgra.size(); i += 4) {
        bgra[i]     = rgba[i + 2];   // RGBA8 → BGRA8
        bgra[i + 1] = rgba[i + 1];
        bgra[i + 2] = rgba[i];
        bgra[i + 3] = rgba[i + 3];
    }

    ComInit com;
    auto factory = wicFactory();
    if (!factory) {
        log("encodePng: WIC-fabriken kunde inte skapas");
        return false;
    }

    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    const bool ok =
        SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromFilename(toWide(path).c_str(),
                                                 GENERIC_WRITE)) &&
        SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                                         &enc)) &&
        SUCCEEDED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(enc->CreateNewFrame(&frame, &props)) &&
        SUCCEEDED(frame->Initialize(props.Get())) &&
        SUCCEEDED(frame->SetSize(static_cast<UINT>(w),
                                 static_cast<UINT>(h))) &&
        SUCCEEDED(frame->SetPixelFormat(&fmt)) &&
        IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA) &&
        SUCCEEDED(frame->WritePixels(static_cast<UINT>(h),
                                     static_cast<UINT>(w) * 4,
                                     static_cast<UINT>(bgra.size()),
                                     bgra.data())) &&
        SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());

    if (!ok)
        log("encodePng: kunde inte skriva %s", path.c_str());
    return ok;
}

void D3dRenderer::drawImage(Image& img, const Rectf& src, const Rectf& dst,
                            float opacity)
{
    // Bilden kommer alltid ur samma backend (loadImage ovan) — casten är
    // säker per konstruktion, som drawText:s Font.
    auto& im = static_cast<D3dImage&>(img);
    if (!im.valid() || im.width() <= 0 || im.height() <= 0 ||
        !imageDrawOk(img))
        return;

    // Bildens EGET filter — batch-tillstånd, så ett byte mellan två
    // ritningar av samma bild gäller den andra.
    setState(im.srv(), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, im.filter());

    // Käll-rect → normaliserad UV, batchad i samma pipeline som all annan
    // ritning. En bild med raderna nerifrån (`flippedV`) speglas här.
    const float iw = static_cast<float>(im.width());
    const float ih = static_cast<float>(im.height());
    const float u1 = src.x / iw, u2 = (src.x + src.w) / iw;
    float       v1 = src.y / ih, v2 = (src.y + src.h) / ih;
    if (im.flippedV()) {
        v1 = 1.0f - v1;
        v2 = 1.0f - v2;
    }

    // OPACITETEN bärs i vertexfärgens ALFA (R8G8B8A8, alfa i översta
    // byten) och multipliceras in i bildens rak-alfa i shaderns mode 5–7 —
    // färgkanalerna rörs inte. Vitt med full alfa = bilden som den är.
    // Mode 5 = RGBA rakt av (som compositen), 6 = ett render-måls
    // premultiplicerade innehåll, 7 = R8 som gråskala.
    const float a = std::clamp(opacity, 0.0f, 1.0f);
    Vertex v{};
    v.rgba = 0x00ffffffu |
             (static_cast<std::uint32_t>(std::lround(a * 255.0f)) << 24);
    v.mode = im.format() == ImageFormat::R8 ? 7.0f
             : im.premultiplied()           ? 6.0f
                                            : 5.0f;
    const float x1 = dst.x, y1 = dst.y;
    const float x2 = dst.x + dst.w, y2 = dst.y + dst.h;
    const float xs[6] = {x1, x2, x1, x2, x2, x1};
    const float ys[6] = {y1, y1, y2, y1, y2, y2};
    const float us[6] = {u1, u2, u1, u2, u2, u1};
    const float vs[6] = {v1, v1, v2, v1, v2, v2};
    for (int i = 0; i < 6; ++i) {
        v.x = xs[i];
        v.y = ys[i];
        v.u = us[i];
        v.v = vs[i];
        batch_.push_back(v);
    }
}

// ======================================================== Image-programmet
//
// Scenens bilder: världsquads ur `ImageVertex` med
// kamerans matris. Programmet bor bredvid bilderna och inte i
// d3d11_scene.cpp — det samplar en bild, resten av scenen gör det inte —
// men delar scenens cbuffer b2 (i sin egen form, `ImageCb`) och
// klippmaskerna i b1. Varje hörn bär en projektiv vikt `q`:
// programmet
// interpolerar (u·q, v·q, q) och delar i fragmentet, så en bild på fyra fria
// hörn blir projektiv (en rektangel har q = 1 och samplas affint). Den raka
// färgen FÄRGJUSTERAS före opaciteten — förspelets
// `glaze_colorAdjust` (`kHlslAdjustSrc`), samma funktion som ritytans
// Mesh-program kör.

namespace {

// Klippmaskernas del (`kHlslClipSrc`) och färgjusteringen (`kHlslAdjustSrc`)
// klistras FÖRE.
const char kImageShaderSrc[] = R"hlsl(
// SCENEN per rit-anrop — b2, samma buffert som Line-programmets i
// Image-programmets egen form (`ImageCb`).
cbuffer Scene : register(b2)
{
    float4x4 gViewProj;   // chunk-lokalt → pixelklipp (kolumnordning, mul(M, v))
    float4   gParams;     // .xy målets storlek (px), .z opaciteten,
                          // .w flaggorna: 1 premultiplicerad, 2 speglad v, 4 R8
    float4   gAdjust;     // färgjusteringen: mättnad, kontrast, ljushet, 0
}

Texture2D    tex0 : register(t0);
SamplerState smp0 : register(s0);

struct ImageIn
{
    float2 pos : POSITION;
    float2 uv  : TEXCOORD0;
    float  q   : TEXCOORD1;   // hörnets projektiva vikt (1 för en rektangel)
};

struct ImageOut
{
    float4 pos : SV_Position;
    // (u·q, v·q, q) PERSPEKTIVKORREKT (förvalet): w går orört vidare, så en
    // platta under tilt samplas där världspunkten faktiskt ligger, och
    // divisionen med q i fragmentet gör en fyrhörning exakt projektiv.
    float3 uvq : TEXCOORD0;
};

int flags() { return (int)(gParams.w + 0.5); }

ImageOut imageVs(ImageIn i)
{
    ImageOut o;
    const float4 c = mul(gViewProj, float4(i.pos, 0.0, 1.0));
    // Pixelklipp → D3D:s klipprymd. Linjär i (x, y, w) — ingen division
    // här; den gör rasteriseringen, och det är den som ger rätt w.
    o.pos = float4(c.x * 2.0 / gParams.x - c.w, c.w - c.y * 2.0 / gParams.y,
                   c.z, c.w);
    // Speglingen FÖRE multiplikationen med q — v' är bildens koordinat.
    const float v = (flags() & 2) ? 1.0 - i.uv.y : i.uv.y;
    o.uvq = float3(i.uv.x * i.q, v * i.q, i.q);
    return o;
}

float4 imagePs(ImageOut i) : SV_Target
{
    float4 t = tex0.Sample(smp0, i.uvq.xy / i.uvq.z);
    if (flags() & 4)
        t = float4(t.rrr, 1.0);               // R8: gråskala, helt täckande
    if (flags() & 1)
        t.rgb = t.a > 0.0 ? t.rgb / t.a       // render-mål: premultiplicerat
                          : float3(0.0, 0.0, 0.0);
    // Färgjusteringen på den RAKA färgen — förvalen lämnar den orörd.
    t.rgb = glaze_colorAdjust(t.rgb, gAdjust.xyz);
    // Rak alfa som UI:t; opaciteten, klippmaskerna och målarens klipp rör
    // bara alfan.
    t.a *= saturate(gParams.z) * clipCoverage(i.pos.xy) * sceneClip(i.pos.xy);
    return t;
}
)hlsl";

} // namespace

bool D3dRenderer::initImageProgram()
{
    const std::string src =
        std::string(kHlslClipSrc) + kHlslAdjustSrc + kImageShaderSrc;
    ComPtr<ID3DBlob> vsb = compileHlsl(src, "imageVs", "vs_4_0");
    ComPtr<ID3DBlob> psb = compileHlsl(src, "imagePs", "ps_4_0");

    // Layouten läser ImageVertex på dess fasta offset (gfx/scene.h).
    const D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         offsetof(ImageVertex, x), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         offsetof(ImageVertex, u), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0,
         offsetof(ImageVertex, q), D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    const bool ok =
        vsb && psb &&
        SUCCEEDED(device_->CreateVertexShader(vsb->GetBufferPointer(),
                                              vsb->GetBufferSize(), nullptr,
                                              &imageVs_)) &&
        SUCCEEDED(device_->CreatePixelShader(psb->GetBufferPointer(),
                                             psb->GetBufferSize(), nullptr,
                                             &imagePs_)) &&
        SUCCEEDED(device_->CreateInputLayout(il, 3, vsb->GetBufferPointer(),
                                             vsb->GetBufferSize(),
                                             &imageLayout_));
    if (!ok) {
        log("D3D11: Image-programmet kunde inte skapas — drawMesh ritar inga "
            "bilder");
        shutdownImageProgram();
    }
    return ok;
}

void D3dRenderer::shutdownImageProgram()
{
    imageLayout_.Reset();
    imagePs_.Reset();
    imageVs_.Reset();
}

void D3dRenderer::drawImageMesh(const MeshDraw& d)
{
    if (!context_ || !meshDrawOk(d, imageVs_ != nullptr) || !sceneCbuf_ ||
        !imageDrawOk(*d.image))
        return;
    // Bilden kommer alltid ur den här backenden — casten är säker per
    // konstruktion, som drawText:s Font.
    const auto& im = static_cast<const D3dImage&>(*d.image);

    flush();   // det UI:t batchat före anropet ritas först — målarordningen
    if (pipeline_ != Pipeline::Image) {
        context_->IASetInputLayout(imageLayout_.Get());
        context_->VSSetShader(imageVs_.Get(), nullptr, 0);
        context_->PSSetShader(imagePs_.Get(), nullptr, 0);
        context_->VSSetConstantBuffers(2, 1, sceneCbuf_.GetAddressOf());
        context_->PSSetConstantBuffers(2, 1, sceneCbuf_.GetAddressOf());
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pipeline_ = Pipeline::Image;
        ++stats_.programSwitches;
    }

    // Kameran, opaciteten, bildens flaggor och färgjusteringen för just det
    // här anropet — räknas inte som uppladdning (RenderStats). Programmets
    // egen, kortare form av b2 (`ImageCb`).
    ImageCb cb{};
    std::memcpy(cb.viewProj, d.uniforms.viewProj, sizeof cb.viewProj);
    cb.params[0] = static_cast<float>(targetW_);   // det BUNDNA målet —
    cb.params[1] = static_cast<float>(targetH_);   // ett render-mål
    cb.params[2] = std::clamp(d.uniforms.opacity, 0.0f, 1.0f);
    cb.params[3] = sceneImageFlags(im);   // samma tal i HLSL, GLSL och MSL
    sceneImageAdjust(d.uniforms, cb.adjust);   // likaså
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(context_->Map(sceneCbuf_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                             &m)))
        return;
    std::memcpy(m.pData, &cb, sizeof cb);
    context_->Unmap(sceneCbuf_.Get(), 0);
    if (!bindSceneClip(d))
        return;

    // Bilden och DESS filter — UI-batchen binder sina egna vid nästa Draw.
    ID3D11ShaderResourceView* srv = im.srv();
    ID3D11SamplerState*       smp = samplerFor(im.filter());
    context_->PSSetShaderResources(0, 1, &srv);
    context_->PSSetSamplers(0, 1, &smp);
    applyMeshState(d.state);   // renderläget

    ID3D11Buffer* vb =
        static_cast<const D3dGpuBuffer*>(d.vertices.buffer)->buffer();
    ID3D11Buffer* ib =
        static_cast<const D3dGpuBuffer*>(d.indices.buffer)->buffer();
    const UINT stride = sizeof(ImageVertex);
    const UINT offset = static_cast<UINT>(d.vertices.offset);
    context_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    context_->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT,
                               static_cast<UINT>(d.indices.offset));
    context_->DrawIndexed(d.indexCount, 0, 0);
    ++stats_.drawCalls;
    stats_.vertices += d.indexCount;
}

} // namespace gfx
