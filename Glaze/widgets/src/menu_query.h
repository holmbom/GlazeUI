#pragma once

// INTERN header (prejudikatet är `prop_parse.h` och `message_box_table.h` i
// samma mapp) — EN fråga om menymodulens levande presentationer, ställd
// utifrån.
//
// Den finns för borttagningens deny-check:
// `Widget::unadopt`/`releaseOwned` nekar ett delträd som är VÄRD för en
// öppen menypresentation, av samma skäl som en öppen `Dropdown`-popup nekas
// — stängningen är uppskjuten till pump-svansen, och en synkron rivning
// härifrån hade dragit in hela `App`s fönsterlivscykel i ett widget-pass.
//
// Varför en fri funktion i en intern header och inte en `Widget`-virtual
// som popup-fallet: en menypresentation ägs av MODULEN (`menu.cpp`s statiska
// lista) och inte av värd-widgeten, som inte vet om att den är värd. Frågan
// hör därför hemma hos den som äger listan.
//
// Den är INTE publik: `glaze/menu.h` är API:t, och en app har inget ärende
// till presentationsregistret.

namespace glaze {

class Widget;

// Är någon LEVANDE menypresentations VÄRD lika med `subtreeRoot`, eller
// ligger den någonstans under den? Vandrar `parent()`-kedjan uppåt från
// varje värd. False när inga menyer är öppna — det normala fallet.
//
// TIDSFÖNSTRET ÄR TVÅ PUMPVARV och inte ett, till skillnad från
// `Dropdown`-fallet. Skälet är två uppskjutningar staplade på varandra:
// `Menu::close()` köar popup-fönstrets stängning, App verkställer den i
// svansen och kör då presentationens `finish()`, och `finish()` POSTAR sin
// egen självdestruktion (en presentation får aldrig raderas inifrån sin
// egen `onClose`). `Menu::isOpen()` svarar därför false redan efter första
// varvet — den läser `Menu`s egen lista, som `finish()` tömmer — medan den
// här frågan läser MODULENS lista, som lever ett varv till. Skrivet
// beteende, grindat i `tests/unadopt_gate.cpp`.
bool menuPresentationHostedIn(const Widget& subtreeRoot);

} // namespace glaze
