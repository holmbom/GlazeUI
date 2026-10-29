// APPENS TIMER PÅ WINDOWS — fasaden
// gfx/timer.h ovanpå `SetTimer`.
//
// MOTTAGAREN är ett lat skapat MEDDELANDEFÖNSTER (`HWND_MESSAGE`, klass
// `GlazeAppTimer`) med en egen WndProc. Inte en TRÅDtimer (`SetTimer(NULL,
// …)`): dess `WM_TIMER` har inget fönster och försvinner i en modal loop —
// fönsterdraget kör sin egen GetMessage/DispatchMessage, och bara ett
// fönsterbundet meddelande når fram (d3d11_platform.cpp, PUFFEN). Toppnivå
// behövs inte, för timern behöver inga broadcasts (till skillnad från
// systemfältets mottagare, som måste höra `TaskbarCreated`).
//
// HÄNDELSERNA: WndProc:ens `WM_TIMER` köar `EventType::Timer` med windowId 0
// och `timerId` = wParam genom `pushEvent`, som fönstrens. Under en modal
// flytt-loop köas en tick per period; glaze slår ihop dem till EN leverans
// efter släppet (den förfallna mängden) — backenden koalescerar inte.
//
// ID:T är 64 bitar (gfx/timer.h) och `SetTimer`s `UINT_PTR` likaså på x64,
// så körnings-id:t går orört genom `WM_TIMER`s wParam. Samma id ersätter
// timern och nollställer perioden — dokumenterat Win32-beteende, och exakt
// fasadens "samma id startar om perioden".
//
// UI-TRÅDENS, som resten av fasaden. Utanför init–shutdown gör ingen
// funktion något (`platformInitialized`): en `glaze::Timer` som rivs efter
// appen får inte skapa ett nytt mottagarfönster.

#include <cstdint>

#include "d3d11_backend.h"
#include "gfx/platform.h"
#include "gfx/timer.h"

namespace gfx {

namespace {

constexpr wchar_t kTimerClass[] = L"GlazeAppTimer";

// Mottagaren — lat, och riven i `appTimerShutdown`. Timrarna dör med den.
HWND g_hwnd = nullptr;

LRESULT CALLBACK timerWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_TIMER) {
        Event e;
        e.type     = EventType::Timer;
        e.windowId = 0;   // appens, inte ett fönsters (gfx/timer.h)
        e.timerId  = static_cast<AppTimerId>(wp);
        pushEvent(e);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// MOTTAGAREN, skapad vid första starten. false + logg när det inte går — då
// startar ingen timer.
bool ensureWindow()
{
    if (g_hwnd)
        return true;
    const HINSTANCE hinst = GetModuleHandleW(nullptr);
    WNDCLASSEXW     wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &timerWndProc;
    wc.hInstance     = hinst;
    wc.lpszClassName = kTimerClass;
    if (!RegisterClassExW(&wc) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        log("Appens timer: fönsterklassen kunde inte registreras (fel %lu)",
            GetLastError());
        return false;
    }
    g_hwnd = CreateWindowExW(0, kTimerClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                             nullptr, hinst, nullptr);
    if (!g_hwnd) {
        log("Appens timer: meddelandefönstret kunde inte skapas (fel %lu)",
            GetLastError());
        return false;
    }
    return true;
}

} // namespace

// Körnings-id:t går som `UINT_PTR` genom SetTimer och tillbaka i `wParam` —
// helt bara på 64 bitar. MSYS2 UCRT64 är 64 bitar; ett
// 32-bitarsbygge hade trunkerat id:t och fått fel körning, så det stoppas här.
static_assert(sizeof(UINT_PTR) >= sizeof(AppTimerId),
              "appens timer kräver ett 64-bitars Windows-bygge");

bool startAppTimer(AppTimerId id, std::uint32_t ms)
{
    if (!platformInitialized() || id == 0)
        return false;   // före init, efter shutdown eller id 0 (gfx/timer.h)
    if (!ensureWindow())
        return false;
    if (ms < 1)
        ms = 1;   // Windows klampar ändå till USER_TIMER_MINIMUM (10 ms)
    if (!SetTimer(g_hwnd, static_cast<UINT_PTR>(id), ms, nullptr)) {
        log("Appens timer: SetTimer vägrade (fel %lu)", GetLastError());
        return false;
    }
    return true;
}

void stopAppTimer(AppTimerId id)
{
    if (!platformInitialized() || !g_hwnd || id == 0)
        return;   // okänt eller inget fönster — no-op (gfx/timer.h)
    // Ett WM_TIMER som redan postats ligger kvar (KillTimer tar inte bort
    // det) — glaze slår upp körnings-id:t och släpper det.
    KillTimer(g_hwnd, static_cast<UINT_PTR>(id));
}

void appTimerShutdown()
{
    // Timrarna dör med fönstret.
    if (g_hwnd)
        DestroyWindow(g_hwnd);
    g_hwnd = nullptr;
}

} // namespace gfx
