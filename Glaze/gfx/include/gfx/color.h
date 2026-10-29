#pragma once
#include <cstdint>

namespace gfx {

// Backend-neutral RGBA-färg (8 bitar per kanal). constexpr-vänlig POD —
// glaze::theme:: definierar sina konstanter som inline constexpr av denna.
struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    // Kanal för kanal — likhetsvakterna i settrar
    // som tar en `std::optional<gfx::Color>` (en rads eller en nods
    // textfärg) kräver den. Structen förblir ett aggregat.
    friend constexpr bool operator==(Color, Color) = default;
};

} // namespace gfx
