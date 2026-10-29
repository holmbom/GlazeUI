#pragma once
#include <cstdint>
#include <string>

#include "glaze/component.h"
#include "glaze/widget.h"   // Handler, EventArgs

// APPENS TIMER (WinForms `Timer`) — en timer
// som hör till APPEN och inte till ett fönster:
//
//     glaze::Timer klocka;                   // en medlem, eller en lokal i main
//     klocka.setInterval(250);
//     klocka.onTick = [this] { visaTid(); };
//     klocka.start();
//
// Den går före det första fönstret och efter det sista. En ICKE-VISUELL
// komponent (`glaze::Component`), så den står i `.glazeui` som en medlem i
// regionen som aldrig adopteras: `setInterval`/`setSingleShot` är
// setter-fält, `start()`/`stop()` metodanrop och `onTick` en koppling.
// Previewn skapar den men startar den aldrig — ingen timer tickar i ett
// förhandsvisat fönster.
//
// REGEL 1 GÄLLER: timern väcker pumpen när den förfaller och appen sover
// emellan. Den invaliderar ingenting själv — det gör settrarna handlern
// anropar, som efter vilken handler som helst.
//
// LEVERANSEN SKER I PUMP-SVANSEN, efter de postade funktionerna och före
// stängningarna (`App::pumpUntilClosed`) — aldrig mitt i dispatchen. Tre
// följder, alla avsiktliga:
//   * Ticken levereras när NÅGON pump går: `run()` av ett synligt, dolt
//     eller offscreen-fönster, och en nästlad modal. Mellan två `run()`
//     väntar den och kommer som EN tick, inte som en kö.
//   * Förfaller timern flera gånger innan svansen hinner fram (en handler
//     som tar längre tid än intervallet, eller Windows modala flytt-loop)
//     blir det EN tick, inte en kö — ritningen svälts aldrig.
//   * En `stop()` som en annan handler gör i samma varv gäller direkt.
//
// EN-GÅNGS-TIMERN (`setSingleShot(true)`) stoppas FÖRE handlern:
// `isRunning()` är false i den, och handlern får starta om den — det vanliga
// mönstret, som i WinForms och Qt.
//
// ÅTERINTRÄDE: öppnar en handler en MODAL dialog fortsätter timern ticka i
// dialogens nästlade pump — också den som öppnade dialogen, som i WinForms.
// Vill handlern inte det stoppar den timern före `run()`. Ingen tick
// levereras två gånger: varje förfallen körning bär sin stämpel, och den
// prövas PRECIS före anropet.
//
// LIVSTIDEN: en handler får riva sin egen timer (handlern hålls vid liv av
// `Handler` under anropet) och stoppa, starta om eller riva en ANNAN timer —
// en körning som inte längre är aktuell levereras aldrig. Destruktorn
// stoppar. Varken kopierbar eller flyttbar (registret pekar hit).

namespace glaze {

namespace detail {
// Registret och leveransen, i src/timer.cpp — bara maskineriet läser dem.
struct TimerState;
} // namespace detail

// Args till `onTick`. Tom i dag — ny data blir ett fält här och aldrig en
// parameter (regel 6).
struct TimerArgs : EventArgs {};

class Timer : public Component {
public:
    // Intervallet i millisekunder: förvalet (WinForms), och golvet som
    // `setInterval` klampar till — Windows minsta (`USER_TIMER_MINIMUM`), lika
    // på alla OS så att "1 ms fungerar på Linux men inte på Windows" aldrig
    // blir en skillnad i API:t.
    static constexpr int kDefaultIntervalMs = 100;
    static constexpr int kMinIntervalMs     = 10;

    Timer();
    ~Timer() override;   // stoppar

    // Under `kMinIntervalMs` klampas (loggas). Ett byte medan timern går
    // startar om den med den nya perioden.
    void setInterval(int ms);
    int  interval() const { return intervalMs_; }

    // En gång (true) eller upprepad (false, förval). Gäller nästa tick.
    void setSingleShot(bool on) { singleShot_ = on; }
    bool isSingleShot() const { return singleShot_; }

    // `start()` skapar `glaze::App` lat (timern kräver plattformen, som ett
    // fönsters `show()`) och startar om en timer som redan går — en ny
    // period räknas från anropet. Vägrar OS:et loggas det och `isRunning()`
    // förblir false. `stop()` på en timer som inte går är en no-op.
    void start();
    void stop();
    bool isRunning() const { return runId_ != 0; }

    // SENDER ÄR TIMERN (regel 6, `Handler<Args, Sender>`). Båda formerna:
    //     klocka.onTick = [this](glaze::Timer* s, glaze::TimerArgs&) { … };
    //     klocka.onTick = [this] { … };
    Handler<TimerArgs, Timer> onTick;

    // Previewns väg in (`Component::setProp`): `interval` (heltal) och
    // `singleShot` (`true`/`false`). Ett ogiltigt värde loggas och ignoreras.
    bool setProp(const std::string& key, const std::string& value) override;

private:
    friend struct detail::TimerState;

    int  intervalMs_ = kDefaultIntervalMs;
    bool singleShot_ = false;
    // Körningen: ett NYTT id per `start()`, aldrig återanvänt (0 = går inte),
    // och stämpeln för den förfallna tick som väntar på svansen (0 = ingen).
    std::uint64_t runId_    = 0;
    std::uint64_t dueStamp_ = 0;
};

} // namespace glaze
