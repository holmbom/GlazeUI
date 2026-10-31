# OpenXR — platta Glaze-paneler i scenen

**Målet:** ett riktigt Glaze-widgetträd (knappar, fält, listor, reglage …) visas på en platt yta i
3D-scenen — i världen som en tavla eller en skylt på stolpe, på en hand, vid huvudet eller på en egen
pose — och styrs med 3D-gränssnittets pekare: strålen, staven eller fingret är musen, avtryckaren
eller fingrets tryck klickar, spaken rullar. Ett VR-tangentbord skriver i panelens fokuserade widget.
Appen väljer vilka paneler, var och hur.


**Status: klar.** `flat_panel_gate` och `vr_keyboard_gate` är skrivna men körs först före merge till `main`.

## Vägen

Panelen är ett `AppWindow` som aldrig visas (`setOffscreen`). Pumpen ritar det när något ändrats;
`AppWindow::updateOffscreen` läser tillbaka bilden till en `glaze::Bitmap` bara när en ny bild ritats,
och ytan i scenen visar bitmappen som textur. Fönstrets layout, routing, fokus, dubbelklick, fonter
och tema gäller oförändrat. Musen och tangenterna går in genom fönstrets egen ingång
(`AppWindow::handleEvent`).

## Gränssnitten (låsta)

- **Glaze** (`app_window.h`, skyddat): `OffscreenUpdate updateOffscreen(Bitmap&)`,
  `virtual bool acceptsOwnedWindow(const AppWindow&) const` (spärrar popuper och dialoger).
- **`glaze_openxr/flat_panel.h`:** `FlatPanel : AppWindow` — `sendEvent`, `refresh`, `image`,
  `snapshot`.
- **`glaze_openxr/interface3d.h`:** `Panel::flat` (`FlatContent`: `live` eller `image`, `FlatStyle`
  med tjocklek, ram och `Stand` — stolpen ned till marken), `PointerTarget::Flat`,
  `PointerHit::flatPx`.
- **`glaze_openxr/session.h`:** `Session::claimStick(Hand)` — spaken når inte förflyttningen
  (rullning i en panel); en pågående teleportering avbryts utan landning.
- **`glaze_openxr/vr_keyboard.h`:** `KeyboardKey`, `KeyboardLayout`, `KeyboardPreset`,
  `keyboardLayout`, `VrKeyboard` (`setLayout`, `setTarget`, `handleElement`).

## Steg

1. Glaze-bryggan, gränssnitten, `claimStick`, CMake.
2. **Paneler och pekare:** `flat_panel.cpp`, `flat_panel_logic.{h,cpp}` (träffpunkt → px, musens
   tillstånd per hand), platta paneler i `interface3d.cpp` (placering, synlighet, tavlan och stolpen,
   texturen bara vid ny bild, samma Auto-val, `consumed`, `claimHand`/`claimStick`).
3. **Tangentbordet:** `vr_keyboard.cpp`, `vr_keyboard_logic.{h,cpp}` (layouterna, tangent →
   händelser, UTF-8 delad på teckengränser).
4. **Testappen:** en panel med riktiga widgets, tangentbordet och skyltar på stolpe vid scenens
   stolpar — den närmaste skylten är levande och ändrar sin stolpes egenskaper, de andra visar
   stillbilder.

## Begränsningar

Popuper (Dropdown, menyer, verktygstips) och dialoger öppnas inte från en platt panel; fönstrets
overlay följer inte med bilden. Ett levande `FlatPanel` är ett eget fönster med egen grafikenhet.

## Plattformar

OpenXR bara Windows. `updateOffscreen` och `acceptsOwnedWindow` är gemensam kod (captureFrame finns i
alla backender); Metal och Linux är skrivna utan prov där.

## Prov

Bygget (0 varningar), sedan prov i headset. `flat_panel_gate` och `vr_keyboard_gate` skrivs och körs
före merge till `main`.

## Dokumenten

`MANUAL.md` (`openxr` och AppWindow), `OpenXR/CLAUDE.md`, `VERSION.md`, `TODO.md`, `doc/os/*/TESTS.md`.
