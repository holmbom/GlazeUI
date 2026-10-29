// Bilder i Metal-backenden: Image I/O (OS-komponent) avkodar PNG →
// CGImage → RGBA8-pixlar (`gfx::decodeImage`, trådsäker) → `createImage` →
// MTLTexture. Ritas i shader-mode 5 (RGBA rakt av — samma väg som
// compositen av retained-RT:n) i den vanliga batch-pipelinen. Spegeln av
// d3d11_image.cpp med en macOS-tvist: pipelinen blandar RAK alfa
// (srcAlpha/invSrcAlpha), och pixlarna läses därför RAKT ur Image I/O:s
// egen avkodning när den är rak (`readRawPixels` — PNG:ens vanliga fall,
// byte för byte som filen). Bara en ovanlig form ritas i en CGBitmapContext,
// som BARA kan rita premultiplicerat, och av-premultipliceras sedan på CPU
// (hoppas steget över får bildkanter dubbelmultiplicerad alfa = mörka
// fransar; det kostar ett till två steg i halvgenomskinliga pixlar). BARA
// PNG: signaturen prövas innan Image I/O får datat,
// och filvarianten läser filen och går samma väg — Image I/O hade annars
// tagit JPEG, HEIC och GIF.

#import <ImageIO/ImageIO.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../image_util.h"
#include "metal_backend.h"
#include "gfx/platform.h"

namespace gfx {
namespace {

// Image I/O:s EGNA avkodade pixlar → rak RGBA8, utan att rita. Bara de
// enkla formerna: grå eller RGB, 8 eller 16 bitar per kanal i heltal, rak
// alfa eller ingen. Allt annat (förmultiplicerat, palett, flyttal, CMYK,
// en vänd byteordning) svarar false och går ritvägen. Ritvägen räcker inte
// för PNG:ens vanliga fall: förmultipliceringen och tillbakadelningen
// tappar ett till två steg i varje halvgenomskinlig pixel (icon_gate).
// Här blir bytena exakt filens, utan färgomvandling — som WIC:s.
bool readRawPixels(CGImageRef image, std::size_t w, std::size_t h,
                   std::vector<std::uint8_t>& px)
{
    const std::size_t bpc = CGImageGetBitsPerComponent(image);
    const std::size_t bpp = CGImageGetBitsPerPixel(image);
    const CGBitmapInfo info = CGImageGetBitmapInfo(image);
    const CGImageAlphaInfo alpha =
        static_cast<CGImageAlphaInfo>(info & kCGBitmapAlphaInfoMask);
    if ((bpc != 8 && bpc != 16) || (info & kCGBitmapFloatComponents))
        return false;

    CGColorSpaceRef cs = CGImageGetColorSpace(image);   // NULL för en mask
    if (!cs)
        return false;
    std::size_t color = 0;
    switch (CGColorSpaceGetModel(cs)) {
    case kCGColorSpaceModelMonochrome: color = 1; break;
    case kCGColorSpaceModelRGB:        color = 3; break;
    default:                           return false;
    }
    bool hasAlpha = false, first = false;
    switch (alpha) {
    case kCGImageAlphaNone:                                   break;
    case kCGImageAlphaLast:       hasAlpha = true;            break;
    case kCGImageAlphaFirst:      hasAlpha = true; first = true; break;
    case kCGImageAlphaNoneSkipLast:                           break;
    case kCGImageAlphaNoneSkipFirst:              first = true; break;
    default:                      return false;   // förmultiplicerat, AlphaOnly
    }
    const bool skip = alpha == kCGImageAlphaNoneSkipLast ||
                      alpha == kCGImageAlphaNoneSkipFirst;
    const std::size_t comps = color + (hasAlpha || skip ? 1 : 0);
    if (bpp != comps * bpc)
        return false;

    // Byteordningen: 16 bitar i stor ände (PNG:ens egen, och förvalet) eller
    // liten; 8 bitar bara i minnesordning (förvalet eller 32Big) — en vänd
    // ordning (32Little, BGRA) går ritvägen.
    const CGBitmapInfo order = info & kCGBitmapByteOrderMask;
    bool little16 = false;
    if (bpc == 16) {
        if (order == kCGBitmapByteOrder16Little)
            little16 = true;
        else if (order != kCGBitmapByteOrderDefault &&
                 order != kCGBitmapByteOrder16Big)
            return false;
    } else if (order != kCGBitmapByteOrderDefault &&
               order != kCGBitmapByteOrder32Big) {
        return false;
    }

    CGDataProviderRef dp = CGImageGetDataProvider(image);
    CFDataRef data = dp ? CGDataProviderCopyData(dp) : nullptr;
    if (!data)
        return false;
    const std::size_t stride = CGImageGetBytesPerRow(image);
    const std::size_t rowBytes = w * comps * (bpc / 8);
    const bool fits = stride >= rowBytes &&
                      static_cast<std::size_t>(CFDataGetLength(data)) >=
                          stride * (h - 1) + rowBytes;
    if (fits) {
        const UInt8* src = CFDataGetBytePtr(data);
        // 16 → 8 bitar avrundat (v·257 ⇒ exakt v).
        auto sample = [&](const UInt8* row, std::size_t k) -> unsigned {
            if (bpc == 8)
                return row[k];
            const UInt8* b = row + k * 2;
            const unsigned v = little16 ? (b[0] | (b[1] << 8))
                                        : ((b[0] << 8) | b[1]);
            return (v * 255u + 32767u) / 65535u;
        };
        px.resize(w * h * 4);
        for (std::size_t y = 0; y < h; ++y) {
            const UInt8* row = src + y * stride;
            for (std::size_t x = 0; x < w; ++x) {
                const std::size_t base = x * comps;
                const std::size_t c0 = base + (first ? 1 : 0);
                std::uint8_t* d = &px[(y * w + x) * 4];
                for (std::size_t c = 0; c < 3; ++c)
                    d[c] = static_cast<std::uint8_t>(
                        sample(row, c0 + (color == 3 ? c : 0)));
                d[3] = hasAlpha ? static_cast<std::uint8_t>(sample(
                                      row, first ? base : base + color))
                                : 255;
            }
        }
    }
    CFRelease(data);
    return fits;
}

// CGImagen → rak RGBA8, rad 0 överst, täta rader. false vid varje fel
// (utdata orörda). CoreGraphics är trådsäkert för egna kontexter.
bool decodeCgImage(CGImageRef image, std::vector<std::uint8_t>& out, int& outW,
                   int& outH)
{
    if (!image)
        return false;
    const std::size_t w = CGImageGetWidth(image);
    const std::size_t h = CGImageGetHeight(image);
    if (w == 0 || h == 0 || w > INT_MAX / 4 || h > INT_MAX)
        return false;

    {
        std::vector<std::uint8_t> raw;
        if (readRawPixels(image, w, h, raw)) {
            out  = std::move(raw);
            outW = static_cast<int>(w);
            outH = static_cast<int>(h);
            return true;
        }
    }

    // Reserven: rita in i en RGBA-kontext oavsett källformat (palett, CMYK
    // ...) — motsvarigheten till WICConvertBitmapSource. AlphaLast +
    // standardbyteordning = R,G,B,A i minnet, samma som RGBA8Unorm.
    // Bufferten blir top-down (rad 0 = översta raden).
    std::vector<std::uint8_t> px(w * h * 4);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        px.data(), w, h, 8, w * 4, cs, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(cs);
    if (!ctx)
        return false;
    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), image);
    CGContextRelease(ctx);

    // Av-premultiplicera: rgb = rgb·255/a (avrundat), a == 0 → 0.
    for (std::size_t i = 0; i < px.size(); i += 4) {
        const unsigned a = px[i + 3];
        if (a == 0) {
            px[i] = px[i + 1] = px[i + 2] = 0;
        } else if (a < 255) {
            for (int c = 0; c < 3; ++c) {
                const unsigned v = (px[i + c] * 255u + a / 2) / a;
                px[i + c] = static_cast<std::uint8_t>(v > 255 ? 255 : v);
            }
        }
    }
    out  = std::move(px);
    outW = static_cast<int>(w);
    outH = static_cast<int>(h);
    return true;
}

// Första bilden i en källa (fil eller minne) → pixlar. Släpper källan.
bool decodeSource(CGImageSourceRef src, std::vector<std::uint8_t>& out,
                  int& w, int& h)
{
    if (!src)
        return false;
    CGImageRef image = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    CFRelease(src);
    const bool ok = decodeCgImage(image, out, w, h);
    if (image)
        CGImageRelease(image);
    return ok;
}

} // namespace

// Fildata → pixlar, utan renderer (image.h: trådsäker). BARA PNG: signaturen
// prövas innan Image I/O får datat.
bool decodeImage(const void* data, std::size_t size,
                 std::vector<std::uint8_t>& rgba, int& w, int& h)
{
    if (!hasPngSignature(data, size)) {
        log("decodeImage: %zu byte är inte PNG — bara PNG avkodas (gfx/image.h)",
            size);
        return false;
    }
    bool ok = false;
    if (size <= static_cast<std::size_t>(LONG_MAX)) {
        CFDataRef cfdata = CFDataCreate(
            nullptr, static_cast<const UInt8*>(data),
            static_cast<CFIndex>(size));
        if (cfdata) {
            ok = decodeSource(CGImageSourceCreateWithData(cfdata, nullptr),
                              rgba, w, h);
            CFRelease(cfdata);
        }
    }
    if (!ok)
        log("decodeImage: kunde inte avkoda %zu byte", size);
    return ok;
}

std::unique_ptr<Image> MetalRenderer::loadImage(const std::string& path)
{
    // Returnerar alltid ett objekt — valid() avgör (som loadFont). Filen
    // läses och går genom decodeImage: samma PNG-prov som minnesvarianten.
    NSData* data = [NSData dataWithContentsOfFile:@(path.c_str())];
    if (!data) {
        log("loadImage: kunde inte läsa %s", path.c_str());
        return std::make_unique<MetalImage>();
    }
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;
    if (!decodeImage(data.bytes, data.length, px, w, h)) {   // loggar själv
        log("loadImage: %s gav ingen bild", path.c_str());
        return std::make_unique<MetalImage>();
    }
    return createImage(w, h, ImageFormat::Rgba8, px.data(), 0);
}

std::unique_ptr<Image> MetalRenderer::loadImage(const void* data,
                                                std::size_t size)
{
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;
    if (!decodeImage(data, size, px, w, h))   // loggar själv
        return std::make_unique<MetalImage>();
    return createImage(w, h, ImageFormat::Rgba8, px.data(), 0);
}

// ------------------------------------------------------ bilder ur pixlar

std::unique_ptr<Image> MetalRenderer::createImage(int w, int h, ImageFormat f,
                                                  const void* px,
                                                  std::size_t stride,
                                                  bool mipmaps)
{
    std::size_t rowBytes = 0;
    if (!device_ || !imageDescOk(w, h, f, stride, rowBytes))
        return std::make_unique<MetalImage>();
    MTLTextureDescriptor* td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:(f == ImageFormat::R8
                                                ? MTLPixelFormatR8Unorm
                                                : MTLPixelFormatRGBA8Unorm)
                                     width:static_cast<NSUInteger>(w)
                                    height:static_cast<NSUInteger>(h)
                                 mipmapped:mipmaps ? YES : NO];
    id<MTLTexture> tex = [device_ newTextureWithDescriptor:td];
    if (!tex) {
        log("createImage: %d × %d kunde inte skapas", w, h);
        return std::make_unique<MetalImage>();
    }
    // En NY textur refereras inte av något kodat rit-anrop än, så
    // replaceRegion (CPU-skrivning, direkt) är säker här — samma regel som
    // uppdateringarnas Direct-väg (updateImage, stämpeln 0). Utan pixlar:
    // nollor — Metal lovar inget om en ny texturs innehåll.
    std::vector<std::uint8_t> zeros;
    const void*               src = px;
    if (!src) {
        zeros.assign(rowBytes * static_cast<std::size_t>(h), 0);
        src = zeros.data();
    }
    [tex replaceRegion:MTLRegionMake2D(0, 0, static_cast<NSUInteger>(w),
                                       static_cast<NSUInteger>(h))
           mipmapLevel:0
             withBytes:src
           bytesPerRow:rowBytes];
    if (px)
        stats_.bytesUploaded += static_cast<std::uint64_t>(w) * h *
                                bytesPerPixel(f);
    auto im = std::make_unique<MetalImage>(tex, w, h, f, false, mipmaps);
    if (mipmaps)
        generateMips(*im);   // kedjan ur nivå 0
    return im;
}

void MetalRenderer::generateMips(const MetalImage& im)
{
    if (!queue_ || !im.valid() || !im.mipmapped())
        return;
    @autoreleasepool {
        if (cmd_ && im.usedIn() != frameSerial_) {
            // I UPPLADDNINGSPASSET: hela bilden som rektangel överlappar varje
            // tidigare skrivning till den i passet, så bliten hamnar i ett
            // senare blit-pass än kopiorna (uploadEncoder).
            const UploadRect hela{0, 0, static_cast<std::size_t>(im.width()),
                                  static_cast<std::size_t>(im.height())};
            [uploadEncoder((__bridge const void*)im.texture(), hela)
                generateMipmapsForTexture:im.texture()];
        } else {
            // Ritad i DEN HÄR framen (encoder-brottet) eller utanför en frame
            // (en egen commandbuffer) — beginBlit väljer.
            id<MTLBlitCommandEncoder> blit = beginBlit();
            [blit generateMipmapsForTexture:im.texture()];
            endBlit(blit);
        }
    }
    // En kodad blit refererar bilden: nästa skrivning tar Pass, Break eller
    // Own, aldrig Direct. Före första framen är löpnumret 0 (= aldrig) — då 1,
    // så också den första framens skrivning går en ordnad väg.
    im.noteUse(std::max<std::uint64_t>(frameSerial_, 1));
}

bool MetalRenderer::updateImage(Image& img, int x, int y, int w, int h,
                                const void* px, std::size_t stride)
{
    std::size_t rowBytes = 0;
    if (!device_ || !imageUpdateOk(img, x, y, w, h, px, stride, rowBytes))
        return false;
    // Bilden kommer alltid ur den här backenden — casten är säker per
    // konstruktion, som drawText:s Font.
    auto& im = static_cast<MetalImage&>(img);
    const std::size_t bpp  = bytesPerPixel(img.format());
    const UploadPath  path = uploadPathFor(im.usedIn());
    if (path == UploadPath::Direct) {
        // ALDRIG RITAD: inget kommando refererar texturen, så CPU:n skriver
        // i den direkt — med anroparens rader som de är.
        [im.texture()
            replaceRegion:MTLRegionMake2D(static_cast<NSUInteger>(x),
                                          static_cast<NSUInteger>(y),
                                          static_cast<NSUInteger>(w),
                                          static_cast<NSUInteger>(h))
              mipmapLevel:0
                withBytes:px
              bytesPerRow:rowBytes];
    } else {
        // RITAD: en blit ur staging med raderna täta på hela fyrbytes
        // (macOS:s blitkrav för radlängden) — i uppladdningspasset före
        // framen, eller med ett encoder-brott när bilden redan ritats i DEN
        // HÄR framen (det som ritats behåller det gamla innehållet,
        // renderer.h). En lokal pool runt commandbuffertarna och encoderna.
        const std::size_t row   = static_cast<std::size_t>(w) * bpp;
        const std::size_t pitch = (row + 3) / 4 * 4;
        @autoreleasepool {
            const Staging s = stagingAlloc(pitch * static_cast<std::size_t>(h));
            if (!s.buf)
                return false;   // loggat
            const auto* srcRows = static_cast<const std::uint8_t*>(px);
            for (int r = 0; r < h; ++r)
                std::memcpy(s.ptr + static_cast<std::size_t>(r) * pitch,
                            srcRows + static_cast<std::size_t>(r) * rowBytes,
                            row);
            const bool                pass = path == UploadPath::Pass;
            const UploadRect          mål{static_cast<std::size_t>(x),
                                          static_cast<std::size_t>(y),
                                          static_cast<std::size_t>(x + w),
                                          static_cast<std::size_t>(y + h)};
            id<MTLBlitCommandEncoder> blit =
                pass ? uploadEncoder((__bridge const void*)im.texture(), mål)
                     : beginBlit();
            [blit copyFromBuffer:s.buf
                       sourceOffset:s.offset
                  sourceBytesPerRow:pitch
                sourceBytesPerImage:pitch * static_cast<std::size_t>(h)
                         sourceSize:MTLSizeMake(static_cast<NSUInteger>(w),
                                                static_cast<NSUInteger>(h), 1)
                          toTexture:im.texture()
                   destinationSlice:0
                   destinationLevel:0
                  destinationOrigin:MTLOriginMake(static_cast<NSUInteger>(x),
                                                  static_cast<NSUInteger>(y), 0)];
            if (!pass)
                endBlit(blit);
        }
    }
    if (im.mipmapped())
        generateMips(im);   // kedjan ur den nya nivå 0
    stats_.bytesUploaded += static_cast<std::uint64_t>(w) * h * bpp;
    return true;
}

// PNG-encode (glaze-render): encode-spegeln av loadImage — Image I/O:s
// CGImageDestination (samma OS-komponent, redan länkad). Rå RGBA8 med
// rak alfa → CGImage via dataprovider (kCGImageAlphaLast — CGImageCreate
// tar rak alfa, till skillnad från bitmap-kontexter) → public.png.
bool encodePng(const std::string& path, const std::vector<std::uint8_t>& rgba,
               int w, int h)
{
    if (w <= 0 || h <= 0 ||
        rgba.size() < static_cast<std::size_t>(w) * h * 4) {
        log("encodePng: ogiltiga mått/otillräcklig pixeldata (%dx%d)", w, h);
        return false;
    }

    bool ok = false;
    CGColorSpaceRef   cs = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef dp = CGDataProviderCreateWithData(
        nullptr, rgba.data(),
        static_cast<std::size_t>(w) * h * 4, nullptr);
    CGImageRef image =
        dp ? CGImageCreate(static_cast<std::size_t>(w),
                           static_cast<std::size_t>(h), 8, 32,
                           static_cast<std::size_t>(w) * 4, cs,
                           static_cast<CGBitmapInfo>(kCGBitmapByteOrderDefault) |
                               static_cast<CGBitmapInfo>(kCGImageAlphaLast),
                           dp, nullptr, false, kCGRenderingIntentDefault)
           : nullptr;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.c_str()),
        static_cast<CFIndex>(path.size()), false);
    if (image && url) {
        CGImageDestinationRef dst = CGImageDestinationCreateWithURL(
            url, CFSTR("public.png"), 1, nullptr);
        if (dst) {
            CGImageDestinationAddImage(dst, image, nullptr);
            ok = CGImageDestinationFinalize(dst);
            CFRelease(dst);
        }
    }
    if (url)
        CFRelease(url);
    if (image)
        CGImageRelease(image);
    if (dp)
        CGDataProviderRelease(dp);
    CGColorSpaceRelease(cs);

    if (!ok)
        log("encodePng: kunde inte skriva %s", path.c_str());
    return ok;
}

void MetalRenderer::drawImage(Image& img, const Rectf& src, const Rectf& dst,
                              float opacity)
{
    // Bilden kommer alltid ur samma backend (loadImage ovan) — casten är
    // säker per konstruktion, som drawText:s Font.
    auto& im = static_cast<MetalImage&>(img);
    // STÄMPELN: bilden batchas här och refereras när batchen
    // flushas, senare i framen — en uppdatering efter det här anropet ska
    // bryta (och flusha) i stället för att skriva direkt.
    im.noteUse(frameSerial_);
    if (!im.valid() || im.width() <= 0 || im.height() <= 0 ||
        !imageDrawOk(img))
        return;

    // Bildens EGET filter — batch-tillstånd.
    setState(im.texture(), MTLPrimitiveTypeTriangle, im.filter());

    // Käll-rect → normaliserad UV, batchad i samma pipeline som all annan
    // ritning. Metal har rad 0 överst i alla texturer (`flippedV` är alltid
    // falskt här), men vägen är densamma som D3D11:s.
    const float iw = static_cast<float>(im.width());
    const float ih = static_cast<float>(im.height());
    const float u1 = src.x / iw, u2 = (src.x + src.w) / iw;
    float       v1 = src.y / ih, v2 = (src.y + src.h) / ih;
    if (im.flippedV()) {
        v1 = 1.0f - v1;
        v2 = 1.0f - v2;
    }

    // OPACITETEN bärs i vertexfärgens ALFA (uchar4Normalized, alfa i
    // översta byten) och multipliceras in i bildens rak-alfa i shaderns
    // mode 5–7 — färgkanalerna rörs inte (samma väg som D3D11). Mode 5 =
    // RGBA rakt av, 6 = ett render-måls premultiplicerade innehåll, 7 = R8
    // som gråskala.
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
// Scenens bilder — rad för rad samma som
// d3d11_image.cpp:s program, också de projektiva vikterna `q` (attribut 2 —
// varje PSO-variant byggs ur samma vertexdeskriptor,
// `psoFor`). Uniformerna i `SceneU`-formen på index 1 i båda
// stegen, klippmaskerna i fragmentbuffert 0 (följer encodern), bilden och
// dess sampler på index 0. Den raka färgen FÄRGJUSTERAS före
// opaciteten (`glaze_colorAdjust`, `kMslAdjustSrc`).

namespace {

// Klippmaskernas del (`kMslClipSrc`) och färgjusteringen (`kMslAdjustSrc`)
// klistras FÖRE.
const char kImageMslSrc[] = R"msl(
struct ImageU {
    float4x4 viewProj;   // chunk-lokalt → pixelklipp
    float4   params;     // .xy målets storlek (px), .z opaciteten,
                         // .w flaggorna: 1 premultiplicerad, 2 speglad v, 4 R8
    float4   adjust;     // färgjusteringen: mättnad, kontrast, ljushet, 0
};

struct ImageIn {
    float2 pos [[attribute(0)]];
    float2 uv  [[attribute(1)]];
    float  q   [[attribute(2)]];   // hörnets projektiva vikt (1 för en rektangel)
};

struct ImageOut {
    float4 pos [[position]];
    // (u·q, v·q, q) PERSPEKTIVKORREKT (förvalet): w går orört vidare till
    // rasteriseringen, och divisionen med q i fragmentet gör en fyrhörning
    // exakt projektiv.
    float3 uvq;
};

vertex ImageOut imageVs(ImageIn i [[stage_in]], constant ImageU& u [[buffer(1)]])
{
    ImageOut o;
    const float4 c = u.viewProj * float4(i.pos, 0.0, 1.0);
    // Pixelklipp → Metals klipprymd (z redan i [0, w], som D3D11).
    o.pos = float4(c.x * 2.0 / u.params.x - c.w, c.w - c.y * 2.0 / u.params.y,
                   c.z, c.w);
    const int flags = int(u.params.w + 0.5);
    // Speglingen FÖRE multiplikationen med q — v' är bildens koordinat.
    const float v = (flags & 2) != 0 ? 1.0 - i.uv.y : i.uv.y;
    o.uvq = float3(i.uv.x * i.q, v * i.q, i.q);
    return o;
}

fragment float4 imagePs(ImageOut i [[stage_in]],
                        texture2d<float> tex0 [[texture(0)]],
                        sampler smp0 [[sampler(0)]],
                        constant ClipMasks& cm [[buffer(0)]],
                        constant ImageU& u [[buffer(1)]],
                        constant SceneClip& sc [[buffer(4)]])
{
    const int flags = int(u.params.w + 0.5);
    float4 t = tex0.sample(smp0, i.uvq.xy / i.uvq.z);
    if ((flags & 4) != 0)
        t = float4(t.rrr, 1.0);                          // R8: gråskala
    if ((flags & 1) != 0)
        t.rgb = t.a > 0.0 ? t.rgb / t.a : float3(0.0);   // render-mål
    // Färgjusteringen på den RAKA färgen — förvalen lämnar den orörd.
    t.rgb = glaze_colorAdjust(t.rgb, u.adjust.xyz);
    // Rak alfa som UI:t; opaciteten, klippmaskerna och målarens klipp rör
    // bara alfan.
    t.a *= saturate(u.params.z) * clipCoverage(i.pos.xy, cm) *
           sceneClip(i.pos.xy, sc);
    return t;
}
)msl";

} // namespace

bool MetalRenderer::initImageProgram()
{
    @autoreleasepool {
        NSError*          err = nil;
        const std::string src =
            std::string(kMslClipSrc) + kMslAdjustSrc + kImageMslSrc;
        id<MTLLibrary>    lib = compileMsl(@(src.c_str()), nil, &err);
        if (!lib) {
            log("MSL (Image) misslyckades: %s — drawMesh ritar inga bilder",
                err ? err.localizedDescription.UTF8String : "?");
            return false;
        }
        id<MTLFunction> vs = [lib newFunctionWithName:@"imageVs"];
        id<MTLFunction> fs = [lib newFunctionWithName:@"imagePs"];
        if (!vs || !fs) {
            log("MSL: imageVs/imagePs saknas i biblioteket");
            return false;
        }

        // ImageVertex på sina fasta offset (gfx/scene.h).
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        vd.attributes[0].format      = MTLVertexFormatFloat2;
        vd.attributes[0].offset      = offsetof(ImageVertex, x);
        vd.attributes[0].bufferIndex = 0;
        vd.attributes[1].format      = MTLVertexFormatFloat2;
        vd.attributes[1].offset      = offsetof(ImageVertex, u);
        vd.attributes[1].bufferIndex = 0;
        vd.attributes[2].format      = MTLVertexFormatFloat;   // q
        vd.attributes[2].offset      = offsetof(ImageVertex, q);
        vd.attributes[2].bufferIndex = 0;
        vd.layouts[0].stride       = sizeof(ImageVertex);
        vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        // Funktionerna och vertexformatet; blandningen, provtagningen och
        // djupet sätter varianterna (`psoFor`, metal_renderer.mm).
        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction   = vs;
        pd.fragmentFunction = fs;
        pd.vertexDescriptor = vd;
        if (!registerProgram(Pipeline::Image, pd, "Image"))
            return false;
    }
    return true;
}

void MetalRenderer::shutdownImageProgram()
{
    progDesc_[static_cast<int>(Pipeline::Image)] = nil;
}

void MetalRenderer::drawImageMesh(const MeshDraw& d)
{
    if (!encoder_ || !meshDrawOk(d, hasProgram(Pipeline::Image)) ||
        !imageDrawOk(*d.image))
        return;
    // Bilden kommer alltid ur den här backenden — casten är säker per
    // konstruktion, som drawText:s Font.
    const auto& im = static_cast<const MetalImage&>(*d.image);

    flush();   // det UI:t batchat före anropet ritas först — målarordningen
    // PSO-varianten för målet och anropets blandning.
    id<MTLRenderPipelineState> pso = psoFor(Pipeline::Image, d.state.blend);
    if (!pso)
        return;   // loggat när varianten byggdes
    if (pipeline_ != Pipeline::Image) {
        pipeline_ = Pipeline::Image;
        countProgram(Pipeline::Image);
    }
    setPso(pso);

    // Kameran, opaciteten, bildens flaggor och färgjusteringen, i båda
    // stegen — per anrop, räknas inte som uppladdning (RenderStats).
    SceneU u{};
    std::memcpy(u.viewProj, d.uniforms.viewProj, sizeof u.viewProj);
    u.params[0] = static_cast<float>(targetW_);   // det BUNDNA målet —
    u.params[1] = static_cast<float>(targetH_);   // ett render-mål
    u.params[2] = std::clamp(d.uniforms.opacity, 0.0f, 1.0f);
    u.params[3] = sceneImageFlags(im);   // samma tal som HLSL:ns och GLSL:ns
    sceneImageAdjust(d.uniforms, u.adjust);   // likaså
    [encoder_ setVertexBytes:&u length:sizeof u atIndex:1];
    [encoder_ setFragmentBytes:&u length:sizeof u atIndex:1];
    bindSceneClip(d);

    // Bilden och DESS sampler — UI:ts flush sätter sina egna vid nästa
    // ritning.
    [encoder_ setFragmentTexture:im.texture() atIndex:0];
    [encoder_ setFragmentSamplerState:samplerFor(im.filter()) atIndex:0];
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
