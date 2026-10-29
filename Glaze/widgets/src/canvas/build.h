#pragma once
#include <cstdint>
#include <optional>
#include <vector>

#include <gfx/geom2d.h>
#include <gfx/geom3d.h>
#include <gfx/sdf.h>

#include "glaze/canvas/shape.h"
#include "scene.h"

// INTERN — en FORM till byggarnas geometri.
// Delad av lagren (scene.cpp — resident i chunkar) och målaren (painter.cpp
// — transient, varje ritning): samma form ger samma vertex i båda vägarna.
//
// En form är en eller flera DELAR i ritordning (`partKeys`): en yta är
// fyllningen och sedan konturen, en linje konturen, en text och en markör en
// Glyph-del, en bild en Image-del. Varje del har en `RunKey` — det som delar
// ett rit-anrops uniformer.

namespace glaze::detail {

// En AFFIN TRANSFORM på formens punkter — målarens `translate`/`rotate`/
// `scale` (lagren bygger alltid med identiteten). x' = a·x + b·y + c,
// y' = d·x + e·y + f; måtten (bredder, storlekar) påverkas inte. En punkt i
// RYMDEN transformeras i sitt xy-plan och behåller sitt z.
struct Xform {
    double a = 1.0, b = 0.0, c = 0.0;
    double d = 0.0, e = 1.0, f = 0.0;

    Point apply(const Point& p) const
    {
        return Point{a * p.x + b * p.y + c, d * p.x + e * p.y + f};
    }
    // Eget namn — `{x, y}` initierar också en Point3 (shape.h:s skäl).
    Point3 apply3(const Point3& p) const
    {
        return Point3{a * p.x + b * p.y + c, d * p.x + e * p.y + f, p.z};
    }
};
inline const Xform kIdentity{};

// Formens delar i ritordning. Tom för en form som inte ritar något (en tom
// form, en osynlig färg, en ogiltig bild).
std::vector<RunKey> partKeys(const Shape& s);

// Världens AABB över formens punkter (transformerade — då rutan kring den
// transformerade lådan, som rymmer allt) — för en form i rymden lådan i xy,
// för ett föremål (mesh, instanser, punkter) `objectBox`s. false för en form
// utan punkter.
bool shapeBounds(const Shape& s, double& x0, double& y0, double& x1,
                 double& y1, const Xform& xf = kIdentity);
// Formens utsträckning i z: punkterna i rymden (föremålens låda), 0 för en
// plan form (och för en form utan ändliga punkter).
void shapeZRange(const Shape& s, double& z0, double& z1);
// FÖREMÅLETS LÅDA I VÄRLDEN med den data det har NU:
// en mesh — också primitivens och ytans — sin låda genom placeringen (de åtta
// hörnen transformerade), instanserna mängdens, punkterna molnets. Tom för en
// form som inte är ett föremål och för ett tomt föremål. En SKÄRMKONSTANT
// mesh (`MeshStyle::size`) har sin låda i skärmens enhet och svarar med sitt
// origo.
std::optional<Box3> objectBox(const Shape& s);

// Konturerna till Line-programmets bitar (LÄGGER TILL i `chunks`, byggarens
// regel). En sluten kontur har en skarv av konturens slag i startpunkten och
// inga ändar.
void buildLinePart(const Shape& s, std::vector<gfx::LineChunk>& chunks,
                   const Xform& xf = kIdentity);
// Linjen i rymden (`Shape::line3`/`polyline3`) till Line3-programmets bitar
// — samma `Stroke`-översättning som `buildLinePart`.
void buildLine3Part(const Shape& s, std::vector<gfx::LineChunk3>& chunks,
                    const Xform& xf = kIdentity);
// Hur långt konturen når från sin mittlinje, i konturens enhet (gallringens
// marginal): halva bredden, gånger spetsgränsen för spetsiga skarvar och √2
// för fyrkantiga ändar.
float linePartHalfWidth(const Shape& s);

// Konturens uniformer ur körningens nyckel: spetsgränsen och strecken, med
// mönstrets skala — `pxPerWorld` är kamerans fysiska px per världsenhet i
// MITTEN och `pxPerDashUnit` mönsterenhetens (px: skalan, mm: tätheten,
// world: samma som `pxPerWorld`). Bara Line- och Line3-körningar; andra
// lämnas orörda.
void strokeUniforms(const RunKey& k, float pxPerWorld, float pxPerDashUnit,
                    gfx::SceneUniforms& u);
// Bildens FÄRGJUSTERING ur körningens nyckel — lagrens och målarens
// EN väg till Image-programmet. Bara Image-körningar; andra lämnas orörda.
// (Opaciteten multipliceras av anroparen med lagrets eller målarens ton.)
void imageUniforms(const RunKey& k, gfx::SceneUniforms& u);

// Ringarna till Fill-programmets trianglar och kantremsor.
void buildFillPart(const Shape& s, std::vector<gfx::FillChunk>& chunks,
                   const Xform& xf = kIdentity);

// En bild: fyra hörn och sex index. Hörnen i världen i Z-ORDNING — övre
// vänster, övre höger, nedre vänster, nedre höger SOM BILDEN STÅR —
// `imageCorners` räknar dem ur rektangeln och y-riktningen, `imageCornersOf`
// ur formen (en bild på FYRA FRIA HÖRN: ringens
// {p0, p1, p3, p2}).
struct ImageChunk {
    double                         originX = 0.0, originY = 0.0;
    std::vector<gfx::ImageVertex>  vertices;
    std::vector<std::uint32_t>     indices;
};
void imageCorners(const Rect& r, bool yUp, Point out[4]);
void imageCornersOf(const Shape& s, bool yUp, Point out[4]);
// Ritar en bilds fyrhörning något? Sant för en rektangel; för fyra fria hörn
// bara när de är strikt konvexa (`gfx::quadWeights`).
bool imageQuadOk(const Shape& s);
// Hörnen i z-ordning (efter målarens transform) till fyra vertex och sex
// index. De PROJEKTIVA VIKTERNA räknas ur hörnen — ringen {c0, c1, c3, c2}
// genom `gfx::quadWeights`, tillbaka i z-ordning som {q0, q1, q3, q2} — och en
// rektangel eller parallellogram får fyra ettor; en fyrhörning utan yta får
// också det (den syns inte ändå). `uv` är var i texturen plattan samplar —
// hela bilden som förval, en rutas del av sin textur annars.
struct ImageUv {
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
};
void buildImageQuad(const Point corners[4], std::vector<ImageChunk>& chunks,
                    const ImageUv& uv = {});

// EN RUTA ur plattorna (en bild större än GPU:ns största textur,
// `ImageCache::tiles`): varje levande platta i `vertices`/`indices` —
// byggarens form, fyra vertex i z-ordning med uv 0–1 och sex index; en död
// platta har sex lika index och hoppas över — avbildas på rutans del `part`
// av hela bildens uv genom plattans HOMOGRAFI (kvadraten till fyrhörningen,
// samma avbildning som de projektiva vikterna ritar) och byggs om med
// `buildImageQuad`, samplad i rutans textur på `uv`. Rutornas plattor möts
// kant i kant och ritar tillsammans hela bilden. `originX/Y` är källans origo.
void splitImageQuads(const std::vector<gfx::ImageVertex>& vertices,
                     const std::vector<std::uint32_t>& indices, double originX,
                     double originY, const ImageUv& part, const ImageUv& uv,
                     std::vector<ImageChunk>& out);

// ---- Glyph-delarna (text och markörer) — byggs mot FÖNSTRETS atlas och
// täthet, i körningens enhet (storlekens).

// Tätheten måtten räknas om med: fysiska px per px, per mm och per
// världsenhet (kamerans skala i mitten när delen byggs).
struct GlyphUnits {
    Unit  unit           = Unit::Px;   // körningens — storlekens
    float pxPerPx        = 1.0f;
    float pxPerMm        = 1.0f;
    float pxPerWorldUnit = 1.0f;
};

// Ett mått i körningens enhet.
float toUnit(Length l, const GlyphUnits& u);

// GOLVET OCH TAKET för en em på skärmen i FYSISKA px — anropets
// uniformer (`SceneUniforms::glyphMinPx`/`glyphMaxPx`) och det placeringen,
// träffprovet, gallringen och mätningen räknar med. Tätheterna är anropets:
// px = skalan, mm = `pxPerMm`, world = kamerans skala i mitten. En gräns ≤ 0
// eller som inte är ändlig är ingen (0).
struct GlyphLimits {
    float minPx = 0.0f, maxPx = 0.0f;
};
GlyphLimits glyphLimits(Length min, Length max, float pxPerPx, float pxPerMm,
                        float pxPerWorld);
GlyphLimits glyphLimits(const RunKey& k, float pxPerPx, float pxPerMm,
                        float pxPerWorld);
// Storleken i fysiska px klampad som Glyph-programmet gör: golvet, sedan
// taket (taket vinner när de korsas).
double clampGlyphPx(double sizePx, const GlyphLimits& l);
// Den största utsträckningen från ankaret i EM (hörnet plus glorians andel) —
// gallringens marginal för en etikett som golvet gjort större än bygget.
float glyphReachEm(const gfx::GlyphChunk& c);
float glyphReachEm(const gfx::GlyphChunk3& c);

// SYNLIGHETEN EFTER ZOOM: ligger zoomen i textens eller markörens
// [minZoom, maxZoom] (scene.h `zoomInRange`)? Sant för alla andra slag och
// för en okänd zoom (≤ 0).
bool visibleAtZoom(const Shape& s, double zoom);

// Fontnyckeln för en textstil: 1 + familj (0 är markörerna).
// Atlasen bär alla fyra stilarna i EN familj, så vanliga
// och feta etiketter i samma lager delar körning — och en etikett med flera
// stilar ritas i ett anrop.
std::uint8_t    fontKey(const TextStyle& st);
gfx::FontFamily fontFamilyOf(std::uint8_t key);

// Etikettens layoutstil i körningens enhet (stilen i `fontStyle`).
gfx::TextLayoutStyle textLayout(const TextStyle& st, const GlyphUnits& u,
                                std::uint32_t label);
// ETIKETTENS DELAR i byggarens form: en vanlig text är EN del i stilens färg
// och fet/kursiv, en etikett i delar (`Shape::spans`) en per `TextSpan` med
// de osatta fälten ärvda ur stilen — bakgrunden och tabbstoppet
// osatta som inga, stoppet i körningens enhet. Strängarna LÅNAS ur formens
// `TrText` — delarna gäller medan formen (eller delarna) lever och språket
// står still.
std::vector<gfx::StyledText> textParts(const Shape& s, const GlyphUnits& u);
std::vector<gfx::StyledText> textParts(const std::vector<TextSpan>& spans,
                                       const TextStyle&             st,
                                       const GlyphUnits&            u);
// TEXTENS MÅTT — etikettens textblock utan platta, luft, ankare och
// förskjutning, mätt i atlasen UTAN uppladdning (`gfx::measureText`, samma
// layout som bygget). Måtten i storlekens enhet räknas om till `out` med
// tätheterna i `u` (px ↔ mm ur fönstret, världsenheter mot kamerans skala i
// mitten) — samma enhet är exakt. Målaren (`Painter::measureText`, i px) och
// ritytan (`Canvas::measureText`) mäter med den. nullopt för en ogiltig
// atlas; en storlek ≤ 0 ger nollor med `lines` 0.
std::optional<TextMetrics> measureLabel(gfx::SdfAtlas& atlas,
                                        std::span<const gfx::StyledText> parts,
                                        const TextStyle& style,
                                        const GlyphUnits& u, Unit out);
// BEHÖVER MÄTNINGEN VÄRLDENS TÄTHET? Sant när `out` är World, eller
// när ett mått `measureLabel` räknar om — storleken, brytbredden, takets höjd
// (`maxHeight`), golvet och taket (`minSize`/`maxSize`) och delarnas
// tabbstopp — har enheten World och ett värde > 0. Glorians och
// konturens bredd räknas inte: textblocket mäts utan dem (`gfx::measureText`
// läser aldrig glorian, och konturen når bara lådan, som `measureLabel` inte
// läser). Utan täthet hade `toUnit` tyst gett
// 0 — ritytans mätning i layouten svarar då nullopt (`Canvas::measureWith`).
// Listan följer `measureLabel`, därför står den här.
bool measureNeedsWorld(const TextStyle& st, const std::vector<TextSpan>& spans,
                       Unit out);
gfx::MarkerLayoutStyle markerLayout(const MarkerStyle& st, const GlyphUnits& u,
                                    std::uint32_t label);

// Den största utsträckningen från ankaret i quadarna, i körningens enhet —
// gallringens marginal för en chunk med glyfer och markörer (också i
// rymden).
float glyphReach(const gfx::GlyphChunk& c);
float glyphReach(const gfx::GlyphChunk3& c);

} // namespace glaze::detail
