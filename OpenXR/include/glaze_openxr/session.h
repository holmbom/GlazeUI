#pragma once

// Sidopaketet OpenXR:s session: visar en glaze::Canvas-scen i ett PC-VR-headset genom
// OpenXR, med huvudspårning, handkontroller, förflyttning och en stråle som
// plockar i scenen.
//
// TRÅDARNA: alla publika anrop och alla callbacks sker i UI-tråden. Sessionen
// har en egen tråd för OpenXR och sin D3D11-enhet; den rör aldrig Canvas.
//
// LIVSTIDEN: Canvas ägs av appen och lever längre än sessionen. Sessionen
// förstörs före Canvas; destruktorn stoppar och väntar in sin tråd.
//
// SCENEN: Canvas ska ha en Camera3D med UpAxis::Z och en världsenhet per
// meter. Annars går sessionen till Failed med skälet i Status::message.

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

struct Options {
    double resolutionScale      = 0.5;    // × runtimens rekommenderade bildmått
    double moveMetresPerSecond  = 1.0;    // vänster spak
    double turnDegreesPerSecond = 60.0;   // höger spak, i sidled
    double stickDeadzone        = 0.2;
    double rayLengthMetres      = 20.0;
    double nearMetres           = 0.05;
    double farMetres            = 1000.0;
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
    double stickX      = 0.0;       // −1 … 1 efter dödzonen
    double stickY      = 0.0;
    double trigger     = 0.0;       // 0 … 1
    bool   selectDown  = false;     // avtryckaren nedtryckt (med hysteres)
    bool   stickActive = false;     // profilen har en spak
};

// En bildruta, innan den ritas. `controllers[0]` är vänster, `[1]` höger.
struct FrameInfo {
    std::uint64_t sequence     = 0;
    double        deltaSeconds = 0.0;   // högst 0,1
    bool          focused      = false; // headsetet har fokus (input aktiv)
    Anchor        anchor;
    std::array<Controller, 2> controllers;
};

// Ett nytt avtryck: strålen i Glaze-världen och träffarna, närmast först.
// Tom träfflista = miss.
struct PickEvent {
    Hand        hand = Hand::Right;
    glaze::Ray3 ray;
    std::vector<glaze::HitResult> hits;
};

struct Status {
    State       state = State::Stopped;
    std::string message;
};

// Callbacks i UI-tråden. Argumenten gäller under anropet. En callback får
// anropa requestStop() men inte förstöra sessionen, öppna en modal pump
// eller kasta.
struct Callbacks {
    std::function<void(const Status&)>    onStatus;
    // Före plockningen och båda ögonens ritning — här uppdaterar appen
    // t.ex. sitt lager med kontrollerna och strålarna.
    std::function<void(const FrameInfo&)> onFrame;
    // En gång per ny tryckflank på avtryckaren.
    std::function<void(const PickEvent&)> onPick;
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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace glaze::openxr
