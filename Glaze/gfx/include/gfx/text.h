#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "gfx/geometry.h"

namespace gfx {

// Font-familjer — CSS-tänket, men backend-/plattformsneutralt: en familj är
// en fallback-kedja av systemfonter som varje plattform löser till en fil
// (kandidatlistorna bor på ETT ställe: src/font_catalog.cpp — aldrig
// hårdkodade sökvägar ute i koden).
//   UI   = plattformens gränssnittsfont (Segoe UI / Helvetica / DejaVu ...)
//   Mono = fast breddsteg (Consolas / Courier New / DejaVu Sans Mono ...)
enum class FontFamily { UI, Mono };

// STILEN — fet och kursiv som egna filer i familjen
// (Segoe UI Bold, DejaVu Sans Oblique …), valda ur katalogen per stil.
// `loadFont` tar den begärda stilen; `Font::style()` svarar med den som
// FAKTISKT laddades — saknar familjen stilen laddas den närmaste (fet kursiv
// → fet → kursiv → vanlig), och ritytan syntetiserar resten.
enum class FontStyle : std::uint8_t { Regular, Bold, Italic, BoldItalic };

constexpr bool isBold(FontStyle s)
{
    return s == FontStyle::Bold || s == FontStyle::BoldItalic;
}
constexpr bool isItalic(FontStyle s)
{
    return s == FontStyle::Italic || s == FontStyle::BoldItalic;
}
constexpr FontStyle fontStyle(bool bold, bool italic)
{
    return bold ? (italic ? FontStyle::BoldItalic : FontStyle::Bold)
                : (italic ? FontStyle::Italic : FontStyle::Regular);
}

// EN GLYF SOM PIXLAR— det `Font::rasterGlyph` ger:
// fontens egen rasterisering i fontens storlek, samma som den ritar sin text
// med, utan atlas. SDF-atlasen (gfx/sdf.h) gör sina avståndsfält ur den.
struct GlyphRaster {
    int   width  = 0;   // bitmappens mått i px — 0 för en tom glyf (mellanslag)
    int   height = 0;
    float left   = 0.0f;   // bitmappens övre vänstra hörn relativt pennan på
    float top    = 0.0f;   // baslinjen; top NEGATIV ovanför (y nedåt)
    float advance = 0.0f;  // pennans förflyttning i px — samma som measure
    std::vector<std::uint8_t> coverage;   // width × height, rad 0 överst,
                                          // täckning 0–255
};

// Typografin — standardstorlekarna i logiska px, på ETT ställe (multiplicera
// med uiScale() vid laddning). Inga sifferstorlekar ute i koden: nya
// användningsfall får en namngiven konstant här.
namespace fontsize {
inline constexpr float kUi    = 13.0f;   // all widget-text (AppWindow)
inline constexpr float kMono  = 13.0f;   // Mono — samma storlek som UI
inline constexpr float kTitle = 12.0f;   // fönster-chromets titelrad

// Kompakt UI-text. En stil som vill ner under ~20 px kontrollhöjd MÅSTE
// välja den här: radhöjden ensam sätter golvet, och den är
// 14.72 logiska px vid kUi mot 12.16 vid kUiCompact. Storleken väljs av
// stilen (`Style::uiFontSize`), men talet bor HÄR — fasaden är enda
// sifferkällan för typografi, även när det är en stil som pekar på den.
inline constexpr float kUiCompact = 10.0f;

// RUBRIKEN — textstorleken för `Label::Role::Heading`.
// Storleken är ROLLBASERAD och inte en multiplikator: appen säger VAD
// texten är, `Style::headingFontSize` äger HUR STOR den blir, och ett
// stilbyte når därför hela vägen ut. Talen bor här av samma skäl som
// resten av typografin — fasaden är enda sifferkällan.
//
// TVÅ konstanter och inte en: en familj som valt `kUiCompact` (10) för
// brödtexten kan inte rimligen ha 20 px rubrik — det vore dubbla
// storleken och skulle spränga just den täthet familjen finns för.
inline constexpr float kUiHeading        = 20.0f;   // mot kUi (13)
inline constexpr float kUiHeadingCompact = 15.0f;   // mot kUiCompact (10)

// UNDERRUBRIKEN — Markdown-vyns H2, mellan rubriken
// och brödtexten (`Style::subheadingFontSize`). Utan den hade H2 och H3 inte
// gått att skilja åt. Två konstanter av rubrikens skäl: en kompakt familj kan
// inte ha en underrubrik som är större än sin egen rubrik.
inline constexpr float kUiSubheading        = 16.0f;   // mellan 20 och 13
inline constexpr float kUiSubheadingCompact = 12.0f;   // mellan 15 och 10
} // namespace fontsize

// UTF-8-textmätning — abstrakt och backend-fri. All text i GUI:t ska gå
// via Font — då hanteras åäö (och all annan Unicode fonten täcker)
// automatiskt.
//
// En Font skapas av sin Renderer (loadFont/loadDefaultFont) och ritas via
// Renderer::drawText — fonten hör till renderern (glyf-texturer m.m. lever
// i backendens renderresurser). Fabrikerna returnerar ALLTID ett objekt;
// valid() avgör om en font faktiskt laddades (ogiltig font mäter 0 och
// ritar inget — appen kör vidare utan text).
class Font {
public:
    virtual ~Font() = default;

    virtual Sizef measure(const std::string& utf8) const = 0;
    virtual float height() const = 0;
    virtual bool  valid() const = 0;

    // ---- textmätning & text-hit-test ----
    // Alla index är BYTE-index men respekterar UTF-8-teckengränser: inget
    // svar pekar mitt i ett flerbyte-tecken (åäö är 2 byte). Kerning ÖVER
    // en mätgräns kan avvika marginellt från en sammanhängande mätning —
    // försumbart i UI-fonter (samma brasklapp som TextInputs selektion).

    // Bredden (px) av strängens första `bytes` byte. bytes klampas till
    // längden och förväntas vara en teckengräns (mitt-i-tecken räknar
    // hela tecknet). Monoton i bytes.
    virtual float prefixWidth(const std::string& utf8,
                              std::size_t bytes) const = 0;

    // Närmaste teckengräns för positionen x px in i strängen: x i vänstra
    // halvan av ett tecken ger gränsen före tecknet, högra halvan efter.
    // x < 0 → 0; x bortom texten → utf8.size().
    virtual std::size_t byteIndexAtX(const std::string& utf8,
                                     float x) const = 0;

    // Hur många byte (hela tecken) ryms inom maxW px från strängens
    // början? Radbrytningens råvara — se wrapText nedan.
    virtual std::size_t fitWidth(const std::string& utf8,
                                 float maxW) const = 0;

    // ---- ritytans text ----

    // Avståndet från radens överkant till baslinjen, px — drawText lägger
    // baslinjen där; en etikett med ankare behöver samma mått.
    virtual float ascent() const = 0;

    // Stilen som FAKTISKT laddades, läst ur fontfilen själv (inte ur
    // begäran) — se FontStyle.
    virtual FontStyle style() const = 0;

    // Glyfen för kodpunkten som pixlar i fontens storlek — samma
    // rasterisering som drawText ritar med, lyft ur atlasskrivningen. En
    // kodpunkt fonten saknar tas ur en RESERVFONT som har den (OS:ets egen
    // reservkedja — se hasGlyph), annars blir den ersättningsglyfen (U+FFFD,
    // sist .notdef) som drawText. Stegbredden är den upplösta glyfens, samma
    // som measure. false bara när fonten är ogiltig.
    virtual bool rasterGlyph(char32_t cp, GlyphRaster& out) const = 0;

    // GLYFRESERVEN: har kodpunkten en RIKTIG glyf —
    // fontens egen eller en reservfonts — eller ritas den som
    // ersättningstecknet? Ordningen är fontens egen glyf → en reservfont som
    // har glyfen (DirectWrite `MapCharacters`, Core Text
    // `CTFontCreateForString`, fontconfigs sorterade lista) → U+FFFD → fontens
    // .notdef, och den gäller mätningen, drawText och rasterGlyph lika. Ett
    // styrtecken (C0, DEL, C1) söker ingen reserv och svarar alltid false.
    // Läsbar degradering: en app kan välja "~" när "≈" saknas. false för en
    // ogiltig font.
    virtual bool hasGlyph(char32_t cp) const = 0;
};

// En rad ur wrapText: byte-intervallet [begin, end) i källsträngen.
// Radens text: utf8.substr(l.begin, l.end - l.begin).
struct WrappedLine {
    std::size_t begin = 0;
    std::size_t end   = 0;
};

// Radbrytning på ORDGRÄNSER (mellanslag; '\n' bryter alltid) ovanpå
// Font::fitWidth. Backend-neutral delad logik (src/text_util.cpp) — samma
// brytpunkter i alla backends per konstruktion, samma delningsmönster som
// klipp-stacken. Ord bredare än maxW hårdbryts (minst ett tecken per rad).
// Radbrytande mellanslag ingår inte i någon rad.
std::vector<WrappedLine> wrapText(const Font& f, const std::string& utf8,
                                  float maxW);

} // namespace gfx
