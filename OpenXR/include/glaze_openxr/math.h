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
// rummets −z (framåt) pekar mot kompassriktningen `yawDegrees`.
struct Anchor {
    Vec3   origin;
    double yawDegrees = 0.0;
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

} // namespace glaze::openxr
