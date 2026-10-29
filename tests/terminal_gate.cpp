// terminal_gate — SIDOPAKETET TERMINALS KÄRNA, headless.
//
// Kör:  ./build-test/tests/terminal_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = sessionsfallen kunde
//       inte köras (ingen process gick att starta — ConPTY saknas) och resten
//       var grönt
//
// Länkar BARA glaze_terminal — att den bygger är beviset för att kärnan står
// utan glaze.
//
//   P  tolken och skärmen mot ett HANDSKRIVNA förväntade värden: CUP/ED/EL, DECSTBM med
//      IND/RI i området, IL/DL/ICH/DCH/ECH, den fördröjda radbrytningen, 1049,
//      SGR 38;5 / 38;2 / kolonformen, UTF-8 över en buffertgräns och ogiltigt
//      → U+FFFD, ett brett tecken i sista kolumnen, kombinerande tecken, DSR/DA
//      och DECRQM, OSC 0/2 med BEL och ST, OSC 11-frågan, DCS som sväljs,
//      DEC-linjegrafiken och kittyprotokollets stack
//   W  teckenbredden (CJK 2, emoji 2, U+0301 0, ASCII 1) och VS16 som breddar
//   C  kommandoraden mot Microsofts dokumenterade exempel, och citeringen
//      tillbaka
//   H  historikringen: taket, de äldsta försvinner, `clearHistory`
//   S  sessionen med en RIKTIG process (terminal_probe): S1 start, utskrift
//      och slutkod 7 · S2 storleken når programmet · S3 0x03 som en byte ·
//      S4 taket på inkorgen och den delade hämtningen (`kTakeBudget`; på
//      Windows bara när ConPTY släpper igenom mer än en hämtning — annars ett
//      överhoppat block, för conhost skickar skärmens skillnader) · S5
//      `kill()` mitt i en utskrift inom 2 s · S6 TERM sätts, appens värde vinner
//
// SABOTAGEN som ska fälla den: `closing` borttagen ur rivningen (S5 — tiden),
// taket borttaget ur läsaren (S4), `kTakeBudget` borttagen ur `take` (S4),
// TERM-förvalet borttaget eller före appens ändring (S6), den fördröjda
// radbrytningen borttagen (P), en överlång UTF-8-form som godtas (P).
//
// VAD DEN INTE MÄTER: att vim, htop, less och claude ser rätt ut (handtestet
// och doc/os-raderna); ConPTY:s egen återgivning (proben läses genom VÅR
// skärm, och conhost får skriva om den på sitt sätt); macOS och Linux, som är
// blinda; ConPTY före 24H2 utöver S5:s tidsgräns; prestandan.

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <glaze_terminal/command_line.h>
#include <glaze_terminal/screen.h>
#include <glaze_terminal/session.h>
#include <glaze_terminal/text_width.h>

namespace {

namespace t = glaze::term;
using Clock = std::chrono::steady_clock;

int  g_fails = 0, g_cases = 0, g_skips = 0;
bool g_verbose = false;

void check(bool ok, const std::string& what, const std::string& fick = "")
{
    ++g_cases;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", what.c_str());
        return;
    }
    std::printf("  FEL  %s%s\n", what.c_str(), fick.empty() ? "" : (" — fick " + fick).c_str());
    ++g_fails;
}

std::string syns(const std::string& s)
{
    std::string ut;
    for (const unsigned char c : s) {
        if (c < 0x20 || c == 0x7F) {
            char b[8];
            std::snprintf(b, sizeof b, "\\x%02X", c);
            ut += b;
        } else {
            ut += static_cast<char>(c);
        }
    }
    return "\"" + ut + "\"";
}

// Skärmrad `r` (0 = skärmens översta) som text, avslutande blanksteg bort.
std::string rad(const t::Screen& s, int r)
{
    const t::Line& l = s.line(s.historySize() + r);
    std::string    ut;
    for (int c = 0; c < static_cast<int>(l.cells.size()); ++c)
        ut += l.text(c);
    while (!ut.empty() && ut.back() == ' ')
        ut.pop_back();
    return ut;
}

// Hela skärmen och historiken som rader med `\n` — det sessionsfallen söker i.
std::string allt(const t::Screen& s)
{
    std::string ut;
    for (int i = 0; i < s.historySize() + s.rows(); ++i) {
        const t::Line& l = s.line(i);
        for (int c = 0; c < static_cast<int>(l.cells.size()); ++c)
            ut += l.text(c);
        ut += '\n';
    }
    return ut;
}

// ================================================================== P

void fallTolken()
{
    {   // CUP, EL, ED
        t::Screen s(10, 4);
        s.feed("abc\r\ndef\r\nghi");
        check(rad(s, 0) == "abc" && rad(s, 1) == "def" && rad(s, 2) == "ghi",
              "P: text och CR LF");
        s.feed("\x1b[1;2H\x1b[K");
        check(rad(s, 0) == "a" && s.cursorRow() == 0 && s.cursorCol() == 1,
              "P: CUP 1;2 och EL 0 raderar från markören", syns(rad(s, 0)));
        s.feed("\x1b[2;2H\x1b[1K");
        check(rad(s, 1) == "  f", "P: EL 1 raderar till och med markören", syns(rad(s, 1)));
        s.feed("\x1b[2;1H\x1b[J");
        check(rad(s, 0) == "a" && rad(s, 1).empty() && rad(s, 2).empty(),
              "P: ED 0 raderar från markören till slutet");
        s.feed("\x1b[2J");
        check(rad(s, 0).empty(), "P: ED 2 raderar allt");
    }
    {   // DECSTBM + IND/RI
        t::Screen s(5, 5);
        s.feed("1\r\n2\r\n3\r\n4\r\n5");
        s.feed("\x1b[2;4r");   // område rad 2–4, markören hem
        check(s.cursorRow() == 0 && s.cursorCol() == 0, "P: DECSTBM flyttar markören hem");
        s.feed("\x1b[4;1H\x1b" "D");   // IND på områdets sista rad
        check(rad(s, 0) == "1" && rad(s, 1) == "3" && rad(s, 2) == "4" && rad(s, 3).empty() &&
                  rad(s, 4) == "5",
              "P: IND i områdets botten rullar bara området",
              rad(s, 0) + "|" + rad(s, 1) + "|" + rad(s, 2) + "|" + rad(s, 3) + "|" + rad(s, 4));
        s.feed("\x1b[2;1H\x1bM");   // RI på områdets första rad
        check(rad(s, 0) == "1" && rad(s, 1).empty() && rad(s, 2) == "3" && rad(s, 3) == "4" &&
                  rad(s, 4) == "5",
              "P: RI i områdets topp rullar området nedåt",
              rad(s, 0) + "|" + rad(s, 1) + "|" + rad(s, 2) + "|" + rad(s, 3) + "|" + rad(s, 4));
        check(s.historySize() == 0, "P: rullning i ett område når inte historiken");
    }
    {   // ICH/DCH/ECH, IL/DL
        t::Screen s(6, 3);
        s.feed("abcdef\x1b[1;3H\x1b[2@");
        check(rad(s, 0) == "ab  cd", "P: ICH 2 skjuter höger, det som faller ut försvinner",
              syns(rad(s, 0)));
        s.feed("\x1b[2P");
        check(rad(s, 0) == "abcd", "P: DCH 2 drar in från höger", syns(rad(s, 0)));
        s.feed("\x1b[1;2H\x1b[2X");
        check(rad(s, 0) == "a  d", "P: ECH 2 raderar utan att flytta", syns(rad(s, 0)));
        t::Screen r(3, 3);
        r.feed("1\r\n2\r\n3\x1b[2;1H\x1b[L");
        check(rad(r, 0) == "1" && rad(r, 1).empty() && rad(r, 2) == "2", "P: IL infogar en rad");
        r.feed("\x1b[M");
        check(rad(r, 1) == "2" && rad(r, 2).empty(), "P: DL tar bort raden");
    }
    {   // den fördröjda radbrytningen
        t::Screen s(3, 2);
        s.feed("abc");
        check(s.cursorRow() == 0 && s.cursorCol() == 2 && rad(s, 1).empty(),
              "P: autowrap — markören väntar i sista kolumnen");
        s.feed("d");
        check(rad(s, 0) == "abc" && rad(s, 1) == "d" && s.line(0).wrapped,
              "P: nästa tecken bryter raden, och raden är MJUKT bruten");
        t::Screen c(3, 2);
        c.feed("abc\rX");
        check(rad(c, 0) == "Xbc" && rad(c, 1).empty(), "P: CR efter sista kolumnen bryter inte");
    }
    {   // 1049
        t::Screen s(4, 2);
        s.feed("ab\x1b[?1049h");
        check(s.modes().altScreen && rad(s, 0).empty(), "P: 1049 byter till en rensad skärm");
        s.feed("xy\x1b[?1049l");
        check(!s.modes().altScreen && rad(s, 0) == "ab" && s.cursorCol() == 2,
              "P: 1049 tillbaka — texten och markören återställda");
    }
    {   // SGR
        t::Screen s(8, 1);
        s.feed("\x1b[38;5;196mA\x1b[38;2;1;2;3mB\x1b[38:2::4:5:6mC\x1b[48:5:7mD\x1b[1;4mE\x1b[0mF");
        const t::Line& l = s.line(0);
        check(l.cells[0].fg == t::Color::indexed(196), "P: SGR 38;5;196");
        check(l.cells[1].fg == t::Color::rgb(1, 2, 3), "P: SGR 38;2;r;g;b");
        check(l.cells[2].fg == t::Color::rgb(4, 5, 6), "P: SGR 38:2::r:g:b (kolonformen)");
        check(l.cells[3].bg == t::Color::indexed(7), "P: SGR 48:5:7");
        check((l.cells[4].attrs & t::attr::kBold) && (l.cells[4].attrs & t::attr::kUnderline),
              "P: SGR 1;4");
        check(l.cells[5].attrs == 0 && l.cells[5].fg == t::Color{} && l.cells[5].bg == t::Color{},
              "P: SGR 0 återställer");
    }
    {   // UTF-8
        t::Screen s(8, 1);
        s.feed("\xC3");
        s.feed("\xA5");
        s.feed("\xFF" "\xC3" "A" "\xE0\x80\xAF");
        check(rad(s, 0) == "\xC3\xA5\xEF\xBF\xBD\xEF\xBF\xBD" "A\xEF\xBF\xBD",
              "P: UTF-8 över en buffertgräns, ogiltigt och överlångt → U+FFFD",
              syns(rad(s, 0)));
    }
    {   // brett tecken i sista kolumnen, kombinerande
        t::Screen s(3, 2);
        s.feed("ab\xE4\xB8\xAD");   // ab中
        check(rad(s, 0) == "ab" && rad(s, 1) == "\xE4\xB8\xAD" &&
                  s.line(1).cells[0].width == 2 && s.line(1).cells[1].width == 0,
              "P: ett brett tecken i sista kolumnen bryter raden först");
        t::Screen k(4, 1);
        k.feed("e\xCC\x81x");   // e + U+0301, x
        check(k.line(0).text(0) == "e\xCC\x81" && k.line(0).text(1) == "x",
              "P: ett kombinerande tecken fäster vid tecknet före");
    }
    {   // svaren
        t::Screen s(10, 5);
        s.feed("\x1b[2;3H\x1b[6n\x1b[c\x1b[5n\x1b[>c\x1b[?2026$p");
        check(s.takeReplies() ==
                  "\x1b[2;3R\x1b[?62;22c\x1b[0n\x1b[>0;10;1c\x1b[?2026;2$y",
              "P: DSR 6, DA1, DSR 5, DA2 och DECRQM som svarsbytes");
        s.setReportedColors(0x112233, 0xA0B0C0, 0xFFFFFF);
        s.feed("\x1b]11;?\x07");
        check(s.takeReplies() == "\x1b]11;rgb:a0a0/b0b0/c0c0\x07",
              "P: OSC 11-frågan svarar med bakgrunden, med frågans avslut");
        s.feed("\x1b[>1u\x1b[?u");
        check(s.keyboardFlags() == 1 && s.takeReplies() == "\x1b[?1u",
              "P: kittyprotokollet — push och frågan");
        s.feed("\x1b[<u");
        check(s.keyboardFlags() == 0, "P: kittyprotokollet — pop");
    }
    {   // OSC, DCS, linjegrafik
        t::Screen s(10, 2);
        s.feed("\x1b]0;Hej\x07");
        check(s.title() == "Hej" && s.titleChanged(), "P: OSC 0 med BEL");
        s.clearDirty();
        s.feed("\x1b]2;D\xC3\xA5\x1b\\");
        check(s.title() == "D\xC3\xA5" && s.titleChanged(), "P: OSC 2 med ST, UTF-8");
        s.feed("\x1bPq#0;2;0;0;0~-\x1b\\X");
        check(rad(s, 0) == "X", "P: DCS sväljs till ST", syns(rad(s, 0)));
        s.feed("\r\n\x1b(0qx\x1b(Bq");
        s.clearDirty();
        s.feed("\r");
        check(s.isDirty(s.cursorRow()) && !s.allDirty(),
              "P: CR markerar markörens rad smutsig (markören ritas om)");
        s.feed("\x1b]99999999999999999999;x\x07\x1b]2;T\x07");
        check(s.title() == "T", "P: ett för stort OSC-nummer ignoreras, nästa OSC tolkas");
        check(rad(s, 1) == "\xE2\x94\x80\xE2\x94\x82q", "P: DEC-linjegrafik i G0 och tillbaka",
              syns(rad(s, 1)));
    }
}

// ================================================================== W

void fallBredden()
{
    check(t::cellWidth(U'A') == 1 && t::cellWidth(0x4E2D) == 2 && t::cellWidth(0x1F600) == 2 &&
              t::cellWidth(0x0301) == 0 && t::cellWidth(0x200D) == 0,
          "W: ASCII 1, CJK 2, emoji 2, U+0301 och ZWJ 0");
    t::Screen s(4, 1);
    s.feed("\xE2\x9D\xA4\xEF\xB8\x8F" "x");   // ❤ + VS16, x
    check(s.line(0).cells[0].width == 2 && s.line(0).cells[1].width == 0 &&
              s.line(0).text(2) == "x",
          "W: VS16 breddar en pictografisk bas till två celler");
    t::Screen k(3, 2);
    k.feed("ab\xE2\x9D\xA4");   // ❤ i sista kolumnen, väntande radbrytning
    k.feed("\xEF\xB8\x8F" "x");   // VS16, x
    check(k.line(0).wrapped && k.line(1).cells[0].width == 2 &&
              k.line(1).text(0) == "\xE2\x9D\xA4\xEF\xB8\x8F" && k.line(1).text(2) == "x" &&
              k.line(0).text(2) == " ",
          "W: VS16 vid högerkanten bryter raden som ett ursprungligen brett tecken");
    t::Screen c(4, 1);
    std::string kombi = "e";
    for (int i = 0; i < 100; ++i)
        kombi += "\xCC\x81";   // U+0301
    c.feed(kombi + "x");
    check(c.line(0).text(0).size() == 1 + 31 * 2 && c.line(0).text(1) == "x",
          "W: ett kluster har högst 32 kodpunkter — överflödet sväljs",
          std::to_string(c.line(0).text(0).size()));
    t::Screen f(4, 1);
    f.feed("\xF0\x9F\x87\xB8\xF0\x9F\x87\xAA" "x");   // 🇸🇪 x
    check(f.line(0).cells[0].width == 2 && f.line(0).text(2) == "x",
          "W: två regionala indikatorer är EN flagga i två celler");
}

// ================================================================== C

void fallKommandoraden()
{
    const struct {
        const char*              in;
        std::vector<std::string> ut;
    } fall[] = {
        {R"("a b c" d e)", {"a b c", "d", "e"}},
        {R"("ab\"c" "\\" d)", {"ab\"c", "\\", "d"}},
        {R"(a\\\b d"e f"g h)", {"a\\\\\\b", "de fg", "h"}},
        {R"(a\\\"b c d)", {"a\\\"b", "c", "d"}},
        {R"(a\\\\"b c" d e)", {"a\\\\b c", "d", "e"}},
        {R"(a"b"" c d)", {"ab\" c d"}},
        {R"(  "" x  )", {"", "x"}},
    };
    for (const auto& f : fall) {
        const std::vector<std::string> fick = t::splitCommandLine(f.in);
        std::string                    s;
        for (const auto& a : fick)
            s += "[" + a + "]";
        check(fick == f.ut, std::string("C: ") + f.in, s);
    }
    for (const std::string a : {"enkel", "med blanksteg", "", "c\"itat", "slut\\", "a\\\"b",
                                "C:\\Program Files\\x\\"}) {
        const std::vector<std::string> åter = t::splitCommandLine(t::quoteArgument(a));
        check(åter.size() == 1 && åter[0] == a, "C: citeringen tillbaka för " + syns(a),
              t::quoteArgument(a));
    }
}

// ================================================================== H

void fallHistoriken()
{
    t::Screen s(3, 2, 3);
    s.feed("1\r\n2\r\n3\r\n4\r\n5\r\n6");
    check(s.historySize() == 3 && s.linesDropped() == 1 && s.line(0).text(0) == "2" &&
              s.line(2).text(0) == "4" && rad(s, 0) == "5" && rad(s, 1) == "6",
          "H: taket 3 — de äldsta försvinner och räknas i linesDropped");
    s.clearHistory();
    check(s.historySize() == 0 && s.linesDropped() == 4 && rad(s, 1) == "6",
          "H: clearHistory tömmer historiken, skärmen står kvar");
    s.feed("\x1b[?1049h");
    check(s.historySize() == 0, "H: den alternativa skärmen visar ingen historik");
}

// ================================================================== S

#ifndef TERMINAL_PROBE
#error TERMINAL_PROBE (sökvägen till terminal_probe) ska sättas av tests/CMakeLists.txt
#endif

// En session och en skärm att läsa den genom.
struct Kör {
    t::Session              s;
    t::Screen               sc{80, 24};
    std::mutex              m;
    std::condition_variable cv;
    bool                    signal = false, slut = false;

    Kör()
    {
        s.onOutput = [this] {
            std::lock_guard<std::mutex> l(m);
            signal = true;
            cv.notify_all();
        };
        s.onExit = [this] {
            std::lock_guard<std::mutex> l(m);
            slut = true;
            cv.notify_all();
        };
    }
    // Trådarna rör `m` och `cv` — de joinas innan medlemmarna förstörs.
    ~Kör() { s.kill(); }

    bool start(const std::string& argument, std::vector<t::EnvChange> env = {})
    {
        t::StartInfo info;
        info.program     = TERMINAL_PROBE;
        info.arguments   = argument;
        info.environment = std::move(env);
        info.columns     = 80;
        info.rows        = 24;
        return s.start(info);
    }

    // Hämta och tolka tills `klart` eller tidsgränsen.
    template <class F>
    bool vänta(F klart, int ms)
    {
        const auto gräns = Clock::now() + std::chrono::milliseconds(ms);
        for (;;) {
            bool more = true;
            while (more) {
                const std::string b = s.take(more);
                if (b.empty())
                    break;
                sc.feed(b);
                const std::string svar = sc.takeReplies();
                if (!svar.empty())
                    s.write(svar);
            }
            if (klart())
                return true;
            std::unique_lock<std::mutex> l(m);
            if (Clock::now() >= gräns)
                return false;
            cv.wait_for(l, std::chrono::milliseconds(20), [&] { return signal; });
            signal = false;
        }
    }
    bool har(const std::string& text) { return allt(sc).find(text) != std::string::npos; }
};

void fallSessionen()
{
    {   // S1
        Kör k;
        if (!k.start("")) {
            std::printf("  --   sessionsfallen hoppas över: %s\n", k.s.lastError().c_str());
            ++g_skips;
            return;
        }
        check(k.vänta([&] { return k.har("SIZE 80 24"); }, 10000),
              "S1: proben startar och ser sin storlek 80×24");
        // S2
        k.s.resize(100, 30);
        k.sc.resize(100, 30);
        check(k.vänta([&] { return k.har("SIZE 100 30"); }, 5000),
              "S2: storleksändringen når programmet");
        // S3
        k.s.write("\x03" "\t");
        check(k.vänta([&] { return k.har("<03><09>"); }, 5000),
              "S3: Ctrl+C (0x03) och Tab når programmet som bytes (rått läge)");
        k.s.write("Q");
        check(k.vänta([&] { return !k.s.isRunning(); }, 5000) && k.s.exitCode() == 7,
              "S1: slutkoden är 7", std::to_string(k.s.exitCode()));
    }
    {   // S4 — taket och den delade hämtningen
        Kör k;
        k.start("--flood 64");
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));   // ingen tömmer
        const std::size_t topp = k.s.inboxHighWater();
        check(topp <= t::Session::kInboxMax, "S4: inkorgen går aldrig över taket",
              std::to_string(topp));
#ifndef _WIN32
        // POSIX: pty:n släpper igenom hela strömmen, så läsaren MÅSTE ha nått
        // taket och väntat — annars mätte fallet ingenting.
        check(topp > t::Session::kInboxMax / 2,
              "S4: inkorgen fylls mot taket (läsaren väntade på ägaren)",
              std::to_string(topp));
#endif
        if (topp > t::Session::kTakeBudget) {
            bool              more = false;
            const std::string b    = k.s.take(more);
            check(b.size() == t::Session::kTakeBudget && more,
                  "S4: en hämtning är högst kTakeBudget — resten väntar till nästa varv",
                  std::to_string(b.size()));
            k.sc.feed(b);
        } else {
            // Windows: conhost ritar om skärmen och skickar bara skillnaderna —
            // 64 MiB rader kan bli några hundra KiB. Då pressades varken taket
            // eller budgeten, och blocket säger det i stället för att vara grönt.
            std::printf("  --   S4: bara %zu byte nådde inkorgen (ConPTY komprimerar "
                        "strömmen) — taket och budgeten pressades inte\n",
                        topp);
            ++g_skips;
        }
        check(k.vänta([&] { return !k.s.isRunning(); }, 110000) && k.s.exitCode() == 7,
              "S4: hela utskriften kommer fram och slutkoden är 7",
              std::to_string(k.s.exitCode()));
        check(k.s.inboxHighWater() <= t::Session::kInboxMax,
              "S4: taket höll hela vägen", std::to_string(k.s.inboxHighWater()));
    }
    {   // S5
        Kör k;
        k.start("--flood 64");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const auto t0 = Clock::now();
        k.s.kill();
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
        check(ms < 2000 && !k.s.isRunning(), "S5: kill() mitt i en utskrift — klar inom 2 s",
              std::to_string(ms) + " ms");
    }
    {   // S6
        Kör k;
        k.start("--env TERM");
        check(k.vänta([&] { return k.har("ENV TERM=xterm-256color"); }, 5000),
              "S6: förvalet sätter TERM=xterm-256color");
        Kör a;
        a.start("--env TERM", {t::EnvChange{"TERM", std::string("dumb")}});
        check(a.vänta([&] { return a.har("ENV TERM=dumb"); }, 5000),
              "S6: appens TERM vinner över förvalet");
        Kör b;
        b.start("--env COLORTERM", {t::EnvChange{"COLORTERM", std::nullopt}});
        check(b.vänta([&] { return b.har("ENV COLORTERM saknas"); }, 5000),
              "S6: en borttagning är också att nämna variabeln");
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("terminal_gate — sidopaketet Terminals kärna\n");
    fallTolken();
    fallBredden();
    fallKommandoraden();
    fallHistoriken();
    fallSessionen();
    std::printf("\n%s — %d fall, %d fel, %d överhoppade block\n",
                g_fails == 0 ? "TERMINAL_GATE GRÖN" : "TERMINAL_GATE FÄLLD", g_cases, g_fails,
                g_skips);
    if (g_fails != 0)
        return 1;
    return g_skips == 0 ? 0 : 77;
}
