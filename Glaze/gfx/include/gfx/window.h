#pragma once
#include <climits>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "gfx/events.h"
#include "gfx/geometry.h"
#include "gfx/renderer.h"

namespace gfx {

// Vem ritar fönsterramen?
//   Native = OS:ets vanliga titelrad och knappar.
//   Custom = ramlöst fönster; vi ritar titelrad, min/max/close och
//            sköter drag/resize via backendens hit-test. Identiskt utseende
//            överallt.
//   None   = ramlöst UTAN chrome: ingen titelrad, ingen drag/resize från
//            ramverket — fri yta (transparenta fönster, popups/menyer).
//            Appen sköter all interaktion själv.
enum class Chrome { Native, Custom, None };

// FÖNSTRETS LÄGE (WinForms `FormWindowState`)
// — startläget (`WindowDesc::startState`) och läget i `placement()`.
enum class WindowState : std::uint8_t { Normal, Minimized, Maximized };

// Plattformsdefault: eget chrome (Custom) på ALLA OS — Glaze ritar
// fönster-chromet självt så apparna ser identiska ut på Windows/Mac/Linux
// (visionen). Vill en app ha OS:ets titelrad sätter den desc.chrome =
// Chrome::Native explicit.
constexpr Chrome defaultChrome()
{
    return Chrome::Custom;
}

// Sentinel för WindowDesc::posX/posY: låt OS:et välja position.
inline constexpr int kWindowPosDefault = INT_MIN;

class Window;

struct WindowDesc {
    std::string title = "App";

    // Klientstorlek i LOGISKA px: backends
    // multiplicerar SJÄLVA till sin fysiska enhet (Win32 × dpi/96, Mac
    // punkter ≈ logiska, Wayland ytenheter × textskalning, X11 × skala).
    // Samma tal ger samma synliga fönsterstorlek på alla DPI.
    int width  = 1024;
    int height = 640;

    Chrome chrome = defaultChrome();

    // Ägarfönster (Z-ordning/minimering följer ägaren — Win32:
    // hWndParent-ägare). Ägarskap, INTE koordinater: posX/posY
    // är alltid skärmkoordinater, aldrig relativa ägaren. Måste vara ett
    // initierat fönster ur samma backend (alla är det — en backend byggs in).
    Window* owner = nullptr;

    // Skärmposition i OS:ets placeringsrymd (Win32: fysiska px, AppKit:
    // punkter — mixed-DPI gör globala fysiska px odefinierade på
    // Mac). Appkod räknar ALDRIG i rymden själv: hämta värden ur
    // Window::clientToScreen. kWindowPosDefault = OS:et väljer.
    int posX = kWindowPosDefault;
    int posY = kWindowPosDefault;

    // Fönstret tar inte fokus när det visas (popups/menyer —
    // WS_EX_NOACTIVATE resp. nonactivating panel på Mac). PERMANENT: också
    // ett klick i fönstret lämnar fokus där det var.
    bool noActivate = false;

    // FÖRSTA VISNINGEN AKTIVERAR INTE — ett STARTVÄRDE
    // som läses vid fönstrets FÖRSTA synlighet (init, eller den första
    // `setVisible(true)` efter `hidden`). Därefter är fönstret vanligt
    // aktiverbart — skilt från `noActivate`, som gäller för alltid. Ett
    // flytande dockningsfönster som lossnar mitt i ett drag stjäl alltså
    // inte fokus från fönstret som håller trycket, och vid en dold start
    // behåller huvudfönstret aktiveringen.
    //
    // DEGRADERINGSTABELLEN — sex implementationer, och två av dem lovar inte
    // en inaktiv visning:
    //
    //   Windows  den första ShowWindow med SW_SHOWNOACTIVATE (ett maximerat
    //            startläge aktiverar ändå — Windows saknar en inaktiv
    //            maximering; minimerat är redan SW_SHOWMINNOACTIVE)
    //   macOS    den första `orderFront:` i stället för
    //            `makeKeyAndOrderFront:`
    //   X11      `_NET_WM_USER_TIME` = 0 före den första mappningen (EWMH:
    //            WM:en ska inte ge fönstret fokus) — WM:en kan välja annat
    //   Wayland  ingen protokollväg — dokumenterad no-op; kompositören väljer
    //   headless inget att visa
    //
    // Ett attribut och ingen ren virtuell: Linux-proxyn skickar `desc`
    // orört, så proxyfällan (en glömd vidarebefordran) finns inte.
    bool showInactive = false;

    // Verktygsfönster: ingen aktivitetsfälts-post (WS_EX_TOOLWINDOW resp.
    // utility-panel på Mac).
    bool toolWindow = false;

    // AKTIVITETSFÄLTET (WinForms
    // `ShowInTaskbar`) — false: ingen knapp i aktivitetsfältet. Ett
    // STARTVÄRDE som läses i init och aldrig växlas i drift: Windows kräver
    // dölj–byt–visa för en växling, och "stäng till systemfältet" behöver den
    // inte (ett dolt fönster har ingen knapp). Skilt från `toolWindow` ovan,
    // som dessutom tar fönstret ur Alt+Tab.
    //
    // DEGRADERINGSTABELLEN — en del av kontraktet:
    //
    //   Windows  ett OFÄGT fönster får ett eget DOLT ÄGARFÖNSTER (Microsofts
    //            recept och WinForms väg) — Alt+Tab står kvar, och flera
    //            fönster utan knapp syns alla där; ett ägt fönster och en
    //            popup har ingen knapp ändå
    //   macOS    ingen knapp per fönster — dockan är APPENS
    //            (`gfx::setShowInDock`, platform.h); false loggas en gång
    //   Linux    inte byggt (dummy) — fönstret behåller sin post; false
    //            loggas en gång i proxyns init, som täcker Wayland, X11 och
    //            headless
    //   headless inget aktivitetsfält
    bool showInTaskbar = true;

    // Per-pixel-genomskinligt fönster (fri geometri — skrivbordet syns
    // genom allt som ritas med alfa < 1). TVINGAR chrome = None (fri
    // geometri och titelrad är oförenliga; loggas om annat satts).
    // KONTRAKT: klick i genomskinliga pixlar släpps IGENOM till fönstret
    // under — hit-testen läser vår egen retained-alfa, så formen (inte
    // fönsterrekten) avgör vad som är klickbart. Windows/D3D11
    // (composition-swapchain + DirectComposition) är referensen; en
    // backend som inte kan släppa igenom klick är en dokumenterad
    // degradering. Opaka fönster rör inte den här vägen alls.
    bool transparent = false;

    // MINSTA/STÖRSTA klientstorlek i LOGISKA px — 0 =
    // obegränsat, per axel. `min == max` på en axel LÅSER den axeln, och
    // det är hela poängen: "ej ändringsbar" behöver inget eget attribut.
    //
    // Fasaden lovar att OS:et själv vägrar dra utanför gränserna. Att
    // emulera det genom att fånga `WindowResized` och kalla `setSize`
    // tillbaka är UTTRYCKLIGT förkastat — det är att slåss med
    // fönsterhanteraren, med flimmer och studsande kant, och regel 1
    // förbjuder just den sortens konst.
    //
    // NOTERA att `setMaximizeBox(false)` INTE gör det här: den gör
    // `show`/`hide` på CaptionButton-WIDGETARNA i vår namnlist medan
    // OS-fönstret är oförändrat och kanten fortfarande går att dra i.
    // Det är kosmetik — och sämre än ingenting, eftersom UI:t då lovar
    // något det inte kan hålla.
    int minWidth  = 0;
    int minHeight = 0;
    int maxWidth  = 0;
    int maxHeight = 0;

    // ALLTID ÖVERST — fönstret ligger över andra appars
    // fönster tills flaggan tas bort. Startvärdet för `setTopmost` nedan;
    // se degraderingstabellen där.
    bool topmost = false;

    // Dolt fönster: OS-fönstret skapas men visas ALDRIG —
    // init hoppar setVisible/ShowWindow. Renderer
    // och retained-RT finns som vanligt, så en engångs-frame kan ritas och
    // läsas tillbaka via captureFrame (glaze-render). setVisible(true)
    // tar fram ett dolt fönster i efterhand som vanligt — och den FÖRSTA
    // `setVisible(true)` gör allt den första visningen i init hade gjort
    // (startläget, `showInactive`, Windows styrplatta). glaze skapar VARJE
    // fönster utom popuperna och offscreen-fönstren så, fyrar `onLoad` och
    // visar det sedan — vägen är alltså den vanliga.
    bool hidden = false;

    // STARTLÄGET — tillämpas vid FÖRSTA SYNLIGHETEN:
    // i init för ett fönster som visas direkt, i den första
    // `setVisible(true)` för ett som startade dolt (`hidden`). Aldrig för ett
    // fönster som aldrig visas (offscreen) och aldrig för en popup — glaze
    // skickar Normal för dem, och en modal startar aldrig minimerad.
    //
    //   Windows  den första `ShowWindow` med SW_SHOWMAXIMIZED respektive
    //            SW_SHOWMINNOACTIVE, verifierad med IsZoomed/IsIconic och
    //            ett andra anrop om STARTUPINFO tog den första
    //   macOS    `zoom:`/`miniaturize:` efter första `orderFront`
    //   X11      `_NET_WM_STATE_MAXIMIZED_*` respektive `WM_HINTS.
    //            initial_state = IconicState` på det omappade fönstret
    //   Wayland  `xdg_toplevel_set_maximized` före den commit som mappar
    //            ytan; Minimized KAN INTE (xdg-shell kan varken fråga efter
    //            eller ta bort minimering) — loggas en gång, start i Normal
    //   headless inget fönster att visa — ignoreras
    WindowState startState = WindowState::Normal;

    // Popup-kontraktet: Wayland
    // kan varken sätta eller läsa skärmpositioner, så popup-placering är
    // FÖRÄLDER-RELATIV. popupAnchorWindow (= ägarfönstret) + ankarrectet i
    // ägarens KLIENT-FYSISKA px (samma rymd som contentArea/resolve) ber
    // backenden: "placera fönstret under ankarrectet; vänd uppåt/klampa i
    // sidled när skärmen tar slut". Flip/slide-ansvaret bor i BACKENDEN:
    // Wayland via xdg_positioner (kompositören sköter flip/slide),
    // D3D11/Metal/X11 konverterar internt via clientToScreen/screenArea
    // och gör själva flip/klamp-logiken.
    // Satt anchorWindow ⇒ posX/posY ignoreras. width/height är fönstrets
    // LOGISKA klientstorlek som för alla fönster (se ovan).
    //
    // FOKUS-KONTRAKTET (gäller ALLA backends): en popup och dess
    // ankarfönster är EN fokusgrupp utåt — att popupen öppnas eller stängs
    // får ALDRIG ge ägaren `WindowFocusLost`/`WindowFocusGained`. Skälet är
    // att glaze::App stänger popups på ägarens fokusförlust (klick-utanför-
    // semantiken): bryter en backend kontraktet stängs listan i samma
    // andetag som den öppnas, och en dropdown blir omöjlig att använda.
    // Windows (`MA_NOACTIVATE`), macOS (nonactivating panel) och X11
    // (override-redirect) uppfyller det per konstruktion; Wayland gör det
    // INTE — `xdg_popup.grab` ger popupen tangentbordsfokus enligt
    // xdg-shell, så backend_linux maskerar det leave/enter-paret
    // (annars syntes dropdown-listan aldrig).
    Window* popupAnchorWindow = nullptr;
    Rectf   popupAnchorRect{};
};

// Färgerna för det egna chromet (titelrad + fönsterknappar). Backends
// ritar chromet ur den här — appen (temasystemet i glaze) trycker ner
// aktiva temats färger via Window::setChromeColors, så titelraden följer
// temat utan att gfx känner till någon temaklass. Defaultvärdena är det
// klassiska mörka chromet — ett fönster utan setChromeColors ser ut som
// det.
struct ChromeColors {
    Color titlebar    {30, 32, 36, 255};      // titelradens bakgrund
    Color titleText   {200, 203, 209, 255};
    Color buttonHover {62, 66, 74, 255};      // min/max-knapparnas hover
    Color buttonPress {50, 53, 60, 255};
    Color closeHover  {196, 43, 28, 255};     // stäng är röd av hävd
    Color glyph       {222, 224, 228, 255};   // streck/kryss/ruta-glyferna
    Color closeGlyph  {222, 224, 228, 255};   // glyfen ovanpå röd stäng-hover
    Color frame       {72, 76, 84, 255};      // tunn ram (omaximerat fönster)
};

// Hit-test-kartan: FÖNSTRET (glaze::AppWindow) är
// källan — den fyller en gfx-neutral roll-karta ur widget-trädet (tom
// TopBar-yta = Caption, TopBar-barn = knapp-roller/Client, resize-
// marginaler) och trycker ner den per repaint via setHitRegions().
// Backenden konsumerar kartan i sin nativa hit-test (WM_NCHITTEST,
// mouseDown-draget på Mac, egen zonuppslagning i backend_linux) — inga
// hårdkodade chrome-regioner. FÖRSTA träffen i listans ordning vinner
// (glaze emitterar i prioritetsordning: resize-hörn/-kanter, TopBar-barn,
// caption); ingen träff = Client. Rects i samma enheter som Event-mus/
// contentArea (fysiska px).
// Min/Max/CloseButton mappas som Client (klicken går till
// CaptionButton-widgetarna) — rollerna finns för Win11 snap-layout-hovern
// (HTMAXBUTTON, ännu ej implementerad).
//
// RESIZE-ROLLERNA ÄR ZONERNA INNANFÖR KLIENTYTAN (6 logiska px längs
// kanterna, 12 × 12 i hörnen — glaze emitterar dem för ett Custom-fönster som
// varken är maximerat eller fast). De tar de yttersta px av ALLT innehåll vid
// kanten, så där OS:et har en storleksram UTANFÖR den synliga kanten hoppar
// backenden över dem: D3D11 behåller Windows osynliga
// ram på vänster-, höger- och nederkanten, Metal låter AppKit äga kanterna,
// och Linux lägger en genomskinlig marginal runt ytan (Wayland
// `xdg_surface.set_window_geometry`, X11 `_GTK_FRAME_EXTENTS`).
// Linux läser zonerna bara där ingen marginal finns: X11 utan
// kompositör eller utan WM-stöd för `_GTK_FRAME_EXTENTS`, och ett tiled
// Wayland-fönster.
enum class HitRole : std::uint8_t {
    Client,
    Caption,
    MinButton,
    MaxButton,
    CloseButton,
    ResizeLeft,
    ResizeRight,
    ResizeTop,
    ResizeBottom,
    ResizeTopLeft,
    ResizeTopRight,
    ResizeBottomLeft,
    ResizeBottomRight,
};

struct HitRegion {
    Rectf   rect;
    HitRole role;
};

// MUSPEKARENS FORM — vad pekaren visar över
// KLIENTYTAN (`Window::setCursor`). Mängden är LITEN med flit: de former en
// widget eller en app har ett skäl att begära. Formerna är OS:ets egna där
// OS:et har dem — pekaren är den enda del av ytan användaren redan känner
// igen från resten av skrivbordet, och en egen bild hade sett olika ut mot
// OS:ets.
//
// ETT UNDANTAG: den STÄNGDA
// HANDEN finns varken bland Windows systempekare eller i X-kärnans pekarfont.
// Där är den en EGEN tvåtonsbild (svart kontur, vit fyllning) ur
// `src/cursor_bitmaps.h`, heltalsskalad till OS:ets pekarstorlek — Qt:s väg
// på samma plattformar, utan ett nytt beroende. macOS och Wayland-temana har
// den som systempekare.
//
//   Arrow       standardpilen
//   IBeam       textmarkören (ett fält man skriver i)
//   Hand        pekande hand (en länk)
//   ResizeWE    ↔ (ett skikt som dras i sidled — delaren)
//   ResizeNS    ↕ (ett skikt som dras i höjdled)
//   NotAllowed  förbud
//   Crosshair   hårkors (att peka ut en punkt på ritytan)
//   Move        flytta (ritytan som vrids). Fyrpilen på Windows och
//               X11; macOS har ingen publik fyrpil, där är det den stängda
//               handen
//   ClosedHand  den STÄNGDA handen — innehåll man håller
//               i och drar: ritytans panorering. Egen bild på Windows och X11
//               (se ovan), `closedHandCursor` på macOS, temats `grabbing` på
//               Wayland
//   Hidden      INGEN pekare över klientytan — `SetCursor(nullptr)`, en
//               genomskinlig `NSCursor`, en `NULL`-yta på Wayland och en tom
//               pixmap på X11. Aldrig de globala räknarna (`ShowCursor`,
//               `[NSCursor hide]`), som läcker om de inte paras
//   Wait        vänta — `IDC_WAIT`, `XC_watch`, temats `wait`. DEGRADERAD på
//               macOS: där finns ingen publik väntepekare, och svaret är pilen
//
// Ny form = en post SIST + en rad i VARJE backends tabell. Tabellerna är
// `switch` utan `default:`, så en glömd rad blir en -Wswitch-varning.
//
// NAMNET är `PointerShape` och inte `CursorShape` med flit: X11:s `X.h`
// definierar `CursorShape` som ett MAKRO (XQueryBestSize-klassen), så
// backendens egna X11-filer — och varje app som inkluderar Xlib före glaze —
// hade inte kompilerat.
enum class PointerShape : std::uint8_t {
    Arrow,
    IBeam,
    Hand,
    ResizeWE,
    ResizeNS,
    NotAllowed,
    Crosshair,
    Move,
    ClosedHand,
    Hidden,
    Wait,
};

// SKÄRMENS FYSISKA UPPLÖSNING — det som gör att en millimeter mäter
// en millimeter med linjal, oberoende av OS-skalan och appens användarskala.
// OS-skalan säger hur STORT användaren vill ha gränssnittet; tätheten säger
// hur många pixlar panelen har per millimeter, och de två är olika saker.
//
// VARIFRÅN svaret kommer, och det är en del av svaret — en app som visar en
// kartskala skriver "≈1:2000" när den inte är mätt:
//   Measured    skärmens egna mått (EDID) mot dess pixlar, ur DIAGONALEN
//   Assumed     inget användbart mått — 96 dpi i FYSISKA px (fönstrets
//               buffert), alltså 96/25,4 px per mm OBEROENDE av OS- och
//               användarskalan: panelen antas 96 dpi och bufferten lika tät
//               som panelen (reservvärdet). `reason` säger varför.
//   Calibrated  appens egen kalibrering för skärmens id
//               (`gfx::setDisplayCalibration`, platform.h)
enum class DisplaySource : std::uint8_t {
    Assumed,
    Measured,
    Calibrated,
};

// 96 dpi som FYSISKA px per mm: svaret innan något är mätt, och reserven
// (Assumed) vid varje skala.
inline constexpr float kAssumedPxPerMm = 96.0f / 25.4f;

struct DisplayMetrics {
    // FYSISKA px per millimeter — fönstrets egna px, de `uiScale()` räknar
    // till (backbuffern), inte nödvändigtvis panelens: macOS i "Mer
    // utrymme" ritar en större buffert än panelen och skalar ner.
    float         pxPerMm  = kAssumedPxPerMm;
    DisplaySource source   = DisplaySource::Assumed;
    int           widthMm  = 0;   // skärmens mått enligt OS:et, 0 = okänt
    int           heightMm = 0;
    // Skärmens NYCKEL — kalibreringens. Tillverkare-produkt-serie ur EDID
    // där OS:et ger den (samma form på Windows och macOS), annars
    // utgångens namn. Tom = inte mätt än.
    std::string   id;
    std::string   reason;     // VARFÖR Assumed — tom för de andra
};

// PROGRAMIKONEN — EN ram av ikonen: RGBA8 med RAK
// (inte förmultiplicerad) alfa, rad 0 överst, täta rader (width × 4 byte).
// Samma form som `captureFrame` och `decodeImage` — backenden räknar om till
// sin egen (HICON:s BGRA, Waylands förmultiplicerade ARGB8888, X11:s
// `long`-matris) och lagrar aldrig referensen.
struct IconImage {
    int                       width  = 0;
    int                       height = 0;
    std::vector<std::uint8_t> rgba;
};

// IKONENS RAMAR — samma bild i flera storlekar; backenden väljer den storlek
// OS:et frågar efter (minsta ram med sida ≥ målet, annars den största) och
// skalar aldrig själv. En TOM lista betyder INGEN IKON alls (appens `noIcon`).
//
// `programFirst` — ramarna är ramverkets FÖRVAL och inte appens val: finns en
// ikon i själva programfilen vinner den (Windows exe:ns ikonresurs nr 1,
// macOS paketets `CFBundleIconFile`), och ramarna gäller bara där den saknas.
// Linux har ingen ikon i programfilen — där gäller ramarna alltid.
struct WindowIcon {
    std::vector<IconImage> images;
    bool                   programFirst = false;
};

// FÖNSTRETS GEOMETRI — NORMALLÄGETS ruta
// och läget: det en app sparar vid stängning och sätter tillbaka före nästa
// `show()` (WinForms `RestoreBounds` + `WindowState`). Ett maximerat eller
// minimerat fönster svarar med den ruta det får tillbaka vid `restore()`,
// inte med den det har just nu.
struct WindowPlacement {
    int x = 0, y = 0;            // NORMALLÄGETS position — samma rymd som
                                 // `WindowDesc::posX`/`setPosition`
    int width = 0, height = 0;   // NORMALLÄGETS klientyta, LOGISKA px
                                 // (= `WindowDesc::width`)
    WindowState state = WindowState::Normal;
    bool hasPosition = false;    // false på Wayland och headless
};

// Abstrakt fönster — skapas via gfx::createWindow() (se platform.h) i två
// faser: fabriken ger ett objekt utan OS-fönster, init(desc) skapar det.
// Därför är setSize/setTitle no-op-säkra före init (AppWindow-fallet:
// genererad kod sätter titel/storlek i konstruktorn, före show()).
class Window {
public:
    virtual ~Window() = default;

    using RenderFn = std::function<void(Window&)>;

    virtual bool init(const WindowDesc& desc) = 0;
    virtual void shutdown() = 0;

    // Fönstrets identitet i events (Event::windowId). 0 före init.
    virtual WindowId id() const = 0;

    // Lyft fönstret överst och ge det fokus (modal-klick-på-ägare m.m.).
    // Ett DOLT fönster (`setVisible(false)`, `desc.hidden`) lyfts INTE — det
    // förblir dolt: Macs `makeKeyAndOrderFront`
    // hade annars visat det bakom glaze rygg, när en dialog stängdes medan
    // appen bodde i systemfältet. Synligheten ändras bara av `setVisible`.
    //
    // ETT `noActivate`-FÖNSTER LYFTS UTAN ATT AKTIVERAS — `noActivate` är
    // PERMANENT, och `raise()` får inte bryta det (Win32:s
    // `SetForegroundWindow` aktiverar också ett `WS_EX_NOACTIVATE`-fönster).
    // Det flytande verktygsfältet lyfts över de
    // flytande panelerna mitt i ett drag, och fönstret som håller trycket får
    // behålla fokus:
    //   Windows  `SetWindowPos(HWND_TOP, …, SWP_NOACTIVATE)` — bara Z-ordningen
    //   macOS    `orderFront:` (inte `makeKeyAndOrderFront:` + `activate`)
    //   X11      `XRaiseWindow` utan `_NET_ACTIVE_WINDOW`
    //   Wayland  no-op — xdg-shell lyfter bara genom aktivering
    //            (`xdg_activation`), och den hade tagit tangentbordet
    //   headless no-op
    virtual void raise() = 0;

    // Mata in varje event. Returnerar false när DET HÄR fönstret begär
    // stängning (close-knappen, WindowCloseRequested, Quit) — anroparen
    // (glaze::App) verkställer; backenden river inget själv.
    virtual bool processEvent(const Event& e) = 0;

    // Callbacket ritar frame-innehållet (clear + appens grejer, INTE chrome
    // eller present) — till den PERSISTENTA offscreen-render-targeten, som
    // komponeras till backbuffern varje present (grunden för partiell
    // omritning; innehållet överlever mellan frames). Registreras det här
    // kan fönstret rita om sig även under pågående resize/drag, då OS:et
    // håller huvudloopen i en modal loop (framför allt på Windows).
    virtual void setRenderCallback(RenderFn fn) = 0;

    // Overlay-callbacket ritar EFTER compositen och chromet, direkt i
    // backbuffern, varje present. Kontrakt: rent informativt lager (drag-
    // ghosts, tooltips, debug-markörer) — ingen hit-test, ingen input, och
    // det påverkar aldrig retained-innehållet eller omritningslogiken.
    // Backbuffern skrivs om av compositen varje present, så overlayen är
    // självstädande: rita och glöm.
    virtual void setOverlayCallback(RenderFn fn) = 0;

    // En hel frame: callback → retained RT · composite → backbuffer ·
    // chrome · overlay-callback · present. Anropa varje varv i huvudloopen.
    virtual void renderFrame() = 0;

    // Ritar titelrad + knappar. Anropas av renderFrame — bara relevant att
    // kalla själv om du inte använder render-callbacket. No-op vid native.
    virtual void drawChrome() = 0;

    // Appens rityta — allt under titelraden.
    virtual Rectf contentArea() const = 0;

    virtual float titlebarHeight() const = 0;   // 0 vid native chrome

    // Fönstrets fulla skala: osScale × gfx::userScale(). Fysisk
    // storlek = logisk × uiScale(); backenden multiplicerar in
    // användarskalan EXAKT en gång. Läses färskt per repaint (makeCtx).
    virtual float uiScale() const = 0;          // t.ex. 1.0, 1.25, ...

    // Användarskalan byttes (gfx::setUserScale — anropas av glaze::App
    // per öppet fönster): räkna om fysisk storlek ur DIN lagrade LOGISKA
    // storlek (invarianten — aldrig inkrementellt ur den fysiska) och
    // pusha WindowDisplayScaleChanged EFTER att renderern bytt storlek
    // (annars ritas en frame i gammal pixelstorlek med ny layout).
    // Samma pixelstorlek ⇒ eventet direkt (fonten måste ändå om).
    // Maximerat fönster: hoppa resizen men pusha eventet; den lagrade
    // logiska storleken röres inte — och backendens resize-krok får
    // ALDRIG räkna tillbaka logisk storlek ur maximeringens ELLER
    // återställningens rect. På restore-flanken MÅSTE backenden själv
    // sätta om fysisk storlek ur den lagrade logiska i AKTUELL skala:
    // OS:et återställer rect:en i den skala som gällde vid maximeringen
    // och vet ingenting om vår.
    //
    // OS:et återställer INTE rätt storlek i nya skalan av sig självt: 800×520
    // logiska, maximera, skala 1.0 → 1.25, återställ ger annars 800×520 FYSISKA
    // (alltså 640×416 logiska) i stället för 1000×650, och driften består vid
    // nästa skalbyte.
    //
    // KORRIGERINGEN ÄR ETT (1) FÖRSÖK per skuld. En WM kan neka målet —
    // våra egna storleksgränser räcker för att göra det ouppnåeligt — och
    // en villkorslös "läk varje avvikelse" blir då en oändlig
    // configure-loop. Nekas målet accepteras WM:ens storlek och skulden
    // släpps; nästa försök får ske först vid ett nytt externt
    // tillståndsbyte.
    // Kedjan är händelsestyrd: sättare → resize → event —
    // ingen polling (hård regel 1). No-op-säker före init.
    virtual void userScaleChanged() = 0;

    // SKÄRMENS FYSISKA UPPLÖSNING (se DisplayMetrics ovan) för skärmen
    // fönstret står på. MÄTS av backenden när fönstret skapas och när det
    // kan ha bytt skärm eller skärmen läge — och då köas
    // `WindowDisplayChanged`; frågan själv är billig och läser det mätta.
    // Kalibreringen läggs på av anroparen (`gfx::effectiveDisplay`) — det
    // här är backendens svar.
    //
    //   Windows  EDID ur registret (GetDeviceCaps(HORZSIZE) duger inte —
    //            Windows räknar den ur skalningsvalet) mot `rcMonitor`;
    //            WM_MOVE/WM_DISPLAYCHANGE/WM_DPICHANGED mäter om
    //   macOS    CGDisplayScreenSize mot punkter × backing; Apples
    //            72-dpi-uppskattning räknas som Assumed;
    //            windowDidChangeScreen mäter om
    //   Wayland  wl_output geometry (mm, 0 = okänt) och mode (px), done
    //            mäter om — bufferten antas lika tät som panelen
    //   X11      XRandR: crtc:n fönstrets mitt står på och dess utgång;
    //            RRScreenChangeNotify mäter om
    //   headless Assumed — det finns ingen skärm
    //
    // REN VIRTUELL av `setTopmost`s skäl, och SEX implementationer: på Linux
    // är proxyn den sjätte, och den får aldrig svara med ett eget värde
    // efter init (grind: `tests/display_gate.cpp`).
    virtual DisplayMetrics displayMetrics() const = 0;

    virtual bool  maximized() const = 0;

    // FÖNSTRETS GEOMETRI (se `WindowPlacement` ovan) — NORMALLÄGETS position
    // och klientyta, och läget. Backenden SPÅRAR normalrutan: den uppdateras
    // när fönstret flyttas eller storleksändras i normalläge och står still
    // medan det är maximerat eller minimerat. Före den första maximeringen
    // bär den den BEGÄRDA rutan, så "maximerad start → spara → återöppna" ger
    // rätt normalruta fast fönstret aldrig visats normalt.
    //
    // DEGRADERINGSTABELLEN — en del av kontraktet, som vid `setTopmost`:
    //
    //   Windows  normalrutan ur WM_MOVE/WM_SIZE när fönstret varken är
    //            zoomat eller ikoniserat. För `Chrome::Custom` KLIENTENS
    //            hörn: `GetWindowRect` plus den osynliga storleksramen
    //            (`setPosition(clientToScreen(0, 0))` är en
    //            identitet); för Native ytterhörnet, samma rymd som
    //            CreateWindowExW. Storleken ur den lagrade logiska, läget ur
    //            IsIconic/IsZoomed. Ett snappat fönster (Aero Snap) räknas som
    //            normalläge, som WinForms `RestoreBounds`
    //   macOS    ramens övre vänstra hörn i placeringsrymden, spårat i
    //            windowDidMove/windowDidResize utanför zoom och miniatyr
    //   X11      KLIENTENS origo i rotens px (XTranslateCoordinates i
    //            ConfigureNotify, plus storleksmarginalen när fönstret har en)
    //            — med `StaticGravity` i WM_NORMAL_HINTS
    //            är det också positionen `setPosition` sätter; läget ur
    //            `_NET_WM_STATE` (`_HIDDEN` = minimerat)
    //   Wayland  hasPosition FALSE (ingen skärmrymd) och aldrig Minimized
    //            (xdg-shell kan inte fråga efter minimering)
    //   headless hasPosition false, alltid Normal
    //
    // REN VIRTUELL av `setTopmost`s skäl, och SEX implementationer: på Linux
    // är proxyn den sjätte och måste vidarebefordra efter init — en
    // geometri FÖRE init finns inte (den är en spårning och ingen begäran),
    // så proxyn svarar då med `WindowPlacement{}` (grind:
    // `tests/geometry_gate.cpp`).
    virtual WindowPlacement placement() const = 0;

    // Fönster-kontrollerna: minimera/maximera/återställ
    // programmatiskt — CaptionButton-klick och appkod går via de här i
    // stället för backendens interna knappvägar. maximize() är REN (redan
    // maximerat = no-op); växlingen gör anroparen (maximized() ? restore()
    // : maximize()). restore() tar fönstret ur BÅDE minimerat och
    // maximerat läge. DEGRADERING: minimize är ENKELRIKTAT på Wayland
    // (ingen query, ingen programmatisk av-minimering — användaren tar
    // fram fönstret själv; restore lämnar bara maximerat läge där).
    // No-op-säkra före init.
    //
    // FÖRE DEN FÖRSTA SYNLIGHETEN — ett
    // fönster skapat med `desc.hidden` som aldrig visats — VISAR de inte
    // fönstret utan byter STARTLÄGET (`WindowDesc::startState`), som gäller
    // när den första `setVisible(true)` kommer: glaze skapar varje fönster
    // dolt och fyrar `onLoad` innan det visas, och ett läge som sätts där ska
    // gälla från första bildrutan. `maximized()` och `placement()` svarar
    // med OS-fönstrets läge, alltså normalläge, tills fönstret visats.
    //   Windows  `startState_` till `showFirst` (`ShowWindow(SW_MAXIMIZE)`
    //            hade visat det)
    //   macOS    `startState_` till den första `setVisible(true)`
    //   X11      det OMAPPADE fönstrets `_NET_WM_STATE_MAXIMIZED_*` och
    //            `WM_HINTS.initial_state` (EWMH/ICCCM — en clientmessage
    //            gäller bara ett mappat fönster)
    //   Wayland  `set_maximized`/`unset_maximized` med configuren invändad
    //            före den första bufferten; minimerat KAN INTE (loggas)
    //   headless ingenting att visa
    virtual void minimize() = 0;
    virtual void maximize() = 0;
    virtual void restore() = 0;

    // Sant om chromets utseende ändrats (hover/tryck på fönsterknapparna)
    // sedan förra anropet — konsumerar flaggan. Används för händelsestyrd
    // rendering: rita bara om när något faktiskt ändrats.
    virtual bool takeDirty() = 0;

    // Sant när retained-RT:n finns OCH fortfarande bär förra framens
    // innehåll. Render-callbacket får bara rita PARTIELLT (damage-rects)
    // när detta är sant — falskt betyder att hela ytan måste ritas om:
    // RT:n är nyss (åter)skapad (resize) eller saknas helt (degraderat
    // läge: direkt i backbuffern, vars innehåll är odefinierat per present).
    virtual bool retainedValid() const = 0;

    // No-op-säkra före init — anropas innan fönstret finns (tvåfas).
    // setSize tar LOGISKA px (som WindowDesc::width/height — backenden
    // multiplicerar till sin fysiska enhet).
    virtual void setSize(int w, int h) = 0;

    // Storleksgränserna i LOGISKA px, 0 = obegränsat per
    // axel — samma semantik som `WindowDesc::minWidth` m.fl., och de
    // fälten är bara startvärdet för det här anropet.
    //
    // REN VIRTUELL med flit: en backend som inte implementerar den skulle
    // tyst lova något den inte håller, och kompileringsfelet är billigare
    // än en app vars dialog går att dra sönder på en plattform.
    //
    // TRE AV FYRA backends måste ÅTERAPPLICERA vid skalbyte (Mac räknar i
    // punkter × user, Wayland i ytenheter, X11 i fysiska px) — bara
    // Windows räknar vid FRÅGAN (`WM_GETMINMAXINFO`) och får skalbyten
    // gratis. Missas det klampar OS:et mot fel enhet efter `setUserScale`.
    virtual void setSizeLimits(int minW, int minH, int maxW, int maxH) = 0;

    // ALLTID ÖVERST — fönstret läggs över ANDRA APPARS
    // fönster och stannar där tills flaggan tas bort. `raise()` lyfter en
    // gång; nästa klick i ett annat fönster lägger oss under igen, och det
    // är skillnaden.
    //
    // REN VIRTUELL med flit, samma skäl som `setSizeLimits`: en glömd
    // backend blir ett kompileringsfel i stället för ett tyst löfte.
    //
    // DEGRADERINGSTABELLEN — den står HÄR och inte i en fotnot, för den
    // är en del av kontraktet:
    //
    //   Windows  SetWindowPos med HWND_TOPMOST / HWND_NOTOPMOST
    //   macOS    NSWindow.level = NSFloatingWindowLevel / NSNormalWindowLevel
    //   X11      _NET_WM_STATE_ABOVE via _NET_WM_STATE
    //   Wayland  KAN INTE — xdg-shell har inget "alltid överst" för
    //            vanliga toplevels; det är kompositörens och användarens
    //            beslut. Anropet är en no-op som LOGGAR (en gång), och
    //            `topmost()` svarar alltid false där. Samma sort som
    //            setPosition/screenArea/minimize redan är på Wayland.
    //   headless Ingen fönsterhanterare finns — dokumenterad tyst no-op.
    //
    // `topmost()` svarar med vad som FAKTISKT gäller, aldrig med vad som
    // begärdes — så en app kan UPPTÄCKA degraderingen i stället för att
    // anta att begäran gick fram (samma prejudikat som `transparent()`,
    // som bekräftar efter init i stället för att lita på `WindowDesc`).
    // No-op-säker före init: värdet lagras och slår igenom i `init()`.
    virtual void setTopmost(bool on) = 0;
    virtual bool topmost() const     = 0;

    virtual void setTitle(const std::string& t) = 0;

    // Flytta fönstret — samma rymd som WindowDesc::posX/posY (OS:ets
    // placeringsenhet; se kommentaren där). Positionen avser KLIENTENS hörn
    // för ett fönster med eget chrome: Windows osynliga storleksram och Linux
    // genomskinliga storleksmarginal räknas bort, så
    // `setPosition(clientToScreen(0, 0))` lämnar fönstret där det står — den
    // identiteten bygger dockningen och `placement()` på. För Native avser den
    // på Windows ytterramen. DEGRADERING (fasadändringen): Wayland saknar
    // skärmpositioner helt — där är detta en no-op + logg. Popup-placering
    // går via WindowDesc::popupAnchor*-kontraktet, som fungerar överallt.
    // No-op-säker före init.
    virtual void setPosition(int x, int y) = 0;

    // Klientytans punkt (FYSISKA px — samma rymd som contentArea/resolve)
    // → skärmrymden ovan. Backenden äger enhets- och origokonverteringen
    // (Win32 ClientToScreen, AppKit flipp + punkter).
    // DEGRADERING: på Wayland finns ingen skärmrymd — där returneras
    // klient-px/skala (relativt eget fönster). Appkod som ska fungera
    // överallt använder popup-kontraktet i stället. (0,0) före init.
    virtual Pointf clientToScreen(float px, float py) const = 0;

    // Arbetsytan (utan menyrad/aktivitetsfält) på skärmen fönstret står
    // på, i SAMMA skärmrymd som setPosition. DEGRADERING: tom rect på
    // Wayland (dokumenterat — kompositören äger placeringen); flip/klamp
    // för popups gör backenden via popup-kontraktet. Tom rect före init.
    virtual Rectf screenArea() const = 0;

    // Dölj/visa fönstret utan att röra dess liv (AppWindow::hide/show —
    // fönstret finns kvar, OS-ytan göms bara). No-op-säker före init.
    virtual void setVisible(bool on) = 0;

    // Chromets färger — alltid säker att kalla (lagras bara); slår igenom
    // vid nästa drawChrome. Temasystemet kallar den per repaint.
    virtual void setChromeColors(const ChromeColors& c) = 0;

    // Chromets HÖRNRADIE i FYSISKA px; 0 = fyrkantigt. Ramen runt ett
    // omaximerat fönster ritas av backenden, så den måste känna radien —
    // annars sticker en rektangulär linje ut i hörnen på ett RUNDAT
    // fönster. Pushas per repaint som färgerna, av samma
    // skäl: radien följer stilen och skalan och kan ändras i drift.
    virtual void setChromeRadius(float radius) = 0;

    // Hit-test-kartan (se HitRole ovan) — glaze trycker ner den per
    // repaint när geometrin är färsk; backenden lagrar och slår upp i
    // sin nativa hit-test. Alltid säker att kalla (lagras bara).
    virtual void setHitRegions(std::vector<HitRegion> regions) = 0;

    // Captionens höjd (LOGISKA px) ur widget-trädet — TopBars faktiska
    // höjd (0 = ingen caption: Native/None/transparent/dold bar).
    // contentArea()/titlebarHeight() följer värdet, så en app med egen
    // barhöjd får rätt klientyta. Sätts av glaze per repaint + vid show.
    virtual void setCaptionHeight(float logicalPx) = 0;

    // Släpp backendens mus-capture i förtid. Vid knapptryck fångar
    // backenden musen till tryck-fönstret (drag utanför fönsterkanten ska
    // ge events tills släpp) — men öppnas ett popup-fönster MITT I trycket
    // (Dropdown, press-och-dra) ser popupen inga mus-events förrän
    // släppet. Anropas av popup-öppnaren på ÄGARFÖNSTRET; no-op när
    // fönstret inte håller capture. Metal: implicit capture per
    // event-ström — kan inte släppas (no-op, dokumenterad skillnad).
    virtual void releaseMouseCapture() = 0;

    // MUSENS CAPTURE TILL DET HÄR FÖNSTRET mitt i ett tryck
    // — motstycket till `releaseMouseCapture`. Resten av det
    // PÅGÅENDE trycket, rörelserna och släppet, levereras hit från och med
    // nu, också när trycket började i ett ANNAT av appens fönster. Svarar
    // true när fönstret FAKTISKT tar emot trycket nu; false = trycket stannar
    // där det började, eller inget tryck pågår (ingen capture tas utan ett
    // tryck). Skälet är Windows: `SetCapture` i ett fönster som inte är
    // förgrundsfönstret ger bara rörelserna över dess synliga yta, så ett
    // permanent `noActivate`-fönster (det flytande verktygsfältet) lämnar
    // sitt drag till förgrundsfönstret. Den förlorande sidan får inget
    // `MouseCaptureLost` — trycket lämnades över UTTRYCKLIGEN, och bara den
    // här vägen räknas som en överlämning.
    //
    //   Windows  `SetCapture`, när ett av appens fönster håller capture för
    //            ett TRYCK (vi tar capture vid varje tryck — capture som OS:ets
    //            flytt-/storleksloop håller räknas inte, inte heller i fönstret
    //            självt) OCH appens tråd äger förgrunden: annars ger capture
    //            bara rörelserna över den synliga ytan, och svaret är false
    //            (loggat) — ett noActivate-fönster klickat medan en annan app
    //            är förgrund behåller sitt tryck
    //   macOS    false — AppKit levererar draget till fönstret där trycket
    //            började, också ett som inte är key (en nonactivating panel)
    //   X11      false — den implicita grabben följer trycket och levererar
    //            det oavsett fokus
    //   Wayland  false — den implicita grabben hör till ytan
    //   headless false
    //
    // REN VIRTUELL och SEX implementationer: proxyn vidarebefordrar efter
    // init och svarar false före (det finns inget tryck att ta).
    virtual bool captureMouse() = 0;

    // HÅLLER FÖNSTRET MUSENS CAPTURE FÖR ETT TRYCK just nu?
    // — FRÅGAN bredvid `captureMouse`, som TAR.
    // Glaze ställer den efter krokarna i ett övertagande: en krok i ett annat
    // fönster kan ha tagit capturen vidare till ett tredje. Windows svarar
    // med `GetCapture()` och tryckets flagga; macOS, X11, Wayland och
    // headless tar aldrig capture och svarar false. REN VIRTUELL, sex
    // implementationer; proxyn vidarebefordrar efter init och svarar false
    // före.
    virtual bool hasMouseCapture() const = 0;

    // MUSPEKAREN — formen pekaren visar när den står
    // över KLIENTYTAN. glaze::AppWindow sätter den ur widgeten under
    // pekaren — eller ur den som håller trycket, så ett skikt som dras
    // behåller ↔ även när pekaren hinner före — vid varje musrörelse.
    //
    // CHROMETS ZONER VINNER: över en resize-kant eller ett resize-hörn i
    // hit-test-kartan (där backenden läser zonerna) och över en ram UTANFÖR
    // klientytan (OS-ramen vid `Chrome::Native`, Windows osynliga
    // storleksram och Linux storleksmarginal vid eget chrome) visar
    // backenden sin egen resize-pekare oavsett begäran, och den begärda
    // formen kommer tillbaka när pekaren går in i klientytan igen.
    //
    // VÄGEN per backend — kontraktet är detsamma, mekaniken inte:
    //
    //   Windows  `SetCursor` direkt när pekaren står i klientytan eller
    //            fönstret håller mus-capture, och i `WM_SETCURSOR` (HTCLIENT)
    //            vid varje rörelse — annars skriver klassens pil över formen.
    //            Under capture skickas inget `WM_SETCURSOR` alls (meddelandets
    //            dokumentation), därför det direkta anropet.
    //   macOS    `[NSCursor set]` + `cursorUpdate:` från vyns spårningsyta —
    //            AppKit sätter annars tillbaka pilen när pekaren går in.
    //   Wayland  `wl_pointer_set_cursor` med temats bild (wl_cursor). Pekaren
    //            är KLIENTENS jobb där och bara giltig medan den står i en av
    //            våra ytor; formen sätts om vid varje `enter`.
    //   X11      `XDefineCursor` på fönstret (font-pekarna) — formen hör till
    //            fönstret, så X-servern visar den så fort pekaren står där.
    //   headless Ingen pekare finns — formen LAGRAS bara.
    //
    // `cursor()` svarar med den SENAST BEGÄRDA formen — inte med vad OS:et
    // visar just nu, som över en resize-kant är en annan pil. Det är en
    // annan sort än `topmost()`, och det är skrivet med flit: topmost är ett
    // TILLSTÅND som OS:et kan neka, formen är en BEGÄRAN som alltid gäller
    // klientytan.
    //
    // REN VIRTUELL av samma skäl som `setTopmost`: en glömd backend blir ett
    // kompileringsfel. No-op-säker före init: formen LAGRAS och gäller när
    // fönstret finns — på Linux är proxyn den SJÄTTE implementationen och
    // måste både lagra och vidarebefordra i `init()` (grind:
    // `tests/splitter_gate.cpp`).
    virtual void        setCursor(PointerShape shape) = 0;
    virtual PointerShape cursor() const               = 0;

    // PROGRAMIKONEN — ikonen OS:et visar för
    // fönstret: aktivitetsfältet, Alt+Tab, dockan, fönsterväxlaren. Ramarna
    // (se `WindowIcon` ovan) KOPIERAS; en tom lista tar bort ikonen.
    //
    // DEGRADERINGSTABELLEN — en del av kontraktet, som vid `setTopmost`:
    //
    //   Windows  WM_SETICON small (SM_CXSMICON) + big (SM_CXICON) i fönstrets
    //            DPI, `CreateIconIndirect`; byggs om vid WM_DPICHANGED.
    //            `programFirst` + ikonresurs nr 1 i exe:n ⇒ resursen
    //   macOS    `[NSApp setApplicationIconImage:]` — dockans ikon gäller
    //            HELA APPEN, inte fönstret. Paketets `CFBundleIconFile`
    //            vinner ALLTID (sant bara för `programFirst`); utan den äger
    //            det FÖRSTA levande fönster som satt en ikon dockan, och ett
    //            annat fönsters ikon svarar false
    //   X11      `_NET_WM_ICON` (ramar ≤ 256 px) — WM:en väljer storlek
    //   Wayland  `xdg-toplevel-icon-v1` när kompositören har den (KWin,
    //            Labwc …). Saknas den: no-op som LOGGAR en gång, och
    //            `iconApplied()` svarar false — skrivbordet tar då ikonen ur
    //            appens `.desktop`-fil via app_id (`gfx::setAppId`)
    //   headless Ingen fönsterhanterare och ingen ikon att visa —
    //            dokumenterad tyst no-op, `iconApplied()` svarar false
    //
    // `iconApplied()` svarar med vad som FAKTISKT gäller — sant när OS:et
    // visar en ikon som fönstret självt satt (ramarna eller programfilens
    // resurs), falskt för en tom lista och för varje degradering — aldrig
    // med vad som begärdes. Samma prejudikat som `topmost()`.
    //
    // REN VIRTUELL av `setTopmost`s skäl. No-op-säker före init: ramarna
    // LAGRAS och tillämpas i `init()` — på Linux är proxyn den SJÄTTE
    // implementationen och måste både lagra och vidarebefordra.
    virtual void setIcon(const WindowIcon& icon) = 0;
    virtual bool iconApplied() const            = 0;

    // Upprepande timer: EventType::Timer med timerId = id levereras var
    // ms millisekund tills stopTimer(id). id måste vara > 0. Timern väcker
    // waitEvent — fasadens väg att vakna på tid utan polling (caret-blink
    // m.m.); appen sover mellan ticken (hård regel 1). startTimer med
    // samma id startar om perioden. No-op-säkra före init.
    virtual void startTimer(std::uint32_t id, std::uint32_t ms) = 0;
    virtual void stopTimer(std::uint32_t id) = 0;

    // Slår på/av OS:ets textinmatning för fönstret. Anropas när en
    // textwidget får/tappar fokus — no-op i backends som alltid levererar
    // tecken (Win32 skickar alltid WM_CHAR), krok för IME-styrning.
    virtual void setTextInputActive(bool on) = 0;

    // Läs tillbaka senast renderade framen ur den persistenta retained-
    // RT:n: rå RGBA8, rad 0 överst, täta rader (w × 4
    // byte), FYSISKA px = klientyta × native scale — 1:1, ingen
    // omsampling (WYSIWYG-kontraktet). Anropas EFTER renderFrame() (RT:n
    // bär då framens innehåll — även för dolt fönster). false när RT:n
    // saknas/är ogiltig (degraderat läge) eller mitt i en öppen frame.
    virtual bool captureFrame(std::vector<std::uint8_t>& rgba,
                              int& w, int& h) = 0;

    // Bär ytan FAKTISKT per-pixel-alfa? `WindowDesc::transparent` är en
    // BEGÄRAN — X11 utan ARGB-visual degraderar till opakt och loggar det.
    // Den som ritar genomskinliga hörn (rundade fönster) måste kunna fråga
    // i stället för att anta: på en opak yta blir ett bortklippt hörn
    // svart, inte genomskinligt. Giltig efter init().
    virtual bool transparent() const = 0;

    // Fönstrets renderer — giltig referens efter init.
    virtual Renderer& renderer() = 0;
};

} // namespace gfx
