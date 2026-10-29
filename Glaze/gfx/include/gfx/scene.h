#pragma once
// Scenens typer — det ritytan laddar upp till GPU:n EN gång och ritar om med
// en ny kamera. Ligger BREDVID UI-pipelinen och rör den inte:
// UI:t batchar sina 56-bytes vertex varje frame, scenen äger buffertar som
// lever mellan frames och ritas med `Renderer::drawMesh`.
//
// VERTEXFORMATEN ÄR KONTRAKT. De läses byte för byte av tre backendars
// input-layout (D3D11), attributpekare (GL) och vertexdeskriptor (Metal);
// `static_assert`:arna nedan låser storleken och offseten så att ett fält
// som flyttas blir ett kompileringsfel och inte tyst förskjutna attribut.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "gfx/color.h"
#include "gfx/program.h"   // egna program: ShaderProgram, VertexLayout,
                           // FrameUniforms

namespace gfx {

class Image;   // gfx/image.h — `MeshDraw::image`

// Programmet en mesh ritas med. Uppräkningen är FAST — nya program läggs SIST.
// En backend som får ett program den saknar loggar och ritar ingenting.
// `Line3` och `Glyph3` är Line- och Glyph-programmen
// kompilerade med GLAZE_POS3 — samma källa, punkterna och ankarna i xyz
// (`LineVertex3`, `GlyphVertex3` nedan).
// `Custom` står SIST: ett EGET program (gfx/program.h) i
// `MeshDraw::shader`, med strömmarnas form i `MeshDraw::layout`.
// DE SKÄRMVÄNDA HÖRNENS DJUP (markdjupet): Line i skärmläget,
// Line3 och Glyph (utom liggande, `glyphflag::kGroundAligned`) extruderar i
// skärmen kring en punkt — och ett hörn får djupet av PLANET GENOM PUNKTEN
// PARALLELLT MED XY-PLANET där det ligger närmare än punktens eget (annars
// punktens). Det som står på ett plan (ritytans markdjup z = 0) skärs då
// aldrig av det. Draget framåt är en ANDEL av punktens djup: helt
// upp till 1/4, inget från 1/2 — ett plan som nästan går genom ögat (punkter
// vid skärmens horisont) drar aldrig hörnet mot närplanet. Priset är skrivet:
// den nedre delen kan dras fram ungefär en fjärdedel av sitt djup, mot det
// som ligger UNDER planet genom punkten — en mur som reser sig genom planet
// strax framför kan alltså skymma den ofullständigt i nedre kanten, och nära
// horisonten förlorar en markör på marken draget och skärs av markdjupet.
// Förspelets `glaze_standingZ` (gfx/program.h); glazes Points-program
// (src/canvas/programs3d.cpp) har dessutom ett tak på fyra radier i världen.
enum class MeshProgram : std::uint8_t { Line, Fill, Glyph, Image, Line3, Glyph3,
                                        Custom };

// Vad en linjevertex ÄR — Line-programmets vertexshader väljer gren på det.
//   SegmentStart  segmentets början; riktningen är mot `next`.
//   SegmentEnd    segmentets slut; riktningen är från `prev`.
//   Round         en rund bit (skarv eller ände): en skiva med halva
//                 bredden som radie, centrerad i punkten. `side` är då
//                 HÖRNET 0–3 i quaden som bär den (bit 0 = +x, bit 1 = +y);
//                 skivan är rund åt alla håll, så quadens vridning spelar
//                 ingen roll.
//   Miter, Bevel  en SPETSIG respektive AVFASAD skarv: kilen mellan två
//                 segments raka ändar på skarvens YTTERSIDA. Ytan och
//                 dess form räknas i vertexshadern ur de PROJICERADE
//                 grannarna (prev → punkt → next), så skarven är rätt även
//                 under tilt; `side` är hörnet 0–3 — bakom punkten, ut längs
//                 det inkommande segmentets normal, ut längs det
//                 utgåendes, och spetsen. En spets som blir längre än
//                 `SceneUniforms::miterLimit` × bredden blir avfasad (SVG).
// Segmenten är raka quads med raka ändar; skarvbitarna täcker kilen i
// varje skarv, de runda bitarna också runda ändar. Raka och fyrkantiga ÄNDAR
// är segmentens egna (`lineflag`).
enum class LineKind : std::uint8_t { SegmentStart = 0, SegmentEnd = 1, Round = 2,
                                     Miter = 3, Bevel = 4 };

// Segmentets ÄNDAR (`LineVertex::ends`) — samma byte i segmentets fyra
// vertex, så båda ändarna vet om båda (varje vertex räknar avståndet till
// BÅDA ändarnas kanter, och det måste vara samma linjära funktion i alla
// fyra hörnen).
namespace lineflag {
// Linjens ände: 0 rak, 1 rund (en Round-bit täcker den), 2 fyrkantig
// (segmentet förlängs halva bredden). Gäller också STRECKENS ändar.
inline constexpr std::uint8_t kCapMask   = 0x3u;
inline constexpr std::uint8_t kCapRound  = 1u;
inline constexpr std::uint8_t kCapSquare = 2u;
// Segmentets början/slut är LINJENS ände (ingen skarv där) — en rak eller
// fyrkantig ände förlängs och kantutjämnas längs linjen.
inline constexpr std::uint8_t kStartsLine = 1u << 2;
inline constexpr std::uint8_t kEndsLine   = 1u << 3;
} // namespace lineflag

// Linjens vertex — 40 byte. Punkterna är CHUNK-LOKALA: världen minus
// chunkens origo, i float (RTC). Varje vertex bär hela sanningen om sin
// plats i linjen — föregående och nästa punkt — även när slaget bara läser
// den ena; en punkt utan granne bär sig själv.
struct LineVertex {
    float x = 0.0f, y = 0.0f;           // punkten
    float prevX = 0.0f, prevY = 0.0f;   // föregående punkt (sig själv vid start)
    float nextX = 0.0f, nextY = 0.0f;   // nästa punkt (sig själv vid slut)
    float dist  = 0.0f;                 // linjens längd fram till punkten,
                                        // världsenheter (strecken)
    float width = 0.0f;                 // bredden i linjens enhet, se
                                        // `SceneUniforms`
    Color color{};                      // rak alfa
    std::int8_t   side = 0;             // −1/+1 sidan; hörn 0–3 för bitarna
    std::uint8_t  kind = 0;             // LineKind
    std::uint8_t  ends = 0;             // lineflag (segmentens ändar)
    std::uint8_t  pad  = 0;
};
static_assert(sizeof(LineVertex) == 40, "LineVertex är ett 40-bytes kontrakt");
static_assert(offsetof(LineVertex, prevX) == 8 &&
                  offsetof(LineVertex, nextX) == 16 &&
                  offsetof(LineVertex, dist) == 24 &&
                  offsetof(LineVertex, width) == 28 &&
                  offsetof(LineVertex, color) == 32 &&
                  offsetof(LineVertex, side) == 36 &&
                  offsetof(LineVertex, kind) == 37 &&
                  offsetof(LineVertex, ends) == 38,
              "LineVertex-fälten läses på fasta offset av tre backends");

// LINJENS VERTEX I RYMDEN (`MeshProgram::Line3`) — 52 byte.
// Samma fält som `LineVertex` med punkterna i xyz, chunk-lokala mot chunkens
// origo i xyz (`buildPolyline3`, gfx/geom3d.h). Programmet är Line-programmet
// kompilerat med GLAZE_POS3: bandet extruderas alltid i SKÄRMEN, mot kameran
// — en linje i rymden ligger inte i något plan att extrudera i. En bredd i
// VÄRLDSENHETER (`WidthSpace::World`) räknas om till px per vertex med den
// lokala skalan där (`gfx::pxPerUnitAt(View3D)`, ur matrisen), så skarvarna,
// ändarna och strecken är skärmlägets. `dist` är 3D-längden.
struct LineVertex3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;               // punkten
    float prevX = 0.0f, prevY = 0.0f, prevZ = 0.0f;   // föregående punkt
    float nextX = 0.0f, nextY = 0.0f, nextZ = 0.0f;   // nästa punkt
    float dist  = 0.0f;                 // linjens längd fram till punkten
    float width = 0.0f;                 // bredden i linjens enhet
    Color color{};                      // rak alfa
    std::int8_t   side = 0;             // som LineVertex
    std::uint8_t  kind = 0;             // LineKind
    std::uint8_t  ends = 0;             // lineflag
    std::uint8_t  pad  = 0;
};
static_assert(sizeof(LineVertex3) == 52, "LineVertex3 är ett 52-bytes kontrakt");
static_assert(offsetof(LineVertex3, prevX) == 12 &&
                  offsetof(LineVertex3, nextX) == 24 &&
                  offsetof(LineVertex3, dist) == 36 &&
                  offsetof(LineVertex3, width) == 40 &&
                  offsetof(LineVertex3, color) == 44 &&
                  offsetof(LineVertex3, side) == 48 &&
                  offsetof(LineVertex3, kind) == 49 &&
                  offsetof(LineVertex3, ends) == 50,
              "LineVertex3-fälten läses på fasta offset av tre backends");

// YTANS VERTEX — 32 byte. En yta är trianglar över
// ringarnas punkter PLUS en KANTREMSA längs varje kant: en quad per kant med
// fyra egna vertex, som vertexshadern extruderar 1 fysisk px UTÅT i skärmen
// (kanten förblir 1 px vid varje zoom; priset är ≤ 0,5 px uppblåsning).
// Varje vertex bär därför sin punkt och ringens grannar, som linjens, och
// trianglarna i ytans inre använder kantquadarnas inre vertex i kantens
// början — `buildPolygon` (gfx/geom2d.h) bygger det.
//
// Ytan ligger alltid till VÄNSTER om kanten prev → punkt → next med y uppåt
// (byggaren vänder ringarna), så UTÅT är höger — i skärmen speglat när
// kameran speglar (yUp), vilket programmet läser ur matrisen.
//   EdgeStart  kantens början; kanten går mot `next`, grannkanten in från
//              `prev` ger hörnets geringsriktning.
//   EdgeEnd    kantens slut; kanten kommer från `prev`, grannkanten ut mot
//              `next`.
enum class FillKind : std::uint8_t { EdgeStart = 0, EdgeEnd = 1 };

struct FillVertex {
    float x = 0.0f, y = 0.0f;           // punkten (chunk-lokal)
    float prevX = 0.0f, prevY = 0.0f;   // föregående punkt i ringen
    float nextX = 0.0f, nextY = 0.0f;   // nästa punkt i ringen
    Color color{};                      // rak alfa
    std::uint8_t  kind = 0;             // FillKind
    std::uint8_t  side = 0;             // 0 = på kanten (täckning 1), 1 =
                                        // remsans yttre kant (täckning 0)
    std::uint16_t pad  = 0;
};
static_assert(sizeof(FillVertex) == 32, "FillVertex är ett 32-bytes kontrakt");
static_assert(offsetof(FillVertex, prevX) == 8 &&
                  offsetof(FillVertex, nextX) == 16 &&
                  offsetof(FillVertex, color) == 24 &&
                  offsetof(FillVertex, kind) == 28 &&
                  offsetof(FillVertex, side) == 29,
              "FillVertex-fälten läses på fasta offset av tre backends");

// GLYFENS VERTEX — 52 byte. Text och markörer är
// quads som står UPPRÄTTA på skärmen (billboards): varje hörn bär etikettens
// ANKARE i världen (chunk-lokalt) och sin förskjutning från det i EM, och
// vertexshadern lägger förskjutningen i SKÄRMEN — i px eller mm, vriden efter
// skärmen eller efter kartan. Samma vertex bär glyfer ur SDF-atlasen
// (gfx/sdf.h) och FORMER som avståndsfält i shadern (platta, cirkel,
// kvadrat). `buildText`/`buildMarkers` bygger dem.
enum class GlyphShape : std::uint8_t { Glyph = 0, Plate = 1, Circle = 2,
                                       Square = 3 };

namespace glyphflag {
inline constexpr std::uint32_t kShapeMask = 0x3u;      // GlyphShape
// Vinkeln är kartans: baslinjen följer världsriktningen (cos, sin) så som
// kameran projicerar den vid ankaret — annars skärmens (moturs, y nedåt).
inline constexpr std::uint32_t kMapAligned = 1u << 2;
// Atlasens font saknar fet: tröskeln flyttas utåt (syntetisk fet).
inline constexpr std::uint32_t kSyntheticBold = 1u << 3;
// Glorians pass: fältet utvidgat med `haloWidth`, ritat i `halo` — en
// etiketts alla glorior ligger FÖRE dess fyllningar, så en glyfs gloria
// aldrig täcker grannens bokstav.
inline constexpr std::uint32_t kHaloPass = 1u << 4;
// En BILDMARKÖR: quaden samplar rit-anropets bild (`MeshDraw::image`, RGBA,
// med dess premultiplikation, spegling och format) i u, v = 0–1 i stället för
// ett avståndsfält, och programmet ger den BILDENS proportioner — quaden är
// en kvadrat i em, och den kortare axeln krymper kring ankaret. Formbitarna
// är 0 (ingen kantväxt).
inline constexpr std::uint32_t kImage = 1u << 5;
// LIGGANDE (`TextRotation::Ground`): quaden LIGGER i
// världsplanet vid ankaret i stället för att stå upprätt. Baslinjen är
// världsriktningen (cos, sin, 0) — vinkeln är kartans — och "nedåt" dess
// vinkelräta i planet, vänd så att baslinjen och nedåt bildar skärmens ram:
// texten blir aldrig spegelvänd (en karta vriden 180° visar den upp och
// ner, som tryckt på marken); en em är `size` × `pxPerWidthUnit` px VID
// ANKARET mätt
// vinkelrätt mot blicken (skalan k ur matrisen där: blicken är w-raden,
// ortografiskt djupraden), så quaden förkortas längs blicken som allt annat
// på marken. Kantutjämningen räknas med axlarnas geometriska medel vid
// ankaret. Glyph och Glyph3 i alla tre backends; `kMapAligned` läses inte
// för en liggande quad.
inline constexpr std::uint32_t kGroundAligned = 1u << 6;
} // namespace glyphflag

struct GlyphVertex {
    float ax = 0.0f, ay = 0.0f;   // etikettens ankare (chunk-lokalt)
    float ox = 0.0f, oy = 0.0f;   // hörnets förskjutning från ankaret i EM
                                  // (x höger, y NED), före vridningen
    float u = 0.0f, v = 0.0f;     // glyf: atlas-UV; form: hörnets läge
                                  // relativt formens MITT, i em
    Color color{};                // fyllningen (rak alfa)
    Color halo{};                 // glorian / formens kontur (rak alfa)
    float size      = 0.0f;       // em i anropets enhet — × `pxPerWidthUnit`
                                  // = fysiska px (px: uiScale, mm: pxPerMm),
                                  // klampad till anropets glyphMin/MaxPx
    float angle     = 0.0f;       // radianer, moturs (skärmen eller kartan)
    float haloWidth = 0.0f;       // glorians/konturens bredd, samma enhet som
                                  // size; 0 = ingen
    std::uint32_t flags = 0;      // glyphflag
    // ETIKETTENS INDEX — vertexshadern läser dess synlighet i
    // `MeshDraw::labels` på texeln `MeshDraw::labelBase + label`. En etikett
    // (alla dess quadar) delar ett index; byggarna tar det ur stilens `label`.
    std::uint32_t label = 0;
};
static_assert(sizeof(GlyphVertex) == 52, "GlyphVertex är ett 52-bytes kontrakt");
static_assert(offsetof(GlyphVertex, ox) == 8 && offsetof(GlyphVertex, u) == 16 &&
                  offsetof(GlyphVertex, color) == 24 &&
                  offsetof(GlyphVertex, halo) == 28 &&
                  offsetof(GlyphVertex, size) == 32 &&
                  offsetof(GlyphVertex, angle) == 36 &&
                  offsetof(GlyphVertex, haloWidth) == 40 &&
                  offsetof(GlyphVertex, flags) == 44 &&
                  offsetof(GlyphVertex, label) == 48,
              "GlyphVertex-fälten läses på fasta offset av tre backends");

// GLYFENS VERTEX I RYMDEN (`MeshProgram::Glyph3`) — 56
// byte. Samma fält som `GlyphVertex` med ANKARET i xyz (chunk-lokalt mot
// chunkens origo i xyz); quaden står upprätt i skärmen vid ankarets
// projektion, på ankarets djup. Programmet är Glyph-programmet kompilerat med
// GLAZE_POS3. `buildText`/`buildMarkers` med `Vec3d`-ankare bygger dem
// (gfx/sdf.h).
struct GlyphVertex3 {
    float ax = 0.0f, ay = 0.0f, az = 0.0f;   // etikettens ankare
    float ox = 0.0f, oy = 0.0f;   // hörnets förskjutning i EM (som GlyphVertex)
    float u = 0.0f, v = 0.0f;     // atlas-UV, eller formens läge i em
    Color color{};
    Color halo{};
    float size      = 0.0f;
    float angle     = 0.0f;
    float haloWidth = 0.0f;
    std::uint32_t flags = 0;      // glyphflag
    std::uint32_t label = 0;      // etikettens index i bygget
};
static_assert(sizeof(GlyphVertex3) == 56,
              "GlyphVertex3 är ett 56-bytes kontrakt");
static_assert(offsetof(GlyphVertex3, ox) == 12 &&
                  offsetof(GlyphVertex3, u) == 20 &&
                  offsetof(GlyphVertex3, color) == 28 &&
                  offsetof(GlyphVertex3, halo) == 32 &&
                  offsetof(GlyphVertex3, size) == 36 &&
                  offsetof(GlyphVertex3, angle) == 40 &&
                  offsetof(GlyphVertex3, haloWidth) == 44 &&
                  offsetof(GlyphVertex3, flags) == 48 &&
                  offsetof(GlyphVertex3, label) == 52,
              "GlyphVertex3-fälten läses på fasta offset av tre backends");

// Vad en buffert bär. Index är ALLTID 32 bitar (R32).
enum class BufferKind : std::uint8_t { Vertex, Index };

// En buffert på GPU:n — skapad av sin Renderer (`createBuffer`) och, som
// fonter och bilder, bunden till den: släpp bufferten INNAN fönstret rivs.
// Innehållet ligger kvar mellan frames och ändras bara med `updateBuffer`.
// Fabriken returnerar alltid ett objekt; `valid()` avgör.
class GpuBuffer {
public:
    virtual ~GpuBuffer() = default;

    virtual BufferKind  kind() const  = 0;
    virtual std::size_t size() const  = 0;   // byte
    virtual bool        valid() const = 0;
};

// En del av en buffert. En statisk buffert ritas som `slice(buf)`; en
// TRANSIENT del (`Renderer::uploadTransient`) pekar in i renderarens egen
// ring och gäller bara till framens slut.
struct BufferSlice {
    const GpuBuffer* buffer = nullptr;
    std::size_t      offset = 0;   // byte från buffertens början
    std::size_t      bytes  = 0;

    bool valid() const
    {
        return buffer && buffer->valid() && bytes > 0 &&
               offset + bytes <= buffer->size();
    }
};

// Hela bufferten som en del.
inline BufferSlice slice(const GpuBuffer& b)
{
    return BufferSlice{&b, 0, b.size()};
}

// Var linjens bredd bor.
//   Screen  bredden är konstant på SKÄRMEN: attributet × `pxPerWidthUnit`
//           fysiska px, extruderat i vertexshadern EFTER projektionen — samma
//           bredd vid varje zoom, bäring och tilt.
//   World   bredden är i VÄRLDSENHETER och extruderas i planet FÖRE
//           projektionen — linjen växer med zoomen och smalnar mot horisonten.
enum class WidthSpace : std::uint8_t { Screen, World };

// FÄRGEN — hur Fill- och Line-programmen färgar det de
// ritar. Solid tar vertexfärgen rakt av; en GRADIENT multiplicerar den med
// RAMPEN (`MeshDraw::image`: 256 × 1 RGBA8 ur `buildGradientRamp`,
// gfx/geom2d.h) i den punkt t som räknas PER FRAGMENT ur världspunkten — så
// gradienten ligger fast i världen under zoom, vridning och tilt. Spridningen
// är PAD: t klampas till 0–1, och bortom ändarna gäller ändfärgerna.
enum class PaintKind : std::uint8_t { Solid, Linear, Radial };

struct ScenePaint {
    PaintKind kind = PaintKind::Solid;
    // CHUNK-LOKALA, som vertexen (världen minus chunkens origo — samma
    // subtraktion i double som matrisen): Linear går från (x0, y0) till
    // (x1, y1); Radial har mitten (x0, y0) och går från radien r0 till r1.
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    float r0 = 0.0f, r1 = 0.0f;
};

// STRECKEN (Line-programmet) — mönstret i SKÄRMRYMD: läget
// längs linjen är vertexens `dist` (världsenheter, perspektivkorrekt
// interpolerat) × `perWorldUnit`, så ett mönster i px eller mm mäts på
// skärmen (`perWorldUnit` = kamerans fysiska px per världsenhet i MITTEN
// delat med enhetens px) och ett i världsenheter följer zoomen
// (`perWorldUnit` = 1). SKRIVEN BEGRÄNSNING: under tilt förkortas strecket
// med djupet — det är skalan i mitten som gäller överallt, och längre bort
// täcker en världsenhet färre px. Strecken har linjens ändform (rak,
// rund, fyrkantig), som i SVG.
inline constexpr int kMaxDashes = 8;
struct SceneDash {
    int   count = 0;   // 0 = heldragen; annars JÄMNT, 2–kMaxDashes
    // De KUMULATIVA sluten i mönstrets enhet: på, av, på, av … — ends[0] är
    // första strecket, ends[count − 1] perioden. `makeDash` (gfx/geom2d.h)
    // bygger dem ur ett mönster.
    float ends[kMaxDashes] = {};
    float offset       = 0.0f;   // mönstret börjar så långt in (enheten)
    float perWorldUnit = 1.0f;   // mönsterenheter per världsenhet längs linjen
};

// KLIPPET (glaze `Painter::save(Rect)`) — en KONVEX yta i det BUNDNA
// målets FYSISKA px (origo uppe till vänster, samma rymd som klippstacken),
// skuren ur halvplan: fragmentet i pixelcentrum p ritas där VARJE plans
// `nx·p.x + ny·p.y + d` ≥ 0. Med (nx, ny) av längd 1 är värdet avståndet i
// px, och kanten kantutjämnas över en px som klippmaskerna (täckningen är
// närmaste plans värde + ½); där täckningen är 0 kastas fragmentet — det
// varken färgar eller skriver djup. Line, Fill, Glyph, Image och 3-varianterna
// läser det, i fragmentsteget och med EN funktion i backendens förspel; de
// egna programmen inte. `count` 0 = inget klipp, och planen efter `count`
// läses inte.
inline constexpr int kMaxClipPlanes = 8;
struct ClipPlane {
    float nx = 0.0f, ny = 0.0f;   // normalen INÅT
    float d   = 0.0f;
    float pad = 0.0f;             // float4 i alla tre språken
};
static_assert(sizeof(ClipPlane) == 16, "ClipPlane läses som en float4");

struct SceneClip {
    int       count = 0;   // 0 = inget klipp; annars 1–kMaxClipPlanes plan
    ClipPlane planes[kMaxClipPlanes];
};

// Samma klipp: samma antal och samma AKTIVA plan — backendarnas cache frågar
// så, och ett plan efter `count` räknas inte.
inline bool operator==(const SceneClip& a, const SceneClip& b)
{
    if (a.count != b.count)
        return false;
    for (int i = 0; i < a.count && i < kMaxClipPlanes; ++i) {
        const ClipPlane& p = a.planes[i];
        const ClipPlane& q = b.planes[i];
        if (p.nx != q.nx || p.ny != q.ny || p.d != q.d)
            return false;
    }
    return true;
}

// Per rit-anrop: kameran för just den här chunken och linjebreddens enhet.
struct SceneUniforms {
    // Chunk-lokalt → pixelklipp: `viewProjection(vy, origoX, origoY)
    // .toFloat(...)` med CHUNKENS origo (gfx/camera.h).
    float viewProj[16] = {};
    // Screen: fysiska px per breddenhet. För px är det uiScale. För MM är det
    // skärmens EFFEKTIVA täthet — `DisplayMetrics::pxPerMm` med appens
    // kalibrering pålagd, alltså `glaze::Ctx::pxPerMm` — så att en millimeter
    // mäter en millimeter med linjal, oberoende av OS- och användarskalan.
    // Ingen egen mm-gren i programmet: enheten ÄR den här faktorn.
    // Ignoreras i World.
    float      pxPerWidthUnit = 1.0f;
    WidthSpace widthSpace     = WidthSpace::Screen;
    // Hela anropets OPACITET (0–1, klampas) — multiplicerar det ritades alfa,
    // som `Renderer::drawImage`:s. Ett lagers opacitet går hit.
    float      opacity        = 1.0f;
    // Fill- och Line-programmens färg (se `ScenePaint`); en gradient kräver
    // sin ramp i `MeshDraw::image`. Övriga program ignorerar den.
    ScenePaint paint;
    // LINE-PROGRAMMET: spetsgränsen — en spetsig skarv vars spets blir
    // längre än `miterLimit` × linjens bredd blir avfasad (SVG:s
    // `stroke-miterlimit`, förval 4) — och strecken. Övriga program
    // ignorerar dem.
    float     miterLimit = 4.0f;
    SceneDash dash;
    // GLYPH-PROGRAMMET (Glyph och Glyph3): en em på skärmen —
    // `size` × `pxPerWidthUnit` — klampas till [glyphMinPx, glyphMaxPx]
    // FYSISKA px innan något läser den (formens tillväxt, bildmarkören, den
    // liggande texten); golvet först, sedan taket, så taket vinner när de
    // korsas. 0 = ingen gräns. Övriga program ignorerar dem.
    float glyphMinPx = 0.0f;
    float glyphMaxPx = 0.0f;
    // IMAGE-PROGRAMMET: FÄRGJUSTERINGEN av bildens RAKA färg, före
    // opaciteten och klippet — ljusheten (färgen × talet: 0 svart), kontrasten
    // (sträckt kring mittgrått 0,5: 0 grått) och mättnaden (blandad mot
    // luminansen, Rec. 709 på de gammakodade talen: 0 grått), i den ordningen
    // och klampad till 0–1 efter varje steg. 1 lämnar färgen orörd, över 1
    // förstärker. Ett tal under 0 räknas som 0 och ett som inte är ändligt som
    // 1 (`adjustFactor`), och förvalen ritar bilden EXAKT som utan dem. Samma
    // funktion i alla tre språken — de egna programmen har den i förspelet
    // (`glaze_colorAdjust`, gfx/program.h). Övriga program ignorerar dem.
    float saturation = 1.0f;
    float contrast   = 1.0f;
    float brightness = 1.0f;
    // KLIPPET (se `SceneClip`) — Line, Fill, Glyph, Image och
    // 3-varianterna; förvalet klipper ingenting. `meshDrawOk` avvisar ett
    // antal utanför 0–kMaxClipPlanes och ett aktivt plan som inte är ändligt.
    SceneClip clip;
};

// FÄRGJUSTERINGENS TAL (`SceneUniforms::saturation`, `contrast` och
// `brightness`) som programmen får dem: under 0 är 0, och ett tal som inte är
// ändligt är 1 — orört. Ritytans stilar (glaze `ImageStyle` och `MeshStyle`)
// går samma väg.
inline float adjustFactor(float v)
{
    return std::isfinite(v) ? (v > 0.0f ? v : 0.0f) : 1.0f;
}

// RENDERLÄGET — djupet, blandningen och gallringen för ETT
// rit-anrop (`MeshDraw::state`). FÖRVALET ÄR 2D:S BETEENDE: inget djup, rak
// alfa och ingen gallring, så inget 2D-anrop ändras. glaze-lagrets eget
// renderläge har appens 3D-förval (Less + skrivning); det här är fasadens.
//
// DJUPTESTET jämför fragmentets djup med målets (standard-Z: rensat till 1,
// närmare är mindre — gfx/image.h `TargetDesc`). `Off` är INGET djup alls:
// varken test eller skrivning, och `depthWrite` gäller inte. Den som vill
// skriva utan att jämföra tar `Always`. I ett mål UTAN djup verkar djupet
// inte (som Off), och det loggas en gång.
enum class DepthTest : std::uint8_t { Off, Less, LessEqual, Greater,
                                      GreaterEqual, Equal, Always };

// BLANDNINGEN mot det målet redan bär (som är premultiplicerat):
//   Alpha          rak alfa — färg · a + mål · (1 − a), alfan
//                  a + måletsAlfa · (1 − a).
//   Premultiplied  programmets färg ÄR premultiplicerad: färg + mål · (1 − a).
//   Additive       färg · a adderas till målet, och alfan adderas (mättad vid
//                  1) — ett sken som syns också över en genomskinlig yta.
//   Opaque         ingen blandning: färg och alfa skrivs rakt av. För
//                  ogenomskinligt innehåll, vars program ger alfa 1 — en
//                  lägre alfa lämnar ett mål som inte är premultiplicerat.
//   DepthOnly      färgen skrivs INTE alls; bara djupet, enligt `depth` och
//                  `depthWrite` (ett markplan som döljer det som ligger under).
enum class Blend : std::uint8_t { Alpha, Premultiplied, Additive, Opaque,
                                  DepthOnly };

// GALLRINGEN. Framsidan är den triangel som är MOTURS SETT FRÅN KAMERAN — så
// som den syns i målet, efter projektionen — i alla tre backends. En speglad
// matris vänder alltså framsidan.
enum class Cull : std::uint8_t { None, Back, Front };

struct RenderState {
    DepthTest depth      = DepthTest::Off;
    bool      depthWrite = false;   // skriv fragmentets djup (inte med Off)
    // DJUPBIASEN flyttar fragmentets djup — negativt är NÄRMARE (en kontur som
    // ska vinna över sin yta). `depthBias` i djupformatets minsta steg vid
    // primitivens djup (D32F: 2^(e − 23), e = exponenten för dess största z;
    // D3D11 tar ett heltal, så den avrundas där) och `slopeBias` × primitivens
    // största djuplutning. Verkar bara med ett djuptest.
    float     depthBias  = 0.0f;
    float     slopeBias  = 0.0f;
    Blend     blend      = Blend::Alpha;
    Cull      cull       = Cull::None;
};

// Ett rit-anrop: `indexCount` index ur `indices` (uint32, relativa
// `vertices` början) som trianglar.
struct MeshDraw {
    MeshProgram   program = MeshProgram::Line;
    BufferSlice   vertices;
    BufferSlice   indices;
    std::uint32_t indexCount = 0;
    SceneUniforms uniforms;
    // Bilden programmet samplar — Image-programmets bild, med sitt
    // filter, format, sin premultiplikation och sin v-spegling. Ur SAMMA
    // renderer som anropet. Program som inte samplar ignorerar den. Fill och
    // Line samplar den som gradientens RAMP (bara när `uniforms.paint` är en
    // gradient) och Glyph som SDF-atlasens sida — de två tar
    // alltid linjär sampling, oavsett bildens filter.
    const Image*  image = nullptr;
    // ETIKETTERNAS SYNLIGHET (bara Glyph): en R8-bild med EN TEXEL
    // PER ETIKETT, rad för rad (index i → kolumn i % bredden, rad i /
    // bredden), som vertexshadern läser för varje hörn på texeln `labelBase +
    // GlyphVertex::label`: 0 = dold (quaden gallras), 255 = synlig, och
    // däremellan tonas etiketten (in- och uttoning). nullptr = alla synliga.
    // Basen gör vertexens index — som är dess bygges — till bildens; den är
    // exakt i shadern upp till 2²⁴ etiketter. Ur SAMMA renderer som anropet.
    const Image*  labels    = nullptr;
    std::uint32_t labelBase = 0;
    // RENDERLÄGET — se `RenderState`. Förvalet är 2D:s
    // beteende; alla fyra programmen tar det.
    RenderState   state;

    // ---- EGNA PROGRAM (`MeshProgram::Custom`) — de
    // inbyggda programmen läser inget av fälten nedan. Kontrakten (förspelet,
    // platserna, blocken) står i gfx/program.h.
    //
    // INDEXERAT när `indexCount` > 0, som de inbyggda; annars `vertexCount`
    // vertex i ordning ur `vertices` (tre per triangel). Ingen vertexström
    // alls (en TOM layout) är helskärmspassets: programmet gör sina vertex ur
    // SV_VertexID/gl_VertexID/[[vertex_id]].
    std::uint32_t vertexCount = 0;
    // INSTANSDELEN: `instanceCount` > 0 ritar så många instanser, och
    // layoutens ström 1 läses ur `instances` FRÅN DELENS OFFSET — delens
    // första element är instans 0 för programmet (SV_InstanceID,
    // gl_InstanceID, [[instance_id]]). Ingen baseInstance: GL 3.3 saknar den,
    // och offseten är samma väg i alla tre. 0 = inte
    // instansierat; en layout med instansattribut kräver > 0, och delen ska
    // rymma `instanceCount` hela instanser.
    BufferSlice   instances;
    std::uint32_t instanceCount = 0;
    // Programmet (`Renderer::createProgram`, ur SAMMA renderer) — giltigt,
    // annars ritas anropet inte. Heter `shader` och inte `program`: det
    // namnet bär redan uppräkningen ovan.
    const ShaderProgram* shader = nullptr;
    // Strömmarnas form för just det här anropet — krävs (en tom layout för
    // helskärmspasset).
    const VertexLayout*  layout = nullptr;
    // GlazeFrame — kamerans block. `target` skriver backenden med det BUNDNA
    // målets storlek; resten är anroparens (`frameUniforms`, gfx/camera.h).
    FrameUniforms        frame{};
    // GlazeUser — exakt `shader->userBytes()` byte (std140), eller inget när
    // programmet saknar blocket.
    const void*          user      = nullptr;
    std::uint32_t        userBytes = 0;
    // Texturerna på platserna 0 … `shader->textures()` − 1 (alla måste
    // finnas, och platserna efter är tomma), ur SAMMA renderer. Samplas med
    // varje bilds eget filter.
    std::array<const Image*, kMaxProgramTextures> textures{};
};

// BILDENS VERTEX — 20 byte: en punkt i världen
// (CHUNK-LOKAL, som linjens), var i bilden den samplar (u, v i 0–1, rad 0
// överst — bildens egen form; ett render-mål med `flippedV` speglas av
// programmet, inte av vertexen) och hörnets
// PROJEKTIVA VIKT `q`. En platta är fyra vertex och sex index. Programmet
// interpolerar (u·q, v·q, q) PERSPEKTIVKORREKT — vertexens riktiga w går
// vidare till rasteriseringen, så en platta under tilt samplas där
// världspunkten faktiskt ligger — och delar per fragment: med vikterna ur
// `quadWeights` (gfx/geom2d.h) är de tre affina i världens xy över HELA
// fyrhörningen, så en bild på fyra fria hörn blir exakt projektiv utan knäck
// längs diagonalen. En rektangel och en parallellogram har q = 1 i alla fyra
// (förvalet — ett aggregat med fyra värden får det).
struct ImageVertex {
    float x = 0.0f, y = 0.0f;   // punkten
    float u = 0.0f, v = 0.0f;   // bildkoordinaten
    float q = 1.0f;             // den projektiva vikten
};
static_assert(sizeof(ImageVertex) == 20, "ImageVertex är ett 20-bytes kontrakt");
static_assert(offsetof(ImageVertex, u) == 8 && offsetof(ImageVertex, q) == 16,
              "ImageVertex-fälten läses på fasta offset av tre backends");

} // namespace gfx
