#pragma once
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <utility>

#include "glaze_threads/context.h"
#include "glaze_threads/pool.h"
#include "glaze_threads/result.h"

// glaze_threads — WORKER: bakgrundsarbete med svar.
//
// WinForms `BackgroundWorker` utan `object`: ett ARBETE körs i en pooltråd,
// och dess resultat — värde eller fel — levereras till en FORTSÄTTNING i
// Workerns kontext, vilket i en GUI-app är UI-tråden:
//
//     class KartFönster : public glaze::AppWindow {
//         ...
//         glaze::threads::Worker bakgrund;   // dör med fönstret
//     };
//
//     void KartFönster::räknaClicked(glaze::Widget*, glaze::ClickArgs&)
//     {
//         bakgrund.run([n = antal.value()](std::stop_token stop) {   // POOLTRÅD
//             return räknaUt(n, stop);
//         }, [this](glaze::threads::Result<Rapport>& r) {            // UI-TRÅDEN
//             status.setText(r ? r->text : r.error());
//         });
//     }
//
// DET HÄR TILLFÖR PAKETET utöver `std::jthread`/`std::async` — fyra saker
// som var och en är en klassisk bugg i handskriven trådkod:
//
//   1. SVARET NÅR UI-TRÅDEN via kontexten — i en GUI-app `glaze::app().post`.
//      Paketet uppfinner ingen egen brygga.
//   2. FORTSÄTTNINGEN KÖRS ALDRIG EFTER ÄGAREN. `[this]` i fortsättningen är
//      säkert: när Workern (= fönstret) dör slutar den leverera, även svar
//      som redan ligger i UI-kön.
//   3. UNDANTAG I ARBETET FÅNGAS och blir ett fel i `Result` — ett undantag
//      som lämnar en tråd är annars `std::terminate`.
//   4. AVBRYTNING UTAN STALE-UI: `cancel()` begär stopp i arbetets
//      `std::stop_token` OCH lovar att de avbrutna körningarnas
//      fortsättningar ALDRIG körs. Den som avbröt vet om det; ett "avbrutet"
//      som droppar in efter att en ny körning startat hade skrivit över den.
//
// KONTRAKTEN — läs dem innan du skriver ett arbete:
//
// - ARBETET FÅR BARA FÅNGA VÄRDEN. Det körs i en annan tråd och får aldrig
//   röra fönstret eller en widget, inte ens läsa. Kopiera in det som behövs
//   (`[n = antal.value()]`). Fortsättningen får röra allt.
// - Arbetet tar `std::stop_token` (eller inget) och returnerar T, void eller
//   ett `Result<T>` — det SISTA PLATTAS UT, så ett `glaze::net`-anrop som
//   redan svarar `Result<T>` blir `Result<T>` i fortsättningen, aldrig
//   `Result<Result<T>>`.
// - Fortsättningen tar `Result<T>&` och är OBLIGATORISK. Utan den hade ett
//   fel inte haft någonstans att ta vägen — och ett tyst svalt fel är precis
//   det paketet finns för att förhindra.
// - FLERA KÖRNINGAR SAMTIDIGT är normalt: `run` köar en till och varje
//   körning levererar sitt eget svar. "Senaste vinner" är `cancel(); run(…);`.
// - DTORN avbryter allt och VÄNTAR på körningar som just nu kör (jthread-
//   semantik) — arbetet ska alltså titta på sin token. Köade körningar som
//   inte hunnit starta väntar den inte på; de startar aldrig.
// - Workern ska vara en MEDLEM (fönster, dialog) eller ett lokalt objekt,
//   aldrig en global. Den måste dö före `glaze::App` (app.h:s gräns för
//   `post`), och det gör den bara om den lever i något som dör innan main
//   returnerar. Samma sak gäller en egen `Pool`: den ska leva längre än
//   varje Worker som använder den.

namespace glaze::threads {

namespace detail {

// Arbetets råa returtyp — med eller utan stop_token.
template <class W>
using WorkReturn =
    typename std::conditional_t<std::is_invocable_v<W&, std::stop_token>,
                                std::invoke_result<W&, std::stop_token>,
                                std::invoke_result<W&>>::type;

// `Result<U>` plattas ut till U; allt annat är sig självt.
template <class R>
struct Unwrap {
    using type = R;
};
template <class U>
struct Unwrap<Result<U>> {
    using type = U;
};

template <class W>
using ValueType = typename Unwrap<std::remove_cvref_t<WorkReturn<W>>>::type;

// Det pågående undantaget som fel — anropas BARA inifrån en catch-gren.
Error currentException();

// Arbetets stopp kopplat till Workerns och poolens: begärs någon av dem,
// begärs arbetets. En namngiven funktor (inte en lambda) eftersom
// `std::stop_callback<F>` behöver typen som mallargument.
struct ForwardStop {
    std::stop_source* target;
    void operator()() const noexcept { target->request_stop(); }
};

// En körnings TYPLÖSA yta — så att räknarna, låsen och leveransen kan bo i
// worker.cpp i stället för i varje mallinstans.
class RunBase {
public:
    virtual ~RunBase() = default;

    // Pooltråden: kör arbetet och spara resultatet. Arbetsfunktorn — och
    // därmed dess fångster — släpps HÄR, innan Workerns dtor får gå vidare.
    virtual void work(std::stop_token stop) = 0;

    // Kontexten: kör fortsättningen med resultatet.
    virtual void deliver() = 0;

    // Kopplar arbetets token till Workerns generation och till poolen.
    // Kopplingen lever lika länge som körningen — inte bara medan arbetet
    // kör — så att ett mellanbesked (`Worker::post`) som ligger i UI-kön
    // när `cancel()` kommer ser stoppet och släpps.
    void link(const std::stop_token& worker, const std::stop_token& pool)
    {
        viaWorker_.emplace(worker, ForwardStop{&stop_});
        viaPool_.emplace(pool, ForwardStop{&stop_});
    }
    std::stop_token token() const { return stop_.get_token(); }

private:
    // Ordningen är bärande: callbackarna avregistreras (dör) FÖRE källan
    // de pekar på.
    std::stop_source                                  stop_;
    std::optional<std::stop_callback<ForwardStop>> viaWorker_;
    std::optional<std::stop_callback<ForwardStop>> viaPool_;
};

template <class Work, class Done>
class Run final : public RunBase {
public:
    using T = ValueType<Work>;

    Run(Work work, Done done) : work_(std::move(work)), done_(std::move(done)) {}

    void work(std::stop_token stop) override
    {
        result_.emplace(invoke(std::move(stop)));
        work_.reset();
    }

    void deliver() override
    {
        (*done_)(*result_);
        done_.reset();
    }

private:
    Result<T> invoke(std::stop_token stop)
    {
        try {
            if constexpr (std::is_void_v<WorkReturn<Work>>) {
                call(std::move(stop));
                return Result<void>{};
            } else {
                // T → Result<T> via den konverterande konstruktorn,
                // Result<T> → sig självt (utplattningen).
                return call(std::move(stop));
            }
        } catch (...) {
            return currentException();
        }
    }

    decltype(auto) call(std::stop_token stop)
    {
        if constexpr (std::is_invocable_v<Work&, std::stop_token>)
            return std::invoke(*work_, std::move(stop));
        else
            return std::invoke(*work_);
    }

    std::optional<Work>      work_;
    std::optional<Done>      done_;
    std::optional<Result<T>> result_;
};

// Delad mellan Worker-objektet, jobben i poolen och leveranserna i
// kontexten — därför shared_ptr: en leverans som ligger i UI-kön när
// Workern dör håller kärnan vid liv och ser `alive == false`.
struct WorkerCore {
    std::mutex              m;
    std::condition_variable cv;
    Context*                context = nullptr;
    bool                    alive   = true;
    std::stop_source        stop;              // AKTUELL generation
    unsigned long           generation = 0;    // cancel() stegar
    int                     inflight   = 0;    // köade + körande (wait)
    int                     executing  = 0;    // körande just nu (dtorn)
    int                     pending    = 0;    // aktuell generations olevererade (busy)
};

} // namespace detail

class Worker {
public:
    Worker();                                  // defaultPool + defaultContext
    explicit Worker(Pool& pool);
    explicit Worker(Context& context);
    Worker(Pool& pool, Context& context);
    ~Worker();
    Worker(const Worker&)            = delete;
    Worker& operator=(const Worker&) = delete;

    // Startar ett arbete. Trådsäker. `done(Result<T>&)` körs i kontexten när
    // arbetet är klart — om Workern lever och körningen inte avbrutits.
    template <class Work, class Done>
    void run(Work work, Done done)
    {
        using T = detail::ValueType<Work>;
        static_assert(std::is_invocable_v<Done&, Result<T>&>,
                      "fortsättningen ska ta (glaze::threads::Result<T>&) "
                      "där T är arbetets returtyp");
        submit(std::make_shared<detail::Run<Work, Done>>(std::move(work),
                                                         std::move(done)));
    }

    // Avbryter ALLA körningar som startats hittills: deras token stoppas och
    // deras fortsättningar och mellanbesked körs aldrig. Nya `run` efteråt
    // fungerar som vanligt. Väntar inte.
    void cancel();

    // Finns en oavbruten körning vars svar inte levererats än? Falsk igen i
    // samma ögonblick som sista fortsättningen börjar köras — och direkt
    // efter `cancel()`, för de avbrutna levererar aldrig.
    bool busy() const;

    // Blockerar tills varje startad körning lämnat sin pooltråd (arbetet
    // klart, och fortsättningen körd i `workerContext` eller postad i övriga).
    // För kommandoradsverktyg och grindar — ALDRIG på UI-tråden, det är
    // exakt det hård regel 1 förbjuder. Anropad inifrån Workerns eget arbete
    // väntar den på allt UTOM sig själv.
    void wait();

    // MELLANBESKED från arbetet: kör `fn` i kontexten (förlopp, delresultat).
    // Släpps om Workern dött eller `stop` begärts när det är dags — skicka
    // arbetets egen token, så följer beskedet körningen:
    //
    //     bakgrund.run([this](std::stop_token stop) {
    //         for (int i = 0; i < 100 && !stop.stop_requested(); ++i) {
    //             steg(i);
    //             bakgrund.post(stop, [this, i] { förlopp.setValue(i); });
    //         }
    //     }, [this](glaze::threads::Result<void>&) { förlopp.setValue(100); });
    //
    // `this` i ARBETET är här bara en väg till `bakgrund` — Workern lever
    // garanterat medan arbetet kör, för dtorn väntar på det. Själva
    // widgeten rörs bara i `fn`, i UI-tråden.
    template <class Fn>
    void post(std::stop_token stop, Fn fn)
    {
        auto delad = std::make_shared<Fn>(std::move(fn));
        postImpl(std::move(stop), [delad] { (*delad)(); });
    }

private:
    void submit(std::shared_ptr<detail::RunBase> run);
    void postImpl(std::stop_token stop, std::function<void()> fn);

    std::shared_ptr<detail::WorkerCore> core_;
    Pool* pool_ = nullptr;   // nullptr = defaultPool(), löses vid första run
};

} // namespace glaze::threads
