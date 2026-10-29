// Tangentkodningen — tabellen och reglerna står i keys.h.

#include "keys.h"

#include <string>

namespace glaze::term {
namespace {

// Tangentens level-0-tecken (ASCII), eller 0 när den inte skriver ett.
char level0(gfx::Key k)
{
    using K = gfx::Key;
    if (k >= K::A && k <= K::Z)
        return static_cast<char>('a' + (static_cast<int>(k) - static_cast<int>(K::A)));
    if (k >= K::N0 && k <= K::N9)
        return static_cast<char>('0' + (static_cast<int>(k) - static_cast<int>(K::N0)));
    switch (k) {
    case K::Minus:        return '-';
    case K::Plus:         return '+';
    case K::Equals:       return '=';
    case K::Comma:        return ',';
    case K::Period:       return '.';
    case K::Slash:        return '/';
    case K::Backslash:    return '\\';
    case K::Semicolon:    return ';';
    case K::Apostrophe:   return '\'';
    case K::LeftBracket:  return '[';
    case K::RightBracket: return ']';
    case K::Grave:        return '`';
    case K::Percent:      return '%';
    case K::Asterisk:     return '*';
    default:              return 0;
    }
}

// Ctrl + level-0-tecknet → styrtecknet, eller −1 när kombinationen inte har
// ett (Ctrl+3 … är appens).
int controlChar(char c)
{
    if (c >= 'a' && c <= 'z')
        return c & 0x1F;
    switch (c) {
    case '2':  return 0x00;
    case '[':  return 0x1B;
    case '\\': return 0x1C;
    case ']':  return 0x1D;
    case '6':  return 0x1E;
    case '-':
    case '/':  return 0x1F;
    default:   return -1;
    }
}

std::string csiU(int kod, int mod)
{
    return "\x1b[" + std::to_string(kod) + (mod > 1 ? ";" + std::to_string(mod) : "") + "u";
}

// `CSI 1;m X` med modifierare, annars `prefix X` (CSI eller SS3).
std::string letterKey(char fin, int mod, bool ss3)
{
    if (mod > 1)
        return "\x1b[1;" + std::to_string(mod) + fin;
    return std::string(ss3 ? "\x1bO" : "\x1b[") + fin;
}

std::string tildeKey(int n, int mod)
{
    return "\x1b[" + std::to_string(n) + (mod > 1 ? ";" + std::to_string(mod) : "") + "~";
}

} // namespace

std::string encodeKey(gfx::Key k, std::uint32_t mods, const KeyModes& m)
{
    using K          = gfx::Key;
    const bool ctrl  = (mods & gfx::kModCtrl) != 0;
    const bool shift = (mods & gfx::kModShift) != 0;
    const bool alt   = (mods & gfx::kModAlt) != 0;
    if (mods & gfx::kModCmd)
        return {};   // Cmd är appens (macOS)
    const int  mod   = 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (ctrl ? 4 : 0);
    const bool kitty = (m.kittyFlags & 1) != 0;
    const bool meta  = alt && m.altAsMeta;

    // KITTYPROTOKOLLET FÖRST: pilarna, Home/End
    // och F1–F4 går i CSI-form också i DECCKM — aldrig SS3 — och F3 är
    // `CSI 13 ~` (`CSI R` är markörrapportens form).
    if (kitty) {
        switch (k) {
        case K::Up:    return letterKey('A', mod, false);
        case K::Down:  return letterKey('B', mod, false);
        case K::Right: return letterKey('C', mod, false);
        case K::Left:  return letterKey('D', mod, false);
        case K::Home:  return letterKey('H', mod, false);
        case K::End:   return letterKey('F', mod, false);
        case K::F1:    return letterKey('P', mod, false);
        case K::F2:    return letterKey('Q', mod, false);
        case K::F3:    return tildeKey(13, mod);
        case K::F4:    return letterKey('S', mod, false);
        default:       break;
        }
    }

    switch (k) {
    case K::Up:       return letterKey('A', mod, m.cursorKeys);
    case K::Down:     return letterKey('B', mod, m.cursorKeys);
    case K::Right:    return letterKey('C', mod, m.cursorKeys);
    case K::Left:     return letterKey('D', mod, m.cursorKeys);
    case K::Home:     return letterKey('H', mod, m.cursorKeys);
    case K::End:      return letterKey('F', mod, m.cursorKeys);
    case K::Insert:   return tildeKey(2, mod);
    case K::Delete:   return tildeKey(3, mod);
    case K::PageUp:   return tildeKey(5, mod);
    case K::PageDown: return tildeKey(6, mod);
    case K::F1:       return letterKey('P', mod, true);
    case K::F2:       return letterKey('Q', mod, true);
    case K::F3:       return letterKey('R', mod, true);
    case K::F4:       return letterKey('S', mod, true);
    case K::F5:       return tildeKey(15, mod);
    case K::F6:       return tildeKey(17, mod);
    case K::F7:       return tildeKey(18, mod);
    case K::F8:       return tildeKey(19, mod);
    case K::F9:       return tildeKey(20, mod);
    case K::F10:      return tildeKey(21, mod);
    case K::F11:      return tildeKey(23, mod);
    case K::F12:      return tildeKey(24, mod);

    case K::Tab:
        if (ctrl)
            return {};   // Ctrl+Tab och Ctrl+Shift+Tab: vägen ut
        if (kitty && mod > 1)
            return csiU(9, mod);
        if (shift)
            return "\x1b[Z";
        return meta ? "\x1b\t" : alt ? std::string() : "\t";

    case K::Return: {
        if (kitty && mod > 1)
            return csiU(13, mod);
        const std::string cr = m.newline ? "\r\n" : "\r";
        return meta ? "\x1b" + cr : cr;
    }

    case K::Backspace: {
        if (kitty && mod > 1)
            return csiU(127, mod);
        const std::string bs = ctrl ? "\x08" : "\x7f";
        return meta ? "\x1b" + bs : bs;
    }

    case K::Escape:
        if (kitty)
            return csiU(27, mod);
        return "\x1b";

    case K::Space:
        if (ctrl && !alt)
            return kitty ? csiU(32, mod) : std::string(1, '\0');
        if (meta && !ctrl)
            return kitty ? csiU(32, mod) : "\x1b ";
        return {};   // blanksteget är text

    default:
        break;
    }

    // DE SKRIVANDE TANGENTERNA — bara med Ctrl eller Alt; annars är de text.
    const char c = level0(k);
    if (c == 0 || ctrl == alt) {
        return {};   // ingen modifierare (text) eller Ctrl+Alt (AltGr-regeln)
    }
    if (ctrl) {
        if (kitty)
            return csiU(static_cast<unsigned char>(c), mod);
        if (shift)
            return {};   // övriga Ctrl+Shift-kombinationer är appens
        const int cc = controlChar(c);
        return cc < 0 ? std::string() : std::string(1, static_cast<char>(cc));
    }
    // Alt (utan Ctrl)
    if (!m.altAsMeta)
        return {};   // Option skriver tecken (macOS)
    if (kitty)
        return csiU(static_cast<unsigned char>(c), mod);
    const char t = shift && c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
    return std::string("\x1b") + t;
}

std::string encodePaste(std::string_view text, bool bracketed)
{
    std::string t;
    t.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
            continue;   // CRLF: LF:en nedan blir CR
        t += text[i] == '\n' ? '\r' : text[i];
    }
    if (!bracketed)
        return t;
    static constexpr std::string_view kSlut = "\x1b[201~";
    for (std::size_t p; (p = t.find(kSlut)) != std::string::npos;)
        t.erase(p, kSlut.size());
    return "\x1b[200~" + t + std::string(kSlut);
}

ClipboardKey clipboardKey(gfx::Key k, std::uint32_t mods)
{
    const std::uint32_t m = mods & (gfx::kModCtrl | gfx::kModShift | gfx::kModAlt |
                                    gfx::kModCmd);
#ifdef __APPLE__
    if (m == gfx::kModCmd && k == gfx::Key::C)
        return ClipboardKey::Copy;
    if (m == gfx::kModCmd && k == gfx::Key::V)
        return ClipboardKey::Paste;
#else
    const std::uint32_t cs = gfx::kModCtrl | gfx::kModShift;
    if ((m == cs && k == gfx::Key::C) || (m == gfx::kModCtrl && k == gfx::Key::Insert))
        return ClipboardKey::Copy;
    if ((m == cs && k == gfx::Key::V) || (m == gfx::kModShift && k == gfx::Key::Insert))
        return ClipboardKey::Paste;
#endif
    return ClipboardKey::None;
}

} // namespace glaze::term
