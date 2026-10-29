// TIMERFD-KÄLLAN och APPENS TIMER PÅ LINUX.
//
// `TimerSource` (linux_backend.h) är källan som både fönstrens timers
// (`LinuxWindow::startTimer`, linux_platform.cpp) och appens timer går
// genom. Den är skild från fönstrets startTimer, så att armeringen, dräneringen
// och felvägarna står EN gång: en timerfd i pumpens epoll, som väcker
// `waitEvent` per konstruktion — ingen tråd, och samma väg för Wayland, X11
// och headless.
//
// FELVÄGARNA: `timerfd_settime`s svar kontrolleras
// och `addPollSource` svarar med bool. En källa som skapats men inte armats,
// eller inte står i epollen, väcker aldrig pumpen — och en `glaze::Timer` som
// tror att den går hade då sovit för evigt. `make` stänger fd:n och svarar
// nullptr, och `startAppTimer` svarar false.
//
// APPENS TIMER: en karta id → källa, säker före init och efter shutdown
// (svarar false respektive är en no-op) genom displayen: utan en gällande
// `LinuxDisplay` pumpas ingenting. `appTimerShutdown()` tömmer kartan i
// `gfx::shutdown`.

#include <sys/timerfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <map>
#include <memory>

#include "gfx/platform.h"
#include "gfx/timer.h"
#include "linux_backend.h"

namespace gfx {

// ---------------------------------------------------------------- källan

std::unique_ptr<TimerSource> TimerSource::make(WindowId wid, std::uint64_t id,
                                               std::uint32_t ms)
{
    if (ms < 1)
        ms = 1;   // it_value 0 avväpnar timerfd — då kom aldrig en tick
    const int fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0) {
        log("timer: timerfd_create misslyckades (errno %d)", errno);
        return nullptr;
    }
    itimerspec its{};
    its.it_value.tv_sec  = static_cast<time_t>(ms / 1000);
    its.it_value.tv_nsec = static_cast<long>(ms % 1000) * 1000000L;
    its.it_interval      = its.it_value;
    if (::timerfd_settime(fd, 0, &its, nullptr) != 0) {
        log("timer: timerfd_settime misslyckades (errno %d)", errno);
        ::close(fd);
        return nullptr;
    }

    auto t         = std::make_unique<TimerSource>();
    t->src.fd      = fd;
    t->src.onReady = [fd, wid, id] {
        std::uint64_t n = 0;
        // Dränera räknaren; koalescera missade ticks till ETT event.
        while (::read(fd, &n, sizeof n) > 0) {
        }
        Event e;
        e.type     = EventType::Timer;
        e.windowId = wid;
        e.timerId  = id;
        pushEvent(e);
    };
    if (!addPollSource(&t->src)) {
        ::close(fd);   // `t->fd` är fortfarande -1 — destruktorn rör inget
        return nullptr;
    }
    t->fd = fd;
    return t;
}

TimerSource::~TimerSource()
{
    if (fd < 0)
        return;
    removePollSource(&src);
    ::close(fd);
}

// ---------------------------------------------------------------- appens

namespace {

// Appens källor per id. I NAMNRYMDEN och inte som en lokal static med flit,
// av systemfältets skäl på de andra plattformarna: kartan konstrueras före
// main och rivs därför EFTER glaze::App, så att `appTimerShutdown` — från
// ~App via gfx::shutdown — aldrig rör en riven karta.
std::map<AppTimerId, std::unique_ptr<TimerSource>> g_appTimers;

} // namespace

bool startAppTimer(AppTimerId id, std::uint32_t ms)
{
    if (!LinuxDisplay::current() || id == 0)
        return false;   // före init, efter shutdown eller id 0 (gfx/timer.h)
    stopAppTimer(id);   // samma id = omstartad period
    std::unique_ptr<TimerSource> t = TimerSource::make(0, id, ms);
    if (!t)
        return false;   // loggat i make — timern går INTE
    g_appTimers[id] = std::move(t);
    return true;
}

void stopAppTimer(AppTimerId id)
{
    // Källans destruktor tar den ur epollen och stänger fd:n. Ett okänt id —
    // och varje id efter shutdown — är en no-op.
    g_appTimers.erase(id);
}

void appTimerShutdown()
{
    g_appTimers.clear();
}

} // namespace gfx
