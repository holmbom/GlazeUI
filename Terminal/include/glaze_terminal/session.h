#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "glaze_terminal/pty.h"

// SESSIONEN — en `Pty` med TRE TRÅDAR: läsaren, skrivaren
// och väntaren. ConPTY kräver att varje kanal betjänas i en egen tråd, och
// POSIX tjänar på samma form. Ren std + OS, utan glaze: widgeten
// (`glaze::Terminal`) kopplar `onOutput`/`onExit` till `glaze::app().post`.
//
// LÄSAREN lägger bytes i en INKORG. `onOutput` anropas bara när ingen leverans
// redan väntar — EN post åt gången — och mottagaren hämtar med `take`. Har
// inkorgen nått `kInboxMax` väntar läsaren tills den tömts till hälften: ett
// program som skriver obegränsat (`yes`) blockeras i sin egen skrivning, och
// varken UI:t eller minnet tar skada. Inkorgen överstiger aldrig taket.
//
// SKRIVAREN har en kö: `write` blockerar aldrig. Kön har taket `kOutboxMax`;
// det som inte ryms kapas och sägs i `takeNotices` (ett program som aldrig
// läser sin indata och en enorm inklistring).
//
// VÄNTAREN väntar på processen. `onExit` anropas när processen avslutats
// OCH läsaren nått EOF — programmets sista utdata ligger då i inkorgen.
//
// RIVNINGEN (`kill`, destruktorn) går i en ordning, och ordningen är
// kontraktet:
//   1. `closing` sätts och läsaren väcks — från och med nu SLÄNGER den det
//      den läser i stället för att respektera taket. Annars hade ägaren
//      väntat i ConPTY:s stängning medan läsaren väntade på ägaren.
//   2. Processkedjan dödas: jobbobjektet på Windows; SIGHUP till gruppen på
//      POSIX och SIGKILL efter `kGraceMs` om GRUPPEN lever kvar — också när
//      huvudprocessen redan skördats.
//   3. Windows: `ClosePseudoConsole` på den ÄGANDE tråden medan läsaren tömmer;
//      har väntaren påbörjat den väntas den ut.
//   4. Väckaren och CancelSynchronousIo som säkring — först när stängningen
//      returnerat, för den behöver läsaren.
//   5. Trådarna joinas.
//
// TRÅDARNA: `onOutput` och `onExit` anropas i ARBETARTRÅDAR och får bara
// göra något trådsäkert — `glaze::app().post` är den enda glaze-funktion som
// får det. Allt annat anropas från den ägande tråden. En session startas en
// gång; en ny körning är en ny session.

namespace glaze::term {

class Session {
public:
    static constexpr std::size_t kInboxMax  = std::size_t{1} << 20;   // 1 MiB
    static constexpr std::size_t kOutboxMax = std::size_t{8} << 20;   // 8 MiB
    static constexpr int         kGraceMs   = 200;   // SIGHUP → SIGKILL (POSIX)
    // EN HÄMTNING: högst så mycket per `take`, så att en skur aldrig blir
    // en lång bildruta hos ägaren — resten hämtas nästa varv (postbudgetens
    // anda, `App::setPostBudget`). Taket bor här och inte hos ägaren, så att
    // ingen ägare kan glömma det.
    static constexpr std::size_t kTakeBudget = std::size_t{256} << 10;   // 256 KiB

    Session();
    ~Session();   // kill()
    Session(const Session&)            = delete;
    Session& operator=(const Session&) = delete;

    // Sätts FÖRE start(). Anropas i arbetartrådar (filhuvudet).
    std::function<void()> onOutput;
    std::function<void()> onExit;

    // false = programmet startade inte; `lastError()` säger varför.
    bool               start(const StartInfo& info);
    const std::string& lastError() const { return error_; }
    bool               isRunning() const;
    int                exitCode() const;   // −1 före slutet, eller okänd

    // Till programmets indata — blockerar aldrig (skrivarens kö).
    void write(std::string_view bytes);
    void resize(int columns, int rows);
    // Rivningsordningen ovan; efteråt är sessionen död. Idempotent.
    void kill();

    // ÄGAREN hämtar högst `kTakeBudget` byte ur inkorgen; `more` = det finns
    // mer (ägaren hämtar igen — `onOutput` kommer inte för det som redan låg
    // där).
    std::string take(bool& more);
    // Det som hänt i trådarna och som ägaren bör logga (kapad indata, okänd
    // slutkod).
    std::vector<std::string> takeNotices();
    // Den största inkorg läsaren haft — taket mätt (terminal_gate S4).
    std::size_t inboxHighWater() const;

private:
    void readLoop();
    void writeLoop();
    void waitLoop();
    void finishExit();
    void notice(std::string s);

    Pty         pty_;
    std::string error_;
    bool        started_ = false;
    bool        killed_  = false;

    mutable std::mutex      m_;
    std::condition_variable inboxCv_;    // läsaren väntar på tömning
    std::condition_variable outboxCv_;   // skrivaren väntar på data
    std::condition_variable exitCv_;     // kill() väntar på slutet
    std::string             inbox_;
    std::size_t             highWater_ = 0;
    std::string             outbox_;
    bool                    outboxCut_ = false;
    std::vector<std::string> notices_;
    bool                    closing_  = false;
    bool                    exited_   = false;
    bool                    readerDone_    = false;   // läsaren nådde EOF
    bool                    exitSignalled_ = false;   // `onExit` anropad
    int                     exitCode_ = -1;
    std::atomic<bool>       pending_{false};   // en leverans väntar hos ägaren

    std::thread reader_;
    std::thread writer_;
    std::thread waiter_;
};

} // namespace glaze::term
