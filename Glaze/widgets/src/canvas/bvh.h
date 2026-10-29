#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include "glaze/canvas/geometry.h"

// INTERN — PLOCKNINGENS BVH: en hierarki av lådor över en
// meshs trianglar, så att en stråle bara prövar de trianglar vars lådor den
// når. Byggd med BINNAD SAH (ytans heuristik: en delning kostar barnens area
// gånger deras trianglar, och den billigaste av 16 fack per axel vinns), i
// meshens LOKALA rymd — lägena är Float3 relativt meshens origo, och lådorna
// är exakt lägenas min och max (ingen avrundning).
//
// Ren CPU-kod utan renderer och utan widget: byggs ur lägena och indexen och
// läses av plockningen (pick3d.h). Trådsäker att BYGGA i vilken tråd som
// helst; meshen håller den (mesh_data.h — byggd lat, en gång).
//
// DJUPET är begränsat: efter `kBvhSahDepth` nivåer delas resten mitt i (på
// antalet), så trädet aldrig blir djupare än `kBvhMaxDepth` hur
// patologiskt lägena än ligger — en SAH-kedja som skalar av en triangel i
// taget hade annars gett ett träd lika djupt som meshen är stor, och
// traversalens stack är fast.

namespace glaze::detail {

inline constexpr int kBvhSahDepth = 64;
inline constexpr int kBvhMaxDepth = 128;   // > kBvhSahDepth + 32 (2³² trianglar halverade)

struct BvhNode {
    float lo[3];
    float hi[3];
    // LÖV (count > 0): trianglarna `prims[first … first + count)`.
    // INRE (count == 0): barnen är noderna `first` och `first + 1`.
    std::uint32_t first;
    std::uint32_t count;
};

struct Bvh {
    std::vector<BvhNode>       nodes;   // [0] är roten; tomt = inga trianglar
    std::vector<std::uint32_t> prims;   // triangelindex i meshens ordning
};

// Bygger hierarkin. `indices` tomt = icke-indexerat (triangel t är lägena 3t,
// 3t + 1, 3t + 2); annars är triangel t indexen 3t … 3t + 2.
Bvh buildBvh(std::span<const Float3> positions,
             std::span<const std::uint32_t> indices);

// STRÅLENS PROV MOT EN NODS LÅDA, robust: ett plattprov i double där en
// gräns som blir NaN (0 · ∞ — strålen ligger I lådans sidoplan, alltså på
// kanten) inte begränsar något, och där utgången skjuts ut med 1 + 2γ₃ (Ize,
// "Robust BVH Ray Traversal") så att avrundningen aldrig missar en låda
// strålen snuddar. `inv` är riktningens inverterade komponenter (±∞ för en
// komponent som är noll). Svaret: strålen når lådan inom [0, tMax];
// `tEnter` får var den går in.
inline bool rayHitsNode(const BvhNode& n, const double o[3],
                        const double inv[3], double tMax, double& tEnter)
{
    constexpr double kSlack = 1.0 + 2.0 * (3.0 * 0x1p-53 / (1.0 - 3.0 * 0x1p-53));
    double t0 = 0.0, t1 = tMax;
    for (int a = 0; a < 3; ++a) {
        double tn = (static_cast<double>(n.lo[a]) - o[a]) * inv[a];
        double tf = (static_cast<double>(n.hi[a]) - o[a]) * inv[a];
        if (tn > tf)
            std::swap(tn, tf);
        // En jämförelse med NaN är falsk — gränsen lämnas då orörd.
        t0 = tn > t0 ? tn : t0;
        t1 = tf < t1 ? tf : t1;
    }
    tEnter = t0;
    return t0 <= t1 * kSlack;
}

// GÅR STRÅLEN (o + t · d, lokala koordinater) GENOM HIERARKIN: varje löv vars
// låda strålen når inom [0, tMax] lämnar sina trianglar till
// `visit(triangel, tMax)`, närmaste barnet först. Besökaren får KRYMPA tMax
// (närmaste träffen hittills) — lådor bortom den prövas inte mer. En låda som
// börjar EXAKT på tMax besöks ändå, så att en träff på samma avstånd kan
// avgöras av besökaren (lika avstånd: lägst triangelindex).
template <class Visit>
void traverseRay(const Bvh& bvh, const double o[3], const double d[3],
                 double tMax, Visit&& visit)
{
    if (bvh.nodes.empty())
        return;
    const double inv[3] = {1.0 / d[0], 1.0 / d[1], 1.0 / d[2]};
    double       enter  = 0.0;
    if (!rayHitsNode(bvh.nodes[0], o, inv, tMax, enter))
        return;
    std::uint32_t stack[kBvhMaxDepth];
    int           sp = 0;
    std::uint32_t ni = 0;
    for (;;) {
        const BvhNode& n = bvh.nodes[ni];
        if (n.count > 0) {
            for (std::uint32_t k = n.first; k < n.first + n.count; ++k)
                visit(bvh.prims[k], tMax);
        } else {
            double     ta = 0.0, tb = 0.0;
            const bool ha = rayHitsNode(bvh.nodes[n.first], o, inv, tMax, ta);
            const bool hb =
                rayHitsNode(bvh.nodes[n.first + 1], o, inv, tMax, tb);
            if (ha && hb) {
                const bool aFirst = ta <= tb;
                stack[sp++]       = aFirst ? n.first + 1 : n.first;
                ni                = aFirst ? n.first : n.first + 1;
                continue;
            }
            if (ha || hb) {
                ni = ha ? n.first : n.first + 1;
                continue;
            }
        }
        // Nästa på stacken — prövad på nytt, tMax kan ha krympt sedan den
        // lades dit.
        bool next = false;
        while (sp > 0 && !next) {
            ni   = stack[--sp];
            next = rayHitsNode(bvh.nodes[ni], o, inv, tMax, enter);
        }
        if (!next)
            return;
    }
}

// STRÅLENS NÄRHET (skärmtoleranserna, hörn och kanter
// inom N px): varje löv vars låda kan ha en punkt inom avståndet `reach(r)`
// från strålen (o + t · d, t ≥ 0) lämnar sina trianglar till
// `visit(triangel)`, där r är det största avståndet från strålens början
// till lådans hörn. En toleranskon kring strålen — perspektivets räckvidd
// växer med avståndet — ryms då i lådan uppblåst med `reach(r)` åt alla
// håll: en punkt p i lådan inom räckvidden från strålen har strålen inom
// reach(|p − o|) ≤ reach(r) från sig (`reach` växande), och den kulan ligger
// i den uppblåsta lådan. KONSERVATIV — den som besöks prövas exakt av
// besökaren. Ordningen är djupet först; ingenting krymps.
template <class Reach, class Visit>
void traverseNear(const Bvh& bvh, const double o[3], const double d[3],
                  Reach&& reach, Visit&& visit)
{
    if (bvh.nodes.empty())
        return;
    constexpr float kInfF   = std::numeric_limits<float>::infinity();
    const double    inv[3]  = {1.0 / d[0], 1.0 / d[1], 1.0 / d[2]};
    const double    utanTak = std::numeric_limits<double>::infinity();
    const auto      nära    = [&](const BvhNode& n) {
        double r2 = 0.0;
        for (int k = 0; k < 8; ++k) {
            double s = 0.0;
            for (int a = 0; a < 3; ++a) {
                const double c = ((k >> a) & 1) ? n.hi[a] : n.lo[a];
                s += (c - o[a]) * (c - o[a]);
            }
            r2 = s > r2 ? s : r2;
        }
        // Uppblåst i double och avrundad UTÅT till float, så avrundningen
        // aldrig krymper lådan.
        const double R = reach(std::sqrt(r2));
        BvhNode      u = n;
        for (int a = 0; a < 3; ++a) {
            u.lo[a] = std::nextafter(static_cast<float>(n.lo[a] - R), -kInfF);
            u.hi[a] = std::nextafter(static_cast<float>(n.hi[a] + R), kInfF);
        }
        double enter = 0.0;
        return rayHitsNode(u, o, inv, utanTak, enter);
    };
    if (!nära(bvh.nodes[0]))
        return;
    // Djupet först med en FAST stack: en nod tas ut och dess (högst två)
    // barn läggs dit, så stacken växer med högst en post per nivå — och
    // trädet är aldrig djupare än `kBvhMaxDepth` − 1 (bygget lovar det).
    std::uint32_t stack[kBvhMaxDepth];
    int           sp = 0;
    stack[sp++]      = 0;
    while (sp > 0) {
        const BvhNode& n = bvh.nodes[stack[--sp]];
        if (n.count > 0) {
            for (std::uint32_t k = n.first; k < n.first + n.count; ++k)
                visit(bvh.prims[k]);
            continue;
        }
        for (std::uint32_t c = n.first; c < n.first + 2; ++c)
            if (nära(bvh.nodes[c]))
                stack[sp++] = c;
    }
}

} // namespace glaze::detail
