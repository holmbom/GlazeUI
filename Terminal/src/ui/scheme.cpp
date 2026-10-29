// Terminalens färgscheman — kontraktet i scheme.h. En SCHEMAFIL och inte
// widgetfilen (hård regel 3:s gräns): här står färgerna och talen, med skäl,
// och terminal.cpp läser bara paletten.

#include "glaze_terminal/scheme.h"

#include <algorithm>

#include <glaze/color.h>

namespace glaze {
namespace {

using C = TerminalColor;

constexpr gfx::Color rgb(std::uint32_t v)
{
    return gfx::Color{static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8),
                      static_cast<std::uint8_t>(v), 255};
}

// ------------------------------------------------- härledningen ur temat
//
// De SEX KULÖRERNA är familjegeneratorns vinklar (`color::hueDegrees`) — rött
// är samma rött som temats statusroller, oavsett appens hue. Mättnaden är EN,
// ljusheten följer läget: mörkare mot en ljus yta och ljusare mot en mörk, så
// att varje kulör håller 3:1 mot bakgrunden i alla tjugo paletter i båda
// lägena (terminal_ui_gate F mäter det). Ljust läge är det svåra: grönt och
// cyan är ljusast vid samma HSL-ljushet, och 0,34 är det som håller dem.
constexpr float kHueSat    = 0.70f;
constexpr float kHueLight  = 0.34f;   // ljust läge
constexpr float kHueDark   = 0.62f;   // mörkt läge
constexpr float kBrightUp  = 0.10f;   // de ljusa varianterna: ett steg ljusare
// Gult är amber-vinkeln, som temats varningsroll — rent gult (60°) blir
// oläsligt mot en ljus yta vid varje ljushet som behåller kulören.

// SVART OCH VITT är blandningar mellan bakgrunden och texten, som andel av
// vägen dit. I MÖRKT läge är svart en ton över ytan (ett program som ritar
// svart bakgrund ska synas som ett fält, inte som ett hål) och vitt en ton
// under texten. I LJUST läge är svart texten själv och vitt en ton under
// ytan — programmen antar en mörk bakgrund, och så gör alla ljusa scheman.
// BrightBlack (kommentarer, platshållare) håller 3:1 i båda.
struct Neutrals {
    float black, brightBlack, white, brightWhite;
};
constexpr Neutrals kNeutralDark{0.15f, 0.55f, 0.85f, 1.00f};
constexpr Neutrals kNeutralLight{1.00f, 0.60f, 0.15f, 0.05f};

// SVAG text (SGR 2) — halvvägs mot bakgrunden: tydligt nedtonad, och läsbar
// så länge förgrunden var det.
constexpr float kDimMix = 0.50f;

// Markeringen i de FASTA schemana: förgrunden blandad in i bakgrunden — en
// yta som syns och som texten fortfarande står på.
constexpr float kSelectionMix = 0.30f;

TerminalPalette themePalette(bool dark, const Theme::Variant& v)
{
    const Theme::Colors& t = v.colors;
    TerminalPalette      p;
    const gfx::Color     bg = t.panel;   // ritytans och fältens yta
    const gfx::Color     fg = t.text;
    const Neutrals&      n  = dark ? kNeutralDark : kNeutralLight;
    p[C::Black]       = color::mix(bg, fg, n.black);
    p[C::BrightBlack] = color::mix(bg, fg, n.brightBlack);
    p[C::White]       = color::mix(bg, fg, n.white);
    p[C::BrightWhite] = color::mix(bg, fg, n.brightWhite);

    const float l = dark ? kHueDark : kHueLight;
    const struct {
        C   bas, ljus;
        Hue hue;
    } kulörer[] = {
        {C::Red, C::BrightRed, Hue::Red},          {C::Green, C::BrightGreen, Hue::Green},
        {C::Yellow, C::BrightYellow, Hue::Amber},  {C::Blue, C::BrightBlue, Hue::Blue},
        {C::Magenta, C::BrightMagenta, Hue::Violet}, {C::Cyan, C::BrightCyan, Hue::Teal},
    };
    for (const auto& k : kulörer) {
        const float h = color::hueDegrees(k.hue);
        p[k.bas]      = color::hsl(h, kHueSat, l);
        p[k.ljus]     = color::hsl(h, kHueSat, std::min(1.0f, l + kBrightUp));
    }
    p[C::Foreground]    = fg;
    p[C::Background]    = bg;
    p[C::Cursor]        = t.accent;
    p[C::CursorText]    = t.onAccent;
    p[C::Selection]     = t.accentSurface;
    p[C::SelectionText] = t.onAccent;
    return p;
}

// ------------------------------------------------------- de fasta schemana
//
// Skrivna som sina upphovsmän publicerat dem — 16 färger och förgrund/
// bakgrund per variant. Markören är förgrunden och markeringen härledd
// (kSelectionMix); ingen av källorna definierar dem för alla terminaler.
struct Fixed {
    std::uint32_t ansi[16];
    std::uint32_t fg, bg;
};

// SOLARIZED (Ethan Schoonover, ethanschoonover.com/solarized) — samma 16 i
// båda varianterna; ljust och mörkt byter bara förgrund och bakgrund
// (base00/base3 respektive base0/base03).
constexpr std::uint32_t kSolarized16[16] = {
    0x073642, 0xdc322f, 0x859900, 0xb58900, 0x268bd2, 0xd33682, 0x2aa198, 0xeee8d5,
    0x002b36, 0xcb4b16, 0x586e75, 0x657b83, 0x839496, 0x6c71c4, 0x93a1a1, 0xfdf6e3,
};
// ONE (Atoms One Light och One Dark, terminalpaletten ur onedark.vim).
constexpr Fixed kOneLight{
    {0x383a42, 0xe45649, 0x50a14f, 0xc18401, 0x0184bc, 0xa626a4, 0x0997b3, 0xfafafa,
     0x4f525e, 0xe06c75, 0x98c379, 0xe5c07b, 0x61afef, 0xc678dd, 0x56b6c2, 0xffffff},
    0x383a42, 0xfafafa};
constexpr Fixed kOneDark{
    {0x282c34, 0xe06c75, 0x98c379, 0xe5c07b, 0x61afef, 0xc678dd, 0x56b6c2, 0xabb2bf,
     0x5c6370, 0xe06c75, 0x98c379, 0xd19a66, 0x61afef, 0xc678dd, 0x56b6c2, 0xffffff},
    0xabb2bf, 0x282c34};

// TANGO (GNOME Terminals "Tango"-palett) — ljus och mörk bakgrund.
constexpr std::uint32_t kTango16[16] = {
    0x2e3436, 0xcc0000, 0x4e9a06, 0xc4a000, 0x3465a4, 0x75507b, 0x06989a, 0xd3d7cf,
    0x555753, 0xef2929, 0x8ae234, 0xfce94f, 0x729fcf, 0xad7fa8, 0x34e2e2, 0xeeeeec,
};

TerminalPalette fixedPalette(const std::uint32_t (&ansi)[16], std::uint32_t fg,
                             std::uint32_t bg)
{
    TerminalPalette p;
    for (int i = 0; i < 16; ++i)
        p.colors[static_cast<std::size_t>(i)] = rgb(ansi[i]);
    p[C::Foreground]    = rgb(fg);
    p[C::Background]    = rgb(bg);
    p[C::Cursor]        = rgb(fg);
    p[C::CursorText]    = rgb(bg);
    p[C::Selection]     = color::mix(rgb(bg), rgb(fg), kSelectionMix);
    p[C::SelectionText] = rgb(fg);
    return p;
}

TerminalPalette fixedPalette(const Fixed& f) { return fixedPalette(f.ansi, f.fg, f.bg); }

} // namespace

// xterms 256-palett: kuben har stegen 0, 95, 135, 175, 215, 255 och
// gråskalan 8 + 10·n.
gfx::Color TerminalPalette::indexed(int i) const
{
    i = std::clamp(i, 0, 255);
    if (i < 16)
        return colors[static_cast<std::size_t>(i)];
    if (i < 232) {
        const int  k    = i - 16;
        const auto steg = [](int v) {
            return static_cast<std::uint8_t>(v == 0 ? 0 : 55 + 40 * v);
        };
        return gfx::Color{steg(k / 36), steg((k / 6) % 6), steg(k % 6), 255};
    }
    const auto g = static_cast<std::uint8_t>(8 + 10 * (i - 232));
    return gfx::Color{g, g, g, 255};
}

gfx::Color terminalDimmed(gfx::Color fg, gfx::Color bg)
{
    return color::mix(fg, bg, kDimMix);
}

bool terminalIsDark(const Theme::Variant& v)
{
    return color::luminance(v.colors.text) > color::luminance(v.colors.panel);
}

TerminalPalette terminalPalette(TerminalScheme scheme, bool dark,
                                const Theme::Variant& theme)
{
    switch (scheme) {
    case TerminalScheme::Solarized:
        return dark ? fixedPalette(kSolarized16, 0x839496, 0x002b36)
                    : fixedPalette(kSolarized16, 0x657b83, 0xfdf6e3);
    case TerminalScheme::One:
        return fixedPalette(dark ? kOneDark : kOneLight);
    case TerminalScheme::Tango:
        return dark ? fixedPalette(kTango16, 0xd3d7cf, 0x2e3436)
                    : fixedPalette(kTango16, 0x2e3436, 0xffffff);
    case TerminalScheme::Theme:
    default:
        return themePalette(dark, theme);
    }
}

} // namespace glaze
