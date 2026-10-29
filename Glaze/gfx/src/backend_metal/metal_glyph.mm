// Glyph-programmet i MetalRenderer — och dess
// Glyph3-variant (källan med GLAZE_POS3) — spegel av d3d11_glyph.cpp (läs dess filhuvud: billboards, fältet,
// formerna och den liggande texten,
// `glyphGround`). Här bara
// Metal-skillnaderna: uniformerna är `PaintU` på index 1 i båda stegen
// (params .z = px per storleksenhet, paint0 = fältets mått, paint1.y =
// opaciteten, paint1.x/.w storlekens golv och tak i px, 0 = ingen
// gräns), atlassidan och den linjära samplern binds på index 0 per
// rit-anrop, och flaggorna läses som `uint` direkt (MTLVertexFormatUInt).
// ETIKETTERNAS SYNLIGHET är VERTEXSTEGETS textur 0 (`read`, ingen
// sampler), indexet `label` ett `uint`-attribut och basen params.w — −1 när
// ingen synlighet är bunden.

#include <algorithm>
#include <cstring>

#include "gfx/platform.h"
#include "gfx/sdf.h"
#include "metal_backend.h"

namespace gfx {
namespace {

// Klippmaskernas och färgens delar (`kMslClipSrc`, `kMslPaintSrc`) klistras
// FÖRE. GLYPH3 är samma källa med GLAZE_POS3 — ankaret i
// xyz.
const char kGlyphMslSrc[] = R"msl(
#ifdef GLAZE_POS3
typedef float3 GlyphPos;
#else
typedef float2 GlyphPos;
#endif

struct GlyphIn {
    GlyphPos anchor [[attribute(0)]];
    float2 off    [[attribute(1)]];   // hörnet från ankaret, em (y nedåt)
    float2 uv     [[attribute(2)]];   // atlas-UV, eller formens läge (em)
    float4 col    [[attribute(3)]];
    float4 halo   [[attribute(4)]];
    float  size   [[attribute(5)]];   // em i enheten
    float  angle  [[attribute(6)]];   // radianer, moturs
    float  haloW  [[attribute(7)]];   // glorians/konturens bredd i enheten
    uint   flags  [[attribute(8)]];
    uint   label  [[attribute(9)]];   // etikettens index i bygget
};

struct GlyphOut {
    float4 pos [[position]];
    float2 uv;
    float4 col   [[flat]];
    float4 halo  [[flat]];
    float4 shape [[flat]];   // .xy formens halva mått (em), .z storleken i
                             // px, .w glorians bredd i em
    uint   flags [[flat]];
    float  vis   [[flat]];   // etikettens synlighet 0–1
};

// Världsriktningen v som skärmens riktning vid ankaret (pixelklippet c) —
// d3d11_glyph.cpp:s glyphScreenDir.
float2 glyphScreenDir(constant PaintU& u, float4 c, float3 v)
{
    const float4 d = u.viewProj * float4(v, 0.0);
    return (d.xy * c.w - c.xy * d.w) / (c.w * c.w);
}

// LIGGANDE TEXT (glyphflag::kGroundAligned) —
// d3d11_glyph.cpp:s glyphGround: hörnet i världsplanet vid ankaret, som
// pixelklipp.
float4 glyphGround(constant PaintU& u, GlyphPos anchor, float4 c, float angle,
                   float2 off, float sizePx, thread float& sizeEff)
{
    const float3 ew = float3(cos(angle), sin(angle), 0.0);
    float3       dw = float3(-ew.y, ew.x, 0.0);
    const float2 es = glyphScreenDir(u, c, ew);
    float2       ds = glyphScreenDir(u, c, dw);
    if (es.x * ds.y - es.y * ds.x < 0.0) {
        dw = -dw;
        ds = -ds;
    }
    float3 fw = float3((u.viewProj * float4(1.0, 0.0, 0.0, 0.0)).w,
                       (u.viewProj * float4(0.0, 1.0, 0.0, 0.0)).w,
                       (u.viewProj * float4(0.0, 0.0, 1.0, 0.0)).w);
    if (dot(fw, fw) < 1e-20)
        fw = float3((u.viewProj * float4(1.0, 0.0, 0.0, 0.0)).z,
                    (u.viewProj * float4(0.0, 1.0, 0.0, 0.0)).z,
                    (u.viewProj * float4(0.0, 0.0, 1.0, 0.0)).z);
    if (dot(fw, fw) < 1e-20)
        fw = float3(0.0, 0.0, 1.0);
    float3 tv = cross(fw, ew);
    if (dot(tv, tv) < 1e-20)
        tv = cross(fw, dw);
    const float k = length(glyphScreenDir(u, c, normalize(tv)));
    const float s = sizePx / max(k, 1e-12);   // världsenheter per em
    sizeEff       = sqrt(length(es) * length(ds)) * s;
    const float3 w = (off.x * ew + off.y * dw) * s;
#ifdef GLAZE_POS3
    return u.viewProj * float4(anchor + w, 1.0);
#else
    return u.viewProj * float4(anchor + w.xy, 0.0, 1.0);
#endif
}

vertex GlyphOut glyphVs(GlyphIn i [[stage_in]], constant PaintU& u [[buffer(1)]],
                        texture2d<float> labels [[texture(0)]])
{
    GlyphOut o;
    o.col   = i.col;
    o.halo  = i.halo;
    o.flags = i.flags;
    o.uv    = i.uv;
    o.shape = float4(0.0);
    // Synligheten: 1 utan bunden synlighet (basen negativ), annars texeln på
    // basen + indexet, rad för rad.
    o.vis = 1.0;
    if (u.params.w >= 0.0) {
        const uint w   = labels.get_width();
        const uint idx = uint(u.params.w + 0.5) + i.label;
        o.vis = labels.read(uint2(idx % w, idx / w)).r;
    }
#ifdef GLAZE_POS3
    const float4 c = u.viewProj * float4(i.anchor, 1.0);
#else
    const float4 c = u.viewProj * float4(i.anchor, 0.0, 1.0);
#endif
    if (c.z < 0.0 || c.w <= 1e-6 || o.vis <= 0.0) {
        // Bakom närplanet, eller dold av kollisionen.
        o.pos = float4(0.0, 0.0, -2.0, 1.0);
        return o;
    }
    float sizePx = i.size * u.params.z;
    // Golvet, sedan taket — taket vinner när de korsas.
    if (u.paint1.x > 0.0) sizePx = max(sizePx, u.paint1.x);
    if (u.paint1.w > 0.0) sizePx = min(sizePx, u.paint1.w);
    float2 e;
    if ((i.flags & 4u) != 0u) {
        const float4 d = u.viewProj * float4(cos(i.angle), sin(i.angle), 0.0, 0.0);
        const float2 s = (d.xy * c.w - c.xy * d.w) / (c.w * c.w);
        const float  l = length(s);
        e = l > 1e-12 ? s / l : float2(1.0, 0.0);
    } else {
        e = float2(cos(i.angle), -sin(i.angle));
    }
    const float2 down = float2(-e.y, e.x);
    float2 off = i.off;
    if ((i.flags & 3u) != 0u) {
        o.shape.xy      = abs(i.uv);
        const float  g  = 1.0 / max(sizePx, 1e-6);
        const float2 sg = sign(i.uv);
        off  += sg * g;
        o.uv += sg * g;
    }
    if ((i.flags & 32u) != 0u) {
        // En BILDMARKÖR: bildens proportioner kring punkten, och en bild med
        // raderna nerifrån vänds.
        const float aspect = u.paint0.w;   // bildens bredd / höjd
        off *= aspect >= 1.0 ? float2(1.0, 1.0 / aspect) : float2(aspect, 1.0);
        if ((uint(u.paint1.z + 0.5) & 2u) != 0u)
            o.uv.y = 1.0 - o.uv.y;
    }
    o.shape.z = sizePx;
    o.shape.w = i.size > 0.0 ? i.haloW / i.size : 0.0;
    if ((i.flags & 64u) != 0u) {
        // LIGGANDE: hörnet i världsplanet — glyphGround.
        float        eff = sizePx;
        const float4 g   = glyphGround(u, i.anchor, c, i.angle, off, sizePx, eff);
        o.shape.z = eff;
        o.pos = float4(g.x * 2.0 / u.params.x - g.w, g.w - g.y * 2.0 / u.params.y,
                       g.z, g.w);
        return o;
    }
    // STÅENDE: hörnets djup med planet genom ankaret (`glaze_standingZ`,
    // kMslStandSrc).
    const float2 o2 = (off.x * e + off.y * down) * sizePx;
    const float2 p  = c.xy / c.w + o2;
    const float4 q  =
        float4(p * c.w, glaze_standingZ(u.viewProj, c, o2), c.w);
    o.pos = float4(q.x * 2.0 / u.params.x - q.w, q.w - q.y * 2.0 / u.params.y,
                   q.z, q.w);
    return o;
}

fragment float4 glyphPs(GlyphOut i [[stage_in]],
                        constant ClipMasks& cm [[buffer(0)]],
                        constant PaintU& u [[buffer(1)]],
                        constant SceneClip& sc [[buffer(4)]],
                        texture2d<float> atlas [[texture(0)]],
                        sampler smp [[sampler(0)]])
{
    const uint  shape  = i.flags & 3u;
    const float sizePx = i.shape.z;
    float3 rgb;
    float  a;
    if ((i.flags & 32u) != 0u) {
        // BILDMARKÖREN: R8 som gråskala, ett måls premultiplicerade innehåll
        // tillbaka till rak alfa (u.paint1.z: flaggorna 4 och 1).
        float4     t = atlas.sample(smp, i.uv);
        const uint f = uint(u.paint1.z + 0.5);
        if ((f & 4u) != 0u)
            t = float4(t.rrr, 1.0);
        if ((f & 1u) != 0u)
            t.rgb = t.a > 0.0 ? t.rgb / t.a : float3(0.0);
        rgb = t.rgb;
        a   = t.a;
    } else if (shape == 0u) {
        float d = (atlas.sample(smp, i.uv).r - 0.5) * 2.0 * u.paint0.y;
        if ((i.flags & 8u) != 0u)
            d += u.paint0.z;                 // syntetisk fet
        const bool halo = (i.flags & 16u) != 0u;
        if (halo)
            d += i.shape.w * u.paint0.x;     // glorians bredd, em → texlar
        const float4 col = halo ? i.halo : i.col;
        rgb = col.rgb;
        a   = col.a * saturate(d * sizePx / u.paint0.x + 0.5);
    } else {
        const float dOut  = shape == 2u ? i.shape.x - length(i.uv)
                                        : min(i.shape.x - abs(i.uv.x),
                                              i.shape.y - abs(i.uv.y));
        const float outer = saturate(dOut * sizePx + 0.5);
        const float inner = saturate((dOut - i.shape.w) * sizePx + 0.5);
        // Under PLATTAN ligger konturen bara i bandet.
        const float fa = i.col.a * inner;
        const float ha = i.halo.a * (shape == 1u ? outer - inner
                                                  : outer * (1.0 - fa));
        a   = fa + ha;
        rgb = a > 0.0 ? (i.col.rgb * fa + i.halo.rgb * ha) / a : i.col.rgb;
    }
    return float4(rgb, a * i.vis * u.paint1.y * clipCoverage(i.pos.xy, cm) *
                           sceneClip(i.pos.xy, sc));
}
)msl";

} // namespace

namespace {

// GlyphVertex eller GlyphVertex3 på sina fasta offset (gfx/scene.h) — samma
// fält, ankaret med två eller tre flyttal.
template <class V>
MTLVertexDescriptor* glyphDescriptor(MTLVertexFormat anchor)
{
    MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
    const struct {
        MTLVertexFormat fmt;
        std::size_t     off;
    } attrs[] = {
        {anchor, offsetof(V, ax)},
        {MTLVertexFormatFloat2, offsetof(V, ox)},
        {MTLVertexFormatFloat2, offsetof(V, u)},
        {MTLVertexFormatUChar4Normalized, offsetof(V, color)},
        {MTLVertexFormatUChar4Normalized, offsetof(V, halo)},
        {MTLVertexFormatFloat, offsetof(V, size)},
        {MTLVertexFormatFloat, offsetof(V, angle)},
        {MTLVertexFormatFloat, offsetof(V, haloWidth)},
        {MTLVertexFormatUInt, offsetof(V, flags)},
        {MTLVertexFormatUInt, offsetof(V, label)},
    };
    for (NSUInteger a = 0; a < 10; ++a) {
        vd.attributes[a].format      = attrs[a].fmt;
        vd.attributes[a].offset      = attrs[a].off;
        vd.attributes[a].bufferIndex = 0;
    }
    vd.layouts[0].stride       = sizeof(V);
    vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
    return vd;
}

} // namespace

bool MetalRenderer::initGlyphProgram()
{
    // Glyph och dess Glyph3-variant: samma källa, Glyph3
    // med GLAZE_POS3 först i den. Glyph3 får misslyckas för sig (loggat).
    for (const bool pos3 : {false, true}) {
        @autoreleasepool {
            const char*       name = pos3 ? "Glyph3" : "Glyph";
            NSError*          err  = nil;
            const std::string src  = std::string(pos3 ? "#define GLAZE_POS3 1\n"
                                                      : "") +
                                    kMslClipSrc + kMslPaintSrc + kMslStandSrc +
                                    kGlyphMslSrc;
            id<MTLLibrary> lib = compileMsl(@(src.c_str()), nil, &err);
            if (!lib) {
                log("MSL (%s) misslyckades: %s — drawMesh ritar ingen text",
                    name, err ? err.localizedDescription.UTF8String : "?");
                if (pos3)
                    continue;
                return false;
            }
            id<MTLFunction> vs = [lib newFunctionWithName:@"glyphVs"];
            id<MTLFunction> fs = [lib newFunctionWithName:@"glyphPs"];
            if (!vs || !fs) {
                log("MSL: glyphVs/glyphPs saknas i biblioteket (%s)", name);
                if (pos3)
                    continue;
                return false;
            }

            // Funktionerna och vertexformatet; blandningen, provtagningen och
            // djupet sätter varianterna (`psoFor`, metal_renderer.mm).
            MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
            pd.vertexFunction   = vs;
            pd.fragmentFunction = fs;
            pd.vertexDescriptor =
                pos3 ? glyphDescriptor<GlyphVertex3>(MTLVertexFormatFloat3)
                     : glyphDescriptor<GlyphVertex>(MTLVertexFormatFloat2);
            if (!registerProgram(pos3 ? Pipeline::Glyph3 : Pipeline::Glyph, pd,
                                 name) &&
                !pos3)
                return false;
        }
    }
    return true;
}

void MetalRenderer::shutdownGlyphProgram()
{
    progDesc_[static_cast<int>(Pipeline::Glyph)]  = nil;
    progDesc_[static_cast<int>(Pipeline::Glyph3)] = nil;
}

void MetalRenderer::drawGlyphMesh(const MeshDraw& d)
{
    // Glyph och Glyph3 — samma källa, var sin PSO-familj.
    const Pipeline want =
        d.program == MeshProgram::Glyph3 ? Pipeline::Glyph3 : Pipeline::Glyph;
    if (!encoder_ || !meshDrawOk(d, hasProgram(want)))
        return;

    flush();   // det UI:t batchat före anropet ritas först — målarordningen
    // PSO-varianten för målet och anropets blandning.
    id<MTLRenderPipelineState> pso = psoFor(want, d.state.blend);
    if (!pso)
        return;   // loggat när varianten byggdes
    if (pipeline_ != want) {
        pipeline_ = want;
        countProgram(want);
    }
    setPso(pso);

    PaintU u{};
    std::memcpy(u.viewProj, d.uniforms.viewProj, sizeof u.viewProj);
    u.params[0] = static_cast<float>(targetW_);   // det BUNDNA målet
    u.params[1] = static_cast<float>(targetH_);
    u.params[2] = d.uniforms.pxPerWidthUnit;
    // Etiketternas bas (exakt under 2²⁴), −1 = ingen synlighet bunden.
    u.params[3] = d.labels ? static_cast<float>(d.labelBase) : -1.0f;
    u.paint0[0] = kSdfRefSize;
    u.paint0[1] = static_cast<float>(kSdfRadius);
    u.paint0[2] = kSyntheticBoldEm * kSdfRefSize;
    u.paint1[0] = d.uniforms.glyphMinPx;   // golvet och taket
    u.paint1[1] = std::clamp(d.uniforms.opacity, 0.0f, 1.0f);
    u.paint1[3] = d.uniforms.glyphMaxPx;
    // Bildmarkörernas proportioner och flaggor (Image-programmets tal) — en
    // atlassida läses aldrig så, bara quadar med kImage gör det.
    u.paint0[3] = d.image && d.image->height() > 0
                      ? static_cast<float>(d.image->width()) /
                            static_cast<float>(d.image->height())
                      : 1.0f;
    u.paint1[2] = d.image ? sceneImageFlags(*d.image) : 0.0f;
    [encoder_ setVertexBytes:&u length:sizeof u atIndex:1];
    [encoder_ setFragmentBytes:&u length:sizeof u atIndex:1];
    bindSceneClip(d);

    // Atlassidan (vit när chunken bara bär former) med den LINJÄRA
    // samplern — ett avståndsfält ska interpoleras.
    id<MTLTexture> tex =
        d.image ? static_cast<const MetalImage*>(d.image)->texture() : whiteTex_;
    [encoder_ setFragmentTexture:tex atIndex:0];
    [encoder_ setFragmentSamplerState:samplerLinear_ atIndex:0];
    // Etiketternas synlighet i vertexsteget (vit när ingen är bunden —
    // shadern läser den då inte, men funktionen deklarerar texturen).
    id<MTLTexture> lbl =
        d.labels ? static_cast<const MetalImage*>(d.labels)->texture()
                 : whiteTex_;
    [encoder_ setVertexTexture:lbl atIndex:0];
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
