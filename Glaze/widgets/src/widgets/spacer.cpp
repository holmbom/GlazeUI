// Spacer — MÖNSTER: inbyggd widget UTAN utseende: draw() är tom (luckan
// är barens bakgrund), så ingen ritkod behövs. Beteendet är bara
// grow-vikten som Bar-flexen läser via Widget::flexGrow().
//
// AXELREGELN: Spacer får INGEN contentSize-
// override — den behåller basens råa width × height. `width = 0` betyder
// här redan något: "ren grow-vikt, ingen fast marginal" (Bar-flexen läser
// en SATT width som fast bredd, se buildTopBar i ../app_window.cpp). Noll
// är alltså meningsbärande och inte "ingen uppgift" — axelregelns AUTO
// skulle ha skrivit över just den betydelsen med ett stilmått.

#include "glaze/widgets/spacer.h"

#include <cstdlib>

namespace glaze {

// Registrerat typnamn — paletten/preview och headless-tester.
GLAZE_REGISTER_WIDGET("spacer", Spacer)

bool Spacer::setProp(const std::string& key, const std::string& value)
{
    if (key == "grow") {
        setGrow(std::strtof(value.c_str(), nullptr));
        return true;
    }
    return Widget::setProp(key, value);
}

} // namespace glaze
