// Instansernas celler — kontraktet och reglerna står i
// instances.h.

#include "instances.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <tuple>

#include <gfx/platform.h>

#include "scene.h"         // kChunkCell — blockens storlek
#include "transform3d.h"

namespace glaze::detail {

namespace {

std::atomic<std::uint64_t> g_nextSetId{1};

// Morton-kodens 21 bitar per axel: varje bit tre platser isär.
std::uint64_t spread3(std::uint64_t v)
{
    std::uint64_t x = v & 0x1fffffull;
    x = (x | x << 32) & 0x1f00000000ffffull;
    x = (x | x << 16) & 0x1f0000ff0000ffull;
    x = (x | x << 8) & 0x100f00f00f00f00full;
    x = (x | x << 4) & 0x10c30c30c30c30c3ull;
    x = (x | x << 2) & 0x1249249249249249ull;
    return x;
}

std::uint64_t morton3(std::uint64_t x, std::uint64_t y, std::uint64_t z)
{
    return spread3(x) | spread3(y) << 1 | spread3(z) << 2;
}

bool finite(const Instance& i)
{
    return std::isfinite(i.at.x) && std::isfinite(i.at.y) &&
           std::isfinite(i.at.z) && std::isfinite(i.yaw) &&
           std::isfinite(i.pitch) && std::isfinite(i.roll) &&
           std::isfinite(i.scale.x) && std::isfinite(i.scale.y) &&
           std::isfinite(i.scale.z);
}

void grow(Box3& b, const Box3& o, bool& first)
{
    if (first) {
        b     = o;
        first = false;
        return;
    }
    b.min = {std::min(b.min.x, o.min.x), std::min(b.min.y, o.min.y),
             std::min(b.min.z, o.min.z)};
    b.max = {std::max(b.max.x, o.max.x), std::max(b.max.y, o.max.y),
             std::max(b.max.z, o.max.z)};
}

// Cellens sida (instances.h): de utsträckta axlarnas produkt delad på antalet
// celler, och en axel kortare än sidan räknas inte.
double cellSide(const double (&ext)[3], std::size_t count)
{
    const double cells =
        std::max(1.0, std::ceil(static_cast<double>(count) /
                                static_cast<double>(kInstancesPerCell)));
    bool   use[3] = {ext[0] > 0.0, ext[1] > 0.0, ext[2] > 0.0};
    double s      = 0.0;
    for (int varv = 0; varv < 3; ++varv) {
        int    d    = 0;
        double prod = 1.0;
        for (int a = 0; a < 3; ++a)
            if (use[a]) {
                prod *= ext[a];
                ++d;
            }
        if (d == 0)
            return kChunkCell;   // alla på en punkt: en cell
        s            = std::pow(prod / cells, 1.0 / d);
        bool kortare = false;
        for (int a = 0; a < 3; ++a)
            if (use[a] && ext[a] < s) {
                use[a]  = false;
                kortare = true;
            }
        if (!kortare)
            break;
    }
    // Inom blocket, och så att en cellkoordinat i blocket ryms i Morton-
    // kodens 21 bitar.
    return std::clamp(std::isfinite(s) && s > 0.0 ? s : kChunkCell,
                      kChunkCell / static_cast<double>(1u << 20), kChunkCell);
}

} // namespace

std::shared_ptr<const InstanceSet> buildInstanceSet(const Box3& proto,
                                                    std::span<const Instance> inst)
{
    auto set = std::make_shared<InstanceSet>();
    set->id  = g_nextSetId.fetch_add(1, std::memory_order_relaxed);

    std::vector<std::uint32_t> ok;
    ok.reserve(inst.size());
    for (std::size_t i = 0; i < inst.size(); ++i)
        if (finite(inst[i]))
            ok.push_back(static_cast<std::uint32_t>(i));
    if (ok.size() != inst.size())
        gfx::log("Shape::instances: %zu av %zu instanser är inte ändliga (läget, "
                 "vinklarna eller skalan) — de hoppas över",
                 inst.size() - ok.size(), inst.size());
    if (ok.empty())
        return set;

    // Lägenas utbredning ger cellens sida och blockens raster.
    Point3 lo = inst[ok.front()].at, hi = lo;
    for (std::uint32_t i : ok) {
        const Point3& p = inst[i].at;
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    const double ext[3] = {hi.x - lo.x, hi.y - lo.y, hi.z - lo.z};
    const double s      = cellSide(ext, ok.size());
    const auto   perBlock =
        std::max<std::int64_t>(1, static_cast<std::int64_t>(std::floor(kChunkCell / s)));
    set->cellSize = s;

    struct Nyckel {
        std::int64_t  b[3];   // blocket
        std::int64_t  c[3];   // cellen (global)
        std::uint64_t morton;
        std::uint32_t idx;
    };
    std::vector<Nyckel> nycklar;
    nycklar.reserve(ok.size());
    const double o[3] = {lo.x, lo.y, lo.z};
    for (std::uint32_t i : ok) {
        const double p[3] = {inst[i].at.x, inst[i].at.y, inst[i].at.z};
        Nyckel       k{};
        for (int a = 0; a < 3; ++a) {
            k.c[a] = static_cast<std::int64_t>(std::floor((p[a] - o[a]) / s));
            k.b[a] = k.c[a] / perBlock;
        }
        k.morton = morton3(static_cast<std::uint64_t>(k.c[0] - k.b[0] * perBlock),
                           static_cast<std::uint64_t>(k.c[1] - k.b[1] * perBlock),
                           static_cast<std::uint64_t>(k.c[2] - k.b[2] * perBlock));
        k.idx    = i;
        nycklar.push_back(k);
    }
    std::sort(nycklar.begin(), nycklar.end(), [](const Nyckel& a, const Nyckel& b) {
        return std::tie(a.b[0], a.b[1], a.b[2], a.morton, a.idx) <
               std::tie(b.b[0], b.b[1], b.b[2], b.morton, b.idx);
    });

    set->rows.reserve(nycklar.size());
    set->order.reserve(nycklar.size());
    const double blockSide = static_cast<double>(perBlock) * s;
    bool         förstaLåda = true;
    for (std::size_t n = 0; n < nycklar.size(); ++n) {
        const Nyckel& k   = nycklar[n];
        const bool    nyttBlock =
            n == 0 || k.b[0] != nycklar[n - 1].b[0] ||
            k.b[1] != nycklar[n - 1].b[1] || k.b[2] != nycklar[n - 1].b[2];
        if (nyttBlock)   // blockets mitt — förflyttningarna högst ett halvt block
            set->blocks.push_back(
                Point3{o[0] + (static_cast<double>(k.b[0]) + 0.5) * blockSide,
                       o[1] + (static_cast<double>(k.b[1]) + 0.5) * blockSide,
                       o[2] + (static_cast<double>(k.b[2]) + 0.5) * blockSide});
        const bool nyCell = nyttBlock || k.c[0] != nycklar[n - 1].c[0] ||
                            k.c[1] != nycklar[n - 1].c[1] ||
                            k.c[2] != nycklar[n - 1].c[2];
        if (nyCell) {
            InstanceCell c;
            c.first = static_cast<std::uint32_t>(set->rows.size());
            c.block = static_cast<std::uint32_t>(set->blocks.size() - 1);
            set->cells.push_back(c);
        }
        const Instance& in     = inst[k.idx];
        const Point3&   origin = set->blocks.back();
        // Raden relativt blockets origo; lådan i världen ur samma matris.
        const Affine3 rel  = rotationScale(in.yaw, in.pitch, in.roll, in.scale,
                                           in.at.x - origin.x, in.at.y - origin.y,
                                           in.at.z - origin.z);
        const Affine3 värld = rotationScale(in.yaw, in.pitch, in.roll, in.scale,
                                            in.at.x, in.at.y, in.at.z);
        MeshInstance mi;
        rel.toRows(mi.rows);
        mi.color = in.color;
        set->rows.push_back(mi);
        set->order.push_back(k.idx);
        set->maxScale = std::max({set->maxScale, std::abs(double(in.scale.x)),
                                  std::abs(double(in.scale.y)),
                                  std::abs(double(in.scale.z))});

        InstanceCell& c       = set->cells.back();
        const Box3    låda    = transformBox(värld, proto);
        bool          förstaC = c.count == 0;
        bool          förstaA = c.count == 0;
        grow(c.box, låda, förstaC);
        grow(c.anchors, Box3{in.at, in.at}, förstaA);
        grow(set->bounds, låda, förstaLåda);
        ++c.count;
    }
    return set;
}

} // namespace glaze::detail
