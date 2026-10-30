# Linux — kvar att grinda (backend_linux)

Se [../README.md](../README.md) för hur listan används: ta BORT raden när den
är grön, skriv en 🐛 i `/TODO.md` om den i stället avslöjar en bugg.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/apps/demo/demo                       # Wayland (auto)
GLAZE_LINUX_DRIVER=x11 ./build/apps/demo/demo
```

Backenden är Erik-testad och grön på **TVÅ kompositörer**: Debian 13 / GNOME /
Wayland + XWayland (2026-08-09) och **KDE Plasma / KWin (2026-08-23)**.
Det som står kvar här är miljöer som inte funnits tillgängliga.

## Miljöer som inte kunnat köras

- [ ] **Riktig Xorg-session** (inloggningsväxel, inte XWayland). Hela X11-lagret
  är bara kört under XWayland.
  *Test:* demon lever, chrome-drag/resize, dropdown, klippbord, `Xft.dpi`-byte i
  drift (`echo "Xft.dpi: 144" | xrdb -merge` → fönstret räknar om, texten blir
  skarp, ingen felritad frame under omställningen).
  *Känd degradering att verifiera, inte rätta:* ett HELT skymt animerande
  fönster stryps till ~1 frame/s av vsync-swappen (uppmätt under XWayland;
  pumpen fryser inte).
  *Tillkom med BUGGFIX_TRE B (2026-08-13):* ordningen mellan
  `PropertyNotify(_NET_WM_STATE)` och `ConfigureNotify` vid maximera/
  återställ är **WM-beroende och odokumenterad**, och den är bara MÄTT
  under Mutter/XWayland — där kommer propertyn FÖRE configuren vid
  maximering men EFTER vid återställning. Koden GISSAR inte ordningen alls:
  skulden (`restoreSizeOwed_`) konsumeras synkront på restore-flanken och
  målet räknas ur den LAGRADE logiska storleken, så en rect som hinner
  emellan klassas som vägran och behåller logisk storlek. Två tidigare
  ordnings-KLASSIFICERANDE varianter föll — se fältkommentaren i
  `x11_backend.h`. Beteendet ska ändå ses köra på riktig Xorg med en annan
  WM.
  *Test:* demon 800×520 vid `Xft.dpi: 96` → maximera → skala 1.0 → 1.25 i
  `skalaDd` → återställ. Rätt utfall **1000×650 fysiska**
  (`xdotool getactivewindow getwindowgeometry` eller `xwininfo -name …`), och
  skala tillbaka till 1.0 ⇒ 800×520 igen. Faller det till 800×520
  respektive 640×416 har den WM:ens event-ordning slagit igenom någonstans
  vi inte täckt.

- [ ] **Blandad DPI — kräver tvåskärmsrigg.** Per-surface-outputspårningen
  (`wl_surface.enter/leave` → fönstrets egna outputs, `global_remove`-städning)
  är bara delverifierad på en skärm.
  *Test:* två skärmar med olika skalning (100 % + 200 %). Ett fönster på
  100 %-skärmen ska INTE renderas i 200 %; dra fönstret mellan skärmarna →
  skalan följer med; koppla ur en skärm medan appen kör → ingen krasch, inga
  Wayland-protokollfel (`WAYLAND_DEBUG=1`).
  OBS: gäller bara heltalsskale-fallbacken — `wp_fractional_scale_v1` vinner på
  GNOME ≥ 47.

- [ ] **Ubuntu 24.04 LTS** — "inga byggda beroenden"-beviset.
  *Test:* `ldd build/apps/demo/demo` ska bara visa systembibliotek (ingen
  FetchContent-byggd kod).

- [ ] **Dubbelklickets tid ur GNOME och musens modifierare med riktig mus**
  (TODO 17, 2026-10-07). `gfx::doubleClickMs()` läser portalens
  `org.gnome.desktop.peripherals.mouse/double-click` (`linux_platform.cpp`)
  och faller annars på 400 — WSLg har ingen portal, så bara fallbacken är
  körd. Musens `mods` (X11 `state`, Wayland `WaylandDisplay::mods_`) är
  grindade genom fönstrets ingång men aldrig klickade med en riktig mus.
  *Test:* GNOME-session, `demo --gallery` grupp 14. Ctrl+klick lägger till en
  rad, Shift+klick markerar ett spann — på Wayland OCH X11. Sätt
  Inställningar → Hjälpmedel → dubbelklicksfördröjning till det längsta:
  ett långsamt dubbelklick ska ändå ge "Öppnade: <namn>" i statusraden, även
  när inställningen ändras MEDAN demon kör (SettingChanged).

## Referensbilder

- [x] **`screenshots/start_window_125.png`** — omrenderad 2026-08-10 efter
  US.5 (skala-dropdownen på startsidan; 1000×650, scale 1.25, `--scale
  1.25` cmp-identisk med nativ rendering). Referens att jämföra Windows-
  och macOS-bilderna mot; se [../README.md](../README.md).
- [x] **`screenshots/start_window_100.png`** — renderad 2026-08-10 med
  `--scale 1.0` (800×520): geometri-diffbar 1:1 mot kommande Windows-/
  macOS-bilder med samma suffix.

## Ritytan på riktig hårdvara och med riktig mus (TODO 27, CANVAS_2D, 2026-10-10)

Allt nedan provas i **`demo --gallery` → fliken "Rityta"** (CANVAS_2D etapp
12): kartan, grafen med en inaktiverad kopia och linjalprovet. Grindat i
WSLg på Wayland och X11: `display_gate`, `middle_gate`, `canvas_gate` (med
SYNTETISKA händelser) och `scene_pixel_gate`. WSLg anger 0 mm på båda
utgångarna (Weston-RDP och XWaylands `rdp-0`), så här har bara `Assumed`
synts — linjalens rad säger då "mm: antagen" och kartan "≈1:4000".

- [ ] **`display_gate -v` på en riktig skärm**, `GLAZE_LINUX_DRIVER=x11` och
  `=wayland`. *Rätt utfall:* `Measured` på båda — X11 via XRandR (id ur
  EDID, tillverkare-produkt-serie, annars utgångens namn), Wayland via
  `wl_output` (id = märke + modell) — med samma täthet, ungefär 90–110 dpi på
  en skrivbordsskärm och 140–260 på en laptop.
- [ ] **Linjalprovet: 100 mm vid 100 %, 125 % och 150 %** (GNOME
  Inställningar → Skärmar, med fraktionell skalning påslagen för 125 %; KDE
  Bildskärmskonfiguration). *Vad man gör:* håll en linjal mot linjen utan att
  ha kalibrerat, på Wayland och på X11. *Rätt utfall:* 100 mm (± 1 mm) vid
  alla tre och "mm: uppmätt"; kartan visar "1:4000" utan ≈. Under en
  fraktionell kompositör UTAN fractional-scale antas bufferten lika tät som
  panelen (planens skrivna risk) — mäter linjen fel just vid 125 % är det
  den, inte en ny bugg. Dra eller stega till exakt 100 mm, **Spara** ("mm:
  kalibrerad", kartans mm-innehåll byter storlek direkt), **Återställ**.
- [ ] **Två skärmar** (X11 mäter om vid `ConfigureNotify`, Wayland vid
  `wl_surface.enter/leave`; in- och urkoppling → `RRScreenChangeNotify`
  respektive `wl_output.done`, allt → `WindowDisplayChanged`). *Vad man
  gör:* flytta galleriet mellan skärmarna och koppla ur den ena. *Rätt
  utfall:* linjalens rad visar den nya skärmens id och täthet, linjen mäter
  100 mm där också och kartans skaltal följer; ingen krasch. En sparad
  kalibrering följer sin skärms id.
- [ ] **Skalstocken mäter där den står** — som Windows-raden, på en mätt
  skärm: bandets längd i mm × N ÷ 1000 = etikettens meter, platt, vriden
  och lutad (lutad krymper N och bandet växer). I WSLg är skärmen antagen,
  så där står "≈1:N" och räkningen stämmer bara mot 96 dpi.
- [ ] **Pekarna `Crosshair` och `Move` i GNOME (Adwaita) och KDE (Breeze),
  Wayland och X11** (`cursorNamesFor` i `wayland_display.cpp`,
  `shapeFontCursor` i `x11_display.cpp`). WSLg har inget cursortema: där
  finns `all-scroll` i libwayland-cursors reservtema (uppmätt — `Move` får
  den), men inget kors, så `Crosshair` står kvar som förbudets; X11:s
  kärnfont (`XC_crosshair`/`XC_fleur`) är inte sedd. *Rätt utfall:*
  hårkorset över kartan och grafen och fyrpilen under en panorering, i
  temats storlek; på Wayland ska `crosshair` och `move` (eller `all-scroll`)
  träffa i båda temana.
- [ ] **Hjulets position och fart.** Wayland: `axis` tar positionen ur den
  SENASTE `wl_pointer.motion` (`pointerX_`) och raderna ur `value120`/15;
  X11: knapp 4–7 bär sin egen position. *Rätt utfall:* zoomen i kartan och
  grafen sker mot pekaren i båda, och ett högupplöst hjul (value120) ger en
  mjuk zoom i bråkdelar, utan hack. (Styrplattan PANORERAR sedan TODO 53 —
  se nästa avsnitt.)
- [ ] **Mittendrag och Ctrl+drag på GNOME och KDE.** *Rätt utfall:* som i
  Windows-raden — panoreringen håller punkten under pekaren, också utanför
  fönstret; Ctrl+drag vrider och lutar kartan. Kontrollera att kompositören
  inte äter Ctrl+drag (KDE: Meta/Alt+drag flyttar fönster, Ctrl ska gå
  igenom).
- [ ] **Flikens övriga beteende med riktig mus** — som Windows-raden: klick
  på en form ger träffen i statusraden, Skift+drag i grafen zoomar till
  rutan, dubbelklick visar allt, trögheten stannar och CPU:n går till noll
  när kartan står still (`top`).

## Styrplattan: tvåfingersdrag, nypning och vridning (TODO 53, 2026-10-10)

**Vad som ändrats:** backenden levererar fasadens precisa hjul och
`EventType::Gesture` (kontraktet vid `kPrecisePxPerRow` i `gfx/events.h`).
**Wayland** (`wayland_display.cpp`): ramens `axis_source` finger/continuous
blir `MouseWheel` med `precise` och Begin/Update/End (End vid `axis_stop`),
och `zwp_pointer_gestures_v1` (committad i `protocols/`) ger nypningen.
**X11** (`x11_display.cpp`/`x11_window.cpp`): pekarens rörelse och knappar
går sedan TODO 53 via **XInput 2** (nytt beroende `xi`); en styrplatta
rullar precist ur sina skrollvalutorer och XI 2.4 ger nypningen. Ingen
riktig styrplatta har rört koden: WSLg:s Weston saknar pointer-gestures
och RDP levererar styrplattan som hjul, och under XWayland kan XTest inte
fejka skrollvalutorer eller gester. Det som ÄR kört: Wayland- och
X11-röktest, ctest på båda, och en XTest-harness mot X11-pekaren (klick,
Ctrl/Shift-klick, mitten- och högerknapp, hjul 4–7, drag ut ur fönstret,
`releaseMouseCapture`, captionens intercept, ändrat enhetsträd) — grön.

- [ ] **Tvåfingersdrag på GNOME och KDE (Wayland).** *Vad man gör:* `demo
  --gallery` → "Rityta", dra två fingrar över kartan, platt och vriden
  (Ctrl+drag); rulla sedan en lista och "Anteckningar" med två fingrar.
  *Rätt utfall:* kartan följer fingrarna exakt — punkten under fingrarna
  stannar under dem, åt alla håll, också vriden och lutad — och slutar när
  fingrarna lyfts (ingen egen tröghet på Linux, planens §6); listorna och
  panelerna rullar så att innehållet följer fingrarna 1:1
  (`Widget::wheelScrollPx`). Mushjulet zoomar fortfarande. Med Inställningar →
  "Naturlig rullning" av och på ska kartan följa fingrarna i båda lägena på
  det sätt kompositören visar dem. Med textskalning 125 % (GNOME
  Hjälpmedel → Stor text) ska panoreringen fortfarande följa fingrarna
  exakt.
- [ ] **Nypning och vridning på GNOME och KDE (Wayland).** *Vad man gör:*
  nyp och vrid med två fingrar över kartan, med pekaren på en gatukorsning.
  *Rätt utfall:* zoom och vridning kring PEKAREN (korsningen står still),
  medurs fingrar = medurs karta, nordsnäpp ±7° när fingrarna lyfts; tre och
  fyra fingrar gör ingenting i appen. `WAYLAND_DEBUG=1` ska visa
  `zwp_pointer_gesture_pinch_v1.begin/update/end`; saknar kompositören
  globalen står raden "zwp_pointer_gestures_v1 saknas" i stderr och gesterna
  uteblir (som i WSLg).
- [ ] **Styrplattan i en riktig Xorg-session** (xf86-input-libinput; Xorg ≥
  21.1 och drivrutin ≥ 1.2 för nypningen). *Vad man gör:* samma två rader
  som ovan med `GLAZE_LINUX_DRIVER=x11` — `xinput list-props <styrplattan>`
  ska visa "libinput Tapping Enabled". *Rätt utfall:* tvåfingersdraget
  panorerar kartan (precist, styrplattans px ÷ skalan), listorna rullar,
  styrplattans emulerade knapp 4–7 ger INTE dubbel rullning, mushjulet
  zoomar med ett hack per klick som förut, och nypning/vridning fungerar som
  på Wayland. Första lilla rörelsen efter att pekaren gått in i fönstret
  sätter bara baslinjen (ska inte märkas). *Känd gräns, verifiera inte
  bort:* en styrplatta under den gamla synaptics-drivrutinen känns inte
  igen som styrplatta och zoomar som ett hjul; under XWayland är
  styrplattan och musen EN enhet, så där zoomar tvåfingersdraget också.
- [ ] **X11-pekaren efter ombyggnaden, i en riktig Xorg-session med en annan
  WM än Mutter.** *Vad man gör:* demon och GlazeBuilder — klick,
  dubbelklick, drag, mittendrag i kartan, Ctrl-/Shift-klick i listan (grupp
  14), högerklick (snabbmeny), en dropdown som öppnas MITT I ett tryck,
  drag i namnlisten och i fönsterkanterna, pekarformerna över skiktet och
  kanterna, dra och släpp (grupp 12). *Rätt utfall:* allt som före TODO 53;
  särskilt att namnlistens drag startar (WM:en måste få pekaren — vi
  släpper både kärnans och XI2:s grab) och att en dropdown öppnad under
  ett tryck får musen.

## Ritytans 3D — demofliken på riktig GPU (CANVAS_3D etapp 14, 2026-10-11)

Grindat i WSL (llvmpipe) under Wayland och X11: `scene3d_pixel_gate` del E och
F och `canvas_perf3d`; `canvas_perf3d --full` är kört i WSL med
`GALLIUM_DRIVER=d3d12` på RTX 4070 (etapp 13 — tiderna i
`Glaze/widgets/src/canvas/CLAUDE.md`). Ingen riktig Mesa-GPU har kört det.
Kvar:

- [ ] **Demofliken "3D" i GNOME och KDE (Wayland) och i en riktig
  Xorg-session** (`apps/demo/src/gallery_3d.cpp`). *Vad man gör:* `demo
  --gallery` → "3D" på en riktig GPU (Mesa, inte llvmpipe), ljust och mörkt.
  *Rätt utfall:* som Windows-raden — stolparna, linorna, stationen, skogen
  och kullen syns mot båda ytorna, etiketterna står överst och MSAA ger mjuka
  kanter; `canvas_perf3d` grön och tiderna rimliga mot llvmpipe:s (stolparna
  ~32 ms, 600 000 punkter ~140 ms per frame på llvmpipe).

## Ritytans 3D-interaktion med riktig mus, styrplatta och tangentbord (CANVAS_3D etapp 11, 2026-10-11)

Grindat i WSL under Wayland och X11: `canvas3d_gate` (G12 — orbiten,
mittenknappen till appen, `onGesture`, tickningen genom pumpen och textfältet
på ritytan, med SYNTETISKA händelser) och `trackpad_gate` del O (styrplattan
i orbitläge). Kvar är att kompositören och X-servern levererar händelserna.
Provas i GNOME och KDE (Wayland) och i en riktig Xorg-session
(`GLAZE_LINUX_DRIVER=x11`), i **`demo --gallery` → "3D"** och i
**`./build/tests/canvas3d_gate --hand`** (kullen, lådorna, två textfält på
ritytan, verktygsknappen "Förstaperson" och tickräknaren uppe till höger).

- [ ] **Orbiten med riktig mus och styrplatta.** *Rätt utfall:* som
  Windows-raderna — vänsterdraget kretsar och elevationen stannar vid ±85°,
  mittendraget och Skift+draget panorerar med punkten under pekaren fast,
  hjulet går mot målet (i `--hand` mellan 5 och 600 m), tvåfingersdraget
  panorerar, nypningen går närmare och vridningen svänger kring målet; inget
  glider efter släppet och `top` visar noll när ytan står still.
  Kontrollera att KDE inte äter Ctrl+drag.
- [ ] **Ctrl+tvåfingersdrag kretsar — också direkt efter Ctrl-trycket.**
  *Vad som ändrats (spår F, fyndet ur etapp 11):* hjulet och nypningen BÄR
  nu modifierarna — X11 XI2:s `mods.effective` på skrollvalutorernas hjul
  och på nypningen (`x11_display.cpp` `xiScroll`/`xiPinch`, knapp 4–7
  knappens mask), Wayland `mods_` på hjulen och nypningen
  (`wayland_display.cpp`) — och `AppWindow` läser masken ur dem. Dessutom
  bär en modifierartangents eget event läget EFTER sig (`xkbModsAfterKey`
  i `xkb_keys.cpp`, X11 och Wayland), och Wayland nollar masken vid
  `wl_keyboard.leave`. Grindat med syntetiska händelser (`trackpad_gate` del
  O) och hjälparen mot riktiga keymaps (del B) — inte med riktig platta.
  *Vad man gör:* i `canvas3d_gate --hand`, under Wayland och X11: (1) tryck
  Ctrl, lägg två fingrar på plattan UTAN att röra pekaren först, och dra;
  (2) släpp Ctrl och dra igen; (3) Wayland: håll Ctrl och klicka i ett
  ANNAT fönster, släpp Ctrl där, för pekaren tillbaka över ytan utan att
  klicka och tvåfingersdra. *Rätt utfall:* (1) kameran kretsar som vid ett
  vänsterdrag från första rörelsen; (2) draget panorerar — inget Ctrl hänger
  kvar efter släppet; (3) draget panorerar (ett ofokuserat fönster ser inga
  modifierare). Kretsar (2) eller (3), eller panorerar (1) ⇒ 🐛 med
  drivrutinen (`GLAZE_LINUX_DRIVER`) i raden.
- [ ] **Förstapersonen med WASD** (`--hand`). *Vad man gör och rätt
  utfall:* som Windows-raden — jämn gångfart längs kullen, stopp direkt vid
  släppet (tickräknaren står still, `top` noll), W+D snett, och byte till
  ett annat fönster med W nere stoppar gången. På Wayland är
  tangentupprepningen KLIENTENS (timerfd) — den får inte göra gången
  snabbare eller ryckig; ett fönsterbyte ger `wl_keyboard.leave` och ska
  stoppa den lika säkert som X11:s `FocusOut`.
- [ ] **Textfälten på ritytan** (`--hand`). *Rätt utfall:* som Windows-raden
  — fokusringen och caret i fältet, tecknen (också åäö och compose/döda
  tangenter) i fältet och inte till kameran, CPU:n nära noll medan caret
  blinkar, och det nedre fältet avskuret av ytans kant och rundade hörn.

## Porteringsprovningen i demon — med riktig mus och tangentbord (CANVAS_3D etapp 14, 2026-10-11)

Prövat i WSL (llvmpipe) under Wayland och X11 genom fönstrets ingång
(syntetiska händelser i ett dolt galleri): kuben, rutnätet efter en orbit,
miniatyren, kantläget, förstapersonens gång, vridning och musblick,
markföljningen och punktplockningen — samma kameror och statusrader som på
D3D11. Kvar är händerna, en riktig GPU och kompositören. Provas i GNOME och
KDE (Wayland) och i en riktig Xorg-session (`GLAZE_LINUX_DRIVER=x11`), i
**`demo --gallery`** → **"3D-verktyg"** och **"3D"**.

- [ ] **Rutnätet, kuben, kantläget och miniatyren** ("3D-verktyg"). *Rätt
  utfall:* som Windows-raderna — rutnätet följer orbit och hjul med mjukt
  tonade nivåer och följer kubens glidning (250 ms, sedan noll i `top`),
  kubens etiketter bara på sidorna mot kameran, de synliga kanterna mörka
  utan flimmer mot ytorna (`glPolygonOffset(1, 1)` på fyllningen) och de
  dolda ljusa och streckade, och miniatyren snett ovanifrån utan rutnät.
- [ ] **Förstapersonen och punktplockningen** ("3D"). *Rätt utfall:* som
  Windows-raderna — 2 m över marken, jämn promenadfart som följer terrängen
  och kullen, stopp direkt vid släppet (`top` noll), Q/E och musblicken;
  nära kullen ger ett klick en vit ring och höjden över mark i statusraden.
  På Wayland är tangentupprepningen KLIENTENS (timerfd) — den får inte göra
  gången snabbare eller ryckig — och ett fönsterbyte (`wl_keyboard.leave`)
  stoppar gången lika säkert som X11:s `FocusOut`.

## Programikonen och programmenyn (TODO 65, PROGRAMIKON, 2026-10-11)

- [ ] **X11: `_NET_WM_ICON` + `WM_CLASS`** (`x11_window.cpp`). *Test:*
  `GLAZE_LINUX_DRIVER=x11 ./build/apps/demo/demo`, sedan
  `xprop WM_CLASS _NET_WM_ICON | head -c 300` på fönstret. *Rätt utfall:*
  `WM_CLASS = "demo", "demo"` (app_id = programfilens namn) och en ikon i
  aktivitetsfältet/Alt+Tab; "Nytt fönster" (`setNoIcon`) har ingen
  `_NET_WM_ICON`.
- [ ] **Wayland: `xdg-toplevel-icon-v1`** (`wayland_window.cpp`, scanner-utdata i
  `protocols/`). *Test:* demon på KWin (Plasma 6.x) — fönstret får G-ikonen i
  aktivitetsfältet. På GNOME (protokollet saknas): en loggrad en gång och
  `iconApplied() == false`; med `cmake --install` (demons `demo.desktop` +
  hicolor-PNG:erna) visar GNOME G-ikonen via app_id `demo`.
  *Rätt utfall:* inget protokollfel, ingen krasch utan globalen.
- [ ] **Namnlistens ikon och programmenyn** — startfönstret visar G-ikonen STOR
  till vänster (namnlist + menyrad), klick öppnar programmenyn under ikonen
  (popup-kontraktet, ingen skärmkoordinat), drag på ikonen flyttar INTE
  fönstret. Galleriet: liten kugghjulsikon; "Nytt fönster": ingen.

## Systemfältet — en kommenterad dummy, och dold start (TODO 69, SYSTEMFÄLTET, 2026-10-12)

**Minneslapp, ingen plan:** systemfältet finns inte på Linux än (Erik
2026-10-12: Mac/Windows only, för nu) — `backend_linux/linux_tray.cpp` är en
kommenterad dummy med vägen dit i filhuvudet. Koden är skriven på Windows och
aldrig kompilerad eller körd här.

- [ ] **Bygget och dummyn.** *Test:* bygg (0 varningar), kör `tray_gate` och
  `demo --tray` på Wayland och X11. *Rätt utfall:* `tray_gate` grön — D5 och
  D6 (dispatchen till en SYNLIG ikon) körs bara med den här backenden;
  `TrayIcon::isSupported()` är false, så demons fönster säger att
  systemfältet inte finns, kryssrutorna och dölj-knappen är avstängda,
  fönstret har kvar sin post i aktivitetsfältet och krysset avslutar; stderr
  har EN rad från `TrayIcon::show()` ("plattformen har inget systemfält").
  Proxyns rad ("showInTaskbar = false är inte byggt på Linux", en gång per
  process) kommer bara från ett fönster med `setShowInTaskbar(false)` — demon
  sätter den inte utan systemfält.
- [ ] **Dold start** (`desc.hidden` → `setVisible(true)`, oprövat här). *Test:*
  `tray_gate` H1 på Wayland och X11 (ett fönster med `hide()` före `run()`,
  sedan `show()` — det visar ett riktigt fönster ett ögonblick). *Rätt
  utfall:* fönstret öppnas dolt och `show()` visar det med rätt storlek och
  innehåll; på Wayland inget protokollfel (`WAYLAND_DEBUG=1`) —
  configure-handskakningen ska hinna före den första bufferten.

## Textfälten — lösenordets mask och urklippet (TODO 76, TEXTFALTEN, 2026-10-12)

Koden är plattformsneutral (`Glaze/widgets/src/widgets/textinput.cpp`,
`../text_field.cpp`) och skriven på Windows; det som är Linux är fonten och
urklippet.

- [ ] **Masken "•" ritas** (`Style::passwordMask`, U+2022 — Windows-1252
  0x95, så den ska finnas i Noto Sans/DejaVu fast UI-fonten saknar
  reservkedja, TODO 51). *Vad man gör:* `demo --gallery` → "Text och
  listor", grupp 29, på Wayland och X11; skriv åäö i lösenordsfältet och
  klicka mellan prickarna. *Rätt utfall:* en prick per tecken (inget "�"),
  careten står efter rätt prick och ett klick mellan två prickar sätter den
  där; "Visa lösenordet" visar texten med careten på samma tecken.
- [ ] **Ctrl+C och Ctrl+X i lösenordsfältet lämnar urklippet orört**
  (textfield_gate W2 hoppar över blocket på Wayland — ingen input-serial i
  ett dolt fönster). *Vad man gör:* kopiera något annat till urklippet,
  markera allt i lösenordsfältet (Ctrl+A) och tryck Ctrl+C och Ctrl+X,
  klistra sedan in i ett vanligt fält. *Rätt utfall:* det andra kommer
  tillbaka, inte lösenordet, och lösenordsfältets text står kvar.

## Ritytans text och bild — glyfreserven, textdelarna och bilden på fyra hörn (TODO 74 + 🐛 51, CANVAS_TEXT_OCH_BILD, 2026-10-12)

Skrivet på Windows. Linux-delen är BLIND: fontconfig-/FreeType-reserven
(`backend_linux/linux_text.cpp`) och Image-programmets projektiva vikt
(`linux_image.cpp` — attributet `aQ` på plats 2; GLSL:en är kontrollerad med
glslangValidator men aldrig körd).

- [ ] **Bygget och grindarna på Wayland OCH X11.** *Test:* bygg (0
  varningar) och kör `scene_pixel_gate`, `geom_gate`, `painter_gate`,
  `canvas_gate` och `preview_gate` med båda drivrutinerna
  (`GLAZE_LINUX_DRIVER=x11` för X11). *Rätt utfall:* gröna, och i
  `scene_pixel_gate` särskilt **del R** (R1/R2 för U+2212 och U+2248 — Noto
  Sans saknar dem, så reserven MÅSTE köras; U+4E2D prövas inte på Linux),
  **B8–B11** (bilden på fyra hörn: strecken på homografins plats ±1 px rakt
  uppifrån och ±1,5 px under bäring 30° + tilt 45°; rektangeln och dess fyra
  hörn pixelidentiska; en RenderTexture — speglad på GL — och en Bitmap
  rättvända) och **D8–D9** (en etikett i delar är EN chunk, färgerna på var
  sin sida om delgränsen, och den feta delens gloria täcker aldrig den
  vanliga delens fyllning). Ett grönt R1 på en maskin vars UI-font råkar ha
  − och ≈ bevisar ingenting om reserven — kontrollera med `fc-match
  sans-serif` att det är Noto Sans.
- [ ] **Demons skalstock visar ≈** (stänger 🐛 51). *Test:* `demo
  --gallery` → "Rityta", på Wayland och X11, under WSLg OCH på en riktig
  Debian/Noto-maskin. Stocken skriver "≈1:N" när skärmens mm är ANTAGNA
  (`mapScaleIsExact() == false` — så under WSLg); en skärm med EDID ger
  "1:N" utan ≈, och där är `scene_pixel_gate` R1/R2 provet. *Rätt utfall:*
  ett riktigt ≈ och inget "�"; linjalprovets stegknappar har kvar sitt
  ASCII-minus (bytet till U+2212 är en egen ändring utanför planen).
- [ ] **Färgfonter väljs bort.** *Test:* skriv en emoji (U+1F600) i en
  ritytans etikett på en maskin med Noto Color Emoji. *Rätt utfall:* ingen
  krasch och inget färgat rutnät — ersättningstecknet eller en enfärgad
  emojifont om en sådan finns; `FC_COLOR`-kandidaterna hoppas över.
- [ ] **Bilden på fyra hörn och adressetiketten i demon.** *Test:* `demo
  --gallery` → "Rityta"; zooma in på parken. *Rätt utfall:* den sneda
  översikten (rutmönstret med opacitet 0,6) har RAKA rutor också längs
  fyrhörningens diagonal, under tilt och vridning; "Storgatan 12" har ett
  fetstilt nummer i vägkantens färg med samma gloria som resten.

## Fönster och timer — Wayland OCH X11, BLINT skrivet (TODO 78, FONSTER_OCH_TIMER, 2026-10-12)

- [ ] **Appens timer på båda och headless** (`linux_timer.cpp` — `TimerSource`
  med timerfd i den gemensamma epollen; `timerfd_settime`s och
  `addPollSource`s fel ger `false`, Codex-fynd 8). *Vad man gör:* kör
  `timer_gate` (Wayland, X11 och utan display), `demo --window-lab` och `demo
  --splash`. *Rätt utfall:* timer_gate grön eller 77 utan display; labbets
  geometrirad uppdateras; splashen stängs efter 1,5 s. Också: fönstrets
  egna timrar (caret-blinket, tooltipen) fungerar som förut — de går genom
  samma `TimerSource`.
- [ ] **X11: positionen återställs exakt med en Native-ram** (`writeNormalHints`
  — EN skrivare av `WM_NORMAL_HINTS`: `PWinGravity = StaticGravity` alltid,
  `USPosition|PPosition` när en plats är begärd; normalpositionen ur
  `XTranslateCoordinates` i `onConfigure` bakom samma övergångsvakt som
  storleken, Codex-fynd 4). *Vad man gör:* labbet med en ram från
  fönsterhanteraren (KWin, Mutter/X11, xfwm4): spara, öppna nytt med den
  sparade; flytta och maximera däremellan. *Rätt utfall:* det nya fönstret
  hamnar exakt över det gamla; min/max-storlekarna (`setMinimumSize`) gäller
  fortfarande (samma struktur skrivs).
- [ ] **X11: ingen positionsspårning före den första mappningen** (2026-10-13 —
  `onConfigure` kör `trackNormalPosition` bara när `everMapped_`; före dess är
  `placement()` BEGÄRAN). Grön i WSLg, vars XWM (Weston) aldrig flyttar ett
  omappat fönster och svarar med en syntetisk configure — den skrev förr
  tillbaka den gamla platsen. `geometry_gate` KL använder därför ett mappat
  fönster, och L1/L1b/L3/KL läser platsen efter en tick (`kSvarMs`). *Vad man
  gör:* `GLAZE_LINUX_DRIVER=x11 geometry_gate -v` under en riktig WM (Mutter/X11,
  KWin, xfwm4), och labbets "Öppna nytt med den sparade" efter en dold start
  med `setLocation`. *Rätt utfall:* W2, KL, L1, L1b och L3 gröna; det dolt
  startade fönstret visas på den begärda platsen.
- [ ] **X11: lägena** (`_NET_WM_STATE_HIDDEN` i `readNetWmState` → `minimized_`;
  maximerad start via `_NET_WM_STATE_MAXIMIZED_VERT|HORZ` på det omappade
  fönstret; `IconicState` i `WM_HINTS.initial_state`). *Vad man gör:*
  labbets knappar, och en app med `setWindowState(Maximized)` respektive
  `Minimized` före `run()`; en maximerad start → stäng → öppna igen. *Rätt
  utfall:* geometriraden säger Minimerat/Maximerat; maximerad och minimerad
  start fungerar; återöppningen har normalrutan (Codex-fynd 5).
- [ ] **X11: `setTopmost(true)` före `show()`** (`requestedTopmost_` sätts nu i
  `init` — en rättning som hittades i passet). *Vad man gör:* ett fönster med
  `setTopmost(true)` i konstruktorn. *Rätt utfall:* det står över andra
  fönster direkt.
- [ ] **X11: `screens()` ∩ arbetsytan** (`X11Display::placementScreens` —
  RandR-crtc:erna snittade med `_NET_WORKAREA`, primär via
  `XRRGetOutputPrimary`, skalan Xft/96 × användarskalan). *Vad man gör:*
  `demo --splash` på en och två skärmar, med en panel. *Rätt utfall:*
  splashen mitt i primärskärmens arbetsyta, inte under panelen. Med flera
  virtuella skrivbord läses det AKTUELLA skrivbordets fyrtal ur
  `_NET_WORKAREA` (`_NET_CURRENT_DESKTOP`, `X11Display::readWorkarea`): byt
  till skrivbord 2 med en annan panel och starta splashen där — den ska stå
  mitt i det skrivbordets arbetsyta.
- [ ] **X11: pekarna** (pixmap-handen ur `cursor_bitmaps.h` skalad med uiScale,
  den tomma 1×1-pekaren för `Hidden`, `XC_watch` för `Wait`). *Vad man gör:*
  labbets rutor och kryssruta; panorera ritytan. *Rätt utfall:* stängd hand
  (svart kontur, vit fyllning), ingen pekare, klockan.
- [ ] **Wayland: lägena och platsen** (`applyStartState`: `set_maximized` FÖRE
  det initiala committet — init eller den dolda startens `setVisible`;
  Minimized loggas en gång; `placement()` med `hasPosition` false;
  `setLocation` loggas av fasaden). *Vad man gör:* labbet och en maximerad
  start, också med `hide()` före `run()` och `show()` senare. *Rätt utfall:*
  maximerad start fungerar i båda vägarna; geometriraden säger "ingen
  position"; "Öppna nytt med den sparade" öppnar där kompositören vill, med
  rätt storlek.
- [ ] **Wayland: pekarna** (`cursorNamesFor`: `grabbing`/`closedhand`/`fleur`,
  `wait`/`watch`, och vaktposten för `Hidden` → `wl_pointer_set_cursor` med
  NULL-yta). *Vad man gör:* labbets rutor och kryssruta. *Rätt utfall:*
  temats stängda hand, väntepekaren, ingen pekare i "Dold" — och pekaren
  kommer tillbaka när musen lämnar rutan.
- [ ] **Väckningen med dold start** (Codex-fynd 12): en `glaze::Timer` som
  startas innan ett DOLT fönster `run()` (`hide()` före) ska väcka pumpen på
  både Wayland och X11. *Vad man gör:* timer_gate T1 (offscreen) och en
  liten app med `hide()` + `run()` + en timer som visar fönstret efter 1 s.
  *Rätt utfall:* fönstret visas efter en sekund.
- [ ] **Proxyn vidarebefordrar `placement()`** (`ProxyWindow::placement` i
  `linux_platform.cpp`). *Vad man gör:* kör `geometry_gate` (W1 mäter
  storleken efter init — utan vidarebefordran hade den varit 0). *Rätt
  utfall:* W1 grön på Wayland och X11.

## Träd, listor och bilder ur pixlar (TODO 75, TRAD_LISTOR_BILDER, 2026-10-12)

Ingen backendkod; en Bitmap går samma väg som en asset (`createImage` +
`drawImage`) och OS-ikonen samma väg som förut (`_NET_WM_ICON`,
`xdg-toplevel-icon-v1`).

- [ ] **Flerval och kryss i trädet, Wayland och X11** (`tree_view.cpp`).
  *Vad man gör:* `demo --gallery` → "Tabeller och träd", grupp 13:
  Ctrl+klicka två noder, Shift+klicka ett spann, Ctrl+A, klicka i en nods
  ruta, och dra tre valda noder in i "Bilder". På Wayland också ett
  Ctrl+klick i galleriet medan ett ANNAT fönster har tangentbordsfokus.
  *Rätt utfall:* Ctrl vänder, Shift spänner över de synliga raderna, rutan
  kryssar utan att flytta valet, de tre noderna flyttar i samma ordning och
  är fortfarande valda. Wayland når modifierarna bara ett fönster med
  tangentbordsfokus: Ctrl+klicket i det ofokuserade galleriet får bli ett
  vanligt klick, men aldrig vända fel nod.
- [ ] **Bilder ur pixlar** (`picture.cpp`, `tool_button.cpp`). *Vad man gör:*
  grupp 5 och grupp 9 ("Paneler och remsor") med skala 1 och 2. *Rätt
  utfall:* ringbilden och ringikonen syns, skarpa på skala 2, och ikonen är
  tonad i den inaktiverade remsan.

## Rullgardinsknappen (TODO 77, DOCKNING E4a, 2026-10-13)

Ingen backendkod; menyn läggs under knappens rect genom popup-kontraktet
(X11: skärmkoordinater ur `popupAnchor*`, Wayland: xdg-positionern med
knappens rect som ankare), samma väg som menyraden och dropdownen tar.

- [ ] **Knappmenyn under knappen, Wayland och X11** (`tool_button.cpp` →
  `Menu::popupAtRect`). *Vad man gör:* `demo --gallery` → "Paneler och
  remsor", grupp 9, tredje remsan: tryck på "Ny ▾" och tryck igen; tryck på
  "Ångra" och sedan på dess pil; välj en post; upprepa med fönstret nära
  skärmens underkant. *Rätt utfall:* menyn öppnas under knappen,
  vänsterjusterad med den (vid underkanten vänd uppåt), det andra trycket
  stänger den, "Ångra" ger "Ångrat: …" utan meny och pilen öppnar
  historiken; de två inaktiverade gör ingenting.

## GlazeUI som delat bibliotek — `build-shared`, BLINT skrivet (TODO 73, DELAT_BIBLIOTEK, 2026-10-13)

Koden: `cmake/GlazeShared.cmake`, `Glaze/gfx/CMakeLists.txt` (common_gfx som
OBJECT, PIC), `Glaze/widgets/CMakeLists.txt`, `Glaze/widgets/src/app_anchor.cpp`
+ `gfx::isInProgramFile` i `linux_platform.cpp` (`dl_iterate_phdr`, ALDRIG
kompilerad), `Threads/src/ui_anchor.cpp`, `tests/dll_gate.cpp` (ELF-vägen:
`readelf -g -W`, ALDRIG körd) och `tests/shared_gate*.cpp`.

- [ ] **`build-shared` bygger och sviten är grön.** *Vad man gör:* `cmake -S . -B
  build-shared -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGLAZE_SHARED=ON
  -DGLAZE_TESTS=ON && cmake --build build-shared && ctest --test-dir
  build-shared --output-on-failure`. *Rätt utfall:* 0 varningar (ingen
  "relocation … recompile with -fPIC" — common_gfx och de statiska
  formatbiblioteken är PIC), ctest grön inklusive `shared_gate` (pluginen
  laddas med `dlopen(RTLD_NOW | RTLD_LOCAL)`). D0 mäter ankaret bara där
  `--as-needed` är förval (Ubuntu); på Debian är den grön också utan.
- [ ] **Byggträdets RUNPATH.** *Vad man gör:* `ldd build-shared/apps/demo/demo`.
  *Rätt utfall:* `libglaze.so` ur `build-shared/Glaze/widgets/`, inget "not
  found"; demon kör på Wayland OCH X11 (`GLAZE_LINUX_DRIVER=x11` väljer X11),
  och avslutas rent.
- [ ] **`dll_gate` i den statiska sviten.** *Vad man gör:* `ctest --test-dir
  build-test -R dll_gate --output-on-failure`. *Rätt utfall:* grön; del A:s
  kontroll hittar exakt räknare()::n, delad, perTråd, Medlem::antal, karta
  och ägd (`.bss.`/`.data.`/`.tbss.` i COMDAT-grupper, åäö avkodade ur
  readelf:s `<C3><A4>`), del B H1 1 · H2 4 · H3 3, och 0 fynd i de riktiga
  arkiven och headrarna.

## Levande drag och flytande dockpaneler — X11 och Wayland, BLINT skrivet (TODO 77/81, DOCKNING E3 + DOCKNING_DRAG E5, 2026-10-13)

Ingen ny backendkod i E5. På X11 flyttas det burna fönstret med
`XMoveWindow` vid varje rörelse (en server-rundresa per rörelse för
`clientToScreen`), och punkten räknas ur händelsens koordinater mot källans
läge (L4) — en MotionNotify i flykt över en egen flytt ger ett stegs
översläng som nästa händelse rättar. På Wayland finns ingen skärmrymd
(`gfx::screens()` är tom), så `DockHost::isFloatDragSupported()` svarar
false: inget levande drag — draget visar platshållaren medan objektet står
kvar — och namnlisten syns som flytthandtag. Koden: `Glaze/widgets/src/dock/
dock_float.cpp` och `dock_host.cpp`.

- [ ] **dock_gate grön på Wayland OCH X11** (`GLAZE_LINUX_DRIVER=x11` för X11).
  *Rätt utfall:* grön; på Wayland körs LV5 (den gamla vägen) och LV1–LV12
  (de som inte är Windows-endast) hoppas över, på X11 tvärtom. Fall B håller
  det burna objektets explicita capture i dess eget fönster (Codex-
  granskningen av E5) — `takePointerCapture` svarar alltid false här.
- [ ] **X11: levande drag.** *Vad man gör:* `demo --dock`; dra Utforskarens
  rubrik en bit, sedan ut ur fönstret, och släpp; dra det flytande fönstrets
  rubrik in över Sök; prova med en kompositerande fönsterhanterare (KWin,
  Mutter/X11). *Rätt utfall:* panelen lossnar och fönstret följer handen
  UTAN darr (se översläng ovan) — fönsterhanteraren får inte flytta det —
  platshållaren visas över Sök och släppet där gör panelen till en flik;
  släppt utanför flyter den där den står. Ett nytt flytande panelfönster tar
  inte fokus (`_NET_WM_USER_TIME` 0).
- [ ] **X11: verktygsfältet.** *Vad man gör:* dra Standards grepp ut; klicka
  på en knapp i ett flytande fält med fokus i Sök-fältet. *Rätt utfall:*
  fältet följer pekaren och dockas där platshållaren står; fältets fönster
  tar aldrig fokus (`InputHint false`, utility-typ) — caret kvar i Sök.
- [ ] **X11: ägda fönster.** *Vad man gör:* minimera huvudfönstret, Ctrl+S i
  det flytande fönstret. *Rätt utfall:* det flytande fönstret ligger ovanför
  huvudfönstret och följer det; Ctrl+S når huvudfönstrets meny.
- [ ] **Wayland: draget lossar inget.** *Vad man gör:* dra en panels rubrik
  ut ur fönstret och släpp; dra den sedan in över Sök och släpp. *Rätt
  utfall:* panelen står kvar under draget och ett släpp utanför gör
  ingenting; över Sök visas platshållaren, och släppet där gör panelen till
  en flik.
- [ ] **Wayland: menyn "Flytande".** *Vad man gör:* ▾ i Utforskarens rubrik ▸
  Flytande. *Rätt utfall:* ett eget fönster med SYNLIG namnlist som går att
  flytta (`xdg_toplevel_move`); kompositören placerar det.
- [ ] **Wayland: dubbelklick på rubriken dockar.** *Rätt utfall:* panelen
  tillbaka vid sin kant, fönstret borta.
- [ ] **Wayland: platsen loggas en gång, storleken återställs.** *Vad man
  gör:* gör en panel flytande, ändra fönstrets storlek, Layout ▸ Spara, docka
  den och välj Layout ▸ Återställ den sparade; återställ sedan en sträng MED
  en plats (sparad på X11, eller en `setFloatingBounds` i koden) två gånger.
  *Rätt utfall:* layouten skriver `f=…,-,-,b,h,…` och fönstret får sin
  storlek tillbaka; strängen med en plats ger EN loggrad om att platsen inte
  tillämpas (kompositören placerar), inte en per återställning.

## Storleksmarginalen utanför ytan — Wayland OCH X11, BLINT skrivet (TODO 81, DOCKNING_DRAG E2L, 2026-10-13)

Eriks VAL 1 (b): ramen ligger UTANFÖR den synliga kanten också här, som en
GENOMSKINLIG marginal på 8 logiska px runt ytan (libdecor/GTK:s form). Koden:
`../frame_margin.h` (kantvalet, grindat headless i geometry_gate KM),
`linux_platform.cpp` (`LinuxWindow::hitRoleAt` — marginalens kant först,
kartans zoner innanför hoppas över), `linux_renderer.cpp`/`linux_target.cpp`
(compositen rensar ytan och förskjuter viewporten och scissorn med
marginalen), `wayland_window.cpp` (`applySize`: ytan = klient + 2m,
`xdg_surface_set_window_geometry(m, m, b, h)`, opak region = klientytan,
ingen marginal maximerat/helskärm/tiled), `wayland_display.cpp` (pekarens
positioner −m genom `clientX/Y`), `x11_window.cpp` (X-fönstret = klient + 2m,
`_GTK_FRAME_EXTENTS`, hintsen + 2m, `setPosition`/`clientToScreen`/
`trackNormalPosition` ±m, av vid maximering; ett fönster som startar fast
får ingen och ett som blir fast i drift behåller den INERT — Codex-fynd, se
raden nedan) och
`x11_display.cpp` (`frameExtentsUsable`: ARGB + `_NET_WM_CM_S<n>` +
`_GTK_FRAME_EXTENTS` i `_NET_SUPPORTED`; varje pekarväg −m). Ingenting av
det här har kompilerats eller körts.

- [ ] **Bygget och grindarna.** *Vad man gör:* bygg med `-DGLAZE_TESTS=ON`
  och kör `ctest`. *Rätt utfall:* 0 varningar; geometry_gate grön (KM och,
  på X11, KL) och inga andra fall röda — fönstrens klientytor och
  placeringar är oförändrade genom fasaden.
- [ ] **Wayland (GNOME, KDE, WSLg): storleksändring utanför kanten.** *Vad
  man gör:* `demo`; för pekaren utifrån mot varje kant och hörn och dra.
  *Rätt utfall:* resize-pilen kommer i marginalen UTANFÖR den synliga kanten
  (hörnens pilar en bit in längs kanterna); kompositören storleksändrar;
  ingen synlig ram, skugga eller färgad remsa runt fönstret (marginalen är
  helt genomskinlig och klicken där landar inte i fönstret bakom).
- [ ] **Verktygsfältet vid kanten är klickbart ända ut — buggen.** *Vad man
  gör:* `demo --dock` på Wayland OCH på X11 med kompositör; klicka på det
  stående verktygsfältets knappar ända ut mot vänsterkanten. *Rätt utfall:*
  knappen trycks ända ut, ingen resize.
- [ ] **Pekaren och innehållet står rätt.** *Vad man gör:* hovra och klicka
  på små mål nära alla fyra kanter (knappar, textfältets caret, en
  dropdown); öppna dropdowns och menyer. *Rätt utfall:* träffen landar under
  pekaren (inte 8 px snett), popuperna öppnas exakt under sitt ankare, och
  tooltipen står vid pekaren. Med 125 % (fractional-scale) likadant, och
  ingen suddig eller förskjuten bild.
- [ ] **Maximera, tiled och återställ.** *Vad man gör:* maximera och
  återställ `demo` några gånger (knappen, dubbelklick, Super+Upp); halvera
  den mot en skärmkant (GNOME-tiling); på X11 också med två skärmar.
  *Rätt utfall:* maximerat fyller fönstret arbetsytan utan glapp och utan en
  osynlig remsa som stjäl klick på grannskärmen eller panelen; återställt
  har det samma storlek och plats som förut — det KRYMPER eller VÄXER inte
  med marginalen för varje varv; tiled (Wayland) har ingen marginal och
  kartans zoner innanför kanten gäller.
- [ ] **X11 utan kompositör.** *Vad man gör:* en Xorg-session utan
  kompositör (t.ex. Openbox utan picom), eller med en WM utan
  `_GTK_FRAME_EXTENTS` i `_NET_SUPPORTED`. *Rätt utfall:* ingen svart ram —
  fönstret får ingen marginal och storleksändras från zonerna innanför
  kanten som före E2L. Startar kompositören efter appen gäller det redan
  öppna fönstret som förut (skriven gräns — beslutet tas i init).
- [ ] **X11 med kompositör (mutter/KWin/xfwm4): placering och snapp.** *Vad
  man gör:* spara och återställ fönstrets plats (`demo --window-lab` eller
  Layout ▸ Spara/Återställ i `--dock`); snappa fönstret mot en skärmkant.
  *Rätt utfall:* en sparad plats återställs exakt (klientens hörn); snappet
  lägger den SYNLIGA kanten mot skärmkanten, inte marginalen.
- [ ] **Fast fönster och låsning i drift.** *Vad man gör:* öppna en
  meddelanderuta; starta `demo --tray` (låser storleken i `onLoad`). *Rätt
  utfall:* ingen resize-pil, varken i marginalen eller innanför kanten;
  systemfältsfönstret hoppar inte när det låses. Wayland: ingen marginal,
  geometrin byts med nästa bildruta. X11 (Codex-fynd: att flytta X-fönstret
  runt klientytan vid bytet tävlade med WM:ens configure, så geometrin byts
  inte längre): meddelanderutan, som STARTAR fast, har ingen marginal — ett
  klick strax utanför dess kant når fönstret bakom; systemfältsfönstret, som
  BLIR fast i `onLoad`, behåller sin marginal men inert (`frameResizable_`),
  och X-fönstret ändras inte alls. Där äter marginalens 8 px klick (skriven
  gräns — ingen inmatningsform).
- [ ] **X11: snabba byten (Codex-fynd).** *Vad man gör:* på X11 med
  kompositör, maximera och återställ `demo` snabbt flera gånger i rad
  (dubbelklick i namnlisten, Super+Upp/Ned), och i en egen app: växla
  `setMinimumSize`/`setMaximumSize` mellan fast och fri storlek i en timer
  på 50 ms. *Rätt utfall:* fönstret står kvar på samma plats med samma
  klientyta efter varje varv — inget krypande på marginalens bredd, ingen
  svart remsa.
- [ ] **Wayland: skalbyte i drift (Codex-fynd).** *Vad man gör:* byt
  textskalningen (GNOME Tweaks ▸ Fonts ▸ Scaling Factor) och appens
  användarskala (`demo`, skal-dropdownen på startsidan) medan fönstret är
  normalt, och en gång medan det är maximerat. *Rätt utfall:* ingen bildruta med fel storlek
  eller förskjutet innehåll vid bytet; min/max-gränserna följer den nya
  skalan (dra i kanten); maximerat stannar fönstret maximerat.
- [ ] **Rundade hörn och ARGB.** *Vad man gör:* `demo` med ett tema med
  rundade fönster, på Wayland och X11; på X11 också UTAN ARGB-visual om det
  går att ordna. *Rätt utfall:* hörnen rundade och genomskinliga, ramlinjen
  följer dem, marginalen syns inte. Utan ARGB-visual: fönstret degraderar
  till opakt och fyrkantigt och loggar det (`hasArgbConfig` — den gamla
  kontrollen kunde aldrig slå till) — inga svarta hörn.
- [ ] **Dold start med marginal.** *Vad man gör:* `demo --tray --hidden` och
  visa fönstret från systemfältsmenyn — Linux har ingen systemfältsikon, så
  på Linux: ett fönster som `hide()` före `run()` och `show()` senare.
  *Rätt utfall:* fönstret mappas med marginalen och innehållet på rätt
  plats.

## Flytande verktygsfält — X11 och Wayland, BLINT skrivet (TODO 81, DOCKNING_DRAG E4, 2026-10-13)

Ingen ny backendkod: det flytande verktygsfältet bor i samma ägda fönster som
en flytande panel (`Glaze/widgets/src/dock/dock_float.cpp`), men med FAST
storlek som PASSAS om när fältets innehåll ändras — `fitToolBar` lyfter
gränserna (min/max 0) före den nya innehållsytan och låser dem igen, så
`setSizeLimits` går fast → fri → fast (med E2L:s marginal: Wayland av → på →
av; X11 byter aldrig — ett fönster som skapades fast har ingen marginal, ett
som blev fast har den inert).
dock_gate kör FT1–FT4, FT5 (greppets dubbelklick), FT6 (fönstrets
stängning), FT7, FT9, FT11, FT8 och FT12 (FT3:s plats, rubrikklicken och FT10
är Windows-endast).

- [ ] **dock_gate grön på Wayland OCH X11** (`GLAZE_LINUX_DRIVER=x11` för X11),
  avsnittet FLYTFÄLT. *Rätt utfall:* grön; på Wayland skriver `ft=` platsen
  `-`.
- [ ] **Det flytande fältets fönster.** *Vad man gör:* (efter DOCKNING_DRAG
  E6, eller i en egen app) gör ett verktygsfält flytande med ett dubbelklick
  på greppet, lägg till en knapp i det och byt stil. *Rätt utfall:* X11 — ett
  fönster utan namnlist med rubriken (titel, ✕) och fältet liggande, på
  `setFloatingPosition`s plats; Wayland — namnlisten SYNS (flytthandtaget)
  med rubriken under den. Fönstret går inte att storleksändra och växer med
  knappen och stilen utan att hoppa (gränsbytet, och på Wayland marginalen).
  Dubbelklick på rubriken dockar fältet sist i sitt band; ett drag i
  rubriken in över ett band dockar det där på X11 och gör ingenting på
  Wayland.
- [ ] **Passat medan det är dolt, Wayland OCH X11** (Codex-granskningen av
  E4, 2026-10-13 — `Widget::childLayoutChanged`; FT9 mäter bokföringen, inte
  OS-fönstret, och ett dolt Wayland-fönster har ingen yta att konfigurera).
  *Vad man gör:* dölj huvudfönstret (`hide()`) med ett flytande fält, byt en
  knapps text i det till en längre, och visa huvudfönstret. *Rätt utfall:*
  fältets fönster dyker upp DIREKT i den nya bredden — ingen bildruta i den
  gamla, inget hopp.

## Fasaden för levande drag — X11 och Wayland, BLINT skrivet (TODO 81, DOCKNING_DRAG E3, 2026-10-13)

Koden: `x11_window.cpp` (`WindowDesc::showInactive` ⇒ `_NET_WM_USER_TIME` = 0
före första `XMapWindow`, också vid en dold start; egenskapen tas bort vid
en senare `setVisible(true)`, så nästa visning aktiverar), `x11_display.cpp`
(atomen), `wayland_window.cpp` (dokumenterad no-op), `linux_platform.cpp`
(headless no-op; proxyn skickar `desc` orörd och vidarebefordrar
`captureMouse()`, som svarar false i alla tre fönstren — den implicita
grabben ger redan hela trycket till fönstret där det började).
`MouseCaptureLost` skickas inte på Linux. capture_gate kör sitt C-block
(ingångens bokföring) på både Wayland och X11; attributets användare kommer
i DOCKNING_DRAG E5 (flytande paneler).

- [ ] **Bygget och capture_gate på Wayland OCH X11.** *Vad man gör:* bygg med
  `-DGLAZE_TESTS=ON`; `ctest -R capture_gate --output-on-failure`, och igen
  med `GLAZE_LINUX_DRIVER=x11`. *Rätt utfall:* 0 varningar (koden är
  okompilerad på Linux), grön.
- [ ] **X11: ett nytt flytande panelfönster tar inte fokus (efter E5).**
  *Vad man gör:* på X11 (mutter, KWin, xfwm4) `demo --dock`; gör en panel
  flytande (dess meny ▸ Flytande, eller lossa den med musen), med fokus i ett
  textfält i huvudfönstret; dölj panelens fönster och visa det igen.
  *Rätt utfall:* första gången kommer panelfönstret fram UTAN att ta fokus —
  caret blinkar kvar i huvudfönstret; när det visas igen aktiveras det som
  ett vanligt fönster. Wayland: kompositören bestämmer (no-op).

## Ritytans lagerordning, textmåtten och den antagna skärmen — BLINT skrivet (TODO 83, REQ2_RITYTAN, 2026-10-13)

Koden: reserven i `display_util.cpp` (96 dpi i FYSISKA px, ingen skala) och
anropen i `wayland_window.cpp`, `x11_window.cpp` och `linux_platform.cpp`
(headless och proxyn). Skärmens mm mäts INTE längre om vid skalbyte — raden
är borta ur `onPreferredScale`, `onTextScaleChanged`,
`onOutputScaleMaybeChanged` och X11:s `onResourceManagerChanged`, för varken
måtten eller reserven beror på skalan. Mätningen `gfx::measureText`
(`sdf.cpp`) bygger på att FreeTypes `measure` och `rasterGlyph` ger samma
steg (båda genom reservens cache).

- [ ] **display_gate under X11 med Xft.dpi 144.** *Vad man gör:* i WSLg
  `xrdb -merge <<< 'Xft.dpi: 144'`, `GLAZE_LINUX_DRIVER=x11 display_gate -v`.
  *Rätt utfall:* på en antagen skärm `Assumed 3,7795 px/mm` (inte 5,67) och
  F5 grön; på en mätt skärm F5 överhoppad.
- [ ] **canvas_gate M och LO, painter_gate LO och geom_gate S6 på Wayland OCH
  X11.** *Rätt utfall:* gröna — M2 med U+2248 (≈ saknas i Noto Sans, så
  reserven ritar) ger samma mått före och efter ritningen i fyra stilar.
- [ ] **Headless:** glaze-render utan display. *Rätt utfall:* skärmen är
  Assumed med 3,78 px/mm.

## Inmatningen — Waylands leave-regel, popupens släpp och X11 (TODO 82, REQ2_INMATNING, 2026-10-13, BLINT)

Koden: `pushedButtons_`/`pendingCaptureLost_` i
`Glaze/gfx/src/backend_linux/wayland_display.cpp` (pekarens `leave` medan ett
LEVERERAT tryck hålls blir `MouseCaptureLost`, utom när nästa `enter` i samma
dispatch-omgång är en egen popup i samma fokusgrupp), `AppWindow::forwardKeyUp`
(`Glaze/widgets/src/app_window.cpp` — en popup som får ett släpp den inte såg
trycket för lämnar det till ägaren) och kommentaren vid `FocusOut` i
`x11_display.cpp` (X11 ändras inte). Skriven BLINT — inte kompilerad.

- [ ] **Wayland: översikten mitt i ett drag.** *Vad man gör:* `demo --gallery`
  → Interaktion → grupp 30; dra med högerknappen och tryck Super (GNOME:s
  översikt). *Rätt utfall:* "Trycket avbröts" EN gång; tillbaka i fönstret
  är inget tryck kvar.
- [ ] **Wayland: en dropdown öppnad av trycket.** *Vad man gör:* tryck på en
  dropdown, dra in i listan och släpp på en rad (och vanligt klick-klick).
  *Rätt utfall:* som förut — raden väljs och listan stängs; inget falskt
  avbrott (fokusgruppsundantaget). En loggrad "MouseCaptureLost" ska inte
  synas för ägaren.
- [ ] **Wayland: en tangent som hålls när en popup öppnas.** *Vad man gör:*
  fliken 3D → förstapersonen; håll W (förstapersonen går), öppna en dropdown
  med musen medan W hålls, och släpp W med listan öppen. *Rätt utfall:*
  förstapersonen SLUTAR gå (popupens släpp når ägaren) — före ändringen
  tickade ritytan tills nästa fokusförlust.
- [ ] **Wayland: falskt avbrott.** *Vad man gör:* dra med vänsterknappen ut
  ur fönstret och tillbaka i KDE och i GNOME. *Rätt utfall:* draget lever
  (en textmarkering fortsätter). Skickar en kompositör `leave` under tryck
  utan att bryta grabben blir det i stället ett avbrott — skriv det i
  `/TODO.md` med kompositörens namn (wayland#280).
- [ ] **X11: Alt+Tab mitt i ett drag.** *Rätt utfall:* "Trycket avbröts" i
  grupp 30 (`FocusOut` → fokusförlusten).
- [ ] **X11 och Wayland: upprepningen.** *Vad man gör:* håll mellanslag i
  grupp 30s yta. *Rätt utfall:* statusraden står still på "Mellanslag hålls"
  hela tiden (`repeat` är sann från andra KeyDown) — en X-server utan
  detekterbar autorepetition ger i stället omväxlande "släppt"/"hålls"
  (planens kantfall).
- [ ] **Wayland — KÄND GRÄNS (kodgranskningen av 82, fynd 2): en grabbande
  popup lämnar ägarens hållna tangent kvar.** En meny med grab tar
  tangentbordet inom fokusgruppen, och ägaren får ingen fokusförlust. *Vad man
  gör:* fliken 3D → förstapersonen; håll W, öppna en MENY (menyraden eller
  högerklick) medan W hålls, släpp W med menyn öppen. *Känt utfall:*
  förstapersonen kan fortsätta gå tills nästa riktiga släpp eller fokusförlust
  (`heldKeys_`/`keyTargets_` står kvar i ägaren); rättas inte nu. Blir det
  ett verkligt problem: 🐛 i `/TODO.md`.
- [ ] **Wayland — KÄND GRÄNS (kodgranskningen av 82, fynd 4): ett högertryck
  kvar i ägaren efter en egen popup.** Fokusgruppsundantaget stryker
  avbrottet när pekaren går in i vår egen popup, och släppet kommer till
  popupen. *Vad man gör:* grupp 30; högerdra i ytan och låt en popup öppnas
  under trycket (t.ex. en tooltip eller en meny som öppnas av en annan väg),
  släpp över popupen. *Känt utfall:* ägarens `rightPressed()` kan stå kvar
  till nästa tryck med samma knapp eller nästa fokusförlust (linjalen står kvar
  tills dess); rättas inte nu.

## Trädets godkännande och listvyns kryss — X11 och Wayland (TODO 85, REQ2_TRAD_LISTOR, 2026-10-13)

Ingen backendkod — allt bor i widget-lagret och sessionen
(`Glaze/widgets/src/drag_drop.cpp`, `tree_view.cpp`, `list_view.cpp`,
`table.cpp`). Grindarna (dnd_gate 16–18, tree_gate 31–38, listview_gate
K1–K4/F14–F20) körs med sviten på Linux-maskinen; proven nedan på BÅDE
Wayland och X11.

- [ ] **Grupp 13: appens regel och Musik.** *Vad man gör:* `demo --gallery`
  → "Tabeller och träd"; dra en fil (Rapport.pdf, Brev.txt) till trädets
  översta nivå och släpp; dra en nod in i Musik; Ctrl+klicka för att vända
  valet. *Rätt utfall:* överst förbudsmärket och ingen insättningslinje, och
  släppet flyttar inget; i Musik hamnar noden alfabetiskt ("Sorterade in
  alfabetiskt: N"); Ctrl+klick vänder valet.
- [ ] **Grupp 14: listvyns kryss.** *Vad man gör:* klicka i rutorna, tryck
  Space på den aktuella raden, sortera i huvudet. *Rätt utfall:* klicket i
  rutan kryssar utan att flytta valet, Space kryssar den aktuella raden, och
  efter en sortering följer krysset raden.
- [ ] **Inget drag från rutan eller en kolumnavdelare** (listvyn med
  `setAllowDrag` och en `onDragStart` som fyller lasten). *Rätt utfall:*
  inget drag startar; avdelaren ändrar bredden.

## Dialogerna — standard- och avbrytknappen, maxlängden och platshållaren — X11 och Wayland (TODO 84, REQ2_DIALOGER, 2026-10-13)

Ingen backendkod — steget bor i `Glaze/widgets/src/app_window.cpp`,
utseendet i `button.cpp`, fälten i `textinput.cpp`/`text_area.cpp`.
Grindarna (dialog_gate, textfield_gate H6/H7, preview_gate, roundtrip_gate,
pixel_gate) körs med sviten på Linux-maskinen; proven nedan på BÅDE Wayland
och X11, i Noto respektive DejaVu.

- [ ] **Om-dialogen.** *Vad man gör:* `demo` → Arkiv → Nytt fönster →
  "Om…"; Return, öppna igen och numpadens Enter (NumLock på och av), öppna
  igen och Escape; öppna och Tab till Avbryt. *Rätt utfall:* Return och
  numpad-Enter = OK, Escape = Avbryt; accentfyllningen står på OK och
  flyttar till Avbryt med fokus.
- [ ] **Meddelanderutan.** *Vad man gör:* andra fönstret → OKCancel → visa →
  Return och Escape; YesNo → visa → Escape. *Rätt utfall:* OKCancel svarar
  OK respektive Avbryt; i YesNo gör Escape ingenting.
- [ ] **Filväljaren** (`demo --gallery` → grupp 21 → "…"). *Vad man gör:*
  öppna filtypslistan med musen, tryck Escape, och Escape igen. *Rätt
  utfall:* första Escape stänger bara listan, den andra avbryter dialogen —
  på Wayland, där en musöppnad popup tar tangentbordet, är det raden som
  provar att Escape inte når dialogens avbrytknapp förbi listan.
- [ ] **Platshållarna och maxlängden.** *Vad man gör:* grupp 29: töm
  lösenordsfältet; klistra in mer än 16 tecken. Grupp 24: töm fältet med
  brytning. *Rätt utfall:* den dämpade hinten syns i det tomma fältet också
  med fokus; inklistringen kapas till 16 tecken (åäö ett tecken var);
  anteckningens hint bryts mot fältets bredd.

## 16-bitars PNG genom libpng:s fulla API — BLINT skrivet (TODO 66, FAS1_C_PREVIEW, 2026-10-13)

Koden: `avkodaPng`/`läsMedVakt` i `Glaze/gfx/src/backend_linux/linux_image.cpp`
— libpng:s FULLA API i stället för det förenklade (`png_image_*`), med
`png_set_scale_16` (rundning som vår egen kodek), palett och grått utvidgat
till RGB, tRNS till alfa, fyllnadsalfa 255, interlace-passen och INGEN
gammakorrigering; setjmp-vakten bär bara triviala objekt i sin ram.
`loadImage(path)` läser filen hel och går samma väg. Skriven BLINT — inte
kompilerad.

- [ ] **Bygget.** *Vad man gör:* `cmake --build build` på Linux-maskinen.
  *Rätt utfall:* 0 varningar i `linux_image.cpp` (också med `-Wextra`, som
  fångar `-Wclobbered` — vaktens ram ska inte ge någon).
- [ ] **icon_gate A3 på Wayland OCH X11.** *Vad man gör:* `icon_gate -v`.
  *Rätt utfall:* "A3: OS:ets avkodare håller med facit (RGBA 16 bit Adam7)"
  är GRÖN — raden hoppades över före 🐛 66 — liksom de sju andra
  färgtyperna (grått 1/4/16 bitar, RGB + tRNS, palett + kort tRNS, grått +
  alfa, RGBA 8 bitar Adam7).
- [ ] **Resten av bildvägen.** *Vad man gör:* `scene_pixel_gate`,
  `pixel_gate` och `demo --gallery` (ikonerna i verktygsfältet, programikonen,
  ritytans bilder). *Rätt utfall:* gröna och oförändrade — en PNG med en
  sRGB-/gAMA-chunk ritas som under WIC (ingen gammaomräkning), och en trasig
  fil loggar "decodeImage: kunde inte avkoda … (<libpng:s meddelande>)".

## Tre lägen, flerval, radval, alfa, klockslag, dialogerna och statusfälten — X11 och Wayland (TODO 63/54, FAS1_B_WIDGETS, 2026-10-13)

Allt ligger i widget-lagret — inte byggt eller kört på Linux. Windows-raderna i
`doc/os/windows/TESTS.md` gäller också här; det som är Linux eget:

- [ ] **Bygget med GCC/libstdc++.** *Rätt utfall:* 0 varningar; `checkbox_gate`,
  `range_gate`, `statusbar_gate`, `listbox_gate`, `datagrid_gate`,
  `colorpicker_gate`, `datepicker_gate`, `tab_gate` och `dialog_gate` gröna.
- [ ] **Dialogerna på Wayland och X11.** *Vad man gör:* `demo --gallery`, grupp
  34 — färgdialogen och inmatningsrutan. *Rätt utfall:* modala över
  galleriet (Wayland: kompositören placerar dem — `CenterOwner` är en
  begäran), Enter OK och Escape Avbryt, färgytans palett tar tangenterna
  direkt i dialogen (det är inget popupfönster — `setPopupTakesKeys` gäller
  inte här).

## Fönstren — fältet överst och `onLoad` före synligheten — BLINT skrivet (TODO 87, 80, FAS1_A_FONSTER, 2026-10-13)

Koden: `Glaze/gfx/src/backend_linux/x11_window.cpp` (`raise()` på ett
noActivate-fönster är bara `XRaiseWindow`, ingen `_NET_ACTIVE_WINDOW`;
`minimize`/`maximize`/`restore` på ett fönster som aldrig mappats skriver
dess `_NET_WM_STATE`/`WM_HINTS.initial_state` — `setUnmappedStartState`; och
`setPosition` före den första mappningen sätter `USPosition`),
`wayland_window.cpp` (den första visningen av ett dolt skapat, redan
konfigurerat fönster kräver ingen ny configure-runda — B80-4; `raise()` på
ett noActivate-fönster gör ingenting) och `Glaze/widgets/src/app_window.cpp`
(varje fönster utom popuper skapas DOLT, `onLoad` fyras, och det visas
sedan). Inte kompilerat på Linux.

- [ ] **Bygget och sviten på Wayland OCH X11.** *Rätt utfall:* 0 varningar;
  `geometry_gate` L, `window_gate` R1/R2/R5, `input_gate` M och `dock_gate`
  gröna.
- [ ] **Varje fönster syns direkt (80, Wayland).** *Vad man gör:* `demo`,
  `demo --gallery` och GlazeBuilder; en meddelanderuta, filväljaren, en
  dropdown, en meny och tooltipen — på Mutter (GNOME), KWin och gärna en
  wlroots-kompositör. *Rätt utfall:* varje fönster dyker upp direkt — ingen
  sekunds väntan och inget fönster som aldrig syns (det vore B80-4:s väg som
  väntar på en configure som inte kommer) — och dialogerna centrerade i sin
  riktiga storlek (Wayland: kompositören placerar dem).
- [ ] **Startplatsen och startläget på X11 (80).** *Vad man gör:*
  `geometry_gate -v` del L, `demo` → meddelanderutan. *Rätt utfall:* L1
  (platsen i `onLoad`) och L3 (centrerat med `onLoad`s storlek) gröna —
  fönsterhanteraren respekterar `USPosition` — och L6 (dold start +
  `maximize()`) syns maximerad direkt, utan ett ögonblick i normalläge.
- [ ] **Ett ÅTERÖPPNAT dolt fönster på X11 (80, Codex-granskningen).** *Vad
  man gör:* öppna och stäng ett fönster, öppna SAMMA objekt igen med `hide()`
  före `show()` (dold start) och anropa `restore()` medan det är dolt. *Rätt
  utfall:* fönstret förblir dolt tills `show()` — init nollar `everMapped_`
  m.fl. för varje nytt X-fönster, så `restore()` mappar det inte före den
  första visningen.
- [ ] **`hide()` + `show()` på Wayland under GNOME/KDE (dock_gate FT11).**
  `WaylandWindow::setVisible` (wayland_window.cpp): en återvisning som inte
  får någon ny configure inom 1 s mappar nu med den FÖRRA (WSLg:s Weston
  svarar aldrig på den nya initiala committen — fönstret blev förr dolt för
  gott). Bara kört under WSLg. *Vad man gör:* `dock_gate` på Mutter och KWin;
  `demo --dock`: stäng ett flytande fält (✕) och visa det igen. *Rätt utfall:*
  fönstret syns igen direkt, ingen loggrad "åter-visningen fick ingen
  configure" och inget protokollfel (en kompositör som följer protokollet
  svarar med en configure, och den vägen är oförändrad).
- [ ] **Fältet överst (87, X11).** *Vad man gör:* `demo --dock` under X11:
  låt ett verktygsfält och en panel flyta så att de överlappar, klicka i
  panelen och dra fältet i greppet över den. *Rätt utfall:* fältet ligger
  överst medan det dras och efter släppet, och fokus stannar (fältet
  aktiveras aldrig). På Wayland är fältets drag av (`isFloatDragSupported`)
  — raden gäller inte där.
- [ ] **Golvet och taket i Glyph-programmet (91, REQ3_RITYTAN).**
  *Vad:* `linux_glyph.cpp` klampar `sizePx` till `uPaint1.x`/`.w`
  (`SceneUniforms::glyphMinPx`/`glyphMaxPx`) före allt som läser den, i Glyph
  och Glyph3. Grindarna (`scene_pixel_gate` D10–D12, `geom_gate` S7,
  `painter_gate` GT/ZV/MB, `plumbing_gate` PX, `icon_gate` G11, `canvas_gate`
  M8–M9/ZV) är gröna i Linux-sviten 2026-10-13 (WSLg, Wayland OCH X11). *Vad
  man gör:* `demo --gallery` → Rityta på Wayland och X11, på en riktig
  kompositör. *Rätt utfall:*
  anteckningen nordost om Ekparken stannar i sin ruta med "…" (alla fyra
  språken) och följer rutan under zoom, vridning och
  tilt, "Kyrkbacken" växer med zoomen men stannar vid ≈ 2 mm och ≈ 6 mm, och
  husens namn tonar in efter ungefär två gångers inzoomning.

## Storleken, tangentens text och fönstret (TODO 90, REQ3_LAYOUT_FONSTER, 2026-10-13)

- **Alt+bokstav och Ctrl+siffra skriver inte, AltGr gör det** — Wayland och
  X11, BLINT (`wayland_display.cpp` `emitKey`, `x11_display.cpp` `handleKey`,
  `xkb_keys.cpp` `xkbTypesText`). *Gör:* `demo --gallery` → Interaktion,
  grupp 35: skriv å, AltGr+2 (sv-layout), ´ sedan e, Alt+a och Ctrl+1 i fältet.
  *Rätt utfall:* statusraden visar texten «å», «@», «é» (på e) och «» för
  Alt+a och Ctrl+1; varje tecken skrivs EN gång i fältet, och Alt+a/Ctrl+1
  skriver ingenting.
- **`numpad_gate` X2 mot riktiga keymaps** — `tests/numpad_gate.cpp`
  `fallXkbText`. *Gör:* kör grinden på en riktig Debian (xkeyboard-config med
  us och se). *Rätt utfall:* X2 grön (a, Shift+a skriver; Ctrl+1, Alt+a,
  Super+a skriver inte; se AltGr+2 skriver).
- **`onResize` under ett fönsterdrag** — `app_window.cpp` (leveransen i
  pumpens svans). *Gör:* `demo --window-lab`, dra i fönstrets kant (Wayland
  och X11). *Rätt utfall:* "Innehåll: B × H" följer per bildruta under
  draget, också efter mer än en sekund (en storleksändring av fönstret
  nollar kedjespärren).
- **"Följ innehållet"** — `setSizeToContent(Height)` (`refit`, gränserna per
  axel genom `setSizeLimits`). *Gör:* fönsterlabbet → "Följ innehållet",
  kryssa "Fler rader" av och på, dra dialogen bredare. *Rätt utfall:*
  dialogen växer och krymper med raderna; höjden går inte att dra; bredare ⇒
  texten bryts om och höjden följer (Wayland: kompositören respekterar
  min = max på höjden; X11 likaså via WM_NORMAL_HINTS).
- **Aktivt fönster med en popup (Wayland, BLINT)** — `AppWindow::handleEvent`
  (fokus för en popup räknas till ägarkedjans rot). *Gör:* fönsterlabbet,
  öppna en dropdown eller meny i ett annat glaze-fönster och klicka sedan i
  labbet. *Rätt utfall:* "Aktivt fönster" byter till labbet — det gamla
  fönstret står inte kvar som aktivt efter popupen.

## Träd, listor och fält (TODO 89, REQ3_TRAD_LISTOR_FALT, 2026-10-13)

- **Snabbmenyn, autoskrollen och fälten** — ingen backendkod, men högerknappen,
  timern och fontens blanksteg är plattformens (`drag_drop.cpp` autoskrollen,
  `tree_view.cpp`/`list_view.cpp` `rightDown`, `text_field.cpp` tabbstoppen).
  *Gör:* `demo --gallery`, Wayland och X11. Tabeller och träd: högerklicka på
  en OVALD nod (väljs och "Ta bort"-menyn visas), på en vald (valet står
  kvar); dra en nod och håll pekaren nära trädets nederkant; listvyns
  namnkolumn fyller bredden. Fliken Text, rulla ner till grupp 36: Tab i
  anteckningen (g24) ger en tabb i jämnt stopp och Shift+Tab lämnar fältet;
  den kortade etiketten har "…" och tooltip; "Namn" står mitt för fältet;
  röd "Ta bort", 32 px-ikonen, 48 px-miniatyrerna; filväljarens första fält
  öppnar dialogen "Välj en bild". *Rätt utfall:* som beskrivet, och trädet
  rullar jämnt (ingen hackig eller rusande takt) och slutar vid kanten.

## Dubbelklickets tolerans i skärmrymden (TODO 97, Mac-passet 2026-10-14)

- **`countClick` avbildar toleransen genom `clientToScreen`** (`Glaze/widgets/src/
  app_window.cpp`) — rättat för macOS, där skärmrymden är punkter; här är
  avbildningen en ren förskjutning och toleransen ska bli exakt densamma som
  förut. *Gör:* `ctest -R input_gate`, och dubbelklicka i en lista i
  `demo --gallery`. *Rätt utfall:* D-delen grön, och dubbelklicket fungerar
  som förut.

## `app_perf` — en app på riktigt, med minnet (2026-10-14, BLINT skrivet)

- **Bygget och minnet ur `/proc/self/status`** (`tests/app_perf.cpp`,
  `läsMinne`: `VmRSS` och `VmHWM`) — *Gör:* bygg `app_perf` i Release och kör
  `./build-rel/tests/app_perf --synlig` under Wayland och X11, utan att röra
  musen. *Rätt utfall:* 0 varningar; två fönster (kartan och sedan 3D-vyn)
  som rör sig själva i ~90 s; minnesraderna har `process` och `topp` (GPU
  `—`); sist `sessionen gick igenom` och sluttabellen. Kör också utan
  `--synlig` (dolda fönster, ctest:s läge). På Wayland går fönstren inte att
  placera, så de kan hamna ovanpå varandra — det påverkar inte mätningen.

- **Varvtestet** (`app_perf --varv 4`, synligt) — *Gör:* kör det utan att
  röra musen (~8 minuter). *Rätt utfall:* tabellen sist växer med högst
  tiotals MB per varv efter varv 1, inte GB. På macOS läckte Metal 8 GB per
  varv innan `renderFrame` fick sin autorelease-pool (PERFORMANCE anm. 12).
  Backenden här har ingen autorelease, men kan ha en egen läcka.

## Ritytans textmått i layouten och uppladdningarna (TODO 102, REQ7_RITYTAN, 2026-10-14, BLINT skrivet)

Koden: `Canvas::measureText(c, …)` och vakten i `Glaze/widgets/src/widgets/
canvas.cpp`, `detail::measureNeedsWorld` i `Glaze/widgets/src/canvas/build.cpp`
(samma kod på alla tre). Uppladdningarnas vägar är bara Metal-kod: GL skriver
med `glBufferSubData`/`glTexSubImage2D` i kontextens ordning och har inget pass
att bryta, så `RenderStats::uploadBreaks` är 0 här per konstruktion.

- **Grindfallen, Wayland och X11** — *Gör:* `ctest -R "canvas_gate|geom_gate|scene_pixel_gate|canvas_perf"`
  under båda drivrutinerna. *Rätt utfall:* canvas_gate MR1–MR5b, geom_gate S8,
  scene_pixel_gate A8–A11/B3/B12–B13 (brotten 0 — också A8 och B3 —, pixlarna
  gröna), canvas_perf U och canvas_perf3d S/Ä gröna.

## Formuläret — rader mot nederkanten, innehållet som golv och etikettkolumnen — Wayland OCH X11 (TODO 101, REQ7_LAYOUT, 2026-10-14)

Ingen Linux-kod rörs: beteendet går genom `AppWindow::refit`/`effMin*`
(`Glaze/widgets/src/app_window.cpp`), `Widget::extentOf` (`widget.cpp`) och
`Label::groupWidth` (`label.cpp`) — och till fönsterhanteraren genom
`setSizeLimits` (Wayland `xdg_toplevel_set_min_size`/`set_max_size`, X11
`WM_NORMAL_HINTS`). Demon: `apps/demo/src/form_dialog.*`. Samma fyra fall
under BÅDA drivrutinerna.

- **Formuläret öppnas och dras** — *Gör:* `demo --window-lab` → "Formulär…",
  dra nederkanten uppåt och nedåt och högerkanten inåt och utåt. *Rätt utfall:* dialogen öppnas i
  innehållets höjd (OK och Avbryt nere till höger, kontaktlistan mellan
  kryssrutorna och knapparna); kanten går inte att dra uppåt förbi
  innehållet (kompositören/WM:en följer min-storleken), men nedåt, och listan
  växer med; högerkanten går inte att dra inåt förbi etikettkolumnen och
  fältens minsta bredd, men utåt (fälten växer).
- **Lås och visa** — *Gör:* kryssa "Lås storleken", försök dra i kanten;
  kryssa "Visa kontakterna" av och på, låst och olåst. *Rätt utfall:* låst
  går kanten inte att dra och dialogen krymper till innehållet; låst krymper
  och växer den med listan, olåst växer den bara.
- **Maximera** — *Gör:* olåst: maximera och återställ. *Rätt utfall:*
  maximeringen går, och tillbaka står dialogen i sin storlek med golvet kvar.
- **Etikettkolumnen** — *Gör:* startfönstrets språk fi, sedan stil
  Minimalistic, med formuläret öppet. *Rätt utfall:* etikettkolumnen breddas
  i båda ramarna och fälten står i linje i Kontakt och Adress.

## Markdown-vyn — formaterad text i en rullande yta (TODO 103, REQ7_MARKDOWN, 2026-10-14)

BLINT skriven för Linux: ingen backendkod rörs, men vyn läser
FreeType/fontconfig-fonterna (fet, kursiv, DejaVu Sans Mono) under BÅDA drivrutinerna genom fönstrets `FontSet`
(`Glaze/widgets/src/font_set.cpp`) och öppnar länkar med `gfx::openUrl`.
Koden: `Glaze/widgets/src/widgets/markdown_view.cpp`, `Glaze/widgets/src/markdown/`.

- **Grupp 37** — *Gör:* `demo --gallery` → fliken Text, rulla till grupp 37,
  i Slate, Glaze och Minimalistic. *Rätt utfall:* rubrikerna i tre storlekar
  och fetstil; kursiv; koden i DejaVu Sans Mono på SAMMA baslinje som texten
  runt den; citatstrecket, linjen, tabellen och bilderna (info-ikonen och
  kugghjulet) syns.
- **Länkarna** — *Gör:* peka på länkarna, klicka "GlazeUI på GitHub", "Till
  koden" och "byt till fliken Tabeller och träd". *Rätt utfall:* handen och
  tooltipen (titeln eller adressen) över en länk; GitHub-länken öppnar
  webbläsaren, `#kod` rullar till rubriken Kod, `demo:data` byter flik;
  statusraden visar målet.
- **Den korta vyn** — *Gör:* gör fönstret smalare och bredare. *Rätt utfall:*
  den korta vyn till höger växer på höjden när den smalnar, och raden under
  följer den.

## Utskriften — en läsbar degradering (REQ8 10, 2026-10-14)

`Glaze/gfx/src/backend_linux/linux_print.cpp` svarar att det inte finns något
utskriftssystem (ingen GTK/Qt; vägen dit — XDG-portalen
`org.freedesktop.portal.Print` eller CUPS — står i filhuvudet).

- [ ] **Bygget**: `linux_print.cpp` kompilerar utan varningar i Linux-blocket.
- [ ] **Degraderingen är läsbar**: `glaze::PrintJob::isSupported()` är false,
  `showDialog` och `run` svarar false utan dialog och utan förloppsfönster,
  och loggen har EN rad per process ("utskriften är inte byggd på den här
  plattformen (Linux)").

## Mip-kedjorna och rutorna — BLINT skrivna (REQ8 5, 2026-10-14)

Koden: `Glaze/gfx/src/backend_linux/linux_image.cpp` (`createImage(…, mipmaps)`,
`updateImage`), `linux_renderer.cpp` (`createTexture` med `GL_TEXTURE_MAX_LEVEL`
0 för en textur UTAN kedja, `createOffscreen` likadant, `generateMips` med
`glGenerateMipmap`, samplerna `GL_LINEAR_MIPMAP_LINEAR` och
`GL_NEAREST_MIPMAP_NEAREST`, `GL_MAX_TEXTURE_SIZE` i init),
`linux_target.cpp` (målets kedja efter rensningen och när målet lämnas) och
`gl_common.*` (`pglGenerateMipmap`). Rutorna är glaze-sidans, som på Windows.

- [ ] **Bygget**: Linux-blocket kompilerar utan varningar.
- [ ] **INGEN SVART TEXTUR** — samplerna har nu mip-filter, så en textur utan
  kedja och utan `MAX_LEVEL 0` vore ofullständig och ritas svart. *Gör:* demon
  och galleriet under Wayland och X11 — text, ikoner, glyfatlasen, compositen,
  ritytans etiketter (SDF-atlasen), gradienterna (rampen) och render-målen.
  *Rätt utfall:* allt som förut; ingenting svart.
- [ ] **Kedjan på en Bitmap och en RenderTexture** — som macOS-raden: utzoomat
  utan gryn och moiré, `Nearest` skarpt inzoomat, kedjan följer `setPixels`
  och `repaint()`.
- [ ] **Rutorna** — en `Bitmap` större än `GL_MAX_TEXTURE_SIZE` (llvmpipe:
  16384; 20000 × 3000 px) som `Shape::image`, också på fyra hörn och under
  tilt: hela bilden, ingen söm vid rutgränserna.

## `gfx::openFile` — BLINT skriven (REQ8 11, 2026-10-14)

Koden: `Glaze/gfx/src/backend_linux/linux_platform.cpp` (`openFile`, och den
delade `xdgOpen` som `openUrl` också går genom — dubbel `fork` + `execlp
("xdg-open")`). Sökvägen görs absolut (`std::filesystem::absolute`) och
finns-kontrolleras FÖRE `xdg-open`: dubbelforken ger inget svar om filen
finns, och en relativ sökväg som börjar med `-` hade tolkats som en flagga.

- [ ] **Bygget**: Linux-blocket kompilerar utan varningar.
- [ ] **En fil med mellanslag och å/ä/ö i sökvägen** — anropa `gfx::openFile`
  (t.ex. ur en knapp i demon) med `~/Dokument/Årsrapport 2026.pdf` (eller en
  bild). *Rätt utfall:* standardprogrammet öppnar filen, returvärdet är true.
- [ ] **En fil som inte finns, och en tom sträng** — false i båda; den första
  loggar `openFile: filen finns inte (…)` till stderr, den tomma loggar inget.
  Ingen `xdg-open` startas (kontrollera med `ps` att inget barn blir kvar).
- [ ] **En relativ sökväg som börjar med `-`** (`-konstig.txt` i
  arbetskatalogen) — öppnas som en fil, inte som en flagga till `xdg-open`.

## Ritytans text — plattans kontur, radavståndet, delarnas bakgrund och tabbstopp — BLINT skriven shader (REQ8 15–16, 2026-10-14)

Koden: Glyph-programmets fragmentshader i
`Glaze/gfx/src/backend_linux/linux_glyph.cpp` — för PLATTAN (`shape == 1`) är
konturen bara bandet mellan ytterkanten och fyllningen (`outer - inner`), så
en genomskinlig platta inte färgas av konturen och en osynlig blir en ram.
Cirkeln och kvadraten (markörerna) ritas som förut. Layouten
(`Glaze/gfx/src/sdf.cpp`) är backend-neutral.

- [ ] **Bygget**: Linux-blocket kompilerar utan varningar, och Glyph-programmet
  länkar (ingen GLSL-loggrad i stderr) under Wayland och X11.
- [ ] **En etikett med halvgenomskinlig platta och kontur** —
  `TextStyle{.background = gfx::Color{255, 255, 255, 128}, .padding = glaze::px(4),
  .outline = gfx::Color{0, 0, 0}, .outlineWidth = glaze::px(2)}` över en färgad
  yta. *Rätt utfall:* plattans insida är halvgenomskinligt vit (inte grå), och
  ramen är 2 px svart UTANFÖR luften.
- [ ] **Bara en ram** — samma utan `background`. *Rätt utfall:* insidan är helt
  genomskinlig, bara ramen och texten syns.
- [ ] **Markörerna oförändrade** — en markör med kontur ser ut som förut.

## Färgjusteringen — kontrast, ljushet och mättnad — BLINT skriven GLSL (REQ8 3, 2026-10-14)

Koden: `glaze_colorAdjust` i `Glaze/gfx/src/backend_linux/gl_common.cpp`
(`kGlslAdjustSrc` — i de egna programmens förspel i BÅDA stegen,
`linux_program.cpp`, och i Image-programmets fragmentshader); Image-programmets
nya uniform `uAdjust` i `linux_image.cpp` (location-kontrollen i init, en
saknad ⇒ inga bilder och en loggrad). Mesh-programmets GLSL i
`Glaze/widgets/src/canvas/programs3d.cpp` (`GlazeUser.adjust` sist, 288 byte).

- [ ] **Bygget**: Linux-blocket kompilerar utan varningar, och Image-programmet
  länkar (ingen "Image-programmets uniformer saknas" i stderr) under Wayland
  och X11.
- [ ] **Grindarna**: `scene_pixel_gate` B14/B15, `scene3d_pixel_gate` E6 och
  `mesh_program_gate` E6 (och V1: alla 128 Mesh-varianter länkar med det
  längre förspelet) gröna under Wayland och X11.
- [ ] **Förvalen orörda** — galleriets rityta och 3D-flikarna ser ut som förut
  (B14:s första fall kräver bildens EXAKTA texel, också på llvmpipe).

## Målarens klipp — BLINT skriven GLSL (REQ8 1, 2026-10-14)

Koden: uniformerna `uSceneClip[8]`/`uSceneClipN` och `sceneClip(p)` i
`kGlslClipSrc` (`Glaze/gfx/src/backend_linux/gl_common.cpp` — också i
UI-programmets fragmentshader, där funktionen aldrig anropas), anropet sist i
Line-/Line3-, Fill-, Glyph-/Glyph3- och Image-programmens fragmentshader
(`linux_scene.cpp`, `linux_fill.cpp`, `linux_glyph.cpp`, `linux_image.cpp`;
läget ur samma varying som klippmaskerna, aldrig `gl_FragCoord`) och
`findSceneClip`/`setSceneClip` (`linux_scene.cpp` — locations-kontrollen i
varje programs init: en saknad ⇒ programmet byggs inte och en loggrad).
glaze-sidan (`Glaze/widgets/src/canvas/painter.cpp` `clipTo`) är densamma som
på Windows.

- [ ] **Bygget**: Linux-blocket kompilerar utan varningar, och alla sex
  scenprogram länkar (ingen "uniformer saknas" med `uSceneClip` i stderr)
  under Wayland och X11.
- [ ] **Grindarna**: `painter_gate` KL, `scene_pixel_gate` K1–K4 och
  `scene3d_pixel_gate` KL1–KL3 gröna under Wayland och X11 — KL2 mäter att det
  bortklippta inte skriver djup (`discard`).
- [ ] **För ögat**: i `onPaint` en `save(glaze::Rect{…})` efter `rotate(30)`
  kring en stor yta, en linje, en text och en bild — allt klipps till den
  vridna rutan med en mjuk kant, inte speglat i y (GL:s y-flippfälla), och
  efter lägets slut ritas utan klipp.

## Popupen som tar tangentbordet, textstorleken och gränserna i innehållsmått — Wayland OCH X11 (REQ8 21, 26, 27, 2026-10-14)

Ingen backendkod — allt i widget-lagret, byggt och grindat på Windows. Det
OPRÖVADE här: PLATTFORMSDELTAT för en popup som TAR TANGENTBORDET
(`AppWindow::setPopupTakesKeys`, `forwardToKeyPopup` i
`Glaze/widgets/src/app_window.cpp`) — på X11 landar tangenterna hos ägaren som
skickar dem vidare, på Wayland ger popupens grab den tangenterna direkt när den
öppnades av ett TRYCK (och då går släppet av en tangent ägaren höll genom
`forwardKeyUp`), men inte när den öppnades med tangentbordet. Och att
innehållsgränserna när WM:en genom `setSizeLimits` med statusraden inräknad.

- [ ] **Grindarna**: `input_gate` KP, `colorpicker_gate`, `datepicker_gate`,
  `textfield_gate` W3 och `geometry_gate` CM gröna under Wayland och X11.
- [ ] **För handen, Wayland**: galleriets färgväljare öppnad med ett KLICK och
  med Space — i båda fallen tar paletten pilarna, Tab går till reglagen,
  hexfältet tar tecknen och Escape stänger. Datumväljaren likadant (Ned/F4
  och knappen).

## Prestandan — profileraren, GPU-tiden, `terrain_perf` och ritytans omritning — Wayland OCH X11, BLINT skrivet (TODO 98, PRESTANDA, 2026-10-17)

Byggt och grindat på Windows; GL-koden är skriven blint och aldrig
kompilerad. Koden: `loadGlTimerQueries` (`Glaze/gfx/src/backend_linux/gl_common.cpp`
— valfri, GL ≥ 3.3 eller `ARB_timer_query`; utan den ritas det som förut men
ingen GPU-tid), GpuQuery-ringen med två `GL_TIMESTAMP` per frame
(`linux_renderer.cpp`, `startGpuQuery`/`collectGpuQueries` — läst med
`GL_QUERY_RESULT_AVAILABLE`, ingen väntan), `presentNs` = swappen i
renderaren + Waylands frame-callback-väntan i `LinuxWindow::renderFrame`
(`linux_platform.cpp`, `addPresentNs`), `compileNs` över programbyggena och
loggraden "GL: ritar med <GL_RENDERER>". Resten är glaze-sidans och densamma
som på Windows.

- [ ] **Bygget** — *Gör:* bygg hela trädet med `-DGLAZE_TESTS=ON`. *Rätt
  utfall:* 0 varningar i `backend_linux` (`GL_MAJOR_VERSION` och glext-typerna
  `PFNGLQUERYCOUNTERPROC` m.fl. ur systemets headers), i `tests/perf/`
  (selens Linux-gren, `VmRSS`) och i `terrain_perf`.
- [ ] **Grindarna under `GLAZE_LINUX_DRIVER=wayland` och `x11`** — *Gör:*
  `ctest -R "profiler_gate|plumbing_gate|post_gate|canvas_gate|canvas3d_gate|pick_gate|scene3d_pixel_gate|scene_math_gate|canvas_perf3d|preview_gate|roundtrip_gate|app_perf|terrain_perf"`
  med båda drivrutinerna. *Rätt utfall:* gröna — `profiler_gate` P1–P4, P6
  och P8 (på llvmpipe får P8 svara SKIPPED, exit 77; på Mesa/d3d12 i WSL ska
  GPU-tiden komma), `plumbing_gate` R1–R3, `post_gate` del 3, `canvas_gate`
  D1, `canvas3d_gate` T2, `pick_gate` R-skym, R-snap, R4 och HI1–HI7,
  `scene3d_pixel_gate` H6c/H7/H8, `scene_math_gate` fall 20 och
  `canvas_perf3d` M zoom, M5–M8, M10 och M12.
- [ ] **`presentNs` och loggraden** — *Gör:* `demo` med `GLAZE_PROFILE=1`
  under båda drivrutinerna, och läs `glaze::profiler().report()` (t.ex.
  genom `terrain_perf --synlig --steg 12`). *Rätt utfall:* en rad "GL: ritar
  med …" per process (en popup lägger ingen till), och fasen `wait` > 0 i ett
  synligt fönster — på Wayland frame-callbackens väntan plus swappen, på X11
  swappen med intervall 1.
- [ ] **Himlens GLSL i `terrain_perf`** — *Gör:* `terrain_perf --synlig
  --steg 12` under båda. *Rätt utfall:* ingen rad "himlens program
  kompilerade inte", himlen har moln och sol. `--skärm N` gör ingenting på
  Wayland (ingen skärmposition) och flyttar fönstren på X11.
- [ ] **Ett rundat fönster ritar alltid allt** (`roundedChrome_`,
  `Glaze/widgets/src/app_window.cpp`) — *Gör:* ett fönster med en rityta och
  en HUD i `onPaintOverlay` som uppdateras med `invalidateOverlay()`, med
  `GLAZE_PROFILE=1`. *Rätt utfall:* räknaren `full_damage` är 1 i varje bild
  på Linux (rundningen kräver full omritning), men 3D-målet ritas inte om
  (`target_renders` 0 i de bilderna) — skriv upp vad det kostar.

## Inmatningen — rullningens riktning och Shift+hjul — Wayland OCH X11 (REQ9_INMATNING, 2026-10-20)

- [ ] **Rullningens riktning och Shift+hjul (REQ9_INMATNING, BLINT)** — *Gör:*
  på Wayland (GNOME/KDE) och X11 (libinput): slå naturlig rullning på och av i
  skrivbordets inställningar, sätt `setWheelInvert(Touchpad, true, false)` och
  rulla en lista med två fingrar; rulla en bred tabell med Shift+mushjul; och
  i galleriets 3D-grupp, Ctrl + tvåfingersdrag. *Rätt utfall:* vändningen
  vänder relativt skrivbordets val, Shift+hjul NED rullar åt HÖGER, och Ctrl +
  tvåfingersdrag orbitar som förut (bindningstabellens förval).

## Terminalen — posix_openpt, Wayland OCH X11, BLINT skrivet (REQ9 3, REQ9_TERMINAL, 2026-10-20)

Sidopaketet `Terminal/` är skrivet och byggt på Windows; POSIX-lagret
(`Terminal/src/pty_posix.cpp` — `pipe2`, `environ`, `TIOCSCTTY`) och
tangentkodningen för Alt som Meta (`Terminal/src/ui/keys.cpp`) har aldrig
kompilerats eller körts på Linux.

- [ ] **Bygget och grindarna**: `terminal_gate` (S1–S6 mot `terminal_probe`)
  och `terminal_ui_gate`, på Wayland och X11. *Rätt utfall:* gröna.
- [ ] **Programmen i demons terminalflik** (`demo --dock`, fliken Terminal):
  `vim`, `htop`, `less` och `claude`; förvalet är `$SHELL`. *Rätt utfall:*
  skärmen ritas rätt, färgerna syns, och skalet är användarens.
- [ ] **Tangenterna**: AltGr-tecken (`@`, `€`, `{`), Alt+x i `bash` (Meta-x →
  ESC x, t.ex. Alt+b/Alt+f flyttar ett ord), Ctrl+C avbryter `sleep 100`,
  Ctrl+Shift+C/V och Shift+Insert. *Rätt utfall:* AltGr skriver tecknet,
  Alt flyttar ordvis, Ctrl+C avbryter, urklippet går.
- [ ] **Storleken och rivningen**: dra i fönstret med `htop` igång, stäng
  sedan fönstret. *Rätt utfall:* `htop` ritar om (SIGWINCH); inga
  zombieprocesser (`ps -e -o stat,comm | grep Z`).
- [ ] **Läget**: byt ljust/mörkt i skrivbordets inställningar (portalen) med
  färgad utskrift på skärmen. *Rätt utfall:* färgerna byter direkt.

## Grafikkortet — sysfs, miljön i gfx::init och EGL, BLINT skrivet (REQ9 1–2, REQ9_GRAFIKKORTET, 2026-10-20)

`Glaze/gfx/src/backend_linux/linux_gpu.cpp` (listan ur sysfs,
`applyGpuEnvironment` först i `gfx::init`, `eglAdapterId`,
`reportGpuOutcome`, `linuxChildEnvironment`) och `xdgOpen` med `execve` i
`linux_platform.cpp` är syntaxkontrollerade men aldrig körda. Regeln själv
(`gpu_util.h`) grindas headless av `gpu_gate`, som INTE mäter att Mesa och
libglvnd läser variablerna, den riktiga sysfs-läsningen eller EGL-frågan.

- [ ] **Bygget och grindarna**: `gpu_gate` och `scene3d_pixel_gate` K2–K4, på
  Wayland och X11 var för sig. *Rätt utfall:* gröna (K2/K3 får skriva SKIP
  under llvmpipe).
- [ ] **Listan**: `terrain_perf --gpus` på en maskin med två kort. *Rätt
  utfall:* båda korten, id `pci:vvvv:dddd:ssssssss:rr`, namnen, slaget
  (Intel integrerat, NVIDIA/AMD-dGPU diskret), förvalet på boot_vga-kortet,
  inga `card1-HDMI-A-1` eller simpledrm.
- [ ] **Intel + NVIDIA (proprietär drivrutin), HighPerformance**, startad från
  en terminal (`terrain_perf --hp`). *Rätt utfall:* loggen "gfx:
  grafikkortet — mål pci:10de:…, satt __NV_PRIME_RENDER_OFFLOAD=1", ALDRIG
  `DRI_PRIME`; "GL: ritar med NVIDIA … [pci:10de:…]" och ingen rad om
  mjukvarurendering.
- [ ] **Intel + AMD** (`terrain_perf --gpu <AMD:s id>`). *Rätt utfall:*
  `DRI_PRIME=pci-0000_03_00_0` (kortets slot) i loggen och `GL_RENDERER` är
  AMD-kortet.
- [ ] **Användarens variabel**: `DRI_PRIME=1 terrain_perf --hp`. *Rätt
  utfall:* loggen "användarens miljö gäller (DRI_PRIME=1)", ingenting satt,
  och `--gpus` med samma variabel listar inget kort som valbart. Med
  `DRI_PRIME= terrain_perf --gpu <AMD:s id>` (satt men TOM) ska variabeln
  däremot skrivas över: loggen visar `DRI_PRIME=pci-…` satt.
- [ ] **Förvalet NVIDIA (boot_vga), Mesa-kort uttryckligt**: på en maskin där
  NVIDIA:s kort är boot_vga, `terrain_perf --gpu <Intel/AMD:s id>`. *Rätt
  utfall:* `DRI_PRIME=pci-…` sätts; loggen säger om EGL ändå ritar med ett
  annat kort.
- [ ] **`adapterId()` ur EGL**: K2:s info-rad och "ritar med … [id]". *Rätt
  utfall:* id:t är kortet som ritar (inte tomt på en riktig GPU).
- [ ] **Barnprocessen**: välj NVIDIA/AMD, öppna en länk (demons Link) och läs
  `/proc/<xdg-open:s eller webbläsarens pid>/environ`; likadant skalet i
  demons terminalflik. *Rätt utfall:* varken `DRI_PRIME` eller
  `__NV_PRIME_RENDER_OFFLOAD` finns där (om användaren inte själv satt dem),
  och länken öppnas som förut.
- [ ] **Ett val efter init**: `gfx::setGpuAdapter` efter första fönstret.
  *Rätt utfall:* en loggrad "gäller vid nästa start" när nästa fönster
  öppnas.

## De skärmvända hörnens djup — punktmolnet vid horisonten — BLINT skriven GLSL, Wayland OCH X11 (REQ9 7, REQ9_BUGGAR, 2026-10-20)

Koden: `glaze_standingZ` i `kGlslStandSrc` (`Glaze/gfx/src/backend_linux/gl_common.cpp`)
— nytt namn och nytt slut (draget högst ¼ av ankarets djup, inget från ½) —
nu också i de egna programmens förspel i BÅDA stegen (`linux_program.cpp`,
delarna räknade med `std::size`), anropad i `linux_scene.cpp` (Line, två
grenar) och `linux_glyph.cpp`. Points-programmets GLSL i
`Glaze/widgets/src/canvas/programs3d.cpp`: den egna kopian borta, `pointZ`
med taket `POINT_STAND_K` (fyra radier i världen).

- [ ] **Grinden**: `scene3d_pixel_gate -v` under Wayland och X11. *Rätt
  utfall:* del J grön (J1–J5): J1–J4 som förut, J5 inga röda px bakom lådan
  vid horisonten för alla fem slagen, J5n och J5t noll röda, J5k-kontrollerna
  gröna. `mesh_program_gate` V1 (alla varianter länkar med det längre
  förspelet) grön. Ledtrådar: *"saknar GLSL-källa"/appens källa tom* —
  delarna räknas fel; *J5 rött* — det gamla slutet kvar.
- [ ] **Demon**: 3D-fliken, ett punktmoln i ögonhöjd bakom en mesh (gå ned
  till markhöjd i förstapersonen). *Rätt utfall:* ingen linje av punkter som
  lyser igenom meshen eller molnet.

## Externa mål och GPU-markör — "stöds inte" i GL, BLINT skrivet, Wayland OCH X11 (OpenXR direkt)

Koden: `GlRenderer::nativeDevice`, `importRenderTarget`, `setExternalFramePacing` och
`insertCompletion` i `Glaze/gfx/src/backend_linux/linux_renderer.cpp` (efter `endOffscreen`) har
aldrig kompilerats. De svarar "stöds inte" enligt kontraktet i `gfx/renderer.h`: `api == None`,
ett ogiltigt mål (`GlRenderTarget`) med text i `error`, ingenting, respektive nullptr med text.

- [ ] **Bygget och sviten**: bygg och kör den kompletta sviten under Wayland och X11. *Rätt utfall:*
  0 varningar i `linux_renderer.cpp`; sviten grön.

## GlazeFrame med `rayCenter` — förspelet i GLSL, BLINT skrivet, Wayland OCH X11 (OpenXR direkt)

Koden: blocket `GlazeFrame` i `kGlslPrelude` (`Glaze/gfx/src/backend_linux/linux_program.cpp`) har
`vec4 rayCenter` SIST (offset 240, std140, 256 byte) — samma form som `gfx::FrameUniforms`, som
laddas upp med `sizeof f`. Aldrig kompilerat eller kört på Linux.

- [ ] **GlazeFrame, 256 byte**: bygg och kör `mesh_program_gate` (fall G1 ritar strålen
  `rayCenter + ndc.x · right + ndc.y · up` genom varje pixel i ett eget GLSL-program, för ett
  asymmetriskt, ett förskjutet och ett symmetriskt frustum) och `scene_math_gate`, under Wayland och
  X11. *Rätt utfall:* GLSL:en länkar utan fel (blocket matchar `FrameUniforms`), G1 grönt (färgen i
  varje provad pixel = hålkamerans riktning ±3, `forward` = (128, 255, 128), raden uppåt rätt väg i
  målet); 0 varningar.
