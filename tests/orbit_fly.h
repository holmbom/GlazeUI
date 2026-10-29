#pragma once
// FLYGLÄGET SOM BINDNINGAR: orbitens panorering FLYGER fram/bak
// längs blicken, och med Shift HÖJER och sänker den längs upp-axeln.
// Nycklarna är mittendraget (också Ctrl+Shift, som
// annars är Shifts rad i orbiten), Shift+vänsterdraget och tvåfingersdraget.
// Delas av canvas3d_gate, trackpad_gate och binding_gate (fall O).

#include <glaze/glaze.h>

namespace gate {

inline void orbitFly(glaze::Canvas& c)
{
    using S = glaze::InputSource;
    using A = glaze::InputAction;
    c.setBinding("orbit", S::MiddleDragY, 0, A::Fly);
    c.setBinding("orbit", S::MiddleDragY, gfx::kModShift, A::Lift);
    c.setBinding("orbit", S::MiddleDragY, gfx::kModCtrl | gfx::kModShift, A::Lift);
    c.setBinding("orbit", S::LeftDragY, gfx::kModShift, A::Lift);
    c.setBinding("orbit", S::ScrollY, 0, A::Fly);
    c.setBinding("orbit", S::ScrollY, gfx::kModShift, A::Lift);
}

} // namespace gate
