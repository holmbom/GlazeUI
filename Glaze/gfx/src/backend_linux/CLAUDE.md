# CLAUDE.md — Glaze/gfx/src/backend_linux

Linux-backenden: Wayland OCH X11 i en backend, EGL/GL, FreeType/fontconfig, libpng, D-Bus. Del av `common_gfx` — se [../CLAUDE.md](../CLAUDE.md) för backend-neutrala
filer, event-översättningen och de gemensamma fallgroparna, och
[root /CLAUDE.md](../../../../CLAUDE.md) för lagerbilden och de hårda reglerna. Utbruten ur
`gfx/src/CLAUDE.md` 2026-10-10 (CANVAS_3D, Eriks beslut 2: varje källkodsmapp har sin
egen fil; backendarnas 3D-del hade inte rymts under 1000 rader).

## backend_linux (Linux — Wayland OCH X11 i EN backend; bara systembibliotek)


Spegel av backend_d3d11 (läs det avsnittet); protokollvalet görs i
`gfx::init` (env `GLAZE_LINUX_DRIVER=wayland|x11` → WAYLAND_DISPLAY →
DISPLAY → surfacelös headless).

- **`linux_backend.h`** — protokollNEUTRAL intern header: `GlRenderer`,
  `FtFont`, `PngImage`, `LinuxWindow`-basen (callbacks/chrome/hit-test/
  timerfd-timers/renderFrame-sekvensen), `LinuxDisplay`-växeln, epoll-
  `PollSource`, `pushEvent`, `unwrapWindow` (fasadens fönster är en PROXY
  — typvalet sker i init, EFTER att displayen valts). Wayland-/Xlib-typer
  bor i `wayland_backend.h`/`x11_backend.h` (Xlib förorenar `Window`/
  `None` — får bara dras in av sina egna .cpp).
- **`linux_platform.cpp`** — pumpen: `waitEvent` sover i `epoll_wait`
  (display-fd + timerfd:ar + D-Bus-fd + **wakeup-eventfd:n**; Wayland kör prepare_read-dansen i
  prepoll/postpoll), delad event-kö, **`postWakeup`** (APP_API_2 R17: en
  `eventfd` i SAMMA epoll — SKRIVNINGEN är den enda operation i backenden
  som får göras från en främmande tråd, för event-kön är en olåst
  `std::deque` som ägs av UI-tråden; läsningen och `pushEvent(Wakeup)` sker
  i `onReady`, alltså på UI-tråden inne i `pumpOnce`. Räknaren summerar N
  skrivningar till ETT vaknande), portal-`systemTheme()` +
  textskalningen (`SettingChanged` → `onTextScaleChanged()` per fönster +
  broadcast), **`isInProgramFile(adress)`** (DELAT_BIBLIOTEK R3, BLINT —
  `dl_iterate_phdr`: adressen i ett av HUVUDPROGRAMMETS laddade segment, det
  första objektet som besöks; segmenten och inte basen, för ett program utan
  PIE har förskjutningen 0), **`systemLocale()`** (`LC_ALL` → `LC_MESSAGES` → `LANG` i
  POSIX-prioritet + normalisering — kodning och `@variant` kapas, `_`
  blir `-`, och `C`/`POSIX` ger TOM sträng), **`knownPlaces`** (post 18 —
  `$HOME` med lösenordsdatabasen som reserv, XDG-mapparna ur
  `$XDG_CONFIG_HOME/user-dirs.dirs` i formatets DELMÄNGD: citerat värde som
  börjar med `$HOME` eller är absolut, `\` skyddar nästa tecken, `"$HOME"`
  ensamt = avstängd, och en saknad nyckel ger verktygets förval — skrivbordet
  `$HOME/Desktop`, de andra avstängda. Volymerna: "/" och det
  `/proc/self/mounts` har monterat under `/media/` och `/run/media/`, med
  tabellens oktala skydd (`\040`) avkodat. XDG-läsningen grindas på riktigt
  i `filedialog_gate` K5 med ett eget `$XDG_CONFIG_HOME`; monteringarna kan
  grinden inte skapa), `isHiddenFile` (alltid false — punkten ÄR
  konventionen och tillämpas av filväljaren själv), proxy-fabriken,
  HeadlessWindow/-Display (surfacelös EGL — glaze-render utan display),
  **`openUrl`** (dubbel fork + `execve` — aldrig `system()`/`popen`, skalet
  skulle tolka url:en; `xdg-open` söks i PATH och argv/envp byggs FÖRE fork,
  så barnet bara forkar, `execve`:ar och `_exit`:ar; miljön är
  `linuxChildEnvironment()`, utan grafikkortsvalet; barnbarnet ärvs av init
  så ingen zombie blir kvar, och mellanstegets exit-status bär om forken
  lyckades) i den delade `xdgOpen`; **`openFile`** (REQ8 11, BLINT) går samma väg med en
  ABSOLUT, finns-kontrollerad sökväg — dubbelforken svarar inte om filen finns,
  och en relativ `-x` hade tolkats som en flagga till `xdg-open`.
  **libdbus-fallgropen:** systemTheme():s blockerande portal-anrop läser
  ÄVEN signaler in i libdbus interna kö (fd:n blir aldrig läsbar, epoll ser
  inget) — pumpvarvet dränerar därför dispatch-kön (`dbusDispatchPending`,
  ren statuskoll — ingen polling).
  **`addPollSource` svarar `bool`** sedan FONSTER_OCH_TIMER (Codex-fynd 8):
  en källa som inte kom in i epollen hade annars lämnat `waitEvent` sovande
  för evigt trots att anroparen trodde att timern gick. **`screens()`**
  frågar den aktiva displayens `placementScreens()` (förval tom — Wayland och
  headless har ingen skärmrymd). **`HeadlessWindow::placement()`** =
  `{0, 0, logicalW_, logicalH_, Normal, hasPosition false}` och
  **`ProxyWindow::placement()`** vidarebefordrar efter init (före: tom) —
  geometry_gate W1 mäter det, för bygget fångar ingen tappad vidarebefordran.
- **`linux_timer.cpp`** (FONSTER_OCH_TIMER, 2026-10-12, BLINT) — den DELADE
  timerkällan **`TimerSource`** (`make(windowId, id, ms)`: `timerfd_create`,
  `timerfd_settime` KONTROLLERAD, `addPollSource` kontrollerad — fd:n sätts
  först när allt lyckats, annars stängs den och svaret är nullptr; `onReady`
  tömmer räknaren, så missade tick blir ETT event, och köar
  `EventType::Timer` med windowId och 64-bitars id; dtorn avregistrerar och
  stänger). Bröts ut ur `LinuxWindow::startTimer`, som nu håller
  `unique_ptr<TimerSource>` per id. Appens timer (`gfx/timer.h`) är en karta
  på namnrymdsnivå id → källa med windowId 0: kräver en
  `LinuxDisplay::current()` (false före init), ms < 1 blir 1 (it_value 0
  hade avväpnat), `appTimerShutdown()` först i `gfx::shutdown`. Den
  gemensamma epollen täcker Wayland, X11 och headless.
- **`gl_common.h/.cpp`** (CANVAS_2D etapp 1) — det GL-programmen DELAR:
  loadern (funktionspekarna nyare än GL 1.1 via eglGetProcAddress),
  `compileGlShader` (shader ur flera delar i ordning — glShaderSource tar dem
  som de är), `linkGlProgram` (attribut i fasta platser), `kGlslVersion` och
  **`kGlslClipSrc`** (uniformerna `uClipP0/P1/N` + `sdRoundRect` +
  `clipCoverage`), som VARJE fragmentshader som ritar i ett mål med
  klippstacken klistrar in efter `#version` — och MÅLARENS KLIPP (REQ8 1):
  `uSceneClip[8]`/`uSceneClipN` + `sceneClip(p)`, som scenens sex program
  multiplicerar alfan med (`discard` utanför — inget djup) och som laddas
  upp per anrop med `setSceneClip` (linux_scene.cpp; inget anrop när varken
  anropet eller programmet har ett klipp) efter `findSceneClip` i varje
  programs init (en saknad location ⇒ programmet byggs inte). Loadern bär sedan etapp 3
  SAMPLEROBJEKTEN (`glGenSamplers`/`glBindSampler`/`glSamplerParameteri`).
  Färgens GLSL **`kGlslPaintSrc`** (etapp 4: `uPaint0`/`uPaint1`, sampler-
  enheten `uRamp` och `paintColor`) bor också här och klistras efter
  klippmaskerna i Fill-, Line- och Glyph-programmen — i Glyph bär enheten
  atlassidan. **`kGlslStandSrc`** (REQ9 7) är HLSL:ns `glaze_standingZ`
  (kHlslStandSrc) och klistras efter definitionerna i Line- och
  Glyph-programmens VERTEXshader och i de egna programmens förspel (båda
  stegen, delarna räknade med `std::size`) — de skärmvända hörnens djup.
  **`kGlslAdjustSrc`** (REQ8 3) är `glaze_colorAdjust` — Image-programmets
  fragmentshader och de egna programmens förspel. GL-anropen bor i `gl_common.cpp`, `linux_renderer.cpp`,
  `linux_scene.cpp`, `linux_image.cpp` (bilderna och Image-programmet),
  `linux_target.cpp` (render-målen), `linux_fill.cpp` (ytorna),
  `linux_glyph.cpp` (texten) och `linux_program.cpp` (de egna programmen,
  CANVAS_3D etapp 1 — avsnittet längst ned) — ingen annan.
- **`linux_renderer.cpp`** — `GlRenderer`: EGL-kontext + GLSL-port av
  d3d11:s SDF-shader (mode 0–7), samma Vertex-format/batcher/retained-FBO.
  Text/bild går via textur-hjälparna här (`createTexture(f, w, h, px,
  rowBytes)`/`updateTexture`, format-generiska sedan etapp 3: justering 1 och
  UNPACK_ROW_LENGTH = rowBytes / pixelstorlek). Y-flipparna: glScissor (`sy =
  MÅLETS höjd − bottom` — `targetW_/targetH_` är det BUNDNA målets storlek,
  inte fönstrets; ett render-mål har sin egen),
  glReadPixels (radflipp i captureRetained), composite-quadens UV:er.
  **SAMPLEROBJEKTEN (etapp 3):** två (Nearest, Linear) på enhet 0;
  `flush()` binder batchens VID VARJE RITNING (`bindSampler`) — ett bundet
  samplerobjekt skuggar texturens parametrar, så det Image-programmet
  lämnat hade annars bestämt samplingen för text och composite (texturernas
  egna parametrar gäller alltså aldrig). **Målets storlek till
  UI-programmet** följer LAT: `flush` laddar upp `vp` när `targetW_/
  targetH_` skiljer från det programmet senast fick (`uiVpW_/uiVpH_`).
  `currentFbo_` är den bundna FBO:n (offFbo_ i framen, 0 efter composite,
  ett mål däremellan).
  **MASKGENERATIONEN (CANVAS_2D §5):** GL-uniformer är PER PROGRAM, så
  klippmaskerna LAGRAS (`setClipMasks` stegar `clipGen_` bara när något
  ändras) och laddas upp LAT till det program som ritar (`syncClipMasks` —
  UI:ts flush och scenens drawMesh, var sin generation). Inga masker ⇒
  inget GL-anrop. **`pipeline_`** som D3D11: `flush()` återbinder UI:ts
  program, VAO OCH `GL_ARRAY_BUFFER` (inte VAO-tillstånd — `glBufferData` i
  flush skriver i den buffert som är bunden där).
  **SDF-koordinaten flippas ALDRIG** — den interpoleras ur ritkoordinaten
  (varyingen `vPix`), aldrig ur `gl_FragCoord`: dess y-riktning är olika i
  retained-FBO:n och backbuffern, så en fast flipp speglar masken kring
  fönstermitten i det ena målet (bugg 2026-08-09 — knappbakgrunder försvann).
  **FÖNSTERFORMEN är en mask i compositen** (`setCompositeRadius`,
  KAPSEL_CHROME): mode 5-quaden multiplicerar retained-FBO:n med
  `sdRoundRect`-täckningen, så ett rundat fönster får sin form EFTER att
  allt ritats. Den sitter där och inte i ritvägen för att den måste gälla
  även rects en APP målat själv i sin `frame()`-krok — GlazeBuilder fyller
  hela mittenytan och fyllde annars igen de nedre hörnen. FBO:n bär
  premultiplicerad alfa, så täckningen multipliceras in i alla fyra kanaler.
  **RUNDAT BARNKLIPP är den andra, separata masken** (2026-08-21) och bor
  i RITVÄGEN: `uploadClipMasks` skriver klippstackens rundade nivåer till
  uniformarrayen `uClipP0`/`uClipP1`/`uClipN`, och fragmentshaderns
  `clipCoverage()` multiplicerar in produkten i ALLA modes. Maskerna
  utvärderas mot varyingen **`vPix`, aldrig `gl_FragCoord`** — samma
  y-flippfälla som SDF-koordinaten (se ovan). Att den ligger i ritvägen
  och inte i compositen är hela skillnaden mot fönsterformen: det finns en
  mask PER DELTRÄD, och `captureFrame` läser FBO:n FÖRE compositen, så
  ritvägens klipp är det enda av de två som är headless-grindbart
  (`tests/clip_gate.cpp`). Loadern bär `glUniform4fv` för dem, och
  `buildPipeline` FALLERAR med logg om någon location är -1.
- **`linux_scene.cpp`** (CANVAS_2D etapp 1) — scenen, spegel av
  d3d11_scene.cpp: `GlGpuBuffer` (destruktorn raderar genom renderern —
  fontens livstidsregel), den transienta ringen (lagringen ORPHANAS vid
  framens första uppladdning i stället för DISCARD) och Line-programmet
  (GLSL 330, `noperspective` avstånd, `flat` halvbredd, `vPix` för maskerna,
  z omräknat till GL:s [−w, w] i vertexshadern — matrisen är densamma som
  D3D11:s). **Alla scenbuffertar laddas upp via `GL_COPY_WRITE_BUFFER`**:
  den ingår varken i någon VAO eller i UI-batcherns `GL_ARRAY_BUFFER`, så en
  uppladdning mitt i en frame kan aldrig få UI:ts `glBufferData` att skriva
  över en scenbuffert. Programmets VAO har attributen påslagna en gång;
  pekarna sätts per rit-anrop ur vertexdelens offset (sida, slag och ändar
  som HELTAL, `ivec4` — fallgropen om skalade format), och indexbufferten
  binds medan VAO:n är bunden (den är VAO-tillstånd). **Etapp 4:**
  Line-programmets fragmentshader tar färgens del (`kGlslPaintSrc`) och
  multiplicerar med `paintColor(vPpos)` och opaciteten; locations
  `linePaint0Loc_`/`linePaint1Loc_` och `uRamp` på enhet 0 kontrolleras
  som de andra (en optimerad bortfallen uniform ⇒ programmet byggs inte).
  `drawMesh` skickar Fill och Glyph till sina filer överst, som Image.
  **T2 (2026-10-10):** D3D11:s skarvar, ändar och streck i GLSL — `aDist`
  attribut 6 (sju attribut), `uStroke` + `uDash[2]` med kontrollerade
  locations (`lineStrokeLoc_`/`lineDashLoc_`).
- **`linux_fill.cpp`** (CANVAS_2D etapp 4) — Fill-programmet, spegel av
  d3d11_fill.cpp: egna uniformer och maskgeneration (`fillClipGen_`),
  maskernas läge ur ett PERSPEKTIVKORREKT `vPixH` = (klipp.xy, klipp.w)
  delat i fragmentshadern (rätt också där klipparen skurit mot närplanet),
  slag + sida som heltal (`uvec4`). `setPaint(d, loc0, loc1)` — färgens
  uniformer, rampen (eller vit) på enhet 0 och ALLTID
  `bindSampler(Linear)` — delas med Line och Glyph.
- **`linux_glyph.cpp`** (CANVAS_2D etapp 5) — Glyph-programmet, spegel av
  d3d11_glyph.cpp (REQ3_RITYTAN: storleken klampas till `uPaint1.x`/`.w` =
  golvet och taket, BLINT): flaggorna som heltal (`uint`) → `flat int`,
  maskernas läge `noperspective vPix` (quadens hörn delar ankarets w och ett
  ankare bakom närplanet gallras helt), atlassidan på enhet 0 med
  `bindSampler(Linear)`. **Etapp 9:** synligheten på ENHET 1 (`uLabels`,
  `texelFetch` i vertexshadern — samplern spelar ingen roll), indexet som
  heltalsattribut 9, basen i `uParams.w` (exakt under 2²⁴); enhet 0 görs
  aktiv igen efter bindningen, som resten av renderern förutsätter, och en
  saknad `uLabels`-location bygger inte programmet. **Bildmarkören:**
  vertexshadern läser `uPaint0.w` (proportionerna) och `uPaint1.z`
  (`sceneImageFlags` — GL:s mål är speglade) — samma uniformer som
  fragmentshadern, deklarerade i båda stegen.
- **`linux_text.cpp`** — `FtFont`: fontconfig-match ("sans-serif"/
  "monospace" + `:bold`/`:italic` → fil + index; katalogen med närmaste
  stil per familj är fallback) + FreeType (`FT_LOAD_RENDER|TARGET_LIGHT` →
  R8-atlas, samma packning som d3d11); mätning via `linearHoriAdvance`
  (rasteriserar aldrig, mutable cache). **Etapp 5:** rasteriseringen är EN
  funktion (`rasterize`, raderna kopierade täta — en negativ pitch vänds)
  som atlasen och `rasterGlyph` delar, och `readStyle()` läser
  `style_flags` och OS/2-vikten (≥ 600 = fet, `FT_TRUETYPE_TABLES_H`) —
  fontconfig svarar alltid med NÅGOT, så stilen är vad filen är.
  **GLYFRESERVEN** (CANVAS_TEXT_OCH_BILD A4, BLINT — `doc/os/linux/TESTS.md`;
  stänger 🐛 51 — Noto Sans saknar − och ≈): fonten minns sin fil, sitt
  index och sitt fontconfig-MÖNSTER (`pattern_` — `loadFamily`s eget, eller
  för `load(path)` "monospace"/"sans-serif" efter `FT_IS_FIXED_WIDTH` i
  filens stil). En kodpunkt fonten saknar söks bland mönstrets SORTERADE
  kandidater (`candidates`: `FcFontSort` med trim = FcFalse — en trimning
  tar bort fonter vars tecken redan täcks, alltså just reserverna —, byggd EN
  gång per mönster och process bakom en mutex, icke-skalbara fonter och
  färgfonter (`FC_COLOR`) bortvalda, teckenuppsättningen kopierad per
  kandidat): den första vars `FcCharSetHasChar` har den, som inte är fontens
  egen fil och index, som går att ladda i fontens pixelstorlek och där
  `FT_Get_Char_Index` hittar glyfen. Reserverna är `Reserve{face, fil,
  index}` i `reserves_`; en som inte gick att ladda står kvar utan face, så
  den aldrig prövas igen. Regeln (egen → reserv → U+FFFD → .notdef,
  styrtecken aldrig ur reserven) är `resolveGlyph` i `../font_fallback.h`,
  och `resolve(cp)` cachar `{GlyphRef, stegbredd}` per kodpunkt — mätningen,
  atlasen och `rasterGlyph` läser den. Stegbredden räknas på EN väg för
  facen (`advanceIn`: `FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP`,
  `linearHoriAdvance`), och `rasterize(face, gi, …)` sätter den FÖRE
  renderingen. `unload()` stänger reservernas faces före
  `FT_Done_FreeType`. fontconfigs förval `sans-serif` står kvar (Eriks val i
  planens §3: reservkedjan och inte ett annat förval).
- **`linux_image.cpp`** — libpng åt båda hållen (rad 0 överst, rak alfa =
  fasadkontraktet). **AVKODNINGEN är det FULLA API:t** sedan 🐛 66 (BLINT,
  doc/os/linux/TESTS.md): förenklade API:t räknade om 16-bitars RGBA som
  linjärt ljus med förmultiplicerad alfa. `avkodaPng` (ur minnet; `loadImage`
  läser filen först) — `png_set_scale_16` (vår kodeks rundning), palett/grått
  → RGB, tRNS → alfa, fyllnadsalfa, interlace, INGEN gamma — med setjmp i
  `läsMedVakt`, vars ram bara bär triviala objekt (vektorerna och felbufferten
  i anroparens). Kodaren är förenklade API:ts `PNG_FORMAT_RGBA`.
  `decodeImage` (trådsäker, BARA PNG — signaturen prövas först med samma logg
  som D3D11 och Metal, `../image_util.h`) och `loadImage` går genom
  `createImage`. **`GlImage`** bär format, premultiplikation och `flippedV`.
  **Image-programmet** bor här: egna uniformer och egen maskgeneration
  (`imageClipGen_`), z omräknat till [−w, w], och fragmentets läge för
  maskerna ur ett PERSPEKTIVKORREKT (x, y, w) som delas i fragmentshadern —
  inte ur `gl_FragCoord` (y-flippfällan) och inte ur ett noperspective-läge
  räknat i vertexshadern, som hade blivit skräp för hörn bakom kameran.
  **Den PROJEKTIVA vikten** (CANVAS_TEXT_OCH_BILD B1, BLINT): attributet
  `aQ` på plats 2 (`ImageVertex::q`, 1 float vid `offsetof(q)` — stride
  20), vertexshadern skickar `vUvq = (u·q, v·q, q)` med v speglat FÖRE
  multiplikationen, och fragmentshadern samplar `vUvq.xy / vUvq.z`. Plats 2
  ska vara påslagen i Image-ritningens VAO-inställning precis som 0 och 1;
  q = 1 är den gamla vägen exakt. **FÄRGJUSTERINGEN** (REQ8 3, BLINT):
  uniformen `uAdjust` (`sceneImageAdjust`, location-kontrollen i init) och
  `glaze_colorAdjust` ur `kGlslAdjustSrc` (gl_common.cpp — samma text i de
  egna programmens förspel) på den RAKA färgen före opaciteten.
- **`linux_target.cpp`** (CANVAS_2D etapp 6) — `GlRenderTarget` (FBO +
  RGBA8-textur; bilden är en premultiplicerad, `flippedV` GlImage — FBO:n
  ritas med samma vertexshader, så bildens överkant hamnar på v = 1 och
  ritvägarna speglar tillbaka) och `bindTarget` (FBO, glViewport, målets
  storlek). Ett mål som skapas mitt i en frame rensas med sin FBO bunden och
  binder tillbaka `currentFbo_` efteråt. Djupet, MSAA, `supportedSamples`
  och `readTarget` (CANVAS_3D etapp 0): se 3D-avsnittet nedan.
- **`wayland_display/window.cpp`** — registry/globals (versionstak),
  seat-input (xkbcommon + compose; EGEN key-repeat via timerfd; hjul via
  axis_value120→diskret→värde/15, vertikalt negeras), **STYRPLATTAN (TODO
  53):** `axis_source` finger/continuous = PRECISA hjulet (ytenheter →
  fysiska px ÷ FULLA skalan ÷ `kPrecisePxPerRow`; Begin/Update, End utan
  rader vid `axis_stop`, leave eller tappad pekare); `zwp_pointer_gestures_v1`
  (v2; pinch-objektet när BÅDE globalen och pekaren finns) = nypningen — två
  fingrar, skalan som kvot (protokollets är absolut), rotationen medurs rakt
  av; saknad global loggas (WSLg). **Modifierarna** (CANVAS_3D etapp 11):
  musen, hjulen och nypningen bär `mods_` (senaste `wl_keyboard.modifiers`),
  `wl_keyboard.leave` NOLLAR den (protokollet nollställer läget, nästa enter
  följs av `modifiers` — utan raden bar pekaren över ett ofokuserat fönster
  det Ctrl som hölls när fokus gick), och tangenteventet får läget EFTER sig
  med `xkbModsAfterKey` — `modifiers` kommer efter `key`. Cursortemat per
  hit-roll och per klientform (`setCursor`, post 15), data-device-klippbordet (ägd text
  kortsluts — pipe-läsning av egen selection vore deadlock; främmande läses
  med poll-timeout), xdg_toplevel/popup + configure-ack-dansen,
  **fokus-kontraktet** (fasadens regel i window.h: popup + ägare är EN
  fokusgrupp utåt — `xdg_popup.grab` ger popupen tangentbordsfokus, så
  `wl_keyboard.leave` hålls inne tills nästa `enter` är känd och paret
  SLÄPPS när det bara rör fönstret och dess egen popup; annars stänger
  `App::closePopups` listan i samma andetag som den öppnas. Ingen matchande
  enter i batchen = riktig fokusförlust, flushas i postpoll),
  fractional-scale+viewport (annars buffer_scale), `beforeSwap`-pacingen
  (frame-callback i en EGEN `wl_event_queue` med tidstak — fallgropen står
  i rot-CLAUDE.md), decoration-managern. Ytenheter = logiska px ×
  portal-textskalning × userScale; buffert = ytenheter × monitorskala;
  desc-LOGISKA storleken lagras (`descW_/H_`, uppdateras vid resize) så
  textskalebyte i DRIFT räknar om ytenheter/buffert utan att fönstret
  hoppar. **Skalor:** heltalsfallbacken tar max av FÖNSTRETS outputs
  (`wl_surface.enter/leave` → `surfaceOutputs_`; `global_remove` städar —
  blandad DPI), fractional-scale vinner där det finns.
  **STARTLÄGET OCH PLATSEN** (FONSTER_OCH_TIMER, BLINT): `applyStartState()`
  gör `xdg_toplevel_set_maximized` FÖRE det initiala committet, EN gång — i
  init för ett synligt fönster, annars i `setVisible` (dold start) före
  återcommitten; Minimized loggas en gång och startar normalt (xdg-shell kan
  varken fråga eller ta bort minimering). `placement()` = `{0, 0, descW_,
  descH_, maximized_ ? Maximized : Normal, hasPosition false}` — `descW_/H_`
  står still under maximeringen. **Pekarna:** `ClosedHand` = `grabbing`/
  `closedhand`/`fleur`/`all-scroll`, `Wait` = `wait`/`watch`/`progress`/
  `left_ptr_watch`, och `Hidden` = vaktposten `glaze:hidden`, som
  `applyCursorNamed` ser och svarar med `wl_pointer_set_cursor(…, NULL, 0, 0)`
  ("If surface is NULL, the pointer image is hidden").
  **xdg_activation-raise:** requesten SPÅRAS i displayen — dör target-ytan
  förstörs token+request (aldrig aktivering av död proxy); done slår upp
  att requesten lever. **Klippbordet:** `weOwnSelection_` sätts BARA med
  giltig input-serial (serial 0 nekas — annars kortsluter clipboardText
  till fel sträng; följden att ett DOLT fönster i en grind aldrig äger
  urklippet är därför avsiktlig, och `textarea_gate` hoppar över sina
  urklippsfall här); DnD-offers förstörs direkt i enter (ingen DnD); dtorn
  river offer/seat-enheter (release)/outputs/globals i omvänd bindordning.
- **SKÄRMENS mm på Linux** (CANVAS_2D etapp 2) — fyra av de sex
  implementationerna: **Wayland** sparar varje outputs `geometry` (mm, 0 =
  okänt, märke/modell) och AKTUELLA `mode` i `OutputInfo::metrics`, och
  `done` låter fönstren mäta om; fönstret mäter den ÄLDSTA output ytan står
  på (före första enter displayens enda — med flera vet vi inte vilken:
  Assumed). Bufferten ANTAS lika tät som panelen (sant med fractional-scale;
  i heltalsvägen hos en fraktionell kompositör går förhållandet inte att
  veta — planens skrivna risk). Id = märke + modell. **X11** läser RandR:s
  aktiva crtc:er EN gång i `X11Display::readScreens` (vid start och vid
  `RRScreenChangeNotify`, som väljs på roten) med utgångens mm och id (EDID-
  egenskapen, annars utgångens namn); fönstret slår upp crtc:n dess MITT
  står på (`XTranslateCoordinates` — WM:ens ram gör egna koordinater
  föräldrarelativa) vid init och `ConfigureNotify` — inte vid skalbyte,
  varken på X11 eller Wayland: inget av måtten eller reserven beror på
  skalan sedan REQ2_RITYTAN B4. Kräver RandR
  1.3 (`XRRGetScreenResourcesCurrent` frågar inte hårdvaran) och
  pkg-config-modulen `xrandr` (E7). **Headless** svarar Assumed med orsaken,
  **proxyn** det inre fönstrets svar (display_gate F2).
- **`x11_display/window.cpp`** — XOpenDisplay/atom-cache, core-events +
  xkbcommon (keymap ur rotens `_XKB_RULES_NAMES` + new_from_names —
  AVVIKELSE: xkbcommon-x11 saknade dev-paket; repeat = detectable
  autorepeat), selections-klippbordet (INCR = logg + begränsning), EWMH
  (`_NET_WM_MOVERESIZE`/`_NET_WM_STATE`/`_NET_WORKAREA`/`_MOTIF_WM_HINTS`),
  popups = override-redirect + flip/klamp HÄR (X11 har skärmrymd),
  uiScale = Xft.dpi/96 × userScale (dpi:n bär redan GNOME:s textskalning
  — portalfaktorn multipliceras INTE in igen; alla logisk↔fysisk-
  omräkningar går via fönstrets fulla `uiScale()`). **Skalbyte i drift:**
  PropertyChangeMask på root; `RESOURCE_MANAGER`-PropertyNotify ⇒ läs om
  Xft.dpi (FÄRSKT via XGetWindowProperty — XResourceManagerString är bara
  init-cachen) ⇒ fönstren resizas ur lagrad logisk storlek +
  WindowDisplayScaleChanged. Vsync-swap (interval 1) fryser inte skymt
  under XWayland — stryps till ~1 frame/s (uppmätt; pumpen avancerar).
  **XINPUT 2 (TODO 53, `xi` ≥ 1.8):** vid XI ≥ 2.1 tar XI_Motion/
  XI_ButtonPress/Release (≥ 2.4 + XI_GesturePinch*) över kärnans
  pekarmasker (inträde, utträde och tangenter stannar). Rörelse bara med
  valutor 0/1 (kärnregeln). STYRPLATTOR (XIGestureClass eller "libinput
  Tapping Enabled") rullar precist ur sina ACKUMULERADE skrollvalutorer per
  slav (px ÷ fulla skalan ÷ `kPrecisePxPerRow`, fas None) och deras
  emulerade knapp 4–7 kastas; MUSHJUL rullar med knapp 4–7 som förut — en
  valutorbaslinje hade ätit första klicket efter varje enter. **Hjulen och
  nypningen bär `mods.effective`** (knapp 4–7 knappens mask) sedan CANVAS_3D
  etapp 11, och ett TANGENTevent får läget EFTER sig: kärnans `state` är
  läget före, så `xkbModsAfterKey` lägger på modifierartangentens bit.
  `ungrabPointer` = `XUngrabPointer` + `XIUngrabDevice`.
  **FÖNSTRETS PLATS OCH LÄGE** (FONSTER_OCH_TIMER, BLINT): EN skrivare av
  `WM_NORMAL_HINTS`, **`writeNormalHints()`** (förr `applySizeLimits`):
  `XSetWMNormalHints` skriver HELA strukturen, så min/max, `PWinGravity =
  StaticGravity` (klientens position ÄR positionen, ICCCM) och
  `USPosition|PPosition` (när en plats är begärd) måste skrivas tillsammans
  — en andra skrivare hade raderat den första. Normalpositionen
  (`trackNormalPosition`, `XTranslateCoordinates` till roten) spåras i
  `onConfigure` FÖRE returen för oförändrad storlek och bakom samma vakt som
  normalstorleken — inte maximerat, ingen återställningsskuld, inte
  minimerat (Codex-fynd 4). `readNetWmState` läser också
  `_NET_WM_STATE_HIDDEN` → `minimized_`. Startläget före första map:
  Maximized = `_NET_WM_STATE_MAXIMIZED_VERT|HORZ` på det OMAPPADE fönstret
  (hjälparen `setUnmappedNetWmState`, som `applyTopmost` också använder),
  Minimized = `WM_HINTS.initial_state = IconicState` (återställt vid nästa
  map). `requestedTopmost_` sätts nu i init — `desc.topmost` nådde aldrig
  fönstret förut. `placementScreens()` (displayen) = RandR-crtc:erna snittade
  med `_NET_WORKAREA` (`readWorkarea`, flyttad hit ur fönstret), primär via
  `XRRGetOutputPrimary` i `readScreens` (annars skärmen vid origo), skalan
  uiScale × användarskalan. **Pekarna:** `XC_watch` för `Wait`, och
  `ClosedHand`/`Hidden` som pseudo-nycklar ≥ 1000 i `cursors_`
  (`makePixmapCursor`: handen ur `../cursor_bitmaps.h` som XBM-källa + mask,
  LSB först, svart förgrund och vit bakgrund, skalad med uiScale; den dolda en
  tom 1×1-pixmap).
- **`xkb_keys.h/.cpp`** — xkb → `gfx::Key`, **DELAD av båda
  Linux-drivrutinerna** (APP_API B3, 2026-08-13) — EN tabell, inte en per
  drivrutin. Kontraktet (och varför numpaden är undantaget) bor i
  `gfx/events.h` vid `enum class Key` och implementeras här: **numpaden ur
  tangentbordets FAKTISKA läge**, allt annat ur **level 0**
  (`xkb_keymap_key_get_syms_by_level(..., 0)` med
  `xkb_state_key_get_layout` — per-tangent-grupper respekteras).
  Den PRODUCERADE symbolen (`xkb_state_key_get_one_sym`) används
  fortfarande för compose/TextInput — det är TECKNET, en annan fråga.
  **`xkbTypesText(state, keymap, keycode)`** (REQ3_LAYOUT_FONSTER B20-2,
  BLINT): false när Ctrl, Alt (Mod1) eller Logo (Mod4) är aktiva och inte
  konsumerade av tangenten — en kommandokombination skriver ingen text (som
  på Windows och macOS; Alt+a och Ctrl+1 skrev förr), AltGr (Level3) gör det.
  `emitKey` (Wayland) och `handleKey` (X11) frågar den FÖRE compose, räknar
  texten FÖRE KeyDown köas och köar sedan KeyDown med texten (`Event::text`)
  och dess TextInput — i den ordningen. Grind: numpad_gate X2.
  Sedan TODO-post 31 ger det faktiska läget också numpadens
  NAVIGATIONSROLLER som egna poster (se "Navigationsklustret" nedan).
  **`xkbModsAfterKey(before, sym, pressed)`** (CANVAS_3D etapp 11) — en
  modifierartangents eget event bär läget EFTER sig (kontraktet vid
  `kModCtrl`), men X11:s `state` och Waylands `mods_` är läget FÖRE:
  Ctrl/Shift/Alt(Meta)/Super lägger till sin bit vid trycket och tar bort
  den vid släppet, allt annat lämnar masken orörd; AltGr och Hyper räknas
  inte. Släpps den ena av två hållna Ctrl försvinner biten till nästa event.
  De två funktionerna är den enda backendkod en grind når DIREKT:
  `tests/numpad_gate.cpp` och `tests/trackpad_gate.cpp` del B inkluderar
  `xkb_keys.h` och översätter mot RIKTIGA keymaps ur systemets
  xkeyboard-config (`evdev`/`pc105`/`us`, trackpad_gate också `se` för
  AltGr; NumLock = `Mod2` låst/olåst) — så filen ska fortsätta vara fri från
  drivrutinstillstånd.
- **`protocols/`** — committad wayland-scanner-utdata (xdg-shell,
  xdg-decoration-unstable-v1, fractional-scale-v1, viewporter,
  xdg-activation-v1, pointer-gestures-unstable-v1, **xdg-toplevel-icon-v1**
  — PROGRAMIKON, ur wayland-protocols 1.44 `staging/`, wayland-scanner
  1.23.1) — ALDRIG ur bygget. En ny protokollfil står i CMakeLists linux-block
  OCH i `wayland_backend.h`; glöms `.c`-filen länkar bara exe:erna fel.
- **PROGRAMIKONEN** (PROGRAMIKON, 2026-10-11). **X11** (`x11_window.cpp`):
  `applyClassHint` (WM_CLASS res_name = res_class = `gfx::appId()`) och
  `applyIcon` (`_NET_WM_ICON`: per ram bredd, höjd och 0xAARRGGBB med RAK
  alfa i `unsigned long` — format 32 är `long` i Xlib; ramar ≤ 256 px
  stigande, kapade så hela propertyn ryms i `XExtendedMaxRequestSize`) körs
  FÖRE `XMapWindow`; tom lista ⇒ `XDeleteProperty`. **Wayland**
  (`wayland_window.cpp`): app_id = `gfx::appId()` (förr hårdkodat
  `se.glazeui.app`); `applyIcon` bygger EN memfd-pool med varje KVADRATISK
  ram som förmultiplicerad ARGB8888 (BGRA i minnet), skapar buffertarna,
  släpper pool/mappning/fd direkt, och sätter `create_icon` + `add_buffer`
  (skala 1) + `set_icon`; den nya sätts FÖRE den gamla rivs (objektet först,
  buffertarna sedan — protokollets krav), och commit görs bara efter
  configure-handskakningen. Utan `xdg_toplevel_icon_manager_v1` (GNOME,
  Weston/WSLg): EN loggrad per process och `iconApplied()` false. Globalen
  binds utan lyssnare (`icon_size` gäller skalbara ikoner). Popups: ingen
  toplevel, ingen ikon. **Headless**: tyst no-op. **Proxyn**: lagrar alltid,
  vidarebefordrar efter `init()`.
- **UTSKRIFTEN — `linux_print.cpp` är en LÄSBAR DEGRADERING** (REQ8 10,
  2026-10-14): `printAvailable()` false, dialogen, standardskrivaren och jobbet
  vägrar TYST (`glaze::PrintJob` loggar en gång per process). Vägen dit står i
  filhuvudet: XDG-portalen `org.freedesktop.portal.Print` med en egen PDF/PS,
  eller CUPS.
- **SYSTEMFÄLTET — `linux_tray.cpp` är en KOMMENTERAD DUMMY** (SYSTEMFÄLTET,
  2026-10-12; Erik: Mac/Windows only, för nu). `trayAvailable()` är false,
  `setTrayIcon` och `trayIconRect` svarar false och `removeTrayIcon`/
  `setTrayTimer` är no-op — allt TYST, för `glaze::TrayIcon` loggar en gång
  per process vid `show()`. Filhuvudet bär vägen dit: StatusNotifierItem över
  D-Bus mot `org.kde.StatusNotifierWatcher` (KDE; GNOME bara med
  AppIndicator-tillägget) med vår egen popup som meny (ingen dbusmenu);
  XEmbed (`_NET_SYSTEM_TRAY_S0`) är X11-only och utgående. `applyShowInDock`
  loggar false en gång (begäran lagras i ../platform.cpp). **`WindowDesc::showInTaskbar ==
  false`** är också en dummy: `ProxyWindow::init` loggar den EN gång per
  process — ett ställe för Wayland, X11 och headless — och `desc` går orörd
  vidare; kommentarer i `wayland_window.cpp` (xdg-shell saknar formen) och
  `x11_window.cpp` (`_NET_WM_STATE_SKIP_TASKBAR` är inte byggt) säger det.
  Dold start bygger på fasadens löfte att `setVisible(true)` tar fram ett
  fönster skapat med `desc.hidden` — och sedan TODO 80 går VARJE fönster utom
  popuperna den vägen (skapat dolt, visat efter `onLoad`). **Wayland (B80-4,
  BLINT):** init tillämpar startläget och gör den initiala committen också
  för en dold yta, och den FÖRSTA `setVisible(true)` på en konfigurerad,
  aldrig mappad yta (`firstMapPending_`) gör ingen ny configure-runda —
  nästa ritning mappar den med sin första buffert (Mutter skickar den
  första configuren EN gång). En återvisning efter `hide()` gör rundan som
  förut — men kommer ingen configure inom tidsgränsen (WSLg:s Weston svarar
  inte på den nya initiala committen) gäller den FÖRRA och ytan mappas med
  nästa ritning, som i `awaitStateBeforeMap`, i stället för att bli dold för
  gott. `ProxyWindow::renderer()` svarar utan inre fönster (före init, efter
  `shutdown()`) med en `NullRenderer` — en stängd `AppWindow` bygger Ctx när
  ett lånat barn lossas efter stängningen. `maximize`/`restore` före första visningen väntar in configuren
  (`awaitStateBeforeMap`), en minimerad start loggas. **X11 (B80-3/5,
  BLINT):** `minimize`/`maximize`/`restore` på ett aldrig mappat fönster
  skriver dess `_NET_WM_STATE`/`WM_HINTS` (`setUnmappedStartState`), och
  `setPosition` före första mappningen sätter `USPosition`
  (`writeNormalHints`). "Aldrig mappat" gäller DET OS-FÖNSTRET: init nollar
  `mapped_`/`everMapped_`/`minimized_` och hintsens bokföring, för objektet
  lever vidare över `shutdown()` och ett återöppnat dolt fönster är ett nytt
  (Codex-granskningen). **`raise()` på ett noActivate-fönster** (TODO 87):
  X11 bara `XRaiseWindow` (ingen `_NET_ACTIVE_WINDOW`), Wayland ingenting
  (xdg-shell lyfter bara genom aktivering). Rader i `doc/os/linux/TESTS.md`.

## Storleksmarginalen utanför ytan (DOCKNING_DRAG E2L, 2026-10-13 — BLINT)

Eriks VAL 1 (b): ramen ska ligga UTANFÖR den synliga kanten också på Linux,
som Windows osynliga ram, så innehållet vid kanten (verktygsfältet mot
vänsterkanten) är klickbart ända ut. libdecor/GTK:s form. Rader i
`doc/os/linux/TESTS.md`; grind för kantvalet: geometry_gate KM (KL för X11:s
placering).

- **YTAN = klientytan + 2m**, genomskinlig i marginalen. `LinuxWindow::
  frameMargin_` är marginalen i fysiska px (0 = ingen), och ALLT som talar
  klientytan står kvar: `pixelSize()`, `contentArea`, hit-test-kartan,
  `captureFrame`. Marginalen bredd: `frame::kMarginLogical` (8 logiska px,
  `../frame_margin.h`).
- **RENDERAREN** (`GlRenderer::setFrameMargin`): retained-FBO:n är
  klientytans storlek som förut; `composite()` rensar HELA backbuffern till
  (0, 0, 0, 0) och lägger FBO:n m px in genom att förskjuta VIEWPORTEN
  (`bbOrigin_`) — allt som ritas efter compositen (chromet, overlayen) talar
  klientens px. `applyClip` lägger `bbOrigin_` på scissorn (höjd − bottom +
  m), `bindTarget(nullptr)` återställer förskjutningen i backbufferfasen
  (`backbufferOrigin()`), ett render-mål har 0. Det degraderade läget (ingen
  FBO) ritar med förskjutningen men marginalen får rensfärgen. Compositens
  quad ritas UTAN klipp, och klippstacken tillämpas igen efter den
  (`Renderer::reapplyClip`, nu med `bbOrigin_`) — en scissor satt i FBO:ns
  koordinater hade legat m px fel (Codex-fynd).
- **TRÄFFPROVET** (`LinuxWindow::hitRoleAt`, en sanning för pilen och
  trycket): en punkt i marginalen ger sin kant ur `frame::marginRole` (hörnet
  16 logiska px längs kanten), och kartans resize-zoner INNANFÖR klientytan
  hoppas över medan marginalen finns. Utan marginal läses kartan som förut.
- **WAYLAND** (`wayland_window.cpp`): `marginSurf_` (ytenheter) räknas i
  `applySize` ur läget (`wantedMarginSurf`: toplevel, Custom, inte fast,
  inte maximerat/helskärm/tiled — tiled-lägena läses ur configuren, v2),
  bufferten är `physW_ + 2m`, viewportens destination hela ytan, och
  `xdg_surface_set_window_geometry(m, m, b, h)` — satt för varje toplevel,
  också (0, 0, b, h), för en satt geometri kan inte tas bort. Configurens
  storlek, `set_min/max_size` och en popups positioner-ankare gäller
  fönstrets geometri = klientytan, så de är orörda. Opaka regionen = bara
  klientytan. `onSurfaceConfigure` sätter LÄGET (maximerat, helskärm, tiled)
  FÖRE `applySize`; händelsen om maximeringen går ut efter storlekens som
  förut. Pekarens POSITIONER går genom `clientX/Y` (ytans koordinater − m);
  `toPhysical` är kvar för sträckor (hjulets deltan). `setSizeLimits` som
  byter marginalen gör `applySize(…, true)` och committar gränserna med nästa
  frame (`applySizeLimits(false)`) — en egen commit hade lagt den nya
  geometrin över den gamla bufferten. Samma regel för skalbytena i drift
  (Codex-fynd): `onTextScaleChanged` och `userScaleChanged` (utom maximerat,
  där ingen ny buffert är på väg) committar gränserna med nästa swap.
- **X11** (`x11_window.cpp`, `x11_display.cpp`): bara där X-sessionen kan
  bära marginalen (`X11Display::frameExtentsUsable` — ARGB-konfig, en
  ägare av `_NET_WM_CM_S<skärm>` och `_GTK_FRAME_EXTENTS` i `_NET_SUPPORTED`,
  GTK3:s regel; frågad i varje init). `marginCapable_` + `marginPx_` (räknad
  EN gång i init:s skala — ett skalbyte i drift ändrar inte greppets px).
  Fönstret skapas då med ARGB-visual och -konfig också när det inte är
  transparent (`transparent_` styr bara compositens blandning). X-fönstret =
  klient + 2m med origo −m; `physW_/H_`, `normalX_/Y_`, `setPosition`,
  `clientToScreen`, `trackNormalPosition`, `measureDisplay` och
  `writeNormalHints` (storlekarna + 2m, positionen −m) talar klientytan;
  `onConfigure` drar av 2m FÖRST. `_GTK_FRAME_EXTENTS` (CARDINAL[4]) skrivs
  före första XMapWindow och vid varje byte (också nollor).
  `applyFrameMargin()` byter marginalen BARA vid maximering (⇒ 0, och WM:en
  anpassar X-fönstret själv — mutter behåller den synliga ramen); en
  `WindowExposed` ber om en ny bildruta. **Fast ↔ ändringsbart byter ALDRIG
  geometrin** (Codex-fynd): att flytta X-fönstret runt den oförändrade
  klientytan (förr `XMoveResizeWindow` i `setSizeLimits`) tävlade med WM:ens
  asynkrona configure, och snabba byten kunde lägga fönstret fel. Ett fönster
  som STARTAR fast får därför ingen marginal (den genomskinliga marginalen
  hade ätit klick — X11 släpper inte input genom genomskinliga pixlar, och vi
  sätter ingen inmatningsform), och ett som BLIR fast i drift behåller den
  men INERT (`LinuxWindow::frameResizable_` — träffprovet ger Client där).
  En maximerad start börjar utan marginal. Displayen drar av `frameMargin()` från VARJE
  pekarväg (kärnan, XI2, inträdet, skrollvalutorerna, nypningen) — trycket
  med golv, inte trunkering. `hasArgbConfig()` ersatte en kontroll som
  aldrig kunde slå till (`eglConfig(true)` faller tillbaka och är aldrig
  nullptr).
- **INGEN MARGINAL**: popuper, Native/None, ett fast fönster (Wayland alltid;
  X11 när det STARTAR fast — ett som blir fast i drift behåller den, inert),
  maximerat (båda), helskärm och tiled (Wayland),
  och X11 utan kompositör eller utan WM-stöd — där gäller kartans zoner
  innanför klientytan som före E2L. Kompositören som startar eller slutar
  MITT i ett fönsters liv följs inte (beslutet tas i init) — skriven gräns.

## Fasaden för levande drag (DOCKNING_DRAG E3, 2026-10-13 — BLINT)

Kontrakten står vid `WindowDesc::showInactive`, `Window::captureMouse` och
`EventType::MouseCaptureLost` i `gfx/`; grinden `tests/capture_gate.cpp`
kör bara sitt C-block här (ingångens bokföring — D-blocket är D3D11).

- **`showInactive`** (första visningen aktiverar inte): **X11** sätter
  `_NET_WM_USER_TIME` = 0 (CARDINAL, format 32) före första `XMapWindow` —
  EWMH:s "ta inte fokus vid mappningen", också för en dold start
  (`userTimeZero_`); egenskapen TAS BORT vid nästa `setVisible(true)` efter
  första mappningen, så en senare visning aktiverar som vanligt. **Wayland**:
  dokumenterad no-op (xdg-shell ger klienten inget val; kompositören
  bestämmer fokus). **Headless**: inget att visa. **Proxyn** skickar `desc`
  orörd till det valda fönstret — ingen egen lagring.
- **`captureMouse()`** och **`hasMouseCapture()`** svarar `false` i Wayland-,
  X11- och headless-fönstren: den implicita grabben ger redan hela trycket
  till fönstret där det började, oavsett fokus. Proxyn vidarebefordrar båda
  efter `init()` och svarar false före.
- **`MouseCaptureLost`** (REQ2_INMATNING B5, BLINT): **Wayland** skickar den
  när `wl_pointer.leave` kommer medan ett LEVERERAT tryck hålls —
  `pushedButtons_` får knappens bit EFTER `pushMouse` (ett tryck som
  `interceptPress` gav kompositören räknas inte) och tappar den vid släppet,
  FÖRE frågan om ett fönster under pekaren. Leave PARKERAR fönstret i
  `pendingCaptureLost_`, och flushen (`flushPendingCaptureLost` — prepoll,
  postpoll, `roundtrip` och nästa `enter`) skickar det; är nästa enter i samma
  dispatch-omgång en yta i samma FOKUSGRUPP (`sameFocusGroup` — en popup som
  trycket öppnade, GTK:s undantag, wayland#280) stryks det. Sätet som tappar
  pekaren gör som leave, och en riven yta nollar posten. **X11** skickar den
  aldrig (ingen annan klient kan ta en aktiv grab mitt i vår implicita;
  `FocusOut` bär resten), och **headless** har ingen pekare.

## 3D-delen (CANVAS_3D etapp 0 — djup, MSAA och renderläget)

Kontrakten står i [../../include/gfx/CLAUDE.md](../../include/gfx/CLAUDE.md);
grind: G9 `tests/scene3d_pixel_gate.cpp` (Wayland och X11).

- **LOADERN** (`gl_common`) bär sedan etappen renderbuffertarna och blitten:
  `glGenRenderbuffers`/`glBindRenderbuffer`/
  `glRenderbufferStorageMultisample`/`glFramebufferRenderbuffer`/
  `glDeleteRenderbuffers`/`glBlitFramebuffer` (GL 3.0).
- **MÅLET** (`linux_target.cpp`): utan MSAA sitter texturen i rit-FBO:n och
  djupet (`GL_DEPTH_COMPONENT32F`) är en vanlig renderbuffert; med MSAA är
  färgen och djupet PROVTAGNA renderbuffertar i rit-FBO:n och texturen sitter
  i en egen upplösnings-FBO (`resolveFbo`). `bindTarget` löser upp målet som
  LÄMNAS med `glBlitFramebuffer` (`resolveTarget` — UI:ts läge och scissorn
  av först: blitten lyder scissorn, och somliga drivrutiner färgmasken).
  **Provtagningen:** `supportedSamples` = den närmaste ≤ begärt som
  `GL_MAX_SAMPLES` (init) rymmer — llvmpipe har 4 — och blir FBO:n ändå inte
  komplett (formatets eget tak) prövas HÄLFTEN, ned till ett prov; en lägre
  än begärd loggas en gång, och `RenderTarget::samples` säger vad det blev.
  Ett nytt mål nollar bara BILDEN (i den FBO som bär den) — renderbuffertarna
  rensas av pushTarget. **`readTarget`**: `glReadPixels` ur bildens FBO, och
  raderna VÄNDS (GL läser nerifrån — y-flipp nr 2, som captureRetained),
  sedan `unpremultiplyRgba`.
- **RENDERLÄGET** (`linux_renderer.cpp`): GL:s tillstånd är globalt per
  kontext, så det cachas (`glDepthOn_` … `glBlend_`, -1 = okänt;
  `resetStateCache` i beginFrame) och sätts med fyra sättare — djupet
  (`glEnable(GL_DEPTH_TEST)`/`glDepthFunc`/`glDepthMask`), polygon offset
  (`glPolygonOffset(lutning, bias)` — flyttalsdjupets enhet 2^(e − 23), som
  D3D11 och Metal), gallringen (`glCullFace`; `glFrontFace(GL_CCW)` sätts EN
  gång i buildPipeline — fönsterkoordinaterna har y uppåt som NDC, så CCW är
  moturs för ögat) och blandningen med färgmasken (DepthOnly = `glColorMask`
  av). Varje program anropar **`applyMeshState(d.state)`** före sin
  glDrawElements; djupet verkar bara i ett mål med djup.
- **ÅTERSTÄLLNINGEN FÖRE UI:T OCH FÖRE CLEAR.** `bindUiPipeline` sätter UI:ts
  läge (djup av, ingen offset, ingen gallring, rak alfa, masken på). `clear`
  gör det också INNAN glClear, för glClear LYDER färg- och djupmasken — ett
  scenanrop med DepthOnly eller utan djupskrivning hade annars lämnat målet
  orensat (och djupet rensas med `GL_DEPTH_BUFFER_BIT` + `glClearDepth(1)`
  när målet har ett). UI-läget lämnar djupmasken PÅ med flit. Den
  transparenta compositen binder UI-pipelinen innan den slår av blandningen.
- **`beginOffscreen`** gör kontexten aktuell (UI-programmet och dess läge
  är bundna sedan init); när sista målet poppas binds standard-FBO:n, och
  `clear` rensar ingenting utan ett mål. `endOffscreen` flushar och kör
  `glFlush`.
- **GRAFIKKORTET — `linux_gpu.cpp`** (REQ9_GRAFIKKORTET, BLINT): valet görs
  BARA i `gfx::init`, där `applyGpuEnvironment` körs FÖRST, före
  protokollvalet och därmed före första EGL-anropet (libglvnd väljer
  tillverkare och Mesa kort ur miljön vid det anropet). Regeln står i
  `../gpu_util.h` (`primeEnvironment` över `scanDrmCards` i sysfs — aldrig
  `EGL_EXT_device_enumeration`, som hade laddat tillverkarbiblioteken före
  valet); tillämpningen är `gpu::applyPrimeEnvironment` med `setenv(…, 1)`
  — en TOM variabel räknas som osatt och skrivs över, en icke-tom är
  användarens och rörs aldrig — och namnen vi satte sparas
  (`childEnvironmentRemovals`). `GlRenderer::init` fyller
  `adapterId_` med `eglAdapterId` (EGL_EXT_device_query + device_drm →
  `/dev/dri/cardN` mot sysfs; tomt för llvmpipe) och `reportGpuOutcome`
  loggar ett mål som inte blev av, mjukvarurendering och en begäran ändrad
  efter init. "GL: ritar med X [id] (preferensen Y, kortet Z)" loggas när
  renderaren är en annan än förra (PRESTANDA A1).
- **MÄTPUNKTERNA (PRESTANDA A2, BLINT — aldrig kompilerade):**
  `compileNs` över programbyggena (kompileringen OCH länkningen).
  `presentNs` delas mellan renderaren (swappen i `present`) och
  `LinuxWindow::renderFrame` (`linux_platform.cpp`), som lägger Waylands
  frame-callback-väntan (`beforeSwap`) i renderaren med `addPresentNs`.
  GPU-TIDEN: `loadGlTimerQueries` (gl_common.cpp) laddar
  `glGenQueries`/`glQueryCounter`/`glGetQueryObject*` VALFRITT, en gång,
  med en aktuell kontext (GL ≥ 3.3 eller `ARB_timer_query`; utan dem ritas
  det som förut men mäts ingen GPU-tid, loggat); ringen om `kGpuQueries` (4)
  med två `GL_TIMESTAMP` per frame läses i `beginFrame` med
  `GL_QUERY_RESULT_AVAILABLE` (ingen väntan) och avslutas i `present` före
  swappen; `shutdownGpuQueries` raderar frågorna med kontexten aktuell. P8
  får svara SKIPPED på llvmpipe. Det oprövade står i doc/os/linux/TESTS.md.

## Egna program (CANVAS_3D etapp 1 — `linux_program.cpp`)

Spegel av backend_d3d11:s avsnitt; kontrakten vid `program.h` i
[../../include/gfx/CLAUDE.md](../../include/gfx/CLAUDE.md). Grind: G9 del C,
Wayland och X11.

- **LOADERN** (`gl_common`) bär sedan etappen `glGetUniformBlockIndex`/
  `glUniformBlockBinding`/`glGetActiveUniformBlockiv`/`glBindBufferBase`,
  `glVertexAttribDivisor`, `glDrawArraysInstanced`/`glDrawElementsInstanced`,
  `glDisableVertexAttribArray` och reflektionens `glGetActiveAttrib`/
  `glGetAttribLocation`/`glGetActiveUniform`/`glGetActiveUniformsiv`.
  `compileGlShader` och
  `linkGlProgram` tar ett valfritt `error` som får loggen i stället för
  `log` (ett eget program loggas EN gång, i createProgram).
- **`createProgram`**: källorna i delar — `#version 330 core`, definitionerna
  som `#define`-rader, FÖRSPELET (`layout(std140) uniform GlazeFrame { … }
  glaze;` + `glaze_toClip`, som räknar om z till [−w, w]), `kGlslAdjustSrc`
  (`glaze_colorAdjust`, REQ8 3 — i båda stegen), `#line 1` och appens källa. Mesa (glcpp) följer GLSL 3.30:s regel att raden EFTER
  `#line 1` är rad 1 — uppmätt: "0:7(12): error". Saknas glslVertex ELLER
  glslFragment: "saknar GLSL-källa (Linux)". Länkas UTAN
  glBindAttribLocation (platserna står i källan). Sedan REFLEKTIONEN: de
  aktiva attributen (`glGetActiveAttrib` + `glGetAttribLocation`; ett
  inbyggt har plats −1, en matris tar en plats per kolumn) blir
  `ShaderProgram::inputs` med heltalstyperna i `integerInputs`; varje aktiv
  sampler måste heta `GlazeTexture<i>` med i under `ProgramDesc::textures`
  (en annan hade fått enhet 0 och tyst samplat fel bild); ingen aktiv
  uniform utanför ett block utom samplerna och inga block utöver de två
  (`uniformReflection` — de hade aldrig fått något värde); blocken binds på
  NAMN — GlazeFrame på punkt 3, GlazeUser på 4, och GlazeUser:s
  `GL_UNIFORM_BLOCK_DATA_SIZE` måste vara exakt `userBytes`. Samplerna får
  sina enheter med glUniform1i, vilket kräver programmet bundet: det bundna
  (`GL_CURRENT_PROGRAM`) binds tillbaka direkt, så `pipeline_` aldrig ljuger.
  `GlShaderProgram` raderar sitt program genom renderern (`destroyProgram`,
  fontens livstidsregel).
- **RITNINGEN** (`drawCustomMesh`): EN VAO för alla egna program
  (`customVao_`) — attributen slås på och av efter layouten
  (`customEnabled_`), så en tom layout ritar med en tom VAO (core-profilen
  kräver en bunden). Pekarna sätts per anrop med delens offset — också
  instansdelens, med `glVertexAttribDivisor` 1, för GL 3.3 saknar
  baseInstance — och heltalsformaten med `glVertexAttribIPointer`. Blocken
  skrivs om per anrop (`glBufferData`, orphan) i `frameUbo_`/`userUbo_` och
  binds med `glBindBufferBase`; texturerna på enhet 0… med var sin bilds
  samplerobjekt (som skuggar texturens parametrar), och enhet 0 görs aktiv
  igen. `glDraw{Elements,Arrays}[Instanced]`. Programmet byts när
  löpnumret skiljer (`curProgram_`), och `bindUiPipeline` binder tillbaka
  UI:ts program, VAO och `GL_ARRAY_BUFFER` som för de andra programmen.

## Line3 och Glyph3 (CANVAS_3D etapp 3 — GLAZE_POS3)

- **SAMMA KÄLLA, TVÅ PROGRAM.** Line- och Glyph-källan länkas en gång till
  med `#define GLAZE_POS3 1` före källan (`kGlslPos3Define` i
  linux_scene.cpp, samma rad i linux_glyph.cpp): programmens uniform-
  locations och VAO:er bor i `LineProgram`/`GlyphProgram` (`line_`/`line3_`,
  `glyph_`/`glyph3_`), byggda av `buildLineProgram(pos3, p)` och
  `buildGlyphProgram(pos3, p)` med samma kontroll av varje location (en
  bortoptimerad ⇒ programmet byggs inte). Attributpekarna sätts per anrop
  ur formatets offset (`glyphAttribs<V>` för båda glyfformaten; punkterna och
  ankaret som tre flyttal). Ett POS3-program som inte byggs loggas — då
  ritar bara den varianten inget.
- **VERTEXSHADERN** är HLSL:ns POS3-gren rad för rad (bredden ur den
  lokala skalan, `noperspective vHw`, båda ändarnas halva bredd, den EXAKTA
  förlängningen, `world` falskt och kastade fragment utanför bandet) —
  GLSL:s mat4 indexeras per KOLUMN, så `pxPerWorldAt` plockar raderna ur
  kolumnernas komponenter.
- **KLIPPYTAN** (båda linjegrenarna, se backend_d3d11/CLAUDE.md):
  `trimSegment` håller segmentets ändar innanför när- och fjärrplanet och
  en skyddsram, och `keepInside` ändarnas förlängning innanför klippdjupet;
  ett segment med båda ändarna utanför samma plan, och en rund bit bakom
  närplanet, KASTAS (solfjädern bakom kameran, 0.1.1 — G9 L1).
  **Det var llvmpipe som krävde det:** ett band som GL klipper (en vertex
  bakom närplanet eller bortom det bortre planet) fick sina noperspective-
  värden fel interpolerade — G9 D2 blev en kil som smalnade mot närplanet,
  och G1 tappade en rutnätslinje bortom 3D-målets ändliga bortre plan.
  `GL_DEPTH_CLAMP` hjälpte inte (prövat 2026-10-11). Grindar: G9 D2 och G1
  på Wayland och X11; sabotagen "keepInside borta" och "bara närplanet"
  fäller dem här (D3D11 klipper rätt och är grönt med båda).
- **LIGGANDE TEXT (CANVAS_3D etapp 9, `kGroundAligned`)** — `groundCorner`
  i båda programmen är d3d11_glyph.cpp:s `glyphGround` rad för rad (se
  backend_d3d11/CLAUDE.md): hörnet i världsplanet, "nedåt" vänd så att
  ramen aldrig speglas, skalan vid ankaret vinkelrätt mot blicken. Matrisens
  rader nås genom produkter med enhetsvektorerna, så kolumnordningen spelar
  ingen roll. Klippmaskernas `vPix` är HÖRNETS egen projektion — ett hörn
  bakom ögat får ingen (0, 0; sällsynt för en etikett, skriven gräns). G9
  H5/H5b/H5c på Wayland och X11.
