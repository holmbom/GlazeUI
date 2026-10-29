#pragma once
// Backend-neutralt och internt: pekarbilder
// som OS:et SAKNAR. Den stängda handen finns varken bland Windows
// systempekare eller i X-kärnans pekarfont, så de två backendarna ritar den ur
// samma bild här — en sanning och inte två kopior som kan glida isär. macOS
// (`closedHandCursor`) och Wayland-temana (`grabbing`) har den som
// systempekare och läser aldrig filen.
//
// FORMEN är egen pixelritning efter den form Qt och macOS använder för att
// dra innehåll: fyra knogar överst, tummen inböjd till vänster, handflatan
// under. Svart kontur och vit fyllning, så den syns mot både ljust och mörkt.
// Header-only och ren std; bara `expandCursor` gör något, och den skalar med
// HELTAL (en pekare ska vara skarp, aldrig utsmetad).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gfx {

// EN pekarbild: `size` × `size` pixlar i `rows` (rad 0 överst), och hotspoten
// i bildens egna pixlar. Tecknen: '.' genomskinlig, 'X' svart, 'o' vit.
struct CursorBitmap {
    int         size;
    int         hotX;
    int         hotY;
    const char* rows[16];
};

// DEN STÄNGDA HANDEN (`PointerShape::ClosedHand`) — 16 × 16, hotspot i
// handflatans mitt, där man "håller i" innehållet.
inline constexpr CursorBitmap kClosedHand = {
    16, 8, 8,
    {
        "................",
        "................",
        "....XX.XX.XX....",
        "...XooXooXooXX..",
        "...XooooooooXoX.",
        "....XoooooooooX.",
        "..XXXoooooooooX.",
        ".XooXoooooooooX.",
        ".XoooooooooooX..",
        "..XooooooooooX..",
        "...XoooooooooX..",
        "....XoooooooX...",
        ".....XooooooX...",
        ".....XooooooX...",
        ".....XXXXXXXX...",
        "................",
    },
};

// Bilden som RGBA8 med RAK alfa (rad 0 överst, täta rader), varje bildpixel
// ett `scale` × `scale`-block — alltså en kvadrat med sidan `size × scale`.
// Hotspoten skalas med samma faktor och skrivs till `hotX`/`hotY`. `scale`
// under 1 tolkas som 1.
inline std::vector<std::uint8_t> expandCursor(const CursorBitmap& b, int scale,
                                              int& hotX, int& hotY)
{
    if (scale < 1)
        scale = 1;
    const int                 sida = b.size * scale;
    std::vector<std::uint8_t> ut(static_cast<std::size_t>(sida) *
                                     static_cast<std::size_t>(sida) * 4u,
                                 0);
    for (int y = 0; y < b.size; ++y)
        for (int x = 0; x < b.size; ++x) {
            const char c = b.rows[y][x];
            if (c != 'X' && c != 'o')
                continue;   // genomskinlig — nollorna står kvar
            const std::uint8_t v = c == 'X' ? 0 : 255;
            for (int dy = 0; dy < scale; ++dy)
                for (int dx = 0; dx < scale; ++dx) {
                    const std::size_t p =
                        (static_cast<std::size_t>(y * scale + dy) *
                             static_cast<std::size_t>(sida) +
                         static_cast<std::size_t>(x * scale + dx)) *
                        4u;
                    ut[p]     = v;
                    ut[p + 1] = v;
                    ut[p + 2] = v;
                    ut[p + 3] = 255;
                }
        }
    hotX = b.hotX * scale;
    hotY = b.hotY * scale;
    return ut;
}

} // namespace gfx
