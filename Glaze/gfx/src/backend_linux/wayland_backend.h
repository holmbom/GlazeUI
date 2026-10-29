#pragma once
// Wayland-sidan av Linux-backenden — intern header, får BARA inkluderas av
// wayland_display.cpp och wayland_window.cpp (drar in wayland-client och
// protokoll-headrarna; resten av backenden är protokollneutral via
// linux_backend.h).

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <wayland-egl.h>

#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon.h>

#include "protocols/fractional-scale-v1-client-protocol.h"
#include "protocols/pointer-gestures-unstable-v1-client-protocol.h"
#include "protocols/viewporter-client-protocol.h"
#include "protocols/xdg-activation-v1-client-protocol.h"
#include "protocols/xdg-decoration-unstable-v1-client-protocol.h"
#include "protocols/xdg-shell-client-protocol.h"
#include "protocols/xdg-toplevel-icon-v1-client-protocol.h"

#include "linux_backend.h"

namespace gfx {

class WaylandWindow;
class WaylandDisplay;

// xdg-activation: en utestående token-request (raise). SPÅRAS i
// WaylandDisplay: target-fönstret kan stängas innan kompositörens done
// kommer (aktivering av förstörd proxy), och svarar kompositören aldrig
// får token + request inte läcka — windowDestroyed/dtorn städar dem.
struct ActivationRequest {
    WaylandDisplay*          display = nullptr;
    wl_surface*              target  = nullptr;
    xdg_activation_token_v1* token   = nullptr;
};

// ------------------------------------------------------- WaylandDisplay

class WaylandDisplay final : public LinuxDisplay {
public:
    // nullptr när anslutningen/obligatoriska globals (wl_compositor,
    // xdg_wm_base, wl_seat) saknas — init faller vidare till X11.
    static WaylandDisplay* create();
    ~WaylandDisplay() override;

    EGLDisplay egl() const override { return egl_; }
    std::unique_ptr<Window> makeWindow() override;

    // Pump-krokarna: den korrekta läsdansen wl_display_prepare_read →
    // flush → epoll → read_events/cancel_read → dispatch_pending.
    void prepoll() override;
    void postpoll(bool fdReady) override;
    void abortPoll() override;
    int  fd() const override { return wl_display_get_fd(display_); }

    std::string clipboardText() override;
    void setClipboardText(const std::string& utf8) override;

    // Åtkomst för WaylandWindow.
    wl_display*    display() const { return display_; }

    // Frame-callbackens EGNA eventkö. Pacingen i
    // WaylandWindow::beforeSwap väntar mitt i en frame — mellan composite
    // och eglSwapBuffers — och får därför ALDRIG köra godtyckliga
    // listeners: en xdg_surface.configure som dispatchades där resizade
    // EGL-ytan under fötterna på den redan ritade framen, som sedan
    // committades i FEL storlek (spökbilderna vid kantdrag). Med
    // callbacken i en egen kö dispatchar beforeSwap bara den; configure
    // och input ligger kvar i default-kön och körs av pumpens läsdans,
    // före nästa frame ritas.
    wl_event_queue* frameQueue() const { return frameQueue_; }
    wl_compositor* compositor() const { return compositor_; }
    xdg_wm_base*   wmBase() const { return wmBase_; }
    wl_seat*       seat() const { return seat_; }
    zxdg_decoration_manager_v1* decorationManager() const
    {
        return decorationManager_;
    }
    wp_fractional_scale_manager_v1* fractionalScaleManager() const
    {
        return fractionalManager_;
    }
    wp_viewporter*     viewporter() const { return viewporter_; }
    xdg_activation_v1* activation() const { return activation_; }
    // PROGRAMIKONEN: ikonernas buffertar och protokollet —
    // nullptr när kompositören inte annonserar globalen.
    wl_shm*                       shm() const { return shm_; }
    xdg_toplevel_icon_manager_v1* iconManager() const { return iconManager_; }
    EGLConfig          eglConfig() const { return eglConfig_; }
    int                maxOutputScale() const;   // heltalsskala, minst 1
    // Skalan för EN output (per-surface-spårningen); 0 = okänd/borttagen.
    int                outputScaleOf(const wl_output* o) const;

    // SKÄRMENS mm: en outputs mått ur wl_output —
    // `geometry` (millimeter, 0 = okänt enligt protokollet, och märke/
    // modell) och `mode` (den AKTUELLA upplösningen i px). Uppdateras av
    // events och gäller från `done`, då fönstren på outputen mäter om.
    struct OutputMetrics {
        int         mmW = 0, mmH = 0;
        int         pxW = 0, pxH = 0;
        std::string make, model;
    };
    // Mätningen för EN output; nullptr = okänd/borttagen. `onlyOutput` är
    // displayens ENDA output, eller nullptr när de är fler eller ingen.
    const OutputMetrics* outputMetrics(const wl_output* o) const;
    const wl_output*     onlyOutput() const
    {
        return outputs_.size() == 1 ? outputs_.front().output : nullptr;
    }
    std::uint32_t      lastInputSerial() const { return lastInputSerial_; }
    std::uint32_t      lastButtonSerial() const { return lastButtonSerial_; }
    // Serial som duger till xdg_popup.grab: satt av ett knappTRYCK, ännu
    // inte släppt (den implicita grabben lever), och tillhör det fönster
    // popupen hänger under. Allt annat är protokollfel `invalid_grab` —
    // och det river displayen, dvs. dödar appen. 0 = grabba inte.
    std::uint32_t      grabSerialFor(const WaylandWindow* parent) const
    {
        return (grabSerial_ != 0 && grabWin_ == parent) ? grabSerial_ : 0;
    }

    void registerSurface(wl_surface* s, WaylandWindow* w);
    void unregisterSurface(wl_surface* s);
    void windowDestroyed(WaylandWindow* w);   // nolla fokus-/pekarreferenser

    // Activation-spårningen (se ActivationRequest ovan). untrack tar bort
    // requesten ur listan; false = redan städad (windowDestroyed hann
    // före) — done-callbacken får då INTE röra pekaren.
    void trackActivation(ActivationRequest* req);
    bool untrackActivation(ActivationRequest* req);

    // MUSPEKAREN — EN väg för båda källorna: över en resize-zon
    // i pekarfönstrets hit-test-karta visas zonens pil, annars den form
    // fönstret BEGÄRT (`Window::setCursor`). Sätts bara när det effektiva
    // valet ändrats efter förra gången (`appliedCursor_`); `enter` nollar
    // minnet, för en ny enter-serial kräver att pekaren sätts igen.
    void refreshCursor();

    void flushDisplay() { wl_display_flush(display_); }
    // Roundtrip dispatchar callbacks — uppskjuten fokusförlust måste
    // därför flushas efteråt (se flushPendingLeave).
    void roundtrip()
    {
        wl_display_roundtrip(display_);
        flushPendingLeave();
        flushPendingCaptureLost();
    }

private:
    WaylandDisplay() = default;
    bool connect();

    // Registry/globals.
    static void onGlobal(void* data, wl_registry* reg, std::uint32_t name,
                         const char* iface, std::uint32_t version);
    static void onGlobalRemove(void* data, wl_registry* reg,
                               std::uint32_t name);

    // Seat/input.
    void seatCapabilities(std::uint32_t caps);
    void keymapChanged(int fd, std::uint32_t size);
    void keyEvent(std::uint32_t serial, std::uint32_t key, bool pressed);
    void emitKey(std::uint32_t keycode, bool pressed, bool isRepeat);
    void modifiersChanged(std::uint32_t depressed, std::uint32_t latched,
                          std::uint32_t locked, std::uint32_t group);
    void pointerFrameFlush();
    // STYRPLATTANS TVÅFINGERSDRAG: pågår en precis sekvens får
    // dess fönster ett End (noll rader) — vid `axis_stop`, när pekaren
    // lämnar ytan och när pekaren försvinner ur sätet. No-op annars.
    void endPreciseScroll();
    // NYPNINGEN (zwp_pointer_gesture_pinch_v1): pinch-objektet skapas när
    // BÅDE globalen och pekaren finns, i den ordning de dyker upp.
    void ensurePinch();
    void pinchBegin(wl_surface* surface, std::uint32_t fingers);
    void pinchUpdate(double scale, double rotation);
    void pinchEnd(bool cancelled);
    void pushGesture(GesturePhase phase, float scale, float rotation);
    void armRepeat(std::uint32_t keycode);
    void disarmRepeat();

    WaylandWindow* windowFor(wl_surface* s) const;

    // Klippbordet (wl_data_device).
    void selectionOffered(wl_data_offer* offer);

    wl_display*     display_    = nullptr;
    wl_event_queue* frameQueue_ = nullptr;   // se frameQueue()
    wl_registry*    registry_   = nullptr;
    wl_compositor* compositor_ = nullptr;
    xdg_wm_base*   wmBase_     = nullptr;
    wl_seat*       seat_       = nullptr;
    wl_shm*        shm_        = nullptr;
    wl_data_device_manager*         dataDeviceManager_ = nullptr;
    wl_data_device*                 dataDevice_        = nullptr;
    zxdg_decoration_manager_v1*     decorationManager_ = nullptr;
    wp_fractional_scale_manager_v1* fractionalManager_ = nullptr;
    wp_viewporter*                  viewporter_        = nullptr;
    xdg_activation_v1*              activation_        = nullptr;
    zwp_pointer_gestures_v1*        gestures_          = nullptr;
    xdg_toplevel_icon_manager_v1*   iconManager_       = nullptr;

    struct OutputInfo {
        wl_output*    output = nullptr;
        int           scale  = 1;
        std::uint32_t name   = 0;   // registry-namnet (global_remove-nyckeln)
        OutputMetrics metrics;      // skärmens mm
    };
    std::vector<OutputInfo> outputs_;

    // EGL.
    EGLDisplay egl_       = EGL_NO_DISPLAY;
    EGLConfig  eglConfig_ = nullptr;

    // Ytregistret (seat-events bär wl_surface, vi routar till fönstret).
    std::unordered_map<wl_surface*, WaylandWindow*> surfaces_;

    // Pekaren.
    wl_pointer*    pointer_        = nullptr;
    WaylandWindow* pointerWin_     = nullptr;
    double         pointerX_       = 0.0;   // logiska ytenheter
    double         pointerY_       = 0.0;
    std::uint32_t  pointerEnterSerial_ = 0;
    std::uint32_t  lastButtonSerial_   = 0;
    std::uint32_t  lastInputSerial_    = 0;
    // Den LEVANDE implicita pekargrabben (tryck → släpp) och fönstret den
    // hör till — enda serial som får skickas till xdg_popup.grab.
    std::uint32_t  grabSerial_ = 0;
    WaylandWindow* grabWin_    = nullptr;
    // TRYCKET SOM AVBRYTS (`EventType::MouseCaptureLost`).
    // En bit per `MouseButton` vars `MouseButtonDown` FAKTISKT skickats och
    // vars släpp inte kommit; ett tryck `interceptPress` tog (kompositörens
    // flytt och storleksändring) räknas inte. `leave` medan en bit står gör
    // fönstret till `pendingCaptureLost_` — utom när nästa `enter` i samma
    // dispatch-omgång är vår egen yta i SAMMA FOKUSGRUPP: en popup som trycket
    // öppnade och som tar pekaren (GTK:s undantag; protokollet säger inte att
    // en leave under tryck tappar släppet, wayland#280). Det som står kvar vid
    // omgångens slut skickas (`flushPendingCaptureLost`, vid prepoll, postpoll
    // och roundtrip — fokusförlustens platser).
    std::uint8_t   pushedButtons_      = 0;
    WaylandWindow* pendingCaptureLost_ = nullptr;
    void           flushPendingCaptureLost();
    // Hjul-ackumulatorer (axis → frame): value120 vinner över diskret som
    // vinner över kontinuerligt värde.
    double axisV_ = 0.0, axisH_ = 0.0;         // kontinuerligt (enheter)
    double v120V_ = 0.0, v120H_ = 0.0;         // value120 (1/120 klick)
    bool   sawV120_ = false;
    // STYRPLATTAN. `axis_source` gäller hela ramen (-1 = ingen
    // kom), `axis_stop` kommer per axel men avslutar EN sekvens. Källa
    // finger/continuous är det PRECISA hjulet; `preciseWin_` är fönstret
    // vars Begin gått ut utan ett End, och positionen dess End bär om
    // pekaren hunnit lämna det.
    int            axisSource_  = -1;
    bool           axisStop_    = false;
    WaylandWindow* preciseWin_  = nullptr;
    float          preciseX_    = 0.0f, preciseY_ = 0.0f;   // fysiska px
    // Nypningen: objektet (ett per pekare), fönstret en TVÅFINGERSnypning
    // började i (nullptr = ingen pågår, eller den hade fler fingrar) och den
    // ABSOLUTA skalan i förra eventet — protokollets skala räknas från
    // begin, kontraktets från förra eventet.
    zwp_pointer_gesture_pinch_v1* pinch_      = nullptr;
    WaylandWindow*                pinchWin_   = nullptr;
    double                        pinchScale_ = 1.0;
    bool   pendingMotion_ = false;
    HitRole lastCursorRole_ = HitRole::Client;
    // Senast SATTA pekarnamnet (`refreshCursor`) — nullptr = ingen satt
    // efter senaste enter. Namnen är strängliteraler ur tabellerna i
    // wayland_display.cpp, så pekarjämförelsen är en identitetsjämförelse.
    const char* appliedCursor_ = nullptr;
    void        applyCursorNamed(const char* const* names);

    // Cursortemat.
    wl_cursor_theme* cursorTheme_   = nullptr;
    wl_surface*      cursorSurface_ = nullptr;
    int              cursorScale_   = 1;

    // Tangentbordet.
    wl_keyboard*      keyboard_    = nullptr;
    WaylandWindow*    keyboardWin_ = nullptr;
    // Fokus-kontraktet (se maskningen i wayland_display.cpp): en
    // `wl_keyboard.leave` hålls inne tills nästa `enter` i SAMMA
    // dispatch-batch är känd — går fokus bara mellan ett fönster och dess
    // egen popup är det INGEN fokusändring utåt.
    WaylandWindow*    pendingLeave_    = nullptr;
    bool              hasPendingLeave_ = false;
    void flushPendingLeave();
    static bool sameFocusGroup(WaylandWindow* a, WaylandWindow* b);
    xkb_context*      xkb_         = nullptr;
    xkb_keymap*       keymap_      = nullptr;
    xkb_state*        xkbState_    = nullptr;
    xkb_compose_table* composeTable_ = nullptr;
    xkb_compose_state* composeState_ = nullptr;
    std::uint32_t     mods_        = 0;

    // Key-repeat är KLIENTENS jobb på Wayland (repeat_info + timerfd).
    std::int32_t  repeatRateHz_  = 25;
    std::int32_t  repeatDelayMs_ = 400;
    std::uint32_t repeatKeycode_ = 0;
    int           repeatFd_      = -1;
    PollSource    repeatSource_;

    // Utestående activation-requests (raise) — städas i windowDestroyed
    // när target-ytan dör och i dtorn när kompositören aldrig svarat.
    std::vector<ActivationRequest*> activationRequests_;

    // Klippbordet.
    wl_data_offer* selectionOffer_    = nullptr;
    bool           selectionHasUtf8_  = false;
    wl_data_source* ownSource_        = nullptr;
    std::string     ownClipboardText_;
    bool            weOwnSelection_   = false;

    bool prepared_ = false;   // prepare_read-låset hålls

    friend class WaylandWindow;
};

// -------------------------------------------------------- WaylandWindow

class WaylandWindow final : public LinuxWindow {
public:
    explicit WaylandWindow(WaylandDisplay* d) : display_(d) {}
    ~WaylandWindow() override { shutdown(); }

    bool init(const WindowDesc& desc) override;
    void shutdown() override;
    void raise() override;

    float uiScale() const override;
    bool  maximized() const override { return maximized_; }
    // FÖNSTRETS GEOMETRI — DEGRADERAD: ingen
    // skärmrymd (hasPosition false) och aldrig Minimized (xdg-shell kan inte
    // fråga efter minimering). Storleken är `descW_`/`descH_`, som står
    // still under maximeringen.
    WindowPlacement placement() const override;
    void  minimize() override;
    void  maximize() override;
    void  restore() override;

    void setSize(int w, int h) override;       // logiska px (fasadändringen)
    void setSizeLimits(int minW, int minH, int maxW,
                       int maxH) override;     // logiska px

    // ALLTID ÖVERST: KAN INTE på Wayland — xdg-shell har
    // inget "alltid överst" för vanliga toplevels; z-ordningen är
    // kompositörens och användarens beslut. No-op som LOGGAR en gång (bara
    // vid begäran `on == true` — att logga en avstängning vore brus), och
    // `topmost()` svarar alltid FALSE, så degraderingen är LÄSBAR för
    // appen i stället för tyst svald. Samma sort som setPosition/
    // screenArea/minimize redan är här.
    void setTopmost(bool on) override;
    bool topmost() const override { return false; }

    void setTitle(const std::string& t) override;
    void setVisible(bool on) override;

    // Wayland saknar skärmpositioner — dokumenterade degraderingar
    // (fasadändringen: popups går via popupAnchor-kontraktet i stället).
    void setPosition(int, int) override;
    Pointf clientToScreen(float px, float py) const override;
    Rectf screenArea() const override { return Rectf{0, 0, 0, 0}; }

    void releaseMouseCapture() override {}   // implicit grab per tryck —
                                             // kan inte släppas (som Metal)
    // …och inte tas över: grabben hör till ytan där
    // trycket började — false (fasadens tabell), och därför ingen capture att
    // fråga om heller.
    bool captureMouse() override { return false; }
    bool hasMouseCapture() const override { return false; }

    // MUSPEKAREN: lagras i basen, och står pekaren i DET HÄR
    // fönstret sätter displayen om den direkt (wl_pointer är sätets, inte
    // ytans — det är displayen som vet var pekaren står).
    void setCursor(PointerShape shape) override;

    // PROGRAMIKONEN: `xdg-toplevel-icon-v1` när kompositören
    // har den, annars den DOKUMENTERADE degraderingen — en loggrad (en gång)
    // och `iconApplied()` false; skrivbordet tar då ikonen ur appens
    // `.desktop` via app_id. Popups har ingen toplevel och ingen ikon.
    void setIcon(const WindowIcon& icon) override;
    bool iconApplied() const override { return iconApplied_; }

    // Display-krokar (listeners i wayland_window.cpp/display.cpp).
    void onToplevelConfigure(std::int32_t w, std::int32_t h,
                             wl_array* states);
    void onSurfaceConfigure(std::uint32_t serial);
    void onPopupConfigure(std::int32_t w, std::int32_t h);
    void onPopupDone();
    void onCloseRequested();
    void onPreferredScale(std::uint32_t scale120);
    void onOutputScaleMaybeChanged();
    void onTextScaleChanged() override;   // ytenheter ur descW_/descH_

    // SKÄRMENS mm. Fönstrets output är den äldsta av
    // de ytan står på (enter/leave), och före första enter displayens ENDA
    // — finns flera vet vi inte vilken, och svaret blir antaget. Mäts om vid
    // enter/leave och när outputens `done` kommer — inte vid skalbyte:
    // varken panelens mått eller reserven beror på skalan;
    // `announce` köar WindowDisplayChanged när svaret blev ett annat.
    DisplayMetrics displayMetrics() const override { return metrics_; }
    void           measureDisplay(bool announce);
    void userScaleChanged() override;     // US.2: samma väg + skal-event

    // Per-surface-outputspårningen (wl_surface enter/leave — blandad
    // DPI): fönstrets skala är max av DESS outputs, inte alla anslutna.
    void onSurfaceEnterOutput(wl_output* o);
    void onSurfaceLeaveOutput(wl_output* o);
    void outputRemoved(wl_output* o);   // global_remove: släpp + räkna om

    // Pekar-routing (display → fönster). Koordinater i fysiska px.
    // `toPhysical` räknar om en STRÄCKA (hjulets deltan); en POSITION går
    // genom `clientX/Y`, som också drar av storleksmarginalen (E2L) — ytans
    // origo ligger marginalen utanför klientytans.
    float toPhysical(double logical) const
    {
        return static_cast<float>(logical) * (scale120_ / 120.0f);
    }
    float clientX(double surfaceX) const
    {
        return toPhysical(surfaceX - marginSurf_);
    }
    float clientY(double surfaceY) const
    {
        return toPhysical(surfaceY - marginSurf_);
    }
    bool interceptPress(float px, float py, std::uint32_t serial);
    HitRole roleAt(float px, float py) const { return hitRoleAt(px, py); }

    wl_surface*  surface() const { return surface_; }
    xdg_surface* xdgSurface() const { return xdgSurface_; }

protected:
    bool canRender() const override
    {
        return renderer_.ready() && configured_;
    }
    bool shouldSwap() const override
    {
        return configured_ && !hidden_ && visible_;
    }
    // Pacing: eglSwapInterval är 0 (en vsync-blockerad swap på en skymd
    // yta hade hängt pumpen) — i stället väntas förra swappens
    // frame-callback in med TIDSTAK (~en vsync-period) före nästa swap.
    // Synlig yta ⇒ callbacken kommer på nästa vblank = vsync-takt; skymd
    // yta ⇒ taket slår till = begränsad takt, aldrig häng. Callbacken
    // används för pacing av en REDAN begärd present — aldrig som klocka
    // (hård regel 1: ritningen är händelsestyrd).
    void beforeSwap() override;
    Sizef pixelSize() const override
    {
        return Sizef{static_cast<float>(physW_),
                     static_cast<float>(physH_)};
    }

private:
    // Klientytans storlek i ytenheter → buffert, renderare, viewport och —
    // för en toplevel — fönstrets geometri. Räknar också om
    // storleksmarginalen (E2L) ur fönstrets läge, så varje väg hit (configure,
    // skalbyten, setSize, gränserna) får rätt marginal.
    void applySize(int logicalW, int logicalH, bool notify);
    // Skickar de LAGRADE gränserna i aktuella ytenheter —
    // anropas av setSizeLimits, efter toplevel-skapandet OCH vid varje
    // skalbyte (text/user), eftersom ytenheten då betyder något annat.
    // `commit` false när en ny buffert redan är på väg (marginalen
    // bytte) och tar gränserna med sig i sin commit.
    void applySizeLimits(bool commit = true);
    void updateOpaqueRegion();
    // STORLEKSMARGINALEN i YTENHETER för fönstrets
    // nuvarande läge: `frame::kMarginLogical` för en toplevel med eget chrome
    // som går att storleksändra, annars 0 — popup, Native/None, fast storlek
    // (min == max), maximerat, helskärm och tiled (åt något håll; GTK4 tar
    // bort skuggan där, och en marginal mot en grannes kant hade stulit
    // dennes pekare).
    int  wantedMarginSurf() const;
    int  maxSurfaceOutputScale() const;   // max av fönstrets outputs;
                                          // före första enter: displayens
    int  physFromLogical(int logical) const;        // ytenheter → buffert-px
    int  surfaceUnitsFromLogical(int logical) const;   // desc-logiska px →
                                                       // ytenheter (× text-
                                                       // skalning)
    bool waitConfigured();

    WaylandDisplay* display_ = nullptr;

    wl_surface*  surface_    = nullptr;
    xdg_surface* xdgSurface_ = nullptr;
    xdg_toplevel* toplevel_  = nullptr;
    xdg_popup*    popup_     = nullptr;
    xdg_positioner* positioner_ = nullptr;
    zxdg_toplevel_decoration_v1* decoration_ = nullptr;
    wp_fractional_scale_v1*      fractional_ = nullptr;
    wp_viewport*                 viewport_   = nullptr;

    wl_egl_window* eglWindow_  = nullptr;
    EGLSurface     eglSurface_ = EGL_NO_SURFACE;

    int  logicalW_ = 0, logicalH_ = 0;   // ytenheter (logiska px)
    // Beställd DESC-LOGISK storlek (fasadens logiska px, FÖRE text-
    // skalningen) — källan när ytenheterna räknas om vid textskalebyte
    // i drift. Uppdateras vid init/setSize OCH när användaren resizat
    // (onSurfaceConfigure), annars hoppar fönstret vid nästa skalbyte.
    int  descW_    = 0, descH_    = 0;
    // STORLEKSGRÄNSERNA i LOGISKA px, 0 = obegränsat.
    // Lagras för att kunna ÅTERAPPLICERAS: xdg_toplevel tar YTENHETER,
    // så ett byte av textskalning eller userScale gör de gamla talen fel.
    int  limMinW_ = 0, limMinH_ = 0, limMaxW_ = 0, limMaxH_ = 0;
    int  physW_    = 0, physH_    = 0;   // buffert (fysiska px)
    int  scale120_ = 120;                // monitorskala × 120
    bool fractionalActive_ = false;      // preferred_scale styr skalan

    // Outputs som ytan just nu visas på (wl_surface enter/leave) — bara
    // heltalsskale-fallbacken bryr sig (fractional vinner på GNOME ≥ 47) —
    // och skärmens mm, som mäter den äldsta av dem.
    std::vector<wl_output*> surfaceOutputs_;
    DisplayMetrics          metrics_;   // senaste mätningen (measureDisplay)

    int  pendingW_ = 0, pendingH_ = 0;   // ur toplevel-configure (logiska)
    bool pendingMaximized_  = false;
    bool pendingFullscreen_ = false;   // configurens läge, gäller vid ack
    bool pendingTiled_      = false;   // TILED_LEFT/RIGHT/TOP/BOTTOM (v2)

    bool configured_ = false;
    bool visible_    = true;
    bool maximized_  = false;
    bool fullscreen_ = false;
    bool tiled_      = false;
    bool isPopup_    = false;
    // `desc.noActivate` — permanent; `raise()` lyfter då inte (xdg-shell lyfter
    // bara genom aktivering).
    bool noActivate_ = false;
    // DEN FÖRSTA VISNINGEN VÄNTAR: ytan skapades dold,
    // init:s initiala commit fick sin configure, och ingen buffert har
    // kommittats än. Den första `setVisible(true)` behöver då ingen ny
    // configure-runda — nästa ritning mappar ytan. En återvisning efter
    // `hide()` (null-buffert ⇒ omappad) gör configure-rundan.
    bool firstMapPending_ = false;

    // STORLEKSMARGINALEN i YTENHETER (E2L; `frameMargin_` i basen är samma
    // marginal i buffertens px). Ytan är klientytan + 2 × marginalen,
    // `set_window_geometry(m, m, logicalW_, logicalH_)` säger kompositören
    // var fönstret är, och configurens storlek och `set_min/max_size` gäller
    // klientytan (fönstrets geometri).
    int  marginSurf_ = 0;

    // STARTLÄGET: `desc.startState`, tillämpat EN gång
    // före den commit som mappar ytan — init:s initiala commit, eller den i
    // första `setVisible(true)` efter en dold start (`applyStartState`).
    WindowState startState_        = WindowState::Normal;
    bool        startStateApplied_ = false;
    void        applyStartState();
    // Ett läge som ändrats före den första visningen (`maximize`/`restore`):
    // commit och vänta in configuren, så att den första
    // bufferten får rätt storlek.
    void        awaitStateBeforeMap();
    // Popupens ÄGARFÖNSTER (xdg_popup-föräldern). Bär inga koordinater —
    // den finns för fokus-kontraktet: en grabbad popup och dess ägare är
    // EN fokusgrupp utåt (se maskningen i wayland_display.cpp).
    WaylandWindow* popupParent_ = nullptr;

    wl_callback* frameCb_ = nullptr;   // förra swappens frame-callback

    // PROGRAMIKONEN. Ramarna lagras (tvåfas: init skickar dem
    // före den första committen) och den SATTA ikonen hålls vid liv med sina
    // buffertar: protokollet kräver buffertarna så länge ikonobjektet finns,
    // och objektet behålls tills nästa ikon är satt — då rivs det gamla.
    WindowIcon              icon_;
    bool                    iconApplied_ = false;
    xdg_toplevel_icon_v1*   iconObj_     = nullptr;
    std::vector<wl_buffer*> iconBuffers_;
    void applyIcon();
    void releaseIcon();   // rivs: ikonobjektet först, buffertarna sedan

    friend struct WaylandFrameDone;
    friend class WaylandDisplay;   // fokusgruppen + ytregistret
};

} // namespace gfx
