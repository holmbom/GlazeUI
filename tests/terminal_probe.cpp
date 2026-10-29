// terminal_probe — HJÄLPPROGRAMMET som terminal_gate och terminal_ui_gate kör
// i en pseudoterminal. Aldrig en grind själv, aldrig för hand.
//
//   (inga argument)  RÅTT läge (inget eko, ingen radbuffring, Ctrl+C som en
//                    byte): skriver "SIZE <kolumner> <rader>" vid start och
//                    vid varje storleksändring, och varje indatabyte som
//                    "<XX>" (hex, versaler). Byten 'Q' avslutar med kod 7.
//   --flood <MiB>    skriver så många MiB rader och slutar med kod 7
//   --env <NAMN>     skriver "ENV <NAMN>=<värde>" eller "ENV <NAMN> saknas"
//                    och slutar med kod 7
//
// Storleken läses ur konsolen (Windows: GetConsoleScreenBufferInfo — i en
// pseudokonsol är det ConPTY:s storlek) respektive ur terminalen (TIOCGWINSZ).
// Utskriften avslutas med CRLF: i rått läge gör terminalen ingen CR själv.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

std::mutex g_ut;

void say(const std::string& s)
{
    std::lock_guard<std::mutex> l(g_ut);
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

bool size(int& cols, int& rows)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return false;
    cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
#else
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0)
        return false;
    cols = ws.ws_col;
    rows = ws.ws_row;
#endif
    return true;
}

void rawMode()
{
#ifdef _WIN32
    // ENABLE_VIRTUAL_TERMINAL_INPUT utan PROCESSED/LINE/ECHO: bytes som de
    // kommer, Ctrl+C inräknat.
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_VIRTUAL_TERMINAL_INPUT);
#else
    termios t{};
    if (tcgetattr(STDIN_FILENO, &t) == 0) {
        cfmakeraw(&t);
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
#endif
}

int readByte()
{
    unsigned char c = 0;
#ifdef _WIN32
    DWORD n = 0;
    if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), &c, 1, &n, nullptr) || n != 1)
        return -1;
#else
    if (::read(STDIN_FILENO, &c, 1) != 1)
        return -1;
#endif
    return c;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc >= 3 && std::strcmp(argv[1], "--flood") == 0) {
        const long long mib  = std::atoll(argv[2]);
        const std::string rad =
            std::string(78, 'y') + "\r\n";   // 80 byte per rad
        const long long antal = mib * 1024 * 1024 / static_cast<long long>(rad.size());
        for (long long i = 0; i < antal; ++i)
            std::fwrite(rad.data(), 1, rad.size(), stdout);
        std::fflush(stdout);
        return 7;
    }
    if (argc >= 3 && std::strcmp(argv[1], "--env") == 0) {
        const char* v = std::getenv(argv[2]);
        say(std::string("ENV ") + argv[2] + (v ? std::string("=") + v : " saknas") + "\r\n");
        return 7;
    }

    rawMode();
    std::atomic<bool> slut{false};
    std::thread läsare([&] {
        for (;;) {
            const int c = readByte();
            if (c < 0)
                break;
            char hex[8];
            std::snprintf(hex, sizeof hex, "<%02X>", c);
            say(hex);
            if (c == 'Q') {
                say("\r\nSLUT\r\n");
                break;
            }
        }
        slut = true;   // 'Q', eller indatan tog slut
    });
    int cols = -1, rows = -1;
    while (!slut) {
        int c = 0, r = 0;
        if (size(c, r) && (c != cols || r != rows)) {
            cols = c;
            rows = r;
            say("SIZE " + std::to_string(c) + " " + std::to_string(r) + "\r\n");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    läsare.join();
    return 7;
}
