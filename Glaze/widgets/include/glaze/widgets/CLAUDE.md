# CLAUDE.md — Glaze/widgets/include/glaze/widgets

**Komponent-headers** för `glaze` — en fil per widget, ingen infrastruktur.
Infrastrukturen (paraplyet `glaze.h`, `widget.h`, `draw.h`, `style.h`,
`theme.h`, `app.h`, `app_window.h`, `assets.h`) bor ETT steg upp i
[../](../CLAUDE.md); implementationen i [../../../src/widgets/](../../../src/widgets/).
Del av GlazeUI — se [root /CLAUDE.md](../../../../../CLAUDE.md).

Apparna inkluderar ALDRIG en fil härifrån direkt: `<glaze/glaze.h>` är
paraplyet och listar dem alla. **Typnamnen är oföränderliga**
(`glaze::Button` …) — genererad committad kod och `cpp_reader`-grammatiken
beror på dem (hård regel 4).

## Tredelningen (samma i varje fil)

**Alla mått i den här katalogen är LOGISKA px — även mätvägen**
(MÄTKONTRAKTET): konsumenten (`Widget::resolve`, `Bar::placeChild`) skalar
en gång, när svaret möter `c.area`, och mallen (axelregeln nedan) har ingen
skalfaktor: `gfx::Sizef s{width, height};`. Grind: `tests/measure_gate.cpp`.

## TEXTEN är ett TOKEN — `TrText`

`Label` · `Button` · `Checkbox` · `Radio` · `Link` · `ToolButton` ·
`GroupBox` · `DockPanel` · `TabPage` · `MarkdownView` lagrar sin `text_` som en
**`glaze::TrText`**, som bär ANTINGEN en literal ELLER ett
översättnings-id. Etiketter INNE I data är också `TrText`:
`Table::Column::text` (kolumnrubriken) och `TreeNode`s text — medan
tabellens CELLER är strängar (ListBox-prejudikatet nedan). Ctorerna från
`const char*`/`std::string` är implicita, så `setText("Hej")` och
`setText(glaze::tr(1042))` kompilerar båda.

**GETTER-PARET är additivt, och gränsen är hela poängen:**

    text()        den UPPLÖSTA texten — OFÖRÄNDRAD signatur
                  (`const std::string&`) och OFÖRÄNDRAD betydelse
    textToken()   IDENTITETEN — det writern emitterar och det
                  menystigen matchar på

`AppWindow` har samma par (`title()`/`titleToken()`), och `MenuItem::text`
är också en `TrText`. **`TextInput` och `TextArea` är UNDANTAGEN** — de bär
ANVÄNDARENS text, så `setText` tar `std::string`; likaså `items` på
`ListBox`/`Dropdown` (appen fyller om dem vid språkbyte). **`SpinBox` har ingen `text`-sättare alls**:
talfältets tillstånd är `value`, och `text()` är bara läsvägen till det
fältet visar.

**JÄMFÖRELSEN i settern är TOKENjämförelse**, inte textjämförelse:
`setText(tr(5))` på en widget som redan visar samma STRÄNG är en riktig
ändring, för identiteten blev en annan och texten följer nu språket.

**`setTextToken`-overriden KEDJAR till basen** (`toolTip` är basens): varje
widget med egen text avslutar med `return Widget::setTextToken(key, value);`,
annars blir `x.setToolTip(glaze::tr(…))` opak just där (`tooltip_gate`).

## TILLSTÅNDET är PRIVAT — settrarna INVALIDERAR

Tillståndet är privat med `setX()`/`x()` (bool-getters `isX()`) — ett rått fält
syns bara när anropsvägen råkar rita om (regel 1). LAYOUT-fälten är kvar RÅA
(kontraktet i `widget.h`). Utanför matrisen med skäl: `PropertyGrid::Row` är
DATA, och `TopBar`s `titleLabel()`/`minButton()`/… är LÄSaccessorer till BARN
(`AppWindow` är `friend` och bygger baren). **En setter fyrar ALDRIG `onChange`
eller `onCommit`** — på varje widget.

## `onChange` och `onCommit` — två frågor (0.1.2)

`onChange` = värdet ändrades; **`onCommit` = användaren är KLAR** med det.
Samma args-typ som widgetens `onChange` (datagridens `CellChangeArgs`), bara
efter en FAKTISK ändring sedan redigeringen började (baslinjen sätts när
fokus kommer, efter varje commit och av en setter som BYTER värdet — ett eko
av samma värde flyttar den inte), och aldrig från en setter. Fyras båda för
samma handling kommer `onChange` först — via `Widget::firePair`, som frågar
`HandlerGuard` emellan; kod EFTER en handler frågar svaret och rör aldrig
`this` när det är false. En commit som fokusförlusten utlöser POSTAS
(`postEvent`). Ett klick som INTE tar fokus (ToolButton, menyval,
accelerator) committar inget av sig självt: `commitEdit()` overridas av de
åtta som har något som väntar, och `AppWindow::commitEdits()` går kedjan.

| Widget | `onChange` | `onCommit` |
|---|---|---|
| `TextInput` (och FilePickers fält) | varje tangent | Enter (konsumeras inte) / fokus bort |
| `TextArea` | varje tangent | fokus bort (Enter = ny rad) |
| `SpinBox`, DatePickers fält, ColorPickers hexfält | vid commit och varje steg | samtidigt |
| `Slider`, `Scrollbar`, ColorPickers reglage | live under draget | släppet; varje tangent-/hjulsteg |
| `Checkbox`, `Radio`, `ToolButton`, `Dropdown`, `ListBox`/`TreeView`/`ListView` (+ `onCheck`), paletten, kalendern, filvalet | samtidigt | samtidigt |
| `PropertyGrid`, `DataGrid` | vid commit | samtidigt |

Utanför: `Splitter`, `DockHost`/`DockPanel`, `TabControl` (navigering och
inget värde) och `Canvas`. Fokus INOM och kompositernas `selectAll`/
`setSelection` står i widget.h respektive här nedan. Grind: `commit_gate`.

## Axelregeln gäller ALLA komponenter

Regeln står i [../](../CLAUDE.md) (`widget.h`). Ingen header har ett eget
"auto"-fält: AUTO-måttet bor i `Style` (`fieldDefaultW`, `progressHeight`,
`*DefaultLen`) och mönstret i `contentSize()` ([../../../src/widgets/](../../../src/widgets/CLAUDE.md)).

## Filer

- **`label.h`** — `Label`: `text` + ROLL (`Role` Text/Dim/Accent/
  Titlebar/**Heading** följer temat; Custom = fast färg i `color`) +
  **`align`** (Left/Center/Right; meningsfull först när ytan är BREDARE än
  texten, alltså satt `width`/dubbelankare/dockad remsa). **Rollen bär även
  STORLEKEN** (APP_API D1): `Role::Heading` ritas i `Style::headingFontSize`,
  och `contentSize()` MÄTER med samma font — annars ljuger autostorleken.
  Privata `roleColor()` (roll → temafärg; Heading faller till `text` med
  FLIT — en rubrik skiljer sig genom STORLEK), `roleFont()` och `roleCtx()`
  (kopia med rollens font; Draw läser `c.font`, så ett storleksbyte är ett
  fältbyte). **`wrap`** (MESSAGEBOX steg 3): texten bryts mot widgetens
  BREDD via `Draw::textWrapped` — ordgräns, `\n` bryter alltid, för brett ord
  hårdbryts på UTF-8-teckengräns. **HÖJDEN FÖLJER BREDDEN** (skyddade `heightForWidth`,
  widget.h): satt `height` vinner, satt `width` ger höjden ur sig, en STRÄCKT eller Top/Bottom-
  DOCKAD etikett får höjden för den bredd layouten GAV, Fill/Left/Right/Bar låter ytan ge höjden
  (texten klipps), en etikett utan bredd är en rad, och i en komposit med autohöjd ur barnen
  räknas den som en rad (`childrenExtent` har ingen bredd). `contentSize` frågar samma krok och
  ritningen bryter vid samma flyttal. Bredd och höjd är LOGISKA px.
  **KORTNINGEN, HÖJDLEDET OCH GRANNEN** (REQ3_TRAD_LISTOR_FALT): `setElide` (EN
  rad kortas med "…" vid bredden; HELA texten blir tooltip när den kortats —
  `toolTipAt` mot `Draw::elide`, en egen tooltip vinner; `wrap` går före),
  `setVAlign` (Top/Center/Bottom, Center förval) och `setBuddy(namn)` (AUTO-höjden
  minst ett SYSKONS mätta höjd, uppslaget på `name`; en Label som granne
  ignoreras, så cykeln går inte att uttrycka). **`setWidthGroup(namn)`**
  (REQ7_LAYOUT B3): samma gruppnamn ⇒ den bredaste SYNLIGA medlemmens bredd i
  fönstrets träd som AUTO-bredd (satt `width` = golv, rollens font, EN rad);
  text, roll, grupp och synlighet ritar om hela fönstret. `setProp`: text/color/role
  (`heading` och `titlebar` ingår)/align/wrap/elide/vAlign/buddy/widthGroup/haloWidth. OBS:
  `Label::setColor({})` är SVART — knapparnas `setColor` tar en `std::optional`. **GLORIAN** (REQ8 24): `setHalo(std::optional<gfx::Color>)` + `setHaloWidth(px)` (0 = `Style::labelHaloWidth`, begränsad av `overdraw`) — konturen ritas av `Draw::halo`.
- **`button.h`** — `Button`: `text`, fokuserbar, Space/Enter aktiverar.
  **DIALOGKNAPPEN**: `setDialogResult` (ett klick stänger fönstret med svaret
  EFTER `onClick`) och `performClick()`; som fönstrets STANDARDKNAPP ritas den
  på accentytan (`shownAsDefault`). `labelColor()` är PROTECTED (filväljarens
  ikonknapp). **Bild:** `img` (`ImageSource` — en asset via `glaze::img` eller en
  `Bitmap`; sidan `sizeClass()`, för pixlar `iconFallback`) +
  `imagePos` (Left/Right/Above/Below); tom text = bara ikon. **Autostorlek
  gäller alla knappar** (bara osatta axlar) — höjden blir radhöjd + 2 ×
  `buttonPadY`, vilket är det som gör stilens knapphöjd till det man FÅR.
  Privata `content()` (bild + text som EN `Draw::Content`), `fillColor()`,
  `radius()` (piller när `buttonRadius` < 0) samt
  Glaze-formens `contentRect()` (innehållet sjunker `buttonPressShift` vid
  tryck — hela tryckrörelsen, definierad EN gång så bild och text följs åt).
  Lamineringen är **`Draw::laminate`** (fem widgets delar den). `setProp`:
  text/img/imagepos/dialogResult/iconSize. **INAKTIV IKON dimmas**: `content()` sätter
  `Content::imgOpacity` = 1 − `Style::iconFade`, bildens OPACITET.
  **`setIconSize(px)`** (REQ3, logiska px, 0 = AUTO — storleksklassen, annars
  `iconFallback`; samma namn på ToolButton och tabellerna) och **`setColor(
  std::optional<gfx::Color>)`** (bara TEXTEN; `nullopt`/`{}` = temat,
  accentytan vinner, inaktiv `textDim`; ingen textform — en anropssats).
  **Den godkända MALLEN** — kopiera den; VARJE widget med en `draw()` har
  alla tre ritgrenarna.
- **`checkbox.h`** — `Checkbox`: `text`, `onChange` + `onCommit` (läs värdet
  ur sender), fokuserbar, Space togglar (Enter går till standardknappen).
  **TRE LÄGEN** (FAS1_B_WIDGETS): `CheckState`; `setThreeState` styr BARA
  användarens cykel av → på → mellanläge, `isChecked()` är sann också i
  mellanläget. Minimalistic FYLLER den markerade rutan — familjens enda
  undantag från "ingen yta i vila". **`setColor`** som Buttons (REQ3): bara
  texten (`labelColor`), rutan och märket följer temat.
- **`textinput.h`** — `TextInput`: enradigt textfält. **`align`**
  (APP_API_2 B1): SAMMA fält och samma namn som på `Label` (regel 6), så en
  DISPLAY kan vara högerjusterad OCH markerbar. Privata **`textOriginX(c)`**
  är EN sanning om var texten börjar (läst av `drawContent`, `indexFromX`,
  `caretBounds` OCH `ensureCaretVisible`); offseten är klampad ≥ 0 och blir
  noll exakt när texten svämmar över. `caretEdge()` finns för att
  `textOriginX` behöver caretens mått utan att fråga `caretBounds`.
  **`readOnly`** (en SETTER): fältet VISAR men tar inte emot tangenter —
  `wantsTextInput()` false, `keyDown()` spärrar Backspace/Delete/Ctrl+X/Ctrl+V,
  navigering, kopiering, fokus och musmarkering ORÖRDA; ytan `fieldReadOnly`,
  texten dimmas INTE (skilt från `disable()`). Lästillstånd: `caret()`, `selBegin/selEnd()`,
  `hasSelection()`, `caretVisible()`, `scrollX()` (LOGISKA px),
  `caretRect(Ctx)` (FYSISKA px — en PROJEKTION av privata `caretBounds`).
  Privat `drawContent(selFill, selText)` DELAS av alla tre ritningarna.
  **Fältet DELAS** (ytan, markeringen, caretens bredd, UTF-8-gränserna,
  filtret, historiken, genvägsfrågan) med `TextArea` genom den interna
  `src/text_field.h`. `setText` klampar caret och markering på TECKENGRÄNS.
  **`onCommit`** (0.1.2): Enter — som INTE konsumeras — eller fokus bort,
  bara efter en ändring sedan baslinjen (`commitBase_`); fokusförlustens
  postas från `focusLost` (en override — kompositernas fält anropar basen).
  **`selectAll()`/`setSelection(b, e)`**: BYTE-index, klampade och
  nedsnappade, ankaret på `b` och careten på `e`; inga events.
  **ÅNGRA/GÖR OM** (TEXTFALTEN): `undo()`/`redo()`/`canUndo()`/`canRedo()`,
  och Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z ANROPAR dem — i ett fokuserat fält ALLTID
  konsumerade, också med tom historik, så en menys accelerator aldrig tar
  över. Båda fyrar `onChange`. Ett steg är ett ord, en följd backsteg eller
  Delete, eller ett Block (klistra, klipp, raderad markering); flytt, Enter
  och fokusförlust förseglar; 100 steg och 4 MiB. `setText` med NY text
  tömmer historiken (samma text är no-op), och skyddade `resetHistory()`
  tömmer OVILLKORLIGT — för cell-editorer som återanvänds.
  Historiken är en LAT `unique_ptr<textfield::History>` (ctor/dtor i cpp:n);
  kompositerna får inga vidarebefordrade metoder. **LÖSENORDET**
  (`setPassword`/`isPassword`): masken `Style::passwordMask` en per kodpunkt,
  caret och markering i den RIKTIGA texten; kopiera/klipp ut konsumeras som
  no-op och fältet har ingen historik. Genvägarna är Ctrl UTAN Alt (AltGr),
  på macOS Cmd utan Ctrl och Option (TODO 79). IME = senare. **MAXLÄNGDEN** (`setMaxLength`,
  KODPUNKTER, 0 = obegränsat) kapar det som skrivs och klistras, aldrig `setText`;
  **PLATSHÅLLAREN** (`TrText`, `textDim`) syns med fokus tills man skriver. **TEXTSTORLEKEN** (REQ8 26, `setTextSize(Length)`
  px/mm, 0 = stilens; samma på TextArea, ListBox och Dropdown) mäter och ritar texten genom `textCtx` — aldrig `resolve`.
- **`text_area.h`** — `TextArea : Panel`, det FLERRADIGA fältet (WinForms TextBox `Multiline`;
  `heightForRows(c, rader)` = höjden för ett antal rader, AUTO-höjden och `inputBox`s flerradsläge
  frågar den). **EN EGEN WIDGET OCH INTE `TextInput::setMultiline(true)`** — skälen står i headern;
  de två delar FÄLTET (`src/text_field.h`). **RADINDEX**: texten är EN sträng där `\n` skiljer
  styckena, caret/markering i BYTE på teckengräns som i `TextInput`, de VISUELLA raderna en layout
  över strängen (utan brytning ett stycke per rad, med brytning `Draw::wrapLines` som en
  radbruten `Label`). En position där ett ord HÅRDBRÖTS är både radslut och nästa radstart;
  careten minns sidan (`upstream_` — End och ett klick bortom radslutet ger den övre). **`wrap`**
  som `Label::wrap`, default PÅ. **TANGENTERNA**: Upp/Ner en VISUELL rad med bevarat x-läge
  (`goalX_`), PageUp/PageDown en sida, Home/End den visuella radens, Ctrl+Home/End hela textens,
  Enter en ny rad (med Ctrl/Alt/Cmd är den appens, `onKeyDown`), Tab flyttar fokus — med
  **`setAcceptsTab(true)`** skriver Tab en tabb (Shift+Tab/Ctrl+Tab flyttar ändå, skrivskyddat
  släpps den) —, Ctrl+A/C/X/V (Cmd på macOS). **TABBSTOPPEN**: en tabb ritas till nästa stopp
  (`textAreaTabSpaces` blanksteg från den VISUELLA radens början), bryter aldrig en rad, och
  caret, markering och mus talar samma stopp. **MUSEN**: tryck, drag (förbi kanten RULLAR det, per
  musrörelse — widgets har ingen egen timer), dubbelklick markerar ordet (`textfield::wordAt`).
  **RULLNINGEN**: den lodräta remsan syns när texten inte ryms, hjulet rullar UTAN fokus och
  bubblar i kanten, careten hålls synlig när DEN flyttas (`scrollToCaret` ur tangent-, text- och
  musvägen — aldrig i `draw()`). LUFTEN rullar med texten men täcks aldrig (`textClip`), och
  `padRight` är minst remsans tjocklek så brytbredden inte ändras när remsan dyker upp.
  **URKLIPPET** talar LF åt båda hållen (inklistrat: CRLF/CR → LF, tabb → blanksteg utom med
  `acceptsTab`; fasaden översätter till OS:ets form). `setText` normaliserar RADSLUTEN och inget
  annat. `readOnly` och `onChange` har TextInputs kontrakt. **PRESTANDA**: layouten är cachad mot
  en nyckel (font, skala, radhöjd, brytbredd), en redigering lägger bara om sina stycken
  (`relayout`) och ritningen besöker de synliga raderna. AUTO-storleken är `textAreaDefaultW` ×
  `textAreaRows` rader. `setProp`: text, readOnly, wrap, placeholder, acceptsTab, maxLength
  (`\n` är en kodpunkt), textSize — inget `title`-alias. **`onCommit`** bara vid fokus bort
  (Enter är en ny rad); **`selectAll`/`setSelection`** med TextInputs kontrakt, utan att rulla.
  **ÅNGRA/GÖR OM**: TextInputs fyra metoder, tangenter och gruppering; Enter är SKRIVANDE (ordet
  efter en radbrytning börjar ett nytt steg), ångra går genom `replaceRaw` så den inkrementella
  layouten följer med, och fältet rullar till careten. SKRIVNA GRÄNSER i headern: IME,
  justering, egna tabbstopp, ordhopp, Shift+klick, rulla till careten, lösenordsläge, radfrågor.
- **`markdown_view.h`** — `MarkdownView : Panel`, MARKDOWN-VYN (REQ7_MARKDOWN; Qt `QTextBrowser`): formaterad text i en
  rullande yta — release notes, hjälp. EN EGEN WIDGET med eget layoutträd (ett fett ord mitt i en rad går inte med
  Label/Link). `text` är ett `TrText` (en NY text rullar till toppen; ett väntande `scrollToAnchor` står kvar),
  `setImage(namn, bild)` en NYCKLAD tabell (`{}` tar bort; utan post är namnet `lista/nyckel` som asset, aldrig med
  ett schema), `scrollToAnchor(slug)` och `linkAt(lokal)`. `onLinkClick` (`LinkClickArgs {url, handled}`) vid tryck
  OCH släpp på samma länk; utan `handled` rullar `#ankare`, http/https/mailto går till systemet, allt annat
  ingenting. AUTO-höjden följer bredden (`heightForWidth`). Inte fokuserbar, ingen markering — gränserna i headern.
- **`spin_box.h`** — `SpinBox : Widget`, TALFÄLTET (wxSpinCtrlDouble, WinForms NumericUpDown):
  `value`/`min`/`max`/`step`/`decimals` + `onChange`/`onCommit` + `selectAll`/`setSelection`
  (vidare till fältet). **DEN FÖRSTA RIKTIGA KOMPOSITEN.** FÄLTET är en riktig `TextInput` —
  privat nästlad `SpinBox::Field` (som `Dropdown::ListWindow`), ägd, `Dock::Fill` i `childArea()`
  = ytan vänster om stegaren minus `spinGap`. Caret, markering, klippbord och scroll ärvs orörda;
  subklassen lägger bara till Up/Down (en tangent går till den FOKUSERADE widgeten), ett
  teckenfilter (siffror, tecken, `,`/`.`) och COMMIT av texten vid Enter (som INTE konsumeras)
  och fokusförlust. **STEGAREN ÄR DATA och inte två `Button`-barn** (ListBox/MenuBar-prejudikatet):
  en `Button` är ett tabbstopp och tar fokus vid klick, så caret hade lämnat fältet. ETT
  tabbstopp (`focusesChildren()`), fokus som landar på kompositen går vidare till fältet
  (`focusProxy()`), `disable()` når fältet (`enabledChanged()`) och fältet kan inte lossas
  (`canReleaseChild`). **VÄRDET är `double`** och `value()` är det EFFEKTIVA värdet, klampat och
  avrundat VID LÄSNING, så settrarnas ordning aldrig spelar roll. **COMMIT, NumericUpDown-
  modellen**: att SKRIVA ändrar inte värdet; Enter och fokusförlust tolkar texten, KLAMPAR ("5" +
  Tab med min 10 ⇒ 10) och skriver om den, och `onChange` + `onCommit` fyras TILLSAMMANS när det
  effektiva värdet ändrades — fokusförlustens POSTADE (`postEvent`). Text som inte tolkas (`""`,
  `"-"`) lämnar värdet orört och skrivs tillbaka. Ett STEG (stegaren, pilarna, hjulet) utgår från
  en tolkbar skriven text, annars från värdet. De två protected ANVÄNDARvägarna
  `stepFromUser(n)` och `textFromUser()` (= Enter) är de enda som fyrar. **HJULET KRÄVER FOKUS**
  i FÄLTET (ombudet); utan svarar `mouseWheel` false och hjulet bubblar (`spinbox_gate`).
  **DECIMALTECKNET följer SPRÅKET** (`str::DecimalSeparator`, ramverks-id 24: `,` på sv/nb/fi,
  `.` på en), och ett språkbyte skriver om ett fält som INTE redigeras genom
  `Widget::languageChanged()`. Tolkningen godtar både `,` och `.`, men bara ETT av dem.
  `setProp` (value/min/max/step/decimals) tolkar en C++-LITERAL (`1e3`, `.5`, `2.5f`) — en annan
  parser än användarens text med flit; ett ogiltigt värde loggas och nyckeln räknas som känd.
  Geometrin (`stepperRect`/`partRect`/`partAt`) tar ytan `b` som argument. INTE HÄR: upprepning
  medan man HÅLLER stegaren (widgets har ingen egen timer), hexadecimalt läge och
  tusentalsgrupper.
- **`file_picker.h`** — `FilePicker : Widget`, FILVÄLJARFÄLTET (wxFilePickerCtrl/wxDirPickerCtrl): en sökväg
  i ett textfält och en "…"-knapp som öppnar filväljardialogen (`glaze/file_dialog.h`). **TVÅ
  RIKTIGA BARN och två tabbstopp — ett beslut mot talfältets:** "…" öppnar en MODAL dialog, så
  fokus spelar ingen roll, och knappen som eget stopp är tangentbordsvägen dit (Tab +
  Space/Enter). Kompositen är inget stopp (`focusesChildren` falsk), PLACERAR barnen
  (`placesChildren` — knappen i högerkanten så bred som sin egen mätning, fältet resten minus
  `filePickerGap`), låter `disable()` följa med in (`enabledChanged`) och nekar att barnen
  lossas. Den ritar INGEN egen yta (pixel_gate har den bland de rena layoutwidgetarna).
  **VÄRDET heter `path`** och inte `text`. `mode` (FileDialogMode) och `path` är SETTER-fält,
  filtren kollektionsvokabulären (`addFilter`/`setFilter`/`setFilters`, samma `FileFilter` som
  dialogen), och knappens tooltip följer läget (dialogens titel). `onChange` fyras LIVE när man
  skriver och EN gång när ett val i dialogen gav en annan sökväg; **`onCommit`** är fältets egen
  vidarebefordrad (Enter, fokus bort ur FÄLTET — också till "…") och fyras tillsammans med
  dialogvalets `onChange`; programmatisk `setPath` aldrig. Fokus INOM gör att en flytt fält ↔
  knapp inte fyrar kompositens `onFocusLost`; `selectAll`/`setSelection` når fältet. `browse()`
  öppnar dialogen i sökvägens mapp med namnet förvalt (mappläget: i mappen själv), minns det
  valda filtret och dialogens kryssruta för DOLDA filer (också efter Avbryt) och tål att fältet
  rivs under den modala pumpen (basens `lifeToken()`). **`setDialogTitle(TrText)`** — tom =
  lägets titel, och knappens tooltip följer den. `setProp`: path, mode (open/save/folder),
  dialogTitle.
- **`color_picker.h`** — `ColorPicker : Widget`, FÄRGVÄLJAREN (wxColourPickerCtrl). Ett FÄLT med en
  färgruta och dropdownens pil; tryck, Space, Ned eller F4 öppnar en POPUP (inte en modal dialog —
  färgen väljs med resultatet synligt bakom; Enter går till standardknappen) med PALETTEN
  (`standardPalette()`, rutorna DATA), R/G/B-reglagen (VALT framför HSL, skälen i headern) och ett
  HEXFÄLT — ytan är den interna `src/color_panel.h`, som färgdialogen (`../color_dialog.h`) delar.
  **VALET GÄLLER DIREKT, och två events säger hur långt det kommit**: reglagen ändrar färgen LIVE
  (`onChange` per läge) och committar vid släppet och varje steg; hexfältet committas som
  talfältets text (Enter, Tab vidare eller att popupen STÄNGS); paletten väljer och STÄNGER med
  båda eventen. Stängningen ÅTERSTÄLLER INGENTING. `sync` skriver hexfältet bara när användaren
  INTE skrivit i det (`hexRedigeras_`). Pilarna i paletten flyttar bara markören.
  **TANGENTBORDET** tar popupen så länge den är öppen (Tab vandrar palett → reglagen → hex),
  genom `AppWindow::setPopupTakesKeys`. **ALFAN** (`setAllowAlpha`; av = opak): ett A-reglage,
  `#RRGGBBAA` och rutan över ett rutmönster. Färgen LAGRAS med alfan och `color()` läser den opak
  utan flaggan, så satsordningen inte avgör; paletten byter kulören och behåller alfan.
  **REGEL 8:** formen i `.glazeui` är HEXTEXTEN, ett setter-fält — `farg.setColor("#3366FF");` —
  och `setColor(gfx::Color{…})` kodens form; previewn utför båda. Ogiltig text ⇒ `gfx::log` +
  orört (`#` valfritt, sex siffror eller åtta med alfan, ingen kortform); `Label`s `color` tar
  bara sex. `colorText()` = `#RRGGBB(AA)` med versaler. `colorFromUser` (protected) är det
  AVSLUTADE valets väg, och alla vägar går genom privata `userColor(c, klar, postad)` (baslinjen
  `commitBase_`). Hjulet: reglagen i popupen bara med fokus, den stängda kontrollen aldrig.
  `hasOpenPopup` (deny vid `unadopt`), `focusLost`/`disable()` stänger. `setProp`: color,
  allowAlpha. INTE HÄR: egen palett, senast använda, pipett, tooltips i popupen.
- **`date_picker.h`** — `DatePicker : Widget`, DATUMVÄLJAREN (wxDatePickerCtrl, WinForms DateTimePicker).
  TALFÄLTETS KOMPOSIT: en riktig `TextInput` (privat `DatePicker::Field`, Fill i `childArea`) och
  en KNAPP SOM ÄR DATA (popupen är inte modal och tangenterna ska stanna i fältet), ETT
  tabbstopp (`focusesChildren` + `focusProxy`), `disable()` når fältet, inredningen går inte att
  lossa. Knappen, Ned (också Alt+Ned) och F4 öppnar en POPUP med ett MÅNADSRUTNÄT — alltid 6 × 7,
  så popupen har samma storlek varje månad. **VÄRDET** är `std::chrono::year_month_day`, och
  `date()` svarar med det EFFEKTIVA: klampat till [minDate, maxDate] VID LÄSNING (settrarnas
  ordning spelar ingen roll). Det finns ALLTID ett datum: utan `setDate` DAGENS (lokal tid). År
  1–9999. **FÄLTET TALAR ISO 8601** (`ÅÅÅÅ-MM-DD`, den enda form som läses likadant på varje
  språk) och COMMITTAS: vid Enter (går vidare till standardknappen), fokusförlust (postat) och
  när kalendern ÖPPNAS ur fältet blir en hel giltig text datumet (klampat), allt annat lämnar det
  orört, och texten skrivs om; `onChange` + `onCommit` tillsammans. Bara siffror och `-` tas
  emot; `selectAll`/`setSelection` når fältet. **I POPUPEN** flyttar pilarna en MARKÖR (upp/ner
  en vecka), PageUp/PageDown en månad (Ctrl/Cmd: ett år), Home/End till månadens första/sista —
  markören är det PROVISORISKA (`provisionalDate()`, ogiltigt när stängd) och går aldrig utanför
  gränserna; Enter/Space eller ett tryck på en dag VÄLJER och stänger, Escape och Tab stänger
  UTAN ändring. Dagens datum är ringat, dagar utanför gränserna släckta. Tangenterna når popupen
  genom `AppWindow::setPopupTakesKeys`, som färgväljarens. **SNABBLÄDDRINGEN**: titeln går ett
  läge UPP — dagar → årets tolv månader (HELA namnen ur id 61–72) → åren (ett decennium per
  sida) — och ett val i en översikt ett läge NED; först en dag väljer. **Ctrl+Upp** (Cmd på
  Mac) går upp, Enter ned, pilarna/PageUp/PageDown/Home/End stegar i lägets enhet, Escape/Tab
  STÄNGER i varje läge. **HJULET**: den stängda ändrar aldrig datumet (hjulet bubblar), i
  popupen BLÄDDRAR det en sida — vyn rullar, värdet står. **VECKAN** börjar på språkets
  `str::FirstWeekday` (måndag på alla fyra). **REGEL 8:** tre heltal per sättare —
  `datum.setDate(2026, 10, 8);`, `setMinDate`/`setMaxDate` likadant — och
  `setDate(std::chrono::year_month_day)` är kodens form. Ett datum som inte finns ⇒ `gfx::log` +
  orört. `dateText()` = det effektiva datumet i ISO-form, medan `text()` är det fältet visar.
  `dateFromUser`/`textFromUser` (protected) är de ENDA vägar som fyrar `onChange`/`onCommit`;
  programmatisk `setDate` STÄNGER en öppen popup. `hasOpenPopup` (deny vid `unadopt`).
  `setProp`: date, minDate, maxDate (ISO-form), time, showTime. **KLOCKSLAGET** (`setShowTime`):
  fältet `ÅÅÅÅ-MM-DD HH:MM`, värdet PARET `date()` + `time()` i minuter utan klocka. INTE HÄR:
  "inget datum", veckonummer, flera månader, lokalt datumformat.
- **`dropdown.h`** — `Dropdown`: vallista (ComboBox, DropDownList-läget).
  `items`/`selectedIndex`/`selectedText()`/`onChange` + `onCommit`
  (tillsammans), `open()`/`close()`/
  `isOpen()`. Listan är ett RIKTIGT popup-fönster (Chrome::None + popup);
  rader är DATA, kapas vid stilens `dropMaxRows` och hjulscrollar;
  placeringen via `setPopupAnchor` (backenden äger flip/klamp).
  `setProp`: items ("a;b;c")/`selectedIndex`/**`align`** (post 52 — fältets
  text Left/Center/Right, Labels och TextInputs namn; datagridens val-editor
  följer kolumnen med den, och popupens rader står som listor står).
  **Popup-listan ÄR en `glaze::ListBox`-subklass**: dropdownen äger det
  COMMITTADE valet, popup-listan det PROVISORISKA (`provisionalRow()`, −1
  stängd) och går aldrig genom ListBoxens ANVÄNDARväg — ingen dubbelfyrning.
  `setItems`/`addItem`/`setItem`/`setTextSize` STÄNGER en öppen popup (listan har en kopia; storleken gäller raderna).
  **KOLLEKTIONSVOKABULÄREN**: `addItem`/`setItem`/`setItems` — IDENTISK med
  `ListBox`s (nedan); en form att lära sig, inte tre.
- **`listbox.h`** — `ListBox : Panel`: rullande lista med enkelval — eller
  FLERVAL (FAS1_B_WIDGETS) med LISTVYNS ord och regler (`setMultiSelect`,
  `setSelected`/`isSelected`, `selectedRows()`, `selectAll`/`clearSelection`,
  solo vid släppet) — och valfria kryss. `items`/`selectedIndex`/`selectedText()`/`showChecks`/
  `setChecked(rad, på)`/`isChecked`/`changedRow()`; `onChange` (valet) och
  `onCheck` (ett kryss) fyras BARA av ANVÄNDARen, och `onCommit` (0.1.2)
  direkt efter VARDERA (skilj dem åt med `changedRow()`). Raderna är DATA — inga
  widget-barn alls, alltså ETT tabbstopp, och kryssrutan är en GLYF och inte
  en `Checkbox`. Geometrin är EN sanning i tre skyddade metoder
  (`rowRect` → `checkRect` → `textX`, + `checkZone` för klickzonen "rutan +
  `checkGap`"); ritningen och musvägen läser samma. Ärver Panels klipp,
  overlay-remsor och scroll (`contentExtent`-override: rader × `listRowH`,
  bredden = viewportens ⇒ ingen horisontell axel).
  **De två skyddade ANVÄNDARvägarna** (`selectFromUser`/`toggleCheckFromUser`)
  är de enda som fyrar event; `setSelectedIndex`/`setChecked`/
  `setProvisionalIndex` gör det aldrig. `handleStripFirst(c, mx, my, down)`
  kapslar PropertyGrids remsa-först-mönster åt subklasser, och
  `ownsSurface()` är den ENDA skillnaden mellan en fristående lista och
  dropdownens popup (Minimalistic fyller ingen yta i vila — utom när
  widgeten ÄR fönstret). De SKRIVNA BEGRÄNSNINGARNA (autostorlek, rullning,
  ellips, sidbläddring) står i headern.
  **SNABBMENYN** (REQ3_TRAD_LISTOR_FALT): `rowAt(localX, localY)` i LOGISKA px
  (`MouseArgs::local`; −1 i remsan och nedanför — den interna i fysiska px heter
  `rowAtY`) och `contextRow()`, raden under det SENASTE högertrycket (−1
  inaktiverad och efter `setItems`). Högertrycket (`rightDown`, svarar alltid
  false) VÄLJER som Utforskaren: utanför valet ensam genom användarvägen, i valet
  bara aktuell; dropdownens och menyns rader svarar nej. **AKTIVERINGEN** (REQ5 1,
  listvyns form): `onActivate` vid dubbelklick på den valda raden (utanför
  kryss-zonen) eller Enter med ett val — utan handler eller val bubblar Enter.
  **`setChecked` tar TVÅ argument här och ETT på `Checkbox`/`Radio`** — builderns
  settertabell är TYPMEDVETEN, så `lista.setChecked(1, true);` är en `Call`.
  **KOLLEKTIONSVOKABULÄREN** (regel 8): `addItem`/`setItem(index, text)`/
  `setItems`, IDENTISKT på `Dropdown`; utanför ⇒ logg + no-op, aldrig tyst
  tillväxt. `setTextSize` (REQ8 26): radhöjden = `listRowH` + textens radhöjdsskillnad (skyddade `textCtx`).
- **`tree_view.h`** — `TreeView : ColumnPanel`, TRÄDET (wxTreeCtrl, WinForms
  TreeView). Noderna är DATA (text som `TrText`, bild som `ImageSource`,
  barn, utfälld, kryssad, textfärg, celler) — inga widget-barn, ETT tabbstopp,
  ListBoxens grund (klipp, hjul i rader, overlay-remsor, remsa-först) och tre
  markeringsrecept. **ADRESSEN ÄR EN INDEXSTIG** (`glaze::TreePath` =
  `std::vector<int>`, `{0, 2}`, `{}` = roten) och inte menyradens textstig —
  nodtexter UPPREPAS bland syskon — så kollektionsvokabulären tar en stig där
  listan har ett index, FÖRÄLDERN först: `addNode(förälder, text[, bild[,
  färg]]) → TreePath` · `setNode(stig, …)` (nodens EGNA fält, utelämnat =
  ingen bild/temats färg; barn, utfällning, val och kryss rörs inte) ·
  `setNodes(lista)` (aggregatet `glaze::TreeNode`, sju fält som växer SIST,
  positionellt eller designerat), `removeNode`, `moveNode` och
  `moveNodes(stigar, …)` i **FÖRE-flytten-koordinater** (Qt:s `moveRows`;
  flera atomiskt, i trädordning, en källa inuti en annan följer med) och
  `setExpanded`. Ogiltig stig ⇒ logg + no-op. **VALET, KRYSSET OCH
  UTFÄLLNINGEN FÖLJER NODEN**: varje nod bär ett stabilt id och sina flaggor,
  och den aktuella noden, ankaret, solovalet och ett drag håller fast vid
  id:n. **FLERVALET** (`setMultiSelect`) är listvyns: spannet i SYNLIG
  ordning, Ctrl+A väljer de synliga, och SOLO VID SLÄPPET (en vald nod väljs
  ensam i `mouseUp`, så hela valet går att dra). Läs `selectedPath()` (den
  aktuella om vald, annars den första i trädordning), `selectedPaths()` och
  `currentPath()`; `setSelectedPath({})` släpper ÄVEN den aktuella (listvyn
  behåller sin). En ihopfällning som gömmer valda noder väljer föräldern och
  flyttar den aktuella dit (`onChange` efter `onCollapse`). **KRYSSEN**
  (`setShowChecks`/`setChecked`/`checkedPaths()`, `onCheck` + `onCommit` med
  stigen i `changedPath()`) är OBEROENDE — ingen kaskad — i ListBoxens mått
  efter pilen: zonen vänder utan att flytta valet, Space vänder den aktuellas
  (också i enkelval, före valet). **TEXTFÄRGEN** (`setNodeColor`) gäller
  TEXTEN på en omarkerad rad — markeringens färg vinner, inaktivt `textDim`,
  pilen och bocken följer temat. **AKTIVERINGEN** är listvyns `onActivate` på
  den AKTUELLA noden (en nod med barn fälls också, PILEN aktiverar aldrig;
  Enter bara med `onActivate` satt, som listvyns). **SNABBMENYN** (REQ3):
  `pathAt(localX, localY)` (`{}` utanför raderna) och `contextPath()` (noden under
  det senaste högertrycket, hållen vid ID — `{}` inaktiverat eller borta);
  högertrycket väljer som ListBoxens. Autoskrollen under ett drag är basens.
  **DRAG-AND-DROP är opt-in** (`allowDrag`/`allowDrop`): från en VALD nod HELA
  valet, beskuret till sina översta noder, annars den tryckta — aldrig från
  pilen, rutan eller remsan (`canDragFrom`); lasten är stigarna med `;` i
  `kNodeFormat` och texterna i `text/plain`. Platsen räknas ur `treeDropEdge`
  (före/efter/in i; under en utfälld nod först bland barnen), och trädet ritar
  sin linje (`ownsDropIndicator`). **APPENS GODKÄNNANDE** (REQ2_TRAD_LISTOR):
  trädet FÖRESLÅR `Move`, `onDragOver` (också vid inträdet) kan neka, och
  `onDrop` körs FÖRE flytten — `None` nekar, `handled` = appen flyttade själv —
  annars flyttar privata `moveIds` (allt validerat FÖRE mutation) till LUCKAN
  appen såg: `DropSpot` håller förälderns och grannarnas ID, löst vid varje
  `dropParent()`/`dropIndex()`; en lucka eller dragen nod som försvunnit ger
  `None`. `draggedPaths()` gäller från `onDragStart` (`isDragSource`). En
  `Move` till ett ANNAT mål tar bort alla. **SLÄPPREGELN PER NOD** (post 49,
  `setAllowDropInto`, femte fältet): ett LÖV för släppet — före/efter går, "in
  i" aldrig, och `dropParent()` pekar aldrig på en nod som säger nej.
  **KOLUMNERNA** (REQ8 13) är basens (inget huvud utan kolumner); kolumn 0
  är nodens text, 1… `TreeNode::cells`/`setCell(stig, kolumn, text)`, och
  syskonen SORTERAS när `rows()` byggs — stigarna är appens. **BREDD EFTER
  INNEHÅLLET** (REQ8 8): `cellContentW` mäter de SYNLIGA raderna (kolumn 0 =
  indrag + pil + ruta + ikon + text, 1… cellerna); `rowsChanged`, `setNode`,
  `setCell` och `setShowChecks` anropar `contentChanged()`, och
  `mouseDoubleClick` frågar `headerDoubleClick` FÖRST.
- **`column_panel.h`** — `ColumnPanel : Panel`, KOLUMNHUVUDET som delad bas
  (REQ8 13) för `Table` och `TreeView`; registreras INTE, konstruktorn är
  skyddad. Den äger KOLUMNERNA (aggregatet `Column` {text som `TrText`, bredd
  i logiska px, justering, rubrikjustering} — `Table::Column` är samma typ,
  `addColumn`/`setColumn`/`setColumns`/`setColumnWidth`), sorteringens
  TILLSTÅND (`setSortColumn`/`setSortAscending`/`setSortable`), huvudets
  geometri och ritning (`headerRect`, `scrollViewport`, `columnX`/`columnW`,
  `drawHeader`) och huvudets mus: `headerMouseDown` (remsa först, avdelaren,
  rubriken), `mouseDrag` (avdelaren, aldrig smalare än `columnMinW`),
  `mouseUp` (sorterar när trycket släpps över SAMMA rubrik) och `cursorAt`.
  VAD som sorteras är subklassens (`orderChanged`, `headerSorted`,
  `hasHeader`; den delade `compareNatural`).
  **HUVUDET PÅ FLERA RADER** (post 52): `Column::headerAlign` (fjärde
  fältet; utelämnad = cellernas `align`), `setHeaderLines(n)` (`tableHeaderH`
  + (n − 1) radhöjder; 0 = AUTO, cachat mot font, skala, luft och en
  generation) och `setHeaderVAlign` — rubriken bryts av `Draw::elideLines`,
  och AUTO räknar utan sorteringspilen. **FYLLKOLUMNEN** (`setFillColumn`,
  REQ3, −1 = ingen) tar resten av bredden; dess avdelare går inte att greppa.
  **KOLUMNBREDD EFTER INNEHÅLLET** (REQ8 8): `Column::autoWidth` (−1, varje
  negativ bredd) gör kolumnens bas till `fittedW` — rubrikens bredaste rad
  (pilens plats alltid reserverad när huvudet är sorterbart) och kroken
  `cellContentW` (subklassens, utan luft), med `cellPadX` på båda sidor och
  minst `columnMinW` — cachat per kolumn i `FitCache` mot font, stil, skala,
  sorteringstillstånd och `fitGen_` (`contentChanged()`: subklassen anropar
  den när det den mäter ändras). `headerDoubleClick` (anropas FÖRST i
  subklassens `mouseDoubleClick`) skriver `fittedW` som en FAST bredd på en
  avdelare och avslutar det drag det andra trycket började. **FASTA FÖRSTA
  KOLUMNEN** (REQ8 7): kroken `frozenW` (0 i basen — trädet) — `scrollViewport`
  börjar till höger om den, `headerCellRect` rullar den inte, `dividerAt` hoppar
  över kanter under den och `drawHeader` klipper de rullande rubrikerna mot
  ytan till höger och ritar den fasta sist; `drawHeaderSurface` och
  `headerLineColor` delas med `Table::drawRowHeaders`.
- **`table.h`** — `Table : ColumnPanel`, den DELADE maskinen bakom ListView
  och DataGrid (post 17/24; oregistrerad, skyddad konstruktor): RADERNA som
  data (`Table::Row` {celler som `std::string`, bild i första kolumnen,
  textfärg}), deras sortering och radgeometrin; subklasserna äger sina event
  och hur raderna ritas runt valet (VARFÖR en
  bas och inte `DataGrid : ListView` — två `onChange` med var sitt prejudikat
  — står i headern). **RADENS TEXTFÄRG** (`setRowColor`/`rowColor`) är appens
  och ligger i RADEN, så den följer sorteringen; `setRow` nollar en
  utelämnad, `setCell` lämnar den. **CELLENS FÄRGER** (REQ8 6,
  `setCellColor(rad, kolumn, bakgrund, text)` + getterna `cellBackground`/
  `cellTextColor`) är radfärgens finare form: parallell lagring per rad
  (`cellColors_`, sorteringsnycklarnas form), `setRow`/`setRows` nollar,
  `setCell` lämnar, kolumnen behöver inte finnas än. Subklasserna ritar dem
  genom `drawCell`/`drawCells(…, fg, appColors)`: med `appColors` (= aktiv
  tabell och OMARKERAD rad/cell) är `fg` temats färg, bakgrunden fylls först
  och texten är `appTextColor(rad, kolumn, tema)` = cellens, radens, temats;
  utan är `fg` färdig (markeringens eller `textDim`) — markeringen vinner.
  Kroken `rowReplaced(rad)` säger till när `setRow` bytt en rad (listvyns
  väntande soloval). **RADRUBRIKER** (REQ8 7, `setRowHeaders`, setter-fält):
  `frozenW` = kolumn 0:s bredd, `cellRect` rullar den inte, `contentExtent`
  är de rullande kolumnernas, `hitPosition`/`columnAt` räknar remsan som
  radens del, och `drawRowHeaders` (en gång per ritning, bredvid
  `drawHeader`) ritar remsan med huvudets yta och hårlinjer, namnen genom
  `drawCell(…, false)` och subklassens märken i `drawRowHeaderMarks`
  (listvyns kryssruta, Minimalistics valstreck). Remsan tonas aldrig av val,
  hover eller appfärger. **BREDD EFTER INNEHÅLLET** (REQ8 8): `cellContentW`
  mäter ALLA radernas text (+ zonen och ikonen i kolumn 0); `addRow`/`setRow`/
  `setRows`/`setCell`/`setIconSize` anropar `contentChanged()`.
  **KOLLEKTIONSVOKABULÄREN:** `addColumn`/`setColumn`/`setColumns` och
  `addRow`/`setRow`/`setRows` (tilldelning — `setColumn` utan bredd ger
  standardbredden, `setRow` släpper radens nycklar men behåller valet), plus
  `setCell(rad, kolumn, text)` (fyller ut en kort rad; kolumnen behöver inte
  finnas än — satsordningen i en `.glazeui` ska inte avgöra),
  `setColumnWidth(index, bredd)` (dragets programmatiska tvilling) och
  `setSortKey(rad, kolumn, double)`. Utanför ⇒ `gfx::log` + no-op.
  **INDEXEN ÄR APPENS:** sorteringen ändrar BARA visningsordningen
  (`displayRow`/`rowAtDisplay`, en lat cache), så `setRow(3, …)`, valet och
  den aktuella raden följer RADEN och inte platsen. **TABELLEN SORTERAR
  SJÄLV** (beslutet mot WinForms `ColumnClick` i headern; `setSortColumn`/
  `setSortAscending`/`setSortable` är setter-fält). Jämförelsen är
  NATURLIG (siffror som tal, ASCII-versaler vikta, stabil i båda
  riktningarna), och en cell med SORTERINGSNYCKEL jämförs numeriskt —
  vägen för "12 kB" mot "1,2 MB" och datum i språkets form; ingen
  användarkod körs under en sortering, därför får den vara lat. Radernas
  geometri är EN sanning i skyddade metoder (`rowRect`, `cellRect`, träffarna
  `positionAt`/`columnAt`) och ritdelen `drawCells` delas; huvudet är basens.
  Radens mått är krokar (`baseRowH`/`cellPadX` — listradens som
  standard; `rowH` lägger ikonstorleken ovanpå), och RADENS IKON (`iconSide`/`drawIcon` — standard är radens bild
  i sin storleksklass, pixlar i `iconFallback`; filväljarens interna lista
  ritar mappar och filer med `Draw::icon` i radens textfärg i stället).
  **ZONEN FÖRE IKONEN** (REQ2_TRAD_LISTOR): kroken `leadW(c)` (0 som standard
  — datagriden, filväljaren) och `leadZone(c, plats)` (cellens luft in i
  kolumn 0, radens höjd — EN sanning för ritning och träff); `drawCell`
  flyttar ikonen och texten förbi den (listvyns kryssruta). **`setIconSize`** (0 =
  AUTO): varje rad reserverar ikonkolumnen och blir minst ikonen + stilens luft.
  **`rowAt(localX, localY)`** (appens index, −1 i huvudet och remsorna) delar
  `hitPosition` med listvyns högertryck. SKRIVNA BEGRÄNSNINGAR: ingen
  autostorlek, ingen språkmedveten kollation (å/ä/ö jämförs bytevis — en nyckel
  bär rätt ordning), kolumnerna kan inte flyttas/döljas.
- **`list_view.h`** — `ListView : Table`, LISTVYN (wxListCtrl i report-läge,
  WinForms ListView Details) — filväljarens lista. **VALET:** enkelval som
  standard; `setMultiSelect(true)` ger Utforskarens flerval — Ctrl+klick
  vänder (Cmd på macOS, båda räknas överallt), Shift+klick väljer spannet
  från ANKARET, Shift+pil utökar, Ctrl+pil flyttar utan att välja och
  Ctrl+Space vänder, Ctrl+A väljer alla. Spannet går i VISNINGSORDNINGEN.
  Musens modifierare läses ur `AppWindow::modifierKeys()`. Läsvägarna är
  appens radindex: `selectedIndex()` (den aktuella om vald, annars den första
  valda), `selectedRows()` (visningsordning), `isSelected`, `currentRow()`;
  settrarna fyrar ALDRIG. **EVENTEN:** `onChange` exakt en gång per
  användarhandling som ändrade valet och `onCommit` direkt efter,
  `onActivate` (`ActivateArgs`) vid dubbelklick på en rad eller Enter (Enter
  bara med `onActivate` satt — annars når den standardknappen, B9). De
  skyddade `selectFromUser(plats, mods, frånTangent)`,
  `toggleCheckFromUser(rad)` och `activateFromUser()` är de enda som fyrar.
  `setProp`: selectedIndex, multiSelect, showChecks + Tables. **SOLO VID
  SLÄPPET**: ett tryck utan modifierare på en VALD rad i flerval gör den bara
  aktuell (`pendingSolo_`) och väljer den ensam i `mouseUp` — nollat av nästa
  tryck, `rowsReplaced`/`rowReplaced` och `dragStart`. **KRYSSEN**
  (REQ2_TRAD_LISTOR, ListBoxens ord): `setShowChecks`/`setChecked`/`isChecked`/
  `checkedRows()`/`changedRow()`/`onCheck` (+ `onCommit`), per RAD i `checked_`
  (följer sorteringen; `setRows` nollar, `setRow` behåller), rutan i
  `Table::leadZone` med TEMATS pennor. Zonens tryck rör varken valet, den
  aktuella raden eller solovalet; Space kryssar den aktuella (också i
  enkelval), dubbelklick i zonen aktiverar inte, och `canDragFrom` nekar
  huvudet, remsorna och zonen. `contextRow()` och högertrycket (REQ3) har
  ListBoxens regel. Med **radrubriker** (REQ8 7) står rutan i den fasta
  remsan: `drawRowHeaderMarks` ritar den i rubrikens färger (remsan tonas
  aldrig av valet), `onCheckZone` räknar radernas yta från innehållsytans
  vänsterkant, och `setShowChecks` anropar `contentChanged()` (zonen är
  kolumn 0:s innehåll för `autoWidth`). `mouseDoubleClick` frågar
  `headerDoubleClick` FÖRST (en avdelare anpassar sin kolumn). INTE HÄR:
  typeahead, kryss i huvudet, gummibandsmarkering.
- **`data_grid.h`** — `DataGrid : Table`, DATAGRIDDEN (post 24, 2026-10-07;
  wxGrid, WinForms DataGridView): listvyns tabell med REDIGERBARA celler
  (varför den inte ärver `ListView` står vid `table.h`). Den AKTUELLA cellen
  behåller markeringens färger, resten bär appens — cellens, annars radens
  (`drawCell(…, appColors)`, table.h). Med **radrubriker** (REQ8 7) är kolumn
  0 en rubrik: `editable(0)` är false, `firstColumn()` håller den aktuella
  cellen borta från den (klick på ett namn behåller kolumnen), och
  `drawRowHeaderMarks` ritar Minimalistics radvalsstreck i remsan.
  **EDITORERNA ÄR RIKTIGA BARN, exakt två**: privata nästlade `TextEditor : TextInput` och
  `ChoiceEditor : Dropdown` (talfältets `Field`-prejudikat — en tangent går bara till den
  FOKUSERADE widgeten), ägda, dolda i vila och oåtkomliga för `unadopt` (`canReleaseChild`).
  PLACERINGEN är datagridens (`placesChildren`/`placeChild`): editorn läggs på cellen ur
  `Table::cellInContent`, samma formel som ritningen, vid varje `resolve()` — den följer scroll,
  sortering, en dragen kolumnbredd och ett högt huvud, och står i KOLUMNENS justering
  (`Column::align` → editorns `setAlign`, läst när redigeringen börjar). ETT tabbstopp
  (`focusesChildren`). **KOLUMNTYPEN HÖR TILL INDEXET**: `setColumnEditor(kolumn,
  Editor::Text|Choice|None)` + `setColumnChoices(kolumn, lista)` överlever `setColumn`/
  `setColumns` som `sortColumn` gör. `None` och en val-kolumn UTAN val går inte att redigera och
  ritas som ett skrivskyddat fält. **REDIGERINGEN**: börja med dubbelklick på den aktuella
  cellen, F2 eller att man BÖRJAR SKRIVA (tecknet ersätter texten — bara fritext); spara med
  Enter (+ ner en rad, Shift upp), Tab (+ en cell), Upp/Ned i fältet (+ en rad) eller
  fokusförlust; Escape avbryter i ETT steg, även med vallistan öppen. En val-cell öppnar listan
  direkt, och ett NYTT val sparar och avslutar. **NAVIGERINGEN** utan redigering: pilarna, Tab
  cell för cell med radbyte (och UT ur datagriden i första respektive sista cellen), Home/End
  (radens), Ctrl+Home/End (tabellens), PageUp/PageDown och Enter/Shift+Enter; Ctrl+Tab tackar
  den nej till. **EVENTET** är `onChange` med `CellChangeArgs { row, column }` (ärver
  `ChangeArgs`), EN gång när ANVÄNDAREN sparat en cell vars text ändrades; raden är appens index
  och värdet läses ur sender (`cell(e.row, e.column)`); **`onCommit`** med SAMMA args direkt
  efter. En sparning för att fokus LÄMNADE editorn postas (`Widget::postEvent`) och slopas om
  datagriden dött eller raderna bytts. **PROGRAMMATISKT**: `setCurrentCell`/`currentColumn` och
  `beginEdit`/`commitEdit`/`cancelEdit` (`commitEdit` fyrar); `setRows`/`setColumns`/
  `setCurrentCell` AVBRYTER en redigering, `setRow`/`setCell`/`addRow` gör det INTE —
  redigeringen följer RADEN, en medveten avvikelse från PropertyGrid. Radens mått är GRIDRADENS
  (`gridRowH`/`gridPadX` via `baseRowH`). **RADVALET**: `SelectionMode::Cell`/`FullRow` +
  `setMultiSelect`, listvyns ord per RAD och ett EGET event, `onSelectionChange` (`onChange` är
  cellernas). `setProp`: selectionMode, multiSelect + Tables. SKRIVNA BEGRÄNSNINGAR i headern.
- **`panel.h`** — `Panel`, CONTAINER-primitivet: barn i förälder-relativa
  koordinater, `clip`, `border`, scroll (`setScroll`; mushjulet scrollar
  vid överflöd), **`innerBounds(Ctx)`** (innehållsytan i LOGISKA px —
  SANNINGEN som ritningen och remsgeometrin räknar i) och
  **`innerRect(Ctx)`** (dess FYSISKA projektion — klipp/childArea/hit-test
  och subklassernas geometri), `scrollRange(Ctx)` (spelrummet per axel),
  **`showScrollbars`** — overlay-lister OVANPÅ barnen vid överflöd; deras
  tumme ritas av privata `overlayThumb()` PER UI och är ett KONTRAKT med
  `Scrollbar` (samma färg för samma tillstånd).
  `contentExtent` är virtuell (PropertyGrids rader är data). **`scrollViewport`**
  (virtuell) är den del av innehållsytan som SCROLLAS — default `innerBounds`;
  `Table` svarar med ytan UNDER kolumnhuvudet, och spelrummet, remsornas ränna
  och tummens sida följer med. **`dragScroll`** (REQ3): autoskrollen inom
  `dragScrollZone` från rullningsytans kant, åt ett håll med spelrum — varje
  subklass ärver den; skyddade `inRowColumn` är radlistornas träffväg (remsan och
  sidramen är ingen rad). `setProp`: border/clip.
- **`group_box.h`** — `GroupBox : Widget`, GRUPPRAMEN (post 13; wxStaticBox,
  WinForms GroupBox): en ram med rubriken I KANTEN — linjen
  bryts där rubriken står (`Draw::frameGap`). **INTE en Panel** (beslut): den
  scrollar aldrig, klipper alltid, har alltid sin ram, och innerytan har
  rubrikbandet upptill i stället för en ramtjocklek. Rubriken är `text`
  (TrText + `setTextToken`, inget `title`-alias). Geometrin är tre privata
  metoder som tar ytan `b` som argument — ritningen skickar `c.bounds()`,
  containerkrokarna `Draw::bounds(c, resolve(c))` — så det man ser och det
  barnen placeras mot inte kan glida isär. **Tom rubrik flyttar ingenting**:
  rubrikbandet reserveras ändå (WinForms gör likadant). **AUTOSTORLEK UR
  BARNEN** — kompositstödets opt-in, gruppramen är dess första användare:
  en AUTO-axel blir `childrenExtent()` (REQ7_LAYOUT: också barn nere till höger
  och spända) + luften, minst så bred som rubriken.
  Radien är **`groupRadius`** och inte `panelRadius` (den är 0 och hör till
  det parkerade utseendepasset). `enabled` kaskadar inte (Panel-regeln).
- **`splitter.h`** — `Splitter : Widget`, DELAREN (post 15; WinForms
  SplitContainer, wxSplitterWindow). Rutorna är de TVÅ FÖRSTA
  barnen (ett tredje placeras inte) och delaren PLACERAR dem själv
  (`placesChildren`, Bar-prejudikatet — barnens x/y/dock ignoreras).
  Setter-fält: `vertical` (default true = stående skikt), `fixedPane`
  (`Pane::First`/`Second`), `paneSize`, `firstMin`/`secondMin`.
  **`paneSize` MÄTER DEN FASTA RUTAN och inte avståndet från vänster** —
  det skrivna beslutet mot WinForms `SplitterDistance`: layouten blir
  TILLSTÅNDSLÖS, och samma tal betyder samma sak oavsett delarens storlek.
  ≤ 0 = AUTO (lika delning). När båda minimimåtten inte
  ryms vinner den fasta rutans. Skiktdraget FÖRHANDSVISAS: `paneSize`
  skrivs först vid släppet (`mouseUp`), och `onChange` fyras EN gång då —
  aldrig av settrarna, och inte när fokusförlusten avbryter draget (läget
  går tillbaka av sig själv, läst ur `pressed()`). Inget tabbstopp, ingen
  hopfällning (en dold ruta lämnar ett hål). Ingen autostorlek
  (Panel-beslutet). Skiktets ritning och greppmått DELAS med dockningen
  (`src/sash.h`).
- **`dock_host.h`** + **`dock_panel.h`** — DOCKNINGEN (post 16; wxAuiManager,
  VS-verktygsfönster). `DockPanel : Widget` är en rubrikrad (titel `text`; menyknapp ▾, KNAPPNÅL
  och stängknapp som DATA) över sitt innehåll; placeringen är `dock`
  (default `Left`) och `width`/`height`, AUTO = `dockDefaultSize`; `setImg`,
  `setMinimumSize`/`setMaximumSize` (logiska px, 0 = ingen gräns),
  `setClosable`, `setPinned`, `activate()`; rubrikmenyn byggs vid öppning
  (ramverkets 162–164). `DockHost : Widget` håller modellen
  KANT → REMSA → CELL → FLIK (src/dock/): flikgrupper med flikrad nederst och
  staplar (`dockPanel(p, mål, var)`), remsor i en ordning användaren kan
  ändra (`dockPanel(p, kant)` = ytterst; barnordningen är z-ordningen och
  flyttas aldrig), och allt annat är HUVUDYTAN. En INFÄLLD eller ONÅLAD panel
  är ett HANDTAG utanför panelremsorna (stående: ikon eller första tecknet,
  titeln via `toolTipAt`); en onålad fälls ut som OVERLAY (`frontChild`)
  ovanpå huvudytan utan att trycka undan den, högst en åt gången, och fälls in
  när pekaren (geometriskt, `AppWindow::mousePosition`), fokus, ett tryck och
  popuperna alla lämnat den (en upprepad `glaze::Timer` var
  `dockHideDelayMs`) eller med Escape. Overlayn och en remsa som kommer och
  går med sin enda panel GLIDER (`dockAnimMs`, stegad en gång per bild i svansen).
  Inaktiva flikar och infällda paneler DÖLJS INTE — tom yta, `showsChildren()
  == false`; `hide()` är stängd, och fokus lämnar dem också när appen gör
  ändringen (utan stopp: `AppWindow::clearFocus`). VERKTYGSFÄLT (E4b) står i
  BAND ytterst (`band()` och `dock` är fälten), dras i sitt grepp och kan
  FLYTA (DOCKNING_DRAG: rubrik och fast storlek, `ft=` i strängen). Skikten
  har förhandsvisning (cellernas = vikter) och SNAPP (`dockSnapZone`; under
  golvet fälls panelerna in); dragen LOSSAR objektet (levande drag) och visar en
  SIMULERAD platshållare. Värden är ETT tabbstopp med handtag eller flikgrupper,
  och ett tryck på en flik väljer också tangentbordets post (ringen följer).
  `layout()`/`setLayout()` är strängen v2 (`src/dock/dock_string.h`) — atomisk,
  okända namn hoppas över; overlayn är drifttillstånd (skrivs infälld, inget
  `onChange`). `onChange` på panelen OCH värden, panelens först.
  `dockPanel`/`setLayout`/`activate` i en handskriven `.glazeui` står EFTER
  trädbygget. FLYTANDE (E3): `setFloating` lyfter panelen till ett EGET fönster
  som värdens fönster äger (tillståndet direkt, flytten mellan träden i pumpens
  svans — en öppen popup i panelen skjuter upp den; dold start skapar fönstret
  dolt, offscreen aldrig); `setFloatable`, `setFloatingBounds` (logiska px) och
  `f=` i strängen; rubriken utan knappnål, dubbelklick och ▾ Flytande/Docka;
  levande drag, inte på Wayland (`isFloatDragSupported()`); fönstrets Alt+F4
  stänger panelen, ägarens kaskad rör den inte.
- **`tab_control.h`** + **`tab_page.h`** — FLIKARNA (post 19, 2026-10-08;
  WinForms TabControl/TabPage, wxNotebook). `TabPage : Panel` är en sida vars
  `text` (TrText + `setTextToken`, inget `title`-alias) är FLIKENS rubrik;
  sidan ritar ingen egen yta (bara sin ram när `setBorder` är satt) och
  `setText` invaliderar FÖRÄLDERN, där rubriken ritas och flikens bredd
  räknas. `TabControl : Widget` har SIDORNA SOM RIKTIGA BARN i adopt-ordning
  — samma skäl som delarens rutor: ett kedjat `flikar.page(0).adopt(x)` går
  inte att emittera, `flikar.adopt(sida); sida.adopt(x);` är trädbyggets egna
  satser. Ett barn som inte är en `TabPage` loggas, döljs och placeras inte.
  **SIDORNAS VOKABULÄR** (FAS1_B_WIDGETS): `addPage(text)` → `TabPage&`,
  `setPage`, `setPages` (atomisk) — i en handskriven `.glazeui` EFTER trädbygget.
  Flikkontrollen PLACERAR (`placesChildren` — den valda sidan får ytan under
  flikraden, innanför ramen, de andra en tom rekt) och KLIPPER sina barn, och
  den ÄGER deras SYNLIGHET: den valda syns, de andra är `hide()`-dolda, så en
  dold sidas kontroller varken ritas, träffas eller står i tabbordningen.
  **VALET:** `setSelectedIndex` LAGRAR talet som det är och `selectedIndex()` läser det KLAMPAT
  (−1 utan sidor) — talfältets `value()`-prejudikat — så `flikar.setSelectedIndex(1);` FÖRE
  trädbyggets adopt gäller när sidan kommer; negativt ⇒ logg + no-op. `page(i)`/`pageCount()`/
  `pageIndex()`/`selectedPage()` är läsvägarna. **BORTTAGNINGEN** är trädets `unadopt`/
  `releaseOwned`, och VALET FÖLJER SIDAN (den valda borttagen ⇒ grannen till höger, till vänster
  när den var sist — Qt:s `SelectRightTab`; inget event) genom basens `childAdded`/`childRemoved`.
  **ANVÄNDAREN** byter flik med ett tryck (väljer vid TRYCKET, som Windows) och tangenterna —
  flikkontrollen i fokus: Vänster/Höger (stannar i kanten), Home/End, Ctrl+Tab/Ctrl+Shift+Tab och
  Ctrl+PageDown/PageUp (runt om); fokus i sidan: Ctrl-varianterna, genom basens
  `descendantKeyDown`, och fokus flyttar då till den nya sidans första tabbstopp (flikkontrollen
  när sidan saknar ett). Flikkontrollen är ETT tabbstopp och sidans kontroller följer efter den.
  `onChange` fyras EN gång per användarbyte, aldrig av sättaren. **FOKUS STANNAR ALDRIG I EN DOLD
  SIDA:** ett programmatiskt byte som gömmer sidan med fokus flyttar fokus till flikkontrollen.
  **STÄNGBARA FLIKAR** (`setClosable`): ett ✕ längst till höger i fliken (synligt på den valda
  och den pekaren står över), ett klick fyrar `onTabClose` med `TabCloseArgs { index }` —
  flikkontrollen tar ALDRIG bort sidan själv (appen gör `unadopt` i handlern). Ett tryck på ✕
  väljer inte fliken. **DEN FULLA RADEN:** flikarna krymper lika långt mot `tabMinW` (ellips),
  och räcker inte det förskjuts raden så att den VALDA syns hel — ingen rullning med pilknappar.
  `setProp`: selectedIndex, closable (sidan: text + Panels). SKRIVNA BEGRÄNSNINGAR: ingen
  autostorlek (Panel-beslutet), flikarna står alltid överst, inga ikoner i flikarna, ingen
  omordning med musen, ingen tooltip på själva fliken.
- **`property_grid.h`** — `PropertyGrid : Panel`: tvåkolumners egenskaps-
  lista (namn läs-bar | värde). Rader är DATA (`Row` {name, value,
  choices, readOnly, tag — OPAK}); EN hostad editor (privata `TextEditor :
  TextInput`) + EN vallista (`ChoiceEditor : Dropdown`) flyttas till aktiv cell. `rows()` är en
  LÄSVÄG (0.1.2): raderna ändras bara via `setRows`/`addRow`/`setRow`.
  **REDIGERINGEN COMMITTAR som DataGrids** (0.1.2):
  editorn öppnas med ALLT markerat; Enter, Tab/fokus bort (postat, slopas
  efter `setRows` och `setRow` på raden) och ett klick i gridden sparar —
  klicket på en annan rad sparar FÖRST, direkt, och öppnar sedan den nya, så
  en omräknande handler är klar innan nästa editor finns; Escape avbryter.
  `onChange` + `onCommit` EN gång var, bara vid ett ändrat värde; läs
  `changedRow()` ur sender. Vallistan sparar vid valet (+ `onCommit`), och
  Enter/Escape avslutar den i ett steg — konsumerade, aldrig dialogens.
  `setRows`, och `setRow` på den redigerade raden, AVBRYTER. Skriven gräns:
  gridden är inget tabbstopp, så fokus står kvar i den DOLDA editorn när den
  stängs.
  `nameWidth` logiska px (0 = stilens `gridNameW`). Privat cellgeometri
  (`nameW`/`rowBounds`/`valueBounds`) i LOGISKA px; cellträffen
  PROJICERAR dem. GENERISK: choices utifrån, inga builder-typer.
  **STYCKEFORMEN + `setProp`** (REGEL8_PREVIEW):
  `addRow(name, value = "", readOnly = false, tag = "")` ·
  `setRow(index, …)` · `setRows(vec)` — `addX`/`setX(index, …)`/
  `setXs(lista)`, samma vokabulär som `ListBox`/`Dropdown`s
  `addItem`/`setItem`/`setItems`. **`setRow` ERSÄTTER hela raden**, också
  utelämnade fält (`Items[i] = …`). Index utanför intervallet ⇒ logg + no-op. `choices` uttrycks i
  `setRows`-literalen och har medvetet ingen egen metod. `setProp`:
  nameWidth (basens nycklar tar `Panel`).
- **`scrollbar.h`** — `Scrollbar`: fristående rullningslist.
  `orientation`, `min`/`max`/`value`/`page` (valfria enheter), `onChange`
  (live) + `onCommit` (0.1.2 — släppet efter tumdrag/rännklick, varje
  hjulsteg; `mouseUp`-kroken); tumdrag, rännklick sidvis, hjul. Tumgeometrin är DELAD
  (`Draw::ScrollMetrics` + `Draw::scrollThumb`/`scrollThumbDraw` — Panels
  overlay-lister ritar samma pixlar). `setProp`: orientation/min/max/
  value/page. `value()` KLAMPAS VID LÄSNING (FAS1_B_WIDGETS, talfältets regel).
- **`separator.h`** — `Separator`: skiljelinjen. `vertical`; `contentSize`
  ger tvärs streck + luft (`separatorThickness` + 2 × `separatorInset`) och
  längs stilens `separatorDefaultLen` — dock/anchors sträcker den. I en `Bar`
  står den TVÄRS baren vilket `vertical` den än har och är tunn på båda
  axlarna (DOCKNING E4b, D15). Inget tabbstopp.
- **`picture.h`** — `Picture`: bildrutan. `img` (`ImageSource` som Button)
  + `fit` (Fit/Fill/Stretch/Center). Privat `imageBounds()` (målrecten i
  logiska px ur `Draw::imageSize`) — ritningen och klippbeslutet läser
  samma uträkning. Autostorlek = bildens naturliga mått = pixlarna / skalan
  (en Bitmap blir logiskt mindre på högre skala; mätningen laddar inte upp). NEDSKALAD (REQ8 23): ritas bilden mindre än det naturliga måttet går den genom `Draw::glyphResampled` (ytans medelvärde), annars `Draw::glyph`.
- **`link.h`** — `Link`: `text` + `url`; fokuserbar, Space/Enter. `clicked`
  kör onClick FÖRE `gfx::openUrl(url)` (tom url = ren klickhanterare).
  Privata `textBounds()` (understrykningen följer TEXTENS bredd) och
  `linkColor()` (temats **link/linkHover** — en länk är inte `accent`).
  Pekaren är HANDEN (`cursorAt`, post 15) — `TextInput` svarar med I-balken.
- **`radio.h`** — `Radio`: `text`/`checked`/`group`, `onChange` + `onCommit`
  (bara den som BLEV vald). Gruppen = SYSKON med samma `group` (tom = alla) —
  ETT tabbstopp (`focusable()` dynamiskt), pilarna flyttar valet. ENVALET ÄR
  HÄRLETT (🐛 59): egen flagga + STÄMPEL, `isChecked()`/ritningen visar den
  senast satta i gruppen — samma läge vare sig `.glazeui`:s sättare körs före
  trädbygget (appen) eller efter (previewn); användarens val släcker syskonen.
  **`setColor`** som kryssrutans (REQ3 — ringen och pricken följer temat).
- **`slider.h`** — `Slider`: `min`/`max`/`value`/`step`/`vertical` +
  `onChange` (LIVE under draget; läs värdet ur sender) + `onCommit` (0.1.2 —
  vid släppet om värdet ändrats sedan förra commiten, och vid varje
  tangent-/hjulsteg efter `onChange`). Privata `trackBounds()`/
  `thumbBounds()` i logiska px matar BÅDE ritningen och musmappningen
  (`Draw::localX/localY`); klamp + kvantisering på ETT ställe (`userSet`).
  **HJULET KRÄVER FOKUS** (post 45, samma regel som `SpinBox`): med fokus
  flyttar ett hjulsteg lika långt som en piltangent (`effectiveStep`), utan
  fokus bubblar det. `value()` klampas vid läsning som `Scrollbar`s. Grindar:
  `range_gate` (klampningen) och hjulfallen i `spinbox_gate`.
- **`progress.h`** — `Progress`: `min`/`max`/`value` + **`setIndeterminate`**
  (SETTER så bytet invalidaterar). Obestämt läge animerar genom att
  `draw()` invalidaterar SIG SJÄLV — **aldrig via fönstrets
  `setAnimating`**, som är en delad bool utan ägarskap (widgeten kunde
  annars slå av appens egen animering); dolt ⇒ ritas aldrig ⇒ animationen
  stannar av sig själv. Privata `fillBounds()`/`chunkBounds()`
  (triangelvåg ur `gfx::ticksMs`, modulo i heltal).
- **`bar.h`** — `Bar : Panel`, flex-RAD-motorn (grunden för TopBar/
  StatusBar/MenuBar/ToolBar): barnen packas i ADD-ordning, mätt bredd ×
  barens fulla höjd — STÅENDE (`isVertical()` = `dock` Left/Right, D15) i en
  egen `placeChildVertical`, mätt höjd × full bredd; spacers delar luckan
  efter vikt. `addSpacer()`; `height` 0 = stilens `barHeight`. **`setPlain(true)`** (REQ8 25): REN RAD — ingen yta och ingen hårlinje, barnen behåller sitt mätta tvärmått (centrerat; utan eget mått fyller de). Flexen är
  SKILD från Dock. **`childGap(c)`** (protected virtual, post 11) är luckan
  MELLAN två SYNLIGA barn i logiska px — 0 för en vanlig bar, `toolGap` för
  `ToolBar`; luckorna räknas in i flexens FASTA summa, och **`inFlow(barn)`** (REQ8 9) är vilka
  barn flexen packar (förval de synliga). Privat `innerEdge()`
  ritar alla tre familjernas hårlinje mot INNEHÅLLET (`dock` avgör sidan;
  ingen när den virtuella `drawsInnerEdge()` svarar false) i rollen `frame`,
  och `isVertical()` är VIRTUELL; privat `fillBody()` ritar YTAN och rundar
  de ÖVRE hörnen enligt `topRadius()` (TopBar överrider, så namnlisten följer
  ett rundat fönsterhörn). `setProp`: height. **`innerEdgeInsets(c)`**
  (virtuell, PROGRAMIKON B3) är hårlinjens luckor — `{0, 0}` för en vanlig
  bar; TopBar överrider.
- **`spacer.h`** — `Spacer`: grow-vikt-barnet (default 1) som passivt
  sväljer barens lediga utrymme; `grow = 0` + `width` = FAST lucka; tom
  `draw()`. `setProp`: grow.
- **`caption_button.h`** — `CaptionButton`: fönsterknapparna min/max/close.
  `Kind` (Minimize/Maximize/Close) + `Shape`: **Capsule** (HUSETS form och
  default på ALLA tre plattformarna — ETT kluster med rundade ändar i temats
  **capsule-roller**, likadant i alla tre UI eftersom namnlisten är CHROME),
  Glyph (Windows ─▢✕, chrome-rollerna) och Circle (Mac traffic lights,
  traffic-rollerna) — de två sista så att äldre `.glazeui` round-trippar
  (regel 4). `restoreGlyph` är ett setter-fält med `setProp`-gren. Privata
  `drawCapsule`/`drawGlyphButton`/`drawTrafficLight`, `clusterPos()` (fackets
  plats bland de SYNLIGA kapsel-syskonen — avgör vilken ände som rundas) och
  `drawGlyphs(pen, restoreFill)` (TECKNET, delat; `restoreFill` = det BAKOM knappen).
  INTE fokuserbar. `setProp`: kind/shape (`"style"` = äldre alias).
- **`top_bar.h`** — `TopBar : Bar`, den RIKTIGA namnlisten: skapas ALLTID av
  AppWindow vid `Chrome::Custom`; namngivna pekare `titleLabel`/
  `minButton`/`maxButton`/`closeButton` (null när de saknas). Överrider
  `topRadius()` med `AppWindow::cornerRadius()` — namnlisten ligger mot
  fönstrets övre hörn och måste rundas med EXAKT samma tal som bakgrunden,
  annars sticker en fyrkantig remsa ut ur ett rundat fönster. Och
  **`innerEdgeInsets`**: fönstrets EGEN namnlist hoppar över den STORA ikonen.
- **`menu_bar.h`** — `MenuBar : Bar`, MENYRADEN (MENYER, 2026-08-19). Ett
  publikt VÄRDE-FÄLT på `AppWindow` — `window.menuBar` — som skapas med
  fönstret, adopteras LÅNAD i konstruktorn EFTER TopBar och FÖRE
  statusraden, och är DOLD tills `menuBar.show();`. Den ligger i KLIENTYTAN
  (ingen hit-roll i `pushHitRegions`); höjden är `Style::menuRowH` via en egen
  `contentSize()`. **TITLARNA ÄR DATA, inte barn** — som widget hade varje
  rubrik blivit ett tabbstopp —, så `titleRect` är EN sanning för ritningen OCH
  hit-testen, och `focusable()` är false.
  API: `addMenu(titel) -> Menu&` (STABIL referens — menyerna ligger bakom
  `unique_ptr` eftersom `Menu` varken kan kopieras eller flyttas),
  `menuCount`/`menuTitle`/`menuAt`/`clearMenus`, `openMenu`/`closeMenu`/
  `openIndex`, och **`tryShortcut(key, mods)`** som `AppWindow::handleEvent`
  frågar EFTER fokuserad widget och FÖRE Tab. Klick på rubrik öppnar, klick
  igen stänger (samma deferred toggle-ordning som Dropdown), och HOVER på en
  annan rubrik medan en meny är öppen BYTER meny.
  Menyn byggs i BÅDA VÄGARNA (regel 8): STRUKTUREN som platta satser —
  `addMenu(titel)` · `addItem(stig, text, genväg)` · `addSeparator`/
  `addSubmenu`/`addCheck`/`addRadio`, STIGEN är TITELVÄGEN — och HANDLERNA i kod
  via `setHandler(stig, …)` (en TEMPLATE, båda lambdaformerna; previewn utför den
  inte). Träd-API:t (`addMenu -> Menu&`) ger EXAKT samma tillstånd men emitteras inte.
  **STIGEN ÄR SEGMENTERAD** (`addRadio({"Utseende", "Läge"}, "System")`, eller
  med `glaze::tr(…)` per segment — ett `TrText` bär aldrig två id); matchningen
  är TOKEN mot token, rubrikerna `TrText` (`menuTitle()`/`menuTitleToken()`), `/`
  i ett literalsegment AVVISAS, och en tom stig avvisas här (programikonens rot).
  **POSTENS TILLSTÅND** (TRAD_LISTOR_BILDER) har samma platta form på alla
  fyra menybärarna: `setItemIcon`/`setItemEnabled`/`setItemChecked(stig, …)`,
  en implementation i `src/menu.cpp` (`detail::setMenuItem*`).
- **`tool_bar.h`** — `ToolBar : Bar`, VERKTYGSFÄLTET (post 11; wxAuiToolBar, WinForms ToolStrip). Samma remsa
  som menyraden (Bars ritning) med EGNA mått: tjockleken `toolBarH`, luften `toolBarPadX` längs och
  `toolBarPadY` tvärs (egen `childArea()`), luckan `toolGap`, greppet `toolGripW` i ett band i en `DockHost`
  (`Draw::grip`; draget och DUBBELKLICKET går till värden) och överflödsknappen `toolOverflowW`. EGEN
  LÄNGD (D16) när `width` (liggande) eller `height` (stående) ≤ 0; privata `sizeFor(c, vertical,
  withGrip)` ger måtten i en riktning fältet inte står i än (värdens simulering). `setBand`/`band()`
  är bandet. FLYTANDE (T2): `setFloating`/`setFloatable`/`setFloatingPosition` (logiska px) och
  titeln `setText` (tom = ramverkets 166) — det flytande fältet LIGGER (`isVertical()`-override;
  `dock`/`band()` är minnet; bredd ↔ höjd byts vid ett riktningsbyte), har ingen hårlinje och
  `~ToolBar` säger till värden. Dock=Top ur konstruktorn. **BARNEN ÄR RIKTIGA WIDGETS och inte
  data** (`toolBar1.adopt(spara);`); en `Separator` står tvärs baren av sig själv.
  **`setAllowedEdges({…})`** (REQ3, Qt `setAllowedAreas`; förval alla fyra) begränsar ANVÄNDARENS
  drag (`{}` = bara flyta, Fill/None loggas); appen kan alltid. AUTO-tjockleken (`autoThickness`)
  växer med den största synliga verktygsknappsIKONEN (+ 2 × (`toolButtonPadY` + `toolBarPadY`)), så en
  vanlig textknapp ger exakt `toolBarH`. **ÖVERFLÖDET** (REQ8 9, `setOverflow`/`overflow()`, på som
  förval): ryms barnen inte DÖLJS de sista i layouten (`visible` orört) och den privata
  `OverflowButton` — en `ToolButton`, ett ÄGT barn utanför flödet (`inFlow`) som inte går att lossa
  (`canReleaseChild`) — står i änden; ryms de är `placeChild` Bars flex. Menyn byggs vid varje tryck
  ur snittet (`overflowCut`): knappar som poster (`performClick`), rullgardiner som undermenyer genom
  knappens egen meny, kontroller som poster som ger fokus — ett barn i överflödet med fokus VISAS
  (`childFocusChanged` ritar om). `minLength` är bandets krympgolv. `setProp`: text, band, floating,
  floatable, overflow + Bars.
- **`tool_button.h`** — `ToolButton : Widget`, VERKTYGSKNAPPEN (post 11).
  Buttons ord: `text` (TrText, INGET `title`-alias), `img`, `imagePos` (de tre
  lägger om syskonen, `relayoutSiblings`); plus `checkable`/`checked` och
  `onChange`/`onCommit` (växlingen; före `onClick`). Tre saker skiljer den från
  en `Button`: **`focusable()` är false** (inget tabbstopp OCH ingen fokustjuv
  — ett klick på "Fet" lämnar caret i fältet), **platt** (ingen platta i vila)
  och **växlingsbar** (`clicked()` slår om läget, `onChange` FÖRE `onClick`;
  programmatisk `setChecked` fyrar aldrig, `setCheckable(false)` lämnar
  `checked` orört). Inaktiv ikon tonas med opacitet (`content(c, fg)`).
  **RULLGARDINEN** (DOCKNING E4a, REQ1 19): med MENYPOSTER en pil i en egen
  ruta till höger (`toolArrowW` — AUTO-bredden växer, `arrowRect` är ritningens
  och träffens EN geometri), och ett TRYCK öppnar menyn under knappen med
  menyradens toggle — inget `onClick`, ingen växling (`menuPress_`: satt i
  `mouseDown`, läst i `clicked()`). `setSplit(true)` = DELAD: huvuddelen
  klickar och växlar, pildelen öppnar. Menyn har SYSTEMFÄLTSIKONENS vokabulär —
  `{}` som rot, INGET genvägsargument (ingen acceleratorväg frågar en
  knappmeny), `setHandler`, `itemAtPath`, `menu()` och postens tre tillstånd
  (`src/menu_path.h`). **`onDropDown`** fyras när ANVÄNDAREN trycker fram
  menyn, FÖRE öppningen (dynamisk meny); `openMenu()` öppnar utan den. Öppen
  meny = intryckt platta. `setIconSize` som Buttons (en större ikon gör knappen
  och fältet högre). **`performClick()`** (REQ8 9) är klicket utan tryck (`fireClick` — också
  på en knapp med poster), och skyddade **`showsArrow()`** svarar false för överflödsknappen.
  `setProp`: text, img, imagePos, checkable, checked, split, iconSize. Tooltipen är BASENS.
  INTE HÄR: radiogrupper (`checkable` + `onClick`) och en meny som öppnas annat än under knappen.
- **`status_bar.h`** — `StatusBar : Bar`, statusraden: Dock=Bottom i
  INNEHÅLLSYTAN, inga default-barn. Ett publikt VÄRDE-FÄLT på `AppWindow` —
  `window.statusBar` — skapat med fönstret, adopterat i konstruktorn och DOLT
  tills `statusBar.show();`. Höjden är **`Style::statusBarH`** via en egen
  `contentSize()` — `setContentSize` räknar baklänges genom chromet.
  **FÄLTEN** (FAS1_B_WIDGETS): `StatusPanel` i kollektionsvokabulären
  (`addPanel`/`setPanel`/`setPanels` + `setPanelText`), ägda barn av en intern
  typ bland appens egna, hittade bland barnen och följer radens `enabled`.
- **`app_icon.h`** — `AppIcon : Widget`, PROGRAMIKONEN och PROGRAMMENYN
  (PROGRAMIKON, 2026-10-11; "appicon"). Det tredje värdefältet på
  `AppWindow` (`appIcon`), SYNLIGT som förval: ogiltig `icon()` = GlazeUI-
  ikonen, `setSmallIcon` en egen liten, `setNoIcon(true)` ingen alls,
  `setLarge(true)` en BEGÄRAN som gäller med synlig menyrad, `hide()` döljer
  den i fönstret men inte på OS:et. Ett CHROME-rotbarn som TopBar, vänster
  på Windows/Linux, höger på macOS (statiska `onRightSide()`). **`reservedWidth(c,
  Row)`** är geometrins ENDA sanning (logiska px) — motvikten
  `CaptionBalance`, `MenuBar::titleStart` och TopBars `innerEdgeInsets`
  läser den. Programmenyn: MenuBars vokabulär med den TOMMA stigen `{}` som
  rot + `menu()` (samma tillstånd); en tom meny öppnas inte och plattan syns
  då inte. `tryShortcut` frågas EFTER menyraden. `setProp`: `icon`/
  `smallIcon` ("lista/nyckel"), `large`, `noIcon`. `effectiveIcons()` är
  bildparet som GÄLLER (egen eller förval) — för ritningen OCH OS-ikonen.
  Bilderna är `ImageSource`: en Bitmap ritas genom `glyphResampled` och når
  OS-ikonen ur sina levande pixlar efter ett nytt `setIcon(sammaBild)`; en
  RenderTexture ritas men blir ingen OS-ikon. Posttillstånden som MenuBar.
- **`canvas.h`** — `Canvas : Widget`, RITYTAN i 2D och 3D (TODO 27 och 50; planerna i ../canvas/CLAUDE.md) — EN
  scen med kamera. Ytan är ett FÄLT, lika i alla tre ritningar (bakgrunden, kanten OVANPÅ scenen, fokusringen;
  inaktiverad tonar den innehållet med 1 − `Style::iconFade`), AUTO-måtten `canvasDefaultW/H`. API:t och dess
  kontrakt — kamerorna, lagren och formerna i kollektionsvokabulären, eventen, gesterna och orbiten, träffprovet
  och plockningen, 3D-målet, del-vyerna, de egna programmen, `renderToBitmap` och `setProp`-nycklarna — står i
  HEADERN. Pekaren (FONSTER_OCH_TIMER E5): hårkorset över en yta med gester, den STÄNGDA HANDEN medan den
  panoreras, fyrpilen medan den vrids. **MUSEN** (TODO 86): `CanvasMouseArgs : MouseArgs`, och `onMouseDown`/
  `onMouseUp`/`onMouseCancel` är BASENS (fönstret fyrar dem; ritytans egna fält och högerkrokarna är rivna) —
  `onMouseMove`/`onMouseWheel`/`onDoubleClick` är ritytans. `onResize` är också basens (det synliga utsnittet
  läses i handlern). **MINNET** (REQ3): `setMemoryBudget(byte)` (0 = `App::canvasMemoryBudget()`) — bilderna,
  render-texturerna och draperingarna ritytan ritade SENAST vräks per rityta, så två ytor i ett fönster vräker
  inte varandras; setProp `memoryBudget`. **TEXTENS MÅTT I LAYOUTEN** (REQ7 4): `measureText(c, …)` med krokens
  Ctx i en egen ritytas `heightForWidth`/`contentSize` — formen utan Ctx frågar layouten och svarar därifrån
  `nullopt` med en logg. **DE FÖRANKRADE BARNEN** (REQ8 14): `setChildAnchor(barn, punkt, ankare)` — ritytan
  placerar BARA de förankrade (`placesChild`, widget.h) ur kameran, i SAMMA bildruta som scenen; får stå före
  `adopt` (pekare + livstecken). **MÅLARENS PLATS** (REQ8 2): `onPaintLayer` i lagren med `setLayerPainter`, med
  lagrets djup; **PLATT** (REQ8 4): `setFlat`. **DE LÖPANDE HÄNDELSERNA RITAR OM RITYTAN, INTE FÖNSTRET**
  (PRESTANDA D1, ett medvetet brott mot widget.h:s "efter en handler ritas hela fönstret om"): efter `onTick`,
  `onGesture` och `onCameraChange` från drag, hjul, gest, tröghet och snäpp ritas ritytan och dess 3D-mål om, inte
  resten och inte en annan ritytas mål — en annan widget ändras genom sin sättare; diskreta händelser (klick,
  dubbelklick, tangent) ritar om fönstret. Ticken och kamerans händelse ur ritningen går i App:s RAMKÖ, före appens
  poster och utan budget (`detail::postFrameWork`). **SNAP PER LAGER** (PRESTANDA C5): `setLayerSnap(lager, på)`/
  `layerSnap` — av ger inga Vertex/Edge-träffar ur lagret i `hitTest(lokal, HitOptions)`, ytan står kvar; förval
  på, följer `moveLayer`, `setLayer(s)` slår på, ritar inte om. **BINDNINGARNA** (REQ9_INMATNING): `setInputMode`
  (setProp `inputMode`), `setBinding`/`removeBinding`/`clearBindings`/`bindings`/`effectiveBinding` och källans och
  handlingens vändning — vokabulären i `../input.h`, appens tabell ärvs rad för rad, och `OrbitPan`/`setOrbitPan` är
  RIVNA (flygläget är två rader). Ritytan svarar `followsWheelDirection() == false`: den får det råa hjulet.
  Typerna: [../canvas/CLAUDE.md](../canvas/CLAUDE.md); motorn:
  [../../../src/canvas/](../../../src/canvas/CLAUDE.md); koden: [../../../src/widgets/](../../../src/widgets/CLAUDE.md).
- **`floating_panel.h`** — `FloatingPanel : Panel`, DEN FLYTTBARA PANELEN (REQ8 14): dockningspanelens rubrik
  (text, valfri stängknapp, samma `dock*`-mått och ytor) och ett GREPP i en remsa längst ned (`floatingPanelGrip`
  — barnen klipps ovanför, så en Fill-kropp aldrig tar greppet). Draget flyttar i förälderns logiska px per axel
  (x/y, right/bottom eller syskonens luft), greppet ändrar width/height med övre vänstra hörnet stilla; på en
  rityta med förankring flyttar draget ANKARET. `onChange` vid släppet; ett avbrott återgår till trycket.

## Vid ändring

- **Ny inbyggd widget** = ny header HÄR + cpp i `../../../src/widgets/` +
  rad i paraplyet `../glaze.h` + rad i `../../../CMakeLists.txt`
  (`src/widgets/<namn>.cpp`) + `GLAZE_REGISTER_WIDGET` i cpp:n. Kopiera
  `button.h`/`button.cpp` — de är mallen.
- Nya MÅTT, FÄRGER och rit-PRIMITIVER: hård regel 3 i rot-CLAUDE.md
  (`Style`, `Theme::Colors` — BÅDA varianterna i `theme.cpp` — och `Draw`).
- Widget-headers får INTE inkludera varandra i onödan; `panel.h`/
  `scrollbar.h` drar in `glaze/draw.h` eftersom `Draw::ScrollMetrics` står
  i deras deklarationer.
