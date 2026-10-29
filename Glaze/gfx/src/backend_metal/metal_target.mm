// Render-målen i Metal-backenden. Spegel av d3d11_target.cpp; här
// bara Metal-skillnaderna:
//   * Att byta mål är ett ENCODER-BROTT: det som batchats ritas, encodern
//     avslutas och en ny startas på målet med Load — så det som redan ritats
//     i målet (och i framens eget mål när man återvänder) står kvar.
//     `startEncoder` binder om allt encoder-tillstånd, viewport-uniformen
//     ur målets storlek (`targetW_/targetH_`), och scissorn klampas mot det
//     nya målet.
//   * Formatet är BGRA8Unorm, retained-RT:ns — PSO:erna är byggda för det.
//   * Metal har rad 0 överst i alla texturer: målets bild är rättvänd.
//   * DJUP OCH MSAA: en 2DMultisample-textur att rita i
//     och Depth32Float med samma provtagning. Varje encoder-brott lagrar
//     (Store) både färgen och djupet; när målet LÄMNAS löser ett eget, tomt
//     pass upp färgen till bilden (`resolveTarget`) — med Store kvar om målet
//     ligger kvar i stacken (ett nästlat mål ovanpå, man kommer tillbaka),
//     annars bara upplösningen och djupet DontCare. Encoderns attachments
//     (`curSamples_`, `curDepthTex_`) väljer PSO-varianten.

#include "../image_util.h"
#include "gfx/platform.h"
#include "metal_backend.h"

namespace gfx {

int MetalRenderer::supportedSamples(int requested) const
{
    // Den närmaste ≤ det begärda som enheten provtar (8, 4, 2). Apple silicon
    // har 4 som tak, äldre Mac-GPU:er 8.
    if (!device_)
        return 1;
    for (int n : {8, 4, 2})
        if (n <= requested &&
            [device_ supportsTextureSampleCount:static_cast<NSUInteger>(n)])
            return n;
    return 1;
}

std::unique_ptr<RenderTarget> MetalRenderer::createRenderTarget(
    int w, int h, const TargetDesc& d)
{
    if (!device_ || !queue_ || !targetDescOk(w, h, d))
        return std::make_unique<MetalRenderTarget>();
    const int samples = supportedSamples(d.samples);
    if (samples < d.samples && !samplesLogged_) {
        samplesLogged_ = true;
        log("createRenderTarget: %d prov stöds inte av enheten — %d", d.samples,
            samples);
    }

    // Bilden — ett prov, läsbar för ritvägarna och upplösningens mål.
    // Med MIP-KEDJA ritas det i nivå 0 (passens förval) och
    // kedjan räknas om när målet lämnas (bindTarget).
    MTLTextureDescriptor* td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:static_cast<NSUInteger>(w)
                                    height:static_cast<NSUInteger>(h)
                                 mipmapped:d.mipmaps ? YES : NO];
    td.usage       = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> tex = [device_ newTextureWithDescriptor:td];

    // Den provtagna ytan (bara med MSAA) och djupet, med samma provtagning.
    MTLTextureDescriptor* md = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:static_cast<NSUInteger>(w)
                                    height:static_cast<NSUInteger>(h)
                                 mipmapped:NO];
    md.textureType = samples > 1 ? MTLTextureType2DMultisample
                                 : MTLTextureType2D;
    md.sampleCount = static_cast<NSUInteger>(samples);
    md.usage       = MTLTextureUsageRenderTarget;
    md.storageMode = MTLStorageModePrivate;
    id<MTLTexture> msaa = samples > 1 ? [device_ newTextureWithDescriptor:md]
                                      : nil;
    id<MTLTexture> depth = nil;
    if (d.depth) {
        md.pixelFormat = MTLPixelFormatDepth32Float;
        depth          = [device_ newTextureWithDescriptor:md];
    }
    if (!tex || (samples > 1 && !msaa) || (d.depth && !depth)) {
        log("createRenderTarget: %d × %d (%d prov%s) kunde inte skapas", w, h,
            samples, d.depth ? ", djup" : "");
        return std::make_unique<MetalRenderTarget>();
    }

    // Nollställt (fasadens löfte): ett pass med Clear på BILDEN — den kan
    // ritas innan målet någonsin bundits; den provtagna ytan och djupet
    // rensas av pushTarget. Mitt i en frame i framens commandbuffer med ett
    // encoder-brott — som `beginBlit` — annars i en egen.
    MTLRenderPassDescriptor* pass =
        [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture     = tex;
    pass.colorAttachments[0].loadAction  = MTLLoadActionClear;
    pass.colorAttachments[0].clearColor  = MTLClearColorMake(0, 0, 0, 0);
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    // Kedjan ur den rensade nivå 0 i samma commandbuffer — bilden
    // kan ritas innan målet någonsin lämnats.
    if (cmd_) {
        flush();
        id<MTLTexture> target = curTarget_;
        endEncoder();
        [[cmd_ renderCommandEncoderWithDescriptor:pass] endEncoding];
        if (d.mipmaps) {
            id<MTLBlitCommandEncoder> blit = [cmd_ blitCommandEncoder];
            [blit generateMipmapsForTexture:tex];
            [blit endEncoding];
        }
        if (target)
            startEncoder(target, MTLLoadActionLoad, MTLClearColor{});
    } else {
        id<MTLCommandBuffer> cb = [queue_ commandBuffer];
        [[cb renderCommandEncoderWithDescriptor:pass] endEncoding];
        if (d.mipmaps) {
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit generateMipmapsForTexture:tex];
            [blit endEncoding];
        }
        [cb commit];
    }
    ++stats_.targetsCreated;
    return std::make_unique<MetalRenderTarget>(tex, msaa, depth, w, h,
                                               samples, d.mipmaps);
}

// UPPLÖSNINGSPASSET för ett MSAA-mål som lämnas (se filhuvudet): ett tomt
// pass som laddar den provtagna ytan och löser upp den till bilden. `final`
// (målet ligger inte kvar i stacken): den provtagna ytan behövs inte mer —
// MultisampleResolve utan Store, och djupet DontCare.
void MetalRenderer::resolveTarget(MetalRenderTarget& t, bool final)
{
    MTLRenderPassDescriptor* pass =
        [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture        = t.msaaTexture();
    pass.colorAttachments[0].loadAction     = MTLLoadActionLoad;
    pass.colorAttachments[0].resolveTexture = t.texture();
    pass.colorAttachments[0].storeAction =
        final ? MTLStoreActionMultisampleResolve
              : MTLStoreActionStoreAndMultisampleResolve;
    if (final && t.depthTexture()) {
        pass.depthAttachment.texture     = t.depthTexture();
        pass.depthAttachment.loadAction  = MTLLoadActionDontCare;
        pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    }
    [[cmd_ renderCommandEncoderWithDescriptor:pass] endEncoding];
}

void MetalRenderer::bindTarget(RenderTarget* t)
{
    // Målen binds inom en frame eller ett offscreen-block (renderer.h) — utan
    // commandbuffer finns ingen encoder att bryta.
    if (!cmd_)
        return;
    flush();   // det som batchats hör till målet det ritades för
    MetalRenderTarget* leaving = bound_;
    id<MTLTexture>     target  = frameTarget_;
    if (t) {
        auto& rt     = static_cast<MetalRenderTarget&>(*t);
        bound_       = &rt;
        target       = rt.colorTexture();
        curSamples_  = rt.samples();
        curDepthTex_ = rt.depthTexture();
        targetW_     = rt.image().width();
        targetH_     = rt.image().height();
    } else {
        bound_       = nullptr;
        curSamples_  = 1;
        curDepthTex_ = nil;
        targetW_     = width_;
        targetH_     = height_;
    }
    endEncoder();
    // Det utgående MSAA-målet löses upp EFTER sin sista encoder och FÖRE
    // nästa måls — ligger det kvar i stacken kommer man tillbaka till det.
    if (leaving && leaving->msaaTexture())
        resolveTarget(*leaving, !targetInStack(*leaving));
    // MIP-KEDJAN räknas om när målet lämnas — efter upplösningen och
    // mellan två encoders, där ingen render-encoder är öppen.
    if (leaving && leaving->mipmapped()) {
        id<MTLBlitCommandEncoder> blit = [cmd_ blitCommandEncoder];
        [blit generateMipmapsForTexture:leaving->texture()];
        [blit endEncoding];
    }
    if (target)
        startEncoder(target, MTLLoadActionLoad, MTLClearColor{});
}

// Målets bild → CPU, rak alfa (renderer.h). UTANFÖR en frame: arbetet som
// ritade målet måste ha körts, och i en öppen frame är det inte ens
// inskickat — då false. En egen commandbuffer med blit + väntan, som
// captureRetained.
bool MetalRenderer::readTarget(RenderTarget& t, std::vector<std::uint8_t>& rgba)
{
    if (!queue_ || !readTargetOk(t))
        return false;
    if (cmd_) {
        if (!readLogged_) {
            readLogged_ = true;
            log("readTarget: mitt i en frame eller ett offscreen-block — läs "
                "efter renderFrame eller endOffscreen");
        }
        return false;
    }
    auto&          rt  = static_cast<MetalRenderTarget&>(t);
    id<MTLTexture> tex = rt.texture();
    const int      tw  = static_cast<int>(tex.width);
    const int      th  = static_cast<int>(tex.height);
    const std::size_t bytes =
        static_cast<std::size_t>(tw) * static_cast<std::size_t>(th) * 4;
    @autoreleasepool {
        id<MTLBuffer> buf =
            [device_ newBufferWithLength:bytes
                                 options:MTLResourceStorageModeShared];
        if (!buf) {
            log("readTarget: läsbufferten kunde inte skapas");
            return false;
        }
        id<MTLCommandBuffer>      cmd  = [queue_ commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
        [blit copyFromTexture:tex
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(0, 0, 0)
                          sourceSize:MTLSizeMake(tex.width, tex.height, 1)
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
    unpremultiplyRgba(rgba.data(), static_cast<std::size_t>(tw) * th);
    return true;
}

} // namespace gfx
