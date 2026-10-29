// Egna program i Metal-backenden (gfx/program.h) — spegel
// av d3d11_program.cpp.
// Appens MSL kompileras i körtid (newLibraryWithSource) med FÖRSPELET före
// och definitionerna som `preprocessorMacros`, och `MeshProgram::Custom`
// ritas med strömmarnas form per rit-anrop. Metal-skillnaderna:
//
//   * PSO:n BAKAR vertexdeskriptorn (layouten), blandningen, provtagningen och
//     djupformatet — varianten byggs med renderarens `buildPso` (samma som de
//     inbyggda programmens) och cachas I PROGRAMMET efter (layoutens
//     nyckel, blandning, prov, djup), också en som inte gick att bygga (nil,
//     loggad en gång).
//   * GlazeFrame och GlazeUser går med setVertexBytes/setFragmentBytes på
//     buffer(2) och buffer(3) — under 4 KB, därav GlazeUser-taket.
//     Strömmarna: buffer(0) vertex, buffer(1) instans, med OFFSET (ingen
//     baseInstance — [[instance_id]] räknar från delens början).
//   * Reflektionen är vertexfunktionens `vertexAttributes` (de aktiva
//     platserna och deras typ). GlazeUser-blockets storlek och samplerna
//     prövas INTE här — Metal reflekterar argumenten först i en PSO; dess
//     valideringslager säger till om en för liten buffert.

#include <algorithm>
#include <map>
#include <string>
#include <tuple>

#include "gfx/platform.h"
#include "metal_backend.h"

namespace gfx {
namespace {

// FÖRSPELET (gfx/program.h) — GlazeFrame som struct och glaze_toClip, som
// tar blocket som argument (MSL har inga globala block), och därefter
// färgjusteringen (`kMslAdjustSrc` nedan) och de skärmvända
// hörnens djup (`kMslStandSrc`). Efter förspelet
// står `#line 1 "<namn>"` (createProgram).
const char kMslPrelude[] = R"msl(
#include <metal_stdlib>
using namespace metal;
struct GlazeFrame
{
    float4x4 viewProj;
    float4x4 invViewProj;
    float4   eye;
    float4   forward;
    float4   right;
    float4   up;
    float4   target;
    float4   scale;
    float4   pixelSize;
};
// Kamerans pixelklipp (x/w och y/w i målets px, z/w i [0, 1]) → Metals
// klipprymd. Linjär — w går orört vidare till rasteriseringen.
inline float4 glaze_toClip(float4 c, constant GlazeFrame& glaze)
{
    return float4(c.x * 2.0 * glaze.target.z - c.w,
                  c.w - c.y * 2.0 * glaze.target.w, c.z, c.w);
}
)msl";

} // namespace

// FÄRGJUSTERINGEN (gfx/program.h) — förspelets andra del och
// Image-programmets (metal_image.mm), HLSL:ns kHlslAdjustSrc rad för rad.
// Ljusheten är c·k och kontrasten och mättnaden c·k + p·(1 − k), så (1, 1, 1)
// är EXAKT identiteten också när kompilatorn gör en fma av stegen.
const char kMslAdjustSrc[] = R"msl(
// Färgjusteringen av en RAK färg 0–1: a = (mättnad, kontrast, ljushet) —
// ljusheten multiplicerar, kontrasten sträcker kring mittgrått 0,5 och
// mättnaden blandar mot luminansen (Rec. 709), klampad efter varje steg.
inline float3 glaze_colorAdjust(float3 c, float3 a)
{
    c = saturate(c * a.z);
    c = saturate(c * a.y + 0.5 * (1.0 - a.y));
    const float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
    return saturate(c * a.x + lum * (1.0 - a.x));
}
)msl";

// DE SKÄRMVÄNDA HÖRNENS DJUP (gfx/program.h) — förspelets
// tredje del och Line-, Glyph- och Points-programmens, HLSL:ns kHlslStandSrc
// rad för rad. Matrisen är ett argument: scenens program har `u.viewProj`
// (PaintU), förspelets `glaze.viewProj`.
const char kMslStandSrc[] = R"msl(
// ETT SKÄRMVÄNT HÖRNS DJUP: hörnet `off` px från ankarets pixelklipp `c` får
// djupet av planet genom ankaret parallellt med xy-planet där det ligger
// närmare än ankaret, och draget framåt är en ANDEL av ankarets djup: helt
// upp till 1/4, inget från 1/2 och linjärt däremellan.
inline float glaze_standingZ(float4x4 m, float4 c, float2 off)
{
    if (c.w <= 0.0 || c.z > c.w)
        return c.z;
    const float4 mx  = m * float4(1.0, 0.0, 0.0, 0.0);
    const float4 my  = m * float4(0.0, 1.0, 0.0, 0.0);
    const float2 p   = c.xy / c.w + off;
    const float2 u2  = mx.xy - p * mx.w;
    const float2 v2  = my.xy - p * my.w;
    const float  det = u2.x * v2.y - u2.y * v2.x;
    if (abs(det) <= 1e-6 * length(u2) * length(v2))
        return c.z;
    const float2 r  = off * c.w;
    const float  a  = (r.x * v2.y - r.y * v2.x) / det;
    const float  b  = (u2.x * r.y - u2.y * r.x) / det;
    const float  wq = c.w + a * mx.w + b * my.w;
    if (wq <= 1e-6 * c.w)
        return c.z;
    const float zq   = (c.z + a * mx.z + b * my.z) / wq;
    const float z0   = c.z / c.w;
    const float drag = 1.0 - wq / c.w;
    const float k    = saturate((0.5 - drag) / 0.25);
    return mix(z0, min(z0, max(zq, 0.0)), k) * c.w;
}
)msl";

namespace {

MTLVertexFormat mtlFormatOf(AttribFormat f)
{
    switch (f) {
    case AttribFormat::Float:    return MTLVertexFormatFloat;
    case AttribFormat::Float2:   return MTLVertexFormatFloat2;
    case AttribFormat::Float3:   return MTLVertexFormatFloat3;
    case AttribFormat::Float4:   return MTLVertexFormatFloat4;
    case AttribFormat::UNorm8x4: return MTLVertexFormatUChar4Normalized;
    case AttribFormat::SNorm8x4: return MTLVertexFormatChar4Normalized;
    case AttribFormat::UInt8x4:  return MTLVertexFormatUChar4;
    case AttribFormat::UInt:     return MTLVertexFormatUInt;
    }
    return MTLVertexFormatInvalid;
}

bool integerType(MTLDataType t)
{
    switch (t) {
    case MTLDataTypeInt: case MTLDataTypeInt2: case MTLDataTypeInt3:
    case MTLDataTypeInt4: case MTLDataTypeUInt: case MTLDataTypeUInt2:
    case MTLDataTypeUInt3: case MTLDataTypeUInt4: case MTLDataTypeShort:
    case MTLDataTypeShort2: case MTLDataTypeShort3: case MTLDataTypeShort4:
    case MTLDataTypeUShort: case MTLDataTypeUShort2: case MTLDataTypeUShort3:
    case MTLDataTypeUShort4: case MTLDataTypeChar: case MTLDataTypeChar2:
    case MTLDataTypeChar3: case MTLDataTypeChar4: case MTLDataTypeUChar:
    case MTLDataTypeUChar2: case MTLDataTypeUChar3: case MTLDataTypeUChar4:
        return true;
    default:
        return false;
    }
}

bool layoutReads(const VertexLayout& l, unsigned stream)
{
    for (const VertexAttrib& a : l.attribs)
        if (a.stream == stream)
            return true;
    return false;
}

// Vertexdeskriptorn för layouten: attributen på sina platser, ström 0 per
// vertex och ström 1 per instans. nil för en tom layout (helskärmspasset).
MTLVertexDescriptor* vertexDescriptorOf(const VertexLayout& l)
{
    if (l.attribs.empty())
        return nil;
    MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
    for (const VertexAttrib& a : l.attribs) {
        vd.attributes[a.location].format      = mtlFormatOf(a.format);
        vd.attributes[a.location].offset      = a.offset;
        vd.attributes[a.location].bufferIndex = a.stream;
    }
    if (layoutReads(l, 0)) {
        vd.layouts[0].stride       = l.stride[0];
        vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
        vd.layouts[0].stepRate     = 1;
    }
    if (layoutReads(l, 1)) {
        vd.layouts[1].stride       = l.stride[1];
        vd.layouts[1].stepFunction = MTLVertexStepFunctionPerInstance;
        vd.layouts[1].stepRate     = 1;
    }
    return vd;
}

// Programmets löpnummer — aldrig återanvänt, till skillnad från en adress.
std::uint64_t nextProgramSerial()
{
    static std::uint64_t n = 0;
    return ++n;
}

// Programmet: funktionerna och PSO-varianterna. Cachen är `mutable`: att
// bygga en variant ändrar inte programmet, och `MeshDraw::shader` är const.
class MetalShaderProgram final : public ShaderProgram {
public:
    explicit MetalShaderProgram(const ProgramDesc& d)
        : ShaderProgram(d), serial(nextProgramSerial())
    {
    }

    bool               valid() const override { return vs != nil && fs != nil; }
    const std::string& error() const override { return err; }

    // Vertexstegets indata (`ShaderProgram::inputs`) — ur vertexAttributes.
    void reflected(std::uint32_t inputs, std::uint32_t integer)
    {
        setInputs(inputs, integer);
    }

    // (layoutens nyckel, blandning, provtagning, djup)
    using PsoKey = std::tuple<std::uint64_t, int, int, bool>;
    bool cached(const PsoKey& k, __strong id<MTLRenderPipelineState>& pso) const
    {
        auto it = psos_.find(k);
        if (it == psos_.end())
            return false;
        pso = it->second;
        return true;
    }
    void remember(const PsoKey& k, id<MTLRenderPipelineState> pso) const
    {
        psos_[k] = pso;
    }

    const std::uint64_t serial;
    std::string         err;
    id<MTLFunction>     vs = nil;
    id<MTLFunction>     fs = nil;

private:
    mutable std::map<PsoKey, id<MTLRenderPipelineState>> psos_;
};

std::string utf8(NSString* s)
{
    return s ? std::string(s.UTF8String) : std::string("?");
}

} // namespace

// ---------------------------------------------------------------- program

std::unique_ptr<ShaderProgram> MetalRenderer::createProgram(
    const ProgramDesc& d)
{
    auto        p   = std::make_unique<MetalShaderProgram>(d);
    std::string err = programDescError(d);
    if (err.empty() && d.msl.empty())
        err = programError(d, "saknar MSL-källa (macOS)");
    if (err.empty() && !device_)
        err = programError(d, "kunde inte skapas — renderaren har ingen enhet");

    id<MTLFunction> vs = nil, fs = nil;
    std::uint32_t   inputs = 0, integer = 0;
    if (err.empty()) {
        @autoreleasepool {
            // Förspelet, `#line 1 "<namn>"` och appens källa; definitionerna
            // går genom kompilatorn.
            const std::string src = std::string(kMslPrelude) + kMslAdjustSrc +
                                    kMslStandSrc + "#line 1 \"" +
                                    programSourceName(d) +
                                    "\"\n" + d.msl;
            NSString* source = [NSString stringWithUTF8String:src.c_str()];
            NSMutableDictionary<NSString*, NSObject*>* macros =
                [NSMutableDictionary dictionary];
            for (const auto& [namn, värde] : d.defines) {
                NSString* n = [NSString stringWithUTF8String:namn.c_str()];
                NSString* v = [NSString stringWithUTF8String:värde.c_str()];
                if (n && v)
                    macros[n] = v;
            }
            MTLCompileOptions* opts  = [MTLCompileOptions new];
            opts.preprocessorMacros  = macros;
            NSError*        nerr     = nil;
            id<MTLLibrary>  lib      = source ? compileMsl(source, opts, &nerr)
                                              : nil;
            if (!source) {
                err = programError(d, "har en MSL-källa som inte är giltig UTF-8");
            } else if (!lib) {
                err = programError(d, "kunde inte kompileras (MSL):\n" +
                                          utf8(nerr.localizedDescription));
            } else {
                vs = [lib newFunctionWithName:
                              [NSString stringWithUTF8String:d.vertexEntry.c_str()]];
                fs = [lib newFunctionWithName:
                              [NSString stringWithUTF8String:d.fragmentEntry.c_str()]];
                if (!vs || !fs)
                    err = programError(d, "saknar MSL-funktionen '" +
                                              (vs ? d.fragmentEntry
                                                  : d.vertexEntry) +
                                              "'");
            }
            // Reflektionen: de AKTIVA vertexattributen, platserna 0–15.
            if (err.empty()) {
                for (MTLVertexAttribute* a in vs.vertexAttributes) {
                    if (!a.active)
                        continue;
                    const NSUInteger at = a.attributeIndex;
                    if (at >= attrib::kCount) {
                        err = programError(
                            d, "läser [[attribute(" + std::to_string(at) +
                                   ")]] — platserna är 0–" +
                                   std::to_string(attrib::kCount - 1));
                        break;
                    }
                    inputs |= 1u << at;
                    if (integerType(a.attributeType))
                        integer |= 1u << at;
                }
            }
        }
    }

    if (!err.empty()) {
        p->err = err;
        log("%s", err.c_str());   // EN gång — här, aldrig per rit-anrop
        return p;
    }
    p->vs = vs;
    p->fs = fs;
    p->reflected(inputs, integer);
    ++stats_.programsBuilt;
    return p;
}

// ---------------------------------------------------------------- ritning

void MetalRenderer::drawCustomMesh(const MeshDraw& d)
{
    if (!encoder_ || !meshDrawOk(d, true))
        return;
    // Programmet kommer alltid ur den här backenden (createProgram) — casten
    // är säker per konstruktion, som drawText:s Font.
    const auto&         p = static_cast<const MetalShaderProgram&>(*d.shader);
    const VertexLayout& l = *d.layout;

    flush();   // det UI:t batchat före anropet ritas först — målarordningen
    // PSO-varianten för layouten, blandningen och det bundna målet.
    const MetalShaderProgram::PsoKey key{l.key(),
                                         static_cast<int>(d.state.blend),
                                         curSamples_, curDepthTex_ != nil};
    id<MTLRenderPipelineState> pso = nil;
    if (!p.cached(key, pso)) {
        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction               = p.vs;
        pd.fragmentFunction             = p.fs;
        pd.vertexDescriptor             = vertexDescriptorOf(l);
        const std::string what          = "programmet '" + p.name() + "'";
        pso = buildPso(pd, d.state.blend, curSamples_, curDepthTex_ != nil,
                       what.c_str());
        p.remember(key, pso);
    }
    if (!pso)
        return;   // loggat när varianten byggdes
    if (pipeline_ != Pipeline::Custom || curProgram_ != p.serial) {
        pipeline_   = Pipeline::Custom;
        curProgram_ = p.serial;
        countProgram(Pipeline::Custom, p.serial);
    }
    setPso(pso);

    // GlazeFrame: anroparens block med det BUNDNA målets storlek i `target`.
    FrameUniforms f = d.frame;
    f.target[0]     = static_cast<float>(targetW_);
    f.target[1]     = static_cast<float>(targetH_);
    f.target[2]     = targetW_ > 0 ? 1.0f / static_cast<float>(targetW_) : 0.0f;
    f.target[3]     = targetH_ > 0 ? 1.0f / static_cast<float>(targetH_) : 0.0f;
    [encoder_ setVertexBytes:&f length:sizeof f atIndex:2];
    [encoder_ setFragmentBytes:&f length:sizeof f atIndex:2];
    // GlazeUser — exakt programmets storlek (meshDrawOk har prövat den).
    if (d.userBytes > 0) {
        [encoder_ setVertexBytes:d.user length:d.userBytes atIndex:3];
        [encoder_ setFragmentBytes:d.user length:d.userBytes atIndex:3];
    }
    // Texturerna med var sin bilds filter, i båda stegen.
    for (int i = 0; i < p.textures(); ++i) {
        const auto& im = static_cast<const MetalImage&>(
            *d.textures[static_cast<std::size_t>(i)]);
        const NSUInteger at = static_cast<NSUInteger>(i);
        [encoder_ setVertexTexture:im.texture() atIndex:at];
        [encoder_ setFragmentTexture:im.texture() atIndex:at];
        [encoder_ setVertexSamplerState:samplerFor(im.filter()) atIndex:at];
        [encoder_ setFragmentSamplerState:samplerFor(im.filter()) atIndex:at];
    }
    applyMeshState(d.state);   // renderläget

    // Strömmarna — instansdelen MED OFFSET.
    if (layoutReads(l, 0))
        [encoder_
            setVertexBuffer:static_cast<const MetalGpuBuffer*>(d.vertices.buffer)
                                ->buffer()
                     offset:d.vertices.offset
                    atIndex:0];
    if (layoutReads(l, 1))
        [encoder_
            setVertexBuffer:static_cast<const MetalGpuBuffer*>(d.instances.buffer)
                                ->buffer()
                     offset:d.instances.offset
                    atIndex:1];

    const NSUInteger inst  = d.instanceCount;
    std::uint32_t    count = 0;
    if (d.indexCount > 0) {
        count = d.indexCount;
        id<MTLBuffer> ib =
            static_cast<const MetalGpuBuffer*>(d.indices.buffer)->buffer();
        if (inst > 0)
            [encoder_ drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                 indexCount:count
                                  indexType:MTLIndexTypeUInt32
                                indexBuffer:ib
                          indexBufferOffset:d.indices.offset
                              instanceCount:inst];
        else
            [encoder_ drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                 indexCount:count
                                  indexType:MTLIndexTypeUInt32
                                indexBuffer:ib
                          indexBufferOffset:d.indices.offset];
    } else {
        count = d.vertexCount;
        if (inst > 0)
            [encoder_ drawPrimitives:MTLPrimitiveTypeTriangle
                         vertexStart:0
                         vertexCount:count
                       instanceCount:inst];
        else
            [encoder_ drawPrimitives:MTLPrimitiveTypeTriangle
                         vertexStart:0
                         vertexCount:count];
    }
    ++stats_.drawCalls;
    stats_.vertices += static_cast<std::uint64_t>(count) *
                       std::max<std::uint32_t>(d.instanceCount, 1u);
    stats_.instances += d.instanceCount;
}

} // namespace gfx
