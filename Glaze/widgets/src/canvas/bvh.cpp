// Plockningens BVH — bygget. Kontraktet och traversalen
// står i bvh.h.

#include "bvh.h"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace glaze::detail {
namespace {

constexpr int           kBins    = 16;   // fack per axel i SAH-svepet
constexpr std::uint32_t kMaxLeaf = 8;    // fler trianglar än så delas alltid
// En nods kostnad mätt i triangelprov: att gå ner i den kostar ungefär ett.
constexpr double kTraversalCost = 1.0;

constexpr float kInf = std::numeric_limits<float>::infinity();

// En triangel under bygget: dess låda och plats i meshen.
struct Ref {
    float         lo[3];
    float         hi[3];
    std::uint32_t tri;
};

struct Box {
    float lo[3] = {kInf, kInf, kInf};
    float hi[3] = {-kInf, -kInf, -kInf};

    void grow(const float l[3], const float h[3])
    {
        for (int a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], l[a]);
            hi[a] = std::max(hi[a], h[a]);
        }
    }
    void grow(const Box& b) { grow(b.lo, b.hi); }

    // Halva ytan (konstanten tar ut sig i jämförelserna); 0 för en tom låda.
    double area() const
    {
        if (lo[0] > hi[0])
            return 0.0;
        const double x = static_cast<double>(hi[0]) - lo[0];
        const double y = static_cast<double>(hi[1]) - lo[1];
        const double z = static_cast<double>(hi[2]) - lo[2];
        return x * y + y * z + z * x;
    }
};

float centroid(const Ref& r, int a)
{
    return (r.lo[a] + r.hi[a]) * 0.5f;
}

// Facket för en mittpunkt — SAMMA räkning när facken räknas och när
// trianglarna delas, så delningen blir exakt den som vägdes.
int binOf(float c, double lo, double scale)
{
    const double b = (static_cast<double>(c) - lo) * scale;
    return std::clamp(static_cast<int>(b), 0, kBins - 1);
}

struct Task {
    std::uint32_t node, begin, end;
    int           depth;
};

} // namespace

Bvh buildBvh(std::span<const Float3> positions,
             std::span<const std::uint32_t> indices)
{
    Bvh               bvh;
    const std::size_t n3   = indices.empty() ? positions.size() : indices.size();
    const auto        nTri = static_cast<std::uint32_t>(n3 / 3);
    if (nTri == 0)
        return bvh;

    std::vector<Ref> refs(nTri);
    for (std::uint32_t t = 0; t < nTri; ++t) {
        Ref& r = refs[t];
        r.tri  = t;
        for (int a = 0; a < 3; ++a) {
            r.lo[a] = kInf;
            r.hi[a] = -kInf;
        }
        for (std::uint32_t k = 0; k < 3; ++k) {
            const std::size_t v = indices.empty()
                                      ? static_cast<std::size_t>(3) * t + k
                                      : indices[static_cast<std::size_t>(3) * t + k];
            const float p[3] = {positions[v].x, positions[v].y, positions[v].z};
            for (int a = 0; a < 3; ++a) {
                r.lo[a] = std::min(r.lo[a], p[a]);
                r.hi[a] = std::max(r.hi[a], p[a]);
            }
        }
    }

    bvh.nodes.push_back({});
    std::vector<Task> work{{0, 0, nTri, 0}};
    while (!work.empty()) {
        const Task w = work.back();
        work.pop_back();
        const std::uint32_t n = w.end - w.begin;

        // Nodens låda (trianglarnas) och mittpunkternas låda (delningens).
        Box b, cb;
        for (std::uint32_t i = w.begin; i < w.end; ++i) {
            b.grow(refs[i].lo, refs[i].hi);
            const float c[3] = {centroid(refs[i], 0), centroid(refs[i], 1),
                                centroid(refs[i], 2)};
            cb.grow(c, c);
        }
        BvhNode& node = bvh.nodes[w.node];
        for (int a = 0; a < 3; ++a) {
            node.lo[a] = b.lo[a];
            node.hi[a] = b.hi[a];
        }
        node.first = w.begin;
        node.count = n;   // ett löv tills en delning väljs
        if (n == 1)
            continue;

        std::uint32_t mid   = w.begin;
        bool          split = false;
        if (w.depth < kBvhSahDepth) {
            // BINNAD SAH: varje axel med utsträckning i 16 fack, och varje
            // gräns mellan två fack vägs — barnens area gånger deras antal.
            double bestCost = std::numeric_limits<double>::infinity();
            int    bestAxis = -1, bestBin = -1;
            for (int a = 0; a < 3; ++a) {
                const double ext = static_cast<double>(cb.hi[a]) - cb.lo[a];
                if (!(ext > 0.0))
                    continue;
                const double  scale = kBins / ext;
                Box           bins[kBins];
                std::uint32_t counts[kBins] = {};
                for (std::uint32_t i = w.begin; i < w.end; ++i) {
                    const int k = binOf(centroid(refs[i], a), cb.lo[a], scale);
                    bins[k].grow(refs[i].lo, refs[i].hi);
                    ++counts[k];
                }
                double        leftArea[kBins - 1];
                std::uint32_t leftCount[kBins - 1];
                Box           acc;
                std::uint32_t cnt = 0;
                for (int s = 0; s < kBins - 1; ++s) {
                    acc.grow(bins[s]);
                    cnt += counts[s];
                    leftArea[s]  = acc.area();
                    leftCount[s] = cnt;
                }
                acc = Box{};
                cnt = 0;
                for (int s = kBins - 1; s >= 1; --s) {
                    acc.grow(bins[s]);
                    cnt += counts[s];
                    const std::uint32_t nl = leftCount[s - 1];
                    if (nl == 0 || cnt == 0)
                        continue;
                    const double cost = leftArea[s - 1] * nl + acc.area() * cnt;
                    if (cost < bestCost) {
                        bestCost = cost;
                        bestAxis = a;
                        bestBin  = s - 1;
                    }
                }
            }
            if (bestAxis >= 0) {
                // Ett löv är billigare när n ≤ kTraversalCost + delningens
                // vägda antal — men fler än kMaxLeaf delas ändå.
                if (b.area() * (n - kTraversalCost) <= bestCost && n <= kMaxLeaf)
                    continue;
                const double ext =
                    static_cast<double>(cb.hi[bestAxis]) - cb.lo[bestAxis];
                const double scale = kBins / ext;
                const double lo    = cb.lo[bestAxis];
                const auto   it    = std::partition(
                    refs.begin() + w.begin, refs.begin() + w.end,
                    [&](const Ref& r) {
                        return binOf(centroid(r, bestAxis), lo, scale) <= bestBin;
                    });
                mid   = static_cast<std::uint32_t>(it - refs.begin());
                split = true;
            } else if (n <= kMaxLeaf) {
                continue;   // alla mittpunkter i en punkt: inget att dela på
            }
        } else if (n <= kMaxLeaf) {
            continue;
        }
        if (!split) {
            // MITT I på antalet, längs mittpunkternas längsta axel (lika
            // mittpunkter i indexordning) — djupets tak och de sammanfallande.
            int axis = 0;
            for (int a = 1; a < 3; ++a)
                if (cb.hi[a] - cb.lo[a] > cb.hi[axis] - cb.lo[axis])
                    axis = a;
            mid = w.begin + n / 2;
            std::nth_element(refs.begin() + w.begin, refs.begin() + mid,
                             refs.begin() + w.end,
                             [axis](const Ref& p, const Ref& q) {
                                 const float cp = centroid(p, axis);
                                 const float cq = centroid(q, axis);
                                 return cp < cq || (cp == cq && p.tri < q.tri);
                             });
        }

        const auto left = static_cast<std::uint32_t>(bvh.nodes.size());
        bvh.nodes.push_back({});
        bvh.nodes.push_back({});
        bvh.nodes[w.node].first = left;   // `node` kan ha flyttat med vektorn
        bvh.nodes[w.node].count = 0;
        work.push_back({left + 1, mid, w.end, w.depth + 1});
        work.push_back({left, w.begin, mid, w.depth + 1});
    }

    bvh.prims.resize(nTri);
    for (std::uint32_t i = 0; i < nTri; ++i)
        bvh.prims[i] = refs[i].tri;
    bvh.nodes.shrink_to_fit();
    return bvh;
}

} // namespace glaze::detail
