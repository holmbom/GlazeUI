// md_layout — md::Document i en bredd. Kontraktet
// står i md_layout.h; måtten är `c.style->markdown*` och fonterna `fontFor`,
// så inget tal och ingen font bor här. Blocken läggs uppifrån i ett flöde:
// luften mellan dem, citatets streck och indrag, listans markörkolumn,
// kodblockets yta och tabellens kolumner — och texten i varje block bryts i
// rader av `flow`.

#include "md_layout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

#include "glaze/draw.h"

namespace glaze::md {
namespace {

// En BIT i flödet: ett ord med sina efterföljande blanksteg (blanksteget är
// den enda brytpunkten), en bild, en bild som saknas eller en hård brytning.
struct Bit {
    enum class K : std::uint8_t { Word, Image, Missing, Break };
    K           k = K::Word;
    std::string text;
    std::size_t wordLen = 0;   // ordets byte; resten av `text` är blanksteg
    FontKey     font;
    int         link  = -1;
    int         image = -1;
    Role        role  = Role::Body;
    float       w     = 0.0f;   // med blankstegen
    float       wTrim = 0.0f;   // utan dem
    float       h     = 0.0f;   // bildens och ikonens höjd
    float       after = 0.0f;   // luft efter biten (ikonen före alt-texten)
    int         codeRun   = -1;   // inline-kodens körning — ytan per rad
    bool        codeStart = false;
    bool        codeEnd   = false;
};

// En bit PLACERAD på raden som byggs — eller en del av ett hårdbrutet ord.
struct Placed {
    Item::Kind  kind = Item::Kind::Text;
    std::string text;
    FontKey     font;
    int         link  = -1;
    int         image = -1;
    Role        role  = Role::Body;
    float       x     = 0.0f;
    float       w     = 0.0f;
    float       wTrim = 0.0f;
    float       h     = 0.0f;
    int         codeRun   = -1;
    bool        codeStart = false;
    bool        codeEnd   = false;
};

class Builder {
public:
    Builder(const Ctx& c, const Document& d, std::span<const ImageSource> bilder)
        : c_(c), d_(d), bilder_(bilder), m_(*c.style)
    {
    }
    Layout run(float bredd);

private:
    // En font med sitt Ctx och sina mått, frågad EN gång per layout.
    struct Fnt {
        Ctx   ctx{};
        float lh   = 0.0f;
        float asc  = 0.0f;
        bool  klar = false;
    };
    const Fnt& fnt(FontKey k);
    float      measure(FontKey k, std::string_view s);
    FontKey    keyFor(std::uint8_t stil, TextSize size, bool fet) const;

    void             words(std::vector<Bit>& ut, std::string_view t,
                           const Bit& mall);
    std::vector<Bit> bitsFor(const std::vector<Span>& spans, TextSize size,
                             bool fet, float bredd);
    void             flow(const std::vector<Bit>& bits, float x0, float w,
                          float& y, FontKey strut, CellAlign align,
                          bool keepLeading);
    void             finishLine(std::vector<Placed>& rad, float x0, float w,
                                float& y, FontKey strut, CellAlign align);

    void blocks(const std::vector<Block>& bl, float x, float w, float& y,
                bool tight, int nivå);
    void block(const Block& b, float x, float w, float& y, int nivå);
    void code(const Block& b, float x, float w, float& y);
    void list(const Block& b, float x, float w, float& y, int nivå);
    void table(const Block& b, float x, float w, float& y);

    const Ctx&                   c_;
    const Document&              d_;
    std::span<const ImageSource> bilder_;
    const Style&                 m_;
    Layout                       L_;
    Fnt                          fonts_[3][2][4];
    int                          codeRuns_ = 0;
};

const Builder::Fnt& Builder::fnt(FontKey k)
{
    Fnt& f = fonts_[static_cast<std::size_t>(k.size)]
                   [k.family == gfx::FontFamily::Mono ? 1 : 0]
                   [static_cast<std::size_t>(k.style) & 3u];
    if (!f.klar) {
        f.ctx      = c_;
        f.ctx.font = fontFor(c_, k);
        f.lh       = Draw::lineHeight(f.ctx);
        f.asc      = Draw::ascent(f.ctx);
        f.klar     = true;
    }
    return f;
}

float Builder::measure(FontKey k, std::string_view s)
{
    return s.empty() ? 0.0f : Draw::measure(fnt(k).ctx, std::string(s)).w;
}

// Spannets font i en rad av storleken `size`: kod är Mono i vanlig stil
// (kod tar ingen fetstil, inte heller i en rubrik), annat UI med fet
// (spannets eller radens, som en rubrik) och kursiv.
FontKey Builder::keyFor(std::uint8_t stil, TextSize size, bool fet) const
{
    if (stil & kCode)
        return FontKey{size, gfx::FontFamily::Mono, gfx::FontStyle::Regular};
    return FontKey{size, gfx::FontFamily::UI,
                   gfx::fontStyle(fet || (stil & kBold) != 0,
                                  (stil & kItalic) != 0)};
}

void Builder::words(std::vector<Bit>& ut, std::string_view t, const Bit& mall)
{
    std::size_t i = 0;
    while (i < t.size()) {
        const std::size_t a = i;
        while (i < t.size() && t[i] != ' ')
            ++i;
        const std::size_t ordSlut = i;
        while (i < t.size() && t[i] == ' ')
            ++i;
        Bit b     = mall;
        b.k       = Bit::K::Word;
        b.text    = std::string(t.substr(a, i - a));
        b.wordLen = ordSlut - a;
        b.wTrim   = measure(b.font, t.substr(a, ordSlut - a));
        b.w       = b.wTrim + measure(b.font, t.substr(ordSlut, i - ordSlut));
        ut.push_back(std::move(b));
    }
}

// Spannen som bitar. En bild mäts med `Draw::imageSize` och krymps till
// `bredd` med bevarade proportioner; en som mäter 0 saknas och blir ikonen
// följd av sin alt-text i den dämpade rollen.
std::vector<Bit> Builder::bitsFor(const std::vector<Span>& spans,
                                  TextSize size, bool fet, float bredd)
{
    std::vector<Bit> ut;
    for (const Span& s : spans) {
        Bit mall;
        mall.link = s.link;
        switch (s.kind) {
        case Span::Kind::Break: {
            Bit b;
            b.k = Bit::K::Break;
            ut.push_back(std::move(b));
            break;
        }
        case Span::Kind::Text: {
            mall.font            = keyFor(s.style, size, fet);
            const bool        kod  = (s.style & kCode) != 0;
            const std::size_t före = ut.size();
            if (kod)
                mall.codeRun = codeRuns_++;
            words(ut, s.text, mall);
            if (kod && ut.size() > före) {
                ut[före].codeStart = true;
                ut.back().codeEnd  = true;
            }
            break;
        }
        case Span::Kind::Image: {
            mall.image = s.image;
            ImageSource bild;
            if (s.image >= 0 && static_cast<std::size_t>(s.image) < bilder_.size())
                bild = bilder_[static_cast<std::size_t>(s.image)];
            gfx::Sizef mått = bild.valid() ? Draw::imageSize(c_, bild)
                                           : gfx::Sizef{};
            Bit b = mall;
            if (mått.w > 0.0f && mått.h > 0.0f) {
                if (mått.w > bredd && bredd > 0.0f) {
                    mått.h *= bredd / mått.w;
                    mått.w = bredd;
                }
                b.k = Bit::K::Image;
                b.w = b.wTrim = mått.w;
                b.h           = mått.h;
                ut.push_back(std::move(b));
                break;
            }
            b.k = Bit::K::Missing;
            b.w = b.wTrim = m_.iconFallback;
            b.h           = m_.iconFallback;
            b.after       = s.text.empty() ? 0.0f : m_.iconGap;
            ut.push_back(std::move(b));
            Bit alt  = mall;
            alt.font = keyFor(s.style, size, fet);
            alt.role = Role::Dim;
            words(ut, s.text, alt);
            break;
        }
        }
    }
    return ut;
}

Placed placedFrom(const Bit& b, float x)
{
    Placed p;
    p.kind      = b.k == Bit::K::Image     ? Item::Kind::Image
                  : b.k == Bit::K::Missing ? Item::Kind::Missing
                                           : Item::Kind::Text;
    p.text      = b.text;
    p.font      = b.font;
    p.link      = b.link;
    p.image     = b.image;
    p.role      = b.role;
    p.x         = x;
    p.w         = b.w;
    p.wTrim     = b.wTrim;
    p.h         = b.h;
    p.codeRun   = b.codeRun;
    p.codeStart = b.codeStart;
    p.codeEnd   = b.codeEnd;
    return p;
}

// FLÖDET — bitarna i rader om `w` logiska px från `x0`, med början på `y`
// (som flyttas fram). Girigt: en bit som inte ryms börjar en ny rad, och ett
// ord som inte ryms på en TOM rad hårdbryts. Inline-kodens luft står i
// körningens ändar: luften FÖRE räknas mot bredden (koden börjar i spalten),
// luften EFTER gör det inte — den får gå ut i marginalen, som är minst lika
// bred (`markdownPad`, cellens och markörens luft). `keepLeading` behåller
// blanksteg i radens början (kod).
void Builder::flow(const std::vector<Bit>& bits, float x0, float w, float& y,
                   FontKey strut, CellAlign align, bool keepLeading)
{
    const float         pad   = m_.markdownCodeSpanPadX;
    const std::size_t   rader = L_.lines.size();
    std::vector<Placed> rad;
    float               x = 0.0f;
    const auto avsluta = [&] {
        finishLine(rad, x0, w, y, strut, align);
        rad.clear();
        x = 0.0f;
    };
    for (const Bit& b : bits) {
        if (b.k == Bit::K::Break) {
            avsluta();
            continue;
        }
        const bool blank = b.k == Bit::K::Word && b.wordLen == 0;
        if (blank && rad.empty() && !keepLeading)
            continue;
        const float före = b.codeStart ? pad : 0.0f;
        if (!rad.empty() && x + före + b.wTrim > w) {
            avsluta();
            if (blank && !keepLeading)
                continue;
        }
        if (b.k == Bit::K::Word && rad.empty() && b.wordLen > 0 &&
            före + b.wTrim > w) {
            // HÅRDBRYTNINGEN — samma regel som en radbruten etikett.
            const std::string ord   = b.text.substr(0, b.wordLen);
            const std::string svans = b.text.substr(b.wordLen);
            const auto delar = Draw::wrapLines(fnt(b.font).ctx, ord, w);
            for (std::size_t k = 0; k < delar.size(); ++k) {
                Placed p = placedFrom(b, k == 0 ? före : 0.0f);
                p.text   = ord.substr(delar[k].begin,
                                      delar[k].end - delar[k].begin);
                p.wTrim  = measure(b.font, p.text);
                p.w      = p.wTrim;
                p.codeStart = b.codeStart && k == 0;
                p.codeEnd   = b.codeEnd && k + 1 == delar.size();
                if (k + 1 == delar.size()) {
                    p.text += svans;
                    p.w += measure(b.font, svans);
                }
                x = p.x + p.w + (p.codeEnd ? pad : 0.0f);
                rad.push_back(std::move(p));
                if (k + 1 < delar.size())
                    avsluta();
            }
            continue;
        }
        Placed p = placedFrom(b, x + före);
        x        = p.x + p.w + (b.codeEnd ? pad : b.after);
        rad.push_back(std::move(p));
    }
    if (!rad.empty() || L_.lines.size() == rader)
        avsluta();   // sista raden — och ett tomt stycke är en tom rad
}

// EN RAD till layouten: höjden ur delarna och strutten, delarna på
// baslinjen, sammanslagning av grannar i samma stil, justeringen och
// inline-kodens ytor.
void Builder::finishLine(std::vector<Placed>& rad, float x0, float w,
                         float& y, FontKey strut, CellAlign align)
{
    // Blankstegen sist på raden står i marginalen — de mäts inte.
    while (!rad.empty() && rad.back().kind == Item::Kind::Text &&
           rad.back().text.find_first_not_of(' ') == std::string::npos)
        rad.pop_back();
    if (!rad.empty() && rad.back().kind == Item::Kind::Text) {
        Placed& sist = rad.back();
        sist.text.erase(sist.text.find_last_not_of(' ') + 1);
        sist.w = sist.wTrim;
    }
    const float pad  = m_.markdownCodeSpanPadX;
    const Fnt&  s    = fnt(strut);
    float       asc  = s.asc;
    float       desc = s.lh - s.asc;
    float       radB = 0.0f;
    for (const Placed& p : rad) {
        if (p.kind == Item::Kind::Text) {
            const Fnt& f = fnt(p.font);
            asc          = std::max(asc, f.asc);
            desc         = std::max(desc, f.lh - f.asc);
        } else {
            asc = std::max(asc, p.h);   // en bild står på baslinjen
        }
        radB = std::max(radB, p.x + p.w + (p.codeEnd ? pad : 0.0f));
    }
    float dx = 0.0f;
    if (align == CellAlign::Center)
        dx = std::max(0.0f, (w - radB) / 2.0f);
    else if (align == CellAlign::Right)
        dx = std::max(0.0f, w - radB);

    Line line;
    line.y        = y;
    line.h        = asc + desc;
    line.baseline = y + asc;
    line.first    = static_cast<std::uint32_t>(L_.items.size());
    for (const Placed& p : rad) {
        Item it;
        it.kind  = p.kind;
        it.x     = x0 + dx + p.x;
        it.w     = p.w;
        it.font  = p.font;
        it.text  = p.text;
        it.link  = p.link;
        it.image = p.image;
        it.role  = p.role;
        if (p.kind == Item::Kind::Text) {
            const Fnt& f = fnt(p.font);
            it.top       = line.baseline - f.asc;
            it.h         = f.lh;
            if (it.text.empty())
                continue;
            // SAMMA STIL I FÖLJD är en del — mätningen är additiv.
            if (L_.items.size() > line.first) {
                Item& förra = L_.items.back();
                if (förra.kind == Item::Kind::Text && förra.font == it.font &&
                    förra.link == it.link && förra.image == it.image &&
                    förra.role == it.role &&
                    std::abs(förra.x + förra.w - it.x) < 1e-3f) {
                    förra.text += it.text;
                    förra.w += it.w;
                    continue;
                }
            }
        } else {
            it.top = line.baseline - p.h;
            it.h   = p.h;
        }
        L_.items.push_back(std::move(it));
    }
    line.count =
        static_cast<std::uint32_t>(L_.items.size()) - line.first;

    // INLINE-KODENS YTA per körning på raden: från första delens början till
    // sista delens slut, med luften i körningens riktiga ändar (inte vid en
    // brytning inne i den), i kodens egen radbox.
    for (std::size_t i = 0; i < rad.size();) {
        if (rad[i].codeRun < 0) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j + 1 < rad.size() && rad[j + 1].codeRun == rad[i].codeRun)
            ++j;
        const Fnt&  f     = fnt(rad[i].font);
        const float vänst = x0 + dx + rad[i].x - (rad[i].codeStart ? pad : 0.0f);
        const float höger =
            x0 + dx + rad[j].x + rad[j].w + (rad[j].codeEnd ? pad : 0.0f);
        Deco d;
        d.kind = Deco::Kind::CodeSpan;
        d.r    = gfx::Rectf{vänst, line.baseline - f.asc, höger - vänst, f.lh};
        L_.decos.push_back(d);
        i = j + 1;
    }
    L_.lines.push_back(line);
    y += line.h;
}

// BLOCKEN efter varandra: `markdownBlockGap` emellan (inget i en TÄT lista)
// och `markdownHeadingGap` till före en rubrik — inget före det första.
void Builder::blocks(const std::vector<Block>& bl, float x, float w, float& y,
                     bool tight, int nivå)
{
    for (std::size_t i = 0; i < bl.size(); ++i) {
        if (i > 0) {
            if (!tight)
                y += m_.markdownBlockGap;
            if (bl[i].kind == Block::Kind::Heading)
                y += m_.markdownHeadingGap;
        }
        block(bl[i], x, w, y, nivå);
    }
}

void Builder::block(const Block& b, float x, float w, float& y, int nivå)
{
    const FontKey bröd{};
    switch (b.kind) {
    case Block::Kind::Paragraph: {
        const std::vector<Bit> bits = bitsFor(b.spans, TextSize::Body, false, w);
        flow(bits, x, w, y, bröd, CellAlign::Left, false);
        break;
    }
    case Block::Kind::Heading: {
        // H1 i rubrikens storlek, H2 i underrubrikens, H3–H6 i brödtextens —
        // alla feta (`Label::Role::Heading` är med flit vanlig vikt).
        const TextSize size = b.level == 1   ? TextSize::Heading
                              : b.level == 2 ? TextSize::Subheading
                                             : TextSize::Body;
        L_.anchors.emplace_back(b.anchor, y);
        const std::vector<Bit> bits = bitsFor(b.spans, size, true, w);
        flow(bits, x, w, y,
             FontKey{size, gfx::FontFamily::UI, gfx::FontStyle::Bold},
             CellAlign::Left, false);
        break;
    }
    case Block::Kind::Code:
        code(b, x, w, y);
        break;
    case Block::Kind::Quote: {
        const float topp = y;
        if (b.children.empty())
            y += fnt(bröd).lh;   // ett tomt citat är en tom rad med sitt streck
        else
            blocks(b.children, x + m_.markdownQuoteIndent,
                   std::max(0.0f, w - m_.markdownQuoteIndent), y, false, nivå);
        Deco d;
        d.kind = Deco::Kind::QuoteBar;
        d.r    = gfx::Rectf{x, topp, m_.markdownQuoteBarW, y - topp};
        L_.decos.push_back(d);
        break;
    }
    case Block::Kind::List:
        list(b, x, w, y, nivå);
        break;
    case Block::Kind::Item:
        blocks(b.children, x, w, y, true, nivå);
        break;
    case Block::Kind::Rule: {
        Deco d;
        d.kind = Deco::Kind::Rule;
        d.r    = gfx::Rectf{x, y, w, 0.0f};
        L_.decos.push_back(d);
        break;
    }
    case Block::Kind::Table:
        table(b, x, w, y);
        break;
    }
}

// KODBLOCKET: Mono, raderna brutna mot bredden (ingen vågrät rullning) med
// de inledande blankstegen kvar, i en yta med `markdownCodePad` luft.
void Builder::code(const Block& b, float x, float w, float& y)
{
    const float   topp = y;
    const float   pad  = m_.markdownCodePad;
    const FontKey mono{TextSize::Body, gfx::FontFamily::Mono,
                       gfx::FontStyle::Regular};
    y += pad;
    std::string_view rest(b.code);
    for (;;) {
        const std::size_t nl = rest.find('\n');
        std::vector<Bit>  bits;
        Bit               mall;
        mall.font = mono;
        words(bits, rest.substr(0, nl), mall);
        flow(bits, x + pad, std::max(0.0f, w - 2.0f * pad), y, mono,
             CellAlign::Left, true);
        if (nl == std::string_view::npos)
            break;
        rest.remove_prefix(nl + 1);
    }
    y += pad;
    Deco d;
    d.kind = Deco::Kind::CodeBlock;
    d.r    = gfx::Rectf{x, topp, w, y - topp};
    L_.decos.push_back(d);
}

// LISTAN: markörkolumnen är minst `markdownListIndent` och minst den
// bredaste markören plus `markdownMarkerGap`. Punkten (nivå 0 skiva, 1 ring,
// djupare kvadrat) står centrerad i postens första rad; numret står
// högerställt mot samma baslinje och hör till den raden.
void Builder::list(const Block& b, float x, float w, float& y, int nivå)
{
    const FontKey            bröd{};
    float                    kolumn = m_.markdownListIndent;
    std::vector<std::string> nummer;
    if (b.ordered) {
        for (std::size_t i = 0; i < b.children.size(); ++i) {
            nummer.push_back(
                std::to_string(static_cast<long long>(b.start) +
                               static_cast<long long>(i)) +
                b.delim);
            kolumn = std::max(kolumn,
                              measure(bröd, nummer.back()) + m_.markdownMarkerGap);
        }
    } else {
        kolumn = std::max(kolumn, m_.markdownBulletSize + m_.markdownMarkerGap);
    }
    const float innehåll = x + kolumn;
    const float bredd    = std::max(0.0f, w - kolumn);
    for (std::size_t i = 0; i < b.children.size(); ++i) {
        if (i > 0 && !b.tight)
            y += m_.markdownBlockGap;
        const std::size_t förstaRad = L_.lines.size();
        const std::size_t markör    = L_.items.size();
        if (b.ordered)
            L_.items.emplace_back();   // platsen före postens rader
        const float topp = y;
        blocks(b.children[i].children, innehåll, bredd, y, b.tight, nivå + 1);
        if (L_.lines.size() == förstaRad) {
            // En tom post — eller en med bara en linje: markören får en rad.
            const Fnt& f = fnt(bröd);
            Line       l;
            l.y        = topp;
            l.h        = f.lh;
            l.baseline = topp + f.asc;
            l.first    = static_cast<std::uint32_t>(L_.items.size());
            L_.lines.push_back(l);
            y = std::max(y, topp + f.lh);
        }
        const Line rad = L_.lines[förstaRad];
        if (b.ordered) {
            const Fnt& f = fnt(bröd);
            Item&      m = L_.items[markör];
            m.kind       = Item::Kind::Marker;
            m.font       = bröd;
            m.text       = nummer[i];
            m.w          = measure(bröd, m.text);
            m.h          = f.lh;
            m.x          = innehåll - m_.markdownMarkerGap - m.w;
            m.top        = rad.baseline - f.asc;
            if (rad.first == markör + 1) {
                L_.lines[förstaRad].first = static_cast<std::uint32_t>(markör);
                ++L_.lines[förstaRad].count;
            } else {
                Line l  = rad;
                l.first = static_cast<std::uint32_t>(markör);
                l.count = 1;
                L_.lines.push_back(l);
            }
        } else {
            const float s = m_.markdownBulletSize;
            Deco        d;
            d.kind = nivå == 0   ? Deco::Kind::BulletDisc
                     : nivå == 1 ? Deco::Kind::BulletRing
                                 : Deco::Kind::BulletSquare;
            d.r    = gfx::Rectf{innehåll - m_.markdownMarkerGap - s,
                                rad.y + (rad.h - s) / 2.0f, s, s};
            L_.decos.push_back(d);
        }
    }
}

// TABELLEN: varje kolumns NATURLIGA bredd (bredaste cellen på en rad) och
// MINSTA (längsta ordet), båda med cellens luft. Ryms summan står den
// naturligt; annars får varje kolumn sin minsta och resten delas efter
// (naturlig − minsta); ryms inte ens minsta-summan går tabellen ut över
// bredden och klipps. Huvudet är fetstil, justeringen gäller varje rad i
// cellen, och radens höjd är den högsta cellens plus luften.
void Builder::table(const Block& b, float x, float w, float& y)
{
    const std::size_t n = b.align.size();
    if (n == 0)
        return;
    const float        padX = m_.markdownCellPadX;
    const float        padY = m_.markdownCellPadY;
    const float        pad  = m_.markdownCodeSpanPadX;
    std::vector<float> naturlig(n, 2.0f * padX);
    std::vector<float> minsta(n, 2.0f * padX);
    for (std::size_t r = 0; r < b.rows.size(); ++r) {
        for (std::size_t k = 0; k < n && k < b.rows[r].size(); ++k) {
            const std::vector<Bit> bits =
                bitsFor(b.rows[r][k], TextSize::Body, r == 0, w);
            float radX = 0.0f, radB = 0.0f, längst = 0.0f;
            for (const Bit& bit : bits) {
                if (bit.k == Bit::K::Break) {
                    radX = 0.0f;
                    continue;
                }
                const float före  = bit.codeStart ? pad : 0.0f;
                const float efter = bit.codeEnd ? pad : bit.after;
                radB   = std::max(radB, radX + före + bit.wTrim + efter);
                radX  += före + bit.w + efter;
                längst = std::max(längst, före + bit.wTrim + efter);
            }
            naturlig[k] = std::max(naturlig[k], radB + 2.0f * padX);
            minsta[k]   = std::max(minsta[k], längst + 2.0f * padX);
        }
    }
    float sumN = 0.0f, sumM = 0.0f;
    for (std::size_t k = 0; k < n; ++k) {
        sumN += naturlig[k];
        sumM += minsta[k];
    }
    std::vector<float> bredd = naturlig;
    if (sumN > w) {
        bredd = minsta;
        if (sumM < w)
            for (std::size_t k = 0; k < n; ++k)
                bredd[k] = minsta[k] + (naturlig[k] - minsta[k]) * (w - sumM) /
                                           (sumN - sumM);
    }
    for (std::size_t r = 0; r < b.rows.size(); ++r) {
        const float   topp = y;
        const FontKey strut{TextSize::Body, gfx::FontFamily::UI,
                            r == 0 ? gfx::FontStyle::Bold
                                   : gfx::FontStyle::Regular};
        float höjd = 0.0f;
        float cx   = x;
        for (std::size_t k = 0; k < n; ++k) {
            const float inre = std::max(0.0f, bredd[k] - 2.0f * padX);
            float       cy   = topp + padY;
            const std::vector<Span> tom;
            const std::vector<Bit>  bits = bitsFor(
                k < b.rows[r].size() ? b.rows[r][k] : tom, TextSize::Body,
                r == 0, inre);
            flow(bits, cx + padX, inre, cy, strut, b.align[k], false);
            höjd = std::max(höjd, cy - (topp + padY));
            cx += bredd[k];
        }
        const float radH = höjd + 2.0f * padY;
        cx               = x;
        for (std::size_t k = 0; k < n; ++k) {
            Deco d;
            d.kind = r == 0 ? Deco::Kind::HeaderCell : Deco::Kind::Cell;
            d.r    = gfx::Rectf{cx, topp, bredd[k], radH};
            L_.decos.push_back(d);
            cx += bredd[k];
        }
        y = topp + radH;
    }
}

Layout Builder::run(float bredd)
{
    float y = 0.0f;
    blocks(d_.blocks, 0.0f, std::max(0.0f, bredd), y, false, 0);
    L_.height = y;
    // En tabells celler står bredvid varandra — raderna sorteras på y, så
    // ritningen och träffen kan söka binärt.
    std::stable_sort(L_.lines.begin(), L_.lines.end(),
                     [](const Line& a, const Line& b) { return a.y < b.y; });
    for (const Line& l : L_.lines)
        L_.maxLineH = std::max(L_.maxLineH, l.h);
    return std::move(L_);
}

} // namespace

gfx::Font* fontFor(const Ctx& c, FontKey k)
{
    if (k.size == TextSize::Body && k.family == gfx::FontFamily::UI &&
        k.style == gfx::FontStyle::Regular)
        return c.font;
    if (c.textFonts && c.r && c.style)
        if (gfx::Font* f = c.textFonts->get(*c.r, *c.style, c.scale, k.size,
                                            k.family, k.style))
            return f;
    if (k.size == TextSize::Heading && c.headingFont)
        return c.headingFont;
    return c.font;
}

Metrics metricsFor(const Ctx& c)
{
    const Style& m = *c.style;
    Metrics      k;
    k.scale              = c.scale;
    k.font               = c.font;
    k.fontH              = c.font ? c.font->height() : 0.0f;
    k.fontAscent         = c.font ? c.font->ascent() : 0.0f;
    k.headingFont        = c.headingFont;
    k.headingH           = c.headingFont ? c.headingFont->height() : 0.0f;
    k.fonts              = c.textFonts;
    k.uiFontSize         = m.uiFontSize;
    k.subheadingFontSize = m.subheadingFontSize;
    k.headingFontSize    = m.headingFontSize;
    k.blockGap           = m.markdownBlockGap;
    k.headingGap         = m.markdownHeadingGap;
    k.listIndent         = m.markdownListIndent;
    k.markerGap          = m.markdownMarkerGap;
    k.bulletSize         = m.markdownBulletSize;
    k.quoteIndent        = m.markdownQuoteIndent;
    k.quoteBarW          = m.markdownQuoteBarW;
    k.codePad            = m.markdownCodePad;
    k.codeSpanPadX       = m.markdownCodeSpanPadX;
    k.cellPadX           = m.markdownCellPadX;
    k.cellPadY           = m.markdownCellPadY;
    k.iconFallback       = m.iconFallback;
    k.iconGap            = m.iconGap;
    return k;
}

Layout build(const Ctx& c, const Document& d,
             std::span<const ImageSource> bilder, float bredd)
{
    return Builder(c, d, bilder).run(bredd);
}

} // namespace glaze::md
