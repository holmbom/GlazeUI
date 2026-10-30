#include <glaze_openxr/math.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace glaze::openxr {

namespace {

constexpr double kPi  = 3.14159265358979323846;
constexpr double kDeg = 180.0 / kPi;
constexpr double kRad = kPi / 180.0;

// Hur långt från längden 1 en kvaternion får vara och ändå gälla som en
// enhetskvaternion (runtimens float-kvaternioner ligger långt innanför).
constexpr double kQuaternionTolerance = 1e-3;

// Under den här längden på en riktnings vågräta del saknar den kompassriktning.
constexpr double kHorizontalEpsilon = 1e-9;

bool finite(double v) noexcept { return std::isfinite(v); }
bool finite(const Vec3& v) noexcept {
    return finite(v.x) && finite(v.y) && finite(v.z);
}

// Kvaternionen som enhetskvaternion, eller tomt vid icke-ändliga värden eller
// en längd som inte är 1.
std::optional<Quaternion> unitQuaternion(const Quaternion& q) noexcept {
    if (!finite(q.x) || !finite(q.y) || !finite(q.z) || !finite(q.w)) {
        return std::nullopt;
    }
    const double n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!finite(n) || std::fabs(n - 1.0) > kQuaternionTolerance) {
        return std::nullopt;
    }
    return Quaternion{q.x / n, q.y / n, q.z / n, q.w / n};
}

Vec3 add(const Vec3& a, const Vec3& b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(const Vec3& a, const Vec3& b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale(const Vec3& a, double s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
double dot(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(const Vec3& a) noexcept { return std::sqrt(dot(a, a)); }

// Vektorn vriden av enhetskvaternionen: v + 2w(u×v) + 2u×(u×v).
Vec3 rotate(const Quaternion& q, const Vec3& v) noexcept {
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 uv = cross(u, v);
    const Vec3 uuv = cross(u, uv);
    return add(v, add(scale(uv, 2.0 * q.w), scale(uuv, 2.0)));
}

// OpenXR (x höger, y upp, −z framåt) → Glaze (x öster, y norr, z upp).
Vec3 toGlaze(const Vec3& p) noexcept { return {p.x, -p.z, p.y}; }

// Vektorn vriden `yawDegrees` MEDURS (sett uppifrån) kring Z: norr (0,1)
// blir riktningen (sin ψ, cos ψ).
Vec3 rotateYaw(double yawDegrees, const Vec3& v) noexcept {
    const double a = yawDegrees * kRad;
    const double s = std::sin(a);
    const double c = std::cos(a);
    return {v.x * c + v.y * s, -v.x * s + v.y * c, v.z};
}

// Vinkeln i (−180, 180].
double wrapDegrees(double d) noexcept {
    d = std::fmod(d, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d <= -180.0) d += 360.0;
    return d;
}

// Kompassriktningen för en vågrät riktning (x öster, y norr).
double compassDegrees(double east, double north) noexcept {
    return std::atan2(east, north) * kDeg;
}

} // namespace

std::optional<Anchor> makeAnchor(const TrackingPose& initialHead,
                                 Vec3 cameraEye,
                                 double cameraYawDegrees) noexcept {
    if (!finite(initialHead.position) || !finite(cameraEye) || !finite(cameraYawDegrees)) {
        return std::nullopt;
    }
    const auto q = unitQuaternion(initialHead.orientation);
    if (!q) return std::nullopt;

    // Huvudets horisontella blick i Glaze-världen utan förankring. Rakt ned
    // pekar huvudets upp framåt i horisonten, rakt upp bakåt.
    const Vec3 forward = toGlaze(rotate(*q, {0.0, 0.0, -1.0}));
    double east = forward.x;
    double north = forward.y;
    if (std::hypot(east, north) < kHorizontalEpsilon) {
        const Vec3 up = toGlaze(rotate(*q, {0.0, 1.0, 0.0}));
        const double s = forward.z < 0.0 ? 1.0 : -1.0;
        east = s * up.x;
        north = s * up.y;
        if (std::hypot(east, north) < kHorizontalEpsilon) return std::nullopt;
    }

    Anchor anchor;
    anchor.yawDegrees = wrapDegrees(cameraYawDegrees - compassDegrees(east, north));
    const Vec3 headRotated = rotateYaw(anchor.yawDegrees, toGlaze(initialHead.position));
    anchor.origin = sub(cameraEye, headRotated);
    if (!finite(anchor.origin) || !finite(anchor.yawDegrees)) return std::nullopt;
    return anchor;
}

std::optional<WorldPose> toWorldPose(const Anchor& anchor,
                                     const TrackingPose& pose) noexcept {
    if (!finite(anchor.origin) || !finite(anchor.yawDegrees) || !finite(pose.position)) {
        return std::nullopt;
    }
    const auto q = unitQuaternion(pose.orientation);
    if (!q) return std::nullopt;

    const auto toWorld = [&](const Vec3& v) { return rotateYaw(anchor.yawDegrees, toGlaze(v)); };
    WorldPose world;
    world.position = add(anchor.origin, toWorld(pose.position));
    world.right    = toWorld(rotate(*q, {1.0, 0.0, 0.0}));
    world.up       = toWorld(rotate(*q, {0.0, 1.0, 0.0}));
    world.forward  = toWorld(rotate(*q, {0.0, 0.0, -1.0}));
    if (!finite(world.position)) return std::nullopt;
    return world;
}

std::optional<CameraAngles> cameraAngles(const WorldPose& pose,
                                         double previousYawDegrees) noexcept {
    // Vinklarna räknas ur forward och up, men en pose med en degenererad
    // axel är trasig och avvisas hel.
    if (!finite(pose.forward) || !finite(pose.up) || !finite(pose.right) ||
        !finite(previousYawDegrees)) {
        return std::nullopt;
    }
    const double fl = length(pose.forward);
    const double ul = length(pose.up);
    if (fl < kHorizontalEpsilon || ul < kHorizontalEpsilon ||
        length(pose.right) < kHorizontalEpsilon) {
        return std::nullopt;
    }
    const Vec3 forward = scale(pose.forward, 1.0 / fl);
    const Vec3 up = scale(pose.up, 1.0 / ul);

    const double horizontal = std::hypot(forward.x, forward.y);
    CameraAngles angles;
    angles.pitchDegrees = std::atan2(forward.z, horizontal) * kDeg;
    angles.yawDegrees = horizontal < kHorizontalEpsilon
        ? previousYawDegrees
        : compassDegrees(forward.x, forward.y);

    // Den orullade basen för blicken: höger är vågrätt åt höger om
    // kompassriktningen, upp vinkelrätt mot båda. Rollen är hur långt `up`
    // är vriden från den åt höger (positivt = kameran medurs).
    const double yawRad = angles.yawDegrees * kRad;
    const Vec3 baseRight{std::cos(yawRad), -std::sin(yawRad), 0.0};
    const Vec3 baseUp = cross(baseRight, forward);
    const double sideways = dot(up, baseRight);
    const double upwards = dot(up, baseUp);
    if (std::hypot(sideways, upwards) < 1e-6) return std::nullopt;
    angles.rollDegrees = std::atan2(sideways, upwards) * kDeg;

    if (!finite(angles.yawDegrees) || !finite(angles.pitchDegrees) || !finite(angles.rollDegrees)) {
        return std::nullopt;
    }
    angles.yawDegrees = wrapDegrees(angles.yawDegrees);
    return angles;
}

} // namespace glaze::openxr
