// threads_gate — SIDOPAKETET Threads, KÄRNAN.
//
// Kör:  ./build/tests/threads_gate
// Exit: 0 = grönt · 1 = ett fall föll · ctest-timeouten fångar ett häng
//
// Länkar BARA `glaze_threads` — aldrig `glaze`. Att grinden bygger och kör
// är därmed också beviset för att kärnan står fristående.
//
// VAD DEN MÄTER:
//   Pool    FIFO-ordningen (en tråd ⇒ strikt köordning), kapaciteten (högst
//           N samtidigt OCH faktiskt N samtidigt — båda behövs, annars
//           passerar en pool med en enda tråd), rivningen (köade jobb körs
//           exakt en gång med STOPPAD token; körande ser stoppet).
//   Worker  värdet, undantaget, utplattningen av Result, void; avbrytningen
//           (token stoppas, fortsättningen körs ALDRIG, busy() faller
//           direkt); dtorn väntar in KÖRANDE arbete men inte köat; flera
//           samtidiga körningar; mellanbeskeden.
//   Leveransen i en POSTANDE kontext — mätt med `Manuell`, en kontext som
//           köar fortsättningarna tills grinden själv dränerar dem. Det är
//           UI-bryggans form utan glaze: posten sker i pooltråden, körningen
//           i "UI-tråden" (main), och Workern som dör eller avbryter medan
//           svaret ligger i kön ska få det släppt.
//
// VAD DEN INTE MÄTER: den riktiga UI-bryggan (`glaze::app().post`, pumpen,
// självregistreringen) — det gör threads_ui_gate. Tidsgränserna är generösa
// (sekunder mot millisekunder) och mäter "avbröts, väntade inte ut"; de
// mäter inte latensen i sig.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <glaze_threads/threads.h>

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

using Klocka = std::chrono::steady_clock;

double sekunderSedan(Klocka::time_point t0)
{
    return std::chrono::duration<double>(Klocka::now() - t0).count();
}

// Väntar på arbetets token (eller ett tak) — ett arbete som SKÖTER sig.
void väntaPåStopp(std::stop_token stopp, std::chrono::milliseconds tak)
{
    std::mutex                  m;
    std::condition_variable_any cv;
    std::unique_lock<std::mutex> lås(m);
    cv.wait_for(lås, stopp, tak, [] { return false; });
}

// En postande kontext som grinden dränerar själv — UI-pumpens form. Den
// noterar också VILKEN tråd som förberedde (prepare) och vilken som postade.
class Manuell final : public gt::Context {
public:
    void prepare() override { förberedd_ = std::this_thread::get_id(); }

    void post(std::function<void()> fn) override
    {
        std::lock_guard<std::mutex> lås(m_);
        postadFrån_ = std::this_thread::get_id();
        kö_.push_back(std::move(fn));
    }

    // Kör allt som ligger i kön, i den anropande tråden.
    int dränera()
    {
        std::vector<std::function<void()>> nu;
        {
            std::lock_guard<std::mutex> lås(m_);
            nu.swap(kö_);
        }
        for (auto& fn : nu)
            fn();
        return static_cast<int>(nu.size());
    }

    std::thread::id förberedd() const { return förberedd_; }
    std::thread::id postadFrån() const
    {
        std::lock_guard<std::mutex> lås(m_);
        return postadFrån_;
    }

private:
    mutable std::mutex                 m_;
    std::vector<std::function<void()>> kö_;
    std::thread::id                    förberedd_;
    std::thread::id                    postadFrån_;
};

// ------------------------------------------------------------------ Pool

void poolFifo()
{
    std::vector<int> ordning;
    std::mutex       m;
    {
        gt::Pool pool{1};
        check(pool.threadCount() == 1, "Pool{1} har exakt en tråd");
        for (int i = 0; i < 100; ++i)
            pool.post([&, i](std::stop_token) {
                std::lock_guard<std::mutex> lås(m);
                ordning.push_back(i);
            });
    }   // dtorn kör klart kön och joinar
    bool iOrdning = ordning.size() == 100;
    for (int i = 0; iOrdning && i < 100; ++i)
        iOrdning = ordning[static_cast<std::size_t>(i)] == i;
    check(iOrdning, "Pool{1}: 100 jobb körs i exakt köordning (FIFO)");
}

void poolKapacitet()
{
    std::atomic<int> körande{0};
    std::atomic<int> max{0};
    std::atomic<int> klara{0};
    {
        gt::Pool pool{3};
        for (int i = 0; i < 9; ++i)
            pool.post([&](std::stop_token) {
                const int nu = ++körande;
                int m = max.load();
                while (nu > m && !max.compare_exchange_weak(m, nu)) {}
                // Vänta tills tre kör samtidigt (eller ett tak) — så att
                // "nådde 3" bevisas och inte bara "översteg aldrig 3".
                const auto t0 = Klocka::now();
                while (körande.load() < 3 && Klocka::now() - t0 < 2s)
                    std::this_thread::sleep_for(1ms);
                std::this_thread::sleep_for(5ms);
                --körande;
                ++klara;
            });
    }
    check(klara == 9, "Pool{3}: alla nio jobb kördes");
    check(max == 3, "Pool{3}: högst 3 samtidigt — och faktiskt 3 samtidigt");
}

void poolRivning()
{
    std::atomic<int>  körningar{0};
    std::atomic<int>  sågStopp{0};
    std::atomic<bool> långaSågStopp{false};
    std::atomic<bool> långaStartade{false};
    {
        gt::Pool pool{1};
        pool.post([&](std::stop_token stopp) {
            långaStartade = true;
            väntaPåStopp(stopp, 10s);
            långaSågStopp = stopp.stop_requested();
        });
        for (int i = 0; i < 2; ++i)
            pool.post([&](std::stop_token stopp) {
                ++körningar;
                if (stopp.stop_requested())
                    ++sågStopp;
            });
        while (!långaStartade)
            std::this_thread::sleep_for(1ms);
        check(pool.queued() == 2, "queued(): två jobb väntar bakom det långa");
    }
    check(långaSågStopp, "rivning: det KÖRANDE jobbet såg stoppet i sin token");
    check(körningar == 2, "rivning: varje köat jobb kördes EXAKT en gång");
    check(sågStopp == 2, "rivning: de köade körde med en redan stoppad token");
}

// ---------------------------------------------------------------- Worker

void workerGrund()
{
    check(&gt::defaultContext() == &gt::workerContext(),
          "utan UI-bryggan länkad är defaultkontexten pooltråden");

    gt::Pool   pool{2};
    gt::Worker w{pool, gt::workerContext()};
    const auto main = std::this_thread::get_id();

    std::atomic<int>  värde{0};
    std::atomic<bool> iPooltråd{false};
    w.run([] { return 42; }, [&](gt::Result<int>& r) {
        värde     = r ? *r : -1;
        iPooltråd = std::this_thread::get_id() != main;
    });

    std::string fel;
    std::error_code kod;
    w.run([](std::stop_token) -> int { throw std::runtime_error("trasig indata"); },
          [&](gt::Result<int>& r) {
              kod = r.code();
              fel = r.error();
          });

    // Utplattningen: arbetet svarar redan med Result<int> ⇒ fortsättningen
    // får Result<int>, inte Result<Result<int>> (annars kompilerar inte raden).
    std::error_code platt;
    w.run([]() -> gt::Result<int> {
              return gt::Error{gt::Code::Cancelled, "redan avbrutet"};
          },
          [&](gt::Result<int>& r) { platt = r.code(); });

    std::atomic<bool> voidOk{false};
    w.run([] {}, [&](gt::Result<void>& r) { voidOk = r.ok(); });

    w.wait();

    // Default: den delade poolen + defaultkontexten.
    std::atomic<int> standardVärde{0};
    {
        gt::Worker standard;
        standard.run([] { return 3; },
                     [&](gt::Result<int>& r) { standardVärde = *r; });
        standard.wait();
    }

    check(standardVärde == 3, "Worker{} kör på den delade poolen");
    check(värde == 42, "värdet når fortsättningen");
    check(iPooltråd, "workerContext: fortsättningen körs i pooltråden");
    check(kod == gt::Code::Exception, "undantag i arbetet ⇒ Code::Exception");
    check(fel.find("trasig indata") != std::string::npos,
          "undantagets what() finns i felets text");
    check(platt == gt::Code::Cancelled, "Result<T> från arbetet plattas ut");
    check(voidOk, "void-arbete ⇒ lyckat Result<void>");
}

void workerAvbrytning()
{
    gt::Pool   pool{2};
    gt::Worker w{pool, gt::workerContext()};

    std::atomic<bool> levererad{false};
    std::atomic<bool> startad{false};
    std::atomic<bool> sågStopp{false};
    w.run([&](std::stop_token stopp) {
              startad = true;
              väntaPåStopp(stopp, 10s);
              sågStopp = stopp.stop_requested();
              return 1;
          },
          [&](gt::Result<int>&) { levererad = true; });
    while (!startad)
        std::this_thread::sleep_for(1ms);
    check(w.busy(), "busy() medan arbetet kör");

    const auto t0 = Klocka::now();
    w.cancel();
    check(!w.busy(), "busy() faller DIREKT vid cancel()");
    w.wait();
    check(sekunderSedan(t0) < 2.0, "cancel(): arbetet såg stoppet (väntade inte ut 10 s)");
    check(sågStopp, "cancel(): arbetets token är stoppad");
    check(!levererad, "cancel(): den avbrutna körningens fortsättning körs ALDRIG");

    std::atomic<int> efter{0};
    w.run([] { return 7; }, [&](gt::Result<int>& r) { efter = r ? *r : -1; });
    w.wait();
    check(efter == 7, "run() efter cancel() levererar som vanligt");

    // Många samtidiga — var och en med sitt eget svar.
    std::atomic<int> summa{0};
    std::atomic<int> antal{0};
    for (int i = 1; i <= 50; ++i)
        w.run([i] { return i; }, [&](gt::Result<int>& r) {
            summa += *r;
            ++antal;
        });
    w.wait();
    check(antal == 50 && summa == 1275, "50 samtidiga körningar levererar var sitt svar");
    check(!w.busy(), "busy() falsk när allt levererats");
}

void workerDtor()
{
    gt::Pool pool{2};

    // Dtorn VÄNTAR in körande arbete — även ett som struntar i sin token.
    std::atomic<bool> startad{false};
    std::atomic<bool> klart{false};
    std::atomic<bool> levererad{false};
    {
        gt::Worker w{pool, gt::workerContext()};
        w.run([&] {
                  startad = true;
                  std::this_thread::sleep_for(200ms);
                  klart = true;
              },
              [&](gt::Result<void>&) { levererad = true; });
        while (!startad)
            std::this_thread::sleep_for(1ms);
    }
    check(klart, "dtorn väntade in det KÖRANDE arbetet (jthread-semantik)");
    check(!levererad, "dtorn: en körning som avslutas under rivningen levererar inte");

    // …men inte köat arbete: poolen är upptagen, körningen hinner inte starta.
    // Synkobjekten står FÖRE poolen så att de överlever dess trådar.
    std::mutex              m;
    std::condition_variable cv;
    bool                    släpp = false;
    std::atomic<bool>       köadKörd{false};
    gt::Pool                ensam{1};
    ensam.post([&](std::stop_token) {
        std::unique_lock<std::mutex> lås(m);
        cv.wait(lås, [&] { return släpp; });
    });
    const auto t0 = Klocka::now();
    {
        gt::Worker w{ensam, gt::workerContext()};
        w.run([&] { köadKörd = true; }, [](gt::Result<void>&) {});
    }
    check(sekunderSedan(t0) < 1.0, "dtorn väntar INTE på en körning som bara är köad");
    {
        std::lock_guard<std::mutex> lås(m);
        släpp = true;
    }
    cv.notify_all();
    // Poolen dräneras i sin dtor längre ned; vänta in kön här i stället.
    while (ensam.queued() > 0)
        std::this_thread::sleep_for(1ms);
    std::this_thread::sleep_for(20ms);
    check(!köadKörd, "den köade körningen startade aldrig efter att Workern dött");
}

// Leveransen i en POSTANDE kontext — UI-bryggans form, utan glaze.
void postandeKontext()
{
    gt::Pool    pool{2};
    Manuell     ui;
    const auto  main = std::this_thread::get_id();

    {
        gt::Worker w{pool, ui};
        std::thread::id kördI;
        int             värde = 0;
        w.run([] { return 5; }, [&](gt::Result<int>& r) {
            kördI = std::this_thread::get_id();
            värde = *r;
        });
        w.wait();
        check(ui.förberedd() == main, "prepare() körs på den STARTANDE tråden");
        check(ui.postadFrån() != main, "svaret postas från pooltråden");
        check(värde == 0, "inget körs förrän kontexten dränerar (ingen egen loop)");
        check(w.busy(), "busy() tills svaret faktiskt levererats");
        check(ui.dränera() == 1 && värde == 5 && kördI == main,
              "fortsättningen körs i kontextens tråd när den dräneras");
        check(!w.busy(), "busy() faller när fortsättningen körts");
    }

    // Workern DÖR medan svaret ligger i kön ⇒ svaret släpps.
    bool levererad = false;
    {
        gt::Worker w{pool, ui};
        w.run([] { return 1; }, [&](gt::Result<int>&) { levererad = true; });
        w.wait();
    }
    ui.dränera();
    check(!levererad, "Workern död innan dräneringen ⇒ fortsättningen körs ALDRIG");

    // cancel() medan svaret ligger i kön ⇒ svaret släpps.
    {
        gt::Worker w{pool, ui};
        bool avbrutenLevererad = false;
        w.run([] { return 1; }, [&](gt::Result<int>&) { avbrutenLevererad = true; });
        w.wait();
        w.cancel();
        ui.dränera();
        check(!avbrutenLevererad, "cancel() efter posten men före dräneringen ⇒ släppt");
    }

    // Mellanbesked: postade från arbetet, i ordning före svaret, och
    // släppta när cancel() kommer medan de ligger i kön.
    {
        std::vector<int> ordning;
        std::vector<int> besked;
        gt::Worker       w{pool, ui};
        w.run([&w, &ordning](std::stop_token stopp) {
                  for (int i = 0; i < 3; ++i)
                      w.post(stopp, [&ordning, i] { ordning.push_back(i); });
                  return 0;
              },
              [&](gt::Result<int>&) { ordning.push_back(99); });
        w.wait();
        check(ui.dränera() == 4 && ordning == std::vector<int>{0, 1, 2, 99},
              "tre mellanbesked FÖRE svaret, i postordning");

        std::atomic<bool> startad{false};
        w.run([&](std::stop_token stopp) {
                  w.post(stopp, [&] { besked.push_back(1); });
                  startad = true;
                  väntaPåStopp(stopp, 10s);
              },
              [](gt::Result<void>&) {});
        while (!startad)
            std::this_thread::sleep_for(1ms);
        w.cancel();
        w.wait();
        ui.dränera();
        check(besked.empty(), "mellanbesked i kön när cancel() kommer ⇒ släppt");
    }
}

} // namespace

int main()
{
    std::printf("=== threads_gate — sidopaketet Threads, kärnan ===\n");
    poolFifo();
    poolKapacitet();
    poolRivning();
    workerGrund();
    workerAvbrytning();
    workerDtor();
    postandeKontext();
    std::printf("\nthreads_gate: %s (%d fel)\n", g_fel ? "FÄLLD" : "GRÖN", g_fel);
    return g_fel ? 1 : 0;
}
