# TODO — GlazeUI

Planer: `doc/plans/` och arkivet `doc/plans/archive/` (lokala — committas inte) · research: [doc/research/](doc/research/)

## Regler

1. **TRE RADER PER POST** — status + rubrik + länk på rad 1, sedan **max två rader** text. Allt annat bor i planfilen.
2. **Ingen historik här.** Utfört arbete står i arkivet och i commit-loggen. Resonemang, mätningar och öppna frågor hör hemma i `doc/plans/` (ofärdig plan, lokal) eller `doc/research/` (mätning, med länk).
3. **Ny rad → 🐛 eller 💡**, ingen planfil. Blir det en arbetsström: planfil i `doc/plans/` (lokal, ingen länk), byt till 🔴. En bugg behåller 🐛 efter lägeskoden.
4. **Uppdatera ALLTID** när status ändras.
5. **🟢 → Eriks test → arkiv.** Kod klar men otestad = 🟢. Efter godkänt test: planfilen till `archive/`, raden bort härifrån.
6. **Ordning:** 🐛 → 💡 → 💭 → ☁️ → 🔴 → 🟠 → 🟢, med 📖/🔬 sist. Buggar överst — trasigt går före önskat.

**Koder** · **sort:** 🐛 bugg · 💡 idé · 🔧 golv · 🎨 utseende · ♿ tillgänglighet · 🌍 språk · 📚 dokumentation
**läge:** 💭 tankefil · ☁️ scope beslutat · 🔴 plan finns · 🟠 påbörjad · 🟢 test kvar · 📖 referens · 🔬 research

---

# v1.0

**Nio villkor:** golvet (**STÄNGT** 2026-08-22 — inga öppna poster) · språket (**BYGGT** 2026-08-22; tillgängligheten flyttad till v1.0+ 2026-08-23) · komponenterna · förutsättningarna · alla buggar · sidopaketen · Windows+macOS · GlazeBuilder · dokumentationen.

**TVÅ FASER** (Erik 2026-08-21): **fas 1 är GlazeUI** — den ska funka genom kodvägen OCH `.glazeui` — **fas 2 är GlazeBuilder**. En fas 1-post är klar när **tre** saker gäller: C++-API:t finns · formen går att uttrycka i `.glazeui` · previewn UTFÖR den.

## Fas 1 — `builder_core` + `builder_preview`

Ramverket plus `.glazeui`-vägens maskineri: grammatiken (`window_model`/`cpp_reader`/`cpp_writer`) och utförandet (`apply_props`/`preview_tree`/`palette`/`standin_widget`). Editorns exe är fas 2.

### Resten av fas 1

- 🐛 **macOS: en utloggning avbryts av appen också när den inte vetar** — [metal_platform.mm](Glaze/gfx/src/backend_metal/metal_platform.mm)
  `applicationShouldTerminate` svarar `NSTerminateCancel` och köar `Quit`, så systemet ser ett avbrott. Rättelsen: `NSTerminateLater` och `replyToApplicationShouldTerminate:` när App stängt allt (eller fått ett veto).
- 🐛 **Windows 11 26300: kärnans adress och adaptertyp svarar tomt** — [d3d11_gpu.cpp](Glaze/gfx/src/backend_d3d11/d3d11_gpu.cpp)
  `D3DKMTQueryAdapterInfo` lyckas men ger `00:00.0` och typen 0 för alla kort. Slaget kommer då bara ur proben (loggen säger "okänt slag"), och två lika kort går inte att skilja åt.
- 💡 **`DockHost::placeChild` räknar hela dockningslayouten en gång per barn** — [dock_host.cpp](Glaze/widgets/src/widgets/dock_host.cpp)
  Varje anrop kopierar och normaliserar modellen (`synced()`) och kör `computeLayout` med verktygsfältens mätning — O(barn × layout) flera gånger per bildruta. Rättelsen: ett minne per layoutpass (som `ResolveScope`) med en säker nyckel för modellen, `DockLive` och Ctx.
- 🔴 **Simplify över kodbasen och hela sviten på Linux**
  `/simplify` över det som tillkommit utan genomgång, och sviten (statisk + delad + röktest) på Linux; bara de fallerande om efter rättning.
  Ändrade shaders (HLSL/GLSL/MSL) kompileras först i körning — röktestet och `scene_pixel_gate`/`scene3d_pixel_gate` avgör.
- 💡 **Textvägen formar grafemkluster (ZWJ-emoji, hudtoner) som EN glyf**
  Terminalen räknar cellbredden per kluster rätt, men `drawText` rasteriserar och placerar varje kodpunkt för sig, så en ZWJ-familj ritas som flera emoji. Formningen hör hemma i alla tre backendarnas textväg.
- 💡 **Dra och släpp filer från andra program** — [widget.h](Glaze/widgets/include/glaze/widget.h)
  `setAllowDrop` och `onDragEnter`/`onDragOver`/`onDragLeave`/`onDrop` också när källan är Utforskaren eller Finder: `DragArgs::source` nullptr, sökvägarna i UTF-8 som last, effekten `Copy` eller ingen.
  OS-delen: `IDropTarget` på Windows, `NSDraggingDestination` på macOS; Linux kan vänta. Bara in i appen, bara filer.
- 💡 **`messageBox`: valbar standardknapp** — [message_box.h](Glaze/widgets/include/glaze/message_box.h)
  I dag är den första knappen standard, så Enter i "Ta bort alla poster?" svarar Ja. En standardknapp per anrop som får fokus och blir `setAcceptButton`; Escape och krysset ger avbryt som förut.
- 💡 **Publik fråga om en meny eller vallista är öppen i fönstret**
  `AppWindow::hasOpenMenu()` (eller på `App`) som svarar för menyraden, snabbmenyer, Dropdown-listor och dockningspanelernas menyer men inte för tooltips — så en app kan låta Esc stänga menyn i stället för att avbryta sitt eget kommando.
- 💡 **Tangenterna i en flytande dockningspanel når ägarfönstret**
  Ägarens `event()` och `acceleratorFallback` får också de tangenter ingen widget i den flytande panelen tog, som om panelen satt dockad. I dag släpps bara menygenvägarna igenom.
- 💡 **Händelse när appens tema eller läge byts**
  Bara OS:ets byte ger en händelse (`EventType::SystemThemeChanged`). Ett `onThemeChanged` som fyrar vid `setThemeMode`, vid byte av tema och vid OS-bytet i System-läget, efter att det nya temat gäller.
- 💡 **`PropertyGrid` som ett tabbstopp med en aktuell rad** — [property_grid.h](Glaze/widgets/include/glaze/widgets/property_grid.h)
  Gridden är inget tabbstopp, så Space når bara en kryssruta som just tryckts. En aktuell rad som ListViews: pilarna flyttar den, Space växlar en kryssruta, Enter eller F2 öppnar cellens redigerare.
- 💡 **Redigerbar kombinationsruta (`ComboBox`)**
  En `Dropdown` där man också kan skriva ett eget värde: `text()`, `setItems`, `onChange` när texten ändras och `onCommit` vid Enter eller fokusförlust; listan filtreras gärna på det som skrivits.
- 💡 **Linfysik: nedhäng efter temperatur, töjning, vind och is**
  Linor mellan fästpunkter som hänger korrekt (kedjekurva med elasticitet och värmeutvidgning), belastade av simulerad vind och is, med temperaturen som parameter.
  Öppet: en generell förmåga i ramverket eller appens beräkning.
- 💡 **Bildvarianter per storlek och läge i `assets.bin`** — [assets.h](Glaze/widgets/include/glaze/assets.h)
  För det som inte är en ritning (foton, logotyper): en bild per storlek och för ljust och mörkt läge, vald när den ritas, som de ritade ikonerna gör av sig själva.
- 💡 🎨 **Ikonernas recept i två nivåer och lägets ton** — [stock_icon.cpp](Glaze/widgets/src/stock_icon.cpp)
  Ett recept för de små storlekarna och ett för de stora, och samma bild några procent nedtonad i mörkt läge och uppljusad i ljust, där bara konturen skiljer. Gäller standardikonerna och appens.
- 🟠 **Windows- och Mac-testet** — [doc/os/](doc/os/)
  Kvar är rader som kräver hårdvara eller handtest: mus, skärmskalning och VS Code på Windows; andra skärm, PC-tangentbord och Intel-Mac på macOS.
  Stängs när `doc/os/windows/TESTS.md` och `doc/os/macos/TESTS.md` är TOMMA.
- 🟢 **Ritytans minneskonto och rumsliga träd** — [canvas/](Glaze/widgets/src/canvas/) · mätt i [RITYTANS_KONTO_OCH_TRAD.md](doc/research/RITYTANS_KONTO_OCH_TRAD.md)
  Ritytans minnesbudget räknar allt den håller (GPU och CPU) och vräker det som inte syns, och ett rumsligt träd per körning ger ritningen det som kan synas i stället för att varje chunk och föremål prövas.
  Kvar: sviten före commit, och Eriks test. Grunden för automatisk detaljnivå — pixelfel som mått, terräng, meshar och punktmoln som ritytan själv delar och förenklar, och instansanrop för former som delar mesh.
- 🟠 **Ritytans renderingstråd** — [canvas/](Glaze/widgets/src/canvas/) · [gfx/](Glaze/gfx/include/gfx/)
  Ritytan ritar i en egen tråd per fönster med egen grafikkontext, och UI-tråden visar den senast färdiga bilden utan att vänta. Scenen lämnas som frusna ögonblicksbilder, appens målare spelas in, och `renderToBitmap` svarar i efterhand.
  Byggt: scen-kontexten i de tre backendarna (Metal och Linux blint), inspelade målare, ögonblicksbilderna, tråden med jobbkö och tre bilder i växling, ritytans trådläge (`GLAZE_CANVAS_THREAD=0` stänger av det), låsen kring bild- och meshdata, förankrade barn efter den visade bildens kamera, grindarna `scene_context_gate` och `canvas_thread_gate`.
  `renderToBitmap(w, h, kamera, done)` svarar i efterhand och ritas av renderingstråden, liksom utskriftens sidor; profileraren räknar trådens faser i bilden där den visas.
  Kvar: Eriks test i en riktig app, och Metal och Linux (blint skrivna — `doc/os/*/TESTS.md`).

## Utseendepasset — UI, Tema och Style, PARKERAT

**Erik 2026-08-21:** *"UI, Tema och Style. Det är ett senare, men jag är hyffsat nöjd med hur det ser ut."* Färger, kontraster, måttabeller och tjocklekar hör HIT — saknade förmågor och saknade komponenter gör det inte. **Blockerar ingenting:** en widget hämtar sina färger ur `c.theme->colors.*` och sina mått ur `c.style->*` oavsett vilka värden som står där. **Referensbilderna hör också hit:** fixturfönster → PNG mot facit kan inte grindas förrän designen är låst — att allt RITAR mäter `pixel_gate` redan.

- 🐛 🎨 **Minimalistic-Neutral: `accent` och `link` fem steg från `textDim`**
  Inaktiverad Link (mörkt) och inaktiverad Progress (båda lägena) ser AKTIVA ut.
  Står som KÄNT i grindens `kKändaFel` — raden är självutlöpande och fäller grinden den dag paletten rättas.
- 🎨 **Paletten: `button` ÄR `panel`**
  `c.button = c.panel;` ⇒ 1.00:1 i **19 av 20 teman**, i Minimalistic också, inte bara Slate.
  Följd: overlay-scrolltummen är osynlig, och `buttonHover`/`button` mäter 1.04–1.11 utan att `contrast_gate` ser det.
- 🎨 **Minimalistics måttabell**
  Familjen ärver Slates tal: `checkSize` 18 = hela knapphöjden, `listRowH` 26 = 44 % högre än knappen.
  Kräver att formen ses renderad — en tätare siffra är annars en ogrindad gissning.

## Fas 2 — GlazeBuilder

**Först när fas 1 är klar** (Erik 2026-08-21). Editorns egen yta — att klicka fram det `.glazeui` redan uttrycker och previewn redan utför. **Listan är ofullständig med flit:** Eriks sparade synpunkter kommer in när fasen börjar.

- 🐛 **Builderns invers → `builder_core`**
  `layout_gate` fall 16 KOPIERAR `dragTo`s formler i stället för att anropa dem ⇒ grön oavsett vad buildern gör.
  `dragTo` bor bara i exet, alltså i canvasens musdragning.
- 💡 **Panelen ska ALLTID visa ALLA properties**
  Även de utan ändringar — annars går de inte att ändra. I dag har en nyplacerad widget inga rader alls.
  VISA är inte SKRIVA: filen är en diff mot default, panelen den fulla vyn.
- 💡 **Collection editor för listinnehåll**
  `Call`-satser visas inte alls, och de är en sekvens av anrop — inte ett värde. WinForms-svaret är `(Collection)` + `…`-knapp.
  EN dialog räcker för grid, listbox, dropdown och menyrad — de delar kollektionsvokabulär.
- 💡 **Placera i containers + dock-UX**
  Placering hamnar i dag alltid på rotnivån, och dockade widgets kan inte flyttas i canvasen. Samma geometri som builderns invers.
- 💡 **Fönsteregenskaper i UI:t**
  Markera canvasen = markera fönstret (titel, storlek, klassnamn, livscykel-events). I dag hårdläses de ur modellen.
- 💡 **Popup som SÄTTER designtemat**
  Designtaggen läses av buildern och extensionen men kan inte skapas i UI:t. Tre dropdowns med demons kaskad.
- 💡 **Ångra/gör om**
  `WindowModel` är värdesemantik, så en undo-stack av hela modellen räcker långt. Läggs på när editorns form slutat ändra sig.
- 💡 **Komponentfacket**
  Komponentfacket under canvasen (WinForms component tray) för `Timer`, `TrayIcon` och egna `glaze::Component`. Grammatiken och previewn finns redan.

## Efter båda

- 📚 **Dokumentationen — för människor och AI-verktyg**
  ALLRA SIST: den beskriver allt det andra, så den kan inte skrivas klart medan resten ändrar form.
  AI-delen är filer verktygen hittar själva — utan dem gissar varje assistent på Qt/wx-mönster. Första versionen finns: [MANUAL.md](MANUAL.md).

---

# v1.0+

Utanför grinden. Inte mindre viktigt — men inte villkor för släppet, och får inte skjuta det framför sig.

- ☁️ **Bindning — UI-variabler**
  `glaze::Int`/`Float`/`String`/`Bool`: en plats, N vyer. Modellen är beslutad, ett vägval kvar.
- ☁️ **Mått i grammatiken — TRE VAL: `em` · `mm` · autostorlek**
  Ett spikat `height = 232` följer inte med vid stilbyte — men mätningen gav TOMRUM och inte brott, och elva av tretton spikade tal i demon är BREDDER.
  **Lutar mot `mm`** (fysisk, läsbar, en ren konstant): `em` är stil-relativ men oläsbar, autostorlek tar bort behovet av talet.
- 💡 **Rotation**
  `rotation` på Widget-basen, fasta steg 0/90/180/270. Pausad — tung.
- 💡 **VS Code-extension: Win/Linux-VSIX + marketplace** — [tools/vscode-glazeui/](tools/vscode-glazeui/)
  Klar på Mac. Kvar: render-verifiering på Windows, linux-x64-VSIX och publicering (kräver publisher-kontot).
- 💡 ♿ **Tillgänglighet — skärmläsarstöd saknas HELT**
  Noll träffar på UIA/NSAccessibility/AT-SPI: en skärmläsare ser ett tomt fönster. Kräver BÅDE ett `Widget`-träd (roll/namn/värde) och en brygga per OS.
  Börjar från noll den dag den tas upp.
- 💡 **Bidi/RTL saknas i textvägen**
  Träffar där de tre backendarna skiljer sig MEST: DirectWrite och Core Text gör bidi och shaping åt en, FreeType inte alls.
  Språkpaketen byter strängar; RTL byter TEXTVÄGEN.

---

# Kanske

Möjligt och genomtänkt, men inte beslutat. Ritytan med mycket stora 3D-modeller på 2–4 GB grafikminne.

- 💡 **16-bitars index för meshar under 65 536 hörn**
  Indexen är alltid 32 bitar i dag (fasaden). Halverar indexdatan; kräver en ändring i alla tre backendarna.
  Vinst: minne och inladdning, knappt bildtakt.
- 💡 **Tätare hörnformat — lägen som 16-bitars heltal relativt meshens låda**
  12–16 byte per hörn mot 20; två nya attributformat i fasaden, avpackningen i modellmatrisen så egna program inte märker det.
  Hake: meshar som delar en kant får olika rutnät — väljs av ramverket efter lådans storlek. Vinst: minne, inladdning, lite bildtakt.
- 💡 **Mesh utan CPU-kopia — eller en LADDARE i stället för data**
  Kopian läses av uppladdningen i ett annat fönster och på en annan enhet, återuppladdningen efter vräkning, ändringsloggen, målaren och `heightAt`.
  Släppt kopia = `Bitmap::setKeepPixels(false)`s regler; en laddare låter ramverket släppa och hämta om själv. Vinst: RAM, inte fart.
- 💡 **Plockning genom en id-buffert på renderingstråden**
  Föremålets och triangelns nummer ritas i ett litet mål kring pekaren, djupet ger punkten: inget sökträd, ingen väntan i UI-tråden, svar i efterhand.
  Ger ytan under pekaren — inte hörn- och kantsnäpp, inte det skymda. Den stora vinsten för klick och inladdning.
- 💡 **Plockning som inte blockerar — bara meshar med sökträd, eller svar i efterhand**
  En flagga i `HitOptions` (ofullständigt svar, sagt i svaret), och `hitTest` med en svarsfunktion där träden byggs i arbetspoolen.
  Sökträden bör räknas i ritytans minnesbudget och kunna vräkas.
- 💡 **Omvänt djup i 3D-målet**
  Nära = 1, långt bort = 0, vänt inuti ramverket så att `DepthTest::Less` fortsätter betyda "närmare vinner"; tar bort flimret i kilometerstora scener.
  Full effekt på D3D11 och Metal, på GL bara med `glClipControl`. Bias-värdena (markdjupet, stående etiketter, punkter) måste mätas om. Ingen fartvinst.
- 💡 **Minnesbudgeten när appen har ett eget urval**
  I dag: appen sätter `setMemoryBudget` högt och väljer själv, annars vräker ramverket det appen tror är inladdat.
  Målet är att appen säger vad som är viktigast och ramverket vräker — kräver laddaren ovan.
