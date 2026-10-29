# CLAUDE.md — Glaze/widgets/src/dock

**Dockningens interna delar** (med de flytande verktygsfälten och det levande
draget):
modellen, geometrin, layoutsträngen och de flytande panelernas och
verktygsfältens fönster bakom `DockHost`, och panelrubrikens ritning som
`DockPanel` och `FloatingPanel` delar. Widgeten själv —
ritningen, musen, tangentbordet och fokus — bor i
[../widgets/dock_host.cpp](../widgets/CLAUDE.md), headern i
[../../include/glaze/widgets/dock_host.h](../../include/glaze/widgets/CLAUDE.md).
INTERNT (`sash.h`-prejudikatet): en app har inget ärende hit, och ingen
publik header inkluderar något härifrån — `DockHost` håller modellen genom
en `unique_ptr` till en framåtdeklarerad typ. Del av GlazeUI — se
[root /CLAUDE.md](../../../../CLAUDE.md).

## Filer

- **`dock_model.h` / `dock_model.cpp`** — STRUKTUREN och GEOMETRIN (E1;
  banden E4b, overlayn och glidningen E2).
  - `detail::DockModel`: remsor i skärordning (yttersta först), en remsa =
    en kant och celler LÄNGS kanten, en cell = flikar + aktiv + vikt.
    Operationerna `add` (adopt-regeln: innersta remsa), `remove`,
    `dockAtEdge` (ytterst), `dockOn` (Fill = flik; längs remsan = ny cell
    bredvid, målets vikt delas lika; tvärs = ny remsa direkt utanför eller
    innanför), `activate`, `normalize` (fälten vann: ett ändrat `dock` flyttar
    panelen till innersta remsan vid den nya kanten, i modellens ordning; en
    FLYTANDE panel lämnar modellen, E3). `remove` finns också med en PEKARE —
    den jämförs bara, så en död panel går att ta bort.
    Varje operation svarar false när FORMEN blev densamma (`sameShape`,
    vikterna räknas inte) och lämnar då modellen orörd.
  - VERKTYGSFÄLTEN (E4b): `DockModel::bars` är ALLA verktygsfälts ORDNING
    (också huvudytans, utan kant — `barInBand` skiljer dem) med kant och
    BANDNYCKEL per fält (`DockBar`); banden HÄRLEDS (`bandsAt`: grupperat på
    nyckeln, stigande, packat till index 0, 1, 2 …, dolda fält med).
    `addBar` (sist — alltså sist i sitt band; ett FLYTANDE fält läggs inte
    till), `removeBar` (också med en PEKARE som bara jämförs — ett dött fält
    på väg hem), `moveBar` (greppdraget: till ett annat fälts band före ett
    fält, eller ett NYTT innersta band; nycklarna packas — och ett fält som
    SAKNAS sätts in där, DOCKNING_DRAG T8: ett flytande fält som dockas med
    ett drag) och `packBarKeys`. `normalize` tar bort ett flytande fält ur
    ordningen, som en flytande panel ur remsorna.
  - `detail::computeLayout(modell, yta, ctx, live, barSize)` → `DockGeometry`
    i LOGISKA px med origo i värdens hörn: BANDEN (Top, Bottom, Left, Right,
    band 0 ytterst — tjockleken det tjockaste synliga fältets, fälten packade
    med `toolGap` och, när de inte ryms, KRYMPTA bakifrån ned till sin
    kortaste längd — REQ8 9) och zonerna för ett nytt innersta band, handtagsremsor
    (utanför alla panelremsor), panelremsor med skikt, celler med skikt,
    flikrad nederst vid minst två dockade flikar, huvudytan — och OVERLAYN
    (E2), som skär ingenting: vid sin kant i ytan innanför handtagen, i
    panelens föredragna tjocklek klampad mot min/max och ytan, gånger
    andelen. `barSize` mäter ett fält i en riktning (värden äger mätningen —
    den är skyddad) och svarar en `BarExtent` (REQ8 9): måtten och den
    KORTASTE längden (`ToolBar::minLength` — greppet, luften och
    överflödsknappen med överflödet på, annars hela längden). `DockLive` bär det modellen inte äger men layouten ska
    visa: dragens förhandsvisningar (remsans tjocklek, cellernas vikter, med
    index i modellen), GLIDNINGARNA (`grow` — en remsa som kommer eller går
    med sin enda panel, som läggs ut dockad också infälld; `overlay` med sin
    andel), SNAPPENS infällda (`collapsed` — blir handtag) och rubrikdragets
    `dragged` (läggs ut nålad och utfälld, som efter släppet).
  - `docksOpen` (öppen, NÅLAD och inte infälld — E2: en onålad utfälld panel
    är overlayn och står i ingen remsa), `shownTab` (den aktiva om den
    står dockad, annars den första), `openTabs` och `handlesInOrder`
    (infällda OCH onålade — overlayns handtag står kvar; med `live` också
    förhandsvisningens) — EN sanning för värdens tangentbordslista och
    panelens `showsChildren`; geometrin räknar samma regel med `DockLive`
    inräknad.
  - TALEN (Codex-granskningen av E1): `normalizeWeights` (största = 1, golv
    `kMinWeight` = fyra gånger minsta normala float), `weightsForLengths`
    (skiktdragets vikter, i de gamla vikternas skala) och `panelSize` (ett
    mått som inte är ett ändligt tal > 0 är AUTO). `pastDragThreshold` är
    dragtröskeln för dockningens fyra drag (rubriken, fliken, greppet, det
    flytande fältets rubrik — också `tool_bar.cpp`).
- **`dock_string.h` / `dock_string.cpp`** — LAYOUTSTRÄNGEN v2 (E1; `t=`
  verktygsfälten sedan E4b, `f=` de flytande panelerna sedan E3 — nio fält:
  fönstrets x, y eller `-`, innehållsytan, stängd, dockningskanten (också
  `fill`/`none`) och panelens bredd/höjd; `ft=` de FLYTANDE verktygsfälten
  sedan DOCKNING_DRAG — sex fält: fönstrets x, y eller `-`, dold, och
  minnet `dock` (dock-ordet, också `fill`/`none`) och `band`, skrivna sist
  i namnordning; versionen förblir `v2`).
  - `detail::parseLayout(s, spec, fel)` är REN STD och ger en `LayoutSpec`
    med NAMN. Atomisk: hela strängen tolkas innan något returneras.
  - `DockHost::layout()` och `DockHost::setLayout()` är värdens MEDLEMMAR
    men bor här (tooltip.cpp-prejudikatet — en fil som implementerar en bit
    av en klass): de rör panelernas privata `collapsed_`/`pinned_`.
  - `numberText` skriver kortaste exakta formen (`std::to_chars`), `tal` läser
    med `std::from_chars` och avvisar icke ändliga tal.
- **`dock_float.h` / `dock_float.cpp`** — DE FLYTANDE FÖNSTREN (E3, planens
  D10–D13; GENERALISERADE för verktygsfälten i DOCKNING_DRAG T1 — samma
  maskineri, ingen kopia). Namnrymden är `glaze` för fönstren (friend-raden
  i app_window.h träffar `glaze::DockFloatWindow`), posten är `detail::`,
  och destruktorerna står i cpp:n (`unique_ptr` till ofullständiga typer).
  - `FloatBody` fyller fönstret och PLACERAR objektet: panelen över hela
    ytan (dess `dock` är tillbakadockningens minne och skrivs aldrig över),
    fältet under rubriken (`dockHeaderH`). När objektet visas eller döljs
    följer fönstret, POSTAT (rubrikens synlighet räknas inte); dess `draw()`
    frågar värden om något väntar — en flytt, eller ett fält vars mått
    ändrats; `release` är rivningsvägen (`Widget::severChild`).
  - `ToolFloatHeader` (T4) — det flytande verktygsfältets RUBRIK, en intern
    oregistrerad widget som kroppen äger och bara adopterar i fältets
    fönster: titeln (fältets `text`, annars `str::DockToolBar` = 166) och en
    stängknapp, panelrubrikens recept med egen `switch (c.ui)` och samma
    mått (inga nya stilfält, inga nya temaroller). Ett drag i ytan går till
    värden (`toolHeaderDrag`/`toolHeaderDragEnd` — indikatorn i värden,
    släppet på ett mål dockar, utanför gör ingenting i E4), ett dubbelklick
    dockar (`userFloatBar(false)`), ✕ döljer fältet (`closeFloatingBar`).
    `dragging()` läses med `pressed()`, så ett avbrutet tryck släcker
    indikatorn. Ingen fokus. Medan fältet bärs i fönstret utan övertagande
    håller rubriken explicit capture (`holdCarry`/`releaseCarry`, värden tar
    och släpper den), och dess `mouseCancel` går till värdens
    `itemPressLost` (REQ2_INMATNING — som också släcker den gamla vägens
    platshållare, `toolDrag_`/`toolDrop_`).
  - `DockFloatWindow` — två konstruktorer (panel, fält) och en `setUp()`:
    `Chrome::Custom` med namnlist och programikon DOLDA där fönster har
    skärmposition (annars, på Wayland, syns namnlisten som flytthandtag),
    ingen min/max-knapp, ingen knapp i aktivitetsfältet. `open(ägare)` —
    PANELEN: innehållsytan och platsen ur `floatingBounds` genom 78:s API i
    LOGISKA px, gränserna ur panelens min/max plus det UPPMÄTTA chromet;
    FÄLTET: den FASTA storleken (`fitToolBar` — rubriken plus fältet
    liggande, aldrig smalare än stängknappen och dess luft; min == max ur
    den uppmätta klientytan) och platsen ur `floatingPosition`. Okänd plats:
    mitt över ägaren; ett fönster som stängts med ägaren öppnas där det stod
    (kortet); DOLT när ägaren är dold eller objektet stängt. `onClose`
    lägger vetot och stänger OBJEKTET (panelen stängs, fältet döljs) — utom
    när ägaren stänger (`e.reason == CloseReason::OwnerClosing`, TODO 72 —
    kaskaden, vad ägaren än stängs för). `acceleratorFallback` frågar ägarens
    `tryShortcut` (publik sedan REQ3_LAYOUT_FONSTER — ett tecken i panelens
    fält når aldrig hit: text vinner över genvägen), `ownerVisibilityChanged`
    följer ägaren. `itemChanged`
    är titeln (och panelens gränser, eller fältets rubrik och passning).
  - FÄLTETS FASTA STORLEK FÖLJER ALLA MÅTT (Codex-granskningen av
    DOCKNING_DRAG, fynd 9, och av E4, fynd 1) — utan poll, i tre lager:
    1. MÅTTÄNDRINGEN POSTAR SYNKEN när den händer, också för ett DOLT fönster
       som aldrig ritas: barnens krokar (`ToolBar::childAdded`/`Removed` och
       `childLayoutChanged` — visat, dolt, eller ett barns eget mått, som
       verktygsknappens text och bild via `Widget::relayoutSiblings`),
       fältets titel och — för ett flytande fält — språkbytet
       (`ToolBar::languageChanged`) går alla till `DockHost::barChanged`.
    2. RITNINGENS FRÅGA är reserven för det som ändrar måtten utan en
       händelse (ett stilbyte, ett rått layoutfält, en sättare som inte kallar
       `relayoutSiblings`): `fitWanted()` jämför det ÖNSKADE måttet (mätt i
       fönstrets eget Ctx när det är öppet, annars värdens) med det senast
       passade, i `floatsNeedSync` vid varje ritning av värden och fönstret.
    3. `syncVisible` passar fönstret FÖRE det visas — det dolda fönstret har
       inte ritats.
    `fitToolBar` lyfter gränserna (0, 0) före innehållsytan och sätter min ==
    max efter — en fast storlek hade annars hållit kvar den gamla.
  - AKTIVERINGEN (DOCKNING_DRAG L6): `setUp` sätter AppWindows interna
    attribut som vän — en panels fönster `showInactive_` (den FÖRSTA
    visningen stjäl inte fokus: lossningen mitt i ett drag, och en dold start
    där huvudfönstret ska behålla aktiveringen), ett fälts `noActivate_` +
    `toolWindow_` (det aktiveras aldrig). `open(ägare, placering)` tar ett
    BURET objekts plats i placeringsrymden (`initX_`/`initY_` med
    `StartPosition::Default` — inga avrundade logiska px och ingen klämning,
    fönstret som följer pekaren får stå delvis utanför skärmen), och
    `requestSync` frågar värdens `checkCarry` först. `ToolFloatHeader::
    pressPoint()` är det burna fältets grepp.
  - ETT FLYTANDE OBJEKT SOM DÖR (`~DockPanel`, `~ToolBar` — Codex-
    granskningen av E3 fynd 1) säger det till värden i SAMMA ögonblick
    (`floatingItemDestroyed`): fönstret släpper det (`FloatBody::release`),
    glömmer varje widgetpekare (`AppWindow::forgetWidgets` — ingen widget
    rörs) och döljs, modellen tappar det, och posten rivs i svansen. Posten
    vet vilken sort objektet är — kroken körs ur destruktorn, och objektet
    frågas aldrig om sin typ. Annars hade en ritning, en musrörelse eller en
    caret-tick före synken derefererat det döda objektet.
  - `detail::DockFloat` är värdens post {panel ELLER fält, livstecken,
    fönster} med `item()` som widget (pekaren jämförs), och
    `detail::toClientOf` räknar en skärmpunkt till ett fönsters klientrymd
    ur `clientToScreen(0,0)`/`(1,1)` (affin på Windows, macOS och X11).
- **`dock_header.h` / `dock_header.cpp`** — PANELRUBRIKENS RITNING, delad av
  `DockPanel` (dockad, i en värd eller en remsa) och `FloatingPanel` (REQ8 14,
  den fria panelen) — en panel med rubrik är samma sak i dockningen och fritt.
  `detail::drawPanelHeader(c, PanelHeader, text)` ritar ytan, rubrikraden,
  hårlinjen, de tända knapparnas plattor, kanten, titeln och knapptecknen i
  de tre familjernas recept (`switch (c.ui)` bor alltså här och inte i de
  två widgetarna). Widgeten äger GEOMETRIN (sina `*Rect`) och TILLSTÅNDET
  (`hot`/`down` per `detail::HeaderButton`) och skickar dem — ritningen läser
  samma rektar och flaggor som musvägen, så de kan inte glida isär; helpern
  äger bara ritningen. En knapp med TOM rekt finns inte (stängknappen utan
  `closable`, knappnålen på en flytande panel). `freestanding` ger den fria
  panelen kanten runt om i Slate och Minimalistic; Glaze ramar alltid in.
  Ett TÄNT tecken ritas om i full textfärg ovanpå vilotecknet, klippt till
  knappens ruta. `panelGlyphColor` är tecknens vilofärg — och `FloatingPanel`s
  greppets, som är panelens eget och ritas efter rubriken (likaså
  `DockPanel`s fokusring). `ToolFloatHeader` (dock_float.cpp) har ännu sin
  egen `switch (c.ui)`: den ritar bara raden, utan kant och med en elliderad
  titel.

## Kontrakten

- **FÄLTEN ÄR SANNINGEN FÖR KANT OCH TJOCKLEK, STRUKTUREN FÖR GRUPPERING OCH
  ORDNING** (planens D2). Operationerna rör ALDRIG en panels fält — värden
  skriver tillbaka `dock` (`writeBackEdges`) efter en VERKLIG ändring. Det är
  det som gör att indikatorns simulering (D9) kan köra samma operation på en
  KOPIA av modellen: det man ser är exakt det man får. En operation som
  skrev fält hade ändrat panelen redan under draget.
- **REMSANS TJOCKLEK ÄR HÄRLEDD** — det största föredragna måttet bland dess
  öppna, utfällda paneler (inaktiva flikar inräknade, så ett flikbyte inte
  ändrar den), klampat mot min/max där MIN VINNER en krock. En panel som
  släpps in i en remsa kan alltså göra den bredare (D2:s regel, och därmed
  exakt vad indikatorn visar). Ett remsskiktdrag skriver måttet till ALLA
  paneler i remsan, så de är överens igen.
- **STRÄNGEN SPARAR PANELERNAS MÅTT PER PANEL** och `s=` bara kanten
  (Codex-granskningen, fynd 4 — avvikelse från planens `s=kant,tal`, skälet
  står i dock_string.h): en tjocklek på remsan hade varit en andra sanning.
- **SKRIVAREN EMITTERAR BARA DET PARSERN GODTAR** (fynd 5): namnlösa och
  dubblerade paneler utelämnas (loggat), en tom cell och en tom remsa skrivs
  inte, och den aktiva väljs bland de kvarvarande öppna.
- **CELLERNAS FÖRDELNING** är vattenfyllning med gränser: proportionellt
  efter vikt, sidan som bryter mest låses varje varv (högst ett varv per
  cell). Längs remsan finns ingen huvudyta — ett överskott när alla cellerna
  nått sitt största mått fördelas ändå efter vikt, och ryms inte minimimåtten
  klipps cellerna i ordning. Den räknas i DOUBLE med vikterna relativt den
  största (fynd 1: två giltiga vikter på 3e38 summerade i float är ∞), och
  längderna är ändliga och ryms i remsan.
- **VARJE VIKT SOM SKAPAS ÄR ÄNDLIG OCH > 0** (fynd 4): `dockOn` normaliserar
  remsan före halveringen — en vikt som halverats många gånger hade annars
  underflödat till 0, som `layout()` skrev och `setLayout()` avvisade. Vikter
  ur strängen lämnas som de står (de är redan giltiga).
- **CELLSKIKTETS FÖRHANDSVISNING ÄR VIKTER** (fynd 5): draget räknar
  `weightsForLengths` för remsans ALLA synliga celler (paret får dragets
  längder, de andra behåller sina), och layouten ritas ur dem
  (`DockLive::cellWeights`) — en simulering, som indikatorn. Släppet skriver
  exakt de vikterna, så det man ser är det man får. Längden klampas mot båda
  cellernas golv (> 0) och tak; ryms inte båda golven står skiktet still.
- **ETT PANELMÅTT LÄSES GENOM `panelSize`** vid användning OCH sparning (fynd
  7): −1, NaN och ∞ är AUTO, och skrivaren emitterar 0 för dem.
- **VERKTYGSFÄLTENS BAND ÄR HÄRLEDDA UR FÄLTEN** (E4b, D14): kanten och
  nyckeln är `dock`/`band()` — synken läser av dem, ordningen är
  adoptionens — så previewn (fälten efter trädbygget) och appen (före) får
  samma band ur samma tal. Grupperingen är ordningsoberoende: ett band
  utanför antalet blir ett nytt innersta oavsett i vilken ordning fälten
  kom. Värden skriver tillbaka kant och PACKAT index bara efter en
  användarändring (`writeBackBars`, med bredd ↔ höjd vid ett riktningsbyte),
  så `band()` kan vara glest tills dess; strängen skriver det packade. Ett
  fält UTAN kant står kvar på sin adoptionsplats i ordningen, så ett som får
  en kant efter adoptionen hamnar lika i appen och previewn (Codex E4b,
  fynd 4). `setLayout` översätter de ONÄMNDA fältens nycklar till de
  packade indexen innan de sammanfogas med strängens (fynd 2), och ingen
  fältrektangel når förbi sitt bands slut (fynd 3). Ryms fälten inte KRYMPER
  bandet dem bakifrån — det sista först — ned till sin kortaste längd (REQ8 9,
  `BarExtent::minLength`), så varje fält med överflöde behåller sin
  överflödsknapp och de dolda knapparna nås i dess meny; det som ändå inte
  ryms kortas vid slutet som förut. Krympningen är geometri och inget
  tillstånd: platshållarens simulering räknar samma regel.
- **`t=` ÄR KONTEXTFRI** och delar namnrymd med `p=` (objektens namn);
  platsen räknas bland ALLA bandets fält, så ett utelämnat namnlöst fält
  lämnar en lucka men aldrig två fält på samma plats — som tolkaren avvisar.
- **ETT FLYTANDE FÄLT HAR EN EGEN TAGG, `ft=`** (DOCKNING_DRAG T7) och inte en
  utökad `t=`: `t=`s platsunikhet och packade index gäller dockade fält, och
  en flytande post hade fått undantag i varje regel. Samma namnrymd som de
  andra taggarna (ett namn i både `t=` och `ft=` avvisas), en `t=` för ett
  fält som flyter dockar det, och ett flytande fält som strängen inte nämner
  fortsätter att flyta. Storleken står inte med — den är fältets egen.
- **INDEX I `DockLive` ÄR MODELLENS** — värden löser dem ur dragens PANELER
  vid varje fråga (`DockHost::live`), så en modell som ändras mitt i ett
  drag (en timer, en `post`) inte förhandsvisar fel remsa; cellskiktets
  vikter gäller bara så länge remsan har lika många celler som när de räknades.
- **FOKUSVAKTEN** (`DockHost::guardFocus`, D20 och fynd 2–3) körs efter
  VARJE ändring av synlighet, aktiv flik och infällning — också appens
  `show()`/`hide()` och `setLayout` — och värden tar fokus bara som
  tabbstopp; annars töms fönstrets fokus (`AppWindow::clearFocus`).
- **FLYTANDE: TILLSTÅNDET FÖRST, TRÄDET I SVANSEN** (E3, D10; verktygsfälten
  DOCKNING_DRAG T1). `setFloating` — panelens och fältets — ändrar flaggan
  och modellen DIREKT (`isFloating()` och `layout()` svarar), och
  `DockHost::syncFloats` flyttar delträdet med `Widget::transferChild`
  (ägandet följer med) i pumpens svans. En flytt som en öppen popup eller
  meny i objektet nekar VÄNTAR utan logg (`floatBlocked` före
  `transferChild`) — inga omförsök: nästa ritning av värden eller det
  flytande fönstret, eller `shownChanged`, postar synken igen (Codex 7).
  Ett objekt PÅ VÄG HEM står i modellen innan det är värdens barn. Ett fält
  vars EFFEKTIVA riktning byts (ett stående som börjar flyta, ett flytande
  som dockas vid en stående kant) byter bredd och höjd — EN regel,
  `ToolBar::turnIfNeeded`, i `setFloating`, `writeBackBars`, `applyTool` och
  `setLayout`.
- **MODELLEN HÅLLER BARA BARN OCH LEVANDE HEMVÄNDARE** (E3; verktygsfälten
  likadant sedan DOCKNING_DRAG T1): en hemvändare som dör tas bort DIREKT
  (`floatingItemDestroyed`), och `pruneModel` är skyddet bakom — för
  panelerna OCH fälten: den körs FÖRST i varje synk (före `normalize`, som
  läser objekten), i `syncOverlay` och — sedan Codex-granskningen av E3,
  fynd 2 — före varje läsning av cellens flikar eller remsans paneler utanför
  en synk (Ctrl+PageUp/PageDown, skiktens släpp), och tar bort varje annan
  pekare utan att dereferera den. Ett FLYTANDE fält står aldrig i modellen
  (`normalize`, `addBar`); ett på väg HEM gör det, sist i band `band()` vid
  `dock` (T6 — D14:s adoptionsregel; platsen i bandet minns inte).
- **PLATSHÅLLAREN ÄR SLUTGEOMETRIN också för ett flytande fält** (Codex-
  granskningen av DOCKNING_DRAG, fynd 8): simuleringen sätter in fältet i en
  KOPIA (`moveBar`) och mäter det MED greppet (`layoutIn` → `ToolBar::
  sizeFor(c, riktning, true)`) i bandets riktning — de satta måtten läses i
  den riktning fältet har NU, så originalet rörs inte och svaret är ändå det
  släppet ger. Ett flytande fält har inget grepp förrän det dockats; utan
  `withGrip` hade platshållaren varit en greppbredd för kort.
- **`applyTool` DOCKAR ETT FLYTANDE FÄLT** (fynd 1, panelvägens `apply`):
  fältet ska vara MITT (`mine` — ett barn eller i ett av mina fönster),
  sätts in på målets plats, slutar flyta med riktningen följd
  (`turnIfNeeded`), och flytten hem postas.
- **RIVNINGEN VANDRAR INGET DELTRÄD** (E3, Codex 1–2): värdens destruktor
  lossar levande paneler och fält ur kropparna med `severChild` (förälder
  och fönster nollade, ättlingarna orörda — ett lånat barn kan redan vara
  dött), jämför bara döda, och förstör ägda uttryckligen; sedan rivs
  fönstren.
- **SJÄLVTRÄFFEN GÄLLER BARA HUVUDYTANS PANELER** (Codex-granskningen av E3,
  fynd 5): `dropAt`s regel "över sig själv i huvudytan = inget mål" kräver
  ett barn utanför modellen som INTE flyter — en flytande panel prövar
  kantzonerna, också en som just lossnat och ännu väntar på sitt fönster.
  Annars hade en tom värd, där huvudytan täcker allt, nekat varje kantzon.
  Ingen reserv HEM finns i draget (DOCKNING_DRAG L2, `Drop::Home` är borta):
  ett släpp utan mål lämnar objektet där det är.
- **DRAGET ÄGS AV VÄRDEN** (DOCKNING_DRAG L1–L10, ersätter D11:s konturdrag;
  koden i ../widgets/dock_host.cpp, avsnittet "det levande draget"). Ett
  rubrik-, flik- eller greppdrag som passerar tröskeln där draget kan BÄRAS
  (`canCarry`: skärmpositioner, ett öppet aktiverat värdfönster som inte är
  offscreen, och ett dockat objekt som får flyta) LOSSAR objektet: `carry_`
  får objektet med sitt livstecken, GREPPET (källans fysiska px gjorda till
  placeringsenheter med källans `clientToScreen`) och fönstrets hörn `at`;
  objektet blir flytande utan `onChange`, och fönstret öppnas i svansen på
  `carryPlacement`. FALL A (dockat): värden tar explicit capture, nollar
  objektets dragflaggor, och rörelserna och släppet når värdens
  `mouseDrag`/`mouseUp` — också sedan objektet bytt fönster. FALL B (ett
  flytande objekts eget drag): trycket stannar i dess fönster, som flyttas
  med `setPosition`, och OBJEKTET håller explicit capture där (panelen,
  fältets rubrik) så att fönstrets avbrott når värden (`mouseCancel` → `itemPressLost`)
  också när inget ritas; för ett FÄLT tar värden över trycket EN gång vid
  lossningen (`takeOverToolPress` → `takePointerCapture`, Codex 2) — de
  andra fönstrens avbrottskrokar körs då, så värden, källan och dragets
  identitet (`Carry::serial`) prövas igen efteråt, också vid false; svarar
  den false fortsätter draget i fältets fönster. SLÄPPET markeras
  (`Carry::releasing`) innan det flyttar fönstret — objektets tryck är redan
  släckt, och en ritning mitt i släppet (en flytt till en skärm med annan
  DPI ritar synkront) hade annars avslutat draget; `carryMove` prövar draget
  igen efter `setPosition`. Ett släpp FÖRE svansen lämnar en LANDNING
  (`landings_`): bärandets plats och en panels lyft väntar tills `syncFloats`
  öppnat fönstret (`carryPlacement`, `landed`) — appens egen plats
  (`floatBoundsChanged`, `setLayout`) vinner, och ett objekt som dör eller
  dockas tappar sin. Punkten räknas alltid ur KÄLLANS händelse och
  `clientToScreen` (L4, ingen global pekarfråga), och källan följer med
  varje anrop — den lagras aldrig. `forgetPanel`/`forgetBar` rör INTE
  `carry_`: flytten till det egna fönstret går genom `childRemoved` och hade
  annars dödat draget. SLUTET (`finishCarry`, Codex 3) tar en ögonblicksbild,
  städar capture och tillstånd och kör sedan handlarna — EN `onChange` per
  drag, vid släppet; ett AVBRUTET drag (värdens `mouseCancel` i fall A,
  objektets `mouseCancel` → `itemPressLost` i fall B — men inte källans
  avbrott medan värden TAR ÖVER trycket, `Carry::takingOver` —, `checkCarry`
  i ritningen som reserv, ett nytt
  tryck, objektets död i `floatingItemDestroyed`, en hemflytt i `syncFloats`
  av ett objekt som bars i sitt fönster) lämnar objektet flytande där det
  står och POSTAR `onChange` (`postEvent`). Ett objekt som dör mitt i draget släpper sitt
  livstecken först (under destruktorn lever det ännu), så slutet varken
  skriver i det eller ger det ett `onChange`. DEN GAMLA VÄGEN (L10 — Wayland,
  headless, `setFloatable(false)`, en offscreen-värd): platshållaren medan
  objektet står kvar; ett flytande objekt har där inget mål.
- **KNAPPNÅLEN ÄR EN FLAGGA PÅ PANELEN, inte en plats i strukturen** (E2,
  D4): en onålad panel står kvar i sin cell — `docksOpen` släpper den bara ur
  layouten — så knappnålen i overlayn lägger tillbaka den exakt där den
  stod, och `layout()` skriver den med sin plats (infälld). Overlayn och
  glidningen är DRIFT och finns bara i `DockLive`: ingen operation och ingen
  sträng känner dem.

## Fallgropar

- **Ingen rad här får läsa `c.box`** — `computeLayout` körs från
  `placeChild` (inne i `resolve()`). Fonten (handtagens textbredd) kommer ur
  `c.font`, som därför måste finnas i ett Ctx som placerar en värd —
  layoutriggar i grindarna sätter en falsk font (dock_gate).
- **Glidningens andelar räknas EN gång per bild** (`DockHost::stepAnims`,
  postad av `draw()` till svansen FÖRE nästa bilds skadeinsamling) och LÄSES
  av allt annat via `DockLive` — en andel räknad ur klockan i
  `computeLayout` hade gett ritningen och barnens placering i samma bild
  olika tal, och ett steg i `draw()` hade stegats en gång per skadeområde.
- **`normalize` måste köras innan en operation** på den riktiga modellen —
  värden gör det i `syncModel()` (först i `draw()` och i varje operation).
  Previewn sätter fälten EFTER trädbygget och kör sedan `dockPanel`; utan
  synken hade operationen sett de gamla kanterna. De CONST-vägarna
  (geometrin, `layout()`, träffprovet, tangentbordslistan, `showsPanel`)
  läser en synkad KOPIA (`DockHost::synced()`), så de svarar mot fälten
  också innan nästa ritning — och `DockLive`s index räknas i samma kopia.
- **Döda paneler**: modellen håller råa pekare och derefererar dem bara
  medan panelen är värdens barn eller en levande hemvändare (`childRemoved`
  och `pruneModel` tar bort den). En panel som dör som värde-medlem under
  fönstrets rivning rörs inte — vektorn av pekare rivs utan att derefereras.
- **Ett flytande fönster ritas bara när det syns**: i ett dolt fönster
  postar ingen ritning synken, så en väntande flytt där görs först när
  fönstret visas eller tillståndet ändras — och ett fälts fasta storlek
  passas därför i `syncVisible`, FÖRE visningen.
- **Ett buret fönster flyttas mellan två händelser**: Win32-meddelanden bär
  KLIENTkoordinater, så en grind som postar ett drag till det burna
  fönstret räknar varje punkt mot fönstrets NUVARANDE hörn och postar nästa
  först när den förra levererats (dock_gate `postatDrag`) — annars hamnar
  släppet förskjutet med varje rörelse. Två tryck på SAMMA klientpunkt i
  samma fönster inom `doubleClickMs()` är ett DUBBELKLICK — som växlar
  flytande och river fönstret — så ett andra drag tar en annan punkt.
- **Allt efter en krok i ett annat fönster prövas igen**: `takePointerCapture`
  kör de andra fönstrens `dropPointer` (drag-and-drop-avbrottets handlers är
  användarkod), och `setPosition` kan rita synkront — efter båda läser
  värden dragets identitet (`Carry::serial`) innan något i `carry_`
  derefereras.
- **Synken får aldrig rita om utan en verklig ändring**: medan en flytt
  väntar på en popup frågar VARJE ritning av fönstret efter synken igen, så
  en ovillkorlig `invalidate()` i synken (rubriken, fönstret) hade snurrat
  pumpen (regel 1). `itemChanged` ritar om rubriken bara när titeln byttes,
  och `fitToolBar` gör ingenting när måttet är det passade.
- **Fältets fasta storlek byts genom att gränserna LYFTS** (`fitToolBar`):
  ett öppet fönster blir alltså storleksändringsbart ett ögonblick och fast
  igen — på Windows betyder det att stilen (och storleksramen) slås av och
  på, och backenden måste bevara klientytans plats och storlek över bytet.
