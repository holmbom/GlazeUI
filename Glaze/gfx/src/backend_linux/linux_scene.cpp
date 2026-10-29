// Scenens del av GlRenderer — GPU-buffertarna, den
// transienta ringen och Line-programmet (och dess Line3-variant: källan med
// GLAZE_POS3 efter `#version`, ett eget länkat program).
// Spegel av d3d11_scene.cpp; här bara GL-skillnaderna:
//   * UNIFORMER ÄR PER PROGRAM. Klippmaskerna lagras i renderern och
//     laddas upp LAT till det program som ritar, styrt av en generation
//     (`syncClipMasks`) — UI-programmet och Line-programmet har var sin.
//     Målarens klipp följer rit-anropet (`setSceneClip`, nedan).
//   * Buffertarna laddas upp via bindningspunkten GL_COPY_WRITE_BUFFER: den
//     ingår varken i någon VAO eller i UI-batcherns GL_ARRAY_BUFFER, så en
//     uppladdning mitt i en frame kan aldrig få UI:ts glBufferData att
//     skriva över en scenbuffert.
//   * z räknas om till GL:s klipprymd ([−w, w]) i vertexshadern; kamerans
//     matris är densamma som D3D11:s och Metals.
//   * Fragmentets läge kommer ur varyingen vPix, aldrig gl_FragCoord —
//     samma y-flippfälla som UI-pipelinens SDF (linux_renderer.cpp).

#include <cstring>

#include "gfx/platform.h"
#include "gl_common.h"
#include "linux_backend.h"

namespace gfx {
namespace {

// Line-programmets vertexshader — rad för rad samma som HLSL:ns lineVs
// (läs d3d11_scene.cpp för formen: ett tecknat avstånd per bit, skarvarna
// ur de projicerade grannarna, ändarna och strecken). LINE3 är
// samma källa med GLAZE_POS3 — HLSL:ns POS3-gren rad för rad.
const char kLineVsSrc[] = R"glsl(
uniform mat4 uViewProj;   // chunk-lokalt → pixelklipp
uniform vec4 uParams;     // .xy målets storlek (px), .z px per breddenhet,
                          // .w 1 = bredden i världen
uniform vec4 uStroke;     // .x spetsgränsen (resten läser fragmentshadern)
#ifdef GLAZE_POS3
in vec3  aPos;
in vec3  aPrev;
in vec3  aNext;
#else
in vec2  aPos;
in vec2  aPrev;
in vec2  aNext;
#endif
in float aWidth;
in vec4  aCol;
in ivec4 aSk;             // .x sidan (−1/+1) eller hörnet (0–3), .y LineKind,
                          // .z ändarna (lineflag) — HELTAL (glVertexAttribIPointer)
in float aDist;           // linjens längd fram till punkten (världen)
out vec4 vCol;
// Skärmläget: px från mittlinjen/punkten, linjärt i SKÄRMEN.
noperspective out vec2 vLocal;
// Världsläget: världsenheter, perspektivkorrekt.
out vec2 vWorld;
// Halva bredden — platt; i Line3 linjär i skärmen (ändarna kan skilja).
#ifdef GLAZE_POS3
noperspective out float vHw;
#else
flat out float vHw;
#endif
noperspective out vec2 vPix;   // fragmentets läge i målets px (maskerna)
// Världspunkten för en gradient: mittlinjens punkt i skärmläget,
// den extruderade i världsläget — perspektivkorrekt.
out vec2 vPpos;
// Ändarnas tecknade avstånd (+ utanför): skärmläget i px, världsläget i
// världsenheter.
noperspective out vec2 vEndsS;
out vec2 vEndsW;
out float vDist;               // strecken — perspektivkorrekt
flat out vec4 vJn;             // skarvens utåtnormaler (in .xy, ut .zw)
flat out vec4 vJm;             // bisektrisen .xy, fasens avstånd .z
flat out ivec2 vKc;            // LineKind, ändformen

const float kFar = 1.0e4;

#ifdef GLAZE_POS3
vec4 toClip(vec3 p) { return uViewProj * vec4(p, 1.0); }
#else
vec4 toClip(vec2 p) { return uViewProj * vec4(p, 0.0, 1.0); }
#endif

// Pixelklipp → målets px. Ett w ≤ 0 ligger bakom kameran, och linjeprogrammen
// kastar det själva (trimSegment, de runda bitarna) innan det används.
vec2 toPx(vec4 c) { return c.xy / max(c.w, 1e-6); }

// Pixelklipp → GL:s klipprymd: x och y som D3D:s, z ur [0, w] till [−w, w].
vec4 toNdc(vec4 c)
{
    return vec4(c.x * 2.0 / uParams.x - c.w, c.w - c.y * 2.0 / uParams.y,
                2.0 * c.z - c.w, c.w);
}

// a flyttad längs a→b till närplanet (klippdjup 0).
vec4 toNear(vec4 a, vec4 b) { return mix(a, b, a.z / (a.z - b.z)); }

// SEGMENTET INNANFÖR KLIPPYTAN och förlängningen innanför klippdjupet —
// HLSL:ns trimPlane/trimSegment/keepInside och skäl (GL-klippningen
// snedvrider de skärmlinjära värdena i llvmpipe). Båda ändarna utanför samma
// plan ⇒ `outside`, och anroparen kastar segmentet — bakom närplanet hade
// förlängningens negativa tak skjutit ett hörn förbi ögat.
void trimPlane(float fc, float fn, inout vec4 c, inout vec4 nb,
               inout float span, inout float moved, inout bool outside)
{
    if (fc < 0.0 && fn < 0.0) {
        outside = true;
    } else if (fc < 0.0 && fn >= 0.0) {
        float t = fc / (fc - fn);
        c      = mix(c, nb, t);
        moved += t * span;
        span  *= 1.0 - t;
    } else if (fn < 0.0 && fc >= 0.0) {
        float t = fn / (fn - fc);
        nb    = mix(nb, c, t);
        span *= 1.0 - t;
    }
}

float trimSegment(inout vec4 c, inout vec4 nb, float span, out bool outside)
{
    float g = max(uParams.x, uParams.y);
    float r = uParams.x + g, b = uParams.y + g;
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

float keepInside(vec4 c, vec4 dc, float t)
{
    float dz = dc.z * t;
    float df = (dc.w - dc.z) * t;
    float k  = 1.0;
    if (dz < 0.0)
        k = min(k, max(c.z, 0.0) / -dz);
    if (df < 0.0)
        k = min(k, max(c.w - c.z, 0.0) / -df);
    return t * k;
}

vec2 cornerOf(int i)
{
    return vec2((i & 1) != 0 ? 1.0 : -1.0, (i & 2) != 0 ? 1.0 : -1.0);
}

vec2 perpOf(vec2 d) { return vec2(-d.y, d.x); }

vec2 unitOr(vec2 d, vec2 fallback)
{
    float l = length(d);
    return l > 1e-6 ? d / l : fallback;
}

void joinFrame(vec2 dA, vec2 dB, out vec2 nA, out vec2 nB, out vec2 m,
               out float c)
{
    float s = (dA.x * dB.y - dA.y * dB.x) >= 0.0 ? -1.0 : 1.0;
    nA = s * perpOf(dA);
    nB = s * perpOf(dB);
    vec2  mv = nA + nB;
    float ml = length(mv);
    m = ml < 1e-3 ? dA : mv / ml;
    c = ml < 1e-3 ? 0.0 : dot(nA, m);
}

vec2 jointCorner(int corner, vec2 nA, vec2 nB, vec2 m, float c, float ext,
                 bool miter)
{
    float tip = miter ? ext / max(c, 1e-6)
                      : max(ext * c, ext / (2.0 * c + 1.0));
    return corner == 0 ? -m * ext
         : corner == 1 ? nA * ext
         : corner == 2 ? nB * ext
                       : m * tip;
}

#ifndef GLAZE_POS3

float worldPx(vec2 p, vec2 d, float hw)
{
    float l = max(hw, 1e-6);
    float k = length(toPx(toClip(p + d * l)) - toPx(toClip(p))) / l;
    return k > 0.0 ? 1.0 / k : 0.0;
}

void main()
{
    vCol   = aCol;
    vDist  = aDist;
    vLocal = vec2(0.0);
    vWorld = vec2(0.0);
    vEndsS = vec2(-kFar);
    vEndsW = vec2(-kFar);
    vJn    = vec4(0.0);
    vJm    = vec4(0.0, 0.0, kFar, 0.0);
    int  side    = int(aSk.x);
    int  kind    = int(aSk.y);
    int  flags   = int(aSk.z);
    int  cap     = flags & 3;
    bool segment = kind == 0 || kind == 1;
    vKc = ivec2(kind, cap);
    bool  startCap = segment && (flags & 4) != 0 && cap != 1;
    bool  endCap   = segment && (flags & 8) != 0 && cap != 1;
    bool  isStart  = kind == 0;
    float miterLim = uStroke.x;

    if (uParams.w > 0.5) {
        // BREDDEN I VÄRLDEN: extruderingen i planet FÖRE projektionen.
        float hw = 0.5 * aWidth;
        vHw = hw;
        vec2 off;
        if (segment) {
            vec2  seg    = isStart ? aNext - aPos : aPos - aPrev;
            vec2  d      = unitOr(seg, vec2(1.0, 0.0));
            float len    = length(seg);
            float px     = worldPx(aPos, perpOf(d), hw);
            float ext    = hw + px;
            float capExt = cap == 2 ? hw : 0.0;
            float sExt   = startCap ? capExt + px : 0.0;
            float eExt   = endCap ? capExt + px : 0.0;
            float along  = isStart ? -sExt : len + eExt;
            off    = perpOf(d) * (float(side) * ext) + d * (isStart ? -sExt : eExt);
            vWorld = vec2(0.0, float(side) * ext);
            vEndsW = vec2(startCap ? -capExt - along : -kFar,
                          endCap ? along - len - capExt : -kFar);
            vDist  = aDist + (isStart ? -sExt : eExt);
        } else if (kind == 2) {
            vec2  dir = cornerOf(side);
            float ext = hw + worldPx(aPos, normalize(dir), hw);
            off    = dir * ext;
            vWorld = off;
        } else {
            vec2  nA, nB, m;
            float c;
            vec2  dA = unitOr(aPos - aPrev, unitOr(aNext - aPos, vec2(1.0, 0.0)));
            vec2  dB = unitOr(aNext - aPos, dA);
            joinFrame(dA, dB, nA, nB, m, c);
            float ext   = hw + worldPx(aPos, nA, hw);
            bool  miter = kind == 3 && c * miterLim >= 1.0;
            off    = jointCorner(side, nA, nB, m, c, ext, miter);
            vWorld = off;
            vJn    = vec4(nA, nB);
            vJm    = vec4(m, miter ? kFar : hw * c, 0.0);
        }
        vec4 cw     = toClip(aPos + off);
        gl_Position = toNdc(cw);
        vPix        = toPx(cw);
        vPpos       = aPos + off;
        return;
    }

    // BREDDEN PÅ SKÄRMEN: extruderingen i px EFTER projektionen.
    float hw  = 0.5 * aWidth * uParams.z;
    float ext = hw + 1.0;   // + kantutjämningens marginal
    vHw   = hw;
    vPpos = aPos;
    vec4 c = toClip(aPos);
    vec2 off;
    if (segment) {
        vec2  nbPos = isStart ? aNext : aPrev;
        vec2  segW  = isStart ? aNext - aPos : aPos - aPrev;
        vec4  nb    = toClip(nbPos);
        // KLIPPYTAN: en ände utanför flyttas längs segmentet till planet —
        // linjens längd och mittpunkten med (HLSL:en).
        bool  outside;
        float moved = trimSegment(c, nb, length(segW), outside);
        if (outside) {
            gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
            vPix        = vec2(0.0);
            return;
        }
        float dist  = aDist + (isStart ? moved : -moved);
        if (moved > 0.0)
            vPpos = mix(aPos, nbPos, moved / length(segW));
        vec2 d = toPx(nb) - toPx(c);
        if (!isStart)
            d = -d;                        // början → slut
        float len = length(d);
        d = len > 1e-6 ? d / len : vec2(1.0, 0.0);
        float capExt = cap == 2 ? hw : 0.0;
        float sExt   = startCap ? capExt + 1.0 : 0.0;
        float eExt   = endCap ? capExt + 1.0 : 0.0;
        float along  = isStart ? -sExt : len + eExt;
        vLocal = vec2(0.0, float(side) * ext);
        vEndsS = vec2(startCap ? -capExt - along : -kFar,
                      endCap ? along - len - capExt : -kFar);
        // FÖRLÄNGNINGEN i VÄRLDEN längs segmentet med den lokala skalan
        // (perspektivdivisionens derivata) — vertexen får sitt eget w och
        // `dist` hör ihop med det (se HLSL:en).
        vec4  dc   = uViewProj * vec4(unitOr(segW, vec2(1.0, 0.0)), 0.0, 0.0);
        float pxW  = length((dc.xy * c.w - c.xy * dc.w) / max(c.w * c.w, 1e-12));
        float extW = keepInside(c, dc, (isStart ? -sExt : eExt) / max(pxW, 1e-6));
        c     = c + dc * extW;
        vDist = dist + extW;
        off   = perpOf(d) * (float(side) * ext);
    } else if (kind == 2) {
        // En rund skarv eller ände bakom närplanet syns inte (HLSL:en).
        if (c.z < 0.0) {
            gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
            vPix        = vec2(0.0);
            return;
        }
        off    = cornerOf(side) * ext;
        vLocal = off;
    } else {
        // SKARVEN, i SKÄRMEN ur de projicerade grannarna; punkten bakom
        // närplanet ger ingen skarv.
        if (c.z < 0.0) {
            gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
            vPix        = vec2(0.0);
            return;
        }
        vec4 ca = toClip(aPrev);
        vec4 cb = toClip(aNext);
        if (ca.z < 0.0)
            ca = toNear(ca, c);
        if (cb.z < 0.0)
            cb = toNear(cb, c);
        vec2  pc = toPx(c);
        vec2  nA, nB, m;
        float cs;
        vec2  dA = unitOr(pc - toPx(ca), unitOr(toPx(cb) - pc, vec2(1.0, 0.0)));
        vec2  dB = unitOr(toPx(cb) - pc, dA);
        joinFrame(dA, dB, nA, nB, m, cs);
        bool miter = kind == 3 && cs * miterLim >= 1.0;
        off    = jointCorner(side, nA, nB, m, cs, ext, miter);
        vLocal = off;
        vJn    = vec4(nA, nB);
        vJm    = vec4(m, miter ? kFar : hw * cs, 0.0);
    }
    vec2 p = toPx(c) + off;
    gl_Position =
        toNdc(vec4(p * c.w, glaze_standingZ(uViewProj, c, off), c.w));
    vPix        = p;
}

#else   // GLAZE_POS3 — Line3 (HLSL:ns POS3-gren rad för rad)

// Skärmens px per världsenhet vid klipp-punkten c, vinkelrätt mot blicken:
// x-radens del vinkelrätt mot w-raden, genom djupet. GLSL:s mat4 indexeras
// per KOLUMN — raderna plockas ur kolumnernas komponenter.
float pxPerWorldAt(vec4 c)
{
    vec3  rx = vec3(uViewProj[0][0], uViewProj[1][0], uViewProj[2][0]);
    vec3  rw = vec3(uViewProj[0][3], uViewProj[1][3], uViewProj[2][3]);
    float ww = dot(rw, rw);
    vec3  s  = ww > 0.0 ? rx - rw * (dot(rx, rw) / ww) : rx;
    return length(s) / max(c.w, 1e-6);
}

vec3 unitOr3(vec3 d, vec3 fallback)
{
    float l = length(d);
    return l > 1e-6 ? d / l : fallback;
}

float halfWidthAt(float width, vec4 c)
{
    return 0.5 * width * (uParams.w > 0.5 ? pxPerWorldAt(c) : uParams.z);
}

void main()
{
    vCol   = aCol;
    vDist  = aDist;
    vLocal = vec2(0.0);
    vWorld = vec2(0.0);
    vEndsS = vec2(-kFar);
    vEndsW = vec2(-kFar);
    vJn    = vec4(0.0);
    vJm    = vec4(0.0, 0.0, kFar, 0.0);
    int  side    = int(aSk.x);
    int  kind    = int(aSk.y);
    int  flags   = int(aSk.z);
    int  cap     = flags & 3;
    bool segment = kind == 0 || kind == 1;
    vKc = ivec2(kind, cap);
    bool  startCap = segment && (flags & 4) != 0 && cap != 1;
    bool  endCap   = segment && (flags & 8) != 0 && cap != 1;
    bool  isStart  = kind == 0;
    float miterLim = uStroke.x;

    vPpos = aPos.xy;
    vec4  c = toClip(aPos);
    float hw;
    vec2  off;
    if (segment) {
        vec3  nbPos = isStart ? aNext : aPrev;
        vec3  segW  = isStart ? aNext - aPos : aPos - aPrev;
        vec4  nb    = toClip(nbPos);
        bool  outside;
        float moved = trimSegment(c, nb, length(segW), outside);
        if (outside) {
            gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
            vPix        = vec2(0.0);
            return;
        }
        float dist  = aDist + (isStart ? moved : -moved);
        if (moved > 0.0)
            vPpos = mix(aPos, nbPos, moved / length(segW)).xy;
        hw = halfWidthAt(aWidth, c);
        float hwNb = halfWidthAt(aWidth, nb);
        float hwS  = isStart ? hw : hwNb;
        float hwE  = isStart ? hwNb : hw;
        float ext  = hw + 1.0;
        vec2 d = toPx(nb) - toPx(c);
        if (!isStart)
            d = -d;
        float len = length(d);
        d = len > 1e-6 ? d / len : vec2(1.0, 0.0);
        float capS  = cap == 2 ? hwS : 0.0;
        float capE  = cap == 2 ? hwE : 0.0;
        float sExt  = startCap ? capS + 1.0 : 0.0;
        float eExt  = endCap ? capE + 1.0 : 0.0;
        float along = isStart ? -sExt : len + eExt;
        vLocal = vec2(0.0, float(side) * ext);
        vEndsS = vec2(startCap ? -capS - along : -kFar,
                      endCap ? along - len - capE : -kFar);
        // FÖRLÄNGNINGEN, EXAKT I SKÄRMEN — HLSL:ns formel och skäl:
        // t = e·w² / (lin − e·w·q).
        float sgn  = isStart ? -1.0 : 1.0;
        float e    = isStart ? sExt : eExt;
        vec4  dc   = sgn * (uViewProj * vec4(unitOr3(segW, vec3(1.0, 0.0, 0.0)), 0.0));
        vec2  ud   = sgn * d;
        float lin  = dot(ud, dc.xy) * c.w - dot(ud, c.xy) * dc.w;
        float den  = lin - e * c.w * dc.w;
        float tMax = 1e3 * c.w / max(dc.w, 1e-6);
        float t    = keepInside(c, dc, lin > 1e-6 * c.w * c.w
                         ? (den > 0.0 ? min(e * c.w * c.w / den, tMax) : tMax)
                         : 0.0);
        c     = c + dc * t;
        vDist = dist + sgn * t;
        off   = perpOf(d) * (float(side) * ext);
    } else {
        hw = halfWidthAt(aWidth, c);
        float ext = hw + 1.0;
        if (c.z < 0.0) {
            // Bakom närplanet: ingen skarv och ingen rund ände syns.
            vHw         = hw;
            gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
            vPix        = vec2(0.0);
            return;
        }
        if (kind == 2) {
            off    = cornerOf(side) * ext;
            vLocal = off;
        } else {
            vec4 ca = toClip(aPrev);
            vec4 cb = toClip(aNext);
            if (ca.z < 0.0)
                ca = toNear(ca, c);
            if (cb.z < 0.0)
                cb = toNear(cb, c);
            vec2  pc = toPx(c);
            vec2  nA, nB, m;
            float cs;
            vec2  dA = unitOr(pc - toPx(ca), unitOr(toPx(cb) - pc, vec2(1.0, 0.0)));
            vec2  dB = unitOr(toPx(cb) - pc, dA);
            joinFrame(dA, dB, nA, nB, m, cs);
            bool miter = kind == 3 && cs * miterLim >= 1.0;
            off    = jointCorner(side, nA, nB, m, cs, ext, miter);
            vLocal = off;
            vJn    = vec4(nA, nB);
            vJm    = vec4(m, miter ? kFar : hw * cs, 0.0);
        }
    }
    vHw = hw;
    vec2 p = toPx(c) + off;
    gl_Position =
        toNdc(vec4(p * c.w, glaze_standingZ(uViewProj, c, off), c.w));
    vPix        = p;
}

#endif   // GLAZE_POS3
)glsl";

// Fragmentshadern — klippmaskernas och färgens delar (kGlslClipSrc,
// kGlslPaintSrc) klistras FÖRE.
const char kLineFsSrc[] = R"glsl(
uniform vec4 uParams;
uniform vec4 uStroke;     // .x spetsgränsen, .y strecken (antal slut, 0 =
                          // heldragen), .z mönsterenheter per världsenhet,
                          // .w mönstrets förskjutning
uniform vec4 uDash[2];    // strecken som KUMULATIVA slut (på, av, …)
in vec4 vCol;
noperspective in vec2 vLocal;
in vec2 vWorld;
#ifdef GLAZE_POS3
noperspective in float vHw;
#else
flat in float vHw;
#endif
noperspective in vec2 vPix;
in vec2 vPpos;
noperspective in vec2 vEndsS;
in vec2 vEndsW;
in float vDist;
flat in vec4 vJn;
flat in vec4 vJm;
flat in ivec2 vKc;
out vec4 frag;

const float kFar = 1.0e4;

float dashEnd(int k)
{
    return k < 0 ? 0.0 : uDash[k >> 2][k & 3];
}

// Det tecknade avståndet i mönstrets enhet till närmaste streck (+ inne).
float dashDistance(float t)
{
    int   n      = int(uStroke.y + 0.5);
    float period = 0.0;
    for (int e = 0; e < 8; ++e)
        period = e < n ? uDash[e >> 2][e & 3] : period;
    float u    = t - period * floor(t / period);
    float best = -kFar * 1e4;
    for (int k = 0; k < 4; ++k) {
        if (2 * k < n) {
            float lo = dashEnd(2 * k - 1);
            float hi = dashEnd(2 * k);
            best = max(best, min(u - lo, hi - u));
            best = max(best, min(u - lo - period, hi + period - u));
            best = max(best, min(u - lo + period, hi - period - u));
        }
    }
    return best;
}

void main()
{
#ifdef GLAZE_POS3
    bool  world  = false;   // Line3 extruderar alltid i skärmen
#else
    bool  world  = uParams.w > 0.5;
#endif
    bool  dashed = uStroke.y > 0.5;
    int   kind   = vKc.x;
    int   cap    = vKc.y;
    // Mönstrets läge och dess skärmderivata — utanför varje gren.
    float t      = vDist * uStroke.z + uStroke.w;
    float tPerPx = length(vec2(dFdx(t), dFdy(t)));

    float hw = vHw;
    float d;
    if (kind == 0 || kind == 1) {
        float lat = world ? vWorld.y : vLocal.y;
        vec2  en  = world ? vEndsW : vEndsS;
        d = min(hw - abs(lat), min(-en.x, -en.y));
        if (dashed) {
            float dd    = dashDistance(t);
            float along = world ? dd / uStroke.z : dd / max(tPerPx, 1e-12);
            if (cap == 1)
                d = along >= 0.0 ? d : min(d, hw - length(vec2(along, lat)));
            else
                d = min(d, along + (cap == 2 ? hw : 0.0));
        }
    } else {
        vec2 r = world ? vWorld : vLocal;
        if (kind == 2) {
            d = hw - length(r);
        } else {
            d = min(hw - abs(dot(r, vJn.xy)), hw - abs(dot(r, vJn.zw)));
            d = min(d, vJm.z - dot(r, vJm.xy));
        }
        if (dashed && dashDistance(t) < 0.0)
            d = -kFar;
    }
    float fw    = max(fwidth(d), 1e-6);
    float alpha = world ? clamp(d / fw + 0.5, 0.0, 1.0) : clamp(d + 0.5, 0.0, 1.0);
    // Färgen (enfärgad eller gradient) och anropets opacitet.
    vec4 col = vCol * paintColor(vPpos);
#ifdef GLAZE_POS3
    // Utanför bandet skriver Line3 inget djup (HLSL:ns kommentar).
    if (alpha <= 0.0)
        discard;
#endif
    // Målarens klipp sist (kGlslClipSrc) — utanför kastas fragmentet.
    frag = vec4(col.rgb, col.a * alpha * uPaint1.y * clipCoverage(vPix) *
                             sceneClip(vPix));
}
)glsl";

// Line3-varianten: definitionen läggs efter `#version`.
const char kGlslPos3Define[] = "#define GLAZE_POS3 1\n";

// Den transienta ringen — samma mått som D3D11:s (d3d11_scene.cpp).
constexpr std::size_t kTransientAlign   = 256;
constexpr std::size_t kTransientInitial = std::size_t{1} << 20;

std::size_t alignUp(std::size_t v, std::size_t a)
{
    return (v + a - 1) / a * a;
}

const void* byteOffset(std::size_t o)
{
    return reinterpret_cast<const void*>(o);
}

} // namespace

// ---------------------------------------------------------------- buffert

GlGpuBuffer::~GlGpuBuffer()
{
    if (renderer_ && id_)
        renderer_->destroyBuffer(id_);
}

void GlRenderer::destroyBuffer(GlBuffer b)
{
    if (!b || !ready())
        return;
    makeCurrent();
    pglDeleteBuffers(1, &b);
}

// ---------------------------------------------------------------- program

// GLSL-arrayens längd är skriven som en literal i källan.
static_assert(kMaxClipPlanes == 8,
              "uSceneClip i kGlslClipSrc är deklarerad [8]");

// Målarens klipp — scenprogrammens gemensamma uppslag och
// uppladdning (linux_backend.h `SceneClipLocs`).
bool GlRenderer::findSceneClip(unsigned program, SceneClipLocs& l)
{
    l        = SceneClipLocs{};
    l.planes = pglGetUniformLocation(program, "uSceneClip");
    l.count  = pglGetUniformLocation(program, "uSceneClipN");
    return l.planes >= 0 && l.count >= 0;
}

void GlRenderer::setSceneClip(const MeshDraw& d, SceneClipLocs& l)
{
    const SceneClip& k = d.uniforms.clip;
    if (k.count == 0 && l.sent == 0)
        return;
    pglUniform1i(l.count, k.count);
    if (k.count > 0) {
        // Antalet har `meshDrawOk` prövat.
        float v[kMaxClipPlanes][4] = {};
        for (int i = 0; i < k.count; ++i) {
            v[i][0] = k.planes[i].nx;
            v[i][1] = k.planes[i].ny;
            v[i][2] = k.planes[i].d;
        }
        pglUniform4fv(l.planes, k.count, &v[0][0]);
    }
    l.sent = k.count;
}

// Line-programmet — eller med `pos3` dess Line3-variant (definitionen
// efter `#version`) — länkat, med locations och VAO.
bool GlRenderer::buildLineProgram(bool pos3, LineProgram& p)
{
    const StatTimer klocka(stats_.compileNs);   // programbygget
    const char* name = pos3 ? "Line3" : "Line";
    const char* vsParts[] = {kGlslVersion, pos3 ? kGlslPos3Define : "",
                             kGlslStandSrc, kLineVsSrc};
    const char* fsParts[] = {kGlslVersion, pos3 ? kGlslPos3Define : "",
                             kGlslClipSrc, kGlslPaintSrc, kLineFsSrc};
    const unsigned vs = compileGlShader(GL_VERTEX_SHADER, vsParts, 4);
    const unsigned fs = compileGlShader(GL_FRAGMENT_SHADER, fsParts, 5);
    if (!vs || !fs) {
        if (vs)
            pglDeleteShader(vs);
        if (fs)
            pglDeleteShader(fs);
        log("GL: %s-programmet kunde inte kompileras — drawMesh ritar inget",
            name);
        return false;
    }
    // Attributen: LineVertex-fälten (gfx/scene.h) — sida, slag och ändar i
    // `aSk` (`pad` läses inte) och `dist` sist. Samma namn i Line3.
    static const char* const kAttribs[] = {"aPos", "aPrev", "aNext", "aWidth",
                                           "aCol", "aSk",   "aDist"};
    p.program = linkGlProgram(vs, fs, kAttribs, 7);
    if (!p.program) {
        log("GL: %s-programmet kunde inte länkas — drawMesh ritar inget", name);
        return false;
    }

    p.viewProj = pglGetUniformLocation(p.program, "uViewProj");
    p.params   = pglGetUniformLocation(p.program, "uParams");
    p.clipP0   = pglGetUniformLocation(p.program, "uClipP0");
    p.clipP1   = pglGetUniformLocation(p.program, "uClipP1");
    p.clipN    = pglGetUniformLocation(p.program, "uClipN");
    p.paint0   = pglGetUniformLocation(p.program, "uPaint0");
    p.paint1   = pglGetUniformLocation(p.program, "uPaint1");
    p.stroke   = pglGetUniformLocation(p.program, "uStroke");
    p.dash     = pglGetUniformLocation(p.program, "uDash");
    const int  rampLoc = pglGetUniformLocation(p.program, "uRamp");
    const bool klipp   = findSceneClip(p.program, p.sceneClip);
    // Samma regel som UI-programmets klippuniformer (buildPipeline): en
    // location som saknas betyder att källan och C++-sidan glidit isär, och
    // glUniform hade tyst svalt -1.
    if (p.viewProj < 0 || p.params < 0 || p.clipP0 < 0 || p.clipP1 < 0 ||
        p.clipN < 0 || p.paint0 < 0 || p.paint1 < 0 || p.stroke < 0 ||
        p.dash < 0 || rampLoc < 0 || !klipp) {
        log("GL: %s-programmets uniformer saknas (uViewProj=%d uParams=%d "
            "uClipP0=%d uClipP1=%d uClipN=%d uPaint0=%d uPaint1=%d "
            "uStroke=%d uDash=%d uRamp=%d uSceneClip=%d uSceneClipN=%d) — "
            "drawMesh ritar inget",
            name, p.viewProj, p.params, p.clipP0, p.clipP1, p.clipN, p.paint0,
            p.paint1, p.stroke, p.dash, rampLoc, p.sceneClip.planes,
            p.sceneClip.count);
        pglDeleteProgram(p.program);
        p = LineProgram{};
        return false;
    }
    pglUseProgram(p.program);
    pglUniform1i(rampLoc, 0);   // rampen på enhet 0, som UI:ts tex0
    pglUseProgram(program_);    // UI-pipelinen är den bundna

    // Programmets VAO: attributen påslagna en gång; pekarna sätts per
    // rit-anrop (vertexdelens offset).
    pglGenVertexArrays(1, &p.vao);
    pglBindVertexArray(p.vao);
    for (unsigned i = 0; i < 7; ++i)
        pglEnableVertexAttribArray(i);
    pglBindVertexArray(vao_);   // UI-pipelinen är den bundna
    return true;
}

void GlRenderer::releaseLineProgram(LineProgram& p)
{
    if (p.vao)
        pglDeleteVertexArrays(1, &p.vao);
    if (p.program)
        pglDeleteProgram(p.program);
    p = LineProgram{};
}

bool GlRenderer::initScene()
{
    if (!buildLineProgram(false, line_))
        return false;
    // Line3 får misslyckas för sig (loggat) — Line står kvar.
    buildLineProgram(true, line3_);
    return true;
}

void GlRenderer::shutdownScene()
{
    // Kontexten är aktuell (shutdownRenderer) — buffertarna raderas här,
    // innan den rivs.
    for (GlTransientRing& r : transient_)
        r = GlTransientRing{};
    releaseLineProgram(line3_);
    releaseLineProgram(line_);
}

void GlRenderer::beginSceneFrame()
{
    for (GlTransientRing& r : transient_) {
        r.offset = 0;
        r.fresh  = true;
        r.retired.clear();
    }
    pipeline_ = Pipeline::Ui;   // beginFrame har just bundit UI-pipelinen
}

// --------------------------------------------------------------- buffertar

std::unique_ptr<GpuBuffer> GlRenderer::createBuffer(BufferKind kind,
                                                    std::size_t bytes,
                                                    const void* data)
{
    GlBuffer id = 0;
    if (bytes > 0 && makeCurrent()) {
        pglGenBuffers(1, &id);
        pglBindBuffer(GL_COPY_WRITE_BUFFER, id);
        pglBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(bytes),
                      data, GL_STATIC_DRAW);
        pglBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    }
    if (id) {
        ++stats_.buffersCreated;
        if (data)
            stats_.bytesUploaded += bytes;
    } else {
        log("createBuffer: %zu byte kunde inte skapas", bytes);
    }
    return std::make_unique<GlGpuBuffer>(this, kind, id ? bytes : 0, id);
}

bool GlRenderer::updateBuffer(GpuBuffer& b, std::size_t offset,
                              const void* data, std::size_t bytes)
{
    // Bufferten kommer alltid ur den här backenden — casten är säker per
    // konstruktion, som drawText:s Font.
    auto& gb = static_cast<GlGpuBuffer&>(b);
    if (!updateOk(b, offset, data, bytes) || !makeCurrent())
        return false;
    // glBufferSubData är ordnad mot rit-anropen: det som redan ritats i
    // framen behåller det gamla innehållet.
    pglBindBuffer(GL_COPY_WRITE_BUFFER, gb.id());
    pglBufferSubData(GL_COPY_WRITE_BUFFER, static_cast<GLintptr>(offset),
                     static_cast<GLsizeiptr>(bytes), data);
    pglBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    stats_.bytesUploaded += bytes;
    return true;
}

BufferSlice GlRenderer::uploadTransient(BufferKind kind, const void* data,
                                        std::size_t bytes)
{
    if (!data || bytes == 0 || !makeCurrent())
        return {};
    GlTransientRing& ring = transient_[kind == BufferKind::Index ? 1 : 0];

    std::size_t at = alignUp(ring.offset, kTransientAlign);
    if (!ring.buf || at + bytes > ring.buf->size()) {
        // Ringen räcker inte för framen: en dubbelt så stor buffert. Den
        // gamla kan bära delar som redan lämnats ut — den lever till nästa
        // frame.
        std::size_t cap = ring.buf ? ring.buf->size() * 2 : kTransientInitial;
        while (cap < bytes)
            cap *= 2;
        GlBuffer id = 0;
        pglGenBuffers(1, &id);
        if (!id) {
            log("uploadTransient: ringen kunde inte växa till %zu byte", cap);
            return {};
        }
        pglBindBuffer(GL_COPY_WRITE_BUFFER, id);
        pglBufferData(GL_COPY_WRITE_BUFFER, static_cast<GLsizeiptr>(cap),
                      nullptr, GL_STREAM_DRAW);
        if (ring.buf)
            ring.retired.push_back(std::move(ring.buf));
        ring.buf   = std::make_unique<GlGpuBuffer>(this, kind, cap, id);
        ring.fresh = false;   // nyallokerad — lagringen är redan ny
        at         = 0;
    } else {
        pglBindBuffer(GL_COPY_WRITE_BUFFER, ring.buf->id());
        if (ring.fresh) {
            // Framens första uppladdning: ny lagring (orphan), så att
            // förra framens rit-anrop aldrig behöver väntas in.
            pglBufferData(GL_COPY_WRITE_BUFFER,
                          static_cast<GLsizeiptr>(ring.buf->size()), nullptr,
                          GL_STREAM_DRAW);
            ring.fresh = false;
        }
    }
    pglBufferSubData(GL_COPY_WRITE_BUFFER, static_cast<GLintptr>(at),
                     static_cast<GLsizeiptr>(bytes), data);
    pglBindBuffer(GL_COPY_WRITE_BUFFER, 0);

    ring.offset = at + bytes;
    stats_.bytesUploaded += bytes;
    return BufferSlice{ring.buf.get(), at, bytes};
}

// ---------------------------------------------------------------- ritning

void GlRenderer::drawMesh(const MeshDraw& d)
{
    // De egna programmen bor i linux_program.cpp.
    if (d.program == MeshProgram::Custom) {
        drawCustomMesh(d);
        return;
    }
    // Image-programmet bor bredvid bilderna (linux_image.cpp).
    if (d.program == MeshProgram::Image) {
        drawImageMesh(d);
        return;
    }
    // Fill-programmet (ytorna) bor i linux_fill.cpp, Glyph (texten) i
    // linux_glyph.cpp.
    if (d.program == MeshProgram::Fill) {
        drawFillMesh(d);
        return;
    }
    if (d.program == MeshProgram::Glyph || d.program == MeshProgram::Glyph3) {
        drawGlyphMesh(d);   // också Glyph3
        return;
    }
    // Line och Line3 — samma källa, var sitt program.
    const bool   pos3 = d.program == MeshProgram::Line3;
    LineProgram& lp   = pos3 ? line3_ : line_;
    if (!ready() ||
        !meshDrawOk(d, pos3 ? line3_.program != 0
                            : d.program == MeshProgram::Line) ||
        !lp.program)
        return;

    flush();   // det UI:t batchat före anropet ritas först — målarordningen
    const Pipeline want = pos3 ? Pipeline::Line3 : Pipeline::Line;
    if (pipeline_ != want) {
        pglUseProgram(lp.program);
        pglBindVertexArray(lp.vao);
        pipeline_ = want;
        ++stats_.programSwitches;
    }
    syncClipMasks(lp.clipP0, lp.clipP1, lp.clipN, lp.clipGen);
    setSceneClip(d, lp.sceneClip);

    // Kameran för just den här chunken — per anrop, räknas inte som
    // uppladdning (RenderStats).
    pglUniformMatrix4fv(lp.viewProj, 1, GL_FALSE, d.uniforms.viewProj);
    pglUniform4f(lp.params, static_cast<float>(targetW_),
                 static_cast<float>(targetH_), d.uniforms.pxPerWidthUnit,
                 d.uniforms.widthSpace == WidthSpace::World ? 1.0f : 0.0f);
    setPaint(d, lp.paint0, lp.paint1);   // färgen
    // Konturen: spetsgränsen och strecken (meshDrawOk har prövat dem).
    const SceneDash& dash = d.uniforms.dash;
    pglUniform4f(lp.stroke, d.uniforms.miterLimit,
                 static_cast<float>(dash.count), dash.perWorldUnit,
                 dash.offset);
    pglUniform4fv(lp.dash, 2, dash.ends);

    const auto& vb = static_cast<const GlGpuBuffer&>(*d.vertices.buffer);
    const auto& ib = static_cast<const GlGpuBuffer&>(*d.indices.buffer);
    const std::size_t base = d.vertices.offset;
    pglBindBuffer(GL_ARRAY_BUFFER, vb.id());
    if (pos3) {
        // LineVertex3: punkterna som tre flyttal, resten som LineVertex.
        const auto stride = static_cast<GLsizei>(sizeof(LineVertex3));
        pglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                               byteOffset(base + offsetof(LineVertex3, x)));
        pglVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                               byteOffset(base + offsetof(LineVertex3, prevX)));
        pglVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride,
                               byteOffset(base + offsetof(LineVertex3, nextX)));
        pglVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride,
                               byteOffset(base + offsetof(LineVertex3, width)));
        pglVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                               byteOffset(base + offsetof(LineVertex3, color)));
        pglVertexAttribIPointer(5, 4, GL_BYTE, stride,
                                byteOffset(base + offsetof(LineVertex3, side)));
        pglVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride,
                               byteOffset(base + offsetof(LineVertex3, dist)));
    } else {
        const auto stride = static_cast<GLsizei>(sizeof(LineVertex));
        pglVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride,
                               byteOffset(base + offsetof(LineVertex, x)));
        pglVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                               byteOffset(base + offsetof(LineVertex, prevX)));
        pglVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                               byteOffset(base + offsetof(LineVertex, nextX)));
        pglVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride,
                               byteOffset(base + offsetof(LineVertex, width)));
        pglVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride,
                               byteOffset(base + offsetof(LineVertex, color)));
        // sidan, slaget och ändarna som HELTAL (−1/+1 eller 0–3, 0–4 och
        // 0–15) — `R8G8B8A8_SINT` som i D3D11. Ett skalat format (GL_BYTE till
        // float) finns inte i D3D12, så Mesas d3d12-drivrutin (WSLg) räknade
        // om varje ritad vertex på CPU:n: canvas_perf:s karta kostade 8,5 ms
        // per kameraändring i stället för 1,4 (Line, Fill och Glyph
        // tillsammans).
        pglVertexAttribIPointer(5, 4, GL_BYTE, stride,
                                byteOffset(base + offsetof(LineVertex, side)));
        pglVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride,
                               byteOffset(base + offsetof(LineVertex, dist)));
    }
    pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib.id());   // VAO:ns tillstånd
    applyMeshState(d.state);   // renderläget
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(d.indexCount),
                   GL_UNSIGNED_INT, byteOffset(d.indices.offset));
    ++stats_.drawCalls;
    stats_.vertices += d.indexCount;
}

} // namespace gfx
