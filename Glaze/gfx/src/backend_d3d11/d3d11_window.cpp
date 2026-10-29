// D3dWindow — gfx::Window ovanpå rå Win32. Eget chrome enligt Windows
// Terminals recept: behåll WS_OVERLAPPEDWINDOW (så OS:et
// sköter resize, Aero Snap och min/max-animationer), ta bort bara ÖVERKANTENS
// non-client-yta i WM_NCCALCSIZE och låt den osynliga storleksramen på
// vänster-, höger- och nederkanten stå kvar UTANFÖR den synliga kanten —
// innehållet vid kanten (ett verktygsfält mot vänsterkanten) är klickbart
// ända ut. WM_NCHITTEST lämnar ramen åt DefWindowProc och svarar själv för
// resten. Placeringsrymden för eget chrome är KLIENTENS hörn. Live-
// resize: Windows kör drag/resize i en modal OS-loop där huvudloopens
// GetMessage står still — därför ritas det direkt ur WM_SIZE/WM_PAINT.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <dwmapi.h>
#include <vector>

#include "../cursor_bitmaps.h"
#include "../display_util.h"
#include "../icon_util.h"
#include "d3d11_backend.h"
#include "gfx/platform.h"

namespace gfx {

// Chrome-måtten (caption-höjd, knappar) ägs av widget-trädet:
// glaze trycker ner hit-test-kartan + captionH_ — inga
// hårdkodade regioner här. Storleksramen är OS:ets egen:
// kartans resize-zoner läses inte (`hitTest`).

static constexpr wchar_t kWndClass[] = L"GlazeWindow";
static constexpr wchar_t kOwnerClass[] = L"GlazeTaskbarOwner";

HWND createHiddenToolWindow(const wchar_t* cls, WNDPROC proc, const char* vem)
{
    const HINSTANCE hinst = GetModuleHandleW(nullptr);
    WNDCLASSEXW     wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = proc;
    wc.hInstance     = hinst;
    wc.lpszClassName = cls;
    if (!RegisterClassExW(&wc) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        log("%s: fönsterklassen kunde inte registreras (fel %lu)", vem,
            GetLastError());
        return nullptr;
    }
    const HWND h = CreateWindowExW(WS_EX_TOOLWINDOW, cls, L"", WS_POPUP, 0, 0,
                                   0, 0, nullptr, nullptr, hinst, nullptr);
    if (!h)
        log("%s: fönstret kunde inte skapas (fel %lu)", vem, GetLastError());
    return h;
}

Rectf workArea(HMONITOR mon)
{
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    if (!mon || !GetMonitorInfoW(mon, &mi))
        return Rectf{0.0f, 0.0f, 0.0f, 0.0f};
    const RECT& w = mi.rcWork;
    return Rectf{float(w.left), float(w.top), float(w.right - w.left),
                 float(w.bottom - w.top)};
}

// ---------------------------------------------------------------- init/shutdown

bool D3dWindow::init(const WindowDesc& desc)
{
    chrome_      = desc.chrome;
    title_       = desc.title;
    transparent_ = desc.transparent;
    // STARTLÄGET — tillämpas vid den FÖRSTA
    // synligheten (`showFirst`), här nedan eller i en senare setVisible.
    startState_  = desc.startState;
    firstShown_  = false;

    // Transparent ⇒ chrome av (fri geometri och titelrad är oförenliga).
    if (transparent_ && chrome_ != Chrome::None) {
        log("transparent fönster: chrome tvingas av (None)");
        chrome_ = Chrome::None;
    }

    // Fönster-id för Event::windowId — löpnummer (0 = ogiltigt).
    // Enkeltrådad (som event-kön).
    static WindowId nextWindowId = 0;
    windowId_ = ++nextWindowId;

    const HINSTANCE hinst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &D3dWindow::wndProcThunk;
    wc.hInstance     = hinst;
    // IDC_ARROW är ett ANSI-makro utan UNICODE-define — W-varianten explicit
    wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = kWndClass;
    if (!RegisterClassExW(&wc) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        log("Fönsterklassen kunde inte registreras (fel %lu)", GetLastError());
        return false;
    }

    // desc-måtten är klientyta. Vid native läggs OS-ramen på utanpå här.
    // Eget chrome skapas med desc-måtten som FÖNSTER och får sin storlek
    // efteråt (`setSize` nedan): storleksramen (`normalFrame`) räknas i
    // fönstrets DPI, och den finns först när fönstret finns.
    // Chrome::None = WS_POPUP: ingen ram alls, ytan är exakt desc-måtten.
    const DWORD style =
        chrome_ == Chrome::None ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    RECT r{0, 0, desc.width, desc.height};
    if (chrome_ == Chrome::Native)
        AdjustWindowRect(&r, style, FALSE);

    // WindowDesc-attributen: ägare (Z-ordning/minimering följer ägaren),
    // position (sentinel = OS-default), aktivering, aktivitetsfältet.
    DWORD exStyle = 0;
    if (desc.noActivate) exStyle |= WS_EX_NOACTIVATE;
    if (desc.toolWindow) exStyle |= WS_EX_TOOLWINDOW;
    if (transparent_)
        exStyle |= WS_EX_NOREDIRECTIONBITMAP;   // composition äger ytan
    // Ägaren är alltid ett D3dWindow — en backend byggs in (samma
    // per-konstruktion-säkra cast som drawText:s Font).
    HWND ownerHwnd =
        desc.owner ? static_cast<D3dWindow*>(desc.owner)->hwnd_ : nullptr;
    // AKTIVITETSFÄLTET: ett ÄGT toppnivåfönster har ingen
    // knapp, så ett ofägt fönster som inte ska ha någon får ett eget dolt
    // ägarfönster — Microsofts recept ("create a hidden window and make this
    // hidden window the owner of your visible window", The Taskbar) och
    // WinForms väg. Till skillnad från WS_EX_TOOLWINDOW står fönstret kvar i
    // Alt+Tab, och en ägare PER fönster låter flera sådana alla stå där. Ett
    // ägt fönster och ett verktygsfönster (popupen, systemfältsmenyns värd)
    // har ingen knapp ändå. Ägaren har inget eget att svara på (klassens
    // WndProc är DefWindowProcW); går den inte att skapa öppnas fönstret ändå,
    // med sin knapp (loggat i createHiddenToolWindow).
    if (!desc.showInTaskbar && !desc.owner && !desc.toolWindow) {
        taskbarOwner_ = createHiddenToolWindow(
            kOwnerClass, DefWindowProcW, "Aktivitetsfältets ägarfönster");
        if (taskbarOwner_)
            ownerHwnd = taskbarOwner_;
    }
    // WS_POPUP kan inte CW_USEDEFAULT (ger 0-mått) — ge en fast default.
    const int fallbackPos = chrome_ == Chrome::None ? 200 : CW_USEDEFAULT;
    const int posX =
        desc.posX == kWindowPosDefault ? fallbackPos : desc.posX;
    const int posY =
        desc.posY == kWindowPosDefault ? fallbackPos : desc.posY;

    // hwnd_ sätts i WM_NCCREATE (via lpCreateParams) — meddelanden skickas
    // redan under CreateWindowExW.
    CreateWindowExW(exStyle, kWndClass, toWide(title_).c_str(), style,
                    posX, posY,
                    r.right - r.left, r.bottom - r.top,
                    ownerHwnd, nullptr, hinst, this);
    if (!hwnd_) {
        log("Fönstret kunde inte skapas (fel %lu)", GetLastError());
        destroyTaskbarOwner();
        return false;
    }
    noActivate_   = desc.noActivate;
    showInactive_ = desc.showInactive;   // läses av showFirst

    // STARTGRÄNSERNA: desc-fälten är bara startvärdet för
    // `setSizeLimits` — samma lagring, samma väg (och stilmaskningen för
    // ett fast fönster sker i applyResizeStyle, före första ShowWindow).
    setSizeLimits(desc.minWidth, desc.minHeight, desc.maxWidth,
                  desc.maxHeight);

    // ALLTID ÖVERST: desc-fältet är startvärdet för
    // `setTopmost` — samma lagring, samma väg. Måste ske FÖRE första
    // ShowWindow så fönstret aldrig blinkar förbi i fel z-ordning.
    setTopmost(desc.topmost);

    // PROGRAMIKONEN: ramar satta före init tillämpas nu, före
    // första ShowWindow — aktivitetsfältets knapp får ikonen från början.
    applyIcon();

    // Tvinga en ramomräkning nu när fönstret finns — utan den ritar DWM
    // native-titelraden ovanpå vårt chrome trots WM_NCCALCSIZE-svaret.
    if (chrome_ == Chrome::Custom) {
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE | SWP_FRAMECHANGED);
        // Win11 rundar hörnen på free-floating fönster — DWM sköter hela
        // övergången (maximerat/snappat = fyrkantigt) när preferensen är
        // satt. Returvärdet ignoreras avsiktligt: attributet finns först
        // från Win11 (build 22000); på Win10 misslyckas anropet tyst och
        // fyrkantigt är redan rätt där.
        const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE,
                              &corner, sizeof(corner));
    }

    // Desc-måtten är LOGISKA px.
    // Lagras som invarianten — WM_SIZE under CreateWindowExW
    // hann räkna tillbaka ur fel skala, desc-värdena är sanningen här.
    logicalW_ = desc.width > 0 ? desc.width : 1;
    logicalH_ = desc.height > 0 ? desc.height : 1;
    // STORLEKEN OCH PLATSEN FÖRE RENDERAREN. DPI:n och storleksramen är
    // kända först när fönstret finns, så klientytan sätts nu — men innan
    // renderaren finns: WM_SIZE kör `onResize`, och med en renderare hade
    // fönstret ritat en frame redan i init (ett fönster ska
    // aldrig ha ritat före visning). Popup-vägen sätter storlek + plats i ett svep.
    if (desc.popupAnchorWindow) {
        // Popup-kontraktet: placera under ankarrectet med flip-uppåt/
        // klamp-i-sidled
        // (skärmrymden på Win32 ÄR fysiska px, så ankarets px mappar 1:1).
        auto* ow = static_cast<D3dWindow*>(desc.popupAnchorWindow);
        const float os = ow->uiScale();
        const float sw = static_cast<float>(desc.width) * os;
        const float sh = static_cast<float>(desc.height) * os;
        const Rectf& a = desc.popupAnchorRect;
        Pointf      pos = ow->clientToScreen(a.x, a.y + a.h);
        const Rectf scr = ow->screenArea();
        if (scr.w > 0.0f) {
            if (pos.y + sh > scr.y + scr.h)
                pos.y = ow->clientToScreen(a.x, a.y).y - sh;   // vänd uppåt
            // OBS: inte std::clamp — popup bredare än arbetsytan ger
            // max < min = UB där; max/min-formen låter vänsterkanten
            // vinna i stället.
            pos.x = std::max(scr.x, std::min(pos.x, scr.x + scr.w - sw));
        }
        SetWindowPos(hwnd_, nullptr,
                     static_cast<int>(std::lround(pos.x)),
                     static_cast<int>(std::lround(pos.y)),
                     static_cast<int>(std::lround(sw)),
                     static_cast<int>(std::lround(sh)),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        // Klientytan ur den logiska storleken i fönstrets FULLA skala — när
        // den inte redan stämmer: vid en annan skala än 1, och ALLTID för
        // ett ändringsbart Custom-fönster (ramen har dragit in klientytan
        // efter att det skapats med desc-måtten som fönster).
        const float s  = uiScale();
        const Sizef nu = clientSize();
        if (static_cast<long>(nu.w) != std::lround(logicalW_ * s) ||
            static_cast<long>(nu.h) != std::lround(logicalH_ * s))
            setSize(logicalW_, logicalH_);
        // PLATSEN ÄR KLIENTENS HÖRN: CreateWindowExW lade YTTERhörnet
        // på den begärda platsen, och ramen sköt klienten åt höger.
        if (chrome_ == Chrome::Custom && desc.posX != kWindowPosDefault &&
            desc.posY != kWindowPosDefault)
            setPosition(desc.posX, desc.posY);
    }

    const Sizef cs = clientSize();
    if (!gfxRenderer_.init(hwnd_, static_cast<int>(cs.w),
                           static_cast<int>(cs.h), transparent_)) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        destroyTaskbarOwner();   // EFTER fönstret det äger
        return false;
    }
    // Renderern kan ha degraderat (composition-vägen föll) — följ med.
    transparent_ = gfxRenderer_.transparent();

    // noActivate: visa utan att stjäla fokus (popups/menyer).
    // Dolt fönster (glaze-render): hoppa ShowWindow helt — CreateWindowExW
    // utan WS_VISIBLE lämnar fönstret dolt; renderer/retained-RT finns
    // ändå.
    // Skärmen fönstret står på — mätt nu när fönstret och DPI:n finns.
    measureDisplay(false);

    // NORMALRUTANS STARTVÄRDE: platsen
    // fönstret har nu, innan det visats — skapat, eventuellt omskalat till
    // sin DPI och (popupen) placerat. En maximerad start bär därmed den
    // BEGÄRDA rutan, fast fönstret aldrig visats normalt.
    trackNormalPosition();

    // STYRPLATTAN: Direct Manipulation och de råa kontakterna — före
    // visningen. Ett DOLT fönster kopplas först när det visas (`setVisible`
    // — dold start); ett offscreen-fönster (grindarna,
    // glaze-render) visas aldrig och får aldrig input.
    if (!desc.hidden) {
        touchpad_.attach(hwnd_, *this);
        showFirst();
    }
    return true;
}

// FÖRSTA SYNLIGHETEN — STARTLÄGET i den första
// ShowWindow. Processens första ShowWindow kan ersättas av STARTUPINFO
// (genvägens "Kör: Maximerat/Minimerat"), men enligt Raymond Chen bara när
// kommandot är SW_SHOW, SW_SHOWNORMAL eller SW_SHOWDEFAULT — ett direkt
// SW_SHOWMAXIMIZED står sig. Det är odokumenterat, och därför VERIFIERAS det:
// stämmer läget inte efteråt görs ett andra anrop, och ett andra anrop
// respekteras alltid. Normalläget behåller SW_SHOW, så en genväg som ber om
// maximerat fortfarande får det. `showInactive_` gör den
// första normala visningen till SW_SHOWNOACTIVATE — ett flytande
// dockningsfönster som lossnar mitt i ett drag tar inte fokus från fönstret
// som håller trycket; en senare visning aktiverar som vanligt. Maximerat
// aktiverar ändå (Windows saknar en inaktiv maximering).
void D3dWindow::showFirst()
{
    firstShown_ = true;
    switch (startState_) {
    case WindowState::Maximized:
        ShowWindow(hwnd_, SW_SHOWMAXIMIZED);
        if (!IsZoomed(hwnd_))
            ShowWindow(hwnd_, SW_SHOWMAXIMIZED);
        break;
    case WindowState::Minimized:
        ShowWindow(hwnd_, SW_SHOWMINNOACTIVE);
        if (!IsIconic(hwnd_))
            ShowWindow(hwnd_, SW_SHOWMINNOACTIVE);
        break;
    case WindowState::Normal:
        ShowWindow(hwnd_, noActivate_ || showInactive_ ? SW_SHOWNOACTIVATE
                                                       : SW_SHOW);
        break;
    }
}

// NORMALRUTANS POSITION: hörnet i placeringsrymden — samma rymd som
// setPosition — men bara när fönstret står i normalläge. För eget chrome är
// det KLIENTENS hörn: ytterhörnet plus den osynliga storleksramen,
// så `setPosition(clientToScreen(0, 0))` är en identitet
// som dockningen och `setLocation`/`geometry()` bygger på; för Native
// ytterhörnet. Ett zoomat fönster står där Windows lagt det, och
// ett ikoniserat på (-32000, -32000); ingen av dem är rutan `restore()` ger
// tillbaka. Ett snappat fönster (Aero Snap) räknas som normalläge, som
// WinForms `RestoreBounds`.
void D3dWindow::trackNormalPosition()
{
    if (!hwnd_ || IsZoomed(hwnd_) || IsIconic(hwnd_))
        return;
    RECT r{};
    if (GetWindowRect(hwnd_, &r)) {
        const RECT f = normalFrame();
        normalX_ = r.left + f.left;
        normalY_ = r.top + f.top;
    }
}

WindowPlacement D3dWindow::placement() const
{
    WindowPlacement p;
    if (!hwnd_)
        return p;   // före init finns ingen geometri att spåra
    p.x           = normalX_;
    p.y           = normalY_;
    p.width       = logicalW_;   // står still under maximeringen (WM_SIZE)
    p.height      = logicalH_;
    p.state       = IsIconic(hwnd_)  ? WindowState::Minimized
                  : IsZoomed(hwnd_)  ? WindowState::Maximized
                                     : WindowState::Normal;
    p.hasPosition = true;
    return p;
}

// ---------------------------------------------------------------- skärmens mm

namespace {

// EDID UR REGISTRET. `GetDeviceCaps(HORZSIZE)` duger inte: Windows räknar fram den ur
// skalningsvalet och svarar med nominell täthet i stället för panelens.
// Gränssnittsnamnet `\\?\DISPLAY#BOE0B8B#5&…&UID8388688#{guid}` blir nyckeln
// `SYSTEM\CurrentControlSet\Enum\DISPLAY\BOE0B8B\5&…&UID8388688\Device
// Parameters`, och värdet EDID där är skärmens egna block.
bool edidRegistryPath(const std::wstring& deviceId, std::wstring& ut)
{
    const std::size_t start = deviceId.find(L"DISPLAY#");
    if (start == std::wstring::npos)
        return false;
    const std::size_t first = start + 8;
    const std::size_t guid  = deviceId.find(L"#{", first);
    if (guid == std::wstring::npos || guid <= first)
        return false;
    std::wstring instans = deviceId.substr(first, guid - first);
    for (wchar_t& ch : instans)
        if (ch == L'#')
            ch = L'\\';
    ut = L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\" + instans +
         L"\\Device Parameters";
    return true;
}

bool readEdid(const MONITORINFOEXW& mi, std::vector<std::uint8_t>& ut)
{
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof dd;
    if (!EnumDisplayDevicesW(mi.szDevice, 0, &dd,
                             EDD_GET_DEVICE_INTERFACE_NAME))
        return false;
    std::wstring nyckel;
    if (!edidRegistryPath(dd.DeviceID, nyckel))
        return false;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, nyckel.c_str(), 0, KEY_READ, &key) !=
        ERROR_SUCCESS)
        return false;
    std::uint8_t edid[256] = {};
    DWORD        size      = sizeof edid;
    DWORD        typ       = 0;
    const LSTATUS st =
        RegQueryValueExW(key, L"EDID", nullptr, &typ, edid, &size);
    RegCloseKey(key);
    if (st != ERROR_SUCCESS || typ != REG_BINARY)
        return false;
    ut.assign(edid, edid + size);
    return true;
}

} // namespace

// Processen är per-monitor-v2-medveten (d3d11_platform.cpp), så `rcMonitor`
// är panelens FYSISKA px — och bufferten är 1:1 med dem.
void D3dWindow::measureDisplay(bool announce)
{
    if (!hwnd_)
        return;
    HMONITOR       mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof mi;
    if (!mon || !GetMonitorInfoW(mon, &mi))
        return;
    const int  w   = mi.rcMonitor.right - mi.rcMonitor.left;
    const int  h   = mi.rcMonitor.bottom - mi.rcMonitor.top;
    const UINT dpi = GetDpiForWindow(hwnd_);
    if (mon == displayMon_ && w == displayW_ && h == displayH_ &&
        dpi == displayDpi_)
        return;   // samma skärm i samma läge — ingen registerläsning
    displayMon_ = mon;
    displayW_   = w;
    displayH_   = h;
    displayDpi_ = dpi;

    std::vector<std::uint8_t> block;
    display::Edid             edid;
    DisplayMetrics            m;
    if (readEdid(mi, block) &&
        display::parseEdid(block.data(), block.size(), edid))
        m = display::measure(w, h, edid.widthMm, edid.heightMm, edid.id);
    else
        m = display::assumed(toUtf8(mi.szDevice),
                             "skärmens EDID gick inte att läsa ur registret");
    const bool ändrad = m.pxPerMm != display_.pxPerMm ||
                        m.source != display_.source || m.id != display_.id;
    display_ = std::move(m);
    if (announce && ändrad) {
        Event e;
        e.type     = EventType::WindowDisplayChanged;
        e.windowId = windowId_;
        pushEvent(e);
    }
}

// Släpp mus-capture i förtid (Dropdown-popup öppnad mitt i ett tryck:
// SetCapture i WM_LBUTTONDOWN routar annars ALLA WM_MOUSEMOVE hit tills
// släppet — popupen ser ingen hover). Bara vår egen capture släpps — och det
// är vårt EGET släpp: `pressCapture_` nollas före, så `WM_CAPTURECHANGED`
// blir inget `MouseCaptureLost`.
void D3dWindow::releaseMouseCapture()
{
    if (hwnd_ && GetCapture() == hwnd_) {
        pressCapture_ = false;
        ReleaseCapture();
    }
}

namespace {

// Glaze-fönstret bakom `h` — samma process, vår fönsterklass och ett levande
// objekt — annars nullptr. Klassnamnet räcker inte ensamt (en annan process
// kan registrera samma namn), och användardatat är noll under rivningen.
D3dWindow* ownWindowOf(HWND h)
{
    if (!h)
        return nullptr;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId())
        return nullptr;
    wchar_t klass[64] = {};
    const int n = GetClassNameW(h, klass, 64);
    if (n <= 0 || wcscmp(klass, kWndClass) != 0)
        return nullptr;
    return reinterpret_cast<D3dWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
}

// DEN UTTRYCKLIGA ÖVERLÄMNINGEN: fönstret som
// `captureMouse` tar capture till JUST NU. `SetCapture` skickar
// `WM_CAPTURECHANGED` till den gamla hållaren SYNKRONT, och bara medan det här
// står är meddelandet en överlämning och inte en förlust — ett annat av appens
// fönster som tar capture på något annat sätt (en OS-loop, ett vanligt
// `SetCapture`) lämnar källans tryck att städa, som vilken förlust som helst.
// UI-tråden.
HWND g_handoverTo = nullptr;

} // namespace

// MUSENS CAPTURE HIT mitt i ett tryck. Vi tar capture
// vid VARJE tryck och släpper den först när sista knappen är uppe, så "ett
// tryck pågår i ett av appens fönster" är "ett av appens fönster håller
// capture MED `pressCapture_`" — capture utan flaggan är OS:ets flytt- eller
// storleksloop, och den tas aldrig, inte heller när den ligger i fönstret
// självt. Bara FÖRGRUNDENS tråd får hela trycket
// (SetCapture-dokumentationen: ett bakgrundsfönster får bara musen över sin
// synliga yta), så när en annan app är förgrund — ett noActivate-fönster
// klickat i bakgrunden — stannar trycket där det började: false, loggat. Den
// gamla hållaren får `WM_CAPTURECHANGED` med oss som lParam medan
// `g_handoverTo` pekar hit, och skickar då inget avbrott.
bool D3dWindow::captureMouse()
{
    if (!hwnd_)
        return false;
    D3dWindow* const hållare = ownWindowOf(GetCapture());
    if (!hållare || !hållare->pressCapture_)
        return false;   // inget tryck i appen — eller loopens capture
    const HWND förgrund = GetForegroundWindow();
    if (!förgrund ||
        GetWindowThreadProcessId(förgrund, nullptr) != GetCurrentThreadId()) {
        log("captureMouse: förgrunden tillhör en annan tråd — trycket stannar "
            "i fönstret där det började");
        return false;
    }
    if (hållare == this)
        return true;
    g_handoverTo = hwnd_;
    SetCapture(hwnd_);
    g_handoverTo = nullptr;
    if (GetCapture() != hwnd_)
        return false;
    pressCapture_ = true;   // trycket är vårt nu — och dess släpp
    return true;
}

// Frågan bredvid `captureMouse` — glaze ställer den efter krokarna i ett
// övertagande. Capture utan tryckets flagga (OS:ets loop, eller en som redan
// gick förlorad) räknas inte.
bool D3dWindow::hasMouseCapture() const
{
    return hwnd_ && pressCapture_ && GetCapture() == hwnd_;
}

// MUSPEKAREN: fasadens form → OS:ets SYSTEMPEKARE. Talen står
// utskrivna av samma skäl som klassens pil i `init`: `IDC_*` är
// ANSI-makron utan UNICODE-define (winuser.h: IDC_ARROW 32512, IDC_IBEAM
// 32513, IDC_WAIT 32514, IDC_CROSS 32515, IDC_SIZEWE 32644, IDC_SIZENS
// 32645, IDC_SIZEALL 32646, IDC_NO 32648, IDC_HAND 32649).
// Systempekarna är DELADE handtag — de laddas per anrop och förstörs aldrig.
//
// TVÅ UNDANTAG: `Hidden` är nullptr — `SetCursor(NULL)`
// tar bort pekaren över klientytan (dokumenterat), utan `ShowCursor`s globala
// räknare som läcker om den inte paras. Och `ClosedHand`, som Windows saknar,
// är fönstrets EGEN pekare ur src/cursor_bitmaps.h, heltalsskalad till
// systemets pekarstorlek i fönstrets DPI (SM_CXCURSOR är 32 vid 96 dpi) och
// byggd om när storleken ändras. Går den inte att bygga visas fyrpilen.
HCURSOR D3dWindow::cursorHandle(PointerShape s)
{
    WORD id = 32512;   // IDC_ARROW
    switch (s) {
    case PointerShape::Arrow:      id = 32512; break;   // IDC_ARROW
    case PointerShape::IBeam:      id = 32513; break;   // IDC_IBEAM
    case PointerShape::Hand:       id = 32649; break;   // IDC_HAND
    case PointerShape::ResizeWE:   id = 32644; break;   // IDC_SIZEWE
    case PointerShape::ResizeNS:   id = 32645; break;   // IDC_SIZENS
    case PointerShape::NotAllowed: id = 32648; break;   // IDC_NO
    case PointerShape::Crosshair:  id = 32515; break;   // IDC_CROSS
    case PointerShape::Move:       id = 32646; break;   // IDC_SIZEALL
    case PointerShape::Wait:       id = 32514; break;   // IDC_WAIT
    case PointerShape::Hidden:     return nullptr;      // ingen pekare
    case PointerShape::ClosedHand: {
        const UINT dpi   = hwnd_ ? GetDpiForWindow(hwnd_) : USER_DEFAULT_SCREEN_DPI;
        const int  skala = std::max(
            1, GetSystemMetricsForDpi(SM_CXCURSOR, dpi) / kClosedHand.size);
        const int  sida  = kClosedHand.size * skala;
        if (closedHand_ && closedHandSide_ == sida)
            return closedHand_;
        IconImage im;
        int       hx = 0, hy = 0;
        im.width  = sida;
        im.height = sida;
        im.rgba   = expandCursor(kClosedHand, skala, hx, hy);
        const HCURSOR ny = iconFromImage(im, true, hx, hy);
        if (!ny) {
            static bool loggat = false;
            if (!loggat) {
                loggat = true;
                log("Pekaren: den stängda handen gick inte att skapa — "
                    "fyrpilen visas i stället");
            }
            id = 32646;   // IDC_SIZEALL
            break;
        }
        if (closedHand_) {
            // Den gamla storleken får inte vara pekaren som visas när den
            // rivs — byt först.
            if (GetCursor() == closedHand_)
                SetCursor(ny);
            DestroyCursor(closedHand_);
        }
        closedHand_     = ny;
        closedHandSide_ = sida;
        return closedHand_;
    }
    }
    return LoadCursorW(nullptr, MAKEINTRESOURCEW(id));
}

// Formen LAGRAS alltid (också före init — tvåfasen) och visas DIREKT bara
// när pekaren står i vår klientyta eller fönstret håller capture. Skälet
// till det direkta anropet är två: `WM_SETCURSOR` kommer först vid NÄSTA
// rörelse (pekaren hade släpat en rörelse efter), och under capture skickas
// det inte alls — ett skikt som dras hade tappat ↔ så fort pekaren lämnade
// det. Villkoret gör också att ett DOLT fönster (grindarna, glaze-render)
// aldrig rör utvecklarens pekare.
void D3dWindow::setCursor(PointerShape s)
{
    cursor_ = s;
    if (hwnd_ && (mouseInside_ || GetCapture() == hwnd_))
        SetCursor(cursorHandle(s));
}

// Lyft fönstret överst och ge det fokus (modal-klick-på-ägare m.m.).
// Ett noActivate-fönster lyfts BARA i Z-ordningen:
// `SetForegroundWindow` aktiverar också ett `WS_EX_NOACTIVATE`-fönster
// ("To activate the window, use the SetActiveWindow or SetForegroundWindow
// function"), och det flytande verktygsfältet hade då tagit fokus från värden
// som håller trycket. `HWND_TOP` lägger det över ägarens andra fönster — ägda
// fönster står alltid över sin ägare — och behåller ett topmost-fönster i sitt
// band.
void D3dWindow::raise()
{
    if (!hwnd_ || !IsWindowVisible(hwnd_))   // dolt förblir dolt (fasaden)
        return;
    if (noActivate_) {
        SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
}

void D3dWindow::shutdown()
{
    gfxRenderer_.shutdownRenderer();
    if (hwnd_) {
        // Styrplattan lossas medan fönstret finns: Direct Manipulation
        // avaktiveras mot HWND:en, och pumptimern hör till den.
        touchpad_.detach();
        HWND h = hwnd_;
        hwnd_  = nullptr;   // WndProc:en ska inte röra objektet under rivning
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        DestroyWindow(h);
    }
    // Aktivitetsfältets ägare EFTER fönstret den äger (se init).
    destroyTaskbarOwner();
    // Den stängda handen är fönstrets EGEN pekare —
    // och visas den just nu får den inte vara den som visas när den rivs.
    if (closedHand_) {
        if (GetCursor() == closedHand_)
            SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
        DestroyCursor(closedHand_);
    }
    closedHand_     = nullptr;
    closedHandSide_ = 0;
    // Programikonens HICON:er EFTER fönstret: så länge det fanns var de
    // satta på det. Ramarna står kvar — ett nytt init tillämpar dem igen.
    if (iconSmall_)
        DestroyIcon(iconSmall_);
    if (iconBig_)
        DestroyIcon(iconBig_);
    iconSmall_   = nullptr;
    iconBig_     = nullptr;
    iconApplied_ = false;
}

// AKTIVITETSFÄLTETS ÄGARE rivs alltid EFTER fönstret den äger — i shutdown
// och i init-felvägarna. Rivs ägaren först förstör OS:et fönstret med den
// ("The system automatically destroys an owned window when its owner is
// destroyed", Window Features) och inte vår rivning. Idempotent.
void D3dWindow::destroyTaskbarOwner()
{
    if (taskbarOwner_)
        DestroyWindow(taskbarOwner_);
    taskbarOwner_ = nullptr;
}

// ---------------------------------------------------------------- programikonen
//
// PROGRAMIKONEN. Två storlekar, i fönstrets DPI:
// ICON_SMALL (SM_CXSMICON — namnlistens och Alt+Tab-listans lilla) och
// ICON_BIG (SM_CXICON — aktivitetsfältet och Alt+Tab). Varje storlek tar sin
// ram med valregeln (icon_util.h) och lämnar skalningen till Windows.
//
// `iconFromImage` och `programIcon` står UTANFÖR en anonym namnrymd och är
// deklarerade i d3d11_backend.h: systemfältets ikon (d3d11_tray.cpp) går
// samma väg.

// En ram som HICON: en 32-bitars DIB uppifrån och ned med RAK alfa — samma
// form som en 32-bitars bild i en .ico-fil, som Windows ritar med alfan —
// plus en AND-mask ur alfan (1 = genomskinlig), som OS:et faller tillbaka på
// där det ritar utan alfa. Ramen ska vara giltig (`iconImageValid` —
// `pickIconImage` lämnar bara ut sådana). nullptr när GDI vägrar.
//
// `cursor` gör samma bild till en PEKARE: `fIcon =
// FALSE` och hotspoten i ramens pixlar. För en ikon ignorerar Windows
// hotspoten (den sitter i mitten).
HICON iconFromImage(const IconImage& im, bool cursor, int hotX, int hotY)
{
    BITMAPV5HEADER bi{};
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = im.width;
    bi.bV5Height      = -im.height;   // negativ höjd = rad 0 överst
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    void*         bits  = nullptr;
    const HDC     dc    = GetDC(nullptr);
    const HBITMAP color = CreateDIBSection(
        dc, reinterpret_cast<const BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits,
        nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color)
        return nullptr;
    if (!bits) {
        DeleteObject(color);
        return nullptr;
    }

    // RGBA → BGRA: maskerna ovan i little-endian-minne.
    const std::size_t   n   = static_cast<std::size_t>(im.width) *
                              static_cast<std::size_t>(im.height);
    const std::uint8_t* src = im.rgba.data();
    auto*               dst = static_cast<std::uint8_t*>(bits);
    for (std::size_t i = 0; i < n; ++i, src += 4, dst += 4) {
        dst[0] = src[2];
        dst[1] = src[1];
        dst[2] = src[0];
        dst[3] = src[3];
    }

    // AND-masken: en bit per pixel, mest signifikanta biten först, raderna
    // jämnade till 16 bitar (CreateBitmap kräver WORD-justerade rader).
    const std::size_t stride = static_cast<std::size_t>((im.width + 15) / 16) * 2;
    std::vector<std::uint8_t> mask(stride * static_cast<std::size_t>(im.height),
                                   0);
    for (int y = 0; y < im.height; ++y)
        for (int x = 0; x < im.width; ++x) {
            const std::size_t p = (static_cast<std::size_t>(y) *
                                       static_cast<std::size_t>(im.width) +
                                   static_cast<std::size_t>(x)) * 4u;
            if (im.rgba[p + 3] == 0)
                mask[static_cast<std::size_t>(y) * stride +
                     static_cast<std::size_t>(x / 8)] |=
                    static_cast<std::uint8_t>(0x80u >> (x % 8));
        }
    const HBITMAP maskBmp = CreateBitmap(im.width, im.height, 1, 1, mask.data());

    HICON icon = nullptr;
    if (maskBmp) {
        ICONINFO ii{};
        ii.fIcon    = cursor ? FALSE : TRUE;
        ii.xHotspot = cursor ? static_cast<DWORD>(hotX) : 0;
        ii.yHotspot = cursor ? static_cast<DWORD>(hotY) : 0;
        ii.hbmMask  = maskBmp;
        ii.hbmColor = color;
        icon        = CreateIconIndirect(&ii);   // kopierar bitmapparna
        DeleteObject(maskBmp);
    }
    DeleteObject(color);
    return icon;
}

// PROGRAMFILENS IKON (`programFirst`): exe:ns ikongrupp nr 1 — den
// `glaze_app_icon` kompilerar in — i sidan `side` px, nullptr när exe:n
// saknar den. 14 = RT_GROUP_ICON: ANSI-makro utan UNICODE-define, W-varianten
// explicit (som IDC_ARROW i init). LoadImageW utan LR_SHARED ger en egen ikon
// som DestroyIcon river, precis som CreateIconIndirect:s.
HICON programIcon(int side)
{
    const HINSTANCE hinst = GetModuleHandleW(nullptr);
    if (!FindResourceW(hinst, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(14)))
        return nullptr;
    return static_cast<HICON>(LoadImageW(hinst, MAKEINTRESOURCEW(1),
                                         IMAGE_ICON, side, side,
                                         LR_DEFAULTCOLOR));
}

void D3dWindow::setIcon(const WindowIcon& icon)
{
    icon_ = icon;
    applyIcon();   // före init: lagrat, init tillämpar
}

// Bygger ikonerna ur de lagrade ramarna i AKTUELL DPI och sätter dem. Körs vid
// setIcon, i init och vid WM_DPICHANGED (SM_CXICON följer skärmens DPI).
void D3dWindow::applyIcon()
{
    if (!hwnd_)
        return;
    const UINT dpi   = GetDpiForWindow(hwnd_);
    const int  small = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    const int  big   = GetSystemMetricsForDpi(SM_CXICON, dpi);

    HICON nySmall = nullptr;
    HICON nyBig   = nullptr;
    if (!icon_.images.empty()) {
        // PROGRAMFILENS IKON FÖRST (`programFirst`) — den vinner över
        // ramverkets förval i BÅDA storlekarna (`programIcon`).
        if (icon_.programFirst) {
            nySmall = programIcon(small);
            nyBig   = programIcon(big);
        }
        if (!nySmall && !nyBig) {
            if (const IconImage* im = pickIconImage(icon_, small))
                nySmall = iconFromImage(*im);
            if (const IconImage* im = pickIconImage(icon_, big))
                nyBig = iconFromImage(*im);
        }
        if (!nySmall && !nyBig)
            log("setIcon: ingen av %u ramar gick att göra till en ikon",
                static_cast<unsigned>(icon_.images.size()));
    }
    // Den nya först, den gamla rivs sedan — WM_SETICON håller bara handtaget.
    // En tom lista sätter null: fönstret har då ingen egen ikon.
    SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(nySmall));
    SendMessageW(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(nyBig));
    if (iconSmall_)
        DestroyIcon(iconSmall_);
    if (iconBig_)
        DestroyIcon(iconBig_);
    iconSmall_   = nySmall;
    iconBig_     = nyBig;
    iconApplied_ = nySmall != nullptr || nyBig != nullptr;
}

void D3dWindow::renderFrame()
{
    // Före renderaren finns ingenting att rita i: init sätter klientytan
    // (storleksramen, skalan) INNAN den skapas, och WM_SIZE därifrån når hit.
    if (!hwnd_ || !gfxRenderer_.ready())
        return;
    gfxRenderer_.beginFrame();      // binder retained-RT:n
    if (renderCb_)
        renderCb_(*this);           // frame-innehållet → RT:n
    gfxRenderer_.composite();       // RT → backbuffer (skriver hela ytan)
    drawChrome();                   // chrome direkt i backbuffern, ovanpå
    if (overlayCb_)
        overlayCb_(*this);          // informativt topplager — självstädande
    gfxRenderer_.present();
}

void D3dWindow::onResize()
{
    if (!hwnd_)
        return;
    const Sizef cs = clientSize();
    gfxRenderer_.resize(static_cast<int>(cs.w), static_cast<int>(cs.h));
    // Rita direkt — under den modala resize-loopen kommer huvudloopen inte åt
    renderFrame();
}

// Fasadens readback (glaze-render): retained-RT:n 1:1 i fysiska px —
// pixelidentisk med det fönstret visar/skulle visa (WYSIWYG-kontraktet).
bool D3dWindow::captureFrame(std::vector<std::uint8_t>& rgba, int& w, int& h)
{
    if (!hwnd_)
        return false;
    return gfxRenderer_.captureRetained(rgba, w, h);
}

// ---------------------------------------------------------------- egenskaper

// No-op-säkra före init — anropas innan fönstret finns
// (tvåfas-mönstret: createWindow() i konstruktorn, init() i show()).

void D3dWindow::setSize(int w, int h)
{
    if (!hwnd_)
        return;
    // LOGISKA px (fasadändringen) → fysiska via fönstrets FULLA skala
    // (dpi/96 × userScale). Lagras som invarianten.
    logicalW_ = w;
    logicalH_ = h;
    const float s = uiScale();
    const RECT  r = windowRectFor(static_cast<int>(std::lround(w * s)),
                                  static_cast<int>(std::lround(h * s)));
    // Egen resize: WM_SIZE körs SYNKRONT under SetWindowPos och skulle
    // annars räkna tillbaka logisk storlek ur den AVRUNDADE fysiska —
    // 801 logiska px vid skala 0.5 blir 401 fysiska blir 802 logiska, och
    // driften ackumuleras för varje omgång. Logiska storleken ovan är
    // sanningen; vakten skyddar den.
    pendingScaleResize_ = true;
    SetWindowPos(hwnd_, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    pendingScaleResize_ = false;
}

// STORLEKSGRÄNSERNA.
//
// Talen LAGRAS bara; själva klampningen sker i `WM_GETMINMAXINFO`, alltså
// vid FRÅGAN. Det är hela poängen med Windows-vägen: DPI-/skalbyten blir
// gratis eftersom omräkningen görs när OS:et frågar och inte när appen
// sätter — de tre andra backendarna måste återapplicera vid varje
// skalbyte.
//
// `min == max` LÅSER dessutom fönstret på riktigt: `WS_THICKFRAME` (dragbar
// kant) och `WS_MAXIMIZEBOX` maskas bort. Utan den delen hade
// WM_GETMINMAXINFO visserligen hållit storleken, men kanten hade fortsatt
// visa resize-pekare och dubbelklick på namnlisten hade maximerat.
void D3dWindow::setSizeLimits(int minW, int minH, int maxW, int maxH)
{
    limMinW_ = minW;
    limMinH_ = minH;
    limMaxW_ = maxW;
    limMaxH_ = maxH;
    applyResizeStyle();
}

// ALLTID ÖVERST.
// `SetWindowPos` med `HWND_TOPMOST`/`HWND_NOTOPMOST` är Win32-vägen; den
// sätter samtidigt `WS_EX_TOPMOST`, vilket är det som gör att `topmost()`
// kan svara med vad som FAKTISKT gäller i stället för med begäran.
// SWP_NOACTIVATE med flit: z-ordningen ska ändras utan att fönstret rycker
// fokus (en popup med noActivate hade annars brutit sitt eget kontrakt).
void D3dWindow::setTopmost(bool on)
{
    topmost_ = on;
    if (!hwnd_)
        return;   // tvåfas: slår igenom i init via desc.topmost
    SetWindowPos(hwnd_, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool D3dWindow::topmost() const
{
    if (!hwnd_)
        return topmost_;
    return (GetWindowLongPtrW(hwnd_, GWL_EXSTYLE) &
            static_cast<LONG_PTR>(WS_EX_TOPMOST)) != 0;
}

void D3dWindow::applyResizeStyle()
{
    // Chrome::None är WS_POPUP och har varken kant eller maximeringsknapp
    // — inget att maska.
    if (!hwnd_ || chrome_ == Chrome::None)
        return;
    LONG_PTR st = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    const LONG_PTR before = st;
    if (fixedSize())
        st &= ~static_cast<LONG_PTR>(WS_THICKFRAME | WS_MAXIMIZEBOX);
    else
        st |= static_cast<LONG_PTR>(WS_THICKFRAME | WS_MAXIMIZEBOX);
    if (st == before)
        return;
    // KLIENTYTAN STÅR STILL. För eget chrome är
    // storleksramen vår (`normalFrame`): den finns för ett ändringsbart
    // fönster och inte för ett fast, så ett byte mellan de två hade annars
    // flyttat och storleksändrat klientytan med ramens bredd — det appen
    // placerat och dimensionerat. Klientens rektangel läses FÖRE bytet och
    // fönstret läggs runt den efteråt. Maximerat och minimerat äger OS:et
    // geometrin; där räknas bara ramen om.
    const bool behåll = chrome_ == Chrome::Custom && !IsZoomed(hwnd_) &&
                        !IsIconic(hwnd_);
    RECT  klient{};
    POINT origo{0, 0};
    if (behåll) {
        GetClientRect(hwnd_, &klient);
        ClientToScreen(hwnd_, &origo);
    }
    SetWindowLongPtrW(hwnd_, GWL_STYLE, st);
    // SWP_FRAMECHANGED krävs för att stilbytet ska slå igenom (samma
    // regel som WM_NCCALCSIZE-receptet i init).
    if (!behåll) {
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                         SWP_FRAMECHANGED);
        return;
    }
    const RECT r = windowRectFor(klient.right, klient.bottom);   // NYA ramen
    // Samma klientyta i fysiska px — den logiska storleken är invarianten
    // och räknas inte tillbaka ur WM_SIZE (vakten, se setSize).
    pendingScaleResize_ = true;
    SetWindowPos(hwnd_, nullptr, origo.x + r.left, origo.y + r.top,
                 r.right - r.left, r.bottom - r.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    pendingScaleResize_ = false;
}

// ---------------------------------------------------------- storleksramen
//
// STORLEKSRAMEN UTANFÖR DEN SYNLIGA KANTEN — se
// d3d11_backend.h. Ramen finns bara för ett `Chrome::Custom`-fönster som går
// att storleksändra: ett fast fönster (min == max) har varken
// WS_THICKFRAME eller ram, och dess klientyta ÄR fönstret. Chrome::None
// (popuper, menyer, tooltips, transparenta fönster) rörs inte.

RECT D3dWindow::normalFrame() const
{
    RECT f{0, 0, 0, 0};
    if (!hwnd_ || chrome_ != Chrome::Custom || fixedSize())
        return f;
    RECT r{0, 0, 0, 0};
    const auto ex =
        static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
    if (!AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, ex,
                                  GetDpiForWindow(hwnd_))) {
        static bool loggat = false;
        if (!loggat) {
            loggat = true;
            log("AdjustWindowRectExForDpi misslyckades (fel %lu) — fönstret "
                "får ingen storleksram",
                GetLastError());
        }
        return f;
    }
    // Vänster, höger och nederkant. Överkantens icke-klientyta (namnlisten
    // och dess ram) är borttagen — där svarar WM_NCHITTEST HTTOP i ett band
    // inne i klientytan i stället.
    f.left   = -r.left;
    f.right  = r.right;
    f.bottom = r.bottom;
    return f;
}

RECT D3dWindow::frameInsets() const
{
    if (!hwnd_ || chrome_ != Chrome::Custom)
        return RECT{0, 0, 0, 0};
    if (IsZoomed(hwnd_)) {
        // Vid maximering skjuter Windows ut ramen utanför skärmen — klient-
        // ytan dras in med ramtjockleken på ALLA fyra sidor, så innehållet
        // hamnar på skärmen.
        const UINT dpi = GetDpiForWindow(hwnd_);
        const int  fx  = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) +
                       GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        const int  fy  = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) +
                       GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
        return RECT{fx, fy, fx, fy};
    }
    return normalFrame();
}

RECT D3dWindow::windowRectFor(int physW, int physH) const
{
    RECT r{0, 0, static_cast<LONG>(physW), static_cast<LONG>(physH)};
    if (chrome_ == Chrome::Native) {
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    } else if (chrome_ == Chrome::Custom) {
        const RECT f = normalFrame();
        r.left   -= f.left;
        r.top    -= f.top;
        r.right  += f.right;
        r.bottom += f.bottom;
    }
    return r;
}

void D3dWindow::setTitle(const std::string& t)
{
    title_ = t;
    if (hwnd_)
        SetWindowTextW(hwnd_, toWide(title_).c_str());
}

void D3dWindow::setVisible(bool on)
{
    if (!hwnd_)
        return;
    // STYRPLATTAN vid FÖRSTA visningen: ett fönster som
    // öppnades dolt (`desc.hidden` — dold start) kopplades inte i init, och
    // utan den här raden hade det aldrig fått styrplattans gester. Före
    // ShowWindow, som i init.
    if (on && !touchpad_.attached())
        touchpad_.attach(hwnd_, *this);
    // STARTLÄGET vid FÖRSTA synligheten: ett fönster
    // som öppnades dolt har inte visats än, och det är nu startläget gäller.
    if (on && !firstShown_) {
        showFirst();
        return;
    }
    ShowWindow(hwnd_, on ? (noActivate_ ? SW_SHOWNOACTIVATE : SW_SHOW)
                         : SW_HIDE);
}

// Skärmrymden på Win32 ÄR fysiska px — klient-px mappar 1:1, origo via
// ClientToScreen. Positionen avser för EGET CHROME KLIENTENS hörn:
// den osynliga storleksramen räknas bort, så
// `setPosition(clientToScreen(0, 0))` lämnar fönstret där det står — det
// dockningen, `setLocation` och `geometry()` bygger på. För Native avser den
// YTTERramen (som CreateWindowExW); för popups (Chrome::None, ingen ram) är
// ytter == klient.

void D3dWindow::setPosition(int x, int y)
{
    if (!hwnd_)
        return;
    const RECT f = frameInsets();
    SetWindowPos(hwnd_, nullptr, x - f.left, y - f.top, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

Pointf D3dWindow::clientToScreen(float px, float py) const
{
    if (!hwnd_)
        return Pointf{0.0f, 0.0f};
    POINT p{0, 0};
    ClientToScreen(hwnd_, &p);
    return Pointf{float(p.x) + px, float(p.y) + py};
}

Rectf D3dWindow::screenArea() const
{
    if (!hwnd_)
        return Rectf{0.0f, 0.0f, 0.0f, 0.0f};
    // Arbetsytan på skärmen fönstret står på — fysiska px, samma rymd som
    // SetWindowPos.
    return workArea(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST));
}

// Upprepande fönster-timer: WM_TIMER → EventType::Timer via WndProc:en.
// SetTimer med samma id startar om perioden (dokumenterat Win32-beteende) —
// samma semantik som fasaden lovar. No-op-säkra före init.

void D3dWindow::startTimer(std::uint32_t id, std::uint32_t ms)
{
    if (hwnd_ && id != 0)
        SetTimer(hwnd_, id, ms, nullptr);
}

void D3dWindow::stopTimer(std::uint32_t id)
{
    if (hwnd_ && id != 0)
        KillTimer(hwnd_, id);
}

// ---------------------------------------------------------------- geometri

Sizef D3dWindow::clientSize() const
{
    RECT r{};
    if (hwnd_)
        GetClientRect(hwnd_, &r);
    return Sizef{static_cast<float>(r.right - r.left),
                 static_cast<float>(r.bottom - r.top)};
}

float D3dWindow::uiScale() const
{
    // Fulla skalan: per-fönster-DPI × användarskalan — faktorn
    // in EXAKT en gång; setSize/titlebarHeight/popup-vägen anropar hit.
    if (!hwnd_)
        return userScale();
    const UINT dpi = GetDpiForWindow(hwnd_);
    const float os = dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
    return os * userScale();
}

float D3dWindow::titlebarHeight() const
{
    return chrome_ == Chrome::Custom ? captionH_ * uiScale() : 0.0f;
}

// Fönster-kontrollerna (fasaden) — ShowWindow-vägarna.
// SW_RESTORE tar fönstret ur både minimerat och maximerat
// läge. No-op-säkra före init.
// FÖRE DEN FÖRSTA VISNINGEN byter de tre STARTLÄGET:
// `ShowWindow(SW_MAXIMIZE)` hade VISAT ett fönster som skapats dolt —
// mitt i `onLoad`, eller under en dold start — och `showFirst` tillämpar läget
// när fönstret visas.
void D3dWindow::minimize()
{
    if (hwnd_ && !firstShown_) {
        startState_ = WindowState::Minimized;
        return;
    }
    if (hwnd_)
        ShowWindow(hwnd_, SW_MINIMIZE);
}

void D3dWindow::maximize()
{
    if (hwnd_ && !firstShown_) {
        startState_ = WindowState::Maximized;
        return;
    }
    if (hwnd_ && !maximized())
        ShowWindow(hwnd_, SW_MAXIMIZE);
}

void D3dWindow::restore()
{
    if (hwnd_ && !firstShown_) {
        startState_ = WindowState::Normal;
        return;
    }
    if (hwnd_)
        ShowWindow(hwnd_, SW_RESTORE);
}

bool D3dWindow::maximized() const
{
    return hwnd_ && IsZoomed(hwnd_);
}

Rectf D3dWindow::contentArea() const
{
    const Sizef cs = clientSize();
    const float th = titlebarHeight();
    return Rectf{0.0f, th, cs.w, cs.h - th};
}

// ---------------------------------------------------------------- hit-test

// Hit-test-kartans zoner översatta till Win32:s HT-koder. OS:et
// sköter därefter draget/resizen nativt (inkl. Aero Snap och dubbelklick på
// titelraden).
LRESULT D3dWindow::hitTest(float x, float y) const
{
    // Kartan ur widget-trädet: glaze emitterar i
    // prioritetsordning (resize-hörn/-kanter, TopBar-barn, caption) —
    // första träffen vinner. Knapp-/klientroller = HTCLIENT (klicket går
    // till widgetarna; HTMAXBUTTON/snap-layout-hovern är uppskjuten);
    // ingen träff = HTCLIENT.
    //
    // KARTANS RESIZE-ROLLER HOPPAS ÖVER, som Metal
    // gör: OS:et äger kanterna — storleksramen ligger UTANFÖR den synliga
    // kanten (WM_NCCALCSIZE) och WM_NCHITTEST lämnar den åt DefWindowProc.
    // Zonerna ligger INNANFÖR klientytan, och att svara på dem hade tagit
    // de yttersta px av allt innehåll vid kanten (verktygsfältet mot
    // vänsterkanten blev en storleksändring). Linux läser dem
    // där ingen marginal finns utanför ytan.
    for (const auto& r : hitRegions_) {
        if (!r.rect.contains(x, y))
            continue;
        switch (r.role) {
        case HitRole::Caption:
            return HTCAPTION;
        case HitRole::ResizeLeft:
        case HitRole::ResizeRight:
        case HitRole::ResizeTop:
        case HitRole::ResizeBottom:
        case HitRole::ResizeTopLeft:
        case HitRole::ResizeTopRight:
        case HitRole::ResizeBottomLeft:
        case HitRole::ResizeBottomRight:
            continue;
        default:
            return HTCLIENT;
        }
    }
    return HTCLIENT;
}

// ---------------------------------------------------------------- WndProc

namespace {

// Level-0-symbolen ur ett VK enligt kontraktet vid `enum class Key`
// (gfx/events.h): den symbol tangenten ger UTAN modifierare i den AKTIVA
// layouten. `MapVirtualKeyW(vk, MAPVK_VK_TO_CHAR)` svarar på exakt den
// frågan och — till skillnad från `ToUnicode` — rör aldrig kernelns
// tangentbordsstate, så dead keys kan inte gå sönder av att vi frågar.
//
// Returvärdets bit 31 markerar en DEAD KEY; en sådan har ingen `Key`-post
// och ska ge Unknown (tecknet kommer via WM_CHAR när den komponerats).
Key symbolFromVk(WPARAM vk)
{
    const UINT ch = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_CHAR);
    if (ch == 0 || (ch & 0x80000000u))
        return Key::Unknown;
    switch (static_cast<wchar_t>(ch & 0xffffu)) {
    case L'-':  return Key::Minus;
    case L'+':  return Key::Plus;
    case L'=':  return Key::Equals;
    case L',':  return Key::Comma;
    case L'.':  return Key::Period;
    case L'/':  return Key::Slash;
    case L'\\': return Key::Backslash;
    case L';':  return Key::Semicolon;
    case L'\'': return Key::Apostrophe;
    case L'[':  return Key::LeftBracket;
    case L']':  return Key::RightBracket;
    case L'`':  return Key::Grave;
    case L'%':  return Key::Percent;
    case L'*':  return Key::Asterisk;
    default:    return Key::Unknown;
    }
}

Key translateKey(WPARAM vk, LPARAM lp)
{
    // Bokstäver/siffror ligger på ASCII-värden som virtual keys
    if (vk >= 'A' && vk <= 'Z')
        return static_cast<Key>(static_cast<int>(Key::A) + (vk - 'A'));
    if (vk >= '0' && vk <= '9')
        return static_cast<Key>(static_cast<int>(Key::N0) + (vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12)
        return static_cast<Key>(static_cast<int>(Key::F1) + (vk - VK_F1));

    // NUMPADEN: VK_NUMPAD0–9 levereras BARA när NumLock är
    // på — är den av skickar Windows navigations-VK:na (VK_END m.fl.) i
    // stället, vilket är exakt det utfall kontraktet beskriver.
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return static_cast<Key>(static_cast<int>(Key::KP_N0) +
                                (vk - VK_NUMPAD0));

    // NAVIGATIONSKLUSTRET: numpadens navigationsläge och de
    // dedikerade tangenterna delar VK (VK_END är båda), och det enda som
    // skiljer dem är EXTENDED-flaggan i lParam (bit 24, `KF_EXTENDED` i
    // HIWORD) — satt för den dedikerade tangenten, osatt för numpadens.
    // Kända avvikare, skrivna: SendInput utan `KEYEVENTF_EXTENDEDKEY`
    // (skärmtangentbord, viss fjärrstyrning) ger de dedikerade tangenterna
    // utan flagga, och de blir då `KP_*`. Widgetarna märker ingenting —
    // `AppWindow` viker `KP_*` till tvillingen — bara en app som binder
    // `End` men inte `KP_End` gör det.
    const bool dedikerad = (HIWORD(lp) & KF_EXTENDED) != 0;
    auto nav = [dedikerad](Key egen, Key numpad) {
        return dedikerad ? egen : numpad;
    };

    switch (vk) {
    case VK_RETURN: return Key::Return;   // även numpadens (KF_EXTENDED) —
                                          // KP_Enter fälls ihop med Return
    case VK_ESCAPE: return Key::Escape;
    case VK_TAB:    return Key::Tab;
    case VK_BACK:   return Key::Backspace;
    case VK_SPACE:  return Key::Space;
    case VK_DELETE: return nav(Key::Delete, Key::KP_Delete);
    case VK_LEFT:   return nav(Key::Left, Key::KP_Left);
    case VK_RIGHT:  return nav(Key::Right, Key::KP_Right);
    case VK_UP:     return nav(Key::Up, Key::KP_Up);
    case VK_DOWN:   return nav(Key::Down, Key::KP_Down);
    case VK_HOME:   return nav(Key::Home, Key::KP_Home);
    case VK_END:    return nav(Key::End, Key::KP_End);
    case VK_PRIOR:  return nav(Key::PageUp, Key::KP_PageUp);
    case VK_NEXT:   return nav(Key::PageDown, Key::KP_PageDown);
    case VK_INSERT: return nav(Key::Insert, Key::KP_Insert);
    // Mitttangenten (numpad-5, NumLock av). Ingen dedikerad tvilling.
    case VK_CLEAR:  return Key::KP_Begin;

    case VK_ADD:      return Key::KP_Add;
    case VK_SUBTRACT: return Key::KP_Subtract;
    case VK_MULTIPLY: return Key::KP_Multiply;
    case VK_DIVIDE:   return Key::KP_Divide;
    case VK_DECIMAL:  return Key::KP_Decimal;

    // SKILJETECKNEN går via layouten och inte via VK_OEM_*-konstanterna:
    // `VK_OEM_PLUS` heter så efter US-layouten, men på svensk layout
    // sitter `+` på en helt annan tangent. Level-0-kontraktet säger
    // SYMBOL, inte position — och `MapVirtualKeyW` är den frågan.
    default:        return symbolFromVk(vk);
    }
}

std::uint32_t currentMods()
{
    // GetKeyState täcker både vänster- och högertangenten
    std::uint32_t mods = 0;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= kModCtrl;
    if (GetKeyState(VK_SHIFT) & 0x8000)   mods |= kModShift;
    if (GetKeyState(VK_MENU) & 0x8000)    mods |= kModAlt;
    if ((GetKeyState(VK_LWIN) & 0x8000) ||
        (GetKeyState(VK_RWIN) & 0x8000))  mods |= kModCmd;
    return mods;
}

// lParam-koordinater är signerade (negativa vid capture utanför fönstret)
float mouseX(LPARAM lp) { return static_cast<float>(static_cast<short>(LOWORD(lp))); }
float mouseY(LPARAM lp) { return static_cast<float>(static_cast<short>(HIWORD(lp))); }

// Alla push-hjälpare stämplar avsändande fönster i Event::windowId.

void pushSimple(WindowId wid, EventType t)
{
    Event e;
    e.type     = t;
    e.windowId = wid;
    pushEvent(e);
}

void pushMouse(WindowId wid, EventType t, float x, float y,
               MouseButton b = MouseButton::None)
{
    Event e;
    e.type     = t;
    e.windowId = wid;
    e.mouseX   = x;
    e.mouseY   = y;
    e.button   = b;
    // Modifierarna även för musen (Ctrl-/Shift-klick i en lista).
    // `GetKeyState` följer meddelandeköns takt, så läget är det som gällde
    // när just det här mus-meddelandet skapades.
    e.mods     = currentMods();
    pushEvent(e);
}

// `text` är texten trycket skriver (KeyDown) —
// kopierad som i pushText men trunkerad på en UTF-8-teckengräns, för den
// kan bära flera tecken (´ + x).
void pushKey(WindowId wid, EventType t, WPARAM vk, LPARAM lp,
             const char* text = nullptr)
{
    Event e;
    e.type     = t;
    e.windowId = wid;
    e.key      = translateKey(vk, lp);
    e.mods     = currentMods();
    if (text) {
        std::size_t n = std::strlen(text);
        if (n >= sizeof(e.text)) {
            n = sizeof(e.text) - 1;
            while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80)
                --n;
        }
        std::memcpy(e.text, text, n);
    }
    pushEvent(e);
}

void pushText(WindowId wid, const char* utf8)
{
    Event e;
    e.type     = EventType::TextInput;
    e.windowId = wid;
    // text[32] är nollad av default-initialiseringen — kopiera max 31 byte
    // (ett kodpunkts-UTF-8 är max 4 byte; trunkering sker aldrig här).
    std::size_t n = std::strlen(utf8);
    if (n >= sizeof(e.text))
        n = sizeof(e.text) - 1;
    std::memcpy(e.text, utf8, n);
    pushEvent(e);
}

void pushTimer(WindowId wid, std::uint32_t id)
{
    Event e;
    e.type     = EventType::Timer;
    e.windowId = wid;
    e.timerId  = id;
    pushEvent(e);
}

} // namespace

// Hjulet och gesten står UTANFÖR den anonyma namnrymden: styrplattan
// (d3d11_touchpad.cpp) går samma väg (d3d11_backend.h).
void pushWheel(WindowId wid, float x, float y, float dx, float dy,
               bool precise, GesturePhase phase)
{
    Event e;
    e.type     = EventType::MouseWheel;
    e.windowId = wid;
    e.mouseX   = x;
    e.mouseY   = y;
    e.wheelX   = dx;
    e.wheelY   = dy;
    e.precise  = precise;
    e.phase    = phase;
    // Modifierarna även för hjulet: Ctrl/Cmd +
    // styrplattans drag kretsar på ritytan, och draget direkt efter ett
    // Ctrl-tryck har ingen mushändelse emellan som kunde ha burit dem.
    e.mods     = currentMods();
    pushEvent(e);
}

void pushGesture(WindowId wid, float x, float y, GesturePhase phase,
                 float scale, float rotation)
{
    Event e;
    e.type            = EventType::Gesture;
    e.windowId        = wid;
    e.mouseX          = x;
    e.mouseY          = y;
    e.phase           = phase;
    e.gestureScale    = scale;
    e.gestureRotation = rotation;
    e.mods            = currentMods();
    pushEvent(e);
}

// WM_CHAR-AVKODNINGEN, delad av WM_CHAR och
// WM_KEYDOWN:s uttag ur kön: en UTF-16-kodenhet in, ett HELT tecken som
// UTF-8 ut (true). Surrogatpar kommer som två meddelanden — den höga halvan
// väntar i highSurrogate_. Kontrolltecken (Backspace/Tab/CR/Escape,
// Ctrl+bokstav < 0x20) filtreras: de är kommandon och går via KeyDown.
bool D3dWindow::charText(wchar_t ch, std::string& out)
{
    wchar_t buf[2];
    int     n = 0;
    if (IS_HIGH_SURROGATE(ch)) {
        highSurrogate_ = ch;   // vänta på den låga halvan
        return false;
    }
    if (IS_LOW_SURROGATE(ch)) {
        if (!highSurrogate_)
            return false;      // ensam låg halva — ogiltig, släpp
        buf[n++] = highSurrogate_;
        buf[n++] = ch;
        highSurrogate_ = 0;
    } else {
        highSurrogate_ = 0;
        if (ch < 0x20 || ch == 0x7F)
            return false;      // kontrolltecken — inte text
        buf[n++] = ch;
    }
    char utf8[8] = {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, buf, n, utf8,
                                        sizeof(utf8) - 1, nullptr, nullptr);
    if (len <= 0)
        return false;
    out.assign(utf8, static_cast<std::size_t>(len));
    return true;
}

LRESULT CALLBACK D3dWindow::wndProcThunk(HWND hwnd, UINT msg,
                                         WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE) {
        auto* cs   = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* self = static_cast<D3dWindow*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;   // meddelanden kommer redan under CreateWindowExW
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    auto* self = reinterpret_cast<D3dWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self || !self->hwnd_)
        return DefWindowProcW(hwnd, msg, wp, lp);
    return self->wndProc(msg, wp, lp);
}

// Win32-meddelande → gfx::Event-översättningen. Meddelanden utan
// motsvarighet går till DefWindowProc och köar inget — de behandlas som
// "inget hände" och triggar ingen omritning (händelsestyrd rendering).
// OBS: fönsterhändelserna nedan måste täcka ALLA typer AppWindow::handleEvent
// invalidatar på — tappas en blir symtomet "UI:t uppdateras inte förrän jag
// rör musen".
LRESULT D3dWindow::wndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {

    // ---- fönstrets liv ----
    case WM_CLOSE:
        // Per-fönster-stängning (kryss/Alt+F4) — appen bestämmer, ingen
        // destroy här. Quit är reserverat för app-nivå (WM_QUIT, id 0).
        pushSimple(windowId_, EventType::WindowCloseRequested);
        return 0;

    case WM_ERASEBKGND:
        return 1;   // vi ritar allt själva — låt inte GDI blinka till

    case WM_PAINT: {
        // Validera ytan (annars sprutar WM_PAINT) och rita direkt — det här
        // är vägen som håller fönstret levande under modala OS-loopar.
        PAINTSTRUCT ps;
        BeginPaint(hwnd_, &ps);
        EndPaint(hwnd_, &ps);
        renderFrame();
        pushSimple(windowId_, EventType::WindowExposed);
        return 0;
    }

    // Modal drag/resize-loop: presentera osynkat så den nyritade framen
    // hinner ut per WM_SIZE — vsync-blockerad Present gör att DWM visar
    // förra framens backbuffer uppskalad ("sträcks konstigt", fix 3).
    case WM_ENTERSIZEMOVE:
        gfxRenderer_.setUnsyncedPresent(true);
        break;   // vidare till DefWindowProc
    case WM_EXITSIZEMOVE: {
        gfxRenderer_.setUnsyncedPresent(false);
        // Den modala loopen ÅT varje TRÅDmeddelande som postades under den
        // — PostThreadMessage-dokumentationen: de går förlorade i en modal
        // loop. `postWakeup` och pumpens puff är båda sådana, så en
        // `glaze::app().post()` från en tråd under en dragning väntade
        // efteråt på nästa meddelande. Köa
        // därför en väckning nu: App dränerar då sin postkö när dragningen
        // släpps. Ett överflödigt Wakeup kostar ett tomt pumpvarv.
        Event w{};
        w.type = EventType::Wakeup;
        pushEvent(w);
        break;   // vidare till DefWindowProc
    }

    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) {
            wasMaxOrMin_ = true;   // nästa SIZE_RESTORED är en riktig restore
            return 0;
        }
        if (wp == SIZE_MAXIMIZED)
            pushSimple(windowId_, EventType::WindowMaximized);
        else if (wp == SIZE_RESTORED && wasMaxOrMin_)
            pushSimple(windowId_, EventType::WindowRestored);
        {
        const bool restoreFlank = (wp == SIZE_RESTORED && wasMaxOrMin_);
        wasMaxOrMin_ = (wp == SIZE_MAXIMIZED);
        // ANVÄNDAR-/OS-resize: räkna tillbaka den lagrade LOGISKA
        // storleken (fysiska px ÷ fulla skalan). ALDRIG ur vår egen
        // skalresize (vakten — avrundningen driftar annars), aldrig
        // vid maximering och ALDRIG PÅ RESTORE-FLANKEN: Windows
        // återställer den ihågkomna rect:en i den skala som gällde vid
        // maximeringen och vet ingenting om vår, så en tillbakaräkning
        // där gör 800×520 logiska till 640×416 vid ett skalbyte
        // 1.0 → 1.25.
        if (!pendingScaleResize_ && wp != SIZE_MAXIMIZED && !restoreFlank) {
            const int cw = static_cast<int>(LOWORD(lp));
            const int ch = static_cast<int>(HIWORD(lp));
            const float s = uiScale();
            if (cw > 0 && ch > 0 && s > 0.0f) {
                logicalW_ = std::max(
                    1, static_cast<int>(std::lround(cw / s)));
                logicalH_ = std::max(
                    1, static_cast<int>(std::lround(ch / s)));
            }
        }
        // RESTORE-KORRIGERINGEN: sätt om FYSISK storlek ur den lagrade
        // LOGISKA i AKTUELL skala. `SIZE_MINIMIZED` sätter också
        // `wasMaxOrMin_`, men en restore-från-minimerat ger samma
        // klientstorlek — jämförelsen no-op:ar då, korrekt gratis.
        //
        // REENTRANSEN: `SetWindowPos` kör WM_SIZE SYNKRONT, alltså
        // nästlat inuti det här anropet. Vakten stoppar den inre
        // tillbakaräkningen, och ÄGARSKAPET är skrivet: den INRE
        // körningen har den slutgiltiga storleken och äger därför både
        // renderer-resizen och event-emissionen — den YTTRE returnerar
        // direkt efter korrigeringen utan att pusha om. Annars får en
        // enda storleksändring dubbla WindowResized, och `glaze::App`
        // ritar om per event (regel 1).
        if (restoreFlank && !pendingScaleResize_) {
            const int   cw = static_cast<int>(LOWORD(lp));
            const int   ch = static_cast<int>(HIWORD(lp));
            const float s  = uiScale();
            const int   pw = std::max(
                  1, static_cast<int>(std::lround(logicalW_ * s)));
            const int   ph = std::max(
                  1, static_cast<int>(std::lround(logicalH_ * s)));
            if (s > 0.0f && (pw != cw || ph != ch)) {
                // KLIENT → FÖNSTER. `WM_SIZE`:s lParam är KLIENT-måttet
                // medan `SetWindowPos` tar FÖNSTER-måttet: vid None är de
                // samma, men `Chrome::Native` har en OS-ram utanpå och ett
                // ändringsbart Custom-fönster sin osynliga storleksram
                // — utan den blir klientytan för liten
                // med exakt ramens storlek. Samma väg som `userScaleChanged`.
                const RECT r = windowRectFor(pw, ph);
                pendingScaleResize_ = true;
                SetWindowPos(hwnd_, nullptr, 0, 0, r.right - r.left,
                             r.bottom - r.top,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                pendingScaleResize_ = false;
                return 0;   // den INRE körningen ägde emissionen
            }
        }
        // NORMALRUTAN: en storleksändring från
        // vänster- eller överkanten flyttar också hörnet, och återställningen
        // lägger tillbaka rutan — båda i normalläge. Maximeringen och
        // minimeringen hoppas över av `trackNormalPosition` själv.
        trackNormalPosition();
        pushSimple(windowId_, EventType::WindowResized);
        pushSimple(windowId_, EventType::WindowPixelSizeChanged);
        onResize();   // swapchain + omritning — live-resize
        return 0;
        }

    case WM_SHOWWINDOW:
        if (wp)
            pushSimple(windowId_, EventType::WindowShown);
        break;   // vidare till DefWindowProc

    case WM_SETFOCUS:
        pushSimple(windowId_, EventType::WindowFocusGained);
        return 0;

    case WM_KILLFOCUS:
        touchpad_.onFocusLost();   // WM_INPUT följer fokus — vridningen slutar
        pushSimple(windowId_, EventType::WindowFocusLost);
        return 0;

    case WM_SETTINGCHANGE:
        // OS:ets ljus/mörkt-byte annonseras som "ImmersiveColorSet" —
        // översätt till SystemThemeChanged så appen invalidatar och nästa
        // repaint resolvar temat på nytt (via gfx::systemTheme()).
        if (lp && wcscmp(reinterpret_cast<const wchar_t*>(lp),
                         L"ImmersiveColorSet") == 0)
            pushSimple(windowId_, EventType::SystemThemeChanged);
        break;   // vidare till DefWindowProc

    case WM_DPICHANGED: {
        // Flytta/storleksändra till OS:ets förslag för den nya skärmen
        // (dess placering respekterar snap/monitorkant — den vill vi ha).
        // WM_SIZE körs SYNKRONT under SetWindowPos, så renderern har nya
        // storleken när skal-eventet går ut efteråt. Vakten:
        // logisk storlek är INVARIANTEN och får inte räknas tillbaka ur
        // OS-rectet — avrundningen skulle drifta för varje DPI-byte.
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        pendingScaleResize_ = true;
        SetWindowPos(hwnd_, nullptr, r->left, r->top,
                     r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        pendingScaleResize_ = false;
        // Skärmens mm FÖRE skal-eventet: ett DPI-byte är oftast en ANNAN
        // skärm, och glaze läser om mätningen när eventet kommer. (Reserven
        // följer inte skalan.)
        measureDisplay(true);
        // Programikonen i den nya DPI:n — SM_CXSMICON/SM_CXICON följer den.
        applyIcon();
        pushSimple(windowId_, EventType::WindowDisplayScaleChanged);
        return 0;
    }

    // SKÄRMENS mm: fönstret flyttade — kanske till en
    // annan skärm — eller en skärm bytte läge. Mätningen är billig när
    // ingenting ändrats (se measureDisplay) och köar WindowDisplayChanged
    // först när svaret blev ett annat. NORMALRUTAN
    // följer flytten — bara i normalläge, det avgör `trackNormalPosition`.
    case WM_MOVE:
        trackNormalPosition();
        measureDisplay(true);
        break;   // vidare till DefWindowProc
    case WM_DISPLAYCHANGE:
        measureDisplay(true);
        break;

    // STORLEKSGRÄNSERNA — beräknade VID FRÅGAN, inte vid
    // sättningen: DPI och userScale kan ha ändrats efter att appen satte
    // talen, och den här formen gör skalbyten gratis. De tre andra
    // backendarna måste återapplicera; Windows behöver inte.
    //
    // MMI talar FÖNSTER-storlek (inte klientyta), så ramen läggs på:
    // Native-ramen, och för ett ändringsbart Custom-fönster den osynliga
    // storleksramen. NORMALLÄGETS ram också medan
    // fönstret är maximerat (`windowRectFor` → `normalFrame`):
    // gränserna gäller fönstret det återställs till.
    case WM_GETMINMAXINFO: {
        if (!limMinW_ && !limMinH_ && !limMaxW_ && !limMaxH_)
            break;   // inga gränser satta — låt OS:et bestämma
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        const float s = uiScale();
        auto toWindow = [&](int logicalW, int logicalH) {
            const RECT r =
                windowRectFor(static_cast<int>(std::lround(logicalW * s)),
                              static_cast<int>(std::lround(logicalH * s)));
            return SIZE{r.right - r.left, r.bottom - r.top};
        };
        const SIZE minsta  = toWindow(limMinW_, limMinH_);
        const SIZE största = toWindow(limMaxW_, limMaxH_);
        if (limMinW_ > 0)
            mmi->ptMinTrackSize.x = minsta.cx;
        if (limMinH_ > 0)
            mmi->ptMinTrackSize.y = minsta.cy;
        if (limMaxW_ > 0)
            mmi->ptMaxTrackSize.x = största.cx;
        if (limMaxH_ > 0)
            mmi->ptMaxTrackSize.y = största.cy;
        return 0;
    }

    // ---- eget chrome ----
    case WM_NCCALCSIZE:
        if (chrome_ == Chrome::Custom) {
            // Klientytan = fönstret minus `frameInsets()` (gäller BÅDA
            // wParam-varianterna — annars ritar DWM native-ramen): ett
            // ändringsbart fönster behåller den osynliga storleksramen på
            // vänster-, höger- och nederkanten medan överkantens
            // non-client-yta tas bort (Windows Terminals
            // `_OnNcCalcSize`); maximerat dras klientytan in med
            // ramtjockleken på alla fyra sidor, så innehållet hamnar på
            // skärmen; ett fast fönster har ingen ram.
            // rgrc[0] (wp=TRUE) och RECT (wp=FALSE) ligger båda först.
            auto*      r = reinterpret_cast<RECT*>(lp);
            const RECT f = frameInsets();
            r->left   += f.left;
            r->top    += f.top;
            r->right  -= f.right;
            r->bottom -= f.bottom;
            return 0;
        }
        break;

    case WM_NCHITTEST:
        if (chrome_ == Chrome::Custom) {
            POINT pt{static_cast<LONG>(static_cast<short>(LOWORD(lp))),
                     static_cast<LONG>(static_cast<short>(HIWORD(lp)))};
            ScreenToClient(hwnd_, &pt);
            // STORLEKSRAMEN — bara för ett ändringsbart
            // fönster i normalläge. En punkt UTANFÖR klientytan ligger i
            // OS:ets ram (vänster, höger, under): DefWindowProc svarar, för
            // den delen av ramen är orörd (WT: "handles the left, right and
            // bottom parts of the frame because we didn't change them").
            // Överkanten har ingen ram kvar, så ett band lika tjockt som
            // ramen inne i klientytan svarar HTTOP (WT:s isOnResizeBorder) —
            // skriven gräns: de översta px av namnlisten och dess knappar,
            // som över Windows egna fönsterknappar.
            const RECT f = normalFrame();
            if (f.bottom > 0 && !IsZoomed(hwnd_)) {
                RECT kl{};
                GetClientRect(hwnd_, &kl);
                if (pt.x < 0 || pt.y < 0 || pt.x >= kl.right ||
                    pt.y >= kl.bottom)
                    return DefWindowProcW(hwnd_, msg, wp, lp);
                if (pt.y < f.bottom)
                    return HTTOP;
            }
            return hitTest(static_cast<float>(pt.x),
                           static_cast<float>(pt.y));
        }
        if (transparent_) {
            // Alfa-hit-testen: genomskinliga pixlar släpper klicket IGENOM
            // till fönstret under — fri geometri utan region-hack. Läser
            // vår egen retained-alfa (CPU-spegeln, uppdaterad per present).
            POINT pt{static_cast<LONG>(static_cast<short>(LOWORD(lp))),
                     static_cast<LONG>(static_cast<short>(HIWORD(lp)))};
            ScreenToClient(hwnd_, &pt);
            return gfxRenderer_.alphaHit(pt.x, pt.y) ? HTCLIENT
                                                     : HTTRANSPARENT;
        }
        break;

    // MUSPEKAREN. KLIENTYTAN visar den begärda formen; chromets
    // zoner (resize-kanter/-hörn, caption) lämnas åt DefWindowProc, som
    // väljer OS:ets egna pilar ur hit-test-koden. Utan grenen hade klassens
    // pil skrivits över den begärda formen vid varje rörelse. `wp` är
    // fönstret under pekaren — ett barnfönster får svara för sig självt.
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && reinterpret_cast<HWND>(wp) == hwnd_) {
            SetCursor(cursorHandle(cursor_));
            return TRUE;
        }
        break;   // vidare till DefWindowProc

    case WM_MOUSEACTIVATE:
        // noActivate-fönster (popups): WS_EX_NOACTIVATE stoppar INTE
        // klick-aktivering ensamt — utan MA_NOACTIVATE-svaret aktiveras
        // popupen vid äkta klick, ägaren får WM_KILLFOCUS →
        // WindowFocusLost → App::closePopups river listan innan klicket
        // processas ("popupen försvinner men inget väljs"). Mac-parallellen
        // är NSPanel:s nonactivating-beteende.
        if (noActivate_)
            return MA_NOACTIVATE;
        break;   // vidare till DefWindowProc (MA_ACTIVATE)

    case WM_NCMOUSEMOVE: {
        // Musen över dragbara ytor (HTCAPTION m.m.) ger inga vanliga
        // mus-events — översätt ändå så knapp-hovern i chromet släcks.
        POINT pt{static_cast<LONG>(static_cast<short>(LOWORD(lp))),
                 static_cast<LONG>(static_cast<short>(HIWORD(lp)))};
        ScreenToClient(hwnd_, &pt);
        pushMouse(windowId_, EventType::MouseMotion, static_cast<float>(pt.x),
                  static_cast<float>(pt.y));
        break;
    }

    // ---- mus ----
    case WM_MOUSEMOVE:
        if (!mouseInside_) {
            mouseInside_ = true;
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);   // ger WM_MOUSELEAVE när musen lämnar
        }
        pushMouse(windowId_, EventType::MouseMotion, mouseX(lp), mouseY(lp));
        return 0;

    case WM_MOUSELEAVE:
        mouseInside_ = false;
        pushSimple(windowId_, EventType::MouseLeave);
        return 0;

    // MUSENS CAPTURE TOGS (`EventType::MouseCaptureLost`).
    // lParam är fönstret som får capture —
    // NULL när den bara släpptes, också när DefWindowProc svarar på
    // `WM_CANCELMODE` (Alt+Tab, en systemdialog), som därför inte behöver en
    // egen gren. Ett AVBROTT bara när fönstret höll capture för ett tryck
    // (`pressCapture_` — vårt eget släpp nollar den före `ReleaseCapture`,
    // och OS:ets flytt-/storleksloop har aldrig satt den), och inte när
    // capture går till fönstret självt eller lämnas över UTTRYCKLIGEN
    // (`captureMouse` medan `g_handoverTo` pekar på mottagaren). Ett annat av
    // appens fönster som tar capture på annat sätt är en förlust
    // — annars hade källans tryck kunnat fastna.
    case WM_CAPTURECHANGED: {
        const bool hadPress    = pressCapture_;
        pressCapture_          = false;
        const HWND ny          = reinterpret_cast<HWND>(lp);
        const bool överlämning = ny != nullptr && ny == g_handoverTo;
        if (hadPress && ny != hwnd_ && !överlämning)
            pushSimple(windowId_, EventType::MouseCaptureLost);
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
        // Capture: drag som lämnar fönstret fortsätter ge events tills släpp
        SetCapture(hwnd_);
        pressCapture_ = true;   // ett tryck äger den (MouseCaptureLost-vakten)
        pushMouse(windowId_, EventType::MouseButtonDown, mouseX(lp), mouseY(lp),
                  msg == WM_LBUTTONDOWN   ? MouseButton::Left
                  : msg == WM_MBUTTONDOWN ? MouseButton::Middle
                                          : MouseButton::Right);
        return 0;

    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
        // Capture släpps när den SISTA knappen släpps — wParam bär de som
        // fortfarande är nere. Att släppa den vid FÖRSTA
        // släppet tappade nästa knapps släpp utanför fönstret: en
        // mittenpanorering som höll medan ett vänsterklick släpptes hade
        // fortsatt på vanliga rörelser tills nästa mittentryck. X11 och
        // Wayland håller den implicita grabben tills alla knappar är uppe.
        // Vårt EGET släpp: `pressCapture_` nollas före, så meddelandet
        // `WM_CAPTURECHANGED` under anropet inte blir ett avbrott.
        if (!(wp & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON))) {
            pressCapture_ = false;
            ReleaseCapture();
        }
        pushMouse(windowId_, EventType::MouseButtonUp, mouseX(lp), mouseY(lp),
                  msg == WM_LBUTTONUP   ? MouseButton::Left
                  : msg == WM_MBUTTONUP ? MouseButton::Middle
                                        : MouseButton::Right);
        return 0;

    // STYRPLATTAN (d3d11_touchpad.cpp). DM_POINTERHITTEST är det
    // första meddelandet i en styrplattesekvens: tar Direct Manipulation
    // kontakten kommer tvåfingersdraget som PRECIST hjul och nypningen som
    // `Gesture` i stället för WM_MOUSEWHEEL. Båda vidare till DefWindowProc —
    // WM_INPUT MÅSTE dit (det städar rapportens handtag).
    case DM_POINTERHITTEST:
        touchpad_.onPointerHitTest(wp);
        break;
    case WM_INPUT:
        touchpad_.onRawInput(lp);
        break;

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        // Hjulet ger multiplar av WHEEL_DELTA (120) → rader. Fasadens
        // tecken: vertikalt positivt = uppåt/bort, horisontellt = höger.
        // Ett MUSHJUL (`precise` false): styrplattan når hit bara när
        // Direct Manipulation inte tog den (reserven — gammalt Windows,
        // ingen precisionsplatta), och då går den inte att skilja från ett.
        // OBS: hjul-lParam är SKÄRM-koordinater (till skillnad från övriga
        // mus-meddelanden) — översätt till klientyta för hit-test.
        POINT pt{static_cast<LONG>(static_cast<short>(LOWORD(lp))),
                 static_cast<LONG>(static_cast<short>(HIWORD(lp)))};
        ScreenToClient(hwnd_, &pt);
        const float rows =
            static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
        pushWheel(windowId_,
                  static_cast<float>(pt.x), static_cast<float>(pt.y),
                  msg == WM_MOUSEHWHEEL ? rows : 0.0f,
                  msg == WM_MOUSEWHEEL  ? rows : 0.0f);
        return 0;
    }

    // ---- tangentbord ----
    case WM_KEYDOWN: {
        // TANGENTENS TEXT: pumpen kör
        // TranslateMessage FÖRE DispatchMessageW, så de WM_CHAR tangenten
        // ger ligger redan i kön ("The character messages are posted to the
        // calling thread's message queue"). De hämtas här, avkodas som i
        // WM_CHAR-grenen och köas EFTER KeyDown, som bär den sammanslagna
        // texten — ett uttaget WM_CHAR dispatchas aldrig igen. PeekMessage
        // hämtar WM_QUIT oavsett filtret: avslutet läggs tillbaka och
        // slingan bryts. WM_SYSKEYDOWN ger WM_SYSCHAR och ingen text.
        std::string              text;
        std::vector<std::string> delar;
        MSG m;
        while (PeekMessageW(&m, hwnd_, WM_CHAR, WM_CHAR, PM_REMOVE)) {
            if (m.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(m.wParam));
                break;
            }
            std::string del;
            if (charText(static_cast<wchar_t>(m.wParam), del)) {
                text += del;
                delar.push_back(std::move(del));
            }
        }
        pushKey(windowId_, EventType::KeyDown, wp, lp, text.c_str());
        for (const std::string& d : delar)
            pushText(windowId_, d.c_str());
        break;   // DefWindowProc
    }

    case WM_SYSKEYDOWN:
        pushKey(windowId_, EventType::KeyDown, wp, lp);
        break;   // DefWindowProc — Alt+F4 m.fl. ska fungera

    case WM_KEYUP:
    case WM_SYSKEYUP:
        pushKey(windowId_, EventType::KeyUp, wp, lp);
        break;

    case WM_CHAR: {
        // Ett WM_CHAR som inte följde ett WM_KEYDOWN (det tar sina egna ur
        // kön): Alt+numpad-koden vid Alt-släppet, ett postat tecken.
        // Avkodningen är charText.
        std::string utf8;
        if (charText(static_cast<wchar_t>(wp), utf8))
            pushText(windowId_, utf8.c_str());
        return 0;
    }

    case WM_TIMER:
        // Startad via startTimer — id:t är wParam. Väcker waitEvent så
        // appen kan agera på tid utan polling (caret-blink m.m.). Styrplattans
        // PRIVATA pumptimer (ett id utanför fasadens) blir aldrig ett event.
        if (touchpad_.onTimer(wp))
            return 0;
        pushTimer(windowId_, static_cast<std::uint32_t>(wp));
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// ---------------------------------------------------------------- events

bool D3dWindow::processEvent(const Event& e)
{
    switch (e.type) {
    // false = "det här fönstret begär stängning" — både per-fönster-
    // begäran (krysset, Alt+F4) och app-avslut (Quit når varje fönster).
    case EventType::Quit:
    case EventType::WindowCloseRequested:
        return false;

    // Chrome-knapparna är CaptionButton-widgets, så
    // musen går orörd till widget-lagret.
    default:
        break;
    }
    return true;
}

// Användarskalan bytte (anropas av glaze::App per öppet fönster):
// ny fysisk storlek ur den LAGRADE logiska (aldrig inkrementellt), event
// EFTER renderer-resizen. WM_SIZE körs synkront under SetWindowPos, så
// när eventet pushas har swapchain/retained-RT redan nya storleken.
void D3dWindow::userScaleChanged()
{
    if (!hwnd_)
        return;
    if (maximized()) {
        // OS:et äger maximerad geometri — hoppa resizen men pusha
        // eventet (fonten måste ändå om). Lagrad logisk storlek röres
        // inte, och det RÄCKER INTE av sig självt: Windows återställer
        // den ihågkomna rect:en i den GAMLA skalan, så restore-flanken i
        // WM_SIZE måste sätta om fysisk storlek ur den lagrade logiska.
        pushSimple(windowId_, EventType::WindowDisplayScaleChanged);
        return;
    }
    const float s  = uiScale();
    const int   pw = std::max(1, static_cast<int>(
                                     std::lround(logicalW_ * s)));
    const int   ph = std::max(1, static_cast<int>(
                                     std::lround(logicalH_ * s)));
    const Sizef cs = clientSize();
    if (pw == static_cast<int>(cs.w) && ph == static_cast<int>(cs.h)) {
        // Samma pixelstorlek ⇒ ingen WM_SIZE kommer — eventet direkt,
        // annars laddas fonten aldrig om (X11-mönstret).
        pushSimple(windowId_, EventType::WindowDisplayScaleChanged);
        return;
    }
    // Klientyta pw×ph: vid None ÄR klientytan hela fönstret; Native lägger
    // på OS-ramen och ett ändringsbart Custom-fönster sin osynliga
    // storleksram utanpå.
    const RECT r = windowRectFor(pw, ph);
    pendingScaleResize_ = true;   // WM_SIZE ska inte räkna tillbaka logisk
    SetWindowPos(hwnd_, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    pendingScaleResize_ = false;
    pushSimple(windowId_, EventType::WindowDisplayScaleChanged);
}

// ---------------------------------------------------------------- ritning

void D3dWindow::drawChrome()
{
    if (chrome_ != Chrome::Custom)
        return;

    // Captionen (titelrad + knappar) ritas av
    // TopBar-widgeten i innehållspasset. Här finns bara
    // den tunna ramen runt omaximerat fönster.
    const Sizef cs = clientSize();
    if (!maximized())
        gfxRenderer_.strokeRect(Rectf{0.0f, 0.0f, cs.w, cs.h},
                                chromeColors_.frame);
}

} // namespace gfx
