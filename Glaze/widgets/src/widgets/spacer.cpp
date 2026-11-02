// Spacer — MÖNSTER: inbyggd widget UTAN utseende: draw() är tom (luckan
// är barens bakgrund), så ingen ritkod behövs. Beteendet är bara
// grow-vikten som Bar-flexen läser via Widget::flexGrow().
//
// AXELREGELN: med en vikt betyder `width = 0` "ren grow-vikt, ingen fast
// marginal", och noll är meningsbärande. UTAN vikt är Spacern ett fast
// mellanrum, och där är noll AUTO: stilens `spacerW`.

#include "glaze/widgets/spacer.h"

#include <cstdlib>

#include "glaze/style.h"

namespace glaze {

// Registrerat typnamn — paletten/preview och headless-tester.
GLAZE_REGISTER_WIDGET("spacer", Spacer)

gfx::Sizef Spacer::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default:
        if (grow_ > 0.0f)
            return gfx::Sizef{width, height};
        return gfx::Sizef{width > 0.0f ? width : c.style->spacerW,
                          height > 0.0f ? height : c.style->spacerW};
    }
}

bool Spacer::setProp(const std::string& key, const std::string& value)
{
    if (key == "grow") {
        setGrow(std::strtof(value.c_str(), nullptr));
        return true;
    }
    return Widget::setProp(key, value);
}

} // namespace glaze
