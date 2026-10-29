// APPENS TIMER — `glaze::Timer`, registret
// och leveransen i pump-svansen. Kontraktet utåt står i glaze/timer.h,
// kopplingen till App i src/timer_registry.h och fasaden i gfx/timer.h.
//
// Tre saker att inte råka bryta:
//
//   1. DISPATCHEN ANTECKNAR, SVANSEN LEVERERAR. `App::dispatch` kör
//      aldrig en handler för en tick — den antecknar körningen som förfallen,
//      EN gång. En handler som tar längre tid än intervallet hade annars
//      svält ritningen (pumpen tömmer hela kön innan den ritar), och Windows
//      modala flytt-loop köar en tick per period: anteckningen gör det till
//      EN leverans efter släppet.
//   2. VARJE KÖRNING BÄR SIN STÄMPEL. En förfallen tick är
//      ett par (körnings-id, stämpel), och leveransen prövar PRECIS före
//      anropet att körningen fortfarande går och att stämpeln är timerns
//      aktuella. Den är det inte när en nästlad pump (en modal i en tidigare
//      handler) redan levererat ticken — då hade ytterbatchen annars
//      levererat den en gång till — eller när timern stoppats, startats om
//      eller rivits under tiden.
//   3. INGET GFX-ANROP FÖRE FÖRSTA `start()`. Konstruktorn rör bara
//      registret, så en `Timer` i previewn är ofarlig per konstruktion.

#include "glaze/timer.h"

#include <cstdint>
#include <deque>
#include <map>

#include <gfx/platform.h>
#include <gfx/timer.h>

#include "glaze/app.h"
#include "prop_parse.h"
#include "timer_registry.h"

namespace glaze {

GLAZE_REGISTER_COMPONENT("glaze::Timer", Timer, ::glaze::PreviewForm::Inert)

namespace detail {

namespace {

// En FÖRFALLEN körning — som den antecknades.
struct Förfallen {
    std::uint64_t run   = 0;
    std::uint64_t stamp = 0;
};

// REGISTRET — körnings-id → timer, räknarna och de förfallna i
// anteckningsordning. Skapas av den första `Timer`-konstruktorn och
// överlever därmed varje timer.
struct Registret {
    std::map<std::uint64_t, Timer*> running;
    std::uint64_t                   nextRun   = 0;   // 64 bitar — slår aldrig om
    std::uint64_t                   nextStamp = 0;
    std::deque<Förfallen>           due;
};

Registret& registret()
{
    static Registret r;
    return r;
}

} // namespace

// Maskineriet — den ENDA vännen till `Timer`.
struct TimerState {
    static void noteDue(const gfx::Event& e);
    static void drain();
    static void stopRun(Timer& t);
    static std::uint64_t runIdOf(const Timer& t) { return t.runId_; }
};

void TimerState::stopRun(Timer& t)
{
    if (t.runId_ == 0)
        return;   // inget gfx-anrop för en timer som aldrig startats
    gfx::stopAppTimer(t.runId_);
    registret().running.erase(t.runId_);
    t.runId_    = 0;
    t.dueStamp_ = 0;   // en antecknad tick för körningen är död
}

void TimerState::noteDue(const gfx::Event& e)
{
    if (e.windowId != 0)
        return;   // ett fönsters timer (vars fönster stängts) — inte vår
    Registret& r  = registret();
    const auto it = r.running.find(e.timerId);
    if (it == r.running.end())
        return;   // stoppad, omstartad eller riven — en gammal tick i kön
    Timer& t = *it->second;
    if (t.dueStamp_ != 0)
        return;   // redan förfallen och inte levererad — EN tick
    t.dueStamp_ = ++r.nextStamp;
    r.due.push_back(Förfallen{e.timerId, t.dueStamp_});
}

// Posterna tas ur den DELADE listan en i taget, så att en nästlad pump i en
// handler (en modal dialog) ser — och levererar — resten av samma varv i
// stället för att de väntar tills dialogen stängts. `gräns` håller anropet
// till det som var förfallet när det började: det som antecknas under en
// handler levereras av nästa svans.
void TimerState::drain()
{
    Registret& r = registret();
    if (r.due.empty())
        return;
    const std::uint64_t gräns = r.nextStamp;
    while (!r.due.empty() && r.due.front().stamp <= gräns) {
        const Förfallen f = r.due.front();
        r.due.pop_front();
        const auto it = r.running.find(f.run);
        if (it == r.running.end())
            continue;   // stoppad, omstartad eller riven sedan anteckningen
        Timer& t = *it->second;
        if (t.dueStamp_ != f.stamp)
            continue;   // redan levererad — av en nästlad pump
        t.dueStamp_ = 0;   // en ny tick får antecknas från och med nu
        if (t.singleShot_)
            stopRun(t);    // FÖRE handlern: isRunning() är false i den
        if (t.onTick) {
            TimerArgs a;
            t.onTick(&t, a);   // `Handler` håller lambdan vid liv — och `t`
                               // får vara riven efteråt: rör den inte
        }
    }
}

void timerNoteDue(const gfx::Event& e)
{
    TimerState::noteDue(e);
}

void timerDrain()
{
    TimerState::drain();
}

bool timerHasDue()
{
    return !registret().due.empty();
}

std::uint64_t timerRunIdOf(const Timer& t)
{
    return TimerState::runIdOf(t);
}

} // namespace detail

// ===================================================================== Timer

Timer::Timer()
{
    // REGISTRET FÖRST: skapat här överlever det varje timer — också en
    // global, som rivs efter registret annars hade rivits.
    (void)detail::registret();
}

Timer::~Timer()
{
    stop();
}

void Timer::setInterval(int ms)
{
    if (ms < kMinIntervalMs) {
        gfx::log("Timer(%s): setInterval(%d) under %d ms — klampas",
                 name.c_str(), ms, kMinIntervalMs);
        ms = kMinIntervalMs;
    }
    if (ms == intervalMs_)
        return;
    intervalMs_ = ms;
    if (isRunning())
        start();   // byte medan timern går = omstart med den nya perioden
}

void Timer::start()
{
    glaze::app();   // App lat, som ett fönsters show(): timern kräver gfx::init
    detail::TimerState::stopRun(*this);   // omstart = nytt id och ny period
    detail::Registret& r   = detail::registret();
    const std::uint64_t run = ++r.nextRun;
    if (!gfx::startAppTimer(run, static_cast<std::uint32_t>(intervalMs_))) {
        // Fasaden svarar false före init (plattformen kunde inte startas)
        // och när OS:et vägrar — backenden har loggat det senare.
        gfx::log("Timer(%s): start — plattformen startade ingen timer; "
                 "isRunning() förblir false",
                 name.c_str());
        return;
    }
    runId_ = run;
    r.running.emplace(run, this);
}

void Timer::stop()
{
    detail::TimerState::stopRun(*this);
}

// Previewns väg in. Ett ogiltigt värde är en KÄND nyckel — true, loggat och
// ignorerat (kontraktet vid `Widget::setProp`).
bool Timer::setProp(const std::string& key, const std::string& value)
{
    if (key == "interval") {
        int v = 0;
        if (!propparse::parseInt(value, v)) {
            gfx::log("Timer(%s): '%s' är inget heltal för interval "
                     "(ignoreras)",
                     name.c_str(), value.c_str());
            return true;
        }
        setInterval(v);
        return true;
    }
    if (key == "singleShot") {
        if (value == "true")
            setSingleShot(true);
        else if (value == "false")
            setSingleShot(false);
        else
            gfx::log("Timer(%s): '%s' är inte true/false för singleShot "
                     "(ignoreras)",
                     name.c_str(), value.c_str());
        return true;
    }
    return false;
}

} // namespace glaze
