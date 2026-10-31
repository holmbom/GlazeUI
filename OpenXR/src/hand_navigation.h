#pragma once

// FÖRFLYTTNING MED HÄNDERNA (`Locomotion::handNavigation`) — ren logik som gör
// den spårade handens nyp till samma spakvärden som kontrollens spakar ger
// `Mover`. Ingen session och inget Canvas, så delen provas utan headset
// (tests/hand_navigation_gate).
//
// MoveTurn   vänster hands nyp håller en virtuell spak: handens förskjutning
//            från nypets början, i en referensram som låses då (huvudets
//            vågräta blick och dess höger), delas med `rangeMetres` och blir
//            gång framåt/åt sidan; höger hands nyp ger vridningen (sidled).
// Teleport   höger hands nyp är teleportspaken framåt; släppet landar.
// Gränssnittet går först: ett nyp som börjar medan handen är tagen
// (`Session::claimHand` — den pekar på en panel) blir ett tryck där och ingen
// gest. En hand är reserverad (`Controller::navigationReserved`) bara medan
// dess gest pågår: gränssnittet och appen låter den då vara.
//
// En gest börjar bara ur ett neutralt läge (nypet släppt): efter ett byte av
// läge, ett byte av källa eller förlorad spårning krävs ett släpp först. Förlorad
// spårning, förlorat fokus eller ett byte av källa avbryter en pågående gest
// utan landning.

#include <array>
#include <cstdint>

#include <glaze_openxr/session.h>

namespace glaze::openxr::detail {

struct NavigationHand {
    Controller::Source source = Controller::Source::None;   // vägen som matar handen
    std::uint64_t      epoch  = 0;                          // Controller::inputEpoch
    bool               tracked  = false;   // nypets punkt är giltig
    bool               pinching = false;   // nypet är nedtryckt (Trigger)
    bool               claimed  = false;   // gränssnittet tog handen förra bildrutan
    Vec3               pinchPoint;         // nypets punkt i spårningsrummet
};

struct NavigationInput {
    HandNavigation mode        = HandNavigation::None;
    double         rangeMetres = 0.12;   // förskjutningen som ger fullt utslag
    double         deadzone    = 0.2;    // radiell, som kontrollernas spakar
    bool           focused     = false;
    bool           headValid   = false;
    TrackingPose   head;                 // låser referensramen när nypet börjar
    std::array<NavigationHand, 2> hands; // [0] vänster, [1] höger
};

struct NavigationOutput {
    std::array<bool, 2>   reserved{};   // handens gest pågår (Controller::navigationReserved)
    std::array<bool, 2>   drives{};     // handens spakvärden gäller i stället för kontrollens
    std::array<bool, 2>   aborted{};    // gesten bröts utan släpp (spårning, fokus, källa, läge): ingen landning
    std::array<double, 2> stickX{};     // −1 … 1
    std::array<double, 2> stickY{};
};

class HandNavigator {
public:
    NavigationOutput update(const NavigationInput& input);
    // Alla gester avbryts och neutralt läge krävs.
    void reset() noexcept;

private:
    struct Gesture {
        bool          armed  = false;   // nypet har setts släppt sedan senaste avbrott
        bool          active = false;
        std::uint64_t epoch  = 0;
        bool          epochKnown = false;
        Vec3          start;     // nypets början
        Vec3          forward;   // referensramen: framåt och höger, vågräta
        Vec3          right;
    };

    HandNavigation mode_ = HandNavigation::None;
    std::array<Gesture, 2> gestures_{};
};

} // namespace glaze::openxr::detail
