// Prov av händerna som ingång (OpenXR/src/hand_logic.h) utan headset, runtime,
// fönster eller grafikenhet: de rena delarna provas direkt.
//
// Prövar:
//   A  det härledda siktet: vänster och höger hand (axelns sida), pekfingrets
//      grundled och handleden som reserv, saknade leder och saknad hand eller
//      huvud, icke-ändliga tal, ortonormala axlar, rakt upp och ned
//   B  utjämningen: första värdet orört, tidsbaserad (två halva steg = ett helt),
//      dt = 0 flyttar inte, återställning och ogiltigt sikte nollställer
//   C  tryckvärdena: begränsning, runtimens nyp mot ledernas, ready-grinden
//   D  avtryckaren: hysteres 0,55 / 0,45 och exakt ett tryck och ett släpp,
//      sikteaktivering mot nyp (den som styr håller i sig, en kvarhållen andra
//      gest ger inget nytt tryck)
//   E  källvalet: kontroll går före hand, fristen för en kontroll som tappat
//      sin pose, systemgest och förlorad spårning, epoken, släppflankerna vid
//      ett källbyte och spärren av en gest som redan hålls
//
// Varje kontroll skriver vad som föll; programmet returnerar 1 om något föll.

#include "hand_logic.h"
#include "vector_ops.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

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

bool nearTo(double a, double b, double eps = 1e-9)
{
    return std::abs(a - b) <= eps;
}

bool nearTo(Vec3 a, Vec3 b, double eps = 1e-9)
{
    return nearTo(a.x, b.x, eps) && nearTo(a.y, b.y, eps) && nearTo(a.z, b.z, eps);
}

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

TrackingPose headAt(double x, double y, double z)
{
    return TrackingPose{Vec3{x, y, z}, Quaternion{}};   // blickar längs −z, y upp
}

// En spårad hand med handleden och pekfingrets grundled på givna platser.
HandSample handWith(Vec3 wrist, Vec3 indexProximal)
{
    HandSample hand;
    hand.active = true;
    auto set = [&](int joint, Vec3 position) {
        JointSample& j = hand.joints[static_cast<std::size_t>(joint)];
        j.valid = true;
        j.pose.position = position;
    };
    set(kJointWrist, wrist);
    set(kJointIndexProximal, indexProximal);
    return hand;
}

// ------------------------------------------------------------ A. SIKTET
void derivedAimCases()
{
    const TrackingPose head = headAt(0.0, 1.6, 0.0);
    // Högra axeln: (0,17, 1,4, 0); den vänstra (−0,17, 1,4, 0).
    const Vec3 rightShoulder{0.17, 1.4, 0.0};
    const Vec3 leftShoulder{-0.17, 1.4, 0.0};
    const Vec3 knuckle{0.0, 1.3, -0.4};

    {   // A1: höger hand: riktningen från högra axeln genom grundleden
        const RawAim aim = deriveAim(handWith({0.0, 1.25, -0.35}, knuckle), false, &head);
        CHECK(aim.valid, "A1 giltigt");
        CHECK(nearTo(aim.position, knuckle), "A1 ursprunget är pekfingrets grundled");
        CHECK(nearTo(aim.forward, vnormalized(vsub(knuckle, rightShoulder))), "A1 riktningen ur högra axeln");
        CHECK(aim.forward.x < 0.0 && aim.forward.z < 0.0, "A1 mot mitten och framåt");
        CHECK(nearTo(vlength(aim.forward), 1.0), "A1 enhetsvektor");
    }
    {   // A2: vänster hand: axeln på andra sidan
        const RawAim aim = deriveAim(handWith({0.0, 1.25, -0.35}, knuckle), true, &head);
        CHECK(aim.valid && nearTo(aim.forward, vnormalized(vsub(knuckle, leftShoulder))),
              "A2 riktningen ur vänstra axeln");
        CHECK(aim.forward.x > 0.0, "A2 mot mitten");
        const RawAim right = deriveAim(handWith({0.0, 1.25, -0.35}, knuckle), false, &head);
        CHECK(nearTo(aim.forward, Vec3{-right.forward.x, right.forward.y, right.forward.z}),
              "A2 samma punkt är spegelbilden mellan händerna");
    }
    {   // A3: axeln följer huvudets vridning (huvudet vridet 90° åt vänster: −z → −x)
        // Rotation 90° kring y: z-axeln blir x, så huvudets höger (x) blir −z.
        const double s = std::sqrt(0.5);
        TrackingPose turned = headAt(0.0, 1.6, 0.0);
        turned.orientation = Quaternion{0.0, s, 0.0, s};
        const Vec3 right = rotateByQuaternion(turned.orientation, Vec3{1.0, 0.0, 0.0});
        CHECK(nearTo(right, Vec3{0.0, 0.0, -1.0}, 1e-9), "A3 provets rotation: x blir −z");
        const RawAim aim = deriveAim(handWith({0.0, 1.25, -0.35}, knuckle), false, &turned);
        const Vec3 shoulder{0.0, 1.4, -0.17};
        CHECK(aim.valid && nearTo(aim.forward, vnormalized(vsub(knuckle, shoulder))), "A3 axeln på huvudets höger sida");
    }
    {   // A4: pekfingrets grundled saknas: handleden
        HandSample hand = handWith({0.05, 1.25, -0.35}, knuckle);
        hand.joints[kJointIndexProximal].valid = false;
        const RawAim aim = deriveAim(hand, false, &head);
        CHECK(aim.valid && nearTo(aim.position, Vec3{0.05, 1.25, -0.35}), "A4 handleden som reserv");
    }
    {   // A5: inga leder, ingen hand, inget huvud
        HandSample none = handWith({0.0, 1.25, -0.35}, knuckle);
        none.joints[kJointIndexProximal].valid = false;
        none.joints[kJointWrist].valid = false;
        CHECK(!deriveAim(none, false, &head).valid, "A5 inga leder: ogiltigt");
        HandSample inactive = handWith({0.0, 1.25, -0.35}, knuckle);
        inactive.active = false;
        CHECK(!deriveAim(inactive, false, &head).valid, "A5 hand som inte spåras: ogiltigt");
        CHECK(!deriveAim(handWith({0.0, 1.25, -0.35}, knuckle), false, nullptr).valid, "A5 utan huvud: ogiltigt");
        CHECK(!deriveAim(HandSample{}, false, &head).valid, "A5 tom hand: ogiltigt");
    }
    {   // A6: icke-ändliga tal
        HandSample nan = handWith({0.0, 1.25, -0.35}, Vec3{kNaN, 1.3, -0.4});
        const RawAim fallback = deriveAim(nan, false, &head);
        CHECK(fallback.valid && nearTo(fallback.position, Vec3{0.0, 1.25, -0.35}),
              "A6 NaN i grundleden: handleden används");
        nan.joints[kJointWrist].pose.position.y = kNaN;
        CHECK(!deriveAim(nan, false, &head).valid, "A6 NaN i båda lederna: ogiltigt");
        TrackingPose badHead = head;
        badHead.position.x = kNaN;
        CHECK(!deriveAim(handWith({0.0, 1.25, -0.35}, knuckle), false, &badHead).valid, "A6 NaN i huvudet: ogiltigt");
        badHead = head;
        badHead.orientation.w = std::numeric_limits<double>::infinity();
        CHECK(!deriveAim(handWith({0.0, 1.25, -0.35}, knuckle), false, &badHead).valid, "A6 oändligt i huvudet: ogiltigt");
        // Handen på axeln: ingen riktning.
        CHECK(!deriveAim(handWith(rightShoulder, rightShoulder), false, &head).valid, "A6 handen på axeln: ogiltigt");
    }
    {   // A7: orienteringen — −z längs riktningen, ortonormala axlar, y upp
        const RawAim aim = deriveAim(handWith({0.0, 1.25, -0.35}, knuckle), false, &head);
        const auto pose = aimPoseOf(aim);
        CHECK(pose.has_value(), "A7 pose");
        if (pose) {
            const Quaternion& q = pose->orientation;
            CHECK(nearTo(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1.0, 1e-9), "A7 enhetskvaternion");
            const Vec3 forward = rotateByQuaternion(q, Vec3{0.0, 0.0, -1.0});
            const Vec3 right   = rotateByQuaternion(q, Vec3{1.0, 0.0, 0.0});
            const Vec3 up      = rotateByQuaternion(q, Vec3{0.0, 1.0, 0.0});
            CHECK(nearTo(forward, aim.forward, 1e-9), "A7 −z längs siktet");
            CHECK(nearTo(vdot(right, forward), 0.0, 1e-9) && nearTo(vdot(up, forward), 0.0, 1e-9) &&
                      nearTo(vdot(right, up), 0.0, 1e-9),
                  "A7 ortogonala axlar");
            CHECK(nearTo(vlength(right), 1.0, 1e-9) && nearTo(vlength(up), 1.0, 1e-9), "A7 enhetsaxlar");
            CHECK(up.y > 0.9, "A7 y pekar uppåt");
            CHECK(nearTo(vcross(right, up), vmul(forward, -1.0), 1e-9), "A7 högerhänt");
        }
        CHECK(!aimPoseOf(RawAim{}).has_value(), "A7 ogiltigt sikte ger ingen pose");
        // Rakt upp och rakt ned, och en kraftigt lutad riktning.
        for (const Vec3& dir : {Vec3{0.0, 1.0, 0.0}, Vec3{0.0, -1.0, 0.0}, Vec3{1.0, 0.0, 0.0},
                                Vec3{0.3, -0.8, 0.5}}) {
            const auto q = orientationLookingAlong(dir);
            CHECK(q.has_value(), "A7 riktning ger orientering");
            if (q) {
                CHECK(nearTo(rotateByQuaternion(*q, Vec3{0.0, 0.0, -1.0}), vnormalized(dir), 1e-9),
                      "A7 −z längs riktningen (också rakt upp och ned)");
                CHECK(nearTo(q->x * q->x + q->y * q->y + q->z * q->z + q->w * q->w, 1.0, 1e-9),
                      "A7 enhetskvaternion (alla grenar)");
            }
        }
        CHECK(!orientationLookingAlong(Vec3{}).has_value(), "A7 nollvektor: ingen orientering");
        CHECK(!orientationLookingAlong(Vec3{kNaN, 0.0, 1.0}).has_value(), "A7 NaN: ingen orientering");
    }
}

// ----------------------------------------------------------- B. UTJÄMNINGEN
void smoothingCases()
{
    RawAim a;
    a.valid = true;
    a.position = Vec3{0.0, 0.0, 0.0};
    a.forward = Vec3{0.0, 0.0, -1.0};
    RawAim b = a;
    b.position = Vec3{1.0, 0.0, 0.0};
    b.forward = vnormalized(Vec3{1.0, 0.0, -1.0});

    {   // B1: första värdet orört; sedan ett steg mot det nya
        AimSmoother smoother;
        const RawAim first = smoother.step(a, 0.016);
        CHECK(first.valid && nearTo(first.position, a.position) && nearTo(first.forward, a.forward), "B1 första värdet orört");
        CHECK(smoother.active(), "B1 aktiv");
        const RawAim next = smoother.step(b, 0.016);
        CHECK(next.position.x > 0.0 && next.position.x < 1.0, "B1 delvis mot det nya läget");
        CHECK(next.forward.x > 0.0 && next.forward.x < b.forward.x, "B1 delvis mot den nya riktningen");
        CHECK(nearTo(vlength(next.forward), 1.0, 1e-9), "B1 enhetsriktning");
    }
    {   // B2: tidsbaserad: två halva steg är ett helt
        AimSmoother whole, halves;
        whole.step(a, 0.016);
        halves.step(a, 0.016);
        const RawAim one = whole.step(b, 0.016);
        halves.step(b, 0.008);
        const RawAim two = halves.step(b, 0.008);
        CHECK(nearTo(one.position.x, two.position.x, 1e-9), "B2 samma läge oavsett bildfrekvens");
    }
    {   // B3: dt = 0, negativt och NaN flyttar inte
        AimSmoother smoother;
        smoother.step(a, 0.016);
        for (const double dt : {0.0, -0.1, kNaN}) {
            const RawAim same = smoother.step(b, dt);
            CHECK(nearTo(same.position, a.position) && nearTo(same.forward, a.forward), "B3 dt utan tid flyttar inte");
        }
    }
    {   // B4: konvergerar
        AimSmoother smoother;
        smoother.step(a, 0.016);
        RawAim out;
        for (int i = 0; i < 200; ++i)
            out = smoother.step(b, 0.016);
        CHECK(nearTo(out.position, b.position, 1e-6) && nearTo(out.forward, b.forward, 1e-6), "B4 når det råa värdet");
    }
    {   // B5: återställning och ogiltigt sikte nollställer
        AimSmoother smoother;
        smoother.step(a, 0.016);
        smoother.reset();
        CHECK(!smoother.active(), "B5 reset: inte aktiv");
        const RawAim fresh = smoother.step(b, 0.016);
        CHECK(nearTo(fresh.position, b.position) && nearTo(fresh.forward, b.forward), "B5 efter reset: det råa värdet");
        smoother.step(a, 0.016);
        const RawAim lost = smoother.step(RawAim{}, 0.016);
        CHECK(!lost.valid && !smoother.active(), "B5 ogiltigt sikte: ogiltigt och nollställt");
        const RawAim back = smoother.step(b, 0.016);
        CHECK(nearTo(back.position, b.position), "B5 efter förlusten börjar siktet om");
        RawAim bad = a;
        bad.position.x = kNaN;
        CHECK(!smoother.step(bad, 0.016).valid, "B5 NaN: ogiltigt");
    }
    {   // B6: motsatta riktningar
        AimSmoother smoother;
        RawAim backwards = a;
        backwards.forward = Vec3{0.0, 0.0, 1.0};
        smoother.step(a, 0.016);
        // alpha 0,5 av två motsatta riktningar tar ut varandra: det nya gäller.
        const RawAim out = smoother.step(backwards, 0.05 * std::log(2.0));
        CHECK(out.valid && nearTo(vlength(out.forward), 1.0, 1e-9), "B6 giltig enhetsriktning vid motsatta riktningar");
    }
}

// ------------------------------------------------------------ C. VÄRDEN
void valueCases()
{
    CHECK(unitValue(-0.5) == 0.0 && unitValue(1.5) == 1.0 && nearTo(unitValue(0.3), 0.3), "C1 begränsat till 0 … 1");
    CHECK(unitValue(kNaN) == 0.0 && unitValue(std::numeric_limits<double>::infinity()) == 0.0,
          "C1 icke-ändligt blir 0");

    HandSample hand;
    hand.pinch = 0.2;
    hand.grab = 0.7;
    hand.aimPinch = 0.9;
    CHECK(nearTo(pinchStrengthOf(hand), 0.2), "C2 ledernas nyp utan runtimens sikte");
    hand.aimValid = true;
    CHECK(nearTo(pinchStrengthOf(hand), 0.9), "C2 runtimens nyp när siktet gäller");
    hand.systemGesture = true;
    CHECK(nearTo(pinchStrengthOf(hand), 0.2), "C2 under en systemgest används inte runtimens nyp");
    CHECK(nearTo(graspStrengthOf(hand), 0.7), "C2 greppet ur lederna");
    hand.grab = kNaN;
    CHECK(graspStrengthOf(hand) == 0.0, "C2 NaN-grepp blir 0");

    CHECK(gateByReady(0.8, true, false) == 0.0, "C3 inte redo: noll");
    CHECK(nearTo(gateByReady(0.8, true, true), 0.8), "C3 redo: värdet");
    CHECK(nearTo(gateByReady(0.8, false, false), 0.8), "C3 ingen ready-action: värdet");

    CHECK(analogPressed(0.55, false) && !analogPressed(0.5499, false), "C4 trycktröskeln 0,55");
    CHECK(!analogPressed(0.45, true) && analogPressed(0.4501, true), "C4 släpptröskeln 0,45");
}

// --------------------------------------------------------------- RIGGEN
// En hand: källvalet, knappspåraren och det den ger, bildruta för bildruta.
struct Rig {
    HandSource source;
    ButtonTracker tracker;
    HandOutput out;
    std::array<ButtonState, kButtonCount> buttons{};
    std::vector<InputEvent> events;   // alla flanker hittills
    std::vector<InputEvent> frameEvents;
    double controllerTrigger = 0.0;   // kontrollens avtryckare när källan är en kontroll

    void frame(const HandInputs& in, double dt = 0.016, bool focused = true)
    {
        frameEvents.clear();
        out = source.update(dt, in);
        if (out.cut)
            tracker.cut(Hand::Right, frameEvents);
        std::array<RawButton, kButtonCount> raw{};
        std::array<bool, kButtonCount> available{};
        const std::size_t trigger = static_cast<std::size_t>(Button::Trigger);
        const std::size_t squeeze = static_cast<std::size_t>(Button::Squeeze);
        if (out.source == Controller::Source::Controller) {
            available[trigger] = true;
            raw[trigger].hasValue = true;
            raw[trigger].value = controllerTrigger;
        } else if (out.source == Controller::Source::Hand) {
            available[trigger] = available[squeeze] = true;
            raw[trigger].hasValue = true;
            raw[trigger].value = out.trigger;
            raw[squeeze].hasValue = true;
            raw[squeeze].value = out.squeeze;
        }
        tracker.update(Hand::Right, focused, raw, available, buttons, frameEvents);
        events.insert(events.end(), frameEvents.begin(), frameEvents.end());
    }

    int count(InputKind kind, Button button = Button::Trigger) const
    {
        int n = 0;
        for (const InputEvent& e : events)
            if (e.kind == kind && e.button == button)
                ++n;
        return n;
    }
    bool pressed(Button button = Button::Trigger) const
    {
        return buttons[static_cast<std::size_t>(button)].pressed;
    }
};

HandInputs handOnly(double pinch = 0.0, double aim = 0.0, double squeeze = 0.0)
{
    HandInputs in;
    in.handAim = true;
    in.triggerPinch = pinch;
    in.triggerAim = aim;
    in.squeeze = squeeze;
    return in;
}

// ------------------------------------------------------- D. AVTRYCKAREN
void triggerCases()
{
    {   // D1: hysteres: exakt ett tryck och ett släpp
        Rig rig;
        for (const double v : {0.0, 0.3, 0.5, 0.54})
            rig.frame(handOnly(v));
        CHECK(!rig.pressed() && rig.count(InputKind::Pressed) == 0, "D1 under 0,55: inget tryck");
        rig.frame(handOnly(0.56));
        CHECK(rig.pressed() && rig.count(InputKind::Pressed) == 1, "D1 0,56: tryck");
        for (const double v : {0.6, 0.5, 0.46, 0.5, 0.9})
            rig.frame(handOnly(v));
        CHECK(rig.pressed() && rig.count(InputKind::Pressed) == 1 && rig.count(InputKind::Released) == 0,
              "D1 däremellan behålls trycket, inga extra flanker");
        rig.frame(handOnly(0.44));
        CHECK(!rig.pressed() && rig.count(InputKind::Released) == 1, "D1 0,44: släpp");
        for (const double v : {0.5, 0.54, 0.3})
            rig.frame(handOnly(v));
        CHECK(rig.count(InputKind::Pressed) == 1 && rig.count(InputKind::Released) == 1,
              "D1 skakning mellan trösklarna ger inga fler flanker");
        rig.frame(handOnly(0.8));
        CHECK(rig.count(InputKind::Pressed) == 2, "D1 nytt tryck efter ett släpp");
        CHECK(rig.out.source == Controller::Source::Hand && rig.out.epoch == 1, "D1 en källa, en epok");
    }
    {   // D2: greppet är knappen Squeeze med samma hysteres
        Rig rig;
        rig.frame(handOnly(0.0, 0.0, 0.2));
        rig.frame(handOnly(0.0, 0.0, 0.7));
        CHECK(rig.pressed(Button::Squeeze) && rig.count(InputKind::Pressed, Button::Squeeze) == 1, "D2 grepp: tryck");
        CHECK(!rig.pressed(Button::Trigger), "D2 greppet trycker inte avtryckaren");
        rig.frame(handOnly(0.0, 0.0, 0.5));
        CHECK(rig.pressed(Button::Squeeze), "D2 0,5 behåller greppet");
        rig.frame(handOnly(0.0, 0.0, 0.1));
        CHECK(!rig.pressed(Button::Squeeze) && rig.count(InputKind::Released, Button::Squeeze) == 1, "D2 släpp");
        CHECK(rig.buttons[static_cast<std::size_t>(Button::Trigger)].available &&
                  rig.buttons[static_cast<std::size_t>(Button::Squeeze)].available,
              "D2 knapparna finns för en hand");
        CHECK(!rig.buttons[static_cast<std::size_t>(Button::Primary)].available, "D2 inga andra knappar");
    }
    {   // D3: sikteaktivering styr; nypet som också stiger ger ingen ny gest
        Rig rig;
        rig.frame(handOnly(0.0, 0.0));
        rig.frame(handOnly(0.0, 0.7));
        CHECK(rig.pressed() && rig.count(InputKind::Pressed) == 1, "D3 sikteaktivering: tryck");
        rig.frame(handOnly(0.95, 0.7));
        CHECK(rig.pressed() && rig.count(InputKind::Pressed) == 1, "D3 nyp stiger: samma tryck");
        rig.frame(handOnly(0.95, 0.2));
        CHECK(!rig.pressed() && rig.count(InputKind::Released) == 1, "D3 den styrande släpper: släpp");
        rig.frame(handOnly(0.95, 0.1));
        rig.frame(handOnly(0.9, 0.0));
        CHECK(rig.count(InputKind::Pressed) == 1, "D3 kvarhållet nyp ger inget nytt tryck");
        rig.frame(handOnly(0.2, 0.0));
        rig.frame(handOnly(0.2, 0.8));
        CHECK(rig.count(InputKind::Pressed) == 2, "D3 efter att båda släppts går ett nytt tryck");
    }
    {   // D4: nyp styr; sikteaktiveringen som stiger ändrar inget
        Rig rig;
        rig.frame(handOnly(0.0, 0.0));
        rig.frame(handOnly(0.8, 0.1));
        CHECK(rig.pressed() && rig.count(InputKind::Pressed) == 1, "D4 nyp: tryck");
        rig.frame(handOnly(0.8, 0.9));
        CHECK(rig.pressed() && rig.count(InputKind::Pressed) == 1, "D4 sikteaktivering stiger: samma tryck");
        rig.frame(handOnly(0.1, 0.9));
        CHECK(!rig.pressed() && rig.count(InputKind::Released) == 1, "D4 nypet släpper: släpp");
        rig.frame(handOnly(0.0, 0.0));
        CHECK(rig.count(InputKind::Pressed) == 1 && rig.count(InputKind::Released) == 1, "D4 exakt ett tryck och ett släpp");
    }
    {   // D5: ingen ready (värdet 0) ger inget tryck och släpper ett pågående
        Rig rig;
        rig.frame(handOnly());
        rig.frame(handOnly(0.9));
        CHECK(rig.pressed(), "D5 nyp håller");
        rig.frame(handOnly(gateByReady(0.9, true, false)));
        CHECK(!rig.pressed() && rig.count(InputKind::Released) == 1, "D5 inte redo: släpp");
        rig.frame(handOnly(gateByReady(0.9, true, false)));
        CHECK(rig.count(InputKind::Pressed) == 1, "D5 inte redo: inget nytt tryck");
    }
    {   // D6: utan fokus är allt släppt
        Rig rig;
        rig.frame(handOnly());
        rig.frame(handOnly(0.9));
        rig.frame(handOnly(0.9), 0.016, false);
        CHECK(!rig.pressed() && rig.count(InputKind::Released) == 1, "D6 utan fokus: släpp");
        rig.frame(handOnly(0.9), 0.016, true);
        CHECK(!rig.pressed() && rig.count(InputKind::Pressed) == 1, "D6 fokus tillbaka med nypet kvar: spärrat");
        rig.frame(handOnly(0.1));
        rig.frame(handOnly(0.9));
        CHECK(rig.count(InputKind::Pressed) == 2, "D6 efter ett släpp går ett nytt tryck");
    }
}

// ------------------------------------------------------------ E. KÄLLAN
void sourceCases()
{
    HandInputs controller;
    controller.controllerProfile = true;
    controller.controllerPose = true;

    {   // E1: ingen ingång: ingen källa, epok 0
        Rig rig;
        rig.frame(HandInputs{});
        CHECK(rig.out.source == Controller::Source::None && rig.out.epoch == 0 && !rig.out.cut, "E1 ingen källa");
    }
    {   // E2: hand blir källan: epok 1, ingenting hålls att släppa
        Rig rig;
        rig.frame(HandInputs{});
        rig.frame(handOnly());
        CHECK(rig.out.source == Controller::Source::Hand && rig.out.epoch == 1 && rig.out.cut, "E2 handen tar över");
        CHECK(rig.events.empty(), "E2 inga flanker utan hållna knappar");
        rig.frame(handOnly());
        CHECK(rig.out.epoch == 1 && !rig.out.cut, "E2 epoken står stilla");
    }
    {   // E3: kontroll går före hand
        Rig rig;
        HandInputs both = controller;
        both.handAim = true;
        rig.frame(both);
        CHECK(rig.out.source == Controller::Source::Controller, "E3 kontrollen går före handen");
        rig.controllerTrigger = 0.9;
        rig.frame(both);
        CHECK(rig.pressed() && rig.count(InputKind::Pressed) == 1, "E3 kontrollens avtryckare");
    }
    {   // E4: kontroll → hand med hållen avtryckare: släppflank först, sedan spärr
        Rig rig;
        rig.frame(controller);
        rig.controllerTrigger = 0.9;
        rig.frame(controller);
        CHECK(rig.pressed() && rig.count(InputKind::Pressed) == 1, "E4 kontrollen trycker");
        const std::uint64_t before = rig.out.epoch;
        HandInputs hand = handOnly(0.9);   // handen nyper redan
        rig.frame(hand);
        CHECK(rig.out.source == Controller::Source::Hand && rig.out.epoch == before + 1 && rig.out.cut,
              "E4 källbyte: ny epok");
        CHECK(rig.count(InputKind::Released) == 1, "E4 exakt en släppflank");
        CHECK(rig.frameEvents.size() == 1 && rig.frameEvents[0].kind == InputKind::Released,
              "E4 släppflanken är bildrutans enda");
        CHECK(!rig.pressed(), "E4 släppt efter bytet");
        rig.frame(hand);
        rig.frame(hand);
        CHECK(!rig.pressed() && rig.count(InputKind::Pressed) == 1, "E4 nypet som redan hölls är spärrat");
        rig.frame(handOnly(0.0));
        rig.frame(handOnly(0.8));
        CHECK(rig.pressed() && rig.count(InputKind::Pressed) == 2 && rig.count(InputKind::Released) == 1,
              "E4 efter ett släpp går ett nytt tryck");
    }
    {   // E5: hand → kontroll med hållen avtryckare
        Rig rig;
        rig.frame(handOnly());
        rig.frame(handOnly(0.9));
        CHECK(rig.pressed(), "E5 handen trycker");
        rig.controllerTrigger = 0.9;
        const std::uint64_t before = rig.out.epoch;
        rig.frame(controller);
        CHECK(rig.out.source == Controller::Source::Controller && rig.out.epoch == before + 1, "E5 kontrollen tar över");
        CHECK(rig.count(InputKind::Released) == 1 && !rig.pressed(), "E5 handens tryck släpps");
        rig.frame(controller);
        CHECK(!rig.pressed() && rig.count(InputKind::Pressed) == 1, "E5 kontrollens redan hållna avtryckare spärrad");
        rig.controllerTrigger = 0.0;
        rig.frame(controller);
        rig.controllerTrigger = 0.9;
        rig.frame(controller);
        CHECK(rig.pressed() && rig.count(InputKind::Pressed) == 2, "E5 nytt tryck efter släpp");
    }
    {   // E6: greppet släpps också vid bytet
        Rig rig;
        rig.frame(handOnly());
        rig.frame(handOnly(0.0, 0.0, 0.9));
        CHECK(rig.pressed(Button::Squeeze), "E6 grepp");
        rig.frame(controller);
        CHECK(rig.count(InputKind::Released, Button::Squeeze) == 1 && !rig.pressed(Button::Squeeze), "E6 greppet släpps");
    }
    {   // E7: en kontroll som tappat sin pose behåller källan en frist
        Rig rig;
        HandInputs lost = controller;
        lost.controllerPose = false;
        lost.handAim = true;
        rig.frame(controller);
        const std::uint64_t epoch = rig.out.epoch;
        for (int i = 0; i < 10; ++i)   // 0,16 s
            rig.frame(lost);
        CHECK(rig.out.source == Controller::Source::Controller && rig.out.epoch == epoch, "E7 inom fristen: kontrollen kvar");
        rig.frame(controller);   // posen tillbaka: fristen börjar om
        for (int i = 0; i < 10; ++i)
            rig.frame(lost);
        CHECK(rig.out.source == Controller::Source::Controller && rig.out.epoch == epoch, "E7 posen tillbaka nollställer fristen");
        for (int i = 0; i < 20; ++i)   // 0,32 s
            rig.frame(lost);
        CHECK(rig.out.source == Controller::Source::Hand && rig.out.epoch == epoch + 1, "E7 efter fristen: handen");
    }
    {   // E8: en profil som lämnat kontrollen byter direkt; dt utan tid räknar inte
        Rig rig;
        rig.frame(controller);
        HandInputs gone = handOnly();
        rig.frame(gone);
        CHECK(rig.out.source == Controller::Source::Hand, "E8 profilen borta: handen direkt");
        Rig stuck;
        HandInputs lost = controller;
        lost.controllerPose = false;
        stuck.frame(controller);
        for (const double dt : {0.0, -1.0, kNaN})
            for (int i = 0; i < 50; ++i)
                stuck.frame(lost, dt);
        CHECK(stuck.out.source == Controller::Source::Controller, "E8 dt utan tid flyttar inte fristen");
    }
    {   // E9: systemgest och förlorad spårning avbryter hand-källan
        Rig rig;
        rig.frame(handOnly());
        rig.frame(handOnly(0.9));
        CHECK(rig.pressed(), "E9 handen trycker");
        HandInputs gesture = handOnly(0.9);
        gesture.suspended = true;
        const std::uint64_t before = rig.out.epoch;
        rig.frame(gesture);
        CHECK(rig.out.source == Controller::Source::None && rig.out.epoch == before + 1, "E9 systemgest: ingen källa, ny epok");
        CHECK(rig.count(InputKind::Released) == 1 && !rig.pressed(), "E9 trycket släpps");
        CHECK(!rig.buttons[static_cast<std::size_t>(Button::Trigger)].available, "E9 inga knappar utan källa");
        rig.frame(gesture);
        CHECK(rig.count(InputKind::Pressed) == 1, "E9 under gesten inget tryck");
        rig.frame(handOnly(0.9));   // gesten över, nypet kvar
        CHECK(rig.out.source == Controller::Source::Hand && rig.out.epoch == before + 2, "E9 handen tillbaka: ny epok");
        CHECK(!rig.pressed() && rig.count(InputKind::Pressed) == 1, "E9 nypet som hölls är spärrat");

        Rig lostTracking;
        lostTracking.frame(handOnly());
        lostTracking.frame(handOnly(0.9));
        const std::uint64_t e = lostTracking.out.epoch;
        lostTracking.frame(HandInputs{});
        CHECK(lostTracking.out.source == Controller::Source::None && lostTracking.out.epoch == e + 1,
              "E9 förlorad spårning: ingen källa, ny epok");
        CHECK(lostTracking.count(InputKind::Released) == 1, "E9 förlorad spårning släpper trycket");
        lostTracking.frame(HandInputs{});
        CHECK(lostTracking.out.epoch == e + 1 && !lostTracking.out.cut, "E9 epoken står stilla utan källa");
    }
    {   // E10: utan källa kommer inga värden ur handen
        HandSource source;
        HandInputs in = handOnly(0.9, 0.9, 0.9);
        in.suspended = true;
        const HandOutput out = source.update(0.016, in);
        CHECK(out.source == Controller::Source::None && out.trigger == 0.0 && out.squeeze == 0.0, "E10 inga värden utan källa");
    }
}

} // namespace

int main()
{
    derivedAimCases();
    smoothingCases();
    valueCases();
    triggerCases();
    sourceCases();
    std::cerr << g_checks << " kontroller, " << g_failures << " föll\n";
    return g_failures == 0 ? 0 : 1;
}
