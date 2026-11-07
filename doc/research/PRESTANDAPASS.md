# Prestandapass 2 — buggar och bromsar

Skriven 2026-11-07. **Ingen kod är ändrad** — det här är undersökningen och
planen för arbetet, som fortsätter i branchen `performance`.

## Vad som gjordes

1. **Bygget på Linux** (Ubuntu 24.04, GCC 13.3, Debug, `-DGLAZE_TESTS=ON`):
   hela trädet bygger, **en varning** — `tests/terminal_probe.cpp:125`
   (`-Wformat-truncation`, `hex` är 8 byte). Den blint skrivna Linux-koden
   (backend_linux, `tests/perf/`, `terrain_perf`) kompilerar alltså.
2. **Sviten under Xvfb + llvmpipe, `GLAZE_LINUX_DRIVER=x11`**: 75 av 79 gröna,
   2 röda (`gradient_gate`, `scene_pixel_gate` R3 — P23, P24), 2 hoppade
   (`profiler_gate` P8 på llvmpipe, `net_gate` utan nät). Wayland är inte kört.
3. **Prestandatesterna** på samma maskin: `canvas_perf` och `canvas_perf3d`
   gröna (0 av 4, 0 av 51 fel), `app_perf` röd (P25), `terrain_perf` körd
   till steg 6 av 27 och sedan stoppad (llvmpipe för långsamt; se mätbilagan). Tiderna är llvmpipe-tider (CPU-rendering) och säger inget om
   en riktig GPU — räknarna (anrop, buffertar, skada) gäller ändå.
4. **Kodläsning**: kärnan (app, app_window, widget, draw, tooltip, timer,
   drag_drop, language) läst i sin helhet; tabellen, trädet, dockningen,
   terminalen och nätet i stickprov. Kanvasmotorn, D3D11 och Metal är INTE
   genomlästa i det här passet (steg 6).
5. **cppcheck** gick inte att använda: den vägrar identifierare med å/ä/ö
   (118 filer avbröts med `syntaxError`). Statisk analys måste gå genom
   clang-tidy (steg 6).

Säkerheten står per fynd: **Verifierad** = jag har läst hela kedjan i koden,
**Uppmätt** = syns i en mätning, **Trolig** = läst av granskningen men inte
följd hela vägen av mig. Inget fynd är reproducerat med en grind än — det är
första steget för varje.

---

## Fynden, viktigast först

### Buggar

**P2 — Krasch: ett fönster som stängs under en nästlad modal** · Hög · Verifierad (kedjan, inte körd)
- Var: `app_window.cpp:4554` (`dött` frågar bara objektets liv),
  `:4572` (`pekareNu` → `makeCtx` → `widgetAt`), `:5439–5447`,
  `app.cpp:1031–1032` (`font_.reset()`), `draw.cpp:958` (`c.font->` utan vakt).
- Vad: fönster A:s `onClick` kör `B.run(A)`. Medan B pumpar stängs A
  (appens Quit, en timer, en postad funktion, OS-stängning av ägaren).
  `executeClose` nollar A:s font. När `run` returnerar lever A-objektet, så
  `handleEvent` fortsätter: `pekareNu` → `widgetAt` → mätning → `Draw::measure`
  med `c.font == nullptr`. Namnlistens etikett besöks alltid, så det gäller
  varje fönster med eget chrome. Tangentvägen likadant.
- Rättelse: `dött()` svarar också sant när `!open_`; gärna även en vakt i
  `Draw::measure`/`makeCtx` för ett stängt fönster.
- Grind först: A med en knapp vars `onClick` kör modal B, B:s `onLoad` postar
  `A.close()`; mata Down/Up till A. I dag segfault.

**P13 — Språkbytet går över råa fönsterpekare förbi användarkod** · Låg-medel · Trolig
- Var: `app.cpp:172–196` (`languageChangedAll`).
- Vad: `onLanguageChanged` får riva ett annat `AppWindow`-objekt; senare poster
  i ögonblicksbilden läses då efter frigörning.
- Rättelse: ögonblicksbilden bär varje fönsters `lifeToken()`; fråga det före
  varje åtkomst.
- Grind: A:s `onLanguageChanged` nollar en `unique_ptr` med B, sedan `setLanguage` (ASan).

**P7 — En widget som byter storlek lämnar spökpixlar efter de syskon den flyttar** · Medel · Trolig (medel-hög)
- Var: `label.cpp:176–182` (`setText` → bara `invalidate()`), `widget.cpp:1179–1208`
  (`relayoutSiblings` bara vid synlighet), `app_window.cpp:2750–2789`
  (`collectDamage` skadar bara smutsiga widgets).
- Vad: en autostorleks-Label som växer flyttar ett syskon med `anchorLeft`,
  en Fill-yta under en Top-dockad radbruten etikett eller en autostorleks-förälder.
  Den partiella ritningen ritar bara etikettens gamla ∪ nya yta. Syns inte
  efter en handler (den ritar om allt) men väl när sättaren körs ur
  `app().post`, `Timer::onTick`, ritytans `onMouseMove` eller `onDragOver`.
  Windows och macOS; Linux döljs av P3.
- Rättelse: `collectDamage` jämför varje synlig widgets `paintBounds` mot
  `lastRect_` och skadar båda när de skiljer.
- Grind: etikett + knapp med `anchorLeft = {"etikett", 8}`, `setText` ur en
  timer, jämför `captureFrame` mot en full omritning.

**P11 — Händelser till ett fönster med väntande stängning tappas** · Låg-medel · Trolig
- Var: `app.cpp:599` (`open_ && !pendingClose_`), `:912–929`, `:1059`.
- Vad: under "Spara ändringar?" ur `onClose` är `pendingClose_` satt hela den
  nästlade pumpen: `WindowFocusLost`, `KeyUp`, `MouseButtonUp` och
  `WindowResized` till ägaren tappas (Alt+F4:s släpp försvinner, nästa tryck
  räknas som upprepning). Efter Avbryt lyfts ägaren inte heller.
- Rättelse: släpp bara INMATNING för ett fönster som väntar på stängning;
  avgör lyftet efter vetorundan.
- Grind: `onClose` visar en meddelanderuta och vetar; Alt+F4 + Avbryt;
  pröva `isKeyHeld(Alt)` och `isActive()`.

**P15 — `post()` kan krocka med nedstängningen i väckningen** · Låg · Trolig
- Var: `app.cpp:803–814` (`postWakeup` utanför låset), `app.cpp:244–270`,
  `linux_platform.cpp:268–290`.
- Vad: en tråd lägger i kön, släpper låset och anropar `postWakeup()` medan
  UI-tråden kör `gfx::shutdown()` och stänger eventfd:n — skrivning till en
  stängd eller återanvänd fd.
- Rättelse: räkna väckningar på väg under `postMutex_`; `shutDown` väntar ut
  dem innan `gfx::shutdown`.
- Grind: N trådar postar i en loop medan main avslutar, under TSan.

**P24 — `scene_pixel_gate` R3 (U+0378) röd på Linux** · Medel · Uppmätt, orsak okänd
- Var: `tests/scene_pixel_gate.cpp:5306–5314`, `linux_text.cpp` (`resolve`, `candidates`).
- Vad: en otilldelad kodpunkt ska ge `hasGlyph` false och ersättningens raster
  och steg. Ett av de tre villkoren håller inte (maskinen har Inter som UI-font
  och 119 fonter). Kan vara en riktig reservbugg (en kandidat som "har" U+0378,
  eller U+FFFD ur en annan font än ersättningen jämförs med) eller miljön.
- Gör: skriv ut de tre delsvaren; `fc-list ':charset=378'`.

**P23 — `gradient_gate` röd på Linux** · Låg · Verifierad (testfel)
- Var: `tests/gradient_gate.cpp:217–218, 229, B2`.
- Vad: proven tas i (2, h − 3) och (w − 3, h − 3) — inne i hörnet som Linux
  rundar bort (`windowRadius` 16). Ramverket är inte fel; grinden läser
  genomskinliga pixlar (0,0,0).
- Rättelse: prova en bit in från hörnet (≥ radien), eller fråga `cornerRadius()`.

**P25 — `app_perf` röd utan GPU** · Låg · Uppmätt (miljö)
- Vad: steget "3D: körning 200 km/h" hinner inte på 120 s med llvmpipe.
  Ramverket är inte fel, men grinden är röd på varje Linux-maskin utan GPU.
- Rättelse: svara SKIPPED (77) vid mjukvarurendering, som `profiler_gate` P8.

### Bromsar i kärnan

**P1 — Linux: varje `makeCtx` är ett blockerande D-Bus-anrop** · Hög (Linux) · Verifierad
- Var: `app_window.cpp:502–514` → `theme.cpp:22–33` (`Theme::resolve`, System-läget
  är förval) → `linux_platform.cpp:511–528, 359–379`
  (`dbus_connection_send_with_reply_and_block`, 200 ms tak, `ReadOne` och sedan `Read`).
- Vad: `makeCtx` körs per bild och i varje `ctxFor`, `widgetAt`,
  `refreshCursor` och `toolTipSourceAt` — minst fyra gånger per musrörelse.
  Varje gång en tur och retur till portalen. Svarar portalen långsamt kan
  varje anrop stå i 200–400 ms. `SettingChanged` skickar redan
  `SystemThemeChanged`, så frågan behövs inte.
- Rättelse: cacha OS:ets läge i App, läs om vid `SystemThemeChanged` (och
  vid start). Samma för `osIconColors()`. (På D3D11 och Metal är frågan
  billig, men samma cache skadar inte.)
- Mät: `dbus-monitor "interface='org.freedesktop.portal.Settings'"` medan
  musen rör sig över demon, eller `strace -c -e sendmsg,recvmsg`.

**P3 — Linux: rundat chrome ritar om HELA fönstret i varje bild** · Hög (Linux) · Verifierad + uppmätt
- Var: `app_window.cpp:850–863, 892, 2410–2411` (`full = … || roundedChrome_`).
- Vad: varje standardfönster på Wayland (och X11 med kompositör) har
  `roundedChrome_`, också maximerat. Hover, caretblink, en overlay-bild och
  varje bild av en obestämd Progress blir full omritning. Uppmätt:
  `full_damage 1.0` i varje bild i `terrain_perf`. Står som beslut i
  `doc/os/linux/TESTS.md`, men kostar.
- Rättelse: partiell väg när fönstret är maximerat (radien 0) eller när ingen
  skada når hörnens r × r; annars partiell ritning under `pushClip(klient,
  cornerRadius())`. En ren overlay-bild ska aldrig gå full väg.
- Mät: `GLAZE_PROFILE=1`, `full_damage` per bild vid hover och caret.

**P4 — Handler-epoken stegas av VARJE händelse, så mätcachen nästan aldrig överlever** · Medel-hög · Verifierad
- Var: `app_window.cpp:4724–4737` (`HandlerCall` runt `event(e)` utan villkor),
  mot kontraktet i `app.h:70–77` ("när appens råa krok tagit en händelse"),
  `widget.cpp:351–378`.
- Vad: varje musrörelse, caret-tick, tooltip-timer och fokushändelse stegar
  den globala epoken, också när appen inte overridar `event()`. Varje widgets
  cache missar då, så varje `widgetAt` blir en kall omlayout med textmätning.
  Dessutom stegar rent visuella invalideringar (hover, tryck, fokusring,
  Progress per bild) `subtreeGen_` för widgeten och alla förfäder.
- Rättelse: stega bara när `event()`/`acceleratorFallback` svarade true
  (kontraktet). Inför en ritnings-invalidering utan `stepSubtreeGen` för
  tillstånd som inte ändrar mått.
- Grind: `measure_gate` — en `MouseMotion` utan handlers genom `handleEvent`
  ska lämna `handlerEpoch()` och antalet mätningar orörda.

**P5 — `refreshCursor` körs synkront i sättarna** · Medel-hög · Trolig
- Var: `widget.cpp:184–188, 1008–1015`, `widget.h:2887–2930`, `app_window.cpp:1746–1753`.
- Vad: varje `setVisible`/`setEnabled`/`setCursor`/`unadopt` i ett öppet
  fönster med pekaren inne gör en hel träffvandring direkt. Att dölja eller ta
  bort N barn i en handler blir N vandringar; med P6 O(n³) för en platt panel
  ("inaktivera formuläret medan vi jobbar").
- Rättelse: en flagga, en omprövning — i pump-svansen eller överst i `renderContent`.
- Mät: panel med 1000 etiketter, dölj alla i en handler; räkna `widgetAt`.

**P6 — `Panel::childArea` räknar scrollRange också när panelen inte rullar** · Medel · Verifierad
- Var: `panel.cpp:179–188` (→ `scrollRange` → `contentExtent`, som resolvar varje barn).
- Vad: klampningen blir 0 när scrollen är 0, men spelrummet räknas ändå — n
  barn resolvas, och varje `carveSiblingDocks` går över n syskon: O(n²) per
  container och vandring, och per förfader i varje `ctxFor`.
- Rättelse: returnera `innerRect(c)` direkt när `scrollX_ <= 0 && scrollY_ <= 0`
  (exakt samma svar). Överväg ett minne för `carveSiblingDocks` per vandring.
- Mät: räkna `resolve()` per bild och per musrörelse med en panel om 300–500 barn.

**P8 — En bild utan skada vandrar ändå hela trädet** · Medel · Trolig
- Var: `app_window.cpp:2404–2440, 2483–2494, 2221–2311, 2900–2913`.
- Vad: en ren overlay-bild (namnlistens hover, dragbilden vid varje rörelse)
  kör `stepTree`, `pushHitRegions` och `collectDamage` över hela trädet, och
  overlayn vandrar det igen med en rekursiv `std::function`. Med skada går
  varje rect (upp till 8) från roten, och överlappande slås inte ihop.
- Rättelse: räkna smutsiga widgets per fönster och hoppa över `collectDamage`
  vid 0; cacha hit-kartan (storlek, maximerat, namnlistens generation); slå
  ihop rects eller pröva alla i en vandring.

**P9 — Musrörelser slås inte ihop, och varje rörelse gör flera O(N)-pass** · Medel · Trolig
- Var: `app.cpp:518–522`, `x11_display.cpp:57–71`, `app_window.cpp:4903–5010`, `tooltip.cpp:147–218`.
- Vad: per rörelse `widgetAt`, en hover-synk över hela trädet, två `ctxFor`,
  `toolTipSourceAt` (två vektorer, `childArea` per förfader) och ett
  `startTimer`-anrop — med kall mätcache (P4). XI2 levererar 125–1000 Hz.
- Rättelse: släpp en `MouseMotion` som följs direkt av en till för samma
  fönster med samma knappar och modifierare; spåra den ENA hovrade widgeten;
  räkna `ctxFor(over)` en gång per händelse.

**P10 — Ett minimerat fönster som animerar fortsätter rita** · Medel · Trolig
- Var: `app_window.h:1121–1125` (`wantsFrame`), `progress.cpp:184–188`,
  `d3d11_renderer.cpp:862` (svaret från `Present` läses inte).
- Vad: obestämd Progress, `setAnimating` eller en tickande rityta ritar vidare
  minimerat — på D3D11 ger `Present` `DXGI_STATUS_OCCLUDED` utan vsync-väntan,
  så pumpen snurrar.
- Rättelse: inga bilder för ett minimerat fönster (behåll smutsflaggorna till
  återställningen); läs gärna `DXGI_STATUS_OCCLUDED`.
- Mät: Windows, demon med obestämd Progress, minimera, titta på CPU.

**P12 — Temat i mätnyckeln får barnen i barer att mätas om** · Låg-medel · Trolig
- Var: `widget.cpp:366`, `app_window.cpp:2545, 2600`, `bar.cpp:81–90`.
- Vad: ritvandringen mäter barens barn med den dämpade varianten, träffprovet
  och `ctxFor` med bastemat — cachen (en post) töms vid varje byte, minst två
  gånger per bild. Färger påverkar aldrig mått.
- Rättelse: ta bort `theme` ur `MeasureStamp` (temabyten går redan genom
  `invalidateAll`, som stegar epoken).

**P14 — Dockningens glidning går genom appens postkö** · Låg · Trolig
- Var: `dock_host.cpp:1300–1310`, `app_window.cpp:1332–1346` (`postRefit`).
- Vad: nästa-bild-arbete i `posted_` väntar bakom en skur trådsvar (4 ms
  budget) och stegar epoken varje steg.
- Rättelse: `detail::postFrameWork`.

**P16–P18 — småsaker per varv** · Låg · Trolig
- P16 `widget.cpp:1532–1541`: `g_resolveMemo.clear()` på en tom karta
  nollar hela hinktabellen — vakta med `!empty()`.
- P17 `app.cpp:874–881`: postkön är en vektor där resten läggs först varje
  varv — kvadratiskt under en flod. `std::deque` eller en egen restkö.
- P18 allokeringar per varv: `drainCommitQueues` kopierar `windows_`
  (`app.cpp:886`), `ctxFor` en vektor per anrop (`app_window.cpp:3018–3035`),
  tooltipen två vektorer per rörelse, hit-kartan per bild, `fyraHover`
  invaliderar allt per `onHover`-handler (`:4597–4599`).

### Bromsar i komponenterna

**P19 — `DockHost::placeChild` räknar hela dockningslayouten per barn** · Medel · Verifierad (står redan i TODO.md)
- Var: `dock_host.cpp:345–` (`synced()` kopierar och normaliserar modellen,
  `dynamic_cast` per barn, `computeLayout` med fältens mätning) — och samma
  kopia i `topBandBar()`/`topBandHeight()`, som programikonens mätning frågar.
- Rättelse: TODO-postens — ett minne per layoutpass med säker nyckel.

**P20 — `Table::removeRow` i en loop blir O(k · n log n)** · Medel (stora listor) · Verifierad
- Var: `table.cpp:212–261` (`visibleSnapshot()` kopierar `pos_`, fem `erase`,
  `ensureOrder()` sorterar om, `reportHidden` går över alla rader — per anrop).
- Vad: att ta bort 5 000 rader ur 100 000 en i taget sorterar om tabellen 5 000 gånger.
- Rättelse: en sammanhållen borttagning (`removeRows(lista)` eller ett
  intervall) i kollektionsvokabulärens anda, med EN omsortering. Ny funktion
  — rimlig i en PATCH (bryter inget).
- Mät: listview_gate:s PR-fall med 20 000 rader + en borttagningsloop.

**P21 — Trädets radcache allokerar en stig per rad, och AutoMin-bredden är ocachad** · Låg-medel · Verifierad
- Var: `tree_view.cpp:224–263` (`Row{&n, djup, p}` kopierar en `TreePath` per
  rad vid varje ombyggnad), `:283–302` (`cellContentW` mäter texten för ALLA
  synliga rader — Table har cachen `cellW_`, trädet ingen).
- Vad: varje fällning eller strukturändring i ett stort träd = N
  heap-allokeringar, och med en AutoMin-kolumn N textmätningar.
- Rättelse: stigen räknas vid behov (förälderindex i raden) i stället för att
  lagras; bredd per nod cachad som i Table.

**P22 — Terminalens inkorg flyttar resten vid varje hämtning** · Låg · Verifierad
- Var: `Terminal/src/session.cpp:119–121` (`inbox_.erase(0, n)` på en sträng om upp till 1 MiB).
- Rättelse: läsposition + kompaktering, eller två buffertar som byts.

**P26 — Hundratals nya GPU-buffertar per bild under strömning** · Medel · Uppmätt, orsak ej följd
- Var: räknaren `buffers_created` (`linux_scene.cpp:797`).
- Vad: `terrain_perf` steg 2–4: 93, 124 och 559 nya buffertar per bild;
  `app_perf` panorering och resa ~90 per bild. Samtidigt `glyph_builds`
  15–20 per bild och plattor in/ut. Kan vara byggt så (ombyggda lager och
  textbyggen får nya buffertar), men varje `glGenBuffers` + `glBufferData` är
  drivrutinsarbete.
- Gör: samma steg på Windows med `GLAZE_PROFILE=1`. Är talet lika: en pool
  per storleksklass i `CanvasResources` (de `retired*` som släpps i `trim`
  återanvänds i stället för att rivas).

---

## Ordningen

Varje rättelse följer husets form: **grinden först** (röd före), rättelsen,
sabotaget skrivet och kört. `GLAZE_PROFILE=1` före och efter för bromsarna.
Inget här bryter API:t, så allt ryms i PATCH-versioner.

1. **Baslinjen.** `app_perf`, `terrain_perf` och demon med profileraren på
   Windows (GPU) och Linux med riktig GPU, Wayland OCH X11. Lägg till
   räknare för det som saknas: `makeCtx`, `resolve`, mätmissar per bild och
   per musrörelse.
2. **Buggarna:** P2 (krasch) först, sedan P13, P7, P11, P15. P23–P25 är
   grindarnas egna (snabba). P24 undersöks innan något rättas.
3. **De stora bromsarna i kärnan:** P1, P4, P6 — små ändringar med stor
   verkan, mätbara var för sig. Sedan P3 (störst arbete: partiell ritning
   under rundat klipp) och P5.
4. **De medelstora:** P8, P9, P10, P12, P19, P20, P26.
5. **Småsakerna:** P14, P16, P17, P18, P21, P22.
6. **Det som inte är granskat än**, ett område i taget (ingen parallell utskickning):
   - Kanvasmotorn (`src/canvas/`, 17 000 rader) — förra passet täckte budgetar,
     klipp och terräng; kvar: minnestillväxt (atlaser, masker, mål), cachar som
     aldrig vräks, `readTarget` i interaktiva vägar.
   - D3D11 och Metal — per bild-allokeringar, `Present`/`nextDrawable` som
     blockerar pumpen, autorelease-pooler, Metal-kod som aldrig kompilerats.
   - Listor och textfält i sin helhet (DataGrid, PropertyGrid, TextArea med
     1 MB text, MarkdownView med stort dokument).
   - Dockning och menyer i sin helhet (drag som avbryts, indexstigar).
   - Sidopaketen under **TSan** (`-fsanitize=thread`): Threads, Networking,
     Terminal (`terminal_gate`, `net_gate`, `threads_gate`).
   - **clang-tidy** med `bugprone-*` och `performance-*` över `Glaze/` (ur
     `ninja -t compdb`) — cppcheck klarar inte å/ä/ö.
   - **ASan/UBSan** över hela sviten en gång — P2 och P13 är just sådant.
   - Läcktest: `app_perf --varv N` på en maskin med GPU (processen gick
     10 MB → 1,3 GB på llvmpipe; ingen slutsats utan varv).

## Det som redan är bra (behöver inte göras om)

Postkön (byte under lås, ordningen behålls, budget efter varje funktion,
stängd efter nedstängningen) · sovvillkoret (frågar postat, timer och
stängning) · `Wakeup` ritar inte · timerregistret · `executeClose`s ordning ·
mätcachens mekanik · syskonankarnas minne · `onResize`-spärren · bildcachens
trim och budget · drag-and-drop (ingen kopia av lasten, ingen full omritning
per rörelse) · tooltipens timer · `TrText`s cache · terminalens historik (en
deque) och skärmens rullning (`rotate`) · nätets skrivloop (EINTR,
`MSG_NOSIGNAL`/`SO_NOSIGPIPE`) · etikettplaceringen i ritytan (bara lådor
som når ytan, sorterad bara vid ändring).

## Bilaga: mätningarna (Linux, llvmpipe, Debug, X11 under Xvfb)

`canvas_perf`: 115 644 former, 10 004 etiketter; panorering median 69 ms,
zoom 43 ms, översikten 126 ms per bild; placeringen 1,7–3,1 ms per
kameraändring. Allt grönt.

`canvas_perf3d`: stolpar och träd ~53 ms per kameraändring, 600 000 punkter
293 ms, terräng 106 ms, klipp 3D 17 ms / 2D 16 ms med en mask och 0 nya
mål. Allt grönt.

`app_perf`: kartan går igenom (resa 8 km median 20 ms, p90 24 ms); 3D-steget
med körning hinner inte (P25). Processen 10 MB → 1 344 MB, bildcachen 299 MB
vid 3D-start. ~90 nya buffertar per bild under panorering (P26).

`terrain_perf`: `full_damage 1.0` i varje bild (P3); `buffers_created`
93 / 124 / 559 per bild i steg 2–4 (P26); `glaze.hitTest` median 2,1–3,0 ms,
`glaze.castRay` 0,7–0,8 ms per anrop (Debug, CPU-rendering i bakgrunden).

Loggarna låg i sessionens scratchpad (`build.log`, `ctest.log`, `*_perf.log`)
och följer inte med.
