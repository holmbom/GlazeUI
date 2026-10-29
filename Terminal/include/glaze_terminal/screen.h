#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "glaze_terminal/parser.h"

// SKÄRMMODELLEN — det en terminal VISAR, som ren data:
// rutnätet av celler, markören, lägena, historiken och svaren till
// programmet. Ren std och headless (terminal_gate P och H); widgeten
// `glaze::Terminal` ritar den och kopplar den till en pty.
//
// TVÅ BUFFERTAR: den primära med HISTORIK och den alternativa (`CSI ?1049h`
// — vim, less, htop) utan. Historiken gäller bara den primära, hålls i en
// deque av rader (ringens form: de äldsta faller bort först) med taket
// `setScrollback` (förval 10 000), och syns inte medan den alternativa är
// uppe — xterms beteende.
//
// RADINDEXEN. `line(i)` räknar från historikens äldsta rad: 0 … historySize()
// − 1 är historiken och historySize() … historySize() + rows() − 1 skärmen.
// En rad som faller ur historiken stegar `linesDropped()`, så
// `linesDropped() + i` är ett STABILT index för en rad (markeringen läser
// det).
//
// KLUSTREN. En cell bär ett GRAFEMKLUSTER: den första
// kodpunkten i cellen och resten — kombinerande tecken, ZWJ-följder, VS16,
// hudtoner, flaggans andra indikator — i radens överflödstabell. Ett brett
// kluster tar två celler, den andra en FORTSÄTTNING (`width == 0`). Ett
// brett tecken som inte ryms i sista kolumnen bryter raden först.
//
// STORLEKSÄNDRING: ingen omflödning (xterms beteende) — raderna kapas
// eller fylls, och markören klämms. Krymper skärmen flyttas raderna ovanför
// markören till historiken, så den står kvar på samma rad i texten.
//
// SVAREN (DSR, DA, DECRQM, OSC 10/11/12-frågorna, kittyprotokollets fråga)
// samlas i `takeReplies()` — widgeten skriver dem till pty:n. Färgfrågorna
// besvaras ur `setReportedColors`, som widgeten matar ur sitt schema.

namespace glaze::term {

// En cells färg, packad: förvalet, ett index i 256-paletten eller RGB.
struct Color {
    enum class Kind : std::uint8_t { Default, Indexed, Rgb };
    Kind          kind = Kind::Default;
    std::uint8_t  r = 0, g = 0, b = 0;   // Indexed: r = index

    static Color indexed(int i)
    {
        Color c;
        c.kind = Kind::Indexed;
        c.r    = static_cast<std::uint8_t>(i);
        return c;
    }
    static Color rgb(int r, int g, int b)
    {
        Color c;
        c.kind = Kind::Rgb;
        c.r    = static_cast<std::uint8_t>(r);
        c.g    = static_cast<std::uint8_t>(g);
        c.b    = static_cast<std::uint8_t>(b);
        return c;
    }
    bool operator==(const Color&) const = default;
};

// SGR-flaggorna.
namespace attr {
inline constexpr std::uint16_t kBold      = 1u << 0;
inline constexpr std::uint16_t kDim       = 1u << 1;
inline constexpr std::uint16_t kItalic    = 1u << 2;
inline constexpr std::uint16_t kUnderline = 1u << 3;
inline constexpr std::uint16_t kBlink     = 1u << 4;
inline constexpr std::uint16_t kInverse   = 1u << 5;
inline constexpr std::uint16_t kHidden    = 1u << 6;
inline constexpr std::uint16_t kStrike    = 1u << 7;
} // namespace attr

struct Cell {
    char32_t      cp    = U' ';   // klustrets första kodpunkt (0 i en fortsättning)
    Color         fg;
    Color         bg;
    std::uint16_t attrs = 0;
    std::uint8_t  width = 1;      // 1, 2 = brett, 0 = fortsättning
    bool          more  = false;  // klustret har fler kodpunkter (radens `extra`)
    bool operator==(const Cell&) const = default;
};

struct Line {
    std::vector<Cell> cells;
    bool              wrapped = false;   // MJUK brytning efter raden (kopieringen fogar ihop)
    // Överflödet: kolumn → klustrets kodpunkter EFTER den första.
    std::vector<std::pair<int, std::u32string>> extra;

    // Klustret i kolumn `col` som UTF-8 (tomt i en fortsättning).
    std::string text(int col) const;
};

enum class CursorShape : std::uint8_t { Block, Underline, Bar };
enum class MouseMode : std::uint8_t { None, Press, Drag, Any };   // 1000, 1002, 1003

struct Modes {
    bool        cursorKeys     = false;   // DECCKM (1)
    bool        keypadApp      = false;   // DECKPAM
    bool        origin         = false;   // DECOM (6)
    bool        autoWrap       = true;    // DECAWM (7)
    bool        cursorBlink    = false;   // 12 / DECSCUSR
    bool        cursorVisible  = true;    // DECTCEM (25)
    bool        insert         = false;   // IRM (4)
    bool        newline        = false;   // LNM (20)
    bool        focusEvents    = false;   // 1004
    bool        bracketedPaste = false;   // 2004
    bool        synchronized   = false;   // 2026
    // 1007 — hjulet i den alternativa skärmen blir pilar. FÖRVAL PÅ, som i
    // Windows Terminal och VS Code: less och man rullar då med hjulet.
    bool        altScroll      = true;
    bool        altScreen      = false;   // 47/1047/1049
    MouseMode   mouse          = MouseMode::None;
    bool        mouseSgr       = false;   // 1006
    CursorShape cursorShape    = CursorShape::Block;
};

class Screen final : private ParserSink {
public:
    static constexpr int kDefaultScrollback = 10000;
    // VT100:ans mått — skärmen innan den första ritningen gett den sin storlek.
    static constexpr int kDefaultColumns = 80;
    static constexpr int kDefaultRows    = 24;
    // SYNKRONISERAD UTSKRIFT (2026): den längsta tid läget får hålla tillbaka
    // ritningen innan ägaren ritar ändå — ett program som aldrig släpper det
    // ska inte frysa vyn.
    static constexpr int kSyncTimeoutMs = 150;

    Screen(int columns, int rows, int scrollback = kDefaultScrollback);
    Screen(const Screen&)            = delete;
    Screen& operator=(const Screen&) = delete;

    // Programmets utdata — en del av strömmen, delad var som helst.
    void feed(std::string_view bytes);

    // Ny storlek (≥ 1 × 1). Se filhuvudet: ingen omflödning.
    void resize(int columns, int rows);
    int  columns() const { return cols_; }
    int  rows() const { return rows_; }

    // Historiken (0 medan den alternativa skärmen är uppe) och raderna.
    int         historySize() const;
    const Line& line(int index) const;   // 0 … historySize() + rows() − 1
    std::uint64_t linesDropped() const { return dropped_; }

    void setScrollback(int lines);   // < 0 klampas till 0
    int  scrollback() const { return scrollback_; }
    void clearHistory();             // ED 3:s verkan — skärmen står kvar

    // Markören, i skärmens rader och kolumner (0-baserat). `cursorCol` kan
    // vara `columns()` − 1 med en väntande radbrytning.
    int          cursorRow() const { return cur_.row; }
    int          cursorCol() const { return cur_.col; }
    const Modes& modes() const { return modes_; }
    int          keyboardFlags() const;   // kittyprotokollet, aktuell skärm

    const std::string& title() const { return title_; }
    const std::string& workingDirectory() const { return cwd_; }   // OSC 7

    // Svaren till programmet sedan förra frågan.
    std::string takeReplies();

    // Färgerna OSC 10/11/12 svarar med (RGB, 8 bitar per kanal).
    void setReportedColors(std::uint32_t fg, std::uint32_t bg, std::uint32_t cursor);

    // SMUTSEN — vad som ändrats sedan `clearDirty`. `allDirty` efter en
    // rullning, en skärmväxling, en storleksändring och RIS; annars per rad.
    bool isDirty(int screenRow) const;
    bool allDirty() const { return allDirty_; }
    bool historyChanged() const { return historyChanged_; }
    bool titleChanged() const { return titleChanged_; }
    void clearDirty();

private:
    struct Cursor {
        int           row = 0, col = 0;
        bool          pendingWrap = false;
        Color         fg, bg;
        std::uint16_t attrs = 0;
        bool          origin = false;   // DECOM — sparas av DECSC
        bool          g0Graphics = false, g1Graphics = false, shiftOut = false;
    };
    struct Buffer {
        std::vector<Line> lines;   // rows_ rader
        Cursor            saved;
        std::vector<int>  kittyStack;   // kittyprotokollets flaggor, toppen sist
    };

    // ParserSink
    void print(char32_t cp) override;
    void execute(std::uint8_t c) override;
    void escDispatch(std::string_view inter, char final) override;
    void csiDispatch(char prefix, const CsiParams& p, std::string_view inter,
                     char final) override;
    void oscDispatch(std::string_view text, bool bel) override;

    Buffer&       buf() { return modes_.altScreen ? alt_ : main_; }
    const Buffer& buf() const { return modes_.altScreen ? alt_ : main_; }
    Line&         row(int r) { return buf().lines[static_cast<std::size_t>(r)]; }
    Cell          blank() const;   // tom cell i aktuell bakgrund
    Line          blankLine() const;
    void          dirty(int r);
    void          dirtyAll() { allDirty_ = true; }

    void lineFeed();
    void reverseIndex();
    void carriageReturn();
    // Innehållet i [top, bottom] uppåt; `toHistory` = de bortrullade raderna
    // går till historiken (bara den primära skärmen, bara med topp 0).
    void scrollUp(int top, int bottom, int n, bool toHistory);
    void scrollDown(int top, int bottom, int n);
    void moveTo(int row, int col);                 // med DECOM
    void clampCursor();
    int  regionTop() const { return modes_.origin ? top_ : 0; }
    int  regionBottom() const { return modes_.origin ? bottom_ : rows_ - 1; }

    void eraseCells(int r, int from, int to);      // [from, to)
    void eraseInDisplay(int mode);
    void eraseInLine(int mode);
    void insertChars(int n);
    void deleteChars(int n);
    void insertLines(int n);
    void deleteLines(int n);
    void fixWide(Line& l, int col);                // en halv bred cell blir tom
    void attachToCluster(char32_t cp);
    bool joinsCluster(char32_t cp) const;          // ZWJ-följd, hudton, flagga

    void setMode(char prefix, int mode, bool on);
    int  modeState(char prefix, int mode) const;   // DECRQM: 1 satt, 2 av, 0 okänd
    void clearBuffer(Buffer& b);
    void saveCursor();
    void restoreCursor();
    void sgr(const CsiParams& p);
    void fullReset();
    void reply(std::string s) { replies_ += s; }
    void pushHistory(Line l);

    Parser parser_{*this};

    int cols_;
    int rows_;
    int scrollback_;

    Buffer           main_;
    Buffer           alt_;
    std::deque<Line> history_;
    std::uint64_t    dropped_ = 0;

    Cursor            cur_;
    Modes             modes_;
    int               top_ = 0, bottom_ = 0;   // DECSTBM, inklusive
    std::vector<bool> tabs_;
    char32_t          lastPrinted_ = 0;   // REP
    int               lastRow_ = -1, lastCol_ = -1;   // senaste klustret

    std::string   title_;
    std::string   cwd_;
    std::string   replies_;
    std::uint32_t reportFg_ = 0xFFFFFF, reportBg_ = 0, reportCursor_ = 0xFFFFFF;

    std::vector<bool> dirty_;
    bool              allDirty_       = true;
    bool              historyChanged_ = false;
    bool              titleChanged_   = false;
};

} // namespace glaze::term
