// scene_math_gate — RITYTANS KAMERA, headless.
//
// VAD GRINDEN PÅSTÅR — KARTKAMERAN (2D):
//   1. Mat4d räknar rätt: identitet, produkt, invers (M · M⁻¹ = I) och
//      projektionernas djup (närplanet 0, bortre planet 1, det oändliga
//      planet 1 − n/w).
//   2. `project` lägger punkter där ett HANDRÄKNAT förväntat värde säger — vid pitch
//      0 som ren skalning och vridning (y uppåt och nedåt, bäring 30° och
//      90° med cos/sin skrivna som tal), vid pitch 45° enligt en
//      HÅLKAMERA härledd för sig: kameran på avståndet D = 1,5 × ytans höjd
//      från mitten, planet fällt 45°, skärmläget = läget i kameran · D /
//      djupet. Ingen förväntan läses ur camera.cpp:s matriskedja.
//   3. project ↔ unproject tur och retur vid SWEREF med bäring och tilt,
//      åt båda hållen (världspunkter och pixlar).
//   4. Pitch 0 (ortografiskt) och pitch → 0 (perspektiv) ger samma bild.
//   5. HORISONTEN: en pixel ovanför den har ingen världspunkt (false), en
//      strax under har det; skalan där är 0 respektive mindre än zoomen.
//   6. ZOOM MOT PUNKT och PANORERING (`anchorWorldAt`): världspunkten
//      under pekaren står kvar under pekaren efter zoom ×3 — också under
//      vridning och tilt.
//   7. `pxPerUnitAt`: lika med zoomen överallt vid pitch 0; under tilt lika
//      med zoomen på mittraden, mindre ovanför och större nedanför.
//   8. RTC-PRECISIONEN: en vertex lagrad som float(punkt − chunkens origo)
//      och projicerad med den FLOAT-avrundade matrisen — exakt GPU:ns väg,
//      emulerad i float — hamnar inom 0,05 px från double-referensen vid
//      SWEREF och 100 resp. 1000 px/m. KONTROLLEN bredvid: samma punkt
//      lagrad mot origo 0 (ingen RTC) missar med mer än en px, så
//      mätningen ser skillnaden den finns för.
//
//
// VAD GRINDEN PÅSTÅR — 3D-VYN OCH 3D-KAMERAN. FÖRVÄNTAT VÄRDE är
// grindens egen vektorräkning och en egen HÅLKAMERA skriven ur definitionen
// av en perspektivbild (skärmläget = mitten + brännvidden · sidled / djup),
// och orbitens konvention härledd för sig (ögat bakom blicken, upp = ögats
// rörelse när elevationen ökar); ingenting ur camera3d.cpp eller
// camera_math.cpp:s formler.
//   9. Mat4d::rotationY och lookAt mot handräknade värden.
//  10. LIKHETEN: `toView3D(MapView)` projicerar punkter i z = 0 som
//      `project(MapView)` (förväntat värde — redan grindat ovan) inom 1e-6 px, vid
//      bäring 0/30/170°, pitch 0/30/60° och yUp båda, också med ett z-spann
//      som flyttar det bortre planet. Djupet: det bortre planet mot
//      MapLibres furthest distance i sluten form (spannet {0}) och mot en
//      egen härledning under marken; ortografiskt ryms en 30 m stolpe vid
//      100 px/m (kontrollen: kartmatrisens ±d klipper den).
//  11. KONVENTIONEN: vid azimut 0 står ögat SÖDER om målet, vid 90 VÄSTER;
//      `Camera3D` → `View3D` → `project` lägger punkter där hålkamerans
//      det förväntade värdet säger vid flera azimuter, elevationer och synfält; rollen
//      vrider BILDEN moturs; `lookFrom` tar pitch
//      uppåt och ger tillbaka ögat.
//  12. Ortografiskt med MATCHAD höjd = perspektivet för punkter i målplanet.
//  13. Strålen genom en pixel tillbaka till samma pixel (perspektiv,
//      ortografiskt, ur toView3D), enhetlig riktning.
//  14. `pxPerUnitAt` mot en numerisk derivata av `project` VID punkten,
//      också nära ytans hörn och på olika djup.
//  15. TUMLINGEN: elevation 85 → 95° i steg om 1° med tumling — elevationen
//      följer, upp-vektorn vrids exakt 1° per steg, och en
//      referenspunkt rör sig kontinuerligt dit det förväntade värdet säger. Utan tumling
//      klampas den.
//  16. UpAxis::Y: världens +Y uppåt i bilden, öster höger, norr (−z) bort —
//      och samma bild som Z-kameran av den roterade världen.
//  17. visiblePlaneRegion: ortografiskt rakt på planet EXAKT rutan; i
//      perspektiv ligger varje hörn i bild och skärmhörnens träffar i
//      polygonet, också med maxDistance; ett plan bakom ögat syns inte.
//  18. RTC: punkter vid SWEREF + n mm genom GPU:ns float-väg (float(punkt −
//      origo), float-matris) < 0,05 px från double-referensen — kontrollen
//      utan RTC missar med mer än 1 px.
//  19. Kamerans verktyg: orbitens klampning, panoreringen i takt med handen
//      (målplanets punkt under pekaren följer, båda projektionerna — och
//      fram/bak längs BLICKEN respektive världens z med
//      målplanets belopp, ett drag nedåt framåt och uppåt), dollyn
//      och dess gränser, glidningen (azimuten den korta vägen, avståndet
//      geometriskt), inramningen (hörnen ryms med luften och sitter tätt),
//      `intersect`, `frameUniforms` (strålbasen = `rayThrough`, världen per
//      px = 1 / `pxPerUnitAt`) och `Projection` ur en 3D-vy.
//  20. SCENENS LÅDA (cachad per vy): efter `setPositions`
//      följer `sceneBox` meshen — sabotaget meshernas generation utanför
//      nyckeln fäller den.
//  21. FÖRSTAPERSONEN: `lookAround` vrider och tiltar
//      kring ÖGAT — ögat står kvar, blicken pekar dit grindens egen
//      kompassformel säger, elevationen spärras — och `moveEye` flyttar ögat
//      en enhet längs blickens VÅGRÄTA del (också tiltad), åt höger, längs
//      upp-axeln (Z och Y) och längs kamerans upp. Sabotaget `lookAround` som
//      vrider kring målet (orbitens) fäller det.
//  22. ETT ASYMMETRISKT FRUSTUM (`View3D::frustum`) mot en egen hålkamera med
//      tangenter: ytans hörn på tangenterna hamnar på ytans hörn, `project`
//      och djupet 0–1, mittstrålen (för hand), `rayThrough` mot hålkamerans
//      stråle och tur och retur, `pxPerUnitAt`, `visiblePlaneRegion`,
//      GlazeFrame (`rayCenter` + ndc · (right, up) = strålen, forward kvar,
//      float-vägen < 0,05 px) — axelrätt, vridet med förskjutet frustum
//      (noll utanför spannen) och lutat vid SWEREF. Symmetriskt är
//      oförändrat (frustum med ±tan(fov/2) = synfältets matris; utan frustum
//      är rayCenter exakt forward), ortografiskt ignorerar frustumet och
//      left ≥ right, down ≥ up och icke-ändliga tal ger en oanvändbar vy.
//
// INTE MÄTT här (scene3d_pixel_gate mäter det i pixlar genom backenden): att shadern läser
// matrisen i rätt ordning, närplanstrimningen i vertexshadern och klipp-
// rymdens z i GL. 3D-delen mäter inte heller GPU:ns djup och djuptest
// (scene3d_pixel_gate), att förspelet läser GlazeFrame rätt (C9 där),
// ritytans 3D-mål och kamerans gester genom fönstret
// eller Metal.
//
// SABOTAGEN den fäller: origot ignorerat i viewProjection (fall 2 — varje
// referenspunkt tusentals px fel), grader tolkade som radianer (fall 2, bäringen) och yUp
// ignorerat (fall 2, också tiltens förväntade värde). Dessutom:
// z-speglingen fel för UpAxis::Y (fall 16),
// azimutens tecken (fall 11), omatchad orthoHeight (fall 12), ett ändligt
// bortre plan som påverkar x/y (fall 10), tumling via klampning (fall 15)
// och skalan i mitten i stället för vid punkten (fall 14).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include <gfx/camera.h>
#include <gfx/mat4.h>
#include <glaze/canvas/camera.h>
#include <glaze/canvas/mesh.h>
#include <glaze/canvas/shape.h>

#include "canvas/camera_math.h"   // Glaze/widgets/src — 3D-kamerans räkning
#include "canvas/scene.h"         // scenens låda (fall 20)

namespace {

int  g_fel     = 0;
int  g_fall    = 0;
bool g_verbose = false;

void check(bool ok, const std::string& vad, const std::string& fick = {})
{
    ++g_fall;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", vad.c_str());
        return;
    }
    std::printf("  FEL  %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                fick.c_str());
    ++g_fel;
}

std::string par(double a, double b)
{
    char buf[96];
    std::snprintf(buf, sizeof buf, "(%.6f, %.6f)", a, b);
    return buf;
}

bool nära(double a, double b, double tol)
{
    return std::abs(a - b) <= tol;
}

// En vy med ytan {100, 50, 400, 300} — mitten i (300, 200).
gfx::MapView basvy()
{
    gfx::MapView v;
    v.cx        = 1000.0;
    v.cy        = 2000.0;
    v.pxPerUnit = 2.0;
    v.box       = gfx::Rectf{100.0f, 50.0f, 400.0f, 300.0f};
    return v;
}

// Projicerar och kontrollerar mot förväntat värde (fysiska px).
void projCheck(const gfx::MapView& v, double wx, double wy, double fx,
               double fy, double tol, const std::string& vad)
{
    double px = 0.0, py = 0.0;
    const bool ok = gfx::project(v, wx, wy, px, py);
    check(ok && nära(px, fx, tol) && nära(py, fy, tol), vad,
          (ok ? par(px, py) : std::string("false")) + " mot facit " +
              par(fx, fy));
}

// ------------------------------------------------------------------ 1
void fallMatriser()
{
    using gfx::Mat4d;
    const Mat4d id = Mat4d::identity();
    const Mat4d t  = Mat4d::translation(3.0, -4.0, 5.0) *
                    Mat4d::rotationZ(0.3) * Mat4d::scale(2.0, 3.0, 4.0);
    const Mat4d it = id * t;
    bool lika = true;
    for (int i = 0; i < 16; ++i)
        lika = lika && it.m[i] == t.m[i];
    check(lika, "matris: I · M = M");

    // Translationen ligger i KOLUMN 3 (kolumnordning, kolumnvektorer).
    double p[4];
    Mat4d::translation(3.0, -4.0, 5.0).transform(1.0, 1.0, 1.0, 1.0, p);
    check(p[0] == 4.0 && p[1] == -3.0 && p[2] == 6.0 && p[3] == 1.0,
          "matris: translationen flyttar punkten (kolumnordning)");

    // Inversen på en riktig kameramatris (tilt och bäring).
    gfx::MapView v = basvy();
    v.bearingDeg   = 30.0;
    v.pitchDeg     = 45.0;
    const Mat4d vp = gfx::viewProjection(v, v.cx, v.cy);
    Mat4d       inv;
    const bool  ok = vp.inverse(inv);
    const Mat4d e  = vp * inv;
    double maxFel = 0.0;
    for (int i = 0; i < 16; ++i)
        maxFel = std::max(maxFel,
                          std::abs(e.m[i] - ((i % 5) == 0 ? 1.0 : 0.0)));
    check(ok && maxFel < 1e-9, "matris: M · M⁻¹ = I för kamerans matris",
          "största avvikelse " + std::to_string(maxFel));
    Mat4d noll;
    check(!noll.inverse(inv), "matris: nollmatrisen har ingen invers");

    // Perspektivets djup: närplanet 0, bortre planet 1 — ögonrymden tittar
    // längs −z, så avståndet d ligger på z = −d.
    double c[4];
    Mat4d::perspective(1.0, 1.5, 2.0, 100.0).transform(0, 0, -2.0, 1, c);
    check(nära(c[2] / c[3], 0.0, 1e-12), "perspektiv: närplanet har djup 0");
    Mat4d::perspective(1.0, 1.5, 2.0, 100.0).transform(0, 0, -100.0, 1, c);
    check(nära(c[2] / c[3], 1.0, 1e-12), "perspektiv: bortre planet djup 1");
    Mat4d::perspective(1.0, 1.5, 2.0, INFINITY).transform(0, 0, -8.0, 1, c);
    check(nära(c[2] / c[3], 1.0 - 2.0 / 8.0, 1e-12) && c[3] == 8.0,
          "perspektiv: oändligt plan ger djupet 1 − n/w och w = avståndet");
    Mat4d::ortho(-10, 30, -5, 15, -1, 1).transform(30, 15, 0, 1, c);
    check(nära(c[0], 1.0, 1e-12) && nära(c[1], 1.0, 1e-12) &&
              nära(c[2], 0.5, 1e-12),
          "ortho: lådans hörn hamnar i NDC (1, 1) och z = 0 mitt i djupet");

    float f[16];
    t.toFloat(f);
    bool ordning = true;
    for (int i = 0; i < 16; ++i)
        ordning = ordning && f[i] == static_cast<float>(t.m[i]);
    check(ordning, "matris: toFloat behåller kolumnordningen");
}

// ------------------------------------------------------------------ 2
void fallFacit()
{
    // PITCH 0 — ren skalning: (wx − cx, wy − cy) · zoom, y uppåt eller ned.
    gfx::MapView v = basvy();
    v.yUp          = true;
    projCheck(v, 1010.0, 2005.0, 320.0, 190.0, 1e-9,
              "pitch 0, yUp: öster höger, norr UPPÅT");
    projCheck(v, 1000.0, 2000.0, 300.0, 200.0, 1e-9,
              "pitch 0: kamerans mitt i ytans mitt");
    v.yUp = false;
    projCheck(v, 1010.0, 2005.0, 320.0, 210.0, 1e-9,
              "pitch 0, y nedåt: +y NEDÅT på skärmen");

    // BÄRINGEN vrider innehållet MOTURS: bäring 90° med yUp lägger öster
    // uppåt (MapLibre).
    v            = basvy();
    v.yUp        = true;
    v.bearingDeg = 90.0;
    projCheck(v, 1010.0, 2000.0, 300.0, 180.0, 1e-9,
              "bäring 90°: öster pekar UPPÅT");
    // Bäring 30°: öster 20 px → (20·cos30°, −20·sin30°) = (17.320508, −10).
    v.bearingDeg = 30.0;
    projCheck(v, 1010.0, 2000.0, 317.3205080756888, 190.0, 1e-9,
              "bäring 30°, yUp: öster vrids 30° moturs");
    // Med y nedåt: +y (ned, 20 px) vrids moturs mot höger:
    // (20·sin30°, 20·cos30°) = (10, 17.320508).
    v.yUp = false;
    projCheck(v, 1000.0, 2010.0, 310.0, 217.32050807568876, 1e-9,
              "bäring 30°, y nedåt: nedåt vrids moturs mot höger");

    // PITCH 45° — HÅLKAMERAN, härledd för sig. Ytans höjd 300 ⇒ D = 450
    // (tan(fov/2) = 1/3). En punkt s px norr om mitten ligger i kameran på
    // höjden −s·cos45° och djupet D + s·sin45°; skärmen visar höjden ·
    // D / djupet. Öster/väster skalas med samma D / djupet.
    v          = basvy();
    v.yUp      = true;
    v.pitchDeg = 45.0;
    const double D  = 450.0;
    const double c4 = std::sqrt(0.5);   // cos 45° = sin 45°
    {
        const double s = 50.0 * 2.0;    // 50 världsenheter norr, zoom 2
        const double fy = 200.0 - s * c4 * D / (D + s * c4);
        projCheck(v, 1000.0, 2050.0, 300.0, fy, 1e-6,
                  "pitch 45°: norr hamnar ovanför och KRYMPER mot djupet");
    }
    {
        const double s  = 40.0 * 2.0;   // söderut — närmare kameran
        const double fy = 200.0 + s * c4 * D / (D - s * c4);
        projCheck(v, 1000.0, 1960.0, 300.0, fy, 1e-6,
                  "pitch 45°: söder hamnar nedanför och VÄXER");
    }
    projCheck(v, 1030.0, 2000.0, 360.0, 200.0, 1e-6,
              "pitch 45°: på mittraden är skalan zoomen (djupet = D)");
    {
        const double sx = 30.0 * 2.0, sy = 50.0 * 2.0;
        const double djup = D + sy * c4;
        projCheck(v, 1030.0, 2050.0, 300.0 + sx * D / djup,
                  200.0 - sy * c4 * D / djup, 1e-6,
                  "pitch 45°: öster OCH norr delar samma djupdivision");
    }
}

// ------------------------------------------------------------------ 3
gfx::MapView swerefvy()
{
    gfx::MapView v;
    v.cx         = 674032.5;
    v.cy         = 6580117.25;
    v.pxPerUnit  = 120.0;
    v.bearingDeg = 30.0;
    v.pitchDeg   = 45.0;
    v.yUp        = true;
    v.box        = gfx::Rectf{0.0f, 0.0f, 800.0f, 600.0f};
    return v;
}

void fallTurOchRetur()
{
    const gfx::MapView v = swerefvy();
    double maxVärld = 0.0;
    bool   allaOk   = true;
    for (int i = -3; i <= 3; ++i)
        for (int j = -3; j <= 3; ++j) {
            const double wx = v.cx + i * 0.9, wy = v.cy + j * 0.7;
            double px, py, ux, uy;
            if (!gfx::project(v, wx, wy, px, py) ||
                !gfx::unproject(v, px, py, ux, uy)) {
                allaOk = false;
                continue;
            }
            maxVärld = std::max(maxVärld, std::hypot(ux - wx, uy - wy));
        }
    check(allaOk && maxVärld < 1e-6,
          "SWEREF, bäring 30°, pitch 45°: värld → px → värld",
          "största fel " + std::to_string(maxVärld) + " m");

    double maxPx = 0.0;
    allaOk       = true;
    for (int x = 50; x <= 750; x += 100)
        for (int y = 150; y <= 590; y += 110) {
            double wx, wy, px, py;
            if (!gfx::unproject(v, x, y, wx, wy) ||
                !gfx::project(v, wx, wy, px, py)) {
                allaOk = false;
                continue;
            }
            maxPx = std::max(maxPx, std::hypot(px - x, py - y));
        }
    check(allaOk && maxPx < 1e-6,
          "SWEREF, bäring 30°, pitch 45°: px → värld → px",
          "största fel " + std::to_string(maxPx) + " px");
}

// ------------------------------------------------------------------ 4
void fallOrthoMotPerspektiv()
{
    gfx::MapView o = swerefvy();
    o.pitchDeg     = 0.0;
    gfx::MapView p = o;
    p.pitchDeg     = 1e-7;
    double maxFel  = 0.0;
    for (int i = -2; i <= 2; ++i)
        for (int j = -2; j <= 2; ++j) {
            const double wx = o.cx + i * 1.3, wy = o.cy + j * 1.1;
            double ox, oy, px, py;
            gfx::project(o, wx, wy, ox, oy);
            gfx::project(p, wx, wy, px, py);
            maxFel = std::max(maxFel, std::hypot(px - ox, py - oy));
        }
    check(maxFel < 1e-3,
          "pitch 0 (ortografiskt) = pitch → 0 (perspektiv)",
          "största skillnad " + std::to_string(maxFel) + " px");
}

// ------------------------------------------------------------------ 5
void fallHorisonten()
{
    // Ytans höjd 300 ⇒ D = 450. Horisonten ligger D/tan(pitch) ovanför
    // mitten: vid 60° är det 259,8 px — ovanför ytan (mitten är y = 200).
    gfx::MapView v = basvy();
    v.yUp          = true;
    v.pitchDeg     = 60.0;
    const double horisont = 200.0 - 450.0 / std::tan(60.0 * M_PI / 180.0);
    double wx, wy;
    check(!gfx::unproject(v, 300.0, horisont - 20.0, wx, wy),
          "horisonten: en pixel 20 px OVANFÖR har ingen världspunkt");
    check(gfx::pxPerUnitAt(v, 300.0, horisont - 20.0) == 0.0,
          "horisonten: skalan ovanför är 0");
    const bool under = gfx::unproject(v, 300.0, horisont + 20.0, wx, wy);
    double px = 0.0, py = 0.0;
    check(under && gfx::project(v, wx, wy, px, py) &&
              nära(py, horisont + 20.0, 1e-6),
          "horisonten: en pixel 20 px UNDER har en världspunkt, och den "
          "projiceras tillbaka dit");
    const double k = gfx::pxPerUnitAt(v, 300.0, horisont + 20.0);
    check(k > 0.0 && k < v.pxPerUnit,
          "horisonten: skalan nära horisonten är mindre än zoomen",
          std::to_string(k));
    // Ovanför taket klampas tilten: 89° beter sig som 70°.
    gfx::MapView brant = v;
    brant.pitchDeg     = 89.0;
    gfx::MapView tak   = v;
    tak.pitchDeg       = gfx::kMaxPitchDeg;
    double bx, by, tx, ty;
    gfx::project(brant, 1000.0, 2040.0, bx, by);
    gfx::project(tak, 1000.0, 2040.0, tx, ty);
    check(bx == tx && by == ty, "tilten klampas till taket (70°)");
}

// ------------------------------------------------------------------ 6
void fallZoomMotPunkt()
{
    gfx::MapView v = swerefvy();
    v.pitchDeg     = 40.0;
    const double px = 520.0, py = 410.0;
    double       wx, wy;
    check(gfx::unproject(v, px, py, wx, wy), "zoom: pekarens världspunkt");
    v.pxPerUnit *= 3.0;
    const bool flyttad = gfx::anchorWorldAt(v, wx, wy, px, py);
    double     nx = 0.0, ny = 0.0;
    gfx::project(v, wx, wy, nx, ny);
    check(flyttad && nära(nx, px, 1e-6) && nära(ny, py, 1e-6),
          "ZOOM MOT PUNKT ×3: världspunkten står kvar under pekaren",
          par(nx, ny) + " mot " + par(px, py));

    // Panorering: punkten under ett tryck följer pekaren.
    const bool drag = gfx::anchorWorldAt(v, wx, wy, 300.0, 500.0);
    gfx::project(v, wx, wy, nx, ny);
    check(drag && nära(nx, 300.0, 1e-6) && nära(ny, 500.0, 1e-6),
          "PANORERING: den greppade världspunkten följer pekaren");

    // Ovanför horisonten finns ingen punkt att hålla fast — vyn orörd.
    gfx::MapView brant = basvy();
    brant.pitchDeg     = 60.0;
    const gfx::MapView före = brant;
    check(!gfx::anchorWorldAt(brant, 1000.0, 2000.0, 300.0, -80.0) &&
              brant.cx == före.cx && brant.cy == före.cy,
          "anchorWorldAt ovanför horisonten: false och vyn orörd");
}

// ------------------------------------------------------------------ 7
void fallSkalan()
{
    gfx::MapView v = swerefvy();
    v.pitchDeg     = 0.0;
    double maxFel  = 0.0;
    for (int x = 100; x <= 700; x += 200)
        for (int y = 100; y <= 500; y += 200)
            maxFel = std::max(
                maxFel, std::abs(gfx::pxPerUnitAt(v, x, y) - v.pxPerUnit));
    check(maxFel < 1e-6, "pitch 0: skalan är zoomen i varje pixel",
          "största avvikelse " + std::to_string(maxFel));

    v.pitchDeg     = 45.0;
    const double k = gfx::pxPerUnitAt(v, 400.0, 300.0);
    check(nära(k, v.pxPerUnit, 1e-4),
          "pitch 45°: på mittraden är skalan zoomen", std::to_string(k));
    check(gfx::pxPerUnitAt(v, 400.0, 100.0) < k &&
              gfx::pxPerUnitAt(v, 400.0, 500.0) > k,
          "pitch 45°: mindre ovanför mitten, större nedanför");
}

// ------------------------------------------------------------------ 8
//
// GPU:ns väg i float: vertexen är float(punkt − origo) och matrisen
// float-avrundad, multiplikationen och divisionen görs i float.
double floatFel(const gfx::MapView& v, double wx, double wy, double ox,
                double oy)
{
    float m[16];
    gfx::viewProjection(v, ox, oy).toFloat(m);
    const float vx = static_cast<float>(wx - ox);
    const float vy = static_cast<float>(wy - oy);
    const float cx = m[0] * vx + m[4] * vy + m[12];
    const float cy = m[1] * vx + m[5] * vy + m[13];
    const float cw = m[3] * vx + m[7] * vy + m[15];
    double rx = 0.0, ry = 0.0;
    gfx::project(v, wx, wy, rx, ry);
    return std::hypot(static_cast<double>(cx / cw) - rx,
                      static_cast<double>(cy / cw) - ry);
}

void fallRtc()
{
    for (double zoom : {100.0, 1000.0}) {
        gfx::MapView v = swerefvy();
        v.cx           = 674000.0;
        v.cy           = 6580000.0;
        v.pxPerUnit    = zoom;
        // Chunkens origo är en av DATANS punkter, inte kamerans mitt.
        const double ox = 674001.125, oy = 6580000.375;
        const double r  = 300.0 / zoom;   // ±300 px kring mitten
        double maxRtc = 0.0, minUtan = 1e30;
        for (int i = -3; i <= 3; ++i)
            for (int j = -3; j <= 3; ++j) {
                const double wx = v.cx + i * r / 3.0 + 0.123;
                const double wy = v.cy + j * r / 3.0 + 0.0456;
                maxRtc  = std::max(maxRtc, floatFel(v, wx, wy, ox, oy));
                minUtan = std::min(minUtan, floatFel(v, wx, wy, 0.0, 0.0));
            }
        const std::string z = std::to_string(static_cast<int>(zoom)) + " px/m";
        check(maxRtc < 0.05, "RTC vid SWEREF, " + z + ": float-vägen < 0,05 px",
              "största fel " + std::to_string(maxRtc) + " px");
        check(minUtan > 1.0,
              "KONTROLL, " + z + ": utan RTC (origo 0) missar float-vägen "
              "med mer än 1 px — mätningen ser skillnaden",
              "minsta fel " + std::to_string(minUtan) + " px");
    }
}

// ------------------------------------------------------------------ övrigt
void fallTomVy()
{
    gfx::MapView v = basvy();
    v.box.w        = 0.0f;
    double px, py;
    check(!gfx::project(v, 1000.0, 2000.0, px, py),
          "en vy utan yta projicerar ingenting");
    const gfx::Mat4d m = gfx::viewProjection(v, 0.0, 0.0);
    bool noll = true;
    for (double e : m.m)
        noll = noll && e == 0.0;
    check(noll, "en vy utan yta ger nollmatrisen (W = 0 — inget ritas)");
    v           = basvy();
    v.pxPerUnit = 0.0;
    check(!gfx::project(v, 1000.0, 2000.0, px, py),
          "zoom 0 projicerar ingenting");
}

// ================================================================== 3D
//
// FÖRVÄNTAT VÄRDE FÖR 3D-DELEN — egen vektorräkning och en egen hålkamera, skrivna ur
// definitionerna. Ingenting här anropar camera3d.cpp eller camera_math.cpp:s
// formler; de mäts MOT det här.

constexpr double kPi = std::numbers::pi;

double rad(double deg)
{
    return deg * kPi / 180.0;
}

struct V3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

V3 operator+(V3 a, V3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

V3 operator-(V3 a, V3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

V3 operator*(V3 a, double s)
{
    return {a.x * s, a.y * s, a.z * s};
}

double dot(V3 a, V3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

V3 cross(V3 a, V3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

double len(V3 a)
{
    return std::sqrt(dot(a, a));
}

gfx::Vec3d g3(V3 a)
{
    return {a.x, a.y, a.z};
}

V3 v3(gfx::Vec3d a)
{
    return {a.x, a.y, a.z};
}

V3 v3(glaze::Point3 a)
{
    return {a.x, a.y, a.z};
}

V3 v3(glaze::Vec3 a)
{
    return {a.x, a.y, a.z};
}

glaze::Point3 p3(V3 a)
{
    return {a.x, a.y, a.z};
}

std::string tre(V3 a)
{
    char buf[128];
    std::snprintf(buf, sizeof buf, "(%.9f, %.9f, %.9f)", a.x, a.y, a.z);
    return buf;
}

// HÅLKAMERAN: skärmläget är ytans mitt + brännvidden · (sidled / djup) med
// y nedåt, brännvidden halva ytans höjd / tan(fov/2); ortografiskt sidled ·
// (halva ytans höjd / halvhöjden).
struct Hal {
    V3         eye, f, r, u;
    double     fovY  = 0.0;
    double     halfH = 0.0;
    bool       ortho = false;
    gfx::Rectf box{};
};

bool hal(const Hal& h, V3 p, double& px, double& py)
{
    const V3     q  = p - h.eye;
    const double d  = dot(q, h.f);
    const double cx = h.box.x + h.box.w / 2.0;
    const double cy = h.box.y + h.box.h / 2.0;
    double       s  = 0.0;
    if (h.ortho) {
        s = (h.box.h / 2.0) / h.halfH;
    } else {
        if (!(d > 0.0))
            return false;
        s = (h.box.h / 2.0) / std::tan(h.fovY / 2.0) / d;
    }
    px = cx + s * dot(q, h.r);
    py = cy - s * dot(q, h.u);
    return true;
}

// ORBITENS FÖRVÄNTADE VÄRDE ur konventionen (glaze/canvas/camera.h): ögat står `dist`
// från målet BAKOM blicken — i kompassriktningen azimut + 180° — och
// `elevation` över horisonten. UPP är riktningen ögat rör sig när
// elevationen ökar (ögats derivata efter elevationen), höger = blicken ×
// upp. Med Y upp är (öster, norr, upp) = (x, −z, y). Rollen ingår inte —
// den mäts som en vridning av BILDEN.
Hal orbitFacit(V3 target, double dist, double azDeg, double elDeg,
               double fovDeg, gfx::Rectf box, bool yUp = false)
{
    const double az     = rad(azDeg);
    const double el     = rad(elDeg);
    const auto   världen = [yUp](double e, double n, double u) {
        return yUp ? V3{e, u, -n} : V3{e, n, u};
    };
    const V3 bakåt = världen(-std::sin(az) * std::cos(el),
                             -std::cos(az) * std::cos(el), std::sin(el));
    const V3 upp   = världen(std::sin(az) * std::sin(el),
                             std::cos(az) * std::sin(el), std::cos(el));
    Hal h;
    h.eye  = target + bakåt * dist;
    h.f    = bakåt * -1.0;
    h.u    = upp;
    h.r    = cross(h.f, h.u);
    h.fovY = rad(fovDeg);
    h.box  = box;
    return h;
}

const gfx::Rectf kRuta3D{40.0f, 30.0f, 800.0f, 600.0f};

// Implementationens skärmläge för en kamera (3D-vyn ur camera_math).
bool proj3(const glaze::Camera3D& cam, V3 p, double& px, double& py,
           const gfx::Rectf& box = kRuta3D)
{
    return gfx::project(glaze::detail::view3D(cam, box, std::nullopt), g3(p),
                        px, py);
}

double hypot2(double ax, double ay, double bx, double by)
{
    return std::hypot(ax - bx, ay - by);
}

// ------------------------------------------------------------------ 9
void fallMatriser3D()
{
    using gfx::Mat4d;
    double p[4];
    Mat4d::rotationY(kPi / 2.0).transform(0.0, 0.0, 1.0, 1.0, p);
    check(nära(p[0], 1.0, 1e-15) && nära(p[1], 0.0, 1e-15) &&
              nära(p[2], 0.0, 1e-15),
          "rotationY: +90° vrider z till x");
    Mat4d::rotationY(kPi / 2.0).transform(1.0, 0.0, 0.0, 1.0, p);
    check(nära(p[0], 0.0, 1e-15) && nära(p[2], -1.0, 1e-15),
          "rotationY: +90° vrider x till −z");

    // Ögat (1, 2, 3) tittar längs −z mot (1, 2, −7) med y uppåt: ögat blir
    // origo, målet (0, 0, −10), +x höger och +y upp.
    const Mat4d la = Mat4d::lookAt(1, 2, 3, 1, 2, -7, 0, 5, 0);
    struct {
        double x, y, z, fx, fy, fz;
        const char* vad;
    } const fall[] = {
        {1, 2, 3, 0, 0, 0, "lookAt: ögat i origo"},
        {1, 2, -7, 0, 0, -10, "lookAt: målet på −z"},
        {2, 2, 3, 1, 0, 0, "lookAt: +x höger"},
        {1, 3, 3, 0, 1, 0, "lookAt: +y upp (upp-vektorn behöver inte vara enhetlig)"},
    };
    for (const auto& f : fall) {
        la.transform(f.x, f.y, f.z, 1.0, p);
        check(nära(p[0], f.fx, 1e-12) && nära(p[1], f.fy, 1e-12) &&
                  nära(p[2], f.fz, 1e-12) && p[3] == 1.0,
              f.vad);
    }
    // En vriden blick: ögat i origo tittar mot (1, 1, 0) med z upp — höger
    // är (1, −1, 0)/√2.
    Mat4d::lookAt(0, 0, 0, 1, 1, 0, 0, 0, 1)
        .transform(1.0, -1.0, 0.0, 1.0, p);
    check(nära(p[0], std::sqrt(2.0), 1e-12) && nära(p[1], 0.0, 1e-12) &&
              nära(p[2], 0.0, 1e-12),
          "lookAt: höger = blicken × upp");
    bool noll1 = true, noll2 = true;
    const Mat4d par = Mat4d::lookAt(0, 0, 0, 0, 0, 5, 0, 0, 1);
    const Mat4d sam = Mat4d::lookAt(1, 1, 1, 1, 1, 1, 0, 0, 1);
    for (int i = 0; i < 16; ++i) {
        noll1 = noll1 && par.m[i] == 0.0;
        noll2 = noll2 && sam.m[i] == 0.0;
    }
    check(noll1 && noll2,
          "lookAt: upp parallell med blicken eller ögat på målet ger "
          "nollmatrisen");
}

// ------------------------------------------------------------------ 10
void fallLikheten()
{
    double maxFel = 0.0;
    int    antal  = 0;
    bool   allaOk = true;
    for (bool yUp : {false, true})
        for (double bäring : {0.0, 30.0, 170.0})
            for (double pitch : {0.0, 30.0, 60.0})
                for (int spann = 0; spann < 2; ++spann) {
                    gfx::MapView mv;
                    mv.cx         = 674032.5;
                    mv.cy         = 6580117.25;
                    mv.pxPerUnit  = 37.5;
                    mv.bearingDeg = bäring;
                    mv.pitchDeg   = pitch;
                    mv.yUp        = yUp;
                    mv.box        = gfx::Rectf{60.0f, 20.0f, 640.0f, 480.0f};
                    // Utan spann, och med ett som flyttar det bortre planet
                    // och djupet — x och y får inte röra sig.
                    const gfx::View3D v = spann == 0
                                              ? gfx::toView3D(mv, 0.0, 0.0)
                                              : gfx::toView3D(mv, -80.0, 450.0);
                    // Punkterna i planet: pixlar i och utanför ytan
                    // tillbaka genom kartans (redan grindade) unproject.
                    for (int i = -2; i <= 12; ++i)
                        for (int j = -2; j <= 12; ++j) {
                            const double x = 60.0 + 64.0 * i;
                            const double y = 20.0 + 48.0 * j;
                            double wx, wy;
                            if (!gfx::unproject(mv, x, y, wx, wy))
                                continue;   // ovanför horisonten
                            double ax, ay, bx, by;
                            const bool a = gfx::project(mv, wx, wy, ax, ay);
                            const bool b =
                                gfx::project(v, gfx::Vec3d{wx, wy, 0.0}, bx, by);
                            if (!a || !b) {
                                allaOk = false;
                                continue;
                            }
                            maxFel = std::max(maxFel, hypot2(ax, ay, bx, by));
                            ++antal;
                        }
                }
    check(allaOk && antal > 1000 && maxFel < 1e-6,
          "LIKHETEN: toView3D(MapView) lägger z = 0 där project(MapView) gör "
          "(bäring 0/30/170°, pitch 0/30/60°, yUp båda, med och utan spann)",
          std::to_string(antal) + " punkter, största fel " +
              std::to_string(maxFel) + " px");

    // DJUPET i perspektiv: spannet {0} ger MapLibres furthest distance i
    // sluten form × 1,01 — och närplanet kartans (höjden/50) — i
    // världsenheter.
    gfx::MapView mv;
    mv.cx         = 1000.0;
    mv.cy         = 2000.0;
    mv.pxPerUnit  = 2.5;
    mv.bearingDeg = 30.0;
    mv.pitchDeg   = 45.0;
    mv.yUp        = true;
    mv.box        = gfx::Rectf{0.0f, 0.0f, 400.0f, 300.0f};
    const double D  = 450.0;   // 1,5 × höjden, px
    const double p  = rad(45.0);
    const double hf = gfx::kMapFovRad / 2.0;
    const double topHalf =
        std::sin(hf) * D / std::sin(kPi - (kPi / 2.0 + p) - hf);
    const double furthest = std::cos(kPi / 2.0 - p) * topHalf + D;
    gfx::View3D  v        = gfx::toView3D(mv, 0.0, 0.0);
    check(nära(v.farZ, 1.01 * furthest / 2.5, 1e-9 * v.farZ) &&
              nära(v.nearZ, 300.0 / 50.0 / 2.5, 1e-9) &&
              v.projection == gfx::Projection3D::Perspective,
          "djupet: bortre planet = MapLibres furthest distance × 1,01, "
          "närplanet kartans",
          "far " + std::to_string(v.farZ) + " mot " +
              std::to_string(1.01 * furthest / 2.5));
    // Under marken: översta radens stråle — vinkeln (90° − pitch) − fov/2
    // under horisonten, från ögat D · cos(pitch) px över planet — når
    // z = −20 på sträckan L, djupet L · cos(fov/2).
    {
        const double Z = 20.0 * 2.5;   // px
        const double L =
            (D * std::cos(p) + Z) / std::sin(kPi / 2.0 - p - hf);
        v = gfx::toView3D(mv, -20.0, 40.0);
        check(nära(v.farZ, 1.01 * L * std::cos(hf) / 2.5, 1e-9 * v.farZ),
              "djupet: spannet under marken flyttar det bortre planet dit "
              "översta radens stråle når dess botten",
              std::to_string(v.farZ) + " mot " +
                  std::to_string(1.01 * L * std::cos(hf) / 2.5));
    }

    // ORTOGRAFISKT: vid 100 px/m och ytans höjd 200 px är kartans ±d bara
    // ±3 m — en 30 m stolpe klipps där men ryms i z-spannets djup.
    gfx::MapView o;
    o.cx        = 674000.0;
    o.cy        = 6580000.0;
    o.pxPerUnit = 100.0;
    o.yUp       = true;
    o.box       = gfx::Rectf{0.0f, 0.0f, 300.0f, 200.0f};
    const gfx::View3D ov = gfx::toView3D(o, 0.0, 30.0);
    double dTopp = -1.0, dFot = -1.0, px, py;
    const bool topp = gfx::project(ov, {o.cx, o.cy, 30.0}, px, py, &dTopp);
    const bool fot  = gfx::project(ov, {o.cx, o.cy, 0.0}, px, py, &dFot);
    double c[4];
    gfx::viewProjection(o, o.cx, o.cy).transform(0.0, 0.0, 30.0, 1.0, c);
    check(ov.projection == gfx::Projection3D::Orthographic && topp && fot &&
              dTopp > 0.0 && dTopp < 1.0 && dFot > 0.0 && dFot < 1.0 &&
              dTopp < dFot && !(c[2] / c[3] >= 0.0 && c[2] / c[3] <= 1.0),
          "ortografiskt: en 30 m stolpe vid 100 px/m ryms i djupet (toppen "
          "närmast) — kartmatrisens ±d klipper den",
          "topp " + std::to_string(dTopp) + ", fot " + std::to_string(dFot) +
              ", kartans " + std::to_string(c[2] / c[3]));

    gfx::MapView tom = o;
    tom.box.w        = 0.0f;
    const gfx::View3D tv = gfx::toView3D(tom, 0.0, 0.0);
    check(tv.box.w == 0.0f && !gfx::project(tv, {o.cx, o.cy, 0.0}, px, py),
          "en oanvändbar MapView ger en oanvändbar vy");
}

// ------------------------------------------------------------------ 11
void fallKonventionen()
{
    using glaze::Camera3D;
    Camera3D c;
    c.target   = {674000.0, 6580000.0, 10.0};
    c.distance = 100.0;
    c.azimuth  = 0.0f;
    const V3 e0 = v3(c.eye());
    check(nära(e0.x, c.target.x, 1e-9) && e0.y < c.target.y &&
              e0.z > c.target.z,
          "azimut 0: ögat står SÖDER om (och ovanför) målet — blicken mot norr",
          tre(e0));
    c.azimuth   = 90.0f;
    const V3 e9 = v3(c.eye());
    check(e9.x < c.target.x && nära(e9.y, c.target.y, 1e-9),
          "azimut 90: ögat står VÄSTER om målet — blicken mot öster", tre(e9));

    // Hela bilden mot hålkamerans förväntade värde.
    struct Fall {
        float az, el, fov;
    };
    const Fall fall[] = {{0, 30, 45},  {30, 30, 45},   {135, 70, 60},
                         {-100, -20, 35}, {200, 85, 45}, {10, 90, 50}};
    const V3   mål{674010.25, 6580020.5, 12.0};
    double     maxFel = 0.0;
    bool       allaOk = true;
    int        antal  = 0;
    for (const Fall& f : fall) {
        Camera3D k;
        k.target    = p3(mål);
        k.distance  = 55.0;
        k.azimuth   = f.az;
        k.elevation = f.el;
        k.fov       = f.fov;
        const Hal h = orbitFacit(mål, 55.0, f.az, f.el, f.fov, kRuta3D);
        for (int i = -3; i <= 3; ++i)
            for (int j = -3; j <= 3; ++j)
                for (int z = -1; z <= 1; ++z) {
                    const V3 p = mål + V3{i * 4.5, j * 3.5, z * 6.0};
                    double   fx, fy, ix, iy;
                    if (!hal(h, p, fx, fy))
                        continue;
                    if (!proj3(k, p, ix, iy)) {
                        allaOk = false;
                        continue;
                    }
                    maxFel = std::max(maxFel, hypot2(fx, fy, ix, iy));
                    ++antal;
                }
    }
    check(allaOk && antal > 800 && maxFel < 1e-6,
          "Camera3D → View3D → project = hålkamerans facit (azimut, "
          "elevation, synfält)",
          std::to_string(antal) + " punkter, största fel " +
              std::to_string(maxFel) + " px");

    // ROLLEN vrider bilden MOTURS kring mitten: (dx, dy) → (dx cos ρ +
    // dy sin ρ, −dx sin ρ + dy cos ρ) med y nedåt.
    {
        Camera3D k;
        k.target    = p3(mål);
        k.distance  = 40.0;
        k.azimuth   = 25.0f;
        k.elevation = 35.0f;
        Camera3D rullad = k;
        rullad.roll     = 20.0f;
        const double ρ  = rad(20.0);
        const double cx = kRuta3D.x + kRuta3D.w / 2.0;
        const double cy = kRuta3D.y + kRuta3D.h / 2.0;
        double rFel = 0.0;
        bool   ok   = true;
        for (const V3& d : {V3{5, 0, 0}, V3{0, 7, 2}, V3{-3, 4, -1}}) {
            double ax, ay, bx, by;
            ok = ok && proj3(k, mål + d, ax, ay) &&
                 proj3(rullad, mål + d, bx, by);
            const double dx = ax - cx, dy = ay - cy;
            rFel = std::max(rFel,
                            hypot2(cx + dx * std::cos(ρ) + dy * std::sin(ρ),
                                   cy - dx * std::sin(ρ) + dy * std::cos(ρ), bx,
                                   by));
        }
        check(ok && rFel < 1e-6, "rollen: positiv vrider bilden moturs",
              "största fel " + std::to_string(rFel) + " px");
    }

    // FÖRSTAPERSONEN: Riktningen (sin yaw · cos pitch, cos yaw ·
    // cos pitch, sin pitch) — pitch UPPÅT — och ögat tillbaka.
    const glaze::Point3 öga{674100.0, 6580200.0, 2.0};
    const Camera3D      fp = Camera3D::lookFrom(öga, 30.0f, 10.0f);
    const V3            fw = v3(glaze::detail::cameraBasis(fp).forward);
    const V3 facitFw{std::sin(rad(30)) * std::cos(rad(10)),
                     std::cos(rad(30)) * std::cos(rad(10)), std::sin(rad(10))};
    check(len(fw - facitFw) < 1e-12 && len(v3(fp.eye()) - v3(öga)) < 1e-9 &&
              nära(fp.distance, 1.0, 0.0),
          "lookFrom: blicken som en förstapersonskamera (pitch uppåt), "
          "målet en enhet fram, ögat tillbaka",
          tre(fw) + " öga " + tre(v3(fp.eye())));
}

// ------------------------------------------------------------------ 12
void fallMatchad()
{
    using glaze::Camera3D;
    Camera3D p;
    p.target    = {674000.5, 6580000.25, 15.0};
    p.distance  = 120.0;
    p.azimuth   = 40.0f;
    p.elevation = 28.0f;
    p.fov       = 50.0f;
    Camera3D o  = p;
    o.projection = glaze::Projection::Orthographic;   // orthoHeight 0 = matchad
    const Hal h = orbitFacit(v3(p.target), 120.0, 40.0, 28.0, 50.0, kRuta3D);
    double maxFel = 0.0;
    bool   ok     = true;
    for (int i = -3; i <= 3; ++i)
        for (int j = -3; j <= 3; ++j) {
            // Punkter i målplanet: målet + höger · a + upp · b (grindens bas).
            const V3 q = v3(p.target) + h.r * (i * 9.0) + h.u * (j * 7.0);
            double   ax, ay, bx, by;
            ok = ok && proj3(p, q, ax, ay) && proj3(o, q, bx, by);
            maxFel = std::max(maxFel, hypot2(ax, ay, bx, by));
        }
    check(ok && maxFel < 1e-6,
          "ortografiskt med MATCHAD höjd = perspektivet för punkter i "
          "målplanet",
          "största skillnad " + std::to_string(maxFel) + " px");
}

// ------------------------------------------------------------------ 13
void fallStrålen()
{
    using glaze::Camera3D;
    Camera3D k;
    k.target    = {674000.0, 6580000.0, 20.0};
    k.distance  = 60.0;
    k.azimuth   = -35.0f;
    k.elevation = 22.0f;
    k.roll      = 8.0f;
    Camera3D o  = k;
    o.projection = glaze::Projection::Orthographic;
    gfx::MapView mv;
    mv.cx         = 674000.0;
    mv.cy         = 6580000.0;
    mv.pxPerUnit  = 12.0;
    mv.bearingDeg = 20.0;
    mv.pitchDeg   = 50.0;
    mv.yUp        = true;
    mv.box        = kRuta3D;
    const gfx::View3D vyer[] = {
        glaze::detail::view3D(k, kRuta3D, std::nullopt),
        glaze::detail::view3D(o, kRuta3D, std::nullopt),
        gfx::toView3D(mv, -10.0, 60.0)};
    const char* namn[] = {"perspektiv", "ortografiskt", "ur toView3D"};
    for (int n = 0; n < 3; ++n) {
        double maxFel = 0.0, maxLen = 0.0;
        bool   ok     = true;
        for (int i = 0; i <= 8; ++i)
            for (int j = 0; j <= 6; ++j) {
                const double x = kRuta3D.x + kRuta3D.w * i / 8.0;
                const double y = kRuta3D.y + kRuta3D.h * j / 6.0;
                gfx::Ray3d   r;
                double       px, py;
                ok = ok && gfx::rayThrough(vyer[n], x, y, r);
                const V3 p = v3(r.origin) + v3(r.dir) * 45.0;
                ok = ok && gfx::project(vyer[n], g3(p), px, py);
                maxFel = std::max(maxFel, hypot2(px, py, x, y));
                maxLen = std::max(maxLen, std::abs(len(v3(r.dir)) - 1.0));
            }
        check(ok && maxFel < 1e-6 && maxLen < 1e-12,
              std::string("strålen genom en pixel tillbaka till pixeln (") +
                  namn[n] + ")",
              "största fel " + std::to_string(maxFel) + " px");
    }
}

// ------------------------------------------------------------------ 14
void fallSkalan3D()
{
    using glaze::Camera3D;
    Camera3D k;
    k.target    = {674000.0, 6580000.0, 0.0};
    k.distance  = 80.0;
    k.azimuth   = 30.0f;
    k.elevation = 25.0f;
    k.fov       = 55.0f;
    const gfx::View3D v = glaze::detail::view3D(k, kRuta3D, std::nullopt);
    const Hal h = orbitFacit(v3(k.target), 80.0, 30.0, 25.0, 55.0, kRuta3D);
    // Punkter nära och fjärran, i mitten och nära ytans hörn — derivatan
    // längs höger och upp (vinkelrätt mot blicken) VID punkten.
    double maxRel = 0.0;
    bool   ok     = true;
    for (const double djup : {30.0, 80.0, 400.0})
        for (const double ax : {0.0, 0.7, -0.95})
            for (const double ay : {0.0, -0.6, 0.9}) {
                const double t = std::tan(h.fovY / 2.0);
                const V3     p = h.eye + h.f * djup +
                             h.r * (ax * t * djup * kRuta3D.w / kRuta3D.h) +
                             h.u * (ay * t * djup);
                // Steget: längs höger och upp ändras inte djupet, så
                // projektionen är LINJÄR där och differensen exakt för
                // varje steg — det som begränsar är punktens avrundning vid
                // SWEREF (≈ 1e-9 m), och 1/1000 av djupet håller den under
                // 1e-7 relativt.
                const double s  = gfx::pxPerUnitAt(v, g3(p));
                const double hh = 1e-3 * djup;
                for (const V3& d : {h.r, h.u}) {
                    double x0, y0, x1, y1;
                    ok = ok && gfx::project(v, g3(p - d * hh), x0, y0) &&
                         gfx::project(v, g3(p + d * hh), x1, y1);
                    const double num = hypot2(x1, y1, x0, y0) / (2.0 * hh);
                    maxRel = std::max(maxRel, std::abs(s - num) / num);
                }
            }
    check(ok && maxRel < 1e-6,
          "pxPerUnitAt = numerisk derivata av project VID punkten (nära, "
          "fjärran, i hörnen)",
          "största relativa fel " + std::to_string(maxRel));

    Camera3D o   = k;
    o.projection = glaze::Projection::Orthographic;
    const gfx::View3D ov = glaze::detail::view3D(o, kRuta3D, std::nullopt);
    const double      hh = 80.0 * std::tan(rad(55.0) / 2.0);
    check(nära(gfx::pxPerUnitAt(ov, g3(h.eye + h.f * 10.0)),
               300.0 / hh, 1e-9) &&
              nära(gfx::pxPerUnitAt(ov, g3(h.eye + h.f * 900.0)),
                   300.0 / hh, 1e-9) &&
              gfx::pxPerUnitAt(v, g3(h.eye - h.f * 5.0)) == 0.0,
          "pxPerUnitAt: ortografiskt samma på varje djup, perspektiv 0 "
          "bakom ögat");
}

// ------------------------------------------------------------------ 15
void fallTumlingen()
{
    using glaze::Camera3D;
    Camera3D c;
    c.target    = {100.0, 200.0, 5.0};
    c.distance  = 50.0;
    c.azimuth   = 20.0f;
    c.elevation = 85.0f;
    const V3 ref = v3(c.target) + V3{6.0, 9.0, 2.0};
    V3       förraUpp = v3(glaze::detail::cameraBasis(c).up);
    double   förraX = 0.0, förraY = 0.0;
    proj3(c, ref, förraX, förraY);
    double maxVinkelFel = 0.0, maxFacit = 0.0, maxUppFacit = 0.0,
           maxSteg = 0.0;
    bool ok = true;
    for (int steg = 1; steg <= 10; ++steg) {
        c = glaze::detail::orbitCamera3D(c, 0.0f, 1.0f, true,
                                         glaze::detail::defaultElevationMin(),
                                         glaze::detail::defaultElevationMax());
        ok = ok && nära(c.elevation, 85.0 + steg, 1e-4);
        const V3     upp = v3(glaze::detail::cameraBasis(c).up);
        const double vinkel =
            std::acos(std::clamp(dot(upp, förraUpp), -1.0, 1.0));
        maxVinkelFel = std::max(maxVinkelFel, std::abs(vinkel - rad(1.0)));
        const Hal h = orbitFacit(v3(c.target), 50.0, 20.0, 85.0 + steg, 45.0,
                                 kRuta3D);
        maxUppFacit = std::max(maxUppFacit, len(upp - h.u));
        double x, y, fx, fy;
        ok = ok && proj3(c, ref, x, y) && hal(h, ref, fx, fy);
        maxFacit = std::max(maxFacit, hypot2(x, y, fx, fy));
        maxSteg  = std::max(maxSteg, hypot2(x, y, förraX, förraY));
        förraUpp = upp;
        förraX   = x;
        förraY   = y;
    }
    check(ok && maxVinkelFel < 1e-6 && maxUppFacit < 1e-6 && maxFacit < 1e-6 &&
              maxSteg < 25.0,
          "TUMLINGEN 85 → 95°: elevationen följer, upp vrids 1° per steg och "
          "är facits, referenspunkten rör sig kontinuerligt dit facit säger",
          "upp-vinkelfel " + std::to_string(maxVinkelFel) + ", facitfel " +
              std::to_string(maxFacit) + " px, största steg " +
              std::to_string(maxSteg) + " px");

    Camera3D spärrad = c;
    spärrad.elevation = 80.0f;
    spärrad = glaze::detail::orbitCamera3D(
        spärrad, 15.0f, 10.0f, false, glaze::detail::defaultElevationMin(),
        glaze::detail::defaultElevationMax());
    Camera3D varv = c;
    varv.azimuth  = 170.0f;
    varv = glaze::detail::orbitCamera3D(varv, 30.0f, 0.0f, false, -85.0f,
                                        85.0f);
    check(nära(spärrad.elevation, 85.0, 0.0) &&
              nära(spärrad.azimuth, 35.0, 1e-4) &&
              nära(varv.azimuth, -160.0, 1e-4),
          "utan tumling klampas elevationen (85°); azimuten i (−180, 180]");
}

// ------------------------------------------------------------------ 16
void fallUppY()
{
    using glaze::Camera3D;
    Camera3D z;
    z.target    = {3.0, -4.0, 2.0};
    z.distance  = 30.0;
    z.azimuth   = 25.0f;
    z.elevation = 30.0f;
    // Samma kamera i den roterade världen (x, y, z) → (x, z, −y).
    const auto rot = [](V3 p) { return V3{p.x, p.z, -p.y}; };
    Camera3D   y   = z;
    y.up           = glaze::UpAxis::Y;
    y.target       = p3(rot(v3(z.target)));
    double maxFel = 0.0;
    bool   ok     = true;
    for (int i = -2; i <= 2; ++i)
        for (int j = -2; j <= 2; ++j)
            for (int k = -1; k <= 1; ++k) {
                const V3 p = v3(z.target) + V3{i * 3.0, j * 2.5, k * 4.0};
                double   ax, ay, bx, by;
                ok = ok && proj3(z, p, ax, ay) && proj3(y, rot(p), bx, by);
                maxFel = std::max(maxFel, hypot2(ax, ay, bx, by));
            }
    check(ok && maxFel < 1e-6,
          "UpAxis::Y: samma bild som Z-kameran av den roterade världen (ingen "
          "spegling)",
          "största fel " + std::to_string(maxFel) + " px");

    // Rakt på: azimut 0 — +y upp, +x höger och norr (−z) längre bort.
    Camera3D a;
    a.up        = glaze::UpAxis::Y;
    a.target    = {1.0, 2.0, 3.0};
    a.distance  = 20.0;
    a.elevation = 20.0f;
    const V3 t  = v3(a.target);
    double   tx, ty, ux, uy, rx, ry, nx, ny, sx, sy;
    ok = proj3(a, t, tx, ty) && proj3(a, t + V3{0, 1, 0}, ux, uy) &&
         proj3(a, t + V3{1, 0, 0}, rx, ry) &&
         proj3(a, t + V3{0, 0, -1}, nx, ny) && proj3(a, t + V3{0, 0, 1}, sx, sy);
    check(ok && uy < ty - 10.0 && nära(ux, tx, 1e-9) && rx > tx + 10.0 &&
              ny < ty && sy > ty,
          "UpAxis::Y: världens +Y uppåt i bilden, öster höger, norr (−z) bort");
}

// ------------------------------------------------------------------ 17
// Punkten i det konvexa polygonet (hörnen moturs sett från normalen n),
// med toleransen tol i världsenheter.
bool iPolygonet(const std::vector<gfx::Vec3d>& poly, V3 n, V3 p, double tol)
{
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const V3 a = v3(poly[i]);
        const V3 b = v3(poly[(i + 1) % poly.size()]);
        const V3 kant = b - a;
        // Inåt = n × kanten för ett moturs polygon.
        const V3 in = cross(n, kant) * (1.0 / len(kant));
        if (dot(p - a, in) < -tol)
            return false;
    }
    return true;
}

void fallSynligt()
{
    // ORTOGRAFISKT RAKT PÅ: rutan 400 × 300 med halvhöjden 5 — planet z = 0
    // syns som x ∈ 10 ± 6,667, y ∈ 20 ± 5, och inget annat.
    gfx::View3D o;
    o.eye             = {10.0, 20.0, 50.0};
    o.forward         = {0.0, 0.0, -1.0};
    o.up              = {0.0, 1.0, 0.0};
    o.projection      = gfx::Projection3D::Orthographic;
    o.orthoHalfHeight = 5.0;
    o.nearZ           = 1.0;
    o.farZ            = 100.0;
    o.box             = gfx::Rectf{0.0f, 0.0f, 400.0f, 300.0f};
    const auto rak = gfx::visiblePlaneRegion(o, {{0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}},
                                             0.0);
    const double hw  = 5.0 * 4.0 / 3.0;
    bool         ok  = rak.size() == 4;
    double       yta = 0.0;
    for (std::size_t i = 0; ok && i < rak.size(); ++i) {
        const gfx::Vec3d& q = rak[i];
        const bool hörn = (nära(q.x, 10.0 - hw, 1e-9) || nära(q.x, 10.0 + hw, 1e-9)) &&
                          (nära(q.y, 15.0, 1e-9) || nära(q.y, 25.0, 1e-9)) &&
                          nära(q.z, 0.0, 1e-9);
        ok = ok && hörn;
        const gfx::Vec3d& n = rak[(i + 1) % rak.size()];
        yta += q.x * n.y - n.x * q.y;
    }
    check(ok && nära(yta / 2.0, 2.0 * hw * 10.0, 1e-9),
          "visiblePlaneRegion: ortografiskt rakt på planet ger EXAKT rutan, "
          "moturs sett från normalen",
          std::to_string(rak.size()) + " hörn, area " + std::to_string(yta / 2.0));

    // PERSPEKTIV snett mot marken: varje hörn i bild (inom ytan och mellan
    // klipplanen), och skärmhörnens träffar i polygonet.
    glaze::Camera3D k;
    k.target    = {674000.0, 6580000.0, 0.0};
    k.distance  = 70.0;
    k.azimuth   = 30.0f;
    k.elevation = 12.0f;
    k.fov       = 60.0f;
    k.farPlane  = 400.0;
    const gfx::View3D v = glaze::detail::view3D(k, kRuta3D, std::nullopt);
    const gfx::Plane3d mark{{0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};
    for (const double maxD : {0.0, 150.0}) {
        const auto poly = gfx::visiblePlaneRegion(v, mark, maxD);
        bool       iBild = poly.size() >= 3;
        for (const gfx::Vec3d& q : poly) {
            double px, py, d;
            iBild = iBild && gfx::project(v, q, px, py, &d) &&
                    px >= kRuta3D.x - 1e-6 &&
                    px <= kRuta3D.x + kRuta3D.w + 1e-6 &&
                    py >= kRuta3D.y - 1e-6 &&
                    py <= kRuta3D.y + kRuta3D.h + 1e-6 && d >= -1e-9 &&
                    d <= 1.0 + 1e-9 && nära(q.z, 0.0, 1e-6);
        }
        int  träffar = 0;
        bool ryms    = true;
        const double gräns = maxD > 0.0 ? maxD : v.farZ;
        const V3 f = v3(v.forward) * (1.0 / len(v3(v.forward)));
        for (int c = 0; c < 4; ++c) {
            gfx::Ray3d r;
            gfx::Vec3d hit;
            gfx::rayThrough(v, kRuta3D.x + ((c & 1) ? kRuta3D.w : 0.0f),
                            kRuta3D.y + ((c & 2) ? kRuta3D.h : 0.0f), r);
            if (!gfx::intersect(r, mark, hit) ||
                dot(v3(hit) - v3(v.eye), f) > gräns)
                continue;   // ovanför horisonten eller bortom gränsen
            ++träffar;
            ryms = ryms && iPolygonet(poly, V3{0, 0, 1}, v3(hit), 1e-6);
        }
        check(iBild && träffar == 2 && ryms,
              "visiblePlaneRegion i perspektiv (maxDistance " +
                  std::to_string(static_cast<int>(maxD)) +
                  "): hörnen i bild, skärmhörnens träffar i polygonet",
              std::to_string(poly.size()) + " hörn, " +
                  std::to_string(träffar) + " träffar");
    }
    const auto bakom = gfx::visiblePlaneRegion(
        v, {{0.0, 0.0, 1000.0}, {0.0, 0.0, 1.0}}, 0.0);
    check(bakom.empty(), "visiblePlaneRegion: ett plan ovanför ögat som vyn "
                         "inte ser är tomt");
}

// ------------------------------------------------------------------ 18
// GPU:ns väg i float för en 3D-vy: vertexen float(punkt − origo) och
// matrisen float-avrundad, multiplikationen och divisionen i float.
double floatFel3D(const gfx::View3D& v, V3 p, V3 o)
{
    float m[16];
    gfx::viewProjection(v, o.x, o.y, o.z).toFloat(m);
    const float vx = static_cast<float>(p.x - o.x);
    const float vy = static_cast<float>(p.y - o.y);
    const float vz = static_cast<float>(p.z - o.z);
    const float cx = m[0] * vx + m[4] * vy + m[8] * vz + m[12];
    const float cy = m[1] * vx + m[5] * vy + m[9] * vz + m[13];
    const float cw = m[3] * vx + m[7] * vy + m[11] * vz + m[15];
    double rx = 0.0, ry = 0.0;
    gfx::project(v, g3(p), rx, ry);
    return hypot2(static_cast<double>(cx / cw), static_cast<double>(cy / cw),
                  rx, ry);
}

void fallRtc3D()
{
    glaze::Camera3D k;
    k.target    = {674000.123, 6580000.456, 35.0};
    k.distance  = 2.5;
    k.azimuth   = 33.0f;
    k.elevation = 20.0f;
    const gfx::View3D v = glaze::detail::view3D(k, kRuta3D, std::nullopt);
    // Chunkens origo är en av DATANS punkter, inte ögat.
    const V3 origo{674000.5, 6580000.25, 34.0};
    double   maxRtc = 0.0, minUtan = 1e30;
    for (int i = -3; i <= 3; ++i)
        for (int j = -3; j <= 3; ++j)
            for (int z = -2; z <= 2; ++z) {
                // SWEREF + hela mm: 1 mm är ≈ 0,3 px på det här avståndet.
                const V3 p = v3(k.target) + V3{i * 0.101, j * 0.073, z * 0.031} +
                             V3{0.001, 0.001, 0.001};
                maxRtc  = std::max(maxRtc, floatFel3D(v, p, origo));
                minUtan = std::min(minUtan, floatFel3D(v, p, V3{}));
            }
    check(maxRtc < 0.05,
          "RTC i 3D vid SWEREF + 1 mm: float-vägen < 0,05 px från double",
          "största fel " + std::to_string(maxRtc) + " px");
    check(minUtan > 1.0,
          "KONTROLL: utan RTC (origo 0) missar 3D-vägens float med mer än 1 px",
          "minsta fel " + std::to_string(minUtan) + " px");
}

// ------------------------------------------------------------------ 19
void fallVerktygen()
{
    using glaze::Camera3D;
    namespace d = glaze::detail;

    // PANORERINGEN i takt med handen, båda projektionerna: punkten i
    // målplanet under trycket står under pekaren efteråt.
    for (const bool orto : {false, true}) {
        Camera3D k;
        k.target     = {674000.0, 6580000.0, 12.0};
        k.distance   = 90.0;
        k.azimuth    = 50.0f;
        k.elevation  = 33.0f;
        k.roll       = 5.0f;
        k.projection = orto ? glaze::Projection::Orthographic
                            : glaze::Projection::Perspective;
        const gfx::View3D v = d::view3D(k, kRuta3D, std::nullopt);
        const V3 fw = v3(d::cameraBasis(k).forward);
        gfx::Ray3d r;
        gfx::Vec3d a;
        const bool träff =
            gfx::rayThrough(v, 200.0, 150.0, r) &&
            gfx::intersect(r, {g3(v3(k.target)), g3(fw)}, a);
        Camera3D   efter = k;
        const bool pan   = d::panCamera3D(efter, kRuta3D, 200.0, 150.0, 640.0,
                                          420.0, d::PanVertical::Plane);
        double px = 0.0, py = 0.0;
        proj3(efter, v3(a), px, py);
        check(träff && pan && nära(px, 640.0, 1e-6) && nära(py, 420.0, 1e-6) &&
                  efter.distance == k.distance && efter.azimuth == k.azimuth,
              std::string("panoreringen i takt med handen (") +
                  (orto ? "ortografiskt" : "perspektiv") +
                  "): målplanets punkt följer pekaren",
              par(px, py));
    }

    // FRAM/BAK: sidled är målplanets, och den
    // LODRÄTA delen går längs BLICKEN (Fly) eller världens z (Lift) med
    // målplanets belopp. Förväntat värde ur grindens egen hålkamera (`orbitFacit`,
    // rollen 0): målplanets punkter under trycket och släppet, och deras
    // skillnad uppdelad på hålkamerans höger och upp — inget ur
    // camera_math:s formel.
    //
    // SABOTAGE: `panCamera3D` som ignorerar läget (alltid Plane) ⇒ Fly- och
    // Lift-fallen faller; den lodräta delens tecken vänt ⇒ "nedåt flyger
    // framåt" och "nedåt höjer" faller; Fly längs blickens MARKPROJEKTION i
    // stället för blicken ⇒ Fly-fallet faller (elevationen är 33°).
    for (const bool orto : {false, true}) {
        Camera3D k;
        k.target     = {674000.0, 6580000.0, 12.0};
        k.distance   = 90.0;
        k.azimuth    = 50.0f;
        k.elevation  = 33.0f;
        k.projection = orto ? glaze::Projection::Orthographic
                            : glaze::Projection::Perspective;
        Hal h = orbitFacit(v3(k.target), k.distance, k.azimuth, k.elevation,
                           k.fov, kRuta3D);
        h.ortho = orto;
        h.halfH = k.distance * std::tan(rad(k.fov) / 2.0);   // matchad
        const double cx = kRuta3D.x + kRuta3D.w / 2.0;
        const double cy = kRuta3D.y + kRuta3D.h / 2.0;
        // Målplanets punkt under en pixel: strålen ur hålkameran mot planet
        // genom målet vinkelrätt mot blicken (avståndet längs blicken).
        const auto målpunkt = [&](double px, double py) {
            if (h.ortho) {
                const double s = (kRuta3D.h / 2.0) / h.halfH;
                return h.eye + h.r * ((px - cx) / s) - h.u * ((py - cy) / s) +
                       h.f * k.distance;
            }
            const double F = (kRuta3D.h / 2.0) / std::tan(h.fovY / 2.0);
            const V3     riktning =
                h.f + h.r * ((px - cx) / F) - h.u * ((py - cy) / F);
            return h.eye + riktning * k.distance;   // riktning · f = 1
        };
        const V3 D = målpunkt(200.0, 150.0) - målpunkt(640.0, 420.0);
        const char* proj = orto ? "ortografiskt" : "perspektiv";
        for (const d::PanVertical läge :
             {d::PanVertical::Fly, d::PanVertical::Lift}) {
            const bool fly   = läge == d::PanVertical::Fly;
            Camera3D   efter = k;
            const bool pan   = d::panCamera3D(efter, kRuta3D, 200.0, 150.0,
                                              640.0, 420.0, läge);
            const V3   väntad = v3(k.target) + h.r * dot(D, h.r) +
                              (fly ? h.f : V3{0.0, 0.0, 1.0}) * dot(D, h.u);
            check(pan && len(v3(efter.target) - väntad) < 1e-6 &&
                      efter.distance == k.distance &&
                      efter.azimuth == k.azimuth &&
                      efter.elevation == k.elevation,
                  std::string(fly ? "Fly" : "Lift") + " (" + proj +
                      "): sidled målplanets, fram/bak " +
                      (fly ? "längs blicken" : "längs z") +
                      " med målplanets belopp; avstånd och vinklar orörda",
                  tre(v3(efter.target)) + " mot " + tre(väntad));
        }
        // Ett rent drag NEDÅT: världen dras emot en — kameran flyger FRAMÅT,
        // och med Lift höjs den, lika långt som målplanet flyttats.
        const double belopp = len(målpunkt(440.0, 330.0) - målpunkt(440.0, 430.0));
        Camera3D     fram   = k;
        Camera3D     upp    = k;
        d::panCamera3D(fram, kRuta3D, 440.0, 330.0, 440.0, 430.0,
                       d::PanVertical::Fly);
        d::panCamera3D(upp, kRuta3D, 440.0, 330.0, 440.0, 430.0,
                       d::PanVertical::Lift);
        const V3 df = v3(fram.target) - v3(k.target);
        check(nära(dot(df, h.f), belopp, 1e-6) && len(df - h.f * dot(df, h.f)) < 1e-6,
              std::string("Fly (") + proj + "): ett drag nedåt flyger FRAMÅT "
                                            "längs blicken",
              tre(df));
        check(nära(upp.target.z - k.target.z, belopp, 1e-6) &&
                  nära(upp.target.x, k.target.x, 1e-6) &&
                  nära(upp.target.y, k.target.y, 1e-6),
              std::string("Lift (") + proj + "): ett drag nedåt höjer kameran",
              tre(v3(upp.target)));
    }

    // DOLLYN och dess gränser; ortografiskt följer en satt halvhöjd med.
    Camera3D k;
    k.distance = 100.0;
    const Camera3D n1 = d::dollyCamera3D(k, 0.5, 0.0, 0.0);
    const Camera3D n2 = d::dollyCamera3D(k, 0.01, 20.0, 0.0);
    const Camera3D n3 = d::dollyCamera3D(k, 50.0, 0.0, 400.0);
    const Camera3D n4 = d::dollyCamera3D(k, -2.0, 0.0, 0.0);
    Camera3D       ok = k;
    ok.projection     = glaze::Projection::Orthographic;
    ok.orthoHeight    = 8.0;
    const Camera3D n5 = d::dollyCamera3D(ok, 0.25, 0.0, 0.0);
    check(nära(n1.distance, 50.0, 1e-12) && nära(n2.distance, 20.0, 0.0) &&
              nära(n3.distance, 400.0, 0.0) && n4.distance == 100.0 &&
              nära(n5.distance, 25.0, 1e-12) && nära(n5.orthoHeight, 2.0, 1e-12),
          "dollyn: avståndet × faktorn inom gränserna, satt halvhöjd följer");

    // GLIDNINGEN: ändarna exakta, azimuten den korta vägen över 180°,
    // avståndet geometriskt, elevationen linjärt.
    Camera3D a, b;
    a.target    = {0.0, 0.0, 0.0};
    a.distance  = 10.0;
    a.azimuth   = 170.0f;
    a.elevation = 10.0f;
    b.target    = {100.0, -40.0, 6.0};
    b.distance  = 1000.0;
    b.azimuth   = -170.0f;
    b.elevation = 50.0f;
    const Camera3D m = d::easeCamera3D(a, b, 0.5);
    const Camera3D e = d::easeCamera3D(a, b, 1.0);
    check(nära(m.distance, 100.0, 1e-9) && nära(std::abs(m.azimuth), 180.0, 1e-4) &&
              nära(m.elevation, 30.0, 1e-4) && nära(m.target.x, 50.0, 1e-9) &&
              nära(m.target.y, -20.0, 1e-9) && m.orthoHeight == 0.0 &&
              e.azimuth == b.azimuth && e.distance == b.distance,
          "glidningen: azimuten den korta vägen, avståndet geometriskt, "
          "elevationen linjärt, slutet exakt",
          "azimut " + std::to_string(m.azimuth) + ", avstånd " +
              std::to_string(m.distance));

    // INRAMNINGEN: hörnen ryms med luften och sitter tätt, rotationen orörd.
    const glaze::Box3 låda{{674000.0, 6580000.0, 0.0},
                           {674030.0, 6580012.0, 18.0}};
    const double luft = 20.0;
    for (int läge = 0; läge < 3; ++läge) {
        Camera3D c;
        c.distance  = 7.0;
        c.azimuth   = 40.0f;
        c.elevation = 30.0f;
        if (läge > 0)
            c.projection = glaze::Projection::Orthographic;
        if (läge == 2)
            c.orthoHeight = 3.0;
        const Camera3D f = d::fitBox(c, låda, kRuta3D, luft);
        double         ute = 0.0, tätast = 1e30;
        bool           alla = true;
        for (int i = 0; i < 8; ++i) {
            const V3 h{(i & 1) ? låda.max.x : låda.min.x,
                       (i & 2) ? låda.max.y : låda.min.y,
                       (i & 4) ? låda.max.z : låda.min.z};
            double px, py;
            alla = alla && proj3(f, h, px, py);
            const double kant = std::min(
                {px - (kRuta3D.x + luft), kRuta3D.x + kRuta3D.w - luft - px,
                 py - (kRuta3D.y + luft), kRuta3D.y + kRuta3D.h - luft - py});
            ute    = std::min(ute, kant);
            tätast = std::min(tätast, std::abs(kant));
        }
        const char* namn[] = {"perspektiv", "ortografiskt matchad",
                              "ortografiskt satt höjd"};
        check(alla && ute > -1e-6 && tätast < 1e-6 &&
                  f.azimuth == c.azimuth && f.elevation == c.elevation &&
                  nära(f.target.x, 674015.0, 1e-9) &&
                  (läge != 2 || (f.distance == c.distance &&
                                 f.orthoHeight != c.orthoHeight)),
              std::string("inramningen (") + namn[läge] +
                  "): hörnen ryms med luften och sitter tätt",
              "utanför " + std::to_string(ute) + " px, närmast kanten " +
                  std::to_string(tätast) + " px");
    }

    // INTERSECT: träffen och avståndet, normalen behöver inte vara enhetlig;
    // parallellt och bakom är inga träffar.
    gfx::Vec3d hit;
    double     t = 0.0;
    const bool t1 = gfx::intersect({{1.0, 2.0, 10.0}, {0.0, 0.0, -2.0}},
                                   {{0.0, 0.0, 0.0}, {0.0, 0.0, 5.0}}, hit, &t);
    check(t1 && nära(hit.x, 1.0, 1e-12) && nära(hit.y, 2.0, 1e-12) &&
              nära(hit.z, 0.0, 1e-12) && nära(t, 5.0, 1e-12) &&
              !gfx::intersect({{0, 0, 1}, {1, 0, 0}}, {{0, 0, 0}, {0, 0, 1}}, hit) &&
              !gfx::intersect({{0, 0, 1}, {0, 0, 1}}, {{0, 0, 0}, {0, 0, 1}}, hit),
          "intersect: träffen och t, parallellt och bakom ger ingen träff");

    // FRAMEUNIFORMS: strålbasen ger `rayThrough`s riktning (perspektiv) och
    // början (ortografiskt), världen per px = 1 / pxPerUnitAt, ögat relativt
    // origot och matrisen chunkens.
    for (const bool orto : {false, true}) {
        Camera3D c;
        c.target     = {674000.0, 6580000.0, 30.0};
        c.distance   = 75.0;
        c.azimuth    = -20.0f;
        c.elevation  = 40.0f;
        c.projection = orto ? glaze::Projection::Orthographic
                            : glaze::Projection::Perspective;
        const gfx::View3D   v = d::view3D(c, kRuta3D, std::nullopt);
        const V3            o{674010.0, 6580005.0, 25.0};
        const gfx::FrameUniforms u = gfx::frameUniforms(v, o.x, o.y, o.z);
        float vp[16];
        gfx::viewProjection(v, o.x, o.y, o.z).toFloat(vp);
        bool lika = true;
        for (int i = 0; i < 16; ++i)
            lika = lika && u.viewProj[i] == vp[i];
        double strålFel = 0.0;
        for (const double nx : {-1.0, 0.3, 1.0})
            for (const double ny : {-1.0, -0.4, 1.0}) {
                const double px = kRuta3D.x + kRuta3D.w * (nx + 1.0) / 2.0;
                const double py = kRuta3D.y + kRuta3D.h * (1.0 - ny) / 2.0;
                gfx::Ray3d   r;
                gfx::rayThrough(v, px, py, r);
                const V3 f{u.forward[0], u.forward[1], u.forward[2]};
                const V3 rr{u.right[0], u.right[1], u.right[2]};
                const V3 uu{u.up[0], u.up[1], u.up[2]};
                const V3 e{u.eye[0], u.eye[1], u.eye[2]};
                if (orto) {
                    const V3 början = e + o + rr * nx + uu * ny;
                    strålFel = std::max({strålFel, len(början - v3(r.origin)),
                                         len(f - v3(r.dir))});
                } else {
                    const V3 riktning = f + rr * nx + uu * ny;
                    strålFel = std::max(
                        strålFel,
                        len(riktning * (1.0 / len(riktning)) - v3(r.dir)));
                }
            }
        const V3     p = v3(c.target) + V3{3.0, -2.0, 1.0};
        const double djup =
            dot(p - v3(v.eye), v3(d::cameraBasis(c).forward));
        const double perPx = u.pixelSize[0] * djup + u.pixelSize[1];
        check(lika && strålFel < 1e-4 &&
                  nära(perPx * gfx::pxPerUnitAt(v, g3(p)), 1.0, 1e-6) &&
                  u.eye[3] == (orto ? 0.0f : 1.0f) && u.scale[2] == 1.0f &&
                  u.rayCenter[0] == u.forward[0] &&
                  u.rayCenter[1] == u.forward[1] &&
                  u.rayCenter[2] == u.forward[2] && u.rayCenter[3] == 0.0f,
              std::string("frameUniforms (") +
                  (orto ? "ortografiskt" : "perspektiv") +
                  "): strålbasen = rayThrough, världen per px = 1 / "
                  "pxPerUnitAt, chunkens matris",
              "strålfel " + std::to_string(strålFel));
    }
    gfx::View3D trasig;   // ingen yta
    const gfx::FrameUniforms tom = gfx::frameUniforms(trasig, 0.0, 0.0, 0.0);
    bool noll = tom.scale[1] == 1.0f && tom.scale[2] == 1.0f;
    for (float x : tom.viewProj)
        noll = noll && x == 0.0f;
    check(noll, "frameUniforms för en oanvändbar vy: nollmatris, scale på "
                "sina förval");

    // PROJECTION ur en 3D-vy = project, med programmens gallring; ur en
    // MapView utan den.
    Camera3D c;
    c.target    = {674000.0, 6580000.0, 0.0};
    c.distance  = 40.0;
    c.azimuth   = 15.0f;
    c.elevation = 35.0f;
    const gfx::View3D        v = d::view3D(c, kRuta3D, std::nullopt);
    const d::Projection      pr(v);
    double                   maxFel = 0.0;
    bool                     alla   = pr.map() == nullptr;
    for (int i = -2; i <= 2; ++i)
        for (int j = -2; j <= 2; ++j) {
            const V3 p = v3(c.target) + V3{i * 5.0, j * 4.0, (i + j) * 1.5};
            double   ax, ay, bx, by;
            alla = alla && pr.toScreen(p.x, p.y, p.z, ax, ay) &&
                   gfx::project(v, g3(p), bx, by);
            maxFel = std::max(maxFel, hypot2(ax, ay, bx, by));
        }
    const V3 bakom = v3(v.eye) - v3(d::cameraBasis(c).forward) * 3.0;
    double   sx, sy;
    gfx::MapView mv = swerefvy();
    const d::Projection kp(mv);
    double kx, ky, gx, gy;
    const bool kOk = kp.map() != nullptr && kp.toScreen(mv.cx + 2.0, mv.cy - 1.5, kx, ky) &&
                     gfx::project(mv, mv.cx + 2.0, mv.cy - 1.5, gx, gy) &&
                     kx == gx && ky == gy;
    check(alla && maxFel < 1e-9 && !pr.toScreen(bakom.x, bakom.y, bakom.z, sx, sy) &&
              kOk,
          "Projection ur en 3D-vy = project (bakom ögat ingen skärmpunkt); ur "
          "en MapView bit för bit som kartans",
          "största fel " + std::to_string(maxFel) + " px");
}

// 20. SCENENS LÅDA FÖLJER MESHEN: `sceneBox` är cachad per vy
// med lagrens formversioner och meshernas generation i nyckeln. Efter
// `setPositions` — ingen annan ändring, varken formen eller lagret — följer
// lådan meshens nya lägen, och en fråga till med samma scen ger samma låda.
void fallScenensLåda()
{
    namespace d = glaze::detail;
    d::SceneData sc;
    const auto   L = std::make_shared<d::LayerData>();
    sc.layers.push_back(L);
    glaze::MeshDesc m;
    m.origin    = {674000.0, 6580000.0, 0.0};
    m.positions = {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}};
    glaze::Mesh mesh(std::move(m));
    d::appendShape(*L, glaze::Shape::mesh(mesh), false);
    const std::optional<glaze::Box3> före = d::sceneBox(sc);
    const std::vector<glaze::Float3> lyft{{0, 0, 0}, {4, 0, 7}, {0, 4, 0}};
    mesh.setPositions(0, lyft);
    const std::optional<glaze::Box3> efter = d::sceneBox(sc);
    const std::optional<glaze::Box3> igen  = d::sceneBox(sc);
    check(före && efter && igen && nära(före->max.z, 0.0, 1e-9) &&
              nära(efter->max.z, 7.0, 1e-9) && nära(igen->max.z, 7.0, 1e-9) &&
              nära(efter->min.x, 674000.0, 1e-9) && nära(efter->max.x, 674004.0, 1e-9),
          "20. sceneBox efter setPositions följer meshen (och svarar lika igen)",
          före && efter ? std::to_string(före->max.z) + " → " + std::to_string(efter->max.z)
                        : std::string("tom"));
}

// ------------------------------------------------------------------ 21
void fallFörstapersonen()
{
    namespace d = glaze::detail;
    const auto ögat = [](const glaze::Camera3D& k) { return k.eye(); };
    const auto lika = [](glaze::Point3 a, glaze::Point3 b, double tol) {
        return nära(a.x, b.x, tol) && nära(a.y, b.y, tol) && nära(a.z, b.z, tol);
    };
    // Blickens riktning ur grindens egen konvention: kompassen azimuten,
    // nedåt elevationen (Z upp).
    const auto blick = [](double az, double el) {
        const double a = az * std::numbers::pi / 180.0, e = el * std::numbers::pi / 180.0;
        return glaze::Point3{std::sin(a) * std::cos(e), std::cos(a) * std::cos(e),
                             -std::sin(e)};
    };
    const glaze::Camera3D k0 =
        glaze::Camera3D::lookFrom({674000.0, 6580000.0, 2.0}, 10.0f, -20.0f, 60.0f);
    const glaze::Point3   e0 = ögat(k0);
    const glaze::Camera3D k1 = d::lookAround(k0, 80.0f, -10.0f, -85.0f, 85.0f);
    const glaze::Point3   f  = blick(90.0, 10.0);
    const glaze::Point3   t{e0.x + f.x * k1.distance, e0.y + f.y * k1.distance,
                          e0.z + f.z * k1.distance};
    check(lika(ögat(k1), e0, 1e-6) && nära(k1.azimuth, 90.0, 1e-4) &&
              nära(k1.elevation, 10.0, 1e-4) && lika(k1.target, t, 1e-6),
          "21. lookAround: gir +80° och tilt −10° kring ÖGAT — ögat står kvar och "
          "målet ligger framför det i den nya blicken");
    const glaze::Camera3D k2 = d::lookAround(k0, 0.0f, 200.0f, -85.0f, 85.0f);
    check(nära(k2.elevation, 85.0, 1e-4) && lika(ögat(k2), e0, 1e-6),
          "21. lookAround: elevationen spärras till spannet, ögat står kvar",
          std::to_string(k2.elevation));
    // moveEye: en enhet framåt längs blickens VÅGRÄTA del — tiltad 20°.
    glaze::Camera3D m = k1;
    d::moveEye(m, 1.0, 0.0, 0.0, 0.0);
    const glaze::Point3 fram{e0.x + 1.0, e0.y, e0.z};   // azimut 90 = öster
    check(lika(ögat(m), fram, 1e-9),
          "21. moveEye: framåt en enhet längs blickens vågräta del (öster vid "
          "azimut 90, också tiltad)");
    m = k1;
    d::moveEye(m, 0.0, 1.0, 0.0, 0.0);
    check(lika(ögat(m), glaze::Point3{e0.x, e0.y - 1.0, e0.z}, 1e-9),
          "21. moveEye: åt höger en enhet (söder när blicken är öster)");
    m = k1;
    d::moveEye(m, 0.0, 0.0, 1.0, 0.0);
    check(lika(ögat(m), glaze::Point3{e0.x, e0.y, e0.z + 1.0}, 1e-9),
          "21. moveEye: uppåt en enhet längs världens Z");
    glaze::Camera3D y = glaze::Camera3D::lookFrom({0.0, 2.0, 0.0}, 0.0f, 0.0f, 60.0f,
                                                  glaze::UpAxis::Y);
    const glaze::Point3 ey = ögat(y);
    d::moveEye(y, 0.0, 0.0, 1.0, 0.0);
    check(lika(ögat(y), glaze::Point3{ey.x, ey.y + 1.0, ey.z}, 1e-9),
          "21. moveEye: uppåt en enhet längs världens Y med UpAxis::Y");
    m = k1;
    d::moveEye(m, 0.0, 0.0, 0.0, 1.0);
    const double e = 10.0 * std::numbers::pi / 180.0;   // kamerans upp: blickens tangent
    check(lika(ögat(m), glaze::Point3{e0.x + std::sin(e), e0.y, e0.z + std::cos(e)}, 1e-9),
          "21. moveEye: en enhet längs kamerans upp (lodrätt i bildplanet)");
}

// ------------------------------------------------------------------ 22
//
// ETT ASYMMETRISKT FRUSTUM (`View3D::frustum`). FÖRVÄNTAT VÄRDE: en egen
// hålkamera med tangenter, skriven ur definitionen — punkten q relativt ögat
// med djupet t = q·blick ger sidovinklarna a = q·höger / t och b = q·upp / t,
// och skärmläget är px = x + w · (a − vänster) / (höger − vänster) och py =
// y + h · (upp − b) / (upp − ned). Djupet i [0, 1] mellan klipplanen är
// f · (t − n) / (t · (f − n)). Basen (blick, höger, upp) står uttryckligen i
// kameran nedan; ingenting läses ur camera3d.cpp.

constexpr double kFrNear = 0.5;
constexpr double kFrFar  = 200.0;

struct FrKamera {
    V3                   eye, fw, rt, upv;   // ögat och basen, skrivna för hand
    gfx::FrustumTangents t;
    gfx::Rectf           box{40.0f, 30.0f, 800.0f, 600.0f};
};

gfx::View3D frVy(const FrKamera& k)
{
    gfx::View3D v;
    v.eye        = g3(k.eye);
    v.forward    = g3(k.fw);
    v.up         = gfx::Vec3d{0.0, 0.0, 1.0};
    v.projection = gfx::Projection3D::Perspective;
    v.fovY       = 2.5;   // ersätts av tangenterna — ett värde som skulle synas
    v.frustum    = k.t;
    v.nearZ      = kFrNear;
    v.farZ       = kFrFar;
    v.box        = k.box;
    return v;
}

bool frPx(const FrKamera& k, V3 w, double& px, double& py, double* djup = nullptr)
{
    const V3     q = w - k.eye;
    const double t = dot(q, k.fw);
    if (!(t > 0.0))
        return false;
    const double a = dot(q, k.rt) / t;
    const double b = dot(q, k.upv) / t;
    px = k.box.x + k.box.w * (a - k.t.left) / (k.t.right - k.t.left);
    py = k.box.y + k.box.h * (k.t.up - b) / (k.t.up - k.t.down);
    if (djup)
        *djup = kFrFar * (t - kFrNear) / (t * (kFrFar - kFrNear));
    return true;
}

// Punkten på djupet t med sidovinklarna (a, b).
V3 frPunkt(const FrKamera& k, double t, double a, double b)
{
    return k.eye + k.fw * t + k.rt * (a * t) + k.upv * (b * t);
}

// Strålens riktning genom pixeln, enhetlig: (blick + a · höger + b · upp)
// med a och b ur pixelns läge i frustumet.
V3 frStråle(const FrKamera& k, double px, double py)
{
    const double a = k.t.left + (px - k.box.x) / k.box.w * (k.t.right - k.t.left);
    const double b = k.t.up - (py - k.box.y) / k.box.h * (k.t.up - k.t.down);
    const V3     d = k.fw + k.rt * a + k.upv * b;
    return d * (1.0 / len(d));
}

std::vector<FrKamera> frKameror()
{
    const double s   = std::sqrt(0.5);
    const double c30 = std::cos(rad(30.0));
    const double s30 = std::sin(rad(30.0));
    std::vector<FrKamera> v;
    // Axelrätt, ett vanligt öga: noll ligger mellan tangenterna.
    v.push_back({V3{10.0, 20.0, 30.0}, V3{0.0, 1.0, 0.0}, V3{1.0, 0.0, 0.0},
                 V3{0.0, 0.0, 1.0}, gfx::FrustumTangents{-0.9, 0.5, -0.4, 0.8}});
    // Vriden 45° och FÖRSKJUTET: noll ligger utanför båda spannen.
    v.push_back({V3{-5.0, 3.0, 2.0}, V3{s, s, 0.0}, V3{s, -s, 0.0},
                 V3{0.0, 0.0, 1.0}, gfx::FrustumTangents{0.1, 0.9, 0.2, 0.6}});
    // Lutad 30° nedåt, vid SWEREF.
    v.push_back({V3{674010.5, 6580005.25, 25.125}, V3{0.0, c30, -s30},
                 V3{1.0, 0.0, 0.0}, V3{0.0, s30, c30},
                 gfx::FrustumTangents{-1.2, 0.3, -0.7, 0.35}});
    return v;
}

// Rad `rad` av en kolumnordnad float-matris gånger (x, y, z, 1).
float frKlipp(const float* m, int rad, float x, float y, float z)
{
    return m[rad] * x + m[4 + rad] * y + m[8 + rad] * z + m[12 + rad];
}

V3 frVec3(const float* a)
{
    return V3{a[0], a[1], a[2]};
}

// Provpunkten för de oanvändbara vyerna.
const gfx::Vec3d kFrProv{10.0, 25.0, 30.0};

void fallFrustum()
{
    const std::vector<FrKamera> kameror = frKameror();
    int                         nr      = 0;
    for (const FrKamera& k : kameror) {
        ++nr;
        const std::string namn = "frustum, kamera " + std::to_string(nr) + ": ";
        const gfx::View3D v    = frVy(k);
        const double      x0 = k.box.x, x1 = k.box.x + k.box.w;
        const double      y0 = k.box.y, y1 = k.box.y + k.box.h;

        // HÖRNEN: ytans fyra hörn ligger på tangenterna och projicerar till
        // ytans fyra hörn (ndc ±1).
        {
            struct Hörn {
                double a, b, px, py;
            };
            const Hörn h[] = {
                {k.t.left, k.t.down, x0, y1},
                {k.t.right, k.t.down, x1, y1},
                {k.t.right, k.t.up, x1, y0},
                {k.t.left, k.t.up, x0, y0},
            };
            double fel = 0.0;
            bool   ok  = true;
            for (const Hörn& c : h)
                for (const double t : {1.0, 7.0, 90.0}) {
                    double px, py;
                    if (!gfx::project(v, g3(frPunkt(k, t, c.a, c.b)), px, py)) {
                        ok = false;
                        continue;
                    }
                    fel = std::max({fel, std::abs(px - c.px), std::abs(py - c.py)});
                }
            check(ok && fel < 1e-5, namn + "tangenternas hörn hamnar på ytans hörn",
                  "största fel " + std::to_string(fel) + " px");
        }

        // PROJEKTIONEN mot hålkameran, också utanför ytan, och djupet.
        {
            double fel = 0.0, djupFel = 0.0;
            bool   ok = true;
            for (const double t : {0.8, 3.0, 40.0})
                for (const double a : {k.t.left - 0.5, 0.0,
                                       (k.t.left + k.t.right) / 2.0,
                                       k.t.right + 0.3})
                    for (const double b : {k.t.down - 0.2, 0.1, k.t.up + 0.4}) {
                        const V3 p = frPunkt(k, t, a, b);
                        double   px, py, d, fx, fy, fd;
                        if (!gfx::project(v, g3(p), px, py, &d) ||
                            !frPx(k, p, fx, fy, &fd)) {
                            ok = false;
                            continue;
                        }
                        fel     = std::max({fel, std::abs(px - fx), std::abs(py - fy)});
                        djupFel = std::max(djupFel, std::abs(d - fd));
                    }
            check(ok && fel < 1e-5 && djupFel < 1e-9,
                  namn + "project = hålkameran med tangenter, djupet i [0, 1]",
                  "px " + std::to_string(fel) + ", djup " + std::to_string(djupFel));
            double dn = -1.0, df = -1.0, sx, sy;
            gfx::project(v, g3(frPunkt(k, kFrNear, 0.0, 0.0)), sx, sy, &dn);
            gfx::project(v, g3(frPunkt(k, kFrFar, 0.0, 0.0)), sx, sy, &df);
            check(nära(dn, 0.0, 1e-9) && nära(df, 1.0, 1e-9),
                  namn + "närplanet har djup 0 och det bortre 1",
                  std::to_string(dn) + " / " + std::to_string(df));
        }

        // STRÅLEN genom pixeln mot hålkameran, och tillbaka: punkten längs
        // strålen projicerar till samma pixel.
        {
            double fel = 0.0, tur = 0.0;
            bool   ok = true;
            for (const double px : {x0, 100.3, (x0 + x1) / 2.0, 700.7, x1})
                for (const double py : {y0, 200.1, (y0 + y1) / 2.0, 555.5, y1}) {
                    gfx::Ray3d r;
                    if (!gfx::rayThrough(v, px, py, r)) {
                        ok = false;
                        continue;
                    }
                    fel = std::max(fel, len(v3(r.dir) - frStråle(k, px, py)));
                    fel = std::max(fel, len(v3(r.origin) - k.eye));
                    double qx, qy;
                    if (!gfx::project(v, g3(v3(r.origin) + v3(r.dir) * 5.0), qx, qy)) {
                        ok = false;
                        continue;
                    }
                    tur = std::max({tur, std::abs(qx - px), std::abs(qy - py)});
                }
            check(ok && fel < 1e-9 && tur < 1e-5,
                  namn + "rayThrough = hålkamerans stråle, och project tar den tillbaka",
                  "stråle " + std::to_string(fel) + ", tur och retur " +
                      std::to_string(tur) + " px");
        }

        // SKALAN: px per världsenhet vinkelrätt mot blicken = ytans höjd /
        // (tangentspannet · djupet).
        {
            double fel = 0.0;
            for (const double t : {1.0, 6.0, 55.0}) {
                const V3     p   = frPunkt(k, t, k.t.right, k.t.up);
                const double var = k.box.h / ((k.t.up - k.t.down) * t);
                fel = std::max(fel, std::abs(gfx::pxPerUnitAt(v, g3(p)) / var - 1.0));
            }
            check(fel < 1e-9,
                  namn + "pxPerUnitAt = ytans höjd / (tangentspannet · djupet)",
                  "relativt fel " + std::to_string(fel));
        }

        // GLAZEFRAME: strålens mitt och basen. rayCenter + ndc.x · right +
        // ndc.y · up är strålen genom ytans pixel; ett förskjutet origo
        // ändrar bara ögat.
        {
            const V3 o = k.eye + V3{3.0, -2.0, 1.0};
            const gfx::FrameUniforms u = gfx::frameUniforms(v, o.x, o.y, o.z);
            const double hx = (k.t.right - k.t.left) / 2.0;
            const double hy = (k.t.up - k.t.down) / 2.0;
            const double cx = (k.t.right + k.t.left) / 2.0;
            const double cy = (k.t.up + k.t.down) / 2.0;
            const V3     rc = k.fw + k.rt * cx + k.upv * cy;
            double       fel = 0.0;
            for (const double nx : {-1.0, 0.0, 0.37, 1.0})
                for (const double ny : {-1.0, 0.0, -0.61, 1.0}) {
                    const V3 d = frVec3(u.rayCenter) + frVec3(u.right) * nx +
                                 frVec3(u.up) * ny;
                    const double px = k.box.x + k.box.w * (nx + 1.0) / 2.0;
                    const double py = k.box.y + k.box.h * (1.0 - ny) / 2.0;
                    fel = std::max(fel, len(d * (1.0 / len(d)) - frStråle(k, px, py)));
                }
            check(fel < 1e-5 && len(frVec3(u.rayCenter) - rc) < 1e-6 &&
                      u.rayCenter[3] == 0.0f &&
                      len(frVec3(u.forward) - k.fw) < 1e-6 &&
                      len(frVec3(u.right) - k.rt * hx) < 1e-6 &&
                      len(frVec3(u.up) - k.upv * hy) < 1e-6 &&
                      len(frVec3(u.eye) - (k.eye - o)) < 1e-6 && u.eye[3] == 1.0f &&
                      nära(u.pixelSize[0], hy / (k.box.h / 2.0), 1e-7) &&
                      u.pixelSize[1] == 0.0f,
                  namn + "frameUniforms: rayCenter + ndc · (right, up) = strålen, "
                         "forward kvar som axeln",
                  "strålfel " + std::to_string(fel));
        }

        // GPU:ns VÄG: float(punkt − origo) genom den float-avrundade
        // matrisen i blocket hamnar inom 0,05 px från hålkameran.
        {
            const V3 o = k.eye + V3{1.0, 2.0, -3.0};
            const gfx::FrameUniforms u = gfx::frameUniforms(v, o.x, o.y, o.z);
            double fel = 0.0;
            for (const double t : {2.0, 20.0, 120.0})
                for (const double a : {k.t.left, 0.0, k.t.right})
                    for (const double b : {k.t.down, k.t.up}) {
                        const V3    p  = frPunkt(k, t, a, b);
                        const float lx = static_cast<float>(p.x - o.x);
                        const float ly = static_cast<float>(p.y - o.y);
                        const float lz = static_cast<float>(p.z - o.z);
                        const float cw = frKlipp(u.viewProj, 3, lx, ly, lz);
                        double      fx = 0.0, fy = 0.0;
                        frPx(k, p, fx, fy);
                        fel = std::max(
                            {fel,
                             std::abs(frKlipp(u.viewProj, 0, lx, ly, lz) / cw - fx),
                             std::abs(frKlipp(u.viewProj, 1, lx, ly, lz) / cw - fy)});
                    }
            check(fel < 0.05, namn + "RTC: float-vägen genom blockets matris < 0,05 px",
                  "största fel " + std::to_string(fel) + " px");
        }

        // SYNLIGA DELEN av planet vinkelrätt mot blicken på djupet 10: exakt
        // rektangeln mellan tangenterna.
        {
            std::vector<V3> hörn;
            for (const double a : {k.t.left, k.t.right})
                for (const double b : {k.t.down, k.t.up})
                    hörn.push_back(frPunkt(k, 10.0, a, b));
            const std::vector<gfx::Vec3d> poly = gfx::visiblePlaneRegion(
                v, gfx::Plane3d{g3(k.eye + k.fw * 10.0), g3(k.fw)}, 0.0);
            int funna = 0;
            for (const V3& h : hörn)
                for (const gfx::Vec3d& p : poly)
                    if (len(v3(p) - h) < 1e-6) {
                        ++funna;
                        break;
                    }
            check(poly.size() == 4 && funna == 4,
                  namn + "visiblePlaneRegion: planet på djupet 10 är rektangeln "
                         "mellan tangenterna",
                  std::to_string(poly.size()) + " hörn, " + std::to_string(funna) +
                      " träffar");
        }
    }

    // MITTSTRÅLEN, med tal skrivna för hand: tangenterna (−0,9, 0,5, −0,4,
    // 0,8) ger mitten (−0,2, 0,2), och ytans mittpixel (440, 330) strålen
    // (−0,2, 1, 0,2)/√1,08. Halva spannen är 0,7 och 0,6.
    {
        const gfx::View3D v = frVy(kameror[0]);
        gfx::Ray3d        r;
        const bool        ok = gfx::rayThrough(v, 440.0, 330.0, r);
        const double      n  = std::sqrt(1.08);
        check(ok && nära(r.dir.x, -0.2 / n, 1e-12) && nära(r.dir.y, 1.0 / n, 1e-12) &&
                  nära(r.dir.z, 0.2 / n, 1e-12),
              "frustum: ytans mittpixel ger strålen (−0,2, 1, 0,2) normerad",
              ok ? tre(v3(r.dir)) : "false");
        const gfx::FrameUniforms u = gfx::frameUniforms(v, 0.0, 0.0, 0.0);
        check(nära(u.rayCenter[0], -0.2, 1e-6) && nära(u.rayCenter[1], 1.0, 1e-6) &&
                  nära(u.rayCenter[2], 0.2, 1e-6) && nära(u.right[0], 0.7, 1e-6) &&
                  nära(u.up[2], 0.6, 1e-6),
              "frustum: rayCenter (−0,2, 1, 0,2), right 0,7 och up 0,6 med tal "
              "för hand");
    }

    // SYMMETRISKT ÄR OFÖRÄNDRAT: ett frustum med tangenter ±tan(fov/2) ·
    // bildförhållandet och ±tan(fov/2) ger samma matris som synfältet, och
    // utan frustum är rayCenter EXAKT forward (perspektiv som ortografiskt).
    {
        gfx::View3D v;
        v.eye     = gfx::Vec3d{674000.0, 6580000.0, 30.0};
        v.forward = gfx::Vec3d{0.3, 1.0, -0.4};
        v.fovY    = 1.1;
        v.nearZ   = 0.5;
        v.farZ    = 300.0;
        v.box     = gfx::Rectf{40.0f, 30.0f, 800.0f, 600.0f};
        gfx::View3D  s  = v;
        const double hy = std::tan(0.55);
        s.frustum = gfx::FrustumTangents{-hy * 800.0 / 600.0, hy * 800.0 / 600.0,
                                         -hy, hy};
        const gfx::Mat4d a = gfx::viewProjection(v, 674001.0, 6580002.0, 28.0);
        const gfx::Mat4d b = gfx::viewProjection(s, 674001.0, 6580002.0, 28.0);
        double skillnad = 0.0, störst = 0.0;
        for (int i = 0; i < 16; ++i) {
            skillnad = std::max(skillnad, std::abs(a.m[i] - b.m[i]));
            störst   = std::max(störst, std::abs(a.m[i]));
        }
        check(störst > 0.0 && skillnad < 1e-9 * störst,
              "frustum: ett symmetriskt frustum ger synfältets matris",
              "skillnad " + std::to_string(skillnad));
        gfx::Ray3d ra, rb;
        gfx::rayThrough(v, 123.0, 456.0, ra);
        gfx::rayThrough(s, 123.0, 456.0, rb);
        check(len(v3(ra.dir) - v3(rb.dir)) < 1e-12,
              "frustum: samma stråle ur synfältet och ur det symmetriska "
              "frustumet");

        for (const bool orto : {false, true}) {
            gfx::View3D w = v;
            if (orto) {
                w.projection      = gfx::Projection3D::Orthographic;
                w.orthoHalfHeight = 20.0;
            }
            const gfx::FrameUniforms u =
                gfx::frameUniforms(w, w.eye.x, w.eye.y, w.eye.z);
            bool exakt = true;
            for (int i = 0; i < 4; ++i)
                exakt = exakt && u.rayCenter[i] == u.forward[i];
            check(exakt && u.forward[1] != 0.0f,
                  std::string("frustum: utan frustum är rayCenter exakt forward (") +
                      (orto ? "ortografiskt" : "perspektiv") + ")");
        }
    }

    // ORTOGRAFISKT IGNORERAR frustumet — också ett ogiltigt.
    {
        gfx::View3D v;
        v.eye             = gfx::Vec3d{5.0, 6.0, 40.0};
        v.forward         = gfx::Vec3d{0.0, 0.0, -1.0};
        v.up              = gfx::Vec3d{0.0, 1.0, 0.0};
        v.projection      = gfx::Projection3D::Orthographic;
        v.orthoHalfHeight = 15.0;
        v.nearZ           = 1.0;
        v.farZ            = 100.0;
        v.box             = gfx::Rectf{0.0f, 0.0f, 640.0f, 480.0f};
        gfx::View3D g     = v;
        g.frustum         = gfx::FrustumTangents{2.0, -2.0, 5.0, 1.0};
        const gfx::Mat4d a = gfx::viewProjection(v, 0.0, 0.0, 0.0);
        const gfx::Mat4d b = gfx::viewProjection(g, 0.0, 0.0, 0.0);
        bool lika2 = true, någon = false;
        for (int i = 0; i < 16; ++i) {
            lika2 = lika2 && a.m[i] == b.m[i];
            någon = någon || a.m[i] != 0.0;
        }
        gfx::Ray3d ra, rb;
        const bool ok = gfx::rayThrough(g, 100.0, 100.0, rb) &&
                        gfx::rayThrough(v, 100.0, 100.0, ra);
        check(någon && lika2 && ok && len(v3(ra.dir) - v3(rb.dir)) == 0.0 &&
                  len(v3(ra.origin) - v3(rb.origin)) == 0.0,
              "frustum: ortografiskt ignoreras frustumet, också ett ogiltigt");
    }

    // OANVÄNDBARA FRUSTUM: left ≥ right, down ≥ up, icke-ändliga tal — vyn
    // ger nollmatrisen och falska svar. Ett användbart frustum behöver
    // inget giltigt fovY, och noll behöver inte ligga mellan tangenterna.
    {
        const double inf = std::numeric_limits<double>::infinity();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const gfx::FrustumTangents dåliga[] = {
            {0.5, 0.5, -1.0, 1.0},    // left == right
            {1.0, -1.0, -1.0, 1.0},   // left > right
            {-1.0, 1.0, 0.3, 0.3},    // down == up
            {-1.0, 1.0, 1.0, -1.0},   // down > up
            {nan, 1.0, -1.0, 1.0},    {-1.0, nan, -1.0, 1.0},
            {-1.0, 1.0, nan, 1.0},    {-1.0, 1.0, -1.0, nan},
            {-inf, 1.0, -1.0, 1.0},   {-1.0, inf, -1.0, 1.0},
            {-1.0, 1.0, -inf, 1.0},   {-1.0, 1.0, -1.0, inf},
        };
        int ogiltiga = 0, fel = 0;
        for (const gfx::FrustumTangents& t : dåliga) {
            ++ogiltiga;
            gfx::View3D v = frVy(kameror[0]);
            v.frustum     = t;
            const gfx::Mat4d m = gfx::viewProjection(v, 0.0, 0.0, 0.0);
            bool noll = true;
            for (const double e : m.m)
                noll = noll && e == 0.0;
            const gfx::FrameUniforms u = gfx::frameUniforms(v, 0.0, 0.0, 0.0);
            bool uNoll = u.rayCenter[0] == 0.0f && u.rayCenter[1] == 0.0f &&
                         u.rayCenter[2] == 0.0f;
            for (const float e : u.viewProj)
                uNoll = uNoll && e == 0.0f;
            double     px, py;
            gfx::Ray3d r;
            const bool ok =
                noll && uNoll && !gfx::project(v, kFrProv, px, py) &&
                !gfx::rayThrough(v, 400.0, 300.0, r) &&
                gfx::pxPerUnitAt(v, kFrProv) == 0.0 &&
                gfx::visiblePlaneRegion(
                    v, gfx::Plane3d{kFrProv, gfx::Vec3d{0.0, 1.0, 0.0}}, 0.0)
                    .empty();
            fel += ok ? 0 : 1;
        }
        check(fel == 0,
              "frustum: left ≥ right, down ≥ up och icke-ändliga tal ger en "
              "oanvändbar vy",
              std::to_string(fel) + " av " + std::to_string(ogiltiga) +
                  " användbara");

        gfx::View3D v = frVy(kameror[0]);
        v.fovY        = 0.0;   // ersatt av frustumet — påverkar inte användbarheten
        double px, py;
        check(gfx::project(v, kFrProv, px, py),
              "frustum: ett användbart frustum behöver inget giltigt fovY");
        v.nearZ = 0.0;
        check(!gfx::project(v, kFrProv, px, py),
              "frustum: närplanet 0 ger fortfarande en oanvändbar vy");
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== scene_math_gate — ritytans kamera ===\n");
    fallMatriser();
    fallFacit();
    fallTurOchRetur();
    fallOrthoMotPerspektiv();
    fallHorisonten();
    fallZoomMotPunkt();
    fallSkalan();
    fallRtc();
    fallTomVy();
    // 3D-vyn och 3D-kameran.
    fallMatriser3D();
    fallLikheten();
    fallKonventionen();
    fallMatchad();
    fallStrålen();
    fallSkalan3D();
    fallTumlingen();
    fallUppY();
    fallSynligt();
    fallRtc3D();
    fallVerktygen();
    // Scenens låda.
    fallScenensLåda();
    // Förstapersonen.
    fallFörstapersonen();
    // Det asymmetriska frustumet.
    fallFrustum();
    std::printf(g_fel ? "=== scene_math_gate: %d FEL av %d ===\n"
                      : "=== scene_math_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    return g_fel ? 1 : 0;
}
