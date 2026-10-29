# CLAUDE.md — apps/GlazeBuilder/src

**GlazeBuilder** — WYSIWYG-editorn för tre-fils-modellen, själv byggd med
GlazeUI. Del av GlazeUI — se [root /CLAUDE.md](../../../CLAUDE.md) för
modellen och de hårda reglerna (särskilt 4: okänt bevaras ordagrant, och
5: raderar ALDRIG användarkod).

## Filer

**`builder_core`** (statiskt lib, [../CMakeLists.txt](../CMakeLists.txt)) =
`window_model` + `cpp_reader` + `cpp_writer` — den delbara kärnan (ren
std, fönsterfri), länkad av GlazeBuilder OCH `apps/glaze-render` (GLAZE_
RENDER beslut 3): ingen andra sanning om triplett-grammatiken.

**`builder_preview`** (statiskt lib, samma CMakeLists — APP_API steg 1,
2026-08-13) = den TREDJE nivån: `apply_props` + `palette` +
`standin_widget` + `design_theme` + `preview_tree` (2026-08-20) +
`window_tree` (2026-10-07) — och med sidopaketet Terminal **`apply_terminal.cpp`** (REQ9_TERMINAL E: `if(TARGET glaze_terminal_ui)` lägger källan, länkar `glaze_terminal_ui` PUBLIC och definierar `GLAZE_PREVIEW_TERMINAL`; terminalens anrop med flera argument och textstorleken, med `apply_props.cpp`:s parsrar genom den interna `apply_props_internal.h`, och palettraden finns bara då) — alltså det
som beror på BÅDE `glaze` och `builder_core` — en sanning, mätt av
`tests/preview_gate.cpp`. Länkar `glaze` — ALDRIG `glaze_impl` (WHOLE_ARCHIVE
i statiskt läge; i DELAT läge (`GLAZE_SHARED`) bär `glaze` programfilens
ankare). Exe:n har `glaze_runtime_dlls(GlazeBuilder)` (DLL:erna bredvid den i
delat läge, annars en no-op); VSIX:en buntar alltid det statiska bygget.

Övriga filer är GlazeBuilder-appens egna.

- **`window_model.h/.cpp`** — `bld::WindowModel`: värdesemantisk modell av
  ETT fönster (klassnamn, titel/storlek, noder med C++-typnamn/id/parent/
  props/events). Egenskapsvärden lagras som **RÅ C++-uttryckstext**
  (`"\"Tryck här\""`, `"{false, false, true, true}"`) — så round-trippas
  okända fältrader ordagrant. Props-listans ORDNING är emissionsordningen;
  `setProp()` skriver om värden PÅ PLATS och appendar nya fält sist.
  **`Call`** (APP_API steg 5): en METODANROPSSATS `<id>.<metod>(<arg>);`
  — sedan APP_API_2 en `Stmt` i den ordnade listan (`Prop`/`Call` är i dag
  bara de LÖSKOPPLADE par `applyProp`/`applyCall` tar emot), argumentet
  lagrat som RÅ uttryckstext precis som ett fältvärde. Formen finns för att `.glazeui`
  är riktig C++ som KOMPILERAS: `visible_` är privat och API:t är
  `setVisible()`/`show()`/`hide()`, så tillstånd GÅR INTE att uttrycka som
  en fältrad — och ett publikt `visible`-fält hade gått runt settern, som
  är den som INVALIDERAR (regel 1).
  **`Stmt`** (APP_API_2 steg 1) är satsformen: `WindowCall`/`WindowEvent`/
  `Prop`/`Call`/`Event`/`Adopt` i EN ordnad lista. Titel och storlek är
  därför INTE fält längre utan LÄSFUNKTIONER (`title()`/`clientWidth()`/
  `clientHeight()`) över den SISTA relevanta `WindowCall` — "den som sätts
  sist gäller", exakt kontraktet i `app_window.h`. `title()` svarar bara med
  en LITERALS text, annars tomt (🐛 58: glaze-render konstruerar fönstret med
  den); readern tar `setTitle(<uttryck>)` som en OPAK sats, aldrig fail-safe.
  Muteringen sker genom
  NAMNGIVNA operationer (`setProp`/`appendCall`/`appendEvent`/
  `setAdoption`/`setWindowCall`/`appendNode`/`removeNode`), och
  `setWindowCall` uppdaterar SISTA förekomsten på plats — två
  `setContentSize` i källan round-trippar oförändrat.
  **`isSetterField`/`setterName`/`setterField`/`isGlazeType`** (APP_API_2
  steg 8) är fält ↔ setter-tabellen: writern skriver setter-fälten i
  CALL-formen (`okBtn.setText("…");` — regel 4-säkert, en äldre builder
  ser ett okänt anrop) och readern normaliserar tillbaka till en `Prop`,
  så `kFieldMeta`, `applyProp` och egenskapspanelen är OFÖRÄNDRADE.
  Omskrivningen gäller BARA `glaze::`-typer.
  **TABELLEN ÄR TYPMEDVETEN** (fälten är per typ — en namn-global tabell gjorde
  `lista.setChecked(1, true);` till Checkbox/Radios bool-fält): formen är
  **`SetterFieldSpec {cppType, field, subtypes}`**: `cppType == nullptr`
  = alla glaze-typer (samma konvention som `kFieldMeta`), `subtypes` = typen
  OCH dess ättlingar — posterna som bär den är `Panel::clip`/`border`,
  som ärvs av Bar/PropertyGrid/ListBox och transitivt av MenuBar/TopBar/
  StatusBar, och sedan post 21 drag-and-drop-paret
  **`allowDrag`/`allowDrop` mot `glaze::Widget`** (basens egenskaper, alltså
  varje glaze-typ — `subtypes` mot basen och INTE `nullptr`, så en okänd
  användartyp behåller sin anropsform, precis som för clip/border) — och
  sedan post 14 **`toolTip`** i samma form, och sedan FONSTER_OCH_TIMER
  **`cursor`** (`x.setCursor(gfx::PointerShape::Hand);`). Komponenternas
  fält är EXAKTA rader (`glaze::Timer` `interval`/`singleShot`,
  `glaze::TrayIcon` `icon`/`smallIcon`/`templateImage`/`toolTip`/`interval`)
  — basens `subtypes`-rader når dem aldrig, för `kTypeParents` har sedan
  dess TVÅ rötter (`""` = kedjans rot): `glaze::Widget` och
  `glaze::Component` med Timer och TrayIcon. Tooltipen är en TEXT, så
  previewns tr-gren tar den bredvid `text`/`title`: `x.setToolTip(
  glaze::tr(5))` går till `setTextToken` och bevarar tokenet, en literal
  till `setProp`.
  Signaturerna är `isSetterField(cppType, field)` och
  `setterField(cppType, method, field)`; den NAMN-GENERÖSA
  `setterField(method, field)` finns kvar för EXAKT ETT anropsställe —
  previewns setter-fallback, där en användarwidget som ärver en glaze-typ
  har typnamnet `MyPanel` i modellen och faller utanför matrisen.
  **`isOrDerivesFrom`/`isKnownType`** är arvskedjan, en MANUELLT
  UNDERHÅLLEN tabell med TVÅ rötter sedan FONSTER_OCH_TIMER (`glaze::Widget`
  och `glaze::Component`): `builder_core` är ren std, ser bara typnamnet som en
  STRÄNG och har ingen reflektion, så en framtida `class TreeView : public
  Panel` känns INTE igen automatiskt. Det är `preview_gate`s
  register↔palett-kontroll som gör en utebliven klassning SYNLIG i stället
  för tyst — flaggan lovar ingenting den inte kan hålla.
  **`setterFieldSpecs()`** exponerar tabellen ITERERBAR för grindens
  fullständighet, men grindens ORAKEL är en OBEROENDE matris (ur samma tabell
  vore kontrollen cirkulär). **`color` är ett setter-fält BARA på
  `glaze::ColorPicker`** (värdet ÄR hextexten, `farg.setColor("#3366FF");` ⇒
  `Prop{color}`): `Label::setProp("color")` är ett äldre alias för `role`, så
  Labels `setColor(gfx::Color{…})` förblir en Call, och knapparnas
  `setColor(std::optional)` likaså (REQ3). **REQ3:s setter-fält:** Labels
  `elide`/`vAlign`/`buddy`, `iconSize` på Button, ToolButton, ListView och
  DataGrid, tabellernas `fillColumn` och `rowHeaders` (REQ8 7), FilePickers `dialogTitle`, TextAreas
  `acceptsTab` och ritytans `memoryBudget`; REQ7_LAYOUT: Labels `widthGroup`
  (ett gruppnamn, inget nodnamn — `renameNode` rör det inte); REQ7_MARKDOWN:
  `glaze::MarkdownView` ärver Panel i `kTypeParents` (clip/border) och har
  setter-fältet `text`; REQ8 24/25: Labels `haloWidth` och EXAKT Bars `plain` är setter-fält, `setHalo(std::optional<gfx::Color>)` en Call (`argValfriFärg`); REQ8 26: `textSize` på TextInput/TextArea/ListBox/Dropdown, TYPAT (`parseMått`), panelens Raw-rad grindad av `bld::parseLength`; REQ9_TERMINAL: `glaze::Terminal` ärver Panel i `kTypeParents`, åtta setter-fält (program, arguments, workingDirectory, colorMode, colorScheme, scrollback, altAsMeta, textSize — den sista TYPAD som fältens), `onExit`/`onTitleChange` i `isKnownEvent` — bara namn, `builder_core` länkar ingenting.
  `roundtrip_gate` letar råa `.color =`-FÄLTRADER i korpusen; sedan etapp 10
  räknas bara MEDLEMSÅTKOMST (en identifierare före punkten) — en designerare
  i en aggregatliteral, ritytans `{.offset = 0, .color = …}`, är ingen
  fältrad.
  **`builtinMember(id)`** är tabellen över INBYGGDA fönstermedlemmar
  (`statusBar` → `glaze::StatusBar`, `menuBar` → `glaze::MenuBar` och
  `appIcon` → `glaze::AppIcon`, den enda som är SYNLIG som förval i appen):
  namn som grammatiken känner utan en medlemsdeklaration, eftersom
  `glaze::AppWindow` redan har dem.
  En sådan nod bär `builtin = true` och saknar adoptions-SATS — fönstret
  adopterar den i sin konstruktor. Sedan APP_API_2 steg 1 ligger builtins
  FÖRST i nodordningen, så ett barn som `statusBar.adopt(chip);` pekar ut
  byggs EFTER sin förälder i båda trädbyggarna.
  **`parseStringList`** (LISTBOX 2026-08-19) avkodar `items`-radens
  C++-LISTLITERAL (`{"a", "b"}`) FÖRLUSTFRITT — semikolonformatet i
  `setProp("items", …)` kan inte representera ett item som innehåller
  semikolon, ett tomt element eller en escape, och den befintliga splittern
  tappar dessutom ett avslutande tomt element. Strikt delmängd som
  `parseImgCall`: varje element måste vara EN strängliteral, `{}` är en TOM
  lista, och allt annat ⇒ false ⇒ raden förblir OPAK (aldrig degraderad till
  en skräplista).
  **`parseIndexPath`/`indexPathText`** (TRÄDET, post 22) avkodar en
  INDEXSTIG (`{0, 2}`, `{}` = roten) — samma familj: exakt ett yttre `{…}`,
  varje element ett icke-negativt DECIMALT heltal (högst nio siffror, så
  ingen overflowfråga), avslutande komma tillåten. `-1`, `0x2`, `1u` och
  uttryck ⇒ OPAK. Den bor i builder_core för att egenskapspanelens Raw-rad
  (`selectedPath`) och previewns `applyCall` ska dela EN tolkning.
  **`parseSiblingAnchorText`/`siblingAnchorText`/`isSiblingAnchorField`**
  (SYSKONANKARE 2026-08-19) avkodar syskonankarets STRUCT-LITERAL
  (`{"knapp7", 8}`) — fältradens tredje värdeform efter `anchors` och
  `items`. **Ingen ny satsform behövdes:** writerns Prop-gren emitterar
  redan generiskt och readerns `balancedValue` släpper igenom klammern, så
  grammatiken var oförändrad. Formen är id-STRÄNG och inte en pekare
  (`&knapp7`) med flit — `bld::applyProp` har bara widgeten i handen och
  kan aldrig göra ett namn till en pekare in i preview-trädet, så
  pekarformen hade blivit en opak rad och previewn visat FEL GEOMETRI,
  alltså exakt det hård regel 8 kallar ett regelbrott. Alla tre former som
  KOMPILERAR avkodas (`{"id", tal}`, `{"id"}`, `{}`) av samma skäl; allt
  annat ⇒ OPAK rad.
  **NAMN-INVARIANTEN** (`nameState`/`migrateNames`/`renameNode`/
  `validNodeName`, ESCAPES_DOCK_NAMN etapp C 2026-08-19): `Widget::name` ÄR
  objektets namn, speglat. Modellen SA det redan, men grammatiken tar
  fältrader, så en handredigerad `Button1.name = "annat";` återskapade
  exakt buggen `runtimeIdOf` löste. Nu VALIDERAS formen: exakt en rad med
  värdet == nodnamnet är kanoniskt · ingen rad migreras in · **avvikande
  värde eller FLERA rader ⇒ FAIL-SAFE** (tvetydig i identitetsmodellen, och
  regel 4 säger vägra i stället för att gissa). BUILTINS undantas helt —
  `glaze::AppWindow`s ctor sätter deras runtime-namn. `migrateNames`
  INSÄTTER raden först i nodens EGET block (`setProp` hade appendat sist i
  hela listan, alltså efter trädbygget), är idempotent, och är en MEDVETEN
  engångsdiff på orörda filer — tvärtemot tabIndex-prejudikatet, eftersom
  namnet MÅSTE finnas i den byggda appen. `renameNode` byter SEX saker:
  satsernas `target`, adoptionsföräldern (`Adopt::name`), syskonankarnas
  `to`, Labels `buddy` (REQ3 — en `buddy`-rad som inte är en strängliteral ger
  vägran FÖRE all mutation, den kan peka på noden osynligt), fönsteranropen med
  medlemmen som argument (`kMemberRefWindowCalls` — `setAcceptButton`/
  `setCancelButton`; `removeNode` STRYKER dem, annars kompilerar filen inte) OCH
  den kanoniska radens LITERALVÄRDE — missas det sista produceras
  `BtnStatus.name = "Button1";`, den tvetydighet valideringen fäller på.
  Värde-hjälpare: **`parseCppString`/`cppString` — STRIKT och SYMMETRISK
  escape-delmängd** (etapp A): `\"` `\\` `\n` `\t` `\r` avkodas OCH kodas,
  allt annat avvisas (`\x41`, oktalt, `\u`, `\b`, `\f`, `\0`, okänd escape,
  AVSLUTANDE backslash i `"abc\"`, oescapat `"` inuti, RÅA KONTROLLBYTES
  < 0x20). UTF-8 (≥ 0x80) passerar OFÖRÄNDRAD — svensk text är
  normalfallet. Avvisad = OPAK rad: hade `\x41` avkodats till `A` skulle
  emissionen skrivit `"A"` tillbaka, alltså tyst förvrängt användarens rad.
  Round-trippen är byte-exakt åt båda hållen, och `cppString` har därför en
  FELKANAL (`bool cppString(raw, ut)`) — en råemitterad LF hade gett en
  radbrytning mitt i literalen. ANROPSPOLICY: redigering av befintligt
  värde ⇒ avvisa committen; skapande av ny rad ⇒ avbryt med
  statusmeddelande; interna identifierare är INVARIANTER.
  `parseNum`/`numText` (heltalsformen bara inom `long long` — en minnesbudget i
  byte är ett stort tal), `parseAnchorsText`/`anchorsText`, `parseEnumTail`, `parseImgCall`/
  `imgCallText` (bildfältets `glaze::img("lista", "nyckel")` — GLAZE_ASSETS
  steg 11; strikt form, aldrig generell parsning).
  **SPRÅKPAKET (2026-08-22) lade till fyra grammatikfunktioner, alla i
  builder_core:** `splitArgs` (den STRUKTURELLA argumentsplittaren — en
  STACK över `{}` och `()`, inte två räknare, som hade accepterat
  felordnade former som `({)}`; `apply_props` `delaArgument`/`delaKlammer`
  ÄR den nu, och `parseMenuPath` delar med samma regler),
  `parseTrCall`/`trCallText` (`glaze::tr(<decimalt heltal>)` — STRIKT som
  `parseImgCall`; en KONSTANT avvisas, för ren std har ingen reflektion
  och kan aldrig lösa `glaze::str::Ok` till ett tal, och därför bär
  `.glazeui` alltid NUMRET), `classifyTextValue` (Literal/TrId/**TrExpr**/
  Other — TrExpr, 🐛 58, är ETT `glaze::tr(<konstant>)`-anrop: opakt som
  Other, men panelen skyddar det och loggen säger "skriv numret";
  klassificeringen hör till GRAMMATIKEN, grindad headless) och
  `parseMenuPath`/`menuPathText` (den SEGMENTERADE stigen: exakt ett yttre
  `{…}` eller ett ensamt segment, varje segment en strängliteral ELLER
  strikt `glaze::tr(N)`, minst ett segment, och `/` i ett literalsegment
  AVVISAS — utan den regeln hade en omigrerad `"Utseende/Läge"` tyst
  blivit ETT segment som aldrig matchar).
  Bär även **`designLine`** + `kDesignPrefix`/`parseDesignLine`
  (designtaggen — se
  eget avsnitt nedan): raden splittas i nyckel/värde-STRÄNGAR här, men
  avkodas till `glaze::`-typer först i `design_theme` — modellen förblir
  ren std. Ren std — headless.
- **`cpp_reader.h/.cpp`** — läser TILLBAKA builderns egen delmängd:
  `findRegion` (exakt ETT `#pragma region GlazeBuilder`-par; dubbletter/
  oparade/nästlade = fel), `parseHeader` (medlemsdeklarationer + ctor-
  deklaration i regionen — ALDRIG något utanför), `parseGuiCpp`
  (radparsern för grammatiken: setTitle/setClientSize/fältsättning/
  event-lambda i **TVÅ FORMER**/adopt/`}`; nodordning = adopt-
  ordning). **EVENT-SATSEN HAR TVÅ FORMER sedan APP_API_3 B:** regel
  6-fullformen och den PARAMETERLÖSA `btn.onClick = [this] { klick(); };`.
  Den korta MINNS explicit (`Stmt::shortForm`) och skrivs tillbaka KORT —
  ingen expansion, för handlern tar noll argument och `klick(s, e)` hade
  inte kompilerat (att rätta signaturen vore att skriva i användarens
  `.cpp`, regel 5). Matchningen är **TVÅLEDAD**: träffen måste vara radens
  TOPPNIVÅ-tilldelning med validerat vänsterled OCH namnet ett KÄNT event
  (`isKnownEvent` — varje widgets event: widgetarnas tre, livscykelns fyra,
  drag-and-drop-sexan, trädets `onExpand`/`onCollapse`, `onActivate`,
  `onTabClose`, ritytans nio (`onMouseDown`/`onMouseUp`/`onMouseCancel` är basens på varje widget sedan TODO 86), rymdens `onTick`/`onGesture`,
  `onCommit`/`onFocus`/`onFocusLost`, `onCheck`, `onDropDown` och basens
  `onKeyDown`/`onKeyUp`, datagridens `onSelectionChange`, basens `onResize`, Markdown-vyns `onLinkClick`; ett NYTT event står där i
  samma ändring — TODO 57:s läxa). En breddad `find`
  ensam hade träffat sekvenser inuti VÄRDEN (`btn.setText("x = [this] {
  fel(); };");`) och gjort fail-safe av en rad som ska bli en vanlig `Prop`.
  Miss på något led — ELLER en misslyckad kortformsparse på en WIDGET-rad
  (`btn.onClick = [this] { refresh(1); };` är giltig C++ som `Handler` tar
  emot, men inte ett naket parameterlöst anrop) — ⇒ FALL IGENOM till
  `Prop`, aldrig fail-safe. FÖNSTERformen fail-safe:ar däremot vid trasig
  grammatik: den har ingen opak fältväg. KÄND BEGRÄNSNING: ett publikt
  användarfält som faktiskt HETER `onClick` och bär den exakta kortformen
  klassas som en koppling — att skilja dem åt kräver typkännedom
  `builder_core` medvetet inte har. **FULLFORMENS SENDERTYP ÄR FRI** sedan
  FONSTER_OCH_TIMER (B3): `[this](<Sender>* s, <Args>& e) { h(s, e); };` där
  `<Sender>` är texten fram till första `* s, ` och varje led mellan `::` en
  identifierare (`validIdentifier`) — en timers koppling bär `glaze::Timer* s`. Avviker formen
  är det fail-safe som förut. Sendern lagras i `EventBind::sender`/
  `Stmt::sender` (SIST i aggregaten, så befintliga `EventBind{…}` bygger),
  `glaze::Widget` som TOM sträng, och `appendEvent`/`appendWindowEvent`
  kopierar den (Codex-fynd 1 — utan raden skrevs timerns koppling tillbaka
  med `glaze::Widget*` och slutade kompilera). **Okänt FÄLT på känd widget = opak Prop** (inte fel);
  oidentifierbar sats/brace-obalans/skadat stängsel = **fail-safe**:
  `ok=false` + diagnos med fil:rad, inga filer röres. Radslut (LF/CRLF)
  detekteras per fil och bevaras. Ren std — headless.
- **`cpp_writer.h/.cpp`** — kanonisk emittering (`emitRegion`,
  `emitGuiCpp`, mallarna, handler-decl/stub) + **de fyra skriv-
  operationerna** (regel 5 strukturellt — API:t KAN inte uttrycka "ändra/
  radera användar-bytes"): `rewriteRegion` (splasa: enbart bytes mellan
  markörraderna byts; markörerna letas upp i filens AKTUELLA innehåll vid
  varje skrivning — skadade sedan öppningen ⇒ vägra), `rewriteGeneratedFile`
  (*.glazeui — filen ÄR regionen), `appendToUserArea` (direkt efter
  `#pragma endregion`), `appendToCppEnd`, `createFromTemplate` (vägrar
  skriva över). Emissionen är KONTRAKTET mot cpp_reader: ny satsform =
  reader-stöd + round-trip-test i samma ändring. Kopplingen, handlerns
  deklaration och stubben skriver SENDERTYPEN ur modellen (`senderType` —
  tom = `glaze::Widget`). Ren std — headless.
- **`design_theme.h/.cpp`** — designtaggens AVKODNING: strängar →
  `glaze::UI`/`Theme`/`Style` (`decodeDesignLine`/`applyDesignTag`/
  `describeDesign`). Kompileras även av glaze-render (`palette.cpp`-
  precedensen — beror på `glaze`, hör inte hemma i rena builder_core).
  `applyDesignTag` sätter det **APP-GLOBALA** utseendet i ordningen
  **UI → Theme → Style**; ordningen är bärande eftersom `setUI` återställer
  de två andra axlarna till familjens defaults — och det ÄR semantiken för
  en partiell tagg (bara `ui=Minimalistic` ⇒ Minimalistics defaults, aldrig
  verktygets palett på Minimalistics form). Odekodbart värde ⇒ logg + axeln
  behandlas som osatt; ett `style`-index utanför intervallet loggas INNAN
  `styleAt`s tysta klampning tar över.
- **`apply_props.h/.cpp`** — `bld::applyProp` + `bld::applyCall`: modellens
  råa C++-uttryckstext applicerad på en RIKTIG `glaze::Widget`. EN
  sanning, delad av builderns preview och glaze-render (APP_API steg 1).
  `applyProp` känner basfälten direkt, **`name`** (den gamla nyckeln `id` är
  en OPAK rad som vilket okänt fält som helst), `img` via `parseImgCall`,
  **TEXTFÄLTEN `text`/`title`/`toolTip`/`placeholder`/`dialogTitle` genom EN regel,
  `applyTextField`** (🐛 58, också i setter-fallbacken och för komponenterna):
  en literal genom `setProp`, `glaze::tr(N)` genom den VIRTUELLA
  `setTextToken` — ALDRIG `setProp` med den upplösta strängen (previewn får
  aldrig göra MER än C++-vägen; kan typen inte ta tokenet är raden OPAK) — och
  ALLT ANNAT (`glaze::tr(konstant)`, ett uttryck) opakt, målet orört: aldrig
  uttrycket som text. `setTitle` likadant. Övriga värden går till `setProp` som
  **`nyckeltext`** — fältradernas och setter-fallbackarnas EN normalisering, för
  widgetar och komponenter (literal avciteras, enum → sista segmentet gement,
  tal och bool som de står). **`items` TYPAT via
  `parseStringList` → `setItems(vec)` för `glaze::Dropdown`/`glaze::ListBox`**
  (parsefel ⇒ OPAK) och resten via `setProp`. **Labels `widthGroup` TYPAT**
  (REQ7_LAYOUT B3-5, `selectedPath`-formen): bara en STRÄNGLITERAL sätter
  gruppen — en identifierare hade blivit gruppnamnet genom `nyckeltext` — annars
  OPAK och etiketten orörd, också i setter-fallbacken (en egen Label-subklass); `applyCall`
  utför `show`/`hide`/`enable`/`disable`/`setIndeterminate(bool)`/
  `showScrollbars(bool)`. **`applyWindowCall`** (APP_API_2 steg 2) utför de
  NAKNA fönsteranropen på ett `glaze::AppWindow` — `setTitle`/
  `setClientSize`/`setContentSize`/`setMinimumSize`/`setMaximumSize`/`set{Minimum,Maximum,Fixed}ContentSize` (REQ8 21)/
  `setControlBox`/`setMinimizeBox`/`setMaximizeBox`/`setModal`/`setPopup`/`setPopupTakesKeys` (REQ8 27)/
  `setTransparent`/`setTopmost`/`setShowInTaskbar` (de två sista attribut som
  lagras före show — previewns fönster visas aldrig med dem; SYSTEMFÄLTET B8
  för det senare, grindat i `preview_gate` och `roundtrip_gate`). Ett nakent
  `hide();` (dold start) känns INTE igen: det bevaras opakt och utförs
  medvetet inte, för ritbordet ska visa fönstret. Sedan FONSTER_OCH_TIMER
  (B8) också `setLocation` (två heltal), `setUseWaitCursor` (en bool) och en
  ENUM-gren med STRIKTA tabeller genom `enumVärde` — `setChrome`
  (`gfx::Chrome::Native/Custom/None`), `setStartPosition`
  (`glaze::StartPosition::…`), `setWindowState` (`gfx::WindowState::…`), `setSizeToContent` (`glaze::SizeToContent::…`, REQ3)
  och `setSizeToContentMode` (`glaze::SizeToContentMode::GrowAndShrink/GrowOnly`, REQ7_LAYOUT),
  kvalificerat, typkvalificerat eller bart (`using enum`); ett okänt värde är
  opakt. `setChrome(None)` gör previewn SANN: glaze-render ritar utan
  namnlist. **`setAcceptButton`/`setCancelButton`** (REQ2_DIALOGER) tar en
  MEDLEM: `nullptr` nollställer, en identifierare som `win.find<glaze::Button>`
  hittar sätts (`ärIdentifierare`, delad med `argBarn`), allt annat loggas och
  är opakt — så glaze-render ritar standardknappen. `applyCall` känner
  `performClick()` på en `Button` och en `ToolButton` (REQ8 9) men UTFÖR den aldrig (previewn kör ingen
  användarkod). `applyCall` utför `resetCursor()` på varje widget (sättaren
  `setCursor` är setter-fältet `cursor`, basens som tooltipen).
  **`applyComponentStmt`** (B7) är samma kontrakt för en ICKE-VISUELL medlem:
  Prop — `name`, ikonens `icon`/`smallIcon` (`parseBildKälla` = `argImg`, en
  regel), ett tr-id genom komponentens `setTextToken`,
  resten genom dess `setProp`; Call — timerns `start`/`stop` och ikonens
  `show`/`hide`/`openMenu`/`closeMenu` KÄNDA men aldrig utförda (inget når
  OS:et ur en preview), animationens `addFrame`/`setFrame`/`setFrames`,
  `startAnimation`/`stopAnimation` (begäran) och menyn; annars widgetens
  setter-fallback och sedan opakt. Programikonens, systemfältsikonens och
  verktygsknappens (DOCKNING E4a) MENYGREN är EN mall,
  **`applyRootMenuCall<MedGenväg>`** — `{}` som rot, atomisk, `setHandler`
  känd; bara programikonens `addItem` tar ett genvägsargument. Verktygsknappens
  `split` är ett setter-fält. **`applyStmt`** dispatchar EN sats ur
  satslistan till rätt av de två, så båda trädbyggarna kan köra satserna i
  KÄLLORDNING utan att var för sig veta vilka satstyper som är widget-riktade. **Okänd nyckel OCH okänt metodnamn är OPAKA** —
  loggas, ignoreras vid ritning, round-trippas ordagrant. ALDRIG
  fail-safe: en okänd metod är samma sak som ett okänt fält (regel 4).
  **SYSKONANKARNA har en egen gren sedan 2026-08-19**, speglad på
  anchors-grenen: `anchorLeft`/`anchorTop`/`anchorRight`/`anchorBottom`
  avkodas med `parseSiblingAnchorText` och sätts DIREKT på fältet, och
  vägen SLUTAR där — en ogiltig literal faller INTE vidare till `setProp`
  (vars textform är en annan) utan blir en OPAK rad. Grinden är [tests/preview_gate.cpp](../../../tests/preview_gate.cpp).
  **MENYRADEN har egna grenar sedan 2026-08-19** (hård regel 8 — allt ska gå
  i BÅDA vägarna): `addMenu`/`addItem`/`addCheck`/`addRadio`/`addSeparator`/
  `addSubmenu` UTFÖRS, så previewn visar menyn med sina rubriker i stället
  för en tom rad. `setHandler` känns igen men utförs INTE — previewn kör
  aldrig användarkod. Argumentlistan delas av en lokal splitter som
  respekterar strängliteraler, så en menytext med komma inte klyvs.
  **POSTENS TILLSTÅND** (TRAD_LISTOR_BILDER, Codex 1): `setItemIcon`/
  `setItemEnabled`/`setItemChecked(stig, …)` utförs på menyraden, programikonen,
  systemfältsikonen och verktygsknappen genom EN mall, `applyMenuItemState` —
  `{}`-roten bara där den ÄR roten, atomisk, en okänd stig bärarens logg.
  **KOLLEKTIONSVOKABULÄREN** (REGEL8_PREVIEW): `setRows`/`addRow`/`setRow` (PropertyGrid),
  `addItem`/`setItem` (ListBox OCH Dropdown — en gren, två typer),
  `setChecked(int,bool)` (ListBox) och `setColor(gfx::Color{…})` (Label);
  knapparnas `setColor(std::optional<gfx::Color>)` (Button/Checkbox/Radio, REQ3)
  tar EXAKT ett argument i `argValfriFärg`s tre former — en bar `{200, 0, 0}` är opak.
  **TRÄDET** har samma sorts gren med en STIG där listan har ett index:
  `addNode`/`setNode` (2–4 argument: stig, text, bild, färg)/`setNodes`/
  `removeNode`/`moveNode`/`setExpanded`/`setAllowDropInto`, `moveNodes`
  (REQ2_TRAD_LISTOR — STIGLISTAN via `argPathList`: `{}` tom, annars varje
  element en klammerstig, avslutande komma som i C++; `{0}` som lista är
  opak), `setChecked`/`setSelected` (stig + flagga), `setNodeColor` (stig +
  färg) och de argumentlösa `selectAll()`/`clearSelection()`, ATOMISK som de
  andra — stigarna via `parseIndexPath`, texten via `argText` (literal eller
  `glaze::tr(N)`, tokenet bevaras), bilden via `argImg` (`glaze::img(…)` eller
  `{}` → ett `ImageSource`), färgen via `argValfriFärg` (`gfx::Color{…}`,
  `std::nullopt` eller `{}`; en bar `{200, 0, 0}` kompilerar inte och är opak)
  och `setNodes`-aggregatet via den rekursiva `parseTreeLiteral` (1–8 fält i
  `glaze::TreeNode`s ordning — femte släppregeln, sjätte krysset, sjunde
  färgen, åttonde cellerna (REQ8 13; också `addNode`/`setNode`s femte och
  `setCell(stig, kolumn, text)`) — POSITIONELLT eller DESIGNERAT, med eller
  utan typnamnet). Listformen `{T{…}, {…}, …}` är **`parseAggregatLista`**, delad med
  tabellraderna och statusfälten (atomisk: `ut` fylls först när HELA literalen
  gått igenom). `selectedPath`/`multiSelect`/`showChecks` och listvyns
  sorterings-, huvud- och fyllkolumnsfält (ColumnPanels) är SETTER-fält.
  **DOCKNINGEN (DOCKNING E1)** — värdens `dockPanel` med 2 eller 3 argument:
  panelerna är IDENTIFIERARE slagna mot värdens barn på `name` (`argBarn<T>` — samma uppslag för ritytans
  `setChildAnchor(barn, punkt, ankare)`/`clearChildAnchor(barn)`, REQ8 14),
  kanten exakt `glaze::Dock::…` (`argKant`); panelens `setMinimumSize`/
  `setMaximumSize` (två `argMått`), `setFloatingBounds` (fyra, E3) och
  `activate()`. `closable`/`img`/`pinned` (E2)/`floating`/`floatable` (E3)
  är DockPanels setter-fält — previewn utför det flytande tillståndet men
  öppnar aldrig ett fönster (trädet står inte i ett öppet fönster) — och
  `band` (E4b), `text`/`floating`/`floatable` (DOCKNING_DRAG) och `overflow` (REQ8 9) ToolBars,
  med ToolBars `setFloatingPosition` (två `argMått`, atomiskt) och `setAllowedEdges` (en lista med `argKant`, aldrig Fill/None, atomiskt; REQ3) som anrop.
  Satserna gäller barn — de står efter trädbygget.
  **KOLUMNERNA OCH TABELLEN (post 17/24, REQ8 13)** — kolumnerna i EN gren
  för tabellerna OCH trädet (`dynamic_cast` till `glaze::ColumnPanel`):
  `addColumn`/`setColumn`/`setColumns`/`setColumnWidth`; raderna i grenen för
  `glaze::Table`: `addRow`/`setRow`/`setRows`/`setCell`/`setSortKey`/
  `setRowColor` (index + färg), `setCellColor` (index, kolumn, två valfria färger; REQ8 6), ATOMISK som de andra. Rubriken via `argText` (literal eller tr-token),
  bredden via `argKolumnBredd` (`argMått`s tal eller `Column::autoWidth`, REQ8 8), justeringen via `argJustering`
  (EXAKT prefixet `glaze::Align::` — `parseEnumTail` ensam hade tagit en
  `glaze::Dock::Right` som inte kompilerar), cellerna via
  `parseStringList`, bilden via `argImg`, radens färg via `argValfriFärg`
  (sist i `addRow`/`setRow`), nyckeln via `argDecimal` (ett
  `double`, för bytes och tidsstämplar; ett f-suffix tolkas med
  float-precision som i C++, `nan`/`inf` slipper aldrig in), och
  massformerna via `parseColumnsLiteral` (1–4 fält i `ColumnPanel::Column`s
  ordning) och `parseTableRowsLiteral` (0–3 fält i `Table::Row`s ordning —
  celler, bild, färg — positionellt eller designerat, `{}` = tom rad). **RUBRIKENS JUSTERING** (post 52) är kolumnens fjärde
  fält och styckeformens fjärde argument (`addColumn`/`setColumn` tar 1–4
  respektive 2–5), avkodad av `argRubrikJustering`: `glaze::Align::X` med
  exakt prefix, eller C++:s två tomma former för en `std::optional` —
  `std::nullopt` och `{}` — som betyder "som cellerna". Alla tre
  kompilerar (tests/fixtures/calls bevisar det), så alla tre utförs; en
  strängare parser hade gjort lagliga rader opaka. `\n` i rubriken är
  `parseCppString`s escape. Listvyns val och kryss har en egen gren efteråt:
  `setSelected(rad, på)`, `setChecked(rad, på)` (REQ2_TRAD_LISTOR, ListBoxens
  form) och de argumentlösa `selectAll()`/`clearSelection()`. SETTER-fält:
  `selectedIndex`/`multiSelect`/`showChecks` (listvyns), och sorteringens tre
  och HUVUDETS två (`headerLines`, 0 = AUTO, och `headerVAlign`) på BÅDA.
  DATAGRIDDEN (post 24) har sin gren efter listvyns: `setColumnEditor(
  kolumn, glaze::DataGrid::Editor::X)` (EXAKT prefixet, via `argEditor` —
  samma skäl som justeringens), `setColumnChoices(kolumn, {…})`
  (`parseStringList`) och `setCurrentCell(rad, kolumn)`, atomiska som
  tabellgrenen.
  **FILVÄLJARFÄLTET (post 25)** har sin gren efter datagridens:
  `addFilter(namn, mönster)` · `setFilter(index, namn, mönster)` ·
  `setFilters({{namn, mönster}, …})` — namnet via `argText` (literal eller
  `glaze::tr(N)`, tokenet bevaras), mönstren via `argStr`, massformen via
  `parseFiltersLiteral` (EXAKT två fält i `glaze::FileFilter`s ordning — ett
  filter utan mönster betyder ingenting). Atomisk som de andra, och FÖRE
  setter-fallbacken, som annars hade gjort `setFilter(…)` till ett okänt fält
  `filter`. `path` och `mode` är SETTER-fält.
  **FÄRGVÄLJAREN (post 20)** har en TYPAD gren i `applyProp`: `color` med en
  `gfx::Color{…}`-literal (kodens form, `parseColorLiteral` — Labels
  delmängd) går till `ColorPicker::setColor(gfx::Color)`, medan
  hextexten går den vanliga strängvägen till `setProp("color")`. En
  användarwidget som ärver färgväljaren emitteras som Call, och
  setter-fallbacken skickar då `color` till samma gren (`items`-prejudikatet).
  **RITYTAN (CANVAS_2D etapp 10)** har den största grenen, FÖRE
  setter-fallbacken (som annars hade skickat "674000, 6580000" till
  `setProp("center")` utan att pröva formen): `setCenter`/`setZoomRange`/
  `setMapScale`/`setBackground`/`zoomToRect`/`setCamera`, lagren
  (`addLayer`/`setLayer`/`setLayers`/`removeLayer`, `moveLayer` och de sex
  lageregenskaperna — REQ8 2 `setLayerPainter`) och formerna (`addShape`/`setShape`/`setShapes`/
  `removeShape`, med eller utan lager). Ett eget LITERALAVSNITT avkodar
  formerna — varje fabrik i shape.h med överlagringen C++ väljer (punkter
  mot ringar på klammerdjupet; en yta med kontur tar `glaze::Stroke{…}`
  eller `std::nullopt`, en bar klammerlista kompilerar inte till en
  `std::optional`), `Paint::linear/radial` med stopp, `glaze::px/mm/world`
  eller ett naket tal (px), enum med EXAKT typens prefix, `glaze::img` eller
  `{}`, text som literal eller `glaze::tr(N)`, och aggregaten Stroke/
  TextStyle/MarkerStyle/Camera/Rect/Point/PathSegment/GradientStop med
  typnamnet eller bart där parametern har typen, POSITIONELLA eller
  DESIGNERADE — designerarna i DEKLARATIONSORDNING, för fel ordning, ett
  okänt eller upprepat namn och en blandning kompilerar inte i C++20 och
  avvisas här likadant (`aggregatFält`). TextStyle och MarkerStyle bär SIST
  golvet och taket (`minSize`/`maxSize`, mått) och zoomen (`minZoom`/`maxZoom`,
  tal), TextStyle dessutom taket `maxLines`/`maxHeight` (REQ3). ATOMISKT: allt till temporärer,
  ritytan muteras först när varje argument gått igenom. `easeTo` utförs
  MEDVETET inte — en glidning är ett förlopp och ritbordet visar tillstånd —
  och raden bevaras opakt. Talen går genom **`decimalt`** (`parseNum`s grind
  i double med f-suffixets float-precision), som AVVISAR ett oktalt heltal
  (`010`) och allt hexadecimalt — och `argDecimal` (tabellens sorteringsnyckel)
  går genom den, så samma regel gäller där sedan dess.
  **TEXTDELAR OCH BILDEN PÅ FYRA HÖRN (CANVAS_TEXT_OCH_BILD)** går genom
  samma gren: `Shape::image` med 5–6 argument är fyrhörningen (fyra punkter,
  källan, valfri `ImageStyle` — `parseBildStil`, REQ8 3; ett bart tal avvisas
  som i C++ —, en ogiltig fyrhörning ritar ingenting som i appen), och `text`/`text3`s andra argument väljs med
  **`parseEtikettText`** som C++:s överlagring gör: `{"abc"}`, en literal och
  `glaze::tr(N)` är TEXT; en lista med `glaze::TextSpan{…}` eller med
  DESIGNERADE delar (`{{.text = "a", .bold = true}, …}`) är DELAR
  (`parseTextDel` — `text`, `color`, `bold`, `italic` i deklarationsordning,
  positionellt eller designerat, `valfri` för de valfria fälten); och `{}`
  samt en lista med EN bar del `{{"abc"}}` är TVETYDIGA i C++ och blir därför
  OPAKA rader här (`EtikettText::Fel`) — aldrig en gissning. `preview_gate`s
  fyrhörnings- och delfall.
  **RYMDEN (CANVAS_3D etapp 12)** går genom SAMMA gren och samma
  `parseForm`: fabrikerna `line3`/`polyline3`/`polygon3`/`text3`/`marker3`/
  `points3`/`mesh`/`box`/`sphere`/`cylinder`/`tube`/`instances`/`screenPass`
  (instansernas PROTOTYP är själv en form, avkodad rekursivt; ett utelämnat
  sista argument ger fabrikens förval genom att fabriken anropas med färre
  argument, så `gfx::kCylinderSides` & co skrivs aldrig av), `Mesh::
  triangles`/`indexed` (ramverkets fabrik bygger meshen — en felaktig blir
  ingen mesh + logg, som i appen) och ett eget LITERALAVSNITT för rymdens
  aggregat i DEKLARATIONSORDNING: Point3/Vec3/Float3 (`parseXyz` — utelämnade
  fält är 0, aggregatets förval och inte den omgivande medlemmens), Box3,
  IndexRange (`parseU32`: 0–2³²−1 eller `glaze::kRangeToEnd`; negativt,
  decimalt, oktalt och suffix avvisas — narrowing i en klammerlista),
  ColorRamp, MeshStyle (fjorton fält — `contrast`/`brightness` sist), PointStyle, PassStyle, Placement,
  Instance, Camera3D, Lighting, glaze::RenderState (`gfx::RenderState` är en
  ANNAN typ och avvisas; DepthTest/Blend/Cull tar båda prefixen, för
  glaze-namnen är alias) och View. Ritytans nya Call-satser: `setLighting`,
  `setLayerPlacement`, `setLayerRenderState`, `setLayerViews`,
  `addView`/`setView`/`setViews`/`removeView`, `setElevationRange`,
  `setDistanceRange`, `setTicking` (ingen `setProp`-nyckel, alltså Call och
  inte setter-fält) och `zoomToBox` (`zoomToRect`s tvilling — väntar på ytan
  som den; planen nämnde den inte, men den är tillstånd och inte en
  handling). **KAMERAN ÄR ÖVERLAGRAD** och `parseKameraArg` väljer som C++
  ([over.ics.list]): `glaze::Camera{…}` och `glaze::Camera3D{…}`/
  `Camera3D::lookFrom(…)` efter typen, en bar DESIGNERAD lista efter
  NAMNEN (fälten är disjunkta), och en bar POSITIONELL lista eller `{}` är
  TVETYDIG och avvisas — den kompilerar inte, och den gamla 2D-raden
  `setCamera({{1, 2}, 4})` blev därmed opak i och med etapp 3:s överlagring.
  Planen sa "typen krävd"; en bar designerad lista kompilerar och väljer
  rätt överlagring, så den utförs (camera.h:s eget exempel är en sådan).
  Inne i en VY finns ingen överlagring, så där är en bar positionell kamera
  laglig och utförs. Programmen är NAMN: previewn registrerar inga
  (`addProgram` är koddata), så ramverkets reserv ritar (det inbyggda
  programmet, passets `fallback`) och loggar en gång per rityta och namn.
  `orbitBy`/`panBy`/`dollyBy`/`easeTo`/`renderToBitmap` är handlingar och
  bevaras opakt. `preview_gate`s `fall3D` mäter tillståndet form för form
  med avslagen, och `fallCallsRityta` kör VARJE karta-sats i
  `tests/fixtures/calls.*` — en rad en senare etapp lägger där utan en gren
  här fäller den.
  Grenarna ligger FÖRE setter-fallbacken:
  `setChecked` ger fältnamnet `checked`, som fallbacken annars hade skickat
  till `ListBox::setProp` som en skräpsträng.
  **DATUMVÄLJAREN (post 26)** har en TYPAD gren i `applyCall`:
  `setDate`/`setMinDate`/`setMaxDate` med EXAKT tre heltal (`argInt`) går
  till sättarna som tar tre heltal — ett datum som inte finns loggas där
  och ändrar ingenting. Grenen ligger FÖRE setter-fallbacken, som annars
  hade gjort tre argument till ett okänt fält; tre heltal är ingen
  setter-fältform, så ingen av de tre står i `kSetterFields`.
  **FAS1_B_WIDGETS** (post 63/54), grenar FÖRE setter-fallbacken: radvalet på
  ListBox, ListView och DataGrid genom EN mall, **`applyRowSelectionCall<MedKryss>`**
  — `setSelected(rad, på)`, `setChecked(rad, på)` där listan bär kryss och
  `selectAll()`/`clearSelection()`, atomiskt; `nullopt` = inget av orden —,
  `setTime(timme, minut)` (EXAKT två heltal), flikarnas
  `addPage()`/`addPage(text)`/`setPage(index, text)`/`setPages({…})` och
  statusradens `addPanel`/`setPanel`/`setPanelText`/`setPanels` — texterna via
  `argText` (tokenet bevaras), panellistan via `parseStatusPanelsLiteral`
  (positionellt, `glaze::StatusPanel{…}` eller designerat I FÄLTORDNING). Setter-
  fälten med panelrader: Checkbox `threeState`/`checkState` (`glaze::CheckState::`),
  ListBox `multiSelect`, DataGrid `selectionMode` (`glaze::DataGrid::SelectionMode::`)/
  `multiSelect`, ColorPicker `allowAlpha`, DatePicker `showTime` (`fallFas1B`).
  **MARKDOWN-VYN** (REQ7_MARKDOWN) har en gren FÖRE setter-fallbacken: `setImage(namn, bild)` med EXAKT två
  argument (`argStr` + `argImg` — `glaze::img(…)` eller `{}`, som tar bort) och `scrollToAnchor("…")` UTFÖRS,
  atomiskt, annars logg + OPAK; ankaret väntar till previewns första ritning, som i appen (`fallMarkdown`).
  **MARKERINGEN (0.1.2)** har en gren för de fem som bär den — `TextInput`,
  `TextArea`, `SpinBox`, `DatePicker`, `FilePicker`: `selectAll()` (inga
  argument) och `setSelection(b, e)` (EXAKT två strikta heltal) UTFÖRS, och
  markeringen sätts på previewns widget men syns inte (ett fält ritar den
  bara med fokus) — raden är alltså KÄND och inte opak. Atomisk; listvyns
  `selectAll` (raderna) har sin egen gren före. `preview_gate`s
  `fallCommitFokusMarkering`. **ÅNGRA/GÖR OM (TEXTFALTEN)** har en gren efter
  markeringens för de två fält som bär historiken (`TextInput`, `TextArea` —
  kompositerna har inga): `undo()`/`redo()` utan argument UTFÖRS (historiken
  är tom i previewn som i `initializeComponent`, så inget syns), med argument
  ⇒ logg + OPAK. GlazeBuilder emitterar dem aldrig. Lösenordsläget är
  SETTER-fältet **`password`** på EXAKT `glaze::TextInput` (`kSetterFields`,
  panelraden Bool i `kFieldMeta` bredvid `readOnly`) — previewn ritar fältet
  maskerat via `setProp("password")`. `preview_gate`s `fallLosenordAngra`.
  **REQ2_DIALOGER** lade till setter-fälten `placeholder` (Text) och
  `maxLength` (Integer) på TextInput OCH TextArea och Buttons `dialogResult`
  (Enum med prefixet `glaze::DialogResult::`), med panelrader; `preview_gate`s
  `fallDialogerOchFalt`.
  **⚠ MENYGRENEN ÄR INGET PREJUDIKAT.** Den gör tom sträng vid parsfel och
  returnerar ändå FRAMGÅNG — en oparsbar rad hade alltså gett en meny med
  PÅHITTAD tom rubrik, alltså ett tillstånd som inte finns någonstans. De
  nya grenarna är **ATOMISKA**, utskrivet som krav: exakt argumentantal
  kontrolleras, ALLA argument parsas till temporärer, och widgeten muteras
  först när allt lyckats — annars logg + oförändrat tillstånd. Beteendet är
  mätt i `preview_gate` på WIDGETENS tillstånd och inte på modellen:
  `applyCall` får ingen muterbar modell alls, så ett modelltest är grönt per
  konstruktion medan previewn lagt till en halvparsad rad.
  De två LITERALPARSRARNA (`setRows`-radlistan och `gfx::Color{…}`) är
  strikta delmängder i samma familj som `parseStringList`/`parseImgCall`.
  `gfx::Color` bär BYTEkanaler, så delmängden är EXAKT specificerad — NOLL
  TILL FYRA DECIMALA heltal i 0–255 (sedan CANVAS_2D etapp 10; ett
  avslutande komma efter minst en kanal) och inget annat. Färre än fyra är
  laglig C++ — aggregatet har standardvärden — och de utelämnade kanalerna
  läses ur `gfx::Color{}` självt (r = g = b = 0, a = 255). En allmän floatparser
  hade accepterat `-1`, `256` och `12.5`, och previewn hade tolkat raden
  annorlunda än C++ gör. **Känd avvikelse:** `parseStriktInt` läser `010`
  som tio, C++ som åtta (oktalt) — i ALLA heltalsargument, inte bara färgen
  (`decimalt`, ritytans och sorteringsnyckelns tal, avvisar det). **`applyCall(setColor)` anropar ENDAST
  `Label::setColor` och rör ALDRIG rollen** — textvägen
  `setProp("color", "#rrggbb")` gör TVÅ operationer (färg + `Custom`),
  C++-settern gör EN. Satte grenen båda hade previewn visat rött medan den
  kompilerade appen använder temats textfärg.
- **`preview_tree.h/.cpp`** — `bld::drawPreviewTree`: PREVIEWVANDRINGEN, en
  fri funktion så den grindas headless — `preview_gate` mäter den med en
  test-renderer som loggar `applyClip` (`pushClip`/`popClip` är icke-virtuella,
  så bara klippstacken visar ordningen). Den håller `AppWindow::drawTree`s
  kontrakt: `drawAfterChildren`, SYNLIGHETSVAKTEN, DEN TOMMA REKTEN och
  `frameStep` (REQ9 6), `pushClip(w.clipRect(c), w.clipRadius(c))` (samma `Widget`-par;
  `KlippRenderer` loggar radierna, `"klipp r=12 r=4"` — maskerna YTTERSTA FÖRST,
  en rektangulär nivå tar ingen plats) och `frontChild()` SIST (DOCKNING E2, annars
  hamnar en öppen overlay under huvudytan; `preview_gate` fall 8).
- **`component_tree.h/.cpp`** (FONSTER_OCH_TIMER B6) — `bld::buildComponents(
  model, origin)` → `std::vector<PreviewComponent {node, object}>`: de
  ICKE-VISUELLA medlemmarna (varken inbyggda eller adopterade) skapade ur
  komponentregistret och utförda med sina satser i KÄLLORDNING genom
  `applyComponentStmt`. PREVIEWKONTRAKTET (Codex-fynd 10): bara en typ
  registrerad som `PreviewForm::Inert` skapas; `StandIn` och en okänd typ
  loggas EN gång per nod och utförs inte — raderna står kvar i modellen
  (regel 4). Delad av glaze-render (`RenderWindow::components_`) och
  builderns `rebuildPreview` (`previewComponents_`); ingenting ritar dem —
  komponentfacket är fas 2 (TODO 70).
- **`window_tree.h/.cpp`** — `bld::buildWindowTree(win, model, origin)`:
  modellen → trädet i ett RIKTIGT `AppWindow` (inbyggda först, sedan
  adopt-ordningen, satserna i källordning) och widgeten per modellnod
  tillbaka. Delad av glaze-render och `tests/pixel_gate.cpp` (en kopia i
  grinden hade mätt kopian och inte verktyget). Builderns egen canvas bygger INTE
  härigenom (den bygger mot en Panel i editorfönstret, se `preview_tree`).
- **`palette.h/.cpp`** — `bld::kPalette`/`kPaletteCount`/`paletteByCpp`
  (utbruten ur BuilderWindow — GLAZE_RENDER steg 1): VERKTYGSLÅDANS METADATA,
  C++-klassnamn ↔ verktygslåde-namn + default-storlek. Den SKAPAR inga widgets
  sedan 🐛 60: trädbyggarna frågar registret (`makeNodeWidget`), och kolumnen
  med registrets typnamn är borttagen. Ny ramverkstyp = en rad här.
  **Default-storleken är 0 där widgeten AUTOSIZAR** (axelregeln,
  2026-08-12): en placerad Button/TextInput/Dropdown/Separator/Slider/
  Progress ska visa STILENS mått, annars ser man aldrig vad ett stilbyte
  gör. Kvar med tal: `Panel` och `Scrollbar` (autosizar med flit inte) och
  `Picture` — naturmåttet av ett OSATT bildhandtag är `{0,0}`, så utan tal
  blir den placerade bilden osynlig. **0 gäller PER AXEL**, så en typ kan
  ha default på ena axeln och auto på den andra.
  **Komponentlistan (2026-10-07):** `ToolBar` (200 × auto — StatusBar-formen,
  dockad remsa som äger sin höjd), `ToolButton` (auto × auto), `GroupBox`
  (200 × 120 — den autosizar ur sina barn, men en NYPLACERAD ram har inga,
  och en yta man ska släppa saker i behöver ett mått; Panel-formen) och
  `SpinBox` (auto × auto — bredden ur `spinDefaultW`, höjden är fältets) och
  `TreeView` (160 × 120 — ListBox-formen: trädet autosizar med flit inte, och
  den ärver Panel genom `glaze::ColumnPanel` i `kTypeParents`, alltså
  clip/border) och `ListView` (240 × 140 — tabellen autosizar med flit inte; i
  `kTypeParents` ärver den den OREGISTRERADE basen `glaze::Table`, som ärver
  den oregistrerade `glaze::ColumnPanel` och den Panel — kedjan ska vara
  hel även för en typ paletten aldrig visar), `DataGrid` (280 × 140 —
  listvyns skäl och arvskedja, något bredare: en datagrid har oftast en
  kolumn till), `Splitter` (300 × 200 — en
  placeringsyta, autosizar med flit inte; en nyplacerad delare visar sitt
  skikt även utan rutor), `DockHost` (400 × 300, samma skäl) och
  `DockPanel` (auto × auto — `dockDefaultSize`, och den dockar Left ur
  konstruktorn). Alla tre ärver `glaze::Widget` i `kTypeParents` — INTE
  Panel, så de har inga clip/border-fält. FLIKARNA (post 19): `TabControl`
  (300 × 200 — en placeringsyta som delaren, ärver `glaze::Widget`;
  setter-fälten `selectedIndex` och `closable`) och `TabPage` (200 × 120 —
  utanför en flikkontroll är sidan en panel, inne i en placeras den av den;
  ärver `glaze::Panel` och därmed clip/border, och sidans `text` är FLIKENS
  rubrik). Sidorna uttrycks med trädbyggets `adopt` och ingen kollektion,
  och `preview_gate`s `fallFlikar` kör hela kedjan i källordning.
  FILVÄLJARFÄLTET (post 25): `FilePicker` (auto × auto — talfältets form,
  bredden ur `filePickerDefaultW`, höjden ur fältet; ärver `glaze::Widget`,
  setter-fälten `path` (Text) och `mode` (Enum `Open;Save;Folder` med
  prefixet `glaze::FileDialogMode::`), filtren är Call-satser och grindas i
  `preview_gate`s `fallFilvaljarfalt`). FÄRGVÄLJAREN (post 20):
  `ColorPicker` (auto × auto — bredden ur `colorPickerDefaultW`, höjden
  fältets; ärver `glaze::Widget`, setter-fältet `color` som Text — hextexten
  — och `preview_gate`s `fallFargvaljare` kör båda formerna). DATUMVÄLJAREN
  (post 26): `DatePicker` (auto × auto — bredden ur `dateDefaultW`, höjden
  fältets; ärver `glaze::Widget`, inga setter-fält — datumen är Call-satser
  med tre heltal, och `preview_gate`s `fallDatumvaljare` kör dem i
  källordning — gränserna före datumet och en vidgad gräns efter). DET
  FLERRADIGA FÄLTET (post 23): `TextArea` (auto × auto — `textAreaDefaultW` ×
  `textAreaRows` rader; ärver `glaze::Panel` och därmed clip/border,
  setter-fälten `text` (Text — en radbrytning är en vanlig `\n` i literalen,
  som `parseCppString`/`cppString` redan bär åt båda hållen), `readOnly` och
  `wrap` (Bool — samma namn som Labels), och `preview_gate`s `fallFlerradigt`
  kör kedjan med radbrytningar och citattecken). Panelens textrad är EN rad,
  så en flerradig text redigeras bekvämast i koden — editorns yta, fas 2.
  MARKDOWN-VYN (REQ7_MARKDOWN): `MarkdownView` (300 × 200 — ritytans skäl: en nyplacerad vy har ingen text och
  ska rulla; ärver `glaze::Panel`, setter-fältet `text` som Text, samma enradiga panelrad som TextAreas).
  RITYTAN (CANVAS_2D etapp 7 och 10): `Canvas` (300 × 200 — ytan autosizar ur `canvasDefaultW/H`, men en placerad yta
  ska ha ett mått man ser och drar i; ärver `glaze::Widget`, setter-fälten `gridSize`/`zoom`/`bearing`/`pitch`/
  `maxPitch` och vridsnäppets `rotationSnap`/`rotationSnapTolerance` (TODO 53) (Number), `border`/`flat` (REQ8
  4)/`yUp`/`pannable`/`zoomable`/`rotatable`/ `pitchable`/`inertia`/`labelFade` (Bool) och sedan CANVAS_3D etapp 12
  `multisample` (Integer) och `depth`/`orbitTumble` (Bool), REQ3:s `memoryBudget` som Number och INTE Integer (Integer
  går genom `int` och spricker över 2 GiB; texten round-trippar ordagrant) och REQ9_INMATNING:s `inputMode` (Text;
  `orbitPan` är RIVET — flygläget är två `setBinding`-rader). Bindningarna och vändningarna är Call-satser i
  `applyCall`s ritytegren med `parseKälla`/`parseHandling`/`parseRiktning`/`parseEnhet` och **`argMods`** (`0` eller
  `gfx::kModX` med `|`, inget annat uttryck); basens och fönstrets `setWheelInvert`/ `clearWheelInvert` står bredvid
  `resetCursor` respektive i `applyWindowCall`, och `inputMode` går den typade vägen som `widthGroup` (bara en
  strängliteral). — `border` är ritytans egen, den är ingen Panel; `focusable` har ingen sättare och står därför inte
  där). Galleriets grupp 27 (`tests/fixtures/gallery.*`) är rymdens korpus för `pixel_gate`.
  `setBackground(gfx::Color{…})`, `setCenter`/`setZoomRange`/`setMapScale`
  (två argument), kameran, lagren och formlistan är Call-satser som previewn
  UTFÖR sedan etapp 10 (se `apply_props` nedan). `fixedPane` är ett Enum-fält
  (`First;Second` med prefixet `glaze::Splitter::Pane::`), `layout` ett
  textfält; en ny `layout`-rad hamnar SIST i satslistan (`setProp`
  appendar), alltså efter trädbygget, där den måste stå för att den
  kompilerade appen ska hitta panelerna (dock_host.h). En ny typ
  kräver TRE rader i den här mappen utöver paletten: arvstabellen
  `kTypeParents` och `kSetterFields` i window_model.cpp, och `kFieldMeta` i
  builder_window.cpp — plus kravmatrisen i `preview_gate`, som är skriven
  OBEROENDE med flit och fäller bygget om de glider isär.
- **`standin_widget.h/.cpp`** — `StandInWidget`: ruta i rätt storlek med
  klassnamnet centrerat för en klass REGISTRET inte känner, och
  **`bld::makeNodeWidget(cppType, &standIn)`** — EN skapelseväg för en
  modellnod (🐛 60): `glaze::makeWidgetOfClass` med nodens klassnamn (EXAKT,
  med namnrymden — ramverkets och en egen registrerad widget i ett bygge som
  länkar den, `GLAZE_TOOLS`), annars stand-in. window_tree, `rebuildPreview`
  och preview_gate går genom den. EGEN widget (fallback-vägen): överrider
  `draw()` själv, ritar via `c.r` + `c.theme->colors` + `c.font`. Registreras
  ALDRIG. **MÅTTET är LOGISKA px** — minimimåttet (100×40) går ut rakt av;
  utanför libglaze når `measure_gate` den inte, så `tests/preview_gate.cpp`
  grindar den i skala 1 och 2 (ett glömt enhetsbyte hade dubblat den på HiDPI).
- **`asset_panel.h/.cpp`** — asset-läget (GLAZE_ASSETS steg 10):
  `bld::AssetDoc` (den redigerade appens assets.bin i minnet — letas
  uppåt från .h-katalogen; `load`/`save` (atomär via `glzassets::pack`)/
  `allKeys` för img-choices) + `AssetWindow` (lista med export/ta bort
  per rad — fast widget-POOL som växer men aldrig krymper, trädet saknar
  barn-borttagning med flit; ny lista + lägg-till-PNG via textfält —
  ramverkets filväljardialog finns sedan post 18 men är inte inkopplad här,
  det hör till fas 2; @2x är husstandard). Komponerar BARA
  ramverkswidgets (Erik-regeln: alla widgets bor i glaze).
- **`builder_window.h/.cpp`** — editorn: verktygslåda (vänster, ur
  **palett-tabellen** `kPalette`: C++-klassnamn ↔ namn ↔ default-storlek;
  ett tr-anrop med konstant visas rått och skrivskyddat i panelen och
  spärras i commiten som ett tr-id, 🐛 58), egenskapspanel (höger — **PropertyGrid**:
  rader ur markerad nods props via **`kFieldMeta`**-vokabulären (kind
  Text/Number/Bool/Enum/Raw styr display-avkodning OCH omkodning till
  C++-text; okänt fält = läs-bar opak rad — aldrig rå uttryckstext via
  gridden); img-fältet får DYNAMISKA choices ur `assetDoc_.allKeys()`;
  **en publik egenskap i ramverket MÅSTE in i `kFieldMeta` OCH i
  `kSetterFields` (window_model.cpp)** — annars exponerar ramverket något
  editorn inte modellerar, vilket är ett brutet WYSIWYG-kontrakt: en äldre
  builder bevarar visserligen raden opakt (regel 4), men den NYA måste
  kunna den. Drag-and-drop-paret (post 21) står som `{nullptr, "allowDrag"/
  "allowDrop", Bool}` — basens egenskaper, alltså varje typ, och Bool och
  inte Raw eftersom värdet bara kan vara `true`/`false`. Tooltipen
  (post 14) står som `{nullptr, "toolTip", Text}` — samma textrad som
  `text`, så ett tr-värde visas UPPLÖST och skrivskyddat med id:t bredvid,
  precis som en översatt `text`. Pekaren (FONSTER_OCH_TIMER) står som
  `{nullptr, "cursor", Enum}` med de elva formerna och prefixet
  `gfx::PointerShape::`. **`previewComponents_`** byggs om SIST i
  `rebuildPreview` med `bld::buildComponents` — de gamla rivs först, så ingen
  timer eller ikon finns i två exemplar. Kolumnhuvudet (post 52) står på
  `glaze::ListView` OCH `glaze::DataGrid` — `headerLines` (Integer) och
  `headerVAlign` (Enum `Top;Center;Bottom`, prefix `glaze::VAlign::`) — och
  dropdownens `align` (Enum, Labels och TextInputs rad) i båda tabellerna;
  listvyns `showChecks` (Bool, REQ2_TRAD_LISTOR) står bredvid ListBoxens och
  trädets. `glaze::Dropdown` och `glaze::ListBox` bär `selectedIndex`
  (Integer), ListBox också `showChecks` (Bool) och **`items` som Raw-rad**,
  där Raw-grenen i `commitPropEdit` validerar med `bld::parseStringList` —
  samma grind som `anchors`, av samma skäl: `.glazeui` får aldrig bära text
  som inte kompilerar. REQ3:s rader: Labels `elide` (Bool)/`vAlign` (Enum,
  `glaze::VAlign::`)/`buddy` (Text), `iconSize` (Number) på Button, ToolButton,
  ListView och DataGrid, `fillColumn` (Integer), `dialogTitle` (Text),
  `acceptsTab` (Bool) och `memoryBudget` (Number), och REQ7_LAYOUT:s `widthGroup`
  (Text); live-commit → `node.set` → `rebuildPreview` — raderna byggs ALDRIG om
  under redigering, setRows dödar editorn), canvas i mitten.
  **SYSKONANKARNAS fyra Raw-rader** (2026-08-19) validerar med
  `bld::parseSiblingAnchorText` i `commitPropEdit` — samma grind som
  `anchors` och `items`, av samma skäl. De ligger MEDVETET utanför
  `kSetterFields`: kontraktet i `widget.h` säger att LAYOUT-fälten är RÅA.
  **KOPPLINGSLÄGET** (steg 6) är den riktiga vägen dit: verktygsradens
  "Koppla"-växel (eller Ctrl+L — Ctrl och inte en naken bokstav, för
  `event()` körs FÖRE routingen till fokuserad widget och hade ätit
  tecknet mitt i en egenskapsredigering) slår om musen till ett EGET läge
  där ett drag betyder "koppla" och inte "flytta"; därför ritas ingen
  markeringsram där (kopplingsLINJERNA ritas — de är information och
  ingen affordans). Släpp över en widget öppnar en riktig `glaze::Menu`
  med de fyra kanterna, och **ogiltiga mål visas INAKTIVERADE med en
  förklaring** i stället för att tyst uteslutas: riktningsregeln
  ("tidigare syskon" = adopt-/z-ordningen) är inget användaren kan gissa
  sig till. Menyn är en MEDLEM (`popupAt` är icke-blockerande, en lokal
  `Menu` hade förstörts direkt) och dess handlers bär **id-STRÄNGAR och
  inte nodindex** — handlern körs senare, och listan kan ha ändrats.
  Avståndet defaultar till widgetarnas FAKTISKA avstånd, avrundat, så
  kopplingen inte flyttar något i det ögonblick den skapas.
  **`tabIndex`-raden är SYNTETISERAD** (del C, 2026-08-12): den visas för
  markerad widget även när noden saknar propen — annars går ordningen inte
  att sätta — men skrivs till modellen först när användaren faktiskt anger
  ett värde. **Regel 4/5: buildern får ALDRIG börja emittera `tabIndex`-
  rader för widgets ingen rört**, då hade varje spara diffat. Värdet visas
  som `auto (n)` och PÅSTÅR inte att `n` är runtimes siffra: buildern ser
  bara modellens noder (`modelDfsIndex`), medan runtime-listan bara
  innehåller de FOKUSERBARA och dessutom widgets som skapas inuti andra
  widgets (PropertyGrids editorer). Talet duger för att välja ordning, inte
  som facit. `placeAt` skriver storleken **PER AXEL** (en `height = 0`-rad
  dödar autosizen). **DEFAULTTEXTEN skrivs bara där typen HAR en text**: en
  textlös typ (Slider, Separator, Bar …) hade annars fått en `text`-Prop som
  writern emitterar som en FÄLTRAD som inte kompilerar, så settertabellen
  frågas (`bld::isSetterField(typ, "text")`), med TextInput kvar som
  medvetet undantag (ToolBar har en text sedan DOCKNING_DRAG — det flytande
  fönstrets titel — och får palettnamnet). Preview = **riktigt
  adopterat träd** under en ägarlös rot-panel (`previewRoot_`), byggt ur
  registret + `setProp`/direkta basfält — dockade barn ser sina syskon.
  **En träff i en KOMPOSITS INREDNING markerar kompositen** (post 12,
  2026-10-07): `nodeIndexFor` vandrar `parent()` uppåt till närmaste
  MODELLERADE nod, för talfältets inre `TextInput` är ett riktigt barn i
  previewträdet men ingen nod i modellen (SpinBox-konstruktorn skapar det).
  Utan uppvandringen gick ett talfält bara att markera via stegaren; samma
  väg används av kopplingsdraget.
  Placera/markera/dra (drag uppdaterar de offsets nodens ANKARE pekar ut;
  dockade flyttas inte)/**ÄNDRA STORLEK i hörnhandtagen**/dubbelklick (kopplar `<id>Clicked`, appendas vid
  spara)/Delete (tar bort nod + delträd; varnar att handlers lever kvar —
  också numpadens Delete med NumLock av: `event()`-kroken ser den OVIKTADE
  `KP_Delete` och jämför därför via `gfx::navigationKey`, TODO-post 31)/
  Ctrl+S (splasa + total `.glazeui`-omskrivning + pending-handler-appends
  med substring-dubblettvakt). **SPRÅKBYTET bygger om previewn**
  (`onLanguageChanged()`, 2026-10-07): previewträdet är ÄGARLÖST och når
  aldrig `Widget::languageChanged`, så det en widget härleder ur språket
  och lagrar (talfältets decimaltecken) hade annars stått kvar i det gamla
  språket när designtaggens `lang=` sätts i `onLoad`. Öppning via valfri triplett-fil
  (`.h`/`.cpp`/`.glazeui` → normaliseras till `.h` av `headerPathFor`; syskon
  per konvention); saknad `.h` → "nytt fönster"-mall (skapas först vid spara
  + status-påminnelse om CMake-raden — buildern rör aldrig CMakeLists).
  **Fail-safe-läget**: canvasen visar diagnosen (och vid spar-vägran
  koden att klistra in); allt redigerande avstängt.
- **`main.cpp`** — `GlazeBuilder [fönster.h/.cpp/.glazeui]`; ingen fil → nytt
  tomt fönster. `BuilderWindow win(...); return win.run();`.

## Designtaggen — fönstrets utseende, för VERKTYGEN

`*.glazeui` kan bära en **designrad**:

```
// GENERERAD av GlazeBuilder — redigera i GlazeBuilder, inte här.
// glazeui-design: ui=Slate theme=slate(Teal) style=0
#include "start_window.h"
```

Grammatiken:

```
"// glazeui-design:" WS ( nyckel "=" värde WS* )*
nyckel  = [a-z]+
värde   = alla tecken UTOM blanksteg
ui=Glaze|Slate|Minimalistic · theme=<familj>(<namn>) · style=<index>
lang=<BCP-47-tagg>   sv · en · nb · fi — RITBORDETS språk (SPRÅKPAKET §5d)
```

- Det är en **KOMMENTAR och aldrig en sats.** Därför är "används EJ vid
  kompilering och körning" sant per KONSTRUKTION, och en ÄLDRE GlazeBuilder
  hoppar bara över raden i stället för att tvingas i fail-safe av en okänd
  sats. Ingen ny satsform ⇒ grammatikens kompilerade del är orörd.
- **Värden får inte innehålla blanksteg** — raden splittas på whitespace, så
  `theme=slate( Teal )` är tre fält och inte ett värde.
- `theme` bär sin FAMILJ i värdet därför att namnrymden är per UI:
  `slate(Neutral)` och `minimalistic(Neutral)` är två olika paletter. Att
  korsa familjer är tillåtet och användarens val — namnrymden hålls isär i
  UPPSLAGET (`themeAt(ui, i)`), inte av en vakt.
- Okänd NYCKEL ignoreras vid avkodning men **skrivs tillbaka** (regel 4).
  Dubblerad nyckel i raden ⇒ raden avkodas inte alls (loggas, bevaras).
  **TVÅ designrader i filen ⇒ fail-safe** — tvetydig metadata är ett
  strukturfel, och regel 4 säger vägra i stället för "sista vinner".
- **Kontraktet är kanonisk NORMALISERING, inte ordagrann round-trip.**
  `cpp_reader` fångar raden var den än står (prefixkontrollen ligger direkt
  efter `trim`, före både `St::After`-grenen och kommentarskippen — annars
  hade radens POSITION avgjort om taggen hittas, och nästa spara raderat
  den tyst). `cpp_writer` emitterar den alltid efter GENERERAD-raden och
  före `#include`, och bara när modellen bär en — så tagglösa tripletter
  får noll diff. Det som bevaras ordagrant är VÄRDENA, inte indrag eller
  radposition. `parseHeader` fångar den INTE: taggen bor i `.glazeui`.
- **Buildern sätter utseendet i `onLoad`, inte i `openFile()`** —
  `openFile()` körs i konstruktorn och `onLoad` senare, så builderns egna
  Slate/Teal hade annars skrivit över taggen. Ordningen där är: builderns
  default först, sedan taggen, och statusraden SIST (konstruktorns egna
  meddelanden sätts före `onLoad` och skulle annars skriva över den).
- **Ritvägen är OFÖRÄNDRAD.** `canvasCtx()` hämtar sin Ctx ur `makeCtx()`,
  så previewn byter utseende utan att en rad i `frame()` rörs — och den
  ritas därmed genom exakt samma kodväg som en riktig app och som
  glaze-render. Priset (uttryckligen accepterat av Erik): hela verktyget
  byter skepnad efter det fönster som redigeras.
- **Taggen beskriver RITBORDET, inte DRIFTTILLSTÅNDET** (APP_API C3,
  2026-08-13). Appens egna `setUI`/`setTheme` är appens val i drift och
  FÅR skilja sig från taggen — previewn visar då ett annat utseende än den
  körda appen, och det är meningen. En grind som mäter avvikelsen **går
  inte att bygga**: appens setUI står i ANVÄNDARKOD (`.cpp`/`onLoad`), som
  aldrig parsas, och byggtidsgenerering är förbjuden — det finns bara två
  principbrott att välja mellan. Att låta buildern GENERERA `setUI` i
  `initializeComponent()` är uttryckligen förkastat: det vore
  per-fönster-tema i runtime, stängt 2026-08-12 ("Ett tema per app!
  Punkt."), infört bakvägen.
- **`lang=` är RITBORDETS språk, inte driftens** (SPRÅKPAKET §5d): samma
  svar som `setUI` fick i APP_API C3 — en genererad `setLanguage("fi");` i
  `initializeComponent` vore per-fönster-språk bakvägen. Driftvalet är
  APPKOD; taggen krävde ingen ny satsform, bara en nyckel.
  `applyDesignTag` kör `glaze::setLanguage(tag)` SIST i kedjan, och en
  okänd tagg loggas av butiken och lämnar axeln osatt.
- **Ingen väg att SÄTTA taggen i UI:t finns än** — popupen är uppskjuten,
  så raden skrivs för hand i filen tills vidare (💡 i TODO.md).

Grinden är [tests/roundtrip_gate.cpp](../../../tests/roundtrip_gate.cpp) med
fixturerna i [tests/fixtures/](../../../tests/fixtures/).

## Nyckelidé — buildern parsar ALDRIG användarkod

Upplösningen av research §3: reader läser BARA regionen i `.h` +
`*.glazeui` (100 % genererad, strikt grammatik). Vid spara SPLITTAS filen
på markörraderna (ren strängoperation på råbytes) — allt utanför klistras
tillbaka byte för byte, inklusive CRLF/BOM/trailing whitespace.
Round-trip-testet (läs → modell → skriv utan ändring → byte-identiskt)
är regressionsgrinden; demo-fönstren är facit. Den bor i
[tests/roundtrip_gate.cpp](../../../tests/roundtrip_gate.cpp) (`ctest -R
roundtrip_gate`) och länkar bara `builder_core` — inget fönster, ingen pump.

## Lokala konventioner & fallgropar

- **PREVIEWVANDRINGEN ÄR EN PARALLELL IMPLEMENTATION av `AppWindow::drawTree`
  och måste hålla SAMMA kontrakt** — därför bor den bara i `bld::drawPreviewTree`
  (glaze-render går genom `drawTree`). Den sätter `wc.box = w.resolve(c)` före
  `render()`: `Draw` projicerar mot `c.box`, och en OSATT box gör widgeten osynlig.
- **NAMN-RADEN är KANONISK** (etapp C, 2026-08-19): den står på VARJE vanlig
  nod, migreras in vid öppning och valideras — ETT namn, och `n.id` ÄR
  svaret (utan raden bar den BYGGDA appen tomt namn medan previewn fungerade).
  **AUTO-NAMNET är `Button1`/`Button2`** (`genId(classBaseName(cppType))` —
  klassnamnets form, inte registrets gemena typnamn), och namnet REDIGERAS
  i egenskapspanelen via taggen `$name` (kan inte kollidera med ett fält:
  `validFieldName` släpper aldrig igenom `$`), som `commitPropEdit` fångar
  FÖRE `kFieldMeta`-uppslaget. Buildern VARNAR att direkta referenser till
  det gamla MEDLEMSNAMNET i `.cpp` måste rättas för hand — men rättar dem
  aldrig (regel 5); handler-BINDNINGEN retargetas och handler-SYMBOLEN
  behöver aldrig byta namn. Inbyggda medlemmar är läs-bara.
- **Modell → preview är enkelriktad:** ändra alltid `model_` och kör
  `rebuildPreview()` — mutera aldrig preview-widgets direkt.
  `previewByNode_[i]` mappar nodindex → widget (null för icke-adopterade).
- **Koordinater:** placeringen är logiska px relativt FÖRÄLDERNS childArea
  (rotens = canvasen). Omvandling mus↔modell via `canvasRect()` +
  `window().uiScale()`; drag i en panel räknar mot panelens childArea.
  **EN koordinatväg: `ctxForNode`** — förälderkedjan ur `model_.parentOf` med
  ett `childArea` per LED, exakt som `drawPreviewTree`/`hitTestTree` (bara den
  DIREKTA föräldern är fel för ett BARNBARN). `frame()`,
  hit-testen, flytten och storleksändringen delar dessutom
  `selectionRect`/`handleRects`, så det man SER och det man kan GREPPA
  inte kan glida isär.
  **INVERSEN RÄKNAR MOT DEN SKURNA AREAN — `layoutArea(nod, pc)`** (§2.8,
  2026-08-19): `ctxForNode` ger FÖRÄLDERNS area, men sedan dockade remsor
  skär även för icke-dockade syskon är det inte samma yta som `resolve()`
  placerar mot. **Placering, drag OCH storleksändring** går därför via
  `layoutArea` → `glaze::Widget::carveSiblingDocks` (Codex-fyndet:
  `resizeTo` räknade `right`/`bottom` ur `pc.area` direkt). `nodeIdx < 0`
  = "ny nod på rotnivån", utan uteslutning — vad `placeAt` behöver, som
  räknar koordinater INNAN preview-widgeten finns. Resultatet får ALDRIG
  skickas till `resolve()`: den skär själv. Handtag, hit-test och
  kopplingsgapet går redan genom `resolve()` och följer automatiskt.
- **Storleksändringen skriver ANKAR-MEDVETET** — spegelbilden av `dragTo`,
  styrt av widgetens `anchors`, för `resolve` läser olika fält i de fyra
  fallen. Vid **dubbelankare (sträckning) IGNORERAS `width`/`height` helt**
  och storleken ändras genom OFFSETS (`x`/`right`, `y`/`bottom`); skrivs
  måttet där händer INGENTING synligt. Utan ankare (centrerad) skrivs både
  måttet och offsetens exakta invers av `widget.cpp:297/307`.
  **DOCKAD NOD VISAR INGA HANDTAG** (markeringsramen finns kvar): docken
  äger både plats och remsans mått, och UI:t ska inte lova vad draget inte
  kan hålla — samma princip som `isFixedSize` och resize-pekaren.
  Remstjockleken redigeras i egenskapspanelen.
  **EN SYSKONANKRAD KANT ÄR LÅST PÅ SAMMA SÄTT** (SYSKONANKARE steg 4),
  men frågan är PER KANT och inte per nod: `edgeAnchored` svarar per fält
  och `handleActive(nod, hörn)` är den EN fråga som både ritningen och
  hit-testen ställer, så ett osynligt handtag aldrig kan greppas. `dragTo`
  hoppar över den låsta AXELN (en widget ankrad i x-led går fortfarande
  att dra i y-led) och säger ifrån i statusraden. En OPAK ankarrad låser
  ingenting — `resolve()` placerar ju då med x/y.
  **Handtags-hit-testen ligger FÖRE canvas-grinden** i `MouseButtonDown`:
  handtaget är centrerat på markeringens hörn, så för en widget som ligger
  an mot canvasens kant — vilket varje sträckt eller höger-/bottenankrad
  widget gör — hamnar de yttre handtagen utanför `canvasRect()` och
  greppet hade svalts. Undantaget är snävt: bara ett handtag på en redan
  markerad, odockad nod.
- **Resize-tillståndet lagrar BARA nodindex och numerisk startgeometri** —
  aldrig en `glaze::Widget*` eller en referens över anropet.
  `rebuildPreview()` river hela det gamla previewträdet vid VARJE motion,
  så en sparad pekare dinglar direkt. (`dragTo` är säker i dag just för
  att den inte gör det.)
- **`refreshPropPanel()` körs på MOUSE-UP efter drag och resize** (annars
  visar width/height/x/y gamla tal) — aldrig under pågående motion: `setRows`
  river en aktiv editor.
- **Ctx byggs aldrig för hand:** `canvasCtx()` hämtar `makeCtx()` och byter
  bara `area`; preview-trädet vandras med `childArea`/`clipRect` (publika
  container-krokar) — samma geometri som appen.
- **Värdekonvertering preview:** basfält (x/y/right/bottom/width/height/
  anchors) sätts direkt ur råtexten; övriga via `setProp` (sträng
  avciteras, enum → sista segmentet gement) — UTOM textfälten, som bara tar
  en literal eller `glaze::tr(N)` (🐛 58). Okänd nyckel loggas +
  ignoreras (opak — ritas inte, bevaras vid spara).
- **Fyra writer-operationer är HELA skrivytan** — lägg aldrig till en väg
  som skriver filer förbi cpp_writer (kodgranska mot regel 5).
- **Ny emitterad satsform** (t.ex. en ny egenskapstyp) = uppdatera reader +
  writer + round-trip-test i SAMMA ändring, annars bryts öppna/spara.
  **OCH en fixtur som faktiskt KOMPILERAS** (APP_API steg 5, Codex-fyndet
  som rev planens första utkast): `.glazeui` är riktig C++ som byggs, så
  reader och writer kan vara helt överens om en rad kompilatorn avvisar —
  en grön round-trip är inget kompileringsbevis. Targetet `calls_fixture`
  i `tests/CMakeLists.txt` bygger `tests/fixtures/calls.*` av just det
  skälet.
- **SATSORDNINGEN ÄR KÄLLORDNINGEN**: modellen bär EN ordnad
  `std::vector<Stmt>` som enda sanning om initializeComponent-kroppen, och
  writern emitterar den i LISTORDNING — regel 4 skyddar ORDNINGEN lika mycket
  som texten, för satserna är exekverbar C++. Det ENDA som normaliseras är BLANKRADERNA (en rad
  skjuts in när blocket byter: fönsteranrop · en widgets rader · events ·
  trädbygget), så en kanoniskt ordnad fil får exakt sin gamla layout
  tillbaka.
  **NODORDNINGEN är en ANNAN ordning:** `nodes` bär Z-/trädbyggsordningen
  med INBYGGDA fönstermedlemmar FÖRST (fönstret adopterar dem i sin
  konstruktor), och att `statusBar` flyttas först bland noderna flyttar
  alltså inte dess satser.
- **INBYGGDA fönstermedlemmar särhanteras i BÅDA trädbyggarna.**
  `statusBar`/`menuBar`/`appIcon` har varken medlemsrad eller adopt-rad —
  writern får inte emittera någondera (medlemsraden hade SKUGGAT
  basklassens fält och inte kompilerat). I glaze-render (`window_tree.cpp`)
  mappas noden till fönstrets EGEN `statusBar`, `menuBar` respektive
  `appIcon`; i builderns preview, vars rot är en `Panel` och inte ett
  `AppWindow`, skapas en riktig widget ur registret — och den DÖLJS för
  alla builtins, också `appIcon` (canvasen har ingen namnlist att sätta
  den i; canvasens chrome är fas 2).
  **PROGRAMIKONEN (PROGRAMIKON steg 16):** setter-fälten `icon`/`smallIcon`
  (värdet `glaze::img(…)` ELLER `{}` = förvalet — `apply_props.cpp` bygger
  ett `ImageSource` TYPAT, för `{}` har ingen "lista/nyckel"-text) och
  `large`/`noIcon` (bools via `AppIcon::setProp`). Programmenyn är en
  ATOMISK `applyCall`-gren med menyradens form, där den TOMMA stigen `{}`
  känns igen som roten HÄR och bara här — `bld::parseMenuPath` avvisar
  fortfarande en tom stig, för menyraden. `addMenu` finns inte på ikonen
  (opak rad); `setHandler` är känd men utförs inte. Palettraden finns för
  att `appicon` är REGISTRERAD (registret ska vara kartlagt i paletten,
  `preview_gate`); i ett fönster är ikonen den inbyggda medlemmen.
  **MENYRADENS INNEHÅLL visas när det står i `.glazeui`** (den PLATTA
  formen, MENYER 2026-08-19) och aldrig när det byggs i appens `onLoad` —
  användarkod parsas inte, och det gäller varje widget, inte menyraden.
  Samma gäller PropertyGrids rader sedan 2026-08-20: `setRows`/`addRow`/
  `setRow` i filen UTFÖRS, `grid->setRows(...)` i `onLoad` gör det inte.
  ORDNINGEN löses i
  MODELLEN sedan APP_API_2 steg 1 (builtins först i nodlistan) i stället
  för av ett eget pass i varje trädbyggare — en dockad remsa skär i
  BARNORDNING, och en builtin sist hade byggts efter sitt eget barn.
- **Designtaggen är den enda vägen i filen där användardata kan TAPPAS:**
  missas reader-fångsten raderar första spara raden tyst. Round-trip-testet
  är skyddet för användarens fil, inte en formalitet.
- Editorn följer händelsestyrningen: `event()` returnerar `true` när något
  ändrats så fönstret ritas om. Ingen egen render-loop.
