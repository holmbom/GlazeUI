# CLAUDE.md — Glaze/gfx/src/backend_d3d11

Windows-backenden: Win32-fönster och chrome, D3D11-batcher, DirectWrite-text. Del av `common_gfx` — se [../CLAUDE.md](../CLAUDE.md) för backend-neutrala
filer, event-översättningen och de gemensamma fallgroparna, och
[root /CLAUDE.md](../../../../CLAUDE.md) för lagerbilden och de hårda reglerna. Utbruten ur
`gfx/src/CLAUDE.md` 2026-10-10 (CANVAS_3D, Eriks beslut 2: varje källkodsmapp har sin
egen fil; backendarnas 3D-del hade inte rymts under 1000 rader).

## backend_d3d11 (Windows — bara OS-komponenter, inga externa beroenden)


- **`d3d11_backend.h`** — intern header: `D3dFont`, `D3dRenderer`,
  `D3dWindow` + `pushEvent`/`toWide`, och sedan SYSTEMFÄLTET det som delas
  med `d3d11_tray.cpp`: `platformInitialized()`, `iconFromImage`/
  `programIcon` och `trayShutdown()` — och sedan FONSTER_OCH_TIMER
  `appTimerShutdown()` (`d3d11_timer.cpp`) och `iconFromImage`s
  pekarform (`cursor = true` + hotspot → `ICONINFO.fIcon = FALSE`).
- **`d3d11_platform.cpp`** — `gfx::init` (per-monitor-DPI-medvetenhet),
  `waitEvent`/`pollEvent`: Win32 levererar det mesta via WndProc:en, så den
  köar färdigöversatta `gfx::Event` (`pushEvent`) och pumpen dränerar kön;
  **`postWakeup`** (R17) postar ett eget TRÅDmeddelande
  (`WM_APP + 0x51`) med `PostThreadMessageW` till UI-trådens id, fångat i
  `init()` PÅ UI-tråden. Ett trådmeddelande har inget `HWND` och når
  ALDRIG en WndProc, så det känns igen i BÅDE `waitEvent` och `pollEvent`
  FÖRE `DispatchMessageW` och köar ett `EventType::Wakeup`;
  `GetMessageW` blockerar = ~0 % CPU i vila. **SKICKADE meddelanden
  (TODO-post 35, 2026-10-07):** `GetMessageW` kör WndProc:en för ett
  SKICKAT meddelande (SendMessage från en annan tråd, OS:ets
  WM_SETTINGCHANGE vid ljus/mörkt-byte, aktiverings-/fokusbyten) INUTI
  anropet och väntar sedan vidare — den återvänder bara för ett POSTAT.
  Köade WndProc:en ett event där låg det kvar tills musen rördes. Därför
  postar `pushEvent` en **puff** (`WM_APP + 0x52`, köar inget eget event)
  när den körs inifrån GetMessageW-väntan (`g_inGetMessage`), högst en per
  väntan (`g_puffPosted`), och `pollEvent` hämtar ur kön även när
  `PeekMessageW` svarat nej. Grind: `tests/pump_gate.cpp` (bara Windows).
  **MODALA LOOPAR ÄTER TRÅDMEDDELANDEN** (PostThreadMessage-
  dokumentationen): under en dragning/resize (Windows flytt-/storleksloop
  inuti `DispatchMessageW`) försvinner både `postWakeup` och puffen, så en
  `glaze::app().post()` från en tråd väntade efteråt på nästa meddelande.
  `WM_EXITSIZEMOVE` köar därför ett `EventType::Wakeup`. Inte en ctest-grind
  med flit: tangentbordets flyttloop FLYTTAR MUSPEKAREN, och det får ingen
  grind göra med utvecklarens skrivbord. Kvar av gränsen: en modal loop som
  INTE är flytt/resize (ingen finns i dag — menyerna och dialogerna är våra).
  **`WM_APP + 0x53` är UPPTAGET** av systemfältets callback (`kMsgTray` i
  `d3d11_tray.cpp`) — ett FÖNSTERmeddelande till mottagarfönstret, inte ett
  trådmeddelande, så pumpen känner inte igen det. **`shutdown()` kör
  `trayShutdown()` FÖRST** (ingen spökikon efter processen) och sedan
  `appTimerShutdown()`, och
  **`platformInitialized()`** är sann mellan init och shutdown (UI-trådens
  id) — systemfältet gör ingenting utanför den.
  `log` → debug-utgången + stderr, `basePath` (GetModuleFileNameW),
  `toWide`/`toUtf8`, `clipboardText`/`setClipboardText` (`CF_UNICODETEXT` ↔ UTF-8;
  `SetClipboardData` tar över minnesägandet bara vid lyckat anrop; sedan
  post 23 RADSLUTEN — LF ut blir CRLF och CRLF in blir LF, fasadens
  kontrakt — och `OpenClipboard` FÖRSÖKS IGEN (tio gånger, fem ms emellan):
  urklippshistoriken öppnar urklippet efter varje ändring, och två täta
  ändringar tappade annars den andra tyst),
  `systemTheme()` (registry `AppsUseLightTheme`, läses färskt per anrop —
  advapi32), **`systemLocale()`** (`GetUserDefaultLocaleName` +
  `WideCharToMultiByte`), **`openUrl`**/**`openFile`** (`ShellExecuteW` i den delade `shellOpen`; länkar `shell32`),
  **`knownPlaces`** (post 18: `SHGetKnownFolderPath` för FOLDERID_Profile/
  Desktop/Documents/Downloads/Pictures — strängen frigörs med
  `CoTaskMemFree` OAVSETT utfall — och `GetLogicalDriveStringsW` +
  `GetDriveTypeW` + `GetVolumeInformationW` för volymerna, under
  `SetThreadErrorMode(SEM_FAILCRITICALERRORS)` så en tom läsare aldrig öppnar
  en systemruta; en `DRIVE_REMOTE` frågas inte alls. Etiketten blir
  "Windows (C:)", eller bara "C:" utan volymnamn. Länkar `uuid` för
  FOLDERID-konstanterna — knownfolders.h DEKLARERAR dem bara utan
  INITGUID), **`isHiddenFile`** (`GetFileAttributesW` →
  `FILE_ATTRIBUTE_HIDDEN`). Båda grindade av `filedialog_gate` (K1–K4),
  **`screens()`** (FONSTER_OCH_TIMER — `EnumDisplayMonitors` +
  `GetMonitorInfoW`: `rcMonitor`/`rcWork` i fysiska px, skalan
  `GetDpiForMonitor(MDT_EFFECTIVE_DPI)` / 96 × användarskalan (shcore),
  `MONITORINFOF_PRIMARY`; tom före init),
  **`isInProgramFile(adress)`** (DELAT_BIBLIOTEK R3 — modulen som äger
  adressen, `GetModuleHandleExW(FROM_ADDRESS | UNCHANGED_REFCOUNT)`, jämförd
  med `GetModuleHandleW(nullptr)`; frågas av programfilens ankare, också i en
  DLL:s DETACH — laddarlåset är rekursivt),
  `createWindow()` (= `make_unique<D3dWindow>`).
- **`d3d11_window.cpp`** — `D3dWindow`. **Skärmens mm** (CANVAS_2D
  etapp 2): `measureDisplay` läser EDID ur REGISTRET:
  `EnumDisplayDevicesW(…, EDD_GET_DEVICE_INTERFACE_NAME)` ger
  `\\?\DISPLAY#<hw>#<instans>#{guid}`, som blir
  `HKLM\SYSTEM\CurrentControlSet\Enum\DISPLAY\<hw>\<instans>\Device
  Parameters\EDID` — mot `rcMonitor` (per-monitor-v2 ⇒ panelens px).
  `GetDeviceCaps(HORZSIZE)` duger inte (räknas ur skalningsvalet). Körs vid
  init, `WM_MOVE`, `WM_DISPLAYCHANGE` och `WM_DPICHANGED` (före
  skal-eventet), men läser registret BARA när HMONITOR, dess px eller DPI:n
  ändrats; `WindowDisplayChanged` köas när svaret blev ett annat. Ingen EDID
  ⇒ Assumed med enhetens namn (`\\.\DISPLAY1`) som id. Eget chrome via
  Windows Terminals recept: WS_OVERLAPPEDWINDOW + `WM_NCCALCSIZE` som drar in
  klientytan med `frameInsets()` (BÅDA wParam-varianterna, + `SWP_FRAMECHANGED`
  direkt efter CreateWindow — annars ritar DWM native-ramen ändå) och egen
  `WM_NCHITTEST` (OS:et sköter drag/resize/Aero Snap).
  **STORLEKSRAMEN UTANFÖR DEN SYNLIGA KANTEN** (DOCKNING_DRAG K1–K5,
  2026-10-13): ett ändringsbart Custom-fönster behåller OS:ets osynliga ram
  på vänster-, höger- och nederkanten — bara överkantens icke-klientyta tas
  bort — så innehållet vid kanten (verktygsfältet mot vänsterkanten) är
  klickbart ända ut. TVÅ frågor: **`normalFrame()`** = normallägets ram
  {L, 0, R, B} ur `AdjustWindowRectExForDpi(WS_OVERLAPPEDWINDOW, exStil,
  fönstrets DPI)` för Custom som inte är fast, annars 0 — gäller också
  maximerat (Codex 6: storleksomräkningarna och `WM_GETMINMAXINFO` talar
  normalläget); **`frameInsets()`** = AKTUELL inset: maximerat dagens
  ramtjocklek på alla fyra sidor (SM_CXSIZEFRAME + SM_CXPADDEDBORDER),
  annars `normalFrame()`. **`windowRectFor(b, h)`** är klient → fönster för
  de fem omräkningarna (setSize, restore-flanken, WM_GETMINMAXINFO,
  userScaleChanged, klientytan i applyResizeStyle): Native `AdjustWindowRect`
  som förut, Custom `normalFrame`, None rakt av. `WM_NCHITTEST` för ett
  ändringsbart fönster i normalläge: en punkt UTANFÖR klientytan →
  `DefWindowProcW` (ramen är orörd), de översta B px av klientytan →
  `HTTOP` (K5 — skriven gräns: namnlistens och dess knappars översta px),
  resten `hitTest`, som HOPPAR ÖVER kartans resize-roller (`continue`, som
  Metal). **PLACERINGEN ÄR KLIENTENS HÖRN** (K3): `setPosition` drar av
  `frameInsets()`, `trackNormalPosition` lägger på `normalFrame()`, och init
  flyttar fönstret med `setPosition(posX, posY)` efter storleken — så
  `setPosition(clientToScreen(0, 0))` är en identitet (dockningen,
  `setLocation`/`geometry()`). **`applyResizeStyle`** (fast ↔ ändringsbart)
  läser klientens rektangel FÖRE stilbytet och lägger fönstret runt den
  efteråt (vakten `pendingScaleResize_`) — maximerat/minimerat bara
  SWP_FRAMECHANGED. **INIT SÄTTER STORLEK OCH PLATS FÖRE RENDERAREN**: ramen
  kräver fönstrets DPI, och WM_SIZE därifrån hade annars ritat en frame redan
  i init (G9 B4); `renderFrame` gör ingenting förrän `D3dRenderer::ready()`.
  Grind: geometry_gate K1–K10. Windows 10:s 1 px-kant mot vår ram och
  blandad DPI: doc/os/windows/TESTS.md.
  `Chrome::None` = WS_POPUP (ingen ram; CW_USEDEFAULT funkar inte där —
  fast fallback-position). **WindowDesc-attributen:** owner → hWndParent-
  ägare, posX/posY (+ `setPosition`/`clientToScreen`/`screenArea` — fysiska
  px), noActivate (WS_EX_NOACTIVATE + SW_SHOWNOACTIVATE), showInactive
  (DOCKNING_DRAG L6: bara den FÖRSTA normala visningen blir
  SW_SHOWNOACTIVATE i `showFirst` — en senare `setVisible(true)` aktiverar,
  och en maximerad start aktiverar ändå),
  toolWindow (WS_EX_TOOLWINDOW), transparent (WS_EX_NOREDIRECTIONBITMAP +
  composition-vägen i renderern; chrome tvingas None; `WM_NCHITTEST` →
  `alphaHit` → HTTRANSPARENT släpper klick igenom). `id()` = löpnumret som
  stämplar events; `raise()` = SetForegroundWindow+SetFocus — för ett
  noActivate-fönster BARA `SetWindowPos(HWND_TOP, SWP_NOACTIVATE)` (fasaden,
  TODO 87: `SetForegroundWindow` aktiverar också ett WS_EX_NOACTIVATE-fönster,
  och det flytande verktygsfältet hade tagit fokus från värden). WM_CLOSE →
  `WindowCloseRequested` (per fönster, fasadens `processEvent`-kontrakt).
  Live-resize = rita direkt i `WM_SIZE`/`WM_PAINT` (Windows modala
  resize-loop håller annars huvudloopen still); mellan
  `WM_ENTERSIZEMOVE`/`WM_EXITSIZEMOVE` presenteras OSYNKAT
  (`setUnsyncedPresent` → Present(0,0)) så nya framen hinner ut innan DWM
  visar den gamla uppskalad. Custom-chrome sätter DWM-rundade hörn en gång
  vid skapande (`DWMWA_WINDOW_CORNER_PREFERENCE` = ROUND, dwmapi; Win10 =
  tyst no-op, max/snap-övergången sköter DWM). WndProc:en är
  event-översättningen: täcker ALLA nio invalidate-fönsterhändelser +
  mus (TrackMouseEvent → MouseLeave, SetCapture vid knapptryck,
  `WM_MOUSEACTIVATE` → MA_NOACTIVATE för noActivate-fönster —
  WS_EX_NOACTIVATE stoppar INTE klick-aktivering ensamt; utan svaret
  aktiveras popupen och ägarens WindowFocusLost river listan före klicket,
  `WM_NCMOUSEMOVE` → MouseMotion så knapp-hovern släcks över dragytor,
  `WM_MOUSEWHEEL`/`WM_MOUSEHWHEEL` → MouseWheel — OBS skärmkoordinater i
  lParam, översätts till klientyta; `pushWheel` och `pushGesture` står
  UTANFÖR den anonyma namnrymden, deklarerade i d3d11_backend.h, för
  styrplattan går samma väg och modifierarna (`currentMods()`, CANVAS_3D
  etapp 11) stämplas på ETT ställe) + tangenter (`WM_SYSKEY*` går vidare
  till DefWindowProc — Alt+F4; `WM_CHAR` → TextInput: UTF-16 → UTF-8,
  surrogatpar paras via `highSurrogate_`, kontrolltecken < 0x20 filtreras —
  avkodningen är `charText`; sedan REQ3_LAYOUT_FONSTER hämtar `WM_KEYDOWN`
  de WM_CHAR som `TranslateMessage` just postat med `PeekMessageW(hwnd_,
  WM_CHAR, WM_CHAR, PM_REMOVE)`, köar KeyDown med den sammanslagna texten
  (`Event::text`) och sedan ett TextInput per tecken; ett uttaget WM_CHAR
  dispatchas aldrig igen, och ett `WM_QUIT` ur slingan läggs tillbaka) +
  `WM_TIMER` → Timer (`startTimer` = `SetTimer` på fönstret) +
  `WM_SETTINGCHANGE`/"ImmersiveColorSet" → SystemThemeChanged, och
  styrplattans `DM_POINTERHITTEST`/`WM_INPUT`/`WM_KILLFOCUS` till
  `touchpad_` (se `d3d11_touchpad` nedan). Chromet = BARA ramen (TopBar
  ritar captionen); `WM_NCHITTEST` slår upp i glaze:s karta
  (`setHitRegions`/`setCaptionHeight`) utom resize-rollerna (ramen är
  OS:ets, se ovan). **MUSENS CAPTURE** (DOCKNING_DRAG E3, Codex 2 och 5):
  `pressCapture_` är satt medan fönstret håller capture för ett TRYCK
  (WM_xBUTTONDOWN, `captureMouse`) och nollas FÖRE vårt eget `ReleaseCapture`
  (sista knappen upp, `releaseMouseCapture`). `WM_CAPTURECHANGED` (lParam =
  den nya hållaren, NULL vid ett släpp — också när DefWindowProc svarar på
  `WM_CANCELMODE`) blir `EventType::MouseCaptureLost` bara när flaggan var
  satt och capturen inte gick till fönstret självt eller lämnades över
  UTTRYCKLIGEN — `g_handoverTo` pekar på mottagaren BARA under
  `captureMouse`s egen `SetCapture` (meddelandet kommer synkront). Ett annat
  av appens fönster som tar capture på annat sätt är en förlust
  (Codex-granskningen av E3); OS:ets flytt-/storleksloop sätter aldrig
  flaggan. **`captureMouse()`** tar ett pågående tryck från ett annat av
  appens fönster: hållaren (`ownWindowOf` — samma process, `kWndClass`,
  objektet ur `GWLP_USERDATA`) måste ha `pressCapture_` (loopens capture tas
  aldrig, inte heller i fönstret självt), och appens tråd måste äga
  FÖRGRUNDEN — bara förgrundens capture ger hela trycket utanför fönstren,
  så ett noActivate-fönster klickat medan en annan app är förgrund behåller
  sitt tryck (false, loggat). Det flytande verktygsfältets permanenta
  noActivate-fönster lämnar därför sitt drag till värden.
  **`hasMouseCapture()`** = `pressCapture_ && GetCapture() == hwnd_`, frågan
  glaze ställer efter krokarna i ett övertagande. Grind: capture_gate D1–D10.
  Glaze gör förlusten till `mouseCancel` för varje tryck som hade fått ett
  släpp (REQ2_INMATNING — inga nya filter här, Codex 1); `input_gate` K1–K5
  matar `WM_CAPTURECHANGED` genom WndProc.
  **PROGRAMIKONEN** (PROGRAMIKON,
  BLINT — `doc/os/windows/TESTS.md`): `setIcon` lagrar ramarna, `applyIcon`
  (i init före ShowWindow, i setIcon och vid `WM_DPICHANGED`) väljer ram per
  storlek ur `GetSystemMetricsForDpi(SM_CXSMICON/SM_CXICON)` med
  `pickIconImage` och gör HICON med `CreateIconIndirect` — 32-bitars
  top-down-DIB (`BITMAPV5HEADER`, BI_BITFIELDS) med RAK alfa som en .ico-bild
  plus en AND-mask ur alfan (WORD-justerade rader). `programFirst` +
  `FindResourceW(1, RT_GROUP_ICON = 14)` ⇒ `LoadImageW` ur exe:n i stället.
  `WM_SETICON` small/big med den NYA först, den gamla `DestroyIcon` sedan
  (meddelandet kopierar inte handtaget); de sista rivs i `shutdown` EFTER
  `DestroyWindow`. `programName()` (d3d11_platform.cpp) = exe:ns namn utan
  `.exe`. **`iconFromImage`/`programIcon(side)`** står UTANFÖR den anonyma
  namnrymden (deklarerade i d3d11_backend.h): systemfältets ikon går samma
  väg. **AKTIVITETSFÄLTET** (SYSTEMFÄLTET B8): `!desc.showInTaskbar` på ett
  OFÄGT fönster som inte är `toolWindow` ⇒ ett eget DOLT ÄGARFÖNSTER
  (`taskbarOwner_`, klass `GlazeTaskbarOwner`, `DefWindowProcW`,
  `WS_EX_TOOLWINDOW` som WinForms `TaskbarOwner`) blir `hWndParent` —
  Microsofts recept: ett ägt toppnivåfönster har ingen knapp, men står kvar
  i Alt+Tab. Det rivs EFTER fönstret i `shutdown` och i init-felvägarna
  (`destroyTaskbarOwner`): rivs ägaren först förstör OS:et fönstret med den,
  förbi vår egen väg. Går ägaren inte att skapa öppnas fönstret med knapp
  (loggat). **STYRPLATTAN KOPPLAS VID FÖRSTA VISNINGEN** (`D3dTouchpad::attached()`):
  i init för ett synligt fönster, annars i `setVisible(true)` — dold start
  (SYSTEMFÄLTET B10); ett offscreen-fönster visas aldrig och kopplas aldrig.
  **STARTLÄGET OCH NORMALRUTAN** (FONSTER_OCH_TIMER): `showFirst()` gör den
  FÖRSTA `ShowWindow` — i init för ett synligt fönster, annars i
  `setVisible(true)` (dold start, `firstShown_` — och sedan TODO 80 varje
  fönster utom popuper och offscreen, som skapas dolt och visas efter
  `onLoad`; `minimize`/`maximize`/`restore` före `firstShown_` byter bara
  `startState_`, för `ShowWindow(SW_MAXIMIZE)` hade visat det) — med `SW_SHOWMAXIMIZED`
  respektive `SW_SHOWMINNOACTIVE` och VERIFIERAR efteråt (`IsZoomed`/
  `IsIconic`): STARTUPINFO-heuristiken byter bara ut `SW_SHOW`/
  `SW_SHOWNORMAL`/`SW_SHOWDEFAULT` (Raymond Chen, odokumenterat), och ett
  andra anrop respekteras alltid. `trackNormalPosition()` läser
  `GetWindowRect` plus `normalFrame()` (för Custom KLIENTENS hörn, för
  Native ytterhörnet) i `WM_MOVE`, `WM_SIZE` och före den första visningen —
  BARA när fönstret varken är zoomat eller minimerat, så en maximerad start
  bär den begärda normalplatsen. `placement()` = den spårade platsen + `logicalW_/H_` (som
  står still under maximering och minimering) + läget ur
  `IsIconic`/`IsZoomed`. **PEKARNA** (`cursorHandle`): `Wait` = `IDC_WAIT`,
  `Hidden` = `nullptr` (`SetCursor(nullptr)` döljer, dokumenterat),
  `ClosedHand` = VÅR bild ur `../cursor_bitmaps.h` skalad med
  `GetSystemMetricsForDpi(SM_CXCURSOR) / 16` vid fönstrets DPI, byggd lat och
  cachad per storlek; den gamla förstörs EFTER att den nya satts, och den
  sista i `shutdown`. Går bilden inte att skapa: fyrpilen (loggad en gång).
- **`d3d11_timer.cpp`** (FONSTER_OCH_TIMER, 2026-10-12) — `gfx/timer.h`:
  ett lat skapat MEDDELANDEFÖNSTER (`HWND_MESSAGE`, klass `GlazeAppTimer`,
  egen WndProc) bär `SetTimer(hwnd, id, ms)` — samma id ersätter och
  nollställer perioden — och WM_TIMER köar `EventType::Timer` med windowId 0
  och `timerId = wParam` (UINT_PTR bär det 64-bitars körnings-id:t i
  UCRT64-bygget). Inte en trådtimer (`SetTimer(NULL, …)`): trådmeddelanden
  utan HWND äts av de modala looparna. Inget toppnivåfönster behövs —
  timern tar inga broadcasts. `startAppTimer` vägrar före init
  (`platformInitialized()`) och för id 0, och ms < 1 blir 1;
  `appTimerShutdown()` river fönstret och därmed timrarna.
- **`d3d11_tray.cpp`** (SYSTEMFÄLTET, 2026-10-12) — `gfx/tray.h` ovanpå
  `Shell_NotifyIconW` i **NOTIFYICON_VERSION_4** (`NIM_SETVERSION` efter VARJE
  add — versionen dör med ikonen). **MOTTAGAREN** är ett lat skapat, DOLT
  TOPPNIVÅFÖNSTER (klass `GlazeTray`, `WS_POPUP` + `WS_EX_TOOLWINDOW`) och
  ALDRIG `HWND_MESSAGE`: ett message-only-fönster får inga broadcasts, och
  utan `TaskbarCreated` kommer ikonerna inte tillbaka efter en
  Explorer-omstart eller ett byte av primärskärmens DPI. Ett upphöjt program
  släpper igenom callbacken och `TaskbarCreated` med
  `ChangeWindowMessageFilterEx` (UIPI). **CALLBACKEN** (`kMsgTray`, v4:
  händelsen i `LOWORD(lParam)`, id:t i `HIWORD`, ankaret i wParam med
  teckenutvidgning — fysiska skärm-px, samma rymd som `setPosition`):
  `NIN_SELECT`/`NIN_KEYSELECT` = Click, `WM_LBUTTONDBLCLK` = DoubleClick och
  det `NIN_SELECT` som följer SVÄLJS (Qt:s regel; `suppressSelect` nollas
  också av `WM_LBUTTONDOWN` och `NIN_KEYSELECT`, så ett dubbelklick vars
  select aldrig kom inte äter nästa klick), `WM_CONTEXTMENU` = ContextMenu
  (också Skift+F10 och menytangenten), `WM_MOUSEMOVE` = Enter. **LEAVE mäter
  vi själva**: en pollning var 50:e ms (`GetCursorPos` mot
  `Shell_NotifyIconGetRect`) som BARA går medan pekaren står på ikonen, och
  ett `WM_MOUSEMOVE` sedan förra pollningen hoppar över geometrin (skalets
  eget besked väger tyngre — annars Enter/Leave om vartannat) — Leave inom
  ~100 ms; plus `NIN_POPUPCLOSE` om den kommer (`NIN_POPUPOPEN/CLOSE` ensamma
  är opålitliga på Windows 11). **TIMRARNA** sitter på mottagarfönstret:
  `0x10000 + id` = animationen (Tick), `0x20000 + id` = hoverns pollning.
  **`TaskbarCreated` SKJUTS UPP** till pumpen med en timer (`kReaddTimer`,
  `USER_TIMER_MINIMUM`): en skickad broadcast kan levereras MITT I ett eget
  `Shell_NotifyIconW` (skalet väntar med `SendMessageTimeout`), och en
  återuppbyggnad där hade kunnat lägga tillbaka en ikon `removeTrayIcon`
  just tog bort. **`place()`** uppdaterar med `NIM_MODIFY` (tooltipen bara
  när den ändrats) och faller åt båda hållen: ett vägrat MODIFY betyder att
  Explorer startat om (ADD direkt), ett vägrat ADD kan ha överskridit
  skalets tidsgräns och ändå lyckats (MODIFY). Ett ADD som inte går
  (Explorer har inte startat vid en autostart) loggas EN gång, och ikonen är
  ändå registrerad — `TaskbarCreated` lägger till den. **BILDEN** är
  `SM_CXSMICON` i AKTIVITETSFÄLTETS DPI (`GetDpiForMonitor` på
  `Shell_TrayWnd`s skärm — `shcore` i länkningen), `programFirst` ⇒
  `programIcon` och annars `pickIconImage` + `iconFromImage`; HICON:en hålls
  tills NÄSTA har ersatt den (som fönstrets `WM_SETICON`). `setTrayIcon`
  utan bild på ett registrerat id tar bort ikonen (false = inte
  registrerat). Tooltipen med `NIF_SHOWTIP` (v4 döljer den annars), kapad
  vid 127 UTF-16-enheter utan att dela ett surrogatpar. **`g_entries` står i
  namnrymden** (rivs efter `glaze::App`, se ../CLAUDE.md), och
  `applyShowInDock` loggar false en gång (begäran lagras i ../platform.cpp).
- **`d3d11_print.cpp`** (REQ8 10) — `gfx/print.h`: `PrintDlgExW` med
  `PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE` (kopiorna är drivrutinens) och
  en DEVMODE för STANDARDSKRIVAREN som förval (`DocumentPropertiesW`, papperet
  som `DMPAPER_*` inom 1 mm, annars `DMPAPER_USER` i tiondels mm); måtten ur
  DC:n (`PHYSICALWIDTH`/`PHYSICALOFFSETX`/`HORZRES` …, orienterade). Sidan =
  `StartPage` + `StretchDIBits` (HALFTONE, BGRA top-down) över `HORZRES` ×
  `VERTRES` + `EndPage`. DC:n (`PrinterDc`) är `PrintSettings::native`.
  `hwndOwner` måste vara giltigt — `D3dWindow::hwnd()` finns för det. Länkar
  `comdlg32` och `winspool`.
- **`d3d11_touchpad.h/.cpp`** (TODO 53) — STYRPLATTAN per fönster
  (`D3dWindow::touchpad_`; kopplad vid första visningen, lossad före
  DestroyWindow):
  DIRECT MANIPULATION (Chromiums falska viewport, MANUALUPDATE, COM STA per
  fönster, handrullad hanterare) ger tvåfingersdraget som `MouseWheel` med
  `precise` och nypningen som `Gesture` — genom WndProc:ens `pushWheel`/
  `pushGesture`, så de bär modifierarna; RÅA HID-KONTAKTER (`WM_INPUT` +
  `HidP_*`, registrerat EN gång per process, följer tangentbordsfokus) ger
  vridningen — EN sekvens. Pumptimern (PRIVAT id, frågas först i `WM_TIMER`,
  aldrig ett event) går bara under en interaktion (regel 1); `WM_INPUT` till
  DefWindowProc ALLTID. Detaljerna i headern; hårdvaran oprövad (TESTS.md).
- **`d3d11_renderer.cpp`** — `D3dRenderer`: device/swapchain (flip-model,
  vsync via `Present(1, 0)`), EN pipeline för allt (3D-first: 2D = ortho-
  quads; VS gör px → NDC via viewport-cbuffer; PS samplar R8-textur som
  alfa-mask — 1×1 vit för geometri, glyf-atlas för text). Batchern samlar
  vertex i ritordning, flushar vid textur-/topologibyte/clear/present —
  målarordningen bevaras. `strokeRect` = fyra 1-px-stavar (exakta hörn).
  De rikare primitiven (`fillRoundedRect`/`strokeRoundedRect`/`dropShadow`/
  `innerShadow`) är SDF i pixel-shadern: parametrarna (centrum/halvmått/
  radie/blur/offset + mode) bär varje vertex, så de batchas i samma
  pipeline som allt annat — mode 0 = texturmask, 1 = fylld rundad, 2 =
  kontur, 3 = drop shadow, 4 = inner shadow (`sdfQuad`), 5 = RGBA rakt av
  (compositen). HLSL kompileras i runtime med `D3DCompile`
  (d3dcompiler_47.dll är OS-komponent) — EN gång per process: `compileHlsl`
  sparar bytekoden per (källa, ingång, mål, definitioner), för varje fönster
  har en egen enhet och kompilerade förut ALLA program (~12) på nytt, så en
  meny eller dropdown tog 0,5–1 s att öppna (0.1.4). `loadFont` kopplar fontens
  device-pekare före `load`. **Retained-RT:n** (`offTex_/offRtv_/offSrv_`,
  följer backbufferns storlek): `beginFrame` binder den (`activeRtv_`),
  `composite()` flushar, byter till backbuffern och ritar RT:n som
  fullskärms-quad i mode 5 — skriver HELA backbuffern (det som gör
  FLIP_DISCARD ok och overlayen självstädande). `retainedValid_`
  (fasadens `Window::retainedValid`): falskt vid `createOffscreen`
  (nyskapad yta = odefinierat innehåll), sant efter composite — styr om
  glaze-lagret får rita partiellt; i degraderat läge förblir det falskt =
  full omritning varje frame. `clear` clear:ar aktiva målet; misslyckas
  RT-skapandet ritas direkt i backbuffern (degraderat).
  **Klipp** (`applyClip`): flush + `RSSetScissorRects` + rasterizer med
  `ScissorEnable` (`rasterScissor_`) — scissor är context-state, batchen
  måste flushas före bytet (samma mönster som textur-/topologibyte).
  **De RUNDADE maskerna** (RUNDAT_BARNKLIPP) går till en
  EGEN PS-cbuffer **`b1`** (`cbuffer ClipMasks`, 144 byte 16-byte-packad:
  `float4 gClipP0[4]` + `float4 gClipP1[4]` + `float4 gClipMeta`),
  uppdaterad med `Map(WRITE_DISCARD)` i `uploadClipMasks` EFTER flushen
  och bunden med `PSSetConstantBuffers(1, …)` i `beginFrame`. **`b0` är
  VS:ens viewportbuffer och rörs INTE.**
  **Transparent-vägen** (`init(..., transparent)`): EGEN swapchain via
  `CreateSwapChainForComposition` + `DXGI_ALPHA_MODE_PREMULTIPLIED` +
  DirectComposition-träd (device→target(hwnd)→visual→commit;
  `DCompositionCreateDevice` hämtas via `LoadLibrary("dcomp.dll")` —
  MinGW saknar importlibben). Rak-alfa-blandning mot alfa-0-clear ger
  premultiplicerat innehåll i RT:n; compositen skriver den då UTAN
  blandning (opaka fönster behåller blend-läget — vägen är orörd).
  Misslyckas composition degraderas fönstret till opakt (loggat).
  **Alfa-spegeln**: per present (ENDAST transparenta fönster) kopieras
  retained-RT:n till en staging-textur och alfakanalen läses till CPU —
  `alphaHit(x, y)` svarar fönstrets `WM_NCHITTEST` (`HTTRANSPARENT` under
  tröskel = klick går igenom). Degraderat läge ⇒ opak hit-test.
- **`d3d11_text.cpp`** — `D3dFont`. DirectWrite: `CreateFontFace` från
  .ttf-sökväg (godtyckliga filer), glyfer rasteriseras vid behov med
  `IDWriteGlyphRunAnalysis` (ren DWrite, ingen D2D; ClearType-3x1 medlas
  till gråskala) och packas rad-för-rad i en R8-atlas (512², reset om full).
  `draw` pixel-snappar baslinjen och lägger texturerade quads;
  `measure`/`prefixWidth`/`byteIndexAtX`/`fitWidth` går alla via
  `advanceOf(cp)` (design-advances — rasteriserar aldrig, funkar utan
  device). `loadFamily(f, storlek, stil)` löser familjen och den närmaste
  stilen via `font_catalog`. **Etapp 5:** rasteriseringen är EN funktion,
  `rasterize(face, gi, GlyphRaster&)`, som atlasen (`getGlyph`) och
  `rasterGlyph` delar, och `readStyle()` läser stilen ur fontens OS/2-tabell
  (`TryGetFontTable`: fsSelection bit 5/0/9 och vikten ≥ 600 som fet), också
  för `load(path)` — och vikten (`weight_`), som reserven frågar med.
  **GLYFRESERVEN** (CANVAS_TEXT_OCH_BILD A3): en kodpunkt fontens egen fil
  saknar slås upp i systemets kedja — `IDWriteFactory2::GetSystemFontFallback`
  (hämtas lat en gång per font; saknas den loggas det en gång per process
  och fonten ritar som förut) → `MapCharacters` med `OneCharSource`, en
  analyskälla för EN kodpunkt med användarens språk (`userLocale`), fontens
  vikt och stil, normal bredd och ingen basfamilj. Svaret är en
  `IDWriteFont` vars face måste ha glyfen; dess emSize är fontens gånger
  skalan `MapCharacters` gav. Reserverna är `Face`-poster i `reserves_`
  (face, emSize, skala, nyckel) och samma font två gånger blir samma post
  (`faceKey` = fontfilernas referensnycklar + facens index + simuleringarna
  + skalan — aldrig pekaren, för kedjan kan svara med en ny face för samma
  fil); över 64 loggas en gång per font. Regeln (egen → reserv → U+FFFD → .notdef, styrtecken aldrig
  ur reserven) är `resolveGlyph` i `../font_fallback.h`, och `resolve(cp)`
  cachar `{GlyphRef, stegbredd}` per kodpunkt i `resolved_` — `advanceOf`,
  `getGlyph` och `rasterGlyph` läser alla den, så mätningen och ritningen
  aldrig kan gå isär. `'?'`-steget är borta. `hasGlyph` = `ref.real`.
  `unload()` tömmer cachen och reserverna (en font som laddas om får en ny
  kedja). Ogiltiga kodpunkter (surrogater, > U+10FFFF) frågas aldrig.
- **`d3d11_image.cpp`** — `D3dImage` + `D3dRenderer::loadImage/drawImage`
  + bilderna ur pixlar och Image-programmet (CANVAS_2D etapp 3).
  WIC (windowscodecs — OS-komponent) avkodar PNG-data → RGBA8
  (`gfx::decodeImage`, TRÅDSÄKER: COM per anrop och tråd; BARA PNG sedan
  CANVAS_3D beslut 5 — `hasPngSignature` prövas innan WIC får datat, och
  `loadImage(path)` läser filen och går samma väg i stället för
  `CreateDecoderFromFilename`) → `createImage`
  → DEFAULT-textur + SRV (R8_UNORM eller R8G8B8A8_UNORM; utan pixlar en
  nollad buffert — D3D11 lovar inget om en textur utan initdata).
  `updateImage` FLUSHAR UI-batchen före `UpdateSubresource` med en box:
  det som batchats läser texturen först vid flushen, så utan den hade det
  redan ritade fått det nya innehållet. Ritas som mode 5–7-quads i samma
  batch-pipeline (5 = RGBA rakt av, 6 = ett målets PREMULTIPLICERADE bild —
  av-premultipliceras i HLSL:en, 7 = R8 som gråskala; en `flippedV`-bild
  speglas i UV:erna) med bildens filter som batch-tillstånd och
  bildens OPACITET (2026-10-07) i vertexfärgens alfa — HLSL:ens mode 5
  multiplicerar `t.a` med `i.col.a × clipCov`, alltså bara alfan (rak
  alfa), och compositens quad bär full alfa så faktorn är 1 där. Grindad i
  pixlar av `pixel_gate` (verktygsknappen måste visa sitt inaktiva läge).
  **IMAGE-PROGRAMMET** bor här och inte i d3d11_scene.cpp: världsquads ur
  `ImageVertex`, scenens b2 i programmets EGEN form **`ImageCb`** (params .z
  opaciteten, .w flaggorna 1 = premultiplicerad, 2 = speglad v, 4 = R8 —
  samma tal i GLSL och MSL — och sedan REQ8 3 `adjust` = `sceneImageAdjust`,
  som pixelshadern ger förspelets `glaze_colorAdjust` på den RAKA färgen,
  `kHlslAdjustSrc` i d3d11_program.cpp),
  vertexshadern lämnar w ORÖRT (perspektivkorrekt uv), bildens SRV och
  sampler på t0/s0 per ritning; `drawMesh` skickar hit överst. **Den
  PROJEKTIVA vikten** (CANVAS_TEXT_OCH_BILD B1): `ImageVertex::q` är ett
  tredje indataelement (`TEXCOORD1`, R32_FLOAT vid `offsetof(q)` — 20 byte
  per vertex), vertexshadern skickar `(u·q, v·q, q)` (v speglat FÖRE
  multiplikationen) och pixelshadern samplar `uvq.xy / uvq.z`. q = 1 är den
  gamla vägen exakt; en fri fyrhörning får sina vikter ur `quadWeights` i
  ritytans byggare. Plus fasadens
  `gfx::encodePng` (GLAZE_RENDER): WIC-encodern, 32bppBGRA +
  deterministisk RGBA→BGRA-swizzle (litar inte på SetPixelFormat-
  förhandlingen). **Readback-paret**: `D3dRenderer::captureRetained`
  (d3d11_renderer.cpp — egen staging + CopyResource + Map(READ),
  BGRA→RGBA) bakom `D3dWindow::captureFrame` — glaze-renders väg, med
  `WindowDesc::hidden` (init hoppar ShowWindow).
- **`d3d11_scene.cpp`** (CANVAS_2D etapp 1) — scenen bredvid UI-pipelinen:
  **`D3dGpuBuffer`** (statiska = DEFAULT + `UpdateSubresource` med en box,
  ordnad mot rit-anropen; ringens = DYNAMIC), den **transienta ringen** per
  buffertslag (256-byte-delar, DISCARD vid framens första Map och
  NO_OVERWRITE sedan, dubblas när den inte räcker — en utväxt buffert lever
  i `retired` till nästa `beginFrame`, för delar ur den kan redan ha
  lämnats ut) och **Line-programmet** (HLSL i runtime, `vs_4_0`/`ps_4_0`).
  Input-layouten läser `LineVertex` på dess offset (`dist` som `R32_FLOAT`;
  sida, slag, ändar och `pad` som `R8G8B8A8_SINT` — `pad` läses inte).
  Scenens uniformer i en EGEN cbuffer **`b2`** (`SceneCb`: matrisen i
  kolumnordning + målets storlek, breddens enhet och läge — sedan etapp 4
  färgen och sedan T2 `stroke` (spetsgräns, streckantal, `perWorldUnit`,
  förskjutning) och `dash[8]`, 160 byte totalt) bunden till BÅDA stegen —
  b0 och b1 rörs inte, och b1 (maskerna) läses av Line-programmets
  pixelshader precis som av UI:ts. `DrawIndexed` med R32-index. Vertexshadern: skärmläget
  projicerar punkten och grannen, TRIMMAR mot närplanet (klippdjup 0 — en
  ände bakom kameran flyttas längs segmentet; sedan CANVAS_3D etapp 3 också
  mot det bortre planet och en skyddsram, se "Klippytan" nedan) och extruderar i px med
  `noperspective` avstånd; världsläget extruderar i planet med en px-
  marginal räknad ur den lokala skalan och AA via `fwidth`. Misslyckas
  programmet loggas det och `drawMesh` ritar inget — UI:t lever. **Etapp
  4:** Line-programmets källa tar färgens del (`kHlslPaintSrc`) i stället
  för en egen cbuffer (ett program som inte läser allt deklarerar en
  kortare) — och fragmentshadern multiplicerar med `paintColor`
  i mittlinjens världspunkt (`ppos`, trimmad som punkten) och med
  anropets opacitet. `drawMesh` skickar Fill och Glyph till sina filer
  överst, som Image. **KONTUREN KOMPLETT (T2, 2026-10-10):** täckningen är
  ett TECKNAT AVSTÅND (skärmen `saturate(d + ½)`, världen `d/fwidth(d) + ½`)
  till sidan, en LINJEÄNDES kant och strecken. En rak eller fyrkantig
  linjeände förlängs (AA-px, fyrkantig + halva bredden) i VÄRLDEN längs
  segmentet — klipprymden är linjär, och px → världsenheter med den lokala
  derivatan — så vertexen får sitt eget w och `dist` följer (en flytt i
  skärmen med det gamla w sneddade strecken under tilt; H8). SKARVBITARNA
  Miter/Bevel är fyrhörningar ur de projicerade grannarna (`joinFrame`,
  spetsen ext/c eller fasens punkt) som klipps i fragmentet mot båda
  segmentens remsor och, avfasad, mot fasens linje. Strecken: läget
  `dist × perWorldUnit + förskjutning`, närmaste streck med ±perioden, i px
  via `ddx/ddy` av läget, med linjens ändform (SVG).
- **`d3d11_fill.cpp`** (CANVAS_2D etapp 4) — Fill-programmet:
  `FillVertex`-layouten (slag + sida som `R8G8B8A8_UINT`), vertexshadern
  som lämnar kantens punkt orörd (rasteriseraren klipper) och flyttar
  remsans yttre vertex 1 px utåt — kantens och grannkantens normal i
  SKÄRMEN, gering högst 4 px, punkten bakom närplanet flyttad längs sin
  kant, speglingen ur planets homografi — och täckningen `noperspective`.
  Färgens HLSL **`kHlslPaintSrc`** (hela b2, rampen t0, linjär sampler s0,
  `paintColor`) bor här och delas med Line och Glyph. De SKÄRMVÄNDA hörnens
  djup är **`glaze_standingZ(m, c, off)`** i **`kHlslStandSrc`**
  (d3d11_program.cpp, REQ9 7): EN definition, i de egna programmens förspel
  och efter färgen i Line- och Glyph-källan — planets djup genom ankaret
  parallellt med xy där det är närmare, draget högst ¼ av djupet (G9 J1, J5) —
  `fillPaint(cb, d)` och `bindPaintRamp(d)` — den senare binder rampen
  (eller vit) och ALLTID `samplerLinear_`.
- **`d3d11_glyph.cpp`** (CANVAS_2D etapp 5; REQ3_RITYTAN: storleken klampas
  till `gPaint1.x`/`.w` = `glyphMinPx`/`glyphMaxPx` direkt efter `sizePx`,
  golvet först) — Glyph-programmet: billboards
  (ankaret projiceras, förskjutningen × storleken läggs i skärmen, vriden
  efter skärmen eller kartans projicerade riktning — perspektivdivisionens
  derivata), ett ankare bakom närplanet gallras (z = −2), formerna växer en
  px. Fragmentshadern: fältet → avstånd i texlar (+ syntetisk fet, + glorian
  i glorians pass) → täckning med skalan storlek/referens; formerna som
  avståndsfält med kontur utanför. b2: params .z = storlekens enhet, paint0 =
  (referensen, radien, fetstilen), paint1.y = opaciteten; atlassidan på t0
  med `samplerLinear_`, vit när chunken bara bär former. **Etapp 9:**
  etiketternas synlighet på VERTEXSTEGETS t1 (`VSSetShaderResources(1)`,
  läst med `Load` — ingen sampler), indexet `label` som `R32_UINT`-attribut
  (`TEXCOORD6`), basen i params .w (−1 = ingen bild bunden); vertexshadern
  gallrar en dold etikett (z = −2) och pixelshadern multiplicerar alfan med
  synligheten (`nointerpolation vis`). **BILDMARKÖREN (`kImage`):** paint0.w
  är bildens bredd/höjd (vertexshadern krymper den kortare axeln kring
  ankaret och speglar v för en `flippedV`-bild), paint1.z
  `sceneImageFlags` — pixelshadern samplar bilden och av-premultiplicerar
  ett målets.
- **UI-pipelinens del i scenen** (`d3d11_renderer.cpp`): klippmaskernas
  HLSL är **`kHlslClipSrc`** (cbuffer b1 + `sdRoundRect` + `clipCoverage`),
  klistrad före BÅDE UI:ts och Line-programmets källa (`compileHlsl`). Där
  bor också MÅLARENS KLIPP (REQ8 1): cbuffer **b5** (`SceneClipCb`, 144
  byte) och `sceneClip(p)` — scenens fyra källor multiplicerar alfan med den
  och den `discard`:ar utanför (inget djup); UI:t läser den inte.
  `bindPipelineState` binder b5 bredvid b1, och **`bindSceneClip`**
  (d3d11_scene.cpp) skriver den bara när anropets klipp skiljer från
  `sceneClip_` (det b5 bär — inget klipp från `initScene`), och
  **`pipeline_`**: `drawMesh` flushar UI-batchen först och lämnar sitt
  program bundet; **`flush()`** — det enda stället UI-pipelinen ritar —
  återbinder layout, vertexbuffert och shaders (`bindUiPipeline`) innan sin
  `Draw` — i `flush` och inte i `setState` med flit: då kan ingen UI-`Draw`
  gå förbi den. `beginFrame` binder UI-pipelinen och nollar ringarna
  (`beginSceneFrame`). **SAMPLERN (etapp 3)**: `samplerNearest_`/
  `samplerLinear_` och batchens `curFilter_` — `setState(srv, topo, filter)`
  bryter batchen vid ett filterbyte och `flush()` binder batchens sampler
  vid VARJE Draw (inte en gång per frame), så Image-programmets sampler
  aldrig läcker in; text, geometri och compositen säger Nearest.
- **`d3d11_program.cpp`** (CANVAS_3D etapp 1) — de EGNA programmen:
  `createProgram` med förspelet och reflektionen, `D3dShaderProgram` och
  `drawCustomMesh`. Se avsnittet längst ned.
- **`d3d11_target.cpp`** (CANVAS_2D etapp 6) — `D3dRenderTarget` (B8G8R8A8-
  textur med RTV och SRV, bilden en premultiplicerad `D3dImage`, nollad vid
  skapandet) och `bindTarget`: flushar, nollar t0 (läs/skriv-hazarden),
  binder RTV:n och `setViewport` — viewporten och UI:ts b0 med MÅLETS
  storlek (`targetW_/targetH_`, som scenens program också läser i sina b2).
  `bindTarget(nullptr)` återvänder till `frameRtv_` (retained-RT:n från
  beginFrame, backbuffern efter composite) — och i ett offscreen-block till
  INGET mål. `clear` lagrar färgen premultiplicerad. Djupet, MSAA,
  `supportedSamples` och `readTarget` (CANVAS_3D etapp 0): se nästa avsnitt.

## 3D-delen (CANVAS_3D etapp 0 — djup, MSAA och renderläget)

Kontrakten står i [../../include/gfx/CLAUDE.md](../../include/gfx/CLAUDE.md)
(`TargetDesc`, `RenderState`, `readTarget`, `beginOffscreen`); grind: G9
`tests/scene3d_pixel_gate.cpp`.

- **MÅLET** (`d3d11_target.cpp`): bilden är alltid en B8G8R8A8-textur med
  ett prov (RTV + SRV — RTV:n behövs också för att nolla den). Med
  provtagning > 1 ritas det i en EGEN provtagen textur (`msaaTexture`, RTV:n
  sitter där) som `ResolveSubresource` löser upp till bilden när målet
  LÄMNAS — i `bindTarget`, alltså både i popTarget och när ett nästlat mål
  binds ovanpå (bilden kan ritas i det nästlade, och den provtagna ytan finns
  kvar när man kommer tillbaka). Djupet: en D32F-textur med samma
  provtagning och en DSV (`TEXTURE2DMS` med MSAA), bunden bredvid RTV:n
  (`activeDsv_`); `clear` rensar den till 1 med `ClearDepthStencilView`.
  `bound_` är målet som är bundet — `beginFrame`/`composite`/
  `beginOffscreen` nollar det. **`supportedSamples`** läser `msaaOk_`, som init
  frågar EN gång med `CheckMultisampleQualityLevels` för BÅDE B8G8R8A8 och
  D32_FLOAT. **`readTarget`**: staging + CopyResource av bilden + Map,
  BGRA → RGBA och `unpremultiplyRgba` (`../image_util.h`).
- **RENDERLÄGET** (`d3d11_renderer.cpp`): tillståndsobjekten skapas i init —
  `depthStates_[DepthTest][skriv]` (Off = `DepthEnable FALSE`, också
  skrivningen av), `blendStates_[Blend]` (`blendDescOf`, mot det
  PREMULTIPLICERADE målet; DepthOnly = `RenderTargetWriteMask 0`) — och
  rasteriserarna LAT i `rasterFor` (nyckel: gallring, bias, lutning, scissor,
  MSAA, djupklippning; högst 4096, sedan UI:ts — loggat). Varje program
  (`d3d11_scene/fill/glyph/image.cpp`) anropar **`applyMeshState(d.state)`**
  före sin DrawIndexed. Tre saker i den är beslut: djupet verkar bara i ett
  mål MED djup (`bound_->hasDepth()`), `FrontCounterClockwise = TRUE` (D3D11
  avgör vindningen som triangeln syns i målet — moturs för ögat är
  framsidan), och med djup klipper rasteriseraren i z (`DepthClipEnable`, som
  GL och Metal alltid gör). 2D-vägens läge (ingen gallring, bias, MSAA eller
  djupklippning) är EXAKT `raster_`/`rasterScissor_` — inga nya objekt, inga
  nya anrop (cacharna `cur*`/`*Known_` hoppar ett oförändrat tillstånd).
  **De två har också `FrontCounterClockwise = TRUE`** (CANVAS_3D etapp 4):
  utan gallring blir pixlarna desamma, men `SV_IsFrontFace` följer flaggan,
  och ett scenanrop med Cull::None utan djup, MSAA och bias får just de
  rasteriserarna — Mesh-programmets tvåsidiga ljus och platta normal hade
  vänt fram- och baksidan där (mesh_program_gate E2/E3 "utan djup"; fäller
  rättelsen borttagen).
- **UI-ÅTERSTÄLLNINGEN ÄR BÄRANDE.** D3D11:s FÖRVALDA djupläge (nullptr) har
  djupet PÅ så snart en DSV är bunden, och ett scenanrop lämnar sitt läge
  kvar. `bindUiPipeline` — som `flush()` kör när ett scenprogram var bundet —
  sätter därför UI:ts läge (`applyUiState`: djup av, UI:ts rasteriserare,
  rak alfa), och `bindPipelineState` (beginFrame/beginOffscreen) glömmer
  cachen och sätter det uttryckligen. `applyClip` byter scissor via
  `rasterFor`, så MSAA-målets UI får sin rasteriserare (`MultisampleEnable`,
  som FL 10.0 kräver för trianglar). Den transparenta compositen binder
  UI-pipelinen INNAN den slår av blandningen — annars hade flush() återställt
  UI:ts blandning ovanpå undantaget.
- **`beginOffscreen` är INTE en no-op** (avvikelse från planen): före ett
  fönsters första frame är ingenting bundet i kontexten, så blocket binder
  UI-pipelinens tillstånd (`bindPipelineState`) men inget mål (`activeRtv_`
  nullptr — utanför ett render-mål ritas ingenting, och `clear` rensar
  ingenting). `endOffscreen` flushar; Map i readTarget väntar in arbetet.
  Sedan DOCKNING_DRAG E2 ritar fönstret ingen frame i init vid någon skala:
  storleken och platsen sätts innan renderaren finns (förr ritade WM_SIZE
  från `setSize` en frame vid skala ≠ 1, och G9 B4 tvingade skalan till 1).
  Blocket har en egen GPU-fråga (`startGpuQuery`/`endGpuQuery`) och mäts som en
  frame, med upplösningen av målen som lämnas inuti.
- **EXTERNA MÅL OCH ANDRA GRAFIK-API:er** (`gfx/native.h`; grind
  `tests/external_target_gate.cpp`): `nativeDevice` lånar ut rendererns
  `ID3D11Device` med LUID:en (`packLuid`, `adapterLuid_` satt i init) och
  funktionsnivån. `importRenderTarget` (`d3d11_target.cpp`) kräver SAMMA enhet
  (`GetDevice`), mått, ett prov, RENDER_TARGET + SHADER_RESOURCE och en
  RGBA8-/BGRA8-familj som matchar `color`; bilden skrivs genom en egen
  UNORM-RTV/SRV (aldrig sRGB — färgerna är redan kodade), med D32F-djup och
  vid MSAA en provtagen yta i samma UNORM-format som `ResolveSubresource` löser
  upp i `D3dRenderTarget::resolveFormat()` när målet lämnas (`bindTarget`;
  `readTarget` byter kanaler bara för BGRA). `sharedAttachments` kopierar den
  provtagna ytans, dess RTV:s och djupets COM-referenser — samma mått,
  `TargetDesc` och färgformat krävs — och `pushTarget` nollställer dem per
  mål. Ingen rensning vid import; fel ger ett ogiltigt mål och text.
  `insertCompletion` är en `D3D11_QUERY_EVENT` + `Flush`, pollad med
  `DONOTFLUSH`. `setExternalFramePacing` (`externalPacing_`, skild från
  storleksloopens `unsyncedPresent_`) presenterar med intervall 0 och
  `DXGI_PRESENT_DO_NOT_WAIT`; en present som inte hinner hoppas över.
- **GRAFIKKORTEN — `d3d11_gpu.cpp`** (REQ9_GRAFIKKORTET; också GPU-
  preferensen och adapterloggen, flyttade ur `d3d11_renderer.cpp`):
  `enumerateAdapters(f, probeKind)` går `EnumAdapters1` (index 0 = förvalet,
  det `D3D11CreateDevice(nullptr, HARDWARE)` tar — Intel UHD på Eriks
  Optimus-dator) med id ur `DXGI_ADAPTER_DESC1` (`VendorId`, `DeviceId`,
  `SubSysId`, `Revision`) och slaget ur KÄRNANS adaptertyp: D3DKMT, som
  MinGW saknar headern till — våra egna deklarationer, importstubbarna i
  libgdi32. **`KMTQAITYPE_ADAPTERTYPE` är 15, inte 14** (14 är ett hål i
  enumen, kontrollerat mot Wines header); ADAPTERADDRESS är 6 och ger
  `@bb:dd.f` vid dubbletter. Utan hybridflaggor PROBAR bara listan
  (`gfx::gpuAdapters`): en enhet utan kontext och dess
  `UnifiedMemoryArchitecture` — aldrig när ett fönster öppnas. `D3dRenderer::
  init` räknar upp EN gång utan prob: `chosenAdapter` (kortet `gpuAdapter()`,
  annars `preferredAdapter` = `IDXGIFactory6::EnumAdapterByGpuPreference`,
  loggat), enheten med `D3D_DRIVER_TYPE_UNKNOWN`, och reserverna HARDWARE och
  WARP som förut. `deviceName_` är den FAKTISKA adapterns `Description`,
  `adapterId_` dess LUID mot listan (också efter WARP; tomt utan träff).
  `GpuAdapter::localAdapterId` är LUID:en packad med `packLuid`.
- **ADAPTERLOGGEN (PRESTANDA A1, `logAdapter`):** EN gång per process listas
  varje adapter (id, namn, slaget när flaggorna känner det, VendorId, eget
  minne), och vid varje NY adapter (en annan LUID än förra enhetens) "D3D11:
  ritar med X [id] (preferensen Y, kortet Z)"; popuper, menyer och tooltips
  på samma adapter loggar inget.
- **MÄTPUNKTERNA (PRESTANDA A2):** `compileHlsl` är en privat MEDLEM (var en
  fri funktion) för räknarens skull: en MISS i processens bytekodcache läggs
  i `RenderStats::compileNs`, en träff kostar 0. `presentNs` mäts runt
  `Present`, `targetsCreated` i `createRenderTarget`. GPU-TIDEN är en ring om
  `kGpuQueries` (4) med en `D3D11_QUERY_TIMESTAMP_DISJOINT` och två
  `TIMESTAMP` var: `beginFrame` läser de färdiga med
  `D3D11_ASYNC_GETDATA_DONOTFLUSH` (`collectGpuQueries`, ingen väntan, 2–3
  frames senare) och börjar en ny när tidtagningen är på och en plats är
  ledig (`startGpuQuery`); `present` avslutar den FÖRE `Present`. En frame
  med `Disjoint` kastas. **DISJOINT kan vara klar FÖRE stämplarna** (Intel
  UHD: `S_FALSE` på båda) — då behåller platsen sin frame till nästa
  `collectGpuQueries`; att kasta den där gjorde att Intel aldrig mättes
  (profiler_gate P8, 0.2.0). Frågorna skapas i init (`initGpuQueries`) —
  misslyckas det är `gpuTimingSupported_` falskt. **D3 utgick** (PRESTANDA
  2026-10-17): två fönster som presenterar med vsync i följd halverade inte
  takten i `terrain_perf` (steg 24 mot 12), så varje fönster har kvar sin
  enhet och `Present(1, 0)`.

## Egna program (CANVAS_3D etapp 1 — `d3d11_program.cpp`)

Kontrakten (förspelet, platserna, blocken) står i
[../../include/gfx/CLAUDE.md](../../include/gfx/CLAUDE.md) vid `program.h`;
grind: G9 del C.

- **`createProgram`**: `programDescError`, sedan HLSL-källan (saknas den:
  "saknar HLSL-källa (Windows)"), och källan blir FÖRSPELET (`GlazeFrameData`,
  256 byte med `rayCenter` sist, som struct i `cbuffer GlazeFrame : register(b3)` — medlemmarna heter
  `glaze.…` som i GLSL och MSL, packningen är densamma — och `glaze_toClip`)
  + **`kHlslAdjustSrc`** (`glaze_colorAdjust`, REQ8 3 — samma text som
  Image-programmet klistrar in; ritytans Mesh-program kör den härifrån)
  + **`#line 1 "<namn>"`** + appens källa. NAMNET i `#line` är inte prydnad:
  med ett källnamn i D3DCompile:s argument gör kompilatorn det till en sökväg
  under arbetskatalogen ("C:\…\rad_sju(7,64)"), med `#line`-formen blir det
  "rad_sju(7,64)" (uppmätt). `compileHlsl` fick definitionerna (en
  nullterminerad `D3D_SHADER_MACRO`-lista) och ett `error` som tar emot
  kompilatorns text i stället för loggen; vs_4_0/ps_4_0. Ett ogiltigt program
  loggas EN gång, i createProgram.
- **REFLEKTIONEN (`D3DReflect`, `IID_ID3D11ShaderReflection` ur dxguid)**:
  vertexstegets indatasignatur blir `ShaderProgram::inputs` (ATTR<N> = plats
  N; UINT32/SINT32 = heltal; systemvärdena hoppas över) — en annan semantik
  än ATTR, eller en plats över 15, är ett fel. I båda stegen prövas att
  texturerna och samplerna ligger under `ProgramDesc::textures`, att inga
  andra cbuffrar än GlazeFrame och GlazeUser finns (en lös global hamnar i
  `$Globals` på b0 — UI:ts viewport-buffert sitter där; felet nämner den
  första variabeln) och att GlazeUser ligger i b4 och är EXAKT `userBytes`
  stort — annars hade en för liten b4, en obunden SRV eller UI:ts b0 gett
  skräp eller nollor, tyst.
- **INPUT LAYOUTEN** byggs per program och `layout.key()` och cachas i
  programmet (`mutable` — `MeshDraw::shader` är const), också en som inte gick
  att bygga (loggad en gång). Elementen heter "ATTR" med platsen som
  semantikindex; instansströmmen i slot 1 med `D3D11_INPUT_PER_INSTANCE_DATA`.
  **En TOM layout har INGEN input layout**: D3D11 vägrar en utan element
  (E_INVALIDARG, uppmätt) — helskärmspasset binder nullptr, och
  `customDrawOk` har redan krävt att programmet då inte läser något.
  Typfel (ett _UINT-format mot en float-indata) är bara en VARNING i D3D11 —
  därför prövar `layoutFits` slaget, inte CreateInputLayout.
- **RITNINGEN** (`drawCustomMesh`): flush, programmet (VS/PS, b3 i båda
  stegen, topologin) när ett ANNAT program tas i bruk — jämfört med
  löpnumret `curProgram_`, aldrig adressen —, input layouten, GlazeFrame
  (`MeshDraw::frame` med det bundna målets storlek i `target`, Map DISCARD),
  GlazeUser i programmets egen DYNAMIC b4, texturerna på t0… med var sin
  bilds sampler i BÅDA stegen, `applyMeshState`, strömmarna (slot 0 och 1 —
  instansdelen MED OFFSET, StartInstanceLocation alltid 0) och
  `DrawIndexed[Instanced]`/`Draw[Instanced]`. **`customSrvs_`**: texturerna
  sitter kvar efter anropet, och `bindTarget` nollar dem i båda stegen innan
  ett mål binds (samma läs/skriv-hazard som t0).

## Line3 och Glyph3 (CANVAS_3D etapp 3 — GLAZE_POS3)

- **SAMMA KÄLLA, TVÅ VARIANTER.** Line- och Glyph-källan kompileras en
  gång till med makrot `GLAZE_POS3` (`kPos3Defines` i d3d11_scene.cpp, en
  egen `D3D_SHADER_MACRO`-lista i d3d11_glyph.cpp, genom `compileHlsl`s
  definitioner): `line3Vs_/line3Ps_/line3Layout_` och
  `glyph3Vs_/glyph3Ps_/glyph3Layout_`, egna `Pipeline::Line3/Glyph3`.
  Layouterna läser `LineVertex3` respektive `GlyphVertex3` på deras offset
  (punkterna/ankaret som `R32G32B32_FLOAT`, resten som 2D-formatets). Ett
  program som inte går att skapa loggas och nollas — varianten ritar då
  inget, 2D-varianten lever. `drawMesh` skickar Line och Line3 genom samma
  väg (`pos3` väljer program och layout), Glyph och Glyph3 till
  d3d11_glyph.cpp.
- **LINE3:s VERTEXSHADER** är skärmlägets gren rad för rad med punkterna i
  xyz (`#else // GLAZE_POS3`): trimningen mot klippytan (nedan), skarvarna
  ur de projicerade grannarna, ändarna och strecken. Bredden: `halfWidthAt` —
  px rakt av, en bredd i världen × `pxPerWorldAt(c)` = x-radens del
  vinkelrätt mot w-raden genom w (HLSL:s `gViewProj[i]` är en RAD), samma
  tal som `gfx::pxPerUnitAt(View3D)`. Segmentet räknar BÅDA ändarnas halva
  bredd (`hwS`/`hwE`, efter trimningen) så ändarnas avstånd är samma linjära
  funktion i alla fyra hörnen, och `hw` är `noperspective` i POS3 (platt i
  2D). Ändarnas förlängning är EXAKT i skärmen: t = e·w²/(lin − e·w·q) med
  lin = ud·dc.xy·w − ud·c.xy·dc.w (perspektivdivisionen löst för
  världssträckan; bortom flyktpunkten siktar den på 1000 × djupet, ett
  segment längs blicken förlängs inte) — 2D-grenens linjära förlängning är
  orörd.
  Fragmentet: `world` är falskt i POS3, och en täckning ≤ 0 KASTAS så
  kantutjämningens marginal inte skriver djup.
- **GLYPH3** projicerar ankaret i xyz (`glyphToClip`) och gallrar det bakom
  närplanet som 2D-varianten; allt annat är Glyph-programmets.
- **LIGGANDE TEXT (CANVAS_3D etapp 9, `glyphflag::kGroundAligned` = 64)** —
  `glyphGround` i BÅDA varianterna: hörnet läggs i världsplanet vid ankaret
  (POS3 med ankarets z), baslinjen (cos, sin, 0) och "nedåt" dess
  vinkelräta, vänd när de två skärmriktningarna (`glyphScreenDir` —
  perspektivdivisionens derivata) speglar skärmens ram; skalan k = px per
  världsenhet vid ankaret längs en världsriktning vinkelrät mot blicken
  (blicken ur matrisens w-RAD, ortografiskt ur z-raden), så en em har sin
  storlek där och förkortas längs blicken. Kantutjämningens storlek
  (`shape.z`) är axlarnas geometriska medel. Formernas px-tillväxt och
  bildmarkörens proportioner gäller som förut — de ändrar `off` före
  grenen. G9 H5/H5b/H5c mäter den.
- **KLIPPYTAN — BÅDA LINJEGRENARNA (2D och POS3).** GPU:n får aldrig klippa
  ett linjeband: `trimSegment` flyttar segmentets ändar till närplanet, det
  bortre planet och en SKYDDSRAM en målstorlek utanför målets kanter (i den
  ordningen, varje plan `lerp(utanför, innanför, …)` i båda ändarnas vertex
  — paret blir bit för bit detsamma, och närplanets trimning är den gamla),
  och `keepInside` kortar ändarnas förlängning så att vertexen stannar
  innanför klippdjupet. Skälet är GL:s: llvmpipe interpolerar de
  skärmlinjära (noperspective) värdena fel i ett klippt band när ändarnas w
  skiljer mycket — G9 D2 (en ände bakom kameran) och G1 (rutnätets linjer
  bortom 3D-målets ändliga bortre plan) föll där. D3D11 klipper rätt, men
  källan är en och samma i tre backends, och kartkamerans bortre plan
  ligger i oändligheten (w − z konstant), så 2D-vägen trimmas aldrig mot
  det. **Båda ändarna utanför SAMMA plan ⇒ segmentet kastas** (`outside`),
  och en rund skarv eller ände bakom närplanet likaså — FÖRE förlängningen:
  med w < 0 blev förlängningens tak `tMax` negativt, vann över noll och sköt
  hörnet förbi ögat, så linor bakom kameran syntes som en solfjäder genom
  flyktpunkten (GL har samma risk fast GL alltid klipper i z — felet sitter
  i vertexen, inte i `DepthClipEnable`). Grind: G9 L1.
