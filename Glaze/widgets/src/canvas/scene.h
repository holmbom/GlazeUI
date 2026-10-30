#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gfx/camera.h>
#include <gfx/geom2d.h>
#include <gfx/geom3d.h>
#include <gfx/mat4.h>
#include <gfx/scene.h>

#include "glaze/canvas/shape.h"
#include "glaze/canvas/style3d.h"
#include "glaze/widget.h"
#include "glaze/widgets/canvas.h"   // DrawOrder

// INTERN — ritytans LAGER. Det `glaze::Canvas` äger:
// lagren, formerna och deras CPU-geometri i KÖRNINGAR och CHUNKAR. GPU-kopian
// bor per FÖNSTER i `CanvasResources` (resources.h), som synkar mot
// chunkarnas ändringslogg.
//
// MODELLEN:
//   SceneData   ritytans lager i ordning.
//   LayerData   formerna (ShapeEntry, index = det publika indexet) och
//               körningarna de byggts till.
//   RunData     en KÖRNING: på varandra följande delar med samma nyckel
//               (program, enhet, gradient, bild, font) — det som delar
//               rit-anropens uniformer. Line/Fill/Image bär CPU-chunkar;
//               Glyph (text och markörer) bär bara formerna, för texten
//               byggs mot FÖNSTRETS atlas (resources.cpp).
//   ChunkData   ≤ 65 536 vertex med ETT origo i double (RTC) — ETT
//               rit-anrop. Världens AABB för gallringen, och en ÄNDRINGSLOGG
//               som fönstren laddar upp ur.
//
// ORDNINGEN (`DrawOrder`):
//   Strict  formens index ÄR målarordningen. En ny körning börjar vid varje
//           nyckelbyte, och inom en körning börjar en ny chunk när formen
//           ligger för långt från chunkens origo (`kChunkCell`) — ordningen
//           bevaras exakt, precisionen likaså.
//   ByKind  bilderna, ytorna, linjerna och texten i den ordningen, var för
//           sig grupperade på nyckel och sedan SPATIALT i rutor om
//           `kChunkCell` världsenheter. Inom ett slag ritas formerna ruta
//           för ruta — få rit-anrop, men två överlappande former i olika
//           rutor ritas i rutornas ordning och inte i indexordning.
//
// BORTTAGNINGEN degenererar formens index (DÖD data — ingen ny geometri, bara
// indexintervallet laddas upp igen); när mer än en fjärdedel av lagrets index
// är döda KOMPAKTERAS lagret: allt byggs om ur de levande formerna.
//
// I RYMDEN: formerna med höjd blir Line3- och
// Glyph3-delar, chunkarna har origo och låda i xyz, och lagret har en
// PLACERING och ett RENDERLÄGE (style3d.h). Ritningen är delad i tre —
// `prepareLayers` (allt som laddar upp), `drawGround` (markens geometri
// och/eller etiketter) och `drawWorld` (World-lagren med djup) — så att
// 3D-målet (target3d.h) kan lägga dem i sin ordning, medan 2D-vägen anropar
// förberedelsen och `drawGround(All)` i ett svep.

namespace glaze {

class CanvasResources;
struct Ctx;

namespace detail {

struct HitGrid;        // hit_index.h — träffprovets rutnät, byggt lat per lager
struct TerrainIndex;   // drape.cpp — terrängens rutnät, byggt lat per scen

// SPATIAL GRUPPERING: rutans sida i världsenheter, och samtidigt det längsta
// avstånd en form får ligga från sin chunks origo i Strict. Felet i en vertex
// är ≈ |punkt − origo| · 2⁻²⁴ — vid SWEREF ger rutans diagonal (5,8 km)
// ≈ 0,35 mm (< 0,1 px vid 100 px/m). Världsenheten förutsätts
// vara rimlig: meter, px, mm. En värld i GRADER behöver en egen projektion.
inline constexpr double kChunkCell = 4096.0;

// Andelen döda index som utlöser kompakteringen.
inline constexpr double kCompactDeadShare = 0.25;

// VYERNA: huvudvyn är vy 0 — ritytan själv — och
// del-vyerna (style3d.h `View`) är 1, 2 … i `Canvas::addView`s ordning. Ett
// lager ritas i vyerna i sin `LayerData::views`.
inline constexpr int kMainView = 0;
// ETIKETTPLACERINGENS PLATS för en bild ur `Canvas::renderToBitmap`: bilden
// har sin egen kamera och placerar sina etiketter för sig, så skärmens
// placering (träffprovet, toningen) står orörd (`PaintContext::labelSlot`).
inline constexpr int kBitmapLabels = -1;
// …och för ett MÅL i `ExternalCanvasPass::render` (ett headsets öga): målet
// n har plats `kExternalLabels − n`. Egna platser, så varken skärmens
// placering eller bildens rörs.
inline constexpr int kExternalLabels = -2;

// Vad en del ritas med — och slagens ordning i ByKind: linjerna i rymden
// efter de plana, och texten i rymden efter den plana — text överst.
// FÖREMÅLEN I RYMDEN är Mesh (meshar, ytor i rymden och
// instanser) och Points (punkter), efter ytorna och före linjerna: det
// ogenomskinliga först, linjerna och texten över. HELSKÄRMSPASSEN
// (`Shape::screenPass`) står först — de ritas först i bildrutan (target3d.h
// steg 1), oavsett ordningen.
enum class PartKind : std::uint8_t { Pass, Image, Fill, Mesh, Points, Line,
                                     Line3, Glyph, Glyph3 };

gfx::MeshProgram programOf(PartKind k);

// Föremålsslagen (Mesh, Points) — körningen bär formerna (`RunData::objects`)
// och fönstret håller dem residenta (scene3d.h), som glyfslagen bär sina.
// Passens körning bär också sina former i `objects`, men är inget föremål:
// de har ingen data att hålla resident.
inline bool isObjectKind(PartKind k)
{
    return k == PartKind::Mesh || k == PartKind::Points;
}

// Linjeslagen (Line, Line3) — konturens uniformer gäller dem — och
// glyfslagen (Glyph, Glyph3), som byggs per fönster och är etiketter.
inline bool isLineKind(PartKind k)
{
    return k == PartKind::Line || k == PartKind::Line3;
}
inline bool isGlyphKind(PartKind k)
{
    return k == PartKind::Glyph || k == PartKind::Glyph3;
}

// Det som delas av ETT rit-anrops uniformer och bindningar.
struct RunKey {
    PartKind     kind = PartKind::Line;
    Unit         unit = Unit::Px;     // Line: bredden; Glyph: storleken
    bool         gradient = false;    // Line/Fill: färgen är anropets
    Paint        paint;               //   …och den här (bara vid gradient)
    ImageSource  image;               // Image; Glyph: markörernas bild
    float        opacity = 1.0f;      // Image
    // Image: FÄRGJUSTERINGEN (shape.h `ImageStyle`) — anropets
    // uniformer (build.h `imageUniforms`), som opaciteten.
    float        saturation = 1.0f, contrast = 1.0f, brightness = 1.0f;
    std::uint8_t font = 0;            // Glyph: 0 = markörer, annars 1 + familj
    // Line: konturens UNIFORMER — spetsgränsen (förvalet för andra skarvar
    // än spetsiga, så de delar körning) och strecken (`makeDash`s form, med
    // mönstrets enhet; skalan sätts per rit-anrop, build.h `strokeUniforms`).
    float          miterLimit = 4.0f;
    gfx::SceneDash dash;
    Unit           dashUnit = Unit::Px;
    // Glyph: LIGGANDE text och markörer (`Alignment::Ground`) — en egen
    // körning, för i 3D-målet ligger de med marken (steg 2, och
    // i draperingen på terräng) och inte överst med etiketterna.
    bool           ground = false;
    // Glyph: GOLVET OCH TAKET för en em på skärmen — anropets
    // uniformer (`SceneUniforms::glyphMinPx`/`glyphMaxPx`, build.h
    // `glyphLimits`), som strecken; värdet ≤ 0 eller icke-ändligt = ingen.
    Length         minSize, maxSize;

    bool operator==(const RunKey& o) const;
};

// Ett nytt id ur scenens löpnummer (chunkarnas, körningarnas, lagrens) —
// globalt och atomiskt, återanvänds aldrig.
std::uint64_t nextSceneId();

// FNV-1a, 64 bitar — cachenycklarna: draperingens innehåll och markstämpel,
// terrängens region (drape.cpp) och scenens låda (`sceneBox`). En nyckel per
// fråga, inget delat tillstånd.
struct KeyHash {
    std::uint64_t h = 14695981039346656037ull;
    void          mix(std::uint64_t v)
    {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (8 * i)) & 0xffu;
            h *= 1099511628211ull;
        }
    }
    void mix(double v)
    {
        std::uint64_t b = 0;
        std::memcpy(&b, &v, sizeof b);
        mix(b);
    }
};

// En form i ett lager — delad mellan lagrets lista och Glyph-körningen den
// ritas i (som läser den när fönstret bygger texten), eller Mesh- och
// Points-körningen. `id` är FORMPLATSENS: det
// överlever `setShape` och ombyggnaden, så fönstrets residens kan rita den
// gamla meshen på platsen tills den nya är uppladdad (scene3d.h).
struct ShapeEntry {
    Shape         shape;
    bool          alive = true;   // false efter borttagning — körningen hoppar över den
    std::uint64_t id    = nextSceneId();
    // FORMENS VERSION på platsen — stegas av varje `setShape`
    // (`replaceShapeAt`), så det som nycklas på platsen och läser formen
    // (lådan nedan, terrängens region i drape.cpp) ser att den bytts.
    std::uint64_t version = 0;
    // FÖREMÅLETS LÅDA (`objectBox(entry)`) räknad med formens
    // och meshens version (`MeshData::version` — `setPositions`) — en cache,
    // därför `mutable` (`SceneData::fallbackLogged`s skäl). UI-tråden.
    mutable std::optional<Box3> box;
    mutable std::uint64_t       boxVersion = 0, boxMesh = 0;
    mutable bool                boxCached  = false;
};

// FÖREMÅLETS LÅDA för platsens form — build.h `objectBox(form)`, räknad om
// bara när formen eller meshens lägen ändrats (scenens låda, terrängens
// rutnät, draperingarna och skymmarnas ordning frågar den). Tom som
// `objectBox(form)`.
const std::optional<Box3>& objectBox(const ShapeEntry& entry);

// En chunk: CPU-geometrin och ändringsloggen (GPU-kopian är per fönster).
struct ChunkData {
    ChunkData();

    const std::uint64_t id;   // globalt, återanvänds aldrig
    PartKind            kind = PartKind::Line;
    double              originX = 0.0, originY = 0.0;
    double              originZ = 0.0;   // Line3 (2D-slagen ligger på z = 0)
    std::vector<gfx::LineVertex>  lines;
    std::vector<gfx::LineVertex3> lines3;   // Line3
    std::vector<gfx::FillVertex>  fills;
    std::vector<gfx::ImageVertex> images;
    std::vector<std::uint32_t>    indices;

    // Världens AABB över chunkens punkter (i z bara för Line3 — de plana
    // slagen har z = 0), och den största halva bredden i körningens enhet
    // (gallringens skärmmarginal; i världsenheter för en linje vars bredd är
    // i världen — den läggs då på AABB:n i stället).
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
    double minZ = 0.0, maxZ = 0.0;
    bool   hasBox = false;
    float  margin = 0.0f;

    // ÄNDRINGSLOGGEN: varje ändring stegar `version` och antecknas; ett
    // fönster som synkat till version v laddar upp ändringarna efter v om
    // loggen räcker dit (v ≥ `base`), annars hela chunken. Högst
    // `kMaxChanges` ändringar — sedan ger den upp sin äldre halva.
    struct Change {
        std::uint64_t version = 0;
        std::size_t   vtxFrom = 0, vtxCount = 0;   // vertex att ladda upp
        std::size_t   idxFrom = 0, idxCount = 0;   // index att ladda upp
    };
    static constexpr std::size_t kMaxChanges = 32;
    std::uint64_t       version = 1;
    std::uint64_t       base    = 1;
    std::vector<Change> changes;

    std::size_t vertexCount() const;
    std::size_t vertexSize() const;   // byte per vertex
    const void* vertexData() const;
    void        note(std::size_t vtxFrom, std::size_t vtxCount,
                     std::size_t idxFrom, std::size_t idxCount);
    // Lådan växer med [x0, x1] × [y0, y1] × [z0, z1] (z bara för Line3).
    void        grow(double x0, double y0, double x1, double y1,
                     double z0 = 0.0, double z1 = 0.0);
};

// Var en forms geometri ligger — för borttagningen och ersättningen på plats.
// `part` är delens index i formens `partKeys` (en del som inte rymdes i en
// chunk har flera bitar); `dead` är indexen i intervallet som redan är döda
// (en ersättning på plats med färre index fyller ut resten).
struct Piece {
    ChunkData*   chunk = nullptr;
    std::uint8_t part  = 0;
    std::size_t  vtxFrom = 0, vtxCount = 0;
    std::size_t  idxFrom = 0, idxCount = 0;
    std::size_t  dead = 0;
};

struct RunData {
    RunData();

    const std::uint64_t id;   // globalt — fönstrets textbygge nycklas på det
    RunKey              key;
    DrawOrder           order = DrawOrder::Strict;   // lagrets, när den skapades

    // Line/Fill/Image: chunkarna i ritordning. Strict fyller den sista; ByKind
    // har en "svans" per ruta (`cells`: ruta → index i `chunks`).
    std::vector<std::shared_ptr<ChunkData>>         chunks;
    std::map<std::pair<long long, long long>, std::size_t> cells;

    // Glyph och Glyph3: formerna i ordning — fönstret bygger dem mot sin
    // atlas — och ANKARNAS låda i världen (växer med varje form, krymper
    // inte vid borttagning — som chunkarnas): scenens låda (`sceneBox`).
    std::vector<std::shared_ptr<ShapeEntry>> glyphShapes;
    std::uint64_t glyphVersion = 1;   // stegas vid varje ändring av texten
    double        minX = 0.0, minY = 0.0, minZ = 0.0;
    double        maxX = 0.0, maxY = 0.0, maxZ = 0.0;
    bool          hasBox = false;

    // Mesh och Points: FÖREMÅLEN i ordning — fönstret
    // håller deras data residenta och ritar dem (scene3d.h). Lådan ovan gäller
    // inte dem: en mesh kan ändra sina lägen efter tillägget, så scenens låda
    // räknas ur föremålen när den behövs. Pass: HELSKÄRMSPASSEN i
    // ordning.
    std::vector<std::shared_ptr<ShapeEntry>> objects;
};

// En form i lagret och var dess geometri byggdes. Indexet i `LayerData::
// slots` ÄR formens publika index.
struct ShapeSlot {
    std::shared_ptr<ShapeEntry> entry;
    std::vector<Piece>          pieces;              // Line/Fill/Image
    RunData*                    glyphRun = nullptr;  // Glyph-körningen
    RunData*                    objectRun = nullptr; // Mesh/Points/Pass-körningen
};

struct LayerData {
    LayerData();

    const std::uint64_t id;
    TrText              name;
    bool                visible   = true;
    float               opacity   = 1.0f;
    double              minZoom   = 0.0;   // 0 = obegränsat
    double              maxZoom   = 0.0;
    bool                collision = true;  // etiketterna kolliderar
    bool                snap      = true;  // hörnen och kanterna träffas
    DrawOrder           order     = DrawOrder::Strict;
    // I RYMDEN (style3d.h): var lagret ligger i 3D-målet
    // och dess renderläge — och hur många av formerna som har höjd, det
    // `LayerPlacement::Auto` avgörs på (räknas vid varje ändring och vid
    // ombyggnaden).
    LayerPlacement      placement = LayerPlacement::Auto;
    RenderState         state;
    std::size_t         shapes3D  = 0;
    // HELSKÄRMSPASSEN i lagret — räknade som `shapes3D`; ett pass
    // tar ritytan till 3D-målet men ändrar inte lagrets placering.
    std::size_t         passes    = 0;
    // MARKENS INNEHÅLL — de former som MÅLAS PÅ MARKEN när lagret är ett
    // Ground-lager (`paintsGround`), räknade som `shapes3D`: markdjupet
    // (target3d.h steg 3) ritas bara när ett ritat Ground-lager har någon.
    std::size_t         groundShapes = 0;
    // VYERNA lagret ritas i (`Canvas::setLayerViews`):
    // sorterade och unika, huvudvyn från start. Tom = ingen vy.
    std::vector<int>    views{kMainView};
    // MÅLARENS PLATS (`Canvas::setLayerPainter`): ritytans
    // `onPaintLayer` ritas i lagret, efter dess former (`LayerPaint` nedan).
    bool                painter = false;

    std::vector<ShapeSlot>                slots;
    std::vector<std::shared_ptr<RunData>> runs;   // i ritordning

    // FORMERNAS VERSION — stegas av varje ändring av formerna (tillägg,
    // borttagning, ersättning, ombyggnad). Träffprovets rutnät
    // byggs lat ur formerna och gäller bara för den version det byggdes ur.
    std::uint64_t            shapesVersion = 1;
    std::shared_ptr<HitGrid> hit;
    // TEXTENS VERSION — stegas av `touchText` (språkbytet), som bygger om
    // glyfkörningarna utan att formerna ändrats: draperingens markstämpel
    // (drape.cpp) läser den bredvid `shapesVersion`.
    std::uint64_t            textVersion = 1;

    // Det döda — kompakteringens mått. Index för Line/Fill/Image, former för
    // Glyph-körningarna (deras text byggs om ur formerna i fönstret) och för
    // föremålens körningar.
    std::size_t totalIndices = 0;
    std::size_t deadIndices  = 0;
    std::size_t totalGlyph   = 0;
    std::size_t deadGlyph    = 0;
    std::size_t totalObjects = 0;
    std::size_t deadObjects  = 0;
    bool        dirty        = false;   // byggs om före nästa ritning
};

// ETT EGET PROGRAM på ritytan (`Canvas::addProgram`):
// beskrivningen, dess VERSION — ett id ur scenens löpnummer, nytt vid varje
// `addProgram`/`setProgram`/`setPrograms` — och ett LIVSTECKEN: fönstrets
// kompilerade kopia (resources.h) är nycklad på versionen och släpps i
// `trim` när livstecknet dött (programmet ersatt eller borttaget, ritytan
// riven), så den överlever aldrig en ändring av källan.
struct ProgramSlot {
    Program                     program;
    std::uint64_t               version = nextSceneId();
    std::shared_ptr<const char> life    = std::make_shared<const char>();
};

struct SceneData {
    SceneData();

    const std::uint64_t                     id;
    std::vector<std::shared_ptr<LayerData>> layers;
    bool                                    yUp = false;   // bildernas uv
    // De egna programmen i `addProgram`s ordning — namnen unika.
    std::vector<ProgramSlot>                programs;
    // Programnamnen vars RESERV redan loggats (en gång per rityta och namn,
    // scene3d.cpp) — en loggcache, därför `mutable`: ritningen ser scenen
    // som const.
    mutable std::set<std::string>           fallbackLogged;
    // MINNESBUDGETEN (`Canvas::setMemoryBudget`): bilderna och
    // draperingarna ritytan ritar, i byte; 0 = `App::canvasMemoryBudget()`.
    std::size_t                             memoryBudget = 0;
    // LAGRENS MÄNGD — stegas av varje sättare i canvas.cpp
    // som ändrar lagrens mängd, ordning, synlighet, placering eller vyer
    // (`addLayer`, `setLayers`, `removeLayer`, `moveLayer`,
    // `setLayerVisible`, `setLayerPlacement`, `setLayerViews`). Terrängens
    // rutnät (drape.cpp) prövar den, meshernas generation och Terrain-lagrens
    // `shapesVersion` i O(1) per höjdfråga. Del-vyernas omnumrering
    // (`removeView`, `setViews`) stegar den inte — scenens låda läser därför
    // vyerna själv.
    std::uint64_t                           layerSetVersion = 1;
    // SCENENS LÅDA PER VY (`sceneBox`) med sin nyckel — en cache, `mutable`
    // som `fallbackLogged`.
    struct BoxCache {
        int                 view = kMainView;
        std::uint64_t       key  = 0;
        std::optional<Box3> box;
    };
    mutable std::vector<BoxCache>           boxCache;
    // TERRÄNGENS RUTNÄT (drape.cpp) — byggt lat av höjdfrågan och
    // regionnyckeln, ombyggt när det blivit inaktuellt.
    mutable std::shared_ptr<TerrainIndex>   terrainIndex;
};

// RITYTANS KONTO i fönstrets bildcache: medan objektet lever räknas
// varje bild, render-textur och omräknad bild som hämtas genom `c.images` på
// scenen (id, scenen som livstecken och budgeten). Implementerad i scene.cpp,
// så widgetfilen inte behöver bildcachen; utan bildcache gör den ingenting.
class ImageAccount {
public:
    ImageAccount(const Ctx& c, const std::shared_ptr<SceneData>& scene);
    ~ImageAccount();
    ImageAccount(const ImageAccount&)            = delete;
    ImageAccount& operator=(const ImageAccount&) = delete;

private:
    ImageCache*   cache_    = nullptr;
    std::uint64_t previous_ = 0;
};

// Programmet med namnet, eller nullptr.
const ProgramSlot* findProgram(const SceneData& scene, const std::string& name);

// ---- BYGGET (scene.cpp)

// Lägger formen sist i lagret och bygger dess geometri (inkrementellt).
void appendShape(LayerData& layer, Shape s, bool yUp);
// Tar bort formen på index `i` (DÖD data; över `kCompactDeadShare` märks
// lagret för kompaktering, som sker före nästa ritning).
void removeShapeAt(LayerData& layer, std::size_t i);
// Ersätter formen på index `i` — på plats om den nya ryms där den gamla låg,
// annars byggs lagret om.
void replaceShapeAt(LayerData& layer, std::size_t i, Shape s, bool yUp);
// Bygger om hela lagret ur de levande formerna (setShapes, kompaktering,
// ordningsbyte, y-riktningen).
void rebuildLayer(LayerData& layer, bool yUp);
// Ersätter ALLA former (setShapes): lagret byggs på en gång före nästa
// ritning.
void assignShapes(LayerData& layer, std::vector<Shape> shapes);
// Glyph-körningarnas text ska byggas om i fönstren (språkbyte) — lagrets
// `textVersion` stegas, `shapesVersion` inte (formerna står kvar).
void touchText(LayerData& layer);

// ---- PLACERINGEN (style3d.h)

// Lagrets placering med Auto avgjord: World med någon form i rymden, annars
// Ground. Terrain står kvar (det ritas som World tills draperingen finns).
LayerPlacement effectivePlacement(const LayerData& layer);
// Har ritytan ett SYNLIGT World- eller Terrain-lager? Ett av villkoren för
// 3D-målet (canvas.h).
bool hasWorldLayer(const SceneData& scene);
// Har ritytan ett helskärmspass i ett synligt lager? Också ett av
// villkoren för 3D-målet.
bool hasScreenPass(const SceneData& scene);
// Ritas lagret i vyn?
bool layerInView(const LayerData& layer, int view);
// MÅLAS formen PÅ MARKEN i ett Ground-lager? Allt utom ett helskärmspass och
// en STÅENDE etikett eller markör (text och markörer utan `Alignment::Ground`
// ritas överst, steg 5 — de är inte marken). `LayerData::groundShapes`
// räknar dem.
bool paintsGround(const Shape& shape);
// Har lagret något som målas på marken? Ritningens (`prepareLayers` —
// markdjupet, target3d.h steg 3) och träffprovets (canvas.cpp) EN fråga: ett
// tomt Ground-lager — ritytans lager 0 finns alltid — gör ingen mark.
bool paintsGround(const LayerData& layer);
// Scenens låda i världen över de synliga lagren i VYN — chunkarnas och
// etikettankarnas lådor (de plana slagen på z = 0). Tom för en tom scen.
// 3D-kamerans klipplan och kartkamerans djupintervall räknas ur den, så en
// del-vys lager (en navigeringskub vid origo) aldrig drar ut huvudvyns
// djupintervall över en SWEREF-karta — eller tvärtom. CACHAD per vy:
// nyckeln är varje lagers id, synlighet i vyn och
// `shapesVersion` och meshernas generation (mesh_data.h) — O(lager) per
// fråga i stället för O(föremål), för den frågas vid varje musrörelse,
// `castRay`, `hitTest` och `project`.
std::optional<Box3> sceneBox(const SceneData& scene, int view = kMainView);

// LAGRETS RENDERLÄGE i fasadens form: etiketterna
// (`label`) skriver aldrig djup — de står upprätta, och en etiketts gloria och
// fyllning ligger på samma djup (gloria hade skymt fyllningen). Målaren
// (`Painter::setRenderState`) går samma väg.
gfx::RenderState gfxState(const RenderState& s, bool label);
// Ett renderläge fasaden tar emot: varje uppräkning inom sina värden och
// biaserna ändliga — annars hade varje rit-anrop med det vägrats.
bool validRenderState(const RenderState& s);

// ---- RITNINGEN (scene.cpp)

// Det en ritning av ritytan delar: Ctx:t (renderer, bildcache, skala, mm),
// fönstrets resurser, världens och skärmens vy och tonen (inaktiverad yta).
//
// 3D-VYN är satt i 3D-målet och ALDRIG i 2D-vägen: med
// den går matriserna (`chunkMatrix`), gallringen och etiketternas projektion
// genom vyn, och `world` står kvar för det som mäts i KAMERANS MITT — skalan
// för text och markörer i världsenheter och strecken i px/mm (med en
// `Camera3D` är det skalan vid målet, camera_math `mapViewAt`).
//
// VYN: ritningen gäller vy `view` — bara lagren i den
// ritas (`LayerData::views`; ett lager utanför vyn hålls resident som ett
// utanför sitt zoomintervall) — och etiketterna placeras i `labelSlot`:
// vyns egen placering, eller bildens (`kBitmapLabels`), som ritar
// huvudvyns lager med sin egen kamera.
struct PaintContext {
    const Ctx*       c   = nullptr;
    CanvasResources* res = nullptr;
    gfx::MapView     world;    // kameran
    gfx::MapView     screen;   // ritytans lokala logiska px
    double           zoom    = 1.0;   // kamerans, logiska px per världsenhet
    float            opacity = 1.0f;  // tonen — 1 − iconFade när inaktiverad
    std::optional<gfx::View3D> view3D;   // 3D-målets vy (se ovan)
    int              view      = kMainView;   // vyn som ritas (se ovan)
    int              labelSlot = kMainView;   // etikettplaceringens plats
    // LJUSET för meshar (`Canvas::setLighting`) — ritytans;
    // nullptr ritar med `Lighting`s förval.
    const Lighting*  lighting  = nullptr;
};

// LAGRETS MÅLARE: ritytans `onPaintLayer` för ett lager med
// `LayerData::painter`, anropad av `drawGround`/`drawWorld` i lagrens ordning
// efter lagrets former — med ritningens läge för lagret (tonen gånger lagrets
// opacitet), lagrets plats och, i ett World- eller Terrain-lager, dess
// renderläge som målarens start (nullptr på marken: inget djup, som lagrens
// former där). Den hör till huvudvyn och bilden, som `onPaint`.
using LayerPaint =
    std::function<void(PaintContext& f, int layer, const RenderState* state)>;

// Matrisen chunk-lokalt → pixelklipp för en chunk med origot (ox, oy, oz):
// 3D-vyns när den är satt, annars kartkamerans — med oz ≠ 0 förskjuten i z
// (en form i rymden ritad i 2D-vägen); med oz = 0 exakt kartkamerans matris.
gfx::Mat4d chunkMatrix(const PaintContext& f, double ox, double oy,
                       double oz = 0.0);

// Fysiska px per enhet för ett rit-anrop (pxPerWidthUnit): px = skalan, mm =
// skärmens täthet, world = kamerans skala i mitten (text och markörer).
float pxPerUnit(const PaintContext& f, const gfx::MapView& vy, Unit u);

// Syns världsrutan (med `marginPx` fysiska px runt sitt projicerade avtryck)
// i vyns yta? Konservativ: ett hörn bakom kameran ritar alltid.
bool boxVisible(const gfx::MapView& vy, double minX, double minY, double maxX,
                double maxY, float marginPx);
// Samma fråga för en låda i rymden med ritningens kamera: i 2D-vägen och
// för en plan låda (z = 0) EXAKT `boxVisible` ovan, annars lådans åtta hörn
// genom ritningens projektion (camera_math `Projection`).
bool boxVisible(const PaintContext& f, double minX, double minY, double minZ,
                double maxX, double maxY, double maxZ, float marginPx);

// Rutnätet: linjer var `size` världsenhet (glesare när de skulle hamna
// närmare än `minPx` logiska px), i färgen och bredden (logiska px, 0 =
// hårlinje). En statisk mesh per fönster — en kameraändring laddar inte upp.
void drawGrid(const PaintContext& f, double size, gfx::Color color, float minPx,
              float widthPx);
// Rutnätets mesh skapad nu, med samma villkor som `drawGrid` — 3D-målets
// förberedelse (inget får laddas upp inne i målet).
void prepareGrid(const PaintContext& f, double size, gfx::Color color,
                 float minPx, float widthPx);

// ---- LAGREN — delade i förberedelse och ritning

// Det förberedelsen lämnar åt ritningen — etiketternas `LabelFrame` och
// lagrens avgjorda placering. Definierad i labels.h (den bär `LabelFrame`,
// och labels.h ser redan den här headern); den som anropar funktionerna
// nedan inkluderar labels.h.
struct LayerFrame;

// De synliga märkta lagren byggs om nu — 3D-målet behöver scenens låda (och
// därmed vyns klipplan) före förberedelsen, som annars bygger om lager för
// lager.
void rebuildDirty(SceneData& scene);

// FÖRBEREDELSEN: de märkta lagren byggs om, varje Glyph-körning i ett synligt
// lager byggs för fönstret (RESIDENT — också utanför zoomintervallet), de
// RITADE körningarna samlas i ritordning och etiketterna placeras (labels.h).
// `residentFirst` (3D-målet): dessutom synkas varje chunk
// och hämtas varje bild och ramp de synliga lagren ritar — allt som laddar
// upp sker då HÄR, före målet. 2D-vägen synkar i ritningen.
// UPPLADDNINGEN I SYNLIGT-FÖRST-ORDNING: det som syns i vyn —
// föremålen och bilderna för en körning med en synlig chunk — går först,
// närmast ögat först, och resten sedan med det som blir kvar av framens
// budget (`prepareUploads`); det osynliga blir resident över de följande
// framesen. Chunkarna har ingen budget och synkas alla.
// `labelFade`: etiketterna tonar in och ut. ETIKETTERNA placeras för vyn
// (`f.view` — bara dess lager tar plats) i `f.labelSlot`; en DEL-VYS
// förberedelse rör bara vyns egna lager — resten har huvudvyns
// redan gjort resident.
LayerFrame prepareLayers(const PaintContext& f,
                         const std::shared_ptr<SceneData>& scene,
                         bool labelFade, bool residentFirst);

// UPPLADDNINGEN: föremålen och bildkörningarnas bilder i
// vyns synliga lager — de som syns i vyn (ritningens gallring) närmast ögat
// först, och sedan resten i lagrens ordning med det som blir kvar av framens
// uppladdningsbudget. 3D-målets förberedelse kör den (`prepareLayers`), och
// 2D-vägen efter sin ritning, där de synliga redan laddats upp. Det som är
// uppe kostar en uppslagning.
void prepareUploads(const PaintContext& f, const SceneData& scene);

// Vad `drawGround` ritar av Ground-lagren: GEOMETRIN (bilder, ytor, linjer —
// också i rymden — den liggande texten och föremålen) och/eller ETIKETTERNA
// (stående text och markörer). 2D-vägen ritar båda i ett svep — lagren och
// körningarna i ritordning. Med TERRÄNG (drape.h)
// ritar Geometry bara föremålen: resten ritas i draperingen, `Drape` —
// bilderna, ytorna, linjerna och den liggande texten, utan synlighet.
enum class GroundPart : std::uint8_t { Geometry = 1, Labels = 2, All = 3,
                                       Drape = 4 };

// MARKEN: Ground-lagren i ordning, utan djup (målarordning) — i 2D-vägen alla
// lager. Synlighet, zoomintervall och gallring som vanligt; ett lager utanför
// sitt zoomintervall hålls ändå resident. Med `paint` ritar GEOMETRIN också
// lagrens målare — aldrig etiketterna eller draperingen, som inte
// ritas om varje bildruta.
void drawGround(const PaintContext& f, const SceneData& scene,
                const LayerFrame& lf, GroundPart parts,
                const LayerPaint* paint = nullptr);

// WORLD-LAGREN i ordning, varje lager med sitt renderläge — etiketterna
// djuptestade men utan skrivning (style3d.h) — och Terrain-lagrens linjer och
// text (deras meshar ritar `drawTerrain`); med `paint` också lagrens målare
// med lagrets renderläge. Bara i 3D-målet.
void drawWorld(const PaintContext& f, const SceneData& scene,
               const LayerFrame& lf, const LayerPaint* paint = nullptr);

// TERRÄNGEN (target3d.h steg 2): Terrain-lagrens meshar
// och ytor i rymden med lagrets renderläge, var och en med sin drapering ur
// `lf.drapes` (scene3d.cpp binder den). Bara i 3D-målet.
void drawTerrain(const PaintContext& f, const SceneData& scene,
                 const LayerFrame& lf);

// Ritas lagret i ritningen — i vyn, inom sitt
// zoomintervall och inte helt genomskinligt? Ett lager som inte ritas hålls
// ändå resident. Synligheten (`LayerData::visible`) prövar anroparen.
bool layerDrawn(const PaintContext& f, const LayerData& layer);
// ZOOMINTERVALLET — lagrens (`setLayerZoomRange`) och etiketternas
// (`TextStyle::minZoom`/`maxZoom`) EN regel: 0 = obegränsat åt det hållet, och
// en okänd zoom (≤ 0, eller icke-ändlig) räknas som synlig.
bool zoomInRange(double minZoom, double maxZoom, double zoom);

// HELSKÄRMSPASSEN (target3d.h steg 1): de synliga och
// ritade lagrens pass i lagrens ordning, utan djup, med lagrets blandning och
// opacitet (scene3d.h `drawScreenPass`). Programmen och texturerna har
// förberedelsen redan hämtat. Bara i 3D-målet.
void drawPasses(const PaintContext& f, const SceneData& scene);

} // namespace detail
} // namespace glaze
