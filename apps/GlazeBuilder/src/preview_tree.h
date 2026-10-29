#pragma once
#include <glaze/glaze.h>

// preview_tree — PREVIEWVANDRINGEN, en sanning.
//
// GlazeBuilder ritar sitt preview-träd UTANFÖR ett `AppWindow`: canvasen är
// en yta i editorfönstret, inte ett eget OS-fönster, så `AppWindow::drawTree`
// går inte att anropa. Vandringen är därför en PARALLELL implementation —
// och just därför måste den hålla exakt samma kontrakt. Två punkter som en
// parallell kopia lätt tappar:
//
//   * `drawAfterChildren` måste anropas (kroken är `protected`), annars syns
//     en Panels overlay-remsor i den byggda appen men aldrig i canvasen.
//   * SYNLIGHETSVAKTEN: `drawTree` returnerar tidigt för en osynlig widget,
//     så barnen till en DOLD Panel får inte ritas.
//
// Att den bor i `builder_preview` betyder en funktion i stället för en
// privat kopia, och därmed GRINDBAR headless (`tests/preview_gate.cpp`).
//
// Vad den INTE gör: hit-test (`hitTestTree` har sin egen vandring — och
// behöver ingenting av det här, remsorna är rena rit-overlays) och
// markeringsramar (de är builderns egen ritning ovanpå trädet).

namespace bld {

// Rita `w` och hela dess delträd i `c`. Speglar `AppWindow::drawTree`:
//
//   1. OSYNLIG widget ⇒ inget ritas, varken den eller barnen.
//   2. Widgetens EGEN rect bärs i Ctx:t under `draw()` — `Draw::phys`
//      projicerar varje ritanrop mot `c.box`, och en osatt box lägger allt
//      i en nollstor rect (widgeten "fanns" men ritade i ingenting).
//   3. Barnen ritas i förälderns `childArea`, klippta mot `clipRect` när
//      containern klipper.
//   4. `drawAfterChildren` SIST — ovanpå barnen och UTANFÖR barnklippet.
void drawPreviewTree(glaze::Widget& w, const glaze::Ctx& c);

} // namespace bld
