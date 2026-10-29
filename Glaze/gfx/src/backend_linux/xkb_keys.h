#pragma once
// xkb → gfx::Key — EN sanning för BÅDA Linux-drivrutinerna.
//
// Tabellen finns i ETT exemplar och delas av `translateKeysym` i
// wayland_display.cpp och x11_display.cpp: två kopior som ingen mäter glider
// isär, exakt samma glidrisk som builderns `applyProp` hade.
//
// KONTRAKTET bor i gfx/events.h vid `enum class Key` och ska läsas därifrån:
// `Key` är LEVEL-0-SYMBOLEN i användarens aktiva layout, med numpaden som
// skrivet undantag. Den här filen är implementationen av just det.

#include <xkbcommon/xkbcommon.h>

#include "gfx/events.h"

namespace gfx {

// Översätter ett tryck till `Key` enligt kontraktet. Behöver BÅDE state
// och keymap: numpaden läses ur det FAKTISKA läget (NumLock), allt annat
// ur level 0. Null-säker — saknad state/keymap ger `Key::Unknown`.
Key translateXkbKey(xkb_state* state, xkb_keymap* keymap,
                    xkb_keycode_t keycode);

// MODIFIERARTANGENTENS EGET EVENT bär läget EFTER tangenten (kontraktet vid
// `kModCtrl` i gfx/events.h) — men X11:s `state` och Waylands senaste
// `wl_keyboard.modifiers` är läget FÖRE (Wayland skickar `modifiers` efter
// `key`). `before` är den masken, `sym` tangentens symbol: trycket av en
// Ctrl-, Shift-, Alt- eller Super-tangent lägger till dess bit och släppet
// tar bort den; varje annan tangent lämnar masken orörd. FÖRBEHÅLLET: släpps
// den ena av två hållna Ctrl tas biten bort fast den andra hålls — nästa
// event med mask rättar det. AltGr (`ISO_Level3_Shift`) är inte Alt, och
// Hyper räknas inte (dess modifierare skiljer mellan konfigurationerna).
std::uint32_t xkbModsAfterKey(std::uint32_t before, xkb_keysym_t sym,
                              bool pressed);

// SKRIVER TRYCKET TEXT? — false när Ctrl, Alt
// (Mod1) eller Logo (Mod4) är aktiva och INTE konsumerade av tangenten: en
// sådan kombination är ett kommando, som på Windows och macOS. AltGr är
// Level3 och står inte i listan, så dess tecken skrivs. Drivrutinerna
// frågar FÖRE compose (Ctrl + död tangent startar ingen sekvens) och före
// `xkb_state_key_get_utf8`, som annars ger "1" för Ctrl+1 och "a" för
// Alt+a. Null-säker — saknad state/keymap ger false.
bool xkbTypesText(xkb_state* state, xkb_keymap* keymap,
                  xkb_keycode_t keycode);

} // namespace gfx
