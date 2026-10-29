#pragma once
// INTERN header — meshens och punktmolnets delade data: det ett
// `glaze::Mesh`- eller `glaze::PointCloud`-handtag pekar på,
// och som ritningen och plockningen läser. Ingår inte i publika API:t; bara
// src/canvas/*.cpp och grindarna drar in den.
//
// SEGMENTEN: meshens GPU-data delas i delar om högst
// `kMeshSegmentBytes` vertexdata OCH lika mycket index — D3D11 tillåter
// IA-buffertar på max(128 MB, ¼ VRAM), och Intel UHD ger 128 MB (en stolpe är
// ≈ 12 MB, men terräng och sammanslagna meshar blir större). Utan index delas
// meshen på triangelgränser; med index på indexintervall med LOKAL
// ommappning. Segmenten är
// UPPLADDNINGENS vyer — CPU-kopian står kvar i originalets ordning, och det
// är den plockningen läser, så en träff har alltid originalets
// triangelindex utan omväg. Ett segment är ett SAMMANHÄNGANDE stycke av
// originalets trianglar, så tabellen tillbaka är dess första element.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <gfx/color.h>

#include "bvh.h"
#include "glaze/canvas/mesh.h"

namespace glaze::detail {

inline constexpr std::size_t kMeshSegmentBytes = std::size_t{64} << 20;

// Ett SEGMENT — ett stycke av originalets element, `firstElement` …
// `firstElement + elementCount` (hela trianglar).
//   Utan index: lägena är samma intervall i meshens strömmar.
//   Med index, odelat (ett enda segment): meshens egna lägen och index.
//   Med index, delat: `vertices` är segmentets lägen som GLOBALA lägen i
//     växande ordning (lokal → global — en ändrad följd av globala lägen är
//     därmed en följd av lokala), och `indices` segmentets LOKALA index.
// `lods` är meshens nivåer i segmentets lokala element, i samma ordning som
// meshens — en nivå som inte når hit är tom.
struct MeshSegment {
    std::uint32_t              firstElement = 0;
    std::uint32_t              elementCount = 0;
    std::vector<std::uint32_t> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Lod>           lods;

    // Tabellen tillbaka: segmentets lokala triangel j är originalets
    // `firstTriangle() + j`.
    std::uint32_t firstTriangle() const { return firstElement / 3; }
};

// Strömmen en ändring gäller.
enum class MeshStream : std::uint8_t { Positions, Colors };

struct MeshChange {
    std::uint64_t version;
    MeshStream    stream;
    std::uint32_t first, count;   // lägen
};

// Plockningens BVH, byggd EN gång (`std::call_once`). `setPositions` byter
// ut hela posten mot en ny, så nästa plockning bygger om; den som håller den
// gamla (en pågående plockning) läser färdigt i den.
struct MeshPicking {
    std::once_flag once;
    Bvh            bvh;
};

struct MeshData {
    std::uint64_t id = 0;   // residensens nyckel, återanvänds aldrig

    // CPU-KOPIAN i originalets ordning — lägena relativt `origin`.
    std::vector<Float3>                              positions;
    std::vector<Float3>                              normals;
    std::vector<gfx::Color>                          colors;
    std::vector<std::array<float, 2>>                uvs;
    std::vector<float>                               scalars;
    std::array<std::vector<std::array<float, 4>>, 4> user;
    std::vector<std::uint32_t>                       indices;
    // Nivåerna med `count` KLAMPAT till meshens slut (aldrig kRangeToEnd).
    std::vector<Lod> lods;
    Point3           origin{};
    bool             keepCpuCopy = true;
    Box3             bounds{};   // i världen

    std::vector<MeshSegment> segments;

    // ÄNDRINGSLOGGEN (som Bitmaps): `version` stegas vid varje ändring och
    // `changes` bär varje ändring med version > `base`, utom de en senare
    // ändring av HELA samma ström gjort överflödiga. Ett fönster som laddat
    // upp version v kommer ikapp med ändringarna efter v om v ≥ base, och
    // behöver hela meshen annars.
    std::uint64_t           version = 0;
    std::uint64_t           base    = 0;
    std::vector<MeshChange> changes;

    std::shared_ptr<MeshPicking> picking = std::make_shared<MeshPicking>();
    // Loggat en gång per mesh: plockning utan CPU-kopia, och en ogiltig nivå
    // eller ett ogiltigt intervall i en plockning.
    std::atomic<bool> loggedNoCopy{false};
    std::atomic<bool> loggedRange{false};

    bool          isIndexed() const { return !indices.empty(); }
    std::uint32_t elementCount() const
    {
        return static_cast<std::uint32_t>(isIndexed() ? indices.size()
                                                      : positions.size());
    }
    std::uint32_t triangleCount() const { return elementCount() / 3; }
    // Läget i triangel `t`:s hörn `k` (0–2), i meshens globala lägen.
    std::uint32_t corner(std::uint32_t t, std::uint32_t k) const
    {
        return isIndexed() ? indices[std::size_t{3} * t + k] : 3 * t + k;
    }

    // Antecknar en ändring av lägena first … first + count i `stream`.
    void markChanged(MeshStream stream, std::uint32_t first,
                     std::uint32_t count);
};

// MESHERNAS GENERATION — stegas varje gång en meshs LÄGEN
// ändras någonstans i processen (`Mesh::setPositions`, `markChanged` för
// lägena; färgerna flyttar ingen låda). Scenens låda och terrängens rutnät
// har den i sin nyckel: ett tal i stället för en vandring över meshernas
// versioner vid varje fråga. Atomisk — meshar ändras i vilken tråd som helst.
std::uint64_t meshGeneration();

// Handtagens data för ramverkets ritning, plockning och grindarna.
struct MeshAccess {
    static const std::shared_ptr<MeshData>& data(const Mesh& m) { return m.d_; }
    static const std::shared_ptr<PointCloudData>& data(const PointCloud& p)
    {
        return p.d_;
    }
};

// Byte per läge — strömmarna meshen HAR, i CPU-formen (GPU:ns packning är
// aldrig större, så segmentgränsen håller där också).
std::size_t vertexBytes(const MeshData& d);

// Delar meshen i segment om högst `maxBytes` vertexdata och lika mycket
// index (fyra byte per index). Ett segment rymmer alltid minst en triangel.
// Meshens konstruktor kör den med `kMeshSegmentBytes`; grindarna med mindre.
std::vector<MeshSegment> segmentMesh(const MeshData& d, std::size_t maxBytes);

// Elementintervallet [first, first + count) (redan klampat) i segmentets
// lokala element — tomt när det inte når segmentet.
IndexRange localRange(const MeshSegment& s, std::uint32_t first,
                      std::uint32_t count);

// DETALJNIVÅN på avståndet `distance` (ögat till meshens låda): den första
// nivån vars maxDistance räcker (0 = obegränsat). `kWholeMesh` när meshen
// saknar nivåer — hela meshen ritas — och `kNoLevel` bortom den sista nivån,
// där ingenting ritas. Ritningen och plockningen väljer med SAMMA regel.
inline constexpr int kWholeMesh = -1;
inline constexpr int kNoLevel   = -2;
int lodLevel(const MeshData& d, double distance);
// Samma regel över en lista nivåer — fönstrets kopia av en mesh bär sina
// egna (residency.h), så den gamla kopian väljer med sina nivåer.
int lodLevel(std::span<const Lod> lods, double distance);

// Plockningens BVH, byggd lat och en gång i `p` (meshens nuvarande post).
const Bvh& meshBvh(const MeshData& d, MeshPicking& p);

// ------------------------------------------------------------- punktmolnet

// En CHUNK av molnet: punkterna `first … first + count` i datans ordning,
// med eget origo (lådans mitt) — lägena i chunken är relativa det.
struct PointChunk {
    Point3        origin{};
    Box3          bounds{};   // i världen
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

struct PointCloudData {
    std::uint64_t           id = 0;
    std::vector<Float3>     positions;   // var och en relativt SIN chunks origo
    std::vector<gfx::Color> colors;      // tomt eller en per punkt
    std::vector<float>      scalars;     // tomt eller en per punkt
    std::vector<PointChunk> chunks;
    // Datans plats → punktens plats i INDATAN; tomt när ordningen är
    // indatans (ett moln om högst `kPointChunkSize` punkter delas inte).
    std::vector<std::uint32_t> order;
    Box3                       bounds{};
};

} // namespace glaze::detail
