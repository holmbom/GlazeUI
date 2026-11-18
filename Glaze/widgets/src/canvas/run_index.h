#pragma once
// KÖRNINGENS RUMSLIGA INDEX — INTERN. Ritningen och förberedelsen frågar en
// körning vilka av dess chunkar och föremål som KAN synas i vyn, i stället för
// att pröva vart och ett: svaret kommer ur ett rumsligt träd (spatial_index.h)
// över deras lådor, så kostnaden följer det som syns.
//
// SVARET ÄR KANDIDATER — en övermängd — som index i `RunData::chunks`
// respektive `RunData::objects`, i VÄXANDE ordning. Den som frågar prövar
// varje kandidat med ritningens EXAKTA gallring (`chunkVisible`,
// `objectInView`) och ritar dem i den ordning de kom: rit-anropen och deras
// ordning är desamma som om varje chunk och föremål hade prövats.
//
// NODERNAS PROV ÄR KONSERVATIVT: en låda som innehåller en synlig låda är själv
// synlig i gallringens mening (ett hörn bakom kameran ritar alltid — halvrymden
// är konvex; annars innehåller dess avtryck det mindre avtrycket; och "alla hörn
// utanför samma plan" gäller också varje del av lådan). Linjernas marginal i px
// eller mm är körningens STÖRSTA.
//
// INDEXET ÄR EN CACHE i körningen (`RunData::index`), byggd när den första
// frågan kommer och giltig för lagrets `shapesVersion` — varje ändring av
// formerna stegar den — och, för föremålen, `meshGeneration()`: en mesh som
// flyttat sina lägen har en annan låda. Föremålens lådor räknas ur formen
// (`objectBox(form)`), inte ur formplatsens cache. En KORT körning (under
// `kRunIndexMin`) har inget träd: alla dess chunkar och föremål är kandidater.
// UI-tråden.

#include <cstdint>
#include <vector>

#include "spatial_index.h"

namespace glaze::detail {

struct RunData;
struct PaintContext;
class Projection;

struct RunIndex {
    // Chunkarna: trädet, dess nyckel och körningens största marginal.
    SpatialIndex  chunks;
    std::uint64_t chunkShapes = 0;
    bool          chunksBuilt = false;
    float         chunkMargin = 0.0f;   // körningens enhet

    // Föremålen: trädet, de utan världslåda (alltid kandidater), z-spannet
    // över lådorna och nyckeln.
    SpatialIndex               objects;
    std::vector<std::uint32_t> always;
    double                     zMin = 0.0, zMax = 0.0;
    std::uint64_t              objectShapes = 0, objectMeshes = 0;
    bool                       objectsBuilt = false;
    std::uint64_t              builds = 0;   // gångerna föremålens träd förts ikapp

    // INKREMENTELLT. Varje plats i körningens listor har en SIGNATUR (platsens
    // id, formens och datans version; 0 = ingen post), och ett inaktuellt
    // index förs ikapp genom att bara de platser vars signatur ändrats läggs
    // in på nytt. Trädet redigeras inte, så den gamla posten ligger kvar —
    // svaret är en övermängd, och frågan tar bort dubbletter och döda. När de
    // kvarliggande posterna blivit fler än `kIndexStaleShare` av de levande
    // byggs trädet om från början. En kopia av körningen (en ögonblicksbild)
    // ÄRVER indexet och för det ikapp på samma sätt.
    std::vector<std::uint64_t> objectSig;      // per plats i `RunData::objects`
    std::vector<std::uint8_t>  objectAlways;   // platsen står i `always`
    std::size_t                objectStale = 0;
    bool                       objectTree  = false;   // byggt med träd
    std::vector<std::uint64_t> chunkSig;       // per plats i `RunData::chunks`
    std::size_t                chunkStale = 0;
    // Grindarnas räknare: poster som lagts in, och byggen från början.
    std::uint64_t inserted = 0, rebuilds = 0;
};

// Andelen kvarliggande poster som får ett inkrementellt index att byggas om.
inline constexpr double kIndexStaleShare = 0.25;

// En körning med färre chunkar respektive föremål har inget träd.
inline constexpr std::size_t kRunIndexMin = 64;

// Chunkarna som kan synas i ritningens vy `f` — de med geometri och låda.
// `slackPx` är gallringens marginal i fysiska px (`chunkVisible`s).
void chunkCandidates(const PaintContext& f, const RunData& run,
                     std::uint64_t shapesVersion, float slackPx,
                     std::vector<std::uint32_t>& out);

// De levande föremålen som kan synas genom `proj` — de utan världslåda (en
// skärmkonstant mesh, ett föremål utan data) alltid. `slackPx` som ovan.
void objectCandidates(const RunData& run, std::uint64_t shapesVersion,
                      const Projection& proj, double slackPx,
                      std::vector<std::uint32_t>& out);

// Z-spannet över körningens levande föremål, med 0 inräknat.
void objectZSpan(const RunData& run, std::uint64_t shapesVersion, double& zMin,
                 double& zMax);

// Gångerna körningens föremålsträd förts ikapp — grindarnas räknare (0 utan
// träd) — och posterna som lagts in och byggena från början.
std::uint64_t objectIndexBuilds(const RunData& run);
std::uint64_t objectIndexInserted(const RunData& run);
std::uint64_t objectIndexRebuilds(const RunData& run);

} // namespace glaze::detail
