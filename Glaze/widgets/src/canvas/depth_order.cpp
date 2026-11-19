// DJUPORDNINGEN (depth_order.h).

#include "depth_order.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>

#include "mesh_data.h"
#include "run_index.h"
#include "scene.h"
#include "spatial_index.h"

namespace glaze::detail {

namespace {

// Föremålens högsta rang (depth_order.h).
constexpr int kObjectRankMax = kDepthRankMax - 1;

// Två lådor ligger an när de möts inom den här andelen av den störres
// längsta sida — meshens lägen är float kring ett origo i double.
constexpr double kTouchShare = 1e-5;

// Axlarna där lådorna måste överlappa för att de ska dela en YTA.
constexpr int kTouchAxes = 2;

std::atomic<std::uint64_t> g_builds{0};

// Ytorna som kan ligga i samma plan som en annan: mesh, yta i rymden och
// instanser. Punkter och helskärmspass har ingen rang.
bool isSurface(const Shape& s)
{
    const Shape::Kind k = s.kind();
    return k == Shape::Kind::Mesh || k == Shape::Kind::Area3 ||
           k == Shape::Kind::Instances;
}

double extent(const Box3& b)
{
    return std::max({b.max.x - b.min.x, b.max.y - b.min.y, b.max.z - b.min.z});
}

// Ligger lådorna an mot varandra med en yta? De möts på alla tre axlarna
// (inom `eps`) och överlappar på minst två.
bool touching(const Box3& a, const Box3& b, double eps)
{
    const double lo[3] = {std::max(a.min.x, b.min.x), std::max(a.min.y, b.min.y),
                          std::max(a.min.z, b.min.z)};
    const double hi[3] = {std::min(a.max.x, b.max.x), std::min(a.max.y, b.max.y),
                          std::min(a.max.z, b.max.z)};
    int wide = 0;
    for (int i = 0; i < 3; ++i) {
        const double len = hi[i] - lo[i];
        if (len < -eps)
            return false;
        if (len > eps)
            ++wide;
    }
    return wide >= kTouchAxes;
}

// Lagrets signatur: körningarna, och för varje föremål dess signatur.
std::uint64_t signatureOf(const LayerData& L)
{
    KeyHash k;
    k.mix(L.runs.size());
    for (const std::shared_ptr<RunData>& run : L.runs) {
        k.mix(static_cast<std::uint64_t>(run->key.kind));
        k.mix(run->objects.size());
        for (const std::shared_ptr<ShapeEntry>& e : run->objects)
            k.mix(e->alive && isSurface(e->shape) ? objectSignature(*e) : 0);
    }
    return k.h;
}

std::shared_ptr<const DepthOrder> build(const LayerData& L)
{
    auto order = std::make_shared<DepthOrder>();
    order->objects.resize(L.runs.size());
    order->runs.assign(L.runs.size(), 0);

    SpatialIndex              tree;
    std::vector<Box3>         boxes;   // de inlagda, i trädets numrering
    std::vector<std::uint8_t> ranks;
    double                    widest = 0.0;   // de inlagdas längsta sida
    bool                      any    = false; // en yta står före i lagret
    int                       top    = 0;     // ytornas högsta rang hittills

    for (std::size_t ri = 0; ri < L.runs.size(); ++ri) {
        const RunData& run = *L.runs[ri];
        if (!isObjectKind(run.key.kind)) {
            // Det som ritas efter ytorna får rangen över dem.
            if (any)
                order->runs[ri] = static_cast<std::uint8_t>(top + 1);
            continue;
        }
        std::vector<std::uint8_t>& ut = order->objects[ri];
        ut.assign(run.objects.size(), 0);
        for (std::size_t i = 0; i < run.objects.size(); ++i) {
            const ShapeEntry& e = *run.objects[i];
            if (!e.alive || !isSurface(e.shape))
                continue;
            any = true;
            const std::optional<Box3> b = objectWorldBox(e.shape);
            if (!b)
                continue;   // skärmkonstant: ingen låda i världen, rang 0
            // Grannarna: de tidigare ytorna vars låda ligger an mot den här.
            const double  own    = extent(*b);
            const double  reach  = kTouchShare * std::max(own, widest);
            std::uint32_t used   = 0;
            int           högst  = -1;
            tree.query(
                [&](const SpatialIndex::Box& n) {
                    return n.minX <= b->max.x + reach && n.maxX >= b->min.x - reach &&
                           n.minY <= b->max.y + reach && n.maxY >= b->min.y - reach &&
                           n.minZ <= b->max.z + reach && n.maxZ >= b->min.z - reach;
                },
                [&](std::uint32_t j) {
                    const double eps = kTouchShare * std::max(own, extent(boxes[j]));
                    if (!touching(*b, boxes[j], eps))
                        return;
                    used |= std::uint32_t{1} << ranks[j];
                    högst = std::max(högst, static_cast<int>(ranks[j]));
                });
            // Över alla grannar — eller, när rangerna är slut, den högsta
            // lediga bland dem.
            int rang = högst + 1;
            if (rang > kObjectRankMax) {
                rang = kObjectRankMax;
                for (int r = kObjectRankMax; r >= 0; --r)
                    if (!(used & (std::uint32_t{1} << r))) {
                        rang = r;
                        break;
                    }
            }
            ut[i] = static_cast<std::uint8_t>(rang);
            top   = std::max(top, rang);
            tree.insert(static_cast<std::uint32_t>(boxes.size()),
                        SpatialIndex::Box{b->min.x, b->min.y, b->min.z, b->max.x,
                                          b->max.y, b->max.z});
            boxes.push_back(*b);
            ranks.push_back(static_cast<std::uint8_t>(rang));
            widest = std::max(widest, own);
        }
    }
    order->top = top;
    for (std::uint8_t r : order->runs)
        order->top = std::max(order->top, static_cast<int>(r));
    g_builds.fetch_add(1);
    return order;
}

} // namespace

std::shared_ptr<const DepthOrder> depthOrder(const LayerData& L)
{
    const std::lock_guard<std::mutex> vakt(nodeLock(L.id));
    const std::uint64_t               gen = meshGeneration();
    if (L.depth && L.depthShapes == L.shapesVersion && L.depthMeshes == gen)
        return L.depth;
    // Formerna eller någon mesh har ändrats — men ordningen gäller så länge
    // lagrets egna föremål är desamma.
    const std::uint64_t sig = signatureOf(L);
    if (!L.depth || L.depthSig != sig) {
        L.depth    = build(L);
        L.depthSig = sig;
    }
    L.depthShapes = L.shapesVersion;
    L.depthMeshes = gen;
    return L.depth;
}

int slotRank(const LayerData& L, const DepthOrder& order, const ShapeSlot& slot)
{
    if (!slot.objectRun)
        return 0;
    for (std::size_t ri = 0; ri < L.runs.size() && ri < order.objects.size(); ++ri)
        if (L.runs[ri].get() == slot.objectRun)
            return slot.objectPos < order.objects[ri].size()
                       ? order.objects[ri][slot.objectPos]
                       : 0;
    return 0;
}

std::uint64_t depthOrderBuilds()
{
    return g_builds.load();
}

} // namespace glaze::detail
