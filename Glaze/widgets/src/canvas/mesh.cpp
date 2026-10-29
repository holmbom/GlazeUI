// glaze::Mesh — datan, prövningen, segmenten, ändringsloggen och nivåerna.
// Ingen renderer här: uppladdningen är fönstrets, och
// plockningen bor i pick3d.cpp. Kontrakten står i glaze/canvas/mesh.h och
// mesh_data.h.

#include "glaze/canvas/mesh.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <utility>

#include "gfx/platform.h"
#include "mesh_data.h"

namespace glaze {
namespace detail {
namespace {

// Meshernas id — atomiskt, för konstruktorerna körs i vilken tråd som helst.
std::atomic<std::uint64_t> g_nextMeshId{1};

// Meshernas generation (mesh_data.h `meshGeneration`) — atomisk av samma skäl.
std::atomic<std::uint64_t> g_meshGeneration{1};

constexpr std::uint32_t kNoVertex = std::numeric_limits<std::uint32_t>::max();

bool finite(const Float3& p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

bool finite(const Point3& p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

// Lådan kring lägena, i världen.
Box3 boundsOf(const std::vector<Float3>& pos, const Point3& origin)
{
    if (pos.empty())
        return Box3{origin, origin};
    Float3 lo = pos[0], hi = pos[0];
    for (const Float3& p : pos) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    return Box3{{origin.x + lo.x, origin.y + lo.y, origin.z + lo.z},
                {origin.x + hi.x, origin.y + hi.y, origin.z + hi.z}};
}

// En ström är TOM eller har ett värde per läge.
template <class T>
bool streamOk(const std::vector<T>& s, std::size_t n)
{
    return s.empty() || s.size() == n;
}

// Prövar datan. Ett avslag loggas DÄR det upptäcks, en rad per orsak med det
// som behövs för att hitta felet.
bool descOk(const MeshDesc& d)
{
    const std::size_t n = d.positions.size();
    if (n > std::numeric_limits<std::uint32_t>::max()) {
        gfx::log("Mesh: %zu lägen är fler än ett uint32-index når — ingen mesh",
                 n);
        return false;
    }
    const bool user = std::all_of(d.user.begin(), d.user.end(),
                                  [n](const auto& u) { return streamOk(u, n); });
    if (!streamOk(d.normals, n) || !streamOk(d.colors, n) ||
        !streamOk(d.uvs, n) || !streamOk(d.scalars, n) || !user) {
        gfx::log("Mesh: en ström har inte ett värde per läge (%zu lägen) — "
                 "ingen mesh",
                 n);
        return false;
    }
    if (!finite(d.origin) ||
        !std::all_of(d.positions.begin(), d.positions.end(),
                     [](const Float3& p) { return finite(p); })) {
        gfx::log("Mesh: ett läge eller origot är inte ändligt — ingen mesh");
        return false;
    }
    const std::size_t elements = d.indices.empty() ? n : d.indices.size();
    if (elements > std::numeric_limits<std::uint32_t>::max() ||
        !std::all_of(d.indices.begin(), d.indices.end(),
                     [n](std::uint32_t i) { return i < n; })) {
        gfx::log("Mesh: ett index pekar utanför de %zu lägena, eller indexen "
                 "är fler än uint32 räknar — ingen mesh",
                 n);
        return false;
    }
    if (elements % 3 != 0) {
        gfx::log("Mesh: %zu %s är inte hela trianglar — ingen mesh", elements,
                 d.indices.empty() ? "lägen (utan index)" : "index");
        return false;
    }
    double last = 0.0;
    for (std::size_t k = 0; k < d.lods.size(); ++k) {
        const Lod& l = d.lods[k];
        if (l.first % 3 != 0 || l.first > elements ||
            std::min<std::size_t>(l.count, elements - l.first) % 3 != 0) {
            gfx::log("Mesh: nivå %zu (%u + %u) är inte hela trianglar inom "
                     "meshens %zu element — ingen mesh",
                     k, l.first, l.count, elements);
            return false;
        }
        const bool sist = k + 1 == d.lods.size();
        if (!std::isfinite(l.maxDistance) || l.maxDistance < 0.0 ||
            (l.maxDistance == 0.0 && !sist) ||
            (l.maxDistance != 0.0 && l.maxDistance <= last)) {
            gfx::log("Mesh: nivå %zu har avståndet %g — nivåerna ska stå i "
                     "växande avstånd, och 0 (obegränsat) bara sist — ingen mesh",
                     k, l.maxDistance);
            return false;
        }
        last = l.maxDistance;
    }
    return true;
}

// Ett segments lokala lägen och index ur originalets element [first, end):
// de globala lägena det använder i VÄXANDE ordning, och indexen ommappade
// till platserna i den listan. `local` är ett läge → lokalt-fält som lämnas
// rent (kNoVertex) efteråt.
void fillSegment(const MeshData& d, MeshSegment& s,
                 std::vector<std::uint32_t>& local)
{
    const std::uint32_t end = s.firstElement + s.elementCount;
    for (std::uint32_t e = s.firstElement; e < end; ++e) {
        const std::uint32_t v = d.indices[e];
        if (local[v] == kNoVertex) {
            local[v] = 0;
            s.vertices.push_back(v);
        }
    }
    std::sort(s.vertices.begin(), s.vertices.end());
    for (std::uint32_t i = 0; i < s.vertices.size(); ++i)
        local[s.vertices[i]] = i;
    s.indices.resize(s.elementCount);
    for (std::uint32_t e = s.firstElement; e < end; ++e)
        s.indices[e - s.firstElement] = local[d.indices[e]];
    for (std::uint32_t v : s.vertices)
        local[v] = kNoVertex;
}

void addSegment(const MeshData& d, std::vector<MeshSegment>& out,
                std::uint32_t first, std::uint32_t count)
{
    MeshSegment s;
    s.firstElement = first;
    s.elementCount = count;
    s.lods.reserve(d.lods.size());
    for (const Lod& l : d.lods) {
        const IndexRange r = localRange(s, l.first, l.count);
        s.lods.push_back(Lod{r.first, r.count, l.maxDistance});
    }
    out.push_back(std::move(s));
}

// Världspunkter → lokala lägen kring lådans mitt (double hela vägen tills
// skillnaden är liten). Ett läge som inte är ändligt blir NaN — konstruktorn
// avvisar det.
void fromWorldInto(std::span<const Point3> pos, MeshDesc& d)
{
    bool   any = false;
    Point3 lo{}, hi{};
    for (const Point3& p : pos) {
        if (!finite(p))
            continue;
        if (!any) {
            lo = hi = p;
            any     = true;
        }
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    d.origin = {(lo.x + hi.x) / 2.0, (lo.y + hi.y) / 2.0, (lo.z + hi.z) / 2.0};
    d.positions.resize(pos.size());
    for (std::size_t i = 0; i < pos.size(); ++i)
        d.positions[i] = {static_cast<float>(pos[i].x - d.origin.x),
                          static_cast<float>(pos[i].y - d.origin.y),
                          static_cast<float>(pos[i].z - d.origin.z)};
}

} // namespace

std::uint64_t meshGeneration()
{
    return g_meshGeneration.load(std::memory_order_relaxed);
}

void MeshData::markChanged(MeshStream stream, std::uint32_t first,
                           std::uint32_t count)
{
    ++version;
    if (stream == MeshStream::Positions)
        g_meshGeneration.fetch_add(1, std::memory_order_relaxed);
    if (first == 0 && count == positions.size()) {
        // Hela strömmen: äldre ändringar av samma ström behövs inte längre.
        std::erase_if(changes, [stream](const MeshChange& c) {
            return c.stream == stream;
        });
    }
    changes.push_back(MeshChange{version, stream, first, count});
    if (changes.size() > kMaxMeshChanges) {
        const std::size_t drop = changes.size() / 2;
        base = changes[drop - 1].version;
        changes.erase(changes.begin(),
                      changes.begin() + static_cast<std::ptrdiff_t>(drop));
    }
}

std::size_t vertexBytes(const MeshData& d)
{
    std::size_t b = sizeof(Float3);
    if (!d.normals.empty())
        b += sizeof(Float3);
    if (!d.colors.empty())
        b += sizeof(gfx::Color);
    if (!d.uvs.empty())
        b += sizeof(std::array<float, 2>);
    if (!d.scalars.empty())
        b += sizeof(float);
    for (const auto& u : d.user)
        if (!u.empty())
            b += sizeof(std::array<float, 4>);
    return b;
}

IndexRange localRange(const MeshSegment& s, std::uint32_t first,
                      std::uint32_t count)
{
    const std::uint64_t lo = std::max<std::uint64_t>(first, s.firstElement);
    const std::uint64_t hi =
        std::min<std::uint64_t>(std::uint64_t{first} + count,
                                std::uint64_t{s.firstElement} + s.elementCount);
    if (lo >= hi)
        return IndexRange{0, 0};
    return IndexRange{static_cast<std::uint32_t>(lo - s.firstElement),
                      static_cast<std::uint32_t>(hi - lo)};
}

std::vector<MeshSegment> segmentMesh(const MeshData& d, std::size_t maxBytes)
{
    std::vector<MeshSegment> out;
    const std::uint32_t      ne = d.elementCount();
    if (ne == 0)
        return out;
    // Ett segment rymmer minst en triangel, vad gränsen än säger.
    const std::size_t maxVerts = std::max<std::size_t>(maxBytes / vertexBytes(d), 3);
    const std::size_t maxIdx =
        std::max<std::size_t>(maxBytes / sizeof(std::uint32_t), 3);

    if (!d.isIndexed()) {
        // På triangelgränser: hela trianglar per segment.
        const auto per = static_cast<std::uint32_t>(
            std::min<std::size_t>(maxVerts / 3 * 3, ne));
        for (std::uint32_t f = 0; f < ne;) {
            const std::uint32_t c = std::min(per, ne - f);
            addSegment(d, out, f, c);
            f += c;
        }
        return out;
    }
    if (d.positions.size() <= maxVerts && ne <= maxIdx) {
        addSegment(d, out, 0, ne);   // odelat: meshens egna lägen och index
        return out;
    }

    // På indexintervall: trianglarna i ordning, och ett nytt segment när
    // nästa triangels NYA lägen eller dess index inte ryms. `seen[v]` är det
    // senaste segment som tagit med läget v.
    std::vector<std::uint32_t> seen(d.positions.size(), kNoVertex);
    std::vector<std::uint32_t> local(d.positions.size(), kNoVertex);
    std::uint32_t              seg = 0, first = 0;
    std::size_t                verts = 0;
    for (std::uint32_t e = 0; e < ne; e += 3) {
        const std::uint32_t v[3] = {d.indices[e], d.indices[e + 1],
                                    d.indices[e + 2]};
        const auto fresh = [&] {
            std::size_t n = 0;
            for (int k = 0; k < 3; ++k)
                if (seen[v[k]] != seg && (k == 0 || v[k] != v[0]) &&
                    (k < 2 || v[k] != v[1]))
                    ++n;
            return n;
        };
        std::size_t add = fresh();
        if (e > first && (verts + add > maxVerts || e + 3 - first > maxIdx)) {
            addSegment(d, out, first, e - first);
            fillSegment(d, out.back(), local);
            ++seg;
            first = e;
            verts = 0;
            add   = fresh();
        }
        for (int k = 0; k < 3; ++k)
            seen[v[k]] = seg;
        verts += add;
    }
    addSegment(d, out, first, ne - first);
    fillSegment(d, out.back(), local);
    return out;
}

int lodLevel(const MeshData& d, double distance)
{
    return lodLevel(std::span<const Lod>(d.lods), distance);
}

int lodLevel(std::span<const Lod> lods, double distance)
{
    if (lods.empty())
        return kWholeMesh;
    for (std::size_t k = 0; k < lods.size(); ++k)
        if (lods[k].maxDistance == 0.0 || distance <= lods[k].maxDistance)
            return static_cast<int>(k);
    return kNoLevel;
}

const Bvh& meshBvh(const MeshData& d, MeshPicking& p)
{
    std::call_once(p.once, [&] { p.bvh = buildBvh(d.positions, d.indices); });
    return p.bvh;
}

} // namespace detail

Mesh::Mesh(MeshDesc desc)
{
    if (desc.positions.empty())
        return;   // tomt är inget fel
    if (!detail::descOk(desc))
        return;   // loggat
    auto d         = std::make_shared<detail::MeshData>();
    d->id          = detail::g_nextMeshId.fetch_add(1, std::memory_order_relaxed);
    d->positions   = std::move(desc.positions);
    d->normals     = std::move(desc.normals);
    d->colors      = std::move(desc.colors);
    d->uvs         = std::move(desc.uvs);
    d->scalars     = std::move(desc.scalars);
    d->user        = std::move(desc.user);
    d->indices     = std::move(desc.indices);
    d->origin      = desc.origin;
    d->keepCpuCopy = desc.keepCpuCopy;
    d->lods        = std::move(desc.lods);
    const std::uint32_t ne = d->elementCount();
    for (Lod& l : d->lods)
        l.count = std::min(l.count, ne - l.first);
    d->bounds   = detail::boundsOf(d->positions, d->origin);
    d->segments = detail::segmentMesh(*d, detail::kMeshSegmentBytes);
    d_          = std::move(d);
}

Mesh Mesh::fromWorld(std::span<const Point3>    positions,
                     std::vector<std::uint32_t> indices)
{
    MeshDesc d;
    detail::fromWorldInto(positions, d);
    d.indices = std::move(indices);
    return Mesh(std::move(d));
}

Mesh Mesh::triangles(std::vector<Point3> positions, gfx::Color color)
{
    MeshDesc d;
    detail::fromWorldInto(positions, d);
    d.colors.assign(d.positions.size(), color);
    return Mesh(std::move(d));
}

Mesh Mesh::indexed(std::vector<Point3>        positions,
                   std::vector<std::uint32_t> indices, gfx::Color color)
{
    if (indices.empty())
        return Mesh{};
    MeshDesc d;
    detail::fromWorldInto(positions, d);
    d.colors.assign(d.positions.size(), color);
    d.indices = std::move(indices);
    return Mesh(std::move(d));
}

std::size_t Mesh::vertexCount() const
{
    return d_ ? d_->positions.size() : 0;
}

std::size_t Mesh::triangleCount() const
{
    return d_ ? d_->triangleCount() : 0;
}

Box3 Mesh::bounds() const
{
    return d_ ? d_->bounds : Box3{};
}

void Mesh::setPositions(std::size_t first, std::span<const Float3> positions)
{
    const std::size_t n = d_ ? d_->positions.size() : 0;
    if (!d_ || positions.empty() || first > n || positions.size() > n - first ||
        !std::all_of(positions.begin(), positions.end(),
                     [](const Float3& p) { return detail::finite(p); })) {
        gfx::log("Mesh::setPositions: %zu lägen från %zu i en mesh med %zu — "
                 "utanför, tomt eller inte ändligt; ingenting skrivs",
                 positions.size(), first, n);
        return;
    }
    std::copy(positions.begin(), positions.end(),
              d_->positions.begin() + static_cast<std::ptrdiff_t>(first));
    d_->markChanged(detail::MeshStream::Positions,
                    static_cast<std::uint32_t>(first),
                    static_cast<std::uint32_t>(positions.size()));
    d_->bounds  = detail::boundsOf(d_->positions, d_->origin);
    d_->picking = std::make_shared<detail::MeshPicking>();
}

void Mesh::setColors(std::size_t first, std::span<const gfx::Color> colors)
{
    const std::size_t n = d_ ? d_->colors.size() : 0;
    if (!d_ || colors.empty() || first > n || colors.size() > n - first) {
        gfx::log("Mesh::setColors: %zu färger från %zu i en mesh med %zu "
                 "färger — utanför, tomt eller ingen färgström; ingenting "
                 "skrivs",
                 colors.size(), first, n);
        return;
    }
    std::copy(colors.begin(), colors.end(),
              d_->colors.begin() + static_cast<std::ptrdiff_t>(first));
    d_->markChanged(detail::MeshStream::Colors, static_cast<std::uint32_t>(first),
                    static_cast<std::uint32_t>(colors.size()));
}

void Mesh::preparePicking() const
{
    if (!d_)
        return;
    if (!d_->keepCpuCopy) {
        if (!d_->loggedNoCopy.exchange(true))
            gfx::log("Mesh::preparePicking: meshen saknar CPU-kopia "
                     "(keepCpuCopy = false) och kan inte plockas");
        return;
    }
    const std::shared_ptr<detail::MeshPicking> p = d_->picking;
    detail::meshBvh(*d_, *p);
}

} // namespace glaze
