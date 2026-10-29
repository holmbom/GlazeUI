#pragma once
#include <cstdint>

#include "glaze/profiler.h"

// INTERN — PROFILERARENS KROKAR. Det publika API:t och vad som
// mäts står i glaze/profiler.h; inspelningen bor i profiler.cpp. Krokarna
// sitter där arbetet görs — pumpen (app.cpp), innehållet och overlayen
// (app_window.cpp), ritytans faser (target3d.cpp, canvas.cpp) och räknarna
// (drape.cpp, scene.cpp, resources.cpp) — och varje krok börjar med
// `if (!profiling()) return;`: avstängd kostar den ett anrop och en atomisk
// läsning. Två undantag städar efter en inspelning också när profileraren
// stängts av under tiden: bildens slut (`profileFrameEnd` — en öppen bild
// kastas och GPU-tidtagningen slås av) och stängningen
// (`profileWindowClosed` — ringen släpps).
//
// BILDEN ÄR UI-TRÅDENS. Pumpen öppnar och stänger den runt fönstrets
// `renderFrame` (`profileFrameBegin`/`profileFrameEnd`); en bild som ritas
// UTANFÖR pumpen (D3D11:s `WM_SIZE`/`WM_PAINT` i den modala storleksloopen)
// öppnas av innehållspasset och stängs av overlayen. Faserna och räknarna
// hamnar i den bild som är öppen — utanför en bild (en `renderToBitmap`, en
// utskrift) spelas ingenting in.

namespace glaze {

class AppWindow;

namespace detail {

// Är profileraren på? Den fråga varje krok ställer först.
bool profiling() noexcept;

// `GLAZE_PROFILE` (glaze/profiler.h) — läses en gång per process; App:s
// konstruktor frågar, så att variabeln gäller också i en app som aldrig
// anropar `profiler()`.
void profileEnvironment();

// ---- PUMPEN (App::pumpUntilClosed)

// Varvets delar i ordning — `PumpProfile`s fält.
enum class PumpPart : std::uint8_t { Wait, Events, Posted, Timers, Commits, Closes };

// Varvet börjar på nästlingsdjupet `depth` (App::pumpDepth_); `profilePumpLap`
// ger tiden sedan förra varvet till `part` (och `count` händelser till
// Events); `profilePumpEnd` lägger posten i ringen. Ett nästlat varv räknas
// bort ur den yttre delens tid.
void profilePumpBegin(int depth);
void profilePumpLap(PumpPart part, std::uint32_t count = 0);
void profilePumpEnd(int depth);

// EN POSTAD FUNKTION (App::drainPosted) — antalet och den längsta.
class PostedCall {
public:
    PostedCall() noexcept;
    ~PostedCall();
    PostedCall(const PostedCall&)            = delete;
    PostedCall& operator=(const PostedCall&) = delete;

private:
    bool          on_ = false;
    double        t0_ = 0.0;   // ms
    double        nested0_ = 0.0;   // varvets nästlade tid vid start
};

// ---- BILDEN

// Runt `renderFrame` i pumpen. `continued`: nästa bild var begärd när den här
// var klar (`wantsFrame() || hasPosted()`).
void profileFrameBegin(AppWindow& w);
void profileFrameEnd(AppWindow& w, bool continued);

// En inmatning nådde fönstret (App::dispatch) — `FrameProfile::inputLatency`
// räknas från den första sedan förra bilden.
void profileInput(AppWindow& w);

// Fönstret stängs (App::executeClose, före OS-fönstrets rivning): dess ring
// släpps — också när profileraren är av.
void profileWindowClosed(AppWindow& w);

// INNEHÅLLET ELLER OVERLAYEN i fönstrets bild (`ProfilePhase::Content` eller
// `Overlay`). Innehållet öppnar en bild UTANFÖR pumpen när ingen är öppen, och
// overlayen stänger den — `total` blir då innehåll + overlay.
class WindowPass {
public:
    WindowPass(AppWindow& w, ProfilePhase phase) noexcept;
    ~WindowPass();
    WindowPass(const WindowPass&)            = delete;
    WindowPass& operator=(const WindowPass&) = delete;

private:
    AppWindow*    w_     = nullptr;   // nullptr = spelar inte in
    ProfilePhase  phase_ = ProfilePhase::Content;
    std::uint64_t token_ = 0;         // bilden den mäter i
    double        t0_    = 0.0;       // ms
};

// EN FAS i den öppna bilden — ritytans. Den andra formen räknas bort ur den
// yttre fasen `outer` (CanvasPaint inuti CanvasDraw).
class PhaseScope {
public:
    explicit PhaseScope(ProfilePhase phase) noexcept;
    PhaseScope(ProfilePhase phase, PhaseScope& outer) noexcept;
    ~PhaseScope();
    PhaseScope(const PhaseScope&)            = delete;
    PhaseScope& operator=(const PhaseScope&) = delete;

private:
    ProfilePhase  phase_;
    PhaseScope*   outer_    = nullptr;
    std::uint64_t token_    = 0;     // 0 = spelar inte in
    double        t0_       = 0.0;   // ms
    double        excluded_ = 0.0;   // ms som inre faser tagit
};

// `n` till räknaren `c` i den öppna bilden.
void profileCount(ProfileCount c, std::uint64_t n = 1) noexcept;

// RAMVERKETS ETIKETTER — förregistrerade (ids 0, 1, 2), så de har samma id i
// varje körning.
enum class FrameworkLabel : std::uint8_t { HitTest, CastRay, HeightAt };
const ProfileLabel& frameworkLabel(FrameworkLabel l);

} // namespace detail
} // namespace glaze
