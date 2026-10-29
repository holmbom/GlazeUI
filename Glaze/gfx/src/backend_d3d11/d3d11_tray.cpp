// Systemfältet på Windows — fasaden gfx/tray.h
// ovanpå `Shell_NotifyIcon` i NOTIFYICON_VERSION_4.
//
// MOTTAGAREN är ett lat skapat, DOLT TOPPNIVÅFÖNSTER (klass `GlazeTray`) och
// inte ett message-only-fönster (HWND_MESSAGE): ett sådant får inga
// broadcasts, och utan `TaskbarCreated` kommer ikonerna inte tillbaka efter
// en Explorer-omstart — eller, från Windows 10, efter ett byte av
// primärskärmens DPI. Det visas aldrig och lever till `trayShutdown()`
// (gfx::shutdown). Ett UPPHÖJT program hade inte hört något alls från det
// icke-upphöjda Explorer (UIPI), så callbacken och TaskbarCreated släpps
// igenom med `ChangeWindowMessageFilterEx`.
//
// HÄNDELSERNA (v4): callbacken `kMsgTray` bär händelsen i LOWORD(lParam),
// ikonens id i HIWORD(lParam) och ankaret i wParam — skärmkoordinater i
// fysiska px, samma rymd som `D3dWindow::setPosition`/`clientToScreen`
// (processen är per-monitor-v2-medveten). De blir `EventType::Tray` med
// windowId 0 genom `pushEvent`, som WndProc:ens övriga events — och puffen
// där hämtar dem ur en GetMessageW-väntan också när skalet SKICKAR
// callbacken i stället för att posta den.
//
//   NIN_SELECT, NIN_KEYSELECT  Click — att föredra framför råa musklick
//                              (Microsofts NotificationIcon-exempel), och
//                              tangentbordets Enter och mellanslag kommer
//                              samma väg. Det NIN_SELECT som följer ett
//                              dubbelklick SVÄLJS (Qt:s regel): ett dubbelklick
//                              är Click + DoubleClick, inte Click +
//                              DoubleClick + Click
//   WM_LBUTTONDBLCLK           DoubleClick
//   WM_CONTEXTMENU             ContextMenu — högerklick, Skift+F10 och
//                              menytangenten (ankaret är då ikonens hörn)
//   WM_MOUSEMOVE               Enter. Leave mäter vi själva: en pollning var
//                              50:e ms (GetCursorPos mot
//                              Shell_NotifyIconGetRect) som BARA går medan
//                              pekaren står på ikonen, och som säger det runt
//                              100 ms sent som mest, som gfx/events.h lovar
//                              (se pollHover). Plus NIN_POPUPCLOSE om den
//                              kommer — NIN_POPUPOPEN/CLOSE ensamma är
//                              rapporterat opålitliga på Windows 11
//
// TIMERS sitter på mottagarfönstret, en per ikon och slag: animationens
// (`setTrayTimer`) blir Tick, hoverns är intern och blir inget event förrän
// Leave. WM_TIMER hämtas bara av pumpen och kommer därför aldrig MITT I ett
// skalanrop härifrån (se `kReaddTimer`).
//
// BILDEN är aktivitetsfältets lilla ikon (SM_CXSMICON) i AKTIVITETSFÄLTETS
// DPI och inte fönstrens: fältet står på `Shell_TrayWnd`s skärm, som kan byta
// skala i drift (då kommer TaskbarCreated, och ikonerna byggs om).
// `programFirst` ⇒ exe:ns ikongrupp nr 1 i den storleken, annars ramen
// valregeln väljer (`pickIconImage`) — samma regel och samma HICON-väg som
// fönstrets programikon (d3d11_window.cpp). HICON:en hålls tills nästa har
// ersatt den, som fönstrets WM_SETICON: då spelar det ingen roll om skalet
// kopierar handtaget direkt eller läser det senare.
//
// TOOLTIPEN är OS:ets egen — NIF_SHOWTIP, för v4 döljer
// den annars — kapad vid 127 UTF-16-enheter på en kodpunktsgräns.
//
// UI-TRÅDENS, som resten av fasaden. Utanför init–shutdown gör ingen
// funktion något (`platformInitialized`), utom `trayAvailable` som svarar
// ändå.

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>

#include "../icon_util.h"
#include "d3d11_backend.h"
#include "gfx/platform.h"
#include "gfx/tray.h"

#include <shellapi.h>          // Shell_NotifyIcon — efter windows.h
#include <shellscalingapi.h>   // GetDpiForMonitor (shcore) — dito

namespace gfx {

namespace {

// Systemfältets callback — ett FÖNSTERmeddelande till mottagaren. Granne med
// postWakeup:s och puffens TRÅDmeddelanden (WM_APP + 0x51/0x52,
// d3d11_platform.cpp), och numret är lika privat: bara skalet skickar det,
// och bara till vårt eget fönster.
constexpr UINT kMsgTray = WM_APP + 0x53;

constexpr wchar_t kTrayClass[] = L"GlazeTray";

// Timer-id:n på mottagarfönstret. Ikonens id ryms i 16 bitar (`kTrayIdMax`),
// så varje slag får ett eget intervall med id:t som förskjutning.
constexpr UINT_PTR kAnimTimerBase  = 0x10000;   // setTrayTimer → Tick
constexpr UINT_PTR kHoverTimerBase = 0x20000;   // intern: mäter Leave
// Halva gfx/events.h:s gräns för ett sent Leave: pollHover kan hoppa över
// en pollning (se där), och två pollningar ska rymmas inom 100 ms (plus
// WM_TIMER:s upplösning, en systemtick på ~16 ms).
constexpr UINT     kHoverPollMs    = 50;

// TASKBARCREATED SKJUTS UPP TILL PUMPEN. Meddelandet är en broadcast, och om
// det SKICKAS kan det levereras MITT I ett Shell_NotifyIconW härifrån — det
// väntar på skalet med SendMessageTimeout, som tar emot skickade meddelanden
// under väntan. Att lägga tillbaka alla ikoner där hade kunnat lägga tillbaka
// den `removeTrayIcon` just tog bort (en spökikon) och riva en HICON som
// anropet utanför höll på med. En timer på kortaste tid hämtas bara av
// pumpen, och flera TaskbarCreated i följd blir EN återuppbyggnad.
constexpr UINT_PTR kReaddTimer = 1;

// En REGISTRERAD ikon — från ett lyckat setTrayIcon till removeTrayIcon.
struct Entry {
    // Den senaste beskrivningen — TaskbarCreated bygger om bilden ur den.
    TrayIconDesc desc;
    // Bilden skalet fick senast; rivs först när nästa har ersatt den.
    HICON        icon = nullptr;
    std::wstring tip;                      // tooltipen skalet har (kapad)
    bool         added          = false;   // skalet har ikonen
    bool         hovered        = false;   // pekaren på ikonen: timern går
    bool         movedSincePoll = false;   // WM_MOUSEMOVE efter pollningen
    bool         suppressSelect = false;   // nästa NIN_SELECT: dubbelklick
    bool         addFailLogged  = false;   // NIM_ADD-felet är loggat
};

// TILLSTÅNDET. Kartan står i namnrymden och inte i en funktion med flit: den
// konstrueras före main och rivs därför EFTER glaze::App (som skapas lat i
// main), så att `trayShutdown` — från ~App via gfx::shutdown — aldrig når en
// riven karta. Efter shutdown rör ingen fasadfunktion den
// (`platformInitialized`), så en `glaze::TrayIcon` som rivs ännu senare inte
// heller gör det.
HWND                    g_hwnd           = nullptr;   // mottagaren (lat)
UINT                    g_taskbarCreated = 0;         // RegisterWindowMessageW
std::map<TrayId, Entry> g_entries;

void pushTray(TrayId id, TrayAction a, float x = 0.0f, float y = 0.0f)
{
    Event e;
    e.type       = EventType::Tray;
    e.windowId   = 0;   // appens, inte ett fönsters (gfx/events.h)
    e.trayId     = id;
    e.trayAction = a;
    e.mouseX     = x;   // ankaret — 0 för Enter, Leave och Tick
    e.mouseY     = y;
    pushEvent(e);
}

// Aktivitetsfältets DPI: skärmen `Shell_TrayWnd` står på — huvudskärmen när
// fältet inte går att hitta (Explorer har inte startat) — i effektiv DPI som
// fönstrens. Frågas vid varje ikonbygge, för fältet kan byta skärm och skala.
UINT taskbarDpi()
{
    const HWND     bar = FindWindowW(L"Shell_TrayWnd", nullptr);
    const HMONITOR mon =
        bar ? MonitorFromWindow(bar, MONITOR_DEFAULTTOPRIMARY)
            : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    UINT dx = 0, dy = 0;
    if (mon && SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy)) &&
        dx > 0)
        return dx;
    return USER_DEFAULT_SCREEN_DPI;
}

// IKONEN i fältets storlek: programfilens ikon först när `programFirst` och
// exe:n har en, annars ramen valregeln väljer. nullptr = ingen bild.
HICON buildIcon(const WindowIcon& image)
{
    const int side = GetSystemMetricsForDpi(SM_CXSMICON, taskbarDpi());
    HICON     icon = image.programFirst ? programIcon(side) : nullptr;
    if (!icon) {
        if (const IconImage* ram = pickIconImage(image, side))
            icon = iconFromImage(*ram);
    }
    return icon;
}

// TOOLTIPEN som szTip tar den: UTF-16, högst 127 enheter före NUL:en, kapad
// på en kodpunktsgräns — ett surrogatpar delas aldrig.
std::wstring tipText(const std::string& utf8)
{
    constexpr std::size_t kMax =
        sizeof(NOTIFYICONDATAW::szTip) / sizeof(WCHAR) - 1;
    std::wstring w = toWide(utf8);
    if (w.size() > kMax) {
        std::size_t n = kMax;
        if (IS_HIGH_SURROGATE(w[n - 1]))
            --n;   // den låga halvan låg bortom kapningen
        w.resize(n);
    }
    return w;
}

NOTIFYICONDATAW iconData(TrayId id)
{
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g_hwnd;
    nid.uID    = id;
    return nid;
}

// IKONEN TILL SKALET. En ikon skalet redan har uppdateras (NIM_MODIFY, med
// tooltipen bara när den ändrats); annars läggs den till (NIM_ADD) och får
// v4-formen (NIM_SETVERSION — efter VARJE add, för versionen är skalets
// tillstånd per ikon och dör med den). Det andra försöket är reserven åt båda
// hållen: ett NIM_MODIFY som skalet inte känner igen betyder att Explorer har
// startat om (ikonen läggs till direkt i stället för att vänta på
// TaskbarCreated), och ett vägrat NIM_ADD kan betyda att ikonen redan finns —
// ett NIM_ADD som överskred skalets tidsgräns kan ha lyckats ändå.
// true = skalet har ikonen nu.
bool place(TrayId id, Entry& e, HICON icon, bool tipChanged)
{
    NOTIFYICONDATAW nid  = iconData(id);
    nid.uCallbackMessage = kMsgTray;
    nid.hIcon            = icon;
    std::copy(e.tip.begin(), e.tip.end(), nid.szTip);   // ≤ 127 + nollad NUL
    if (e.added) {
        nid.uFlags = NIF_ICON | NIF_SHOWTIP | (tipChanged ? NIF_TIP : 0u);
        if (Shell_NotifyIconW(NIM_MODIFY, &nid))
            return true;
        e.added = false;
    }
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
        const DWORD fel = GetLastError();
        if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
            if (!e.addFailLogged) {
                e.addFailLogged = true;
                log("Systemfältet: ikon %u kunde inte läggas till (fel %lu) — "
                    "den läggs till när aktivitetsfältet startar "
                    "(TaskbarCreated)",
                    id, fel);
            }
            return false;
        }
    }
    e.added         = true;
    e.addFailLogged = false;
    nid.uVersion    = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &nid)) {
        static bool loggat = false;
        if (!loggat) {
            loggat = true;
            log("Systemfältet: NOTIFYICON_VERSION_4 vägrades — ikonens "
                "händelser kommer inte fram");
        }
    }
    return true;
}

// Ur skalet — oavsett vad vi tror om ikonen: ett NIM_DELETE på en ikon skalet
// inte har vägras bara, och en spökikon är värre än ett onödigt anrop.
void deleteFromShell(TrayId id)
{
    NOTIFYICONDATAW nid = iconData(id);
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

// Ikonens rect enligt skalet, i fysiska skärm-px. false när skalet inte kan
// svara — ikonen är inte tillagd, eller skalet vet inte var den står.
bool shellIconRect(TrayId id, RECT& r)
{
    NOTIFYICONIDENTIFIER nii{};
    nii.cbSize = sizeof(nii);
    nii.hWnd   = g_hwnd;
    nii.uID    = id;
    return SUCCEEDED(Shell_NotifyIconGetRect(&nii, &r));
}

// HOVERN SLUTAR: timern stannar, och Leave köas om pekaren stod på ikonen.
void endHover(TrayId id, Entry& e)
{
    KillTimer(g_hwnd, kHoverTimerBase + id);
    if (!e.hovered)
        return;
    e.hovered = false;
    pushTray(id, TrayAction::Leave);
}

// HOVERNS POLLNING (var 50:e ms medan pekaren står på ikonen): Leave när
// pekaren inte längre står på ikonens rect — eller när rect:en inte går att
// få, för då står pekaren inte på något skalet visar. MEN ett WM_MOUSEMOVE
// efter förra pollningen är skalets eget besked att pekaren står på ikonen,
// och det väger tyngre än vår geometri: utan det hade en hover-yta som är
// större än rect:en, eller en fråga som tillfälligt inte får svar, gett
// Enter och Leave om vartannat medan pekaren rör sig. Priset är att Leave
// kan komma en pollning senare — därför 50 ms, så att två pollningar ryms
// inom gfx/events.h:s 100 ms.
void pollHover(TrayId id)
{
    const auto it = g_entries.find(id);
    if (it == g_entries.end()) {
        KillTimer(g_hwnd, kHoverTimerBase + id);
        return;
    }
    if (it->second.movedSincePoll) {
        it->second.movedSincePoll = false;
        return;
    }
    POINT      pt{};
    RECT       r{};
    const bool kvar =
        GetCursorPos(&pt) && shellIconRect(id, r) && PtInRect(&r, pt);
    if (!kvar)
        endHover(id, it->second);
}

// CALLBACKEN (v4) → `EventType::Tray`. Tabellen står i filhuvudet.
void onCallback(WPARAM wp, LPARAM lp)
{
    const UINT   vad = LOWORD(lp);
    const TrayId id  = HIWORD(lp);
    const auto   it  = g_entries.find(id);
    if (it == g_entries.end())
        return;   // borttagen — ett meddelande som låg kvar i kön
    Entry& e = it->second;
    // Ankaret med GET_X_LPARAM/GET_Y_LPARAM:s teckenutvidgning (windowsx.h):
    // en skärm till vänster om eller ovanför huvudskärmen har negativa
    // koordinater.
    const float x = static_cast<float>(static_cast<short>(LOWORD(wp)));
    const float y = static_cast<float>(static_cast<short>(HIWORD(wp)));
    switch (vad) {
    case NIN_SELECT:
        if (e.suppressSelect)
            e.suppressSelect = false;   // dubbelklickets — redan DoubleClick
        else
            pushTray(id, TrayAction::Click, x, y);
        break;
    case NIN_KEYSELECT:
        e.suppressSelect = false;
        pushTray(id, TrayAction::Click, x, y);
        break;
    case WM_LBUTTONDOWN:
        // Ett nytt tryck: ett dubbelklick vars NIN_SELECT aldrig kom
        // (knappen släpptes utanför ikonen) får inte svälja nästa klick.
        e.suppressSelect = false;
        break;
    case WM_LBUTTONDBLCLK:
        e.suppressSelect = true;
        pushTray(id, TrayAction::DoubleClick, x, y);
        break;
    case WM_CONTEXTMENU:
        pushTray(id, TrayAction::ContextMenu, x, y);
        break;
    case WM_MOUSEMOVE:
        e.movedSincePoll = true;   // se pollHover
        if (!e.hovered) {
            e.hovered = true;
            SetTimer(g_hwnd, kHoverTimerBase + id, kHoverPollMs, nullptr);
            pushTray(id, TrayAction::Enter);
        }
        break;
    case NIN_POPUPCLOSE:
        endHover(id, e);
        break;
    default:
        // Knapparnas övriga tryck och släpp, och NIN_POPUPOPEN: Enter kommer
        // ur WM_MOUSEMOVE (filhuvudet).
        break;
    }
}

// TASKBARCREATED (uppskjutet, se kReaddTimer): ett nytt aktivitetsfält minns
// inga ikoner. Varje ikon byggs om ur sin beskrivning — i fältets kanske nya
// DPI — och läggs till på nytt; går den inte att bygga om får skalet den
// gamla bilden.
void readdAll()
{
    for (auto& [id, e] : g_entries) {
        e.added  = false;
        HICON ny = buildIcon(e.desc.image);
        place(id, e, ny ? ny : e.icon, true);
        if (ny) {
            if (e.icon)
                DestroyIcon(e.icon);
            e.icon = ny;
        }
    }
}

void onTimer(UINT_PTR t)
{
    if (t == kReaddTimer) {
        KillTimer(g_hwnd, kReaddTimer);
        readdAll();
    } else if (t > kAnimTimerBase && t <= kAnimTimerBase + kTrayIdMax) {
        const auto id = static_cast<TrayId>(t - kAnimTimerBase);
        if (g_entries.contains(id))
            pushTray(id, TrayAction::Tick);
    } else if (t > kHoverTimerBase && t <= kHoverTimerBase + kTrayIdMax) {
        pollHover(static_cast<TrayId>(t - kHoverTimerBase));
    }
}

LRESULT CALLBACK trayWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == kMsgTray) {
        onCallback(wp, lp);
        return 0;
    }
    if (msg == WM_TIMER) {
        onTimer(wp);
        return 0;
    }
    if (g_taskbarCreated != 0 && msg == g_taskbarCreated) {
        SetTimer(h, kReaddTimer, USER_TIMER_MINIMUM, nullptr);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// MOTTAGAREN, skapad vid första ikonen (filhuvudet). false + logg när det
// inte går — då registreras ingen ikon.
bool ensureWindow()
{
    if (g_hwnd)
        return true;
    // Toppnivå (ingen förälder, inte HWND_MESSAGE — TaskbarCreated är en
    // broadcast) och aldrig visat.
    g_hwnd = createHiddenToolWindow(kTrayClass, &trayWndProc,
                                    "Systemfältets mottagare");
    if (!g_hwnd)
        return false;
    if (!g_taskbarCreated)
        g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    if (!g_taskbarCreated)
        log("Systemfältet: TaskbarCreated kunde inte registreras (fel %lu) — "
            "ikonerna kommer inte tillbaka efter en Explorer-omstart",
            GetLastError());
    // UIPI: ett upphöjt program får annars varken callbacken eller
    // TaskbarCreated från det icke-upphöjda Explorer. Ett program som inte är
    // upphöjt påverkas inte.
    for (const UINT msg : {kMsgTray, g_taskbarCreated}) {
        if (msg != 0 &&
            !ChangeWindowMessageFilterEx(g_hwnd, msg, MSGFLT_ALLOW, nullptr))
            log("Systemfältet: ChangeWindowMessageFilterEx(0x%X) misslyckades "
                "(fel %lu)",
                msg, GetLastError());
    }
    return true;
}

} // namespace

bool trayAvailable()
{
    // Windows har ett systemfält — också före init, och innan Explorer har
    // startat vid en autostart (ikonen läggs då till vid TaskbarCreated).
    return true;
}

bool setTrayIcon(TrayId id, const TrayIconDesc& d)
{
    if (!platformInitialized())
        return false;   // före init eller efter shutdown (gfx/tray.h)
    if (id == 0 || id > kTrayIdMax) {
        log("setTrayIcon: id %u ligger utanför 1–%u — vägrat", id, kTrayIdMax);
        return false;
    }
    const HICON icon = buildIcon(d.image);
    if (!icon) {
        log("setTrayIcon(%u): ingen bild — ramarna saknas eller gick inte att "
            "göra till en ikon, och programfilen har ingen egen — vägrat",
            id);
        // false = INTE registrerat (gfx/tray.h), också för ett id som var det:
        // den gamla bilden blir inte stående i fältet som om anropet gick.
        removeTrayIcon(id);
        return false;
    }
    if (!ensureWindow()) {
        DestroyIcon(icon);
        return false;
    }
    Entry&             e      = g_entries[id];   // ny eller befintlig
    const std::wstring tip    = tipText(d.toolTip);
    const bool         ändrad = tip != e.tip;
    e.desc = d;
    e.tip  = tip;
    // Går skalet inte att nå är ikonen ändå registrerad: TaskbarCreated eller
    // nästa anrop lägger till den (gfx/tray.h).
    place(id, e, icon, ändrad);
    if (e.icon)
        DestroyIcon(e.icon);   // den gamla — först nu, när den nya är lämnad
    e.icon = icon;
    return true;
}

void removeTrayIcon(TrayId id)
{
    if (!platformInitialized())
        return;
    const auto it = g_entries.find(id);
    if (it == g_entries.end())
        return;   // okänt id — idempotent
    deleteFromShell(id);
    // Timrarna EFTER skalanropet: en callback som skalet skickade under
    // väntan kan ha startat hovertimern.
    KillTimer(g_hwnd, kAnimTimerBase + id);
    KillTimer(g_hwnd, kHoverTimerBase + id);
    if (it->second.icon)
        DestroyIcon(it->second.icon);
    g_entries.erase(it);
}

bool trayIconRect(TrayId id, Rectf& icon, Rectf& work)
{
    if (!platformInitialized() || !g_entries.contains(id))
        return false;
    RECT r{};
    if (!shellIconRect(id, r))
        return false;
    // Fysiska skärm-px — placeringsrymden; arbetsytan på skärmen ikonen
    // står på.
    work = workArea(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST));
    if (work.w <= 0.0f || work.h <= 0.0f)
        return false;
    icon = Rectf{float(r.left), float(r.top), float(r.right - r.left),
                 float(r.bottom - r.top)};
    return true;
}

void setTrayTimer(TrayId id, std::uint32_t ms)
{
    if (!platformInitialized() || !g_entries.contains(id))
        return;   // oregistrerat id — no-op (gfx/tray.h)
    // SetTimer med samma id startar om perioden — fasadens "ny period vid
    // omstart", som fönstrens startTimer.
    if (ms == 0)
        KillTimer(g_hwnd, kAnimTimerBase + id);
    else
        SetTimer(g_hwnd, kAnimTimerBase + id, ms, nullptr);
}

void trayShutdown()
{
    for (auto& [id, e] : g_entries) {
        deleteFromShell(id);
        if (e.icon)
            DestroyIcon(e.icon);
    }
    g_entries.clear();
    // Timrarna dör med fönstret.
    if (g_hwnd)
        DestroyWindow(g_hwnd);
    g_hwnd = nullptr;
}

// Loggen går bara vid false — det är BEGÄRAN som inte kan uppfyllas, och en
// rad varje gång någon bekräftar förvalet vore brus. En rad per process.
void applyShowInDock(bool show)
{
    static bool loggat = false;
    if (!show && !loggat) {
        loggat = true;
        log("setShowInDock(false): Windows har ingen docka — fönstrets knapp i "
            "aktivitetsfältet styrs per fönster (AppWindow::setShowInTaskbar)");
    }
}

} // namespace gfx
