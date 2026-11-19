#pragma once
// DJUPORDNINGEN — INTERN. Två ytor i samma plan har samma djup, och djuptestet
// avgör då vinnaren pixel för pixel ur avrundningen. I 3D-målet får därför
// varje World-lager och varje föremål i det en RANG, och rangen blir en
// djupbias mot ögat (`ranked`): högre rang vinner där ytorna ligger i samma
// plan, och det som ligger framför vinner som förut.
//
//   LAGREN   ett senare World-lager har högre rang än allt i ett tidigare —
//            lagrets bas är summan av de tidigare lagrens högsta rang + 1
//            (scene.cpp `prepareLayers`, `LayerFrame::depthBase`). Appens egen
//            bias (`RenderState::depthBias`) läggs ovanpå.
//   FÖREMÅLEN i ett lager (mesh, yta i rymden, instanser): ett föremål vars
//            låda ligger an mot ett tidigare föremåls låda med en yta — inte
//            bara en kant eller ett hörn — får en rang över det. Två föremål
//            som ligger an mot varandra har alltid OLIKA rang, så vinnaren är
//            densamma från varje håll. Det senare vinner så länge rangen ryms
//            under `kDepthRankMax`; i en kedja längre än så tar föremålet den
//            högsta lediga rangen bland sina grannar, och då kan ett tidigare
//            vinna över ett senare.
//   ÖVRIGT   i lagret (linjer, ytor på z = 0, text, lagrets målare) får
//            rangen över de föremål som står före det i lagret.
//
// Lådorna är en ÖVERMÄNGD av de ytor som faktiskt ligger i samma plan: två
// föremål kan få olika rang utan att dela ett plan. Det kostar bara biasen.
//
// ORDNINGEN ÄR EN CACHE i lagret (`LayerData::depth`), läst och bytt under
// lagrets lås (`nodeLock`). Den räknas om när lagrets föremål ändrats —
// signaturen över deras platser, former och data — och räkningen går över
// ALLA lagrets föremål. Ett värde som lämnats ut ändras aldrig.

#include <cstdint>
#include <memory>
#include <vector>

namespace glaze::detail {

struct LayerData;
struct ShapeSlot;

// Den högsta rangen i ett lager. Föremålen tar 0 … `kDepthRankMax` − 1, så
// att det som ritas efter dem alltid har en rang över.
inline constexpr int kDepthRankMax = 15;

// Biasen per rang: djupformatets minsta steg och andelen av primitivens
// största djuplutning (gfx/scene.h `RenderState`).
inline constexpr float kDepthRankBias  = 2.0f;
inline constexpr float kDepthRankSlope = 0.25f;

struct DepthOrder {
    // Rangen per föremål: [körningens plats i `LayerData::runs`][platsen i
    // `RunData::objects`]. Tom för en körning utan föremål.
    std::vector<std::vector<std::uint8_t>> objects;
    // Rangen för en körning som inte är föremål, per körning.
    std::vector<std::uint8_t> runs;
    int top = 0;   // lagrets högsta rang
};

// Lagrets ordning — räknad om bara när föremålen ändrats.
std::shared_ptr<const DepthOrder> depthOrder(const LayerData& layer);

// Rangen för formplatsens föremål; 0 för det som inte är ett föremål.
int slotRank(const LayerData& layer, const DepthOrder& order, const ShapeSlot& slot);

// Renderläget med rangens bias — `glaze::RenderState` och fasadens.
template <class State> State ranked(State s, int rank)
{
    s.depthBias -= kDepthRankBias * static_cast<float>(rank);
    s.slopeBias -= kDepthRankSlope * static_cast<float>(rank);
    return s;
}

// Gångerna en ordning räknats om i processen — grindarnas räknare.
std::uint64_t depthOrderBuilds();

} // namespace glaze::detail
