#pragma once
// 3D-GEOMETRIN — de genererade meshparna och den plana trianguleringen
// och LINJERNA I RYMDEN. Ren CPU-kod utan
// backend, som geom2d.h. Generatorerna har inget vertexformat: svaret är RENA
// DATA — lägen, normaler och trianglar — som glaze gör en `Mesh` av (origo,
// float, strömmarna). Linjerna (`buildPolyline3`) bygger däremot
// Line3-programmets vertex (`LineVertex3`, gfx/scene.h), som geom2d.h:s
// `buildPolyline` bygger Line-programmets.
//
// VÄRLDEN är högerhänt med z UPP (gfx/camera.h). FRAMSIDAN är den sida en
// triangel går MOTURS sett ifrån — samma regel som `Cull` — och varje
// generator vänder framsidan UTÅT: normalen i varje läge pekar bort från
// kroppen, och en sluten form har positiv volym.

#include <cstdint>
#include <span>
#include <vector>

#include "gfx/camera.h"
#include "gfx/geom2d.h"   // PolylineStyle, kMaxChunkVertices
#include "gfx/scene.h"    // LineVertex3

namespace gfx {

// ---- LINJERNA I RYMDEN (`MeshProgram::Line3`)

// En chunk linjer i rymden: `LineChunk`s kontrakt (geom2d.h) med origot i xyz
// — varje vertex bär float(punkt − origo) i alla tre axlarna (RTC).
struct LineChunk3 {
    double                     originX = 0.0;
    double                     originY = 0.0;
    double                     originZ = 0.0;
    std::vector<LineVertex3>   vertices;
    std::vector<std::uint32_t> indices;
};

// Polylinjen i rymden — SAMMA bitar och regler som `buildPolyline` (geom2d.h,
// läs dess kontrakt: segmenten, skarvarna, ändarna, den slutna linjen, en bit
// delas aldrig mellan två chunkar), med punkterna i xyz och `dist` som
// 3D-längden fram till punkten, räknad i double. En ny chunk får origo i
// bitens punkt. Bredden är i linjens enhet (`SceneUniforms`): px och mm är
// skärmens, världsenheter räknas om per vertex i programmet (scene.h
// `LineVertex3`).
void buildPolyline3(std::span<const Vec3d> points, const PolylineStyle& style,
                    std::vector<LineChunk3>& chunks);

// En genererad mesh. `positions` i VÄRLDEN (double — en SWEREF-koordinat får
// inte avrundas förrän den blivit relativ ett origo, och det origot väljer
// konsumenten), en ENHETSNORMAL per läge och trianglarna som index-tripplar.
//
// Generatorerna LÄGGER TILL: indexen förskjuts med antalet lägen som redan
// fanns, så flera former blir EN mesh (ett rit-anrop) — en stolpe av en
// cylinder och en låda, alla faser i ett spann som rör.
struct MeshGeometry {
    std::vector<Vec3d>         positions;
    std::vector<Vec3d>         normals;
    std::vector<std::uint32_t> indices;
};

// LÅDAN mellan hörnen `a` och `b` (min och max får byta plats per axel): sex
// sidor med fyra EGNA lägen var — sidans normal i varje hörn, så kanten är
// skarp — alltså 24 lägen och 12 trianglar. En låda utan utsträckning längs
// en axel ger sidor utan area; ett hörn som inte är ändligt ger ingenting.
void box(const Vec3d& a, const Vec3d& b, MeshGeometry& out);

// SFÄREN kring `center` med radien `radius`: en UV-sfär med `slices`
// meridianer (minst 3) och `stacks` breddgrader (minst 2) och EN punkt i
// varje pol, på ±z. Normalen är radiell (mjuk). 2 + slices · (stacks − 1)
// lägen och 2 · slices · (stacks − 1) trianglar. Förvalet är en UV-sfär
// med 24 × 16. En radie ≤ 0 eller något som inte är ändligt ger
// ingenting.
inline constexpr int kSphereSlices = 24;
inline constexpr int kSphereStacks = 16;
void sphere(const Vec3d& center, double radius, MeshGeometry& out,
            int slices = kSphereSlices, int stacks = kSphereStacks);

// CYLINDERN från `base` till `top` med radien `radius` och `sides` sidor
// (minst 3). Manteln har radiella normaler och delar sina lägen mellan
// grannsidorna (rund runt om); de två locken har egna lägen med axelns
// normal (kanten är skarp). 4 · sides lägen och 4 · sides − 4 trianglar.
// Läge 0 i varje ring ligger åt den världsaxel som står mest vinkelrätt mot
// axeln (x för en lodrät cylinder), och vinkeln växer moturs sett från
// `top`. En axel utan längd, en radie ≤ 0 eller något som inte är ändligt
// ger ingenting.
inline constexpr int kCylinderSides = 16;
void cylinder(const Vec3d& base, const Vec3d& top, double radius,
              MeshGeometry& out, int sides = kCylinderSides);

// RÖRET längs vägen `path` med diametern `diameter` och `sides` sidor
// (minst 3). En ring i varje punkt, i planet som
// HALVERAR knät (geringen): manteln är rak mellan punkterna och sidorna möts
// i knät utan glapp, och ramen förs vidare med den minsta vridningen, så
// röret aldrig snor sig. Ringens lägen delas av de två grannsegmenten med en
// radiell normal mitt emellan deras (mjukt i knät). Lock i båda ändar — röret
// är slutet.
//
// Ett knä skarpare än `kTubeMiterLimit` (geringen längre än så många gånger
// radien — SVG:s spetsgräns, ≈ 151°) delar röret: biten före slutar med en
// rät ring och ett lock, och en ny börjar i punkten. Varje bit är sluten.
// Upprepade och icke-ändliga punkter hoppas över; färre än två kvar, en
// diameter ≤ 0 eller en som inte är ändlig ger ingenting. Ett kort segment i
// ett skarpt knä (kortare än geringen) viker innerkanten — gränsen för en
// geringsfog.
//
// Utan delning: (punkter + 2) · sides lägen och 2 · sides · (punkter − 1) +
// 2 · (sides − 2) trianglar.
inline constexpr int    kTubeSides      = 8;
inline constexpr double kTubeMiterLimit = 4.0;
void tube(std::span<const Vec3d> path, double diameter, MeshGeometry& out,
          int sides = kTubeSides);

// POLYGONEN I ETT PLAN, med hål. `rings[0]` är ytterringen och resten HÅL —
// samma kontrakt som `buildPolygon` (geom2d.h): en upprepad slutpunkt tas
// bort, upprepade och icke-ändliga punkter hoppas över, en ring med färre än
// tre punkter eller utan area hoppas över, och utan ytterring blir det
// ingenting — inget fel.
//
// PLANET är ytterringens, med Newells normal: FRAMSIDAN är den sida
// ytterringen går MOTURS sett ifrån, varje läge får den normalen och varje
// triangel går moturs kring den. Ringarna projiceras på planet och
// trianguleras med den egna öronklippningen (earcut.cpp), men lägena är
// INDATANS punkter — en nästan plan ring (avrundningsbrus) ritas där den
// ligger. Ett hål går åt vilket håll som helst. En ring utan tecknad area
// (en symmetrisk fluga) får planet ur sin största triangel, och framsidan
// är då godtycklig.
//
// Svaret är false när ringarna inte är en enkel yta i planet — en
// självkorsning eller ett hål utanför. Ytan byggs ändå (det som fastnar
// stängs med en solfjäder, så inget uteblir) och det loggas EN gång per
// episod: en lyckad yta efteråt avslutar episoden.
bool triangulatePlanar(std::span<const std::vector<Vec3d>> rings,
                       MeshGeometry&                       out);

} // namespace gfx
