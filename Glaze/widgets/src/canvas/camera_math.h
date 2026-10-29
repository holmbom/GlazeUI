#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <gfx/camera.h>
#include <gfx/geometry.h>
#include <gfx/mat4.h>

#include "glaze/canvas/camera.h"
#include "glaze/canvas/geometry.h"
#include "glaze/input.h"

// INTERN — ritytans KAMERARÄKNING: glazes kamera
// (logiska px per världsenhet, grader) till fasadens `MapView` (fysiska px),
// och de räkningar `Canvas` gör på den — vridningens normalform, glidningens
// kurva och rektangeln som ska rymmas. Också GESTERNAS tal
// (MapLibres konventioner): hjulets och tangenternas zoomsteg, Ctrl+
// dragets grader per px, vridsnäppet och trögheten — och
// styrplattans precisa panorering. Också 3D-KAMERAN
// (`Camera3D` → fasadens `View3D`, orbit, panorering, dolly, glidning och
// inramning) och `Projection` ur båda vyerna. Bor här och inte i
// widget-filen, som håller sig fri från tal (hård regel 3); de två gester
// som är MÅTT i logiska px — träffens tolerans och piltangentens steg — är
// stilens (`canvasHitTolerance`, `canvasPanStep`).
//
// NAMNET `Projection` är två saker: här i `glaze::detail` KLASSEN nedan (en
// vys projektion på CPU:n), i `glaze` ENUMET på `Camera3D` (perspektiv eller
// ortografiskt). Inne i `detail` skuggar klassen enumet — skriv
// `glaze::Projection::Perspective` där.

namespace glaze::detail {

// Kameran som MapView i rutan `box` (fysiska px) vid skalan `scale`; tilten
// klampas till [0, maxPitch].
gfx::MapView mapView(const Camera& cam, bool yUp, float maxPitch,
                     const gfx::Rectf& box, float scale);

// Ritytans LOKALA rymd som MapView: logiska px från rutans övre vänstra
// hörn, y nedåt, ingen vridning — målarens `screenSpace()`.
gfx::MapView localView(const gfx::Rectf& box, float scale);

// Bäringen i (−180, 180]; ett icke-ändligt värde blir 0.
float wrapBearing(float deg);

// Glidningen vid tiden t ∈ [0, 1]: mjuk start och landning, mitten och
// tilten linjärt, zoomen geometriskt (en halvering tar lika lång tid åt båda
// hållen) och bäringen den korta vägen runt.
Camera easeCamera(const Camera& from, const Camera& to, double t);
// Vridsnäppets glidning: bara bäringen, kortaste vägen, samma kurva.
float easeBearing(float from, float to, double t);

// Zoomen som rymmer en rektangel w × h (världsenheter) i ytan aw × ah
// (logiska px) vid bäringen — den vridna rektangelns utsträckning. 0 när
// rektangeln saknar utsträckning.
double fitZoom(double w, double h, double bearingDeg, double aw, double ah);

// EN VYS PROJEKTION PÅ CPU:N, i double — etiketternas skärmlådor
// och träffprovet räknar med den. En MATRIS, en RUTA och ett ORIGO:
// matrisen tas EN gång med origot i kamerans mitt (kartkameran) respektive
// ögat (3D-vyn), så en SWEREF-punkt blir ett litet tal innan något
// multipliceras (här i double hela vägen). Samma matris och
// samma gallring som programmen: en punkt med klippdjup < 0 (bakom
// närplanet) eller w ≤ 0 har ingen skärmpunkt. Formerna utan z räknar i
// planet z = 0 — kartkamerans 2D-väg, bit för bit som före 3D-vyn.
class Projection {
public:
    explicit Projection(const gfx::MapView& v);
    explicit Projection(const gfx::View3D& v);

    // Kartkameran projektionen byggdes ur — nullptr för en 3D-vy. 2D-vägens
    // frågor mot planet z = 0 (`gfx::unproject`, skalan längs bildraden)
    // ställs till den.
    const gfx::MapView* map() const { return map_ ? &v_ : nullptr; }
    // Ytan i målets fysiska px.
    const gfx::Rectf& box() const { return box_; }
    // Klipprymden (X, Y, Z, W) för en världspunkt; X/W, Y/W är målets px.
    void clip(double wx, double wy, double out[4]) const
    {
        clip(wx, wy, 0.0, out);
    }
    void clip(double wx, double wy, double wz, double out[4]) const;
    // Världspunkt → målets fysiska px; false bakom närplanet.
    bool toScreen(double wx, double wy, double& sx, double& sy) const
    {
        return toScreen(wx, wy, 0.0, sx, sy);
    }
    bool toScreen(double wx, double wy, double wz, double& sx,
                  double& sy) const;
    // Världsriktningen så som kameran projicerar den VID punkten, som
    // skärmens enhetsvektor (y nedåt) — perspektivdivisionens derivata,
    // Glyph-programmets Map-rotation. false bakom närplanet eller när
    // riktningen inte syns.
    bool direction(double wx, double wy, double dx, double dy, double& ex,
                   double& ey) const
    {
        return direction(wx, wy, 0.0, dx, dy, 0.0, ex, ey);
    }
    bool direction(double wx, double wy, double wz, double dx, double dy,
                   double dz, double& ex, double& ey) const;

    // MARKEN — planet z = 0 — under en pixel (målets fysiska px), för båda
    // vyerna (träffprovet och ritytans världspunkter
    // frågar här i stället för hos `map()`): punkten där pixelns stråle når
    // planet framför kameran. false ovanför horisonten.
    bool ground(double px, double py, double& wx, double& wy) const;
    // Skalan vid markpunkten under pixeln, fysiska px per världsenhet:
    // kartkamerans längs bildraden (`gfx::pxPerUnitAt(MapView)`), 3D-vyns
    // vinkelrätt mot blicken vid punkten (`gfx::pxPerUnitAt(View3D)`). 0
    // ovanför horisonten.
    double groundScale(double px, double py) const;

private:
    gfx::MapView v_{};
    gfx::View3D  v3_{};   // 3D-vyn (när `map_` är falsk)
    gfx::Mat4d   m_;
    gfx::Rectf   box_{};
    double       ox_ = 0.0, oy_ = 0.0, oz_ = 0.0;
    bool         map_ = false;
};

// ------------------------------------------------------------ 3D-KAMERAN
// Vinklar i grader, längder i
// världsenheter, rutor och pekare i målets FYSISKA px. Konventionen står vid
// `Camera3D` (glaze/canvas/camera.h).

// En kamera som går att räkna på: ändliga tal, avstånd > 0, synfältet i
// (0, 180), orthoHeight och klipplanen ≥ 0 och — när båda klipplanen är
// satta — far > near. Sättarens fråga; räkningarna nedan förutsätter den.
bool validCamera3D(const Camera3D& cam);

// Kamerans ortonormerade bas i världen: blicken, höger (= blicken × upp) och
// upp — upp längs elevationens TANGENT och vriden med rollen.
struct CameraBasis {
    Vec3 forward, right, up;
};
CameraBasis cameraBasis(const Camera3D& cam);

// Kameran som fasadens 3D-vy i rutan `box`. Klipplan som är 0 räknas ur
// scenens låda och målet: i PERSPEKTIV är far 1,05 × det största djupet
// längs blicken av lådans hörn och målet, och near far / 10⁴ men högst
// halva avståndet; ORTOGRAFISKT omsluter klipplanen lådans och målets
// djup med 5 % av spannet (minst av avståndet) till godo, och närplanet FÅR
// ligga bakom ögat — en inzoomning flyttar ögat in mot målet och ska aldrig
// klippa bort det som syns. Utan låda (tom scen)
// räknas bara målet. Krockar ett SATT plan med ett härlett flyttas det
// härledda: det bortre till dubbla närplanets djup, närplanet ett avstånd
// framför det bortre.
gfx::View3D view3D(const Camera3D& cam, const gfx::Rectf& box,
                   const std::optional<Box3>& scene);

// 3D-KAMERANS KARTSKUGGA: en MapView med samma ruta,
// mitten i målets xy och skalan = 3D-vyns vid målet (`pxPerUnitAt`, oberoende
// av klipplanen) — det ritytan mäter "i mitten": text och markörer i
// världsenheter, strecken i px och mm och lagrens zoomintervall. INGEN
// projektion: 3D-målet ritar genom 3D-vyn.
gfx::MapView mapViewAt(const Camera3D& cam, const gfx::Rectf& box, bool yUp);

// ORBITEN: azimuten + dAz (normalform (−180, 180]) och elevationen + dEl —
// utan tumling klampad till [minEl, maxEl], med tumling fri och i
// normalformen (−180, 180], så kameran går över polen. En elevation som
// redan står UTANFÖR spannet (en kamera koden satt) dras aldrig in med ett
// hopp: spannet vidgas till att rymma den, så orbiten får föra den mot
// spannet men inte längre ut (gesternas regel: ikapp, aldrig ett
// hopp).
Camera3D orbitCamera3D(const Camera3D& cam, float dAzimuth, float dElevation,
                       bool tumble, float minElevation, float maxElevation);
// Elevationens förvalda spärr, ±85°.
float defaultElevationMin();
float defaultElevationMax();
// Ett giltigt spannvärde för elevationen: ändligt och inom ±180 (normalformen).
bool validElevationLimit(float deg);

// PANORERINGENS LODRÄTA DEL. Sidled är alltid
// målplanets — punkten under pekaren följer i sidled. Fram/bak:
//   Plane  i målplanet, längs kamerans upp-vektor (verktyget `panBy`)
//   Fly    längs BLICKEN — draget är ett grepp, så nedåt drar världen emot
//          en: kameran flyger FRAMÅT (gesterna utan Shift, pilarna)
//   Lift   längs världens upp-axel (`Camera3D::up`) — nedåt höjer kameran
//          (gesterna med Shift)
// Beloppet för Fly och Lift är målplanets lodräta belopp — världsenheterna
// draget motsvarar vid målet — så ett drag är lika "snabbt" i alla tre, och
// flera små steg blir ett stort (avståndet och vinklarna står still).
enum class PanVertical : std::uint8_t { Plane, Fly, Lift };

// PANORERINGEN I TAKT MED HANDEN: punkten i MÅLPLANET (genom målet,
// vinkelrätt mot blicken) under (fromX, fromY) hamnar i sidled under (toX,
// toY), och fram/bak går som `vertical` säger — målet och ögat flyttas lika
// mycket. Med `Plane` hamnar punkten exakt under (toX, toY). false och
// kameran orörd när en stråle inte når planet.
bool panCamera3D(Camera3D& cam, const gfx::Rectf& box, double fromX,
                 double fromY, double toX, double toY, PanVertical vertical);

// DOLLY: avståndet × `factor`, klampat till [minDistance, maxDistance] (≤ 0
// = ingen gräns åt det hållet; aldrig noll) — och som orbiten aldrig med ett
// hopp: ett avstånd utanför spannet får gå mot det, inte längre ut. En satt
// orthoHeight följer med i samma förhållande, så dollyn zoomar också
// ortografiskt.
Camera3D dollyCamera3D(const Camera3D& cam, double factor, double minDistance,
                       double maxDistance);

// GLIDNINGEN vid t ∈ [0, 1], kartglidningens kurva (`easeCamera`): målet
// linjärt, avståndet geometriskt, azimuten och rollen den korta vägen runt,
// elevationen och synfältet linjärt, och orthoHeight geometriskt — matchad
// i båda ändar förblir den matchad. Projektionen, upp-axeln och klipplanen
// byter vid slutet (t ≥ 1).
Camera3D easeCamera3D(const Camera3D& from, const Camera3D& to, double t);

// INRAMNINGEN: lådans mitt blir målet och avståndet det
// minsta där lådans åtta hörn ryms i rutan med `padPx` fysiska px luft —
// rotationen orörd. I perspektiv hålls dessutom varje hörn minst halva
// avståndet framför ögat. Ortografiskt bestämmer halvhöjden: matchad sätts
// avståndet ur den, satt sätts den själv (ögats läge klipper ingenting där).
// En låda utan utsträckning i bilden flyttar bara målet; en omvänd låda
// (min > max) lämnar kameran orörd.
Camera3D fitBox(const Camera3D& cam, const Box3& b, const gfx::Rectf& box,
                double padPx);

// KAMERAN VRIDEN SOM EN VY (del-vyns `followRotation`
// när huvudvyn är kartkameran): `cam` med azimut, elevation och roll — och
// upp-axeln Z — satta så att dess bas är vyns: blicken `forward` och uppåt
// `up` (behöver varken vara enhetliga eller vinkelräta, som i
// `gfx::View3D`). Målet, avståndet, projektionen och synfältet står kvar.
// Elevationen hamnar i [−90, 90]; rakt upp- eller nedåt bär azimuten
// vridningen och rollen är 0. En blick eller upp-vektor som är noll eller
// parallella lämnar kameran orörd.
Camera3D orientedAs(const Camera3D& cam, const gfx::Vec3d& forward,
                    const gfx::Vec3d& up);

// ------------------------------------------------------------ GESTERNA
// Vinklar i grader, längder i LOGISKA px där inget annat
// sägs — anroparen räknar om sina fysiska px med skalan.

// Hjulets zoomfaktor för `rows` rader (positivt = hjulet upp = in). En
// pekplatta ger bråkdelar, och faktorn är kontinuerlig i dem.
double wheelZoomFactor(float rows);
// En tangents zoomfaktor (+ in, − ut): en hel zoomnivå.
double keyZoomFactor(bool in);
// Ctrl+drag: bäringens ändring för en vågrät förflyttning och tiltens för en
// lodrät (uppåt = mer tilt), båda i logiska px.
float dragBearingDelta(float dxLogical);
float dragPitchDelta(float dyLogical);
// Shift+pilar: ett steg i bäring respektive tilt.
float keyBearingStep();
float keyPitchStep();

// ORBITENS GESTER — kartans tal, så en hand känner igen
// sig: dragets azimut är Ctrl+dragets bäring (0,8° per logisk px åt höger)
// och elevationen dess tilt med omvänt tecken (0,5° per px NEDÅT — tilten
// ökar uppåt, och mer tilt är lägre elevation). Hjulets och tangenternas
// DOLLY är zoomens steg som avstånd: en rad är en halv zoomnivå, + och − en
// hel, så skalan vid målet ändras precis som kartans zoom.
float  orbitAzimuthDelta(float dxLogical);
float  orbitElevationDelta(float dyLogical);
double wheelDollyFactor(float rows);
double keyDollyFactor(bool in);
// ZOOMNIVÅERNA som faktor: kartans zoom × 2^nivåer, och
// dollyns avstånd × 1/2^nivåer — `wheelDollyFactor`s identitet, EN sanning.
double zoomLevelsFactor(double levels);
double dollyLevelsFactor(double levels);

// ------------------------------------------------- BINDNINGARNAS MÄNGD
// — HUR MYCKET en bunden källa gör, i handlingens enhet:
// Pan/Fly/Lift i LOGISKA px (kartan och orbiten) eller VÄRLDSENHETER
// (förstapersonen), Zoom i zoomnivåer, Rotate och Tilt i grader. Den
// gemensamma valutan är ett HJULSTEG: pekarens och styrplattans px blir steg
// genom `wheelStepPx` (`Widget::wheelNotches`-regeln), nypningens ln genom
// ln(`wheelZoomFactor(1)`) och vridningens grader genom `keyBearingStep`. Där
// handlingen har ett EGET mått per px eller per tangent används det exakt —
// dragets 0,8° och 0,5° per px, pilens 15° och 10°, tangentens hela zoomnivå,
// nypningens ln/ln 2 — så förvalen ger samma kamera som gesterna gav innan
// de blev en tabell. `raw` är källans värde i dess enhet (input.h);
// `panStepPx` är pilarnas steg (`Style::canvasPanStep`). Farten och tecknet
// är radens (input_map `bindingFactor`) och läggs på av anroparen.
//   Map          kartkameran: Fly och Lift gör ingenting
//   Orbit        3D-kameran kring målet
//   FirstPerson  3D-kameran kring ögat (Zoom är Fly — anroparen byter)
enum class MotionKind : std::uint8_t { Map, Orbit, FirstPerson };
double bindingAmount(InputAction a, InputSource s, double raw, MotionKind k,
                     float wheelStepPx, float panStepPx);

// FÖRSTAPERSONEN — kamerans rörelser kring ÖGAT, där
// orbiten vrider kring målet. `lookAround`: azimuten + `dYaw` och
// elevationen + `dTilt` (spärrad till [minEl, maxEl] utan hopp, orbitens
// regel), och målet räknas om framför ögat på samma avstånd — ögat står
// kvar. `moveEye`: ögat och målet flyttas lika mycket, i världsenheter:
// `forward` längs blickens VÅGRÄTA del (rakt ned- eller uppåt: upp-vektorns),
// `right` längs kamerans höger, `lift` längs världens upp-axel (Z eller Y)
// och `rise` längs kamerans upp — lodrätt i bildplanet.
Camera3D lookAround(const Camera3D& cam, float dYaw, float dTilt, float minEl,
                    float maxEl);
void moveEye(Camera3D& cam, double forward, double right, double lift,
             double rise);

// TICKEN: sekunderna mellan två tider i ms, högst en
// tiondel — ett fönster som legat dolt eller blockerats ger inget hopp.
double tickSeconds(std::uint64_t fromMs, std::uint64_t toMs);

// VRIDSNÄPPET (MapLibres `bearingSnap`, gjort STÄLLBART
// och VALBART): släpps en VRIDANDE gest inom `toleranceDeg` från en vinkel
// i steget `stepDeg` (0°, steget, 2 × steget … räknat MODULO 360, och 360
// är samma vinkel som 0) går kameran dit i en glidning på `bearingSnapMs()`.
// Svaret är målbäringen i (−180, 180] — 268° med steget 90 blir −90 (alltså
// 270), 355° med steget 360 blir 0 — eller tomt: steget 0 (av), utanför
// toleransen, eller bäringen står redan EXAKT på vinkeln (ingenting att
// glida). Ett steg som inte delar 360 jämnt har sin sista vinkel vid
// steget × n < 360; mellan den och 360 snäpper bäringen mot NORR. Värdena
// förutsätts giltiga (se nedan).
std::optional<float> snapBearing(float bearingDeg, float stepDeg,
                                 float toleranceDeg);
int  bearingSnapMs();
// Förvalen: steget 360 — BARA NORR, ritytans beteende före snäppet blev
// ställbart — och toleransen 7°.
float defaultRotationSnap();
float defaultRotationSnapTolerance();
// GILTIGA VÄRDEN: steget 0–360 (0 = av) och toleransen 0–180 (mer än ett
// halvt varv betyder ingenting); icke-ändligt är ogiltigt.
bool validRotationSnap(float stepDeg);
bool validRotationSnapTolerance(float toleranceDeg);

// ZOOM ELLER VRIDNING — aldrig båda i samma gest: bara rotering + pan, zoom
// + pan eller bara pan. Gestens ackumulerade zoom (ln av den totala
// faktorn) och vridning (grader) jämförs ENHETSLÖST: ett finger på avståndet
// r från mitten rör sig r · |ln s| av zoomen och r · |vinkeln i radianer| av
// vridningen, så r tar ut sig — samma mått som Windows-klassaren räknar ur
// fingrarna. Har bara EN sort kommit avgörs det tidigt (≈ 1°/2 %); har båda
// kommit vid ≈ 4,6°/8 %, och vridningen måste vara DUBBELT så stor som
// zoomen för att vinna (en nypning vrider alltid lite) — oavgjort blir zoom.
// Tomt = inte bestämt än.
enum class GestureKind : std::uint8_t { Zoom, Rotate };
std::optional<GestureKind> decideGesture(double logScale, double rotationDeg);
// Den som vinner när beslutet faller (vridningen med dubbel vikt).
GestureKind dominantGesture(double logScale, double rotationDeg);
// IKAPP UTAN HOPP: det som hölls före beslutet är en SKULD
// — varje följande steg levereras med en lika stor del av skulden till, så
// kartan rör sig dubbelt så fort som fingrarna tills den hunnit ikapp, och
// står still när de står still. Returnerar steget att utföra och minskar
// skulden. Samma regel som Windows-backendens `gfx::touchpad::catchUp`.
double catchUp(double step, double& debt);

// STYRPLATTANS TVÅFINGERSDRAG: innehållets förflyttning i FYSISKA
// px för ett precist hjul — fasadens kontrakt (gfx/events.h): innehållet
// flyttar sig (−wheelX, +wheelY) × `gfx::kPrecisePxPerRow` LOGISKA px, samma
// tecken som en rullad lista, gånger fönstrets skala.
void precisePanPx(float wheelX, float wheelY, float scale, double& dx,
                  double& dy);
// Samma förflyttning i LOGISKA px — bindningarnas källor `ScrollX`/`ScrollY`.
void preciseScroll(float wheelX, float wheelY, double& dx, double& dy);

// Etiketternas in- och uttoning (`Canvas::setLabelFade`), millisekunder.
int labelFadeMs();

// TRÖGHETEN — en panorerings fart vid släppet, avklingande exponentiellt.
// Rörelsen samlas som SAMPEL (tid i ms, pekaren i fysiska px) medan den
// pågår; vid släppet ger `inertiaStart` farten och längden, eller false när
// rörelsen var för långsam, för kort eller hade stannat innan släppet.
// Förflyttningen vid tiden t är `inertiaOffset` — den når hela sträckan
// exakt vid slutet, så trögheten STANNAR på en bestämd punkt (regel 1).
struct PanSample {
    std::uint64_t ms = 0;
    float         x = 0.0f, y = 0.0f;
};
struct InertiaPlan {
    double vx = 0.0, vy = 0.0;   // farten vid släppet, fysiska px per ms
    double durationMs = 0.0;     // tills rörelsen är under en halv px
};
// Lägger till ett sampel och släpper de som ligger längre bak än fönstret
// farten mäts över — bufferten är TIDSBASERAD, så en mus som rapporterar
// tusen gånger i sekunden ger samma fönster som en som rapporterar sextio.
void addPanSample(std::vector<PanSample>& samples, const PanSample& s);
bool inertiaStart(std::span<const PanSample> samples, std::uint64_t releaseMs,
                  float scale, InertiaPlan& out);
// Förflyttningen i fysiska px vid `tMs` efter släppet (hela sträckan när t
// är förbi slutet).
void inertiaOffset(const InertiaPlan& p, double tMs, double& dx, double& dy);

} // namespace glaze::detail
