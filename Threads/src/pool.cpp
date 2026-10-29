// glaze_threads — trådpoolen. Kontraktet (FIFO, kapacitet = trådantal,
// rivningens tre steg) står i pool.h.

#include <algorithm>
#include <system_error>

#include "glaze_threads/pool.h"

namespace glaze::threads {

Pool::Pool(unsigned threads)
{
    if (threads == 0)
        threads = std::max(2u, std::thread::hardware_concurrency());
    threads_.reserve(threads);
    try {
        for (unsigned i = 0; i < threads; ++i)
            threads_.emplace_back([this] { loop(); });
    } catch (const std::system_error&) {
        // Trådarna som hann starta kör `loop()` på ett objekt vars dtor
        // aldrig kommer att köras (konstruktorn kastar) — de måste
        // stoppas och joinas HÄR, annars är deras std::thread-dtor
        // `std::terminate`. Felet går sedan vidare till anroparen.
        {
            std::lock_guard<std::mutex> lås(m_);
            stopping_ = true;
        }
        cv_.notify_all();
        for (auto& t : threads_)
            t.join();
        throw;
    }
}

Pool::~Pool()
{
    {
        std::lock_guard<std::mutex> lås(m_);
        stopping_ = true;
    }
    // UTANFÖR låset: `request_stop` kör jobbens stop_callbacks synkront i
    // den här tråden, och en callback som postar ett nytt jobb hade annars
    // låst sig själv.
    stop_.request_stop();
    cv_.notify_all();
    for (auto& t : threads_)
        t.join();
}

void Pool::post(std::function<void(std::stop_token)> job)
{
    {
        std::lock_guard<std::mutex> lås(m_);
        queue_.push_back(std::move(job));
    }
    cv_.notify_one();
}

std::size_t Pool::queued() const
{
    std::lock_guard<std::mutex> lås(m_);
    return queue_.size();
}

// Trådarnas loop. Tar jobb i köordning tills poolen rivs OCH kön är tom —
// ett köat jobb körs alltså alltid, vid rivning med en stoppad token (se
// rivningens andra steg i pool.h). Jobbet körs utanför låset.
void Pool::loop()
{
    for (;;) {
        std::function<void(std::stop_token)> job;
        {
            std::unique_lock<std::mutex> lås(m_);
            cv_.wait(lås, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty())
                return;   // rivning och inget kvar
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        job(stop_.get_token());
    }
}

// AVSIKTLIGT LÄCKT, i båda lägena — poolen
// rivs aldrig. I delat läge hade `~Pool` körts i libglaze_threads
// DLL_PROCESS_DETACH, efter att `ExitProcess` dödat processens trådar: dtorn
// tar `m_` och begär stopp före join, och en tråd som dödades med låset i
// handen hänger då processen. Ingenting går förlorat på att den aldrig rivs:
// ägarna — varje `Worker` — har dött före `main` returnerat (Threads-
// kontraktet), köade fristående jobb överges vid processlut, och OS:et tar
// trådarna, som då väntar i sin kö.
Pool& defaultPool()
{
    static Pool& pool = *new Pool;
    return pool;
}

} // namespace glaze::threads
