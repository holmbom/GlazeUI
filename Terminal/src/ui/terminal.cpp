// Terminal — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med få rader:
// Draw ritar, c.style bär måtten och SCHEMAT färgerna (scheme.cpp). Kärnan
// (glaze_terminal) äger skärmen, tolken och pty:n; här bor kopplingen —
// sessionen till pump-svansen, tangenterna till programmet, markeringen,
// rullningen och ritningen. Kontraktet står i terminal.h.
//
// LEVERANSEN: sessionens läsartråd postar EN `drain` åt gången genom
// `glaze::app().post` med widgetens livstecken och SESSIONENS GENERATION —
// en post för en gammal session, eller till en död widget, är en no-op.
// `drain` tolkar högst `Session::kTakeBudget` per varv och postar sig själv
// igen för resten, och invaliderar bara de smutsiga raderna (regel 1).

#include "glaze_terminal/terminal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

#include <gfx/platform.h>

#include "glaze_terminal/screen.h"
#include "glaze_terminal/session.h"
#include "keys.h"

namespace glaze {

namespace {

using Clock = std::chrono::steady_clock;

// Ordet för dubbelklicket: bokstäver och siffror (allt utanför ASCII räknas
// dit) och tecknen som håller ihop sökvägar, adresser och flaggor.
bool isWordChar(char32_t cp)
{
    if (cp >= 0x80)
        return true;
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9'))
        return true;
    return cp != 0 && std::strchr("_-./~:@", static_cast<int>(cp)) != nullptr;
}

gfx::Color resolveColor(const term::Color& col, const TerminalPalette& p,
                        TerminalColor def)
{
    switch (col.kind) {
    case term::Color::Kind::Indexed: return p.indexed(col.r);
    case term::Color::Kind::Rgb:     return gfx::Color{col.r, col.g, col.b, 255};
    case term::Color::Kind::Default:
    default:                         return p[def];
    }
}

// En cells färger efter SGR:s omvändning, svaga och dolda text och
// markeringen.
struct Look {
    gfx::Color    fg, bg;
    std::uint16_t attrs = 0;
    bool operator==(const Look& o) const
    {
        return fg == o.fg && bg == o.bg && attrs == o.attrs;
    }
};

Look lookOf(const term::Cell& cell, const TerminalPalette& p, bool selected)
{
    Look l{resolveColor(cell.fg, p, TerminalColor::Foreground),
           resolveColor(cell.bg, p, TerminalColor::Background), cell.attrs};
    if (cell.attrs & term::attr::kInverse)
        std::swap(l.fg, l.bg);
    if (cell.attrs & term::attr::kDim)
        l.fg = terminalDimmed(l.fg, l.bg);
    if (cell.attrs & term::attr::kHidden)
        l.fg = l.bg;
    if (selected) {
        l.bg = p[TerminalColor::Selection];
        l.fg = p[TerminalColor::SelectionText];
    }
    return l;
}

gfx::FontStyle styleOf(std::uint16_t attrs)
{
    return gfx::fontStyle((attrs & term::attr::kBold) != 0,
                          (attrs & term::attr::kItalic) != 0);
}

} // namespace

// ------------------------------------------------------------ tillståndet

struct Terminal::State {
    std::unique_ptr<term::Screen>  screen;
    std::unique_ptr<term::Session> session;
    std::uint64_t                  generation = 0;   // ny per start()

    std::string                  program, arguments, workDir;
    std::vector<term::EnvChange> env;   // nycklad: en post per namn
    std::string                  lastError, title;
    int                          exitCode = -1;
    bool                         running  = false;

    int               scrollback = term::Screen::kDefaultScrollback;
    TerminalColorMode colorMode  = TerminalColorMode::Auto;
    TerminalScheme    scheme     = TerminalScheme::Theme;
    std::array<std::optional<gfx::Color>, kTerminalColorCount> own[2];   // [ljus, mörk]
    Length textSize;
#ifdef __APPLE__
    bool altAsMeta = false;   // Option skriver tecken
#else
    bool altAsMeta = true;
#endif

    bool follow    = true;    // vyn följer slutet
    bool altScreen = false;   // senast sedd — en växling släpper markeringen

    // MARKERINGEN i STABILA radindex (`linesDropped() + index`); `col` är en
    // GRÄNS (0 … kolumner), `cell` cellen trycket träffade (ordets och
    // radens markering utgår från den).
    struct Pos {
        std::int64_t line = 0;
        int          col  = 0;
        int          cell = 0;
    };
    enum class Mode : std::uint8_t { Char, Word, Line };
    bool  selecting = false;
    bool  selected  = false;
    Pos   anchor, head;
    Mode  mode = Mode::Char;
    Clock::time_point dblTime;   // trippelklicket: ett tryck strax efter ett dubbelklick
    std::int64_t      dblLine = -1;

    bool       focusReported = false;
    bool       caretOn       = true;
    bool       swallowText   = false;   // Alt som Meta tog trycket — dess tecken är inte text
    bool       syncHeld      = false;   // 2026 håller tillbaka invalideringen
    bool       titlePending  = false;   // titelns händelse väntar på 2026:s släpp
    glaze::Timer syncTimer;
    std::uint64_t droppedSeen = 0;      // `linesDropped()` vid förra ritningen

    int   mouseButton = -1;   // knappen ett RAPPORTERAT tryck håller
    int   mouseCol = -1, mouseRow = -1;
    float wheelAcc = 0.0f;
};

// En ritnings mått — cellen och rutnätets yta (widget-lokala logiska px).
struct Terminal::Grid {
    std::array<Ctx, 4> font;   // Mono i Regular, Bold, Italic, BoldItalic
    float              cw = 1.0f, ch = 1.0f;
    gfx::Rectf         area;
    int                cols = 1, rows = 1;
};

GLAZE_REGISTER_WIDGET("terminal", Terminal)

Terminal::Terminal() : s_(std::make_unique<State>())
{
    s_->screen = std::make_unique<term::Screen>(term::Screen::kDefaultColumns,
                                                term::Screen::kDefaultRows, s_->scrollback);
    setClip(true);
    setBorder(true);
    showScrollbars(true);   // historiken ska SYNAS (TextArea/MarkdownView-prejudikatet)
    s_->syncTimer.setSingleShot(true);
    s_->syncTimer.setInterval(term::Screen::kSyncTimeoutMs);
    s_->syncTimer.onTick = [this] {
        // Programmet släppte aldrig 2026 — rita ändå.
        s_->syncHeld = false;
        invalidate();
        flushTitle();
    };
}

Terminal::~Terminal()
{
    if (s_->session)
        s_->session->kill();
}

void term::detail::uiAnchor() {}

// ---------------------------------------------------------- programmet

void Terminal::setProgram(std::string path) { s_->program = std::move(path); }
const std::string& Terminal::program() const { return s_->program; }
void Terminal::setArguments(std::string commandLine) { s_->arguments = std::move(commandLine); }
const std::string& Terminal::arguments() const { return s_->arguments; }
void Terminal::setWorkingDirectory(std::string dir) { s_->workDir = std::move(dir); }
const std::string& Terminal::workingDirectory() const { return s_->workDir; }

void Terminal::setEnvironmentVariable(const std::string& name, std::string value)
{
    for (term::EnvChange& e : s_->env)
        if (e.name == name) {
            e.value = std::move(value);
            return;
        }
    s_->env.push_back(term::EnvChange{name, std::move(value)});
}

void Terminal::unsetEnvironmentVariable(const std::string& name)
{
    for (term::EnvChange& e : s_->env)
        if (e.name == name) {
            e.value.reset();
            return;
        }
    s_->env.push_back(term::EnvChange{name, std::nullopt});
}

std::optional<std::string> Terminal::environmentVariable(const std::string& name) const
{
    for (const term::EnvChange& e : s_->env)
        if (e.name == name)
            return e.value;
    return std::nullopt;
}

const std::string& Terminal::lastError() const { return s_->lastError; }
bool Terminal::isRunning() const { return s_->running; }
int Terminal::exitCode() const { return s_->exitCode; }
const std::string& Terminal::title() const { return s_->title; }
int Terminal::columns() const { return s_->screen->columns(); }
int Terminal::rows() const { return s_->screen->rows(); }

bool Terminal::start()
{
    State& s = *s_;
    AppWindow* w = appWindow();
    if (!w || !w->isOpen()) {
        s.lastError = "terminalen sitter inte i ett öppet fönster";
        gfx::log("Terminal(%s): start() — %s", name.c_str(), s.lastError.c_str());
        return false;
    }
    if (s.session)
        s.session->kill();   // en ny körning är en ny session

    // Storleken ur layouten — i onLoad har terminalen inte ritats än.
    const Ctx  c = behaviorCtx();
    const Grid g = grid(c);
    s.screen->resize(g.cols, g.rows);
    s.screen->feed("\x1b" "c");   // RIS: förra programmets lägen gäller inte (historiken står kvar)

    // Sessionens callbacks körs i ARBETARTRÅDAR: de postar bara, med
    // livstecknet och generationen fångade HÄR, på UI-tråden.
    // App finns redan (fönstret är öppet), så ingen arbetartråd skapar den.
    const std::uint64_t gen = ++s.generation;
    const std::weak_ptr<char> liv = lifeToken();
    auto session        = std::make_unique<term::Session>();
    session->onOutput = [liv, gen, this] {
        glaze::app().post([liv, gen, this] {
            if (!liv.expired())
                drain(gen);
        });
    };
    session->onExit = [liv, gen, this] {
        glaze::app().post([liv, gen, this] {
            if (!liv.expired())
                sessionEnded(gen);
        });
    };
    term::StartInfo info;
    info.program          = s.program;
    info.arguments        = s.arguments;
    info.workingDirectory = s.workDir;
    // Barnet ärver inte grafikkortsvalet GlazeUI satte i processen
    // (tomt utom på Linux) — borttagningarna FÖRST, så
    // att appens egna ändringar efter dem vinner.
    for (const std::string& namn : gfx::childEnvironmentRemovals())
        info.environment.push_back(term::EnvChange{namn, std::nullopt});
    info.environment.insert(info.environment.end(), s.env.begin(), s.env.end());
    info.columns          = g.cols;
    info.rows             = g.rows;
    if (!session->start(info)) {
        s.lastError = session->lastError();
        s.running   = false;
        gfx::log("Terminal(%s): start() — %s", name.c_str(), s.lastError.c_str());
        return false;
    }
    s.session   = std::move(session);
    s.running   = true;
    s.exitCode  = -1;
    s.lastError.clear();
    s.follow    = true;
    s.selected  = false;
    invalidate();
    return true;
}

void Terminal::kill()
{
    State& s = *s_;
    if (!s.session)
        return;
    s.session->kill();
    s.running  = false;
    s.exitCode = s.session->exitCode();
    invalidate();
}

void Terminal::write(std::string_view bytes)
{
    if (s_->session && s_->running)
        s_->session->write(bytes);
}

// INKLISTRINGEN — formen står vid `encodePaste` (keys.h).
void Terminal::paste(std::string_view text)
{
    s_->follow = true;
    write(term::encodePaste(text, s_->screen->modes().bracketedPaste));
}

// UTDATA UTAN PROGRAM — samma väg som pty:ns (tolkning, svar, invalidering).
void Terminal::feedOutput(std::string_view vt)
{
    State&     s       = *s_;
    const bool altFöre = s.screen->modes().altScreen;
    s.screen->feed(vt);
    const std::string svar = s.screen->takeReplies();
    if (!svar.empty() && s.session && s.running)
        s.session->write(svar);
    afterOutput(altFöre != s.screen->modes().altScreen);
}

// --------------------------------------------------------------- leveransen

// En hämtning ur sessionen: tolkad, svaren skrivna, invaliderad. `all` =
// töm inkorgen helt (programmets slut — utdatan före `onExit`).
// EN hämtning ur sessionen (högst `kTakeBudget`): tolkad, svaren skrivna,
// invaliderad. Svaret är om det finns mer.
bool Terminal::consume()
{
    State&            s       = *s_;
    bool              more    = false;
    const bool        altFöre = s.screen->modes().altScreen;
    const std::string b       = s.session->take(more);
    for (const std::string& n : s.session->takeNotices())
        gfx::log("Terminal(%s): %s", name.c_str(), n.c_str());
    if (!b.empty()) {
        s.screen->feed(b);
        const std::string svar = s.screen->takeReplies();
        if (!svar.empty())
            s.session->write(svar);
        afterOutput(altFöre != s.screen->modes().altScreen);
    }
    return more;
}

// Ett nytt varv i pump-svansen för samma session — `drain` eller `sessionEnded`.
void Terminal::repost(std::uint64_t gen, void (Terminal::*steg)(std::uint64_t))
{
    const std::weak_ptr<char> liv = lifeToken();
    glaze::app().post([liv, gen, steg, this] {
        if (!liv.expired())
            (this->*steg)(gen);
    });
}

void Terminal::drain(std::uint64_t gen)
{
    State& s = *s_;
    if (gen != s.generation || !s.session)
        return;
    if (consume())
        repost(gen, &Terminal::drain);
}

// Titelns händelse — uppskjuten medan 2026 håller ritningen,
// och bara för den session som satte den.
void Terminal::flushTitle()
{
    State& s = *s_;
    if (!s.titlePending || s.syncHeld)
        return;
    s.titlePending = false;
    postEvent([this, gen = s.generation] {
        if (gen == s_->generation)
            fireEvent(onTitleChange);
    });
}

// Efter tolkningen: titeln, 2026 och invalideringen.
void Terminal::afterOutput(bool screenSwitched)
{
    State&        s  = *s_;
    term::Screen& sc = *s.screen;
    if (screenSwitched)
        s.selected = false;   // markeringens rader hörde till den andra skärmen
    if (sc.titleChanged()) {
        s.title        = sc.title();
        s.titlePending = true;
    }
    // SYNKRONISERAD UTSKRIFT: medan läget är satt invalideras ingenting,
    // och timern tvingar fram en ritning om programmet aldrig släpper det.
    if (sc.modes().synchronized) {
        if (!s.syncHeld) {
            s.syncHeld = true;
            s.syncTimer.start();
        }
        return;
    }
    if (s.syncHeld) {
        s.syncHeld = false;
        s.syncTimer.stop();
        invalidate();
        flushTitle();
        return;
    }
    flushTitle();
    AppWindow* w = appWindow();
    if (!w || !w->isOpen())
        return;
    const Ctx c = behaviorCtx();
    if (sc.allDirty() || sc.historyChanged()) {
        invalidate();   // en rullning flyttar varje rad
        return;
    }
    const Grid       g  = grid(c);
    const gfx::Rectf r  = resolve(c);
    const float      sy = scrolledY(c);
    for (int i = 0; i < sc.rows(); ++i)
        if (sc.isDirty(i))
            invalidateRect(Draw::project(c, r, rowRect(g, sc.historySize() + i, sy)));
}

void Terminal::sessionEnded(std::uint64_t gen)
{
    State& s = *s_;
    if (gen != s.generation || !s.session)
        return;
    // Utdatan FÖRE slutet syns innan `onExit` fyras:
    // sessionen anmäler slutet först när läsaren nått EOF, och inkorgen töms
    // här med budgeten — ett varv i taget — innan händelsen.
    if (consume()) {
        repost(gen, &Terminal::sessionEnded);
        return;
    }
    s.running  = false;
    s.exitCode = s.session->exitCode();
    invalidate();
    postEvent([this, gen] {
        if (gen == s_->generation)
            fireEvent(onExit);
    });
}

// ------------------------------------------------------- historik, markering

void Terminal::setScrollback(int lines)
{
    if (lines < 0) {
        gfx::log("Terminal(%s): setScrollback(%d) — ett antal rader ≥ 0; ignoreras",
                 name.c_str(), lines);
        return;
    }
    s_->scrollback = lines;
    s_->screen->setScrollback(lines);
    invalidate();
}

int Terminal::scrollback() const { return s_->scrollback; }

void Terminal::clear()
{
    s_->screen->clearHistory();
    s_->selected = false;
    s_->follow   = true;
    invalidate();
}

void Terminal::scrollToBottom()
{
    s_->follow = true;
    invalidate();
}

bool Terminal::hasSelection() const { return s_->selected; }

void Terminal::clearSelection()
{
    if (!s_->selected)
        return;
    s_->selected = false;
    invalidate();
}

void Terminal::selectAll()
{
    State&             s  = *s_;
    const term::Screen& sc = *s.screen;
    const std::int64_t bas = static_cast<std::int64_t>(sc.linesDropped());
    s.mode     = State::Mode::Char;
    s.anchor   = State::Pos{bas, 0, 0};
    s.head     = State::Pos{bas + sc.historySize() + sc.rows() - 1, sc.columns(),
                            sc.columns() - 1};
    s.selected = true;
    invalidate();
}

namespace {

// Markeringens ändar som [start, slut) i (stabil rad, kolumngräns), efter
// läget — ordets och radens markering växer ut från cellerna.
struct Span {
    std::int64_t aLine, bLine;
    int          aCol, bCol;   // bCol kan vara kolumnantalet (radens slut)
};

int wordStart(const term::Line& l, int cell)
{
    const int n = static_cast<int>(l.cells.size());
    cell        = std::clamp(cell, 0, std::max(0, n - 1));
    while (cell > 0 && l.cells[static_cast<std::size_t>(cell)].width == 0)
        --cell;
    if (n == 0 || !isWordChar(l.cells[static_cast<std::size_t>(cell)].cp))
        return cell;
    while (cell > 0) {
        int p = cell - 1;
        while (p > 0 && l.cells[static_cast<std::size_t>(p)].width == 0)
            --p;
        if (!isWordChar(l.cells[static_cast<std::size_t>(p)].cp))
            break;
        cell = p;
    }
    return cell;
}

int wordEnd(const term::Line& l, int cell)
{
    const int n = static_cast<int>(l.cells.size());
    cell        = std::clamp(cell, 0, std::max(0, n - 1));
    if (n == 0 || !isWordChar(l.cells[static_cast<std::size_t>(cell)].cp))
        return cell + 1;
    while (cell + 1 < n && (l.cells[static_cast<std::size_t>(cell) + 1].width == 0 ||
                            isWordChar(l.cells[static_cast<std::size_t>(cell) + 1].cp)))
        ++cell;
    return cell + 1;
}

} // namespace

std::string Terminal::selectedText() const
{
    const State&        s  = *s_;
    const term::Screen& sc = *s.screen;
    if (!s.selected)
        return {};
    const auto bas   = static_cast<std::int64_t>(sc.linesDropped());
    const int  total = sc.historySize() + sc.rows();
    auto       index = [&](std::int64_t stabil) {
        return static_cast<int>(std::clamp<std::int64_t>(stabil - bas, 0, total - 1));
    };
    State::Pos a = s.anchor, b = s.head;
    const bool byCell = s.mode != State::Mode::Char;
    if (std::pair(b.line, byCell ? b.cell : b.col) < std::pair(a.line, byCell ? a.cell : a.col))
        std::swap(a, b);
    Span sp{a.line, b.line, a.col, b.col};
    if (s.mode == State::Mode::Word) {
        sp.aCol = wordStart(sc.line(index(a.line)), a.cell);
        sp.bCol = wordEnd(sc.line(index(b.line)), b.cell);
    } else if (s.mode == State::Mode::Line) {
        sp.aCol = 0;
        sp.bCol = std::numeric_limits<int>::max();
    }

    // Avslutande blanksteg per rad bort, mjukt brutna rader ihop.
    std::string ut;
    for (std::int64_t L = std::max(sp.aLine, bas); L <= sp.bLine && L - bas < total; ++L) {
        const term::Line& l  = sc.line(index(L));
        const int         n  = static_cast<int>(l.cells.size());
        const int         c0 = L == sp.aLine ? std::clamp(sp.aCol, 0, n) : 0;
        const int         c1 = L == sp.bLine ? std::clamp(sp.bCol, 0, n) : n;
        std::string       rad;
        for (int k = c0; k < c1; ++k)
            rad += l.text(k);
        const bool fortsätter = l.wrapped && L != sp.bLine;
        if (!fortsätter)
            while (!rad.empty() && rad.back() == ' ')
                rad.pop_back();
        ut += rad;
        if (!fortsätter && L != sp.bLine)
            ut += '\n';
    }
    return ut;
}

void Terminal::copy()
{
    if (s_->selected)
        gfx::setClipboardText(selectedText());
}

// ---------------------------------------------------------------- utseendet

void Terminal::setColorMode(TerminalColorMode m)
{
    if (s_->colorMode == m)
        return;
    s_->colorMode = m;
    invalidate();
}
TerminalColorMode Terminal::colorMode() const { return s_->colorMode; }

void Terminal::setColorScheme(TerminalScheme sc)
{
    if (s_->scheme == sc)
        return;
    s_->scheme = sc;
    invalidate();
}
TerminalScheme Terminal::colorScheme() const { return s_->scheme; }

void Terminal::setSchemeColor(ThemeMode variant, TerminalColor role, gfx::Color c)
{
    if (variant == ThemeMode::System) {
        gfx::log("Terminal(%s): setSchemeColor — ThemeMode::System är ingen variant "
                 "(Light eller Dark); ignoreras",
                 name.c_str());
        return;
    }
    s_->own[variant == ThemeMode::Dark ? 1 : 0][static_cast<std::size_t>(role)] = c;
    invalidate();
}

void Terminal::clearSchemeColors()
{
    for (auto& v : s_->own)
        v.fill(std::nullopt);
    invalidate();
}

void Terminal::setTextSize(Length size)
{
    if (size.unit == Unit::World || !std::isfinite(size.value) || size.value < 0.0f) {
        gfx::log("Terminal(%s): setTextSize(%g) — ett mått ≥ 0 i px eller mm "
                 "(0 = stilens); ignoreras",
                 name.c_str(), double(size.value));
        return;
    }
    s_->textSize = size.value == 0.0f ? Length{} : size;
    invalidate();
}
Length Terminal::textSize() const { return s_->textSize; }

void Terminal::setAltAsMeta(bool on) { s_->altAsMeta = on; }
bool Terminal::altAsMeta() const { return s_->altAsMeta; }

// Paletten: Auto läser den UPPLÖSTA varianten (`c.theme` — appens läge och,
// i System-läget, OS:ets), ett låst läge appens tema i den varianten.
TerminalPalette Terminal::palette() const
{
    const AppWindow* w = appWindow();
    return paletteWith(w && w->isOpen() ? *behaviorCtx().theme
                                        : defaultTheme(UI::Slate).light);
}

TerminalPalette Terminal::paletteWith(const Theme::Variant& resolved) const
{
    const AppWindow* w = appWindow();
    const Theme&     t = w ? w->theme() : defaultTheme(UI::Slate);
    const State&     s = *s_;
    const Theme::Variant* v    = &resolved;
    bool                  dark = false;
    switch (s.colorMode) {
    case TerminalColorMode::Light:
        v = &t.light;
        break;
    case TerminalColorMode::Dark:
        v    = &t.dark;
        dark = true;
        break;
    case TerminalColorMode::Auto:
    default:
        dark = terminalIsDark(*v);
        break;
    }
    TerminalPalette p = terminalPalette(s.scheme, dark, *v);
    for (int i = 0; i < kTerminalColorCount; ++i)
        if (const auto& egen = s.own[dark ? 1 : 0][static_cast<std::size_t>(i)])
            p.colors[static_cast<std::size_t>(i)] = *egen;
    return p;
}

bool Terminal::setProp(const std::string& key, const std::string& value)
{
    auto heltal = [&](int& ut) {
        char* slut = nullptr;
        const long v = std::strtol(value.c_str(), &slut, 10);
        if (value.empty() || *slut != '\0')
            return false;
        ut = static_cast<int>(v);
        return true;
    };
    if (key == "program") {
        setProgram(value);
        return true;
    }
    if (key == "arguments") {
        setArguments(value);
        return true;
    }
    if (key == "workingDirectory") {
        setWorkingDirectory(value);
        return true;
    }
    if (key == "colorMode") {
        if (value == "auto")
            setColorMode(TerminalColorMode::Auto);
        else if (value == "light")
            setColorMode(TerminalColorMode::Light);
        else if (value == "dark")
            setColorMode(TerminalColorMode::Dark);
        else
            return false;
        return true;
    }
    if (key == "colorScheme") {
        if (value == "theme")
            setColorScheme(TerminalScheme::Theme);
        else if (value == "solarized")
            setColorScheme(TerminalScheme::Solarized);
        else if (value == "one")
            setColorScheme(TerminalScheme::One);
        else if (value == "tango")
            setColorScheme(TerminalScheme::Tango);
        else
            return false;
        return true;
    }
    if (key == "scrollback") {
        int n = 0;
        if (!heltal(n))
            return false;
        setScrollback(n);
        return true;
    }
    if (key == "altAsMeta") {
        if (value != "true" && value != "false")
            return false;
        setAltAsMeta(value == "true");
        return true;
    }
    if (key == "textSize") {
        char*       slut = nullptr;
        const float v    = std::strtof(value.c_str(), &slut);
        if (value.empty() || *slut != '\0')
            return false;
        setTextSize(Length{v});
        return true;
    }
    return Panel::setProp(key, value);
}

// ------------------------------------------------------------- mätningen

Terminal::Grid Terminal::grid(const Ctx& c) const
{
    const auto& m = *c.style;
    Grid        g;
    for (int i = 0; i < 4; ++i)
        g.font[static_cast<std::size_t>(i)] =
            Draw::withFont(c, gfx::FontFamily::Mono, static_cast<gfx::FontStyle>(i),
                           s_->textSize);
    g.cw = std::max(Draw::measure(g.font[0], "M").w, 1.0f / c.scale);
    g.ch = std::max(Draw::lineHeight(g.font[0]), 1.0f / c.scale);
    // Rutnätet i innehållsytan minus fältets luft — till höger minst remsans
    // tjocklek, så kolumnantalet aldrig beror på om remsan syns.
    const gfx::Rectf in   = innerBounds(c);
    const float      padR = std::max(m.fieldPadX, m.scrollThick);
    g.area = gfx::Rectf{in.x + m.fieldPadX, in.y + m.fieldPadY,
                        std::max(0.0f, in.w - m.fieldPadX - padR),
                        std::max(0.0f, in.h - 2.0f * m.fieldPadY)};
    g.cols = std::max(1, static_cast<int>(g.area.w / g.cw));
    g.rows = std::max(1, static_cast<int>(g.area.h / g.ch));
    return g;
}

// Raden `index` (0 = historikens äldsta) i widgetens lokala logiska px, vid
// rullningen `sy` (`scrolledY`).
gfx::Rectf Terminal::rowRect(const Grid& g, int index, float sy) const
{
    return gfx::Rectf{g.area.x, g.area.y + static_cast<float>(index) * g.ch - sy,
                      static_cast<float>(g.cols) * g.cw, g.ch};
}

gfx::Sizef Terminal::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN (TextArea): AUTO-bredden `textAreaDefaultW`, AUTO-höjden
        // `textAreaRows` rader i Mono plus luften och ramen.
        gfx::Sizef s{width, height};
        if (s.w > 0.0f && s.h > 0.0f)
            return s;
        const auto& m = *c.style;
        if (s.w <= 0.0f)
            s.w = m.textAreaDefaultW;
        if (s.h <= 0.0f)
            s.h = static_cast<float>(m.textAreaRows) *
                      Draw::lineHeight(Draw::withFont(c, gfx::FontFamily::Mono,
                                                      gfx::FontStyle::Regular, s_->textSize)) +
                  2.0f * (m.fieldPadY + (isBorder() ? m.panelInset : 0.0f));
        return s;
    }
    }
}

// FYSISKA px — Panels kontrakt. Historiken plus EN vy: rullad till botten står
// skärmens första rad överst, precis som utan historik.
gfx::Sizef Terminal::contentExtent(const Ctx& c) const
{
    const gfx::Rectf in = innerBounds(c);
    const Grid       g  = grid(c);
    return gfx::Sizef{in.w * c.scale,
                      (static_cast<float>(s_->screen->historySize()) * g.ch + in.h) * c.scale};
}

// ---------------------------------------------------------------- ritningen

void Terminal::draw(const Ctx& c)
{
    Panel::draw(c);   // ramen — ytan målas med schemats bakgrund nedan
    State&        s  = *s_;
    term::Screen& sc = *s.screen;
    const auto&   m  = *c.style;
    const Grid    g  = grid(c);

    // STORLEKEN: skärmmodellen ändras direkt (ren data, krävs för att
    // rita rätt) och pty:ns storlek POSTAS — ingen OS-åtgärd mitt i ritningen.
    if (g.cols != sc.columns() || g.rows != sc.rows()) {
        sc.resize(g.cols, g.rows);
        if (s.session && s.running) {
            const std::weak_ptr<char> liv = lifeToken();
            glaze::app().post([liv, gen = s.generation, cols = g.cols, rows = g.rows, this] {
                if (!liv.expired() && gen == s_->generation && s_->session)
                    s_->session->resize(cols, rows);
            });
        }
    }
    if (s.altScreen != sc.modes().altScreen) {
        s.altScreen = sc.modes().altScreen;
        s.selected  = false;
    }
    // HISTORIKEN NÄR RINGEN ÄR FULL: de äldsta raderna
    // faller bort, och en uppåtrullad vy flyttas lika många rader upp så att
    // den står still över samma text.
    const std::uint64_t föll = sc.linesDropped() - s.droppedSeen;
    s.droppedSeen            = sc.linesDropped();
    if (föll > 0 && !s.follow)
        setScroll(scrollX(),
                  std::max(0.0f, scrollY() - static_cast<float>(föll) * g.ch));
    followIfNeeded(c);

    const TerminalPalette p = paletteWith(*c.theme);
    auto rgb = [](gfx::Color k) {
        return (std::uint32_t{k.r} << 16) | (std::uint32_t{k.g} << 8) | k.b;
    };
    sc.setReportedColors(rgb(p[TerminalColor::Foreground]), rgb(p[TerminalColor::Background]),
                         rgb(p[TerminalColor::Cursor]));

    const gfx::Rectf in = innerBounds(c);
    Draw::rect(c, in, innerRadius(c), p[TerminalColor::Background]);
    Draw::clip(c, in, innerRadius(c));

    // FYSISKA px-kanter på båda sidor: två grannytor delar exakt samma kant
    // och får aldrig en söm.
    auto snapX = [&](float x) { return (std::round(c.box.x + x * c.scale) - c.box.x) / c.scale; };
    auto snapY = [&](float y) { return (std::round(c.box.y + y * c.scale) - c.box.y) / c.scale; };

    // Markeringens ändar (selectedText:s regel) — per rad nedan.
    const auto bas  = static_cast<std::int64_t>(sc.linesDropped());
    const int  hist = sc.historySize();
    const int  total = hist + sc.rows();
    std::optional<Span> sel;
    if (s.selected) {
        State::Pos a = s.anchor, b = s.head;
        const bool byCell = s.mode != State::Mode::Char;
        if (std::pair(b.line, byCell ? b.cell : b.col) < std::pair(a.line, byCell ? a.cell : a.col))
            std::swap(a, b);
        Span sp{a.line, b.line, a.col, b.col};
        auto index = [&](std::int64_t L) {
            return static_cast<int>(std::clamp<std::int64_t>(L - bas, 0, total - 1));
        };
        if (s.mode == State::Mode::Word) {
            sp.aCol = wordStart(sc.line(index(a.line)), a.cell);
            sp.bCol = wordEnd(sc.line(index(b.line)), b.cell);
        } else if (s.mode == State::Mode::Line) {
            sp.aCol = 0;
            sp.bCol = std::numeric_limits<int>::max();
        }
        sel = sp;
    }

    const float sy    = scrolledY(c);
    const int   first = std::max(0, static_cast<int>(std::floor(sy / g.ch)));
    const int   last  = std::min(total - 1, static_cast<int>((sy + g.area.h) / g.ch));
    const float asc   = Draw::ascent(g.font[0]);
    for (int i = first; i <= last; ++i) {
        const term::Line& l  = sc.line(i);
        const gfx::Rectf  rr = rowRect(g, i, sy);
        const int         n  = std::min(static_cast<int>(l.cells.size()), g.cols);
        const std::int64_t L = bas + i;
        auto valt = [&](int k) {
            return sel && L >= sel->aLine && L <= sel->bLine &&
                   (L != sel->aLine || k >= sel->aCol) && (L != sel->bLine || k < sel->bCol);
        };
        // YTORNA — sammanhängande celler med samma bakgrund i ETT anrop.
        for (int k = 0; k < n;) {
            const gfx::Color bg = lookOf(l.cells[static_cast<std::size_t>(k)], p, valt(k)).bg;
            int              e  = k + 1;
            while (e < n && lookOf(l.cells[static_cast<std::size_t>(e)], p, valt(e)).bg == bg)
                ++e;
            if (bg != p[TerminalColor::Background]) {
                const float x0 = snapX(rr.x + static_cast<float>(k) * g.cw);
                const float x1 = snapX(rr.x + static_cast<float>(e) * g.cw);
                const float y0 = snapY(rr.y), y1 = snapY(rr.y + g.ch);
                Draw::rect(c, {x0, y0, x1 - x0, y1 - y0}, 0.0f, bg);
            }
            k = e;
        }
        // TEXTEN — en följd ASCII-celler med samma utseende i ETT anrop
        // (mätningen är additiv, så den landar på rutnätet); allt annat per
        // cell, centrerat över sin bredd.
        for (int k = 0; k < n;) {
            const term::Cell& cell = l.cells[static_cast<std::size_t>(k)];
            const Look        lk   = lookOf(cell, p, valt(k));
            int               e    = k + 1;
            std::string       run;
            const bool        ascii = cell.width == 1 && !cell.more && cell.cp < 0x80;
            if (ascii) {
                run += static_cast<char>(cell.cp);
                while (e < n) {
                    const term::Cell& nx = l.cells[static_cast<std::size_t>(e)];
                    if (nx.width != 1 || nx.more || nx.cp >= 0x80 || !(lookOf(nx, p, valt(e)) == lk))
                        break;
                    run += static_cast<char>(nx.cp);
                    ++e;
                }
            } else if (cell.width == 0) {
                k = e;
                continue;   // fortsättningen — ritad med sin början
            } else {
                run = l.text(k);
                e   = k + cell.width;
            }
            const gfx::Rectf r{rr.x + static_cast<float>(k) * g.cw, rr.y,
                               static_cast<float>(e - k) * g.cw, g.ch};
            const Ctx& fc = g.font[static_cast<std::size_t>(styleOf(lk.attrs))];
            if (run.find_first_not_of(' ') != std::string::npos)
                Draw::text(fc, r, run, lk.fg, ascii ? Align::Left : Align::Center);
            if (lk.attrs & term::attr::kUnderline)
                Draw::line(c, r.x, r.y + asc + m.linkGap, r.x + r.w, r.y + asc + m.linkGap,
                           Pen{lk.fg});
            if (lk.attrs & term::attr::kStrike)
                Draw::line(c, r.x, r.y + g.ch / 2.0f, r.x + r.w, r.y + g.ch / 2.0f, Pen{lk.fg});
            k = e;
        }
    }

    // MARKÖREN: block, understreck eller stapel; blinket ur fönstrets
    // caret-timer och bara när programmet bett om det; utan fokus en ihålig
    // ram, med DECTCEM av ingen alls.
    const term::Modes& md   = sc.modes();
    const int          cAbs = hist + sc.cursorRow();
    if (md.cursorVisible && cAbs >= first && cAbs <= last) {
        const term::Line& l     = sc.line(cAbs);
        const int         col   = std::min(sc.cursorCol(), g.cols - 1);
        const bool        bred  = col < static_cast<int>(l.cells.size()) &&
                          l.cells[static_cast<std::size_t>(col)].width == 2;
        const gfx::Rectf  rr    = rowRect(g, cAbs, sy);
        const gfx::Rectf  cell{rr.x + static_cast<float>(col) * g.cw, rr.y,
                              (bred ? 2.0f : 1.0f) * g.cw, g.ch};
        const bool        fokus = focused() && appWindow() && appWindow()->isActive();
        const float       bw    = std::max(m.caretWidth, 1.0f / c.scale);
        const gfx::Color  mk    = p[TerminalColor::Cursor];
        if (!fokus) {
            Draw::frame(c, cell, 0.0f, Pen{mk});
        } else if (!md.cursorBlink || s.caretOn) {
            switch (md.cursorShape) {
            case term::CursorShape::Underline:
                Draw::rect(c, {cell.x, cell.y + cell.h - bw, cell.w, bw}, 0.0f, mk);
                break;
            case term::CursorShape::Bar:
                Draw::rect(c, {cell.x, cell.y, bw, cell.h}, 0.0f, mk);
                break;
            case term::CursorShape::Block:
            default: {
                Draw::rect(c, cell, 0.0f, mk);
                const std::string t = col < static_cast<int>(l.cells.size()) ? l.text(col) : "";
                if (!t.empty() && t != " ")
                    Draw::text(g.font[static_cast<std::size_t>(
                                   styleOf(l.cells[static_cast<std::size_t>(col)].attrs))],
                               cell, t, p[TerminalColor::CursorText], Align::Center);
                break;
            }
            }
        }
    }
    Draw::endClip(c);

    // Fokusringen UTANFÖR ramen, med stilens mått (fältens form).
    if (focused() && isEnabled())
        Draw::frame(c, Draw::inset(c.bounds(), -m.focusOffset), m.cornerRadius + m.focusOffset,
                    Pen{c.theme->colors.accent, m.focusWidth});
    sc.clearDirty();
}

// Markörens rad — blinket och fokusbytet ritar bara den.
void Terminal::invalidateCursor()
{
    AppWindow* w = appWindow();
    if (!w || !w->isOpen())
        return;
    const Ctx           c  = behaviorCtx();
    const Grid          g  = grid(c);
    const term::Screen& sc = *s_->screen;
    invalidateRect(Draw::project(c, resolve(c),
                                 rowRect(g, sc.historySize() + sc.cursorRow(), scrolledY(c))));
}

// ------------------------------------------------------------- rullningen

// Rullningen KLAMPAD mot spelrummet — det lagrade värdet kan ligga bortom det
// efter att historiken krympt.
float Terminal::scrolledY(const Ctx& c) const
{
    return std::clamp(scrollY(), 0.0f, scrollRange(c).h);
}

void Terminal::followIfNeeded(const Ctx& c)
{
    if (!s_->follow)
        return;
    const float range = scrollRange(c).h;
    if (scrollY() != range)
        setScroll(0.0f, range);
}

// Användaren rullade: följningen gäller igen först vid botten.
void Terminal::noteUserScroll(const Ctx& c)
{
    const float range = scrollRange(c).h;
    s_->follow        = std::clamp(scrollY(), 0.0f, range) >= range - grid(c).ch / 2.0f;
}

// ---------------------------------------------------------------- tangenterna

bool Terminal::keyDown(gfx::Key k, std::uint32_t mods)
{
    State& s      = *s_;
    s.swallowText = false;
    switch (term::clipboardKey(k, mods)) {
    case term::ClipboardKey::Copy:
        copy();
        return true;
    case term::ClipboardKey::Paste:
        paste(gfx::clipboardText());
        return true;
    case term::ClipboardKey::None:
        break;
    }
    if (!s.session || !s.running)
        return false;   // inget program — tangenterna är fönstrets (Tab flyttar fokus)
    const term::Modes&  md = s.screen->modes();
    const term::KeyModes km{md.cursorKeys, md.newline, s.screen->keyboardFlags(), s.altAsMeta};
    const std::string    b = term::encodeKey(k, mods, km);
    if (b.empty())
        return false;
    // Alt som Meta tog trycket: tecknet OS:et ändå skickar (Option+x på
    // macOS) är inte text.
    s.swallowText = (mods & gfx::kModAlt) != 0;
    s.follow      = true;
    s.session->write(b);
    return true;
}

bool Terminal::textInput(const char* utf8)
{
    State& s = *s_;
    if (s.swallowText) {
        s.swallowText = false;
        return true;
    }
    if (!s.session || !s.running)
        return false;
    s.follow = true;
    s.session->write(utf8);
    return true;
}

// FOKUSHÄNDELSERNA (1004): widgetfokus OCH fönstrets aktivering.
// Fönstret anropar caret-kroken vid varje aktiveringsbyte, så den räcker som
// väg — inget nytt publikt krokbehov.
void Terminal::syncFocusReport()
{
    State&     s  = *s_;
    const bool nu = focused() && appWindow() && appWindow()->isActive();
    if (nu == s.focusReported)
        return;
    s.focusReported = nu;
    if (s.running && s.screen->modes().focusEvents)
        s.session->write(nu ? "\x1b[I" : "\x1b[O");
}

void Terminal::caretBlink(const Ctx&, bool on)
{
    const bool byte = s_->caretOn != on;
    s_->caretOn     = on;
    const bool före = s_->focusReported;
    syncFocusReport();
    if ((byte && s_->screen->modes().cursorBlink) || före != s_->focusReported)
        invalidateCursor();
}

void Terminal::focusGained()
{
    s_->caretOn = true;
    syncFocusReport();
    invalidate();   // ringen och den fyllda markören
}

void Terminal::focusLost()
{
    syncFocusReport();
    invalidate();
}

// ------------------------------------------------------------------- musen

// MUSRAPPORTERINGEN: 1000/1002/1003 med SGR-kodningen 1006, annars
// X10-formen upp till kolumn och rad 223. true = rapporterad (eller tagen).
bool Terminal::reportMouse(const Ctx& c, float x, float y, int button, bool press,
                           bool motion)
{
    State&             s  = *s_;
    const term::Modes& md = s.screen->modes();
    if (md.mouse == term::MouseMode::None || !s.running)
        return false;
    if (motion && (md.mouse == term::MouseMode::Press ||
                   (md.mouse == term::MouseMode::Drag && s.mouseButton < 0)))
        return true;
    const Grid       g  = grid(c);
    const gfx::Rectf r  = resolve(c);
    const float      lx = Draw::localX(c, r, x) - g.area.x;
    const float      ly = Draw::localY(c, r, y) - g.area.y;
    const int col = std::clamp(static_cast<int>(std::floor(lx / g.cw)), 0, g.cols - 1) + 1;
    const int row = std::clamp(static_cast<int>(std::floor(ly / g.ch)), 0, g.rows - 1) + 1;
    if (motion && col == s.mouseCol && row == s.mouseRow)
        return true;   // samma cell — ingen ny rapport
    s.mouseCol = col;
    s.mouseRow = row;
    const std::uint32_t mods = appWindow() ? appWindow()->modifierKeys() : 0;
    int cb = button + (motion ? 32 : 0) + ((mods & gfx::kModShift) ? 4 : 0) +
             ((mods & gfx::kModAlt) ? 8 : 0) + ((mods & gfx::kModCtrl) ? 16 : 0);
    std::string ut;
    if (md.mouseSgr) {
        ut = "\x1b[<" + std::to_string(cb) + ";" + std::to_string(col) + ";" +
             std::to_string(row) + (press || motion ? "M" : "m");
    } else {
        if (!press && !motion)
            cb = (cb & ~3) | 3;   // X10:s släpp bär ingen knapp
        if (col > 223 || row > 223)
            return true;          // bortom formens räckvidd — tas, rapporteras inte
        ut = std::string("\x1b[M") + static_cast<char>(32 + cb) + static_cast<char>(32 + col) +
             static_cast<char>(32 + row);
    }
    s.session->write(ut);
    return true;
}

void Terminal::mouseDown(const Ctx& c, float x, float y)
{
    State& s = *s_;
    Panel::mouseDown(c, x, y);   // remsan — nollar också sitt drag per tryck
    if (inScrollStrip(c, x, y)) {
        noteUserScroll(c);       // ett rännklick rullar — följningen omprövas
        return;
    }
    const std::uint32_t mods = appWindow() ? appWindow()->modifierKeys() : 0;
    // Med musrapportering markerar Shift+drag ändå (konventionen).
    if (!(mods & gfx::kModShift) && reportMouse(c, x, y, 0, true, false)) {
        s.mouseButton = 0;
        return;
    }
    const Grid          g  = grid(c);
    const term::Screen& sc = *s.screen;
    const gfx::Rectf    r  = resolve(c);
    const float         sy = scrolledY(c);
    const float lx = Draw::localX(c, r, x) - g.area.x;
    const float ly = Draw::localY(c, r, y) - g.area.y + sy;
    const int   rad = std::clamp(static_cast<int>(std::floor(ly / g.ch)), 0,
                                 sc.historySize() + sc.rows() - 1);
    const State::Pos p{static_cast<std::int64_t>(sc.linesDropped()) + rad,
                       std::clamp(static_cast<int>(std::lround(lx / g.cw)), 0, g.cols),
                       std::clamp(static_cast<int>(std::floor(lx / g.cw)), 0, g.cols - 1)};
    const auto nu = Clock::now();
    if (s.dblLine == p.line &&
        nu - s.dblTime <= std::chrono::milliseconds(gfx::doubleClickMs())) {
        s.mode     = State::Mode::Line;   // TRIPPELKLICKET — raden
        s.anchor   = s.head = p;
        s.selected = true;
        s.dblLine  = -1;
    } else if ((mods & gfx::kModShift) && s.selected) {
        s.head = p;                       // Shift+klick utvidgar
    } else {
        s.mode     = State::Mode::Char;
        s.anchor   = s.head = p;
        s.selected = false;
    }
    s.selecting = true;
    invalidate();
}

void Terminal::mouseDrag(const Ctx& c, float x, float y)
{
    State& s = *s_;
    Panel::mouseDrag(c, x, y);   // remsans tumme
    noteUserScroll(c);
    if (s.mouseButton >= 0) {
        reportMouse(c, x, y, s.mouseButton, true, true);
        return;
    }
    if (!s.selecting)
        return;
    const Grid          g  = grid(c);
    const term::Screen& sc = *s.screen;
    const gfx::Rectf    r  = resolve(c);
    const float         ly0 = Draw::localY(c, r, y);
    // Ett drag förbi kanten rullar en rad i taget.
    if (ly0 < g.area.y || ly0 > g.area.y + g.area.h) {
        setScroll(0.0f, std::clamp(scrollY() + (ly0 < g.area.y ? -g.ch : g.ch), 0.0f,
                                   scrollRange(c).h));
        noteUserScroll(c);
    }
    const float sy  = scrolledY(c);
    const float lx  = Draw::localX(c, r, x) - g.area.x;
    const float ly  = ly0 - g.area.y + sy;
    const int   rad = std::clamp(static_cast<int>(std::floor(ly / g.ch)), 0,
                                 sc.historySize() + sc.rows() - 1);
    s.head = State::Pos{static_cast<std::int64_t>(sc.linesDropped()) + rad,
                        std::clamp(static_cast<int>(std::lround(lx / g.cw)), 0, g.cols),
                        std::clamp(static_cast<int>(std::floor(lx / g.cw)), 0, g.cols - 1)};
    s.selected = s.mode != State::Mode::Char || s.head.line != s.anchor.line ||
                 s.head.col != s.anchor.col;
    invalidate();
}

void Terminal::mouseMove(const Ctx& c, float x, float y)
{
    Panel::mouseMove(c, x, y);
    if (s_->screen->modes().mouse == term::MouseMode::Any && s_->mouseButton < 0 &&
        !inScrollStrip(c, x, y))
        reportMouse(c, x, y, 3, true, true);   // rörelse utan knapp
}

bool Terminal::mouseUp(const Ctx& c, float x, float y)
{
    State& s = *s_;
    if (s.mouseButton >= 0) {
        reportMouse(c, x, y, s.mouseButton, false, false);
        s.mouseButton = -1;
    }
    s.selecting = false;
    return false;
}

bool Terminal::mouseCancel(gfx::MouseButton)
{
    s_->selecting   = false;
    s_->mouseButton = -1;
    return false;
}

void Terminal::mouseDoubleClick(const Ctx& c, float x, float y)
{
    State& s = *s_;
    if (s.mouseButton >= 0 || inScrollStrip(c, x, y))
        return;
    s.mode     = State::Mode::Word;   // ORDET — trycket satte redan ankaret
    s.head     = s.anchor;
    s.selected = true;
    s.dblTime  = Clock::now();
    s.dblLine  = s.anchor.line;
    invalidate();
}

bool Terminal::middleDown(const Ctx& c, float x, float y)
{
    if (!reportMouse(c, x, y, 1, true, false))
        return false;
    s_->mouseButton = 1;
    return true;
}

void Terminal::middleDrag(const Ctx& c, float x, float y)
{
    if (s_->mouseButton == 1)
        reportMouse(c, x, y, 1, true, true);
}

bool Terminal::middleUp(const Ctx& c, float x, float y)
{
    if (s_->mouseButton != 1)
        return false;
    reportMouse(c, x, y, 1, false, false);
    s_->mouseButton = -1;
    return false;
}

// HJULET: med musrapportering knapparna 64/65; i den alternativa skärmen
// (1007) pilar; annars historiken, och då gäller följningens regel.
bool Terminal::mouseWheel(const Ctx& c, float x, float y, float wheelX, float wheelY)
{
    State&             s  = *s_;
    const term::Modes& md = s.screen->modes();
    if (s.running && (md.mouse != term::MouseMode::None || (md.altScreen && md.altScroll))) {
        s.wheelAcc += wheelNotches(c, wheelY);
        const int steg = static_cast<int>(s.wheelAcc);
        s.wheelAcc -= static_cast<float>(steg);
        for (int i = 0; i < std::abs(steg); ++i) {
            if (md.mouse != term::MouseMode::None) {
                reportMouse(c, x, y, steg > 0 ? 64 : 65, true, false);
            } else {
                const term::KeyModes km{md.cursorKeys, md.newline, s.screen->keyboardFlags(),
                                        s.altAsMeta};
                s.session->write(term::encodeKey(steg > 0 ? gfx::Key::Up : gfx::Key::Down, 0, km));
            }
        }
        return true;
    }
    const bool tog = Panel::mouseWheel(c, x, y, wheelX, wheelY);
    noteUserScroll(c);
    return tog;
}

gfx::PointerShape Terminal::cursorAt(const Ctx& c, float x, float y) const
{
    if (inScrollStrip(c, x, y) || s_->screen->modes().mouse != term::MouseMode::None)
        return Widget::cursorAt(c, x, y);
    return gfx::PointerShape::IBeam;
}

} // namespace glaze
