// Skärmmodellen — kontraktet i screen.h.
// Varje sekvens gör det xterm gör (ctlseqs), utom där en rad säger annat.

#include "glaze_terminal/screen.h"

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "glaze_terminal/text_width.h"

namespace glaze::term {
namespace {

// DEC SPECIAL GRAPHICS (`ESC ( 0`) — 0x5F–0x7E som linjegrafik. Tabellen är
// VT100:ans; mc, dialog och äldre ncurses ritar ramar med den.
constexpr char32_t kDecGraphics[] = {
    0x00A0, 0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0,
    0x00B1, 0x2424, 0x240B, 0x2518, 0x2510, 0x250C, 0x2514, 0x253C,
    0x23BA, 0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534,
    0x252C, 0x2502, 0x2264, 0x2265, 0x03C0, 0x2260, 0x00A3, 0x00B7,
};

// Kittyprotokollets flaggor vi stöder: bara 1, "disambiguate".
// Frågan svarar med de flaggor som faktiskt gäller, så ett program som bett om
// fler ser att de inte fick dem. Stacken har ett tak, som kittys egen.
constexpr int kKittySupported = 1;
constexpr std::size_t kKittyStackMax = 16;

// TAKET för ett grafemkluster: så många
// kodpunkter, basen inräknad. Ett program som skickar tusentals kombinerande
// tecken efter samma bas får inte låta en cell växa utan gräns — överflödet
// sväljs, utan ny allokering.
constexpr std::size_t kClusterMax = 32;

// OSC-numrets tak: kända nummer är små, och ett längre tal är
// ingenting vi tolkar — det ignoreras i stället för att räknas över.
constexpr int kOscNumberMax = 65535;

bool isRegional(char32_t c) { return c >= 0x1F1E6 && c <= 0x1F1FF; }
bool isSkinTone(char32_t c) { return c >= 0x1F3FB && c <= 0x1F3FF; }

void appendUtf8(std::string& s, char32_t cp)
{
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::u32string* extraAt(Line& l, int col)
{
    for (auto& [k, s] : l.extra)
        if (k == col)
            return &s;
    return nullptr;
}

void dropExtra(Line& l, int col)
{
    l.extra.erase(std::remove_if(l.extra.begin(), l.extra.end(),
                                 [&](const auto& p) { return p.first == col; }),
                  l.extra.end());
}

// Överflödets nycklar flyttas `d` kolumner från och med `from`; det som
// hamnar utanför [0, cols) försvinner.
void shiftExtra(Line& l, int from, int d, int cols)
{
    for (auto& p : l.extra)
        if (p.first >= from)
            p.first += d;
    l.extra.erase(std::remove_if(l.extra.begin(), l.extra.end(),
                                 [&](const auto& p) {
                                     return p.first < 0 || p.first >= cols;
                                 }),
                  l.extra.end());
}

int clampInt(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

} // namespace

std::string Line::text(int col) const
{
    std::string s;
    if (col < 0 || col >= static_cast<int>(cells.size()))
        return s;
    const Cell& c = cells[static_cast<std::size_t>(col)];
    if (c.width == 0)
        return s;
    appendUtf8(s, c.cp);
    if (c.more)
        for (const auto& [k, rest] : extra)
            if (k == col)
                for (const char32_t cp : rest)
                    appendUtf8(s, cp);
    return s;
}

// ------------------------------------------------------------- uppbyggnad

Screen::Screen(int columns, int rows, int scrollback)
    : cols_(std::max(1, columns)), rows_(std::max(1, rows)),
      scrollback_(std::max(0, scrollback))
{
    main_.lines.assign(static_cast<std::size_t>(rows_), blankLine());
    alt_.lines.assign(static_cast<std::size_t>(rows_), blankLine());
    bottom_ = rows_ - 1;
    tabs_.assign(static_cast<std::size_t>(cols_), false);
    for (int c = 8; c < cols_; c += 8)
        tabs_[static_cast<std::size_t>(c)] = true;
    dirty_.assign(static_cast<std::size_t>(rows_), false);
}

void Screen::feed(std::string_view bytes) { parser_.feed(bytes); }

Cell Screen::blank() const
{
    Cell c;
    c.bg = cur_.bg;   // BCE — en raderad cell får aktuell bakgrund, som xterm
    return c;
}

Line Screen::blankLine() const
{
    Line l;
    l.cells.assign(static_cast<std::size_t>(cols_), blank());
    return l;
}

void Screen::clearBuffer(Buffer& b)
{
    for (Line& l : b.lines)
        l = blankLine();
}

int Screen::historySize() const
{
    return modes_.altScreen ? 0 : static_cast<int>(history_.size());
}

const Line& Screen::line(int index) const
{
    const int h = historySize();
    if (index < h)
        return history_[static_cast<std::size_t>(std::max(0, index))];
    return buf().lines[static_cast<std::size_t>(clampInt(index - h, 0, rows_ - 1))];
}

void Screen::pushHistory(Line l)
{
    history_.push_back(std::move(l));
    while (static_cast<int>(history_.size()) > scrollback_) {
        history_.pop_front();
        ++dropped_;
    }
    historyChanged_ = true;
}

void Screen::setScrollback(int lines)
{
    scrollback_ = std::max(0, lines);
    while (static_cast<int>(history_.size()) > scrollback_) {
        history_.pop_front();
        ++dropped_;
        historyChanged_ = true;
    }
}

void Screen::clearHistory()
{
    dropped_ += history_.size();
    history_.clear();
    historyChanged_ = true;
}

int Screen::keyboardFlags() const
{
    const Buffer& b = buf();
    return b.kittyStack.empty() ? 0 : b.kittyStack.back();
}

std::string Screen::takeReplies()
{
    std::string ut;
    ut.swap(replies_);
    return ut;
}

void Screen::setReportedColors(std::uint32_t fg, std::uint32_t bg, std::uint32_t cursor)
{
    reportFg_     = fg;
    reportBg_     = bg;
    reportCursor_ = cursor;
}

bool Screen::isDirty(int r) const
{
    return allDirty_ || (r >= 0 && r < rows_ && dirty_[static_cast<std::size_t>(r)]);
}

void Screen::clearDirty()
{
    std::fill(dirty_.begin(), dirty_.end(), false);
    allDirty_       = false;
    historyChanged_ = false;
    titleChanged_   = false;
}

void Screen::dirty(int r)
{
    if (r >= 0 && r < rows_)
        dirty_[static_cast<std::size_t>(r)] = true;
}

// ------------------------------------------------------------ storleken

void Screen::resize(int columns, int rows)
{
    columns = std::max(1, columns);
    rows    = std::max(1, rows);
    if (columns == cols_ && rows == rows_)
        return;

    // RADERNA. Den aktiva skärmen behåller markörens rad i texten: först går
    // raderna UNDER markören, sedan de översta — till historiken på den
    // primära. Den andra bufferten kapas eller fylls nedtill.
    auto höjd = [&](Buffer& b, bool aktiv) {
        auto& L = b.lines;
        while (static_cast<int>(L.size()) > rows) {
            if (aktiv && static_cast<int>(L.size()) - 1 <= cur_.row) {
                if (&b == &main_)
                    pushHistory(std::move(L.front()));
                L.erase(L.begin());
                --cur_.row;
            } else {
                L.pop_back();
            }
        }
        while (static_cast<int>(L.size()) < rows)
            L.push_back(blankLine());
    };
    höjd(main_, !modes_.altScreen);
    höjd(alt_, modes_.altScreen);

    // KOLUMNERNA — kapas eller fylls; ett brett tecken som delas vid den nya
    // kanten blir tomt.
    const int gamlaCols = cols_;
    cols_               = columns;
    rows_               = rows;
    auto bredd = [&](Line& l) {
        Cell tom;
        l.cells.resize(static_cast<std::size_t>(cols_), tom);
        if (l.cells.back().width == 2) {
            dropExtra(l, cols_ - 1);
            l.cells.back() = tom;
        }
        shiftExtra(l, 0, 0, cols_);
    };
    for (Line& l : main_.lines)
        bredd(l);
    for (Line& l : alt_.lines)
        bredd(l);

    tabs_.resize(static_cast<std::size_t>(cols_), false);
    for (int c = (gamlaCols / 8 + 1) * 8; c < cols_; c += 8)
        tabs_[static_cast<std::size_t>(c)] = true;

    top_    = 0;
    bottom_ = rows_ - 1;
    dirty_.assign(static_cast<std::size_t>(rows_), false);
    cur_.pendingWrap = false;
    clampCursor();
    main_.saved.row = clampInt(main_.saved.row, 0, rows_ - 1);
    main_.saved.col = clampInt(main_.saved.col, 0, cols_ - 1);
    alt_.saved.row  = clampInt(alt_.saved.row, 0, rows_ - 1);
    alt_.saved.col  = clampInt(alt_.saved.col, 0, cols_ - 1);
    lastRow_        = -1;
    dirtyAll();
}

// -------------------------------------------------------------- markören

void Screen::clampCursor()
{
    cur_.row = clampInt(cur_.row, 0, rows_ - 1);
    cur_.col = clampInt(cur_.col, 0, cols_ - 1);
}

void Screen::moveTo(int r, int c)
{
    dirty(cur_.row);
    if (modes_.origin)
        cur_.row = clampInt(r + top_, top_, bottom_);
    else
        cur_.row = clampInt(r, 0, rows_ - 1);
    cur_.col         = clampInt(c, 0, cols_ - 1);
    cur_.pendingWrap = false;
    dirty(cur_.row);
}

// De vågräta förflyttningarna markerar markörens rad smutsig —
// markören ritas om där den lämnade och där den står.
void Screen::carriageReturn()
{
    cur_.col         = 0;
    cur_.pendingWrap = false;
    dirty(cur_.row);
}

void Screen::lineFeed()
{
    dirty(cur_.row);
    if (cur_.row == bottom_)
        scrollUp(top_, bottom_, 1, top_ == 0 && !modes_.altScreen);
    else if (cur_.row < rows_ - 1)
        ++cur_.row;
    cur_.pendingWrap = false;
    dirty(cur_.row);
}

void Screen::reverseIndex()
{
    dirty(cur_.row);
    if (cur_.row == top_)
        scrollDown(top_, bottom_, 1);
    else if (cur_.row > 0)
        --cur_.row;
    cur_.pendingWrap = false;
    dirty(cur_.row);
}

void Screen::scrollUp(int top, int bottom, int n, bool toHistory)
{
    n = std::min(n, bottom - top + 1);
    if (n <= 0)
        return;
    auto& L = buf().lines;
    for (int i = 0; i < n; ++i) {
        Line& l = L[static_cast<std::size_t>(top + i)];
        if (toHistory)
            pushHistory(std::move(l));
        l = blankLine();
    }
    std::rotate(L.begin() + top, L.begin() + top + n, L.begin() + bottom + 1);
    dirtyAll();
}

void Screen::scrollDown(int top, int bottom, int n)
{
    n = std::min(n, bottom - top + 1);
    if (n <= 0)
        return;
    auto& L = buf().lines;
    std::rotate(L.begin() + top, L.begin() + bottom + 1 - n, L.begin() + bottom + 1);
    for (int i = 0; i < n; ++i)
        L[static_cast<std::size_t>(top + i)] = blankLine();
    dirtyAll();
}

// ------------------------------------------------------------- tecknen

// En halv bred cell i `col` gör hela tecknet tomt — annars står en
// fortsättning kvar utan sin början (eller tvärtom).
void Screen::fixWide(Line& l, int col)
{
    if (col < 0 || col >= cols_)
        return;
    Cell& c = l.cells[static_cast<std::size_t>(col)];
    if (c.width == 0 && col > 0) {
        Cell& f = l.cells[static_cast<std::size_t>(col - 1)];
        dropExtra(l, col - 1);
        f = Cell{U' ', f.fg, f.bg, f.attrs, 1, false};
    } else if (c.width == 2 && col + 1 < cols_) {
        Cell& e = l.cells[static_cast<std::size_t>(col + 1)];
        e       = Cell{U' ', e.fg, e.bg, e.attrs, 1, false};
    }
}

// Fogas `cp` till klustret före markören (utöver de kombinerande tecknen,
// som alltid gör det)? Bara direkt efter ett skrivet kluster.
bool Screen::joinsCluster(char32_t cp) const
{
    if (lastRow_ < 0)
        return false;
    const Line& l  = buf().lines[static_cast<std::size_t>(lastRow_)];
    const Cell& b  = l.cells[static_cast<std::size_t>(lastCol_)];
    char32_t    sista = b.cp;
    if (b.more)
        for (const auto& [k, s] : l.extra)
            if (k == lastCol_ && !s.empty())
                sista = s.back();
    if (sista == 0x200D && isPictographic(cp))
        return true;   // ZWJ-följden: 👨‍👩‍👧
    if (isSkinTone(cp) && isPictographic(b.cp))
        return true;   // hudtonen: 👍🏽
    return isRegional(cp) && isRegional(b.cp) && !b.more;   // flaggan: 🇸🇪
}

void Screen::attachToCluster(char32_t cp)
{
    if (lastRow_ < 0)
        return;   // ingen bas att fästa vid — tecknet försvinner, som i xterm
    Line&           l    = buf().lines[static_cast<std::size_t>(lastRow_)];
    Cell&           b    = l.cells[static_cast<std::size_t>(lastCol_)];
    std::u32string* rest = extraAt(l, lastCol_);
    if (rest && rest->size() + 1 >= kClusterMax)
        return;   // klustret är fullt — överflödet sväljs
    if (rest)
        *rest += cp;
    else
        l.extra.emplace_back(lastCol_, std::u32string(1, cp));
    b.more = true;
    dirty(lastRow_);

    // VS16 BREDDAR en pictografisk bas — bara direkt efter den (ingen
    // förflyttning emellan).
    if (cp != 0xFE0F || b.width != 1 || !isPictographic(b.cp) || lastRow_ != cur_.row)
        return;
    if (!cur_.pendingWrap && cur_.col == lastCol_ + 1) {
        fixWide(l, cur_.col);
        b.width = 2;
        Cell& f = l.cells[static_cast<std::size_t>(cur_.col)];
        dropExtra(l, cur_.col);
        f = Cell{0, b.fg, b.bg, b.attrs, 0, false};
        if (++cur_.col >= cols_) {
            cur_.col         = cols_ - 1;
            cur_.pendingWrap = modes_.autoWrap;
        }
        return;
    }
    // VID HÖGERKANTEN: basen står i sista kolumnen med en väntande
    // radbrytning. Ett ursprungligen brett tecken där hade brutit raden först,
    // och det breddade klustret följer samma regel — det flyttar till nästa
    // rad. Utan autowrap breddas det inte.
    if (cur_.pendingWrap && lastCol_ == cols_ - 1 && cur_.col == lastCol_ &&
        modes_.autoWrap && cols_ >= 2) {
        Cell                 bas   = b;
        const std::u32string resten = *extraAt(l, lastCol_);
        eraseCells(lastRow_, lastCol_, cols_);
        l.wrapped = true;
        carriageReturn();
        lineFeed();
        Line& n = row(cur_.row);
        fixWide(n, 0);
        fixWide(n, 1);
        dropExtra(n, 0);
        dropExtra(n, 1);
        bas.width    = 2;
        n.cells[0]   = bas;
        n.cells[1]   = Cell{0, bas.fg, bas.bg, bas.attrs, 0, false};
        n.extra.emplace_back(0, resten);
        lastRow_         = cur_.row;
        lastCol_         = 0;
        cur_.col         = 2;
        cur_.pendingWrap = false;
        if (cur_.col >= cols_) {
            cur_.col         = cols_ - 1;
            cur_.pendingWrap = true;
        }
        dirty(cur_.row);
    }
}

void Screen::print(char32_t cp)
{
    if (cp >= 0x5F && cp <= 0x7E &&
        (cur_.shiftOut ? cur_.g1Graphics : cur_.g0Graphics))
        cp = kDecGraphics[cp - 0x5F];

    int w = cellWidth(cp);
    if (w == 0 || joinsCluster(cp)) {
        attachToCluster(cp);
        return;
    }
    if (w == 2 && cols_ < 2)
        w = 1;

    if (cur_.pendingWrap && modes_.autoWrap) {
        row(cur_.row).wrapped = true;
        carriageReturn();
        lineFeed();
    }
    cur_.pendingWrap = false;
    // Ett brett tecken i SISTA kolumnen ryms inte: raden bryts först.
    if (w == 2 && cur_.col == cols_ - 1) {
        if (!modes_.autoWrap)
            return;
        eraseCells(cur_.row, cur_.col, cols_);
        row(cur_.row).wrapped = true;
        carriageReturn();
        lineFeed();
    }

    if (modes_.insert)
        insertChars(w);
    Line& l = row(cur_.row);
    fixWide(l, cur_.col);
    if (w == 2)
        fixWide(l, cur_.col + 1);
    dropExtra(l, cur_.col);
    l.cells[static_cast<std::size_t>(cur_.col)] =
        Cell{cp, cur_.fg, cur_.bg, cur_.attrs, static_cast<std::uint8_t>(w), false};
    if (w == 2) {
        dropExtra(l, cur_.col + 1);
        l.cells[static_cast<std::size_t>(cur_.col) + 1] =
            Cell{0, cur_.fg, cur_.bg, cur_.attrs, 0, false};
    }
    lastRow_     = cur_.row;
    lastCol_     = cur_.col;
    lastPrinted_ = cp;
    dirty(cur_.row);

    cur_.col += w;
    if (cur_.col >= cols_) {
        cur_.col         = cols_ - 1;
        cur_.pendingWrap = modes_.autoWrap;
    }
}

void Screen::execute(std::uint8_t c)
{
    lastRow_ = -1;
    switch (c) {
    case 0x08:   // BS
        if (cur_.col > 0)
            --cur_.col;
        cur_.pendingWrap = false;
        dirty(cur_.row);
        break;
    case 0x09: {   // HT
        int n = cur_.col + 1;
        while (n < cols_ && !tabs_[static_cast<std::size_t>(n)])
            ++n;
        cur_.col         = std::min(n, cols_ - 1);
        cur_.pendingWrap = false;
        dirty(cur_.row);
        break;
    }
    case 0x0A:
    case 0x0B:
    case 0x0C:   // LF, VT, FF
        lineFeed();
        if (modes_.newline)
            carriageReturn();
        break;
    case 0x0D:
        carriageReturn();
        break;
    case 0x0E:
        cur_.shiftOut = true;
        break;
    case 0x0F:
        cur_.shiftOut = false;
        break;
    default:   // BEL och resten: ingenting
        break;
    }
}

// ----------------------------------------------------------- radering

void Screen::eraseCells(int r, int from, int to)
{
    from = clampInt(from, 0, cols_);
    to   = clampInt(to, 0, cols_);
    if (from >= to)
        return;
    Line& l = row(r);
    fixWide(l, from);
    fixWide(l, to - 1);
    const Cell tom = blank();
    for (int c = from; c < to; ++c) {
        dropExtra(l, c);
        l.cells[static_cast<std::size_t>(c)] = tom;
    }
    dirty(r);
}

void Screen::eraseInDisplay(int mode)
{
    switch (mode) {
    case 0:
        eraseCells(cur_.row, cur_.col, cols_);
        for (int r = cur_.row + 1; r < rows_; ++r)
            eraseCells(r, 0, cols_);
        break;
    case 1:
        for (int r = 0; r < cur_.row; ++r)
            eraseCells(r, 0, cols_);
        eraseCells(cur_.row, 0, cur_.col + 1);
        break;
    case 2:
        for (int r = 0; r < rows_; ++r) {
            eraseCells(r, 0, cols_);
            row(r).wrapped = false;
        }
        break;
    case 3:
        clearHistory();
        break;
    default:
        break;
    }
}

void Screen::eraseInLine(int mode)
{
    if (mode == 0)
        eraseCells(cur_.row, cur_.col, cols_);
    else if (mode == 1)
        eraseCells(cur_.row, 0, cur_.col + 1);
    else if (mode == 2)
        eraseCells(cur_.row, 0, cols_);
}

void Screen::insertChars(int n)
{
    Line& l = row(cur_.row);
    n       = std::min(n, cols_ - cur_.col);
    if (n <= 0)
        return;
    fixWide(l, cur_.col);
    auto b = l.cells.begin() + cur_.col;
    std::move_backward(b, l.cells.end() - n, l.cells.end());
    std::fill(b, b + n, blank());
    shiftExtra(l, cur_.col, n, cols_);
    if (l.cells.back().width == 2) {   // ett brett tecken knuffat förbi kanten
        dropExtra(l, cols_ - 1);
        l.cells.back() = blank();
    }
    dirty(cur_.row);
}

void Screen::deleteChars(int n)
{
    Line& l = row(cur_.row);
    n       = std::min(n, cols_ - cur_.col);
    if (n <= 0)
        return;
    fixWide(l, cur_.col);
    fixWide(l, cur_.col + n - 1);
    for (int c = cur_.col; c < cur_.col + n; ++c)
        dropExtra(l, c);
    auto b = l.cells.begin() + cur_.col;
    std::move(b + n, l.cells.end(), b);
    std::fill(l.cells.end() - n, l.cells.end(), blank());
    shiftExtra(l, cur_.col + n, -n, cols_);
    dirty(cur_.row);
}

void Screen::insertLines(int n)
{
    if (cur_.row < top_ || cur_.row > bottom_)
        return;
    scrollDown(cur_.row, bottom_, n);
    carriageReturn();
}

void Screen::deleteLines(int n)
{
    if (cur_.row < top_ || cur_.row > bottom_)
        return;
    scrollUp(cur_.row, bottom_, n, false);
    carriageReturn();
}

// -------------------------------------------------------- spara/återställ

void Screen::saveCursor()
{
    buf().saved        = cur_;
    buf().saved.origin = modes_.origin;
}

void Screen::restoreCursor()
{
    dirty(cur_.row);
    cur_          = buf().saved;
    modes_.origin = cur_.origin;
    clampCursor();
    dirty(cur_.row);
}

void Screen::fullReset()
{
    modes_           = Modes{};
    cur_             = Cursor{};
    main_.saved      = Cursor{};
    alt_.saved       = Cursor{};
    main_.kittyStack.clear();
    alt_.kittyStack.clear();
    clearBuffer(main_);
    clearBuffer(alt_);
    top_    = 0;
    bottom_ = rows_ - 1;
    std::fill(tabs_.begin(), tabs_.end(), false);
    for (int c = 8; c < cols_; c += 8)
        tabs_[static_cast<std::size_t>(c)] = true;
    dirtyAll();
}

// ---------------------------------------------------------------- ESC

void Screen::escDispatch(std::string_view inter, char f)
{
    lastRow_ = -1;
    if (inter.empty()) {
        switch (f) {
        case '7': saveCursor(); break;
        case '8': restoreCursor(); break;
        case 'D': lineFeed(); break;                       // IND
        case 'E': carriageReturn(); lineFeed(); break;     // NEL
        case 'M': reverseIndex(); break;                   // RI
        case 'H': tabs_[static_cast<std::size_t>(cur_.col)] = true; break;   // HTS
        case 'c': fullReset(); break;                      // RIS
        case '=': modes_.keypadApp = true; break;          // DECKPAM
        case '>': modes_.keypadApp = false; break;         // DECKPNM
        default: break;                                    // ST (`\`) m.fl.
        }
        return;
    }
    if (inter == "(" || inter == ")") {   // G0/G1: `0` linjegrafik, annat ASCII
        bool& g = inter == "(" ? cur_.g0Graphics : cur_.g1Graphics;
        g       = f == '0';
    }
}

// ---------------------------------------------------------------- lägen

void Screen::setMode(char prefix, int mode, bool on)
{
    if (prefix == 0) {
        if (mode == 4)
            modes_.insert = on;
        else if (mode == 20)
            modes_.newline = on;
        return;
    }
    if (prefix != '?')
        return;
    switch (mode) {
    case 1: modes_.cursorKeys = on; break;
    case 6:
        modes_.origin = on;
        moveTo(0, 0);
        break;
    case 7: modes_.autoWrap = on; break;
    case 12: modes_.cursorBlink = on; dirty(cur_.row); break;
    case 25: modes_.cursorVisible = on; dirty(cur_.row); break;
    case 47:
    case 1047:
        // 1047 rensar den alternativa skärmen när den LÄMNAS, 47 aldrig.
        if (on == modes_.altScreen)
            break;
        if (!on && mode == 1047)
            clearBuffer(alt_);
        modes_.altScreen = on;
        dirtyAll();
        break;
    case 1048:
        on ? saveCursor() : restoreCursor();
        break;
    case 1049:
        // Spara markören, byt och rensa — och tillbaka med markören.
        if (on == modes_.altScreen)
            break;
        if (on) {
            saveCursor();
            modes_.altScreen = true;
            clearBuffer(alt_);
        } else {
            modes_.altScreen = false;
            restoreCursor();
        }
        dirtyAll();
        break;
    case 1000: modes_.mouse = on ? MouseMode::Press : MouseMode::None; break;
    case 1002: modes_.mouse = on ? MouseMode::Drag : MouseMode::None; break;
    case 1003: modes_.mouse = on ? MouseMode::Any : MouseMode::None; break;
    case 1006: modes_.mouseSgr = on; break;
    case 1004: modes_.focusEvents = on; break;
    case 1007: modes_.altScroll = on; break;
    case 2004: modes_.bracketedPaste = on; break;
    case 2026:
        modes_.synchronized = on;
        if (!on)
            dirtyAll();   // släppet visar allt som hänt under tiden
        break;
    default: break;
    }
}

int Screen::modeState(char prefix, int mode) const
{
    auto svar = [](bool on) { return on ? 1 : 2; };
    if (prefix == 0) {
        if (mode == 4)
            return svar(modes_.insert);
        if (mode == 20)
            return svar(modes_.newline);
        return 0;
    }
    switch (mode) {
    case 1: return svar(modes_.cursorKeys);
    case 6: return svar(modes_.origin);
    case 7: return svar(modes_.autoWrap);
    case 12: return svar(modes_.cursorBlink);
    case 25: return svar(modes_.cursorVisible);
    case 47:
    case 1047:
    case 1049: return svar(modes_.altScreen);
    case 1000: return svar(modes_.mouse == MouseMode::Press);
    case 1002: return svar(modes_.mouse == MouseMode::Drag);
    case 1003: return svar(modes_.mouse == MouseMode::Any);
    case 1004: return svar(modes_.focusEvents);
    case 1006: return svar(modes_.mouseSgr);
    case 1007: return svar(modes_.altScroll);
    case 2004: return svar(modes_.bracketedPaste);
    case 2026: return svar(modes_.synchronized);
    default: return 0;
    }
}

// ----------------------------------------------------------------- SGR

void Screen::sgr(const CsiParams& p)
{
    if (p.count == 0) {
        cur_.fg    = Color{};
        cur_.bg    = Color{};
        cur_.attrs = 0;
        return;
    }
    auto val = [&](int i) { return clampInt(p.get(i, 0), 0, 255); };
    for (int i = 0; i < p.count;) {
        const int v    = std::max(0, p.value[i]);
        int       subs = 0;   // underparametrarna efter fältet (`:`-formen)
        while (i + 1 + subs < p.count && p.sub[i + 1 + subs])
            ++subs;
        int extra = 0;        // fält som `;`-formen förbrukade
        switch (v) {
        case 0: cur_.fg = Color{}; cur_.bg = Color{}; cur_.attrs = 0; break;
        case 1: cur_.attrs |= attr::kBold; break;
        case 2: cur_.attrs |= attr::kDim; break;
        case 3: cur_.attrs |= attr::kItalic; break;
        case 4:   // `4:0` av, `4:1`–`4:5` på (dubbel, vågig … ritas enkel)
            if (subs > 0 && p.value[i + 1] == 0)
                cur_.attrs &= ~attr::kUnderline;
            else
                cur_.attrs |= attr::kUnderline;
            break;
        case 5:
        case 6: cur_.attrs |= attr::kBlink; break;
        case 7: cur_.attrs |= attr::kInverse; break;
        case 8: cur_.attrs |= attr::kHidden; break;
        case 9: cur_.attrs |= attr::kStrike; break;
        case 21: cur_.attrs |= attr::kUnderline; break;   // dubbel — ritas enkel
        case 22: cur_.attrs &= ~(attr::kBold | attr::kDim); break;
        case 23: cur_.attrs &= ~attr::kItalic; break;
        case 24: cur_.attrs &= ~attr::kUnderline; break;
        case 25: cur_.attrs &= ~attr::kBlink; break;
        case 27: cur_.attrs &= ~attr::kInverse; break;
        case 28: cur_.attrs &= ~attr::kHidden; break;
        case 29: cur_.attrs &= ~attr::kStrike; break;
        case 39: cur_.fg = Color{}; break;
        case 49: cur_.bg = Color{}; break;
        case 38:
        case 48:
        case 58: {   // utökad färg; 58 (understrykningens) tolkas och ignoreras
            Color c;
            bool  ok = false;
            if (subs > 0) {
                const int typ = p.get(i + 1, -1);
                if (typ == 5 && subs >= 2) {
                    c  = Color::indexed(val(i + 2));
                    ok = true;
                } else if (typ == 2 && subs >= 4) {
                    const int o = subs >= 5 ? i + 3 : i + 2;   // färgrymden är valfri
                    c  = Color::rgb(val(o), val(o + 1), val(o + 2));
                    ok = true;
                }
            } else {
                const int typ = p.get(i + 1, -1);
                if (typ == 5) {
                    c     = Color::indexed(val(i + 2));
                    extra = 2;
                    ok    = true;
                } else if (typ == 2) {
                    c     = Color::rgb(val(i + 2), val(i + 3), val(i + 4));
                    extra = 4;
                    ok    = true;
                }
            }
            if (ok && v == 38)
                cur_.fg = c;
            else if (ok && v == 48)
                cur_.bg = c;
            break;
        }
        default:
            if (v >= 30 && v <= 37)
                cur_.fg = Color::indexed(v - 30);
            else if (v >= 40 && v <= 47)
                cur_.bg = Color::indexed(v - 40);
            else if (v >= 90 && v <= 97)
                cur_.fg = Color::indexed(v - 90 + 8);
            else if (v >= 100 && v <= 107)
                cur_.bg = Color::indexed(v - 100 + 8);
            break;   // 53, 55, 59 och okända: ingenting
        }
        i += 1 + subs + extra;
    }
}

// ----------------------------------------------------------------- CSI

void Screen::csiDispatch(char prefix, const CsiParams& p, std::string_view inter,
                         char f)
{
    lastRow_ = -1;
    const int n = p.get(0, 1, true);   // ett antal: 0 och tomt betyder 1

    // DECRQM — `CSI ? Ps $ p` och `CSI Ps $ p`.
    if (inter == "$" && f == 'p') {
        char buf[48];
        std::snprintf(buf, sizeof buf, "\x1b[%s%d;%d$y", prefix == '?' ? "?" : "",
                      p.get(0, 0), modeState(prefix, p.get(0, 0)));
        reply(buf);
        return;
    }
    if (inter == " " && f == 'q') {   // DECSCUSR
        const int s       = p.get(0, 0);
        modes_.cursorShape = s <= 2 ? CursorShape::Block
                             : s <= 4 ? CursorShape::Underline
                                      : CursorShape::Bar;
        modes_.cursorBlink = s == 0 || s % 2 == 1;
        dirty(cur_.row);
        return;
    }
    if (inter == "!" && f == 'p') {   // DECSTR — mjuk återställning
        modes_.insert        = false;
        modes_.origin        = false;
        modes_.autoWrap      = true;
        modes_.cursorKeys    = false;
        modes_.keypadApp     = false;
        modes_.cursorVisible = true;
        top_                 = 0;
        bottom_              = rows_ - 1;
        cur_.fg              = Color{};
        cur_.bg              = Color{};
        cur_.attrs           = 0;
        cur_.g0Graphics = cur_.g1Graphics = cur_.shiftOut = false;
        buf().saved          = Cursor{};
        dirty(cur_.row);
        return;
    }
    if (!inter.empty())
        return;

    // KITTYPROTOKOLLET — skilt från SCORC `CSI u` av prefixet.
    if (f == 'u' && prefix != 0) {
        auto& st = buf().kittyStack;
        switch (prefix) {
        case '>':
            if (st.size() >= kKittyStackMax)
                st.erase(st.begin());
            st.push_back(p.get(0, 0) & kKittySupported);
            break;
        case '<':
            for (int i = 0; i < n && !st.empty(); ++i)
                st.pop_back();
            break;
        case '=': {
            const int flaggor = p.get(0, 0) & kKittySupported;
            const int läge    = p.get(1, 1);
            if (st.empty())
                st.push_back(0);
            int& top = st.back();
            top      = läge == 1 ? flaggor : läge == 2 ? (top | flaggor) : (top & ~flaggor);
            break;
        }
        case '?': {
            char buf[32];
            std::snprintf(buf, sizeof buf, "\x1b[?%du", keyboardFlags());
            reply(buf);
            break;
        }
        default: break;
        }
        return;
    }

    if (prefix == '?') {
        switch (f) {
        case 'h':
        case 'l':
            for (int i = 0; i < p.count; ++i)
                setMode('?', p.get(i, 0), f == 'h');
            break;
        case 'n':
            if (p.get(0, 0) == 6) {   // DECXCPR
                char buf[48];
                std::snprintf(buf, sizeof buf, "\x1b[?%d;%dR",
                              cur_.row + 1 - (modes_.origin ? top_ : 0), cur_.col + 1);
                reply(buf);
            }
            break;
        default: break;
        }
        return;
    }
    if (prefix == '>') {
        if (f == 'c')
            reply("\x1b[>0;10;1c");   // DA2: VT100-klass, version 10
        return;   // XTMODKEYS (`>m`), XTVERSION (`>q`) m.fl.: ingenting
    }
    if (prefix != 0)
        return;

    switch (f) {
    case 'A': {
        const int lim = cur_.row >= top_ ? top_ : 0;
        moveTo(std::max(lim, cur_.row - n) - (modes_.origin ? top_ : 0), cur_.col);
        break;
    }
    case 'B':
    case 'e': {
        const int lim = cur_.row <= bottom_ ? bottom_ : rows_ - 1;
        moveTo(std::min(lim, cur_.row + n) - (modes_.origin ? top_ : 0), cur_.col);
        break;
    }
    case 'C':
    case 'a':
        cur_.col         = std::min(cols_ - 1, cur_.col + n);
        cur_.pendingWrap = false;
        dirty(cur_.row);
        break;
    case 'D':
        cur_.col         = std::max(0, cur_.col - n);
        cur_.pendingWrap = false;
        dirty(cur_.row);
        break;
    case 'E':
        csiDispatch(0, p, inter, 'B');
        carriageReturn();
        break;
    case 'F':
        csiDispatch(0, p, inter, 'A');
        carriageReturn();
        break;
    case 'G':
    case '`':
        cur_.col         = clampInt(n - 1, 0, cols_ - 1);
        cur_.pendingWrap = false;
        dirty(cur_.row);
        break;
    case 'H':
    case 'f':
        moveTo(p.get(0, 1, true) - 1, p.get(1, 1, true) - 1);
        break;
    case 'd':
        moveTo(n - 1, cur_.col);
        break;
    case 'J': eraseInDisplay(p.get(0, 0)); break;
    case 'K': eraseInLine(p.get(0, 0)); break;
    case '@': insertChars(n); break;
    case 'P': deleteChars(n); break;
    case 'X': eraseCells(cur_.row, cur_.col, cur_.col + n); break;
    case 'L': insertLines(n); break;
    case 'M': deleteLines(n); break;
    case 'S': scrollUp(top_, bottom_, n, top_ == 0 && !modes_.altScreen); break;
    case 'T':
        if (p.count <= 1)   // fem fält är musens gamla spårning — inte vår
            scrollDown(top_, bottom_, n);
        break;
    case 'r': {   // DECSTBM
        const int t = p.get(0, 1, true) - 1;
        const int b = std::min(p.get(1, rows_, true), rows_) - 1;
        if (t < b) {
            top_    = t;
            bottom_ = b;
            moveTo(0, 0);
        }
        break;
    }
    case 's': saveCursor(); break;      // SCOSC (utan vänster/höger-marginaler)
    case 'u': restoreCursor(); break;   // SCORC
    case 'b':                           // REP
        if (lastPrinted_ != 0)
            for (int i = 0; i < std::min(n, cols_ * rows_); ++i)
                print(lastPrinted_);
        break;
    case 'g':
        if (p.get(0, 0) == 0)
            tabs_[static_cast<std::size_t>(cur_.col)] = false;
        else if (p.get(0, 0) == 3)
            std::fill(tabs_.begin(), tabs_.end(), false);
        break;
    case 'I':
        for (int i = 0; i < n; ++i)
            execute(0x09);
        break;
    case 'Z':
        for (int i = 0; i < n && cur_.col > 0; ++i) {
            --cur_.col;
            while (cur_.col > 0 && !tabs_[static_cast<std::size_t>(cur_.col)])
                --cur_.col;
        }
        cur_.pendingWrap = false;
        dirty(cur_.row);
        break;
    case 'n':
        if (p.get(0, 0) == 5) {
            reply("\x1b[0n");
        } else if (p.get(0, 0) == 6) {
            char buf[48];
            std::snprintf(buf, sizeof buf, "\x1b[%d;%dR",
                          cur_.row + 1 - (modes_.origin ? top_ : 0), cur_.col + 1);
            reply(buf);
        }
        break;
    case 'c':
        if (p.get(0, 0) == 0)
            reply("\x1b[?62;22c");   // DA1: VT220-klass med ANSI-färg
        break;
    case 'h':
    case 'l':
        for (int i = 0; i < p.count; ++i)
            setMode(0, p.get(i, 0), f == 'h');
        break;
    case 'm': sgr(p); break;
    default: break;   // `t` (fönsteråtgärder) och okända: ingenting
    }
}

// ----------------------------------------------------------------- OSC

void Screen::oscDispatch(std::string_view text, bool bel)
{
    const std::size_t semi = text.find(';');
    if (semi == std::string_view::npos)
        return;
    int nr = 0;
    for (const char c : text.substr(0, semi)) {
        if (c < '0' || c > '9' || nr > (kOscNumberMax - (c - '0')) / 10)
            return;   // inget tal, eller för stort — gränsen FÖRE multiplikationen
        nr = nr * 10 + (c - '0');
    }
    const std::string_view arg = text.substr(semi + 1);
    switch (nr) {
    case 0:
    case 2:
        if (title_ != arg) {
            title_        = std::string(arg);
            titleChanged_ = true;
        }
        break;
    case 7:
        cwd_ = std::string(arg);
        break;
    case 10:
    case 11:
    case 12:
        if (arg == "?") {
            const std::uint32_t v = nr == 10 ? reportFg_ : nr == 11 ? reportBg_ : reportCursor_;
            const unsigned r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
            char buf[64];
            std::snprintf(buf, sizeof buf, "\x1b]%d;rgb:%02x%02x/%02x%02x/%02x%02x%s",
                          nr, r, r, g, g, b, b, bel ? "\x07" : "\x1b\\");
            reply(buf);
        }
        break;
    default:
        break;   // 1, 4, 8, 52, 104, 110–112 …: tolkade och ignorerade
    }
}

} // namespace glaze::term
