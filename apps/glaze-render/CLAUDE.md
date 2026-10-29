# CLAUDE.md — apps/glaze-render

**glaze-render** — konsolprogram som renderar en genererad fönster-triplett
offscreen till PNG.
Fundamentet för VS Code-previewn och regressionsbilder. Del av GlazeUI —
se [root /CLAUDE.md](../../CLAUDE.md) för tre-fils-modellen och de hårda
reglerna.

```
glaze-render <fil.h|.glazeui|.cpp> [-o ut.png] [--scale <faktor>]
```

Valfri triplett-fil normaliseras till `.h` (`headerPathFor`); default-utfil
är `<bas>.png` bredvid källan. Exit-koder: **0** = OK · **1** =
reader-fail-safe (skadat stängsel/saknad fil — diagnos på stderr, regel 4)
· **2** = render-/IO-fel eller felaktig användning.

**`--scale <faktor>` tar ABSOLUT skala** (L12/US del 2b): `--scale 1.0`
ger scale 1 på alla maskiner — deterministisk jämförelserendering för
referensbilderna i [doc/os/](../../doc/os/README.md) (namnkonventionen
`<fönster>_<skala×100>.png`). **Probe-and-recreate, INTE skalbyte i
drift** (CLI:t har ingen pump — eventvägen fungerar inte här): (1) skapa
fönstret dolt och läs `os = uiScale()` (userScale är 1.0 ⇒ ren OS-skala),
(2) riv det och `gfx::setUserScale(mål/os)`, (3) skapa PÅ NYTT — init
räknar logisk × os × user från början och showOwned laddar fonten i fulla
skalan. OBS: userScale klampas 0.5–2.0 (låst beslut), så mål utanför
os × [0.5, 2.0] klampas — stdout-raden visar faktisk skala. Utan flaggan:
maskinens nativa skala, ingen probe.

**Stdout-raden vid OK är ett KONTRAKT:** `"<path>: <w>x<h> px (scale <s>)"`
— VS Code-extensionen ([tools/vscode-glazeui](../../tools/vscode-glazeui/CLAUDE.md))
parsar bredd/höjd/scale ur den (HiDPI: webview-bredd = w/scale). Formatet
får inte ändras fritt; scale skrivs med `%g` (full precision — 1.25 får
aldrig trunkeras). **Kontraktstillägg (del 2b):** med `--scale` är
scale-fältet den TVINGADE skalan — formatet är oförändrat och extensionen
skickar aldrig flaggan. Stdout ska förbli REN — loggar går till stderr.

## Så funkar det (hela flödet bor i `src/main.cpp`)

1. **Läser via builderns `cpp_reader`** (`builder_core`-libben) — parsar
   ALDRIG användarens `.cpp`/handlers; `!ok` ⇒ diagnosen rakt av + exit 1.
2. **assets.bin** letas uppåt från `.h`-katalogen (fyra nivåer, samma
   vandring som builderns `AssetDoc`) och laddas FÖRE fönstret — img-fält
   ritar riktiga ikoner.
3. **language.bin** letas uppåt på EXAKT samma sätt och laddas också FÖRE
   fönstret (SPRÅKPAKET §5d), så `setText(glaze::tr(1042))` ritar riktig
   text i stället för platshållaren `⟦1042⟧`. Ingen fil är inget fel:
   ramverkets egna id 1–999 är inbäddade i libglaze och fungerar ändå.
   SPRÅKET väljs sedan av `lang=`-nyckeln i designtaggen om den finns, och
   annars av kedjan i `glaze::tr`: OS-lokalen → engelska. Att previewn kan
   visa ett annat språk än den körda appen är samma normala möjlighet som
   för `ui`/`theme` — taggen beskriver RITBORDET.
4. **Trädet byggs av `bld::buildWindowTree`** ([../GlazeBuilder/src/window_tree.h](../GlazeBuilder/src/window_tree.h),
   libbet `builder_preview`) — bygget bodde här i `RenderWindow` fram till
   2026-10-07 och flyttade ut när `tests/pixel_gate.cpp` behövde EXAKT
   samma träd (en kopia i grinden hade mätt kopian). Funktionen returnerar
   widgeten per modellnod; glaze-render behöver inte svaret.
   Bygget sker i **TVÅ PASS** (APP_API_2 steg 1) exakt enligt
   `rebuildPreview`-mönstret (GlazeBuilder): först TRÄDET i NODORDNING
   (inbyggda fönstermedlemmar först, sedan adopt-ordningen = z-ordningen),
   sedan SATSERNA i KÄLLORDNING. De två ordningarna är avsiktligt skilda —
   satslistan bär exekveringsordningen, nodlistan trädbygget — och
   misslyckas ett föräldrauppslag LOGGAS det numera i stället för att tyst
   falla till roten. **Nakna fönsteranrop** (`setContentSize(…)`,
   `setMinimumSize(…)`, `setModal(…)` …) körs i samma pass via
   `bld::applyWindowCall`, alltså i källordning — ordningen bär semantik
   ("den som sätts sist gäller"). Ett fönsteranrop kan bära en MEDLEM
   (`setAcceptButton(okBtn);`, REQ2_DIALOGER): det slås upp i det byggda
   trädet, så satsen måste stå efter trädbygget — det gör den, för pass 2 kör
   efter pass 1 — och standardknappen ritas i bilden. Vidare:
   **REGISTRET skapar widgeten** (🐛 60, `bld::makeNodeWidget` →
   `glaze::makeWidgetOfClass` med medlemsradens klassnamn, EXAKT med
   namnrymden); en klass registret inte känner ⇒ `StandInWidget` (regel 4).
   Ett glaze-render-bygge som länkar appens egna widgets (`GLAZE_TOOLS`)
   ritar dem alltså på riktigt; paletten frågas inte. En titel som inte är en
   literal eller `glaze::tr(N)` är en opak sats — fönstret får ingen titel,
   aldrig uttrycket (🐛 58). Egenskaper OCH metodanrop via **`bld::applyProp`/
   `bld::applyCall`** i libbet `builder_preview` — EN sanning delad med
   builderns preview sedan 2026-08-13 (APP_API steg 1; här stod förut en
   handkopierad spegel vars kommentar rentav sa "ordagrann spegel", och de
   var inte det). Okänd nyckel och okänt metodnamn är OPAKA: loggas +
   ritas/utförs inte. Adoption i nodordning = z-ordning.
   **INBYGGDA fönstermedlemmar särhanteras:** `statusBar` skapas ALDRIG ur
   registret — fönstret HAR redan widgeten och adopterade den i sin
   konstruktor, så noden mappas till fönstrets `statusBar` och får sina fält
   och anrop applicerade där. En ny instans hade gett en andra statusrad
   utan koppling till fönstret, och `.show()` hade då synts i previewn men
   inte i appen. **De ICKE-VISUELLA medlemmarna** (FONSTER_OCH_TIMER — en
   `glaze::Timer`, en `glaze::TrayIcon`, egna `glaze::Component`) skapas av
   **`bld::buildComponents`** efter trädet och hålls i
   `RenderWindow::components_`: bara typer registrerade som INERTA, med sina
   satser utförda — men `start()` och `show()` aldrig, så ingen timer tickar
   och ingen ikon hamnar i OS:ets fält. De syns inte i bilden; en okänd
   komponent loggas. `setChrome(gfx::Chrome::None)` ger en bild utan
   namnlist. **Events ignoreras** —
   de ligger som text i modellen, inget kompileras.
5. **Designtaggen hedras** (DESIGNTEMA): bär `.glazeui` en rad
   `// glazeui-design: ui=… theme=… style=… lang=…` sätts det APP-GLOBALA
   utseendet ur den (`bld::applyDesignTag` i
   [../GlazeBuilder/src/design_theme.h](../GlazeBuilder/src/design_theme.h) —
   EN sanning delad med GlazeBuilder). **EN gång, före `show()`:**
   utseende-API:t skriver `App::themeState` (statiskt), så state:t överlever
   att probe-fönstret rivs, och `show()` laddar fonten ur aktuell Style —
   inget explicit `reloadFont()` behövs. Taggen är en KOMMENTAR och
   kompileras aldrig; utan tagg gäller ramverkets defaults precis som förut.
   Avkodningsdiagnoser går till **stderr** — stdout-raden är orörd.
   Två designrader i filen = fail-safe ⇒ exit 1, som skadat stängsel.
6. **EN engångs-frame i ett DOLT fönster** (regel 1 — ingen pump):
   `setOffscreen(true)` + `show()` + `window().renderFrame()`, sedan
   `captureFrame` (retained-RT:n 1:1, fysiska px) + `gfx::encodePng`.
7. **Teardown utan pump:** fönstrets dtor kör `executeClose` (images →
   font → shutdown — orubblig ordning); ingen `run()` behövs.

## WYSIWYG-garantin

Länkar `glaze` ⇒ den enda inkompilerade backenden för byggplattformen
(Mac=Metal, Windows=D3D11, Linux=Wayland/X11-backenden).
PNG:en ritas av exakt samma backend som appen kör med
på maskinen, i native scale (mått = klientyta × `uiScale`), chrome MED i
bilden — pixelidentisk per konstruktion.

**Vad garantin BETYDER** (preciserat i DESIGNTEMA): bilden visar "så här ser
fönstret ut i SITT designtema", inte "så här ser det ut för användaren".
Utseendet är app-globalt och ljust/mörkt följer betraktarens OS (`ThemeMode`
ingår med flit INTE i taggen — läget hör till den som TITTAR, inte till den
som designar). Det var redan sant före taggen; taggen gör bara valet
explicit i stället för dolt.

Känd preview-avvikelse mot körd app: opaka fältrader (t.ex. Dropdowns
`items = {...}`) ritas inte — samma utfall som GlazeBuilders preview — och
inte heller ett textfält vars värde är en konstant (`glaze::tr(app::str::X)`,
`kNamn`): verktygen läser bara literalen och NUMRET.

## Fallgropar

- **Länka `glaze`, aldrig `glaze_impl`** (WHOLE_ARCHIVE-regeln i statiskt
  läge — widget-registreringarna strippas annars och varje typ blir
  stand-in; i delat läge bär `glaze` programfilens ankare).
- **Egna widgets i ETT EGET verktygsbygge** (🐛 60): `-DGLAZE_TOOLS=ON` hos en
  konsument bygger glaze-render (och GlazeBuilder) också när GlazeUI är ett
  beroende, och appen länkar in sina widgets med `target_link_libraries(
  glaze-render PRIVATE <OBJECT-bibliotek>)` — ett statiskt bibliotek hade
  strukit registreringarna. Binären hamnar under GlazeUI:s byggträd
  (`_deps/glazeui-build/apps/…` med FetchContent), så VS Code pekas dit med
  `glazeui.renderPath`. Medlemsraden måste skriva klassen med namnrymden.
- **Statiskt i VSIX:en.** I delat läge (`GLAZE_SHARED`) lägger
  `glaze_runtime_dlls(glaze-render)` DLL:erna bredvid exe:n; det buntade
  verktyget är alltid det statiska bygget, och `copy-bin.mjs` vägrar ett
  delat `build/`.
- `palette`/`standin_widget`/`design_theme`/`apply_props`/`window_tree`/
  `component_tree` bor i libbet **`builder_preview`** ([apps/GlazeBuilder/CMakeLists.txt](../GlazeBuilder/CMakeLists.txt))
  — de beror på BÅDE `glaze` och `builder_core` och hör därför varken
  hemma i rena `builder_core` (ren std) eller som käll-listade filer i två
  appar. Fram till 2026-08-13 var de just det, och `applyProp` fanns i två
  handkopierade exemplar.
- **Mac kräver WindowServer-session** även för dolda fönster
  (AppKit/CAMetalLayer) — funkar lokalt/i VS Code-scenariot, INTE på ren
  headless-CI (känd begränsning ur planen).
- Skalan: `setClientSize` tar LOGISKA px (L6a), så PNG:ns PIXELmått följer
  maskinens skala — `--scale <absolut>` ger samma mått överallt och gör
  geometrin diffbar mellan OS:en (se [doc/os/README.md](../../doc/os/README.md)).
