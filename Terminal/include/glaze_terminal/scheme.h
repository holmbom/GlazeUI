#pragma once
#include <array>
#include <cstdint>

#include <gfx/color.h>
#include <glaze/theme.h>

// TERMINALENS FÄRGER — de 16 grundfärgerna, förgrunden,
// bakgrunden, markören och markeringen. De är INTE `Theme::Colors`-roller:
// rollerna är positionella och ritas av varje app i varje palett, och 22
// roller för en komponent de flesta inte länkar hade belastat alla. Färgerna
// HÄRLEDS i stället här ur temat vid varje ritning (som `canvasGrid` och
// `stock*` härleds i ramverket), så de följer temat och läget ändå.
//
// SCHEMANA är DATA (färgväljarens `ColorPalette`-skäl): `Theme` (förval —
// härlett ur appens tema), `Solarized` (Ethan Schoonovers), `One` (One
// Light/One Dark) och `Tango` (GNOME:s). Alla har en ljus och en mörk
// variant; `Terminal::setColorMode` väljer vilken (Auto följer appens läge).
// Egna färger per roll och variant läggs OVANPÅ (`Terminal::setSchemeColor`).

namespace glaze {

enum class TerminalScheme : std::uint8_t { Theme, Solarized, One, Tango };

// Rollerna — de 16 i ANSI-ordning, sedan de sex ytorna.
enum class TerminalColor : std::uint8_t {
    Black, Red, Green, Yellow, Blue, Magenta, Cyan, White,
    BrightBlack, BrightRed, BrightGreen, BrightYellow,
    BrightBlue, BrightMagenta, BrightCyan, BrightWhite,
    Foreground, Background, Cursor, CursorText, Selection, SelectionText,
};
inline constexpr int kTerminalColorCount = 22;

struct TerminalPalette {
    std::array<gfx::Color, kTerminalColorCount> colors{};

    gfx::Color& operator[](TerminalColor c) { return colors[static_cast<std::size_t>(c)]; }
    gfx::Color operator[](TerminalColor c) const
    {
        return colors[static_cast<std::size_t>(c)];
    }
    // 256-paletten (SGR 38;5;n): 0–15 rollerna, 16–231 xterms kub och
    // 232–255 gråskalan. Utanför 0–255 klämms indexet.
    gfx::Color indexed(int i) const;
};

// Är varianten MÖRK? Texten ljusare än ytan (`panel`) — samma fråga som
// temats egen härledning ställer.
bool terminalIsDark(const Theme::Variant& v);

// SVAG text (SGR 2): förgrunden blandad mot bakgrunden.
gfx::Color terminalDimmed(gfx::Color fg, gfx::Color bg);

// Paletten för ett schema i en variant. `theme` läses bara av `Theme`-schemat.
// Den ENDA härledningen — ritningen, OSC 10/11 och grinden läser den.
TerminalPalette terminalPalette(TerminalScheme scheme, bool dark,
                                const Theme::Variant& theme);

} // namespace glaze
