# CLAUDE.md — apps/demo/src

Exempel-appen **och referensmönstret** för hur ett GlazeUI-fönster skrivs.
Nya fönster (här eller i andra appar) kopieras från tre-fils-tripletten.
Del av GlazeUI — se [root /CLAUDE.md](../../../CLAUDE.md) för
tre-fils-modellen och de hårda reglerna.

## Filer — ett fönster = tre filer (alla committade, alla kompileras)

- **`start_window.h/.glazeui/.cpp`** — huvudfönstret (mönster-tripletten).
  `.h` har EN `#pragma region GlazeBuilder` (ctor-deklaration +
  widget-VÄRDE-medlemmar + `initializeComponent()`-deklaration — ägs av
  GlazeBuilder); allt utanför regionen är användarens (handlers, `frame`/
  `event`-overrides, `second_`-medlemmen, `clicks_`).
  `.glazeui` är 100 % GENERERAD: ctor `{ initializeComponent(); }` +
  `initializeComponent()` (satserna i KÄLLORDNING sedan APP_API_2 steg 1:
  fönsteranrop → per-widget-block → event-kopplingar → adopt-rader;
  adopt-ordningen ÄR z-ordningen). TILLSTÅNDSFÄLTEN skrivs som SETTER-
  ANROP (`okBtn.setText("Tryck här");` — B2), layoutfälten som fältrader.
  `.cpp` är 100 % användarens: handler-kroppar. **Startlogiken bor i
  `onLoad`** (`setAnimating(animate.isChecked())`) — aldrig i ctorn (genererad
  plumbing). `temaDd` = tema-dropdown på startsidan (icke-modala
  Dropdown-referensen — Om-dialogen är den modala; startvärdet synkas i
  `onLoad`). `skalaDd` = användarskalans dropdown (L12/US: 0.8–1.25;
  handlern kallar `glaze::app().setUserScale` — ALLT växer, logisk
  storlek är invarianten; startvärdet synkas i `onLoad` mot
  `app().userScale()`; demon lagrar inget mellan körningar — appen äger
  lagringen).
  **De fyra utseende-väljarna** (2026-08-12) visar att axlarna är skilda:
  `temaDd` = LÄGET (System/Ljust/Mörkt), `uiDd` = RITNINGEN
  (Glaze/Slate/Minimalistic), `themeDd` = FÄRGEN och `styleDd` = MÅTTEN.
  De två sista fylls ur det VALDA UI:t (`glaze::themeCount/themeAt` resp.
  `styleCount/styleAt`) — teman och styles hör till sitt UI, det finns
  ingen global lista. `uiDdChanged` kallar `setUI` (som återställer de två
  andra till familjens defaults) och kör sedan
  `syncThemeAndStyleDropdowns()`, som `onLoad` också gör — EN väg, så start
  och drift inte kan glida isär. Glaze har med flit bara ett tema och alla
  tre bara en style: dropdownen visas ändå med sin enda rad, för att en axel
  finns med ett val är information. Panel-exemplet (`panel` + `inre`/`scrollTxt`/`klipps`) ligger
  i `.glazeui` — nästling uttrycks med `panel.adopt(barn)`.
  **`komp`-panelen** (nivå 1-komponenterna, L1) visar alla sex nya
  widgets så de går att PROVA: `kompSep` (Separator), `kompBild`
  (Picture), `kompLank` (Link — handlern körs före `gfx::openUrl`),
  `valA`/`valB` (Radio i samma `group` — gruppen är ETT tabbstopp,
  pilarna flyttar valet), `skjut` + `skjutVarde` (Slider vars onChange
  syns i en label OCH driver förloppet) och `forlopp` + `obestamd`
  (Progress i bestämt läge, checkboxen slår om till obestämt = animation
  som stängs av igen när den kryssas ur).
  **LAYOUTEN ÄR GRUPPERAD I FYRA PANELER** (2026-08-13, Erik): `utseende`
  (de tre axlarna + Läge + Skala, etiketten ÖVER sin dropdown), `prova`
  (knapp/checkbox/fält/status), `komp` (nivå 1) och `panel` (klippning).
  Före det låg allt löst på roten i en ordning som inte följde vad som hörde
  ihop — de tre axlarna var utspridda i ett rutnät blandat med Läge och
  Skala, och etiketterna satt till HÖGER om sina dropdowns. Sidan inleds med
  `rubrik` + `intro1`/`intro2` som säger vad ramverket ÄR: demon är det
  första någon ser av GlazeUI.
  **Ankringen är resize-referensen:** högerpanelerna (`komp`, `panel`) är
  vänster+topp+HÖGER — alltså sträckta, `width` ignoreras då av `resolve` —
  och deras remsor (`kompSep`/`skjut`/`forlopp`/`panelSep`) sträcks med dem
  på samma sätt. Utan det klipptes de fasta 392 px långt före minimibredden.
  **SYSKONANKARE genomgående sedan 2026-08-19** — startfönstret är
  referensen för formen, och det är därför de spikade talen är borta:
  `temaBtn.anchorLeft = {"okBtn", 12};` i stället för `x = 160`,
  hela utseende-RUTNÄTET (etikett över dropdown, kolumn 2 mot kolumn 1,
  rad 2 mot rad 1) och `komp`-panelens KEDJA på sju led
  (`kompBild → valA → valB → skjut → skjutVarde → forlopp → obestamd`).
  Panelerna ankras mot varandra: `prova` under `utseende`, `komp` bredvid
  `utseende`, `panel` bredvid `prova` och under `komp`.
  **Poängen är stilbytet, och den är verifierad renderad:** i Minimalistic
  är knapparna ~18 px höga mot Slates ~27 och märkbart smalare — med
  `x = 160` gapade raden, med ankaret står de 12 px isär i BÅDA. Ett spikat
  tal är en POSITION och sann i exakt en Style; ett ankare är en RELATION
  och sann i alla. **NAMN-RADEN står på VARJE nod sedan 2026-08-19**
  (`rubrik.name = "rubrik";`): den ÄR medlemsnamnet speglat till runtime,
  och det är den strängen ankaret och `find<>()` slår upp. Fram till dess
  emitterades den bara för ankrade MÅL, och hette `id`.
  **BOTTENRADEN (`prova`, `panel`) är BOTTEN-ankrad med `bottom = 88`, och
  det är en KORREKTHETSFRÅGA och inte kosmetik:** knappraden nederst är
  botten-ankrad, så med FASTA panelhöjder berodde gapet mellan panel och
  knapp på fönsterhöjden — vid minimimåttet blev det ~18 px, och en grövre
  Style (knapphöjden kommer ur Style, panelens `height` gjorde inte det)
  åt upp det så knapparna gled upp ÖVER panelerna. Med botten-ankare är
  avståndet konstant oavsett höjd OCH Style. Verifierat renderat vid
  minimimåttet i både Glaze och Slate.
  **`setContentSize` + `setMinimumSize`** (APP_API B1): storleken sätts i
  INNEHÅLLSYTAN. Minimimåttet är **räknat ur layouten**, inte gissat:
  760×668 för startfönstret. Galleriets sidor är FASTA layouter (fyra
  kolumner à 304) och dess minimimått är därför designmåttet, räknat i
  ctorn — 1302×690; kan sidan inte krympas utan att en kolumn klipps ska den
  inte låtsas kunna det (men en för LÅG sida rullar).
  `second_window` likaså 380 i bredd (texten klipps under det).
  **Läxan: ett gissat minimimått är värre än inget** — de första försöken
  (720×560 resp. 900×640) var båda mindre än vad layouten tålde och
  garanterade alltså precis den krock måttet skulle förhindra.
  **Måtten är AUTO där de kan vara det** (axelregeln, 2026-08-12): knappar,
  fält och dropdowns saknar `width`/`height`-rader helt och tar därmed
  STILENS mått — det är precis det som gör stil-växlaren ärlig, och det är
  demons uppgift att visa. Kvar med tal, med avsikt: `panel`/`komp`
  (containrar dimensioneras av appen), `kompBild` 32×32 (avsiktlig
  nedskalning) och remsornas `kompSep`/`skjut`/`forlopp` = 236 — de ska
  SPÄNNA panelen, och stilens defaultlängd 200 är fel just där. Lägg inte
  tillbaka mått som bara råkar vara stilens.
  `galleriBtn` ("Alla widgets…") öppnar widget-galleriet som ÄGT fönster.
  `frame()`-override för egen ritning, `event()`-override binder **F12 →
  `setDebugDamage`**.
  **PROGRAMIKONEN (PROGRAMIKON, 2026-10-11)** — demon visar alla tre
  tillstånden, ett per fönster: startfönstret FÖRVALET STORT
  (`appIcon.setLarge(true);` i `.glazeui`, GlazeUI-ikonen över namnlist +
  menyrad) med en PROGRAMMENY ur befintliga strängar (1002 Nytt fönster ·
  1003 Alla widgets · 1012 Om · 1004 Avsluta, `appIcon.addItem({}, …)` —
  roten är den tomma stigen) vars handlers `byggMenyer()` kopplar med
  `appIcon.setHandler(<text>, …)`; programmenyn har INGA genvägar, de bor
  kvar i menyraden (programmenyn ersätter inget). Galleriet visar
  KODVÄGEN liten: `appIcon.setIcon(glaze::img("toolbar", "gear"))` och
  träd-API:t `appIcon.menu().addItem(…)`. `second_window.glazeui` bär
  `appIcon.setNoIcon(true);` — ingen ikon och ingen plats. Programfilens
  ikon (exe:n, bundlen) kommer ur `glaze_app_icon(demo DESKTOP
  demo.desktop)` i `../CMakeLists.txt`; `../demo.desktop` är demons egen
  committade skrivbordsfil för Linux (`Icon=demo`, `StartupWMClass=demo` —
  app_id:t är programfilens namn).
  **MENYERNA (MENYER, 2026-08-19)** — demon visar BÅDA vägarna, som hård
  regel 8 kräver: `.glazeui` bär hela STRUKTUREN som platta satser
  (`menuBar.addMenu("Arkiv");` · `menuBar.addItem("Arkiv", "Nytt fönster",
  "Ctrl+N");` · `addSeparator`/`addSubmenu`/`addCheck`/`addRadio` — stigen
  är TITELVÄGEN, "Utseende/Läge"), medan `byggMenyer()` i `.cpp` bara
  kopplar HANDLERNA med `setHandler` på samma stigar. Previewn visar därför
  menyraden med Arkiv/Utseende/Hjälp. Menyerna visar med flit ALLT som
  finns: separator, genvägar (`Ctrl+N`/`Ctrl+G`/`Ctrl+W` — acceleratorerna
  fungerar utan att menyn är öppen), en UNDERMENY med en RADIOGRUPP (Läge)
  och kryssrader. **Snabbmenyn** hänger på `prova.onContextMenu` och visar
  routingen: högerklick på knappen eller fältet INUTI panelen når panelens
  handler, för routingen är en ROUTED LOOKUP uppåt i `parent()`-kedjan.
  `snabbmeny_` är en MEDLEM och inte en lokal variabel — `popupAt` är
  icke-blockerande, och en lokal `Menu` hade förstörts direkt (dess
  destruktor stänger sina presentationer). `snabbmenyBegard` muterar menyn
  FÖRE `popupAt` (raden "Kopiera namnet" inaktiveras när fältet är tomt),
  eftersom presentationen tar sin SNAPSHOT vid öppningen och en ändring
  efteråt inte slår igenom. `syncMenyTillstand()` speglar kryss och radio
  mot appens faktiska tillstånd och kallas från varje väg som ändrar samma
  sak på annat håll — menyns tillstånd är DATA appen äger, ramverket synkar
  det aldrig åt en.
  **KOMPENSATIONEN FÖR MENYRADEN ÄR BORTA** (2026-08-19): när menyraden kom
  in flyttades hela layouten ner 24 px, eftersom en Top-dockad remsa
  skar innehållsytan men INTE syskonens area. Sedan
  `Widget::carveSiblingDocks` gör det automatiskt är `rubrik.y` tillbaka på
  **14** (var 38) — resten hänger i ankarkedjor och följde med gratis.
  `setContentSize(880, 644)` och `setMinimumSize(760, 668)` står KVAR
  oförändrade, och det är räknat och inte gissat: en Top-carve flyttar
  `area.y` men lämnar `area.y + area.h` orörd, så de botten-ankrade
  knapparna står stilla och klientstorleken behöver inte ändras. Verifierat
  RENDERAT (glaze-render) i Slate och Minimalistic vid minimimåttet.
- **`widget_gallery.h/.cpp`** — **galleriet: alla widgets, var och en i
  minst TVÅ tillstånd** (normalt + inaktiverat), öppnat från startsidans
  `galleriBtn` ("Alla widgets…") eller direkt med **`demo --gallery`**.
  **HANDKODAT sedan 2026-08-19 (Erik) — ingen `.glazeui`, ingen
  `#pragma region`, inga värde-medlemmar.** Det är demons halva poäng:
  startfönstret visar DESIGNERvägen (dock, anchors, syskonankare, allt
  emitterbart), galleriet visar samma ramverk byggt HELT i C++. Hård
  regel 8 säger att vägarna är likvärdiga, och två fönster som gör det är
  ett bättre bevis än en mening.
  **VAD OMSKRIVNINGEN AVSLÖJADE**, och som är skälet att skriva ner den:
  den genererade vägens `onLoad` bar ~30 rader `disable()` plus
  `showScrollbars`, `setIndeterminate`, `setChecked(rad, på)` och
  `setRows()` — allt sådant som inte går att uttrycka som en FÄLTSÄTTNING.
  (Alla är numera METODANROPSSATSER som previewn UTFÖR — `setChecked`/
  `setRows`/`addRow` sedan 2026-08-20, resten sedan 2026-08-13 — så gränsen
  mellan vägarna går vid LOOPAR och LAMBDOR, inte vid tillstånd.)
  Handkodat sätts varje tillstånd DÄR
  WIDGETEN SKAPAS, och `onLoad` behövs inte alls. Det är inte ett fel i
  `.glazeui` — det är GRÄNSEN mellan vägarna, gjord synlig.
  Formen skiljer sig därefter: `add<T>()` ger pekare, grupp-panelen är en
  HJÄLPFUNKTION (`grupp()`) i stället för fem utskrivna satser × åtta, och
  radiogruppen/panelbarnen byggs i LOOPAR. En loop går inte att emittera i
  grammatiken — och det är ingen brist i grammatiken utan i vad en
  WYSIWYG-yta kan visa. Åt andra hållet: buildern kan inte öppna det här
  fönstret, precis som `about_dialog` och `free_window`.
  **FLIKAR sedan 2026-10-08 (post 19)** — grupperna ligger i en
  `TabControl` (`flikar_`), EN FLIK PER TEMA, i stället för i en rad
  kolumner som växte åt höger med varje komponentspår (sju kolumner, 2224
  px — bredare än en skärm). En sida rymmer fyra kolumner (x = 12 / 328 /
  644 / 960, 304 breda) och den högsta gruppen (g3, 484). Flikkontrollen är
  SPÄND mellan fönstrets överkant och stängknappens överkant (`anchorBottom`
  mot knappen, som därför skapas FÖRST — ett syskonankare pekar bara bakåt),
  och varje sida har `showScrollbars(true)` (`sida()`), så en för låg sida
  RULLAR i stället för att klippas. Måtten `setContentSize(1302, 610)` +
  `setMinimumSize(1302, 690)` är räknade i ctorn ur flikraden, knappen och
  g3 (högst i Glaze). Flikarna och deras grupper:
    **Grundläggande** — 1 Label/Button/Link/Separator · 2 Checkbox/Radio ·
      **31 Kryssruta med tre lägen** (FAS1_B_WIDGETS, under g2: en ruta med
      `setThreeState` som användaren klickar runt av → på → mellanläge, en
      förälder "Alla lager" vars mellanläge är APPENS — två indragna lager
      (kartans namn) räknar fram det i `onChange`, och förälderns klick kryssar
      alla eller inga — och en inaktiverad i mellanläget; 194 hög, RÄKNAD) ·
      4 Slider/Progress (bestämt, av, obestämt) · 5 Picture/Scrollbar (tre
      bilder — normal, av och UR PIXLAR: en `glaze::Bitmap` på 64 × 64 som
      `ringar()` räknar fram i appens egna färger, i en ruta på 32 × 32 så den
      är skarp på skala 2; texten bredvid är sträckt och radbruten — och två
      VÅGRÄTA listor — `Scrollbar` står lodrätt som default, och raden
      `setOrientation(Horizontal)` måste stå).
    **Text och listor** — 3 TextInput (normal, av, skrivskyddad)/Dropdown/
      **ListBox** (normal, kryss, av) · 11 SpinBox (heltal, två decimaler
      med kvartssteg och ett inaktiverat, UTAN mått och syskonankrade;
      decimaltecknet följer SPRÅKET, så ett byte i startfönstret skriver om
      de vilande talfälten — `Widget::languageChanged`) · 7 PropertyGrid ·
      **21 FilePicker** (post 25, fjärde kolumnen: öppna med appens "Bilder"
      och ramverkets "Alla filer", spara med ".txt", mapp och ett
      inaktiverat — etiketterna är RAMVERKETS dialogtitlar, och "…" öppnar
      filväljardialogen (post 18) i fältets läge; `filvalAndrat` skriver
      sökvägen ur sender i statusraden) · **29 Lösenord, ångra och
      radbrytning** (TEXTFALTEN, fjärde kolumnen under 21: y = 286, 210 hög —
      RÄKNAD i Slates radhöjd, inte renderad, se doc/os/windows/TESTS.md — så
      gruppen slutar med g3. Ett lösenordsfält sträckt fram till kryssrutan
      "Visa lösenordet" (`onChange` läser rutan ur SENDER), en stående
      `Splitter` vars första ruta (fast 210, KLIPPER) bär en STRÄCKT
      radbruten etikett och en dämpad rad ankrad under den — dra skiktet så
      bryts texten om och raden följer med, REQ 12:s provyta — och en
      sträckt radbruten fotrad om Ctrl+Z/Ctrl+Y; lösenordet "p4ssw0rd" är
      exempeldata. Fältet bär PLATSHÅLLAREN 1830 och MAXLÄNGDEN 16 —
      REQ2_DIALOGER; töm det så syns hinten, och en inklistring kapas) ·
      **36 Etiketter och knappar** (REQ3_TRAD_LISTOR_FALT — nr 35 var redan
      REQ3_LAYOUT_FONSTERs — UNDER de tre kolumnerna, y = 508, 936 bred, så
      sidan rullar dit: "Namn" med `setBuddy` mot ett fält, en etikett med
      `setElide`, "Ta bort" i appens röda `setColor`, en bildknapp med
      `ringar(64)` i `setIconSize(32)`, radioknappar i sina färger, och en
      listvy med 48 px-miniatyrer och `setFillColumn(0)`). Sedan samma pass
      har g21:s första fält `setDialogTitle` (1906) och g24:s anteckning
      `setAcceptsTab(true)` (1471 säger det) · **37 Markdown** (REQ7_MARKDOWN —
      under grupp 36, y = 720, STRÄCKT mot sidans högerkant: en rullande
      `MarkdownView` med ram vars dokument är ETT id (1972 — rubriker, listor,
      kod, citat, tabell och ankarlänkar som följer språkets rubriker),
      `setImage("ikon", …)` och `![](toolbar/gear)` som asset; `onLinkClick`
      visar målet i statusraden, och `demo:data` byter till fliken Tabeller och
      träd med `handled` — webbadressen och `#kod` går till förvalet. Till höger
      en AUTO-hög vy sträckt mellan dokumentet och kanten, och en dämpad rad
      syskonankrad under den som följer när fönstret görs smalare).
    **Paneler och remsor** — 6 Panel med `showScrollbars(true)` OCH barn
      som svämmar över i BÅDA axlarna (utan överflöd ritas remsorna aldrig)
      + en inaktiverad panel · 8 Bar/Spacer/CaptionButton/TopBar/StatusBar
      (`statusD` NOLLAR sin `dock` — ctorn sätter `Bottom`, och dockad hade
      den skurit 24 px ur alla syskon i gruppen — och bär tre FÄLT,
      FAS1_B_WIDGETS: texten fyller, "100 %" är AUTO och "UTF-8" fast; raden är
      inaktiverad och dämpar dem själv) · 9 ToolBar/ToolButton (två
      remsor med samma innehåll, aktiv och inaktiverad, byggda i en LAMBDA
      vars `verktyg` tar ett `ImageSource` — en asset eller medlemmen
      `pixelIkon_`, en Bitmap på 32 × 32 som båda remsorna delar och som
      tonas i den inaktiverade;
      remsan dockar Top som default och nollas här; `verktygKlickat` skriver
      verktygets `name` och läget, som redan är omslaget när `onClick` når
      den — och en TREDJE remsa med RULLGARDINERNA, DOCKNING E4a: "Ny ▾" med
      menyn bakom hela knappen och den DELADE "Ångra ▾", vars knapp ångrar
      det senaste och vars meny `byggAngraMeny` bygger om ur `angraHistorik_`
      i `onDropDown`, FÖRE öppningen — samma två inaktiverade bredvid; en tom
      historik fylls på igen, så knappen behåller sin pil) · 10 GroupBox (två
      ramar UTAN MÅTT som autosizar ur sina barn,
      en aktiv och en inaktiverad vars barn släcks för sig).
    **Tabeller och träd** — 13 TreeView (det aktiva byggt med `addNode`s
      SVAR, det inaktiverade med MASSFORMEN `setNodes`; omordning med
      `allowDrag` + `allowDrop`, och de två FILERNA är löv för släppet —
      `setAllowDropInto(…, false)`, post 49 — så en nod dras före/efter dem
      men aldrig in i dem. Det aktiva har FLERVAL och KRYSS och Brev.txt en
      statusfärg (`setNodeColor`); det inaktiverade visar samma med
      aggregatets kryss- och färgfält, designerat för filerna. Gruppen är
      268 hög för en fot på tre rader, sträckt och radbruten.
      `tradAndrat` skriver valet — antalet när flera är valda —
      `tradKryssad` krysset ur `changedPath()`, `tradFalld` utfällningen och
      `tradAktiverad` den AKTUELLA noden. APPENS GODKÄNNANDE (REQ2_TRAD_LISTOR):
      `tradOver` nekar en FIL överst — regeln i `onDragOver`, som fyras också
      vid inträdet; filen känns igen på `allowDropInto` — och `tradSlapp`, som
      frågas FÖRE trädets flytt, sorterar själv in noderna alfabetiskt i Musik
      (känd på översättnings-id:t; utfälld FÖRE `moveNodes`, `handled`) och
      skriver annars "Flyttade noder: N"; tooltipen 1358 säger regeln.
      REQ3_TRAD_LISTOR_FALT: Musik bär tolv utfällda spår ("01.flac" … — data,
      löv för släppet), så trädet rullar och ett drag nära kanten
      AUTOSKROLLAR; `tradMeny` öppnar snabbmenyn "Ta bort" (tar bort valet
      bakifrån) mot `contextPath()`, och listvyn i 14 har `setFillColumn(0)`
      och `listaMeny` mot `contextRow()`) · 14
      ListView (styckeformen ur en lokal tabell med `setSortKey` på bytes —
      storleken VISAS som text men SORTERAS på talet — flerval, sorterad på
      namn, Budget.xlsx i felfärg med `setRowColor`, KRYSS med Semester.png
      kryssad och tooltipen 1842; den inaktiverade med MASSFORMEN, fallande på
      storleken, en rad med färgfältet och ett kryss; `listaAktiverad` läser
      raden ur `currentRow()`, `listaKryssad` krysset ur `changedRow()` med
      trädets texter 1600/1601) · 18 DataGrid (värd
      och port i fritext, protokollet ur en VALLISTA och id:t skrivskyddat;
      FTP-raden i felfärg; fem rader i
      en yta för fyra så den rullar; en inaktiverad med massformen.
      `dataAndrad` skriver den sparade cellen: VILKEN ur `CellChangeArgs`,
      värdet ur sender) · **32 ListBox med flerval** (FAS1_B_WIDGETS, under
      g13: kartans fyra lager med två valda, listvyns tangenter och solo vid
      släppet) och **33 DataGrid med radval** (under g18: `FullRow` med
      flerval, två valda; `onSelectionChange` och listboxens `onChange` delar
      `valdaRaderAndrade`, som skriver antalet ur `selectedRows()`; båda
      slutar vid g3:s underkant, RÄKNADE) · **26 Kolumnrubriker** (post 52, fjärde kolumnen:
      LINBERÄKNINGEN — rubriker på flera rader med `\n`,
      AUTO-höjd, centrerade och bottenjusterade, talen högerjusterade och
      lastfallet centrerat; under den en redigerbar datagrid med två
      rubrikrader i överkant vars rubriker följer cellerna — vänster, mitten,
      höger — och en lång rubrik som kortas med "…". Antalet är en vallista,
      så editorerna visar kolumnens justering. Kolumnbredderna är MÄTTA mot
      det längsta ordet på alla fyra språken, och standardens prefix följer
      språket: SS · NEK · SFS · EN).
    **Layout** — 15 Splitter (stående med första rutan fast på 110 och
      sträckt, liggande med andra rutan fast och minimum 20, och
      inaktiverad; rutorna är ramade Panels, och läget skrivs först vid
      släppet) · 17 DockHost (verktygspanel med textfält, utdatapanel,
      dokument i mitten och knapparna Visa alla/Spara/Återställ —
      `layout()`/`setLayout`) · **20 TabControl** (STÄNGBARA flikar med
      ägda sidor — `flikStangs` tar bort sidan med `unadopt` i handlern, för
      flikkontrollen gör det aldrig själv; "Ny flik" lägger till en sida i
      drift med `addPage` (i `nySida`, FAS1_B_WIDGETS); en inaktiverad med översatta rubriker. Den ligger i galleriets
      egen flikkontroll, så Ctrl+Tab inifrån en av dess sidor tas av den
      INRE — fokus närmast vinner).
    **Väljare** (post 20, sjunde fliken) — 22 ColorPicker (en textfärg vars
      provtext under följer valet LIVE — `prov->setColor(…)` i `onChange` —
      en STRÄCKT med färgen i kodens form `gfx::Color{…}` och en
      inaktiverad; `fargAndrad` skriver `colorText()` i statusraden; sedan
      FAS1_B_WIDGETS en rad MED ALFA — ramverkets etikett "Alfa", `#3366FF80`,
      rutmönstret — så gruppen är 238 hög, RÄKNAD). ·
      23 DatePicker (ett datum som standard är DAGENS, en LEVERANS med
      gränserna i dag och 30 dagar fram — räknade med `std::chrono` i koden,
      så dagarna utanför syns släckta — och en inaktiverad; `datumAndrat`
      skriver `dateText()` i statusraden, det EFFEKTIVA datumet och inte
      fältets text — med klockslaget `timeText()` efter; sedan FAS1_B_WIDGETS
      en rad "Klockslag" med `setShowTime`, 240 hög, RÄKNAD) · **34
      Färgdialog och inmatning** (FAS1_B_WIDGETS, tredje kolumnen: två
      knappar under varandra — finskans texter ryms inte bredvid — som öppnar
      `glaze::ColorDialog` (en MEDLEM, `fargDialog_`, med alfan, så den öppnar
      med förra valet; svaret som `#RRGGBBAA` ur appens `hexText`) och
      `glaze::inputBox` (lagrets namn i `lagerNamn_` som förval; `nullopt`
      skriver "Dialogen avbröts")). Fliken är tänkt för komponenter som
      väljer ett värde i en popup — och dialogerna som gör det modalt.
    **Interaktion** — 12 Dra och släpp (en knappkälla med "Rapport.pdf" i
      lasten och ikonen i dragbilden, ett mål som svarar `Copy` i
      `onDragEnter`, ett INAKTIVERAT mål som draget routas förbi så att
      förbudsmärket syns; ramen, märket och dragbilden ritar RAMVERKET) ·
      16 Tooltip (en knapp med ikon, en inaktiverad knapp — tooltipen visas
      ändå, den säger varför — en verktygsknapp med bara ikon, och en ram
      vars tooltip ärvs av en etikett men inte av ett talfält med egen) ·
      30 Inmatning (REQ2_INMATNING, x = 644 bredvid g16: ett `TextArea` där
      `onKeyDown` skickar texten till statusraden vid Ctrl/Cmd+Enter UTAN Alt
      — AltGr är Ctrl+Alt på Windows — och upprepningen sväljs utan att skicka;
      under det en rityta utan gester, spänd ner till gruppens underkant, vars
      handlers TAR mitten- och högertrycket men lämnar vänster (fokus åt
      mellanslaget): högerdraget ritar en linjal i `onPaintOverlay`
      (`accent`, `invalidateOverlay`) och släppet skriver längden, medan
      dubbelklicken, `onMouseCancel` och mellanslagets tryck, släpp och
      avbrutna släpp (`cancelled`) skrivs i statusraden) ·
      35 Storlek och tangenter (REQ3_LAYOUT_FONSTER, x 12, y 114 under g12:
      ett fält vars `onKeyDown` skriver tangentens namn och `KeyArgs::text`,
      och en `Splitter` med en rityta vars `onResize` skriver ytan i
      statusraden; högertrycket tas i `onMouseDown` och släppet öppnar en
      snabbmeny vid `toWindow(e.local)`).
    **Rityta** (CANVAS_2D etapp 12, åttonde fliken — byggd i
      `gallery_canvas.cpp`, se nedan) — 25 · Canvas tre gånger: kartan
      (kolumn 1–2), grafen med en inaktiverad kopia (kolumn 3–4) och
      linjalprovet över HELA bredden under dem (y = 336, underkanten 496 är
      g3:s, så fönstrets mått gäller oförändrade). Linjalen fick hela bredden
      med flit: 100 mm är upp till ~740 logiska px på en tät skärm vid 100 %
      skala och ryms inte i en halv sida.
    **3D** (CANVAS_3D etapp 14, nionde fliken — byggd i `gallery_3d.cpp`,
      se nedan) — 27 · Canvas — scen i 3D över HELA sidan (1252 × 484, g3:s
      höjd, så fönstrets mått gäller oförändrade); under ytan tre rader:
      foten, förstapersonens kryssruta med vad klicket och tangenterna gör, och
      INMATNINGEN (REQ9_INMATNING, `inmatningAndrad`): lägesvalet (automatiskt,
      karta, orbit, förstaperson och demons eget "mäta", där vänsterdraget är
      `None`) och kryssrutorna "Vänd hjulet"/"Vänd styrplattan" (källans
      vändning) och "Vänd zoomen" (handlingens, för alla tre enheterna) — på
      ritytan, aldrig appens tabell. Listan "Med kryssrutor" i grupp 3 har ett
      vänt mushjul (`setWheelInvert`) och en tooltip som säger det.
    **3D-verktyg** (porteringsprovningen, tionde fliken — byggd i
      `gallery_3d_tools.cpp`, se nedan) — 28 · Canvas — 3D-verktyg: ritytan
      sträckt till en kolumn om 272 px till höger (kantlägets kryssruta,
      "Miniatyr" och en rityta på 256 × 256 som visar bilden), samma 484.
  `flikBytt` betjänar BÅDE galleriets flikar och grupp 20:s och skriver den
  nya fliken i statusraden — och för galleriets egna flikar också i
  statusradens ENDA FÄLT (FAS1_B_WIDGETS: `addPanel` före högerluften, bland
  appens egna barn; `setPanelText(0, sidans token)`, så det följer språket). **`enabled` kaskadar inte ner i trädet** — de
  inaktiverade containrarna släcker sina barn uttryckligen, här som loopar.
  **GRUPPERNAS HÖJDER ÄR MÄTTA** mot innehållet i alla tre UI (2026-10-08):
  g3, g6 och g7 var lägre än sitt innehåll sedan kolumnerna fylldes — den
  inaktiverade listan i g3 syntes inte alls — och har fått höjden.
  **24 · TextArea** (post 23) ligger på "Text och listor" och SPÄNNER kolumn
  två och tre (x = 328, 620 bred) under talfältet och egenskapslistan (y =
  236 till g3:s underkant 496): fyra fält i två kolumner — brytning (med fler
  rader än det rymmer, så remsan och hjulet syns), utan brytning (den vågräta
  remsan), skrivskyddat och inaktiverat — alla med sin AUTO-höjd
  (`textAreaRows` rader ur stilen) och staplade med syskonankare. En stapel av
  fyra i EN kolumn rymdes inte i Glaze och Slate utan spikade höjder, och de
  höll inte vid 1,25. `anteckningAndrad` skriver antalet LOGISKA rader ur
  sender i statusraden. Brytningsfältet (`ant`) bär PLATSHÅLLAREN 1831
  (REQ2_DIALOGER) — töm det så syns den, radbruten mot fältets bredd.
  **EN NY KOMPONENT I GALLERIET**: lägg
  gruppen på den flik den hör till med `grupp(sida, rubrik, namn, x, y, w,
  h)` — x i en av de fyra kolumnerna, y i sidans koordinater — eller ge
  komponenten en EGEN flik: `glaze::TabPage* s = sida(demo::str::FlikX);` i
  ctorn efter de befintliga (flikordningen är adopt-ordningen). Fliktitelns
  id läggs i `demo.langtxt` (flikarnas block 1360–1379, alla fyra språken)
  och filen packas om. Ingen annan flik behöver räknas om när en sida växer:
  blir den högre än fönstret rullar den. Blir den nya gruppen HÖGRE än g3
  ska ctorns måtträkning och minimimåttet följa med.
  **Galleriets gamla `.glazeui`-triplett lever kvar som
  `tests/fixtures/gallery.*`** — `roundtrip_gate`s BREDASTE facit (alla
  widgettyper, alla fältformer) och `pixel_gate`s korpus. Fixturen förblir
  PLATT, en kolumn per komponentspår och inga flikar runt grupperna:
  pixel_gate kräver att varje widget SYNS, och en dold fliksida syns inte.
  Flikkontrollen själv står där som grupp 20 (x = 2540), filväljarfältet
  som grupp 21 (x = 2856, post 25), färgväljaren som grupp 22 (x = 3172,
  post 20 — båda `setColor`-formerna), datumväljaren som grupp 23
  (x = 3488, post 26 — tre heltal per sättare, gränserna FÖRE ett datum
  utanför, så rutan visar det klampade) och det flerradiga fältet som grupp
  24 (x = 3804, post 23 — texter med `\n` och citattecken som ska
  round-trippa byte-exakt, ett fält som rullar lodrätt, ett i sidled, ett
  skrivskyddat och ett inaktiverat). Ritytan står där som grupp 25 (x = 3488,
  y = 184 under datumväljaren, CANVAS_2D etapp 7/10 — former i två lager,
  en vriden och tiltad yta och en inaktiverad, allt i `.glazeui`-form), och
  kolumnrubrikerna som grupp 26 (x = 2540, y = 324 under flikarna, post 52
  — `setHeaderLines`/`setHeaderVAlign` och kolumnens fjärde fält, en
  sorterad listvy med AUTO-höjd och en inaktiverad datagrid på två rader).
  Fixturen följer inte demogalleriet rad för rad — den är en designer-väg,
  galleriet en kodväg.
- **`gallery_canvas.cpp`** — galleriets flik **"Rityta"** (CANVAS_2D etapp
  12): `WidgetGallery::ritytaFlik` och ritytans handlers, i en EGEN fil för
  att den är större än en grupp och resten av galleriet inte behöver något
  av den (medlemmarna står i `widget_gallery.h`). Fyra delar:
  **Kartan** — en scen med kamera i SWEREF 99 TM (meter, `setYUp`), skalan
  `setMapScale(4000)` ur skärmens mm (väntar till första ritningen — en tät
  skärm visar mindre av kartan än en gles, en centimeter är 40 m på båda).
  Fyra lager i kollektionsvokabulären med ÖVERSATTA namn: plattor (6 × 4
  `Bitmap` à 128 px RÄKNADE ur ett deterministiskt värdebrus i ctorn — demon
  bär inga bilder; de BEHÅLLER sina pixlar, för galleriet stängs och öppnas
  igen ur samma objekt och ett nytt fönster kan inte ladda upp en släppt
  bild, REQ3_RITYTAN B10). REQ3_RITYTAN visar också TEXT MED TAK (anteckningen
  nordost om Ekparken, id 1935, liggande i sin ruta och kortad med "…"),
  GOLV OCH TAK ("Kyrkbacken" i världsenheter mellan 2 och 6 mm) och
  SYNLIGHET EFTER ZOOM (husens namn från zoom 2). Ytor (`DrawOrder::ByKind` — alla fyllningar, sedan alla
  kanter; i indexordning hade varje hus med kontur kostat två rit-anrop),
  vägar (två konturer per väg i mm, alla kanter före alla fyllningar, och de
  tre skarvarna Round/Miter/Bevel; stigen streckad i mm med runda ändar) och
  etiketter (gloria, `Alignment::Map` med vinkeln längs vägen, prioriteter
  så att utzoomning DÖLJER de mindre viktiga — `setLabelFade(true)` tonar;
  markörerna: hållplatserna som punktmoln, en kartnål ur en Bitmap med
  ankaret Bottom och infopunkten ur `glaze::img`). Sjön har ett HÅL och en
  radiell gradient, parken en linjär och en streckad kant. **Parkens
  ÖVERSIKT** (CANVAS_TEXT_OCH_BILD) är en bild på FYRA FRIA HÖRN — ett
  räknat rutmönster (`rutor()`, 64 px) snett över parken med opaciteten 0,6,
  FÖRST i vägarnas lager så den täcker parken men inte vägarna; rutorna
  förblir raka också längs fyrhörningens diagonal, och det är beviset för
  den projektiva avbildningen (Eriks ögontest). **Adressen "Storgatan 12"**
  är en etikett i DELAR (`TextSpan`): numret fetstilt i vägkantens färg, i
  samma rit-anrop, gloria och kollision som resten av etiketten — båda
  namnen är kartans data och översätts inte. Kartans egennamn
  är DATA och översätts inte; kategorierna ("Badplats") är id i formlistan
  och byter språk. Alla fyra gesterna är på, också STYRPLATTANS (TODO 53 —
  två fingrar panorerar, nyper och vrider). **VRIDSNÄPPET** väljs i en
  vallista OVANPÅ kartans övre högra hörn (Av/Norr/90°/45° →
  `setRotationSnap(0/360/90/45)`, förval 90° så det syns direkt), lagd EFTER
  kartan så den ritas och träffas överst; raderna är "Vridsnäpp: " + valet,
  så ingen etikett behöver stå ovanpå kartan. **SKALSTOCKEN** nere till
  vänster (`onPaintOverlay`, Eriks önskan) står fast på skärmen och MÄTER
  DÄR DEN STÅR: under tilt är skalan större närmare kameran, så mittens
  `mapScale()` hade varit fel vid underkanten — `unitsPerPixelAt` på
  stockens rad ger meter per logisk px, och längs en bildrad är skalan
  lika också under vridning och tilt. Längden är talet i 1-2-5-följden
  närmast 31,6 mm (`narmast125`), bandet växlande fyllt och tomt (fem
  delar för 1 och 5, fyra för 2), etiketterna i mm, och bredvid står
  "1:N"/"≈1:N" mätt på samma plats och varifrån skärmens mm kommer — på en
  OGENOMSKINLIG platta i temats färger (en halvgenomskinlig lät kartans
  glorior lysa igenom i mörkt läge). **Klicket**: `onMouseDown` sparar trycket och `onMouseUp` kör
  `hitTest` om släppet ligger inom `style().dragThreshold` (annars var det
  en panorering) — statusraden får den översta träffen ur `kartNamn_`
  (namn per lager och form, fyllt av `lagg`) och antalet under den.
  **LAGERLISTAN** (REQ2_RITYTAN) uppe till vänster: en vallista med lagren
  ÖVERST FÖRST ("Lager: Vägar" — rad r är platsen n − 1 − r) och ▲/▼ med
  Microsofts termer som tooltips; `kartaLagerSteg` gör `moveLayer(p, p + 2)`
  respektive `moveLayer(p, p − 1)` (platsen räknas FÖRE flytten), byter två
  grannar i `kartOrdning_` och skriver ordningen i statusraden. Träffens
  lager är en PLATS, så klicket slår upp namnet genom `kartOrdning_` (plats →
  ursprungligt lager) — demons bild av fallgropen "positionen är ingen
  identitet". Listan fylls inte om vid språkbyte (vridsnäppets prejudikat).
  **Grafen** — `setYUp`, `setGridSize(1)`, panorering och hjul men ingen
  vridning; axlarnas tal ritas i `onPaint` med TEMATS dämpade textfärg och
  ett steg i rutnätets 1-2-5-följd med minst fem rutnätslinjer emellan (då
  står talen alltid på en linje). **Gummibandet**: Skift+tryck TAR trycket
  (`handled`) så ingen panorering startar, draget ritar rutan i
  overlay-lagret (`invalidateOverlay` — ingen omritning av lagren) och
  släppet kör `zoomToRect`; ett VÄNSTER dubbelklick visar allt igen (sedan
  REQ2_INMATNING når `Right` och mitten-/högerdubbelklicket ritytans
  handlers, så `onDoubleClick` och `grafNed` prövar `e.button`); koordinaterna
  under pekaren står i overlay-lagrets hörn och försvinner med `onHover`.
  **Den inaktiverade** är samma former med `disable()`.
  **Linjalprovet** — en rityta UTAN kamera (lokala logiska px) som i
  `onPaint` ritar 100 mm som `100 × px/mm / uiScale` logiska px, med ett
  streck per mm och skärmens id, källa och tal överst. Drag (eller tryck) i
  ytan och stegen - / + (ASCII-minus — valt när UI-fonten saknade
  glyfreserv och Noto Sans, fontconfigs sans-serif på Debian 13, inte har
  U+2212; reserven finns sedan CANVAS_TEXT_OCH_BILD, och bytet till U+2212
  är en egen liten ändring utanför den planen; en FYSISK
  pixel på 100 mm per steg, 0,01 px/mm) ändrar
  talet; Spara kallar `App::setDisplayCalibration(id, tal)` — varje fönster
  mäts om, så kartans mm och skala byter direkt — och Återställ sätter 0.
  Talet hör till skärmen det provades på (`kandId_`): står fönstret på en
  annan skärm läser `kandidat()` den skärmens eget tal, utan någon
  skärmhändelse. Spannet 50–800 dpi är platform.h:s. DEMON SPARAR INGET —
  en riktig app skriver talet i sina inställningar och sätter det vid
  start, och texten bredvid knapparna säger det.
  **FÄRGERNA**: kartans och kurvornas är DATA (shape.h — de följer inte
  temat); det som är GRÄNSSNITT på ytorna (skalstocken, axlarnas tal,
  gummibandet, grafens koordinater, linjalen) läser
  `theme().resolve(themeMode())`, och texten där har stilens `uiFontSize`
  eller `fontsize::kUiCompact` — utom skalstockens, som är i mm.
  **Visuellt prov**: galleriet renderas inte av glaze-render (handkodat) —
  det provas med ett scratch-program som öppnar galleriet DOLT, väljer
  fliken och läser `captureFrame`. Overlay-lagret ligger i backbuffern och
  syns inte där: spegla `onPaintOverlay` in i `onPaint` med `screenSpace()`
  för bilden. **3D-flikarna kräver PUMPEN**: rutnätet i "3D-verktyg" byggs i
  ritytans första tick, och ticken postas till pump-svansen — kör stegen i
  fönstrets timer (`glaze::app().post` före `run()`, en subklass med
  `event()` och `handleEvent` för klick och tangenter, canvas3d_gate:s
  mönster), inte `renderFrame` i en loop.
- **`gallery_3d.cpp`** — galleriets flik **"3D"** (CANVAS_3D etapp 14):
  `WidgetGallery::tredeFlik` och dess handlers, en rityta med en `Camera3D`
  i orbit kring en liten kraftledning i SWEREF (samma mitt som kartan —
  appen anger världskoordinater, ritytan ritar relativt varje forms origo).
  Varje del visar ett verktyg: RUTNÄTET är `line3` var tionde meter (APPENS
  — ramverket har med flit inget rutnät), STOLPARNA primitiver (cylinder,
  låda, isolatorer — delade enhetsmeshar), LINORNA `tube` längs en parabel
  (diametern förstorad så den syns på avstånd), NÄTSTATIONEN en EGEN mesh ur
  lägen, normaler och färger som en `Placement` vrider och skalar, SKOGEN en
  kon i två detaljnivåer som `instances`, KULLEN en laserskanning vars skalär
  är höjden över mark och vars `ColorRamp` färgar den (nedan), och
  ETIKETTERNA `text3` — i ett
  lager satt till `LayerPlacement::Ground`, så de ritas överst och skyms
  inte av 3D:n. Ljuset är `setLighting` (exponenten 1,5 och ett svagt
  rimljus). Gesterna är påslagna — med en `Camera3D` är de orbitens (spår
  B, etapp 11). FÄRGERNA är scenens data och följer inte temat; de är valda
  så att de syns mot både den ljusa och den mörka ytan bakom (provat
  renderat i alla tre UI, ljust och mörkt, 2026-10-11), och rutnätet är
  halvgenomskinligt grått av samma skäl. **HIMLEN** (etapp 8) är APPENS
  — ett eget `Program` (`himmelProgram`, HLSL, GLSL och MSL i filen) som
  `screenPass` först i bildrutan, med horisontens, zenitens och disets
  färger som GlazeUser och horisontens färg som `fallback` (den syns där
  programmet inte kan köras, och i previewn, som aldrig kör användarkod).
  **TERRÄNGEN** (etapp 9) i sydost: 3 × 3 rutor av egna meshar i ett
  Terrain-lager (en låg kulle, och dammen — 2 × 2 celler utan trianglar),
  vägarna i ett Ground-lager DRAPERADE på dem (skogsvägen i en båge över
  kullen, en streckad stig i mm), en stående etikett vars ankare står på
  kullen och en LIGGANDE (`Alignment::Ground`) längs stigen.
  **PORTERINGSPROVNINGEN (planens §6) i den här fliken:** LASERSKANNINGEN är
  en mark och 24 träd vars skalär är HÖJDEN ÖVER MARK (`kulleMark` — samma
  funktion som kullens terräng, ett 25 × 25-nät i Terrain-lagret under
  molnet; aldrig under noll) färgad med en höjdramp
  (`hojdRamp`, 0–40 m, CloudCompare-lik) — marken marinblå också på
  kullens topp. Ett KLICK (släppet inom `dragThreshold`, kartans regel)
  plockar närmaste punkt inom 8 px bland punkterna inom 10 m längs
  synlinjen (`hitTest` med `HitOptions`, lager 5, `HitKind::Point`) — en
  ring (`marker3` i ett Ground-lager, `kMarkering`) och statusraden får
  punktens plats i indatan och dess höjd (`punktHag_`, med språkets
  decimaltecken — `tal()` i demo_text.h); en miss tömmer ringen. På 96 m
  avstånd är inget inom 10 m: gå dit i förstapersonen eller dolla in.
  FÖRSTAPERSONEN är en kryssruta UNDER ytan (på himlen hade en ljus text
  försvunnit i mörkt läge): på ställer ögat 2 m över marken där orbitens
  mål stod (`Camera3D::lookFrom`, synfältet 45°) och slår av de fyra
  gesterna, av ställer översikten kring där ögat stod och slår på dem igen,
  och båda ger ritytan fokus (`focus()`). `onKeyDown` TAR W/A/S/D, pilarna
  och Q/E bara i förstapersonen — ritytan tickar så länge en tagen tangent
  hålls (canvas.h, regel 1) — och `onTick` går 5 m/s längs blickens
  kompassriktning, halva farten i sidled, vrider 67,5°/s och följer marken
  med `heightAt` (planet z = 0 utanför terrängen och i dammen). Ett
  vänsterdrag är musblicken (0,1°/px, ±85°).
- **`gallery_3d_tools.cpp`** — galleriets flik **"3D-verktyg"**
  (PORTERINGSPROVNINGEN, CANVAS_3D etapp 14, planens §6):
  `WidgetGallery::tredeVerktygFlik` och dess handlers — en 3D-editors
  förmågor byggda med ritytans verktyg kring en STOLPTOPP på arbetsplanet z = 0 (meter
  kring origo): ETT svetsat nät ur prismor och en låda, utan normaler (den
  platta normalen ur derivatorna), färg per läge. Fyra prov, och allt som är
  modulens står här: **RUTNÄTET** (`rutnat`) — `line3` i px-bredd i lager 0
  utan djupskrivning; den fina nivån är den minsta tiopotens vars linjer står
  minst 10 logiska px isär där strålen genom ytans mitt (orbitens mål står
  alltid där — `project(target)`) når planet (`worldPerPixel`), tonad in
  mellan 10 och 100 px, den grova tio gånger den med axlarna röd (x) och grön
  (y); varje nivå klipps mot `visibleRegion` till 3 respektive 30 gånger
  avståndet, VIDGAD 1,5 gånger (ritytan säger inte till när ytan växer) och
  med ett tak på 2000 linjer per riktning. Byggs om i `onCameraChange`
  (användarens rörelser) och i `onTick` — efter första ritningen
  (`setTicking(true)` i bygget) och medan kuben glider — och tickningen slås
  av när `isEasing()` är falsk. Kamerans `farPlane` är satt (1000 m):
  klipplanen ur scenens låda hade räknat rutnätet självt, som då växt för
  varje ombygge. **KUBEN** — `box` i lager 4 och `text3` i lager 5, båda
  bara i del-vyn uppe till höger (`View`, 120 × 120, ortografisk,
  `followRotation`); etiketterna står på MARKEN (överst) och bara på de
  sidor som vänder sig mot kameran (`kubEtiketter`, byggs om med rutnätet) —
  i ett World-lager skar grannsidan av en upprätt etikett. Ett vänstertryck
  frågar `hitTest`, som routas till den översta vyn; första träffen i en
  del-vy (en sida eller en etikett) ger regionen (`region`, 0,6:
  sida, kant eller hörn — 26) och `standardvy` dess azimut och elevation
  (ögat på regionens sida, rakt uppifrån och underifrån med norr uppåt),
  `easeTo` 250 ms, och trycket TAS så ingen orbit startar. **KANTLÄGET** —
  `kanter` är kantextraktionen (öppna kanter och veck över 30°),
  meshen har djupbias 1 + slope 1 bakåt (polygon offset)
  och kanterna ligger två gånger: lager 2 `LessEqual` mörka 1,5 px, lager 3
  `Greater` ljusa och streckade (de dolda), båda utan djupskrivning;
  kryssrutan döljer de två lagren. **MINIATYREN** — "Miniatyr" ritar
  `renderToBitmap(256, 256, kamera)` med en miniatyrkamera (lådans mitt,
  45°, 30°, 2,5 radier) och rutnätslagret dolt under bilden, och visar den
  som `Shape::image` på en rityta UTAN kamera (skrivet när `Picture` bara tog
  en asset — sedan TRAD_LISTOR_BILDER tar den också en `Bitmap`);
  statusraden får måtten. LJUSET kommer från västsydväst bakom kameran med
  högre omgivning än förvalet, så de tre sidor man ser från start får var
  sin ton.
- **`second_window.h/.glazeui/.cpp`** — andra fönstret: multi-fönster-
  referensen. Knappar: `om` → **modal dialog** (WinForms-mönstret:
  `AboutDialog dlg;` som LOKAL variabel, `dlg.run(*this)`, resultatet i
  `tips.setText(...)`), `fritt` → transparent fönster, `stang` → `close()`
  (och fönstrets AVBRYTKNAPP — `setCancelButton(stang);` i `.glazeui`, så
  Escape stänger: den genererade vägen för REQ2_DIALOGER),
  **`medd` → `glaze::messageBox`** (MESSAGEBOX, 2026-08-19): EN rad i
  stället för en dialogklass. Fönstret är en RIGG, inte ett enda exempel —
  `typDd` (fem typer) × `knappDd` (sex uppsättningar) × `egenBild` gör alla
  trettio kombinationerna provbara, och kryssrutan byter typens badge mot
  `glaze::img("toolbar", "gear")` för att visa att egen bild vinner. Svaret
  skrivs i `tips`. Indexen i `meddClicked` följer dropdownens `setItems`-
  ordning EXAKT — listorna står i `.glazeui` och ordningen ÄR kontraktet
  mellan dem. `None`-grenen finns fast den inte kan nås: kan den nås har
  kontraktet brutits, och då ska demon säga det i stället för att tiga.
  **Previewn visar tomma dropdowns** — `setItems`/`setSelectedIndex` körs i
  `initializeComponent`, som varken glaze-render eller buildern kör (den
  befintliga 💡-raden om tillstånd i `.glazeui`). I den körda appen är de
  fyllda.
- **`about_dialog.h/.cpp`** — Om-dialogen, HELT handskriven C++ (ingen
  builder-region): kvittot på fönstermodellen — en dialog är ett vanligt
  AppWindow med `setModal(true)` i ctorn, och knapparna BÄR SVARET
  (`setDialogResult(OK/Cancel)`, REQ2_DIALOGER) som fönstrets standard- och
  avbrytknapp (`setAcceptButton`/`setCancelButton`): Enter = OK, Escape =
  Avbryt, och accentytan följer fokus mellan knapparna.
- **`free_window.h/.cpp`** — transparent fönster (VISION-exemplet): rund
  platta + text som sticker ut, `setTransparent(true)` före show; klick
  utanför formen går igenom till fönstret under (Windows). Stäng-knappen
  är ankarlös (= centrerad).
- **`tray_window.h/.cpp`** (SYSTEMFÄLTET, 2026-10-12) — **`demo --tray`**, en
  app som BOR i systemfältet; HANDKODAT som `about_dialog`/`free_window` —
  kodvägen (ikonen kan stå i `.glazeui` sedan FONSTER_OCH_TIMER, se
  `tests/fixtures/components.*`). `TrayWindow` bär
  medlemmen `glaze::TrayIcon tray_` och visar varje del: ingen knapp i
  aktivitetsfältet (`setShowInTaskbar(!isSupported())` — på macOS tar `main`
  bort dockikonen), bara STÄNGKNAPPEN i namnlisten (`setMinimizeBox(false)`/
  `setMaximizeBox(false)` — ett fönster som bor i fältet döljs, det
  minimeras inte; Erik 2026-10-12), förvalsikonen G (ingen `setIcon`) med
  fyra av demons verktygsbilder som animation (400 ms, kryssrutan "Animera
  ikonen"), händelserna i FULLFORMEN med ikonen som sender (klick växlar
  fönstret, dubbelklick visar det, `onContextMenu` tänder och släcker
  menyns poster FÖRE öppningen, hovern i statusraden) och menyn i `appIcon`s
  platta form med `{}`-roten (Visa · Dölj · Animera · Om… · Avsluta).
  "Om…" körs ÄGARLÖS (`dlg.run()`): systemfältsmenyn är appens och fönstret
  kan vara dolt — och den är MODAL, så ett klick på ikonen medan den är
  öppen lyfter dialogen i stället för att fyras. Ikonen läggs i fältet i
  `onLoad` (fyras också vid en dold start, `onShow` gör det inte); krysset
  STÄNGER TILL FÄLTET (`onClose`-vetot + `hide()` medan "Stäng till
  systemfältet" är ikryssad — bara för `CloseReason::UserClosing`, TODO 72,
  så Cmd+Q och ett appavslut går igenom), och "Avsluta" sätter en flagga och
  anropar `close()` förbi vetot (appens `close()` är också `UserClosing`). Utan systemfält (Linux) är kryssrutorna och
  dölj-knappen AVSTÄNGDA och krysset avslutar — en app som gömmer sig utan
  en ikon att visa sig igen med går inte att nå. Texterna `rad1`/`rad2` är
  radbrutna och STRÄCKTA (`anchors` vänster+höger, `right = 16`, TEXTFALTEN)
  — höjden följer bredden layouten ger, inget spikat `width = 408`.
- **`splash_window.h/.glazeui/.cpp`** (FONSTER_OCH_TIMER F1) — **`demo
  --splash`**: ett RAMLÖST fönster (`setChrome(gfx::Chrome::None)`) mitt på
  primärskärmens arbetsyta (`setStartPosition(glaze::StartPosition::
  CenterScreen)`), utan knapp i aktivitetsfältet, med rubrik, en radbruten
  rad och ett obestämt förlopp — och en `glaze::Timer stangTimer` i
  REGIONEN: en icke-visuell medlem som aldrig adopteras, `setInterval(1500)`/
  `setSingleShot(true)`/`start()` och kopplingen med `glaze::Timer* s` som
  sender. Hela fönstret är `.glazeui`-vägen; `.cpp` bär bara
  `stangTimerTick` → `close()`. roundtrip_gate läser den byte-identiskt och
  med spar-policyn, preview_gate kräver att texterna är tr-id.
- **`window_lab.h/.cpp`** (FONSTER_OCH_TIMER F2) — **`demo --window-lab`**,
  HANDKODAT (kodvägen): geometriraden ur `geometry()` uppdaterad av en
  upprepande `glaze::Timer` (250 ms, startad i `onShow`, stoppad i `onHide`/
  `onClose` — ett dolt labb väcker inte appen), knapparna Maximera/Minimera/
  Normal (`setWindowState`), "Spara geometrin" och "Öppna nytt med den
  sparade" (plats, storlek och läge satta på ett ANNAT labb FÖRE `show()`; det
  nya är ÄGT och ÅTERANVÄNT — ett stängt visas igen med ny geometri, ett
  öppet säger det), fyra pekarytor (`Panel` med kant och `setCursor`; rubriken
  ovanför, för en etikett inuti hade varit det pekaren träffar) och
  kryssrutan för väntepekaren (`setUseWaitCursor`). Sedan REQ3_LAYOUT_FONSTER
  också "Innehåll" ur `root().onResize`, "Aktivt fönster" ur
  `glaze::app().activeWindow()` i timerns tick och "Följ innehållet": en ägd
  `FoljDialog` (i cpp:n) med `setSizeToContent(Height)` — en Top-dockad
  radbruten text, en kryssruta som visar och döljer tre Top-dockade rader och
  en Bottom-dockad Stäng. Sedan REQ7_LAYOUT också "Formulär…" (1950, till
  höger om "Följ innehållet"): en ägd och återanvänd `FormDialog` (`form_`).
- **`form_dialog.h/.glazeui/.cpp`** (REQ7_LAYOUT) — formuläret, öppnat ur
  fönsterlabbets "Formulär…": `.glazeui`-vägen för REQ7 1–3 i ETT fönster.
  `setContentSize(420, 300)` + `setSizeToContent(WidthAndHeight)` +
  `setSizeToContentMode(GrowOnly)` + `CenterOwner` — innehållet är golvet på
  BÅDA axlarna (med bara `Height` gick fönstret att dra smalare än
  etikettkolumnen och fälten, Eriks test 2026-10-14): två `GroupBox`ar (Kontakt
  med namn/e-post/telefon, Adress med gata/postort) SPÄNDA i sidled och
  syskonankrade uppifrån, kryssrutorna "Visa kontakterna" (`bottom = 12` —
  LUFTEN ovanför knapparna när listan är dold) och "Lås storleken", och en
  kontaktlista (sex namn som DATA) SPÄND mellan sin rubrik och `ok` med
  `height = 96` som minsta mått. OK och Avbryt (ramverkets id 1 och 2,
  `setDialogResult`, standard- och avbrytknapp) står nere till höger och
  adopteras FÖRST — listan ankras mot `ok`, och ett syskonankare pekar bara
  bakåt — med `tabIndex` 100/101 så Tab-ordningen ändå slutar med dem.
  Etiketterna i båda ramarna bär `setWidthGroup("form")` och `setBuddy`, och
  fälten `anchorLeft = {lbl, 8}`, sträckta till ramens högerkant (ramens
  `groupPad` är luften). `.cpp`: `onLoad` (fokus i namnet), `visaAndrad`
  (rubrik och lista visas/döljs) och `lasAndrad` (`GrowAndShrink` låst,
  annars `GrowOnly`). roundtrip_gate läser den byte-identiskt och med
  spar-policyn, preview_gate kräver att texterna är tr-id.
- **`dock_window.h/.glazeui/.cpp`** (DOCKNING E1/E4b/E2/E3, TODO 77;
  DOCKNING_DRAG, TODO 81) — **`demo
  --dock`**: en `DockHost` med fem paneler och ett dokument i huvudytan.
  Hela startlayouten står i `.glazeui`: trädbygget ger ordningen, och EFTER
  det formar `dock.dockPanel(sok, utforskare, glaze::Dock::Fill)` en
  flikgrupp och `dock.dockPanel(egenskaper, utforskare, glaze::Dock::Bottom)`
  en stapel; `utforskare.activate()` väljer fliken. Utforskaren har min 160 och
  max 400 (`setMinimumSize`/`setMaximumSize`), Egenskaper och Utdata en ikon
  (`setImg`, handtagets bild — Utforskaren och Sök visar monogram).
  Utforskarens fillista (`filer`, en `ListBox`) har `onActivate` (REQ5 1, i
  `onLoad`): dubbelklick eller Enter på en fil skriver den i statusraden.
  Egenskaper är ONÅLAD (E2, `egenskaper.setPinned(false)`): ett handtag vid
  vänsterkanten som fäller ut panelen som overlay över dokumentet, och
  knappnålen i dess rubrik fäster den i stapeln. Visa-menyn
  (`activate()` per panel) och Layout-menyn (spara, återställ, startlayouten
  och "Mät" med genvägen M utan modifierare — REQ3_LAYOUT_FONSTER: den körs med
  fokus på en knapp, men ett m som skrivs i ett fält skrivs)
  har sin struktur i `.glazeui` och sina handlers i `onLoad`; Redigera-fältet
  dockas bara uppe eller nere (`setAllowedEdges`, tooltip 1934); statusraden
  (`statusBar.adopt(statusTxt)`) visar `layout()` efter varje `onChange`.
  TERMINALEN (REQ9_TERMINAL F) är en flik bredvid Utdata (`terminalPanel` med
  en `glaze::Terminal`, `dock.dockPanel(terminalPanel, utdata, Fill)` och
  Visa ▸ Terminal): OS:ets skal startat i `onLoad`, `onTitleChange` →
  flikens rubrik, `onExit` → statusraden (2001), startfelet i statusraden
  (2002), och en snabbmeny på `onContextMenu` med Redigera-fältets
  Kopiera/Klistra in (1721/1722 — paketet äger inga strängar). Headern
  inkluderar `<glaze_terminal/terminal.h>` UTANFÖR regionen, och
  `apps/demo/CMakeLists.txt` länkar `glaze_terminal_ui` direkt och stoppar
  med ett skäl om paketet är avstängt (`GLAZE_TERMINAL=OFF`).
  VERKTYGSFÄLTEN (E4b) står ytterst i band: Standard (rullgardinsknapparna
  "Ny ▾" och den DELADE "Ångra ▾" — menyernas poster i `.glazeui`, handlerna
  per stig i `onLoad` — och "Nytt verktygsfält", som adopterar ett ägt fält
  under körning, sist i band 0) och Redigera i band 0 överst, Felsök i band 1
  (`setBand(1)`) och Verktyg på högkant vid vänsterkanten (`dock = Left`, fyra
  ikonknappar). Alla verktygsknappar går till `verktygKlickat` (statusraden).
  Fälten har TITLAR (`setText(glaze::tr(1727…1730))`, också det som "Nytt
  verktygsfält" skapar — "Extra", 1723): det flytande fältets rubrik. FELSÖK
  STARTAR FLYTANDE (`felsok.setFloating(true)`, DOCKNING_DRAG): ett eget
  fönster mitt över huvudfönstret, med rubriken och fast storlek — dubbelklick
  på rubriken dockar det i band 1 överst. Det LEVANDE DRAGET syns här: en
  panels rubrik eller flik och ett fälts grepp lossar objektet, fönstret
  följer pekaren, och platshållaren visar var det dockas (1712/1726 berättar).
  Bevakning STARTAR FLYTANDE (E3, `bevakning.setFloating(true)` i
  `.glazeui`): ett eget fönster ägt av huvudfönstret, mitt över det;
  dubbelklick på rubriken eller ▾ ▸ Docka dockar den vid högerkanten, och
  Visa ▸ Bevakning (`activate()`) lyfter fönstret.
  roundtrip_gate läser den byte-identiskt och med spar-policyn.
- **`main.cpp`** — `StartWindow win; return win.run();` (App-singletonen
  skapas lat); argumenten väljer galleriet eller systemfältsappen (nedan).
  Vanlig `main` på alla plattformar (WIN32-subsystem: MinGW:s runtime slussar
  WinMain→main).
  **`demo --gallery`** öppnar det HANDKODADE galleriet direkt. Demon är en
  RIGG och inte ett exempel (Erik), och argumentet är den enda vägen att nå
  handkodsvägen utan att klicka sig dit — vilket också gör den röktestbar
  för sig: `timeout 3 ./demo --gallery`. **`demo --tray [--hidden]`** öppnar
  systemfältsappen; med stöd för systemfält anropas `glaze::setShowInDock
  (false)` FÖRE fönstret och, med `--hidden`, `hide()` före `run()` — DOLD
  START, bara ikonen syns (en autostart). Utan systemfält ignoreras båda, för
  en dold app utan ikon går inte att nå. `timeout 3 ./demo --tray --hidden`
  ska ge 124: pumpen lever med fönstret dolt. **`demo --window-lab`** öppnar
  fönsterlabbet, **`demo --dock`** dockningen, och **`demo --splash`** visar
  splashen FÖRST (dess `run()`
  återvänder när timern stängt den) och fortsätter sedan med det fönster de
  andra flaggorna valt.

## Mönstret att kopiera (håll det exakt)

```cpp
// xxx.h
class StartWindow : public glaze::AppWindow {
#pragma region GlazeBuilder
    // GENERERAD av GlazeBuilder — redigera i GlazeBuilder, inte för hand.
public:
    StartWindow();
private:
    glaze::Button okBtn;
    void initializeComponent();
#pragma endregion

    // Allt härunder är användarens — buildern appendar bara handlers.
    void onLoad(glaze::Widget* sender, glaze::EventArgs& e);
    void okBtnClicked(glaze::Widget* sender, glaze::ClickArgs& e);
};

// main.cpp
int main(int, char**) {
    StartWindow win;
    return win.run();   // visar + pumpar tills win stängts
}
```

CMake: tripletten läggs i `add_executable` via `glaze_add_window` i
[../CMakeLists.txt](../CMakeLists.txt) — helpern tar `<bas>.cpp` +
`<bas>.glazeui` och sätter `LANGUAGE CXX` på `.glazeui` (genererad C++,
egen extension; bygget genererar ingenting).

## SPRÅKET — demon är ÖVERSATT, hela vägen (SPRÅKPAKET steg 13)

Erik E9: *"demot ska ha all text som ändras … appen ska ju demo:a vad som
är möjligt"* — ett halvöversatt demo visar motsatsen till vad ramen kan.

- **`demo.langtxt`** (i `apps/demo/`) är KÄLLAN: id från 1000 och
  uppåt (galleriets block 1200–1299 växer med komponentlistan — 1248–1253
  är verktygsfältets, 1254–1261 gruppramens — och egna block per spår, så
  samtidiga spår inte krockar i numreringen: 1340–1379 (1340–1348 trädets,
  1349–1359 tooltipens, 1360–1374 FLIKARNAS — flikrubrikerna och grupp
  20), 1380–1399 (1380–1386 listvyns, 1390–1395 datagridens) och 1420–1439
  (1420–1424 delarens, 1430–1437 dockningens, som ÅTERANVÄNDER "Dokument"
  1341 och "Spara" 1350) och 1440–1449 (1440–1444 filväljarfältets — dess
  radetiketter och "Alla filer" är RAMVERKETS id) och 1450–1459
  (1450–1455 färgväljarens) och 1460–1469 (1460–1465 datumväljarens; fliken
  "Väljare" är 1375 och "Inaktiverad" återanvänder 1212) och 1470–1479
  (1470–1476 det flerradiga fältets — texterna BÄR radbrytningar, `\n` i
  langtxt-literalen) och 1480–1509 (1480–1507 ritytans, 1505–1507
  vridsnäppets vallista — fliken "Rityta" är 1376, och "Spara" 1350 och
  "Återställ" 1434 återanvänds; kartans egennamn och gradtalen är data och
  står inte där) och 1510–1519 (1510–1518
  kolumnrubrikernas — rubrikerna bär `\n`) och 1520–1569 (1520–1535
  3D-flikens — fliken "3D" är 1377, och "Etiketter" 1486 återanvänds — och
  1536–1537 + 1550–1569 porteringsprovningens: fliken "3D-verktyg" är 1378,
  kubens sidor, kantläget, miniatyren, förstapersonen och plockningens
  statusrader; "Rutnät" 1521 återanvänds) och 1570–1589 (1570–1584
  systemfältsappens, `tray_window` — "Avsluta" 1004 och "Om GlazeUI…" 1012
  återanvänds; nb- och fi-namnen på systemfältet är skrivna utan
  modersmålstalare) och 1590–1599 (1590–1594 textfältens grupp 29,
  TEXTFALTEN) och 1600–1609 (1600–1602 trädets kryss och flerval i
  statusraden, TRAD_LISTOR_BILDER) och 1700–1739 (1700–1730 `dock_window`,
  DOCKNING E1/E4b/E3 — panelernas titlar är också Visa-menyns poster, 1717–1724
  verktygsfälten, 1725–1726 den flytande Bevakningen, 1727–1730 fältens titlar
  (DOCKNING_DRAG); grupp 17:s fot 1435
  pekar på `demo --dock`) och 1740–1749
  (grupp 9:s rullgardiner, DOCKNING E4a — `dock_window`s "Ny"/"Ångra" och
  deras poster ÅTERANVÄNDER dem) och 1750–1759 (1750–1752 splashen) och 1760–1789
  (1760–1778 fönsterlabbet; talen och tecknen i geometriraden står likadant
  i alla fyra), FONSTER_OCH_TIMER) och 1800–1819 (1800–1811 grupp 30,
  REQ2_INMATNING — rubriken, de två tipsen och statusradens rader; nb och fi
  utan modersmålstalare) och 1820–1829 (1820–1823 ritytans
  lagerlista, REQ2_RITYTAN — ▲/▼:s tooltips, listans prefix och
  statusradens ordning) och 1830–1839 (1830–1831 platshållarna i grupp 29
  och 24, REQ2_DIALOGER; nb och fi utan modersmålstalare) och 1840–1849 (1840–1842 träd och listor,
  REQ2_TRAD_LISTOR — trädets godkännande i statusraden och listvyns
  kryss-tooltip; trädets tooltip 1358 fick ny text på plats; nb och fi utan
  modersmålstalare) och 1865–1884 (FAS1_B_WIDGETS — grupperna 31–34, raden
  "Klockslag" i grupp 23 och statusraderna; kartans lager 1483–1486 och
  ramverkets "Alfa" 255 återanvänds; nb och fi utan modersmålstalare) och
  1900–1919 (REQ3_TRAD_LISTOR_FALT — trädets snabbmeny "Ta bort" och
  statusradens "Snabbmeny för:" i grupp 13 och 14, grupp 36 och filväljarens
  dialogtitel; spåren "01.flac" … är data; nb och fi utan modersmålstalare) och
  1920–1934 (REQ3_LAYOUT_FONSTER — galleriets grupp 35, fönsterlabbets nya
  rader och dialog, dockningens "Mät" och Redigera-fältets tooltip; nb och fi
  utan modersmålstalare) och 1950–1969 (REQ7_LAYOUT — 1950 fönsterlabbets
  "Formulär…", 1951–1960 formuläret; "Namn" 1381 återanvänds, OK och Avbryt
  är ramverkets, kontakternas namn är data; nb och fi utan modersmålstalare,
  finskans långa "Postinumero ja -toimipaikka" med flit) och 1970–1989
  (REQ7_MARKDOWN — grupp 37: rubriken, "Länk: ", dokumentet, den korta vyn och
  raden under; nb och fi utan modersmålstalare) och 2000–2009 (REQ9_TERMINAL —
  dockningens terminalflik: rubriken, slutkoden och startfelet; nb och fi utan
  modersmålstalare) och 2010–2019 (REQ9_INMATNING —
  grupp 27:s lägen och vändningar och listans tooltip), alla
  fyra språken KOMPLETTA. Packas till den committade
  `apps/demo/language.bin`, som CMake kopierar till körplatsen precis som
  `assets.bin`, och `main.cpp` laddar den med `glaze::loadLanguage()`.
- **`demo_strings.h`** är GENERERAD av `glazelanguage pack --header` och
  COMMITTAD (regel 7). **`demo_text.h`** bär den handskrivna hjälparen
  `trs(id)` — `glaze::tr(id).str()` är ett KOMPILERINGSFEL på en temporär
  (`str() const&&` är raderad), så en `std::string` ur ett id måste gå via
  ett namngivet objekt. Widgetarna behöver den aldrig; den finns för de
  ställen där appen bygger ihop en sträng SJÄLV. Bredvid står `tal(v,
  decimaler)` — ett tal med SPRÅKETS decimaltecken
  (`glaze::str::DecimalSeparator`), delad av ritytans flikar.
- **I `.cpp` skrivs KONSTANTEN** (`glaze::tr(demo::str::TryckHar)`), **i
  `.glazeui` NUMRET** (`glaze::tr(1023)`) — och det är inte en inkonsekvens
  utan grammatikens gräns: `builder_core` är ren std utan reflektion och
  kan aldrig lösa ett konstantnamn till ett tal.
- **RAMVERKETS roller återanvänds:** OK · Avbryt · Ja · Nej · Försök igen ·
  Ignorera tas ur `glaze::str::*` och får aldrig egna demo-id — de finns
  redan översatta i alla fyra språken, och ett andra id hade varit en
  andra sanning.
- **SPRÅK-DROPDOWNEN** i `utseende`-panelen byggs ur `glaze::languageAt()`
  så den följer FILEN i stället för en hårdkodad lista, och raderna är
  SJÄLVBENÄMNINGAR (Svenska · English · Norsk bokmål · Suomi) som ALDRIG
  översätts: en väljare som visar "Finska" för den som inte läser svenska
  är oanvändbar. **⚠ Handlern POSTAR bytet** (`glaze::app().post`) —
  `Dropdown::commit()` fyrar `onChange` och anropar `close()` FÖRST
  DÄREFTER, alltså mitt i popupens klick-dispatch, medan `setLanguage`
  river popups SYNKRONT. Ett direkt anrop hade rivit det fönster vars
  event pågår. Grindat i `menu_gate`.
- **`StartWindow::onLanguageChanged()`** fyller om det som INTE är
  `TrText`: listornas items (de är kvar `std::string` — skrivet
  scopesnitt) och de texter appen själv satt ihop av flera delar
  (`"Reglage: " + värde`, `"Hej " + namn + "!"`). `syncSprak()` delas av
  `onLoad` och kroken, så start och drift inte kan glida isär.
- **DET SOM MEDVETET INTE ÖVERSÄTTS:** egennamn i listor (Glaze · Slate ·
  Minimalistic), API-namn (Custom · Information · OKCancel · None · Left …),
  tal (0.8 · 1.0 · 1.25), `name`-rader, assetnycklar, dock-värden och
  GENVÄGAR (`"Ctrl+N"` är den KANONISKA serialiseringsformen och får aldrig
  översättas — visningen översätts av ramverket).
- **Grind:** `preview_gate` läser demons `.glazeui` med den riktiga
  grammatiken och assertar att `setTitle`, `text`/`title`-props och
  menyradens stig- och textargument INTE bär nakna literaler. Den mäter
  ARGUMENTPOSITIONER och inte tecken, så genvägen på tredje platsen i
  `addItem` är oberörd med rätta.

## Lokala konventioner

- **En medlemsfunktion per event**, formen är (sender, args) — hård regel 6:
  `(glaze::Widget* sender, glaze::ClickArgs& e)` / `HoverArgs` (`e.entered`) /
  `ChangeArgs` (läs värdet ur widgeten, t.ex. `animate.isChecked()`) /
  `CloseArgs` (`e.cancel = true` stoppar stängningen och hela kaskaden).
  Raderar du en handler som `.glazeui` refererar → medvetet kompileringsfel
  (ingen runtime-magi).
- **Widgets är värde-medlemmar** — läs/skriv dem direkt, men TILLSTÅNDET
  går via settrar sedan APP_API_2 B2 (`status.setText(...)`,
  `animate.isChecked()`): settern är den som INVALIDERAR, så en tilldelning
  från en timer, en `glaze::app().post()` eller en annan tråd syns
  OMEDELBART. LAYOUT-fälten (`x`/`y`/`width`/`height`/`right`/`bottom`/
  `dock`/`anchors`/`tabIndex`/`name`) är fortsatt RÅA — kontraktet står i
  `widget.h`. De adopteras LÅNAT av trädet (`root().adopt(...)` i
  `.glazeui`); fönsterklassen äger objekten.
- **Redigera aldrig `GlazeBuilder`-regionen eller `.glazeui` för hand** när
  buildern används — den skriver om dem helt vid spara. Handskrivna
  ändringar där ska följa grammatiken exakt (en sats per rad), annars
  vägrar buildern öppna filen (fail-safe).
- **`frame()` ritar under widgetarna** och lyder händelsestyrningen:
  kontinuerlig animation kräver `setAnimating(true)` (se checkboxen) —
  inför aldrig en egen render-loop. `frame()` måste rita deterministiskt:
  vid partiell omritning körs den en gång per damage-rect med klipp.
- **Nytt fönster** = ny triplett (`.h` + `.glazeui` + `.cpp`) + en
  `glaze_add_window`-rad i CMakeLists — kopiera start/second som mall, eller
  låt GlazeBuilder skapa den från sin mall. Rena C++-fönster (dialoger, fria
  fönster) behöver ingen region — se about_dialog/free_window.
  Fönsterobjekt ägs av ANVÄNDAREN (medlem eller lokal variabel runt
  `run(owner)`) — ramverket raderar dem aldrig.
