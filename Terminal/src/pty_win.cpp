// Pseudoterminalen på Windows — ConPTY. Kontraktet står i
// pty.h; Microsofts "Creating a Pseudoconsole session" är förlagan, med
// skälen till varje avsteg i kommentarerna.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cwchar>
#include <mutex>
#include <string>
#include <vector>

#include "pty_impl.h"

namespace glaze::term {
namespace {

// ConPTY:s tre funktioner slås upp i körtid: MinGW:s consoleapi.h deklarerar
// dem bara med NTDDI_VERSION >= NTDDI_WIN10_RS5, och repot sätter ingen
// _WIN32_WINNT — och en programfil som importerar dem startar inte alls på en
// Windows före 1809. Uppslaget ger i stället ett läsbart `start()`-fel.
using Hpcon    = void*;
using CreatePc = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, Hpcon*);
using ResizePc = HRESULT(WINAPI*)(Hpcon, COORD);
using ClosePc  = void(WINAPI*)(Hpcon);

// PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE — samma NTDDI-vakt i winbase.h, så
// värdet står här: ProcThreadAttributeValue(22, FALSE, TRUE, FALSE).
constexpr DWORD_PTR kAttrPseudoConsole = 0x00020016;

struct ConPty {
    CreatePc create = nullptr;
    ResizePc resize = nullptr;
    ClosePc  close  = nullptr;
};

// kernel32 avlastas aldrig — pekarna gäller processens livstid.
const ConPty& conPty()
{
    static const ConPty api = [] {
        ConPty a;
        if (HMODULE k = GetModuleHandleW(L"kernel32.dll")) {
            a.create = reinterpret_cast<CreatePc>(GetProcAddress(k, "CreatePseudoConsole"));
            a.resize = reinterpret_cast<ResizePc>(GetProcAddress(k, "ResizePseudoConsole"));
            a.close  = reinterpret_cast<ClosePc>(GetProcAddress(k, "ClosePseudoConsole"));
        }
        return a;
    }();
    return api;
}

std::wstring wide(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                      nullptr, 0);
    std::wstring ut(static_cast<std::size_t>(std::max(n, 0)), L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), ut.data(), n);
    return ut;
}

std::string utf8(const wchar_t* w, std::size_t len)
{
    if (len == 0)
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), nullptr, 0,
                                      nullptr, nullptr);
    std::string ut(static_cast<std::size_t>(std::max(n, 0)), '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w, static_cast<int>(len), ut.data(), n, nullptr,
                            nullptr);
    return ut;
}

// Systemets feltext för `kod` (på OS:ets språk) med felnumret.
std::string systemText(const char* vad, DWORD kod)
{
    wchar_t*    buf = nullptr;
    const DWORD n   = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                         FORMAT_MESSAGE_FROM_SYSTEM |
                                         FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, kod, 0, reinterpret_cast<LPWSTR>(&buf), 0,
                                     nullptr);
    std::string t = (n && buf) ? utf8(buf, n) : std::string("okänt fel");
    if (buf)
        LocalFree(buf);
    while (!t.empty() && (t.back() == '\n' || t.back() == '\r' || t.back() == ' '))
        t.pop_back();
    return std::string(vad) + ": " + t + " (" + std::to_string(kod) + ")";
}

COORD coord(int columns, int rows)
{
    return COORD{static_cast<SHORT>(std::clamp(columns, 1, 32767)),
                 static_cast<SHORT>(std::clamp(rows, 1, 32767))};
}

// Två namn i miljön — skiftlägesokänsligt, ordinalt, som CreateProcess jämför.
int compareNames(const std::string& a, const std::string& b)
{
    const std::wstring wa = wide(a), wb = wide(b);
    return CompareStringOrdinal(wa.c_str(), static_cast<int>(wa.size()), wb.c_str(),
                                static_cast<int>(wb.size()), TRUE) -
           CSTR_EQUAL;
}

// Processens miljö med appens ändringar, SORTERAD skiftlägesokänsligt — som
// CREATE_UNICODE_ENVIRONMENT kräver — och som ett block "namn=värde\0…\0".
// De dolda `=C:`-posterna (kataloger per enhet) står kvar först.
std::wstring environmentBlock(const std::vector<EnvChange>& changes)
{
    detail::EnvList bas;
    if (wchar_t* blk = GetEnvironmentStringsW()) {
        for (const wchar_t* p = blk; *p; p += std::wcslen(p) + 1) {
            const std::wstring e(p);
            const std::size_t  eq = e.find(L'=', 1);
            if (eq != std::wstring::npos)
                bas.emplace_back(utf8(e.data(), eq), utf8(e.data() + eq + 1, e.size() - eq - 1));
        }
        FreeEnvironmentStringsW(blk);
    }
    detail::EnvList env = detail::mergeEnvironment(
        std::move(bas), changes,
        [](const std::string& a, const std::string& b) { return compareNames(a, b) == 0; });
    std::stable_sort(env.begin(), env.end(), [](const auto& a, const auto& b) {
        return compareNames(a.first, b.first) < 0;
    });
    std::wstring block;
    for (const auto& [namn, värde] : env) {
        block += wide(namn);
        block += L'=';
        block += wide(värde);
        block += L'\0';
    }
    block += L'\0';
    return block;
}

// Programnamnet (argv[0]) har inga bakstrecksregler — det citeras bara när
// det bär blanksteg, och ett namn kan inte innehålla `"`.
std::wstring quoteProgram(const std::wstring& p)
{
    if (!p.empty() && p.find_first_of(L" \t") == std::wstring::npos)
        return p;
    return L"\"" + p + L"\"";
}

} // namespace

struct Pty::Impl {
    Hpcon  hpc     = nullptr;
    HANDLE inWrite = nullptr;   // vår sida av programmets indata
    HANDLE outRead = nullptr;   // vår sida av programmets utdata
    HANDLE process = nullptr;
    HANDLE job     = nullptr;   // KILL_ON_JOB_CLOSE — hela processkedjan
    HANDLE stop    = nullptr;   // väcker väntaren

    std::mutex        m;   // hpc, stängningen och trådarnas handtag
    // STÄNGNINGEN i två steg: PÅBÖRJAD är inte
    // AVSLUTAD — `ClosePseudoConsole` väntar (före 24H2) tills läsaren tömt
    // utdatan, och läsaren får inte väckas förrän den returnerat.
    enum class Console : std::uint8_t { Open, Closing, Closed };
    Console                 console = Console::Open;
    std::condition_variable closed;
    HANDLE            reader = nullptr;   // för CancelSynchronousIo
    HANDLE            writer = nullptr;
    std::atomic<bool> woken{false};

    // Den anropande trådens handtag, en gång (GetCurrentThread är en
    // pseudo-handtag som bara gäller i tråden själv).
    void remember(HANDLE& h)
    {
        std::lock_guard<std::mutex> l(m);
        if (!h)
            DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                            &h, 0, FALSE, DUPLICATE_SAME_ACCESS);
    }
};

Pty::Pty() : impl_(std::make_unique<Impl>()) {}

Pty::~Pty()
{
    Impl& p = *impl_;
    terminate(true);
    // Utan läsartråd (Pty ensam) tömmer ingen utdatan: vår läsände stängs
    // FÖRST, så att ConPTY:s skrivningar misslyckas i stället för att
    // stängningen väntar på en läsare som aldrig kommer.
    if (p.outRead)
        CloseHandle(p.outRead);
    p.outRead = nullptr;
    closeConsole();
    for (HANDLE h : {p.inWrite, p.process, p.job, p.stop, p.reader, p.writer})
        if (h)
            CloseHandle(h);
}

bool Pty::start(const StartInfo& info, std::string& error)
{
    Impl&         p   = *impl_;
    const ConPty& api = conPty();
    if (!api.create || !api.resize || !api.close) {
        error = "ConPTY (CreatePseudoConsole) saknas — terminalen kräver Windows 10 "
                "1809 eller senare";
        return false;
    }
    if (p.process) {
        error = "pty:n är redan startad";
        return false;
    }

    // Programmet och kommandoraden: programmet citerat, sedan ett blanksteg
    // och argumenten ORDAGRANT (.NET:s form).
    std::wstring program = wide(info.program);
    if (program.empty()) {
        wchar_t buf[MAX_PATH];
        const DWORD n = GetEnvironmentVariableW(L"ComSpec", buf, MAX_PATH);
        program       = (n > 0 && n < MAX_PATH) ? std::wstring(buf, n) : L"cmd.exe";
    }
    std::wstring cmd = quoteProgram(program);
    if (!info.arguments.empty())
        cmd += L" " + wide(info.arguments);
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');
    std::wstring       env = environmentBlock(info.environment);
    const std::wstring cwd = wide(info.workingDirectory);

    HANDLE inRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &p.inWrite, nullptr, 0) ||
        !CreatePipe(&p.outRead, &outWrite, nullptr, 0)) {
        error = systemText("CreatePipe", GetLastError());
        for (HANDLE h : {inRead, p.inWrite, p.outRead, outWrite})
            if (h)
                CloseHandle(h);
        p.inWrite = p.outRead = nullptr;
        return false;
    }

    // PSEUDOCONSOLE_INHERIT_CURSOR används INTE: med den frågar ConPTY efter
    // markören vid starten och väntar på svaret, och en värd som inte hunnit
    // svara kan låsa stängningen (Microsoft Learn). Programmet börjar på en
    // tom skärm, som i Windows Terminal.
    const HRESULT hr =
        api.create(coord(info.columns, info.rows), inRead, outWrite, 0, &p.hpc);
    if (FAILED(hr)) {
        error = systemText("CreatePseudoConsole", static_cast<DWORD>(hr));
        for (HANDLE h : {inRead, p.inWrite, p.outRead, outWrite})
            CloseHandle(h);
        p.inWrite = p.outRead = nullptr;
        p.hpc     = nullptr;
        return false;
    }

    SIZE_T storlek = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &storlek);
    std::vector<char> attrBuf(storlek);
    auto* attr = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    bool  ok   = InitializeProcThreadAttributeList(attr, 1, 0, &storlek) &&
              UpdateProcThreadAttribute(attr, 0, kAttrPseudoConsole, p.hpc, sizeof(p.hpc),
                                        nullptr, nullptr);

    // STARTF_USESTDHANDLES med tomma handtag: annars ärver ett konsolprogram
    // förälderns omdirigerade standardhandtag (glaze-render är ett) i
    // stället för pseudokonsolens.
    STARTUPINFOEXW si{};
    si.StartupInfo.cb      = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.lpAttributeList     = attr;
    PROCESS_INFORMATION pi{};
    DWORD               fel = 0;
    if (ok) {
        // SUSPENDERAD tills processen sitter i jobbet — annars hinner ett barn
        // den startar födas utanför.
        ok = CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
                            EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT |
                                CREATE_SUSPENDED,
                            env.data(), cwd.empty() ? nullptr : cwd.c_str(),
                            &si.StartupInfo, &pi) != 0;
        if (!ok)
            fel = GetLastError();
        DeleteProcThreadAttributeList(attr);
    } else {
        fel = GetLastError();
    }
    // Pty-sidans handtag hör nu till pseudokonsolen.
    CloseHandle(inRead);
    CloseHandle(outWrite);
    if (!ok) {
        error = systemText("CreateProcess", fel);
        api.close(p.hpc);
        p.hpc = nullptr;
        CloseHandle(p.inWrite);
        CloseHandle(p.outRead);
        p.inWrite = p.outRead = nullptr;
        return false;
    }

    // JOBBET: kill() och rivningen tar hela processkedjan. Går
    // jobbet inte att skapa, ställa in eller tilldela startar terminalen INTE:
    // en process vars barn inte kan dödas hade kunnat
    // överleva rivningen. Den suspenderade processen har inte kört en rad.
    p.job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    const char* jobbsteg = !p.job ? "CreateJobObject"
                           : !SetInformationJobObject(p.job, JobObjectExtendedLimitInformation,
                                                      &li, sizeof li)
                               ? "SetInformationJobObject"
                           : !AssignProcessToJobObject(p.job, pi.hProcess) ? "AssignProcessToJobObject"
                                                                           : nullptr;
    if (jobbsteg) {
        error = systemText(jobbsteg, GetLastError());
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (p.job)
            CloseHandle(p.job);
        p.job = nullptr;
        // Vår läsände först, så att stängningen inte väntar på en läsare.
        CloseHandle(p.outRead);
        p.outRead = nullptr;
        api.close(p.hpc);
        p.hpc = nullptr;
        CloseHandle(p.inWrite);
        p.inWrite = nullptr;
        return false;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    p.process = pi.hProcess;
    p.stop    = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return true;
}

std::ptrdiff_t Pty::read(char* buf, std::size_t n)
{
    Impl& p = *impl_;
    p.remember(p.reader);
    for (;;) {
        if (p.woken || !p.outRead)
            return 0;
        DWORD fick = 0;
        if (!ReadFile(p.outRead, buf, static_cast<DWORD>(std::min<std::size_t>(n, 1u << 30)),
                      &fick, nullptr)) {
            const DWORD e = GetLastError();
            return (e == ERROR_BROKEN_PIPE || e == ERROR_OPERATION_ABORTED) ? 0 : -1;
        }
        if (fick > 0)
            return static_cast<std::ptrdiff_t>(fick);
    }
}

bool Pty::write(const char* data, std::size_t n)
{
    Impl& p = *impl_;
    p.remember(p.writer);
    while (n > 0) {
        if (p.woken || !p.inWrite)
            return false;
        DWORD skrev = 0;
        if (!WriteFile(p.inWrite, data, static_cast<DWORD>(std::min<std::size_t>(n, 1u << 20)),
                       &skrev, nullptr))
            return false;
        data += skrev;
        n -= skrev;
    }
    return true;
}

int Pty::wait(std::string&)
{
    Impl& p = *impl_;
    if (!p.process)
        return -1;
    HANDLE hs[2] = {p.process, p.stop};
    if (WaitForMultipleObjects(2, hs, FALSE, INFINITE) != WAIT_OBJECT_0)
        return -1;
    DWORD kod = 0;
    if (!GetExitCodeProcess(p.process, &kod))
        return -1;
    return static_cast<int>(kod);
}

void Pty::resize(int columns, int rows)
{
    Impl&                       p = *impl_;
    std::lock_guard<std::mutex> l(p.m);
    if (p.hpc && p.console == Impl::Console::Open)
        conPty().resize(p.hpc, coord(columns, rows));
}

void Pty::terminate(bool)
{
    Impl& p = *impl_;
    if (p.job)
        TerminateJobObject(p.job, 1);
    else if (p.process)
        TerminateProcess(p.process, 1);
}

// Påbörjad under låset och utförd UTANFÖR det: stängningen väntar (före
// 24H2) tills läsaren tömt utdatan, och `resize` ska inte vänta med. Har en
// annan tråd redan påbörjat den väntar anroparen tills den AVSLUTATS — så att
// den som går vidare till `wakeAll` aldrig stoppar läsaren mitt i den.
void Pty::closeConsole()
{
    Impl&                        p = *impl_;
    std::unique_lock<std::mutex> l(p.m);
    if (!p.hpc || p.console == Impl::Console::Closed)
        return;
    if (p.console == Impl::Console::Closing) {
        p.closed.wait(l, [&] { return p.console == Impl::Console::Closed; });
        return;
    }
    p.console     = Impl::Console::Closing;
    const Hpcon h = p.hpc;
    l.unlock();
    conPty().close(h);
    l.lock();
    p.console = Impl::Console::Closed;
    p.closed.notify_all();
}

// Läsaren stoppas aldrig medan pseudokonsolen stängs: en påbörjad stängning
// väntas ut först (den behöver läsaren).
void Pty::wakeAll()
{
    Impl&                        p = *impl_;
    std::unique_lock<std::mutex> l(p.m);
    p.closed.wait(l, [&] { return p.console != Impl::Console::Closing; });
    p.woken = true;
    if (p.stop)
        SetEvent(p.stop);
    for (HANDLE t : {p.reader, p.writer})
        if (t)
            CancelSynchronousIo(t);
}

bool Pty::alive() const
{
    const Impl& p = *impl_;
    if (p.job) {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
        if (QueryInformationJobObject(p.job, JobObjectBasicAccountingInformation, &info,
                                      sizeof info, nullptr))
            return info.ActiveProcesses > 0;
    }
    return p.process && WaitForSingleObject(p.process, 0) == WAIT_TIMEOUT;
}

} // namespace glaze::term
