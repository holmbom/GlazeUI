// KÖRNINGENS RUMSLIGA INDEX (run_index.h).

#include "run_index.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>

#include "build.h"
#include "camera_math.h"
#include "mesh_data.h"
#include "render_loop.h"
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
    return objectWorldBox(s);
}

// Signaturens bit för "platsen har en post" — signaturen 0 betyder ingen.
constexpr std::uint64_t kSigPresent = 1;

// Föremålets signatur: platsens id, formens version och datans (en mesh som
// flyttat sina lägen har en annan låda).
std::uint64_t objectSig(const ShapeEntry& e)
{
    return objectSignature(e);
}

} // namespace

std::optional<Box3> objectWorldBox(const Shape& s)
{
    const Shape::Kind k = s.kind();
    if ((k == Shape::Kind::Mesh || k == Shape::Kind::Area3 || k == Shape::Kind::Instances) &&
        s.meshStyle().size.value > 0.0f)
        return std::nullopt;
    return objectBox(s);
}

std::uint64_t objectSignature(const ShapeEntry& e)
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

namespace {

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

// Syns tabellens rad genom `proj`? En död plats aldrig, en utan världslåda
// alltid — ritningens `objectInView`.
bool rowVisible(const ObjectInfo& rad, const Projection& proj, double slackPx)
{
    switch (rad.state) {
    case ObjectInfo::State::None:   return false;
    case ObjectInfo::State::Always: return true;
    case ObjectInfo::State::Boxed:  break;
    }
    return boxVisible3(proj, rad.box, slackPx);
}

RunIndex& objectIndex(const RunData& run, std::uint64_t shapesVersion)
{
    RunIndex&           ix  = indexOf(run);
    const std::uint64_t gen = meshGeneration();
    if (ix.objectsBuilt && ix.objectShapes == shapesVersion && ix.objectMeshes == gen)
        return ix;
    const std::size_t n    = run.objects.size();
    const bool        träd = n >= kRunTreeMin;
    // TABELLEN FRÅN BÖRJAN: första gången, eller en körning som krympt
    // (platserna har numrerats om). Annars förs den ikapp på plats.
    const bool från0 = !ix.objectsBuilt || n < ix.objectSig.size();
    // TRÄDET FRÅN BÖRJAN (ur tabellen, utan att något räknas om): med
    // tabellen, första gången körningen är lång nog, eller när för många
    // gamla poster ligger kvar.
    const bool trädOm = träd && (från0 || !ix.objectTree ||
                                 tooStale(ix.objectStale, ix.objects.size()));
    if (från0) {
        ix.objectSig.assign(n, 0);
        ix.objectAlways.assign(n, 0);
        ix.info.assign(n, ObjectInfo{});
        ++ix.rebuilds;
        sceneTally(SceneTally::Rebuilds, 1);
    } else {
        ix.objectSig.resize(n, 0);
        ix.objectAlways.resize(n, 0);
        ix.info.resize(n);
    }
    // BARA DE ÄNDRADE platserna ur körningens logg (scene.h), när den räcker
    // tillbaka till indexets version och ingen mesh flyttat sina lägen —
    // annars alla.
    const bool logg = !från0 && ix.objectMeshes == gen &&
                      ix.objectShapes >= run.objectChangesBase;
    bool alltidÄndrat = från0;
    bool ändrat       = från0;
    const auto plats = [&](std::size_t i) {
        const ShapeEntry&   e   = *run.objects[i];
        const std::uint64_t sig = e.alive ? objectSig(e) : 0;
        if (sig == ix.objectSig[i])
            return;   // oförändrad — posten står kvar
        ändrat       = true;
        alltidÄndrat = alltidÄndrat || ix.objectAlways[i];
        // Platsens gamla post ligger kvar i trädet (det redigeras inte).
        if (ix.objectSig[i] != 0 && !ix.objectAlways[i])
            ++ix.objectStale;
        ix.objectSig[i]    = sig;
        ix.objectAlways[i] = 0;
        ObjectInfo& rad    = ix.info[i];
        rad                = ObjectInfo{};
        if (sig == 0)
            return;
        const auto                nr = static_cast<std::uint32_t>(i);
        const std::optional<Box3> b  = objectBox(e.shape);
        rad.cost = static_cast<float>(objectPaceCost(e.shape));
        if (b) {
            rad.box    = *b;
            rad.hasBox = true;
        }
        if (const std::optional<Box3> w = worldBox(e.shape)) {
            rad.state = ObjectInfo::State::Boxed;
            if (träd && !trädOm)
                ix.objects.insert(nr, SpatialIndex::Box{w->min.x, w->min.y, w->min.z,
                                                        w->max.x, w->max.y, w->max.z});
        } else {
            rad.state          = ObjectInfo::State::Always;
            ix.objectAlways[i] = 1;
            alltidÄndrat       = true;
        }
        ++ix.inserted;
        sceneTally(SceneTally::Inserted, 1);
    };
    if (logg) {
        for (const RunData::ObjectChange& c : run.objectChanges)
            if (c.version > ix.objectShapes && c.index < n)
                plats(c.index);
    } else {
        for (std::size_t i = 0; i < n; ++i)
            plats(i);
    }
    // Listan över dem utan världslåda görs om bara när någon sådan ändrats.
    if (alltidÄndrat) {
        ix.always.clear();
        for (std::size_t i = 0; i < n; ++i)
            if (ix.objectAlways[i])
                ix.always.push_back(static_cast<std::uint32_t>(i));
    }
    // Z-spannet exakt över de levande lådorna, ur tabellen.
    if (ändrat) {
        ix.zMin = ix.zMax = 0.0;
        for (const ObjectInfo& rad : ix.info)
            if (rad.hasBox) {
                ix.zMin = std::min(ix.zMin, rad.box.min.z);
                ix.zMax = std::max(ix.zMax, rad.box.max.z);
            }
    }
    if (trädOm) {
        ix.objects.clear();
        ix.objectStale = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const ObjectInfo& rad = ix.info[i];
            if (rad.state == ObjectInfo::State::Boxed)
                ix.objects.insert(static_cast<std::uint32_t>(i),
                                  SpatialIndex::Box{rad.box.min.x, rad.box.min.y,
                                                    rad.box.min.z, rad.box.max.x,
                                                    rad.box.max.y, rad.box.max.z});
        }
    } else if (!träd && ix.objectTree) {
        ix.objects.clear();   // körningen har krympt under trädets gräns
        ix.objectStale = 0;
    }
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
    const RunIndex* ixp = nullptr;
    {
        const SceneCostScope kostnad(SceneCost::QIndex);
        ixp = &objectIndex(run, shapesVersion);
    }
    const RunIndex& ix = *ixp;
    if (!ix.objectTree) {
        // Utan träd: tabellen rakt igenom.
        for (std::size_t i = 0; i < ix.info.size(); ++i)
            if (rowVisible(ix.info[i], proj, slackPx))
                out.push_back(static_cast<std::uint32_t>(i));
        return;
    }
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

void objectsInView(const RunData& run, std::uint64_t shapesVersion,
                   const Projection& proj, double slackPx, const Point3& eye,
                   std::vector<ObjectSeen>& out)
{
    out.clear();
    const std::lock_guard<std::mutex> vakt(nodeLock(run.id));
    const RunIndex* ixp = nullptr;
    {
        const SceneCostScope kostnad(SceneCost::QIndex);
        ixp = &objectIndex(run, shapesVersion);
    }
    const RunIndex& ix  = *ixp;
    const auto      syn = [&](std::uint32_t i) {
        const ObjectInfo& rad = ix.info[i];
        if (!rowVisible(rad, proj, slackPx))
            return;
        ObjectSeen s;
        s.index = i;
        s.cost  = rad.cost;
        if (rad.hasBox) {
            s.distance = distanceToBox(eye, rad.box);
            const double dx = rad.box.max.x - rad.box.min.x,
                         dy = rad.box.max.y - rad.box.min.y,
                         dz = rad.box.max.z - rad.box.min.z;
            s.diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
            s.hasBox   = true;
        }
        out.push_back(s);
    };
    if (!ix.objectTree) {
        for (std::size_t i = 0; i < ix.info.size(); ++i)
            syn(static_cast<std::uint32_t>(i));
        return;
    }
    // Trädets kandidater (en övermängd med gamla poster), växande och utan
    // dubbletter, och de utan världslåda — var och en prövad mot tabellen.
    std::vector<std::uint32_t> kandidater;
    ix.objects.query(
        [&](const SpatialIndex::Box& b) {
            return boxVisible3(proj, Box3{{b.minX, b.minY, b.minZ}, {b.maxX, b.maxY, b.maxZ}},
                               slackPx);
        },
        [&kandidater](std::uint32_t i) { kandidater.push_back(i); });
    kandidater.insert(kandidater.end(), ix.always.begin(), ix.always.end());
    std::sort(kandidater.begin(), kandidater.end());
    kandidater.erase(std::unique(kandidater.begin(), kandidater.end()), kandidater.end());
    for (std::uint32_t i : kandidater)
        if (i < ix.info.size())
            syn(i);
}

void objectsUnseen(const RunData& run, std::uint64_t shapesVersion,
                   const Projection& proj, double slackPx,
                   const std::vector<std::uint64_t>& seen,
                   const std::vector<std::uint32_t>* only,
                   std::vector<ObjectUnseen>& out)
{
    out.clear();
    const std::lock_guard<std::mutex> vakt(nodeLock(run.id));
    const RunIndex& ix    = objectIndex(run, shapesVersion);
    const auto      plats = [&](std::size_t i) {
        const std::uint64_t sig = ix.objectSig[i];
        if (sig == (i < seen.size() ? seen[i] : 0))
            return;
        out.push_back(ObjectUnseen{static_cast<std::uint32_t>(i), sig,
                                   sig != 0 && rowVisible(ix.info[i], proj, slackPx)});
    };
    if (only) {
        for (std::uint32_t i : *only)
            if (i < ix.objectSig.size())
                plats(i);
        return;
    }
    for (std::size_t i = 0; i < ix.objectSig.size(); ++i)
        plats(i);
}

void syncObjectIndex(const RunData& run, std::uint64_t shapesVersion)
{
    const std::lock_guard<std::mutex> vakt(nodeLock(run.id));
    objectIndex(run, shapesVersion);
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
