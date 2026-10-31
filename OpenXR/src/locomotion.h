#pragma once

// Den inbyggda förflyttningen (`Locomotion`): gå och flyga med vänster spak,
// vrida med höger, teleportera med en båge och greppa världen med en eller
// två händer. Flyttar förankringen — spårningsrummets plats i världen — så
// att huvudet och händerna i rummet förblir där de är. Ren logik utan
// OpenXR-typer och utan Canvas: strålarna mot scenen kommer som en funktion,
// så den går att prova utan headset.

#include <array>
#include <functional>
#include <optional>
#include <vector>

#include <glaze_openxr/session.h>

namespace glaze::openxr::detail {

// En hands läge och reglage, i spårningsrummet.
struct LocomotionHand {
    bool         gripValid = false;
    bool         aimValid  = false;
    TrackingPose grip;
    TrackingPose aim;
    double       stickX  = 0.0;   // efter dödzonen
    double       stickY  = 0.0;
    bool         squeeze = false; // greppknappen är nedtryckt
};

struct LocomotionInput {
    double       deltaSeconds = 0.0;
    bool         focused      = false;
    bool         headValid    = false;
    TrackingPose head;
    // Golvets höjd (y) i spårningsrummet; teleporteringen sätter fötterna dit.
    double       floorHeight  = 0.0;
    // Den raka teleportstrålens längd i meter i rummet (skalas med
    // förankringen); 0 eller mindre ger 20 m.
    double       rayLengthMetres = 20.0;
    // Teleporteringens landning lämnas som en begäran (`Mover::takeLanding`)
    // i stället för att utföras direkt — komforten tonar då ut först.
    bool         deferLanding = false;
    std::array<LocomotionHand, 2> hands;   // [0] vänster, [1] höger
};

// Var användaren ska stå efter en teleportering: fötterna i världen och, om
// den anges, kompassriktningen att vända mot.
struct Landing {
    Vec3                  feet;
    std::optional<double> yawDegrees;
};

// Träffarna längs en sträcka ur `ray.origin` i `ray.direction` (enhetsvektor),
// högst `maxDistance` långt, närmast först.
using SegmentCast = std::function<std::vector<glaze::HitResult>(const glaze::Ray3& ray, double maxDistance)>;

// RESERVMARKEN: träffen där strålen når planet z = `groundZ` — lager och index
// −1, en yta med normalen (0, 0, 1), avståndet längs strålen. Tomt när
// strålen inte pekar mot planet, är parallell med det eller når det längre
// bort än `maxDistance` (> 0). `ray.direction` behöver inte vara enhetlig.
std::optional<glaze::HitResult> groundHit(const glaze::Ray3& ray, double maxDistance, double groundZ);

// `hits` som de är, utom när de är tomma och reservmarken `groundZ` är satt:
// då är marken den enda träffen, om strålen når den.
std::vector<glaze::HitResult> withGround(std::vector<glaze::HitResult> hits, const glaze::Ray3& ray,
                                         double maxDistance, const std::optional<double>& groundZ);

// Förankringen vriden kring huvudet `head` så att blickens vågräta riktning
// pekar mot kompassriktningen `yawDegrees`. Huvudet står kvar i världen och
// skalan behålls. Tomt vid ogiltiga värden.
std::optional<Anchor> faceYaw(const Anchor& anchor, const TrackingPose& head, double yawDegrees);

// Landningen: flyttar `anchor` så att FÖTTERNA — huvudet `head` rakt ned på
// golvet `floorHeight` (y i rummet), med skalan — hamnar på `feet` i världen.
// Med `yawDegrees` vrids förankringen först kring huvudet (`faceYaw`). false
// och `anchor` orörd vid ogiltiga värden.
bool placeFeet(Anchor& anchor, const TrackingPose& head, double floorHeight, const Vec3& feet,
               const std::optional<double>& yawDegrees);

class Mover {
public:
    // Flyttar `anchor` en bildruta enligt `settings`. Utan fokus eller huvud
    // sker ingenting och pågående gester avbryts. Teleportvägens punkter
    // (bågen eller den raka strålen) och landningsplatsen hålls i
    // `arc()`/`arcHit()` tills nästa anrop. En teleportering som släpps
    // landar direkt (`landed()`), eller lämnar en begäran (`takeLanding`) när
    // `LocomotionInput::deferLanding` är satt. En teleportering vars sikte
    // tappas avbryts utan landning.
    void update(const LocomotionInput& input, const Locomotion& settings, Anchor& anchor,
                const SegmentCast& cast);

    // Förankringen byttes utanför: en pågående gest börjar om från den nya.
    void cancel() noexcept;
    // En pågående teleportering avbryts utan landning (spaken togs av appen).
    void cancelTeleport() noexcept;

    // Landningsbegäran från senaste `update`, en gång; tom annars.
    std::optional<Landing> takeLanding() noexcept;
    // Senaste `update` flyttade förankringen med en direkt landning.
    bool landed() const noexcept { return landed_; }
    // Senaste `update` tog ett steg i den stegvisa vridningen.
    bool snapped() const noexcept { return snapped_; }

    const std::vector<Vec3>&                arc() const noexcept { return arc_; }
    const std::optional<glaze::HitResult>&  arcHit() const noexcept { return arcHit_; }
    bool grabbing() const noexcept { return grab_.mode != GrabMode::None; }

private:
    enum class GrabMode { None, One, Two };
    struct Grab {
        GrabMode mode = GrabMode::None;
        int      hand = 0;            // GrabMode::One: handen som håller
        Anchor   anchor;              // förankringen när greppet började
        std::array<Pose, 2> rigAtGrab;   // händernas lägen i rummet (utan förankring) vid greppet
        Vec3     worldMid;            // mittpunkten mellan händerna i världen vid greppet
    };

    bool updateGrab(const LocomotionInput& input, const Locomotion& settings, Anchor& anchor);
    void updateTurn(const LocomotionInput& input, const Locomotion& settings, Anchor& anchor);
    void updateMove(const LocomotionInput& input, const Locomotion& settings, Anchor& anchor);
    void updateTeleport(const LocomotionInput& input, const Locomotion& settings, Anchor& anchor,
                        const SegmentCast& cast);
    void reset() noexcept;

    Grab   grab_;
    bool   snapArmed_ = true;
    bool   teleporting_ = false;
    std::optional<Vec3> landing_;   // där bågen landade senast, för släppet
    std::optional<Landing> request_;   // begärd landning som ännu inte tagits
    bool   landed_  = false;
    bool   snapped_ = false;
    std::vector<Vec3>               arc_;
    std::optional<glaze::HitResult> arcHit_;
};

} // namespace glaze::openxr::detail
