#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <gfx/color.h>

#include "glaze/canvas/geometry.h"

// glaze::Mesh och glaze::PointCloud — 3D-INNEHÅLL UR DATA. Trianglar och
// punkter som appen räknar fram i drift: en stolpe på
// miljontals trianglar ur ett konstruktionsrecept, ett punktmoln ur en
// laserskanning, en modell som redigeras. Ritytan ritar dem som former; det
// här är DATAN, som `Bitmap` är pixlarna.
//
//     glaze::MeshDesc d;
//     d.positions = std::move(lokala);         // Float3 relativt origot
//     d.origin    = {674000.0, 6580000.0, 0};  // världen, double
//     d.colors    = std::move(färger);         // en per läge, rak alfa
//     d.lods      = {{0, n0, 40.0}, {n0, n1, 0}};   // nära, sedan resten
//     glaze::Mesh stolpe(std::move(d));        // valfri tråd
//     stolpe.preparePicking();                 // valfri tråd — BVH:n nu, inte vid första klicket
//
//     auto m = glaze::Mesh::fromWorld(punkter, index);   // origot räknas fram
//
// ETT HANDTAG, inte ett värde: kopior DELAR datan (samma mesh i två former är
// EN mesh). Datan lever så länge något handtag gör det.
//
// PRECISIONEN är ramverkets (RTC): lägena är Float3 RELATIVT `origin`, som
// är double. `fromWorld` tar världspunkter och lägger origot i lådans mitt,
// så en SWEREF-mesh aldrig avrundas mer än sin egen storlek kräver.
//
// TRIANGLARNA: med `indices` är varje trippel en triangel (uint32); utan är
// varje tre lägen en. FRAMSIDAN är den sida triangeln går MOTURS sett ifrån
// (`Cull`). Ett ELEMENT är ett index — eller ett läge när meshen saknar
// index — och intervallen nedan (`Lod`, `IndexRange`) räknas i element, i
// HELA trianglar (multiplar av tre).
//
// TRÅDARNA: konstruktorerna, `fromWorld`/`triangles`/`indexed` och
// `preparePicking` går att köra i vilken tråd som helst (en arbetartråd
// bygger meshen och lämnar den till UI-tråden med `glaze::app().post`).
// Ändringar, ritning och plockning sker i UI-tråden — och ingen ändring får
// pågå medan en annan tråd kör `preparePicking` på samma mesh.
//
// Regel 8: data som räknas fram i drift har ingen `.glazeui`-form (Bitmaps
// prejudikat); små meshar uttrycks som literaler med `Mesh::triangles` och
// `Mesh::indexed`.

namespace glaze {

namespace detail {
struct MeshData;         // src/canvas/mesh_data.h
struct PointCloudData;
struct MeshAccess;
}

// Ett intervall SLUTAR vid meshens slut: `count` klampas dit, som
// `std::string::substr` — förvalet `kRangeToEnd` är resten, och 0 är tomt.
inline constexpr std::uint32_t kRangeToEnd = 0xFFFFFFFFu;

// Ett intervall av meshens element (markering, del av en mesh): `first` och
// `count` i hela trianglar. Förvalet är hela meshen.
struct IndexRange {
    std::uint32_t first = 0;
    std::uint32_t count = kRangeToEnd;
};

// En DETALJNIVÅ: elementen nivån ritar och det största avståndet (från ögat
// till meshens låda) den gäller för. Nivåerna står i VÄXANDE avstånd, och
// den första vars `maxDistance` räcker ritas; 0 betyder obegränsat och får
// bara stå på den sista. Bortom den sista nivån ritas ingenting — en mesh
// utan en obegränsad nivå försvinner på avstånd. Utan nivåer ritas hela
// meshen alltid.
struct Lod {
    std::uint32_t first       = 0;
    std::uint32_t count       = kRangeToEnd;
    double        maxDistance = 0.0;
};

// Meshens data. Varje ström är TOM eller har ett värde per läge (`positions`
// bestämmer antalet). `user` är egna programs attribut (user0–3).
struct MeshDesc {
    std::vector<Float3>                              positions;   // relativt `origin`
    std::vector<Float3>                              normals;
    std::vector<gfx::Color>                          colors;      // rak alfa
    std::vector<std::array<float, 2>>                uvs;
    std::vector<float>                               scalars;
    std::array<std::vector<std::array<float, 4>>, 4> user;
    std::vector<std::uint32_t>                       indices;     // tomt = icke-indexerat
    std::vector<Lod>                                 lods;
    Point3                                           origin{};
    // CPU-kopian är PLOCKNINGENS: den som stänger av den kan inte plocka mot
    // meshen (det loggas en gång).
    bool keepCpuCopy = true;
};

// Hur många ändringar en mesh antecknar innan loggen ger upp och nästa
// ritning laddar upp hela strömmen.
inline constexpr std::size_t kMaxMeshChanges = 32;

class Mesh {
public:
    // Ingen mesh: `valid()` falskt, ritas inte.
    Mesh() = default;

    // Meshen ur sin data. Ogiltig data ger INGEN mesh + logg: en ström med
    // fel längd, ett index utanför lägena, lägen eller index som inte är hela
    // trianglar, ett läge eller origo som inte är ändligt, eller nivåer som
    // inte är hela trianglar eller inte står i växande avstånd. Utan lägen
    // blir det ingen mesh och ingen logg — tomt är inget fel.
    explicit Mesh(MeshDesc desc);

    // Världspunkter → mesh: origot i lådans mitt, lägena relativt det.
    // Indexen som i `MeshDesc` (tomt = icke-indexerat).
    static Mesh fromWorld(std::span<const Point3>    positions,
                          std::vector<std::uint32_t> indices = {});
    // LITERALFORMERNA: icke-indexerat respektive indexerat, varje läge i
    // färgen `color`. `indexed` med tomma index är en tom mesh.
    static Mesh triangles(std::vector<Point3> positions, gfx::Color color);
    static Mesh indexed(std::vector<Point3>        positions,
                        std::vector<std::uint32_t> indices, gfx::Color color);

    bool        valid() const { return d_ != nullptr; }
    std::size_t vertexCount() const;
    std::size_t triangleCount() const;
    // Lådan kring lägena i VÄRLDEN (origot inräknat, före formens placering).
    Box3        bounds() const;

    // Skriver lägena från läge `first` och antecknar dem: nästa ritning
    // laddar upp BARA dem, lådan räknas om och plockningens BVH byggs om vid
    // nästa plockning. Utanför meshen, tomt eller ett läge som inte är
    // ändligt ⇒ logg och ingenting skrivs.
    void setPositions(std::size_t first, std::span<const Float3> positions);
    // Skriver färgerna från läge `first` (rak alfa) — som `setPositions`, och
    // bara i en mesh som har en färgström.
    void setColors(std::size_t first, std::span<const gfx::Color> colors);

    // Bygger plockningens BVH NU i stället för vid första plockningen — i en
    // arbetartråd för en stor mesh (miljontals trianglar tar en märkbar
    // stund). Byggs en gång; samtidiga anrop väntar på samma bygge.
    void preparePicking() const;

private:
    std::shared_ptr<detail::MeshData> d_;
    friend struct detail::MeshAccess;   // ramverkets ritning och plockning
};

// Ett punktmoln delas i CHUNKAR om högst så många punkter, var och en med
// eget origo — precisionen och gallringen.
inline constexpr std::size_t kPointChunkSize = 65536;

// glaze::PointCloud — PUNKTER UR DATA: ett handtag som `Mesh`, med samma
// trådregler (konstruktorerna i valfri tråd). Varje punkt har ett läge och
// valfritt en färg och en skalär (en höjd över mark som en färgramp läser).
//
// CHUNKARNA: molnet delas RUMSLIGT — längs den längsta axeln, mitt i, tills
// varje del har högst `kPointChunkSize` punkter — och varje del får origot i
// sin låda. Ett moln på miljontals punkter gallras alltså per del oavsett i
// vilken ordning punkterna kom, och en SWEREF-punkt avrundas bara efter sin
// dels storlek. Ritytan svarar ändå med punktens plats i INDATAN.
class PointCloud {
public:
    // Inget moln: `valid()` falskt.
    PointCloud() = default;

    // Lägena relativt `origin`. Färgerna och skalärerna är TOMMA eller en per
    // punkt. Fel längd, ett läge eller origo som inte är ändligt ⇒ inget moln
    // + logg; inga punkter ⇒ inget moln och ingen logg.
    PointCloud(std::vector<Float3> local, Point3 origin,
               std::vector<gfx::Color> colors = {},
               std::vector<float>      scalars = {});

    // Världspunkter → moln.
    static PointCloud fromWorld(std::span<const Point3>  points,
                                std::vector<gfx::Color> colors = {},
                                std::vector<float>      scalars = {});

    bool        valid() const { return d_ != nullptr; }
    std::size_t pointCount() const;
    Box3        bounds() const;

private:
    std::shared_ptr<detail::PointCloudData> d_;
    friend struct detail::MeshAccess;
};

} // namespace glaze
