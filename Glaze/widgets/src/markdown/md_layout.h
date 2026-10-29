#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gfx/geometry.h>
#include <gfx/text.h>

#include "glaze/canvas/image_source.h"
#include "glaze/widget.h"

#include "../font_set.h"
#include "md_document.h"

// LAYOUTEN — ett `md::Document` lagt i en bredd:
// rader med delar, ytor och ankare i LOGISKA px från innehållets övre vänstra
// hörn. INTERN och glaze-bunden (Ctx, fonterna, Draw): Markdown-vyn
// (`src/widgets/markdown_view.cpp`) mäter och ritar ur den, och
// `markdown_gate` del L grindar den headless mot en inspelande renderer.
//
// DEN ENDA REGELN FÖR FONTEN är `fontFor`: brödtexten i vanlig stil är
// fönstrets `c.font`, allt annat fönstrets lata textfonter (`Ctx::textFonts`)
// — och utan dem, eller när en variant inte går att ladda, H1 i
// rollfontens storlek och resten i brödtextens ("en rubrik i fel storlek är
// bättre än ingen text", Labels regel). Mätningen och ritningen frågar samma
// funktion, så en del mäts aldrig i en font och ritas i en annan.
//
// EN RAD är delar som står på SAMMA baslinje: radens höjd är den största
// ascenten plus den största nedre delen (höjd − ascent) bland delarna och
// styckets egen font (strutten, som CSS — en rad med bara en bild får textens
// luft under sig), och en del står med sin överkant på baslinjen minus sin
// EGEN ascent (`Draw::ascent`). En bild har sin höjd som ascent och står på
// baslinjen. Delar i samma stil efter varandra är EN del: mätningen är
// additiv i alla tre backends, så en sammanslagen sträng landar där bitarna
// mättes.
//
// BRYTNINGEN är `gfx::wrapText`s regel ord för ord: blanksteget U+0020 är den
// enda brytpunkten, ett ord som inte ryms på en TOM rad hårdbryts med
// `Draw::wrapLines` (som en radbruten etikett), kod behåller sina inledande
// blanksteg, och en hård brytning (`Span::Kind::Break`) tvingar ny rad.

namespace glaze::md {

// Vilken font en del ritas i: storleken (brödtext, underrubrik, rubrik),
// familjen och stilen.
struct FontKey {
    TextSize        size   = TextSize::Body;
    gfx::FontFamily family = gfx::FontFamily::UI;
    gfx::FontStyle  style  = gfx::FontStyle::Regular;
    bool            operator==(const FontKey&) const = default;
};

// En dels ROLL — färgen väljer widgeten: brödtexten (och en länk i sin
// länkfärg), eller den dämpade alt-texten bredvid en bild som saknas.
enum class Role : std::uint8_t { Body, Dim };

// EN DEL på en rad.
//   Text     text i `font` — `w` och `h` är dess bredd och fontens radhöjd
//   Image    bilden `image` (`Layout`s bildlista) i `w` × `h`
//   Missing  en bild som saknas: ikonen i en kvadrat av `w` × `h`
//   Marker   en numrerad listas nummer ("3." / "3)"), text i brödtexten
// `link` är länkens index i `Document::links` (−1 ingen), också för en
// bild i en länk; `image` bildens index för Image, Missing och alt-texten.
struct Item {
    enum class Kind : std::uint8_t { Text, Image, Missing, Marker };
    Kind        kind = Kind::Text;
    float       x    = 0.0f;
    float       top  = 0.0f;
    float       w    = 0.0f;
    float       h    = 0.0f;
    FontKey     font;
    std::string text;
    int         link  = -1;
    int         image = -1;
    Role        role  = Role::Body;
};

// EN RAD: överkanten, höjden, baslinjen (allt i dokumentets y) och delarna
// `items[first, first + count)`. Raderna står SORTERADE på `y` — en tabells
// celler ligger bredvid varandra, så en rad kan börja ovanför den förra.
struct Line {
    float         y        = 0.0f;
    float         h        = 0.0f;
    float         baseline = 0.0f;
    std::uint32_t first    = 0;
    std::uint32_t count    = 0;
};

// EN YTA eller ett streck bakom texten — widgeten ritar dem per familj.
struct Deco {
    enum class Kind : std::uint8_t {
        CodeBlock,     // kodblockets yta
        CodeSpan,      // inline-kodens yta, per rad
        QuoteBar,      // citatets streck
        Rule,          // linjen (`r.h` 0 — strecket står på `r.y`)
        BulletDisc,    // punkten på nivå 0
        BulletRing,    // nivå 1
        BulletSquare,  // nivå 2 och djupare
        Cell,          // en tabellcell
        HeaderCell     // en cell i tabellhuvudet
    };
    Kind       kind = Kind::Rule;
    gfx::Rectf r{};
};

// LAYOUTENS NYCKEL — allt den beror på utöver dokumentet och bredden, som
// VÄRDEN: `c.style` är samma pekare efter ett stilbyte (ett värde i App), så
// en nyckel på pekaren hade aldrig sett bytet. Fonternas pekare OCH deras
// mått (en omladdad font kan hamna på samma adress), skalan, stilens
// textstorlekar (de lata fonterna laddas om på dem) och de mått layouten läser.
struct Metrics {
    float            scale              = 0.0f;
    const gfx::Font* font               = nullptr;
    float            fontH              = 0.0f;
    float            fontAscent         = 0.0f;
    const gfx::Font* headingFont        = nullptr;
    float            headingH           = 0.0f;
    const FontSet*   fonts              = nullptr;
    float            uiFontSize         = 0.0f;
    float            subheadingFontSize = 0.0f;
    float            headingFontSize    = 0.0f;
    float            blockGap           = 0.0f;
    float            headingGap         = 0.0f;
    float            listIndent         = 0.0f;
    float            markerGap          = 0.0f;
    float            bulletSize         = 0.0f;
    float            quoteIndent        = 0.0f;
    float            quoteBarW          = 0.0f;
    float            codePad            = 0.0f;
    float            codeSpanPadX       = 0.0f;
    float            cellPadX           = 0.0f;
    float            cellPadY           = 0.0f;
    float            iconFallback       = 0.0f;
    float            iconGap            = 0.0f;
    bool             operator==(const Metrics&) const = default;
};

struct Layout {
    float                                      height   = 0.0f;
    float                                      maxLineH = 0.0f;   // ritningens sökning
    std::vector<Line>                          lines;
    std::vector<Item>                          items;
    std::vector<Deco>                          decos;
    std::vector<std::pair<std::string, float>> anchors;   // rubrikens slug → y
};

// Fonten för en del — EN regel för mätningen och ritningen (se filhuvudet).
gfx::Font* fontFor(const Ctx& c, FontKey k);

// Nyckelns värden ur ett Ctx (bara det layouten läser).
Metrics metricsFor(const Ctx& c);

// Dokumentet lagt i `bredd` logiska px. `bilder[i]` är källan för
// `Document::images[i]` — ogiltig när widgeten inte hittade den (då ritas en
// saknad bild). Mätt med `Draw::imageSize`: en bild bredare än raden krymps
// med bevarade proportioner, och en som mäter 0 (ingen cache, trasig) saknas.
Layout build(const Ctx& c, const Document& d,
             std::span<const ImageSource> bilder, float bredd);

} // namespace glaze::md
