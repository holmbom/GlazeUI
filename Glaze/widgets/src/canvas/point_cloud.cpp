// glaze::PointCloud — punkterna, prövningen och den rumsliga delningen i
// chunkar. Ingen renderer här: uppladdningen är
// fönstrets. Kontraktet står i glaze/canvas/mesh.h; datan i mesh_data.h.

#include "glaze/canvas/mesh.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

#include "gfx/platform.h"
#include "mesh_data.h"

namespace glaze {
namespace detail {
namespace {

// Molnens id — atomiskt, konstruktorerna körs i vilken tråd som helst.
std::atomic<std::uint64_t> g_nextCloudId{1};

bool finite(const Point3& p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

double axisOf(const Point3& p, int a)
{
    return a == 0 ? p.x : a == 1 ? p.y : p.z;
}

// Lådan kring punkterna perm[begin, end) i världen.
template <class World>
Box3 boxOf(const std::vector<std::uint32_t>& perm, std::uint32_t begin,
           std::uint32_t end, const World& world)
{
    Point3 lo = world(perm[begin]), hi = lo;
    for (std::uint32_t i = begin + 1; i < end; ++i) {
        const Point3 p = world(perm[i]);
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    return Box3{lo, hi};
}

// Molnets data ur n punkter, där `world(i)` är punkt i i världen (double):
// DELAD rumsligt — mitt i längs lådans längsta axel (lika läge i
// indexordning, så delningen är densamma i varje standardbibliotek) tills
// varje del har högst `kPointChunkSize` punkter — och varje del i indatans
// ordning, med origot i sin låda.
template <class World>
std::shared_ptr<PointCloudData> buildCloud(std::size_t n, const World& world,
                                           std::vector<gfx::Color> colors,
                                           std::vector<float>      scalars)
{
    struct Span {
        std::uint32_t begin, end;
    };
    const auto                 count = static_cast<std::uint32_t>(n);
    std::vector<std::uint32_t> perm(count);
    std::iota(perm.begin(), perm.end(), 0u);
    std::vector<Span> leaves;
    std::vector<Span> work{{0, count}};
    while (!work.empty()) {
        const Span s = work.back();
        work.pop_back();
        if (s.end - s.begin <= kPointChunkSize) {
            leaves.push_back(s);
            continue;
        }
        const Box3   b = boxOf(perm, s.begin, s.end, world);
        const double ex[3] = {b.max.x - b.min.x, b.max.y - b.min.y,
                              b.max.z - b.min.z};
        const int axis = ex[0] >= ex[1] && ex[0] >= ex[2] ? 0 : ex[1] >= ex[2] ? 1 : 2;
        const std::uint32_t mid = s.begin + (s.end - s.begin) / 2;
        std::nth_element(perm.begin() + s.begin, perm.begin() + mid,
                         perm.begin() + s.end,
                         [&](std::uint32_t p, std::uint32_t q) {
                             const double a = axisOf(world(p), axis);
                             const double c = axisOf(world(q), axis);
                             return a < c || (a == c && p < q);
                         });
        work.push_back({mid, s.end});   // vänstra delen först ut: ordningen
        work.push_back({s.begin, mid}); // på chunkarna följer delningen
    }

    auto d = std::make_shared<PointCloudData>();
    d->id  = g_nextCloudId.fetch_add(1, std::memory_order_relaxed);
    d->positions.resize(count);
    if (!colors.empty())
        d->colors.resize(count);
    if (!scalars.empty())
        d->scalars.resize(count);
    bool first = true;
    for (const Span& s : leaves) {
        std::sort(perm.begin() + s.begin, perm.begin() + s.end);
        PointChunk c;
        c.bounds = boxOf(perm, s.begin, s.end, world);
        c.origin = {(c.bounds.min.x + c.bounds.max.x) / 2.0,
                    (c.bounds.min.y + c.bounds.max.y) / 2.0,
                    (c.bounds.min.z + c.bounds.max.z) / 2.0};
        c.first = s.begin;
        c.count = s.end - s.begin;
        for (std::uint32_t i = s.begin; i < s.end; ++i) {
            const std::uint32_t src = perm[i];
            const Point3        p   = world(src);
            d->positions[i] = {static_cast<float>(p.x - c.origin.x),
                               static_cast<float>(p.y - c.origin.y),
                               static_cast<float>(p.z - c.origin.z)};
            if (!colors.empty())
                d->colors[i] = colors[src];
            if (!scalars.empty())
                d->scalars[i] = scalars[src];
        }
        if (first) {
            d->bounds = c.bounds;
            first     = false;
        } else {
            d->bounds.min = {std::min(d->bounds.min.x, c.bounds.min.x),
                             std::min(d->bounds.min.y, c.bounds.min.y),
                             std::min(d->bounds.min.z, c.bounds.min.z)};
            d->bounds.max = {std::max(d->bounds.max.x, c.bounds.max.x),
                             std::max(d->bounds.max.y, c.bounds.max.y),
                             std::max(d->bounds.max.z, c.bounds.max.z)};
        }
        d->chunks.push_back(c);
    }
    if (count > kPointChunkSize)
        d->order = std::move(perm);
    return d;
}

// Antalen, som båda konstruktorerna prövar. Ett avslag loggas här, direkt.
bool countsOk(std::size_t n, std::size_t colors, std::size_t scalars)
{
    if (n > std::numeric_limits<std::uint32_t>::max()) {
        gfx::log("PointCloud: %zu punkter är fler än uint32 räknar — inget moln",
                 n);
        return false;
    }
    if ((colors != 0 && colors != n) || (scalars != 0 && scalars != n)) {
        gfx::log("PointCloud: %zu färger och %zu skalärer till %zu punkter — de "
                 "ska vara tomma eller en per punkt; inget moln",
                 colors, scalars, n);
        return false;
    }
    return true;
}

} // namespace
} // namespace detail

PointCloud::PointCloud(std::vector<Float3> local, Point3 origin,
                       std::vector<gfx::Color> colors,
                       std::vector<float>      scalars)
{
    const std::size_t n = local.size();
    if (n == 0)
        return;   // tomt är inget fel
    if (!detail::countsOk(n, colors.size(), scalars.size()))
        return;   // loggat
    if (!detail::finite(origin) ||
        !std::all_of(local.begin(), local.end(), [](const Float3& p) {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        })) {
        gfx::log("PointCloud: ett läge eller origot är inte ändligt — inget moln "
                 "(%zu punkter)",
                 n);
        return;
    }
    const auto world = [&](std::uint32_t i) {
        return Point3{origin.x + local[i].x, origin.y + local[i].y,
                      origin.z + local[i].z};
    };
    d_ = detail::buildCloud(n, world, std::move(colors), std::move(scalars));
}

PointCloud PointCloud::fromWorld(std::span<const Point3>  points,
                                 std::vector<gfx::Color> colors,
                                 std::vector<float>      scalars)
{
    PointCloud        c;
    const std::size_t n = points.size();
    if (n == 0)
        return c;
    if (!detail::countsOk(n, colors.size(), scalars.size()))
        return c;   // loggat
    if (!std::all_of(points.begin(), points.end(),
                     [](const Point3& p) { return detail::finite(p); })) {
        gfx::log("PointCloud::fromWorld: en punkt är inte ändlig — inget moln "
                 "(%zu punkter)",
                 n);
        return c;
    }
    const auto world = [&](std::uint32_t i) { return points[i]; };
    c.d_ = detail::buildCloud(n, world, std::move(colors), std::move(scalars));
    return c;
}

std::size_t PointCloud::pointCount() const
{
    return d_ ? d_->positions.size() : 0;
}

Box3 PointCloud::bounds() const
{
    return d_ ? d_->bounds : Box3{};
}

} // namespace glaze
