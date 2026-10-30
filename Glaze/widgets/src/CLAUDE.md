# CLAUDE.md — Glaze/widgets/src

Implementation av **`glaze`**-libbet — INFRASTRUKTUREN. Komponenterna bor
i [widgets/](widgets/CLAUDE.md) (en cpp per widget); publika
deklarationerna i [../include/glaze/](../include/glaze/). Del av GlazeUI —
se [root /CLAUDE.md](../../../CLAUDE.md) för tre-fils-modellen och de hårda
reglerna. `CMakeLists.txt` (ett steg upp) bygger `glaze` av dessa filer.

## Filer

Allt här ritar/pratar via **gfx-fasaden** (`gfx::Renderer`, `gfx::Event`,
`gfx::log` …) — aldrig direkta backend-anrop; utseendet är rotens regel 3.

- **`app_anchor.cpp`** — INGEN del av libglaze: en INTERFACE-källa på `glaze` i DELAT läge, inkompilerad i
  varje mål som länkar det DIREKT (ett statiskt mellanbibliotek med PRIVATE sväljer det); dess statiska objekt river App i PROGRAMFILENS `exit()`, aldrig i en DLL:s DETACH (R3).
- **`app.cpp`** — `glaze::App` + **`glaze::app()`** (Meyers-singletonen:
  skapas lat vid första fönstret, `gfx::init` i ctorn, `gfx::shutdown` i dtorn
  vid processlut — idempotent `shutDown()`, som också STÄNGER postkön och förstör det kvarvarande utanför låset före `gfx::shutdown`, i delat läge via `detail::shutdownApp` ur ankaret), loop-ägaren: EN pump (`pumpUntilClosed`, delad av alla
  `AppWindow::run`-anrop — skyddet mot reentrans; `pumpDepth_` gör
  kvarlever-varningen bara ytterst), sov-villkoret (`waitEvent` bara när INGET
  fönster vill ha frame — hård regel 1 över N fönster; dolda fönster vill
  aldrig), event-routing på `Event::windowId` (`Quit` köar bara ägarkedjornas
  RÖTTER med `AppQuit` — ett ägt fönster stängs i sin rots kaskad, DOCKNING E3), modal-filtret (bara stackens
  topp får INPUT; fönsterhändelser/timers/MouseLeave släpps igenom; klick på
  blockerat fönster ⇒ modalens `raise`; UNDANTAG: fönster transitivt ÄGT av
  toppen — dialogens egen Dropdown-popup — får input), popup-stängning,
  registret utåt (`windows`/`windowCount`/`find`), **postkön** (APP_API_2 R17,
  PRESTANDA D2: TVÅ köer under ETT lås — `posted_` appens, `frame_`
  ramverkets — och EN mekanism: `enqueue` lägger i och väcker (också ramkön,
  för ritning utanför pumpen), `runQueue` kör en snapshot via swap UTANFÖR
  låset, frågar budgeten efter VARJE funktion och lägger resten FÖRST i kön
  under låset. `drainPosted` kör ramkön hel och sedan appens inom
  `postBudgetMs_` (väggtid från köns början, en nästlad pump inräknad), i
  pump-SVANSEN FÖRE `flushPendingCloses()`; `hasPosted` frågar båda och
  `shutDown` stänger båda. Ramkön tas av `stepTick`, `postCameraChange` och
  `AppWindow::scheduleResizes` — övriga ramverksposter följer appens budget.
  En postad lambda får posta nya och öppna en modal dialog. **EXTERN BILDKÄLLA** (`registerExternalFrameSource(desktopHz)`/`postExternalFrame`/`unregisterExternalFrameSource` i app.h, registret i app.cpp, pumpens och fönstrets sida i `external_frames.h`): källans EN väntande jobb körs i pump-svansen FÖRE ramkön, helt och utan budget (`externalRunJobs`); medan en källa finns presenterar alla fönster osynkat (`setExternalFramePacing`, också ett fönster som öppnas under tiden) och skrivbordet ritas högst den LÄGSTA källans `desktopHz` (0 räknas som 1) — pumpen sover till periodens slut på en intern timer (`kExternalWakeTimer`), ett väntande jobb håller den vaken, ett jobb körs aldrig efter avregistreringen och `externalClose` i `shutDown` släpper resten. `post` väcker med `gfx::postWakeup()`
  utanför låset; `EventType::Wakeup` har en egen gren i `dispatch` som varken
  loggar eller invaliderar — `windowId` är 0; `EventType::Tray`, också med
  windowId 0, går till systemfältets register med modalstackens topp,
  `detail::trayDispatch`; `EventType::Timer` med windowId 0 ANTECKNAS
  (`detail::timerNoteDue`) och levereras i svansen efter de postade
  (`detail::timerDrain`), och sovvillkoret frågar `timerHasDue()`) och
  **uppskjutna stängningar i pump-svansen**: `flushPendingCloses` kör först **onClose-VETORUNDAN**
  (`confirmClose(w, skäl)` — ägda barn djupast först med `OwnerClosing`, sedan fönstret; första `e.cancel`
  avblåser HELA kaskaden, ingen stängs, och det vetade fönstrets `result_`
  nollas — `dialogResult()`) och sedan `executeClose` (river
  OS-fönstret; kaskad: BARNEN FÖRE ÄGAREN; render-callbacken nollas FÖRE
  rivning; efter `shutdown()` **återaktiveras en överlevande ägare explicit**
  — `owner->window().raise()`, bara om ägaren själv lever och inte
  `pendingClose_`, annars tappar OS:et foreground till fel fönster vid nästlad
  ägarkedja — **men aldrig efter en POPUP** (en popup tar aldrig aktiveringen);
  en DOLD ägare lyfts inte (fasadens `raise()` lämnar ett dolt fönster dolt)),
  raderar ALDRIG C++-objektet (användaren äger det). **`executeClose` plockar
  fönstret ur stängningskön** precis som ur modalstacken — annars stänger
  nästa svans FEL fönster på samma adress (`window_gate`, placement new). `executeClose`
  tömmer också de hållna tangenterna och tangentminnet (`isKeyHeld`) —
  objektet lever vidare och ska svara nej —, nollar `active_` och
  `resizeQueue_` (ett stängt fönster levererar inget `onResize`) och tar
  KORTET (`liveGeometry()` — BEGÄRAN om fönstret aldrig syntes, TODO 80) medan
  OS-fönstret finns; stängt maximerat eller minimerat får `initW_/initH_` normalrutans storlek.
  **SOVVILLKORET** frågar också `pendingClose_.empty()`: ett dolt fönster ger
  på Linux inga händelser, och ett `close()` ur `onLoad` hade legat kvar i kön.
  **EFTER VARVETS RITLOOP** kör pumpen `detail::releaseQueuedPixels()`
  (`canvas/image_data.h`): bilder med `setKeepPixels(false)` släpper sina
  pixlar först här, så varje fönster som ritade bilden i varvet hann få den.
  **`activeWindow()`** = första öppna, inte stängningsväntande fönster med
  `active_` (AppWindow sätter det). **`noteEvent(typ)`** stegar `eventSeq_`
  — `onResize`-kedjespärrens räkneverk — för riktig inmatning
  (`isInputEvent`) och `WindowResized` (Linux levererar fönsterdraget som
  storleksändringar utan pekarhändelser); Wakeup, timer och fokus stegar inte.
  Den anropas överst i `AppWindow::handleEvent`, så grindarnas syntetiska
  händelser räknas också.
  **Skärmkalibreringen** valideras i `gfx::setDisplayCalibration`, sedan
  `measureDisplay` per öppet fönster; **upp- och minnesbudgeten** LAGRAS bara
  (nästa `renderContent` läser dem). **Användarskalan**: värdet bor i gfx
  (`gfx::setUserScale` klampar); sättaren är en no-op utan ändring, stänger
  popups (fel ankare efter bytet) och kör `window().userScaleChanged()` per
  öppet fönster — fontomladdningen sker i event-vägen (`WindowDisplayScaleChanged`
  → `reloadFont`), aldrig i sättaren. **`App::languageChangedAll()`** är
  språkbytets runda och den enda PUBLIKA av de statiska (språkbutiken är FRIA
  FUNKTIONER och når inte de privata). ORDNINGEN är bärande —
  `closePopupsNow(nullptr)` SYNKRONT (en öppen meny bär en snapshot med gammal
  text) → systemfältets tooltips (`detail::trayLanguageChanged`) → SNAPSHOT av
  `open_ && !pendingClose_` → `repushTitle()` gjort av App SEPARAT (en
  override som glömmer basanropet tappar inte titeln) → per fönster FÖRST
  `Widget::languageChanged()` över hela trädet (det en widget härleder ur
  språket och lagrar) och SEDAN `AppWindow::onLanguageChanged()`, så en
  omlayout mäter de nya texterna — med OMPRÖVAT fönsterfilter före VARJE
  anrop (kroken får öppna/stänga fönster) → `invalidateAll()`. INTE
  `reloadFontsAll`, INTE `applyContentSizeAll` (chromets höjd är stilmått).
- **`widget.cpp`** — PEKAREN: `setProp("cursor")` (prefixen
  `gfx::PointerShape::` undantagna i `lang_gate`), `setCursor`/`resetCursor`
  och `refreshPointer` (också ur `setEnabled`/`setVisible`; `detach` EFTER
  mutationen; `refreshDefaultButton` likadant, ur samma sättare och `adopt`).
  CAPTUREN routas till fönstret (`hasPointerCapture` läser `capturedWidget_`;
  `takePointerCapture`). Och basen: tillstånd → färgROLL (`textColor`/
  `hotBorder`, så egna widgets får dem gratis), `paintBounds()` (resolve +
  `c.style->overdraw`) och
  **`Widget::resolve()`** — placeringen: 0) FLEX-förälder (`placesChildren()`
  ⇒ förälderns `placeChild()` — Bar; barnets dock/anchors ignoreras), 1)
  DOCK-carving via SYSKONVANDRING (remsor ur förälderns childArea i
  barnordning; Fill tar resten; dolda syskon skär inget; stateless O(n²)), 2)
  **KANTBINDNING per axel** (SYSKONANKARE). **GREN 2 BÖRJAR MED EN SKÄRNING**:
  ytan `a` är `carveSiblingDocks(*parent_, this, c)` när widgeten står i
  dockflödet, annars `c.area`, och alla `c.area`-läsningar nedanför läser `a`
  — annars lägger sig ett icke-dockat barn över menyraden.
  **`carveSiblingDocks` är en PUBLIK STATISK basmedlem** (en fri funktion når
  inte `protected contentSize()`) och återanvänder `carveDock` rakt av — EN
  beskrivning av vad en remsa gör. `except` FÅR vara null (GlazeBuilder räknar
  koordinater innan preview-widgeten finns). **`AppWindow` sätter
  `inDockFlow_ = false` på SIN EGEN `topBar_`** — på INSTANSEN och inte på
  typen: en nästlad TopBar SKA skäras av sina dockade syskon.
  **REMSANS MÅTT är `dockedSize(w, area, c)`** (privat statisk):
  `phys(contentSize)`, och för Top/Bottom höjden ur `heightForWidth(c, area.w
  / c.scale)` när kroken svarar — med den yta som ÅTERSTÅR när remsan skärs, i
  dock-uppspelningen OCH i `carveSiblingDocks`, så en remsa är lika tjock för
  sig själv och för syskonen. `sz` räknas EFTER dock-grenen (layout_gate fall
  14). Fortsättningen på gren 2: en kant är BUNDEN av ett SYSKONANKARE eller
  ett FÖRÄLDERANKARE, syskonet vinner där båda finns — två bundna = sträck
  (klampad ≥ 0), en bunden = position + mätt storlek, ingen = centrerad +
  offset (`layout_gate` fall 10). **HÖJD FÖR BREDD**: sträckt vågrätt men inte
  lodrätt ⇒ `h = heightForWidth(c, pw / c.scale) × scale` när kroken svarar,
  i `ph` OCH i positionen i alla tre lodräta grenar (en bottenbunden etikett
  växer uppåt; `layout_gate` fall 20).
  Syskonkanten resolvar MÅLET med SAMMA Ctx — syskon delar förälderns
  childArea — via **`earlierSibling(namn)`**, som vandrar `parent_->children_`
  och **stannar vid `this`** precis som dock-carvingen. Den raden ÄR
  cykelskyddet: uppslaget kan bara peka BAKÅT, så ingen djupvakt behövs.
  **SYSKONANKARNAS MINNE** (REQ4 1): `resolveSibling` minns varje syskons rect
  under det YTTERSTA `resolve`-anropet (`ResolveScope`, ett djup
  + en karta som töms vid djup 0; nyckel widget OCH ctx — adress, yta, skala,
  stil — för en förälder kan resolva sina barn med ett eget Ctx), så varje
  syskon löses högst en gång och resolve är tillståndslös utåt. UI-tråden.
  Miss ⇒ **TYST** fallback (ingen `gfx::log` i `resolve()` — den körs 6+ ggr per
  frame; valideringen bor i `setProp`/`applyProp`/builderns
  `commitPropEdit`). **ENHETSBRYGGAN ligger i `resolve()`**: `contentSize()`
  svarar i LOGISKA px, `c.area` är FYSISK, och den lokala hjälparen
  `phys(logical, c)` räknar om på KONSUMTIONSsidan (`dockedSize` och
  mätgrenen). Bas-`contentSize()` är den RÅA `{width, height}` (AXELREGELN
  bor i varje widgets egen, widgets/CLAUDE.md; dubbelankare sträcker över
  auto-måttet). **`childrenExtent(c[, bredd])`** — autostorlek UR BARNEN,
  opt-in för komposit-författare — är `extentOf(c, bredd, skip)`: en
  ORDNINGSKÄNSLIG invers av `carveDock`, räknad BAKLÄNGES genom barnordningen,
  där `Fill` behandlas VID SIN POSITION och varje remsa bär sitt TVÄRKRAV. De
  ODOCKADE (REQ7_LAYOUT B1) i ett framåtpass med `Kant`/`Led` (anonyma
  namnrymden): `närkant`/`fjärrkant` och `härled` är resolve-reglerna med
  LUFTEN, också DOLDA följer med (kanterna bär kedjan) men bara synliga bidrar,
  och `överVarandra` avgör sidledsparen. Med en BREDD (REQ3) får varje dockad
  remsa sin bredd (en `vector` per barnindex), de odockade restbredden, och ett
  barn med två kända x-kanter `kändBredd`. `skip` är fönstrets anpassning
  (`AppWindow::fitContent` utesluter menyraden och statusraden, som
  `chromeInsetH` redan räknar). **`actualSize`/`toWindow`/`fromWindow`**
  (B17) går genom den privata `windowBox`: `ctxFor` + `resolve` som ritningen,
  men ett stängt fönster eller ett utan font ger ett tomt svar i stället för
  `behaviorCtx`s precondition; roten är `c.area`, och `fromWindow` är
  `fireMouse`s formel (så `toWindow(e.local)` är trycket).
  **`lifeToken()`/`HandlerGuard`/`postEvent(fn)`** (det lata livstecknet
  `liv_`; vakten fångar det, FÖNSTRETS — rotens, via den privata
  `windowLife` — och `attachGen_`, som `setOwnerForTree` stegar vid varje
  adopt/lossning; `postEvent` KRÄVER `owner_` och prövar vakten FÖRE och EFTER
  `fn`; händelseparen går genom `fireEvent`/`firePair` i headern),
  **BORTTAGNINGEN `unadopt`/`releaseOwned`** (fallgropslistan nedan),
  **FLYTTEN `transferChild`**, rivningsvägen **`severChild`** och
  **`liveOwner()`** — fönstret bara medan dess livstecken (`ownerLife_`)
  lever —, bas-`setProp()` (size/pos/dock/anchor/right/bottom/tabIndex/
  visible/enabled + `anchorLeft`…`anchorBottom` med TEXTformen
  `"<namn>[,<gap>]"` — en ANNAN form än `.glazeui`:s structliteral, som
  `bld::applyProp` sätter direkt på fältet), `adopt(unique_ptr)` (ÄGD) och
  **`adopt(Widget&)` (LÅNAD; dubbeladoption = no-op + logg)** +
  `setOwnerForTree`, `setEnabled/setVisible` (privata — invaliderar BARA vid
  ändring; `setEnabled` kör kroken `enabledChanged()` efteråt, så en komposit
  kan föra läget vidare), **`languageChangedTree()`** (privat — körd av
  `adopt(Widget&)` in i ett ÖPPET fönster, och av fönstret vid bytet och vid
  visning), capture/grab-vidarebefordran och `invalidate()`/
  `invalidateRect()`/`restartCaretBlink()`. **`invalidate()` stegar
  `invalidations_`** FÖRE ägarvakten, och **`damageStamp()`** är den plus
  ägarens `fullInvalidations_` (här, för headern får inte inkludera
  app_window.h); varken `dirty_` eller damage läser den. **`AppWindow`-ctorn
  sätter `menuBar.name`/`statusBar.name`** (inbyggda, utan rad i `.glazeui`).
  **`notifyContentInsets()`** anropas av `setVisible`: chromets höjd eller
  innehållet KAN ha ändrats (`AppWindow::contentInsetsChanged` — `Manual`
  synkront och gratis utan begärd innehållsstorlek, en anpassad axel POSTAD).
  **`relayoutSiblings()`** likaså: en remsa vid en kant eller ett barn till en
  `placesChildren`-förälder flyttar SYSKONEN, som inte är dirty — föräldern
  får `childLayoutChanged` och invalideras (är den `root_`, hela fönstret;
  dock_gate F11). **TRÄDKROKARNA**: `adopt(Widget&)` kör `childAdded(barn)`
  direkt efter länkningen och FÖRE invalideringen, `detach` kör
  `childRemoved(barn)` efter `setOwnerForTree(nullptr)` — barnet är ute ur
  listan men ännu inte förstört.
- **`sash.h/.cpp`** (INTERN) — SKIKTET, delat av `Splitter` och
  `DockHost`: `sash::draw` (familjernas form — Glaze en greppstav på en
  hårlinje, Slate/Minimalistic en linje som tänds i `accent`),
  `sash::cursor` (↔/↕), och de två räknestegen `paneFromPointer` (pekaren
  → rutans mått, med greppets offset så skiktet inte hoppar) och
  `clampPane` (minimimåtten; den fasta rutans vinner när båda inte ryms).
- **`ease.h`** (INTERN, E2) — glidningarnas kurva, kamerans och dockningens.
- **`text_field.h/.cpp`** (INTERN) — TEXTFÄLTET, delat av `TextInput` och
  `TextArea` (`sash.h`-prejudikatet): `drawSurface` (ytan och kanten per
  familj med `switch (c.ui)` och de fyra tillståndens tabeller; svaret är
  markeringens färgpar), `drawFocusRing`, `drawRun` (EN textrad med valfri
  markering i tre segment — TextInputs rad och varje rad i det flerradiga
  fältet), `caretWidth` (aldrig under en fysisk px), UTF-8-gränserna
  (`nextBoundary`/`prevBoundary`/`floorBoundary` — den sista för en position
  ur en ANNAN text, `setText` i båda fälten), `wordAt` (dubbelklickets ord:
  ordtecken — ASCII-alnum, `_`, allt utanför ASCII — blanktecken eller
  skiljetecken i följd; ett ordtecken på EN av sidorna vinner, radbrytningen
  är en gräns) och FILTREN för det som SKRIVS och KLISTRAS (`singleLine` —
  TextInputs, `multiLine(text, keepTabs)` — CRLF/CR → LF, tabb → blanksteg
  utom med `keepTabs` (`TextArea::setAcceptsTab`) — och `unifyNewlines`, bara
  radsluten, för `TextArea::setText`). **TABBSTOPPEN** (REQ3_TRAD_LISTOR_FALT
  B8): `tabbedWidth`/`tabbedPrefix`/`tabbedIndexAtX`/`tabbedWrapLines` och
  `drawRun(…, tabW)` — en tabb flyttar x till NÄSTA multipel av `tabW` från
  den visuella radens början, bitarna mellan mäts med Draw (aldrig `c.font`),
  TABBEN BRYTER INTE, och utan `\t` eller med `tabW <= 0` är svaren EXAKT
  Draw:s (snabbvägen — det enradiga fältet rörs inte); kerning över en tabb
  finns inte. GEOMETRIN i övrigt är widgetens. Grindad headless i
  `textarea_gate` (filtren, ordet, gränserna, T1–T8). Också
  `editShortcut(mods)` — fältens genvägar A/C/X/V/Z/Y är
  Ctrl UTAN Alt (AltGr är Ctrl+Alt på Windows), på macOS Cmd utan Ctrl och
  Option (TODO 79), EN fråga för båda fälten — lösenordets `codepoints`/
  `byteOfCodepoint`/`maskBytes`/`maskInto` (en tom eller saknad mask ⇒ 0,
  ingen division), MAXLÄNGDEN `fitMaxLength` (kodpunkter; resten är texten
  utan markeringen, `ins` kapas, tomt när inget ryms, 0 = obegränsat — `resten
  >= max` prövas FÖRE subtraktionen) och ÅNGRA-HISTORIKEN `History`: differenser (position,
  borttaget, infogat, markeringen FÖRE) i en `deque`; `record` kapar gör
  om-svansen, slår ihop efter ORD (skrivande sammanhängande och utan
  ordtecken direkt efter blanksteg/`\n`; backsteg respektive Delete i följd;
  `Block` är eget och förseglar) och håller djupet 100 och budgeten 4 MiB
  (det nyaste steget behålls alltid) — talen i det anonyma blocket, inte i
  Style. `undo`/`redo` förseglar och svarar med en `Restore`, som fältet
  utför i sin egen väg. Grindad i `textfield_gate` H1.
- **`date_model.h`** (INTERN, header-only) — DATUMVÄLJARENS MODELL, REN STD:
  giltigheten (år 1–9999 — prövat FÖRE `std::chrono::year`, som bara bär
  ±32767), ISO-texten åt båda hållen (`iso`/`läsIso` — generös med blanksteg
  och ensiffrigt, strikt med året och avskiljaren), klampningen (en övre
  gräns under den undre räknas som den undre), dag- och månadssteg (31 jan +
  1 månad = sista feb), veckans första dag ur språkets text, månadsrutnätets
  FÖRSTA RUTA och `idag()` ur `localtime` (ingen tidszonsdatabas krävs).
  ÖVERSIKTERNA: tolv rutor (`kOversiktKolumner` × `kOversiktRader`, statiskt
  kontrollerat), `decennium`, `åretIRuta`, `åretFinns` och `månadenInom`/
  `åretInom` ("släckt" i översikten). KLOCKSLAGET: `tid`/`hhmm`/`läsTid`/
  `läsIsoTid`/`nuMinuter`, i minuter. Grindat headless i `datepicker_gate`
  mot ett handskrivet facit.
- **`dialog_layout.h`** (INTERN) — modaldialogernas
  gemensamma layout, utan tillstånd: `placeButtonRow(c, rad)` (bredderna med
  `Button`s formel, golvade av `msgBtnMinW`, högerställda med KUMULATIVA
  `right`-offsets — samma offset hade lagt dem ovanpå varandra) och
  `lockSize(fönster, w, h)` (`setContentSize` talar INNEHÅLLSYTAN men
  min/max KLIENTSTORLEK, så låsningen går via `clientSize()` EFTER
  `setContentSize`; avrundat UPPÅT, för truncering klipper sista radens
  glyfer). Meddelanderutan, inmatningsrutan och färgdialogen.
- **`drag_scroll.h`** (INTERN) — autoskrollens timer-id
  `kDragScrollTimerId` = `0x80000003` i ramverkets höga intervall (caret
  0x80000001, tooltip 0x80000002 — appens `Window::startTimer`-id ligger
  under); i en header för att `tree_gate` Q8 injicerar ett `Timer`-event med
  det. Mekaniken bor i `drag_drop.cpp`.
- **`prop_parse.h`** (internt) — delade parse-hjälpare för setProp
  (splitComma/parseBool/parseSize/parseDock/parseAnchors/parseAlign/
  **parseImg**/**parseNumber**), nådda som `"../prop_parse.h"`.
  **`parseInt`** är `parseDouble`s regler + ett HELT tal inom `int` (timerns
  och systemfältets `interval`): skräp är false och aldrig 0, och `out` rörs
  inte då.
  `parseNumber` är STRIKT: en C++-literal (valfritt `f`/`l`-suffix), klassisk
  locale, ändlig, inget skräp efter — `"12abc"` avvisas helt i stället för
  att bli 12 (`std::stof`-fällan); `SpinBox` har en egen literal-tolk, en
  känd dubblering. `parseImg` ("lista/nyckel" → `AssetHandle`, delad av
  `Button`, `Picture` och verktygsknappen) bor här av `parseAlign`s skäl.
  **`parseHexColor`/`hexColorText`** är färgens textform `#RRGGBB` — STRIKT
  (valfritt `#`, exakt sex hexsiffror — åtta, `#RRGGBBAA`, bara med
  alfaflaggan, som färgväljaren sätter — inga blanksteg, ingen kortform) —
  läst av färgväljaren och av `Label`s `color`-nyckel, som kräver `#`. **`parseDouble`/`parseDoublePair`** — `parseNumber`s regler i DOUBLE
  (ritytans världskoordinater: en SWEREF-koordinat får inte avrundas) och
  två tal ur ett setter-anrops argument (`setCenter(674000, 6580000)` →
  `"674000, 6580000"`); `parseNumber` är den avrundade formen.
- **`widgets/`** — komponenterna, en cpp var ([egen CLAUDE.md](widgets/CLAUDE.md)).
- **`color_panel.h/.cpp`**, **`color_dialog.cpp`** (+ **`color_dialog_window.h`**) och **`input_box.cpp`**
  (+ **`input_box_window.h`**) (INTERNA, FAS1_B_WIDGETS) — FÄRGYTAN (palett, R/G/B(+A)-reglage, hexfält;
  barnen "palette", "red" … "hex") som färgväljarens popup och färgdialogen DELAR (`setDialogMode`: paletten
  väljer utan att stänga, ytan ritar inget), och de två dialogerna på meddelanderutans prejudikat (knapparna
  bär svaret, måtten i `onLoad` genom `dialog_layout.h`, fönstret i en intern header för grindarna; färgdialogens
  OK committar FÖRST). Kanalernas `static_assert` räknar med `std::extent_v` — GCC 14 vägrar `std::size(medlem)`
  i ett konstant uttryck (det använder `this`). Inmatningsrutans fält är ett `TextInput` eller, med `InputBoxMode::MultiLine` (REQ8 22), ett `TextArea` (`TextArea::heightForRows`) — båda heter "field".
- **`dock/`** — dockningens modell, sträng och fönster ([egen CLAUDE.md](dock/CLAUDE.md)).
- **`canvas/`** — RITYTANS motor ([egen CLAUDE.md](canvas/CLAUDE.md));
  bildernas texturer bor i `image_cache.cpp`. **`CanvasResources` följer
  bildcachens livstid:** skapas i `showOwned` EFTER `images_`, `trim()` direkt
  efter bildcachens, rivs i `App::executeClose` FÖRE `images_`; `Ctx::canvas`.
- **`font_set.h/.cpp`** (INTERN, REQ7_MARKDOWN) — TEXTFONTERNA bakom `Ctx::textFonts`: storlek × UI/Mono × stil, SLUTEN
  men LAT (laddas vid första frågan, en ny px laddar om); skapas i `showOwned`, rivs i `App::executeClose` med `font_`.
  FRIA STORLEKAR (REQ8 26) bredvid: `sized(r, px, familj, stil)` en per (fysisk px, familj, stil) — familjen och stilen kom med `Draw::withFont` (REQ9_TERMINAL B14), taket `kFriaMax` (8) släpps BARA i `trim()`
  överst i `renderContent`. `textSizeValue` (sättarnas regel) och `sizedText` (= `Draw::withFont` i UI/Regular — bara
  TEXTEN, aldrig `resolve`, som mäter syskonen) bär fältens och listornas `setTextSize`.
- **`markdown/`** — Markdown-vyns tolkning och layout ([egen CLAUDE.md](markdown/CLAUDE.md)).
- **`language.cpp`** (+ den GENERERADE **`lang_builtin.{h,cpp}`**) —
  SPRÅKBUTIKEN: fristående Meyers-singleton med `App::themeState`s precedens
  (den lever FÖRE App — ett `tr()` i en fönster-ctor får inte konstruera App).
  **TVÅ BUTIKER, och ROUTINGEN är låset på 1–999:** `builtin` (ramverkets
  id, ur `lang_builtin.cpp`, verifierade med `requireFrameworkScope`) och
  `app` (1000+, ur `loadLanguage`, `requireAppScope`). `tr(id)` väljer butik
  på TALET och aldrig på filens påståenden, så en appfil kan inte skugga en
  ramverkssträng. Uppslagskedjan är AKTIVT SPRÅK → FALLBACKEN → `⟦id⟧` + EN
  loggrad per id (`loggedMisses`). Startspråket är EXPLICIT →
  `gfx::systemLocale()` → engelska, upplöst vid FÖRSTA uppslaget.
  `setFallbackLanguage` kräver att taggen är KOMPLETT i BÅDA butikerna, och
  `loadLanguage` omvaliderar ATOMISKT: gör en ny appfil fallbacken
  ofullständig avslås FILEN. Butiken håller bara INDEX in i filens
  id-sorterade entrytabeller. `TrText`-metoderna bor också här — cachen
  jämför mot generationsräknaren och skriver om `resolved_` IN PLACE, vilket
  gör `str()`-referensen giltig över ett språkbyte. **`lang_builtin.cpp` är
  GENERERAD av `glazelanguage embed` och committad**; `lang_gate` del 2 mäter
  att den är BYTE-IDENTISK med `Glaze/language/language.bin`.
- **`assets.cpp`** — asset-storen: fristående Meyers-singleton (themeState-
  precedensen — lever före/efter App), äger `glzassets::File` (råbytes) +
  logiska tabellen handtag → post; `loadAssets` (basePath-kandidater, EN
  loggrad vid miss), `img` (miss loggas EN gång per nyckel), `assetSizeClass`,
  intern `detail::assetBytes` (sömmen till cachen). RAMVERKETS butik
  (PROGRAMIKON B4) ligger bredvid, LAT, ur `kFrameworkAssetsBin`
  (`assets_builtin.{h,cpp}`, GENERERAD av `glazeassets embed` och committad
  som `lang_builtin`): id `0x80000000 | (index+1)`, `detail::builtinAsset`,
  routning i `assetBytes`/`assetSizeClass`, och `detail::assetGeneration()`
  stegas av varje `loadAssets`.
- **`image_cache.h/.cpp`** (INTERN header — till skillnad från `glaze/draw.h`)
  — per-FÖNSTER texturcache: `get(handle)` avkodar LAZILY via
  `Renderer::loadImage (bytes)`; NEGATIV cachning (trasig asset =
  nullptr-post, inte per-frame- försök). Livstid: skapas efter `font_` i
  showOwned, **rivs FÖRE `font_` i App::executeClose** (texturer hör till
  renderern — orubbliga ordningen). **RITYTANS BILDKÄLLOR** (CANVAS_2D etapp
  3/6, typerna i [canvas/](canvas/CLAUDE.md)): `get(const Bitmap&)` laddar upp
  en ny bild hel och en ändrad bara de rektanglar ändringsloggen bär sedan
  TEXTURENS version (annars hela); `get(const RenderTexture&, const Ctx&)`
  skapar målet och MÅLAR (pushTarget → funktionen med ett Ctx över målets yta
  → popTarget) när versionen är ny — med rekursionsvakt. Båda under en
  BYTE-BUDGET med LRU och, för bilderna, en UPPLADDNINGSBUDGET per frame
  (`App::setCanvasUploadBudget`, förval 16 MiB, CANVAS_3D §3.5) — DELAD med
  ritytans meshar, instanser och punkter via
  `uploadRoom`/`spendUpload`/`deferUpload` (canvas/residency.h). Framens
  första uppladdning går alltid igenom, och det som är kvar räknas med `min`,
  så en första uppladdning över budgeten lämnar noll kvar i stället för att
  slå runt. **`trim(förvalsbudget, uppladdningsbudget)` är framens början och
  ENDA vräkningsstället** — budgetarna kommer ur App via `renderContent`, så
  cachen känner aldrig App. Den släpper döda källor (`weak_ptr` utgången),
  flyttar döda kontons poster till fönstret och vräker, PER KONTO över sin
  budget, de som legat oritade längst — aldrig de som ritades i förra framen.
  `uploadsDeferred()` säger att något väntar på nästa frame. **`contentGeneration()`** (PRESTANDA C2) stegas när
  någon textur skapas, kommer ikapp, vräks eller släpps eller en render-textur målas — draperingens markstämpel
  läser den i stället för varje bilds version. Nyckeln är källans id. En Bitmap och en
  RenderTexture har MIP-KEDJA, och en Bitmap över `maxTextureSize` är RUTOR i SAMMA post (`tiles`, REQ8 5). **KONTONA** (REQ3 10,
  `Canvas::setMemoryBudget`): en rityta som ritar öppnar sitt konto
  (`beginAccount(scenens id, scenen som livstecken, budget)` via RAII-typen
  `detail::ImageAccount`, canvas/scene.h — i ritytans tre ritvägar), och allt
  som hämtas medan det är öppet räknas på den rityta som RITADE det senast;
  utanför ritytorna konto 0, fönstret, med appens förval (`budget` 0 =
  förvalet). Draperingarna läggs på kontot med `setAccountExtra`: `trim`
  jämför bilderna PLUS dem mot budgeten men vräker bara bilder. All räkning går
  genom EN väg, `book(postens konto, postens byte, konto, byte)` — en ny post,
  ny storlek, en annan rityta, en borttagning (byte 0) — med snabbväg när
  inget ändrats (inga kontouppslag per bild och frame). **BILDER SOM SLÄPPER
  SINA PIXLAR** (REQ3 9, `Bitmap::setKeepPixels(false)`): `get(Bitmap)` köar
  bilden (`detail::queuePixelRelease`, med den uppladdade versionen) när
  texturen är HEL och AKTUELL, och App släpper efter ritloopen. Utan pixlar
  blir en ny post NEGATIV (en tom buffert hade blivit en svart textur), ett
  fönster en version efter står kvar, båda loggade en gång per bild
  (`missLogged`); en släppt eller KÖAD bild och dess omräkningar vräks ALDRIG
  (`pinned` — de går inte att ladda upp igen, och under Windows modala
  storleksloop kan flera frames gå före svansen) men räknas i budgeten.
  **`get(ImageSource, Ctx)`** är widgetarnas uppslag — en switch till de tre.
  **`getResampled(källa, w, h)`** skalar med `glzassets::resizeRgba` och
  laddar upp EN textur per (id, w, h) — `Draw::glyphResampled`s väg: en asset
  avkodad en gång; en Bitmap ur sina LEVANDE pixlar, omräknad vid ny version,
  i en egen tabell (version + `weak_ptr`) i bytebokföringen,
  uppladdningsbudgeten och LRU:n — utan pixlar svarar den med den textur den
  har, annars nullptr —; en RenderTexture svarar nullptr. `sourceSize` svarar
  källans mått för valregeln, ur handtaget.
- **`draw.cpp`** — implementationen av **`Draw`** (publika headern är
  [../include/glaze/draw.h](../include/glaze/draw.h)): RITANDET och inget
  annat. **Inga konstanter, inga mått, inga färger** — allt sådant kommer in
  som argument. Det ENDA filen vet på egen hand är DPI: `phys()` är det enda
  stället widget-relativa LOGISKA px blir absoluta FYSISKA (`c.box` +
  `c.scale`), och det är därför ritkod aldrig innehåller `resolve()` eller `*
  c.scale`. Primitiverna listas vid `draw.h` i include-CLAUDE.md; här det
  implementationen bär: `laminate` tar andelarna och färgerna som PARAMETRAR
  (Draw känner aldrig en widgets Style-fält); glyf-formernas proportioner är
  TECKNENS definition (rutan som skickas in är hela måttet); `elide` ritar en
  text som ryms orörd, annars prefixet ur `c.font->fitWidth` för bredden minus
  ellipsen (fysiskt, som `wrapLines`) utan avslutande blanksteg + U+2026, och
  tomt när inte ens ellipsen ryms; radbrytningen ligger ovanpå
  `gfx::wrapText` och `measureWrapped`/`textWrapped` delar `wrapLines`, så de
  aldrig blir oense; `elideLines` bryter varje `\n`-stycke genom TU-lokala
  `ordrader` (en rad som `wrapText` HÅRDBRÖT förlängs till ordets slut och
  kortas i stället), och den sista synliga raden bär RESTEN av sitt stycke;
  `clip(c, r, radius)` med radie är EXAKT den form `rect(c, r, radius, …)`
  fyller (0 kostar ingenting); och Scrollbar och Panels overlay-remsor ritar
  SAMMA pixlar genom `scrollThumb`/`scrollThumbDraw`.
  **`glyphResampled`** ritar 1:1 i fysiska px ur `getResampled`; en
  RenderTexture — och en Bitmap som släppt sina pixlar utan omräknad textur i
  fönstret (REQ3 9) — går till `glyph` och GPU:ns filter.
  **IKONENS OPACITET:** `glyph` tar en opacitet och
  skickar den till `Renderer::drawImage`, och `content`s bild går genom den
  TU-lokala `contentGlyph` med `Content::imgOpacity` (inaktivt läge).
  Ingenting ritas OVANPÅ bilden — en slöja i ytans färg syns som en ruta på
  varje yta utom den den är avstämd mot (`toolbar_gate`, `pixel_gate`).
  **`badge`** — drag-and-drops effektmärke: `circle` + ett `Sign` ur
  `plus`/`minus`/`chevron` i skivans mittersta halva. **`frameGap`** —
  konturen med en lucka i överkanten, ritad TRE gånger under tre RECT-klipp
  (vänster om luckan, höger om, under den) eftersom Draw aldrig har ett
  HÅL-klipp. **Luckans kanter SNÄPPS TILL HELA FYSISKA PIXLAR** (scissorn
  rundar UTÅT, och två klipp som möts mitt i en pixel ritar nederkanten
  dubbelt — en ljusare prick; `groupbox_gate`), och luckan KAPAS mot
  överkantens raka del, innanför sidlinjen och förbi hörnbågen, avrundad INÅT
  — en lucka som når ett hörn tar annars sidlinjens övre halva. `penWidth` är
  pennans logiska bredd (hårlinjen 1/skala) för den som ska MÖTA en linje
  (`tab_gate` P1–P3). **`icon`** — de ritade ikonerna (`Icon`) i en
  enhetskvadrat: raka konturer `frame` (SDF-hörn utan skarvar), sneda och
  öppna streck `line`; proportionerna står här och bara här.
- **`style.cpp`** — MÅTT-tabellerna + uppslaget `styleCount/styleAt/
  defaultStyle(UI)`: HÄR bor varje tal som styr utseendet, i logiska px, per UI
  (`kSlateRegular` / `kGlazeRegular` / `kMinimalisticRegular`). Ett mått som
  FLERA widgets läser får inte heta `<widget>…` (`sheenLower`/`sheenUpper` i
  den delade sektionen — fem widgets laminerar); omvänt fick sliderns tumme
  EGNA `sliderShadowDy/Blur`. `kSlateRegular` är BASEN och skrivs ut i sin
  helhet; **de två andra är DELTAN** (`Style m = kSlateRegular;` + de rader en
  färdigdesignad widget äger), så tabellerna inte kan glida isär vid nästa
  nya fält — en implementationsdetalj, inte ett släktskap i modellen.
  **`uiFontSize`** hör hit och inte till `gfx::fontsize::kUi`: radhöjden sätter
  golvet för varje kontrollhöjd. Ett nytt mått = ett nytt fält i `Style` + en
  rad i varje tabell som avviker. **`msg*`-måtten** hör hit trots att
  meddelanderutan är ett FÖNSTER — ett tal i `message_box.cpp` hade kopplat
  bort Style-axeln för dialogen. Remsornas fyra AUTO-längder
  (`*DefaultLen`) är AVSIKTLIGT samma tal men fyra fält (noten under
  `kSlateRegular`): en tätare stil ska kunna korta dem var för sig.
  Komponenternas tal har sina skäl VID talen (verktygsfältets härledda par:
  `toolbar_gate`), och blocken står i passens ordning, det senaste SIST.
- **`app_window.cpp`** — fönstrets liv: `show`/`showOwned` (init-blocket:
  OS-fönster, skärmens mått (`measureDisplay` — `effectiveDisplay` ovanpå
  backendens `displayMetrics`, invalidering vid ändring, och EN loggrad per
  skärm-id och process när måttet är antaget; körs också vid
  `WindowDisplayChanged`/`WindowDisplayScaleChanged` och ur App vid
  kalibrering), font, callbacks, registrering i den lat-skapade App:n;
  åter-show av dolt fönster = `setVisible(true)`), `hide()` (dölj utan
  stängning). **DOLD START** (regeln i rot-CLAUDE.md): `hidden_` →
  `desc.hidden`, nollad av `App::executeClose`. **DEN FÖRSTA VISNINGEN** (TODO 80): `showOwned` skapar
  fönstret DOLT utom popup/offscreen, fyrar `onLoad` (sedan livstecknet OCH
  fönstrets id) och visar med `revealHidden`, startplatsen omräknad; under
  `firstShowPending_` är plats och läge startvärden och läsarna begäran. Ett
  vetat `close()` ur `onLoad` fortsätter till visningen (`flushPendingCloses`),
  och ett ägt fönster som aktiverades under `onLoad` får aktiveringen tillbaka
  efter ägarens första visning (`loadActivated_`).
  **SKÄLET** (TODO 72): `requestClose(skäl)` bakom `close()`, `AppQuit`
  uppgraderar. **APPENS MUS** (TODO 86): `AppPress` per knapp bakom
  `mouseTarget`/`appMouseDown`/`appPressStep`/`appMouseUp`/`appMouseCancel`
  (kontrakten i headern); `AppPress::end()` är EN nollställning (pekaren
  tillbaka, `taken` av, `seq` stegad — också för ett tryck vars `onMouseDown`
  kör just nu), som `forgetWidgets`/`detachTree`/`showOwned` kör tyst, och
  hover-kroken hoppar över målet. `showInTaskbar_`, `toolWindow_` och de
  flytande dockningsfönstrens `noActivate_`/`showInactive_` kopieras till
  `WindowDesc`; `setShowInTaskbar` efter show loggar.
  **`run()`/`run(owner)`/`runInternal`** (`isModal_` pushar modalstacken;
  `run(owner)` returnerar `result_` ur `closeWithResult`), `close()`
  (uppskjuten), **livscykel-avfyrningarna** (`fireLifecycle`/
  `fireCloseCancelled` — sender = `&root_`; onLoad EN gång per objekt via
  `loadFired_`; `Handler` konverterar MEDVETET inte till `std::function`),
  **`setTopmost`/`topmost()`** (gettern läser OS-FÖNSTRET när det är öppet, så
  Waylands degradering syns). **`pushHitRegions`**: resize-zonerna INNANFÖR
  klientytan läses bara av Linux utan storleksmarginal — D3D11, Metal och
  Linux med marginal har ramen UTANFÖR. Och `handleEvent()`:
  hover/press/fokusring/mus-drag = per-widget-damage; körd ANVÄNDAR-handler =
  FULL `invalidate()`. **Träd-vandringarna** (EN sanning om geometrin):
  `drawTree`/`drawTreePartial`, `collectDamage`, `widgetAtRec`, `ctxFor` —
  behavior-krokarna får sitt Ctx därifrån, aldrig rå `makeCtx`. Ritningen tar
  `frontChildOf(w)` SIST, `widgetAtRec` FÖRST (E2); en TOM rekt (`hasArea`,
  REQ9 6) ritar inte sig själv (inte heller overlay-lagret, men klippen poppas)
  — barnen ändå; `stepTree` kör `frameStep` före skadan. **TEXT VINNER ÖVER GENVÄGEN** (REQ3_LAYOUT_FONSTER
  B18-3, Qt:s `ShortcutOverride`): ett `KeyDown` som SKRIVER (bär `e.text`,
  också i `KeyArgs::text`) i en fokuserad, synlig och aktiverad widget med
  `wantsTextInput()` når varken genvägarna, reserven eller standardknappen —
  AltGr+2 (`@`) kör ingen Ctrl+Alt+2; Ctrl+S, F5, Delete och Enter har ingen
  text och går vidare, och ett skrivskyddat fält släpper en bokstavsgenväg.
  Annars den PUBLIKA `tryShortcut` (menyraden, sedan programmenyn —
  `DockFloatWindow`s reserv frågar ägarens), sedan
  `acceleratorFallback`, sedan STANDARD- OCH AVBRYTKNAPPEN och sist Tab:
  Return utan Ctrl/Alt/Cmd och Escape med `mods == 0` erbjuds FÖRST en öppen
  popups kontroll (`openPopupKeyTarget`, genom ombudet `proxyTarget` som
  `setFocus` delar), sedan `liveAccept()`/`liveCancel()` — en `usable` knapp
  får `performClick()`. Utseendets kandidater är accept-knappen och den
  fokuserade `Button`: `defaultButtonChanged` i sättarna och `setFocus`,
  `defaultButtonMayChange` ur basens sättare, `adopt` och `detachTree`. `notifyOwnedVisibility` ger de ägda
  fönstren `ownerVisibilityChanged`, och `forgetWidgets` släpper varje
  widgetpekare utan att röra en widget. **Fokusordningen** (`collectFocusable`
  + `moveFocus`): djupet-först (dolda/inaktiverade/icke-fokuserbara hoppas
  över, och ingen går in i en widget med `showsChildren() == false`), sedan
  STABILT sorterad på `tabIndex`, där de auto-satta (`-1`) bär sin
  **DFS-POSITION** som nyckel — räknad per Tab-tryck. **FOKUSHÄNDELSERNA**:
  SIST i `setFocus` lägger `postFocusEvents(från, till)` de två kedjornas — efter att den SYNKRONT gett
  föräldern till varje widget vars fokus inom bytte `childFocusChanged` (REQ8 9, ingen användarkod) —
  `onFocusLost` (innerst först) och `onFocus` (ytterst först), var och en med
  en `HandlerGuard`, SIST i fönstrets kö — en `shared_ptr<deque>` som varje
  postad leverans bär, så den överlever fönstret; `deliverFocusEvents` tar
  bort händelsen FÖRE handlern, så en nästlad pump fortsätter där den yttre
  var. Ett fält som committar i `focusLost` har redan postat sitt (commit före
  fokus); `detachTree` anropar `setFocus(nullptr)` före mutationen.
  `WindowFocusLost`/`Gained` fyrar inget. **`commitEdits()`**: vakter FÖRST;
  medan `commitDepth_` > 0 lägger `postEvent` i fönstrets FIFO `commitQueue_`,
  tömd framifrån före retur, av en rekursiv `commitEdits()` och av pumpens
  svans (`drainCommitQueues`); djupet ner via RAII, omritning bara om
  `handlerRuns_` rört sig. **Dispatchen** frågar fönstrets livstecken efter
  varje krok med användarkod. **KOMPOSITERNAS TVÅ VÄGAR IN**: `setFocus`
  börjar med `proxyTarget` (`focusProxy()` följs så länge ombudet är en
  STRIKT ättling), och `nextFocus(dir)` letar startstationen uppåt genom
  `parent()` till närmaste post i `focusOrder()` — fältet inuti en komposit
  står inte själv i listan. **DRAG-AND-DROP i `handleEvent`**: EN rad per
  händelsetyp in i sessionen — `MouseButtonDown` armerar en
  `allowDrag`-källa (`dragArm`) och sväljer andra knappar under ett aktivt
  drag, `MouseMotion` tar draget FÖRE hover och `mouseDrag` (ett
  Pending-tryck som passerar tröskeln BLIR ett drag där), `MouseWheel` rullar
  och gör om målvalet på samma punkt, `MouseButtonUp` avslutar draget utan
  klick, `MouseLeave` lämnar målet (`dragOutside`), `KeyDown` är MODAL under
  draget (Escape avbryter, allt annat sväljs), och `WindowFocusLost` avbryter
  som Escape. **SLÄPPET**: `MouseButtonUp` (vänster) levererar `mouseUp`
  först till en explicit fångande widget (om den inte är den tryckta) och
  sedan till den TRYCKTA — `pressedWidget_` läses först NÄR den behövs och
  prövas efter kroken — och `clicked()` bara om den finns kvar och pekaren
  står över den; true ⇒ allt ritas om, annars widgetens yta.
  **AVBROTTET är EN väg, `dropPointer()`**, för `WindowFocusLost`,
  `MouseCaptureLost` (grab, fokus, caret orörda) och `takePointerCapture`:
  mottagarna samlas med vakter, ALL bokföring nollas (tre knappar, capturen,
  `buttonsDown_`), drag-and-drop-avbrottets handlers körs, och SIST får varje
  mottagare vars vakt står sig och som inte tagit trycket igen
  `mouseCancel(knapp)` (en fångande Left med vänsterknappen nere, annars None).
  `takePointerCapture` för över knappmasken, sätter `capturedWidget_` FÖRE de
  andra fönstrens krokar och återvaliderar efter VARJE (öppet, `w` kvar,
  `hasMouseCapture`) — bryter och städar annars. Privata **`updateCursor(over, x, y)`**: väntepekaren (`useWaitCursor_`) slår allt; annars den fångande,
  den tryckta, den som håller mitten- eller högertrycket eller widgeten under pekaren —
  dess SATTA form (`cursor_`) före `cursorAt`, pilen när inaktiverad — efter
  rörelse, tryck och släpp; `MouseLeave` ger pilen (väntepekaren) när inget
  tryck pågår. **`refreshCursor()`** kör samma väg med den senast kända
  positionen (`lastMouse_`/`mouseInside_`, satta i modifierar-switchen). Overlay-callbacken ritar
  appens `overlay()`, `drawWidgetOverlays`, `drawDragFeedback` och
  damage-debuggen i den ordningen, och `showOwned` nollar sessionen.
  **MITTEN- OCH HÖGERKNAPPEN** har EGEN bokföring (`middleWidget_`/`rightWidget_` =
  `auxWidget(b)`): `MouseButtonDown` med `Middle`/`Right` (efter grab-outside,
  `cancelLostPress` och appens `onMouseDown`) går till **`routeAuxDown`** —
  hjulets bubbling, inaktiverade hoppas över, roten frågas inte, vid capture
  bara den fångande — och ett OTAGET högertryck till `fireContextMenu`. Den tillfrågade är
  PRELIMINÄR mottagare under sin krok, och `auxSeq(b)` (stegad vid start,
  släpp, `dropPointer`, `forgetWidgets`) avslöjar ett tryck som tog slut under
  kroken: inget återställs, och svaret false ger varken meny eller räkning.
  `middleDrag`/`rightDrag` går ÖVERST i `MouseMotion` (en fångande som fått en
  av dem får `mouseDrag` bara med vänsterknappen nere); `MouseButtonUp` nollar
  FÖRE `middleUp`/`rightUp` (true ⇒ full omritning), `detachTree`/`showOwned` tyst.
  **`drawWidgetOverlays`** frågar först billigt (`isVisible`/`hasOverlay`)
  och bygger sedan per träff klippet med RENDERERNS stack: basklippet och
  förfäderna yttersta först med `clipRect`/`clipRadius`, barnens area ur
  `childArea` — `drawTree`s steg (`middle_gate`). **`detachTree` städar
  sessionens två pekare** utan att köra användarkod: målet får bara sin
  `dragLeave`-KROK, en källa som lämnar avslutar draget tyst (en
  Finishing-fas behåller sitt släpp men tappar källan; `dnd_gate` 14–15).
  **NUMPADENS VIKNING** — regeln i rot-CLAUDE.md.
  **FÖRFADERSLOOPEN**: en tangent den fokuserade
  widgeten tackade nej till går till `descendantKeyDown` på dess synliga och
  aktiverade förfäder, närmast först, och FÖRE menyradens acceleratorer —
  fokus närmast vinner, så flikkontrollens Ctrl+Tab slår en meny som binder
  samma tangent. Loopen bryter DIREKT vid träffen och läser aldrig
  `f->parent()` efter anropet (en handler kan ha tagit förfadern ur trädet).
  **Visningen** (`showOwned`) kör `languageChangedTree()` före livscykeln —
  språket kan ha bytts medan fönstret var stängt. **MODIFIERARNA och
  DUBBELKLICKEN**: överst i `handleEvent`, FÖRE appens `event()`-krok, sparas
  `e.mods` ur varje mus-, tangent-, hjul- och gestevent i `modifiers_` och
  knapparna i `buttonsDown_`. **`countClick(clicks_[knapp], …)`** jämför
  trycket med knappens förra — samma widget, inom `gfx::doubleClickMs()`,
  avståndet i SKÄRMENS rymd inom `dragThreshold × uiScale` avbildat genom
  `clientToScreen` (macOS: punkter — TODO 97) — och
  dubbelklicket anropas EFTER tryckets krok (mitten och höger: på den som TOG
  trycket); `detachTree`/`forgetWidgets` nollar följden. **DE HÅLLNA
  TANGENTERNA** i ett eget switch-block FÖRE appens krok: `KeyDown`/`KeyUp` går till
  `setKeyHeld` (en bit per OVIKTAD `gfx::Key` i `heldKeys_`, storleken ur
  `gfx::kLastKey`; `Unknown` och värden utanför enumet ignoreras, och ett släpp
  släpper också numpadens andra läge — `numpadTwin`, tabellen i gfx/events.h),
  `WindowFocusLost` tömmer, liksom `hide()` och `App::executeClose`.
  `isKeyHeld` svarar exakt för den ovikta posten och för en DEDIKERAD tangent
  också när en hållen post viks till den (`gfx::navigationKey`). **TANGENTMINNET**
  (REQ2_INMATNING): `heldRaw` avgör där om ett `KeyDown` är en UPPREPNING, och
  ett `KeyUp` tar sin post (`takeKeyTarget` — `findKeyTarget`s uppslag,
  tangenten eller dess numpad-tvilling) FÖRE appens krok. Efter grab-toppen
  sparar `KeyDown` för ett NYTT tryck en `KeyTarget` (rå och vikt tangent,
  fokusenheten som `KeyChain` med vakter, mottagarens index, `skipped` vid en
  öppen popup) — en UPPREPNING kör postens kedja (`findKeyTarget`; utan post
  skapar den posten) — och kör `onKeyDown` innerst först tills `handled` (⇒
  `keyTaken`) eller en fokusflytt; `KeyUp` går genom `deliverKeyUp` till postens
  kedja, utan post till den aktuella enheten, och en popup för ett främmande
  släpp till ägaren (`forwardKeyUp`). `WindowFocusLost` och `hide()` ger
  `cancelled`-släpp (`releaseKeyTargets`) EFTER `dropPointer`. **`fullInvalidations_`**
  (`damageStamp`s fönsterdel) stegas i headerns inline `invalidate()`, som också
  `showOwned` går genom. Grind: `tests/plumbing_gate.cpp`; inmatningen `input_gate`.
  **STYRPLATTAN:** samma ställe sparar ett `MouseWheel`s `precise`/`phase`
  (`isPreciseWheel()`/`wheelPhase()`). **RULLNINGENS RIKTNING** (REQ9_INMATNING B10) på EN plats: hjuldispatchen
  räknar raderna PER WIDGET i `wheelFor` — Shift+mushjul till sidled (bara utan sidled i eventet) och widgetens
  upplösta `isWheelInverted` — utom för `followsWheelDirection() == false` (ritytan), som får dem råa. `EventType::Gesture` går till
  **`routeGesture`**: Begin väljer målet som hjulet (inaktiverade hoppas över,
  explicit capture frågar bara den fångande) och den första vars `gesture`
  svarar true HÅLLS i `gestureWidget_` — Update dit om den är aktiverad,
  End/Cancel alltid (fältet nollat FÖRE kroken). Update utan hållen sekvens
  levereras som en Begin; End/Cancel utan är ingenting. Under målvalet står
  kandidaten PRELIMINÄRT i fältet (kroken kan köra `onCameraChange`) och läses
  efteråt; `detachTree` och `showOwned` nollar det UTAN anrop.
  Gesten gömmer tooltipen och gör om drag-and-drops målval som hjulet; `isInputEvent`. Grind: `trackpad_gate` (M),
  riktningen `wheel_gate`. **Capture/grab-routing** och **timrarna**:
  `kCaretTimerId` (500 ms), tooltipens och `kDragScrollTimerId` →
  `dragScrollTick`. **MENYERNAS INGÅNGAR**: `MouseButtonDown` kör grab-outside
  för BÅDA knapparna (ett högertryck utanför en öppen meny stänger den också)
  medan capture, fokus, `pressedWidget_` och `mouseDown` stannar Left-only;
  `fireContextMenu()` är ROUTED LOOKUP uppåt efter första satta
  `onContextMenu`, med `over == nullptr` (tom fönsteryta) som start i roten.
  I `KeyDown` ligger `menuKeyHook_` FÖRST (en ÖPPEN meny är modal för
  tangentbordet; satt av `MenuPresentation`, nollad när kedjan stängs).
  **`renderContent()`**: full väg vid `fullDamage_`/`animating_`/
  `transparent_`/`!retainedValid()`/tröskel (>8 rects eller >60 % yta),
  annars partiell per damage-rect; chromet HELT utanför damage. ÖVERST
  `images_->trim(App::canvasMemoryBudget(), uppladdningsbudget)` — förvalet
  per rityta och fönstrets tak —, och en lokal vakt begär NÄSTA frame när en
  bild fick vänta på uppladdningsbudgeten, vilken väg framen än tog ut (varje
  frame laddar upp minst en bild, så kedjan tar slut; scene_pixel_gate B5,
  plumbing_gate W4). Scope-vakten `EfterFramen` kör `scheduleResizes()` och
  `fitDue()` efter framen, också den. **Innehållsytan och
  storleksgränserna** (kontraktet i rot-CLAUDE.md): `chromeInsetH()` är
  chromets höjd (TopBar bara vid Custom och synlig), `applyContentSize()`
  anropas från `showOwned`, `App::applyContentSizeAll` och
  `contentInsetsChanged`, och `showOwned` kopierar de EFFEKTIVA gränserna
  (`effMinW()` …, här i cpp:n — låsta, eller golvade i `GrowOnly`) till `WindowDesc`.
  GRÄNSER I INNEHÅLLSMÅTT (REQ8 21): `minInContent_`/`maxInContent_` bredvid måtten, `appMinH`/`appMaxH` lägger
  på `chromeInsetPx()` (EN avrundning) när de används, och `applyContentSize` skickar gränserna FÖRE storleken.
  POPUPEN SOM TAR TANGENTBORDET (REQ8 27): `forwardToKeyPopup` FÖRST i `handleEvent` skickar tangent, släpp och
  tecken till `openKeyPopup()` med dess `windowId` (biten i `keysToPopup_` = trycket gick bara dit, släppet sväljs);
  popupens Escape stänger sist före Tab. Färg- och datumväljarens popuper är vanliga `AppWindow` med attributet.
  `setClientSize` bor i cpp:n och går genom anpassningen. Fonten skapas via
  `reloadFont()` i `showOwned()`, laddas om vid `WindowDisplayScaleChanged`
  OCH vid byte av UI/Style (`App::reloadFontsAll`); storleken `uiFontSize ×
  uiScale()` jämförs mot `fontPx_` (oförändrad ⇒ no-op, så ett färgbyte aldrig
  river en atlas). **ROLLFONTERNA** (`headingFont_`) skapas EAGER i samma
  svep — mängden är SLUTEN, och fontskapande är dyrt — och måste rivas i SAMMA
  block som `font_` i `App::executeClose`: en atlas är en TEXTUR, och en
  kvarlevande rollfont kraschar teardown. `renderContent`/overlay-lambdan har
  font-vakter.
  **PROGRAMIKONEN** (PROGRAMIKON): `appIcon` adopteras SIST i ctorn (`name`,
  `inDockFlow_ = false`, ankare topp+vänster, på Mac topp+höger);
  **`isChromeRoot(w)`** — fallgropen i rot-CLAUDE.md; namnlistens motvikt är det interna
  `CaptionBalance : Spacer` med `contentSize = max(kantluft + kluster,
  reservedWidth(Title))`; `pushHitRegions` ger ikonen `HitRole::Client` före
  barens Caption; acceleratorerna frågar `appIcon.tryShortcut` EFTER
  menyraden; `applyWindowIcon` körs efter `win_->init` i `showOwned`, aldrig
  för en popup eller ett offscreen-fönster (glaze-render, grindarna).
  **RAMEN, PLATSEN OCH LÄGET** (FONSTER_OCH_TIMER): `applyChrome` bygger
  namnlisten LATT och flyttar den FÖRST i `root_.children_` (ikonen är inbyggd
  och kan inte `unadopt`:as); `showOwned` tillämpar `nextChrome_` före
  innehållsstorleken, räknar startplatsen (`startPlacementFor` — ägarytan bara
  för en öppen, synlig, inte minimerad ägare) och sätter `desc.startState`.
  **`onResize`** (REQ3_LAYOUT_FONSTER B4 — kontraktet i widget.h):
  UPPTÄCKTEN är `noteSize(w, box, skala)` i ritvandringarna direkt efter
  `resolve` (storleken finns bara där — ingen sidoeffekt i `resolve`, ingen
  användarkod), bara med satt handler: EXAKT jämförelse mot `resizeSeen_` (den
  senast LEVERERADE), köad med vakt och den RITADE storleken, som en ny ritning
  före svansen skriver över; en dold widget ritas inte, en i en inaktiv flik
  frågas `shownInTree`. Roten noteras med innehållsytan. LEVERANSEN postas
  efter framen i App:s RAMKÖ (`scheduleResizes`, en åt gången, livstecknet) — aldrig i
  render-callbacken, som på Windows och macOS körs inne i OS:ets storleksloop
  (där kommer händelsen därför vid släppet). `deliverResizes` hoppar över en
  post som inte längre lever, sitter i fönstret eller syns, vars
  `actualSize()` skiljer sig från den ritade (en tidigare handler rörde den —
  ingen oritad geometri) eller är den levererade; efter VARJE handler frågas
  livet, OS-fönstrets id och en begärd stängning, och efter en körd handler
  ritas fönstret om. **KEDJESPÄRREN**: omgångar i följd med en körd handler och
  ingen ny `App::eventSeq_` räknas; vid `kResizeChainMax` (64 — dockningens
  glidning tar ~38 bildrutor vid 240 Hz) levereras inget förrän nästa
  händelse, loggat en gång, och storleken skrivs INTE som sedd. Den FÖRSTA
  leveransen sker synkront i `revealHidden`s första visning, före första
  synliga bildrutan (`noteSizesIn` — ritvandringen utan ritning), med
  `onLoad`s regler: `hide()` ger en dold start, ett vetat `close()` kommer hit
  igen. `showOwned` nollar kön, kedjan och varje `resizeSeen_` (nytt liv,
  `first` igen). **FÖNSTRET FÖLJER INNEHÅLLET** (`setSizeToContent`, B16):
  `refit()` mäter `fitContent` (rotens barn utom menyraden och statusraden
  genom `extentOf`; bredden först, höjden med den kända bredden), avrundar
  UPPÅT, och ordningen är bärande — `fitWanted_` FÖRE `setSize` (D3D11 ritar
  synkront i `WM_SIZE`, och den nästlade ritningens fråga ska se det nya
  måttet) och de nya gränserna före storleken. Maximerat eller minimerat rörs
  inte (`WindowRestored` postar en ny); den FRIA axeln är begärans när en ny
  begäran väntar (`sizeRequestNew_`), annars fönstrets aktuella
  (`trackClientSize` — användarens drag vinner); 0 på en anpassad axel loggas
  en gång. LÄGET (REQ7_LAYOUT B2): låst är storleken och `fitClient*`
  innehållets; i `GrowOnly` är `fitClient*` innehållet (golvet, 0 utan mått),
  storleken begäran eller den aktuella inom `effMin*`/`effMax*`, och höjden i
  `WidthAndHeight` mäts vid max(fri bredd, innehållets).
  TRE LAGER: (1) `contentInsetsChanged` → `postRefit` (en åt gången,
  i samma pumpvarv före framen), (2) `fitDue()` efter varje frame jämför det
  oavrundade måttet på de anpassade axlarna, (3) `showOwned` efter init (först
  då finns fonten — före den gäller begäran rakt av) och `revealHidden` efter
  `onLoad`, före startplatsen. **AKTIVT FÖNSTER** (B18-1): `active_` sätts
  överst i `handleEvent` av `WindowFocusGained`/`Lost` på ägarkedjans rot — en
  popup blir aldrig aktiv (Waylands fokusgrupp ger popupens id) — och nollas
  av `hide()`, `showOwned` och `executeClose`; `isActive()` läser det.
- **`drag_drop.cpp`** — DRAG-AND-DROPS mekanik i TRE delar som hålls isär:
  **LASTEN** (`DragData`, format → bytes), **SESSIONEN** (`AppWindow::drag*`:
  Idle → Pending → Active → Finishing, en rad per händelsetyp i
  `handleEvent`) och **ÅTERKOPPLINGEN** (`drawDragFeedback`, overlay-lagret:
  målets ram i `accent`/`dropLineW` klippt mot förfädernas klipp, märket
  `Draw::badge` och dragbilden — `ImageSource` i `sizeClass()` eller
  `iconFallback`; ram och märke LIKA i alla tre UI, plattan per UI). Fem
  saker att inte råka bryta: **(1) PEKARNA ÄR LIVSFARLIGA** — efter VARJE
  anrop ut (krok, handler, `canDragFrom`) prövas SESSIONENS fält, aldrig en
  lokal kopia (`detachTree` nollar pekarna när en handler unadopterar).
  **(2) KROKEN FÖRE HANDLERN** med samma args — UTOM SLÄPPET: `onDrop` först,
  sedan `drop` med FÄRSKT Ctx och källan omläst (nullptr om riven) bara om
  målet sitter kvar, `!handled` och effekten inte är None; ett mål rivet i
  handlern utan `handled` ger None. Inträdet är enter och over; `dragBegin`
  frågar `canDragFrom` vid TRYCKET före allt annat och sätter `starting` under
  starten (`Widget::isDragSource`, definierad här). Effekten valideras EFTER
  krok och handler (utanför masken ⇒ None, flera bitar ⇒ Copy · Move · Link),
  målet invalideras vid ÄNDRAD `dropEffect`. **(3) REGEL 1:** dragbilden
  följer pekaren med `invalidateOverlay()`, och efter `onDragOver` görs INGEN
  full omritning (per rörelse), medan start/enter/leave/drop/end ger den
  vanliga. **(4) FINISHING** täcker drop-/end-handlerna: ett nästlat
  `WindowFocusLost` (en modal i `onDrop`) avbryter inte ett pågående släpp.
  **(5) AUTOSKROLLEN** (REQ3_TRAD_LISTOR_FALT B4): `dragScrollChain(x, y,
  verkställ)` frågar `Widget::dragScroll` på widgeten under pekaren och dess
  förfäder UTOM roten (inaktiverade och dolda hoppas över, första som svarar
  true vinner — en inre lista vid sitt slut lämnar till den yttre panelen;
  utanför klientytan ingenting) och kör ingen användarkod. Kan något rulla
  startar rörelsen fönstrets timer (`kDragScrollTimerId`, `dragScrollMs`,
  `drag_.scrolling`); `dragScrollTick` rullar ett steg och gör sedan
  `dragMotion` på SAMMA punkt (hjulets regel — insättningslinjen och
  `onDragOver` följer med), och första ticket utan rullning, ett sent tick och
  sessionens slut stoppar timern. Tröskeln är EUKLIDISK (MER än
  `dragThreshold`), utanför klientytan finns inget mål, och efter `MouseLeave`
  står pekaren på −1e9. Grind: `dnd_gate`, `tree_gate` Q8–Q9.
- **`tooltip.h`** (INTERN) **+ `tooltip.cpp`** — TOOLTIPEN: plattan
  (`ToolTipBody`, `switch (c.ui)`, bryter mot `Style::toolTipMaxW`; `measure`
  är den ENDA mätningen, så storleken och radbrytningen aldrig går isär),
  popup-fönstret (`ToolTipWindow` — `Chrome::None` + `setPopup(true)`,
  stänger sig vid ett tryck och meddelar ägaren via `onClose`) och
  `AppWindow`s `toolTip*`-metoder (sessionen `tip_`, fönstret `tipWin_`, ett
  per ägare, lat; timer-id:t `kToolTipTimerId` i ramverkets höga intervall).
  Fem saker att inte råka bryta:
  **(1) POPUP, inte overlay** — en tooltip får gå utanför fönstret, och
  Wayland saknar skärmrymd, så placeringen går genom popup-kontraktet
  (`setPopupAnchor` med PEKARENS ZON: `toolTipOffsetUp` ovanför spetsen
  till `toolTipOffsetY` under). Zonens överkant får aldrig ligga PÅ spetsen:
  backenden avrundar topp och höjd var för sig, plattans nedersta rad kan
  då hamna under pekaren, ägaren får `MouseLeave` och tooltipen blinkar.
  **(2) ALDRIG medan en annan popup lever** — `closePopupsNow` i
  `showOwned` hade stängt listan eller menyn; `toolTipTimer` avstår.
  **(3) REGEL 1** — timern går BARA medan en visning väntar
  (`tip_.pending`), och en kvarglömd tick visar ingenting. **(4) TYSTNADEN** — tryck/tangent tystar källan
  (`tip_.blocked`) tills pekaren lämnat den; hjul, `MouseLeave` och
  fokustapp gömmer utan att tysta. **(5) PEKARNA** — `detachTree`, `hide()`
  och `showOwned` nollar sessionen, och `toolTipChanged` gömmer en visad
  tooltip vars text byts (den nya texten visas vid nästa vila). Uppslaget
  är ROUTAT (`toolTipSourceAt`: första förfader vars `toolTipAt` vid pekaren
  svarar) och frågas vid VARJE musrörelse, så kedjans Ctx räknas UPPIFRÅN i
  ETT pass (`ctxFor`s regel, sedan varje förälders `childArea`) i stället för
  ett `ctxFor` per förfader, som var kvadratiskt. KÄLLAN är widget OCH text
  (två delar = två källor), timern gör ett NYTT träffprov och källuppslag och
  visar bara samma källa, synlig med förfädernas `showsChildren`
  (`AppWindow::shownInTree`), och inaktiverade visar sin. En kortad `Label`
  (`elide`) svarar med sin text (widgets/CLAUDE.md). Grind: `tests/tooltip_gate.cpp`.
- **`timer.cpp`** + interna **`timer_registry.h`** — `glaze::Timer` och
  REGISTRET (skapat av första konstruktorn), med `TimerState` som enda vän:
  `noteDue` antecknar en körning EN gång med en STÄMPEL; `drain` tar posterna
  ur den DELADE listan en i taget och prövar körning och stämpel PRECIS före
  anropet (en nästlad pump levererar resten); en-gångs stoppas före handlern.
- **`profiler.cpp`** + interna **`profiler_hooks.h`** (PRESTANDA A1) — PROFILERAREN. Registret är en
  funktionslokal pekare som aldrig rivs (`defaultPool()`s prejudikat, DELAT LÄGE (2)): ringarna per fönster
  (nycklade på `const AppWindow*`, släppta i `profileWindowClosed` ur `App::executeClose`), pumpringen och
  etiketterna — namnen KOPIERAS in under registrets lås; varje tråd ett block via en `thread_local`-pekare,
  med eget okontenderat lås. `report()` med `snprintf`, fasnamnen gemener ASCII. KROKARNA (`profiler_hooks.h`,
  `detail::`): `PhaseScope(fas)`, `profilePumpBegin/End`, `profileFrameBegin/End(fönster, continued)` och
  `profileCount(räknare, n)`. Pumpen (`pumpUntilClosed`) mäter wait/events/posted/timers/commits/closes och
  ramar in `renderFrame()`: renderarens `stats()` läses före och efter som en DIFFERENS (aldrig
  `resetStats`), GPU-tidtagningen slås på i Begin och av i End (`takeGpuTimes` matchar på `frameSerial`), och
  `continued = wantsFrame() || hasPosted()` — det GLOBALA `hasPosted` är mätfelet i PERFORMANCE anm. 16.
  `app_window.cpp` mäter Content och Overlay och räknar DamageRects/FullDamage (full ⇒ 1); en bild utanför
  pumpen (D3D11:s `WM_SIZE`) får `outsidePump`. Ritytans faser och räknare: ../canvas/ och widgets/.
- **`print.cpp`** — `PrintJob` + TU-lokala `PrintProgressWindow`: en sida per pump-svans, rastrerad med ritytans motor (`beginOffscreen`, Ctx med sidans täthet), om tills ingen bild väntar.
- **`component.cpp`** — komponentregistret. **`window_placement.h`** (INTERN)
  — startplatsen och logisk ↔ placeringsrymd ur `gfx::screens()` (PRIMÄRENS
  skala, närmaste skärm, klämning mot arbetsytan); ren, grindad i geometry_gate P.
- **`gui_loader.cpp`** — widget-REGISTRET (Meyers-singleton: typnamn → fabrik +
  `type_info`), `makeWidget`, `registeredWidgetTypes()` och klassuppslaget
  `makeWidgetOfClass`/`widgetClassOf` (🐛 60, LATT avmanglat med `__cxa_demangle`).
  Events kopplas ALDRIG här och inga egenskaper sätts — behåll uppdelningen.
- **`menu_query.h`** (INTERN) — `menuPresentationHostedIn(rot)`, EN fråga om
  menymodulens levande presentationer, för borttagningens deny-check
  (också flikarnas `setPages`) och dockningsoverlayns lämna-prövning. En FRI
  FUNKTION och ingen `Widget`-virtual, för en presentation ägs av MODULEN;
  implementationen i `menu.cpp` mot `presentations()`. TIDSFÖNSTRET är TVÅ pumpvarv.
- **`menu_path.h`** (INTERN, PROGRAMIKON B6) — MENYSTIGEN, delad av MenuBar,
  AppIcon, TrayIcon och ToolButton (DOCKNING E4a; i `menu.cpp`): `descendMenu`
  svarar nullptr + logg för en UNDERKÄND stig (`!valid()`) — aldrig roten —
  plus `matchaSeg`/`loggaTypfel`, add-kropparna, posttillstånden och inline
  `itemAtRootPath` (posten där den tomma stigen är roten). MenuBar avvisar
  den tomma stigen själv; för de tre andra ÄR den roten.
- **`window_icon.h/.cpp`** (INTERN) — OS-IKONEN:
  `iconFrames(large, small, maxSide)` (`ImageSource`, svar med VÄRDE) ger en
  ram per `kIconSizes` upp till taket (fönstret alla, systemfältet 64) ur
  valregeln och `fitSquare` — cachad på (id, id, `assetGeneration()`, tak)
  BARA när båda är assets eller ogiltiga; en Bitmap räknas ur sina levande
  pixlar vid varje anrop, och en RenderTexture OCH en Bitmap som släppt sina
  pixlar (`setKeepPixels(false)`, VAKTEN i `avkoda`) avvisas — tomt, en logg
  per process. `iconFrameCacheSize()` är grindarnas diagnostik.
  `defaultIconLarge/Small()` är `glaze/icon`/`icon16`; `effectiveIcons` (egen bild, annars förvalet) är EN regel för namnlistens
  ritning och `osIcon` (ramar + `programFirst`) åt fönstret och TrayIcon. En standardikon rastreras i 256 i `osIconColors()` (app.h).
- **`stock_icon.cpp`** — STANDARDIKONERNA (REQ8 12): `kDefs` (en ritbeskrivning per `StockIcon`, ordningen `static_assert`ad; språket och delarna `kDelar` i filhuvudet; `K` stansar ut en text ur hela ikonen — filtypernas etikett — med glyferna i `kGlyfer` (A–Z, 0–9, en ruta på 3 × 5, nedskalad som helhet när den inte ryms), och en utstansad punkt är genomskinlig i ALLA lager), färgvariantens lager `kFärger` (färgord, `under`/`over`, `all`, och `strong` — tonen blandad mot textfärgen, så en utstansad etikett håller kontrasten i båda lägena), rastreringen (prov `kProv`² per px, ned med `glzassets::resizeRgba`) och `detail::stockBitmap` — EN `Bitmap` per (ikon, px, stil, färger), som `Draw::glyph` (rectets exakta px), `ImageCache::get(källa, ctx)` (klass × skala) och `window_icon.cpp` frågar. Arket: `tests/stockicon_sheet`.
- **`tray_icon.cpp`** (+ interna **`tray_model.h`**/**`tray_registry.h`**) —
  `glaze::TrayIcon`. `tray_model.h` är den RENA modellen, header-only:
  `TrayAnimation`, `routeTray` (dold ⇒ ingenting; hover-dubbletter sållas;
  klicken under en modal ⇒ `raiseModal`) och `trayMenuAnchor` (ikonens rect
  KLÄMD i arbetsytan, så menyn hamnar kant i kant med fältet).
  `tray_registry.h` är App:s ingångar (`trayDispatch(e, modalTop)`,
  `trayLanguageChanged()`) och grindens `trayIdOf`. I cpp:n REGISTRET (lägsta
  lediga id och en GENERATION per registrering), EN `sync` mot fasaden
  (synlig, stödd och nytt innehåll — `TrayBild` bär `ImageSource`; ramarna ur
  `osIcon(…, 64)`) och menyns värd **`TrayHostWindow`** (1 × 1, genomskinlig,
  utan chrome, `toolWindow_` som TAR fokus, placerad på ankarets hörn och
  lyft med `raise()`). Fyra regler står överst i filen: INGET gfx-anrop före
  `show()`; en handler FÅR riva ikonen (id OCH generation slås upp igen);
  menyn öppnas i PUMP-SVANSEN, efter värdens fokusbyte; värdarna ägs av
  REGISTRET och rivs aldrig medan något pekar dit (`~Menu` kör inte
  `onChainClosed`, så `~TrayIcon` stänger värden själv). Komponent (Inert)
  med `setProp` (`interval` via `parseInt`)/`setTextToken`. Grind:
  `tests/tray_gate.cpp` (`show()` bara med Linux-backenden).
- **`message_box.cpp`** (+ interna **`message_box_table.h`** och
  **`status_badge.h`**) — **`glaze::messageBox`**: ett vanligt MODALT
  `AppWindow` byggt av vanliga widgets, deklarerat helt i cpp:n (prejudikatet
  `Dropdown::ListWindow`), mitt över ägaren (`CenterOwner`). **SVARET BÄRS AV
  KNAPPARNA** (`setDialogResult`) och läses ur `dialogResult()` också på den
  ägarlösa vägen, där `run()` svarar med en exit-kod; Enter/Escape är
  fönstrets standard- och avbrytknapp (första knappen, avbryt-ekvivalenten —
  ingen för YesNo/AbortRetryIgnore). Knappraden och låsningen är
  `dialog_layout.h`s, och mätningen sker i **`onLoad`** (den kräver fonten,
  som skapas i `showOwned`). **`ButtonSpec::text` är ett ID**, och
  `onLanguageChanged()` kör om `layoutAndSize()` — rutan sätter EXPLICITA
  bredder en gång, så `invalidateAll()` ritar om men layoutar INTE om.
  **Tabellen** (`message_box_table.h`) är den enda REN STD-delen och grindas
  headless av `msgbox_gate` — knapparna per uppsättning, deras resultat och
  avbryt-ekvivalenten på ETT ställe. **Badgen** (`status_badge.h`, egen
  header för att `StatusBadge::contentSize()` ska gå att grinda — klassen är
  `final`, så grinden mäter med `Widget::measure` via en `Meter`-subklass)
  lyder regel 3 och ritar glyfen som TEXT via en Ctx-KOPIA med `headingFont`.
- **`file_dialog.cpp`** (+ interna **`file_dialog_window.h`** och
  **`file_dialog_model.h/.cpp`**) — **`glaze::FileDialog`**, filväljardialogen,
  på meddelanderutans prejudikat: ett MODALT `AppWindow` av vanliga widgets —
  adress- och namnfält (`TextInput`), platserna (`ListBox`), filerna
  (`ListView` — sortering, huvud, flerval och aktivering är dess), en
  `Splitter`, filtypslistan (`Dropdown`) och kryssrutan för dolda filer.
  Interna ättlingar: `FileList : ListView` (mapp/fil med `Draw::icon` genom
  Tables ikonkrokar; en uttryckligt satt `setIconSize` gäller också där),
  `PlacesList : ListBox` (ikon + namn i `drawRow`; `rightDown` svarar false —
  ett val i platslistan NAVIGERAR, och ett högerklick ska inte göra det),
  `IconButton : Button` (uppåtpilen, kvadratisk, MÄTT ur adressfältet),
  **`Buddy<Base>`** (en MALL: etiketterna och kryssrutan lika höga som sitt
  fält) och `DialogButton` (`fileDlgBtnMinW`). Fyra saker att inte råka bryta:
  **(1) MODELLEN ÄR REN STD** (`glaze::filedlg`): katalogläsningen
  (`listFolder` — mappar först, punktnamn dolda, filtret bara på filer,
  aldrig ett undantag), filtren (`globMatch` — `?` är ett TECKEN, ASCII utan
  skiftläge, "*"/"*.*" allt), ändelsen, namnfältets citatform, namnordningen,
  storleks- och datumtexten (decimaltecknet och BYTE-ENHETEN som argument —
  finskans "12 kt"), sorteringsnycklarna (RANGER med mapparna först per
  kolumn) och sökvägarna — grindat HEADLESS av `filedialog_gate`. Det OS:et
  döljer kommer in som en FUNKTIONSPEKARE (`gfx::isHiddenFile`), så modellen
  aldrig länkar gfx; **`showHidden`** visar punktnamnen OCH det pekaren hade
  dolt utan att fråga den, och står SIST med flit (en bool före pekaren hade
  tagit emot pekaren i ett missat anropsställe — och kompilerat).
  **(2) UTF-8 ÖVERALLT**: `pathFromUtf8`/`utf8FromPath` (`u8string`) är den
  enda övergången — `path::string()` går genom ANSI-teckentabellen på Windows
  och förstör åäö. **UNC-sökvägarna läses ur TEXTEN**: libstdc++ ger
  `\\server\resurs` inget rotnamn, kallar den relativ och `lexically_normal`
  slår ihop de två inledande avskiljarna (GCC 16), så `resolvePath`/
  `isRootFolder`/`nearestExisting` bär formen själva (resursen är en rot,
  `..` stannar där; H8 jämför `native()`, för libstdc++:s `==` håller
  `\server` och `\\server` för lika). **(3) FÖNSTRET STÅR I EN INTERN HEADER**
  (`file_dialog_window.h`) för grindens skull: grinden bygger det i en
  subklass som når `handleEvent`, visar det DOLT och hittar delarna med
  `find<T>(namn)` ("path", "files", "name", "ok" …). `FileDialog::run` är tre
  rader runt det; fönstret skriver svaret i inställningsobjektet (`friend`).
  **(4) FLÖDET** (lägenas regler i MANUAL `file-dialog`): OK på ett namn som
  är en MAPP öppnar mappen i alla tre lägena, och Save lägger ändelsen på det
  SISTA ledet ("v1.2/ny" → "ny.pdf"). OK VALIDERAR i sin `onClick` och bär
  inget svar, Avbryt bär `Cancel` (en öppen filtypslista tar sitt Escape
  först); `event()` tar FÖRE den fokuserade widgeten adressfältets Enter,
  Backspace UTANFÖR textfälten, Alt+Upp och F5. Fel står i meddelanderaden
  (libstdc++ på Windows svarar EINVAL, inte EACCES, för en mapp man inte får
  lista). Kryssrutan för dolda filer adopteras FÖRE adressfältet (dess
  `anchorRight` pekar bakåt), och en växling läser om mappen (`refresh()`,
  delad med F5 och språkbytet) och skriver `cfg_.setShowHidden` DIREKT.
- **`menu.cpp`** — MENYERNA: `Menu`s aktivering + hela VISNINGEN. Tre saker
  med hård gräns mellan sig: `Menu` är REN DATA appen äger,
  **`MenuPresentation`** är visningen (en SNAPSHOT + en kedja popup-fönster,
  som når data via INDEX-STIGAR — rot-CLAUDE.md), och `MenuWindow`/
  **`MenuRows`** är fönstret och raderna. `MenuRows` är en `ListBox`-SUBKLASS:
  radloopen, scrollen, klippet, hit-testen och MARKERINGEN är listans, och
  menyn lägger bara till radens INNEHÅLL (`drawRow`, med postens
  `ImageSource` i `SnapRow`) och HÖJD (`rowH` — separatorer är tunna);
  `rightDown` svarar false, för menyns armering är ett PROVISORISKT val som
  aldrig får gå listans högertrycksväg. **POSTENS TILLSTÅND**:
  `detail::setMenuItemIcon/Enabled/Checked` (`menu_path.h`) är den ENA
  implementationen bakom de fyra bärarnas platta mutatorer (okänd stig ⇒ logg
  med bärarens namn, no-op). **AKTIVERINGEN ÄR EN VÄG** (`Menu::activatePath`,
  ordningen i rot-CLAUDE.md); efter anropet rör ingen kod itemet eller kedjan.
  **KEDJEKONTRAKTET står som kommentar överst i filen** — protokoll, inte
  stil: all öppning via `show(*förälderfönstret)` → `showOwned` →
  `closePopupsNow` (som stänger syskon men aldrig föräldern), all stängning
  via `close()`/kaskaden. Undermenyns ankare är en DEGENERERAD 1×1-rect vid
  radens högerkant (positioneraren lägger "under ankaret", så det blir
  höger-om-raden utan fasadtillägg; vid skärmkant SLIDE:ar den över
  föräldern). **GRABB I BÅDA ÄNDAR**: på Wayland flyttar `xdg_popup_grab`
  tangentbordsfokus till popupen, på Windows/macOS/X11 är den `noActivate`
  och tangenterna landar hos ÄGAREN — `MenuRows` griper i sitt fönster och
  presentationen hänger `setMenuKeyHook` på ägaren; båda ropar samma
  `navigate()`, och ett event har ett `windowId`, så ingen dubbel dispatch.
  Presentationen ÄGS av modulen (en statisk lista) och självdestruktionen är
  POSTAD (aldrig inifrån sin `onClose`, mitt i App:s stängningsrunda) — därför
  bor **`menuPresentationHostedIn()`** här, och listan lever ETT VARV LÄNGRE än
  `Menu::isOpen()`: deny-checken släpper efter TVÅ pumpvarv (`unadopt_gate`).
  **`keyName` är KLUVEN i TVÅ:** `canonicalKeyNameImpl` (engelsk och STABIL —
  den enda som får SPARAS, `canonicalKeyName`/`parseCanonicalKey`) och den
  ÖVERSATTA visningen i `shortcutText` (Mac-symbolerna ⌃⌥⇧⌘ är universella,
  tangentnamnet efter dem översätts). `MenuPath` med sin PRIVATA validator och
  `menuTokenMatches` bor också här.
- **`theme.cpp`** — `Theme::resolve()` (System → `gfx::systemTheme()` per
  anrop) + de två HANDSKRIVNA paletterna (`glazeDefault()` "Lavender" — varje
  färg grindad, mätvärdet i kommentaren — och `slateDefault()` "Windows", med
  trafikljusen lika i båda lägena; Windows är REFERENSEN och grindas inte). **PALETTGENERATORN**
  (`Theme::slate(Hue)`/`Theme::minimalistic(Hue)`): `color::hsl` (färghjälparna bor i **`color.cpp`**, publika i glaze/color.h sedan REQ9_TERMINAL) + en
  LJUSHETSTRAPPA (`struct Ladder`, en per familj och läge) som mappar hue →
  hel `Theme`; Neutral är samma funktion med mättnad 0. Trappan skrivs och
  kommenteras, hexvärdena är dess utfall, och `contrast_gate` mäter dem. TVÅ
  saker avgörs av MÄTNING, för de beror på kulören: `onAccent` är den av
  nästan-vit/nästan-svart som mäter högst mot accentytan, och hover/press
  flyttar ALLTID BORT från den (annars landar hover i dödbandet runt
  luminans 0.19 där ingendera når 4.5:1). `themeCount/themeAt/defaultTheme`
  mappar UI → temalistan (index 0 = default). Nya roller: rot-CLAUDE.md
  (SIST i `Colors`, i båda handskrivna och i generatorn); de HÄRLEDDA räknas
  i **`deriveRoles()`** (kapseln, `fieldReadOnly`, `dropHint` =
  `accentSurface` med `kDropHintAlpha`) i ETT anrop från alla tre ställena —
  en glömd härledd roll blir svart. **`deriveStatus()`**
  (`warningSurface`/`onWarning`/`errorSurface`/`onError`) är den enda som
  INTE läser paletten: FASTA vinklar (amber 44°, röd 358°) vid en ljushet,
  lika i båda lägena — rött betyder fel i en violett app och i en grön —, och
  `on*` väljs med `onAccent`s mätning; därför står `hsl`/`luminance`/
  `contrast` i det FÖRSTA anonyma blocket. **`standardPalette()`** —
  färgväljarens förval: färger, men INTE en temaroll (innehåll, lika i varje tema).
  **Ritning/tema/mått/läge är APP-GLOBALT**
  (`App::themeState` — fyra fält): setters skriver det delade state:t och
  kör `App::invalidateAll()` (+ `App::reloadFontsAll()` när måtten rörs),
  `makeCtx` resolvar ur det per repaint (och bär `ui` i `Ctx::ui`,
  måtten i `Ctx::style`). Lägg aldrig tillbaka per-fönster-tema.

## Lokala konventioner & fallgropar

- **Händelsestyrd rendering** och **lånade barn i rivning** — hård regel 1
  och fallgropen i rot-CLAUDE.md. För SNÅL damage syns som spökpixlar
  (`setDebugDamage(true)`).
- **Arbete för NÄSTA BILD går i ramkön** (`detail::postFrameWork`), inte i
  `app().post`: appens kö har en budget, och en tick bakom en skur av
  trådarnas svar ger en bild utan rörelse. Ramkön är bara UI-trådens.
- **En ny profilerarkrok börjar med `if (!profiling()) return;`** — utom
  bildens slut och fönstrets stängning, som städar. Låsordningen är
  registret FÖRE blocket; trådslutet rör inga lås (döda block slås in vid
  nästa läsning), och UI-trådens ringar låses inte.
- **BORTTAGNINGEN ÄR ETT DRIFTSANROP, aldrig en rivningsväg.** `unadopt(w)`
  är symmetrin till `adopt` — LÅNAT barn släpps, ÄGT barn FÖRSTÖRS — och
  `releaseOwned(w)` ger ägandet tillbaka som `unique_ptr`. Fyra regler:
  **(1) VALIDERING FÖRE MUTATION** — `validateRelease` muterar inget, `detach`
  allt, så en nekad operation (också `releaseOwned` på ett LÅNAT barn) lämnar
  trädet exakt som det var.
  **(2) FÖNSTERSTÄDNING FÖRE TRÄDMUTATION.** `detach` kallar
  `AppWindow::detachTree(w)` FÖRST, för `setFocus(nullptr)` går via
  `ctxFor(*old)` — en förälderkedjevandring som kräver en intakt kedja.
  **(3) `~Widget` GÖR INGEN AUTO-`unadopt`.** Lånade barn dör före basens rot.
  **(4) DENY ÄR HELA POPUP-HANTERINGEN.** `hasOpenPopup()` REKURSIVT över
  målträdet plus `menuPresentationHostedIn`; nekandet står kvar ett pumpvarv
  efter `close()`, två för en meny (`menu.cpp`). Grind: `tests/unadopt_gate.cpp`.
- **Ny widget-typ** → mönstret i [../include/glaze/CLAUDE.md](../include/glaze/CLAUDE.md)
  (kopiera `button.*`), med `src/widgets/<namn>.cpp` i `CMakeLists.txt`.
  **Och SINA STRÄNGAR I FYRA SPRÅK** (TODO-post 32): varje text widgeten
  själv visar — etikett, tooltip, felmening, månadsnamn — är ett ramverks-id
  i `glaze/strings.h` med en rad per språk (sv · en · nb · fi) i
  `Glaze/language/framework.langtxt`, ompackat och inbäddat enligt filens
  huvud. Aldrig en literal: `lang_gate` del 2 fäller ett id som saknas i ett
  språk, och del 4 fäller en hårdkodad text (`// lang_gate:rad <skäl>`
  undantar det som inte är UI-text, t.ex. ett kanoniskt namn).
- **onClose-vetot fyras i pump-svansen** (handlers får kalla close()/show()
  där) — **utom för popuparna: en popup får aldrig skapas medan en annan
  lever** (xdg-shell tillåter bara att den ÖVERSTA förstörs, och öppningen sker
  SYNKRONT i dispatch). `showOwned` kallar därför **`App::closePopupsNow()`**
  innan popup-fönstret skapas — `closePopups`s urval, verkställt direkt och
  BARA för popups. **Popup-stängningen är INTE vetobar**: `firePopupClose`
  ignorerar `e.cancel`, för ett veto hade lämnat den gamla levande medan den
  nya skapades (kompositörens `popup_done` går inte heller att neka). Det är
  den ENDA platsen där användarkod körs synkront så; `showOwned` bär en
  reentransvakt (`showing_`), för `open_` sätts först i slutet.
