// Win32-implementationen av plattformsfasaden (gfx/platform.h): global init
// (DPI-medvetenhet), den händelsestyrda event-pumpen (GetMessage blockerar =
// ~0 % CPU i vila), loggning, exe-katalog och fönsterfabriken.
//
// Win32 levererar de flesta händelser via WndProc:en (under DispatchMessage),
// inte som returvärde från GetMessage. Därför köar WndProc:en färdig-
// översatta gfx::Event via pushEvent(); waitEvent/pollEvent dränerar kön
// och pumpar meddelanden tills något hamnar i den.

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <deque>
#include <filesystem>

#include "../icon_util.h"
#include "d3d11_backend.h"
#include "gfx/platform.h"

#include <shellapi.h>         // ShellExecuteW (openUrl, openFile) — efter windows.h
#include <shellscalingapi.h>  // GetDpiForMonitor (screens) — dito
#include <shlobj.h>           // SHGetKnownFolderPath (knownPlaces) — dito

namespace gfx {

// ---------------------------------------------------------------- init/log

namespace {

// TRÅDSÄKER VÄCKNING — se `postWakeup` längre ner.
// UI-trådens id fångas HÄR, alltså PÅ UI-tråden: en workertråd som kallar
// `GetCurrentThreadId()` får sitt EGET id och hade postat meddelandet till
// en tråd utan pump.
// ATOMISK av samma skäl som Linux g_wakeupFd — läses från främmande tråd.
std::atomic<DWORD> g_uiThread{0};

// Eget fönsterlöst meddelande. `WM_APP + n` räcker och behöver inget
// `RegisterWindowMessage`: numret är privat för den här appen, och
// meddelandet skickas bara till VÅR egen tråd.
constexpr UINT kMsgWakeup = WM_APP + 0x51;

// PUFFEN — ett andra trådmeddelande, SKILT från väckningen
// ovan: väckningen köar ett `EventType::Wakeup`, puffen köar INGENTING. Den
// finns bara för att få `GetMessageW` att återvända när eventet den ska
// hämta redan ligger i kön — se `pushEvent`.
constexpr UINT kMsgPuff = WM_APP + 0x52;

// `WM_APP + 0x53` är UPPTAGET av systemfältets callback (`kMsgTray`,
// d3d11_tray.cpp) — ett FÖNSTERmeddelande till systemfältets
// mottagarfönster och inte ett trådmeddelande som de två ovan: det har ett
// HWND, når WndProc:en via DispatchMessageW och känns därför inte igen här.

} // namespace

bool init()
{
    // Per-monitor-DPI så GetDpiForWindow ger rätt skala och Windows inte
    // bitmap-skalar oss. Misslyckas anropet (äldre Windows) kör vi vidare
    // med systemskalan.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_uiThread = GetCurrentThreadId();   // PÅ UI-tråden
    // TVINGA FRAM MEDDELANDEKÖN. En tråd får sin
    // Win32-kö först när den rör ett meddelande-API, och
    // `PostThreadMessageW` MISSLYCKAS mot en tråd som ännu inte har en.
    // Utan den här raden kan en `post()` före första pumpen tappa sin
    // väckning. Pumpen är hardenad mot det (`App::hasPosted()`),
    // men väckningen ska fungera på riktigt och inte förlita sig på skyddet.
    MSG dummy;
    PeekMessageW(&dummy, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    return true;
}

void shutdown()
{
    // Systemfältets ikoner ur fältet FÖRST, medan plattformen fortfarande
    // är uppe — en ikon som blir kvar efter processen syns som en spökikon
    // tills pekaren drar över den.
    trayShutdown();
    // Appens timrar efter fältet: meddelandefönstret rivs,
    // och timrarna med det, medan plattformen fortfarande är uppe.
    appTimerShutdown();
    g_uiThread = 0;
}

// UI-trådens id är satt precis mellan init och shutdown — det är svaret.
bool platformInitialized()
{
    return g_uiThread != 0;
}

// TRÅDSÄKER VÄCKNING. `PostThreadMessage` är trådsäker och kräver
// inget fönster — den ENDA operation här som får göras från en främmande
// tråd (event-kön nedan är en olåst `std::deque` som ägs av UI-tråden).
//
// TVÅ SAKER GÖR ATT DET INTE RÄCKER MED ETT WM_NULL:
//   1. Ett TRÅDmeddelande har inget `HWND` och når därför ALDRIG en
//      `WndProc` — översättningen måste ske i pumpen själv.
//   2. `waitEvent` loopar tills något ligger i gfx-kön, så ett meddelande
//      som bara dispatchas skickar loopen rakt tillbaka i `GetMessageW`.
// Därför känns meddelandet igen i BÅDE `waitEvent` och `pollEvent`, FÖRE
// `DispatchMessageW`, och köar ett `EventType::Wakeup`.
void postWakeup()
{
    const DWORD tid = g_uiThread;
    if (tid == 0)
        return;   // före init / efter shutdown — no-op, som fasaden lovar
    // Returen KONTROLLERAS: misslyckas posten (måltråden har ingen kö, eller
    // kön är full) är väckningen förlorad, och den enda ärliga åtgärden är
    // att säga det. Pumpen hänger inte av det — `App::hasPosted()` ser kön
    // före den blockerande väntan — men en tyst förlust hade gjort
    // felsökningen omöjlig.
    if (!PostThreadMessageW(tid, kMsgWakeup, 0, 0))
        log("postWakeup: PostThreadMessageW misslyckades (fel %lu) — posten "
            "körs vid nästa pumpvarv", static_cast<unsigned long>(GetLastError()));
}

void log(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    // WIN32-subsystem: ingen konsol — debug-utgången är det som syns
    // (DebugView/IDE). stderr med för säkerhets skull.
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
    std::fprintf(stderr, "%s\n", buf);
}

std::wstring toWide(const std::string& utf8)
{
    if (utf8.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                      static_cast<int>(utf8.size()),
                                      nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                        static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string toUtf8(const std::wstring& wide)
{
    if (wide.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                      static_cast<int>(wide.size()),
                                      nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                        static_cast<int>(wide.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}

std::string basePath()
{
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return {};
    std::wstring path(buf, n);
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos)
        return {};
    path.resize(slash + 1);   // behåll avslutande separator
    return toUtf8(path);
}

// Modulen som äger adressen, utan att röra dess referensräkning — och
// programfilens modul. GetModuleHandleExW tar laddarlåset, som tråden redan
// håller om frågan ställs i en DLL:s DETACH; låset är rekursivt.
bool isInProgramFile(const void* address)
{
    HMODULE modul = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(address), &modul))
        return false;
    return modul == GetModuleHandleW(nullptr);
}

// PROGRAMFILENS NAMN (icon_util.h) — förvalet för `appId()`:
// exe:ns filnamn utan katalog och utan sista filändelsen (`demo.exe` → `demo`).
std::string programName()
{
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return {};
    std::wstring namn(buf, n);
    const size_t slash = namn.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        namn.erase(0, slash + 1);
    const size_t punkt = namn.find_last_of(L'.');
    if (punkt != std::wstring::npos && punkt > 0)
        namn.resize(punkt);
    return toUtf8(namn);
}

// ---------------------------------------------------------------- klippbord
//
// RADSLUTEN (kontraktet i gfx/platform.h): fasaden talar LF, och
// CF_UNICODETEXT har CR-LF som radslut — ett klassiskt flerradigt EDIT-fält
// (och mycket annat på Windows) visar en ensam LF som ingenting. Ut får
// därför varje LF som inte redan står efter ett CR ett, och in blir varje
// CRLF en LF. Ett ensamt CR rörs inte åt något håll: det är inte Windows
// form, och mottagaren (det flerradiga fältets filter) tolkar det själv.

namespace {

std::string medCrlf(const std::string& lf)
{
    std::string ut;
    ut.reserve(lf.size());
    for (std::size_t i = 0; i < lf.size(); ++i) {
        if (lf[i] == '\n' && (i == 0 || lf[i - 1] != '\r'))
            ut.push_back('\r');
        ut.push_back(lf[i]);
    }
    return ut;
}

std::string medLf(const std::string& crlf)
{
    std::string ut;
    ut.reserve(crlf.size());
    for (std::size_t i = 0; i < crlf.size(); ++i) {
        if (crlf[i] == '\r' && i + 1 < crlf.size() && crlf[i + 1] == '\n')
            continue;   // CR:et i ett CRLF — LF:et kommer i nästa varv
        ut.push_back(crlf[i]);
    }
    return ut;
}

// URKLIPPET ÄR DELAT: en annan process — urklippshistoriken, ett
// fjärrskrivbord, en urklippshanterare — öppnar det en kort stund efter
// varje ändring, och då svarar OpenClipboard nej. Utan ett nytt försök
// försvinner en kopiering TYST när två urklippsändringar följer tätt. Tio försök med fem millisekunder emellan är den vanliga
// läkningen; under tiden står UI-tråden, men bara när urklippet faktiskt är
// upptaget.
bool öppnaUrklipp()
{
    for (int försök = 0; försök < 10; ++försök) {
        if (OpenClipboard(nullptr))
            return true;
        Sleep(5);
    }
    return false;
}

} // namespace

std::string clipboardText()
{
    // CF_UNICODETEXT: OS:et konverterar från andra textformat vid behov.
    if (!öppnaUrklipp())
        return {};
    std::string out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* w = static_cast<const wchar_t*>(GlobalLock(h))) {
            out = medLf(toUtf8(w));
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}

void setClipboardText(const std::string& utf8)
{
    const std::wstring w = toWide(medCrlf(utf8));
    if (!öppnaUrklipp())
        return;
    EmptyClipboard();
    const SIZE_T bytes = (w.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        bool handedOver = false;
        if (void* p = GlobalLock(h)) {
            std::memcpy(p, w.c_str(), bytes);
            GlobalUnlock(h);
            // Lyckat SetClipboardData tar över ägandeskapet av minnet
            handedOver = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
        }
        if (!handedOver)
            GlobalFree(h);
    }
    CloseClipboard();
}

// ------------------------------------------------------- openUrl / openFile

namespace {

// Öppningen openUrl och openFile delar: ShellExecuteW med verbet "open", så
// OS:et väljer standardhanteraren — för en adress eller en filsökväg, båda
// som den är (UTF-16). Returvärdet är en HINSTANCE av hävd — värden > 32
// betyder lyckat. `vad` är anroparens namn i loggen.
bool shellOpen(const std::string& mal, const char* vad)
{
    const std::wstring w = toWide(mal);
    const HINSTANCE    r = ShellExecuteW(nullptr, L"open", w.c_str(), nullptr,
                                         nullptr, SW_SHOWNORMAL);
    const INT_PTR      code = reinterpret_cast<INT_PTR>(r);
    if (code <= 32) {
        log("%s: ShellExecuteW misslyckades (kod %lld)", vad,
            static_cast<long long>(code));
        return false;
    }
    return true;
}

} // namespace

bool openUrl(const std::string& url)
{
    if (url.empty())
        return false;
    return shellOpen(url, "openUrl");
}

bool openFile(const std::string& path)
{
    if (path.empty())
        return false;
    return shellOpen(path, "openFile");
}

SystemTheme systemTheme()
{
    // AppsUseLightTheme: 1 = ljust, 0 = mörkt. Läses färskt per anrop
    // (registryt är cachat av OS:et — mikrosekunder). Saknas värdet
    // (äldre Windows) → Light, som är OS-defaulten.
    DWORD value = 1;
    DWORD size  = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\"
                 L"Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr,
                 &value, &size);
    return value != 0 ? SystemTheme::Light : SystemTheme::Dark;
}

std::uint32_t doubleClickMs()
{
    // Användarens inställning (Musegenskaper → Dubbelklickshastighet),
    // läst färskt per anrop — det är ett billigt systemanrop.
    return static_cast<std::uint32_t>(GetDoubleClickTime());
}

std::string systemLocale()
{
    // GetUserDefaultLocaleName ger redan BCP-47-formen ("sv-SE"), så
    // ingen normalisering behövs — bara UTF-16 → UTF-8. Misslyckas
    // anropet svarar vi TOMT, alltså "OS:et har ingen preferens", och
    // kedjan i widget-lagret faller till engelska.
    wchar_t buf[LOCALE_NAME_MAX_LENGTH] = {};
    const int n = GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH);
    if (n <= 1)                      // n räknar med NUL:en
        return std::string();
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, buf, n - 1, nullptr, 0,
                                          nullptr, nullptr);
    if (bytes <= 0)
        return std::string();
    std::string ut(static_cast<std::size_t>(bytes), '\0');
    // ANDRA anropets returvärde måste kontrolleras: misslyckas det EFTER
    // att strängen allokerats returnerar vi annars `bytes` NUL-tecken i
    // stället för den tomma sträng kontraktet lovar — och en tagg av
    // NUL-byte matchar inget språk men är heller inte "ingen preferens".
    const int skrivna = WideCharToMultiByte(CP_UTF8, 0, buf, n - 1,
                                            ut.data(), bytes, nullptr,
                                            nullptr);
    if (skrivna != bytes)
        return std::string();
    // LOCALE_NAME_INVARIANT är tomt; "C" kan inte komma härifrån, men
    // regeln skrivs likadant i alla tre backends.
    if (ut == "C" || ut == "POSIX")
        return std::string();
    return ut;
}

void setQuitMenuPrefix(const std::string&)
{
    // Windows har ingen appmeny att fylla: fönstrets systemmeny (Alt+Space)
    // är OS:ets egen och översätts av OS:et (gfx/platform.h).
}

// ------------------------------------------------------------ knownPlaces

namespace {

// En känd mapp som UTF-8, eller tom sträng. Strängen SHGetKnownFolderPath
// lämnar ut ska frigöras med CoTaskMemFree OAVSETT om anropet lyckades —
// dokumentationens kontrakt, och CoTaskMemFree(nullptr) är ofarlig.
std::string knownFolder(REFKNOWNFOLDERID id)
{
    PWSTR       p = nullptr;
    std::string ut;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p)
        ut = toUtf8(p);
    CoTaskMemFree(p);
    return ut;
}

bool isDirectory(const std::string& utf8)
{
    std::error_code ec;
    return !utf8.empty() &&
           std::filesystem::is_directory(std::filesystem::path(toWide(utf8)),
                                         ec);
}

} // namespace

std::vector<Place> knownPlaces()
{
    std::vector<Place> ut;
    const std::string  hem = knownFolder(FOLDERID_Profile);
    if (isDirectory(hem))
        ut.push_back(Place{PlaceKind::Home, hem, {}});
    // En användarmapp som är hemmet SJÄLVT är ingen egen plats (XDG:s sätt
    // att stänga av en mapp; på Windows i praktiken aldrig, men regeln är
    // fasadens och skrivs likadant i alla tre backends).
    auto mapp = [&](PlaceKind k, REFKNOWNFOLDERID id) {
        const std::string p = knownFolder(id);
        if (isDirectory(p) && p != hem)
            ut.push_back(Place{k, p, {}});
    };
    mapp(PlaceKind::Desktop, FOLDERID_Desktop);
    mapp(PlaceKind::Documents, FOLDERID_Documents);
    mapp(PlaceKind::Downloads, FOLDERID_Downloads);
    mapp(PlaceKind::Pictures, FOLDERID_Pictures);

    // VOLYMERNA: "C:\␀D:\␀␀". Returvärdet är längden UTAN avslutande NUL,
    // eller den längd som KRÄVS när bufferten är för liten — 26 bokstäver ×
    // 4 tecken ryms med god marginal, så det andra fallet betyder fel.
    wchar_t     enheter[256] = {};
    const DWORD n            = GetLogicalDriveStringsW(256, enheter);
    if (n == 0 || n >= 256)
        return ut;
    // Inget "sätt i en skiva i enhet E:" — en tom kortläsare eller en
    // utmatad skiva ska bara saknas i listan, aldrig öppna en systemruta.
    DWORD gammal = 0;
    SetThreadErrorMode(SEM_FAILCRITICALERRORS, &gammal);
    for (const wchar_t* d = enheter; *d; d += std::wcslen(d) + 1) {
        const UINT typ = GetDriveTypeW(d);
        if (typ == DRIVE_UNKNOWN || typ == DRIVE_NO_ROOT_DIR)
            continue;
        const std::wstring bokstav(d, 2);   // "C:"
        std::wstring       namn = bokstav;
        // En NÄTVERKSENHET frågas inte (kontraktet i gfx/platform.h): en
        // frånkopplad hade hållit kvar anropet i SMB:s tidsgräns.
        if (typ != DRIVE_REMOTE) {
            wchar_t etikett[MAX_PATH + 1] = {};
            if (!GetVolumeInformationW(d, etikett, MAX_PATH + 1, nullptr,
                                       nullptr, nullptr, nullptr, 0))
                continue;   // inte redo
            if (etikett[0] != L'\0')
                namn = std::wstring(etikett) + L" (" + bokstav + L")";
        }
        ut.push_back(Place{PlaceKind::Volume, toUtf8(d), toUtf8(namn)});
    }
    SetThreadErrorMode(gammal, nullptr);
    return ut;
}

// ---------------------------------------------------------------- skärmarna
//
// SKÄRMARNA — varje skärm i placeringsrymden, som på
// Win32 är fysiska px (processen är per-monitor-v2-medveten, init). `scale`
// är skärmens EFFEKTIVA DPI / 96 × användarskalan: ett fönster med den
// logiska bredden B blir B × scale fysiska px brett där, samma uträkning som
// `D3dWindow::setSize` gör i fönstrets egen DPI.

namespace {

BOOL CALLBACK samlaSkärm(HMONITOR mon, HDC, LPRECT, LPARAM data)
{
    auto*       ut = reinterpret_cast<std::vector<ScreenInfo>*>(data);
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(mon, &mi))
        return TRUE;   // en skärm som inte svarar hoppas över
    UINT dx = 0, dy = 0;
    const float dpi =
        SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy)) && dx > 0
            ? static_cast<float>(dx)
            : static_cast<float>(USER_DEFAULT_SCREEN_DPI);
    const RECT& b = mi.rcMonitor;
    const RECT& w = mi.rcWork;
    ScreenInfo  s;
    s.bounds  = Rectf{float(b.left), float(b.top), float(b.right - b.left),
                      float(b.bottom - b.top)};
    s.work    = Rectf{float(w.left), float(w.top), float(w.right - w.left),
                      float(w.bottom - w.top)};
    s.scale   = dpi / static_cast<float>(USER_DEFAULT_SCREEN_DPI) * userScale();
    s.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    ut->push_back(s);
    return TRUE;
}

} // namespace

std::vector<ScreenInfo> screens()
{
    std::vector<ScreenInfo> ut;
    if (!platformInitialized())
        return ut;   // före init: DPI-medvetenheten är inte satt än
    EnumDisplayMonitors(nullptr, nullptr, &samlaSkärm,
                        reinterpret_cast<LPARAM>(&ut));
    return ut;
}

bool isHiddenFile(const std::string& utf8Path)
{
    // Utforskaren döljer allt med HIDDEN-biten; "skyddade systemfiler" bär
    // dessutom SYSTEM, men alltid tillsammans med HIDDEN — en bit räcker.
    const DWORD a = GetFileAttributesW(toWide(utf8Path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_HIDDEN) != 0;
}

// ---------------------------------------------------------------- events

namespace {

// Event-kön — enkeltrådad (WndProc körs på samma tråd som pumpen).
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

// SKICKADE MEDDELANDEN. `GetMessageW` levererar ett SKICKAT
// meddelande — SendMessage från en annan tråd, OS:ets WM_SETTINGCHANGE-
// utsändning vid ljus/mörkt-byte, aktiverings- och fokusbyten — genom att
// köra WndProc:en INUTI anropet, och fortsätter därefter VÄNTA: den återvänder
// bara för ett POSTAT meddelande. Köade WndProc:en ett gfx-event där låg
// det kvar tills något postades, alltså tills användaren rörde musen.
//
// `g_inGetMessage` säger att vi står i just den väntan; `g_puffPosted` att
// en puff redan är postad under den, så att en skur av skickade meddelanden
// inte fyller trådkön med puffar. Båda rörs bara av UI-tråden.
//
// MODALA LOOPAR: startar ett skickat meddelande en modal OS-loop inuti
// GetMessageW (SC_MOVE skickat från en annan tråd) är det den loopen som
// hämtar puffen, och ett trådmeddelande utan HWND försvinner där. För
// flytt-/storleksloopen täcks det av `WM_EXITSIZEMOVE` i d3d11_window.cpp,
// som köar ett Wakeup när loopen släpps (och då, inifrån GetMessageW, en ny
// puff). En ANNAN modal loop har ingen sådan krok — ingen finns.
bool g_inGetMessage = false;
bool g_puffPosted   = false;

} // namespace

void pushEvent(const Event& e)
{
    queue().push_back(e);
    // Puffen postas BARA inifrån GetMessageW-väntan: under DispatchMessageW
    // tittar loopen i kön så fort dispatchen återvänt, och en puff per
    // musrörelse hade fördubblat trafiken utan att göra någon nytta.
    if (g_inGetMessage && !g_puffPosted)
        g_puffPosted =
            PostThreadMessageW(GetCurrentThreadId(), kMsgPuff, 0, 0);
}

bool waitEvent(Event& e)
{
    for (;;) {
        if (popQueued(e))
            return true;
        MSG m;
        // Flaggan sparas och återställs i stället för att nollas: väntan
        // nästlas inte (en modal dialogs pump körs ur ett HANTERAT
        // event, aldrig inifrån GetMessageW), men en återställning kostar
        // ingenting och tål att det ändras.
        const bool wasInside = g_inGetMessage;
        g_inGetMessage       = true;
        g_puffPosted         = false;
        const BOOL r = GetMessageW(&m, nullptr, 0, 0);   // blockerar — ~0 % CPU
        g_inGetMessage       = wasInside;
        if (r == -1)
            return false;
        if (r == 0) {   // WM_QUIT
            e = Event{};
            e.type = EventType::Quit;
            return true;
        }
        // Trådmeddelandet har inget HWND och skulle försvinna i
        // DispatchMessageW — fånga det HÄR och köa gfx-eventet.
        if (m.hwnd == nullptr && m.message == kMsgWakeup) {
            Event w{};
            w.type = EventType::Wakeup;
            pushEvent(w);
            continue;
        }
        // Puffen har gjort sitt när GetMessageW återvänt — eventet den
        // skulle hämta ligger redan i kön och tas i nästa varv.
        if (m.hwnd == nullptr && m.message == kMsgPuff)
            continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);   // WndProc:en köar ev. gfx::Event — nästa varv
    }
}

bool pollEvent(Event& e)
{
    for (;;) {
        if (popQueued(e))
            return true;
        MSG m;
        // PeekMessageW levererar också skickade meddelanden innan den
        // svarar — och svarar NEJ när inget POSTAT fanns. Ett event som en
        // skickad WndProc köade under anropet ligger då i kön och måste
        // hämtas här, annars säger pollEvent "tomt" med ett event i kön.
        if (!PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE))
            return popQueued(e);
        if (m.message == WM_QUIT) {
            e = Event{};
            e.type = EventType::Quit;
            return true;
        }
        if (m.hwnd == nullptr && m.message == kMsgWakeup) {
            Event w{};
            w.type = EventType::Wakeup;
            pushEvent(w);
            continue;
        }
        if (m.hwnd == nullptr && m.message == kMsgPuff)
            continue;   // en kvarbliven puff ur en föregående väntan
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

// ---------------------------------------------------------------- fabrik

std::unique_ptr<Window> createWindow()
{
    return std::make_unique<D3dWindow>();
}

} // namespace gfx
