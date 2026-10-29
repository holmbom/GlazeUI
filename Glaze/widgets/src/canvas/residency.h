#pragma once
// INTERN — FÖREMÅLENS RESIDENS PER FÖNSTER. Meshar, instanser och punktmoln
// på GPU:n, och de inbyggda
// Mesh- och Points-programmen kompilerade för fönstrets renderer. En del av
// `CanvasResources` (resources.h: `objects()`) och samma livstidsregel: allt
// här hör till fönstrets renderer, `trim()` överst i `renderContent` är det
// enda stället något släpps, och det rivs med CanvasResources i
// `App::executeClose` FÖRE bildcachen.
//
// EN GPU-KOPIA PER DATA. En mesh (`MeshData::id`), en instansmängd
// (`InstanceSet::id`) och ett punktmoln (`PointCloudData::id`) laddas upp EN
// gång per fönster, hur många former som än bär den — tusen lådor är
// enhetslådan en gång. Meshens SEGMENT (≤ 64 MiB, mesh_data.h) blir var sin
// vertex- och indexbuffert, punktmolnets CHUNKAR var sin buffert. Vertexen
// bär de strömmar meshen HAR — också user0–3, som bara ett eget program
// läser.
//
// UPPLADDNINGEN STRÖMMAR inom fönstrets BUDGET per frame — densamma som
// bildernas (`App::setCanvasUploadBudget`, `ImageCache::uploadRoom`) — i hela
// element (en vertex, ett index, en instans, en punkt), och framens första
// uppladdning får hela budgeten (den går alltid igenom). En kopia RITAS FÖRST
// NÄR DEN ÄR KOMPLETT. Det som inte hann antecknas (`ImageCache::
// deferUpload`): fönstret ritar då om allt i nästa frame, ritytans 3D-mål är
// inaktuellt och strömningen fortsätter — och när inget väntar ber ingen om
// en frame (hård regel 1).
//
// PLATSEN (`ShapeEntry::id` — den överlever `setShape` och lagrets
// ombyggnad) VISAR en kopia. När formens data byts ut — en ny mesh, en ny
// instansmängd, ett nytt moln, eller en mesh vars ändringslogg inte räcker
// till fönstrets kopia — ritas den GAMLA kopian på platsen tills den nya är
// komplett: DUBBELBUFFRINGEN. Sedan visar platsen den nya, och den gamla går
// till `retired` och släpps i nästa `trim()`. En NY plats ritar ingenting
// förrän dess kopia är komplett.
//
// ÄNDRINGSLOGGEN (`Mesh::setPositions`/`setColors`): en kopia som ligger
// efter sin mesh laddar upp BARA de ändrade lägena — omsorterade per segment,
// hela vertex — och allt i EN frame, så ingen halvt uppdaterad mesh ritas:
// när det ryms i budgeten eller är framens första uppladdning, annars i
// nästa frame (den gamla datan ritas en frame till). Räcker loggen inte
// laddas en ny kopia upp, dubbelbuffrad.
//
// PROGRAMMEN kompileras latent per variant (programs3d.h: 128 Mesh och 4
// Points) — och de vanligaste i förväg när ritytans 3D-mål skapas för en
// scen med föremål (`precompile`): D3DCompile per variant ger annars ett
// hack vid första användningen. Ett program som inte kompilerar loggas av
// backenden och ritar ingenting.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <gfx/program.h>
#include <gfx/renderer.h>
#include <gfx/scene.h>

#include "glaze/canvas/mesh.h"
#include "instances.h"
#include "mesh_data.h"
#include "programs3d.h"
#include "scene.h"

namespace glaze {

class ImageCache;

namespace detail {

class ObjectResidency {
public:
    explicit ObjectResidency(gfx::Renderer& r);
    ~ObjectResidency();
    ObjectResidency(const ObjectResidency&)            = delete;
    ObjectResidency& operator=(const ObjectResidency&) = delete;

    // Framens början (CanvasResources::trim): släpper det som ersattes i
    // förra framen, platserna vars form dött och kopiorna vars data dött
    // (en plats som fortfarande visar en kopia håller den vid liv).
    void trim();

    // ---- KOPIORNA
    // En MESH på GPU:n — allt den behöver för att ritas, oberoende av den
    // mesh formen bär NU (dubbelbuffringen ritar den gamla).
    struct GpuMesh {
        std::weak_ptr<const MeshData> src;
        std::uint64_t     id      = 0;   // meshens
        std::uint64_t     version = 0;   // ändringsloggens version i kopian
        Point3            origin{};      // meshens origo (double)
        Box3              localBox{};    // lägenas låda relativt origot
        std::vector<Lod>  lods;          // nivåerna (count klampat)
        std::uint32_t     elements = 0;  // index, eller lägen utan index
        bool              indexed  = false;
        MeshVariant       streams;       // strömmarna kopian BÄR
        // EGNA PROGRAMS strömmar: bit i = user<i>, Float4 sist i
        // vertexen på platsen kUser0 + i — de inbyggda programmen läser dem
        // inte.
        std::uint8_t      user = 0;
        gfx::VertexLayout layout;        // de strömmarna, ström 0
        gfx::VertexLayout layoutInstanced;   // + instansströmmen
        struct Segment {
            std::uint32_t                   firstElement = 0, elementCount = 0;
            std::uint32_t                   vertices = 0;
            std::unique_ptr<gfx::GpuBuffer> vb, ib;
        };
        std::vector<Segment> segments;
        bool                 complete = false;
        // Strömningen: nästa segment, om det är indexen som laddas, och hur
        // många element av den bufferten som är uppe.
        std::size_t   nextSegment = 0;
        bool          inIndices   = false;
        std::uint32_t done        = 0;
    };
    // En INSTANSMÄNGD — raderna i cellordning i EN buffert.
    struct GpuInstances {
        std::weak_ptr<const InstanceSet> src;
        std::uint64_t                    id = 0;
        std::unique_ptr<gfx::GpuBuffer>  buf;
        std::uint32_t                    count = 0, done = 0;
        bool                             complete = false;
    };
    // Ett PUNKTMOLN — en buffert per chunk, i molnets chunkordning.
    struct GpuCloud {
        std::weak_ptr<const PointCloudData> src;
        std::uint64_t                       id = 0;
        PointVariant                        streams;   // färger, skalärer
        gfx::VertexLayout                   layout;
        std::vector<std::unique_ptr<gfx::GpuBuffer>> chunks;
        std::size_t                         nextChunk = 0;
        std::uint32_t                       done      = 0;
        bool                                complete  = false;
    };

    // DET PLATSEN VISAR — FORMEN och kopiorna den ritas med i den här framen:
    // formens nuvarande när dess data är uppe, annars den förra (placeringen,
    // stilen och slaget hör till den gamla datan). `shape` nullptr när
    // platsen inget har att visa än.
    struct Shown {
        const Shape*        shape = nullptr;
        const GpuMesh*      mesh  = nullptr;
        const GpuInstances* inst  = nullptr;
        const GpuCloud*     cloud = nullptr;
    };
    // Platsen för formen `entry`: med `upload` strömmas dess data inom
    // budgeten (`images` — fönstrets bildcache; nullptr = ingen budget), och
    // svaret är det platsen visar efter det. Ett föremål utan data visar
    // ingenting.
    Shown object(const std::shared_ptr<ShapeEntry>& entry, ImageCache* images,
                 bool upload);

    // ---- PROGRAMMEN
    // Varianten kompilerad för fönstret (latent), eller nullptr när den inte
    // kompilerade (backenden har loggat felet).
    const gfx::ShaderProgram* meshProgram(const MeshVariant& v);
    const gfx::ShaderProgram* pointProgram(const PointVariant& v);
    // De vanligaste varianterna i förväg — en gång per fönster.
    void precompile();

    // ---- räknarna (grindarna)
    std::size_t   meshCount() const { return meshes_.size(); }
    std::size_t   instanceSetCount() const { return instances_.size(); }
    std::size_t   cloudCount() const { return clouds_.size(); }
    std::size_t   slotCount() const { return slots_.size(); }
    // Hur många kopior som SKAPATS (en ny mesh, mängd eller moln, eller en
    // ny kopia när ändringsloggen inte räckte) — en kameraändring skapar
    // inga (canvas_perf3d R).
    std::uint64_t copiesMade() const { return copiesMade_; }

private:
    gfx::Renderer* r_;

    std::unordered_map<std::uint64_t, std::shared_ptr<GpuMesh>>      meshes_;
    std::unordered_map<std::uint64_t, std::shared_ptr<GpuInstances>> instances_;
    std::unordered_map<std::uint64_t, std::shared_ptr<GpuCloud>>     clouds_;

    struct Slot {
        std::weak_ptr<const ShapeEntry> src;
        bool                            showing = false;
        Shape                           shape;   // formen platsen VISAR
        std::shared_ptr<GpuMesh>        mesh;    // …och dess kopior
        std::shared_ptr<GpuInstances>   inst;
        std::shared_ptr<GpuCloud>       cloud;
    };
    std::unordered_map<std::uint64_t, Slot> slots_;

    std::unordered_map<std::uint32_t, std::unique_ptr<gfx::ShaderProgram>> meshPrograms_;
    std::unordered_map<std::uint32_t, std::unique_ptr<gfx::ShaderProgram>> pointPrograms_;
    bool precompiled_ = false;

    // Det som slutade visas MITT I en frame lever till nästa trim.
    std::vector<std::shared_ptr<GpuMesh>>      retiredMeshes_;
    std::vector<std::shared_ptr<GpuInstances>> retiredInstances_;
    std::vector<std::shared_ptr<GpuCloud>>     retiredClouds_;

    std::uint64_t copiesMade_ = 0;

    // Kopian för datan (skapad vid behov, ikapp med ändringsloggen så långt
    // budgeten räcker); nullptr utan data.
    std::shared_ptr<GpuMesh>      meshCopy(const std::shared_ptr<MeshData>& d,
                                           ImageCache* images, bool upload);
    std::shared_ptr<GpuInstances> instanceCopy(
        const std::shared_ptr<const InstanceSet>& s, ImageCache* images,
        bool upload);
    std::shared_ptr<GpuCloud>     cloudCopy(const std::shared_ptr<PointCloudData>& d,
                                            ImageCache* images, bool upload);
    // Strömmar en kopia vidare så långt budgeten räcker; true när den är
    // komplett.
    bool stream(GpuMesh& g, const MeshData& d, ImageCache* images);
    bool stream(GpuInstances& g, const InstanceSet& s, ImageCache* images);
    bool stream(GpuCloud& g, const PointCloudData& d, ImageCache* images);
    // En komplett meshkopia ikapp med ändringsloggen: true när den är ikapp
    // (eller ändringarna fick vänta på budgeten — den gamla datan ritas),
    // false när loggen inte räcker och en ny kopia behövs.
    bool catchUp(GpuMesh& g, const MeshData& d, ImageCache* images);
};

} // namespace detail
} // namespace glaze
