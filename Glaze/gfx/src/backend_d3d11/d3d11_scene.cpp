// Scenens del av D3dRenderer — GPU-buffertarna, den
// transienta ringen och Line-programmet (och dess Line3-variant — källan
// med GLAZE_POS3). Lever BREDVID UI-pipelinen i
// d3d11_renderer.cpp och rör inte dess tillstånd mer än nödvändigt:
//   * cbuffer b2 är scenens; b0 (UI:ts viewport) och b1 (klippmaskerna) är
//     orörda, och b1 läses av Line-programmets pixelshader precis som av
//     UI:ts — en rundad mask gäller ritytan per konstruktion. b5 är målarens
//     klipp (`bindSceneClip`), läst av scenens fyra program.
//   * `drawMesh` flushar UI-batchen FÖRST (målarordningen) och lämnar sitt
//     program bundet; `flush()` återbinder UI-pipelinen när den ritar nästa
//     gång (`pipeline_`).
//   * Scissorn är rasteriserarens — samma tillstånd för båda programmen.
//
// Index är alltid R32 (`DrawIndexed`). HLSL kompileras i runtime som UI:ts.

#include <climits>
#include <cstring>

#include "d3d11_backend.h"
#include "gfx/platform.h"

namespace gfx {
namespace {

// Line-programmet. Klippmaskernas del (`kHlslClipSrc`) och färgens
// (`kHlslPaintSrc`, d3d11_fill.cpp — scenens b2: gParams .xy målets storlek,
// .z px per breddenhet, .w 1 = bredden i världen; gStroke och gDash
// konturens spetsgräns och streck) klistras FÖRE. b0 och b1 är
// UI-pipelinens och rörs inte.
//
// FORMEN ÄR ETT TECKNAT AVSTÅND i fragmentshadern — positivt inne, i px i
// skärmläget och i världsenheter i världsläget — och täckningen är avståndet
// + ½ (i världsläget delat med en px i världsenheter, `fwidth`). Segmentet
// mäter sidledes mot halva bredden och längs linjen mot ändarnas kanter;
// en skarv är snittet av de två segmentens band (spetsig), också begränsat
// av fasen (avfasad); en rund bit avståndet till punkten. STRECKEN är ett
// tecknat avstånd längs linjen i mönstrets enhet, gjort till px med
// skärmderivatan — och de får linjens ändform, som i SVG.
//
// LINE3 är SAMMA källa kompilerad med GLAZE_POS3:
// punkterna i xyz (`LineVertex3`), och bandet extruderas ALLTID i skärmen —
// skärmlägets gren, med bredden i världen omräknad till px per vertex ur den
// lokala skalan (`pxPerWorldAt`) och halva bredden interpolerad, för
// segmentets två ändar kan ha olika bredd under perspektiv. Ett fragment
// utanför bandet kastas, så kantutjämningens marginal inte skriver djup.
const char kLineShaderSrc[] = R"hlsl(

#ifdef GLAZE_POS3
typedef float3 LinePos;
#else
typedef float2 LinePos;
#endif

struct LineIn
{
    LinePos pos  : POSITION;
    LinePos prev : TEXCOORD0;
    LinePos next : TEXCOORD1;
    float  width : TEXCOORD2;
    float  dist  : TEXCOORD4;   // linjens längd fram till punkten (världen)
    float4 col   : COLOR0;
    int4   sk    : TEXCOORD3;   // .x sidan (−1/+1) eller hörnet (0–3),
                                // .y LineKind, .z ändarna (lineflag)
};

struct LineOut
{
    float4 pos : SV_Position;
    float4 col : COLOR0;
    // SKÄRMLÄGET (px): segmentet (0, sidled från mittlinjen) och bitarna
    // fragmentet relativt punkten. NOPERSPECTIVE med flit — extruderingen är
    // gjord i skärmen, så avståndet är linjärt i SKÄRMEN; perspektivkorrekt
    // interpolering hade gjort linjen tjockare och tunnare längs sin längd
    // under tilt.
    noperspective float2 local : TEXCOORD0;
    // VÄRLDSLÄGET (världsenheter): samma två, extruderat i planet och
    // därför perspektivkorrekt.
    float2 world : TEXCOORD1;
    // Halva bredden (px / världsenheter) — platt; i Line3 linjär i skärmen.
#ifdef GLAZE_POS3
    noperspective float hw : TEXCOORD2;
#else
    nointerpolation float hw : TEXCOORD2;
#endif
    // Världspunkten för en gradient: mittlinjens punkt i
    // skärmläget, den extruderade i världsläget — perspektivkorrekt.
    float2 ppos : TEXCOORD3;
    // ÄNDARNA: tecknat avstånd bortom början- och slutkanten (+ utanför),
    // −kFar där segmentet inte slutar linjen. Linjära i skärmen respektive
    // i världen, som `local`/`world`.
    noperspective float2 endsS : TEXCOORD4;
    float2 endsW : TEXCOORD5;
    // STRECKEN: linjens längd fram till fragmentet, världsenheter —
    // perspektivkorrekt, för den är linjär i världen.
    float dist : TEXCOORD6;
    // SKARVEN: utåtnormalerna (in .xy, ut .zw), bisektrisen .xy och fasens
    // avstånd från punkten .z (kFar = spetsig).
    nointerpolation float4 jn : TEXCOORD7;
    nointerpolation float4 jm : TEXCOORD8;
    nointerpolation int2   kc : TEXCOORD9;   // LineKind, ändformen
};

static const int kSegmentStart = 0;
static const int kSegmentEnd   = 1;
static const int kRound        = 2;
static const int kMiter        = 3;
static const int kCapRound     = 1;
static const int kCapSquare    = 2;
static const int kStartsLine   = 4;
static const int kEndsLine     = 8;
// "Ingen kant här" — långt utanför varje avstånd shadern jämför med.
static const float kFar = 1.0e4;

#ifdef GLAZE_POS3
float4 toClip(float3 p) { return mul(gViewProj, float4(p, 1.0)); }
#else
float4 toClip(float2 p) { return mul(gViewProj, float4(p, 0.0, 1.0)); }
#endif

// Pixelklipp → målets px. Ett w ≤ 0 ligger bakom kameran, och linjeprogrammen
// kastar det själva (trimSegment, de runda bitarna) innan det används.
float2 toPx(float4 c) { return c.xy / max(c.w, 1e-6); }

// Pixelklipp → D3D:s klipprymd.
float4 toNdc(float4 c)
{
    return float4(c.x * 2.0 / gParams.x - c.w, c.w - c.y * 2.0 / gParams.y,
                  c.z, c.w);
}

// a flyttad längs a→b till närplanet (klippdjup 0).
float4 toNear(float4 a, float4 b) { return lerp(a, b, a.z / (a.z - b.z)); }

// SEGMENTET INNANFÖR KLIPPYTAN. GPU:n får aldrig klippa
// ett linjeband: klippningen interpolerar de skärmlinjära (noperspective)
// värdena olika i olika implementationer — llvmpipe snedvrider bandet när
// ändarnas w skiljer mycket. Ändarna flyttas därför längs
// segmentet till närplanet, det bortre planet (3D-målets är ändligt;
// kartkamerans ligger i oändligheten, w − z är konstant, och nås aldrig) och
// en SKYDDSRAM en målstorlek utanför målets kanter, i den ordningen. Varje
// plan flyttar den ände som ligger utanför med `lerp(utanför, innanför, …)`
// i båda ändarnas vertex, så paret blir bit för bit detsamma (närplanets
// trimning är samma uttryck för båda ändarna). `moved` är den egna ändens
// flytt i världen mot grannen, `span` sträckan kvar mellan ändarna.
// BÅDA ÄNDARNA UTANFÖR SAMMA PLAN ⇒ `outside`: segmentet syns inte, och
// anroparen kastar det INNAN något mer räknas. Bakom närplanet kan w vara
// negativt, och förlängningen nedan har då ett NEGATIVT tak (`tMax`) som
// vinner över noll — annars skjuts hörnet längs segmentet förbi ögat och
// hamnar FRAMFÖR det: linorna bakom kameran syns som en solfjäder genom
// flyktpunkten (scene3d_pixel_gate L).
void trimPlane(float fc, float fn, inout float4 c, inout float4 nb,
               inout float span, inout float moved, inout bool outside)
{
    if (fc < 0.0 && fn < 0.0) {
        outside = true;
    } else if (fc < 0.0 && fn >= 0.0) {
        const float t = fc / (fc - fn);
        c      = lerp(c, nb, t);
        moved += t * span;
        span  *= 1.0 - t;
    } else if (fn < 0.0 && fc >= 0.0) {
        const float t = fn / (fn - fc);
        nb    = lerp(nb, c, t);
        span *= 1.0 - t;
    }
}

float trimSegment(inout float4 c, inout float4 nb, float span, out bool outside)
{
    const float g = max(gParams.x, gParams.y);
    const float r = gParams.x + g, b = gParams.y + g;
    float moved = 0.0;
    outside = false;
    trimPlane(c.z, nb.z, c, nb, span, moved, outside);
    trimPlane(c.w - c.z, nb.w - nb.z, c, nb, span, moved, outside);
    trimPlane(c.x + g * c.w, nb.x + g * nb.w, c, nb, span, moved, outside);
    trimPlane(r * c.w - c.x, r * nb.w - nb.x, c, nb, span, moved, outside);
    trimPlane(c.y + g * c.w, nb.y + g * nb.w, c, nb, span, moved, outside);
    trimPlane(b * c.w - c.y, b * nb.w - nb.y, c, nb, span, moved, outside);
    return moved;
}

// Flytten c + dc·t KORTAD så att punkten stannar innanför klippdjupet
// [0, w] — ändarnas förlängning, av samma skäl.
float keepInside(float4 c, float4 dc, float t)
{
    const float dz = dc.z * t;
    const float df = (dc.w - dc.z) * t;
    float k = 1.0;
    if (dz < 0.0)
        k = min(k, max(c.z, 0.0) / -dz);
    if (df < 0.0)
        k = min(k, max(c.w - c.z, 0.0) / -df);
    return t * k;
}

float2 cornerOf(int i)
{
    return float2((i & 1) ? 1.0 : -1.0, (i & 2) ? 1.0 : -1.0);
}

float2 perpOf(float2 d) { return float2(-d.y, d.x); }

float2 unitOr(float2 d, float2 fallback)
{
    const float l = length(d);
    return l > 1e-6 ? d / l : fallback;
}

// SKARVENS RAM i ett plan (skärmen eller världen): ur in- och utriktningen
// utåtnormalerna (på YTTERSIDAN — mot vridningen), bisektrisen m och
// c = cos θ/2 = nA · m. En vändning (in ≈ −ut) har ingen bisektris: den får
// inriktningen och c = 0, och skarven blir platt i punkten.
void joinFrame(float2 dA, float2 dB, out float2 nA, out float2 nB,
               out float2 m, out float c)
{
    const float s = (dA.x * dB.y - dA.y * dB.x) >= 0.0 ? -1.0 : 1.0;
    nA = s * perpOf(dA);
    nB = s * perpOf(dB);
    const float2 mv = nA + nB;
    const float  ml = length(mv);
    m = ml < 1e-3 ? dA : mv / ml;
    c = ml < 1e-3 ? 0.0 : dot(nA, m);
}

// Skarvbitens hörn: bakom punkten, ut längs in- och utnormalen och spetsen
// på bisektrisen — quaden rymmer skarvens HELA kantband (ext = halva
// bredden + kantutjämningens marginal). Spetsig: där de två ytterkanterna
// möts, ext / c. Avfasad: så långt att fasens nollkant ryms i hörnet där
// den möter segmentets, max(ext · c, ext / (2c + 1)).
float2 jointCorner(int corner, float2 nA, float2 nB, float2 m, float c,
                   float ext, bool miter)
{
    const float tip = miter ? ext / max(c, 1e-6)
                            : max(ext * c, ext / (2.0 * c + 1.0));
    return corner == 0 ? -m * ext
         : corner == 1 ? nA * ext
         : corner == 2 ? nB * ext
                       : m * tip;
}

#ifndef GLAZE_POS3

// En fysisk px i världsenheter VID punkten, längs riktningen d.
float worldPx(float2 p, float2 d, float hw)
{
    const float  l = max(hw, 1e-6);
    const float  k = length(toPx(toClip(p + d * l)) - toPx(toClip(p))) / l;
    return k > 0.0 ? 1.0 / k : 0.0;
}

LineOut lineVs(LineIn i)
{
    LineOut o;
    o.col   = i.col;
    o.dist  = i.dist;
    o.local = float2(0.0, 0.0);
    o.world = float2(0.0, 0.0);
    o.endsS = float2(-kFar, -kFar);
    o.endsW = float2(-kFar, -kFar);
    o.jn    = float4(0.0, 0.0, 0.0, 0.0);
    o.jm    = float4(0.0, 0.0, kFar, 0.0);
    const int  side    = i.sk.x;
    const int  kind    = i.sk.y;
    const int  cap     = i.sk.z & 3;
    const bool segment = kind == kSegmentStart || kind == kSegmentEnd;
    o.kc = int2(kind, cap);
    // Linjens ÄNDAR som segmentet bär (rak eller fyrkantig — en rund ände
    // är en egen bit): båda ändarna, för varje vertex räknar båda kanterna.
    const bool  startCap = segment && (i.sk.z & kStartsLine) != 0 && cap != kCapRound;
    const bool  endCap   = segment && (i.sk.z & kEndsLine) != 0 && cap != kCapRound;
    const bool  isStart  = kind == kSegmentStart;
    const float miterLim = gStroke.x;

    if (gParams.w > 0.5) {
        // BREDDEN I VÄRLDEN: extruderingen i planet FÖRE projektionen — linjen
        // följer zoomen och smalnar mot horisonten. Kantutjämningens marginal
        // är en px, uttryckt i världsenheter VID punkten.
        const float hw = 0.5 * i.width;
        o.hw   = hw;
        float2 off;
        if (segment) {
            const float2 d   = unitOr(isStart ? i.next - i.pos : i.pos - i.prev,
                                      float2(1.0, 0.0));
            const float  len = length(isStart ? i.next - i.pos : i.pos - i.prev);
            const float  px  = worldPx(i.pos, perpOf(d), hw);
            const float  ext = hw + px;
            const float  capExt = cap == kCapSquare ? hw : 0.0;
            const float  sExt   = startCap ? capExt + px : 0.0;
            const float  eExt   = endCap ? capExt + px : 0.0;
            // Läget längs segmentet från början; förlängningen hör till den
            // här vertexens ände.
            const float along = isStart ? -sExt : len + eExt;
            off     = perpOf(d) * (float(side) * ext) + d * (isStart ? -sExt : eExt);
            o.world = float2(0.0, float(side) * ext);
            o.endsW = float2(startCap ? -capExt - along : -kFar,
                             endCap ? along - len - capExt : -kFar);
            o.dist  = i.dist + (isStart ? -sExt : eExt);
        } else if (kind == kRound) {
            const float2 dir = cornerOf(side);
            const float  ext = hw + worldPx(i.pos, normalize(dir), hw);
            off     = dir * ext;
            o.world = off;
        } else {
            float2 nA, nB, m;
            float  c;
            const float2 dA = unitOr(i.pos - i.prev, unitOr(i.next - i.pos, float2(1.0, 0.0)));
            const float2 dB = unitOr(i.next - i.pos, dA);
            joinFrame(dA, dB, nA, nB, m, c);
            const float ext   = hw + worldPx(i.pos, nA, hw);
            const bool  miter = kind == kMiter && c * miterLim >= 1.0;
            off     = jointCorner(side, nA, nB, m, c, ext, miter);
            o.world = off;
            o.jn    = float4(nA, nB);
            o.jm    = float4(m, miter ? kFar : hw * c, 0.0);
        }
        o.pos  = toNdc(toClip(i.pos + off));
        o.ppos = i.pos + off;
        return o;
    }

    // BREDDEN PÅ SKÄRMEN: punkten projiceras, extruderingen görs i px EFTER
    // projektionen — samma bredd vid varje zoom, bäring och tilt.
    const float hw  = 0.5 * i.width * gParams.z;
    const float ext = hw + 1.0;   // + kantutjämningens marginal
    o.hw   = hw;
    o.ppos = i.pos;
    float4 c = toClip(i.pos);
    float2 off;
    if (segment) {
        // Grannen som ger segmentets riktning: början → nästa, slutet ←
        // föregående.
        const float2 nbPos = isStart ? i.next : i.prev;
        const float2 segW  = isStart ? i.next - i.pos : i.pos - i.prev;
        float4 nb   = toClip(nbPos);
        // KLIPPYTAN: en ände utanför (bakom närplanet, bortom det bortre
        // planet eller skyddsramen) flyttas längs segmentet till planet, så
        // riktningen och läget blir den SYNLIGA delens — samma par i båda
        // ändarnas vertex, så längden nedan är densamma. Linjens längd och
        // mittpunkten flyttar med (trimSegment).
        bool        outside;
        const float moved = trimSegment(c, nb, length(segW), outside);
        if (outside) {
            o.pos = float4(0.0, 0.0, -2.0, 1.0);
            return o;
        }
        const float dist  = i.dist + (isStart ? moved : -moved);
        if (moved > 0.0)
            o.ppos = lerp(i.pos, nbPos, moved / length(segW));
        float2 d = toPx(nb) - toPx(c);
        if (!isStart)
            d = -d;                        // början → slut
        const float len = length(d);
        d = len > 1e-6 ? d / len : float2(1.0, 0.0);
        // ÄNDARNA: en rak ände förlängs kantutjämningens px, en fyrkantig
        // dessutom halva bredden — och avstånden till kanterna är linjära
        // längs segmentet, samma funktion i alla fyra hörnen.
        const float capExt = cap == kCapSquare ? hw : 0.0;
        const float sExt   = startCap ? capExt + 1.0 : 0.0;
        const float eExt   = endCap ? capExt + 1.0 : 0.0;
        const float along  = isStart ? -sExt : len + eExt;
        o.local = float2(0.0, float(side) * ext);
        o.endsS = float2(startCap ? -capExt - along : -kFar,
                         endCap ? along - len - capExt : -kFar);
        // FÖRLÄNGNINGEN görs i VÄRLDEN längs segmentet, med den lokala skalan
        // (perspektivdivisionens derivata) — så vertexen får sitt eget w och
        // `dist` hör ihop med det. Att bara flytta den i skärmen med det gamla
        // w hade sneddat den perspektivkorrekta interpoleringen längs hela
        // segmentet: vid den bortre änden under tilt är en px många
        // världsenheter. Klipprymden är linjär: clip(p + d·e) = c + M·d·e.
        const float4 dc   = mul(gViewProj, float4(unitOr(segW, float2(1.0, 0.0)), 0.0, 0.0));
        const float  pxW  = length((dc.xy * c.w - c.xy * dc.w) / max(c.w * c.w, 1e-12));
        const float  extW = keepInside(c, dc, (isStart ? -sExt : eExt) / max(pxW, 1e-6));
        c      = c + dc * extW;
        o.dist = dist + extW;
        off    = perpOf(d) * (float(side) * ext);
    } else if (kind == kRound) {
        // En rund skarv eller ände bakom närplanet syns inte (segmenten
        // trimmas där) — kastas som den spetsiga nedan.
        if (c.z < 0.0) {
            o.pos = float4(0.0, 0.0, -2.0, 1.0);
            return o;
        }
        off     = cornerOf(side) * ext;
        o.local = off;
    } else {
        // SKARVEN, räknad i SKÄRMEN ur de projicerade grannarna. Punkten
        // bakom närplanet: ingen skarv syns (segmenten trimmas där).
        if (c.z < 0.0) {
            o.pos = float4(0.0, 0.0, -2.0, 1.0);
            return o;
        }
        float4 ca = toClip(i.prev);
        float4 cb = toClip(i.next);
        if (ca.z < 0.0)
            ca = toNear(ca, c);
        if (cb.z < 0.0)
            cb = toNear(cb, c);
        const float2 pc = toPx(c);
        float2 nA, nB, m;
        float  cs;
        const float2 dA = unitOr(pc - toPx(ca), unitOr(toPx(cb) - pc, float2(1.0, 0.0)));
        const float2 dB = unitOr(toPx(cb) - pc, dA);
        joinFrame(dA, dB, nA, nB, m, cs);
        const bool miter = kind == kMiter && cs * miterLim >= 1.0;
        off     = jointCorner(side, nA, nB, m, cs, ext, miter);
        o.local = off;
        o.jn    = float4(nA, nB);
        o.jm    = float4(m, miter ? kFar : hw * cs, 0.0);
    }
    const float2 p = toPx(c) + off;
    o.pos = toNdc(float4(p * c.w, glaze_standingZ(gViewProj, c, off), c.w));
    return o;
}

#else   // GLAZE_POS3 — Line3

// SKÄRMENS px per världsenhet vid klipp-punkten c, för en sträcka vinkelrätt
// mot blicken: brännvidden — x-radens del vinkelrätt mot w-raden, som är
// blicken — genom djupet w. Samma tal som gfx::pxPerUnitAt(View3D), räknat ur
// matrisen (ortografiskt är w-raden noll och w = 1).
float pxPerWorldAt(float4 c)
{
    const float3 rx = gViewProj[0].xyz;
    const float3 rw = gViewProj[3].xyz;
    const float  ww = dot(rw, rw);
    const float3 s  = ww > 0.0 ? rx - rw * (dot(rx, rw) / ww) : rx;
    return length(s) / max(c.w, 1e-6);
}

float3 unitOr3(float3 d, float3 fallback)
{
    const float l = length(d);
    return l > 1e-6 ? d / l : fallback;
}

// Halva bredden i px vid klipp-punkten c: px-bredden rakt av, en bredd i
// världen med den lokala skalan där.
float halfWidthAt(float width, float4 c)
{
    return 0.5 * width * (gParams.w > 0.5 ? pxPerWorldAt(c) : gParams.z);
}

// Skärmlägets gren ovan, rad för rad, med punkterna i xyz. Bredden räknas
// EFTER trimningen mot klippytan (en ände bakom kameran får bredden där den
// synliga delen börjar), och ett segment räknar båda ändarnas — ändarnas
// avstånd ska vara samma linjära funktion i alla fyra hörnen. Ändarnas
// förlängning är EXAKT i skärmen (se den), inte 2D-grenens linjära.
LineOut lineVs(LineIn i)
{
    LineOut o;
    o.col   = i.col;
    o.dist  = i.dist;
    o.local = float2(0.0, 0.0);
    o.world = float2(0.0, 0.0);
    o.endsS = float2(-kFar, -kFar);
    o.endsW = float2(-kFar, -kFar);
    o.jn    = float4(0.0, 0.0, 0.0, 0.0);
    o.jm    = float4(0.0, 0.0, kFar, 0.0);
    const int  side    = i.sk.x;
    const int  kind    = i.sk.y;
    const int  cap     = i.sk.z & 3;
    const bool segment = kind == kSegmentStart || kind == kSegmentEnd;
    o.kc = int2(kind, cap);
    const bool  startCap = segment && (i.sk.z & kStartsLine) != 0 && cap != kCapRound;
    const bool  endCap   = segment && (i.sk.z & kEndsLine) != 0 && cap != kCapRound;
    const bool  isStart  = kind == kSegmentStart;
    const float miterLim = gStroke.x;

    o.ppos = i.pos.xy;
    float4 c = toClip(i.pos);
    float  hw;
    float2 off;
    if (segment) {
        const float3 nbPos = isStart ? i.next : i.prev;
        const float3 segW  = isStart ? i.next - i.pos : i.pos - i.prev;
        float4 nb   = toClip(nbPos);
        bool        outside;
        const float moved = trimSegment(c, nb, length(segW), outside);
        if (outside) {
            o.hw  = 0.0;
            o.pos = float4(0.0, 0.0, -2.0, 1.0);
            return o;
        }
        const float dist  = i.dist + (isStart ? moved : -moved);
        if (moved > 0.0)
            o.ppos = lerp(i.pos, nbPos, moved / length(segW)).xy;
        hw = halfWidthAt(i.width, c);
        const float hwNb = halfWidthAt(i.width, nb);
        const float hwS  = isStart ? hw : hwNb;
        const float hwE  = isStart ? hwNb : hw;
        const float ext  = hw + 1.0;
        float2 d = toPx(nb) - toPx(c);
        if (!isStart)
            d = -d;                        // början → slut
        const float len = length(d);
        d = len > 1e-6 ? d / len : float2(1.0, 0.0);
        const float capS  = cap == kCapSquare ? hwS : 0.0;
        const float capE  = cap == kCapSquare ? hwE : 0.0;
        const float sExt  = startCap ? capS + 1.0 : 0.0;
        const float eExt  = endCap ? capE + 1.0 : 0.0;
        const float along = isStart ? -sExt : len + eExt;
        o.local = float2(0.0, float(side) * ext);
        o.endsS = float2(startCap ? -capS - along : -kFar,
                         endCap ? along - len - capE : -kFar);
        // FÖRLÄNGNINGEN, EXAKT I SKÄRMEN: punkten flyttas längs segmentet i
        // världen (vertexen får sitt eget w och `dist` hör ihop med det) så
        // långt att projektionen flyttar e px längs skärmriktningen ud. Med
        // a = ud·c.xy, b = ud·dc.xy och q = dc.w ligger den efter t
        // världsenheter i (a + t·b)/(w + t·q), alltså t = e·w² / (lin − e·w·q)
        // med lin = b·w − a·q. 2D-grenens linjära förlängning (den lokala
        // skalan) är exakt bara utan perspektiv — i rymden kommer den bortre
        // änden till korta. En förlängning bortom flyktpunkten (nämnaren ≤ 0)
        // tar sikte på 1000 × djupet, ett segment längs blicken (lin ≈ 0)
        // förlängs inte, och flytten stannar innanför klippdjupet
        // (keepInside).
        const float  sgn  = isStart ? -1.0 : 1.0;
        const float  e    = isStart ? sExt : eExt;
        const float4 dc   = sgn * mul(gViewProj, float4(unitOr3(segW, float3(1.0, 0.0, 0.0)), 0.0));
        const float2 ud   = sgn * d;
        const float  lin  = dot(ud, dc.xy) * c.w - dot(ud, c.xy) * dc.w;
        const float  den  = lin - e * c.w * dc.w;
        const float  tMax = 1e3 * c.w / max(dc.w, 1e-6);
        const float  t    = keepInside(c, dc, lin > 1e-6 * c.w * c.w
                                ? (den > 0.0 ? min(e * c.w * c.w / den, tMax) : tMax)
                                : 0.0);
        c      = c + dc * t;
        o.dist = dist + sgn * t;
        off    = perpOf(d) * (float(side) * ext);
    } else {
        hw = halfWidthAt(i.width, c);
        const float ext = hw + 1.0;
        if (c.z < 0.0) {
            // Bakom närplanet: ingen skarv och ingen rund ände syns.
            o.hw  = hw;
            o.pos = float4(0.0, 0.0, -2.0, 1.0);
            return o;
        }
        if (kind == kRound) {
            off     = cornerOf(side) * ext;
            o.local = off;
        } else {
            float4 ca = toClip(i.prev);
            float4 cb = toClip(i.next);
            if (ca.z < 0.0)
                ca = toNear(ca, c);
            if (cb.z < 0.0)
                cb = toNear(cb, c);
            const float2 pc = toPx(c);
            float2 nA, nB, m;
            float  cs;
            const float2 dA = unitOr(pc - toPx(ca), unitOr(toPx(cb) - pc, float2(1.0, 0.0)));
            const float2 dB = unitOr(toPx(cb) - pc, dA);
            joinFrame(dA, dB, nA, nB, m, cs);
            const bool miter = kind == kMiter && cs * miterLim >= 1.0;
            off     = jointCorner(side, nA, nB, m, cs, ext, miter);
            o.local = off;
            o.jn    = float4(nA, nB);
            o.jm    = float4(m, miter ? kFar : hw * cs, 0.0);
        }
    }
    o.hw = hw;
    const float2 p = toPx(c) + off;
    o.pos = toNdc(float4(p * c.w, glaze_standingZ(gViewProj, c, off), c.w));
    return o;
}

#endif   // GLAZE_POS3

// Det KUMULATIVA slutet nummer k i mönstret (0 före det första).
float dashEnd(int k)
{
    return k < 0 ? 0.0 : gDash[k >> 2][k & 3];
}

// Det tecknade avståndet i MÖNSTRETS enhet från läget t till närmaste
// streck — positivt inne i ett streck. Varje streck prövas också en period
// före och efter, så mönstrets skarv inte syns.
float dashDistance(float t)
{
    const int   n = (int)(gStroke.y + 0.5);
    float       period = 0.0;
    [unroll] for (int e = 0; e < 8; ++e)
        period = e < n ? gDash[e >> 2][e & 3] : period;
    const float u    = t - period * floor(t / period);
    float       best = -kFar * 1e4;
    [unroll] for (int k = 0; k < 4; ++k) {
        if (2 * k < n) {
            const float lo = dashEnd(2 * k - 1);
            const float hi = dashEnd(2 * k);
            best = max(best, min(u - lo, hi - u));
            best = max(best, min(u - lo - period, hi + period - u));
            best = max(best, min(u - lo + period, hi - period - u));
        }
    }
    return best;
}

float4 linePs(LineOut i) : SV_Target
{
#ifdef GLAZE_POS3
    const bool world  = false;   // Line3 extruderar alltid i skärmen
#else
    const bool world  = gParams.w > 0.5;
#endif
    const bool dashed = gStroke.y > 0.5;
    const int  kind   = i.kc.x;
    const int  cap    = i.kc.y;
    // Mönstrets läge och dess skärmderivata — UTANFÖR varje gren (en
    // derivata får inte stå i en).
    const float t     = i.dist * gStroke.z + gStroke.w;
    const float tPerPx = length(float2(ddx(t), ddy(t)));

    const float hw = i.hw;
    float d;   // tecknat avstånd till kanten, + inne (px / världsenheter)
    if (kind == kSegmentStart || kind == kSegmentEnd) {
        const float  lat = world ? i.world.y : i.local.y;
        const float2 en  = world ? i.endsW : i.endsS;
        d = min(hw - abs(lat), min(-en.x, -en.y));
        if (dashed) {
            // Strecket längs linjen, i lägets enhet — och dess ände har
            // linjens form: rak, rund (avståndet till ändpunkten) eller
            // fyrkantig (halva bredden längre).
            const float dd    = dashDistance(t);
            const float along = world ? dd / gStroke.z : dd / max(tPerPx, 1e-12);
            if (cap == kCapRound)
                d = along >= 0.0 ? d : min(d, hw - length(float2(along, lat)));
            else
                d = min(d, along + (cap == kCapSquare ? hw : 0.0));
        }
    } else {
        const float2 r = world ? i.world : i.local;
        if (kind == kRound) {
            d = hw - length(r);
        } else {
            // Snittet av de två segmentens band (spetsig) — och fasen.
            d = min(hw - abs(dot(r, i.jn.xy)), hw - abs(dot(r, i.jn.zw)));
            d = min(d, i.jm.z - dot(r, i.jm.xy));
        }
        // En skarv eller rund ände i ett MELLANRUM ritas inte.
        if (dashed && dashDistance(t) < 0.0)
            d = -kFar;
    }
    // Täckningen: avståndet + ½ px — i världsläget delat med en px i
    // världsenheter.
    const float fw    = max(fwidth(d), 1e-6);
    const float alpha = world ? saturate(d / fw + 0.5) : saturate(d + 0.5);
    // Färgen (enfärgad eller gradient) och anropets opacitet; rak alfa som
    // UI:t, och klippmaskerna gäller ritytan som allt annat.
    const float4 col = i.col * paintColor(i.ppos);
#ifdef GLAZE_POS3
    // Utanför bandet (kantutjämningens marginal) skriver Line3 inget djup —
    // annars hade den osynliga kanten skymt det som ritas bakom.
    if (alpha <= 0.0)
        discard;
#endif
    // Målarens klipp sist (kHlslClipSrc) — utanför kastas fragmentet.
    return float4(col.rgb, col.a * alpha * gPaint1.y * clipCoverage(i.pos.xy) *
                               sceneClip(i.pos.xy));
}
)hlsl";

// Line3-varianten: källan ovan med GLAZE_POS3 definierad.
const D3D_SHADER_MACRO kPos3Defines[] = {{"GLAZE_POS3", "1"},
                                         {nullptr, nullptr}};

// Den transienta ringen: delarna läggs på 256-byte-gränser (säkert för
// alla tre backendarnas offset-krav) i en buffert som börjar på 1 MiB och
// dubblas när en frame behöver mer.
constexpr std::size_t kTransientAlign   = 256;
constexpr std::size_t kTransientInitial = std::size_t{1} << 20;

std::size_t alignUp(std::size_t v, std::size_t a)
{
    return (v + a - 1) / a * a;
}

UINT bindFlagsOf(BufferKind k)
{
    return k == BufferKind::Index ? D3D11_BIND_INDEX_BUFFER
                                  : D3D11_BIND_VERTEX_BUFFER;
}

} // namespace

// ---------------------------------------------------------------- program

bool D3dRenderer::initScene()
{
    const std::string src =
        std::string(kHlslClipSrc) + kHlslPaintSrc + kHlslStandSrc +
        kLineShaderSrc;
    ComPtr<ID3DBlob> vsb = compileHlsl(src, "lineVs", "vs_4_0");
    ComPtr<ID3DBlob> psb = compileHlsl(src, "linePs", "ps_4_0");

    // Layouten läser LineVertex på dess fasta offset (gfx/scene.h): sida,
    // slag, ändar och utfyllnad som R8G8B8A8_SINT (`pad` läses inte).
    const D3D11_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         offsetof(LineVertex, x), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
         offsetof(LineVertex, prevX), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0,
         offsetof(LineVertex, nextX), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 4, DXGI_FORMAT_R32_FLOAT, 0,
         offsetof(LineVertex, dist), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0,
         offsetof(LineVertex, width), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0,
         offsetof(LineVertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 3, DXGI_FORMAT_R8G8B8A8_SINT, 0,
         offsetof(LineVertex, side), D3D11_INPUT_PER_VERTEX_DATA, 0},
    };

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth      = sizeof(SceneCb);
    cbd.Usage          = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    // Målarens klipp i b5 — först inget klipp (nollor), som
    // `sceneClip_` säger.
    D3D11_BUFFER_DESC kbd = cbd;
    kbd.ByteWidth = sizeof(SceneClipCb);
    const SceneClipCb            ingetKlipp{};
    const D3D11_SUBRESOURCE_DATA kinit{&ingetKlipp, 0, 0};
    sceneClip_ = SceneClip{};

    const bool ok =
        vsb && psb &&
        SUCCEEDED(device_->CreateVertexShader(vsb->GetBufferPointer(),
                                              vsb->GetBufferSize(), nullptr,
                                              &lineVs_)) &&
        SUCCEEDED(device_->CreatePixelShader(psb->GetBufferPointer(),
                                             psb->GetBufferSize(), nullptr,
                                             &linePs_)) &&
        SUCCEEDED(device_->CreateInputLayout(il, 7, vsb->GetBufferPointer(),
                                             vsb->GetBufferSize(),
                                             &lineLayout_)) &&
        SUCCEEDED(device_->CreateBuffer(&cbd, nullptr, &sceneCbuf_)) &&
        SUCCEEDED(device_->CreateBuffer(&kbd, &kinit, &sceneClipCbuf_));
    if (!ok) {
        // UI:t lever vidare — det är bara ritytan som inte ritar.
        log("D3D11: Line-programmet kunde inte skapas — drawMesh ritar inget");
        shutdownScene();
        return false;
    }

    // LINE3: samma källa med GLAZE_POS3 och punkterna i
    // xyz — tre flyttal per punkt på `LineVertex3`s offset.
    ComPtr<ID3DBlob> vs3 = compileHlsl(src, "lineVs", "vs_4_0", kPos3Defines);
    ComPtr<ID3DBlob> ps3 = compileHlsl(src, "linePs", "ps_4_0", kPos3Defines);
    const D3D11_INPUT_ELEMENT_DESC il3[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         offsetof(LineVertex3, x), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         offsetof(LineVertex3, prevX), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0,
         offsetof(LineVertex3, nextX), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 4, DXGI_FORMAT_R32_FLOAT, 0,
         offsetof(LineVertex3, dist), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0,
         offsetof(LineVertex3, width), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0,
         offsetof(LineVertex3, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 3, DXGI_FORMAT_R8G8B8A8_SINT, 0,
         offsetof(LineVertex3, side), D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    const bool ok3 =
        vs3 && ps3 &&
        SUCCEEDED(device_->CreateVertexShader(vs3->GetBufferPointer(),
                                              vs3->GetBufferSize(), nullptr,
                                              &line3Vs_)) &&
        SUCCEEDED(device_->CreatePixelShader(ps3->GetBufferPointer(),
                                             ps3->GetBufferSize(), nullptr,
                                             &line3Ps_)) &&
        SUCCEEDED(device_->CreateInputLayout(il3, 7, vs3->GetBufferPointer(),
                                             vs3->GetBufferSize(),
                                             &line3Layout_));
    if (!ok3) {
        log("D3D11: Line3-programmet kunde inte skapas — linjerna i rymden "
            "ritas inte");
        line3Layout_.Reset();
        line3Ps_.Reset();
        line3Vs_.Reset();
    }
    return true;
}

void D3dRenderer::shutdownScene()
{
    for (TransientRing& r : transient_)
        r = TransientRing{};
    sceneClipCbuf_.Reset();
    sceneClip_ = SceneClip{};
    sceneCbuf_.Reset();
    line3Layout_.Reset();
    line3Ps_.Reset();
    line3Vs_.Reset();
    lineLayout_.Reset();
    linePs_.Reset();
    lineVs_.Reset();
}

void D3dRenderer::beginSceneFrame()
{
    // Framens första uppladdning tar DISCARD (den förra framens draws
    // läser den gamla ytan); delar som växte ur ringen förra framen släpps
    // nu, när inget längre pekar på dem.
    for (TransientRing& r : transient_) {
        r.offset = 0;
        r.fresh  = true;
        r.retired.clear();
    }
    pipeline_ = Pipeline::Ui;   // beginFrame har just bundit UI-pipelinen
}

// --------------------------------------------------------------- buffertar

std::unique_ptr<GpuBuffer> D3dRenderer::createBuffer(BufferKind kind,
                                                     std::size_t bytes,
                                                     const void* data)
{
    // DEFAULT-användning: GPU-residenta, och uppdaterbara med
    // UpdateSubresource — den rekommenderade formen för sällsynta ändringar.
    ComPtr<ID3D11Buffer> buf;
    if (device_ && bytes > 0 && bytes <= UINT_MAX) {
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = static_cast<UINT>(bytes);
        bd.Usage     = D3D11_USAGE_DEFAULT;
        bd.BindFlags = bindFlagsOf(kind);
        const D3D11_SUBRESOURCE_DATA init{data, 0, 0};
        if (FAILED(device_->CreateBuffer(&bd, data ? &init : nullptr, &buf))) {
            log("createBuffer: %zu byte kunde inte skapas", bytes);
            buf.Reset();
        }
    }
    if (buf) {
        ++stats_.buffersCreated;
        if (data)
            stats_.bytesUploaded += bytes;
    }
    return std::make_unique<D3dGpuBuffer>(kind, bytes, std::move(buf));
}

bool D3dRenderer::updateBuffer(GpuBuffer& b, std::size_t offset,
                               const void* data, std::size_t bytes)
{
    // Bufferten kommer alltid ur den här backenden (createBuffer) — casten
    // är säker per konstruktion, som drawText:s Font.
    auto& db = static_cast<D3dGpuBuffer&>(b);
    if (!context_ || !updateOk(b, offset, data, bytes))
        return false;
    // Ordnad mot rit-anropen i den omedelbara kontexten: det som redan
    // ritats i framen behåller det gamla innehållet.
    const D3D11_BOX box{static_cast<UINT>(offset), 0, 0,
                        static_cast<UINT>(offset + bytes), 1, 1};
    context_->UpdateSubresource(db.buffer(), 0, &box, data, 0, 0);
    stats_.bytesUploaded += bytes;
    return true;
}

BufferSlice D3dRenderer::uploadTransient(BufferKind kind, const void* data,
                                         std::size_t bytes)
{
    if (!device_ || !context_ || !data || bytes == 0 || bytes > UINT_MAX)
        return {};
    TransientRing& ring = transient_[kind == BufferKind::Index ? 1 : 0];

    std::size_t at = alignUp(ring.offset, kTransientAlign);
    if (!ring.buf || at + bytes > ring.buf->size()) {
        // Ringen räcker inte för framen: en dubbelt så stor buffert. Den
        // gamla kan bära delar som redan lämnats ut i den här framen — den
        // lever till nästa frame.
        std::size_t cap = ring.buf ? ring.buf->size() * 2 : kTransientInitial;
        while (cap < bytes)
            cap *= 2;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth      = static_cast<UINT>(cap);
        bd.Usage          = D3D11_USAGE_DYNAMIC;
        bd.BindFlags      = bindFlagsOf(kind);
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        ComPtr<ID3D11Buffer> nb;
        if (cap > UINT_MAX || FAILED(device_->CreateBuffer(&bd, nullptr, &nb))) {
            log("uploadTransient: ringen kunde inte växa till %zu byte", cap);
            return {};
        }
        if (ring.buf)
            ring.retired.push_back(std::move(ring.buf));
        ring.buf   = std::make_unique<D3dGpuBuffer>(kind, cap, std::move(nb));
        ring.fresh = true;
        at         = 0;
    }

    // DISCARD första gången bufferten skrivs i framen, NO_OVERWRITE sedan —
    // ringen växer bara framåt, så inget GPU:n redan fått i framen skrivs över.
    D3D11_MAPPED_SUBRESOURCE m;
    const D3D11_MAP mode =
        ring.fresh ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE;
    if (FAILED(context_->Map(ring.buf->buffer(), 0, mode, 0, &m)))
        return {};
    std::memcpy(static_cast<std::uint8_t*>(m.pData) + at, data, bytes);
    context_->Unmap(ring.buf->buffer(), 0);

    ring.fresh  = false;
    ring.offset = at + bytes;
    stats_.bytesUploaded += bytes;
    return BufferSlice{ring.buf.get(), at, bytes};
}

// ---------------------------------------------------------------- ritning

// HLSL-arrayens längd är skriven som en literal i källan.
static_assert(kMaxClipPlanes == 8,
              "gSceneClip i kHlslClipSrc är deklarerad [8]");

// Målarens klipp i b5 — skrivet bara när anropets klipp skiljer
// från det bufferten bär, så lagren (inget klipp) aldrig laddar upp det.
// Antalet har `meshDrawOk` prövat.
bool D3dRenderer::bindSceneClip(const MeshDraw& d)
{
    const SceneClip& k = d.uniforms.clip;
    if (k == sceneClip_)
        return true;
    SceneClipCb cb{};
    for (int i = 0; i < k.count; ++i) {
        cb.planes[i][0] = k.planes[i].nx;
        cb.planes[i][1] = k.planes[i].ny;
        cb.planes[i][2] = k.planes[i].d;
    }
    cb.meta[0] = static_cast<float>(k.count);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(context_->Map(sceneClipCbuf_.Get(), 0, D3D11_MAP_WRITE_DISCARD,
                             0, &m)))
        return false;
    std::memcpy(m.pData, &cb, sizeof cb);
    context_->Unmap(sceneClipCbuf_.Get(), 0);
    sceneClip_ = k;
    return true;
}

void D3dRenderer::drawMesh(const MeshDraw& d)
{
    // De egna programmen bor i d3d11_program.cpp.
    if (d.program == MeshProgram::Custom) {
        drawCustomMesh(d);
        return;
    }
    // Image-programmet bor bredvid bilderna (d3d11_image.cpp).
    if (d.program == MeshProgram::Image) {
        drawImageMesh(d);
        return;
    }
    // Fill-programmet (ytorna) bor i d3d11_fill.cpp, Glyph (texten) i
    // d3d11_glyph.cpp.
    if (d.program == MeshProgram::Fill) {
        drawFillMesh(d);
        return;
    }
    if (d.program == MeshProgram::Glyph || d.program == MeshProgram::Glyph3) {
        drawGlyphMesh(d);   // också Glyph3
        return;
    }
    // Line och Line3 — samma källa, var sin variant.
    const bool pos3 = d.program == MeshProgram::Line3;
    if (!context_ ||
        !meshDrawOk(d, pos3 ? line3Vs_ != nullptr
                            : d.program == MeshProgram::Line) ||
        !sceneCbuf_)
        return;

    flush();   // det UI:t batchat före anropet ritas först — målarordningen
    const Pipeline want = pos3 ? Pipeline::Line3 : Pipeline::Line;
    if (pipeline_ != want) {
        context_->IASetInputLayout(pos3 ? line3Layout_.Get() : lineLayout_.Get());
        context_->VSSetShader(pos3 ? line3Vs_.Get() : lineVs_.Get(), nullptr, 0);
        context_->PSSetShader(pos3 ? line3Ps_.Get() : linePs_.Get(), nullptr, 0);
        context_->VSSetConstantBuffers(2, 1, sceneCbuf_.GetAddressOf());
        context_->PSSetConstantBuffers(2, 1, sceneCbuf_.GetAddressOf());
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        pipeline_ = want;
        ++stats_.programSwitches;
    }

    // Kameran för just den här chunken — per anrop, och räknas inte som
    // uppladdning (RenderStats).
    SceneCb cb{};
    std::memcpy(cb.viewProj, d.uniforms.viewProj, sizeof cb.viewProj);
    cb.params[0] = static_cast<float>(targetW_);   // det BUNDNA målet —
    cb.params[1] = static_cast<float>(targetH_);   // ett render-mål
    cb.params[2] = d.uniforms.pxPerWidthUnit;
    cb.params[3] = d.uniforms.widthSpace == WidthSpace::World ? 1.0f : 0.0f;
    fillPaint(cb, d);   // färgen och opaciteten
    // Konturen: spetsgränsen och strecken (meshDrawOk har prövat dem).
    const SceneDash& dash = d.uniforms.dash;
    cb.stroke[0] = d.uniforms.miterLimit;
    cb.stroke[1] = static_cast<float>(dash.count);
    cb.stroke[2] = dash.perWorldUnit;
    cb.stroke[3] = dash.offset;
    std::memcpy(cb.dash, dash.ends, sizeof cb.dash);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(context_->Map(sceneCbuf_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0,
                             &m)))
        return;
    std::memcpy(m.pData, &cb, sizeof cb);
    context_->Unmap(sceneCbuf_.Get(), 0);
    if (!bindSceneClip(d))
        return;
    bindPaintRamp(d);
    applyMeshState(d.state);   // renderläget

    ID3D11Buffer* vb =
        static_cast<const D3dGpuBuffer*>(d.vertices.buffer)->buffer();
    ID3D11Buffer* ib =
        static_cast<const D3dGpuBuffer*>(d.indices.buffer)->buffer();
    const UINT stride = pos3 ? sizeof(LineVertex3) : sizeof(LineVertex);
    const UINT offset = static_cast<UINT>(d.vertices.offset);
    context_->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    context_->IASetIndexBuffer(ib, DXGI_FORMAT_R32_UINT,
                               static_cast<UINT>(d.indices.offset));
    context_->DrawIndexed(d.indexCount, 0, 0);
    ++stats_.drawCalls;
    stats_.vertices += d.indexCount;
}

} // namespace gfx
