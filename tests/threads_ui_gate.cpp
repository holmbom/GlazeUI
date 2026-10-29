// threads_ui_gate — SIDOPAKETET Threads, UI-BRYGGAN.
//
// Kör:  ./build/tests/threads_ui_gate
// Exit: 0 = grönt · 1 = ett fall föll · 77 = ingen display (pumpfallen
//       kunde inte köras — ctest rapporterar SKIPPED, aldrig grönt)
//       · ctest-timeouten fångar ett häng
//
// Länkar `glaze_threads_ui` (bryggan) och via den `glaze`. Pumpen körs
// headless som i post_gate: ett DOLT fönster med `run()`.
//
// VAD DEN MÄTER:
//   0. Självregistreringen — länkad brygga ÄR `defaultContext()`.
//   A. Fortsättningen körs i UI-TRÅDEN, både för ett svar som låg i kön när
//      pumpen startade och för ett som kommer medan pumpen SOVER i
//      `waitEvent` (väckningen måste alltså fungera). Fallet börjar med att
//      Workerns `run` är processens FÖRSTA kontakt med glaze: `prepare()`
//      ska skapa App på main. Saboteras det skapas App i stället av
//      pooltrådens `post`, D3D11 fångar pooltrådens id i `gfx::init`, och
//      det andra svaret väcker fel tråd — grinden HÄNGER och ctest-timeouten
//      fäller den. (På Linux är väckningen inte trådbunden; där fångas den
//      sabotagevarianten inte.)
//   B. Workern DÖR medan svaret ligger i App-kön ⇒ svaret släpps.
//   C. Mellanbesked körs i UI-tråden, i postordning, före svaret.
//   D. `cancel()` medan arbetet kör ⇒ fortsättningen körs aldrig.
//
// VAD DEN INTE MÄTER: kärnans semantik i övrigt (threads_gate gör det).

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include <glaze/glaze.h>
#include <glaze_threads/threads.h>
#include <glaze_threads/ui.h>

namespace {

using namespace std::chrono_literals;
namespace gt = glaze::threads;

int g_fel = 0;

void check(bool ok, const char* vad)
{
    std::printf("%-6s %s\n", ok ? "OK" : "FEL", vad);
    if (!ok)
        ++g_fel;
}

class DoltFönster : public glaze::AppWindow {
public:
    DoltFönster() : glaze::AppWindow("threads_ui_gate", 200, 120)
    {
        setOffscreen(true);
    }
};

// En pumpomgång som stänger sig själv: allt som ligger i App-kön dräneras i
// första svansen, stängningen sist (kön är FIFO).
void pumpaEnGång()
{
    DoltFönster f;
    glaze::app().post([&f] { f.close(); });
    f.run();
}

void väntaPåStopp(std::stop_token stopp, std::chrono::milliseconds tak)
{
    std::mutex                   m;
    std::condition_variable_any  cv;
    std::unique_lock<std::mutex> lås(m);
    cv.wait_for(lås, stopp, tak, [] { return false; });
}

} // namespace

int main()
{
    std::printf("=== threads_ui_gate — sidopaketet Threads, UI-bryggan ===\n");
    const auto main = std::this_thread::get_id();

    // ---- 0. Självregistreringen (ingen display behövs).
    check(&gt::defaultContext() == &gt::uiContext(),
          "länkad brygga är defaultkontexten (självregistrerad)");

    // ---- A, första halvan: run() är processens FÖRSTA glaze-kontakt.
    gt::Worker      w;
    std::thread::id förstaArbete{};
    std::thread::id förstaKördI{};
    w.run([] { return std::this_thread::get_id(); },
          [&](gt::Result<std::thread::id>& r) {
              förstaArbete = r ? *r : std::thread::id{};
              förstaKördI  = std::this_thread::get_id();
          });
    w.wait();   // arbetet klart — svaret ligger i App-kön, ingen pump än

    // Displaykollen på ett EGET fönster (`run()` vägrar ett redan öppet).
    // Proben stängs av sin DESTRUKTOR och inte med `close()`: en köad
    // stängning på ett fönster som förstörs innan pumpen hunnit verkställa
    // den lämnar en dinglande pekare i `App::pendingClose_` (`~AppWindow` →
    // `executeClose` plockar inte ur kön), och nästa fönster på samma
    // stackadress stängdes av den — `f.run()` nedan returnerade direkt.
    bool harDisplay = false;
    {
        DoltFönster probe;
        probe.show();
        harDisplay = probe.isOpen();
    }
    {
        if (!harDisplay) {
            std::printf("  --   pumpfallen hoppas över: inget fönster kunde "
                        "skapas (ingen display)\n");
            return g_fel ? 1 : 77;
        }
    }

    // ---- A, andra halvan: ett svar som kommer medan pumpen SOVER.
    int             andraVärde = 0;
    std::thread::id andraKördI{};
    {
        DoltFönster f;
        w.run([](std::stop_token) {
                  std::this_thread::sleep_for(300ms);
                  return 2;
              },
              [&](gt::Result<int>& r) {
                  andraVärde = r ? *r : -1;
                  andraKördI = std::this_thread::get_id();
                  f.close();
              });
        f.run();   // HÄNGER om väckningen går till fel tråd
    }
    check(förstaArbete != std::thread::id{} && förstaArbete != main,
          "A: arbetet kördes i en pooltråd");
    check(förstaKördI == main, "A: svaret som låg i kön körs i UI-tråden");
    check(andraVärde == 2 && andraKördI == main,
          "A: svaret som kom medan pumpen sov väckte den och kördes i UI-tråden");

    // ---- B. Workern dör medan svaret ligger i App-kön.
    bool bLevererad = false;
    {
        gt::Worker kort;
        kort.run([] { return 1; }, [&](gt::Result<int>&) { bLevererad = true; });
        kort.wait();
    }
    pumpaEnGång();
    check(!bLevererad, "B: Workern död med svaret i App-kön ⇒ fortsättningen körs ALDRIG");

    // ---- C. Mellanbesked i UI-tråden, i ordning, före svaret.
    std::vector<int>             ordning;
    std::vector<std::thread::id> trådar;
    {
        DoltFönster f;
        gt::Worker  c;
        c.run([&c, &ordning, &trådar](std::stop_token stopp) {
                  for (int i = 0; i < 3; ++i)
                      c.post(stopp, [&ordning, &trådar, i] {
                          ordning.push_back(i);
                          trådar.push_back(std::this_thread::get_id());
                      });
                  return 0;
              },
              [&](gt::Result<int>&) {
                  ordning.push_back(99);
                  trådar.push_back(std::this_thread::get_id());
                  f.close();
              });
        f.run();
    }
    bool allaPåMain = trådar.size() == 4;
    for (auto id : trådar)
        allaPåMain = allaPåMain && id == main;
    check(ordning == std::vector<int>{0, 1, 2, 99},
          "C: tre mellanbesked före svaret, i postordning");
    check(allaPåMain, "C: mellanbesked och svar kördes alla i UI-tråden");

    // ---- D. cancel() medan arbetet kör.
    bool dLevererad = false;
    {
        gt::Worker        d;
        std::atomic<bool> startad{false};
        d.run([&](std::stop_token stopp) {
                  startad = true;
                  väntaPåStopp(stopp, 10s);
                  return 1;
              },
              [&](gt::Result<int>&) { dLevererad = true; });
        while (!startad)
            std::this_thread::sleep_for(1ms);
        d.cancel();
        d.wait();
        pumpaEnGång();
    }
    check(!dLevererad, "D: cancel() medan arbetet kör ⇒ fortsättningen körs aldrig");

    std::printf("\nthreads_ui_gate: %s (%d fel)\n", g_fel ? "FÄLLD" : "GRÖN",
                g_fel);
    return g_fel ? 1 : 0;
}
