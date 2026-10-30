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
}

// ======================================================= INTERAKTIONENS MATEMATIK

const Pose kIdentityPose{{0, 0, 0}, {0, 0, 0, 1}};
constexpr Vec3 kAxisUp{0, 0, 1};   // Glazes z

// Samma vridning: kvaternionerna lika upp till tecken.
void checkQuat(Quaternion actual, Quaternion expected, const std::string& what,
               double tol = kVecTol,
               const std::source_location& where = std::source_location::current())
{
    ++g_checks;
    const double d    = actual.x * expected.x + actual.y * expected.y + actual.z * expected.z +
                        actual.w * expected.w;
    const double sign = d < 0.0 ? -1.0 : 1.0;
    const double err  = std::max({std::abs(actual.x - sign * expected.x),
                                  std::abs(actual.y - sign * expected.y),
                                  std::abs(actual.z - sign * expected.z),
                                  std::abs(actual.w - sign * expected.w)});
    if (!(err <= tol)) {
        report(what + ": kvaternionerna skiljer " + std::to_string(err) + " (fick (" +
                   std::to_string(actual.x) + ", " + std::to_string(actual.y) + ", " +
                   std::to_string(actual.z) + ", " + std::to_string(actual.w) + "))",
               where);
    }
}

void checkPose(const Pose& actual, const Pose& expected, const std::string& what,
               double tol = kVecTol,
               const std::source_location& where = std::source_location::current())
{
    checkVec(actual.position, expected.position, what + ": läge", tol, where);
    checkQuat(actual.orientation, expected.orientation, what + ": orientering", tol, where);
}

// En vridning kring z i Placements riktning: yaw `degrees` medurs ovanifrån.
Quaternion compassTurn(double degrees)
{
    return axisAngle(kAxisUp, -degrees);
}

// Placements vridningsmatris ur dokumentationen i style3d.h, R = Rz(−yaw) · Rx(pitch) ·
// Ry(roll), med de vanliga högerhänta axelrotationerna. Kolumn 0 är lokala x (höger),
// kolumn 1 lokala y (framåt) och kolumn 2 lokala z (upp).
struct Matrix {
    double m[3][3];
};

Matrix matMul(const Matrix& a, const Matrix& b)
{
    Matrix out{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    return out;
}

Matrix placementMatrix(double yawDeg, double pitchDeg, double rollDeg)
{
    const double ay = -yawDeg * kDeg, ap = pitchDeg * kDeg, ar = rollDeg * kDeg;
    const Matrix rz{{{std::cos(ay), -std::sin(ay), 0}, {std::sin(ay), std::cos(ay), 0}, {0, 0, 1}}};
    const Matrix rx{{{1, 0, 0}, {0, std::cos(ap), -std::sin(ap)}, {0, std::sin(ap), std::cos(ap)}}};
    const Matrix ry{{{std::cos(ar), 0, std::sin(ar)}, {0, 1, 0}, {-std::sin(ar), 0, std::cos(ar)}}};
    return matMul(matMul(rz, rx), ry);
}

Vec3 column(const Matrix& a, int j)
{
    return {a.m[0][j], a.m[1][j], a.m[2][j]};
}

void testAnchorScale()
{
    // makeAnchor ger skalan 1.
    const auto made = makeAnchor(pose({0, 1.6, 0}), {0, 0, 1.7}, 0.0);
    if (const Anchor* a = require(made, "makeAnchor för skalan"))
        checkNear(a->scale, 1.0, 0.0, "makeAnchor: skala 1");

    // Skala 2, yaw 90: (1, 0, −2) i rummet = (1, 2, 0) i Glaze, vridet 90° medurs
    // (2, −1, 0), gånger 2, plus origo.
    const Anchor doubled{{10, 20, 5}, 90.0, 2.0};
    const auto w = toWorldPose(doubled, pose({1, 0, -2}));
    if (const WorldPose* p = require(w, "skala 2")) {
        checkVec(p->position, {14, 18, 5}, "skala 2: positionen skalas kring origo");
        checkVec(p->forward, {1, 0, 0}, "skala 2: riktningarna skalas inte (framåt)");
        checkVec(p->right, {0, -1, 0}, "skala 2: riktningarna skalas inte (höger)");
        checkVec(p->up, {0, 0, 1}, "skala 2: riktningarna skalas inte (upp)");
    }

    // Skala 10: huvudet 1,6 m upp hamnar 16 enheter upp; skala 0,1 ger 0,16.
    const auto big = toWorldPose(Anchor{{0, 0, 0}, 0.0, 10.0}, pose({0, 1.6, 0}));
    if (const WorldPose* p = require(big, "skala 10"))
        checkVec(p->position, {0, 0, 16}, "skala 10: huvudhöjden");
    const auto small = toWorldPose(Anchor{{0, 0, 0}, 0.0, 0.1}, pose({0, 1.6, 0}));
    if (const WorldPose* p = require(small, "skala 0,1"))
        checkVec(p->position, {0, 0, 0.16}, "skala 0,1: huvudhöjden");

    // Skala 1 ger samma pose som en förankring utan skala.
    const Anchor plain{{3, 4, 5}, 33.0};
    const auto a1 = toWorldPose(plain, pose({0.3, 1.5, -0.7}, headRotation(25.0, 40.0, -15.0)));
    const auto a2 = toWorldPose(Anchor{{3, 4, 5}, 33.0, 1.0},
                                pose({0.3, 1.5, -0.7}, headRotation(25.0, 40.0, -15.0)));
    const WorldPose* p1 = require(a1, "förval-skala");
    const WorldPose* p2 = require(a2, "uttrycklig skala 1");
    if (p1 && p2) {
        checkVec(p1->position, p2->position, "skala 1 = förvalet (läge)");
        checkVec(p1->forward, p2->forward, "skala 1 = förvalet (framåt)");
    }

    // Ogiltig eller icke-positiv skala ger tomt.
    for (const double bad : {0.0, -1.0, kNaN, kInf, -kInf})
        check(!toWorldPose(Anchor{{0, 0, 0}, 0.0, bad}, pose({0, 0, 0})),
              "toWorldPose: ogiltig skala " + std::to_string(bad));
}

void testPoseConversions()
{
    // Identiteten tittar norrut med z upp.
    const WorldPose id = toWorldPose(kIdentityPose);
    checkVec(id.right, {1, 0, 0}, "identitetspose: höger");
    checkVec(id.forward, {0, 1, 0}, "identitetspose: framåt");
    checkVec(id.up, {0, 0, 1}, "identitetspose: upp");

    // En vridning på 90° medurs ovanifrån (−90° kring z): framåt blir öster.
    const Pose east{{1, 2, 3}, compassTurn(90.0)};
    const WorldPose we = toWorldPose(east);
    checkVec(we.position, {1, 2, 3}, "österpose: läge");
    checkVec(we.forward, {1, 0, 0}, "österpose: framåt");
    checkVec(we.right, {0, -1, 0}, "österpose: höger (söderut)");
    checkVec(we.up, {0, 0, 1}, "österpose: upp");
    checkFrame(we, "österpose");
    checkPose(toPose(we), east, "toPose(toWorldPose(pose))");

    checkVec(transformDirection(east, {1, 0, 0}), {0, -1, 0}, "transformDirection: x → söder");
    checkVec(transformPoint(east, {1, 0, 0}), {1, 1, 3}, "transformPoint: vridning + läge");

    // Tur och retur för många orienteringar, också halvvarv (alla grenar av
    // matris → kvaternion).
    const std::vector<Quaternion> turns = {
        kIdentity,
        axisAngle({1, 0, 0}, 180.0),
        axisAngle({0, 1, 0}, 180.0),
        axisAngle({0, 0, 1}, 180.0),
        axisAngle({1, 1, 0}, 170.0),
        axisAngle({0, 1, 1}, 175.0),
        axisAngle({1, 0, 1}, 160.0),
        axisAngle({1, 2, 3}, 40.0),
        axisAngle({-3, 1, 2}, -130.0),
        axisAngle({1, -1, 1}, 95.0),
        headRotation(25.0, 40.0, -15.0),
        headRotation(-170.0, -60.0, 80.0),
    };
    for (std::size_t i = 0; i < turns.size(); ++i) {
        const Pose p{{0.5, -1.5, 2.5}, turns[i]};
        const std::string tag = "tur och retur " + std::to_string(i);
        const WorldPose w = toWorldPose(p);
        checkFrame(w, tag);
        checkPose(toPose(w), p, tag);
    }

    // Axlar som är skalade och inte helt vinkelräta ger ändå den rena posen:
    // framåt och upp styr, höger räknas ur dem.
    const Pose ref{{7, 8, 9}, axisAngle({1, 2, 3}, 40.0)};
    WorldPose skew = toWorldPose(ref);
    const Vec3 forward0 = skew.forward;
    skew.forward = mul(forward0, 3.0);
    skew.up      = add(mul(skew.up, 0.5), mul(forward0, 0.2));   // lutar mot framåt
    skew.right   = mul(skew.right, 7.0);
    checkPose(toPose(skew), ref, "toPose: skalade axlar");

    // Degenererade axlar ger identitetens orientering men behåller läget.
    WorldPose broken{};
    broken.position = {4, 5, 6};
    checkPose(toPose(broken), Pose{{4, 5, 6}, kIdentity}, "toPose: nollaxlar");
    broken.forward = {0, 1, 0};
    broken.up      = {0, 2, 0};   // parallell med framåt
    checkPose(toPose(broken), Pose{{4, 5, 6}, kIdentity}, "toPose: parallella axlar");

    // En kvaternion normeras; en nollkvaternion är identiteten.
    const Pose scaled{{0, 0, 0}, {0, 0, 2, 2}};   // 90° moturs kring z
    checkVec(transformDirection(scaled, {1, 0, 0}), {0, 1, 0}, "onormerad kvaternion normeras");
    const Pose zero{{1, 1, 1}, {0, 0, 0, 0}};
    checkVec(transformPoint(zero, {1, 2, 3}), {2, 3, 4}, "nollkvaternion = identitet");
}

void testComposeInverse()
{
    const Pose a{{1, 2, 3}, axisAngle({1, 1, 0}, 40.0)};
    const Pose b{{-2, 0.5, 1}, axisAngle({0, 1, 1}, -70.0)};
    const Pose c{{0.3, -4, 2}, axisAngle({1, 0, 1}, 130.0)};

    checkPose(compose(inverse(a), a), kIdentityPose, "inverse(a) · a = identitet");
    checkPose(compose(a, inverse(a)), kIdentityPose, "a · inverse(a) = identitet");
    checkPose(compose(compose(a, b), c), compose(a, compose(b, c)), "associativitet");
    checkPose(compose(kIdentityPose, a), a, "identitet · a");
    checkPose(compose(a, kIdentityPose), a, "a · identitet");
    checkPose(inverse(inverse(b)), b, "inverse(inverse(b)) = b");

    const Vec3 p{0.7, -1.1, 2.3};
    checkVec(transformPoint(compose(a, b), p), transformPoint(a, transformPoint(b, p)),
             "transformPoint genom compose");
    checkVec(transformPoint(inverse(a), transformPoint(a, p)), p, "transformPoint tur och retur");

    // En riktning flyttas inte men vrids, och längden består.
    const Vec3 d{0, 0, 5};
    checkVec(transformDirection(a, d), sub(transformPoint(a, d), a.position),
             "transformDirection = transformPoint utan läget");
    checkNear(length(transformDirection(a, d)), 5.0, kVecTol, "transformDirection bevarar längden");

    // Känt fall: föräldern står i (10, 0, 0) och tittar österut; ett barn 1 m
    // framför den hamnar 1 m österut.
    const Pose parent{{10, 0, 0}, compassTurn(90.0)};
    const Pose child{{0, 1, 0}, kIdentity};
    checkPose(compose(parent, child), Pose{{11, 0, 0}, compassTurn(90.0)}, "förälder · barn");
    // Barnet 1 m åt höger om en förälder som tittar österut ligger 1 m söderut.
    checkVec(transformPoint(parent, {1, 0, 0}), {10, -1, 0}, "höger om en österblickande pose");

    // Inversen av en ren förflyttning.
    checkPose(inverse(Pose{{1, 2, 3}, kIdentity}), Pose{{-1, -2, -3}, kIdentity}, "inverse av förflyttning");
    // Inversen av en ren vridning kring z.
    checkPose(inverse(Pose{{0, 0, 0}, compassTurn(90.0)}), Pose{{0, 0, 0}, compassTurn(-90.0)},
              "inverse av vridning");
}

void testInterpolate()
{
    const Pose from{{0, 0, 0}, kIdentity};
    const Pose to{{2, 4, 6}, compassTurn(90.0)};
    checkPose(interpolate(from, to, 0.0), from, "interpolate t = 0");
    checkPose(interpolate(from, to, 1.0), to, "interpolate t = 1");
    checkPose(interpolate(from, to, 0.5), Pose{{1, 2, 3}, compassTurn(45.0)}, "interpolate t = 0,5");
    checkPose(interpolate(from, to, 0.25), Pose{{0.5, 1, 1.5}, compassTurn(22.5)}, "interpolate t = 0,25");

    // Kortaste vägen: 10° till 350° (= −10°) går över 0°, inte över 180°. Samma
    // gäller när slutvridningen anges med motsatt tecken på kvaternionen.
    const Pose a{{0, 0, 0}, compassTurn(10.0)};
    const Pose b{{0, 0, 0}, compassTurn(350.0)};
    checkQuat(interpolate(a, b, 0.5).orientation, kIdentity, "kortaste vägen över 0°");
    checkQuat(interpolate(a, b, 0.25).orientation, compassTurn(5.0), "kortaste vägen, t = 0,25");
    const Quaternion nq = b.orientation;
    const Pose bNeg{{0, 0, 0}, {-nq.x, -nq.y, -nq.z, -nq.w}};
    checkQuat(interpolate(a, bNeg, 0.5).orientation, kIdentity, "kortaste vägen, motsatt tecken");

    // Resultatet är en enhetskvaternion.
    const Quaternion r = interpolate(from, Pose{{0, 0, 0}, axisAngle({1, 2, 3}, 120.0)}, 0.37).orientation;
    checkNear(std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w), 1.0, kVecTol,
              "interpolate: enhetskvaternion");
    // Nästan lika vridningar (linjär gren) ger fortfarande rätt mittvridning.
    checkQuat(interpolate(Pose{{0, 0, 0}, compassTurn(0.0)}, Pose{{0, 0, 0}, compassTurn(0.02)}, 0.5)
                  .orientation,
              compassTurn(0.01), "interpolate: små vinklar", 1e-9);
}

void testYawPitchRoll()
{
    // Handräknade fall mot Placement (R = Rz(−yaw) · Rx(pitch) · Ry(roll)).
    struct Case {
        double yaw, pitch, roll;
        Vec3 right, forward, up;
    };
    const std::vector<Case> cases = {
        {0, 0, 0, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
        // Yaw 90: blicken österut, höger hand söderut.
        {90, 0, 0, {0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        // Pitch 90: nosen rakt upp, upp-axeln söderut.
        {0, 90, 0, {1, 0, 0}, {0, 0, 1}, {0, -1, 0}},
        // Roll 90: höger sida rakt ned, upp-axeln åt öster.
        {0, 0, 90, {0, 0, -1}, {0, 1, 0}, {1, 0, 0}},
        // Yaw 90 sedan pitch 90: nosen upp, upp-axeln västerut.
        {90, 90, 0, {0, -1, 0}, {0, 0, 1}, {-1, 0, 0}},
        // Yaw 90 sedan roll 90.
        {90, 0, 90, {0, 0, -1}, {1, 0, 0}, {0, -1, 0}},
    };
    for (const Case& c : cases) {
        const std::string tag = "Placement (" + std::to_string(c.yaw) + ", " +
                                std::to_string(c.pitch) + ", " + std::to_string(c.roll) + ")";
        const Quaternion q = fromYawPitchRoll({c.yaw, c.pitch, c.roll});
        const WorldPose w = toWorldPose(Pose{{0, 0, 0}, q});
        checkVec(w.right, c.right, tag + ": höger");
        checkVec(w.forward, c.forward, tag + ": framåt");
        checkVec(w.up, c.up, tag + ": upp");
    }

    // Mot Placements matris ur style3d.h, och tur och retur.
    const std::vector<double> yaws   = {-170, -90, -30, 0, 45, 90, 135, 179};
    const std::vector<double> pitchs = {-80, -45, 0, 30, 89};
    const std::vector<double> rolls  = {-150, -60, 0, 25, 100, 170};
    for (const double yaw : yaws) {
        for (const double pitch : pitchs) {
            for (const double roll : rolls) {
                const std::string tag = "vinklar (" + std::to_string(yaw) + ", " +
                                        std::to_string(pitch) + ", " + std::to_string(roll) + ")";
                const Quaternion q = fromYawPitchRoll({yaw, pitch, roll});
                const Matrix ref   = placementMatrix(yaw, pitch, roll);
                const WorldPose w  = toWorldPose(Pose{{0, 0, 0}, q});
                checkVec(w.right, column(ref, 0), tag + ": höger mot matrisen");
                checkVec(w.forward, column(ref, 1), tag + ": framåt mot matrisen");
                checkVec(w.up, column(ref, 2), tag + ": upp mot matrisen");

                const YawPitchRoll back = toYawPitchRoll(q);
                checkAngle(back.yawDegrees, yaw, tag + ": yaw tur och retur");
                checkNear(back.pitchDegrees, pitch, kAngleTol, tag + ": pitch tur och retur");
                checkAngle(back.rollDegrees, roll, tag + ": roll tur och retur");

                // Samma vinklar som kamerans: yaw som kompass, pitch uppåt, roll
                // med positivt medurs — Placements och Camera3D:s tecken är lika.
                const auto cam = cameraAngles(w, 0.0);
                if (const CameraAngles* a = require(cam, tag + " cameraAngles")) {
                    checkAngle(a->yawDegrees, yaw, tag + ": kamerans yaw");
                    checkNear(a->pitchDegrees, pitch, kAngleTol, tag + ": kamerans pitch");
                    checkAngle(a->rollDegrees, roll, tag + ": kamerans roll");
                }
            }
        }
    }

    // Blicken rakt upp eller ned: vridningen läggs på yaw och rollen blir 0, men
    // posen består.
    for (const double pitch : {90.0, -90.0}) {
        const Quaternion q = fromYawPitchRoll({30.0, pitch, 20.0});
        const YawPitchRoll back = toYawPitchRoll(q);
        const std::string tag = "pitch " + std::to_string(pitch);
        checkNear(back.pitchDegrees, pitch, kAngleTol, tag + ": pitch");
        checkNear(back.rollDegrees, 0.0, kAngleTol, tag + ": roll 0");
        checkAngle(back.yawDegrees, pitch > 0 ? 30.0 - 20.0 : 30.0 + 20.0, tag + ": yaw tar rollen");
        checkQuat(fromYawPitchRoll(back), q, tag + ": samma vridning", 1e-9);
    }

    // En icke-normerad kvaternion normeras; en nollkvaternion är identiteten.
    const YawPitchRoll fromScaled = toYawPitchRoll(Quaternion{0, 0, 2 * std::sin(-45 * kDeg), 2 * std::cos(-45 * kDeg)});
    checkAngle(fromScaled.yawDegrees, 90.0, "toYawPitchRoll: onormerad kvaternion");
    const YawPitchRoll fromZero = toYawPitchRoll(Quaternion{0, 0, 0, 0});
    checkNear(fromZero.pitchDegrees, 0.0, kAngleTol, "toYawPitchRoll: nollkvaternion");
    checkAngle(fromZero.yawDegrees, 0.0, "toYawPitchRoll: nollkvaternion yaw");
}

void testFollowGrab()
{
    const Pose hand0{{1, 0, 1}, axisAngle({1, 0, 0}, 20.0)};
    const Pose obj0{{3, 1, 1}, axisAngle({0, 0, 1}, 35.0)};

    checkPose(followGrab(hand0, obj0, hand0), obj0, "followGrab: handen står kvar");

    // Ren förflyttning av handen flyttar föremålet lika mycket.
    const Pose moved{{3, 0, 1}, hand0.orientation};
    checkPose(followGrab(hand0, obj0, moved), Pose{{5, 1, 1}, obj0.orientation},
              "followGrab: handen flyttas 2 m");

    // Föremålet 1 m framför en hand i origo; handen flyttas till (5, 5, 0) och
    // vrids att titta österut: föremålet hamnar 1 m österut och vrids lika.
    const Pose h0{{0, 0, 0}, kIdentity};
    const Pose o0{{0, 1, 0}, kIdentity};
    const Pose h1{{5, 5, 0}, compassTurn(90.0)};
    checkPose(followGrab(h0, o0, h1), Pose{{6, 5, 0}, compassTurn(90.0)}, "followGrab: handen vrids österut");

    // Föremålet står stelt i handens ram.
    const Pose now{{-2, 3, 0.5}, axisAngle({1, 2, 3}, 70.0)};
    const Pose obj1 = followGrab(hand0, obj0, now);
    checkPose(compose(inverse(now), obj1), compose(inverse(hand0), obj0), "followGrab: stelt i handens ram");
}

Pose handAt(double x, double y, double z)
{
    return Pose{{x, y, z}, kIdentity};
}

void testTwoHands()
{
    const Pose objTilted{{0, 5, 1}, compassTurn(20.0)};
    const Pose obj{{0, 5, 1}, kIdentity};
    const TwoHandRotation modes[] = {TwoHandRotation::Full, TwoHandRotation::YawOnly,
                                     TwoHandRotation::None};

    // Bara flytt: båda händerna 1, 2, 3 m — föremålet följer, skalan består.
    for (const TwoHandRotation mode : modes) {
        const TwoHandResult r = twoHandManipulate(handAt(-0.5, 0, 1), handAt(0.5, 0, 1), objTilted, 1.5,
                                                  handAt(0.5, 2, 4), handAt(1.5, 2, 4), mode);
        checkPose(r.pose, Pose{{1, 7, 4}, objTilted.orientation}, "två händer: bara flytt");
        checkNear(r.scale, 1.5, kVecTol, "två händer: bara flytt, skala");
    }

    // 90° yaw kring mittpunkten (0, 0, 1): linjen går från österut till söderut
    // (medurs ovanifrån). Föremålet 5 m norrut hamnar 5 m österut och vrids lika.
    for (const TwoHandRotation mode : {TwoHandRotation::Full, TwoHandRotation::YawOnly}) {
        const TwoHandResult r = twoHandManipulate(handAt(-0.5, 0, 1), handAt(0.5, 0, 1), obj, 1.0,
                                                  handAt(0, 0.5, 1), handAt(0, -0.5, 1), mode);
        checkPose(r.pose, Pose{{5, 0, 1}, compassTurn(90.0)}, "två händer: 90° yaw");
        checkNear(r.scale, 1.0, kVecTol, "två händer: 90° yaw, skala");
    }
    {
        const TwoHandResult r = twoHandManipulate(handAt(-0.5, 0, 1), handAt(0.5, 0, 1), obj, 1.0,
                                                  handAt(0, 0.5, 1), handAt(0, -0.5, 1),
                                                  TwoHandRotation::None);
        checkPose(r.pose, obj, "två händer: ingen vridning");
    }

    // Dubbelt avstånd: skala 2 (gånger startskalan), föremålet skalas kring
    // greppets mittpunkt — 5 m norr om den blir 10 m norr om den.
    for (const TwoHandRotation mode : modes) {
        const TwoHandResult r = twoHandManipulate(handAt(-0.5, 0, 1), handAt(0.5, 0, 1),
                                                  Pose{{0, 5, 1}, kIdentity}, 1.5,
                                                  handAt(-1, 0, 1), handAt(1, 0, 1), mode);
        checkPose(r.pose, Pose{{0, 10, 1}, kIdentity}, "två händer: dubbelt avstånd");
        checkNear(r.scale, 3.0, kVecTol, "två händer: dubbelt avstånd, skala 1,5 × 2");
    }
    // Halva avståndet och mittpunkten flyttad: skala 0,5.
    {
        const TwoHandResult r = twoHandManipulate(handAt(-1, 0, 0), handAt(1, 0, 0), obj, 1.0,
                                                  handAt(9.5, 0, 0), handAt(10.5, 0, 0),
                                                  TwoHandRotation::Full);
        checkNear(r.scale, 0.5, kVecTol, "två händer: halva avståndet, skala");
        // Föremålet 5 m norr och 1 m upp om den gamla mittpunkten (0, 0, 0)
        // hamnar halva sträckan från den nya mittpunkten (10, 0, 0).
        checkVec(r.pose.position, {10, 2.5, 0.5}, "två händer: halva avståndet, läge");
    }

    // Lutad linje: Full vrider det minsta möjliga från österut till uppåt (−90°
    // kring y), YawOnly vrider inte alls (ingen vågrät riktning att följa).
    {
        const Pose side{{2, 0, 0}, kIdentity};
        const TwoHandResult full = twoHandManipulate(handAt(-0.5, 0, 0), handAt(0.5, 0, 0), side, 1.0,
                                                     handAt(0, 0, -0.5), handAt(0, 0, 0.5),
                                                     TwoHandRotation::Full);
        checkVec(full.pose.position, {0, 0, 2}, "Full: föremålet följer linjen uppåt");
        checkVec(transformDirection(full.pose, {0, 0, 1}), {-1, 0, 0}, "Full: upp-axeln vrids mot väster");
        checkVec(transformDirection(full.pose, {0, 1, 0}), {0, 1, 0}, "Full: ingen rullning kring linjen");
        const TwoHandResult yaw = twoHandManipulate(handAt(-0.5, 0, 0), handAt(0.5, 0, 0), side, 1.0,
                                                    handAt(0, 0, -0.5), handAt(0, 0, 0.5),
                                                    TwoHandRotation::YawOnly);
        checkPose(yaw.pose, side, "YawOnly: lodrät linje vrider inte");
    }

    // Händerna byter plats (motsatt riktning): halvt varv kring z, upp består.
    for (const TwoHandRotation mode : {TwoHandRotation::Full, TwoHandRotation::YawOnly}) {
        const TwoHandResult r = twoHandManipulate(handAt(-0.5, 0, 0), handAt(0.5, 0, 0),
                                                  Pose{{2, 0, 0}, kIdentity}, 1.0,
                                                  handAt(0.5, 0, 0), handAt(-0.5, 0, 0), mode);
        checkVec(r.pose.position, {-2, 0, 0}, "två händer: halvt varv, läge");
        checkVec(transformDirection(r.pose, {0, 0, 1}), {0, 0, 1}, "två händer: halvt varv, upp");
        checkVec(transformDirection(r.pose, {0, 1, 0}), {0, -1, 0}, "två händer: halvt varv, framåt");
    }

    // Händerna ihop nu eller vid greppet: ingen vridning och ingen skala, bara flytt.
    {
        const TwoHandResult r = twoHandManipulate(handAt(-0.5, 0, 0), handAt(0.5, 0, 0), obj, 2.0,
                                                  handAt(1, 1, 1), handAt(1, 1, 1), TwoHandRotation::Full);
        checkNear(r.scale, 2.0, kVecTol, "två händer: hopfallande händer, skala");
        checkVec(r.pose.position, {1, 6, 2}, "två händer: hopfallande händer, läge");
    }
}

void testSurfaceFrame()
{
    const double s = std::sqrt(0.5);

    // Ett plan som stiger norrut (normalen mot söder och upp) och önskad framåt
    // norrut: framåt blir uppför planet, höger öster, upp längs normalen.
    const auto tilted = surfaceFrame({1, 2, 3}, {0, -s, s}, {0, 1, 0});
    if (const Pose* f = require(tilted, "lutande plan")) {
        checkVec(f->position, {1, 2, 3}, "lutande plan: läge");
        checkVec(transformDirection(*f, {0, 0, 1}), {0, -s, s}, "lutande plan: z längs normalen");
        checkVec(transformDirection(*f, {0, 1, 0}), {0, s, s}, "lutande plan: framåt uppför planet");
        checkVec(transformDirection(*f, {1, 0, 0}), {1, 0, 0}, "lutande plan: höger öster");
    }

    // Vågrätt golv, ej enhetsnormal, framåt österut: y österut, x söderut.
    const auto floor = surfaceFrame({0, 0, 0}, {0, 0, 4}, {1, 0, 0});
    if (const Pose* f = require(floor, "golv")) {
        checkVec(transformDirection(*f, {0, 1, 0}), {1, 0, 0}, "golv: framåt österut");
        checkVec(transformDirection(*f, {1, 0, 0}), {0, -1, 0}, "golv: höger söderut");
        checkVec(transformDirection(*f, {0, 0, 1}), {0, 0, 1}, "golv: upp");
    }

    // Önskad framåt med en del längs normalen projiceras på ytan.
    const auto projected = surfaceFrame({0, 0, 0}, {0, 0, 1}, {1, 0, 7});
    if (const Pose* f = require(projected, "projicerad framåt"))
        checkVec(transformDirection(*f, {0, 1, 0}), {1, 0, 0}, "framåt projiceras på ytan");

    // Framåt parallell med normalen: världens norr.
    const auto parallel = surfaceFrame({0, 0, 0}, {0, 0, 1}, {0, 0, 5});
    if (const Pose* f = require(parallel, "framåt parallell med normalen")) {
        checkVec(transformDirection(*f, {0, 1, 0}), {0, 1, 0}, "parallell: norr");
        checkVec(transformDirection(*f, {1, 0, 0}), {1, 0, 0}, "parallell: höger öster");
    }
    // Nollvektor som framåt: norr.
    const auto none = surfaceFrame({0, 0, 0}, {0, 0, 1}, {0, 0, 0});
    if (const Pose* f = require(none, "ingen framåt"))
        checkVec(transformDirection(*f, {0, 1, 0}), {0, 1, 0}, "nollframåt: norr");

    // En vägg som vetter mot norr och framåt parallell med normalen: norr är också
    // parallell, så öster blir framåt och upp-axeln längs normalen.
    const auto wall = surfaceFrame({0, 0, 0}, {0, 1, 0}, {0, 1, 0});
    if (const Pose* f = require(wall, "vägg mot norr")) {
        checkVec(transformDirection(*f, {0, 1, 0}), {1, 0, 0}, "vägg: framåt österut");
        checkVec(transformDirection(*f, {0, 0, 1}), {0, 1, 0}, "vägg: z längs normalen");
        checkVec(transformDirection(*f, {1, 0, 0}), {0, 0, 1}, "vägg: höger uppåt");
    }

    // Ramen är alltid en riktig vridning (ortonormala, högerhänta axlar).
    const auto any = surfaceFrame({0, 0, 0}, {0.3, -0.5, 0.8}, {0.2, 0.9, -0.4});
    if (const Pose* f = require(any, "godtycklig yta"))
        checkFrame(toWorldPose(*f), "godtycklig yta");

    check(!surfaceFrame({0, 0, 0}, {0, 0, 0}, {0, 1, 0}), "surfaceFrame: nollnormal");
    check(!surfaceFrame({kNaN, 0, 0}, {0, 0, 1}, {0, 1, 0}), "surfaceFrame: NaN i punkten");
    check(!surfaceFrame({0, 0, 0}, {0, kInf, 1}, {0, 1, 0}), "surfaceFrame: ∞ i normalen");
    check(!surfaceFrame({0, 0, 0}, {0, 0, 1}, {kNaN, 1, 0}), "surfaceFrame: NaN i framåt");
}

void testSnap()
{
    checkVec(snapToGrid({1.26, -0.74, 2.5}, 0.5), {1.5, -0.5, 2.5}, "snapToGrid: steg 0,5");
    checkVec(snapToGrid({1.26, -0.74, 2.5}, 0.5, {0.1, 0.1, 0.1}), {1.1, -0.9, 2.6},
             "snapToGrid: rutnät kring en origo");
    checkVec(snapToGrid({674000.12, 6580000.37, 31.6}, 1.0, {674000, 6580000, 0}),
             {674000, 6580000, 32}, "snapToGrid: stora koordinater");
    checkVec(snapToGrid({0.3, 0.4, 0.5}, 0.0), {0.3, 0.4, 0.5}, "snapToGrid: steg 0 lämnar punkten");
    checkVec(snapToGrid({0.3, 0.4, 0.5}, -1.0), {0.3, 0.4, 0.5}, "snapToGrid: negativt steg");
    checkVec(snapToGrid({0.3, 0.4, 0.5}, kNaN), {0.3, 0.4, 0.5}, "snapToGrid: NaN-steg");

    checkNear(snapAngle(47.0, 15.0), 45.0, kAngleTol, "snapAngle 47 / 15");
    checkNear(snapAngle(53.0, 15.0), 60.0, kAngleTol, "snapAngle 53 / 15");
    checkNear(snapAngle(-47.0, 15.0), -45.0, kAngleTol, "snapAngle −47 / 15");
    checkNear(snapAngle(7.4, 5.0), 5.0, kAngleTol, "snapAngle 7,4 / 5");
    checkNear(snapAngle(7.6, 5.0), 10.0, kAngleTol, "snapAngle 7,6 / 5");
    checkNear(snapAngle(90.0, 45.0), 90.0, kAngleTol, "snapAngle på ett steg");
    checkNear(snapAngle(33.3, 0.0), 33.3, kAngleTol, "snapAngle: steg 0 lämnar vinkeln");
    checkNear(snapAngle(33.3, -5.0), 33.3, kAngleTol, "snapAngle: negativt steg");
}

void testBallisticArc()
{
    // Riktning (0, 0,6, 0,8) med farten 10: 6 enheter/s norrut och 8 uppåt;
    // tyngden 10. Toppen vid t = 0,8: y = 4,8, z = 1 + 8·0,8 − 5·0,64 = 4,2.
    const std::vector<Vec3> arc = ballisticArc({0, 0, 1}, {0, 0.6, 0.8}, 10.0, 10.0, 0.1, 2.0);
    check(arc.size() == 21, "kastbana: 21 punkter för 2 s i steg om 0,1 s");
    if (arc.size() == 21) {
        checkVec(arc.front(), {0, 0, 1}, "kastbana: första punkten är origo");
        checkVec(arc[8], {0, 4.8, 4.2}, "kastbana: toppen vid 0,8 s");
        checkVec(arc.back(), {0, 12, -3}, "kastbana: sista punkten vid 2 s");
        double top = -1e9;
        for (std::size_t i = 0; i < arc.size(); ++i) {
            const double t = 0.1 * static_cast<double>(i);
            checkVec(arc[i], {0, 6.0 * t, 1.0 + 8.0 * t - 5.0 * t * t},
                     "kastbana: punkt " + std::to_string(i));
            top = std::max(top, arc[i].z);
        }
        checkNear(top, 4.2, kVecTol, "kastbana: högsta punkten");
    }

    // Riktningen normeras (längden spelar ingen roll) och en sista punkt läggs vid
    // maxSeconds när den inte är ett helt steg: 0, 0,3, 0,6, 0,9, 1,0.
    const std::vector<Vec3> partial = ballisticArc({0, 0, 1}, {0, 3, 4}, 10.0, 10.0, 0.3, 1.0);
    check(partial.size() == 5, "kastbana: sista punkten vid maxSeconds");
    if (partial.size() == 5)
        checkVec(partial.back(), {0, 6, 4}, "kastbana: punkten vid 1 s");

    // Utan tyngd är banan en rät linje; ett längre steg än tiden ger origo och slutet.
    const std::vector<Vec3> line = ballisticArc({1, 2, 3}, {0, 0, 5}, 3.0, 0.0, 1.0, 2.0);
    check(line.size() == 3, "kastbana utan tyngd: tre punkter");
    if (line.size() == 3) {
        checkVec(line[1], {1, 2, 6}, "kastbana utan tyngd: 1 s");
        checkVec(line[2], {1, 2, 9}, "kastbana utan tyngd: 2 s");
    }
    const std::vector<Vec3> single = ballisticArc({1, 2, 3}, {1, 0, 0}, 3.0, 9.8, 0.5, 0.0);
    check(single.size() == 1, "kastbana: 0 s ger bara origo");
    const std::vector<Vec3> shortTime = ballisticArc({0, 0, 0}, {1, 0, 0}, 4.0, 0.0, 1.0, 0.5);
    check(shortTime.size() == 2, "kastbana: steget längre än tiden ger origo och slutpunkten");
    if (shortTime.size() == 2)
        checkVec(shortTime[1], {2, 0, 0}, "kastbana: slutpunkten vid 0,5 s");

    // Ogiltiga indata ger tomt.
    check(ballisticArc({0, 0, 0}, {0, 1, 0}, 1.0, 9.8, 0.0, 1.0).empty(), "kastbana: steg 0");
    check(ballisticArc({0, 0, 0}, {0, 1, 0}, 1.0, 9.8, -0.1, 1.0).empty(), "kastbana: negativt steg");
    check(ballisticArc({0, 0, 0}, {0, 1, 0}, 1.0, 9.8, kNaN, 1.0).empty(), "kastbana: NaN-steg");
    check(ballisticArc({0, 0, 0}, {0, 1, 0}, 1.0, 9.8, 0.1, -1.0).empty(), "kastbana: negativ tid");
    check(ballisticArc({0, 0, 0}, {0, 1, 0}, 1.0, 9.8, 0.1, kInf).empty(), "kastbana: oändlig tid");
    check(ballisticArc({0, 0, 0}, {0, 0, 0}, 1.0, 9.8, 0.1, 1.0).empty(), "kastbana: nollriktning");
    check(ballisticArc({0, 0, 0}, {kNaN, 1, 0}, 1.0, 9.8, 0.1, 1.0).empty(), "kastbana: NaN-riktning");
    check(ballisticArc({kInf, 0, 0}, {0, 1, 0}, 1.0, 9.8, 0.1, 1.0).empty(), "kastbana: ∞ i origo");
    check(ballisticArc({0, 0, 0}, {0, 1, 0}, kNaN, 9.8, 0.1, 1.0).empty(), "kastbana: NaN-fart");
    check(ballisticArc({0, 0, 0}, {0, 1, 0}, -1.0, 9.8, 0.1, 1.0).empty(), "kastbana: negativ fart");
    check(ballisticArc({0, 0, 0}, {0, 1, 0}, 1.0, kNaN, 0.1, 1.0).empty(), "kastbana: NaN-tyngd");
    check(ballisticArc({0, 0, 0}, {0, 1, 0}, 1.0, -9.8, 0.1, 1.0).empty(), "kastbana: negativ tyngd");
    check(ballisticArc({0, 0, 0}, {0, 1, 0}, 1.0, 9.8, 1e-9, 1e6).empty(), "kastbana: orimligt många punkter");
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
    testRejections();
    testAnchorScale();
    testPoseConversions();
    testComposeInverse();
    testInterpolate();
    testYawPitchRoll();
    testFollowGrab();
    testTwoHands();
    testSurfaceFrame();
    testSnap();
    testBallisticArc();

    if (g_failures != 0) {
        std::cerr << g_failures << " av " << g_checks << " kontroller föll.\n";
        return 1;
    }
    std::cout << "Alla " << g_checks << " kontroller gick igenom.\n";
    return 0;
}
