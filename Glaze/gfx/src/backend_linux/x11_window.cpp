// X11Window — gfx::Window ovanpå Xlib + EGL: XCreateWindow med EGL-
// konfigens visual, EWMH (_NET_WM_MOVERESIZE/_NET_WM_STATE/_NET_WORKAREA/
// _MOTIF_WM_HINTS), WM_DELETE_WINDOW och popup-kontraktet via
// override-redirect (menyer placeras fritt i skärm-px — X11 HAR
// skärmpositioner, så flip/klamp görs här).
// GL/text/bild återanvänds orört (GlRenderer).
//
// STORLEKSMARGINALEN UTANFÖR YTAN —
// Där X-sessionen har en kompositör och
// WM:en känner `_GTK_FRAME_EXTENTS` (GTK3:s regel) är X-fönstret klientytan +
// 2m, genomskinligt i marginalen, och egenskapen säger WM:en att marginalen
// inte är fönstret. ALLT i den här filen talar klientytan: `physW_/H_`,
// `normalX_/Y_`, `setPosition`, `clientToScreen` och placeringen — X-fönstrets
// origo och storlek är klientens minus respektive plus marginalen, och
// displayen drar av den från pekarens koordinater. Marginalen är 0 för
// popuper, Native/None och maximerat; ett fast fönster behåller den men
// inert, så geometrin aldrig byts av oss själva (bara av WM:en vid
// maximering). Helt utan stöd läser `hitRoleAt` kartans zoner innanför
// klientytan som vanligt.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "../display_util.h"
#include "../frame_margin.h"
#include "../icon_util.h"
#include "gfx/platform.h"
#include "x11_backend.h"

namespace gfx {

namespace {

// _MOTIF_WM_HINTS: decorations = 0 (eget chrome — ingen WM-ram).
struct MotifWmHints {
    unsigned long flags       = 0;
    unsigned long functions   = 0;
    unsigned long decorations = 0;
    long          inputMode   = 0;
    unsigned long status      = 0;
};
constexpr unsigned long kMwmHintsDecorations = 1UL << 1;

} // namespace

int physFromLogicalX11(int logical, float scale)
{
    return std::max(1, static_cast<int>(std::lround(logical * scale)));
}

bool X11Window::init(const WindowDesc& desc)
{
    chrome_      = desc.chrome;
    transparent_ = desc.transparent;
    hidden_      = desc.hidden;
    title_       = desc.title;

    const bool isPopup = desc.popupAnchorWindow != nullptr;
    // POPUPS är per definition ramlösa. TRANSPARENS är det däremot INTE:
    // ett RUNDAT fönster är en genomskinlig yta MED eget chrome — hörnen
    // är bortklippta, inte målade. Titelraden är en widget i trädet och
    // ritas av appen, som därmed också äger hörnen.
    if (isPopup && chrome_ != Chrome::None)
        chrome_ = Chrome::None;
    // `eglConfig(true)` svarar med den opaka konfigen när ARGB saknas och är
    // aldrig nullptr — frågan måste ställas direkt (en
    // jämförelse mot nullptr slår aldrig till).
    if (transparent_ && !display_->hasArgbConfig()) {
        log("x11: ingen ARGB-konfig — transparent degraderar till opakt");
        transparent_ = false;
    }

    windowId_ = nextWindowId();
    Display* dpy = display_->xdisplay();

    // Desc-måtten är LOGISKA px → fysiska via fönstrets
    // FULLA skala (Xft.dpi × användarskalan).
    const float s = uiScale();
    logicalW_ = desc.width > 0 ? desc.width : 1;
    logicalH_ = desc.height > 0 ? desc.height : 1;
    physW_ = physFromLogicalX11(logicalW_, s);
    physH_ = physFromLogicalX11(logicalH_, s);

    // STORLEKSMARGINALEN UTANFÖR YTAN. Ett ÄNDRINGSBART
    // fönster med eget chrome får en där X-sessionen kan rita den
    // genomskinlig och WM:en förstår den (`frameExtentsUsable`) — då skapas
    // fönstret med ARGB-visual också när det inte är transparent: marginalen
    // är alfa 0 och klientytan opak. Ett fönster som STARTAR fast
    // får ingen: marginalen hade bara ätit klick (genomskinlighet släpper inte
    // igenom input på X11, och vi sätter ingen inmatningsform). Beslutet
    // står sig — geometrin byts aldrig vid fast ↔ ändringsbart:
    // ett fönster som BLIR fast behåller marginalen, inert
    // (`frameResizable_`), och ett som blir ändringsbart utan marginal läser
    // kartans zoner innanför kanten. En maximerad start börjar utan (WM:en
    // hade annars fått lägga ut den utanför skärmen och sedan ta bort den
    // igen). Utan marginal gäller allt nedan som vanligt.
    limMinW_        = desc.minWidth;
    limMinH_        = desc.minHeight;
    limMaxW_        = desc.maxWidth;
    limMaxH_        = desc.maxHeight;
    frameResizable_ = !fixedSizeLimits();
    marginCapable_  = chrome_ == Chrome::Custom && !isPopup &&
                     frameResizable_ && display_->frameExtentsUsable();
    marginPx_       = marginCapable_
                          ? physFromLogicalX11(frame::kMarginLogical, s)
                          : 0;
    frameMargin_    = desc.startState == WindowState::Maximized
                          ? 0
                          : wantedMargin();
    const bool argb = transparent_ || marginCapable_;

    const XVisualInfo vi = display_->visualFor(argb);
    if (!vi.visual) {
        log("x11: ingen visual för EGL-konfigen");
        return false;
    }
    colormap_ = XCreateColormap(dpy, RootWindow(dpy, display_->screen()),
                                vi.visual, AllocNone);

    overrideRedirect_ = isPopup;

    XSetWindowAttributes attrs{};
    attrs.colormap          = colormap_;
    attrs.background_pixel  = 0;
    attrs.border_pixel      = 0;
    attrs.override_redirect = overrideRedirect_ ? kX11True : kX11False;
    attrs.event_mask = ExposureMask | StructureNotifyMask |
                       KeyPressMask | KeyReleaseMask |
                       EnterWindowMask | LeaveWindowMask |
                       FocusChangeMask | PropertyChangeMask;
    // Pekarens rörelse och knappar: via XInput 2 när servern har ≥ 2.1
    // (styrplattans skrollvalutorer och nypning finns bara där;
    // `selectXiPointer` nedan), annars kärnans masker. Inträde,
    // utträde och tangentbordet stannar i kärnan.
    if (!display_->xi2())
        attrs.event_mask |= ButtonPressMask | ButtonReleaseMask |
                            PointerMotionMask;

    int posX = 0, posY = 0;
    if (desc.posX != kWindowPosDefault && desc.posY != kWindowPosDefault) {
        posX = desc.posX;
        posY = desc.posY;
    }
    // FÖNSTRETS GEOMETRI och STARTLÄGET. En
    // BEGÄRD position blir `USPosition` i WM_NORMAL_HINTS — annars får WM:en
    // placera fönstret som den vill — och normalrutans startvärde, så en
    // maximerad start bär den begärda rutan.
    userPosition_ = desc.posX != kWindowPosDefault &&
                    desc.posY != kWindowPosDefault;
    normalX_      = posX;
    normalY_      = posY;
    startState_   = desc.startState;
    noActivate_   = desc.noActivate;
    // ETT NYTT X-FÖNSTER ÄR ALDRIG MAPPAT:
    // objektet lever vidare över `shutdown()`, och ett kvarvarande
    // `everMapped_` hade låtit `restore()`/`maximize()`/`setPosition` och
    // `setVisible`s hintstädning behandla ett återöppnat DOLT fönster som redan
    // visat — `restore()` hade mappat det före den första visningen.
    // Kartläggningens bokföring börjar om här; resten skrivs nedan ur `desc`.
    mapped_        = false;
    everMapped_    = false;
    minimized_     = false;
    iconicHintSet_ = false;
    userTimeZero_  = false;

    // X-fönstret är ytan: klientytan plus marginalen runt om, med origo
    // marginalen före klientens hörn (E2L). `normalX_/Y_` ovan är klientens.
    win_ = XCreateWindow(dpy, RootWindow(dpy, display_->screen()),
                         posX - frameMargin_, posY - frameMargin_,
                         static_cast<unsigned>(physW_ + 2 * frameMargin_),
                         static_cast<unsigned>(physH_ + 2 * frameMargin_), 0,
                         vi.depth, InputOutput, vi.visual,
                         CWColormap | CWBackPixel | CWBorderPixel |
                             CWOverrideRedirect | CWEventMask,
                         &attrs);
    if (!win_) {
        log("x11: fönstret kunde inte skapas");
        // Colormap:en skapades före fönstret och shutdown() early-
        // returnar på !win_ — frigör den här (mönster: shutdown-städningen).
        XFreeColormap(dpy, colormap_);
        colormap_ = 0;
        return false;
    }
    display_->registerWindow(win_, this);
    display_->selectXiPointer(win_);   // no-op utan XI2

    // WM_DELETE_WINDOW → WindowCloseRequested (krysset).
    Atom protocols[] = {display_->aWmDelete_};
    XSetWMProtocols(dpy, win_, protocols, 1);

    // STARTGRÄNSERNA: desc-fälten är bara startvärdet för
    // `setSizeLimits` — samma lagring (satt ovan, före marginalen), samma
    // väg. Måste sättas FÖRE XMapWindow: en WM läser WM_NORMAL_HINTS när
    // fönstret mappas. Samma skrivare bär gravitationen och den begärda
    // positionen (`writeNormalHints`) — och
    // `_GTK_FRAME_EXTENTS` likaså före mappningen: WM:en läser den första
    // som "det här är fönstrets ram".
    writeNormalHints();
    writeFrameExtents();

    // ALLTID ÖVERST: `desc.topmost` är startvärdet för
    // `setTopmost`, samma lagring och samma väg. Måste sättas FÖRE
    // XMapWindow — EWMH:s clientmessage gäller bara MAPPADE fönster, så en
    // begäran i fönsterklassens konstruktor hade annars tappats helt.
    // BEGÄRAN sätts, inte svaret: `applyTopmost` läser `requestedTopmost_`,
    // och med bara `topmost_` satt hade den skrivit bort begäran direkt.
    requestedTopmost_ = desc.topmost;
    if (requestedTopmost_)
        applyTopmost();

    // STARTLÄGET, också det FÖRE första XMapWindow —
    // på det omappade fönstret skriver klienten själv (EWMH respektive
    // ICCCM), och WM:en läser det när fönstret mappas, nu eller efter en
    // dold start. Maximerat är `_NET_WM_STATE_MAXIMIZED_*`; minimerat är
    // `initial_state = IconicState` i WM_HINTS (som `writeWmHints` skriver
    // ihop med `InputHint` nedan).
    if (startState_ == WindowState::Maximized) {
        setUnmappedNetWmState(display_->aNetWmStateMaxV_, true);
        setUnmappedNetWmState(display_->aNetWmStateMaxH_, true);
    }
    iconicHintSet_ = startState_ == WindowState::Minimized;

    // PROGRAMIKONEN: WM_CLASS ur appens id och ramar satta
    // före init — båda FÖRE XMapWindow, där WM:en läser fönstrets klass och
    // aktivitetsfältet sin första ikon.
    applyClassHint();
    applyIcon();

    setTitle(title_);

    // Eget chrome: be WM:et att inte rita ram/titelrad.
    if (chrome_ != Chrome::Native) {
        MotifWmHints hints;
        hints.flags       = kMwmHintsDecorations;
        hints.decorations = 0;
        XChangeProperty(dpy, win_, display_->aMotifHints_,
                        display_->aMotifHints_, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&hints),
                        5);
    }

    // Fönster-attributen. `desc.showInTaskbar` är en DUMMY här:
    // `_NET_WM_STATE_SKIP_TASKBAR` är inte byggt, och proxyns init loggar
    // begäran (linux_platform.cpp).
    if (desc.toolWindow || isPopup) {
        const Atom type = isPopup ? display_->aNetWmTypeDropdown_
                                  : display_->aNetWmTypeUtility_;
        XChangeProperty(dpy, win_, display_->aNetWmWindowType_, XA_ATOM,
                        32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&type), 1);
    }
    if (noActivate_ || iconicHintSet_)
        writeWmHints(iconicHintSet_);
    // FÖRSTA VISNINGEN AKTIVERAR INTE: EWMH —
    // `_NET_WM_USER_TIME` = 0 på ett fönster som mappas säger WM:en att inte
    // ge det fokus. Före den FÖRSTA mappningen, här eller efter en dold start
    // (`setVisible`), som tar bort egenskapen inför nästa. WM:en får välja
    // annorlunda — fasadens tabell lovar inte mer.
    if (desc.showInactive) {
        const long noll = 0;
        XChangeProperty(dpy, win_, display_->aNetWmUserTime_, XA_CARDINAL, 32,
                        PropModeReplace,
                        reinterpret_cast<const unsigned char*>(&noll), 1);
        userTimeZero_ = true;
    }
    if (desc.owner) {
        auto* owner = static_cast<X11Window*>(unwrapWindow(desc.owner));
        if (owner && owner->win_)
            XSetTransientForHint(dpy, win_, owner->win_);
    }

    // Popup-kontraktet: placera under ankarrectet med flip-uppåt/klamp
    // (skärmrymden på X11
    // ÄR fysiska px).
    if (isPopup) {
        auto* ow = static_cast<X11Window*>(
            unwrapWindow(desc.popupAnchorWindow));
        if (ow && ow->win_) {
            const float sw = static_cast<float>(physW_);
            const float sh = static_cast<float>(physH_);
            const Rectf& a = desc.popupAnchorRect;
            Pointf      pos = ow->clientToScreen(a.x, a.y + a.h);
            const Rectf scr = ow->screenArea();
            if (scr.w > 0.0f) {
                if (pos.y + sh > scr.y + scr.h)
                    pos.y = ow->clientToScreen(a.x, a.y).y - sh;
                // OBS: inte std::clamp — popup bredare än arbetsytan ger
                // max < min = UB där; max/min-formen låter vänsterkanten
                // vinna i stället.
                pos.x = std::max(scr.x,
                                 std::min(pos.x, scr.x + scr.w - sw));
            }
            XMoveWindow(dpy, win_,
                        static_cast<int>(std::lround(pos.x)),
                        static_cast<int>(std::lround(pos.y)));
        }
    }

    // EGL-ytan + renderern (samma GlRenderer som Wayland-sidan). Konfigen
    // följer visualen (ARGB för en transparent yta ELLER en marginal); den
    // genomskinliga compositen (`transparent_`) bara för en transparent yta.
    // Renderaren ritar klientytan och lägger den marginalen in i ytan.
    eglSurface_ = eglCreateWindowSurface(
        display_->egl(), display_->eglConfig(argb),
        static_cast<EGLNativeWindowType>(win_), nullptr);
    if (eglSurface_ == EGL_NO_SURFACE) {
        log("x11: EGL-ytan kunde inte skapas: 0x%04x", eglGetError());
        shutdown();
        return false;
    }
    renderer_.setFrameMargin(frameMargin_);
    if (!renderer_.init(display_->egl(), display_->eglConfig(argb),
                        eglSurface_, physW_, physH_, transparent_)) {
        shutdown();
        return false;
    }
    // X11: vsync-swap är säkert (vblank-styrt även för skymda fönster —
    // XWayland/Xorg fejkar vblank) och bromsar animationer som övriga
    // backends. Wayland-fallets häng-risk finns inte här.
    renderer_.setSwapInterval(1);

    registerLinuxWindow(this);
    visible_ = !hidden_;
    if (!hidden_) {
        XMapWindow(dpy, win_);   // MapNotify → WindowShown
        mapped_     = true;
        everMapped_ = true;
    }
    XFlush(dpy);
    measureDisplay(false);   // skärmens mm
    return true;
}

// ---------------------------------------------------------------- geometrin
//
// FÖNSTRETS GEOMETRI. Positionen är KLIENTENS origo i rotens px, och med
// `StaticGravity` i WM_NORMAL_HINTS (`writeNormalHints`) är det också den
// position `setPosition` och init sätter — en sparad position återställs
// exakt, också med en Native-ram runt fönstret.

void X11Window::trackNormalPosition()
{
    if (!win_)
        return;
    // XTranslateCoordinates och inte ConfigureNotify:s x/y: ett fönster i en
    // WM-ram får sina koordinater relativt ramen (ICCCM 4.1.5), och bara en
    // SYNTETISK ConfigureNotify bär rotens.
    int      rx = 0, ry = 0;
    ::Window barn = 0;
    if (XTranslateCoordinates(display_->xdisplay(), win_,
                              display_->rootWindow(), 0, 0, &rx, &ry,
                              &barn)) {
        // KLIENTENS origo: X-fönstrets plus storleksmarginalen (E2L).
        normalX_ = rx + frameMargin_;
        normalY_ = ry + frameMargin_;
    }
}

WindowPlacement X11Window::placement() const
{
    WindowPlacement p;
    if (!win_)
        return p;   // före init finns ingen geometri att spåra
    p.x           = normalX_;
    p.y           = normalY_;
    p.width       = logicalW_;   // står still under maximeringen (onConfigure)
    p.height      = logicalH_;
    p.state       = minimized_ ? WindowState::Minimized
                  : maximized_ ? WindowState::Maximized
                               : WindowState::Normal;
    p.hasPosition = true;
    return p;
}

void X11Window::writeWmHints(bool iconic)
{
    if (!win_)
        return;
    XWMHints wmh{};
    wmh.flags         = StateHint;
    wmh.initial_state = iconic ? IconicState : NormalState;
    if (noActivate_) {
        wmh.flags |= InputHint;
        wmh.input = kX11False;   // WM:et ska inte ge fönstret fokus
    }
    XSetWMHints(display_->xdisplay(), win_, &wmh);
}

// ---------------------------------------------------------------- programikonen
//
// PROGRAMIKONEN. `_NET_WM_ICON` (EWMH) är en
// CARDINAL-matris: per ram bredd, höjd och bredd × höjd pixlar som
// 0xAARRGGBB med RAK alfa, rad 0 överst. Format 32 betyder att Xlib tar emot
// en matris av `long` — 64 bitar per element på en 64-bitarsmaskin, där de
// låga 32 gäller. WM:en väljer storleken själv, så alla giltiga ramar ≤ 256
// px skickas (den minsta större ramen ensam när ingen är så liten). Hela
// propertyn går i EN begäran, och ryms den inte i serverns största
// (BIG-REQUESTS) tas de största ramarna bort: ett BadLength hade annars
// fällt appen genom Xlibs förvalda felhanterare.

void X11Window::setIcon(const WindowIcon& icon)
{
    icon_ = icon;
    applyIcon();   // före init: lagrat, init skriver det före XMapWindow
}

void X11Window::applyIcon()
{
    if (!win_)
        return;
    Display*      dpy      = display_->xdisplay();
    constexpr int kMaxSida = 256;

    std::vector<const IconImage*> ramar;
    for (const IconImage& im : icon_.images)
        if (iconImageValid(im) && iconImageSide(im) <= kMaxSida)
            ramar.push_back(&im);
    if (ramar.empty())
        if (const IconImage* im = pickIconImage(icon_, kMaxSida))
            ramar.push_back(im);
    std::sort(ramar.begin(), ramar.end(),
              [](const IconImage* a, const IconImage* b) {
                  return iconImageSide(*a) < iconImageSide(*b);
              });

    // Begärans tak i fyrbyteenheter, med marginal för ChangeProperty-huvudet.
    long tak = XExtendedMaxRequestSize(dpy);
    if (tak <= 0)
        tak = XMaxRequestSize(dpy);
    tak -= 64;

    std::vector<unsigned long> data;
    std::size_t                total = 0;
    for (const IconImage* im : ramar)
        total += 2 + static_cast<std::size_t>(im->width) *
                         static_cast<std::size_t>(im->height);
    data.reserve(total);
    std::size_t tagna = 0;
    for (const IconImage* im : ramar) {
        const std::size_t n = static_cast<std::size_t>(im->width) *
                              static_cast<std::size_t>(im->height);
        if (static_cast<long>(data.size() + 2 + n) > tak)
            break;   // stigande ordning — resten är ännu större
        data.push_back(static_cast<unsigned long>(im->width));
        data.push_back(static_cast<unsigned long>(im->height));
        const std::uint8_t* p = im->rgba.data();
        for (std::size_t i = 0; i < n; ++i, p += 4)
            data.push_back((static_cast<unsigned long>(p[3]) << 24) |
                           (static_cast<unsigned long>(p[0]) << 16) |
                           (static_cast<unsigned long>(p[1]) << 8) |
                           static_cast<unsigned long>(p[2]));
        ++tagna;
    }
    if (tagna < ramar.size())
        log("x11: _NET_WM_ICON ryms inte i en begäran — %u av %u ramar "
            "skickas",
            static_cast<unsigned>(tagna), static_cast<unsigned>(ramar.size()));
    else if (!icon_.images.empty() && ramar.empty())
        log("x11: ingen av ikonens %u ramar är giltig",
            static_cast<unsigned>(icon_.images.size()));

    if (data.empty()) {
        XDeleteProperty(dpy, win_, display_->aNetWmIcon_);
        iconApplied_ = false;
    } else {
        XChangeProperty(dpy, win_, display_->aNetWmIcon_, XA_CARDINAL, 32,
                        PropModeReplace,
                        reinterpret_cast<const unsigned char*>(data.data()),
                        static_cast<int>(data.size()));
        iconApplied_ = true;
    }
    XFlush(dpy);
}

// WM_CLASS: res_name OCH res_class = appens id (`gfx::appId()`). GNOME och
// KDE hittar `.desktop`-filen — och därmed ikonen i fönsterväxlaren — genom
// res_class, mot filens namn eller dess StartupWMClass.
void X11Window::applyClassHint()
{
    if (!win_)
        return;
    std::string namn  = appId();
    std::string klass = namn;
    XClassHint  hint{};
    hint.res_name  = namn.data();
    hint.res_class = klass.data();
    XSetClassHint(display_->xdisplay(), win_, &hint);
}

// SKÄRMENS mm — crtc:n fönstrets MITT står på (en
// translation till rotens px; WM:ens ram gör fönstrets egna koordinater
// relativa föräldern), annars den första aktiva. X11 skalar aldrig om
// bufferten, så fönstrets px ÄR skärmens. RandR:s millimeter är utgångens
// orienterade mått, och diagonalen bryr sig inte om vridningen.
void X11Window::measureDisplay(bool announce)
{
    if (!win_)
        return;
    DisplayMetrics m;
    if (!display_->randr()) {
        m = display::assumed("x11", "XRandR 1.3 saknas på servern");
    } else {
        int      rx = 0, ry = 0;
        ::Window barn = 0;
        XTranslateCoordinates(display_->xdisplay(), win_,
                              display_->rootWindow(),
                              frameMargin_ + physW_ / 2,
                              frameMargin_ + physH_ / 2, &rx, &ry, &barn);
        const X11Display::Screen* hit = nullptr;
        for (const X11Display::Screen& s : display_->screens())
            if (rx >= s.x && ry >= s.y && rx < s.x + s.w && ry < s.y + s.h) {
                hit = &s;
                break;
            }
        if (!hit && !display_->screens().empty())
            hit = &display_->screens().front();
        m = hit ? display::measure(hit->w, hit->h, hit->mmW, hit->mmH, hit->id)
                : display::assumed("x11", "XRandR rapporterar ingen aktiv skärm");
    }
    const bool ändrad = m.pxPerMm != metrics_.pxPerMm ||
                        m.source != metrics_.source || m.id != metrics_.id;
    metrics_ = std::move(m);
    if (announce && ändrad) {
        Event e;
        e.type     = EventType::WindowDisplayChanged;
        e.windowId = windowId_;
        pushEvent(e);
    }
}

void X11Window::shutdown()
{
    if (!win_)
        return;
    stopAllTimers();
    unregisterLinuxWindow(this);
    display_->windowDestroyed(this);
    renderer_.shutdownRenderer();
    if (eglSurface_ != EGL_NO_SURFACE) {
        eglDestroySurface(display_->egl(), eglSurface_);
        eglSurface_ = EGL_NO_SURFACE;
    }
    display_->unregisterWindow(win_);
    XDestroyWindow(display_->xdisplay(), win_);
    if (colormap_) {
        XFreeColormap(display_->xdisplay(), colormap_);
        colormap_ = 0;
    }
    XFlush(display_->xdisplay());
    win_       = 0;
    windowId_  = 0;
    maximized_ = false;
    iconApplied_ = false;   // propertyn dog med fönstret; ramarna står kvar
    // Storleksmarginalen beslutas om i nästa init (kompositören kan ha
    // kommit eller gått).
    marginCapable_ = false;
    marginPx_      = 0;
    frameMargin_   = 0;
}

// ---------------------------------------------------------------- events

void X11Window::onConfigure(int w, int h)
{
    // STORLEKSMARGINALEN: ConfigureNotify bär X-FÖNSTRETS storlek, och
    // allt nedan — bokföringen, skulden, den logiska storleken — talar
    // klientytan. En configure som skickades innan marginalen bytte räknas
    // om med den nya; den blir ett mellanläge som nästa configure rättar.
    w -= 2 * frameMargin_;
    h -= 2 * frameMargin_;
    if (w <= 0 || h <= 0)
        return;

    // BOKFÖRINGEN LIGGER FÖRE NO-OP-RETUREN, och det är avsiktligt.
    // En configure som bär SAMMA mått är fortfarande ett SVAR: lämnas den
    // obehandlad fastnar korrigeringens kredit i `ownResizes_` och
    // restore-skulden hänger kvar för alltid. Precis det händer om
    // klassificeringen ligger efter returen.

    // KLASSIFICERINGEN — fyra sorters configure, och bara EN av dem får
    // räkna tillbaka den lagrade logiska storleken:
    //
    //   1. korrigeringens SVAR   måttet är det vi bad om  → behåll logisk
    //   2. korrigeringen NEKAD   något annat mått kom     → UPPGIVNING
    //   3. vår egen resize       ownResizes_ > 0          → behåll logisk
    //   4. maximerad / skuld     WM:en äger måttet        → behåll logisk
    //   5. användarens resize    allt annat               → räkna tillbaka
    //
    // Punkt 4: maximeringens rect är WM:ens storlek och
    // inte användarens val, och återställningens rect kommer i den skala
    // som gällde vid maximeringen. Räknas logisk storlek tillbaka ur
    // någon av dem är restore-storleken förstörd redan vid zoomen.
    // `maximized_` ensamt räcker inte, för under Mutter kommer
    // restore-configuren FÖRE propertyn som släcker flaggan — men medan
    // en skuld är utestående är den lagrade logiska storleken per
    // definition sanningen och den fysiska den föråldrade, så skulden är
    // den andra halvan av samma vakt.
    bool ownAnswer   = false;
    bool keepLogical = false;

    if (restoreExpectW_ != 0) {
        const bool svarade = (w == restoreExpectW_ && h == restoreExpectH_);
        restoreExpectW_ = 0;
        restoreExpectH_ = 0;
        if (svarade) {
            ownAnswer   = true;
            keepLogical = true;
        } else {
            // VÄGRAN — eller WM:ens egen restore-rect som hann emellan.
            // Den lagrade LOGISKA storleken behålls i båda fallen: den är
            // sanningen, den fysiska är den föråldrade (återställd i den
            // skala som gällde vid maximeringen). Räknades den om här vore
            // hela fixen verkningslös i den ordning där WM:ens rect kommer
            // efter vår begäran.
            //
            // KVARSTÅENDE GRÄNS, medvetet inte hårdare skyddad: skickar
            // WM:en INGEN configure alls (ICCCM säger "bör", inte "ska")
            // ligger väntan kvar tills nästa configure — och kommer en
            // genuin användar-resize dessförinnan klassas den här som en
            // vägran. Kostnaden är EN överhoppad logisk uppdatering:
            // grenen nollar väntan och lämnar tillbaka krediten, så nästa
            // configure räknar om normalt igen. Felet är alltså BEGRÄNSAT
            // och självläkande — till skillnad från ett fel som
            // korrumperar invarianten permanent. Att täcka
            // det helt kräver att man kan skilja vår egen resize från
            // användarens, och det går inte utan serialnummer på
            // ConfigureRequest som X11 inte ger oss.
            keepLogical = true;
            // UPPGIVNINGEN — passets subtilaste rad. `XResizeWindow` fick
            // aldrig sitt svar, så krediten vi tog i `++ownResizes_` måste
            // lämnas tillbaka HÄR; annars fastnar räknaren positiv och
            // nästa riktiga användar-resize klassas som vår egen (punkt 3)
            // och slutar uppdatera logicalW_/H_ för alltid.
            // Configuren är alltså INTE vårt svar: den räknas inte ned som
            // ett sådant, WM:ens storlek ACCEPTERAS, och samma storlek
            // begärs ALDRIG om. Ett nytt försök får ske först vid ett nytt
            // externt tillståndsbyte (nytt skalbyte eller ny maximering).
            if (ownResizes_ > 0)
                --ownResizes_;
        }
    } else if (ownResizes_ > 0) {
        // Räknaren, inte en bool: två skalbyten i snabb följd ger två
        // configures, och den första får varken nolla vakten eller släppa
        // ut skal-eventet (då skulle mellanläget bli ny "sanning").
        ownAnswer   = true;
        keepLogical = true;
    } else if (maximized_ || restoreSizeOwed_) {
        keepLogical = true;
    }

    // NORMALRUTANS POSITION — bakom
    // SAMMA övergångsvakt som den lagrade logiska storleken: maximerat eller
    // med en utestående restore-skuld äger WM:en geometrin (och under Mutter
    // kommer återställningens configure FÖRE propertyn som släcker
    // `maximized_`, se ovan), och ett minimerat fönster står inte där det
    // återställs. FÖRE returen för oförändrat mått: en ren flytt bär samma
    // mått och är just den configure som flyttar normalrutan.
    // ALDRIG FÖRE DEN FÖRSTA MAPPNINGEN: då är positionen BEGÄRAN
    // (`setPosition`, USPosition), för WM:en placerar fönstret först när det
    // mappas. Ett omappat fönsters serverposition är inte den — Westons XWM
    // (WSLg) flyttar det inte alls och svarar med en syntetisk configure, som
    // annars hade skrivit tillbaka den gamla platsen över begäran.
    if (everMapped_ && !maximized_ && !restoreSizeOwed_ && !minimized_)
        trackNormalPosition();

    if (w == physW_ && h == physH_) {
        // Måttet är oförändrat — men bokföringen ovan har redan skett, så
        // varken krediten eller det väntade måttet kan bli hängande.
        if (ownAnswer)
            --ownResizes_;
        return;
    }
    physW_ = w;
    physH_ = h;

    if (ownAnswer)
        --ownResizes_;
    if (!keepLogical) {
        // Användar-/WM-styrd storlek: uppdatera även den lagrade LOGISKA
        // storleken (fysiska px ÷ skala), annars hoppar fönstret tillbaka
        // vid nästa Xft.dpi-byte.
        const float s = uiScale();   // fulla skalan (× userScale)
        logicalW_ = std::max(1, static_cast<int>(std::lround(physW_ / s)));
        logicalH_ = std::max(1, static_cast<int>(std::lround(physH_ / s)));
    }
    renderer_.resize(physW_, physH_);   // EGL-ytan följer fönstret själv
    Event e;
    e.windowId = windowId_;
    e.type     = EventType::WindowResized;
    pushEvent(e);
    e.type = EventType::WindowPixelSizeChanged;
    pushEvent(e);
    // Skal-eventet EFTER resizen: nu är buffert/renderer i nya storleken,
    // så fontomladdningen ritar i rätt pixelstorlek direkt.
    if (ownAnswer && scaleEventOwed_ && ownResizes_ == 0) {
        scaleEventOwed_ = false;
        e.type = EventType::WindowDisplayScaleChanged;
        pushEvent(e);
    }
}

// RESTORE-KORRIGERINGEN. Anropas från BÅDA
// flankerna och gör något bara när paret tillstånd+storlek är komplett:
// skulden är inlöst, vi är inte längre maximerade, och en configure har
// setts för just det läget.
//
// Placeringen är TVÅ ställen och inte ett, för event-ordningen är
// WM-beroende. Under Mutter/XWayland (800×520):
//   maximera:   PropertyNotify(max=1)  →  ConfigureNotify(2560×1558)
//   återställ:  ConfigureNotify(800×520)  →  PropertyNotify(max=0)
// Ordningen är alltså inte ens densamma åt de två hållen, och den är
// varken specificerad eller möjlig att avgöra statiskt. `queryMaximized()`
// duger inte som klassificerare: den svarar med propertyns läge NU, inte
// med läget som gällde när den configure vi behandlar skickades — och den
// lägger en blockerande X-roundtrip på varje interaktiv resize.
void X11Window::maybeCorrectRestoreSize()
{
    if (!restoreSizeOwed_ || maximized_)
        return;
    // Skulden är FÖRBRUKAD AV FÖRSÖKET, inte av framgången — den nollas
    // här och inte i svaret. Se kommentaren vid fälten: ett andra försök
    // mot en WM som nekar är en oändlig loop.
    restoreSizeOwed_ = false;
    if (!win_)
        return;
    const float s  = uiScale();
    const int   pw = physFromLogicalX11(logicalW_, s);
    const int   ph = physFromLogicalX11(logicalH_, s);
    if (pw == physW_ && ph == physH_)
        return;   // WM:en råkade återställa rätt — inget att göra
    restoreExpectW_ = pw;   // klientytan — onConfigure drar av marginalen
    restoreExpectH_ = ph;
    ++ownResizes_;
    XResizeWindow(display_->xdisplay(), win_,
                  static_cast<unsigned>(pw + 2 * frameMargin_),
                  static_cast<unsigned>(ph + 2 * frameMargin_));
    XFlush(display_->xdisplay());
}

void X11Window::readNetWmState(bool& maxed, bool& above, bool& hidden) const
{
    Atom type = 0;
    int  fmt  = 0;
    unsigned long n = 0, after = 0;
    unsigned char* data = nullptr;
    bool maxV = false, maxH = false;
    above  = false;
    hidden = false;
    if (XGetWindowProperty(display_->xdisplay(), win_,
                           display_->aNetWmState_, 0, 64, kX11False,
                           XA_ATOM, &type, &fmt, &n, &after,
                           &data) == Success &&
        data && type == XA_ATOM) {
        const Atom* atoms = reinterpret_cast<const Atom*>(data);
        for (unsigned long i = 0; i < n; ++i) {
            if (atoms[i] == display_->aNetWmStateMaxV_) maxV = true;
            if (atoms[i] == display_->aNetWmStateMaxH_) maxH = true;
            if (atoms[i] == display_->aNetWmStateAbove_) above = true;
            if (atoms[i] == display_->aNetWmStateHidden_) hidden = true;
        }
    }
    if (data)
        XFree(data);
    maxed = maxV && maxH;
}

bool X11Window::queryMaximized() const
{
    bool maxed = false, above = false, hidden = false;
    readNetWmState(maxed, above, hidden);
    return maxed;
}

void X11Window::onPropertyNetWmState()
{
    bool m = false, above = false, hidden = false;
    readNetWmState(m, above, hidden);
    // ALLTID ÖVERST: WM:ets svar är sanningen — `topmost()`
    // ska svara med vad som FAKTISKT gäller. Ingen event-typ hör till
    // bytet; flaggan läses på begäran.
    topmost_ = above;
    // MINIMERAT: EWMH:s `_NET_WM_STATE_HIDDEN` —
    // `placement()` svarar med det, och normalrutans position står still
    // medan det gäller (onConfigure). Ingen event-typ hör till bytet här
    // heller; återställningen ger MapNotify → WindowShown.
    minimized_ = hidden;
    if (m == maximized_)
        return;
    maximized_ = m;
    Event e;
    e.windowId = windowId_;
    e.type = m ? EventType::WindowMaximized : EventType::WindowRestored;
    pushEvent(e);

    // STORLEKSMARGINALEN följer läget (E2L, `applyFrameMargin`) — FÖRE
    // restore-korrigeringen, som begär klientytan plus marginalen.
    applyFrameMargin();

    // RESTORE-FLANKEN är den ENDA platsen korrigeringen sker, och den
    // körs SYNKRONT här: propertyn är det auktoritativa beskedet om att vi
    // lämnat maximerat läge, och skulden får aldrig överleva den. Om
    // WM:ens restore-rect ännu inte hunnit fram spelar det ingen roll —
    // målet räknas ur den LAGRADE logiska storleken och inte ur den
    // fysiska, och en rect som hinner emellan klassas som en vägran i
    // `onConfigure` (som då behåller logisk storlek och lämnar tillbaka
    // krediten). Se fältkommentaren i x11_backend.h för varför de två
    // ordningsgissande varianterna inte duger.
    if (!m)
        maybeCorrectRestoreSize();
}

// (x, y) i KLIENTENS px (displayen har dragit av marginalen). Ett tryck i
// storleksmarginalen (E2L) ger marginalens kant ur `hitRoleAt`, och kartans
// zoner innanför klientytan hoppas över medan marginalen finns.
bool X11Window::interceptPress(int x, int y, int xRoot, int yRoot,
                               unsigned long time)
{
    if (chrome_ != Chrome::Custom || overrideRedirect_)
        return false;
    const HitRole role = hitRoleAt(static_cast<float>(x),
                                   static_cast<float>(y));
    // _NET_WM_MOVERESIZE-riktningarna (EWMH): 0–7 = kanter/hörn, 8 = move.
    long dir = -1;
    switch (role) {
    case HitRole::Caption:           dir = 8; break;
    case HitRole::ResizeTopLeft:     dir = 0; break;
    case HitRole::ResizeTop:         dir = 1; break;
    case HitRole::ResizeTopRight:    dir = 2; break;
    case HitRole::ResizeRight:       dir = 3; break;
    case HitRole::ResizeBottomRight: dir = 4; break;
    case HitRole::ResizeBottom:      dir = 5; break;
    case HitRole::ResizeBottomLeft:  dir = 6; break;
    case HitRole::ResizeLeft:        dir = 7; break;
    default:                         return false;
    }
    Display* dpy = display_->xdisplay();
    // Spec-kravet: släpp den implicita pekargrabben innan WM:et tar över —
    // med XI2 är det en XI2-grab, och displayen släpper båda sorterna.
    display_->ungrabPointer(time);
    XEvent ev{};
    ev.xclient.type         = ClientMessage;
    ev.xclient.window       = win_;
    ev.xclient.message_type = display_->aNetMoveResize_;
    ev.xclient.format       = 32;
    ev.xclient.data.l[0]    = xRoot;
    ev.xclient.data.l[1]    = yRoot;
    ev.xclient.data.l[2]    = dir;
    ev.xclient.data.l[3]    = Button1;
    ev.xclient.data.l[4]    = 1;   // källa: normal applikation
    XSendEvent(dpy, RootWindow(dpy, display_->screen()), kX11False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(dpy);
    return true;
}

// ---------------------------------------------------------------- fasaden

void X11Window::raise()
{
    if (!win_ || !visible_)   // dolt förblir dolt (fasaden)
        return;
    Display* dpy = display_->xdisplay();
    XRaiseWindow(dpy, win_);
    // Ett noActivate-fönster lyfts UTAN `_NET_ACTIVE_WINDOW`:
    // WM:en hade gett det fokus, och det flytande
    // verktygsfältet tagit tangentbordet från fönstret som håller trycket.
    if (noActivate_) {
        XFlush(dpy);
        return;
    }
    XEvent ev{};
    ev.xclient.type         = ClientMessage;
    ev.xclient.window       = win_;
    ev.xclient.message_type = display_->aNetActiveWindow_;
    ev.xclient.format       = 32;
    ev.xclient.data.l[0]    = 1;   // källa: applikation
    XSendEvent(dpy, RootWindow(dpy, display_->screen()), kX11False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(dpy);
}

// FÖRE DEN FÖRSTA MAPPNINGEN byter de tre STARTLÄGET:
// en clientmessage gäller bara MAPPADE fönster (EWMH), och
// `XIconifyWindow` på ett omappat fönster hade bara skrivit om hintet. Det
// omappade fönstrets egenskaper skrivs i stället, som init gör med
// `desc.startState` — WM:en läser dem när fönstret mappas.
void X11Window::setUnmappedStartState(WindowState s)
{
    startState_ = s;
    const bool max = s == WindowState::Maximized;
    setUnmappedNetWmState(display_->aNetWmStateMaxV_, max);
    setUnmappedNetWmState(display_->aNetWmStateMaxH_, max);
    iconicHintSet_ = s == WindowState::Minimized;
    writeWmHints(iconicHintSet_);
    XFlush(display_->xdisplay());
}

void X11Window::minimize()
{
    if (win_ && !everMapped_) {
        setUnmappedStartState(WindowState::Minimized);
        return;
    }
    if (win_)
        XIconifyWindow(display_->xdisplay(), win_, display_->screen());
}

void X11Window::setNetWmState(bool add, Atom a1, Atom a2)
{
    Display* dpy = display_->xdisplay();
    XEvent ev{};
    ev.xclient.type         = ClientMessage;
    ev.xclient.window       = win_;
    ev.xclient.message_type = display_->aNetWmState_;
    ev.xclient.format       = 32;
    ev.xclient.data.l[0]    = add ? 1 : 0;   // _NET_WM_STATE_ADD/REMOVE
    ev.xclient.data.l[1]    = static_cast<long>(a1);
    ev.xclient.data.l[2]    = static_cast<long>(a2);
    ev.xclient.data.l[3]    = 1;
    XSendEvent(dpy, RootWindow(dpy, display_->screen()), kX11False,
               SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    XFlush(dpy);
}

void X11Window::maximize()
{
    if (win_ && !everMapped_) {
        setUnmappedStartState(WindowState::Maximized);
        return;
    }
    if (win_ && !maximized_)
        setNetWmState(true, display_->aNetWmStateMaxV_,
                      display_->aNetWmStateMaxH_);
}

void X11Window::restore()
{
    if (!win_)
        return;
    if (!everMapped_) {
        setUnmappedStartState(WindowState::Normal);
        return;
    }
    setNetWmState(false, display_->aNetWmStateMaxV_,
                  display_->aNetWmStateMaxH_);
    XMapWindow(display_->xdisplay(), win_);   // av-minimera (om minimerat)
}

void X11Window::setSize(int w, int h)
{
    if (!win_ || w <= 0 || h <= 0)
        return;
    // LOGISKA px (fasadändringen) → fysiska via fulla skalan.
    logicalW_ = w;
    logicalH_ = h;
    const float s  = uiScale();
    const int   pw = physFromLogicalX11(w, s);
    const int   ph = physFromLogicalX11(h, s);
    // Egen resize: configuren som kommer får INTE räkna tillbaka logisk
    // storlek ur den avrundade fysiska (801 logiska px vid skala 0.5 blir
    // 401 fysiska blir 802 logiska — drift för varje omgång). Räknaren
    // ökas bara när storleken faktiskt ändras; annars kommer ingen
    // configure och vakten skulle fastna.
    if (pw != physW_ || ph != physH_)
        ++ownResizes_;
    XResizeWindow(display_->xdisplay(), win_,
                  static_cast<unsigned>(pw + 2 * frameMargin_),
                  static_cast<unsigned>(ph + 2 * frameMargin_));
    XFlush(display_->xdisplay());
}

void X11Window::onUiScaleChanged()
{
    // Xft.dpi ELLER användarskalan bytte i drift (RESOURCE_MANAGER-
    // bevakningen resp. userScaleChanged): samma LOGISKA storlek ger ny
    // fysisk. XResizeWindow är ASYNKRON — skal-eventet får INTE gå ut
    // här, för då laddar widgets om fonten och ritar den nya layouten i
    // den GAMLA pixelstorleken (en synligt fel frame) innan
    // ConfigureNotify hunnit fram. Eventet köas i stället i onConfigure,
    // direkt efter renderer-resizen.
    if (!win_)
        return;
    const float s  = uiScale();   // fulla skalan (× userScale)
    // Hintsen talar FYSISKA px — de måste räknas om i den NYA skalan,
    // annars klampar WM:en mot gamla tal efter ett Xft.dpi-/userScale-byte.
    writeNormalHints();
    const int   pw = physFromLogicalX11(logicalW_, s);
    const int   ph = physFromLogicalX11(logicalH_, s);
    // Maximerat fönster äger WM:en storleken på — XResizeWindow ignoreras
    // och ingen configure kommer. Samma sak när skalan ger exakt samma
    // pixelstorlek. I båda fallen måste eventet gå direkt, annars laddas
    // fonten aldrig om (och vakten skulle fastna).
    if (maximized_ || (pw == physW_ && ph == physH_)) {
        // SKULDEN: den lagrade logiska storleken står
        // kvar, men den FYSISKA som WM:en kommer att återställa till hör
        // till den GAMLA skalan. Skulden löses in på restore-flanken.
        // Även Xft.dpi-byten i maximerat läge går den här vägen — samma
        // fel, samma fix.
        if (maximized_)
            restoreSizeOwed_ = true;
        Event e;
        e.type     = EventType::WindowDisplayScaleChanged;
        e.windowId = windowId_;
        pushEvent(e);
        return;
    }
    ++ownResizes_;
    scaleEventOwed_ = true;
    // Marginalens bredd står still vid ett skalbyte (`marginPx_`, räknad i
    // init) — bara klientytan byter storlek.
    XResizeWindow(display_->xdisplay(), win_,
                  static_cast<unsigned>(pw + 2 * frameMargin_),
                  static_cast<unsigned>(ph + 2 * frameMargin_));
    XFlush(display_->xdisplay());
}

// WM_NORMAL_HINTS — EN skrivare. `XSetWMNormalHints`
// skriver över HELA strukturen, så storleksgränserna, gravitationen och
// positionen MÅSTE skrivas tillsammans; en separat positionsskrivare hade
// raderat gränserna, eller tvärtom.
//
// STORLEKSGRÄNSERNA — `PMinSize`/`PMaxSize`. Enheten är FYSISKA
// px, så logiska px räknas om via fönstrets fulla skala — och därför måste
// hintsen ÅTERAPPLICERAS när Xft.dpi eller userScale ändras
// (`onUiScaleChanged` gör det). 0 = obegränsat, och det uttrycks genom att
// flaggan INTE sätts — XSizeHints har ingen "ingen gräns"-siffra.
//
// GRAVITATIONEN är ALLTID `StaticGravity` (ICCCM 4.1.2.3): klientfönstrets
// position ÄR positionen, och WM:en lägger sin ram runt den i stället för att
// flytta klienten med ramens tjocklek. Det är vad som gör att en position ur
// `placement()` (klientens origo) återställs exakt — med NorthWest, förvalet,
// hade varje spara-och-återställ med en Native-ram vandrat en ramtjocklek.
// `USPosition|PPosition` säger att positionen är BEGÄRD och inte ett förslag
// WM:en får ersätta med sin egen placering.
//
// STORLEKSMARGINALEN: hintsen talar X-FÖNSTRET, och marginalen är en
// del av det — gränserna gäller klientytan, så 2m läggs på (GTK3 gör
// detsamma med sina skuggor), och positionen är X-fönstrets origo,
// marginalen före klientens hörn. Med `StaticGravity` lägger WM:en
// X-fönstret exakt där, och klientens hörn hamnar på den begärda platsen.
void X11Window::writeNormalHints()
{
    if (!win_)
        return;
    const float s  = uiScale();
    const int   m2 = 2 * frameMargin_;
    XSizeHints h{};
    h.flags       = PWinGravity;
    h.win_gravity = StaticGravity;
    if (userPosition_) {
        h.flags |= USPosition | PPosition;
        // fälten är föråldrade (ICCCM) men fylls ändå
        h.x = normalX_ - frameMargin_;
        h.y = normalY_ - frameMargin_;
    }
    if (limMinW_ > 0 || limMinH_ > 0) {
        h.flags |= PMinSize;
        h.min_width  = (limMinW_ > 0 ? physFromLogicalX11(limMinW_, s) : 1) + m2;
        h.min_height = (limMinH_ > 0 ? physFromLogicalX11(limMinH_, s) : 1) + m2;
    }
    if (limMaxW_ > 0 || limMaxH_ > 0) {
        h.flags |= PMaxSize;
        // Obegränsad axel i ett PMaxSize-par: ett stort tal, inte 0 —
        // 0 hade betytt "max noll px" för WM:en.
        h.max_width  =
            limMaxW_ > 0 ? physFromLogicalX11(limMaxW_, s) + m2 : 32767;
        h.max_height =
            limMaxH_ > 0 ? physFromLogicalX11(limMaxH_, s) + m2 : 32767;
    }
    XSetWMNormalHints(display_->xdisplay(), win_, &h);
    XFlush(display_->xdisplay());
}

void X11Window::setSizeLimits(int minW, int minH, int maxW, int maxH)
{
    limMinW_ = minW;
    limMinH_ = minH;
    limMaxW_ = maxW;
    limMaxH_ = maxH;
    // Fast ↔ ändringsbart ändrar INTE X-fönstrets geometri:
    // marginalen står kvar och blir bara inert för ett fast fönster
    // (`frameResizable_` — ingen storleksändring och ingen pil i den). Ett
    // byte av geometrin hade gått asynkront genom WM:en, och byten i snabb
    // följd hade läst ytans gamla origo med den nya marginalen.
    frameResizable_ = !fixedSizeLimits();
    writeNormalHints();
}

// ---------------------------------------------------------------- marginalen
//
// STORLEKSMARGINALEN UTANFÖR YTAN — se x11_backend.h.

bool X11Window::fixedSizeLimits() const
{
    // Samma regel som `AppWindow::isFixedSize`: båda axlarna låsta.
    return limMinW_ > 0 && limMinW_ == limMaxW_ && limMinH_ > 0 &&
           limMinH_ == limMaxH_;
}

int X11Window::wantedMargin() const
{
    return marginCapable_ && !maximized_ ? marginPx_ : 0;
}

// Bara VÄXLINGEN med läget går genom WM:en: maximerat har ingen marginal (den
// hade legat utanför arbetsytan och stulit pekaren från en grannskärm). WM:en
// äger geometrin i båda riktningarna och anpassar X-fönstret till de nya
// `_GTK_FRAME_EXTENTS` själv (mutter behåller den synliga ramen); en configure
// som redan var på väg räknas med den nya marginalen och rättas av nästa.
bool X11Window::applyFrameMargin()
{
    const int m = wantedMargin();
    if (!win_ || m == frameMargin_)
        return false;
    frameMargin_ = m;
    renderer_.setFrameMargin(m);
    writeFrameExtents();
    writeNormalHints();
    // Ytan bär en annan layout — fönstret ber om en ny bildruta själv, för
    // klientytan kan vara densamma och då köar configuren ingen
    // storleksändring.
    Event e;
    e.type     = EventType::WindowExposed;
    e.windowId = windowId_;
    pushEvent(e);
    return true;
}

// `_GTK_FRAME_EXTENTS` = vänster, höger, topp, botten (CARDINAL[4]; format 32
// är `long` i Xlib). Skrivs också med nollor — WM:en ska veta att marginalen
// är borta, inte gissa.
void X11Window::writeFrameExtents()
{
    if (!win_ || !marginCapable_)
        return;
    const long m      = frameMargin_;
    const long ext[4] = {m, m, m, m};
    XChangeProperty(display_->xdisplay(), win_, display_->aGtkFrameExtents_,
                    XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(ext), 4);
    XFlush(display_->xdisplay());
}

// ALLTID ÖVERST — `_NET_WM_STATE_ABOVE`. TVÅ vägar, och
// valet är inte kosmetiskt: EWMH säger att ett MAPPAT fönster byter
// tillstånd med en clientmessage till roten (WM:et äger propertyn då),
// medan klienten själv skriver propertyn på ett OMAPPAT fönster. Bara
// clientmessage-vägen hade tappat varje begäran gjord före `show()`.
void X11Window::applyTopmost()
{
    if (!win_)
        return;
    if (mapped_) {
        setNetWmState(requestedTopmost_, display_->aNetWmStateAbove_, 0);
        return;
    }
    setUnmappedNetWmState(display_->aNetWmStateAbove_, requestedTopmost_);
    // Den HÄR vägen skriver propertyn själv, alltså VET vi vad den säger nu
    // — till skillnad från clientmessage-vägen, där vi bara frågat. WM:et
    // kan fortfarande välja bort den vid map, och då rättar
    // `onPropertyNetWmState` värdet.
    topmost_ = requestedTopmost_;
}

// Omappat: läs listan, lägg till eller ta bort `a` och skriv tillbaka — aldrig
// ett rakt XDeleteProperty, som hade slagit bort de andra tillstånden (en
// maximerad start och "alltid överst" står i samma property). Delas av
// `applyTopmost` och den maximerade starten.
void X11Window::setUnmappedNetWmState(Atom a, bool on)
{
    if (!win_)
        return;
    Display*       dpy  = display_->xdisplay();
    Atom           type = 0;
    int            fmt  = 0;
    unsigned long  n = 0, after = 0;
    unsigned char* data = nullptr;
    std::vector<Atom> atoms;
    if (XGetWindowProperty(dpy, win_, display_->aNetWmState_, 0, 64,
                           kX11False, XA_ATOM, &type, &fmt, &n, &after,
                           &data) == Success &&
        data && type == XA_ATOM) {
        const Atom* befintliga = reinterpret_cast<const Atom*>(data);
        for (unsigned long i = 0; i < n; ++i)
            if (befintliga[i] != a)
                atoms.push_back(befintliga[i]);
    }
    if (data)
        XFree(data);
    if (on)
        atoms.push_back(a);
    if (atoms.empty())
        XDeleteProperty(dpy, win_, display_->aNetWmState_);
    else
        XChangeProperty(dpy, win_, display_->aNetWmState_, XA_ATOM, 32,
                        PropModeReplace,
                        reinterpret_cast<const unsigned char*>(atoms.data()),
                        static_cast<int>(atoms.size()));
    XFlush(dpy);
}

void X11Window::setTopmost(bool on)
{
    // Bara BEGÄRAN sätts här. `topmost_` (svaret) skrivs av
    // `onPropertyNetWmState` när WM:et faktiskt ändrat propertyn — annars
    // hade gettern rapporterat vår önskan som faktum, och en WM som
    // ignorerar begäran hade lämnat den permanent fel.
    requestedTopmost_ = on;
    applyTopmost();
}

void X11Window::setTitle(const std::string& t)
{
    title_ = t;
    if (!win_)
        return;
    Display* dpy = display_->xdisplay();
    XStoreName(dpy, win_, title_.c_str());
    XChangeProperty(dpy, win_, display_->aNetWmName_, display_->aUtf8_, 8,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char*>(title_.data()),
                    static_cast<int>(title_.size()));
    XFlush(dpy);
}

void X11Window::setVisible(bool on)
{
    if (!win_ || visible_ == on)
        return;
    visible_ = on;
    hidden_  = false;
    mapped_ = on;
    if (on) {
        // STARTLÄGET gäller bara den FÖRSTA mappningen:
        // en minimerad start skriver IconicState, och WM:en läser
        // `initial_state` vid VARJE mappning från Withdrawn — utan
        // återställningen hade varje senare show() minimerat fönstret igen.
        // Före en dold starts första mappning står hintet kvar.
        if (everMapped_ && iconicHintSet_) {
            writeWmHints(false);
            iconicHintSet_ = false;
        }
        // …och den första inaktiva visningen: nästa mappning aktiverar
        // som vanligt.
        if (everMapped_ && userTimeZero_) {
            XDeleteProperty(display_->xdisplay(), win_,
                            display_->aNetWmUserTime_);
            userTimeZero_ = false;
        }
        everMapped_ = true;
        XMapWindow(display_->xdisplay(), win_);
    } else {
        XUnmapWindow(display_->xdisplay(), win_);
    }
    XFlush(display_->xdisplay());
}

// Positionen är KLIENTENS hörn — X-fönstret står storleksmarginalen före det,
// så `setPosition(clientToScreen(0, 0))` lämnar fönstret där det står.
// FÖRE DEN FÖRSTA MAPPNINGEN är det en BEGÄRD
// position: WM:en placerar annars fönstret själv vid mappningen, så
// `USPosition` skrivs (`writeNormalHints`) — startplatsen som räknats om efter
// `onLoad` ska gälla också när init inte fick någon.
void X11Window::setPosition(int x, int y)
{
    if (win_) {
        if (!everMapped_) {
            normalX_      = x;
            normalY_      = y;
            userPosition_ = true;
            writeNormalHints();   // också hintsens (föråldrade) x och y
        }
        XMoveWindow(display_->xdisplay(), win_, x - frameMargin_,
                    y - frameMargin_);
        XFlush(display_->xdisplay());
    }
}

Pointf X11Window::clientToScreen(float px, float py) const
{
    if (!win_)
        return Pointf{0.0f, 0.0f};
    int      x = 0, y = 0;
    ::Window child = 0;
    XTranslateCoordinates(display_->xdisplay(), win_,
                          RootWindow(display_->xdisplay(),
                                     display_->screen()),
                          frameMargin_, frameMargin_, &x, &y, &child);
    return Pointf{static_cast<float>(x) + px, static_cast<float>(y) + py};
}

Rectf X11Window::screenArea() const
{
    if (!win_)
        return Rectf{0, 0, 0, 0};
    // _NET_WORKAREA (första desktopen) — fallback: hela skärmen. Läsningen är
    // displayens, delad med `placementScreens`.
    Rectf r;
    if (display_->readWorkarea(r))
        return r;
    Display* dpy = display_->xdisplay();
    return Rectf{0.0f, 0.0f,
                 static_cast<float>(DisplayWidth(dpy, display_->screen())),
                 static_cast<float>(
                     DisplayHeight(dpy, display_->screen()))};
}

void X11Window::releaseMouseCapture()
{
    // X11:s implicita grab per tryck — släpp så en popup öppnad mitt i
    // trycket ser mus-events (samma syfte som D3D11:s ReleaseCapture). Med
    // XI2 är grabben XI2:ans, och displayen släpper båda sorterna.
    if (win_)
        display_->ungrabPointer(CurrentTime);
}

} // namespace gfx
