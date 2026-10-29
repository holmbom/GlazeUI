#pragma once
// INTERN — INSTANSERNAS CELLER. Det
// `Shape::instances` bygger EN gång och ritytan sedan ritar ur: instanserna
// sorterade i RUMSLIGA CELLER i Morton-ordning, deras rader (affin 3 × 4 +
// färg, `MeshInstance` — programs3d.h) i cellordning, och för varje cell
// instanserna den rymmer och deras låda i världen.
//
// PRECISIONEN (RTC): världen delas i BLOCK om högst `kChunkCell`
// världsenheter (scene.h — samma gräns som chunkarnas), ett origo i double
// per block, och en instans förflyttning lagras relativt sitt blocks origo i
// float. Cellerna ligger helt i sitt block, så angränsande celler i samma
// block kan ritas i ETT anrop med blockets origo — en kameraändring laddar
// inte upp något och ritar högst ett anrop per synlig cell.
//
// CELLSTORLEKEN väljs ur instansernas utbredning så att en cell i snitt har
// ungefär `kInstancesPerCell` instanser: sidan s = (produkten av de
// utsträckta axlarna / antalet celler)^(1/axlarna), och en axel vars
// utsträckning är kortare än s räknas inte (träd på en karta blir celler i
// planet). s ≤ blocket. Ordningen inom ett block är cellkoordinatens
// Morton-kod (21 bitar per axel), blocken i koordinatordning, lika lägen i
// indatans ordning — deterministiskt.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "glaze/canvas/geometry.h"
#include "glaze/canvas/style3d.h"
#include "programs3d.h"   // MeshInstance

namespace glaze::detail {

inline constexpr std::size_t kInstancesPerCell = 64;

struct InstanceCell {
    std::uint32_t first = 0;   // första raden i `rows`
    std::uint32_t count = 0;
    std::uint32_t block = 0;   // `blocks[block]` är radernas origo
    Box3          box{};       // instansernas lådor (prototypen placerad) i världen
    Box3          anchors{};   // instansernas lägen — en skärmkonstant prototyp
};

struct InstanceSet {
    std::uint64_t              id = 0;   // residensens nyckel, återanvänds aldrig
    std::vector<MeshInstance>  rows;     // i cellordning, relativa sitt blocks origo
    std::vector<Point3>        blocks;   // blockens origo i världen
    std::vector<InstanceCell>  cells;    // block för block, Morton inom blocket
    // rows[i] är instansen order[i] i indatan (plockningen svarar med den).
    std::vector<std::uint32_t> order;
    Box3                       bounds{};   // alla instansers lådor
    double                     cellSize = 0.0;
    // Den största skalan någon instans har längs en axel — en skärmkonstant
    // prototyps räckvidd i px är storleken × prototypens radie × den.
    double                     maxScale = 0.0;
};

// Cellerna för prototypens låda i dess LOKALA ram (instansens ram — där
// prototypens placering redan är pålagd) och instanserna. En instans som
// inte är ändlig (läge, vinklar, skala) hoppas över och det loggas; utan
// instanser blir mängden tom.
std::shared_ptr<const InstanceSet> buildInstanceSet(const Box3& prototypeBox,
                                                    std::span<const Instance> inst);

} // namespace glaze::detail
