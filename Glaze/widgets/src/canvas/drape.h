#pragma once
// INTERN — TERRÄNGEN OCH DRAPERINGEN. Ett Terrain-lagers meshar ÄR marken: de
// ritas med lagrets
// renderläge (djup Less + skrivning som förval) i 3D-målets steg 2, och varje
// mesh bär en DRAPERING — Ground-lagrens bilder, ytor, linjer och liggande
// text (`Alignment::Ground`) ritade uppifrån i ett eget mål över meshens
// XY-låda och lagda över dess yta av Mesh-programmets DRAPED-variant
// (programs3d.h). Med terräng ritas Ground-lagrens geometri BARA genom
// draperingen: ett hål i terrängen visar det som ligger bakom, och marken
// z = 0 skymmer ingenting (inget markdjup, target3d.h steg 3). Ground-lagrens
// stående etiketter och markörer står vid markhöjd — ankarets z ur
// `terrainHeight` när texten byggs (resources.cpp), med terrängens REGION
// under körningen i byggets nyckel (`terrainRegionKey`).
//
// DRAPERINGSMÅLET — ett RGBA8-mål utan djup och MSAA per Terrain-mesh
// (formplatsens id, `ShapeEntry::id`), KVADRATISKT över meshens XY-låda (den
// kortare sidan utvidgad kring mitten), ritat genom lagrens egen väg
// (scene.h `drawGround` med `GroundPart::Drape`) med en ovanifrån-`MapView`:
// samma chunkar, gallring och program som marken i 2D-vägen. Mått i px och mm
// mäts i MÅLETS px — de stämmer på skärmen där målets upplösning motsvarar
// plattans skärmavtryck (MapLibres begränsning).
//
// UPPLÖSNINGSKLASSEN — en tvåpotens 64–2048 ur plattans SKÄRMAVTRYCK (den
// största sidan av dess projicerade lådas skärmruta; ett hörn bakom
// närplanet ger taket) med HYSTERES 2×: klassen byts UPP så snart avtrycket
// växer förbi den, men NED först när den behövda klassen är en fjärdedel —
// en tvåpotens emellan hålls, så en panorering under tilt inte växlar fram
// och tillbaka vid en gräns. Bara plattor som SYNS (gallringen) får sin klass
// räknad av huvudvyn; en dold platta behåller sin.
//
// OMRITNINGEN sker BARA när plattans INNEHÅLLSNYCKEL eller klass ändrats —
// GPU-arbete, ingen uppladdning (allt är resident sedan förberedelsen).
// Nyckeln är en hash över det draperingen ritar: Ground-lagrens tillstånd
// (synligt, ritat, opacitet, ordning) och, för varje chunk och liggande
// glyfkörning som når plattans låda, dess id och version (byggets löpnummer)
// — och bildernas innehållsversion i fönstrets bildcache, så en bild som
// väntar på budgeten ritas om när den kommer. En kameraändring som inte byter
// klass ritar alltså ingenting om.
//
// NYCKELN RÄKNAS BARA NÄR MARKEN ÄNDRATS: MARKSTÄMPELN är
// O(lager) per förberedelse — varje lagers id, synlighet och placering, och
// för ett Ground-lager formernas och textens version, opacitet, ordning och
// om det ritas; skalan, mm-tätheten och y-riktningen; bildcachens
// innehållsgeneration (`ImageCache::contentGeneration`), atlasernas epok och
// temats standardikonfärger. Oförändrad stämpel, klass och låda ⇒ oförändrad
// nyckel, och den räknas inte. FALLGROPEN: den som lägger en NY INDATA i
// innehållsnyckeln (`contentKey`) måste lägga den i stämpeln också — annars
// ritas plattan inte om när indatan ändras (grindarna ser bara de indata som
// finns).
//
// SYNLIGT OCH MED BUDGET: en DOLD platta ritas aldrig om — den
// gamla draperingen står kvar (oritad i vyn) och ritas om när plattan syns.
// De synliga som behöver ritas — utan drapering (eller med en ur en annan
// låda: en flyttad formplats), med ändrad nyckel, med bytt klass, i den
// ordningen och störst avtryck först inom varje — ritas inom
// `drapeFrameBudget()` PIXLAR PER BILD (fönstrets, räknade från `trim`),
// minst en per förberedelse, så arbetet alltid går framåt och varje rityta
// kommer fram. Det som inte hann VÄNTAR (`DrapeFrame::pending`): den gamla
// draperingen visas — vid ett klassbyte samma låda i lägre upplösning, vid
// en ändring det förra innehållet; en flyttad formplats ritas utan — och
// ritytan ber om nästa bild (regel 1: den slutar be när kön är tom; en dold
// platta väntar aldrig). INNEHÅLLET KAN ALLTSÅ SLÄPA NÅGRA BILDER under en
// stor ändring. Bilden (`renderToBitmap`) ritar KOMPLETT
// (`DrapeMode::Complete`), utan budget.
//
// MÅLEN ÅTERANVÄNDS: ett mål som lämnar en drapering (klassbytet, en riven
// formplats) går till en POOL per klass i nästa `trim` — högst
// `kDrapePoolBytes` — och tas därifrån i stället för `createRenderTarget`.
// Poolen räknas i sin senaste ritytas budget och töms först när den går över.
//
// MINNET räknas i RITYTANS minnesbudget (`Canvas::setMemoryBudget`
// — förvalet `App::setCanvasMemoryBudget`) ihop med ritytans konto i
// bildcachen: SUMMAN av dess bilder och draperingar jämförs med budgeten, och
// över den släpps först ritytans mål i poolen och sedan vräks dess
// draperingar som INTE användes i framen, äldst först (LRU); bildcachens
// `trim` vräker bilderna mot samma summa (`ImageCache::setAccountExtra`).
// Vräkningen lägger målet i `retired` — det släpps i nästa `trim`, aldrig
// mitt i en frame. En platta som syns vräks aldrig (det loggas en gång). En
// platta som vräkts ritas om när den syns igen.
//
// DRAPERINGEN ÄR HUVUDVYNS: nyckeln och ritningen ser huvudvyns lager och
// zoom (zoomen ur huvudvyns senaste förberedelse), så en del-vy eller bilden
// (renderToBitmap) med en annan kamera eller ett annat lagerurval varken
// ritar om den eller får en egen — en nyckel per vy hade ritat om plattan två
// gånger varje frame. Följden är en SKRIVEN GRÄNS: ett Ground-lager
// utanför en del-vy syns ändå draperat på en terräng i vyn. Del-vyerna och
// bilden byter heller ingen klass; saknas en drapering draperas den med
// deras avtryck.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <gfx/image.h>
#include <gfx/renderer.h>

#include "scene.h"

namespace glaze::detail {

struct LayerFrame;   // labels.h

// ------------------------------------------------------------- terrängen

// Har ritningens vy TERRÄNG — ett synligt Terrain-lager som ritas
// (`layerDrawn`) och bär en mesh eller en yta i rymden? Då gäller
// draperingen (filhuvudet).
bool hasTerrain(const PaintContext& f, const SceneData& scene);

// TERRÄNGENS HÖJD under (x, y): den översta ytan i de synliga
// Terrain-lagrens meshar och ytor i rymden, rakt under punkten, på den
// FINASTE detaljnivån — en lodrät stråle uppifrån genom plockningens BVH
// (pick3d.h). Tomt i ett hål och utanför. `Canvas::heightAt` frågar samma.
// TERRÄNGENS RUTNÄT: formerna ligger i ett jämnt XY-rutnät
// (cellen = medianformens sida) per scen, byggt lat och ombyggt när lagrens
// mängd (`SceneData::layerSetVersion`), meshernas generation eller ett
// Terrain-lagers former ändrats — en fråga prövar cellens former (O(1) för
// plattor i ett rutnät) och sedan BVH:n, utan allokering.
std::optional<double> terrainHeight(const SceneData& scene, double x, double y);

// TERRÄNGENS REGION under XY-lådan [x0, x1] × [y0, y1]: id, formens version
// och meshens version för varje terrängform vars låda når den — summerade, så
// svaret är ORDNINGSOBEROENDE (en `moveLayer` bland Terrain-lagren bygger
// inte om etiketterna). 0 utan terräng i lådan. Etiketterna
// vid markhöjd (resources.h) har den i byggets nyckel: en ny platta bygger om
// bara körningarna vars ankarlåda den når.
std::uint64_t terrainRegionKey(const SceneData& scene, double x0, double y0,
                               double x1, double y1);

// TERRÄNGENS RUTNÄT RÄKNAT — hur många gånger det byggts och hur många
// former höjdfrågorna prövat (cellernas, före lådan). Processens, för
// grindarna (pick_gate HI).
struct TerrainIndexStats {
    std::uint64_t builds     = 0;
    std::uint64_t candidates = 0;
};
TerrainIndexStats& terrainIndexStats();

// ------------------------------------------------------------ draperingen

// En draperad formplats som terrängens ritning binder: målets bild och dess
// låda i världen (kvadratisk), och om bildens rad 0 ligger i lådans norra kant
// — draperingen ritad uppifrån, kameran står ovanför plattan — eller i den
// södra (underifrån: en kartkamera med y nedåt ser planet från undersidan).
// Den liggande texten i draperingen läses då rättvänd från kamerans sida.
struct DrapeView {
    const gfx::Image* image = nullptr;
    double            minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
    bool              northUp = true;
};

// FRAMENS DRAPERINGAR — förberedelsens svar till ritningen (labels.h
// `LayerFrame::drapes`): formplatsens id → draperingen.
class DrapeFrame {
public:
    const DrapeView* find(std::uint64_t entryId) const;
    void             add(std::uint64_t entryId, const DrapeView& v);
    std::size_t      size() const { return views_.size(); }
    // Synliga plattor vars drapering väntar på nästa bilds budget (filhuvudet)
    // — ritytan ber om nästa bild så länge det inte är 0.
    std::size_t      pending() const { return pending_; }
    void             addPending() { ++pending_; }

private:
    std::unordered_map<std::uint64_t, DrapeView> views_;
    std::size_t                                  pending_ = 0;
};

// Hur en förberedelse ritar draperingarna (filhuvudet): HUVUDVYN räknar
// klasserna ur avtrycket och ritar inom budgeten, en DEL-VY behåller
// klasserna och ritar inom budgeten, och BILDEN (`renderToBitmap`) behåller
// klasserna och ritar KOMPLETT — utan budget, så en bild aldrig släpar.
enum class DrapeMode : std::uint8_t { Main, View, Complete };

// Bildens pixelbudget för draperingarna (8 Mi px — två mål om 2048²),
// räknad i pixlar och inte i tid så att grindarna blir deterministiska.
std::uint64_t drapeFrameBudget();

// DRAPERINGSMÅLEN PER FÖNSTER (`CanvasResources::drapes()`), samma
// livstidsregel som resten: `trim()` överst i framen är det enda stället
// något släpps.
class DrapeCache {
public:
    explicit DrapeCache(gfx::Renderer& r) : r_(&r) {}
    DrapeCache(const DrapeCache&)            = delete;
    DrapeCache& operator=(const DrapeCache&) = delete;

    // Framens början: målen som lämnade en drapering i förra framen går till
    // poolen (eller släpps), budgeten börjar om, och draperingarna vars
    // formplats dött lämnar sina mål till poolen.
    void trim();

    // ---- räknarna (grindarna)
    // Hur många gånger ett draperingsmål RITATS, hur många pixlar det blivit,
    // och hur många innehållsnycklar som
    // räknats (markstämpeln).
    std::uint64_t renders() const { return renders_; }
    std::uint64_t pixels() const { return pixels_; }
    std::uint64_t keysComputed() const { return keysComputed_; }
    // Draperingarna (formplatser med ett mål) och deras byte — poolens med.
    std::size_t   count() const;
    std::size_t   bytes() const;
    // En ritytas draperingar och mål i poolen (scenens id) — budgeten räknas
    // per rityta.
    std::size_t   bytes(std::uint64_t scene) const;
    // Formplatsens upplösningsklass (målets sida i px), 0 utan drapering —
    // och alla draperingars (formplatsens id → klassen), som grindarna
    // jämför före och efter en kameraändring.
    int           classOf(std::uint64_t entryId) const;
    std::unordered_map<std::uint64_t, int> classes() const;

private:
    friend DrapeFrame prepareDrapes(const PaintContext& f,
                                    const std::shared_ptr<SceneData>& scene,
                                    const LayerFrame& lf, DrapeMode mode);

    struct Entry {
        std::weak_ptr<const ShapeEntry>    src;
        std::unique_ptr<gfx::RenderTarget> target;
        int                                cls = 0;           // målets sida
        std::uint64_t                      drawnKey = 0;      // målets innehåll
        // Innehållsnyckeln (0 = inget av Ground-lagren når plattan) och vad
        // den räknades med — markstämpeln, klassen och lådan.
        std::uint64_t                      key = 0;
        std::uint64_t                      keyInput = 0;
        std::uint64_t                      lastFrame = 0;     // senast synlig
        DrapeView                          view;              // det målet bär
        std::uint64_t                      scene = 0;         // ritytans (budgeten)
    };
    // Ett mål som lämnat sin drapering — till poolen i nästa trim (`pool`)
    // eller släppt där (en vräkning, eller poolen full).
    struct Retired {
        std::unique_ptr<gfx::RenderTarget> target;
        int                                cls   = 0;
        std::uint64_t                      scene = 0;
        bool                               pool  = false;
    };
    // Ett mål i poolen, med den rityta som senast bar det (budgeten).
    struct Pooled {
        std::unique_ptr<gfx::RenderTarget> target;
        std::uint64_t                      scene = 0;
    };
    static constexpr std::size_t kClasses = 6;   // 64, 128 … 2048
    // Klassens plats i poolen (64 → 0 … 2048 → 5).
    static std::size_t classSlot(int cls);

    // Målet i klassen `cls` — ur poolen, annars nytt; nullptr när det inte
    // gick att skapa (fasaden har loggat).
    std::unique_ptr<gfx::RenderTarget> takeTarget(int cls);
    // Ritar draperingen `e` i klassen `cls` över lådan `v` (filhuvudet: ett
    // klassbyte i ett NYTT mål, det gamla visas tills det nya är ritat) —
    // false när målet inte gick att skapa.
    bool render(const PaintContext& df, const SceneData& sc, Entry& e,
                const DrapeView& v, int cls);
    // Ett mål i poolen, inom taket — annars släppt.
    void toPool(std::unique_ptr<gfx::RenderTarget> t, int cls,
                std::uint64_t scene);
    // Släpper ritytans mål ur poolen, de största först, tills `bytes`
    // (ritytans) inte längre ligger över `limit`.
    void dropPooled(std::uint64_t scene, std::size_t& bytes, std::size_t limit);

    gfx::Renderer*                                  r_;
    std::unordered_map<std::uint64_t, Entry>        entries_;   // formplatsens id
    std::vector<Retired>                            retired_;
    std::array<std::vector<Pooled>, kClasses>       pool_;
    std::size_t                                     poolBytes_ = 0;
    // Huvudvyns zoom vid dess senaste förberedelse — draperingarnas lager
    // (zoomintervallen) för varje vy (filhuvudet).
    std::optional<double>                           mainZoom_;
    std::uint64_t                                   frame_   = 0;
    std::uint64_t                                   framePixels_ = 0;   // budgeten
    std::uint64_t                                   renders_ = 0;
    std::uint64_t                                   pixels_  = 0;
    std::uint64_t                                   keysComputed_ = 0;
    bool                                            overLogged_ = false;
};

// FÖRBEREDELSEN (target3d.cpp — före målet): för varje terrängform i
// vyn — en mesh eller yta i rymden i ett ritat Terrain-lager — draperingens
// klass (huvudvyn räknar den ur avtrycket; del-vyerna och bilden behåller
// den) och dess mål, ritat om när innehållsnyckeln eller klassen ändrats och
// plattan syns, inom budgeten utom för bilden (`mode`, filhuvudet —
// nästlade `pushTarget`, före huvudmålets). En platta utan Ground-innehåll i
// sin låda draperas inte. Svaret är framens draperingar och det som väntar.
DrapeFrame prepareDrapes(const PaintContext& f,
                         const std::shared_ptr<SceneData>& scene,
                         const LayerFrame& lf, DrapeMode mode);

// Upplösningsklassen ur avtrycket i px med hysteresen (filhuvudet): `current`
// 0 = ingen än. Exponerad för grindarna, som räknar avtrycket själva.
int drapeClass(int current, double footprintPx);

} // namespace glaze::detail
