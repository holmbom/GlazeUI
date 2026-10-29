# CLAUDE.md — Glaze/widgets/src/widgets

**Komponenternas implementation** — en cpp per widget, och filen innehåller
HELA widgeten: ritning (`draw`/`contentSize` med `switch (c.ui)`),
geometri, beteende och `GLAZE_REGISTER_WIDGET`. Headers i
[../../include/glaze/widgets/](../../include/glaze/widgets/CLAUDE.md);
infrastrukturen (App/AppWindow/Widget/Draw/Style/Theme/assets) i
[../](../CLAUDE.md). Del av GlazeUI — se [root /CLAUDE.md](../../../../CLAUDE.md).

## Fyrdelningen är hela mönstret

Rotens hårda regel 3 (UI/Theme/Style/Draw). Konsekvenser som gäller VARJE fil här:

- **Inga tal och inga färger** (rotens grep-grind = 0 träffar).
- **TILLSTÅNDET är PRIVAT**: ritkoden läser
  `text_`/`checked_`/`value_` … direkt, `setProp` går via SETTRARNA
  (`setText`/`setChecked`/…) så bytet invaliderar, och en ANVÄNDAR-ändring
  som ska fyra `onChange` skriver fältet direkt + `invalidate()` (se
  `Checkbox::clicked`, `Scrollbar::userSet`). Programmatisk sättning
  avfyrar ALDRIG `onChange` — kontraktet gäller varje widget.
- **EN HANDLER FÅR RIVA DIG** (0.1.2): ett händelsepar går genom
  `firePair(onChange, onCommit)`, ett ensamt event som följs av mer kod genom
  `fireEvent(h)`. Svaret false betyder att widgeten dött, lossats eller
  förlorat sitt fönster (`HandlerGuard`) — returnera direkt och rör aldrig
  `this` igen, och låt den som anropade dig fråga samma svar (`Radio::select`,
  `SpinBox::stepFromUser`, `DataGrid::saveEdit` svarar bool). Ur
  `focusLost`: `postEvent`. `commitEdit()` overridas där något väntar.
- **En sanning om geometrin.** Det ritningen och beteendet delar
  definieras EN gång i logiska px (`innerBounds`, `boxRect`, `rowBounds`,
  `caretBounds`, `track`) och beteendet PROJICERAR den till fysiska px med
  `Draw::project` / `Draw::localX/localY`. Aldrig två uppsättningar mått.
- **`contentSize()` talar LOGISKA px** (rotens MÄTKONTRAKT). **`c.scale` i en
  widget-fil är därmed ALLTID en beteendekrok** — radträff, caret, scroll —
  aldrig en mätning. `measure_gate` mäter `contentSize()` DIREKT för varje
  registrerad typ i skala 1 och 2 (`layout_gate` fångar `L·scale²`).
- **AXELREGELN i `contentSize()` — samma mönster i VARJE widget.** En satt
  axel (`> 0`) vinner, en axel `<= 0` är AUTO och fylls ur stilen och
  innehållet. **Per AXEL**, aldrig allt-eller-inget. Mallen (kopiera
  ordagrant ur `button.cpp`/`picture.cpp`):

      gfx::Sizef s{width, height};                 // LOGISKA px, ingen scale
      if (s.w > 0.0f && s.h > 0.0f) return s;      // helt satt: rör inget
      if (s.w <= 0.0f) s.w = <stilens eller innehållets mått>;
      if (s.h <= 0.0f) s.h = <stilens eller innehållets mått>;
      return s;

  Konsekvensen av regel 3: AUTO-måttet är ALDRIG ett tal i filen — det är
  `c.style->…` eller en mätning. **Mätningen går via `Draw`**
  (`Draw::measure`, `Draw::lineHeight`, `Draw::imageSize`) och ALDRIG via
  `c.font->measure`/`c.font->height()` direkt: fonten talar FYSISKA px och
  `Draw` är bryggan som delar med skalan.
  Behövs en ny defaultlängd blir den ett `Style`-fält.
  **De fyra som med FLIT inte autosizar** har skrivna, daterade BESLUT i
  sin egen cpp — läs dem innan du "fixar" dem: `Panel` och `PropertyGrid`
  är placeringsytor som appen dimensionerar (0×0), `Bar` får ingen
  auto-BREDD (den är alltid dockad eller flexad) och `Spacer` har ingen
  override alls (`width = 0` betyder redan "ren grow-vikt" — noll är
  meningsbärande där). **AUTOSTORLEK UR BARNEN** är `Widget::childrenExtent(c)`,
  OPT-IN för en KOMPOSIT, som till skillnad från en placeringsyta VET vad den
  innehåller.
- `switch (c.ui)` med **`case UI::Slate:` + `default:`** står kvar i
  varje `draw()`/`contentSize()`. Slate är BASEN: allt som inte
  uttryckligen avviker ritas så — och basen är **DESIGNAD och inte ärvd**:
  en Slate-ritning i fyra klasser (**Yta / Platta / Fält / Spår**) och en
  skriven fyra-tillstånds-tabell som KOMMENTAR i varje cpp. Rör du en
  `case UI::Slate:` — uppdatera tabellen i samma ändring.
- **ALLA TRE RITNINGARNA:** varje `draw()` har en `case UI::Glaze:` OCH en
  `case UI::Minimalistic:`. Formspråken: Glaze
  ("Soft Laminate" — **lamineringen KRÄVER konturen**) och
  Minimalistic ("Outline" —
  ingen yta i vila, fyllning betyder tillstånd, spår är streck).
  `contentSize()` har med flit BARA `case UI::Slate: default:` (axelregeln är
  gemensam). **En widget som ritas LIKADANT i två familjer skriver ändå sin
  `case`, med skälet i klartext** — att text är text i varje formspråk är ett
  BESLUT, och beslut hör hemma i filen. Minimalistic ritar FÄRRE och ANDRA
  lager än en fylld familj, och det går inte att uttrycka i en `Style`-tabell
  — därför är ritningsvalet en switch.

**TEXTEN ÄR ETT TOKEN** (SPRÅKPAKET, listan i headerns CLAUDE.md): ritkoden läser `text()` — den UPPLÖSTA
strängen — och **rör aldrig `text_` direkt i en cpp här** (det är `TrText`:en). `TextInput` är undantaget.

**TOOLTIPEN ÄR BASENS och har ingen kod här** (post 14): visningen och det
routade uppslaget bor i [../tooltip.cpp](../tooltip.cpp), så ingen widget-fil
ritar eller mäter en tooltip. Det ENDA kontraktet här: `setTextToken`-overriden
avslutar med `return Widget::setTextToken(key, value);` (`tooltip_gate`).

## Filer

- **`label.cpp` / `button.cpp` / `checkbox.cpp`** — de tre kortaste
  mallarna. Label har **`align`** (argumentet till `Draw::text`) och
  **`Role::Heading`**, som bär STORLEKEN: `roleCtx()` kopierar Ctx:t och
  byter `font` till `c.headingFont`, i BÅDE `draw()` och `contentSize()` —
  mäts rubriken med brödtextens font ljuger autostorleken, och det är
  autostorleken som gör Style-axeln ärlig. `parseColor` känner också
  `heading` OCH `titlebar`. **GLORIAN** (REQ8 24): `draw()` ritar texten som ett lambdaanrop `rita(ctx, färg)` — `Draw::halo` anropar det per ringpunkt i gloriafärgen, sedan ritas texten själv; bredden ur `haloWidthAt` (satt eller `labelHaloWidth`, högst `overdraw`), `wrap`-klippet växer med den. **`wrap`**: `draw()` väljer `Draw::textWrapped`
  mot den yta widgeten FICK (klippt), och **`heightForWidth`** svarar med
  radernas höjd för en bredd layouten GAV — `nullopt` utan wrap, med satt
  `height` och för en bredd ≤ 0 —; `contentSize` frågar samma krok för en
  satt bredd, och utan bredd är mätningen EN rad. **REQ3_TRAD_LISTOR_FALT B9:**
  **`elide`** ritar en rad med `Draw::textElided`, och `toolTipAt` svarar då
  med TOKENET (`text_`, följer språket) när texten faktiskt kortas — appens
  egen tooltip vinner, och med `wrap` bryts texten i stället. **`vAlign`**:
  Center är hela ytan rakt av (dagens bild bit för bit), Top/Bottom en rect
  lika hög som TEXTBLOCKET mot sin kant. **`buddy`** (ett syskons namn):
  `buddyHeight` = första SYSKON med namnet som inte är jag och INTE en Label
  (en etikett som granne hade kunnat peka tillbaka och mätningen aldrig tagit
  slut), dold eller inte, och dess mätta höjd är ett GOLV för AUTO-höjden och
  för `heightForWidth`; körs i `resolve()`, så den loggar aldrig. Tabellen
  står i `label.h`; grind `layout_gate` fall 20, `measure_gate` 3,
  `label_gate` L1–L9 och `msgbox_gate`. **BREDDGRUPPEN** (REQ7_LAYOUT B3):
  AUTO-bredden är `groupWidth` — från roten, aldrig in i ett dolt delträd, max
  av `naturalWidth` (satt `width`, eller texten med MEDLEMMENS `roleCtx`), jag
  själv alltid med; `sizeMayChange`/`shownChanged` ritar om hela fönstret.
  `button.cpp` är den GODKÄNDA mallen:
  `content()` bygger bild + text som EN `Draw::Content`, `fillColor()` väljer
  vila/hover/tryck/inaktiv, `radius()` löser pillerfallet. **Slate**-grenen är
  fyra rader (skugga, fylld+konturad rekt, innehåll, fokusring);
  **`case UI::Glaze:`** lägger till `Draw::laminate` (ljusfälten ur
  `sheen`/`sheenHigh`) och `contentRect()` (innehållet sjunker vid tryck);
  **`case UI::Minimalistic:`** tar BORT — ingen fyllning i vila, kanten ur
  `buttonBorder` är formen och går mot `accent` vid tryck, och trycket syns i
  FYLLNADEN (1 px är en artondel av en 18 px knapp). `contentSize()` delas av
  alla tre. **En inaktiv knapps IKON tonas** (`Content::imgOpacity` = 1 −
  `iconFade`). `img` parsas av `propparse::parseImg`; ikonens sida är en SATT
  **`iconSize`** (`setIconSize`, negativt ⇒ logg + no-op, 0 = AUTO), annars
  `img_.sizeClass()`, och för en bild ur pixlar (0) `iconFallback` — likadant
  i `tool_button.cpp`. Tryck tar bort både skugga och laminering, hela
  tryckrörelsen i Glaze. **STANDARDKNAPPEN** (`shownAsDefault()`):
  `fillColor` tar accentytans tre tillstånd, `labelColor` `onAccent`; Glaze
  laminerar INTE (2,91:1), och Minimalistics kant och Slates kontur tar
  fyllnadens färg. **TEXTFÄRGEN** (`setColor(std::optional<gfx::Color>)`, B7)
  på Button/Checkbox/Radio: `labelColor` = `onAccent` på standardknappen,
  annars appens färg på en AKTIV widget, annars `textColor` (dimmad som
  inaktiv); Checkbox och Radio har sin `labelColor` inline i headern, och
  rutan, ringen och pricken ritas ALDRIG i appens färg (`label_gate` L8–L9).
  `clicked()` = `onClick` + `closeWithResult` med svaret; `performClick()`
  vaktar aktiveringen. Checkboxen bär samma tre former: Glaze lamineras rutan
  (och **bocken är `text`, inte `accent` — 2.40:1 på den laminerade kroppen i
  mörkt läge**), Minimalistic fyller den MARKERADE rutan (ikryssad eller
  mellanläget) med `accentSurface` och ritar märket i `onAccent` — familjens
  enda ställe där "fyllning betyder tillstånd" slår "ingen yta i vila". Märket
  är `Draw::check` eller mellanlägets `Draw::minus` (privata `drawMark`) i en
  `boxRect()`; Label ritas MEDVETET likadant i alla tre. Beteende: Space/Enter
  aktiverar en knapp, Space en kryssruta (Enter går till standardknappen —
  VAL 1, också i radio.cpp, den stängda dropdownen och färgväljaren);
  `Checkbox::clicked()` togglar + `onChange`.
- **`panel.cpp`** — Panel: ritningen (EN rad: `Draw::rect` med `Pen` när
  `border`), `innerBounds` (LOGISK sanning) → `innerRect` (fysisk
  projektion), `scrollRange` (spelrummet per axel) och BETEENDET:
  `childArea` (innerRect minus klampad scroll), `contentExtent`
  (VIRTUELL — data-innehåll överrider), `mouseWheel` (rader → px genom
  `wheelScrollPx` — styrplattan följer fingrarna 1:1 i varje rullande
  widget, de stegande tar `wheelNotches`; TODO 53), `setProp` +
  **overlay-listerna**: `track(c, vertical)` och `scrollMetrics(c,
  vertical)` i logiska px matar BÅDE greppet (`localPoint` gör musen
  logisk) och `overlayThumb()` — den privata metod som ritar tummen PER UI
  och är ett KONTRAKT med `scrollbar.cpp`; samma pixlar som
  Scrollbar-widgeten.
  **RULLNINGSYTAN är en krok** (`scrollViewport`, default `innerBounds`):
  `scrollRange`, `track` och `scrollMetrics` räknar mot den, och
  `scrollRange` projicerar den till fysiska px FÖRST — som `innerRect`.
  `childArea`/`clipRect` rörs inte (subklassens beslut; `Table` — raderna
  under huvudet). **AUTOSKROLLEN** (`dragScroll`, REQ3_TRAD_LISTOR_FALT B4 —
  kontraktet vid `Widget::dragScroll`): zonen `dragScrollZone` mot
  RULLNINGSYTANS kant, steget `dragScrollStep`, bara åt ett håll med spelrum
  kvar (en lista vid sitt slut svarar nej och kedjan går vidare), scrollen läst
  KLAMPAD. **`inRowColumn`** (skyddad, ListBox och TreeView delar den):
  utanför remsan och inom innehållets x. Panelens RADIE kommer ur
  **`Style::panelRadius`** (rundade hörn kräver rundat barnklipp), och
  `clipRadius()`/`innerRadius()` är KONCENTRISKA (`panelRadius − panelInset`
  när ramen finns). Värdet är **0 i alla tre stiltabellerna**, och att
  **Glaze-grenen faller igenom till Slate** är ett VAL. Minimalistic ritar
  ingen fyllning alls — bara hårlinjen när `border`.
- **`group_box.cpp`** — GRUPPRAMEN (post 13). `draw()` är tre
  rader — ytan `panel`, ramen `border` med en LUCKA (`Draw::frameGap`) och
  rubriken i luckan — och **ALLA TRE ritgrenarna ritar likadant, som ett
  SKRIVET beslut**: gruppramen är en YTA som en ramad Panel (`panel` +
  `border`), och familjerna skiljer sig i MÅTTEN (`group*`) och färgerna.
  Rubriken står halvt över ytan, halvt utanför. Geometrin (`frameRect`/
  `titleRect`/`innerRect`) tar ytan som argument; luckan kapas mot ramens
  högra del så en för lång rubrik aldrig bryter bågen på andra sidan.
  `clipRadius` är KONCENTRISK (`groupRadius − groupPad`, 0 med dagens tal —
  formeln är kontraktet). `contentSize` använder `childrenExtent`. Grind:
  `groupbox_gate` (också autostorleken); `pixel_gate`s ramar har SATTA mått
  med flit — en autosizande ram KRYMPER när nivåmätningen döljer barnen.
- **`splitter.cpp`** — DELAREN (post 15). `layout()` är EN sanning
  (logiska px, rutorna + skiktet) som ritningen, `placeChild` och
  musvägen läser; den räknar med förhandsvisningens mått medan
  `liveDrag()` (= `dragging_ && pressed()`) gäller — så en fokusförlust
  som släcker trycket ÅTERSTÄLLER läget utan en rad kod. `mouseDown`
  nollar `dragging_` först (ett avbrutet drag lämnade det satt), `mouseUp`
  skriver `paneSize_` och fyrar `onChange` bara om draget FLYTTADE skiktet
  och delaren fortfarande är aktiverad. Hover tänder bara skiktet
  (`invalidateRect` på dess yta). Ritningen är `sash::draw` i alla tre
  grenarna (formen bor där). Delaren ritar ALLTID två rutor och ett skikt
  — en dold ruta lämnar ett hål. Grind: `tests/splitter_gate.cpp` (också
  `mouseUp`-leveransen och pekaren).
- **`dock_panel.cpp`** / **`dock_host.cpp`** — DOCKNINGEN. Panelens
  geometri (`headerRect`, `slotRect` för ▾/knappnål/stäng — stäng ytterst, tom
  utan `closable` —, `titleRect`, `bodyRect`) tar ytan som argument;
  knapparna är verktygsknappens recept, flaggorna meddelar värden med läget
  FÖRE (`panelStateChanged`), och `pinAndExpand` (fäst och utfälld) är
  vägen UTAN värd, i visningen och `userExpand`. Värdens MODELL och GEOMETRI bor i
  [../dock/](../dock/CLAUDE.md) (`computeLayout` är EN sanning för ritningen,
  `placeChild`, `hitAt`, dragen och glidningen; `detail::pastDragThreshold`
  är dragtröskeln för panelen, värden och verktygsfältet); `syncModel()` först
  i `frameStep` (varje bild före skadan, också med tom rekt — REQ9 6: synken och en
  väntande flytt i en inaktiv flik) och i varje operation. Rubrik- och flikdraget räknar målet i
  `dropAt` och SIMULERAR det (tom yta = inget mål), och cellskiktets
  förhandsvisning är VIKTER som släppet skriver. Inaktiva flikar och infällda
  paneler får en TOM rekt (ritas inte — AppWindow) och `showsChildren() == false`. Ett TRYCK på en flik
  eller ett handtag väljer tangentbordets post (`selectKbdItem`).
  `guardFocus` körs efter VARJE synlighets-, flik- och infällningsändring —
  utan stopp töms fönstrets fokus. `userChanged` fyrar panelernas `onChange`
  före värdens. BANDEN: `layoutIn` mäter fälten i bandets riktning och MED
  greppet, `toolDropAt` räknar målet och simuleras — en kant fältet inte FÅR
  dockas vid (`ToolBar::allowsEdge`, REQ3_LAYOUT_FONSTER B21) är inget mål,
  varken band eller bandzon, också fältets nuvarande kant, och `applyTool`
  vägrar den som försvar bakom —, `writeBackBars` skriver kant och packat
  band. OVERLAYN: `overlayPanel()` prövar `overlay_`, `syncOverlay` håller EN
  åt gången, `frontChild()` ger den, `hideCheck` (pekaren GEOMETRISKT) går på
  `hideTimer_`, glidningen stegas en gång per bild i svansen. FLYTANDE:
  `panelFloatingChanged`/`barFloatingChanged` (tillståndet först),
  `syncFloats` i svansen (ett hinder väntar utan logg),
  `floatingItemDestroyed` (ett dött objekt släpps NU), `pruneModel`,
  `applyTool` som också dockar ett flytande fält, destruktorn utan
  barnvandring, och det LEVANDE DRAGET (`carry_` — fall A med värdens capture,
  fall B i objektets fönster, ETT `onChange` per drag, avbrottet
  `itemPressLost`; ett buret FÄLT lyfts utan fokusbyte i varje `carryMove` och
  vid släppet). De tre dragen (rubriken, greppet, det flytande fältets rubrik)
  går genom `carryStep(objekt, x, y)` (börja eller flytta, källan = objektets
  fönster just nu) och `carryRelease` — panel och fält kan inte glida isär.
  Grind: `dock_gate`, tooltip_gate 20–24.
- **`tab_control.cpp`** / **`tab_page.cpp`** — FLIKARNA (post 19). Geometrin
  är EN sanning i logiska px som tar ytan som argument (gruppramens form): `stripRect` (flikraden, `tabHeaderH`), `frameRect`
  (visningsytan), `pageRect` (den minus `panelInset`) och `tabRects` — en
  rekt per sida, så bred som rubriken plus `2 × tabPadX` (stängknappens
  kvadrat ERSÄTTER högerluften och läggs på `tabMinW`), VATTENFYLLD mot ett
  gemensamt tak när raden inte rymmer alla och sedan FÖRSKJUTEN så att den
  valda slutar vid radens högerkant. Ritningen, `placeChild`, `clipRect`,
  träffen (`tabAt`/`closeAt`) och hovern läser samma funktioner.
  **`syncPages(intoPage)` är EN väg för synligheten**: den valda sidan visas
  FÖRST, fokus i ett barn som ska gömmas flyttar (till den nya sidans första
  stopp ur `AppWindow::focusOrder()` när användaren bytte inifrån sidan,
  annars till flikkontrollen), och SIST göms resten — då står den nya sidans
  kontroller redan i tabbordningen och fokus flyttar exakt en gång.
  `childAdded`/`childRemoved` (basens trädkrokar) håller valet: `shown_`
  (sidan som VISAS, jämförd men aldrig dereferenserad efter borttagningen)
  avgör om indexet ska följa sidan eller stå kvar på platsen. `selectFromUser`
  har DataGridens ordning — tillståndet, omritningen, handlern SIST.
  **RITNINGEN** per UI: Slate och Glaze ritar en FLIK — den valda är sidans
  `panel`-yta som sticker upp, en rundad rekt (`tabRadius`) en hel flikhöjd
  NEDANFÖR fliken klippt vid flikens underkant (kapselns teknik), och ramens
  överkant är öppen under den (`Draw::frameGap`) — LUCKAN ÄR FLIKENS INSIDA,
  mellan dess sidlinjer (`Draw::penWidth`), så linjerna möts i slutna hörn;
  Glaze med `buttonBorder` och den TÄNDA ovalda fliken laminerad med kontur,
  Slate med `border` och en `buttonHover`-platta. Minimalistic har INGEN
  flikform: rubriker i rad, den valda med `rowMarkerW`-strecket i underkanten
  och texten i `accent` (menyradens recept), ytan `panel` med hel kant.
  Stängknappen följer verktygsknappens recept och ✕ är `tabCloseGlyph`;
  fokusringen ligger INNANFÖR den valda fliken. Hovern ritar om bara
  flikraden, ett byte hela flikkontrollen. `TabPage::draw` ritar bara Panels
  ram när `border` är satt (ytan är flikkontrollens). **`setPages`** prövar ALLA sidor
  (`subtreeHasOpenPopup` + `menuPresentationHostedIn`) FÖRE mutationen, släpper
  dem med `unadopt` och skriver tillbaka det LAGRADE valet före `syncPages`;
  `addPage` är `add<TabPage>`. Grind: `tests/tab_gate.cpp`.
- **`scrollbar.cpp`** — fristående listen: `track`/`thumb`/`localPos` i
  logiska px, tumdrag (dragOff mot hopp), rännklick sidvis, hjul. Tummen
  kommer ur SAMMA `Draw::scrollThumb` som ritar den.
  **RÄNNAN ritas HÄR och aldrig i den delade primitiven**: den fristående
  listen äger sin remsa, medan Panels overlay svävar över barnen (en opak
  ränna där hade täckt innehåll). TUMFÄRGERNA är däremot ett KONTRAKT med
  `panel.cpp`s `overlayThumb`: samma färg för samma TILLSTÅND, bytta i båda
  filerna i SAMMA ändring (härledningen får skilja sig — Panel vet vilken
  AXEL som dras). `value()` KLAMPAR VID LÄSNING (FAS1_B_WIDGETS, som
  reglaget). **Slates tumme är `border`/`textDim`**, Glaze ritar
  `accentSurface` och Minimalistic ritar INGEN RÄNNA alls.
- **`textinput.cpp`** — fältet, selektionen i tre segment och careten. **`readOnly`** spärrar allt som ÄNDRAR
  texten och behåller allt som LÄSER den: `wantsTextInput()` blir false (släcker textflödet OCH caret-timern, och
  "text vinner över genvägen" gäller då inte), `keyDown` svarar tidigt på Backspace/Delete/Ctrl+X/Ctrl+V medan
  navigering, Ctrl+A och Ctrl+C faller igenom. Ytan byter till `fieldReadOnly` i alla tre grenarna — i
  **Minimalistic** familjens ENDA fältfyllning, INNANFÖR kanten (grindat `buttonBorder` ≥ 3:1 mot den). Vidare:
  caret/anchor, `keyDown`/`textInput`/`mouseDown`/`mouseDrag`, horisontell scroll (`ensureCaretVisible`,
  `scrollX_` logisk) och `caretBlink` (invaliderar ENDAST `caretRect`); textmätningen via `Draw`. Innehållet ritas
  av **`drawContent(c, selFill, selText)`**, DELAD av alla tre ritningarna — selektionsPARET skickas in
  (Slate/Glaze `accent`+`windowBg`, Minimalistic `accentSurface`+`onAccent`). **Ytan per familj, fokusringen,
  markeringens rad, caretens bredd, UTF-8-gränserna, filtret, historiken och genvägsfrågan bor i
  `../text_field.cpp`**, som det flerradiga fältet också läser; `draw()` har kvar sin `switch (c.ui)`.
  **EN REDIGERINGSVÄG**: varje ändring går genom `replace(b, e, ins, kind)`, som registrerar steget (utom i
  lösenordsläge) med markeringen FÖRE; `insertFiltered` ersätter markeringen i SAMMA anrop, och ångra/gör om går
  genom `restore` (texten, stegets markering, `edited()`). `movedCaret` = seal + `showCaret`, `edited` =
  `showCaret` + `onChange` — så varje ren flytt förseglar gruppen men ingen redigering; Enter och `focusLost`
  förseglar också. `canUndo` spärrar skrivskyddat; lösenordsläget har ingen historik och ingen spärr.
  **LÖSENORDET**: de fem ställen som ritar eller träffar text läser `shown(c)` (masken i den återanvända
  `mutable masked_`) och `toShown`/`fromShown` (byte i texten ↔ byte i masken, en tom mask ger 0). **COMMITEN**:
  `commitBase_` sätts av `focusGained` och av en `setText` som BYTER texten (ett eko är en no-op, så
  bindningsmönstret inte äter commiten); `commitFromUser` (Enter och `commitEdit`) fyrar direkt med `fireEvent`
  och invaliderar fönstret BARA om vakten står sig, `focusLost` postar. `selectAll`/`setSelection` via
  `floorBoundary`; Ctrl+A är `selectAll`. **Det HÅLLNA trycket**: ett tryck som gav fokus (`pressGaveFocus()`)
  blir ett drag först bortom `dragThreshold` och ankras då om vid `pressIndex_` — en omankring ritas om också när
  careten står still (TextArea likadant, 2D). **MAXLÄNGDEN** kapar i `insertFiltered` (`fitMaxLength` FÖRE
  `replace` — ryms inget står markeringen kvar); **PLATSHÅLLAREN** i `drawContent` efter klippet (kortad,
  `textDim`). **TEXTSTORLEKEN** (REQ8 26): sex löv (`contentSize`, `textOriginX`, `drawContent`, `caretBounds`,
  `indexFromX`, `ensureCaretVisible`) läser `textCtx(c)`.
- **`text_area.cpp`** — DET FLERRADIGA FÄLTET, en `Panel`-ättling (remsorna, hjulet; AUTO-höjden är
  `heightForRows(c, textAreaRows)`). **LAYOUTEN** är en lat cache bakom const-ytan (`mutable Layout`): de
  visuella raderna `[b, e)` i textordning för en NYCKEL (fontpekaren OCH radhöjden — en omladdad font kan hamna
  på samma adress —, skalan, brytbredden och TABBREDDEN `tabW`). Ny nyckel ⇒ allt läggs om (`linesFor(c,
  brytbredd, tabW, …)` per stycke: med tabbar `textfield::tabbedWidth`/`tabbedWrapLines`, ett tabbfritt stycke
  exakt `Draw`s väg); en REDIGERING går genom EN väg (`replace`, som registrerar ångra-steget och kallar
  `replaceRaw`) och lägger bara om de stycken den rörde (`relayout`; `textarea_gate` F10 jämför mot en hel
  omläggning). **GEOMETRIN** är EN sanning i logiska px: `origin` (inre hörnet + luften − KLAMPAD scroll),
  `lineAt` (sista raden som börjar ≤ p; `upstream` väljer den övre vid en hårdbrytning), `xAt`/`indexAt`
  (`textfield::tabbedPrefix`/`tabbedIndexAtX`) och `textClip`. **TABBEN** (`setAcceptsTab`, WinForms
  `AcceptsTab`): Tab UTAN modifierare skriver `\t` (Typing — ett `onChange`, ett ångra-steg; Shift+Tab och
  Ctrl+Tab flyttar fokus, skrivskyddat släpper den), inklistrade tabbar behålls (`multiLine(…, acceptsTab_)`),
  och `tabWidth` = max(1, `textAreaTabSpaces`) × ett blankstegs bredd; `maxLength` räknar tabben som en
  kodpunkt. Remsornas plats och brytbredden hänger ihop: `padRight` = max(`fieldPadX`, `scrollThick`) ALLTID,
  och `padBottom` växer till remsan bara när texten rullar i sidled — så ingen cirkel mellan "rullar den?" och
  "var bryts den?". `contentExtent` räknar LUFTEN med, med brytning är bredden ytans egen. **Tangenterna och
  texten får inget Ctx** av AppWindow — `openCtx()` (= `behaviorCtx()` i ett ÖPPET fönster) ger det, och utan
  fönster svarar Upp/Ner/Home/End/PageUp/PageDown false. `scrollToCaret` körs ur tangent-, text- och musvägarna
  och ALDRIG i `draw()`. PageUp/PageDown räknar de helt synliga raderna med en FYSISK px tolerans (annars tappade
  AUTO-höjden en rad på en flyttalsavrundning). `drawLines` är O(synliga) och markerar en markerad radbrytning
  med ett blankstegsbrett block. `caretBlink` invaliderar caretens rect KLIPPT mot texten. Musen är remsa-först
  (`inScrollStrip` + `Panel::mouseDown`), `textDrag_` skiljer textdraget från tumdraget, och draget förbi kanten
  rullar genom `scrollToCaret`. Ritgrenarna: alla tre går till `textfield::drawSurface`. `onCommit` är
  TextInputs fokusförlust-halva; Enter är en ny rad. `setSelection` nollar `upstream_`/`goalX_` och rullar inte —
  Ctrl+A är `selectAll` + `follow()`. **ÅNGRA**: `restore` går genom `replaceRaw`, sätter stegets markering,
  nollar `upstream_`/`goalX_` och kör `edited()` — som rullar till careten (`follow`). Grind:
  `tests/textarea_gate.cpp` (F13 för ångra) och `textfield_gate`. **MAXLÄNGDEN** i `insertText` (den kapade
  insättningen i en EGEN variabel — bara den når historiken); **PLATSHÅLLAREN** i `drawLines` före careten
  (radbruten med `wrap`). **TEXTSTORLEKEN** (REQ8 26): layoutens nyckel (font, radhöjd), `tabWidth`, `linesFor`,
  `xAt`/`indexAt`, `heightForRows` och `drawLines` läser `textCtx` — aldrig remsorna.
- **`markdown_view.cpp`** — MARKDOWN-VYN (REQ7_MARKDOWN), en `Panel`-ättling som TextArea; tolkningen och layouten är
  [../markdown/](../markdown/CLAUDE.md). CACHARNA: dokumentet tolkas om när den UPPLÖSTA texten skiljer sig
  (`document()` nollar länkindexen), layouten i TVÅ platser (`LayoutSlot`, nyckeln `md::Metrics` som värden + bildernas
  generationer; bilderna per plats — tabellen, sedan `lista/nyckel` utan schema). `hitAt` bär trycket, släppet, hovern,
  handen, tooltipen och `linkAt`; klicket är en knapps, url:en kopieras FÖRE handlern, som körs DIREKT under en
  `HandlerGuard` (`fireEvent` kopierar args). Det väntande ankaret löses i `draw()`. `contentWidth`/`autoHeight` delas
  av mätningen och ritningen och läser aldrig `resolve`. Grind: `markdown_gate`.
- **`spin_box.cpp`** — TALFÄLTET (post 12), den första riktiga
  kompositen. Fältet är `SpinBox::Field`, en `TextInput` med TRE tillägg
  och inget annat (Up/Down stegar, bara taltecken tas emot, Enter — som
  sedan svarar false, B8 — och fokusförlust COMMITTAR texten — `commitText(postad)`: tolka,
  klampa, skriv om, och `fireChange` = `onChange` + `onCommit` direkt eller
  via `postEvent`). Fältets `onChange` är OSATT — att skriva ändrar inte
  värdet — och `stepFromUser` utgår från en tolkbar skriven text. `focusLost`
  rör bara värdet och fältets egen text, aldrig en handler. **TVÅ TOLKARE med
  flit:** `tolkaText` (användarens text — `,` ELLER `.`, högst ett, inga
  exponenter) och `tolkaLiteral` (en C++-literal ur `setProp` — `1e3`,
  `.5`, `2.5f`, aldrig komma), båda i `std::locale::classic()` så appens
  `setlocale` aldrig ändrar vad ett tal betyder. `effective()` klampar,
  avrundar till `decimals` och klampar IGEN (en gräns som inte är en
  multipel av 10^−decimals får aldrig ge ett värde utanför), och −0 blir 0.
  `format()` skriver i "C"-lokalen och byter punkten mot språkets tecken.
  **SPRÅKBYTET når ett redan visat tal genom kroken `languageChanged()`** —
  bara när fältet INTE redigeras, och `TextInput::setText` är en no-op när
  texten redan stämmer, så kroken tål att anropas utan språkbyte (vid visning
  och adoption). `draw()` ÄNDRAR ingenting (`spinbox_gate`). Stegaren är EN
  platta i knappfärgerna; en HALVA tänds genom att hela plattan ritas KLIPPT
  till halvan (kapselns teknik). Recepten per UI står som tabeller i filen.
  Hjulet samlar bråkdelar tills ett helt steg — och bara när FÄLTET har fokus
  (`field_->focused()`: kompositen är aldrig fokuserad själv, ombudet är).
- **`listbox.cpp`** — listan: rad-geometrin (`rowRect`/`checkRect`/`textX`/
  `checkZone`, logiska px, scroll-förskjutna) som ritning OCH musväg delar,
  med **VARIABEL RADHÖJD i basen**: `rowH`/`uniformRows` (virtuella) +
  `rowTop`/`totalRowsH`/`rowAtOffset`/`firstVisibleRow` är ETT kontrakt som
  alla `listRowH`-beräkningar går igenom — i BASEN, annars TVÅ
  höjdsanningar. `uniformRows()` är true som default (O(1)-vägarna);
  `drawRow` är `protected virtual`, så en subklass byter radens INNEHÅLL utan
  att röra markering, scroll eller radloop. **`addItem`/`setItem`**: `addItem`
  gör `push_back` + `checked_.resize` (parallellvektorns invariant) utan
  scrollklampning, `setItem` byter en rads text utan att röra kryss, val eller
  scroll och invaliderar bara vid FAKTISK ändring; index utanför ⇒ logg +
  no-op. Radloopen startar vid FÖRSTA SYNLIGA raden (O(synliga)), hjulet
  stegar i RADER. Radens INNEHÅLL ritas av `drawRow`, DELAD av alla tre — det
  som skiljer familjerna är MARKERINGEN: Slate fyller den valda raden med
  `accent` (text `windowBg`), Glaze med det GRINDADE `accentSurface`/
  `onAccent` (OLAMINERAT), Minimalistic `rowMarkerW`-strecket. Hover är
  `buttonHover` och SVAG — spegelvänt mot popupen. Kryssets färg följer RADENS
  textfärg. DISABLED har en egen gren per UI (`textDim` + `buttonDisabled`).
  **FLERVALET** är listvyns: `selected_` parallell med `items_`,
  `assignSelection` med ändringsvakt (ett event per handling), ankaret,
  `pendingSolo_` (solo i `mouseUp`, nollat av `dragStart`) och den AKTUELLA
  radens ram. **TRÄFFEN** (REQ3_TRAD_LISTOR_FALT B2–B3): `rowAtY(c, my)` är
  den interna y-frågan, `hitRow(c, mx, my)` = `inRowColumn` + `rowAtY` (remsan
  och sidramen är "ingen"), och publika `rowAt(lokalX, lokalY)` projicerar
  LOGISKA px och frågar `hitRow`. `inCheckZone` är kryss-zonens träff, delad
  av trycket och dubbelklicket. **HÖGERTRYCKET VÄLJER SOM UTFORSKAREN**
  (`rightDown`): `contextRow_` = raden (−1 för tom yta); en rad utanför valet
  väljs ensam genom `selectFromUser` (vakt, och en körd handler ritar om
  fönstret), en rad i valet blir bara aktuell; ingen fokusflytt, och svaret är
  ALLTID false så snabbmenyn fyras med `contextRow()` satt. Popupraderna,
  menyraderna och platslistan overridar `rightDown` → false (deras val är
  provisoriskt respektive navigerar). **AKTIVERINGEN** (`onActivate`, REQ5 1 —
  listvyns form): `activateFromUser` fyrar bara med handler och vald rad och
  svarar om den TOG handlingen; Enter går dit (annars bubblar den till
  standardknappen), och `mouseDoubleClick` på den valda raden utanför
  kryss-zonen (där har båda trycken vänt krysset). **TEXTSTORLEKEN** (REQ8 26):
  `rowH` = `listRowH` + radhöjdsskillnaden (oförändrad utan storlek), `drawRow`s text genom `textCtx`.
- **`tree_view.cpp`** — TRÄDET, ListBox rad för rad där det går: samma tre
  markeringsrecept, remsa-först, hjul i rader och O(synliga) radloop. Det som
  är trädets eget: **RADERNA ÄR DE SYNLIGA NODERNA**, utplattade djupet först
  genom de utfällda (`rows()`, varje syskonlista i sorteringskolumnens
  naturliga ordning när en gäller — REQ8 13, stigen är nodens plats i DATA),
  byggda om LATT efter varje strukturmutation och ny ordning via
  `rowsChanged()`; pekarna i radcachen gäller till nästa mutation, så
  varje användarväg tar en KOPIA av stigen innan något kan bygga om cachen.
  **IDENTITETEN ÄR ETT ID** (`Node::id`, aldrig återanvänt), och valet och
  krysset är FLAGGOR i noden: den aktuella noden, ankaret, det väntande
  solovalet och dragets källor (`currentId_`/`anchorId_`/`pendingSoloId_`/
  `dragIds_`) håller fast vid id:n, `assignSelection(ids)` sätter flaggorna
  EXAKT och svarar om något ändrades (bara då fyras något), och `forgetIn`
  släpper id:n i ett borttaget delträd (`kidsIn`/`forEachNode`: mallar, ingen
  `const_cast`). `selectFromUser(rad, mods, frånTangent)` är listvyns.
  `mouseDown` i ordningen remsa och huvud (basens `headerMouseDown`) → pil →
  RUTANS ZON (`toggleCheckFromUser`: vänd, `onCheck`, stigen igen,
  `onCommit`) → en vald nod i flerval väntar på `mouseUp` → välj;
  `toggleFromUser` väljer föräldern när en ihopfällning
  gömde valda noder. **GEOMETRIN** står i basens `scrollViewport` (under
  huvudet): `expanderRect` (följer kolumn 0 i sidled) → `checkRect` →
  `checkZone` (+ `checkGap`) → `contentX`; `cellRect` för kolumn 1…, där
  cellerna ritas med tabellens luft och ellips. Radens INNEHÅLL — pilen,
  rutan och bocken (ListBoxens pennor), ikonen och texten som ETT
  `Draw::content`-block — delas av alla tre ritningarna i en lambda, och
  TEXTEN tar nodens färg bara omarkerad i ett aktivt träd. Med flerval och
  fokus ramas den aktuella raden in efter radloopen. `mouseDoubleClick` frågar
  `headerDoubleClick` FÖRST (en avdelare anpassar sin kolumn) och avstår sedan
  på pilen och i rutan. **BREDD EFTER INNEHÅLLET** (REQ8 8): `cellContentW`
  mäter de SYNLIGA raderna med samma mått som `contentX`/`draw` — kolumn 0 =
  djup · `treeIndent` + `treeExpanderW` (+ `checkSize` + `checkGap`) (+ ikonen
  + `iconGap`) + texten, kolumn 1… cellerna — och `rowsChanged`, `setNode`,
  `setCell` och `setShowChecks` anropar `contentChanged()`.
  **SNABBMENYN** (REQ3 B1–B3): privata `hitRow` (−1 i
  remsan och utanför i x OCH y) bär `pathAt` (`{}` FÖRE indexeringen) och
  `rightDown` (väljer som Utforskaren, noden vid ID i `contextId_`):
  `contextPath()` är `{}` när noden saknas och när trädet är inaktiverat.
  **Drag-and-drop**: `canDragFrom` nekar remsan, huvudet, pilen och zonen; `dragStart`
  drar `topLevelSelected()` från en vald nod (annars den tryckta) och nollar
  solovalet; `dropSpotAt` räknar platsen ur `treeDropEdge` och sparar LUCKAN
  som id:n (`DropSpot::parentId`/`prevId`/`nextId`), som `resolveDrop` löser
  vid varje `dropParent()`/`dropIndex()` (false när luckan inte längre
  stämmer); `dragOver` invaliderar BARA vid ändring och föreslår `Move` för
  egna levande noder. `drop` körs EFTER appens `onDrop`, räknar ALDRIG om
  platsen ur pekaren och flyttar genom `moveIds` (validera allt, FÖRE-index,
  målet vid sitt ID) — annars `None`; publika `moveNodes` går också hit.
  `dragEnd` tar bort alla vid `Move` till ett annat mål; `drawDropIndicator`
  är LIKA i alla tre. **SLÄPPREGELN PER NOD** (post 49): ingen mittzon, och en
  plats vars FÖRÄLDER säger nej blir `DropSpot{}`. Grind: `tests/tree_gate.cpp`.
- **`table.cpp`** — den DELADE tabellmaskinen: rader, deras sortering och
  geometri för ListView och DataGrid. **SORTERINGEN ÄR EN LAT
  PERMUTATION** (`order_` plats → rad, `pos_` rad → plats) som
  `ensureOrder()` räknar om först när den behövs efter `orderChanged()` —
  stabil, så lika rader behåller appens ordning i BÅDA riktningarna.
  Jämförelsen (`compareRows`): nyckel mot nyckel numeriskt, en cell MED
  nyckel före en utan, annars basens `compareNatural` (sifferföljder som tal,
  inledande nollor ignorerade, ASCII-versaler vikta, UTF-8 bytevis). NaN räknas som
  ingen nyckel — en jämförelse som inte är en strikt svag ordning hade gjort
  `stable_sort` odefinierad. **GEOMETRIN** utgår från `scrollViewport`
  (raderna under huvudet); `rowRect` är det SYNLIGA bandet (markeringen fyller
  det man ser och Minimalistics markör står kvar vid vänsterkanten när man
  rullar i sidled), `cellRect` följer den horisontella scrollen, och scrollen
  läses alltid KLAMPAD. `clipRect` är raderna, så datagridens editorer aldrig
  ritas över huvudet. `contentExtent` är bara radernas och kolumnernas.
  Hjulet stegar i RADER lodrätt och i `wheelStep` vågrätt. **IKONSTORLEKEN**
  (B6; 0 = AUTO): kroken **`baseRowH`** (`listRowH`/DataGrids `gridRowH`), och
  `rowH` = max(bas, ikon + (bas − `iconFallback`)). **CELLENS FÄRGER**
  (REQ8 6): `setCellColor` (logg + no-op utanför, kolumnen får saknas, växer
  inte vektorn för en sättning som inte ändrar, invaliderar vid ändring),
  `cellColors_` parallell med `rows_` (nollas av `setRow`/`setRows`), och
  `drawCell` med `appColors` fyller bakgrunden FÖRE innehållet och väljer
  `appTextColor` (cell → rad → tema); `drawCells` hoppar över kolumn 0 när
  den är fast. **RADRUBRIKER** (REQ8 7):
  `frozenW`/`cellRect`/`contentExtent`/`hitPosition`/`columnAt` (remsan är
  radens del) och `drawRowHeaders` — en pass klippt mot radernas yta på hela
  innehållsbredden: huvudets yta (`drawHeaderSurface`), per rad namnet
  (`drawCell(…, false)`), subklassens `drawRowHeaderMarks` och en hårlinje
  under raden, sist remsans högerkant. **BREDD EFTER INNEHÅLLET** (REQ8 8):
  `cellContentW` mäter ALLA rader (text + i kolumn 0 `leadW` och ikonen);
  `addRow`/`setRow`/`setRows`/`setCell`/`setIconSize` anropar
  `contentChanged()`.
- **`column_panel.cpp`** — KOLUMNHUVUDET (REQ8 13, utbrutet ur table.cpp för
  Table och TreeView; loggraderna säger `ColumnPanel(namn)`).
  **HUVUDET** (`drawHeader`, per UI): Slate och Glaze en remsa i `windowBg`
  (barernas yta — Glaze med linjerna i den bestämda `buttonBorder`),
  Minimalistic ingen yta; sorteringspilen är dropdownens chevron i `textDim`,
  och avdelaren TÄNDS i `accent`/`focusWidth` under pekaren och medan den
  dras. **RUBRIKERNA PÅ FLERA RADER**: `headerH` är `tableHeaderH` för EN rad
  och en `Draw::lineHeight` till per rad; `headerLineCount` är det satta talet
  eller AUTO:s, CACHAT i `HeaderCache` mot fontpekaren OCH radhöjden, skalan,
  `cellPadX`, `columnDefaultW`, fyllkolumnens bredd `fillW` och `headerGen_`
  (stegad av `headerChanged()` i kolumnsättarna, `setColumnWidth`,
  `setFillColumn` och `languageChanged()` — två fyllkolumner med samma bredd
  ger samma `fillW`). Raderna ur `Draw::elideLines` ritas var och en i en rect
  LIKA HÖG SOM HUVUDET, flyttad `platsSkift` från mitten (`förstaPlats` per
  `VAlign`) — ett enradshuvud får exakt den rect det alltid fått, och
  `setHeaderLines`/`setColumns` klampar scrollen. **FYLLKOLUMNEN**
  (`setFillColumn`, REQ3_TRAD_LISTOR_FALT B5, Qt:s Stretch; −1 = ingen):
  kolumnen tar resten av innehållets bredd och aldrig mindre än sin egen
  (`columnBaseW`); geometrin tar innehållsbredden som argument (`columnW`/
  `columnX`/`columnsW`/`cellInContent`/`headerH(c, cw)`), och `contentW(c)` =
  `innerBounds(c).w` BARA med en fyllkolumn — en tabell utan räknar exakt som
  förut, och ett satt `headerLines` frågar inte bredden; fyllkolumnens kant
  går inte att dra. `hasHeader` false (trädet utan kolumner) ger höjden 0.
  **BREDD EFTER INNEHÅLLET** (REQ8 8): `columnBaseW` ger `fittedW` för en
  negativ bredd (`Column::autoWidth`) — rubrikens bredaste `\n`-rad + luft
  (pilens plats reserverad när `sortable_` eller kolumnen är den sorterade)
  mot `cellContentW` + luft, båda plus kantens tjocklek som ANDRUM (utan det
  fick den bredaste texten en ellips: kortningen jämför strikt och
  (mått + luft) − luft är inte alltid måttet i flyttal), minst `columnMinW`,
  cachat per kolumn i
  `FitCache` (font, stil, skala, sortering, `fitGen_`); `setColumnWidth` stegar
  inte `fitGen_` (en annan kolumns bredd ändrar inte måttet). Dubbelklicket:
  `headerDoubleClick` nollar `resizeColumn_` (det andra trycket började ett
  drag) och skriver `fittedW` som fast bredd. **FASTA FÖRSTA KOLUMNEN**
  (REQ8 7): kroken `frozenW` smalnar `scrollViewport` från vänster,
  `headerCellRect` rullar inte kolumn 0, `dividerAt` hoppar över rullande
  kanter under den och `drawHeader` ritar rubrikerna som lambdan `rubrik(i)` —
  de rullande klippta mot ytan till höger, den fasta sist.
  **TABELLENS TRÄFF** (B3): skyddade `hitPosition(c, mx, my)` (huvudet, remsorna och
  allt utanför rullningsytan i x och y är "ingen") bär publika `rowAt(lokalX,
  lokalY)`, som svarar med APPENS radindex, och listvyns högertryck.
  **IKONEN ÄR EN KROK** (`iconSide`/`drawIcon`): `drawCell` frågar sidan (0 =
  ingen ikon) och låter kroken rita i en kvadrat — standard är radens
  `ImageSource` (asset i sin storleksklass, pixlar i `iconFallback`) med
  `iconFade`; en SATT ikonstorlek gäller VARJE rad, också en utan bild (texten
  i linje), och bara en giltig bild ritas. Filväljarens interna lista ritar
  mappar och filer i radens textfärg. **ZONEN FÖRE IKONEN**: `drawCell` flyttar
  kolumn 0:s innehåll `leadW(c)`; `leadZone` är tom utan kolumner (inget
  `columnW(0)` på en tom lista). **RADENS FÄRG**: `Row::color`, `setRowColor`
  (logg + no-op utanför, invaliderar vid ändring) och skyddade
  `appTextColor(rad, kolumn, tema)` för omarkerade rader; `setRow` är
  tilldelning (färgen nollas) och anropar kroken `rowReplaced(rad)`, `setCell`
  lämnar färgen.
- **`list_view.cpp`** — LISTVYN: valet och de tre recepten — samma som
  ListBox (Slate `accent`/`windowBg`, Glaze `accentSurface`/`onAccent`
  olaminerat, Minimalistic `rowMarkerW`-strecket, hover `buttonHover`,
  inaktiv `buttonDisabled`/`textDim`), plus en hårlinje runt den AKTUELLA
  raden i radens textfärg när listan har fokus OCH flerval. Ramen ritas EFTER
  radloopen, annars hade nästa rads fyllning täckt dess underkant. Radloopen
  bryter vid `r.y >= nederkanten`. Valet och KRYSSEN lagras PER RAD
  (`std::vector<char>` `selected_`/`checked_`), så de följer raden genom en
  sortering; ankaret är också en rad. `selectFromUser` är den enda vägen som
  fyrar `onChange` (och bara när `assignSelection` säger att något ändrades),
  och den — liksom Ctrl+A — sätter `changedRow_ = -1`; musen läser
  modifierarna ur `AppWindow::modifierKeys()`. **SOLO VID SLÄPPET**: ett
  omodifierat tryck på en vald rad i flerval sätter `pendingSolo_` (ett
  RADINDEX) och den aktuella raden, och `mouseUp` väljer raden ensam —
  nollat av nästa tryck, `rowsReplaced`, `rowReplaced` och `dragStart`.
  **KRYSSEN** (REQ2_TRAD_LISTOR): `leadW` = `checkSize + checkGap` med rutor;
  `checkRect` v-centrerad i `Table::leadZone`, `onCheckZone` kräver också
  `scrollViewport`. `mouseDown`: huvudet → ZONEN (`toggleCheckFromUser`) →
  solovalet → valet; dubbelklick avstår i zonen, Space före valet,
  `canDragFrom` nekar remsor, huvud och zon; `drawCheck` efter `drawCells` med
  TEMATS pennor (appens färger bara för texten och bakgrunden). Enter tas bara med en
  aktuell rad OCH `onActivate` satt (annars standardknappen; trädet och
  ListBox likadant). **HÖGERTRYCKET** (`rightDown`) är ListBoxens regel genom
  `hitPosition`, med `contextRow_` (appens rad, −1 för ingen) — nollad med
  raderna. Grind: `listview_gate`.
- **`data_grid.cpp`** — DATAGRIDDEN: editorerna, kolumntypen, redigeringen
  och recepten runt cellerna — kolumnerna, raderna, sorteringen och huvudet
  är `table.cpp`s. `TextEditor`/`ChoiceEditor` lämnar BARA över tangenterna
  som AVSLUTAR en redigering (Enter, Escape, Tab, Upp/Ned — med Ctrl/Cmd/Alt
  går tangenten orörd till fältet, också Ctrl+Z) och meddelar
  fokusförlusten; resten ärvs. En cell börjar med `TextEditor::börja(text)` =
  `setText` + `resetHistory()`. Vallistan STÄNGD lämnar Upp/Ned åt
  datagriden, ÖPPEN är de listans. **AVSLUTET ÄR EN VÄG** (`endEdit(spara,
  postad)`) och ORDNINGEN är bärande: tillståndet nollas FÖRST (editorns
  `focusLost` ser då att ingen redigering pågår), datagriden tar fokus INNAN
  editorn göms (annars tangenterna i tomma luften), och cellen skrivs och
  `onChange` fyras SIST — en handler får bygga om tabellen. Ur `focusLost`
  POSTAS eventet med radgenerationen `rowsGen_`. `fireCellChange` fyrar
  `onChange` + `onCommit` med samma (rad, kolumn) genom `firePair`;
  `endEdit`/`saveEdit` svarar, och editorerna flyttar inte cellen vid false.
  `editable(k)` är EN fråga för ritningen och redigeringen, så en val-kolumn
  utan val SER skrivskyddad ut för att den ÄR det.
  `placeChild` läser bara Ctx:ets egna fält (`c` är BARNETS — datagridens
  `resolve()` hade räknats mot fel yta), skickar därför innehållsbredden
  UTTRYCKLIGEN (`c.area` är datagridens `childArea`, fyllkolumnens bredd) och
  ger de dolda editorerna en tom rect; radhöjden är `baseRowH` (`gridRowH`).
  `startEdit` ger editorn KOLUMNENS justering innan den visas.
  **RITNINGEN**: ytorna är FÄLTETS och LIKA i alla tre — `panel` är
  skrivytan och `fieldReadOnly` en kolumn som inte går att redigera
  (TextInputs två ytor) — och rutnätet `border`. Det som skiljer familjerna
  är den AKTUELLA cellen, som listvyns val: Slate `accent`/`windowBg`, Glaze
  `accentSurface`/`onAccent` olaminerat med ramen i `buttonBorder`,
  Minimalistic `rowMarkerW`-strecket och texten i `accent`; resten av en rad
  bär appens färger (cellens, annars radens — `drawCell(…, på && !markerad)`,
  som också fyller cellens bakgrund), också i en skrivskyddad kolumn; med
  radrubriker (REQ8 7) är kolumn 0 en rubrik (`editable(0)` false,
  `firstColumn()` klampar den aktuella cellen) och `drawRowHeaders` anropas
  före `drawHeader`. **RADVALET**
  (FAS1_B_WIDGETS): `setCurrentPosition` slutar i `selectRowFromUser`; valda rader fylls
  i listvyns recept ut till högerkanten, den aktuella cellen hårlinjeramad;
  under redigering ritar editorn sig själv där. Radloopen är O(synliga).
  Grind: `datagrid_gate` — R-fallen recepten som LOGIK mot en inspelande
  renderer (`gridRowH` ≠ `listRowH`), E-fallen redigeringen genom fönstret.
- **`dropdown.cpp`** — den stängda kontrollen (fält + `Draw::chevron`) OCH
  popup-maskineriet. **`DropdownRows` är en `ListBox`-SUBKLASS** — radritning,
  hit-test, klampning och scroll bor i listbox.cpp. **`addItem`/`setItem`
  ligger däremot HÄR** — de måste STÄNGA en öppen popup (den har en egen kopia
  av raderna), och `ListWindow` är ofullständig i headern. Popup-specifikt:
  `setBorder(false)`, `showScrollbars(false)`, `ownsSurface() = true` och de
  tre mus/hjul-krokarna. `ListWindow` (Chrome::None + popup): OBJEKTET
  återanvänds, OS-fönstret återskapas per öppning, placering via
  `setPopupAnchor` + `setClientSize` — flip/klamp bor i BACKENDEN. `open_`
  nollas i popupens onClose och som fail-safe i `openAt`.
  **TILLSTÅNDSMODELLEN**: Dropdown äger det COMMITTADE valet, popup-listan det
  PROVISORISKA, och ingen tredje källa. Musen sätter det provisoriska med den
  PROGRAMMATISKA settern (noll event) och ber ÄGAREN committa; popupen går
  ALDRIG genom ListBoxens `selectFromUser` — därför svarar `DropdownRows::
  rightDown` false (listans högertryck hade gått den vägen). TANGENTERNA bor
  på Dropdown (den har fokus; stängd öppnar Space, Enter går till
  standardknappen — VAL 1) — popupens `keyDown` svarar alltid false, för
  en andra tabell hade varit en andra väg in i samma tillstånd. Grind:
  `tests/dropdown_gate.cpp`. **JUSTERINGEN** (post 52): fältets text ritas i
  `textRect` — från `fieldPadX` till `fieldPadX` före pilens ruta — med
  `align_`. **TEXTSTORLEKEN** (REQ8 26) i fältet (`textCtx`) och i popupens rader (`setTextSize` vid öppningen; höjden ur `rowHeight`).
- **`property_grid.cpp`** — kolumnbakgrunder, rader och gridlinjer +
  cellgeometrin (`nameW`/`rowBounds`/`valueBounds`, logiska px, scroll-
  förskjutna) som ritning, cellträff (via `Draw::project`) och
  editor-placering delar. EN hostad editor + EN vallista (ägda barn,
  flyttas till aktiv cell i childArea-logiska px ⇒ följer scrollen). Vallistan
  är `ChoiceEditor : Dropdown` (datagridens form):
  Enter — med listans val när den är öppen, under en `HandlerGuard` — och
  Escape avslutar redigeringen (`hideEditors`) och konsumeras, öppen som
  stängd, så en STÄNGD lista aldrig släpper dem till dialogens knappar.
  **COMMITEN är datagridens form** (0.1.2): `TextEditor : TextInput` lämnar
  Enter (spara) och Escape (avbryt) till `endEdit(spara, postad)` och
  meddelar fokusförlusten (postad); `mouseDown` anropar `endEdit(true,
  false)` FÖRE cellträffen, så en handler som bygger om raderna har kört
  innan nästa editor öppnas — och träffen görs mot raderna EFTER den.
  ORDNINGEN i `endEdit`: läs rad + text, `hideEditors()` (aktiv = −1), skriv
  värdet, fyra/posta sist — `endEdit` svarar som `firePair`, och `mouseDown`
  slutar på false. Ett postat event slopas av `rowsGen_` (`setRows`) och av
  radens `rowGen_[r]` (`setRow` på just raden) — parallell med `rows_`,
  som bara ändras i `setRows`/`addRow`/`setRow` (`rows()` är const).
  `fireChange` sätter `changed_` igen före `onCommit`. Fältets eget
  `onCommit` används INTE: dess fokus-commit hade kunnat landa senare.
  `openEditor` markerar allt. `contentExtent`-override: max(barnen,
  rader × radhöjden). Glaze markerar aktiv rad med `accentSurface` +
  `onAccent`, Minimalistic tar bort BÅDA kolumnfyllningarna och markerar med
  `rowMarkerW`-strecket.
  **`setProp` (nameWidth) och STYCKEFORMEN `addRow`/`setRow`**
  (REGEL8_PREVIEW — settertabellens nyckel har en widget-ände, så previewn
  utför raden). `Panel::setProp` anropas SIST — konventionen. `addRow` gör
  `push_back` + `invalidate()`; `setRow` ERSÄTTER hela raden (utelämnade
  fält nollas — tilldelningssemantik) och stänger en editor som stod öppen
  på just den raden; index utanför intervallet ⇒ `gfx::log` + no-op.
- **`separator.cpp` / `picture.cpp`** — de två ritnings-ENDA nivå
  1-komponenterna. Separatorns `draw()` är EN `Draw::line` mitt i ytan — i en
  `Bar` TVÄRS baren (`drawnVertical`) och tunn på båda axlarna (D15);
  Pictures geometri bor i `imageBounds()` (logiska px, ur
  `Draw::imageSize` + Fit-läget — en Bitmap mäts ur handtaget, pixlar /
  skala, utan uppladdning) och `draw()` klipper bara när recten
  spiller utanför ytan (Fill / för stor Center) och väljer `Draw::glyphResampled` när recten är mindre än det naturliga måttet (REQ8 23), annars `Draw::glyph`.
- **`link.cpp` / `radio.cpp` / `slider.cpp`** — de tre med beteende.
  Link: text + `Draw::line` under TEXTENS bredd + fokusring; `clicked()`
  kör onClick före `gfx::openUrl` (fasadens OS-handling). Radio speglar
  checkbox.cpp med `Draw::circle` + `Pen`; GRUPPEN är syskonvandring och det
  visade läget HÄRLETT ur flagga + stämpel (`isChecked`, 🐛 59 — radio.h).
  Slider: `trackBounds`/`thumbBounds` (logiska px) matar både
  ritning och musmappning; klamp/kvantisering på ETT ställe (`userSet`);
  `value()` klampar VID LÄSNING (FAS1_B_WIDGETS) och en `setMin`/`setMax` som
  flyttar det effektiva värdet flyttar `commitBase_`;
  hjulet stegar bara med fokus (post 45 — utan fokus svarar `mouseWheel`
  false och hjulet bubblar), grindat i `spinbox_gate`.
  Ingen explicit capture — auto-capture under tryck räcker (scrollbarens
  resonemang). Värdet skrivs LIVE under draget; släppkroken `mouseUp`
  COMMITTAR (0.1.2 — `commitFromUser` mot `commitBase_`, som förra commiten,
  `focusGained` och en `setValue` som byter värde sätter), och tangent- och
  hjulsteg går genom `stepFromUser` = `userSet` + commit. Scrollbaren har
  samma par utan fokusdelen.
  Ritgrenarna: Link är Slate även i Glaze (en länk är TEXT), men
  Minimalistic drar bort understrykningen i vila och tänder den vid hover —
  det håller bara därför att `link` är grindad ≥ 4.5:1 och ensam skiljer
  länken från vanlig text. Slidern lamineras i Glaze (tumme + kontur +
  `sliderShadow*`, EGNA skuggfält och inte knappens) medan Minimalistic gör
  spåret till ett STRECK och tummen till en fylld `accentSurface`-cirkel
  **med hårlinje** (fyllningen ensam når inte 3:1 mot bakgrunden).
- **`progress.cpp`** — den enda som rör hård regel 1: obestämt läge
  animerar, men **aldrig via `AppWindow::setAnimating`** (en bool utan
  ägarskap — den hade kunnat slå av APPENS animering): `draw()` invalidaterar
  SIG SJÄLV så länge läget är obestämt, stannar när widgeten göms och ritar
  bara om remsan. Fasen: modulon i `chunkBounds` görs i HELTAL före
  float-konverteringen (`ticksMs()` passerar 2^24 ms efter ~4,7 h drift).
- **`bar.cpp` / `spacer.cpp`** — flex-raden: Bars `placeChild` ÄR flexen
  (stateless tvåpass som dock-carvingen; dolda barn och de utanför `inFlow` tar ingen plats,
  overflow rinner över och klipps) — en STÅENDE bar (`dock` Left/Right, D15)
  går till `placeChildVertical`, samma två pass med y/höjd, så den liggande
  vägen står bit-identisk. **REN RAD** (REQ8 25, `plain_`): `draw()` återvänder före `switch` och `crossSpan` ger barnet sitt mätta tvärmått centrerat i baren (utan eget mått hela baren) — i båda riktningarna. **LUCKAN MELLAN BARNEN** (`childGap`, post 11)
  läggs bara mellan två SYNLIGA barn och räknas in i pass 1:s fasta summa, så
  en Spacer slutar exakt vid innerkanten (`toolbar_gate` har ett handräknat
  Bar-facit). Ritningen är EN rad: ytan i **`windowBg`** via **`fillBody()`**,
  som rundar de ÖVRE hörnen enligt `topRadius()` (0 för en vanlig bar,
  fönstrets radie för TopBar; den nedre halvan fylls fyrkantigt igen —
  `Draw::rect` har en radie per FYRKANT), och defaulthöjden är `barHeight`.
  **Alla tre** ritar **`innerEdge()`** — hårlinjen mot INNEHÅLLET (`dock`
  avgör sidan; ingen när `drawsInnerEdge()` svarar false) — i rollen
  **`frame`**: en FÖNSTER-linje, inte KONTROLLENS `border`. **ALLA TRE
  fyller `windowBg`** (CaptionButton kräver en fylld bar). Riktningen är den
  VIRTUELLA `isVertical()`. Spacer är bara grow-vikten — tom `draw()`.
- **`caption_button.cpp`** — kind/shape/restoreGlyph + setProp (alla tre
  nycklarna har en gren — settertabellen och previewn kräver det); klicket är
  basens `clicked()` → onClick. **`drawCapsule` ligger FÖRE `switch (c.ui)`**
  — likadant i alla tre familjerna, för namnlisten ÄR chrome. Kapselns
  rundade ände görs UTAN per-hörn-radie — kroppen ritas som en rundad rekt
  som sträcks förbi facket åt de inre skarvarna och klipps med
  `Draw::clip` till det egna facket, så bågen hamnar utanför klippet där
  den inte ska synas. `clusterPos()` räknar bara SYNLIGA kapsel-syskon, så
  ett dolt fack ger en kortare kapsel med rätt ändar i stället för en
  lucka. De TVÅ äldre formerna är privata metoder:
  `drawGlyphButton` (platta + `Draw::minus`/`frame`/`cross`) speglar
  backend-chromets drawButton, `drawTrafficLight` ritar `Draw::circle` i
  temats **traffic-roller** (vila/tryck) + hover-glyf vid GRUPP-hover
  (`clusterHot` — macOS-beteendet), utan hårdkodade färger.
  **TECKNET är utbrutet till `drawGlyphs(c, pen, restoreFill)`**, delat av
  alla tre ritningarna: plattan (Slate fylld · Glaze fylld + laminerad +
  hårlinje `border` · Minimalistic ingen platta, bara en hårlinjeruta vid
  hover) ritas av respektive gren, glyfen av den gemensamma metoden.
  `restoreFill` är det som ligger BAKOM knappen och som återställ-glyfens
  främre ruta fylls med — `titlebar` i Slate/Glaze, **`windowBg` i
  Minimalistic** vars bar är fylld i just den. Får den fel värde blir det en
  färgplätt mitt i namnlisten; bar.cpp och den här filen är ETT paket.
  TRAFIKLJUSEN ritas likadant i alla tre — de speglar en macOS-KONVENTION,
  och en gul ring betyder inte "minimera".
- **`menu_bar.cpp`** — MENYRADEN (MENYER): ytan är Bars (`Bar::draw` →
  `fillBody`/`innerEdge` — menyraden är samma sorts remsa som topplisten och
  statusraden och ska se ut så), rubrikerna ritas här. TITLARNA ÄR DATA och
  inte barn, så `titleRect` är EN sanning som ritningen och hit-testen
  (`titleAt` via `Draw::project`) delar. Recepten per UI: Slate fyller den
  ÖPPNA rubriken med `accent`, Glaze med det grindade `accentSurface`/
  `onAccent`, Minimalistic fyller inte alls utan markerar med ett
  `rowMarkerW`-streck i underkanten — samma stark/svag-par som en listrad,
  och INGA nya temaroller. Klick togglar (deferred, Dropdown-ordningen),
  hover på annan rubrik BYTER meny medan en är öppen. Menyernas innehåll
  byggs i appens `onLoad`; `setProp` känner bara Bars nycklar, för ett
  menyträd har ingen textform i grammatiken.
  **STIGEN ÄR SEGMENTERAD**: `resolvePath`/`itemAtPath` tar en `MenuPath` och
  matchar TOKEN mot token via `menuTokenMatches`. LOGGNINGEN är skild från
  MATCHNINGEN: typfelet loggas bara när hela scanningen missade OCH en
  kandidat hade SAMMA upplösta text men fel sort (en separator bär en TOM
  literal). Posttillstånden `setItemIcon/Enabled/Checked` går genom
  `itemAtPath` till `detail::setMenuItem*` (menu.cpp).
- **`app_icon.cpp`** — PROGRAMIKONEN (PROGRAMIKON). `contentSize`
  är geometrins enda källa: FRISTÅENDE (registret, previewn) namnlistens
  kvadrat, utan synlig TopBar eller med `noIcon` {0, 0}, annars kvadraten
  namnlist [+ menyrad när `showsLarge()` — stor begärd, synlig menyrad] med
  axelregeln på barernas `height`; `reservedWidth` läser den. `draw` ritar
  PLATTAN (`capsuleHover`/`capsulePress`, `appIconPlateInset`/`appIconRadius`)
  bara när menyn har poster och är öppen eller hovrad, och bilden med
  `Draw::pickIcon` + `Draw::glyphResampled` — förvalet är
  `detail::defaultIconLarge/Small()`, samma par som OS-ikonen; en Bitmap
  räknas om ur sina levande pixlar per (id, w, h) i bildcachen. `mouseDown`
  togglar i menyradens ordning (popup-stängningen är redan köad); settrarna
  invaliderar, flyttar chromet (`placeChanged`) och sätter om OS-ikonen.
- **`top_bar.cpp` / `status_bar.cpp`** — StatusBar har registreringen och
  en `contentSize()`-override: default-höjden är **`Style::statusBarH`**
  (`setContentSize` räknar baklänges genom chromet, så höjden måste följa
  stilen — inget tal i konstruktorn);
  TopBar har dessutom `topRadius()`, som hämtar `AppWindow::cornerRadius()`
  och delar den med Bars `fillBody()` så namnlistens övre hörn följer
  fönstrets exakt (KAPSEL_CHROME). StatusBars FÄLT (FAS1_B_WIDGETS) är den
  TU-lokala `StatusBar::Cell` — Bar-flexens barn (`flexGrow` = fyllningen),
  hittade bland barnen med `dynamic_cast`, skilda av separatorns penna och
  släckta med raden (`enabledChanged`, också i `addCell`). Plattforms-defaulterna + wiringen byggs
  i `../app_window.cpp` (`buildTopBar`), ritningen är Bars.
- **`file_picker.cpp`** — FILVÄLJARFÄLTET. Kompositen ritar INGENTING själv
  — skrivet beslut i `draw()`: fältet och knappen är riktiga barn, och en yta
  bakom dem hade bara syntes i luften mellan dem. Filen äger PLACERINGEN
  (`placeChild`: knappen i högerkanten så bred som `measure(*button_)`, fältet
  resten minus `filePickerGap`), MÅTTET (AUTO-bredden `filePickerDefaultW`,
  höjden FÄLTETS) och FLÖDET: fältets `onChange` vidarebefordras LIVE och dess
  `onCommit` som det är, med kompositen som sender, och knappens klick är
  `browse()` — `FileDialog` i fältets läge med dess filter och det senast
  valda, i sökvägens mapp med namnet förvalt; ett OK som gav en ANNAN sökväg
  skriver fältet och fyrar `onChange` + `onCommit` en gång. Dialogens kryssruta
  för dolda filer läses tillbaka till `showHidden_` FÖRE OK-kontrollen (den
  gäller också efter Avbryt), men efter livstecknet — den modala pumpen kan
  riva fältet. **`setDialogTitle(TrText)`** (B10; `dialogTitle` i `setProp`
  som literal, i `setTextToken` som tr-token): en SATT titel är dialogens och
  knappens tooltip, annars lägets titel (`syncToolTip`). Grind:
  `filepicker_gate`.
- **`color_picker.cpp`** — FÄRGVÄLJAREN (post 20). Den stängda
  kontrollen är DROPDOWNENS fält per familj, med en färgruta där dropdownen
  har sin text (`swatchRect`/`chevronRect` tar ytan som argument); rutan är
  INNEHÅLL och ritas likadant i alla tre, kontur `buttonBorder` så vit syns i
  vitt, och INAKTIV tonas den med `iconFade` (färgens alfa — ikonernas
  opacitetsregel); med ALFAN står den över `Draw::checker` (`panel`/
  `appWorkspace`, FAS1_B_WIDGETS). Popupen bor här som `Dropdown::ListWindow`:
  `Popup : AppWindow` med `setPopupTakesKeys(true)` (REQ8 27) fylld av den DELADE färgytan
  (`../color_panel.h` — paletten, reglagen, hexfältet och deras regler bor
  där; färgdialogen använder samma) och dess fem krokar: paletten väljer och
  STÄNGER (stängningen köas FÖRE händelsen — menyns ordning), reglagen går
  LIVE och KLARA, hexfältet klart, postat eller inte. Alla vägar in går genom
  `ColorPicker::userColor(c, klar, postad)` mot `commitBase_`. Storleken
  räknas ur ägarens Ctx och sätts FÖRE `show()`. Ytan per familj är
  dropdownlistans/tooltipens, fyrkantig (popupen är opak). Stängd öppnar den
  på tryck, Space, Ned och F4 — inte Enter (VAL 1). Grind:
  `tests/colorpicker_gate.cpp`.
- **`date_picker.cpp`** — DATUMVÄLJAREN. Den stängda kontrollen är TALFÄLTETS form: fältet (`Field : TextInput`) i
  `childArea()` (allt utom knappen och `dateGap`), och kompositen ritar bara KNAPPEN — talfältets platta per
  familj (Glaze laminerad, Minimalistic kant, Slate hårlinje som tänds i `accent`) med `Icon::Calendar`. ÖPPEN
  kalender = `buttonPress`. Medan popupen är öppen når tangenterna aldrig `Field` (ägarfönstret skickar dem till
  popupen), som öppnar på Ned/F4 (efter en commit av texten), COMMITTAR texten på Enter (som sedan svarar false)
  och fokusförlust (`commitText(postad)` + `fireChange`), och sväljer allt utom siffror och `-` (med `showTime`
  också `:`, blanksteg och `T`: `syncText` skriver datum + tid och `commitText` tolkar med `läsIsoTid`, ett byte
  av BARA tiden fyrar paret). Popupen bor här som `Dropdown::ListWindow`: `Popup : AppWindow`
  (`setPopupTakesKeys`) med EN widget, `CalendarView` (namnet `"calendar"`) — titelraden och, i dagläget,
  veckodagarna och 6 × 7 dagar som DATA (ett fokus), geometrin EN gång (`titel`/`pilRect`/`titelKnapp`/`ruta`/
  `dagRuta`/`översiktRuta`) för ritning och träff. Kalenderräkningen är den rena `../date_model.h`. **TRE LÄGEN**:
  dagar · månader · år, i SAMMA ruta (popupen kan inte växa) — översikterna är tolv rutor över dagrutnätets yta.
  Titeln går UPP (`upp`), ett val i en översikt flyttar markören och går sedan ENTERS väg ned (`väljRuta` →
  `väljMarkören`, så musen och tangenterna delar övergången); tangenttabellen per läge står vid klassen. Vyns
  tillstånd är LÄGET, MARKÖREN och den VISADE månaden (följer markören); varje öppning börjar bland dagarna, och
  valet, dagens datum och gränserna sätts vid öppning. **Fyra kanaler per ruta** (`ritaRuta`, samma för dagar och
  översikter): vald = fyllning (Glaze/Minimalistic `accentSurface` + `onAccent`, Slate `accent` + `windowBg`),
  dagens = talet och en hårlinjering i `accent`, markören = fokusringen INNANFÖR rutan (bara med fokus), pekaren
  = `buttonHover`. Utanför månaden eller gränserna: talet i `textDim`. Popupens Tab STÄNGER (vyn är dess enda
  stopp). Valet köar STÄNGNINGEN FÖRE händelsen (menyns ordning). Storleken mäts ur ägarens Ctx och sätts FÖRE
  `show()`. Grind: `tests/datepicker_gate.cpp`.
- **`tool_bar.cpp` / `tool_button.cpp`** — VERKTYGSFÄLTET. `ToolBar`: `sizeFor(c, vertical, withGrip)` är
  EN sanning för måtten (den EGNA längden när ingen är satt; de satta läses i nuvarande riktning; värden
  mäter MED greppet, det flytande fönstret utan), och AUTO-tjockleken är **`autoThickness`** =
  max(`toolBarH`, största synliga verktygsknappsIKON + 2 × (`toolButtonPadY` + `toolBarPadY`)) — IKONEN och
  inte knappens mätta höjd (en TEXTknapp rubbade tjockleken); en 16 px ikon ger exakt `toolBarH`, och andra
  barn räknas inte. `childArea()` är Panels minus greppet och `toolBarPadX` längs / `toolBarPadY` tvärs. I
  en `DockHost`: greppet (`Draw::grip` i `border`), dess drag (`detail::pastDragThreshold`) och
  dubbelklick till värden och flyttpekaren; `setBand` invaliderar värden. **DE TILLÅTNA KANTERNA**
  (`setAllowedEdges`, REQ3_LAYOUT_FONSTER B21): fyra bool, Fill/None loggas; ritar och invaliderar inget —
  bara användarens drag frågar `allowsEdge` (`DockHost::toolDropAt`). FLYTANDE är tillstånd här (fönstret är
  värdens): `turnIfNeeded` är EN regel för riktningsbytet, `isVertical()`/`drawsInnerEdge()`/`inBand()`
  svarar för ett flytande fält, och barnens krokar, språket, `setText` och `~ToolBar` berättar för värden.
  `ToolButton::setIconSize` (B6) lägger om syskonen (`relayoutSiblings`). `ToolButton` följer
  `button.cpp`-mallen utan platta i vila; plattan finns när knappen är aktiv OCH tänd (hover, tryck, öppen
  meny) eller ikryssad, och **ikryssat läge är `buttonPress` med kanten i `accent`** — färgade ikoner
  försvinner på den fyllda accentytan, och `accent` är grindad ≥ 3:1 som tillståndsmarkering. Glaze
  laminerar den tända plattan, Minimalistic fyller bara vid tillstånd; inaktiv ikon tonas med OPACITET,
  aldrig en slöja. `clicked()` växlar FÖRE `onClick`. **RULLGARDINEN** (DOCKNING E4a): med poster delas
  ytan i innehållet och `arrowRect` (EN geometri för ritningen och den delade knappens träff i
  `mouseDown`); pilen står i textens färg och skiljelinjen i `border` bara där plattan finns. `mouseDown`
  sätter `menuPress_` (odelad: hela knappen, delad: pildelen) och `clicked()` gör ingenting för det
  trycket. Toggle i menyradens ordning, `onDropDown` genom `fireEvent` FÖRE `openMenu()` (fönstret ritas om
  efter en körd handler), och `onChainClosed` släcker bara när INGEN presentation lever — en gammal kedja
  som rivs synkront när en ny öppnas får inte släcka den nya. Den första add-satsen lägger om syskonen.
  **ÖVERFLÖDET** (REQ8 9) LÅNAR allt: knappen är `ToolBar::OverflowButton : ToolButton` (stockikonen,
  tooltip 420, `showsArrow()` false, längden `toolOverflowW`; dess `mouseDown` bygger menyn FÖRE basens
  tryck), och klicket `performClick` → `fireClick` (växlingen + `onClick`, delad med `clicked()` —
  `menuPress_` läses inte). SNITTET (`overflowCut`) är tre allokeringsfria pass per fråga (längden och
  fokus, prefixet före knappen, prefixet med det fokuserade): ett överskott under en hel FYSISK pixel
  (`std::floor`) är avrundning, prefixet slutar aldrig på en separator, och i överflödet växer inget.
  Utan överflöde har knappen (och de dolda) en TOM rekt i hörnet, som inte ritas (REQ9 6 — prickarna).
  Menyn: dolda rullgardiners `onDropDown` först (vännen `ToolBar` når knappens `fireEvent`), sedan
  posterna — en knapps klick och speglade poster bär knappens `HandlerGuard` och aktiverar genom KNAPPENS
  meny (`mirrorItems`), en kontrolls post fokuserar det första tabbstoppet i `focusOrder()` inom den
  (`fokusmål`). `childFocusChanged` invaliderar fältet; `enabledChanged` för vidare till knappen.
- **`floating_panel.cpp`** — DEN FLYTTBARA PANELEN (REQ8 14): rubriken ritas av `drawPanelHeader` (../dock/), greppet med
  `Draw::grip`, dragtröskeln `detail::pastDragThreshold`. Läget vid trycket sparas och draget räknas alltid därifrån
  (`moveTo`/`resizeTo` med (0, 0) = avbrottets återgång); på ritytan via `toLocal`/`toWorld` + `setChildAnchor`.
- **`canvas.cpp`** — RITYTAN, den TUNNASTE av widgetarna i förhållande till vad den gör: ytan ritas med `Draw`
  (bakgrund, ett klipp med fältets hörn, kanten OVANPÅ scenen, fokusringen), och allt däremellan är ritytans motor
  i [../canvas/](../canvas/CLAUDE.md), anropad med temats färger och stilens mått. **Ingen `c.r` och inga tal i
  filen** — kameraräkningen bor i `../canvas/camera_math.cpp` av samma skäl. Alla tre `case` delar EN gren med
  beslutet i klartext (ritytan är innehåll); tabellen står i filhuvudet. Kameran görs om till fasadens `MapView`
  på ETT ställe (`view()`), som ritningen, musens världspunkt, `toWorld`/`toLocal` och `unitsPerPixelAt` delar;
  `knownBox` ger den senast ritade ytan (eller en färsk resolve). **DE FÖRANKRADE BARNEN** (REQ8 14): `projectIn`
  är `toLocal`s och `placeChild`s EN projektion; `placeChild` räknas mot `c.area` (ritytans egen yta) EFTER
  `draw()` i ritvandringen, så trögheten som stegas där syns i barnet samma bildruta. Hörnet: `gfx::anchorCorner`.
  **TEXTENS MÅTT** (REQ7 4): de två formerna av `measureText` slutar i `measureWith` (nullopt utan `c.canvas`, och
  för ett världsmått när tätheten är okänd — `detail::measureNeedsWorld`). Formen MED Ctx tar tätheten ur
  `layoutPxPerUnit` — `lastBox_` eller de satta måtten, kameran som den är och en väntande kartskala; ALDRIG
  `knownBox`/`resolve`/`contentSize`, för den anropas ur layouten. Formen UTAN går genom `behaviorCtx` + `resolve`
  och har VAKTEN `measuring_` (RAII nollar den): ett anrop under dess egen layoutvandring loggar en gång per rityta
  och svarar nullopt (canvas_gate MR). **INRAMNINGEN MOT YTAN JUST NU** (REQ9 6): `zoomToRect`/`zoomToBox`/`panBy`
  räknar mot `currentBox` (färsk resolve, bredd och höjd > 0) — aldrig `lastBox_`, som en inaktiv flik (inte ritad
  sedan REQ9 6) lämnar gammal; annars väntar de till `applyPending` i en ritning med yta (canvas_gate FL).
  `onResize` är basens (widget.h). **MINNET** (REQ3 10): `setMemoryBudget` LAGRAR i `scene_->memoryBudget` (0 =
  `App::canvasMemoryBudget()`) och gäller från nästa ritning, och ritytans TRE ritvägar (`drawScene`, `drawOverlay`,
  `renderToBitmap`) öppnar sitt konto i fönstrets bildcache med RAII-typen `detail::ImageAccount` — bilderna räknas
  på den rityta som ritade dem (../CLAUDE.md, `image_cache`); `setProp("memoryBudget")` prövar ändligt, heltal ≥ 0
  och inom `size_t` FÖRE omvandlingen. **MÅLARNA** (REQ8 2) går EN väg, `måla` — `onPaint`, `onPaintOverlay` och
  `onPaintLayer`, med lagret i `PaintArgs::layer` och ett lagers renderläge som start; `lagerMålare` ger den sista
  som `detail::LayerPaint` åt `drawGround` (2D) och `målArgs(yta, c)` — 3D-målets argument (provtagningen,
  rutnätet, `onPaint`, lagrens målare) som `drawScene` och `renderToBitmap` delar. `easeTo` stegas FÖRST i `draw()`
  och invaliderar sig själv tills den är framme (Progress-mönstret; `scene_pixel_gate` F6).
  FÖNSTRET fyrar `onMouseDown`/`onMouseUp`/`onMouseCancel` med ritytans args (`fireMouse` → `mouseArgs`) och
  krokarna gör bara gesten; ett tryck appen TOG når ritytan som `appPress` (Begin fångar en kastad karta, Move
  ger `onMouseMove`, DoubleClick `onDoubleClick`). Ett tryck och ett dubbelklick ritar om fönstret, en rörelse
  gör det inte, och hjulkroken har PEKARENS position. **`onMouseMove` kommer EN gång per rörelse**: hover-kroken
  tiger medan `pressed()` — rörelsen kommer med knappen genom `mouseDrag` (`canvas_gate` E). **GESTERNA** bor i
  krokarna, EFTER appens handler och bara när den inte satt `handled` — och sedan REQ9_INMATNING genom
  BINDNINGSTABELLEN (../canvas/input_map.h): ett tryck läser dragets plan (`dragPlan` → `resolveDrag`, läget ur
  `groupMode`) och `beginDrag` greppar världspunkten under pekaren när planen panorerar; VARJE RÖRELSE läser
  planen igen med fönstrets modifierare (`moveDrag` — Shift mitt i draget byter), summerar axlarnas mängder per
  handling (`addMotion`, `bindingAmount` × `bindingFactor`) och flyttar en VIRTUELL PEKARE med panoreringens
  mängd som greppet ankras under (`gfx::anchorWorldAt`; med förvalen den riktiga), och släppet (`endDrag`)
  startar trögheten ur den virtuella pekarens sampel eller vridsnäppet (`snapRotation` — en EGEN glidning av
  bara bäringen som panorering och zoom INTE stoppar; bara en ny vridning och kamerasättarna gör det).
  Mitten- och högerknappen går samma väg (`drags_[3]`) och TAR trycket bara när planen har en påslagen handling
  (högerknappen alltså bara med en `RightDragX/Y`-rad). Ett drag gäller bara medan knappen hålls, och
  `cursorAt` (`activeDrag`) läser samma sak. Hjulet, styrplattan och tangenterna slår upp sin rad per händelse
  (`resolveEvent`) och utförs av `applyMotion` (vridning, zoom, förflyttning per kamerans sort). TRÖGHETEN stegas i `draw()` (`stepCoast`, efter
  `stepEase`) och invaliderar tills `camera_math` säger att den är framme; `onCameraChange` därifrån POSTAS
  (`postCameraChange` — en åt gången, med livstecken, i App:s RAMKÖ `detail::postFrameWork`, PRESTANDA D2), för
  ingen handler får köras mitt i en ritning. **EFTER DE LÖPANDE HÄNDELSERNA RITAS RITYTAN OM, INTE FÖNSTRET**
  (PRESTANDA D1): `onTick`, `postCameraChange`, hjulets tre grenar, `onGesture` och gestens kamerasteg anropar
  ritytans `invalidate()` (som stegar `damageStamp` och ritar 3D-målet) i stället för `appWindow()->invalidate()`;
  de tre dubbelklicken och tangenterna ritar fortfarande om fönstret. canvas3d_gate T2 och canvas_gate D1.
  **STYRPLATTAN**: ett PRECIST hjul (`appWindow()->isPreciseWheel()`) PANORERAR i stället för att zooma —
  `panAround` ankrar världspunkten under pekaren vid pekaren + `detail::precisePanPx`, exakt också under tilt;
  OS:ets tröghet panorerar likadant, ritytans egen startar aldrig, och utan `pannable` bubblar hjulet
  (`CanvasMouseArgs::precise`). Nypningen och vridningen är `Widget::gesture`: Begin tas bara med zoom eller
  vridning på, och gesten ZOOMAR ELLER VRIDER — aldrig båda (Erik 2026-10-10): före `detail::decideGesture`s
  beslut hålls båda (vridningen måste vara DUBBELT så stor), det hållna av den valda blir en SKULD som
  `detail::catchUp` betalar av (dubbel fart, aldrig ett hopp), och en gest som släpps obestämd gör ingenting.
  Vridningen är `rotateAround` (bäringen MINUS graderna), och ett steg som flyttade kameran fyrar
  `onCameraChange` + ritytans omritning; en sekvens som vred snäpper vid End/Cancel. **VRIDSNÄPPET**:
  `setRotationSnap(steg)`/`setRotationSnapTolerance(grader)` validerar med `camera_math` (förvalen 360 och 7),
  invaliderar ingenting. Grind: `trackpad_gate`. **TRÄFFPROVET** är `detail::hitLayer` per ritat lager,
  uppifrån, med kameran som den är nu och `behaviorCtx()` (kräver ett öppet fönster). **OVERLAY-LAGRET:**
  `hasOverlay` svarar om `onPaintOverlay` är satt, och `drawOverlay` ritar den i de lokala px klippt till ytan
  med ytans hörn — målaren tömmer sin sats innan klippet släpps. **ETIKETTERNA:** `drawScene` ger
  `detail::prepareLayers` toningsflaggan och invaliderar sig själv medan en toning pågår (`labelsAnimating_`) —
  och, i 3D-målet, medan en drapering väntar på bildens budget (`Target3DState::drapesPending`, PRESTANDA C1).
  **PROFILERAREN** (PRESTANDA A1): 2D-vägens faser (`detail::PhaseScope` — CanvasPrepare, CanvasDraw med
  rutnätet, CanvasPaint) och scopes på `glaze.hitTest` (alla tre formerna), `glaze.castRay` och `glaze.heightAt`.
  **3D-VÄGEN (CANVAS_3D):** `uses3DTarget()` — en `Camera3D`, `setMultisample(> 1)`, `setDepth(true)`, en
  del-vy, ett synligt World- eller Terrain-lager eller ett pass — tar `drawScene` till `detail::drawTarget3D`
  (../canvas/target3d.h) med två `PaintContext`, fönstrets och målets (`targetBox`), och stämpeln
  `damageStamp()`; annars 2D-vägen som förut, som släpper gamla mål (`dropViews(id, 0)`). `renderToBitmap` ger
  `detail::renderBitmap3D` ett `PaintContext` med bildens ruta. Med 3D-kameran räknas "mitten" mot dess skugga
  (`mapViewAt`) och `toWorld`/`toLocal`/musens världspunkt mot marken (`projection()`/`view3DFor()`);
  `mainView3DFor` är 3D-mattens, plockningens och `CanvasMouseArgs::ray`s kamera. DEL-VYERNA (`views_`, vy 1
  först) och de EGNA PROGRAMMEN (`SceneData::programs`, namnen unika) följer kollektionsvokabulären, och
  `removeView`/`setViews` numrerar om lagrens vylistor. `setLighting` lagrar ljuset som `paintContext` pekar ut;
  meshar, instanser och punkter är former som andra, utan egna grenar här. **ORBITEN (etapp 11)** går genom
  SAMMA krokar: med `is3D_` (`motionKind` — Orbit, eller FirstPerson i läget "firstPerson" med `lookAround`/
  `moveEye`) ger förvalets rader orbit för vänsterknappen och panorering för mitten och Shift, och
  `applyMotion` kallar `orbitCamera`/`panCamera` (ett anrop per lodrätt läge)/`dollyCamera` (camera_math).
  Ingen egen tröghet och inget snäpp — OS:ets tröghet (Momentum) panorerar som i kartan. Verktygen `orbitBy`/
  `panBy`/`dollyBy`/`easeTo(Camera3D)`/`zoomToBox` går samma väg utan `onCameraChange` (`panBy` och `zoomToBox`
  väntar på ytan, `applyPending`). Ett BYTE AV KAMERASORT mitt i ett drag avslutar draget; en ny `Camera3D` gör
  det inte (förstapersonens tick). Högerknappen har en gest bara med en bindning — annars är trycket appens;
  `mouseCancel` stoppar gesten där den står, och `gesture` fyrar `onGesture` först.
  **TICKNINGEN:** `stepTick` i `draw()` postar EN tick åt gången i App:s
  RAMKÖ (livstecknet; före appens poster, utan budget) så länge `tickAlive()` — `onTick` satt och
  `setTicking` eller en tangent i `tickKeys_` (`keyTaken`: `onKeyDown` tog den)
  som `isKeyHeld` fortfarande svarar för; en följd som tar slut nollar
  `tickRunning_`. **`cameraLanded`** stegar stämpeln på det sista steget av en
  rörelse som stegas i ritningen. **PLOCKNINGEN** (`hitTest(lokal,
  HitOptions)`) routar till den översta vyn, samlar 2D-formerna (`hitLayer`,
  `tolerance` som bas — REQ8 18) och rymdens (`pickShape3D`), skymmer och
  sorterar med RITNINGENS regler (../canvas/CLAUDE.md); RUTAN (`hitTest(Rect,
  HitMode)`, REQ8 19) är `hitLayerRect` i huvudvyn, utan Ground på terräng.
  PRESTANDA C5: terrängens yta ur den FÖRSTA loopen återanvänds i den andra
  (bara Mesh/Area3 utan `opaqueOnly` — instanserna blandar yta och hörn; den
  andra plockar med `PickContext::surface = false`), ett lager utan snap
  (`LayerData::snap`) ger inga hörn och kanter, `HitQuery::noGround` när
  terräng ritas men inte under pekaren, skymmarna samlas bara med
  `occlusion` i 3D-målet (`occluder(post, opacitet)` med lådan), och
  `castRay` räknar skymmaren FÖRST och sätter taket `occlusionReach` — annars
  bygger `pickMesh` BVH:n innan roten prövas. `pickShape3D(post)` sållar
  Mesh/Instances i världens enhet på den CACHADE lådan före placeringen.
  **WIDGETS PÅ RITYTAN:** `clipsChildren()`, och `clipRadius` = `surfaceRadius` × skalan — fältets hörn, 0 på en
  PLATT yta (REQ8 4, `flat_`), som också ritar fokusmarkeringen rak INNANFÖR ytan och ingen kant.

## Fallgropar

Rotens regel 1 (invalidera varje synlig ändring; hover-krokar BARA vid
ändring), lånade barn i rivning och `ctxFor` för behavior-krokar gäller här
ordagrant.

- **`Draw::clip` måste balanseras med `Draw::endClip`** — obalans är ett
  programfel (klippstacken är delad i renderern). **Gäller lika hårt för
  den RUNDADE formen** `Draw::clip(c, r, radius)`: en obalans lämnar inte
  bara en scissor utan också en MASK aktiv, som kapar hörnen på allt som
  ritas efteråt. Backends nollställer maskerna vid varje `beginFrame` — ett
  skyddsnät, inte en ursäkt.
- **En container som ritar sin yta RUNDAD måste override:a
  `clipRadius()`** (RUNDAT_BARNKLIPP) — annars läcker barnen ut i hörnen.
  Radien ska vara KONCENTRISK mot den ritade ytan: mät ytans radie minus det
  indrag `clipRect` gör, som `Panel::innerRadius` gör. Radier hör i övrigt
  hemma i `Style` som alla andra mått — inga tal i widget-filen (regel 3).
- **`c.bounds()` är giltig BARA i `draw()`/`drawAfterChildren()`.**
  Geometri som beteendet också läser måste utgå från
  `Draw::bounds(c, resolve(c))` — annars blir den noll utanför ritvägen.
- **En publik träffväg i LOGISKA px** (`ListBox`/`Table::rowAt`,
  `TreeView::pathAt`) kräver ett öppet fönster (`behaviorCtx`) och ska fråga
  SAMMA privata träff som musvägen (`hitRow`/`hitPosition`) — annars är de
  oense om remsan, ramen och huvudet.
