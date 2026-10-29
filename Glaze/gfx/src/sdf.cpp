// Ritytans text och markörer — backend-neutralt.
// Kontraktet står i include/gfx/sdf.h; här är tre delar:
//
//  1. AVSTÅNDSFÄLTET (`makeSdf`): rastret trösklas vid täckningen 0,5, och
//     två exakta euklidiska avståndstransformer (Felzenszwalb–Huttenlocher,
//     separerbar: kolumnerna, sedan raderna) ger varje pixels avstånd till
//     närmaste pixel på andra sidan. Gränsen ligger en HALV pixel från båda
//     pixelcentrumen, så avståndet till kanten är centrumavståndet − ½ —
//     exakt för raka kanter, inom en femtedels pixel i hörn. Fältet räknas i
//     rastrets upplösning (`scale` × fältets) och tas ned genom att läsas mitt
//     i varje fälttexel.
//  2. ATLASEN (`SdfAtlas`): EN per fontfamilj, med
//     en font per stil (den vanliga i konstruktorn, resten lat) och glyfernas
//     nyckel (stil, kodpunkt); hyllpackning med en texels luft på R8-sidor om
//     1024² — en full sida ger en ny, aldrig en vräkning.
//  3. BYGGARNA: etikettens DELAR (färg och stil per del) i en platt
//     glyflista, raderna med `gfx::wrapText`s regel över den (`layLines`),
//     ankare och justering i atlasens texlar (= referens-px), omräknade till
//     em i vertexen; plattan, alla glorior och alla fyllningar i den
//     ordningen också över atlassidor; markörerna som former. LAYOUTEN —
//     glyflistan, raderna, radmåtten, blocket och lådan — är `layoutText`,
//     delad med MÄTNINGEN (`measureText`), som tar stegen ur
//     `SdfAtlas::advance` i stället för glyferna och aldrig laddar upp.

#include "gfx/sdf.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include "gfx/platform.h"
#include "gfx/renderer.h"

namespace gfx {
namespace {

constexpr double kInf = 1e20;
constexpr double kPi  = 3.14159265358979323846;

// Vinkelns ram som quadarnas flaggor: kartans riktning projicerad
// (`kMapAligned`), eller quaden LIGGANDE i världsplanet med kartans vinkel
// (`kGroundAligned` — programmet läser bara den
// flaggan då); skärmens är ingen flagga.
std::uint32_t rotationFlags(TextRotation r)
{
    switch (r) {
    case TextRotation::Map:      return glyphflag::kMapAligned;
    case TextRotation::Ground:   return glyphflag::kGroundAligned;
    case TextRotation::Viewport: break;
    }
    return 0u;
}

// Kvadrerat avstånd längs EN linje: d[q] = min över p av (q − p)² + f[p]
// (Felzenszwalb–Huttenlocher, nedre höljet av paraboler). `v` och `z` är
// arbetsminne med plats för n respektive n + 1. "Långt bort" i f är kInf
// (ändligt, så skärningarna förblir tal); höljets ändar är riktiga
// oändligheter, så s alltid ligger över z[0] och slingan stannar där.
void edt1d(const double* f, int n, double* d, int* v, double* z)
{
    constexpr double kOo = std::numeric_limits<double>::infinity();
    int k = 0;
    v[0]  = 0;
    z[0]  = -kOo;
    z[1]  = kOo;
    for (int q = 1; q < n; ++q) {
        double s;
        for (;;) {
            const int p = v[k];
            s = ((f[q] + double(q) * q) - (f[p] + double(p) * p)) /
                (2.0 * q - 2.0 * p);
            if (s > z[k])
                break;
            --k;
        }
        ++k;
        v[k]     = q;
        z[k]     = s;
        z[k + 1] = kOo;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < q)
            ++k;
        const double dq = q - v[k];
        d[q] = dq * dq + f[v[k]];
    }
}

// Kvadrerat avstånd i 2D, på plats: kolumnerna först, sedan raderna.
void edt2d(std::vector<double>& g, int w, int h)
{
    const int        n = std::max(w, h);
    std::vector<double> f(n), d(n), z(n + 1);
    std::vector<int>    v(n);
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y)
            f[y] = g[static_cast<std::size_t>(y) * w + x];
        edt1d(f.data(), h, d.data(), v.data(), z.data());
        for (int y = 0; y < h; ++y)
            g[static_cast<std::size_t>(y) * w + x] = d[y];
    }
    for (int y = 0; y < h; ++y) {
        double* row = &g[static_cast<std::size_t>(y) * w];
        std::copy(row, row + w, f.begin());
        edt1d(f.data(), w, d.data(), v.data(), z.data());
        std::copy(d.begin(), d.begin() + w, row);
    }
}

int floorDiv(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

// Minimal UTF-8-avkodare — ogiltiga sekvenser blir U+FFFD och en byte äts
// (samma regel som backendarnas, så texten blir densamma).
char32_t nextCodepoint(std::string_view s, std::size_t& i)
{
    const auto b0 = static_cast<unsigned char>(s[i++]);
    if (b0 < 0x80)
        return b0;
    int      len = 0;
    char32_t cp  = 0;
    if ((b0 & 0xE0) == 0xC0) {
        len = 1;
        cp  = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
        len = 2;
        cp  = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
        len = 3;
        cp  = b0 & 0x07;
    } else {
        return 0xFFFD;
    }
    for (int k = 0; k < len; ++k) {
        if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
            return 0xFFFD;
        cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
    }
    return cp;
}

// Kodpunkten som UTF-8 — `nextCodepoint` baklänges, så `Font::measure` får
// exakt den kodpunkt byggaren hade rasteriserat (också det avkodaren låter
// igenom: surrogater och upp till 21 bitar).
std::string utf8Of(char32_t cp)
{
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | ((cp >> 18) & 0x07));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

// Glyfens nyckel i atlasen: (stil, kodpunkt) — samma kodpunkt i vanlig och
// fet är två glyfer, också när fet ritas med den vanliga fonten (syntesen).
std::uint64_t glyphKey(char32_t cp, FontStyle s)
{
    return (static_cast<std::uint64_t>(static_cast<unsigned>(s) & 3u) << 32) | cp;
}

} // namespace

// ------------------------------------------------------------- fältet

void makeSdf(const GlyphRaster& in, int radius, int scale, SdfField& out)
{
    out = SdfField{};
    if (in.width <= 0 || in.height <= 0 || scale < 1 || radius < 1 ||
        in.coverage.size() < static_cast<std::size_t>(in.width) * in.height)
        return;

    // Fältets texlar relativt pennan: glyfens låda i fältets upplösning
    // (utåt avrundad) plus marginalen.
    const int rl = static_cast<int>(std::lround(in.left));
    const int rt = static_cast<int>(std::lround(in.top));
    const int fl = floorDiv(rl, scale) - radius;
    const int ft = floorDiv(rt, scale) - radius;
    const int fr = floorDiv(rl + in.width + scale - 1, scale) + radius;
    const int fb = floorDiv(rt + in.height + scale - 1, scale) + radius;
    const int fw = fr - fl, fh = fb - ft;

    // Rastret i ett rutnät som täcker fältet, i rastrets upplösning.
    const int hw = fw * scale, hh = fh * scale;
    const int dx = rl - fl * scale, dy = rt - ft * scale;
    std::vector<double> outer(static_cast<std::size_t>(hw) * hh, kInf);
    std::vector<double> inner(static_cast<std::size_t>(hw) * hh, 0.0);
    for (int y = 0; y < in.height; ++y)
        for (int x = 0; x < in.width; ++x)
            if (in.coverage[static_cast<std::size_t>(y) * in.width + x] >= 128) {
                const std::size_t j =
                    static_cast<std::size_t>(y + dy) * hw + (x + dx);
                outer[j] = 0.0;    // inne: inget avstånd till insidan
                inner[j] = kInf;   // … och långt till utsidan
            }
    edt2d(outer, hw, hh);   // kvadrerat avstånd till närmaste inne-pixel
    edt2d(inner, hw, hh);   // … till närmaste ute-pixel

    // Det signerade avståndet till kanten i rastrets pixlar, positivt inne:
    // centrumavståndet till andra sidan − ½ (gränsen ligger mitt emellan).
    const auto sd = [&](int x, int y) {
        x = std::clamp(x, 0, hw - 1);
        y = std::clamp(y, 0, hh - 1);
        const std::size_t j = static_cast<std::size_t>(y) * hw + x;
        return inner[j] > 0.0 ? std::sqrt(inner[j]) - 0.5
                              : -(std::sqrt(outer[j]) - 0.5);
    };

    out.width  = fw;
    out.height = fh;
    out.left   = fl;
    out.top    = ft;
    out.values.resize(static_cast<std::size_t>(fw) * fh);
    for (int ty = 0; ty < fh; ++ty) {
        for (int tx = 0; tx < fw; ++tx) {
            // Fälttexelns mitt i rastrets koordinater: scale × (t + ½). Med
            // jämnt scale ligger den mellan fyra pixlar — medelvärdet av dem
            // är den bilinjära läsningen där; med scale 1 är det pixeln själv.
            double d;
            if (scale % 2 == 0) {
                const int cx = tx * scale + scale / 2;
                const int cy = ty * scale + scale / 2;
                d = (sd(cx - 1, cy - 1) + sd(cx, cy - 1) + sd(cx - 1, cy) +
                     sd(cx, cy)) / 4.0;
            } else {
                d = sd(tx * scale + scale / 2, ty * scale + scale / 2);
            }
            d /= scale;   // i fältets texlar
            const double v = std::clamp(0.5 + d / (2.0 * radius), 0.0, 1.0);
            out.values[static_cast<std::size_t>(ty) * fw + tx] =
                static_cast<std::uint8_t>(std::lround(v * 255.0));
        }
    }
}

// ------------------------------------------------------------- atlasen

SdfAtlas::SdfAtlas(Renderer& r, FontFamily family) : r_(r), family_(family)
{
    // Den vanliga stilen laddas här — `valid()` avser den; de andra lat.
    fonts_[0] = r.loadFont(family, kSdfRefSize * kSdfSupersample,
                           FontStyle::Regular);
    tried_[0] = true;
}

const Font* SdfAtlas::font(FontStyle s)
{
    if (!valid())
        return nullptr;
    const auto i = static_cast<std::size_t>(s) & 3u;
    if (!tried_[i]) {
        // Laddas i den begärda stilen (eller den närmaste inom familjen — se
        // `Renderer::loadFont`); en som inte går att ladda lämnar platsen tom,
        // och den vanliga ritar.
        tried_[i] = true;
        std::unique_ptr<Font> f =
            r_.loadFont(family_, kSdfRefSize * kSdfSupersample, s);
        if (f && f->valid())
            fonts_[i] = std::move(f);
    }
    return fonts_[i] ? fonts_[i].get() : fonts_[0].get();
}

FontStyle SdfAtlas::style(FontStyle s)
{
    const Font* f = font(s);
    return f ? f->style() : FontStyle::Regular;
}

float SdfAtlas::ascent(FontStyle s)
{
    const Font* f = font(s);
    return f ? f->ascent() / kSdfSupersample : 0.0f;
}

float SdfAtlas::lineHeight(FontStyle s)
{
    const Font* f = font(s);
    return f ? f->height() / kSdfSupersample : 0.0f;
}

void SdfAtlas::clear()
{
    glyphs_.clear();
    pages_.clear();
    penX_ = penY_ = rowH_ = 0;
}

// En plats w × h på sista sidan (en texels luft mot grannarna); är den full
// tas en ny sida.
bool SdfAtlas::place(int w, int h, int& page, int& x, int& y)
{
    if (w + 1 > kSdfPageSize || h + 1 > kSdfPageSize)
        return false;
    if (!pages_.empty() && penX_ + w + 1 > kSdfPageSize) {
        penX_ = 0;
        penY_ += rowH_ + 1;
        rowH_ = 0;
    }
    if (pages_.empty() || penY_ + h + 1 > kSdfPageSize) {
        auto pg = r_.createImage(kSdfPageSize, kSdfPageSize, ImageFormat::R8,
                                 nullptr, 0);
        if (!pg->valid()) {
            log("SdfAtlas: atlassidan (%d²) kunde inte skapas", kSdfPageSize);
            return false;
        }
        pages_.push_back(std::move(pg));
        penX_ = penY_ = rowH_ = 0;
    }
    page = static_cast<int>(pages_.size()) - 1;
    x    = penX_;
    y    = penY_;
    penX_ += w + 1;
    rowH_ = std::max(rowH_, h);
    return true;
}

const SdfAtlas::Glyph* SdfAtlas::glyph(char32_t cp, FontStyle s)
{
    const std::uint64_t key = glyphKey(cp, s);
    if (const auto it = glyphs_.find(key); it != glyphs_.end())
        return &it->second;
    const Font* f = font(s);
    if (!f)
        return nullptr;

    GlyphRaster raster;
    if (!f->rasterGlyph(cp, raster))
        return nullptr;
    Glyph g;
    g.advance = raster.advance / kSdfSupersample;
    SdfField field;
    makeSdf(raster, kSdfRadius, kSdfSupersample, field);
    if (field.width > 0 && field.height > 0) {
        int page = 0, x = 0, y = 0;
        if (!place(field.width, field.height, page, x, y) ||
            !r_.updateImage(*pages_[page], x, y, field.width, field.height,
                            field.values.data(), 0))
            return nullptr;
        constexpr float texel = 1.0f / kSdfPageSize;
        g.page = page;
        g.u0   = x * texel;
        g.v0   = y * texel;
        g.u1   = (x + field.width) * texel;
        g.v1   = (y + field.height) * texel;
        g.left = static_cast<float>(field.left);
        g.top  = static_cast<float>(field.top);
        g.w    = static_cast<float>(field.width);
        g.h    = static_cast<float>(field.height);
    }
    return &glyphs_.emplace(key, g).first->second;
}

float SdfAtlas::advance(char32_t cp, FontStyle s)
{
    // En BYGGD glyfs steg först — det layouten ritar med — och annars
    // fontens mätning, som ALDRIG rasteriserar: samma upplösta glyf och
    // samma steg som `rasterGlyph` (backendarnas `advanceIn`).
    const std::uint64_t key = glyphKey(cp, s);
    if (const auto it = glyphs_.find(key); it != glyphs_.end())
        return it->second.advance;
    if (const auto it = advances_.find(key); it != advances_.end())
        return it->second;
    const Font* f = font(s);
    if (!f)
        return 0.0f;
    const float a = f->measure(utf8Of(cp)).w / kSdfSupersample;
    advances_.emplace(key, a);
    return a;
}

// ------------------------------------------------------------- byggarna

namespace {

// Chunken nästa quad hamnar i — dess INDEX: den senaste från `first` och
// framåt som samplar samma sida (eller ingen) och har plats, annars en ny med
// origo i etikettens ankare. `first` håller EN etiketts ordning över
// atlassidor: chunkarna ritas i
// sin ordning, och utan gränsen hade en fyllning på sida 0 kunnat hamna i en
// äldre chunk än glorian på sida 1 — och ritats under den.
std::size_t chunkFor(std::vector<GlyphChunk>& chunks, int page,
                     const Point2d& anchor, std::size_t first = 0)
{
    for (std::size_t i = chunks.size(); i-- > first;) {
        GlyphChunk& c = chunks[i];
        if (c.vertices.size() + 4 > kMaxChunkVertices)
            break;   // en full chunk — tidigare chunkar ska inte få mer
        if (page < 0 || c.page < 0 || c.page == page) {
            if (page >= 0)
                c.page = page;
            return i;
        }
    }
    GlyphChunk c;
    c.originX = anchor.x;
    c.originY = anchor.y;
    c.page    = page;
    chunks.push_back(std::move(c));
    return chunks.size() - 1;
}

// En quad: hörnen (x0, y0)–(x1, y1) i em relativt ankaret, med `uv0`–`uv1`
// i samma hörnordning, och skjuvningen `shear` kring baslinjen `yb` (em).
// Hörnordningen är (x0,y0), (x1,y0), (x0,y1), (x1,y1) — index 0-1-2, 2-1-3.
void pushQuad(GlyphChunk& c, const Point2d& anchor, float x0, float y0,
              float x1, float y1, float u0, float v0, float u1, float v1,
              float shear, float yb, const GlyphVertex& proto)
{
    const float ax  = static_cast<float>(anchor.x - c.originX);
    const float ay  = static_cast<float>(anchor.y - c.originY);
    const auto  base = static_cast<std::uint32_t>(c.vertices.size());
    const float xs[4] = {x0, x1, x0, x1};
    const float ys[4] = {y0, y0, y1, y1};
    const float us[4] = {u0, u1, u0, u1};
    const float vs[4] = {v0, v0, v1, v1};
    for (int k = 0; k < 4; ++k) {
        GlyphVertex g = proto;
        g.ax = ax;
        g.ay = ay;
        g.ox = xs[k] - shear * (ys[k] - yb);
        g.oy = ys[k];
        g.u  = us[k];
        g.v  = vs[k];
        c.vertices.push_back(g);
    }
    c.indices.insert(c.indices.end(),
                     {base, base + 1, base + 2, base + 2, base + 1, base + 3});
}

} // namespace

// ANKARET: lådans övre vänstra hörn relativt ankarpunkten, för en låda w × h
// (y nedåt) — samma nio lägen för texten, markörerna och ritytans förankrade
// barn (sdf.h).
void anchorCorner(TextAnchor a, float w, float h, float& x, float& y)
{
    switch (a) {
    case TextAnchor::Top:
    case TextAnchor::Center:
    case TextAnchor::Bottom:      x = -w / 2.0f; break;
    case TextAnchor::TopRight:
    case TextAnchor::Right:
    case TextAnchor::BottomRight: x = -w; break;
    default:                      x = 0.0f; break;
    }
    switch (a) {
    case TextAnchor::Left:
    case TextAnchor::Center:
    case TextAnchor::Right:       y = -h / 2.0f; break;
    case TextAnchor::BottomLeft:
    case TextAnchor::Bottom:
    case TextAnchor::BottomRight: y = -h; break;
    default:                      y = 0.0f; break;
    }
}

namespace {

// Takets ellips — `Draw::elideLines` tecken, typografi och inget mått.
constexpr char32_t kEllipsis = U'…';

// EN GLYF i etikettens platta lista över ALLA delar:
// kodpunkten, glyfen ur delens stil (nullptr för en radbrytning — och för
// VARJE post i mätningen, som bara hämtar steget), steget med en syntetisk
// fet inräknad, och det delen bär — färgen, fetstilens och kursivens syntes
// och stilen. En radbrytning bär också delens stil: en tom rad som kortas
// tar ellipsens utseende ur den. Sist delens bakgrund och, på
// delens FÖRSTA post, dess tabbstopp.
struct LaidGlyph {
    char32_t               cp       = 0;
    const SdfAtlas::Glyph* g        = nullptr;
    float                  advance  = 0.0f;   // texlar, boldStep inräknad
    float                  boldStep = 0.0f;   // den syntetiska fetstilens tillägg
    float                  shear    = 0.0f;   // den syntetiska kursivens skjuvning
    Color                  color{};
    bool                   synthBold = false;
    FontStyle              style     = FontStyle::Regular;
    Color                  background{0, 0, 0, 0};   // alfa 0 = ingen
    float                  tab = 0.0f;        // texlar från radens början; 0 = inget
};

// Var glyfen BÖRJAR på raden när pennan står på `pen`: ett
// tabbstopp flyttar pennan fram till stoppet, aldrig bakåt. EN regel för
// radbrytningen, radernas bredd, taket och bygget.
float startOf(float pen, const LaidGlyph& it)
{
    return std::max(pen, it.tab);
}

// Pennan efter glyferna [b, e) från radens början — radens bredd.
float penAfter(const std::vector<LaidGlyph>& items, std::size_t b, std::size_t e)
{
    float pen = 0.0f;
    for (std::size_t k = b; k < e; ++k)
        pen = startOf(pen, items[k]) + items[k].advance;
    return pen;
}

// En RAD: ett sammanhängande intervall av glyferna och dess bredd.
struct LaidLine {
    std::size_t begin = 0, end = 0;   // [begin, end) i glyflistan
    float       width = 0.0f;         // texlar
};

// RADERNA ur glyflistan — `gfx::wrapText`s regel glyf för glyf (text_util.cpp:
// samma brytpunkter som en sträng i EN stil hade fått): '\n' bryter alltid; med
// en bredd bryts en rad vid det sista mellanslaget på eller före den första
// glyf som inte ryms (ett mellanslag först på raden räknas inte), annars hårt
// vid den glyfen — minst en per rad — och mellanslagen efter ett hårdbrott
// hoppas över; det brytande mellanslaget hör inte till någon rad. Bredderna är
// glyfernas egna steg i sina stilar, och ett tabbstopp räknas från den rad
// glyfen hamnar på (`startOf`). Radbrytningen känns igen på KODPUNKTEN,
// inte på en tom glyfpekare: mätningens poster har ingen glyf alls.
std::vector<LaidLine> layLines(const std::vector<LaidGlyph>& items, float maxW,
                               bool wrap)
{
    std::vector<LaidLine> lines;
    const auto line = [&](std::size_t b, std::size_t e) {
        lines.push_back(LaidLine{b, e, penAfter(items, b, e)});
    };
    const auto space = [&](std::size_t k) { return items[k].cp == U' '; };
    for (std::size_t b = 0;;) {
        std::size_t e = b;   // stycket [b, e) — fram till nästa '\n'
        while (e < items.size() && items[e].cp != U'\n')
            ++e;
        if (wrap && e > b) {
            for (std::size_t s = b; s < e;) {
                // Hur många glyfer ryms (`Font::fitWidth`s fråga)?
                float       pen = 0.0f;
                std::size_t fit = 0;
                while (s + fit < e) {
                    const LaidGlyph& it = items[s + fit];
                    if (startOf(pen, it) + it.advance > maxW)
                        break;
                    pen = startOf(pen, it) + it.advance;
                    ++fit;
                }
                if (s + fit >= e) {
                    line(s, e);   // resten ryms
                    break;
                }
                if (fit == 0)
                    fit = 1;   // minst en glyf per rad
                // Det sista mellanslaget på eller före den första som inte
                // ryms; ett först på raden räknas inte.
                std::size_t brk = 0;
                for (std::size_t k = std::min(fit, e - s - 1) + 1; k-- > 1;)
                    if (space(s + k)) {
                        brk = k;
                        break;
                    }
                if (brk == 0) {
                    line(s, s + fit);
                    s += fit;
                    while (s < e && space(s))
                        ++s;   // mellanslagen efter ett hårdbrott
                } else {
                    line(s, s + brk);
                    s += brk + 1;   // det brytande mellanslaget ingår inte
                }
            }
        } else {
            line(b, e);   // också ett tomt stycke är en rad
        }
        if (e >= items.size())
            break;
        b = e + 1;   // förbi '\n'
    }
    return lines;
}

// ETIKETTENS LAYOUT — byggarens och mätningens GEMENSAMMA del: den
// platta glyflistan över delarna, raderna, radmåtten, blocket, ankaret och
// lådan, i atlasens texlar (lådan i em). `forDraw` hämtar glyferna ur
// atlasen — rasteriserar och laddar upp första gången — och hoppar över en
// glyf atlasen vägrar; mätningen tar stegen ur `SdfAtlas::advance` och lämnar
// glyfpekaren tom, så ingenting laddas upp. Anroparen har prövat atlasen och
// storleken.
struct TextLayout {
    std::vector<LaidGlyph> items;
    std::vector<LaidLine>  lines;
    float                  lh = 0.0f, asc = 0.0f;         // radhöjden, baslinjen
    float                  pitch = 0.0f;                  // radavståndet
    float                  rim   = 0.0f;                  // konturen, enheten; 0 = ingen
    float                  blockW = 0.0f, blockH = 0.0f;  // textblocket
    float                  bx = 0.0f, by = 0.0f;          // blockets hörn vid ankaret
    TextBox                box;                           // em, med luften och konturen
    bool                   truncated = false;             // kortad av taket
};

// TAKET — `Draw::elideLines` regel speglad över glyflistan, som
// `layLines` speglar `wrapText`: de `n` första raderna står kvar, och den
// sista av dem bär RESTEN av sitt stycke, blanksteg närmast ellipsen bort.
// Med en bredd behålls det längsta prefix som ryms tillsammans med ellipsen
// (inget prefix alls ⇒ bara ellipsen, för en kortad etikett ska synas som
// kortad); utan bredd läggs ellipsen efter raden. Ellipsen tar den sista
// kvarvarande glyfens färg, stil och syntes — och utan glyf kvar radens
// första post, som för en tom rad är dess radbrytning (delens). Vägrar
// atlasen ellipsen i ritningen läggs ingen till; mätningen räknar dess steg.
void truncateLines(TextLayout& t, SdfAtlas& atlas, std::size_t n, float maxW,
                   bool wrap, const TextLayoutStyle& st, bool forDraw)
{
    std::vector<LaidGlyph>& items = t.items;
    const std::size_t       b     = t.lines[n - 1].begin;
    std::size_t             e     = b;   // styckets slut — nästa '\n'
    while (e < items.size() && items[e].cp != U'\n')
        ++e;
    const auto space = [&](std::size_t k) { return items[k].cp == U' '; };
    while (e > b && space(e - 1))
        --e;

    // Referensen för ett prefix om `k` glyfer, och ellipsens steg i den.
    LaidGlyph fallback;
    fallback.color = st.color;
    fallback.style = st.fontStyle;
    const auto ref = [&](std::size_t k) -> const LaidGlyph& {
        if (k > 0)
            return items[b + k - 1];
        return b < items.size() ? items[b] : fallback;
    };
    const auto ellipsisStep = [&](const LaidGlyph& r) {
        return atlas.advance(kEllipsis, r.style) + r.boldStep;
    };

    std::size_t keep = e - b;
    if (wrap) {
        // Pennan efter varje prefix — ett tabbstopp gör den större än stegens
        // summa (`startOf`).
        std::vector<float> pen(keep + 1, 0.0f);
        for (std::size_t k = 0; k < keep; ++k)
            pen[k + 1] = startOf(pen[k], items[b + k]) + items[b + k].advance;
        for (;; --keep) {
            // Ett prefix som slutar i ett blanksteg prövas i sin kortade form.
            if ((keep == 0 || !space(b + keep - 1)) &&
                !(pen[keep] + ellipsisStep(ref(keep)) > maxW))
                break;
            if (keep == 0)
                break;   // inte ens ellipsen ryms — den står ändå
        }
        while (keep > 0 && space(b + keep - 1))
            --keep;
    }

    // Ellipsen står direkt efter det som är kvar — inget eget tabbstopp.
    LaidGlyph dots = ref(keep);
    dots.cp        = kEllipsis;
    dots.g         = nullptr;
    dots.tab       = 0.0f;
    dots.advance   = ellipsisStep(dots);
    bool add       = true;
    if (forDraw) {
        dots.g = atlas.glyph(kEllipsis, dots.style);
        add    = dots.g != nullptr;
        if (add)
            dots.advance = dots.g->advance + dots.boldStep;
    }
    items.resize(b + keep);
    if (add)
        items.push_back(dots);
    t.lines.resize(n);
    LaidLine& last = t.lines.back();
    last.end       = items.size();
    last.width     = penAfter(items, last.begin, last.end);
    t.truncated = true;
}

TextLayout layoutText(SdfAtlas& atlas, std::span<const StyledText> parts,
                      const TextLayoutStyle& st, bool forDraw)
{
    TextLayout t;
    // Måtten i atlasens TEXLAR (referens-px); vertexen tar em.
    const float em        = kSdfRefSize;
    const float unitToTex = em / st.size;

    // GLYFERNA i en platt lista över alla delar, var och en ur sin dels stil
    // med delens färg och syntes — '\n' står kvar som en radbrytning. Högst
    // en post per byte (en kodpunkt är minst en), så listan växer aldrig om.
    std::size_t bytes = 0;
    for (const StyledText& part : parts)
        bytes += part.utf8.size();
    t.items.reserve(bytes + 1);   // + ellipsen ur taket
    bool used[4] = {};            // stilarna som bär text
    bool anyUsed = false;
    for (const StyledText& part : parts) {
        if (part.utf8.empty())
            continue;
        const FontStyle s    = part.style;
        const FontStyle real = atlas.style(s);
        used[static_cast<int>(s) & 3] = anyUsed = true;
        // Fet och kursiv som stilens font saknar syntetiseras. Syntetisk fet
        // gör varje glyf bredare: pennan flyttas båda sidornas förskjutning
        // längre (som FreeTypes embolden) — också i radbrytningen och i
        // mätningen (`advance` räknar den inte själv).
        const bool  synthBold = isBold(s) && !isBold(real);
        const float boldStep  = synthBold ? 2.0f * kSyntheticBoldEm * em : 0.0f;
        const float shear =
            isItalic(s) && !isItalic(real) ? kSyntheticItalicShear : 0.0f;
        // TABBSTOPPET sitter på delens FÖRSTA post — också när den
        // är en radbrytning, och på nästa när atlasen vägrar den första.
        const float tabTex = part.tab * unitToTex;
        float       tab    = std::isfinite(tabTex) && tabTex > 0.0f ? tabTex : 0.0f;
        for (std::size_t i = 0; i < part.utf8.size();) {
            const char32_t cp = nextCodepoint(part.utf8, i);
            if (cp == U'\n') {
                t.items.push_back(LaidGlyph{cp, nullptr, 0.0f, boldStep, shear,
                                            part.color, synthBold, s,
                                            part.background, tab});
                tab = 0.0f;
                continue;
            }
            const SdfAtlas::Glyph* g    = nullptr;
            float                  step = 0.0f;
            if (forDraw) {
                g = atlas.glyph(cp, s);
                if (!g)
                    continue;
                step = g->advance;
            } else {
                step = atlas.advance(cp, s);
            }
            t.items.push_back(LaidGlyph{cp, g, step + boldStep, boldStep, shear,
                                        part.color, synthBold, s,
                                        part.background, tab});
            tab = 0.0f;
        }
    }

    // RADERNA (gfx::wrapText:s regel över delarna) och radens mått: de
    // STÖRSTA bland stilarna som bär text — en etikett med en stil får exakt
    // dess mått, och bara tomma delar ger `st.fontStyle`s.
    const float maxW = st.wrapWidth * unitToTex;
    const bool  wrap = st.wrapWidth > 0.0f;
    t.lines          = layLines(t.items, maxW, wrap);
    for (int s = 0; s < 4; ++s)
        if (used[s]) {
            t.lh  = std::max(t.lh, atlas.lineHeight(static_cast<FontStyle>(s)));
            t.asc = std::max(t.asc, atlas.ascent(static_cast<FontStyle>(s)));
        }
    if (!anyUsed) {
        t.lh  = atlas.lineHeight(st.fontStyle);
        t.asc = atlas.ascent(st.fontStyle);
    }
    // RADAVSTÅNDET: baslinje till baslinje.
    const float spacing = st.lineSpacing > 0.0f && std::isfinite(st.lineSpacing)
                              ? st.lineSpacing
                              : 1.0f;
    t.pitch = t.lh * spacing;

    // TAKET: det strängaste av radantalet och höjden, minst en rad —
    // n rader är en radhöjd och n − 1 radavstånd höga.
    std::size_t n = t.lines.size();
    if (st.maxLines > 0)
        n = std::min(n, static_cast<std::size_t>(st.maxLines));
    if (st.maxHeight > 0.0f && std::isfinite(st.maxHeight) && t.lh > 0.0f) {
        const double fit =
            std::floor((st.maxHeight * unitToTex - t.lh) / t.pitch + 1e-4) + 1.0;
        n = std::min(n, static_cast<std::size_t>(std::clamp(
                            fit, 1.0, static_cast<double>(t.lines.size()))));
    }
    if (n < t.lines.size())
        truncateLines(t, atlas, n, maxW, wrap, st, forDraw);

    for (const LaidLine& l : t.lines)
        t.blockW = std::max(t.blockW, l.width);
    // En radhöjd och ett radavstånd per rad efter den första (minst en rad
    // står alltid) — i den form som är radantalet × radhöjden vid faktorn 1.
    t.blockH =
        t.lh * (1.0f + spacing * static_cast<float>(t.lines.size() - 1));

    // ANKARET: blockets övre vänstra hörn relativt ankarpunkten. Lådan är
    // blocket med luften och KONTUREN, som ligger utanför plattan.
    anchorCorner(st.anchor, t.blockW, t.blockH, t.bx, t.by);
    t.bx += st.offsetX * unitToTex;
    t.by += st.offsetY * unitToTex;
    t.rim = st.outlineWidth > 0.0f && st.outline.a > 0 ? st.outlineWidth : 0.0f;
    const float pad = (std::max(st.padding, 0.0f) + t.rim) * unitToTex;
    t.box = TextBox{(t.bx - pad) / em, (t.by - pad) / em,
                    (t.bx + t.blockW + pad) / em, (t.by + t.blockH + pad) / em};
    return t;
}

} // namespace

TextBox buildText(SdfAtlas& atlas, std::span<const StyledText> parts,
                  Point2d anchor, const TextLayoutStyle& st,
                  std::vector<GlyphChunk>& chunks)
{
    if (!atlas.valid() || !(st.size > 0.0f) || !std::isfinite(anchor.x) ||
        !std::isfinite(anchor.y))
        return TextBox{};

    // LAYOUTEN (delad med mätningen) — här med de byggda glyferna.
    const TextLayout              t         = layoutText(atlas, parts, st, true);
    const std::vector<LaidGlyph>& items     = t.items;
    const std::vector<LaidLine>&  lines     = t.lines;
    const TextBox&                box       = t.box;
    const float                   em        = kSdfRefSize;
    const float                   unitToTex = em / st.size;

    // Det gemensamma i alla quadar.
    GlyphVertex proto;
    proto.halo  = st.halo;
    proto.size  = st.size;
    proto.angle = static_cast<float>(st.angleDeg * kPi / 180.0);
    proto.label = st.label;
    const std::uint32_t align = rotationFlags(st.rotation);

    // Sidan plattan (som inte samplar) delar chunk med: den första glyfens.
    int firstPage = -1;
    for (const LaidLine& l : lines)
        for (std::size_t k = l.begin; k < l.end && firstPage < 0; ++k)
            if (items[k].g->page >= 0)
                firstPage = items[k].g->page;

    // ETIKETTENS ORDNING ÖVER ATLASSIDOR: varje chunk dess glorior hamnar i
    // ligger på eller efter plattans, och varje chunk dess fyllningar hamnar
    // i på eller efter den SISTA gloria-chunken — så ritas plattan, alla
    // glorior och alla fyllningar i den ordningen också när glyferna ligger
    // på flera sidor. Gränsen börjar vid den SISTA
    // chunken som fanns när etiketten började: annars
    // hade en senare etikett kunnat lägga glyfer i en äldre chunk på samma
    // sida och ritats UNDER en tidigare etiketts fyllning på en annan sida —
    // fel ordning mellan två överlappande etiketter. Med en sida (det vanliga)
    // är den sista chunken också den enda, så ingenting delas upp.
    std::size_t first = chunks.empty() ? 0 : chunks.size() - 1;

    // En PLATTA (em, relativt ankaret) — formens uv är hörnet relativt dess
    // mitt — i `fill`, med en kontur `rimWidth` (anropets enhet) bred i `rim`
    // som bandet innanför kanten. Den delar chunk med den första glyfens sida
    // och ligger före allt som kommer efter den.
    const auto plate = [&](float x0, float y0, float x1, float y1, Color fill,
                           Color rim, float rimWidth) {
        GlyphVertex p = proto;
        p.color       = fill;
        p.halo        = rim;
        p.haloWidth   = rimWidth;
        p.flags = static_cast<std::uint32_t>(GlyphShape::Plate) | align;
        const float hx = (x1 - x0) / 2.0f, hy = (y1 - y0) / 2.0f;
        first          = chunkFor(chunks, firstPage, anchor, first);
        pushQuad(chunks[first], anchor, x0, y0, x1, y1, -hx, -hy, hx, hy, 0.0f,
                 0.0f, p);
    };
    // Radens vänsterkant efter justeringen.
    const auto lineX = [&](const LaidLine& l) {
        if (st.align == TextAlign::Center)
            return t.bx + (t.blockW - l.width) / 2.0f;
        if (st.align == TextAlign::Right)
            return t.bx + t.blockW - l.width;
        return t.bx;
    };
    constexpr Color kNone{0, 0, 0, 0};

    // 1. PLATTAN och dess KONTUR: lådan har vuxit med konturen, så
    // bandet innanför dess kant ligger utanför bakgrunden och luften — och
    // ritas också utan bakgrund.
    if (st.background.a > 0 || t.rim > 0.0f)
        plate(box.x0, box.y0, box.x1, box.y1, st.background,
              t.rim > 0.0f ? st.outline : kNone, t.rim);

    // 2. DELARNAS BAKGRUNDER: en ruta per rad och följd glyfer i
    // samma färg, radhöjden hög från radens överkant; luckan ett tabbstopp
    // lämnar bryter följden.
    for (std::size_t k = 0; k < lines.size(); ++k) {
        const LaidLine& l   = lines[k];
        const float     lx  = lineX(l);
        const float     top = t.by + t.pitch * static_cast<float>(k);
        float           pen = 0.0f, x0 = 0.0f;
        Color           bg  = kNone;   // den öppna följdens färg
        const auto      flush = [&] {
            if (bg.a > 0 && pen > x0)
                plate((lx + x0) / em, top / em, (lx + pen) / em,
                      (top + t.lh) / em, bg, kNone, 0.0f);
            bg = kNone;
        };
        for (std::size_t j = l.begin; j < l.end; ++j) {
            const LaidGlyph& it = items[j];
            const float      x  = startOf(pen, it);
            if (bg.a > 0 && (x > pen || !(it.background == bg)))
                flush();
            if (bg.a == 0 && it.background.a > 0) {
                bg = it.background;
                x0 = x;
            }
            pen = x + it.advance;
        }
        flush();
    }

    // 3–4. GLORIORNA och FYLLNINGARNA — alla glorior först, över alla delar.
    const float haloTex =
        std::min(std::max(st.haloWidth, 0.0f) * unitToTex,
                 static_cast<float>(kSdfRadius - 1));
    const bool  halo     = st.haloWidth > 0.0f && st.halo.a > 0;
    std::size_t lastHalo = first;
    for (int pass = halo ? 0 : 1; pass < 2; ++pass) {
        const std::size_t lower = pass == 0 ? first : lastHalo;
        GlyphVertex       q     = proto;
        q.haloWidth             = haloTex / unitToTex;
        for (std::size_t k = 0; k < lines.size(); ++k) {
            const LaidLine& l   = lines[k];
            const float     lx  = lineX(l);
            const float     yb  = t.by + t.asc + t.pitch * static_cast<float>(k);
            float           pen = 0.0f;
            for (std::size_t j = l.begin; j < l.end; ++j) {
                const LaidGlyph&       it = items[j];
                const SdfAtlas::Glyph& g  = *it.g;
                pen = startOf(pen, it);   // tabbstoppet
                if (g.page >= 0) {   // ett mellanslag är bara steget
                    // Delens färg och syntes; fältet ligger relativt pennan,
                    // och en syntetisk fet centreras i sitt bredare steg.
                    q.color = it.color;
                    q.flags = static_cast<std::uint32_t>(GlyphShape::Glyph) |
                              align |
                              (it.synthBold ? glyphflag::kSyntheticBold : 0u) |
                              (pass == 0 ? glyphflag::kHaloPass : 0u);
                    const float x0 = lx + pen + it.boldStep / 2.0f + g.left;
                    const float y0 = yb + g.top;
                    const std::size_t c = chunkFor(chunks, g.page, anchor, lower);
                    if (pass == 0)
                        lastHalo = std::max(lastHalo, c);
                    pushQuad(chunks[c], anchor, x0 / em, y0 / em,
                             (x0 + g.w) / em, (y0 + g.h) / em, g.u0, g.v0, g.u1,
                             g.v1, it.shear, yb / em, q);
                }
                pen += it.advance;
            }
        }
    }
    return box;
}

TextBox buildText(SdfAtlas& atlas, std::string_view utf8, Point2d anchor,
                  const TextLayoutStyle& st, std::vector<GlyphChunk>& chunks)
{
    const StyledText part{utf8, st.color, st.fontStyle};
    return buildText(atlas, std::span<const StyledText>(&part, 1), anchor, st,
                     chunks);
}

TextMeasure measureText(SdfAtlas& atlas, std::span<const StyledText> parts,
                        const TextLayoutStyle& st)
{
    TextMeasure m;
    if (!atlas.valid() || !(st.size > 0.0f))
        return m;
    // Byggarens layout med de MÄTTA stegen — inga glyfer hämtas.
    const TextLayout t  = layoutText(atlas, parts, st, false);
    const float      em = kSdfRefSize;
    m.box        = t.box;
    m.width      = t.blockW / em;
    m.height     = t.blockH / em;
    m.lineHeight = t.lh / em;
    m.ascent     = t.asc / em;
    m.lines      = static_cast<int>(t.lines.size());
    m.truncated  = t.truncated;
    return m;
}

TextBox buildMarkers(std::span<const Point2d> points,
                     const MarkerLayoutStyle& st,
                     std::vector<GlyphChunk>& chunks)
{
    if (!(st.size > 0.0f))
        return TextBox{};
    // Fyllningen har radien ½ em; konturen ligger utanför den, så quaden
    // (och formens halva mått) växer med konturens bredd i em. En bild har
    // ingen kontur — dess ruta är 1 em.
    const bool  outline = !st.image && st.outlineWidth > 0.0f && st.outline.a > 0;
    const float h       = 0.5f + (outline ? st.outlineWidth / st.size : 0.0f);
    // ANKARET flyttar rutan; formens läge (u, v) mäts fortfarande från dess
    // MITT, så avståndsfältet är detsamma var punkten än sitter.
    float bx = 0.0f, by = 0.0f;
    anchorCorner(st.anchor, 2.0f * h, 2.0f * h, bx, by);
    const float sx = bx + h, sy = by + h;   // rutans mitt relativt punkten
    GlyphVertex proto;
    proto.color     = st.fill;
    proto.halo      = st.outline;
    proto.size      = st.size;
    proto.angle     = static_cast<float>(st.angleDeg * kPi / 180.0);
    proto.haloWidth = outline ? st.outlineWidth : 0.0f;
    proto.label     = st.label;
    proto.flags =
        (st.image ? glyphflag::kImage
                  : static_cast<std::uint32_t>(st.shape == MarkerShape::Square
                                                   ? GlyphShape::Square
                                                   : GlyphShape::Circle)) |
        rotationFlags(st.rotation);
    for (const Point2d& p : points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
            continue;
        GlyphChunk& c = chunks[chunkFor(chunks, -1, p)];
        if (st.image)
            pushQuad(c, p, sx - h, sy - h, sx + h, sy + h, 0.0f, 0.0f, 1.0f,
                     1.0f, 0.0f, 0.0f, proto);
        else
            pushQuad(c, p, sx - h, sy - h, sx + h, sy + h, -h, -h, h, h, 0.0f,
                     0.0f, proto);
    }
    return TextBox{sx - h, sy - h, sx + h, sy + h};
}

// ------------------------------------------------- i rymden
//
// Layouten är EN: etiketten byggs med 2D-byggaren vid ankaret (0, 0) — då är
// varje vertex ankare 0 — och dess quadar LYFTS in i rymdens chunkar vid det
// riktiga ankaret. Samma quadar, samma sidor, samma ordning inom en sida.

namespace {

bool finite3(const Vec3d& p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

// `chunkFor` med origot i xyz — och samma undre gräns `first`.
std::size_t chunkFor3(std::vector<GlyphChunk3>& chunks, int page,
                      const Vec3d& anchor, std::size_t first = 0)
{
    for (std::size_t i = chunks.size(); i-- > first;) {
        GlyphChunk3& c = chunks[i];
        if (c.vertices.size() + 4 > kMaxChunkVertices)
            break;
        if (page < 0 || c.page < 0 || c.page == page) {
            if (page >= 0)
                c.page = page;
            return i;
        }
    }
    GlyphChunk3 c;
    c.originX = anchor.x;
    c.originY = anchor.y;
    c.originZ = anchor.z;
    c.page    = page;
    chunks.push_back(std::move(c));
    return chunks.size() - 1;
}

// En quad (fyra vertex i `pushQuad`s hörnordning) vid ankaret.
void pushQuad3(GlyphChunk3& c, const Vec3d& anchor, const GlyphVertex* q)
{
    const float ax   = static_cast<float>(anchor.x - c.originX);
    const float ay   = static_cast<float>(anchor.y - c.originY);
    const float az   = static_cast<float>(anchor.z - c.originZ);
    const auto  base = static_cast<std::uint32_t>(c.vertices.size());
    for (int k = 0; k < 4; ++k) {
        GlyphVertex3 g;
        g.ax        = ax;
        g.ay        = ay;
        g.az        = az;
        g.ox        = q[k].ox;
        g.oy        = q[k].oy;
        g.u         = q[k].u;
        g.v         = q[k].v;
        g.color     = q[k].color;
        g.halo      = q[k].halo;
        g.size      = q[k].size;
        g.angle     = q[k].angle;
        g.haloWidth = q[k].haloWidth;
        g.flags     = q[k].flags;
        g.label     = q[k].label;
        c.vertices.push_back(g);
    }
    c.indices.insert(c.indices.end(),
                     {base, base + 1, base + 2, base + 2, base + 1, base + 3});
}

// Ett bygge i origo (2D-byggarens chunkar, fyra vertex per quad) till rymden.
// 2D-byggets chunkordning ÄR etikettens ritordning (plattan, glorior,
// fyllningar — över atlassidor), så varje quad hamnar i en chunk på eller
// efter den förra quadens: lyftet behåller ordningen.
void lift(const std::vector<GlyphChunk>& flat, const Vec3d& anchor,
          std::vector<GlyphChunk3>& chunks)
{
    std::size_t first = chunks.empty() ? 0 : chunks.size() - 1;   // som 2D
    for (const GlyphChunk& f : flat)
        for (std::size_t q = 0; q + 4 <= f.vertices.size(); q += 4) {
            first = chunkFor3(chunks, f.page, anchor, first);
            pushQuad3(chunks[first], anchor, &f.vertices[q]);
        }
}

} // namespace

TextBox buildText(SdfAtlas& atlas, std::span<const StyledText> parts,
                  Vec3d anchor, const TextLayoutStyle& st,
                  std::vector<GlyphChunk3>& chunks)
{
    if (!finite3(anchor))
        return TextBox{};
    std::vector<GlyphChunk> flat;
    const TextBox box = buildText(atlas, parts, Point2d{0.0, 0.0}, st, flat);
    lift(flat, anchor, chunks);
    return box;
}

TextBox buildText(SdfAtlas& atlas, std::string_view utf8, Vec3d anchor,
                  const TextLayoutStyle& st, std::vector<GlyphChunk3>& chunks)
{
    const StyledText part{utf8, st.color, st.fontStyle};
    return buildText(atlas, std::span<const StyledText>(&part, 1), anchor, st,
                     chunks);
}

TextBox buildMarkers(std::span<const Vec3d> points, const MarkerLayoutStyle& st,
                     std::vector<GlyphChunk3>& chunks)
{
    // EN markör i origo är förlagan; varje punkt får dess fyra vertex.
    const Point2d           origo{0.0, 0.0};
    std::vector<GlyphChunk> flat;
    const TextBox box = buildMarkers(std::span<const Point2d>(&origo, 1), st, flat);
    if (flat.empty() || flat.front().vertices.size() < 4)
        return box;
    const GlyphVertex* proto = flat.front().vertices.data();
    for (const Vec3d& p : points)
        if (finite3(p))
            pushQuad3(chunks[chunkFor3(chunks, -1, p)], p, proto);
    return box;
}

} // namespace gfx
