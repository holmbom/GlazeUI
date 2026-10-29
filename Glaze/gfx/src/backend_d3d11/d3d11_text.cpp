// D3dFont — gfx::Font ovanpå DirectWrite (OS-native, laddar godtyckliga
// .ttf-filer via IDWriteFontFace). Glyfer rasteriseras vid behov med
// IDWriteGlyphRunAnalysis (ren DWrite, ingen D2D): ClearType-3x1-täckningen
// medlas till gråskala och packas rad för rad i en R8-atlas på GPU:n.
// drawText blir texturerade quads ur atlasen — text är quads som allt annat.
// Rasteriseringen är EN funktion (`rasterize`) som atlasen och ritytans
// SDF-atlas (`rasterGlyph`) delar, och stilen läses ur
// fontens egen OS/2-tabell.
//
// GLYFRESERVEN: fonten är EN fil, och en kodpunkt
// den saknar (CJK, matematiska tecken …) slås upp i systemets reservkedja —
// `IDWriteFactory2::GetSystemFontFallback` → `MapCharacters` med en
// analyskälla för EN kodpunkt — och ritas och mäts med den facen. Regeln och
// ordningen står i ../font_fallback.h; svaret cachas per kodpunkt (också ett
// negativt), och mätningen, atlasen och rasterGlyph läser samma cache.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

#include "../font_catalog.h"
#include "d3d11_backend.h"
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

// ---------------------------------------------------------- glyfreserven

// Fler reservfacer än så i EN font loggas en gång — skydd mot en patologisk
// text, inte ett tak: täckningen får inte bero på textens ordning.
constexpr std::size_t kReserveLog = 64;

// ANALYSKÄLLAN för en kodpunkt (1–2 UTF-16-enheter): ett stackobjekt med
// referensräkningen som no-op — `MapCharacters` lever kortare än det.
// Vänster-till-höger, användarens språk, ingen nummersubstitution.
class OneCharSource final : public IDWriteTextAnalysisSource {
public:
    OneCharSource(const WCHAR* text, UINT32 len, const WCHAR* locale)
        : text_(text), len_(len), locale_(locale)
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
        if (iid == __uuidof(IUnknown) ||
            iid == __uuidof(IDWriteTextAnalysisSource)) {
            *out = this;
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 pos, const WCHAR** text,
                                                UINT32* len) override
    {
        *text = pos < len_ ? text_ + pos : nullptr;
        *len  = pos < len_ ? len_ - pos : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 pos,
                                                    const WCHAR** text,
                                                    UINT32* len) override
    {
        *text = pos > 0 && pos <= len_ ? text_ : nullptr;
        *len  = pos > 0 && pos <= len_ ? pos : 0;
        return S_OK;
    }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE
    GetParagraphReadingDirection() override
    {
        return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    }
    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 pos, UINT32* len,
                                            const WCHAR** locale) override
    {
        *len    = len_ - std::min(pos, len_);
        *locale = locale_;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNumberSubstitution(
        UINT32 pos, UINT32* len, IDWriteNumberSubstitution** sub) override
    {
        *len = len_ - std::min(pos, len_);
        *sub = nullptr;
        return S_OK;
    }

private:
    const WCHAR* text_;
    UINT32       len_;
    const WCHAR* locale_;
};

// Användarens språk (GetUserDefaultLocaleName) — reservkedjan väljer efter
// det (CJK-varianterna). Läst en gång per process.
const WCHAR* userLocale()
{
    static const std::wstring loc = [] {
        WCHAR buf[LOCALE_NAME_MAX_LENGTH] = {};
        return GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH) > 0
                   ? std::wstring(buf)
                   : std::wstring(L"en-US");
    }();
    return loc.c_str();
}

// En reservfaces STABILA identitet: fontfilernas
// referensnycklar (längden först, så två nycklar inte kan flyta ihop), facens
// index, simuleringarna och skalan — samma font i två svar ur kedjan blir
// samma reserv, oavsett pekaren.
std::string faceKey(IDWriteFontFace* face, float scale)
{
    std::string key;
    const auto  lägg = [&key](const void* p, std::size_t n) {
        key.append(static_cast<const char*>(p), n);
    };
    UINT32 n = 0;
    if (SUCCEEDED(face->GetFiles(&n, nullptr)) && n > 0) {
        std::vector<IDWriteFontFile*> files(n, nullptr);
        if (SUCCEEDED(face->GetFiles(&n, files.data())))
            for (IDWriteFontFile* f : files) {
                const void* ref  = nullptr;
                UINT32      size = 0;
                if (f && SUCCEEDED(f->GetReferenceKey(&ref, &size)) && ref) {
                    lägg(&size, sizeof size);
                    lägg(ref, size);
                }
                if (f)
                    f->Release();   // GetFiles lämnar en referens per fil
            }
    }
    const UINT32                    index = face->GetIndex();
    const DWRITE_FONT_SIMULATIONS   sim   = face->GetSimulations();
    lägg(&index, sizeof index);
    lägg(&sim, sizeof sim);
    lägg(&scale, sizeof scale);
    return key;
}

} // namespace

// ---------------------------------------------------------------- laddning

bool D3dFont::load(const std::string& path, float sizePt)
{
    unload();

    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(factory_.GetAddressOf()));
    if (FAILED(hr))
        return false;

    ComPtr<IDWriteFontFile> file;
    hr = factory_->CreateFontFileReference(toWide(path).c_str(), nullptr,
                                           &file);
    if (FAILED(hr)) {
        factory_.Reset();
        return false;
    }

    BOOL                 supported = FALSE;
    DWRITE_FONT_FILE_TYPE fileType  = DWRITE_FONT_FILE_TYPE_UNKNOWN;
    DWRITE_FONT_FACE_TYPE faceType  = DWRITE_FONT_FACE_TYPE_UNKNOWN;
    UINT32                faces     = 0;
    hr = file->Analyze(&supported, &fileType, &faceType, &faces);
    if (FAILED(hr) || !supported || faces == 0) {
        factory_.Reset();
        return false;
    }

    IDWriteFontFile* files[] = {file.Get()};
    hr = factory_->CreateFontFace(faceType, 1, files, 0,
                                  DWRITE_FONT_SIMULATIONS_NONE, &own_.face);
    if (FAILED(hr)) {
        factory_.Reset();
        return false;
    }
    readStyle();

    // Em-storleken tolkas som px ("ptsize" = pixelhöjd vid 72 dpi) —
    // tvärs-backend-KONTRAKTET: samma tal ger samma textmått i alla
    // backends, så layouten blir identisk på Windows/Mac/Linux.
    own_.emSize = sizePt;
    DWRITE_FONT_METRICS fm{};
    own_.face->GetMetrics(&fm);
    own_.scale = own_.emSize / static_cast<float>(fm.designUnitsPerEm);
    ascent_    = fm.ascent * own_.scale;
    height_    = (fm.ascent + fm.descent + fm.lineGap) * own_.scale;
    return true;
}

bool D3dFont::loadFamily(FontFamily f, float sizePt, FontStyle style)
{
    // Fallback-kedjan kommer ur katalogen — enda stället med sökvägar. Inom
    // en familj prövas den närmaste stilen först, och nästa familj först när
    // ingen av dess filer gick att ladda: två familjer blandas aldrig.
    for (const FontFiles& fam : systemFontCandidates(f))
        for (FontStyle s : nearestStyles(style))
            if (const char* path = fontFile(fam, s); path && load(path, sizePt))
                return true;
    return false;
}

// Stilen ur fontens OS/2-tabell: fsSelection bit 5 (BOLD) eller en vikt på
// minst 600 är fet, bit 0 (ITALIC) eller bit 9 (OBLIQUE) kursiv. Ingen
// tabell — vanlig. Vikten följer med till reservkedjan, som väljer en
// reserv i närmaste vikt.
void D3dFont::readStyle()
{
    const void* data   = nullptr;
    UINT32      size   = 0;
    void*       ctx    = nullptr;
    BOOL        exists = FALSE;
    bool        bold = false, italic = false;
    unsigned    weight = 0;
    if (SUCCEEDED(own_.face->TryGetFontTable(
            DWRITE_MAKE_OPENTYPE_TAG('O', 'S', '/', '2'), &data, &size, &ctx,
            &exists)) &&
        exists) {
        if (size >= 64) {
            const auto* b = static_cast<const std::uint8_t*>(data);
            weight             = (b[4] << 8) | b[5];
            const unsigned sel = (b[62] << 8) | b[63];
            bold   = (sel & (1u << 5)) != 0 || weight >= 600;
            italic = (sel & 1u) != 0 || (sel & (1u << 9)) != 0;
        }
        own_.face->ReleaseFontTable(ctx);
    }
    style_  = fontStyle(bold, italic);
    weight_ = weight >= 1 && weight <= 999
                  ? static_cast<DWRITE_FONT_WEIGHT>(weight)
                  : (bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
}

void D3dFont::unload()
{
    style_  = FontStyle::Regular;
    weight_ = DWRITE_FONT_WEIGHT_NORMAL;
    glyphs_.clear();
    resolved_.clear();
    reserves_.clear();
    fallback_.Reset();
    fallbackTried_  = false;
    reservesLogged_ = false;
    atlasSrv_.Reset();
    atlasTex_.Reset();
    penX_ = penY_ = rowH_ = 0;
    own_ = Face{};
    factory_.Reset();
}

// ---------------------------------------------------------------- atlasen

bool D3dFont::ensureAtlas()
{
    if (atlasTex_)
        return true;
    if (!device_)
        return false;

    D3D11_TEXTURE2D_DESC td{};
    td.Width            = kAtlasW;
    td.Height           = kAtlasH;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_DEFAULT;
    td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, &atlasTex_)))
        return false;
    if (FAILED(device_->CreateShaderResourceView(atlasTex_.Get(), nullptr,
                                                 &atlasSrv_))) {
        atlasTex_.Reset();
        return false;
    }
    return true;
}

void D3dFont::resetAtlas()
{
    // Atlasen full — börja om från tomt (glyferna rasteriseras om vid
    // behov). Händer i praktiken aldrig för UI-text med en atlas per font.
    glyphs_.clear();
    penX_ = penY_ = rowH_ = 0;
}

const D3dFont::Glyph* D3dFont::getGlyph(char32_t cp)
{
    auto it = glyphs_.find(cp);
    if (it != glyphs_.end())
        return &it->second;
    if (!own_.face || !ensureAtlas())
        return nullptr;

    // Kodpunkt → glyf genom reserven (../font_fallback.h): fontens egen, en
    // reservfonts, ersättningen — samma svar som mätningen läser.
    const GlyphRef ref = resolve(cp).ref;

    // Rasterisera täckningen (DEN delade rasteriseringen, baslinjen i origo)
    GlyphRaster raster;
    if (!rasterize(faceOf(ref.face), static_cast<UINT16>(ref.index), raster))
        return nullptr;
    Glyph g;
    g.advance   = raster.advance;
    const int w = raster.width;
    const int h = raster.height;

    if (w > 0 && h > 0) {
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

        D3D11_BOX box{static_cast<UINT>(penX_), static_cast<UINT>(penY_), 0,
                      static_cast<UINT>(penX_ + w),
                      static_cast<UINT>(penY_ + h), 1};
        context_->UpdateSubresource(atlasTex_.Get(), 0, &box, gray.data(),
                                    static_cast<UINT>(w), 0);

        g.u0 = static_cast<float>(penX_) / kAtlasW;
        g.v0 = static_cast<float>(penY_) / kAtlasH;
        g.u1 = static_cast<float>(penX_ + w) / kAtlasW;
        g.v1 = static_cast<float>(penY_ + h) / kAtlasH;
        g.w  = static_cast<float>(w);
        g.h  = static_cast<float>(h);
        g.bearingX = raster.left;   // relativt baslinjen
        g.bearingY = raster.top;    // negativ ovanför
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
const D3dFont::Resolved& D3dFont::resolve(char32_t cp) const
{
    if (const auto it = resolved_.find(cp); it != resolved_.end())
        return it->second;
    Resolved r;
    r.ref = resolveGlyph(
        cp,
        [this](char32_t c) {
            UINT32 c32 = c;
            UINT16 gi  = 0;
            own_.face->GetGlyphIndices(&c32, 1, &gi);
            return std::uint32_t{gi};
        },
        [this](char32_t c, GlyphRef& ref) { return reserveFace(c, ref); });
    r.advance = advanceIn(faceOf(r.ref.face), static_cast<UINT16>(r.ref.index));
    return resolved_.emplace(cp, r).first->second;
}

IDWriteFontFallback* D3dFont::fallback() const
{
    if (!fallbackTried_) {
        fallbackTried_ = true;
        ComPtr<IDWriteFactory2> f2;
        if (factory_ && SUCCEEDED(factory_.As(&f2)) && f2)
            f2->GetSystemFontFallback(&fallback_);
        static std::atomic<bool> loggad{false};
        if (!fallback_ && !loggad.exchange(true))
            log("D3D11: DirectWrite saknar systemets reservkedja "
                "(IDWriteFactory2, Windows 8.1+) — en glyf fonten saknar "
                "ritas som ersättningstecknet; loggas en gång");
    }
    return fallback_.Get();
}

// Systemets reservkedja för EN kodpunkt: `MapCharacters` med fontens vikt
// och stil, normal bredd och ingen basfamilj — svaret är en font (eller
// ingen), och dess glyf måste finnas. Samma font två gånger blir samma reserv
// (`faceKey`).
bool D3dFont::reserveFace(char32_t cp, GlyphRef& out) const
{
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return false;   // ingen giltig kodpunkt — ingen kedja svarar på den
    IDWriteFontFallback* fb = fallback();
    if (!fb)
        return false;
    WCHAR  text[2] = {};
    UINT32 len     = 1;
    if (cp > 0xFFFF) {
        const char32_t v = cp - 0x10000;
        text[0]          = static_cast<WCHAR>(0xD800 + (v >> 10));
        text[1]          = static_cast<WCHAR>(0xDC00 + (v & 0x3FF));
        len              = 2;
    } else {
        text[0] = static_cast<WCHAR>(cp);
    }
    OneCharSource       src(text, len, userLocale());
    UINT32              mapped = 0;
    ComPtr<IDWriteFont> font;
    FLOAT               scale = 1.0f;
    if (FAILED(fb->MapCharacters(&src, 0, len, nullptr, nullptr, weight_,
                                 isItalic(style_) ? DWRITE_FONT_STYLE_ITALIC
                                                  : DWRITE_FONT_STYLE_NORMAL,
                                 DWRITE_FONT_STRETCH_NORMAL, &mapped, &font,
                                 &scale)) ||
        !font || mapped == 0)
        return false;
    ComPtr<IDWriteFontFace> face;
    if (FAILED(font->CreateFontFace(&face)) || !face)
        return false;
    if (!std::isfinite(scale) || !(scale > 0.0f))
        scale = 1.0f;
    UINT32 c32 = cp;
    UINT16 gi  = 0;
    if (FAILED(face->GetGlyphIndices(&c32, 1, &gi)) || gi == 0)
        return false;

    const std::string key = faceKey(face.Get(), scale);
    for (std::size_t i = 0; i < reserves_.size(); ++i)
        if (reserves_[i].key == key) {
            out = GlyphRef{static_cast<int>(i) + 1, gi, true};
            return true;
        }
    DWRITE_FONT_METRICS fm{};
    face->GetMetrics(&fm);
    if (fm.designUnitsPerEm == 0)
        return false;
    Face f;
    f.face   = face;
    f.emSize = own_.emSize * scale;
    f.scale  = f.emSize / static_cast<float>(fm.designUnitsPerEm);
    f.key    = key;
    reserves_.push_back(std::move(f));
    if (reserves_.size() > kReserveLog && !reservesLogged_) {
        reservesLogged_ = true;
        log("D3D11: en font har fler än %zu reservfonter — en ovanligt "
            "blandad text; loggas en gång per font",
            kReserveLog);
    }
    out = GlyphRef{static_cast<int>(reserves_.size()), gi, true};
    return true;
}

bool D3dFont::hasGlyph(char32_t cp) const
{
    return own_.face && resolve(cp).ref.real;
}

// ------------------------------------------------- rasteriseringen

// Penn-förflyttningen ur facens designmetrik — mätningen och rastret räknar
// den på EN väg, så de är eniga också för en reservs glyf.
float D3dFont::advanceIn(const Face& f, UINT16 gi) const
{
    DWRITE_GLYPH_METRICS gm{};
    return SUCCEEDED(f.face->GetDesignGlyphMetrics(&gi, 1, &gm, FALSE))
               ? gm.advanceWidth * f.scale
               : 0.0f;
}

// DEN rasteriseringen — glyfen som täckning i fontens storlek, baslinjen i
// origo: ClearType-3x1 via GlyphRunAnalysis, medlad till gråskala. Atlasen
// (getGlyph) och ritytans SDF-atlas (rasterGlyph) läser samma pixlar — ur
// fontens egen face eller reservens.
bool D3dFont::rasterize(const Face& f, UINT16 gi, GlyphRaster& out) const
{
    out         = GlyphRaster{};
    out.advance = advanceIn(f, gi);

    const float advance = 0.0f;
    const DWRITE_GLYPH_OFFSET offset{0.0f, 0.0f};
    DWRITE_GLYPH_RUN run{};
    run.fontFace      = f.face.Get();
    run.fontEmSize    = f.emSize;
    run.glyphCount    = 1;
    run.glyphIndices  = &gi;
    run.glyphAdvances = &advance;
    run.glyphOffsets  = &offset;

    ComPtr<IDWriteGlyphRunAnalysis> analysis;
    HRESULT hr = factory_->CreateGlyphRunAnalysis(
        &run, 1.0f, nullptr, DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC,
        DWRITE_MEASURING_MODE_NATURAL, 0.0f, 0.0f, &analysis);
    if (FAILED(hr))
        return false;

    RECT b{};
    analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &b);
    const int w = b.right - b.left;
    const int h = b.bottom - b.top;
    if (w <= 0 || h <= 0)
        return true;   // tom glyf (mellanslag m.fl.): bara advance

    // ClearType-3x1 = tre subpixelvärden per pixel → medla till gråskala
    std::vector<std::uint8_t> rgb(static_cast<size_t>(w) * 3 * h);
    hr = analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &b,
                                      rgb.data(),
                                      static_cast<UINT32>(rgb.size()));
    if (FAILED(hr))
        return false;
    out.coverage.resize(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < out.coverage.size(); ++i)
        out.coverage[i] = static_cast<std::uint8_t>(
            (rgb[i * 3] + rgb[i * 3 + 1] + rgb[i * 3 + 2]) / 3);
    out.width  = w;
    out.height = h;
    out.left   = static_cast<float>(b.left);   // relativt baslinjen
    out.top    = static_cast<float>(b.top);    // negativ ovanför
    return true;
}

bool D3dFont::rasterGlyph(char32_t cp, GlyphRaster& out) const
{
    if (!own_.face) {
        out = GlyphRaster{};
        return false;
    }
    // Samma glyf som drawText: fontens egen, en reservs eller ersättningen
    // (U+FFFD, sist .notdef) — genom reservens cache.
    const GlyphRef ref = resolve(cp).ref;
    return rasterize(faceOf(ref.face), static_cast<UINT16>(ref.index), out);
}

// ---------------------------------------------------------------- ritning

float D3dFont::draw(D3dRenderer& r, float x, float y,
                    const std::string& utf8, Color color)
{
    if (!own_.face || utf8.empty())
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
            r.texturedQuad(atlasSrv_.Get(), dst, uv, color);
        }
        pen += g->advance;
    }
    return pen;
}

// Den upplösta glyfens design-advance — rasteriserar ALDRIG. Grunden för
// hela mät-API:t; samma glyf som drawText ritar (reservens cache), så en
// saknad glyf mäts som det som faktiskt ritas.
float D3dFont::advanceOf(char32_t cp) const
{
    return resolve(cp).advance;
}

Sizef D3dFont::measure(const std::string& utf8) const
{
    if (!own_.face || utf8.empty())
        return Sizef{0.0f, own_.face ? height_ : 0.0f};

    // Mätningen får inte rasterisera (const) — summera design-advances
    float width = 0.0f;
    const char* p   = utf8.data();
    const char* end = p + utf8.size();
    while (p < end)
        width += advanceOf(nextCodepoint(p, end));
    return Sizef{width, height_};
}

float D3dFont::prefixWidth(const std::string& utf8, std::size_t bytes) const
{
    if (!own_.face)
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

std::size_t D3dFont::byteIndexAtX(const std::string& utf8, float x) const
{
    if (!own_.face || x <= 0.0f)
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

std::size_t D3dFont::fitWidth(const std::string& utf8, float maxW) const
{
    if (!own_.face || maxW <= 0.0f)
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

float D3dFont::height() const
{
    return own_.face ? height_ : 0.0f;
}

} // namespace gfx
