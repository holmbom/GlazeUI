# CLAUDE.md — Glaze/widgets/include/glaze

Publika headers för **`glaze`** — API:t apparna faktiskt inkluderar
(`#include <glaze/glaze.h>`). Den här katalogen bär INFRASTRUKTUREN;
komponent-headerna bor i [widgets/](widgets/CLAUDE.md) (en fil per widget).
Implementationen i [../../src/](../../src/). Del av GlazeUI — se
[root /CLAUDE.md](../../../../CLAUDE.md) för lagerbilden, tre-fils-modellen
och de hårda reglerna (utseendets fyra delar är regel 3).

## Filer

- **`glaze.h`** — PARAPLYET: inkluderar allt nedan OCH hela `widgets/`.
  Apparna (och genererad kod) inkluderar bara den här — nya headers läggs
  till här så splitten aldrig kostar användaren en rad.
- **`version.h`** — VERSIONEN, den enda källan: `GLAZE_VERSION_MAJOR/MINOR/PATCH`, `GLAZE_VERSION_STRING`,
  `GLAZE_VERSION`/`GLAZE_VERSION_OF`; rot-CMakeLists läser den till `project(VERSION …)` — grind `version_gate`.
- **`app.h`** — `class App` + **`glaze::app()`**, LOOP-ÄGAREN som NÅBAR
  Meyers-singleton: EN per process, skapas LAT vid första fönstret (ingen `App
  app;` i main); `gfx::shutdown` i dtorn vid processlut — i DELAT läge i programmets `exit()` av ankaret src/app_anchor.cpp (`detail::shutdownApp`, idempotent `App::shutDown`). Äger fönsterregistret
  (icke-ägande — fönstren är användarens stack-/ medlemsobjekt; utåt:
  `windows()`/`windowCount()`/`find(titel)` — aktivitetshanterar-ytan), **det
  app-globala utseendet** (`themeState()` = stil + tema + läge;
  `invalidateAll()` ritar om varje öppet fönster vid byte), **användarskalan**
  (`userScale()`/`setUserScale(float)` — app-global multiplikator ovanpå
  OS-skalan, `uiScale() = osScale × userScale`, klampas 0.5–2.0 i gfx; ALLT
  växer, logisk storlek är invarianten; sättaren stänger öppna popups och
  fonten laddas om av EVENT-vägen; appen äger lagringen),
  **skärmkalibreringen** (`setDisplayCalibration(id, pxPerMm)` — appens
  mätning av en skärm, per skärm-id, app-global; 0 tar bort; varje öppet
  fönster mäts om och ritas om, appen äger lagringen), **ritytans
  minnesbudget** (`setCanvasMemoryBudget(byte)`, 256 MB — FÖRVALET PER RITYTA
  (`Canvas::setMemoryBudget` slår det) och taket för fönstrets bilder utanför
  ritytorna; LRU-vräkning i `ImageCache::trim` i början av nästa frame, aldrig
  mitt i en; det som ritades i förra framen vräks aldrig), **ritytans
  uppladdningsbudget** (`setCanvasUploadBudget(byte)`/`canvasUploadBudget()`,
  förval 16 MiB PER FÖNSTER OCH FRAME — det som inte ryms väntar på nästa
  frame, som fönstret begär själv, och framens FÖRSTA uppladdning går alltid
  igenom; bilderna, meshar, instanser och punktmoln delar den; 0 och negativt
  (> `PTRDIFF_MAX`) loggas och ändrar inget; `renderContent` lämnar båda
  till `trim`), **`reloadFontsAll()`/`applyContentSizeAll()`** (ett byte av
  UI/Style ändrar både fontstorleken och chromets höjd — fonten först, sedan
  omräkningen, och båda är no-op per fönster när inget ändrats),
  **`post(fn)`** (TRÅDSÄKER post till UI-tråden, körd i pump-SVANSEN FÖRE
  `flushPendingCloses()` så en postad `close()` som HANN köras tas i samma
  varv. Den ENDA glaze-funktion som får kallas från en annan tråd;
  väckningen går via `gfx::postWakeup()`), **`setPostBudget(ms)`/
  `postBudget()`** (PRESTANDA D2 — appens kö körs FIFO tills budgeten är
  slut, förval 4 ms, 0 = obegränsat, negativt loggas; varvets första alltid,
  resten FÖRST i kön till nästa varv, ingen delning av en lång funktion) och
  den interna **`detail::postFrameWork(fn)`** (ramverkets RAMKÖ för nästa
  bild — ritytans tick, kamerans händelse, `onResize` — körd FÖRE appens,
  hel och utan budget; bara från UI-tråden, aldrig appens väg; friend som
  `detail::shutdownApp`), modalstacken och pumpen (`pumpUntilClosed` — delas av
  alla `AppWindow::run`-anrop, nästlade modaler pumpar samma implementation;
  sover i `waitEvent` när inget fönster vill ha frame; uppskjutna stängningar
  verkställs i pump-SVANSEN efter **onClose-vetorundan** (`confirmClose`:
  barnen djupast först, första `cancel` avbryter hela kaskaden; APPAVSLUTET
  köar bara ägarkedjornas rötter), aldrig mitt i dispatch).
  **`setAppId(id)`** — appens identitet FÖRE första fönstret (Linux xdg
  app_id + `WM_CLASS`; tom = programfilens namn), `gfx::setAppId` nåbar utan
  gfx-headern. **`setShowInDock(show)`/`showInDock()`** — macOS dockikon,
  likaså FÖRE första fönstret; Windows och Linux lagrar och loggar false.
  Fönstrets knapp i aktivitetsfältet är en ANNAN fråga (`setShowInTaskbar`).
  **`activeWindow()`** — appens AKTIVA fönster (WinForms `Form.ActiveForm`):
  det första öppna med `isActive()`, nullptr utan tangentbordsfokus; en popup
  blir aldrig aktiv, så det är ägaren. Privata `eventSeq_` (`noteEvent`,
  stegad för varje riktig inmatning, inte för en Wakeup) är `onResize`s kedjespärr. **`setWheelInvert`** och
  **`setCanvasBinding`** m.fl. (REQ9_INMATNING): rullningens och ritytornas förval — app-global drift, ingen `.glazeui`-form.
- **`widget.h`** — kontraktet alla widgets delar:
  - `enum class Dock { None, Left, Top, Right, Bottom, Fill }` +
    `struct Anchors { left, top, right, bottom }` och **`struct SiblingAnchor
    { std::string to; float gap; }`** + `anchorLeft`/`anchorTop`/
    `anchorRight`/`anchorBottom` — PLACERINGEN. Reglerna (remsorna i
    barnordning som skär också för de icke-dockade syskonen, dubbelankare
    sträcker, syskonankarets KANT-semantik, `to` = målets `name`, bara
    TIDIGARE syskon, ignorerat i en `Bar`, prioriteten) står i rot-CLAUDE.md
    under tre-fils-modellen. Här det som bara headern bär: `Widget::
    carveSiblingDocks` är en PUBLIK statisk basmedlem (GlazeBuilder räknar
    koordinater BAKLÄNGES med samma funktion), `inDockFlow()` håller `TopBar`
    utanför flödet, `earlierSibling` stannar vid `this`, `resolve()` loggar
    aldrig (6+ anrop per frame — valideringen sker där texten kommer IN), ett
    DOLT mål räknas ändå (följaren lämnar ett hål), och layouten är
    tillståndslös. Grind: `tests/layout_gate.cpp`.
  - `struct Ctx { gfx::Renderer* r; gfx::Font* font; gfx::Font* headingFont;
    float scale; gfx::Rectf area; const Theme::Variant* theme; UI ui;
    const Style* style; ImageCache* images; float pxPerMm;
    CanvasResources* canvas; FontSet* textFonts; gfx::Rectf box; }` — skickas till varje
    `draw()`. **`pxPerMm`** (CANVAS_2D etapp 2) är fönstrets FYSISKA px per
    mm på skärmen det står på (`AppWindow::display()`, med kalibreringen) —
    ritytans mm-enhet; mm i logiska px är `mm * pxPerMm / scale`, och default
    det antagna 96 dpi. **`canvas`** (etapp 7) är fönstrets ritytsresurser
    (intern `src/canvas/resources.h`) — FÅR vara null; ritytan ritar då bara
    sin yta. Rita via `Draw::` mot `c.bounds()` (widget-lokala LOGISKA px,
    origo i eget hörn — `box` är widgetens fysiska rect och sätts av
    ritvandringen); ALLA mått ur `c.style`, ALL färg ur `c.theme->colors`.
    `ui` = den AKTIVA RITNINGEN och `style` de AKTIVA MÅTTEN (app-globala,
    resolvade per repaint) — widgeten switchar på `ui` och läser sina tal ur
    `style`. `images` = per-FÖNSTER texturcachen (assets — FÅR vara null,
    ritkoden kollar). **`headingFont`** = ROLLFONTEN: en FÖNSTERresurs (därför
    varken i `Style` eller i widgeten), förskapad EAGER i `reloadFont()` ur den
    SLUTNA mängden storlekar; FÅR vara null (då `font`). **`textFonts`** (REQ7_MARKDOWN): samma slutna
    mängd men LAT (`src/font_set.h`); FÅR vara null (då `font`/`headingFont`). **Ny medlem FÖRE `box`** —
    Ctx brace-initieras i `makeCtx` med `box` sist. Bygg aldrig en Ctx för hand —
    hämta via `AppWindow::makeCtx()` (protected); en annan yta: kopiera, byt `area`.
  - **`Handler<Args, Sender = Widget>`** — handler-FÄLTETS typ: fullformen
    `(Sender* sender, Args& e)` OCH den PARAMETERLÖSA formen, normaliserad
    vid TILLDELNING och lagrad i en `shared_ptr`; `operator()` håller en
    egen referens under anropet, så en handler som river sin widget kör
    färdigt, och en KOPIA delar det anropsbara (en muterbar lambda: ETT
    tillstånd). Nollställning `= nullptr` (`= {}` och en generisk lambda är
    tvetydiga — avsiktliga kompileringsfel). **`Sender`** (SYSTEMFÄLTET E6)
    är regel 6:s "den som avfyrade" som typ: en klass med egna events som
    inte är en widget är sin EGEN sender (`Handler<ClickArgs, TrayIcon>`),
    och varje `Handler<X>` är `Handler<X, Widget>`. Ett fält med POLYMORFA
    args tar också fullformen med en ÄTTLING (TODO 86 — typen ur anropsoperatorn
    eller den förfallna funktionspekaren, också `noexcept`; `dynamic_cast` vid anropet, fel typ körs inte och loggas en gång).
  - `struct EventArgs {}` + `ClickArgs`/`HoverArgs { entered }`/`ChangeArgs`/
    **`MouseArgs { button, local, mods, handled }`** (polymorf — ritytans
    `CanvasMouseArgs` ärver; TODO 86)/`CloseArgs { cancel, reason }` (ett
    `CloseReason`, TODO 72; event-UTDATA; **`enum class DialogResult`** står
    här, för en `Button` bär sitt svar)/`MouseDownOutsideArgs { x, y }`/
    **`ContextMenuArgs { x, y, source }`** (HÖGERklicket, MENYER — fysiska
    px i FÖNSTERkoordinater, alltså exakt vad `Menu::popupAt` vill ha;
    `source` är widgeten UNDER pekaren och FÅR vara nullptr på tom
    fönsteryta. Routingen är ROUTED LOOKUP och inte bubbling: första
    FÖRFADER med satt `onContextMenu` anropas och ÄR `sender` — regel 6 —
    medan den träffade widgeten ligger i args)/**`ActivateArgs`**
    (AKTIVERINGEN: dubbelklick eller Enter på den aktuella posten, WinForms
    `ItemActivate`; skild från `onChange` med flit — att VÄLJA och att ÖPPNA
    är olika handlingar — och här och inte i listvyns header, för varje
    lista med poster aktiverar likadant)/**`KeyArgs { key, mods, handled,
    repeat, cancelled, text }`** (`onKeyDown`/`onKeyUp` på varje widget: `key` är
    den VIKTA ur `keyDown` — i `onKeyUp` tryckets —, `handled` UTDATA, `repeat`
    OS:ets upprepning, `cancelled` fokusförlustens släpp, `text` det trycket
    SKRIVER i UTF-8 (`gfx::Event::text`; tom för styrtecken, en död tangent och
    Ctrl/Alt/Cmd — inte AltGr/Option; `handled` sväljer inte tecknet))/
    **`ResizeArgs { previous, first }`** (`onResize`, nedan)/**`FocusArgs`**
    (fokushändelsernas, TOM med flit: vem är `sender`).
  - **TOOLTIPEN** — en egenskap på BASEN, som wx `SetToolTip` på `wxWindow`:
    **`setToolTip(TrText)`** / `toolTip()` (upplöst text) / `toolTipToken()`
    (identiteten). Ett TOKEN som `text`, så `glaze::tr(id)` fungerar och ett
    språkbyte slår igenom vid nästa visning. Tom = ingen tooltip. Sättaren
    invaliderar INGENTING (tooltipen ritas inte i widgeten) utan meddelar
    fönstret, som gömmer en VISAD tooltip vars text byts. Uppslaget är
    ROUTAT: första förfader med en tooltip visas — en komposit (talfältet)
    bär den åt sina inre delar, och en panels tooltip förklarar barn som
    saknar en egen. Inaktiverade widgets visar sin. Uttrycks i `.glazeui`
    som SETTER-fältet `x.setToolTip("…");` på varje typ. Mekaniken
    (popup-fönstret, fördröjningen, tystnaden) bor i `src/tooltip.cpp`.
    **`toolTipAt(c, x, y)`** (DOCKNING E1) är tooltipen PER DEL — källan är
    widget OCH text, så två delar är två källor (kontraktet i widget.h).
  - **PEKAREN** (FONSTER_OCH_TIMER) — **`setCursor(gfx::PointerShape)`**/
    **`resetCursor()`**/`cursor()` (`std::optional` — ingen = typens
    `cursorAt`). Slår widgetens egen form, för widgeten SJÄLV (aldrig ärvd av
    barnen), och visas inte för en inaktiv widget. Sättarna, `setEnabled` och
    `setVisible` prövar om pekaren direkt (`refreshPointer` →
    `AppWindow::refreshCursor`), och `detach` gör det efter mutationen.
    `setProp("cursor", …)` tar namnet med typens prefix eller utan
    (skiftlägesokänsligt på namnet, ett annat prefix är ogiltigt); i
    `.glazeui` setter-fältet `x.setCursor(gfx::PointerShape::Hand);`.
  - **STORLEKEN OCH PUNKTEN I FÖNSTRET** (REQ3_LAYOUT_FONSTER) — **`onResize`**
    (`Handler<ResizeArgs>` på varje widget, WinForms `Resize`): `actualSize()`
    är en annan än vid förra LEVERANSEN. Upptäckten i ritningen (layouten är
    tillståndslös), leveransen i pump-SVANSEN — framen ritas med det gamla
    innehållet, mellanlägen slås ihop (A → B → A ger ingen), den första (`first`)
    kommer före fönstrets första synliga bildruta, en dold widget och en inaktiv
    dockningsflik får ingen, och under Windows/macOS fönsterdrag kommer den först
    vid släppet. KEDJESPÄRREN (regel 1): 64 levererande bildrutor i följd utan
    riktig inmatning ⇒ vänta till nästa händelse. `root().onResize` är ytan under
    namnlisten. **`actualSize()`** (logiska px, ur `resolve`) och **`toWindow(lokal)`**/
    **`fromWindow(fönster)`** (logiska lokala px ↔ FYSISKA fönster-px —
    `ContextMenuArgs`/`Menu::popupAt`s rymd) svarar tomt utan ett öppet fönster.
  - **DRAG-AND-DROP** — ett drag MELLAN widgets, INOM FÖNSTRET (WinForms):
    **`setAllowDrop`/`allowDrop`** (släppmål) och **`setAllowDrag`/`allowDrag`**
    (källa — tröskeln är ramverkets, `Style::dragThreshold`), plus SEX
    `Handler`-fält: källans `onDragStart` (`DragStartArgs` — UTDATA: fyll
    `e.data`, och dragbilden `e.text`/`e.img` — ett `ImageSource`; TOM last
    eller `allowed == None` = inget drag, trycket förblir ett tryck) och
    `onDragEnd` (`DragEndArgs { effect, target }` — utfallet), målets
    `onDragEnter`/`onDragOver`/`onDragLeave`/`onDrop` (`DragArgs` — `data`
    är en REFERENS till sessionens last, `effect` är UTDATA och BÄRS mellan
    eventen, **`handled`** är `onDrop`s UTDATA (REQ2_TRAD_LISTOR B8), `x/y`
    fysiska fönster-px och `localX/localY` logiska px i mottagarens hörn,
    `source` FÅR vara nullptr). **Lasten `DragData` är MIME-format → bytes**
    i preferensordning (`set` ersätter PÅ PLATS; `kText` = text/plain, UTF-8)
    och ALDRIG `void*` — räckvidderna "mellan våra fönster" och "till/från
    andra appar" ska gå att bygga utan API-brott. **`DragEffect`** är en
    bitmask (`allows(mask, e)` — `None` tillåts aldrig); ramverket VALIDERAR
    målets val: utanför källans mask ⇒ `None`, flera bitar ⇒ den första av
    Copy · Move · Link som källan tillåter. Routingen: MÅLET är första
    förfader-eller-jag under pekaren med `allowDrop` som är aktiverad
    (`onContextMenu`s routing; tom fönsteryta börjar i roten, utanför
    klientytan finns inget mål), KÄLLAN den tryckta widgeten och ingen
    förfader. Ordningen: enter och DIREKT over vid inträdet (B9 — varje plats
    möter `onDragOver`) → over per rörelse → leave vid målbyte; ett accepterat
    släpp ger `onDrop` (Drop ERSÄTTER Leave, OLE-ordningen), ett avvisat
    `onDragLeave`, och källan får ALLTID `onDragEnd` sist. Escape och
    fokusförlust avbryter, ett drag ger inget klick, och draget är MODALT för
    tangentbordet. **Ett skrivet undantag:** lämnar källan eller målet TRÄDET
    mitt i draget körs INGEN apphandler (`detachTree` ger målet bara dess
    `dragLeave`-KROK; en källa som lämnar avslutar draget tyst). **Inbyggt
    beteende**: sju protected krokar — `canDragFrom(c, x, y)` (får ett drag
    BÖRJA vid TRYCKET? frågas före allt som fyller lasten; B6) och
    `dragStart`/`dragEnd`/`dragEnter`/`dragOver`/`dragLeave`/`drop` — som
    FÖRESLÅR före appens handler med samma args, UTOM i SLÄPPET: `onDrop` körs
    FÖRST (B7), och `drop` bara om målet sitter kvar och appen varken nekade
    (`None`) eller satte `handled`, med effekten validerad. **`isDragSource()`**
    = källan till ett drag som startar (redan i `onDragStart`) eller pågår;
    `ownsDropIndicator()`, **`dropEffect()`** (valet nu). **AUTOSKROLLEN**
    (REQ3_TRAD_LISTOR_FALT) är kroken **`dragScroll(c, x, y, apply)`** — kan jag
    rulla mot pekaren (`apply` = ett `dragScrollStep`)? Kedjan frågas från
    widgeten under pekaren UPPÅT som hjulet, första true vinner; fönstrets timer
    (`kDragScrollTimerId`, `dragScrollMs`) stegar och gör om målvalet efter varje
    steg, och `Panel` svarar inom `dragScrollZone` — varje rullande yta har den.
    Grind: `tests/dnd_gate.cpp`.
  - **AXELREGELN** (`width`/`height`, ALLA widgets): en satt axel (`> 0`)
    vinner, en axel `<= 0` är **AUTO** och fylls av stilen + innehållet — PER
    AXEL, och med ett verkligt mått, aldrig noll. Undantagen är skrivna beslut i
    sina cpp: `Panel`/`PropertyGrid` (0×0), `Bar` (bara defaultHÖJD), `Spacer`
    (`width = 0` är grow-vikt).
  - **`tabIndex`** (`int`, `-1` = AUTO, WinForms): auto-numret LAGRAS aldrig —
    det ÄR positionen i DFS-fokuslistan (`AppWindow::moveFocus`); ett satt värde
    jämförs mot de andras listpositioner, så `0` är "först bland dem som bär 0".
  - **LAYOUT-FÄLTEN är RÅA** (`name`, `x/y`, `width/height`, `right/bottom`,
    `dock`, `anchors`, `anchor*`, `tabIndex` — den genererade kodens yta), allt
    annat privat med `setX()`/`x()`/`isX()`, för settern INVALIDERAR. Ett
    layoutfält ändrat utanför en handler kräver `invalidate()` själv.
  - **`name`** är objektets namn, speglat (rot-CLAUDE.md): `find<T>(namn)` och
    `SiblingAnchor::to` slår upp den. **`inDockFlow()`** är INTE virtuell —
    `AppWindow` (friend) sätter den false på exakt fönstrets `topBar_`, som
    resolvas mot chrome-ctx:t (en nästlad TopBar skär som vanligt; `layout_gate` 18).
  - `class Widget` (bas): `name`, `x/y`, `width/height` (0 = auto, se
    axelregeln ovan), `tabIndex`, `dock`, `anchors`, `right/bottom`,
    `enable()/disable()/isEnabled()` + `show()/hide()/isVisible()`
    (settrarna INVALIDATERAR vid ändring och prövar om standardknappens
    utseende, `refreshDefaultButton` — liksom `adopt`),
    `onClick`/`onHover`/**`onContextMenu`**, **`onFocus`/`onFocusLost`**
    (FOKUS INOM, WinForms `Enter`/`Leave`: vid A → B `onFocusLost` på A:s
    kedja som inte står i B:s, INNERST först, sedan `onFocus` på B:s nya,
    YTTERST först; roten fyrar aldrig. POSTADE ur fönstrets EN kö, i bytenas
    ordning också över en nästlad pump, och bara om widgetens `HandlerGuard`
    står sig. Efter tryckets caret — så `x.onFocus = [this] { x.selectAll(); };`
    fungerar vid klick och Tab. OS-fokusförlusten fyrar inte, `hide()`/
    `disable()` flyttar inte fokus, och en fokusförlusts commit postas FÖRE
    fokushändelserna), `resolve(Ctx) → gfx::Rectf`, virtuella
    `draw(Ctx)`/`contentSize(Ctx)`/`clicked()`.
    **Trädet:** `parent()` (INNEHÅLL — slutar vid fönsterroten; fönster äger
    fönster via `AppWindow::owner()`, som ALDRIG bär koordinater), `children()`
    (`vector<Widget*>` i z-ordning: sist överst; blandar ägda och lånade barn),
    `add<T>(...)`/`adopt(unique_ptr)` (ÄGD) och **`adopt(Widget&)` (LÅNAD —
    värde-medlemmar i fönsterklassen; genererad kod: `root().adopt(okBtn)`;
    dubbeladoption = no-op + logg; trädet raderar aldrig lånade barn)**.
    **BORTTAGNINGEN:** **`unadopt(w)`** är symmetrin (LÅNAT släpps, ÄGT
    FÖRSTÖRS), **`releaseOwned(w)`** ger ägandet tillbaka som `unique_ptr`
    (flik-fallet). Båda kräver ett DIREKT barn och validerar i FYRA steg FÖRE
    varje mutation: direkt barn · rätt ägandekategori (`releaseOwned` kräver
    `ownedChildren_`) · **`canReleaseChild()`** på föräldern (en komposit med
    RÅA barnpekare nekar sin inredning — `PropertyGrid`/`TopBar`; fönstrets
    inbyggda nekas via ägaren) · **`hasOpenPopup()`** REKURSIVT över
    målträdet plus frågan om delträdet är värd för en öppen MENY (också
    tangentvägens: Return/Escape före dialogknapparna, widget.h). **En nekad
    operation lämnar trädet EXAKT som det var**, och nekandet ÄR hela
    popup-hanteringen: stängning är uppskjuten, så nekandet står kvar direkt
    efter `close()` (ett pumpvarv för en dropdown, TVÅ för en meny). ALDRIG
    en rivningsväg — `~Widget` gör ingen auto-`unadopt`. Grind:
    `tests/unadopt_gate.cpp`. **`childAdded`/`childRemoved(Widget&)`**
    (protected) är containerns besked EFTER `adopt` respektive borttagningen
    (barnet LEVER ännu i `childRemoved`) — aldrig en användar-handler.
    **`childVisibilityChanged(Widget&)`** är tredje syskonet (barnets `show()`/`hide()`,
    efter bytet), **`childLayoutChanged`** det fjärde (se Damage), **`childFocusChanged`** det femte (REQ8 9:
    fokus kom in i eller lämnade barnets delträd — synkront i `setFocus`, bara invalidera); **`showsChildren()`**
    (publik) stänger Tab-vägen in i barnen OVILLKORLIGT, till skillnad från `focusesChildren`: en inaktiv flik är inte dold. **`frontChild()`** (publik) — barnet
    som RITAS SIST och TRÄFFAS FÖRST utan att barnordningen rörs (dockningens overlay); ett
    svar som inte är ett barn ignoreras (`subtreeHasOpenPopup` är protected).
    **`transferChild(w, to)`** FLYTTAR ett direkt barn, också till ett
    annat fönster: ÄGANDET följer med, och `unadopt`s fyra kontroller plus
    målets (inte föräldern, barnet eller dess delträd) prövas FÖRE varje
    mutation; **`severChild(w, alive)`** (protected) är RIVNINGSVÄGEN utan
    delträd, och `appWindow()` är null också när fönstret DÖTT (`ownerLife_`
    — en lånad widget kan överleva det). **`shownChanged()`** (E2): syns/döljs.
    **`focusesChildren()`** (post 2a) — *"jag ERSÄTTER mina barn som
    tabbstopp"*: Tab-vandringen rekurserar inte förbi en komposit som svarar
    true, men BARA när den själv blev kandidat (annars vore en
    `!focusable()`-komposits hela gren otabbbar). Klickfokus ändras inte.
    **`focusProxy()`** (post 12) — FOKUSOMBUDET, parhästen: `AppWindow::
    setFocus` följer det när fokus landar på kompositen (Tab, klick på dess
    egen yta, `focus()`), så talfältets fokus hamnar i dess `TextInput`.
    `focusesChildren()` stänger Tab-vandringen INÅT, ombudet öppnar fokus
    inåt igen. KONTRAKTET: ombudet måste vara en ÄTTLING — annars följs det
    inte (Qts `setFocusProxy`). **`childrenExtent(c)`** (post 2c, protected)
    — autostorlek UR BARNEN, opt-in: en ORDNINGSKÄNSLIG invers av
    dock-carvingen där `Fill` behandlas VID SIN POSITION i ett baklängespass
    och varje remsa bär sitt TVÄRKRAV (`Left, Fill` ⇒ 80×40, `Fill, Left` ⇒ 50×40).
    De ODOCKADE (REQ7_LAYOUT B1): kanter från början OCH slutet, två flöden i
    höjdled, sidledspar, SPÄNDA barn med mått som minsta, `right`/`bottom` som
    LUFT och det dolda ledets hål — regeln och det obestämda vid deklarationen.
    **`heightForWidth(c, bredd)`** (TEXTFALTEN, protected virtual) — höjden när
    layouten GER bredden, logiska px in och ut, `nullopt` = beror inte på bredden.
    Frågas BARA med en given bredd: sträckt vågrätt i gren 2, och Top/Bottom-
    dockad med remsans bredd (privata `dockedSize`, i dock-uppspelningen OCH
    `carveSiblingDocks`) — aldrig i en Bar, för Fill/Left/Right eller i
    `childrenExtent(c)` (skriven gräns). Läser aldrig `c.box`; en satt `height`
    vinner. Bara `Label` (`wrap`) svarar; **`measureHeightForWidth`** är parhästen.
    **`childrenExtent(c, bredd)`** (publik, REQ3) — samma mått med BREDDEN GIVEN
    (fönstret som följer innehållet): Top/Bottom/Fill-remsorna och odockade barn
    med två KÄNDA x-kanter tar `heightForWidth` med sin bredd; kärnan är privata `extentOf`.
    Container-krokar (PUBLIKA — GlazeBuilders parallella vandring måste nå
    dem, pariteten grindas av `preview_gate`; subklassernas override får
    förbli `protected`, åtkomsten prövas mot typen `Widget`):
    `childArea(Ctx)`, `clipsChildren()`, `clipRect(Ctx)`,
    **`clipRadius(Ctx)`**, **`drawAfterChildren(Ctx)`** (OVANPÅ barnen,
    utanför eget barnklipp), **`frameStep()`** (REQ9 6: varje bild före
    skadan, `stepTree`, också med TOM rekt, där draw/efter-kroken/lagret inte anropas) — EN
    sanning för ritning, hit-test och damage — samt flex-paret `placesChildren()`/
    `placeChild()` (+ `placesChild(barn)`, REQ8 14: per barn — ritytans förankrade) (en container som PLACERAR sina barn själv — Bar;
    `resolve()` frågar föräldern först, barnets dock/anchors ignoreras då)
    och `flexGrow()` (Spacers grow-vikt; 0 = växer inte).
    **Egenskaper:** `setProp(key, value) → bool` (nyckel/värde-text —
    builderns modell-/preview-väg; basen tar size/pos/dock/anchor/right/
    bottom/visible/enabled; subklass overridar + anropar basen sist;
    false = okänd ⇒ anroparen loggar + ignorerar — regel 4-arvet).
    **`setTextToken(key, TrText) → bool`** (SPRÅKPAKET) är SKILD från
    `setProp` MED FLIT: en sättare som tar `std::string` kan inte ta emot
    `glaze::tr(...)` i C++ heller, så default false ⇒ raden blir OPAK —
    previewn gör aldrig MER än C++-vägen (regel 8). En EGEN widget deltar
    genom att overrida den och ge sin sättare en `TrText`-parameter. **Basen svarar för `toolTip`** (post 14), så en
    override MÅSTE avsluta med `return Widget::setTextToken(key, value);`
    — annars blir `x.setToolTip(glaze::tr(…))` opak just på widgets med
    egen text. `tooltip_gate` prövar varje REGISTRERAD typ.
    **Tangenterna** (REQ2_INMATNING): **`onKeyDown`/`onKeyUp`** på varje widget —
    trycket till FOKUSENHETEN (den fokuserade + närmaste Tab-stoppskompositer,
    roten utan fokus) innerst först FÖRE `keyDown`, inga handlers med en öppen
    popup i enheten; upprepningen och släppet till TRYCKETS kedja (`cancelled` vid
    fokusförlust/`hide()`). Krokarna **`keyUp`** (den som tog den) och **`keyTaken`**.
    **Fokus:** `focusable()` (default false), `focused()`, **`focus()`**
    (programmatiskt — värd → hostad editor), `keyDown(Key, mods) → bool`,
    **`descendantKeyDown(Key, mods) → bool`** (post 19, protected — en
    tangent den FOKUSERADE widgeten tackade nej till frågas dess synliga och
    aktiverade FÖRFÄDER om, närmast först, FÖRE acceleratorerna och Tab —
    flikkontrollens Ctrl+Tab inifrån en sida),
    `focusGained()/focusLost()` (kör ALDRIG användarkod — fältets `onCommit`
    därifrån går genom **`postEvent(fn)`**: pump-svansen, om **`HandlerGuard`**en
    från posten står sig — lever, sitter kvar (anknytningen stegas vid varje
    adopt/lossning), fönstret lever; ett händelsepar går genom
    **`firePair`**/`fireEvent`, vakten emellan; **`pressGaveFocus()`** i
    `mouseDown`; **`commitEdit()`** — committa NU, `AppWindow::commitEdits()`
    går kedjan), **`enabledChanged()`** (post 12, protected —
    `setEnabled` kör den EFTER en riktig ändring: `enabled` kaskadar INTE i
    ramverket, men en komposit vars INREDNING måste följa — talfältets fält —
    för det vidare själv; samma regel som fokuskrokarna, aldrig en
    användar-handler), **`languageChanged()`** (protected —
    SPRÅKET KAN HA BYTTS sedan widgeten senast fick veta: körs på varje
    widget i ett ÖPPET fönsters träd vid ett språkbyte, på hela trädet när
    ett fönster VISAS och på ett delträd som ADOPTERAS in i ett öppet
    fönster, så den ska vara IDEMPOTENT. En `TrText` behöver den inte; den
    är till för det en widget HÄRLEDER ur språket och LAGRAR (talfältets
    decimaltecken). Fristående widgets utan fönster får den aldrig, och
    GlazeBuilder bygger därför om sin ägarlösa preview i
    `onLanguageChanged`). **Mus:** `mouseDown`/`mouseDrag(Ctx, x, y)`
    (auto-capture under tryck), **`mouseMove`** (rörelse UTAN knapp —
    hover-rad; invalidatera BARA vid ändring. OBS: `AppWindow` frågar den
    på widgeten under pekaren ÄVEN under ett tryck, också på den tryckta
    efter dess `mouseDrag` — utom på målet för ett tryck APPEN tog), `mouseWheel(Ctx, x, y,
    wx, wy) → bool` (bubblar; det som RULLAR tar hjulet utan fokus, en
    VÄRDEWIDGET — Slider, SpinBox — bara MED fokus; rader → rörelse via
    `wheelScrollPx`/`wheelNotches`, så att styrplattan följer fingrarna),
    **`gesture(Ctx, x, y, fas, skala, grader)`** (nypning/vridning),
    **`mouseDoubleClick(Ctx, x, y)`** (det ANDRA trycket, räknat av `AppWindow`
    — regeln i rot-CLAUDE.md —, EFTER dess `mouseDown`; trycket efter börjar en
    ny följd). Modifierarna läses ur `appWindow()->modifierKeys()`.
    **APPENS MUS** (TODO 86, kontraktet vid fälten): fönstret fyrar
    `onMouseDown`/`onMouseUp`/`onMouseCancel` för målet FÖRE krokarna; ett
    taget tryck når widgeten bara som **`appPress(c, x, y, knapp, steg)`**
    (Begin/Move/DoubleClick), och **`fireMouse`** bygger args (ritytan sina).
    **`mouseUp(Ctx, x, y) → bool`** — SLÄPPET, EN gång per släppt tryck: till
    den tryckta widgeten och till en explicit fångande om den är en annan,
    var pekaren än står och även inaktiverad; FÖRE `clicked()` och med
    `pressed()` redan släckt. Ett AVBRUTET tryck (fokus- eller capture-förlusten,
    ett annat fönster tog det, ett nytt tryck) får **`mouseCancel(knapp) → bool`**
    i stället — bokföringen redan nollad, ett drag ska inte committas — och inget
    besked under en trädmutation eller när drag-and-drop tog trycket. `true` = en
    användar-handler kördes ⇒ hela fönstret ritas om.
    **`cursorAt(Ctx, x, y) const → gfx::PointerShape`** — pekarformen över
    punkten (default pil; `TextInput` I-balk, `Link` hand, skikten ↔/↕).
    AppWindow frågar widgeten under pekaren — och UNDER ett tryck den TRYCKTA
    (eller fångande, eller den som håller mittentrycket), så ↔ står kvar mitt
    i ett drag fast pekaren hunnit över ett textfält.
    **MITTENKNAPPEN** (CANVAS_2D etapp 8, protected): **`middleDown → bool`**
    BUBBLAR som hjulet (widgeten under pekaren, sedan förfäderna; inaktiverade
    hoppas över, roten frågas inte, explicit capture frågar bara den
    fångande), och den som svarar true får **`middleDrag`** (FÖRST i varje
    musrörelse, var pekaren än står, bara aktiverad) och **`middleUp → bool`**
    (en gång, även inaktiverad — `mouseUp`s svar). Ingen fokusflytt, ingen
    `pressed()` och ingen handler i tryck och drag — den som ändå kör en
    (ritytan) ber om full omritning. Fokusförlust och ett nytt tryck avbryter
    UTAN `middleUp` (med `mouseCancel(Middle)`; `unadopt` tyst), så ett pågående
    mittendrag läses ur den publika **`middlePressed()`**. **HÖGERKNAPPEN**
    (REQ2_INMATNING): `rightDown`/`rightDrag`/`rightUp`/`rightPressed()` likadant,
    ett taget tryck ger ingen snabbmeny; `middle`-/`rightDoubleClick` till den som tog det.
    **OVERLAY-LAGRET** (etapp 8, protected): **`hasOverlay()`/`drawOverlay(Ctx)`**
    — ritytans gummiband och HUD, ritat vid VARJE present efter appens
    `overlay()`, i trädordning, med `draw()`s Ctx och FÖRFÄDERNAS klipp
    (rektanglar och radier) inom ytan trädet ritas i; egen rect klipper inte.
    Ren ritning — ny present är `appWindow()->invalidateOverlay()`.
    Grind för båda: `tests/middle_gate.cpp`.
    **`clipRadius(Ctx)`** = barnklippets HÖRNRADIE i FYSISKA px (0 =
    fyrkantigt); en rundad container MÅSTE override:a den KONCENTRISKT mot sin
    yta (`Panel::innerRadius` = `panelRadius − panelInset`). HIT-TESTEN följer
    REKTANGELN, inte radien (skriven gräns). **PUBLIKA:** `appWindow()`/`behaviorCtx()`
    (ctxFor-vandringen utanför event-flödet — aldrig rå makeCtx;
    popup-ägande widgets och `Menu::popupAt`, som måste MÄTA sina rader före
    `show()`). PRECONDITION för `behaviorCtx()`: widgeten måste sitta i ett
    ÖPPET fönster — renderer och font skapas först i `show()`.
    **Capture/grab:** `setPointerCapture()`/`releasePointerCapture()`,
    `grabInput()`/`releaseInput()` (stack; toppen får KeyDown/TextInput före
    fokuserad widget + `mouseDownOutside(args)`). Båda släpps vid
    `WindowFocusLost`, liksom TRYCKEN; `MouseCaptureLost` släpper capturen och
    trycken, inte grab och fokus. Den fångande får `mouseCancel(Left)` med
    vänsterknappen nere, annars `None` — och `mouseDrag` bara med den nere efter
    sin egen `middleDrag`/`rightDrag`. **`takePointerCapture()`** (DOCKNING_DRAG
    E3: ett tryck från ett ANNAT av appens fönster lämnas hit och det avbryter
    sitt; true bara på Windows med appen i förgrunden, en krok som stänger,
    flyttar eller tar vidare ⇒ städat och false), **`hasPointerCapture()`**.
    **Text:** `textInput(utf8) → bool`, `caretBlink` och **`wantsTextInput()`** —
    PUBLIK (REQ3): "har ett textfält fokus" är `w->focusedWidget() &&
    w->focusedWidget()->wantsTextInput()`; fönstret läser den för textregeln (ett
    tecken i ett textfält når ingen genväg) och OS:ets textinmatning.
    **Damage:** `invalidate()` (union gammal+ny yta), `invalidateRect`,
    `paintBounds(Ctx)` (resolve + `c.style->overdraw`),
    `restartCaretBlink()`. **`relayoutSiblings()`** (post 16, protected):
    widgetens plats i förälderns FLÖDE ändrades (dold, visad, ny
    remstjocklek eller längd) ⇒ SYSKONEN flyttar men är inte dirty, så
    föräldern ritas om — eller hela fönstret när föräldern är roten, som inte
    ingår i damage-vandringen. Bara för en remsa vid en kant eller ett barn
    till en `placesChildren`-förälder. Kallas av `setVisible` (dock_gate F11)
    och verktygsknappens text och bild; föräldern får `childLayoutChanged`.
    **`damageStamp()`** (CANVAS_3D §3.8, protected) — ändras när något kan
    ha ändrat det widgeten ritar: widgetens EGNA `invalidate()` plus
    fönstrets FULLA (`AppWindow::invalidate`, också efter en körd handler);
    ett barns invalidate, `invalidateRect`/`invalidateOverlay` och den
    RITADE vägen stegar den inte. För en widget som cachar sin ritning
    (ritytans 3D-mål), jämförbar inom samma fönster. Grind: `plumbing_gate`.
    Färgroller: `textColor(c)`/`hotBorder(c)` — de två tillstånds-valen varje
    kontroll gör likadant.
    Ägar-pekaren sätts av `adopt` (fristående = no-op-säkert).
  - `makeWidget(typnamn)` — en widget ur REGISTRET (utan egenskaper/events);
    `GLAZE_REGISTER_WIDGET("typnamn", Klass)` i widgetens EGEN cpp (libglaze
    WHOLE_ARCHIVE i statiskt läge, en DLL med alla objekt i delat) skickar
    också `typeid(Klass)`: **`makeWidgetOfClass(cppKlass)`**/**`widgetClassOf(typnamn)`**
    (🐛 60) är KLASSuppslaget verktygen gör med medlemsradens namn — avmanglat,
    med namnrymden, EXAKT. **`registeredWidgetTypes()`** räknar upp registret
    SORTERAT (`measure_gate`); bara det som FAKTISKT registrerats syns.
- **`widgets/`** — komponent-headerna, en per widget ([egen CLAUDE.md](widgets/CLAUDE.md)).
- **`canvas/`** — RITYTANS typer (CANVAS_2D och CANVAS_3D), listade i sin
  [egen CLAUDE.md](canvas/CLAUDE.md); widgeten är `widgets/canvas.h`.
- **`assets.h`** — asset-API:t: `AssetHandle` (0 = ogiltig = ingen bild),
  `loadAssets(path)` (råbytes FÖRE första fönstret; basePath-kandidater;
  saknad fil = tyst no-op + EN loggrad), `img(lista, nyckel)` (handtag;
  första missen per nyckel loggas), `assetSizeClass` (logiska px). FRIA
  funktioner — `app()` är lat och får inte konstrueras av ett uppslag i
  genererad kod; lagringen är en fristående Meyers-singleton (themeState-
  precedensen). Texturen avkodas lazily PER FÖNSTER (`Ctx::images`).
  Widgetarna TAR ett `ImageSource` (canvas/image_source.h) — handtaget går in
  genom den implicita konverteringen, bredvid en `Bitmap` ur pixlar.
- **`app_window.h`** — `class AppWindow`, basklassen appar ärver: **Livet:**
  `show()` (icke-blockerande; App skapas lat), `show(owner)` (ÄGT —
  kaskad-stängs med ägaren, barnen först), `hide()` (dölj utan att stänga —
  fyrar onHide; show() tar fram + fyrar onShow; dolt fönster ritas inte;
  FÖRE öppningen eller i `onLoad` är den DOLD START — `hidden_`, `onLoad`
  men inte `onShow`, aldrig för en modal),
  **`run()` → int** (visa + pumpa tills stängt — main: `return win.run();`),
  **`run(owner) → DialogResult`** (dialogvägen; med `setModal(true)` filtreras
  input till fönstret — bara modalstackens topp får mus/tangent/text/hjul,
  fönsterhändelser/timers släpps igenom, klick på blockerat fönster lyfter
  modalen), `closeWithResult(r)`, `close()` (uppskjuten — verkställs i App:s
  pump-svans EFTER onClose-vetot; alltid säkert ur handlers), `dialogResult()`
  (nollat av ett veto), **STANDARD- OCH AVBRYTKNAPPEN** (regeln i
  rot-CLAUDE.md): `setAcceptButton`/`setCancelButton` (`Button&` eller
  `nullptr`; pekare + livstecken, så ordningen mot `adopt` är fri),
  `acceptButton()`/`cancelButton()` (null när knappen dött) och
  `isDefaultButton(b)` (den RITADE: fokuserad användbar knapp, annars accept),
  `isOpen()`/`isHidden()`, **fönster-kontrollerna**
  `minimize()`/`maximize()`/`restore()`/ `isMaximized()` (.NET-formen över
  fasaden; växling = `isMaximized() ? restore() : maximize()` —
  CaptionButton-mönstret), **caption-barerna** `topBar()` (null vid
  Native/None; transparent döljer den)/**`statusBar`** och **`menuBar`**
  (publika VÄRDE-FÄLT deklarerade EFTER `root_`, adopterade LÅNADE i ctorn
  efter TopBar — menyraden först — och DOLDA; se widgets/)/`setControlBox`/
  `setMinimizeBox`/`setMaximizeBox` (show/hide på knapparna), `owner()` (ÄGARSKAP — aldrig koordinater), **`commitEdits()`**
  (0.1.2, widget.h). **Livscykel-events** (fråga 15 — regel 6-formen, sender =
  fönstrets rot-panel): `onLoad` (EN gång, med OS-fönstret DOLT — det visas
  EFTER `onLoad`, TODO 80 — startlogikens hem), `onShow`/`onHide`, `onClose(Widget*, CloseArgs&)`
  (`e.cancel` stoppar hela kaskaden, `e.reason` är skälet). Genererad kod kopplar dem KVALIFICERAT
  (`glaze::AppWindow::onLoad = [this](...)`) så fönstrets handler får heta
  likadant. Typen är `Handler<Args>`, så de tar BÅDE fullformen och den
  PARAMETERLÖSA. **Attribut** (före show/run): `setModal()`, `setPopup()`
  (noActivate+ toolWindow; App stänger vid klick utanför/fokusförlust) + **`setPopupTakesKeys()`** (REQ8 27 —
  ägaren för tangenterna till popupens egen ingång, Escape stänger), `setTransparent()` (per-pixel-alfa, chrome av, alltid full omritning),
  **`setTopmost()`/`topmost()`** (APP_API_3 A — alltid överst, över andra
  appars fönster, verksam både före och efter show; gettern svarar med vad som
  FAKTISKT gäller: **Wayland KAN INTE** och svarar alltid false,
  Windows/macOS/X11 klarar den — tabellen i `gfx/window.h`),
  **`setShowInTaskbar()`/`showInTaskbar()`** (SYSTEMFÄLTET B8 — ett RENT
  attribut till `WindowDesc::showInTaskbar`; fasaden har ingen sättare efter
  init, så ett öppet fönster loggar och värdet väntar på nästa öppning),
  **`cornerRadius()`** (hörnradien i FYSISKA px, 0 orundat eller maximerat — EN
  sanning för bakgrunden och TopBars hörn; rundningen kommer ur
  `Style::windowRadius` och behåller chromet), `setOffscreen()` (GLAZE_RENDER: fönstret skapas DOLT —
  `WindowDesc::hidden` — men renderer/font/retained-RT finns; engångs- frame +
  `window().captureFrame` = glaze-renders väg), `setPopupAnchor(rect)`
  (popup-placering för nästa show — ankarrect i ÄGARENS klient-fysiska px;
  backenden placerar under rectet och äger flip/klamp — fungerar även på
  Wayland som saknar skärmpositioner). **Titel/storlek:**
  `setTitle`/`title()`/`setClientSize`/`clientSize()` — genererad kod börjar
  initializeComponent med dem; no-op-säkra före show.
  **`setContentSize(w, h)`/`contentSize()`** (APP_API B2 — INNEHÅLLSYTAN,
  uppskjutet upplöst; `App::applyContentSizeAll`,
  `Widget::notifyContentInsets`) och **`setMinimumSize`/`setMaximumSize`/
  `isFixedSize()`** (APP_API B1; i innehållsmått `set{Minimum,Maximum,Fixed}ContentSize`, REQ8 21): kontraktet
  står i rot-CLAUDE.md, "Fönstrets storlek, tillstånd och tangenter". **`SizeToContent {Manual, Width, Height,
  WidthAndHeight}`** + **`setSizeToContent`** (REQ3, WPF) är ett ATTRIBUT: axeln
  räknas ur rotens barn utom `menuBar`/`statusBar` och följer när en rad visas
  eller döljs, en text, språket, stilen eller skalan byts; den slår
  `setContentSize`/`setClientSize` på sin axel, den fria följer användarens drag,
  och **`SizeToContentMode {GrowAndShrink, GrowOnly}`** (`setSizeToContentMode`,
  REQ7_LAYOUT B2) avgör hur: LÅST (förvalet, effektiva min = max) eller som GOLV
  (max(begäran eller aktuell, innehållet), krymper aldrig själv; `effMin*` i
  cpp:n). `Height` mäter med fönstrets bredd (`childrenExtent(c, bredd)`, uppåt
  avrundat). Tre lager: en synlighetsändring (postad), ritningens fråga
  (`fitDue` mot det oavrundade `fitWanted_`) och före första visningen (efter
  `onLoad`); maximerat eller minimerat inte.
  **RAMEN, PLATSEN, LÄGET OCH PEKAREN**
  (FONSTER_OCH_TIMER, samma avsnitt där): `setChrome(c)`/`chrome()` — ett
  attribut; öppet loggas det och väntar i `nextChrome_`; Custom bygger
  namnlisten LATT (`applyChrome`) och lägger den FÖRST i roten, och `topBar()`
  svarar bara vid Custom. `StartPosition {Default, CenterScreen, CenterOwner,
  Manual}` + `setStartPosition`, `setLocation(x, y)` (före första visningen —
  också i `onLoad` — Manual, efter en flytt i normalläge utan klämning),
  `setWindowState`/`windowState()` (före första visningen startläget, också
  `minimize`/`maximize`/`restore` — aldrig popup/offscreen, aldrig minimerad modal),
  `WindowGeometry` + `geometry()` (begäran · OS-fönstret via `liveGeometry` ·
  KORTET `closedGeometry_` som `App::executeClose` tar) och
  `setUseWaitCursor`/`useWaitCursor()`. Privata **`refreshCursor()`** prövar
  om pekaren med den senast kända muspositionen (`lastMouse_`/`mouseInside_`,
  uppdaterade i modifierar-switchen) — `Widget`s pekarsättare, `disable()`,
  `hide()` och en lossad widget anropar den. **Trädet:** `add<T>()` lägger i den osynliga
  ROT-panelen; `root()` (`root().adopt(medlem)` är genererade kodens
  trädbygge); `find<T>(namn)` söker rekursivt; **Tab-ordning = STABIL sort på
  `tabIndex`** över djupet-först-listan (regeln vid `tabIndex` ovan).
  **`focusOrder()`** är listan som ett LÄSBART värde (`moveFocus` konsumerar
  den — EN sanning; `collectFocusable`/`moveFocus` är privata). Introspektion,
  inte tillstånd: ingen `.glazeui`-form. **`nextFocus(dir)`** (post 12) är stationen
  Tab/Shift+Tab GÅR till (`moveFocus` = `setFocus(nextFocus(dir))`), med start
  i den NÄRMASTE stationen bland det fokuserade och dess FÖRFÄDER — fältet
  inuti en komposit står inte själv i ordningen. Publik av samma skäl.
  **`clearFocus()`** (DOCKNING E1) — fönstret UTAN fokus (WinForms
  `ActiveControl = null`): dockningsvärdens reserv när inget stopp finns.
  **`isActive()`** (tangentbordsfokus ur OS:ets fokushändelser; en popups räknas
  till ägarkedjans rot), **`focusedWidget()`** (WinForms `ActiveControl`, kan vara
  en komposits inre fält) och **`tryShortcut(k, mods)`** (fönstrets genvägar —
  menyraden, sedan programmenyn; efter true kan fönstret vara rivet) — REQ3.
  **`mousePosition()`** (E2) — pekarens senast kända position i klientytan
  (fysiska px, nullopt när den lämnat fönstret) för den som måste veta om
  pekaren står över en YTA utan en händelse: overlayns lämna-prövning —
  `hovered()` är falsk över ett INAKTIVERAT barn.
  **`acceleratorFallback(k, mods)`** och **`ownerVisibilityChanged(shown)`**
  (E3, protected) — reservvägen för en tangent ingen tog (efter fönstrets
  egna genvägar, före Tab) och beskedet att ägaren dolts eller visats; den
  flytande dockningsfönstret (`friend class DockFloatWindow`) svarar med
  ägarens, och panelens eller fältets död släpper allt med `forgetWidgets`.
  Vännen sätter också de interna `noActivate_`/`showInactive_` (DOCKNING_DRAG L6 — `showOwned`
  kopierar dem). Privata `dropPointer()` är EN väg för ett tryck som förlorades utan släpp,
  och `AppPress::end()` tryckens EN nollställning (målet släpps, identiteten stegas).
  **`toolTipWidget()`** — introspektion: widgeten vars tooltip VISAS just nu, annars
  nullptr. **`isDragging()`** — introspektion och inte tillstånd: sant från att tröskeln
  passerades och `onDragStart` gav en last till att `onDragEnd` körts (sessionen bor i fönstret).
  **`handleEvent(e)` är PROTECTED** — en PRODUKTYTA (WinForms `WndProc`): en
  subklass, och grindarna som mäter routing, matar sin egen ingång med ett
  syntetiskt event på OS-eventets väg; kontraktet står vid deklarationen.
  **`display()`** — skärmens `gfx::DisplayMetrics`
  med appens kalibrering, CACHAD: mäts vid show, vid `WindowDisplayChanged`/
  `WindowDisplayScaleChanged` och när `App::setDisplayCalibration` körs;
  `makeCtx` ger den som `Ctx::pxPerMm`. **`modifierKeys()`** (post 17,
  WinForms `Control.ModifierKeys`) — masken ur den SENASTE mus-, tangent-,
  hjul- eller gesthändelsen, uppdaterad FÖRE appens `event()`-krok — för
  krokar utan args (listvyns Ctrl-/Shift-klick, ritytans hjul). En tryckt Ctrl ENSAM
  syns direkt på alla plattformar (fasadens kontrakt vid `gfx::Event::mods`).
  **DUBBELKLICKEN RÄKNAS HÄR** för alla tre knapparna, liksom tangentminnet och
  knappmasken (bokföringen i src/). **`isKeyHeld(Key)`** (CANVAS_3D §3.11,
  `modifierKeys()`s mönster) — de hållna tangenterna, uppdaterade FÖRE
  `event()`-kroken (också för ett event kroken äter) och TÖMDA vid
  `WindowFocusLost`, `hide()` och stängningen. Mängden bär den OVIKTA posten
  (`isKeyHeld(KP_End)` svarar bara för numpaden), men en DEDIKERAD tangent
  svarar som en widget ser den (`isKeyHeld(End)` också för numpadens End,
  `gfx::navigationKey`), och numpadens två lägen släpps som EN tangent.
  `Unknown` hålls aldrig. Till för det som frågas PER BILDRUTA (ritytans
  tickning, förstapersonens WASD) — ett läge som växlar lyssnar på `onKeyUp`.
  Grind: `tests/plumbing_gate.cpp`. **NUMPADENS VIKNING** (TODO-post 31) —
  regeln står i rot-CLAUDE.md; grind `tests/numpad_gate.cpp`.
  **Övrigt:** `invalidate()` (= FULL omritning), `invalidateOverlay()`,
  `setAnimating()`, `setDebugDamage()`, utseende-API:t **`setUI(UI)`/ `ui()`**
  (ritningen — ÅTERSTÄLLER theme+style till familjens defaults),
  **`setStyle(const Style&)`/`style()`** (måtten — laddar om fonten i alla
  öppna fönster, eftersom Style äger `uiFontSize`), `setTheme()`/`theme()`/
  `setThemeMode()`/`setColorsLight/Dark()` — **APP-GLOBALT**
  (`App::themeState`): en setter ritar om ALLA öppna fönster. Hooks
  `frame(Ctx)`/`event(gfx::Event)`/`overlay(Ctx)` (därefter widgetarnas
  overlay-lager, drag-återkopplingen, damage-debuggen) + protected
  `makeCtx()`. `frame()` MÅSTE rita deterministiskt (körs per damage-rect vid
  partiell omritning). Caret-timern ägs här. Fönstret skapas i ctorn via
  `gfx::createWindow()` (tvåfas — OS-fönstret öppnas i `show()`); `font()`
  giltig först då. Alla ctor-argument har default (genererad ctor är tom,
  titel/storlek via settrarna). **`appIcon`** (PROGRAMIKON,
  `widgets/app_icon.h`) är det tredje publika värdefältet efter
  `menuBar`/`statusBar` men SYNLIGT som förval, adopterat LÅNAT SIST i ctorn
  och deklarerat sist (rivs först); det ligger i CHROME-trädet som `topBar_`,
  och **`isChromeRoot(w)`** är frågan de sex vandringarna ställer (skadan,
  full och partiell ritning, lagren, hit-testen, `ctxFor`).
  **`applyWindowIcon()`** (privat) sätter OS-fönstrets `gfx::WindowIcon` efter
  `win_->init` och vid ikonens settrar ur `detail::osIcon` (samma regel): `noIcon` ⇒
  tom lista, appens bild ⇒ `programFirst = false`, förvalet ⇒ `programFirst =
  true`; aldrig för popup eller offscreen-fönster. `friend class AppIcon`, och
  `friend class TrayHostWindow` (systemfältsmenyns värd: `toolWindow_` — ett
  verktygsfönster som TAR fokus — och `initX_`/`initY_`).
- **`color.h`** (REQ9_TERMINAL B9) — FÄRGHJÄLPARNA `glaze::color::hsl`/`mix` (klampad, `t` > 1 extrapolerar)/`luminance`/`contrast` (WCAG)/`hueDegrees(Hue)`: temats egen matematik, publik för ett sidopaket som HÄRLEDER färger ur temat (terminalens schema); `contrast_gate` mäter med samma funktion. Inget tillstånd (`src/color.cpp`).
- **`theme.h`** — PALETTEN och ALLA färger: `ThemeMode { System, Light,
  Dark }` + **`enum class Hue`** (Neutral/Red/Orange/Amber/Green/Teal/Blue/
  Indigo/Violet + `kAllHues`/`kHueCount` — ENUM och inte fri vinkel, för
  kontrasten varierar kraftigt med kulören och ett enum går att GRINDA) +
  `class Theme`. Två HANDSKRIVNA paletter: **`slateDefault()`** =
  "Windows" (Win11-gråskalan, REFERENSEN — grindas inte) och
  **`glazeDefault()`** = "Lavender" (husets violetta palett, KNAPP.md; Glaze
  har med flit BARA det temat). Två GENERERADE familjer: **`Theme::slate(Hue)`** och
  **`Theme::minimalistic(Hue)`** — nio paletter var, byggda ur en
  ljushetstrappa i theme.cpp och grindade av `tests/contrast_gate.cpp`.
  Namnrymden är PER UI: "Neutral" för Slate och för Minimalistic är två
  olika paletter, eftersom Slate ritar fyllda ytor där Minimalistic ritar
  kanter. Fria funktioner **`themeCount(UI)`/`themeAt(UI, i)`/
  `defaultTheme(UI)`** är EN sanning om vilka teman en ritning har (index
  0 = default). `Variant light/dark` bär `Colors`; `resolve(mode)` frågar
  `gfx::systemTheme()` per repaint i System. Rollerna (namnen i theme.h) täcker
  kontrollerna, **accentytan** (`accentSurface`… — skild från `accent`: ytan
  som bär vit text och textfärgen är två olika färger), **lamineringen**
  (`sheen`/`sheenHigh` — alfan i färgen, andelarna i Style; helt
  genomskinliga = ingen laminering, så ser Slate ut), chromet,
  **trafikljusen** (Mac-namnlistens cirklar) och **KAPSELKLUSTRET**
  (de fem `capsule*` — fönsterknapparnas form). De HÄRLEDS av `deriveCapsule()` i theme.cpp för varje palett, handskriven
  som genererad, och står därför SIST i structen och saknas i de positionella
  literalerna, UTAN ACCENT ("inramad, men står inte ut" — receptet i theme.cpp).
  Sedan **`fieldReadOnly`** (den SKRIVSKYDDADE fältytan: en EGEN roll och
  inte disabled-tillståndet, för fältytan ÄR redan `panel`; härledd som
  kapselrollerna, halvvägs mellan `windowBg` och `appWorkspace`, och grindad
  `text ≥ 4.5:1` + `buttonBorder ≥ 3:1` — texten ska LÄSAS, och i
  Minimalistic ÄR kanten hela fältet). Sedan **STATUSROLLERNA**
  (warningSurface/onWarning/errorSurface/onError — meddelanderutans badge; de
  ENDA roller som inte följer familjens kulör eller läget; härledda i
  `deriveStatus()`, `on*` MÄTT som `onAccent`, ≥ 4.5:1 i alla tjugo teman i
  contrast_gate). Slutligen **länken** (link/linkHover — en länk är
  inte alltid `accent`; paletten ska kunna välja fritt), **`dropHint`**
  (dockningens släppindikator: `accentSurface` med alfa 0.25, HÄRLEDD i
  `deriveRoles()` så innehållet under syns; ramen runt den är `accent`) och
  **`canvasGrid`** (ritytans rutnät: en femtedel från `panel` mot `text`,
  HÄRLEDD; tål att halveras av en hårlinje och tonas av en inaktiverad yta).
  En ny färg i en widget = en ny roll HÄR + värden i BÅDA varianterna.
  **`standardPalette()`** är INTE en roll: färgväljarens förvalda färger
  (`ColorPalette` — färgerna + rutnätets `columns`), INNEHÅLL som är detsamma
  i varje tema och läge; färger, och därför här och inte i widgeten.
- **`style.h`** — RITNINGSVALET och ALLA mått. **`enum class UI { Glaze,
  Slate, Minimalistic }`** (Slate = basen och `default:`-grenen, en designad
  bas i fyra klasser Yta/Platta/Fält/Spår; **VARJE `draw()` har alla tre
  grenarna, och samma ritning i två familjer är ett SKRIVET beslut i cpp:n**)
  (VALET — app-globalt, bärs i `Ctx::ui`; varje widget switchar på den själv,
  så en ny ritning kan införas widget för widget) + **`struct Style`**
  (TABELLEN — `Ctx::style`, alla mått i LOGISKA px; bär ett `name`). **VARJE
  UI ÄGER SINA EGNA styles**, precis som sina teman: uppslaget går via
  **`styleCount(UI)`/`styleAt(UI, i)`/`defaultStyle(UI)`** (index 0 = default)
  och det finns AVSIKTLIGT ingen platt global lista där "Regular" kunde betyda
  vad som helst (talen per ritning står i rot-CLAUDE.md). Fältet
  **`uiFontSize`** är UI-textens storlek (radhöjden sätter golvet för varje
  kontrollhöjd; `gfx::fontsize::kUi` förblir enda SIFFERkällan).
  **`headingFontSize`** är RUBRIKENS storlek (`Label::Role::Heading`, APP_API
  D1 — appen säger VAD texten är, stilen HUR STOR, och mängden fontstorlekar
  är SLUTEN). **`statusBarH`** är statusradens höjd. Övriga fält är grupperade per
  widget (`checkSize`, `fieldPadX`, `listRowH`/`listPadX` — DELADE av ListBox
  och dropdownens popup, därför utan widgetnamn; `chevron*`/`dropMaxRows` är
  Dropdownens, `scrollThick`, `gridRowH`, `barHeight`, `trafficSize`,
  `separator*`, `link*`, `radio*`, `slider*`, `progress*`, **`menu*`**
  (`menuRowH`/`menuPadX`/`menuSepH`/`menuShortcutGap`/`menuMinW`/
  `menuBarPadX`/`menuArrowW`/`menuMaxRows` — MEDVETET egna tal och inte lån av
  `listRowH`/`listPadX`); remsornas AUTO-längder `separatorDefaultLen`/
  `sliderDefaultLen`/`scrollDefaultLen`/`progressDefaultLen` (avsiktligt SAMMA tal — se noten i
  style.cpp); Buttons `buttonRadius` (negativt = piller), `buttonPadX/Y`,
  `buttonShadowDy/ Blur` och `buttonPressShift`; Sliders `sliderShadowDy/Blur`
  — EGNA fält och inte knappens under ett annat namn; `rowMarkerW` = en RADS
  vänstermarkör, delad av PropertyGrid och Dropdown-listan; **`windowRadius`**
  = HELA fönstrets hörnradie, 0 = fyrkantigt — ett rundat fönster kräver en
  GENOMSKINLIG yta, så måttet är samtidigt en begäran om per-pixel-alfa, och
  kapselns **`capsuleH`/`capsuleSeg`/`capsuleInset`/`capsuleStroke`** hänger
  ihop med det: radien är HÄRLEDD så klustrets rundade ände och fönsterhörnet
  blir koncentriska, med samma luft runt om som över och under kapseln) plus
  det delade (`cornerRadius`, `borderWidth`, `focusOffset/Width`,
  **`sheenLower/sheenUpper`** (lamineringens andelar 0–1, fem widgets),
  `raise*` — SLATES lyft, Glaze använder buttonShadow* i stället —
  `overdraw`). **Verktygsfältet:** `toolBarH`/`toolBarPadX`/`toolBarPadY`/
  `toolGap`/`toolButtonPadX`/`toolButtonPadY` — HÄRLEDDA i par (remsans höjd
  = ikonknappens naturliga höjd + 2 × `toolBarPadY`, så en knapp i remsan och
  en fri knapp blir lika höga; `toolbar_gate` grindar meningen i alla tre UI)
  — och **`iconFade`**, hur mycket en inaktiv ikon TONAS bort, DELAT av Button
  och ToolButton (opacitet 1 − iconFade, se `Content::imgOpacity` nedan).
  **Drag-and-drop:** `dragThreshold` (hur långt ett tryck får darra och ändå
  vara ett tryck — euklidiskt, MER än krävs), `dragOffset`/`dragBadgeSize`
  (märket vid pekaren), `dragGhostPadX/Y`/`dragGhostMaxW` (dragbildens platta)
  och `dropLineW` (målets ram och en lista/ett träds insättningslinje) — INGA
  nya temaroller, återkopplingen ritas i befintliga (`accent`,
  `errorSurface`/`onError`, `accentSurface`/`onAccent`, `panel`/`border`).
  **Komponenternas block** (namnen i style.h, skälen vid talen i
  style.cpp): gruppramen (`group*` — en EGEN radie, inte `panelRadius`),
  talfältet (`spin*`; AUTO-höjden är FÄLTETS, så talfält och textfält står
  i rad), trädet (`treeIndent`/`treeExpanderW` lika stora, så ett barns pil
  står under förälderns text; raderna är LISTRADER; `treeDropEdge` är ett
  BETEENDEMÅTT som `dragThreshold`), tooltipen (`toolTip*` — zonen aldrig
  0, `src/tooltip.cpp`), tabellen (`tableHeaderH` = `listRowH`,
  `columnDefaultW`, `columnMinW`, `columnGrip` — beteendemått; raderna
  LÅNADE: listvyns listrader, datagridens gridrader), delaren och dockningen
  (`splitter*` DELADE av `Splitter` och `DockHost` via `src/sash.cpp`,
  `dock*` med `dockDropZone` som beteendemått), flikarna (`tab*`; markören
  och sidans yta LÅNADE — `rowMarkerW`, `panel`/`border`), filväljaren
  (`iconStroke`, `fileDlg*`, `fileCol*`, `filePicker*` — Glaze ärver Slates
  med flit), färg- och datumväljaren (`color*`, `date*`, `cal*` — popupens
  storlek räknas ur dem FÖRE visningen), det flerradiga fältet (bara
  AUTO-måtten `textAreaDefaultW` och RADANTALET `textAreaRows`; resten är
  fältets och Panels), ritytan (`canvasDefaultW/H`, `canvasGridMin`,
  `canvasGridWidth` 0 = hårlinje, `canvasHitTolerance`/`canvasPanStep`) och
  programikonen (`appIcon*`; platsen härleds ur `barHeight` + `menuRowH`,
  plattan i `capsuleHover`/`capsulePress`). **`passwordMask`** (`const
  char*` — "•" U+2022, ETT glyfsteg per kodpunkt, får aldrig vara tom).
  Sedan blocken i passens ordning, det senaste SIST: dockningen
  (`dockHandle*`, `dockSnapZone`, `dockAnim*`/`dockHideDelayMs`,
  `toolGripW`, `toolArrow*`, `dockDropShare`), FAS1_B_WIDGETS,
  REQ3_TRAD_LISTOR_FALT (autoskrollens `dragScroll*`, `textAreaTabSpaces` —
  ett ANTAL), REQ7_MARKDOWN (`subheadingFontSize`, `markdown*`) och REQ8 9
  (`toolOverflowW`). Ett nytt tal i en widget = ett nytt fält här, aldrig en
  `constexpr` i widget-filen.
- **`draw.h`** — RITANDET: `struct Draw` (statiska primitiver), `Pen` (kontur;
  width i logiska px, 0 = HÅRLINJE), `Align`, `ImagePos` och `Content` (bild —
  ett `ImageSource` — + text som EN sak, med **`imgOpacity`**, bildens OPACITET i inaktivt läge,
  som `Renderer::drawImage` multiplicerar in i bildens alfa, så ikonen tonas
  mot VILKEN yta som helst; SIST i structen för `Button::content`s
  positionella init), **`Sign`** (märkets tecken: Plus/Minus/Arrow —
  drag-and-drops effektmärke) och **`Icon`** + **`Draw::icon(c, ruta, ikon,
  penna)`** (de RITADE ikonerna Folder/File/Document/Drive/House/Monitor/
  Download/Picture/ArrowUp/Calendar och knappnålen Pinned/Unpinned (DOCKNING
  E2) — en linjeteckning i en enhetskvadrat centrerad i rutan: ramverket har inga egna
  BILDER, och en ritad ikon är skarp i varje skala och följer pennans färg; namnen säger vad man SER).
  **Koordinaterna är widget-relativa LOGISKA px** —
  `c.bounds()` är widgetens yta med (0,0) i eget hörn, och Draw lägger på både
  origo (`c.box`) och skalan; därför står det varken `resolve()` eller `*
  c.scale` i ritkod. Primitiver: ytor (`rect` ×2 / `frame` / **`frameGap`**
  (kontur med en LUCKA i överkanten — gruppramens rubrik i linjen; en lucka som når ett HÖRN kapas
  innanför sidlinjen och förbi hörnbågen, sidorna hela) / `shadow` / `circle` ×2 — fylld och
  fylld+konturad / **`checker`** — rutmönstret bakom en genomskinlig färg), `line`,
  **`penWidth(c, pen)`** (pennans bredd i LOGISKA px
  — hårlinjen är 1/skala; flikkontrollens lucka är flikens insida MELLAN dess
  sidlinjer), **`laminate`** (två klippta ljusfält över en fylld yta — Glazes
  material; andelarna och färgerna är PARAMETRAR, för Draw får aldrig känna
  till en widgets Style-fält), glyf-formerna (`check` / `chevron` (riktningen
  **`Draw::Dir`** — ∧∨‹› är samma tecken vridet) / `cross` / `plus` / `minus`
  — proportionerna är TECKNETS definition, storleken är rutan man skickar in —
  och **`badge`** (post 21: en fylld skiva med ett `Sign` i mittersta halvan,
  strecket en åttondel av diametern; drag-and-drops effektmärke) och
  **`grip`** (DOCKNING E4b: två rader prickar längs rutans långa axel —
  verktygsfältets grepp), text & bild (`text` / **`textWrapped`** /
  **`textElided`** (post 17 — KORTAD med en ellips på teckengräns via
  `fitWidth`, ingenting när inte ens ellipsen ryms; `elide(c, s, maxW)` är strängen den ritar, mätbar utan pixlar) /
  `glyph` (ett `ImageSource` genom `ImageCache::get(img, c)`, med en valfri
  OPACITET) / `content`) med mätningen i logiska px
  (`measure` ×2 / **`measureWrapped`** / `imageSize` (bildens naturliga mått,
  pixlar / skala — en Bitmap/RenderTexture ur handtaget UTAN uppladdning,
  ogiltig ⇒ {0,0}) / `lineHeight` / **`ascent`** (baslinjen — blandade fonter på EN rad) / **`withFont(c, familj, stil, storlek)`** (REQ9_TERMINAL B14: Ctx:t med Mono/fet/kursiv eller en fri storlek ur fönstrets textfonter — utan dem, eller i världsenheter, svarar den `c`) /
  `prefixWidth` / `byteIndexAtX`),
  klipp (`clip` / `endClip`), rymd (`inset` / `project` / `bounds` / `localX`
  / `localY`) och rullningslisterna (`ScrollMetrics` + `scrollThumb` /
  `scrollThumbDraw` — Scrollbar och Panels overlay-remsor ritar SAMMA pixlar ⇒
  EN sanning). **Draw innehåller INGA konstanter, INGA mått och INGA färger**
  — allt sådant kommer in som argument; det enda Draw vet på egen hand är DPI
  (växtregeln är rotens regel 3). `project`/`bounds`/`localX`/`localY` är
  bryggan till de FYSISKA px som mät- och damage-kontrakten talar: geometri
  definieras EN gång i logiska px och projiceras, aldrig räknas två gånger.
  **RADBRYTNINGEN** (`textWrapped`/`measureWrapped`) ligger ovanpå
  `gfx::wrapText` (ordgräns, `\n` alltid, för brett ord på UTF-8-teckengräns):
  in logiska px (`wrapText` får `maxW * c.scale`), radblocket centrerat i recten
  med en `lineHeight` per rad, och ALLTID minst en radhöjd — tom text och
  avslutande `\n` ger en tom rad (`msgbox_gate`). **`wrapLines`** (publik)
  är RADERNA de två delar, som byte-intervall i texten — för en widget som
  äger sina rader själv (det flerradiga fältets caret, markering och rullning)
  och ändå ska bryta exakt där en radbruten etikett bryter. Ett radbrytande
  blanksteg ingår i ingen rad, så raderna täcker inte alltid texten.
  **`elideLines(c, s, maxW, maxLines)`** (post 52 — kolumnhuvudets rubriker)
  är raderna för en FAST höjd: `\n` och ordgräns via `wrapLines`, men ett för
  brett ord KORTAS i stället för att hårdbrytas, och den sista synliga raden
  bär resten av sitt stycke med en ellips; `maxLines <= 0` = alla (tabellens
  AUTO-höjd), och en rad utan `\n` är exakt `elide`. Höjdledens enum är
  **`VAlign`** (Top/Center/Bottom) — ett eget och inte `Align`, för vänster
  och höger betyder ingenting där. Grind: `tests/listview_gate.cpp`.
  **`Draw::glyphResampled(c, r, img, opacity)`** (PROGRAMIKON) ritar en bild
  SKARPT nedskalad på CPU till målrutans fysiska px
  (`ImageCache::getResampled`, cache per (id, w, h); en Bitmap ur sina
  levande pixlar, en RenderTexture med `glyph`) i stället för GPU:ns
  linjära filter, och **`Draw::pickIcon(c, r, {källor})`** väljer källan med
  valregeln (`glzassets::pickIconSource`) — programikonens två primitiver. **`Draw::halo(c, bredd, paint)`** (REQ8 24) anropar `paint` med ett Ctx förskjutet till varje punkt på en ring — `Label::setHalo`s kontur.

- **`language.h`** + **`strings.h`** — SPRÅKPAKETET: **`TrText`**
  (bär ANTINGEN en literal ELLER ett översättnings-`id` och cachar sin
  upplösning i sig själv mot en global GENERATIONSRÄKNARE; ctorerna från
  `const char*`/`std::string` är IMPLICITA med flit, så `setText("Hej")`
  kompilerar oförändrat), **`glaze::tr(id)`**,
  `loadLanguage`/`loadLanguageBytes`, `setLanguage`/`language`/
  `languageSource`, `setFallbackLanguage`/`fallbackLanguage`,
  **`unloadLanguage`** och `languageCount`/`languageAt` (underlaget för en
  språkväljare — `LanguageInfo` bär SJÄLVBENÄMNINGEN, som aldrig översätts).
  **Den initiala fallbacken kommer ur RAMVERKSFILENS `defaultLang`** — aldrig
  ur en literal i C++, annars vore headerfältet dekoration.
  **`loadLanguage(path, fallbackTag)` byter fil och fallback som EN
  TRANSAKTION** (annars kan de låsa varandra — `lang_gate`), och
  `unloadLanguage()` släpper appbutiken. `strings.h` är
  ramverkets HANDSKRIVNA konstanter (id 1–999; blocken står i huvudet på
  `Glaze/language/framework.langtxt`); appens 1000+ genereras av
  `glazelanguage pack --header` och committas. **Id 24 `DecimalSeparator`**
  och **id 80 `FirstWeekday`** är SPRÅKFAKTA och inga etiketter: talfältets
  decimaltecken (`,` sv/nb/fi, `.` en) och veckans första dag som ISO-nummer
  (`"1"` på alla fyra — språket bär ingen region); `gfx::systemLocale()` är
  språkVALET, ingen talformatering. **`TrText::str()` returnerar `const
  std::string&`** och referensen avser STRÄNGOBJEKTET, som lever lika länge
  som `TrText`:en — innehållet skrivs om IN PLACE vid språkbyte. `str()
  const&&` är RADERAD: `tr(id).str()` hade dinglat, och fällan fångas därför i
  KOMPILERINGEN. **Ingen `operator==(std::string_view)`** — den blir tvetydig
  mot den implicita ctorn, så `t == någonSträng` hade slutat kompilera.
  Jämförelsen är TOKENsemantik: id mot id, literal mot literal, och BLANDNING
  är ALDRIG lika. ⚠ **TRÅDKONTRAKT:** allt här är UI-trådsoperationer (den
  lata `mutable`-cachen är inte trådsäker) — `glaze::app().post()` är enda
  vägen in från en annan tråd. ⚠ Och **`setLanguage` får inte anropas SYNKRONT
  ur en handler som fyras ur ett POPUP-fönster**: `Dropdown:: commit()` fyrar
  `onChange` och stänger FÖRST DÄREFTER, medan `setLanguage` river popups synkront — posta bytet.
- **`menu.h`** — MENYERNA (MENYER): `Shortcut {Key, mods}` + `shortcutText`
  (MATCHNING och PRESENTATION är SKILDA — visningstexten härleds och är
  plattformsberoende), `MenuItem` (text/ikon (`ImageSource`)/genväg/enabled/checkable/
  checked/radio/separator/`submenu` + **`Handler<ClickArgs> onClick` PER
  POST**) och `class Menu` (`items` + `addItem/addCheck/addRadio/
  addSeparator/addSubmenu`, `popupAt(host, x, y)`/`popupAtRect`,
  `close()`/`isOpen()`, `tryShortcut`, `activatePath` — den ENDA vägen in i en
  aktivering, delad av klick, Enter och accelerator). `MenuItem::text` är en
  `TrText`, stigen en **segmenterad `MenuPath`** (fyra ctorer — `const
  char*`/`std::string`/`TrText`/`initializer_list<TrText>`; ett snedstreck i
  ett literalsegment AVVISAS med logg), matchningen är `menuTokenMatches` (id
  mot id, literal mot literal, BLANDNING = typfel ⇒ no-op), och genvägen är
  KLUVEN: `canonicalKeyName`/`parseCanonicalKey` är den STABILA engelska
  formen `.glazeui` bär, `shortcutText` den ÖVERSATTA visningen. INGEN widget
  (meddelanderutans skäl); visningen (`MenuPresentation`/`MenuWindow`/
  `MenuRows`) bor i `../../src/menu.cpp`. `Menu` är varken KOPIERBAR eller FLYTTBAR: dess
  öppna presentationer pekar tillbaka på den, och de når posterna via
  INDEX-STIGAR i stället för pekare eftersom en `push_back` i en
  `submenu`-vektor reallokerar under en öppen meny. **`MenuPath() = default`**
  (PROGRAMIKON B6) är den TOMMA stigen = programmenyns ROT
  (`appIcon.addItem({}, …)`) och loggar inte; en UNDERKÄND stig (tomt segment,
  `/`) bär flaggan som **`valid()`** läser och hamnar ALDRIG i roten;
  `parent()` av ett led ger `MenuPath{}`, `debugText` `<ogiltig>`. Menyraden
  avvisar fortfarande en tom stig. Stigupplösningen och add-kropparna delas
  med AppIcon, TrayIcon och ToolButton i `../../src/menu_path.h`.
- **`message_box.h`** — **`glaze::messageBox(...)`**, meddelanderutan
  (MESSAGEBOX): `MessageBoxType` (Custom/Information/Confirmation/Warning/
  Error — husets vokabulär, `Confirmation` där .NET säger `Question`; `Custom`
  = ingen default-ikon, alltså platsen för en egen bild — ett `ImageSource`,
  asset eller pixlar), `MessageBoxButtons` (OK/OKCancel/YesNo/YesNoCancel/
  RetryCancel/AbortRetryIgnore) och två överlagringar (ägd + ägarlös). INGEN
  widget (därför i katalogroten: registreras inte, adopteras inte, aldrig i
  `.glazeui`; fönstret bor i `../../src/message_box.cpp`). En FRI FUNKTION,
  knapparna ett ENUM (wx-idiomen mappar 1:1, se headern).
  **`DialogResult::None` läcker aldrig ut** — Escape, krysset och ägarens
  kaskadstängning mappas till uppsättningens avbryt-ekvivalent, så
  anropsstället kan skriva `if (r == Yes)` utan ett fjärde tyst utfall. De
  två uppsättningar som saknar avbryt får krysset SLÄCKT.
- **`file_dialog.h`** — **`glaze::FileDialog`**, filväljardialogen
  (wxFileDialog + wxDirDialog). VÅR, aldrig OS:ets. INGEN widget
  (meddelanderutans prejudikat: registreras inte, adopteras inte, finns aldrig
  i `.glazeui` — fönstret bor i `../../src/file_dialog.cpp` och den interna
  `file_dialog_window.h`). **EN KLASS med egenskaper + `run(ägare)`/`run()`**
  och inte en fri funktion som grannen (skälet står i headern); ett
  BEHÅLLET objekt öppnar där användaren slutade (`folder()` skrivs vid OK).
  **TRE LÄGEN ÖVER EN IMPLEMENTATION**: `FileDialogMode` Open/Save/Folder
  (filväljarfältet bär läget som setter-fält). Egenskaperna: titel (tom = lägets),
  `folder` (startmappen — finns den inte, närmaste befintliga förälder),
  `fileName` (förvalt namn), filtren i KOLLEKTIONSVOKABULÄREN (`FileFilter
  {TrText name; std::string patterns}` — `addFilter`/`setFilter`/
  `setFilters`; visas "Bilder (*.png;*.jpg)", utan filter "Alla filer (*.*)"),
  `filterIndex` (in och ut), `multiSelect` (bara Open), `overwritePrompt`
  (bara Save, PÅ — WinForms `OverwritePrompt`) och **`showHidden`** (AV —
  punktnamnen och det OS:et döljer; dialogens kryssruta byter listan direkt
  och SKRIVER TILLBAKA valet, också om dialogen sedan avbryts: en
  vyinställning, medan `folder`/`fileName` bara skrivs vid OK). Svaret är OK
  eller Cancel — **`None` läcker aldrig ut** — och `path()`/`paths()` (UTF-8, absolut) töms vid varje `run`.
- **`color_dialog.h`** — **`glaze::ColorDialog`** (FAS1_B_WIDGETS, WinForms
  `ColorDialog`): filväljarens form — en KLASS med `setColor`/`color()`,
  `setAllowAlpha` och `setTitle` (tom = id 256), `run(ägare)`/`run()` →
  OK/Cancel och färgen skriven bara vid OK — med färgväljarens yta
  (`../../src/color_panel.h`). INGEN widget (meddelanderutans prejudikat).
- **`input.h`** — INMATNINGENS VOKABULÄR (REQ9_INMATNING), ren std: `InputDevice`, `InputSource` (en axel per källa),
  `InputAction`, `InputDirection`, `InputBinding`, `deviceOf`. `Widget::setWheelInvert` ärver från förfäderna till fönstrets rot
  (`AppWindow::setWheelInvert`) och sedan appen; tabellen och uppslaget är interna (`../../src/canvas/input_map.h`).
- **`input_box.h`** — **`glaze::inputBox(...)`** (FAS1_B_WIDGETS, VB
  `InputBox`): meddelanderutans form, en FRI FUNKTION (ägd + ägarlös) →
  `std::optional<std::string>` — `nullopt` = Avbryt, en tom text är ett svar. `InputBoxMode::MultiLine` (REQ8 22): fältet blir ett `TextArea`, Enter en ny rad, OK med knappen.
- **`stock_icon.h`** — **`glaze::stockIcon(StockIcon, sizeClass)`** (REQ8 12): standardikonerna som `ImageSource` (`Kind::Stock`) — vektorer i `../../src/stock_icon.cpp`, rastrerade i ritningens fysiska px i temats textfärg; `StockIconStyle::Color` lägger accentdelar i temats härledda `stock*`-roller (theme.h); `stockIconName`/`ByName` är `.glazeui`-formens namn.
- **`tray_icon.h`** — **`glaze::TrayIcon`**, systemfältets ikon (WinForms `NotifyIcon`): Windows meddelandefält,
  macOS menyradens statusposter, Linux `isSupported() == false`. INGEN widget
  av `Menu`s skäl — ikonen sitter i OS:ets fält och är APPENS (ett värdefält
  på `AppWindow` hade gett varje dialog en). Sedan FONSTER_OCH_TIMER en
  `glaze::Component` och därmed i `.glazeui` som icke-visuell medlem
  (`setProp` tar `interval`/`templateImage`/`toolTip`, `setTextToken`
  tooltipen; previewn utför aldrig `show()`). FÖRVAL DOLD; `show()`
  skapar App lat. Bilden (`setIcon`/`setSmallIcon`, `ImageSource`; ogiltigt = förvalet med
  `programFirst`; `setTemplateImage`), tooltipen som `TrText` och ANIMATIONEN
  i kollektionsvokabulären (`addFrame`/`setFrame`/`setFrames`, `setInterval`
  klampat till `kMinIntervalMs`, `startAnimation` en BEGÄRAN som går bara
  synlig och med minst två bilder). Menyn har `AppIcon`s platta form med `{}`
  som roten men UTAN genväg (+ `setItemIcon/Enabled/Checked`); `openMenu()` öppnar vid ikonen. Events med
  IKONEN som sender (`Handler<Args, TrayIcon>`): `onClick`, `onDoubleClick`,
  `onContextMenu` (`TrayMenuArgs`, FÖRE menyn) och `onHover`. Varken
  kopierbar eller flyttbar; det interna bakom `unique_ptr<detail::TrayState>`
  (den enda vännen) — maskineriet i `../../src/tray_icon.cpp`.
- **`profiler.h`** — **`glaze::profiler()`** (PRESTANDA A1): var tiden i en
  bild går. ALLTID inkompilerad och AV som förval (`setEnabled` från vilken
  tråd som helst, eller `GLAZE_PROFILE` — läst vid första `profiler()` och
  när App skapas), läst genom HÄMTNING (`frames`/`pump`/`summary`/`scopes`/
  `report`) — ingen callback, den väcker aldrig pumpen. TVÅ SLAGS DATA:
  bilderna (`FrameProfile` — `ProfilePhase`, `ProfileCount`, `continued`,
  `outsidePump`) och pumpvarven (`PumpProfile` med `depth`) är UI-trådens, i
  en ring per fönster (nycklad på `const AppWindow*`, släppt när fönstret
  stängs) och en för pumpen; ETIKETTERNA (`ProfileLabel`/`ProfileScope`/
  `profileCount`, förregistrerade `glaze.hitTest`/`castRay`/`heightAt`) från
  vilken tråd som helst, ett logaritmiskt histogram per etikett (8 fack per
  fördubbling — percentilen ≤ ~9 % hög). Tilläggen mot planen: `capacity()`,
  `ProfilePhase::Compile`, `PumpProfile::depth`, `profilePhaseName`/
  `profileCountName`. INGA statiska variabler och inga inline-variabler i
  headern (DELAT LÄGE (1)); registret bor i cpp:n. Utanför regel 8 med flit.
- **`print.h`** — **`glaze::PrintJob`** (REQ8 10): systemets utskrift, `FileDialog`s klassform; sidan med ritytans `Painter` i mm, rastrerad (besluten i headern).
- **`component.h`** — **`glaze::Component`** (FONSTER_OCH_TIMER, WinForms
  `Component`): basen för det som står som MEDLEM utan att vara en widget.
  `name` (speglat som `Widget::name` — writern emitterar namnraden på varje
  medlem), `setProp`/`setTextToken` (previewns väg, widgetkontraktet) och
  REGISTRET nycklat på C++-TYPNAMNET (`registerComponentType`,
  `makeComponent`, `componentPreviewForm`, `registeredComponentTypes`;
  makrot `GLAZE_REGISTER_COMPONENT(typ, Klass, previewform)`) med
  `PreviewForm {Inert, StandIn}` UTAN förval — previewn skapar bara Inert.
  Varken kopierbar eller flyttbar. Registret i `../../src/component.cpp`.
- **`timer.h`** — **`glaze::Timer : Component`** (FONSTER_OCH_TIMER, WinForms
  `Timer`): APPENS timer. `setInterval` (golvet `kMinIntervalMs` 10, förval
  100; ett byte medan den går startar om), `setSingleShot`, `start` (App lat,
  ett nytt körnings-id)/`stop`/`isRunning`, `Handler<TimerArgs, Timer>
  onTick` och `setProp` (`interval`, `singleShot`). Leveranskontraktet (EN
  tick per svans, en-gångs stoppad FÖRE handlern, återinträdet genom en
  modal, livstiden) står i headern; maskineriet i `../../src/timer.cpp`.

## Konventioner i dessa headers

- **Event-kontraktet** är hård regel 6 i rot-CLAUDE.md; `e` är icke-const
  `&`, och `onCommit`s regler står i [widgets/](widgets/CLAUDE.md).
- **Ny INBYGGD widget = ny header i `widgets/`** (kopiera `button.*`; formen
  — `switch (c.ui)`, mått i `Style`, färger i `Theme::Colors` — är hård regel
  3 i rot-CLAUDE.md): `GLAZE_REGISTER_WIDGET` och `setProp` i widgetens cpp,
  rad i paraplyet `glaze.h` och i `../../CMakeLists.txt`, README:s
  egenskapslista, palettrad i buildern vid behov. En EGEN widget utanför
  ramverket följer exakt samma mönster.
- **`contentSize()`-KONTRAKTET FÖR EN DOCKAD WIDGET:** den mäts mot
  förälderns OSKURNA context, och remstjockleken får inte bero på `c.area`
  längs SKÄRAXELN (Left/Right ⇒ `size.w` oberoende av `c.area.w`; Top/Bottom
  ⇒ `size.h` oberoende av `c.area.h`). Tväraxelberoende är legitimt och går
  genom `heightForWidth` (TEXTFALTEN), inte genom `c.area`; för
  `Fill` är kravet irrelevant. Bryts det blir syskonens layout beroende av
  frågeordningen och det finns ingen fixpunkt. `layout_gate` mäter
  kontraktet med två areor på skäraxeln + en anropsräknare med facit.
- **Tvärs-widget-geometri är inte ritning** och hör därför inte till
  `draw.h`: den blir en metod på ÄGAREN (`Panel::innerBounds`/`innerRect`
  är publika eftersom `PropertyGrid` ärver dem; `TextInput::innerBounds`
  är privat). Definiera den EN gång i logiska px och projicera med
  `Draw::project` när beteendet behöver fysiska px. Hit-test följer alltid
  widgetens `resolve()`-rect, aldrig den ritade formen.
- **Damage-kontraktet:** widgetens eget tillstånd → `Widget::invalidate()`
  (eller `invalidateRect`); en handler för en DISKRET händelse behöver aldrig
  invalidatera — efter den ritar AppWindow om hela fönstret. De LÖPANDE
  (dragets rörelser, ritytans `onTick`/`onCameraChange`/`onGesture`/hjul —
  PRESTANDA D1) ritar bara om sin widget: kontraktet vid `invalidate()` i widget.h.
- `AppWindow` är move-only (raderad copy); lånade barn × destruktion: rotens fallgrop.

## Vid ändring

- Nytt publikt beteende → deklarera här, implementera i motsvarande .cpp i
  [../../src/](../../src/) (`widget.cpp` för basen, `<widget>.cpp` per
  widget, `app_window.cpp` för fönstrets liv/event/damage, `app.cpp` för
  pumpen/registret, `gui_loader.cpp` för widget-registret).
