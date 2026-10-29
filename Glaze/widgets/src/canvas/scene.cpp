// Ritytans lager — bygget (former → körningar → chunkar) och ritningen
// (gallring, GPU-synk, rit-anrop). Modellen och ordningsreglerna står i
// scene.h; formernas delar i build.h; fönstrets GPU-kopior i resources.h.

#include "scene.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

#include "../image_cache.h"
#include "../profiler_hooks.h"
#include "build.h"
#include "camera_math.h"
#include "drape.h"
#include "labels.h"
#include "mesh_data.h"
#include "resources.h"
#include "scene3d.h"

namespace glaze::detail {

namespace {

std::atomic<std::uint64_t> g_nextId{1};

std::uint64_t nextId()
{
    return g_nextId.fetch_add(1, std::memory_order_relaxed);
}

// Gallringens marginal utöver delens egen: kantutjämningen (en px på varje
// sida i Line-, Fill- och Glyph-programmen) och lite till.
constexpr float kCullSlackPx = 2.0f;

using Cell = std::pair<long long, long long>;

Cell cellOf(double x0, double y0, double x1, double y1)
{
    return Cell{static_cast<long long>(std::floor((x0 + x1) / 2.0 / kChunkCell)),
                static_cast<long long>(std::floor((y0 + y1) / 2.0 / kChunkCell))};
}

// Ligger rutan så nära chunkens origo att precisionen håller (scene.h)? I z
// också — `kChunkCell` gäller i alla tre axlarna (de plana slagen har z = 0
// och origo på 0, så frågan är densamma för dem).
bool near(const ChunkData& c, double x0, double y0, double x1, double y1,
          double z0 = 0.0, double z1 = 0.0)
{
    return std::max(std::abs(x0 - c.originX), std::abs(x1 - c.originX)) <=
               kChunkCell &&
           std::max(std::abs(y0 - c.originY), std::abs(y1 - c.originY)) <=
               kChunkCell &&
           std::max(std::abs(z0 - c.originZ), std::abs(z1 - c.originZ)) <=
               kChunkCell;
}

// En form i rymden (det `LayerData::shapes3D` räknar).
bool spatial(const Shape& s)
{
    return s.is3D();
}

// Ett helskärmspass (det `LayerData::passes` räknar).
bool isPass(const Shape& s)
{
    return s.kind() == Shape::Kind::Pass;
}

// Körningen för en del: i Strict den sista om nyckeln är densamma, i ByKind
// den med nyckeln — annars en ny (i ByKind sist i sitt slag).
RunData& runFor(LayerData& L, const RunKey& key)
{
    if (L.order == DrawOrder::Strict) {
        if (!L.runs.empty() && L.runs.back()->key == key)
            return *L.runs.back();
    } else {
        for (const std::shared_ptr<RunData>& r : L.runs)
            if (r->key == key)
                return *r;
    }
    auto run   = std::make_shared<RunData>();
    run->key   = key;
    run->order = L.order;
    if (L.order == DrawOrder::ByKind) {
        const auto it =
            std::find_if(L.runs.begin(), L.runs.end(),
                         [&key](const std::shared_ptr<RunData>& r) {
                             return r->key.kind > key.kind;
                         });
        return **L.runs.insert(it, std::move(run));
    }
    L.runs.push_back(std::move(run));
    return *L.runs.back();
}

// Chunken delen ska fortsätta i, eller nullptr för en ny. Strict: körningens
// sista; ByKind: rutans senaste — i båda fallen bara om delen ligger nära
// (rutan i xy; i rymden också nära i z).
std::shared_ptr<ChunkData>* targetFor(const LayerData& L, RunData& run,
                                      double x0, double y0, double x1,
                                      double y1, Cell& cell, double z0 = 0.0,
                                      double z1 = 0.0)
{
    std::shared_ptr<ChunkData>* t = nullptr;
    if (L.order == DrawOrder::ByKind) {
        cell          = cellOf(x0, y0, x1, y1);
        const auto it = run.cells.find(cell);
        if (it != run.cells.end())
            t = &run.chunks[it->second];
    } else if (!run.chunks.empty()) {
        t = &run.chunks.back();
    }
    return t && near(**t, x0, y0, x1, y1, z0, z1) ? t : nullptr;
}

// Byggarens origo till chunken — i z bara för byggarna i rymden.
template <class Built>
void takeOrigin(ChunkData& c, const Built& b)
{
    c.originX = b.originX;
    c.originY = b.originY;
    if constexpr (requires { b.originZ; })
        c.originZ = b.originZ;
}

// Delens låda för chunkens AABB i alla tre axlarna (de plana slagen z = 0).
struct PartBox {
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
    double z0 = 0.0, z1 = 0.0;
};

// Byggarens chunkar in i körningen: den första i målchunken om den ryms där
// (byggarens origo var målets — den förfyllda tomma chunken), resten som nya
// chunkar. Varje bit antecknas på formen.
template <class Built, class V>
void merge(LayerData& L, RunData& run, ShapeSlot& slot, std::uint8_t part,
           const Cell& cell, std::shared_ptr<ChunkData>* target,
           std::vector<Built>& built, std::vector<V> ChunkData::*verts,
           PartKind kind, const PartBox& box, float margin)
{
    for (std::size_t k = 0; k < built.size(); ++k) {
        Built& b = built[k];
        if (b.indices.empty())
            continue;
        ChunkData*  dst   = nullptr;
        std::size_t vbase = 0, ibase = 0;
        if (k == 0 && target &&
            (*target)->vertexCount() + b.vertices.size() <= gfx::kMaxChunkVertices) {
            dst   = target->get();
            vbase = (dst->*verts).size();
            ibase = dst->indices.size();
            (dst->*verts).insert((dst->*verts).end(), b.vertices.begin(),
                                 b.vertices.end());
            for (std::uint32_t i : b.indices)
                dst->indices.push_back(i + static_cast<std::uint32_t>(vbase));
            dst->note(vbase, b.vertices.size(), ibase, b.indices.size());
        } else {
            auto nc  = std::make_shared<ChunkData>();
            nc->kind = kind;
            takeOrigin(*nc, b);
            (*nc).*verts = std::move(b.vertices);
            nc->indices  = std::move(b.indices);
            dst         = nc.get();
            run.chunks.push_back(std::move(nc));
            if (L.order == DrawOrder::ByKind)
                run.cells[cell] = run.chunks.size() - 1;
        }
        const std::size_t vn = (dst->*verts).size() - vbase;
        const std::size_t in = dst->indices.size() - ibase;
        dst->grow(box.x0, box.y0, box.x1, box.y1, box.z0, box.z1);
        dst->margin = std::max(dst->margin, margin);
        slot.pieces.push_back(Piece{dst, part, vbase, vn, ibase, in, 0});
        L.totalIndices += in;
    }
}

// En förfylld tom chunk med origot (ox, oy[, oz]) — byggarna LÄGGER TILL i
// den sista chunken, så den bestämmer origot för det som ryms.
template <class Built>
std::vector<Built> seededAt(double ox, double oy, double oz = 0.0)
{
    std::vector<Built> v(1);
    v.front().originX = ox;
    v.front().originY = oy;
    if constexpr (requires { v.front().originZ; })
        v.front().originZ = oz;
    return v;
}

// Med målchunkens origo — eller tom, och då väljer byggaren (delens första
// punkt).
template <class Built>
std::vector<Built> seeded(const std::shared_ptr<ChunkData>* target)
{
    return target ? seededAt<Built>((*target)->originX, (*target)->originY,
                                    (*target)->originZ)
                  : std::vector<Built>{};
}

// Delens låda för chunkens AABB: en linje vars bredd är i VÄRLDEN växer
// lådan med halva bredden (marginalen i px är då bara kantutjämningen) — i
// rymden i alla tre axlarna.
PartBox partBox(const Shape& s, PartKind kind, double x0, double y0,
                double x1, double y1, double z0, double z1, float& margin)
{
    double pad = 0.0;
    margin     = 0.0f;
    if (isLineKind(kind)) {
        if (s.stroke().width.unit == Unit::World)
            pad = linePartHalfWidth(s);
        else
            margin = linePartHalfWidth(s);
    }
    PartBox b{x0 - pad, y0 - pad, x1 + pad, y1 + pad, z0, z1};
    if (kind == PartKind::Line3) {
        b.z0 -= pad;
        b.z1 += pad;
    }
    return b;
}

void place(LayerData& L, RunData& run, ShapeSlot& slot, std::uint8_t part,
           bool yUp, double x0, double y0, double x1, double y1, double z0,
           double z1)
{
    const Shape& s    = slot.entry->shape;
    const PartKind k  = run.key.kind;
    Cell           cell{};
    auto*          target = targetFor(L, run, x0, y0, x1, y1, cell, z0, z1);
    float          margin = 0.0f;
    const PartBox  box    = partBox(s, k, x0, y0, x1, y1, z0, z1, margin);
    switch (k) {
    case PartKind::Line: {
        auto built = seeded<gfx::LineChunk>(target);
        buildLinePart(s, built);
        merge(L, run, slot, part, cell, target, built, &ChunkData::lines, k,
              box, margin);
        break;
    }
    case PartKind::Line3: {
        auto built = seeded<gfx::LineChunk3>(target);
        buildLine3Part(s, built);
        merge(L, run, slot, part, cell, target, built, &ChunkData::lines3, k,
              box, margin);
        break;
    }
    case PartKind::Fill: {
        auto built = seeded<gfx::FillChunk>(target);
        buildFillPart(s, built);
        merge(L, run, slot, part, cell, target, built, &ChunkData::fills, k,
              box, margin);
        break;
    }
    case PartKind::Image: {
        auto  built = seeded<ImageChunk>(target);
        Point corners[4];
        imageCornersOf(s, yUp, corners);
        buildImageQuad(corners, built);
        merge(L, run, slot, part, cell, target, built, &ChunkData::images, k,
              box, margin);
        break;
    }
    case PartKind::Glyph:
    case PartKind::Glyph3:
        break;   // byggs per fönster (resources.cpp)
    case PartKind::Mesh:
    case PartKind::Points:
    case PartKind::Pass:
        break;   // föremålen och passen: körningens `objects`
    }
}

// Glyph-körningens ANKARLÅDA växer med formens (scene.h — scenens låda och
// terrängens region för etiketterna vid markhöjd läser den).
void growRunBox(RunData& run, double x0, double y0, double z0, double x1,
                double y1, double z1)
{
    run.minX   = run.hasBox ? std::min(run.minX, x0) : x0;
    run.minY   = run.hasBox ? std::min(run.minY, y0) : y0;
    run.minZ   = run.hasBox ? std::min(run.minZ, z0) : z0;
    run.maxX   = run.hasBox ? std::max(run.maxX, x1) : x1;
    run.maxY   = run.hasBox ? std::max(run.maxY, y1) : y1;
    run.maxZ   = run.hasBox ? std::max(run.maxZ, z1) : z1;
    run.hasBox = true;
}

void buildSlot(LayerData& L, ShapeSlot& slot, bool yUp)
{
    const Shape& s  = slot.entry->shape;
    double       x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0, z0 = 0.0, z1 = 0.0;
    if (isPass(s)) {
        // HELSKÄRMSPASSET har ingen låda — det täcker ritytan.
        for (const RunKey& k : partKeys(s)) {
            RunData& run = runFor(L, k);
            run.objects.push_back(slot.entry);
            slot.objectRun = &run;
            ++L.totalObjects;
        }
        return;
    }
    if (!shapeBounds(s, x0, y0, x1, y1))
        return;
    shapeZRange(s, z0, z1);
    const std::vector<RunKey> keys = partKeys(s);
    for (std::size_t i = 0; i < keys.size(); ++i) {
        RunData& run = runFor(L, keys[i]);
        if (isGlyphKind(keys[i].kind)) {
            run.glyphShapes.push_back(slot.entry);
            ++run.glyphVersion;
            slot.glyphRun = &run;
            ++L.totalGlyph;
            growRunBox(run, x0, y0, z0, x1, y1, z1);
            continue;
        }
        if (isObjectKind(keys[i].kind)) {
            // Föremålen: formen i körningens lista —
            // fönstret håller dess data resident och ritar den (scene3d.h).
            run.objects.push_back(slot.entry);
            slot.objectRun = &run;
            ++L.totalObjects;
            continue;
        }
        place(L, run, slot, static_cast<std::uint8_t>(i), yUp, x0, y0, x1, y1,
              z0, z1);
    }
}

bool sameKeys(const std::vector<RunKey>& a, const std::vector<RunKey>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!(a[i] == b[i]))
            return false;
    return true;
}

// En dels nya geometri byggd mot den gamla bitens chunk, för att skrivas på
// samma plats.
struct InPlace {
    Piece*                     piece = nullptr;
    std::vector<gfx::LineVertex>  lines;
    std::vector<gfx::LineVertex3> lines3;
    std::vector<gfx::FillVertex>  fills;
    std::vector<gfx::ImageVertex> images;
    std::vector<std::uint32_t>    indices;
    PartBox                       box;
    float                         margin = 0.0f;
};

template <class Built>
bool takeBuilt(std::vector<Built>& built, std::size_t vtxCount,
               std::size_t idxCount, decltype(Built::vertices)& vout,
               std::vector<std::uint32_t>& iout)
{
    // EN chunk, med EXAKT lika många vertex och högst lika många index.
    std::size_t used = 0;
    for (const Built& b : built)
        used += b.indices.empty() ? 0 : 1;
    if (used != 1 || built.front().vertices.size() != vtxCount ||
        built.front().indices.size() > idxCount)
        return false;
    vout = std::move(built.front().vertices);
    iout = std::move(built.front().indices);
    return true;
}

// Skriver formens nya delar på de gamla bitarnas plats om ALLA ryms där —
// annars ingenting (lagret byggs då om).
bool writeInPlace(LayerData& L, ShapeSlot& slot, const Shape& s,
                  const std::vector<RunKey>& keys, bool yUp)
{
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0, z0 = 0.0, z1 = 0.0;
    if (!shapeBounds(s, x0, y0, x1, y1))
        return false;
    shapeZRange(s, z0, z1);
    std::vector<InPlace> plan;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        // Texten byggs per fönster, och ett föremål har ingen geometri i
        // chunkarna — fönstret ritar den nya formen på samma plats (scene3d.h:
        // den gamla meshen tills den nya är uppladdad).
        if (isGlyphKind(keys[i].kind) || isObjectKind(keys[i].kind))
            continue;
        Piece* p = nullptr;
        for (Piece& q : slot.pieces)
            if (q.part == i) {
                if (p)
                    return false;   // delen bredde ut sig över flera chunkar
                p = &q;
            }
        if (!p || !near(*p->chunk, x0, y0, x1, y1, z0, z1))
            return false;
        InPlace ip;
        ip.piece = p;
        ip.box   = partBox(s, keys[i].kind, x0, y0, x1, y1, z0, z1, ip.margin);
        const double ox = p->chunk->originX, oy = p->chunk->originY;
        bool         ok = false;
        switch (keys[i].kind) {
        case PartKind::Line: {
            auto built = seededAt<gfx::LineChunk>(ox, oy);
            buildLinePart(s, built);
            ok = takeBuilt(built, p->vtxCount, p->idxCount, ip.lines, ip.indices);
            break;
        }
        case PartKind::Line3: {
            auto built = seededAt<gfx::LineChunk3>(ox, oy, p->chunk->originZ);
            buildLine3Part(s, built);
            ok = takeBuilt(built, p->vtxCount, p->idxCount, ip.lines3,
                           ip.indices);
            break;
        }
        case PartKind::Fill: {
            auto built = seededAt<gfx::FillChunk>(ox, oy);
            buildFillPart(s, built);
            ok = takeBuilt(built, p->vtxCount, p->idxCount, ip.fills, ip.indices);
            break;
        }
        case PartKind::Image: {
            auto  built = seededAt<ImageChunk>(ox, oy);
            Point corners[4];
            imageCornersOf(s, yUp, corners);
            buildImageQuad(corners, built);
            ok = takeBuilt(built, p->vtxCount, p->idxCount, ip.images, ip.indices);
            break;
        }
        case PartKind::Glyph:
        case PartKind::Glyph3:
        case PartKind::Mesh:
        case PartKind::Points:
        case PartKind::Pass:
            break;
        }
        if (!ok)
            return false;
        plan.push_back(std::move(ip));
    }

    for (InPlace& ip : plan) {
        Piece&     p = *ip.piece;
        ChunkData& c = *p.chunk;
        switch (c.kind) {
        case PartKind::Line:
            std::copy(ip.lines.begin(), ip.lines.end(), c.lines.begin() + p.vtxFrom);
            break;
        case PartKind::Line3:
            std::copy(ip.lines3.begin(), ip.lines3.end(),
                      c.lines3.begin() + p.vtxFrom);
            break;
        case PartKind::Fill:
            std::copy(ip.fills.begin(), ip.fills.end(), c.fills.begin() + p.vtxFrom);
            break;
        case PartKind::Image:
            std::copy(ip.images.begin(), ip.images.end(),
                      c.images.begin() + p.vtxFrom);
            break;
        case PartKind::Glyph:
        case PartKind::Glyph3:
        case PartKind::Mesh:
        case PartKind::Points:
        case PartKind::Pass:
            break;
        }
        const auto base = static_cast<std::uint32_t>(p.vtxFrom);
        for (std::size_t j = 0; j < p.idxCount; ++j)
            c.indices[p.idxFrom + j] =
                j < ip.indices.size() ? ip.indices[j] + base : base;   // döda
        const std::size_t dead = p.idxCount - ip.indices.size();
        L.deadIndices += dead > p.dead ? dead - p.dead : 0;
        L.deadIndices -= dead < p.dead ? p.dead - dead : 0;
        p.dead = dead;
        c.note(p.vtxFrom, p.vtxCount, p.idxFrom, p.idxCount);
        c.grow(ip.box.x0, ip.box.y0, ip.box.x1, ip.box.y1, ip.box.z0, ip.box.z1);
        c.margin = std::max(c.margin, ip.margin);
    }
    // Glyph-körningens ankarlåda följer den nya formen, som chunkarnas —
    // annars hamnar en flyttad etikett utanför scenens låda och terrängens
    // region (drape.cpp `terrainRegionKey`).
    if (slot.glyphRun)
        growRunBox(*slot.glyphRun, x0, y0, z0, x1, y1, z1);
    return true;
}

bool overDead(const LayerData& L)
{
    return (L.totalIndices > 0 &&
            static_cast<double>(L.deadIndices) >
                kCompactDeadShare * static_cast<double>(L.totalIndices)) ||
           (L.totalGlyph > 0 &&
            static_cast<double>(L.deadGlyph) >
                kCompactDeadShare * static_cast<double>(L.totalGlyph)) ||
           (L.totalObjects > 0 &&
            static_cast<double>(L.deadObjects) >
                kCompactDeadShare * static_cast<double>(L.totalObjects));
}

gfx::ScenePaint paintFor(const Paint& p, double ox, double oy)
{
    if (p.kind() == gfx::PaintKind::Radial)
        return gfx::radialPaint(gfx::Point2d{p.from().x, p.from().y}, p.r0(),
                                p.r1(), ox, oy);
    return gfx::linearPaint(gfx::Point2d{p.from().x, p.from().y},
                            gfx::Point2d{p.to().x, p.to().y}, ox, oy);
}

} // namespace

// ------------------------------------------------------------ datatyperna

ChunkData::ChunkData() : id(nextId()) {}
RunData::RunData() : id(nextId()) {}
LayerData::LayerData() : id(nextId()) {}
SceneData::SceneData() : id(nextId()) {}

ImageAccount::ImageAccount(const Ctx& c, const std::shared_ptr<SceneData>& scene)
    : cache_(scene ? c.images : nullptr)
{
    if (cache_)
        previous_ = cache_->beginAccount(
            scene->id, std::weak_ptr<const void>(scene), scene->memoryBudget);
}

ImageAccount::~ImageAccount()
{
    if (cache_)
        cache_->endAccount(previous_);
}

std::uint64_t nextSceneId()
{
    return nextId();
}

std::size_t ChunkData::vertexCount() const
{
    switch (kind) {
    case PartKind::Fill:   return fills.size();
    case PartKind::Image:  return images.size();
    case PartKind::Line:   return lines.size();
    case PartKind::Line3:  return lines3.size();
    case PartKind::Glyph:
    case PartKind::Glyph3:
    case PartKind::Mesh:
    case PartKind::Points:
    case PartKind::Pass:   break;
    }
    return 0;
}

std::size_t ChunkData::vertexSize() const
{
    switch (kind) {
    case PartKind::Fill:   return sizeof(gfx::FillVertex);
    case PartKind::Image:  return sizeof(gfx::ImageVertex);
    case PartKind::Line:   return sizeof(gfx::LineVertex);
    case PartKind::Line3:  return sizeof(gfx::LineVertex3);
    case PartKind::Glyph:
    case PartKind::Glyph3:
    case PartKind::Mesh:
    case PartKind::Points:
    case PartKind::Pass:   break;
    }
    return 0;
}

const void* ChunkData::vertexData() const
{
    switch (kind) {
    case PartKind::Fill:   return fills.data();
    case PartKind::Image:  return images.data();
    case PartKind::Line:   return lines.data();
    case PartKind::Line3:  return lines3.data();
    case PartKind::Glyph:
    case PartKind::Glyph3:
    case PartKind::Mesh:
    case PartKind::Points:
    case PartKind::Pass:   break;
    }
    return nullptr;
}

void ChunkData::note(std::size_t vtxFrom, std::size_t vtxCount,
                     std::size_t idxFrom, std::size_t idxCount)
{
    ++version;
    changes.push_back(Change{version, vtxFrom, vtxCount, idxFrom, idxCount});
    if (changes.size() > kMaxChanges) {
        // Den äldre halvan ges upp: ett fönster som inte hunnit ikapp med
        // den laddar upp hela chunken.
        const std::size_t drop = changes.size() / 2;
        base = changes[drop - 1].version;
        changes.erase(changes.begin(),
                      changes.begin() + static_cast<std::ptrdiff_t>(drop));
    }
}

void ChunkData::grow(double x0, double y0, double x1, double y1, double z0,
                     double z1)
{
    if (!hasBox) {
        minX = x0;
        minY = y0;
        maxX = x1;
        maxY = y1;
        minZ = z0;
        maxZ = z1;
        hasBox = true;
        return;
    }
    minX = std::min(minX, x0);
    minY = std::min(minY, y0);
    maxX = std::max(maxX, x1);
    maxY = std::max(maxY, y1);
    minZ = std::min(minZ, z0);
    maxZ = std::max(maxZ, z1);
}

// ------------------------------------------------------------------- bygget

void appendShape(LayerData& L, Shape s, bool yUp)
{
    ++L.shapesVersion;
    L.shapes3D += spatial(s) ? 1 : 0;
    L.passes += isPass(s) ? 1 : 0;
    L.groundShapes += paintsGround(s) ? 1 : 0;
    L.slots.push_back(
        ShapeSlot{std::make_shared<ShapeEntry>(ShapeEntry{std::move(s), true}),
                  {}, nullptr});
    if (!L.dirty)   // ett märkt lager bygger allt på en gång före ritningen
        buildSlot(L, L.slots.back(), yUp);
}

void removeShapeAt(LayerData& L, std::size_t i)
{
    ++L.shapesVersion;
    ShapeSlot& slot   = L.slots[i];
    slot.entry->alive = false;
    L.shapes3D -= spatial(slot.entry->shape) ? 1 : 0;
    L.passes -= isPass(slot.entry->shape) ? 1 : 0;
    L.groundShapes -= paintsGround(slot.entry->shape) ? 1 : 0;
    if (!L.dirty) {
        // DÖD DATA: formens index degenereras (tre lika index — en triangel
        // utan yta), och bara indexintervallet laddas upp igen.
        for (Piece& p : slot.pieces) {
            if (p.idxCount == 0)
                continue;
            std::vector<std::uint32_t>& idx = p.chunk->indices;
            const auto first = static_cast<std::ptrdiff_t>(p.idxFrom);
            const auto last  = static_cast<std::ptrdiff_t>(p.idxFrom + p.idxCount);
            std::fill(idx.begin() + first, idx.begin() + last,
                      static_cast<std::uint32_t>(p.vtxFrom));
            p.chunk->note(0, 0, p.idxFrom, p.idxCount);
            L.deadIndices += p.idxCount - p.dead;
        }
        if (slot.glyphRun) {
            ++slot.glyphRun->glyphVersion;   // fönstret bygger om utan den
            ++L.deadGlyph;
        }
        if (slot.objectRun)
            ++L.deadObjects;   // körningen hoppar över den döda formen
    }
    L.slots.erase(L.slots.begin() + static_cast<std::ptrdiff_t>(i));
    if (overDead(L))
        L.dirty = true;   // kompakteras före nästa ritning
}

void replaceShapeAt(LayerData& L, std::size_t i, Shape s, bool yUp)
{
    ++L.shapesVersion;
    ShapeSlot& slot = L.slots[i];
    L.shapes3D -= spatial(slot.entry->shape) ? 1 : 0;
    L.shapes3D += spatial(s) ? 1 : 0;
    L.passes -= isPass(slot.entry->shape) ? 1 : 0;
    L.passes += isPass(s) ? 1 : 0;
    L.groundShapes -= paintsGround(slot.entry->shape) ? 1 : 0;
    L.groundShapes += paintsGround(s) ? 1 : 0;
    if (!L.dirty) {
        const std::vector<RunKey> oldKeys = partKeys(slot.entry->shape);
        const std::vector<RunKey> newKeys = partKeys(s);
        // Samma delar med samma nycklar ⇒ samma körningar, och då går formen
        // att skriva på plats — ordningen i lagret består.
        if (sameKeys(oldKeys, newKeys) && writeInPlace(L, slot, s, newKeys, yUp)) {
            slot.entry->shape = std::move(s);
            ++slot.entry->version;
            if (slot.glyphRun)
                ++slot.glyphRun->glyphVersion;
            if (overDead(L))   // en krympt form lämnar döda index efter sig
                L.dirty = true;
            return;
        }
    }
    slot.entry->shape = std::move(s);
    ++slot.entry->version;
    L.dirty = true;
}

void rebuildLayer(LayerData& L, bool yUp)
{
    profileCount(ProfileCount::LayerRebuilds);
    ++L.shapesVersion;
    L.runs.clear();
    L.totalIndices = L.deadIndices = 0;
    L.totalGlyph = L.deadGlyph = 0;
    L.totalObjects = L.deadObjects = 0;
    L.shapes3D = 0;
    L.passes   = 0;
    L.groundShapes = 0;
    L.dirty = false;
    for (ShapeSlot& slot : L.slots) {
        slot.pieces.clear();
        slot.glyphRun  = nullptr;
        slot.objectRun = nullptr;
        L.shapes3D += spatial(slot.entry->shape) ? 1 : 0;
        L.passes += isPass(slot.entry->shape) ? 1 : 0;
        L.groundShapes += paintsGround(slot.entry->shape) ? 1 : 0;
        buildSlot(L, slot, yUp);
    }
}

void assignShapes(LayerData& L, std::vector<Shape> shapes)
{
    L.slots.clear();
    ++L.shapesVersion;   // träffprovets rutnät gällde de gamla formerna
    L.shapes3D = 0;
    L.passes   = 0;
    L.groundShapes = 0;
    for (Shape& s : shapes) {
        L.shapes3D += spatial(s) ? 1 : 0;
        L.passes += isPass(s) ? 1 : 0;
        L.groundShapes += paintsGround(s) ? 1 : 0;
        L.slots.push_back(ShapeSlot{
            std::make_shared<ShapeEntry>(ShapeEntry{std::move(s), true}), {},
            nullptr});
    }
    L.dirty = true;   // allt byggs på en gång före nästa ritning
}

void touchText(LayerData& L)
{
    ++L.textVersion;   // draperingens markstämpel (drape.cpp)
    for (const std::shared_ptr<RunData>& r : L.runs)
        if (isGlyphKind(r->key.kind))
            ++r->glyphVersion;
}

// --------------------------------------------------------------- placeringen

LayerPlacement effectivePlacement(const LayerData& L)
{
    if (L.placement != LayerPlacement::Auto)
        return L.placement;
    return L.shapes3D > 0 ? LayerPlacement::World : LayerPlacement::Ground;
}

bool hasWorldLayer(const SceneData& sc)
{
    for (const std::shared_ptr<LayerData>& L : sc.layers)
        if (L->visible && effectivePlacement(*L) != LayerPlacement::Ground)
            return true;
    return false;
}

bool hasScreenPass(const SceneData& sc)
{
    for (const std::shared_ptr<LayerData>& L : sc.layers)
        if (L->visible && L->passes > 0)
            return true;
    return false;
}

bool paintsGround(const Shape& s)
{
    switch (s.kind()) {
    case Shape::Kind::None:
    case Shape::Kind::Pass:
        return false;
    case Shape::Kind::Text:
    case Shape::Kind::Text3:
        return s.textStyle().rotation == Alignment::Ground;
    case Shape::Kind::Markers:
    case Shape::Kind::Markers3:
        return s.markerStyle().rotation == Alignment::Ground;
    default:
        return true;
    }
}

bool paintsGround(const LayerData& L)
{
    return L.groundShapes > 0;
}

const ProgramSlot* findProgram(const SceneData& sc, const std::string& name)
{
    for (const ProgramSlot& p : sc.programs)
        if (p.program.name == name)
            return &p;
    return nullptr;
}

bool layerInView(const LayerData& L, int view)
{
    return std::binary_search(L.views.begin(), L.views.end(), view);
}

const std::optional<Box3>& objectBox(const ShapeEntry& e)
{
    // Lådan beror på formen (platsens version) och meshens lägen (meshens
    // version — `setPositions`); instansmängden och molnet ändras aldrig.
    const std::shared_ptr<MeshData>& d    = MeshAccess::data(e.shape.mesh());
    const std::uint64_t              mesh = d ? d->version : 0;
    if (!e.boxCached || e.boxVersion != e.version || e.boxMesh != mesh) {
        e.box        = objectBox(e.shape);
        e.boxVersion = e.version;
        e.boxMesh    = mesh;
        e.boxCached  = true;
    }
    return e.box;
}

std::optional<Box3> sceneBox(const SceneData& sc, int view)
{
    // NYCKELN (scene.h): de lager som räknas — synliga i vyn — med sina
    // formers version, och meshernas generation. Vyerna läses här och inte
    // genom `layerSetVersion`: del-vyernas omnumrering stegar den inte.
    KeyHash k;
    k.mix(meshGeneration());
    for (const std::shared_ptr<LayerData>& L : sc.layers) {
        const bool räknas = L->visible && layerInView(*L, view);
        k.mix(L->id);
        k.mix(static_cast<std::uint64_t>(räknas));
        if (räknas)
            k.mix(L->shapesVersion);
    }
    auto cache = std::find_if(sc.boxCache.begin(), sc.boxCache.end(),
                              [view](const SceneData::BoxCache& c) {
                                  return c.view == view;
                              });
    if (cache != sc.boxCache.end() && cache->key == k.h)
        return cache->box;
    if (cache == sc.boxCache.end())
        cache = sc.boxCache.insert(sc.boxCache.end(), SceneData::BoxCache{view, 0, {}});

    std::optional<Box3> box;
    const auto add = [&box](double x0, double y0, double z0, double x1,
                            double y1, double z1) {
        if (!box) {
            box = Box3{{x0, y0, z0}, {x1, y1, z1}};
            return;
        }
        box->min = {std::min(box->min.x, x0), std::min(box->min.y, y0),
                    std::min(box->min.z, z0)};
        box->max = {std::max(box->max.x, x1), std::max(box->max.y, y1),
                    std::max(box->max.z, z1)};
    };
    for (const std::shared_ptr<LayerData>& L : sc.layers) {
        if (!L->visible || !layerInView(*L, view))
            continue;
        for (const std::shared_ptr<RunData>& run : L->runs) {
            if (isGlyphKind(run->key.kind)) {
                if (run->hasBox)
                    add(run->minX, run->minY, run->minZ, run->maxX, run->maxY,
                        run->maxZ);
                continue;
            }
            if (isObjectKind(run->key.kind)) {
                // Föremålen med sina NUVARANDE lådor — en mesh kan ha ändrat
                // sina lägen sedan den lades till (scene3d.h).
                for (const std::shared_ptr<ShapeEntry>& e : run->objects)
                    if (e->alive)
                        if (const std::optional<Box3>& b = objectBox(*e))
                            add(b->min.x, b->min.y, b->min.z, b->max.x,
                                b->max.y, b->max.z);
                continue;
            }
            for (const std::shared_ptr<ChunkData>& ch : run->chunks)
                if (ch->hasBox)
                    add(ch->minX, ch->minY, ch->minZ, ch->maxX, ch->maxY,
                        ch->maxZ);
        }
    }
    cache->key = k.h;
    cache->box = box;
    return box;
}

// ---------------------------------------------------------------- ritningen

float pxPerUnit(const PaintContext& f, const gfx::MapView& vy, Unit u)
{
    switch (u) {
    case Unit::Mm:    return f.c->pxPerMm;
    case Unit::World: return static_cast<float>(vy.pxPerUnit);
    case Unit::Px:    break;
    }
    return f.c->scale;
}

bool boxVisible(const gfx::MapView& vy, double minX, double minY, double maxX,
                double maxY, float marginPx)
{
    // Rutans fyra hörn på skärmen. Ligger alla framför kameran är rutans
    // avtryck den konvexa fyrhörning hörnen spänner upp, och dess AABB
    // (med marginalen för det som extruderas i skärmen) avgör. Ett hörn
    // bakom kameran: rita — gallringen får aldrig ta något synligt.
    double     sx0 = 0.0, sy0 = 0.0, sx1 = 0.0, sy1 = 0.0;
    const double xs[2] = {minX, maxX}, ys[2] = {minY, maxY};
    bool       first = true;
    for (double x : xs)
        for (double y : ys) {
            double px = 0.0, py = 0.0;
            if (!gfx::project(vy, x, y, px, py))
                return true;
            if (first) {
                sx0 = sx1 = px;
                sy0 = sy1 = py;
                first = false;
            }
            sx0 = std::min(sx0, px);
            sy0 = std::min(sy0, py);
            sx1 = std::max(sx1, px);
            sy1 = std::max(sy1, py);
        }
    const double m = marginPx;
    return sx1 + m >= vy.box.x && sx0 - m <= vy.box.x + vy.box.w &&
           sy1 + m >= vy.box.y && sy0 - m <= vy.box.y + vy.box.h;
}

gfx::Mat4d chunkMatrix(const PaintContext& f, double ox, double oy, double oz)
{
    if (f.view3D)
        return gfx::viewProjection(*f.view3D, ox, oy, oz);
    // Kartkamerans matris tar origot i planet; en form i rymden ritad i
    // 2D-vägen får sitt z som en förskjutning efteråt — kartvärldens z är
    // samma rymd (gfx/camera.h). z = 0 är exakt kartkamerans matris.
    const gfx::Mat4d m = gfx::viewProjection(f.world, ox, oy);
    return oz == 0.0 ? m : m * gfx::Mat4d::translation(0.0, 0.0, oz);
}

bool boxVisible(const PaintContext& f, double minX, double minY, double minZ,
                double maxX, double maxY, double maxZ, float marginPx)
{
    if (!f.view3D && minZ == 0.0 && maxZ == 0.0)
        return boxVisible(f.world, minX, minY, maxX, maxY, marginPx);
    // Lådans åtta hörn genom ritningens projektion (camera_math): ett hörn
    // bakom kameran ritar alltid, annars de projicerade hörnens ruta med
    // marginalen.
    const Projection proj = f.view3D ? Projection(*f.view3D) : Projection(f.world);
    double     sx0 = 0.0, sy0 = 0.0, sx1 = 0.0, sy1 = 0.0;
    bool       first = true;
    const double xs[2] = {minX, maxX}, ys[2] = {minY, maxY}, zs[2] = {minZ, maxZ};
    for (double x : xs)
        for (double y : ys)
            for (double z : zs) {
                double c[4];
                proj.clip(x, y, z, c);
                if (!(c[3] > 0.0))
                    return true;
                const double px = c[0] / c[3], py = c[1] / c[3];
                if (first) {
                    sx0 = sx1 = px;
                    sy0 = sy1 = py;
                    first = false;
                }
                sx0 = std::min(sx0, px);
                sy0 = std::min(sy0, py);
                sx1 = std::max(sx1, px);
                sy1 = std::max(sy1, py);
            }
    const gfx::Rectf& b = proj.box();
    const double      m = marginPx;
    return sx1 + m >= b.x && sx0 - m <= b.x + b.w && sy1 + m >= b.y &&
           sy0 - m <= b.y + b.h;
}

namespace {

// Rutnätets täthet: storleken × 1, 2, 5, 10, 20 … tills två linjer ligger
// minst `minPhys` fysiska px isär. 0 när ingen rimlig täthet finns.
double gridSpacing(double size, double ppu, double minPhys)
{
    if (size * ppu >= minPhys)
        return size;
    for (double decade = 1.0; decade < 1e12; decade *= 10.0)
        for (double step : {1.0, 2.0, 5.0})
            if (size * step * decade * ppu >= minPhys)
                return size * step * decade;
    return 0.0;
}

gfx::Image* imageOf(const PaintContext& f, const ImageSource& src)
{
    if (!f.c->images)
        return nullptr;
    switch (src.kind()) {
    case ImageSource::Kind::Asset:   return f.c->images->get(src.asset());
    case ImageSource::Kind::Bitmap:  return f.c->images->get(src.bitmap());
    case ImageSource::Kind::Texture: return f.c->images->get(src.texture(), *f.c);
    case ImageSource::Kind::Stock:   return f.c->images->get(src, *f.c);
    case ImageSource::Kind::None:    break;
    }
    return nullptr;
}

// En Bitmap större än GPU:ns största textur är RUTOR — nullptr för
// varje annan källa och för en bild som ryms i en textur.
const std::vector<ImageTile>* tilesOf(const PaintContext& f,
                                      const ImageSource& src)
{
    if (!f.c->images || src.kind() != ImageSource::Kind::Bitmap)
        return nullptr;
    return f.c->images->tiles(src.bitmap());
}

// Chunkens plattor ritade RUTA FÖR RUTA: varje platta delad på
// rutornas delar av bilden (`splitImageQuads`), transient, med rutans textur
// och i övrigt chunkens rit-anrop `base`.
void drawTiledChunk(const PaintContext& f, const ChunkData& ch,
                    const std::vector<ImageTile>& tiles,
                    const gfx::MeshDraw& base)
{
    std::vector<ImageChunk> delar;
    for (const ImageTile& t : tiles) {
        delar.clear();
        splitImageQuads(ch.images, ch.indices, ch.originX, ch.originY,
                        ImageUv{t.u0, t.v0, t.u1, t.v1},
                        ImageUv{t.tu0, t.tv0, t.tu1, t.tv1}, delar);
        for (const ImageChunk& del : delar) {
            gfx::MeshDraw d = base;
            d.vertices      = f.c->r->uploadTransient(
                gfx::BufferKind::Vertex, del.vertices.data(),
                del.vertices.size() * sizeof(gfx::ImageVertex));
            d.indices = f.c->r->uploadTransient(
                gfx::BufferKind::Index, del.indices.data(),
                del.indices.size() * sizeof(std::uint32_t));
            if (!d.vertices.valid() || !d.indices.valid())
                return;
            d.indexCount = static_cast<std::uint32_t>(del.indices.size());
            chunkMatrix(f, del.originX, del.originY, ch.originZ)
                .toFloat(d.uniforms.viewProj);
            d.image = t.image;
            f.c->r->drawMesh(d);
        }
    }
}

// Körningens bild i fönstret — RUTORNA för en Bitmap över GPU:ns största
// textur, annars texturen — uppladdad inom framens budget. Ingen av
// dem: ogiltig, eller väntar på bildcachens budget (nästa frame).
struct RunImage {
    const gfx::Image*             image = nullptr;
    const std::vector<ImageTile>* tiles = nullptr;
    bool                          ok() const { return image || tiles; }
};

RunImage runImage(const PaintContext& f, const ImageSource& src)
{
    RunImage r;
    r.tiles = tilesOf(f, src);
    r.image = r.tiles ? nullptr : imageOf(f, src);
    return r;
}

// Syns chunken i ritningens vy? Gallringens fråga — en linje i px eller mm
// når sin halva bredd utanför lådan — delad av ritningen och förberedelsens
// synliga pass, så de aldrig är oense.
bool chunkVisible(const PaintContext& f, const RunData& run, const ChunkData& ch)
{
    const RunKey& k      = run.key;
    const float   margin = (isLineKind(k.kind) && k.unit != Unit::World
                                ? ch.margin * pxPerUnit(f, f.world, k.unit)
                                : 0.0f) +
                         kCullSlackPx;
    return boxVisible(f, ch.minX, ch.minY, ch.minZ, ch.maxX, ch.maxY, ch.maxZ,
                      margin);
}

// Körningens chunkar: alltid synkade (resident), ritade när `draw` och
// synliga i vyn — med renderläget `state` (marken: inget djup).
void drawChunks(const PaintContext& f, RunData& run, float op, bool draw,
                const gfx::RenderState& state)
{
    const RunKey& k          = run.key;
    const bool    worldWidth = isLineKind(k.kind) && k.unit == Unit::World;
    const float   unitPx     = pxPerUnit(f, f.world, k.unit);
    // BILDEN OCH RAMPEN hämtas vid den första SYNLIGA chunken:
    // en körning utanför bild laddar inte upp sin bild här — det gör
    // `prepareUploads` med det som blir kvar av framens budget.
    RunImage          bild;
    const gfx::Image* ramp    = nullptr;
    bool              hämtat  = false, saknas = false;
    for (const std::shared_ptr<ChunkData>& ch : run.chunks) {
        if (ch->indices.empty() || !ch->hasBox)
            continue;
        // RESIDENT FÖRST, GALLRAT SEDAN: varje chunk i ett synligt lager
        // finns på GPU:n, också den som ligger utanför bild — en panorering
        // in över den ska rita, inte ladda upp (kameran ändrar
        // aldrig innehållet). Gallringen sparar rit-anropet.
        const CanvasResources::GpuChunk* g = f.res->sync(ch);
        if (!g || !draw || saknas || !chunkVisible(f, run, *ch))
            continue;
        if (!hämtat) {
            hämtat = true;
            if (k.kind == PartKind::Image)
                bild = runImage(f, k.image);
            ramp   = k.gradient ? f.res->ramp(k.paint.stops()) : nullptr;
            // Ogiltig, eller väntar på bildcachens tak — nästa frame; resten
            // av chunkarna synkas ändå.
            saknas = (k.kind == PartKind::Image && !bild.ok()) || (k.gradient && !ramp);
            if (saknas)
                continue;
        }
        const gfx::Image*             image = bild.image;
        const std::vector<ImageTile>* tiles = bild.tiles;
        gfx::MeshDraw d;
        d.program    = programOf(k.kind);
        d.vertices   = gfx::BufferSlice{g->vb.get(), 0,
                                        ch->vertexCount() * ch->vertexSize()};
        d.indices    = gfx::BufferSlice{g->ib.get(), 0,
                                        ch->indices.size() * sizeof(std::uint32_t)};
        d.indexCount = static_cast<std::uint32_t>(ch->indices.size());
        chunkMatrix(f, ch->originX, ch->originY, ch->originZ)
            .toFloat(d.uniforms.viewProj);
        d.uniforms.pxPerWidthUnit = worldWidth ? 1.0f : unitPx;
        d.uniforms.widthSpace =
            worldWidth ? gfx::WidthSpace::World : gfx::WidthSpace::Screen;
        // Lagrets opacitet gånger FORMENS — `RunKey::opacity` är
        // bildens och 1 för alla andra slag — och bildens färgjustering;
        // rutorna ärver båda (`drawTiledChunk`).
        d.uniforms.opacity = op * k.opacity;
        imageUniforms(k, d.uniforms);
        strokeUniforms(k, pxPerUnit(f, f.world, Unit::World),
                       pxPerUnit(f, f.world, k.dashUnit), d.uniforms);
        if (k.gradient) {
            d.uniforms.paint = paintFor(k.paint, ch->originX, ch->originY);
            d.image          = ramp;
        }
        if (image)
            d.image = image;
        d.state = state;
        if (tiles) {
            drawTiledChunk(f, *ch, *tiles, d);
            continue;
        }
        f.c->r->drawMesh(d);
    }
}

CanvasResources::GlyphEnv glyphEnv(const PaintContext& f)
{
    CanvasResources::GlyphEnv env;
    env.pxPerPx        = f.c->scale;
    env.pxPerMm        = f.c->pxPerMm;
    env.pxPerWorldUnit = static_cast<float>(f.world.pxPerUnit);
    return env;
}

// Körningens bygge VID MARKHÖJD? Ett Ground-lagers
// stående etiketter och markörer när en terräng ritas — den liggande texten
// draperas och texten i rymden har sitt eget z.
bool onTerrain(const LayerFrame& lf, LayerPlacement placement, const RunData& run)
{
    return lf.terrain && placement == LayerPlacement::Ground &&
           run.key.kind == PartKind::Glyph && !run.key.ground;
}

// Byggets miljö för körningen — terrängen med när etiketterna står på den,
// och terrängens region under körningens ankarlåda (drape.h — det enda av
// terrängen bygget läser). Förberedelsen och ritningen frågar samma, så
// bygget träffas i cachen.
CanvasResources::GlyphEnv glyphEnv(const PaintContext& f, const SceneData& sc,
                                   const LayerFrame& lf, LayerPlacement placement,
                                   const RunData& run)
{
    CanvasResources::GlyphEnv env = glyphEnv(f);
    if (onTerrain(lf, placement, run)) {
        env.terrain    = &sc;
        env.terrainKey = run.hasBox ? terrainRegionKey(sc, run.minX, run.minY,
                                                       run.maxX, run.maxY)
                                    : 0;
    }
    return env;
}

// Körningens text, ritad — Glyph eller Glyph3 (chunkens program: en plan
// etikett vid markhöjd är Glyph3), med renderläget `state`. Bygget är redan
// resident (förberedelsen, `prepareLayers`, med samma miljö `env`);
// etiketternas synlighet och körningens bas ur placeringen — en körning utan
// bas ritas utan synlighet (alla synliga).
void drawGlyphs(const PaintContext& f, const std::shared_ptr<RunData>& run,
                const CanvasResources::GlyphEnv& env, float op,
                const LabelFrame& labels, const gfx::RenderState& state)
{
    const CanvasResources::GlyphBuild& b = f.res->glyphs(run, env);
    const std::int64_t base   = labels.tex ? labels.baseOf(run->id) : -1;
    const float        unitPx = pxPerUnit(f, f.world, run->key.unit);
    // GOLVET OCH TAKET — anropets uniformer med anropets tätheter.
    const GlyphLimits  lim    = glyphLimits(run->key, f.c->scale, f.c->pxPerMm,
                                            static_cast<float>(f.world.pxPerUnit));
    // BILDMARKÖRERNA ritas med sin bild bunden (körningens nyckel bär den);
    // väntar den på bildcachens tak ritas de nästa frame.
    const gfx::Image* markerImage = nullptr;
    if (run->key.image.kind() != ImageSource::Kind::None) {
        markerImage = imageOf(f, run->key.image);
        if (!markerImage)
            return;
    }
    for (const CanvasResources::GlyphGpu& g : b.chunks) {
        // Golvet kan bara göra en etikett större än byggets mått, taket bara
        // mindre — marginalen är den större av de två.
        if (!boxVisible(f, g.minX, g.minY, g.minZ, g.maxX, g.maxY, g.maxZ,
                        std::max(g.reach * unitPx, g.reachEm * lim.minPx) +
                            kCullSlackPx))
            continue;
        gfx::MeshDraw d;
        d.program    = g.program;
        d.vertices   = gfx::slice(*g.vb);
        d.indices    = gfx::slice(*g.ib);
        d.indexCount = g.count;
        d.image      = markerImage ? markerImage
                       : g.atlas && g.page >= 0
                           ? g.atlas->page(static_cast<std::size_t>(g.page))
                           : nullptr;
        chunkMatrix(f, g.originX, g.originY, g.originZ)
            .toFloat(d.uniforms.viewProj);
        d.uniforms.pxPerWidthUnit = unitPx;
        d.uniforms.opacity        = op;
        d.uniforms.glyphMinPx     = lim.minPx;
        d.uniforms.glyphMaxPx     = lim.maxPx;
        if (base >= 0) {
            d.labels    = labels.tex;
            d.labelBase = static_cast<std::uint32_t>(base);
        }
        d.state = state;
        f.c->r->drawMesh(d);
    }
}

} // namespace

gfx::RenderState gfxState(const RenderState& s, bool label)
{
    gfx::RenderState g;
    g.depth      = s.depth;
    g.depthWrite = s.depthWrite && !label;
    g.depthBias  = s.depthBias;
    g.slopeBias  = s.slopeBias;
    g.blend      = s.blend;
    g.cull       = s.cull;
    return g;
}

bool validRenderState(const RenderState& s)
{
    return static_cast<unsigned>(s.depth) <=
               static_cast<unsigned>(DepthTest::Always) &&
           static_cast<unsigned>(s.blend) <= static_cast<unsigned>(Blend::DepthOnly) &&
           static_cast<unsigned>(s.cull) <= static_cast<unsigned>(Cull::Front) &&
           std::isfinite(s.depthBias) && std::isfinite(s.slopeBias);
}

namespace {

// Rutnätets täthet `s` och dess mesh (skapad första gången) — nullptr när
// inget rutnät ritas. Bredden i logiska px — eller HÅRLINJEN, en fysisk px:
// vertexens bredd bakas in i meshen, anropets enhet gör den till fysiska px.
const CanvasResources::GridMesh* gridFor(const PaintContext& f, double size,
                                         gfx::Color color, float minPx,
                                         float widthPx, double& s,
                                         bool& hairline)
{
    if (!(size > 0.0) || !std::isfinite(size) || color.a == 0 ||
        !(f.world.pxPerUnit > 0.0))
        return nullptr;
    s = gridSpacing(size, f.world.pxPerUnit, std::max(minPx, 1.0f) * f.c->scale);
    if (!(s > 0.0))
        return nullptr;
    hairline = !(widthPx > 0.0f);
    return f.res->grid(color, hairline ? 1.0f : widthPx);
}

} // namespace

void prepareGrid(const PaintContext& f, double size, gfx::Color color,
                 float minPx, float widthPx)
{
    double s        = 0.0;
    bool   hairline = false;
    gridFor(f, size, color, minPx, widthPx, s, hairline);
}

void drawGrid(const PaintContext& f, double size, gfx::Color color, float minPx,
              float widthPx)
{
    double      s        = 0.0;
    bool        hairline = false;
    const auto* mesh     = gridFor(f, size, color, minPx, widthPx, s, hairline);
    if (!mesh)
        return;
    // Origot i kamerans närmaste rutnätspunkt (double — SWEREF), och meshens
    // heltal skalade till rutans storlek. I 3D-målet ligger rutnätet på
    // marken, z = 0, genom 3D-vyn.
    const double  ox = std::floor(f.world.cx / s) * s;
    const double  oy = std::floor(f.world.cy / s) * s;
    const gfx::Mat4d m = chunkMatrix(f, ox, oy) * gfx::Mat4d::scale(s, s, 1.0);
    gfx::MeshDraw d;
    d.program    = gfx::MeshProgram::Line;
    d.vertices   = gfx::slice(*mesh->vb);
    d.indices    = gfx::slice(*mesh->ib);
    d.indexCount = mesh->count;
    m.toFloat(d.uniforms.viewProj);
    d.uniforms.pxPerWidthUnit = hairline ? 1.0f : f.c->scale;
    d.uniforms.widthSpace     = gfx::WidthSpace::Screen;
    d.uniforms.opacity        = f.opacity;
    f.c->r->drawMesh(d);
}

// Ritas lagret? Utanför vyn som ritas, utanför sitt
// zoomintervall eller helt genomskinligt ritas det inte (men hålls resident).
bool layerDrawn(const PaintContext& f, const LayerData& L)
{
    return layerInView(L, f.view) && L.opacity > 0.0f &&
           zoomInRange(L.minZoom, L.maxZoom, f.zoom);
}

bool zoomInRange(double minZoom, double maxZoom, double zoom)
{
    if (!(zoom > 0.0) || !std::isfinite(zoom))
        return true;   // okänd zoom — ingenting döljs
    return !(minZoom > 0.0 && zoom < minZoom) &&
           !(maxZoom > 0.0 && zoom > maxZoom);
}

namespace {

// 3D-målets förberedelse av en körning: chunkarna
// residenta (de har ingen budget), och för ett ritat lager rampen, passens
// program och markörernas bild hämtade — det som laddar upp sker då före
// målet. Föremålen och bildkörningarnas bilder laddas upp av
// `prepareUploads`, synligt först. Ritningen hittar allt i cacharna.
void prepareRun(const PaintContext& f, const SceneData& sc, RunData& run,
                bool draw)
{
    const RunKey& k = run.key;
    if (isObjectKind(k.kind))
        return;   // `prepareUploads` (scene3d.h)
    if (k.kind == PartKind::Pass) {
        // Passen: programmen kompilerade och texturerna hämtade.
        if (draw)
            for (const std::shared_ptr<ShapeEntry>& e : run.objects)
                if (e->alive)
                    prepareScreenPass(f, sc, e->shape);
        return;
    }
    if (isGlyphKind(k.kind)) {
        if (draw && k.image.kind() != ImageSource::Kind::None)
            imageOf(f, k.image);   // bildmarkörerna
        return;
    }
    if (draw && k.gradient)
        f.res->ramp(k.paint.stops());
    for (const std::shared_ptr<ChunkData>& ch : run.chunks)
        if (!ch->indices.empty() && ch->hasBox)
            f.res->sync(ch);
}

bool has(GroundPart parts, GroundPart p)
{
    return (static_cast<unsigned>(parts) & static_cast<unsigned>(p)) != 0;
}

// LAGRETS MÅLARE (scene.h `LayerPaint`) — i ritningens läge med
// lagrets ton `op`, så målarens former tonas som lagrets egna.
void paintLayer(const PaintContext& f, const LayerPaint* paint,
                const LayerData& L, std::size_t li, float op,
                const RenderState* state)
{
    if (!paint || !*paint || !L.painter)
        return;
    PaintContext lager = f;
    lager.opacity      = op;
    (*paint)(lager, static_cast<int>(li), state);
}

} // namespace

void rebuildDirty(SceneData& sc)
{
    for (const std::shared_ptr<LayerData>& L : sc.layers)
        if (L->visible && L->dirty)
            rebuildLayer(*L, sc.yUp);
}

LayerFrame prepareLayers(const PaintContext& f,
                         const std::shared_ptr<SceneData>& scene,
                         bool labelFade, bool residentFirst)
{
    SceneData& sc = *scene;
    LayerFrame lf;
    lf.placement.assign(sc.layers.size(), LayerPlacement::Ground);
    // TERRÄNGEN (drape.h) — bara i 3D-målet; den avgör var
    // Ground-lagren ritas och var deras etiketter står.
    lf.terrain = f.view3D && hasTerrain(f, sc);

    // De märkta lagren byggs om, varje Glyph-körning i ett synligt lager
    // byggs för fönstret (RESIDENT — också utanför zoomintervallet, så zoomen
    // som tar in lagret inte bygger), och de RITADE körningarna samlas i
    // ritordning för etiketternas placering.
    std::vector<LabelSource> sources;
    for (std::size_t li = 0; li < sc.layers.size(); ++li) {
        LayerData& L = *sc.layers[li];
        if (!L.visible)
            continue;   // ett dolt lager är appens val — det laddas upp när det visas
        if (f.view != kMainView && !layerInView(L, f.view))
            continue;   // en del-vy förbereder sina lager; huvudvyn har resten
        if (L.dirty)
            rebuildLayer(L, sc.yUp);
        lf.placement[li] = effectivePlacement(L);
        const bool draw  = layerDrawn(f, L);
        // MARKEN finns där ett ritat Ground-lager har något som målas på
        // den — ett tomt (lager 0 finns alltid) eller ett med bara stående
        // etiketter gör ingen (markdjupet, target3d.h steg 3).
        if (draw && lf.placement[li] == LayerPlacement::Ground && paintsGround(L))
            lf.groundDrawn = true;
        for (const std::shared_ptr<RunData>& run : L.runs) {
            if (residentFirst)
                prepareRun(f, sc, *run, draw);
            if (!isGlyphKind(run->key.kind))
                continue;
            const CanvasResources::GlyphBuild& b =
                f.res->glyphs(run, glyphEnv(f, sc, lf, lf.placement[li], *run));
            // Den liggande texten på terräng draperas: den kolliderar inte
            // (drape.h).
            const bool draperad = lf.terrain &&
                                  lf.placement[li] == LayerPlacement::Ground &&
                                  run->key.ground;
            if (draw && !draperad)
                sources.push_back(LabelSource{run.get(), &b,
                                              static_cast<int>(li), L.collision});
        }
    }
    // Föremålen och bilderna, synligt först — också de före
    // målet.
    if (residentFirst)
        prepareUploads(f, sc);
    lf.labels = updateLabels(f, scene, sources, labelFade);
    return lf;
}

void prepareUploads(const PaintContext& f, const SceneData& sc)
{
    // KÖN: det synliga i vyn med avståndet från ögat (ritningens gallring —
    // `chunkVisible`, scene3d.cpp:s `queueObjects`) och resten i lagrens
    // ordning. Föremålen och bildkörningarna delar budgeten, så de delar kön.
    std::vector<UploadItem> synligt, resten;
    const bool              öga = f.view3D.has_value();
    for (const std::shared_ptr<LayerData>& Lp : sc.layers) {
        const LayerData& L = *Lp;
        if (!L.visible || (f.view != kMainView && !layerInView(L, f.view)))
            continue;
        const bool draw = layerDrawn(f, L);
        for (const std::shared_ptr<RunData>& run : L.runs) {
            const RunKey& k = run->key;
            if (isObjectKind(k.kind)) {
                queueObjects(f, *run, draw, synligt, resten);
                continue;
            }
            if (!draw || k.kind != PartKind::Image)
                continue;
            bool   syns    = false;
            double närmast = 0.0;
            for (const std::shared_ptr<ChunkData>& ch : run->chunks) {
                if (ch->indices.empty() || !ch->hasBox || !chunkVisible(f, *run, *ch))
                    continue;
                const double d =
                    öga ? distanceToBox(Point3{f.view3D->eye.x, f.view3D->eye.y,
                                               f.view3D->eye.z},
                                        Box3{{ch->minX, ch->minY, ch->minZ},
                                             {ch->maxX, ch->maxY, ch->maxZ}})
                        : 0.0;
                närmast = syns ? std::min(närmast, d) : d;
                syns    = true;
            }
            (syns ? synligt : resten).push_back(UploadItem{närmast, nullptr, run.get(), true});
        }
    }
    std::stable_sort(synligt.begin(), synligt.end(),
                     [](const UploadItem& a, const UploadItem& b) {
                         return a.distance < b.distance;
                     });
    for (const std::vector<UploadItem>* kö : {&synligt, &resten})
        for (const UploadItem& u : *kö) {
            if (u.object)
                prepareObject(f, sc, *u.object, u.draw);
            else if (u.imageRun)
                runImage(f, u.imageRun->key.image);
        }
}

void drawGround(const PaintContext& f, const SceneData& sc,
                const LayerFrame& lf, GroundPart parts, const LayerPaint* paint)
{
    const gfx::RenderState mark;   // marken: inget djup, målarordning
    // MARKENS GEOMETRI — bilderna, ytorna, linjerna och den liggande texten —
    // ritas i draperingen (`Drape`), eller i Geometry-delen utan terräng
    // (med terräng finns den bara genom draperingen).
    const bool mark2D = has(parts, GroundPart::Drape) ||
                        (has(parts, GroundPart::Geometry) && !lf.terrain);
    const LabelFrame ingen;   // draperingen bär ingen synlighet
    for (std::size_t li = 0; li < sc.layers.size(); ++li) {
        LayerData& L = *sc.layers[li];
        if (!L.visible || lf.placement[li] != LayerPlacement::Ground)
            continue;
        // Utanför lagrets zoomintervall (eller helt genomskinligt) ritas
        // ingenting — men lagret hålls RESIDENT, så zoomen som tar in det
        // ritar i stället för att ladda upp.
        const bool  draw = layerDrawn(f, L);
        const float op   = std::min(L.opacity, 1.0f) * f.opacity;
        for (const std::shared_ptr<RunData>& run : L.runs) {
            if (run->key.kind == PartKind::Pass)
                continue;   // passen ritas först (`drawPasses`)
            if (isGlyphKind(run->key.kind)) {
                const CanvasResources::GlyphEnv env =
                    glyphEnv(f, sc, lf, LayerPlacement::Ground, *run);
                if (run->key.ground) {
                    // LIGGANDE text och markörer ligger med marken.
                    if (draw && mark2D)
                        drawGlyphs(f, run, env, op,
                                   has(parts, GroundPart::Drape) ? ingen : lf.labels,
                                   mark);
                } else if (draw && has(parts, GroundPart::Labels)) {
                    drawGlyphs(f, run, env, op, lf.labels, mark);
                }
            } else if (isObjectKind(run->key.kind)) {
                if (has(parts, GroundPart::Geometry))
                    drawObjects(f, sc, lf, *run, op, draw, mark);
            } else if (mark2D) {
                drawChunks(f, *run, op, draw, mark);
            }
        }
        // MÅLAREN efter lagrets former, på marken utan djup — också med
        // terräng, där den inte draperas (draperingen ritas inte om varje
        // bildruta).
        if (draw && has(parts, GroundPart::Geometry))
            paintLayer(f, paint, L, li, op, nullptr);
    }
}

void drawTerrain(const PaintContext& f, const SceneData& sc, const LayerFrame& lf)
{
    for (std::size_t li = 0; li < sc.layers.size(); ++li) {
        LayerData& L = *sc.layers[li];
        if (!L.visible || lf.placement[li] != LayerPlacement::Terrain)
            continue;
        const bool             draw = layerDrawn(f, L);
        const float            op   = std::min(L.opacity, 1.0f) * f.opacity;
        const gfx::RenderState geo  = gfxState(L.state, false);
        for (const std::shared_ptr<RunData>& run : L.runs)
            if (isObjectKind(run->key.kind))
                drawObjects(f, sc, lf, *run, op, draw, geo);
    }
}

void drawWorld(const PaintContext& f, const SceneData& sc, const LayerFrame& lf,
               const LayerPaint* paint)
{
    for (std::size_t li = 0; li < sc.layers.size(); ++li) {
        LayerData& L = *sc.layers[li];
        if (!L.visible || lf.placement[li] == LayerPlacement::Ground)
            continue;
        const bool             draw  = layerDrawn(f, L);
        const float            op    = std::min(L.opacity, 1.0f) * f.opacity;
        const gfx::RenderState geo   = gfxState(L.state, false);
        const gfx::RenderState label = gfxState(L.state, true);
        for (const std::shared_ptr<RunData>& run : L.runs) {
            if (run->key.kind == PartKind::Pass)
                continue;   // passen ritas först (`drawPasses`)
            if (isGlyphKind(run->key.kind)) {
                if (draw)
                    drawGlyphs(f, run, glyphEnv(f), op, lf.labels, label);
            } else if (isObjectKind(run->key.kind)) {
                // Terrängens meshar ritas med marken (`drawTerrain`).
                if (lf.placement[li] != LayerPlacement::Terrain)
                    drawObjects(f, sc, lf, *run, op, draw, geo);
            } else {
                drawChunks(f, *run, op, draw, geo);
            }
        }
        // MÅLAREN efter lagrets former, med lagrets renderläge som start —
        // djuptestad som lagret.
        if (draw)
            paintLayer(f, paint, L, li, op, &L.state);
    }
}

void drawPasses(const PaintContext& f, const SceneData& sc)
{
    for (const std::shared_ptr<LayerData>& Lp : sc.layers) {
        const LayerData& L = *Lp;
        if (!L.visible || L.passes == 0 || !layerDrawn(f, L))
            continue;
        // Utan djup — passet ligger under allt — men med lagrets blandning.
        gfx::RenderState st = gfxState(L.state, false);
        st.depth      = gfx::DepthTest::Off;
        st.depthWrite = false;
        const float op = std::min(L.opacity, 1.0f) * f.opacity;
        for (const std::shared_ptr<RunData>& run : L.runs) {
            if (run->key.kind != PartKind::Pass)
                continue;
            for (const std::shared_ptr<ShapeEntry>& e : run->objects)
                if (e->alive)
                    drawScreenPass(f, sc, e->shape, op, st);
        }
    }
}

} // namespace glaze::detail
