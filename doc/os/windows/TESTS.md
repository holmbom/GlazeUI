# Windows — kvar att grinda (backend_d3d11)

Se [../README.md](../README.md) för hur listan används: ta BORT raden när den
är grön, skriv en 🐛 i `/TODO.md` om den i stället avslöjar en bugg.

Bygg: MSYS2 UCRT64.
```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/apps/demo/demo.exe
```
OBS: apparna är WIN32-subsystem — `gfx::log` hamnar i debug-utgången, inte i
konsolen (kör under debugger eller DebugView för att se loggarna).

**Testpasset 2026-10-07** (post 36, på en Windows 11-burk med två skärmar,
BÅDA i 125 %) tog bort allt som gick att mäta utan en människa: bygget (0
varningar, ctest grönt), glaze-renders D3D11-väg och `--scale`, popup-
placeringen (flip uppåt/nedåt, clamp-vakten mot en 5000 px bred lista),
logiska fönstermått, användarskalan i drift, zoom + skalbyte + återställ,
min/max och fast fönster, `setTopmost`, `Window::transparent()`, det rundade
barnklippet, nivå 1-komponenterna och `systemLocale()`. Det som står kvar
kräver MUSEN, OLIKA skärmskalningar eller VS Code. Referensbilden ligger i
`screenshots/start_window_100.png`. Linux-bilderna med samma namn är
omrenderade 2026-10-08 i WSL (svenskt språk, samma 880×730 — läget följer
systemet, så Windows-maskinen ger mörkt och WSLg ljust); macOS-bilden är från
den ÄLDRE demon (800×520) och behöver renderas om på en Mac innan den jämförs.

## Kvar — kräver en människa

- [ ] **Fokus-kontraktet, med MUSEN** (`gfx/window.h` vid
  `popupAnchorWindow`). En popup och dess ankarfönster är EN fokusgrupp:
  öppning/stängning får ALDRIG ge ägaren `WindowFocusLost`/`Gained`. Windows
  uppfyller det via `WM_MOUSEACTIVATE` → `MA_NOACTIVATE` — och den vägen tar
  bara ett riktigt musklick. Placeringen är mätt 2026-10-07 och tangenterna
  (pil upp/ner, Enter committar, Escape stänger) grindas av `dropdown_gate`.
  *Test:* klicka upp dropdownen i **Om-dialogen** (modal) och på startsidan.
  *Rätt utfall:* listan syns och STANNAR KVAR (bryts kontraktet stängs den i
  samma ögonblick som den öppnas — buggen som drabbade Wayland 2026-08-09);
  klick på en rad committar, klick utanför stänger, klick på dropdownen igen
  togglar.

- [ ] **Två skärmar med OLIKA skalning** (`d3d11_window.cpp` — `WM_DPICHANGED`
  + `AppWindow::reloadFont()` vid `WindowDisplayScaleChanged`). Den här
  burkens två skärmar står båda i 125 %, så sätt först den ena i 100 % eller
  150 % (Inställningar → System → Skärm → Skala). Samma omladdning via
  användarskalan i drift är verifierad (skarp text vid uiScale 1.875).
  *Test:* dra demofönstret mellan skärmarna, och sedan fram och tillbaka fem
  gånger. *Rätt utfall:* texten är skarp i den nya skalan DIREKT (inte
  uppskalad och suddig), layouten behåller sin logiska storlek, och fönstret
  hamnar på EXAKT samma fysiska storlek varje gång det återvänder till samma
  skärm — ingen krypning (`pendingScaleResize_`-vakten).

- [ ] **Användarskalan, med MUSEN** (L12/US). Storlekarna är mätta 2026-10-07
  utan människa: rundresan 1.25 → 0.8 → 1.0 → 1.25 → 1.0 landar på exakt
  samma pixelstorlek, en manuell resize skalas om med den NYA storleken, och
  maximera → skalbyte → återställ ger den logiska storleken i nya skalan med
  EN `WindowResized`. Kvar är det som bara syns och känns.
  *Test:* demons skala-dropdown 0.8 → 1.25 → 1.5 → 1.0. *Rätt utfall:*
  namnlisten går att dra, kapselns fack och dropdownlistan träffar där de
  ritas i varje läge, och ingen felritad frame (gammal pixelstorlek med ny
  layout) blinkar förbi under själva bytet.

- [ ] **VS Code-extensionen: render-verifiering.** win32-x64-VSIX:en byggs och
  installeras (`install-ext.bat`); glaze-renders D3D11-väg den hänger på är
  verifierad 2026-10-07 (rätt PNG, och inget fönster blinkar — 0 synliga
  fönster på 12 888 pollningar).
  *Test:* öppna en `.glazeui` i VS Code → renderad bild; spara → om-render;
  Öppna-i-GlazeBuilder-knappen.

- [ ] **`gfx::openUrl`** (`Glaze/gfx/src/backend_d3d11/d3d11_platform.cpp`,
  `ShellExecuteW` + `shell32`). Bygget länkar; själva anropet öppnar
  standardwebbläsaren och prövades därför inte automatiskt.
  *Test:* starta demo, klicka länken "GlazeUI på GitHub" i panelen "Nivå
  1-komponenterna". *Rätt utfall:* statusraden byter text (appens onClick
  körs FÖRE OS-handlingen) OCH webbläsaren öppnar
  `https://github.com/holmbom/GlazeUI`. En Link med tom `url` öppnar INGET.

- [ ] **`gfx::openFile`** (`d3d11_platform.cpp`, REQ8 11). Delar `shellOpen`
  med `openUrl` (`ShellExecuteW`, verbet "open"); bygget länkar, men anropet
  öppnar ett program och prövades därför inte automatiskt.
  *Test:* anropa `gfx::openFile` (t.ex. ur en knapp i demon) med en PDF eller
  bild vars sökväg har mellanslag och å/ä/ö (`C:\Users\<du>\Skrivbord\Årsrapport
  2026.pdf`). *Rätt utfall:* standardprogrammet öppnar filen, returvärdet är
  true. Med en sökväg som inte finns är det false och loggen säger
  `openFile: ShellExecuteW misslyckades (kod 2)`; en tom sträng ger false.

- [ ] **Kapselklustret, hover och tryck** (KAPSEL_CHROME —
  `drawCapsule()` i `Glaze/widgets/src/widgets/caption_button.cpp`).
  Vilotillståndet är sett i glaze-render i skala 1.0/1.25/1.5/2.0: ett
  piller med rundade ändar till HÖGER, titeln centrerad i hela baren, och
  ett fast fönster tappar maximera-facket.
  *Test:* hovra och tryck på facken i demons namnlist. *Rätt utfall:* hovern
  tänder ETT fack med 2 px luft in från konturen, tryck fyller facket med
  hela accentytan och glyfen blir `onAccent`, stängfacket blir rött.

- [ ] **`gfx::Key` fick skiljetecken och numpad — BLINT skriven**
  (`Glaze/gfx/include/gfx/events.h` = enumet + KONTRAKTET,
  `Glaze/gfx/src/backend_d3d11/d3d11_window.cpp` = `translateKey` +
  nya `symbolFromVk`). Kontraktet är **level-0-symbolen i den AKTIVA
  layouten** — den symbol tangenten ger UTAN modifierare — och Windows
  svarar på den frågan med `MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR)`.
  `VK_OEM_*`-konstanterna används MEDVETET inte: de heter efter
  US-layouten och `+` sitter någon helt annanstans på svensk.
  *Test:* kör demon med SVENSK layout och tryck `+`, `-`, `,`, `.`,
  `'`, `<`. Rätt utfall: varje tangent ger sin egen `Key`-post
  (`Plus`, `Minus`, `Comma`, `Period`, `Apostrophe`). Byt till
  amerikansk layout: `+`-tangentens plats ger nu `Minus` och `=`-tangenten
  `Equals` — det är RÄTT enligt kontraktet, och `Ctrl+Shift+=` ska ge
  `Key::Equals` + `kModShift` (inte `Key::Plus`).
  *Numpad, med ett FYSISKT tangentbord* — översättningen av syntetiska
  `WM_KEYDOWN` med och utan extended-flaggan är grindad sedan 2026-10-07
  (`tests/numpad_gate.cpp`); det som återstår är att ett riktigt
  tangentbord sätter flaggan så som Microsoft dokumenterar. Med **NumLock
  PÅ** ska numpad-1 ge `KP_N1` och numpad-punkt `KP_Decimal`; med **NumLock
  AV** `KP_End` respektive `KP_Delete`, medan de dedikerade tangenterna
  fortfarande ger `End`/`Delete` — och en ListBox i `demo --gallery` ska
  ändå bläddra med numpaden, för `AppWindow` viker den till tvillingen.
  Numpadens Enter ska fortsätta aktivera en fokuserad knapp (den fälls
  MEDVETET ihop med `Return`).
  *Fel att leta efter:* en dead key (svenskt `¨`) får INTE ge en
  `Key`-post — `MapVirtualKeyW` sätter bit 31 för dem och koden ska
  returnera `Unknown`. Tecknet ska ändå komma som `WM_CHAR`/TextInput när
  det komponerats.

- [ ] **Muspekaren, med MUSEN** (post 15 — `setCursor`/`WM_SETCURSOR` i
  `Glaze/gfx/src/backend_d3d11/d3d11_window.cpp`). splitter_gate mäter den
  BEGÄRDA formen (`cursor()`); vad Windows VISAR kräver en riktig pekare
  över ett synligt fönster.
  *Test:* `demo --gallery`, grupp 15 och 17. *Rätt utfall:* ↔ över ett
  stående skikt och ↕ över ett liggande; ↔ STANNAR medan man drar ut över
  ett textfält och blir pil efter släppet; I-balk över textfält, hand över
  länken; pil över den inaktiverade delaren. Ut ur fönstret mitt i ett
  hover över skiktet → skrivbordets vanliga pekare (formen får inte läcka
  till ett annat fönster); fönsterkanten visar fortfarande resize-pekaren.

## Sidopaketet Networking — HTTPS genom WinHTTP mot en riktig server (TODO 30, 2026-10-07)

**Vad som ändrats:** nytt sidopaket `Networking/` (lib `glaze_net`). HTTP(S)
går genom WinHTTP i ASYNKRONT läge (`Networking/src/http_winhttp.cpp` — det
är det enda sättet att kunna avbryta). `tests/http_gate` kör http mot en egen
loopback-server och är grön på D3D11-maskinen; HTTPS går inte att grinda utan
nät. Kontrollerat manuellt 2026-10-07 mot publika servrar — raden är provet mot den
WMS som faktiskt ska användas.

**Vad man gör:** ett litet program mot `glaze_net`
(`add_executable(wmsprov wmsprov.cpp)` +
`target_link_libraries(wmsprov PRIVATE glaze_net)`):

```cpp
#include <cstdio>
#include <glaze_net/net.h>
int main() {
    auto r = glaze::net::get("https://<WMS>?SERVICE=WMS&REQUEST=GetCapabilities", {});
    if (r) std::printf("%d %zu byte %s\n", r->status, r->body.size(),
                       r->header("Content-Type").c_str());
    else   std::printf("FEL: %s\n", r.error().c_str());
}
```

Gärna också bakom en proxy (företagsnät, WPAD) om en sådan finns.

**Rätt utfall:** status 200, en XML-kropp på några kB, Content-Type
`application/vnd.ogc.wms_xml` eller `text/xml`. Bakom proxy samma utfall
(`WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY` följer systemets inställningar).
`https://expired.badssl.com/` ska ge FEL med "utanför giltighetstiden" i
texten (`Code::Tls`) — aldrig ett svar.

## Ritytan med riktig mus, riktig linjal och två skärmar (TODO 27, CANVAS_2D, 2026-10-10)

Allt nedan provas i **`demo --gallery` → fliken "Rityta"** (CANVAS_2D etapp
12): kartan uppe till vänster, grafen med en inaktiverad kopia uppe till
höger och linjalprovet över hela bredden under. Grindat utan människa:
`display_gate` (EDID-vägen — Measured 188,6 dpi, id `BOE-0B8B-00000001` på
referensmaskinen), `middle_gate` (mittenvägen och overlay-vandringen),
`canvas_gate` (gesterna och träffprovet med SYNTETISKA händelser) och
`scene_pixel_gate` (pixlarna). Fliken är renderad offscreen och sedd i alla
tre UI, ljust och mörkt, på svenska och finska — det som står kvar kräver en
hand, en linjal eller en andra skärm.

- [ ] **Linjalprovet: 100 mm vid 100 %, 125 % och 150 %** (`gallery_canvas.cpp`
  `linjalPaint`; tätheten ur `display()`, mm OBEROENDE av OS-skalan — T9).
  *Vad man gör:* håll en linjal mot linjen i kalibreringsgruppen, utan att
  ha kalibrerat, vid var och en av de tre skalorna (Inställningar → Skärm),
  och vid startfönstrets Skala 0.8 och 1.25. *Rätt utfall:* linjen mäter
  100 mm (± 1 mm) i ALLA fem — informationsraden säger "mm: uppmätt" och
  samma px/mm hela tiden. Dra sedan i linjen eller stega med - och + tills
  den mäter exakt 100 mm och tryck **Spara**: raden säger "kalibrerad",
  kartans skala byter tal ("1:4000" → t.ex. "1:4060" — zoomen står kvar) och
  dess vägar och etiketter i mm byter storlek direkt. **Återställ** ger
  tillbaka "uppmätt".
- [ ] **Två skärmar** (`d3d11_window.cpp`, `WM_MOVE`/`WM_DISPLAYCHANGE`/
  `WM_DPICHANGED` → `WindowDisplayChanged` → `AppWindow::measureDisplay`).
  *Vad man gör:* dra galleriet till den andra skärmen och tillbaka, och ändra
  upplösningen medan appen kör (WM_DISPLAYCHANGE). *Rätt utfall:*
  linjalens rad visar den andra skärmens id och täthet så fort fönstret
  bytt skärm, linjen mäter 100 mm där också, och kartans skaltal följer;
  tillbaka ger den första skärmens värden. En sparad kalibrering följer SIN
  skärms id (kalibrera den ena — den andra står kvar "uppmätt").
- [ ] **Skalstocken mäter där den står** (`gallery_canvas.cpp` `kartaOverlay`
  + `Canvas::unitsPerPixelAt`). *Vad man gör:* mät stockens band nere till
  vänster i kartan med en linjal — platt, vriden med Ctrl+drag vågrätt och
  lutad med Ctrl+drag lodrätt. *Rätt utfall:* bandets längd i mm × N i
  "1:N" bredvid ÷ 1000 = etikettens meter (platt 1:4000: 100 m = 25 mm);
  vriden står allt still; lutad krymper N och bandet växer (stocken står
  nära kameran), och räkningen stämmer fortfarande — mittens skala hade
  inte gjort det. Stocken byter till ett annat runt tal när den skulle bli
  kortare än ~20 eller längre än ~50 mm.
- [ ] **Mushjulet zoomar mot pekaren** (`pushWheel` i `d3d11_window.cpp`
  översätter `WM_MOUSEWHEEL`:s SKÄRMkoordinater till klientytan; kroken
  `Widget::mouseWheel` får punkten). *Rätt utfall:* det som står under
  pekaren står kvar där vid varje hjulsteg i kartan och grafen, också nära
  ytans kanter och på en andra skärm med en annan skala. (Styrplattan går
  inte den här vägen längre — se styrplattans avsnitt nedan.)
- [ ] **Mittendrag och Ctrl+vänsterdrag i kartan.** *Rätt utfall:*
  mittendraget panorerar med punkten under pekaren fast (också utanför
  fönstret, tills knappen släpps), Ctrl+drag vrider vågrätt och lutar
  lodrätt (gatunamnen följer vägarna), och en vridning som släpps nära en
  vinkel i vridsnäppets steg glider dit (vallistan uppe till höger i kartan,
  förval 90° — se styrplattans avsnitt).
- [ ] **Capturen släpps först när SISTA knappen släpps** (`d3d11_window.cpp`,
  `WM_*BUTTONUP` läser `MK_*` i wParam). *Vad man gör:* håll mittenknappen
  över kartan, tryck och släpp VÄNSTER, dra ut ur fönstret och släpp mitten
  där. *Rätt utfall:* panoreringen slutar vid mittensläppet. Med den gamla
  raden släpptes capturen vid vänstersläppet, mittensläppet utanför
  fönstret kom aldrig, och vanliga rörelser fortsatte att panorera.
- [ ] **`Crosshair` och `Move` visas** (`systemCursor`: `IDC_CROSS` 32515,
  `IDC_SIZEALL` 32646). *Rätt utfall:* hårkorset över kartan och grafen
  (inte över linjalen, som saknar gester) och fyrpilen under en panorering —
  Windows egna systempekare, i rätt storlek vid 125 %/150 %.
- [ ] **Trögheten känns rätt** (`camera_math.cpp`: τ 260 ms, högst 1,2 s,
  farten ur de sista 100 ms). *Rätt utfall:* ett kast glider ut och stannar
  mjukt; ett drag som STANNAR innan släppet glider inte; CPU:n går till noll
  när kartan står still (Aktivitetshanteraren).
- [ ] **Flikens övriga beteende med riktig mus.** *Rätt utfall:* ett KLICK
  på en form i kartan skriver den översta träffen i statusraden (på
  Storgatans körbana "Kartan: Storgatan (Vägar #3) +2" — fyllningen, kanten
  under och plattan) medan ett drag panorerar utan att skriva; i
  grafen ritar Skift+drag ett gummiband som zoomar till rutan vid släppet,
  dubbelklick visar allt igen och koordinaterna i hörnet försvinner när
  pekaren lämnar ytan; den inaktiverade grafen rör sig inte; zooma ut i
  kartan så tonar de mindre viktiga etiketterna bort.

## Styrplattan på ritytan — med en riktig PRECISIONSPLATTA (TODO 53, 2026-10-10)

**Erik-testat 2026-10-10 med riktig precisionsplatta — "100 % klockrent":**
panoreringen, nypningen, vridningen, gestens val (panorering ELLER zoom ELLER
vridning), ikappningen utan hopp och vridsnäppet — och sedan trögheten (CPU:n
till noll), "Nyp för att zooma", mushjulet och listorna; de raderna är
borttagna. Kvar står reserven, som kräver en platta utan precisionsstöd.
Koden: `backend_d3d11/d3d11_touchpad.cpp` — Direct Manipulation (en falsk
viewport per fönster, Chromiums sekvens) för tvåfingersdraget och nypningen,
råa HID-kontakter (`RegisterRawInputDevices` + `HidP_*`) för vridningen, och
den backend-neutrala spåraren `src/touchpad_rotation.cpp`; ritytans del i
`Canvas::mouseWheel`/`Canvas::gesture` och målvalet i
`AppWindow::routeGesture`. `trackpad_gate` grindar ritytan, routingen och
spåraren med SYNTETISKA händelser — det som står kvar är att Windows faktiskt
levererar dem. Allt provas i **`demo --gallery` → "Rityta"**, kartan uppe
till vänster (alla gester på), grafen till höger (panorering och zoom, ingen
vridning).

- [ ] **Reserven** — utan precisionsplatta (en äldre platta, fjärrskrivbord):
  tvåfingersdraget kommer som vanligt hjul och ZOOMAR kartan (dagens väg),
  inget kraschar. Misslyckas Direct Manipulation står "Direct Manipulation
  kunde inte startas (…) — styrplattan går som mushjul" EN gång i loggen.

## Ritytans 3D-interaktion med riktig mus, styrplatta och tangentbord (CANVAS_3D etapp 11, 2026-10-11)

Grindat utan människa: `canvas3d_gate` (G12, 49 fall: orbiten, panoreringen
i målplanet, dollyn, tumlingen, spannen, verktygen, mittenknappen till appen,
`onGesture`, tickningen genom pumpen och textfältet på ritytan — SYNTETISKA
händelser genom fönstrets ingång) och `trackpad_gate` del O (styrplattan i
orbitläge). Kvar är att Windows levererar händelserna och att det känns rätt
i handen. Provas i **`demo --gallery` → "3D"** och i
**`./build/tests/canvas3d_gate.exe --hand`** — ett synligt fönster med en
kulle, fem lådor, två textfält på ritytan och verktygsknappen
"Förstaperson"; rutan uppe till höger räknar tickarna.

- [ ] **Orbiten med precisionsplattan** (Direct Manipulation som i
  styrplattans avsnitt ovan). *Vad man gör:* dra två fingrar, nyp, vrid,
  och dra två fingrar med Ctrl nere — också DIREKT efter Ctrl-trycket, utan
  att röra pekaren först (sedan fyndet i etapp 11 bär hjulet masken själv:
  Direct Manipulation går genom WndProc:ens `pushWheel`/`pushGesture` i
  d3d11_window.cpp, oprövat med riktig platta). *Rätt utfall:* tvåfingersdraget
  panorerar med punkten under fingrarna fast, nypningen går närmare och
  längre bort (mot målet), vridningen svänger kameran kring målet, och
  Ctrl+tvåfingersdrag kretsar som ett vänsterdrag. Efter ett snabbt svep
  glider rörelsen ut med Windows EGEN tröghet (fasen Momentum, som i
  kartan) — ritytans egen tröghet startar aldrig i orbiten.
- [ ] **Förstapersonen med WASD** (`--hand`, `Hand::gå` i
  `tests/canvas3d_gate.cpp`; ritytans `onTick` och `AppWindow::isKeyHeld`).
  *Vad man gör:* klicka i ytan, slå på "Förstaperson" (knappen tar inte
  fokus), håll W och gå mot den vita lådan på kullen, släpp; håll W+D; håll
  W och Alt+Tab till ett annat fönster och tillbaka; dra med musen medan W
  hålls. *Rätt utfall:* ögat glider framåt i jämn gångfart och följer
  kullens yta (`heightAt`) — upprepningen gör det inte snabbare; vid släppet
  står det still DIREKT, tickräknaren står still och CPU:n går till noll;
  W+D går snett; Alt+Tab stoppar gången (fokusförlusten tömmer de hållna
  tangenterna) och den börjar inte igen förrän W trycks på nytt; draget
  styr blicken under gången. Med knappen av flyttar WASD målet och Q/E
  upp och ner.
- [ ] **Textfälten på ritytan** (`--hand`). *Vad man gör:* klicka i fältet
  uppe till vänster och skriv "wasd", åäö och en död tangent (´ + e); klicka
  i ytan igen och tryck W; titta på fältet längst ner till höger och klicka
  på den del av det som borde ha stuckit ut. *Rätt utfall:* fältet får
  fokusringen och ett blinkande caret, bokstäverna hamnar i fältet och
  kameran står still, é blir ett tecken; CPU:n ligger nära noll medan caret
  blinkar (3D-scenen ritas inte om per blink). Klicket i ytan ger den
  fokus och W går igen. Det nedre fältet är avskuret av ytans kant och
  rundade hörn, och klicket utanför ytan når det inte.

## Programikonen och programmenyn (TODO 65, PROGRAMIKON, 2026-10-11)

- [ ] **Ikonen i namnlisten, aktivitetsfältet och Alt+Tab** (`d3d11_window.cpp`
  `setIcon` — `CreateIconIndirect` + `WM_SETICON` small/big;
  `glaze/widgets/app_icon.h`; `src/window_icon.cpp`). Grindat headless
  (icon_gate A–G), aldrig SETT.
  *Test:* starta demon. Startfönstret visar G-ikonen STOR (namnlist +
  menyrad, menyraden börjar bredvid), galleriet (Arkiv → Alla widgets) den
  LILLA kugghjulsikonen, "Nytt fönster" INGEN ikon och titeln där den var.
  Aktivitetsfältet och Alt+Tab visar G-ikonen för demon (exe:ns resurs vinner
  i förvalsläget, `programFirst`) och kugghjulet för galleriet. Klick på den
  stora ikonen öppnar programmenyn (Nytt fönster · Alla widgets · Om · Avsluta)
  under ikonen; ytan är INTE dragyta (ingen flytt av fönstret vid drag).
  Plattan syns vid hover och medan menyn är öppen.
  *Rätt utfall:* skarpa kanter i 100 % OCH 125 %/150 % (byt skalning i drift —
  `WM_DPICHANGED` bygger om ikonerna); ingen svart eller vit kant runt
  G:ets halvgenomskinliga pixlar (rak mot förmultiplicerad alfa i
  `CreateIconIndirect` — planens risk).
- [ ] **Programfilens ikon** (`cmake/GlazeAppIcon.cmake` + windres,
  `Glaze/assets/icon/glazeui.ico`). *Test:* öppna `build/apps/demo/` i
  Utforskaren — `demo.exe` och `GlazeBuilder.exe` visar G-ikonen i alla
  vylägen (små ikoner → extra stora). *Rätt utfall:* ingen generisk exe-ikon.

## Textfälten — AltGr, lösenordet och grupp 29 (TODO 76, TEXTFALTEN, 2026-10-12)

- [ ] **AltGr med polsk layout** (`textfield::editShortcut` i
  `Glaze/widgets/src/text_field.cpp` — fältens genvägar är Ctrl UTAN Alt;
  Windows levererar AltGr som Ctrl+Alt i `currentMods()`, och tecknet kommer
  som WM_CHAR efter tangenten). Logiken är grindad (textfield_gate H5) med
  masken matad direkt; den RIKTIGA översättningen WM_KEYDOWN → WM_CHAR är
  aldrig provad. *Vad man gör:* lägg till "Polska (programmerare)" som
  tangentbordslayout, öppna `demo --gallery` → "Text och listor" och skriv
  AltGr+A, AltGr+C, AltGr+X och AltGr+Z i ett textfält (grupp 3) och i
  anteckningsfältet (grupp 24) — en gång utan markering och en gång med en
  markering. *Rätt utfall:* ą ć ź ż skrivs (över markeringen när en finns);
  ingenting markerar allt, kopierar, klipper ut eller ångrar, och Ctrl+Z
  ångrar sedan som vanligt.
- [ ] **Grupp 29 i galleriet — höjden är RÄKNAD, inte renderad** (210 px,
  räknad i Slates radhöjd i `widget_gallery.cpp`). *Vad man gör:* `demo
  --gallery` → "Text och listor", byt UI (Glaze/Slate/Minimalistic) och
  språk (sv/en/nb/fi) i startfönstret. *Rätt utfall:* inget klipps i
  gruppen och sidan rullar inte (gruppen slutar i höjd med grupp 3);
  lösenordsfältet visar prickar, "Visa lösenordet" visar texten; dra skiktet
  i delaren — texten i första rutan bryts om, blir högre/lägre och raden
  under den följer med; fotraden ryms (två rader).

## Fönster och timer — splashen, fönsterlabbet och pekarna (TODO 78, FONSTER_OCH_TIMER, 2026-10-12)

- [ ] **Splashen** (`apps/demo/src/splash_window.*` — `setChrome(gfx::Chrome::None)`,
  `setStartPosition(CenterScreen)`, `setShowInTaskbar(false)` och en
  `glaze::Timer` i `.glazeui`). *Vad man gör:* `demo --splash`. *Rätt
  utfall:* ett ramlöst fönster mitt i primärskärmens ARBETSYTA, ingen knapp i
  aktivitetsfältet, förloppsstapeln rör sig, och efter 1,5 s stängs det och
  startfönstret öppnas.
- [ ] **Fönsterlabbet — geometrin** (`apps/demo/src/window_lab.cpp`,
  `AppWindow::geometry()`, D3D11 `trackNormalPosition`/`placement()`). *Vad
  man gör:* `demo --window-lab`; flytta fönstret, maximera, minimera och
  återställ med knapparna och med namnlisten. *Rätt utfall:* geometriraden
  följer flytten; maximerat visar den NORMALrutan (platsen och storleken
  före maximeringen) och läget "Maximerat"; minimerat "Minimerat".
- [ ] **Spara och återställ — också på en annan skärm** (`setLocation` +
  `setClientSize` + `setWindowState` före `show()`, `window_placement.h`).
  *Vad man gör:* "Spara geometrin", sedan "Öppna nytt med den sparade";
  upprepa med labbet på en sekundärskärm med ANNAN skala (100 % mot 150 %),
  och spara på en skärm som sedan kopplas ur innan nytt öppnas. *Rätt
  utfall:* det nya fönstret öppnas exakt över det gamla (samma plats och
  storlek), också på sekundärskärmen; sparat på en urkopplad skärm hamnar
  det inne i den närmaste skärmens arbetsyta.
- [ ] **Pekarna vid 100/150/200 %** (`d3d11_window.cpp` `cursorHandle` — den
  stängda handen är VÅR bild ur `Glaze/gfx/src/cursor_bitmaps.h`, skalad med
  `SM_CXCURSOR / 16`). *Vad man gör:* i labbet: håll musen över de fyra
  rutorna; kryssa "Väntepekare över hela fönstret"; panorera ritytan i
  galleriet (`demo --gallery` → "Rityta"). *Rätt utfall:* hand, stängd hand
  (skarp, svart kontur och vit fyllning, rätt storlek i varje skala),
  ingen pekare alls i "Dold", väntepekaren (animerad) i "Vänta" och över
  hela fönstret medan kryssrutan är ikryssad; I-balken i textfälten
  oförändrad; panoreringen visar den stängda handen och vridningen (Ctrl +
  drag) fyrpilen.
- [ ] **Maximerad start från en genväg** (`D3dWindow::showFirst` — STARTUPINFO-
  heuristiken ersätter bara `SW_SHOW`/`SW_SHOWNORMAL`; första anropet
  verifieras och upprepas). *Vad man gör:* bygg en app med
  `setWindowState(gfx::WindowState::Maximized)` före `run()` (eller ändra
  labbets konstruktor tillfälligt), gör en genväg i Utforskaren med "Kör:
  Normalt fönster" och starta. *Rätt utfall:* fönstret öppnas maximerat.
- [ ] **En timer under fönsterdrag** (`d3d11_timer.cpp` — `SetTimer` på ett
  meddelandefönster; WM_TIMER kommer i Windows modala flytt-loop). *Vad man
  gör:* i labbet, dra fönstret i namnlisten och håll det stilla i några
  sekunder utan att släppa. *Rätt utfall:* geometriraden fortsätter att
  uppdateras medan fönstret dras (WM_TIMER når pumpen i flytt-loopen), eller
  — om ritningen står still under draget — kommer EN uppdatering efter
  släppet, aldrig en skur.

## Träd, listor och bilder ur pixlar i galleriet (TODO 75, TRAD_LISTOR_BILDER, 2026-10-12)

Byggt utan att köras (nattpasset körde inga program). Galleriets nya mått är
RÄKNADE i Slates radhöjd, inte renderade.

- [ ] **Grupp 13 med flerval, kryss och färg** (`apps/demo/src/widget_gallery.cpp`).
  *Vad man gör:* `demo --gallery` → "Tabeller och träd": Ctrl+klicka, Shift+
  klicka, Ctrl+A, klicka i rutorna, dra tre valda noder in i "Bilder",
  dubbelklicka en nod, och byt UI/tema/språk i startfönstret. *Rätt utfall:*
  statusraden säger "Valda noder: N" / "Kryssad: …" / "Trädet — öppnade: …";
  Brev.txt i orange tills den väljs; noderna flyttar i trädordning och står
  kvar valda; FOTEN (tre rader på finska) ryms i gruppens 268 px i alla tre
  UI och klipps inte; trädets text med kryss och ikon klipps vid 140 px
  (godtagbart, trädet har ingen vågrät rullning).
- [ ] **Grupp 14 och 18: radfärgen** (`setRowColor`). *Vad man gör:* sortera
  listvyn på namn och storlek, välj Budget.xlsx, Ctrl+klicka flera rader och
  tryck på en vald rad utan att dra. *Rätt utfall:* Budget.xlsx röd i varje
  sortering och i markeringens färg när den är vald; trycket väljer bara
  raden när knappen släpps. Datagridens FTP-rad röd utom i den aktuella
  cellen.
- [ ] **Grupp 5 och 9: bilder ur pixlar** (`ringar()`). *Vad man gör:* 100 %
  och 200 % skala. *Rätt utfall:* tre bilder i grupp 5 — den tredje blå och
  gula ringar, skarp på 200 % — med texten "Picture: normal, av, ur pixlar"
  radbruten bredvid utan att krocka med listerna under; grupp 9:s ringikon i
  båda remsorna, tonad i den inaktiverade, och remsan ryms i gruppens bredd.
- [ ] **Trädet: flytt av flera noder där en ligger inuti en annan**
  (`TreeView::moveNodes`, överlappsbeskärningen — Codex kodgranskning,
  ingen grind). *Vad man gör:* i en egen testapp: flerval på, välj A och B,
  och låt `onDragStart` flytta B in under A (`moveNode`); dra till C och
  släpp. *Rätt utfall:* A hamnar i C med B kvar inuti A — ingen krasch,
  inga förlorade noder.

## GlazeUI som delat bibliotek — `build-shared` (TODO 73, DELAT_BIBLIOTEK, 2026-10-13)

Bygg: `cmake -S . -B build-shared -G Ninja -DCMAKE_BUILD_TYPE=Debug
-DGLAZE_SHARED=ON -DGLAZE_TESTS=ON && cmake --build build-shared` (0
varningar 2026-10-13). Sviten (`ctest --test-dir build-shared`, med
`shared_gate`) och sabotagen hör till commit-passet; raderna här är det bara
en människa ser.

- [ ] **Galleriet ur DLL:erna** (`build-shared/apps/demo/demo.exe --gallery`).
  *Rätt utfall:* ser ut och beter sig som det statiska bygget —
  flikarna, ritytan, 3D-fliken och menyerna.
- [ ] **Processluten med en ikon i systemfältet** (R3 — App rivs i programmets
  `exit()` av ankaret `Glaze/widgets/src/app_anchor.cpp`, inte i libglaze:s
  `DLL_PROCESS_DETACH`). *Vad man gör:* `build-shared/apps/demo/demo.exe
  --tray`, sedan "Avsluta" i systemfältsmenyn. *Rätt utfall:* ikonen
  försvinner direkt (ingen spökikon som står kvar tills musen förs över
  fältet), och `tasklist | grep demo` visar ingen kvarlevande process.
- [ ] **GlazeBuilder ur `build-shared`** öppnar `apps/demo/src/start_window.h`
  och ritar canvasen och paletten som det statiska bygget.
- [ ] **Importerna:** `objdump -p build-shared/apps/demo/demo.exe | grep "DLL
  Name"` visar `libglaze.dll` (och verktygskedjans `libstdc++-6.dll`/
  `libgcc_s_seh-1.dll`), och `libglaze.dll` ligger bredvid exe:n.

## Levande drag och flytande dockning — riktig mus, blandad DPI och ±1 px (TODO 77/81, DOCKNING E3 + DOCKNING_DRAG E4/E5, 2026-10-13)

Koden: `Glaze/widgets/src/dock/dock_float.cpp` (de flytande fönstren,
aktiveringen) och `dock_host.cpp` (det levande draget: `beginCarry`/
`carryMove`/`carryEnd`/`finishCarry`, och `syncFloats`). dock_gate mäter
lossningen, platshållaren, greppet och fönstrets hörn ±1 px med syntetiska
händelser (LV1/LV2/LV4/LV9/LV12, LV6/LV7) och fall B med POSTADE meddelanden
till ett dolt fönster (LV3, LV11, TOM, FT10a/FT10b). Den mäter inte en riktig
mus, OS:ets capture-routning, två skärmar eller blandad skalning — det gör
raderna här.

- [ ] **Levande drag med riktig mus.** *Vad man gör:* `demo --dock`; dra
  Utforskarens rubrik en bit (inne i fönstret), sedan ut ur fönstret, och
  släpp; dra det flytande fönstrets rubrik in över Sök och släpp; dra en flik
  och släpp mitt i dokumentet. *Rätt utfall:* panelen LOSSNAR vid tröskeln —
  dess fönster följer pekaren med handen kvar där den tog (en flik: mitt på
  rubriken) och skymmer det som står under, opakt; över Sök och vid kanterna
  visar värden platshållaren (exakt den yta panelen får), och släppet där
  dockar panelen; släppt utanför fönstret eller mitt i dokumentet flyter den
  kvar där den står; statusraden visar `f=utforskare,…` respektive
  `p=utforskare,…` EN gång per drag. Och en SNABB knyck (tryck, dra ut och
  släpp på ett ögonblick): fönstret öppnas där släppet var — också delvis
  utanför skärmen — och lyfts (Codex-granskningen av E5, fynd 4).
- [ ] **Huvudfönstret behåller fokus.** *Vad man gör:* `demo --dock`, klicka i
  Sök-fältet och skriv; dra sedan Utforskaren ut ur fönstret och släpp.
  *Rätt utfall:* lossningen stjäl inte fokus mitt i draget (draget avbryts
  inte); efter släppet lyfts panelens fönster (som i VS). Vid dold start med
  flytande paneler (Bevakning) behåller huvudfönstret aktiveringen.
- [ ] **Verktygsfälten.** *Vad man gör:* dra Standards grepp ut ur bandet och
  släpp mitt i dokumentet; dra dess rubrik över ett band och släpp; dra
  Felsöks rubrik (flytande från start) SNABBT fram och tillbaka och släpp
  utanför fönstret; klicka på en knapp i ett flytande fält medan Sök-fältet
  har fokus. *Rätt utfall:* fältet lossnar och följer pekaren, platshållaren
  visar bandet/platsen, släppet dockar det; Felsöks fönster följer också ett
  snabbt drag (värden tar trycket — fall B), och ett klick i det flytande
  fältet lämnar caret i Sök-fältet (fönstret aktiveras aldrig).
- [ ] **Blandad DPI mitt i draget.** *Vad man gör:* två skärmar med olika
  skalning (t.ex. 100 % och 150 %); dra en panel ut till den ANDRA skärmen och
  tillbaka utan att släppa. *Rätt utfall:* fönstret byter storlek efter OS:ets
  förslag när det korsar skärmgränsen och greppet hamnar under pekaren igen
  vid nästa rörelse; fönstret har panelens LOGISKA storlek (fler fysiska px på
  den högre skalningen). Släpp också precis vid skärmgränsen, så att släppets
  flytt byter skalning: draget avslutas som ett släpp (ETT `onChange`, ingen
  krasch — fynd 2).
- [ ] **En sparad layout efter att skärmen kopplats bort.** *Vad man gör:* lägg
  en panel flytande på den andra skärmen, Layout ▸ Spara layouten, koppla bort
  skärmen och välj Layout ▸ Återställ den sparade. *Rätt utfall:* fönstret
  hamnar inne i en befintlig skärms arbetsyta (78:s klämning), inte utanför.
- [ ] **Ägt fönster, Alt+F4 och genvägarna.** *Vad man gör:* med Bevakning
  flytande: klicka i dess fönster och tryck Ctrl+S, sedan Alt+F4; minimera
  huvudfönstret och återställ; Alt+Tab. *Rätt utfall:* Ctrl+S sparar layouten
  (huvudfönstrets meny svarar, statusraden säger "Sparad: …"); Alt+F4 stänger
  PANELEN och Visa ▸ Bevakning tar fram den i sitt fönster igen; det flytande
  fönstret ligger alltid ovanför huvudfönstret, minimeras med det och har
  ingen egen knapp i aktivitetsfältet.

## Storleksramen utanför den synliga kanten (TODO 81, DOCKNING_DRAG E2, 2026-10-13)

Koden: `Glaze/gfx/src/backend_d3d11/d3d11_window.cpp` — `WM_NCCALCSIZE` drar
in klientytan med `frameInsets()` (vänster, höger och nederkant; överkanten
orörd), `WM_NCHITTEST` lämnar ramen åt DefWindowProc och svarar HTTOP i ett
band överst, `hitTest` hoppar över kartans resize-roller, och
`setPosition`/`trackNormalPosition` räknar bort ramen (placeringen =
klientens hörn). geometry_gate K1–K10 mäter träffkoderna med postade
WM_NCHITTEST, ramen runt klientytan, det fasta och det maximerade fönstret,
`WM_GETMINMAXINFO`, bytet fast ↔ ändringsbart, None/transparent/popup och
DWM:s synliga kant. Den mäter inte en riktig mus, DWM:s utseende eller
blandad DPI — det gör raderna här.

- [ ] **Storleksändring med riktig mus från alla fyra kanter och hörnen.**
  *Vad man gör:* `demo` (och `demo --dock`); för pekaren sakta över varje
  kant och hörn utifrån och dra. *Rätt utfall:* resize-pilen kommer några px
  UTANFÖR den synliga kanten på vänster-, höger- och nederkanten (och i
  hörnen) och i de översta px av namnlisten; draget storleksändrar och Aero
  Snap (dra mot skärmkanten, Win+pil) fungerar som förut; fönstrets ram och
  rundade hörn ser ut som förut. DWM:s SKUGGA kan ha tillkommit (den ritas
  runt icke-klientramen, som nu finns på tre sidor) — notera hur den ser ut
  och om överkanten saknar den; ingen grind kan se det.
- [ ] **Verktygsfältet vid kanten är klickbart ända ut — buggen.** *Vad man
  gör:* `demo --dock`; klicka på det stående verktygsfältets knappar hela
  vägen ut mot vänsterkanten, och på statusraden/nedre fältet mot
  nederkanten. *Rätt utfall:* knappen trycks (ingen resize-pil, ingen
  storleksändring) ända ut till den synliga kanten.
- [ ] **Windows 10: överkanten och den dubbla linjen.** *Vad man gör:* på
  Windows 10, `demo`; titta på vänster-, höger- och nederkanten och dra i
  överkanten. *Rätt utfall:* ingen dubbel linje (DWM:s 1 px-kant innanför
  ramen mot vår egen ram) — syns den: 🐛, och vår ram ritas bara i överkanten
  när icke-klientramen finns. Överkanten storleksändrar i bandet.
- [ ] **Blandad DPI.** *Vad man gör:* två skärmar med olika skalning; dra
  `demo` från den ena till den andra och tillbaka, och storleksändra på båda.
  *Rätt utfall:* fönstret behåller sin LOGISKA storlek (±1 px — ramens
  tjocklek skalas av OS:et), ramen följer med och ingen kant hoppar.
- [ ] **Ett fast fönster har ingen ram, och låsningen i drift flyttar
  inget.** *Vad man gör:* öppna en meddelanderuta i `demo` (min == max);
  starta `demo --tray`, vars fönster låser sin storlek i `onLoad` — EFTER att
  OS-fönstret finns (`applyResizeStyle` tar bort ramen runt den oförändrade
  klientytan). *Rätt utfall:* ingen resize-pil någonstans, varken utanför
  eller innanför kanten; systemfältsfönstret står kvar på samma plats och i
  samma storlek när det låses (inget hopp på ramens bredd).

## Musens capture — förlusten och övertagandet (TODO 81, DOCKNING_DRAG E3, 2026-10-13)

Koden: `Glaze/gfx/src/backend_d3d11/d3d11_window.cpp` — `WM_CAPTURECHANGED`
blir `gfx::EventType::MouseCaptureLost` bara när fönstret höll capture för ett
tryck (`pressCapture_`) och capturen inte lämnades över UTTRYCKLIGEN
(`g_handoverTo`, satt bara under `captureMouse`); `captureMouse()` tar ett
pågående tryck från ett annat av appens fönster — bara när hållaren har ett
TRYCK och appen äger FÖRGRUNDEN (annars false och en loggrad);
`showInactive` gör den första visningen till `SW_SHOWNOACTIVATE`. I
`Glaze/widgets/src/app_window.cpp` nollar `dropPointer()` trycket utan
`mouseUp`, och `takePointerCapture` återvaliderar efter varje krok.
capture_gate D1–D10 mäter översättningen med SYNTETISKA meddelanden
(SendMessageW) — inte vad OS:et faktiskt skickar eller vart det routar
släppet. Det flytande verktygsfältets drag (fall B, där övertagandet används)
och lossningen utan fokusstöld är DOCKNING_DRAG E5:s rader.

- [ ] **Alt+Tab mitt i ett drag.** *Vad man gör:* `demo --gallery`; dra en
  Splitter eller ett reglage och tryck Alt+Tab innan du släpper; släpp i
  det andra fönstret och gå tillbaka med Alt+Tab. Gör om det med ett
  textfälts markering (dra över texten). *Rätt utfall:* draget står där det
  avbröts och följer INTE pekaren efteråt; nästa klick är ett vanligt klick
  (inget kvarhängande tryck, ingen knapp som ser nedtryckt ut).
- [ ] **Fönsterdrag och storleksändring ger inget avbrott.** *Vad man gör:*
  dra fönstret i namnlisten och storleksändra det från kanten, flera gånger;
  öppna en dropdown med ett tryck och dra till en rad. *Rätt utfall:* som
  före E3 — OS:ets flytt-/storleksloop och dropdownens förtida släpp
  (`releaseMouseCapture`) avbryter inget, och raden väljs.
- [ ] **Övertagandet kräver förgrunden (efter E5).** *Vad man gör:* `demo
  --dock` med ett flytande verktygsfält; klicka i en annan app så att den är
  förgrund, och dra sedan direkt i fältets rubrik (fältets fönster är
  noActivate och aktiverar inte appen). *Rätt utfall:* ingen krasch och inget
  fastnat tryck — draget stannar i fältets fönster (en loggrad säger att
  förgrunden tillhör en annan tråd); med appen som förgrund följer draget
  pekaren också utanför fältet.

## Ritytans lagerordning, textmåtten och den antagna skärmen (TODO 83, REQ2_RITYTAN, 2026-10-13)

Koden: `Canvas::moveLayer` och `Canvas::measureText`
(`Glaze/widgets/src/widgets/canvas.cpp`), mätningen `gfx::measureText`/
`SdfAtlas::advance` (`Glaze/gfx/src/sdf.cpp`), demons lagerlista
(`apps/demo/src/gallery_canvas.cpp` — `kartaLagerSteg`) och reserven i
`Glaze/gfx/src/display_util.cpp` med anropet i `d3d11_window.cpp`
`measureDisplay`. painter_gate LO, canvas_gate LO/M, geom_gate S6 och
display_gate R/F5 mäter det som går utan en människa — gröna i sviten
2026-10-13.

- [ ] **Lagerlistan i kartan.** *Vad man gör:* `demo --gallery` → Rityta;
  välj "Lager: Plattor" i vallistan uppe till vänster och tryck ▲ tre
  gånger, sedan ▼ tillbaka; klicka på en form efter varje steg. *Rätt
  utfall:* plattorna täcker till slut allt (och ligger underst igen efter
  ▼-stegen), statusraden visar ordningen överst först, ett klick ger rätt
  namn och lager, ingen hackning under flytten, och ▲/▼ har tooltipsen
  "Flytta framåt"/"Flytta bakåt".
- [ ] **En antagen skärm vid 100 % och 150 %.** *Vad man gör:* en RDP-session
  eller en VM utan EDID; demon → Rityta → linjalprovet vid 100 % och vid
  150 % skala. *Rätt utfall:* raden visar "mm: antagen" och 3,78 px/mm i
  BÅDA (förr 5,67 vid 150 %), loggraden säger "antas (96 dpi)", och kartans
  skala visar "≈1:N".

## Inmatningen — högerknappen, avbrottet och tangentsläppet (TODO 82, REQ2_INMATNING, 2026-10-13)

Koden: `AppWindow::dropPointer`/`routeAuxDown`/`countClick` och tangentvägen
(`Glaze/widgets/src/app_window.cpp`), ritytans krokar
(`Glaze/widgets/src/widgets/canvas.cpp`), `WM_CAPTURECHANGED` i
`d3d11_window.cpp` (oförändrad från DOCKNING_DRAG E3) och demons grupp 30
(`apps/demo/src/widget_gallery.cpp`, fliken Interaktion). input_gate H/D/A/T/K
och canvas_gate IN mäter det som går utan en människa — gröna i sviten
2026-10-13.

- [ ] **Högerdraget i grupp 30.** *Vad man gör:* `demo --gallery` →
  Interaktion → grupp 30; dra med HÖGER knapp i ytan. *Rätt utfall:* en
  accentfärgad linjal följer pekaren, statusraden säger "Höger ned" och vid
  släppet "Höger upp: N px"; ingen snabbmeny öppnas.
- [ ] **Avbrottet mitt i ett högerdrag.** *Vad man gör:* börja ett högerdrag
  i ytan och tryck Alt+Tab, Ctrl+Shift+Esc eller Win-tangenten innan
  knappen släpps. *Rätt utfall:* "Trycket avbröts" EN gång och linjalen
  försvinner; ett senare släpp ger inget "Höger upp".
- [ ] **Mellanslaget och fokusförlusten.** *Vad man gör:* klicka i ytan, håll
  mellanslag och tryck Alt+Tab. *Rätt utfall:* "Mellanslag hålls" medan
  tangenten hålls (statusraden står still under OS:ets upprepning), och
  "Mellanslag släppt (avbrutet)" vid Alt+Tab; ett vanligt släpp ger
  "Mellanslag släppt".
- [ ] **Dubbelklicken.** *Vad man gör:* dubbelklicka med mittenknappen och
  med högerknappen i ytan. *Rätt utfall:* "Mitten dubbelklick" respektive
  "Höger dubbelklick".
- [ ] **Ctrl+Enter i meddelandefältet.** *Vad man gör:* skriv i fältet,
  tryck Enter, sedan Ctrl+Enter, sedan AltGr+Enter (svensk layout). *Rätt
  utfall:* Enter ger en ny rad; Ctrl+Enter skickar ("Skickat: …" i
  statusraden) och tömmer fältet utan radbrytning; AltGr+Enter skickar inte;
  Ctrl+Z efter sändningen tar inte tillbaka det skickade.
- [ ] **Kartans panorering och Alt+Tab.** *Vad man gör:* fliken Rityta —
  börja panorera kartan med vänsterknappen, kasta den och tryck Alt+Tab mitt
  i draget. *Rätt utfall:* ingen tröghet efteråt — kartan står kvar där
  draget avbröts.

## Trädets godkännande och listvyns kryss (TODO 85, REQ2_TRAD_LISTOR, 2026-10-13)

Koden: sessionen (`Glaze/widgets/src/drag_drop.cpp` — `onDrop` före kroken,
`onDragOver` efter `onDragEnter`, `canDragFrom`), trädet
(`Glaze/widgets/src/widgets/tree_view.cpp`), listvyn och tabellen
(`list_view.cpp`, `table.cpp`) och demons grupp 13 och 14
(`apps/demo/src/widget_gallery.cpp`, fliken "Tabeller och träd"). dnd_gate
16–18, tree_gate 31–38 och listview_gate K1–K4/F14–F20 mäter det som går utan
en människa — gröna i sviten 2026-10-13. Grindarna ser INTE
overlayn: förbudsmärket och den uteblivna insättningslinjen är bara synliga
här. Ingen backendkod.

- [ ] **Grupp 13: appens regel och Musik.** *Vad man gör:* `demo --gallery`
  → "Tabeller och träd"; dra Rapport.pdf eller Brev.txt till trädets översta
  nivå (före Dokument, mellan toppnoderna, under Musik), släpp, och dra sedan
  en nod (t.ex. Semester) in i Musik — två gånger, med olika noder. *Rätt
  utfall:* överst visar pekaren förbudsmärket och ingen linje, och släppet
  flyttar ingenting (också när pekaren släpps utan att röra sig efter
  inträdet); i Musik hamnar noderna ALFABETISKT, Musik fälls ut och
  statusraden säger "Sorterade in alfabetiskt: N"; ett vanligt släpp någon
  annanstans säger "Flyttade noder: N". Tooltipen på trädet förklarar
  regeln, i alla fyra språken.
- [ ] **Grupp 14: listvyns kryss.** *Vad man gör:* klicka i rutorna framför
  namnen, tryck Space på den aktuella raden, sortera i huvudet (båda
  riktningarna), Ctrl+klicka flera rader och klicka sedan i en vald rads
  ruta, dubbelklicka en ruta, och byt UI/tema i startfönstret. *Rätt
  utfall:* klicket i rutan vänder krysset UTAN att flytta valet eller den
  aktuella raden ("Kryssad: …"/"Avkryssad: …" i statusraden); Space kryssar;
  krysset följer RADEN genom sorteringen; dubbelklicket i rutan öppnar inget
  ("Öppnade" syns inte); bocken är alltid i temats färg (aldrig Budget.xlsx
  röda), och den inaktiverade listan visar en dämpad bock.
- [ ] **Inget drag från rutan eller avdelaren.** *Vad man gör:* i en egen
  testapp med `setAllowDrag(true)` och en `onDragStart` som fyller lasten på
  listvyn: tryck och dra från en kryssruta och från en kolumnavdelare; i
  trädet med kryss från pilen och från rutan. *Rätt utfall:* inget drag
  startar (ingen dragbild); avdelaren ändrar kolumnens bredd.

## Dialogerna — standard- och avbrytknappen, maxlängden och platshållaren (TODO 84, REQ2_DIALOGER, 2026-10-13)

Koden: steget sist före Tab i `Glaze/widgets/src/app_window.cpp`
(`setAcceptButton`/`setCancelButton`, `defaultButton`), utseendet i
`Glaze/widgets/src/widgets/button.cpp` (`shownAsDefault`), fälten i
`textinput.cpp`/`text_area.cpp` (`fitMaxLength`, platshållaren),
meddelanderutan och filväljaren (`message_box.cpp`, `file_dialog.cpp`) och
demons Om-dialog, andra fönster och grupp 29/24 (`apps/demo/src/`).
dialog_gate (ny), textfield_gate H6/H7, preview_gate, roundtrip_gate och
pixel_gate mäter det som går utan en människa — gröna i sviten 2026-10-13.
Grindarna matar eventen direkt: tangentbordets Return och
numpadens Enter som verklig tangent provas bara här. Ingen backendkod.

- [ ] **Om-dialogen, i alla tre UI.** *Vad man gör:* `demo` → Arkiv → Nytt
  fönster → "Om…"; tryck Return, öppna igen och tryck numpadens Enter, öppna
  igen och tryck Escape; öppna en fjärde gång och Tab till Avbryt. Byt UI i
  startfönstret (Glaze, Slate, Minimalistic) och gör om. *Rätt utfall:*
  Return och numpad-Enter svarar OK, Escape Avbryt (tipsraden i andra
  fönstret säger vilket); OK står på accentytan när dialogen öppnas, och
  accentfyllningen flyttar till Avbryt när den får fokus — och tillbaka till
  OK när fokus lämnar knapparna.
- [ ] **Andra fönstret stängs med Escape.** *Vad man gör:* Arkiv → Nytt
  fönster, klicka i fönstret (inte i en lista) och tryck Escape. *Rätt
  utfall:* fönstret stängs, som med Stäng-knappen.
- [ ] **Meddelanderutan.** *Vad man gör:* andra fönstret → välj
  uppsättningen OKCancel → "Visa meddelanderutan" → Return; igen → Escape;
  välj YesNo → visa → Escape, sedan Tab till Nej och Return. *Rätt utfall:*
  OKCancel: Return = OK, Escape = Avbryt; YesNo: Escape gör ingenting (rutan
  står kvar, krysset är släckt), Return på Nej = Nej. Svaret står i tipsraden.
- [ ] **Filväljaren: Escape med filtypslistan öppen.** *Vad man gör:*
  `demo --gallery` → grupp 21 → "…" på det första fältet; öppna filtypslistan
  med musen och tryck Escape; tryck Escape igen. *Rätt utfall:* första
  Escape stänger bara listan, den andra avbryter dialogen. Enter i namnfältet
  med ett befintligt filnamn svarar OK.
- [ ] **Platshållarna och maxlängden.** *Vad man gör:* `demo --gallery` →
  grupp 29: töm lösenordsfältet (Ctrl+A, Delete) och klicka bort; klicka i
  det igen; klistra in en text längre än 16 tecken (t.ex. "åäöåäöåäöåäöåäöåäöåäö").
  Grupp 24: töm fältet med brytning. Byt språk i startfönstret. *Rätt
  utfall:* den dämpade hinten "Lösenord (högst 16 tecken)" syns i det tomma
  fältet, också med fokus, tills man skriver; inklistringen kapas till 16
  tecken (åäö räknas som ett tecken var); anteckningsfältets hint bryts mot
  fältets bredd; båda hintarna följer språket.

## Previewns text, radiogruppen, egna widgets och 3D-flygningen (TODO 58/59/60/55, FAS1_C_PREVIEW, 2026-10-13)

- [ ] **3D-panoreringen FLYGER.** *Vad man gör:* `demo --gallery` → fliken
  med ritytan i 3D; dra med MITTENKNAPPEN uppåt och nedåt, sedan i sidled;
  håll Shift och dra med mitten uppåt och nedåt; Shift + vänsterknappen
  uppåt och nedåt; styrplattans tvåfingersdrag med och utan Shift; upp- och
  nedpilen med ritytan i fokus. *Rätt utfall:* ett drag NEDÅT flyger FRAMÅT
  längs blicken (världen kommer emot en, också nedåt när kameran tittar ned),
  uppåt bakåt; i sidled följer scenen pekaren som förut; med Shift höjs
  kameran vid ett drag nedåt och sänks uppåt; uppåtpilen flyger framåt. Den
  lutade kartan (2D-fliken) panorerar som förut.
- [ ] **Radiogruppen i previewn och appen.** *Vad man gör:* skriv två
  `setChecked(true)` i samma grupp i en `.glazeui` (t.ex. en kopia av
  `tests/fixtures/gallery.glazeui`s `radA`/`radB`), bygg och kör appen, och
  öppna samma fil i VS Code-previewn. *Rätt utfall:* EN prick, på den
  senare, i båda.
- [ ] **En titel och en text med en konstant.** *Vad man gör:* skriv
  `setTitle(glaze::tr(app::str::Titel));` och `x.setText(glaze::tr(app::str::Hej));`
  i en `.glazeui` och öppna den i GlazeBuilder och VS Code. *Rätt utfall:*
  filen öppnas (inte fail-safe), titeln och texten syns INTE som
  uttryckstext, egenskapspanelen visar raden skrivskyddad, och Ctrl+S
  skriver tillbaka raderna ordagrant.

## Tre lägen, flerval, radval, alfa, klockslag, dialogerna och statusfälten (TODO 63/54, FAS1_B_WIDGETS, 2026-10-13)

- [ ] **Galleriets nya och växta grupper — RÄKNADE, inte renderade.** *Vad
  man gör:* `demo --gallery`; fliken Grundläggande (grupp 31 under g2),
  Tabeller och träd (32 under g13, 33 under g18) och Väljare (g22 och g23
  med en rad till, 34 i tredje kolumnen); byt UI (Glaze/Slate/Minimalistic)
  och språk (finskan är längst) i startfönstret. *Rätt utfall:* ingen grupp
  klipper sitt innehåll eller sin fot, ingen fot går ut genom gruppens
  högerkant, och sidorna rullar inte (g32 och g33 slutar vid g3:s underkant).
- [ ] **Kryssrutans tre lägen.** *Vad man gör:* klicka i "Tre lägen — klicka
  runt" fyra gånger; kryssa lagren under "Alla lager" ett i taget och klicka
  sedan i "Alla lager". *Rätt utfall:* av → på → streck → av; "Alla lager"
  visar strecket när ett av två lager är kryssat och bocken när båda är det,
  och ett klick i den kryssar alla eller inga; den inaktiverade visar ett
  tonat streck i alla tre UI (Minimalistic fyller rutan).
- [ ] **Flervalet i listan och datagriden, med MUSEN.** *Vad man gör:* grupp
  32 och 33: klick, Ctrl+klick, Shift+klick, Ctrl+Shift+klick, Shift+pil,
  Ctrl+pil följt av Ctrl+Space, Ctrl+A; tryck på en VALD rad och släpp.
  *Rätt utfall:* listvyns beteende rad för rad; statusraden visar "Valda
  rader: N" en gång per handling; trycket på en vald rad väljer den ensam
  först vid släppet; i datagriden fylls de valda radernas band ut till
  högerkanten, den aktuella cellen har en hårlinjeram, och en dubbelklickad
  cell redigeras som förut.
- [ ] **Färgväljarens alfa.** *Vad man gör:* grupp 22, raden "Alfa": öppna
  popupen, dra A-reglaget, skriv `#11223344` i hexfältet + Enter, välj en
  palettruta. *Rätt utfall:* rutan står över ett rutmönster som syns genom
  färgen; statusraden visar åtta siffror; paletten byter kulören men behåller
  alfan; de andra väljarna har ingen A-rad.
- [ ] **Datumväljarens klockslag.** *Vad man gör:* grupp 23, "Klockslag":
  skriv `2026-10-13 9:05` + Enter, `2026-10-14` + Tab, `2026-10-15T7:00` +
  Enter, `2026-10-15 25:00` + Enter; välj en dag i kalendern. *Rätt utfall:*
  fältet blir `2026-10-13 09:05`, sedan `2026-10-14 09:05` (tiden står kvar)
  och `2026-10-15 07:00`; `25:00` skrivs tillbaka utan ändring; kalenderns
  val behåller tiden; fältet är bredare än de andra och klipper inte texten.
- [ ] **Färgdialogen och inmatningsrutan.** *Vad man gör:* grupp 34: öppna
  färgdialogen, pila i paletten, dra A, skriv ett hexvärde utan Enter och
  klicka OK; öppna igen och tryck Escape; öppna "Byt namn …", skriv ett namn
  + Enter, öppna igen, töm fältet + OK, öppna igen + Escape. *Rätt utfall:*
  dialogerna står mitt över galleriet med VÅR namnlist och utan programikon;
  pilarna väljer utan att stänga; OK tar det skrivna hexvärdet (statusraden
  `#RRGGBBAA`) och nästa öppning börjar där; Escape ändrar ingenting
  ("Dialogen avbröts"); inmatningens förval står markerat med caret i
  fältet; ett tömt fält + OK ger "Nytt namn: " (en tom text är ett svar).
- [ ] **Statusfälten.** *Vad man gör:* byt flik i galleriet; byt språk; titta
  på grupp 8:s inaktiverade statusrad. *Rätt utfall:* fönstrets statusrad
  har ett fält längst till höger med den aktiva flikens namn, avskilt med en
  hårlinje, och det följer språket; grupp 8:s rad visar tre dämpade fält med
  hårlinjer emellan.

## Storleken, fönstret som följer innehållet, tangentens text och verktygsfältets kanter (TODO 90, REQ3_LAYOUT_FONSTER, 2026-10-13)

- **`onResize`** — *Gör:* `demo --window-lab`, dra i fönstrets kant; sedan
  `demo --gallery` → Interaktion, grupp 35, dra i delaren. *Rätt utfall:*
  labbets "Innehåll: B × H" uppdateras när draget SLÄPPS (OS:ets modala
  loop), galleriets "Ytan: B × H" per rörelse.
- **"Följ innehållet"** — *Gör:* labbet → "Följ innehållet", kryssa "Fler
  rader" av och på, försök dra i över- och underkanten, dra dialogen bredare.
  *Rätt utfall:* dialogen växer och krymper med raderna, höjden går inte att
  dra, och bredare ⇒ texten bryts om och höjden följer (efter släppet).
- **Snabbmenyn vid pekaren** — *Gör:* grupp 35, högerklicka i ytan. *Rätt
  utfall:* menyn "Markera här" öppnas exakt vid pekaren vid släppet, och
  valet skriver punkten i statusraden.
- **Tangentens text** — *Gör:* grupp 35:s fält: å, AltGr+2, ´ sedan e,
  Ctrl+S. *Rätt utfall:* statusraden «å», «@», «é» (på e) och «» för Ctrl+S;
  varje tecken skrivs EN gång i fältet.
- **Text vinner över genvägen** — *Gör:* `demo --dock`: lossa sökpanelen,
  skriv m i dess fält; sätt sedan fokus på en knapp och tryck M. *Rätt
  utfall:* m skrivs i fältet utan statustext; med fokus på knappen står
  "Genvägen M kördes".
- **Redigera-fältets kanter** — *Gör:* `demo --dock`, dra Redigera-fältets
  grepp mot vänster- och högerkanten, sedan uppe och nere. *Rätt utfall:*
  ingen platshållare vid vänster/höger kant (tooltipen säger varför); uppe
  och nere dockas det.
- **"Aktivt fönster"** — *Gör:* klicka mellan labbet och galleriet, och till
  en annan app. *Rätt utfall:* raden byter till det klickade fönstrets
  titel, och "inget" när en annan app är framme.

## Träd, listor och fält (TODO 89, REQ3_TRAD_LISTOR_FALT, 2026-10-13)

- **Snabbmenyn, autoskrollen och fälten (Eriks handtest)** — *Gör:* `demo
  --gallery`. Tabeller och träd: högerklicka på en ovald nod (väljs och "Ta
  bort"-menyn visas), på en vald (valet står kvar); dra en nod nära trädets
  nederkant; listvyns namnkolumn fyller bredden. Fliken Text, grupp 36
  (rulla ner): Tab i anteckningen, "…"-etiketten med tooltip, "Namn" mitt
  för fältet, röd knapp, 32 px-ikon, 48 px-miniatyrer, dialogtitlen "Välj en
  bild". *Rätt utfall:* som beskrivet; grindarna ser inte hur autoskrollens
  takt KÄNNS.

## Ritytans text med tak, golv och tak, synlighet efter zoom (TODO 91, REQ3_RITYTAN, 2026-10-13)

Koden: taket i `Glaze/gfx/src/sdf.cpp` (`layoutText`, `maxLines`/
`maxHeight`), golvet och taket i Glyph-programmet (`d3d11_glyph.cpp`,
`SceneUniforms::glyphMinPx`/`glyphMaxPx`), zoomintervallet genom
synlighetsbilden (`Glaze/widgets/src/canvas/`) och demons karta
(`apps/demo/src/gallery_canvas.cpp`). geom_gate S7, painter_gate GT/ZV/MB,
scene_pixel_gate D10–D12, canvas_gate M8–M9/ZV, plumbing_gate PX och icon_gate
G11 är gröna i sviten 2026-10-13; raderna här är det bara en människa ser.

- **Anteckningen i sin ruta** — *Gör:* `demo --gallery` → Rityta; leta upp
  den ljusa rutan nordost om Ekparken, byt språk (sv, en, nb, fi) i
  startfönstret, zooma, vrid (Ctrl+drag i sidled) och tilta (Ctrl+drag
  lodrätt). *Rätt utfall:* texten bryts vid rutans bredd och stannar i
  rutan med "…" på sista raden i alla fyra språken, och den följer rutan
  (ligger i marken) vid zoom, vridning och tilt.
- **"Kyrkbacken" med golv och tak** — *Gör:* zooma ut och in över kyrkan och
  mät namnets höjd med linjalprovet (eller en linjal mot skärmen). *Rätt
  utfall:* namnet växer med zoomen men stannar vid ≈ 2 mm utzoomat och
  ≈ 6 mm inzoomat.
- **Husens namn efter zoom** — *Gör:* zooma in från startläget, ungefär två
  gånger, och ut igen. *Rätt utfall:* husens namn tonar in vid ungefär 2×
  inzoomning och tonar ut igen när man zoomar ut.

## Listans aktivering (TODO 93, REQ5 1, 2026-10-13)

- **`ListBox::onActivate`** — *Gör:* `demo --dock`; dubbelklicka på en fil i
  Utforskarens lista, välj en annan med pilarna och tryck Enter; dubbelklicka
  på tom yta under raderna. *Rätt utfall:* statusraden visar filens namn vid
  dubbelklicket och vid Enter, och tom yta ger ingenting. listbox_gate A1–A3
  är gröna i sviten 2026-10-13.

## Dubbelklickets tolerans i skärmrymden (TODO 97, Mac-passet 2026-10-14)

- **`countClick` avbildar toleransen genom `clientToScreen`** (`Glaze/widgets/src/
  app_window.cpp`) — rättat för macOS, där skärmrymden är punkter; här är
  avbildningen en ren förskjutning och toleransen ska bli exakt densamma som
  förut. *Gör:* `ctest -R input_gate`, och dubbelklicka i en lista i
  `demo --gallery`. *Rätt utfall:* D-delen grön, och dubbelklicket fungerar
  som förut.

## `app_perf` — en app på riktigt, med minnet (2026-10-14, BLINT skrivet)

- **Bygget och minnesfrågorna** (`tests/app_perf.cpp`, `läsMinne`:
  `GetProcessMemoryInfo` med `PROCESS_MEMORY_COUNTERS_EX` och DXGI:s
  `IDXGIAdapter3::QueryVideoMemoryInfo` för adapter 0; länkar `dxgi` och
  `psapi`) — *Gör:* bygg `app_perf` i Release och kör
  `./build-rel/tests/app_perf --synlig` utan att röra musen. *Rätt utfall:*
  0 varningar; två fönster (kartan och sedan 3D-vyn) som rör sig själva i
  ~90 s; varje stegs minnesrad har ett tal för `process` och `GPU` (inte
  `—`); sist `sessionen gick igenom` och sluttabellen. Kör också utan `--synlig`
  (dolda fönster, ctest:s läge): samma utfall utan synliga fönster.

- **Varvtestet** (`app_perf --varv 4`, synligt) — *Gör:* kör det utan att
  röra musen (~8 minuter). *Rätt utfall:* tabellen sist växer med högst
  tiotals MB per varv efter varv 1, inte GB. På macOS läckte Metal 8 GB per
  varv innan `renderFrame` fick sin autorelease-pool (PERFORMANCE anm. 12).
  Backenden här har ingen autorelease, men kan ha en egen läcka.

## Ritytans textmått i layouten och uppladdningarna (TODO 102, REQ7_RITYTAN, 2026-10-14, BLINT skrivet)

Koden: `Canvas::measureText(c, …)` och vakten i `Glaze/widgets/src/widgets/
canvas.cpp`, `detail::measureNeedsWorld` i `Glaze/widgets/src/canvas/build.cpp`
(samma kod på alla tre). Uppladdningarnas vägar är bara Metal-kod: D3D11
ordnar `UpdateSubresource` i kontexten och har inget pass att bryta, så
`RenderStats::uploadBreaks` är 0 här per konstruktion.

- **Grindfallen** — *Gör:* `ctest -R "canvas_gate|geom_gate|scene_pixel_gate|canvas_perf"`.
  *Rätt utfall:* canvas_gate MR1–MR5b, geom_gate S8, scene_pixel_gate
  A8–A11/B3/B12–B13 (brotten 0 — också A8 och B3 —, pixlarna gröna),
  canvas_perf U och canvas_perf3d S/Ä gröna.
- **`app_perf --synlig` mot M1** — *Gör:* mät `app_perf` i Release och jämför kartans panorering (andel > 25 ms) med
  M1-avsnittet efter REQ7_RITYTAN. *Rätt utfall:* `brott +0` i varje steg;
  körningen förs in som ett eget avsnitt under *Appen* ("kostar inte mer på
  Metal än på D3D11", planens §8).

## Formuläret — rader mot nederkanten, innehållet som golv och etikettkolumnen (TODO 101, REQ7_LAYOUT, 2026-10-14)

Ingen Windows-kod rörs: beteendet går genom `AppWindow::refit`/`effMin*`
(`Glaze/widgets/src/app_window.cpp`), `Widget::extentOf` (`widget.cpp`) och
`Label::groupWidth` (`label.cpp`) — och till OS:et genom D3D11:s
`setSizeLimits` (`WM_GETMINMAXINFO`). Demon: `apps/demo/src/form_dialog.*`.

- **Formuläret öppnas och dras** — *Gör:* `demo --window-lab` → "Formulär…",
  dra nederkanten uppåt och nedåt och högerkanten inåt och utåt. *Rätt utfall:* dialogen öppnas i
  innehållets höjd (OK och Avbryt nere till höger, kontaktlistan mellan
  kryssrutorna och knapparna); kanten går INTE att dra uppåt förbi
  innehållet, men nedåt, och listan växer med; högerkanten går inte att dra
  inåt förbi etikettkolumnen och fältens minsta bredd, men utåt (fälten växer).
- **Lås och visa** — *Gör:* kryssa "Lås storleken", försök dra i kanten;
  kryssa "Visa kontakterna" av och på, låst och olåst. *Rätt utfall:* låst
  går kanten inte att dra och dialogen krymper till innehållet; låst krymper
  och växer den med listan, olåst växer den bara (en dold lista lämnar
  storleken).
- **Maximera** — *Gör:* olåst: maximera och återställ. *Rätt utfall:*
  maximeringen går, och tillbaka står dialogen i sin storlek med golvet kvar.
- **Etikettkolumnen** — *Gör:* startfönstrets språk fi, sedan stil
  Minimalistic, med formuläret öppet. *Rätt utfall:* etikettkolumnen breddas
  i BÅDA ramarna ("Postinumero ja -toimipaikka") och fälten står i linje
  under varandra i Kontakt och Adress.

## Markdown-vyn — formaterad text i en rullande yta (TODO 103, REQ7_MARKDOWN, 2026-10-14)

BLINT skriven för Windows: ingen backendkod rörs, men vyn läser
D3D11:s DirectWrite-fonter (fet, kursiv, Consolas) genom fönstrets `FontSet`
(`Glaze/widgets/src/font_set.cpp`) och öppnar länkar med `gfx::openUrl`.
Koden: `Glaze/widgets/src/widgets/markdown_view.cpp`, `Glaze/widgets/src/markdown/`.

- **Grupp 37** — *Gör:* `demo --gallery` → fliken Text, rulla till grupp 37,
  i Slate, Glaze och Minimalistic. *Rätt utfall:* rubrikerna i tre storlekar
  och fetstil; kursiv; koden i Consolas på SAMMA baslinje som texten
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

## Utskriften — skriven och byggd, aldrig körd (REQ8 10, 2026-10-14)

`Glaze/gfx/src/backend_d3d11/d3d11_print.cpp` och `Glaze/widgets/src/print.cpp`.
Ingen grind kan öppna systemets dialog eller en skrivare — det här är händer.

- [ ] **Dialogen**: `PrintJob::showDialog(*this)` med A4 liggande och 2 kopior
  förvalda — PrintDlgEx öppnar över ägaren med standardskrivaren, liggande och
  2 kopior; Avbryt ger false, Skriv ut ger `printerName()` och
  `printableArea()` (några mm marginal på en riktig skrivare, 0 på
  "Microsoft Print to PDF").
- [ ] **Till PDF**: "Microsoft Print to PDF", `run(*this, 3, …)` med en ram 5 mm
  innanför `printableArea()` och text i `glaze::mm(5)` — tre sidor, ramen och
  texten på rätt plats och i rätt storlek (mät i PDF-läsaren), förloppsfönstret
  med "Sida n / 3" medan sidorna ritas. Avbryt mitt i (t.ex. 50 sidor) ger
  inget jobb i kön.
- [ ] **Sida för sida**: `run(*this, 0, …)` som svarar false på sida 4 ger fyra
  sidor; intervallet 2–3 ur dialogen ger två.
- [ ] **Bilder**: en sida med en stor `Bitmap` (större än uppladdningsbudgeten,
  16 MiB) — bilden finns på sidan (sidan ritas om tills den är uppe).

## Prestandan — adaptern, `terrain_perf` och den riktiga takten (TODO 98, PRESTANDA, 2026-10-17)

Grindarna körs med sviten före committen; det här är det som kräver synliga
fönster, en riktig panel och en människa som läser. Koden: adapterloggen i
`Glaze/gfx/src/backend_d3d11/d3d11_renderer.cpp` (en gång per process varje
adapter, och "ritar med" vid varje ny adapter), `tests/terrain_perf.cpp` och
`tests/perf/`.

- [ ] **Adapterraden i debug-utgången** — *Gör:* kör `terrain_perf --synlig`
  och `terrain_perf --synlig --hp` under DebugView (eller i en konsol — testet
  är ett konsolprogram). *Rätt utfall:* "D3D11: adapter 0: Intel(R) UHD
  Graphics …", "adapter 1: NVIDIA GeForce RTX 4070 Laptop GPU …" och
  "adapter 2: Microsoft Basic Render Driver … programvara" EN gång, sedan
  "D3D11: ritar med Intel(R) UHD Graphics (preferensen Default)" utan `--hp`
  och "… NVIDIA GeForce RTX 4070 Laptop GPU (preferensen HighPerformance)"
  med; 3D-vyns fönster och popuper på samma adapter loggar ingenting till.
- [ ] **`terrain_perf` på den inbyggda panelen (240 Hz)** — *Gör:*
  `terrain_perf --synlig` (Intel UHD, förvalet) och `--synlig --hp` i Debug,
  utan att röra musen. *Rätt utfall:* sessionen går igenom, steg 27 ritar 0
  bilder, och båda körningarna förs in som avsnitt. Målet efter etapp E:
  intervallets p95 ≤ 20 ms i rörelsestegen (3–7, 12–16, 24 och 26) — ett mål,
  ingen grind. Efter våg 2 på `--hp` gällde det inte i steg 3–6 (p95
  20–26 ms) eller i 26 (23 ms); Intel UHD är inte mätt.
- [ ] **`terrain_perf` på den externa 4K-skärmen (59 Hz, NVIDIA)** — *Gör:*
  anslut skärmen och kör `terrain_perf --synlig --skärm 2`. *Rätt utfall:*
  båda fönstren öppnas på den externa skärmen, 3D-vyn ensam (steg 12–16)
  håller p50 ≈ 16,9 ms, och loggen säger vilken GPU som ritar där.

## Inmatningen — rullningens riktning med riktiga enheter (REQ9_INMATNING, 2026-10-20)

- [ ] **Rullningens riktning med riktiga enheter (REQ9_INMATNING)** —
  *Gör:* (1) ändra styrplattans "rullningsriktning" i Inställningar och rulla en
  lista med två fingrar, med och utan `setWheelInvert(Touchpad, true, …)`; (2)
  ett sidohjul (`WM_MOUSEHWHEEL`) i en bred tabell; (3) Shift+mushjul i en bred
  tabell; (4) galleriets lista "Med kryssrutor" (vänt mushjul) och 3D-gruppens
  "Vänd hjulet"/"Vänd zoomen". *Rätt utfall:* (1) OS:ets val är redan
  tillämpat i det glaze får (B13-antagandet) och vändningen vänder därifrån;
  (2) sidohjulet rullar i sidled; (3) Shift+hjul NED rullar åt HÖGER; (4)
  listan rullar åt andra hållet än de andra, ritytan zoomar ut på hjul upp.
