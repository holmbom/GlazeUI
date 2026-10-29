#pragma once

namespace gfx {

// Backend-neutrala geometri-typer — de enda punkt-/storleks-/rektangel-
// typerna i publika API:er. Flyttal i fysiska px — logiska mått
// multipliceras med uiScale före de hamnar här.

struct Pointf {
    float x = 0.0f;
    float y = 0.0f;
};

struct Sizef {
    float w = 0.0f;
    float h = 0.0f;
};

struct Rectf {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    // Punkt-i-rektangel — ersätter de lokala inRect-hjälparna.
    constexpr bool contains(float px, float py) const
    {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

} // namespace gfx
