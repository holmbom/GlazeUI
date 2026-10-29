// FtFont — gfx::Font ovanpå FreeType, med fontval via fontconfig (samma
// svar som GTK/Qt får = användarens riktiga systemfont) och font_catalog
// som fallback. Spegel av d3d11_text.cpp: glyfer rasteriseras vid behov
// (FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT → 8-bitars gråskala) och packas
// rad-för-rad i en R8-atlas; drawText blir texturerade quads ur atlasen.
// ALL mätning går via linearHoriAdvance (16.16 ohintad design-advance) —
// measure/prefixWidth rasteriserar aldrig, precis som d3d11:s advanceOf.
// Rasteriseringen är EN funktion (`rasterize`) som atlasen och ritytans
// SDF-atlas (`rasterGlyph`) delar, och stilen läses ur
// fontens egna flaggor.
//
// GLYFRESERVEN:
// fontconfigs `sans-serif` (Noto Sans på Debian) saknar t.ex. − och ≈,
// så en kodpunkt fonten saknar slås upp bland fontconfigs SORTERADE
// kandidater för fontens mönster (`FcFontSort`, otrimmad, byggd EN gång per
// mönster och process): den första vars teckenuppsättning har kodpunkten och
// som går att ladda i fontens storlek. Icke-skalbara fonter och färgfonter
// väljs bort. Regeln och ordningen står i ../font_fallback.h; svaret cachas
// per kodpunkt (också ett negativt), och mätningen, atlasen och rasterGlyph
// läser samma cache.

#include <fontconfig/fontconfig.h>

#include <cstring>
#include <map>
#include <mutex>

#include "../font_catalog.h"
#include "gfx/platform.h"
#include "linux_backend.h"

#include FT_TRUETYPE_TABLES_H   // OS/2-tabellen (readStyle) — efter ft2build.h

namespace gfx {

// ---------------------------------------------------------------- UTF-8

namespace {

// Minimal UTF-8-avkodare — kopierad från d3d11_text.cpp (ren C++):
// ogiltiga sekvenser blir U+FFFD och en byte äts (tofu, aldrig krasch).
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

// fontconfigs konfiguration — EN per process, delad av fontvalet och
// reservens kandidater.
FcConfig* fontConfig()
{
    static FcConfig* config = FcInitLoadConfigAndFonts();
    return config;
}

// fontconfig-uppslag: familjenamn ("sans-serif"/"monospace") → filsökväg +
// face-index. Regeln "fontsökvägar bara i font_catalog.cpp" bryts inte:
// fontconfig LEVERERAR sökvägar i runtime, inget hårdkodas här.
bool fontconfigMatch(const char* family, std::string& pathOut,
                     int& indexOut)
{
    FcConfig* config = fontConfig();
    if (!config)
        return false;
    FcPattern* pat = FcNameParse(
        reinterpret_cast<const FcChar8*>(family));
    if (!pat)
        return false;
    FcConfigSubstitute(config, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult   result = FcResultNoMatch;
    FcPattern* match  = FcFontMatch(config, pat, &result);
    FcPatternDestroy(pat);
    if (!match || result != FcResultMatch) {
        if (match)
            FcPatternDestroy(match);
        return false;
    }
    FcChar8* file  = nullptr;
    int      index = 0;
    const bool ok =
        FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch &&
        file;
    if (ok) {
        pathOut = reinterpret_cast<const char*>(file);
        if (FcPatternGetInteger(match, FC_INDEX, 0, &index) ==
            FcResultMatch)
            indexOut = index;
        else
            indexOut = 0;
    }
    FcPatternDestroy(match);
    return ok;
}

// Stilens del av ett fontconfig-mönster — samma som fontvalets.
const char* styleSuffix(FontStyle s)
{
    static constexpr const char* kStyle[] = {"", ":bold", ":italic",
                                             ":bold:italic"};
    return kStyle[static_cast<int>(s) & 3];
}

// ---------------------------------------------------------- glyfreserven

// EN KANDIDAT i reservkedjan: filen, facen och teckenuppsättningen (en egen
// referens, FcCharSetCopy — listan lever processen ut).
struct Candidate {
    std::string file;
    int         index   = 0;
    FcCharSet*  charset = nullptr;
};

// Kandidaterna för ett mönster: fontconfigs SORTERADE lista efter
// substitutionen (`FcFontSort` med trim = FcFalse — en trimning tar bort
// senare fonter vars täckning redan finns i en föregående, och filtreras den
// föregående därefter bort är alternativet borta), FC_FILE/FC_INDEX/FC_CHARSET
// direkt ur setets mönster (inga rendringstips — de är våra egna
// FreeType-flaggor). Icke-skalbara fonter och färgfonter väljs bort. Byggd
// EN gång per mönster och process, bakom ett lås.
const std::vector<Candidate>& candidates(const std::string& pattern)
{
    static std::mutex                                    lås;
    static std::map<std::string, std::vector<Candidate>> lista;
    const std::lock_guard<std::mutex>                    l(lås);
    auto [it, ny] = lista.try_emplace(pattern);
    if (!ny)
        return it->second;
    FcConfig* config = fontConfig();
    if (!config)
        return it->second;
    FcPattern* pat =
        FcNameParse(reinterpret_cast<const FcChar8*>(pattern.c_str()));
    if (!pat)
        return it->second;
    FcConfigSubstitute(config, pat, FcMatchPattern);
    FcDefaultSubstitute(pat);
    FcResult   result = FcResultNoMatch;
    FcFontSet* set    = FcFontSort(config, pat, FcFalse, nullptr, &result);
    FcPatternDestroy(pat);
    if (!set)
        return it->second;
    for (int i = 0; i < set->nfont; ++i) {
        FcPattern* f        = set->fonts[i];
        FcBool     scalable = FcTrue;
        if (FcPatternGetBool(f, FC_SCALABLE, 0, &scalable) == FcResultMatch &&
            scalable == FcFalse)
            continue;
#ifdef FC_COLOR
        FcBool color = FcFalse;
        if (FcPatternGetBool(f, FC_COLOR, 0, &color) == FcResultMatch &&
            color == FcTrue)
            continue;
#endif
        FcChar8*   file  = nullptr;
        FcCharSet* cs    = nullptr;
        int        index = 0;
        if (FcPatternGetString(f, FC_FILE, 0, &file) != FcResultMatch || !file ||
            FcPatternGetCharSet(f, FC_CHARSET, 0, &cs) != FcResultMatch || !cs)
            continue;
        if (FcPatternGetInteger(f, FC_INDEX, 0, &index) != FcResultMatch)
            index = 0;
        it->second.push_back(Candidate{reinterpret_cast<const char*>(file),
                                       index, FcCharSetCopy(cs)});
    }
    FcFontSetDestroy(set);
    return it->second;
}

} // namespace

// ---------------------------------------------------------------- laddning

bool FtFont::load(const std::string& path, float sizePt, int faceIndex)
{
    unload();

    if (FT_Init_FreeType(&library_) != 0) {
        library_ = nullptr;
        return false;
    }
    if (FT_New_Face(library_, path.c_str(), faceIndex, &face_) != 0) {
        face_ = nullptr;
        FT_Done_FreeType(library_);
        library_ = nullptr;
        return false;
    }
    // Em-storleken tolkas som px — fasadens konvention (d3d11_text.cpp:
    // samma tolkning), så måtten matchar Windows/Mac.
    if (FT_Set_Pixel_Sizes(face_, 0,
                           static_cast<FT_UInt>(sizePt + 0.5f)) != 0) {
        unload();
        return false;
    }
    sizePx_ = sizePt;
    ascent_ = static_cast<float>(face_->size->metrics.ascender) / 64.0f;
    height_ = static_cast<float>(face_->size->metrics.height) / 64.0f;
    style_  = readStyle();
    // Reservens nyckel: filen som inte ska sökas igen, och ett mönster för
    // en fil utan familj (`loadFont(path)`) — fast bredd ⇒ "monospace",
    // annars "sans-serif", i filens stil. `loadFamily` sätter sitt eget.
    path_      = path;
    faceIndex_ = faceIndex;
    pattern_   = std::string(FT_IS_FIXED_WIDTH(face_) ? "monospace"
                                                      : "sans-serif") +
               styleSuffix(style_);
    return true;
}

// Stilen ur fontens egna flaggor: FreeTypes style_flags (head/OS/2-bitarna)
// och — som D3D11 — en OS/2-vikt på minst 600 som fet.
FontStyle FtFont::readStyle() const
{
    bool bold         = (face_->style_flags & FT_STYLE_FLAG_BOLD) != 0;
    const bool italic = (face_->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
    if (const auto* os2 =
            static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(face_, FT_SFNT_OS2)))
        bold = bold || os2->usWeightClass >= 600;
    return fontStyle(bold, italic);
}

bool FtFont::loadFamily(FontFamily f, float sizePt, FontStyle style)
{
    // fontconfig först (systemets riktiga UI-font, i den begärda stilen —
    // fontconfig svarar alltid med NÅGOT, och style() läser vad det blev),
    // katalogen som fallback (fontconfig trasigt/cachelöst → de statiska
    // DejaVu/Noto-sökvägarna, närmaste stil inom en familj i taget). Mönstret
    // är också reservens: kandidaterna sorteras mot det.
    std::string pattern = f == FontFamily::Mono ? "monospace" : "sans-serif";
    pattern += styleSuffix(style);
    std::string path;
    int         index = 0;
    if (fontconfigMatch(pattern.c_str(), path, index) &&
        load(path, sizePt, index)) {
        pattern_ = pattern;
        return true;
    }
    for (const FontFiles& fam : systemFontCandidates(f))
        for (FontStyle s : nearestStyles(style))
            if (const char* file = fontFile(fam, s); file && load(file, sizePt)) {
                pattern_ = pattern;
                return true;
            }
    return false;
}

void FtFont::unload()
{
    style_ = FontStyle::Regular;
    glyphs_.clear();
    resolved_.clear();
    if (atlasTex_ && renderer_)
        renderer_->destroyTexture(atlasTex_);
    atlasTex_ = 0;
    penX_ = penY_ = rowH_ = 0;
    // Reserverna bor i fontens egen FreeType-instans — släpps FÖRE den.
    for (Reserve& r : reserves_)
        if (r.face)
            FT_Done_Face(r.face);
    reserves_.clear();
    path_.clear();
    faceIndex_ = 0;
    pattern_.clear();
    if (face_) {
        FT_Done_Face(face_);
        face_ = nullptr;
    }
    if (library_) {
        FT_Done_FreeType(library_);
        library_ = nullptr;
    }
}

// ---------------------------------------------------------------- atlasen

bool FtFont::ensureAtlas()
{
    if (atlasTex_)
        return true;
    if (!renderer_)
        return false;
    atlasTex_ = renderer_->createTexture(ImageFormat::R8, kAtlasW, kAtlasH,
                                         nullptr, 0, false);
    return atlasTex_ != 0;
}

void FtFont::resetAtlas()
{
    // Atlasen full — börja om från tomt (glyferna rasteriseras om vid
    // behov). Händer i praktiken aldrig för UI-text med en atlas per font.
    glyphs_.clear();
    penX_ = penY_ = rowH_ = 0;
}

const FtFont::Glyph* FtFont::getGlyph(char32_t cp)
{
    auto it = glyphs_.find(cp);
    if (it != glyphs_.end())
        return &it->second;
    if (!face_ || !ensureAtlas())
        return nullptr;

    // Kodpunkt → glyf genom reserven (../font_fallback.h): fontens egen, en
    // reservfonts, ersättningen — samma svar som mätningen läser.
    const GlyphRef ref = resolve(cp).ref;
    GlyphRaster raster;
    if (!rasterize(faceOf(ref.face), ref.index, raster))
        return nullptr;
    Glyph g;
    g.advance   = raster.advance;
    const int w = raster.width;
    const int h = raster.height;
    if (w > 0 && h > 0) {
        // Packa in i atlasen, rad för rad (1 px luft mot grannglyfer).
        if (penX_ + w + 1 > kAtlasW) {
            penX_ = 0;
            penY_ += rowH_ + 1;
            rowH_ = 0;
        }
        if (penY_ + h + 1 > kAtlasH) {
            resetAtlas();
            return getGlyph(cp);   // om igen i tom atlas
        }
        renderer_->updateTexture(atlasTex_, ImageFormat::R8, penX_, penY_, w,
                                 h, raster.coverage.data(),
                                 static_cast<std::size_t>(w));

        g.u0 = static_cast<float>(penX_) / kAtlasW;
        g.v0 = static_cast<float>(penY_) / kAtlasH;
        g.u1 = static_cast<float>(penX_ + w) / kAtlasW;
        g.v1 = static_cast<float>(penY_ + h) / kAtlasH;
        g.w  = static_cast<float>(w);
        g.h  = static_cast<float>(h);
        g.bearingX = raster.left;
        g.bearingY = raster.top;   // negativt ovanför baslinjen
        penX_ += w + 1;
        rowH_ = rowH_ > h ? rowH_ : h;
    }
    // w/h == 0 (mellanslag m.fl.): bara advance, ingen quad

    return &glyphs_.emplace(cp, g).first->second;
}

// ---------------------------------------------------------- glyfreserven

// Kodpunktens glyf — fontens egen, en reservs eller ersättningen — och dess
// stegbredd, en gång per kodpunkt. Referensen lever lika länge som fonten
// (unordered_map flyttar aldrig sina element).
const FtFont::Resolved& FtFont::resolve(char32_t cp) const
{
    if (const auto it = resolved_.find(cp); it != resolved_.end())
        return it->second;
    Resolved r;
    r.ref = resolveGlyph(
        cp,
        [this](char32_t c) {
            return static_cast<std::uint32_t>(FT_Get_Char_Index(face_, c));
        },
        [this](char32_t c, GlyphRef& ref) { return reserveFace(c, ref); });
    r.advance = advanceIn(faceOf(r.ref.face), r.ref.index);
    return resolved_.emplace(cp, r).first->second;
}

// Den första kandidaten för fontens mönster vars teckenuppsättning har
// kodpunkten, som inte är fontens egen fil och som går att ladda i fontens
// storlek — och där FreeType faktiskt hittar glyfen. En kandidat som inte
// gick att ladda står kvar utan face, så den inte prövas igen.
bool FtFont::reserveFace(char32_t cp, GlyphRef& out) const
{
    if (cp > 0x10FFFF || pattern_.empty() || !library_)
        return false;
    for (const Candidate& c : candidates(pattern_)) {
        if (!FcCharSetHasChar(c.charset, static_cast<FcChar32>(cp)) ||
            (c.file == path_ && c.index == faceIndex_))
            continue;
        std::size_t slot = reserves_.size();
        for (std::size_t i = 0; i < reserves_.size(); ++i)
            if (reserves_[i].file == c.file && reserves_[i].index == c.index) {
                slot = i;
                break;
            }
        if (slot == reserves_.size()) {
            FT_Face f = nullptr;
            if (FT_New_Face(library_, c.file.c_str(), c.index, &f) != 0)
                f = nullptr;
            else if (FT_Set_Pixel_Sizes(
                         f, 0, static_cast<FT_UInt>(sizePx_ + 0.5f)) != 0) {
                FT_Done_Face(f);
                f = nullptr;
            }
            reserves_.push_back(Reserve{f, c.file, c.index});
        }
        const FT_Face f = reserves_[slot].face;
        if (!f)
            continue;
        const FT_UInt gi = FT_Get_Char_Index(f, cp);
        if (gi == 0)
            continue;
        out = GlyphRef{static_cast<int>(slot) + 1,
                       static_cast<std::uint32_t>(gi), true};
        return true;
    }
    return false;
}

bool FtFont::hasGlyph(char32_t cp) const
{
    return face_ && resolve(cp).ref.real;
}

// ------------------------------------------------- rasteriseringen

// Design-advancen (linearHoriAdvance, 16.16, ohintad) för glyfen i facen —
// FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP, utan rendering. Mätningen och
// rastret räknar den på EN väg, så de är eniga också för en reservs glyf.
float FtFont::advanceIn(FT_Face face, FT_UInt gi) const
{
    if (!face ||
        FT_Load_Glyph(face, gi, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0)
        return 0.0f;
    return static_cast<float>(face->glyph->linearHoriAdvance) / 65536.0f;
}

// DEN rasteriseringen — glyfen som täckning i fontens storlek: TARGET_LIGHT
// (lätt hinting i x-led, ingen i y — jämn gråskala som DWrite:s medlade
// ClearType, utseendeparitet). Atlasen (getGlyph) och ritytans SDF-atlas
// (rasterGlyph) läser samma pixlar; raderna kopieras täta (pitch bort).
bool FtFont::rasterize(FT_Face face, FT_UInt gi, GlyphRaster& out) const
{
    out         = GlyphRaster{};
    out.advance = advanceIn(face, gi);   // FÖRE renderingen — samma Load-plats
    if (!face || FT_Load_Glyph(face, gi, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) != 0)
        return false;

    const FT_Bitmap& bm = face->glyph->bitmap;
    const int w = static_cast<int>(bm.width);
    const int h = static_cast<int>(bm.rows);
    if (w <= 0 || h <= 0 || !bm.buffer || bm.pixel_mode != FT_PIXEL_MODE_GRAY)
        return true;   // tom glyf (mellanslag m.fl.): bara advance
    out.coverage.resize(static_cast<std::size_t>(w) * h);
    const int pitch = bm.pitch != 0 ? bm.pitch : w;
    for (int y = 0; y < h; ++y) {
        // En negativ pitch betyder rader nerifrån och upp — rad 0 överst ut.
        const unsigned char* row =
            pitch > 0 ? bm.buffer + static_cast<std::size_t>(y) * pitch
                      : bm.buffer + static_cast<std::size_t>(h - 1 - y) * -pitch;
        std::memcpy(out.coverage.data() + static_cast<std::size_t>(y) * w, row,
                    static_cast<std::size_t>(w));
    }
    out.width = w;
    out.height = h;
    out.left = static_cast<float>(face->glyph->bitmap_left);
    // FT: bitmap_top = avstånd baslinje → översta raden (positivt uppåt);
    // vårt top är negativt ovanför baslinjen (som DWrite-analysens b.top).
    out.top = -static_cast<float>(face->glyph->bitmap_top);
    return true;
}

bool FtFont::rasterGlyph(char32_t cp, GlyphRaster& out) const
{
    if (!face_) {
        out = GlyphRaster{};
        return false;
    }
    // Samma glyf som drawText: fontens egen, en reservs eller ersättningen
    // (U+FFFD, sist .notdef) — genom reservens cache.
    const GlyphRef ref = resolve(cp).ref;
    return rasterize(faceOf(ref.face), ref.index, out);
}

// ---------------------------------------------------------------- ritning

float FtFont::draw(GlRenderer& r, float x, float y,
                   const std::string& utf8, Color color)
{
    if (!face_ || utf8.empty())
        return 0.0f;

    // Pixel-snappa baslinjen — glyferna är rasteriserade för heltalslägen.
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

// Den upplösta glyfens design-advance — rasteriserar ALDRIG (reservens
// cache bär den). Samma glyf som drawText ritar, så en saknad glyf mäts som
// det som faktiskt ritas.
float FtFont::advanceOf(char32_t cp) const
{
    return face_ ? resolve(cp).advance : 0.0f;
}

Sizef FtFont::measure(const std::string& utf8) const
{
    if (!face_ || utf8.empty())
        return Sizef{0.0f, face_ ? height_ : 0.0f};

    float width = 0.0f;
    const char* p   = utf8.data();
    const char* end = p + utf8.size();
    while (p < end)
        width += advanceOf(nextCodepoint(p, end));
    return Sizef{width, height_};
}

float FtFont::prefixWidth(const std::string& utf8, std::size_t bytes) const
{
    if (!face_)
        return 0.0f;
    if (bytes > utf8.size())
        bytes = utf8.size();

    float width = 0.0f;
    const char* start = utf8.data();
    const char* end   = start + utf8.size();
    const char* p     = start;
    while (p < end && static_cast<std::size_t>(p - start) < bytes)
        width += advanceOf(nextCodepoint(p, end));
    return width;
}

std::size_t FtFont::byteIndexAtX(const std::string& utf8, float x) const
{
    if (!face_ || x <= 0.0f)
        return 0;

    const char* start = utf8.data();
    const char* end   = start + utf8.size();
    const char* p     = start;
    float pen = 0.0f;
    while (p < end) {
        const char* q   = p;
        const float adv = advanceOf(nextCodepoint(q, end));
        if (x < pen + adv * 0.5f)
            return static_cast<std::size_t>(p - start);
        pen += adv;
        p = q;
    }
    return utf8.size();
}

std::size_t FtFont::fitWidth(const std::string& utf8, float maxW) const
{
    if (!face_ || maxW <= 0.0f)
        return 0;

    const char* start = utf8.data();
    const char* end   = start + utf8.size();
    const char* p     = start;
    float pen = 0.0f;
    while (p < end) {
        const char* q   = p;
        const float adv = advanceOf(nextCodepoint(q, end));
        if (pen + adv > maxW)
            return static_cast<std::size_t>(p - start);
        pen += adv;
        p = q;
    }
    return utf8.size();
}

float FtFont::height() const
{
    return face_ ? height_ : 0.0f;
}

} // namespace gfx
