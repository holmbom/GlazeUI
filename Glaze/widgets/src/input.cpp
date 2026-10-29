// Inmatningens vokabulär — se glaze/input.h.

#include "glaze/input.h"

namespace glaze {

InputDevice deviceOf(InputSource s)
{
    switch (s) {
    case InputSource::ScrollX:
    case InputSource::ScrollY:
    case InputSource::Pinch:
    case InputSource::Twist:
        return InputDevice::Touchpad;
    case InputSource::ArrowsX:
    case InputSource::ArrowsY:
    case InputSource::PlusMinus:
        return InputDevice::Keyboard;
    default:
        return InputDevice::Mouse;
    }
}

} // namespace glaze
