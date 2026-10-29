// glaze_threads — Workerns maskineri: räknarna, låsen och leveransen.
// Kontraktet står i worker.h; här står VARFÖR ordningen är den den är.
//
// TRE RÄKNARE, för tre olika frågor:
//   inflight   köade + körande — `wait()` väntar på 0
//   executing  körande JUST NU — dtorn väntar på 0 (köade startar aldrig)
//   pending    aktuell generations olevererade — `busy()`; nollas av cancel
//
// GENERATIONEN är det som gör avbrytningslöftet vattentätt. En körning
// levererar bara om dess generation fortfarande är Workerns — kollat under
// låset i ögonblicket före leveransen, i den tråd där leveransen sker.
// Stopptoken räcker inte ensam: `cancel()` begär stoppet UTANFÖR låset
// (se där), och i glipan hade en körning kunnat se en ännu ostoppad token.

#include <exception>

#include "glaze_threads/worker.h"

namespace glaze::threads {
namespace detail {
namespace {

// Vilken Worker den här tråden just nu kör arbete (eller en fortsättning i
// workerContext) åt. Läses av dtorn och `wait()`: en Worker som rivs eller
// väntas på inifrån sitt EGET arbete får inte vänta på sig själv — det
// vore ett dödläge, inte ett fel som syns.
thread_local const WorkerCore* t_inuti = nullptr;

class Inuti {
public:
    explicit Inuti(const WorkerCore* c) : förra_(t_inuti) { t_inuti = c; }
    ~Inuti() { t_inuti = förra_; }
    Inuti(const Inuti&)            = delete;
    Inuti& operator=(const Inuti&) = delete;

private:
    const WorkerCore* förra_;
};

// En körning når ett SLUTLÄGE (levererad eller släppt). Den räknas bara
// av om den hör till aktuell generation — de avbrutna nollades redan av
// `cancel()`.
void settle(WorkerCore& c, unsigned long gen)
{
    if (gen == c.generation && c.pending > 0)
        --c.pending;
}

bool levande(const WorkerCore& c, unsigned long gen,
             const std::stop_token& poolStopp)
{
    return c.alive && gen == c.generation && !poolStopp.stop_requested();
}

// Ett jobb i poolen. Körs i en pooltråd.
void execute(const std::shared_ptr<WorkerCore>& core,
             const std::shared_ptr<RunBase>& run, unsigned long gen,
             const std::stop_token& workerStopp,
             const std::stop_token& poolStopp)
{
    // 1. Avbruten innan den hann starta (Workern död, cancel() eller
    //    poolen som rivs) — arbetet körs aldrig. Fångsterna dör när sista
    //    shared_ptr till körningen släpps.
    {
        std::lock_guard<std::mutex> lås(core->m);
        if (!levande(*core, gen, poolStopp)) {
            settle(*core, gen);
            --core->inflight;
            core->cv.notify_all();
            return;
        }
        ++core->executing;
    }

    // 2. Arbetet, med en token som stoppar på BÅDE Workern och poolen.
    run->link(workerStopp, poolStopp);
    {
        Inuti inuti(core.get());
        run->work(run->token());
    }

    // 3. Leveransen. Kontrollen görs IGEN: arbetet kan ha tagit lång tid.
    bool leverera = false;
    {
        std::lock_guard<std::mutex> lås(core->m);
        leverera = levande(*core, gen, poolStopp);
        if (!leverera)
            settle(*core, gen);
    }
    if (leverera) {
        Context& kontext = *core->context;
        if (kontext.runsInline()) {
            // Fortsättningen körs här, i pooltråden, UTANFÖR låset (den får
            // anropa run/cancel på samma Worker) — men fortfarande räknad i
            // `executing`, så dtorn väntar in den.
            {
                std::lock_guard<std::mutex> lås(core->m);
                settle(*core, gen);
            }
            Inuti inuti(core.get());
            run->deliver();
        } else {
            // Postas UTANFÖR låset. Det är säkert för att `executing` räknas
            // ned först EFTER posten: Workerns dtor väntar alltså in den,
            // och efter dtorn postar ingen körning något mer — exakt det
            // app.h:s gräns kräver (postande trådar klara innan App rivs).
            // Leveransen själv frågar en sista gång, i kontextens tråd:
            // Workern kan ha dött eller avbrutits medan svaret låg i kön.
            kontext.post([core, run, gen, poolStopp] {
                {
                    std::lock_guard<std::mutex> lås(core->m);
                    const bool ok = levande(*core, gen, poolStopp);
                    settle(*core, gen);
                    if (!ok)
                        return;
                }
                run->deliver();
            });
        }
    }

    {
        std::lock_guard<std::mutex> lås(core->m);
        --core->executing;
        --core->inflight;
    }
    core->cv.notify_all();
}

} // namespace

Error currentException()
{
    try {
        throw;
    } catch (const std::exception& e) {
        return {Code::Exception, std::string("arbetet kastade: ") + e.what()};
    } catch (...) {
        return {Code::Exception, "arbetet kastade ett okänt undantag"};
    }
}

} // namespace detail

Worker::Worker() : Worker(defaultContext()) {}

Worker::Worker(Pool& pool) : Worker(pool, defaultContext()) {}

Worker::Worker(Context& context)
    : core_(std::make_shared<detail::WorkerCore>())
{
    core_->context = &context;
}

Worker::Worker(Pool& pool, Context& context) : Worker(context)
{
    pool_ = &pool;
}

Worker::~Worker()
{
    std::stop_source stopp;
    {
        std::lock_guard<std::mutex> lås(core_->m);
        core_->alive = false;
        stopp        = core_->stop;   // delar tillstånd — stoppas nedan
    }
    // UTANFÖR låset av samma skäl som i cancel().
    stopp.request_stop();

    std::unique_lock<std::mutex> lås(core_->m);
    const int själv = detail::t_inuti == core_.get() ? 1 : 0;
    core_->cv.wait(lås, [&] { return core_->executing <= själv; });
}

void Worker::submit(std::shared_ptr<detail::RunBase> run)
{
    // Kontexten förbereds på den STARTANDE tråden — och FÖRE den delade
    // poolen skapas. UI-bryggan skapar här `glaze::App`, alltså på UI-tråden
    // och aldrig i en pooltråd. Den delade poolen rivs aldrig (pool.cpp): vid
    // processlut väntar dess trådar i kön, och ingen av dem rör App — varje
    // Worker, och därmed varje körning, har dött före `main` returnerat.
    core_->context->prepare();
    Pool& pool = pool_ ? *pool_ : defaultPool();

    unsigned long   gen = 0;
    std::stop_token workerStopp;
    {
        std::lock_guard<std::mutex> lås(core_->m);
        ++core_->inflight;
        ++core_->pending;
        gen         = core_->generation;
        workerStopp = core_->stop.get_token();
    }
    pool.post([core = core_, run = std::move(run), gen,
               workerStopp](std::stop_token poolStopp) {
        detail::execute(core, run, gen, workerStopp, poolStopp);
    });
}

void Worker::cancel()
{
    std::stop_source gammal;
    {
        std::lock_guard<std::mutex> lås(core_->m);
        gammal = std::exchange(core_->stop, std::stop_source{});
        ++core_->generation;
        core_->pending = 0;
    }
    // UTANFÖR låset: `request_stop` kör varje registrerad stop_callback
    // synkront HÄR — även de som ett arbete själv registrerat (ett
    // nätverksanrop som väcker sin väntan). En sådan callback som rör
    // Workern (t.ex. `post`) hade annars låst sig själv.
    gammal.request_stop();
}

bool Worker::busy() const
{
    std::lock_guard<std::mutex> lås(core_->m);
    return core_->pending > 0;
}

void Worker::wait()
{
    std::unique_lock<std::mutex> lås(core_->m);
    const int själv = detail::t_inuti == core_.get() ? 1 : 0;
    core_->cv.wait(lås, [&] { return core_->inflight <= själv; });
}

void Worker::postImpl(std::stop_token stop, std::function<void()> fn)
{
    {
        std::lock_guard<std::mutex> lås(core_->m);
        if (!core_->alive || stop.stop_requested())
            return;
    }
    Context& kontext = *core_->context;
    if (kontext.runsInline()) {
        fn();
        return;
    }
    kontext.post([core = core_, stop = std::move(stop), fn = std::move(fn)] {
        {
            std::lock_guard<std::mutex> lås(core->m);
            if (!core->alive || stop.stop_requested())
                return;
        }
        fn();
    });
}

} // namespace glaze::threads
