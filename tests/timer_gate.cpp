// timer_gate — APPENS TIMER.
//
// Kör:  ./build-test/tests/timer_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = ett block kunde inte
//       köras (ingen display, eller plattformen startade ingen timer) — ctest
//       räknar den som SKIPPED, aldrig grön
//
// `glaze::Timer` hör till APPEN: fasaden (`gfx::startAppTimer`, windowId 0)
// väcker pumpen, `App::dispatch` ANTECKNAR körningen som förfallen och
// pumpens SVANS levererar (src/timer_registry.h). Grinden mäter kedjan med
// riktiga timrar i offscreen-fönstrens pumpar och leveransregeln med
// SYNTETISKA ticks (`timerNoteDue` + `timerDrain`, utan pump):
//
//   T0  `gfx::startAppTimer(1, 10)` FÖRST i main, före allt annat ⇒ false
//       (plattformen är inte startad).
//   T1  en timer startad INNAN något fönster finns tickar i ett offscreen-
//       fönsters pump: handlern stänger fönstret vid tredje ticken ⇒ exakt 3
//       tick och minst 2 intervall förflutna.
//   T2  EN-GÅNGS-timern: `isRunning()` är false i handlern, och den tickar
//       EN gång på fem intervall.
//   T3  en en-gångs-timer som startas om i sin handler tickar 2 gånger.
//   T4  en timer på heapen som rivs i sin EGEN handler: ingen krasch och inga
//       fler tick (räknaren ligger utanför timern).
//   T5  `stop()` och sedan en tick med det GAMLA körnings-id:t ⇒ ingen
//       handler — och start/stop/start i följd: det nya id:t
//       skiljer sig, det gamla levereras aldrig, det nya en gång.
//   T6  fem anteckningar av samma körning före svansen ⇒ EN handler.
//   T7  en tick öppnar en MODAL offscreen-dialog: timern tickar vidare i den
//       nästlade pumpen och stänger dialogen efter två tick till.
//   T8  en tick med windowId 5 och samma id ⇒ ingen handler (ett fönsters
//       timer, inte appens).
//   T9  `setInterval(3)` klampas till 10 (`kMinIntervalMs`), och ett byte
//       medan timern går startar om den med ett NYTT körnings-id.
//   T10 ÅTERINTRÄDET: två timrar förfallna i SAMMA svans, där
//       den första öppnar en nästlad pump (en modal) — den andra levereras
//       INNE i dialogens pump, och EN gång: ytterbatchen levererar den inte
//       igen när dialogen stängts.
//   T11 en handler som stoppar och startar om en ANNAN förfallen timer, och
//       river en tredje: ingen av de två levereras i samma svans, ingenting
//       kraschar, och den omstartade levereras vid sin nästa tick.
//
// SABOTAGEN som ska fälla den (körs före commit, aldrig under bygget):
//   · app.cpp: `case gfx::EventType::Timer` bort ur dispatch ........... T1
//   · app.cpp: `detail::timerDrain()` bort ur pumpens svans ........ T1, T7
//   · app.cpp: `detail::timerHasDue()` bort ur sovvillkoret ...... T10 (häng
//     tills vakthunden — fallet faller på tiden och på leveransplatsen)
//   · timer.cpp noteDue: `if (t.dueStamp_ != 0) return;` bort .......... T6
//   · timer.cpp: handlern körd i noteDue (dispatchen) i stället för i
//     svansen ......................................................... T6
//   · timer.cpp drain: `stopRun(t)` EFTER handlern ..................... T2
//   · timer.cpp start: körnings-id:t återanvänt (`run = 1`) ......... T5, T9
//   · timer.cpp: `~Timer` utan `stop()` ............ T4 (räknaren; sanerare)
//   · timer.cpp noteDue: `windowId`-villkoret bort ..................... T8
//   · timer.h: `kMinIntervalMs` ändrad ................................. T9
//   · timer.cpp drain: listan BYTS UT mot en lokal kopia (`swap`) ...... T10
//     (den andra timern levereras först EFTER dialogen)
//   · timer.cpp drain: posten tas inte ur listan / stämpeln prövas inte . T10
//     (dubbelleverans)
//   · timer.cpp drain: `running.find(f.run)`-vakten bort ............. T11
//   · timer.cpp setInterval: utan omstart när timern går ............... T9
//
// VAD GRINDEN INTE MÄTER (en skriven lista, inte ett förbiseende):
//   · timerns PRECISION — bara undre gränser för tiden, aldrig en övre (en
//     belastad maskin får vara sen).
//   · Windows MODALA FLYTT-LOOP: att ett fönsterdrag ger EN tick efter
//     släppet (doc/os/windows/TESTS.md, rad 5).
//   · Metal (`metal_timer.mm`) och Linux (`linux_timer.cpp`) — grinden körs på
//     det OS den byggs för; de andra OS:en har rader i doc/os/*/TESTS.md.
//   · att `gfx::startAppTimer` svarar false EFTER `gfx::shutdown` — appen
//     stängs aldrig ner under en grind.
//   · previewns timer (`applyComponentStmt` utför aldrig `start()`) — det
//     mäter preview_gate.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <gfx/timer.h>
#include <glaze/glaze.h>

#include "timer_registry.h"   // intern — anteckningen, svansen och körnings-id:t

namespace {

int  g_fails   = 0;
int  g_cases   = 0;
int  g_skips   = 0;
bool g_verbose = false;

void check(bool ok, const std::string& what, const std::string& fick = "")
{
    ++g_cases;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", what.c_str());
        return;
    }
    std::printf("  FEL  %s%s\n", what.c_str(),
                fick.empty() ? "" : (" — fick " + fick).c_str());
    ++g_fails;
}

void hoppa(const char* vad, const char* varför)
{
    std::printf("  --   %s hoppas över: %s\n", vad, varför);
    ++g_skips;
}

std::string tal(long long v) { return std::to_string(v); }

constexpr int kIntervall = 40;   // ms — långt över golvet, kort nog för sviten

// En tick som fasaden hade levererat, byggd för hand.
gfx::Event tick(std::uint64_t körning, gfx::WindowId fönster = 0)
{
    gfx::Event e;
    e.type     = gfx::EventType::Timer;
    e.windowId = fönster;
    e.timerId  = körning;
    return e;
}

// VAKTHUNDEN — en tråd som postar `close()` till ett fönster efter en
// tidsgräns, så att ett fel blir ett tydligt fall och inte ett häng.
// `glaze::app().post` är den enda glaze-funktion som får kallas från en
// annan tråd. Läget är DELAT: kommer posten fram efter att vakthunden rivits
// (pumpen slutade just då) rör den varken fönstret eller vakthunden.
class Vakthund {
public:
    Vakthund(glaze::AppWindow& w, std::chrono::milliseconds tak)
        : läge_(std::make_shared<Läge>()),
          tråd_([läge = läge_, &w, tak](std::stop_token st) {
              std::mutex                  m;
              std::unique_lock            lås(m);
              std::condition_variable_any cv;
              cv.wait_for(lås, st, tak, [] { return false; });
              if (st.stop_requested())
                  return;
              glaze::app().post([läge, &w] {
                  if (!läge->levande)
                      return;
                  läge->utlöst = true;
                  w.close();
              });
          })
    {
    }
    ~Vakthund() { läge_->levande = false; }   // tråden stoppas och väntas in sist

    bool utlöst() const { return läge_->utlöst; }

private:
    struct Läge {
        bool levande = true;
        bool utlöst  = false;
    };
    std::shared_ptr<Läge> läge_;
    std::jthread          tråd_;
};

// Ett offscreen-fönster för pumpen — det syns aldrig.
class PumpFönster : public glaze::AppWindow {
public:
    explicit PumpFönster(const char* titel) : glaze::AppWindow(titel, 160, 100)
    {
        setOffscreen(true);
    }
};

using Klocka = std::chrono::steady_clock;

long long msSedan(Klocka::time_point t0)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Klocka::now() -
                                                                 t0)
        .count();
}

// ================================================== T1–T4: riktiga timrar

void delPump()
{
    std::printf("\n=== T1–T4, T7: riktiga timrar i en pump ===\n");

    // T1 — startad INNAN något fönster finns.
    {
        glaze::Timer t;
        t.setInterval(kIntervall);
        PumpFönster* fönster = nullptr;   // sätts innan pumpen kör
        int          n       = 0;
        long long    vidTre  = 0;
        const auto   t0      = Klocka::now();
        t.onTick = [&] {
            ++n;
            if (n == 3) {
                vidTre = msSedan(t0);
                fönster->close();
            }
        };
        t.start();
        if (!t.isRunning()) {
            hoppa("T1", "plattformen startade ingen timer (ingen display?)");
        } else {
            PumpFönster w("timer_gate-T1");
            fönster = &w;
            Vakthund vakt(w, std::chrono::milliseconds(5000));
            w.run();
            check(!vakt.utlöst(), "T1 handlern stängde fönstret — inte "
                                  "vakthunden (5 s)");
            check(n == 3, "T1 exakt 3 tick — inget levererat efter "
                          "stängningen i samma pump",
                  tal(n));
            check(vidTre >= 2 * kIntervall,
                  "T1 minst 2 intervall förflutna vid tredje ticken",
                  tal(vidTre) + " ms");
        }
    }

    // T2 — en-gångs: stoppad FÖRE handlern, och bara en tick.
    {
        PumpFönster  w("timer_gate-T2");
        glaze::Timer en, klocka;
        int          n = 0, varv = 0;
        bool         gickIHandlern = true;
        en.setInterval(kIntervall);
        en.setSingleShot(true);
        en.onTick = [&](glaze::Timer* s, glaze::TimerArgs&) {
            ++n;
            gickIHandlern = s->isRunning();
        };
        klocka.setInterval(kIntervall);
        klocka.onTick = [&] {
            if (++varv == 5)
                w.close();
        };
        en.start();
        klocka.start();
        if (!en.isRunning() || !klocka.isRunning()) {
            hoppa("T2", "plattformen startade ingen timer");
        } else {
            Vakthund vakt(w, std::chrono::milliseconds(5000));
            w.run();
            check(!vakt.utlöst(), "T2 (fixturen) klockan stängde fönstret");
            check(n == 1, "T2 en-gångs-timern tickade EN gång på fem "
                          "intervall",
                  tal(n));
            check(!gickIHandlern, "T2 isRunning() är false i handlern — "
                                  "stoppad FÖRE");
            check(!en.isRunning(), "T2 … och efteråt");
        }
    }

    // T3 — en en-gångs-timer som startar om sig själv en gång.
    {
        PumpFönster  w("timer_gate-T3");
        glaze::Timer en, klocka;
        int          n = 0, varv = 0;
        en.setInterval(kIntervall);
        en.setSingleShot(true);
        en.onTick = [&](glaze::Timer* s, glaze::TimerArgs&) {
            if (++n == 1)
                s->start();   // det vanliga mönstret — tillåtet i handlern
        };
        klocka.setInterval(kIntervall);
        klocka.onTick = [&] {
            if (++varv == 6)
                w.close();
        };
        en.start();
        klocka.start();
        if (!en.isRunning() || !klocka.isRunning()) {
            hoppa("T3", "plattformen startade ingen timer");
        } else {
            Vakthund vakt(w, std::chrono::milliseconds(5000));
            w.run();
            check(!vakt.utlöst(), "T3 (fixturen) klockan stängde fönstret");
            check(n == 2, "T3 omstartad i handlern ⇒ 2 tick", tal(n));
        }
    }

    // T4 — riven i sin egen handler; räknaren ligger utanför timern.
    {
        PumpFönster                   w("timer_gate-T4");
        auto                          t = std::make_unique<glaze::Timer>();
        glaze::Timer                  klocka;
        int                           n = 0, varv = 0;
        t->setInterval(kIntervall);
        t->onTick = [&] {
            ++n;
            t.reset();   // `Handler` håller lambdan vid liv under anropet
        };
        klocka.setInterval(kIntervall);
        klocka.onTick = [&] {
            if (++varv == 5)
                w.close();
        };
        t->start();
        klocka.start();
        if (!t->isRunning() || !klocka.isRunning()) {
            hoppa("T4", "plattformen startade ingen timer");
        } else {
            Vakthund vakt(w, std::chrono::milliseconds(5000));
            w.run();
            check(!vakt.utlöst(), "T4 (fixturen) klockan stängde fönstret");
            check(n == 1, "T4 en tick, sedan riven — inga fler", tal(n));
            check(!t, "T4 (fixturen) timern revs i handlern");
        }
    }

    // T7 — en modal i handlern: timern tickar vidare i den nästlade pumpen.
    {
        PumpFönster  ägare("timer_gate-T7");
        PumpFönster  dlg("timer_gate-T7-dialog");
        glaze::Timer t;
        int          n = 0, iDialogen = 0;
        bool         återvände = false;
        dlg.setModal(true);
        t.setInterval(kIntervall);
        t.onTick = [&] {
            ++n;
            if (n == 1) {
                dlg.run(ägare);   // nästlad pump — returnerar när den stängs
                återvände = true;
                ägare.close();
            } else {
                if (dlg.isOpen())
                    ++iDialogen;
                if (n == 3)
                    dlg.close();
            }
        };
        t.start();
        if (!t.isRunning()) {
            hoppa("T7", "plattformen startade ingen timer");
        } else {
            Vakthund vakt(ägare, std::chrono::milliseconds(5000));
            ägare.run();
            check(!vakt.utlöst(), "T7 (fixturen) kedjan stängde fönstren — "
                                  "inte vakthunden");
            check(återvände, "T7 dialogen stängdes och handlern återvände");
            check(n == 3, "T7 3 tick: en öppnade dialogen, två i den", tal(n));
            check(iDialogen == 2, "T7 de två senare ticken levererades MEDAN "
                                  "dialogen var öppen (nästlad pump)",
                  tal(iDialogen));
        }
    }
}

// ============================================ T5, T6, T8, T9, T11: svansen

void delSvansen()
{
    std::printf("\n=== T5, T6, T8, T9, T11: leveransen utan pump ===\n");
    using glaze::detail::timerDrain;
    using glaze::detail::timerNoteDue;
    using glaze::detail::timerRunIdOf;

    // T9 — klampningen behöver ingen plattform.
    {
        glaze::Timer t;
        t.setInterval(3);
        check(t.interval() == glaze::Timer::kMinIntervalMs &&
                  glaze::Timer::kMinIntervalMs == 10,
              "T9 setInterval(3) klampas till 10", tal(t.interval()));
        check(glaze::Timer::kDefaultIntervalMs == 100 &&
                  glaze::Timer().interval() == 100,
              "T9 förvalet är 100 ms (WinForms)");
    }

    // Långt intervall: ingen RIKTIG tick hinner komma — och ingen pump går.
    constexpr int kLångt = 60000;

    {
        glaze::Timer t;
        int          n = 0;
        t.setInterval(kLångt);
        t.onTick = [&] { ++n; };
        t.start();
        if (!t.isRunning()) {
            hoppa("T5/T6/T8/T9b", "plattformen startade ingen timer");
            return;
        }

        // T6 — fem anteckningar före svansen ⇒ EN leverans.
        const std::uint64_t körning = timerRunIdOf(t);
        for (int i = 0; i < 5; ++i)
            timerNoteDue(tick(körning));
        timerDrain();
        check(n == 1, "T6 fem anteckningar av samma körning ⇒ EN handler",
              tal(n));
        timerDrain();
        check(n == 1, "T6 … och en tom svans levererar ingenting", tal(n));

        // T8 — ett fönsters timer med samma id är inte appens.
        timerNoteDue(tick(körning, 5));
        timerDrain();
        check(n == 1, "T8 windowId 5 ⇒ ingen handler", tal(n));

        // T9b — ett intervallbyte medan timern går = ny körning.
        t.setInterval(kLångt + 1000);
        const std::uint64_t efterByte = timerRunIdOf(t);
        check(t.isRunning() && efterByte != 0 && efterByte != körning,
              "T9 setInterval medan timern går startar om med ett NYTT "
              "körnings-id",
              tal(static_cast<long long>(efterByte)));

        // T5 — stopp, sedan en tick med det gamla id:t.
        const std::uint64_t gammal = timerRunIdOf(t);
        t.stop();
        check(!t.isRunning() && timerRunIdOf(t) == 0,
              "T5 (fixturen) stop() ⇒ ingen körning");
        timerNoteDue(tick(gammal));
        timerDrain();
        check(n == 1, "T5 en tick för den STOPPADE körningen ⇒ ingen handler",
              tal(n));

        // T5 — start/stop/start i följd.
        t.start();
        const std::uint64_t a = timerRunIdOf(t);
        t.stop();
        t.start();
        const std::uint64_t b = timerRunIdOf(t);
        check(a != 0 && b != 0 && a != b && a != gammal && b != gammal,
              "T5 varje start() får ett NYTT körnings-id — aldrig återanvänt",
              tal(static_cast<long long>(a)) + " / " +
                  tal(static_cast<long long>(b)));
        timerNoteDue(tick(a));
        timerDrain();
        check(n == 1, "T5 den mellersta körningens tick ⇒ ingen handler",
              tal(n));
        timerNoteDue(tick(b));
        timerDrain();
        check(n == 2, "T5 den aktuella körningens tick ⇒ en handler", tal(n));
        t.stop();
    }

    // T11 — en handler stoppar och startar om en ANNAN förfallen timer och
    // river en tredje. Alla tre antecknade i samma svans.
    {
        glaze::Timer a, b;
        auto         c  = std::make_unique<glaze::Timer>();
        int          na = 0, nb = 0, nc = 0;
        a.setInterval(kLångt);
        b.setInterval(kLångt);
        c->setInterval(kLångt);
        a.onTick = [&] {
            ++na;
            b.stop();
            b.start();   // ny körning — den gamla tickens körning finns inte
            c.reset();   // riven medan dess tick väntar i samma svans
        };
        b.onTick = [&] { ++nb; };
        c->onTick = [&] { ++nc; };
        a.start();
        b.start();
        c->start();
        if (!a.isRunning() || !b.isRunning() || !c->isRunning()) {
            hoppa("T11", "plattformen startade ingen timer");
        } else {
            timerNoteDue(tick(timerRunIdOf(a)));
            timerNoteDue(tick(timerRunIdOf(b)));
            timerNoteDue(tick(timerRunIdOf(*c)));
            timerDrain();
            check(na == 1, "T11 (fixturen) den första timerns handler körde",
                  tal(na));
            check(nb == 0, "T11 den omstartade timerns GAMLA tick levereras "
                           "inte",
                  tal(nb));
            check(nc == 0 && !c, "T11 den rivna timerns tick levereras inte "
                                 "— och ingenting kraschar",
                  tal(nc));
            timerNoteDue(tick(timerRunIdOf(b)));
            timerDrain();
            check(nb == 1, "T11 den omstartade timern levereras vid sin NÄSTA "
                           "tick",
                  tal(nb));
        }
    }
}

// ============================================== T10: återinträdet

void delÅterinträde()
{
    std::printf("\n=== T10: två förfallna timrar och en nästlad pump ===\n");
    using glaze::detail::timerNoteDue;
    using glaze::detail::timerRunIdOf;

    constexpr int kLångt = 60000;   // bara de syntetiska tickarna räknas
    PumpFönster   ägare("timer_gate-T10");
    PumpFönster   dlg("timer_gate-T10-dialog");
    dlg.setModal(true);
    glaze::Timer a, b;
    int          na = 0, nb = 0;
    bool         bIDialogen = false;
    a.setInterval(kLångt);
    b.setInterval(kLångt);
    a.onTick = [&] {
        ++na;
        dlg.run(ägare);   // B ska levereras HÄRINNE — och bara här
        ägare.close();
    };
    b.onTick = [&] {
        ++nb;
        bIDialogen = dlg.isOpen();
        if (dlg.isOpen())
            dlg.close();
    };
    a.start();
    b.start();
    if (!a.isRunning() || !b.isRunning()) {
        hoppa("T10", "plattformen startade ingen timer");
        return;
    }
    // Båda antecknas i SAMMA svans: den postade funktionen körs i
    // `drainPosted`, och `timerDrain` följer direkt efter.
    bool kördes = false;
    glaze::app().post([&] {
        kördes = true;
        timerNoteDue(tick(timerRunIdOf(a)));
        timerNoteDue(tick(timerRunIdOf(b)));
    });
    Vakthund vakt(ägare, std::chrono::milliseconds(5000));
    ägare.run();
    if (!kördes) {
        hoppa("T10", "inget fönster kunde skapas (ingen display)");
        return;
    }
    // Vakthunden stänger bara ägaren — en dialog som aldrig stängdes av B
    // hade hängt kvar i A:s handler, och då når vi aldrig hit.
    check(!vakt.utlöst(), "T10 (fixturen) kedjan stängde fönstren — inte "
                          "vakthunden");
    check(na == 1, "T10 den första timern levererades en gång", tal(na));
    check(nb == 1, "T10 den andra levererades EN gång — ytterbatchen "
                   "levererade den inte igen",
          tal(nb));
    check(bIDialogen, "T10 … och det skedde INNE i dialogens pump");
}

} // namespace

int main(int argc, char** argv)
{
    // T0 — FÖRST av allt: ingen plattform, ingen timer.
    const bool förInit = gfx::startAppTimer(1, 10);

    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("timer_gate — appens timer\n");
    std::printf("\n=== T0: före plattformen ===\n");
    check(!förInit, "T0 gfx::startAppTimer före gfx::init ⇒ false");
    gfx::stopAppTimer(1);   // ett okänt id är en no-op — också före init

    delSvansen();
    delPump();
    delÅterinträde();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("\n%s — %d fall, %d fel\n",
                g_fails == 0 ? "TIMER_GATE GRÖN" : "TIMER_GATE FÄLLD", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
