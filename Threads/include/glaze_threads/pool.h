#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

// glaze_threads — TRÅDPOOLEN.
//
// Ett fast antal trådar som tar jobb ur EN kö i den ordning de lades dit
// (FIFO). Det std saknar: `std::async` startar i libstdc++ en ny tråd per
// anrop utan tak, och `std::jthread` är en tråd, inte en pool.
//
// KAPACITETEN är trådantalet — högst så många jobb körs samtidigt, resten
// väntar i kön. Med EN tråd körs jobben alltså strikt i tur och ordning,
// vilket är det enkla sättet att få "en egen tråd" för ett långlivat jobb
// (en lyssnarloop) utan att låna en av den delade poolens trådar:
//
//     glaze::threads::Pool lyssnarTråd{1};
//     glaze::threads::Worker lyssnare{lyssnarTråd};
//
// Ett jobb får en `std::stop_token` som begärs när poolen RIVS. Rivningen:
//   1. stopp begärs för alla jobb (de som kör ser det i sin token),
//   2. kön töms — men VARJE köat jobb körs ändå, EXAKT en gång, med en redan
//      stoppad token. Ett jobb ska alltså titta på token först. Skälet är
//      att ägaren (t.ex. en `Worker`) alltid ska få veta att jobbet tagit
//      slut; ett jobb som bara försvann hade lämnat dess räknare hängande,
//   3. trådarna joinas — dtorn väntar tills varje jobb är ute ur sin tråd.
//
// Ett jobb får inte kasta: ett undantag som lämnar jobbet är
// `std::terminate`, precis som för `std::thread` — högljutt, aldrig tyst.
// `Worker` fångar arbetets undantag åt en och gör dem till ett `Result`.

namespace glaze::threads {

class Pool {
public:
    // 0 = `std::thread::hardware_concurrency()`, minst 2 — en pool med en
    // enda tråd ska vara ett VAL, aldrig något en enkärnig VM råkar ge.
    explicit Pool(unsigned threads = 0);
    ~Pool();
    Pool(const Pool&)            = delete;
    Pool& operator=(const Pool&) = delete;

    // Lägger jobbet SIST i kön. Trådsäker; får anropas från ett jobb.
    void post(std::function<void(std::stop_token)> job);

    unsigned    threadCount() const { return static_cast<unsigned>(threads_.size()); }
    std::size_t queued() const;   // köade, ännu inte startade

private:
    void loop();

    mutable std::mutex                                m_;
    std::condition_variable                           cv_;
    std::deque<std::function<void(std::stop_token)>> queue_;
    std::stop_source                                  stop_;
    bool                                              stopping_ = false;
    std::vector<std::thread>                          threads_;
};

// Den delade poolen — skapas vid första användningen och rivs ALDRIG: den
// lever till processlut, och OS:et tar dess trådar (avsiktligt läckt — skälet
// står i pool.cpp). `Worker` använder den när ingen pool anges.
Pool& defaultPool();

} // namespace glaze::threads
