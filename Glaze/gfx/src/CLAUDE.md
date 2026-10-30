# CLAUDE.md — Glaze/gfx/src

Implementation av **`common_gfx`** — backends bakom render-fasaden i
[../include/gfx/](../include/gfx/). Del av GlazeUI — se
[root /CLAUDE.md](../../../CLAUDE.md) för lagerbilden och de hårda reglerna.
`CMakeLists.txt` (ett steg upp) bygger `common_gfx` av EN backend — auto =
`backend_d3d11` på Windows, `backend_metal` på macOS, `backend_linux`
annars (okänt värde stoppas av rotens FATAL_ERROR-validering). Statiskt
bibliotek som förval; i DELAT läge (`GLAZE_SHARED`, TODO 73) ett
OBJECT-bibliotek vars objekt länkas in i libglaze — BARA `glaze_impl` får
länka det, annars får målet ett eget backendtillstånd (händelsekön,
UI-trådens id, shadercachen) bredvid libglaze:s. Allt processvitt
tillstånd här ligger i cpp-filer, och ska fortsätta göra det
(`tests/dll_gate.cpp`).

## Regeln som är hela poängen

**Backend-API:er (Win32/`ID3D11*`/`IDWrite*`, ObjC/`MTL*`/AppKit,
Wayland/Xlib/EGL/FreeType/fontconfig/libpng)
får BARA förekomma här** — i sin backend-mapp. Utåt syns enbart fasaden.
`*_backend.h`-headrarna ligger i src/, INTE include/, och inkluderas aldrig
utanför sin mapp (metal_backend.h bär ObjC-typer — BARA .mm-filer).

## Struktur

- **`platform.cpp`** — backend-neutralt: `gfx::ticksMs()` (std::chrono,
  monoton) + **användarskalans lagring** (L12/US): `userScale()`/
  `setUserScale` (default 1.0, klampas 0.5–2.0 + logg, NaN/≤ 0
  ignoreras). `uiScale()` per backend multiplicerar in faktorn EXAKT en
  gång: D3D11 `dpi/96 × user`, Metal `backing × user` (PUNKTerna bär
  user), Wayland `monitor × text × user` (ytenheterna bär text × user),
  X11 `Xft.dpi/96 × user`, headless `1 × user`.
  **`userScaleChanged()`-flödet:** ny fysisk storlek ur lagrad LOGISK
  (invarianten), `WindowDisplayScaleChanged` EFTER renderer-resizen —
  D3D11 `SetWindowPos` (WM_SIZE synkront; `pendingScaleResize_`-vakt mot
  logisk tillbakaräkning), Metal `setContentSize` (dito), Wayland
  `applySize` ur `descW_/H_` + event, X11 → `onUiScaleChanged`
  (`ownResizes_`-RÄKNAREN — inte en bool — märker egna XResizeWindow:ar så
  två skalbyten i följd inte låter mellanläget bli ny logisk sanning;
  `scaleEventOwed_` släpps av den SISTA configuren).
  **MAXIMERAT — omskrivet av BUGGFIX_TRE B (2026-08-13), alla fyra
  backends var fel:** alla fyra hoppar nu resizen och pushar bara eventet,
  och den lagrade LOGISKA storleken är fredad från BÅDE maximeringens och
  ÅTERSTÄLLNINGENS rect. Det räcker inte att låta WM:ets configure vinna —
  OS:et återställer rect:en i den skala som gällde vid maximeringen och vet
  ingenting om vår, så tillbakaräkningen gjorde 800×520 logiska till
  640×416 vid ett skalbyte 1.0 → 1.25 (uppmätt på X11 OCH Wayland).
  **På restore-flanken sätter backenden själv om fysisk storlek** ur lagrad
  logisk × AKTUELL skala: D3D11 `SetWindowPos` i `WM_SIZE`:s
  SIZE_RESTORED-gren, Metal `setContentSize` i `onDidResize`, Wayland
  ignorerar kompositörens föreslagna mått i unmaximize-configuren
  (xdg-shell tillåter det — storleken utan maximized/fullscreen/tiled är
  ett FÖRSLAG), X11 en SKULD med väntat mått. Korrigeringen är **ETT
  försök per skuld**: en villkorslös läkning är en oändlig configure-loop
  när WM:en nekar målet (våra egna `WM_NORMAL_HINTS` räcker för att göra
  det ouppnåeligt). Nekas målet accepteras WM:ens storlek och krediten i
  `ownResizes_` lämnas TILLBAKA — annars fastnar räknaren positiv och
  nästa användar-resize klassas som vår egen för alltid.
  **X11:s event-ordning är WM-beroende och får INTE gissas.** Uppmätt under
  Mutter/XWayland kommer `PropertyNotify(_NET_WM_STATE)` FÖRE configuren
  vid maximering men EFTER vid återställning — inte ens samma ordning åt de
  två hållen. TVÅ försök att KLASSIFICERA configuren föll (en "configure
  setts"-flagga kan inte skilja maximeringens configure från
  återställningens, eftersom båda anländer med `maximized_` fortfarande
  true; en geometrisk skiljedom lagrar den maximerade storleken bara i den
  ena ordningen). Designen behöver därför inte VETA vilken configure som är
  vilken: skulden konsumeras SYNKRONT på restore-flanken, målet räknas ur
  den LAGRADE logiska storleken × aktuell skala (aldrig ur den fysiska),
  och en rect som hinner emellan klassas som vägran — då BEHÅLLS logisk
  storlek och krediten lämnas tillbaka. ALDRIG `queryMaximized()` som
  klassificerare: den svarar med propertyns läge NU i stället för läget när
  den behandlade configuren skickades, och lägger en blockerande roundtrip
  på varje interaktiv resize.
  **Bokföringen ligger FÖRE `onConfigure`:s no-op-retur:** en configure med
  oförändrat mått är fortfarande ett svar, och lämnas den obehandlad
  fastnar korrigeringens kredit i `ownResizes_`.
  **GPU-PREFERENSENS OCH KORTETS lagring** (CANVAS_3D beslut 1,
  REQ9_GRAFIKKORTET): `setGpuPreference`/`gpuPreference` och
  `setGpuAdapter`/`gpuAdapter` — backenden läser dem när ett fönster skapar
  sin renderare (D3D11 `chosenAdapter`, Metal `preferredMetalDevice`) och
  Linux EN gång i `gfx::init` (`applyGpuEnvironment`). Kontraktet står i
  `platform.h`; `gpuAdapters`/`childEnvironmentRemovals` bor i backendarna.
  `GpuAdapter::localAdapterId` fylls bara av D3D11 (LUID:en); Metal och Linux
  lämnar det tomt.
  **INTEGRATION MED ETT ANNAT GRAFIK-API** (`gfx/native.h`): `nativeDevice`,
  `importRenderTarget`, `setExternalFramePacing` och `insertCompletion` är
  rena virtuella i `Renderer`; bara D3D11 implementerar dem
  ([backend_d3d11](backend_d3d11/CLAUDE.md)). Metal och GL svarar "stöds
  inte" (`api == None`, ogiltigt mål med feltext, ingen åtgärd, `nullptr` med
  feltext) och `NullRenderer` likadant. `rayCenter` ligger sist i
  GlazeFrame-blocket i alla tre förspelen (256 byte).
  **APPENS ID** (PROGRAMIKON): `setAppId`/`appId` — lagringen, valideringen
  (`[A-Za-z0-9._-]`, ≤ 255, ogiltigt loggas och ignoreras) och förvalet:
  `programName()` normaliserat (varje annan byte blir '_', "glazeui" om
  namnet inte går att läsa). Läses av Linux-backenden när ett fönster skapas.
- **`icon_util.h`** (PROGRAMIKON, 2026-10-11) — backend-neutralt och internt:
  det de sex `setIcon`-implementationerna delar. `iconImageValid` (mått > 0,
  exakt w × h × 4 byte), `iconImageSide` (den längre sidan) och
  **`pickIconImage(icon, mål)`** — VALREGELN (minsta giltiga ram med sida ≥
  målet, annars den största; lika sida: den första), samma regel som
  `glzassets::pickIconSource`. Backenden väljer, skalar aldrig. Deklarerar
  också `programName()` (programfilens namn utan katalog och ändelse), som
  varje backend definierar bredvid `basePath()`.
- **`cursor_bitmaps.h`** (FONSTER_OCH_TIMER, 2026-10-12) — backend-neutralt,
  header-only, ren std: den STÄNGDA HANDEN som en egen tvåtonsbild
  (`CursorBitmap` — 16×16 rader av `.` genomskinlig, `X` svart, `o` vit,
  hotspot mitt i handflatan) och `expandCursor(bild, skala, hotX, hotY)` →
  RGBA8 med rak alfa, heltalsskalad med hotspoten. Planens E3: Windows och
  X11 saknar en stängd hand bland systempekarna, så D3D11 (`CreateIconIndirect`
  med `fIcon = FALSE`, skalan `SM_CXCURSOR / 16`) och X11
  (`XCreatePixmapCursor`, skalan ur uiScale) bygger den härifrån — macOS och
  Wayland har den i systemet (`closedHandCursor`, `grabbing`). Grind:
  `tests/cursor_gate.cpp` C1, som jämför VARJE pixel med mönstret.
- **`frame_margin.h`** (DOCKNING_DRAG E2L, 2026-10-13) — backend-neutralt,
  header-only, ren std + `gfx/window.h`: Linux STORLEKSMARGINAL utanför
  ytan. `kMarginLogical` (8) och `kCornerLogical` (16) i logiska px,
  `marginRole(x, y, b, h, m, hörn)` — kanten för en punkt i KLIENTENS px
  (Client innanför klientytan och bortom marginalen; hörnet räcker `hörn` px
  längs kanten, kapat till halva kortaste sidan) — och `isResizeRole`.
  `LinuxWindow::hitRoleAt` läser den för pekarens pil och trycket på Wayland
  och X11; D3D11 och Metal behöver den inte (OS:et äger ramen). Grind:
  `tests/geometry_gate.cpp` KM, mot ett handräknat facit.
- **`image_util.h`** (CANVAS_3D etapp 0) — backend-neutralt, header-only och
  internt: det de tre backendarnas bildvägar DELAR, så reglerna är en sanning.
  `kPngSignature`/`hasPngSignature` — provet `decodeImage` gör FÖRST i varje
  backend (beslut 5: bara PNG) — och `unpremultiplyRgba`, som `readTarget`
  lämnar målens premultiplicerade innehåll med (samma avrundning i alla tre;
  klampad, för ett mål skrivet med `Blend::Opaque` kan bära färg > alfa).
- **`renderer.cpp`** — backend-neutralt: klipp-stackens delade logik
  (`pushClip`/`popClip` — intersektion, obalans loggas). Backends
  implementerar bara `applyClip(scissor, masks, count)`; rundningen (UTÅT
  till hela pixlar) görs i backendens applyClip, likadant i alla.
  **RUNDADE MASKER (2026-08-21):** stacken bär `{scissor, mask, radius}`
  per nivå, `applyTop()` plockar de fyra YTTERSTA rundade nivåerna ur den
  FULLSTÄNDIGA stacken (icke-destruktivt urval — en poppad nivå kan
  återfå sin mask) och loggar EN gång per överskridande-episod. Kontraktet
  i sin helhet står i [../include/gfx/CLAUDE.md](../include/gfx/CLAUDE.md).
  Den skyddade **`reapplyClip()`** (DOCKNING_DRAG, Codex-fynd på E2L) är
  `applyTop()` för en backend vars målkoordinater bytts mitt i en frame —
  GL:s composite tillämpar stacken igen efter storleksmarginalens
  förskjutning.
  **GPU-TIDENS BREVLÅDA (PRESTANDA A2):** `takeGpuTimes(out)` lägger till
  det som kommit och tömmer under brevlådans lås; den statiska
  `addGpuTime(box, serial, ns)` lägger till och släpper de äldsta över
  `kMaxGpuTimes` (64) — statisk och på en `GpuTimeBox&`, så Metals
  completion handler kan lämna ett svar genom sin egen `shared_ptr` efter
  att renderaren rivits. Backendarna läser `gpuTiming_` vid framens början
  och sätter `gpuTimingSupported_` när frågorna gick att skapa.
  **SCENENS DELADE REGLER (CANVAS_2D etapp 1):** `meshDrawOk` och
  `updateOk` — samma avvisningar i alla tre backends (ogiltig/förväxlad
  del, indexCount utanför, offset/bytes som inte är hela fyrbytes), loggade
  EN gång per orsak och renderer i `sceneLogged_` (ett felaktigt anrop görs
  typiskt varje frame). Bildernas (etapp 3) bor bredvid: `imageDescOk` (mått,
  stride i hela pixlar — svarar med den effektiva radlängden) och
  `imageUpdateOk` (rektangeln, stride:n, inget render-mål), och
  `meshDrawOk` kräver en giltig `MeshDraw::image` för Image-programmet och
  för Line giltiga streck och spetsgräns (`strokeOk`, `kLogStroke`), och för
  alla inbyggda program ett giltigt klipp (REQ8 1, `clipOk`, `kLogClip` —
  0–`kMaxClipPlanes` plan, de aktiva ändliga).
  `sceneImageFlags` är bildflaggorna ALLA scenprogram läser.
  **RENDER-MÅLENS STACK (etapp 6):** `pushTarget` flyttar klippstacken in i
  målnivån och börjar tomt, binder målet (`bindTarget`), tillämpar inget
  klipp och rensar; `popTarget` binder det yttre målet FÖRST och lägger
  sedan tillbaka och tillämpar den sparade stacken — i den ordningen räknas
  scissorn mot rätt mål. `beginFrameTargets` (backendernas beginFrame)
  tömmer en obalanserad stack, `imageDrawOk` vägrar det bundna målets egen
  bild.
  **3D-MÅLEN OCH RENDERLÄGET (CANVAS_3D etapp 0):** `createRenderTarget(w,
  h)` — bekvämligheten — är `createRenderTarget(w, h, TargetDesc{})`.
  `targetDescOk` (mått > 0, provtagning 1/2/4/8, `kLogTargetDesc`),
  `readTargetOk` (giltigt och INTE i målstacken), `boundTarget()` (toppen)
  och `targetInStack(t)`. `meshDrawOk` prövar också `MeshDraw::state`
  (`stateOk`: uppräkningarna inom sina värden, ändliga biaser — `kLogState`)
  och loggar EN gång (`kLogNoDepth`) ett djuptest i ett mål utan djup, som
  ändå ritas — som Off. Loggbitarna 13, 14 och 25–27 är 3D-passets.
  **EGNA PROGRAM (CANVAS_3D etapp 1):** `meshDrawOk` lämnar
  `MeshProgram::Custom` till **`customDrawOk`**, och båda slutar i
  **`meshVerdict`** (djuploggen, eller orsaken en gång per bit). Custom-
  reglerna i ordning: ett giltigt program (`kLogCustomProgram`, bit 15), en
  giltig layout (`layoutOk` — platserna unika under 16, offset och steg i
  hela fyrbytes, attributen inom steget; bit 21) som PASSAR programmet
  (`layoutFits` mot `ShaderProgram::inputs`/`integerInputs` — reflektionen
  backenden gjorde; bit 30), index eller `vertexCount` (båda 0 = inget att
  rita, inget fel), strömmarna layouten läser med plats för hela vertex och
  hela instanser (`streamOk`, och instansattribut kräver `instanceCount` > 0;
  bit 23), GlazeUser i programmets storlek (bit 28), programmets texturer och
  inga fler (bit 29) och renderläget — sist `imageDrawOk` för varje textur.
  `programDescError` (GlazeUser en multipel av 16 och högst 4096,
  texturantalet, `defines` som identifierare utan radbrytning),
  `programError` och `programSourceName` är backendarnas gemensamma form på
  felen. Bitarna 0–7 är programmens (`& 7u` sedan uppräkningen fick sju
  poster).
- **`text_util.cpp`** — backend-neutralt: `gfx::wrapText` (radbrytning på
  ordgränser ovanpå `Font::fitWidth` — samma brytpunkter i alla backends
  per konstruktion; '\n' bryter alltid, långa ord hårdbryts). Ritytans
  etiketter bryter inte genom den sedan CANVAS_TEXT_OCH_BILD: `sdf.cpp`
  speglar REGELN glyf för glyf över delarna (`layLines`), och `geom_gate`
  S5d håller de två eniga för en etikett i en stil.
- **`font_fallback.h`** (CANVAS_TEXT_OCH_BILD A1) — backend-neutralt, internt
  och HEADER-ONLY (ingen CMake-rad, så fällan med de tre backendblocken
  uppstår inte): GLYFRESERVENS REGEL, EN för alla tre `Font`. `GlyphRef
  {face, index, real}` (face 0 = fontens egen, 1… reserverna i den ordning
  backenden lade till dem), `isControlCodepoint` (C0, DEL, C1) och
  `resolveGlyph(cp, own, reserve)`: fontens egen glyf → en reservfont som har
  glyfen (inte för ett styrtecken) → U+FFFD (egen, sedan en reservs) →
  fontens .notdef. `real` (det `Font::hasGlyph` svarar) är sant bara för
  kodpunktens egen glyf och aldrig för ett styrtecken — en font som mappar
  tabben ger oftast en tom glyf, och den ritas ändå. Backenden cachar svaret
  per kodpunkt (också negativt) med glyfens stegbredd, och mätningen,
  UI-atlasen (`getGlyph`) och `rasterGlyph` går alla genom cachen; `'?'`-steget
  som UI-vägen hade förut finns inte mer. Reservkedjorna per OS står i
  backendarnas CLAUDE.md.
- **`mat4.cpp` / `camera.cpp` / `geom2d.cpp`** — backend-neutralt, ritytans
  scen (CANVAS_2D etapp 1). `mat4.cpp`: `Mat4d` (inversen är Gauss–Jordan
  med partiell pivotering — kamerans element spänner över flera
  storleksordningar). `camera.cpp`: kedjan `T(origo − mitt)` (DEN enda
  subtraktionen med stora tal, i double) → skala med y/z-spegling för yUp →
  `rotationZ(−bäring)` (moturs i en rymd med y nedåt) → vid tilt
  `rotationX(−pitch)` + kameran på avståndet D + perspektiv, annars ortho →
  ytans NDC till målets px. `unproject` drar linjen genom två punkter på
  strålen och godtar träffen BARA om den ligger framför kameran (annars är
  pixeln ovanför horisonten). `geom2d.cpp`: `buildPolyline` — bitar om fyra
  vertex (segmentet med sina `ends`, skarvens bit av skarvens slag, en sluten
  linjes skarv i första punkten), chunk efter vertexantal med origo i bitens
  punkt — `makeDash` (SVG:s upprepning av ett udda mönster), `quadWeights`
  (CANVAS_TEXT_OCH_BILD: diagonalernas skärning i double, t och s strikt i
  (ε, 1 − ε) med ε = 10⁻⁹, parallella diagonaler relativt deras längder) och
  (etapp 4)
  `buildPolygon`: rensa ringarna, vänd dem (ytterringen moturs, hålen
  medurs — kantremsans "utåt" bygger på det), triangulera, och lägg en
  kantquad per kant (vertex 4g … 4g+3; de inre trianglarna använder 4g).
  Bygget sker i double och blir chunk-lokalt först när chunken är vald; en
  yta som inte ryms i en tom chunk fördelas med vertexkopior. En misslyckad
  yta loggas en gång per episod (atomic — byggarna får köras i en
  arbetartråd). Plus gradientens ramp (premultiplicerad interpolering) och
  `linearPaint`/`radialPaint`. Kontrakten står i
  [../include/gfx/CLAUDE.md](../include/gfx/CLAUDE.md). `mat4.cpp`s
  `lookAt` (CANVAS_3D etapp 2) lägger ögonrymdens axlar som RADER (s, v,
  −f) efter flytten av ögat till origo.
- **`camera3d.cpp`** (CANVAS_3D etapp 2) — backend-neutralt, 3D-VYN. Basen
  (blicken normerad, höger = blicken × upp, upp räknad om), matrisen
  `T(origo − öga)` (den enda subtraktionen med stora tal, i double) →
  `lookAt` → `perspective`/`ortho` → ytans NDC till målets px — det sista
  steget är kartkamerans (camera.cpp steg 5), skrivet två gånger, och
  likheten i scene_math_gate håller dem ihop. Strålarna och skalan räknas
  ANALYTISKT ur basen och halvmåtten, inte genom en invers. ETT ASYMMETRISKT
  FRUSTUM (`View3D::frustum`) går genom `Extents` (halva spannet + mittens
  förskjutning i bildplanet): `offAxisPerspective` lägger förskjutningen i
  matrisens z-kolumn, `rayThrough` räknar kanterna på `cx ± hx`/`cy ± hy`,
  `pxPerUnitAt` bygger på det halva höjdspannet `hy`, och utan frustum går
  perspektivet genom
  `Mat4d::perspective` (`fovY`). `usable` kräver ändliga tangenter i ordning.
  `frameUniforms` fyller `rayCenter` = blicken + mittens förskjutning längs
  höger och upp (ortografiskt och utan frustum = `forward`) och skalar
  `right`/`up` med halvspannen. `visiblePlane-
  Region`: synpyramidens åtta hörn RELATIVT ögat, punkterna där kanterna
  byter sida om planet, sorterade kring tyngdpunkten; ett polygon utan
  area är tomt. `toView3D`: tre punkter ur kartmatrisens invers (mitt-
  pixeln på djupet 0 och 0,5, överkantens mitt på 0,5) ger blicken, upp
  och halvhöjden, ögat är `M⁻¹ · (0, 0, 1, 0)` (projektionens centrum), och
  valet perspektiv/ortografiskt (`pitch > 0` efter klampning) görs som i
  camera.cpp. Ligger i ALLA TRE backendblocken i `CMakeLists.txt`, som de
  andra backend-neutrala filerna. Kontrakten i
  [../include/gfx/CLAUDE.md](../include/gfx/CLAUDE.md); grind:
  `tests/scene_math_gate.cpp` del 3D.
- **`earcut.h/.cpp`** (CANVAS_2D etapp 4, E8) — backend-neutralt, intern:
  den EGNA öronklippningen, ingen tredjepartskod. `triangulate(punkter,
  ringstarter, ut)`: dubbellänkade ringar (ytterringen moturs, hålen
  medurs), hålen bryggas in från vänster (strålen till närmaste kant, den
  synliga punkten närmast strålen, två dubblerade noder), öronen klipps
  (konvex nod utan reflex nod i triangeln; > 80 noder gallrar med z-ordning
  över ringens låda), och när klippningen fastnar: rensa, bota lokala
  självkorsningar, dela längs en giltig diagonal — sist en solfjäder.
  Koordinaterna relativt ytterringens första punkt (SWEREF). Svaret false
  också när AREAKONTROLLEN slår fel (trianglarnas area ≠ ytterringen minus
  hålen — en självkorsning som botesteget "klarade"). **Delningssteget har
  en arbetsbudget** (2·10⁷ besökta kanter, `budget_`): diagonalsökningen är
  O(n³) i värsta fall och hängde annars på slumpskräp. **Kompileras UTAN
  FMA** (`-ffp-contract=off` i `../CMakeLists.txt`, 2026-10-12): `turn` är
  två produkter och en subtraktion, och slås en produkt ihop med
  subtraktionen ger tre punkter på en linje inte längre exakt 0 och omvänd
  ordning inte exakt motsatt tecken — 2 av Y2:s 200 slumpytor föll på macOS
  (clang på arm64). Grind: `tests/geom_gate.cpp` (Y1–Y6).
- **`geom3d.cpp`** (CANVAS_3D etapp 4) — backend-neutralt, i ALLA TRE
  backendblocken i `CMakeLists.txt`: generatorerna och polygonen i ett plan
  (kontrakten i [../include/gfx/CLAUDE.md](../include/gfx/CLAUDE.md)). En EGEN
  vektortyp `V` med operatorer i den anonyma namnrymden — fasadens `Vec3d` är
  ett aggregat utan räknesätt, och operatorer på den hade kunnat krocka med
  andra filers. Rörets GERING: ringen är den förra ramens prisma skuren av
  halveringsplanet, och nästa ram är ringens RÄTA projektion mot den nya
  riktningen — den minsta vridningen, och exakt samma ring sedd från andra
  hållet (geom_gate R2: varje ring på radien från båda segmentens linjer, och
  volymen = arean × vägens längd). Polygonen: Newell relativt första punkten,
  ringarna projicerade på (u, v) och rensade PÅ NYTT där (två skilda punkter
  kan sammanfalla i projektionen), och earcuts trianglar vända efter tecknet
  i (u, v) — earcut lovar ingen orientering.
- **`sdf.cpp`** (CANVAS_2D etapp 5) — backend-neutralt (sedan REQ3_RITYTAN
  också TAKET: `truncateLines` speglar `Draw::elideLines` i `layoutText`, så
  bygget och mätningen kortar lika — ellipsen i referensglyfens stil, en
  radbrytning bär delens stil, och `TextMeasure::truncated`): avståndsfältet
  (`makeSdf` — tröskel vid ½, två exakta EDT:er i rastrets upplösning,
  centrumavståndet − ½, läst mitt i varje fälttexel), atlasen (EN per
  FAMILJ sedan CANVAS_TEXT_OCH_BILD: en font per stil i `fonts_`, den
  vanliga i konstruktorn och resten lat — en som inte går att ladda lämnar
  platsen tom och den vanliga ritar —, glyferna nycklade (stil << 32) | cp,
  hyllpackning med en texels luft, en ny sida när den sista är full,
  uppladdning med `updateImage`) och byggarna. TEXTEN (`buildText` över
  `StyledText`-delar): en PLATT glyflista (`LaidGlyph` — kodpunkt, glyf ur
  delens stil, steg med en syntetisk fet inräknad, skjuvning, färg, syntes),
  raderna ur den med `layLines` (`gfx::wrapText`s regel glyf för glyf: sista
  mellanslaget på eller före den första glyf som inte ryms, ett mellanslag
  först på raden räknas inte, annars hårt; mellanslag efter ett hårdbrott
  hoppas över; '\n' bryter alltid — känt på KODPUNKTEN, inte på en tom
  glyfpekare, för mätningens poster har ingen glyf), radhöjd och baslinje som
  de största bland stilarna med text, och sedan plattan, glorior och
  fyllningar — färgen och syntesen per glyf. **MÄTNINGEN (REQ2_RITYTAN):**
  glyflistan, raderna, radmåtten, blocket, ankaret och lådan är
  `layoutText(…, forDraw)`, DELAD av `buildText` (glyferna ur atlasen) och
  `measureText` (stegen ur `SdfAtlas::advance` — en byggd glyfs, annars
  stilens `Font::measure` för kodpunkten genom en egen UTF-8-kodare, cachat
  i `advances_`, som `clear()` inte tömmer); `boldStep` läggs på i layouten,
  aldrig i `advance`. Glyfnyckeln är `glyphKey`, delad av de två.
  `chunkFor` tar en UNDRE GRÄNS: glorierna hamnar på eller efter plattans
  chunk och fyllningarna på eller efter den sista gloria-chunken, så
  ordningen håller också över atlassidor; `lift` (rymden) går aldrig bakåt i
  chunklistan. Markörernas former och bildmarkören; ankarets hörn i
  `anchorCorner`, delad av texten och markörerna. Egen UTF-8-avkodare med
  backendarnas regel.
- **`display_util.h/.cpp`** — backend-neutralt, SKÄRMENS mm (CANVAS_2D
  etapp 2): det de sex `displayMetrics`-implementationerna delar, så
  formeln, spannet och reserven är EN sanning. `parseEdid` (millimetrarna ur
  första TIMING-deskriptorn — VAKTEN hoppar över block med pixelklocka 0,
  alltså monitor-deskriptorer, vars namn annars blivit millimeter — och
  basfältens hela cm som reserv; id:t tillverkare-produkt-serie),
  `pxPerMmFromDiagonal` (diagonalerna: EDID:s centimeterfel per axel tar ut
  varandra; 0 utanför 50–800 dpi), `isApple72Estimate` (macOS:
  CGDisplayScreenSize utan EDID är punkterna / 2,835) och
  `measure(px, mm, id)`/`assumed(id, orsak)` som ger en hel `DisplayMetrics`
  med orsak. RESERVEN är `kAssumedPxPerMm` — 96 dpi i FYSISKA px, utan
  någon skala-parameter. Fasadens kalibrering (`setDisplayCalibration`/
  `effectiveDisplay`, en map per id) bor också här. Grind: `tests/display_gate.cpp`, som når headern.
- **`gpu_util.h/.cpp`** (REQ9_GRAFIKKORTET, 2026-10-20) — backend-neutralt,
  ren std, namnrymden `gfx::gpu`: GRAFIKKORTENS gemensamma del. `vendorName`
  (PCI-tillverkaren), `preferenceName`/`kindName` (loggarna i alla tre),
  `pciAdapterId` (id:t `pci:vvvv:dddd:ssssssss:rr` — subsystemet i DXGI:s
  `SubSysId`-form, samma tal som Linux sysfs) och `disambiguateIds` (`@adress`
  eller `#n` BARA på id som krockar). Och LINUX-VALET som rena funktioner,
  kompilerade i alla tre bygg så att `tests/gpu_gate.cpp` mäter dem headless:
  `scanDrmCards(DrmPaths)` (sysfs: bara `card[0-9]+` med en `renderD*`;
  drivrutinen och sloten ur `device/uevent` — inte ur symlänkarna, som
  grindens falska träd inte kan ha på Windows; namnet ur NVIDIA:s
  `information`, pci.ids eller "PCI vvvv:dddd"; förval = `boot_vga`),
  `primeEnvironment` (miljöregeln — användarens variabel vinner, VALBARHETEN
  först, NVIDIA får `__NV_PRIME_RENDER_OFFLOAD=1` och aldrig `DRI_PRIME`, ett
  UTTRYCKLIGT val stoppas aldrig av `boot_vga` eller förvalets drivrutin),
  `markSelectable` och `applyPrimeEnvironment` (tillämpningen över en
  getenv/setenv som anroparen ger — en TOM variabel skrivs över, en
  icke-tom aldrig). Processens miljö rör den aldrig själv — det gör
  `backend_linux/linux_gpu.cpp` genom den.
- **`font_catalog.h/.cpp`** — backend-neutralt: `systemFontCandidates(
  FontFamily)` = fallback-kedjan av FAMILJER per plattform, varje familj med
  sin fil per stil (`FontFiles`; nullptr där stilen saknas — Tahoma och
  Lucida Console saknar kursiv, Noto Sans Mono likaså), `fontFile(familj,
  stil)` och `nearestStyles(stil)` (fet kursiv → fet → kursiv → vanlig).
  Backendarna prövar den närmaste stilen inom EN familj innan nästa — två
  familjer blandas aldrig (CANVAS_2D etapp 5). macOS har samma .ttc för
  alla fyra och väljer stilen i Core Text. **ENDA stället i trädet där
  fontsökvägar får förekomma** (storlekarna i fasadens `fontsize::`). Ny
  plattform/familj/stil → utöka katalogen.
- **`touchpad_rotation.h/.cpp`** (TODO 53) — backend-neutralt ur råa kontakter:
  `FrameAssembler` (ramen, hybridläget), `RotationTracker` (grader medurs ur
  EXAKT två fingrar, grepp vid 3°, sedan den TOTALA vridningen) och
  `TwoFingerClassifier` — Pan, Zoom eller Rotate (aldrig två av zoom/vridning,
  vridningen måste vara 2× zoomen), enhetslöst, låst till lyftet. Grind: trackpad_gate.

### Backendarna — en CLAUDE.md per mapp

Varje backend har sin egen fil (utbrutna 2026-10-10, CANVAS_3D beslut 2):

- **[backend_d3d11/CLAUDE.md](backend_d3d11/CLAUDE.md)** — Windows: Win32, D3D11, DirectWrite, WIC,
  styrplattans Direct Manipulation och råa kontakter, systemfältets `Shell_NotifyIcon`.
- **[backend_metal/CLAUDE.md](backend_metal/CLAUDE.md)** — macOS: AppKit, Metal, Core Text, Image I/O, systemfältets `NSStatusItem` (BLINT).
- **[backend_linux/CLAUDE.md](backend_linux/CLAUDE.md)** — Linux: Wayland och X11, EGL/GL, FreeType/
  fontconfig, libpng, portalen över D-Bus, XInput 2, pointer-gestures.

Varje fil har samma tre 3D-avsnitt (CANVAS_3D): **3D-delen** (djupet och
MSAA i målet, renderlägets tillstånd och UI-återställningen,
`beginOffscreen`/`readTarget`, GPU-preferensen), **Egna program**
(`createProgram`, förspelet, reflektionen, ritningen) och **Line3 och Glyph3**
(`GLAZE_POS3`, klippytan, den liggande texten). Kontrakten står i
[../include/gfx/CLAUDE.md](../include/gfx/CLAUDE.md).

Det som gäller ALLA backends står kvar här: event-översättningen och de lokala fallgroparna nedan.

## Event-översättningen (kritisk för händelsestyrningen)

D3D11: WndProc:en; Metal:
GlazeView + delegaten; Linux: seat-lyssnarna i `wayland_display.cpp` resp.
`translate()` i `x11_display.cpp` (alla köar via `pushEvent`). Oöversatt →
`EventType::None` = ingen omritning. **Systemfältets händelser**
(`EventType::Tray`, windowId 0 — SYSTEMFÄLTET) går samma `pushEvent`: D3D11
ur mottagarfönstrets WndProc (callbacken `WM_APP + 0x53` och timrarna),
Metal ur statuspostens action och spåryta; Linux har inget fält. **Appens
timer** (`EventType::Timer`, windowId 0 — FONSTER_OCH_TIMER) likaså: D3D11
ur meddelandefönstret `GlazeAppTimer`s WM_TIMER (`d3d11_timer.cpp`), Metal ur
NSTimer-blocket (`metal_timer.mm`), Linux ur timerfd-källan (`linux_timer.cpp`
— samma `TimerSource` som fönstrens timrar, med körnings-id:t i 64 bitar).
**Fönsterhändelserna måste täcka ALLA typer
`AppWindow::handleEvent` invalidatar på** — tappas en är symtomet "UI:t
uppdateras inte förrän jag rör musen". Modifierare: hela masken
(vänster+höger); `kModCmd` = Win-tangenten/Command/Super.

**MUSENS MODIFIERARE (post 17, 2026-10-07)** — `Event::mods` på musens
knappar och rörelser, för Ctrl-/Shift-klick i en lista, och **sedan CANVAS_3D
etapp 11 på hjulet och gesten** (Ctrl/Cmd + styrplattans drag kretsar på
ritytan; kontraktet vid `kModCtrl`). **D3D11:** `pushMouse`, `pushKey` och
`pushWheel`/`pushGesture` sätter `currentMods()` (`GetKeyState`, som följer
meddelandekön — läget för just det meddelandet); hjulet och gesten är EN väg
för WndProc:en och Direct Manipulation (d3d11_backend.h). **X11:**
`translateStateMask` på XI2-eventens `mods.effective` — också skrollvalutorernas
hjul och nypningen — (kärnans `state` utan XI2, för knapp 4–7 och vid
inträde): läget FÖRE eventet, rätt för en knapp och ett hjul (Ctrl hålls
redan). **Wayland:** pekaren bär inga modifierare; `pushMouse`, hjulen och
`pushGesture` får `WaylandDisplay::mods_` (senaste `wl_keyboard.modifiers`) —
når bara en yta med tangentbordsfokus, och `wl_keyboard.leave` nollar den
(protokollet nollställer läget, nästa enter följs av `modifiers`).
**Metal (BLINT):** `pushMouse`, `pushWheel` och `pushGesture` tar
`translateMods(ev.modifierFlags)`. **MODIFIERARTANGENTENS EGET EVENT** bär
läget EFTER tangenten: Win32:s `GetKeyState` gör det själv, X11 (`state`) och
Wayland (`modifiers` kommer efter `key`) lägger på tangentens bit med
`xkbModsAfterKey` (xkb_keys.h), och Metal gör `flagsChanged:` — AppKit
skickar ingen `keyDown:` för en modifierare — till KeyDown/KeyUp
(`pushModifierKey`). Grind: `tests/trackpad_gate.cpp` del O och B.
**DUBBELKLICKETS TID (`doubleClickMs`, post 17):** D3D11 `GetDoubleClickTime()`
i `d3d11_platform.cpp`; Linux i `linux_platform.cpp` — portalens
`org.gnome.desktop.peripherals.mouse/double-click` (int32) läst LAT vid
första frågan (`g_doubleClickMs`, 0 = oläst), levande genom
SettingChanged-filtret, 400 utan portal; Metal (BLINT)
`NSEvent.doubleClickInterval` × 1000 i `metal_platform.mm`.

**AVBROTTET OCH TANGENTSLÄPPET (REQ2_INMATNING, 2026-10-13)** —
`EventType::MouseCaptureLost` (kontraktet vid enumet) per backend:
**D3D11** `WM_CAPTURECHANGED` medan `pressCapture_` står och capturen inte gick
till fönstret självt eller via `g_handoverTo` (DOCKNING_DRAG E3,
[backend_d3d11](backend_d3d11/CLAUDE.md)); **Wayland (BLINT)**
`wl_pointer.leave` medan ett LEVERERAT tryck hålls, parkerat till nästa flush
och struket när nästa enter är vår egen popup i samma fokusgrupp
([backend_linux](backend_linux/CLAUDE.md)); **X11, Metal och headless** aldrig
(`FocusOut`/`resignKey` bär resten, headless har ingen pekare). **Metal
(BLINT)** levererar Cmd-SLÄPPET själv: `[NSApp sendEvent:]` sväljer `keyUp:`
medan Cmd hålls, så pumpens `dispatchNSEvent` ger ett sådant `KeyUp` direkt
till fönstret. Glaze gör avbrottet till `Widget::mouseCancel` och släppet till
`onKeyUp` ([../../widgets/src/CLAUDE.md](../../widgets/src/CLAUDE.md)).

**NAVIGATIONSKLUSTRET OCH NUMPADEN (TODO-post 31, 2026-10-07)** — kontraktet
och tabellen per plattform står vid `enum class Key`; här är var varje
backend gör det. **D3D11:** numpadens navigationsläge och de dedikerade
tangenterna delar VK (`VK_END` är båda), och det ENDA som skiljer dem är
EXTENDED-flaggan i lParam (bit 24, `KF_EXTENDED` i `HIWORD`) — `pushKey`
tar därför lParam och `translateKey(vk, lp)` väljer `End` eller `KP_End`;
`VK_CLEAR` (numpad-5, NumLock av) blir `KP_Begin`. Känd avvikare, skriven
vid koden: `SendInput` utan `KEYEVENTF_EXTENDEDKEY` ger de dedikerade
tangenterna utan flagga, alltså `KP_*` — widgetarna märker det inte
(`AppWindow` viker), bara en app som binder `End` men inte `KP_End`.
**Linux (`xkb_keys.cpp`):** navigationsrollerna (`XKB_KEY_KP_End` …) står
nu i `keypadKey` — det FAKTISKA läget — och INTE i level-0-tabellen, där
de förr blev `End`; level 0 bär bara de dedikerade keysymerna.
**Metal (BLINT):** `PageUp`/`PageDown` ur `kVK_PageUp`/`kVK_PageDown`, och
`Insert` ur **`kVK_Help`** (0x72) — ett PC-tangentbords Insert-plats på en
Mac; ett ANTAGANDE som står i `doc/os/macos/TESTS.md`. Numpaden ger alltid
siffrorna, så `KP_End` m.fl. kommer ALDRIG från den backenden. Grind för
D3D11 och xkb: `tests/numpad_gate.cpp`.

## Lokala konventioner & fallgropar

- **SYSTEMFÄLTET (`gfx/tray.h`, SYSTEMFÄLTET 2026-10-12) har TRE
  implementationer, en per backend och i var sitt block i
  `../CMakeLists.txt`** — `d3d11_tray.cpp`, `metal_tray.mm` (också i
  ARC-listan) och `linux_tray.cpp` (en kommenterad dummy) — så en ny
  fasadfunktion där kostar tre, inte sex. Varje funktion är säker före init
  och efter shutdown (D3D11 `platformInitialized()`, Metal `trayInit`/
  `trayShutdown`, Linux tyst), och `gfx::shutdown()` tar bort ikonerna
  FÖRST. Tillståndet (`g_entries`) står i NAMNRYMDEN och inte som en lokal
  static: konstruerat före main och därför rivet EFTER `glaze::App`, vars
  destruktor kör `gfx::shutdown` — en lokal static skapad vid första ikonen
  hade rivits före App. Dockans BEGÄRAN lagras i `platform.cpp` (`setShowInDock`/`showInDock`); backenden gör sin del i `applyShowInDock` (deklarerad i `icon_util.h`).
  `WindowDesc::showInTaskbar` är ett attribut: D3D11 ger ett ofägt fönster
  ett dolt ägarfönster, Metal loggar i `MetalWindow::init` och Linux i
  `ProxyWindow::init` (en rad för Wayland, X11 och headless).
- **EN NY BACKEND-NEUTRAL FIL LÄGGS I ALLA TRE BLOCKEN** i
  `../CMakeLists.txt` — `add_library(common_gfx …)` står en gång per backend,
  och filerna här (`camera3d.cpp`, `geom3d.cpp`, `sdf.cpp` …) upprepas i
  varje. En fil i bara ett block bygger på din plattform och länkar fel på de
  andra två; en fil som ska in överallt utan att upprepas går efter blocken
  med `target_sources` (`touchpad_rotation.cpp`).
- **HELTAL I GL-PROGRAMMEN GÅR SOM HELTAL** (CANVAS_2D etapp 11): sida,
  slag, ändar, flaggor och etikettindex är `ivec4`/`uvec4`/`uint` med
  `glVertexAttribIPointer` — som D3D11:s `_SINT`/`_UINT` och Metals `Char4`/
  `UInt`. Ett SKALAT format (heltal utan normering till float) finns inte i
  D3D12, och Mesas d3d12-drivrutin (WSLg) räknade om varje ritad vertex på
  CPU:n: 8,5 i stället för 1,4 ms per kameraändring i `canvas_perf`.
- **Chrome-mått är logiska px** (`* uiScale()`) och IDENTISKA mellan
  backends; färger ur `chromeColors_` (temasystemet), aldrig konstanter.
- **STORLEKSGRÄNSERNA (`setSizeLimits`, APP_API B1) finns i FEM
  implementationer på Linux, inte fyra** — och samma räkning gäller
  **`setTopmost`/`topmost()`** (APP_API_3 A), som dessutom har en SJÄTTE
  utanför Linux räknat över alla backends. `createWindow()` returnerar en
  `ProxyWindow` (linux_platform.cpp) som ärver `Window` DIREKT och
  delegerar till den runtime-valda Wayland-/X11-/headless-instansen —
  lagring i `LinuxWindow`-basen hjälper den alltså inte. Proxyn måste
  LAGRA gränser satta FÖRE `init()` och skicka dem vidare när det riktiga
  fönstret skapas; bygget fångar en glömd override som kompileringsfel,
  men INTE en tappad vidarebefordran. Per backend: Wayland
  `xdg_toplevel_set_min_size`/`set_max_size` i YTENHETER (som fanns
  genererade i protokoll-headern och aldrig anropades) — committas bara
  EFTER configure-handskakningen, X11 `XSizeHints`/`WM_NORMAL_HINTS` i
  FYSISKA px satta FÖRE `XMapWindow`, headless en dokumenterad no-op
  (ingen WM som kan neka, ingen kant att dra i). Wayland och X11
  ÅTERAPPLICERAR vid text-/userScale-byte respektive Xft.dpi-byte.
- **MUSPEKAREN (`setCursor`/`cursor()`, post 15) finns i SEX
  implementationer** — samma proxyfälla som storleksgränserna, tredje
  gången: ProxyWindow LAGRAR en form satt före `init()` och skickar den
  vidare där och efter (splitter_gate F1, båda vidarebefordringarna
  sabotageprövade på Wayland och X11). Per backend: **D3D11** svarar på
  `WM_SETCURSOR` med `HTCLIENT` ur `cursor_` (chromets resize-zoner går
  till `DefWindowProc` som förut) och kallar `SetCursor` direkt bara när
  pekaren är inne eller fönstret håller capture — annars hade en form
  satt utanför fönstret bytt pekaren över ett ANNAT fönster. **Wayland**:
  wl_cursor-NAMN per form med reservnamn (`ew-resize` → `col-resize` →
  `sb_h_double_arrow` …, temana skiljer sig), EN väg (`refreshCursor`)
  för resize-rollerna och klientformen, dedupe på namnpekaren, och bara
  fönstret som har pekaren (`pointerWin_`) tillämpar. **X11**:
  font-cursors (`XC_xterm`, `XC_hand2`, `XC_sb_h_double_arrow` …)
  cachade per form i displayen och tillämpade PER FÖNSTER — den
  display-globala `lastCursorRole_` som fanns förut kunde ge fel form när
  pekaren bytte fönster. **Metal** (BLINT): `NSTrackingCursorUpdate` +
  `cursorUpdate:` och en `mouseInside_`-flagga i vyn. **Headless**
  lagrar bara. Heter `PointerShape` för att `X.h` definierar
  `CursorShape` som makro. **`Crosshair`/`Move`** (CANVAS_2D etapp 8):
  D3D11 `IDC_CROSS`/`IDC_SIZEALL` (32515/32646), Wayland `crosshair`/`cross`
  och `move`/`all-scroll`/`fleur`/`grabbing` — reservtemat utan cursortema
  (WSLg) har inget kors men svarar på `all-scroll` (libwayland-cursor 1.23,
  uppmätt) — X11 `XC_crosshair`/`XC_fleur`, Metal `crosshairCursor`/
  `closedHandCursor` (ingen publik fyrpil i AppKit; BLINT). Proxyn och
  headless behövde ingen ändring: de lagrar och vidarebefordrar formen
  generiskt.
- **MITTENKNAPPEN och CAPTUREN (CANVAS_2D etapp 8).** D3D11 tar
  `SetCapture` vid VARJE knapps tryck men släpper den först när den SISTA
  är uppe (`WM_*BUTTONUP` med `wParam & (MK_LBUTTON | MK_MBUTTON |
  MK_RBUTTON)` == 0) — vid första släppet tappades nästa knapps släpp
  utanför fönstret, och ett mittendrag hade fortsatt på vanliga rörelser.
  X11 och Wayland håller den implicita grabben tills alla knappar är uppe.
  Metal: `otherMouseDown/Up` gäller VARJE knapp utom vänster och höger, så
  bara `buttonNumber == 2` blir `MouseButton::Middle` — sidoknapparna
  (bakåt/framåt) hade annars startat en panorering (BLINT).
- **PROGRAMIKONEN (`setIcon`/`iconApplied`, PROGRAMIKON) finns i SEX
  implementationer** — fjärde gången proxyfällan: ProxyWindow LAGRAR ramarna
  alltid (även efter init, så ett åter-`init()` får dem) och vidarebefordrar
  efter `init()` och vid varje senare anrop; `iconApplied()` frågar det inre
  fönstret och svarar false före init. Per backend: **D3D11** HICON ur ramen
  (32-bitars DIB med RAK alfa + AND-mask ur alfan) i SM_CXSMICON/SM_CXICON
  för fönstrets DPI, exe-resursen först vid `programFirst`, om vid
  WM_DPICHANGED; **X11** `_NET_WM_ICON` (ramar ≤ 256, kapat till serverns
  största begäran — ett BadLength fäller appen) + `WM_CLASS` ur `appId()`,
  båda före `XMapWindow`; **Wayland** `xdg-toplevel-icon-v1` (kvadratiska
  ramar, förmultiplicerad ARGB8888 ur en memfd-pool, objekt och buffertar
  levande tills nästa ikon) eller loggrad + false, app_id ur `appId()`;
  **Metal** (BLINT) dockans ikon är APPENS — paketikonen vinner, annars äger
  första fönstret den; **headless** tyst no-op, false.
- **ALLTID ÖVERST (`setTopmost`, APP_API_3 A) har TVÅ VÄGAR på X11 och
  degraderas HELT på Wayland.** X11: EWMH säger att ett MAPPAT fönster
  byter tillstånd med en clientmessage till roten (WM:et äger
  `_NET_WM_STATE` då) medan klienten skriver propertyn själv på ett
  OMAPPAT — bara clientmessage-vägen hade tappat varje begäran gjord före
  `show()`, uppmätt. Grenvalet får därför INTE läsa `visible_` (den är
  `true` redan före första `XMapWindow`) utan `mapped_`. Gettern läser
  WM:ets svar via `onPropertyNetWmState`, alltså vad som FAKTISKT gäller.
  Wayland: xdg-shell saknar formen — no-op som LOGGAR EN gång (bara vid
  `on == true`; en avstängning är inget att rapportera) och `topmost()`
  svarar alltid false. Headless: dokumenterad tyst no-op.
- **`WaylandWindow::shutdown()` AVREGISTRERAR FÖRST** — `unregisterLinuxWindow`
  + `unregisterSurface` + `windowDestroyed` ligger överst, före all
  protokollnedrivning, och ordningen är bärande. Allt därunder kan dispatcha
  lyssnare, och så länge ytan står kvar i displayens tabell slår `windowFor`
  upp ETT FÖNSTER SOM RIVS: en `wl_pointer.enter`/`wl_keyboard.enter` i flykt
  mot ytan satte tillbaka `pointerWin_`/`keyboardWin_` till fönstret, som
  `ProxyWindow::shutdown` raderar (`inner_.reset()`) en rad senare. Avregistrerad
  först resolvas allt sådant till nullptr och lyssnarna är null-säkra.
  **`waitConfigured` är en TIDSgräns (1000 ms, sover i `poll()`)**, inte 64
  `wl_display_roundtrip`: en roundtrip återvänder så fort kompositören svarat på
  VÅR sync, så den gamla "väntan" kunde vara över på någon millisekund och rev
  ett fönster som var på väg att mappas.
- **Chrome-interaktion sätter `dirty_`** — huvudloopen ritar om
  händelsestyrt via `takeDirty()`; annars "hänger" hovern.
- **Ritning under modal resize**: D3D11 `WM_SIZE`/
  `WM_PAINT`, Metal `windowDidResize` — sådan kod bor i render-callbacket.
- **Textur-livstid hör till renderern** — släpp fonter OCH bilder före
  renderern rivs: `App::executeClose` gör `images_.reset()` +
  `font_.reset()` före `win_->shutdown()`.
- **D3D11-batchern flushar på tillståndsbyte** — nya rit-primitiv går via
  `setState`/`quad`, aldrig egna `Draw` mitt i en frame; `clear()` flushar
  före `ClearRenderTargetView`.
- **Win32-detaljer:** ANSI-makron (`IDC_ARROW` m.fl., ingen UNICODE-define)
  — W-API:er + `toWide`; lParam-muskoordinater signerade (negativa vid
  capture); `WM_NCCALCSIZE` kräver `SWP_FRAMECHANGED` efter CreateWindow.
