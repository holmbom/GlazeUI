# macOS — kvar att grinda (backend_metal)

Se [../README.md](../README.md) för hur listan används: ta BORT raden när den
är grön, skriv en 🐛 i `/TODO.md` om den i stället avslöjar en bugg.

Bygg: Apple clang + Homebrew-cmake/ninja.
```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/apps/demo/demo.app/Contents/MacOS/demo
```
Loggar går till unified log + stderr.

**2026-10-21, Mac-passet inför 0.2.0 — allt som går att köra här är grönt.**
Statisk och delad svit (0 varningar, `ctest` grönt — `dll_gate` del A körs inte
på Mach-O, med flit), röktest, grindarna med `MTL_DEBUG_LAYER=1` och varje
handrad i demon, galleriet, systemfältet, dockningen, fönsterlabbet,
GlazeBuilder och ett provprogram för det demon saknar (Erik). Fel som hittades
och rättades samma dag: terminalens storlek vid start (`pty_posix.cpp`),
draperingens uppskjutna bild (`contentVersion`, H8), Slates inaktiva fält
(TODO 67), det dolda fönstrets vsync-väntan (`setPresenting`) och
macOS 27-SDK:ns varningar (`metal_gpu.mm`). Det som står kvar kräver hårdvara
som inte fanns vid passet.

## Kräver hårdvara som saknas vid Mac-passet (Erik 2026-10-21)

Mac-passet inför 0.2.0 kördes på en MacBook Pro M1 med en mus med mitten- och
sidoknappar — utan andra skärm, PC-tangentbord och Intel-Mac. Raderna nedan
kunde därför inte köras, och Erik beslöt att de står här i stället för att
blockera 0.2.0: de körs när hårdvaran finns. Avsnittet de kom ur står inom
hakparentes.

- [ ] *[Kvar]* **L6a: popup-kontraktet på en SEKUNDÄRSKÄRM** (`WindowDesc::
  popupAnchorWindow/Rect`, `metal_window.mm`). Placeringen är punkter med
  top-left-flip mot HUVUDskärmens höjd — punkt/px-flippen är den klassiska
  felkällan när fönstret ligger på en annan skärm. På huvudskärmen är
  flip-uppåt och klamp-i-sidled mätgrindade, och dropdownarna + skalbytet
  mellan skärmar är Erik-testade (2026-08-10).
  *Test:* öppna tema-dropdownen nära under- resp. högerkanten medan
  demofönstret står på den ANDRA skärmen — listan ska vända uppåt resp.
  klampas in, på rätt skärm. (Att ägda fönster försvann på annan skärm är
  rättat BLINT 2026-10-07 — se sista avsnittet; huvudfönstret har ingen
  ägare och har alltid gått att dra.)

- [ ] *[Ägda fönster är inte längre barnfönster — BLINT skrivet (TODO-post 35, 2026-10-07)]* **Andra skärmen:** starta demo, Arkiv → Nytt fönster (Ctrl+N), dra det
  nya fönstret till den ANDRA skärmen → det syns där och STANNAR. Flytta
  sedan startfönstret → det ägda följer INTE med (det gjorde det förr).

- [ ] *[Navigationsklustret och numpaden — BLINT skrivet (TODO 31, 2026-10-07)]* **Med ett PC-tangentbord:** `demo --gallery`, fokusera en lista och
  tryck Insert, Page Up, Page Down — logga `e.key` i en `event()`-krok (eller
  bind dem som genvägar i en meny): `Insert`, `PageUp`, `PageDown`. Ger
  Insert något annat än keyCode 114 är antagandet fel — skriv det i
  `metal_window.mm` och här.

- [ ] *[Navigationsklustret och numpaden — BLINT skrivet (TODO 31, 2026-10-07)]* **Numpaden med NumLock "av"** (PC-tangentbordets NumLock-tangent, eller
  Clear på ett Apple-tangentbord): numpad-1 ger `KP_N1`, ALDRIG `KP_End`, och
  numpad-5 ger `KP_N5`, aldrig `KP_Begin`. En meny med genvägen "Num End"
  ska alltså aldrig gå att träffa på Mac — det är RÄTT.

- [ ] *[Ritytan — Metal och Core Text BLINT skrivna (TODO 27, CANVAS_2D etapp 1–12, 2026-10-09/10)]* **Två skärmar.** *Vad man gör:* dra galleriet till den andra skärmen
  och tillbaka. *Rätt utfall:* `windowDidChangeScreen:` mäter om och köar
  `WindowDisplayChanged` — linjalens rad visar den andra skärmens id och
  täthet, linjen mäter 100 mm där också och kartans skaltal följer. En skärm
  med Apples 72-dpi-uppskattning ger "mm: antagen" och "≈1:4000".

- [ ] *[Systemfältet, dockan och hovern i popuper — BLINT skrivet (TODO 69 och 68, SYSTEMFÄLTET, 2026-10-12)]* **Menyns plats på en ANDRA skärm** (`trayIconRect`: `w.screen` och
  dess `visibleFrame`). *Vad man gör:* med två skärmar och en menyrad på
  båda, högerklicka ikonen i den andra skärmens menyrad. *Rätt utfall:*
  menyn öppnas under menyraden på DEN skärmen.

- [ ] *[Fönster och timer — BLINT skrivet (TODO 78, FONSTER_OCH_TIMER, 2026-10-12)]* **`gfx::screens()` med flippen** (`metal_platform.mm` — `NSScreen.screens`,
  `frame`/`visibleFrame` genom `toPlacement`, primär = `screens[0]`). *Vad
  man gör:* två skärmar, den andra OVANFÖR eller till vänster; `demo
  --splash` och labbets spara/återställ. *Rätt utfall:* splashen mitt i
  primärskärmens synliga yta (under menyraden, ovanför dockan); återställd
  plats exakt.

- [ ] *[Fönster och timer — BLINT skrivet (TODO 78, FONSTER_OCH_TIMER, 2026-10-12)]* **CenterOwner** (`showOwned` → `clientToScreen` över ägarens
  `contentArea()`; meddelanderutan och filväljaren har den sedan 78). *Vad
  man gör:* öppna en meddelanderuta och filväljaren från ett fönster på en
  andra skärm. *Rätt utfall:* dialogen mitt över ägaren, på ägarens skärm.

- [ ] *[Ritytans textmått och den antagna skärmen — BLINT skrivet (TODO 83, REQ2_RITYTAN, 2026-10-13)]* **display_gate -v med en EXTERN skärm utan EDID.** *Rätt utfall:*
  `Assumed 3,7795 px/mm` i buffertens px både vid "Standard" och vid "Mer
  utrymme", och F5 är grön där. (Den inbyggda är grön 2026-10-14: Measured
  298 dpi i buffertens px, id `APP-A048-…`.)

- [ ] *[Grafikkorten — metal_gpu.mm, BLINT skrivet (REQ9 1, REQ9_GRAFIKKORTET, 2026-10-20)]* **En Intel-Mac med två kort och/eller eGPU**: `--gpus` och sedan
  `--gpu` för varje kort. *Rätt utfall:* Intel integrerat, AMD diskret, eGPU
  externt; loggen och K3 visar att varje kort ritar när det väljs.

- [ ] *[Grafikkorten — metal_gpu.mm, BLINT skrivet (REQ9 1, REQ9_GRAFIKKORTET, 2026-10-20)]* **Listan slår inte om systemets GPU**: på en Mac med automatisk
  grafikväxling, `terrain_perf --gpus` medan Aktivitetskontrollens
  GPU-kolumn/Energi-fliken är öppen. *Rätt utfall:* den kraftfulla GPU:n
  vaknar inte av listan.

- [ ] *[Träd, listor och bilder ur pixlar (TODO 75, TRAD_LISTOR_BILDER, 2026-10-12)]* **Bilder ur pixlar på en skärm med skala 1** (`picture.cpp`, `tool_button.cpp`). Retina-delen är grön (Erik 2026-10-21). *Vad man gör:* `demo --gallery`, grupp 5 och fliken "Paneler och remsor", grupp 9, på en extern skärm med skala 1. *Rätt utfall:* grupp 5:s tredje bild skarp, grupp 9:s ringikon i båda remsorna och tonad i den inaktiverade.

- [ ] *[Fönster och timer — BLINT skrivet (TODO 78, FONSTER_OCH_TIMER, 2026-10-12)]* **Geometrin och `placement()` på en ANDRA skärm** (`metal_window.mm`: `windowDidMove:`, `windowShouldZoom:toFrame:`, `trackNormalFrame`). En skärm är grön (Erik 2026-10-21). *Vad man gör:* i `demo --window-lab` flytta, zooma, minimera och återställ på den andra skärmen; "Spara geometrin" och "Öppna nytt med den sparade". *Rätt utfall:* geometriraden visar normalrutan under zoom; det nya fönstret hamnar exakt över det gamla, på den skärmen.

- [ ] *[App-API:t (APP_API — 2026-08-13)]* **`gfx::Key` på numpaden** (`metal_window.mm` `translateKey`). Skiljetecknen med svensk layout är gröna (Erik 2026-10-21); numpaden kräver ett tangentbord med numerisk del. *Vad man gör:* `mac_prov` (scratchpad) eller en `event()`-krok som loggar `e.key`: numpad-1 med och utan NumLock/Clear, och numpadens Enter på en fokuserad knapp. *Rätt utfall:* `KP_N1` i båda lägena (macOS levererar alltid `KP_*` — kontraktet vid `enum class Key`), och Enter aktiverar knappen (ihopfälld med `Return`). Faller det ut som `End` har macOS ett läge vi inte kände till, och kontraktet ska skrivas om.

### Navigationsklustret och numpaden — BLINT skrivet (TODO 31, 2026-10-07)

**Vad som ändrats:** `gfx::Key` fick `PageUp`/`PageDown`/`Insert` och
numpadens navigationsläge som egna poster (`KP_End` m.fl.). På Windows och
Linux är översättningen grindad (`tests/numpad_gate.cpp`); på macOS ska
numpaden ALDRIG ge navigationsposterna — kontraktet och tabellen står vid
`enum class Key` i `Glaze/gfx/include/gfx/events.h`.

**Var koden bor:** `Glaze/gfx/src/backend_metal/metal_window.mm`,
`translateKey`: keyCode 116 → `PageUp` (`kVK_PageUp`), 121 → `PageDown`
(`kVK_PageDown`) och **114 → `Insert` (`kVK_Help`)**. Det sista är ett
ANTAGANDE och inte kontrollerat mot Apples dokumentation: Events.h namnger
0x72 bara `kVK_Help` — det är den plats där ett PC-tangentbord har sin
Insert (USB HID 0x49 "Keyboard Insert"), och GLFW mappar den så.

**Vad man gör och rätt utfall:**

### Ritytans textmått och den antagna skärmen — BLINT skrivet (TODO 83, REQ2_RITYTAN, 2026-10-13)

Koden: reserven i `display_util.cpp` (96 dpi i BUFFERTENS px, ingen skala —
bufferten antas lika tät som panelen; en omräkning i "Mer utrymme"
byggs inte) och anropen i `metal_window.mm` `measureDisplay`; mätningen
`gfx::measureText` (`sdf.cpp`) bygger på att Core Texts `measure` och
`rasterGlyph` ger samma steg (båda genom reservens cache).

### Grafikkorten — metal_gpu.mm, BLINT skrivet (REQ9 1, REQ9_GRAFIKKORTET, 2026-10-20)

`Glaze/gfx/src/backend_metal/metal_gpu.mm` (listan, kortet i
`preferredMetalDevice`, `metalAdapterId`, `logDevice`) har aldrig kompilerats.
Den ska stå i BÅDE källistan och ARC-listan i `Glaze/gfx/CMakeLists.txt`.

### Uppladdningarna på Metal — Managed-vägen BLINT (TODO 102, REQ7_RITYTAN, 2026-10-14)

Koden: `Glaze/gfx/src/backend_metal/metal_scene.mm` (`bufferStorage`/
`wroteDirect`, `createBuffer`, `updateBuffer`) — statiska buffertar i Managed
och `didModifyRange:` efter varje direkt CPU-skrivning när GPU:n saknar delat
minne (`device.hasUnifiedMemory == NO`, en Intel-Mac med diskret GPU). Grenen
KOMPILERAS bara när den minsta macOS-versionen är under 27 (macOS 27 finns bara
för Apple silicon, och SDK:n avråder från Managed där) — på en M1 med macOS 27
byggs och körs den aldrig. Shared-vägen (Apple silicon) grindas av sviten.

- **Managed-vägen på en Intel-Mac med diskret GPU** — *Gör:* bygg på macOS 26
  eller äldre (eller med `-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0`), kör
  `scene_pixel_gate` (A9–A11, B12) och `demo --gallery` → "Rityta"
  (panorera, zooma, vrid). *Rätt utfall:* fallen gröna; kartan utan
  artefakter — inga linjer eller bilder med gammalt eller halvt innehåll
  efter en ändring.
