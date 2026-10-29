// WaylandDisplay — Wayland-sidan av LinuxDisplay: anslutning + registry-
// bindning av globals, seat-input (pointer/keyboard via xkbcommon inkl.
// EGEN key-repeat — protokollet repeterar inte åt klienten), styrplattan
// (axis_source/axis_stop = det precisa hjulet, zwp_pointer_gestures_v1 =
// nypning och vridning), cursortemat, klippbordet (wl_data_device) och
// pumpens prepare_read-läsdans.
// Event-översättningen speglar d3d11-WndProc:en: allt
// blir gfx::Event via pushEvent; koordinater konverteras till FYSISKA px
// (ytenheter × buffer-skala) redan här — glaze-lagret rörs inte.

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

#include "gfx/platform.h"
#include "wayland_backend.h"
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

// `mods` är tangentbordets modifierare NÄR eventet hände (Ctrl-/
// Shift-klick). Wayland skickar dem inte med pekaren; de är den senaste
// `wl_keyboard.modifiers` (`WaylandDisplay::mods_`), som anroparen skickar.
void pushMouse(WindowId wid, EventType t, float x, float y,
               MouseButton b, std::uint32_t mods)
{
    Event e;
    e.type     = t;
    e.windowId = wid;
    e.mouseX   = x;
    e.mouseY   = y;
    e.button   = b;
    e.mods     = mods;
    pushEvent(e);
}

// Per-offer-data för klippbordet: vilken text-mime erbjudandet bär.
struct OfferData {
    std::string mime;   // tom = ingen text
};

// Cursornamn per hit-roll, med klassiska X-namnen som fallback. FYRA
// kandidater: namnet ur cursor-specen/CSS, de klassiska X-namnen
// och sist ett namn ur libwayland-cursors INBYGGDA reservtema, som är det
// enda som finns när inget cursortema är installerat (t.ex. i WSLg: det
// har `left_side`/`bottom_side` men varken `ew-resize` eller
// `sb_h_double_arrow`).
struct CursorNames {
    const char* names[4];
};

// `PointerShape::Hidden` — en VAKTPOST och inget
// temanamn: `applyCursorNamed` känner igen pekaren och sätter en `NULL`-yta
// ("If surface is NULL, the pointer image is hidden", wl_pointer.set_cursor).
// En egen sträng och inte nullptr, av två skäl: nullptr betyder redan "ingen
// zon" i resize-tabellen nedan, och `refreshCursor`/`applyCursorNamed`
// deduplicerar på `names[0]` som en identitetsjämförelse.
constexpr char kHiddenName[] = "glaze:hidden";

// Resize-zonens pil — eller nullptr-namn när rollen INTE är en resize-zon
// (klientytan, captionen, fönsterknapparna). Bara resize-zonerna vinner
// över fönstrets begärda form (kontraktet vid `Window::setCursor`).
CursorNames cursorNamesFor(HitRole role)
{
    switch (role) {
    case HitRole::ResizeLeft:
    case HitRole::ResizeRight:
        return {{"ew-resize", "sb_h_double_arrow", nullptr, nullptr}};
    case HitRole::ResizeTop:
    case HitRole::ResizeBottom:
        return {{"ns-resize", "sb_v_double_arrow", nullptr, nullptr}};
    case HitRole::ResizeTopLeft:
    case HitRole::ResizeBottomRight:
        return {{"nwse-resize", "bottom_right_corner", nullptr, nullptr}};
    case HitRole::ResizeTopRight:
    case HitRole::ResizeBottomLeft:
        return {{"nesw-resize", "bottom_left_corner", nullptr, nullptr}};
    default:
        return {{nullptr, nullptr, nullptr, nullptr}};
    }
}

// FÖNSTRETS BEGÄRDA FORM → temanamnen. Först det namn
// cursor-specen och CSS använder, sedan de klassiska X-namnen som äldre
// teman bara har (temat har ofta bara det ena av dem). Skiktets ↔ provar
// `col-resize` efter `ew-resize` — samma pil i de flesta teman, men vissa
// har bara den ena — och sist reservtemats sidpil (se `CursorNames`).
// Förbudet har ingen reserv: utan tema står pekaren kvar som den var.
CursorNames cursorNamesFor(PointerShape shape)
{
    switch (shape) {
    case PointerShape::Arrow:
        return {{"default", "left_ptr", nullptr, nullptr}};
    case PointerShape::IBeam:
        return {{"text", "xterm", nullptr, nullptr}};
    case PointerShape::Hand:
        return {{"pointer", "hand2", "hand1", nullptr}};
    case PointerShape::ResizeWE:
        return {{"ew-resize", "col-resize", "sb_h_double_arrow", "right_side"}};
    case PointerShape::ResizeNS:
        return {{"ns-resize", "row-resize", "sb_v_double_arrow", "bottom_side"}};
    case PointerShape::NotAllowed:
        return {{"not-allowed", "crossed_circle", nullptr, nullptr}};
    // Hårkorset: CSS-namnet och det klassiska X-namnet.
    // Reservtemat har inget kors — utan tema står pekaren kvar, som förbudet.
    case PointerShape::Crosshair:
        return {{"crosshair", "cross", nullptr, nullptr}};
    // Flytta: CSS `move`, sedan `all-scroll` (samma fyrpil i de
    // flesta teman, men vissa har bara den ena) och X-namnet `fleur`.
    // Reservtemat har ingen fyrpil, men libwayland-cursor 1.23 bär CSS-alias
    // där och svarar på `all-scroll` (i WSLg, Debian 13: 16 × 16 som
    // `grabbing`, medan `move`, `fleur`, `crosshair` och `cross` saknas).
    // `grabbing` sist — den stängda handen, macOS form för att dra innehåll —
    // täcker en äldre version utan alias: bättre än att pekaren står kvar.
    case PointerShape::Move:
        return {{"move", "all-scroll", "fleur", "grabbing"}};
    // Den stängda handen: CSS `grabbing`, Qt:s och
    // äldre temans `closedhand`, och sist samma reserver som flytta — en
    // fyrpil är bättre än att pekaren står kvar.
    case PointerShape::ClosedHand:
        return {{"grabbing", "closedhand", "fleur", "all-scroll"}};
    // Vänta: CSS `wait`, X-namnet `watch`, och sist `progress`/
    // `left_ptr_watch` (pil med klocka) i teman som bara har den formen.
    case PointerShape::Wait:
        return {{"wait", "watch", "progress", "left_ptr_watch"}};
    // Ingen pekare: vaktposten, som `applyCursorNamed` gör till en NULL-yta.
    case PointerShape::Hidden:
        return {{kHiddenName, nullptr, nullptr, nullptr}};
    }
    return {{"default", "left_ptr", nullptr, nullptr}};
}

} // namespace

// ---------------------------------------------------------------- registry

void WaylandDisplay::onGlobal(void* data, wl_registry* reg,
                              std::uint32_t name, const char* iface,
                              std::uint32_t version)
{
    auto* d = static_cast<WaylandDisplay*>(data);
    auto bind = [&](const wl_interface* wi, std::uint32_t maxV) {
        return wl_registry_bind(reg, name, wi,
                                version < maxV ? version : maxV);
    };

    if (std::strcmp(iface, wl_compositor_interface.name) == 0) {
        d->compositor_ =
            static_cast<wl_compositor*>(bind(&wl_compositor_interface, 4));
    } else if (std::strcmp(iface, xdg_wm_base_interface.name) == 0) {
        d->wmBase_ =
            static_cast<xdg_wm_base*>(bind(&xdg_wm_base_interface, 3));
        static const xdg_wm_base_listener pingListener = {
            [](void*, xdg_wm_base* base, std::uint32_t serial) {
                xdg_wm_base_pong(base, serial);
            }};
        xdg_wm_base_add_listener(d->wmBase_, &pingListener, d);
    } else if (std::strcmp(iface, wl_seat_interface.name) == 0) {
        d->seat_ = static_cast<wl_seat*>(bind(&wl_seat_interface, 8));
        static const wl_seat_listener seatListener = {
            [](void* dd, wl_seat*, std::uint32_t caps) {
                static_cast<WaylandDisplay*>(dd)->seatCapabilities(caps);
            },
            [](void*, wl_seat*, const char*) {}};
        wl_seat_add_listener(d->seat_, &seatListener, d);
    } else if (std::strcmp(iface, wl_shm_interface.name) == 0) {
        d->shm_ = static_cast<wl_shm*>(bind(&wl_shm_interface, 1));
    } else if (std::strcmp(iface, wl_output_interface.name) == 0) {
        auto* out =
            static_cast<wl_output*>(bind(&wl_output_interface, 2));
        d->outputs_.push_back({out, 1, name});   // name = remove-nyckeln
        // SKÄRMENS mm: geometry bär millimetrarna och
        // märket, mode den aktuella upplösningen, och `done` avslutar en
        // omgång — först då mäter fönstren på outputen om.
        static const wl_output_listener outputListener = {
            [](void* dd, wl_output* o, std::int32_t, std::int32_t,
               std::int32_t mmW, std::int32_t mmH, std::int32_t,
               const char* make, const char* model, std::int32_t) {
                for (auto& oi : static_cast<WaylandDisplay*>(dd)->outputs_)
                    if (oi.output == o) {
                        oi.metrics.mmW   = mmW;
                        oi.metrics.mmH   = mmH;
                        oi.metrics.make  = make ? make : "";
                        oi.metrics.model = model ? model : "";
                    }
            },   // geometry
            [](void* dd, wl_output* o, std::uint32_t flags, std::int32_t w,
               std::int32_t h, std::int32_t) {
                if (!(flags & WL_OUTPUT_MODE_CURRENT))
                    return;   // bara läget skärmen faktiskt visar
                for (auto& oi : static_cast<WaylandDisplay*>(dd)->outputs_)
                    if (oi.output == o) {
                        oi.metrics.pxW = w;
                        oi.metrics.pxH = h;
                    }
            },   // mode
            [](void* dd, wl_output*) {
                for (auto& [surface, win] :
                     static_cast<WaylandDisplay*>(dd)->surfaces_)
                    if (win)
                        win->measureDisplay(true);
            },   // done
            [](void* dd, wl_output* o, std::int32_t factor) {   // scale
                auto* disp = static_cast<WaylandDisplay*>(dd);
                for (auto& oi : disp->outputs_)
                    if (oi.output == o)
                        oi.scale = factor > 0 ? factor : 1;
                // Per-surface-spårningen: fönstren på outputen räknar om
                // sin skala (heltalsfallbacken — fractional vinner).
                for (auto& [surface, win] : disp->surfaces_)
                    if (win)
                        win->onOutputScaleMaybeChanged();
            },
            [](void*, wl_output*, const char*) {},   // name
            [](void*, wl_output*, const char*) {},   // description
        };
        wl_output_add_listener(out, &outputListener, d);
    } else if (std::strcmp(iface, wl_data_device_manager_interface.name) ==
               0) {
        d->dataDeviceManager_ = static_cast<wl_data_device_manager*>(
            bind(&wl_data_device_manager_interface, 3));
    } else if (std::strcmp(iface,
                           zxdg_decoration_manager_v1_interface.name) ==
               0) {
        d->decorationManager_ = static_cast<zxdg_decoration_manager_v1*>(
            bind(&zxdg_decoration_manager_v1_interface, 1));
    } else if (std::strcmp(
                   iface, wp_fractional_scale_manager_v1_interface.name) ==
               0) {
        d->fractionalManager_ =
            static_cast<wp_fractional_scale_manager_v1*>(
                bind(&wp_fractional_scale_manager_v1_interface, 1));
    } else if (std::strcmp(iface, wp_viewporter_interface.name) == 0) {
        d->viewporter_ =
            static_cast<wp_viewporter*>(bind(&wp_viewporter_interface, 1));
    } else if (std::strcmp(iface, xdg_activation_v1_interface.name) == 0) {
        d->activation_ = static_cast<xdg_activation_v1*>(
            bind(&xdg_activation_v1_interface, 1));
    } else if (std::strcmp(iface, zwp_pointer_gestures_v1_interface.name) ==
               0) {
        // STYRPLATTANS NYPNING. v2 för `release` — hållgesten i v3
        // används inte. Pinch-objektet kräver pekaren också, och sätets
        // capabilities kan komma före eller efter den här globalen.
        d->gestures_ = static_cast<zwp_pointer_gestures_v1*>(
            bind(&zwp_pointer_gestures_v1_interface, 2));
        d->ensurePinch();
    } else if (std::strcmp(iface,
                           xdg_toplevel_icon_manager_v1_interface.name) == 0) {
        // PROGRAMIKONEN: fönstrens egna ikoner (KWin, Labwc …).
        // Ingen lyssnare — `icon_size` är kompositörens önskade storlekar för
        // en SKALBAR ikon, och vi skickar redan varje ram vi har.
        d->iconManager_ = static_cast<xdg_toplevel_icon_manager_v1*>(
            bind(&xdg_toplevel_icon_manager_v1_interface, 1));
    }
}

void WaylandDisplay::onGlobalRemove(void* data, wl_registry*,
                                    std::uint32_t name)
{
    // En output kopplades ur: släpp den ur fönstrens per-surface-set
    // FÖRE destroy (annars dinglar pekarna) och låt dem räkna om skalan.
    auto* d = static_cast<WaylandDisplay*>(data);
    for (auto it = d->outputs_.begin(); it != d->outputs_.end(); ++it) {
        if (it->name != name)
            continue;
        wl_output* out = it->output;
        d->outputs_.erase(it);
        for (auto& [surface, win] : d->surfaces_)
            if (win)
                win->outputRemoved(out);
        wl_output_destroy(out);
        return;
    }
    // Andra globals (seat/decoration/…) försvinner inte i praktiken —
    // och hanteras inte här (medveten begränsning).
}

// ---------------------------------------------------------------- create

WaylandDisplay* WaylandDisplay::create()
{
    auto* d = new WaylandDisplay();
    if (!d->connect()) {
        delete d;
        return nullptr;
    }
    return d;
}

bool WaylandDisplay::connect()
{
    display_ = wl_display_connect(nullptr);
    if (!display_)
        return false;

    // Egen kö för frame-callbacks (se frameQueue() i wayland_backend.h) —
    // skapas före allt annat så varje fönster kan lägga sin callback där.
    // Misslyckas den är det ett initieringsfel: beforeSwap använder kön
    // ovillkorligt, och en null-kö vore odefinierat beteende i libwayland.
    frameQueue_ = wl_display_create_queue(display_);
    if (!frameQueue_) {
        log("wayland: kunde inte skapa frame-eventkön — faller vidare");
        return false;
    }

    registry_ = wl_display_get_registry(display_);
    static const wl_registry_listener regListener = {&onGlobal,
                                                     &onGlobalRemove};
    wl_registry_add_listener(registry_, &regListener, this);
    wl_display_roundtrip(display_);   // globals
    wl_display_roundtrip(display_);   // outputs/seat-detaljer

    if (!compositor_ || !wmBase_ || !seat_) {
        log("wayland: obligatoriska globals saknas (compositor/wm_base/"
            "seat) — faller vidare");
        return false;
    }
    if (!gestures_)
        log("wayland: zwp_pointer_gestures_v1 saknas — styrplattans nypning "
            "och vridning når inte appen");

    // EGL mot Wayland-displayen (EGL 1.5-core-vägen).
    auto getPlatformDisplay =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (getPlatformDisplay)
        egl_ = getPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, display_,
                                  nullptr);
    if (egl_ == EGL_NO_DISPLAY)
        egl_ = eglGetDisplay(
            reinterpret_cast<EGLNativeDisplayType>(display_));
    if (egl_ == EGL_NO_DISPLAY ||
        eglInitialize(egl_, nullptr, nullptr) != EGL_TRUE) {
        log("wayland: EGL kunde inte initieras");
        return false;
    }
    const EGLint cfgAttribs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                 EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                                 EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                 EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
                                 EGL_NONE};
    EGLint n = 0;
    if (eglChooseConfig(egl_, cfgAttribs, &eglConfig_, 1, &n) != EGL_TRUE ||
        n < 1) {
        log("wayland: ingen ARGB-EGL-konfig");
        return false;
    }

    xkb_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

    if (dataDeviceManager_ && seat_) {
        dataDevice_ = wl_data_device_manager_get_data_device(
            dataDeviceManager_, seat_);
        static const wl_data_device_listener ddListener = {
            // data_offer: nytt erbjudande — samla text-mimen per offer.
            [](void*, wl_data_device*, wl_data_offer* offer) {
                auto* od = new OfferData();
                static const wl_data_offer_listener offerListener = {
                    [](void* data, wl_data_offer*, const char* mime) {
                        auto* o = static_cast<OfferData*>(data);
                        if (std::strcmp(mime,
                                        "text/plain;charset=utf-8") == 0)
                            o->mime = mime;   // föredragen
                        else if (o->mime.empty() &&
                                 (std::strcmp(mime, "UTF8_STRING") == 0 ||
                                  std::strcmp(mime, "text/plain") == 0))
                            o->mime = mime;
                    },
                    [](void*, wl_data_offer*, std::uint32_t) {},
                    [](void*, wl_data_offer*, std::uint32_t) {}};
                wl_data_offer_add_listener(offer, &offerListener, od);
                wl_data_offer_set_user_data(offer, od);
            },
            // enter (dnd): vi gör ingen drag-and-drop — förstör offret
            // direkt (wl_data_offer_destroy är protokollrätt för
            // oanvända offers), annars läcker proxy + OfferData för
            // varje fil som dras över fönstret.
            [](void*, wl_data_device*, std::uint32_t, wl_surface*,
               wl_fixed_t, wl_fixed_t, wl_data_offer* offer) {
                if (!offer)
                    return;
                if (auto* od = static_cast<OfferData*>(
                        wl_data_offer_get_user_data(offer)))
                    delete od;
                wl_data_offer_destroy(offer);
            },
            [](void*, wl_data_device*) {},                   // leave
            [](void*, wl_data_device*, std::uint32_t, wl_fixed_t,
               wl_fixed_t) {},                               // motion
            [](void*, wl_data_device*) {},                   // drop
            [](void* dd, wl_data_device*, wl_data_offer* offer) {
                static_cast<WaylandDisplay*>(dd)->selectionOffered(offer);
            }};
        wl_data_device_add_listener(dataDevice_, &ddListener, this);
    }

    // Key-repeat-timern (disarmad tills första repeterbara trycket).
    repeatFd_ = ::timerfd_create(CLOCK_MONOTONIC,
                                 TFD_NONBLOCK | TFD_CLOEXEC);
    if (repeatFd_ >= 0) {
        repeatSource_.fd      = repeatFd_;
        repeatSource_.onReady = [this] {
            // AVSIKTLIGT: utgångna timerperioder koalesceras till ETT
            // repeat-event (read-loopen dränerar räknaren). En pump-
            // fördröjd burst av N repeats vore sämre UX än ett tappat
            // tick — ett avsiktligt val, "fixas" inte.
            std::uint64_t nExp = 0;
            while (::read(repeatFd_, &nExp, sizeof nExp) > 0) {
            }
            if (keyboardWin_ && repeatKeycode_ != 0)
                emitKey(repeatKeycode_, true, true);
        };
        addPollSource(&repeatSource_);
    }
    return true;
}

WaylandDisplay::~WaylandDisplay()
{
    if (repeatFd_ >= 0) {
        removePollSource(&repeatSource_);
        ::close(repeatFd_);
    }
    // Activation-requests kompositören aldrig besvarade (token + request
    // läcker annars — fynd #5).
    for (ActivationRequest* req : activationRequests_) {
        xdg_activation_token_v1_destroy(req->token);
        delete req;
    }
    activationRequests_.clear();
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
    if (cursorSurface_)
        wl_surface_destroy(cursorSurface_);
    if (cursorTheme_)
        wl_cursor_theme_destroy(cursorTheme_);
    // Klippbordet: sist mottagna selection-offret + dess OfferData.
    if (selectionOffer_) {
        if (auto* od = static_cast<OfferData*>(
                wl_data_offer_get_user_data(selectionOffer_)))
            delete od;
        wl_data_offer_destroy(selectionOffer_);
    }
    if (ownSource_)
        wl_data_source_destroy(ownSource_);
    if (dataDevice_)
        wl_data_device_destroy(dataDevice_);
    // Seat-enheterna: release är protokollets destruktor-request (finns
    // sedan v3/v5) — annars ren proxy-destroy. Nypningen hänger på pekaren
    // och rivs före den.
    if (pinch_)
        zwp_pointer_gesture_pinch_v1_destroy(pinch_);
    if (pointer_) {
        if (wl_pointer_get_version(pointer_) >=
            WL_POINTER_RELEASE_SINCE_VERSION)
            wl_pointer_release(pointer_);
        else
            wl_pointer_destroy(pointer_);
    }
    if (keyboard_) {
        if (wl_keyboard_get_version(keyboard_) >=
            WL_KEYBOARD_RELEASE_SINCE_VERSION)
            wl_keyboard_release(keyboard_);
        else
            wl_keyboard_destroy(keyboard_);
    }
    // Bundna globals i omvänd bindordning (onGlobal), registry sist.
    if (iconManager_)
        xdg_toplevel_icon_manager_v1_destroy(iconManager_);
    if (gestures_) {
        if (zwp_pointer_gestures_v1_get_version(gestures_) >=
            ZWP_POINTER_GESTURES_V1_RELEASE_SINCE_VERSION)
            zwp_pointer_gestures_v1_release(gestures_);
        else
            zwp_pointer_gestures_v1_destroy(gestures_);
    }
    if (activation_)
        xdg_activation_v1_destroy(activation_);
    if (viewporter_)
        wp_viewporter_destroy(viewporter_);
    if (fractionalManager_)
        wp_fractional_scale_manager_v1_destroy(fractionalManager_);
    if (decorationManager_)
        zxdg_decoration_manager_v1_destroy(decorationManager_);
    if (dataDeviceManager_)
        wl_data_device_manager_destroy(dataDeviceManager_);
    for (auto& oi : outputs_)
        wl_output_destroy(oi.output);
    outputs_.clear();
    if (shm_)
        wl_shm_destroy(shm_);
    if (seat_) {
        if (wl_seat_get_version(seat_) >= WL_SEAT_RELEASE_SINCE_VERSION)
            wl_seat_release(seat_);
        else
            wl_seat_destroy(seat_);
    }
    if (wmBase_)
        xdg_wm_base_destroy(wmBase_);
    if (compositor_)
        wl_compositor_destroy(compositor_);
    if (registry_)
        wl_registry_destroy(registry_);
    // Frame-kön EFTER alla proxies som kan ha lagts i den (fönstren är
    // rivna här) men FÖRE wl_display_disconnect.
    if (frameQueue_)
        wl_event_queue_destroy(frameQueue_);
    if (egl_ != EGL_NO_DISPLAY)
        eglTerminate(egl_);
    if (display_) {
        wl_display_flush(display_);   // destruktor-requests ut före stängning
        wl_display_disconnect(display_);
    }
}

std::unique_ptr<Window> WaylandDisplay::makeWindow()
{
    return std::make_unique<WaylandWindow>(this);
}

int WaylandDisplay::maxOutputScale() const
{
    int s = 1;
    for (const auto& oi : outputs_)
        if (oi.scale > s)
            s = oi.scale;
    return s;
}

const WaylandDisplay::OutputMetrics*
WaylandDisplay::outputMetrics(const wl_output* o) const
{
    for (const auto& oi : outputs_)
        if (oi.output == o)
            return &oi.metrics;
    return nullptr;
}

int WaylandDisplay::outputScaleOf(const wl_output* o) const
{
    for (const auto& oi : outputs_)
        if (oi.output == o)
            return oi.scale;
    return 0;   // okänd/borttagen — räknas inte
}

// ---------------------------------------------------------------- pumpen

void WaylandDisplay::prepoll()
{
    // Den korrekta läsdansen: dispatcha allt köat innan läslåset tas,
    // flusha utgående requests före sömnen.
    while (wl_display_prepare_read(display_) != 0)
        wl_display_dispatch_pending(display_);
    // KRITISKT före sömnen: en uppskjuten fokusförlust som dispatchats
    // här (eller i en roundtrip) måste ut i kön INNAN epoll_wait(-1) —
    // annars sover pumpen på obestämd tid med popupen kvar öppen efter
    // att användaren växlat app. Likadant ett avbrutet tryck — en
    // flytande dockningspanel vars drag tappade pekaren väntar annars.
    flushPendingLeave();
    flushPendingCaptureLost();
    prepared_ = true;
    wl_display_flush(display_);
}

void WaylandDisplay::postpoll(bool fdReady)
{
    if (prepared_) {
        if (fdReady)
            wl_display_read_events(display_);
        else
            wl_display_cancel_read(display_);
        prepared_ = false;
    }
    if (wl_display_dispatch_pending(display_) < 0)
        log("wayland: display-fel — anslutningen bruten");
    // Kom ingen matchande enter i batchen var det en RIKTIG fokusförlust
    // (användaren växlade fönster/app) — då ska den ut. Samma regel för
    // pekarens leave under ett tryck: ingen enter till en egen popup i
    // omgången, alltså ett avbrott.
    flushPendingLeave();
    flushPendingCaptureLost();
}

// Fokus-kontraktet (fasadregel, se gfx/window.h): en popup och dess ägare
// är EN fokusgrupp utåt. Wayland är den enda backenden där OS:et faktiskt
// flyttar tangentbordsfokus till popupen — `xdg_popup.grab` ger den
// fokus per xdg-shell-specen. Windows (`MA_NOACTIVATE`), macOS
// (nonactivating panel) och X11 (override-redirect) gör det aldrig, så
// utan den här maskningen skulle ETT WindowFocusLost till ägaren nå
// App::closePopups och stänga popupen i samma andetag som den öppnades
// (dropdown-listan skulle aldrig synas).
bool WaylandDisplay::sameFocusGroup(WaylandWindow* a, WaylandWindow* b)
{
    if (!a || !b)
        return false;
    // Jämför gruppernas ROT (toplevel-ankaret), inte bara om den ena
    // ligger i den andras förälderkedja — annars räknas två SYSKON-popups
    // under samma ägare som olika grupper, och fokusbyte mellan dem
    // läcker ut ett FocusLost som stänger hela popup-kedjan.
    while (a->popupParent_)
        a = a->popupParent_;
    while (b->popupParent_)
        b = b->popupParent_;
    return a == b;
}

void WaylandDisplay::flushPendingLeave()
{
    if (!hasPendingLeave_)
        return;
    if (pendingLeave_)
        pushSimple(pendingLeave_->id(), EventType::WindowFocusLost);
    hasPendingLeave_ = false;
    pendingLeave_    = nullptr;
}

// PEKARENS AVBROTT: fönstret är registrerat så länge
// posten står — `windowDestroyed` nollar den med fönstret.
void WaylandDisplay::flushPendingCaptureLost()
{
    if (!pendingCaptureLost_)
        return;
    pushSimple(pendingCaptureLost_->id(), EventType::MouseCaptureLost);
    pendingCaptureLost_ = nullptr;
}

void WaylandDisplay::abortPoll()
{
    if (prepared_) {
        wl_display_cancel_read(display_);
        prepared_ = false;
    }
}

// ---------------------------------------------------------------- ytregister

void WaylandDisplay::registerSurface(wl_surface* s, WaylandWindow* w)
{
    surfaces_[s] = w;
}

void WaylandDisplay::unregisterSurface(wl_surface* s)
{
    surfaces_.erase(s);
}

void WaylandDisplay::trackActivation(ActivationRequest* req)
{
    activationRequests_.push_back(req);
}

bool WaylandDisplay::untrackActivation(ActivationRequest* req)
{
    for (auto it = activationRequests_.begin();
         it != activationRequests_.end(); ++it)
        if (*it == req) {
            activationRequests_.erase(it);
            return true;
        }
    return false;
}

void WaylandDisplay::windowDestroyed(WaylandWindow* w)
{
    // Utestående activation-requests mot fönstrets yta: förstör token +
    // request nu — kommer done efter det här är proxyn borta och
    // callbacken fyras aldrig (aktivering av förstörd yta undviks).
    for (auto it = activationRequests_.begin();
         it != activationRequests_.end();) {
        ActivationRequest* req = *it;
        if (req->target == w->surface_) {
            xdg_activation_token_v1_destroy(req->token);
            delete req;
            it = activationRequests_.erase(it);
        } else {
            ++it;
        }
    }
    if (pointerWin_ == w)
        pointerWin_ = nullptr;
    // Styrplattans sekvenser dör tyst med sitt fönster: ett End till ett
    // fönster som rivs har ingen mottagare.
    if (preciseWin_ == w)
        preciseWin_ = nullptr;
    if (pinchWin_ == w)
        pinchWin_ = nullptr;
    if (grabWin_ == w) {   // grabben kan inte överleva sitt fönster
        grabWin_    = nullptr;
        grabSerial_ = 0;
    }
    if (keyboardWin_ == w) {
        keyboardWin_ = nullptr;
        disarmRepeat();
    }
    if (pendingLeave_ == w) {   // fokusförlusten dog med sitt fönster
        pendingLeave_    = nullptr;
        hasPendingLeave_ = false;
    }
    if (pendingCaptureLost_ == w)   // avbrottet likaså
        pendingCaptureLost_ = nullptr;
    for (auto& [surface, win] : surfaces_)   // ingen dinglande fokusgrupp
        if (win && win->popupParent_ == w)
            win->popupParent_ = nullptr;
}

WaylandWindow* WaylandDisplay::windowFor(wl_surface* s) const
{
    const auto it = surfaces_.find(s);
    return it == surfaces_.end() ? nullptr : it->second;
}

// ---------------------------------------------------------------- seat

void WaylandDisplay::seatCapabilities(std::uint32_t caps)
{
    const bool hasPointer  = caps & WL_SEAT_CAPABILITY_POINTER;
    const bool hasKeyboard = caps & WL_SEAT_CAPABILITY_KEYBOARD;

    if (hasPointer && !pointer_) {
        pointer_ = wl_seat_get_pointer(seat_);
        static const wl_pointer_listener pointerListener = {
            // enter
            [](void* dd, wl_pointer*, std::uint32_t serial,
               wl_surface* surface, wl_fixed_t sx, wl_fixed_t sy) {
                auto* d = static_cast<WaylandDisplay*>(dd);
                d->pointerWin_         = d->windowFor(surface);
                // FOKUSGRUPPSUNDANTAGET: pekaren gick från fönstret där
                // trycket hölls till en egen popup — trycket öppnade den, och
                // släppet kommer till popupen. Inget avbrott; går den någon
                // annanstans skickas avbrottet nu, före inträdets rörelse.
                if (d->pendingCaptureLost_ &&
                    sameFocusGroup(d->pendingCaptureLost_, d->pointerWin_))
                    d->pendingCaptureLost_ = nullptr;
                d->flushPendingCaptureLost();
                d->pointerEnterSerial_ = serial;
                d->pointerX_           = wl_fixed_to_double(sx);
                d->pointerY_           = wl_fixed_to_double(sy);
                // En NY enter-serial: pekaren är odefinierad tills vi sätter
                // den, så minnet av den förra nollas och den sätts om —
                // fönstrets begärda form eller zonens pil.
                // Positionerna i KLIENTENS px: ytans lokala koordinater
                // minus storleksmarginalen (`clientX/Y`).
                d->lastCursorRole_ =
                    d->pointerWin_
                        ? d->pointerWin_->roleAt(
                              d->pointerWin_->clientX(d->pointerX_),
                              d->pointerWin_->clientY(d->pointerY_))
                        : HitRole::Client;
                d->appliedCursor_ = nullptr;
                d->refreshCursor();
                if (d->pointerWin_)
                    pushMouse(d->pointerWin_->id(), EventType::MouseMotion,
                              d->pointerWin_->clientX(d->pointerX_),
                              d->pointerWin_->clientY(d->pointerY_),
                              MouseButton::None, d->mods_);
            },
            // leave
            [](void* dd, wl_pointer*, std::uint32_t, wl_surface*) {
                auto* d = static_cast<WaylandDisplay*>(dd);
                // Ett tvåfingersdrag som pågick slutar här för fönstret —
                // dess axis_stop går till nästa yta, eller ingenstans.
                d->endPreciseScroll();
                if (d->pointerWin_)
                    pushSimple(d->pointerWin_->id(), EventType::MouseLeave);
                // ETT LEVERERAT TRYCK HÖLLS: släppet kommer inte hit.
                // Avbrottet väntar på resten av omgången — en enter till en
                // egen popup i samma fokusgrupp upphäver det.
                if (d->pushedButtons_ && d->pointerWin_) {
                    d->flushPendingCaptureLost();
                    d->pendingCaptureLost_ = d->pointerWin_;
                }
                d->pushedButtons_ = 0;
                d->pointerWin_    = nullptr;
                d->grabSerial_    = 0;   // pekaren borta ⇒ ingen levande grab
                d->grabWin_       = nullptr;
            },
            // motion
            [](void* dd, wl_pointer*, std::uint32_t, wl_fixed_t sx,
               wl_fixed_t sy) {
                auto* d = static_cast<WaylandDisplay*>(dd);
                d->pointerX_ = wl_fixed_to_double(sx);
                d->pointerY_ = wl_fixed_to_double(sy);
                WaylandWindow* w = d->pointerWin_;
                if (!w)
                    return;
                const float px = w->clientX(d->pointerX_);
                const float py = w->clientY(d->pointerY_);
                pushMouse(w->id(), EventType::MouseMotion, px, py,
                          MouseButton::None, d->mods_);
                // Cursorbyte över resize-kanterna (klientens jobb på
                // Wayland). `refreshCursor` sätter bara när det effektiva
                // valet ändrats — fönstrets egen form följer med
                // `setCursor`, som appen anropar när rörelsen behandlats.
                const HitRole role = w->roleAt(px, py);
                if (role != d->lastCursorRole_) {
                    d->lastCursorRole_ = role;
                    d->refreshCursor();
                }
            },
            // button
            [](void* dd, wl_pointer*, std::uint32_t serial, std::uint32_t,
               std::uint32_t button, std::uint32_t state) {
                auto* d = static_cast<WaylandDisplay*>(dd);
                const bool pressed =
                    state == WL_POINTER_BUTTON_STATE_PRESSED;
                if (pressed) {
                    d->lastButtonSerial_ = serial;
                    d->lastInputSerial_  = serial;
                    // Trycket öppnar den implicita grabben — noteras med
                    // sitt fönster så bara popups under DET fönstret får
                    // grabba, och bara innan knappen släpps.
                    d->grabSerial_ = serial;
                    d->grabWin_    = d->pointerWin_;
                } else {
                    d->grabSerial_ = 0;   // släpp → grabben är slut
                    d->grabWin_    = nullptr;
                }
                MouseButton mb = MouseButton::None;
                if (button == BTN_LEFT)        mb = MouseButton::Left;
                else if (button == BTN_MIDDLE) mb = MouseButton::Middle;
                else if (button == BTN_RIGHT)  mb = MouseButton::Right;
                // Knappens bit i de LEVERERADE trycken — släppet tar
                // bort den också utan fönster under pekaren.
                const std::uint8_t bit =
                    mb == MouseButton::None
                        ? 0
                        : static_cast<std::uint8_t>(
                              1u << static_cast<unsigned>(mb));
                if (!pressed)
                    d->pushedButtons_ =
                        static_cast<std::uint8_t>(d->pushedButtons_ & ~bit);
                WaylandWindow* w = d->pointerWin_;
                if (!w)
                    return;
                const float px = w->clientX(d->pointerX_);
                const float py = w->clientY(d->pointerY_);
                // Tryck i Caption/Resize-zonerna — och i storleksmarginalen
                // (E2L) — startar kompositörens interaktiva move/resize;
                // eventet konsumeras där, och trycket är inte vårt.
                if (pressed && mb == MouseButton::Left &&
                    w->interceptPress(px, py, serial))
                    return;
                pushMouse(w->id(),
                          pressed ? EventType::MouseButtonDown
                                  : EventType::MouseButtonUp,
                          px, py, mb, d->mods_);
                if (pressed)
                    d->pushedButtons_ |= bit;
            },
            // axis
            [](void* dd, wl_pointer*, std::uint32_t, std::uint32_t axis,
               wl_fixed_t value) {
                auto* d = static_cast<WaylandDisplay*>(dd);
                const double v = wl_fixed_to_double(value);
                if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
                    d->axisV_ += v;
                else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
                    d->axisH_ += v;
            },
            // frame
            [](void* dd, wl_pointer*) {
                static_cast<WaylandDisplay*>(dd)->pointerFrameFlush();
            },
            // axis_source (v5) — gäller hela ramen
            [](void* dd, wl_pointer*, std::uint32_t source) {
                static_cast<WaylandDisplay*>(dd)->axisSource_ =
                    static_cast<int>(source);
            },
            // axis_stop (v5) — per axel; ramen avslutar EN sekvens
            [](void* dd, wl_pointer*, std::uint32_t, std::uint32_t) {
                static_cast<WaylandDisplay*>(dd)->axisStop_ = true;
            },
            // axis_discrete (v5–7)
            [](void* dd, wl_pointer*, std::uint32_t axis,
               std::int32_t discrete) {
                auto* d = static_cast<WaylandDisplay*>(dd);
                if (d->sawV120_)
                    return;   // value120 är finare — vinner
                if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
                    d->v120V_ += discrete * 120.0;
                else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
                    d->v120H_ += discrete * 120.0;
            },
            // axis_value120 (v8)
            [](void* dd, wl_pointer*, std::uint32_t axis,
               std::int32_t v120) {
                auto* d = static_cast<WaylandDisplay*>(dd);
                if (!d->sawV120_) {
                    d->sawV120_ = true;
                    d->v120V_ = d->v120H_ = 0.0;
                }
                if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
                    d->v120V_ += v120;
                else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
                    d->v120H_ += v120;
            },
            // axis_relative_direction (v9 — binds aldrig, stub för structen)
            [](void*, wl_pointer*, std::uint32_t, std::uint32_t) {},
        };
        wl_pointer_add_listener(pointer_, &pointerListener, this);
        ensurePinch();
    } else if (!hasPointer && pointer_) {
        // Sätet tappade pekaren: sekvenser som pågick avslutas (deras End
        // kan inte komma längre), och nypningen rivs före pekaren den
        // skapades ur. Ett levererat tryck får sitt avbrott — släppet
        // kommer inte heller.
        endPreciseScroll();
        pinchEnd(true);
        if (pushedButtons_ && pointerWin_) {
            flushPendingCaptureLost();
            pendingCaptureLost_ = pointerWin_;
        }
        pushedButtons_ = 0;
        if (pinch_) {
            zwp_pointer_gesture_pinch_v1_destroy(pinch_);
            pinch_ = nullptr;
        }
        wl_pointer_destroy(pointer_);
        pointer_    = nullptr;
        pointerWin_ = nullptr;
    }

    if (hasKeyboard && !keyboard_) {
        keyboard_ = wl_seat_get_keyboard(seat_);
        static const wl_keyboard_listener keyboardListener = {
            // keymap
            [](void* dd, wl_keyboard*, std::uint32_t format, int fd,
               std::uint32_t size) {
                auto* d = static_cast<WaylandDisplay*>(dd);
                if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
                    d->keymapChanged(fd, size);
                else
                    ::close(fd);
            },
            // enter — tangentbordsfokus = fönsterfokus
            [](void* dd, wl_keyboard*, std::uint32_t serial,
               wl_surface* surface, wl_array*) {
                auto* d = static_cast<WaylandDisplay*>(dd);
                d->lastInputSerial_ = serial;
                d->keyboardWin_     = d->windowFor(surface);
                // Fokus-kontraktet: hoppar fokus bara mellan ett fönster
                // och dess EGEN popup är det ingen fokusändring utåt —
                // släpp både den innehållna leave:n och den här enter:n.
                if (d->hasPendingLeave_ &&
                    sameFocusGroup(d->pendingLeave_, d->keyboardWin_)) {
                    d->hasPendingLeave_ = false;
                    d->pendingLeave_    = nullptr;
                    return;
                }
                d->flushPendingLeave();
                if (d->keyboardWin_)
                    pushSimple(d->keyboardWin_->id(),
                               EventType::WindowFocusGained);
            },
            // leave
            [](void* dd, wl_keyboard*, std::uint32_t, wl_surface*) {
                auto* d = static_cast<WaylandDisplay*>(dd);
                // Håll inne fokusförlusten tills nästa enter är känd (se
                // fokus-kontraktet ovan). Kommer ingen enter flushas den
                // som riktig WindowFocusLost i postpoll.
                d->flushPendingLeave();
                d->pendingLeave_    = d->keyboardWin_;
                d->hasPendingLeave_ = true;
                d->keyboardWin_     = nullptr;
                d->disarmRepeat();
                // Protokollet nollställer tangentbordets logiska läge vid
                // leave, modifierarna inräknade — och nästa enter följs av
                // ett `modifiers`. Utan raden bar pekaren över ett fönster
                // UTAN fokus det Ctrl som hölls när fokus gick (Ctrl-klick i
                // ett annat fönster): ett tvåfingersdrag kretsade då i
                // stället för att panorera (kontraktet vid `kModCtrl`).
                d->mods_ = 0;
            },
            // key
            [](void* dd, wl_keyboard*, std::uint32_t serial, std::uint32_t,
               std::uint32_t key, std::uint32_t state) {
                static_cast<WaylandDisplay*>(dd)->keyEvent(
                    serial, key,
                    state == WL_KEYBOARD_KEY_STATE_PRESSED);
            },
            // modifiers
            [](void* dd, wl_keyboard*, std::uint32_t,
               std::uint32_t depressed, std::uint32_t latched,
               std::uint32_t locked, std::uint32_t group) {
                static_cast<WaylandDisplay*>(dd)->modifiersChanged(
                    depressed, latched, locked, group);
            },
            // repeat_info — repeteringen är VÅR (timerfd)
            [](void* dd, wl_keyboard*, std::int32_t rate,
               std::int32_t delay) {
                auto* d = static_cast<WaylandDisplay*>(dd);
                d->repeatRateHz_  = rate;
                d->repeatDelayMs_ = delay;
            },
        };
        wl_keyboard_add_listener(keyboard_, &keyboardListener, this);
    } else if (!hasKeyboard && keyboard_) {
        wl_keyboard_destroy(keyboard_);
        keyboard_    = nullptr;
        keyboardWin_ = nullptr;
        disarmRepeat();
    }
}

void WaylandDisplay::pointerFrameFlush()
{
    // STYRPLATTAN: `axis_source` gäller hela ramen. Finger och
    // continuous är det PRECISA hjulet (kontraktet vid `kPrecisePxPerRow` i
    // gfx/events.h): axelvärdet är ytans enheter, som blir fönstrets LOGISKA
    // px genom fysiska px ÷ fulla skalan (textskalningen och användarskalan
    // ingår — ytenheten är inte en logisk px när de inte är 1), och raderna
    // är de px ÷ kPrecisePxPerRow. Då flyttar ritytan innehållet exakt så
    // långt som pekaren hade flyttat sig. Tecknen som hjulets nedan. Fasen:
    // Begin för första ramen efter ett stopp, sedan Update, och `axis_stop`
    // avslutar med ett End utan rader — kompositören har ingen egen tröghet,
    // så Momentum kommer aldrig härifrån. Okänd källa (ingen axis_source)
    // och wheel/wheel_tilt går hjulets väg. Varje hjulevent bär
    // `mods_` (senaste `wl_keyboard.modifiers`) som musens knappar — Ctrl/Cmd
    // + draget kretsar på ritytan.
    const int  source = axisSource_;
    const bool stop   = axisStop_;
    axisSource_ = -1;
    axisStop_   = false;
    if (source == WL_POINTER_AXIS_SOURCE_FINGER ||
        source == WL_POINTER_AXIS_SOURCE_CONTINUOUS) {
        const double h = axisH_, v = axisV_;
        axisV_ = axisH_ = v120V_ = v120H_ = 0.0;
        sawV120_ = false;
        WaylandWindow* w = pointerWin_;
        if (w && (h != 0.0 || v != 0.0)) {
            const float pxPerRow = w->uiScale() * kPrecisePxPerRow;   // fysiska
            Event e;
            e.type     = EventType::MouseWheel;
            e.windowId = w->id();
            e.mouseX   = w->clientX(pointerX_);   // positionen: klientens px
            e.mouseY   = w->clientY(pointerY_);
            e.wheelX   = w->toPhysical(h) / pxPerRow;   // sträckan
            e.wheelY   = -w->toPhysical(v) / pxPerRow;
            e.precise  = true;
            e.phase    = preciseWin_ == w ? GesturePhase::Update
                                          : GesturePhase::Begin;
            e.mods     = mods_;
            preciseWin_ = w;
            preciseX_   = e.mouseX;
            preciseY_   = e.mouseY;
            pushEvent(e);
        }
        if (stop)
            endPreciseScroll();
        return;
    }
    // Ett stopp i en ram utan precis källa avslutar ändå en pågående
    // sekvens — protokollet lovar inte att källan upprepas i stoppets ram.
    if (stop)
        endPreciseScroll();

    // Hjulet: value120 (finaste) > diskreta klick > kontinuerligt värde
    // (15 enheter ≈ ett klick). Tecken som D3D11: vertikalt positivt =
    // uppåt (Wayland: positivt = nedåt → negera), horisontellt positivt =
    // höger (samma tecken som Wayland).
    double rowsY = 0.0, rowsX = 0.0;
    if (v120V_ != 0.0 || v120H_ != 0.0) {
        rowsY = v120V_ / 120.0;
        rowsX = v120H_ / 120.0;
    } else {
        rowsY = axisV_ / 15.0;
        rowsX = axisH_ / 15.0;
    }
    axisV_ = axisH_ = v120V_ = v120H_ = 0.0;
    sawV120_ = false;
    if ((rowsY == 0.0 && rowsX == 0.0) || !pointerWin_)
        return;
    Event e;
    e.type     = EventType::MouseWheel;
    e.windowId = pointerWin_->id();
    e.mouseX   = pointerWin_->clientX(pointerX_);
    e.mouseY   = pointerWin_->clientY(pointerY_);
    e.wheelX   = static_cast<float>(rowsX);
    e.wheelY   = static_cast<float>(-rowsY);
    e.mods     = mods_;
    pushEvent(e);
}

void WaylandDisplay::endPreciseScroll()
{
    WaylandWindow* w = preciseWin_;
    if (!w)
        return;
    preciseWin_ = nullptr;
    Event e;
    e.type     = EventType::MouseWheel;
    e.windowId = w->id();
    // Står pekaren kvar i fönstret är det dess position; annars den sista
    // sekvensen bar (End kommer också när pekaren redan lämnat ytan).
    e.mouseX   = pointerWin_ == w ? w->clientX(pointerX_) : preciseX_;
    e.mouseY   = pointerWin_ == w ? w->clientY(pointerY_) : preciseY_;
    e.precise  = true;
    e.phase    = GesturePhase::End;
    e.mods     = mods_;
    pushEvent(e);
}

// ---------------------------------------------------------------- nypningen

void WaylandDisplay::ensurePinch()
{
    if (pinch_ || !gestures_ || !pointer_)
        return;
    pinch_ = zwp_pointer_gestures_v1_get_pinch_gesture(gestures_, pointer_);
    static const zwp_pointer_gesture_pinch_v1_listener pinchListener = {
        // begin
        [](void* dd, zwp_pointer_gesture_pinch_v1*, std::uint32_t,
           std::uint32_t, wl_surface* surface, std::uint32_t fingers) {
            static_cast<WaylandDisplay*>(dd)->pinchBegin(surface, fingers);
        },
        // update — dx/dy (gestens mittpunkt) används inte: kontraktets
        // ankare är pekaren, som står still under en gest.
        [](void* dd, zwp_pointer_gesture_pinch_v1*, std::uint32_t,
           wl_fixed_t, wl_fixed_t, wl_fixed_t scale, wl_fixed_t rotation) {
            static_cast<WaylandDisplay*>(dd)->pinchUpdate(
                wl_fixed_to_double(scale), wl_fixed_to_double(rotation));
        },
        // end
        [](void* dd, zwp_pointer_gesture_pinch_v1*, std::uint32_t,
           std::uint32_t, std::int32_t cancelled) {
            static_cast<WaylandDisplay*>(dd)->pinchEnd(cancelled != 0);
        },
    };
    zwp_pointer_gesture_pinch_v1_add_listener(pinch_, &pinchListener, this);
}

// Bara TVÅFINGERSnypningen blir en `Gesture` — tre och fyra fingrar ingår
// inte (kompositören tar ofta dem själv). En nypning med fler
// fingrar lämnar `pinchWin_` tom, så dess update/end faller bort.
void WaylandDisplay::pinchBegin(wl_surface* surface, std::uint32_t fingers)
{
    pinchWin_   = fingers == 2 ? windowFor(surface) : nullptr;
    pinchScale_ = 1.0;
    if (pinchWin_)
        pushGesture(GesturePhase::Begin, 1.0f, 0.0f);
}

// Protokollets `scale` är ABSOLUT från begin och kontraktets
// `gestureScale` gäller från FÖRRA eventet — kvoten mot förra skalan.
// `rotation` är redan grader medurs från förra eventet (protokolltexten),
// alltså kontraktets tecken rakt av.
void WaylandDisplay::pinchUpdate(double scale, double rotation)
{
    if (!pinchWin_)
        return;
    float ratio = 1.0f;
    if (scale > 0.0) {
        ratio       = static_cast<float>(scale / pinchScale_);
        pinchScale_ = scale;
    }
    pushGesture(GesturePhase::Update, ratio, static_cast<float>(rotation));
}

void WaylandDisplay::pinchEnd(bool cancelled)
{
    if (!pinchWin_)
        return;
    pushGesture(cancelled ? GesturePhase::Cancel : GesturePhase::End, 1.0f,
                0.0f);
    pinchWin_ = nullptr;
}

// Ankaret är PEKAREN (kontraktet): fönstrets aktuella pekarposition i
// fysiska px. Står pekaren inte i gestens fönster — kompositören skickar
// gesten till pekarens yta, så det ska inte hända — är mitten ankaret.
// Modifierarna är `mods_`, som hjulets.
void WaylandDisplay::pushGesture(GesturePhase phase, float scale,
                                 float rotation)
{
    WaylandWindow* w = pinchWin_;
    Event e;
    e.type            = EventType::Gesture;
    e.windowId        = w->id();
    e.mouseX          = pointerWin_ == w ? w->clientX(pointerX_)
                                         : static_cast<float>(w->physW_) * 0.5f;
    e.mouseY          = pointerWin_ == w ? w->clientY(pointerY_)
                                         : static_cast<float>(w->physH_) * 0.5f;
    e.phase           = phase;
    e.gestureScale    = scale;
    e.gestureRotation = rotation;
    e.mods            = mods_;
    pushEvent(e);
}

// ---------------------------------------------------------------- keyboard

void WaylandDisplay::keymapChanged(int fd, std::uint32_t size)
{
    void* map = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        ::close(fd);
        return;
    }
    xkb_keymap* km = xkb_keymap_new_from_string(
        xkb_, static_cast<const char*>(map),
        XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    ::munmap(map, size);
    ::close(fd);
    if (!km)
        return;
    if (xkbState_)
        xkb_state_unref(xkbState_);
    if (keymap_)
        xkb_keymap_unref(keymap_);
    keymap_   = km;
    xkbState_ = xkb_state_new(keymap_);

    // Compose (dead keys: ˝a → ä) ur användarens locale.
    if (!composeTable_) {
        const char* locale = std::getenv("LC_ALL");
        if (!locale || !*locale) locale = std::getenv("LC_CTYPE");
        if (!locale || !*locale) locale = std::getenv("LANG");
        if (!locale || !*locale) locale = "C";
        composeTable_ = xkb_compose_table_new_from_locale(
            xkb_, locale, XKB_COMPOSE_COMPILE_NO_FLAGS);
        if (composeTable_)
            composeState_ = xkb_compose_state_new(
                composeTable_, XKB_COMPOSE_STATE_NO_FLAGS);
    }
}

void WaylandDisplay::modifiersChanged(std::uint32_t depressed,
                                      std::uint32_t latched,
                                      std::uint32_t locked,
                                      std::uint32_t group)
{
    if (!xkbState_)
        return;
    xkb_state_update_mask(xkbState_, depressed, latched, locked, 0, 0,
                          group);
    mods_ = 0;
    auto active = [&](const char* name) {
        return xkb_state_mod_name_is_active(xkbState_, name,
                                            XKB_STATE_MODS_EFFECTIVE) > 0;
    };
    if (active(XKB_MOD_NAME_CTRL))  mods_ |= kModCtrl;
    if (active(XKB_MOD_NAME_SHIFT)) mods_ |= kModShift;
    if (active(XKB_MOD_NAME_ALT))   mods_ |= kModAlt;
    if (active(XKB_MOD_NAME_LOGO))  mods_ |= kModCmd;
}

void WaylandDisplay::keyEvent(std::uint32_t serial, std::uint32_t key,
                              bool pressed)
{
    if (pressed)
        lastInputSerial_ = serial;
    const std::uint32_t keycode = key + 8;   // evdev → XKB-offset
    emitKey(keycode, pressed, false);
    if (!keymap_)
        return;
    if (pressed) {
        if (xkb_keymap_key_repeats(keymap_, keycode))
            armRepeat(keycode);
    } else if (keycode == repeatKeycode_) {
        disarmRepeat();
    }
}

void WaylandDisplay::emitKey(std::uint32_t keycode, bool pressed,
                             bool isRepeat)
{
    if (!xkbState_ || !keyboardWin_)
        return;
    const xkb_keysym_t sym =
        xkb_state_key_get_one_sym(xkbState_, keycode);

    Event e;
    e.type     = pressed ? EventType::KeyDown : EventType::KeyUp;
    e.windowId = keyboardWin_->id();
    // Tangent-mappen är DELAD med X11-drivrutinen (xkb_keys.h), i ETT exemplar.
    // Kontraktet står vid `enum class Key` i gfx/events.h.
    e.key      = translateXkbKey(xkbState_, keymap_, keycode);
    // `wl_keyboard.modifiers` kommer EFTER `key`, så `mods_` är läget FÖRE
    // tangenten — men en modifierartangents eget event bär läget EFTER sig
    // (kontraktet vid `kModCtrl` i gfx/events.h).
    e.mods     = xkbModsAfterKey(mods_, sym, pressed);

    if (!pressed) {
        pushEvent(e);
        return;
    }

    // TEXTEN FÖRE KeyDown: KeyDown bär texten
    // trycket skriver, och dess TextInput köas direkt efter. Ctrl, Alt och
    // Super skriver inget (`xkbTypesText`, prövad FÖRE compose så Ctrl + död
    // tangent inte startar en sekvens); AltGr gör det. Compose först (dead
    // keys); repeat hoppar compose (en död tangent repeterar inte
    // meningsfullt).
    char buf[16] = {};
    int  len     = 0;
    if (xkbTypesText(xkbState_, keymap_, keycode)) {
        bool handled = false;
        bool noText  = false;
        if (!isRepeat && composeState_ &&
            xkb_compose_state_feed(composeState_, sym) ==
                XKB_COMPOSE_FEED_ACCEPTED) {
            switch (xkb_compose_state_get_status(composeState_)) {
            case XKB_COMPOSE_COMPOSING:
                noText = true;   // mitt i en sekvens — inget tecken än
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
        if (!handled && !noText)
            len = xkb_state_key_get_utf8(xkbState_, keycode, buf,
                                         sizeof buf);
        // Kontrolltecken (< 0x20, DEL) är kommandon, inte text — samma
        // filter som WM_CHAR-vägen.
        if (len > 0) {
            const unsigned char first = static_cast<unsigned char>(buf[0]);
            if (first < 0x20 || first == 0x7F)
                len = 0;
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
    t.windowId = keyboardWin_->id();
    std::memcpy(t.text, buf, nCopy);
    pushEvent(t);
}

void WaylandDisplay::armRepeat(std::uint32_t keycode)
{
    if (repeatFd_ < 0 || repeatRateHz_ <= 0)
        return;
    repeatKeycode_ = keycode;
    itimerspec its{};
    its.it_value.tv_sec  = repeatDelayMs_ / 1000;
    its.it_value.tv_nsec = static_cast<long>(repeatDelayMs_ % 1000) *
                           1000000L;
    const long intervalNs = 1000000000L / repeatRateHz_;
    its.it_interval.tv_sec  = intervalNs / 1000000000L;
    its.it_interval.tv_nsec = intervalNs % 1000000000L;
    ::timerfd_settime(repeatFd_, 0, &its, nullptr);
}

void WaylandDisplay::disarmRepeat()
{
    repeatKeycode_ = 0;
    if (repeatFd_ >= 0) {
        itimerspec its{};
        ::timerfd_settime(repeatFd_, 0, &its, nullptr);
    }
}

// ---------------------------------------------------------------- cursor

// Det EFFEKTIVA valet: resize-zonens pil över en sådan zon, annars den form
// pekarfönstret begärt (klientytan) eller pilen (captionen, knapparna —
// fönstrets begäran gäller klientytan, kontraktet vid `Window::setCursor`).
void WaylandDisplay::refreshCursor()
{
    if (!pointerWin_)
        return;
    CursorNames cn = cursorNamesFor(lastCursorRole_);
    if (!cn.names[0])
        cn = cursorNamesFor(lastCursorRole_ == HitRole::Client
                                ? pointerWin_->cursor()
                                : PointerShape::Arrow);
    if (cn.names[0] == appliedCursor_)
        return;   // redan satt — inget nytt protokollanrop per rörelse
    applyCursorNamed(cn.names);
}

void WaylandWindow::setCursor(PointerShape shape)
{
    if (cursor_ == shape)
        return;
    cursor_ = shape;
    if (display_ && display_->pointerWin_ == this)
        display_->refreshCursor();
}

void WaylandDisplay::applyCursorNamed(const char* const* names)
{
    if (!pointer_ || !shm_ || !compositor_)
        return;
    // INGEN PEKARE: en NULL-yta döljer pekaren
    // över ytan tills nästa set_cursor — inget tema behövs.
    if (names[0] == kHiddenName) {
        wl_pointer_set_cursor(pointer_, pointerEnterSerial_, nullptr, 0, 0);
        appliedCursor_ = names[0];
        return;
    }
    if (!cursorTheme_) {
        int size = 24;
        if (const char* env = std::getenv("XCURSOR_SIZE"))
            if (const int v = std::atoi(env); v > 0)
                size = v;
        cursorScale_ = maxOutputScale();
        cursorTheme_ = wl_cursor_theme_load(std::getenv("XCURSOR_THEME"),
                                            size * cursorScale_, shm_);
        if (!cursorTheme_)
            return;
        cursorSurface_ = wl_compositor_create_surface(compositor_);
    }
    if (!cursorSurface_)
        return;

    // Fyra kandidater (CursorNames), första som finns i temat vinner.
    wl_cursor* cursor = nullptr;
    for (int i = 0; i < 4 && names[i]; ++i) {
        cursor = wl_cursor_theme_get_cursor(cursorTheme_, names[i]);
        if (cursor && cursor->image_count > 0)
            break;
        cursor = nullptr;
    }
    if (!cursor)
        return;
    wl_cursor_image* img = cursor->images[0];
    wl_buffer* buf       = wl_cursor_image_get_buffer(img);
    if (!buf)
        return;
    wl_surface_set_buffer_scale(cursorSurface_, cursorScale_);
    wl_surface_attach(cursorSurface_, buf, 0, 0);
    wl_surface_damage(cursorSurface_, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(cursorSurface_);
    wl_pointer_set_cursor(pointer_, pointerEnterSerial_, cursorSurface_,
                          static_cast<int>(img->hotspot_x) / cursorScale_,
                          static_cast<int>(img->hotspot_y) / cursorScale_);
    appliedCursor_ = names[0];   // satt — `refreshCursor` dedupar mot det
}

// ---------------------------------------------------------------- klippbord

void WaylandDisplay::selectionOffered(wl_data_offer* offer)
{
    if (selectionOffer_) {
        if (auto* od = static_cast<OfferData*>(
                wl_data_offer_get_user_data(selectionOffer_)))
            delete od;
        wl_data_offer_destroy(selectionOffer_);
    }
    selectionOffer_ = offer;
    selectionHasUtf8_ = false;
    if (offer) {
        if (auto* od =
                static_cast<OfferData*>(wl_data_offer_get_user_data(offer)))
            selectionHasUtf8_ = !od->mime.empty();
    } else {
        weOwnSelection_ = false;   // klippbordet tömt av någon annan
    }
}

std::string WaylandDisplay::clipboardText()
{
    // Äger vi selection läser vi ALDRIG via pipe — vi kan inte besvara
    // vårt eget send-event medan vi blockerar i read (deadlock).
    if (weOwnSelection_)
        return ownClipboardText_;
    if (!selectionOffer_ || !selectionHasUtf8_)
        return {};
    auto* od = static_cast<OfferData*>(
        wl_data_offer_get_user_data(selectionOffer_));
    if (!od || od->mime.empty())
        return {};

    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0)
        return {};
    wl_data_offer_receive(selectionOffer_, od->mime.c_str(), fds[1]);
    ::close(fds[1]);
    wl_display_flush(display_);

    // Läs med poll-timeout — får ALDRIG blockera pumpen (avsändaren kan
    // vara trög/död). Storleksbegränsning: klippbords-text, inte filer.
    std::string out;
    constexpr std::size_t kMaxBytes  = 8 * 1024 * 1024;
    constexpr int         kTimeoutMs = 250;
    char buf[4096];
    for (;;) {
        pollfd pf{fds[0], POLLIN, 0};
        const int pr = ::poll(&pf, 1, kTimeoutMs);
        if (pr <= 0)
            break;   // timeout/fel — returnera det vi hann få
        const ssize_t n = ::read(fds[0], buf, sizeof buf);
        if (n <= 0)
            break;
        out.append(buf, static_cast<std::size_t>(n));
        if (out.size() > kMaxBytes) {
            log("clipboardText: avbryter — mer än %zu byte", kMaxBytes);
            break;
        }
    }
    ::close(fds[0]);
    return out;
}

void WaylandDisplay::setClipboardText(const std::string& utf8)
{
    ownClipboardText_ = utf8;
    if (!dataDevice_ || !dataDeviceManager_)
        return;
    if (ownSource_)
        wl_data_source_destroy(ownSource_);
    ownSource_ = wl_data_device_manager_create_data_source(
        dataDeviceManager_);
    static const wl_data_source_listener srcListener = {
        [](void*, wl_data_source*, const char*) {},   // target (dnd)
        // send: skriv texten till fd:n och stäng. Callbacken körs INNE i
        // Wayland-dispatchen, så en blockerande write vore en frysning av
        // hela den enkeltrådade pumpen om mottagaren slutar läsa och
        // pipebufferten fylls. Samma poll-med-timeout-disciplin som
        // läsvägen i clipboardText().
        [](void* dd, wl_data_source*, const char*, int fd) {
            auto* d = static_cast<WaylandDisplay*>(dd);
            const std::string& s = d->ownClipboardText_;
            const int fl = ::fcntl(fd, F_GETFL, 0);
            if (fl != -1)
                ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
            constexpr int kTimeoutMs = 250;   // per väntan på skrivbarhet
            std::size_t off = 0;
            while (off < s.size()) {
                const ssize_t n =
                    ::write(fd, s.data() + off, s.size() - off);
                if (n > 0) {
                    off += static_cast<std::size_t>(n);
                    continue;
                }
                if (n < 0 && errno == EINTR)
                    continue;
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    pollfd pf{fd, POLLOUT, 0};
                    if (::poll(&pf, 1, kTimeoutMs) > 0 &&
                        !(pf.revents & (POLLERR | POLLHUP | POLLNVAL)))
                        continue;
                    log("klippbordet: mottagaren läser inte — avbryter "
                        "efter %zu/%zu byte",
                        off, s.size());
                }
                break;   // trög/död mottagare eller fel — ge upp
            }
            ::close(fd);
        },
        // cancelled: någon annan tog över klippbordet
        [](void* dd, wl_data_source* src) {
            auto* d = static_cast<WaylandDisplay*>(dd);
            wl_data_source_destroy(src);
            if (d->ownSource_ == src) {
                d->ownSource_      = nullptr;
                d->weOwnSelection_ = false;
            }
        },
        [](void*, wl_data_source*) {},                  // dnd_drop_performed
        [](void*, wl_data_source*) {},                  // dnd_finished
        [](void*, wl_data_source*, std::uint32_t) {},   // action
    };
    wl_data_source_add_listener(ownSource_, &srcListener, this);
    wl_data_source_offer(ownSource_, "text/plain;charset=utf-8");
    wl_data_source_offer(ownSource_, "text/plain");
    wl_data_source_offer(ownSource_, "UTF8_STRING");
    // Kräver en färsk input-serial (senaste tryck/tangent) — utan input
    // ännu kan kompositören neka (best effort, som fasaden tillåter).
    wl_data_device_set_selection(dataDevice_, ownSource_,
                                 lastInputSerial_);
    if (lastInputSerial_ != 0) {
        weOwnSelection_ = true;
    } else {
        // Serial 0 nekas i praktiken — flagga INTE ägarskap, annars
        // kortsluter clipboardText() till vår lokala sträng fast andra
        // appar fortfarande ser den gamla selectionen.
        weOwnSelection_ = false;
        log("setClipboardText: ingen input-serial ännu — selection kan "
            "nekas (best effort)");
    }
    wl_display_flush(display_);
}

// ---------------------------------------------------------------- fabrik

LinuxDisplay* createWaylandDisplay()
{
    return WaylandDisplay::create();
}

} // namespace gfx
