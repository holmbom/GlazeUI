#include <glaze_openxr/math.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
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

// Under den här längden på en vektor saknar den riktning.
constexpr double kDirectionEpsilon = 1e-9;

// Kvaternionen som enhetskvaternion; en nollkvaternion eller ett icke-ändligt
// värde ger identiteten.
Quaternion normalized(const Quaternion& q) noexcept {
    const double n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!finite(n) || n < 1e-12) return Quaternion{};
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

// Hamilton-produkten a·b: först b, sedan a.
Quaternion multiply(const Quaternion& a, const Quaternion& b) noexcept {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Quaternion conjugate(const Quaternion& q) noexcept { return {-q.x, -q.y, -q.z, q.w}; }

// Vridningen `radians` kring den normerade axeln, högerhänt.
Quaternion axisAngle(const Vec3& unitAxis, double radians) noexcept {
    const double h = 0.5 * radians;
    const double s = std::sin(h);
    return {unitAxis.x * s, unitAxis.y * s, unitAxis.z * s, std::cos(h)};
}

// Vektorn som enhetsvektor, eller `ok == false` när den saknar riktning.
Vec3 unit(const Vec3& v, bool& ok) noexcept {
    const double n = length(v);
    ok = finite(n) && n >= kDirectionEpsilon;
    return ok ? scale(v, 1.0 / n) : Vec3{};
}

// Minsta vridningen som för den normerade riktningen `a` till den normerade
// `b`. Motsatta riktningar vrids ett halvt varv kring den axel vinkelrät mot
// `a` som ligger närmast världens upp (annars öster), så att en uppåtriktning
// består.
Quaternion shortestArc(const Vec3& a, const Vec3& b) noexcept {
    const double d = dot(a, b);
    if (d > -1.0 + 1e-12) {
        const Vec3 c = cross(a, b);
        return normalized({c.x, c.y, c.z, 1.0 + d});
    }
    for (const Vec3 candidate : {Vec3{0.0, 0.0, 1.0}, Vec3{1.0, 0.0, 0.0}}) {
        bool ok = false;
        const Vec3 axis = unit(sub(candidate, scale(a, dot(candidate, a))), ok);
        if (ok) return {axis.x, axis.y, axis.z, 0.0};
    }
    return Quaternion{};
}

// Rotationsmatrisen (rad i, kolumn j) ur en enhetskvaternion.
struct Matrix3 {
    double m[3][3];
};
Matrix3 toMatrix(const Quaternion& q) noexcept {
    return {{{1.0 - 2.0 * (q.y * q.y + q.z * q.z), 2.0 * (q.x * q.y - q.w * q.z),
              2.0 * (q.x * q.z + q.w * q.y)},
             {2.0 * (q.x * q.y + q.w * q.z), 1.0 - 2.0 * (q.x * q.x + q.z * q.z),
              2.0 * (q.y * q.z - q.w * q.x)},
             {2.0 * (q.x * q.z - q.w * q.y), 2.0 * (q.y * q.z + q.w * q.x),
              1.0 - 2.0 * (q.x * q.x + q.y * q.y)}}};
}

// Enhetskvaternionen för en rotationsmatris vars kolumner är de tre axlarna.
Quaternion fromMatrix(const Matrix3& r) noexcept {
    const auto& m = r.m;
    const double trace = m[0][0] + m[1][1] + m[2][2];
    Quaternion q;
    if (trace > 0.0) {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        q = {(m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s};
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        const double s = std::sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2.0;
        q = {0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s};
    } else if (m[1][1] > m[2][2]) {
        const double s = std::sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2.0;
        q = {(m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s};
    } else {
        const double s = std::sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2.0;
        q = {(m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s};
    }
    return normalized(q);
}

// Under det här värdet på cos(pitch) är vinklarna yaw och roll inte åtskilda
// (blicken rakt upp eller ned).
constexpr double kGimbalEpsilon = 1e-8;

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
    anchor.scale = 1.0;
    anchor.yawDegrees = wrapDegrees(cameraYawDegrees - compassDegrees(east, north));
    const Vec3 headRotated = rotateYaw(anchor.yawDegrees, toGlaze(initialHead.position));
    anchor.origin = sub(cameraEye, headRotated);
    if (!finite(anchor.origin) || !finite(anchor.yawDegrees)) return std::nullopt;
    return anchor;
}

std::optional<WorldPose> toWorldPose(const Anchor& anchor,
                                     const TrackingPose& pose) noexcept {
    if (!finite(anchor.origin) || !finite(anchor.yawDegrees) || !finite(anchor.scale) ||
        !(anchor.scale > 0.0) || !finite(pose.position)) {
        return std::nullopt;
    }
    const auto q = unitQuaternion(pose.orientation);
    if (!q) return std::nullopt;

    // Positionen skalas relativt origo; riktningarna skalas inte.
    const auto toWorld = [&](const Vec3& v) { return rotateYaw(anchor.yawDegrees, toGlaze(v)); };
    WorldPose world;
    world.position = add(anchor.origin, scale(toWorld(pose.position), anchor.scale));
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

// ------------------------------------------------------- STELA TRANSFORMER

Pose toPose(const WorldPose& pose) noexcept {
    Pose out;
    out.position = pose.position;
    // Lokala axlar: x höger, y framåt, z upp. Framåt och upp styr; höger
    // räknas som framåt × upp, så att axlarna blir ortonormala och högerhänta.
    bool okForward = false;
    bool okUp = false;
    const Vec3 y = unit(pose.forward, okForward);
    const Vec3 upHint = unit(pose.up, okUp);
    if (!okForward || !okUp) return out;
    bool okRight = false;
    const Vec3 x = unit(cross(y, upHint), okRight);
    if (!okRight) return out;
    const Vec3 z = cross(x, y);
    const Matrix3 axes{{{x.x, y.x, z.x}, {x.y, y.y, z.y}, {x.z, y.z, z.z}}};
    out.orientation = fromMatrix(axes);
    return out;
}

WorldPose toWorldPose(const Pose& pose) noexcept {
    const Quaternion q = normalized(pose.orientation);
    WorldPose world;
    world.position = pose.position;
    world.right    = rotate(q, {1.0, 0.0, 0.0});
    world.forward  = rotate(q, {0.0, 1.0, 0.0});
    world.up       = rotate(q, {0.0, 0.0, 1.0});
    return world;
}

Pose compose(const Pose& parent, const Pose& child) noexcept {
    const Quaternion pq = normalized(parent.orientation);
    const Quaternion cq = normalized(child.orientation);
    return {add(parent.position, rotate(pq, child.position)), normalized(multiply(pq, cq))};
}

Pose inverse(const Pose& pose) noexcept {
    const Quaternion inv = conjugate(normalized(pose.orientation));
    return {scale(rotate(inv, pose.position), -1.0), inv};
}

Vec3 transformPoint(const Pose& pose, Vec3 point) noexcept {
    return add(pose.position, rotate(normalized(pose.orientation), point));
}

Vec3 transformDirection(const Pose& pose, Vec3 direction) noexcept {
    return rotate(normalized(pose.orientation), direction);
}

Pose interpolate(const Pose& from, const Pose& to, double t) noexcept {
    const Quaternion a = normalized(from.orientation);
    Quaternion b = normalized(to.orientation);
    double d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    // q och −q är samma vridning; den närmaste av dem ger kortaste vägen.
    if (d < 0.0) {
        b = {-b.x, -b.y, -b.z, -b.w};
        d = -d;
    }
    Quaternion q;
    if (d > 0.9995) {
        // Nästan lika: linjär blandning är nog och undviker division med ~0.
        q = normalized({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                        a.w + (b.w - a.w) * t});
    } else {
        const double theta = std::acos(std::min(d, 1.0));
        const double sinTheta = std::sin(theta);
        const double wa = std::sin((1.0 - t) * theta) / sinTheta;
        const double wb = std::sin(t * theta) / sinTheta;
        q = normalized({wa * a.x + wb * b.x, wa * a.y + wb * b.y, wa * a.z + wb * b.z,
                        wa * a.w + wb * b.w});
    }
    return {add(from.position, scale(sub(to.position, from.position), t)), q};
}

// Placements vridning: R = Rz(−yaw) · Rx(pitch) · Ry(roll), alltså yaw medurs
// ovanifrån, pitch lyfter lokala +y och roll sänker lokala +x.
Quaternion fromYawPitchRoll(const YawPitchRoll& angles) noexcept {
    const Quaternion yaw   = axisAngle({0.0, 0.0, 1.0}, -angles.yawDegrees * kRad);
    const Quaternion pitch = axisAngle({1.0, 0.0, 0.0}, angles.pitchDegrees * kRad);
    const Quaternion roll  = axisAngle({0.0, 1.0, 0.0}, angles.rollDegrees * kRad);
    return normalized(multiply(multiply(yaw, pitch), roll));
}

YawPitchRoll toYawPitchRoll(const Quaternion& orientation) noexcept {
    // Ur R = Rz(−yaw) · Rx(pitch) · Ry(roll): kolumn 1 (framåt) är
    // (sin y · cos p, cos y · cos p, sin p) och rad 2 är
    // (−cos p · sin r, sin p, cos p · cos r).
    const auto m = toMatrix(normalized(orientation)).m;
    const double horizontal = std::hypot(m[0][1], m[1][1]);   // cos(pitch)
    YawPitchRoll angles;
    angles.pitchDegrees = std::atan2(m[2][1], horizontal) * kDeg;
    if (horizontal > kGimbalEpsilon) {
        angles.yawDegrees  = std::atan2(m[0][1], m[1][1]) * kDeg;
        angles.rollDegrees = std::atan2(-m[2][0], m[2][2]) * kDeg;
    } else {
        // Rakt upp eller ned samverkar yaw och roll; hela vridningen läggs på
        // yaw och rollen blir 0.
        angles.yawDegrees  = std::atan2(-m[1][0], m[0][0]) * kDeg;
        angles.rollDegrees = 0.0;
    }
    angles.yawDegrees  = wrapDegrees(angles.yawDegrees);
    angles.rollDegrees = wrapDegrees(angles.rollDegrees);
    return angles;
}

// --------------------------------------------------------------- GREPPEN

Pose followGrab(const Pose& handAtGrab, const Pose& objectAtGrab, const Pose& handNow) noexcept {
    // Föremålet i handens ram vid greppet, flyttat med handen till dess nya läge.
    return compose(handNow, compose(inverse(handAtGrab), objectAtGrab));
}

TwoHandResult twoHandManipulate(const Pose& leftAtGrab, const Pose& rightAtGrab,
                                const Pose& objectAtGrab, double scaleAtGrab,
                                const Pose& leftNow, const Pose& rightNow,
                                TwoHandRotation rotation) noexcept {
    const Vec3 midAtGrab = scale(add(leftAtGrab.position, rightAtGrab.position), 0.5);
    const Vec3 midNow    = scale(add(leftNow.position, rightNow.position), 0.5);
    const Vec3 lineAtGrab = sub(rightAtGrab.position, leftAtGrab.position);
    const Vec3 lineNow    = sub(rightNow.position, leftNow.position);

    // Utan en linje mellan händerna (hopfallande händer) finns varken vridning
    // eller avståndskvot: bara flytten gäller.
    bool okGrab = false;
    bool okNow  = false;
    const Vec3 dirAtGrab = unit(lineAtGrab, okGrab);
    const Vec3 dirNow    = unit(lineNow, okNow);
    const bool hasLine   = okGrab && okNow;

    const double ratio = hasLine ? length(lineNow) / length(lineAtGrab) : 1.0;

    Quaternion turn;
    if (hasLine) {
        switch (rotation) {
        case TwoHandRotation::Full:
            turn = shortestArc(dirAtGrab, dirNow);
            break;
        case TwoHandRotation::YawOnly: {
            // Vinkeln mellan linjernas vågräta delar, kring z (moturs positivt).
            const double crossZ = lineAtGrab.x * lineNow.y - lineAtGrab.y * lineNow.x;
            const double dotXY  = lineAtGrab.x * lineNow.x + lineAtGrab.y * lineNow.y;
            if (std::hypot(lineAtGrab.x, lineAtGrab.y) >= kDirectionEpsilon &&
                std::hypot(lineNow.x, lineNow.y) >= kDirectionEpsilon) {
                turn = axisAngle({0.0, 0.0, 1.0}, std::atan2(crossZ, dotXY));
            }
            break;
        }
        case TwoHandRotation::None:
            break;
        }
    }

    // Föremålet vrids och skalas kring greppets mittpunkt, som följer med till
    // mittpunkten nu.
    TwoHandResult result;
    const Vec3 offset = sub(objectAtGrab.position, midAtGrab);
    result.pose.position = add(midNow, scale(rotate(turn, offset), ratio));
    result.pose.orientation = normalized(multiply(turn, normalized(objectAtGrab.orientation)));
    result.scale = scaleAtGrab * ratio;
    return result;
}

// ----------------------------------------------------- YTOR OCH SNÄPP

std::optional<Pose> surfaceFrame(Vec3 point, Vec3 normal, Vec3 forward) noexcept {
    if (!finite(point) || !finite(normal) || !finite(forward)) return std::nullopt;
    bool ok = false;
    const Vec3 z = unit(normal, ok);
    if (!ok) return std::nullopt;

    // Önskad framåt projicerad på ytan; saknar den riktning där (noll eller
    // parallell med normalen) prövas världens norr och sedan öster.
    Vec3 y;
    ok = false;
    for (const Vec3 wanted : {forward, Vec3{0.0, 1.0, 0.0}, Vec3{1.0, 0.0, 0.0}}) {
        bool wantedOk = false;
        const Vec3 dir = unit(wanted, wantedOk);
        if (!wantedOk) continue;
        y = unit(sub(dir, scale(z, dot(dir, z))), ok);
        if (ok) break;
    }
    if (!ok) return std::nullopt;

    const Vec3 x = cross(y, z);
    Pose frame = toPose(WorldPose{point, x, z, y});
    frame.position = point;
    return frame;
}

Vec3 snapToGrid(Vec3 point, double step, Vec3 origin) noexcept {
    if (!(step > 0.0) || !finite(step) || !finite(point) || !finite(origin)) return point;
    const auto snap = [step](double v, double o) { return o + std::round((v - o) / step) * step; };
    return {snap(point.x, origin.x), snap(point.y, origin.y), snap(point.z, origin.z)};
}

double snapAngle(double degrees, double stepDegrees) noexcept {
    if (!(stepDegrees > 0.0) || !finite(stepDegrees) || !finite(degrees)) return degrees;
    return std::round(degrees / stepDegrees) * stepDegrees;
}

std::vector<Vec3> ballisticArc(Vec3 origin, Vec3 direction, double speed, double gravity,
                               double stepSeconds, double maxSeconds) {
    // Ett tak på antalet punkter skyddar mot en vektor som aldrig behövs.
    constexpr double kMaxPoints = 100000.0;
    bool ok = false;
    const Vec3 dir = unit(direction, ok);
    if (!ok || !finite(origin) || !finite(speed) || speed < 0.0 || !finite(gravity) ||
        gravity < 0.0 || !finite(stepSeconds) || !(stepSeconds > 0.0) || !finite(maxSeconds) ||
        maxSeconds < 0.0 || maxSeconds / stepSeconds > kMaxPoints) {
        return {};
    }

    const auto at = [&](double t) {
        return Vec3{origin.x + dir.x * speed * t, origin.y + dir.y * speed * t,
                    origin.z + dir.z * speed * t - 0.5 * gravity * t * t};
    };

    // Punkterna vid 0, step, 2·step … och en sista vid `maxSeconds` om den inte
    // redan är en av dem.
    const auto whole = static_cast<std::size_t>(std::floor(maxSeconds / stepSeconds + 1e-9));
    std::vector<Vec3> points;
    points.reserve(whole + 2);
    for (std::size_t i = 0; i <= whole; ++i) {
        points.push_back(at(static_cast<double>(i) * stepSeconds));
    }
    if (maxSeconds - static_cast<double>(whole) * stepSeconds > 1e-9 * stepSeconds) {
        points.push_back(at(maxSeconds));
    }
    return points;
}

} // namespace glaze::openxr
