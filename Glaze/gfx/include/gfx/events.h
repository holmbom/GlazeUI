#pragma once
#include <cstdint>
#include <type_traits>

namespace gfx {

// Backend-neutralt event-system. Backends översätter sina OS-/bibliotek-
// events till gfx::Event i waitEvent/pollEvent. Events som inte har någon
// motsvarighet här blir EventType::None — de behandlas som "inget hände"
// och triggar ingen omritning (händelsestyrd rendering, hård regel 1).

enum class EventType {
    None,
    Quit,                        // AVSLUTA APPEN (WM_QUIT m.fl.) —
                                 // app-nivå, windowId är alltid 0
    WindowCloseRequested,        // DET HÄR fönstret begär stängning (kryss,
                                 // Alt+F4) — per fönster, windowId stämplat

    // Fönsterhändelser som kräver omritning — AppWindow::handleEvent
    // invalidatar på SAMTLIGA. Tappas en i backend-översättningen blir
    // symtomet "UI:t uppdateras inte förrän jag rör musen".
    WindowExposed,
    WindowResized,
    WindowPixelSizeChanged,
    WindowShown,
    WindowRestored,
    WindowMaximized,
    WindowFocusGained,
    WindowFocusLost,
    WindowDisplayScaleChanged,
    // SKÄRMEN fönstret står på kan ha bytts, eller dess läge ändrats:
    // `Window::displayMetrics()` svarar annorlunda — en millimeter är ett
    // annat antal px. Skalan bär sitt eget event ovan; det här gäller den
    // FYSISKA tätheten.
    WindowDisplayChanged,

    // OS:et bytte ljus/mörkt läge. Också en invalidate-händelse: repainten
    // resolvar aktivt tema på nytt (System-läget följer OS:et) — själva
    // temavärdet läses via gfx::systemTheme(), inte ur eventet.
    SystemThemeChanged,

    MouseMotion,
    MouseButtonDown,
    MouseButtonUp,
    MouseLeave,                  // musen lämnade fönstret

    // MUSENS CAPTURE TOGS: fönstret hade musens capture för ett pågående
    // tryck, och OS:et
    // eller en annan app tog den — inget `MouseButtonUp` kommer för de
    // knappar som hålls nu. Ingen nyttolast. INTE input: det släpps igenom
    // modalfiltret (ett fönster bakom en modal kan ha ett tryck att
    // avsluta), och glaze::AppWindow släpper då fönstrets tryck och
    // capture — samma bokföring som vid fokusförlusten, utan att röra
    // fokus, grab eller tangenter.
    //
    //   Windows  `WM_CAPTURECHANGED` — utom vårt eget släpp
    //            (`releaseMouseCapture`, knappsläppet), capture till fönstret
    //            självt och ett UTTRYCKLIGT övertagande
    //            (`Window::captureMouse` — ett tryck som lämnas över inom
    //            appen). Ett annat av appens fönster som tar capture på något
    //            annat sätt (en OS-loop, ett vanligt SetCapture) är en
    //            förlust. `WM_CANCELMODE` (Alt+Tab, en systemdialog) kommer
    //            samma väg: DefWindowProc släpper capture och då kommer
    //            meddelandet
    //   macOS    aldrig — AppKit levererar släppet till vyn där trycket
    //            började, och fokusförlusten bär resten
    //   X11      aldrig — ingen annan klient kan ta en aktiv grab mitt i
    //            vår implicita, och `FocusOut` bär resten
    //   Wayland  `wl_pointer.leave` medan ett LEVERERAT tryck hålls (inte
    //            ett som kompositörens flytt tog) — utom när nästa enter i
    //            samma dispatch-omgång är vår egen popup i samma fokusgrupp:
    //            trycket öppnade den, och släppet kommer dit
    //   headless aldrig
    //
    // glaze ger varje widget som hade fått ett släpp `Widget::mouseCancel`
    // (ritytan också `onMouseCancel`) — ett avbrott är inte ett släpp.
    MouseCaptureLost,

    // KeyDown BÄR TEXTEN trycket SKRIVER:
    // Event::text, UTF-8 — exakt det (de) TextInput-event som backenden köar
    // DIREKT EFTER i samma fönster. Tom för modifierare, navigering,
    // funktionstangenter, styrtecken (Enter, Tab, Escape, Backspace), en död
    // tangent (dess tecken kommer med nästa tryck) och en kombination med
    // Ctrl, Alt eller Cmd/Win/Super — men inte AltGr (Windows Ctrl+Alt,
    // Linux Level3) eller Option på macOS, som är skrivtangenter.
    // Upprepningen bär texten igen; KeyUp har ingen. Inte IME (inget stöd),
    // Windows Alt+numpad-koden (tecknet kommer vid Alt-släppet utan KeyDown)
    // eller ett WM_CHAR en annan app postar. Per backend: D3D11 hämtar
    // WM_KEYDOWN:s WM_CHAR ur kön, Wayland/X11 räknar texten före KeyDown
    // (`xkbTypesText`), Metal fångar `insertText:` under `interpretKeyEvents`.
    KeyDown,
    KeyUp,

    TextInput,    // skrivtecken som UTF-8 (Event::text) — inte kommandon
    MouseWheel,   // Event::wheelX/wheelY i rader (+ muspos i mouseX/mouseY)
    Timer,        // Event::timerId — startad via Window::startTimer; windowId 0
                  // = appens timer (gfx/timer.h)

    // TRÅDSÄKER VÄCKNING: `gfx::postWakeup()` väcker
    // pumpen från en FRÄMMANDE tråd. windowId är alltid 0 — eventet hör
    // till APPEN och inte till ett fönster, precis som Quit.
    //
    // Eventet BÄR INGEN NYTTOLAST och ska inte invalidera något: dess
    // enda uppgift är att få `waitEvent` att RETURNERA, så pumpen når sin
    // svans och dränerar `glaze::App`:s postkö. Ett rent OS-wakeup räcker
    // inte — `waitEvent` loopar `for(;;) { if (popQueued(e)) return; …}`
    // i alla tre backends, så ett meddelande som bara dispatchas skickar
    // loopen rakt tillbaka i blockering. Backenden måste alltså BÅDE
    // väcka OS-pumpen OCH köa det här eventet.
    Wakeup,

    // STYRPLATTANS NYPNING OCH VRIDNING — Event::phase,
    // gestureScale, gestureRotation och pekaren i mouseX/mouseY. Se
    // kommentaren vid `kPrecisePxPerRow`.
    Gesture,

    // SYSTEMFÄLTETS IKON — Event::trayId (id:t
    // ur `gfx::setTrayIcon`, gfx/tray.h) och Event::trayAction. windowId är
    // ALLTID 0: ikonen hör till APPEN och inte till ett fönster, precis som
    // Quit och Wakeup, och `glaze::App` routar eventet på trayId.
    //
    // mouseX/mouseY är ANKARET för Click, DoubleClick och ContextMenu — var
    // det hände, i PLACERINGSRYMDEN (`WindowDesc::posX`/`setPosition`s rymd:
    // Win32 fysiska px, AppKit punkter med origo uppe till vänster på
    // huvudskärmen) och INTE i ett fönsters klientyta, för det finns inget
    // fönster. 0 för Enter, Leave och Tick.
    Tray,
};

// Fönster-identitet i events. 0 = ogiltigt/okänt (t.ex. Quit utan fönster).
// Backends stämplar fältet; glaze::App routar events till rätt fönster via
// Window::id(). Jämför aldrig mot något annat än id() — talet är backendens.
using WindowId = std::uint32_t;

enum class MouseButton { None, Left, Middle, Right };

// KONTRAKTET — läs det innan du binder
// en genväg, för det finns tre rimliga svar och bara ETT gäller här:
//
//   `Key` är **LEVEL-0-SYMBOLEN i användarens AKTIVA layout** — den symbol
//   tangenten ger UTAN modifierare.
//
// Alltså varken den fysiska tangentpositionen (US-layoutens etikett) eller
// den symbol som FAKTISKT producerades efter Shift. Skälen, i ordning:
//
//   1. `Key` är till för KOMMANDON. Tecken kommer via `EventType::TextInput`
//      (den vägen ger numpad och svensk kommadecimal gratis), och att låta
//      `Key` betyda samma sak hade gjort de två redundanta.
//   2. Ett `Key`-värde får inte FLIMRA när Shift trycks. Med "producerad
//      symbol" blir Shift+1 `Exclam` i stället för `N1`, och då går det
//      inte att skriva `if (k == Key::N1 && (mods & kModShift))`.
//      Bokstäverna har alltid folded så (A och a är båda `Key::A`) —
//      gäller samma regel för hela enumet.
//   3. Alla tre plattformarna kan svara på PRECIS den frågan utan
//      dead-key-minor: xkb `xkb_keymap_key_get_syms_by_level(..., 0)`,
//      Windows `MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR)`, macOS
//      `charactersIgnoringModifiers`/`UCKeyTranslate` med noll modifierare.
//
// **PRISET, utskrivet:** en symbol som är SKIFTAD i en layout når aldrig
// sin egen post där. På svensk layout är `+` en egen tangent ⇒ `Key::Plus`;
// på amerikansk är den Shift+`=` ⇒ **`Key::Equals` + `kModShift`**. En app
// som vill ha "zooma in" binder därför BÅDA:
//
//     const bool zoomIn = k == Key::Plus ||
//                         (k == Key::Equals && (mods & kModShift));
//
// Samma sak gäller `Percent` och `Asterisk`: de finns i enumet för
// fullständighet, men på de flesta layouter är de skiftade och levereras
// då som `N5` respektive `N8`/`KP_Multiply`. De TESTAS inte.
//
// NUMPADEN är ett UNDANTAG från level-0-regeln, och det måste den vara:
// numpadens level 0 är navigationssymbolerna (`KP_End` …), så en ren
// level-0-läsning hade gjort numpad-siffrorna omöjliga att ta emot.
// Numpaden löses därför ur tangentbordets FAKTISKA läge: NumLock PÅ ⇒
// siffran (`KP_N1`), NumLock AV ⇒ navigationssymbolen, och den har sedan
// en EGEN post (`KP_End`) i stället för att
// smälta ihop med den dedikerade tangenten (`End`). Numpad-1 är alltså
// skiljbar från 1, och numpad-End från End.
//
// **NUMPADEN ÄR PLATTFORMSBEROENDE, och det är ett BESLUT.**
// Faktiskt-läge-regeln ovan gäller **Windows och
// Linux**. **macOS levererar ALLTID siffrorna** — numpad-1 är `KP_N1`
// även med NumLock av, och `KP_End` m.fl. kommer ALDRIG därifrån. Det är
// inte en bugg i backenden utan en följd av att macOS inte HAR något
// NumLock-begrepp: `NSEvent` bär ingen sådan flagga, tangenten
// `kVK_ANSI_KeypadClear` är ren Clear och OS:et gör aldrig om numpaden
// till navigationstangenter. Det finns alltså inget "faktiskt läge" att
// följa där, och en emulerad flagga i backenden hade varit ett påhittat
// tillstånd som ingen app kan lita på.
//
// **VAD SOM LEVERERAS VAR** (grindat: tests/numpad_gate.cpp — Windows
// genom WndProc:en, Linux mot en riktig xkb-keymap):
//
//   tangent     Windows/Linux   Windows/Linux        macOS
//               NumLock PÅ      NumLock AV           (alltid)
//   numpad-1    `KP_N1`         `KP_End`             `KP_N1`
//   numpad-7    `KP_N7`         `KP_Home`            `KP_N7`
//   numpad-9    `KP_N9`         `KP_PageUp`          `KP_N9`
//   numpad-3    `KP_N3`         `KP_PageDown`        `KP_N3`
//   numpad-8    `KP_N8`         `KP_Up`              `KP_N8`
//   numpad-2    `KP_N2`         `KP_Down`            `KP_N2`
//   numpad-4    `KP_N4`         `KP_Left`            `KP_N4`
//   numpad-6    `KP_N6`         `KP_Right`           `KP_N6`
//   numpad-0    `KP_N0`         `KP_Insert`          `KP_N0`
//   numpad-,    `KP_Decimal`    `KP_Delete`          `KP_Decimal`
//   numpad-5    `KP_N5`         `KP_Begin`           `KP_N5`
//   numpad-↵    `Return`        `Return`             `Return`
//
// De DEDIKERADE tangenterna (End, Home, PgUp, PgDn, Insert, Delete,
// pilarna) ger sin egen post överallt, oavsett NumLock. Insert på macOS är
// `kVK_Help`-platsen — där ett PC-tangentbord har sin Insert — se
// doc/os/macos/TESTS.md.
//
// Konsekvensen för en APP som binder numpadens NAVIGERING (en genväg, en
// egen `event()`-krok): bind både `End` och `KP_End` — de är olika poster.
// WIDGETARNA behöver inte det: `AppWindow` viker `KP_End` till `End`
// (`navigationKey` nedan) innan den fokuserade widgeten, grab-toppen och
// en öppen meny får tangenten, så en lista bläddrar med numpaden precis
// som med de dedikerade tangenterna. Appens `event()`-krok och
// acceleratorerna ser den OVIKTA posten. Konsekvensen för en app som vill
// ha SIFFRORNA: `KP_N1` räcker, och på macOS får den dem även utan NumLock.
// `KP_Enter` är MEDVETET inte en egen post: den fälls ihop med `Return`,
// eftersom ingen app vill skilja dem och varje aktiveringsväg
// (Button/Checkbox) fortsätter fungera oförändrat.
enum class Key {
    Unknown,
    Return, Escape, Tab, Backspace, Space, Delete,
    Left, Right, Up, Down, Home, End,
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    N0, N1, N2, N3, N4, N5, N6, N7, N8, N9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    // Skiljetecken. Namnen är SYMBOLENS, inte tangentens
    // position — det följer av level-0-kontraktet ovan.
    Minus, Plus, Equals, Comma, Period, Slash, Backslash,
    Semicolon, Apostrophe, LeftBracket, RightBracket, Grave,
    Percent, Asterisk,

    // Numeriska tangentbordet. Skilt från siffer-raden med FLIT: en
    // kalkylator vill kunna behandla dem olika, och utan den skillnaden
    // vore posterna meningslösa. NumLock av ⇒ tangenterna levereras som
    // sina navigationsroller (`KP_End` m.fl. nedan). `KP_Enter` finns
    // inte — se noten ovan.
    KP_N0, KP_N1, KP_N2, KP_N3, KP_N4,
    KP_N5, KP_N6, KP_N7, KP_N8, KP_N9,
    KP_Add, KP_Subtract, KP_Multiply, KP_Divide, KP_Decimal,

    // NAVIGATIONSKLUSTRETS resterande dedikerade tangenter. Utan dem hade
    // numpadens
    // `KP_PageUp`/`KP_PageDown`/`KP_Insert` nedan inte haft någon tvilling
    // att bindas tillsammans med.
    PageUp, PageDown, Insert,

    // NUMPADEN I NAVIGATIONSLÄGE — NumLock AV på Windows och
    // Linux, aldrig på macOS (se NUMPADEN ovan). Skilda från de dedikerade
    // tangenterna med FLIT, så att en app kan binda dem olika; widgetarna
    // ser dem vikta till tvillingen (`navigationKey` nedan).
    //
    // `KP_Begin` är MITTTANGENTEN (numpad-5). Den är med därför att båda
    // plattformarna med ett NumLock levererar en EGEN symbol för den i
    // navigationsläget — xkb `KP_Begin`, Win32 `VK_CLEAR` — och en tangent
    // som har en symbol ska ha en post, annars blir den `Unknown`. Namnet
    // är xkb:s: "Clear" är redan ett annat begrepp på macOS (numpadens
    // NumLock-plats). Den har ingen dedikerad tvilling och viks därför inte.
    KP_Home, KP_End, KP_Left, KP_Right, KP_Up, KP_Down,
    KP_PageUp, KP_PageDown, KP_Insert, KP_Delete, KP_Begin,
};

// SISTA posten — för den som måste ITERERA enumet (`glaze::
// parseCanonicalKey`). `Key` har med flit ingen `Count`-post: den hade
// blivit ett giltigt värde i varje switch. En ny post läggs SIST, och den
// här konstanten flyttas med den i samma ändring.
inline constexpr Key kLastKey = Key::KP_Begin;

// NUMPADENS NAVIGATIONSROLL → den dedikerade tvillingen (`KP_End` → `End`);
// allt annat, siffrorna och `KP_Begin` inräknade, kommer tillbaka orört.
// `AppWindow` viker med den innan en widget får tangenten — se NUMPADEN
// ovan för varför appens egen krok och acceleratorerna INTE gör det.
constexpr Key navigationKey(Key k)
{
    switch (k) {
    case Key::KP_Home:     return Key::Home;
    case Key::KP_End:      return Key::End;
    case Key::KP_Left:     return Key::Left;
    case Key::KP_Right:    return Key::Right;
    case Key::KP_Up:       return Key::Up;
    case Key::KP_Down:     return Key::Down;
    case Key::KP_PageUp:   return Key::PageUp;
    case Key::KP_PageDown: return Key::PageDown;
    case Key::KP_Insert:   return Key::Insert;
    case Key::KP_Delete:   return Key::Delete;
    default:               return k;
    }
}

// Modifierarmask (Event::mods). Ctrl/Shift/Alt täcker både vänster- och
// högertangenten. Cmd är Command på Mac, Windows-tangenten på Windows,
// Super/GUI på Linux — egen bit så handlers kan skilja Cmd från Ctrl
// plattformsoberoende.
//
// VILKA EVENTS BÄR DEN: tangenterna (`KeyDown`/`KeyUp`), musens
// `MouseButtonDown`/`MouseButtonUp`/`MouseMotion` — Ctrl-klick och
// Shift-klick i en lista med flerval går inte att skilja från ett vanligt
// klick annars — och `MouseWheel` och `Gesture`: Ctrl/Cmd + styrplattans
// tvåfingersdrag kretsar på ritytan, och ett drag direkt efter ett
// Ctrl-tryck har ingen mushändelse emellan som kunde ha burit masken.
// Pekarens mask är läget NÄR eventet hände (X11 `state`/XI2
// `mods.effective`, Win32 `GetKeyState` i meddelandets takt, Wayland den
// senaste `wl_keyboard.modifiers`, macOS `modifierFlags`). En
// MODIFIERARTANGENTS eget event (`Key::Unknown` — enumet har inga poster för
// dem) bär läget EFTER tangenten på alla plattformar: trycket av Ctrl har
// `kModCtrl`, släppet inte. X11 och Wayland levererar läget FÖRE, så
// backenden lägger på tangentens egen bit (släpps den ena av två hållna Ctrl
// försvinner biten där ändå, till nästa event med mask); macOS skickar ingen
// `keyDown:` för dem, så `flagsChanged:` blir KeyDown/KeyUp. FÖRBEHÅLLET: på
// Wayland når modifierarna bara en yta som har TANGENTBORDSfokus — utan
// fokus är masken 0, så ett Ctrl-klick i ett INAKTIVT fönster kan komma
// utan Ctrl. Övriga events (text, timer, fönsterhändelser) bär den inte (0).
inline constexpr std::uint32_t kModCtrl  = 1u << 0;
inline constexpr std::uint32_t kModShift = 1u << 1;
inline constexpr std::uint32_t kModAlt   = 1u << 2;
inline constexpr std::uint32_t kModCmd   = 1u << 3;

// STYRPLATTAN. Två sorters hjul och en gest:
//
// * `MouseWheel` med `precise == false` är ett MUSHJUL — rader, ofta hela
//   klick. Ritytan ZOOMAR på det.
// * `MouseWheel` med `precise == true` är STYRPLATTANS TVÅFINGERSDRAG (och
//   Magic Mouse/kontinuerliga hjul som OS:et märker så): raderna är
//   LOGISKA px delat med `kPrecisePxPerRow`, så en lista rullar lika långt
//   som med ett mushjul, medan ritytan kan få tillbaka exakta px och PANORERA.
//   Innehållet flyttar sig på skärmen (−wheelX, +wheelY) × kPrecisePxPerRow
//   logiska px — samma tecken som en rullad lista. OS:ets val av "naturlig
//   rullning" är redan tillämpat av backenden, och glaze vänder aldrig
//   tillbaka det: appens vändning är RELATIV mot OS:et.
//   AXLARNA ÄR ENHETENS: Shift byter aldrig axel i fasaden — macOS byte av
//   ett mushjul med Shift görs om i backenden — utan glaze gör det för det
//   som rullar, och ritytans bindningar ser hjulets egen axel med Shift.
//   `phase` säger Begin/Update/
//   End när OS:et vet det och `Momentum` för OS:ets EGEN tröghet efter att
//   fingrarna släppt (macOS, Windows Direct Manipulation); None = okänt.
// * `Gesture` är NYPNING OCH VRIDNING — en sekvens Begin → Update… → End
//   (eller Cancel). `gestureScale` är den MULTIPLIKATIVA ändringen sedan
//   förra eventet (> 1 = isärdragna fingrar = zooma in), `gestureRotation`
//   grader sedan förra eventet, POSITIVT = MEDURS på skärmen. Ett event kan
//   bära båda; det ena är då 1 resp. 0. Backenden SLÅR IHOP plattformens
//   källor till EN sekvens (macOS: magnify + rotate, Windows: Direct
//   Manipulation + råa HID-kontakter) — Begin när den första startar, End
//   när den sista slutat. mouseX/mouseY är PEKAREN (den står still under en
//   gest), och den är gestens ankare.
inline constexpr float kPrecisePxPerRow = 10.0f;

enum class GesturePhase : std::uint8_t {
    None,       // okänt (ett mushjul har ingen fas)
    Begin,
    Update,
    End,
    Cancel,     // gesten avbröts — ändringarna hittills står kvar
    Momentum,   // OS:ets tröghet efter att fingrarna lyfts (bara MouseWheel)
};

// SYSTEMFÄLTETS HÄNDELSER — Event::trayAction för
// `EventType::Tray`. Backenden översätter OS:ets egna:
//
//   Click        Windows NIN_SELECT/NIN_KEYSELECT (v4 — att föredra framför
//                råa musklick, och tangentbordets Enter kommer samma väg);
//                macOS vänsterklick
//   DoubleClick  Windows WM_LBUTTONDBLCLK; macOS clickCount == 2. Ett
//                dubbelklick ger Click OCH sedan DoubleClick (WinForms, Qt) —
//                Windows extra NIN_SELECT efter dubbelklicket sväljs
//   ContextMenu  Windows WM_CONTEXTMENU (högerklick, Skift+F10 och
//                menytangenten); macOS högerklick eller Ctrl+klick
//   Enter/Leave  pekaren över/ut. Windows mäter Leave själv och kan säga det
//                upp till 100 ms sent
//   Tick         ikonens timer (`gfx::setTrayTimer`)
//
// Backenden behöver inte hålla hovertillståndet: Enter två gånger eller
// Leave utan Enter är tillåtet, och mottagaren (glaze::TrayIcon) släpper
// dubbletterna.
enum class TrayAction : std::uint8_t {
    Click,
    DoubleClick,
    ContextMenu,
    Enter,
    Leave,
    Tick,
};

// Event köas per värde i backendernas event-köer — håll
// typen trivialt kopierbar: TextInput-texten är därför en fast UTF-8-
// buffert, inte en std::string. Kontrollen står under structen.
struct Event {
    EventType     type     = EventType::None;
    WindowId      windowId = 0;    // avsändande fönster (0 = okänt)
    float         mouseX   = 0.0f; // fysiska px (mus-events inkl. MouseWheel);
    float         mouseY   = 0.0f; // Tray: placeringsrymden (se EventType::Tray)
    MouseButton   button   = MouseButton::None;   // mus-knapp-events
    Key           key      = Key::Unknown;        // tangent-events
    std::uint32_t mods     = 0;    // kMod*-mask (tangenter, mus, hjul, gest)
    float         wheelX   = 0.0f; // MouseWheel: rader, positivt = höger
    float         wheelY   = 0.0f; // MouseWheel: rader, positivt = uppåt
    bool          precise  = false; // MouseWheel: styrplatta (se ovan)
    GesturePhase  phase    = GesturePhase::None; // MouseWheel (precis) + Gesture
    float         gestureScale    = 1.0f; // Gesture: faktor sedan förra eventet
    float         gestureRotation = 0.0f; // Gesture: grader sedan förra, medurs +
    // Timer: id ur Window::startTimer — eller, med windowId 0, appens
    // körnings-id ur gfx::startAppTimer. 64 bitar för det senare: körnings-
    // id:t återanvänds aldrig (gfx/timer.h), och ett fönsters 32-bitars id
    // ryms.
    std::uint64_t timerId  = 0;
    // Tray: ikonens id (`gfx::TrayId` ur gfx/tray.h) och vad som hände.
    std::uint32_t trayId     = 0;
    TrayAction    trayAction = TrayAction::Click;
    char          text[32] = {};   // TextInput och KeyDown: UTF-8, NUL-terminerad
                                   // (KeyDown: texten trycket skriver, se KeyDown)
};

// Kravet ovan, kontrollerat: ett nytt fält som inte är trivialt kopierbart
// (en std::string) blir ett kompileringsfel här i stället för en tyst
// förändring av varje backends kö.
static_assert(std::is_trivially_copyable_v<Event>,
              "gfx::Event måste vara trivialt kopierbar");

} // namespace gfx
