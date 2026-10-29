#pragma once
#include <cstdint>

namespace gfx {

// APPENS TIMER — en timer som hör till APPEN
// och inte till ett fönster: den går före det första fönstret och efter det
// sista. Konsumeras av `glaze::Timer`; appar ser aldrig den här headern.
//
// APP-GLOBALT, och därför fria funktioner med ett id i stället för rena
// virtuella på `Window` — samma modell som systemfältet (gfx/tray.h): TRE
// implementationer (D3D11, Metal, Linux) och ingen proxyfälla.
//
// LEVERANSEN är det befintliga `EventType::Timer` med windowId 0 och
// `Event::timerId` = id. Ett fönsters timer stämplas alltid med ett
// fönster-id ≠ 0, så windowId 0 skiljer dem utan en ny post. Upprepande;
// samma id startar om perioden. Väcker `waitEvent` som en fönstertimer, och
// appen sover mellan ticken (hård regel 1).
//
// ID:T VÄLJS AV ANROPAREN och är 64 bitar: glaze ger varje `start()` ett nytt,
// aldrig återanvänt körnings-id, så en tick som en stoppad körning redan
// hunnit lägga i kön inte kan förväxlas med en ny körnings (`KillTimer` tar
// inte bort ett redan postat `WM_TIMER`). 0 är aldrig ett id.
//
// UI-TRÅDENS, som resten av fasaden. Båda funktionerna är säkra före
// `init()` och efter `shutdown()`: `startAppTimer` svarar då false och
// `stopAppTimer` är en no-op, så en `glaze::Timer` som lever längre än appen
// inte kan krascha den.
//
//   Windows  `SetTimer` på ett lat skapat MEDDELANDEFÖNSTER (HWND_MESSAGE) —
//            ett trådmeddelande utan fönster hade försvunnit i en modal
//            flytt-loop
//   macOS    `NSTimer` i `NSRunLoopCommonModes` (tickar också under menyer
//            och fönsterdrag)
//   Linux    en timerfd i pumpens epoll — samma källa som fönstrens timers,
//            och den väcker Wayland-, X11- och headless-pumpen lika
using AppTimerId = std::uint64_t;

// Startar (eller startar om) timern `id` med perioden `ms` millisekunder;
// `ms` < 1 tolkas som 1. false före init, efter shutdown, för id 0 och om
// OS:et vägrar (loggat) — timern går då inte.
bool startAppTimer(AppTimerId id, std::uint32_t ms);

// Stoppar timern. Ett okänt id är en no-op. En tick som redan ligger i kön
// kan fortfarande levereras — mottagaren slår upp id:t och släpper det som
// inte går.
void stopAppTimer(AppTimerId id);

} // namespace gfx
