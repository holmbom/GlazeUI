// APPENS TIMER PÅ macOS — fasaden
// gfx/timer.h ovanpå `NSTimer`.
//
// EN `NSTimer` PER ID på main-runloopen i `NSRunLoopCommonModes`, samma form
// som fönstrens timers (`MetalWindow::startTimer`) och systemfältets
// animation (metal_tray.mm): den tickar också medan en meny eller ett
// fönsterdrag kör sin egen runloop-fas. Blocket köar `EventType::Timer` med
// windowId 0 och `timerId` = id genom `pushEvent`, som också postar
// väcknings-eventet — annars hade `waitEvent` sovit genom ticken.
//
// Samma id startar om perioden: den gamla timern invalideras och en ny
// läggs in (SetTimer-semantiken, fasadens kontrakt).
//
// LIVSTIDEN följer plattformen, som systemfältets: `appTimerInit()` (ur
// `gfx::init`) öppnar fasaden och `appTimerShutdown()` (ur `gfx::shutdown`)
// invaliderar varje timer och stänger den igen. Före och efter svarar
// `startAppTimer` false och `stopAppTimer` gör ingenting — en
// `glaze::Timer` som lever längre än appen kan inte lägga en timer på en
// runloop som inte pumpas.

#include <unordered_map>

#include "metal_backend.h"
#include "gfx/platform.h"
#include "gfx/timer.h"

namespace gfx {

namespace {

// Timrarna per id. ObjC-pekarna är __strong (ARC i C++-containern).
//
// I NAMNRYMDEN och inte som en lokal static med flit, av systemfältets skäl
// (metal_tray.mm): den konstrueras före main och rivs därför EFTER
// glaze::App, så att `appTimerShutdown` — från ~App via gfx::shutdown —
// aldrig rör en riven karta.
std::unordered_map<AppTimerId, NSTimer*> g_timers;

// Öppen mellan `appTimerInit` och `appTimerShutdown`.
bool g_live = false;

} // namespace

void appTimerInit()
{
    g_live = true;
}

void appTimerShutdown()
{
    @autoreleasepool {
        for (auto& kv : g_timers)
            [kv.second invalidate];   // inga ticks efter plattformens död
    }
    g_timers.clear();
    g_live = false;
}

bool startAppTimer(AppTimerId id, std::uint32_t ms)
{
    if (!g_live || id == 0)
        return false;   // före init, efter shutdown eller id 0 (gfx/timer.h)
    if (ms < 1)
        ms = 1;
    stopAppTimer(id);   // samma id = omstartad period
    @autoreleasepool {
        const AppTimerId tid = id;
        NSTimer* t = [NSTimer timerWithTimeInterval:ms / 1000.0
                                            repeats:YES
                                              block:^(NSTimer*) {
                                                  Event e;
                                                  e.type     = EventType::Timer;
                                                  e.windowId = 0;   // appens
                                                  e.timerId  = tid;
                                                  pushEvent(e);
                                              }];
        if (!t) {
            log("Appens timer: NSTimer kunde inte skapas");
            return false;
        }
        [[NSRunLoop mainRunLoop] addTimer:t forMode:NSRunLoopCommonModes];
        g_timers[id] = t;
    }
    return true;
}

void stopAppTimer(AppTimerId id)
{
    const auto it = g_timers.find(id);
    if (it == g_timers.end())
        return;   // okänt id — och efter shutdown är kartan tom
    @autoreleasepool {
        [it->second invalidate];
    }
    g_timers.erase(it);
}

} // namespace gfx
