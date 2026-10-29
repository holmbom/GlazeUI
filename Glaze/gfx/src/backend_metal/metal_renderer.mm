// MetalRenderer — gfx::Renderer ovanpå Metal. Spegeln av d3d11_renderer.cpp
// funktion för funktion: EN pipeline (allt är vertex med pos/uv/färg/SDF-
// parametrar/mode), batcher som flushar vid tillståndsbyte, retained-RT +
// composite (mode 5), scissor-klipp. MSL:en är en rad-för-rad-port av
// HLSL:en och kompileras i RUNTIME via newLibraryWithSource — exakt
// motsvarigheten till D3DCompile: inget byggsteg, ingen .metallib-kedja.
//
// Metal-skillnaderna mot D3D11:
//  * topologin är ett drawPrimitives-argument — curTopo_ är bara en
//    flush-gräns, ingen PSO-växling för line().
//  * map-DISCARD saknas — ring med tre delade vertexbuffertar + semafor
//    (trippelbuffring; CPU:n skriver aldrig i en buffert GPU:n läser).
//  * clear mitt i en pass saknas — encoder-bryt med loadAction=Clear
//    (målarordningen bevaras exakt; scissor/pipeline återsätts per start).
//  * scissor UTANFÖR målets bounds är ett valideringsfel (D3D11 klipper
//    tyst) — applyClip klampar obligatoriskt.
//  * nextDrawable kan blocka och ge nil (minimerat/ockluderat) — composite
//    skippar presenten då; retained-innehållet överlever.
//  * PSO:n BAKAR provtagningen, djupformatet och blandningen:
//    varje program har en variant per mål och blandning (`psoFor`),
//    och djupläget, gallringen och biasen är encoder-tillstånd som måste
//    sättas om efter varje encoder-brott (`startEncoder`).

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>

#include "metal_backend.h"
#include "gfx/platform.h"

namespace gfx {

namespace {

// Blandningen (gfx/scene.h `Blend`) mot ett PREMULTIPLICERAT mål — samma
// faktorer som D3D11:s och GL:s. DepthOnly skriver ingen färg.
void configureBlend(MTLRenderPipelineColorAttachmentDescriptor* ca, Blend b)
{
    ca.writeMask = b == Blend::DepthOnly ? MTLColorWriteMaskNone
                                         : MTLColorWriteMaskAll;
    ca.rgbBlendOperation   = MTLBlendOperationAdd;
    ca.alphaBlendOperation = MTLBlendOperationAdd;
    switch (b) {
    case Blend::Alpha:   // rak alfa — UI:ts och 2D-scenens
        ca.blendingEnabled             = YES;
        ca.sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
        ca.destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
        ca.sourceAlphaBlendFactor      = MTLBlendFactorOne;
        ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        break;
    case Blend::Premultiplied:
        ca.blendingEnabled             = YES;
        ca.sourceRGBBlendFactor        = MTLBlendFactorOne;
        ca.destinationRGBBlendFactor   = MTLBlendFactorOneMinusSourceAlpha;
        ca.sourceAlphaBlendFactor      = MTLBlendFactorOne;
        ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        break;
    case Blend::Additive:
        ca.blendingEnabled             = YES;
        ca.sourceRGBBlendFactor        = MTLBlendFactorSourceAlpha;
        ca.destinationRGBBlendFactor   = MTLBlendFactorOne;
        ca.sourceAlphaBlendFactor      = MTLBlendFactorOne;
        ca.destinationAlphaBlendFactor = MTLBlendFactorOne;
        break;
    case Blend::Opaque:
    case Blend::DepthOnly:
        ca.blendingEnabled = NO;
        break;
    }
}

MTLCompareFunction compareOf(DepthTest t)
{
    switch (t) {
    case DepthTest::Less:         return MTLCompareFunctionLess;
    case DepthTest::LessEqual:    return MTLCompareFunctionLessEqual;
    case DepthTest::Greater:      return MTLCompareFunctionGreater;
    case DepthTest::GreaterEqual: return MTLCompareFunctionGreaterEqual;
    case DepthTest::Equal:        return MTLCompareFunctionEqual;
    case DepthTest::Always:
    case DepthTest::Off:          break;
    }
    return MTLCompareFunctionAlways;
}

} // namespace

// ---------------------------------------------------------------- shaders

// KLIPPMASKERNAS DEL — delad av UI-pipelinens och scenens
// fragmentfunktioner (metal_scene.mm): varje program som ritar i ett mål med
// klippstacken MÅSTE ha den, annars gäller de rundade maskerna inte för det.
// [[position]] i fragmentsteget är pixelcentrum (x+0.5) —
// samma konvention som SV_Position, så sdRoundRect-anropen är oförändrade.
const char kMslClipSrc[] = R"msl(
#include <metal_stdlib>
using namespace metal;

// Avstånd till en rundad rektangel: p relativt centrum, b = halvmått,
// r = hörnradie. Negativt inuti, 0 på kanten, positivt utanför.
float sdRoundRect(float2 p, float2 b, float r)
{
    float2 q = abs(p) - b + r;
    return length(max(q, float2(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

// RUNDADE KLIPPMASKER — klippstackens
// rundade nivåer, bundna med setFragmentBytes. Konventionen är EXAKT
// SDF-vertexens: p0 = centrum.xy + halvmått.zw, p1.x = radien.
// Arraylängden MÅSTE matcha gfx::kMaxRoundedClips.
struct ClipMasks {
    float4 p0[4];
    float4 p1[4];
    float4 meta;   // .x = antal aktiva masker
};

// Produkten av täckningarna: 0 så fort EN mask är 0, 1 bara där alla är 1.
// meta.x == 0 (det vanliga fallet) hoppar över loopen.
float clipCoverage(float2 p, constant ClipMasks& cm)
{
    float cov = 1.0;
    const int n = (int)(cm.meta.x + 0.5);
    for (int i = 0; i < n; ++i) {
        const float d = sdRoundRect(p - cm.p0[i].xy, cm.p0[i].zw, cm.p1[i].x);
        cov *= saturate(0.5 - d);
    }
    return cov;
}

// MÅLARENS KLIPP (gfx::SceneClip) — kantplanen (nx, ny, d) i målets
// px, positivt inne, på fragmentbuffert 4 (`bindSceneClip`); bara scenens
// program läser dem. Arrayens längd MÅSTE matcha gfx::kMaxClipPlanes.
struct SceneClip {
    float4 planes[8];
    float4 meta;   // .x = antal plan, 0 = inget klipp
};

// HLSL:ns sceneClip rad för rad: närmaste plans avstånd + ½, och där
// täckningen är 0 KASTAS fragmentet — varken färg eller djup. Bara ur en
// fragmentfunktion.
float sceneClip(float2 p, constant SceneClip& sc)
{
    const int n = (int)(sc.meta.x + 0.5);
    if (n == 0)
        return 1.0;
    float d = 1.0e30;
    for (int i = 0; i < n; ++i)
        d = min(d, dot(sc.planes[i].xy, p) + sc.planes[i].z);
    const float cov = saturate(d + 0.5);
    if (cov <= 0.0)
        discard_fragment();
    return cov;
}
)msl";

// Rad-för-rad-port av kUiShaderSrc i d3d11_renderer.cpp; klippmaskernas del
// (ovan) klistras FÖRE.
static const char kUiMslSrc[] = R"msl(
struct VSIn {
    float2 pos  [[attribute(0)]];
    float2 uv   [[attribute(1)]];
    float4 col  [[attribute(2)]];
    float4 p0   [[attribute(3)]];
    float4 p1   [[attribute(4)]];
    float  mode [[attribute(5)]];
};
struct VSOut {
    float4 pos [[position]];
    float2 uv;
    float4 col;
    float4 p0;
    float4 p1;
    float  mode;
};

vertex VSOut vsMain(VSIn i [[stage_in]],
                    constant float4& vp [[buffer(1)]])
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

fragment float4 psMain(VSOut i [[stage_in]],
                       texture2d<float> tex0 [[texture(0)]],
                       sampler smp0 [[sampler(0)]],
                       constant ClipMasks& cm [[buffer(0)]])
{
    // mode är konstant över quaden — grenen är uniform per primitiv
    const int mode = (int)(i.mode + 0.5);
    // [[position]].xy är pixelcentrum i px — samma rymd som maskernas p0.
    const float clipCov = clipCoverage(i.pos.xy, cm);
    if (mode >= 5) {
        // Composite av retained-RT:n: färdig RGBA rakt av. Egen väg
        // eftersom mode 0 tolkar texturen som R8-alfamask (text/geometri).
        //
        // ALFA-KONTRAKTET: en mode 5-BILD i ritvägen (metal_image.mm) bär
        // RAK alfa, så barnklippet får bara röra alfakanalen — att
        // multiplicera hela RGBA hade mörknat AA-bandet dubbelt.
        // Compositens quad har count 0 per konstruktion (klippstacken är
        // balanserad och tom där), så raden är en no-op just för den.
        // Vertexfärgens ALFA är bildens OPACITET (Renderer::drawImage) och
        // rör också bara alfan; compositens quad bär full alfa
        // (samma rad som D3D11).
        float4 t = tex0.sample(smp0, i.uv);
        if (mode == 6) {
            // Ett render-måls bild bär PREMULTIPLICERAD färg (gfx/image.h)
            // — delad efter filtreringen, annars mörk frans.
            t.rgb = t.a > 0.0 ? t.rgb / t.a : float3(0.0);
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
        alpha = tex0.sample(smp0, i.uv).r;
    } else {
        // [[position]].xy = pixelcentrum i px — samma koordinatsystem som p0
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
)msl";

// ---------------------------------------------------------------- init

id<MTLLibrary> MetalRenderer::compileMsl(NSString* src,
                                         MTLCompileOptions* opts,
                                         NSError** err)
{
    const StatTimer klocka(stats_.compileNs);
    return [device_ newLibraryWithSource:src options:opts error:err];
}

bool MetalRenderer::createPipeline()
{
    @autoreleasepool {
        NSError*       err = nil;
        const std::string src = std::string(kMslClipSrc) + kUiMslSrc;
        id<MTLLibrary>    lib = compileMsl(@(src.c_str()), nil, &err);
        if (!lib) {
            // Spegel av D3DCompile-fel-loggen
            log("MSL-kompilering misslyckades: %s",
                err ? err.localizedDescription.UTF8String : "?");
            return false;
        }
        id<MTLFunction> vs = [lib newFunctionWithName:@"vsMain"];
        id<MTLFunction> ps = [lib newFunctionWithName:@"psMain"];
        if (!vs || !ps) {
            log("MSL: vsMain/psMain saknas i biblioteket");
            return false;
        }

        // Vertexformatet — motsvarigheten till input-layouten:
        // uchar4Normalized läser samma byteordning som R8G8B8A8_UNORM.
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        vd.attributes[0].format      = MTLVertexFormatFloat2;   // pos
        vd.attributes[0].offset      = 0;
        vd.attributes[0].bufferIndex = 0;
        vd.attributes[1].format      = MTLVertexFormatFloat2;   // uv
        vd.attributes[1].offset      = 8;
        vd.attributes[1].bufferIndex = 0;
        vd.attributes[2].format      = MTLVertexFormatUChar4Normalized;
        vd.attributes[2].offset      = 16;                      // rgba
        vd.attributes[2].bufferIndex = 0;
        vd.attributes[3].format      = MTLVertexFormatFloat4;   // p0
        vd.attributes[3].offset      = 20;
        vd.attributes[3].bufferIndex = 0;
        vd.attributes[4].format      = MTLVertexFormatFloat4;   // p1
        vd.attributes[4].offset      = 36;
        vd.attributes[4].bufferIndex = 0;
        vd.attributes[5].format      = MTLVertexFormatFloat;    // mode
        vd.attributes[5].offset      = 52;
        vd.attributes[5].bufferIndex = 0;
        vd.layouts[0].stride       = sizeof(Vertex);
        vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        // Funktionerna och vertexformatet — blandningen, provtagningen och
        // djupet sätter varianterna (`psoFor`). PSO:er är immutabla: rak alfa
        // (UI:t) och ingen blandning (transparent-compositen) är två
        // varianter, aldrig runtime-toggling à la OMSetBlendState.
        MTLRenderPipelineDescriptor* pd =
            [MTLRenderPipelineDescriptor new];
        pd.vertexFunction   = vs;
        pd.fragmentFunction = ps;
        pd.vertexDescriptor = vd;
        if (!registerProgram(Pipeline::Ui, pd, "UI"))
            return false;

        // Två samplers, ett per filter: Nearest för glyfer, rektanglar
        // och compositen (1:1 — skarpast så), Linear för bilder (förvalet).
        // Vilken som gäller är batch-tillstånd (setState/flush). Båda går
        // genom MIP-KEDJAN: Nearest närmaste nivå, Linear mellan
        // nivåerna; en textur utan kedja har bara nivå 0.
        MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
        sd.minFilter    = MTLSamplerMinMagFilterNearest;
        sd.magFilter    = MTLSamplerMinMagFilterNearest;
        sd.mipFilter    = MTLSamplerMipFilterNearest;
        sd.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
        samplerNearest_ = [device_ newSamplerStateWithDescriptor:sd];
        sd.minFilter    = MTLSamplerMinMagFilterLinear;
        sd.magFilter    = MTLSamplerMinMagFilterLinear;
        sd.mipFilter    = MTLSamplerMipFilterLinear;
        samplerLinear_  = [device_ newSamplerStateWithDescriptor:sd];

        // 1×1 vit R8-textur — alfa-masken för ofärgad geometri
        MTLTextureDescriptor* td = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                         width:1
                                        height:1
                                     mipmapped:NO];
        whiteTex_ = [device_ newTextureWithDescriptor:td];
        const std::uint8_t white = 255;
        [whiteTex_ replaceRegion:MTLRegionMake2D(0, 0, 1, 1)
                     mipmapLevel:0
                       withBytes:&white
                     bytesPerRow:1];

        // Ringen: tre delade buffertar + semafor med samma antal platser
        for (int i = 0; i < kRingSlots; ++i) {
            ringBufs_[i] =
                [device_ newBufferWithLength:kMaxVerts * sizeof(Vertex)
                                     options:MTLResourceStorageModeShared];
            if (!ringBufs_[i]) {
                log("Metal: ring-vertexbuffert %d kunde inte skapas", i);
                return false;
            }
        }
        ringSem_ = dispatch_semaphore_create(kRingSlots);
    }
    return true;
}

bool MetalRenderer::init(CAMetalLayer* layer, int w, int h, bool transparent)
{
    layer_       = layer;
    width_       = w;
    height_      = h;
    transparent_ = transparent;   // styr composite-vägen

    // Lagret fick GPU-preferensens enhet av GlazeView (makeBackingLayer).
    device_ = layer ? layer.device : nil;
    if (!device_)
        device_ = preferredMetalDevice();
    if (!device_) {
        log("Metal: ingen device (MTLCreateSystemDefaultDevice gav nil)");
        return false;
    }
    if (layer_ && !layer_.device)
        layer_.device = device_;
    deviceName_ = device_.name ? device_.name.UTF8String : "";
    adapterId_  = metalAdapterId(device_);   // kortets id
    logDevice(device_, adapterId_);
    // GPU-tiden ur commandbufferns GPUStartTime/GPUEndTime —
    // de finns från macOS 10.15, äldre än något backenden stöder.
    gpuTimingSupported_ = true;
    // Statiska buffertar i Shared på en GPU med delat minne, annars Managed
    // (Apples råd för data som CPU:n fyller och GPU:n läser).
    unifiedMemory_ = device_.hasUnifiedMemory;

    queue_ = [device_ newCommandQueue];
    if (!queue_) {
        log("Metal: kommando-kön kunde inte skapas");
        return false;
    }

    if (!createPipeline())
        return false;

    // Retained-RT:n. Misslyckas den ritas direkt i drawablen i stället
    // (beginFrame/composite degraderar) — appen lever, partiell omritning
    // uteblir bara.
    createRetained(w, h);

    // Scenens program (metal_scene.mm). Misslyckas det loggas det och
    // drawMesh ritar ingenting — UI:t påverkas inte. Image-programmet
    // (metal_image.mm) skapas efter det.
    initScene();
    initImageProgram();
    initFillProgram();   // ytorna (metal_fill.mm)
    initGlyphProgram();  // texten (metal_glyph.mm)

    batch_.reserve(kMaxVerts);
    return true;
}

void MetalRenderer::shutdownRenderer()
{
    // Ev. öppen frame avslutas utan present. Commandbuffers behåller
    // referenser till sina resurser tills de exekverat — ARC-nollningen
    // nedan är säker även med jobb i flykt.
    if (encoder_) {
        [encoder_ endEncoding];
        encoder_ = nil;
    }
    if (cmd_) {
        commitFrame();   // uppladdningspasset först, samma räknare som present
        cmd_ = nil;
    }
    finishUploadPass(nil);   // ett pass utan frame finns inte — försvar
    shutdownGlyphProgram();
    shutdownFillProgram();
    shutdownImageProgram();
    shutdownScene();   // scenens buffertar och program före device:n
    batch_.clear();
    curTex_        = nil;
    curTarget_     = nil;
    frameTarget_   = nil;
    drawable_      = nil;
    retainedTex_   = nil;
    retainedValid_ = false;
    hasClip_       = false;
    alphaBuf_      = nil;
    alphaMirror_.clear();
    alphaW_ = alphaH_ = 0;
    transparent_   = false;
    gpuSerial_          = 0;
    gpuTimingSupported_ = false;
    for (int i = 0; i < kRingSlots; ++i)
        ringBufs_[i] = nil;
    ringSem_    = nil;
    whiteTex_       = nil;
    samplerLinear_  = nil;
    samplerNearest_ = nil;
    psoCache_.clear();
    for (auto& desc : progDesc_)
        desc = nil;
    for (auto& perTest : depthStates_)
        for (auto& s : perTest)
            s = nil;
    curPso_      = nil;
    curProgram_  = 0;
    bound_       = nullptr;
    curSamples_  = 1;
    curDepthTex_ = nil;
    offscreen_   = false;
    queue_      = nil;
    device_     = nil;
    layer_      = nil;
}

bool MetalRenderer::createRetained(int w, int h)
{
    retainedTex_   = nil;
    retainedValid_ = false;   // ny yta = odefinierat innehåll tills composite
    if (!device_ || w <= 0 || h <= 0)
        return false;

    MTLTextureDescriptor* td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:(NSUInteger)w
                                    height:(NSUInteger)h
                                 mipmapped:NO];
    td.usage       = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModePrivate;
    retainedTex_   = [device_ newTextureWithDescriptor:td];
    if (!retainedTex_) {
        log("Retained-RT kunde inte skapas (%dx%d)", w, h);
        return false;
    }
    return true;
}

void MetalRenderer::resize(int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    if (w == width_ && h == height_)
        return;
    width_  = w;
    height_ = h;
    // Drawable-storleken sätts av fönstret (syncLayerGeometry) — här
    // återskapas bara retained-RT:n i nya storleken.
    createRetained(w, h);
}

// ---------------------------------------------------------------- encoder

// Startar en render-pass mot target och återsätter allt encoder-state
// (pipeline, viewport-uniformen, sampler, aktivt klipp) — encoder-byten
// (clear/composite) tappar annars scissorn.
//
// DJUPET: ett mål med djup (`curDepthTex_`, satt
// av bindTarget) får sitt djupattachment — rensat till 1 med färgen, annars
// laddat — och Store vid VARJE encoder-brott, för brotten (en blit, ett nytt
// mål mitt i en frame, ett nästlat mål) får inte tappa det ritade djupet. Ett
// MSAA-måls `target` är den provtagna ytan; upplösningen är ett eget pass när
// målet lämnas (`resolveTarget`, metal_target.mm).
void MetalRenderer::startEncoder(id<MTLTexture> target, MTLLoadAction load,
                                 MTLClearColor cc)
{
    if (!cmd_ || !target)
        return;
    MTLRenderPassDescriptor* pass =
        [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture     = target;
    pass.colorAttachments[0].loadAction  = load;
    pass.colorAttachments[0].clearColor  = cc;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    if (curDepthTex_) {
        pass.depthAttachment.texture    = curDepthTex_;
        pass.depthAttachment.loadAction = load == MTLLoadActionClear
                                              ? MTLLoadActionClear
                                              : MTLLoadActionLoad;
        pass.depthAttachment.clearDepth  = 1.0;   // det bortre planet
        pass.depthAttachment.storeAction = MTLStoreActionStore;
    }

    encoder_   = [cmd_ renderCommandEncoderWithDescriptor:pass];
    curTarget_ = target;
    curPso_    = nil;   // en ny encoder har ingen PSO
    setPso(psoFor(Pipeline::Ui, Blend::Alpha));
    pipeline_ = Pipeline::Ui;   // en ny encoder börjar alltid med UI-pipelinen
    // Renderläget: UI:ts — och framsidan, moturs sett från kameran
    // (gfx/scene.h), en gång per encoder.
    [encoder_ setFrontFacingWinding:MTLWindingCounterClockwise];
    applyUiState();
    // Viewport-uniformen (px → NDC i VS:en) — motsvarar cbuffer-mappen. Det
    // BUNDNA målets storlek: ett render-mål har sin egen.
    const float vp[4] = {static_cast<float>(targetW_),
                         static_cast<float>(targetH_), 0.0f, 0.0f};
    [encoder_ setVertexBytes:vp length:sizeof(vp) atIndex:1];
    // Samplern är batch-tillstånd och sätts av flush() vid varje ritning.
    // Klippmaskerna ÅTERBINDS här av samma skäl som scissorn återappliceras:
    // encoder-state försvinner vid clear/target-byte.
    bindClipMasks();
    applyScissor();
    // Målarens klipp binds av nästa scenanrop — den nya encodern
    // har inget.
    sceneClipBound_ = false;
}

void MetalRenderer::endEncoder()
{
    if (encoder_) {
        [encoder_ endEncoding];
        encoder_ = nil;
    }
}

// En blit ordnad mot rit-anropen (metal_backend.h) — uppladdningarnas Break-
// och Own-väg (`uploadPathFor`). MITT I EN FRAME bryts render-encodern: det
// som batchats ritas först, bliten läggs i framens commandbuffer och
// `endBlit` startar om encodern på samma mål med Load — samma väg som
// `clear` tar, och räknad i `RenderStats::uploadBreaks`. Utanför en frame:
// egen commandbuffer.
id<MTLBlitCommandEncoder> MetalRenderer::beginBlit()
{
    if (cmd_) {
        if (encoder_)
            ++stats_.uploadBreaks;   // passet bryts — RenderStats::uploadBreaks
        flush();
        blitTarget_ = curTarget_;
        endEncoder();
        return [cmd_ blitCommandEncoder];
    }
    blitCmd_ = [queue_ commandBuffer];
    return [blitCmd_ blitCommandEncoder];
}

void MetalRenderer::endBlit(id<MTLBlitCommandEncoder> blit)
{
    [blit endEncoding];
    if (cmd_) {
        if (blitTarget_)
            startEncoder(blitTarget_, MTLLoadActionLoad, MTLClearColor{});
        blitTarget_ = nil;
        return;
    }
    // Commandbuffern håller sina resurser (uppladdningsbufferten) vid liv
    // tills den körts.
    [blitCmd_ commit];
    blitCmd_ = nil;
}

// ---------------------------------------------------------------- frame

void MetalRenderer::beginFrame()
{
    if (!queue_ || cmd_)
        return;
    // Klippmaskerna nollställs vid framens början. Stacken är balanserad
    // per konstruktion, men en obalans är ett LOGGAT programfel och får
    // inte dessutom läcka en mask in i nästa frame. (bindClipMasks är en
    // no-op här — encodern skapas senare och binder själv.)
    clipMasks_ = ClipMaskBuf{};

    // FÖRSVARET: ett uppladdningspass som ingen väg skickade (alla fyra gör
    // det — metal_backend.h) hade låst kön, köat men aldrig inskickat. Det
    // skickas och väntas in: ingen räknare väntar på det, och dess staging
    // får inte skrivas över när slotten används igen.
    if (uploadCmd_) {
        if (!uploadLeftLogged_) {
            uploadLeftLogged_ = true;
            log("Metal: ett uppladdningspass stod kvar utan frame — det "
                "skickas nu (en väg som avslutar en frame glömde det)");
        }
        [finishUploadPass(nil) waitUntilCompleted];
    }

    // Vänta in en ledig ring-slot (GPU:n klar med den) — trippelbuffringen.
    // Väntan är presentationens (`RenderStats::presentNs`): GPU:n ligger
    // efter med tre frames.
    {
        const StatTimer klocka(stats_.presentNs);
        dispatch_semaphore_wait(ringSem_, DISPATCH_TIME_FOREVER);
    }
    ringIndex_  = (ringIndex_ + 1) % kRingSlots;
    ringOffset_ = 0;
    beginSceneFrame();   // slottens transienta ringar är fria nu

    cmd_ = [queue_ commandBuffer];
    ++frameSerial_;       // stämpelns frame (uploadPathFor)
    gpuSerial_ = gpuTiming_ ? frameSerial_ : 0;   // GPU-tiden
    targetW_ = width_;    // retained-RT:n eller drawablen — samma storlek
    targetH_ = height_;
    // Framens eget mål: ett prov, inget djup.
    bound_       = nullptr;
    curSamples_  = 1;
    curDepthTex_ = nil;
    // En obalanserad pushTarget i förra framen får inte peka hit.
    beginFrameTargets();

    if (retainedTex_) {
        // Innehållet ÖVERLEVER mellan frames — det ÄR retained-modellen
        frameTarget_ = retainedTex_;
        startEncoder(retainedTex_, MTLLoadActionLoad, MTLClearColor{});
    } else {
        // Degraderat läge: rita direkt i drawablen (hämtas TIDIGT här —
        // ingen retained att falla tillbaka på). retainedValid_ förblir
        // false = full omritning varje frame, som D3D11.
        {
            const StatTimer klocka(stats_.presentNs);
            drawable_ = [layer_ nextDrawable];
        }
        frameTarget_ = drawable_ ? drawable_.texture : nil;
        if (drawable_)
            startEncoder(drawable_.texture, MTLLoadActionDontCare,
                         MTLClearColor{});
    }
}

void MetalRenderer::composite()
{
    if (!cmd_)
        return;
    flush();   // allt callback-ritat ska in i retained-RT:n först
    if (!retainedTex_)
        return;   // degraderat — callbacket ritade redan mot drawablen

    endEncoder();

    // Callbacket har just ritat klart i RT:n — från och med nu bär den
    // senaste framens innehåll och nästa callback får rita partiellt.
    retainedValid_ = true;

    // Drawablen hämtas SENT (kan blocka ~1 s; nil vid minimerat/ockluderat
    // fönster) — nil ⇒ skippa presenten, retained-innehållet överlever.
    // Väntan är presentationens (`RenderStats::presentNs`). Ett DOLT fönster
    // hämtar ingen alls (`setPresenting`) och går samma väg som nil.
    if (presenting_) {
        const StatTimer klocka(stats_.presentNs);
        drawable_ = [layer_ nextDrawable];
    }
    if (!drawable_) {
        curTarget_   = nil;
        frameTarget_ = nil;
        return;
    }

    // Fullskärms-quad i mode 5 (RGBA rakt av). Den skriver HELA drawablen
    // varje present — det är det som gör loadAction=DontCare ok och
    // overlayen självstädande. Chrome/overlay fortsätter i samma encoder —
    // drawablen är nu framens eget mål för render-målen också.
    frameTarget_ = drawable_.texture;
    targetW_     = width_;
    targetH_     = height_;
    bound_       = nullptr;   // drawablen: ett prov, inget djup
    curSamples_  = 1;
    curDepthTex_ = nil;
    startEncoder(drawable_.texture, MTLLoadActionDontCare, MTLClearColor{});
    // NEAREST uttryckligen: 1:1, och en bild som ritats linjärt sist i
    // framen får inte bestämma hur fönstret samplas.
    setState(retainedTex_, MTLPrimitiveTypeTriangle, ImageFilter::Nearest);
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
        // alfa-blandning mot alfa-0-clear ger exakt det), och Core
        // Animation förväntar sig premultiplicerat för icke-opaka lager —
        // skriv quaden RAKT AV, utan blandning. PSO:er är immutabla:
        // blend av/på är TVÅ varianter, aldrig runtime-toggling. (Encodern
        // är ny här — UI-pipelinen är den bundna, så flush() binder inte om.)
        setPso(psoFor(Pipeline::Ui, Blend::Opaque));
        flush();
        setPso(psoFor(Pipeline::Ui, Blend::Alpha));
        return;
    }
    // (flushas av nästa tillståndsbyte eller av present)
}

void MetalRenderer::present()
{
    if (!cmd_)
        return;
    flush();
    endEncoder();

    // Hit-testens CPU-spegel blit:as i SAMMA commandbuffer — BARA
    // transparenta fönster (kostnaden hör till transparensen).
    const bool mirror = transparent_ && retainedTex_ != nil;
    if (mirror)
        updateAlphaMirror(cmd_);

    if (drawable_)
        [cmd_ presentDrawable:drawable_];
    // Ring-slotten släpps när GPU:n exekverat klart — framen och dess
    // uppladdningspass (`commitFrame`). Commandbuffern committas ALLTID
    // (även utan drawable), annars läcker semaforen.
    id<MTLCommandBuffer> cmd = commitFrame();
    cmd_         = nil;
    drawable_    = nil;
    curTarget_   = nil;
    frameTarget_ = nil;

    if (mirror) {
        // Läs ut alfa-byten efter att GPU:n skrivit bufferten (samma
        // synk-kostnadsmodell som D3D11:s staging-map — per konstruktion
        // bara för transparenta fönster).
        [cmd waitUntilCompleted];
        alphaW_ = width_;
        alphaH_ = height_;
        alphaMirror_.resize(static_cast<std::size_t>(alphaW_) * alphaH_);
        const auto* px = static_cast<const std::uint8_t*>(alphaBuf_.contents);
        const std::size_t n = alphaMirror_.size();
        for (std::size_t i = 0; i < n; ++i)
            alphaMirror_[i] = px[i * 4 + 3];   // BGRA8 → alfa i byte 3
    }
}

// FRAMENS INSKICK (metal_backend.h): uppladdningspasset — köat före framen
// (`enqueue`) — och framens commandbuffer, med samma completion handler på
// båda. Räknaren släpper ring-slotten när den SISTA av dem körts klart: kön
// kör dem i köordning, men att de också BLIR klara i den ordningen är inte
// dokumenterat, och slottens staging läses av passet.
id<MTLCommandBuffer> MetalRenderer::commitFrame()
{
    dispatch_semaphore_t    sem  = ringSem_;
    const auto              rest = std::make_shared<std::atomic<int>>(
        uploadCmd_ ? 2 : 1);
    MTLCommandBufferHandler klar = ^(id<MTLCommandBuffer>) {
        if (rest->fetch_sub(1) == 1 && sem)
            dispatch_semaphore_signal(sem);
    };
    finishUploadPass(klar);
    id<MTLCommandBuffer> cmd = cmd_;
    [cmd addCompletedHandler:klar];
    // GPU-TIDEN — framens commandbuffer, inte
    // uppladdningspassets. Svaret lämnas i brevlådan genom en egen referens
    // (metal_backend.h `gpuSerial_`).
    if (gpuSerial_ != 0) {
        const std::shared_ptr<GpuTimeBox> låda   = gpuTimes_;
        const std::uint64_t               serial = gpuSerial_;
        gpuSerial_                               = 0;
        [cmd addCompletedHandler:^(id<MTLCommandBuffer> klart) {
            const CFTimeInterval t0 = klart.GPUStartTime;
            const CFTimeInterval t1 = klart.GPUEndTime;
            if (t0 > 0.0 && t1 > t0)
                Renderer::addGpuTime(*låda, serial,
                                     static_cast<std::uint64_t>((t1 - t0) * 1.0e9));
        }];
    }
    [cmd commit];
    return cmd;
}

// Köa blit retained-RT → delad buffert (alfa-kanalen läses på CPU efter
// commit). Bufferten återanvänds tills storleken ändras.
void MetalRenderer::updateAlphaMirror(id<MTLCommandBuffer> cmd)
{
    const std::size_t bytes =
        static_cast<std::size_t>(width_) * height_ * 4;
    if (!alphaBuf_ || alphaBuf_.length < bytes) {
        alphaBuf_ = [device_ newBufferWithLength:bytes
                                         options:MTLResourceStorageModeShared];
        if (!alphaBuf_) {
            alphaW_ = alphaH_ = 0;   // degraderat läge → opak hit-test
            return;
        }
    }
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    [blit copyFromTexture:retainedTex_
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(retainedTex_.width,
                                             retainedTex_.height, 1)
                        toBuffer:alphaBuf_
               destinationOffset:0
          destinationBytesPerRow:static_cast<NSUInteger>(width_) * 4
        destinationBytesPerImage:bytes];
    [blit endEncoding];
}

// Readback (glaze-render): retained-RT:n → rå RGBA8 på CPU. Egen
// engångs-commandbuffer med blit + waitUntilCompleted — köordningen
// garanterar att framens committade ritning exekverat före bliten
// (capture EFTER flush/present per kontrakt). RT:n är BGRA8 ⇒ swizzle
// till fasadens RGBA. Rör inte ringen/semaforen (ingen vertexdata).
bool MetalRenderer::captureRetained(std::vector<std::uint8_t>& rgba,
                                    int& w, int& h)
{
    if (!queue_ || !retainedTex_ || !retainedValid_)
        return false;   // degraderat läge/ingen frame ritad än
    if (cmd_)
        return false;   // mitt i en öppen frame — anropa efter renderFrame

    const int tw = static_cast<int>(retainedTex_.width);
    const int th = static_cast<int>(retainedTex_.height);
    if (tw <= 0 || th <= 0)
        return false;
    const std::size_t bytes =
        static_cast<std::size_t>(tw) * static_cast<std::size_t>(th) * 4;

    @autoreleasepool {
        id<MTLBuffer> buf =
            [device_ newBufferWithLength:bytes
                                 options:MTLResourceStorageModeShared];
        if (!buf) {
            log("captureFrame: readback-bufferten kunde inte skapas");
            return false;
        }
        id<MTLCommandBuffer>      cmd  = [queue_ commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
        [blit copyFromTexture:retainedTex_
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(retainedTex_.width,
                                                 retainedTex_.height, 1)
                            toBuffer:buf
                   destinationOffset:0
              destinationBytesPerRow:static_cast<NSUInteger>(tw) * 4
            destinationBytesPerImage:bytes];
        [blit endEncoding];
        [cmd commit];
        [cmd waitUntilCompleted];

        rgba.resize(bytes);
        const auto* px = static_cast<const std::uint8_t*>(buf.contents);
        for (std::size_t i = 0; i < bytes; i += 4) {
            rgba[i]     = px[i + 2];   // BGRA8 → RGBA8
            rgba[i + 1] = px[i + 1];
            rgba[i + 2] = px[i];
            rgba[i + 3] = px[i + 3];
        }
    }
    w = tw;
    h = th;
    return true;
}

bool MetalRenderer::alphaHit(int x, int y) const
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

// ---------------------------------------------------------------- batcher

void MetalRenderer::flush()
{
    if (batch_.empty())
        return;
    if (!encoder_) {
        batch_.clear();   // ingen yta (nil-drawable) — släpp ritningen
        return;
    }
    // Ett drawMesh efter förra UI-ritningen har lämnat scenens PSO och
    // uniformer kvar — återbindningen sitter HÄR, där UI-pipelinen ritar.
    if (pipeline_ != Pipeline::Ui)
        bindUiPipeline();

    const std::size_t bytes = batch_.size() * sizeof(Vertex);
    if (bytes <= 4096) {
        // Små batchar: inline i kommandoströmmen — ingen buffert behövs
        [encoder_ setVertexBytes:batch_.data() length:bytes atIndex:0];
    } else if (ringOffset_ + bytes <=
               static_cast<std::size_t>(kMaxVerts) * sizeof(Vertex)) {
        auto* dst = static_cast<std::uint8_t*>(ringBufs_[ringIndex_].contents);
        std::memcpy(dst + ringOffset_, batch_.data(), bytes);
        [encoder_ setVertexBuffer:ringBufs_[ringIndex_]
                           offset:ringOffset_
                          atIndex:0];
        // 256-justering — säker offset-alignment för vertex-fetch
        ringOffset_ += (bytes + 255) & ~std::size_t{255};
    } else {
        // Ring-slotten full inom EN frame (extremfall) — engångsbuffert;
        // commandbuffern behåller referensen tills GPU:n är klar.
        id<MTLBuffer> tmp =
            [device_ newBufferWithBytes:batch_.data()
                                 length:bytes
                                options:MTLResourceStorageModeShared];
        [encoder_ setVertexBuffer:tmp offset:0 atIndex:0];
    }

    [encoder_ setFragmentTexture:(curTex_ ? curTex_ : whiteTex_) atIndex:0];
    // Batchens filter — vid VARJE ritning, så att scenens program (som
    // sätter sin egen sampler) aldrig bestämmer det.
    [encoder_ setFragmentSamplerState:samplerFor(curFilter_) atIndex:0];
    [encoder_ drawPrimitives:curTopo_
                 vertexStart:0
                 vertexCount:batch_.size()];
    countProgram(Pipeline::Ui);   // också efter ett encoder-brott (metal_backend.h)
    ++stats_.drawCalls;
    stats_.vertices += batch_.size();
    batch_.clear();
}

// UI-pipelinen tillbaka efter scenens program: PSO:n och viewporten på
// index 1 i vertexsteget (där scenen lade sina uniformer) — och
// renderläget (`applyUiState`). Klippmaskerna
// (fragmentbuffert 0) rör scenen aldrig; vertexdatan, texturen och samplern
// (Image-programmet sätter egna) sätter flush() vid varje ritning.
void MetalRenderer::bindUiPipeline()
{
    setPso(psoFor(Pipeline::Ui, Blend::Alpha));
    const float vp[4] = {static_cast<float>(targetW_),
                         static_cast<float>(targetH_), 0.0f, 0.0f};
    [encoder_ setVertexBytes:vp length:sizeof(vp) atIndex:1];
    applyUiState();
    pipeline_ = Pipeline::Ui;
}

// ------------------------------------------------- PSO-varianterna (3D)
//
// Nyckeln är (program, blandning,
// provtagning, djup). En variant som inte går att bygga cachas som nil — den
// loggas en gång och prövas inte om varje anrop.

bool MetalRenderer::registerProgram(Pipeline p, MTLRenderPipelineDescriptor* pd,
                                    const char* name)
{
    progDesc_[static_cast<int>(p)] = pd;
    if (psoVariant(p, Blend::Alpha, 1, false))
        return true;
    progDesc_[static_cast<int>(p)] = nil;
    log("Metal-PSO (%s) misslyckades — programmet ritar inget", name);
    return false;
}

id<MTLRenderPipelineState> MetalRenderer::psoFor(Pipeline p, Blend b)
{
    return psoVariant(p, b, curSamples_, curDepthTex_ != nil);
}

id<MTLRenderPipelineState> MetalRenderer::psoVariant(Pipeline p, Blend b,
                                                     int samples, bool depth)
{
    const std::uint32_t key = static_cast<std::uint32_t>(p) |
                              static_cast<std::uint32_t>(b) << 4 |
                              static_cast<std::uint32_t>(samples) << 8 |
                              (depth ? 1u << 16 : 0u);
    if (auto it = psoCache_.find(key); it != psoCache_.end())
        return it->second;
    MTLRenderPipelineDescriptor* base = progDesc_[static_cast<int>(p)];
    if (!base)
        return nil;
    const std::string what = "program " + std::to_string(static_cast<int>(p));
    id<MTLRenderPipelineState> pso =
        buildPso(base, b, samples, depth, what.c_str());
    psoCache_[key] = pso;
    return pso;
}

id<MTLRenderPipelineState> MetalRenderer::buildPso(
    MTLRenderPipelineDescriptor* base, Blend b, int samples, bool depth,
    const char* what)
{
    MTLRenderPipelineDescriptor* pd = [base copy];
    pd.rasterSampleCount               = static_cast<NSUInteger>(samples);
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    pd.depthAttachmentPixelFormat =
        depth ? MTLPixelFormatDepth32Float : MTLPixelFormatInvalid;
    configureBlend(pd.colorAttachments[0], b);
    NSError*                   err = nil;
    id<MTLRenderPipelineState> pso = nil;
    {
        // En variant byggs första gången den behövs — ofta mitt i en frame
        // (`RenderStats::compileNs`).
        const StatTimer klocka(stats_.compileNs);
        pso = [device_ newRenderPipelineStateWithDescriptor:pd error:&err];
    }
    if (!pso)
        log("Metal-PSO (%s, blandning %d, %d prov%s) misslyckades: %s", what,
            static_cast<int>(b), samples, depth ? ", djup" : "",
            err ? err.localizedDescription.UTF8String : "?");
    return pso;
}

void MetalRenderer::setPso(id<MTLRenderPipelineState> pso)
{
    if (!encoder_ || !pso || pso == curPso_)
        return;
    [encoder_ setRenderPipelineState:pso];
    curPso_ = pso;
}

// ---------------------------------------------------------- renderläget
//
// Se gfx/scene.h `RenderState`. Djupet verkar
// bara i ett mål med djup — utan ett djupattachment får encodern aldrig ett
// jämförande djupläge (PSO:n saknar då djupformatet).

id<MTLDepthStencilState> MetalRenderer::depthStateFor(DepthTest t, bool write)
{
    const int ti = static_cast<int>(t);
    const int wi = write && t != DepthTest::Off ? 1 : 0;
    if (!depthStates_[ti][wi]) {
        MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction = compareOf(t);   // Off: Always
        dd.depthWriteEnabled    = wi ? YES : NO;  // Off: ingen skrivning
        depthStates_[ti][wi]    = [device_ newDepthStencilStateWithDescriptor:dd];
    }
    return depthStates_[ti][wi];
}

void MetalRenderer::applyMeshState(const RenderState& s)
{
    const bool        depth = s.depth != DepthTest::Off && curDepthTex_ != nil;
    const MTLCullMode cull  = s.cull == Cull::Back    ? MTLCullModeBack
                              : s.cull == Cull::Front ? MTLCullModeFront
                                                      : MTLCullModeNone;
    [encoder_ setDepthStencilState:depthStateFor(
                                       depth ? s.depth : DepthTest::Off,
                                       s.depthWrite)];
    [encoder_ setCullMode:cull];
    [encoder_ setDepthBias:(depth ? s.depthBias : 0.0f)
                slopeScale:(depth ? s.slopeBias : 0.0f)
                     clamp:0.0f];
}

void MetalRenderer::applyUiState()
{
    if (!encoder_)
        return;
    [encoder_ setDepthStencilState:depthStateFor(DepthTest::Off, false)];
    [encoder_ setCullMode:MTLCullModeNone];
    [encoder_ setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
}

// ---------------------------------------------------- ritning utan frame
//
// Se renderer.h. Blocket har en EGEN command
// buffer i `cmd_` — då fungerar render-målens encoder-dans, blittarna, den
// transienta ringen och uppladdningarnas vägar (blocket är en
// egen "frame" för stämpeln, med ett eget uppladdningspass) som i en frame —
// men inget eget mål (`frameTarget_` nil), så ingenting ritas utanför ett
// render-mål. Ringslotten väntas inte in: blocket skriver efter förra
// framens del av slotten (den transienta ringen och stagingringen), och
// endOffscreen väntar in arbetet innan slotten kan återanvändas.
bool MetalRenderer::beginOffscreen()
{
    if (!queue_ || cmd_) {
        log("beginOffscreen: %s — blocket öppnas inte",
            !queue_     ? "ingen enhet"
            : offscreen_ ? "ett block är redan öppet"
                         : "inuti en frame");
        return false;
    }
    cmd_         = [queue_ commandBuffer];
    ++frameSerial_;   // blocket är en egen "frame" för stämpeln
    offscreen_   = true;
    encoder_     = nil;
    curTarget_   = nil;
    frameTarget_ = nil;
    bound_       = nullptr;
    curSamples_  = 1;
    curDepthTex_ = nil;
    targetW_     = width_;
    targetH_     = height_;
    pipeline_    = Pipeline::Ui;
    drawn_       = Pipeline::Ui;   // tyst — som framens början
    drawnSerial_ = 0;
    beginFrameTargets();   // en obalanserad stack töms
    return true;
}

void MetalRenderer::endOffscreen()
{
    if (!offscreen_) {
        log("endOffscreen utan beginOffscreen — ignoreras");
        return;
    }
    flush();
    endEncoder();
    // Uppladdningspasset (köat före blocket) skickas först, och båda väntas
    // in: readTarget ska se arbetet, och passets staging delar slot med
    // framen före — den får inte skrivas över under passet.
    id<MTLCommandBuffer> pass = finishUploadPass(nil);
    id<MTLCommandBuffer> cmd  = cmd_;
    [cmd commit];
    [cmd waitUntilCompleted];   // readTarget ser arbetet
    [pass waitUntilCompleted];
    cmd_         = nil;
    offscreen_   = false;
    curTarget_   = nil;
    bound_       = nullptr;
    curSamples_  = 1;
    curDepthTex_ = nil;
}

// Byte av textur, topologi eller FILTER bryter batchen — flusha först så
// ritordningen (bakifrån och fram) bevaras.
void MetalRenderer::setState(id<MTLTexture> tex, MTLPrimitiveType topo,
                             ImageFilter filter)
{
    if (tex != curTex_ || topo != curTopo_ || filter != curFilter_)
        flush();
    curTex_    = tex;
    curTopo_   = topo;
    curFilter_ = filter;
    if (batch_.size() + 6 > kMaxVerts)
        flush();
}

static std::uint32_t pack(Color c)
{
    // uchar4Normalized läser bytes i minnesordning r,g,b,a (little-endian)
    return static_cast<std::uint32_t>(c.r) |
           static_cast<std::uint32_t>(c.g) << 8 |
           static_cast<std::uint32_t>(c.b) << 16 |
           static_cast<std::uint32_t>(c.a) << 24;
}

void MetalRenderer::quad(const Rectf& d, const Rectf& uv, Color c)
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

void MetalRenderer::sdfQuad(const Rectf& d, Color c, float mode,
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

// Aktuellt klipp → encoderns scissor. Rundas UTÅT (delvis täckta pixlar
// ingår — fasadens kontrakt) och KLAMPAS mot målets bounds: Metal
// validerar scissor mot RT:n och kraschar på överskjut (D3D11 tolererar
// tyst — missas klampningen är symtomet en assert i debug-lagret).
void MetalRenderer::applyScissor()
{
    if (!encoder_ || !curTarget_)
        return;
    const auto W = static_cast<NSUInteger>(curTarget_.width);
    const auto H = static_cast<NSUInteger>(curTarget_.height);
    MTLScissorRect sc;
    if (!hasClip_) {
        sc = {0, 0, W, H};
    } else {
        const double cw = static_cast<double>(W);
        const double ch = static_cast<double>(H);
        const double x0 =
            std::clamp(std::floor(static_cast<double>(clip_.x)), 0.0, cw);
        const double y0 =
            std::clamp(std::floor(static_cast<double>(clip_.y)), 0.0, ch);
        const double x1 = std::clamp(
            std::ceil(static_cast<double>(clip_.x + clip_.w)), x0, cw);
        const double y1 = std::clamp(
            std::ceil(static_cast<double>(clip_.y + clip_.h)), y0, ch);
        sc = {static_cast<NSUInteger>(x0), static_cast<NSUInteger>(y0),
              static_cast<NSUInteger>(x1 - x0),
              static_cast<NSUInteger>(y1 - y0)};
    }
    [encoder_ setScissorRect:sc];
}

static float clampRadius(const Rectf& r, float radius);   // se rit-API:t

// MSL-arrayernas längd är skriven som en literal i shaderkällan — den kan
// inte läsa en C++-konstant, så låset står här i stället.
static_assert(kMaxRoundedClips == 4,
              "ClipMasks::p0/p1 i kMslClipSrc är deklarerade [4]");

// Lagrar maskerna och binder dem till fragmentshadern. UPPDELAD i två
// delar med flit: `setClipMasks` ÄGER värdet, `bindClipMasks` skickar det
// till encodern — och den senare måste anropas ÄVEN i `startEncoder`,
// eftersom encoder-state försvinner vid clear/target-byte. En mask som
// bara sattes i applyClip hade tyst tappats vid nästa encoder-bryt (samma
// fälla som `applyScissor` redan finns för).
void MetalRenderer::setClipMasks(const RoundedClip* masks, std::size_t count)
{
    clipMasks_ = ClipMaskBuf{};
    for (std::size_t i = 0; i < count && i < kMaxRoundedClips; ++i) {
        const Rectf& m = masks[i].rect;
        clipMasks_.p0[i][0] = m.x + m.w * 0.5f;   // centrum
        clipMasks_.p0[i][1] = m.y + m.h * 0.5f;
        clipMasks_.p0[i][2] = m.w * 0.5f;         // halvmått
        clipMasks_.p0[i][3] = m.h * 0.5f;
        // Samma klampning som fasadens övriga radier (halva kortsidan).
        clipMasks_.p1[i][0] = clampRadius(m, masks[i].radius);
    }
    clipMasks_.meta[0] = static_cast<float>(count);
    bindClipMasks();
}

void MetalRenderer::bindClipMasks()
{
    if (!encoder_)
        return;
    [encoder_ setFragmentBytes:&clipMasks_
                        length:sizeof(clipMasks_)
                       atIndex:0];
}

void MetalRenderer::applyClip(const Rectf* r, const RoundedClip* masks,
                              std::size_t count)
{
    // Scissor är encoder-state — allt som batchats före bytet måste ritas
    // med GAMLA klippet (samma mönster som textur-/topologibyte).
    flush();
    hasClip_ = (r != nullptr);
    if (r)
        clip_ = *r;
    // RUNDADE MASKER: lagras i clipMasks_ och binds med
    // setFragmentBytes. State:t måste ÅTERBINDAS där encodern skapas om
    // (startEncoder) — encoder-state försvinner vid clear/target-byte, och
    // en mask som bara sattes här hade tyst tappats.
    setClipMasks(masks, count);
    applyScissor();
}

// ---------------------------------------------------------------- rit-API

void MetalRenderer::clear(Color c)
{
    if (!cmd_ || !curTarget_)
        return;
    flush();   // allt ritat före clear ska bort — bevara ordningen
    // Metal saknar clear mitt i en pass — encoder-bryt med loadAction=
    // Clear på samma mål (sker i praktiken en gång per frame, först i
    // callbacket).
    // PREMULTIPLICERAD: varje mål bär premultiplicerat innehåll (rak-alfa-
    // blandning mot en genomskinlig yta ger exakt det), så rensfärgen
    // lagras i samma form. Opak och helt genomskinlig är oförändrade.
    const double   a      = c.a / 255.0;
    id<MTLTexture> target = curTarget_;
    endEncoder();
    startEncoder(target, MTLLoadActionClear,
                 MTLClearColorMake(c.r / 255.0 * a, c.g / 255.0 * a,
                                   c.b / 255.0 * a, a));
}

void MetalRenderer::fillRect(const Rectf& r, Color c)
{
    setState(nil, MTLPrimitiveTypeTriangle, ImageFilter::Nearest);
    quad(r, Rectf{0, 0, 0, 0}, c);   // uv 0 → vita texturens enda texel
}

void MetalRenderer::strokeRect(const Rectf& r, Color c)
{
    // Fyra 1-px-stavar istället för linjer — exakt pixeltäckning i hörnen
    fillRect(Rectf{r.x, r.y, r.w, 1.0f}, c);
    fillRect(Rectf{r.x, r.y + r.h - 1.0f, r.w, 1.0f}, c);
    fillRect(Rectf{r.x, r.y + 1.0f, 1.0f, r.h - 2.0f}, c);
    fillRect(Rectf{r.x + r.w - 1.0f, r.y + 1.0f, 1.0f, r.h - 2.0f}, c);
}

void MetalRenderer::line(float x1, float y1, float x2, float y2, Color c)
{
    setState(nil, MTLPrimitiveTypeLine, ImageFilter::Nearest);
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

void MetalRenderer::fillRoundedRect(const Rectf& r, float radius, Color c)
{
    setState(nil, MTLPrimitiveTypeTriangle, ImageFilter::Nearest);
    // Quaden 1 px större än rektangeln så AA-kanten inte klipps
    const Rectf q{r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f};
    sdfQuad(q, c, 1.0f, r, clampRadius(r, radius), 0.0f, 0.0f, 0.0f);
}

void MetalRenderer::strokeRoundedRect(const Rectf& r, float radius, Color c,
                                      float width)
{
    setState(nil, MTLPrimitiveTypeTriangle, ImageFilter::Nearest);
    const Rectf q{r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f};
    sdfQuad(q, c, 2.0f, r, clampRadius(r, radius), width, 0.0f, 0.0f);
}

void MetalRenderer::dropShadow(const Rectf& r, float radius,
                               float offsetX, float offsetY, float blur,
                               Color c)
{
    setState(nil, MTLPrimitiveTypeTriangle, ImageFilter::Nearest);
    // Skuggkällan är rektangeln förskjuten med offset; quaden täcker
    // hela uttoningen (blur åt båda hållen + AA-marginal)
    const Rectf s{r.x + offsetX, r.y + offsetY, r.w, r.h};
    const float b = blur < 0.5f ? 0.5f : blur;
    const Rectf q{s.x - b - 1.0f, s.y - b - 1.0f,
                  s.w + 2.0f * (b + 1.0f), s.h + 2.0f * (b + 1.0f)};
    sdfQuad(q, c, 3.0f, s, clampRadius(s, radius), b, 0.0f, 0.0f);
}

void MetalRenderer::innerShadow(const Rectf& r, float radius,
                                float offsetX, float offsetY, float blur,
                                Color c)
{
    setState(nil, MTLPrimitiveTypeTriangle, ImageFilter::Nearest);
    // Skuggan lever bara innanför r (shadern klipper mot rektangelns SDF)
    const Rectf q{r.x - 1.0f, r.y - 1.0f, r.w + 2.0f, r.h + 2.0f};
    sdfQuad(q, c, 4.0f, r, clampRadius(r, radius),
            blur < 0.5f ? 0.5f : blur, offsetX, offsetY);
}

float MetalRenderer::drawText(Font& f, float x, float y,
                              const std::string& utf8, Color c)
{
    // Fonten kommer alltid ur samma backend (loadFont/loadDefaultFont
    // nedan) — casten är säker per konstruktion.
    return static_cast<MetalFont&>(f).draw(*this, x, y, utf8, c);
}

void MetalRenderer::texturedQuad(id<MTLTexture> tex, const Rectf& dst,
                                 const Rectf& uv, Color c)
{
    // Glyferna är rasteriserade för hela pixlar och ritas 1:1 — NEAREST
    // uttryckligen, oberoende av vad en bild före dem samplades med.
    setState(tex, MTLPrimitiveTypeTriangle, ImageFilter::Nearest);
    quad(dst, uv, c);
}

// ---------------------------------------------------------------- fabriker

std::unique_ptr<Font> MetalRenderer::loadFont(const std::string& path,
                                              float sizePt)
{
    // Returnerar alltid ett objekt — valid() avgör om laddningen lyckades
    // (text uteblir men appen kör).
    auto font = std::make_unique<MetalFont>();
    font->setDevice(device_);
    font->load(path, sizePt);
    return font;
}

std::unique_ptr<Font> MetalRenderer::loadFont(FontFamily family, float sizePt,
                                              FontStyle style)
{
    auto font = std::make_unique<MetalFont>();
    font->setDevice(device_);
    font->loadFamily(family, sizePt, style);
    return font;
}

// Bild-fabrikerna + drawImage bor i metal_image.mm (Image I/O-vägen) —
// samma filfördelning som d3d11_image.cpp.

} // namespace gfx
