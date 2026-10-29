// Färghjälparna — se glaze/color.h. Paletterna och kontrastgrinden mäter med samma tal.

#include "glaze/color.h"

#include <cmath>
#include <cstdint>

namespace glaze::color {

gfx::Color hsl(float h, float s, float l)
{
    const float c  = (1.0f - std::fabs(2.0f * l - 1.0f)) * s;
    const float hp = h / 60.0f;
    const float x  = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (hp < 1.0f)      { r = c; g = x; }
    else if (hp < 2.0f) { r = x; g = c; }
    else if (hp < 3.0f) { g = c; b = x; }
    else if (hp < 4.0f) { g = x; b = c; }
    else if (hp < 5.0f) { r = x; b = c; }
    else                { r = c; b = x; }
    const float m = l - c / 2.0f;
    auto        B = [&](float v) {
        const long q = std::lround((v + m) * 255.0f);
        return static_cast<std::uint8_t>(q < 0 ? 0 : q > 255 ? 255 : q);
    };
    return gfx::Color{B(r), B(g), B(b), 255};
}

// KLAMPAD, för `t` får vara > 1: en andel över 1 EXTRAPOLERAR bortom `b`
// (temats kapselhover). Utan klampningen wrappar en uint8 tyst och en dämpad
// ton blir gäll.
gfx::Color mix(gfx::Color a, gfx::Color b, float t)
{
    auto m = [&](std::uint8_t x, std::uint8_t y) {
        const float v = x + (y - x) * t + 0.5f;
        return static_cast<std::uint8_t>(v < 0.0f ? 0.0f : v > 255.0f ? 255.0f : v);
    };
    return gfx::Color{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), 255};
}

float luminance(gfx::Color c)
{
    auto f = [](std::uint8_t v) {
        const float s = v / 255.0f;
        return s <= 0.03928f ? s / 12.92f
                             : std::pow((s + 0.055f) / 1.055f, 2.4f);
    };
    return 0.2126f * f(c.r) + 0.7152f * f(c.g) + 0.0722f * f(c.b);
}

float contrast(gfx::Color a, gfx::Color b)
{
    const float la = luminance(a), lb = luminance(b);
    return ((la > lb ? la : lb) + 0.05f) / ((la < lb ? la : lb) + 0.05f);
}

// Kulörvinklarna — jämnt fördelade
// nog att gå att skilja åt, valda så att inget par läser som samma färg.
float hueDegrees(Hue h)
{
    switch (h) {
    case Hue::Red:    return 358.0f;
    case Hue::Orange: return 26.0f;
    case Hue::Amber:  return 44.0f;
    case Hue::Green:  return 138.0f;
    case Hue::Teal:   return 184.0f;
    case Hue::Blue:   return 214.0f;
    case Hue::Indigo: return 248.0f;
    case Hue::Violet: return 282.0f;
    case Hue::Neutral:
    default:          return 0.0f;   // mättnaden nollas — vinkeln spelar ingen roll
    }
}

} // namespace glaze::color
