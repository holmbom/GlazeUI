// CPU-ORIGINALENS BYTE i en scen (scene.h `sceneCpuBytes`).
//
// Varje LAGER bär sin egen lista — den data dess former håller, en post per
// data, och chunkarnas byte — räknad om bara när lagret ändrats. Scenens summa
// slår ihop listorna och räknar data som flera lager delar en gång. Ett lager
// som byggs om kostar alltså sina egna former, inte hela scenens.

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "../image_cache.h"
#include "image_data.h"
#include "instances.h"
#include "mesh_data.h"
#include "scene.h"

namespace glaze::detail {

// Datans slag — id:na är unika inom ett slag.
enum class CpuKind : std::uint8_t { Mesh, Cloud, Instances, Bitmap };

struct CpuItem {
    CpuKind       kind;
    std::uint64_t id;
    std::size_t   bytes;
};

struct LayerCpu {
    std::uint64_t        shapes = 0, meshes = 0, pixels = 0;   // nyckeln
    std::vector<CpuItem> items;                                // en per data
    std::size_t          chunkBytes = 0;
};

namespace {

std::uint64_t itemKey(CpuKind kind, std::uint64_t id)
{
    // Slaget i de två översta bitarna — id:na är löpnummer och når aldrig dit.
    constexpr int kKindShift = 62;
    return (static_cast<std::uint64_t>(kind) << kKindShift) | id;
}

// Lagrets lista, byggd ur dess former.
struct Collect {
    LayerCpu&                         ut;
    std::unordered_set<std::uint64_t> sedda;

    void add(CpuKind kind, std::uint64_t id, std::size_t bytes)
    {
        if (id != 0 && sedda.insert(itemKey(kind, id)).second)
            ut.items.push_back(CpuItem{kind, id, bytes});
    }
    void mesh(const Mesh& m)
    {
        if (const std::shared_ptr<MeshData>& d = MeshAccess::data(m))
            add(CpuKind::Mesh, d->id,
                d->positions.size() * vertexBytes(*d) +
                    d->indices.size() * sizeof(std::uint32_t));
    }
    void cloud(const PointCloud& p)
    {
        if (const std::shared_ptr<PointCloudData>& d = MeshAccess::data(p))
            add(CpuKind::Cloud, d->id,
                d->positions.size() * sizeof(Float3) + d->colors.size() * sizeof(gfx::Color) +
                    d->scalars.size() * sizeof(float) +
                    d->order.size() * sizeof(std::uint32_t));
    }
    void set(const std::shared_ptr<const InstanceSet>& s)
    {
        if (s)
            add(CpuKind::Instances, s->id,
                s->rows.size() * sizeof(MeshInstance) +
                    s->order.size() * sizeof(std::uint32_t) +
                    s->cells.size() * sizeof(InstanceCell));
    }
    void image(const ImageSource& src)
    {
        if (src.kind() != ImageSource::Kind::Bitmap)
            return;
        std::uint64_t     id = 0;
        const std::size_t b  = ImageCache::pixelBytes(src.bitmap(), id);
        add(CpuKind::Bitmap, id, b);
    }
    void meshStyle(const MeshStyle& st)
    {
        image(st.texture);
        for (const ImageSource& t : st.textures)
            image(t);
    }
};

const LayerCpu& layerCpu(const LayerData& L, std::uint64_t meshes, std::uint64_t pixels)
{
    if (!L.cpu)
        L.cpu = std::make_shared<LayerCpu>();
    LayerCpu& c = *L.cpu;
    if (c.shapes == L.shapesVersion && c.meshes == meshes && c.pixels == pixels)
        return c;
    c.items.clear();
    c.chunkBytes = 0;
    Collect samla{c, {}};
    // Bildkörningarnas bilder står i körningens nyckel, föremålen i
    // körningarnas listor — lagrets formlista läses inte (en ögonblicksbild
    // bär ingen).
    for (const std::shared_ptr<RunData>& run : L.runs)
        if (run->key.kind == PartKind::Image)
            samla.image(run->key.image);
    for (const std::shared_ptr<ShapeEntry>& entry : liveObjects(L)) {
        const Shape& s = entry->shape;
        switch (s.kind()) {
        case Shape::Kind::Mesh:
        case Shape::Kind::Area3:
            samla.mesh(s.mesh());
            samla.meshStyle(s.meshStyle());
            break;
        case Shape::Kind::Instances:
            samla.mesh(s.mesh());
            samla.set(s.instanceSet());
            samla.meshStyle(s.meshStyle());
            break;
        case Shape::Kind::Points3:
            samla.cloud(s.pointCloud());
            break;
        case Shape::Kind::Image:   // bildkörningarna, ovan
        case Shape::Kind::None:
        case Shape::Kind::Lines:
        case Shape::Kind::Area:
        case Shape::Kind::Text:
        case Shape::Kind::Markers:
        case Shape::Kind::Lines3:
        case Shape::Kind::Text3:
        case Shape::Kind::Markers3:
        case Shape::Kind::Pass:
            break;
        }
    }
    // Lagrets byggda geometri: chunkarnas vertex och index.
    for (const std::shared_ptr<RunData>& run : L.runs)
        for (const std::shared_ptr<ChunkData>& ch : run->chunks)
            c.chunkBytes += ch->vertexCount() * ch->vertexSize() +
                            ch->indices.size() * sizeof(std::uint32_t);
    c.shapes = L.shapesVersion;
    c.meshes = meshes;
    c.pixels = pixels;
    return c;
}

} // namespace

std::size_t sceneCpuBytes(const SceneData& sc)
{
    const std::uint64_t meshes = meshGeneration(), pixels = pixelGeneration();
    KeyHash             k;
    k.mix(meshes);
    k.mix(pixels);
    for (const std::shared_ptr<LayerData>& L : sc.layers) {
        k.mix(L->id);
        k.mix(L->shapesVersion);
    }
    // Skalets och lagrens cacher — under skalets lås (en bild kan räknas av
    // två trådar).
    const std::lock_guard<std::mutex> vakt(nodeLock(sc.id));
    if (sc.cpuCache.valid && sc.cpuCache.key == k.h)
        return sc.cpuCache.bytes;

    // Lagrens listor ihop — data som flera lager delar räknas en gång.
    std::size_t                       bytes = 0;
    std::unordered_set<std::uint64_t> sedda;
    for (const std::shared_ptr<LayerData>& L : sc.layers) {
        const LayerCpu& c = layerCpu(*L, meshes, pixels);
        bytes += c.chunkBytes;
        for (const CpuItem& i : c.items)
            if (sedda.insert(itemKey(i.kind, i.id)).second)
                bytes += i.bytes;
    }
    sc.cpuCache = SceneData::CpuCache{k.h, bytes, true};
    return bytes;
}

} // namespace glaze::detail
