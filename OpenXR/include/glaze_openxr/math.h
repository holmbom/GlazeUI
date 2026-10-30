#pragma once

// Sidopaketet OpenXR:s matematik: OpenXR:s spårningsrum → Glaze-världen och
// kameravinklar.
// Ren C++ utan beroenden — provas utan fönster, GPU eller runtime.
//
// KOORDINATERNA:
//   OpenXR (TrackingPose): högerhänt, x höger, y UPP, −z framåt, meter.
//   Glaze  (WorldPose, Anchor): högerhänt, x öster, y norr, z UPP, meter
//          (en världsenhet = en meter), i double.
//   Synfältets vinklar (Fov) i RADIANER, som OpenXR:s XrFovf: left och down
//   negativa, right och up positiva för ett vanligt öga.
//   Kameravinklar och förankringens yaw i GRADER; yaw är kompassriktningen
//   (0 = norr, 90 = öster, medurs), som Camera3D::azimuth.

#include <optional>
#include <vector>

namespace glaze::openxr {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// Enhetskvaternion (x, y, z, w) — OpenXR:s XrQuaternionf i double.
struct Quaternion {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
};

// En pose i OpenXR:s spårningsrum.
struct TrackingPose {
    Vec3       position;
    Quaternion orientation;
};

// Var spårningsrummets origo står i Glaze-världen och hur det är vridet:
// rummets −z (framåt) pekar mot kompassriktningen `yawDegrees`. `scale` är
// världsenheter per meter i rummet: 1 = naturlig storlek, 10 = varje steg i
// rummet går tio enheter och ögonavståndet blir tio gånger större — världen
// ses som en modell; 0,1 = världen ses tio gånger förstorad.
struct Anchor {
    Vec3   origin;
    double yawDegrees = 0.0;
    double scale      = 1.0;
};

// En pose i Glaze-världen: positionen och de tre ortonormala axlarna.
struct WorldPose {
    Vec3 position;
    Vec3 right;
    Vec3 up;
    Vec3 forward;
};

// Kamerans vinklar för Camera3D::lookFrom + Camera3D::roll: yaw som
// kompassriktning, pitch UPPÅT från horisonten, roll med Camera3D:s tecken
// (positivt vrider kameran medurs, sett bakifrån längs blicken).
struct CameraAngles {
    double yawDegrees   = 0.0;
    double pitchDegrees = 0.0;
    double rollDegrees  = 0.0;
};

// Ett ögas synfält som vinklar från blicken (radianer, som XrFovf).
struct Fov {
    double left  = 0.0;
    double right = 0.0;
    double up    = 0.0;
    double down  = 0.0;
};

// Förankringen som ställer huvudet `initialHead` i ögat `cameraEye` och
// vänder huvudets horisontella blick mot `cameraYawDegrees`. Tomt vid
// icke-ändliga värden eller en ogiltig kvaternion.
std::optional<Anchor> makeAnchor(const TrackingPose& initialHead,
                                 Vec3 cameraEye,
                                 double cameraYawDegrees) noexcept;

// `pose` i Glaze-världen genom förankringen. Tomt vid icke-ändliga värden
// eller en ogiltig kvaternion.
std::optional<WorldPose> toWorldPose(const Anchor& anchor,
                                     const TrackingPose& pose) noexcept;

// Kameravinklarna för `pose`. Rakt upp eller ned saknar blicken
// kompassriktning; då används `previousYawDegrees` och rollen räknas mot
// den. Tomt vid icke-ändliga värden eller degenererade axlar.
std::optional<CameraAngles> cameraAngles(const WorldPose& pose,
                                         double previousYawDegrees) noexcept;

// ------------------------------------------------------- STELA TRANSFORMER
// En pose i Glaze-världen som läge och orientering. Orienteringens lokala
// axlar är Glazes: x höger, y framåt, z upp — så en identitetspose tittar
// norrut med z upp. Alla funktioner nedan är noexcept; kvaternioner
// normeras, och en nollkvaternion behandlas som identiteten.
struct Pose {
    Vec3       position;
    Quaternion orientation;
};

Pose      toPose(const WorldPose& pose) noexcept;        // ur axlarna
WorldPose toWorldPose(const Pose& pose) noexcept;        // till axlarna
Pose      compose(const Pose& parent, const Pose& child) noexcept;   // parent · child
Pose      inverse(const Pose& pose) noexcept;
Vec3      transformPoint(const Pose& pose, Vec3 point) noexcept;
Vec3      transformDirection(const Pose& pose, Vec3 direction) noexcept;
// Mjuk övergång: läget linjärt, orienteringen sfäriskt (kortaste vägen).
Pose      interpolate(const Pose& from, const Pose& to, double t) noexcept;

// Vinklarna i `glaze::Placement`s konvention — yaw medurs ovanifrån från
// norr, pitch och roll som Placement — och tillbaka.
struct YawPitchRoll {
    double yawDegrees   = 0.0;
    double pitchDegrees = 0.0;
    double rollDegrees  = 0.0;
};
Quaternion   fromYawPitchRoll(const YawPitchRoll& angles) noexcept;
YawPitchRoll toYawPitchRoll(const Quaternion& orientation) noexcept;

// --------------------------------------------------------------- GREPPEN
// Ett föremål som hålls i en hand följer handen stelt: där handen var vid
// greppet (`handAtGrab`) och där föremålet var (`objectAtGrab`) ger var
// föremålet är när handen står i `handNow`.
Pose followGrab(const Pose& handAtGrab, const Pose& objectAtGrab,
                const Pose& handNow) noexcept;

// Två händer: flytt efter mittpunkten, vridning efter linjen mellan händerna
// och skala efter avståndet mellan dem, räknat från greppets läge.
enum class TwoHandRotation { Full, YawOnly, None };
struct TwoHandResult {
    Pose   pose;          // föremålets nya pose
    double scale = 1.0;   // föremålets nya skala (`scaleAtGrab` × avståndskvoten)
};
TwoHandResult twoHandManipulate(const Pose& leftAtGrab, const Pose& rightAtGrab,
                                const Pose& objectAtGrab, double scaleAtGrab,
                                const Pose& leftNow, const Pose& rightNow,
                                TwoHandRotation rotation) noexcept;

// ----------------------------------------------------- YTOR OCH SNÄPP
// En ram på en yta: z längs `normal`, y så nära `forward` som ytan tillåter
// (projicerad på ytan; parallell med normalen ⇒ världens norr, sedan öster).
// Tomt för en nollnormal eller icke-ändliga tal.
std::optional<Pose> surfaceFrame(Vec3 point, Vec3 normal, Vec3 forward) noexcept;
// Närmaste punkt på ett rutnät med steget `step` (> 0) kring `origin`.
Vec3   snapToGrid(Vec3 point, double step, Vec3 origin = {}) noexcept;
// Närmaste multipel av `stepDegrees` (> 0).
double snapAngle(double degrees, double stepDegrees) noexcept;

// En KASTBANA: punkter från `origin` längs `direction` med farten `speed`
// (enheter/s) och tyngden `gravity` (enheter/s², nedåt längs −z), var
// `stepSeconds`, till `maxSeconds`. Första punkten är `origin`. För en
// teleportering eller ett kast; träffen mot scenen söks segment för segment.
std::vector<Vec3> ballisticArc(Vec3 origin, Vec3 direction, double speed,
                               double gravity, double stepSeconds,
                               double maxSeconds);

} // namespace glaze::openxr
