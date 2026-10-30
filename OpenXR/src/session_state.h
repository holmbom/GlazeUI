#pragma once

// Sessionens interna tillstånd, utan OpenXR- och Windows-typer: sessionens
// livscykel som tillståndsmaskin, överlämningen av en bildruta mellan XR-
// tråden och UI-tråden, och det som delas mellan sessionen och dess uppdrag.

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <glaze_openxr/session.h>

namespace glaze {
class Timer;
}

namespace glaze::openxr::detail {

// ------------------------------------------------------------------ LIVSCYKEL

// OpenXR:s sessionstillstånd, i runtimens ordning.
enum class Phase {
    Idle,
    Ready,
    Synchronized,
    Visible,
    Focused,
    Stopping,
    Exiting,
    LossPending
};

// Vad XR-tråden ska göra med sessionen efter ett tillståndsbyte.
enum class Step {
    None,
    BeginSession,   // xrBeginSession
    EndSession      // xrEndSession
};

// Bokföring av sessionens liv: vilket tillstånd runtimen är i, om
// sessionen körs, om ett stopp begärts och varför tråden ska avsluta. Rör
// inte OpenXR; XR-tråden matar den med händelser och gör det den svarar.
class LifeCycle {
public:
    // Runtimen bytte tillstånd. Svaret är nästa anrop i OpenXR.
    Step onPhase(Phase phase);
    // xrBeginSession / xrEndSession lyckades.
    void sessionBegun() noexcept { running_ = true; }
    void sessionEnded() noexcept;
    // Begär stopp. Med ett `failure` blir sluttillståndet Failed med den
    // texten; ett tidigare skäl behålls. Utan körande session avslutas
    // direkt.
    void requestStop(const std::string& failure = {});
    // Ger upp väntan på en avveckling som aldrig kommer; avslutar utan fel.
    void giveUp() noexcept { terminate_ = true; }
    // Ett fel där sessionen inte kan avvecklas i god ordning: avsluta nu.
    void fail(const std::string& message);

    // Sant en gång när xrRequestExitSession ska anropas.
    bool takeExitRequest() noexcept;

    bool stopRequested() const noexcept { return stopRequested_; }
    bool running() const noexcept { return running_; }
    bool terminate() const noexcept { return terminate_; }
    // Bilder ska ritas: sessionen körs, är synlig och inget stopp begärt.
    bool rendering() const noexcept;
    // Headsetet har fokus och inget stopp begärt: indata är aktiv.
    bool focused() const noexcept;
    // Räknas upp varje gång fokus lämnar headsetet. Reglagens tillstånd från
    // före ett byte gäller inte efter det.
    std::uint64_t focusEpoch() const noexcept { return focusEpoch_; }

    // Läget som ska visas just nu (aldrig Stopped eller Failed).
    Status status() const;
    // Sluttillståndet när tråden avvecklat allt.
    Status finalStatus() const;

private:
    Phase       phase_         = Phase::Idle;
    bool        running_       = false;
    bool        stopRequested_ = false;
    bool        exitRequested_ = false;
    bool        terminate_     = false;
    std::uint64_t focusEpoch_  = 0;
    std::string failure_;
};

// ------------------------------------------------------------- ÖVERLÄMNINGEN

// Hur en bildruta som lämnats till UI-tråden slutade.
enum class SubmitResult {
    Done,        // bildrutan är avslutad och bilderna släppta
    Cancelled,   // ingenting gjordes: bilderna är kvar hos anroparen
    Failed       // ett fel; bildrutan är avslutad utan lager och bilderna släppta
};

// Ett uppdrag som XR-tråden ger UI-tråden, ett i taget: Idle → Posted (XR-
// tråden) → Running (UI-tråden) → Done (UI-tråden) → Idle (XR-tråden tar
// emot kvittot). XR-tråden väntar på kvittot men kan avbryta ett uppdrag som
// UI-tråden ännu inte börjat; ett uppdrag som körs väntar den in, för det rör
// då bildrutans objekt. UI-tråden väntar aldrig på XR-tråden.
class JobGate {
public:
    enum class Await {
        Done,        // uppdraget kördes färdigt
        Cancelled    // stopp kom innan uppdraget började; det kommer aldrig att köras
    };

    // Begär stopp. Från vilken tråd som helst.
    void stop();
    bool stopped() const;
    // Väntar högst `time`, avbruten av ett stopp.
    void sleepFor(std::chrono::milliseconds time);

    // XR-tråden: förbereder ett uppdrag. false vid stopp eller om ett annat
    // uppdrag inte är mottaget.
    bool arm(std::uint64_t& id);
    // XR-tråden: uppdraget kunde inte postas och tas tillbaka.
    void abort(std::uint64_t id);
    // XR-tråden: väntar på kvittot.
    Await await(std::uint64_t id);

    // UI-tråden: uppdraget börjar. false när det avbrutits eller ersatts —
    // då får det inte köra.
    bool begin(std::uint64_t id);
    // UI-tråden: uppdraget är färdigt.
    void finish(std::uint64_t id);

private:
    enum class State { Idle, Posted, Running, Done };

    mutable std::mutex      mutex_;
    std::condition_variable wake_;
    bool                    stop_ = false;
    State                   state_ = State::Idle;
    std::uint64_t           id_ = 0;
};

// ------------------------------------------------------------- DELAT TILLSTÅND

// En spårad pose i OpenXR:s spårningsrum.
struct PoseSample {
    TrackingPose pose;
    bool valid              = false;
    bool positionTracked    = false;
    bool orientationTracked = false;
};

// Handkontrollens läge och reglage, så som de lästs. Lägena och hastigheterna
// ligger i spårningsrummet (OpenXR:s axlar); knapparna är redan lösta efter
// dödzon, hysteres och profilens tillgängliga knappar.
struct ControllerSample {
    PoseSample grip;
    PoseSample aim;
    PoseSample palm;
    // Handens hastighet (grip) i spårningsrummet: m/s och rad/s.
    bool velocityValid = false;
    Vec3 linearVelocity;
    Vec3 angularVelocity;
    std::array<ButtonState, kButtonCount> buttons{};
    double stickX = 0.0, stickY = 0.0;   // efter dödzonen
    double padX   = 0.0, padY   = 0.0;
    std::string profile;                 // den aktiva interaktionsprofilen, tom utan kontroll
};

// En handleds läge så som handspårningen gav det, i spårningsrummet.
struct JointSample {
    TrackingPose pose;
    double       radius = 0.0;   // meter
    bool         valid  = false;
};

struct HandSample {
    bool active = false;
    std::array<JointSample, 26> joints{};
    double pinch = 0.0;
    double grab  = 0.0;
};

// Ett ögas läge och synfält så som runtimen gav dem.
struct EyeView {
    bool         valid = false;   // posen och synfältet är giltiga och ögat ska ritas
    TrackingPose pose;
    Fov          fov;
};

// Allt UI-tråden vet om en bildruta innan den ritas.
struct FrameRequest {
    std::uint64_t sequence     = 0;
    double        deltaSeconds = 0.0;
    bool          focused      = false;
    std::uint64_t focusEpoch   = 0;   // LifeCycle::focusEpoch vid bildrutan
    bool          headValid    = false;
    TrackingPose  head;
    std::array<EyeView, 2>          eyes;
    std::array<ControllerSample, 2> controllers;
    std::array<HandSample, 2>       hands;
    std::vector<InputEvent>         events;         // knappflankerna sedan förra bildrutan
    double floorHeight = 0.0;                       // golvets höjd i spårningsrummet (y), meter
    std::array<std::uint32_t, 2>    imageIndex{};   // bilden att rita i, per öga
};

// Ett ögas swapchain-bilder: runtimens rekommenderade mått och bilderna som
// opaka pekare (D3D11: `ID3D11Texture2D*`, ägda av runtimen), så som Canvas-
// renderaren importerar dem.
struct EyeImageSet {
    int                width  = 0;
    int                height = 0;
    bool               bgra   = false;   // annars RGBA; sRGB-familjen i båda
    std::vector<void*> images;
};

struct FrameResult {
    std::string error;           // icke tomt: ett fel som stoppar sessionen
    bool        rendered = false;   // båda ögonen är ritade
};

class CanvasBridge;
class OpenXrContext;
class Actions;

// Hur UI-trådens avveckling slutade.
enum class TeardownEnd {
    Clean,       // GPU:n har bekräftat att allt arbete före markören är klart
    DeviceLost,  // enheten förlorades: inget arbete återstår att vänta på
    GpuTimeout   // GPU:n blev inte klar i tid: arbetet kan fortfarande läsa bilderna
};

// Att UI-tråden river sin sida (målen, passet) innan XR-tråden river
// swapchains och session. XR-tråden väntar på `done`, utan tidsgräns: UI-
// tråden avgör avvecklingen med en bekräftelse, ett enhetsfel eller sin
// egen tidsgräns för GPU:n.
struct Teardown {
    std::mutex              mutex;
    std::condition_variable wake;
    bool                    done = false;               // under `mutex`
    TeardownEnd             end = TeardownEnd::Clean;   // under `mutex`; giltig när `done`
    std::string             failure;                    // under `mutex`; texten när `end` inte är Clean
};

// Tillståndet för EN start. UI-uppdragen och XR-tråden håller var sin
// referens, så uppdragen kan köras (och avstå) även sedan tråden avslutat.
struct Run {
    Options options;
    std::unique_ptr<CanvasBridge> bridge;   // bara UI-tråden rör den
    std::uint64_t source = 0;               // bildkällan i pumpen; bara UI-tråden
    bool          sourceUnregistered = false;   // bara UI-tråden
    bool          teardownBegun = false;    // bara UI-tråden: markören lagd, tidsgränsen satt
    bool          tornDown = false;         // bara UI-tråden: målen och passet släppta
    std::chrono::steady_clock::time_point gpuDeadline{};   // bara UI-tråden: när GPU:n måste vara klar
    std::unique_ptr<glaze::Timer> poll;     // bara UI-tråden: nästa kontroll av GPU:n

    JobGate  gate;
    Teardown teardown;

    // XR-trådens objekt, som UI-uppdragen når medan de körs. Sätts av XR-
    // tråden före första uppdraget och nollas efter det sista; uppdragen
    // körs bara mellan `JobGate::begin` och `finish`, då XR-tråden väntar.
    OpenXrContext* context = nullptr;
    Actions*       actions = nullptr;
    // Skyddar `context` och `actions` mot UI-trådens anrop utanför bildrutan
    // (vibration, uppdateringsfrekvens): XR-tråden sätter och nollar pekarna
    // under låset, och anropen använder dem under det.
    std::mutex     apiMutex;
    bool           refreshRateRequested = false;   // bara UI-tråden: `Options::refreshRate` begärd

    // Kvittot för det senaste bildrutejobbet; skrivs före `finish`, läses efter
    // `await`.
    SubmitResult lastResult = SubmitResult::Done;
    std::string lastError;

    Run();
    ~Run();
};

// Tillståndet som delas mellan sessionen och dess köade uppdrag. Canvas,
// callbacks, `alive`, `run` och `worker` rör bara UI-tråden; XR-tråden når
// bara sin `Run`. `alive` nollställs av ~Session innan tråden joinas, så ett
// uppdrag som körs senare rör varken Canvas eller callbacks.
struct Shared : std::enable_shared_from_this<Shared> {
    glaze::Canvas*       canvas = nullptr;
    Callbacks            callbacks;
    bool                 alive = true;
    std::atomic<State>   state{State::Stopped};   // senast levererade läget
    std::shared_ptr<Run> run;                     // den pågående starten, annars null
    std::thread          worker;
};

} // namespace glaze::openxr::detail
