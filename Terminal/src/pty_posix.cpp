// Pseudoterminalen på macOS och Linux — posix_openpt.
// Kontraktet står i pty.h. Ingen forkpty: allt den gör står
// här, och Linux slipper -lutil.
//
// FORK I EN FLERTRÅDAD PROCESS: efter `fork()` lever bara den anropande
// tråden, och ett lås som en annan tråd höll är låst för alltid i barnet.
// Därför förbereds ALLT före fork — argv, envp, sökvägen ur PATH, den
// absoluta katalogen, signalernas förval — och barnet gör bara
// async-signal-säkra anrop: setsid, open, ioctl, dup2, close, sigprocmask,
// sigaction, chdir, execve, write och _exit. Ingen getenv, ingen malloc,
// ingen PATH-sökning i barnet.

#include <cerrno>
#include <csignal>
#include <cstdlib>   // getenv, posix_openpt, grantpt, unlockpt, ptsname
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#ifdef __APPLE__
#include <crt_externs.h>   // _NSGetEnviron — `environ` når inte en dylib
#endif

#include "glaze_terminal/command_line.h"
#include "pty_impl.h"

#ifndef __APPLE__
extern char** environ;
#endif

namespace glaze::term {
namespace {

char** processEnvironment()
{
#ifdef __APPLE__
    return *_NSGetEnviron();
#else
    return environ;
#endif
}

std::string errnoText(const char* vad, int e)
{
    return std::string(vad) + ": " + std::generic_category().message(e) + " (" +
           std::to_string(e) + ")";
}

void cloexec(int fd) { fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC); }
void nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); }

// Ett rör vars ändar inte ärvs av en exec. Linux gör det atomiskt; på macOS
// finns ingen pipe2, och en fork i en annan tråd mellan pipe och fcntl kan
// ärva änden — skriven gräns, den stängs vid det barnets exec ändå inte.
bool cloexecPipe(int fds[2])
{
#ifdef __linux__
    return pipe2(fds, O_CLOEXEC) == 0;
#else
    if (pipe(fds) != 0)
        return false;
    cloexec(fds[0]);
    cloexec(fds[1]);
    return true;
#endif
}

// `ptsname` delar en statisk buffert — låset gör den trådsäker här
// (ptsname_r finns inte på alla macOS-versioner).
std::string slaveName(int master)
{
    static std::mutex lås;
    std::lock_guard<std::mutex> l(lås);
    const char* n = ptsname(master);
    return n ? std::string(n) : std::string();
}

// Programmet i PATH (före fork — barnet får aldrig söka). Ett namn med `/`
// används som det står.
std::string resolveProgram(const std::string& prog)
{
    if (prog.find('/') != std::string::npos)
        return prog;
    const char* path = std::getenv("PATH");
    std::string p    = path && *path ? path : "/usr/bin:/bin";
    std::size_t start = 0;
    while (start <= p.size()) {
        std::size_t slut = p.find(':', start);
        if (slut == std::string::npos)
            slut = p.size();
        const std::string dir  = slut > start ? p.substr(start, slut - start) : ".";
        const std::string full = dir + "/" + prog;
        struct stat st{};
        if (stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
            access(full.c_str(), X_OK) == 0)
            return full;
        start = slut + 1;
    }
    return {};
}

winsize winSize(int columns, int rows)
{
    winsize ws{};
    ws.ws_col = static_cast<unsigned short>(columns < 1 ? 1 : columns > 65535 ? 65535 : columns);
    ws.ws_row = static_cast<unsigned short>(rows < 1 ? 1 : rows > 65535 ? 65535 : rows);
    return ws;
}

// Barnets fel till föräldern: steget och errno, och sedan _exit(127).
[[noreturn]] void childFail(int fd, int steg)
{
    const int msg[2] = {steg, errno};
    ssize_t   n;
    do {
        n = ::write(fd, msg, sizeof msg);
    } while (n < 0 && errno == EINTR);
    _exit(127);
}

} // namespace

struct Pty::Impl {
    int   master = -1;
    int   wakeR = -1, wakeW = -1;   // väckaren — skrivs en gång och töms aldrig
    pid_t pid    = -1;
    bool  reaped = false;           // waitpid har svarat (väntartråden)
    std::mutex m;                   // `reaped` mot destruktorn
};

Pty::Pty() : impl_(std::make_unique<Impl>()) {}

Pty::~Pty()
{
    Impl& p = *impl_;
    bool  skörda;
    {
        std::lock_guard<std::mutex> l(p.m);
        skörda = p.pid > 0 && !p.reaped;
    }
    if (skörda) {
        // Ingen väntartråd skördade barnet (Pty ensam): döda och skörda här,
        // så att det inte blir en zombie.
        kill(-p.pid, SIGKILL);
        while (waitpid(p.pid, nullptr, 0) < 0 && errno == EINTR) {
        }
    } else if (p.pid > 0) {
        kill(-p.pid, SIGKILL);   // gruppens kvarvarande medlemmar
    }
    for (int fd : {p.master, p.wakeR, p.wakeW})
        if (fd >= 0)
            close(fd);
}

bool Pty::start(const StartInfo& info, std::string& error)
{
    Impl& p = *impl_;
    if (p.pid > 0) {
        error = "pty:n är redan startad";
        return false;
    }

    // ---- allt barnet behöver, förberett här
    std::string program = info.program;
    if (program.empty()) {
        const char* sh = std::getenv("SHELL");
        program        = sh && *sh ? sh : "/bin/sh";
    }
    const std::string path = resolveProgram(program);
    if (path.empty()) {
        error = "programmet '" + program + "' hittades inte i PATH";
        return false;
    }
    std::vector<std::string> args = splitCommandLine(info.arguments);
    args.insert(args.begin(), program);
    std::vector<char*> argv;
    for (std::string& a : args)
        argv.push_back(a.data());
    argv.push_back(nullptr);

    detail::EnvList bas;
    for (char** e = processEnvironment(); e && *e; ++e) {
        const std::string s(*e);
        const std::size_t eq = s.find('=');
        if (eq != std::string::npos && eq > 0)
            bas.emplace_back(s.substr(0, eq), s.substr(eq + 1));
    }
    const detail::EnvList env = detail::mergeEnvironment(
        std::move(bas), info.environment,
        [](const std::string& a, const std::string& b) { return a == b; });
    std::vector<std::string> envStr;
    for (const auto& [namn, värde] : env)
        envStr.push_back(namn + "=" + värde);
    std::vector<char*> envp;
    for (std::string& e : envStr)
        envp.push_back(e.data());
    envp.push_back(nullptr);

    std::string cwd;
    if (!info.workingDirectory.empty()) {
        std::error_code ec;
        cwd = std::filesystem::absolute(info.workingDirectory, ec).string();
        if (ec)
            cwd = info.workingDirectory;
    }

    // ---- pty:n
    p.master = posix_openpt(O_RDWR | O_NOCTTY);
    if (p.master < 0) {
        error = errnoText("posix_openpt", errno);
        return false;
    }
    cloexec(p.master);
    std::string slav;
    if (grantpt(p.master) != 0 || unlockpt(p.master) != 0 ||
        (slav = slaveName(p.master)).empty()) {
        error = errnoText("grantpt/unlockpt/ptsname", errno);
        close(p.master);
        p.master = -1;
        return false;
    }
    const winsize ws = winSize(info.columns, info.rows);
    ioctl(p.master, TIOCSWINSZ, &ws);

    int väck[2];
    int fel[2];
    if (!cloexecPipe(väck)) {
        error = errnoText("pipe", errno);
        close(p.master);
        p.master = -1;
        return false;
    }
    if (!cloexecPipe(fel)) {
        error = errnoText("pipe", errno);
        for (int fd : {p.master, väck[0], väck[1]})
            close(fd);
        p.master = -1;
        return false;
    }
    nonblock(väck[0]);
    nonblock(väck[1]);
    p.wakeR = väck[0];
    p.wakeW = väck[1];

    sigset_t tom;
    sigemptyset(&tom);
    struct sigaction förval{};
    förval.sa_handler = SIG_DFL;
    sigemptyset(&förval.sa_mask);
    static constexpr int kSignaler[] = {SIGHUP,  SIGINT,  SIGQUIT, SIGPIPE, SIGCHLD,
                                        SIGTERM, SIGTSTP, SIGTTIN, SIGTTOU};
    const char* const slavC = slav.c_str();
    const char* const cwdC  = cwd.empty() ? nullptr : cwd.c_str();
    const char* const pathC = path.c_str();

    const pid_t pid = fork();
    if (pid < 0) {
        error = errnoText("fork", errno);
        for (int fd : {fel[0], fel[1], p.master, p.wakeR, p.wakeW})
            close(fd);
        p.master = p.wakeR = p.wakeW = -1;
        return false;
    }
    if (pid == 0) {
        // ---- BARNET: bara async-signal-säkra anrop (filhuvudet). Ordningen
        // är setsid → TIOCSCTTY → dup2 → exec: en ny session utan
        // kontrollterminal, slaven som dess kontrollterminal (krävs på
        // macOS, och harmlöst på Linux, där öppningen redan gjort det).
        if (setsid() < 0)
            childFail(fel[1], 1);
        const int s = open(slavC, O_RDWR);
        if (s < 0)
            childFail(fel[1], 2);
        // Storleken EN GÅNG TILL på slaven: på macOS fastnar den inte på
        // mastern förrän slaven är öppen (läses som 0 0),
        // och föräldern öppnar aldrig slaven själv.
        ioctl(s, TIOCSWINSZ, &ws);
#ifdef TIOCSCTTY
        ioctl(s, TIOCSCTTY, 0);
#endif
        if (dup2(s, 0) < 0 || dup2(s, 1) < 0 || dup2(s, 2) < 0)
            childFail(fel[1], 2);
        if (s > 2)
            close(s);
        sigprocmask(SIG_SETMASK, &tom, nullptr);
        for (const int sig : kSignaler)
            sigaction(sig, &förval, nullptr);
        if (cwdC && chdir(cwdC) != 0)
            childFail(fel[1], 3);
        execve(pathC, argv.data(), envp.data());
        childFail(fel[1], 4);
    }

    // ---- FÖRÄLDERN: felröret ger EOF när exec lyckats (CLOEXEC stängde
    // barnets ände), annars steget och errno. Först då vet vi att gruppen finns.
    close(fel[1]);
    int     msg[2] = {0, 0};
    ssize_t n;
    do {
        n = ::read(fel[0], msg, sizeof msg);
    } while (n < 0 && errno == EINTR);
    close(fel[0]);
    if (n == static_cast<ssize_t>(sizeof msg)) {
        while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
        }
        const int steg = msg[0];
        const char* vad = steg == 1 ? "setsid"
                          : steg == 2 ? "terminalen"
                          : steg == 3 ? "arbetskatalogen"
                                      : "exec";
        const std::string& mål = steg == 2 ? slav : steg == 3 ? cwd : path;
        error = errnoText(steg == 1 ? vad : (std::string(vad) + " " + mål).c_str(), msg[1]);
        for (int fd : {p.master, p.wakeR, p.wakeW})
            close(fd);
        p.master = p.wakeR = p.wakeW = -1;
        return false;
    }
    nonblock(p.master);
    p.pid = pid;
    return true;
}

std::ptrdiff_t Pty::read(char* buf, std::size_t n)
{
    Impl& p = *impl_;
    for (;;) {
        pollfd fds[2] = {{p.master, POLLIN, 0}, {p.wakeR, POLLIN, 0}};
        if (poll(fds, 2, -1) < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (fds[1].revents)
            return 0;
        if (!fds[0].revents)
            continue;
        const ssize_t r = ::read(p.master, buf, n);
        if (r > 0)
            return r;
        if (r == 0 || errno == EIO)
            return 0;   // EIO: barnets sida stängd — slut
        if (errno != EAGAIN && errno != EINTR)
            return -1;
    }
}

bool Pty::write(const char* data, std::size_t n)
{
    Impl& p = *impl_;
    while (n > 0) {
        const ssize_t r = ::write(p.master, data, n);
        if (r > 0) {
            data += r;
            n -= static_cast<std::size_t>(r);
            continue;
        }
        if (r < 0 && errno != EAGAIN && errno != EINTR)
            return false;
        pollfd fds[2] = {{p.master, POLLOUT, 0}, {p.wakeR, POLLIN, 0}};
        if (poll(fds, 2, -1) < 0 && errno != EINTR)
            return false;
        if (fds[1].revents)
            return false;
    }
    return true;
}

int Pty::wait(std::string& notice)
{
    Impl& p = *impl_;
    if (p.pid <= 0)
        return -1;
    int   st = 0;
    pid_t r;
    do {
        r = waitpid(p.pid, &st, 0);
    } while (r < 0 && errno == EINTR);
    {
        std::lock_guard<std::mutex> l(p.m);
        p.reaped = true;
    }
    if (r < 0) {
        if (errno == ECHILD)
            notice = "slutkoden är okänd: appen har satt SIGCHLD till SIG_IGN, "
                     "så barnet skördades av systemet";
        return -1;
    }
    if (WIFEXITED(st))
        return WEXITSTATUS(st);
    if (WIFSIGNALED(st))
        return 128 + WTERMSIG(st);
    return -1;
}

void Pty::resize(int columns, int rows)
{
    Impl& p = *impl_;
    if (p.master < 0)
        return;
    const winsize ws = winSize(columns, rows);
    ioctl(p.master, TIOCSWINSZ, &ws);   // kärnan skickar SIGWINCH själv
}

// Processgruppen (barnet är sessionsledare, så gruppen är dess pid) — OCKSÅ
// efter skörden: huvudprocessens slut är inte
// gruppens, och ett bakgrundsjobb lever kvar i den. Ett grupp-id återanvänds
// inte så länge gruppen har en medlem, så `-pid` träffar bara vår grupp — och
// en tom grupp svarar ESRCH.
void Pty::terminate(bool force)
{
    Impl& p = *impl_;
    if (p.pid > 0)
        kill(-p.pid, force ? SIGKILL : SIGHUP);
}

bool Pty::alive() const
{
    const Impl& p = *impl_;
    return p.pid > 0 && (kill(-p.pid, 0) == 0 || errno == EPERM);
}

void Pty::closeConsole() {}

void Pty::wakeAll()
{
    Impl& p = *impl_;
    if (p.wakeW >= 0) {
        const char c = 1;
        while (::write(p.wakeW, &c, 1) < 0 && errno == EINTR) {
        }
    }
}

} // namespace glaze::term
