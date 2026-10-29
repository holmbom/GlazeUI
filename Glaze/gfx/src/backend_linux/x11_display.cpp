// X11Display — X11-sidan av LinuxDisplay (täcker även XWayland): Xlib-
// anslutning, atom-cache, event-översättning (core-events + xkbcommon, och
// pekarens rörelse och knappar via XInput 2 — styrplattans
// skrollvalutorer och XI 2.4-nypningen finns bara där),
// selections-klippbordet och EGL-mot-X11. Keymappen byggs ur rotens
// _XKB_RULES_NAMES-property + xkb_keymap_new_from_names — inte xkbcommon-x11
// (ett extra dev-paket med egen header/.pc), och RMLVO-vägen är ren Xlib + xkbcommon med samma resultat
// för keymappen. Key-repeat är X-serverns (detectable autorepeat =
// upprepade KeyPress) — ingen egen timer behövs här.

#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../cursor_bitmaps.h"
#include "../display_util.h"
#include "gfx/platform.h"
#include "x11_backend.h"
#include "xkb_keys.h"

#include <EGL/eglext.h>   // efter egl.h (via linux_backend.h)

namespace gfx {

namespace {


void pushSimple(WindowId wid, EventType t)
{
    Event e;
    e.type     = t;
    e.windowId = wid;
    pushEvent(e);
}

std::uint32_t translateStateMask(unsigned state)
{
    // Core-eventens modifierarmask: Mod1 = Alt, Mod4 = Super (standard-
    // mappningen; exotiska ommappningar får leva med avvikelsen). XI2-
    // eventens `mods.effective` bär samma bitar och går samma väg.
    std::uint32_t mods = 0;
    if (state & ControlMask) mods |= kModCtrl;
    if (state & ShiftMask)   mods |= kModShift;
    if (state & Mod1Mask)    mods |= kModAlt;
    if (state & Mod4Mask)    mods |= kModCmd;
    return mods;
}

// PEKARENS RÖRELSE — ur kärnans MotionNotify eller XI2:s XI_Motion. (x, y) är
// X-fönstrets koordinater; storleksmarginalen (E2L) dras av här, så fönstret
// och appen ser KLIENTENS px — samma regel i varje pekarväg nedan.
void pointerMotion(X11Window* w, float x, float y, std::uint32_t mods)
{
    x -= static_cast<float>(w->frameMargin());
    y -= static_cast<float>(w->frameMargin());
    Event e;
    e.type     = EventType::MouseMotion;
    e.windowId = w->id();
    e.mouseX   = x;
    e.mouseY   = y;
    e.mods     = mods;
    pushEvent(e);
    // Cursorbyte över resize-kanterna — och tillbaka till fönstrets
    // begärda form i klientytan.
    w->onCursorRole(w->hitRoleAt(x, y));
}

// KNAPPARNA — ur kärnans ButtonPress/ButtonRelease eller XI2:s. Knapp 4–7
// är hjulet: tryck och släpp kommer i par, så bara trycket räknas. Hjulet bär
// modifierarna som knapparna (kontraktet vid `kModCtrl`).
void pointerButton(X11Window* w, bool pressed, unsigned btn, float x, float y,
                   int xRoot, int yRoot, unsigned long time,
                   std::uint32_t mods)
{
    x -= static_cast<float>(w->frameMargin());   // klientens px (E2L)
    y -= static_cast<float>(w->frameMargin());
    if (btn >= 4 && btn <= 7) {
        if (!pressed)
            return;
        Event e;
        e.type     = EventType::MouseWheel;
        e.windowId = w->id();
        e.mouseX   = x;
        e.mouseY   = y;
        e.wheelY   = btn == 4 ? 1.0f : btn == 5 ? -1.0f : 0.0f;
        e.wheelX   = btn == 6 ? -1.0f : btn == 7 ? 1.0f : 0.0f;
        e.mods     = mods;
        pushEvent(e);
        return;
    }
    MouseButton mb = MouseButton::None;
    if (btn == Button1)      mb = MouseButton::Left;
    else if (btn == Button2) mb = MouseButton::Middle;
    else if (btn == Button3) mb = MouseButton::Right;
    // Tryck i Caption/Resize-zonerna — och i storleksmarginalen (E2L) —
    // → _NET_WM_MOVERESIZE (WM:et tar över draget); eventet konsumeras där.
    // Golvet och inte trunkering: en punkt en halv px ut i marginalen
    // (XI2:s koordinater är flyttal) ska inte avrundas in i klientytan.
    if (pressed && mb == MouseButton::Left &&
        w->interceptPress(static_cast<int>(std::floor(x)),
                          static_cast<int>(std::floor(y)), xRoot, yRoot,
                          time))
        return;
    Event e;
    e.type     = pressed ? EventType::MouseButtonDown
                         : EventType::MouseButtonUp;
    e.windowId = w->id();
    e.mouseX   = x;
    e.mouseY   = y;
    e.button   = mb;
    // Modifierarna NÄR knappen trycktes (Ctrl-/Shift-klick).
    // Kärnans `state` och XI2:s `mods` är läget före eventet, och för en
    // musknapp är det precis det som efterfrågas: Ctrl hålls redan när
    // klicket kommer.
    e.mods     = mods;
    pushEvent(e);
}

// XI2:s valutormask och PACKADE värden: värdet för valutor `n` står på det
// index som räknas av de satta bitarna under `n`.
bool valuatorSet(const XIValuatorState& vs, int n)
{
    return n >= 0 && n < vs.mask_len * 8 && XIMaskIsSet(vs.mask, n);
}

double valuatorValue(const XIValuatorState& vs, int n)   // kräver valuatorSet
{
    int idx = 0;
    for (int i = 0; i < n; ++i)
        if (XIMaskIsSet(vs.mask, i))
            ++idx;
    return vs.values[idx];
}

// _XKB_RULES_NAMES: fem NUL-separerade strängar (rules, model, layout,
// variant, options) på rotfönstret — X-serverns faktiska layout.
bool readXkbRulesNames(Display* dpy, ::Window root, Atom prop,
                       xkb_rule_names& out, std::string& storage)
{
    Atom type = 0;
    int  fmt  = 0;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(dpy, root, prop, 0, 1024, kX11False, XA_STRING,
                           &type, &fmt, &n, &after, &data) != Success ||
        !data || type != XA_STRING || fmt != 8) {
        if (data)
            XFree(data);
        return false;
    }
    storage.assign(reinterpret_cast<const char*>(data),
                   static_cast<std::size_t>(n));
    XFree(data);

    const char* p   = storage.c_str();
    const char* end = storage.c_str() + storage.size();
    const char* fields[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    for (int i = 0; i < 5 && p < end; ++i) {
        fields[i] = p;
        p += std::strlen(p) + 1;
    }
    out = xkb_rule_names{fields[0], fields[1], fields[2], fields[3],
                         fields[4]};
    return true;
}

} // namespace

// ---------------------------------------------------------------- create

LinuxDisplay* createX11Display()
{
    return X11Display::create();
}

X11Display* X11Display::create()
{
    auto* d = new X11Display();
    if (!d->connect()) {
        delete d;
        return nullptr;
    }
    return d;
}

bool X11Display::connect()
{
    dpy_ = XOpenDisplay(nullptr);
    if (!dpy_)
        return false;
    screen_ = DefaultScreen(dpy_);
    root_   = RootWindow(dpy_, screen_);

    auto atom = [&](const char* n) {
        return XInternAtom(dpy_, n, kX11False);
    };
    aWmProtocols_     = atom("WM_PROTOCOLS");
    aWmDelete_        = atom("WM_DELETE_WINDOW");
    aNetWmName_       = atom("_NET_WM_NAME");
    aUtf8_            = atom("UTF8_STRING");
    aNetWmState_      = atom("_NET_WM_STATE");
    aNetWmStateMaxV_  = atom("_NET_WM_STATE_MAXIMIZED_VERT");
    aNetWmStateMaxH_  = atom("_NET_WM_STATE_MAXIMIZED_HORZ");
    aNetWmStateAbove_ = atom("_NET_WM_STATE_ABOVE");
    aNetWmStateHidden_ = atom("_NET_WM_STATE_HIDDEN");   // minimerat
    aNetMoveResize_   = atom("_NET_WM_MOVERESIZE");
    aNetWorkarea_     = atom("_NET_WORKAREA");
    aMotifHints_      = atom("_MOTIF_WM_HINTS");
    aNetWmWindowType_ = atom("_NET_WM_WINDOW_TYPE");
    aNetWmTypeUtility_  = atom("_NET_WM_WINDOW_TYPE_UTILITY");
    aNetWmTypeDropdown_ = atom("_NET_WM_WINDOW_TYPE_DROPDOWN_MENU");
    aNetActiveWindow_ = atom("_NET_ACTIVE_WINDOW");
    aClipboard_       = atom("CLIPBOARD");
    aTargets_         = atom("TARGETS");
    aGlzSelection_    = atom("GLAZE_SELECTION");
    aIncr_            = atom("INCR");
    aNetWmIcon_       = atom("_NET_WM_ICON");   // programikonen
    // STORLEKSMARGINALEN (E2L): egenskapen, WM:ens lista och kompositörens
    // markering för den här skärmen.
    aGtkFrameExtents_ = atom("_GTK_FRAME_EXTENTS");
    aNetSupported_    = atom("_NET_SUPPORTED");
    {
        char namn[32];
        std::snprintf(namn, sizeof namn, "_NET_WM_CM_S%d", screen_);
        aNetWmCmS_ = atom(namn);
    }
    aNetWmUserTime_ = atom("_NET_WM_USER_TIME");   // första visningen utan fokus

    // Detectable autorepeat: repeterade KeyPress UTAN mellanliggande
    // Release — X-serverns repeat blir vår (ingen egen timer på X11).
    Bool supported = kX11False;
    XkbSetDetectableAutoRepeat(dpy_, kX11True, &supported);

    // Osynligt hjälpfönster för selections (ägare/mottagare).
    helper_ = XCreateSimpleWindow(dpy_, root_, -10, -10, 1, 1, 0, 0, 0);
    XSelectInput(dpy_, helper_, PropertyChangeMask);

    // Bevaka RESOURCE_MANAGER på root: GNOME skriver ny Xft.dpi där vid
    // text-/skalbyte i drift — PropertyNotify:n är den händelsestyrda
    // vägen till onResourceManagerChanged (ingen polling, hård regel 1).
    XSelectInput(dpy_, root_, PropertyChangeMask);

    readXftDpi();

    // XRandR (skärmens mm): crtc- och utgångsfrågorna kräver
    // 1.3 (`XRRGetScreenResourcesCurrent` frågar inte hårdvaran), och
    // RRScreenChangeNotify på roten är den händelsestyrda vägen när en
    // skärm kopplas in eller byter läge. Saknas tillägget mäts ingenting —
    // fönstren svarar Assumed med orsaken.
    int rrFel = 0, rrMaj = 0, rrMin = 0;
    randr_ = XRRQueryExtension(dpy_, &randrEvent_, &rrFel) &&
             XRRQueryVersion(dpy_, &rrMaj, &rrMin) &&
             (rrMaj > 1 || (rrMaj == 1 && rrMin >= 3));
    if (randr_) {
        XRRSelectInput(dpy_, root_, RRScreenChangeNotifyMask);
        readScreens();
    } else {
        log("x11: XRandR 1.3 saknas — skärmens millimeter kan inte mätas");
    }

    // XInput 2 — före första fönstret, som väljer sina pekarevent
    // efter vad versionen tillåter.
    initXi2();

    // xkbcommon: keymap ur rotens _XKB_RULES_NAMES (se filhuvudet).
    xkb_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_rule_names names{};
    std::string    storage;
    if (readXkbRulesNames(dpy_, root_, atom("_XKB_RULES_NAMES"), names,
                          storage))
        keymap_ = xkb_keymap_new_from_names(xkb_, &names,
                                            XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!keymap_)
        keymap_ = xkb_keymap_new_from_names(xkb_, nullptr,
                                            XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap_)
        xkbState_ = xkb_state_new(keymap_);
    const char* locale = std::getenv("LC_ALL");
    if (!locale || !*locale) locale = std::getenv("LC_CTYPE");
    if (!locale || !*locale) locale = std::getenv("LANG");
    if (!locale || !*locale) locale = "C";
    composeTable_ = xkb_compose_table_new_from_locale(
        xkb_, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
    if (composeTable_)
        composeState_ = xkb_compose_state_new(composeTable_,
                                              XKB_COMPOSE_STATE_NO_FLAGS);

    // EGL mot X11-displayen.
    auto getPlatformDisplay =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (getPlatformDisplay)
        egl_ = getPlatformDisplay(EGL_PLATFORM_X11_KHR, dpy_, nullptr);
    if (egl_ == EGL_NO_DISPLAY)
        egl_ = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(dpy_));
    if (egl_ == EGL_NO_DISPLAY ||
        eglInitialize(egl_, nullptr, nullptr) != EGL_TRUE) {
        log("x11: EGL kunde inte initieras");
        return false;
    }
    const EGLint base[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                           EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                           EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                           EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0, EGL_NONE};
    EGLint n = 0;
    if (eglChooseConfig(egl_, base, &cfg24_, 1, &n) != EGL_TRUE || n < 1) {
        log("x11: ingen EGL-konfig");
        return false;
    }
    const EGLint argb[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                           EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                           EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                           EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    if (eglChooseConfig(egl_, argb, &cfg32_, 1, &n) != EGL_TRUE || n < 1)
        cfg32_ = nullptr;   // transparent degraderar till opakt (loggas)
    return true;
}

X11Display::~X11Display()
{
    for (auto& [id, cur] : cursors_)
        XFreeCursor(dpy_, cur);
    if (composeState_)
        xkb_compose_state_unref(composeState_);
    if (composeTable_)
        xkb_compose_table_unref(composeTable_);
    if (xkbState_)
        xkb_state_unref(xkbState_);
    if (keymap_)
        xkb_keymap_unref(keymap_);
    if (xkb_)
        xkb_context_unref(xkb_);
    if (egl_ != EGL_NO_DISPLAY)
        eglTerminate(egl_);
    if (helper_)
        XDestroyWindow(dpy_, helper_);
    if (dpy_)
        XCloseDisplay(dpy_);
}

void X11Display::readXftDpi()
{
    // Xft.dpi ur resurssträngen — GNOME lägger textskalningen här
    // (120 → 1.25), så portalens textfaktor
    // multipliceras INTE in igen (annars dubbelappliceras den).
    // OBS: XResourceManagerString returnerar bara strängen som cachades
    // när anslutningen öppnades — för skalbyte i DRIFT måste
    // RESOURCE_MANAGER läsas färskt från root med XGetWindowProperty.
    uiScale_ = 1.0f;
    std::string rmStorage;
    const char* rm = nullptr;
    Atom propType = 0;
    int  propFmt  = 0;
    unsigned long propN = 0, propAfter = 0;
    unsigned char* propData = nullptr;
    if (XGetWindowProperty(dpy_, root_, XA_RESOURCE_MANAGER, 0,
                           1024 * 1024, kX11False, XA_STRING, &propType,
                           &propFmt, &propN, &propAfter,
                           &propData) == Success &&
        propData && propType == XA_STRING && propFmt == 8) {
        rmStorage.assign(reinterpret_cast<const char*>(propData),
                         static_cast<std::size_t>(propN));
        rm = rmStorage.c_str();
    }
    if (propData)
        XFree(propData);
    if (!rm)
        rm = XResourceManagerString(dpy_);   // init-cachen som fallback
    if (!rm)
        return;
    static bool xrmInit = [] {
        XrmInitialize();
        return true;
    }();
    (void)xrmInit;
    XrmDatabase db = XrmGetStringDatabase(rm);
    if (!db)
        return;
    char*    type = nullptr;
    XrmValue val{};
    if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &val) &&
        val.addr) {
        const double dpi = std::atof(val.addr);
        if (dpi > 0.0)
            uiScale_ = static_cast<float>(dpi / 96.0);
    }
    XrmDestroyDatabase(db);
}

XVisualInfo X11Display::visualFor(bool transparent) const
{
    // EGL-konfigens nativa visual — fönstret MÅSTE skapas med samma
    // visual som ytan, annars BadMatch vid eglCreateWindowSurface.
    EGLint vid = 0;
    eglGetConfigAttrib(egl_, eglConfig(transparent),
                       EGL_NATIVE_VISUAL_ID, &vid);
    XVisualInfo tmpl{};
    tmpl.visualid = static_cast<VisualID>(vid);
    int          cnt = 0;
    XVisualInfo* vi  = XGetVisualInfo(dpy_, VisualIDMask, &tmpl, &cnt);
    XVisualInfo  out{};
    if (vi && cnt > 0)
        out = vi[0];
    if (vi)
        XFree(vi);
    return out;
}

// STORLEKSMARGINALEN — se headern. GTK3:s regel för sina
// klientritade skuggor: en kompositör OCH en WM som känner
// `_GTK_FRAME_EXTENTS`. Två serveranrop, en gång per fönster.
bool X11Display::frameExtentsUsable() const
{
    if (!cfg32_)
        return false;   // ingen ARGB-konfig — marginalen kan inte vara genomskinlig
    if (XGetSelectionOwner(dpy_, aNetWmCmS_) == 0)
        return false;   // ingen kompositör — marginalen hade ritats svart
    Atom           typ  = 0;
    int            fmt  = 0;
    unsigned long  n    = 0, efter = 0;
    unsigned char* data = nullptr;
    bool           känd = false;
    if (XGetWindowProperty(dpy_, root_, aNetSupported_, 0, 4096, kX11False,
                           XA_ATOM, &typ, &fmt, &n, &efter, &data) == Success &&
        data && typ == XA_ATOM && fmt == 32) {
        const Atom* lista = reinterpret_cast<const Atom*>(data);
        for (unsigned long i = 0; i < n && !känd; ++i)
            känd = lista[i] == aGtkFrameExtents_;
    }
    if (data)
        XFree(data);
    return känd;
}

std::unique_ptr<Window> X11Display::makeWindow()
{
    return std::make_unique<X11Window>(this);
}

void X11Display::registerWindow(::Window w, X11Window* win)
{
    windows_[w] = win;
}

void X11Display::unregisterWindow(::Window w)
{
    windows_.erase(w);
}

void X11Display::windowDestroyed(X11Window* win)
{
    // En nypning som pågick dör tyst med sitt fönster.
    if (pinchWin_ == win)
        pinchWin_ = nullptr;
}

X11Window* X11Display::windowFor(::Window w) const
{
    const auto it = windows_.find(w);
    return it == windows_.end() ? nullptr : it->second;
}

// ---------------------------------------------------------------- pumpen

void X11Display::prepoll()
{
    // Dränera allt Xlib redan läst in (annars sover epoll på en tom fd
    // medan kön bär events) och flusha utgående requests.
    while (XPending(dpy_) > 0) {
        XEvent ev;
        XNextEvent(dpy_, &ev);
        translate(ev);
    }
    XFlush(dpy_);
}

void X11Display::postpoll(bool fdReady)
{
    if (!fdReady)
        return;
    while (XPending(dpy_) > 0) {
        XEvent ev;
        XNextEvent(dpy_, &ev);
        translate(ev);
    }
}

// ------------------------------------------------------- event-översättning

void X11Display::handleKey(XEvent& ev, bool pressed)
{
    X11Window* win = windowFor(ev.xkey.window);
    if (!win || !xkbState_)
        return;
    const xkb_keycode_t keycode = ev.xkey.keycode;
    // Den PRODUCERADE symbolen (modifierare pålagda) — den behövs för
    // compose/TextInput nedan, alltså för TECKNET. `Key` är en annan
    // fråga (level-0-symbolen, se gfx/events.h) och ställs separat.
    const xkb_keysym_t sym = xkb_state_key_get_one_sym(xkbState_, keycode);

    Event e;
    e.type     = pressed ? EventType::KeyDown : EventType::KeyUp;
    e.windowId = win->id();
    // Tangent-mappen är DELAD med Wayland-drivrutinen (xkb_keys.h).
    // Kontraktet står vid
    // `enum class Key` i gfx/events.h.
    e.key      = translateXkbKey(xkbState_, keymap_, keycode);
    // Kärnans `state` är läget FÖRE eventet, men en modifierartangents eget
    // event bär läget EFTER sig (kontraktet vid `kModCtrl` i gfx/events.h):
    // annars såg `modifierKeys()` inte en ensam Ctrl förrän nästa
    // mushändelse, och ett släppt Ctrl stod kvar till dess.
    e.mods     = xkbModsAfterKey(translateStateMask(ev.xkey.state), sym,
                                 pressed);

    // Uppdatera xkb-state EFTER utläsning (shift-läget i eventet gäller
    // tecknet som trycktes).
    xkb_state_update_key(xkbState_, keycode,
                         pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

    if (!pressed) {
        pushEvent(e);
        return;
    }

    // TEXTEN FÖRE KeyDown: KeyDown bär texten
    // trycket skriver, och dess TextInput köas direkt efter — Waylands
    // ordning. Ctrl, Alt och Super skriver inget (`xkbTypesText`, prövad
    // FÖRE compose); AltGr gör det.
    char buf[16] = {};
    int  len     = 0;
    if (xkbTypesText(xkbState_, keymap_, keycode)) {
        bool handled = false;
        bool noText  = false;
        if (composeState_ &&
            xkb_compose_state_feed(composeState_, sym) ==
                XKB_COMPOSE_FEED_ACCEPTED) {
            switch (xkb_compose_state_get_status(composeState_)) {
            case XKB_COMPOSE_COMPOSING:
                noText = true;
                break;
            case XKB_COMPOSE_COMPOSED:
                len = xkb_compose_state_get_utf8(composeState_, buf,
                                                 sizeof buf);
                xkb_compose_state_reset(composeState_);
                handled = true;
                break;
            case XKB_COMPOSE_CANCELLED:
                xkb_compose_state_reset(composeState_);
                noText = true;
                break;
            case XKB_COMPOSE_NOTHING:
                break;
            }
        }
        if (!handled && !noText) {
            // OBS: state redan uppdaterat med trycket ovan — get_utf8 ger
            // tecknet för aktuell modifier-nivå.
            len = xkb_state_key_get_utf8(xkbState_, keycode, buf,
                                         sizeof buf);
        }
        if (len > 0) {
            const unsigned char first = static_cast<unsigned char>(buf[0]);
            if (first < 0x20 || first == 0x7F)
                len = 0;   // kontrolltecken — kommandon, inte text
        }
    }
    // `len` är det BEHÖVDA antalet byte och kan vara större än bufferten
    // (som då är kapad och NUL-terminerad); text[32] rymmer bufferten.
    std::size_t nCopy = len > 0 ? static_cast<std::size_t>(len) : 0;
    if (nCopy >= sizeof buf)
        nCopy = sizeof buf - 1;
    std::memcpy(e.text, buf, nCopy);
    pushEvent(e);
    if (nCopy == 0)
        return;

    Event t;
    t.type     = EventType::TextInput;
    t.windowId = win->id();
    std::memcpy(t.text, buf, nCopy);
    pushEvent(t);
}

void X11Display::translate(XEvent& ev)
{
    // XINPUT 2: pekarens rörelse, knappar och nypning, och rotens
    // enhetsbyten, kommer som GenericEvent-kakor. Datat hämtas och släpps
    // här — det gäller bara till nästa XNextEvent.
    if (xi2_ && ev.type == GenericEvent && ev.xcookie.extension == xiOpcode_) {
        if (XGetEventData(dpy_, &ev.xcookie)) {
            translateXi(ev.xcookie);
            XFreeEventData(dpy_, &ev.xcookie);
        }
        return;
    }
    // SKÄRMENS mm: en skärm kopplades in, ur eller bytte läge. Listan läses
    // om och varje fönster mäter om (eventbasen är tilläggets, inte ett
    // case-värde — därför före switchen).
    if (randr_ && ev.type == randrEvent_ + RRScreenChangeNotify) {
        XRRUpdateConfiguration(&ev);
        readScreens();
        for (auto& [xw, win] : windows_)
            win->measureDisplay(true);
        return;
    }
    switch (ev.type) {
    case Expose:
        if (ev.xexpose.count == 0)
            if (X11Window* w = windowFor(ev.xexpose.window))
                pushSimple(w->id(), EventType::WindowExposed);
        break;

    case ConfigureNotify:
        if (X11Window* w = windowFor(ev.xconfigure.window)) {
            w->onConfigure(ev.xconfigure.width, ev.xconfigure.height);
            w->measureDisplay(true);   // kanske flyttat till en annan skärm
        }
        break;

    case MapNotify:
        if (X11Window* w = windowFor(ev.xmap.window))
            pushSimple(w->id(), EventType::WindowShown);
        break;

    case ClientMessage:
        if (static_cast<Atom>(ev.xclient.data.l[0]) == aWmDelete_)
            if (X11Window* w = windowFor(ev.xclient.window))
                pushSimple(w->id(), EventType::WindowCloseRequested);
        break;

    case FocusIn:
        if (X11Window* w = windowFor(ev.xfocus.window))
            pushSimple(w->id(), EventType::WindowFocusGained);
        break;
    // X11 SKICKAR ALDRIG `MouseCaptureLost`: ingen annan
    // klient kan ta en aktiv grab medan vår implicita grab gäller, och det
    // som ändå avbryter ett tryck — Alt+Tab, en WM-dialog — tar fokus, så
    // `FocusOut` nedan bär avbrottet (glaze släpper trycket på fokusförlusten).
    case FocusOut:
        if (X11Window* w = windowFor(ev.xfocus.window))
            pushSimple(w->id(), EventType::WindowFocusLost);
        break;

    case EnterNotify:
        // Medan pekaren var utanför rullade styrplattan någon annans fönster:
        // valutorerna har gått vidare, så nästa event sätter en ny baslinje.
        xiResetScroll();
        if (X11Window* w = windowFor(ev.xcrossing.window)) {
            Event e;
            e.type     = EventType::MouseMotion;
            e.windowId = w->id();
            e.mouseX   = static_cast<float>(ev.xcrossing.x - w->frameMargin());
            e.mouseY   = static_cast<float>(ev.xcrossing.y - w->frameMargin());
            e.mods     = translateStateMask(ev.xcrossing.state);
            pushEvent(e);
        }
        break;
    case LeaveNotify:
        if (X11Window* w = windowFor(ev.xcrossing.window))
            pushSimple(w->id(), EventType::MouseLeave);
        break;

    // Kärnans pekare — bara utan XI2 (med XI2 väljer fönstren XI_Motion och
    // XI_ButtonPress/Release, och då levererar X-servern inte de här).
    case MotionNotify:
        if (X11Window* w = windowFor(ev.xmotion.window))
            pointerMotion(w, static_cast<float>(ev.xmotion.x),
                          static_cast<float>(ev.xmotion.y),
                          translateStateMask(ev.xmotion.state));
        break;

    case ButtonPress:
    case ButtonRelease:
        if (X11Window* w = windowFor(ev.xbutton.window))
            pointerButton(w, ev.type == ButtonPress, ev.xbutton.button,
                          static_cast<float>(ev.xbutton.x),
                          static_cast<float>(ev.xbutton.y), ev.xbutton.x_root,
                          ev.xbutton.y_root, ev.xbutton.time,
                          translateStateMask(ev.xbutton.state));
        break;

    case KeyPress:
        handleKey(ev, true);
        break;
    case KeyRelease:
        handleKey(ev, false);
        break;

    case PropertyNotify:
        if (ev.xproperty.window == root_ &&
            ev.xproperty.atom == XA_RESOURCE_MANAGER)
            onResourceManagerChanged();   // Xft.dpi kan ha bytt (skalbyte)
        else if (ev.xproperty.atom == aNetWmState_)
            if (X11Window* w = windowFor(ev.xproperty.window))
                w->onPropertyNetWmState();
        break;

    case SelectionRequest:
        handleSelectionRequest(ev.xselectionrequest);
        break;
    case SelectionClear:
        if (ev.xselectionclear.selection == aClipboard_)
            weOwnSelection_ = false;
        break;

    case MappingNotify:
        XRefreshKeyboardMapping(&ev.xmapping);
        break;

    default:
        break;
    }
}

namespace {

// Skärmens id ur utgångens EDID-egenskap — tillverkare-produkt-serie, samma
// form som på Windows och macOS. Tom när drivrutinen inte exponerar EDID.
std::string edidIdFor(Display* dpy, RROutput out)
{
    const Atom edidAtom = XInternAtom(dpy, "EDID", kX11True);
    if (edidAtom == kX11None)
        return {};
    Atom           typ    = 0;
    int            format = 0;
    unsigned long  n = 0, kvar = 0;
    unsigned char* data = nullptr;
    std::string    id;
    if (XRRGetOutputProperty(dpy, out, edidAtom, 0, 64, kX11False,
                             kX11False, AnyPropertyType, &typ, &format, &n,
                             &kvar, &data) == Success &&
        data && format == 8) {
        display::Edid e;
        if (display::parseEdid(data, n, e))
            id = e.id;
    }
    if (data)
        XFree(data);
    return id;
}

} // namespace

void X11Display::readScreens()
{
    screens_.clear();
    XRRScreenResources* res = XRRGetScreenResourcesCurrent(dpy_, root_);
    if (!res)
        return;
    // RandR:s PRIMÄRA utgång — 0 när ingen är satt;
    // `placementScreens` faller då tillbaka på skärmen vid origo.
    const RROutput primär = XRRGetOutputPrimary(dpy_, root_);
    for (int i = 0; i < res->ncrtc; ++i) {
        XRRCrtcInfo* ci = XRRGetCrtcInfo(dpy_, res, res->crtcs[i]);
        if (!ci)
            continue;
        if (ci->mode != kX11None && ci->noutput > 0 && ci->width > 0 &&
            ci->height > 0) {
            Screen s;
            s.x = ci->x;
            s.y = ci->y;
            s.w = static_cast<int>(ci->width);
            s.h = static_cast<int>(ci->height);
            for (int k = 0; k < ci->noutput; ++k)
                if (primär != 0 && ci->outputs[k] == primär)
                    s.primary = true;
            if (XRROutputInfo* oi = XRRGetOutputInfo(dpy_, res,
                                                     ci->outputs[0])) {
                s.mmW = static_cast<int>(oi->mm_width);
                s.mmH = static_cast<int>(oi->mm_height);
                s.id  = edidIdFor(dpy_, ci->outputs[0]);
                if (s.id.empty() && oi->name)
                    s.id = oi->name;
                XRRFreeOutputInfo(oi);
            }
            screens_.push_back(std::move(s));
        }
        XRRFreeCrtcInfo(ci);
    }
    XRRFreeScreenResources(res);
}

// ---------------------------------------------------------------- skärmarna
//
// SKÄRMARNA. Placeringsrymden på X11 är rotens px.

bool X11Display::readWorkarea(Rectf& ut) const
{
    Atom           type = 0;
    int            fmt  = 0;
    unsigned long  n = 0, after = 0;
    unsigned char* data = nullptr;
    bool           ok   = false;
    // `_NET_WORKAREA` bär ETT fyrtal per virtuellt skrivbord — läs det
    // AKTUELLA skrivbordets (`_NET_CURRENT_DESKTOP`).
    // Saknas egenskapen gäller skrivbord 0.
    long skrivbord = 0;
    {
        const Atom aCurrent = XInternAtom(dpy_, "_NET_CURRENT_DESKTOP", kX11False);
        if (XGetWindowProperty(dpy_, root_, aCurrent, 0, 1, kX11False,
                               XA_CARDINAL, &type, &fmt, &n, &after,
                               &data) == Success &&
            data && n >= 1)
            skrivbord = std::max(0L, *reinterpret_cast<const long*>(data));
        if (data)
            XFree(data);
        data = nullptr;
    }
    if (XGetWindowProperty(dpy_, root_, aNetWorkarea_, skrivbord * 4, 4, kX11False,
                           XA_CARDINAL, &type, &fmt, &n, &after,
                           &data) == Success &&
        data && n >= 4) {
        // Format 32 är `long` i Xlib, också på en 64-bitarsmaskin.
        const long* v = reinterpret_cast<const long*>(data);
        ut = Rectf{static_cast<float>(v[0]), static_cast<float>(v[1]),
                   static_cast<float>(v[2]), static_cast<float>(v[3])};
        ok = ut.w > 0.0f && ut.h > 0.0f;
    }
    if (data)
        XFree(data);
    return ok;
}

// Varje AKTIV crtc (RandR) — utan RandR, eller utan en enda aktiv crtc, hela
// rotfönstret — med arbetsytan som snittet med `_NET_WORKAREA`. EWMH ger EN
// arbetsyta för hela skrivbordet, så med paneler på bara en av flera skärmar
// blir arbetsytan per skärm ungefärlig (dokumenterat). Ett tomt snitt ger
// hela skärmen. Skalan är Xft.dpi/96 × användarskalan — samma faktor som
// `X11Window::uiScale`, så ett fönster med den logiska bredden B är B ×
// scale rotpixlar brett.
std::vector<ScreenInfo> X11Display::placementScreens() const
{
    std::vector<ScreenInfo> ut;
    Rectf      yta;
    const bool harYta = readWorkarea(yta);
    const float skala = uiScale_ * userScale();
    auto lägg = [&](int x, int y, int w, int h, bool primär) {
        ScreenInfo s;
        s.bounds = Rectf{static_cast<float>(x), static_cast<float>(y),
                         static_cast<float>(w), static_cast<float>(h)};
        s.work   = s.bounds;
        if (harYta) {
            const float x0 = std::max(s.bounds.x, yta.x);
            const float y0 = std::max(s.bounds.y, yta.y);
            const float x1 = std::min(s.bounds.x + s.bounds.w, yta.x + yta.w);
            const float y1 = std::min(s.bounds.y + s.bounds.h, yta.y + yta.h);
            if (x1 > x0 && y1 > y0)
                s.work = Rectf{x0, y0, x1 - x0, y1 - y0};
        }
        s.scale   = skala;
        s.primary = primär;
        ut.push_back(s);
    };
    for (const Screen& sc : screens_)
        lägg(sc.x, sc.y, sc.w, sc.h, sc.primary);
    if (ut.empty())
        lägg(0, 0, DisplayWidth(dpy_, screen_), DisplayHeight(dpy_, screen_),
             true);
    // Ingen primär utgång satt: skärmen vid origo — rotens (0, 0) — annars den
    // första. Origo hör till primärskärmen i placeringsrymden.
    if (std::none_of(ut.begin(), ut.end(),
                     [](const ScreenInfo& s) { return s.primary; })) {
        auto vidOrigo = std::find_if(ut.begin(), ut.end(),
                                     [](const ScreenInfo& s) {
                                         return s.bounds.contains(0.0f, 0.0f);
                                     });
        (vidOrigo != ut.end() ? *vidOrigo : ut.front()).primary = true;
    }
    return ut;
}

void X11Display::onResourceManagerChanged()
{
    const float old = uiScale_;
    readXftDpi();
    if (uiScale_ == old)
        return;   // annan resurs ändrades — inget skalbyte
    // Skalbyte i drift: varje fönster räknar om sin fysiska storlek ur
    // den lagrade logiska och pushar WindowDisplayScaleChanged (widgets
    // laddar om fonten + ritar om). Skärmens mm mäts inte om: varken
    // RandR:s mått eller reserven beror på skalan.
    for (auto& [xw, win] : windows_)
        win->onUiScaleChanged();
}

// ---------------------------------------------------------------- XInput 2

// STYRPLATTAN på X11. Kärnprotokollet ser styrplattans
// tvåfingersdrag bara som knapp 4–7 — X-servern emulerar ett klick per
// `increment` av en skrollvalutor — och nypningen inte alls. Båda finns i
// XInput 2: skrollvalutorerna från 2.1 och XI_GesturePinch* från 2.4.
// Under 2.1 går pekaren den gamla kärnvägen, helt och hållet.
void X11Display::initXi2()
{
    int ev = 0, err = 0;
    if (!XQueryExtension(dpy_, "XInputExtension", &xiOpcode_, &ev, &err)) {
        log("x11: XInput saknas — styrplattans tvåfingersdrag blir hjulklick "
            "och nypningen når inte appen");
        return;
    }
    int major = 2, minor = 4;   // det vi kan; servern svarar med sitt tak
    if (XIQueryVersion(dpy_, &major, &minor) != Success ||
        major < 2 || (major == 2 && minor < 1)) {
        log("x11: XInput %d.%d är äldre än 2.1 — pekaren går kärnvägen, "
            "styrplattans tvåfingersdrag blir hjulklick",
            major, minor);
        return;
    }
    xi2_     = true;
    xiPinch_ = major > 2 || minor >= 4;
    if (!xiPinch_)
        log("x11: XInput %d.%d är äldre än 2.4 — styrplattans nypning och "
            "vridning når inte appen",
            major, minor);
    // Klientpekaren är master-pekaren tills ett XI2-event säger annat.
    int cp = 0;
    if (XIGetClientPointer(dpy_, kX11None, &cp))
        xiPointer_ = cp;
    // Enheter som kommer och går (XI_HierarchyChanged) och klasser som
    // byts (XI_DeviceChanged) — båda väljs på roten för alla enheter.
    unsigned char mask[XIMaskLen(XI_LASTEVENT)] = {};
    XISetMask(mask, XI_HierarchyChanged);
    XISetMask(mask, XI_DeviceChanged);
    XIEventMask em{XIAllDevices, static_cast<int>(sizeof mask), mask};
    XISelectEvents(dpy_, root_, &em, 1);
    readXiDevices();
}

// STYRPLATTORNA och deras skrollvalutorer. En enhet är en styrplatta när
// den har en XIGestureClass (XI 2.4 — xf86-input-libinput ≥ 1.2) eller
// egenskapen "libinput Tapping Enabled" (varje libinput-styrplatta). Under
// libinput är valutorns enhet styrplattans px — samma som pekarens rörelse
// — och `increment` användarens "px per klick" (Scroll Pixel Distance), som
// det precisa hjulet inte behöver. Startvärdet är valutorns aktuella värde.
void X11Display::readXiDevices()
{
    xiTouchpads_.clear();
    int           n    = 0;
    XIDeviceInfo* info = XIQueryDevice(dpy_, XIAllDevices, &n);
    if (!info)
        return;
    const Atom tapping =
        XInternAtom(dpy_, "libinput Tapping Enabled", kX11True);
    for (int i = 0; i < n; ++i) {
        const XIDeviceInfo& d = info[i];
        if (d.use != XISlavePointer || !d.enabled)
            continue;
        std::vector<XiScroll> axes;
        bool                  gesture = false;
        for (int k = 0; k < d.num_classes; ++k) {
            const XIAnyClassInfo* c = d.classes[k];
            if (c->type == XIScrollClass) {
                const auto* sc = reinterpret_cast<const XIScrollClassInfo*>(c);
                XiScroll s;
                s.number   = sc->number;
                s.vertical = sc->scroll_type == XIScrollTypeVertical;
                s.inverted = sc->increment < 0.0;
                axes.push_back(s);
            } else if (c->type == XIGestureClass) {
                gesture = true;
            }
        }
        if (axes.empty())
            continue;
        bool touchpad = gesture;
        if (!touchpad && tapping != static_cast<Atom>(kX11None)) {
            int   np    = 0;
            Atom* props = XIListProperties(dpy_, d.deviceid, &np);
            for (int p = 0; p < np && !touchpad; ++p)
                touchpad = props[p] == tapping;
            if (props)
                XFree(props);
        }
        if (!touchpad)
            continue;
        for (int k = 0; k < d.num_classes; ++k) {
            if (d.classes[k]->type != XIValuatorClass)
                continue;
            const auto* vc =
                reinterpret_cast<const XIValuatorClassInfo*>(d.classes[k]);
            for (XiScroll& s : axes)
                if (s.number == vc->number) {
                    s.last      = vc->value;
                    s.lastValid = true;
                }
        }
        xiTouchpads_[d.deviceid] = std::move(axes);
    }
    XIFreeDeviceInfo(info);
}

void X11Display::xiResetScroll()
{
    for (auto& [id, axes] : xiTouchpads_)
        for (XiScroll& s : axes)
            s.lastValid = false;
}

// Fönstrets pekarevent via XI2, för master-enheterna — eventen bär slav-
// enheten i `sourceid`. Nypningen bara när servern kan den: en okänd bit i
// masken är BadValue, och utan egen felhanterare avslutar Xlib appen.
void X11Display::selectXiPointer(::Window w)
{
    if (!xi2_)
        return;
    unsigned char mask[XIMaskLen(XI_LASTEVENT)] = {};
    XISetMask(mask, XI_Motion);
    XISetMask(mask, XI_ButtonPress);
    XISetMask(mask, XI_ButtonRelease);
    if (xiPinch_) {   // alla tre eller ingen — servern kräver det
        XISetMask(mask, XI_GesturePinchBegin);
        XISetMask(mask, XI_GesturePinchUpdate);
        XISetMask(mask, XI_GesturePinchEnd);
    }
    XIEventMask em{XIAllMasterDevices, static_cast<int>(sizeof mask), mask};
    XISelectEvents(dpy_, w, &em, 1);
}

// Två anrop och inte ett: ett tryck som levererats via XI2 öppnar en
// XI2-grab, och `XIUngrabDevice` är dess släpp (GTK:s XI2-väg). Under
// XWayland i WSLg släpper `XUngrabPointer` den OCKSÅ — med XTest räckte
// båda var för sig — men det är serverns tolkning, så
// båda skickas och ingen server lämnar grabben kvar.
void X11Display::ungrabPointer(unsigned long time)
{
    XUngrabPointer(dpy_, time);
    if (xi2_ && xiPointer_ > 0)
        XIUngrabDevice(dpy_, xiPointer_, time);
    XFlush(dpy_);   // släppt NU, inte vid pumpens nästa flush
}

void X11Display::translateXi(XGenericEventCookie& cookie)
{
    switch (cookie.evtype) {
    case XI_HierarchyChanged: {
        // En enhet kom, gick, aktiverades eller kopplades om: läs om
        // tabellen, och klientpekaren — en borttagen master får inte bli
        // kvar som `ungrabPointer`s enhet (BadDevice avslutar appen).
        readXiDevices();
        int cp = 0;
        if (XIGetClientPointer(dpy_, kX11None, &cp))
            xiPointer_ = cp;
        break;
    }
    case XI_DeviceChanged: {
        const auto& ev = *static_cast<XIDeviceChangedEvent*>(cookie.data);
        if (ev.reason == XIDeviceChange) {
            readXiDevices();   // enhetens egna klasser byttes
        } else {
            // XISlaveSwitch: master-pekaren följer nu en annan slav — dess
            // baslinje sätts om av nästa event (GTK gör likadant).
            const auto it = xiTouchpads_.find(ev.sourceid);
            if (it != xiTouchpads_.end())
                for (XiScroll& s : it->second)
                    s.lastValid = false;
        }
        break;
    }
    case XI_Motion:
    case XI_ButtonPress:
    case XI_ButtonRelease:
        xiPointerEvent(cookie.evtype,
                       *static_cast<XIDeviceEvent*>(cookie.data));
        break;
    case XI_GesturePinchBegin:
    case XI_GesturePinchUpdate:
    case XI_GesturePinchEnd:
        xiPinch(cookie.evtype,
                *static_cast<XIGesturePinchEvent*>(cookie.data));
        break;
    default:
        break;
    }
}

// Rörelse och knappar via XI2. Positionerna är fönstrets fysiska px (med
// bråkdelar), modifierarna `mods.effective` — samma bitar som kärnans
// `state`.
//
// HJULET: en STYRPLATTAS tvåfingersdrag är valutorns deltan (`xiScroll`),
// och de knapp 4–7 X-servern EMULERAR ur samma valutor
// (`XIPointerEmulated`) är dubbletter som kastas. Ett MUSHJUL — varje enhet
// som inte är en styrplatta — rullar med sina knappar 4–7 precis som i
// kärnvägen, emulerade eller inte, och dess valutor läses inte. Skälet är
// baslinjen: valutorn är ackumulerad, så första eventet efter varje enter
// kan bara sätta en baslinje — för ett hjul hade det klicket försvunnit,
// medan knappen alltid bär ett helt klick. För styrplattan kostar samma
// baslinje några px.
void X11Display::xiPointerEvent(int evtype, const XIDeviceEvent& ev)
{
    X11Window* w = windowFor(ev.event);
    if (!w)
        return;
    xiPointer_ = ev.deviceid;
    const float x = static_cast<float>(ev.event_x);
    const float y = static_cast<float>(ev.event_y);
    const std::uint32_t mods =
        translateStateMask(static_cast<unsigned>(ev.mods.effective));
    const auto tp = xiTouchpads_.find(ev.sourceid);

    if (evtype == XI_Motion) {
        // Kärnans regel: X-servern gör ingen MotionNotify av ett event där
        // varken x (valutor 0) eller y (1) finns — ett rent skrollevent är
        // ingen rörelse.
        if (valuatorSet(ev.valuators, 0) || valuatorSet(ev.valuators, 1))
            pointerMotion(w, x, y, mods);
        if (tp != xiTouchpads_.end())
            xiScroll(w, ev, tp->second);
        return;
    }
    if (ev.detail >= 4 && ev.detail <= 7 && (ev.flags & XIPointerEmulated) &&
        tp != xiTouchpads_.end())
        return;   // styrplattans emulerade klick — valutorn har rullat redan
    pointerButton(w, evtype == XI_ButtonPress,
                  static_cast<unsigned>(ev.detail), x, y,
                  static_cast<int>(std::lround(ev.root_x)),
                  static_cast<int>(std::lround(ev.root_y)), ev.time, mods);
}

// Styrplattans deltan → det PRECISA hjulet (kontraktet vid
// `kPrecisePxPerRow` i gfx/events.h). Valutorns px är pekarens — fysiska px
// på X11, där pekaren aldrig skalas — så logiska px är px ÷ fönstrets fulla
// skala och raderna de ÷ kPrecisePxPerRow. Tecknen som knapparnas: en
// lodrät valutor växer NEDÅT (knapp 5), så den negeras; en vågrät växer åt
// höger (knapp 7). X11 har inget stopp för en skrollsekvens, så fasen är
// None — okänd, som kontraktet säger. Modifierarna är eventets
// `mods.effective`, läget UNDER draget — Ctrl/Cmd + draget kretsar på
// ritytan.
void X11Display::xiScroll(X11Window* w, const XIDeviceEvent& ev,
                          std::vector<XiScroll>& axes)
{
    double dh = 0.0, dv = 0.0;
    for (XiScroll& s : axes) {
        if (!valuatorSet(ev.valuators, s.number))
            continue;
        const double v = valuatorValue(ev.valuators, s.number);
        if (s.lastValid)
            (s.vertical ? dv : dh) += s.inverted ? s.last - v : v - s.last;
        s.last      = v;
        s.lastValid = true;
    }
    if (dh == 0.0 && dv == 0.0)
        return;
    const double pxPerRow = w->uiScale() * kPrecisePxPerRow;
    Event e;
    e.type     = EventType::MouseWheel;
    e.windowId = w->id();
    e.mouseX   = static_cast<float>(ev.event_x - w->frameMargin());   // E2L
    e.mouseY   = static_cast<float>(ev.event_y - w->frameMargin());
    e.wheelX   = static_cast<float>(dh / pxPerRow);
    e.wheelY   = static_cast<float>(-dv / pxPerRow);
    e.precise  = true;
    e.mods     = translateStateMask(static_cast<unsigned>(ev.mods.effective));
    pushEvent(e);
}

// XI 2.4-NYPNINGEN. `detail` är antalet fingrar — bara tvåfingersnypningen
// blir en `Gesture` (tre och fyra ingår inte). `scale` är
// ABSOLUT från begin (libinputs värde rakt igenom xf86-input-libinput), så
// kontraktets `gestureScale` är kvoten mot förra; `delta_angle` är grader
// från förra eventet, medurs positivt — libinputs tecken, och kontraktets.
// Ankaret är pekaren (`event_x/event_y`), som står still under gesten, och
// modifierarna eventets `mods.effective` som hjulets.
void X11Display::xiPinch(int evtype, const XIGesturePinchEvent& ev)
{
    X11Window* w = windowFor(ev.event);
    GesturePhase phase = GesturePhase::Update;
    float        scale = 1.0f, rotation = 0.0f;
    if (evtype == XI_GesturePinchBegin) {
        pinchWin_   = (w && ev.detail == 2) ? w : nullptr;
        pinchScale_ = 1.0;
        phase       = GesturePhase::Begin;
    } else if (!pinchWin_ || w != pinchWin_) {
        return;   // en nypning vi inte tog (fler fingrar, annat fönster)
    } else if (evtype == XI_GesturePinchUpdate) {
        if (ev.scale > 0.0) {
            scale       = static_cast<float>(ev.scale / pinchScale_);
            pinchScale_ = ev.scale;
        }
        rotation = static_cast<float>(ev.delta_angle);
    } else {
        phase = (ev.flags & XIGesturePinchEventCancelled) ? GesturePhase::Cancel
                                                          : GesturePhase::End;
    }
    if (!pinchWin_)
        return;
    Event e;
    e.type            = EventType::Gesture;
    e.windowId        = pinchWin_->id();
    e.mouseX          =   // klientens px (E2L)
        static_cast<float>(ev.event_x - pinchWin_->frameMargin());
    e.mouseY          =
        static_cast<float>(ev.event_y - pinchWin_->frameMargin());
    e.phase           = phase;
    e.gestureScale    = scale;
    e.gestureRotation = rotation;
    e.mods            = translateStateMask(
        static_cast<unsigned>(ev.mods.effective));
    pushEvent(e);
    if (phase == GesturePhase::End || phase == GesturePhase::Cancel)
        pinchWin_ = nullptr;
}

// ---------------------------------------------------------------- cursors

void X11Display::defineCursor(::Window w, int key)
{
    auto it = cursors_.find(key);
    if (it == cursors_.end()) {
        const Cursor c = key >= kCursorClosedHand
                             ? makePixmapCursor(key)
                             : XCreateFontCursor(dpy_, static_cast<unsigned>(key));
        it = cursors_.emplace(key, c).first;
    }
    XDefineCursor(dpy_, w, it->second);
    XFlush(dpy_);
}

// PIXMAP-PEKARNA. X-kärnans pekarfont
// har varken en stängd hand eller en tom pekare, så de byggs som tvåfärgade
// pixmap-pekare: källans 1-bitar visas i förgrundsfärgen (svart), 0-bitarna
// i bakgrundsfärgen (vit), och masken väljer vilka pixlar som syns.
// `XCreateBitmapFromData` vill ha raderna i bitmap-format: minst signifikanta
// biten först, varje rad utfylld till hela byte.
//
// Den stängda handen skalas med HELTAL till Xft-skalan (16 px vid 96 dpi).
// Kärnans font-pekare tematiseras av libX11 via libXcursor medan den här är
// otematiserad — priset för att slippa ett nytt beroende.
Cursor X11Display::makePixmapCursor(int key)
{
    XColor svart{};
    svart.flags = static_cast<char>(DoRed | DoGreen | DoBlue);
    XColor vit = svart;
    vit.red = vit.green = vit.blue = 0xFFFF;

    if (key == kCursorHidden) {
        // En 1 × 1-pixmap med noll som källa OCH mask: ingen pixel syns.
        static const char tom[1] = {0};
        const Pixmap p = XCreateBitmapFromData(dpy_, root_, tom, 1, 1);
        const Cursor c = XCreatePixmapCursor(dpy_, p, p, &svart, &svart, 0, 0);
        XFreePixmap(dpy_, p);
        return c;
    }

    const int skala = std::max(1, static_cast<int>(std::lround(uiScale_)));
    int       hx = 0, hy = 0;
    const std::vector<std::uint8_t> rgba =
        expandCursor(kClosedHand, skala, hx, hy);
    const int sida = kClosedHand.size * skala;
    const int rad  = (sida + 7) / 8;
    std::vector<char> källa(static_cast<std::size_t>(rad) *
                                static_cast<std::size_t>(sida),
                            0);
    std::vector<char> mask(källa.size(), 0);
    for (int y = 0; y < sida; ++y)
        for (int x = 0; x < sida; ++x) {
            const std::size_t p = (static_cast<std::size_t>(y) *
                                       static_cast<std::size_t>(sida) +
                                   static_cast<std::size_t>(x)) * 4u;
            if (rgba[p + 3] == 0)
                continue;   // genomskinlig — varken källa eller mask
            const std::size_t b = static_cast<std::size_t>(y) *
                                      static_cast<std::size_t>(rad) +
                                  static_cast<std::size_t>(x / 8);
            const char bit = static_cast<char>(1u << (x % 8));
            mask[b] = static_cast<char>(mask[b] | bit);
            if (rgba[p] == 0)   // svart kontur = förgrunden
                källa[b] = static_cast<char>(källa[b] | bit);
        }
    const Pixmap ps = XCreateBitmapFromData(dpy_, root_, källa.data(),
                                            static_cast<unsigned>(sida),
                                            static_cast<unsigned>(sida));
    const Pixmap pm = XCreateBitmapFromData(dpy_, root_, mask.data(),
                                            static_cast<unsigned>(sida),
                                            static_cast<unsigned>(sida));
    const Cursor c = XCreatePixmapCursor(dpy_, ps, pm, &svart, &vit,
                                         static_cast<unsigned>(hx),
                                         static_cast<unsigned>(hy));
    XFreePixmap(dpy_, ps);
    XFreePixmap(dpy_, pm);
    return c;
}

namespace {

// Resize-zonens font-pekare, eller 0 när rollen INTE är en resize-zon
// (klientytan, captionen, fönsterknapparna) — bara resize-zonerna vinner
// över fönstrets begärda form (kontraktet vid `Window::setCursor`).
// `XC_X_cursor` är 0 i cursorfont.h och används aldrig här, så 0 är ledigt
// som "ingen zon".
unsigned resizeFontCursor(HitRole role)
{
    switch (role) {
    case HitRole::ResizeLeft:        return XC_left_side;
    case HitRole::ResizeRight:       return XC_right_side;
    case HitRole::ResizeTop:         return XC_top_side;
    case HitRole::ResizeBottom:      return XC_bottom_side;
    case HitRole::ResizeTopLeft:     return XC_top_left_corner;
    case HitRole::ResizeTopRight:    return XC_top_right_corner;
    case HitRole::ResizeBottomLeft:  return XC_bottom_left_corner;
    case HitRole::ResizeBottomRight: return XC_bottom_right_corner;
    default:                         return 0;
    }
}

// FÖNSTRETS BEGÄRDA FORM → X-kärnans font-pekare. Kärnfonten
// saknar en förbudspekare; `XC_circle` (en överstruken ring i de flesta
// teman) är den närmaste. Hårkorset och fyrpilen finns
// båda i kärnfonten, och klockan (`XC_watch`) är väntepekaren. Den stängda
// handen och den tomma pekaren finns inte där — de är displayens
// pseudonycklar, byggda som pixmap-pekare.
int shapeCursorKey(PointerShape s)
{
    switch (s) {
    case PointerShape::Arrow:      return XC_left_ptr;
    case PointerShape::IBeam:      return XC_xterm;
    case PointerShape::Hand:       return XC_hand2;
    case PointerShape::ResizeWE:   return XC_sb_h_double_arrow;
    case PointerShape::ResizeNS:   return XC_sb_v_double_arrow;
    case PointerShape::NotAllowed: return XC_circle;
    case PointerShape::Crosshair:  return XC_crosshair;
    case PointerShape::Move:       return XC_fleur;
    case PointerShape::Wait:       return XC_watch;
    case PointerShape::ClosedHand: return X11Display::kCursorClosedHand;
    case PointerShape::Hidden:     return X11Display::kCursorHidden;
    }
    return XC_left_ptr;
}

} // namespace

void X11Window::setCursor(PointerShape shape)
{
    cursor_ = shape;
    refreshCursor();
}

void X11Window::onCursorRole(HitRole role)
{
    if (role == cursorRole_)
        return;
    cursorRole_ = role;
    refreshCursor();
}

// Det EFFEKTIVA valet — resize-zonens pil över en sådan, annars den
// begärda formen i klientytan och pilen över captionen/knapparna — och
// bara ett serveranrop när det faktiskt ändrats.
void X11Window::refreshCursor()
{
    if (!win_)
        return;   // före init: formen är lagrad och sätts av första rörelsen
    int key = static_cast<int>(resizeFontCursor(cursorRole_));
    if (!key)
        key = shapeCursorKey(cursorRole_ == HitRole::Client
                                 ? cursor_
                                 : PointerShape::Arrow);
    if (key == appliedCursor_)
        return;
    appliedCursor_ = key;
    display_->defineCursor(win_, key);
}

// ---------------------------------------------------------------- klippbord

void X11Display::handleSelectionRequest(XSelectionRequestEvent& req)
{
    XSelectionEvent reply{};
    reply.type      = SelectionNotify;
    reply.display   = req.display;
    reply.requestor = req.requestor;
    reply.selection = req.selection;
    reply.target    = req.target;
    reply.time      = req.time;
    reply.property  = kX11None;   // default: vägra

    if (weOwnSelection_ && req.selection == aClipboard_ &&
        req.property != static_cast<Atom>(kX11None)) {
        if (req.target == aTargets_) {
            const Atom targets[] = {aTargets_, aUtf8_, XA_STRING};
            XChangeProperty(dpy_, req.requestor, req.property, XA_ATOM, 32,
                            PropModeReplace,
                            reinterpret_cast<const unsigned char*>(targets),
                            3);
            reply.property = req.property;
        } else if (req.target == aUtf8_ || req.target == XA_STRING) {
            XChangeProperty(
                dpy_, req.requestor, req.property, req.target, 8,
                PropModeReplace,
                reinterpret_cast<const unsigned char*>(
                    ownClipboardText_.data()),
                static_cast<int>(ownClipboardText_.size()));
            reply.property = req.property;
        }
    }
    XSendEvent(dpy_, req.requestor, kX11False, 0,
               reinterpret_cast<XEvent*>(&reply));
    XFlush(dpy_);
}

void X11Display::setClipboardText(const std::string& utf8)
{
    ownClipboardText_ = utf8;
    XSetSelectionOwner(dpy_, aClipboard_, helper_, CurrentTime);
    weOwnSelection_ =
        XGetSelectionOwner(dpy_, aClipboard_) == helper_;
    XFlush(dpy_);
}

std::string X11Display::clipboardText()
{
    if (weOwnSelection_)
        return ownClipboardText_;
    if (XGetSelectionOwner(dpy_, aClipboard_) ==
        static_cast<::Window>(kX11None))
        return {};

    XConvertSelection(dpy_, aClipboard_, aUtf8_, aGlzSelection_, helper_,
                      CurrentTime);
    XFlush(dpy_);

    // Vänta in SelectionNotify med poll-timeout — får aldrig blockera
    // pumpen. Övriga events lämnas i kön åt pumpen.
    const int fdX = ConnectionNumber(dpy_);
    for (int waitedMs = 0; waitedMs < 500;) {
        XEvent ev;
        if (XCheckTypedWindowEvent(dpy_, helper_, SelectionNotify, &ev)) {
            if (ev.xselection.property ==
                static_cast<Atom>(kX11None))
                return {};   // ägaren vägrade/kan inte ge UTF8
            Atom type = 0;
            int  fmt  = 0;
            unsigned long n = 0, after = 0;
            unsigned char* data = nullptr;
            if (XGetWindowProperty(dpy_, helper_, aGlzSelection_, 0,
                                   8 * 1024 * 1024, kX11True,
                                   AnyPropertyType, &type, &fmt, &n,
                                   &after, &data) != Success)
                return {};
            std::string out;
            if (data && type != aIncr_ && fmt == 8)
                out.assign(reinterpret_cast<const char*>(data),
                           static_cast<std::size_t>(n));
            else if (type == aIncr_)
                log("clipboardText: INCR-överföring stöds inte "
                    "(begränsning — stora data)");
            if (data)
                XFree(data);
            return out;
        }
        pollfd pf{fdX, POLLIN, 0};
        if (::poll(&pf, 1, 50) < 0)
            break;
        waitedMs += 50;
    }
    return {};
}

} // namespace gfx
