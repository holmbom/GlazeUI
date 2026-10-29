// Glyph-programmet i D3dRenderer — text och markörer
// ur `GlyphVertex` (gfx/scene.h) — och ur
// `GlyphVertex3` (Glyph3: samma källa med GLAZE_POS3, ankaret i xyz) — som
// BILLBOARDS: ankaret projiceras, och
// hörnets förskjutning (em × storleken i px) läggs i SKÄRMEN, vriden efter
// skärmen eller efter kartans projicerade riktning vid ankaret. Etiketten
// står alltså upprätt under bäring och tilt och har samma storlek överallt.
// LIGGANDE TEXT (`glyphflag::kGroundAligned`) lägger i
// stället hörnen i världsplanet vid ankaret (`glyphGround`).
//
// Fragmentshadern läser SDF-atlasen (gfx/sdf.h): fältets värde ger avståndet
// till kanten i atlasens texlar, skalan skärm-px per texel är storleken
// delad med referensen, och täckningen är avståndet i px + ½ — en px bred
// kant vid varje storlek. Glorian är samma fält med kanten flyttad utåt med
// `haloWidth` (eget pass, se glyphflag), syntetisk fet en mindre förskjutning.
// FORMERNA (platta, cirkel, kvadrat) är avståndsfält räknade i shadern ur
// hörnets läge relativt formens mitt; vertexshadern växer quaden en px så att
// kantens utsida får sin halva av kantutjämningen.
//
// Uniformerna i scenens b2 (`kHlslPaintSrc`): gParams .xy målets storlek, .z
// px per storleksenhet (px: uiScale, mm: pxPerMm), .w etiketternas bas
// (`MeshDraw::labelBase`, exakt som flyttal under 2²⁴ — eller −1 när ingen
// synlighet är bunden); gPaint0 = (fältets referens i texlar per em, radien
// i texlar, syntetisk fet i texlar, 0); gPaint1.y anropets opacitet, och
// gPaint1.x/.w GOLVET OCH TAKET för en em i fysiska px
// (`SceneUniforms::glyphMinPx`/`glyphMaxPx`; 0 = ingen) — storleken klampas
// innan något läser den.
// Atlassidan på t0 med den linjära samplern; ETIKETTERNAS SYNLIGHET
// på VERTEXSTEGETS t1 — en R8-texel per etikett, rad för rad, läst med
// Load (ingen sampler). Vertexshadern gallrar en dold etikett och för en
// tonad etiketts synlighet vidare till pixelshadern, som multiplicerar
// alfan.

#include <algorithm>
#include <cstring>

#include "d3d11_backend.h"
#include "gfx/platform.h"
#include "gfx/sdf.h"

namespace gfx {
namespace {

// Klippmaskernas och färgens delar (`kHlslClipSrc`, `kHlslPaintSrc`) klistras
// FÖRE. GLYPH3 är samma källa med GLAZE_POS3: ankaret i
// xyz (`GlyphVertex3`), allt annat lika.
const char kGlyphShaderSrc[] = R"hlsl(
#ifdef GLAZE_POS3
typedef float3 GlyphPos;
#else
typedef float2 GlyphPos;
#endif

struct GlyphIn
{
    GlyphPos anchor : POSITION;
    float2 off    : TEXCOORD0;   // hörnet från ankaret, em (y nedåt)
    float2 uv     : TEXCOORD1;   // atlas-UV, eller formens läge (em)
    float4 col    : COLOR0;
    float4 halo   : COLOR1;
    float  size   : TEXCOORD2;   // em i enheten
    float  angle  : TEXCOORD3;   // radianer, moturs
    float  haloW  : TEXCOORD4;   // glorians/konturens bredd i enheten
    uint   flags  : TEXCOORD5;
    uint   label  : TEXCOORD6;   // etikettens index i bygget
};

struct GlyphOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
    nointerpolation float4 col   : COLOR0;
    nointerpolation float4 halo  : COLOR1;
    // .xy formens halva mått (em), .z storleken i px, .w glorians bredd i em
    nointerpolation float4 shape : TEXCOORD1;
    nointerpolation uint   flags : TEXCOORD2;
    nointerpolation float  vis   : TEXCOORD3;   // etikettens synlighet 0–1
};

// Etiketternas synlighet — vertexstegets t1.
Texture2D<float> gLabels : register(t1);

// Synligheten för etiketten `label` i bygget: 1 när ingen synlighet är
// bunden (basen negativ), annars texeln på basen + indexet, rad för rad.
float glyphVisibility(uint label)
{
    if (gParams.w < 0.0)
        return 1.0;
    uint w, h;
    gLabels.GetDimensions(w, h);
    const uint i = uint(gParams.w + 0.5) + label;
    return gLabels.Load(int3(int(i % w), int(i / w), 0));
}

#ifdef GLAZE_POS3
float4 glyphToClip(float3 p) { return mul(gViewProj, float4(p, 1.0)); }
#else
float4 glyphToClip(float2 p) { return mul(gViewProj, float4(p, 0.0, 1.0)); }
#endif

float4 glyphToNdc(float4 c)
{
    return float4(c.x * 2.0 / gParams.x - c.w, c.w - c.y * 2.0 / gParams.y,
                  c.z, c.w);
}

// Världsriktningen v som SKÄRMENS riktning vid ankaret (pixelklippet c):
// derivatan av perspektivdivisionen, px per världsenhet längs v (y nedåt).
float2 glyphScreenDir(float4 c, float3 v)
{
    const float4 d = mul(gViewProj, float4(v, 0.0));
    return (d.xy * c.w - c.xy * d.w) / (c.w * c.w);
}

// LIGGANDE TEXT (glyphflag::kGroundAligned): quaden LIGGER
// i världsplanet vid ankaret. Baslinjen är världsriktningen (cos, sin, 0) —
// kartans vinkel — och "nedåt" dess vinkelräta i planet, vänd så att
// baslinjen och nedåt bildar skärmens ram (texten blir aldrig spegelvänd; en
// karta vriden 180° visar den upp och ner). Skalan k är px per
// världsenhet VID ANKARET vinkelrätt mot blicken — blicken ur matrisens
// w-rad, ortografiskt ur djupraden — så en em har sin storlek där och
// förkortas längs blicken som allt annat på marken. `sizeEff` får den
// effektiva px-storleken för kantutjämningen: de två axlarnas geometriska
// medel vid ankaret.
float4 glyphGround(GlyphPos anchor, float4 c, float angle, float2 off,
                   float sizePx, out float sizeEff)
{
    const float3 ew = float3(cos(angle), sin(angle), 0.0);
    float3       dw = float3(-ew.y, ew.x, 0.0);
    const float2 es = glyphScreenDir(c, ew);
    float2       ds = glyphScreenDir(c, dw);
    if (es.x * ds.y - es.y * ds.x < 0.0) {
        dw = -dw;
        ds = -ds;
    }
    float3 fw = float3(mul(gViewProj, float4(1.0, 0.0, 0.0, 0.0)).w,
                       mul(gViewProj, float4(0.0, 1.0, 0.0, 0.0)).w,
                       mul(gViewProj, float4(0.0, 0.0, 1.0, 0.0)).w);
    if (dot(fw, fw) < 1e-20)
        fw = float3(mul(gViewProj, float4(1.0, 0.0, 0.0, 0.0)).z,
                    mul(gViewProj, float4(0.0, 1.0, 0.0, 0.0)).z,
                    mul(gViewProj, float4(0.0, 0.0, 1.0, 0.0)).z);
    if (dot(fw, fw) < 1e-20)
        fw = float3(0.0, 0.0, 1.0);
    float3 tv = cross(fw, ew);
    if (dot(tv, tv) < 1e-20)
        tv = cross(fw, dw);
    const float k = length(glyphScreenDir(c, normalize(tv)));
    const float s = sizePx / max(k, 1e-12);   // världsenheter per em
    sizeEff       = sqrt(length(es) * length(ds)) * s;
    const float3 w = (off.x * ew + off.y * dw) * s;
#ifdef GLAZE_POS3
    return glyphToNdc(mul(gViewProj, float4(anchor + w, 1.0)));
#else
    return glyphToNdc(mul(gViewProj, float4(anchor + w.xy, 0.0, 1.0)));
#endif
}

GlyphOut glyphVs(GlyphIn i)
{
    GlyphOut o;
    o.col   = i.col;
    o.halo  = i.halo;
    o.flags = i.flags;
    o.uv    = i.uv;
    o.shape = float4(0.0, 0.0, 0.0, 0.0);
    o.vis   = glyphVisibility(i.label);
    const float4 c = glyphToClip(i.anchor);
    if (c.z < 0.0 || c.w <= 1e-6 || o.vis <= 0.0) {
        // Ankaret bakom närplanet, eller etiketten dold av kollisionen:
        // den syns inte — utanför klippet.
        o.pos = float4(0.0, 0.0, -2.0, 1.0);
        return o;
    }
    float sizePx = i.size * gParams.z;
    // Golvet, sedan taket — taket vinner när de korsas.
    if (gPaint1.x > 0.0) sizePx = max(sizePx, gPaint1.x);
    if (gPaint1.w > 0.0) sizePx = min(sizePx, gPaint1.w);
    // Baslinjens riktning i skärmen (y nedåt): skärmens vinkel, eller
    // världsriktningen så som kameran projicerar den vid ankaret (derivatan
    // av perspektivdivisionen — exakt även under tilt).
    float2 e;
    if ((i.flags & 4u) != 0u) {
        const float4 d = mul(gViewProj, float4(cos(i.angle), sin(i.angle),
                                               0.0, 0.0));
        const float2 s = (d.xy * c.w - c.xy * d.w) / (c.w * c.w);
        const float  l = length(s);
        e = l > 1e-12 ? s / l : float2(1.0, 0.0);
    } else {
        e = float2(cos(i.angle), -sin(i.angle));
    }
    const float2 down = float2(-e.y, e.x);
    float2 off = i.off;
    if ((i.flags & 3u) != 0u) {
        // En form: quaden växer en px utåt, och formens läge med den.
        o.shape.xy      = abs(i.uv);
        const float  g  = 1.0 / max(sizePx, 1e-6);
        const float2 sg = sign(i.uv);
        off  += sg * g;
        o.uv += sg * g;
    }
    if ((i.flags & 32u) != 0u) {
        // En BILDMARKÖR: rutan (en kvadrat i em) får bildens proportioner —
        // den kortare axeln krymper kring punkten, så ankaret står kvar —
        // och en bild med raderna nerifrån (ett GL-mål) vänds.
        const float aspect = gPaint0.w;   // bildens bredd / höjd
        off *= aspect >= 1.0 ? float2(1.0, 1.0 / aspect) : float2(aspect, 1.0);
        if ((uint(gPaint1.z + 0.5) & 2u) != 0u)
            o.uv.y = 1.0 - o.uv.y;
    }
    o.shape.z = sizePx;
    o.shape.w = i.size > 0.0 ? i.haloW / i.size : 0.0;
    if ((i.flags & 64u) != 0u) {
        // LIGGANDE: hörnet i världsplanet — glyphGround.
        float eff;
        o.pos     = glyphGround(i.anchor, c, i.angle, off, sizePx, eff);
        o.shape.z = eff;
        return o;
    }
    // STÅENDE: hörnets djup med planet genom ankaret (`glaze_standingZ`,
    // kHlslStandSrc) — en markör på marken skärs inte av den.
    const float2 o2 = (off.x * e + off.y * down) * sizePx;
    const float2 p  = c.xy / c.w + o2;
    o.pos = glyphToNdc(
        float4(p * c.w, glaze_standingZ(gViewProj, c, o2), c.w));
    return o;
}

float4 glyphPs(GlyphOut i) : SV_Target
{
    const uint  shape  = i.flags & 3u;
    const float sizePx = i.shape.z;
    float3 rgb;
    float  a;
    if ((i.flags & 32u) != 0u) {
        // BILDMARKÖREN: bildens texel, som Image-programmet läser den — R8
        // som gråskala, ett måls premultiplicerade innehåll tillbaka till
        // rak alfa (gPaint1.z: flaggorna 4 och 1).
        float4    t = gRamp.Sample(gLinear, i.uv);
        const uint f = uint(gPaint1.z + 0.5);
        if ((f & 4u) != 0u)
            t = float4(t.rrr, 1.0);
        if ((f & 1u) != 0u)
            t.rgb = t.a > 0.0 ? t.rgb / t.a : float3(0.0, 0.0, 0.0);
        rgb = t.rgb;
        a   = t.a;
    } else if (shape == 0u) {
        // Fältet: avståndet till kanten i atlasens texlar, positivt inne.
        float d = (gRamp.Sample(gLinear, i.uv).r - 0.5) * 2.0 * gPaint0.y;
        if ((i.flags & 8u) != 0u)
            d += gPaint0.z;                  // syntetisk fet
        const bool halo = (i.flags & 16u) != 0u;
        if (halo)
            d += i.shape.w * gPaint0.x;      // glorians bredd, em → texlar
        const float4 col = halo ? i.halo : i.col;
        rgb = col.rgb;
        a   = col.a * saturate(d * sizePx / gPaint0.x + 0.5);
    } else {
        // Formen: avståndet till ytterkanten i em, positivt inne; konturen
        // är bandet mellan ytterkanten och fyllningens kant.
        const float dOut = shape == 2u
                               ? i.shape.x - length(i.uv)
                               : min(i.shape.x - abs(i.uv.x),
                                     i.shape.y - abs(i.uv.y));
        const float outer = saturate(dOut * sizePx + 0.5);
        const float inner = saturate((dOut - i.shape.w) * sizePx + 0.5);
        // Fyllningen över konturen, premultiplicerat och därefter tillbaka. Under
        // PLATTAN ligger konturen bara i bandet: en genomskinlig
        // platta färgas inte av den, och en osynlig blir en ram.
        const float fa = i.col.a * inner;
        const float ha = i.halo.a * (shape == 1u ? outer - inner
                                                  : outer * (1.0 - fa));
        a   = fa + ha;
        rgb = a > 0.0 ? (i.col.rgb * fa + i.halo.rgb * ha) / a : i.col.rgb;
    }
    return float4(rgb, a * i.vis * gPaint1.y * clipCoverage(i.pos.xy) *
                           sceneClip(i.pos.xy));
}
)hlsl";

} // namespace

bool D3dRenderer::initGlyphProgram()
{
    const std::string src =
        std::string(kHlslClipSrc) + kHlslPaintSrc + kHlslStandSrc +
        kGlyphShaderSrc;
    ComPtr<ID3DBlob> vsb = compileHlsl(src, "glyphVs", "vs_4_0");
    ComPtr<ID3DBlob> psb = compileHlsl(src, "glyphPs", "ps_4_0");

    // Layouten läser GlyphVertex på dess fasta offset (gfx/scene.h).
    const D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         offsetof(GlyphVertex, ax), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         offsetof(GlyphVertex, ox), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0,
         offsetof(GlyphVertex, u), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0,
         offsetof(GlyphVertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0,
         offsetof(GlyphVertex, halo), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0,
         offsetof(GlyphVertex, size), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT, 0,
         offsetof(GlyphVertex, angle), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 4, DXGI_FORMAT_R32_FLOAT, 0,
         offsetof(GlyphVertex, haloWidth), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 5, DXGI_FORMAT_R32_UINT, 0,
         offsetof(GlyphVertex, flags), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 6, DXGI_FORMAT_R32_UINT, 0,
         offsetof(GlyphVertex, label), D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    const bool ok =
        vsb && psb &&
        SUCCEEDED(device_->CreateVertexShader(vsb->GetBufferPointer(),
                                              vsb->GetBufferSize(), nullptr,
                                              &glyphVs_)) &&
        SUCCEEDED(device_->CreatePixelShader(psb->GetBufferPointer(),
                                             psb->GetBufferSize(), nullptr,
                                             &glyphPs_)) &&
        SUCCEEDED(device_->CreateInputLayout(il, 10, vsb->GetBufferPointer(),
                                             vsb->GetBufferSize(),
                                             &glyphLayout_));
    if (!ok) {
        log("D3D11: Glyph-programmet kunde inte skapas — drawMesh ritar ingen "
            "text");
        shutdownGlyphProgram();
        return false;
    }

    // GLYPH3: samma källa med GLAZE_POS3 — ankaret är tre
    // flyttal, resten på `GlyphVertex3`s offset.
    const D3D_SHADER_MACRO pos3[] = {{"GLAZE_POS3", "1"}, {nullptr, nullptr}};
    ComPtr<ID3DBlob> vs3 = compileHlsl(src, "glyphVs", "vs_4_0", pos3);
    ComPtr<ID3DBlob> ps3 = compileHlsl(src, "glyphPs", "ps_4_0", pos3);
    const D3D11_INPUT_ELEMENT_DESC il3[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         offsetof(GlyphVertex3, ax), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         offsetof(GlyphVertex3, ox), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0,
         offsetof(GlyphVertex3, u), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0,
         offsetof(GlyphVertex3, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0,
         offsetof(GlyphVertex3, halo), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0,
         offsetof(GlyphVertex3, size), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT, 0,
         offsetof(GlyphVertex3, angle), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 4, DXGI_FORMAT_R32_FLOAT, 0,
         offsetof(GlyphVertex3, haloWidth), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 5, DXGI_FORMAT_R32_UINT, 0,
         offsetof(GlyphVertex3, flags), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 6, DXGI_FORMAT_R32_UINT, 0,
         offsetof(GlyphVertex3, label), D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    const bool ok3 =
        vs3 && ps3 &&
        SUCCEEDED(device_->CreateVertexShader(vs3->GetBufferPointer(),
                                              vs3->GetBufferSize(), nullptr,
                                              &glyph3Vs_)) &&
        SUCCEEDED(device_->CreatePixelShader(ps3->GetBufferPointer(),
                                             ps3->GetBufferSize(), nullptr,
                                             &glyph3Ps_)) &&
        SUCCEEDED(device_->CreateInputLayout(il3, 10, vs3->GetBufferPointer(),
                                             vs3->GetBufferSize(),
                                             &glyph3Layout_));
    if (!ok3) {
        log("D3D11: Glyph3-programmet kunde inte skapas — texten i rymden "
            "ritas inte");
        glyph3Layout_.Reset();
        glyph3Ps_.Reset();
        glyph3Vs_.Reset();
    }
    return true;
}

void D3dRenderer::shutdownGlyphProgram()
{
    glyph3Layout_.Reset();
    glyph3Ps_.Reset();
    glyph3Vs_.Reset();
    glyphLayout_.Reset();
    glyphPs_.Reset();
    glyphVs_.Reset();
}

void D3dRenderer::drawGlyphMesh(const MeshDraw& d)
{
    // Glyph och Glyph3 — samma källa, var sin variant.
    const bool pos3 = d.program == MeshProgram::Glyph3;
    if (!context_ ||
        !meshDrawOk(d, pos3 ? glyph3Vs_ != nullptr : glyphVs_ != nullptr) ||
        !sceneCbuf_)
        return;

    flush();   // det UI:t batchat före anropet ritas först — målarordningen
    const Pipeline want = pos3 ? Pipeline::Glyph3 : Pipeline::Glyph;
    if (pipeline_ != want) {
        context_->IASetInputLayout(pos3 ? glyph3Layout_.Get() : glyphLayout_.Get());
        context_->VSSetShader(pos3 ? glyph3Vs_.Get() : glyphVs_.Get(), nullptr, 0);
        context_->PSSetShader(pos3 ? glyph3Ps_.Get() : glyphPs_.Get(), nullptr, 0);
        context_->VSSetConstantBuffers(2, 1, sceneCbuf_.GetAddressOf());
        context_->PSSetConstantBuffers(2, 1, sceneCbuf_.GetAddressOf());
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pipeline_ = want;
        ++stats_.programSwitches;
    }

    // Kameran, storlekens enhet, fältets mått och opaciteten — räknas inte
    // som uppladdning (RenderStats).
    SceneCb cb{};
    std::memcpy(cb.viewProj, d.uniforms.viewProj, sizeof cb.viewProj);
    cb.params[0] = static_cast<float>(targetW_);   // det BUNDNA målet
    cb.params[1] = static_cast<float>(targetH_);
    cb.params[2] = d.uniforms.pxPerWidthUnit;
    // Etiketternas bas (exakt under 2²⁴), −1 = ingen synlighet bunden.
    cb.params[3] = d.labels ? static_cast<float>(d.labelBase) : -1.0f;
    cb.paint0[0] = kSdfRefSize;
    cb.paint0[1] = static_cast<float>(kSdfRadius);
    cb.paint0[2] = kSyntheticBoldEm * kSdfRefSize;
    cb.paint1[0] = d.uniforms.glyphMinPx;   // golvet och taket
    cb.paint1[1] = std::clamp(d.uniforms.opacity, 0.0f, 1.0f);
    cb.paint1[3] = d.uniforms.glyphMaxPx;
    // Bildmarkörernas bild: proportionerna och flaggorna (Image-programmets
    // tal). En atlassida läses aldrig så — bara quadar med kImage gör det.
    cb.paint0[3] = d.image && d.image->height() > 0
                       ? static_cast<float>(d.image->width()) /
                             static_cast<float>(d.image->height())
                       : 1.0f;
    cb.paint1[2] = d.image ? sceneImageFlags(*d.image) : 0.0f;
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(context_->Map(sceneCbuf_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                             &m)))
        return;
    std::memcpy(m.pData, &cb, sizeof cb);
    context_->Unmap(sceneCbuf_.Get(), 0);
    if (!bindSceneClip(d))
        return;

    // Atlassidan (vit när chunken bara bär former — de samplar inte) med
    // den LINJÄRA samplern: ett avståndsfält ska interpoleras.
    ID3D11ShaderResourceView* srv =
        d.image ? static_cast<const D3dImage*>(d.image)->srv()
                : whiteSrv_.Get();
    ID3D11SamplerState* smp = samplerLinear_.Get();
    context_->PSSetShaderResources(0, 1, &srv);
    context_->PSSetSamplers(0, 1, &smp);
    // Etiketternas synlighet på vertexstegets t1 (vit när ingen är bunden —
    // shadern läser den då inte: basen är negativ).
    ID3D11ShaderResourceView* lsrv =
        d.labels ? static_cast<const D3dImage*>(d.labels)->srv()
                 : whiteSrv_.Get();
    context_->VSSetShaderResources(1, 1, &lsrv);
    applyMeshState(d.state);   // renderläget

    ID3D11Buffer* vb =
        static_cast<const D3dGpuBuffer*>(d.vertices.buffer)->buffer();
    ID3D11Buffer* ib =
        static_cast<const D3dGpuBuffer*>(d.indices.buffer)->buffer();
    const UINT stride = pos3 ? sizeof(GlyphVertex3) : sizeof(GlyphVertex);
    const UINT offset = static_cast<UINT>(d.vertices.offset);
    context_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    context_->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT,
                               static_cast<UINT>(d.indices.offset));
    context_->DrawIndexed(d.indexCount, 0, 0);
    ++stats_.drawCalls;
    stats_.vertices += d.indexCount;
}

} // namespace gfx
