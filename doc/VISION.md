# GlazeUI — Vision

> Det här är **visionen** — riktningen vi bygger mot. Det är **inga hårda regler**
> (de bor i [/CLAUDE.md](../CLAUDE.md)) och **allt kanske inte går att lösa**. När
> visionen krockar med verkligheten vinner *enkelhet* och en produkt som faktiskt
> fungerar.

## Kärnan

GlazeUI ska göra det **så enkelt som möjligt** att bygga riktiga desktop-appar i
**C++** — med känslan av **WinForms / .NET**: dra upp ett fönster i en WYSIWYG-editor,
en medlemsfunktion per event, layouten som genererad C++ (committad, kompilerad —
inget dataformat), logik i C++. Ingen ceremoni, inget pekar-/bygg-krångel.

## Ledstjärna — gör det enkelt för användaren

Allt annat väger mot det här. Konkret:

- **Demo-appen hålls clean** — den är referensen för hur *lite* en app behöver
  innehålla. Växer den i boilerplate har vi misslyckats någonstans.
- **Så mycket som möjligt sköts automatiskt** — kodgenerering, event-koppling,
  build-uppsättning. Användaren ska skriva logik, inte rör.

## Pelare

### 1. WinForms-känsla i C++
Fönster + widgets + events utan ceremoni. Layouten är GENERERAD C++ i tre filer
(`xxx.h` med en GlazeBuilder-region, `xxx.glazeui` genererad, `xxx.cpp` din) —
`initializeComponent()` kopplar events till dina medlemsfunktioner vid
kompilering, du skriver bara handlers (startlogik i `onLoad`, aldrig i ctorn).
**Levererat 2026-07-17** (GUI_PIPELINE-pivoten: inget dataformat alls).

### 2. WYSIWYG-editor — VSCode först, egen IDE sen
- **Steg 1: VSCode-extension** — förhandsvisa fönster-.h direkt i VSCode
  (preview via `cpp_reader` — buildern och verktygen läser bara sin egen
  genererade delmängd, aldrig användarkod; knapp "öppna i GlazeBuilder").
  Lägst tröskel, återanvänder en editor folk redan har. (Finns som 💡 i
  [/TODO.md](../TODO.md).)
- **Längre fram: en egen editor** med inbyggd WYSIWYG + kod-editor + debugging +
  run/make — ett samlat ställe att bygga Glaze-appar. Ambitiöst; kan mycket väl
  visa sig för stort — då stannar vi på extensions-vägen.

### 3. Fri från MS-verktyg — allt på MSYS2 UCRT64
Verktygskedjan (kompilator, build, beroenden) vilar på **öppna MSYS2/UCRT64**, inte
MSVC/Visual Studio. Windows *plattforms-API:er* (D3D11/DirectWrite) används för native
rendering — men själva *toolchainen* är MS-fri, och setup ska funka utan Visual Studio.

### 4. Egna komponenter via en gemensam basklass
Användaren ska kunna skapa egna widgets/komponenter enkelt. Därför bygger **alla**
komponenter — inbyggda som egna — på samma basklass (`glaze::Widget` idag). Ett mönster
att lära sig, förutsägbart att utöka. Att lägga till en egen komponent ska kännas som
att fylla i ett formulär, inte att haka in i ett ramverk.

### 5. Near-automagisk setup med CMake/Ninja
Att sätta upp ett Glaze-projekt ska vara nästan trivialt: FetchContent-vänlig rot +
vanliga källfiler idag (ingen kodgenerering i bygget — fönstrets tre filer läggs som
vilka cpp/h som helst); målet är att så mycket som möjligt "bara funkar" utan
handpåläggning.

## Vad ramverket ska bära

Målet är att **ramverket ska kunna bära alla typerna nedan** — inte att bygga dem alla nu.
Bäraren (grunden) är det vi siktar på; widgetarna är en **önskelista**, inte en
att-göra-lista. Vi plockar en i taget, när den behövs. En widget som inte finns än är inte
"kvar att göra" — den väntar bara på att någon behöver den.

### Finns i dag

- **Bäraren är byggd** (widget-system-strömmen, 2026-07-16): flera fönster, modaler,
  widget-träd, `Panel` (klipp + scroll), capture/grab, bilder (PNG), text-mätning,
  genomskinliga fönster, öppen widget-registrering. De sju punkterna under
  [Vad bäraren måste klara](#vad-bäraren-måste-klara) är levererade.
- **Widgets:** `Label`, `Button` (med bild via assets), `Checkbox`,
  `TextInput`, `Panel` (+ overlay-scrollbars), `Dropdown`, `Scrollbar`,
  `PropertyGrid`.
- **Assets** (GLAZE_ASSETS, 2026-07-20): `assets.bin` — committad
  container som ÄR källan (`.resx`-modellen), eget beroendefritt format,
  `glaze::loadAssets`/`img(lista, nyckel)` + per-fönster-texturcache,
  `glazeassets`-CLI:t och Assets-läget i GlazeBuilder.

### Önskelistan — plockas en i taget

Bäraren är byggd för att kunna hålla allt det här. Ingen post är inplanerad; var och en blir
en egen liten insats när den behövs.

**Kontroller (löv)**
- `Dropdown` ✅ FINNS (GLAZE_ASSETS, 2026-07-20): vallista i ett riktigt
  popup-fönster (sticker ut utanför ägarfönstret, vänder vid skärmkanten)
- `Scrollbar` ✅ FINNS (GLAZE_ASSETS, 2026-07-20): fristående + Panels
  inbyggda overlay-lister (`showScrollbars`) — samma painter-väg
- `PropertyGrid` ✅ FINNS (GLAZE_ASSETS, 2026-07-20): tvåkolumners
  egenskapslista, rader är data, EN hostad editor/picker — driver
  GlazeBuilders egenskapspanel men är generisk
- `Radio` (grupper) · `RichTextEdit`
- `Listbox` — rader/kolumner. **Äger sin egen item-collection** + en för rubrikerna;
  celler kan bära text, bild och checkbox. Items är **data, inte widgets**
  (PropertyGrid är INTE den här — två fasta kolumner ≠ N kolumner/virtualisering)
- `Trackbar`/slider · `ProgressBar` · `DateTimePicker` · `ColorPicker`
- `Timer` — **osynlig kontroll**: ingen yta, ingen hit-test

**Containers**
- **Pages/Tabs** — varje sida är en förälder
- **Splitter** — horisontell/vertikal, drag flyttar gränsen
- **Sliding panels** — fällbara/döljbara ytor

**App-ramen** — allt är paneler med en layout och en look
- **Windowtop/caption** — ✅ FINNS (CAPTION_BARS, 2026-07-19): `TopBar`-widgeten
  med min/max/close, labels och egna widgets, native drag/snap via hit-test-
  kartan ur trädet. **AppImage** (upp till 64×64, ramen växer med den) återstår
- **Menybar** — med **valbar position**, inklusive *inne i captionen*
- **Toolbar** (motorn finns — en `Bar` med `Dock=Top`) · **Statusbar** ✅
  (`statusBar()`) · **Popupmeny** (checkbox- och bildkapabel)

**Dialoger** — egna, ritade av Glaze: `OpenFileDialog`, `SaveFileDialog`. Ev. med ett
öppet lib för filsystemsbiten, men utseendet är vårt.

**Grafik**
- **`Canvas` — EN rityta, inte en 2D- och en 3D-widget.** En scen med kamera: innehållet
  laddas upp till GPU:n i lager, och panorering, zoom, vridning och tilt är bara kameran
  (2D byggt 2026-10-10, TODO 27). 3D är byggt i samma widget på samma grund (TODO 50,
  2026-10-11): djup, MSAA, en fri kamera, meshar, instanser, punktmoln, terräng, egna
  shaders och plockning — rakt uppifrån är det 2D-kartan. Ingen fysik.

### Två sorters förälder

Hela listan ovan är en av två saker:

- **Window** — äger en OS-yta, är trädets rot
- **Panel** — en rektangel i en förälder, kan ha barn, klippa och scrolla

Toolbar, Statusbar, Windowtop och en tab-sida är **paneler**. Popupmeny, modal och
error-rutan är **fönster som ser ut som paneler**. Är Panel gjord ordentligt är Toolbar
en eftermiddag.

### Ett fönstertyp, beteendet är attribut

Det ska **inte** finnas många fönsterklasser. Det finns ett `AppWindow`, och hur det
beter sig är attribut: `isModal`, chrome av/på, ägare, transparens, aktivering, position.
Popup och meny är flaggor, inte en egen typ.

Två verb på fönstret (inte på `App`) — **levererat 2026-07-17**:

- `show()` — visa, **icke-blockerande** (för sido-/popup-fönster medan pumpen redan rullar);
  `hide()` döljer utan att stänga.
- `run()` — visa + **pumpa tills fönstret stängs**, returnerar. Ersatte både gamla
  `app.run(w)` och `showModal()`.

Modaliteten är ett **attribut** (`setModal` — input-filtret), returvärdet är pumpen. De är
två skilda saker: `dlg.setModal(true); auto r = dlg.run(owner);` är en modal dialog.

Färdiga dialoger — `showError()` m.fl. — **ärver `AppWindow`** och sätter attribut +
widgets. Inget specialfall i basen. Det är samtidigt kvittot på ramverket: de inbyggda
dialogerna skrivs i Glaze precis som en användare skulle skriva dem. Måste de fuska har
vi en lucka.

### `App` — en nåbar singleton (aktivitetshanteraren) — levererad 2026-07-17

Loop-ägaren `glaze::App` är en **singleton** (en per process), **publikt nåbar** via
`glaze::app()` så man kan inkludera den och fråga den — en liten inbyggd
aktivitetshanterare:

- `glaze::app().windows()` / `windowCount()` / `find(titel)` — registret över öppna fönster
- `show()`/`hide()` på fönstren man får tag på
- **per-fönster CPU/GPU-tid** — varje fönster mäter sin egen `renderFrame`/GPU-tid, `App`
  läser och visar. Fönstret äger mätningen, App aggregerar. (Mätningen är kvar att bygga —
  registret är kroken.)

`App` skapas **lat vid första fönstret** (Meyers-singleton) och river plattformen i sin
destruktor vid process-slut — ingen `App app;` i main behövs. `main` blir:

```cpp
int main() {
    SplashWindow splash;
    splash.run();           // visar + pumpar tills splashen stängs
    MainWindow win;
    return win.run();       // visar + pumpar tills main stängs
}
```

Det är .NET-modellen: `Application` är statisk och nåbar, man instansierar den aldrig.

### Placering = Anchor + Dock — LEVERERAT (GUI_PIPELINE, 2026-07-17)

Widgets placeras med **`Dock`** (top/bottom/left/right/fill) och **`Anchors`** (hur
widgeten beter sig när föräldern ändrar storlek). Det är också hur en panel placerar
sina barn — så app-ramen faller ut ur en enum: Toolbar = `Dock=Top`, Statusbar =
`Dock=Bottom`.

Implementerat i `Widget::resolve()` (stateless — dock-carving via syskonvandring,
anchors per axel med offsets x/y/right/bottom; dubbelankare = sträckning, inga
ankare = centrerad). Gamla `align` är rivet.

### Vad bäraren måste klara

Sju saker som basen inte hade — **alla sju levererade av widget-system-
strömmen 2026-07-16**; listan står kvar
som facit:

1. **Loop-ägare.** Flera fönster kräver någon ovanför `AppWindow` — idag äger
   `AppWindow::run()` den blockerande loopen, så ingen *kan* äga två fönster.
2. **Widget-träd.** Förälder-relativa koordinater, rekursiv hit-test, klippning,
   scroll-offset i transformen, fokus-vandring djupet-först, damage uppåt genom trädet.
3. **Capture och grab.** Trackbar och splitter behöver **capture** (musen fortsätter gå
   till mig även utanför min rect); menyer behöver **grab** (klick utanför stänger,
   tangenter går till menyn). Saknas helt i dag — och fem punkter i listan kräver det.
4. **Bilder i fasaden.** `drawImage` + PNG-avkodning.
5. **Text-mätning och text-hit-test i fasaden.** Radbrytning, "vilket tecken ligger på
   x?", stilade runs. `drawText` i dag är bara "rita strängen där".
6. **Genomskinliga fönster.** Enda punkten som går ner i backenden. Vi ritar varje pixel
   själva ⇒ hit-testet kan **sampla vår egen alpha** → fri geometri utan region-hack.
7. **Öppen widget-registrering.** Widgeten anmäler sig själv i sin egen cpp, i stället för
   dagens stängda typ-switch i `makeWidget()`. Samma väg för `applyProp` → `setProp()` som
   virtual på basen. Då bor en widget i **sin egen fil** i stället för utspridd över sju
   ställen — bara lookens `drawXxx`/`sizeXxx` ligger kvar utanför, och den kan inte flytta
   (den är per look).

**Egna widgets placeras med C++-kod.** De ärver `Widget`, överrider `draw()` och går förbi
looken — två filer, klart. Att GlazeBuilder inte kan förhandsvisa dem är **accepterat**:
buildern är en färdigbyggd binär som aldrig länkat användarens kod, och C++ har ingen
reflektion. (Plugin-DLL:er eller en builder som byggs per projekt vore vägarna dit — inte
värt priset nu.)

**Därför designas bäraren före arsenalen.** En widget som föds på fel bas skrivs om.

### WinForms/.NET är inspirationen — inte en spec

Vi kopierar **känslan och modellen**, inte katalogen: `sender`/`EventArgs` (hård regel 6),
`Dock`/`Anchor`, ett fönstertyp med `show()`/`showModal()`. Men vi kopierar **inte** .NET:s
widget-katalog eller dess trettio år av bakåtkompatibilitet. Listan ovan är vår — vi ser
hur långt vi klarar oss.

## Vad detta INTE är

- Inte en lista över hårda regler (se [/CLAUDE.md](../CLAUDE.md) "Hårda regler").
- Inte en garanti. Stora delar (egen IDE, debugging, run/make) kan visa sig orimliga
  eller inte värda kostnaden — då prioriterar vi enkelhet och det som levererar värde.
