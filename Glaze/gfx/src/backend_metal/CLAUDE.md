# CLAUDE.md — Glaze/gfx/src/backend_metal

macOS-backenden: AppKit, Metal, Core Text och Image I/O (ObjC++). Del av `common_gfx` — se [../CLAUDE.md](../CLAUDE.md) för backend-neutrala
filer, event-översättningen och de gemensamma fallgroparna, och
[root /CLAUDE.md](../../../../CLAUDE.md) för lagerbilden och de hårda reglerna. Utbruten ur
`gfx/src/CLAUDE.md` 2026-10-10 (CANVAS_3D, Eriks beslut 2: varje källkodsmapp har sin
egen fil; backendarnas 3D-del hade inte rymts under 1000 rader).

## backend_metal (macOS — bara systemramverk; spegel av backend_d3d11)


ObjC++ (`.mm`, ARC); filerna speglar `d3d11_*` en-till-en — läs det
avsnittet, här BARA skillnaderna.
Allt som är märkt BLINT har sin rad i `doc/os/macos/TESTS.md`. **Sedan
2026-10-12 är hela backenden KOMPILERAD och grindad på macOS** (Apple clang,
0 varningar, `ctest` 54/55 — bara `pixel_gate`, TODO 67): "BLINT" betyder
från och med då att det som kräver händer och ögon inte är prövat, inte att
koden aldrig byggts. **Undantaget är SYSTEMFÄLTETS delar** (2026-10-12, efter
den körningen): `metal_tray.mm`, spårytan för `noActivate`-fönster,
aktiveringspolicyn ur `showInDock()` och loggraden för `showInTaskbar` är
ALDRIG kompilerade — och inte heller FÖNSTER OCH TIMER (FONSTER_OCH_TIMER,
2026-10-12): `metal_timer.mm`, `screens()`, `placement()` med
normalspårningen, startläget och de tre nya pekarna.

- **Pumpen** (`metal_platform.mm`): vi äger runloopen (`[NSApp run]` körs
  ALDRIG) — `waitEvent` blockerar i `nextEventMatchingMask:distantFuture`;
  `pushEvent` postar ALLTID ett väcknings-event, annars sover pumpen genom
  NSTimer/KVO ("timern tickar först när jag rör musen"). **`postWakeup`**
  (APP_API_2 R17, BLIND) återanvänder just den vägen — men kön är
  uttryckligen ENKELTRÅDAD, så en workertråd får INTE kalla `pushEvent`
  rakt av: den gör `dispatch_async` på MAIN-kön och kallar `pushEvent`
  där. Det bygger på att `nextEventMatchingMask:...inMode:` kör
  run-loopen och därmed dränerar main-kön även när pumpen blockerar;
  håller det inte är fixen skriven i `doc/os/macos/TESTS.md`. App-delegaten
  köar Quit + NSTerminateCancel; KVO på effectiveAppearance → tema-event.
  **Cmd-SLÄPPET** (REQ2_INMATNING B10, BLINT): `waitEvent` och `pollEvent` går
  genom `dispatchNSEvent` — ett `NSEventTypeKeyUp` med Command skickas direkt
  till `[fönster sendEvent:]` (`ev.window`, annars `keyWindow`), för
  `[NSApp sendEvent:]` sväljer det (Cmd+W hålls, W släpps först ⇒ inget
  `keyUp:`, och `isKeyHeld(W)` stod kvar); allt annat går till NSApp som förut.
  **AKTIVERINGSPOLICYN** (SYSTEMFÄLTET B9, BLINT): `init()` sätter Regular
  eller Accessory ur dockans begäran (`showInDock()`, lagrad av
  `glaze::setShowInDock` före det första fönstret — så dockikonen aldrig
  blinkar till; `LSUIElement` i Info.plist ingår inte) och kör
  `trayInit()` och sedan `appTimerInit()` SIST; `shutdown()` kör
  `trayShutdown()` FÖRST och sedan `appTimerShutdown()`. **`screens()`**
  (FONSTER_OCH_TIMER, BLINT) = `NSScreen.screens` med `frame`/`visibleFrame`
  genom `toPlacement` (top-left-flippen mot huvudskärmen — placeringsrymden
  är punkter), primär = `screens[0]` (skärmen med menyraden), skalan =
  användarskalan (punkter per logisk px).
  **`log`** = `os_log` (unified log) + en egen rad på stderr — ALDRIG
  `NSLog`: den skriver själv till stderr också, så varje rad kom två gånger
  (lang_gate räknar dem), och den läser `%s` som MacRoman (åäö blev
  mojibake).
  **`systemLocale()`** = `NSLocale.preferredLanguages.firstObject`, inte
  `currentLocale` (skälet i include-CLAUDE.md, `platform.h`) — BLINT.
  **APPMENYNS "Avsluta"** (post 32, BLIND): posten skapas i `init()` med
  bara processnamnet som titel, och **`setQuitMenuPrefix`** sätter
  prefix + processnamn — glaze trycker in det ÖVERSATTA prefixet efter
  init och vid varje språkbyte. **`openUrl`** = `[NSWorkspace openURL:]`
  via `NSURL URLWithString:` (ogiltig URL ⇒ nil ⇒ false) — BLINT.
  **`openFile`** (REQ8 11) delar hjälparen `workspaceOpen` men bygger NSURL:en
  med `fileURLWithPath:` (mellanslag och å/ä/ö kodas av OS:et) — BLINT.
  **`knownPlaces`** (post 18, BLINT) = `NSHomeDirectory()` + `NSFileManager
  URLsForDirectory:inDomains:NSUserDomainMask` (iCloud kan ha flyttat
  Skrivbord/Dokument — därför frågan och inte `~/Desktop`) + raderna i
  `/Volumes` (startvolymen är en länk till "/"); sökvägarna i
  `fileSystemRepresentation`. **`isHiddenFile`** = `lstat` + `st_flags &
  UF_HIDDEN` (`~/Library`).
- **GlazeView** (`metal_window.mm`): rå NSView med CAMetalLayer-backing
  (ALDRIG MTKView — egen draw-loop, bryter regel 1); `isFlipped=YES` →
  top-left-origo; punkter × `backingScaleFactor` = fysiska px. Custom =
  FullSizeContentView + dolda trafikljus + `performWindowDragWithEvent:`
  ur glaze:s karta (bara ramen ritas; resize-roller = OS:ets kanter);
  None = borderless (canBecomeKeyWindow-override); noActivate/toolWindow
  = NSPanel; skärmpositionen (`setPosition`/`clientToScreen`/`screenArea`)
  i PUNKTER med top-left-flip mot huvudskärmens höjd (mixed-DPI gör globala
  fysiska px odefinierade); timers = NSTimer (CommonModes); transparent
  klick-genomsläpp = `ignoresMouseEvents`-toggling + global mouseMoved-monitor.
  **SPÅRYTAN FÖR ETT FÖNSTER SOM ALDRIG BLIR KEY** (TODO 68, SYSTEMFÄLTET E2,
  BLINT): menyer, dropdown-listor och datum- och färgpopuper är `noActivate`,
  och med `ActiveInKeyWindow` fick de aldrig något `mouseMoved:` — raden
  under pekaren markerades inte. `updateTrackingAreas` väljer flaggorna ur
  **`MetalWindow::noActivate()`** (satt i init före vyn): sådana fönster får
  `EnteredAndExited | MouseMoved | ActiveAlways | InVisibleRect` UTAN
  `CursorUpdate` (stöds inte ihop med ActiveAlways, NSTrackingArea.h) och
  visar pilen ändå (`setCursor` sätter formen direkt medan pekaren står i
  vyn); andra fönster behåller sina flaggor. **`WindowDesc::showInTaskbar ==
  false`** loggas EN gång i `MetalWindow::init` — macOS har ingen knapp per
  fönster, dockan är appens.
  **NORMALRUTAN OCH STARTLÄGET** (FONSTER_OCH_TIMER, BLINT): `placement()`
  läser ramens övre vänstra hörn LEVANDE i normalläge och annars det
  spårade `normalTopLeft_`, storleken `logicalW_/H_` (som redan står still
  under zoom, skalresize och återställningsflanken — Codex-fynd 6, inga nya
  fält). `trackNormalFrame()` körs i init EFTER positioneringen (en zoomad
  start har en normalruta — Codex-fynd 5), i `windowDidMove:`, `onDidResize`,
  `windowShouldZoom:toFrame:` (före zoomen) och `windowWillMiniaturize:` —
  bara när fönstret varken är zoomat eller minimerat. Startläget tillämpas
  vid FÖRSTA visningen (`firstShown_`, init eller `setVisible` — sedan TODO
  80 normalt `setVisible`, för varje fönster utom popuper och offscreen
  skapas dolt och visas efter `onLoad`): `zoom:` respektive `miniaturize:`
  efter `orderFront`. `minimize`/`maximize`/`restore` före `firstShown_`
  byter bara `startState_` (B80-3, BLINT). **ZOOMAT är `zoomed(w)`, aldrig `isZoomed`
  rakt av**: AppKit svarar YES för varje fönster utan `Resizable` (ramlöst
  som fast), så hjälparen kräver masken (TODO 96, mätt 2026-10-14). **`raise()` på ett
  noActivate-fönster** är `orderFront:` utan `makeKeyAndOrderFront:` och
  `[NSApp activate]` (TODO 87, BLINT — det flytande verktygsfältet tar
  aldrig key). **`showInactive`**
  (DOCKNING_DRAG L6, BLINT): den FÖRSTA visningen blir `orderFront:` i stället
  för `makeKeyAndOrderFront:` (`showInactive_ && !firstShown_`, som
  `noActivate_` gör för alltid) — ett flytande dockningsfönster som lossnar
  mitt i ett drag tar inte key från fönstret som håller trycket.
  **`captureMouse()`** och **`hasMouseCapture()`** svarar false: AppKit
  levererar redan hela trycket (`mouseDragged:` också utanför vyn) till
  fönstret där det började, och `MouseCaptureLost` skickas inte.
  **PEKARNA** (`nsCursorFor`):
  `ClosedHand` = `closedHandCursor` (som `Move`), `Hidden` = en genomskinlig
  16×16-`NSCursor` skapad en gång, `Wait` = PILEN — macOS har ingen publik
  väntepekare (degraderingen står i `gfx/window.h`).
- **APPENS TIMER** (`metal_timer.mm`, FONSTER_OCH_TIMER 2026-10-12, **BLINT —
  aldrig kompilerat**; i ARC-listan): en `NSTimer timerWithTimeInterval:
  repeats:YES block:` per körnings-id, lagd i `NSRunLoopCommonModes` (som
  fönstrens timrar — annars står den still i en meny eller ett fönsterdrag),
  i en karta på namnrymdsnivå under ARC; blocket köar `EventType::Timer` med
  windowId 0 via `pushEvent` (som väcker pumpen). `appTimerInit()`/
  `appTimerShutdown()` från `gfx::init`/`shutdown` (Codex-fynd 2) styr
  `g_live` — ingenting rörs efter shutdown.
  **SKÄRMENS mm** (CANVAS_2D etapp 2, BLINT): `measureDisplay` mäter
  bufferten (skärmens punkter × `backingScaleFactor` — i "Mer utrymme"
  större än panelen, och formeln är rätt ändå) mot `CGDisplayScreenSize`;
  är måtten Apples 72-dpi-uppskattning ur `CGDisplayBounds` (ingen EDID,
  E10) blir svaret Assumed. Id ur `CGDisplayVendorNumber`/`ModelNumber`/
  `SerialNumber` i EDID-formen. Mäts vid init, `windowDidChangeScreen:` och
  ny backing; `NSDeviceResolution` duger inte (72 dpi, koordinatrymden).
  **ÄGARSKAPET (TODO-post 35, 2026-10-07, BLINT):** bara FLYKTIGA fönster
  (popup-kontraktets ANKRADE — `desc.popupAnchorWindow`; sedan DOCKNING_DRAG
  L7 gör noActivate ensamt inte längre ett barnfönster, för det flytande
  verktygsfältets permanenta noActivate-fönster ska INTE flytta med
  huvudfönstret) blir AppKit-BARNFÖNSTER — ett
  barnfönster flyttar med föräldern och är låst till dess Space, så ett
  vanligt ägt fönster försvann vid skärm-/Space-byte, och fasaden säger att
  ägarskap ALDRIG bär koordinater. Ett vanligt ägt fönster registreras i
  stället hos ägaren (`ownerWin_`/`owned_`, råa pekare åt båda håll —
  `shutdown` avregistrerar FÖRST), som emulerar Win32-ägarens två löften:
  `liftOwned` lägger de ägda över ägaren när den blir key (utan att göra
  dem key; bara de på den AKTIVA Spacen), och `hideOwnedForMinimize`/
  `restoreOwnedAfterMinimize` döljer dem kring minimeringen —
  `setVisible(false)` stryker ett fönster ur återställningslistan, så appens
  eget döljande vinner.
  **HJULET OCH STYRPLATTAN (TODO 53, BLINT):** precisa deltan = punkter ×
  backing ÷ fulla skalan ÷ `kPrecisePxPerRow`, fas ur `phase`/`momentumPhase`
  (Momentum); `scrollingDeltaX` är positiv ÅT VÄNSTER och negeras (fel före
  TODO 53). **SHIFT + MUSHJUL BYTS TILLBAKA** (REQ9_INMATNING B12, BLINT): AppKit
  lägger ett mushjuls Shift-rullning i `scrollingDeltaX` (deltaY 0); `scrollWheel:`
  levererar det som LODRÄTT (`wheelY = deltaX`, samma tecken som hjulet), så
  fasaden bär enhetens axel och glaze byter för det som rullar. Gränsen: ett äkta
  sidohjul med Shift nedtryckt blir lodrätt. `magnifyWithEvent:` (1 + magnification) och `rotateWithEvent:`
  (moturs ⇒ minus) blir EN Gesture-sekvens. **Modifierarna** (CANVAS_3D
  etapp 11, BLINT): hjulet och gesten bär `translateMods(ev.modifierFlags)`
  som musen (Cmd/Ctrl + tvåfingersdrag kretsar på ritytan), och
  **`flagsChanged:`** blir KeyDown/KeyUp med `Key::Unknown` och läget EFTER
  ändringen (`pushModifierKey`) — AppKit skickar ingen `keyDown:` för en
  modifierare, så en ensam Cmd gav förr inget event alls. Tryck eller släpp
  ur tangentens flagga i det nya läget (vänster och höger delar den — släppet
  av den ena medan den andra hålls blir ett KeyDown, masken stämmer ändå);
  Caps Lock och Fn ger inget event. **Tangentens text** (REQ3_LAYOUT_FONSTER
  B20-2, BLINT): `keyDown:` fångar `insertText:` i `captured_` medan
  `interpretKeyEvents` körs (`capturing_`), köar KeyDown med den sammanslagna
  texten (`Event::text`) och sedan ett TextInput per tecken; med Cmd ELLER
  Ctrl körs inte `interpretKeyEvents` (Ctrl-bindningarna gick ändå till den
  tomma `doCommandBySelector:`). Option skriver.
- **PROGRAMIKONEN** (`metal_window.mm`, PROGRAMIKON 2026-10-11, **BLINT**):
  macOS har ingen ikon per fönster — dockans är APPENS. `applyIcon` (i init
  och setIcon): har bundlen `CFBundleIconFile` rörs dockan ALDRIG
  (`iconApplied()` sant bara för `programFirst`); annars äger det FÖRSTA
  levande fönster som satt en ikon dockan (`g_iconOwner`, släpps i
  `shutdown`), ett annat fönsters ikon svarar false, och en tom lista
  återställer (`setApplicationIconImage:nil`) bara från ägaren. Ramarna blir
  `NSBitmapImageRep` (RAK alfa, `NSBitmapFormatAlphaNonpremultiplied`) med
  SAMMA punktstorlek i en `NSImage`, så AppKit väljer täthet.
  `programName()` (metal_platform.mm) = sista komponenten i
  `NSBundle.mainBundle.executableURL`. `isInProgramFile(adress)`
  (DELAT_BIBLIOTEK R3, BLINT) = `dladdr`:s `dli_fbase` jämfört med
  `_dyld_get_image_header(0)` — dyld:s bild 0 är programfilen.
- **SYSTEMFÄLTET** (`metal_tray.mm`, SYSTEMFÄLTET 2026-10-12, **BLINT — aldrig
  kompilerat**; i ARC-listan): EN `NSStatusItem` PER ID
  (`NSSquareStatusItemLength`) med en `NSImage` på 18 × 18 pt och
  representationer för 18 och 36 px ur `pickIconImage` (rak alfa, som
  `applyIcon`); `programFirst` + `CFBundleIconFile` ⇒ en KOPIA av
  `NSImageNameApplicationIcon` (`imageNamed:` delar instansen med hela
  appen — storleken hade krympt dockans ikon). Mallbilden sätts med
  `[bild setTemplate:]`: `template` är ett C++-nyckelord. Tooltipen
  (`button.toolTip`) sätts bara när den ÄNDRATS — animationen byter bild
  flera gånger i sekunden, och en ny sträng startar om visningen.
  **`GlazeTrayTarget`** tar knappens action med `sendActionOn:` vänster- och
  högersläpp: höger eller Ctrl = ContextMenu, ett JÄMNT `clickCount` =
  DoubleClick (klick, dubbelklick, klick …), en action utan mushändelse
  (VoiceOver, tangentbordet) = Click; vid Click och DoubleClick körs
  `[NSApp activate]` FÖRE eventet köas (en Accessory-app aktiveras inte av
  ett klick i menyraden), och ankaret är pekaren i placeringsrymden. En
  spåryta på knappen (`EnteredAndExited | ActiveAlways | InVisibleRect`)
  ger Enter/Leave. `trayIconRect` = `button.window.frame` och skärmens
  `visibleFrame`, vända mot huvudskärmens höjd; Tick = en `NSTimer` i
  CommonModes. **`trayInit()`/`trayShutdown()`** (ur init/shutdown, `g_live`)
  gör fasaden EXAKT säker före init och efter shutdown — `NSApp` lever efter
  shutdown, så den duger inte som vakt. `applyShowInDock` byter policyn när
  `NSApp` finns (begäran lagras i ../platform.cpp). `g_entries` står i namnrymden (rivs efter
  `glaze::App`, se ../CLAUDE.md).
- **UTSKRIFTEN** (`metal_print.mm`, REQ8 10, **BLINT — aldrig kompilerat**;
  i ARC-listan): `NSPrintPanel runModalWithPrintInfo:` (APPMODAL, inte ett
  ark — arket är asynkront) på en kopia av `sharedPrintInfo` med marginalerna
  0; `NSPrintInfo` är `PrintSettings::native` (`MacPrintInfo`). Jobbet SAMLAR
  sidornas `CGImage` och skickar dem i `finish` genom en `NSPrintOperation` med
  vyn `GlazePrintView` (`knowsPageRange:`, en pappersstor sida per bild,
  bilden på `imageablePageBounds`), utan panel och med systemets
  förloppspanel; `NSPrintAllPages` sätts, för intervallet är redan gjort.
  Ägaren läses inte (appmodalt).
- **EN AUTORELEASE-POOL PER FRAME** (`MetalWindow::renderFrame`, 2026-10-14):
  commandbuffern (`[queue_ commandBuffer]`), encoderna och drawablen är
  autoreleasade, och commandbuffern håller varje resurs den rört vid liv.
  Pumpen är vår egen och ingen NSApp-loop tömmer något, så utan poolen levde
  varje frames commandbuffer, med staging-buffertarna och de ersatta
  chunkarnas buffertar, till processens slut. Det blev 8 GB GPU-minne per
  varv i `app_perf --varv`, osynligt för
  `leaks`. **Ett nytt anrop som skapar en commandbuffer UTANFÖR en frame**
  (`beginBlit` utan frame, `readTarget`, `captureRetained`,
  `beginOffscreen`) bär samma risk när det görs ofta. Sedan REQ7_RITYTAN
  finns ingen ny staging-buffert per uppladdning (stagingringen nedan), och
  varje blit-väg och uppladdningspassets commandbuffer och encoder skapas i
  en LOKAL pool — ett offscreen-block har ingen frame-pool runt sig.
- **Renderern** (`metal_renderer.mm`): topologin är drawPrimitives-
  argument; map-DISCARD → ring (3 delade buffertar + semafor); clear =
  encoder-bryt; scissor KLAMPAS mot målet (valideringskrasch annars);
  `nextDrawable` kan ge nil → skippa present; ett DOLT fönster hämtar ingen drawable alls (`setPresenting`, satt av `MetalWindow` i init och `setVisible` — `nextDrawable` är vsync-styrd också utanför skärmen, och ett dolt fönster ska inte vänta); blend av/på = TVÅ immutabla
  PSO:er (sedan CANVAS_3D etapp 0 varianter ur `psoFor` — se 3D-delen
  nedan); alfa-spegeln (bara transparent) = blit + waitUntilCompleted.
  **Programbytena** (`RenderStats::programSwitches`) räknas med
  `countProgram` mot det program den SENASTE RITNINGEN tog i bruk
  (`drawn_`), inte mot `pipeline_`: en ny encoder (blit, mål, clear,
  compositen) binder UI-varianten utan att något ritats med den, så
  `pipeline_` blir Ui — och UI:ts ritning efter compositen räknades inte
  alls (scene_pixel_gate A7, 2026-10-12). Samma tal som D3D11 och GL, som
  behåller bindningen över målbyten; framens början nollar tyst.
  **De RUNDADE klippmaskerna** (RUNDAT_BARNKLIPP, BLINT SKRIVEN) binds med
  `setFragmentBytes:…atIndex:0` — och **ÅTERBINDS i `startEncoder`** av
  EXAKT samma skäl som `applyScissor()` återappliceras där: encoder-state
  försvinner vid clear/target-byte, så en mask som bara sattes i
  `applyClip` hade tappats vid framens första `clear`. Därför är
  `setClipMasks` (äger värdet) och `bindClipMasks` (skickar det) TVÅ
  funktioner. **MÅLARENS KLIPP** (REQ8 1, BLINT SKRIVET): `struct SceneClip`
  och `sceneClip(p, sc)` i `kMslClipSrc`, på fragmentbuffert **4** i
  `linePs`/`fillPs`/`glyphPs`/`imagePs` (utanför de egna programmens 2 och
  3) — `discard_fragment()` utanför, så inget djup skrivs. `bindSceneClip`
  (metal_scene.mm) binder det när encodern saknar en bindning
  (`sceneClipBound_`, nollad i `startEncoder`) eller klippet ändrats.
  **Readback** (GLAZE_RENDER): `captureRetained` — egen engångs-
  commandbuffer med blit → delad buffert + waitUntilCompleted (köordningen
  garanterar capture EFTER framens commit), BGRA→RGBA-swizzle; bakom
  `MetalWindow::captureFrame`.
- **Text/bild**: kCGImageAlphaOnly-raster (gråskala) → samma R8-atlas;
  rasteriseringen är EN funktion (`rasterize`) som atlasen och
  `rasterGlyph` delar (etapp 5, BLINT), och stilen väljs ur familjen med
  `CTFontCreateCopyWithSymbolicTraits` (närmaste stil först; nullptr =
  familjen saknar den) och läses tillbaka med `CTFontGetSymbolicTraits`.
  **GLYFRESERVEN** (CANVAS_TEXT_OCH_BILD A5, BLINT — `doc/os/macos/TESTS.md`):
  en kodpunkt fonten saknar slås upp med Core Texts kedja,
  `CTFontCreateForString(font_, kodpunkten)`. Svaret UNDERKÄNNS när det är
  NULL, fontens egen font (`CFEqual`) eller `LastResort` (PostScript-namnet —
  Apples font med en ersättningsruta per block, alltså "ingen font har den"),
  ställs om till fontens storlek med `CTFontCreateCopyWithAttributes` när
  storleken avviker, och måste ha glyfen (`glyphIndexIn`, surrogatpar över
  U+FFFF). Reserverna är ÄGDA `CTFontRef` i `reserves_` (samma font två
  gånger blir samma post med `CFEqual`, `CFRelease` i `unload`). Regeln
  (egen → reserv → U+FFFD → .notdef, styrtecken aldrig ur reserven) är
  `resolveGlyph` i `../font_fallback.h`, och `resolve(cp)` cachar
  `{GlyphRef, stegbredd}` per kodpunkt — mätningen, atlasen och
  `rasterGlyph` läser den (`advanceIn(font, gi)` och `rasterize(font, gi,
  …)` tar facen). En färgfont (Apple Color Emoji) som kedjan svarar med
  ritas som sin alfa — kontexten är `kCGImageAlphaOnly`.
  CG-buffertar är TOP-DOWN (verifierat, ingen radflipp). `decodeImage`
  (etapp 3, trådsäker, som `loadImage` går genom) läser i första hand
  Image I/O:s EGNA avkodade pixlar (`readRawPixels`, 2026-10-12): grå eller
  RGB, 8 eller 16 bitar, rak alfa eller ingen — PNG:ens vanliga fall, byte
  för byte som filen och utan färgomvandling, som WIC. Bara en annan form
  ritas i en kontext, och CG ritar BARA premultiplicerat → av-premultiplicera
  på CPU (annars mörka fransar). Den vägen var den ENDA fram till dess och
  tappade ett till två steg i varje halvgenomskinlig pixel (icon_gate A1/A3).
  **Bilder ur pixlar (etapp 3):** `createImage` fyller en NY textur
  med `replaceRegion` (inget kodat anrop refererar den än), och `updateImage`
  går UPPLADDNINGARNAS väg (nedan): aldrig ritad ⇒ `replaceRegion` med
  anroparens rader som de är, annars en blit ur staging med raderna på hela
  fyrbytes — i uppladdningspasset eller i ett encoder-brott. **Samplern** är
  batch-tillstånd som i D3D11 (`samplerFor`, `flush` sätter batchens per
  ritning). **Image-programmet** bor i metal_image.mm (`ImageU` på index 1
  i båda stegen, bild + sampler på index 0, w orört; sedan
  CANVAS_TEXT_OCH_BILD B1 den PROJEKTIVA vikten `q [[attribute(2)]]` —
  `MTLVertexFormatFloat` vid `offsetof(ImageVertex, q)`, buffert 0, stride
  20 — och `uvq = (u·q, v·q, q)` med v speglat före multiplikationen,
  samplat som `uvq.xy / uvq.z`; BLINT. Vertexbeskrivningen sitter i
  programmets basbeskrivning, som `psoFor` gör varje variant ur — också
  MSAA-målens. Sedan REQ8 3 FÄRGJUSTERINGEN, BLINT: `SceneU`/`ImageU` har
  `adjust` (96 byte, `sceneImageAdjust`) och biblioteket är `kMslClipSrc` +
  `kMslAdjustSrc` (metal_program.mm — samma `glaze_colorAdjust` som de egna
  programmens förspel) + `kImageMslSrc`). **Render-målen**
  (metal_target.mm, etapp 6, BLINT): BGRA8Unorm (PSO:ernas format),
  nollade med ett Clear-pass vid skapandet, och `bindTarget` är ett
  encoder-brott till målet — eller `frameTarget_` (retained-RT:n från
  beginFrame, drawablen efter composite) — med Load. `startEncoder` och
  `bindUiPipeline` skickar MÅLETS storlek (`targetW_/targetH_`) som
  viewport-uniform, och scenens program läser samma. `gfx::encodePng`
  (metal_image.mm): CGImageDestination/"public.png" — `CGImageCreate` med
  kCGImageAlphaLast tar rak alfa direkt (ingen premultiplikations-omväg).
- **UPPLADDNINGARNA** (REQ7_RITYTAN, 2026-10-14 — metal_backend.h
  `uploadPathFor`, metal_scene.mm). EN regel ur resursens STÄMPEL: `usedIn`
  på `MetalGpuBuffer`/`MetalImage` är framen (eller offscreen-blocket) som
  senast REFERERADE den i ett kommando, 0 = aldrig; `frameSerial_` stegas i
  beginFrame och beginOffscreen, och stämpeln sätts FÖRST i `drawMesh` (före
  förgreningen — delarna `vertices`/`indices`/`instances` och bilderna
  `image`/`labels`/`textures[]`) och i `drawImage`, också för ett anrop som
  sedan avvisas (konservativt). **Direct** (aldrig använd): CPU:n skriver —
  `memcpy` i `contents` (+ `didModifyRange:` på Managed), `replaceRegion` i en
  bild; `createBuffer` med data likadant, med resten av fyrbytet nollat.
  **Pass** (använd i en TIDIGARE frame eller ett tidigare block): en blit ur
  stagingringen i UPPLADDNINGSPASSET — en egen commandbuffer (`uploadCmd_`),
  skapad LATT vid framens första Pass och köad direkt (`enqueue`); framens
  `cmd_` köas först vid sin commit, så passet ligger FÖRE framen, och Metals
  spårning låter bliten vänta på förra framens läsningar (på GPU:n). EN
  blit-encoder åt gången; en ny bara när en blit ÖVERLAPPAR en tidigare till
  samma mål i passet (`uploadWrites_`, en buffert är ett intervall med höjden
  1) — ordningen inom ett pass är odokumenterad. **Break** (använd i SAMMA
  frame): encoder-brottet `beginBlit`/`endBlit` (flush, encodern avslutas,
  bliten i framens commandbuffer, omstart på SAMMA mål med Load) med staging
  ur ringen — så det som redan ritats behåller det gamla innehållet, som i
  D3D11/GL — räknat i `RenderStats::uploadBreaks`. **Own** (utanför en frame,
  redan använd): en egen commandbuffer och en egen staging-buffert (kön kör
  den före nästa frames). STAGINGRINGEN (`staging_[kRingSlots]`,
  `stagingAlloc`) är den transienta ringens mönster: en delad buffert per
  slot, nollad i `beginSceneFrame` efter semaforen, dubblad från 1 MiB (den
  utväxta i `retired` till slottens nästa varv), och en enskild uppladdning
  över 8 MiB i en egen buffert. SEMAFOREN släpps först när BÅDE passet och
  framen körts (`commitFrame`, metal_renderer.mm — en delad räknare: köordningen
  är dokumenterad, inte ordningen de blir klara i). LAGRINGEN: statiska
  buffertar i Shared när `device.hasUnifiedMemory` (Apple silicon), annars
  Managed — men Managed-grenen kompileras bara för en minsta macOS-version
  under 27 (`bufferStorage`/`wroteDirect`, metal_scene.mm): macOS 27 finns
  bara för Apple silicon, och SDK:n avråder från Managed där. Managed är
  BLINT (rad i doc/os/macos/TESTS.md). Grindar: scene_pixel_gate A8–A11,
  B3, B12–B13, canvas_perf U, canvas_perf3d S/Ä.
- **Scenen** (`metal_scene.mm`, CANVAS_2D etapp 1): statiska buffertar i
  DELAD lagring, skrivna på uppladdningarnas väg (ovan). macOS kräver
  blitlängder i hela fyrbytes (fasadens `updateOk` kräver det därför
  överallt). Den transienta ringen
  är en DELAD buffert per ring-SLOT och slag (UI-ringens trippelbuffring;
  `beginSceneFrame` efter semaforen) och kräver en öppen frame.
  Line-programmet är ett eget bibliotek ur **`kMslClipSrc`** (delad med
  UI:ts källa: `#include`, `sdRoundRect`, `ClipMasks`, `clipCoverage`) +
  Line-källan, `[[center_no_perspective]]` för skärmavståndet, `Char4` för
  sida + slag. Uniformerna går på index 1 i BÅDA stegen — där UI:ts
  vertexsteg har sin viewport, så `bindUiPipeline` (i `flush`) sätter
  tillbaka PSO:n OCH viewporten; `startEncoder` binder alltid UI-pipelinen
  (`pipeline_ = Ui`). Klippmaskerna (fragmentbuffert 0) följer encodern och
  gäller alla program. **Etapp 4–5 (BLINT):** Line, Fill och Glyph tar
  **`PaintU`** (160 byte: matrisen, `params`, `paint0`/`paint1`, `stroke`,
  `dash[2]` — färgens MSL **`kMslPaintSrc`** bor i metal_fill.mm), Image
  behåller sin `SceneU`. De skärmvända hörnens djup är
  **`glaze_standingZ(m, c, off)`** i **`kMslStandSrc`** (metal_program.mm,
  REQ9 7 — BLINT): förspelets tredje del och efter färgen i Line och Glyph.
  **metal_fill.mm** och **metal_glyph.mm** är speglar av d3d11_fill.cpp
  och d3d11_glyph.cpp (REQ3_RITYTAN: storleken klampas till `u.paint1[0]`/
  `[3]` = golvet och taket, BLINT; `[[center_no_perspective]]` för remsans ton,
  `[[flat]]` för glyfens färger och flaggor, `MTLVertexFormatUInt` för
  flaggorna); rampen/atlassidan och `samplerLinear_` binds på index 0 per
  rit-anrop (`bindPaintRamp`). **Etapp 9 (BLINT):** synligheten är
  VERTEXSTEGETS textur 0 (`setVertexTexture`, `read` utan sampler — vit
  när ingen bild finns, för funktionen deklarerar texturen), indexet
  attribut 9 (`UInt`), basen i `params.w`. **T2 (BLINT):** D3D11:s
  skarvar, ändar och streck i MSL (`dist` attribut 6) och Glyph-programmets
  `kImage`-gren.

## 3D-delen (CANVAS_3D etapp 0 — djup, MSAA och renderläget, BLINT)

Kontrakten står i [../../include/gfx/CLAUDE.md](../../include/gfx/CLAUDE.md);
D3D11 och GL grindas av G9 (`tests/scene3d_pixel_gate.cpp`), Metal har sina
rader i `doc/os/macos/TESTS.md` ("Ritytans 3D — Metal BLINT"). Kompilerat och
grindat av G9 på macOS sedan 2026-10-12; det som kräver ögon står i raderna.

- **PSO-VARIANTERNA ÄR NÖDVÄNDIGA, inte en optimering.** En Metal-PSO bakar
  provtagningen (`rasterSampleCount`), djupformatet och blandningen, och en
  PSO som inte matchar passets attachments är ett valideringsfel. Varje
  program — UI, Line, Fill, Glyph, Image — lämnar därför sin DESKRIPTOR
  (funktionerna och vertexdeskriptorn) i `progDesc_` (`registerProgram`, som
  bygger grundvarianten för att pröva den), och `psoFor(program, blandning)`
  bygger och cachar (`psoCache_`, nyckel program | blandning | prov | djup;
  en misslyckad variant cachas som nil och loggas en gång) varianten för
  encoderns attachments (`curSamples_`, `curDepthTex_`) — själva bygget är
  `buildPso` sedan etapp 1, delat med de egna programmen. ALLA bindningar går
  genom `setPso` — de gamla `pso_`/`psoNoBlend_`/`linePso_` … finns inte mer;
  transparent-compositen är varianten `Blend::Opaque`. `configureBlend` är
  samma faktorer som D3D11 och GL (DepthOnly = `MTLColorWriteMaskNone`).
- **DJUPLÄGET OCH ENCODERNS TILLSTÅND.** `depthStateFor(test, skriv)` cachar
  `MTLDepthStencilState` (Off = Always utan skrivning). `applyMeshState` i
  varje program sätter djupläget (bara med ett djupattachment — PSO:n saknar
  annars djupformatet), `setCullMode` och `setDepthBias:slopeScale:clamp:`;
  `applyUiState` sätter tillbaka av/ingen/0 i `bindUiPipeline`. Encoderns
  tillstånd följer inte med ett encoder-brott, så `startEncoder` binder
  UI-varianten, `setFrontFacingWinding:MTLWindingCounterClockwise` (moturs
  för ögat är framsidan, som D3D11:s `FrontCounterClockwise` — ett ANTAGANDE
  om att Metal avgör vindningen i samma y-uppåt-NDC) och UI:ts läge.
- **MÅLET** (`metal_target.mm`): bilden (BGRA8, ett prov, RenderTarget |
  ShaderRead — också upplösningens mål), med MSAA en 2DMultisample-textur
  att rita i (`colorTexture()`), och djupet Depth32Float med samma
  provtagning. `startEncoder` fäster djupet (`curDepthTex_`, satt av
  bindTarget) — Clear med färgen, annars Load — och BÅDA lagras (Store) vid
  varje encoder-brott: en blit, ett nytt mål mitt i en frame, en clear eller
  ett nästlat mål får inte tappa det ritade. **Upplösningen** är ett EGET,
  tomt pass när målet LÄMNAS (`resolveTarget`, efter målets sista encoder och
  före nästa måls): färgen Load + `StoreAndMultisampleResolve` om målet
  ligger kvar i stacken (`targetInStack` — man kommer tillbaka), annars
  `MultisampleResolve` och djupet DontCare. **`supportedSamples`** =
  `supportsTextureSampleCount:` (Apple silicon 4). **`readTarget`**: bara
  UTANFÖR en frame (`cmd_` nil — annars false och en logg); en egen command
  buffer med blit + `waitUntilCompleted`, BGRA → RGBA, `unpremultiplyRgba`.
- **`beginOffscreen`** öppnar en egen command buffer i `cmd_` — då fungerar
  encoder-dansen, blittarna och den transienta ringen som i en frame — utan
  eget mål (`frameTarget_` nil). Ringslotten väntas inte in: blocket skriver
  efter förra framens del av slotten, och `endOffscreen` committar och
  väntar in arbetet innan slotten kan återanvändas.
- **Integrationen med ett annat grafik-API (`gfx/native.h`) stöds inte här:**
  `nativeDevice` svarar `api == None`, `importRenderTarget` ett ogiltigt mål
  med feltext, `setExternalFramePacing` gör ingenting och `insertCompletion`
  ger `nullptr` med feltext. `GpuAdapter::localAdapterId` är tomt.
- **GRAFIKKORTEN OCH GPU-PREFERENSEN — `metal_gpu.mm`** (REQ9_GRAFIKKORTET,
  BLINT; `preferredMetalDevice` och `logDevice` flyttade ur
  metal_renderer.mm): GlazeView:s `makeBackingLayer` sätter lagrets enhet med
  `preferredMetalDevice`, och renderaren tar lagrets. Kortet `gpuAdapter()`
  vinner när det finns och inte är `isHeadless`; annars Default =
  `MTLCreateSystemDefaultDevice()` och HighPerformance/LowPower = den första
  ur `MTLCopyAllDevices` som inte är (resp. är) `isLowPower` och inte
  `isHeadless`, loggat. `gfx::gpuAdapters` går `MTLCopyAllDevices` — id
  `metal:<namn>` (`@location.locationNumber` vid dubbletter), slaget ur
  `isRemovable`/`hasUnifiedMemory`, minnet `recommendedMaxWorkingSetSize` —
  och anropar **ALDRIG `MTLCreateSystemDefaultDevice`** (det slår om hela
  systemet till den kraftfulla GPU:n): `isDefault` är Apples regel,
  UPPSKATTAD. `deviceName_` = `device.name`, `adapterId_` =
  `metalAdapterId(device)` (`registryID` mot listan), och `logDevice` skriver
  "Metal: ritar med X [id] (preferensen Y, kortet Z)" när enheten är en annan
  än förra renderarens (PRESTANDA A1). `metal_gpu.mm` står i BÅDE källistan
  och ARC-listan i CMakeLists.
- **MÄTPUNKTERNA (PRESTANDA A2, BLINT — aldrig kompilerade):** framens
  löpnummer är basens `frameSerial_` (Metals egen är borttagen; stegad i
  beginFrame och beginOffscreen som förut). **`compileMsl`** är den ENDA
  vägen till `newLibraryWithSource` — UI:t, scenens, bildens, ytornas,
  textens och de egna programmen — med tiden i `compileNs`, och
  `buildPso` mäts likadant. `presentNs` = ring-semaforens väntan i
  `beginFrame` + `nextDrawable` i `composite`. GPU-TIDEN: `beginFrame` sparar
  framens nummer i `gpuSerial_` när tidtagningen är på, och `commitFrame`
  ger commandbuffern en completion handler som lämnar `GPUEndTime −
  GPUStartTime` i basens brevlåda — i Metals tråd, genom en EGEN
  `shared_ptr<GpuTimeBox>` (svaret kan komma efter att renderaren rivits).
  Det oprövade står i doc/os/macos/TESTS.md.
- **BARA PNG** (`metal_image.mm`): `hasPngSignature` före Image I/O, och
  `loadImage(path)` läser filen (`NSData dataWithContentsOfFile:`) och går
  genom decodeImage i stället för `CGImageSourceCreateWithURL`.

## Egna program (CANVAS_3D etapp 1 — `metal_program.mm`, BLINT)

Spegel av backend_d3d11:s avsnitt; kontrakten vid `program.h` i
[../../include/gfx/CLAUDE.md](../../include/gfx/CLAUDE.md). D3D11 och GL är
grindade av G9 del C; raden för Metal står i `doc/os/macos/TESTS.md`.
Kompilerat och grindat (G9 del C, `mesh_program_gate`) på macOS sedan 2026-10-12.

- **`createProgram`**: `newLibraryWithSource` med källan FÖRSPELET (`struct
  GlazeFrame` och `glaze_toClip(c, glaze)` — MSL har inga globala block, så
  funktionen tar blocket som argument; 256 byte med `rayCenter` sist) + `kMslAdjustSrc` (`glaze_colorAdjust`,
  REQ8 3) + `#line 1 "<namn>"` + appens MSL, och
  definitionerna som `MTLCompileOptions.preprocessorMacros` (NSString-värden;
  tomt värde = definierat utan värde). Saknas källan: "saknar MSL-källa
  (macOS)"; saknas en funktion: "saknar MSL-funktionen 'vsMain'".
  REFLEKTIONEN är vertexfunktionens `vertexAttributes` (de aktiva: plats och
  `MTLDataType`, heltalstyperna = `integerInputs`). GlazeUser-blockets
  storlek och texturplatserna prövas INTE här (D3D11 och GL gör det) — Metal
  reflekterar argumenten först i en PSO, och valideringslagret säger till om
  en för liten buffert. Lösa globaler finns inte i MSL (ett program-scope
  `constant` är en konstant), så den kontrollen behövs inte.
- **PSO-VARIANTERNA**: en Metal-PSO bakar vertexdeskriptorn, så varianten
  följer (layoutens nyckel, blandning, provtagning, djup) och cachas I
  PROGRAMMET (`mutable`), också nil (loggad en gång). Bygget är renderarens
  **`buildPso`** — utbruten ur `psoVariant`, så de inbyggda programmen och
  de egna lägger på attachments och blandning på exakt samma sätt.
  Vertexdeskriptorn: attributen på sina platser med `bufferIndex` = strömmen,
  layout 0 per vertex och layout 1 per instans (`stepRate` 1); nil för en tom
  layout (helskärmspasset ur `[[vertex_id]]`).
- **RITNINGEN** (`drawCustomMesh`): GlazeFrame (`target` = det bundna målets
  storlek) och GlazeUser med `setVertexBytes`/`setFragmentBytes` på
  buffer(2) och buffer(3) — under 4 KB, därav taket på `userBytes` —,
  texturerna och deras samplers i båda stegen, `applyMeshState`, strömmarna
  på buffer(0) och buffer(1) MED OFFSET och
  `drawIndexedPrimitives`/`drawPrimitives` med `instanceCount` när den är
  > 0. Index 1 i vertexsteget är UI:ts viewport — `bindUiPipeline` sätter
  tillbaka den som efter de andra programmen; fragmentbuffert 0 (maskerna)
  rörs inte.

## Line3 och Glyph3 (CANVAS_3D etapp 3 — GLAZE_POS3, BLINT)

- **SAMMA KÄLLA, EGNA BIBLIOTEK.** Line- och Glyph-källan kompileras en gång
  till med `#define GLAZE_POS3 1` före källan (init-slingorna i
  metal_scene.mm och metal_glyph.mm): egna funktioner, en egen
  vertexdeskriptor (punkterna/ankaret som `Float3` på `LineVertex3`s
  respektive `GlyphVertex3`s offset — `glyphDescriptor<V>` för båda
  glyfformaten) och egna PSO-varianter genom `registerProgram` med
  `Pipeline::Line3/Glyph3` (index i `progDesc_`, som är en per INBYGGT
  program — `Custom` står utanför). `drawMesh` väljer program och PSO ur
  `MeshDraw::program`.
- **VERTEXFUNKTIONEN** är HLSL:ns POS3-gren rad för rad (bredden ur den
  lokala skalan — MSL:s `float4x4` indexeras per KOLUMN —, båda ändarnas
  halva bredd, den EXAKTA förlängningen, `world` falskt och kastade
  fragment utanför bandet).
- **KLIPPYTAN** (båda linjegrenarna, `lineTrimPlane`/`lineTrimSegment`/
  `lineKeepInside`): segmentets ändar innanför när- och fjärrplanet och en
  skyddsram, och förlängningen innanför klippdjupet — HLSL:ns skäl (llvmpipe
  interpolerade klippta band fel; Metal antas klippa rätt, men källan är en
  och samma); ett segment helt utanför ett plan och en rund bit bakom
  närplanet kastas (0.1.1). Allt här är OPRÖVAT — raderna i doc/os/macos/TESTS.md.
- **LIGGANDE TEXT (CANVAS_3D etapp 9, `kGroundAligned`, BLINT)** —
  `glyphGround` i metal_glyph.mm är d3d11_glyph.cpp:s rad för rad, med
  blocket `PaintU` som argument (MSL har inga globala uniformer): hörnet i
  världsplanet, "nedåt" vänd så att ramen aldrig speglas, skalan vid
  ankaret vinkelrätt mot blicken, i Glyph och Glyph3. Mesh-programmets
  DRAPED-variant (src/canvas/programs3d.cpp) har sin MSL-gren där. OPRÖVAT
  — raderna i doc/os/macos/TESTS.md.

## Fallgropar (uppladdningarna, REQ7_RITYTAN)

- **STÄMPELNS TÄCKNING ÄR DET SOM BÄR Direct.** Bara `drawMesh` och
  `drawImage` binder en `MetalGpuBuffer` eller `MetalImage` i ett kommando,
  och båda stämplar. En ny väg som binder en sådan resurs (en blit som
  LÄSER den, ett nytt program som binder en bild utanför `drawMesh`) utan
  att stämpla den gör den direkta CPU-skrivningen osäker: CPU:n kan skriva i
  något en frame på väg läser. Ingen grind fäller det — `captureFrame` väntar
  in GPU:n, så kapplöpningen uppstår aldrig i scene_pixel_gate.
- **UPPLADDNINGSPASSET MÅSTE ALLTID SKICKAS.** Det är köat (`enqueue`) från
  sin första blit, och en köad commandbuffer som aldrig committas låser kön
  — nästa frame, `captureRetained` och `readTarget` väntar för evigt. Varje
  väg som avslutar en frame skickar det: `commitFrame` (present och
  shutdownRenderer), endOffscreen, och försvaret överst i beginFrame (loggat
  en gång). En ny väg som avslutar en frame eller ett block ska gå genom
  `finishUploadPass`; ett glömt ställe syns som timeout i scene_pixel_gate och
  canvas3d_gate (offscreen-vägen).
