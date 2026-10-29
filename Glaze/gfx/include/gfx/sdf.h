#pragma once
// RITYTANS TEXT OCH MARKÖRER — avståndsfälten (SDF),
// atlasen de bor i och byggarna som lägger ut etiketter och markörer som
// `GlyphVertex`-quads (gfx/scene.h). Backend-neutralt: atlasen går genom
// `Renderer` (loadFont, createImage, updateImage) och byggarna är ren CPU.
//
// EN rasterisering per glyf (`Font::rasterGlyph`) blir ETT fält som tjänar
// alla storlekar, all rotation och tilt: Glyph-programmet skalar fältet till
// etikettens storlek och tröskeln ger kanten — glorian är en andra tröskel,
// syntetisk fet en förskjuten. Fältet har REFERENSTÄTHETEN 32 texlar per em
// (`kSdfRefSize`) och räcker `kSdfRadius` texlar åt båda håll från kanten.

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gfx/camera.h"   // Vec3d — ankaret i rymden
#include "gfx/color.h"
#include "gfx/geom2d.h"
#include "gfx/image.h"
#include "gfx/scene.h"
#include "gfx/text.h"

namespace gfx {

class Renderer;

// Fältets täthet: texlar per em. Det Glyph-programmet räknar skärmens px per
// texel ur (storleken / referensen).
inline constexpr float kSdfRefSize = 32.0f;
// Fältets räckvidd från kanten, i texlar, åt båda håll — glorian och den
// syntetiska fetstilen ryms inom den (en gloria bredare än radien − 1 texel
// kapas av byggaren).
inline constexpr int kSdfRadius = 8;
// Glyferna rasteriseras i (kSdfRefSize × kSdfSupersample) px och fältet räknas
// i den upplösningen innan det tas ned till referensen: kantens läge blir då
// exakt på en kvarts texel, där en tröskling i referensen hade lämnat en halv.
inline constexpr int kSdfSupersample = 2;
// En atlassida: R8, kvadratisk.
inline constexpr int kSdfPageSize = 1024;
// Den syntetiska fetstilens förskjutning av kanten i em (per sida — FreeTypes
// FT_GlyphSlot_Embolden gör glyfen 1/24 em bredare totalt) och kursivens
// skjuvning (≈ 12°, som FT_GlyphSlot_Oblique).
inline constexpr float kSyntheticBoldEm = 1.0f / 48.0f;
inline constexpr float kSyntheticItalicShear = 0.2126f;

// ETT AVSTÅNDSFÄLT — det `makeSdf` ger: texlarna och var de ligger.
struct SdfField {
    int   width  = 0;   // fältets mått i texlar, marginalen inräknad
    int   height = 0;
    int   left   = 0;   // fältets övre vänstra texel relativt pennan på
    int   top    = 0;   // baslinjen, i texlar (top negativ ovanför)
    // 0,5 + d / (2 · radien), klampat till 0–1: d är avståndet till kanten
    // i texlar, POSITIVT inne i glyfen — 0,5 ligger på kanten.
    std::vector<std::uint8_t> values;
};

// Rastret (rasteriserat i `scale` × fältets täthet) till ett fält med
// `radius` texlars marginal runt glyfen. Kanten är täckningen 0,5;
// avståndet räknas exakt (euklidiskt, Felzenszwalb–Huttenlocher) till
// gränsen mellan inne- och ute-pixlar i rastrets upplösning och tas sedan ned
// `scale` gånger. En tom glyf (mellanslag) ger ett tomt fält.
void makeSdf(const GlyphRaster& in, int radius, int scale, SdfField& out);

// SDF-ATLASEN för EN FONTFAMILJ i EN renderer (per familj och fönster;
// alla fyra stilarna i SAMMA atlas). Den
// vanliga stilens font laddas i konstruktorn (`valid()` avser den), de andra
// LAT första gången en glyf i stilen behövs — och går en stil inte att ladda
// ritar den vanliga, och skillnaden syntetiseras. Glyfernas nyckel är (stil,
// kodpunkt): varje glyf rasteriseras, görs till ett fält och laddas upp
// FÖRSTA gången den behövs, sedan ligger den kvar. Sidorna (R8, 1024²) växer
// i antal — en glyf vräker aldrig en annan, så det som byggts mot atlasen
// gäller tills `clear()`. En etikett med vanliga och feta delar hamnar därför
// på samma sidor och ritas i samma anrop. Atlasen hör till renderern: släpp
// den före fönstret.
class SdfAtlas {
public:
    SdfAtlas(Renderer& r, FontFamily family);
    SdfAtlas(const SdfAtlas&)            = delete;
    SdfAtlas& operator=(const SdfAtlas&) = delete;

    bool valid() const { return fonts_[0] && fonts_[0]->valid(); }
    // Fonten en stil ritas med — den vanliga när stilen inte gick att ladda
    // (laddas lat); nullptr när atlasen är ogiltig.
    const Font* font(FontStyle s = FontStyle::Regular);
    // Stilen den fonten FAKTISKT har — skillnaden mot den begärda
    // syntetiseras (fet som en förskjuten tröskel, kursiv som skjuvade hörn).
    FontStyle style(FontStyle s = FontStyle::Regular);

    // Radens mått i TEXLAR (fältets täthet) för en stil: baslinjen under
    // radens överkant och radavståndet.
    float ascent(FontStyle s = FontStyle::Regular);
    float lineHeight(FontStyle s = FontStyle::Regular);

    struct Glyph {
        int   page    = -1;     // atlassidan; −1 = inget fält (mellanslag)
        float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;   // fältets UV
        float left = 0.0f, top = 0.0f;   // fältets hörn relativt pennan på
                                         // baslinjen, texlar (top negativ)
        float w = 0.0f, h = 0.0f;        // fältets mått, texlar
        float advance = 0.0f;            // pennans förflyttning, texlar
    };

    // Glyfen för kodpunkten i stilen — rasteriserad ur stilens font, gjord
    // till fält och uppladdad första gången. nullptr bara när atlasen är
    // ogiltig (eller sidan inte gick att skapa).
    const Glyph* glyph(char32_t cp, FontStyle s = FontStyle::Regular);

    // Glyfens STEG i texlar — utan rasterisering, fält eller uppladdning
    // (`measureText` nedan): en byggd glyfs steg, annars stilens
    // `Font::measure` för kodpunkten (samma tal som `rasterGlyph` ger —
    // kontraktet vid buildText), cachat. En syntetisk fet räknas INTE in:
    // den är layoutens. 0 när atlasen är ogiltig.
    float advance(char32_t cp, FontStyle s = FontStyle::Regular);

    std::size_t  pageCount() const { return pages_.size(); }
    const Image* page(std::size_t i) const
    {
        return i < pages_.size() ? pages_[i].get() : nullptr;
    }

    // Tömmer atlasen — alla sidor och glyfer (fonterna står kvar). BARA
    // MELLAN frames (lagrets trim): det som byggts pekar på sidor
    // och UV:er.
    void clear();

private:
    bool place(int w, int h, int& page, int& x, int& y);

    Renderer&                                 r_;
    FontFamily                                family_;
    // En font per stil (FontStyle som index); [0] laddas i konstruktorn,
    // resten lat — nullptr efter ett försök betyder "den vanliga ritar".
    std::array<std::unique_ptr<Font>, 4>      fonts_;
    std::array<bool, 4>                       tried_{};
    std::vector<std::unique_ptr<Image>>       pages_;
    std::unordered_map<std::uint64_t, Glyph>  glyphs_;   // (stil << 32) | cp
    // De MÄTTA stegen (`advance`) för glyfer som inte byggts, samma nyckel.
    // Töms inte i `clear()`: fonterna står kvar, och stegen med dem.
    std::unordered_map<std::uint64_t, float>  advances_;
    int penX_ = 0, penY_ = 0, rowH_ = 0;   // hyllpackning på sista sidan
};

// ---- BYGGARNA — etiketter och markörer till GlyphVertex-quads.

// En chunk glyfquads: samma kontrakt som `LineChunk` (origo i double, vertex
// relativa det) plus atlassidan den samplar — ett rit-anrop binder EN sida.
// −1 = bara former (ingen sida behövs).
struct GlyphChunk {
    double                     originX = 0.0;
    double                     originY = 0.0;
    int                        page    = -1;
    std::vector<GlyphVertex>   vertices;
    std::vector<std::uint32_t> indices;
};

// En chunk glyfquads I RYMDEN (`MeshProgram::Glyph3`):
// `GlyphChunk`s kontrakt med origot i xyz — ankarna bär float(ankare − origo)
// i alla tre axlarna.
struct GlyphChunk3 {
    double                     originX = 0.0;
    double                     originY = 0.0;
    double                     originZ = 0.0;
    int                        page    = -1;
    std::vector<GlyphVertex3>  vertices;
    std::vector<std::uint32_t> indices;
};

enum class TextAnchor : std::uint8_t { TopLeft, Top, TopRight, Left, Center,
                                       Right, BottomLeft, Bottom,
                                       BottomRight };
// ANKARET som en förskjutning: lådans övre vänstra hörn (x, y) relativt
// ankarpunkten för en låda w × h, y nedåt — `BottomLeft` ger (0, −h). EN regel
// för etiketterna, markörerna och ritytans förankrade barn
// (`Canvas::setChildAnchor`).
void anchorCorner(TextAnchor a, float w, float h, float& x, float& y);
enum class TextAlign : std::uint8_t { Left, Center, Right };
// Vad vinkeln mäts mot: skärmen eller kartan — etiketten står upprätt i
// båda. GROUND (`glyphflag::kGroundAligned`) lägger den
// i världsplanet vid ankaret med kartans vinkel: hörnen ligger i planet, och
// storleken i px eller mm gäller vid ankaret, vinkelrätt mot blicken.
enum class TextRotation : std::uint8_t { Viewport, Map, Ground };

// Etikettens utseende. MÅTTEN (size, haloWidth, padding, offset, wrapWidth,
// maxHeight, outlineWidth) är i rit-anropets ENHET — px eller mm,
// `SceneUniforms::pxPerWidthUnit`.
struct TextLayoutStyle {
    float         size      = 12.0f;   // em
    Color         color{0, 0, 0, 255};
    Color         halo{0, 0, 0, 0};
    float         haloWidth = 0.0f;    // 0 = ingen gloria
    Color         background{0, 0, 0, 0};   // plattan; alfa 0 = ingen
    float         padding   = 0.0f;    // plattans luft runt texten
    TextAnchor    anchor    = TextAnchor::Center;
    TextAlign     align     = TextAlign::Center;   // radernas justering
    float         angleDeg  = 0.0f;    // moturs
    TextRotation  rotation  = TextRotation::Viewport;
    float         offsetX = 0.0f, offsetY = 0.0f;  // i textens ram, y nedåt
    float         wrapWidth = 0.0f;    // radbrytning; 0 = bara '\n'
    std::uint32_t label     = 0;       // kollisionsindex
    // Stilen för en text UTAN delar (strängformen nedan) och radmåttens stil
    // när alla delar är tomma.
    FontStyle     fontStyle = FontStyle::Regular;
    // TAKET: högst så många rader och högst så högt textblock (utan
    // plattans luft); 0 = inget tak, och minst en rad står alltid.
    int           maxLines  = 0;
    float         maxHeight = 0.0f;
    // KONTUREN RUNT PLATTAN: ett band `outlineWidth` brett UTANFÖR
    // plattan (bakgrunden och luften) — lådan växer med det, och bandet ritas
    // också utan bakgrund (en ram). Alfa 0 eller bredden 0 = ingen.
    Color         outline{0, 0, 0, 0};
    float         outlineWidth = 0.0f;
    // RADAVSTÅNDET: en baslinje till nästa är radhöjden × faktorn;
    // ett värde ≤ 0 eller som inte är ändligt är 1.
    float         lineSpacing  = 1.0f;
};

// EN DEL AV EN ETIKETT: texten, färgen och stilen.
// Delarna läggs ut efter varandra på samma rader; storleken, gloria, platta
// och ankare är etikettens. Texten LÅNAS — den måste leva medan byggaren
// körs. Sist delens BAKGRUND — alfa 0 = ingen — och TABBSTOPPET:
// x där delens första glyf börjar, i rit-anropets enhet från RADENS början;
// står det som kommer före redan längre fram börjar delen direkt efter det,
// och 0 är inget stopp.
struct StyledText {
    std::string_view utf8;
    Color            color{0, 0, 0, 255};
    FontStyle        style = FontStyle::Regular;
    Color            background{0, 0, 0, 0};
    float            tab = 0.0f;
};

// Etikettens låda i EM relativt ankaret, ovriden — plattans om den finns
// (med konturen), annars textblockets. Kollisionsplaceringen
// räknar på den.
struct TextBox {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
};

// Lägger ut DELARNA efter varandra vid ankaret och LÄGGER TILL quadarna i
// `chunks`: plattan med sin kontur, delarnas bakgrunder, ALLA glorior och
// sist ALLA fyllningar — över alla delar,
// och också när etiketten spänner över flera atlassidor (varje gloria-chunk
// etiketten använder ritas före varje fyllnads-chunk den använder). Färgen
// är delens, per vertex; fet och kursiv som stilens font saknar syntetiseras
// per del. En BAKGRUND är en ruta per rad bakom en följd glyfer i
// samma bakgrundsfärg, radhöjden hög från radens överkant; luckan ett
// tabbstopp lämnar bryter följden. Radavståndet (`lineSpacing`) flyttar varje
// rad efter den första, och textblocket är en radhöjd plus ett radavstånd per
// rad efter den. RADERNA: '\n' bryter alltid (också inne i en del), och en bredd
// bryter med `gfx::wrapText`s regel — vid det sista mellanslaget på eller
// före den första glyf som inte ryms, annars hårt vid den glyfen (minst en
// per rad); det brytande mellanslaget hör inte till någon rad och mellanslag
// efter ett hårdbrott hoppas över. Bredden är varje glyfs EGET steg i sin
// stil (en syntetisk fet räknas med) — samma som `Font::measure` — skalat
// till storleken, och tabbstoppens luckor; ingen kerning. Radhöjden och
// baslinjen är de STÖRSTA bland
// stilarna som används (en etikett med en stil får exakt den stilens mått;
// bara tomma delar ⇒ `style.fontStyle`s). TAKET (`maxLines`/`maxHeight`)
// kortar med `Draw::elideLines` regel: den sista raden som ryms bär
// resten av sitt stycke, blanksteg närmast ellipsen tas bort och U+2026
// läggs till — med en bredd kortas raden tills den ryms med ellipsen, utan
// läggs den efter raden; ryms inte ens ellipsen står den ändå. Ellipsen har
// den sista kvarvarande glyfens färg, stil och syntes (en tom rad:
// radbrytningens, alltså delens).
TextBox buildText(SdfAtlas& atlas, std::span<const StyledText> parts,
                  Point2d anchor, const TextLayoutStyle& style,
                  std::vector<GlyphChunk>& chunks);
// EN del: texten i `style.color` och `style.fontStyle`.
TextBox buildText(SdfAtlas& atlas, std::string_view utf8, Point2d anchor,
                  const TextLayoutStyle& style, std::vector<GlyphChunk>& chunks);

// TEXTENS MÅTT UTAN BYGGE — EN layout med `buildText`: samma
// glyflista, samma rader och samma radmått, men stegen ur
// `SdfAtlas::advance`, så INGENTING rasteriseras eller laddas upp och
// mätningen får göras utanför en frame. `box` är exakt buildText:s låda
// (luften, konturen, ankaret och förskjutningen inräknade); de andra måtten
// är TEXTBLOCKETS i em — den bredaste raden (tabbstoppen inräknade), en
// radhöjd plus ett radavstånd per rad efter den första, en rad och första
// radens baslinje under blockets överkant. En tom text är en rad.
// Tomt (`lines` 0) när atlasen är ogiltig eller storleken inte är > 0.
// TAKET räknas som i buildText: `lines` är de synliga raderna och
// `truncated` att texten kortades.
// SKRIVEN GRÄNS: mätning och ritning går isär bara när atlasen VÄGRAR en
// glyf (rasteriseringen eller sidan misslyckas) — då hoppar buildText över
// glyfen medan mätningen räknar dess steg; det gäller också ellipsen.
struct TextMeasure {
    TextBox box;
    float   width      = 0.0f;
    float   height     = 0.0f;
    float   lineHeight = 0.0f;
    float   ascent     = 0.0f;
    int     lines      = 0;
    bool    truncated  = false;   // kortad av maxLines/maxHeight
};
TextMeasure measureText(SdfAtlas& atlas, std::span<const StyledText> parts,
                        const TextLayoutStyle& style);

enum class MarkerShape : std::uint8_t { Circle, Square };

struct MarkerLayoutStyle {
    MarkerShape   shape        = MarkerShape::Circle;
    float         size         = 6.0f;   // fyllningens diameter / sida
    Color         fill{0, 0, 0, 255};
    Color         outline{0, 0, 0, 0};
    float         outlineWidth = 0.0f;   // utanför fyllningen; 0 = ingen
    float         angleDeg     = 0.0f;   // kvadratens vridning
    TextRotation  rotation     = TextRotation::Viewport;
    // Var punkten sitter på markören (textens nio ankare): Center är mitt i,
    // Bottom är mitt på underkanten — en kartnål står PÅ punkten.
    TextAnchor    anchor       = TextAnchor::Center;
    // En BILDMARKÖR (`glyphflag::kImage`): rit-anropets bild i en ruta
    // `size` × `size` (bildens proportioner — den längre sidan är `size`),
    // utan form, fyllning och kontur.
    bool          image        = false;
    std::uint32_t label        = 0;
};

// En markör per punkt (tusentals går — fyra vertex var): skärmkonstant
// storlek i enheten, med valfri kontur. LÄGGER TILL i `chunks`. Svaret är
// markörens låda i EM relativt punkten (`buildText`s form) — konturen
// inräknad, ankaret pålagt; en bildmarkörs låda är hela rutan.
TextBox buildMarkers(std::span<const Point2d> points,
                     const MarkerLayoutStyle& style,
                     std::vector<GlyphChunk>& chunks);

// ---- I RYMDEN (`GlyphChunk3`) — samma layout, samma
// quadar och samma låda som formerna ovan; bara ANKARET har ett z (och en ny
// chunk får origo i det). Quaden står upprätt i skärmen vid ankarets
// projektion. Ett ankare som inte är ändligt ger ingenting.
TextBox buildText(SdfAtlas& atlas, std::span<const StyledText> parts,
                  Vec3d anchor, const TextLayoutStyle& style,
                  std::vector<GlyphChunk3>& chunks);
TextBox buildText(SdfAtlas& atlas, std::string_view utf8, Vec3d anchor,
                  const TextLayoutStyle& style, std::vector<GlyphChunk3>& chunks);
TextBox buildMarkers(std::span<const Vec3d> points,
                     const MarkerLayoutStyle& style,
                     std::vector<GlyphChunk3>& chunks);

} // namespace gfx
