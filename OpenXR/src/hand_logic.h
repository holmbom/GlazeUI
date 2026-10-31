#pragma once

// HÄNDERNA SOM INGÅNG — de rena delarna, utan OpenXR- och Windows-typer så att
// de provas utan runtime:
//   * knapparnas tillstånd ur råa värden: hysteres, dödzon och flanker
//     (`ButtonTracker`), gemensamma för kontroller och händer
//   * siktet härlett ur handlederna: axeln uppskattad ur huvudet, axel →
//     pekfingrets grundled, ortonormala axlar och tidsbaserad utjämning
//   * nypets och greppets tryckvärden
//   * källvalet per hand (kontroll, hand eller ingen), avtryckarens val mellan
//     sikteaktivering och nyp, och epoken som ökar när källan byts

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <glaze_openxr/session.h>
#include "session_state.h"
#include "vector_ops.h"

namespace glaze::openxr::detail {

// ------------------------------------------------------------------ KNAPPAR
// Ett analogt värde räknas som nedtryckt över `kPressThreshold` och släppt
// under `kReleaseThreshold`; däremellan behålls föregående läge.
inline constexpr double kPressThreshold   = 0.55;
inline constexpr double kReleaseThreshold = 0.45;

// Vad actionerna gav för EN knapp i en bildruta. `has*` är actionens
// `isActive`.
struct RawButton {
    bool   hasValue = false;
    double value    = 0.0;
    bool   hasClick = false;
    bool   click    = false;
    bool   hasTouch = false;
    bool   touch    = false;
};

bool analogPressed(double value, bool wasPressed) noexcept;

// Knappens tillstånd: nedtryckt är klicket när profilen har ett, annars det
// analoga värdet med hysteres (`wasPressed` är föregående bildrutas råa
// läge). Värdet är det analoga, annars 0/1. Berörd bara där profilen känner
// beröring. En knapp som inte finns (`available == false`) är helt noll.
ButtonState resolveButton(const RawButton& raw, bool wasPressed, bool available) noexcept;

// Spaken efter en radiell dödzon: noll innanför, annars skalad så att kanten
// av dödzonen blir 0 och fullt utslag 1.
void applyDeadzone(double& x, double& y, double deadzone) noexcept;

// Knapparnas tillstånd över tid för båda händerna: löser de råa värdena,
// ger flankerna som händelser (ordning: berörd, nedtryckt, släppt, inte
// längre berörd, per knapp) och spärrar det som redan var nedtryckt eller
// berört när fokus kom tillbaka — det ger ingen flank och räknas som släppt
// tills det släpps på riktigt. Utan fokus är allt släppt.
class ButtonTracker {
public:
    void update(Hand hand, bool focused,
                const std::array<RawButton, kButtonCount>& raw,
                const std::array<bool, kButtonCount>& available,
                std::array<ButtonState, kButtonCount>& out,
                std::vector<InputEvent>& events);
    // Glöm allt (efter en fokusförlust): nästa fokuserade bildruta spärrar på nytt.
    void reset() noexcept;
    // Källan byttes: varje hållen knapp och beröring får sin släppflank, och
    // det som fortfarande hålls i nästa `update` spärras tills det släpps.
    void cut(Hand hand, std::vector<InputEvent>& events);

private:
    struct Per {
        bool rawPressed = false, rawTouched = false;       // runtimens läge
        bool pressed = false, touched = false;             // det som rapporterats
        bool blockPressed = false, blockTouched = false;   // spärrat sedan fokus kom tillbaka
    };
    std::array<std::array<Per, kButtonCount>, 2> state_{};
    std::array<bool, 2> wasFocused_{};
};

// -------------------------------------------------------------- HANDLEDERNA
// Ledernas platser i `HandSample::joints` (OpenXR:s ordning).
inline constexpr int kJointPalm              = 0;
inline constexpr int kJointWrist             = 1;
inline constexpr int kJointThumbMetacarpal   = 2;
inline constexpr int kJointThumbProximal     = 3;
inline constexpr int kJointThumbDistal       = 4;
inline constexpr int kJointThumbTip          = 5;
inline constexpr int kJointIndexMetacarpal   = 6;
inline constexpr int kJointIndexProximal     = 7;
inline constexpr int kJointIndexIntermediate = 8;
inline constexpr int kJointIndexDistal       = 9;
inline constexpr int kJointIndexTip          = 10;
inline constexpr int kJointMiddleMetacarpal  = 11;
inline constexpr int kJointMiddleProximal    = 12;
inline constexpr int kJointMiddleIntermediate = 13;
inline constexpr int kJointMiddleDistal      = 14;
inline constexpr int kJointMiddleTip         = 15;
inline constexpr int kJointRingMetacarpal    = 16;
inline constexpr int kJointRingProximal      = 17;
inline constexpr int kJointRingIntermediate  = 18;
inline constexpr int kJointRingDistal        = 19;
inline constexpr int kJointRingTip           = 20;
inline constexpr int kJointLittleMetacarpal  = 21;
inline constexpr int kJointLittleProximal    = 22;
inline constexpr int kJointLittleIntermediate = 23;
inline constexpr int kJointLittleDistal      = 24;
inline constexpr int kJointLittleTip         = 25;

// ------------------------------------------------------------ HÄRLETT SIKTE
// Axeln uppskattas under och vid sidan av huvudet (spårningsrummet, y upp):
inline constexpr double kShoulderDropMetres = 0.20;   // under ögonen
inline constexpr double kShoulderSideMetres = 0.17;   // åt handens sida, vågrätt
// Utjämningens tidskonstant (s): siktet tar 63 % av ett steg på så lång tid.
inline constexpr double kAimSmoothingSeconds = 0.05;

// Kvaternionen som vektorrotation (`q` normeras först; en nollkvaternion ger
// vektorn oförändrad).
Vec3 rotateByQuaternion(const Quaternion& q, Vec3 v) noexcept;

// Ortonormala axlar med −z längs `forward` och y så nära uppåt (0, 1, 0) som
// möjligt (rakt upp eller ned väljs x = (1, 0, 0) så nära som möjligt) som
// kvaternion i OpenXR:s konvention. Tomt för en nollvektor eller icke-ändliga tal.
std::optional<Quaternion> orientationLookingAlong(Vec3 forward) noexcept;

// Ett sikte utan orientering: ursprung och riktning (enhetsvektor).
struct RawAim {
    bool valid = false;
    Vec3 position;
    Vec3 forward;
};

// Siktet från axeln genom pekfingrets grundled (annars handleden). `head` är
// huvudets läge i spårningsrummet; `leftHand` väljer axelns sida. Ogiltigt
// utan huvud, utan spårad hand, utan någon av lederna, eller vid icke-ändliga
// tal.
RawAim deriveAim(const HandSample& hand, bool leftHand, const TrackingPose* head) noexcept;

// Utjämnar ett sikte över bildrutor: tidsbaserad (oberoende av bildfrekvensen),
// startar på det råa värdet och nollställs av `reset` och av ett ogiltigt sikte.
// dt ≤ 0 eller icke-ändligt flyttar inte siktet.
class AimSmoother {
public:
    RawAim step(const RawAim& raw, double dt) noexcept;
    void   reset() noexcept { has_ = false; }
    bool   active() const noexcept { return has_; }

private:
    bool   has_ = false;
    RawAim last_;
};

// Siktet som pose (orientering ur `orientationLookingAlong`). Tomt för ett
// ogiltigt sikte.
std::optional<TrackingPose> aimPoseOf(const RawAim& aim) noexcept;

// Nypets punkt (mitt mellan tummens och pekfingrets topp) och pekfingrets
// topp; tomma när lederna saknas.
std::optional<Vec3> pinchPointOf(const HandSample& hand) noexcept;
std::optional<Vec3> pokePointOf(const HandSample& hand) noexcept;

// ------------------------------------------------------------ TRYCKVÄRDEN
// (Värdena begränsas med `unitValue`, vector_ops.h.)
// Nypets styrka: runtimens egen när den räknat siktet (och handen inte gör en
// systemgest), annars ur lederna.
double pinchStrengthOf(const HandSample& hand) noexcept;
// Greppets styrka ur lederna.
double graspStrengthOf(const HandSample& hand) noexcept;
// Ett värde som en `ready`-action nollar: `hasReady` false (ingen action)
// släpper igenom värdet.
double gateByReady(double value, bool hasReady, bool ready) noexcept;

// ------------------------------------------------------------------ KÄLLAN
// En kontroll som tappat sin pose behåller källan så här länge innan handen
// får ta över (en blinkning i spårningen avbryter inget).
inline constexpr double kControllerHoldSeconds = 0.25;

// Vad en hand ger en bildruta för valet av källa och knappar.
struct HandInputs {
    bool   controllerProfile = false;   // en kontrollprofil är aktiv för handen
    bool   controllerPose    = false;   // kontrollens sikte är giltigt
    bool   handAim           = false;   // handen ger ett sikte
    bool   suspended         = false;   // systemgest: ingen appinteraktion
    double triggerAim        = 0.0;     // sikteaktivering 0 … 1 (efter ready)
    double triggerPinch      = 0.0;     // nyp 0 … 1 (efter ready)
    double squeeze           = 0.0;     // grepp 0 … 1 (efter ready)
};

struct HandOutput {
    Controller::Source source = Controller::Source::None;
    std::uint64_t      epoch  = 0;      // ökar vid varje källbyte
    bool               cut    = false;  // källan byttes denna bildruta: släpp hållna knappar först
    double             trigger = 0.0;   // hand-källan: avtryckarens värde (annars 0)
    double             squeeze = 0.0;   // hand-källan: greppets värde (annars 0)
};

// Källan per hand. Kontroll när en kontrollprofil är aktiv och dess sikte
// giltigt; annars hand när handen ger ett sikte och inte gör en systemgest;
// annars ingen. Byts källan ökar `epoch` och `cut` är sant den bildrutan.
//
// Avtryckaren för hand-källan är sikteaktiveringen eller nypet: den som först
// passerar tröskeln styr tills den släpps, och tills båda är under släppnivån
// ger en kvarhållen andra gest inget nytt tryck.
class HandSource {
public:
    HandOutput update(double dt, const HandInputs& in) noexcept;
    Controller::Source source() const noexcept { return source_; }

private:
    double trigger(const HandInputs& in) noexcept;

    Controller::Source source_ = Controller::Source::None;
    std::uint64_t      epoch_  = 0;
    double             lostSeconds_ = 0.0;
    // Avtryckarens val.
    bool               pressed_ = false;
    bool               viaPinch_ = false;
    bool               waitLow_  = false;
};

} // namespace glaze::openxr::detail
