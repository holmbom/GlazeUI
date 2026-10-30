#pragma once

// Sessionens interna tillstånd, utan OpenXR- och Windows-typer: sessionens
// livscykel som tillståndsmaskin, och det som delas mellan UI-tråden och
// XR-tråden (uppdragen över till UI-tråden, och bilderna tillbaka).

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <glaze_openxr/session.h>

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

// ------------------------------------------------------------- DELAT TILLSTÅND

// En spårad pose i OpenXR:s spårningsrum.
struct PoseSample {
    TrackingPose pose;
    bool valid              = false;
    bool positionTracked    = false;
    bool orientationTracked = false;
};

// Handkontrollens läge och reglage, så som XR-tråden läst dem.
struct ControllerSample {
    PoseSample grip;
    PoseSample aim;
    double stickX      = 0.0;
    double stickY      = 0.0;
    double trigger     = 0.0;
    bool   selectDown  = false;
    bool   stickActive = false;
};

struct EyeRequest {
    bool          valid = false;     // posen är giltig och bilden ska ritas
    TrackingPose  pose;              // ögat i spårningsrummet
    SymmetricView view;              // bildens mått och synfält
};

// Det XR-tråden lämnar till UI-tråden för en bildruta.
struct FrameRequest {
    std::uint64_t sequence     = 0;
    double        deltaSeconds = 0.0;
    bool          focused      = false;
    std::uint64_t focusEpoch   = 0;   // LifeCycle::focusEpoch vid bildrutan
    bool          headValid    = false;
    TrackingPose  head;
    std::array<EyeRequest, 2>        eyes;
    std::array<ControllerSample, 2>  controllers;
};

struct EyeImage {
    int width  = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;   // RGBA8, rak alfa, rad 0 överst
};

// Det UI-tråden lämnar tillbaka.
struct FrameResult {
    std::string error;               // icke tomt: ett fel som stoppar sessionen
    bool        hasImages = false;   // båda ögonens bilder finns
    std::array<EyeImage, 2> images;
};

class CanvasBridge;

// Tillståndet för EN start. UI-uppdragen och XR-tråden håller var sin
// referens, så uppdragen kan köras (och avstå) även sedan tråden avslutat.
struct Run {
    Options options;
    std::unique_ptr<CanvasBridge> bridge;   // bara UI-tråden rör den

    std::mutex              mutex;
    std::condition_variable wake;
    bool                    stopRequested = false;   // under `mutex`
    bool                    abandoned     = false;   // under `mutex`: tråden väntar inte längre
    std::uint64_t           jobId         = 0;       // under `mutex`: senast postade uppdraget
    std::uint64_t           doneId        = 0;       // under `mutex`: senast färdiga uppdraget
    FrameResult             result;                  // under `mutex`: resultatet för `doneId`

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
