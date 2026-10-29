#pragma once
#include <cstdint>

#include <gfx/events.h>

// INTERN header — ingår inte i publika API:t.
//
// APPENS TIMERREGISTER — kopplingen mellan
// `glaze::App` och de levande `Timer`-objekten. Timern hör till appen och
// inte till ett fönster, så dess tick har ingen fönsterväg att följa:
// `gfx::EventType::Timer` med windowId 0 tas i App:s app-switch (som Tray och
// Wakeup) och antecknas här, och pumpens SVANS levererar.
//
// Registret bor i src/timer.cpp och skapas av den första `Timer`-
// KONSTRUKTORN (`TrayIcon`s mönster) — det överlever därmed varje timer,
// också en global som rivs vid processlut. Varje `start()` får ett NYTT
// körnings-id (64 bitar, aldrig återanvänt): en tick som en stoppad körning
// redan lagt i kön hittar ingenting i stället för att träffa en ny körning.

namespace glaze {
class Timer;
} // namespace glaze

namespace glaze::detail {

// `App::dispatch` → hit, för varje `EventType::Timer` som inte hittade ett
// fönster. Ignorerar windowId ≠ 0 (ett stängt fönsters timer). Antecknar
// körningen som FÖRFALLEN — en gång: är den redan förfallen och inte
// levererad händer ingenting (EN tick, inte en kö). Ett okänt körnings-
// id (stoppad, omstartad, riven) släpps.
void timerNoteDue(const gfx::Event& e);

// `App::pumpUntilClosed` → hit i svansen, efter de postade funktionerna.
// Levererar det som var förfallet när anropet började, i tur och ordning —
// och prövar varje körning PRECIS före anropet: stoppad, omstartad eller
// redan levererad (av en nästlad pump i en tidigare handler) hoppas över. En
// en-gångs-timer stoppas före sin handler. En handler får riva vilken
// timer som helst, också sin egen.
void timerDrain();

// Väntar något på svansen? Pumpens sovvillkor, `App::hasPosted`s parhäst:
// pumpen somnar aldrig med en förfallen tick antecknad.
bool timerHasDue();

// Timerns körnings-id (0 = går inte). För grinden (tests/timer_gate.cpp):
// utan den går det inte att mata en GAMMAL körnings tick.
std::uint64_t timerRunIdOf(const Timer& t);

} // namespace glaze::detail
