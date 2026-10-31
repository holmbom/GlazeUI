// Prov av komfortens rena delar utan headset, runtime, fönster eller
// grafikenhet (comfort_logic.h): vinjettens styrka ur den utförda rörelsen,
// öppningens form i ögats asymmetriska synfält och svarttoningens förlopp.
//
// Prövar:
//   M  rörelsen: m/s, °/s och |ln skala|/s ur förankringens ändring; huvudets
//      egen rörelse och en vridning kring huvudet ger ingen förflyttning; dt = 0,
//      NaN och en ogiltig pose ger noll; ett steg anmäls som steg
//   V  vinjetten: vila, gång, vridning, skalning (hela och halva farten), det
//      största bidraget (inte summan), monotoni i farten, attack och release i
//      sekunder, stegets puls, dt = 0 och NaN, ett avstängt mått
//   F  öppningen: bredaste hörnet vid styrka 0, minFieldDegrees vid 1, det
//      asymmetriska synfältet, kantens minsta bredd, ogiltigt synfält
//   B  svarttoningen: ut → en svart bildruta → flytt → in, flytten först efter
//      en inlämnad svart bildruta, en ny begäran ersätter en väntande, fokusförlust
//      och stopp avbryter, tidsgränsen, aldrig fast i svart
//
// Varje kontroll skriver vad som föll; programmet returnerar 1 om något föll.

#include "comfort_logic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <string>

namespace {

using namespace glaze::openxr;
using namespace glaze::openxr::detail;

int g_failures = 0;
int g_checks   = 0;

void check(bool ok, const std::string& what, int line = 0)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cerr << "FALL (rad " << line << "): " << what << "\n";
    }
}
#define CHECK(cond, what) check((cond), (what), __LINE__)

constexpr double kPi  = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
const double     kNaN = std::numeric_limits<double>::quiet_NaN();

bool nearTo(double a, double b, double eps = 1e-9)
{
    return std::abs(a - b) <= eps;
}

TrackingPose headAt(double x, double y, double z, double yawDegrees = 0.0)
{
    TrackingPose head;
    head.position = Vec3{x, y, z};
    const double half = yawDegrees * kPi / 360.0;
    head.orientation = Quaternion{0.0, std::sin(half), 0.0, std::cos(half)};
    return head;
}

MotionSample walking(double metresPerSecond, double dt = 0.01)
{
    MotionSample m;
    m.deltaSeconds = dt;
    m.metresPerSecond = metresPerSecond;
    return m;
}

MotionSample turning(double degreesPerSecond, double dt = 0.01)
{
    MotionSample m;
    m.deltaSeconds = dt;
    m.degreesPerSecond = degreesPerSecond;
    return m;
}

MotionSample resting(double dt = 0.01)
{
    MotionSample m;
    m.deltaSeconds = dt;
    return m;
}

// Kör `frames` bildrutor med samma rörelse och ger styrkan efter dem.
double settle(Vignette& v, const MotionSample& m, const Comfort& c, int frames)
{
    for (int i = 0; i < frames; ++i) v.update(m, c);
    return v.strength();
}

// ------------------------------------------------------------------ RÖRELSEN
void motionCases()
{
    const TrackingPose head = headAt(0.3, 1.7, -0.2, 20.0);
    Anchor before;
    before.origin = Vec3{5.0, 6.0, 0.0};

    {   // M1: förflyttning i meter i rummet — delad med skalan
        Anchor after = before;
        after.origin.x += 0.1;
        MotionSample m = measureMotion(before, after, head, 0.1, false);
        CHECK(nearTo(m.metresPerSecond, 1.0, 1e-9) && nearTo(m.degreesPerSecond, 0.0) &&
                  nearTo(m.scaleRatePerSecond, 0.0),
              "M1 0,1 m på 0,1 s är 1 m/s");
        Anchor big = before;
        big.scale = 2.0;
        Anchor bigAfter = big;
        bigAfter.origin.y += 0.2;
        m = measureMotion(big, bigAfter, head, 0.1, false);
        CHECK(nearTo(m.metresPerSecond, 1.0, 1e-9), "M1 skala 2: 0,2 världsenheter är 0,1 m i rummet");
    }
    {   // M2: vridning i grader per sekund och som steg
        Anchor after = before;
        after.yawDegrees = 9.0;
        MotionSample m = measureMotion(before, after, head, 0.1, false);
        CHECK(nearTo(m.degreesPerSecond, 90.0, 1e-9) && m.snapDegrees == 0.0, "M2 9° på 0,1 s är 90 °/s");
        m = measureMotion(before, after, head, 0.1, true);
        CHECK(nearTo(m.snapDegrees, 9.0, 1e-9) && m.degreesPerSecond == 0.0, "M2 ett steg är snapDegrees, ingen hastighet");
        Anchor wrapped = before;
        wrapped.yawDegrees = 170.0;
        Anchor over = wrapped;
        over.yawDegrees = -170.0;
        m = measureMotion(wrapped, over, head, 1.0, false);
        CHECK(nearTo(m.degreesPerSecond, 20.0, 1e-9), "M2 vridningen över ±180° är 20°, inte 340°");
    }
    {   // M3: skalning som |ln skala| per sekund, åt båda hållen lika
        Anchor up = before;
        up.scale = std::exp(0.1);
        MotionSample m = measureMotion(before, up, head, 0.1, false);
        CHECK(nearTo(m.scaleRatePerSecond, 1.0, 1e-9), "M3 e^0,1 på 0,1 s är 1 per sekund");
        Anchor down = before;
        down.scale = std::exp(-0.1);
        m = measureMotion(before, down, head, 0.1, false);
        CHECK(nearTo(m.scaleRatePerSecond, 1.0, 1e-9), "M3 förminskning räknas lika");
    }
    {   // M4: ingen förflyttning av en vridning kring huvudet, och ingen av huvudets egen rörelse
        Anchor around = before;
        around.yawDegrees = 30.0;
        // Huvudet står kvar i världen: origo flyttas så att huvudets världsläge är detsamma.
        const auto from = toWorldPose(before, head);
        Anchor turned = around;
        turned.origin = Vec3{0.0, 0.0, 0.0};
        const auto atZero = toWorldPose(turned, head);
        CHECK(from && atZero, "M4 posen går att räkna");
        if (from && atZero) {
            turned.origin = Vec3{from->position.x - atZero->position.x, from->position.y - atZero->position.y,
                                 from->position.z - atZero->position.z};
            const MotionSample m = measureMotion(before, turned, head, 0.1, false);
            CHECK(nearTo(m.metresPerSecond, 0.0, 1e-9) && nearTo(m.degreesPerSecond, 300.0, 1e-6),
                  "M4 vridning kring huvudet: ingen förflyttning, 300 °/s");
        }
        // Samma förankring, huvudet var som helst: noll.
        const MotionSample still = measureMotion(before, before, headAt(40.0, 3.0, -17.0, 77.0), 0.1, false);
        CHECK(still.metresPerSecond == 0.0 && still.degreesPerSecond == 0.0 && still.scaleRatePerSecond == 0.0,
              "M4 huvudets egen rörelse ger noll (förankringen oförändrad)");
    }
    {   // M5: dt = 0, NaN och en ogiltig pose
        Anchor after = before;
        after.origin.x += 1.0;
        MotionSample m = measureMotion(before, after, head, 0.0, false);
        CHECK(m.deltaSeconds == 0.0 && m.metresPerSecond == 0.0, "M5 dt = 0 ger noll");
        m = measureMotion(before, after, head, kNaN, false);
        CHECK(m.metresPerSecond == 0.0, "M5 dt NaN ger noll");
        Anchor bad = after;
        bad.origin.x = kNaN;
        m = measureMotion(before, bad, head, 0.1, false);
        CHECK(m.metresPerSecond == 0.0, "M5 förankring med NaN ger noll");
        bad = after;
        bad.scale = 0.0;
        m = measureMotion(before, bad, head, 0.1, false);
        CHECK(m.metresPerSecond == 0.0 && m.scaleRatePerSecond == 0.0, "M5 skala 0 ger noll");
        TrackingPose nanHead = head;
        nanHead.position.y = kNaN;
        m = measureMotion(before, after, nanHead, 0.1, false);
        CHECK(m.metresPerSecond == 0.0, "M5 huvud med NaN ger noll");
    }
}

// ------------------------------------------------------------------- VINJETT
void vignetteCases()
{
    Comfort c;   // full 1,5 m/s, 90 °/s, 1 per s; attack 0,08 s, release 0,2 s

    {   // V1: vila
        Vignette v;
        CHECK(settle(v, resting(), c, 100) == 0.0, "V1 i vila är styrkan 0");
    }
    {   // V2: gång, full och halv fart
        Vignette v;
        CHECK(nearTo(settle(v, walking(1.5), c, 200), 1.0), "V2 full gångfart ger 1");
        Vignette half;
        CHECK(nearTo(settle(half, walking(0.75), c, 200), 0.5), "V2 halv fart ger 0,5");
        Vignette over;
        CHECK(nearTo(settle(over, walking(10.0), c, 200), 1.0), "V2 över full fart klampas till 1");
    }
    {   // V3: vridning och skalning
        Vignette turn;
        CHECK(nearTo(settle(turn, turning(90.0), c, 200), 1.0), "V3 full vridning ger 1");
        Vignette halfTurn;
        CHECK(nearTo(settle(halfTurn, turning(45.0), c, 200), 0.5), "V3 halv vridning ger 0,5");
        MotionSample scale;
        scale.deltaSeconds = 0.01;
        scale.scaleRatePerSecond = 0.25;
        Vignette scaling;
        CHECK(nearTo(settle(scaling, scale, c, 200), 0.25), "V3 skalning 0,25 per sekund ger 0,25");
    }
    {   // V4: det största bidraget, inte summan
        MotionSample both;
        both.deltaSeconds = 0.01;
        both.metresPerSecond = 0.75;     // 0,5
        both.degreesPerSecond = 63.0;    // 0,7
        Vignette v;
        CHECK(nearTo(settle(v, both, c, 200), 0.7), "V4 största bidraget (0,7), inte summan");
    }
    {   // V5: monotoni i farten
        double previous = -1.0;
        bool monotone = true;
        bool bounded = true;
        for (int i = 0; i <= 40; ++i) {
            Vignette v;
            const double s = settle(v, walking(0.1 * i), c, 300);
            if (s < previous) monotone = false;
            if (s < 0.0 || s > 1.0) bounded = false;
            previous = s;
        }
        CHECK(monotone, "V5 större fart ger aldrig mindre styrka");
        CHECK(bounded, "V5 styrkan är alltid 0 … 1");
    }
    {   // V6: attack och release i sekunder (linjärt)
        Vignette v;
        for (int i = 0; i < 4; ++i) v.update(walking(1.5), c);
        CHECK(nearTo(v.strength(), 0.5), "V6 attack 0,08 s: halvvägs efter 0,04 s");
        for (int i = 0; i < 4; ++i) v.update(walking(1.5), c);
        CHECK(nearTo(v.strength(), 1.0), "V6 attack: uppe efter 0,08 s");
        for (int i = 0; i < 10; ++i) v.update(resting(), c);
        CHECK(nearTo(v.strength(), 0.5), "V6 release 0,2 s: halvvägs efter 0,1 s");
        for (int i = 0; i < 10; ++i) v.update(resting(), c);
        CHECK(nearTo(v.strength(), 0.0), "V6 release: nere efter 0,2 s");
        // Bildrutetakten ändrar inte tiden.
        Vignette slow;
        slow.update(walking(1.5, 0.04), c);
        CHECK(nearTo(slow.strength(), 0.5), "V6 en bildruta på 0,04 s är lika mycket som fyra på 0,01 s");
        // Ingen tid: omedelbart.
        Comfort instant = c;
        instant.attackSeconds = 0.0;
        instant.releaseSeconds = 0.0;
        Vignette now;
        now.update(walking(1.5), instant);
        CHECK(nearTo(now.strength(), 1.0), "V6 attack 0 är omedelbar");
        now.update(resting(), instant);
        CHECK(now.strength() == 0.0, "V6 release 0 är omedelbar");
    }
    {   // V7: en stegvis vridning är en kort puls
        Vignette v;
        MotionSample snap;
        snap.deltaSeconds = 0.01;
        snap.snapDegrees = 30.0;   // 30/90 = 1/3
        v.update(snap, c);
        double peak = v.strength();
        for (int i = 0; i < 10; ++i) {
            v.update(resting(), c);
            peak = std::max(peak, v.strength());
        }
        CHECK(nearTo(peak, 1.0 / 3.0, 1e-9), "V7 pulsen når 30°/90 °/s");
        CHECK(nearTo(v.strength(), 1.0 / 3.0, 1e-9), "V7 pulsen hålls en kort stund");
        for (int i = 0; i < 100; ++i) v.update(resting(), c);
        CHECK(v.strength() == 0.0, "V7 pulsen är över och släpper");
    }
    {   // V8: dt = 0 och NaN ändrar ingenting
        Vignette v;
        settle(v, walking(0.75), c, 200);
        const double before = v.strength();
        MotionSample zero = walking(1.5, 0.0);
        v.update(zero, c);
        CHECK(v.strength() == before, "V8 dt = 0 ändrar ingenting");
        MotionSample nanDt = walking(1.5, kNaN);
        v.update(nanDt, c);
        CHECK(v.strength() == before, "V8 dt NaN ändrar ingenting");
        MotionSample nanSpeed = walking(kNaN);
        v.update(nanSpeed, c);
        CHECK(std::isfinite(v.strength()), "V8 fart NaN ger ett ändligt tal");
        Comfort bad = c;
        bad.fullMoveMetresPerSecond = kNaN;
        bad.fullTurnDegreesPerSecond = 0.0;
        bad.fullScaleRatePerSecond = -1.0;
        bad.attackSeconds = kNaN;
        bad.releaseSeconds = kNaN;
        Vignette other;
        const double s = settle(other, walking(2.0), bad, 10);
        CHECK(std::isfinite(s) && s >= 0.0 && s <= 1.0, "V8 ogiltiga mått ger ett ändligt tal 0 … 1");
    }
    {   // V9: huvudets egen rörelse ger noll hela vägen
        Vignette v;
        Anchor anchor;
        anchor.origin = Vec3{10.0, 10.0, 0.0};
        for (int i = 0; i < 100; ++i) {
            const TrackingPose head = headAt(0.01 * i, 1.7 + 0.001 * i, -0.02 * i, 3.0 * i);
            v.update(measureMotion(anchor, anchor, head, 0.01, false), c);
        }
        CHECK(v.strength() == 0.0, "V9 huvudet rör sig, förankringen står: vinjetten är 0");
    }
    {   // V10: reset
        Vignette v;
        settle(v, walking(1.5), c, 100);
        v.reset();
        CHECK(v.strength() == 0.0, "V10 reset nollställer");
    }
}

// ------------------------------------------------------------------- ÖPPNING
void shapeCases()
{
    Comfort c;   // minField 60°, kant 15°
    Fov square;
    square.left = -45.0 * kDeg;
    square.right = 45.0 * kDeg;
    square.up = 45.0 * kDeg;
    square.down = -45.0 * kDeg;
    const double corner = std::atan(std::sqrt(2.0));

    const VignetteShape open = vignetteShape(square, 0.0, c);
    CHECK(nearTo(open.innerRadians, corner, 1e-9), "F1 styrka 0: öppningen är det bredaste hörnet");
    const VignetteShape closed = vignetteShape(square, 1.0, c);
    CHECK(nearTo(closed.innerRadians, 30.0 * kDeg, 1e-9), "F1 styrka 1: halva minFieldDegrees");
    CHECK(nearTo(closed.featherRadians, 15.0 * kDeg, 1e-9), "F1 kantens bredd är featherDegrees");
    const VignetteShape mid = vignetteShape(square, 0.5, c);
    CHECK(nearTo(mid.innerRadians, 0.5 * (corner + 30.0 * kDeg), 1e-9), "F1 halv styrka: mitt emellan");

    double previous = 10.0;
    bool decreasing = true;
    for (int i = 0; i <= 20; ++i) {
        const double inner = vignetteShape(square, i / 20.0, c).innerRadians;
        if (inner > previous) decreasing = false;
        previous = inner;
    }
    CHECK(decreasing, "F2 öppningen krymper med styrkan");
    CHECK(vignetteShape(square, 2.0, c).innerRadians == closed.innerRadians &&
              vignetteShape(square, -1.0, c).innerRadians == open.innerRadians,
          "F2 styrkan klampas till 0 … 1");
    CHECK(vignetteShape(square, kNaN, c).innerRadians == open.innerRadians, "F2 NaN-styrka räknas som 0");

    // Det asymmetriska synfältet: det bredaste hörnet av de fyra.
    Fov wide;
    wide.left = -50.0 * kDeg;
    wide.right = 30.0 * kDeg;
    wide.up = 35.0 * kDeg;
    wide.down = -45.0 * kDeg;
    const double across = std::tan(50.0 * kDeg);
    const double along = std::tan(45.0 * kDeg);
    CHECK(nearTo(vignetteShape(wide, 0.0, c).innerRadians, std::atan(std::hypot(across, along)), 1e-9),
          "F3 asymmetriskt synfält: det bredaste hörnet");

    // Ett synfält smalare än minFieldDegrees mörknar inte.
    Fov narrow;
    narrow.left = -20.0 * kDeg;
    narrow.right = 20.0 * kDeg;
    narrow.up = 20.0 * kDeg;
    narrow.down = -20.0 * kDeg;
    const double narrowCorner = std::atan(std::hypot(std::tan(20.0 * kDeg), std::tan(20.0 * kDeg)));
    CHECK(nearTo(vignetteShape(narrow, 1.0, c).innerRadians, narrowCorner, 1e-9),
          "F4 öppningen blir aldrig bredare än synfältet");

    // Kanten har en minsta bredd; ogiltiga värden ger ingen vinjett.
    Comfort hard = c;
    hard.featherDegrees = 0.0;
    CHECK(vignetteShape(square, 1.0, hard).featherRadians > 0.0, "F5 kanten har en minsta bredd");
    hard.featherDegrees = kNaN;
    hard.minFieldDegrees = kNaN;
    const VignetteShape bad = vignetteShape(square, 1.0, hard);
    CHECK(std::isfinite(bad.innerRadians) && std::isfinite(bad.featherRadians) && bad.featherRadians > 0.0,
          "F5 NaN i måtten ger ändliga värden");
    Fov invalid = square;
    invalid.left = kNaN;
    CHECK(vignetteShape(invalid, 1.0, c).innerRadians >= kPi, "F5 ogiltigt synfält ger ingen vinjett");
    invalid = square;
    invalid.right = 90.0 * kDeg;
    CHECK(vignetteShape(invalid, 1.0, c).innerRadians >= kPi, "F5 synfält på 90° ger ingen vinjett");
}

// ------------------------------------------------------------- SVARTTONING
Landing landingAt(double x, double y)
{
    Landing l;
    l.feet = Vec3{x, y, 0.0};
    return l;
}

void fadeCases()
{
    Comfort c;   // ut 0,1 s, in 0,15 s

    {   // B1: ut → en svart bildruta → flytt → in
        TeleportFade fade;
        CHECK(!fade.active(), "B1 i vila är den inte aktiv");
        fade.request(landingAt(2.0, 3.0));
        CHECK(fade.phase() == TeleportFade::Phase::FadingOut, "B1 en begäran startar uttoningen");
        TeleportFade::Step s = fade.advance(0.05, true, true, c);
        CHECK(nearTo(s.alpha, 0.5) && !s.move && fade.phase() == TeleportFade::Phase::FadingOut,
              "B1 halvvägs ut: alpha 0,5, ingen flytt");
        s = fade.advance(0.05, true, true, c);
        CHECK(nearTo(s.alpha, 1.0) && !s.move && fade.phase() == TeleportFade::Phase::Black,
              "B1 helt svart: alpha 1, flytten väntar på den inlämnade bildrutan");
        s = fade.advance(0.01, true, true, c);
        CHECK(nearTo(s.alpha, 1.0) && !s.move, "B1 ingen flytt förrän den svarta bildrutan lämnats in");
        fade.blackSubmitted();
        s = fade.advance(0.01, true, true, c);
        CHECK(s.move.has_value() && nearTo(s.move->feet.x, 2.0) && nearTo(s.move->feet.y, 3.0),
              "B1 flytten sker i bildrutan efter den svarta");
        CHECK(fade.phase() == TeleportFade::Phase::FadingIn && nearTo(s.alpha, 1.0 - 0.01 / 0.15),
              "B1 intoningen börjar");
        int moves = 0;
        for (int i = 0; i < 40; ++i) {
            s = fade.advance(0.01, true, true, c);
            if (s.move) ++moves;
        }
        CHECK(moves == 0 && s.alpha == 0.0 && !fade.active(), "B1 flytten sker en gång och toningen slutar i vila");
    }
    {   // B2: en ny begäran ersätter en väntande; under intoningen tonar den ut igen
        TeleportFade fade;
        fade.request(landingAt(1.0, 1.0));
        fade.advance(0.05, true, true, c);
        fade.request(landingAt(9.0, 9.0));
        fade.advance(0.05, true, true, c);
        fade.blackSubmitted();
        TeleportFade::Step s = fade.advance(0.01, true, true, c);
        CHECK(s.move && nearTo(s.move->feet.x, 9.0), "B2 den senaste begäran gäller");
        fade.advance(0.03, true, true, c);
        const double mid = fade.alpha();
        CHECK(mid > 0.0 && mid < 1.0, "B2 mitt i intoningen");
        fade.request(landingAt(4.0, 4.0));
        CHECK(fade.phase() == TeleportFade::Phase::FadingOut && fade.alpha() == mid, "B2 tonar ut igen från där den står");
        for (int i = 0; i < 20; ++i) fade.advance(0.01, true, true, c);
        CHECK(fade.phase() == TeleportFade::Phase::Black, "B2 svart igen");
        fade.blackSubmitted();
        s = fade.advance(0.01, true, true, c);
        CHECK(s.move && nearTo(s.move->feet.x, 4.0), "B2 den nya flytten utförs");
    }
    {   // B3: fokusförlust och stopp avbryter utan flytt, i varje fas
        for (int stage = 0; stage < 4; ++stage) {
            TeleportFade fade;
            fade.request(landingAt(2.0, 2.0));
            if (stage >= 1) fade.advance(0.1, true, true, c);          // svart
            if (stage >= 2) { fade.blackSubmitted(); fade.advance(0.01, true, true, c); }   // intoning
            if (stage >= 3) fade.advance(0.05, true, true, c);
            const TeleportFade::Step s = fade.advance(0.01, false, true, c);
            CHECK(s.alpha == 0.0 && !s.move && !fade.active(), "B3 utan fokus: avbrutet, ljust och ingen flytt");
            const TeleportFade::Step after = fade.advance(0.01, true, true, c);
            CHECK(after.alpha == 0.0 && !after.move, "B3 fokus tillbaka: inget sker av sig självt");
        }
        TeleportFade fade;
        fade.request(landingAt(2.0, 2.0));
        fade.advance(0.1, true, true, c);
        fade.cancel();
        CHECK(!fade.active() && fade.alpha() == 0.0, "B3 cancel tonar bort direkt");
    }
    {   // B4: flush ger flytten som väntar; svarttoningen avstängd
        TeleportFade fade;
        fade.request(landingAt(7.0, 8.0));
        fade.advance(0.05, true, true, c);
        const std::optional<Landing> pending = fade.flush();
        CHECK(pending && nearTo(pending->feet.x, 7.0) && !fade.active(), "B4 flush ger den väntande flytten och avslutar");
        CHECK(!fade.flush().has_value(), "B4 inget mer att ge");
    }
    {   // B5: tidsgräns — aldrig fast i svart
        TeleportFade fade;
        fade.request(landingAt(1.0, 2.0));
        fade.advance(0.1, true, true, c);
        bool moved = false;
        for (int i = 0; i < 100 && !moved; ++i) moved = fade.advance(0.01, true, true, c).move.has_value();
        CHECK(moved, "B5 utan inlämnad svart bildruta flyttar den ändå efter en tidsgräns");
        for (int i = 0; i < 100; ++i) fade.advance(0.01, true, true, c);
        CHECK(!fade.active(), "B5 och tonar in");

        // Huvudet utan pose skjuter flytten upp men tappar den inte.
        TeleportFade waiting;
        waiting.request(landingAt(5.0, 5.0));
        waiting.advance(0.1, true, true, c);
        waiting.blackSubmitted();
        TeleportFade::Step s = waiting.advance(0.01, true, false, c);
        CHECK(!s.move && waiting.phase() == TeleportFade::Phase::Black, "B5 utan huvudpose väntar flytten");
        s = waiting.advance(0.01, true, true, c);
        CHECK(s.move.has_value(), "B5 med huvudpose utförs den");

        // Huvudet utan pose längre än tidsgränsen: flytten avbryts och bilden tonar in.
        TeleportFade headless;
        headless.request(landingAt(5.0, 5.0));
        headless.advance(0.1, true, true, c);
        headless.blackSubmitted();
        bool movedHeadless = false;
        for (int i = 0; i < 100; ++i) movedHeadless |= headless.advance(0.01, true, false, c).move.has_value();
        CHECK(!movedHeadless && !headless.active(), "B5 utan huvudpose över tidsgränsen: ingen flytt, tonar in");
    }
    {   // B6: dt = 0, NaN och ogiltiga tider
        TeleportFade fade;
        fade.request(landingAt(1.0, 1.0));
        TeleportFade::Step s = fade.advance(0.0, true, true, c);
        CHECK(s.alpha == 0.0 && fade.phase() == TeleportFade::Phase::FadingOut, "B6 dt = 0: ingen framgång");
        s = fade.advance(kNaN, true, true, c);
        CHECK(s.alpha == 0.0 && std::isfinite(s.alpha), "B6 dt NaN: ingen framgång");
        Comfort bad = c;
        bad.fadeOutSeconds = 0.0;
        bad.fadeInSeconds = kNaN;
        s = fade.advance(0.01, true, true, bad);
        CHECK(s.alpha == 1.0 && fade.phase() == TeleportFade::Phase::Black, "B6 uttoning 0: svart direkt");
        fade.blackSubmitted();
        s = fade.advance(0.01, true, true, bad);
        CHECK(s.move.has_value() && s.alpha == 0.0 && !fade.active(), "B6 intoning NaN: ljus direkt");
    }
    {   // B7: över varje blandning av inlämning och huvudpose hamnar den i vila
        for (int mask = 0; mask < 8; ++mask) {
            TeleportFade fade;
            fade.request(landingAt(1.0, 1.0));
            for (int i = 0; i < 1000; ++i) {
                const bool canMove = (mask & 1) != 0 || i > 50;
                const TeleportFade::Step s = fade.advance(0.011, true, canMove, c);
                if ((mask & 2) != 0 && s.alpha >= 1.0) fade.blackSubmitted();
            }
            CHECK(!fade.active() && fade.alpha() == 0.0, "B7 aldrig fast i svart");
        }
    }
}

} // namespace

int main()
{
    motionCases();
    vignetteCases();
    shapeCases();
    fadeCases();
    if (g_failures != 0) {
        std::cerr << g_failures << " av " << g_checks << " kontroller föll\n";
        return 1;
    }
    std::printf("comfort_gate: alla %d kontroller gröna\n", g_checks);
    return 0;
}
