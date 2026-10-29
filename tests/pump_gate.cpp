// pump_gate — D3D11-PUMPEN OCH SKICKADE MEDDELANDEN.
//
// Kör:  ./build/tests/pump_gate
// Exit: 0 = grönt · 1 = fallet föll
//
// Win32 levererar ett SKICKAT meddelande (SendMessage från en annan tråd,
// OS:ets WM_SETTINGCHANGE-utsändning vid ljus/mörkt-byte, aktiverings- och
// fokusbyten) INUTI `GetMessageW`: anropet kör fönstrets WndProc och
// fortsätter därefter VÄNTA — det återvänder bara för ett POSTAT meddelande.
// Köade WndProc:en ett gfx-event under det anropet låg eventet kvar tills
// nästa postade meddelande kom, alltså tills användaren rörde musen. Samma
// symtom som en glömd `invalidate()`, men utan någon sådan.
//
// Fallet: ett DOLT fönster körs med `run()` (pumpen sover i `waitEvent`),
// och en BAKGRUNDSTRÅD gör `SendMessageW(hwnd, WM_CLOSE)`. WndProc:en köar
// `WindowCloseRequested` inuti GetMessageW; fungerar pumpen returnerar
// `run()`. Fungerar den inte väcker grinden själv pumpen efter en frist —
// med en postad stängning — så att den FALLER med en skriven rad i stället
// för att hänga till ctest-timeouten (den finns ändå, som fallskärm).
//
// Grinden mäter INTE: att OS:ets egna utsändningar (WM_SETTINGCHANGE,
// WM_ACTIVATEAPP) faktiskt kommer fram — de är samma mekanism som fallet
// här men går inte att framkalla deterministiskt — och inte PeekMessage-
// vägen (`pollEvent`), som saknar ett observerbart häng att mäta. Bara
// Windows: Wayland/X11 läser sina event ur en socket och macOS-backendens
// `pushEvent` postar redan ett väckningsevent.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include <glaze/glaze.h>

// Samma vakter som backendens d3d11_backend.h: MinGW:s libstdc++ definierar
// redan NOMINMAX, och en ovillkorlig omdefinition är en varning.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

int g_fails = 0;

void check(bool ok, const char* vad)
{
    std::printf("%-6s %s\n", ok ? "OK" : "FEL", vad);
    if (!ok)
        ++g_fails;
}

} // namespace

int main()
{
    std::printf("=== pump_gate — skickat meddelande medan pumpen sover ===\n");

    // Titeln är FindWindowW-nyckeln, så den görs unik per process — två
    // samtidiga körningar får inte hitta varandras fönster.
    const std::string titel =
        "pump_gate-" + std::to_string(GetCurrentProcessId());

    glaze::AppWindow w(titel, 200, 120);
    w.setOffscreen(true);

    std::atomic<bool> klar{false};      // run() har returnerat
    std::atomic<bool> skickat{false};   // SendMessageW har returnerat
    std::atomic<bool> hängde{false};    // fristen gick ut — grinden väckte
    std::thread       arbetare([&] {
        const std::wstring wtitel(titel.begin(), titel.end());
        HWND               hwnd = nullptr;
        for (int i = 0; i < 200 && !hwnd; ++i) {
            hwnd = FindWindowW(nullptr, wtitel.c_str());
            if (!hwnd)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (!hwnd) {
            glaze::app().post([&w] { w.close(); });
            return;
        }
        // Sömnen gör fallet skarpt: pumpen ska ha ritat sin första frame
        // och SOVA i GetMessageW när meddelandet kommer, annars levereras
        // det mitt i ett varv och fallet mäter ingenting.
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        SendMessageW(hwnd, WM_CLOSE, 0, 0);   // levereras INUTI GetMessageW
        skickat = true;
        for (int i = 0; i < 300 && !klar; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (!klar) {
            // FELET: eventet ligger i kön och pumpen sover vidare. Väck den
            // med en postad stängning, så att grinden faller i stället för
            // att hänga.
            hängde = true;
            glaze::app().post([&w] { w.close(); });
        }
    });

    const int ut = w.run();
    klar = true;
    arbetare.join();

    check(skickat, "bakgrundstråden hittade fönstret och skickade WM_CLOSE");
    check(!hängde,
          "WM_CLOSE skickat INUTI GetMessageW väckte pumpen — utan grindens "
          "egen väckning");
    check(!w.isOpen(), "fönstret stängdes");
    check(ut == 0, "run() returnerade normalt");

    std::printf("\npump_gate: %s (%d fel)\n", g_fails ? "FÄLLD" : "GRÖN",
                g_fails);
    return g_fails ? 1 : 0;
}
