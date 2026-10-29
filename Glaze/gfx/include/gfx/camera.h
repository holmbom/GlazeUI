#pragma once
// Kameran för ritytans scen — ren double-matte, ingen
// backend och inget fönster. Panorering, zoom, vridning och tilt är BARA
// den här vyn: innehållet ligger kvar i GPU-buffertarna och ritas om med en
// ny matris, aldrig med ny geometri.
//
// RYMDERNA, i den ordning en punkt passerar dem:
//   världen       (wx, wy) i appens enhet — SWEREF-meter, eller widgetens
//                  logiska px när ritytan saknar kamera. Plan z = 0.
//   chunk-lokalt  världen minus chunkens ORIGO (double), lagrad som float i
//                  vertexen. RTC-precisionen bor här: matrisen tar origot i
//                  double, så de stora talen tar ut varandra INNAN något
//                  avrundas till float.
//   pixelklipp    det `viewProjection` lämnar: (X, Y, Z, W) där X/W och Y/W
//                  är MÅLETS FYSISKA PX (origo uppe till vänster, samma rymd
//                  som `box`, klippstacken och UI-pipelinen), Z/W djupet i
//                  [0, 1] och W > 0 framför kameran. Backenden gör sista
//                  steget till sin egen NDC i vertexshadern — den vet målets
//                  storlek, kameran behöver aldrig veta den.
//
// TVÅ KAMEROR i samma rymder: KARTKAMERAN `MapView` (planet z = 0, bäring och
// tilt — 2D-vägen) och 3D-VYN `View3D` (ett öga, en blick
// och en upp-vektor i den högerhänta världen). `toView3D` gör den första till
// den andra, så kartan ritas i 3D-målet med exakt samma bild.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "gfx/geometry.h"
#include "gfx/mat4.h"

namespace gfx {

// MapLibres synfält: 2·atan(1/3) ≈ 36,87°. tan(fov/2) blir exakt 1/3, så
// kameran står 1,5 × ytans höjd från mitten — och vid pitch 0 ger
// perspektivet samma skala som den ortografiska vyn (1 världsenhet =
// pxPerUnit px i mitten).
inline constexpr double kMapFovRad = 0.6435011087932844;

// Tiltens TAK. Över ca 71,6° (90° − fov/2) når ytans överkant
// horisonten och resten av bilden är himmel; 70° är taket med marginal.
// Widgetens förval (60°) är dess eget — det här är vad kameran kan.
inline constexpr double kMaxPitchDeg = 70.0;

// 3D-TYPERNA — double i CPU:n, som Mat4d. Världen är
// högerhänt; kartvärldens z är UPP. 3D-vyn och dess funktioner står längst
// ner i filen.
struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Ray3d {
    Vec3d origin{};
    Vec3d dir{};
};

struct Plane3d {
    Vec3d point{};
    Vec3d normal{0.0, 0.0, 1.0};
};

// En vy över planet z = 0. Alla mått i FYSISKA px — glaze-lagret räknar om
// sina logiska tal (zoom × uiScale) innan de hamnar här.
struct MapView {
    double cx = 0.0;          // världspunkten som hamnar mitt i `box`
    double cy = 0.0;
    double pxPerUnit = 1.0;   // zoomen: fysiska px per världsenhet i mitten
    // Bäringen i GRADER. Innehållet vrids MOTURS på skärmen, MapLibres
    // konvention: bäring 90° med yUp lägger öster uppåt.
    double bearingDeg = 0.0;
    // Tilten i GRADER, 0 = rakt uppifrån (ortografiskt), > 0 = perspektiv
    // där överkanten lutar bort. Klampas till [0, kMaxPitchDeg].
    double pitchDeg = 0.0;
    // Världens y uppåt (karta) eller nedåt (widgetens lokala px, samma
    // riktning som Draw).
    bool yUp = false;
    // Ytan vyn ritar i, i målets fysiska px — ritytans `c.box`.
    Rectf box{};
};

// Matrisen chunk-lokalt → pixelklipp för en chunk med origot (originX,
// originY) — RTC: subtraktionen origo − kamerans mitt görs i double
// här, och bara det lilla resultatet går vidare till float. En vy med tom
// yta eller zoom ≤ 0 ger NOLLMATRISEN (W = 0 — ingenting ritas).
//
// Pitch 0 är ortografisk, pitch > 0 perspektiv med `kMapFovRad`, närplanet
// på höjd/50 (MapLibres) och det bortre planet i oändligheten: 2D:n har
// inget djuptest, och ett ändligt bortre plan hade bara kunnat klippa bort
// innehåll nära horisonten.
Mat4d viewProjection(const MapView& v, double originX, double originY);

// Världspunkt → målets fysiska px, i double. false bakom kameran.
bool project(const MapView& v, double wx, double wy, double& px, double& py);

// Målets fysiska px → världspunkt: strålen genom pixeln mot planet z = 0.
// false när strålen aldrig når planet framför kameran — en pixel OVANFÖR
// HORISONTEN under tilt.
bool unproject(const MapView& v, double px, double py, double& wx,
               double& wy);

// Skalan VID en pixel: fysiska px per världsenhet längs skärmens vågräta
// axel där. Vid pitch 0 är den `pxPerUnit` överallt; under tilt krymper den
// mot horisonten (och är isotrop längs en bildrad). 0 ovanför horisonten.
double pxPerUnitAt(const MapView& v, double px, double py);

// Flyttar kamerans mitt så att världspunkten (wx, wy) hamnar under pixeln
// (px, py) — panoreringens och zoom-mot-pekarens gemensamma steg, exakt
// även under tilt (planet förskjuts, inget annat ändras). false och vyn
// orörd när pixeln ligger ovanför horisonten.
bool anchorWorldAt(MapView& v, double wx, double wy, double px, double py);

// ------------------------------------------------------------------ 3D-VYN
// Backend-neutral double-matte i
// src/camera3d.cpp. Samma rymder som kartkamerans: `viewProjection` lämnar
// PIXELKLIPP med djupet i [0, 1] (mat4.h:s konvention), och backenden gör
// NDC:n — och GL:s z-omräkning 2z − w — i vertexshadern precis som för
// MapView. Inget här vet något om backenden eller fönstret.

enum class Projection3D : std::uint8_t { Perspective, Orthographic };

// En vy i världen. Riktningarna behöver varken vara enhetliga eller
// vinkelräta: blicken normeras, och upp-vektorn räknas om till den räta
// uppåtriktningen i blickens plan (höger = blicken × upp). ANVÄNDBAR är en vy
// med en yta med area, ändliga tal, en blick och en upp-vektor som inte är
// noll och inte parallella, och klipplan där perspektivet har 0 < near < far
// (synfältet i (0, π)) och det ortografiska near < far (halva höjden > 0) —
// det ortografiska närplanet FÅR ligga bakom ögat. En oanvändbar vy ger
// nollmatrisen och falska svar.
struct View3D {
    Vec3d eye{};                       // ögat i världen (double — SWEREF ryms)
    Vec3d forward{0.0, 1.0, 0.0};      // blicken
    Vec3d up{0.0, 0.0, 1.0};           // uppåt
    Projection3D projection = Projection3D::Perspective;
    double fovY            = kMapFovRad;   // VERTIKALT synfält i RADIANER
    double orthoHalfHeight = 1.0;          // halva höjden i världsenheter
    // Klipplanen som avstånd längs blicken från ögat. Ändliga båda: 3D:n har
    // djuptest, och D32F med standard-Z vill ha far/near ≤ 10⁴.
    double nearZ = 0.1;
    double farZ  = 1000.0;
    Rectf  box{};   // ytan i målets fysiska px
};

// Matrisen chunk-lokalt → pixelklipp för en chunk med origot (ox, oy, oz) —
// RTC som kartkamerans: subtraktionen origo − öga görs i double här, och
// bara det lilla resultatet går vidare till float. Oanvändbar vy ⇒
// NOLLMATRISEN.
Mat4d viewProjection(const View3D& v, double ox, double oy, double oz);

// Världspunkt → målets fysiska px, i double. false bakom kameran (W ≤ 0 —
// ortografiskt aldrig) och i en oanvändbar vy. `depth` (FÅR vara null) får
// Z/W: 0 på närplanet och 1 på det bortre — utanför [0, 1] klipper GPU:n
// bort punkten, men `project` svarar ändå.
bool project(const View3D& v, Vec3d w, double& px, double& py,
             double* depth = nullptr);

// Strålen genom pixeln (målets fysiska px), riktningen ENHETLIG: perspektiv
// från ögat, ortografiskt från närplanet under pixeln längs blicken (så att
// allt som syns ligger framför strålens början). false i en oanvändbar vy.
bool rayThrough(const View3D& v, double px, double py, Ray3d& out);

// Skalan VID världspunkten: fysiska px per världsenhet för en sträcka
// vinkelrätt mot blicken där — perspektiv brännvidden genom DJUPET längs
// blicken (inte avståndet till ögat), ortografiskt samma överallt. 0 i
// perspektiv för en punkt i eller bakom ögats plan, och i en oanvändbar vy.
double pxPerUnitAt(const View3D& v, Vec3d w);

// Strålen mot planet (normalen behöver inte vara enhetlig): träffen och, i
// `t` (FÅR vara null), strålens parameter — avståndet när riktningen är
// enhetlig. false när strålen är parallell med planet eller träffen ligger
// bakom strålens början (t < 0).
bool intersect(const Ray3d& r, const Plane3d& p, Vec3d& hit,
               double* t = nullptr);

// Den del av planet vyn SER: snittet mellan planet och synpyramiden från
// närplanet till det bortre planet — eller till djupet `maxDistance` längs
// blicken när det är närmare (≤ 0 = det bortre planet). Ett KONVEXT polygon,
// MOTURS sett från normalens sida; tomt när planet inte skär synpyramiden
// (eller bara snuddar den i en punkt eller längs en kant) och i en
// oanvändbar vy. Ett djup och inte ett avstånd: snittet förblir ett polygon.
std::vector<Vec3d> visiblePlaneRegion(const View3D& v, const Plane3d& p,
                                      double maxDistance);

// KARTKAMERAN SOM 3D-VY. Ögat och riktningarna (synfältet, närplanet, den
// ortografiska halvhöjden) räknas ur KARTMATRISENS INVERS, så punkter i
// planet z = 0 hamnar där `project(MapView)` lägger dem — per konstruktion,
// och grindat inom 1e-6 px. Djupet täcker z-spannet [zMin, zMax] ∪ 0 (de
// höjder scenen har; omkastade gränser tas som spannet, icke-ändliga som
// 0): i PERSPEKTIV ligger det bortre planet där översta bildradens stråle
// når spannets bortersta plan, × 1,01 — MapLibres furthest distance, som
// det är för spannet {0} — och närplanet där kartans ligger; ORTOGRAFISKT
// (pitch 0) står ögat ovanför spannet på kartans perspektivavstånd och
// klipplanen omsluter hela spannet med ett halvt sådant avstånd till godo.
// En oanvändbar MapView ger en oanvändbar vy (tom yta).
View3D toView3D(const MapView& v, double zMin, double zMax);

// GLAZEFRAME — ramverkets block till EGNA program (std140).
// KONTRAKT mot tre shaderspråk: matriserna är `mat4`/`float4x4` och resten
// `vec4`/`float4`, i den här ordningen och på de här platserna (kontrollerat
// nedan). Definierat här bredvid sin fyllare; `program.h` (egna
// program och `MeshDraw::frame`) tar det härifrån.
//
// VEM SOM FYLLER VAD: `frameUniforms` allt som vyn vet; `target` skriver
// BACKENDEN vid ritningen (storleken på målet som faktiskt är bundet — vyn
// vet bara sin ruta i det); `scale` skriver anroparen (ritytan), och
// `frameUniforms` lämnar där (0, 1, 1, 0): pxPerMm okänd, uiScale och
// opaciteten neutrala.
struct FrameUniforms {
    float viewProj[16];      // chunk-lokalt → pixelklipp (förspelet gör NDC)
    float invViewProj[16];   // tillbaka; noll om matrisen saknar invers
    // Ögat chunk-lokalt: w = 1 perspektiv, w = 0 ortografiskt — då punkten
    // på närplanet mitt i ytan.
    float eye[4];
    // STRÅLBASEN, w = 0. Perspektiv: strålen ur ögat har riktningen
    // forward + ndc.x · right + ndc.y · up. Ortografiskt: riktningen är
    // forward och början eye + ndc.x · right + ndc.y · up. ndc ∈ [−1, 1]
    // över ytan, y uppåt. forward är enhetlig; right och up är höger- och
    // uppåtriktningen gånger halva ytans mått — i perspektiv tan(fov/2) ×
    // bildförhållandet respektive tan(fov/2), ortografiskt halva bredden
    // respektive halva höjden i världsenheter.
    float forward[4];
    float right[4];
    float up[4];
    float target[4];      // målets fysiska px (w, h, 1/w, 1/h) — backendens
    float scale[4];       // (pxPerMm, uiScale, opacitet, 0) — ritytans
    // (k, c, 0, 0): världsenheter per fysisk px på djupet d längs blicken är
    // k · d + c (perspektiv c = 0, ortografiskt k = 0).
    float pixelSize[4];
};
static_assert(offsetof(FrameUniforms, viewProj) == 0);
static_assert(offsetof(FrameUniforms, invViewProj) == 64);
static_assert(offsetof(FrameUniforms, eye) == 128);
static_assert(offsetof(FrameUniforms, forward) == 144);
static_assert(offsetof(FrameUniforms, right) == 160);
static_assert(offsetof(FrameUniforms, up) == 176);
static_assert(offsetof(FrameUniforms, target) == 192);
static_assert(offsetof(FrameUniforms, scale) == 208);
static_assert(offsetof(FrameUniforms, pixelSize) == 224);
static_assert(sizeof(FrameUniforms) == 240);

// Blocket för vyn och en chunk med origot (ox, oy, oz). En oanvändbar vy
// ger nollmatriser (ingenting ritas) — `scale` står ändå på sina förval.
FrameUniforms frameUniforms(const View3D& v, double ox, double oy, double oz);

} // namespace gfx
