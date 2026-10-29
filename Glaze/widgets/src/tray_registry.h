#pragma once
#include <cstdint>

#include <gfx/events.h>

// INTERN header — ingår inte i publika API:t.
//
// SYSTEMFÄLTETS REGISTER — kopplingen mellan
// `glaze::App` och de levande `TrayIcon`-objekten. Ikonen är ingen widget och
// sitter inte i ett fönster, så dess händelser har ingen fönsterväg att följa:
// `gfx::EventType::Tray` kommer med windowId 0 och tas i App:s app-switch
// (som Quit och Wakeup), och därifrån hit.
//
// Registret bor i src/tray_icon.cpp. Varje `TrayIcon` står i det från
// konstruktorn till destruktorn med sitt id — det LÄGSTA lediga i
// 1–`gfx::kTrayIdMax` — och en GENERATION, så att en handler som
// förstör ikonen (eller förstör den och skapar en ny med samma id) syns för
// dispatchen innan den rör något mer.

namespace glaze {
class AppWindow;
class TrayIcon;
} // namespace glaze

namespace glaze::detail {

// `App::dispatch` → hit, för varje `EventType::Tray`. `modalTop` är
// modalstackens topp (nullptr utan modal): systemfältets klick fyras inte
// medan en modal dialog är öppen, och den lyfts i stället. Okänt id
// och dold ikon gör ingenting.
void trayDispatch(const gfx::Event& e, AppWindow* modalTop);

// `App::languageChangedAll` → hit: tooltipen är OS:ets egen och bärs av
// ingen widget, så varje synlig ikon trycker om sin upplösta text till
// backenden. Menyn behöver inget — en öppen meny har redan stängts av
// språkbytet, och nästa öppning tar en ny snapshot.
void trayLanguageChanged();

// Ikonens id i registret (0 = inget). För grinden (tests/tray_gate.cpp):
// id:na ska vara de lägsta lediga, och ett id frigörs när ikonen förstörs.
std::uint32_t trayIdOf(const TrayIcon& t);

} // namespace glaze::detail
