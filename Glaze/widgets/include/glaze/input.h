#pragma once
#include <cstdint>
#include <string>

// INMATNINGENS VOKABULÄR — ren std, inget
// tillstånd. Två saker använder den:
//
//   * RULLNINGENS RIKTNING i resten av appen (`Widget::setWheelInvert`,
//     `AppWindow::setWheelInvert`, `App::setWheelInvert`): `InputDevice`
//     skiljer musens hjul från styrplattans tvåfingersdrag.
//   * RITYTANS BINDNINGSTABELL (`Canvas::setBinding`, `App::setCanvasBinding`):
//     (läge, källa, modifierare) → (handling, riktning, fart).
//
// VARJE AXEL ÄR EN EGEN KÄLLA, så "vänd en axel" och "flytta en handling till
// en annan rörelse" är samma sak — en rad. Tecknen (källans + och
// handlingens +) står vid uppräkningarna, och MANUAL `input` har tabellen över
// vad varje handling gör i varje läge.
//
// RIKTNINGEN ÄR RELATIV MOT OS:ET: det OS:et redan
// vänt — naturlig rullning — är redan tillämpat i det glaze får, och en
// vändning här vänder därifrån. Samma regel på alla tre OS.

namespace glaze {

// Varifrån en händelse kommer. Musens hjul är ett hjul som inte är precist
// (`AppWindow::isPreciseWheel` false); STYRPLATTAN är det precisa hjulet —
// tvåfingersdraget, och Magic Mouse som macOS märker likadant — och
// nypningen och vridningen. Tangentbordet bär pilarna och + och −.
enum class InputDevice : std::uint8_t { Mouse, Touchpad, Keyboard };

// En AXEL som kan bindas. Tecknet är källans +:
enum class InputSource : std::uint8_t {
    // Pekarens förflyttning medan knappen hålls, LOGISKA px (höger +, ned +).
    LeftDragX, LeftDragY, MiddleDragX, MiddleDragY, RightDragX, RightDragY,
    // Musens hjul, rader (höger +, upp +).
    WheelX, WheelY,
    // Styrplattans tvåfingersdrag: innehållets rörelse på skärmen, LOGISKA px
    // (höger +, ned +).
    ScrollX, ScrollY,
    // Nypningen: ln(skalan), isär +. Vridningen: grader, medurs +.
    Pinch, Twist,
    // Piltangenterna: ett steg (höger +, ned +).
    ArrowsX, ArrowsY,
    // + och −: ett steg (plus +).
    PlusMinus,
};

// Vad en bunden axel gör med ritytans kamera. Tecknet är handlingens +:
//   PanX / PanY  innehållet flyttar åt höger / nedåt
//   Zoom         in (kartans zoom, orbitens dolly mot målet)
//   Rotate       bäringen / azimuten ökar
//   Tilt         uppåt i elevationen: kartans tilt minskar, orbitens
//                elevation ökar, förstapersonens blick sänks
//   Fly          framåt längs blicken (kartan: ingenting)
//   Lift         uppåt längs världens upp-axel (kartan: ingenting)
// `None` tar bort gesten ur tabellen — den beter sig som obunden.
enum class InputAction : std::uint8_t {
    None, PanX, PanY, Zoom, Rotate, Tilt, Fly, Lift
};

enum class InputDirection : std::uint8_t { Normal, Inverted };

// EN RAD i bindningstabellen. Nyckeln är (läge, källa, modifierare); en ny
// rad med samma nyckel ERSÄTTER den gamla helt. `mods` är `gfx::kMod*`, och
// Cmd räknas som Ctrl (ritytans regel). En rad MATCHAR när dess
// modifierare är en delmängd av de tryckta, och den mest specifika vinner —
// flest bitar, vid lika Alt före Shift före Ctrl. `speed` multiplicerar
// mängden (> 0 och ändlig).
struct InputBinding {
    std::string    mode;
    InputSource    source{};
    std::uint32_t  mods      = 0;
    InputAction    action    = InputAction::None;
    InputDirection direction = InputDirection::Normal;
    float          speed     = 1.0f;
};

// Enheten källan hör till: dragen och hjulet musens, tvåfingersdraget,
// nypningen och vridningen styrplattans, pilarna och + och − tangentbordets.
InputDevice deviceOf(InputSource s);

} // namespace glaze
