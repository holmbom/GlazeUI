#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include <gfx/events.h>

// INTERN — TANGENTKODNINGEN: en tangent
// och dess modifierare → bytes till programmet. En REN funktion, grindad
// headless (terminal_ui_gate K); widgeten anropar den ur `keyDown`.
//
// TOM STRÄNG = TANGENTEN ÄR INTE TERMINALENS: den går vidare till förfäderna,
// fönstrets acceleratorer och appens menyer — eller, för en skrivande
// tangent, som TEXT genom `textInput` (IME och döda tangenter fungerar då).
//
// XTERMS FORMER som förval: pilar `CSI A–D` (`SS3 A–D` i DECCKM), Home/End
// `CSI H/F`, Insert/Delete/PgUp/PgDn `CSI 2~ 3~ 5~ 6~`, F1–F4 `SS3 P–S`,
// F5–F12 `CSI 15~ … 24~`, och modifierarna som `CSI 1;m X` respektive
// `CSI n;m ~` med m = 1 + Shift + 2·Alt + 4·Ctrl. Enter → CR (CRLF i LNM),
// Backsteg → DEL, Ctrl+Backsteg → BS, Tab → HT, Shift+Tab → `CSI Z`, Escape
// → ESC. Ctrl+bokstav → bokstaven & 0x1F med regeln Ctrl UTAN Alt (Windows
// levererar AltGr som Ctrl+Alt — `@` och `€` ska skrivas, inte bli
// styrtecken); Ctrl+Space/2 → NUL, Ctrl+[ \ ] → 0x1B–0x1D, Ctrl+6 → 0x1E,
// Ctrl+- och Ctrl+/ → 0x1F (level-0-symbolerna: finns tangenten inte i
// layouten går den inte att trycka). Alt som META → ESC + tecknet (versalt
// med Shift); en symbol med Alt+Shift får level-0-tecknet — skriven gräns.
//
// KITTYPROTOKOLLET med flaggan 1 ("disambiguate"), när programmet bett om
// det (`CSI > 1 u`): Escape blir `CSI 27 u`, Ctrl/Alt + en skrivande tangent
// `CSI kod;m u`, och Enter/Tab/Backsteg MED modifierare `CSI 13/9/127;m u` —
// Shift+Enter och Ctrl+Shift+X blir entydiga. Pilarna, Home/End och F1–F4 går
// då i CSI-form också i DECCKM (aldrig SS3), och F3 är `CSI 13 ~` (`CSI 13;m ~`
// med modifierare) — `CSI R` är markörrapportens. Utan protokollet skickar
// Shift+Enter CR, som i xterm.
//
// TAS ALDRIG: Ctrl+Tab och Ctrl+Shift+Tab (flikkontrollens väg ut), Cmd+
// vad som helst (macOS-appens), övriga Ctrl+Shift-kombinationer utan
// kittyprotokollet, och Ctrl+Alt (AltGr). Numpadens DECKPAM-koder (SS3 för
// siffrorna) stöds inte: numpaden skriver text.

namespace glaze::term {

struct KeyModes {
    bool cursorKeys = false;   // DECCKM
    bool newline    = false;   // LNM
    int  kittyFlags = 0;       // kittyprotokollet, aktuell skärm
    bool altAsMeta  = true;    // Alt som Meta (Terminal::setAltAsMeta)
};

std::string encodeKey(gfx::Key k, std::uint32_t mods, const KeyModes& m);

// INKLISTRINGEN: radsluten (CRLF och LF) blir CR, som en tryckt
// Enter; med bracketed paste (2004) ramas texten in med `ESC[200~ … ESC[201~`
// och en inbäddad slutmarkör tas bort — annars hade en klistrad text kunnat
// avsluta inramningen och köra resten som kommandon.
std::string encodePaste(std::string_view text, bool bracketed);

// KOPIERA OCH KLISTRA IN — terminalens egna genvägar, före kodningen:
// Windows och Linux Ctrl+Shift+C/V och Ctrl+Insert/Shift+Insert, macOS
// Cmd+C/V (Cmd utan Ctrl och Option).
enum class ClipboardKey : std::uint8_t { None, Copy, Paste };
ClipboardKey clipboardKey(gfx::Key k, std::uint32_t mods);

} // namespace glaze::term
