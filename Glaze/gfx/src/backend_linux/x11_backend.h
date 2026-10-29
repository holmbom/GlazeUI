#pragma once
// X11-sidan av Linux-backenden — intern header, får BARA inkluderas av
// x11_display.cpp och x11_window.cpp. Xlib förorenar det globala
// namnrummet (typedef Window, makron None/True/False) — därför hålls
// headern borta från resten av backenden och makrona av-definieras nedan
// (X11-konstanterna används som råvärden i stället).

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XInput2.h>  // pekaren + styrplattan
#include <X11/extensions/Xrandr.h>   // skärmens mm

#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

#include "linux_backend.h"

// Krockar med Chrome::None/MouseButton::None resp. dbus-bool-bruk.
#undef None
#undef True
#undef False

namespace gfx {

// X11-makronas råvärden (efter #undef ovan).
inline constexpr long kX11None  = 0L;
inline constexpr int  kX11True  = 1;
inline constexpr int  kX11False = 0;

class X11Window;

// ------------------------------------------------------------ X11Display

class X11Display final : public LinuxDisplay {
public:
    static X11Display* create();   // nullptr när DISPLAY inte går att öppna
    ~X11Display() override;

    EGLDisplay egl() const override { return egl_; }
    std::unique_ptr<Window> makeWindow() override;

    void prepoll() override;            // dränera Xlib-kön + XFlush
    void postpoll(bool fdReady) override;
    int  fd() const override { return ConnectionNumber(dpy_); }

    std::string clipboardText() override;
    void setClipboardText(const std::string& utf8) override;

    // Åtkomst för X11Window.
    Display* xdisplay() const { return dpy_; }
    int      screen() const { return screen_; }
    EGLConfig eglConfig(bool transparent) const
    {
        return transparent && cfg32_ ? cfg32_ : cfg24_;
    }
    // Finns en ARGB-konfig? `eglConfig(true)` faller tillbaka på den opaka
    // och svarar aldrig nullptr, så den kan inte ställa frågan.
    bool hasArgbConfig() const { return cfg32_ != nullptr; }
    XVisualInfo visualFor(bool transparent) const;
    float uiScale() const { return uiScale_; }

    void registerWindow(::Window w, X11Window* win);
    void unregisterWindow(::Window w);
    void windowDestroyed(X11Window* win);

    // RESOURCE_MANAGER på root ändrades (PropertyNotify — GNOME skriver
    // ny Xft.dpi vid text-/skalbyte): läs om, och vid ändrad uiScale_
    // räkna om fönstrens fysiska storlek + pusha WindowDisplayScaleChanged.
    void onResourceManagerChanged();

    // Definierar en pekare på fönstret, ur cachen nedan: en FONT-pekare
    // (`XC_*` ur cursorfont.h, alla under 1000) eller en av PSEUDONYCKLARNA
    // nedan, som byggs som pixmap-pekare. Valet av form
    // gör fönstret (`X11Window::refreshCursor`).
    void defineCursor(::Window w, int key);
    // Pekarna X-kärnans font SAKNAR: den stängda handen ur
    // src/cursor_bitmaps.h, heltalsskalad till Xft-skalan, och den TOMMA
    // pekaren (`PointerShape::Hidden` — en 1 × 1-pixmap med tom mask).
    static constexpr int kCursorClosedHand = 1000;
    static constexpr int kCursorHidden     = 1001;

    // SKÄRMARNA i placeringsrymden: XRandR:s aktiva
    // crtc:er (utan RandR: hela rotfönstret) snittade med `_NET_WORKAREA`,
    // och skalan Xft.dpi/96 × användarskalan.
    std::vector<ScreenInfo> placementScreens() const override;
    // `_NET_WORKAREA` för det första skrivbordet — EN rektangel för hela
    // skrivbordet (EWMH), alltså ungefärlig per skärm med paneler på bara en
    // av flera. false när WM:en inte satt den.
    bool readWorkarea(Rectf& ut) const;

    // SKÄRMENS mm (XRandR). Varje AKTIV crtc:
    // dess rect i rotens px och dess första utgångs millimeter och id
    // (EDID:s tillverkare-produkt-serie, annars utgångens namn). Läst vid
    // start och vid RRScreenChangeNotify — fönstren slår upp i listan, så
    // en flytt kostar ingen serverrunda utöver fönstrets egen position.
    struct Screen {
        int         x = 0, y = 0, w = 0, h = 0;
        int         mmW = 0, mmH = 0;
        std::string id;
        bool        primary = false;   // RandR:s primära utgång sitter här
    };
    bool                       randr() const { return randr_; }
    const std::vector<Screen>& screens() const { return screens_; }
    ::Window                   rootWindow() const { return root_; }

    // XINPUT 2. Med XI ≥ 2.1 går pekarens RÖRELSE och KNAPPAR via
    // XI2 för våra fönster — det är bara där skrollvalutorerna syns, och
    // väljer man XI_Motion slutar kärnans MotionNotify komma. Fönstret väljer
    // sina event i init; utan XI2 står kärnmaskerna kvar.
    bool xi2() const { return xi2_; }
    void selectXiPointer(::Window w);
    // Släpper den IMPLICITA pekargrabben ett tryck öppnade — kärnans OCH
    // XI2:ans (ett tryck som levererats via XI2 öppnar en XI2-grab; varför
    // båda anropen står vid definitionen). Före `_NET_WM_MOVERESIZE` och i
    // `releaseMouseCapture`.
    void ungrabPointer(unsigned long time);

    // STORLEKSMARGINALEN UTANFÖR YTAN: går den att rita
    // i den här X-sessionen? Tre villkor, frågade när ett fönster skapas (en
    // kompositör kan starta efter oss): en ARGB-konfig (marginalen är
    // genomskinlig), en KOMPOSITÖR (`_NET_WM_CM_S<skärm>` har en ägare,
    // EWMH — utan den ritas marginalen svart) och en FÖNSTERHANTERARE som
    // känner `_GTK_FRAME_EXTENTS` (den står i `_NET_SUPPORTED` på roten —
    // annars tror WM:en att marginalen är en del av fönstret vid placering,
    // snapp och maximering). Saknas något får fönstret ingen marginal och
    // kartans zoner innanför gäller som vanligt.
    bool frameExtentsUsable() const;

    // Atoms (cache — internas via XInternAtom vid init).
    Atom aWmProtocols_, aWmDelete_, aNetWmName_, aUtf8_, aNetWmState_,
        aNetWmStateMaxV_, aNetWmStateMaxH_, aNetWmStateAbove_,
        aNetWmStateHidden_,   // minimerat
        aNetMoveResize_, aNetWorkarea_,
        aMotifHints_, aNetWmWindowType_, aNetWmTypeUtility_,
        aNetWmTypeDropdown_, aNetActiveWindow_, aClipboard_, aTargets_,
        aGlzSelection_, aIncr_, aNetWmIcon_,
        aGtkFrameExtents_, aNetSupported_,
        aNetWmCmS_,        // `_NET_WM_CM_S<skärm>` (finns en kompositör?)
        aNetWmUserTime_;   // första visningen utan fokus

private:
    X11Display() = default;
    bool connect();
    void translate(XEvent& ev);
    void handleKey(XEvent& ev, bool pressed);
    void handleSelectionRequest(XSelectionRequestEvent& req);
    X11Window* windowFor(::Window w) const;
    void readXftDpi();
    void readScreens();   // RandR:s aktiva crtc:er → screens_

    // XInput 2: version, rotens urval och enhetstabellen.
    void initXi2();
    void readXiDevices();              // styrplattornas skrollvalutorer
    void translateXi(XGenericEventCookie& cookie);
    void xiPointerEvent(int evtype, const XIDeviceEvent& ev);
    void xiPinch(int evtype, const XIGesturePinchEvent& ev);
    void xiResetScroll();              // alla baslinjer ogiltiga (enter)

    Display* dpy_    = nullptr;
    int      screen_ = 0;
    ::Window root_   = 0;
    ::Window helper_ = 0;   // osynligt fönster: selection-ägare/mottagare

    EGLDisplay egl_   = EGL_NO_DISPLAY;
    EGLConfig  cfg24_ = nullptr;   // opaka fönster
    EGLConfig  cfg32_ = nullptr;   // ARGB (transparent) — kan saknas

    float uiScale_ = 1.0f;   // Xft.dpi/96 (bär GNOME:s textskalning)

    std::unordered_map<::Window, X11Window*> windows_;

    // xkbcommon (keymap ur rotens _XKB_RULES_NAMES — xkbcommon-x11
    // används inte; RMLVO-vägen är ren Xlib).
    xkb_context*       xkb_          = nullptr;
    xkb_keymap*        keymap_       = nullptr;
    xkb_state*         xkbState_     = nullptr;
    xkb_compose_table* composeTable_ = nullptr;
    xkb_compose_state* composeState_ = nullptr;

    // Klippbordet (selections).
    std::string ownClipboardText_;
    bool        weOwnSelection_ = false;

    // XRandR (skärmens mm): finns tillägget, dess eventbas, och listan.
    bool                randr_      = false;
    int                 randrEvent_ = 0;
    std::vector<Screen> screens_;

    // XINPUT 2. `xi2_` = XI ≥ 2.1 (XI2-pekaren och
    // skrollvalutorerna), `xiPinch_` = XI ≥ 2.4 (nypningen). `xiPointer_` är
    // master-pekaren `ungrabPointer` släpper XI2-grabben för: klientpekaren
    // från start, därefter det senaste XI2-eventets deviceid.
    bool xi2_       = false;
    bool xiPinch_   = false;
    int  xiOpcode_  = 0;
    int  xiPointer_ = 0;

    // STYRPLATTORNAS SKROLLVALUTORER per SLAV-enhet (eventens sourceid).
    // Värdet är ACKUMULERAT, så deltat är värdet minus förra; en baslinje som
    // inte är giltig (efter enter eller ett enhetsbyte) sätts av nästa event
    // utan att rulla. BARA styrplattor står här — ett mushjul rullar med sina
    // knappar 4–7 (skälet står vid xiPointerEvent).
    struct XiScroll {
        int    number    = 0;       // valutorns nummer
        bool   vertical  = false;
        bool   inverted  = false;   // negativt increment = omvänd riktning
        double last      = 0.0;
        bool   lastValid = false;
    };
    std::unordered_map<int, std::vector<XiScroll>> xiTouchpads_;
    // Styrplattans deltan i ett XI_Motion → ett PRECIST hjul till `w`.
    void xiScroll(X11Window* w, const XIDeviceEvent& ev,
                  std::vector<XiScroll>& axes);

    // Pågående TVÅFINGERSnypning: fönstret den började i och förra ABSOLUTA
    // skalan — XI:s skala gäller från begin, kontraktets från förra eventet.
    X11Window* pinchWin_   = nullptr;
    double     pinchScale_ = 1.0;

    // Font-pekarna (XCreateFontCursor), cachade per form. Rollen under
    // pekaren bor PER FÖNSTER (`X11Window::cursorRole_`): en
    // pekare definieras på ett X-fönster och följer med det, så ett minne
    // för hela displayen hade lämnat ett fönster med en resize-pil kvar när
    // pekaren gick till ett annat och tillbaka.
    std::unordered_map<int, Cursor> cursors_;
    // Pseudonycklarnas pixmap-pekare — byggs EN gång,
    // i Xft-skalan som gällde då, och cachas som font-pekarna.
    Cursor makePixmapCursor(int key);

    friend class X11Window;
};

// ------------------------------------------------------------- X11Window

class X11Window final : public LinuxWindow {
public:
    explicit X11Window(X11Display* d) : display_(d) {}
    ~X11Window() override { shutdown(); }

    bool init(const WindowDesc& desc) override;
    void shutdown() override;
    void raise() override;

    // Fulla skalan: Xft.dpi/96 × användarskalan — ALLA
    // logisk↔fysisk-omräkningar i x11_window.cpp går via den här, aldrig
    // display-skalan direkt (faktorn ska in exakt en gång).
    float uiScale() const override
    {
        return display_->uiScale() * userScale();
    }
    // Samma kortslutningar (maximerat / oförändrad pixelstorlek)
    // och ownResizes_-mekanik som Xft.dpi-bytet — eventet köas i
    // onConfigure efter renderer-resizen (aldrig före).
    void userScaleChanged() override { onUiScaleChanged(); }
    bool  maximized() const override { return maximized_; }
    // FÖNSTRETS GEOMETRI — se x11_window.cpp.
    WindowPlacement placement() const override;
    void  minimize() override;
    void  maximize() override;
    void  restore() override;

    void setSize(int w, int h) override;       // logiska px (fasadändringen)
    void setSizeLimits(int minW, int minH, int maxW,
                       int maxH) override;     // logiska px
    void setTopmost(bool on) override;         // _NET_WM_STATE_ABOVE
    bool topmost() const override { return topmost_; }
    void setTitle(const std::string& t) override;
    void setVisible(bool on) override;
    void setPosition(int x, int y) override;   // fysiska px (skärmrymden)
    Pointf clientToScreen(float px, float py) const override;
    Rectf screenArea() const override;
    void releaseMouseCapture() override;
    // False — den implicita grabben följer trycket och
    // levererar det till fönstret där det började oavsett fokus, så det
    // finns inget att ta över (fasadens tabell) och ingen tagen capture att
    // fråga om.
    bool captureMouse() override { return false; }
    bool hasMouseCapture() const override { return false; }

    // MUSPEKAREN: formen lagras i basen och definieras på
    // X-fönstret direkt — X-servern visar den så fort pekaren står där,
    // också under ett tryck (den implicita grabben har ingen egen pekare).
    void setCursor(PointerShape shape) override;

    // PROGRAMIKONEN: `_NET_WM_ICON` ur ramarna — lagrade, och
    // skrivna FÖRE `XMapWindow` när de satts före init. `iconApplied()` är
    // sant när propertyn bär minst en ram (X11 ger ingen kvittens på att
    // WM:en visar den — propertyn är det som FAKTISKT gäller här).
    void setIcon(const WindowIcon& icon) override;
    bool iconApplied() const override { return iconApplied_; }

    // Display-krokar (event-översättningen). Punkterna är KLIENTENS px:
    // displayen drar av storleksmarginalen (`frameMargin()`, E2L) från
    // X-fönstrets koordinater innan något når fönstret eller appen.
    // Hit-rollen under pekaren (rörelsen — XI_Motion, eller MotionNotify
    // utan XI2): en resize-zon visar sin pil, klientytan fönstrets begärda
    // form.
    void onCursorRole(HitRole role);
    void onConfigure(int w, int h);
    void onPropertyNetWmState();

    // SKÄRMENS mm: crtc:n fönstrets MITT står på, ur
    // displayens lista. Mäts vid init, när fönstret flyttats eller
    // resizats (ConfigureNotify) och vid RRScreenChangeNotify — inte vid
    // skalbyte: RandR:s mått och reserven beror inte på Xft.dpi;
    // `announce` köar WindowDisplayChanged när svaret
    // blev ett annat.
    DisplayMetrics displayMetrics() const override { return metrics_; }
    void           measureDisplay(bool announce);
    void onUiScaleChanged();   // Xft.dpi bytte: ny fysisk storlek ur logisk
    bool interceptPress(int x, int y, int xRoot, int yRoot,
                        unsigned long time);
    ::Window xwindow() const { return win_; }
    bool mappedVisible() const { return visible_ && !hidden_; }

protected:
    bool shouldSwap() const override { return visible_ && !hidden_; }
    Sizef pixelSize() const override
    {
        return Sizef{static_cast<float>(physW_),
                     static_cast<float>(physH_)};
    }

private:
    void setNetWmState(bool add, Atom a1, Atom a2);
    // Läser _NET_WM_STATE EN gång och plockar ut de tre tillstånd vi bryr
    // oss om. Skilt från frågorna för att en PropertyNotify ska kosta ETT
    // serveranrop och inte tre.
    void readNetWmState(bool& maxed, bool& above, bool& hidden) const;
    bool queryMaximized() const;
    void applyTopmost();   // _NET_WM_STATE_ABOVE ur `requestedTopmost_`
    // _NET_WM_STATE på ett OMAPPAT fönster (EWMH: där skriver klienten
    // propertyn själv): läs listan, lägg till eller ta bort `a`, skriv
    // tillbaka — aldrig ett rakt XDeleteProperty, som hade slagit bort de
    // andra tillstånden. Delad av "alltid överst" och maximerad start.
    void setUnmappedNetWmState(Atom a, bool on);

    // EN SKRIVARE av WM_NORMAL_HINTS. `XSetWMNormalHints`
    // skriver över HELA strukturen, så en andra skrivare hade raderat den
    // förstas fält: storleksgränserna (PMinSize/PMaxSize, i FYSISKA px och
    // därför omräknade vid varje skalbyte), `PWinGravity = StaticGravity`
    // ALLTID (klientens position är positionen, ICCCM — så en sparad
    // position återställs exakt också med en Native-ram) och
    // `USPosition|PPosition` när init fick en position.
    void writeNormalHints();
    // WM_HINTS: `InputHint` = false för ett fönster
    // som inte ska ta fokus, och `StateHint` med `initial_state` IconicState
    // för en minimerad start — NormalState annars. WM:en läser
    // `initial_state` vid VARJE mappning från Withdrawn, så den minimerade
    // starten återställs till NormalState inför nästa visning
    // (`setVisible`).
    void writeWmHints(bool iconic);
    // STARTLÄGET PÅ DET OMAPPADE FÖNSTRET: maximize/
    // minimize/restore före den första mappningen skriver om `_NET_WM_STATE`
    // och `initial_state` i stället för att be WM:en (som bara lyssnar på ett
    // mappat fönster).
    void setUnmappedStartState(WindowState s);
    // Normalrutans position: klientens origo i rotens px (se `normalX_`).
    void trackNormalPosition();

    // STORLEKSMARGINALEN UTANFÖR YTAN.
    // X-fönstret är klientytan + 2m, genomskinligt i marginalen (ARGB-
    // visual), och `_GTK_FRAME_EXTENTS` = (m, m, m, m) säger WM:en att
    // marginalen inte är en del av fönstret (mutter, KWin och xfwm4 placerar,
    // snappar och maximerar då klientytan). `frameMargin_` (basen) är den
    // som gäller just nu: `marginPx_` när fönstret kan ha en
    // (`marginCapable_`, beslutat i init — eget chrome, ingen popup, inte
    // FAST vid starten och `X11Display::frameExtentsUsable`) och inte är
    // maximerat, annars 0. Ett fönster som BLIR fast i drift behåller
    // marginalen men den är inert (`frameResizable_` i basen): geometrin
    // byts bara när WM:en ändå äger den (maximering), så inga egna
    // asynkrona omflyttningar kan korsa varandra. Ett fönster som startar
    // fast får ingen — den genomskinliga marginalen hade ätit klick (ingen
    // inmatningsform). Bredden räknas EN gång, i init:s skala — ett skalbyte
    // i drift ändrar inte greppets bredd i px.
    bool marginCapable_ = false;
    int  marginPx_      = 0;
    bool fixedSizeLimits() const;   // min == max på båda axlarna
    int  wantedMargin() const;
    // Byter till `wantedMargin()` när den skiljer (maximerat ↔ inte):
    // renderaren, `_GTK_FRAME_EXTENTS` och WM_NORMAL_HINTS (storlekarna är
    // X-fönstrets, marginalen ingår). WM:en äger geometrin och anpassar
    // X-fönstret till de nya marginalerna själv. Svarar om marginalen bytte.
    bool applyFrameMargin();
    void writeFrameExtents();

    X11Display* display_ = nullptr;
    ::Window    win_     = 0;
    Colormap    colormap_ = 0;
    EGLSurface  eglSurface_ = EGL_NO_SURFACE;

    // KLIENTYTANS fysiska px — X11-fönstret är 2 × `frameMargin_` större,
    // och `onConfigure` drar av marginalen innan något jämförs.
    int  physW_ = 0, physH_ = 0;
    // Beställd LOGISK storlek (fasadens logiska px) — källan när fysisk
    // storlek räknas om vid Xft.dpi-byte i drift. Uppdateras vid init/
    // setSize OCH när användaren resizat (onConfigure), annars hoppar
    // fönstret tillbaka vid nästa skalbyte.
    int  logicalW_ = 0, logicalH_ = 0;
    // Sant mellan vår egen skalbyte-resize och dess ConfigureNotify:
    // skal-eventet väntar på att buffert/renderer faktiskt bytt storlek,
    // och den logiska storleken räknas inte om ur den fysiska.
    // Egna XResizeWindow-anrop som ännu inte fått sin ConfigureNotify.
    // RÄKNARE, inte bool: två skalbyten i följd ger två configures och
    // bara den sista får släppa ut skal-eventet. Deras configures får
    // aldrig räkna tillbaka logisk storlek ur den avrundade fysiska.
    // STORLEKSGRÄNSERNA i LOGISKA px, 0 = obegränsat.
    // Lagras för att kunna ÅTERAPPLICERAS: `XSizeHints` talar FYSISKA px,
    // så ett byte av Xft.dpi eller userScale gör de gamla talen fel.
    // Skrivs av `writeNormalHints`.
    int  limMinW_ = 0, limMinH_ = 0, limMaxW_ = 0, limMaxH_ = 0;

    // FÖNSTRETS GEOMETRI GEOMETRI: normallägets KLIENTORIGO i
    // rotens px — XTranslateCoordinates i ConfigureNotify plus
    // storleksmarginalen, bakom SAMMA övergångsvakt som den lagrade
    // logiska storleken (maximerad eller med en utestående restore-skuld)
    // plus minimeringen. Startvärdet är init:s position, så en maximerad
    // start bär den begärda rutan.
    int  normalX_ = 0, normalY_ = 0;
    bool minimized_ = false;      // `_NET_WM_STATE_HIDDEN` (onPropertyNetWmState)
    // STARTLÄGET och WM-hintsens underlag.
    WindowState startState_   = WindowState::Normal;
    bool        noActivate_   = false;   // InputHint = false (WM_HINTS)
    bool        userPosition_ = false;   // init fick en position → USPosition
    // En minimerad start skrev IconicState — återställs vid nästa mappning.
    bool        iconicHintSet_ = false;
    bool        everMapped_    = false;
    // FÖRSTA VISNINGEN AKTIVERAR INTE (`WindowDesc::showInactive`):
    // `_NET_WM_USER_TIME` = 0 skrevs före den första
    // mappningen och tas bort inför nästa — attributet gäller bara den
    // första, samma mönster som IconicState ovan.
    bool        userTimeZero_  = false;
    // ALLTID ÖVERST. Speglar det FAKTISKA tillståndet, inte
    // begäran: begäran seedar fältet, och `onPropertyNetWmState` skriver
    // om det ur WM:ets svar — en WM som vägrar `_NET_WM_STATE_ABOVE` ska
    // synas i `topmost()`, inte döljas av vår egen förhoppning.
    // BEGÄRAN och OBSERVATION är SKILDA. Kontraktet
    // i window.h säger att `topmost()` svarar med vad som FAKTISKT gäller —
    // och en clientmessage till WM:et är bara en FRÅGA: den kan behandlas
    // sent, eller ignoreras helt utan att någon PropertyNotify kommer. Delar
    // de två ett fält svarar gettern med begäran och kan bli permanent fel.
    bool requestedTopmost_ = false;   // det VI bad om — läses av applyTopmost
    bool topmost_          = false;   // det som står i _NET_WM_STATE
    // Är fönstret FAKTISKT mappat? Skilt från `visible_`, som är sant
    // redan före första `XMapWindow` (default true, sätts i init-svansen)
    // — och EWMH:s clientmessage-väg gäller BARA mappade fönster, så
    // skillnaden avgör vilken av de två vägarna `applyTopmost` tar.
    // Fel flagga här ger en tyst tappad begäran.
    bool mapped_ = false;

    int  ownResizes_    = 0;
    bool scaleEventOwed_ = false;

    // RESTORE-SKULDEN — satt när skalan bytte medan
    // fönstret var MAXIMERAT. WM:en äger storleken då, så vår resize
    // hoppas över; på restore-flanken återställer WM:en sin ihågkomna
    // rect i den GAMLA skalan och vet ingenting om vår.
    //
    // Skulden bär det VÄNTADE FYSISKA måttet, inte bara en flagga. Skälet
    // är att `onConfigure` återvänder tidigt vid oförändrat mått — alltså
    // FÖRE nedräkningen av `ownResizes_` — så en resize WM:en VÄGRAR ger
    // ingen storleksändrande configure och räknaren skulle fastna
    // positiv. Nästa riktiga användar-resize klassas då som vår egen, äts
    // av nedräkningen, och `logicalW_/H_` uppdateras aldrig mer. Med det
    // väntade måttet i handen kan svaret VERIFIERAS: kommer något annat
    // har WM:en vägrat eller klampat, och då lämnas krediten tillbaka.
    //
    // ETT försök per skuld. `writeNormalHints()` sätter fysiska
    // WM_NORMAL_HINTS, så vår EGEN gräns kan göra målet ouppnåeligt — en
    // villkorslös "läk varje avvikelse" vore då en oändlig loop
    // (Configure(A) → begär B → WM svarar A → …).
    bool restoreSizeOwed_ = false;
    int  restoreExpectW_ = 0, restoreExpectH_ = 0;

    // INGEN ORDNINGSGISSNING — det är ett medvetet val, inte en förenkling.
    // Ordningen mellan `PropertyNotify(_NET_WM_STATE)` och
    // `ConfigureNotify` är WM-beroende och odokumenterad; under
    // Mutter/XWayland kommer propertyn FÖRE configuren vid
    // maximering men EFTER vid återställning — alltså inte ens samma
    // ordning åt de två hållen.
    //
    // Att KLASSIFICERA configuren går inte: (1) en "configure setts"-flagga
    // kan inte skilja maximeringens configure från återställningens,
    // eftersom BÅDA anländer med `maximized_` fortfarande true under
    // Mutter; (2) en geometrisk skiljedom faller på att den maximerade
    // storleken bara lagras om maximeringens configure kommer EFTER
    // propertyn — under den omvända ordningen finns den aldrig, och då
    // korrigerar restore-flanken mot maximerad geometri.
    //
    // Lösningen är att inte BEHÖVA veta vilken configure som är vilken.
    // Skulden konsumeras SYNKRONT på restore-flanken (den kan därför
    // aldrig överleva en återställning), och `restoreExpect` avgör i
    // efterhand vad som var vårt svar. Hinner WM:ens restore-rect emellan
    // klassas den som en vägran — och då behålls den LAGRADE logiska
    // storleken, för den är sanningen medan den fysiska är den
    // föråldrade. Krediten lämnas tillbaka och nästa configure räknar om
    // normalt. Båda ordningarna landar därmed rätt, utan att
    // någon väg kan lämna skulden eller krediten hängande.

    // Korrigeringen. Anropas från restore-flanken; gör ETT försök.
    void maybeCorrectRestoreSize();

    // MUSPEKAREN: rollen under pekaren i DET HÄR fönstret och den
    // font-pekare som senast definierades (-1 = ingen än) — `refreshCursor`
    // definierar bara när valet ändrats.
    HitRole cursorRole_    = HitRole::Client;
    int     appliedCursor_ = -1;
    void    refreshCursor();

    // PROGRAMIKONEN — ramarna (tvåfas: lagras före init) och
    // om `_NET_WM_ICON` bär dem. `applyClassHint` skriver WM_CLASS ur
    // `gfx::appId()` — ICCCM vill ha den före första `XMapWindow`.
    WindowIcon icon_;
    bool       iconApplied_ = false;
    void       applyIcon();
    void       applyClassHint();

    bool visible_   = true;
    bool maximized_ = false;
    bool overrideRedirect_ = false;   // popups

    DisplayMetrics metrics_;   // skärmens mm (measureDisplay)
};

} // namespace gfx
