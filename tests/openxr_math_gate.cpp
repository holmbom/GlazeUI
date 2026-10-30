// Prov av sidopaketet OpenXR:s matematik utan fönster, GPU eller runtime. Varje kontroll
// skriver vad som föll; programmet returnerar 1 om något föll, annars 0.
//
// Förväntade värden kommer ur analytiska fall (en vänstersväng på 90° pekar
// västerut) och geometriska invarianter (ortonormala axlar, bevarat avstånd,
// alla ursprungliga frustumkanter ryms), aldrig ur bibliotekets egna formler.
// Vinklar jämförs i grader med 1e-6, vektorer i meter med 1e-9.

#include <glaze_openxr/math.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <source_location>
#include <string>
#include <vector>

namespace {

using namespace glaze::openxr;

constexpr double kDeg = std::numbers::pi / 180.0;
constexpr double kPi  = std::numbers::pi;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

constexpr double kVecTol   = 1e-9;
constexpr double kAngleTol = 1e-6;

int g_failures = 0;
int g_checks   = 0;

void report(const std::string& what, const std::source_location& where)
{
    ++g_failures;
    std::cerr << "FALL rad " << where.line() << ": " << what << "\n";
}

void check(bool ok, const std::string& what,
           const std::source_location& where = std::source_location::current())
{
    ++g_checks;
    if (!ok)
        report(what, where);
}

void checkNear(double actual, double expected, double tol, const std::string& what,
               const std::source_location& where = std::source_location::current())
{
    ++g_checks;
    if (!(std::abs(actual - expected) <= tol)) {
        report(what + ": fick " + std::to_string(actual) + ", väntade " +
                   std::to_string(expected) + " (tolerans " + std::to_string(tol) + ")",
               where);
    }
}

// Skillnaden mellan två kompassvinklar i grader, i [−180, 180].
double angleDiff(double a, double b)
{
    double d = std::fmod(a - b, 360.0);
    if (d > 180.0)
        d -= 360.0;
    if (d < -180.0)
        d += 360.0;
    return d;
}

void checkAngle(double actual, double expected, const std::string& what,
                const std::source_location& where = std::source_location::current())
{
    ++g_checks;
    if (!(std::abs(angleDiff(actual, expected)) <= kAngleTol)) {
        report(what + ": fick " + std::to_string(actual) + "°, väntade " +
                   std::to_string(expected) + "°",
               where);
    }
}

// ----------------------------------------------------------------- vektorer

Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 mul(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double length(Vec3 a) { return std::sqrt(dot(a, a)); }
Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

void checkVec(Vec3 actual, Vec3 expected, const std::string& what, double tol = kVecTol,
              const std::source_location& where = std::source_location::current())
{
    ++g_checks;
    if (!(length(sub(actual, expected)) <= tol)) {
        report(what + ": fick (" + std::to_string(actual.x) + ", " + std::to_string(actual.y) +
                   ", " + std::to_string(actual.z) + "), väntade (" + std::to_string(expected.x) +
                   ", " + std::to_string(expected.y) + ", " + std::to_string(expected.z) + ")",
               where);
    }
}

// ------------------------------------------------------------- kvaternioner

Quaternion axisAngle(Vec3 axis, double degrees)
{
    const double half = degrees * kDeg / 2.0;
    const double s    = std::sin(half) / length(axis);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(half)};
}

// Hamilton-produkten: `b` verkar först, sedan `a` (a·b i lokala axlar:
// först a, sedan b kring den redan vridna ramen).
Quaternion mulQ(Quaternion a, Quaternion b)
{
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Vec3 rotate(Quaternion q, Vec3 v)
{
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = mul(cross(u, v), 2.0);
    return add(add(v, mul(t, q.w)), cross(u, t));
}

// OpenXR-vridningar: yaw åt VÄNSTER kring +y, pitch UPPÅT kring +x, och roll
// kring +z (som pekar bakåt, mot betraktaren).
constexpr Vec3 kAxisX{1, 0, 0};
constexpr Vec3 kAxisY{0, 1, 0};
constexpr Vec3 kAxisZ{0, 0, 1};
const Quaternion kIdentity{0, 0, 0, 1};

TrackingPose pose(Vec3 position, Quaternion q = kIdentity)
{
    return TrackingPose{position, q};
}

// En huvudvridning: yaw vänster, pitch upp och roll-vinkeln kring +z, alla i
// grader, i den ordningen i huvudets egna axlar.
Quaternion headRotation(double yawLeft, double pitchUp, double rollAboutZ)
{
    return mulQ(mulQ(axisAngle(kAxisY, yawLeft), axisAngle(kAxisX, pitchUp)),
                axisAngle(kAxisZ, rollAboutZ));
}

template <class T>
const T* require(const std::optional<T>& value, const std::string& what,
                 const std::source_location& where = std::source_location::current())
{
    ++g_checks;
    if (!value) {
        report(what + ": fick tomt resultat", where);
        return nullptr;
    }
    return &*value;
}

// Kompassriktningen för en horisontell blick (0 = norr, 90 = öster).
double compassOf(Vec3 forward)
{
    return std::atan2(forward.x, forward.y) / kDeg;
}

// Tre ortonormala, högerhänta axlar: längd 1, vinkelräta, höger × framåt = upp.
void checkFrame(const WorldPose& w, const std::string& what,
                const std::source_location& where = std::source_location::current())
{
    checkNear(length(w.right), 1.0, kVecTol, what + ": |höger|", where);
    checkNear(length(w.up), 1.0, kVecTol, what + ": |upp|", where);
    checkNear(length(w.forward), 1.0, kVecTol, what + ": |framåt|", where);
    checkNear(dot(w.right, w.up), 0.0, kVecTol, what + ": höger·upp", where);
    checkNear(dot(w.right, w.forward), 0.0, kVecTol, what + ": höger·framåt", where);
    checkNear(dot(w.up, w.forward), 0.0, kVecTol, what + ": upp·framåt", where);
    checkVec(cross(w.right, w.forward), w.up, what + ": höger × framåt = upp (högerhänt)",
             kVecTol, where);
}

// ------------------------------------------------------------------- proven

void testAxisIdentity()
{
    const Anchor anchor{{0, 0, 0}, 0.0};

    const auto w = toWorldPose(anchor, pose({0, 0, 0}));
    if (const WorldPose* p = require(w, "identitetsposen")) {
        checkVec(p->position, {0, 0, 0}, "position i origo");
        checkVec(p->right, {1, 0, 0}, "XR höger → öster");
        checkVec(p->up, {0, 0, 1}, "XR upp → Glaze upp");
        checkVec(p->forward, {0, 1, 0}, "XR framåt (−z) → norr");
        checkFrame(*p, "identiteten");
    }

    // 1 m höger, 2 m upp, 3 m BAKÅT (z = +3): öster 1, söder 3, upp 2.
    const auto moved = toWorldPose(anchor, pose({1, 2, 3}));
    if (const WorldPose* p = require(moved, "förflyttad pose"))
        checkVec(p->position, {1, -3, 2}, "positionen (1,2,3) i spårningsrummet");

    // Vänstersväng 90° kring +y: blicken västerut, höger hand norrut.
    const auto left = toWorldPose(anchor, pose({0, 0, 0}, axisAngle(kAxisY, 90.0)));
    if (const WorldPose* p = require(left, "vänstersväng")) {
        checkVec(p->forward, {-1, 0, 0}, "blicken västerut efter vänstersväng");
        checkVec(p->right, {0, 1, 0}, "höger hand norrut efter vänstersväng");
        checkVec(p->up, {0, 0, 1}, "upp oförändrad");
    }

    // Nick 90° uppåt kring +x: blicken rakt upp, upp-axeln pekar bakåt (söder).
    const auto pitched = toWorldPose(anchor, pose({0, 0, 0}, axisAngle(kAxisX, 90.0)));
    if (const WorldPose* p = require(pitched, "nick uppåt")) {
        checkVec(p->forward, {0, 0, 1}, "blicken rakt upp");
        checkVec(p->up, {0, -1, 0}, "huvudets upp pekar söderut när blicken är rakt upp");
    }
}

void testHandedness()
{
    struct Case {
        double yaw;
        Quaternion q;
        Vec3 origin;
    };
    const std::vector<Case> cases = {
        {0.0, kIdentity, {0, 0, 0}},
        {33.0, headRotation(25.0, 40.0, -15.0), {10, 20, 5}},
        {-120.0, headRotation(-170.0, -60.0, 80.0), {674000, 6580000, 30}},
        {200.0, headRotation(90.0, 10.0, 170.0), {-5, 7, -2}},
    };
    for (const Case& c : cases) {
        const auto w = toWorldPose(Anchor{c.origin, c.yaw}, pose({0.3, 1.5, -0.7}, c.q));
        if (const WorldPose* p = require(w, "ortonormal pose"))
            checkFrame(*p, "ortonormala axlar yaw " + std::to_string(c.yaw));
    }
}

void testAnchorYaw()
{
    struct Case {
        double yaw;
        Vec3 forward;
        Vec3 right;
    };
    // Förankringens yaw vrider spårningsrummet medurs sett ovanifrån.
    const std::vector<Case> cases = {
        {0.0, {0, 1, 0}, {1, 0, 0}},
        {90.0, {1, 0, 0}, {0, -1, 0}},
        {180.0, {0, -1, 0}, {-1, 0, 0}},
        {270.0, {-1, 0, 0}, {0, 1, 0}},
    };
    const Vec3 origin{10, 20, 5};
    for (const Case& c : cases) {
        const std::string tag = "förankring yaw " + std::to_string(c.yaw);
        const Anchor anchor{origin, c.yaw};
        const auto w = toWorldPose(anchor, pose({0, 0, 0}));
        if (const WorldPose* p = require(w, tag)) {
            checkVec(p->position, origin, tag + ": origo");
            checkVec(p->forward, c.forward, tag + ": blicken");
            checkVec(p->right, c.right, tag + ": höger");
            checkVec(p->up, {0, 0, 1}, tag + ": upp");
        }
        // 2 m framför origo i spårningsrummet ligger 2 m längs yaw-riktningen.
        const auto ahead = toWorldPose(anchor, pose({0, 0, -2}));
        if (const WorldPose* p = require(ahead, tag + " framåt"))
            checkVec(p->position, add(origin, mul(c.forward, 2.0)), tag + ": 2 m framåt");
        // 1 m åt höger i spårningsrummet ligger 1 m längs högerriktningen.
        const auto rightOf = toWorldPose(anchor, pose({1, 0, 0}));
        if (const WorldPose* p = require(rightOf, tag + " höger"))
            checkVec(p->position, add(origin, c.right), tag + ": 1 m åt höger");
    }
}

void testMakeAnchor()
{
    struct Case {
        TrackingPose head;
        Vec3 eye;
        double yaw;
    };
    const std::vector<Case> cases = {
        {pose({0, 0, 0}), {0, -6, 1.7}, 0.0},
        {pose({0.3, 1.6, -0.2}), {0, -6, 1.7}, 90.0},
        {pose({0.3, 1.6, -0.2}, headRotation(30.0, 0.0, 0.0)), {674000, 6580000, 30}, 45.0},
        // Nedåtlutad och rullad blick: den horisontella blicken gäller.
        {pose({-0.4, 1.2, 0.5}, headRotation(-75.0, -20.0, 12.0)), {12.5, -3.25, 1.7}, 200.0},
        {pose({1.0, 0.9, -1.0}, headRotation(170.0, 35.0, -8.0)), {674123.456, 6580987.654, 31.2}, -90.0},
    };
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const Case& c = cases[i];
        const std::string tag = "makeAnchor fall " + std::to_string(i);
        const auto anchor = makeAnchor(c.head, c.eye, c.yaw);
        const Anchor* a = require(anchor, tag);
        if (!a)
            continue;
        const auto w = toWorldPose(*a, c.head);
        const WorldPose* p = require(w, tag + " toWorldPose");
        if (!p)
            continue;
        checkVec(p->position, c.eye, tag + ": huvudet i kamerans öga", 1e-6);
        const double horizontal = std::hypot(p->forward.x, p->forward.y);
        check(horizontal > 1e-3, tag + ": blicken är horisontell nog");
        checkAngle(compassOf(p->forward), c.yaw, tag + ": blickens kompassriktning");
    }
}

void testWorldLock()
{
    const Vec3 eye{674000, 6580000, 30};
    const TrackingPose head0 = pose({0.2, 1.6, 0.1});
    const auto anchor = makeAnchor(head0, eye, 90.0);   // kameran tittar österut
    const Anchor* a   = require(anchor, "förankring för världslåsning");
    if (!a)
        return;

    // Spårningsrummets origo ligger kvar i världen vad huvudet än gör.
    const auto origin = toWorldPose(*a, pose({0, 0, 0}));
    if (const WorldPose* p = require(origin, "spårningsrummets origo"))
        checkVec(p->position, a->origin, "origo = förankringens origin");

    // 1 m framåt i spårningsrummet = 1 m österut (kamerans riktning).
    const auto fwd = toWorldPose(*a, pose({0.2, 1.6, -0.9}));
    if (const WorldPose* p = require(fwd, "1 m framåt"))
        checkVec(p->position, add(eye, {1, 0, 0}), "huvudet 1 m framåt");

    // 0,5 m åt höger = 0,5 m söderut när blicken är österut.
    const auto rgt = toWorldPose(*a, pose({0.7, 1.6, 0.1}));
    if (const WorldPose* p = require(rgt, "0,5 m höger"))
        checkVec(p->position, add(eye, {0, -0.5, 0}), "huvudet 0,5 m åt höger");

    // 0,3 m upp = 0,3 m i höjd.
    const auto upp = toWorldPose(*a, pose({0.2, 1.9, 0.1}));
    if (const WorldPose* p = require(upp, "0,3 m upp"))
        checkVec(p->position, add(eye, {0, 0, 0.3}), "huvudet 0,3 m upp");

    // Vridning på stället: positionen står kvar, blicken går vänster från
    // öster till norr.
    const auto turned = toWorldPose(*a, pose({0.2, 1.6, 0.1}, axisAngle(kAxisY, 90.0)));
    if (const WorldPose* p = require(turned, "vridning på stället")) {
        checkVec(p->position, eye, "positionen står kvar vid vridning");
        checkVec(p->forward, {0, 1, 0}, "blicken norrut efter 90° vänster från öster");
    }

    // Förflyttning med spaken görs genom en ny förankring för samma XR-huvud:
    // huvudet står kvar i världen och blicken vrids 30° medurs.
    const TrackingPose head1 = pose({0.5, 1.7, -0.4}, headRotation(20.0, 10.0, 0.0));
    const auto w1 = toWorldPose(*a, head1);
    const WorldPose* p1 = require(w1, "huvudet före omankring");
    if (!p1)
        return;
    const double yaw1 = compassOf(p1->forward);
    const auto a2     = makeAnchor(head1, p1->position, yaw1 + 30.0);
    const Anchor* an2 = require(a2, "omankring");
    if (!an2)
        return;
    const auto w2 = toWorldPose(*an2, head1);
    if (const WorldPose* p2 = require(w2, "huvudet efter omankring")) {
        checkVec(p2->position, p1->position, "huvudet står kvar i världen vid omankring", 1e-6);
        checkAngle(compassOf(p2->forward), yaw1 + 30.0, "blicken vriden 30° medurs");
    }
}

void testEyeSeparation()
{
    const double halfIpd = 0.032;
    struct Case {
        Anchor anchor;
        TrackingPose head;
    };
    const std::vector<Case> cases = {
        {{{0, 0, 0}, 0.0}, pose({0, 1.6, 0})},
        {{{674000, 6580000, 30}, 137.0},
         pose({0.4, 1.7, -0.3}, headRotation(35.0, -25.0, 18.0))},
        {{{-12, 40, 2}, -75.0}, pose({-1.0, 1.2, 0.8}, headRotation(-120.0, 55.0, -40.0))},
    };
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const Case& c = cases[i];
        const std::string tag = "ögonavstånd fall " + std::to_string(i);
        // Ögonen sitter i huvudets egna x-led, vänster −x och höger +x.
        const Vec3 off     = rotate(c.head.orientation, {halfIpd, 0, 0});
        const auto leftW   = toWorldPose(c.anchor, pose(sub(c.head.position, off), c.head.orientation));
        const auto rightW  = toWorldPose(c.anchor, pose(add(c.head.position, off), c.head.orientation));
        const auto headW   = toWorldPose(c.anchor, c.head);
        const WorldPose* l = require(leftW, tag + " vänster öga");
        const WorldPose* r = require(rightW, tag + " höger öga");
        const WorldPose* h = require(headW, tag + " huvud");
        if (!l || !r || !h)
            continue;
        const Vec3 between = sub(r->position, l->position);
        checkNear(length(between), 2.0 * halfIpd, 1e-6, tag + ": ögonavståndet bevaras");
        checkNear(dot(between, h->right), 2.0 * halfIpd, 1e-6,
                  tag + ": höger öga ligger åt huvudets högra sida");
        checkNear(dot(between, h->up), 0.0, 1e-6, tag + ": ögonen i samma höjd över huvudets upp");
        checkNear(dot(between, h->forward), 0.0, 1e-6, tag + ": ögonen i samma djup");
    }
}

// Pose ur kamerans vinklar med de lagar camera.h anger: blicken ur yaw och
// pitch, den orullade basen vågrät, och positiv roll vrider KAMERAN medurs
// sett bakifrån (upp-axeln lutar åt höger).
WorldPose analyticPose(double yawDeg, double pitchDeg, double rollDeg)
{
    const double y = yawDeg * kDeg, p = pitchDeg * kDeg, r = rollDeg * kDeg;
    const Vec3 forward{std::cos(p) * std::sin(y), std::cos(p) * std::cos(y), std::sin(p)};
    const Vec3 right0{std::cos(y), -std::sin(y), 0.0};
    const Vec3 up0 = cross(right0, forward);
    WorldPose w;
    w.position = {0, 0, 0};
    w.forward  = forward;
    w.right    = sub(mul(right0, std::cos(r)), mul(up0, std::sin(r)));
    w.up       = add(mul(up0, std::cos(r)), mul(right0, std::sin(r)));
    return w;
}

void testPitchAndRoll()
{
    struct Case {
        double yaw, pitch, roll;
    };
    const std::vector<Case> cases = {
        {0, 0, 0},      {90, 0, 0},     {-90, 0, 0},    {180, 0, 0},   {30, 20, 0},
        {200, -45, 10}, {45, 0, 25},    {-120, 60, -30}, {10, 5, -170}, {30, 89.9, 10},
        {300, -89.9, -20}, {0, 0, 90},  {75, 15, -90},
    };
    for (const Case& c : cases) {
        const std::string tag = "vinklar (" + std::to_string(c.yaw) + ", " + std::to_string(c.pitch) +
                                ", " + std::to_string(c.roll) + ")";
        const WorldPose w = analyticPose(c.yaw, c.pitch, c.roll);
        const auto angles = cameraAngles(w, 0.0);
        if (const CameraAngles* a = require(angles, tag)) {
            checkAngle(a->yawDegrees, c.yaw, tag + ": yaw");
            checkNear(a->pitchDegrees, c.pitch, kAngleTol, tag + ": pitch");
            checkAngle(a->rollDegrees, c.roll, tag + ": roll");
        }
    }

    // Samma vinklar genom OpenXR-kvaternioner och en förankring. Roll kring +z
    // med negativ vinkel lutar upp-axeln åt höger = positiv kamerarull.
    struct XrCase {
        double anchorYaw, left, up, aboutZ;
    };
    const std::vector<XrCase> xr = {
        {0, 0, 0, 0},   {0, 0, 0, -20}, {0, 0, 30, 0},   {0, 0, -30, 0},
        {0, 25, 40, -15}, {90, -60, 15, 33}, {200, 170, -50, -70},
    };
    for (const XrCase& c : xr) {
        const std::string tag = "XR-kvaternion (" + std::to_string(c.anchorYaw) + ", " +
                                std::to_string(c.left) + ", " + std::to_string(c.up) + ", " +
                                std::to_string(c.aboutZ) + ")";
        const auto w = toWorldPose(Anchor{{0, 0, 0}, c.anchorYaw},
                                   pose({0, 0, 0}, headRotation(c.left, c.up, c.aboutZ)));
        const WorldPose* p = require(w, tag);
        if (!p)
            continue;
        const auto angles = cameraAngles(*p, 0.0);
        if (const CameraAngles* a = require(angles, tag + " cameraAngles")) {
            checkAngle(a->yawDegrees, c.anchorYaw - c.left, tag + ": yaw");
            checkNear(a->pitchDegrees, c.up, kAngleTol, tag + ": pitch");
            checkAngle(a->rollDegrees, -c.aboutZ, tag + ": roll");
        }
    }
}

void testStraightUpAndDown()
{
    for (const double prev : {40.0, 250.0, -10.0}) {
        const double y = prev * kDeg;
        for (const double roll : {0.0, 15.0, -35.0}) {
            const std::string tag = "rakt upp/ned, tidigare yaw " + std::to_string(prev) +
                                    ", roll " + std::to_string(roll);
            const double r = roll * kDeg;
            // Rakt upp: huvudets upp-axel pekar mot det håll blicken hade
            // bakåt. Rakt ned: framåt. Orullat; rullen läggs på enligt camera.h.
            const Vec3 right0{std::cos(y), -std::sin(y), 0};
            for (const int dir : {+1, -1}) {
                WorldPose w;
                w.forward = {0, 0, static_cast<double>(dir)};
                const Vec3 up0 = cross(right0, w.forward);
                w.right = sub(mul(right0, std::cos(r)), mul(up0, std::sin(r)));
                w.up    = add(mul(up0, std::cos(r)), mul(right0, std::sin(r)));
                const auto angles = cameraAngles(w, prev);
                if (const CameraAngles* a = require(angles, tag + (dir > 0 ? " upp" : " ned"))) {
                    checkAngle(a->yawDegrees, prev, tag + ": yaw följer tidigare yaw");
                    checkNear(a->pitchDegrees, dir > 0 ? 90.0 : -90.0, kAngleTol, tag + ": pitch");
                    checkAngle(a->rollDegrees, roll, tag + ": roll mot tidigare yaw");
                }
            }
        }
    }
}

void testLargeCoordinates()
{
    const Vec3 origin{674000, 6580000, 30};
    for (const double yaw : {0.0, 33.0, 90.0, 217.0}) {
        const Anchor anchor{origin, yaw};
        const auto a = toWorldPose(anchor, pose({0, 1.6, 0}));
        const auto b = toWorldPose(anchor, pose({0.001, 1.6, 0}));
        const auto c = toWorldPose(anchor, pose({0, 1.6, -0.001}));
        const WorldPose* pa = require(a, "stora koordinater a");
        const WorldPose* pb = require(b, "stora koordinater b");
        const WorldPose* pc = require(c, "stora koordinater c");
        if (!pa || !pb || !pc)
            continue;
        const std::string tag = "SWEREF-koordinater yaw " + std::to_string(yaw);
        // En millimeter åt höger respektive framåt syns som en millimeter.
        checkNear(length(sub(pb->position, pa->position)), 0.001, 1e-8, tag + ": 1 mm åt höger");
        checkNear(length(sub(pc->position, pa->position)), 0.001, 1e-8, tag + ": 1 mm framåt");
        checkVec(sub(pb->position, pa->position), mul(pa->right, 0.001), tag + ": mm längs höger", 1e-8);
        checkVec(sub(pc->position, pa->position), mul(pa->forward, 0.001), tag + ": mm längs framåt", 1e-8);
        checkFrame(*pa, tag);
    }

    // Förankringen i stora koordinater: ögat hamnar där kameran står.
    const Vec3 eye{674000.123, 6580000.456, 31.7};
    const TrackingPose head = pose({0.3, 1.6, -0.2}, headRotation(40.0, -10.0, 5.0));
    const auto anchor = makeAnchor(head, eye, 123.0);
    if (const Anchor* an = require(anchor, "förankring i stora koordinater")) {
        const auto w = toWorldPose(*an, head);
        if (const WorldPose* p = require(w, "huvudet i stora koordinater"))
            checkVec(p->position, eye, "ögat vid kameran", 1e-7);
    }
}

// Alla kontroller av en symmetrisk bild mot de ursprungliga synfältet.
void checkSymmetric(const std::string& tag, const Fov& fov, const ViewSize& size, double scale)
{
    const auto result = makeSymmetricView(fov, size, scale);
    const SymmetricView* v = require(result, tag);
    if (!v)
        return;

    // Måtten: skalan × det rekommenderade (högst en pixel avrundning), högst
    // runtimens största, och aldrig noll.
    const double wantW = std::min(scale * size.recommendedWidth, static_cast<double>(size.maxWidth));
    const double wantH = std::min(scale * size.recommendedHeight, static_cast<double>(size.maxHeight));
    check(v->width >= 1 && v->height >= 1, tag + ": måtten är minst 1 px");
    check(v->width <= size.maxWidth && v->height <= size.maxHeight, tag + ": inom runtimens största");
    checkNear(v->width, wantW, 1.0, tag + ": bredd", std::source_location::current());
    checkNear(v->height, wantH, 1.0, tag + ": höjd", std::source_location::current());

    const double aspect = static_cast<double>(v->width) / v->height;
    const double vfov   = static_cast<double>(v->verticalFovDegrees);
    const Fov& s        = v->submittedFov;

    // Symmetrisk bild: vänster = −höger, ned = −upp.
    checkNear(s.left, -s.right, 1e-12, tag + ": symmetri i sidled");
    checkNear(s.down, -s.up, 1e-12, tag + ": symmetri i höjd");
    check(s.right > 0 && s.up > 0, tag + ": positiva halvvinklar");

    // Skickad fov = det som ritas: vertikalen ur det returnerade float-värdet,
    // horisontellen ur bildens aspekt.
    checkNear(s.up, vfov * kDeg / 2.0, 1e-6, tag + ": skickad uppvinkel = ritad vertikal fov");
    checkNear(std::tan(s.right), aspect * std::tan(s.up), 1e-6 * std::tan(s.right),
              tag + ": skickad sidvinkel = atan(aspekt · tan(vfov/2))");

    // Alla ursprungliga kanter ryms, utan marginal åt fel håll.
    check(s.left <= fov.left + 1e-12, tag + ": vänsterkanten ryms");
    check(s.right >= fov.right - 1e-12, tag + ": högerkanten ryms");
    check(s.up >= fov.up - 1e-12, tag + ": överkanten ryms");
    check(s.down <= fov.down + 1e-12, tag + ": underkanten ryms");

    // Inte större än nödvändigt: den snävaste kanten, räknad med den faktiska
    // aspekten, avrundad utåt till float-grader (högst ett par float-steg över).
    const double tx = std::max(std::abs(std::tan(fov.left)), std::abs(std::tan(fov.right)));
    const double ty = std::max(std::abs(std::tan(fov.up)), std::abs(std::tan(fov.down)));
    const double needed = 2.0 * std::atan(std::max(ty, tx / aspect)) / kDeg;
    check(vfov >= needed - 1e-9, tag + ": vertikal fov täcker behovet");
    check(vfov <= needed + 1e-4, tag + ": vertikal fov är inte onödigt stor");
}

void testSymmetricView()
{
    const Fov asym{-0.90, 0.75, 0.80, -0.85};
    checkSymmetric("fov A (hög bild, sidan styr)", asym, {1832, 1920, 4096, 4096}, 0.5);
    checkSymmetric("fov B (bred bild, höjden styr)", asym, {1000, 500, 2000, 2000}, 1.0);
    checkSymmetric("fov C (klampad)", asym, {4000, 4000, 3000, 2000}, 2.0);
    checkSymmetric("fov D (udda avrundning)", Fov{-0.7, 0.9, 0.6, -0.5}, {1000, 1400, 4096, 4096}, 0.37);
    checkSymmetric("fov E (symmetriskt synfält)", Fov{-0.8, 0.8, 0.7, -0.7}, {1440, 1600, 4096, 4096}, 0.5);
    checkSymmetric("fov F (liten skala)", asym, {1832, 1920, 4096, 4096}, 0.01);
    checkSymmetric("fov G (spegelvänt asymmetriskt)", Fov{-0.75, 0.90, 0.85, -0.80}, {1700, 1700, 4096, 4096}, 0.73);

    // Klampningen ger exakt runtimens största.
    const auto clamped = makeSymmetricView(asym, {4000, 4000, 3000, 2000}, 2.0);
    if (const SymmetricView* v = require(clamped, "klampad bild")) {
        check(v->width == 3000, "klampad bredd = största bredd");
        check(v->height == 2000, "klampad höjd = största höjd");
    }
}

// ------------------------------------------------------------- avvisningar

void testRejections()
{
    const TrackingPose goodPose = pose({0.1, 1.6, 0.2});
    const Vec3 eye{1, 2, 3};
    const Quaternion zeroQ{0, 0, 0, 0};

    // makeAnchor
    check(makeAnchor(goodPose, eye, 0.0).has_value(), "makeAnchor: giltiga värden godtas");
    check(!makeAnchor(pose({kNaN, 0, 0}), eye, 0.0), "makeAnchor: NaN i huvudets position");
    check(!makeAnchor(pose({0, kInf, 0}), eye, 0.0), "makeAnchor: ∞ i huvudets position");
    check(!makeAnchor(pose({0, 0, 0}, Quaternion{kNaN, 0, 0, 1}), eye, 0.0), "makeAnchor: NaN i kvaternion");
    check(!makeAnchor(pose({0, 0, 0}, zeroQ), eye, 0.0), "makeAnchor: nollkvaternion");
    check(!makeAnchor(goodPose, Vec3{kNaN, 0, 0}, 0.0), "makeAnchor: NaN i kamerans öga");
    check(!makeAnchor(goodPose, Vec3{0, 0, -kInf}, 0.0), "makeAnchor: −∞ i kamerans öga");
    check(!makeAnchor(goodPose, eye, kNaN), "makeAnchor: NaN-yaw");
    check(!makeAnchor(goodPose, eye, kInf), "makeAnchor: ∞-yaw");

    // toWorldPose
    const Anchor goodAnchor{{1, 2, 3}, 10.0};
    check(toWorldPose(goodAnchor, goodPose).has_value(), "toWorldPose: giltiga värden godtas");
    check(!toWorldPose(Anchor{{kNaN, 0, 0}, 0.0}, goodPose), "toWorldPose: NaN i origin");
    check(!toWorldPose(Anchor{{0, 0, kInf}, 0.0}, goodPose), "toWorldPose: ∞ i origin");
    check(!toWorldPose(Anchor{{0, 0, 0}, kNaN}, goodPose), "toWorldPose: NaN-yaw");
    check(!toWorldPose(Anchor{{0, 0, 0}, -kInf}, goodPose), "toWorldPose: ∞-yaw");
    check(!toWorldPose(goodAnchor, pose({kNaN, 0, 0})), "toWorldPose: NaN i position");
    check(!toWorldPose(goodAnchor, pose({0, 0, kInf})), "toWorldPose: ∞ i position");
    check(!toWorldPose(goodAnchor, pose({0, 0, 0}, Quaternion{0, kInf, 0, 1})), "toWorldPose: ∞ i kvaternion");
    check(!toWorldPose(goodAnchor, pose({0, 0, 0}, zeroQ)), "toWorldPose: nollkvaternion");

    // cameraAngles
    const WorldPose level = analyticPose(10.0, 5.0, 0.0);
    check(cameraAngles(level, 0.0).has_value(), "cameraAngles: giltig pose godtas");
    WorldPose bad = level;
    bad.forward.x = kNaN;
    check(!cameraAngles(bad, 0.0), "cameraAngles: NaN i framåt");
    bad = level;
    bad.up.z = kInf;
    check(!cameraAngles(bad, 0.0), "cameraAngles: ∞ i upp");
    bad = level;
    bad.right = {0, 0, 0};
    check(!cameraAngles(bad, 0.0), "cameraAngles: nollängd på höger");
    bad = level;
    bad.forward = {0, 0, 0};
    check(!cameraAngles(bad, 0.0), "cameraAngles: nollängd på framåt");

    // makeSymmetricView
    const Fov fov{-0.9, 0.75, 0.8, -0.85};
    const ViewSize size{1000, 1000, 4096, 4096};
    check(makeSymmetricView(fov, size, 0.5).has_value(), "makeSymmetricView: giltiga värden godtas");
    check(!makeSymmetricView(fov, size, 0.0), "makeSymmetricView: skala 0");
    check(!makeSymmetricView(fov, size, -1.0), "makeSymmetricView: negativ skala");
    check(!makeSymmetricView(fov, size, kNaN), "makeSymmetricView: NaN-skala");
    check(!makeSymmetricView(fov, size, kInf), "makeSymmetricView: ∞-skala");
    check(!makeSymmetricView(fov, {0, 1000, 4096, 4096}, 0.5), "makeSymmetricView: rekommenderad bredd 0");
    check(!makeSymmetricView(fov, {1000, 0, 4096, 4096}, 0.5), "makeSymmetricView: rekommenderad höjd 0");
    check(!makeSymmetricView(fov, {1000, 1000, 0, 0}, 0.5), "makeSymmetricView: största mått 0");
    check(!makeSymmetricView(Fov{kNaN, 0.75, 0.8, -0.85}, size, 0.5), "makeSymmetricView: NaN i fov");
    check(!makeSymmetricView(Fov{-0.9, kInf, 0.8, -0.85}, size, 0.5), "makeSymmetricView: ∞ i fov");
    check(!makeSymmetricView(Fov{0, 0, 0, 0}, size, 0.5), "makeSymmetricView: synfält utan utsträckning");
    check(!makeSymmetricView(Fov{-kPi / 2.0, 0.75, 0.8, -0.85}, size, 0.5),
          "makeSymmetricView: halvvinkel på 90° eller mer (omöjligt synfält)");
}

} // namespace

int main()
{
    testAxisIdentity();
    testHandedness();
    testAnchorYaw();
    testMakeAnchor();
    testWorldLock();
    testEyeSeparation();
    testPitchAndRoll();
    testStraightUpAndDown();
    testLargeCoordinates();
    testSymmetricView();
    testRejections();

    if (g_failures != 0) {
        std::cerr << g_failures << " av " << g_checks << " kontroller föll.\n";
        return 1;
    }
    std::cout << "Alla " << g_checks << " kontroller gick igenom.\n";
    return 0;
}
