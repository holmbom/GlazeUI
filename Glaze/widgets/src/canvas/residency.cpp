// Föremålens residens per fönster — kontraktet,
// budgeten och dubbelbuffringen står i residency.h.

#include "residency.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <utility>

#include <gfx/platform.h>

#include "../image_cache.h"

namespace glaze::detail {

namespace {

// En buffert som inte gick att skapa loggas en gång per process — kopian
// blir aldrig komplett och ritas inte.
std::atomic<bool> g_bufferLogged{false};

// Strömmarna en mesh BÄR (de inbyggda programmens platser; user0–3 är egna
// program).
MeshVariant streamsOf(const MeshData& d)
{
    MeshVariant v;
    v.normals = !d.normals.empty();
    v.colors  = !d.colors.empty();
    v.uvs     = !d.uvs.empty();
    v.scalars = !d.scalars.empty();
    return v;
}

PointVariant streamsOf(const PointCloudData& d)
{
    PointVariant v;
    v.colors  = !d.colors.empty();
    v.scalars = !d.scalars.empty();
    return v;
}

// EGNA PROGRAMS strömmar: bit i = meshen bär user<i>.
std::uint8_t userStreamsOf(const MeshData& d)
{
    std::uint8_t m = 0;
    for (std::size_t i = 0; i < d.user.size(); ++i)
        if (!d.user[i].empty())
            m = static_cast<std::uint8_t>(m | (1u << i));
    return m;
}

// Layouten med user-strömmarna SIST i vertexen — Float4 på platserna
// kUser0 … kUser0 + 3 (gfx/program.h). De inbyggda programmen läser dem inte;
// ett eget program tar dem enligt platskonventionen.
gfx::VertexLayout withUser(gfx::VertexLayout l, std::uint8_t user)
{
    for (std::uint8_t i = 0; i < 4; ++i)
        if (user & (1u << i)) {
            l.attribs.push_back(gfx::VertexAttrib{
                static_cast<std::uint8_t>(gfx::attrib::kUser0 + i),
                gfx::AttribFormat::Float4, l.stride[0], 0});
            l.stride[0] = static_cast<std::uint16_t>(
                l.stride[0] + gfx::attribBytes(gfx::AttribFormat::Float4));
        }
    return l;
}

// Segmentets läge `local` som meshens globala läge (mesh_data.h).
std::uint32_t globalVertex(const MeshData& d, const MeshSegment& s,
                           std::uint32_t local)
{
    if (!d.isIndexed())
        return s.firstElement + local;
    return s.vertices.empty() ? local : s.vertices[local];
}

std::uint32_t segmentVertices(const MeshData& d, const MeshSegment& s)
{
    if (!d.isIndexed())
        return s.elementCount;
    return s.vertices.empty() ? static_cast<std::uint32_t>(d.positions.size())
                              : static_cast<std::uint32_t>(s.vertices.size());
}

// Segmentets index: meshens egna för ett odelat, segmentets lokala annars.
const std::uint32_t* segmentIndices(const MeshData& d, const MeshSegment& s)
{
    return s.vertices.empty() ? d.indices.data() + s.firstElement
                              : s.indices.data();
}

// Lägena [first, first + n) i segmentet packade i kopians strömmar — de
// inbyggda programmens fält och sist user-strömmarna (`withUser`).
void packVertices(const MeshData& d, const MeshSegment& s,
                  const MeshVariant& v, std::uint8_t user, std::uint16_t stride,
                  std::uint32_t first, std::uint32_t n,
                  std::vector<std::uint8_t>& out)
{
    const std::size_t builtin = meshLayout(v).stride[0];
    out.resize(static_cast<std::size_t>(n) * stride);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::uint32_t g = globalVertex(d, s, first + i);
        MeshVertex          mv;
        mv.position = d.positions[g];
        if (v.normals)
            mv.normal = d.normals[g];
        if (v.colors)
            mv.color = d.colors[g];
        if (v.uvs) {
            mv.u = d.uvs[g][0];
            mv.v = d.uvs[g][1];
        }
        if (v.scalars)
            mv.scalar = d.scalars[g];
        std::uint8_t* ut = out.data() + static_cast<std::size_t>(i) * stride;
        packMeshVertex(v, mv, ut);
        ut += builtin;
        for (std::size_t k = 0; k < d.user.size(); ++k)
            if (user & (1u << k)) {
                std::memcpy(ut, d.user[k][g].data(), sizeof(d.user[k][g]));
                ut += sizeof(d.user[k][g]);
            }
    }
}

// Hur många element om `elem` byte som får laddas upp nu (minst ett när
// framen inte laddat upp något — den första uppladdningen går igenom).
std::uint32_t roomFor(ImageCache* images, std::size_t elem, std::uint32_t want)
{
    if (!images)
        return want;
    const std::size_t room = images->uploadRoom(elem);
    return static_cast<std::uint32_t>(
        std::min<std::size_t>(want, room / elem));
}

void spend(ImageCache* images, std::size_t bytes)
{
    if (images)
        images->spendUpload(bytes);
}

void defer(ImageCache* images)
{
    if (images)
        images->deferUpload();
}

std::unique_ptr<gfx::GpuBuffer> newBuffer(gfx::Renderer& r, gfx::BufferKind k,
                                          std::size_t bytes)
{
    std::unique_ptr<gfx::GpuBuffer> b = r.createBuffer(k, bytes, nullptr);
    if (!b || !b->valid()) {
        if (!g_bufferLogged.exchange(true))
            gfx::log("Canvas: en buffert om %zu byte för ritytans 3D-data gick "
                     "inte att skapa — föremålet ritas inte; loggas en gång",
                     bytes);
        return nullptr;
    }
    return b;
}

} // namespace

ObjectResidency::ObjectResidency(gfx::Renderer& r) : r_(&r) {}

ObjectResidency::~ObjectResidency() = default;

// ------------------------------------------------------------ framens början

void ObjectResidency::trim()
{
    retiredMeshes_.clear();
    retiredInstances_.clear();
    retiredClouds_.clear();
    // Platser vars form dött (borttagen, lagret eller ritytan riven) — deras
    // kopior släpps med dem när ingen annan håller dem. Förra framen är
    // klar, så det som ritades med dem är redan ritat.
    for (auto it = slots_.begin(); it != slots_.end();)
        it = it->second.src.expired() ? slots_.erase(it) : std::next(it);
    // Kopior vars data dött: en plats som fortfarande visar en håller den.
    for (auto it = meshes_.begin(); it != meshes_.end();)
        it = it->second->src.expired() ? meshes_.erase(it) : std::next(it);
    for (auto it = instances_.begin(); it != instances_.end();)
        it = it->second->src.expired() ? instances_.erase(it) : std::next(it);
    for (auto it = clouds_.begin(); it != clouds_.end();)
        it = it->second->src.expired() ? clouds_.erase(it) : std::next(it);
}

// ------------------------------------------------------------------ meshen

bool ObjectResidency::stream(GpuMesh& g, const MeshData& d, ImageCache* images)
{
    std::vector<std::uint8_t> buf;
    while (!g.complete) {
        if (g.nextSegment >= g.segments.size()) {
            g.complete = true;
            break;
        }
        GpuMesh::Segment&  sg = g.segments[g.nextSegment];
        const MeshSegment& ms = d.segments[g.nextSegment];
        if (!sg.vb || (g.indexed && !sg.ib))
            return false;   // en buffert som inte gick att skapa (loggat)
        const std::uint32_t total = g.inIndices ? ms.elementCount : sg.vertices;
        const std::size_t   elem  = g.inIndices ? sizeof(std::uint32_t)
                                                : g.layout.stride[0];
        if (g.done >= total) {
            // Nästa buffert: segmentets index, eller nästa segment.
            if (!g.inIndices && g.indexed) {
                g.inIndices = true;
            } else {
                g.inIndices = false;
                ++g.nextSegment;
            }
            g.done = 0;
            continue;
        }
        const std::uint32_t n = roomFor(images, elem, total - g.done);
        if (n == 0) {
            defer(images);   // fönstret ber om nästa frame — strömningen fortsätter
            return false;
        }
        if (g.inIndices) {
            r_->updateBuffer(*sg.ib, g.done * elem, segmentIndices(d, ms) + g.done,
                             n * elem);
        } else {
            packVertices(d, ms, g.streams, g.user, g.layout.stride[0], g.done, n,
                         buf);
            r_->updateBuffer(*sg.vb, g.done * elem, buf.data(), n * elem);
        }
        spend(images, n * elem);
        g.done += n;
    }
    return true;
}

bool ObjectResidency::catchUp(GpuMesh& g, const MeshData& d, ImageCache* images)
{
    if (g.version == d.version)
        return true;
    if (g.version < d.base)
        return false;   // loggen räcker inte hit — en ny kopia
    // De ändrade LÄGENA sedan kopians version, sammanslagna.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> ändrat;   // [a, b)
    for (const MeshChange& c : d.changes)
        if (c.version > g.version)
            ändrat.emplace_back(c.first, c.first + c.count);
    std::sort(ändrat.begin(), ändrat.end());
    std::vector<std::pair<std::uint32_t, std::uint32_t>> slaget;
    for (const auto& r : ändrat) {
        if (!slaget.empty() && r.first <= slaget.back().second)
            slaget.back().second = std::max(slaget.back().second, r.second);
        else
            slaget.push_back(r);
    }
    // Per segment: de lokala lägen intervallen når (ett delat segments globala
    // lägen är växande, så en följd globala lägen är en följd lokala).
    struct Bit {
        std::size_t   seg;
        std::uint32_t first, count;
    };
    std::vector<Bit> bitar;
    std::size_t      byte = 0;
    for (std::size_t si = 0; si < d.segments.size(); ++si) {
        const MeshSegment& ms = d.segments[si];
        for (const auto& [a, b] : slaget) {
            std::uint32_t lo = 0, hi = 0;
            if (!d.isIndexed()) {
                lo = std::max(a, ms.firstElement);
                hi = std::min(b, ms.firstElement + ms.elementCount);
                if (lo >= hi)
                    continue;
                lo -= ms.firstElement;
                hi -= ms.firstElement;
            } else if (ms.vertices.empty()) {
                lo = a;
                hi = b;
            } else {
                lo = static_cast<std::uint32_t>(
                    std::lower_bound(ms.vertices.begin(), ms.vertices.end(), a) -
                    ms.vertices.begin());
                hi = static_cast<std::uint32_t>(
                    std::lower_bound(ms.vertices.begin(), ms.vertices.end(), b) -
                    ms.vertices.begin());
                if (lo >= hi)
                    continue;
            }
            bitar.push_back(Bit{si, lo, hi - lo});
            byte += static_cast<std::size_t>(hi - lo) * g.layout.stride[0];
        }
    }
    // ALLT I EN FRAME — annars ritas den gamla datan en frame till.
    if (images && byte > images->uploadRoom(byte)) {
        defer(images);
        return true;
    }
    std::vector<std::uint8_t> buf;
    for (const Bit& b : bitar) {
        packVertices(d, d.segments[b.seg], g.streams, g.user, g.layout.stride[0],
                     b.first, b.count, buf);
        r_->updateBuffer(*g.segments[b.seg].vb,
                         static_cast<std::size_t>(b.first) * g.layout.stride[0],
                         buf.data(), buf.size());
    }
    spend(images, byte);
    g.version = d.version;
    // Lägena kan ha flyttat sig: gallringen och detaljnivån mäter kopians låda.
    g.localBox = Box3{{d.bounds.min.x - d.origin.x, d.bounds.min.y - d.origin.y,
                       d.bounds.min.z - d.origin.z},
                      {d.bounds.max.x - d.origin.x, d.bounds.max.y - d.origin.y,
                       d.bounds.max.z - d.origin.z}};
    return true;
}

std::shared_ptr<ObjectResidency::GpuMesh>
ObjectResidency::meshCopy(const std::shared_ptr<MeshData>& d, ImageCache* images,
                          bool upload)
{
    if (!d)
        return nullptr;
    std::shared_ptr<GpuMesh>& slot = meshes_[d->id];
    if (slot && slot->complete && upload && !catchUp(*slot, *d, images))
        slot.reset();   // loggen räckte inte: en ny kopia, dubbelbuffrad
    if (!slot) {
        auto g     = std::make_shared<GpuMesh>();
        g->src     = d;
        g->id      = d->id;
        g->version = d->version;
        g->origin  = d->origin;
        g->localBox = Box3{{d->bounds.min.x - d->origin.x,
                            d->bounds.min.y - d->origin.y,
                            d->bounds.min.z - d->origin.z},
                           {d->bounds.max.x - d->origin.x,
                            d->bounds.max.y - d->origin.y,
                            d->bounds.max.z - d->origin.z}};
        g->lods     = d->lods;
        g->elements = d->elementCount();
        g->indexed  = d->isIndexed();
        g->streams  = streamsOf(*d);
        g->user     = userStreamsOf(*d);
        g->layout   = withUser(meshLayout(g->streams), g->user);
        MeshVariant inst = g->streams;
        inst.instanced   = true;
        g->layoutInstanced = withUser(meshLayout(inst), g->user);
        g->segments.resize(d->segments.size());
        for (std::size_t i = 0; i < d->segments.size(); ++i) {
            const MeshSegment& ms = d->segments[i];
            GpuMesh::Segment&  sg = g->segments[i];
            sg.firstElement = ms.firstElement;
            sg.elementCount = ms.elementCount;
            sg.vertices     = segmentVertices(*d, ms);
            sg.vb = newBuffer(*r_, gfx::BufferKind::Vertex,
                              static_cast<std::size_t>(sg.vertices) *
                                  g->layout.stride[0]);
            if (g->indexed)
                sg.ib = newBuffer(*r_, gfx::BufferKind::Index,
                                  static_cast<std::size_t>(ms.elementCount) *
                                      sizeof(std::uint32_t));
        }
        slot = std::move(g);
        ++copiesMade_;
    }
    if (upload && !slot->complete)
        stream(*slot, *d, images);
    return slot;
}

// -------------------------------------------------------------- instanserna

bool ObjectResidency::stream(GpuInstances& g, const InstanceSet& s,
                             ImageCache* images)
{
    if (!g.buf)
        return false;
    constexpr std::size_t elem = sizeof(MeshInstance);
    while (g.done < g.count) {
        const std::uint32_t n = roomFor(images, elem, g.count - g.done);
        if (n == 0) {
            defer(images);
            return false;
        }
        r_->updateBuffer(*g.buf, g.done * elem, s.rows.data() + g.done, n * elem);
        spend(images, n * elem);
        g.done += n;
    }
    g.complete = true;
    return true;
}

std::shared_ptr<ObjectResidency::GpuInstances>
ObjectResidency::instanceCopy(const std::shared_ptr<const InstanceSet>& s,
                              ImageCache* images, bool upload)
{
    if (!s || s->rows.empty())
        return nullptr;
    std::shared_ptr<GpuInstances>& slot = instances_[s->id];
    if (!slot) {
        auto g   = std::make_shared<GpuInstances>();
        g->src   = s;
        g->id    = s->id;
        g->count = static_cast<std::uint32_t>(s->rows.size());
        g->buf   = newBuffer(*r_, gfx::BufferKind::Vertex,
                             s->rows.size() * sizeof(MeshInstance));
        slot     = std::move(g);
        ++copiesMade_;
    }
    if (upload && !slot->complete)
        stream(*slot, *s, images);
    return slot;
}

// ----------------------------------------------------------------- molnen

bool ObjectResidency::stream(GpuCloud& g, const PointCloudData& d,
                             ImageCache* images)
{
    const std::size_t         elem = g.layout.stride[1];
    std::vector<std::uint8_t> buf;
    while (g.nextChunk < d.chunks.size()) {
        const PointChunk& c = d.chunks[g.nextChunk];
        if (!g.chunks[g.nextChunk])
            return false;   // loggat
        if (g.done >= c.count) {
            ++g.nextChunk;
            g.done = 0;
            continue;
        }
        const std::uint32_t n = roomFor(images, elem, c.count - g.done);
        if (n == 0) {
            defer(images);
            return false;
        }
        buf.resize(static_cast<std::size_t>(n) * elem);
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::uint32_t p = c.first + g.done + i;
            PointVertex         pv;
            pv.position = d.positions[p];
            if (g.streams.colors)
                pv.color = d.colors[p];
            if (g.streams.scalars)
                pv.scalar = d.scalars[p];
            packPoint(g.streams, pv, buf.data() + static_cast<std::size_t>(i) * elem);
        }
        r_->updateBuffer(*g.chunks[g.nextChunk], g.done * elem, buf.data(),
                         buf.size());
        spend(images, buf.size());
        g.done += n;
    }
    g.complete = true;
    return true;
}

std::shared_ptr<ObjectResidency::GpuCloud>
ObjectResidency::cloudCopy(const std::shared_ptr<PointCloudData>& d,
                           ImageCache* images, bool upload)
{
    if (!d || d->chunks.empty())
        return nullptr;
    std::shared_ptr<GpuCloud>& slot = clouds_[d->id];
    if (!slot) {
        auto g     = std::make_shared<GpuCloud>();
        g->src     = d;
        g->id      = d->id;
        g->streams = streamsOf(*d);
        g->layout  = pointLayout(g->streams);
        g->chunks.resize(d->chunks.size());
        for (std::size_t i = 0; i < d->chunks.size(); ++i)
            g->chunks[i] = newBuffer(*r_, gfx::BufferKind::Vertex,
                                     static_cast<std::size_t>(d->chunks[i].count) *
                                         g->layout.stride[1]);
        slot = std::move(g);
        ++copiesMade_;
    }
    if (upload && !slot->complete)
        stream(*slot, *d, images);
    return slot;
}

// ------------------------------------------------------------------ platsen

ObjectResidency::Shown ObjectResidency::object(
    const std::shared_ptr<ShapeEntry>& entry, ImageCache* images, bool upload)
{
    const Shape& s = entry->shape;
    Slot&        p = slots_[entry->id];
    if (p.src.expired())
        p.src = entry;

    // Det formen vill visa NU — kopiorna för dess data.
    std::shared_ptr<GpuMesh>      mesh;
    std::shared_ptr<GpuInstances> inst;
    std::shared_ptr<GpuCloud>     cloud;
    bool                          wantsMesh = false, wantsInst = false,
                                  wantsCloud = false;
    switch (s.kind()) {
    case Shape::Kind::Mesh:
    case Shape::Kind::Area3:
        wantsMesh = true;
        mesh      = meshCopy(MeshAccess::data(s.mesh()), images, upload);
        break;
    case Shape::Kind::Instances:
        wantsMesh = wantsInst = true;
        mesh = meshCopy(MeshAccess::data(s.mesh()), images, upload);
        inst = instanceCopy(s.instanceSet(), images, upload);
        break;
    case Shape::Kind::Points3:
        wantsCloud = true;
        cloud      = cloudCopy(MeshAccess::data(s.pointCloud()), images, upload);
        break;
    case Shape::Kind::None:
    case Shape::Kind::Lines:
    case Shape::Kind::Area:
    case Shape::Kind::Text:
    case Shape::Kind::Markers:
    case Shape::Kind::Image:
    case Shape::Kind::Lines3:
    case Shape::Kind::Text3:
    case Shape::Kind::Markers3:
    case Shape::Kind::Pass:
        break;
    }
    const bool klar = (!wantsMesh || (mesh && mesh->complete)) &&
                      (!wantsInst || (inst && inst->complete)) &&
                      (!wantsCloud || (cloud && cloud->complete)) &&
                      (wantsMesh || wantsInst || wantsCloud);
    if (klar) {
        // DUBBELBUFFRINGEN: platsen byter till den nya formen när ALL dess
        // data är uppe; det den visade kan ha ritats tidigare i framen och
        // lever till nästa trim.
        if (p.mesh && p.mesh != mesh)
            retiredMeshes_.push_back(p.mesh);
        if (p.inst && p.inst != inst)
            retiredInstances_.push_back(p.inst);
        if (p.cloud && p.cloud != cloud)
            retiredClouds_.push_back(p.cloud);
        p.mesh    = std::move(mesh);
        p.inst    = std::move(inst);
        p.cloud   = std::move(cloud);
        p.shape   = s;
        p.showing = true;
    }
    if (!p.showing)
        return Shown{};
    return Shown{&p.shape, p.mesh.get(), p.inst.get(), p.cloud.get()};
}

// --------------------------------------------------------------- programmen

const gfx::ShaderProgram* ObjectResidency::meshProgram(const MeshVariant& v)
{
    std::unique_ptr<gfx::ShaderProgram>& p = meshPrograms_[v.key()];
    if (!p)   // ett ogiltigt program sparas också — backenden loggar en gång
        p = r_->createProgram(meshProgramDesc(v));
    return p && p->valid() ? p.get() : nullptr;
}

const gfx::ShaderProgram* ObjectResidency::pointProgram(const PointVariant& v)
{
    std::unique_ptr<gfx::ShaderProgram>& p = pointPrograms_[v.key()];
    if (!p)
        p = r_->createProgram(pointProgramDesc(v));
    return p && p->valid() ? p.get() : nullptr;
}

void ObjectResidency::precompile()
{
    if (precompiled_)
        return;
    precompiled_ = true;
    // Primitiverna och rören (normaler), stolparna (normaler och färg per
    // triangel), literalmeshen (färg, platt normal) och instanserna av dem —
    // och punkterna med och utan färg, och med rampens skalär.
    const MeshVariant vanliga[] = {
        {.normals = true},
        {.normals = true, .colors = true},
        {.colors = true},
        {.normals = true, .instanced = true},
        {.normals = true, .colors = true, .instanced = true},
    };
    for (const MeshVariant& v : vanliga)
        meshProgram(v);
    const PointVariant punkter[] = {{}, {.colors = true}, {.scalars = true}};
    for (const PointVariant& v : punkter)
        pointProgram(v);
}

} // namespace glaze::detail
