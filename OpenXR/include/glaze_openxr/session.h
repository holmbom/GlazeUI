#pragma once

// Sidopaketet OpenXR:s session: visar en glaze::Canvas-scen i ett PC-VR-headset genom
// OpenXR, med huvudspårning, handkontroller, förflyttning och en stråle som
// plockar i scenen.
//
// TRÅDARNA: alla publika anrop och alla callbacks sker i UI-tråden. Sessionen
// väntar på headsetets bildrutor i en egen tråd, som aldrig rör Canvas; varje
// bildruta ritas i UI-tråden på ritytans fönsters egen D3D11-enhet.
//
// LIVSTIDEN: Canvas ägs av appen och lever längre än sessionen. Sessionen
// förstörs före Canvas; destruktorn stoppar och väntar in sin tråd.
//
// SCENEN: en världsenhet per meter med z upp. Startläget tas ur ritytans
// kamera — en Camera3D (UpAxis::Z) eller kartkameran — så att huvudet börjar
// där skärmens öga står, vänt dit det tittar.

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glaze/glaze.h>
#include <glaze_openxr/math.h>

namespace glaze::openxr {

enum class Hand { Left, Right };

enum class State {
    Stopped,    // ingen session; allt avvecklat
    Starting,   // loader, runtime, enhet och session skapas
    Waiting,    // sessionen finns men runtimen visar den inte än
    Running,    // bilderna går till headsetet
    Stopping,   // sessionen avslutas
    Failed      // start eller drift misslyckades; allt avvecklat
};

// ------------------------------------------------------------------ INDATA
// Knapparna i den gemensamma formen för alla kontroller. En kontroll som
// saknar en knapp svarar `available == false` för den.
//   Trigger    avtryckaren (pekfingret)        Squeeze   greppet (långfingret)
//   Primary    A/X — den nedre på handens sida  Secondary B/Y — den övre
//   Thumbstick spaken (klick och beröring)      Menu      menyknappen
//   Thumbrest  tumstödet (bara beröring)        Trackpad  styrplattan (klick, beröring)
enum class Button : std::uint8_t {
    Trigger, Squeeze, Primary, Secondary, Thumbstick, Menu, Thumbrest, Trackpad,
    Count
};
inline constexpr std::size_t kButtonCount = static_cast<std::size_t>(Button::Count);

struct ButtonState {
    bool   available = false;   // kontrollens profil har knappen
    bool   pressed   = false;   // nedtryckt (analoga knappar med hysteres)
    bool   touched   = false;   // berörd, där profilen kan känna det
    double value     = 0.0;     // 0 … 1 för analoga (Trigger, Squeeze), annars 0/1
};

enum class InputKind : std::uint8_t { Pressed, Released, Touched, Untouched };

// En knapphändelse — en flank, i den ordning runtimen gav dem.
struct InputEvent {
    Hand      hand   = Hand::Right;
    Button    button = Button::Trigger;
    InputKind kind   = InputKind::Pressed;
};

// En spårad pose i Glaze-världen. `pose` saknas när runtimen inte har någon.
struct TrackedPose {
    std::optional<WorldPose> pose;
    bool positionTracked    = false;
    bool orientationTracked = false;
};

struct Controller {
    TrackedPose grip;               // handen
    TrackedPose aim;                // strålen: origo och forward
    TrackedPose palm;               // handflatan, där runtimen har den
    // Hastigheten i världen (enheter/s respektive rad/s kring axeln) — för
    // kast och tröghet. `velocityValid` false när runtimen inte har den.
    Vec3 linearVelocity;
    Vec3 angularVelocity;
    bool velocityValid = false;
    std::array<ButtonState, kButtonCount> buttons{};
    double stickX = 0.0, stickY = 0.0;   // −1 … 1 efter dödzonen
    double padX   = 0.0, padY   = 0.0;   // styrplattan, −1 … 1
    // Den interaktionsprofil runtimen valt för handen (OpenXR-sökvägen, t.ex.
    // "/interaction_profiles/khr/simple_controller"); tom utan kontroll.
    std::string profile;
    // Strålen ur `aim` i världen och vad den träffar den här bildrutan,
    // närmast först (`Options::hover`). Tom när strålen saknas.
    std::optional<glaze::Ray3>    ray;
    std::vector<glaze::HitResult> hover;
    // Teleporteringens båge medan den visas, och var den landar.
    std::vector<Vec3>                arc;
    std::optional<glaze::HitResult>  arcHit;
};

// HANDSPÅRNING (`Options::handTracking`, när runtimen har den): OpenXR:s 26
// leder per hand i specens ordning (handflata, handled, tumme … lillfinger).
struct HandJoint {
    WorldPose pose;
    double    radius = 0.0;   // ledens radie i världsenheter
    bool      valid  = false;
};
struct TrackedHand {
    bool active = false;                    // handen spåras just nu
    std::array<HandJoint, 26> joints{};
    double pinch = 0.0;   // 0 … 1, tumme mot pekfinger
    double grab  = 0.0;   // 0 … 1, knuten hand
};

// -------------------------------------------------------------- RÖRELSEN
// Rummet som spårningen räknas i: Local (sittande, origo där headsetet
// startade), LocalFloor (samma, origo på golvet) och Stage (rummet med
// golvet och användarens gränser). En som runtimen saknar ger Local.
enum class ReferenceSpace : std::uint8_t { Local, LocalFloor, Stage };

// Den inbyggda förflyttningen; allt går att stänga av och göras i appen med
// `Session::setAnchor`. Farter i meter i rummet — `Anchor::scale` gör dem
// till världsenheter.
enum class MoveMode : std::uint8_t { None, Walk, Fly };   // vänster spak
enum class TurnMode : std::uint8_t { None, Smooth, Snap }; // höger spak i sidled
struct Locomotion {
    MoveMode move                 = MoveMode::Walk;   // Walk: i planet; Fly: längs blicken, upp och ned
    TurnMode turn                 = TurnMode::Smooth;
    bool     moveAlongHand        = false;   // riktningen ur vänster hands sikte i stället för huvudets
    double   moveMetresPerSecond  = 1.5;
    double   turnDegreesPerSecond = 90.0;
    double   snapDegrees          = 30.0;
    // TELEPORTERING: höger spak framåt visar vägen (`Controller::arc`) —
    // en båge eller en rak stråle — och släppt flyttas användaren dit den
    // landar på en yta. Egen teleportering i appen: `Session::teleportTo`.
    enum class Path : std::uint8_t { Arc, Straight };
    bool     teleport             = false;
    Path     teleportPath         = Path::Arc;
    double   teleportSpeed        = 8.0;     // m/s ut ur handen (bågen)
    double   teleportGravity      = 9.81;    // m/s² (bågen)
    // GREPPA VÄRLDEN: greppknappen på en hand drar världen; på båda händerna
    // vrids den kring vertikalen och skalas (`Anchor::scale`).
    bool     grabWorld            = false;
    double   minScale             = 0.01;
    double   maxScale             = 1000.0;
};

// Strålarnas förval: det man inte ser (bakom den första ytan) träffas inte.
inline glaze::HitOptions defaultHoverOptions()
{
    glaze::HitOptions options;
    options.occlusion = true;
    return options;
}

// Bilderna har alltid runtimens rekommenderade mått per öga.
struct Options {
    int            msaaSamples    = 4;        // 4, 2 eller 1 — kantutjämningen i ögonen
    ReferenceSpace referenceSpace = ReferenceSpace::LocalFloor;
    Locomotion     locomotion;
    double         stickDeadzone  = 0.2;
    // Strålarna mot scenen varje bildruta (`Controller::hover`).
    bool              hover = true;
    glaze::HitOptions hoverOptions    = defaultHoverOptions();
    double            rayLengthMetres = 20.0;
    // RESERVMARKEN: en vågrät yta på höjden `groundZ` (världsenheter) som
    // strålarna och teleporteringen träffar där scenen inte har någon yta —
    // en platt karta utan terräng, ett tomt rum. Träffen har lager −1.
    std::optional<double> groundZ;
    bool   handTracking = false;   // handlederna i `FrameInfo::hands`
    float  refreshRate  = 0.0f;    // Hz; 0 = runtimens förval
    double nearMetres   = 0.05;
    double farMetres    = 1000.0;
};

// En bildruta, innan den ritas. `[0]` är vänster, `[1]` höger.
struct FrameInfo {
    std::uint64_t sequence     = 0;
    double        deltaSeconds = 0.0;   // högst 0,1
    bool          focused      = false; // headsetet har fokus (indata aktiv)
    Anchor        anchor;               // efter den inbyggda förflyttningen
    TrackedPose   head;
    std::array<TrackedPose, 2> eyes;
    std::array<Controller, 2>  controllers;
    std::array<TrackedHand, 2> hands;
    std::vector<InputEvent>    events;  // bildrutans knapphändelser
};

struct Status {
    State       state = State::Stopped;
    std::string message;
};

// Callbacks i UI-tråden. Argumenten gäller under anropet. En callback får
// anropa sessionens metoder (också requestStop) men inte förstöra sessionen,
// öppna en modal pump eller kasta.
struct Callbacks {
    std::function<void(const Status&)> onStatus;
    // Före båda ögonens ritning — här flyttar appen sina föremål och
    // uppdaterar t.ex. lagret med kontrollerna och strålarna.
    std::function<void(const FrameInfo&)> onFrame;
    // En gång per knapphändelse, före `onFrame` i samma bildruta.
    std::function<void(const InputEvent&, const FrameInfo&)> onInput;
};

class Session final {
public:
    Session(glaze::Canvas& canvas, Callbacks callbacks);
    ~Session();

    Session(const Session&)            = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&)                 = delete;
    Session& operator=(Session&&)      = delete;

    // Startar asynkront. Ett anrop medan en session redan startar eller går
    // gör ingenting. Förankringen tas ur Canvas aktuella kamera.
    void start(const Options& options = {});
    // Begär stopp; blockerar inte. Upprepade anrop gör ingenting.
    void requestStop();
    State state() const noexcept;

    // FÖRANKRINGEN — var rummet står i världen. `setAnchor` gäller från nästa
    // bildruta; den inbyggda förflyttningen fortsätter därifrån. `recenter`
    // ställer huvudet där det står nu, vänt mot `yawDegrees`, med samma skala.
    Anchor anchor() const;
    void   setAnchor(const Anchor& anchor);
    void   recenter(double yawDegrees);
    // Flyttar användaren så att FÖTTERNA står på `feet` (huvudet på sin höjd
    // över golvet, skalan oförändrad), vänd mot `yawDegrees` om den anges —
    // direkt, i nästa bildruta. Appens egen teleportering: en knapp och en
    // träffpunkt räcker. false utan körande session.
    bool   teleportTo(Vec3 feet, std::optional<double> yawDegrees = {});
    // Byter den inbyggda förflyttningen under en körande session.
    void   setLocomotion(const Locomotion& locomotion);

    // VIBRATION i en kontroll: styrka 0 … 1, längd i sekunder (0 = runtimens
    // kortaste), frekvens i Hz (0 = runtimens). false utan session eller fokus.
    bool vibrate(Hand hand, double amplitude, double seconds, double frequencyHz = 0.0);
    void stopVibration(Hand hand);

    // Uppdateringsfrekvenserna headsetet erbjuder och byte till en av dem;
    // tomt respektive false när runtimen inte kan byta.
    std::vector<float> refreshRates() const;
    bool               setRefreshRate(float hz);

    // En egen stråle mot scenen, räknad mot headsetets vy (skärmkonstanta
    // föremål och LOD som i headsetet) — för blicken, en egen pekare eller
    // ett kast. Tom utan körande session.
    std::vector<glaze::HitResult> castRay(const glaze::Ray3& ray,
                                          const glaze::HitOptions& options = {}) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// En pose (och en skala) som `glaze::Placement` — att flytta ett föremål i
// ritytan är att byta dess Placement, så meshen byggs aldrig om.
glaze::Placement toPlacement(const Pose& pose, double scale = 1.0);
Pose             fromPlacement(const glaze::Placement& placement);

} // namespace glaze::openxr
