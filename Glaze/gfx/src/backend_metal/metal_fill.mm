// Fill-programmet i MetalRenderer —
// spegel av d3d11_fill.cpp (läs dess filhuvud: kantremsan, utåt, speglingen).
// Här bara
// Metal-skillnaderna:
//   * Uniformerna är `PaintU` (112 byte: matrisen, parametrarna och färgen)
//     på index 1 i båda stegen — som Line-programmet (det tar `PaintU` för
//     sin gradient) och där Image-programmet har sin kortare `SceneU`, så
//     `bindUiPipeline` återställer UI:ts viewport som vanligt.
//   * Klippmaskerna (fragmentbuffert 0) följer encodern; rampen och den
//     linjära samplern binds på index 0 per rit-anrop.
//   * [[position]].xy är pixelcentrum i målets px — maskernas rymd — också
//     där rasteriseraren klippt mot närplanet.

#include <algorithm>
#include <cstring>

#include "gfx/platform.h"
#include "metal_backend.h"

namespace gfx {

// FÄRGENS DEL — delad av Fill-, Line- och Glyph-programmen: uniformerna och
// `paintColor` (1 för enfärgat, rampens färg i världspunkten för en
// gradient, pad). Klistras efter klippmaskernas del.
const char kMslPaintSrc[] = R"msl(
struct PaintU {
    float4x4 viewProj;   // chunk-lokalt → pixelklipp
    float4   params;     // .xy målets storlek (px); .zw per program
    float4   paint0;     // linjär: från.xy, till.zw; radiell: mitt.xy, r0, r1
    float4   paint1;     // .x färgens slag (0 enfärgad, 1 linjär, 2 radiell),
                         // .y opaciteten, .zw per program
    float4   stroke;     // Line: .x spetsgränsen, .y strecken (antal slut,
                         // 0 = heldragen), .z mönsterenheter per
                         // världsenhet, .w mönstrets förskjutning
    float4   dash[2];    // Line: strecken som KUMULATIVA slut (på, av, …)
};

float4 paintColor(constant PaintU& u, texture2d<float> ramp, sampler smp,
                  float2 p)
{
    if (u.paint1.x < 0.5)
        return float4(1.0);
    float t;
    if (u.paint1.x < 1.5) {
        const float2 ab = u.paint0.zw - u.paint0.xy;
        t = dot(p - u.paint0.xy, ab) / max(dot(ab, ab), 1e-20);
    } else {
        t = (length(p - u.paint0.xy) - u.paint0.z) /
            max(u.paint0.w - u.paint0.z, 1e-20);
    }
    // Texelcentrum i ändarna: t = 0 och t = 1 är ändstoppens färg exakt.
    return ramp.sample(smp, float2((saturate(t) * 255.0 + 0.5) / 256.0, 0.5));
}
)msl";

namespace {

// Klippmaskernas och färgens delar klistras FÖRE.
const char kFillMslSrc[] = R"msl(
struct FillIn {
    float2 pos  [[attribute(0)]];
    float2 prev [[attribute(1)]];
    float2 next [[attribute(2)]];
    float4 col  [[attribute(3)]];
    uchar4 ks   [[attribute(4)]];   // .x FillKind (0 början, 1 slut), .y sidan
};

struct FillOut {
    float4 pos [[position]];
    float4 col;
    float  edge [[center_no_perspective]];   // 0 på kanten → 1 ytterkanten
    float2 world;                            // världspunkten (gradienten)
};

float4 fillToClip(constant PaintU& u, float2 p)
{
    return u.viewProj * float4(p, 0.0, 1.0);
}

float2 fillToPx(float4 c) { return c.xy / max(c.w, 1e-6); }

float4 fillToNdc(constant PaintU& u, float4 c)
{
    return float4(c.x * 2.0 / u.params.x - c.w, c.w - c.y * 2.0 / u.params.y,
                  c.z, c.w);
}

float4 fillToNear(float4 a, float4 b)
{
    return mix(a, b, float4(a.z / (a.z - b.z)));
}

float2 fillScreenDir(float4 a, float4 b)
{
    if (a.z < 0.0 && b.z < 0.0)
        return float2(0.0);
    if (a.z < 0.0)
        a = fillToNear(a, b);
    else if (b.z < 0.0)
        b = fillToNear(b, a);
    const float2 d = fillToPx(b) - fillToPx(a);
    const float  l = length(d);
    return l > 1e-6 ? d / l : float2(0.0);
}

float fillMirrorSign(constant PaintU& u)
{
    const float3x3 h = float3x3(u.viewProj[0].xyw, u.viewProj[1].xyw,
                                u.viewProj[3].xyw);
    return determinant(h) < 0.0 ? -1.0 : 1.0;
}

vertex FillOut fillVs(FillIn i [[stage_in]], constant PaintU& u [[buffer(1)]])
{
    FillOut o;
    o.col   = i.col;
    o.world = i.pos;
    o.edge  = 0.0;
    float4 c = fillToClip(u, i.pos);
    if (i.ks.y == 0) {
        o.pos = fillToNdc(u, c);   // på kanten: rasteriseraren klipper
        return o;
    }
    const bool   start = i.ks.x == 0;
    const float2 other = start ? i.next : i.prev;
    const float4 nb    = fillToClip(u, other);
    const float4 adj   = fillToClip(u, start ? i.prev : i.next);
    if (c.z < 0.0 && nb.z < 0.0) {
        o.pos = fillToNdc(u, c);   // hela kanten bakom närplanet
        return o;
    }
    const float2 e = start ? fillScreenDir(c, nb) : fillScreenDir(nb, c);
    float2       g = start ? fillScreenDir(adj, c) : fillScreenDir(c, adj);
    if (c.z < 0.0) {
        const float t = c.z / (c.z - nb.z);
        c       = mix(c, nb, float4(t));
        o.world = mix(i.pos, other, float2(t));
        g       = float2(0.0);
    }
    const float  s   = fillMirrorSign(u);
    const float2 ne  = s * float2(e.y, -e.x);   // utåt = höger om kanten
    const float2 ng  = s * float2(g.y, -g.x);
    float2       off = ne;
    const float2 m   = ne + ng;
    if (dot(g, g) > 0.0 && dot(m, m) > 1e-6) {
        const float2 mn = normalize(m);
        off             = mn / max(dot(mn, ne), 0.25);
    }
    o.pos  = fillToNdc(u, float4((fillToPx(c) + off) * c.w, c.z, c.w));
    o.edge = 1.0;
    return o;
}

fragment float4 fillPs(FillOut i [[stage_in]],
                       constant ClipMasks& cm [[buffer(0)]],
                       constant PaintU& u [[buffer(1)]],
                       constant SceneClip& sc [[buffer(4)]],
                       texture2d<float> ramp [[texture(0)]],
                       sampler smp [[sampler(0)]])
{
    const float4 col = i.col * paintColor(u, ramp, smp, i.world);
    const float  cov = 1.0 - saturate(i.edge);
    return float4(col.rgb, col.a * cov * u.paint1.y *
                               clipCoverage(i.pos.xy, cm) *
                               sceneClip(i.pos.xy, sc));
}
)msl";

} // namespace

void MetalRenderer::fillPaint(PaintU& pu, const MeshDraw& d)
{
    const ScenePaint& p = d.uniforms.paint;
    const bool radial   = p.kind == PaintKind::Radial;
    pu.paint0[0] = p.x0;
    pu.paint0[1] = p.y0;
    pu.paint0[2] = radial ? p.r0 : p.x1;
    pu.paint0[3] = radial ? p.r1 : p.y1;
    pu.paint1[0] = static_cast<float>(p.kind);
    pu.paint1[1] = std::clamp(d.uniforms.opacity, 0.0f, 1.0f);
}

void MetalRenderer::bindPaintRamp(const MeshDraw& d)
{
    // Rampen (eller den vita texturen när färgen är enfärgad — programmet
    // samplar den då inte) och den LINJÄRA samplern; UI:ts flush sätter
    // sina egna vid nästa ritning.
    id<MTLTexture> ramp =
        d.uniforms.paint.kind != PaintKind::Solid && d.image
            ? static_cast<const MetalImage*>(d.image)->texture()
            : whiteTex_;
    [encoder_ setFragmentTexture:ramp atIndex:0];
    [encoder_ setFragmentSamplerState:samplerLinear_ atIndex:0];
}

bool MetalRenderer::initFillProgram()
{
    @autoreleasepool {
        NSError*          err = nil;
        const std::string src =
            std::string(kMslClipSrc) + kMslPaintSrc + kFillMslSrc;
        id<MTLLibrary> lib = compileMsl(@(src.c_str()), nil, &err);
        if (!lib) {
            log("MSL (Fill) misslyckades: %s — drawMesh ritar inga ytor",
                err ? err.localizedDescription.UTF8String : "?");
            return false;
        }
        id<MTLFunction> vs = [lib newFunctionWithName:@"fillVs"];
        id<MTLFunction> fs = [lib newFunctionWithName:@"fillPs"];
        if (!vs || !fs) {
            log("MSL: fillVs/fillPs saknas i biblioteket");
            return false;
        }

        // FillVertex på sina fasta offset (gfx/scene.h).
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        vd.attributes[0].format = MTLVertexFormatFloat2;
        vd.attributes[0].offset = offsetof(FillVertex, x);
        vd.attributes[1].format = MTLVertexFormatFloat2;
        vd.attributes[1].offset = offsetof(FillVertex, prevX);
        vd.attributes[2].format = MTLVertexFormatFloat2;
        vd.attributes[2].offset = offsetof(FillVertex, nextX);
        vd.attributes[3].format = MTLVertexFormatUChar4Normalized;
        vd.attributes[3].offset = offsetof(FillVertex, color);
        vd.attributes[4].format = MTLVertexFormatUChar4;   // slag och sida
        vd.attributes[4].offset = offsetof(FillVertex, kind);
        for (NSUInteger a = 0; a < 5; ++a)
            vd.attributes[a].bufferIndex = 0;
        vd.layouts[0].stride       = sizeof(FillVertex);
        vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        // Funktionerna och vertexformatet; blandningen, provtagningen och
        // djupet sätter varianterna (`psoFor`, metal_renderer.mm).
        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction   = vs;
        pd.fragmentFunction = fs;
        pd.vertexDescriptor = vd;
        if (!registerProgram(Pipeline::Fill, pd, "Fill"))
            return false;
    }
    return true;
}

void MetalRenderer::shutdownFillProgram()
{
    progDesc_[static_cast<int>(Pipeline::Fill)] = nil;
}

void MetalRenderer::drawFillMesh(const MeshDraw& d)
{
    if (!encoder_ || !meshDrawOk(d, hasProgram(Pipeline::Fill)))
        return;

    flush();   // det UI:t batchat före anropet ritas först — målarordningen
    // PSO-varianten för målet och anropets blandning.
    id<MTLRenderPipelineState> pso = psoFor(Pipeline::Fill, d.state.blend);
    if (!pso)
        return;   // loggat när varianten byggdes
    if (pipeline_ != Pipeline::Fill) {
        pipeline_ = Pipeline::Fill;
        countProgram(Pipeline::Fill);
    }
    setPso(pso);

    // Kameran och färgen, i båda stegen — per anrop, räknas inte som
    // uppladdning (RenderStats).
    PaintU u{};
    std::memcpy(u.viewProj, d.uniforms.viewProj, sizeof u.viewProj);
    u.params[0] = static_cast<float>(targetW_);   // det BUNDNA målet — ett
    u.params[1] = static_cast<float>(targetH_);   // render-mål
    fillPaint(u, d);
    [encoder_ setVertexBytes:&u length:sizeof u atIndex:1];
    [encoder_ setFragmentBytes:&u length:sizeof u atIndex:1];
    bindSceneClip(d);
    bindPaintRamp(d);
    applyMeshState(d.state);   // renderläget

    const auto* vb = static_cast<const MetalGpuBuffer*>(d.vertices.buffer);
    const auto* ib = static_cast<const MetalGpuBuffer*>(d.indices.buffer);
    [encoder_ setVertexBuffer:vb->buffer() offset:d.vertices.offset atIndex:0];
    [encoder_ drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                         indexCount:d.indexCount
                          indexType:MTLIndexTypeUInt32
                        indexBuffer:ib->buffer()
                  indexBufferOffset:d.indices.offset];
    ++stats_.drawCalls;
    stats_.vertices += d.indexCount;
}

} // namespace gfx
