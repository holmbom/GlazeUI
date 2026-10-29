#pragma once
#include <glaze/glaze.h>

// FIXTUR för METODANROPSSATSEN — och till skillnad från
// design_*-fixturerna KOMPILERAS den här: `tests/CMakeLists.txt` bygger
// `calls_fixture` av tripletten via `glaze_add_window`.
//
// Skälet är att `.glazeui` är
// riktig C++ som BYGGS, så ett rent reader/writer-roundtrip kan vara
// GRÖNT medan den emitterade koden inte kompilerar. Writern får inte
// emittera `statusBar.visible = true;` — reader och writer kunde varit
// överens om en rad kompilatorn avvisar (`visible_` är privat).
//
// Vad fixturen täcker:
//   statusBar   INBYGGD fönstermedlem — ingen medlemsrad här, ingen
//               adopt-rad i .glazeui, men fält OCH anrop
//   menuBar     ANDRA inbyggda fönstermedlemmen, och
//               därmed beviset att `builtinMember` är en TABELL och inte
//               ett specialfall för statusraden. Menyernas INNEHÅLL står
//               aldrig i en `.glazeui` — det byggs i `onLoad` — så det som
//               round-trippar är exakt `menuBar.show();`
//   appIcon     TREDJE inbyggda fönstermedlemmen och
//               den första som är SYNLIG som förval. Bildernas två former
//               (`glaze::img(…)` och `{}` = förvalet) och de två bools är
//               SETTER-fält; programmenyn är menyradens platta form med den
//               TOMMA stigen `{}` som roten — `addItem({}, …)` kompilerar
//               bara för att `MenuPath()` är icke-explicit, och fixturen är
//               beviset. En undermeny adresseras som på menyraden
//               (`addItem("Exportera", "PDF")`)
//   prog        `setIndeterminate(true)` — anrop MED argument
//   btn         `disable()` — anrop på en vanlig widget
//   panel       `showScrollbars(true)` — widget-specifikt anrop
//   NAKNA      fönsteranropen: `setContentSize`,
//   FÖNSTER-   `setMinimumSize`/`setMaximumSize`, `setControlBox`/
//   ANROP      `setMinimizeBox`/`setMaximizeBox`, `setModal`/`setPopup`/
//              `setTransparent`/`setTopmost` — nakna anropssatser utan
//              mottagare, som utan en egen grammatikregel gav
//              FAIL-SAFE PÅ HELA FILEN
//              `setShowInTaskbar` är samma
//              form: ett attribut som läses när fönstret öppnas.
//              `setAcceptButton(trBtn)`/`setCancelButton(btn)`
//              bär en MEDLEM som argument — fixturen bevisar
//              att identifieraren kompilerar som argument till fönstret, och
//              `trBtn.setDialogResult(glaze::DialogResult::OK)` är knappens
//              setter-fält (en enum-rad)
//   setAnimating ett naket fönsteranrop `applyWindowCall` INTE känner:
//              raden kompilerar, round-trippar ordagrant och bevaras
//              opakt (regel 4). Fönstrets motsvarighet till `focus()`
//   lista       ListBox: `setItems({...})` är den enda
//               satsen i grammatiken vars VÄRDE är en LISTA, och det är
//               därför den ligger här — den ska round-trippa ordagrant OCH
//               kompilera. `setShowChecks`/`setSelectedIndex` bredvid, så
//               hela widgetens setter-vokabulär mäts i samma fil.
//   etikett     `setWrap(true)` — Label-radbrytningen
//               är en SETTER-egenskap, alltså en Prop som skrivs som ett
//               ANROP. Fixturen mäter både att den round-trippar och att
//               raden KOMPILERAR. Dess `setText` bär dessutom
//               **ESCAPE-DELMÄNGDEN**: `\n`,
//               `\t` och `\"` i EN literal. Avkodas `\n` fel (till bokstaven
//               `n`) visar previewn `radnrad` medan den byggda appen bryter raden —
//               och en omredigering i egenskapspanelen skriver tillbaka den
//               FÖRVRÄNGDA strängen (förlorad användardata, regel 4).
//               Raden är därför både round-trip- och kompileringsbevis
//   SYSKON-    `prog.anchorTop = {"panel", 12};`
//   ANKARNA    och `etikett.anchorLeft = {"prog", 8};` — STRUCT-LITERALEN
//              är fältradens värdeform, och den ska både round-trippa
//              ordagrant och KOMPILERA. `lista.anchorLeft = {"panel"};`
//              och `lista.anchorBottom = {};` är de två KORTA formerna:
//              båda är laglig C++ (gap tar sitt default, respektive
//              nollställt aggregat) och avkodas därför i stället för att
//              bli opaka — en rad som kompilerar och som previewn inte
//              utför vore ett WYSIWYG-brott (hård regel 8).
//              `panel.name`/`prog.name` är MÅLENS namn-rader: utan dem
//              hittar `Widget::resolve` ingenting i den byggda appen,
//              eftersom `Widget::name` annars är tomt. Raden står på VARJE
//              nod — den är
//              KANONISK (`bld::nameState`), inte något buildern lägger
//              till bara för refererade noder
//   KOLLEKTIONS-  `grid`/`dd`/`lista` bär
//   VOKABULÄREN   hela den gemensamma formen `addX` · `setX(index, …)` ·
//                 `setXs(lista)`. `grid.setRows({{…}})` är den enda satsen
//                 i grammatiken vars argument är en NÄSTLAD structlista, och
//                 den ska både round-trippa ordagrant och KOMPILERA — andra
//                 raden bär dessutom `choices`, `readOnly` och `tag`, alltså
//                 hela `Row`-aggregatet. Satserna är MEDVETET varvade med
//                 en OKÄND metod (`grid.setScroll(0, 0)` — riktig
//                 Panel-metod som `applyCall` inte känner) och med en Prop
//                 (`setNameWidth`), så byte-identiteten grindar den
//                 RELATIVA ORDNINGEN och inte bara nodklassningen (regel 4)
//   lista.setChecked  KOLLISIONEN: `setChecked` heter likadant på
//                 Checkbox/Radio med ETT bool-argument. Settertabellen är inte
//                 namn-global — annars lagrades raden som
//                 `Prop{checked, "1, true"}` på en ListBox — den
//                 round-trippade av en SLUMP och utfördes aldrig. Den är
//                 en `Call`, och fixturen är regressionslåset
//   etikett.setColor  `color` finns inte i settertabellen:
//                 `Label::setProp("color")` är ett ÄLDRE ALIAS för `role`
//                 och vill ha ett ROLLNAMN, medan `setColor` tar en riktig
//                 `gfx::Color`. Raden är därför en Call — och den bevisar
//                 att `gfx::Color{…}`-literalen kompilerar och round-trippar
//   btn         `focus()` — en RIKTIG metod som `applyCall` inte känner:
//               raden kompilerar, round-trippar ordagrant och bevaras
//               opakt i previewn (regel 4 — ALDRIG fail-safe). Exakt det
//               en okänd metod från en nyare version ska göra.
//   btn         APPENS MUS PÅ VARJE WIDGET: `btn.onMouseDown` FULL
//               med `glaze::MouseArgs` och `btn.onMouseCancel` KORT — basens
//               fält på en knapp, inte ritytans. Fixturen bevisar att båda
//               formerna KOMPILERAR; att de är kopplingar och inte opaka
//               rader mäter roundtrip_gate.
//   DRAG-AND-  `btn.setAllowDrag(true);` och
//   DROP       `panel.setAllowDrop(true);` är BASENS setter-fält — samma
//              form på varje widget — och `btn.onDragStart` (KORT) +
//              `panel.onDrop` (FULL, med `glaze::DragArgs`) är två av de
//              sex nya eventen. Fixturen bevisar att formerna KOMPILERAR
//              och round-trippar; att previewn utför dem mäter preview_gate.
//   trad        TRÄDET: kollektionsvokabulären med
//               STIGAR. `setNodes` bär den REKURSIVA aggregatliteralen
//               (`glaze::TreeNode` — text, barn, bild, utfälld) med en
//               tr-text och en `{}`-bild, `addNode` båda bildformerna,
//               `setNode`/`setExpanded`/`moveNode`/`removeNode` var sin
//               rad, och `setSelectedPath({1, 0})` är ett SETTER-fält vars
//               värde är en stiglitteral. `onExpand` (KORT) och `onCollapse`
//               (FULL, `glaze::ChangeArgs`) är trädets två egna events.
//               `setAllowDropInto({2, 0}, false)` gör en nod till
//               ett LÖV för släppet — en stig och en flagga, som
//               `setExpanded`.
//               Allt ska KOMPILERA — en `{0, 1}` som C++ tolkar som något
//               annat än `glaze::TreePath` hade synts här först.
//               `setToolTip(glaze::tr(5))` är TOOLTIPEN: basens
//               setter-fält i översättningsformen — på en widget UTAN egen
//               `setTextToken`, så det är basens gren som bär den.
//               FLERVAL, KRYSS OCH FÄRG: `setMultiSelect`/
//               `setShowChecks` är setter-fält, `setChecked(stig, på)`/
//               `setSelected(stig, på)`/`setNodeColor(stig, färg)` (färgen som
//               `gfx::Color{…}` och som `{}`) och de argumentlösa
//               `selectAll()`/`clearSelection()` Call-satser, `addNode`/
//               `setNode` med FJÄRDE argumentet (färgen, och `std::nullopt`) —
//               och `setNodes` med alla SJU fält positionellt, DESIGNERAT
//               (`{.text = …, .checked = true, .color = …}`) och med
//               typnamnet `glaze::TreeNode{…}`. `onCheck` (KORT) är trädets
//               nya event. En bar `{200, 0, 0}` kompilerar inte till en
//               `std::optional` och står därför INTE här.
//               TABELLENS RADFÄRG står på `tabell` (nedan): `addRow`/`setRow`
//               med TREDJE argumentet, `setRows` med 3 fält, designerat och
//               `glaze::Table::Row{…}`, och `setRowColor(index, färg)`.
//               MENYPOSTENS TILLSTÅND: `setItemIcon`/`setItemEnabled`/
//               `setItemChecked` på `menuBar` (titelvägen, ett tr-segment)
//               och på `appIcon` (stigen från `{}`-roten).
//               FLYTTEN AV FLERA: `moveNodes({{1, 0},
//               {2, 0}}, {0}, 0)` med en STIGLISTA — giltig i läget före den
//               sista `setNodes` (Dokument{}, Foton{tr3}, Musik{Spår 1},
//               Fel), som fyller om, så sluttillståndet rörs inte men satsen
//               utförs. Fixturen bevisar att `{{…}, {…}}` KOMPILERAR till
//               `std::vector<glaze::TreePath>`.
//   farg        FÄRGVÄLJAREN: `setColor` i BÅDA sina
//               former — hextexten (designerns, en strängliteral) och
//               `gfx::Color`-literalen (kodens). Två överlagringar av samma
//               namn, och fixturen bevisar att båda raderna KOMPILERAR —
//               en strängliteral som hade valt fel överlagring eller blivit
//               tvetydig mot aggregatet hade synts här först.
//   datum       DATUMVÄLJAREN: datumet och gränserna i
//               den PLATTA formen — tre heltal per anrop, Call-satser — och
//               fixturen bevisar att `setDate(int, int, int)` KOMPILERAR bredvid
//               överlagringen med `std::chrono::year_month_day`.
//   karta       RITYTAN: VARJE form previewn utför —
//               setter-fälten (y, gesterna, toningen, rutnätet, kanten,
//               kamerans tal), `setBackground(gfx::Color{…})`, kameran
//               (`setCamera` designerad, `setCenter`/`setZoomRange`/
//               `setMapScale`/`zoomToRect`), lagren i kollektionsvokabulären
//               och FORMLISTAN: varje fabrik i shape.h, Stroke designerad OCH
//               positionell, TextStyle/MarkerStyle designerade och bara som
//               klammerlista, Paint som färg, linjär och radiell gradient
//               (stoppen positionella och designerade), mått i px/mm/world,
//               enumprefixen med `glaze::` och `gfx::`, `glaze::img`, `{}` som
//               ingen bild, `glaze::tr`, `std::nullopt` och vägen med alla
//               verb. Överlagringarna är C++:s egna — `polygon` med punkter
//               mot ringar, `path` som yta mot kontur, `addShape`/`setShape`/
//               `setShapes`/`removeShape` med och utan lager — och fixturen
//               bevisar att varje rad KOMPILERAR till den överlagring
//               previewn antar. `onPaint` (KORT) och `onCameraChange` (FULL,
//               `glaze::ChangeArgs`) är två av ritytans nio events.
//               BILD OCH TEXT: `image` på FYRA FRIA HÖRN (fem argument
//               med opacitet, sex med `glaze::Point{…}` först och `{}` som
//               källa — överlagringen bredvid rektangelns avgörs av antalet)
//               och etiketter i DELAR — `glaze::TextSpan{…}` designerad med
//               `glaze::tr`, bara klammerlistor positionella med
//               `std::nullopt`, och `text3` med delar. `{}` och `{{"abc"}}`
//               står INTE här: de är tvetydiga och kompilerar inte.
//               I RYMDEN:
//               `setMultisample`/`setDepth` (setter-fält), `setLayerPlacement`,
//               `setLayerRenderState(glaze::RenderState{…})` designerad,
//               `line3`/`polyline3`/`text3`/`marker3` med `Point3` som
//               klammerlista, och `setCamera(glaze::Camera3D{…})` — överlagringen
//               bredvid kartkamerans. FÖREMÅLEN:
//               `setLighting(glaze::Lighting{…})` med alla fält i ordning,
//               `box`/`sphere` (radien i mm och i världen)/`cylinder`/`tube`/
//               `polygon3` med `MeshStyle` designerad och som klammerlista,
//               `mesh` med `Mesh::triangles`/`Mesh::indexed` och en
//               designerad `Placement`, `instances` med en primitiv som
//               prototyp och instanserna som designerade klammerlistor, och
//               `points3` med `PointStyle` och en `ColorRamp`. DEL-VYERNA i
//               kollektionsvokabulären — `addView`/`setView`/`setViews`/
//               `removeView` med `glaze::View{…}` designerad (en nästlad
//               `glaze::Camera3D{…}`, ankaret med båda prefixen) — och
//               `setLayerViews(1, {0, 2})`. Därtill de övriga
//               formerna C++ tar: `setOrbitTumble` (setter-fält),
//               orbitens spann, `setTicking`, ljuset och renderläget som BARA
//               klammerlistor (en överlagring var — de kompilerar),
//               3D-kameran POSITIONELL, som `lookFrom` och som BAR DESIGNERAD
//               lista bredvid kartkamerans — `setCamera` är överlagrad, och en
//               designerad lista väljer överlagring på NAMNEN; en bar
//               positionell hade varit tvetydig och står därför inte här —
//               `zoomToBox`, en `MeshStyle` med alla tolv fält i ordning,
//               sfären med ett naket tal (px), instanser av en låda och av en
//               mesh, ett pass utan stil, en bar vy med en bar positionell
//               kamera (inne i vyn finns ingen överlagring), och HANDLINGARNA
//               `orbitBy`/`panBy`/`dollyBy`/`easeTo(Camera3D, ms)`. `onTick`
//               (KORT) och `onGesture` (FULL, `glaze::GestureArgs`) är
//               rymdens två events. LAGRENS ORDNING:
//               `moveLayer(2, 0)` med två heltal — ett TILLSTÅND och ingen
//               handling, så den utförs — och ordningen inom ett lager under
//               sitt nya namn `setLayerDrawOrder`. Previewn UTFÖR varje
//               karta-sats utom handlingarna, som bevaras opakt —
//               `preview_gate`s `fallCallsRityta` kör hela listan.
//               INMATNINGEN: `onMouseCancel` i BÅDA formerna
//               (FULL med `glaze::CanvasMouseArgs`, därefter KORT — två
//               tilldelningar av samma fält, den sista vinner i drift; raderna
//               är här för att varje form ska KOMPILERA och round-trippa) och
//               `onKeyUp` FULL med `glaze::KeyArgs` — basens fält, ärvt.
//               BINDNINGARNA: `setInputMode` (setter-fält),
//               `setBinding` med fyra och sex argument och modifierarna som
//               `0` och `gfx::kModCtrl | gfx::kModShift`, `None`, ett eget
//               läge, `removeBinding`, käll- och handlingsvändningarna och
//               deras `clear…`. Basens `setWheelInvert`/`clearWheelInvert` på
//               `lista` och fönstrets `setWheelInvert` naket.
//               TEXT- OCH MARKÖRMÅTT: TextStyle med taket (`maxLines`, `maxHeight`),
//               golvet och taket (`minSize`, `maxSize`) och zoomen (`minZoom`,
//               `maxZoom`) designerade SIST i deklarationsordning, MarkerStyle
//               med sina fyra, och minnesbudgeten `setMemoryBudget(268435456)`
//               som setter-fält (heltal, 256 MiB).
//   tabell      KOLUMNHUVUDET PÅ FLERA RADER: huvudets
//               två setter-fält (`setHeaderLines(4)`, `setHeaderVAlign(
//               glaze::VAlign::Bottom)`) och kolumnaggregatets FJÄRDE fält —
//               rubrikens justering som `std::optional<glaze::Align>` — i
//               massformen med alla tre formerna på platsen (en justering,
//               `{}` och `std::nullopt`) och i styckeformens fjärde
//               argument. Rubriken bär `\n`, som ska round-trippa ordagrant.
//               Fixturen bevisar att varje rad KOMPILERAR: en utelämnad
//               fjärde plats och `{}` är laglig C++ just för att fältet är
//               en optional. `dd.setAlign(…)` bredvid är dropdownens
//               setter-fält (datagridens val-editor).
//               KRYSSEN: `setShowChecks(true)` är ett
//               setter-fält och `setChecked(1, true)` en Call-sats (två
//               argument, som ListBoxens) — SIST, efter `setRows`, som nollar
//               kryssen — och `tabell.onCheck` KORT är listvyns event.
//   falt        COMMIT, FOKUS OCH MARKERING: `selectAll()` och
//               `setSelection(0, 2)` är anrop som previewn KÄNNER (de sätter
//               markeringen, som inte syns utan fokus), och de fyra
//               eventnamnen står i BÅDA formerna — `falt.onCommit` och
//               `panel.onFocusLost` KORTA, `falt.onFocus` (med
//               `glaze::FocusArgs`) och `grid.onCommit` FULLA — plus
//               `lista.onCheck` KORT, som utan namnet i `isKnownEvent`
//               blir en opak rad. Fixturen bevisar att varje rad
//               KOMPILERAR; att de är kopplingar och inte opaka rader mäter
//               roundtrip_gate. `falt.setPassword(true);` är
//               lösenordets setter-fält — readern gör den till en Prop och
//               writern skriver tillbaka anropsformen; utan raden i
//               `kSetterFields` hade den blivit fältraden
//               `falt.password = true;`, som inte kompilerar.
//               `falt.setPlaceholder("Höjd i meter")` och `falt.setMaxLength(12)`
//               är setter-fält av samma skäl — en hint och ett
//               heltal, och båda raderna måste kompilera. TEXTSTORLEKEN
//               likaså, i alla tre måttformerna: `falt` med
//               `glaze::mm(3)`, `anteckning` med `glaze::px(16)`, `dd` med ett
//               naket tal och `lista` med `glaze::mm(2.5)`.
//               TANGENTERNA PÅ VARJE WIDGET: `falt.onKeyDown`
//               FULL och `falt.onKeyUp` KORT bevisar att basens fält finns på
//               `glaze::TextInput` (och inte bara på ritytan), och
//               `tabell.onKeyDown` KORT är den andra formen av `onKeyDown`.
//   dock1       DOCKNINGEN: en värd med tre paneler. Panelernas
//               setter-fält (`setText` som literal och som `glaze::tr`,
//               `setClosable`, `setCollapsed`, `setImg(glaze::img(…))`), min/max
//               som anrop med två mått och de RÅA `width`/`dock` — och EFTER
//               trädbygget värdens `dockPanel` i båda formerna (panel + kant,
//               och panel + mål + var) med panelerna som IDENTIFIERARE, och
//               `utforskare.activate()`. Raderna står efter `adopt` med flit:
//               appen kör källordning, och `dockPanel` gäller barn. Sluttillståndet
//               (en flikgrupp utforskare/sok med en stapel egenskaper under,
//               allt till vänster) mäter preview_gate; att raderna KOMPILERAR
//               och round-trippar mäter `calls_fixture` och roundtrip_gate.
//               KNAPPNÅLEN: `egenskaper.setPinned(false)` är
//               setter-fältet `pinned` — efter `setCollapsed(false)`, så
//               panelen slutar som ett HANDTAG (onålad = infälld) i både appen
//               och previewn.
//               Värden bär också ett VERKTYGSFÄLT: `setBand(2)`
//               är setter-fältet `band` och `dock = Left` det råa fältet — i
//               previewn satta EFTER trädbygget, i appen före, och båda ger
//               samma band (index 0 vid vänsterkanten; nyckeln packas).
//               `verktygsrad.setFloatable(false)` är
//               setter-fältet `floatable`, och
//               `verktygsrad.setAllowedEdges({glaze::Dock::Top, glaze::Dock::Bottom})`
//               en Call-sats med en LISTA av kanter.
//               Bland fönsteranropen står `setSizeToContent(
//               glaze::SizeToContent::Manual)` (Manual, så fixturens
//               fönster inte byter storlek i andra fall) och
//               `setSizeToContentMode(glaze::SizeToContentMode::GrowOnly)`
//               (med flit INTE förvalet, så preview_gate ser
//               att det utfördes; `Manual` struntar i läget), och etiketten bär
//               `etikett.setWidthGroup("form")` (ett setter-fält med en
//               strängliteral; raden kompilerar), och `karta.onResize`
//               FULL och `falt.onResize` KORT är basens storlekshändelse. Och ett FLYTANDE verktygsfält,
//               `flytrad`: `setText(glaze::tr(166))` (setter-fältet `text`
//               med ett tr-id), `setFloating(true)` (setter-fältet
//               `floating` — fältet lämnar bandet, och previewn öppnar inget
//               fönster) och `setFloatingPosition(500, 80)` (en Call-sats
//               med två tal).
//   nyKnapp     VERKTYGSKNAPPENS RULLGARDIN: `setSplit(true)` är
//               setter-fältet `split`, menyn systemfältsikonens platta form —
//               alla fem add-satserna med `{}` som roten och UTAN
//               genvägsargument (ett tredje argument till `addItem` hade inte
//               kompilerat), en undermenypost med `glaze::tr` — postens tre
//               tillstånd, och `onDropDown` i den KORTA formen: utan namnet i
//               `isKnownEvent` hade kopplingen blivit en opak Prop som
//               round-trippar lika byte-exakt. Att previewn bygger menyn mäter
//               preview_gate (fallKnappmeny), att raderna KOMPILERAR
//               `calls_fixture`.
//   NYA WIDGETFORMER — varje form, och att den
//               KOMPILERAR: `kryss` med kryssrutans två nya setter-fält
//               (`setThreeState`, `setCheckState(glaze::CheckState::…)`),
//               `flerval` med ListBoxens flerval (`setMultiSelect` som
//               setter-fält, `setSelected`/`selectAll`/`clearSelection` som
//               Call-satser), `flikar` med en medlemssida och SIDORNAS
//               vokabulär (`addPage` med och utan `glaze::tr`, `setPage`) EFTER
//               trädbygget — previewn bygger trädet före satserna — och
//               `flikar2` med `setPages({…})` utan medlemssidor (ordningen kan
//               då inte skilja), `rutnat` med datagriddens radval
//               (`setSelectionMode(glaze::DataGrid::SelectionMode::FullRow)`,
//               `setMultiSelect`, `setSelected`) och `onSelectionChange` KORT,
//               `alfa` med `setColor("#RRGGBBAA")` FÖRE `setAllowAlpha(true)`
//               (läsningens regel), `tid` med `setShowTime(true)` och
//               `setTime(14, 30)`, och statusradens fält — `setPanels` med en
//               `glaze::tr`, `addPanel`, `setPanel` och `setPanelText`. Att
//               previewn utför dem och sluttillståndet mäter preview_gate
//               (fallCallsFas1B).
//   hjalp       MARKDOWN-VYN: dokumentet är
//               setter-fältet `text` — EN literal med `\n`, `\"` och `\\`
//               (escape-delmängden, byte-exakt åt båda hållen) och sedan
//               `glaze::tr(2)` —, bildtabellen `setImage(namn, glaze::img(…))`
//               och `setImage(namn, {})` (tar bort) och `scrollToAnchor` är
//               Call-satser, `setBorder(true)` Panels setter-fält, och
//               `onLinkClick` står i BÅDA formerna — utan namnet i
//               `isKnownEvent` hade den korta blivit en opak Prop som
//               round-trippar lika byte-exakt. Att previewn utför satserna
//               mäter preview_gate (fallMarkdown), att raderna KOMPILERAR
//               `calls_fixture`.
//
// DEN KORTA EVENT-FORMEN — fyra rader, fyra utfall:
//   btn.onClick    KORT widgetform ⇒ `Kind::Event` med `shortForm`, och
//                  skrivs tillbaka KORT. Som opak fältrad hade buildern
//                  skrivit över användarens koppling med en andra `onClick`
//   btn.onHover    FULLFORMEN bredvid — de två får inte störa varandra
//   panel.onClick  kort form med ett ARGUMENT i kroppen: giltig C++ som
//                  `glaze::Handler` tar emot, men INTE ett naket
//                  parameterlöst anrop ⇒ opak `Prop`, aldrig fail-safe
//   glaze::AppWindow::onLoad   KORT FÖNSTERFORM — den saknar punkt och får
//                  inte falla till "oidentifierbar sats" (fail-safe på
//                  hela filen)
//   btn.setText    sekvensen `= [this] { … };` inuti ett STRÄNGVÄRDE:
//                  ska förbli en vanlig `Prop`, varken `Event` eller
//                  fail-safe. Det är fixturen som mäter att den breddade
//                  nyckeln inte blev en regression. (Inte på `btn.name`:
//                  `name` är en KANONISK rad
//                  som måste bära nodnamnet, så testvärdet ligger på
//                  en setter-rad med exakt samma form.)
class CallsFixture : public glaze::AppWindow {
#pragma region GlazeBuilder
    // GENERERAD av GlazeBuilder — redigera i GlazeBuilder, inte för hand.
public:
    CallsFixture();

private:
    glaze::Button btn;
    glaze::Panel panel;
    glaze::Progress prog;
    glaze::Label etikett;
    glaze::ListBox lista;
    glaze::PropertyGrid grid;
    glaze::Dropdown dd;
    glaze::Button trBtn;
    glaze::TreeView trad;
    glaze::ColorPicker farg;
    glaze::DatePicker datum;
    glaze::Canvas karta;
    glaze::ListView tabell;
    glaze::TextInput falt;
    glaze::DockHost dock1;
    glaze::DockPanel utforskare;
    glaze::DockPanel sok;
    glaze::DockPanel egenskaper;
    glaze::ToolBar verktygsrad;
    glaze::ToolBar flytrad;
    glaze::ToolButton nyKnapp;
    glaze::Checkbox kryss;
    glaze::ListBox flerval;
    glaze::TabControl flikar;
    glaze::TabPage sida1;
    glaze::TabControl flikar2;
    glaze::DataGrid rutnat;
    glaze::ColorPicker alfa;
    glaze::DatePicker tid;
    glaze::Radio val1;
    glaze::TextArea anteckning;
    glaze::FilePicker filval;
    glaze::MarkdownView hjalp;
    void initializeComponent();
#pragma endregion

    // Allt härunder är användarens — handlers för event-formerna ovan.
    // De TRE parameterlösa är hela poängen: en handler som tar noll
    // argument går inte att kanonisera till fullformen (`knappTryckt(s, e)`
    // hade inte kompilerat), och att rätta signaturen vore att skriva i
    // användarens kod — regel 5.
    void knappTryckt();
    void knappHovrad(glaze::Widget* sender, glaze::HoverArgs& e);
    void medArgument(int n);
    void fonstretLaddat();
    void knappDras();
    void knappNer(glaze::Widget* sender, glaze::MouseArgs& e);
    void knappAvbruten();
    void panelSlapp(glaze::Widget* sender, glaze::DragArgs& e);
    void tradUt();
    void tradIhop(glaze::Widget* sender, glaze::ChangeArgs& e);
    void tradKryss();
    void kartaRitas();
    void kartaFlyttad(glaze::Widget* sender, glaze::ChangeArgs& e);
    void kartaTickar();
    void kartaGest(glaze::Widget* sender, glaze::GestureArgs& e);
    void listaKryssad();
    void tabellKryssad();
    void gridSparad(glaze::Widget* sender, glaze::ChangeArgs& e);
    void faltKlart();
    void faltFokus(glaze::Widget* sender, glaze::FocusArgs& e);
    void panelLamnad();
    void knappMeny();
    void faltTangent(glaze::Widget* sender, glaze::KeyArgs& e);
    void faltSlapp();
    void tabellTangent();
    void kartaAvbruten(glaze::Widget* sender, glaze::CanvasMouseArgs& e);
    void kartaAvbrott();
    void kartaSlapp(glaze::Widget* sender, glaze::KeyArgs& e);
    void kartaStorlek(glaze::Widget* sender, glaze::ResizeArgs& e);
    void faltStorlek();
    void rutnatValt();
    void hjalpLank(glaze::Widget* sender, glaze::LinkClickArgs& e);
    void lankKlickad();
};
