// Linux-implementationen av plattformsfasaden (gfx/platform.h): protokoll-
// valet (Wayland → X11 → surfacelös headless), den GEMENSAMMA händelse-
// styrda pumpen (epoll på display-fd + timerfd:ar + D-Bus — epoll_wait
// blockerar = ~0 % CPU i vila, hård regel 1), loggning, exe-katalog,
// klippbords-delegering, portal-temat (D-Bus) och fönsterfabriken.
//
// Här bor även LinuxWindow-basens gemensamma delar (renderFrame-sekvensen,
// timrarna, readbacken) och HeadlessWindow (glaze-render utan display:
// surfacelös EGL — allt ritas i retained-FBO:n). Timrarnas timerfd-källa
// bor i linux_timer.cpp, delad med appens timer.

#include <atomic>
#include <fcntl.h>
#include <link.h>
#include <pwd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/wait.h>
#include <unistd.h>

#include <dbus/dbus.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>

#include "../display_util.h"
#include "../frame_margin.h"
#include "../icon_util.h"
#include "gfx/null_renderer.h"
#include "gfx/platform.h"
#include "linux_backend.h"

#include <EGL/eglext.h>   // efter egl.h (via linux_backend.h)

namespace gfx {

// ---------------------------------------------------------------- log m.m.

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
void log(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::fprintf(stderr, "%s\n", buf);
}

std::string basePath()
{
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return {};
    buf[n] = '\0';
    std::string path(buf);
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos)
        return {};
    path.resize(slash + 1);   // behåll avslutande separator
    return path;
}

// Det första objektet dl_iterate_phdr besöker är huvudprogrammet (glibc och
// musl dokumenterar det): adressen hör till programfilen om den ligger i ett
// av dess laddade segment. Segmenten och inte basen — ett program byggt utan
// PIE har laddningsförskjutningen 0 och sin bas på länkadressen.
bool isInProgramFile(const void* address)
{
    struct Fråga {
        std::uintptr_t adress;
        bool           träff;
    } fråga{reinterpret_cast<std::uintptr_t>(address), false};
    dl_iterate_phdr(
        [](dl_phdr_info* info, std::size_t, void* data) -> int {
            auto* f = static_cast<Fråga*>(data);
            for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
                const ElfW(Phdr)& segment = info->dlpi_phdr[i];
                if (segment.p_type != PT_LOAD)
                    continue;
                const std::uintptr_t start = info->dlpi_addr + segment.p_vaddr;
                if (f->adress >= start && f->adress < start + segment.p_memsz)
                    f->träff = true;
            }
            return 1;   // bara huvudprogrammet
        },
        &fråga);
    return fråga.träff;
}

// PROGRAMFILENS NAMN (icon_util.h) — förvalet för `appId()`,
// alltså app_id på Wayland och WM_CLASS på X11: sista komponenten i
// /proc/self/exe. Ingen filändelse att ta bort — en ELF-fil har ingen, och en
// punkt i namnet (`org.example.App`) är en del av identiteten.
std::string programName()
{
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0)
        return {};
    buf[n] = '\0';
    const std::string path(buf);
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

WindowId nextWindowId()
{
    static WindowId next = 0;   // enkeltrådat (pumpen äger allt)
    return ++next;
}

// ------------------------------------------------------- fönster-registret
//
// Broadcast-events (temabyte, textskalning) saknar naturligt fönster —
// spegla D3D11 (varje fönster får sitt WM_SETTINGCHANGE) genom att stämpla
// ett event per öppet fönster.

namespace {

std::vector<LinuxWindow*>& windowRegistry()
{
    static std::vector<LinuxWindow*> v;
    return v;
}

} // namespace

void registerLinuxWindow(LinuxWindow* w)
{
    windowRegistry().push_back(w);
}

void unregisterLinuxWindow(LinuxWindow* w)
{
    auto& v = windowRegistry();
    for (auto it = v.begin(); it != v.end(); ++it)
        if (*it == w) {
            v.erase(it);
            return;
        }
}

void broadcastEvent(EventType t)
{
    for (LinuxWindow* w : windowRegistry()) {
        Event e;
        e.type     = t;
        e.windowId = w->id();
        pushEvent(e);
    }
}

// ---------------------------------------------------------------- event-kö

namespace {

std::deque<Event>& queue()
{
    static std::deque<Event> q;
    return q;
}

bool popQueued(Event& e)
{
    if (queue().empty())
        return false;
    e = queue().front();
    queue().pop_front();
    return true;
}

} // namespace

void pushEvent(const Event& e)
{
    queue().push_back(e);
}

// ---------------------------------------------------------------- epoll

namespace {

int epollFd()
{
    static int fd = ::epoll_create1(EPOLL_CLOEXEC);
    return fd;
}

} // namespace

bool addPollSource(PollSource* s)
{
    epoll_event ev{};
    ev.events   = EPOLLIN;
    ev.data.ptr = s;
    if (::epoll_ctl(epollFd(), EPOLL_CTL_ADD, s->fd, &ev) != 0) {
        log("epoll: kunde inte registrera fd %d (errno %d)", s->fd, errno);
        return false;
    }
    return true;
}

void removePollSource(PollSource* s)
{
    ::epoll_ctl(epollFd(), EPOLL_CTL_DEL, s->fd, nullptr);
}

// ------------------------------------------------- trådsäker väckning
//
// En `eventfd` i SAMMA epoll som allt annat. Skrivningen är den ENDA
// operation i hela backenden som får göras från en främmande tråd —
// event-kön är en vanlig `std::deque` utan lås och ägs av UI-tråden, så
// workern får aldrig röra den. Läsningen och `pushEvent` sker i
// `onReady`, alltså PÅ UI-tråden inne i `pumpOnce`.
//
// Räknaren är hela poängen med eventfd:n: N skrivningar summeras till ETT
// vaknande, så en skur av `post()` från en tråd inte köar N Wakeup-events.

namespace {

PollSource g_wakeupSource;
// ATOMISK: `postWakeup()` läser den från en FRÄMMANDE tråd medan
// `shutdownWakeup()` skriver den från UI-tråden — ett vanligt `int` vore
// en datakapplöpning. Atomiciteten tar bort
// kapplöpningen; fd-ÅTERANVÄNDNINGEN tar den inte bort: stängs fd:n
// mellan läsningen och `write()` kan OS:et ha delat ut numret igen.
// Kontraktet i `app.h` är därför att alla postande trådar ska vara
// stoppade före teardown — se noten där.
std::atomic<int> g_wakeupFd{-1};

void openWakeup()
{
    if (g_wakeupFd >= 0)
        return;
    g_wakeupFd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (g_wakeupFd < 0) {
        log("gfx::init: eventfd misslyckades (errno %d) — postWakeup blir "
            "en no-op", errno);
        return;
    }
    g_wakeupSource.fd      = g_wakeupFd;
    g_wakeupSource.onReady = [] {
        std::uint64_t v = 0;
        while (::read(g_wakeupFd, &v, sizeof v) == sizeof v) {
        }
        Event e;
        e.type     = EventType::Wakeup;
        e.windowId = 0;   // appens event, inte fönstrets
        pushEvent(e);
    };
    addPollSource(&g_wakeupSource);
}

void closeWakeup()
{
    if (g_wakeupFd < 0)
        return;
    removePollSource(&g_wakeupSource);
    ::close(g_wakeupFd);
    g_wakeupFd        = -1;
    g_wakeupSource.fd = -1;
}

} // namespace

void postWakeup()
{
    const int fd = g_wakeupFd;
    if (fd < 0)
        return;   // före init / efter shutdown — no-op, som fasaden lovar
    const std::uint64_t ett = 1;
    if (::write(fd, &ett, sizeof ett) != static_cast<ssize_t>(sizeof ett)) {
        // EAGAIN betyder att räknaren är mättad — då finns redan en
        // obehandlad väckning, vilket är precis det vi ville ha.
        if (errno != EAGAIN)
            log("postWakeup: write misslyckades (errno %d)", errno);
    }
}

// ---------------------------------------------------------------- D-Bus
//
// xdg-desktop-portalens Settings-gränssnitt bär både ljus/mörkt
// (org.freedesktop.appearance/color-scheme) och GNOME:s textskalning
// (org.gnome.desktop.interface/text-scaling-factor). systemTheme() läser
// färskt per anrop (fasadens kontrakt); textskalningen cachas och
// uppdateras via SettingChanged-signalen (D-Bus-fd:n i samma epoll).

namespace {

float g_textScale = 1.0f;

// Läs ut värdet ur portalens svar: ReadOne ger v, Read ger v(v).
bool variantToUint(DBusMessageIter* it, std::uint32_t& out)
{
    while (dbus_message_iter_get_arg_type(it) == DBUS_TYPE_VARIANT) {
        DBusMessageIter sub;
        dbus_message_iter_recurse(it, &sub);
        *it = sub;
    }
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_UINT32)
        return false;
    dbus_bool_t dummy = FALSE;
    (void)dummy;
    std::uint32_t v = 0;
    dbus_message_iter_get_basic(it, &v);
    out = v;
    return true;
}

// DUBBELKLICKETS TIDSFÖNSTER — 0 = inte läst än. Läses lat vid
// första frågan (ett blockerande portal-anrop, men ETT) och hålls levande
// av SettingChanged-signalen nedan, precis som textskalningen.
std::uint32_t g_doubleClickMs = 0;

bool variantToInt32(DBusMessageIter* it, std::int32_t& out)
{
    while (dbus_message_iter_get_arg_type(it) == DBUS_TYPE_VARIANT) {
        DBusMessageIter sub;
        dbus_message_iter_recurse(it, &sub);
        *it = sub;
    }
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_INT32)
        return false;
    std::int32_t v = 0;
    dbus_message_iter_get_basic(it, &v);
    out = v;
    return true;
}

bool variantToDouble(DBusMessageIter* it, double& out)
{
    while (dbus_message_iter_get_arg_type(it) == DBUS_TYPE_VARIANT) {
        DBusMessageIter sub;
        dbus_message_iter_recurse(it, &sub);
        *it = sub;
    }
    if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_DOUBLE)
        return false;
    double v = 0.0;
    dbus_message_iter_get_basic(it, &v);
    out = v;
    return true;
}

// Portal-anrop: ReadOne(ns, key) med Read(ns, key) som äldre fallback.
// nullptr när portalen/bussen saknas (headless SSH) — anroparen defaultar.
DBusMessage* portalRead(DBusConnection* conn, const char* ns,
                        const char* key)
{
    for (const char* method : {"ReadOne", "Read"}) {
        DBusMessage* msg = dbus_message_new_method_call(
            "org.freedesktop.portal.Desktop",
            "/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.Settings", method);
        if (!msg)
            return nullptr;
        dbus_message_append_args(msg, DBUS_TYPE_STRING, &ns,
                                 DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID);
        DBusMessage* reply = dbus_connection_send_with_reply_and_block(
            conn, msg, 200 /* ms — får aldrig hänga pumpen länge */,
            nullptr);
        dbus_message_unref(msg);
        if (reply)
            return reply;
    }
    return nullptr;
}

DBusHandlerResult dbusFilter(DBusConnection*, DBusMessage* msg, void*)
{
    if (dbus_message_is_signal(msg, "org.freedesktop.portal.Settings",
                               "SettingChanged")) {
        const char* ns  = nullptr;
        const char* key = nullptr;
        DBusMessageIter it;
        if (dbus_message_iter_init(msg, &it) &&
            dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING) {
            dbus_message_iter_get_basic(&it, &ns);
            if (dbus_message_iter_next(&it) &&
                dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING)
                dbus_message_iter_get_basic(&it, &key);
        }
        if (ns && key) {
            if (std::strcmp(ns, "org.freedesktop.appearance") == 0 &&
                std::strcmp(key, "color-scheme") == 0) {
                // Värdet läses via systemTheme() vid nästa repaint —
                // eventet är bara invalidate-signalen (som D3D11).
                broadcastEvent(EventType::SystemThemeChanged);
            } else if (std::strcmp(ns, "org.gnome.desktop.interface") == 0 &&
                       std::strcmp(key, "text-scaling-factor") == 0) {
                if (dbus_message_iter_next(&it)) {
                    double d = 0.0;
                    if (variantToDouble(&it, d) && d > 0.0) {
                        g_textScale = static_cast<float>(d);
                        // Backenden måste reagera SJÄLV (eventet nedan
                        // konsumeras i widgets): Wayland-fönstren räknar
                        // om ytenheter/buffert ur sin lagrade logiska
                        // storlek. X11 no-op:ar (Xft.dpi-vägen).
                        for (LinuxWindow* w : windowRegistry())
                            w->onTextScaleChanged();
                        broadcastEvent(
                            EventType::WindowDisplayScaleChanged);
                    }
                }
            } else if (std::strcmp(ns, "org.gnome.desktop.peripherals.mouse") ==
                           0 &&
                       std::strcmp(key, "double-click") == 0) {
                // Bara värdet — inget att rita om; nästa klick läser det.
                if (dbus_message_iter_next(&it)) {
                    std::int32_t ms = 0;
                    if (variantToInt32(&it, ms) && ms > 0)
                        g_doubleClickMs = static_cast<std::uint32_t>(ms);
                }
            }
        }
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

PollSource g_dbusSource;

DBusConnection* dbusConn()
{
    static DBusConnection* conn = [] {
        DBusConnection* c = dbus_bus_get(DBUS_BUS_SESSION, nullptr);
        if (!c)
            return static_cast<DBusConnection*>(nullptr);
        dbus_connection_set_exit_on_disconnect(c, FALSE);
        dbus_bus_add_match(
            c,
            "type='signal',interface='org.freedesktop.portal.Settings',"
            "member='SettingChanged'",
            nullptr);
        dbus_connection_add_filter(c, &dbusFilter, nullptr, nullptr);
        dbus_connection_flush(c);
        int fd = -1;
        if (dbus_connection_get_unix_fd(c, &fd) && fd >= 0) {
            g_dbusSource.fd      = fd;
            g_dbusSource.onReady = [c] {
                // Dränera utan att blockera; filtret ovan köar events.
                dbus_connection_read_write(c, 0);
                while (dbus_connection_get_dispatch_status(c) ==
                       DBUS_DISPATCH_DATA_REMAINS)
                    dbus_connection_dispatch(c);
            };
            addPollSource(&g_dbusSource);
        }
        return c;
    }();
    return conn;
}

void readTextScale()
{
    DBusConnection* c = dbusConn();
    if (!c)
        return;
    if (DBusMessage* reply = portalRead(c, "org.gnome.desktop.interface",
                                        "text-scaling-factor")) {
        DBusMessageIter it;
        double d = 0.0;
        if (dbus_message_iter_init(reply, &it) && variantToDouble(&it, d) &&
            d > 0.0)
            g_textScale = static_cast<float>(d);
        dbus_message_unref(reply);
    }
}

} // namespace

float textScaleFactor()
{
    return g_textScale;
}

// GNOME bär tiden i `org.gnome.desktop.peripherals.mouse/double-click`, och
// portalen exponerar den (samma väg som textskalningen). Utan portal eller
// utan nyckeln — KDE, en naken X-session — gäller 400 ms, som är både GTK:s
// (`gtk-double-click-time`) och KDE:s (`DoubleClickInterval`) förval.
std::uint32_t doubleClickMs()
{
    if (g_doubleClickMs == 0) {
        g_doubleClickMs = 400;
        if (DBusConnection* c = dbusConn()) {
            if (DBusMessage* reply = portalRead(
                    c, "org.gnome.desktop.peripherals.mouse", "double-click")) {
                DBusMessageIter it;
                std::int32_t    ms = 0;
                if (dbus_message_iter_init(reply, &it) &&
                    variantToInt32(&it, ms) && ms > 0)
                    g_doubleClickMs = static_cast<std::uint32_t>(ms);
                dbus_message_unref(reply);
            }
        }
    }
    return g_doubleClickMs;
}

SystemTheme systemTheme()
{
    // Portalens color-scheme: 0 = ingen preferens, 1 = mörkt, 2 = ljust.
    // Okänt/ingen buss → Light (samma fallback som övriga backends).
    DBusConnection* c = dbusConn();
    if (!c)
        return SystemTheme::Light;
    SystemTheme theme = SystemTheme::Light;
    if (DBusMessage* reply = portalRead(c, "org.freedesktop.appearance",
                                        "color-scheme")) {
        DBusMessageIter it;
        std::uint32_t v = 0;
        if (dbus_message_iter_init(reply, &it) && variantToUint(&it, v))
            theme = (v == 1) ? SystemTheme::Dark : SystemTheme::Light;
        dbus_message_unref(reply);
    }
    return theme;
}

std::string systemLocale()
{
    // POSIX-PRIORITETEN, inte en ospecificerad "ta den första som finns":
    // LC_ALL slår allt, LC_MESSAGES är den kategori som styr just
    // ÖVERSÄTTNINGAR, och LANG är golvet. En tom variabel räknas som
    // OSATT — det är så skalet nollställer en kategori.
    const char* kallor[] = {"LC_ALL", "LC_MESSAGES", "LANG"};
    const char* v        = nullptr;
    for (const char* namn : kallor) {
        const char* x = std::getenv(namn);
        if (x && *x) {
            v = x;
            break;
        }
    }
    if (!v)
        return std::string();

    // `sv_SE.UTF-8@euro` → `sv-SE`: kodning och variant kapas, '_' → '-'.
    std::string s(v);
    const std::size_t slut = s.find_first_of(".@");
    if (slut != std::string::npos)
        s.erase(slut);
    for (char& c : s)
        if (c == '_')
            c = '-';
    // `C` och `POSIX` är FRÅNVARON av ett språkval, inte ett språk.
    if (s == "C" || s == "POSIX" || s.empty())
        return std::string();
    return s;
}

void setQuitMenuPrefix(const std::string&)
{
    // Ingen global appmeny i de protokoll vi talar (xdg-shell, X11) — den
    // som finns ritar appen själv i klientytan (gfx/platform.h).
}

// ---------------------------------------------------------------- display

LinuxDisplay*& LinuxDisplay::current()
{
    static LinuxDisplay* d = nullptr;
    return d;
}

namespace {

PollSource g_displaySource;   // display-fd:n (hanteras via pre-/postpoll)

} // namespace

bool init()
{
    if (LinuxDisplay::current())
        return true;

    // GRAFIKKORTET FÖRST: miljövariablerna måste stå
    // innan protokollvalet gör det första EGL-anropet.
    applyGpuEnvironment();

    // Protokollvalet — EN gång: env-överstyrning → Wayland (kräver
    // fungerande anslutning + obligatoriska globals) → X11 (täcker även
    // XWayland) → surfacelös headless (glaze-render över SSH).
    const char* drv = std::getenv("GLAZE_LINUX_DRIVER");
    LinuxDisplay* d = nullptr;
    if (drv && std::strcmp(drv, "x11") == 0) {
        d = createX11Display();
    } else if (drv && std::strcmp(drv, "wayland") == 0) {
        d = createWaylandDisplay();
    } else {
        if (std::getenv("WAYLAND_DISPLAY"))
            d = createWaylandDisplay();
        if (!d && std::getenv("DISPLAY"))
            d = createX11Display();
    }
    if (!d) {
        log("gfx::init: ingen display (WAYLAND_DISPLAY/DISPLAY) — "
            "surfacelös EGL (headless, glaze-render-vägen)");
        d = createHeadlessDisplay();
    }
    if (!d) {
        log("gfx::init: ingen backend kunde initieras");
        return false;
    }
    LinuxDisplay::current() = d;

    if (d->fd() >= 0) {
        g_displaySource.fd        = d->fd();
        g_displaySource.isDisplay = true;
        addPollSource(&g_displaySource);
    }

    openWakeup();      // trådsäker väckning — samma epoll
    readTextScale();   // portalens textskalning in i uiScale
    return true;
}

void shutdown()
{
    // Appens timrar FÖRST — ur epollen medan displayen
    // finns, så att `startAppTimer` efter shutdown svarar false (ingen
    // gällande display) i stället för att lägga en källa som ingen pumpar.
    appTimerShutdown();
    closeWakeup();
    if (LinuxDisplay* d = LinuxDisplay::current()) {
        if (d->fd() >= 0)
            removePollSource(&g_displaySource);
        delete d;
        LinuxDisplay::current() = nullptr;
    }
}

std::string clipboardText()
{
    LinuxDisplay* d = LinuxDisplay::current();
    return d ? d->clipboardText() : std::string();
}

// SKÄRMARNA — den gällande displayens svar: X11:s ur
// XRandR och `_NET_WORKAREA`, Waylands och headless tomt (ingen skärmrymd
// respektive ingen skärm). Tomt också före init.
std::vector<ScreenInfo> screens()
{
    const LinuxDisplay* d = LinuxDisplay::current();
    return d ? d->placementScreens() : std::vector<ScreenInfo>{};
}

void setClipboardText(const std::string& utf8)
{
    if (LinuxDisplay* d = LinuxDisplay::current())
        d->setClipboardText(utf8);
}

// ------------------------------------------------------- openUrl / openFile

namespace {

// Programmet i PATH som en körbar fil — FÖRE fork, för barnet får aldrig
// söka. Tom när det inte finns.
std::string findInPath(const char* prog)
{
    const char*       env  = std::getenv("PATH");
    const std::string path = env && *env ? env : "/usr/local/bin:/usr/bin:/bin";
    std::size_t       från = 0;
    while (från <= path.size()) {
        const std::size_t slut = std::min(path.find(':', från), path.size());
        const std::string dir  = path.substr(från, slut - från);
        const std::string fil  = (dir.empty() ? std::string(".") : dir) + "/" + prog;
        std::error_code   ec;
        if (std::filesystem::is_regular_file(fil, ec) &&
            ::access(fil.c_str(), X_OK) == 0)
            return fil;
        från = slut + 1;
    }
    return {};
}

// Öppningen openUrl och openFile delar: `xdg-open <mål>`, där målet är en
// adress eller en filsökväg. `vad` är anroparens namn i loggen.
bool xdgOpen(const std::string& mal, const char* vad)
{
    // fork + execve, ALDRIG system()/popen (skalet skulle tolka målet).
    // Programmet, argv och envp byggs FÖRE fork: barnet gör bara fork,
    // execve och _exit — ingen allokering och ingen PATH-sökning i en process
    // där en annan tråds lås kan vara låst för alltid. Miljön är processens
    // UTAN grafikkortsvalet GlazeUI satte: en
    // webbläsare ska inte hamna på den diskreta GPU:n för att appen gjorde
    // det. DUBBEL fork: barnbarnet ärvs av init, så ingen zombie blir kvar
    // och pumpen slipper en waitpid — vi väntar bara in mellansteget, som dör
    // omedelbart.
    const std::string prog = findInPath("xdg-open");
    if (prog.empty()) {
        log("%s: xdg-open finns inte i PATH (%s)", vad, mal.c_str());
        return false;
    }
    std::string              arg0   = "xdg-open";
    std::string              arg1   = mal;
    char*                    argv[] = {arg0.data(), arg1.data(), nullptr};
    std::vector<std::string> miljö  = linuxChildEnvironment();
    std::vector<char*>       envp;
    envp.reserve(miljö.size() + 1);
    for (std::string& e : miljö)
        envp.push_back(e.data());
    envp.push_back(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        log("%s: fork misslyckades (errno %d)", vad, errno);
        return false;
    }
    if (pid == 0) {
        const pid_t grand = ::fork();
        if (grand == 0) {
            ::execve(prog.c_str(), argv, envp.data());
            ::_exit(127);   // exec vägrades — inget vi kan göra härifrån
        }
        // Exit-statusen är mellanstegets ENDA kanal tillbaka: 0 = barnbarnet
        // startade, annat = forken misslyckades.
        ::_exit(grand < 0 ? 1 : 0);
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            log("%s: waitpid misslyckades (errno %d)", vad, errno);
            return false;
        }
    }
    // true betyder "begäran skickad", inte "sidan visades" — vad xdg-open
    // gör sedan är osynligt härifrån (barnbarnet ärvs av init).
    const bool spawned = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!spawned)
        log("%s: kunde inte starta xdg-open (%s)", vad, mal.c_str());
    return spawned;
}

} // namespace

bool openUrl(const std::string& url)
{
    if (url.empty())
        return false;
    return xdgOpen(url, "openUrl");
}

bool openFile(const std::string& path)
{
    if (path.empty())
        return false;
    // xdg-open tolkar en relativ sökväg som börjar med '-' som en flagga, och
    // svaret om filen finns går inte att få tillbaka ur dubbelforken — så
    // sökvägen görs absolut (börjar alltid med '/') och finns-kontrolleras
    // här, som ShellExecute och NSWorkspace gör åt de andra två.
    std::error_code ec;
    const std::filesystem::path full = std::filesystem::absolute(path, ec);
    if (ec || !std::filesystem::exists(full, ec)) {
        log("openFile: filen finns inte (%s)", path.c_str());
        return false;
    }
    return xdgOpen(full.string(), "openFile");
}

// ------------------------------------------------------------ knownPlaces

namespace {

bool isDirectory(const std::string& p)
{
    std::error_code ec;
    return !p.empty() && std::filesystem::is_directory(p, ec);
}

// $HOME, och lösenordsdatabasen när variabeln saknas (en tjänst som
// startats utan miljö). Tom sträng = ingen hemkatalog att visa.
std::string homeDirectory()
{
    if (const char* h = std::getenv("HOME"); h && *h)
        return h;
    struct passwd  pw{};
    struct passwd* svar = nullptr;
    char           buf[4096];
    if (::getpwuid_r(::getuid(), &pw, buf, sizeof buf, &svar) == 0 && svar &&
        svar->pw_dir)
        return svar->pw_dir;
    return std::string();
}

// XDG USER DIRS — `$XDG_CONFIG_HOME/user-dirs.dirs` (förval
// `~/.config/user-dirs.dirs`), skriven av xdg-user-dirs-update:
//
//     XDG_DESKTOP_DIR="$HOME/Skrivbord"
//
// Formatet är ett skalfragment men en DELMÄNGD utan andra konstruktioner:
// värdet är citerat och börjar med `$HOME` eller är en absolut sökväg, och
// `\` skyddar nästa tecken. Allt annat hoppas över hellre än gissas. Saknas
// nyckeln gäller verktygets egna förval: skrivbordet är `$HOME/Desktop`,
// de andra pekar på hemmet — alltså avstängda.
std::string xdgUserDir(const std::string& hem, const std::string& nyckel)
{
    const char* cfg = std::getenv("XDG_CONFIG_HOME");
    const std::string fil = (cfg && *cfg ? std::string(cfg) : hem + "/.config") +
                            "/user-dirs.dirs";
    std::ifstream in(fil);
    std::string   rad;
    while (std::getline(in, rad)) {
        const std::size_t a = rad.find_first_not_of(" \t");
        if (a == std::string::npos || rad[a] == '#')
            continue;
        const std::size_t lika = rad.find('=', a);
        if (lika == std::string::npos || rad.compare(a, lika - a, nyckel) != 0)
            continue;
        const std::size_t v0 = rad.find('"', lika);
        const std::size_t v1 = rad.find_last_of('"');
        if (v0 == std::string::npos || v1 <= v0)
            return std::string();
        std::string värde;
        for (std::size_t i = v0 + 1; i < v1; ++i) {
            if (rad[i] == '\\' && i + 1 < v1)
                ++i;
            värde.push_back(rad[i]);
        }
        if (värde.rfind("$HOME", 0) == 0)
            return hem + värde.substr(5);
        return !värde.empty() && värde[0] == '/' ? värde : std::string();
    }
    return nyckel == "XDG_DESKTOP_DIR" ? hem + "/Desktop" : hem;
}

// /proc/self/mounts skyddar blanksteg, tabb, radbrytning och `\` som oktala
// sekvenser (`\040`) — en volymetikett med mellanslag är vanlig.
std::string mountUnescape(const std::string& s)
{
    auto oktal = [](char ch) { return ch >= '0' && ch <= '7'; };
    std::string ut;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 3 < s.size() && oktal(s[i + 1]) &&
            oktal(s[i + 2]) && oktal(s[i + 3])) {
            ut.push_back(static_cast<char>((s[i + 1] - '0') * 64 +
                                           (s[i + 2] - '0') * 8 +
                                           (s[i + 3] - '0')));
            i += 3;
        } else {
            ut.push_back(s[i]);
        }
    }
    return ut;
}

} // namespace

std::vector<Place> knownPlaces()
{
    std::vector<Place> ut;
    const std::string  hem = homeDirectory();
    if (isDirectory(hem))
        ut.push_back(Place{PlaceKind::Home, hem, {}});
    if (!hem.empty()) {
        auto mapp = [&](PlaceKind k, const char* nyckel) {
            std::string p = xdgUserDir(hem, nyckel);
            while (p.size() > 1 && p.back() == '/')
                p.pop_back();
            // Pekar mappen på hemmet SJÄLVT är den avstängd (XDG).
            if (isDirectory(p) && p != hem)
                ut.push_back(Place{k, p, {}});
        };
        mapp(PlaceKind::Desktop, "XDG_DESKTOP_DIR");
        mapp(PlaceKind::Documents, "XDG_DOCUMENTS_DIR");
        mapp(PlaceKind::Downloads, "XDG_DOWNLOAD_DIR");
        mapp(PlaceKind::Pictures, "XDG_PICTURES_DIR");
    }

    // VOLYMERNA: roten först, därefter det som är MONTERAT under /media och
    // /run/media — där udisks och skrivbordens automontering lägger löstagbara
    // enheter. /proc/self/mounts är kärnans egen lista, så bara det som
    // faktiskt är monterat visas (en tom katalog under /media gör det inte).
    ut.push_back(Place{PlaceKind::Volume, "/", "/"});
    std::ifstream in("/proc/self/mounts");
    std::string   enhet, punkt, rest;
    while (in >> enhet >> punkt && std::getline(in, rest)) {
        const std::string p = mountUnescape(punkt);
        if (p.rfind("/media/", 0) != 0 && p.rfind("/run/media/", 0) != 0)
            continue;
        if (!isDirectory(p))
            continue;
        ut.push_back(Place{PlaceKind::Volume, p,
                           p.substr(p.find_last_of('/') + 1)});
    }
    return ut;
}

// Linux har inget dolt-attribut: PUNKTEN är hela konventionen, och den
// tillämpar filväljaren själv (gfx/platform.h).
bool isHiddenFile(const std::string&)
{
    return false;
}

// ---------------------------------------------------------------- pumpen

namespace {

// libdbus-fallgropen: ett blockerande portal-anrop (systemTheme() körs
// per repaint) läser ÄVEN inkommande signaler in i libdbus interna kö —
// socket-fd:n blir då aldrig läsbar och epoll ser inget, så en
// SettingChanged som anländer under anropet skulle svälgas för alltid.
// Dränera därför köad dispatch-data i varje pumpvarv (ren statuskoll på
// redan LÄST data — ingen polling, ingen läsning av fd:n).
void dbusDispatchPending()
{
    DBusConnection* c = dbusConn();
    if (!c)
        return;
    while (dbus_connection_get_dispatch_status(c) ==
           DBUS_DISPATCH_DATA_REMAINS)
        dbus_connection_dispatch(c);
}

// Ett pumpvarv: flusha/dispatcha displayen, sov i epoll_wait (timeoutMs;
// -1 = tills något händer), kör källornas onReady. Events hamnar i kön.
void pumpOnce(int timeoutMs)
{
    dbusDispatchPending();   // signaler svalda av blockerande portal-anrop
    LinuxDisplay* d = LinuxDisplay::current();
    if (d)
        d->prepoll();
    if (!queue().empty()) {
        if (d)
            d->abortPoll();   // Wayland: cancel_read — läslåset släpps
        return;
    }

    epoll_event evs[16];
    int n = ::epoll_wait(epollFd(), evs, 16, timeoutMs);
    if (n < 0 && errno == EINTR)
        n = 0;

    bool displayReady = false;
    for (int i = 0; i < n; ++i) {
        auto* src = static_cast<PollSource*>(evs[i].data.ptr);
        if (src && src->isDisplay)
            displayReady = true;
    }
    if (d)
        d->postpoll(displayReady);
    for (int i = 0; i < n; ++i) {
        auto* src = static_cast<PollSource*>(evs[i].data.ptr);
        if (src && !src->isDisplay && src->onReady)
            src->onReady();
    }
}

} // namespace

bool waitEvent(Event& e)
{
    for (;;) {
        if (popQueued(e))
            return true;
        pumpOnce(-1);   // blockerar — ~0 % CPU i vila
    }
}

bool pollEvent(Event& e)
{
    if (popQueued(e))
        return true;
    pumpOnce(0);
    return popQueued(e);
}

// ---------------------------------------------------------- LinuxWindow

LinuxWindow::~LinuxWindow()
{
    stopAllTimers();
}

bool LinuxWindow::processEvent(const Event& e)
{
    switch (e.type) {
    // false = "det här fönstret begär stängning" — både per-fönster-
    // begäran (krysset, popup-dismissal) och app-avslut (Quit når alla).
    case EventType::Quit:
    case EventType::WindowCloseRequested:
        return false;
    default:
        break;
    }
    return true;
}

void LinuxWindow::renderFrame()
{
    if (!canRender())
        return;
    renderer_.beginFrame();      // binder retained-FBO:n
    if (renderCb_)
        renderCb_(*this);        // frame-innehållet → FBO:n
    renderer_.composite();       // FBO → backbuffer (skriver hela ytan)
    drawChrome();                // chrome direkt i backbuffern, ovanpå
    if (overlayCb_)
        overlayCb_(*this);       // informativt topplager — självstädande
    const bool swap = shouldSwap();
    if (swap) {
        // Wayland: frame-callback-pacing — presentationens väntan
        // (`RenderStats::presentNs`); swappen räknar renderaren.
        const auto väntan = std::chrono::steady_clock::now();
        beforeSwap();
        renderer_.addPresentNs(static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - väntan)
                .count()));
    }
    renderer_.present(swap);
}

void LinuxWindow::drawChrome()
{
    if (chrome_ != Chrome::Custom || maximized())
        return;
    // Bara den tunna ramen — TopBar-widgeten ritar captionen.
    // Är fönstret RUNDAT måste ramen följa hörnen, annars ligger en
    // rektangulär linje utanför den bortklippta formen.
    const Sizef ps = pixelSize();
    const Rectf r{0.0f, 0.0f, ps.w, ps.h};
    if (chromeRadius_ > 0.0f)
        // Bredden skrivs UT: 0 är inte hårlinje här utan en kontur på noll,
        // alltså ingenting alls — fasaden har hårlinjen som DEFAULTARGUMENT
        // (1 px), och overriden i GlRenderer upprepar det inte.
        renderer_.strokeRoundedRect(r, chromeRadius_, chromeColors_.frame,
                                    1.0f);
    else
        renderer_.strokeRect(r, chromeColors_.frame);
}

Rectf LinuxWindow::contentArea() const
{
    const Sizef ps = pixelSize();
    const float th = titlebarHeight();
    return Rectf{0.0f, th, ps.w, ps.h - th};
}

float LinuxWindow::titlebarHeight() const
{
    return chrome_ == Chrome::Custom ? captionH_ * uiScale() : 0.0f;
}

bool LinuxWindow::captureFrame(std::vector<std::uint8_t>& rgba, int& w,
                               int& h)
{
    return renderer_.captureRetained(rgba, w, h);
}

HitRole LinuxWindow::hitRoleAt(float x, float y) const
{
    // STORLEKSMARGINALEN: marginalens kant först — den
    // ligger utanför klientytan, där kartan inte har några zoner — och
    // kartans resize-zoner innanför hoppas över medan marginalen finns. Ett
    // fast fönsters marginal är inert (`frameResizable_`): Client.
    if (frameMargin_ > 0) {
        const Sizef   ps   = pixelSize();
        const HitRole kant = frame::marginRole(
            x, y, ps.w, ps.h, static_cast<float>(frameMargin_),
            static_cast<float>(frame::kCornerLogical) * uiScale());
        if (kant != HitRole::Client)
            return frameResizable_ ? kant : HitRole::Client;
    }
    for (const auto& r : hitRegions_) {
        if (!r.rect.contains(x, y))
            continue;
        if (frameMargin_ > 0 && frame::isResizeRole(r.role))
            continue;
        return r.role;
    }
    return HitRole::Client;
}

// Fönstrets timers går genom samma källa som appens (`TimerSource`,
// linux_timer.cpp) — armeringen, dräneringen och felvägarna står där.
void LinuxWindow::startTimer(std::uint32_t id, std::uint32_t ms)
{
    if (windowId_ == 0 || id == 0)
        return;
    stopTimer(id);   // samma id = starta om perioden (fasadens kontrakt)
    if (std::unique_ptr<TimerSource> t = TimerSource::make(windowId_, id, ms))
        timers_[id] = std::move(t);
}

void LinuxWindow::stopTimer(std::uint32_t id)
{
    timers_.erase(id);   // källans destruktor: ur epollen, fd:n stängd
}

void LinuxWindow::stopAllTimers()
{
    timers_.clear();
}

// ------------------------------------------------------- HeadlessWindow
//
// glaze-render utan display (SSH): inget OS-fönster alls — renderern kör
// surfacelöst (EGL_NO_SURFACE) och allt ritas i retained-FBO:n, som läses
// via captureFrame. Fasadmetoder som kräver en skärm degraderar tyst. Ingen
// pekare, alltså inget tryck att avbryta: `MouseCaptureLost` kommer aldrig
// härifrån.

namespace {

class HeadlessWindow final : public LinuxWindow {
public:
    ~HeadlessWindow() override { shutdown(); }

    bool init(const WindowDesc& desc) override
    {
        chrome_      = desc.chrome;
        transparent_ = desc.transparent;
        hidden_      = true;
        title_       = desc.title;
        if (transparent_ && chrome_ != Chrome::None)
            chrome_ = Chrome::None;
        windowId_ = nextWindowId();
        // Ingen skärm = OS-skala 1.0 — men ANVÄNDARSKALAN gäller ändå
        // (uiScale = 1.0 × userScale), så glaze-render --scale
        // fungerar även surfacelöst. Logisk storlek är invarianten.
        logicalW_ = desc.width > 0 ? desc.width : 1;
        logicalH_ = desc.height > 0 ? desc.height : 1;
        w_ = physFromLogicalHeadless(logicalW_);
        h_ = physFromLogicalHeadless(logicalH_);

        LinuxDisplay* d = LinuxDisplay::current();
        if (!d)
            return false;
        if (!renderer_.init(d->egl(), nullptr, EGL_NO_SURFACE, w_, h_,
                            transparent_))
            return false;
        registerLinuxWindow(this);

        Event e;
        e.type     = EventType::WindowShown;
        e.windowId = windowId_;
        pushEvent(e);
        return true;
    }

    void shutdown() override
    {
        if (windowId_ == 0)
            return;
        stopAllTimers();
        unregisterLinuxWindow(this);
        renderer_.shutdownRenderer();
        windowId_ = 0;
    }

    void raise() override {}
    float uiScale() const override { return userScale(); }
    // SKÄRMENS mm är DEGRADERAD headless: det finns ingen skärm att mäta,
    // så svaret är reserven — med orsaken utskriven.
    DisplayMetrics displayMetrics() const override
    {
        return display::assumed("headless",
                                "ingen skärm (headless — glaze-render utan "
                                "display)");
    }
    bool  maximized() const override { return false; }
    // FÖNSTRETS GEOMETRI är DEGRADERAD headless: ingen
    // skärmrymd (hasPosition false) och inget annat läge än normalläget —
    // bara den logiska storleken är sann.
    WindowPlacement placement() const override
    {
        WindowPlacement p;
        if (windowId_ == 0)
            return p;
        p.width  = logicalW_;
        p.height = logicalH_;
        return p;
    }
    void  minimize() override {}
    void  maximize() override {}
    void  restore() override {}
    void  setSize(int w, int h) override
    {
        if (windowId_ == 0 || w <= 0 || h <= 0)
            return;
        logicalW_ = w;
        logicalH_ = h;
        w_ = physFromLogicalHeadless(w);
        h_ = physFromLogicalHeadless(h);
        renderer_.resize(w_, h_);
    }
    void userScaleChanged() override
    {
        // US.2: ny fysisk storlek ur lagrad logisk, eventet EFTER
        // renderer-resizen (samma ordning som skärm-backenderna).
        if (windowId_ == 0)
            return;
        w_ = physFromLogicalHeadless(logicalW_);
        h_ = physFromLogicalHeadless(logicalH_);
        renderer_.resize(w_, h_);
        Event e;
        e.type     = EventType::WindowDisplayScaleChanged;
        e.windowId = windowId_;
        pushEvent(e);
    }
    // Storleksgränser är DEGRADERADE headless: det finns
    // ingen fönsterhanterare som kan neka en resize, och ingen användare
    // som kan dra i en kant. Ingen klampning görs heller — `setSize` är
    // programmatisk och programmet får den storlek det ber om, precis
    // som `setPosition` är en no-op här.
    void setSizeLimits(int, int, int, int) override {}
    // ALLTID ÖVERST är av samma skäl DEGRADERAT headless:
    // det finns ingen fönsterhanterare med en z-ordning att ligga överst
    // i. Tyst no-op — och `topmost()` svarar false, alltså sanningen.
    void setTopmost(bool) override {}
    bool topmost() const override { return false; }
    void setTitle(const std::string& t) override { title_ = t; }
    void setVisible(bool) override {}
    void setPosition(int, int) override {}
    Pointf clientToScreen(float px, float py) const override
    {
        return Pointf{px, py};   // degraderad — ingen skärmrymd finns
    }
    Rectf screenArea() const override { return Rectf{0, 0, 0, 0}; }
    void releaseMouseCapture() override {}
    // Ingen pekare och inget tryck att ta över — och
    // `desc.showInactive` har inget att visa: båda dokumenterade
    // degraderingar i fasadens tabeller.
    bool captureMouse() override { return false; }
    bool hasMouseCapture() const override { return false; }
    // MUSPEKAREN är DEGRADERAD headless: det finns ingen pekare.
    // Formen LAGRAS (basens `cursor()` svarar med den), precis som titeln
    // lagras fast ingen namnlist visar den.
    void setCursor(PointerShape shape) override { cursor_ = shape; }
    // PROGRAMIKONEN är DEGRADERAD headless: ingen
    // fönsterhanterare och inget aktivitetsfält att visa den i. Tyst no-op —
    // ramarna lagras inte, för ingenting läser dem — och `iconApplied()`
    // svarar false, alltså sanningen.
    void setIcon(const WindowIcon&) override {}
    bool iconApplied() const override { return false; }

protected:
    bool  shouldSwap() const override { return false; }   // ingen backbuffer
    Sizef pixelSize() const override
    {
        return Sizef{static_cast<float>(w_), static_cast<float>(h_)};
    }

private:
    static int physFromLogicalHeadless(int logical)
    {
        return std::max(1, static_cast<int>(std::lround(
                               logical * userScale())));
    }

    int w_ = 1, h_ = 1;                  // fysiska px (logisk × userScale)
    int logicalW_ = 1, logicalH_ = 1;    // invarianten
};

class HeadlessDisplay final : public LinuxDisplay {
public:
    static HeadlessDisplay* create()
    {
        auto* d = new HeadlessDisplay();
        if (!d->initEgl()) {
            delete d;
            return nullptr;
        }
        return d;
    }

    ~HeadlessDisplay() override
    {
        if (egl_ != EGL_NO_DISPLAY)
            eglTerminate(egl_);
    }

    EGLDisplay egl() const override { return egl_; }
    std::unique_ptr<Window> makeWindow() override
    {
        return std::make_unique<HeadlessWindow>();
    }

private:
    bool initEgl()
    {
        // EGL_MESA_platform_surfaceless — display utan fönstersystem.
        constexpr EGLenum kPlatformSurfacelessMesa = 0x31DD;
        auto getPlatformDisplay =
            reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
                eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (getPlatformDisplay)
            egl_ = getPlatformDisplay(kPlatformSurfacelessMesa,
                                      EGL_DEFAULT_DISPLAY, nullptr);
        if (egl_ == EGL_NO_DISPLAY)
            egl_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (egl_ == EGL_NO_DISPLAY ||
            eglInitialize(egl_, nullptr, nullptr) != EGL_TRUE) {
            log("headless: EGL kunde inte initieras");
            egl_ = EGL_NO_DISPLAY;
            return false;
        }
        return true;
    }

    EGLDisplay egl_ = EGL_NO_DISPLAY;
};

} // namespace

LinuxDisplay* createHeadlessDisplay()
{
    return HeadlessDisplay::create();
}

// ---------------------------------------------------------------- fabrik
//
// Tvåfasmönstret: createWindow() anropas i AppWindow-konstruktorn — FÖRE
// gfx::init (App skapas lat vid första show). Vilken konkret fönstertyp
// som gäller vet vi först när displayen valts, så fabriken ger en PROXY
// som skapar det riktiga fönstret i init() och vidarebefordrar allt.
// Pre-init-anrop är no-op-säkra precis som fasaden kräver.

namespace {

class ProxyWindow final : public Window, public InnerWindowHolder {
public:
    Window* innerWindow() override { return inner_.get(); }

    bool init(const WindowDesc& desc) override
    {
        if (inner_)
            return true;
        LinuxDisplay* d = LinuxDisplay::current();
        if (!d) {
            log("Window::init: gfx::init har inte körts");
            return false;
        }
        // AKTIVITETSFÄLTET är en DUMMY på Linux: X11:s
        // `_NET_WM_STATE_SKIP_TASKBAR` är inte byggt och Wayland saknar
        // formen. Begäran loggas HÄR, en gång per process, så att Wayland,
        // X11 och headless täcks på ett ställe — `desc` går orörd vidare.
        if (!desc.showInTaskbar) {
            static bool loggat = false;
            if (!loggat) {
                loggat = true;
                log("showInTaskbar = false är inte byggt på Linux — fönstret "
                    "behåller sin post i aktivitetsfältet");
            }
        }
        // `desc` går ORÖRD vidare — också `showInactive`
        // och de andra attributen: inget av dem behöver lagras här, för det
        // inre fönstret läser dem i sin init (proxyfällan gäller bara
        // sättare som körs FÖRE init).
        inner_ = d->makeWindow();
        if (!inner_)
            return false;
        if (!inner_->init(desc)) {
            inner_.reset();
            return false;
        }
        if (renderCb_)
            inner_->setRenderCallback(renderCb_);
        if (overlayCb_)
            inner_->setOverlayCallback(overlayCb_);
        // Gränser satta FÖRE init (pre-init-lagringen ovan) — bygget
        // fångar en glömd override som kompileringsfel, men INTE en
        // tappad vidarebefordran här.
        if (hasLimits_)
            inner_->setSizeLimits(limMinW_, limMinH_, limMaxW_, limMaxH_);
        // ALLTID ÖVERST: samma pre-init-lagring, samma
        // fälla. `desc.topmost` bär startvärdet, men en sättare som körts
        // före `show()` finns bara här — och `desc` kopieras inte hit.
        if (hasTopmost_)
            inner_->setTopmost(topmost_);
        // MUSPEKAREN — samma fälla: en form satt
        // före `init()` finns bara här. Grindas av splitter_gate (formen
        // satt före show() ska vara det INRE fönstrets efter show()).
        if (hasCursor_)
            inner_->setCursor(cursor_);
        // PROGRAMIKONEN — samma fälla:
        // ramar satta före `init()` finns bara här.
        if (hasIcon_)
            inner_->setIcon(icon_);
        return true;
    }

    void shutdown() override
    {
        if (inner_) {
            inner_->shutdown();
            inner_.reset();   // nästa init väljer typ på nytt (åter-show)
        }
    }

    WindowId id() const override { return inner_ ? inner_->id() : 0; }
    void raise() override
    {
        if (inner_)
            inner_->raise();
    }
    bool processEvent(const Event& e) override
    {
        return inner_ ? inner_->processEvent(e) : true;
    }
    void setRenderCallback(RenderFn fn) override
    {
        renderCb_ = std::move(fn);
        if (inner_)
            inner_->setRenderCallback(renderCb_);
    }
    void setOverlayCallback(RenderFn fn) override
    {
        overlayCb_ = std::move(fn);
        if (inner_)
            inner_->setOverlayCallback(overlayCb_);
    }
    void renderFrame() override
    {
        if (inner_)
            inner_->renderFrame();
    }
    void drawChrome() override
    {
        if (inner_)
            inner_->drawChrome();
    }
    Rectf contentArea() const override
    {
        return inner_ ? inner_->contentArea() : Rectf{0, 0, 0, 0};
    }
    float titlebarHeight() const override
    {
        return inner_ ? inner_->titlebarHeight() : 0.0f;
    }
    float uiScale() const override
    {
        return inner_ ? inner_->uiScale() : 1.0f;
    }
    void userScaleChanged() override
    {
        if (inner_)
            inner_->userScaleChanged();
    }
    // SKÄRMENS mm — den SJÄTTE implementationen. Det
    // finns inget att lagra före init (det är en MÄTNING och ingen begäran),
    // men efter init är det inre fönstrets svar det enda sanna: ett eget
    // standardsvar hade sett ut som en giltig antagen mätning, och appen
    // hade aldrig fått veta skärmens mått. Grindas av display_gate.
    DisplayMetrics displayMetrics() const override
    {
        return inner_ ? inner_->displayMetrics()
                      : display::assumed("", "fönstret är inte skapat än");
    }
    bool maximized() const override
    {
        return inner_ && inner_->maximized();
    }
    // FÖNSTRETS GEOMETRI — den SJÄTTE implementationen,
    // och samma fälla som skärmens mm: bygget fångar en glömd override men
    // aldrig ett eget svar efter init. Före init finns ingen geometri att
    // lagra — den är en SPÅRNING och ingen begäran, och startläget följer med
    // i `desc` till det inre fönstrets init. Grindas av geometry_gate.
    WindowPlacement placement() const override
    {
        return inner_ ? inner_->placement() : WindowPlacement{};
    }
    // Ytans FAKTISKA alfa-status — proxyn vidarebefordrar, för det är det
    // inre fönstret som vet om begäran degraderades.
    bool transparent() const override
    {
        return inner_ && inner_->transparent();
    }
    void minimize() override
    {
        if (inner_)
            inner_->minimize();
    }
    void maximize() override
    {
        if (inner_)
            inner_->maximize();
    }
    void restore() override
    {
        if (inner_)
            inner_->restore();
    }
    bool takeDirty() override { return inner_ && inner_->takeDirty(); }
    bool retainedValid() const override
    {
        return inner_ && inner_->retainedValid();
    }
    void setSize(int w, int h) override
    {
        if (inner_)
            inner_->setSize(w, h);
    }
    // STORLEKSGRÄNSERNA. Proxyn är en FEMTE implementation
    // och inte bara en vidarebefordran: anrop FÖRE `init()` har inget
    // inre fönster att skicka till, så de måste LAGRAS här och skickas
    // vidare när det riktiga fönstret skapas. Lagring i `LinuxWindow`-
    // basen hade inte hjälpt — proxyn ärver `Window` direkt.
    //
    // (Startgränserna kommer normalt via `WindowDesc`, men fasaden lovar
    // att sättaren fungerar när som helst — och `desc` kopieras inte hit.)
    void setSizeLimits(int minW, int minH, int maxW, int maxH) override
    {
        limMinW_ = minW;
        limMinH_ = minH;
        limMaxW_ = maxW;
        limMaxH_ = maxH;
        hasLimits_ = true;
        if (inner_)
            inner_->setSizeLimits(minW, minH, maxW, maxH);
    }
    // ALLTID ÖVERST — proxyn är den SJÄTTE implementationen
    // och samma läxa som storleksgränserna: bygget fångar en glömd
    // override, men INTE en tappad vidarebefordran i `init()`.
    void setTopmost(bool on) override
    {
        topmost_    = on;
        hasTopmost_ = true;
        if (inner_)
            inner_->setTopmost(on);
    }
    // Svaret ska vara vad som FAKTISKT gäller: efter init är det inre
    // fönstrets svar sanningen (Wayland säger alltid false), före init
    // finns bara begäran.
    bool topmost() const override
    {
        return inner_ ? inner_->topmost() : topmost_;
    }
    // MUSPEKAREN — LAGRA före init, VIDAREBEFORDRA efter. Bygget
    // fångar en glömd override, men varken en tappad vidarebefordran här
    // eller i `init()`. Gettern frågar det inre fönstret efter init, så en
    // tappad vidarebefordran SYNS (det inre svarar med pilen).
    void setCursor(PointerShape shape) override
    {
        cursor_    = shape;
        hasCursor_ = true;
        if (inner_)
            inner_->setCursor(shape);
    }
    PointerShape cursor() const override
    {
        return inner_ ? inner_->cursor() : cursor_;
    }
    // PROGRAMIKONEN — LAGRA alltid, VIDAREBEFORDRA efter init.
    // Ramarna sparas även efter init: ett åter-`init()` (åter-show) skapar
    // ett NYTT inre fönster, som annars hade stått utan ikon tills appen
    // satte den igen. Gettern frågar det inre fönstret — vad som FAKTISKT
    // gäller — och svarar false före init, när ingen ikon kan visas.
    void setIcon(const WindowIcon& icon) override
    {
        icon_    = icon;
        hasIcon_ = true;
        if (inner_)
            inner_->setIcon(icon_);
    }
    bool iconApplied() const override
    {
        return inner_ && inner_->iconApplied();
    }
    void setTitle(const std::string& t) override
    {
        if (inner_)
            inner_->setTitle(t);
    }
    void setPosition(int x, int y) override
    {
        if (inner_)
            inner_->setPosition(x, y);
    }
    Pointf clientToScreen(float px, float py) const override
    {
        return inner_ ? inner_->clientToScreen(px, py) : Pointf{0.0f, 0.0f};
    }
    Rectf screenArea() const override
    {
        return inner_ ? inner_->screenArea() : Rectf{0, 0, 0, 0};
    }
    void setVisible(bool on) override
    {
        if (inner_)
            inner_->setVisible(on);
    }
    void setChromeRadius(float radius) override
    {
        if (inner_)
            inner_->setChromeRadius(radius);
    }
    void setChromeColors(const ChromeColors& c) override
    {
        chromeColors_ = c;
        if (inner_)
            inner_->setChromeColors(c);
    }
    void setHitRegions(std::vector<HitRegion> regions) override
    {
        if (inner_)
            inner_->setHitRegions(std::move(regions));
    }
    void setCaptionHeight(float logicalPx) override
    {
        if (inner_)
            inner_->setCaptionHeight(logicalPx);
    }
    void releaseMouseCapture() override
    {
        if (inner_)
            inner_->releaseMouseCapture();
    }
    // Vidarebefordras efter init; före finns inget
    // tryck att ta över — false, inget att lagra. Frågan likadant.
    bool captureMouse() override
    {
        return inner_ && inner_->captureMouse();
    }
    bool hasMouseCapture() const override
    {
        return inner_ && inner_->hasMouseCapture();
    }
    void startTimer(std::uint32_t id, std::uint32_t ms) override
    {
        if (inner_)
            inner_->startTimer(id, ms);
    }
    void stopTimer(std::uint32_t id) override
    {
        if (inner_)
            inner_->stopTimer(id);
    }
    void setTextInputActive(bool on) override
    {
        if (inner_)
            inner_->setTextInputActive(on);
    }
    bool captureFrame(std::vector<std::uint8_t>& rgba, int& w,
                      int& h) override
    {
        return inner_ && inner_->captureFrame(rgba, w, h);
    }
    Renderer& renderer() override
    {
        // ALLTID EN GILTIG REFERENS, som D3D11:s och Metals medlemsrenderare:
        // utan ett inre fönster — före init och efter `shutdown()`, som river
        // det — svarar en renderare som inte ritar. En stängd `AppWindow`
        // bygger fortfarande sitt Ctx (`makeCtx`) när ett lånat barn lossas
        // efter stängningen och fokus släpps (dock_gate fallFlytRivning);
        // utan reserven var det en nollpekare här och bara här.
        return inner_ ? inner_->renderer() : idle_;
    }

private:
    std::unique_ptr<Window> inner_;
    // Pre-init-lagring av storleksgränserna (se setSizeLimits ovan).
    int  limMinW_ = 0, limMinH_ = 0, limMaxW_ = 0, limMaxH_ = 0;
    bool hasLimits_ = false;
    // Pre-init-lagring av "alltid överst" (se setTopmost ovan).
    bool topmost_    = false;
    bool hasTopmost_ = false;
    // Pre-init-lagring av muspekarens form (se setCursor ovan).
    PointerShape cursor_    = PointerShape::Arrow;
    bool        hasCursor_ = false;
    // Lagringen av programikonens ramar (se setIcon ovan).
    WindowIcon icon_;
    bool       hasIcon_ = false;
    RenderFn     renderCb_;
    RenderFn     overlayCb_;
    ChromeColors chromeColors_;
    NullRenderer idle_;   // `renderer()` utan inre fönster (se där)
};

} // namespace

LinuxWindow* unwrapWindow(Window* w)
{
    if (!w)
        return nullptr;
    if (auto* h = dynamic_cast<InnerWindowHolder*>(w))
        w = h->innerWindow();
    return static_cast<LinuxWindow*>(w);
}

std::unique_ptr<Window> createWindow()
{
    return std::make_unique<ProxyWindow>();
}

} // namespace gfx
