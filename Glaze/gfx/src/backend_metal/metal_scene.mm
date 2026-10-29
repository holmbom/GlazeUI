// Scenens del av MetalRenderer — GPU-buffertarna, den
// transienta ringen, UPPLADDNINGARNA och Line-programmet (och dess
// Line3-variant — källan med GLAZE_POS3). Spegel av
// d3d11_scene.cpp. Här
// bara Metal-skillnaderna:
//   * UPPLADDNINGARNA: statiska buffertar ligger i DELAD
//     lagring på Apple silicon (Managed på en diskret GPU) och skrivs på den
//     väg resursens STÄMPEL väljer (metal_backend.h `uploadPathFor`): en
//     aldrig använd direkt av CPU:n, en som ritats i en FÖREGÅENDE frame med en
//     blit i uppladdningspasset — en egen commandbuffer köad FÖRE framens —
//     och bara en som redan ritats i DEN HÄR framen med ett encoder-brott
//     (bliten i framens commandbuffer, encodern omstartad på samma mål med
//     Load), så det som redan ritats behåller det gamla innehållet som i
//     D3D11 och GL. Utanför en frame får bliten en egen commandbuffer; kön
//     kör den före nästa frames. Pass och brott tar sin staging ur ringen per
//     slot (`stagingAlloc`), aldrig en ny buffert per anrop.
//   * Den transienta ringen är en DELAD buffert per ring-slot och slag —
//     samma trippelbuffring som UI-batcherns ring: slotten skrivs först när
//     semaforen sagt att GPU:n är klar med den.
//   * Klippmaskerna är fragmentbuffert 0 för båda programmen och följer
//     encodern (startEncoder återbinder dem); scenens uniformer ligger på
//     index 1 i båda stegen, där UI:ts vertexsteg har sin viewport — därför
//     återbinder `bindUiPipeline` den.

#include <algorithm>
#include <cstring>

#include "gfx/platform.h"
#include "metal_backend.h"

namespace gfx {
namespace {

// Line-programmet — rad för rad samma som HLSL:ns lineVs/linePs (läs
// d3d11_scene.cpp för formen: ett tecknat avstånd per bit, skarvarna ur de
// projicerade grannarna, ändarna och strecken). Klippmaskernas del
// (`kMslClipSrc`) och färgens (`kMslPaintSrc`, metal_fill.mm — `PaintU`:
// params .xy målets storlek, .z px per breddenhet, .w 1 = bredden i världen;
// stroke och dash konturens) klistras FÖRE.
// LINE3 är samma källa med GLAZE_POS3 — HLSL:ns
// POS3-gren rad för rad (d3d11_scene.cpp).
const char kLineMslSrc[] = R"msl(

#ifdef GLAZE_POS3
typedef float3 LinePos;
#else
typedef float2 LinePos;
#endif

struct LineIn {
    LinePos pos  [[attribute(0)]];
    LinePos prev [[attribute(1)]];
    LinePos next [[attribute(2)]];
    float  width [[attribute(3)]];
    float4 col   [[attribute(4)]];
    char4  sk    [[attribute(5)]];   // .x sidan/hörnet, .y LineKind, .z ändarna
    float  dist  [[attribute(6)]];   // linjens längd fram till punkten (världen)
};

struct LineOut {
    float4 pos [[position]];
    float4 col;
    // Skärmläget: px från mittlinjen/punkten, linjärt i SKÄRMEN.
    float2 local [[center_no_perspective]];
    // Världsläget: världsenheter, perspektivkorrekt.
    float2 world;
    // Halva bredden — platt; i Line3 linjär i skärmen (ändarna kan skilja).
#ifdef GLAZE_POS3
    float  hw [[center_no_perspective]];
#else
    float  hw [[flat]];
#endif
    // Världspunkten för en gradient: mittlinjens punkt i
    // skärmläget, den extruderade i världsläget — perspektivkorrekt.
    float2 ppos;
    // Ändarnas tecknade avstånd (+ utanför): skärmläget i px, världsläget i
    // världsenheter.
    float2 endsS [[center_no_perspective]];
    float2 endsW;
    float  dist;                 // strecken — perspektivkorrekt
    float4 jn [[flat]];          // skarvens utåtnormaler (in .xy, ut .zw)
    float4 jm [[flat]];          // bisektrisen .xy, fasens avstånd .z
    int2   kc [[flat]];          // LineKind, ändformen
};

constant float kLineFar = 1.0e4;

#ifdef GLAZE_POS3
float4 lineToClip(constant PaintU& u, float3 p)
{
    return u.viewProj * float4(p, 1.0);
}
#else
float4 lineToClip(constant PaintU& u, float2 p)
{
    return u.viewProj * float4(p, 0.0, 1.0);
}
#endif

// Pixelklipp → målets px. Ett w ≤ 0 ligger bakom kameran, och linjeprogrammen
// kastar det själva (lineTrimSegment, de runda bitarna) innan det används.
float2 lineToPx(float4 c) { return c.xy / max(c.w, 1e-6); }

// Pixelklipp → Metals klipprymd (z redan i [0, w], som D3D11).
float4 lineToNdc(constant PaintU& u, float4 c)
{
    return float4(c.x * 2.0 / u.params.x - c.w, c.w - c.y * 2.0 / u.params.y,
                  c.z, c.w);
}

// a flyttad längs a→b till närplanet (klippdjup 0). Vikten som float4 —
// mix(T, T, T) är den form MSL säkert har.
float4 lineToNear(float4 a, float4 b)
{
    return mix(a, b, float4(a.z / (a.z - b.z)));
}

// SEGMENTET INNANFÖR KLIPPYTAN och förlängningen innanför klippdjupet —
// HLSL:ns trimPlane/trimSegment/keepInside och skäl. Båda ändarna utanför
// samma plan ⇒ `outside`, och anroparen kastar segmentet.
void lineTrimPlane(float fc, float fn, thread float4& c, thread float4& nb,
                   thread float& span, thread float& moved, thread bool& outside)
{
    if (fc < 0.0 && fn < 0.0) {
        outside = true;
    } else if (fc < 0.0 && fn >= 0.0) {
        const float t = fc / (fc - fn);
        c      = mix(c, nb, float4(t));
        moved += t * span;
        span  *= 1.0 - t;
    } else if (fn < 0.0 && fc >= 0.0) {
        const float t = fn / (fn - fc);
        nb    = mix(nb, c, float4(t));
        span *= 1.0 - t;
    }
}

float lineTrimSegment(constant PaintU& u, thread float4& c, thread float4& nb,
                      float span, thread bool& outside)
{
    const float g = max(u.params.x, u.params.y);
    const float r = u.params.x + g, b = u.params.y + g;
    float moved = 0.0;
    outside = false;
    lineTrimPlane(c.z, nb.z, c, nb, span, moved, outside);
    lineTrimPlane(c.w - c.z, nb.w - nb.z, c, nb, span, moved, outside);
    lineTrimPlane(c.x + g * c.w, nb.x + g * nb.w, c, nb, span, moved, outside);
    lineTrimPlane(r * c.w - c.x, r * nb.w - nb.x, c, nb, span, moved, outside);
    lineTrimPlane(c.y + g * c.w, nb.y + g * nb.w, c, nb, span, moved, outside);
    lineTrimPlane(b * c.w - c.y, b * nb.w - nb.y, c, nb, span, moved, outside);
    return moved;
}

float lineKeepInside(float4 c, float4 dc, float t)
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

float2 lineCorner(int i)
{
    return float2((i & 1) != 0 ? 1.0 : -1.0, (i & 2) != 0 ? 1.0 : -1.0);
}

float2 linePerp(float2 d) { return float2(-d.y, d.x); }

float2 lineUnitOr(float2 d, float2 fallback)
{
    const float l = length(d);
    return l > 1e-6 ? d / l : fallback;
}

void lineJoinFrame(float2 dA, float2 dB, thread float2& nA, thread float2& nB,
                   thread float2& m, thread float& c)
{
    const float s = (dA.x * dB.y - dA.y * dB.x) >= 0.0 ? -1.0 : 1.0;
    nA = s * linePerp(dA);
    nB = s * linePerp(dB);
    const float2 mv = nA + nB;
    const float  ml = length(mv);
    m = ml < 1e-3 ? dA : mv / ml;
    c = ml < 1e-3 ? 0.0 : dot(nA, m);
}

float2 lineJointCorner(int corner, float2 nA, float2 nB, float2 m, float c,
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

float lineWorldPx(constant PaintU& u, float2 p, float2 d, float hw)
{
    const float l = max(hw, 1e-6);
    const float k = length(lineToPx(lineToClip(u, p + d * l)) -
                           lineToPx(lineToClip(u, p))) / l;
    return k > 0.0 ? 1.0 / k : 0.0;
}

vertex LineOut lineVs(LineIn i [[stage_in]], constant PaintU& u [[buffer(1)]])
{
    LineOut o;
    o.col   = i.col;
    o.dist  = i.dist;
    o.local = float2(0.0);
    o.world = float2(0.0);
    o.endsS = float2(-kLineFar);
    o.endsW = float2(-kLineFar);
    o.jn    = float4(0.0);
    o.jm    = float4(0.0, 0.0, kLineFar, 0.0);
    const int  side    = int(i.sk.x);
    const int  kind    = int(i.sk.y);
    const int  flags   = int(i.sk.z);
    const int  cap     = flags & 3;
    const bool segment = kind == 0 || kind == 1;
    o.kc = int2(kind, cap);
    const bool  startCap = segment && (flags & 4) != 0 && cap != 1;
    const bool  endCap   = segment && (flags & 8) != 0 && cap != 1;
    const bool  isStart  = kind == 0;
    const float miterLim = u.stroke.x;

    if (u.params.w > 0.5) {
        // BREDDEN I VÄRLDEN: extruderingen i planet FÖRE projektionen.
        const float hw = 0.5 * i.width;
        o.hw = hw;
        float2 off;
        if (segment) {
            const float2 seg    = isStart ? i.next - i.pos : i.pos - i.prev;
            const float2 d      = lineUnitOr(seg, float2(1.0, 0.0));
            const float  len    = length(seg);
            const float  px     = lineWorldPx(u, i.pos, linePerp(d), hw);
            const float  ext    = hw + px;
            const float  capExt = cap == 2 ? hw : 0.0;
            const float  sExt   = startCap ? capExt + px : 0.0;
            const float  eExt   = endCap ? capExt + px : 0.0;
            const float  along  = isStart ? -sExt : len + eExt;
            off     = linePerp(d) * (float(side) * ext) + d * (isStart ? -sExt : eExt);
            o.world = float2(0.0, float(side) * ext);
            o.endsW = float2(startCap ? -capExt - along : -kLineFar,
                             endCap ? along - len - capExt : -kLineFar);
            o.dist  = i.dist + (isStart ? -sExt : eExt);
        } else if (kind == 2) {
            const float2 dir = lineCorner(side);
            const float  ext = hw + lineWorldPx(u, i.pos, normalize(dir), hw);
            off     = dir * ext;
            o.world = off;
        } else {
            float2 nA, nB, m;
            float  c;
            const float2 dA = lineUnitOr(i.pos - i.prev,
                                         lineUnitOr(i.next - i.pos, float2(1.0, 0.0)));
            const float2 dB = lineUnitOr(i.next - i.pos, dA);
            lineJoinFrame(dA, dB, nA, nB, m, c);
            const float ext   = hw + lineWorldPx(u, i.pos, nA, hw);
            const bool  miter = kind == 3 && c * miterLim >= 1.0;
            off     = lineJointCorner(side, nA, nB, m, c, ext, miter);
            o.world = off;
            o.jn    = float4(nA, nB);
            o.jm    = float4(m, miter ? kLineFar : hw * c, 0.0);
        }
        o.pos  = lineToNdc(u, lineToClip(u, i.pos + off));
        o.ppos = i.pos + off;
        return o;
    }

    // BREDDEN PÅ SKÄRMEN: extruderingen i px EFTER projektionen.
    const float hw  = 0.5 * i.width * u.params.z;
    const float ext = hw + 1.0;   // + kantutjämningens marginal
    o.hw   = hw;
    o.ppos = i.pos;
    float4 c = lineToClip(u, i.pos);
    float2 off;
    if (segment) {
        const float2 nbPos = isStart ? i.next : i.prev;
        const float2 segW  = isStart ? i.next - i.pos : i.pos - i.prev;
        float4       nb    = lineToClip(u, nbPos);
        // KLIPPYTAN: en ände utanför flyttas längs segmentet till planet —
        // linjens längd och mittpunkten med (HLSL:en).
        bool         outside;
        const float  moved = lineTrimSegment(u, c, nb, length(segW), outside);
        if (outside) {
            o.pos = float4(0.0, 0.0, -2.0, 1.0);
            return o;
        }
        const float  dist  = i.dist + (isStart ? moved : -moved);
        if (moved > 0.0)
            o.ppos = mix(i.pos, nbPos, float2(moved / length(segW)));
        float2 d = lineToPx(nb) - lineToPx(c);
        if (!isStart)
            d = -d;                        // början → slut
        const float len = length(d);
        d = len > 1e-6 ? d / len : float2(1.0, 0.0);
        const float capExt = cap == 2 ? hw : 0.0;
        const float sExt   = startCap ? capExt + 1.0 : 0.0;
        const float eExt   = endCap ? capExt + 1.0 : 0.0;
        const float along  = isStart ? -sExt : len + eExt;
        o.local = float2(0.0, float(side) * ext);
        o.endsS = float2(startCap ? -capExt - along : -kLineFar,
                         endCap ? along - len - capExt : -kLineFar);
        // FÖRLÄNGNINGEN i VÄRLDEN längs segmentet med den lokala skalan
        // (perspektivdivisionens derivata) — vertexen får sitt eget w och
        // `dist` hör ihop med det (se HLSL:en).
        const float4 dc   = u.viewProj * float4(lineUnitOr(segW, float2(1.0, 0.0)), 0.0, 0.0);
        const float  pxW  = length((dc.xy * c.w - c.xy * dc.w) / max(c.w * c.w, 1e-12));
        const float  extW = lineKeepInside(c, dc, (isStart ? -sExt : eExt) / max(pxW, 1e-6));
        c      = c + dc * extW;
        o.dist = dist + extW;
        off    = linePerp(d) * (float(side) * ext);
    } else if (kind == 2) {
        // En rund skarv eller ände bakom närplanet syns inte (HLSL:en).
        if (c.z < 0.0) {
            o.pos = float4(0.0, 0.0, -2.0, 1.0);
            return o;
        }
        off     = lineCorner(side) * ext;
        o.local = off;
    } else {
        // SKARVEN, i SKÄRMEN ur de projicerade grannarna; punkten bakom
        // närplanet ger ingen skarv.
        if (c.z < 0.0) {
            o.pos = float4(0.0, 0.0, -2.0, 1.0);
            return o;
        }
        float4 ca = lineToClip(u, i.prev);
        float4 cb = lineToClip(u, i.next);
        if (ca.z < 0.0)
            ca = lineToNear(ca, c);
        if (cb.z < 0.0)
            cb = lineToNear(cb, c);
        const float2 pc = lineToPx(c);
        float2 nA, nB, m;
        float  cs;
        const float2 dA = lineUnitOr(pc - lineToPx(ca),
                                     lineUnitOr(lineToPx(cb) - pc, float2(1.0, 0.0)));
        const float2 dB = lineUnitOr(lineToPx(cb) - pc, dA);
        lineJoinFrame(dA, dB, nA, nB, m, cs);
        const bool miter = kind == 3 && cs * miterLim >= 1.0;
        off     = lineJointCorner(side, nA, nB, m, cs, ext, miter);
        o.local = off;
        o.jn    = float4(nA, nB);
        o.jm    = float4(m, miter ? kLineFar : hw * cs, 0.0);
    }
    const float2 p = lineToPx(c) + off;
    o.pos = lineToNdc(
        u, float4(p * c.w, glaze_standingZ(u.viewProj, c, off), c.w));
    return o;
}

#else   // GLAZE_POS3 — Line3 (HLSL:ns POS3-gren rad för rad)

// Skärmens px per världsenhet vid klipp-punkten c, vinkelrätt mot blicken:
// x-radens del vinkelrätt mot w-raden, genom djupet. MSL:s float4x4
// indexeras per KOLUMN — raderna plockas ur kolumnernas komponenter.
float linePxPerWorldAt(constant PaintU& u, float4 c)
{
    const float3 rx = float3(u.viewProj[0][0], u.viewProj[1][0], u.viewProj[2][0]);
    const float3 rw = float3(u.viewProj[0][3], u.viewProj[1][3], u.viewProj[2][3]);
    const float  ww = dot(rw, rw);
    const float3 s  = ww > 0.0 ? rx - rw * (dot(rx, rw) / ww) : rx;
    return length(s) / max(c.w, 1e-6);
}

float3 lineUnitOr3(float3 d, float3 fallback)
{
    const float l = length(d);
    return l > 1e-6 ? d / l : fallback;
}

float lineHalfWidthAt(constant PaintU& u, float width, float4 c)
{
    return 0.5 * width * (u.params.w > 0.5 ? linePxPerWorldAt(u, c) : u.params.z);
}

vertex LineOut lineVs(LineIn i [[stage_in]], constant PaintU& u [[buffer(1)]])
{
    LineOut o;
    o.col   = i.col;
    o.dist  = i.dist;
    o.local = float2(0.0);
    o.world = float2(0.0);
    o.endsS = float2(-kLineFar);
    o.endsW = float2(-kLineFar);
    o.jn    = float4(0.0);
    o.jm    = float4(0.0, 0.0, kLineFar, 0.0);
    const int  side    = int(i.sk.x);
    const int  kind    = int(i.sk.y);
    const int  flags   = int(i.sk.z);
    const int  cap     = flags & 3;
    const bool segment = kind == 0 || kind == 1;
    o.kc = int2(kind, cap);
    const bool  startCap = segment && (flags & 4) != 0 && cap != 1;
    const bool  endCap   = segment && (flags & 8) != 0 && cap != 1;
    const bool  isStart  = kind == 0;
    const float miterLim = u.stroke.x;

    o.ppos = i.pos.xy;
    float4 c = lineToClip(u, i.pos);
    float  hw;
    float2 off;
    if (segment) {
        const float3 nbPos = isStart ? i.next : i.prev;
        const float3 segW  = isStart ? i.next - i.pos : i.pos - i.prev;
        float4       nb    = lineToClip(u, nbPos);
        bool         outside;
        const float  moved = lineTrimSegment(u, c, nb, length(segW), outside);
        if (outside) {
            o.hw  = 0.0;
            o.pos = float4(0.0, 0.0, -2.0, 1.0);
            return o;
        }
        const float  dist  = i.dist + (isStart ? moved : -moved);
        if (moved > 0.0)
            o.ppos = mix(i.pos, nbPos, float3(moved / length(segW))).xy;
        hw = lineHalfWidthAt(u, i.width, c);
        const float hwNb = lineHalfWidthAt(u, i.width, nb);
        const float hwS  = isStart ? hw : hwNb;
        const float hwE  = isStart ? hwNb : hw;
        const float ext  = hw + 1.0;
        float2 d = lineToPx(nb) - lineToPx(c);
        if (!isStart)
            d = -d;
        const float len = length(d);
        d = len > 1e-6 ? d / len : float2(1.0, 0.0);
        const float capS  = cap == 2 ? hwS : 0.0;
        const float capE  = cap == 2 ? hwE : 0.0;
        const float sExt  = startCap ? capS + 1.0 : 0.0;
        const float eExt  = endCap ? capE + 1.0 : 0.0;
        const float along = isStart ? -sExt : len + eExt;
        o.local = float2(0.0, float(side) * ext);
        o.endsS = float2(startCap ? -capS - along : -kLineFar,
                         endCap ? along - len - capE : -kLineFar);
        // FÖRLÄNGNINGEN, EXAKT I SKÄRMEN — HLSL:ns formel och skäl:
        // t = e·w² / (lin − e·w·q).
        const float  sgn  = isStart ? -1.0 : 1.0;
        const float  e    = isStart ? sExt : eExt;
        const float4 dc   = sgn * (u.viewProj * float4(lineUnitOr3(segW, float3(1.0, 0.0, 0.0)), 0.0));
        const float2 ud   = sgn * d;
        const float  lin  = dot(ud, dc.xy) * c.w - dot(ud, c.xy) * dc.w;
        const float  den  = lin - e * c.w * dc.w;
        const float  tMax = 1e3 * c.w / max(dc.w, 1e-6);
        const float  t    = lineKeepInside(c, dc, lin > 1e-6 * c.w * c.w
                                ? (den > 0.0 ? min(e * c.w * c.w / den, tMax) : tMax)
                                : 0.0);
        c      = c + dc * t;
        o.dist = dist + sgn * t;
        off    = linePerp(d) * (float(side) * ext);
    } else {
        hw = lineHalfWidthAt(u, i.width, c);
        const float ext = hw + 1.0;
        if (c.z < 0.0) {
            // Bakom närplanet: ingen skarv och ingen rund ände syns.
            o.hw  = hw;
            o.pos = float4(0.0, 0.0, -2.0, 1.0);
            return o;
        }
        if (kind == 2) {
            off     = lineCorner(side) * ext;
            o.local = off;
        } else {
            float4 ca = lineToClip(u, i.prev);
            float4 cb = lineToClip(u, i.next);
            if (ca.z < 0.0)
                ca = lineToNear(ca, c);
            if (cb.z < 0.0)
                cb = lineToNear(cb, c);
            const float2 pc = lineToPx(c);
            float2 nA, nB, m;
            float  cs;
            const float2 dA = lineUnitOr(pc - lineToPx(ca),
                                         lineUnitOr(lineToPx(cb) - pc, float2(1.0, 0.0)));
            const float2 dB = lineUnitOr(lineToPx(cb) - pc, dA);
            lineJoinFrame(dA, dB, nA, nB, m, cs);
            const bool miter = kind == 3 && cs * miterLim >= 1.0;
            off     = lineJointCorner(side, nA, nB, m, cs, ext, miter);
            o.local = off;
            o.jn    = float4(nA, nB);
            o.jm    = float4(m, miter ? kLineFar : hw * cs, 0.0);
        }
    }
    o.hw = hw;
    const float2 p = lineToPx(c) + off;
    o.pos = lineToNdc(
        u, float4(p * c.w, glaze_standingZ(u.viewProj, c, off), c.w));
    return o;
}

#endif   // GLAZE_POS3

float lineDashEnd(constant PaintU& u, int k)
{
    return k < 0 ? 0.0 : u.dash[k >> 2][k & 3];
}

// Det tecknade avståndet i mönstrets enhet till närmaste streck (+ inne).
float lineDashDistance(constant PaintU& u, float t)
{
    const int n      = int(u.stroke.y + 0.5);
    float     period = 0.0;
    for (int e = 0; e < 8; ++e)
        period = e < n ? u.dash[e >> 2][e & 3] : period;
    const float u0   = t - period * floor(t / period);
    float       best = -kLineFar * 1e4;
    for (int k = 0; k < 4; ++k) {
        if (2 * k < n) {
            const float lo = lineDashEnd(u, 2 * k - 1);
            const float hi = lineDashEnd(u, 2 * k);
            best = max(best, min(u0 - lo, hi - u0));
            best = max(best, min(u0 - lo - period, hi + period - u0));
            best = max(best, min(u0 - lo + period, hi - period - u0));
        }
    }
    return best;
}

fragment float4 linePs(LineOut i [[stage_in]],
                       constant ClipMasks& cm [[buffer(0)]],
                       constant PaintU& u [[buffer(1)]],
                       constant SceneClip& sc [[buffer(4)]],
                       texture2d<float> ramp [[texture(0)]],
                       sampler smp [[sampler(0)]])
{
#ifdef GLAZE_POS3
    const bool world  = false;   // Line3 extruderar alltid i skärmen
#else
    const bool world  = u.params.w > 0.5;
#endif
    const bool dashed = u.stroke.y > 0.5;
    const int  kind   = i.kc.x;
    const int  cap    = i.kc.y;
    // Mönstrets läge och dess skärmderivata — utanför varje gren.
    const float t      = i.dist * u.stroke.z + u.stroke.w;
    const float tPerPx = length(float2(dfdx(t), dfdy(t)));

    const float hw = i.hw;
    float d;
    if (kind == 0 || kind == 1) {
        const float  lat = world ? i.world.y : i.local.y;
        const float2 en  = world ? i.endsW : i.endsS;
        d = min(hw - abs(lat), min(-en.x, -en.y));
        if (dashed) {
            const float dd    = lineDashDistance(u, t);
            const float along = world ? dd / u.stroke.z : dd / max(tPerPx, 1e-12);
            if (cap == 1)
                d = along >= 0.0 ? d : min(d, hw - length(float2(along, lat)));
            else
                d = min(d, along + (cap == 2 ? hw : 0.0));
        }
    } else {
        const float2 r = world ? i.world : i.local;
        if (kind == 2) {
            d = hw - length(r);
        } else {
            d = min(hw - abs(dot(r, i.jn.xy)), hw - abs(dot(r, i.jn.zw)));
            d = min(d, i.jm.z - dot(r, i.jm.xy));
        }
        if (dashed && lineDashDistance(u, t) < 0.0)
            d = -kLineFar;
    }
    const float fw    = max(fwidth(d), 1e-6);
    const float alpha = world ? saturate(d / fw + 0.5) : saturate(d + 0.5);
    // Färgen (enfärgad eller gradient) och anropets opacitet. [[position]].xy
    // är pixelcentrum i målets px — maskernas rymd.
    const float4 col = i.col * paintColor(u, ramp, smp, i.ppos);
#ifdef GLAZE_POS3
    // Utanför bandet skriver Line3 inget djup (HLSL:ns kommentar).
    if (alpha <= 0.0)
        discard_fragment();
#endif
    // Målarens klipp sist (kMslClipSrc) — utanför kastas fragmentet.
    return float4(col.rgb, col.a * alpha * u.paint1.y *
                               clipCoverage(i.pos.xy, cm) *
                               sceneClip(i.pos.xy, sc));
}
)msl";

// Den transienta ringen — samma mått som D3D11:s (d3d11_scene.cpp).
constexpr std::size_t kTransientAlign   = 256;
constexpr std::size_t kTransientInitial = std::size_t{1} << 20;

// STAGINGRINGEN: 1 MiB per slot från start, dubblad vid
// behov; en enskild uppladdning över 8 MiB får en egen buffert i stället för
// att blåsa upp ringen. Delarna justeras som den transienta ringens (256 —
// mer än blittarnas krav på hela fyrbytes och hela pixlar).
constexpr std::size_t kStagingInitial = std::size_t{1} << 20;
constexpr std::size_t kStagingOwn     = std::size_t{8} << 20;

// LAGRINGEN FÖR STATISKA BUFFERTAR: Shared på en GPU med
// delat minne, Managed på en diskret (Intel-Macar), där en
// CPU-skrivning måste anmälas med `didModifyRange:`. Managed-grenen
// kompileras bara när programmet kan köras på en sådan Mac: en minsta
// macOS-version under 27 (270000 i Availability.h:s form). macOS 27 finns
// bara för Apple silicon — allt minne är delat, och SDK:n avråder från
// Managed där (den saknar verkan) — så där är bufferten alltid Shared och
// skrivningen har inget att anmäla.
#if defined(__MAC_OS_X_VERSION_MIN_REQUIRED) && \
    __MAC_OS_X_VERSION_MIN_REQUIRED < 270000
MTLResourceOptions bufferStorage(bool unified)
{
    return unified ? MTLResourceStorageModeShared
                   : MTLResourceStorageModeManaged;
}
void wroteDirect(id<MTLBuffer> b, bool unified, std::size_t offset,
                 std::size_t bytes)
{
    if (!unified)
        [b didModifyRange:NSMakeRange(offset, bytes)];
}
#else
MTLResourceOptions bufferStorage(bool /*unified*/)
{
    return MTLResourceStorageModeShared;
}
void wroteDirect(id<MTLBuffer> /*b*/, bool /*unified*/, std::size_t /*offset*/,
                 std::size_t /*bytes*/)
{
}
#endif

std::size_t alignUp(std::size_t v, std::size_t a)
{
    return (v + a - 1) / a * a;
}

} // namespace

// ---------------------------------------------------------------- program

bool MetalRenderer::initScene()
{
    // Line och dess Line3-variant: samma källa, Line3 med
    // GLAZE_POS3 först i den och punkterna som tre flyttal. Line3 får
    // misslyckas för sig (loggat) — Line står kvar.
    for (const bool pos3 : {false, true}) {
        @autoreleasepool {
            const char*       name = pos3 ? "Line3" : "Line";
            NSError*          err  = nil;
            const std::string src  = std::string(pos3 ? "#define GLAZE_POS3 1\n"
                                                      : "") +
                                    kMslClipSrc + kMslPaintSrc + kMslStandSrc +
                                    kLineMslSrc;
            id<MTLLibrary> lib = compileMsl(@(src.c_str()), nil, &err);
            if (!lib) {
                log("MSL (%s) misslyckades: %s — drawMesh ritar inget", name,
                    err ? err.localizedDescription.UTF8String : "?");
                if (pos3)
                    continue;
                return false;
            }
            id<MTLFunction> vs = [lib newFunctionWithName:@"lineVs"];
            id<MTLFunction> fs = [lib newFunctionWithName:@"linePs"];
            if (!vs || !fs) {
                log("MSL: lineVs/linePs saknas i biblioteket (%s)", name);
                if (pos3)
                    continue;
                return false;
            }

            // LineVertex (LineVertex3) på sina fasta offset (gfx/scene.h);
            // `pad` läses inte.
            const MTLVertexFormat pt = pos3 ? MTLVertexFormatFloat3
                                            : MTLVertexFormatFloat2;
            MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
            vd.attributes[0].format = pt;
            vd.attributes[0].offset =
                pos3 ? offsetof(LineVertex3, x) : offsetof(LineVertex, x);
            vd.attributes[1].format = pt;
            vd.attributes[1].offset = pos3 ? offsetof(LineVertex3, prevX)
                                           : offsetof(LineVertex, prevX);
            vd.attributes[2].format = pt;
            vd.attributes[2].offset = pos3 ? offsetof(LineVertex3, nextX)
                                           : offsetof(LineVertex, nextX);
            vd.attributes[3].format = MTLVertexFormatFloat;
            vd.attributes[3].offset = pos3 ? offsetof(LineVertex3, width)
                                           : offsetof(LineVertex, width);
            vd.attributes[4].format = MTLVertexFormatUChar4Normalized;
            vd.attributes[4].offset = pos3 ? offsetof(LineVertex3, color)
                                           : offsetof(LineVertex, color);
            // sidan, slaget och ändarna som tal
            vd.attributes[5].format = MTLVertexFormatChar4;
            vd.attributes[5].offset = pos3 ? offsetof(LineVertex3, side)
                                           : offsetof(LineVertex, side);
            vd.attributes[6].format = MTLVertexFormatFloat;
            vd.attributes[6].offset = pos3 ? offsetof(LineVertex3, dist)
                                           : offsetof(LineVertex, dist);
            for (NSUInteger a = 0; a < 7; ++a)
                vd.attributes[a].bufferIndex = 0;
            vd.layouts[0].stride =
                pos3 ? sizeof(LineVertex3) : sizeof(LineVertex);
            vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

            // Funktionerna och vertexformatet; blandningen, provtagningen och
            // djupet sätter varianterna (`psoFor`, metal_renderer.mm).
            MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
            pd.vertexFunction   = vs;
            pd.fragmentFunction = fs;
            pd.vertexDescriptor = vd;
            if (!registerProgram(pos3 ? Pipeline::Line3 : Pipeline::Line, pd,
                                 name) &&
                !pos3)
                return false;
        }
    }
    return true;
}

void MetalRenderer::shutdownScene()
{
    for (auto& slot : transient_)
        for (MetalTransientRing& r : slot)
            r = MetalTransientRing{};
    for (StagingRing& r : staging_)
        r = StagingRing{};
    uploadWrites_.clear();
    progDesc_[static_cast<int>(Pipeline::Line)]  = nil;
    progDesc_[static_cast<int>(Pipeline::Line3)] = nil;
}

void MetalRenderer::beginSceneFrame()
{
    // Anropas EFTER semaforen: slotten ringIndex_ är fri — semaforen släpps
    // när framen OCH dess uppladdningspass körts (commitFrame) — och delar
    // som växte ur den förra gången den användes släpps nu.
    for (MetalTransientRing& r : transient_[ringIndex_]) {
        r.offset = 0;
        r.retired.clear();
    }
    staging_[ringIndex_].offset = 0;
    staging_[ringIndex_].retired.clear();
    pipeline_    = Pipeline::Ui;   // startEncoder binder UI-pipelinen
    drawn_       = Pipeline::Ui;   // framens början räknas inte (metal_backend.h)
    drawnSerial_ = 0;
}

// ----------------------------------------------------------- uppladdningarna

MetalRenderer::Staging MetalRenderer::stagingAlloc(std::size_t bytes)
{
    // Utanför en frame finns ingen slot att låna (ringen gäller bara medan
    // semaforen håller den), och en stor uppladdning hade blåst upp ringen
    // för alltid: en EGEN delad buffert, som commandbuffern håller vid liv
    // tills bliten körts.
    if (!cmd_ || bytes > kStagingOwn) {
        id<MTLBuffer> egen =
            [device_ newBufferWithLength:std::max<std::size_t>(bytes, 4)
                                 options:MTLResourceStorageModeShared];
        if (!egen) {
            log("Metal: uppladdningsbufferten (%zu byte) kunde inte skapas",
                bytes);
            return {};
        }
        return Staging{egen, 0, static_cast<std::uint8_t*>(egen.contents)};
    }
    StagingRing& ring = staging_[ringIndex_];
    std::size_t  at   = alignUp(ring.offset, kTransientAlign);
    if (!ring.buf || at + bytes > ring.buf.length) {
        // Ringen räcker inte för framen: en dubbelt så stor buffert. Den
        // gamla bär delar som pass och brott redan kodat — den lever tills
        // slotten används nästa gång.
        std::size_t cap = ring.buf ? ring.buf.length * 2 : kStagingInitial;
        while (cap < bytes)
            cap *= 2;
        id<MTLBuffer> nb =
            [device_ newBufferWithLength:cap
                                 options:MTLResourceStorageModeShared];
        if (!nb) {
            log("Metal: stagingringen kunde inte växa till %zu byte", cap);
            return {};
        }
        if (ring.buf)
            ring.retired.push_back(ring.buf);
        ring.buf = nb;
        at       = 0;
    }
    ring.offset = at + bytes;
    return Staging{ring.buf, at,
                   static_cast<std::uint8_t*>(ring.buf.contents) + at};
}

id<MTLBlitCommandEncoder> MetalRenderer::uploadEncoder(const void*       target,
                                                       const UploadRect& r)
{
    // Passet skapas LATT och köas direkt — framens `cmd_` köas först vid sin
    // commit, så passet hamnar före den. En lokal pool: commandbuffern och
    // encodern är autoreleasade, och ett offscreen-block har ingen
    // frame-pool runt sig.
    if (!uploadCmd_) {
        @autoreleasepool {
            uploadCmd_ = [queue_ commandBuffer];
            [uploadCmd_ enqueue];
        }
    }
    // Ett nytt blit-pass bara när bliten skulle ÖVERLAPPA en tidigare till
    // samma mål i passet: ordningen inom ett pass är inte dokumenterad,
    // mellan två pass synkroniserar Metal (spårade resurser).
    bool       överlapp = false;
    const auto skrivna  = uploadWrites_.find(target);
    if (uploadBlit_ && skrivna != uploadWrites_.end())
        överlapp = std::any_of(skrivna->second.begin(), skrivna->second.end(),
                               [&r](const UploadRect& w) {
                                   return w.overlaps(r);
                               });
    if (!uploadBlit_ || överlapp) {
        if (uploadBlit_)
            [uploadBlit_ endEncoding];
        @autoreleasepool {
            uploadBlit_ = [uploadCmd_ blitCommandEncoder];
        }
        uploadWrites_.clear();
    }
    uploadWrites_[target].push_back(r);
    return uploadBlit_;
}

id<MTLCommandBuffer> MetalRenderer::finishUploadPass(MTLCommandBufferHandler done)
{
    if (!uploadCmd_)
        return nil;
    if (uploadBlit_) {
        [uploadBlit_ endEncoding];
        uploadBlit_ = nil;
    }
    uploadWrites_.clear();
    if (done)
        [uploadCmd_ addCompletedHandler:done];
    id<MTLCommandBuffer> pass = uploadCmd_;
    [pass commit];
    uploadCmd_ = nil;
    return pass;
}

void MetalRenderer::noteUse(const MeshDraw& d) const
{
    // Delarna och bilderna kommer ur den här backenden — casten är säker per
    // konstruktion, som i programmen.
    const auto buffert = [this](const GpuBuffer* b) {
        if (b)
            static_cast<const MetalGpuBuffer*>(b)->noteUse(frameSerial_);
    };
    const auto bild = [this](const Image* i) {
        if (i)
            static_cast<const MetalImage*>(i)->noteUse(frameSerial_);
    };
    buffert(d.vertices.buffer);
    buffert(d.indices.buffer);
    buffert(d.instances.buffer);
    bild(d.image);
    bild(d.labels);
    for (const Image* t : d.textures)
        bild(t);
}

// --------------------------------------------------------------- buffertar

std::unique_ptr<GpuBuffer> MetalRenderer::createBuffer(BufferKind kind,
                                                       std::size_t bytes,
                                                       const void* data)
{
    id<MTLBuffer> buf = nil;
    if (device_ && bytes > 0) {
        // Längden i hela fyrbytes (blittarnas krav); med data skrivs den
        // DIREKT — inget kommando kan referera en ny buffert — och resten
        // av fyrbytet nollas (`newBufferWithBytes:` hade läst förbi datan).
        const std::size_t len = alignUp(bytes, 4);
        buf = [device_ newBufferWithLength:len
                                   options:bufferStorage(unifiedMemory_)];
        if (buf && data) {
            auto* p = static_cast<std::uint8_t*>(buf.contents);
            std::memcpy(p, data, bytes);
            if (len > bytes)
                std::memset(p + bytes, 0, len - bytes);
            wroteDirect(buf, unifiedMemory_, 0, len);
        }
    }
    if (buf) {
        ++stats_.buffersCreated;
        if (data)
            stats_.bytesUploaded += bytes;
    } else {
        log("createBuffer: %zu byte kunde inte skapas", bytes);
    }
    return std::make_unique<MetalGpuBuffer>(kind, bytes, buf);
}

bool MetalRenderer::updateBuffer(GpuBuffer& b, std::size_t offset,
                                 const void* data, std::size_t bytes)
{
    // Bufferten kommer alltid ur den här backenden — casten är säker per
    // konstruktion, som drawText:s Font.
    auto& mb = static_cast<MetalGpuBuffer&>(b);
    if (!device_ || !updateOk(b, offset, data, bytes))
        return false;
    // offset och bytes är hela fyrbytes (updateOk) — blittarnas krav.
    id<MTLBuffer>    dst  = mb.buffer();
    const UploadPath path = uploadPathFor(mb.usedIn());
    if (path == UploadPath::Direct) {
        std::memcpy(static_cast<std::uint8_t*>(dst.contents) + offset, data,
                    bytes);
        wroteDirect(dst, unifiedMemory_, offset, bytes);
    } else {
        // En lokal pool runt commandbuffertarna och encoderna (Own har ingen
        // frame-pool runt sig).
        @autoreleasepool {
            const Staging s = stagingAlloc(bytes);
            if (!s.buf)
                return false;   // loggat
            std::memcpy(s.ptr, data, bytes);
            const bool                pass = path == UploadPath::Pass;
            id<MTLBlitCommandEncoder> blit =
                pass ? uploadEncoder((__bridge const void*)dst,
                                     UploadRect{offset, 0, offset + bytes, 1})
                     : beginBlit();
            [blit copyFromBuffer:s.buf
                    sourceOffset:s.offset
                        toBuffer:dst
               destinationOffset:offset
                            size:bytes];
            if (!pass)
                endBlit(blit);
        }
    }
    stats_.bytesUploaded += bytes;
    return true;
}

BufferSlice MetalRenderer::uploadTransient(BufferKind kind, const void* data,
                                           std::size_t bytes)
{
    // Ringens slot är bara känd inom en frame — en transient del gäller
    // ändå bara till framens slut.
    if (!cmd_ || !device_ || !data || bytes == 0)
        return {};
    MetalTransientRing& ring =
        transient_[ringIndex_][kind == BufferKind::Index ? 1 : 0];

    std::size_t at = alignUp(ring.offset, kTransientAlign);
    if (!ring.buf || at + bytes > ring.buf->size()) {
        // Ringen räcker inte för framen: en dubbelt så stor buffert. Den
        // gamla kan bära delar som redan lämnats ut — den lever tills
        // slotten används nästa gång.
        std::size_t cap = ring.buf ? ring.buf->size() * 2 : kTransientInitial;
        while (cap < bytes)
            cap *= 2;
        id<MTLBuffer> nb =
            [device_ newBufferWithLength:cap
                                 options:MTLResourceStorageModeShared];
        if (!nb) {
            log("uploadTransient: ringen kunde inte växa till %zu byte", cap);
            return {};
        }
        if (ring.buf)
            ring.retired.push_back(std::move(ring.buf));
        ring.buf = std::make_unique<MetalGpuBuffer>(kind, cap, nb);
        at       = 0;
    }
    std::memcpy(static_cast<std::uint8_t*>(ring.buf->buffer().contents) + at,
                data, bytes);
    ring.offset = at + bytes;
    stats_.bytesUploaded += bytes;
    return BufferSlice{ring.buf.get(), at, bytes};
}

// ---------------------------------------------------------------- ritning

// MSL-arrayens längd är skriven som en literal i källan.
static_assert(kMaxClipPlanes == 8,
              "SceneClip::planes i kMslClipSrc är deklarerad [8]");

// Målarens klipp på fragmentbuffert 4 — bundet när encodern saknar
// det eller anropets klipp skiljer från det bundna (metal_backend.h).
// Antalet har `meshDrawOk` prövat.
void MetalRenderer::bindSceneClip(const MeshDraw& d)
{
    const SceneClip& k = d.uniforms.clip;
    if (sceneClipBound_ && k == sceneClip_)
        return;
    SceneClipBuf b{};
    for (int i = 0; i < k.count; ++i) {
        b.planes[i][0] = k.planes[i].nx;
        b.planes[i][1] = k.planes[i].ny;
        b.planes[i][2] = k.planes[i].d;
    }
    b.meta[0] = static_cast<float>(k.count);
    [encoder_ setFragmentBytes:&b length:sizeof b atIndex:4];
    sceneClip_      = k;
    sceneClipBound_ = true;
}

void MetalRenderer::drawMesh(const MeshDraw& d)
{
    // STÄMPELN FÖRST, före förgreningen: varje program binder
    // sina buffertar och bilder här nedanför, och en uppladdning senare i
    // framen ska veta att de refererats (metal_backend.h `uploadPathFor`).
    noteUse(d);
    // De egna programmen bor i metal_program.mm.
    if (d.program == MeshProgram::Custom) {
        drawCustomMesh(d);
        return;
    }
    // Image-programmet bor bredvid bilderna (metal_image.mm).
    if (d.program == MeshProgram::Image) {
        drawImageMesh(d);
        return;
    }
    // Fill-programmet (ytorna) bor i metal_fill.mm, Glyph (texten) i
    // metal_glyph.mm.
    if (d.program == MeshProgram::Fill) {
        drawFillMesh(d);
        return;
    }
    if (d.program == MeshProgram::Glyph || d.program == MeshProgram::Glyph3) {
        drawGlyphMesh(d);   // också Glyph3
        return;
    }
    // Line och Line3 — samma källa, var sin PSO-familj.
    const bool     pos3 = d.program == MeshProgram::Line3;
    const Pipeline want = pos3 ? Pipeline::Line3 : Pipeline::Line;
    if (!encoder_ || !hasProgram(want) ||
        !meshDrawOk(d, pos3 || d.program == MeshProgram::Line))
        return;

    flush();   // det UI:t batchat före anropet ritas först — målarordningen
    // PSO-varianten för målet och anropets blandning.
    id<MTLRenderPipelineState> pso = psoFor(want, d.state.blend);
    if (!pso)
        return;   // loggat när varianten byggdes
    if (pipeline_ != want) {
        pipeline_ = want;
        countProgram(want);
    }
    setPso(pso);

    // Kameran för just den här chunken, i båda stegen — per anrop, räknas
    // inte som uppladdning (RenderStats).
    PaintU u{};
    std::memcpy(u.viewProj, d.uniforms.viewProj, sizeof u.viewProj);
    u.params[0] = static_cast<float>(targetW_);   // det BUNDNA målet —
    u.params[1] = static_cast<float>(targetH_);   // ett render-mål
    u.params[2] = d.uniforms.pxPerWidthUnit;
    u.params[3] = d.uniforms.widthSpace == WidthSpace::World ? 1.0f : 0.0f;
    fillPaint(u, d);   // färgen och opaciteten
    // Konturen: spetsgränsen och strecken (meshDrawOk har prövat dem).
    const SceneDash& dash = d.uniforms.dash;
    u.stroke[0] = d.uniforms.miterLimit;
    u.stroke[1] = static_cast<float>(dash.count);
    u.stroke[2] = dash.perWorldUnit;
    u.stroke[3] = dash.offset;
    std::memcpy(u.dash, dash.ends, sizeof u.dash);
    [encoder_ setVertexBytes:&u length:sizeof u atIndex:1];
    [encoder_ setFragmentBytes:&u length:sizeof u atIndex:1];
    bindSceneClip(d);
    bindPaintRamp(d);
    applyMeshState(d.state);   // renderläget

    const auto* vb = static_cast<const MetalGpuBuffer*>(d.vertices.buffer);
    const auto* ib = static_cast<const MetalGpuBuffer*>(d.indices.buffer);
    [encoder_ setVertexBuffer:vb->buffer() offset:d.vertices.offset atIndex:0];
    [encoder_ drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                         indexCount:d.indexCount
                          indexType:MTLIndexTypeUInt32
                        indexBuffer:ib->buffer()
                  indexBufferOffset:d.indices.offset];
    ++stats_.drawCalls;
    stats_.vertices += d.indexCount;
}

} // namespace gfx
