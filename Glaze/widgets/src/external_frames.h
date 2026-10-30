#pragma once
#include <chrono>
#include <cstdint>

// INTERN header — ingår inte i publika API:t.
//
// EXTERNA BILDKÄLLOR (glaze/app.h `registerExternalFrameSource`): pumpens och
// fönstrets sida av registret som bor i src/app.cpp. Källans jobb körs i
// pump-svansen FÖRE ramverkets och appens poster, och medan en källa finns
// ritas skrivbordet högst `desktopHz` gånger per sekund.

namespace glaze::detail {

using ExternalClock = std::chrono::steady_clock;

// Väntar något källjobb på svansen? Pumpens sovvillkor, `App::hasPosted`s
// parhäst: pumpen somnar aldrig med ett jobb i kön.
bool externalJobPending();

// Svansen: kör de jobb som väntade när anropet började, ett per källa, hela
// och utan budget. Varje jobb prövas PRECIS före anropet — källan avregistrerad
// eller appen nedstängd under tiden stryker det.
void externalRunJobs();

// Presenterar fönstren i den externa takten? Sant medan någon källa finns.
// `AppWindow::show` ställer in ett nyöppnat fönster efter det.
bool externalPacingActive();

// Pumpens frågor om skrivbordsritningen. `externalDesktopAllowed` svarar sant
// när ingen källa finns eller när det gått minst en period sedan förra
// ritningen; annars falskt med `waitMs` = hur länge till (minst 1 ms).
// `externalDesktopDrawn` antecknar att skrivbordet ritades vid `at`.
bool externalDesktopAllowed(ExternalClock::time_point now, std::uint32_t& waitMs);
void externalDesktopDrawn(ExternalClock::time_point at);

// Väckningen på tid medan en skrivbordsritning väntar på sin period: appens
// timer med ett id ramverket äger (`glaze::Timer` delar ut tal från 1 och
// uppåt, så de möts aldrig).
inline constexpr std::uint64_t kExternalWakeTimer = std::uint64_t{1} << 62;

// `App::shutDown`: stänger registret. Väntande jobb släpps, ingen källa tas
// emot och inget jobb körs mer.
void externalClose();

} // namespace glaze::detail
