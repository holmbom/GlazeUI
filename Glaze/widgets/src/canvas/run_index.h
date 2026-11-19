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
#include <optional>
#include <vector>

#include "glaze/canvas/geometry.h"
#include "spatial_index.h"

namespace glaze {
class Shape;
}

namespace glaze::detail {

struct RunData;
struct ShapeEntry;
struct PaintContext;
class Projection;

// TABELLENS RAD för ett föremål: det gallringen och bildtakten läser, räknat
// en gång när platsens signatur ändras — lådan formen har (också en
// skärmkonstant meshs, för z-spannet), om den gallras på lådan eller alltid
// är kandidat, och kostnaden (scene3d.h `objectPaceCost`).
struct ObjectInfo {
    enum class State : std::uint8_t { None, Boxed, Always };
    Box3  box{};
    float cost   = 0.0f;
    State state  = State::None;
    bool  hasBox = false;
};

// Ett föremål som syns (`objectsInView`): platsen, avståndet från ögat till
// lådan, lådans diagonal och kostnaden. Utan låda är avståndet och
// diagonalen 0.
struct ObjectSeen {
    std::uint32_t index    = 0;
    double        distance = 0.0;
    double        diagonal = 0.0;
    float         cost     = 0.0f;
    bool          hasBox   = false;
};

struct RunIndex {
    // FÖREMÅLENS TABELL, en rad per plats i `RunData::objects` — förd ikapp
    // på plats, aldrig ombyggd för att föremål byts. En körning under
    // `kRunTreeMin` gallras rakt igenom tabellen; en längre har trädet ovanpå.
    std::vector<ObjectInfo> info;
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
// Föremålen får ett träd först vid så många: under det är en rak genomgång
// av tabellen snabbare än trädet, och den behöver aldrig byggas om.
inline constexpr std::size_t kRunTreeMin = 4096;

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

// De levande föremålen som SYNS genom `proj` — gallrade exakt mot tabellens
// lådor, i växande ordning — med avståndet från `eye`, diagonalen och
// kostnaden. Ingenting slås upp i formerna.
void objectsInView(const RunData& run, std::uint64_t shapesVersion,
                   const Projection& proj, double slackPx, const Point3& eye,
                   std::vector<ObjectSeen>& out);

// Platserna vars signatur i tabellen skiljer sig från `seen` (en per plats;
// en plats bortom dess slut räknas som 0) — bland `only`, eller alla — med
// tabellens signatur (0 = död) och om platsen syns genom `proj`.
struct ObjectUnseen {
    std::uint32_t index  = 0;
    std::uint64_t sig    = 0;
    bool          inView = false;
};
void objectsUnseen(const RunData& run, std::uint64_t shapesVersion,
                   const Projection& proj, double slackPx,
                   const std::vector<std::uint64_t>& seen,
                   const std::vector<std::uint32_t>* only,
                   std::vector<ObjectUnseen>& out);

// För körningens index ikapp till `shapesVersion` — före en ögonblicksbild,
// som ärver det: renderingstråden får då ett index som redan stämmer.
void syncObjectIndex(const RunData& run, std::uint64_t shapesVersion);

// Föremålets låda i världen, eller ingen: en skärmkonstant mesh (lådan är
// origot) och ett föremål utan data har ingen — ritningens `objectInView`
// gör samma undantag.
std::optional<Box3> objectWorldBox(const Shape& s);

// Föremålets SIGNATUR: platsens id, formens version och datans (en mesh som
// flyttat sina lägen har en annan låda). Aldrig 0.
std::uint64_t objectSignature(const ShapeEntry& e);

// Z-spannet över körningens levande föremål, med 0 inräknat.
void objectZSpan(const RunData& run, std::uint64_t shapesVersion, double& zMin,
                 double& zMax);

// Gångerna körningens föremålsträd förts ikapp — grindarnas räknare (0 utan
// träd) — och posterna som lagts in och byggena från början.
std::uint64_t objectIndexBuilds(const RunData& run);
std::uint64_t objectIndexInserted(const RunData& run);
std::uint64_t objectIndexRebuilds(const RunData& run);

} // namespace glaze::detail
