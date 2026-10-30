#pragma once

// Sidopaketet OpenXR:s matematik: OpenXR:s spårningsrum → Glaze-världen, kameravinklar
// och en symmetrisk projektion som täcker ett ögas asymmetriska synfält.
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

#include <cstdint>
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

// Runtimens bildmått för ett öga (XrViewConfigurationView).
struct ViewSize {
    std::uint32_t recommendedWidth  = 0;
    std::uint32_t recommendedHeight = 0;
    std::uint32_t maxWidth          = 0;
    std::uint32_t maxHeight         = 0;
};

// Den bild som ritas för ett öga: måtten i px, kamerans VERTIKALA synfält i
// grader (Camera3D::fov), och det synfält bilden faktiskt täcker — det som
// lämnas till OpenXR i projektionslagret.
struct SymmetricView {
    std::uint32_t width              = 0;
    std::uint32_t height             = 0;
    float         verticalFovDegrees = 0.0f;
    Fov           submittedFov;
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

// En symmetrisk bild som täcker hela `recommended`: måtten är
// `resolutionScale` × de rekommenderade, klampade till runtimens största,
// och synfältet väljs efter bildens faktiska aspekt. Tomt vid ett omöjligt
// synfält, nollstorlek eller en skala ≤ 0.
std::optional<SymmetricView> makeSymmetricView(const Fov& recommended,
                                               const ViewSize& size,
                                               double resolutionScale) noexcept;

} // namespace glaze::openxr
