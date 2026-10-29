#pragma once
// STYRPLATTAN i D3D11-backenden — intern header, får
// aldrig inkluderas utanför backend_d3d11/.
//
// Windows ger inte en app styrplattans gester av sig själv: utan en
// "upplyst" teknik blir tvåfingersdraget WM_MOUSEWHEEL (som inte går att
// skilja från ett mushjul) och nypningen WM_MOUSEWHEEL med MK_CONTROL utan
// att Ctrl är nedtryckt. TVÅ vägar in, per fönster:
//
//   DIRECT MANIPULATION — panoreringen och nypningen, Chromiums och Firefox
//     väg: en FALSK viewport (1000 × 1000, manuell uppdatering) vars
//     innehållstransform vi läser och nollställer. Skala ≈ 1 är ett
//     tvåfingersdrag ⇒ `MouseWheel` med `precise` (OS:ets tröghet ⇒
//     `Momentum`), annars en nypning ⇒ `Gesture`.
//   RÅA KONTAKTER — vridningen, som Direct Manipulation saknar:
//     precisionsplattan är en HID-digitizer, och dess rapporter (WM_INPUT)
//     tolkas med HidP_* till kontakter som den backend-neutrala spåraren
//     (../touchpad_rotation.h) gör till grader.
//
// De två slås ihop till EN `Gesture`-sekvens: Begin när nypningen startar
// ELLER vridningen griper, End när båda slutat (fasadens kontrakt vid
// `kPrecisePxPerRow` i gfx/events.h). Misslyckas Direct Manipulation (en
// tråd utan STA, ett gammalt Windows) loggas det EN gång och hjulvägen
// gäller — tvåfingersdraget blir hjul och Ctrl+hjul zoomar.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <memory>

namespace gfx {

class Window;

class D3dTouchpad {
public:
    D3dTouchpad();
    ~D3dTouchpad();
    D3dTouchpad(const D3dTouchpad&)            = delete;
    D3dTouchpad& operator=(const D3dTouchpad&) = delete;

    // När OS-fönstret finns (D3dWindow::init, inte för dolda fönster — de får
    // aldrig input). `win` lämnar fönstrets id och skala till eventen och
    // måste överleva `detach`.
    void attach(HWND hwnd, const Window& win);
    // Före DestroyWindow. Idempotent; ingen händelse köas härifrån.
    void detach();
    // Mellan `attach` och `detach` — `attach` river och bygger om, så den
    // som vill koppla EN gång frågar först.
    bool attached() const { return impl_ != nullptr; }

    // WndProc:ens meddelanden. Alla är no-op utan `attach`.
    void onPointerHitTest(WPARAM wp);   // DM_POINTERHITTEST — efter DefWindowProc
    bool onTimer(WPARAM id);            // true = vår privata pumptimer
    void onRawInput(LPARAM lp);         // WM_INPUT — alltid efter DefWindowProc
    void onFocusLost();                 // WM_KILLFOCUS — WM_INPUT tystnar

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gfx
