#pragma once
// Geometribyggarna — världens former till scenens vertex.
// Ren CPU-kod utan backend: körs EN gång när innehållet läggs till, och
// resultatet laddas upp och ligger kvar. Linjerna och ytorna med
// hål och kantremsor; texten och markörerna bor i gfx/sdf.h.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "gfx/scene.h"

namespace gfx {

// En punkt i VÄRLDEN, i double — SWEREF-koordinater får inte avrundas förrän
// de blivit relativa ett chunk-origo.
struct Point2d {
    double x = 0.0;
    double y = 0.0;
};

// Taket för en chunk: högst så många vertex, så att varje index ryms i ett
// chunk-lokalt intervall och en chunk förblir en lagom bit att gallra.
inline constexpr std::size_t kMaxChunkVertices = 65536;

// En CHUNK: vertex och index som ritas med EN matris. `originX/originY` är
// chunkens origo i världen (double); varje vertex bär float(punkt − origo).
// Indexen är relativa chunkens egna vertex. Origot är chunkens FÖRSTA punkt —
// felet i en vertex är ungefär |punkt − origo| · 2⁻²⁴ världsenheter, så den
// som samlar linjer i samma chunk håller dem nära varandra (några km i
// SWEREF ger under en tiondels px vid 100 px/m).
struct LineChunk {
    double                     originX = 0.0;
    double                     originY = 0.0;
    std::vector<LineVertex>    vertices;
    std::vector<std::uint32_t> indices;
};

// Linjens ändar — raka (vid punkten), runda (en halvskiva) eller fyrkantiga
// (förlängda halva bredden). Samma form får strecken.
enum class LineCap : std::uint8_t { Butt, Round, Square };
// Skarvarna: runda, spetsiga (avfasade när spetsen blir längre än
// `SceneUniforms::miterLimit` × bredden) eller avfasade.
enum class LineJoin : std::uint8_t { Round, Miter, Bevel };

struct PolylineStyle {
    float    width = 1.0f;   // i linjens enhet — `SceneUniforms` säger vilken
    Color    color{};        // rak alfa
    LineCap  cap  = LineCap::Butt;
    LineJoin join = LineJoin::Round;
    // SLUTEN: linjen går tillbaka till sin första punkt, med en skarv där i
    // stället för två ändar (en ring, en ytas kontur).
    bool     closed = false;
};

// Bygger polylinjen och LÄGGER TILL den i `chunks`: i den sista chunken så
// länge den ryms, annars i en ny med eget origo. Per segment fyra vertex
// (en quad), per skarv en bit av skarvens slag (`LineKind` Round/Miter/
// Bevel — i varje inre punkt, och för en sluten linje också i den första),
// och vid runda ändar en rund bit i varje ände; raka och fyrkantiga ändar
// är segmentens egna (`LineVertex::ends`). `dist` är linjens längd fram till
// punkten, räknad i double och fortsatt över chunkgränser. Upprepade punkter
// hoppas över (ett segment utan längd har ingen riktning), liksom punkter som
// inte är ändliga; färre än två kvar ger ingenting. En sluten linje tappar
// en upprepad slutpunkt och kräver tre punkter — med två blir den öppen. En
// bit (fyra vertex) delas aldrig mellan två chunkar.
void buildPolyline(std::span<const Point2d> points, const PolylineStyle& style,
                   std::vector<LineChunk>& chunks);

// STRECKMÖNSTRET till `SceneDash` (gfx/scene.h): längderna på, av, på, av …
// i mönstrets enhet, och förskjutningen. SVG:s regler: ett UDDA antal
// upprepas en gång ([5, 3, 2] är [5, 3, 2, 5, 3, 2]); ett tomt mönster, ett
// negativt eller icke-ändligt värde eller summan 0 betyder HELDRAGEN (svaret
// true, `count` 0). Fler än `kMaxDashes` efter upprepningen ryms inte i
// uniformerna — svaret är false och `out` heldragen; anroparen loggar.
// `perWorldUnit` lämnas åt anroparen (den är kamerans).
bool makeDash(std::span<const float> pattern, float offset, SceneDash& out);

// ---- YTORNA — trianguleras EN gång i världen.

// En chunk ytor: samma kontrakt som `LineChunk` (origo i double, vertex
// relativa det, index relativa chunkens egna vertex), med ytans vertex.
struct FillChunk {
    double                     originX = 0.0;
    double                     originY = 0.0;
    std::vector<FillVertex>    vertices;
    std::vector<std::uint32_t> indices;
};

struct PolygonStyle {
    Color color{};   // rak alfa (en gradient ligger i rit-anropets uniformer)
};

// Bygger ytan och LÄGGER TILL den i `chunks`. `rings[0]` är ytterringen och
// resten HÅL; orienteringen spelar ingen roll, en upprepad slutpunkt tas bort
// och upprepade och icke-ändliga punkter hoppas över (som linjens). En ring
// med färre än tre punkter kvar hoppas över — utan ytterring blir det
// ingenting, och det är inget fel.
//
// Ytan trianguleras med den egna öronklippningen (earcut.cpp) och får en
// kantremsa per kant (FillVertex). Den läggs i den sista chunken om den ryms
// där, annars i en ny med origo i ytterringens första punkt; en yta som inte
// ryms ens i en tom chunk delas mellan flera med samma origo.
//
// Svaret är false när ringarna inte är en enkel yta med hål — en
// självkorsning eller ett hål utanför ytterringen. Ytan byggs ändå (det som
// fastnade i trianguleringen stängs med en solfjäder, så inget uteblir) och
// det loggas EN gång per episod: en lyckad yta efteråt avslutar episoden.
bool buildPolygon(std::span<const std::vector<Point2d>> rings,
                  const PolygonStyle& style, std::vector<FillChunk>& chunks);

// ---- BILDEN PÅ FYRA FRIA HÖRN — Image-programmets
// projektiva vikter (`ImageVertex::q`, gfx/scene.h).

// Hörnens PROJEKTIVA VIKTER för en fyrhörning i RINGORDNING (övre vänstra,
// övre högra, nedre högra, nedre vänstra — eller spegelvänt). Programmet
// interpolerar (u·q, v·q, q) och delar per fragment; de tre är affina i
// världens xy över HELA fyrhörningen (homografins invers i homogen form), så
// varje triangulering blir exakt. Vikterna kommer ur diagonalernas skärning
// p0 + t(p2 − p0) = p1 + s(p3 − p1): q0 = 1/(1 − t), q2 = 1/t, q1 = 1/(1 − s),
// q3 = 1/s, normerade så att den största är 1 — en rektangel och en
// parallellogram ger fyra ettor. false när fyrhörningen inte är STRIKT
// konvex: tre hörn i linje, en fluga, en inbuktning, ingen yta eller ett hörn
// som inte är ändligt (t eller s utanför (ε, 1 − ε), diagonalerna parallella)
// — `q` lämnas då orörd.
bool quadWeights(const Point2d (&ring)[4], float (&q)[4]);

// ---- GRADIENTERNA — färgen i Fill- och Line-programmen
// (`ScenePaint`, gfx/scene.h).

struct GradientStop {
    float offset = 0.0f;   // 0–1 längs gradienten
    Color color{};         // rak alfa
};

// Rampens bredd i texlar: texel i bär t = i / 255, och programmen samplar
// den linjärt i texelcentrum — t = 0 och t = 1 ger ändstoppens färg exakt.
inline constexpr int kGradientRampSize = 256;
using GradientRamp = std::array<std::uint8_t, kGradientRampSize * 4>;

// Stoppen till rampen (RGBA8, RAK alfa — `Renderer::createImage(256, 1,
// ImageFormat::Rgba8, …)` laddar upp den). Mellan två stopp interpoleras
// PREMULTIPLICERAT — en övergång till genomskinligt mörknar inte på vägen —
// och före det första och efter det sista gäller ändfärgen. Stoppen sorteras
// på offset STABILT (två stopp på samma offset ger en skarp gräns), offset
// klampas till 0–1 och ett icke-ändligt hoppas över; inga stopp ger en helt
// genomskinlig ramp.
void buildGradientRamp(std::span<const GradientStop> stops, GradientRamp& out);

// Gradientens geometri för EN chunk: världspunkterna minus chunkens origo,
// räknat i double — samma RTC-steg som matrisen, så en gradient över en
// SWEREF-yta ligger på rätt plats i varje chunk.
ScenePaint linearPaint(Point2d from, Point2d to, double originX,
                       double originY);
ScenePaint radialPaint(Point2d center, double r0, double r1, double originX,
                       double originY);

} // namespace gfx
