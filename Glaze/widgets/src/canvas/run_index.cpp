// KÖRNINGENS RUMSLIGA INDEX (run_index.h).

#include "run_index.h"

#include <algorithm>
#include <memory>
#include <optional>

#include "build.h"
#include "camera_math.h"
#include "mesh_data.h"
#include "scene.h"
#include "scene3d.h"

namespace glaze::detail {

namespace {

RunIndex& indexOf(const RunData& run)
{
    if (!run.index)
        run.index = std::make_shared<RunIndex>();
    return *run.index;
}

// Har föremålet en världslåda att gallra på? Samma undantag som ritningens
// `objectInView`: en skärmkonstant mesh (lådan är origot) och ett föremål
// utan låda syns alltid.
std::optional<Box3> worldBox(const Shape& s)
{
    const Shape::Kind k = s.kind();
    if ((k == Shape::Kind::Mesh || k == Shape::Kind::Area3 || k == Shape::Kind::Instances) &&
        s.meshStyle().size.value > 0.0f)
        return std::nullopt;
    return objectBox(s);
}

// Signaturens bit för "platsen har en post" — signaturen 0 betyder ingen.
constexpr std::uint64_t kSigPresent = 1;

// Föremålets signatur: platsens id, formens version och datans (en mesh som
// flyttat sina lägen har en annan låda).
std::uint64_t objectSig(const ShapeEntry& e)
{
    KeyHash k;
    k.mix(e.id);
    k.mix(e.version);
    const Shape&      s    = e.shape;
    const Shape::Kind kind = s.kind();
    if (kind == Shape::Kind::Mesh || kind == Shape::Kind::Area3 ||
        kind == Shape::Kind::Instances)
        if (const std::shared_ptr<MeshData>& d = MeshAccess::data(s.mesh())) {
            k.mix(d->id);
            k.mix(d->version.load());
        }
    return k.h | kSigPresent;
}

std::uint64_t chunkSig(const ChunkData& ch)
{
    KeyHash k;
    k.mix(ch.id);
    k.mix(ch.version);
    return k.h | kSigPresent;
}

// Har de kvarliggande posterna blivit för många mot de levande?
bool tooStale(std::size_t stale, std::size_t live)
{
    return static_cast<double>(stale) > kIndexStaleShare * static_cast<double>(live);
}

RunIndex& objectIndex(const RunData& run, std::uint64_t shapesVersion)
{
    RunIndex&           ix  = indexOf(run);
    const std::uint64_t gen = meshGeneration();
    if (ix.objectsBuilt && ix.objectShapes == shapesVersion && ix.objectMeshes == gen)
        return ix;
    const std::size_t n    = run.objects.size();
    const bool        träd = n >= kRunIndexMin;
    // FRÅN BÖRJAN: första gången, en körning som krympt (platserna har
    // numrerats om), en kort körning (z-spannet ska vara exakt och bygget är
    // billigt) eller för många kvarliggande poster.
    const bool från0 = !ix.objectsBuilt || !träd || !ix.objectTree ||
                       n < ix.objectSig.size() ||
                       tooStale(ix.objectStale, ix.objects.size());
    if (från0) {
        ix.objects.clear();
        ix.objectSig.assign(n, 0);
        ix.objectAlways.assign(n, 0);
        ix.objectStale = 0;
        ix.zMin = ix.zMax = 0.0;
        ++ix.rebuilds;
    } else {
        ix.objectSig.resize(n, 0);
        ix.objectAlways.resize(n, 0);
    }
    for (std::size_t i = 0; i < n; ++i) {
        const ShapeEntry&   e   = *run.objects[i];
        const std::uint64_t sig = e.alive ? objectSig(e) : 0;
        if (sig == ix.objectSig[i])
            continue;   // oförändrad — posten står kvar
        // Platsens gamla post ligger kvar i trädet (det redigeras inte).
        if (ix.objectSig[i] != 0 && !ix.objectAlways[i])
            ++ix.objectStale;
        ix.objectSig[i]    = sig;
        ix.objectAlways[i] = 0;
        if (sig == 0)
            continue;
        const auto                nr = static_cast<std::uint32_t>(i);
        const std::optional<Box3> b  = objectBox(e.shape);
        if (b) {
            ix.zMin = std::min(ix.zMin, b->min.z);
            ix.zMax = std::max(ix.zMax, b->max.z);
        }
        if (!träd)
            continue;   // en kort körning: z-spannet räcker
        if (const std::optional<Box3> w = worldBox(e.shape))
            ix.objects.insert(nr, SpatialIndex::Box{w->min.x, w->min.y, w->min.z,
                                                    w->max.x, w->max.y, w->max.z});
        else
            ix.objectAlways[i] = 1;
        ++ix.inserted;
    }
    ix.always.clear();
    for (std::size_t i = 0; i < n; ++i)
        if (ix.objectAlways[i])
            ix.always.push_back(static_cast<std::uint32_t>(i));
    ix.objectTree   = träd;
    ix.objectShapes = shapesVersion;
    ix.objectMeshes = gen;
    ix.objectsBuilt = true;
    ++ix.builds;
    return ix;
}

} // namespace

void chunkCandidates(const PaintContext& f, const RunData& run,
                     std::uint64_t shapesVersion, float slackPx,
                     std::vector<std::uint32_t>& out)
{
    out.clear();
    const auto ritbar = [](const ChunkData& ch) { return !ch.indices.empty() && ch.hasBox; };
    if (run.chunks.size() < kRunIndexMin) {
        for (std::size_t i = 0; i < run.chunks.size(); ++i)
            if (ritbar(*run.chunks[i]))
                out.push_back(static_cast<std::uint32_t>(i));
        return;
    }
    // Indexet är nodens cache — läst och byggt under körningens lås.
    const std::lock_guard<std::mutex> vakt(nodeLock(run.id));
    RunIndex& ix = indexOf(run);
    if (!ix.chunksBuilt || ix.chunkShapes != shapesVersion) {
        // Ikapp som föremålen (run_index.h): bara chunkarna vars signatur
        // ändrats läggs in på nytt — från början första gången, när
        // körningen krympt eller när för många gamla poster ligger kvar.
        const std::size_t n = run.chunks.size();
        if (!ix.chunksBuilt || n < ix.chunkSig.size() ||
            tooStale(ix.chunkStale, ix.chunks.size())) {
            ix.chunks.clear();
            ix.chunkSig.assign(n, 0);
            ix.chunkStale  = 0;
            ix.chunkMargin = 0.0f;
        } else {
            ix.chunkSig.resize(n, 0);
        }
        for (std::size_t i = 0; i < n; ++i) {
            const ChunkData&    ch  = *run.chunks[i];
            const std::uint64_t sig = ritbar(ch) ? chunkSig(ch) : 0;
            if (sig == ix.chunkSig[i])
                continue;
            if (ix.chunkSig[i] != 0)
                ++ix.chunkStale;
            ix.chunkSig[i] = sig;
            if (sig == 0)
                continue;
            ix.chunks.insert(static_cast<std::uint32_t>(i),
                             SpatialIndex::Box{ch.minX, ch.minY, ch.minZ, ch.maxX,
                                               ch.maxY, ch.maxZ});
            ix.chunkMargin = std::max(ix.chunkMargin, ch.margin);
        }
        ix.chunkShapes = shapesVersion;
        ix.chunksBuilt = true;
    }
    // Nodens marginal: körningens största — en linje i px eller mm når sin
    // halva bredd utanför lådan (`chunkVisible`).
    const RunKey& k      = run.key;
    const float   margin = (isLineKind(k.kind) && k.unit != Unit::World
                                ? ix.chunkMargin * pxPerUnit(f, f.world, k.unit)
                                : 0.0f) +
                           slackPx;
    ix.chunks.query(
        [&](const SpatialIndex::Box& b) {
            return boxVisible(f, b.minX, b.minY, b.minZ, b.maxX, b.maxY, b.maxZ, margin);
        },
        [&out](std::uint32_t i) { out.push_back(i); });
    // Växande, utan dubbletter (en chunk som ändrats har en gammal post
    // kvar) och utan dem som inte längre går att rita.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    out.erase(std::remove_if(out.begin(), out.end(),
                             [&](std::uint32_t i) { return !ritbar(*run.chunks[i]); }),
              out.end());
}

void objectCandidates(const RunData& run, std::uint64_t shapesVersion,
                      const Projection& proj, double slackPx,
                      std::vector<std::uint32_t>& out)
{
    out.clear();
    if (run.objects.size() < kRunIndexMin) {
        for (std::size_t i = 0; i < run.objects.size(); ++i)
            if (run.objects[i]->alive)
                out.push_back(static_cast<std::uint32_t>(i));
        return;
    }
    const std::lock_guard<std::mutex> vakt(nodeLock(run.id));
    const RunIndex& ix = objectIndex(run, shapesVersion);
    ix.objects.query(
        [&](const SpatialIndex::Box& b) {
            return boxVisible3(proj, Box3{{b.minX, b.minY, b.minZ}, {b.maxX, b.maxY, b.maxZ}},
                               slackPx);
        },
        [&out](std::uint32_t i) { out.push_back(i); });
    out.insert(out.end(), ix.always.begin(), ix.always.end());
    // Växande, utan dubbletter (ett föremål som ändrats har en gammal post
    // kvar) och utan de döda.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    out.erase(std::remove_if(out.begin(), out.end(),
                             [&](std::uint32_t i) { return !run.objects[i]->alive; }),
              out.end());
}

void objectZSpan(const RunData& run, std::uint64_t shapesVersion, double& zMin,
                 double& zMax)
{
    const std::lock_guard<std::mutex> vakt(nodeLock(run.id));
    const RunIndex& ix = objectIndex(run, shapesVersion);
    zMin               = ix.zMin;
    zMax               = ix.zMax;
}

std::uint64_t objectIndexBuilds(const RunData& run)
{
    const std::lock_guard<std::mutex> vakt(nodeLock(run.id));
    return run.index ? run.index->builds : 0;
}

std::uint64_t objectIndexInserted(const RunData& run)
{
    const std::lock_guard<std::mutex> vakt(nodeLock(run.id));
    return run.index ? run.index->inserted : 0;
}

std::uint64_t objectIndexRebuilds(const RunData& run)
{
    const std::lock_guard<std::mutex> vakt(nodeLock(run.id));
    return run.index ? run.index->rebuilds : 0;
}

} // namespace glaze::detail
