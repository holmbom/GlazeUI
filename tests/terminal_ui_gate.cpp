// terminal_ui_gate — WIDGETEN glaze::Terminal.
//
// Kör:  ./build-test/tests/terminal_ui_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen eller
//       processen kunde inte köras (ingen display, ingen ConPTY) och resten
//       var grönt
//
// Länkar `glaze_terminal_ui` — som en app gör.
//
//   R0 registret: "terminal" ger en riktig glaze::Terminal (WHOLE_ARCHIVE)
//   K  `encodeKey`: pilarna med och utan DECCKM, F-tangenterna, modifierar-
//      koderna, Ctrl+bokstav, Ctrl+Alt+bokstav (AltGr) tas INTE, Alt som Meta
//      på och av, Ctrl+Tab tas inte, Cmd tas inte, kittyprotokollet
//      (Escape, Shift+Enter, Ctrl+Shift+X) — och genvägarna för urklippet
//   P  `encodePaste`: radsluten blir CR, bracketed paste ramar in och tar
//      bort en inbäddad `ESC[201~`
//   F  färgerna: för ALLA tjugo paletter × två lägen mäter förgrunden ≥ 4,5:1
//      och de sex kulörerna och BrightBlack ≥ 3:1 mot bakgrunden
//      (`glaze::color::contrast`); i ett fönster följer Auto `setThemeMode`,
//      Light står kvar vid ett byte till mörkt, och `setSchemeColor` vinner
//   R  routingen genom fönstrets riktiga ingång (`handleEvent`) mot en
//      RIKTIG process (terminal_probe): Tab, Escape och Ctrl+C når proben;
//      fönstrets accelerator Ctrl+C fyras INTE, avbrytknappen klickas INTE,
//      fokus flyttas INTE; Ctrl+Tab går till förfadern; en `onKeyDown` med
//      `handled` vinner (F5 når aldrig proben); slutkoden 7 och `onExit` en gång
//   M  markeringen och texten (`selectAll`/`selectedText`): avslutande
//      blanksteg bort, åäö hela, en MJUKT bruten rad ihop
//   V  vyn följer slutet, slutar följa efter en uppåtrullning (ny utskrift
//      rullar den inte), och följer igen vid botten
//   Y  2026 håller invalideringen (damage-stämpeln står still) tills släppet —
//      eller tills timern tvingar fram en ritning
//
// M, V och Y matas med `feedOutput` (samma tolkning och invalidering som
// pty:ns utdata) — deterministiskt, utan conhost:s egen återgivning emellan.
//
// SABOTAGEN som ska fälla den: WHOLE_ARCHIVE borttaget (R0), DECCKM ignoreras
// (K), Ctrl+Alt behandlas som Ctrl (K), `keyDown` tar inte Tab (R — fokus
// flyttas), följningen utan uppåtvillkor (V), 2026 som invaliderar ändå (Y),
// kontrastkonstanten för ljust läge (`kHueLight` 0,46 — F).
//
// VAD DEN INTE MÄTER: att program ser rätt ut, pixlarna, IME, musens
// markering och musrapporteringen (proben läser ingen mus), hjulet i den
// alternativa skärmen, fönstrets aktivering för 1004 (ett dolt fönster blir
// aldrig aktivt), urklippet, macOS och Linux (blinda — Cmd-genvägarna mäts
// bara där de kompileras), och den delade hämtningen (terminal_gate S4: taket
// bor i `Session::take`, och widgeten kan inte gå förbi det).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <glaze/glaze.h>
#include <glaze_terminal/terminal.h>

#include "keys.h"   // intern (tests/CMakeLists.txt pekar hit)

namespace {

namespace t = glaze::term;

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

// ================================================================= K, P

void fallTangenterna()
{
    using gfx::Key;
    const std::uint32_t C = gfx::kModCtrl, S = gfx::kModShift, A = gfx::kModAlt;
    t::KeyModes         m;
    auto                k = [&](Key key, std::uint32_t mods = 0) {
        return t::encodeKey(key, mods, m);
    };
    check(k(Key::Up) == "\x1b[A" && k(Key::Left) == "\x1b[D" && k(Key::Home) == "\x1b[H",
          "K: pilarna och Home i CSI-form");
    m.cursorKeys = true;
    check(k(Key::Up) == "\x1bOA" && k(Key::End) == "\x1bOF", "K: DECCKM ger SS3-formen");
    check(k(Key::Up, C) == "\x1b[1;5A", "K: Ctrl+Upp är CSI 1;5A också i DECCKM");
    m.cursorKeys = false;
    check(k(Key::F1) == "\x1bOP" && k(Key::F4) == "\x1bOS" && k(Key::F5) == "\x1b[15~" &&
              k(Key::F12) == "\x1b[24~",
          "K: F1–F4 SS3, F5–F12 CSI n~");
    check(k(Key::F5, S) == "\x1b[15;2~" && k(Key::Delete, A | C) == "\x1b[3;7~" &&
              k(Key::F2, C) == "\x1b[1;5Q",
          "K: modifierarkoderna m = 1 + Shift + 2·Alt + 4·Ctrl");
    check(k(Key::C, C) == "\x03" && k(Key::A, C) == "\x01" && k(Key::Space, C) == std::string(1, '\0') &&
              k(Key::LeftBracket, C) == "\x1b",
          "K: Ctrl+bokstav, Ctrl+Space och Ctrl+[ som styrtecken");
    check(k(Key::C, C | A).empty() && k(Key::N2, C | A).empty(),
          "K: Ctrl+Alt (AltGr) tas INTE — tecknet kommer som text");
    check(k(Key::C, C | S).empty(), "K: Ctrl+Shift+bokstav tas inte utan kittyprotokollet");
    check(k(Key::Tab) == "\t" && k(Key::Tab, S) == "\x1b[Z" && k(Key::Tab, C).empty() &&
              k(Key::Tab, C | S).empty(),
          "K: Tab, Shift+Tab — Ctrl+Tab är vägen ut");
    check(k(Key::Return) == "\r" && k(Key::Return, S) == "\r" && k(Key::Backspace) == "\x7f" &&
              k(Key::Backspace, C) == "\x08" && k(Key::Escape) == "\x1b",
          "K: Enter, Shift+Enter (legacy), Backsteg, Ctrl+Backsteg, Escape");
    m.newline = true;
    check(k(Key::Return) == "\r\n", "K: Enter i LNM är CR LF");
    m.newline = false;
    check(k(Key::X, A) == "\x1bx" && k(Key::X, A | S) == "\x1bX",
          "K: Alt som Meta — ESC + tecknet, versalt med Shift");
    m.altAsMeta = false;
    check(k(Key::X, A).empty(), "K: utan Alt som Meta tas Alt+bokstav inte (Option skriver)");
    m.altAsMeta = true;
    check(k(Key::X, gfx::kModCmd).empty() && k(Key::A).empty() && k(Key::Space).empty(),
          "K: Cmd tas inte, och en skrivande tangent utan modifierare är text");
    m.kittyFlags = 1;
    check(k(Key::Escape) == "\x1b[27u" && k(Key::Return, S) == "\x1b[13;2u" &&
              k(Key::X, C | S) == "\x1b[120;6u" && k(Key::Return) == "\r" &&
              k(Key::Tab, C).empty(),
          "K: kittyprotokollet — Escape, Shift+Enter och Ctrl+Shift+X entydiga");
    m.cursorKeys = true;
    check(k(Key::Up) == "\x1b[A" && k(Key::End) == "\x1b[F" && k(Key::Up, S) == "\x1b[1;2A" &&
              k(Key::F1) == "\x1b[P" && k(Key::F3) == "\x1b[13~" &&
              k(Key::F3, C) == "\x1b[13;5~" && k(Key::F4, A) == "\x1b[1;3S",
          "K: kittyprotokollet — pilarna och F1–F4 i CSI-form också i DECCKM, F3 = CSI 13~");
    m.cursorKeys = false;
    m.kittyFlags = 0;

#ifdef __APPLE__
    check(t::clipboardKey(Key::C, gfx::kModCmd) == t::ClipboardKey::Copy &&
              t::clipboardKey(Key::V, gfx::kModCmd) == t::ClipboardKey::Paste &&
              t::clipboardKey(Key::C, C | S) == t::ClipboardKey::None,
          "K: urklippet — Cmd+C/V på macOS");
#else
    check(t::clipboardKey(Key::C, C | S) == t::ClipboardKey::Copy &&
              t::clipboardKey(Key::V, C | S) == t::ClipboardKey::Paste &&
              t::clipboardKey(Key::Insert, C) == t::ClipboardKey::Copy &&
              t::clipboardKey(Key::Insert, S) == t::ClipboardKey::Paste &&
              t::clipboardKey(Key::C, C) == t::ClipboardKey::None,
          "K: urklippet — Ctrl+Shift+C/V och Ctrl/Shift+Insert, Ctrl+C är programmets");
#endif

    check(t::encodePaste("a\r\nb\nc", false) == "a\rb\rc", "P: radsluten blir CR",
          syns(t::encodePaste("a\r\nb\nc", false)));
    check(t::encodePaste("x\x1b[201~y", true) == "\x1b[200~xy\x1b[201~",
          "P: bracketed paste ramar in och tar bort en inbäddad slutmarkör",
          syns(t::encodePaste("x\x1b[201~y", true)));
}

// =================================================================== F

void fallKontrasten()
{
    int paletter = 0;
    for (const glaze::UI u : {glaze::UI::Glaze, glaze::UI::Slate, glaze::UI::Minimalistic})
        for (int i = 0; i < glaze::themeCount(u); ++i) {
            const glaze::Theme& th = glaze::themeAt(u, i);
            ++paletter;
            for (const bool mörk : {false, true}) {
                const glaze::Theme::Variant& v = mörk ? th.dark : th.light;
                const glaze::TerminalPalette p =
                    glaze::terminalPalette(glaze::TerminalScheme::Theme, mörk, v);
                const gfx::Color bg = p[glaze::TerminalColor::Background];
                const std::string namn = th.name + (mörk ? " mörk" : " ljus");
                const float fg = glaze::color::contrast(p[glaze::TerminalColor::Foreground], bg);
                check(fg >= 4.5f, "F: förgrunden ≥ 4,5:1 — " + namn, std::to_string(fg));
                for (const glaze::TerminalColor r :
                     {glaze::TerminalColor::Red, glaze::TerminalColor::Green,
                      glaze::TerminalColor::Yellow, glaze::TerminalColor::Blue,
                      glaze::TerminalColor::Magenta, glaze::TerminalColor::Cyan,
                      glaze::TerminalColor::BrightBlack}) {
                    const float c = glaze::color::contrast(p[r], bg);
                    check(c >= 3.0f,
                          "F: roll " + std::to_string(static_cast<int>(r)) + " ≥ 3:1 — " + namn,
                          std::to_string(c));
                }
            }
        }
    check(paletter == 20, "F: alla tjugo paletter mätta", std::to_string(paletter));
}

// ============================================================= fönstret

#ifndef TERMINAL_PROBE
#error TERMINAL_PROBE (sökvägen till terminal_probe) ska sättas av tests/CMakeLists.txt
#endif

struct Prov : glaze::Terminal {
    using glaze::Panel::scrollRange;
    using glaze::Widget::damageStamp;
};

// Förfadern som tar Ctrl+Tab (TabControls väg, `descendantKeyDown`).
struct Förfader : glaze::Panel {
    int  träffar = 0;
    bool descendantKeyDown(gfx::Key k, std::uint32_t m) override
    {
        if (k == gfx::Key::Tab && (m & gfx::kModCtrl)) {
            ++träffar;
            return true;
        }
        return false;
    }
};

struct Fonster : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    Förfader      fader;
    Prov          term;
    Prov          visare;
    glaze::Button avbryt{"Avbryt"};
    int           acc = 0, avbrutet = 0, slut = 0;

    Fonster() : glaze::AppWindow("terminal_ui_gate", 900, 640)
    {
        fader.x      = 10;
        fader.y      = 10;
        fader.width  = 420;
        fader.height = 300;
        term.dock    = glaze::Dock::Fill;
        visare.x      = 450;
        visare.y      = 10;
        visare.width  = 400;
        visare.height = 200;
        avbryt.x      = 10;
        avbryt.y      = 400;
        root().adopt(fader);
        fader.adopt(term);
        root().adopt(visare);
        root().adopt(avbryt);
        setCancelButton(avbryt);
        avbryt.onClick = [this] { ++avbrutet; };
        menuBar.show();
        menuBar.addItem("Arkiv", "Kopiera", "Ctrl+C");
        menuBar.setHandler({"Arkiv", "Kopiera"}, [this] { ++acc; });
        term.onKeyDown = [](glaze::Widget*, glaze::KeyArgs& a) {
            if (a.key == gfx::Key::F5)
                a.handled = true;   // appens tangent vinner
        };
        term.onExit = [this] { ++slut; };
    }

    void tangent(gfx::Key k, std::uint32_t mods = 0)
    {
        gfx::Event e;
        e.type     = gfx::EventType::KeyDown;
        e.windowId = window().id();
        e.key      = k;
        e.mods     = mods;
        handleEvent(e);
    }
    void skriv(const char* utf8)
    {
        gfx::Event e;
        e.type     = gfx::EventType::TextInput;
        e.windowId = window().id();
        std::strncpy(e.text, utf8, sizeof(e.text) - 1);
        handleEvent(e);
    }
    void hjul(Prov& w, float rader)
    {
        const glaze::Ctx c = w.behaviorCtx();
        const gfx::Rectf r = w.resolve(c);
        gfx::Event       e;
        e.type     = gfx::EventType::MouseWheel;
        e.windowId = window().id();
        e.mouseX   = r.x + r.w / 2.0f;
        e.mouseY   = r.y + r.h / 2.0f;
        e.wheelY   = rader;
        handleEvent(e);
    }
};

std::string text(glaze::Terminal& t)
{
    t.selectAll();
    std::string s = t.selectedText();
    t.clearSelection();
    return s;
}

// Stegen körs ett i taget av en timer: ett steg svarar true när det är klart.
struct Steg {
    std::string           namn;
    std::function<bool()> klart;
    int                   ms = 5000;
};

void fallMedFonster()
{
    std::unique_ptr<glaze::Widget> r0 = glaze::makeWidget("terminal");
    check(dynamic_cast<glaze::Terminal*>(r0.get()) != nullptr,
          "R0: registret ger en riktig glaze::Terminal (WHOLE_ARCHIVE/ankaret)");

    Fonster w;
    w.setOffscreen(true);
    bool processen = true;
    float före     = 0.0f;
    int   väntaRitning = 0;
    std::uint64_t stämpel = 0;

    std::vector<Steg> steg = {
        {"start", [&] {
             w.term.setProgram(TERMINAL_PROBE);
             if (!w.term.start()) {
                 std::printf("  --   R hoppas över: %s\n", w.term.lastError().c_str());
                 processen = false;
                 ++g_skips;
                 return true;
             }
             w.term.focus();
             return true;
         }},
        {"proben uppe", [&] { return !processen || text(w.term).find("SIZE") != std::string::npos; }},
        {"Tab", [&] {
             if (processen)
                 w.tangent(gfx::Key::Tab);
             return true;
         }},
        {"Tab når proben", [&] { return !processen || text(w.term).find("<09>") != std::string::npos; }},
        {"Escape", [&] {
             if (processen)
                 w.tangent(gfx::Key::Escape);
             return true;
         }},
        {"Escape når proben", [&] { return !processen || text(w.term).find("<1B>") != std::string::npos; }},
        {"Ctrl+C, F5 och Ctrl+Tab", [&] {
             if (!processen)
                 return true;
             w.tangent(gfx::Key::C, gfx::kModCtrl);
             w.tangent(gfx::Key::F5);
             w.tangent(gfx::Key::Tab, gfx::kModCtrl);
             check(w.acc == 0, "R: fönstrets accelerator Ctrl+C fyras INTE");
             check(w.avbrutet == 0, "R: avbrytknappen klickas INTE av Escape");
             check(w.term.focused(), "R: Tab flyttade INTE fokus");
             check(w.fader.träffar == 1, "R: Ctrl+Tab går till förfadern",
                   std::to_string(w.fader.träffar));
             return true;
         }},
        {"Ctrl+C når proben", [&] { return !processen || text(w.term).find("<03>") != std::string::npos; }},
        {"Q", [&] {
             if (processen)
                 w.skriv("Q");
             return true;
         }},
        {"proben slutar", [&] { return !processen || !w.term.isRunning(); }},
        {"slutet", [&] {
             if (!processen)
                 return true;
             if (w.slut == 0)
                 return false;   // onExit kommer i pump-svansen
             const std::string s = text(w.term);
             check(s.find("<7E>") == std::string::npos,
                   "R: en onKeyDown med handled vinner — F5 nådde aldrig proben");
             check(w.term.exitCode() == 7 && w.slut == 1, "R: slutkoden 7 och onExit en gång",
                   std::to_string(w.term.exitCode()) + " / " + std::to_string(w.slut));
             return true;
         }},
        // ---- M (visaren har inget program)
        {"visaren ritad", [&] { return w.visare.columns() != 80; }},
        {"M", [&] {
             const int k = w.visare.columns();
             w.visare.feedOutput("hej   \r\nv\xC3\xA4rld\r\n" + std::string(k + 5, 'x') + "\r\n");
             const std::string s = text(w.visare);
             check(s.find("hej\nv\xC3\xA4rld\n" + std::string(k + 5, 'x')) == 0,
                   "M: blanksteg bort, åäö hela, den mjukt brutna raden ihop", syns(s.substr(0, 40)));
             return true;
         }},
        // ---- F i fönstret
        {"F", [&] {
             const glaze::Theme& th = w.theme();
             w.setThemeMode(glaze::ThemeMode::Light);
             check(w.visare.palette()[glaze::TerminalColor::Background] == th.light.colors.panel,
                   "F: Auto följer appens ljusa läge");
             w.setThemeMode(glaze::ThemeMode::Dark);
             check(w.visare.palette()[glaze::TerminalColor::Background] == th.dark.colors.panel,
                   "F: Auto följer bytet till mörkt");
             w.visare.setColorMode(glaze::TerminalColorMode::Light);
             check(w.visare.palette()[glaze::TerminalColor::Background] == th.light.colors.panel,
                   "F: Light står kvar när appen är mörk");
             const gfx::Color egen{1, 2, 3, 255};
             w.visare.setSchemeColor(glaze::ThemeMode::Light, glaze::TerminalColor::Red, egen);
             check(w.visare.palette()[glaze::TerminalColor::Red] == egen,
                   "F: setSchemeColor vinner över schemat");
             w.visare.clearSchemeColors();
             w.visare.setColorMode(glaze::TerminalColorMode::Auto);
             w.setThemeMode(glaze::ThemeMode::Light);
             return true;
         }},
        // ---- V
        {"V fyll", [&] {
             w.visare.setScrollback(60);   // ringen blir FULL — fynd 12
             std::string s;
             for (int i = 0; i < 100; ++i)
                 s += "rad " + std::to_string(i) + "\r\n";
             w.visare.feedOutput(s);
             return true;
         }},
        {"V följer", [&] {
             const float r = w.visare.scrollRange(w.visare.behaviorCtx()).h;
             if (r <= 0.0f || w.visare.scrollY() != r)
                 return false;   // efter en ritning
             w.hjul(w.visare, 3.0f);
             före = w.visare.scrollY();
             check(före < r, "V: hjulet uppåt rullar historiken", std::to_string(före));
             w.visare.feedOutput("mer\r\nmer\r\n");
             väntaRitning = 0;
             return true;
         }},
        // Några riktiga ritningar innan jämförelsen — kompensationen och
        // följningen sker i `draw`.
        {"V ritning", [&] { return ++väntaRitning >= 10; }},
        {"V står kvar", [&] {
             // Den fulla ringen tappade två rader: vyn flyttas två rader upp
             // och står stilla över samma text (och följer inte slutet).
             const glaze::Ctx c  = w.visare.behaviorCtx();
             const float      ch = glaze::Draw::lineHeight(
                 glaze::Draw::withFont(c, gfx::FontFamily::Mono));
             check(std::fabs((före - w.visare.scrollY()) - 2.0f * ch) < 0.01f,
                   "V: ny utskrift rullar inte vyn medan användaren läser historiken — "
                   "den står stilla när ringen tappar sina äldsta rader",
                   std::to_string(w.visare.scrollY()) + " mot " + std::to_string(före) +
                       " − 2·" + std::to_string(ch));
             w.hjul(w.visare, -10000.0f);
             w.visare.feedOutput("sist\r\n");
             return true;
         }},
        {"V följer igen", [&] {
             const float r = w.visare.scrollRange(w.visare.behaviorCtx()).h;
             return w.visare.scrollY() == r;
         }, 3000},
        // ---- Y
        {"Y håll", [&] {
             w.avbryt.focus();   // visaren utan fokus — inget blink
             w.visare.feedOutput("\x1b[?2026h");
             stämpel = w.visare.damageStamp();
             w.visare.feedOutput("A");
             check(w.visare.damageStamp() == stämpel,
                   "Y: medan 2026 är satt invalideras ingenting");
             w.visare.feedOutput("B\x1b[?2026l");
             check(w.visare.damageStamp() != stämpel, "Y: släppet invaliderar");
             w.visare.feedOutput("\x1b[?2026hC");
             stämpel = w.visare.damageStamp();
             return true;
         }},
        {"Y timern", [&] { return w.visare.damageStamp() != stämpel; }, 2000},
    };

    std::size_t  i       = 0;
    int          väntat  = 0;
    glaze::Timer klocka;
    klocka.setInterval(20);
    klocka.onTick = [&] {
        if (!w.isOpen())
            return;
        while (i < steg.size()) {
            if (!steg[i].klart()) {
                väntat += 20;
                if (väntat > steg[i].ms) {
                    check(false, "steget '" + steg[i].namn + "' blev aldrig klart");
                    i = steg.size();
                }
                return;
            }
            ++i;
            väntat = 0;
        }
        w.visare.feedOutput("\x1b[?2026l");
        w.term.kill();
        w.close();
    };
    glaze::app().post([&] {
        if (!w.isOpen()) {
            std::printf("  --   fönsterfallen hoppas över: fönstret kom aldrig upp\n");
            ++g_skips;
            w.close();
            return;
        }
        klocka.start();
    });
    w.run();
    klocka.stop();
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("terminal_ui_gate — widgeten glaze::Terminal\n");
    fallTangenterna();
    fallKontrasten();
    fallMedFonster();
    std::printf("\n%s — %d fall, %d fel, %d överhoppade block\n",
                g_fails == 0 ? "TERMINAL_UI_GATE GRÖN" : "TERMINAL_UI_GATE FÄLLD", g_cases,
                g_fails, g_skips);
    if (g_fails != 0)
        return 1;
    return g_skips == 0 ? 0 : 77;
}
