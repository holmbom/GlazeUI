# CLAUDE.md — GlazeUI

Eget cross-platform GUI-ramverk i C++20 — native D3D11 på Windows, native
Metal på macOS, native Wayland/X11 på Linux. Glaze ritar varenda pixel
självt — widgets, fönster-chrome OCH dialogerna (fil/meddelande/färg är
VÅRA, aldrig OS:ets — en systemdialog spräcker illusionen precis som en
OS-namnlist skulle) — så apparna ser identiska ut på Windows/Mac/Linux.
Förebilden är WinForms:
WYSIWYG-editor (GlazeBuilder), en medlemsfunktion per event, layouten är
GENERERAD C++ som committas och kompileras som vanlig källkod, logiken i C++.

Repo: `https://github.com/holmbom/GlazeUI.git` · allt arbete sker direkt
på **`main`** — inga andra brancher. Commits skrivs på svenska.

**Versionen**: `Glaze/widgets/include/glaze/version.h` är
den ENDA källan — rot-CMakeLists läser talen till `project(VERSION …)` och
`version_gate` mäter att de är lika — och varje version har git-taggen
`vX.Y.Z`. Ändringarna mellan versionerna står kortfattat i
[VERSION.md](VERSION.md) (`**Fix**`/`**New**`, en rad var, ingen komplett
historik). **En ny MAJOR eller MINOR
(1.0.0, 0.2.0) kräver ALLA tester på ALLA tre OS** innan den taggas; en
PATCH (0.1.5) går ut med det OS vi sitter på (se Testerna under
Arbetsflöde).

**[MANUAL.md](MANUAL.md) är manualen för agenter som BYGGER APPAR** med
GlazeUI (den här filen är för den som utvecklar ramverket). Den läses i
bitar: `grep -n "^<!-- BEGIN:" MANUAL.md` ger avsnitten med radnummer, och
varje avsnitt står mellan `<!-- BEGIN:<id> | <alias> … -->` och
`<!-- END:<id> -->`. Ändras ett publikt API ska dess avsnitt följa med i
samma commit (commit-skillen).
Bredvid den står **[UI_GUIDELINES.md](UI_GUIDELINES.md)** — avstånden och
linjeringen i en app (skalan 4 · 8 · 12 · 16 · 24 och varför 8 är golvet:
fokusringen och skuggan ritas utanför kontrollen). Ändras `focusOffset`,
`focusWidth`, skuggorna eller `overdraw` i `style.cpp` ska dess tabell följa med.

Riktningen — det vi bygger mot, inte hårda regler — står i [doc/VISION.md](doc/VISION.md).

## Före v1.00: BRYTANDE ÄNDRINGAR ÄR GRATIS

Fram till v1.00 finns det **inga externa användare** — även om `main` ligger
på GitHub bygger ingen mot den än. Alltså: **byt namn, ändra signaturer, ta bort
det som blev fel — rakt av.** Ingen deprecation, inga alias, inga
kompatibilitetsgrenar, ingen kod vars enda uppgift är att klara det gamla. Det
enda som ska finnas kvar är det som är RÄTT NU. Hittar vi ett bättre namn eller
en bättre form är det bara att skriva om alla anropsställen i samma svep — en
halvvägs migrerad kodbas är sämre än en bruten.

**Vid v1.00 vänder detta.** Då släpps ramverket på GitHub, `main` blir den
publika ytan och andra bygger mot API:t — från och med då kostar varje
namnbyte något, och kompatibilitetstänket måste börja gälla. Det som ska vara
utrensat innan dess måste alltså rensas FÖRE släppet; efteråt är det för sent.

**VAD v1.00 ÄR — nio villkor, inte en känsla** (Erik 2026-08-20, utökat
2026-08-21). Grinden står i [TODO.md](TODO.md) och består av:

1. **Golvet — FÖRMÅGOR som saknas**, alltså det en widget inte KAN göra
   oavsett vilka värden temat och stilen bär. **STÄNGT 2026-08-22**:
   mätkontraktet och det rundade barnklippet är byggda, och stil-relativa
   mått i grammatiken flyttade till v1.0+.
   **UTSEENDET hör INTE hit** (Erik 2026-08-21): färgval, kontraster,
   måttabeller och tjocklekar är ett eget, PARKERAT pass — *"UI, Tema och
   Style. Det är ett senare, men jag är hyffsat nöjd med hur det ser ut."*
   Det blockerar ingenting, för en widget hämtar sina färger ur
   `c.theme->colors.*` och sina mått ur `c.style->*` oavsett vad som står
   där; det är hela poängen med att utseendet är fyrdelat.
2. **Språket** — YTAN varje widget måste bidra till. Det är vårt ansvar och
   inte OS:ets av EXAKT samma skäl som chromet är det: vi ritar varenda
   pixel och våra dialoger är våra, så vi äger varenda sträng.
   **TILLGÄNGLIGHETEN LÅG HÄR OCKSÅ men flyttades till v1.0+** (Erik
   2026-08-23) efter ett rivet försök — se TODO.md:s v1.0+-sektion.
   **SPRÅKET ÄR BYGGT 2026-08-22** (`Glaze/language/` + `glaze/language.h`
   + `glaze/strings.h`, Erik-testat): id är nyckeln och språket
   dimensionen, ramverkets 1–999 är inbäddade i libglaze i **fyra
   KOMPLETTA språk** (sv · en · nb · fi), appens 1000+ kommer ur en
   committad `language.bin`, och `TrText` bär tokenet så ett språkbyte
   slår igenom i hela appen utan att appen rör en widget. Det som återstår
   av posten är översättningarna varje NY komponent för med sig
   (TODO-post 32), och `lang_gate` del 2 fäller bygget om ett id saknas i
   något av de fyra.
3. **Komponenterna ur behovsinventeringen** — hela PRIO-listan byggd och
   Erik-testad. Listan mäter VERKLIG användning i de konsumerande repona, inte
   vad en toolkit borde ha.
4. **Förutsättningarna** — luckor som BLOCKERAR en komponent: sammansatta
   widgets och drag-and-drop.
5. **Alla buggar stängda** — 🐛-listan tom.
6. **Sidopaketen `Threads` och `Networking`** — utanför ramverket, innanför
   grinden.
7. **Testat på Windows OCH macOS** — `doc/os/windows/TESTS.md` och
   `doc/os/macos/TESTS.md` tomma. Ett ramverk vars löfte är "ser likadant ut på
   tre plattformar" kan inte släppas med två oprövade.
8. **GlazeBuilder** (Erik 2026-08-21) — editorn. Innanför grinden, men SIST:
   se fasgränsen nedan.
9. **Dokumentationen — TVÅ sorter** (Erik 2026-08-20): en för **MÄNNISKOR** och
   en för **AI-VERKTYG**, där den senare är filer som Claude, ChatGPT m.fl.
   hittar och läser av sig själva, så en AI snabbt kan bygga appar med
   ramverket. **Sista punkten före släpp** — den beskriver allt det andra, så
   den kan inte skrivas färdig innan resten står.

Allt annat är **v1.0+**, och TODO.md har en egen sektion för det. Skälet att
villkoret är skrivet: en toolkit blir aldrig "klar", så utan en lista hade
v1.00 flyttat sig varje gång något nytt föll in.

**FASGRÄNSEN: GlazeUI FÖRST, GlazeBuilder SEN** (Erik 2026-08-21) — *"nu ska vi
skilja på GlazeBuilder och GlazeUI. GlazeUI först. Den ska funka med kodväg samt
`.glazeui`. När det är klart tar vi GlazeBuilder."* Villkor 1–7 är **fas 1** och
mäts på ramverket; villkor 8 är **fas 2** och mäts på editorn; villkor 9 kommer
efter båda. Gränsen är redan byggd som tre CMake-targets:
**`builder_core`** (grammatiken) och **`builder_preview`** (utförandet) är fas 1
— de är `.glazeui`-vägens maskineri och konsumeras av glaze-render och
VS Code-extensionen lika mycket som av editorn — medan **`GlazeBuilder`-exet**
(canvasen, panelerna) är fas 2. En fas 1-post är klar först när **TRE** saker
gäller: C++-API:t finns · formen går att uttrycka i `.glazeui` · previewn UTFÖR
den. Det är regel 8:s två likvärdiga vägar, gjorda till ett avslutskrav.

**TVÅ TEST avgör var en rad hör hemma**: *blockerar den en v1.0-komponent?*
(⇒ villkor 4) och *kostar den mer efter komponenterna än före?* (⇒ villkor 1
och 2) — det första släppte ensamt igenom fel saker: rundat barnklipp
blockerar ingen komponent men fördyrar fyra, och ett mätkontrakt som ska läggas
om i 19 widget-filer är billigare än i 29. **Att vänta ska inte kosta något —
gör det det är raden inte v1.0+.** Det är hela skälet till att tillgänglighet
och språk går FÖRE komponenterna och inte efter: kostnaden är per widget, och
antalet ska växa med drygt hälften.

**Vad det INTE rör:** hård regel 4 (okänt innanför stängslet bevaras) är inte
bakåtkompatibilitet utan **skydd av användarens data** — buildern får aldrig
kasta rader den inte förstår, oavsett version. Formatets fail-safe står kvar.
Däremot är rena ARV i formatet (t.ex. `title`-aliaset i `setProp`) sådant som
gärna får dö före v1.00.

## Arkitektur — lagren

Varje källkodsmapp har en egen `CLAUDE.md` med filerna, konventionerna och
fallgroparna för just den mappen. Nedan är kartan — följ länkarna för detaljer.
**Regel:** en sådan fil beskriver ENDAST sin egen mapp och hålls under **1000
rader** (taket har höjts i steg, varje gång för att innehållet var motiverat);
håll den levande när mappens filer/roller ändras
(se `/commit`). **Vid 1000 gallrar vi:** då är det inte taket som är fel utan
innehållet som behöver rensas.

| Mapp | Roll | Per-katalog-doc |
|------|------|-----------------|
| `Glaze/assets/` | lib **glaze_assets** — assets.bin-formatet (läsare/packare/verify/export): egen binär container som ÄR källan för appens assets (som `.resx`) och committas; käll-PNG:er committas inte. REN STD, noll beroenden, headless-kompilerbar (CLI:t `apps/glazeassets/` + GlazeBuilder + `glaze_impl` konsumerar). Keys unika PER LISTA; listan bär storleksklassen (logiska px). Sedan 2026-10-11 (PROGRAMIKON) också bildverktyget: en egen PNG-codec (`png.h`), skalaren `resizeRgba` i förmultiplicerad rymd med valregeln `pickIconSource` (`image.h`, används också i drift av `glaze_impl`), och `.ico`/`.icns`-skrivarna (`icon_file.h`) bakom `glazeassets icon`/`embed`; ramverkets egna bilder ligger i den committade `framework_assets.bin`, inbäddad i libglaze som `src/assets_builtin.cpp` | [doc](Glaze/assets/CLAUDE.md) |
| `Glaze/language/` | lib **glaze_language** — language.bin-formatet (läsare/packare/manifest/dump/embed): egen binär container som ÄR källan för ramverkets OCH appens översättningar, committad precis som assets.bin. REN STD, noll beroenden, headless-kompilerbar (CLI:t `apps/glazelanguage/` + `glaze_impl` konsumerar; **`builder_core` får ALDRIG länka den**). Nyckeln är ett NUMERISKT id, språket är formatets yttre dimension ⇒ ett femte språk är ett rent tillägg. **Id 1–999 är RAMVERKETS** (inbäddade i libglaze via `lang_builtin.cpp` — ett bibliotek utan sina egna knapptexter är trasigt, inte osatt), **1000+ appens**. CRC:n täcker HELA filen och kontrolleras FÖRST — två medvetna avvikelser från assets | [doc](Glaze/language/CLAUDE.md) |
| `Glaze/gfx/` | lib **common_gfx** — **render-fasaden** (`include/gfx/`: egna POD-typer + abstrakta `Renderer`/`Window`/`Font`/`Event`) + EN inkompilerad backend: `src/backend_d3d11/` (Windows: Win32-fönster/chrome, D3D11-batcher, DirectWrite-text), `src/backend_metal/` (macOS: AppKit/Metal/Core Text/Image I/O — ObjC++), `src/backend_linux/` (Linux: Wayland OCH X11 i EN backend — runtime-val i `gfx::init`; EGL/GL med samma SDF-shader, FreeType/fontconfig-text, libpng, portal-tema över D-Bus, `gfx::systemLocale()` = OS:ets språkval som BCP-47-tagg; committad wayland-scanner-utdata i `protocols/`) — alla bara OS-komponenter/systembibliotek. Filväljarens två OS-frågor bor också här: `gfx::knownPlaces()` (hem, skrivbord, dokument, hämtade, bilder och volymerna — en LISTA MED SÖKVÄGAR, aldrig en dialog) och `gfx::isHiddenFile()` (Windows attribut, macOS flagga). **Ritytans scen** (CANVAS_2D, 2026-10-10) står BREDVID UI-pipelinen: `gfx::Mat4d` och kameran (`camera.h` — ortografisk vid pitch 0, perspektiv vid tilt, relative-to-center i double), scen-abstraktionen (`scene.h`: `GpuBuffer`, `uploadTransient`, `drawMesh` med ett FAST programval `MeshProgram {Line, Fill, Glyph, Image, Line3, Glyph3, Custom}`, `RenderTarget` med `pushTarget`/`popTarget`, `createImage`/`updateImage`), byggarna och en egen earcut (`geom2d.h`), SDF-texten (`sdf.h` + `Font::rasterGlyph` — sedan CANVAS_TEXT_OCH_BILD EN atlas per FAMILJ med alla stilarna och etiketter i DELAR, `StyledText`), projektiva bildvikter (`ImageVertex::q`, `quadWeights`) och `NullRenderer` för testernas renderare. **Och scenens 3D** (CANVAS_3D, 2026-10-11): render-mål med DJUP (D32F i alla tre) och MSAA (`TargetDesc`, `supportedSamples`), `gfx::RenderState` per ritanrop (djuptest, bias, blandning, gallring — förvalet Off är 2D:s beteende), EGNA PROGRAM i HLSL/GLSL/MSL (`program.h`: `ShaderProgram`/`Renderer::createProgram`, förspelet GlazeFrame och platskonventionen; Line3/Glyph3 är Line och Glyph kompilerade med `GLAZE_POS3`), 3D-vyn `View3D` (+ `toView3D` — kartkameran som 3D-vy), `geom3d.h` (låda, sfär, cylinder, rör, polygonen i ett plan, `buildPolyline3`), ritning UTANFÖR en frame (`beginOffscreen`/`readTarget`) och `gfx::setGpuPreference`/`gpuAdapters`/`setGpuAdapter` (REQ9_GRAFIKKORTET: korten med ett id som håller över omstart, `Renderer::adapterId`; Linux väljer i `gfx::init` med miljövariabler, logiken i `src/gpu_util.h`). Fönstrets IKON sedan 2026-10-11 (PROGRAMIKON): `gfx::Window::setIcon(WindowIcon)` + `iconApplied()` (ramar i RGBA8 med rak alfa, `programFirst` låter programfilens egen ikon vinna; D3D11 `WM_SETICON`, Metal `setApplicationIconImage`, X11 `_NET_WM_ICON`, Wayland `xdg-toplevel-icon-v1` när kompositören har det) och app_id:t `gfx::setAppId`/`appId` (Linux xdg app_id + `WM_CLASS`, förval programfilens namn). **SYSTEMFÄLTET sedan 2026-10-12 (SYSTEMFÄLTET):** app-globala fria funktioner i `gfx/tray.h` (`trayAvailable`, `setTrayIcon`, `removeTrayIcon`, `trayIconRect`, `setTrayTimer`) med ett id som ANROPAREN väljer (1–`kTrayIdMax`) och `EventType::Tray` med windowId 0 — TRE implementationer, inte sex (D3D11 `Shell_NotifyIcon` v4, Metal `NSStatusItem`, Linux en kommenterad dummy), säkra före init och efter shutdown; bredvid dem attributet `WindowDesc::showInTaskbar` och dockan `gfx::setShowInDock`. Och skärmens FYSISKA mm: `gfx::Window::displayMetrics()` (EDID ur registret, `wl_output`, XRandR, `CGDisplayScreenSize` — sex implementationer) med källan Measured/Assumed/Calibrated, och appens kalibrering `gfx::setDisplayCalibration`. **GLYFRESERVEN sedan 2026-10-12 (CANVAS_TEXT_OCH_BILD):** varje `gfx::Font` slår upp en kodpunkt den saknar i OS:ets reservkedja (DirectWrite `MapCharacters`, Core Text `CTFontCreateForString`, fontconfig `FcFontSort`) med EN regel i den interna header-only `src/font_fallback.h`, cachad per kodpunkt för mätningen, UI-atlasen och `rasterGlyph`; `Font::hasGlyph` säger om det blev en riktig glyf. **FÖNSTER OCH TIMER sedan 2026-10-12 (FONSTER_OCH_TIMER):** APPENS timer i `gfx/timer.h` (`startAppTimer`/`stopAppTimer` med 64-bitars körnings-id, `EventType::Timer` med windowId 0 — TRE implementationer: D3D11 ett meddelandefönster med `SetTimer`, Metal `NSTimer` i CommonModes, Linux en timerfd i den gemensamma epollen), skärmarna i placeringsrymden (`gfx::screens()` → `ScreenInfo` med hela ytan, arbetsytan, skalan och primären; tom på Wayland), fönstrets läge (`WindowState`, `WindowDesc::startState` vid FÖRSTA synligheten och `Window::placement()` — normallägets plats och storlek, sex implementationer) och elva pekarformer (`ClosedHand`, `Hidden`, `Wait` sist; den stängda handen är VÅR tvåtonsbild på Windows och X11, `src/cursor_bitmaps.h`). **MUSENS CAPTURE-FÖRLUST** mitt i ett tryck är eventet `EventType::MouseCaptureLost` (DOCKNING_DRAG/REQ2_INMATNING — D3D11 `WM_CAPTURECHANGED` för ett eget tryck, Wayland pekarens `leave` under ett levererat tryck utom till en egen popup; X11 och macOS bär avbrottet med fokusförlusten), och macOS Cmd-släppet levereras förbi `[NSApp sendEvent:]`. Inga backend-typer utanför `src/` | [include](Glaze/gfx/include/gfx/CLAUDE.md) · [src](Glaze/gfx/src/CLAUDE.md) · [d3d11](Glaze/gfx/src/backend_d3d11/CLAUDE.md) · [metal](Glaze/gfx/src/backend_metal/CLAUDE.md) · [linux](Glaze/gfx/src/backend_linux/CLAUDE.md) |
| `Glaze/widgets/` | lib **glaze** — API:t apparna använder: `App` (loop-ägaren — nåbar singleton `glaze::app()`: pump, fönsterregister, modal-filter), `AppWindow` (show/hide/run, livscykel-events onLoad/onShow/onHide/onClose, owner-kaskad, isModal/popup/transparent/**topmost**-attribut), Widget-TRÄDET med `Panel` som container-primitiv och **Dock/Anchors-placering** (+ Bar-flexen), Label/Button (med bild via `ImageSource` — asset eller `Bitmap`, som varje bildbärande widget sedan TRAD_LISTOR_BILDER)/Checkbox/TextInput/**TextArea (det flerradiga fältet sedan 2026-10-08 — en EGEN widget och inte en egenskap på TextInput, så kompositernas fält rörs inte; ärver Panel för remsorna och delar FÄLTET — ytan, markeringen, UTF-8-gränserna och sedan TEXTFALTEN ångra-historiken (`textfield::History`, grupperad efter ord) och redigeringsgenvägarnas regel (Ctrl utan Alt, på macOS Cmd) — med TextInput i den interna `src/text_field.h`; TextInput har dessutom lösenordsläget `setPassword`)**/**MarkdownView (formaterad Markdown-text i en rullande yta sedan 2026-10-14, REQ7_MARKDOWN — egen CommonMark-parser och layout i den interna `src/markdown/`, fet/kursiv/mono genom fönstrets lata, slutna `FontSet` (`Ctx::textFonts`), `onLinkClick` med `handled`)**/Dropdown (popup-fönsterburen lista — popupens rader ÄR en `ListBox`-subklass sedan 2026-08-19)/**ListBox** (enkelval + valfria kryss; rader som DATA i en Panel, ETT tabbstopp)/Scrollbar/PropertyGrid/Separator/Picture/Link (`gfx::openUrl`)/Radio (grupp = syskon med samma `group`, ETT tabbstopp)/Slider/Progress (obestämt läge animerar)/**ToolBar + ToolButton (verktygsfältet sedan 2026-10-07 — en Bar vars barn är RIKTIGA widgets; verktygsknappen är platt, växlingsbar och INTE fokuserbar, så den varken är ett tabbstopp eller stjäl fokus vid klick; med menyposter en RULLGARDIN sedan 2026-10-13 — pilen, trycket öppnar menyn under knappen, `setSplit(true)` delar den, `onDropDown` före öppningen, menyn med systemfältets vokabulär och `{}` som rot; sedan DOCKNING E4b STÅR ett fält vid Left/Right (`Bar::isVertical`), har en EGEN längd och står i en `DockHost` i BAND med ett grepp; sedan DOCKNING_DRAG kan det FLYTA i ett eget fönster med rubrik och fast storlek som aldrig aktiveras)**/**GroupBox (gruppramen sedan 2026-10-07 — rubriken i linjen, autostorlek ur barnen; en Widget och INTE en Panel, för den scrollar aldrig)**/**SpinBox (talfältet sedan 2026-10-07 — den första riktiga KOMPOSITEN: ett ägt `TextInput`-barn + stegaren som DATA, ETT tabbstopp via `focusesChildren` + fokusombudet `focusProxy`; decimaltecknet följer språket)**/**ListView (listvyn sedan 2026-10-07 — kolumnhuvud, rader som DATA, enkel- eller flerval, valfria kryss per rad (REQ2_TRAD_LISTOR — rutan före ikonen, oberoende av valet) och `onActivate`; byggd på den OREGISTRERADE basen `Table`, som äger kolumner, rader och en SJÄLVSORTERANDE visningsordning där indexen förblir appens)**/**Splitter (delaren sedan 2026-10-07 — två rutor och ett dragbart skikt; `paneSize` mäter den FASTA rutan, så layouten är tillståndslös)**/**DockHost + DockPanel (dockningen sedan 2026-10-07 — paneler med rubrik, menyknapp, knappnål och stängknapp; sedan DOCKNING E1 (2026-10-13) en modell KANT → REMSA → CELL → FLIK i den interna `src/dock/`: flikgrupper och staplar (`dockPanel`), infällda paneler som HANDTAG på kanten, min/max per panel och allt annat som HUVUDYTAN; rubrik- och flikdrag med en SIMULERAD släppindikator och hela läget som versionerad sträng `layout()`/`setLayout()` (`v2`); sedan E4b står verktygsfälten i BAND ytterst längs kanterna och dras i sitt grepp; sedan E2 KNAPPNÅLEN — en onålad panel är ett handtag som fäller ut den som OVERLAY ovanpå huvudytan (`Widget::frontChild`), infälld igen när pekaren och fokus lämnat den — och panelerna GLIDER (ett steg per bild i pump-svansen, ingen render-loop) och skikten SNAPPAR; sedan E3 kan en panel FLYTA i ett eget fönster som värdens fönster ÄGER — tillståndet direkt, flytten i pump-svansen (`Widget::transferChild`) och `f=` i strängen; sedan DOCKNING_DRAG (TODO 81) VS:s LEVANDE DRAG — en rubrik, en flik eller ett grepp LOSSAR objektet till sitt fönster, som följer pekaren medan en platshållare visar var det dockas, och släppt utan platshållare flyter det; inte på Wayland, där draget bara visar platshållaren)**/**MenuBar (menyraden sedan 2026-08-19 — ett publikt VÄRDE-FÄLT på AppWindow, i KLIENTYTAN och inte i chromet; rubrikerna är DATA och inte barn, annars ett tabbstopp per meny)** + **menyerna `glaze::Menu`/`MenuItem` (fri datastruktur i katalogrotens `menu.h` — INGEN widget, precis som meddelanderutan; visningen är popup-`AppWindow` i cpp:n)** + **meddelanderutan `glaze::messageBox` (fri funktion i katalogrotens `message_box.h` — INGEN widget: registreras inte, adopteras inte, finns aldrig i `.glazeui`; det interna modala fönstret bor helt i cpp:n som `Dropdown::ListWindow` gör; Enter/Escape är dess standard- och avbrytknapp, som i varje dialog sedan REQ2_DIALOGER)** + **filväljardialogen `glaze::FileDialog` (sedan 2026-10-08 — katalogrotens `file_dialog.h`, INGEN widget av meddelanderutans skäl men en KLASS med egenskaper + `run(ägare)` och inte en fri funktion: många in- och flera utvärden; tre lägen Open/Save/Folder över EN implementation, de kända platserna ur fasaden och katalogläsningen ren `std::filesystem`; OK och Avbryt är standard- och avbrytknapp)** och **FilePicker (filväljarfältet — ett riktigt `TextInput` och en riktig "…"-knapp, två tabbstopp, som öppnar dialogen)** + **färgdialogen `glaze::ColorDialog` och inmatningsrutan `glaze::inputBox` (sedan 2026-10-13, FAS1_B_WIDGETS — katalogrotens `color_dialog.h`/`input_box.h`, INGA widgets: filväljarens klassform respektive meddelanderutans fria funktion, VÅRA dialoger)** + **systemfältets ikon `glaze::TrayIcon` (sedan 2026-10-12 — katalogrotens `tray_icon.h`, INGEN widget av `Menu`s skäl: ikonen sitter i OS:ets fält och är APPENS, inte ett fönsters; bilderna i kollektionsvokabulären, en animation som bara går medan den syns, VÅR meny med `{}`-roten i ett internt värdfönster som tar fokus, och events med IKONEN som sender — `Handler<Args, Sender>`; bredvid den `AppWindow::setShowInTaskbar`, `glaze::setShowInDock` och dold start med `hide()` före `run()`)** + **appens timer `glaze::Timer` (sedan 2026-10-12, FONSTER_OCH_TIMER — katalogrotens `timer.h`: en timer som hör till APPEN, går före första fönstret och levereras i pump-svansen; som `TrayIcon` en ICKE-VISUELL MEDLEM, `glaze::Component` i `component.h`, med registret `GLAZE_REGISTER_COMPONENT` och en previewform som previewn lyder)** + **fönstrets ram, plats och läge (`setChrome`, `setStartPosition`/`setLocation`, `setWindowState`, `geometry()` — läsbar också efter stängning) och pekarna (`Widget::setCursor`/`resetCursor`, `AppWindow::setUseWaitCursor`)** + **ColorPicker (färgväljaren sedan 2026-10-08 — ett fält med en färgruta och en popup med palett, R/G/B-reglage och hexfält; valet gäller direkt)** och **DatePicker (datumväljaren sedan 2026-10-08 — talfältets komposit: ett ISO-fält och en kalenderknapp som öppnar ett månadsrutnät; `std::chrono::year_month_day`, klampat till gränserna vid läsning)** — båda popuperna TAR TANGENTBORDET med fönsterattributet `AppWindow::setPopupTakesKeys` (publikt sedan REQ8 27, så en app bygger sin egen sökruta eller kommandopalett likadant): ägarfönstret för tangenterna dit på alla plattformar + **Canvas (ritytan sedan 2026-10-10, CANVAS_2D — en SCEN MED KAMERA: formerna `glaze::Shape` ligger i LAGER på GPU:n i kollektionsvokabulären, och panorering, zoom, vridning och tilt är bara kameran; `Painter` i `onPaint`/`onPaintOverlay`, mått i px/mm/världsenheter, etikettkollision, gesterna och `hitTest`; sedan 2026-10-12 (CANVAS_TEXT_OCH_BILD) också etiketter i DELAR (`TextSpan` — färg och fet/kursiv per del, ett rit-anrop) och bilden på FYRA FRIA HÖRN (projektiv, strikt konvex). **3D i SAMMA widget och scen sedan 2026-10-11 (CANVAS_3D)**: `Camera3D` (orbit med tumling, förstaperson via `lookFrom`), meshar (`glaze::Mesh`), instanser, punktmoln (`PointCloud`), terräng med drapering (`LayerPlacement::Terrain`), del-vyer (`View`), egna program (`Program`) och `Shape::screenPass`, plockningen (`hitTest(lokal, HitOptions)`, `castRay`, `heightAt`), appens egen interaktion med `onTick`/`onGesture` och `AppWindow::isKeyHeld`, gesterna som en BINDNINGSTABELL (`setBinding`/`setInputMode` — lägena karta, orbit, förstaperson och appens egna; appens globala tabell ärvs rad för rad, REQ9_INMATNING), och `renderToBitmap` — ingen fysik. Typerna bor i `include/glaze/canvas/`, motorn i `src/canvas/` — widgetfilen har ingen `c.r`)** + caption-barerna Bar/TopBar/**StatusBar (ett publikt VÄRDE-FÄLT på AppWindow sedan 2026-08-13 — dold tills `statusBar.show();`)** + **AppIcon (programikonen och PROGRAMMENYN sedan 2026-10-11 — det tredje publika värdefältet `appIcon`, men SYNLIGT som förval: GlazeUI-ikonen, inbäddad i libglaze, när appen inte valt någon; ett rotbarn i CHROME-trädet som TopBar (`isChromeRoot`), vänster på Windows/Linux och höger på macOS, liten i namnlisten eller STOR över namnlist + menyrad; samma bild på OS-fönstret; programmenyn har menyradens vokabulär med den TOMMA stigen `{}` som roten; `setNoIcon` = ingen ikon alls, `hide()` = ingen i fönstret)**/Spacer/CaptionButton (fönsterknapparna är sedan 2026-08-12 ETT **kapselkluster** på alla tre plattformarna — `Shape::Capsule`, höger på Windows/Linux och vänster på Mac. FORMEN är vår och lika överallt; ORDNINGEN följer plattformen — minimera·maximera·stäng på Windows/Linux, **stäng·minimera·maximera på Mac** (Erik-testat 2026-08-13). Gemensamma nämnaren är att stängknappen hör till YTTERKANTEN, inte att ordningen är densamma. `Glyph`/`Circle` lever kvar för äldre filer) — namnlisten ÄR en widget, hit-test-kartan ur trädet trycks ner till backenden (en header + en cpp per widget, i **`include/glaze/widgets/`** resp. **`src/widgets/`** — infrastrukturen `glaze.h`/`app*.h`/`widget.h`/`draw.h`/`style.h`/`theme.h`/`assets.h`/`message_box.h`/`menu.h`/`file_dialog.h`/`color_dialog.h`/`input_box.h`/`input.h`/`tray_icon.h`/`version.h` stannar i katalogroten; widgeten ritar sig SJÄLV med `switch (c.ui)`; `glaze.h` är paraplyet; öppen registrering via `GLAZE_REGISTER_WIDGET`), det FYRDELADE utseendet i tre oberoende axlar + ritandet — **UI** (vilken `draw()`-gren: Glaze/Slate/Minimalistic) / **Theme** (alla färger; flera per UI — Slate har tio, genererade ur en ljushetstrappa och grindade av `tests/contrast_gate.cpp`) / **Style** (alla mått i logiska px, inkl. UI-fontens storlek; VARJE UI äger sina egna — `styleAt(ui, i)`/`defaultStyle(ui)`, i dag en per UI och alla döpta "Regular") / **Draw** (allt ritande, widget-relativa logiska px) — asset-API:t (`glaze::loadAssets`/`img` — fria funktioner, handtag + per-fönster-texturcache) — och **SPRÅKPAKETET sedan 2026-08-22** (`glaze/language.h` + `glaze/strings.h`): `TrText` bär ANTINGEN en literal ELLER ett översättnings-id och cachar sin upplösning mot en generationsräknare, `glaze::tr(id)` är uppslaget, och `setText`/`setTitle`/`MenuItem::text` tar den — `text()`/`title()` behåller sin gamla `const std::string&`-form och returnerar den UPPLÖSTA texten, medan `textToken()`/`titleToken()` bär IDENTITETEN. Menystigen är en **segmenterad `MenuPath`** (`/`-formen utgår) som matchas TOKEN mot token; genvägen är kluven i KANONISKT namn (serialisering) och ÖVERSATT namn (visning) | [include](Glaze/widgets/include/glaze/CLAUDE.md) · [widgets](Glaze/widgets/include/glaze/widgets/CLAUDE.md) · [canvas](Glaze/widgets/include/glaze/canvas/CLAUDE.md) · [src](Glaze/widgets/src/CLAUDE.md) · [src/widgets](Glaze/widgets/src/widgets/CLAUDE.md) · [src/canvas](Glaze/widgets/src/canvas/CLAUDE.md) · [src/dock](Glaze/widgets/src/dock/CLAUDE.md) · [src/markdown](Glaze/widgets/src/markdown/CLAUDE.md) |
| `Threads/` | **SIDOPAKETET Threads** (TODO 29) — UTANFÖR ramverket, `glaze` länkar det aldrig. Lib **glaze_threads** (ren std): `Worker` — arbete i en pooltråd, svaret till en fortsättning i kontexten, ALDRIG efter ägaren och ALDRIG för en avbruten körning; `Pool` (fast trådantal, FIFO); `Result<T>` med `std::error_code`. Lib **glaze_threads_ui**: UI-bryggan = `glaze::app().post` som `Context`, självregistrerad som default när den länkas (WHOLE_ARCHIVE i statiskt läge, ankaret `ui_anchor.cpp` i delat). Bygger fristående | [doc](Threads/CLAUDE.md) |
| `Networking/` | **SIDOPAKETET Networking** (TODO 30) — lib **glaze_net**, beror på glaze_threads men aldrig på glaze. TCP/UDP/DNS/PING plattformsdelat som `gfx::` (fasad + ett OS-lager: Winsock / BSD-sockets), HTTP(S) genom OS:ETS stack (WinHTTP / NSURLSession / libcurl — "uppfinn inget"); blockerande med tak + `stop_token`, asynkront via en `Worker`. Bygger fristående; `GLAZE_NETWORKING=OFF` stänger av det | [doc](Networking/CLAUDE.md) |
| `Terminal/` | **SIDOPAKETET Terminal** (REQ9 3) — lib **glaze_terminal** (ren std + OS: pty:n — ConPTY via GetProcAddress / `posix_openpt` utan forkpty —, VT-tolken (Williams), skärmmodellen med historik och grafemkluster, sessionen med tre trådar, inkorgens tak och rivningsordningen; headless) och **glaze_terminal_ui** (`glaze::Terminal : Panel`, tangentkodningen, färgschemana härledda ur temat — WHOLE_ARCHIVE/ankaret som Threads). `glaze` länkar det aldrig; `GLAZE_TERMINAL=OFF` stänger av det, och då blir typen en stand-in i previewn | [doc](Terminal/CLAUDE.md) |
| `OpenXR/` | **SIDOPAKETET OpenXR** — lib **glaze_openxr** (ren std: OpenXR-rum → Glaze-världen, förankring, kameravinklar, symmetrisk projektion; headless) och **glaze_openxr_ui** (`glaze::openxr::Session`: visar en `Canvas` med `Camera3D` i ett PC-VR-headset — XR-tråd, egen D3D11-enhet på runtimens LUID, `renderToBitmap` per öga → sRGB-swapchain, kontroller, stråle → `castRay`). Bara Windows. `glaze` länkar det aldrig; `GLAZE_OPENXR` är AV som förval, för paketet hämtar OpenXR-loadern med FetchContent | [doc](OpenXR/CLAUDE.md) |
| `apps/demo/` | exempel-app + referensmönster för nya fönster (tre-fils-modellen) | [src](apps/demo/src/CLAUDE.md) |
| `apps/GlazeBuilder/` | WYSIWYG-editorn, byggd med Glaze själv: `WindowModel` + `cpp_reader`/`cpp_writer` (splasa) + stand-in för egna typer. TVÅ delbara lib: **`builder_core`** (ren std — `window_model`/`cpp_reader`/`cpp_writer`) och **`builder_preview`** (det som beror på BÅDE glaze och builder_core — `apply_props`/`palette`/`standin_widget`/`design_theme`/`window_tree` och sedan FONSTER_OCH_TIMER `component_tree`, som skapar och utför de icke-visuella medlemmarna); glaze-render länkar båda | [src](apps/GlazeBuilder/src/CLAUDE.md) |
| `apps/glaze-render/` | CLI fönster-triplett → PNG: läser via `builder_core`, bygger trädet ur registret (okänd typ → stand-in), EN frame i DOLT fönster → `captureFrame` → `gfx::encodePng`. WYSIWYG per konstruktion (samma backend, native scale, chrome med). Stdout-raden är KONTRAKT — VS Code-extensionen parsar den | [doc](apps/glaze-render/CLAUDE.md) |
| `tools/vscode-glazeui/` | VS Code-extensionen (fristående TypeScript-produkt): `.glazeui` → renderad bild i webview via BUNTAD generisk glaze-render (repot krävs ej; egna projekt-widgets → stand-in), tre toggle-lägen (bild/text/text+sidopreview), Öppna-i-GlazeBuilder (GlazeBuilder buntas med som fallback; `builderPath`/projektbygge vinner), per-plattforms-VSIX | [doc](tools/vscode-glazeui/CLAUDE.md) |

Beroenderiktning: appar → `glaze` → `common_gfx` (fasad) → backend.
Sidopaketen står BREDVID: `glaze_net` → `glaze_threads` ← `glaze_threads_ui`
→ `glaze` ← `glaze_terminal_ui` → `glaze_terminal`, `glaze` ← `glaze_openxr_ui`
→ `glaze_openxr`, och `glaze` länkar aldrig något av dem. **DELAT LÄGE** (`-DGLAZE_SHARED=ON`, TODO 73 — statiskt är
förval): libglaze är `glaze_impl` SJÄLV som DLL/.so/.dylib, med `common_gfx`
som OBJECT-bibliotek inlänkat, och varje sidopaketsbibliotek blir en egen DLL —
EN instans av ramverket per process, delad med de DLL:er programmet laddar. `glaze` är INTERFACE i båda lägena;
`glaze_runtime_dlls(<target>)` ([cmake/GlazeShared.cmake](cmake/GlazeShared.cmake))
lägger DLL:erna bredvid programmet på Windows. Ingen versionskontroll mellan
program och DLL (Eriks beslut): båda byggs mot samma GlazeUI.
Backendvalet görs i byggtid (`GLAZE_BACKEND`: auto/d3d11/metal/linux;
auto = d3d11 på Windows, metal på macOS, **linux** på Linux; okänt värde
stoppas med `FATAL_ERROR` i rot-CMakeLists — aldrig en tyst trasig build).
**linux-backenden är Linux-ENDAST** — Windows/macOS har sina native
backends (CMake `FATAL_ERROR` om man försöker).
`glaze` och apparna ser ALDRIG backend-typer — bara `gfx::`-fasaden.
Appar inkluderar aldrig
`gfx/` direkt (undantag: `gfx::Chrome` m.fl. via glaze.h).
Roten är FetchContent-vänlig: apps/ byggs bara när `PROJECT_IS_TOP_LEVEL`.
**Ingen kodgenerering i byggtid** — bygget är bara en kompilering
(GlazeBuilder genererar när den sparar, inte när det byggs).

## Bygga & köra (Windows: MSYS2 UCRT64 · macOS: Apple clang + Homebrew-cmake/ninja)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/apps/demo/demo.exe                     # Windows
./build/apps/demo/demo.app/Contents/MacOS/demo # macOS (MACOSX_BUNDLE)
./build/apps/GlazeBuilder/GlazeBuilder.exe apps/demo/src/start_window.h

# Grindarna (tests/) byggs bara på begäran — GLAZE_TESTS är av som förval,
# så Launch-listan och vardagsbygget slipper ~55 testprogram. Bara före commit:
cmake -S . -B build-test -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGLAZE_TESTS=ON
cmake --build build-test && ctest --test-dir build-test --output-on-failure

# DELAT LÄGE (GLAZE_SHARED) — samma svit mot DLL:erna, plus shared_gate. Före
# commits som rör byggsystemet och före varje MINOR/MAJOR (TESTERNA nedan):
cmake -S . -B build-shared -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGLAZE_SHARED=ON -DGLAZE_TESTS=ON
cmake --build build-shared && ctest --test-dir build-shared --output-on-failure
```

Windows- och Mac-byggena är beroendefria (D3D11/DXGI/DirectWrite resp.
Metal/AppKit/Core Text/Image I/O = OS-komponenter; HLSL/MSL kompileras i
runtime). Linux-backenden bygger mot systembiblioteken varje skrivbord
redan har (pkg-config: wayland-client/-egl/-cursor, xkbcommon, x11,
xrandr (skärmens mm, CANVAS_2D E7), xi ≥ 1.8 (XInput 2 — styrplattan på
X11, TODO 53), egl/gl, fontconfig, freetype2, libpng, dbus-1 — INGEN
FetchContent;
saknas de är det ett riktigt fel med paketlista i felmeddelandet).
Röktest: `timeout 3 <exe>` — exit 124 = fönstret
levde tills timeout (saknas timeout(1): `perl -e 'alarm 3; exec @ARGV'
<exe>` — exit 142 = samma sak). OBS: apparna är WIN32-subsystem →
`gfx::log` syns inte i Windows-konsolen (debug-utgången); Metal loggar
till unified log + stderr, Linux till stderr.

## Tre-fils-modellen (kärnkonceptet)

Ett fönster = tre filer, alla committade, alla kompileras som vanlig C++:

```
start_window.h       EN klass med EN #pragma region GlazeBuilder — regionen
                     (ctor-deklaration + widget-VÄRDE-medlemmar +
                     initializeComponent-deklaration) ägs av GlazeBuilder;
                     ALLT utanför regionen är användarens (handlers/fält).
start_window.glazeui 100 % GENERERAD (C++): ctor { initializeComponent(); } +
                     initializeComponent() — titel/storlek, fältsättningar,
                     event-koppling (widget- OCH livscykel-events),
                     trädbygge via adopt (adopt-ordningen ÄR z-ordningen).
                     Egen extension (inte .cpp) så buildern associerar
                     tripletten och VSCode nästlar ihop den — kompileras
                     som C++ via glaze_add_window (LANGUAGE CXX).
start_window.cpp     100 % användarens — handler-kroppar.
```

GlazeBuilder genererar vid REDIGERING (Ctrl+S) och **splasar**: skriver om
GlazeBuilder-regionen fritt, klistrar allt utanför byte för byte, appendar
nya handler-deklarationer efter `#pragma endregion` + stubbar i `.cpp`.
Buildern läser bara tillbaka sin egen strikt regelbundna delmängd
(`cpp_reader`-grammatiken) — användarkod parsas aldrig. Skadat stängsel ⇒
fail-safe: vägra + visa koden; filen är riktig C++ och kompilerar ändå.

Fönstren är **RUNDADE** (`Style::windowRadius`): hörnen är bortklippta, inte
målade, så ytan begärs med per-pixel-alfa medan chromet behålls — i dag bara
på Linux (macOS rundar själv, Windows är ett eget pass). Radien är HÄRLEDD ur
kapseln så klustrets ände och fönsterhörnet blir koncentriska, och den är 0
när fönstret är maximerat.

`.glazeui` kan dessutom bära en **designrad** — `// glazeui-design: ui=…
theme=… style=…` — som INTE kompileras (den är en kommentar) och bara läses
av verktygen: GlazeBuilder och glaze-render/VS Code-previewn sätter fönstrets
utseende app-globalt ur den.
**Taggen beskriver RITBORDET, inte drifttillståndet** (APP_API C3): appens
egna `setUI`/`setTheme` är APPENS val i drift — en app får byta utseende i
en meny precis som demon gör — så att previewn visar Slate medan appen kör
Glaze är en normal möjlighet och inte en drift. En grind som mäter
avvikelsen går inte att bygga: appens setUI står i ANVÄNDARKOD, som aldrig
parsas, och byggtidsgenerering är förbjuden. Kodgenererad `setUI` är
uttryckligen FÖRKASTAD (det vore per-fönster-tema bakvägen).

Placeringen är **Dock/Anchors** (WinForms): `dock` (Left/Top/Right/Bottom/
Fill — dockade barn skär remsor ur förälderns childArea i barnordning, och
remsorna skär **även för de ICKE-dockade syskonen**),
`anchors` {left,top,right,bottom} + offsets `x`/`y`/`right`/`bottom`
(logiska px; dubbelankare = sträckning, inget ankare = centrerad + offset).

**SYSKONANKARE** (2026-08-19) ligger BREDVID, per kant:
`knapp8.anchorLeft = {"knapp7", 8};` betyder *"min vänsterkant fäster 8 px
från knapp7"*, alltså knapp8 till HÖGER om knapp7 —
`anchorLeft`/`anchorTop`/`anchorRight`/`anchorBottom` med KANT-semantik,
riktningen följer av VILKET fält man sätter. Skälet är **stilbytet**: ett
spikat `x = 210` uttrycker en POSITION och är bara sann i EN Style (Slates
knapphöjd är 26.7 px, Minimalistics 18), och felet KASKADERAR åt höger; ett
syskonankare uttrycker RELATIONEN och härleds ur grannens FAKTISKA,
stil-mätta rect vid varje `resolve()`. En kant är **bunden** av ett
syskon- ELLER ett förälderankare, och två bundna kanter på samma axel
sträcker precis som `anchors.left && anchors.right` gör — så
`anchorLeft` + `anchors.right` är "sträckt mellan ett syskon och
fönsterkanten". Prioriteten: `placesChildren` > `dock` > (syskon-/
förälderankare per kant).

Tre saker är kontrakt och inte detaljer: **`to` är målets `name`**, så
HANDSKRIVEN KOD MÅSTE SÄTTA `name` PÅ MÅLET (GlazeBuilder emitterar raden
på VARJE nod — se namnstycket nedan); **bara TIDIGARE syskon** kan träffas,
vilket gör cykler OUTTRYCKBARA i stället för upptäckta (en framåtreferens,
en självreferens och ett okänt namn ger alla samma tysta fallback på
förälderankaret/x/y); och **inne i en `Bar` ignoreras ankaret**, som dock
och anchors redan gör. Grinden är `tests/layout_gate.cpp`.

**DOCKADE REMSOR SKÄR FÖR ALLA SYSKON** (2026-08-19). Ett icke-dockat barn
bor i "resten", precis som `Dock::Fill`: dess yta är förälderns childArea
minus ALLA SYNLIGA dockade syskons remsor (`Widget::carveSiblingDocks`) —
annars lägger det sig över en Top-dockad menyrad.
Dolda remsor skär ingenting (samma regel som `chromeInsetH`), `Fill`
förbrukar ingen tjocklek, och `TopBar` står UTANFÖR flödet
(`inDockFlow() == false`) eftersom den resolvas mot chrome-ctx:t och inte
mot innehållsytan. Följden för `contentSize()`: en DOCKAD widgets
remstjocklek får inte bero på `c.area` längs skäraxeln — kontraktet står
vid `contentSize` i `widget.h`. Ett beroende av TVÄRaxeln (en Top-remsa vars
höjd beror på bredden) går sedan TEXTFALTEN genom kroken
`Widget::heightForWidth`, frågad med remsans bredd — inte genom `c.area` —
och samma krok ger en STRÄCKT widget höjden för den bredd layouten gav (en
radbruten `Label` växer med fönstret). `Widget::dockedSize` är EN fråga för
både dock-uppspelningen och syskonens carving. Grind: `layout_gate` fall 20.

**`Widget::name` ÄR OBJEKTETS NAMN, speglat** (2026-08-19; hette `id`).
Det är inte ett fält man sätter separat utan samma identitet som
C++-medlemsnamnet, gjord synlig för runtime: `glaze::Button Button1;` +
`Button1.name = "Button1";`. GlazeBuilder emitterar raden på VARJE nod
(auto-namnet är `Button1`/`Button2`, klassnamnets form) och validerar den
vid öppning: exakt en rad med värdet == nodnamnet är kanoniskt, ingen rad
migreras in, och en AVVIKANDE eller dubblerad rad ger FAIL-SAFE — den är
tvetydig i identitetsmodellen, och regel 4 säger vägra i stället för att
gissa. Döper man om noden byts BÅDE medlemsnamnet och literalvärdet;
användarens `.cpp` kan då sluta kompilera, och det är accepterat (samma sak
som i C#). Handkodade fönster sätter raden själva —
C++ har ingen reflektion över medlemsnamn. **Inbyggda fönstermedlemmar**
(`menuBar`/`statusBar`) får INGEN rad i `.glazeui` men får sina namn av
`AppWindow`-konstruktorn, så ett handkodat ankare mot dem träffar.

App-mönstret (se apps/demo — följ det exakt vid nya fönster):

```cpp
class StartWindow : public glaze::AppWindow {
#pragma region GlazeBuilder             // genererad — rör inte
public:
    StartWindow();                      // = { initializeComponent(); }
private:
    glaze::Button okBtn;                // värde-medlemmar (lånad adoption)
    void initializeComponent();
#pragma endregion

    // Allt härunder är användarens — buildern appendar bara handlers.
    void onLoad(glaze::Widget* sender, glaze::EventArgs& e);   // startlogik
    void okBtnClicked(glaze::Widget* sender, glaze::ClickArgs& e);
    int  clicks_ = 0;
};

int main() {
    StartWindow win;
    return win.run();   // visar + pumpar tills win stängts (App skapas lat)
}
```

`glaze::App` är en nåbar Meyers-singleton (`glaze::app()` — windows()/
windowCount()/find()); ingen `App app;` i main. Fler fönster:
`annat.show(*this)` = ÄGT (kaskad-stängs med ägaren, barnen först);
modala dialoger: `setModal(true)` i dialogens ctor + `dlg.run(*this)` →
`DialogResult`, som knapparna bär (`setDialogResult`, med fönstrets
`setAcceptButton`/`setCancelButton`). `close()` verkställs uppskjutet av App i pump-svansen —
efter onClose-vetot (`e.cancel = true` stoppar HELA kaskaden; barnen frågas
djupast först). `owner()` är ägarskap (livstid/Z-ordning) och bär ALDRIG
koordinater. Startlogik bor i `onLoad`/`onShow` — aldrig i konstruktorn
(den är genererad plumbing). Raderad handler-funktion = kompileringsfel
(avsiktligt — ingen runtime-magi, ingen idiotsäkring).

CMake: fönstrets källfiler läggs i appens `add_executable` via
**`glaze_add_window`** ([cmake/GlazeWindow.cmake](cmake/GlazeWindow.cmake)) —
den lägger till `<bas>.cpp` + `<bas>.glazeui` och sätter `LANGUAGE CXX` på
`.glazeui` (genererad layout = C++, bara egen extension). Buildern rör
aldrig CMakeLists; "nytt fönster" påminner i statusraden.

## Fönstrets storlek, tillstånd och tangenter (APP_API + APP_API_2, 2026-08-13)

- **Storleken sätts i INNEHÅLLSYTAN:** `AppWindow::setContentSize(w, h)`
  räknar baklänges genom det chrome fönstret FAKTISKT har (namnlist,
  statusrad, `Chrome::Native`/`None`) — appen ska aldrig behöva kopiera in
  `Style::barHeight`/`statusBarH` i sin egen kod, för varje sådant tal är
  en plats där Style-axeln tyst slutar fungera. Upplösningen är
  UPPSKJUTEN och räknas om vid `show()`, vid UI-/stilbyte och när
  statusraden visas/döljs. `setClientSize` finns kvar; den som sätts sist
  gäller.
- **Min/max-storlek:** `setMinimumSize`/`setMaximumSize` (logiska px, 0 =
  obegränsat) går hela vägen till OS-fönstret via ren virtuella
  `gfx::Window::setSizeLimits`. `min == max` betyder "ej ändringsbar" utan
  ett eget attribut — och `isFixedSize()` är EN sanning som både backendens
  stilmaskning och `pushHitRegions` läser, så chromet aldrig visar en
  resize-pekare på ett fönster OS:et vägrar resiza. (`setMaximizeBox` gör
  INTE det: den döljer bara CaptionButton-WIDGETEN.) I INNEHÅLLSMÅTT (REQ8
  21): `setMinimumContentSize`/`setMaximumContentSize`/`setFixedContentSize`.
- **`.glazeui` kan uttrycka TILLSTÅND** sedan grammatiken fick
  **metodanropssatsen** `<id>.<metod>(<argument>);` — `statusBar.show()`,
  `btn.disable()`, `prog.setIndeterminate(true)`. Formen behövdes för att
  `.glazeui` är riktig C++ som KOMPILERAS: `visible_` är privat och
  settern är den som INVALIDERAR (regel 1), så en fältrad var aldrig ett
  alternativ. Okänt METODNAMN bevaras opakt och ignoreras vid ritning,
  precis som ett okänt fält (regel 4) — ALDRIG fail-safe.
- **NAKNA FÖNSTERANROP** `<metod>(<argument>);` (APP_API_2 A1,
  2026-08-13): `setContentSize(800, 452);`, `setMinimumSize(640, 400);`,
  `setControlBox(false);`, `setModal(true);`, `setAcceptButton(okBtn);` … —
  en FORM och inte en lista, med samma regel 4-behandling som ett
  metodanrop: känt namn utförs (`bld::applyWindowCall`), okänt bevaras
  ordagrant och loggas.
- **SETTER-FÄLTEN emitteras som ANROP** (APP_API_2 B2): widgetarnas
  tillstånd är privat med `setX()`/`x()` sedan 2026-08-13, så writern
  skriver `okBtn.setText("Tryck här");` och readern normaliserar tillbaka
  till modellens fältvokabulär. LAYOUT-fälten (`x`/`y`/`width`/`height`/
  `right`/`bottom`/`dock`/`anchors`/`tabIndex`/`name`) är kvar som RÅA
  fältrader — kontraktet står i `widget.h`. Omskrivningen sker BARA för
  `glaze::`-typer: en egen widget med ett publikt fält round-trippar i sin
  egen form.
- **`gfx::Key` är LEVEL-0-SYMBOLEN** i användarens aktiva layout (den
  symbol tangenten ger UTAN modifierare) — kontraktet står vid `enum class
  Key` i `gfx/events.h` och måste läsas innan man binder en genväg. Tecken
  kommer via `EventType::TextInput`; `Key` är för KOMMANDON.
  **NUMPADEN är PLATTFORMSBEROENDE och det är ett BESLUT** (APP_API_2 B4):
  faktiskt-läge-regeln gäller **Windows/Linux** — NumLock på ⇒ siffran
  (`KP_N1`), NumLock av ⇒ navigationsrollen som EGEN post (`KP_End`,
  `KP_Home`, `KP_PageUp`, `KP_Insert`, `KP_Delete`, `KP_Begin` för
  mitttangenten …; TODO-post 31, Eriks val: utöka fasaden) — medan
  **macOS alltid levererar siffrorna** (`KP_N1`, aldrig `KP_End`): macOS
  har inget NumLock-begrepp att följa (`NSEvent` bär ingen sådan flagga).
  De dedikerade tangenterna (`End`, `PageUp`, `Insert` …) är sina egna
  poster överallt. **`AppWindow` VIKER `KP_End` till `End`**
  (`gfx::navigationKey`) innan menyns tangentkrok, grab-toppen och den
  fokuserade widgeten får tangenten, så varje widget navigerar med numpaden
  utan en egen gren — men appens `event()`-krok och ACCELERATORERNA ser den
  OVIKTA posten, för där är skillnaden poängen: en app som binder numpadens
  navigering binder både `End` och `KP_End`. Hela tabellen står vid
  `enum class Key`; grind: `tests/numpad_gate.cpp`.
- **MENYERNA (MENYER, 2026-08-19)** — `glaze::Menu` är REN DATA
  (`MenuItem`: text, genväg, kryss/radio, separator, `submenu`, och en
  **`Handler<ClickArgs> onClick` PER POST** — ingen tag-tabell, ingen
  nästlad pump). Visningen är en intern `MenuPresentation` som tar en
  **SNAPSHOT** och når posterna via **INDEX-STIGAR, aldrig via pekare**: en
  `push_back` i en `submenu`-vektor reallokerar under en öppen meny.
  **AKTIVERINGEN ÄR EN VÄG** — klick, Enter och accelerator går alla genom
  `Menu::activatePath(stig)`, och ordningen är bindande: tillståndet ändras
  FÖRST, stängningen köas FÖRE handlern (annars stänger den gamla kedjans
  svans en meny handlern just öppnat) och handlern körs genom en KOPIA av
  handtaget, som delar det anropsbara (widget.h).
  Högerklicket är eventet **`onContextMenu`** med ROUTED LOOKUP: första
  förfader med satt handler anropas och ÄR `sender` (regel 6), medan den
  träffade widgeten ligger i `e.source` och FÅR vara nullptr (tom
  fönsteryta — `widgetAt` svarar inte med roten där). **`grabInput()` är
  INTE fokus:** menyn stjäl aldrig fokus, den grabbar input, så en
  `TextInput`s fokusring och caret ligger kvar. Menyn kapas till
  `Style::menuMaxRows` FÖRE `show()` — Wayland-positioneraren har varken
  `RESIZE_Y` eller `SLIDE_Y`, så en för hög popup blir aldrig en mindre
  viewport. ACCELERATORERNA frågas i `handleEvent` EFTER att fokuserad
  widget tackat nej och FÖRE Tab: en `TextInput`s Ctrl+A/C/V går till
  fältet, aldrig till menyn. Mellan de två frågas den fokuserades FÖRFÄDER
  (`Widget::descendantKeyDown`, närmast först) — flikkontrollens Ctrl+Tab
  inifrån en sida; fokus närmast vinner. Efter dem, sist före Tab, kommer
  fönstrets standard- och avbrytknapp.
- **PROGRAMIKONEN** (PROGRAMIKON, 2026-10-11): `appIcon` är ett publikt
  värdefält som `menuBar`/`statusBar` men SYNLIGT som förval, och samma
  bild går till OS-fönstret (`gfx::Window::setIcon`, ramar per
  `glzassets::kIconSizes` ur `src/window_icon.cpp`). TRE tillstånd:
  `isNoIcon()` ⇒ ingen (ingen plats, tom ikonlista) · giltig `icon()` ⇒
  appens · annars FÖRVALET (ramverkets inbäddade bilder, `programFirst`).
  `hide()` är ortogonalt. Platsen är TILLSTÅNDSLÖS — `reservedWidth(c,
  Row)` är den enda sanningen, och namnlistens motvikt, menyradens
  `titleStart` och TopBars hårlinjeluckor läser den. Programfilens ikon är
  en uttrycklig `glaze_app_icon(<target>)` i appens CMakeLists
  ([cmake/GlazeAppIcon.cmake](cmake/GlazeAppIcon.cmake)) — windres är en
  kompilering, inte en kodgenerering.
- **SYSTEMFÄLTET, AKTIVITETSFÄLTET OCH DOLD START** (SYSTEMFÄLTET,
  2026-10-12): `glaze::TrayIcon` är appens ikon vid klockan (Windows) eller
  i menyraden (macOS; Linux svarar `isSupported() == false`) — ingen widget
  och inget fält på `AppWindow`, för ikonen är APPENS. Dess händelser kommer
  som `gfx::EventType::Tray` med windowId 0 och routas av App till
  registret (`src/tray_registry.h`); medan en modal är öppen lyfts den i
  stället för att klicken fyras. Menyn är vår, i ett internt värdfönster
  (`TrayHostWindow`) som TAR FOKUS — dess fokusförlust stänger menyn vid
  ett klick utanför — och öppnas i pump-svansen. Aktivitetsfältet är ett
  ATTRIBUT (`setShowInTaskbar` → `WindowDesc::showInTaskbar`, före
  `show()`), dockan app-global (`glaze::setShowInDock`, före första
  fönstret): två begrepp, två funktioner. **`hide()` FÖRE `show()`/`run()`
  öppnar fönstret DOLT** (`desc.hidden`): `onLoad` fyras, `onShow` först när
  det visas, och en modal startar aldrig dold. "Stäng till fältet" är
  `onClose`-vetot + `hide()` för `e.reason == CloseReason::UserClosing` (ett
  appavslut är `AppQuit`, TODO 72), "Avsluta" en egen flagga + `close()`.
- **APPENS TIMER** (FONSTER_OCH_TIMER, 2026-10-12): `glaze::Timer`
  (`setInterval` med golvet 10 ms, `setSingleShot`, `start`/`stop`,
  `onTick` med TIMERN som sender) hör till APPEN: `start()` skapar App lat,
  fasadens tick (`gfx::startAppTimer`, windowId 0) ANTECKNAS av
  `App::dispatch` och LEVERERAS i pump-svansen efter de postade
  funktionerna — förfaller den flera gånger innan svansen hinner fram blir
  det EN tick. Varje `start()` får ett nytt 64-bitars körnings-id och varje
  förfallen körning sin stämpel, så en stoppad, omstartad, riven eller redan
  levererad tick aldrig levereras (också genom en modal i en handler, som
  pumpar nästlat). En en-gångs-timer är stoppad när handlern körs.
- **RAMEN, STARTPLATSEN OCH GEOMETRIN** (FONSTER_OCH_TIMER): `setChrome`
  är konstruktorns fjärde argument som platt sättare (före `show()`; efter
  loggas den och gäller nästa öppning) — namnlisten byggs LATT och läggs
  FÖRST i roten. `setStartPosition(Default | CenterScreen | CenterOwner |
  Manual)` och `setLocation(x, y)` räknas i glaze ur `gfx::screens()`
  (`src/window_placement.h`, ren och grindad): LOGISKA px är
  placeringsrymden delad med PRIMÄRSKÄRMENS skala, storleken × målskärmens,
  klämt in i arbetsytan BARA vid start. `setWindowState` före den första
  visningen är startläget (aldrig för offscreen och popuper, aldrig minimerad modal).
  `geometry()` är normallägets plats och storlek och läget — begäran före,
  OS-fönstret öppet, ett KORT taget i `App::executeClose` efter — och ett
  fönster som stängs maximerat öppnas nästa gång i normalstorleken.
  Återställ FÖRE `show()`/`run()` eller i `onLoad`: fönstret skapas DOLT och
  visas EFTER `onLoad` (TODO 80), med startplatsen omräknad för `onLoad`s
  storlek. Wayland har ingen position (`hasPosition` false, läsbart).
- **PEKARNA** (FONSTER_OCH_TIMER): `Widget::setCursor` slår widgetens
  `cursorAt` (för widgeten själv, aldrig ärvd), `AppWindow::setUseWaitCursor`
  slår allt — och båda, liksom `disable()`/`hide()` och en lossad widget,
  OMPRÖVAR pekaren direkt med den senast kända muspositionen
  (`refreshCursor`), utan att musen rör sig. En inaktiv widget visar pilen.
- **ICKE-VISUELLA MEDLEMMAR I `.glazeui`** (FONSTER_OCH_TIMER): en
  `glaze::Component` (Timer, TrayIcon, egna) står i regionen som en medlem
  som ALDRIG adopteras, med settrar, anrop och kopplingar vars sender är
  komponenten (`[this](glaze::Timer* s, glaze::TimerArgs& e)` — sendertypen
  är fri i grammatiken och sparas i `EventBind::sender`). Previewn skapar
  bara typer registrerade som `PreviewForm::Inert`, utför allt som LAGRAR och
  aldrig `start()`/`show()`; en okänd eller `StandIn`-typ loggas och bevaras
  opakt.
- **MUSENS MODIFIERARE OCH DUBBELKLICKET** (post 17, 2026-10-07): `gfx::Event::mods` bärs även av musens
  knappar och rörelser, och en widgets mus-krokar läser masken ur `appWindow()->modifierKeys()` (WinForms
  `Control.ModifierKeys`; appens mushandlers har den i `MouseArgs::mods`). Dubbelklicket RÄKNAS av `AppWindow`
  (samma widget, inom OS:ets `gfx::doubleClickMs()`, inom `dragThreshold`) och levereras EFTER det andra
  trycket — `mouseDoubleClick`, `middleDoubleClick`/`rightDoubleClick` på den widget som TOG trycket — en
  definition för hela ramverket.
- **HÖGER KNAPP, AVBROTTET OCH TANGENTSLÄPPET** (REQ2_INMATNING, 2026-10-13): högerknappen är en tredje
  krokfamilj i mittenknappens form (`rightDown` bubblar; ett taget tryck ger ingen snabbmeny, ett otaget
  `onContextMenu`). Ett AVBRUTET tryck — fokusförlusten, `MouseCaptureLost`, ett övertaget tryck, ett nytt
  tryck med samma knapp — ger `mouseCancel(knapp)` i EN väg (`AppWindow::dropPointer`) och inget `…Up`.
  `onKeyDown`/`onKeyUp` finns på VARJE widget: trycket går till FOKUSENHETEN (den fokuserade och dess
  kompositer, innerst först) FÖRE `keyDown`, upprepningen och släppet till tryckets kedja — eget släpp
  (`cancelled`) vid fokusförlust.
- **HJULETS RIKTNING OCH RITYTANS BINDNINGAR** (REQ9_INMATNING, 2026-10-20): `setWheelInvert` på appen,
  fönstret och varje widget — mushjul och styrplatta, lodrätt och i sidled — ärvs från närmaste förfader till
  fönstrets rot och sedan appen, och vänds på EN plats (`AppWindow::wheelFor`, där också Shift+mushjul blir
  sidled). RELATIV mot OS:et: OS:ets naturliga rullning vänds aldrig tillbaka. Ritytan får det råa hjulet
  (`followsWheelDirection`) och följer sin tabell (läge, källa, modifierare) → (handling, riktning, fart);
  `App::setCanvasBinding` är förvalet den ärver rad för rad. Appens globala har ingen `.glazeui`-form.
- **STANDARD- OCH AVBRYTKNAPPEN** (REQ2_DIALOGER, 2026-10-13 — WinForms `AcceptButton`/`CancelButton`):
  `setAcceptButton`/`setCancelButton`, `Button::setDialogResult` (ett klick stänger med svaret efter
  `onClick`) och `performClick()`. Steget står SIST före Tab, så den som tar tangenten närmare fokus vinner;
  Enter med Ctrl/Alt/Cmd är inte vårt, Escape bara omodifierad, och knappen måste leva, sitta i fönstret,
  synas och vara aktiverad. Utseendet är Win32:s — den fokuserade knappen, annars accept-knappen, på
  accentytan. Meddelanderutan och filväljaren går samma väg.
- **TRÅDSÄKER POST TILL UI-TRÅDEN:** `glaze::app().post(fn)` (APP_API_2
  R17) kör `fn` i UI-tråden, i pump-SVANSEN — aldrig mitt i dispatch. Det är
  den ENDA glaze-funktion som får kallas från en annan tråd. Kön har en
  TIDSBUDGET (`App::setPostBudget`, förval 4 ms): det som inte hann körs
  nästa varv, först, och en funktion som HANN köras stänger i samma varv.
  Ramverkets poster för nästa bild (ritytans tick, kamerans händelse,
  `onResize`) går i en RAMKÖ före, utan budget (`detail::postFrameWork`).
  Under `post` ligger `gfx::postWakeup()`, som måste göra TVÅ saker i varje
  backend: väcka OS-pumpen OCH köa ett `EventType::Wakeup` — ett rent wakeup
  dispatchas bara och skickar `waitEvent` tillbaka i blockering. `Wakeup`
  invaliderar ingenting själv (regel 1); det gör settrarna som den postade
  funktionen anropar.

## Hårda regler

1. **Händelsestyrd rendering.** `glaze::App`-pumpen sover i `gfx::waitEvent`
   (~0 % CPU) när INGET fönster vill ha frame, och ritar bara de fönster där
   något faktiskt ändrats. Inför ALDRIG en kontinuerlig render-loop.
   Animation = `setAnimating(true)` och av igen när klar. Tillstånds-
   ändring utanför event-flödet = `invalidate()`. Behöver något vakna på
   TID (blink, poll-fri klocka) = `Window::startTimer` (väcker `waitEvent`;
   caret-blinket ritar t.ex. bara sin caret-rect per tick) eller, för APPEN
   och före första fönstret, `glaze::Timer` (levereras i pump-svansen,
   invaliderar ingenting själv) — aldrig setAnimating för lågfrekventa
   förlopp. Ska något röra sig VARJE BILDRUTA så länge appen håller det vid
   liv (förstapersonens WASD medan tangenten hålls) = ritytans **`onTick`**:
   den tickar medan `setTicking(true)` gäller eller en tangent som
   `onKeyDown` TOG fortfarande hålls (`AppWindow::isKeyHeld` — släppet och
   fokusförlusten tömmer den), postar EN tick per ritad bildruta till
   pump-svansen och ber inte om fler bildrutor när inget håller den vid liv.
   Efter `onTick` — och ritytans `onCameraChange`, `onGesture` och hjul —
   ritas RITYTAN om, inte fönstret (PRESTANDA D1). Det som rör sig av sig
   självt (en glidning, en tröghet, en uppladdning som strömmar inom
   budgeten, en drapering som väntar på bildens budget) invaliderar på samma
   sätt och STANNAR. Ny widget-interaktion måste sätta dirty via samma
   mönster som befintliga (se handleEvent).
2. **All text via `gfx::Font` + `Renderer::drawText`** (UTF-8 → åäö gratis).
   Fonter väljs via `gfx::FontFamily` (UI/Mono) + `gfx::fontsize::`-
   konstanterna — ALDRIG hårdkodade fontsökvägar (bara i gfx/src/
   font_catalog.cpp) eller sifferstorlekar i koden. Ingen debug-text-
   fallback — namnlisten är en widget sedan CAPTION_BARS och ritas som
   all annan text. **Ritytans scentext** (CANVAS_2D) går en egen väg genom
   samma fonter: `Font::rasterGlyph` och SDF-atlasen ritar etiketterna i
   Glyph-programmet — och en storlek i `Length`-enheter (px/mm/world) är
   DATA på ritytan (`TextStyle::size`), inte en sifferstorlek i koden.
   **Ett tecken fontens fil saknar** (−, ≈, kinesiska …) ritas och mäts med
   OS:ets reservfont — glyfreserven i varje `gfx::Font` sedan
   CANVAS_TEXT_OCH_BILD, EN regel i `Glaze/gfx/src/font_fallback.h` för
   båda textvägarna —, och ersättningstecknet syns bara när INGEN font på
   maskinen har det (`Font::hasGlyph` svarar då false). Fontvalet ändras
   inte: reserven är per kodpunkt, aldrig en annan UI-font.
3. **Varje widget ritar SIG SJÄLV — och utseendet är FYRDELAT.**
   En widget = en header (`Glaze/widgets/include/glaze/widgets/`) + en cpp
   (`Glaze/widgets/src/widgets/`) = ALLT: `draw()`/`contentSize()` bor i
   widgetens EGEN cpp och väljer form med `switch (c.ui)` där
   `default:` är SLATE-vägen (så en ny ritning kan införas widget för
   widget). Gränsen inuti är hård:

       Är det en RITNINGSGREN?  -> UI     (glaze/style.h)   switch (c.ui)
       Är det en FÄRG?          -> Theme  (glaze/theme.h)   c.theme->colors.x
       Är det ett MÅTT/en FORM? -> Style  (glaze/style.h)   c.style->x
       Är det RITNING?          -> Draw   (glaze/draw.h)    Draw::rect(c, ...)

   **UI / Theme / Style är TRE OBEROENDE AXLAR** (+ `ThemeMode` som fjärde,
   läget).
   Att välja UI (`setUI`) sätter de två andra till familjens defaults, men
   båda kan sedan bytas fritt: en UI kan ha FLERA teman (Slate har tio,
   Minimalistic nio, Glaze ett) och FLERA storlekar. **Namnrymden är PER
   UI, inte global:** teman och styles hör alltid till exakt ett UI och
   slås upp via det (`themeAt(ui, i)` / `styleAt(ui, i)`). Namn får därför
   sammanfalla och betyda olika saker — "Regular" finns tre gånger (höjd
   26.7 / 27.3 / 18) och "Neutral" två (Slate ritar fyllda ytor där
   Minimalistic ritar kanter). Ordet "Style" betyder MÅTTEN, punkt.

   Alltså: **inga tal och inga färger i en widget-fil**, och aldrig ett
   `c.r`-anrop (grind: `grep -rn "constexpr float k\|c\.r->"
   Glaze/widgets/src/widgets` = 0). Måtten är LOGISKA px och
   `Draw`-koordinaterna är WIDGET-RELATIVA: `c.bounds()` är widgetens yta
   med (0,0) i eget hörn, och Draw lägger på både origo och skalan — därför
   står varken `resolve()` eller `* c.scale` i ritkod. Målet är **få rader
   som gör mycket**: varje rad i `draw()` ska vara en sak man ser.
   Saknas en primitiv växer `Draw` (det är meningen); saknas ett mått
   växer `Style`; saknas en färg växer `Theme::Colors`.
   **De två positionsfarliga structarna:** `Theme::Colors` initieras
   POSITIONELLT i de handskrivna paletterna — ny roll läggs SIST, för fel
   plats KOMPILERAR och ger tyst förskjutna färger i varenda palett; och
   `Ctx` brace-initieras i `makeCtx` med `box` sist, så en ny medlem går in
   FÖRE `box`. Roller som HÄRLEDS ur en färdig palett (kapseln,
   `fieldReadOnly`) räknas i `deriveRoles()` och står därför sist, utanför
   literalerna.
   Geometri som ritningen och BETEENDET delar definieras EN gång i logiska
   px och projiceras med `Draw::project`/`localX`/`localY` — aldrig två
   uppsättningar mått. **MÄTVÄGEN TALAR OCKSÅ LOGISKA PX** sedan 2026-08-20
   (MÄTKONTRAKTET) — det sista `* c.scale` i widget-lagret är borta, och
   skalningen sker EN gång hos konsumenten (`Widget::resolve`,
   `Bar::placeChild`) när svaret möter `c.area`, som fortsatt är fysisk.
   Kvar i fysiska px med flit: `resolve`/`paintBounds`/`childArea`/
   `clipRect` och beteendekrokarna. Grind: `tests/measure_gate.cpp`.
   Mallen är `button.h`/`button.cpp` — kopiera den.
   Registrera med `GLAZE_REGISTER_WIDGET("typnamn", Klass)` i widgetens
   EGEN cpp och overrida `setProp()` för egna egenskaper (anropa basens
   sist). Egna widgets utanför ramverket ser exakt likadana ut och
   registrerar sig likadant — i app-target är det säkert per
   konstruktion; libglaze själv länkas WHOLE_ARCHIVE i statiskt läge och
   är en DLL med alla sina objekt i delat läge (se
   Glaze/widgets/CMakeLists.txt). Vill GlazeBuilders palett känna typen
   behövs en palettrad — annars visas den som stand-in (helt ok).
4. **Okänt innanför stängslet bevaras ordagrant.** Buildern kastar aldrig
   rader den inte förstår i `GlazeBuilder`-regionen/`*.glazeui` — okända
   fältrader round-trippas opakt, okända typer blir stand-ins, strukturella
   fel ⇒ vägra redigera (aldrig gissa). Äldre builder ska kunna öppna
   nyare genererad kod utan att förstöra den.
5. **GlazeBuilder raderar ALDRIG användarkod.** I `GlazeBuilder`-regionen
   och `*.glazeui` äger buildern allt och skriver om fritt; utanför
   regionen och i `.cpp` får den bara skapa filer från mall eller appenda
   (writer-API:ts fyra operationer gör annat strukturellt omöjligt).
   Osäkert läge → visa koden, låt användaren klistra in.
6. **Event-kontraktet är (sender, args), C#-mönstret:**
   `onClick(Widget* sender, ClickArgs& e)`, `onHover(Widget*, HoverArgs&)`
   (`e.entered`), `onChange(Widget*, ChangeArgs&)`; fönstrets livscykel
   följer samma form (`onClose(Widget*, CloseArgs&)` — `e.cancel` är
   event-UTDATA). Värden läses ur sender, inte ur args
   (`checkbox.isChecked()`, inte `e.value`); ny event-data = nytt fält i
   args-structen (bas `EventArgs`), ALDRIG en ny parameter — det är det som
   gör att handlers aldrig behöver skrivas om. Delade saker delar namn:
   texten heter `text` på alla widgets (aldrig `title`/`label`), "värdet
   ändrades" heter `onChange`, "användaren är KLAR med värdet" heter
   `onCommit` (Enter, fokus bort, släppt reglage, val — sedan 0.1.2, på
   varje inmatning), fokus in i/ut ur en widget eller dess barn heter
   `onFocus`/`onFocusLost`, tangenterna `onKeyDown`/`onKeyUp` och musen
   `onMouseDown`/`onMouseUp`/`onMouseCancel` (TODO 86) på varje widget.
   `title`-aliaset lever kvar i setProp (regel 4-arv).
   **Sender är den som AVFYRADE, också när den inte är en widget**
   (SYSTEMFÄLTET E6, 2026-10-12): typen är `Handler<Args, Sender = Widget>`,
   och en klass med egna events är sin EGEN sender — `glaze::TrayIcon` har
   `Handler<ClickArgs, TrayIcon>` — aldrig `nullptr` eller en intern widget,
   för båda hade brutit regelns två halvor. Varje befintlig `Handler<X>` är
   `Handler<X, Widget>`.
   **Den PARAMETERLÖSA formen finns vid sidan av** sedan 2026-08-13
   (APP_API_2 B3): `stang.onClick = [this] { close(); };` — fälten är
   `Handler<Args>`-wrappers med två `operator=` och lagringen är EN
   normaliserad fullform. Nollställning är **`onClick = nullptr;`** —
   `onClick = {}` är TVETYDIG, och en generisk lambda likaså.
   **ALLA SJU EVENTS tar båda formerna** (APP_API_3 B).
   **Och GRAMMATIKEN bär båda:** `cpp_reader`/`cpp_writer` känner den
   korta lambdan och skriver tillbaka den SOM DEN STOD (kort in, kort ut —
   `Stmt::shortForm`). Ingen expansion till fullformen sker: handlern tar
   noll argument, så `klick(s, e)` hade inte kompilerat, och att rätta
   signaturen vore att skriva i användarens `.cpp` (regel 5). Genererad kod
   emitterar alltid fullformen.
7. **All källkod committas — även genererad.** `*.glazeui` och
   `GlazeBuilder`-regionen i `.h` ägs av GlazeBuilder, märks GENERERAD och
   kompileras som vanlig källkod. Bygget genererar ingenting.
8. **DESIGNER OCH KOD ÄR LIKVÄRDIGA VÄGAR — allt går att göra i BÅDA**
   (Erik 2026-08-19). Varje komponent ska gå att **populera och ändra i
   `.glazeui`**, precis som i varje annan WYSIWYG-editor — OCH exakt samma
   sak ska gå att göra i **kod**. Ingen av vägarna får vara den enda.
   Det är WinForms-modellen och hela skälet till att `.glazeui` ÄR C++:
   designern skriver bara det du själv hade kunnat skriva.

   **Konsekvensen för API-design är bindande:** ett API vars form inte kan
   uttryckas i grammatiken (fältrad, metodanropssats `<id>.<metod>(arg);`,
   naket fönsteranrop, event-koppling) är **fel form** — inte ett fall för
   en opak rad. Returvärden som kedjas, referenser in i vektorer och
   nästlade uttryck går inte att emittera; en PLATT form med index eller
   id-argument gör det. Väljer man ändå trädformen för handkodens skull
   måste den platta finnas vid sidan av, och de två måste producera exakt
   samma tillstånd.

   **KOLLEKTIONSVOKABULÄREN är den formen, och den är SAMMA för varje
   listbärande komponent** (Erik 2026-08-20): **`addX(…)`** lägger till,
   **`setX(index, …)`** ersätter en befintlig post HELT (tilldelning, inte
   partiell uppdatering), **`setXs(lista)`** fyller om allt. Indexbaserat,
   efter WinForms `Add`/`Insert(index, …)`/indexeraren. I dag: `addRow`/
   `setRow`/`setRows` på `PropertyGrid` och `addItem`/`setItem`/`setItems`
   på `ListBox` och `Dropdown`. Index utanför intervallet ⇒ `gfx::log` +
   no-op, aldrig fail-safe och aldrig tyst tillväxt.
   **Erik: *"vi ska få våra komponenters syntax att stämma — så du inte
   lägger till för dessa men har annat för andra."*** En ny komponent med
   en lista följer vokabulären; en avvikelse är ett fel, inte en variant.
   **`MenuBar` står utanför med flit** — den är HIERARKISK, så
   `addItem("Arkiv", "Nytt")` adresserar med STIG i stället för index.
   `.Items.`-nivån (C#:s `Items.Add(…)`) valdes BORT: den hade krävt att
   grammatiken lär sig `<id>.<medlem>.<metod>(arg);`, och metoderna på
   widgeten är lika tydliga.

   **GRÄNSEN GÅR VID UI:t, inte vid regeln** (Erik 2026-08-20): *"tänket är
   att allt vi bygger ska funka genom `.glazeui`."* Ramverkets krav är
   därmed TVÅ — formen går att uttrycka i filen, och previewn UTFÖR den —
   medan att KLICKA fram den i editorn är GlazeBuilder-kategorins krav och
   blockerar inte v1.0. Följden är ingen förhoppning utan en konsekvens av
   hur buildern är byggd: *"allt som funkar genom `.glazeui` kommer funka i
   GlazeBuilder"* — den läser samma fil och ritar med samma `applyProp`/
   `applyCall`, så en uttryckbar och utförbar form kan den redan öppna,
   visa och spara byte-exakt. Utan den gränsen hade varje regel 8-post
   dragit in builder-UX i grinden, och en tom form hade kunnat gömma sig
   bakom "editorn fixar det sen".

   **Det som inte är en widget har också en form** (FONSTER_OCH_TIMER):
   en ICKE-VISUELL MEDLEM (`glaze::Component` — Timer, TrayIcon) står i
   regionen som en medlem som aldrig adopteras, med samma settrar, anrop
   och kopplingar som en widget. Previewn utför det som lagrar och aldrig
   det som når OS:et (en timer som tickar, en ikon i fältet).

   Regel 4 (okänt bevaras opakt) är ett **skyddsnät för framtida filer**,
   inte en ursäkt för att lämna en komponent utanför editorn. Att previewn
   visar tomt är ett REGELBROTT som ska åtgärdas, inte en känd begränsning
   att skriva ner.

## Arbetsflöde

- **TODO.md är dashboarden** — livscykel 🐛/💡→🔴→🟠→🟢→arkiv, planfiler i
  `doc/plans/` (`ÅÅÅÅ-MM-DD_NAMN.md`), arkiv i `doc/plans/archive/` efter
  godkänt test, mätningar och undersökningar i `doc/research/`. Uppdatera
  ALLTID status när något ändras; nytt läggs in som 🐛 (trasigt) eller 💡 (önskat).
  Kod klar men otestad = 🟢.
- **TESTERNA** — när de körs står i de globala reglerna; här står VAD:
  - **Den kompletta sviten:** hela bygget i `build-test` (`-DGLAZE_TESTS=ON`)
    + `ctest` + röktest (demo, galleri, GlazeBuilder). En ren textändring
    (dokument, kommentarer, makron utan beteende) testas inte.
  - **Ett annat OS:** rör ändringen ett annat OS:s kod eller beteende får
    det OS:et en rad i sin `doc/os/<os>/TESTS.md`, precis som BLINT skriven
    kod.
  - **En ny MAJOR eller MINOR:** före 1.0.0, 0.2.0, 0.3.0 … ska ALLA tester
    vara utförda på ALLA tre OS — sviten på varje maskin och raderna i
    `doc/os/*/TESTS.md` genomgångna. En PATCH (0.1.5) taggas med bara det
    OS vi sitter på.
  - **DELAT LÄGE:** den kompletta sviten i `build-shared`
    (`-DGLAZE_SHARED=ON -DGLAZE_TESTS=ON`) + `ctest` + samma röktest körs
    FÖRE varje commit som rör byggsystemet (`CMakeLists.txt`, `cmake/`) och
    före varje MINOR/MAJOR, på alla tre OS. Vardagens commits kör den
    statiska sviten — den bär `dll_gate`, som fångar den vanligaste
    regressionen (statiskt tillstånd i en header) utan ett delat bygge.
- **Plattformsgrindarna bor i [doc/os/](doc/os/)** (`windows/`, `macos/`,
  `linux/` — en `TESTS.md` + `screenshots/` per OS). Skriver du kod som inte
  kan kompileras/köras på ett annat OS (BLINT skriven — vanligt vid
  fasadändringar, som måste implementeras i ALLA backends): **skriv den ändå,
  efter bästa förmåga** — den bromsar ingenting. Lägg en rad i det OS:ets
  `TESTS.md` med vad som ändrats, var koden bor, vad man gör och vad som är
  rätt utfall. Raden är en MINNESLAPP till testpasset, inte en grind. **Varje plattform bockar av sina egna
  rader** — och en grön rad TAS BORT, den sparas inte. Blir raden i stället ett
  fel: 🐛 i TODO.md. Sameness mellan OS:en mäts med glaze-render-PNG:er under
  `doc/os/*/screenshots/` med IDENTISKA filnamn, inte med en delad backend —
  men **pixelperfekt är inget mål före v1.00**, och referensbilderna grindas
  inte förrän designen är låst (ROADMAP steg 10).
- **Prestandan och minnet** mäts med `canvas_perf`, `canvas_perf3d`,
  `app_perf` (en app på riktigt: två fönster, strömmande data, minne och
  energi per steg; `--synlig` för hand, `--varv N` för läckor) och
  `terrain_perf` (en terrängkarta och en 3D-vy i 27 steg; i
  Debug, `--synlig`, `--hp`). Mät före och efter
  en optimering. VAR tiden går visar profileraren (`GLAZE_PROFILE=1`,
  `glaze::profiler().report()` — MANUAL `profiler`).
- Verifiering utan fönster: `makeWidget`/`find<>`/`resolve()` och builderns
  `WindowModel`/`cpp_reader`/`cpp_writer` går att testa headless
  (konsol-testprogram; reader/writer är ren std och kan kompileras helt
  fristående).
- Nya fönster i appar: kopiera demo-mönstret (tre filer + källraderna i
  appens CMakeLists — eller låt GlazeBuilder skapa tripletten från mall).

## Fallgropar

- Backend-typer (Win32/D3D/DWrite/ObjC/Metal/AppKit/Wayland/Xlib/EGL/
  FreeType/fontconfig/libpng) får BARA finnas i `Glaze/gfx/src/backend_*/`
  — grind: `grep -rn "NSWindow\|MTL\|@import\|#import\|wl_\|xdg_\|xcb\|XOpenDisplay\|EGL\|FT_\|FcPattern\|png_"
  Glaze/widgets apps` = 0 träffar; `Glaze/gfx/include` = 0 kodträffar.
  SDL-backenden är riven (L12) och får inte återinföras — grind:
  `grep -rni "sdl" Glaze/ apps/ tools/*/src cmake/` = 0, och levande docs
  (`README.md`, alla `CLAUDE.md`, `TODO.md`, `doc/os/`,
  `tools/vscode-glazeui/README.md`) = 0 träffar (enda undantaget: den här
  grindens egen definition).
- **Wayland/GL-fallgroparna (backend_linux):** en vsync-blockerad
  `eglSwapBuffers` på en skymd yta hänger hela den enkeltrådade pumpen —
  Wayland kör swap-interval 0 + frame-callback-pacing med tidstak
  (`WaylandWindow::beforeSwap`), X11 klassisk interval 1. GL är y-uppochner
  på TRE ställen (glScissor, glReadPixels, FBO-UV:er) — symtomen är lömska
  (fel klippkant, spegelvänd PNG). Key-repeat och muspekare är KLIENTENS
  jobb på Wayland (timerfd + wl_cursor). Configure-handskakningen: ALDRIG
  en buffer före första ack_configure (protokollfel = appen dör).
  **`beforeSwap` väntar MITT i en frame** (mellan composite och swap) och
  får därför bara dispatcha frame-callbackens EGNA `wl_event_queue`: en
  configure som körs där resizar EGL-ytan under den färdigritade framen,
  som sedan committas i fel storlek (spökbilder vid kantdrag). Och varje
  utgång ur den väntan måste släppa läslåset — en lyckad
  `wl_display_prepare_read*` utan matchande `read_events`/`cancel_read`
  gör att NÄSTA läsning väntar för evigt på en läsare som inte finns
  (enkeltrådat = fryst app).
- **Wayland-degraderingarna är fasadfakta:** `setPosition`/`screenArea`/
  `clientToScreen` har ingen skärmrymd där (no-op/tom/klient-relativ) och
  minimize är enkelriktat — popup-placering går ALLTID via popup-kontraktet
  (`AppWindow::setPopupAnchor` → `WindowDesc::popupAnchor*`), aldrig via
  skärmkoordinater. **`setTopmost` är den fjärde** (APP_API_3 A):
  xdg-shell har inget "alltid överst" för vanliga toplevels — z-ordningen
  är kompositörens och användarens beslut. Där är sättaren en no-op som
  LOGGAR en gång och `topmost()` svarar ALLTID false, så degraderingen är
  LÄSBAR för appen i stället för tyst svald. Windows (`HWND_TOPMOST`),
  macOS (`NSFloatingWindowLevel`) och X11 (`_NET_WM_STATE_ABOVE`) klarar
  den.
- **SEX fönsterimplementationer:** en ny ren virtuell på `gfx::Window`
  kostar D3D11, Metal, Wayland, X11, headless OCH Linux-proxyn — och proxyn
  måste vidarebefordra efter `init()` (och lagra en begäran gjord före),
  annars svarar den med sitt eget standardvärde: bygget fångar en glömd
  override men aldrig en tappad vidarebefordran (`setCursor` grindas av
  splitter_gate, `displayMetrics` — skärmens mm, CANVAS_2D etapp 2 — av
  display_gate, `placement()` — FONSTER_OCH_TIMER — av geometry_gate W1).
- **EN MEDLEM I REGIONEN ÄR EN WIDGET ELLER EN KOMPONENT** (FONSTER_OCH_TIMER):
  trädbyggarna (`window_tree`, builderns pass 1) tar de adopterade och de
  inbyggda, `bld::buildComponents` de icke-adopterade som komponentregistret
  känner — allt annat loggas och utförs inte. En ny komponenttyp i
  ramverket behöver sin rad i `kTypeParents` UNDER `glaze::Component`
  (preview_gates komponentorakel fäller annars) och sina setter-fält som
  EXAKTA rader: basens `{"glaze::Widget", …, true}`-fält når aldrig en
  komponent, för dess kedja slutar i den andra roten.
- **TIMERNS HANDLARE KÖRS I PUMP-SVANSEN** (FONSTER_OCH_TIMER): aldrig mitt i
  dispatch, och bara när NÅGON pump går — `run()` av ett synligt, dolt eller
  offscreen-fönster, eller en nästlad modal. En timer som startas utan att
  ett fönster körs tickar alltså inte, och pumpens sovvillkor frågar
  `timerHasDue()` så en antecknad tick aldrig blir liggande.
- **Ritytans 3D (CANVAS_3D) — fällor som inte syns förrän de biter** (detaljerna i
  [Glaze/gfx/src/backend_metal/](Glaze/gfx/src/backend_metal/CLAUDE.md) och
  [Glaze/widgets/src/canvas/](Glaze/widgets/src/canvas/CLAUDE.md)): Metal bakar provtagning, djupformat och
  blandning i PSO:n, så VARJE program byggs i en variant per mål (`psoFor`). **D3D11:s FÖRVALDA djupläge är PÅ
  så snart en DSV är bunden**: varje scenanrop sätter sitt eget läge och UI-flushen återställer UI:ts (GL
  likadant, också före `clear`) — glöms det ärver nästa UI-ritning djuptestet. D3D11:s IA-gräns är skälet till
  meshsegmenten om 64 MiB. 3D-målet ritas om bara när `Widget::damageStamp()` ändras, så `onPaint` måste vara
  deterministisk. **De backend-neutrala filerna (`camera3d.cpp`, `geom3d.cpp` …) står i TRE block i
  `Glaze/gfx/CMakeLists.txt`**, ett per backend — en ny fil i bara ett block bygger på din plattform och
  länkar fel på de två andra.
- **CHROME-TRÄDETS RÖTTER (PROGRAMIKON B2):** `topBar_` och `appIcon`
  resolvas mot chrome-ctx:t (HELA klientytan), allt annat mot
  innehållsytan — och frågan `AppWindow::isChromeRoot(w)` ställs på SEX
  ställen (skadan, full och partiell ritning, lagren, hit-testen och
  `ctxFor`). En ny vandring som jämför med `topBar_` i stället placerar
  ikonen en namnlist för lågt i just den vandringen. Ikonen är
  `HitRole::Client` i hit-kartan — klickbar, aldrig dragyta.
- **EN FLYTANDE DOCKNINGSPANEL BYTER FÖNSTER** (DOCKNING E3): dess
  `appWindow()` är det flytande fönstret, och dess delträd flyttas med
  `Widget::transferChild` i pump-svansen — aldrig mitt i en dispatch, och en
  flytt som en öppen popup i panelen nekar VÄNTAR tills nästa ritning (inga
  omförsök, ingen timer). Det flytande fönstrets `onClose`-veto får inte
  stoppa en KASKAD: det läser HELA ägarkedjans `pendingClose_`, för
  `App::confirmClose` sätter inte flaggan på mellanägare, och appavslutet
  (`EventType::Quit`) köar därför bara ägarkedjornas RÖTTER — annars hade
  ett ägt fönsters egen runda kommit efter rotens veto. Värdens destruktor
  lossar panelerna med `severChild` — utan att vandra deras delträd, där
  lånade barn redan kan vara döda; en lånad widget som överlever sitt
  fönster får `appWindow() == nullptr` (fönstrets livstecken), och en
  flytande panel som DÖR säger det till värden i samma ögonblick
  (`~DockPanel`), innan dess fönster hinner rita eller träffa den igen.
  Panelen (eller verktygsfältet) byter fönster också MITT I ETT TRYCK — det
  levande draget (DOCKNING_DRAG L3): trycket ägs då av VÄRDEN genom explicit
  capture (`setPointerCapture`; ett fälts fönster aktiveras aldrig, så där
  tas trycket över med `takePointerCapture`), och flytten i svansen bryter
  ingenting. `forgetPanel` får därför aldrig nolla dragets tillstånd, och ett
  avbrutet drag avslutas av en krok eller en ritning — aldrig av `draw()`
  med användarkod (`onChange` postas).
- **SYSTEMFÄLTETS MENY KRÄVER ETT FÖRGRUNDSFÖNSTER** (SYSTEMFÄLTET B3): våra
  menyer stängs av VÄRDENS `WindowFocusLost` och tar sina tangenter genom
  värdens menykrok, så utan ett fokuserat ägarfönster stängs en meny aldrig
  vid ett klick utanför — Win32:s `TrackPopupMenu`-fälla i vår form.
  Systemfältets meny öppnas därför i ett eget värdfönster som LYFTS
  (`raise()`), och själva öppningen POSTAS till pump-svansen: fokusbytet
  ger det tidigare fokuserade fönstret `WindowFocusLost`, och
  `App::dispatch` hade stängt den nyss öppnade menyn. Förgrundsrätten
  bygger på att Explorer låter systemfältets ägare ta den vid klicket —
  praxis, ingen dokumenterad garanti.
- **`mouseCancel` KÖRS EFTER NOLLAD BOKFÖRING OCH ALDRIG UNDER EN TRÄDMUTATION**
  (REQ2_INMATNING): en widget som lossas mitt i ett tryck får varken `…Up` eller
  `mouseCancel`. `MouseCaptureLost` delas med dockningens drag (81).
- `waitEvent`-loopen gör att glömd `invalidate()` syns som "UI:t
  uppdateras inte förrän jag rör musen" — leta där först vid sådana buggar
  (även: tappad fönsterhändelse i backendens event-översättning).
- Fönster-drag/resize på Windows kör i en modal OS-loop: kod som måste rulla
  under resize hör hemma i render-callbacket (D3D11: `WM_SIZE`/`WM_PAINT`
  ritar direkt).
- **EGET CHROME HAR EN STORLEKSRAM UTANFÖR DEN SYNLIGA KANTEN** (DOCKNING_DRAG
  K1–K5, E2L): Windows behåller OS:ets OSYNLIGA ram på vänster-, höger- och
  nederkanten (bara överkanten tas bort i `WM_NCCALCSIZE`), Linux lägger en
  GENOMSKINLIG marginal runt ytan (Wayland `set_window_geometry`, X11
  `_GTK_FRAME_EXTENTS` — bara med kompositör och WM-stöd). Därför talar
  `setPosition`/`placement()`/`clientToScreen` KLIENTENS hörn och inte
  fönstrets — `setPosition(clientToScreen(0, 0))` är en identitet som
  dockningen, `setLocation` och `geometry()` bygger på, och D3D11:s
  `GetWindowRect` är INTE klientytan längre. Kartans resize-zoner innanför
  klientytan läses bara av Linux där marginalen saknas. På Linux går varje
  pekarposition genom marginalen (−m) och renderaren förskjuter compositen;
  en ny pekarväg i en drivrutin som glömmer det träffar m px fel.
- **Stängslet:** exakt ETT `#pragma region GlazeBuilder`-par per fönster-.h.
  Skadade/dubblerade/oparade markörer ⇒ buildern vägrar (fail-safe) — den
  "reparerar" aldrig. Grammatiken i `*.glazeui` är kontraktet mellan
  cpp_writer och cpp_reader: varje ny emitterad satsform kräver reader-stöd
  + round-trip-test i samma ändring.
- **Lånade barn × destruktionsordning:** genererade widgets är
  VÄRDE-medlemmar i fönsterklassen, adopterade icke-ägande
  (`adopt(Widget&)`). Subklassens medlemmar dör FÖRE basens rot — roten
  får aldrig röra lånade barn i rivning (children_ är pekare;
  ownedChildren_ äger). Dubbeladoption är no-op + logg.
- Editor-canvasens koordinater: placeringen är logiska px relativt
  förälderns childArea; omvandling sker i BuilderWindow via
  `canvasRect()`+scale.
- **Capture/grab:** behavior-krokar (mouseDown/mouseDrag/mouseWheel/
  caretBlink) får ALLTID sitt Ctx via `ctxFor(widget)` (förälderkedjans
  childArea-vandring) — rå `makeCtx()` ger fel koordinater inne i en
  panel. `parent()` slutar vid fönsterroten; `owner()` bär aldrig
  koordinater.
- **Transparenta fönster:** klick-genomsläpp är kontraktet (Windows/D3D11
  är referensen — alfa-hit-test via retained-spegeln), och partiell
  omritning är avstängd för transparenta fönster (bakgrunds-alfa kan inte
  återställas per rect).
- **WHOLE_ARCHIVE-länken är bärande i statiskt läge:** widgetarnas
  självregistreringar bor som statiska objekt i sina TU:er — targetet `glaze`
  är ett INTERFACE som länkar `glaze_impl` med `$<LINK_LIBRARY:WHOLE_ARCHIVE>`.
  Länka aldrig `glaze_impl` direkt (registreringarna strippas — headless-testet
  i widget-systemets steg 4-grind visade exakt det felet).
- **DELAT LÄGE (GLAZE_SHARED, TODO 73) — fem saker som inte syns förrän de
  biter.** (1) **Inget föränderligt statiskt tillstånd i en header** under
  `Glaze/`, `Threads/` eller `Networking/` (B5): en statisk lokal i en
  inline-funktion, en inline-variabel eller en malls statiska medlem får en
  kopia PER MODUL på Windows, oavsett exportsätt — det hör hemma i en cpp
  bakom en funktion. `tests/dll_gate.cpp` mäter regeln i den statiska sviten.
  (2) **Windows river en DLL:s statiska objekt i `DLL_PROCESS_DETACH`**, efter
  att `ExitProcess` dödat de andra trådarna och under laddarlåset (R3): App
  rivs därför i programmets `exit()` av ankaret `src/app_anchor.cpp` (en
  INTERFACE-källa på `glaze`) — postkön stängs och töms där, före
  `gfx::shutdown()` — och `defaultPool()` rivs aldrig. (3) **Ingen
  `-fvisibility=hidden`** och inga `GLAZE_API`-makron — MinGW:s auto-export
  och ELF/Mach-O:s standardsynlighet gör de interna symbolerna nåbara för
  grindarna. (4) **Bara `glaze_impl` länkar `common_gfx`**: ett mål som länkar
  det direkt får en EGEN uppsättning gfx-objekt och ett eget backendtillstånd.
  (5) **`glaze`, `glaze_threads_ui` och `glaze_terminal_ui` länkas DIREKT till
  varje program och plugin, inte bara via ett statiskt mellanbibliotek** — EN
  regel för ankarna (`app_anchor.cpp`, `ui_anchor.cpp`, `terminal_anchor.cpp`): de är INTERFACE-källor, och ett
  statiskt mellanbibliotek som länkar dem PRIVATE sväljer dem
  (`$<LINK_ONLY>` bär inga källor, och den orefererade arkivmedlemmen stryks).
  Då rivs App i DETACH igen, respektive hamnar fortsättningarna i pooltråden.
  Ingen mekanism vaktar det; regeln står i MANUAL `shared-library`.
