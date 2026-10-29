// D3dRenderer — gfx::Renderer ovanpå D3D11. En enda pipeline (3D-first):
// allt är vertex med pos/uv/färg; VS:en gör ortho-projektionen (px → NDC),
// PS:en samplar en R8-textur som alfa-mask (1×1 vit för vanlig geometri,
// glyf-atlasen för text). Batchern samlar vertex i ritordning och flushar
// vid byte av textur/topologi — målar-ordningen bevaras exakt.
//
// HLSL kompileras i runtime via D3DCompile (d3dcompiler_47.dll är en
// OS-komponent i System32 från Windows 8.1) — inget byggsteg, ingen extern
// beroende-DLL.

#include <d3dcompiler.h>
#include <dcomp.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>

#include "d3d11_backend.h"
#include "gfx/platform.h"

namespace gfx {

namespace {

// Renderlägets tal i D3D11:s form.
D3D11_COMPARISON_FUNC comparisonOf(DepthTest t)
{
    switch (t) {
    case DepthTest::Less:         return D3D11_COMPARISON_LESS;
    case DepthTest::LessEqual:    return D3D11_COMPARISON_LESS_EQUAL;
    case DepthTest::Greater:      return D3D11_COMPARISON_GREATER;
    case DepthTest::GreaterEqual: return D3D11_COMPARISON_GREATER_EQUAL;
    case DepthTest::Equal:        return D3D11_COMPARISON_EQUAL;
    case DepthTest::Always:
    case DepthTest::Off:          break;
    }
    return D3D11_COMPARISON_ALWAYS;
}

// Blandningen (gfx/scene.h `Blend`) mot ett PREMULTIPLICERAT mål.
D3D11_BLEND_DESC blendDescOf(Blend b)
{
    D3D11_BLEND_DESC d{};
    D3D11_RENDER_TARGET_BLEND_DESC& rt = d.RenderTarget[0];
    rt.BlendOp               = D3D11_BLEND_OP_ADD;
    rt.BlendOpAlpha          = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    switch (b) {
    case Blend::Alpha:   // rak alfa (src-over) — UI:ts och 2D-scenens
        rt.BlendEnable    = TRUE;
        rt.SrcBlend       = D3D11_BLEND_SRC_ALPHA;
        rt.DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
        rt.SrcBlendAlpha  = D3D11_BLEND_ONE;
        rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        break;
    case Blend::Premultiplied:
        rt.BlendEnable    = TRUE;
        rt.SrcBlend       = D3D11_BLEND_ONE;
        rt.DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
        rt.SrcBlendAlpha  = D3D11_BLEND_ONE;
        rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        break;
    case Blend::Additive:
        rt.BlendEnable    = TRUE;
        rt.SrcBlend       = D3D11_BLEND_SRC_ALPHA;
        rt.DestBlend      = D3D11_BLEND_ONE;
        rt.SrcBlendAlpha  = D3D11_BLEND_ONE;
        rt.DestBlendAlpha = D3D11_BLEND_ONE;
        break;
    case Blend::Opaque:
        rt.BlendEnable = FALSE;
        break;
    case Blend::DepthOnly:
        rt.BlendEnable           = FALSE;
        rt.RenderTargetWriteMask = 0;   // färgen orörd — bara djupet
        break;
    }
    return d;
}

} // namespace

// ------------------------------------------------------- DirectComposition
//
// dcomp.dll är en OS-komponent men MinGW saknar importlibben — DCompositionCreateDevice hämtas därför i runtime via
// LoadLibrary. Headern (dcomp.h)
// finns i MinGW och ger interfacen.

namespace {

using PFN_DCompositionCreateDevice = HRESULT(WINAPI*)(IDXGIDevice*, REFIID,
                                                      void**);

PFN_DCompositionCreateDevice dcompCreateDevice()
{
    // Biblioteket hålls laddat processens livstid (som d3dcompiler).
    static PFN_DCompositionCreateDevice fn = [] {
        if (HMODULE m = LoadLibraryW(L"dcomp.dll"))
            return reinterpret_cast<PFN_DCompositionCreateDevice>(
                GetProcAddress(m, "DCompositionCreateDevice"));
        return PFN_DCompositionCreateDevice{nullptr};
    }();
    return fn;
}

} // namespace

// Kopplar composition-swapchainen till fönstret: device → target(hwnd) →
// visual(content = swapchain) → commit. Trädet är statiskt — Present
// uppdaterar innehållet utan nya commits.
bool D3dRenderer::initComposition(HWND hwnd)
{
    auto create = dcompCreateDevice();
    if (!create) {
        log("dcomp.dll saknas — ingen composition");
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDev;
    device_.As(&dxgiDev);

    ComPtr<IDCompositionDevice> dev;
    if (FAILED(create(dxgiDev.Get(), IID_PPV_ARGS(&dev))))
        return false;
    ComPtr<IDCompositionTarget> target;
    if (FAILED(dev->CreateTargetForHwnd(hwnd, TRUE, &target)))
        return false;
    ComPtr<IDCompositionVisual> visual;
    if (FAILED(dev->CreateVisual(&visual)))
        return false;
    if (FAILED(visual->SetContent(swapchain_.Get())) ||
        FAILED(target->SetRoot(visual.Get())) ||
        FAILED(dev->Commit()))
        return false;

    dcompDevice_ = dev;
    dcompTarget_ = target;
    dcompVisual_ = visual;
    return true;
}

// ---------------------------------------------------------------- shaders

// KLIPPMASKERNAS DEL — delad av UI-pipelinens och scenens pixelshaders
// (d3d11_scene.cpp). Varje program som ritar i ett mål med klippstacken
// MÅSTE ha den, annars gäller de rundade maskerna inte för det;
// därför står den på ETT ställe och klistras före båda källorna. Här
// bor också MÅLARENS KLIPP (`sceneClip`) — scenens fyra källor
// läser det, UI:t inte (kompilatorn stryker en oanvänd cbuffer).
const char kHlslClipSrc[] = R"hlsl(
// RUNDADE KLIPPMASKER — EGEN cbuffer bunden
// till PIXELSHADERN. b0 är VS:ens viewportbuffer och rörs INTE: att "utöka
// b0" hade betytt att packningen och PS-bindningen gömde sig i ett ord.
// Konventionen är EXAKT SDF-vertexens: p0 = centrum.xy + halvmått.zw,
// p1.x = radien. Arraylängden MÅSTE matcha gfx::kMaxRoundedClips.
cbuffer ClipMasks : register(b1)
{
    float4 gClipP0[4];
    float4 gClipP1[4];
    float4 gClipMeta;   // .x = antal aktiva masker
}

// Avstånd till en rundad rektangel: p relativt centrum, b = halvmått,
// r = hörnradie. Negativt inuti, 0 på kanten, positivt utanför.
float sdRoundRect(float2 p, float2 b, float r)
{
    float2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// Produkten av klippmaskernas täckningar: 0 så fort EN mask är 0, 1 bara
// där alla är 1. gClipMeta.x == 0 (det vanliga fallet) hoppar över loopen.
// p är pixelcentrum i målets px (SV_Position.xy).
float clipCoverage(float2 p)
{
    float cov = 1.0;
    const int n = (int)(gClipMeta.x + 0.5);
    for (int i = 0; i < n; ++i) {
        const float d = sdRoundRect(p - gClipP0[i].xy, gClipP0[i].zw,
                                    gClipP1[i].x);
        cov *= saturate(0.5 - d);
    }
    return cov;
}

// MÅLARENS KLIPP (gfx::SceneClip) — EGEN cbuffer b5, bunden till
// PIXELSHADERN och läst bara av scenens program (b3 och b4 är de egna
// programmens): en konvex yta i målets px som kantplan (nx, ny, d),
// positivt inne. Arraylängden MÅSTE matcha gfx::kMaxClipPlanes.
cbuffer SceneClip : register(b5)
{
    float4 gSceneClip[8];
    float4 gSceneClipMeta;   // .x = antal plan, 0 = inget klipp
}

// Täckningen i p (pixelcentrum i målets px): närmaste plans avstånd + ½,
// kantutjämnat som maskerna — och där den är 0 KASTAS fragmentet, så det
// varken färgar eller skriver djup. Utan plan 1. Bara i ett pixelsteg.
float sceneClip(float2 p)
{
    const int n = (int)(gSceneClipMeta.x + 0.5);
    if (n == 0)
        return 1.0;
    float d = 1.0e30;
    for (int i = 0; i < n; ++i)
        d = min(d, dot(gSceneClip[i].xy, p) + gSceneClip[i].z);
    const float cov = saturate(d + 0.5);
    if (cov <= 0.0)
        discard;
    return cov;
}
)hlsl";

static const char kUiShaderSrc[] = R"hlsl(
cbuffer Viewport : register(b0) { float2 vp; float2 pad; }

struct VSIn  { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0;
               float4 p0 : TEXCOORD1; float4 p1 : TEXCOORD2; float mode : TEXCOORD3; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0;
               float4 p0 : TEXCOORD1; float4 p1 : TEXCOORD2; float mode : TEXCOORD3; };

VSOut vsMain(VSIn i)
{
    VSOut o;
    // Pixelkoordinater (origo uppe till vänster) -> NDC
    o.pos  = float4(i.pos.x * 2.0 / vp.x - 1.0,
                    1.0 - i.pos.y * 2.0 / vp.y, 0.0, 1.0);
    o.uv   = i.uv;
    o.col  = i.col;
    o.p0   = i.p0;
    o.p1   = i.p1;
    o.mode = i.mode;
    return o;
}

Texture2D    tex0 : register(t0);
SamplerState smp0 : register(s0);

float4 psMain(VSOut i) : SV_Target
{
    // mode är konstant över quaden — grenen är uniform per primitiv
    const int mode = (int)(i.mode + 0.5);
    // SV_Position.xy är pixelcentrum i px — samma rymd som maskernas p0.
    // (GL behöver en egen varying här; D3D är redan top-left.)
    const float clipCov = clipCoverage(i.pos.xy);
    if (mode >= 5) {
        // Composite av retained-RT:n: färdig RGBA rakt av. Egen väg
        // eftersom mode 0 tolkar texturen som R8-alfamask (text/geometri).
        //
        // ALFA-KONTRAKTET: en mode 5-BILD i ritvägen (d3d11_image.cpp) bär
        // RAK alfa, så barnklippet får bara röra alfakanalen — att
        // multiplicera hela RGBA hade mörknat AA-bandet dubbelt. Compositens
        // quad har count 0 per konstruktion (klippstacken är balanserad och
        // tom där), så raden är en no-op just för den. Vertexfärgens ALFA är
        // bildens OPACITET (Renderer::drawImage) och rör också bara alfan;
        // compositens quad bär full alfa, så den faktorn är 1 där.
        float4 t = tex0.Sample(smp0, i.uv);
        if (mode == 6) {
            // Ett render-måls bild bär PREMULTIPLICERAD färg (gfx/image.h):
            // filtrerad premultiplicerad och därefter delad är rätt ordning —
            // utan delningen får kanterna en mörk frans över ljus yta.
            t.rgb = t.a > 0.0 ? t.rgb / t.a : float3(0.0, 0.0, 0.0);
        } else if (mode == 7) {
            // En R8-bild är gråskala, helt täckande (gfx/image.h).
            t = float4(t.rrr, 1.0);
        }
        t.a *= i.col.a * clipCov;
        return t;
    }
    float alpha;
    if (mode == 0) {
        // R8-texturen är ren täckningsgrad — färgen kommer ur vertexfärgen
        alpha = tex0.Sample(smp0, i.uv).r;
    } else {
        // SV_Position.xy = pixelcentrum i px — samma koordinatsystem som p0
        const float d = sdRoundRect(i.pos.xy - i.p0.xy, i.p0.zw, i.p1.x);
        if (mode == 1) {
            // Fylld: 1 px analytisk AA-kant runt d = 0
            alpha = saturate(0.5 - d);
        } else if (mode == 2) {
            // Kontur med bredden p1.y innanför kanten (bandet d i [-w, 0])
            alpha = saturate(0.5 - d) * saturate(d + i.p1.y + 0.5);
        } else if (mode == 3) {
            // Drop shadow: heltäckande innanför -blur, tonar ut till +blur
            const float blur = max(i.p1.y, 0.5);
            alpha = 1.0 - smoothstep(-blur, blur, d);
        } else {
            // Inner shadow: starkast vid (offset-förskjutna) kanten, tonar
            // inåt över blur — klipps mot själva rektangeln via d
            const float blur = max(i.p1.y, 0.5);
            const float ds = sdRoundRect(i.pos.xy - i.p0.xy - i.p1.zw,
                                         i.p0.zw, i.p1.x);
            alpha = smoothstep(-blur, 0.0, ds) * saturate(0.5 - d);
        }
    }
    // mode 0–4 är alla rak alfa — klippet skalar täckningen, inte färgen.
    return float4(i.col.rgb, i.col.a * alpha * clipCov);
}
)hlsl";

ComPtr<ID3DBlob> D3dRenderer::compileHlsl(const std::string& src,
                                          const char* entry,
                                          const char* target,
                                          const D3D_SHADER_MACRO* defines,
                                          std::string* error)
{
    // BYTEKODEN ÄR ENHETSOBEROENDE: varje fönster har en egen enhet, och utan
    // cachen kompileras ALLA program på nytt (UI:t, Line/Line3, Fill, Glyph/
    // Glyph3, Image — ~12 D3DCompile) — en meny eller en dropdown tar
    // 0,5–1 s att öppna. Därför kompileras varje (källa, ingång, mål,
    // definitioner) en gång per process; nästa fönster skapar bara sina
    // shaderobjekt ur bloben. Bara lyckade kompileringar sparas. UI-tråden
    // ensam anropar — ingen låsning.
    static std::unordered_map<std::string, ComPtr<ID3DBlob>> cache;
    std::string nyckel = std::string(entry) + '\x1f' + target + '\x1f';
    for (const D3D_SHADER_MACRO* m = defines; m && m->Name; ++m)
        nyckel += std::string(m->Name) + '=' + (m->Definition ? m->Definition : "") + '\x1f';
    nyckel += src;
    if (const auto it = cache.find(nyckel); it != cache.end())
        return it->second;

    ComPtr<ID3DBlob> blob, err;
    HRESULT          hr = E_FAIL;
    {
        const StatTimer klocka(stats_.compileNs);   // bara missen kostar
        hr = D3DCompile(src.data(), src.size(), nullptr, defines, nullptr,
                        entry, target, 0, 0, &blob, &err);
    }
    if (FAILED(hr)) {
        // Kompilatorns text — nullterminerad, men storleken räknar ofta in
        // nollan; den och radslutet kapas.
        std::string text =
            err ? std::string(static_cast<const char*>(err->GetBufferPointer()),
                              err->GetBufferSize())
                : std::string("?");
        while (!text.empty() &&
               (text.back() == '\0' || text.back() == '\n' ||
                text.back() == '\r'))
            text.pop_back();
        if (error)
            *error = text;
        else
            log("HLSL-kompilering (%s) misslyckades: %s", entry, text.c_str());
        return nullptr;
    }
    cache.emplace(std::move(nyckel), blob);
    return blob;
}

// ---------------------------------------------------------------- init

bool D3dRenderer::init(HWND hwnd, int w, int h, bool transparent)
{
    transparent_ = transparent;
    // Device + immediate context (hårdvara, WARP-fallback för t.ex. VM:ar).
    // GPU-PREFERENSEN väljer adaptern först; går
    // enheten inte att skapa på den gäller förvalet, och det loggas.
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0,
                                        D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    // KORTET vinner över preferensen; korten räknas upp
    // EN gång här, utan prob, och samma lista ger id:t och loggen nedan.
    HRESULT                  hr = E_FAIL;
    ComPtr<IDXGIFactory1>    fabrik;
    std::vector<DxgiAdapter> kort;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&fabrik))))
        kort = enumerateAdapters(*fabrik.Get(), false);
    if (ComPtr<IDXGIAdapter1> vald =
            fabrik ? chosenAdapter(*fabrik.Get(), kort) : nullptr) {
        hr = D3D11CreateDevice(vald.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                               0, levels, 3, D3D11_SDK_VERSION, &device_,
                               nullptr, &context_);
        if (FAILED(hr))
            log("D3D11: enheten gick inte att skapa på det valda kortet eller "
                "GPU-preferensens adapter (0x%08lx) — DXGI:s första adapter "
                "gäller",
                hr);
    }
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                               0, levels, 3, D3D11_SDK_VERSION,
                               &device_, nullptr, &context_);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                               0, levels, 3, D3D11_SDK_VERSION,
                               &device_, nullptr, &context_);
    if (FAILED(hr)) {
        log("D3D11CreateDevice misslyckades: 0x%08lx", hr);
        return false;
    }

    // Swapchain mot fönstret — flip-model, vsync via Present(1, 0)
    ComPtr<IDXGIDevice>   dxgiDev;
    ComPtr<IDXGIAdapter>  adapter;
    ComPtr<IDXGIFactory2> factory;
    device_.As(&dxgiDev);
    dxgiDev->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&factory));
    // Enhetens namn och kortets id (`Renderer::deviceName`/`adapterId`) —
    // adaptern den FAKTISKT fick, också efter WARP-reserven (LUID:en mot
    // listan), och i loggen när den är en annan än förra enhetens.
    DXGI_ADAPTER_DESC adapterDesc{};
    if (SUCCEEDED(adapter->GetDesc(&adapterDesc))) {
        deviceName_ = toUtf8(adapterDesc.Description);
        adapterId_  = adapterIdOf(kort, adapterDesc.AdapterLuid);
        logAdapter(kort, adapterDesc, adapterId_);
    }

    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width            = static_cast<UINT>(w);
    sd.Height           = static_cast<UINT>(h);
    sd.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount      = 2;
    sd.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    if (transparent_) {
        // EGEN väg för genomskinliga fönster: composition-swapchain med
        // premultiplicerad alfa, kopplad till fönstret via DirectComposition
        // (WS_EX_NOREDIRECTIONBITMAP sätts av fönstret). Opaka fönster tar
        // ForHwnd-vägen nedan.
        sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        sd.Scaling   = DXGI_SCALING_STRETCH;   // krav för composition
        hr = factory->CreateSwapChainForComposition(device_.Get(), &sd,
                                                    nullptr, &swapchain_);
        if (SUCCEEDED(hr) && !initComposition(hwnd)) {
            swapchain_.Reset();
            hr = E_FAIL;
        }
        if (FAILED(hr)) {
            // Degradera till opakt fönster — appen lever, alfan uteblir.
            log("Composition-swapchain misslyckades (0x%08lx) — "
                "degraderar till opakt", hr);
            transparent_ = false;
        }
    }

    if (!transparent_) {
        hr = factory->CreateSwapChainForHwnd(device_.Get(), hwnd, &sd,
                                             nullptr, nullptr, &swapchain_);
        if (FAILED(hr)) {
            log("CreateSwapChainForHwnd misslyckades: 0x%08lx", hr);
            return false;
        }
    }
    // Alt+Enter-fullskärm hanteras inte — vi äger chromet själva
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    if (!createBackbuffer(w, h))
        return false;

    // Shaders + input-layout (klippmaskernas del först — psMain läser den)
    const std::string uiSrc = std::string(kHlslClipSrc) + kUiShaderSrc;
    ComPtr<ID3DBlob> vsb = compileHlsl(uiSrc, "vsMain", "vs_4_0");
    ComPtr<ID3DBlob> psb = compileHlsl(uiSrc, "psMain", "ps_4_0");
    if (!vsb || !psb)
        return false;
    device_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(),
                                nullptr, &vs_);
    device_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(),
                               nullptr, &ps_);

    const D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 36,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT, 0, 52,
         D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    device_->CreateInputLayout(il, 6, vsb->GetBufferPointer(),
                               vsb->GetBufferSize(), &layout_);

    // Dynamisk vertexbuffert — mappas med DISCARD per flush
    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth      = kMaxVerts * sizeof(Vertex);
    bd.Usage          = D3D11_USAGE_DYNAMIC;
    bd.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&bd, nullptr, &vbuf_);

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth      = 16;   // float2 vp + padding
    cbd.Usage          = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&cbd, nullptr, &cbuf_);

    // Rundade klippmasker — egen PS-cbuffer b1,
    // 16-byte-packad: float4 p0[4] + float4 p1[4] + float4 meta.
    D3D11_BUFFER_DESC mbd{};
    mbd.ByteWidth      = sizeof(ClipMaskCb);
    mbd.Usage          = D3D11_USAGE_DYNAMIC;
    mbd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    mbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    device_->CreateBuffer(&mbd, nullptr, &clipCbuf_);

    // BLANDNINGEN — en tillståndsbit per `Blend`. UI:t
    // och 2D-scenen tar Alpha, rak alfa (src-over).
    for (int b = 0; b < 5; ++b) {
        const D3D11_BLEND_DESC bld = blendDescOf(static_cast<Blend>(b));
        device_->CreateBlendState(&bld, &blendStates_[b]);
    }

    // DJUPET — en tillståndsbit per (test, skrivning). Off är INGET djup
    // (DepthEnable FALSE, också skrivningen av); det är UI:ts, och det
    // binds uttryckligen: D3D11:s FÖRVALDA djupläge (nullptr) har djupet PÅ
    // så snart en DSV är bunden.
    for (int t = 0; t < 7; ++t)
        for (int wr = 0; wr < 2; ++wr) {
            D3D11_DEPTH_STENCIL_DESC dsd{};
            const auto test = static_cast<DepthTest>(t);
            dsd.DepthEnable    = test == DepthTest::Off ? FALSE : TRUE;
            dsd.DepthWriteMask = wr && test != DepthTest::Off
                                     ? D3D11_DEPTH_WRITE_MASK_ALL
                                     : D3D11_DEPTH_WRITE_MASK_ZERO;
            dsd.DepthFunc      = comparisonOf(test);
            dsd.StencilEnable  = FALSE;
            device_->CreateDepthStencilState(&dsd, &depthStates_[t][wr]);
        }

    // MSAA: vilka provtagningar går för BÅDE färgen och
    // djupet — frågat en gång, `supportedSamples` läser svaret.
    for (int i = 0; i < 3; ++i) {
        const UINT n = 2u << i;
        UINT qc = 0, qd = 0;
        msaaOk_[i] =
            SUCCEEDED(device_->CheckMultisampleQualityLevels(
                DXGI_FORMAT_B8G8R8A8_UNORM, n, &qc)) &&
            SUCCEEDED(device_->CheckMultisampleQualityLevels(
                DXGI_FORMAT_D32_FLOAT, n, &qd)) &&
            qc > 0 && qd > 0;
    }

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;   // 2D-quads — ingen backface-cull
    // Framsidan som i `rasterFor` (moturs sett från kameran) också här: utan
    // gallring blir pixlarna desamma, men SV_IsFrontFace följer flaggan — och
    // `rasterFor` ger ett scenanrop med Cull::None utan djup, MSAA och bias
    // DEN HÄR rasteriseraren (Mesh-programmets tvåsidiga ljus och platta
    // normal, mesh_program_gate E2/E3 utan djup).
    rd.FrontCounterClockwise = TRUE;
    device_->CreateRasterizerState(&rd, &raster_);
    // Samma rasterizer med scissor på — klipp-stacken växlar mellan dem
    rd.ScissorEnable = TRUE;
    device_->CreateRasterizerState(&rd, &rasterScissor_);

    // Två samplers, ett per filter. Nearest för glyfer, rektanglar och
    // compositen — de ritas 1:1 och är skarpast så — och Linear för bilder,
    // förvalet. Vilken som gäller är batch-tillstånd (setState/flush). Båda
    // når HELA mip-kedjan (MaxLOD — noll i en nollad desc hade låst nivå 0):
    // Nearest närmaste nivå, Linear mellan nivåerna; en textur utan
    // kedja har bara nivå 0.
    D3D11_SAMPLER_DESC smd{};
    smd.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
    smd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    smd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    smd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    smd.MaxLOD   = D3D11_FLOAT32_MAX;
    device_->CreateSamplerState(&smd, &samplerNearest_);
    smd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    device_->CreateSamplerState(&smd, &samplerLinear_);

    // 1×1 vit R8-textur — alfa-masken för ofärgad geometri
    const std::uint8_t white = 255;
    D3D11_TEXTURE2D_DESC td{};
    td.Width            = 1;
    td.Height           = 1;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_IMMUTABLE;
    td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA init{&white, 1, 0};
    device_->CreateTexture2D(&td, &init, &whiteTex_);
    device_->CreateShaderResourceView(whiteTex_.Get(), nullptr, &whiteSrv_);

    // Scenens program (d3d11_scene.cpp). Misslyckas det loggas det och
    // drawMesh ritar ingenting — UI:t påverkas inte. Image-programmet
    // (d3d11_image.cpp) delar scenens b2 och skapas efter det.
    initScene();
    initImageProgram();
    initFillProgram();   // ytorna (d3d11_fill.cpp) delar också b2
    initGlyphProgram();  // texten (d3d11_glyph.cpp) likaså
    initProgramSupport();   // egna program: GlazeFrame i b3 (d3d11_program.cpp)
    gpuTimingSupported_ = initGpuQueries();   // GPU-tiden

    batch_.reserve(kMaxVerts);
    return true;
}

void D3dRenderer::shutdownRenderer()
{
    for (GpuQuery& q : gpuQueries_)
        q = GpuQuery{};
    gpuQueryOpen_       = -1;
    gpuTimingSupported_ = false;
    shutdownProgramSupport();
    shutdownGlyphProgram();
    shutdownFillProgram();
    shutdownImageProgram();
    shutdownScene();   // scenens buffertar och program före device:n
    batch_.clear();
    curSrv_        = nullptr;
    activeRtv_     = nullptr;
    frameRtv_      = nullptr;
    retainedValid_ = false;
    dcompVisual_.Reset();
    dcompTarget_.Reset();
    dcompDevice_.Reset();
    alphaStaging_.Reset();
    alphaMirror_.clear();
    alphaW_ = alphaH_ = 0;
    transparent_ = false;
    offSrv_.Reset();
    offRtv_.Reset();
    offTex_.Reset();
    whiteSrv_.Reset();
    whiteTex_.Reset();
    samplerLinear_.Reset();
    samplerNearest_.Reset();
    rasterCache_.clear();
    for (auto& perTest : depthStates_)
        for (auto& s : perTest)
            s.Reset();
    for (auto& b : blendStates_)
        b.Reset();
    resetStateCache();
    bound_     = nullptr;
    activeDsv_ = nullptr;
    inFrame_   = false;
    offscreen_ = false;
    rasterScissor_.Reset();
    raster_.Reset();
    clipCbuf_.Reset();
    cbuf_.Reset();
    vbuf_.Reset();
    layout_.Reset();
    ps_.Reset();
    vs_.Reset();
    rtv_.Reset();
    swapchain_.Reset();
    if (context_)
        context_->ClearState();
    context_.Reset();
    device_.Reset();
}

bool D3dRenderer::createBackbuffer(int w, int h)
{
    ComPtr<ID3D11Texture2D> back;
    HRESULT hr = swapchain_->GetBuffer(0, IID_PPV_ARGS(&back));
    if (FAILED(hr) ||
        FAILED(device_->CreateRenderTargetView(back.Get(), nullptr, &rtv_))) {
        log("Backbuffer/RTV misslyckades: 0x%08lx", hr);
        return false;
    }
    width_  = w;
    height_ = h;
    // Retained-RT:n följer backbufferns storlek. Misslyckas den ritas
    // direkt i backbuffern i stället (beginFrame/composite degraderar) —
    // appen lever, partiell omritning uteblir bara.
    createOffscreen(w, h);
    return true;
}

bool D3dRenderer::createOffscreen(int w, int h)
{
    offSrv_.Reset();
    offRtv_.Reset();
    offTex_.Reset();
    retainedValid_ = false;   // ny yta = odefinierat innehåll tills composite

    D3D11_TEXTURE2D_DESC td{};
    td.Width            = static_cast<UINT>(w);
    td.Height           = static_cast<UINT>(h);
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;   // som swapchainen
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_DEFAULT;
    td.BindFlags        = D3D11_BIND_RENDER_TARGET |
                          D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &offTex_)) ||
        FAILED(device_->CreateRenderTargetView(offTex_.Get(), nullptr,
                                               &offRtv_)) ||
        FAILED(device_->CreateShaderResourceView(offTex_.Get(), nullptr,
                                                 &offSrv_))) {
        log("Retained-RT kunde inte skapas (%dx%d)", w, h);
        offSrv_.Reset();
        offRtv_.Reset();
        offTex_.Reset();
        return false;
    }
    return true;
}

void D3dRenderer::resize(int w, int h)
{
    if (!swapchain_ || w <= 0 || h <= 0)
        return;
    if (w == width_ && h == height_)
        return;
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    rtv_.Reset();
    // Misslyckad ResizeBuffers lämnar GAMLA storleken kvar — GetBuffer i
    // createBackbuffer "lyckas" då mot fel yta. Logga så resize-artefakter
    // kan skiljas från degradering.
    const HRESULT hr = swapchain_->ResizeBuffers(
        0, static_cast<UINT>(w), static_cast<UINT>(h),
        DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
        log("ResizeBuffers(%dx%d) misslyckades: 0x%08lx", w, h, hr);
    createBackbuffer(w, h);
}

// ---------------------------------------------------------------- frame

void D3dRenderer::beginFrame()
{
    if (!rtv_)
        return;
    ++frameSerial_;
    // GPU-tiden: de färdiga svaren först, sedan framens fråga.
    collectGpuQueries();
    startGpuQuery();

    // Retained-SRV:n från förra compositen får inte sitta kvar i PS-slotten
    // när dess textur binds som RT (läs/skriv-hazard) — nolla slotten.
    // flush() binder rätt SRV inför varje Draw ändå.
    ID3D11ShaderResourceView* nullSrv = nullptr;
    context_->PSSetShaderResources(0, 1, &nullSrv);

    // Frame-innehållet ritas till retained-RT:n (composite lyfter den till
    // backbuffern). Saknas den (skapandet misslyckades) → direkt till
    // backbuffern, som före retained-modellen.
    inFrame_   = true;
    activeRtv_ = offRtv_ ? offRtv_.Get() : rtv_.Get();
    frameRtv_  = activeRtv_;   // render-målen återvänder hit
    bound_     = nullptr;      // framens eget mål: inget djup, ingen MSAA
    activeDsv_ = nullptr;
    context_->OMSetRenderTargets(1, &activeRtv_, nullptr);

    // Viewporten och ortho-parametrarna (b0) — målets storlek.
    setViewport(width_, height_);
    // En obalanserad pushTarget i förra framen får inte peka hit.
    beginFrameTargets();

    bindPipelineState();

    // UI-pipelinen är bunden; scenens transienta ringar börjar om.
    beginSceneFrame();
}

// UI-pipelinens HELA bindning (beginFrame, beginOffscreen). Tillståndet först
// — djup av, UI:ts rasteriserare utan scissor, rak alfa — med cachen glömd,
// så alla tre binds oavsett vad kontexten bar (före första framen: ingenting).
void D3dRenderer::bindPipelineState()
{
    scissorOn_ = false;
    resetStateCache();
    applyUiState();

    context_->IASetInputLayout(layout_.Get());
    context_->VSSetShader(vs_.Get(), nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, cbuf_.GetAddressOf());
    context_->PSSetShader(ps_.Get(), nullptr, 0);
    // Samplern binds INTE här: den är batch-tillstånd och sätts av flush()
    // vid varje Draw.
    // Klippmaskerna till PIXELSHADERN (b1). Bindningen görs här, i samma
    // tillståndssättning som allt annat; innehållet uppdateras i applyClip.
    context_->PSSetConstantBuffers(1, 1, clipCbuf_.GetAddressOf());
    // Nollställ maskerna vid framens början. Stacken är balanserad per
    // konstruktion, men en obalans är ett LOGGAT programfel och får inte
    // dessutom läcka en mask in i nästa frame.
    clipMaskCount_ = -1;   // tvinga en uppladdning
    uploadClipMasks(nullptr, 0);
    // Målarens klipp till pixelshadern i b5 — innehållet följer
    // rit-anropen (`bindSceneClip`) och ligger kvar mellan framarna.
    context_->PSSetConstantBuffers(5, 1, sceneClipCbuf_.GetAddressOf());

    const UINT stride = sizeof(Vertex), offset = 0;
    context_->IASetVertexBuffers(0, 1, vbuf_.GetAddressOf(), &stride,
                                 &offset);
    pipeline_ = Pipeline::Ui;
}

void D3dRenderer::present()
{
    flush();
    // Framens GPU-tid tar slut här — efter dess sista rit-anrop.
    if (gpuQueryOpen_ >= 0) {
        const GpuQuery& q = gpuQueries_[gpuQueryOpen_];
        context_->End(q.end.Get());
        context_->End(q.disjoint.Get());
        gpuQueryOpen_ = -1;
    }
    // Vsync — utom under den modala resize-
    // loopen: där presenteras osynkat (SyncInterval 0) så framen med NY
    // storlek hinner ut innan DWM visar den gamla uppskalad.
    // Väntan i `Present` är presentationens (`RenderStats::presentNs`).
    if (swapchain_) {
        const StatTimer klocka(stats_.presentNs);
        swapchain_->Present(unsyncedPresent_ ? 0 : 1, 0);
    }
    if (transparent_)
        updateAlphaMirror();   // hit-testens CPU-spegel — BARA transparenta
    inFrame_ = false;
}

// ---------------------------------------------------------- GPU-tiden

bool D3dRenderer::initGpuQueries()
{
    const D3D11_QUERY_DESC disjoint{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
    const D3D11_QUERY_DESC stämpel{D3D11_QUERY_TIMESTAMP, 0};
    gpuQueryOpen_ = -1;
    for (GpuQuery& q : gpuQueries_) {
        q.serial = 0;
        if (FAILED(device_->CreateQuery(&disjoint, &q.disjoint)) ||
            FAILED(device_->CreateQuery(&stämpel, &q.begin)) ||
            FAILED(device_->CreateQuery(&stämpel, &q.end))) {
            log("D3D11: GPU-tidens frågor kunde inte skapas — GPU-tiden mäts "
                "inte");
            for (GpuQuery& r : gpuQueries_)
                r = GpuQuery{};
            return false;
        }
    }
    return true;
}

// De svar som är färdiga, utan att tömma kön — ett svar som inte är klart
// läses nästa frame. En frame med `Disjoint` (klockan hoppade, t.ex. ett
// energiläge) kastas.
void D3dRenderer::collectGpuQueries()
{
    for (int i = 0; i < kGpuQueries; ++i) {
        GpuQuery& q = gpuQueries_[i];
        if (q.serial == 0 || i == gpuQueryOpen_)
            continue;
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        if (context_->GetData(q.disjoint.Get(), &dj, sizeof dj,
                              D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
            continue;
        // DISJOINT kan vara klar före stämplarna (Intel: S_FALSE på båda i
        // samma anrop) — då väntar platsen till nästa frame i stället för att
        // kastas, annars mäts aldrig en frame där.
        UINT64        t0 = 0, t1 = 0;
        const HRESULT h0 = context_->GetData(q.begin.Get(), &t0, sizeof t0,
                                             D3D11_ASYNC_GETDATA_DONOTFLUSH);
        const HRESULT h1 = context_->GetData(q.end.Get(), &t1, sizeof t1,
                                             D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (h0 == S_FALSE || h1 == S_FALSE)
            continue;
        if (!dj.Disjoint && dj.Frequency > 0 && h0 == S_OK && h1 == S_OK &&
            t1 >= t0) {
            constexpr double kNsPerS = 1.0e9;
            addGpuTime(q.serial,
                       static_cast<std::uint64_t>(static_cast<double>(t1 - t0) *
                                                  kNsPerS /
                                                  static_cast<double>(dj.Frequency)));
        }
        q.serial = 0;
    }
}

// Framens fråga börjar — när tidtagningen är på och en plats är ledig (alla
// fyra väntar annars på svar, och framen mäts inte).
void D3dRenderer::startGpuQuery()
{
    gpuQueryOpen_ = -1;
    if (!gpuTiming_ || !gpuTimingSupported_)
        return;
    for (int i = 0; i < kGpuQueries; ++i) {
        GpuQuery& q = gpuQueries_[i];
        if (q.serial != 0)
            continue;
        context_->Begin(q.disjoint.Get());
        context_->End(q.begin.Get());
        q.serial      = frameSerial_;
        gpuQueryOpen_ = i;
        return;
    }
}

// ---------------------------------------------------- ritning utan frame
//
// Se renderer.h. D3D11:s omedelbara kontext behöver ingen
// egen inspelning, men UI-pipelinens TILLSTÅND binds av beginFrame — före
// första framen är ingenting bundet alls, och efter en frame kan ett
// scenprogram ligga kvar. Blocket binder därför samma tillstånd som en frame,
// men inget eget mål: utanför ett render-mål ritas ingenting (`bindTarget`).
bool D3dRenderer::beginOffscreen()
{
    if (!context_ || inFrame_ || offscreen_) {
        log("beginOffscreen: %s — blocket öppnas inte",
            !context_ ? "ingen enhet"
            : inFrame_ ? "inuti en frame"
                       : "ett block är redan öppet");
        return false;
    }
    offscreen_ = true;
    ++frameSerial_;         // blocket har ett eget löpnummer (renderer.h)
    bound_     = nullptr;
    activeDsv_ = nullptr;
    activeRtv_ = nullptr;   // inget eget mål — bara render-målen
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    beginFrameTargets();    // en obalanserad stack töms
    bindPipelineState();
    return true;
}

void D3dRenderer::endOffscreen()
{
    if (!offscreen_) {
        log("endOffscreen utan beginOffscreen — ignoreras");
        return;
    }
    flush();
    offscreen_ = false;
    bound_     = nullptr;
    activeDsv_ = nullptr;
    // Kontexten är omedelbar: readTarget:s Map väntar in arbetet. Framens
    // eget mål binds igen av nästa beginFrame.
}

// CPU-spegel av retained-RT:ns alfa (transparent-vägens hit-test): kopiera
// till staging + läs ut alfakanalen. Körs per present, endast transparenta
// fönster — kostnaden hör till transparensen, aldrig opak-vägen.
void D3dRenderer::updateAlphaMirror()
{
    if (!offTex_) {
        alphaW_ = alphaH_ = 0;   // degraderat läge → opak hit-test
        return;
    }
    if (!alphaStaging_ || alphaW_ != width_ || alphaH_ != height_) {
        alphaStaging_.Reset();
        D3D11_TEXTURE2D_DESC td{};
        offTex_->GetDesc(&td);
        td.Usage          = D3D11_USAGE_STAGING;
        td.BindFlags      = 0;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        td.MiscFlags      = 0;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &alphaStaging_))) {
            alphaW_ = alphaH_ = 0;
            return;
        }
    }
    context_->CopyResource(alphaStaging_.Get(), offTex_.Get());

    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(context_->Map(alphaStaging_.Get(), 0, D3D11_MAP_READ, 0, &m)))
        return;
    alphaW_ = width_;
    alphaH_ = height_;
    alphaMirror_.resize(static_cast<std::size_t>(alphaW_) * alphaH_);
    const auto* rows = static_cast<const std::uint8_t*>(m.pData);
    for (int y = 0; y < alphaH_; ++y) {
        const std::uint8_t* px = rows + static_cast<std::size_t>(y) * m.RowPitch;
        for (int x = 0; x < alphaW_; ++x)
            alphaMirror_[static_cast<std::size_t>(y) * alphaW_ + x] =
                px[x * 4 + 3];   // B8G8R8A8 → alfa i byte 3
    }
    context_->Unmap(alphaStaging_.Get(), 0);
}

// Readback (glaze-render): retained-RT:n → rå RGBA8 på CPU — samma
// staging-mönster som alfa-spegeln men egen engångstextur och alla fyra
// byten (BGRA→RGBA-swizzle till fasadens kontrakt). Anropas EFTER
// renderFrame (RT:n bär då framens innehåll).
bool D3dRenderer::captureRetained(std::vector<std::uint8_t>& rgba,
                                  int& w, int& h)
{
    if (!device_ || !context_ || !offTex_ || !retainedValid_)
        return false;   // degraderat läge/ingen frame ritad än

    D3D11_TEXTURE2D_DESC td{};
    offTex_->GetDesc(&td);
    td.Usage          = D3D11_USAGE_STAGING;
    td.BindFlags      = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    td.MiscFlags      = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &staging))) {
        log("captureFrame: staging-texturen kunde inte skapas");
        return false;
    }
    context_->CopyResource(staging.Get(), offTex_.Get());

    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) {
        log("captureFrame: Map(READ) misslyckades");
        return false;
    }
    const int tw = static_cast<int>(td.Width);
    const int th = static_cast<int>(td.Height);
    rgba.resize(static_cast<std::size_t>(tw) * th * 4);
    const auto* rows = static_cast<const std::uint8_t*>(m.pData);
    for (int y = 0; y < th; ++y) {
        const std::uint8_t* px =
            rows + static_cast<std::size_t>(y) * m.RowPitch;
        std::uint8_t* out = rgba.data() + static_cast<std::size_t>(y) * tw * 4;
        for (int x = 0; x < tw; ++x) {
            out[x * 4]     = px[x * 4 + 2];   // B8G8R8A8 → RGBA8
            out[x * 4 + 1] = px[x * 4 + 1];
            out[x * 4 + 2] = px[x * 4];
            out[x * 4 + 3] = px[x * 4 + 3];
        }
    }
    context_->Unmap(staging.Get(), 0);
    w = tw;
    h = th;
    return true;
}

bool D3dRenderer::alphaHit(int x, int y) const
{
    if (alphaW_ <= 0 || alphaH_ <= 0)
        return true;   // ingen spegel (degraderat) — opak hit-test
    if (x < 0 || y < 0 || x >= alphaW_ || y >= alphaH_)
        return false;
    // Tröskel: nästan helt genomskinliga pixlar släpper klicket igenom.
    constexpr std::uint8_t kAlphaHitThreshold = 8;
    return alphaMirror_[static_cast<std::size_t>(y) * alphaW_ + x] >=
           kAlphaHitThreshold;
}

void D3dRenderer::composite()
{
    if (!rtv_)
        return;
    flush();   // allt callback-ritat ska in i retained-RT:n först

    // Resten av framen (chrome, overlay) ritas direkt i backbuffern — som
    // nu är framens eget mål för render-målen också.
    activeRtv_ = rtv_.Get();
    frameRtv_  = activeRtv_;
    bound_     = nullptr;
    activeDsv_ = nullptr;
    context_->OMSetRenderTargets(1, &activeRtv_, nullptr);
    if (!offSrv_)
        return;   // ingen retained-RT — callbacket ritade redan hit

    // Callbacket har just ritat klart i RT:n — från och med nu bär den
    // senaste framens innehåll och nästa callback får rita partiellt.
    retainedValid_ = true;

    // Fullskärms-quad i mode 5 (RGBA rakt av). Den skriver HELA
    // backbuffern varje present — det är det som gör FLIP_DISCARD ok
    // (odefinierat innehåll efter Present) och overlayen självstädande.
    // NEAREST uttryckligen: 1:1, och en bild som ritats linjärt sist i
    // framen får inte bestämma hur fönstret samplas.
    setState(offSrv_.Get(), D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
             ImageFilter::Nearest);
    Vertex v{};
    v.rgba = 0xffffffff;
    v.mode = 5.0f;
    const float w = static_cast<float>(width_);
    const float h = static_cast<float>(height_);
    const float xs[6] = {0, w, 0, w, w, 0};
    const float ys[6] = {0, 0, h, 0, h, h};
    const float us[6] = {0, 1, 0, 1, 1, 0};
    const float vs[6] = {0, 0, 1, 0, 1, 1};
    for (int i = 0; i < 6; ++i) {
        v.x = xs[i];
        v.y = ys[i];
        v.u = us[i];
        v.v = vs[i];
        batch_.push_back(v);
    }

    if (transparent_) {
        // Transparent-vägen: RT:n bär redan premultiplicerad färg (rak-
        // alfa-blandning mot alfa-0-clear ger exakt det), och swapchainen
        // deklarerar PREMULTIPLIED — skriv den RAKT AV, utan blandning.
        // Opaka fönster behåller blend-läget (alfa är 1
        // överallt där, så vägen är orörd per konstruktion). UI-pipelinen
        // binds FÖRST: annars hade flush() — efter ett scenanrop — återbundit
        // UI:ts blandning ovanpå undantaget.
        if (pipeline_ != Pipeline::Ui)
            bindUiPipeline();
        setBlendState(nullptr);
        flush();
        setBlendState(blendStates_[static_cast<int>(Blend::Alpha)].Get());
        return;
    }
    flush();
}

// ---------------------------------------------------------------- batcher

void D3dRenderer::flush()
{
    if (batch_.empty() || !context_)
        return;
    // Ett drawMesh efter förra UI-ritningen har lämnat scenens program
    // bundet. Återbindningen sitter HÄR, där UI-pipelinen faktiskt ritar —
    // då finns ingen väg till en Draw som kan gå förbi den.
    if (pipeline_ != Pipeline::Ui)
        bindUiPipeline();

    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(context_->Map(vbuf_.Get(), 0, D3D11_MAP_WRITE_DISCARD,
                             0, &m))) {
        batch_.clear();
        return;
    }
    std::memcpy(m.pData, batch_.data(), batch_.size() * sizeof(Vertex));
    context_->Unmap(vbuf_.Get(), 0);

    ID3D11ShaderResourceView* srv = curSrv_ ? curSrv_ : whiteSrv_.Get();
    context_->PSSetShaderResources(0, 1, &srv);
    // Batchens filter — vid VARJE Draw, så att ingen annan väg (scenens
    // program binder sin egen sampler) kan bestämma det.
    ID3D11SamplerState* smp = samplerFor(curFilter_);
    context_->PSSetSamplers(0, 1, &smp);
    context_->IASetPrimitiveTopology(curTopo_);
    context_->Draw(static_cast<UINT>(batch_.size()), 0);
    ++stats_.drawCalls;
    stats_.vertices += batch_.size();
    batch_.clear();
}

// UI-pipelinens tillstånd tillbaka efter scenens program: det drawMesh
// bytte (layout, vertexbuffert, shaders) och också
// renderläget — djup, rasteriserare och blandning (`applyUiState`).
// Viewportbufferten b0 och maskerna i b1 rör scenen aldrig; topologin,
// texturen och samplern (Image-programmet binder egna på t0/s0) sätter
// flush() vid varje Draw.
void D3dRenderer::bindUiPipeline()
{
    context_->IASetInputLayout(layout_.Get());
    const UINT stride = sizeof(Vertex), offset = 0;
    context_->IASetVertexBuffers(0, 1, vbuf_.GetAddressOf(), &stride,
                                 &offset);
    context_->VSSetShader(vs_.Get(), nullptr, 0);
    context_->PSSetShader(ps_.Get(), nullptr, 0);
    applyUiState();
    pipeline_ = Pipeline::Ui;
    ++stats_.programSwitches;
}

// ---------------------------------------------------------- renderläget
//
// Se gfx/scene.h `RenderState`. Djupet verkar bara i ett
// mål MED djup (annars som Off — D3D11 hade testat mot en DSV som inte finns
// och släppt igenom allt, men Metal kräver det uttryckligen, så regeln är en
// och densamma). Med ett djuptest klipper rasteriseraren också i z
// (DepthClipEnable), som GL och Metal alltid gör — 2D-vägen klipper inte i z.

void D3dRenderer::applyMeshState(const RenderState& s)
{
    const bool depth =
        s.depth != DepthTest::Off && bound_ && bound_->hasDepth();
    const DepthTest test = depth ? s.depth : DepthTest::Off;
    setDepthState(depthStates_[static_cast<int>(test)][s.depthWrite ? 1 : 0]
                      .Get());
    // DepthBias är ett heltal i D3D11 (renderläget säger det) — avrundat och
    // klampat till int.
    const float bias  = depth ? std::clamp(s.depthBias, -2.0e9f, 2.0e9f) : 0.0f;
    setRasterState(rasterFor(s.cull, static_cast<int>(std::lround(bias)),
                             depth ? s.slopeBias : 0.0f, depth));
    setBlendState(blendStates_[static_cast<int>(s.blend)].Get());
}

void D3dRenderer::applyUiState()
{
    setDepthState(depthStates_[static_cast<int>(DepthTest::Off)][0].Get());
    setRasterState(rasterFor(Cull::None, 0, 0.0f, false));
    setBlendState(blendStates_[static_cast<int>(Blend::Alpha)].Get());
}

void D3dRenderer::resetStateCache()
{
    depthKnown_  = false;
    rasterKnown_ = false;
    blendKnown_  = false;
}

void D3dRenderer::setDepthState(ID3D11DepthStencilState* s)
{
    if (depthKnown_ && s == curDepth_)
        return;
    context_->OMSetDepthStencilState(s, 0);
    curDepth_   = s;
    depthKnown_ = true;
}

void D3dRenderer::setRasterState(ID3D11RasterizerState* s)
{
    if (rasterKnown_ && s == curRaster_)
        return;
    context_->RSSetState(s);
    curRaster_   = s;
    rasterKnown_ = true;
}

void D3dRenderer::setBlendState(ID3D11BlendState* s)
{
    if (blendKnown_ && s == curBlend_)
        return;
    const float factor[4] = {0, 0, 0, 0};
    context_->OMSetBlendState(s, factor, 0xffffffff);
    curBlend_   = s;
    blendKnown_ = true;
}

ID3D11RasterizerState* D3dRenderer::rasterFor(Cull cull, int bias, float slope,
                                              bool depthClip)
{
    const bool msaa = bound_ && bound_->samples() > 1;
    // 2D-vägens rasteriserare — exakt de objekt som fanns före 3D-passet.
    if (!msaa && cull == Cull::None && bias == 0 && slope == 0.0f && !depthClip)
        return scissorOn_ ? rasterScissor_.Get() : raster_.Get();

    const unsigned flags = static_cast<unsigned>(cull) |
                           (scissorOn_ ? 4u : 0u) | (msaa ? 8u : 0u) |
                           (depthClip ? 16u : 0u);
    const auto key = std::make_tuple(bias, slope, flags);
    if (auto it = rasterCache_.find(key); it != rasterCache_.end())
        return it->second.Get();
    // D3D11 rymmer högst 4096 unika rasteriserare per enhet. Fler är ett
    // fel i den som ritar (en bias per anrop?) — då gäller UI:ts, loggat.
    if (rasterCache_.size() >= 4096) {
        if (!rasterCacheFullLogged_) {
            rasterCacheFullLogged_ = true;
            log("D3D11: 4096 rasteriserare — gallring och bias ignoreras för "
                "fler kombinationer");
        }
        return scissorOn_ ? rasterScissor_.Get() : raster_.Get();
    }
    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = cull == Cull::Back    ? D3D11_CULL_BACK
                  : cull == Cull::Front ? D3D11_CULL_FRONT
                                        : D3D11_CULL_NONE;
    // Framsidan är MOTURS SETT FRÅN KAMERAN (gfx/scene.h). D3D11 avgör
    // vindningen som triangeln syns i målet, och vertexshadrarnas NDC har y
    // uppåt — moturs där är moturs för ögat.
    rd.FrontCounterClockwise = TRUE;
    rd.DepthBias             = bias;
    rd.SlopeScaledDepthBias  = slope;
    rd.DepthClipEnable       = depthClip ? TRUE : FALSE;
    rd.ScissorEnable         = scissorOn_ ? TRUE : FALSE;
    // I ett MSAA-mål ska trianglarna provtas — på feature level 10.0 kräver
    // det MultisampleEnable (på 10.1+ styr den bara linjerna).
    rd.MultisampleEnable = msaa ? TRUE : FALSE;
    ComPtr<ID3D11RasterizerState> s;
    if (FAILED(device_->CreateRasterizerState(&rd, &s)))
        return scissorOn_ ? rasterScissor_.Get() : raster_.Get();
    ID3D11RasterizerState* raw = s.Get();
    rasterCache_.emplace(key, std::move(s));
    return raw;
}

// Byte av textur, topologi eller FILTER bryter batchen — flusha först så
// ritordningen (bakifrån och fram) bevaras.
void D3dRenderer::setState(ID3D11ShaderResourceView* srv,
                           D3D11_PRIMITIVE_TOPOLOGY topo, ImageFilter filter)
{
    if (srv != curSrv_ || topo != curTopo_ || filter != curFilter_)
        flush();
    curSrv_    = srv;
    curTopo_   = topo;
    curFilter_ = filter;
    if (batch_.size() + 6 > kMaxVerts)
        flush();
}

static std::uint32_t pack(Color c)
{
    // R8G8B8A8_UNORM läser bytes i minnesordning r,g,b,a (little-endian)
    return static_cast<std::uint32_t>(c.r) |
           static_cast<std::uint32_t>(c.g) << 8 |
           static_cast<std::uint32_t>(c.b) << 16 |
           static_cast<std::uint32_t>(c.a) << 24;
}

void D3dRenderer::quad(const Rectf& d, const Rectf& uv, Color c)
{
    // p0/p1/mode noll-initieras av aggregatet — mode 0 = texturmask
    const std::uint32_t col = pack(c);
    const float x1 = d.x, y1 = d.y, x2 = d.x + d.w, y2 = d.y + d.h;
    const float u1 = uv.x, v1 = uv.y, u2 = uv.x + uv.w, v2 = uv.y + uv.h;
    batch_.push_back({x1, y1, u1, v1, col});
    batch_.push_back({x2, y1, u2, v1, col});
    batch_.push_back({x1, y2, u1, v2, col});
    batch_.push_back({x2, y1, u2, v1, col});
    batch_.push_back({x2, y2, u2, v2, col});
    batch_.push_back({x1, y2, u1, v2, col});
}

void D3dRenderer::sdfQuad(const Rectf& d, Color c, float mode,
                          const Rectf& prim, float radius, float param,
                          float offX, float offY)
{
    Vertex v{};
    v.rgba  = pack(c);
    v.p0[0] = prim.x + prim.w * 0.5f;   // centrum
    v.p0[1] = prim.y + prim.h * 0.5f;
    v.p0[2] = prim.w * 0.5f;            // halvmått
    v.p0[3] = prim.h * 0.5f;
    v.p1[0] = radius;
    v.p1[1] = param;
    v.p1[2] = offX;
    v.p1[3] = offY;
    v.mode  = mode;

    const float x1 = d.x, y1 = d.y, x2 = d.x + d.w, y2 = d.y + d.h;
    const float xs[6] = {x1, x2, x1, x2, x2, x1};
    const float ys[6] = {y1, y1, y2, y1, y2, y2};
    for (int i = 0; i < 6; ++i) {
        v.x = xs[i];
        v.y = ys[i];
        batch_.push_back(v);
    }
}

// ---------------------------------------------------------------- klipp

static float clampRadius(const Rectf& r, float radius);   // se rit-API:t

// HLSL-arrayernas längd är skriven som en literal i shaderkällan — den kan
// inte läsa en C++-konstant, så låset står här i stället.
static_assert(kMaxRoundedClips == 4,
              "gClipP0/gClipP1 i kHlslClipSrc är deklarerade [4]");

// De rundade maskerna → PS-cbuffern b1. Anropas EFTER flushen i applyClip:
// allt som ritats före bytet ska ha den GAMLA masken, exakt som med
// scissorn (som är context-state av samma skäl).
void D3dRenderer::uploadClipMasks(const RoundedClip* masks, std::size_t count)
{
    // DET VANLIGA FALLET ÄR GRATIS: inga rundade masker nu och inga förut
    // ⇒ ingen Map, inget arbete. Uniformen sätts i övrigt i den flush
    // `applyClip` ändå gör, så varken flushar eller drawcalls tillkommer.
    if (count == 0 && clipMaskCount_ == 0)
        return;
    if (!clipCbuf_)
        return;

    ClipMaskCb cb{};
    for (std::size_t i = 0; i < count && i < kMaxRoundedClips; ++i) {
        const Rectf& m = masks[i].rect;
        cb.p0[i][0] = m.x + m.w * 0.5f;   // centrum
        cb.p0[i][1] = m.y + m.h * 0.5f;
        cb.p0[i][2] = m.w * 0.5f;         // halvmått
        cb.p0[i][3] = m.h * 0.5f;
        // Samma klampning som fasadens övriga radier (halva kortsidan).
        cb.p1[i][0] = clampRadius(m, masks[i].radius);
    }
    cb.meta[0] = static_cast<float>(count);

    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(context_->Map(clipCbuf_.Get(), 0, D3D11_MAP_WRITE_DISCARD,
                                0, &m))) {
        std::memcpy(m.pData, &cb, sizeof cb);
        context_->Unmap(clipCbuf_.Get(), 0);
    }
    clipMaskCount_ = static_cast<int>(count);
}

void D3dRenderer::applyClip(const Rectf* r, const RoundedClip* masks,
                            std::size_t count)
{
    if (!context_)
        return;
    // Scissor är context-state, inte per vertex — allt som batchats före
    // bytet måste ritas med GAMLA klippet (samma mönster som textur-/
    // topologibyte i setState).
    flush();
    // RUNDADE MASKER → PS-cbuffern b1, efter flushen (allt före bytet ska
    // ha gamla masken). b0 är VS:ens viewportbuffer och rörs aldrig.
    uploadClipMasks(masks, count);
    // Rasteriseraren är UI:ts med scissorn av eller på — och i ett MSAA-mål
    // dess MSAA-variant (`rasterFor`). Ett scenanrop som följer sätter sin
    // egen med samma scissor.
    scissorOn_ = r != nullptr;
    if (!r) {
        setRasterState(rasterFor(Cull::None, 0, 0.0f, false));
        return;
    }
    // Runda UTÅT till hela pixlar (delvis täckta pixlar ingår) — samma
    // expansion i alla backends (fasadkontraktet i renderer.h).
    D3D11_RECT sc;
    sc.left   = static_cast<LONG>(std::floor(r->x));
    sc.top    = static_cast<LONG>(std::floor(r->y));
    sc.right  = static_cast<LONG>(std::ceil(r->x + r->w));
    sc.bottom = static_cast<LONG>(std::ceil(r->y + r->h));
    context_->RSSetScissorRects(1, &sc);
    setRasterState(rasterFor(Cull::None, 0, 0.0f, false));
}

// ---------------------------------------------------------------- rit-API

void D3dRenderer::clear(Color c)
{
    // Clear:ar det AKTIVA målet — retained-RT:n under render-callbacket,
    // backbuffern efter composite (ingen gör det senare). I ett
    // offscreen-block utan bundet mål finns inget att rensa.
    ID3D11RenderTargetView* target =
        activeRtv_ ? activeRtv_ : (offscreen_ ? nullptr : rtv_.Get());
    if (!target)
        return;
    flush();   // allt ritat före clear ska bort — bevara ordningen
    // PREMULTIPLICERAD: varje mål bär premultiplicerat innehåll (rak-alfa-
    // blandning mot en genomskinlig yta ger exakt det), så rensfärgen
    // lagras i samma form. Opak och helt genomskinlig är oförändrade.
    const float a      = c.a / 255.0f;
    const float col[4] = {c.r / 255.0f * a, c.g / 255.0f * a,
                          c.b / 255.0f * a, a};
    context_->ClearRenderTargetView(target, col);
    // Ett mål med djup får djupet 1 — det bortre planet.
    if (activeDsv_)
        context_->ClearDepthStencilView(activeDsv_, D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void D3dRenderer::fillRect(const Rectf& r, Color c)
{
    setState(nullptr, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
             ImageFilter::Nearest);
    quad(r, Rectf{0, 0, 0, 0}, c);   // uv 0 → vita texturens enda texel
}

void D3dRenderer::strokeRect(const Rectf& r, Color c)
{
    // Fyra 1-px-stavar istället för LINELIST — exakt pixeltäckning i
    // hörnen (linjerasterisering tappar sista pixeln).
    fillRect(Rectf{r.x, r.y, r.w, 1.0f}, c);
    fillRect(Rectf{r.x, r.y + r.h - 1.0f, r.w, 1.0f}, c);
    fillRect(Rectf{r.x, r.y + 1.0f, 1.0f, r.h - 2.0f}, c);
    fillRect(Rectf{r.x + r.w - 1.0f, r.y + 1.0f, 1.0f, r.h - 2.0f}, c);
}

void D3dRenderer::line(float x1, float y1, float x2, float y2, Color c)
{
    setState(nullptr, D3D11_PRIMITIVE_TOPOLOGY_LINELIST, ImageFilter::Nearest);
    const std::uint32_t col = pack(c);
    // +0.5: träffa pixelcentrum så linjen inte hamnar mellan pixlar
    batch_.push_back({x1 + 0.5f, y1 + 0.5f, 0, 0, col});
    batch_.push_back({x2 + 0.5f, y2 + 0.5f, 0, 0, col});
}

// Radien får aldrig överstiga halva kortsidan — då degenererar SDF:en.
static float clampRadius(const Rectf& r, float radius)
{
    const float m = std::min(r.w, r.h) * 0.5f;
    return radius < 0.0f ? 0.0f : (radius > m ? m : radius);
}

void D3dRenderer::fillRoundedRect(const Rectf& r, float radius, Color c)
{
    setState(nullptr, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
             ImageFilter::Nearest);
    // Quaden 1 px större än rektangeln så AA-kanten inte klipps
    const Rectf q{r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f};
    sdfQuad(q, c, 1.0f, r, clampRadius(r, radius), 0.0f, 0.0f, 0.0f);
}

void D3dRenderer::strokeRoundedRect(const Rectf& r, float radius, Color c,
                                    float width)
{
    setState(nullptr, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
             ImageFilter::Nearest);
    const Rectf q{r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f};
    sdfQuad(q, c, 2.0f, r, clampRadius(r, radius), width, 0.0f, 0.0f);
}

void D3dRenderer::dropShadow(const Rectf& r, float radius,
                             float offsetX, float offsetY, float blur,
                             Color c)
{
    setState(nullptr, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
             ImageFilter::Nearest);
    // Skuggkällan är rektangeln förskjuten med offset; quaden täcker
    // hela uttoningen (blur åt båda hållen + AA-marginal)
    const Rectf s{r.x + offsetX, r.y + offsetY, r.w, r.h};
    const float b = blur < 0.5f ? 0.5f : blur;
    const Rectf q{s.x - b - 1.0f, s.y - b - 1.0f,
                  s.w + 2.0f * (b + 1.0f), s.h + 2.0f * (b + 1.0f)};
    sdfQuad(q, c, 3.0f, s, clampRadius(s, radius), b, 0.0f, 0.0f);
}

void D3dRenderer::innerShadow(const Rectf& r, float radius,
                              float offsetX, float offsetY, float blur,
                              Color c)
{
    setState(nullptr, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
             ImageFilter::Nearest);
    // Skuggan lever bara innanför r (shadern klipper mot rektangelns SDF)
    const Rectf q{r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f};
    sdfQuad(q, c, 4.0f, r, clampRadius(r, radius),
            blur < 0.5f ? 0.5f : blur, offsetX, offsetY);
}

float D3dRenderer::drawText(Font& f, float x, float y,
                            const std::string& utf8, Color c)
{
    // Fonten kommer alltid ur samma backend (loadFont/loadDefaultFont
    // nedan) — casten är säker per konstruktion.
    return static_cast<D3dFont&>(f).draw(*this, x, y, utf8, c);
}

void D3dRenderer::texturedQuad(ID3D11ShaderResourceView* srv,
                               const Rectf& dst, const Rectf& uv, Color c)
{
    // Glyferna är rasteriserade för hela pixlar och ritas 1:1 — NEAREST
    // uttryckligen, oberoende av vad en bild före dem samplades med.
    setState(srv, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, ImageFilter::Nearest);
    quad(dst, uv, c);
}

// ---------------------------------------------------------------- fonter

std::unique_ptr<Font> D3dRenderer::loadFont(const std::string& path,
                                            float sizePt)
{
    // Returnerar alltid ett objekt — valid() avgör om laddningen lyckades
    // (text uteblir men appen kör).
    auto font = std::make_unique<D3dFont>();
    font->setDevice(device_.Get(), context_.Get());
    font->load(path, sizePt);
    return font;
}

std::unique_ptr<Font> D3dRenderer::loadFont(FontFamily family, float sizePt,
                                            FontStyle style)
{
    auto font = std::make_unique<D3dFont>();
    font->setDevice(device_.Get(), context_.Get());
    font->loadFamily(family, sizePt, style);
    return font;
}

} // namespace gfx
