// Fill-programmet i D3dRenderer — ytorna ur
// `FillVertex` (gfx/scene.h): trianglarna i ytans inre ritas som de är, och
// kantremsans yttre vertex flyttas 1 fysisk px UTÅT i SKÄRMEN — kanten
// förblir en px vid varje zoom, bäring och tilt. Täckningen tonar från 1 på
// kanten till 0 i remsans ytterkant (`noperspective`, linjärt i skärmen).
//
// Lever bredvid Line- och Image-programmen och delar deras villkor
// (d3d11_scene.cpp): scenens b2 för uniformerna (`SceneCb` — se källan nedan
// för hur fälten läses här), b1 för klippmaskerna och t0/s0 för rampen, som
// binds per rit-anrop; `flush()` återbinder UI-pipelinen.
//
// UTÅT: ytan ligger till VÄNSTER om varje kant med y uppåt (`buildPolygon`
// vänder ringarna), så utåt är höger — och i skärmen speglat när kameran
// speglar planet. Tecknet läses ur matrisen: planets homografi (kolumnerna x,
// y och translationen, raderna x, y och w) har samma determinanttecken för
// varje punkt framför kameran.

#include <algorithm>
#include <cstring>

#include "d3d11_backend.h"
#include "gfx/platform.h"

namespace gfx {
namespace {

// Klippmaskernas del (`kHlslClipSrc`) och färgens (`kHlslPaintSrc`) klistras
// FÖRE.
const char kFillShaderSrc[] = R"hlsl(
struct FillIn
{
    float2 pos  : POSITION;
    float2 prev : TEXCOORD0;
    float2 next : TEXCOORD1;
    float4 col  : COLOR0;
    uint4  ks   : TEXCOORD2;   // .x FillKind (0 början, 1 slut), .y sidan
};

struct FillOut
{
    float4 pos   : SV_Position;
    float4 col   : COLOR0;
    // 0 på kanten → 1 i remsans ytterkant. NOPERSPECTIVE: remsan är
    // extruderad i skärmen, så tonen ska vara linjär i skärmen.
    noperspective float edge : TEXCOORD0;
    // Världspunkten (chunk-lokal) för gradienten — perspektivkorrekt.
    float2 world : TEXCOORD1;
};

float4 toClip(float2 p) { return mul(gViewProj, float4(p, 0.0, 1.0)); }

// Pixelklipp → målets px. Bara för punkter framför närplanet.
float2 toPx(float4 c) { return c.xy / max(c.w, 1e-6); }

// Pixelklipp → D3D:s klipprymd.
float4 toNdc(float4 c)
{
    return float4(c.x * 2.0 / gParams.x - c.w, c.w - c.y * 2.0 / gParams.y,
                  c.z, c.w);
}

// a flyttad längs a→b till närplanet (klippdjup 0).
float4 toNear(float4 a, float4 b) { return lerp(a, b, a.z / (a.z - b.z)); }

// Kantens riktning i skärmen från a till b, med den ände som ligger bakom
// närplanet flyttad till det; (0, 0) när hela kanten ligger bakom.
float2 screenDir(float4 a, float4 b)
{
    if (a.z < 0.0 && b.z < 0.0)
        return float2(0.0, 0.0);
    if (a.z < 0.0)
        a = toNear(a, b);
    else if (b.z < 0.0)
        b = toNear(b, a);
    const float2 d = toPx(b) - toPx(a);
    const float  l = length(d);
    return l > 1e-6 ? d / l : float2(0.0, 0.0);
}

// −1 när kameran speglar planet (yUp), annars +1 — se filhuvudet.
float mirrorSign()
{
    const float3x3 h = float3x3(gViewProj[0].xyw, gViewProj[1].xyw,
                                gViewProj[3].xyw);
    return determinant(h) < 0.0 ? -1.0 : 1.0;
}

FillOut fillVs(FillIn i)
{
    FillOut o;
    o.col   = i.col;
    o.world = i.pos;
    o.edge  = 0.0;
    float4 c = toClip(i.pos);
    if (i.ks.y == 0) {
        // På kanten: punkten som den är — rasteriseraren klipper mot
        // närplanet.
        o.pos = toNdc(c);
        return o;
    }

    // Remsans ytterkant. Kanten går mot `next` (början) eller kommer från
    // `prev` (slutet); grannkanten på andra sidan punkten ger geringen.
    const bool   start = i.ks.x == 0;
    const float2 other = start ? i.next : i.prev;
    const float4 nb    = toClip(other);
    const float4 adj   = toClip(start ? i.prev : i.next);
    if (c.z < 0.0 && nb.z < 0.0) {
        o.pos = toNdc(c);   // hela kanten bakom närplanet: klipps bort
        return o;
    }
    float2 e = start ? screenDir(c, nb) : screenDir(nb, c);
    float2 g = start ? screenDir(adj, c) : screenDir(c, adj);
    if (c.z < 0.0) {
        // Punkten bakom närplanet flyttas längs KANTEN till planet (kanten
        // har egna vertex) — grannkantens gering gäller inte där.
        const float t = c.z / (c.z - nb.z);
        c       = lerp(c, nb, t);
        o.world = lerp(i.pos, other, t);
        g       = float2(0.0, 0.0);
    }
    const float  s  = mirrorSign();
    const float2 ne = s * float2(e.y, -e.x);   // utåt = höger om kanten
    const float2 ng = s * float2(g.y, -g.x);
    float2       off = ne;
    const float2 m   = ne + ng;
    if (dot(g, g) > 0.0 && dot(m, m) > 1e-6) {
        // Geringen: 1 px vinkelrätt mot båda kanterna, högst 4 px lång.
        const float2 mn = normalize(m);
        off             = mn / max(dot(mn, ne), 0.25);
    }
    o.pos  = toNdc(float4((toPx(c) + off) * c.w, c.z, c.w));
    o.edge = 1.0;
    return o;
}

float4 fillPs(FillOut i) : SV_Target
{
    const float4 col = i.col * paintColor(i.world);
    const float  cov = 1.0 - saturate(i.edge);
    // Rak alfa som UI:t; klippmaskerna och målarens klipp gäller ytan som
    // allt annat.
    return float4(col.rgb, col.a * cov * gPaint1.y * clipCoverage(i.pos.xy) *
                               sceneClip(i.pos.xy));
}
)hlsl";

} // namespace

// FÄRGENS DEL — delad av Fill-, Line- och Glyph-programmen: scenens b2 med
// alla fält (programmen läser var sitt urval), rampen på t0 och den linjära
// samplern på s0. `paintColor(p)` är 1 för enfärgat och rampens färg i
// punkten för en gradient (pad: t klampas). De SKÄRMVÄNDA hörnens djup
// (Line i skärmläget, Line3, Glyph) är `glaze_standingZ` i `kHlslStandSrc`
// (d3d11_program.cpp), som Line- och Glyph-programmen klistrar in efter den
// här delen.
const char kHlslPaintSrc[] = R"hlsl(
cbuffer Scene : register(b2)
{
    float4x4 gViewProj;   // chunk-lokalt → pixelklipp (kolumnordning, mul(M, v))
    float4   gParams;     // .xy målets storlek (px); .zw per program
    float4   gPaint0;     // linjär: från.xy, till.zw; radiell: mitt.xy, r0, r1
    float4   gPaint1;     // .x färgens slag (0 enfärgad, 1 linjär, 2 radiell),
                          // .y opaciteten, .zw per program
    float4   gStroke;     // Line: .x spetsgränsen, .y strecken (antal slut,
                          // 0 = heldragen), .z mönsterenheter per
                          // världsenhet, .w mönstrets förskjutning
    float4   gDash[2];    // Line: strecken som KUMULATIVA slut (på, av, …)
}

Texture2D    gRamp   : register(t0);
SamplerState gLinear : register(s0);

float4 paintColor(float2 p)
{
    if (gPaint1.x < 0.5)
        return float4(1.0, 1.0, 1.0, 1.0);
    float t;
    if (gPaint1.x < 1.5) {
        const float2 ab = gPaint0.zw - gPaint0.xy;
        t = dot(p - gPaint0.xy, ab) / max(dot(ab, ab), 1e-20);
    } else {
        t = (length(p - gPaint0.xy) - gPaint0.z) /
            max(gPaint0.w - gPaint0.z, 1e-20);
    }
    // Texelcentrum i ändarna: t = 0 och t = 1 är ändstoppens färg exakt.
    return gRamp.Sample(gLinear, float2((saturate(t) * 255.0 + 0.5) / 256.0,
                                        0.5));
}
)hlsl";

void D3dRenderer::fillPaint(SceneCb& cb, const MeshDraw& d)
{
    const ScenePaint& p = d.uniforms.paint;
    cb.paint0[0] = p.x0;
    cb.paint0[1] = p.y0;
    cb.paint0[2] = p.kind == PaintKind::Radial ? p.r0 : p.x1;
    cb.paint0[3] = p.kind == PaintKind::Radial ? p.r1 : p.y1;
    cb.paint1[0] = static_cast<float>(p.kind);
    cb.paint1[1] = std::clamp(d.uniforms.opacity, 0.0f, 1.0f);
}

void D3dRenderer::bindPaintRamp(const MeshDraw& d)
{
    // Rampen (eller den vita texturen när färgen är enfärgad — programmet
    // samplar då inte) och den LINJÄRA samplern: UI-batchen binder sina
    // egna vid nästa Draw.
    ID3D11ShaderResourceView* srv =
        d.uniforms.paint.kind != PaintKind::Solid && d.image
            ? static_cast<const D3dImage*>(d.image)->srv()
            : whiteSrv_.Get();
    ID3D11SamplerState* smp = samplerLinear_.Get();
    context_->PSSetShaderResources(0, 1, &srv);
    context_->PSSetSamplers(0, 1, &smp);
}

bool D3dRenderer::initFillProgram()
{
    const std::string src =
        std::string(kHlslClipSrc) + kHlslPaintSrc + kFillShaderSrc;
    ComPtr<ID3DBlob> vsb = compileHlsl(src, "fillVs", "vs_4_0");
    ComPtr<ID3DBlob> psb = compileHlsl(src, "fillPs", "ps_4_0");

    // Layouten läser FillVertex på dess fasta offset (gfx/scene.h): slag +
    // sida (+ två byte utfyllnad) som R8G8B8A8_UINT.
    const D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         offsetof(FillVertex, x), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         offsetof(FillVertex, prevX), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0,
         offsetof(FillVertex, nextX), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0,
         offsetof(FillVertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R8G8B8A8_UINT, 0,
         offsetof(FillVertex, kind), D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    const bool ok =
        vsb && psb &&
        SUCCEEDED(device_->CreateVertexShader(vsb->GetBufferPointer(),
                                              vsb->GetBufferSize(), nullptr,
                                              &fillVs_)) &&
        SUCCEEDED(device_->CreatePixelShader(psb->GetBufferPointer(),
                                             psb->GetBufferSize(), nullptr,
                                             &fillPs_)) &&
        SUCCEEDED(device_->CreateInputLayout(il, 5, vsb->GetBufferPointer(),
                                             vsb->GetBufferSize(),
                                             &fillLayout_));
    if (!ok) {
        log("D3D11: Fill-programmet kunde inte skapas — drawMesh ritar inga "
            "ytor");
        shutdownFillProgram();
    }
    return ok;
}

void D3dRenderer::shutdownFillProgram()
{
    fillLayout_.Reset();
    fillPs_.Reset();
    fillVs_.Reset();
}

void D3dRenderer::drawFillMesh(const MeshDraw& d)
{
    if (!context_ || !meshDrawOk(d, fillVs_ != nullptr) || !sceneCbuf_)
        return;

    flush();   // det UI:t batchat före anropet ritas först — målarordningen
    if (pipeline_ != Pipeline::Fill) {
        context_->IASetInputLayout(fillLayout_.Get());
        context_->VSSetShader(fillVs_.Get(), nullptr, 0);
        context_->PSSetShader(fillPs_.Get(), nullptr, 0);
        context_->VSSetConstantBuffers(2, 1, sceneCbuf_.GetAddressOf());
        context_->PSSetConstantBuffers(2, 1, sceneCbuf_.GetAddressOf());
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pipeline_ = Pipeline::Fill;
        ++stats_.programSwitches;
    }

    // Kameran och färgen för just den här chunken — räknas inte som
    // uppladdning (RenderStats).
    SceneCb cb{};
    std::memcpy(cb.viewProj, d.uniforms.viewProj, sizeof cb.viewProj);
    cb.params[0] = static_cast<float>(targetW_);   // det BUNDNA målet — ett
    cb.params[1] = static_cast<float>(targetH_);   // render-mål
    fillPaint(cb, d);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(context_->Map(sceneCbuf_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                             &m)))
        return;
    std::memcpy(m.pData, &cb, sizeof cb);
    context_->Unmap(sceneCbuf_.Get(), 0);
    if (!bindSceneClip(d))
        return;
    bindPaintRamp(d);
    applyMeshState(d.state);   // renderläget

    ID3D11Buffer* vb =
        static_cast<const D3dGpuBuffer*>(d.vertices.buffer)->buffer();
    ID3D11Buffer* ib =
        static_cast<const D3dGpuBuffer*>(d.indices.buffer)->buffer();
    const UINT stride = sizeof(FillVertex);
    const UINT offset = static_cast<UINT>(d.vertices.offset);
    context_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    context_->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT,
                               static_cast<UINT>(d.indices.offset));
    context_->DrawIndexed(d.indexCount, 0, 0);
    ++stats_.drawCalls;
    stats_.vertices += d.indexCount;
}

} // namespace gfx
