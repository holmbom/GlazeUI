#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <gfx/camera.h>

#include "glaze/canvas/geometry.h"
#include "glaze/canvas/mesh.h"
#include "glaze/canvas/shape.h"
#include "glaze/widgets/canvas.h"   // HitKind
#include "camera_math.h"            // Projection
#include "mesh_data.h"

// INTERN — 3D-PLOCKNINGENS KÄRNA, utan ritytan: en
// stråle i världen mot en mesh och mot ett plan. Ren CPU-kod i double;
// ingen kamera, inget fönster. Ritytans `hitTest`, `castRay` och `heightAt`
// läggs OVANPÅ: de gör strålen (ur kameran, eller lodrätt för en höjd),
// flyttar den in i formens ram när formen har en placering eller är en
// instans (strålens parameter t är densamma i båda ramarna under en affin
// avbildning, därför svarar kärnan med t och inte bara ett avstånd), väljer
// den RITADE nivån med `lodLevel` (mesh_data.h) och sorterar träffarna över
// formerna. Skärmtoleranserna (hörn och kanter inom N px) går mot samma BVH.
//
// MESHEN: Möller–Trumbore i double i BVH:n (bvh.h), i meshens lokala rymd —
// strålens början minus meshens origo, i double — mot CPU-kopians lägen.
// Båda sidorna träffas (ingen gallring av baksidor), och den NÄRMASTE
// träffen vinner; på lika avstånd det lägsta triangelindexet, så svaret är
// entydigt också längs en delad kant. Triangelindexet är ORIGINALETS —
// platsen i `MeshDesc`:s trianglar — eftersom CPU-kopian står kvar i
// originalets ordning; segmenten (uppladdningens vyer) påverkar det inte.

namespace glaze::detail {

struct ShapeEntry;   // scene.h — formplatsen, med lådans cache

struct MeshHit {
    std::uint32_t triangle = 0;     // originalets triangelindex
    double        t        = 0.0;   // strålens parameter: punkten = början + t · riktningen
    double        distance = 0.0;   // t · |riktningen|
    Point3        point{};          // träffen i världen
    // Triangelns ENHETSNORMAL efter vindningen — (b − a) × (c − a), utåt för
    // en mesh med framsidan utåt — och om strålen kom från framsidan.
    Vec3 normal{};
    bool frontFace = true;
    // De barycentriska koordinaterna: punkten = (1 − u − v)·a + u·b + v·c.
    double u = 0.0, v = 0.0;
};

struct MeshPick {
    // Hoppa över trianglar med alfa < 255 i något hörn (färgströmmen). En
    // mesh utan färgström är ogenomskinlig här; stilens färg och opacitet är
    // anroparens.
    bool opaqueOnly = false;
    // Längst bort längs strålen, i riktningens enhet (> 0); 0 = obegränsat.
    double maxDistance = 0.0;
    // Det RITADE: nivån (`lodLevel` — `kWholeMesh` utan begränsning,
    // `kNoLevel` träffar ingenting) och stilens intervall. Bara trianglar i
    // båda kan träffas.
    int        lod = kWholeMesh;
    IndexRange range{};
};

// Den närmaste träffen, eller tomt: ingen träff, en ogiltig stråle (riktning
// noll eller inte ändlig), en mesh utan CPU-kopia (loggas en gång per mesh)
// eller en nivå/ett intervall som inte är hela trianglar i meshen (loggas en
// gång per mesh). Bygger BVH:n första gången (`Mesh::preparePicking` gör det
// i förväg).
std::optional<MeshHit> pickMesh(const Mesh& mesh, const Ray3& ray,
                                const MeshPick& opts = {});

// STRÅLEN MOT PLANET: träffen och strålens parameter t ≥ 0. Tomt när strålen
// är parallell med planet, träffen ligger bakom strålens början eller något
// inte är ändligt. Normalen behöver inte vara enhetlig. Räkningen är
// fasadens (`gfx::intersect`, gfx/camera.h) — en formel, i glazes typer.
struct PlaneHit {
    Point3 point{};
    double t = 0.0;
};
std::optional<PlaneHit> rayPlane(const Ray3& ray, const Plane3& plane);

// ------------------------------------------------------ RITYTANS PLOCKNING
// (ovanpå kärnan): strålen mot FORMERNA I RYMDEN i ett
// lager — meshar och ytor i rymden (triangeln, hörnen, kanterna), instanser,
// punkter och linjer i rymden — med KAMERAN för skärmtoleranserna, den
// RITADE detaljnivån och de skärmkonstanta meshernas storlek (scene3d.cpp:s
// regler, samma tal). Ritytan (canvas.cpp) samlar lagren, 2D-formerna
// (hit_index.h), sorteringen och skymningen.
//
// MESHENS RAM (style3d.h `Placement`): världen = origo + at + R · (skala ·
// läget), och en skärmkonstant mesh (`MeshStyle::size`) skalar läget med
// storleken × världen per px vid origot (Mesh-programmets SCREEN_SIZED).
// Instanserna lägger instansens ram utanpå prototypens (instances.h).
// Strålen förs in i meshens lokala ram — t är detsamma i båda ramarna under
// en affin avbildning — och träffen ut igen.
//
// SKÄRMTOLERANSERNA prövas på SKÄRMEN med projektionen i double: ett hörn
// inom toleransen från pekaren, en kant (avskuren mot närplanet i
// klipprymden) inom toleransen, en punkt inom toleransen + halva sin ritade
// storlek, en linje i rymden inom halva bredden + toleransen. BVH:n och
// punktmolnets chunkar sållar först med en KON kring strålen (bvh.h
// `traverseNear`). ALLA inom toleransen svaras, med sitt skärmavstånd
// (`SceneHit::screen`): ritytan väljer det närmaste SYNLIGA per form och
// sort — det närmaste på skärmen kan ligga bakom en vägg medan ett annat
// syns, och skymningen är ritytans.

// Ett mått på skärmen vid en världspunkt: px och mm som FYSISKA px (redan
// omräknade), världsenheter med kamerans skala där.
struct ScreenTol {
    double px    = 0.0;
    double world = 0.0;
    bool   any() const { return px > 0.0 || world > 0.0; }
};

// En plockning: strålen, kameran och det ritytan vet om lagret.
struct PickContext {
    Ray3 ray;   // enhetlig riktning
    // Kameran som ritar (detaljnivån, skärmkonstanta meshar) — satt utom för
    // en stråle utan kamera (`heightAt`, som tar den finaste nivån).
    const gfx::View3D* view = nullptr;
    // Skärmprojektionen och pekaren (fysiska px) — utan dem prövas bara ytor
    // (`castRay`, `heightAt`).
    const Projection* proj = nullptr;
    double            px = 0.0, py = 0.0;
    double            pxPerPx = 1.0, pxPerMm = 1.0;   // Length → fysiska px
    ScreenTol         vertexTol, edgeTol, pointTol, lineTol;
    bool              opaqueOnly  = false;
    // Längs strålen; 0 = obegränsat. TAKET når också meshens
    // BVH (`MeshPick::maxDistance`) och lådornas prov: en mesh eller instans
    // vars låda strålen går in i bortom taket (och skymningens marginal mot
    // avrundningen) plockas inte alls — varje träff i den, också ett hörns
    // eller en kants fotpunkt i konen, ligger längre bort än så.
    double            maxDistance = 0.0;
    bool              finest      = false; // nivå 0 i stället för den ritade
    float             layerOpacity = 1.0f; // helt täckande först vid 1
    bool              occluder    = false; // lagret skriver djup (skymmer)
    // YTAN längs strålen (förvalet). Utan den bara hörnen, kanterna, punkterna
    // och linjerna — ritytans träffprov har redan terrängens yta ur sin
    // första loop och plockar den inte två gånger.
    bool              surface     = true;
};

struct SceneHit {
    HitKind      kind     = HitKind::Surface;
    std::int64_t element  = -1;
    int          instance = -1;
    Point3       point{};
    Vec3         normal{};
    double       distance = 0.0;   // längs strålen
    // Skärmavståndet från pekaren i fysiska px — hörn, kant, punkt och
    // linjesegment (0 för en yta, som ligger på strålen).
    double screen = 0.0;
    // En ogenomskinlig yta i ett djupskrivande lager — den skymmer det bakom.
    bool occludes = false;
};

// Formens träffar (Mesh, Area3, Instances, Points3, Lines3 — andra slag
// ingenting), tillagda i `out`.
void pickShape3D(const Shape& s, const PickContext& c, std::vector<SceneHit>& out);
// FORMPLATSENS träffar — samma svar, men med platsens LÅDA (scene.h
// `objectBox(post)`, cachen) som första sållning:
// en mesh eller instansmängd i världens enhet vars låda strålen inte når —
// med hörnens och kanternas kon — prövas inte vidare, och dess placering
// räknas aldrig. Formens egna prov sållar med samma låda — en instans med
// sin cells, och mängdens rymmer cellernas — så ingenting faller bort.
void pickShape3D(const ShapeEntry& entry, const PickContext& c,
                 std::vector<SceneHit>& out);

// Den närmaste OGENOMSKINLIGA ytan i ett djupskrivande lager längs strålen
// bland formerna — skymningens synlinje. `layerOpacity` per form, och formens
// LÅDA i världen: `occluder` hämtar den ur formplatsens cache (scene.h
// `objectBox(post)`) EN gång per fråga, för en fråga prövar
// många synlinjer (en per hörn och kant). En skärmkonstant mesh har ingen —
// objectBox svarar med origot — och prövas alltid.
//
// FRAMIFRÅN: formerna prövas i ordning efter var strålen går in
// i deras låda, med den närmaste ytan hittills som plockningens tak
// (`PickContext::maxDistance`), och sökningen SLUTAR vid den första låda som
// börjar bortom den. Det som står bakom en vägg plockas alltså inte och bygger
// inte sin BVH; svaret är detsamma som när varje form prövas.
struct Occluder {
    const Shape*        shape        = nullptr;
    float               layerOpacity = 1.0f;
    std::optional<Box3> box;
};
Occluder occluder(const ShapeEntry& entry, float layerOpacity);
std::optional<double> nearestOccluder(std::span<const Occluder> shapes,
                                      const Ray3& ray, const gfx::View3D* view);

// SKYMNINGENS MARGINAL vid avståndet d längs strålen: det som ligger PÅ den
// skymmande ytan själv (ett hörn i den, etiketten på marken) skyms inte —
// en skymmare räknas först när den står mer än marginalen framför.
double occlusionSlack(double d);
// SKYMNINGENS TAK bakom en skymmare på avståndet d: allt längre bort än så
// skyms (det står mer än marginalen bakom den) — en plockning som bara ska
// svara med det synliga behöver inte gå längre (`castRay`).
double occlusionReach(double d);

// Plockas formen i rymden (`pickShape3D`)? Meshar, ytor i rymden, instanser,
// punkter och linjer i rymden.
bool isObject3D(const Shape& s);

// En enhetlig riktning, eller tom för en stråle som inte går att räkna på
// (riktning noll, inte ändlig, eller en början som inte är ändlig).
std::optional<Ray3> unitRay(const Ray3& r);

struct SceneData;   // scene.h

// `Canvas::castRay` över scenens lager, med den VY som skärmstorlekar,
// detaljnivå och skärmkonstanta meshar räknas mot: `view` (nullptr = ingen
// känd vy, finaste nivån och inga skärmkonstanta) och tätheterna
// `pxPerPx`/`pxPerMm` (0 = okända, förvalet 1). Fönstrets kamera eller en
// uttrycklig (ett headsets öga) — ritytan väljer.
std::vector<HitResult> castRayInScene(const SceneData& scene, const Ray3& ray,
                                      const HitOptions& options,
                                      const gfx::View3D* view, double pxPerPx,
                                      double pxPerMm);

// HÖJDENS STRÅLE (`Canvas::heightAt`): lodrätt NEDÅT genom (x, y), med
// början en bit ovanför lådan, så den första ytan den möter är den översta.
Ray3 verticalRayDown(double x, double y, const Box3& box);

} // namespace glaze::detail
