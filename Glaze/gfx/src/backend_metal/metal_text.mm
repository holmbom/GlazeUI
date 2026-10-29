// MetalFont — gfx::Font ovanpå Core Text (OS-native; descriptor-vägen
// laddar godtyckliga .ttf/.ttc-filer). Spegeln av d3d11_text.cpp:
// UTF-8-avkodaren, atlas-packaren och HELA mät-API:t är ordagranna portar
// (ren C++ ovanpå advanceOf); bara laddningen, advance-källan och
// rasteriseringen är plattformsspecifika. Glyfer rasteriseras vid behov i
// en kCGImageAlphaOnly-kontext (ren 8-bitars täckningsgrad = R8, gråskale-
// AA utan subpixel — motsvarigheten till ClearType-3x1-medlingen fast utan
// medlingssteget) och packas rad för rad i en R8-atlas på GPU:n.
//
// CGBitmapContext-bufferten är
// TOP-DOWN (rad 0 = översta raden) trots CG:s bottom-left-origo — ingen
// radflipp behövs; bearingY = -(ceil(bounds.maxY) + marginal).
//
// Rasteriseringen är EN funktion (`rasterize`) som atlasen och ritytans
// SDF-atlas (`rasterGlyph`) delar; stilen väljs ur
// familjen med CTFontCreateCopyWithSymbolicTraits och läses tillbaka ur
// fontens symboliska drag.
//
// GLYFRESERVEN: en kodpunkt fonten saknar
// slås upp med Core Texts egen kedja, `CTFontCreateForString` — svaret
// underkänns när det är fontens egen font eller `LastResort` (Apples font för
// tecken INGEN font har, vars glyf är en ersättningsruta), ställs i fontens
// storlek när den avviker, och måste ha glyfen. Regeln och ordningen står i
// ../font_fallback.h; svaret cachas per kodpunkt (också ett negativt), och
// mätningen, atlasen och rasterGlyph läser samma cache.

#include <cmath>
#include <cstring>
#include <vector>

#include "../font_catalog.h"
#include "metal_backend.h"
#include "gfx/platform.h"

namespace gfx {

// ---------------------------------------------------------------- UTF-8

namespace {

// Minimal UTF-8-avkodare — ogiltiga sekvenser blir U+FFFD och en byte äts
// (fonten saknar ändå glyfen → tofu, aldrig krasch).
char32_t nextCodepoint(const char*& p, const char* end)
{
    const auto b0 = static_cast<unsigned char>(*p++);
    if (b0 < 0x80)
        return b0;

    int       len = 0;
    char32_t  cp  = 0;
    if      ((b0 & 0xE0) == 0xC0) { len = 1; cp = b0 & 0x1F; }
    else if ((b0 & 0xF0) == 0xE0) { len = 2; cp = b0 & 0x0F; }
    else if ((b0 & 0xF8) == 0xF0) { len = 3; cp = b0 & 0x07; }
    else
        return 0xFFFD;

    for (int i = 0; i < len; ++i) {
        if (p >= end || (static_cast<unsigned char>(*p) & 0xC0) != 0x80)
            return 0xFFFD;
        cp = (cp << 6) | (static_cast<unsigned char>(*p++) & 0x3F);
    }
    return cp;
}

// Kodpunkten som UTF-16 (surrogatpar över 0xFFFF) — antalet enheter.
CFIndex utf16(char32_t cp, UniChar (&buf)[2])
{
    if (cp > 0xFFFF) {
        const char32_t v = cp - 0x10000;
        buf[0]           = static_cast<UniChar>(0xD800 + (v >> 10));
        buf[1]           = static_cast<UniChar>(0xDC00 + (v & 0x3FF));
        return 2;
    }
    buf[0] = static_cast<UniChar>(cp);
    return 1;
}

// `LastResort` — Core Texts sista svar, en font med en ersättningsruta per
// Unicode-block: kedjan HITTADE ingen riktig glyf.
bool isLastResort(CTFontRef f)
{
    CFStringRef ps = CTFontCopyPostScriptName(f);
    const bool  lr =
        ps && CFStringCompare(ps, CFSTR("LastResort"), 0) == kCFCompareEqualTo;
    if (ps)
        CFRelease(ps);
    return lr;
}

} // namespace

// ---------------------------------------------------------------- laddning

bool MetalFont::load(const std::string& path, float sizePt)
{
    unload();

    // Descriptor-vägen klarar .ttc (Mac-katalogen listar Helvetica.ttc/
    // Menlo.ttc) — ta descriptor 0, som CreateFontFace-index 0 i D3D11.
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.c_str()),
        static_cast<CFIndex>(path.size()), false);
    if (!url)
        return false;
    CFArrayRef descs = CTFontManagerCreateFontDescriptorsFromURL(url);
    CFRelease(url);
    if (!descs)
        return false;
    if (CFArrayGetCount(descs) == 0) {
        CFRelease(descs);
        return false;
    }
    auto desc = static_cast<CTFontDescriptorRef>(
        CFArrayGetValueAtIndex(descs, 0));
    // Em-storleken tolkas som px ("ptsize" = pixelhöjd, samma beslut som
    // D3D11) — tvärs-backend-KONTRAKTET: samma tal ger samma textmått i
    // alla backends, så layouten blir identisk på Windows/Mac/Linux.
    font_ = CTFontCreateWithFontDescriptor(desc, sizePt, nullptr);
    CFRelease(descs);
    if (!font_)
        return false;

    sizePx_ = sizePt;
    readMetrics();
    style_ = readStyle();
    return true;
}

void MetalFont::readMetrics()
{
    ascent_ = static_cast<float>(CTFontGetAscent(font_));
    height_ = static_cast<float>(CTFontGetAscent(font_) +
                                 CTFontGetDescent(font_) +
                                 CTFontGetLeading(font_));
}

bool MetalFont::loadFamily(FontFamily f, float sizePt, FontStyle style)
{
    // Fallback-kedjan kommer ur katalogen — enda stället med sökvägar. Inom
    // en familj prövas den närmaste stilens fil först; en samlingsfil (.ttc)
    // ger sin första stil, så stilen väljs sedan ur familjen.
    for (const FontFiles& fam : systemFontCandidates(f))
        for (FontStyle s : nearestStyles(style))
            if (const char* path = fontFile(fam, s); path && load(path, sizePt)) {
                applyStyle(style);
                return true;
            }
    return false;
}

// Den NÄRMASTE stilen ur fontens familj: Core Text letar själv upp t.ex.
// Helvetica Bold i samlingen. nullptr = familjen saknar stilen — nästa
// närmare prövas, och ingen alls lämnar fonten som den är.
void MetalFont::applyStyle(FontStyle wanted)
{
    if (style_ == wanted)
        return;
    for (FontStyle s : nearestStyles(wanted)) {
        CTFontSymbolicTraits traits = 0;
        if (isBold(s))
            traits |= kCTFontTraitBold;
        if (isItalic(s))
            traits |= kCTFontTraitItalic;
        CTFontRef styled = CTFontCreateCopyWithSymbolicTraits(
            font_, 0.0, nullptr, traits, kCTFontTraitBold | kCTFontTraitItalic);
        if (styled) {
            CFRelease(font_);
            font_ = styled;
            readMetrics();
            break;
        }
    }
    style_ = readStyle();
}

FontStyle MetalFont::readStyle() const
{
    const CTFontSymbolicTraits t = CTFontGetSymbolicTraits(font_);
    return fontStyle((t & kCTFontTraitBold) != 0, (t & kCTFontTraitItalic) != 0);
}

void MetalFont::unload()
{
    style_ = FontStyle::Regular;
    glyphs_.clear();
    resolved_.clear();
    for (CTFontRef r : reserves_)
        CFRelease(r);
    reserves_.clear();
    atlasTex_ = nil;
    penX_ = penY_ = rowH_ = 0;
    if (font_) {
        CFRelease(font_);
        font_ = nullptr;
    }
}

// ---------------------------------------------------------------- atlasen

bool MetalFont::ensureAtlas()
{
    if (atlasTex_)
        return true;
    if (!device_)
        return false;

    MTLTextureDescriptor* td = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                                     width:kAtlasW
                                    height:kAtlasH
                                 mipmapped:NO];
    // Delad lagring — replaceRegion skriver från CPU:n (enklare än
    // UpdateSubresource: ingen context behövs). Nya glyfer skrivs alltid i
    // orörda atlas-regioner, så jobb i flykt läser aldrig det som ändras.
    atlasTex_ = [device_ newTextureWithDescriptor:td];
    return atlasTex_ != nil;
}

void MetalFont::resetAtlas()
{
    // Atlasen full — börja om från tomt (glyferna rasteriseras om vid
    // behov). Händer i praktiken aldrig för UI-text med en atlas per font.
    glyphs_.clear();
    penX_ = penY_ = rowH_ = 0;
}

// Kodpunkt → UTF-16 (surrogatpar för cp > 0xFFFF) → glyfindex. 0 = saknas.
CGGlyph MetalFont::glyphIndexIn(CTFontRef f, char32_t cp) const
{
    if (!f || cp > 0x10FFFF)
        return 0;
    UniChar       buf[2];
    const CFIndex n         = utf16(cp, buf);
    CGGlyph       glyphs[2] = {0, 0};
    CTFontGetGlyphsForCharacters(f, buf, glyphs, n);
    return glyphs[0];
}

const MetalFont::Glyph* MetalFont::getGlyph(char32_t cp)
{
    auto it = glyphs_.find(cp);
    if (it != glyphs_.end())
        return &it->second;
    if (!font_ || !ensureAtlas())
        return nullptr;

    // Kodpunkt → glyf genom reserven (../font_fallback.h): fontens egen, en
    // reservfonts, ersättningen — samma svar som mätningen läser.
    const GlyphRef ref = resolve(cp).ref;

    // Rasterisera (DEN delade rasteriseringen)
    GlyphRaster raster;
    if (!rasterize(faceOf(ref.face), static_cast<CGGlyph>(ref.index), raster))
        return nullptr;
    Glyph g;
    g.advance = raster.advance;
    if (raster.width > 0 && raster.height > 0) {
        const int w = raster.width;
        const int h = raster.height;
        const std::vector<std::uint8_t>& gray = raster.coverage;

        // Packa in i atlasen, rad för rad (1 px luft mot grannglyfer)
        if (penX_ + w + 1 > kAtlasW) {
            penX_ = 0;
            penY_ += rowH_ + 1;
            rowH_ = 0;
        }
        if (penY_ + h + 1 > kAtlasH) {
            resetAtlas();
            return getGlyph(cp);   // om igen i tom atlas
        }

        [atlasTex_ replaceRegion:MTLRegionMake2D(
                                     static_cast<NSUInteger>(penX_),
                                     static_cast<NSUInteger>(penY_),
                                     static_cast<NSUInteger>(w),
                                     static_cast<NSUInteger>(h))
                     mipmapLevel:0
                       withBytes:gray.data()
                     bytesPerRow:static_cast<NSUInteger>(w)];

        g.u0 = static_cast<float>(penX_) / kAtlasW;
        g.v0 = static_cast<float>(penY_) / kAtlasH;
        g.u1 = static_cast<float>(penX_ + w) / kAtlasW;
        g.v1 = static_cast<float>(penY_ + h) / kAtlasH;
        g.w  = static_cast<float>(w);
        g.h  = static_cast<float>(h);
        g.bearingX = raster.left;   // relativt pennläget
        g.bearingY = raster.top;    // negativ ovanför baslinjen
                                    // (D3D11-konventionen)
        penX_ += w + 1;
        rowH_ = rowH_ > h ? rowH_ : h;
    }
    // tomma bounds (mellanslag m.fl.): bara advance, ingen quad

    return &glyphs_.emplace(cp, g).first->second;
}

// ---------------------------------------------------------- glyfreserven

// Kodpunktens glyf — fontens egen, en reservs eller ersättningen — och dess
// stegbredd, en gång per kodpunkt. Referensen lever lika länge som fonten
// (unordered_map flyttar aldrig sina element).
const MetalFont::Resolved& MetalFont::resolve(char32_t cp) const
{
    if (const auto it = resolved_.find(cp); it != resolved_.end())
        return it->second;
    Resolved r;
    r.ref = resolveGlyph(
        cp,
        [this](char32_t c) { return std::uint32_t{glyphIndexIn(font_, c)}; },
        [this](char32_t c, GlyphRef& ref) { return reserveFace(c, ref); });
    r.advance =
        advanceIn(faceOf(r.ref.face), static_cast<CGGlyph>(r.ref.index));
    return resolved_.emplace(cp, r).first->second;
}

// Core Texts kedja för EN kodpunkt: `CTFontCreateForString` med fontens egen
// font som bas. Underkänt: inget svar, fontens egen font, `LastResort` eller
// en font utan glyfen; en avvikande storlek ställs om till fontens. Samma
// font två gånger blir samma reserv (`CFEqual`).
bool MetalFont::reserveFace(char32_t cp, GlyphRef& out) const
{
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return false;   // ingen giltig kodpunkt — ingen kedja svarar på den
    UniChar       buf[2];
    const CFIndex n = utf16(cp, buf);
    CFStringRef   s = CFStringCreateWithCharacters(nullptr, buf, n);
    if (!s)
        return false;
    CTFontRef f = CTFontCreateForString(font_, s, CFRangeMake(0, n));
    CFRelease(s);
    if (!f)
        return false;
    if (CFEqual(f, font_) || isLastResort(f)) {
        CFRelease(f);
        return false;
    }
    if (std::abs(CTFontGetSize(f) - static_cast<CGFloat>(sizePx_)) > 0.01) {
        CTFontRef sized = CTFontCreateCopyWithAttributes(
            f, static_cast<CGFloat>(sizePx_), nullptr, nullptr);
        CFRelease(f);
        if (!sized)
            return false;
        f = sized;
    }
    const CGGlyph gi = glyphIndexIn(f, cp);
    if (gi == 0) {
        CFRelease(f);
        return false;
    }
    for (std::size_t i = 0; i < reserves_.size(); ++i)
        if (CFEqual(reserves_[i], f)) {
            CFRelease(f);
            out = GlyphRef{static_cast<int>(i) + 1, gi, true};
            return true;
        }
    reserves_.push_back(f);   // fonten äger den nu — CFRelease i unload
    out = GlyphRef{static_cast<int>(reserves_.size()), gi, true};
    return true;
}

bool MetalFont::hasGlyph(char32_t cp) const
{
    return font_ && resolve(cp).ref.real;
}

// ------------------------------------------------- rasteriseringen

// Penn-förflyttningen ur fontens designmetrik — mätningen och rastret räknar
// den på EN väg, så de är eniga också för en reservs glyf.
float MetalFont::advanceIn(CTFontRef f, CGGlyph gi) const
{
    return f ? static_cast<float>(CTFontGetAdvancesForGlyphs(
                   f, kCTFontOrientationHorizontal, &gi, nullptr, 1))
             : 0.0f;
}

// DEN rasteriseringen — glyfen som täckning i fontens storlek: en
// alfa-kontext över en TOP-DOWN-buffert (verifierat), gråskale-AA utan
// subpixel, 1 px marginal runt rasterboundsen. Atlasen (getGlyph) och
// ritytans SDF-atlas (rasterGlyph) läser samma pixlar — ur fontens egen
// font eller reservens.
bool MetalFont::rasterize(CTFontRef f, CGGlyph gi, GlyphRaster& out) const
{
    out         = GlyphRaster{};
    out.advance = advanceIn(f, gi);
    if (!f)
        return false;

    // Rasterbounds relativt baslinjen (CG: y uppåt) → heltals-bounds med
    // 1 px marginal runt om
    CGRect b = CGRectZero;
    CTFontGetBoundingRectsForGlyphs(f, kCTFontOrientationHorizontal, &gi, &b,
                                    1);
    if (!(b.size.width > 0.0 && b.size.height > 0.0))
        return true;   // tomma bounds (mellanslag m.fl.): bara advance
    const int x0 = static_cast<int>(std::floor(b.origin.x)) - 1;
    const int y0 = static_cast<int>(std::floor(b.origin.y)) - 1;
    const int x1 = static_cast<int>(std::ceil(b.origin.x + b.size.width)) + 1;
    const int y1 = static_cast<int>(std::ceil(b.origin.y + b.size.height)) + 1;
    const int w  = x1 - x0;
    const int h  = y1 - y0;

    out.coverage.assign(static_cast<std::size_t>(w) * h, 0);
    CGContextRef ctx = CGBitmapContextCreate(
        out.coverage.data(), static_cast<std::size_t>(w),
        static_cast<std::size_t>(h), 8, static_cast<std::size_t>(w), nullptr,
        kCGImageAlphaOnly);
    if (!ctx) {
        out = GlyphRaster{};
        return false;
    }
    CGContextSetShouldAntialias(ctx, true);
    // Gråskale-AA, ingen subpixel — motsvarar ClearType-medlingen
    CGContextSetShouldSmoothFonts(ctx, false);
    CGPoint origin = CGPointMake(-x0, -y0);   // baslinjen i bufferten
    CTFontDrawGlyphs(f, &gi, &origin, 1, ctx);
    CGContextRelease(ctx);
    out.width  = w;
    out.height = h;
    out.left   = static_cast<float>(x0);    // relativt pennläget
    out.top    = static_cast<float>(-y1);   // negativ ovanför baslinjen
    return true;
}

bool MetalFont::rasterGlyph(char32_t cp, GlyphRaster& out) const
{
    if (!font_) {
        out = GlyphRaster{};
        return false;
    }
    // Samma glyf som drawText: fontens egen, en reservs eller ersättningen
    // (U+FFFD, sist .notdef) — genom reservens cache.
    const GlyphRef ref = resolve(cp).ref;
    return rasterize(faceOf(ref.face), static_cast<CGGlyph>(ref.index), out);
}

// ---------------------------------------------------------------- ritning

float MetalFont::draw(MetalRenderer& r, float x, float y,
                      const std::string& utf8, Color color)
{
    if (!font_ || utf8.empty())
        return 0.0f;

    // Pixel-snappa baslinjen — glyferna är rasteriserade för heltalslägen
    const float baseX = static_cast<float>(static_cast<int>(x + 0.5f));
    const float baseY =
        static_cast<float>(static_cast<int>(y + ascent_ + 0.5f));

    float pen = 0.0f;
    const char* p   = utf8.data();
    const char* end = p + utf8.size();
    while (p < end) {
        const char32_t cp = nextCodepoint(p, end);
        const Glyph*   g  = getGlyph(cp);
        if (!g)
            continue;
        if (g->w > 0.0f) {
            const float gx = static_cast<float>(
                static_cast<int>(baseX + pen + g->bearingX + 0.5f));
            const Rectf dst{gx, baseY + g->bearingY, g->w, g->h};
            const Rectf uv{g->u0, g->v0, g->u1 - g->u0, g->v1 - g->v0};
            r.texturedQuad(atlasTex_, dst, uv, color);
        }
        pen += g->advance;
    }
    return pen;
}

// Den upplösta glyfens design-advance — rasteriserar ALDRIG. Grunden för
// hela mät-API:t; samma glyf som drawText ritar (reservens cache), så en
// saknad glyf mäts som det som faktiskt ritas.
float MetalFont::advanceOf(char32_t cp) const
{
    return font_ ? resolve(cp).advance : 0.0f;
}

Sizef MetalFont::measure(const std::string& utf8) const
{
    if (!font_ || utf8.empty())
        return Sizef{0.0f, font_ ? height_ : 0.0f};

    // Mätningen får inte rasterisera (const) — summera design-advances
    float width = 0.0f;
    const char* p   = utf8.data();
    const char* end = p + utf8.size();
    while (p < end)
        width += advanceOf(nextCodepoint(p, end));
    return Sizef{width, height_};
}

float MetalFont::prefixWidth(const std::string& utf8, std::size_t bytes) const
{
    if (!font_)
        return 0.0f;
    if (bytes > utf8.size())
        bytes = utf8.size();

    // Tecken vars STARTBYTE ligger före gränsen räknas helt (bytes
    // förväntas vara en teckengräns — kontraktet i text.h).
    float width = 0.0f;
    const char* start = utf8.data();
    const char* end   = start + utf8.size();
    const char* p     = start;
    while (p < end && static_cast<std::size_t>(p - start) < bytes)
        width += advanceOf(nextCodepoint(p, end));
    return width;
}

std::size_t MetalFont::byteIndexAtX(const std::string& utf8, float x) const
{
    if (!font_ || x <= 0.0f)
        return 0;

    // Vandra tecken för tecken: x i vänstra halvan → gränsen före, högra
    // halvan → efter. Svaret är alltid en teckengräns.
    const char* start = utf8.data();
    const char* end   = start + utf8.size();
    const char* p     = start;
    float pen = 0.0f;
    while (p < end) {
        const char* q  = p;
        const float adv = advanceOf(nextCodepoint(q, end));
        if (x < pen + adv * 0.5f)
            return static_cast<std::size_t>(p - start);
        pen += adv;
        p = q;
    }
    return utf8.size();
}

std::size_t MetalFont::fitWidth(const std::string& utf8, float maxW) const
{
    if (!font_ || maxW <= 0.0f)
        return 0;

    const char* start = utf8.data();
    const char* end   = start + utf8.size();
    const char* p     = start;
    float pen = 0.0f;
    while (p < end) {
        const char* q  = p;
        const float adv = advanceOf(nextCodepoint(q, end));
        if (pen + adv > maxW)
            return static_cast<std::size_t>(p - start);
        pen += adv;
        p = q;
    }
    return utf8.size();
}

float MetalFont::height() const
{
    return font_ ? height_ : 0.0f;
}

} // namespace gfx
