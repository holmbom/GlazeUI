// WaylandWindow — gfx::Window ovanpå wl_surface + xdg_surface +
// xdg_toplevel/xdg_popup + wl_egl_window. Configure-handskakningen är
// protokollKRAV: ingen buffer före första ack_configure (fel = protokoll-
// fel = disconnect = appen dör) — init committar utan buffer och väntar in
// första configure innan EGL-ytan skapas och första framen swappas.
// Interaktiv move/resize startas ur hit-test-kartan (xdg_toplevel_move/
// resize); popups placeras förälder-relativt via xdg_positioner
// (fasadändringens popup-kontrakt — Wayland saknar skärmpositioner).
//
// STORLEKSMARGINALEN UTANFÖR YTAN —
// libdecor/GTK4:s form: ytan är
// klientytan + 2m ytenheter, genomskinlig i marginalen (EGL-konfigen bär alfa,
// och den opaka regionen är bara klientytan), och
// `xdg_surface.set_window_geometry(m, m, b, h)` säger kompositören var
// fönstret är — configurens storlek, `set_min/max_size` och positionerarens
// ankare (en popups förälder) gäller då klientytan av sig själva. Pekaren i
// marginalen startar `xdg_toplevel_resize` (`interceptPress` →
// `hitRoleAt`); alla positioner dras av med marginalen (`clientX/Y`).
// Marginalen är 0 för popuper, Native/None, ett fast fönster och maximerat,
// helskärm eller tiled — där läser `hitRoleAt` kartans zoner som vanligt.

#include <poll.h>
#include <sys/mman.h>   // memfd_create + mmap (programikonens buffertar)
#include <unistd.h>     // ftruncate + close


#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>

#include "../display_util.h"
#include "../frame_margin.h"
#include "../icon_util.h"
#include "gfx/platform.h"
#include "wayland_backend.h"

namespace gfx {

// ---------------------------------------------------------------- listeners

namespace {

const xdg_surface_listener kXdgSurfaceListener = {
    [](void* data, xdg_surface*, std::uint32_t serial) {
        static_cast<WaylandWindow*>(data)->onSurfaceConfigure(serial);
    }};

const xdg_toplevel_listener kToplevelListener = {
    // configure
    [](void* data, xdg_toplevel*, std::int32_t w, std::int32_t h,
       wl_array* states) {
        static_cast<WaylandWindow*>(data)->onToplevelConfigure(w, h,
                                                               states);
    },
    // close
    [](void* data, xdg_toplevel*) {
        static_cast<WaylandWindow*>(data)->onCloseRequested();
    },
    // configure_bounds (v4 — binds ej, stub för structen)
    [](void*, xdg_toplevel*, std::int32_t, std::int32_t) {},
    // wm_capabilities (v5 — binds ej)
    [](void*, xdg_toplevel*, wl_array*) {},
};

const xdg_popup_listener kPopupListener = {
    // configure
    [](void* data, xdg_popup*, std::int32_t, std::int32_t, std::int32_t w,
       std::int32_t h) {
        static_cast<WaylandWindow*>(data)->onPopupConfigure(w, h);
    },
    // popup_done — kompositören avfärdade popupen (klick utanför/Escape)
    [](void* data, xdg_popup*) {
        static_cast<WaylandWindow*>(data)->onPopupDone();
    },
    // repositioned
    [](void*, xdg_popup*, std::uint32_t) {},
};

const wp_fractional_scale_v1_listener kFractionalListener = {
    [](void* data, wp_fractional_scale_v1*, std::uint32_t scale120) {
        static_cast<WaylandWindow*>(data)->onPreferredScale(scale120);
    }};

// Per-surface-outputspårningen (blandad DPI): enter/leave säger vilka
// outputs ytan faktiskt visas på — skalan räknas ur max av DEM, inte av
// alla anslutna skärmar (annars renderas ett 100 %-fönster i 200 % så
// fort EN 200 %-skärm finns). Gäller heltalsskale-fallbacken;
// fractional-scale-protokollet vinner där det finns (GNOME ≥ 47).
const wl_surface_listener kSurfaceListener = {
    // enter
    [](void* data, wl_surface*, wl_output* o) {
        static_cast<WaylandWindow*>(data)->onSurfaceEnterOutput(o);
    },
    // leave
    [](void* data, wl_surface*, wl_output* o) {
        static_cast<WaylandWindow*>(data)->onSurfaceLeaveOutput(o);
    },
    // preferred_buffer_scale (v6 — binds ej, stub för structen)
    [](void*, wl_surface*, std::int32_t) {},
    // preferred_buffer_transform (v6 — binds ej)
    [](void*, wl_surface*, std::uint32_t) {},
};

// xdg-activation: token begärs, kompositören svarar med done → activate.
// Requesten är SPÅRAD (ActivationRequest i wayland_backend.h): innan
// pekaren derefereras slås den upp i displayens lista — target-fönstret
// kan ha stängts (windowDestroyed städade då request + token) och `data`
// vore i så fall dinglande. Displayen hämtas via singletonen, inte req.
const xdg_activation_token_v1_listener kActivationTokenListener = {
    [](void* data, xdg_activation_token_v1* token, const char* tokenStr) {
        auto* d = static_cast<WaylandDisplay*>(LinuxDisplay::current());
        auto* req = static_cast<ActivationRequest*>(data);
        if (!d || !d->untrackActivation(req)) {
            xdg_activation_token_v1_destroy(token);
            return;   // requesten lever inte längre — rör inte pekaren
        }
        xdg_activation_v1_activate(d->activation(), tokenStr,
                                   req->target);
        xdg_activation_token_v1_destroy(token);
        d->flushDisplay();
        delete req;
    }};

} // namespace

// ---------------------------------------------------------------- init

int WaylandWindow::physFromLogical(int logical) const
{
    return static_cast<int>(
        std::lround(static_cast<double>(logical) * scale120_ / 120.0));
}

int WaylandWindow::surfaceUnitsFromLogical(int logical) const
{
    // Ytenheter = logiska px × textskalning × ANVÄNDARSKALAN —
    // monitorskalan läggs på i physFromLogical. Tillbakaräkningarna
    // (onSurfaceConfigure) delar med SAMMA produkt, annars driftar
    // storleken vid varje skalbyte.
    return std::max(1, static_cast<int>(std::lround(
                           static_cast<double>(logical) *
                           textScaleFactor() * userScale())));
}

bool WaylandWindow::init(const WindowDesc& desc)
{
    chrome_      = desc.chrome;
    transparent_ = desc.transparent;
    hidden_      = desc.hidden;
    title_       = desc.title;
    isPopup_     = desc.popupAnchorWindow != nullptr;
    noActivate_  = desc.noActivate;
    // STARTLÄGET — tillämpas före den commit som
    // mappar ytan (`applyStartState`).
    startState_        = desc.startState;
    startStateApplied_ = false;

    // POPUPS är per definition ramlösa. TRANSPARENS är det däremot INTE:
    // ett RUNDAT fönster är en genomskinlig yta MED eget chrome — hörnen
    // är bortklippta, inte målade. Titelraden är en widget i trädet och
    // ritas av appen, som därmed också äger hörnen.
    if (isPopup_ && chrome_ != Chrome::None)
        chrome_ = Chrome::None;

    windowId_ = nextWindowId();
    // Före första wl_surface.enter är fönstrets outputs okända —
    // maxSurfaceOutputScale faller då tillbaka på displayens största.
    scale120_ = maxSurfaceOutputScale() * 120;
    // Fasadändringen: desc-måtten är LOGISKA px. Wayland-ytenheter bär
    // monitorskalan men INTE textskalningen — ytenheter = logiska px ×
    // textskalning, så innehållet (utlagt i logiska px × uiScale) fyller
    // bufferten exakt.
    descW_    = desc.width > 0 ? desc.width : 1;
    descH_    = desc.height > 0 ? desc.height : 1;
    logicalW_ = surfaceUnitsFromLogical(descW_);
    logicalH_ = surfaceUnitsFromLogical(descH_);

    surface_ = wl_compositor_create_surface(display_->compositor());
    if (!surface_)
        return false;
    display_->registerSurface(surface_, this);
    wl_surface_add_listener(surface_, &kSurfaceListener, this);

    if (display_->fractionalScaleManager()) {
        fractional_ = wp_fractional_scale_manager_v1_get_fractional_scale(
            display_->fractionalScaleManager(), surface_);
        wp_fractional_scale_v1_add_listener(fractional_,
                                            &kFractionalListener, this);
    }
    if (display_->viewporter())
        viewport_ =
            wp_viewporter_get_viewport(display_->viewporter(), surface_);

    xdgSurface_ =
        xdg_wm_base_get_xdg_surface(display_->wmBase(), surface_);
    xdg_surface_add_listener(xdgSurface_, &kXdgSurfaceListener, this);

    if (isPopup_) {
        // Popup-kontraktet: ankarrect i ägarens KLIENT-FYSISKA px →
        // ägarens logiska ytkoordinater; kompositören sköter flip/slide
        // (FLIP_Y | SLIDE_X).
        auto* parent = static_cast<WaylandWindow*>(
            unwrapWindow(desc.popupAnchorWindow));
        if (!parent || !parent->xdgSurface_) {
            log("popup: ankarfönstret saknar yta");
            shutdown();
            return false;
        }
        popupParent_   = parent;   // fokusgruppen (se fokus-kontraktet)
        const float ps = parent->scale120_ / 120.0f;
        const Rectf& a = desc.popupAnchorRect;
        positioner_ = xdg_wm_base_create_positioner(display_->wmBase());
        xdg_positioner_set_anchor_rect(
            positioner_, static_cast<int>(std::floor(a.x / ps)),
            static_cast<int>(std::floor(a.y / ps)),
            std::max(1, static_cast<int>(std::ceil(a.w / ps))),
            std::max(1, static_cast<int>(std::ceil(a.h / ps))));
        xdg_positioner_set_size(positioner_, logicalW_, logicalH_);
        xdg_positioner_set_anchor(positioner_,
                                  XDG_POSITIONER_ANCHOR_BOTTOM_LEFT);
        xdg_positioner_set_gravity(positioner_,
                                   XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
        xdg_positioner_set_constraint_adjustment(
            positioner_,
            XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y |
                XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X);
        popup_ = xdg_surface_get_popup(xdgSurface_, parent->xdgSurface_,
                                       positioner_);
        xdg_popup_add_listener(popup_, &kPopupListener, this);
        // Grab: dismissal vid klick-utanför (popup_done). Serialen ska
        // komma från en levande implicit grab på ÄGARENS yta. En serial
        // kompositören inte känner igen som färsk ger DISMISSAL — popupen
        // får `popup_done` direkt och stängs vår vanliga väg; det är alltså
        // inget protokollfel. `invalid_grab` gäller något annat: fel
        // FÖRÄLDER, eller en grab begärd efter att popupen mappats.
        // Duger serialen inte (programmatisk open(), tangentbordsöppning,
        // släppt knapp) lever popupen på App::closePopups-vägen i stället.
        if (const std::uint32_t gs = display_->grabSerialFor(parent))
            xdg_popup_grab(popup_, display_->seat(), gs);
    } else {
        toplevel_ = xdg_surface_get_toplevel(xdgSurface_);
        xdg_toplevel_add_listener(toplevel_, &kToplevelListener, this);
        xdg_toplevel_set_title(toplevel_, title_.c_str());
        // APPENS ID: `gfx::setAppId`, förval programfilens
        // namn — skrivbordet hittar appens `.desktop`-fil, och därmed dess
        // ikon, genom det.
        xdg_toplevel_set_app_id(toplevel_, appId().c_str());
        // PROGRAMIKONEN: ramar satta före init följer med den första
        // committen nedan (set_icon är dubbelbuffrad).
        applyIcon();
        // STARTGRÄNSERNA — desc-fälten är bara startvärdet
        // för `setSizeLimits`, så de lagras i samma fält och skickas
        // genom samma väg.
        limMinW_ = desc.minWidth;
        limMinH_ = desc.minHeight;
        limMaxW_ = desc.maxWidth;
        limMaxH_ = desc.maxHeight;
        applySizeLimits();
        // ALLTID ÖVERST: går samma väg som sättaren så begäran LOGGAS en
        // gång i stället för att tyst falla bort.
        setTopmost(desc.topmost);
        if (desc.owner) {
            auto* owner = static_cast<WaylandWindow*>(
                unwrapWindow(desc.owner));
            if (owner && owner->toplevel_)
                xdg_toplevel_set_parent(toplevel_, owner->toplevel_);
        }
        // desc.noActivate/toolWindow: ingen protokollväg för toplevels —
        // dokumenterad no-op (xdg_popup är den naturliga formen).
        // desc.showInactive: likaså — kompositören avgör
        // om ett nytt fönster får fokus; fasadens tabell lovar ingen inaktiv
        // visning här.
        // desc.showInTaskbar: en DUMMY — xdg-shell saknar
        // formen; proxyns init loggar begäran (linux_platform.cpp).

        if (display_->decorationManager()) {
            decoration_ =
                zxdg_decoration_manager_v1_get_toplevel_decoration(
                    display_->decorationManager(), toplevel_);
            // Glaze ritar sitt eget chrome — begär client_side; vid
            // Chrome::Native försöker vi server_side (KDE ritar då ram).
            zxdg_toplevel_decoration_v1_set_mode(
                decoration_,
                chrome_ == Chrome::Native
                    ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
                    : ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
        } else if (chrome_ == Chrome::Native) {
            log("wayland: ingen decoration-manager — Chrome::Native blir "
                "ramlöst (GNOME ritar inga server-ramar)");
        }
    }

    // STARTLÄGET före den första committen — också för ett fönster som skapas
    // DOLT: `onLoad` körs före synligheten, så varje fönster utom popuperna
    // skapas dolt, och configuren
    // nedan ska bära rätt storlek redan när den första bufferten kommer.
    // `set_maximized` är en BEGÄRAN, och configuren bär svaret
    // (`pendingMaximized_`); ett läge som ändras före den första visningen går
    // genom `maximize()`/`restore()`, som är begäranden de också.
    applyStartState();

    // Initial commit UTAN buffer → kompositören svarar med configure;
    // först efter ack får en buffer committas (protokollkravet).
    wl_surface_commit(surface_);
    if (!waitConfigured()) {
        log("wayland: ingen configure — kompositören svarar inte");
        shutdown();
        return false;
    }

    applySize(logicalW_, logicalH_, false);

    // Bufferten är YTAN: klientytan plus storleksmarginalen runt om (E2L) —
    // renderaren ritar klientytan i en FBO av klientens storlek och lägger
    // den marginalen in (`setFrameMargin`, satt av applySize).
    eglWindow_ = wl_egl_window_create(surface_, physW_ + 2 * frameMargin_,
                                      physH_ + 2 * frameMargin_);
    if (!eglWindow_) {
        shutdown();
        return false;
    }
    eglSurface_ = eglCreatePlatformWindowSurface(
        display_->egl(), display_->eglConfig(), eglWindow_, nullptr);
    if (eglSurface_ == EGL_NO_SURFACE)
        eglSurface_ = eglCreateWindowSurface(
            display_->egl(), display_->eglConfig(),
            reinterpret_cast<EGLNativeWindowType>(eglWindow_), nullptr);
    if (eglSurface_ == EGL_NO_SURFACE) {
        log("wayland: EGL-ytan kunde inte skapas: 0x%04x", eglGetError());
        shutdown();
        return false;
    }
    if (!renderer_.init(display_->egl(), display_->eglConfig(),
                        eglSurface_, physW_, physH_, transparent_)) {
        shutdown();
        return false;
    }

    visible_ = !hidden_;   // dolt fönster: buffern committas aldrig (swap
                           // hoppar) → ytan förblir omappad
    // …och den första visningen behöver ingen ny configure-runda: ytan är
    // konfigurerad, och ingen buffert har kommittats (`setVisible`).
    firstMapPending_ = hidden_;
    registerLinuxWindow(this);
    measureDisplay(false);   // skärmens mm — enter mäter om när den kommer
    if (!hidden_) {
        Event e;
        e.type     = EventType::WindowShown;
        e.windowId = windowId_;
        pushEvent(e);
    }
    return true;
}

// Väntan på första configure är TIDSbegränsad, inte VARVbegränsad: ett fast
// antal `wl_display_roundtrip` vore ingen gräns, för varje roundtrip
// återvänder så fort kompositören svarat på VÅR sync, alltså mikrosekunder
// när den svarar normalt. Dröjer den första configuren (kompositören kan
// t.ex. inte applicera vår initial-commit direkt) skulle vi ge upp nästan
// omedelbart och riva ett fönster som är på väg att mappas. Vi sover i
// poll() i stället — ingen CPU, och ingen skur av syncs mot kompositören.
bool WaylandWindow::waitConfigured()
{
    constexpr std::uint64_t kConfigureTimeoutMs = 1000;

    wl_display*         dpy   = display_->display();
    const std::uint64_t start = ticksMs();
    for (;;) {
        // PREPARE_READ-DANSEN, samma som pumpens prepoll/postpoll och
        // beforeSwap — och den är vad som gör tidsgränsen VERKLIG. Ett rakt
        // `wl_display_dispatch` blockerar i sin EGEN `poll(-1)`: läser den
        // in enbart events som hör till en annan kö (frame-callbacken har
        // sin, Mesas buffer-release ligger i EGL:s) dispatchar den noll och
        // väntar vidare utan tak. Här är det bara VÅR poll som väntar.
        // Loopen terminerar: prepare_read misslyckas bara när default-kön
        // är icke-tom, och dispatch_pending tömmer den.
        while (wl_display_prepare_read(dpy) != 0)
            if (wl_display_dispatch_pending(dpy) < 0)
                return false;
        // Dispatchen kör callbacks — en uppskjuten fokusförlust får inte
        // bli kvar innehållen här (fokus-kontraktet).
        display_->flushPendingLeave();
        if (configured_) {
            wl_display_cancel_read(dpy);   // läslåset MÅSTE släppas
            return true;
        }

        const std::uint64_t spent = ticksMs() - start;
        if (spent >= kConfigureTimeoutMs) {
            wl_display_cancel_read(dpy);
            // Följden står hos anroparen: init river ytan, en återvisning
            // och ett startläge mappar med den förra configuren.
            log("wayland: ingen configure på %u ms",
                static_cast<unsigned>(spent));
            return false;
        }

        // OBS: bara POLLIN, av exakt samma skäl som i beforeSwap — läslåset
        // är taget, och en poll som vaknar på SKRIVBAR men inte LÄSBAR fd
        // leder in i wl_display_read_events utan data. En EAGAIN från
        // flushen (full skrivbuffert) tas om i nästa varv i stället;
        // tidsgränsen är skyddet.
        pollfd pf{wl_display_get_fd(dpy), POLLIN, 0};
        if (wl_display_flush(dpy) < 0 && errno != EAGAIN) {
            wl_display_cancel_read(dpy);
            return false;
        }
        const int r = ::poll(&pf, 1,
                             static_cast<int>(kConfigureTimeoutMs - spent));
        if (r <= 0) {
            wl_display_cancel_read(dpy);   // varje utgång släpper låset
            if (r == 0 || errno == EINTR)
                continue;                  // tidsgränsen fångas överst
            return false;
        }
        if (wl_display_read_events(dpy) < 0)
            return false;   // läslåset släpps av read_events självt
    }
}

void WaylandWindow::shutdown()
{
    if (!surface_)
        return;
    stopAllTimers();
    // AVREGISTRERINGEN LIGGER FÖRST — och ordningen är bärande. Allt
    // härunder kan dispatcha lyssnare (protokollobjekt rivs, kön dräneras),
    // och så länge ytan står kvar i displayens tabell slår `windowFor` upp
    // ETT FÖNSTER SOM HÅLLER PÅ ATT RIVAS: en `wl_pointer.enter` eller
    // `wl_keyboard.enter` i flykt mot popupytan skulle då sätta tillbaka
    // `pointerWin_`/`keyboardWin_` till `this` — som `ProxyWindow::shutdown`
    // raderar en rad senare (`inner_.reset()`). Avregistrerad först
    // resolvas allt sådant till nullptr, och lyssnarna är null-säkra.
    unregisterLinuxWindow(this);
    display_->unregisterSurface(surface_);
    display_->windowDestroyed(this);
    if (frameCb_) {
        wl_callback_destroy(frameCb_);
        frameCb_ = nullptr;
    }
    renderer_.shutdownRenderer();
    if (eglSurface_ != EGL_NO_SURFACE) {
        eglDestroySurface(display_->egl(), eglSurface_);
        eglSurface_ = EGL_NO_SURFACE;
    }
    if (eglWindow_) {
        wl_egl_window_destroy(eglWindow_);
        eglWindow_ = nullptr;
    }
    if (decoration_) {
        zxdg_toplevel_decoration_v1_destroy(decoration_);
        decoration_ = nullptr;
    }
    if (fractional_) {
        wp_fractional_scale_v1_destroy(fractional_);
        fractional_ = nullptr;
    }
    if (viewport_) {
        wp_viewport_destroy(viewport_);
        viewport_ = nullptr;
    }
    if (popup_) {
        xdg_popup_destroy(popup_);
        popup_ = nullptr;
    }
    if (positioner_) {
        xdg_positioner_destroy(positioner_);
        positioner_ = nullptr;
    }
    releaseIcon();   // programikonen: objektet först, buffertarna sedan
    iconApplied_ = false;
    if (toplevel_) {
        xdg_toplevel_destroy(toplevel_);
        toplevel_ = nullptr;
    }
    if (xdgSurface_) {
        xdg_surface_destroy(xdgSurface_);
        xdgSurface_ = nullptr;
    }
    wl_surface_destroy(surface_);   // avregistrerad överst, se kommentaren
    surface_ = nullptr;
    surfaceOutputs_.clear();   // dog med ytan
    display_->flushDisplay();
    configured_   = false;
    maximized_    = false;
    fullscreen_   = false;
    tiled_        = false;
    marginSurf_   = 0;   // nästa init räknar om marginalen ur sitt läge
    frameMargin_  = 0;
    windowId_     = 0;
}

// ---------------------------------------------------------------- configure

void WaylandWindow::onToplevelConfigure(std::int32_t w, std::int32_t h,
                                        wl_array* states)
{
    pendingW_ = w;   // logiska ytenheter (fönstrets geometri); 0 = "välj själv"
    pendingH_ = h;
    pendingMaximized_  = false;
    pendingFullscreen_ = false;
    pendingTiled_      = false;
    if (states) {
        const auto* s = static_cast<const std::uint32_t*>(states->data);
        const std::size_t n = states->size / sizeof(std::uint32_t);
        for (std::size_t i = 0; i < n; ++i) {
            switch (s[i]) {
            case XDG_TOPLEVEL_STATE_MAXIMIZED:
                pendingMaximized_ = true;
                break;
            case XDG_TOPLEVEL_STATE_FULLSCREEN:
                pendingFullscreen_ = true;
                break;
            // TILED (xdg_wm_base v2, bunden v3): kanten står mot en annan
            // yta eller skärmens kant — ingen storleksmarginal (E2L).
            case XDG_TOPLEVEL_STATE_TILED_LEFT:
            case XDG_TOPLEVEL_STATE_TILED_RIGHT:
            case XDG_TOPLEVEL_STATE_TILED_TOP:
            case XDG_TOPLEVEL_STATE_TILED_BOTTOM:
                pendingTiled_ = true;
                break;
            default:
                break;
            }
        }
    }
}

void WaylandWindow::onSurfaceConfigure(std::uint32_t serial)
{
    xdg_surface_ack_configure(xdgSurface_, serial);
    const bool first = !configured_;
    configured_ = true;

    // ÅTERSTÄLLNINGS-FLANKEN. Kompositören återställer
    // sin ihågkomna storlek i den skala som gällde vid maximeringen och
    // vet ingenting om vår — hade skalan bytt under tiden är den
    // föreslagna storleken alltså fel. xdg-shell tillåter oss att avvika:
    // storleken i en configure UTAN maximized/fullscreen/tiled är ett
    // FÖRSLAG. Wayland behöver ingen skuld som X11 gör, för state och
    // storlek kommer i SAMMA configure — vi ser båda innan beslutet.
    const bool restoreFlank = maximized_ && !pendingMaximized_;

    const int lw = restoreFlank ? surfaceUnitsFromLogical(descW_)
                                : (pendingW_ > 0 ? pendingW_ : logicalW_);
    const int lh = restoreFlank ? surfaceUnitsFromLogical(descH_)
                                : (pendingH_ > 0 ? pendingH_ : logicalH_);
    pendingW_ = pendingH_ = 0;
    // LÄGET FÖRE STORLEKEN (E2L): storleksmarginalen hör till läget — ingen
    // marginal maximerat, i helskärm eller tiled — och läge och storlek kommer
    // i SAMMA configure, så `applySize` nedan ska se det nya läget.
    // Händelsen om maximeringen går ut efter storlekens.
    const bool maxBytt = toplevel_ && pendingMaximized_ != maximized_;
    if (toplevel_) {
        maximized_  = pendingMaximized_;
        fullscreen_ = pendingFullscreen_;
        tiled_      = pendingTiled_;
    }
    const bool storlekBytt = lw != logicalW_ || lh != logicalH_;
    if (storlekBytt || wantedMarginSurf() != marginSurf_) {
        // Kompositör-/användarstyrd storlek: uppdatera även den lagrade
        // DESC-LOGISKA storleken (ytenheter ÷ (textskalning ×
        // användarskalan) — samma produkt som surfaceUnitsFromLogical),
        // annars hoppar fönstret tillbaka vid nästa skalbyte.
        //
        // MEN INTE när configuren är MAXIMERINGENS eller
        // ÅTERSTÄLLNINGENS: maximeringens storlek är kompositörens och
        // inte användarens val, och räknas den tillbaka är
        // restore-storleken förstörd redan vid zoomen. `applySize` MÅSTE
        // ändå köras — kompositören äger ytan när den är maximerad — det
        // är bara den lagrade logiska storleken som ska stå kvar. Och inte
        // när bara marginalen bytte: klientytan är densamma.
        if (storlekBytt && !pendingMaximized_ && !restoreFlank) {
            const double p = static_cast<double>(textScaleFactor()) *
                             userScale();
            descW_ = std::max(1, static_cast<int>(std::lround(lw / p)));
            descH_ = std::max(1, static_cast<int>(std::lround(lh / p)));
        }
        applySize(lw, lh, !first);
    }

    if (maxBytt) {
        Event e;
        e.type     = maximized_ ? EventType::WindowMaximized
                                : EventType::WindowRestored;
        e.windowId = windowId_;
        pushEvent(e);
    }
    if (!first) {
        // En configure utan storleksbyte kan ändå kräva ny frame
        // (state-byte) — exponera så appen ritar om.
        Event e;
        e.type     = EventType::WindowExposed;
        e.windowId = windowId_;
        pushEvent(e);
    }
}

void WaylandWindow::onPopupConfigure(std::int32_t w, std::int32_t h)
{
    if (w > 0 && h > 0) {
        pendingW_ = w;
        pendingH_ = h;
    }
}

void WaylandWindow::onPopupDone()
{
    onCloseRequested();   // App stänger popupen (closePopups-semantiken)
}

void WaylandWindow::onCloseRequested()
{
    Event e;
    e.type     = EventType::WindowCloseRequested;
    e.windowId = windowId_;
    pushEvent(e);
}

void WaylandWindow::onPreferredScale(std::uint32_t scale120)
{
    if (scale120 == 0)
        return;
    fractionalActive_ = true;
    if (static_cast<int>(scale120) == scale120_)
        return;
    scale120_ = static_cast<int>(scale120);
    applySize(logicalW_, logicalH_, configured_);
    Event e;
    e.type     = EventType::WindowDisplayScaleChanged;
    e.windowId = windowId_;
    pushEvent(e);
}

void WaylandWindow::onTextScaleChanged()
{
    // Portalens textskalning bytte (D-Bus-signalen, broadcast-vägen i
    // linux_platform.cpp): samma DESC-LOGISKA storlek ger nya ytenheter
    // — buffert/FBO följer med i applySize och fönstret behåller sin
    // logiska storlek (mönster: onPreferredScale).
    if (!surface_)
        return;
    applySize(surfaceUnitsFromLogical(descW_),
              surfaceUnitsFromLogical(descH_), configured_);
    // Ytenheten betyder något annat efter ett skalbyte — gränserna
    // måste räknas om, annars klampar kompositören mot fel tal.
    // UTAN egen commit: `applySize` har
    // just bytt ytans layout — buffertens storlek, storleksmarginalens
    // geometri och den opaka regionen — och en commit nu hade lagt den över
    // den GAMLA bufferten. Händelserna från `applySize` ger en ny bildruta,
    // och dess swap committar allt tillsammans (xdg-shells dubbelbuffrade
    // tillstånd).
    applySizeLimits(false);
}

void WaylandWindow::userScaleChanged()
{
    // Användarskalan bytte: samma DESC-LOGISKA storlek ger nya
    // ytenheter/buffert — applySize resizar renderern SYNKRONT, så
    // skal-eventet efteråt ritas i rätt pixelstorlek.
    //
    // MAXIMERAT ÄR EN EGEN GREN. Kompositören ÄGER ytenheterna när fönstret
    // är maximerat: samma pixlar, ny layoutskala — exakt D3D11/Metal-mönstret.
    // En tillbakaräkning ur configuren skulle göra maximeringens mått till ny
    // "logisk" storlek, så ingen sådan sker.
    // `descW_/H_` står kvar och restore-flanken sätter om storleken.
    if (!surface_)
        return;
    const bool nyLayout = !maximized_;
    if (nyLayout) {
        applySize(surfaceUnitsFromLogical(descW_),
                  surfaceUnitsFromLogical(descH_), configured_);
    }
    // Ytenheten betyder något annat efter ett skalbyte — gränserna
    // måste räknas om, annars klampar kompositören mot fel tal.
    // Gäller ÄVEN maximerat: gränserna följer med till
    // återställningen. Committas med nästa swap när `applySize` bytt ytans
    // layout (se onTextScaleChanged); maximerat finns
    // ingen ny buffert på väg, och gränserna committas direkt.
    applySizeLimits(!nyLayout);
    Event e;
    e.type     = EventType::WindowDisplayScaleChanged;
    e.windowId = windowId_;
    pushEvent(e);
}

int WaylandWindow::maxSurfaceOutputScale() const
{
    // Max av FÖNSTRETS outputs (per-surface-spårningen). Före första
    // enter — eller när alla ytans outputs försvunnit — är setet tomt:
    // fall tillbaka på displayens största kända skala.
    int s = 0;
    for (const wl_output* o : surfaceOutputs_)
        s = std::max(s, display_->outputScaleOf(o));
    return s > 0 ? s : display_->maxOutputScale();
}

void WaylandWindow::onSurfaceEnterOutput(wl_output* o)
{
    for (const wl_output* have : surfaceOutputs_)
        if (have == o)
            return;
    surfaceOutputs_.push_back(o);
    onOutputScaleMaybeChanged();
    measureDisplay(true);   // kanske en annan skärm
}

void WaylandWindow::onSurfaceLeaveOutput(wl_output* o)
{
    for (auto it = surfaceOutputs_.begin(); it != surfaceOutputs_.end();
         ++it)
        if (*it == o) {
            surfaceOutputs_.erase(it);
            break;
        }
    onOutputScaleMaybeChanged();
    measureDisplay(true);
}

void WaylandWindow::outputRemoved(wl_output* o)
{
    // global_remove: som leave, men utan garanterat leave-event före —
    // pekaren får inte bli kvar (dinglande) i per-surface-setet.
    onSurfaceLeaveOutput(o);
}

void WaylandWindow::onOutputScaleMaybeChanged()
{
    if (fractionalActive_)
        return;   // preferred_scale styr
    const int s120 = maxSurfaceOutputScale() * 120;
    if (s120 == scale120_)
        return;
    scale120_ = s120;
    applySize(logicalW_, logicalH_, configured_);
    // Widgets laddar om fonten i nya skalan (samma väg som fractional).
    Event e;
    e.type     = EventType::WindowDisplayScaleChanged;
    e.windowId = windowId_;
    pushEvent(e);
}

// SKÄRMENS mm — se headern. Panelens px mot dess mm,
// och bufferten ANTAS lika tät som panelen: med fractional-scale ritar vi i
// outputens egen skala, och i heltalsvägen hos en fraktionell kompositör
// (bufferten större än panelen och nedskalad) går förhållandet inte att
// veta. Id:t är märke och modell ur geometry.
void WaylandWindow::measureDisplay(bool announce)
{
    const wl_output* out = !surfaceOutputs_.empty() ? surfaceOutputs_.front()
                                                    : display_->onlyOutput();
    const WaylandDisplay::OutputMetrics* om =
        out ? display_->outputMetrics(out) : nullptr;
    DisplayMetrics m;
    if (!om) {
        m = display::assumed("", "ytan visas ännu inte på någon känd output");
    } else {
        std::string id = om->make;
        if (!om->model.empty())
            id += (id.empty() ? "" : " ") + om->model;
        m = display::measure(om->pxW, om->pxH, om->mmW, om->mmH,
                             id.empty() ? "wayland" : id);
    }
    const bool ändrad = m.pxPerMm != metrics_.pxPerMm ||
                        m.source != metrics_.source || m.id != metrics_.id;
    metrics_ = std::move(m);
    if (announce && ändrad && windowId_ != 0) {
        Event e;
        e.type     = EventType::WindowDisplayChanged;
        e.windowId = windowId_;
        pushEvent(e);
    }
}

void WaylandWindow::applySize(int logicalW, int logicalH, bool notify)
{
    logicalW_ = std::max(1, logicalW);
    logicalH_ = std::max(1, logicalH);
    physW_    = std::max(1, physFromLogical(logicalW_));
    physH_    = std::max(1, physFromLogical(logicalH_));
    // STORLEKSMARGINALEN (E2L) ur fönstrets läge — i ytenheter och i
    // buffertens px. Med en heltalsskala s är bufferten (b + 2m) × s exakt,
    // alltså delbar med buffer_scale som protokollet kräver.
    marginSurf_  = wantedMarginSurf();
    frameMargin_ = marginSurf_ > 0 ? physFromLogical(marginSurf_) : 0;
    const int ytaW = logicalW_ + 2 * marginSurf_;   // ytenheter
    const int ytaH = logicalH_ + 2 * marginSurf_;

    if (eglWindow_)
        wl_egl_window_resize(eglWindow_, physW_ + 2 * frameMargin_,
                             physH_ + 2 * frameMargin_, 0, 0);
    renderer_.setFrameMargin(frameMargin_);   // lagras också före init
    if (renderer_.ready())
        renderer_.resize(physW_, physH_);

    if (viewport_ && fractionalActive_ && scale120_ % 120 != 0) {
        // Fractional skala: buffer i fysiska px + viewport-destination i
        // logiska ytenheter (protokollets recept) — hela ytan, marginalen
        // inräknad.
        wp_viewport_set_destination(viewport_, ytaW, ytaH);
        wl_surface_set_buffer_scale(surface_, 1);
    } else {
        // Heltalsskala: klassisk buffer_scale (viewport-destination av).
        if (viewport_)
            wp_viewport_set_destination(viewport_, -1, -1);
        wl_surface_set_buffer_scale(surface_,
                                    std::max(1, scale120_ / 120));
    }
    // FÖNSTRETS GEOMETRI (E2L): klientytan, marginalen in från ytans hörn.
    // Dubbelbuffrad som resten ovan — den gäller med nästa commit, alltså
    // med bufferten i den nya storleken. En popup har ingen marginal och
    // ingen geometri att sätta (förvalet är hela ytan); en toplevel sätter
    // den alltid, också till (0, 0, b, h): satt en gång går den inte att ta
    // bort, bara att ersätta.
    if (toplevel_)
        xdg_surface_set_window_geometry(xdgSurface_, marginSurf_, marginSurf_,
                                        logicalW_, logicalH_);
    updateOpaqueRegion();

    if (notify) {
        Event e;
        e.windowId = windowId_;
        e.type     = EventType::WindowResized;
        pushEvent(e);
        e.type = EventType::WindowPixelSizeChanged;
        pushEvent(e);
    }
}

void WaylandWindow::updateOpaqueRegion()
{
    if (!surface_)
        return;
    if (transparent_) {
        wl_surface_set_opaque_region(surface_, nullptr);
        return;
    }
    // Opaka fönster: klientytan täckande — perf-vinst för kompositören och
    // döljer premultiplikations-misstag. Bara
    // klientytan: storleksmarginalen runt den är genomskinlig.
    wl_region* region = wl_compositor_create_region(display_->compositor());
    wl_region_add(region, marginSurf_, marginSurf_, logicalW_, logicalH_);
    wl_surface_set_opaque_region(surface_, region);
    wl_region_destroy(region);
}

// STORLEKSMARGINALEN (E2L) — se headern. Fast storlek är samma regel som
// `AppWindow::isFixedSize` (båda axlarna låsta, min == max > 0).
int WaylandWindow::wantedMarginSurf() const
{
    if (!toplevel_ || chrome_ != Chrome::Custom || maximized_ || fullscreen_ ||
        tiled_)
        return 0;
    if (limMinW_ > 0 && limMinW_ == limMaxW_ && limMinH_ > 0 &&
        limMinH_ == limMaxH_)
        return 0;
    return surfaceUnitsFromLogical(frame::kMarginLogical);
}

// ---------------------------------------------------------------- fasaden

float WaylandWindow::uiScale() const
{
    // Monitorskala × portalens textskalning — samma
    // faktor som GTK-appar visar.
    // × användarskalan: ytenheterna bär text × user
    // (surfaceUnitsFromLogical), bufferten monitorskalan — produkten här
    // är alltså exakt fysiska px per logisk px, faktorn EN gång.
    return (scale120_ / 120.0f) * textScaleFactor() * userScale();
}

void WaylandWindow::raise()
{
    if (!surface_ || !visible_ || !display_->activation())   // dolt förblir dolt
        return;
    // Ett noActivate-fönster lyfts inte:
    // xdg-shell har ingen omstapling utan aktivering, och en aktivering hade
    // tagit tangentbordet. Dokumenterad degradering.
    if (noActivate_)
        return;
    // Activation kan nekas av kompositören (kräver färsk serial) — bästa
    // försök, precis som fasaden dokumenterar.
    auto* token = xdg_activation_v1_get_activation_token(
        display_->activation());
    auto* req   = new ActivationRequest{display_, surface_, token};
    display_->trackActivation(req);
    if (display_->lastInputSerial() != 0)
        xdg_activation_token_v1_set_serial(token,
                                           display_->lastInputSerial(),
                                           display_->seat());
    xdg_activation_token_v1_set_surface(token, surface_);
    xdg_activation_token_v1_add_listener(token, &kActivationTokenListener,
                                         req);
    xdg_activation_token_v1_commit(token);
    display_->flushDisplay();
}

void WaylandWindow::minimize()
{
    // FÖRE DEN FÖRSTA VISNINGEN är det startläget —
    // och ett minimerat startläge KAN INTE (`applyStartState`).
    if (firstMapPending_) {
        static bool loggat = false;
        if (!loggat) {
            loggat = true;
            log("wayland: minimerad start ignoreras — xdg-shell kan varken "
                "fråga efter eller ta bort minimering; fönstret startar i "
                "normalläge");
        }
        return;
    }
    // ENKELRIKTAT på Wayland: det finns varken query eller programmatisk
    // av-minimering (restore degraderar) — dokumenterad skillnad.
    if (toplevel_)
        xdg_toplevel_set_minimized(toplevel_);
}

// ETT ÄNDRAT STARTLÄGE före den första visningen: begäran kommittas och svaret VÄNTAS IN, så att den första bufferten
// får den nya storleken — annars hade fönstret dykt upp i normalläge och sedan
// maximerats (hoppet `onLoad` före synligheten finns för att undvika). Ytan är
// omappad, så en buffertlös commit är tillåten. Kommer ingen configure inom
// tidsgränsen står ytan kvar som konfigurerad — den mappas då i det läge den
// har, hellre än aldrig.
void WaylandWindow::awaitStateBeforeMap()
{
    wl_surface_commit(surface_);
    const bool förut = configured_;
    configured_      = false;
    if (!waitConfigured()) {
        configured_ = förut;
        log("wayland: startläget före den första visningen fick ingen "
            "configure — fönstret visas i sitt nuvarande läge");
    }
}

// STARTLÄGET. EN gång, före den commit som mappar ytan.
// Maximerat är `xdg_toplevel_set_maximized`, och svaret kommer i configuren.
// Minimerat KAN INTE: xdg-shell kan varken fråga efter eller ta bort
// minimering, och ett fönster som startade minimerat utan att appen kan se
// det vore värre än ett i normalläge — det loggas en gång och startar
// normalt. En popup har ingen toplevel och inget startläge.
void WaylandWindow::applyStartState()
{
    if (startStateApplied_ || !toplevel_)
        return;
    startStateApplied_ = true;
    if (startState_ == WindowState::Maximized) {
        xdg_toplevel_set_maximized(toplevel_);
    } else if (startState_ == WindowState::Minimized) {
        static bool loggat = false;
        if (!loggat) {
            loggat = true;
            log("wayland: minimerad start ignoreras — xdg-shell kan varken "
                "fråga efter eller ta bort minimering; fönstret startar i "
                "normalläge");
        }
    }
}

// FÖNSTRETS GEOMETRI — den DOKUMENTERADE
// degraderingen: ingen skärmrymd, och läget är bara normalt eller maximerat.
WindowPlacement WaylandWindow::placement() const
{
    WindowPlacement p;
    if (!surface_)
        return p;
    p.width  = descW_;   // desc-logiska px; står still under maximeringen
    p.height = descH_;
    p.state  = maximized_ ? WindowState::Maximized : WindowState::Normal;
    return p;            // hasPosition false
}

void WaylandWindow::maximize()
{
    if (toplevel_ && !maximized_) {
        xdg_toplevel_set_maximized(toplevel_);
        if (firstMapPending_)
            awaitStateBeforeMap();
    }
}

void WaylandWindow::restore()
{
    if (!toplevel_)
        return;
    const bool var = maximized_;
    xdg_toplevel_unset_maximized(toplevel_);
    if (firstMapPending_ && var)
        awaitStateBeforeMap();
}

void WaylandWindow::setSize(int w, int h)
{
    if (!surface_ || w <= 0 || h <= 0)
        return;
    // Logiska px (fasadändringen) → ytenheter.
    descW_ = w;
    descH_ = h;
    applySize(surfaceUnitsFromLogical(w), surfaceUnitsFromLogical(h),
              true);
}

// STORLEKSGRÄNSERNA. `xdg_toplevel_set_min_size`/`set_max_size`.
// Enheten är YTENHETER (samma som configure-storlekarna), så
// logiska px måste räknas om — och därför måste de återappliceras när
// textskalningen eller userScale ändras (`applySizeLimits` nedan).
//
// 0 = obegränsat, och protokollet stavar det på samma sätt (0 = ingen
// gräns) — så noll behöver ingen särbehandling.
void WaylandWindow::setSizeLimits(int minW, int minH, int maxW, int maxH)
{
    limMinW_ = minW;
    limMinH_ = minH;
    limMaxW_ = maxW;
    limMaxH_ = maxH;
    // Fast ↔ ändringsbart tar bort eller lägger till storleksmarginalen.
    // Klientytan är densamma — bara ytan och geometrin runt den
    // ändras — och händelserna ber appen rita om i den nya bufferten. Då
    // committas gränserna med den framen och inte här: en egen commit nu
    // hade lagt den nya geometrin över den GAMLA bufferten en bildruta.
    const bool marginBytt =
        surface_ && configured_ && wantedMarginSurf() != marginSurf_;
    if (marginBytt)
        applySize(logicalW_, logicalH_, true);
    applySizeLimits(!marginBytt);
}

// ALLTID ÖVERST — den DOKUMENTERADE degraderingen. Loggen
// går bara vid `on == true`: det är BEGÄRAN som inte kan uppfyllas, och en
// rad varje gång någon stänger av något som aldrig var på vore brus.
// `static` flagga per process — en app som sätter attributet i varje
// fönster ska inte fylla stderr.
void WaylandWindow::setTopmost(bool on)
{
    static bool loggat = false;
    if (on && !loggat) {
        loggat = true;
        log("wayland: setTopmost(true) ignoreras — xdg-shell saknar "
            "'alltid överst' för toplevels (kompositörens beslut); "
            "topmost() svarar false");
    }
}

// ---------------------------------------------------------------- programikonen
//
// PROGRAMIKONEN — `xdg-toplevel-icon-v1`. Varje
// KVADRATISK giltig ram blir en wl_shm-buffert (protokollet kräver kvadrat
// och wl_shm, annars `invalid_buffer` — ett protokollfel som river
// anslutningen) i förmultiplicerad ARGB8888, Waylands alfaform, ur EN
// memfd-pool, och läggs till ikonen med skala 1. Poolen, mappningen och fd:n
// släpps direkt — buffertarna håller minnet hos kompositören — medan ikonen
// och buffertarna lever tills nästa ikon är satt (protokollets krav:
// buffertarna så länge ikonobjektet finns). `set_icon` är dubbelbuffrad och
// gäller från nästa commit: här efter configure-handskakningen, annars
// init:s första commit (aldrig en egen commit före första ack_configure).

void WaylandWindow::setIcon(const WindowIcon& icon)
{
    icon_ = icon;
    applyIcon();   // före init: lagrat, init skickar det med första committen
}

void WaylandWindow::releaseIcon()
{
    if (iconObj_) {
        xdg_toplevel_icon_v1_destroy(iconObj_);
        iconObj_ = nullptr;
    }
    for (wl_buffer* b : iconBuffers_)
        wl_buffer_destroy(b);
    iconBuffers_.clear();
}

void WaylandWindow::applyIcon()
{
    if (!toplevel_) {
        iconApplied_ = false;   // popup, eller fönstret finns inte än
        return;
    }
    xdg_toplevel_icon_manager_v1* mgr = display_->iconManager();
    if (!mgr) {
        // DEGRADERINGEN (fasadens tabell): en rad per process, bara när det
        // finns en ikon som inte kan visas.
        static bool loggat = false;
        if (!icon_.images.empty() && !loggat) {
            loggat = true;
            log("wayland: kompositören saknar xdg-toplevel-icon-v1 — "
                "fönstrets ikon kommer ur appens .desktop-fil via app_id "
                "'%s'; iconApplied() svarar false",
                appId().c_str());
        }
        iconApplied_ = false;
        return;
    }

    std::vector<const IconImage*> ramar;
    std::size_t                   total = 0;
    for (const IconImage& im : icon_.images)
        if (iconImageValid(im) && im.width == im.height) {
            ramar.push_back(&im);
            total += im.rgba.size();
        }

    xdg_toplevel_icon_v1*   nyIkon = nullptr;
    std::vector<wl_buffer*> nyaBuffertar;
    if (!ramar.empty() && display_->shm() &&
        total <= static_cast<std::size_t>(INT32_MAX)) {
        const int fd = ::memfd_create("glaze-icon", MFD_CLOEXEC);
        void*     map = MAP_FAILED;
        if (fd >= 0 && ::ftruncate(fd, static_cast<off_t>(total)) == 0)
            map = ::mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED,
                         fd, 0);
        if (map != MAP_FAILED) {
            // RAK RGBA → förmultiplicerad ARGB8888, i little-endian-minne
            // alltså B, G, R, A.
            auto* dst = static_cast<std::uint8_t*>(map);
            for (const IconImage* im : ramar) {
                const std::uint8_t* src = im->rgba.data();
                const std::size_t   n   = im->rgba.size() / 4;
                for (std::size_t i = 0; i < n; ++i, src += 4, dst += 4) {
                    const unsigned a = src[3];
                    dst[0] = static_cast<std::uint8_t>((src[2] * a + 127u) / 255u);
                    dst[1] = static_cast<std::uint8_t>((src[1] * a + 127u) / 255u);
                    dst[2] = static_cast<std::uint8_t>((src[0] * a + 127u) / 255u);
                    dst[3] = static_cast<std::uint8_t>(a);
                }
            }
            ::munmap(map, total);
            wl_shm_pool* pool = wl_shm_create_pool(
                display_->shm(), fd, static_cast<std::int32_t>(total));
            std::size_t offset = 0;
            for (const IconImage* im : ramar) {
                nyaBuffertar.push_back(wl_shm_pool_create_buffer(
                    pool, static_cast<std::int32_t>(offset), im->width,
                    im->height, im->width * 4, WL_SHM_FORMAT_ARGB8888));
                offset += im->rgba.size();
            }
            wl_shm_pool_destroy(pool);
        }
        if (fd >= 0)
            ::close(fd);
        if (nyaBuffertar.empty()) {
            log("wayland: programikonens buffertar kunde inte skapas "
                "(memfd/mmap)");
        } else {
            nyIkon = xdg_toplevel_icon_manager_v1_create_icon(mgr);
            for (wl_buffer* b : nyaBuffertar)
                xdg_toplevel_icon_v1_add_buffer(nyIkon, b, 1);
        }
    } else if (!icon_.images.empty()) {
        log("wayland: ikonen har ingen giltig KVADRATISK ram (eller wl_shm "
            "saknas) — kompositörens förval visas");
    }

    // nullptr = kompositörens förval (appens .desktop-ikon): en tom lista
    // eller ett bygge som inte gick. Den nya sätts FÖRE den gamla rivs.
    xdg_toplevel_icon_manager_v1_set_icon(mgr, toplevel_, nyIkon);
    if (surface_ && configured_)
        wl_surface_commit(surface_);
    releaseIcon();
    iconObj_     = nyIkon;
    iconBuffers_ = std::move(nyaBuffertar);
    iconApplied_ = nyIkon != nullptr;
    display_->flushDisplay();
}

void WaylandWindow::applySizeLimits(bool commit)
{
    if (!toplevel_)
        return;   // popup/ännu inte skapat — kommer via nästa anrop
    auto units = [this](int logical) {
        return logical > 0 ? surfaceUnitsFromLogical(logical) : 0;
    };
    // Ytenheter i FÖNSTRETS GEOMETRI — klientytan, storleksmarginalen (E2L)
    // ingår inte (xdg-shell: "in window geometry coordinates").
    xdg_toplevel_set_min_size(toplevel_, units(limMinW_), units(limMinH_));
    xdg_toplevel_set_max_size(toplevel_, units(limMaxW_), units(limMaxH_));
    // Committa BARA efter configure-handskakningen. Under init är det här
    // anropet en del av den första uppsättningen state, och den commitas
    // ändå av init-sekvensen — en extra commit där skulle lägga sig FÖRE
    // första ack_configure, precis den ordning som är protokollfel
    // (fallgropslistan i rot-CLAUDE.md). `commit` false: en ny buffert är
    // på väg (setSizeLimits bytte marginalen) och tar gränserna med sig.
    if (commit && surface_ && configured_)
        wl_surface_commit(surface_);
}

void WaylandWindow::setTitle(const std::string& t)
{
    title_ = t;
    if (toplevel_)
        xdg_toplevel_set_title(toplevel_, title_.c_str());
}

void WaylandWindow::setVisible(bool on)
{
    if (!surface_ || visible_ == on)
        return;
    visible_ = on;
    if (!on) {
        // NULL-buffer + commit = unmap (xdg-shells dölj-väg).
        wl_surface_attach(surface_, nullptr, 0, 0);
        wl_surface_commit(surface_);
        display_->flushDisplay();
        return;
    }
    // DEN FÖRSTA VISNINGEN av en yta som skapades dold:
    // init:s initiala commit har fått sin configure och ackats, och
    // ingen buffert har kommittats — ytan är redo att mappas, och nästa ritning
    // gör det med sin första buffert (`shouldSwap`). En andra buffertlös
    // commit hade inte gett någon ny configure hos en kompositör som skickar
    // den första en gång, och fönstret hade stått osynligt efter tidsgränsen.
    // Startläget gick med init:s commit, och ett ändrat läge före visningen
    // med `maximize()`/`restore()`.
    if (firstMapPending_ && configured_) {
        firstMapPending_ = false;
        hidden_          = false;
        Event e;
        e.type     = EventType::WindowShown;
        e.windowId = windowId_;
        pushEvent(e);
        return;
    }
    // Åter-visning = ny initial-commit-runda (configure → ack → buffer):
    // `hide()` kommittade en null-buffert, och en omappad yta måste göra den
    // initiala committen igen (xdg-shell).
    firstMapPending_ = false;
    hidden_          = false;
    configured_      = false;
    applyStartState();
    wl_surface_commit(surface_);
    if (!waitConfigured()) {
        // INGEN NY CONFIGURE — men ytan HAR konfigurerats och ackats förut
        // (init kräver det), och samma xdg_surface lever. En kompositör som
        // följer protokollet svarar på den nya initiala committen; en som inte
        // gör det (WSLg:s Weston) har inte heller glömt den
        // förra configuren, och tar en buffert på den. Ytan står därför kvar
        // som konfigurerad, som i `awaitStateBeforeMap`: den mappas med nästa
        // ritning hellre än aldrig. Annars blev fönstret dolt för gott — varje
        // `hide()` + `show()` tog bort det från skärmen. EN loggrad per
        // process.
        configured_ = true;
        static bool loggat = false;
        if (!loggat) {
            loggat = true;
            log("wayland: åter-visningen fick ingen configure — den förra "
                "gäller, ytan mappas med nästa ritning");
        }
    }
    Event e;
    e.type     = EventType::WindowShown;
    e.windowId = windowId_;
    pushEvent(e);
}

void WaylandWindow::setPosition(int, int)
{
    // Protokollet saknar vägen (kompositören äger placeringen) —
    // dokumenterad no-op. Popups går via popup-kontraktet i stället.
    log("setPosition: no-op på Wayland (protokollet saknar "
        "skärmpositioner)");
}

Pointf WaylandWindow::clientToScreen(float px, float py) const
{
    // Degraderad (dokumenterat): ingen skärmrymd finns — klient-px/skala.
    const float s = scale120_ / 120.0f;
    return Pointf{px / s, py / s};
}

// ---------------------------------------------------------------- pacing

struct WaylandFrameDone {
    static void done(void* data, wl_callback* cb, std::uint32_t)
    {
        auto* w = static_cast<WaylandWindow*>(data);
        wl_callback_destroy(cb);
        if (w->frameCb_ == cb)
            w->frameCb_ = nullptr;
    }
};

namespace {
const wl_callback_listener kFrameListener = {&WaylandFrameDone::done};
} // namespace

void WaylandWindow::beforeSwap()
{
    // Vänta in förra swappens frame-callback, max ~en vsync-period —
    // synligt fönster paceas till skärmens takt, skymt slår i taket.
    if (frameCb_) {
        constexpr int kMaxWaitMs = 16;
        const std::uint64_t deadline = ticksMs() + kMaxWaitMs;
        wl_display*     dpy = display_->display();
        wl_event_queue* q   = display_->frameQueue();
        while (frameCb_) {
            const std::uint64_t now = ticksMs();
            if (now >= deadline)
                break;
            // Läsdansen mot BARA frame-kön. Vi står mitt i en frame —
            // innehållet är ritat och compositat, bara swappen återstår —
            // så ingen ANNAN listener får köra här: en configure som
            // dispatchades i det här läget resizade EGL-ytan under den
            // färdigritade framen, som därefter committades i fel storlek.
            // Övriga events läses in i sina köer och dispatchas av pumpen
            // före nästa frame ritas.
            // VARJE utgång härifrån måste släppa läslåset: lyckad
            // prepare_read TAR det, och libwayland låter nästa
            // wl_display_read_events vänta på en läsare som aldrig kommer
            // (enkeltrådat = evig väntan).
            bool prepared = true;
            while (wl_display_prepare_read_queue(dpy, q) != 0) {
                if (wl_display_dispatch_queue_pending(dpy, q) < 0) {
                    prepared = false;   // inget lås taget
                    break;
                }
            }
            if (!prepared)
                break;
            if (!frameCb_) {
                // Callbacken kom i dispatchen ovan — men prepare_read
                // hann lyckas efteråt, så låset ÄR taget.
                wl_display_cancel_read(dpy);
                break;
            }

            // OBS: bara POLLIN. Att även vänta på POLLOUT vid EAGAIN
            // (full skrivbuffert) är den lärobokskorrekta hanteringen,
            // men här läses fd:n under ett taget läslås — en poll som
            // vaknar på SKRIVBAR men inte LÄSBAR fd leder in i
            // wl_display_read_events utan data, och mätriggen hängde sig
            // på det. Vinsten var latens i ett extremfall, priset en
            // hängning: inte värt det. Timeouten nedan är skyddet.
            pollfd pf{wl_display_get_fd(dpy), POLLIN, 0};
            wl_display_flush(dpy);
            if (::poll(&pf, 1, static_cast<int>(deadline - now)) <= 0) {
                wl_display_cancel_read(dpy);   // läslåset MÅSTE släppas
                break;
            }
            if (wl_display_read_events(dpy) < 0)
                break;
            if (wl_display_dispatch_queue_pending(dpy, q) < 0)
                break;
        }
        if (frameCb_) {
            wl_callback_destroy(frameCb_);   // skymd — släpp och kör
            frameCb_ = nullptr;
        }
    }
    // Callbacken skapas via en proxy-WRAPPER så den ärver frame-kön i
    // stället för default-kön — det är den kön beforeSwap väntar på ovan.
    // Wrappern är bara en kö-etikett: den destrueras direkt och rör inte
    // den riktiga ytan.
    wl_surface* wrapped =
        static_cast<wl_surface*>(wl_proxy_create_wrapper(surface_));
    if (wrapped) {
        wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapped),
                           display_->frameQueue());
        frameCb_ = wl_surface_frame(wrapped);
        wl_proxy_wrapper_destroy(wrapped);
    } else {
        // OOM. Callbacken hamnar i default-kön, som beforeSwap INTE
        // dispatchar — pacingen degraderar då till 16 ms-taket per frame.
        // Correctness håller (configure kan fortfarande inte köra mitt i
        // en frame); det är takten som blir sämre.
        frameCb_ = wl_surface_frame(surface_);
    }
    if (frameCb_)
        wl_callback_add_listener(frameCb_, &kFrameListener, this);
    // eglSwapBuffers committar ytan — callbacken följer med committen.
}

// ---------------------------------------------------------------- hit-test

// Punkten i KLIENTENS fysiska px (`clientX/Y`). Ett tryck i storleks-
// marginalen (E2L) ger marginalens kant ur `hitRoleAt` — utanför klientytan,
// där kartan inte har några zoner — och kartans resize-zoner hoppas över
// medan marginalen finns.
bool WaylandWindow::interceptPress(float px, float py,
                                   std::uint32_t serial)
{
    if (chrome_ != Chrome::Custom || !toplevel_)
        return false;
    const HitRole role = hitRoleAt(px, py);
    switch (role) {
    case HitRole::Caption:
        xdg_toplevel_move(toplevel_, display_->seat(), serial);
        return true;
    case HitRole::ResizeLeft:
    case HitRole::ResizeRight:
    case HitRole::ResizeTop:
    case HitRole::ResizeBottom:
    case HitRole::ResizeTopLeft:
    case HitRole::ResizeTopRight:
    case HitRole::ResizeBottomLeft:
    case HitRole::ResizeBottomRight: {
        std::uint32_t edges = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
        switch (role) {
        case HitRole::ResizeLeft:
            edges = XDG_TOPLEVEL_RESIZE_EDGE_LEFT; break;
        case HitRole::ResizeRight:
            edges = XDG_TOPLEVEL_RESIZE_EDGE_RIGHT; break;
        case HitRole::ResizeTop:
            edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP; break;
        case HitRole::ResizeBottom:
            edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM; break;
        case HitRole::ResizeTopLeft:
            edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT; break;
        case HitRole::ResizeTopRight:
            edges = XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT; break;
        case HitRole::ResizeBottomLeft:
            edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT; break;
        case HitRole::ResizeBottomRight:
            edges = XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT; break;
        default: break;
        }
        xdg_toplevel_resize(toplevel_, display_->seat(), serial, edges);
        return true;
    }
    default:
        return false;
    }
}

} // namespace gfx
