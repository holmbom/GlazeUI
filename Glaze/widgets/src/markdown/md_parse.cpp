// md_parse — Markdown-text → md::Document.
// Kontraktet står i md_parse.h, delmängden och dess gränser i
// glaze/widgets/markdown_view.h.
//
// TVÅ FASER, CommonMark 0.31.2:s "Appendix A: A parsing strategy" och
// cmarks struktur (blocks.c/inlines.c) i husets form:
//
//   BLOCKFASEN (`BlockParser`) läser en rad i taget. Först prövas de ÖPPNA
//   behållarna uppifrån (citatets `>`, listpostens indrag, kodens staket),
//   sedan öppnas nya block (citat, rubrik, staket, kommentar, setext,
//   linje, listpost, indragen kod, tabell), och sist hamnar texten i rätt
//   block — eller som en LAT fortsättningsrad i ett öppet stycke.
//   Referensdefinitionerna plockas ur styckenas början när de stängs.
//
//   INLINEFASEN (`InlineParser`) per stycke, rubrik och tabellcell: en nod-
//   lista där kodspann, escapes, entiteter, autolänkar och kommentarer
//   avgörs direkt, medan `*`/`_` och `[`/`![` läggs på stackar.
//   `]` letar sin hakparentes och gör en länk eller bild; `process emphasis`
//   paras ihop betoningarna med `openers_bottom`, så att patologisk indata
//   förblir linjär. Trädet plattas sedan till `Span`s med stilbitar — utan
//   rekursion, för en betoning kan nästlas tusentals nivåer.
//
// Inga undantag kastas: allt som inte blir en konstruktion är text.

#include "md_parse.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace glaze::md {
namespace {

// FORMATETS KONSTANTER — delar av språket och varken mått eller utseende,
// därför här och inte i Style:
//   kTabStop     tabbstoppet (CommonMark 2.2) — indraget, och tabbarna i kod
//   kCodeIndent  indragen kod (CommonMark 4.4)
//   kMaxNesting  behållarnas, hakparentesernas och länkadressens
//                parentesdjup: djupare indata blir TEXT. Ett skydd mot
//                stackspill och kvadratisk tid på fientlig indata (1 000 `>`
//                i rad), inte en regel ur specen — cmark har samma tak för
//                adressens parenteser
//   kMaxLabel    en länketikett är högst 999 tecken (CommonMark 4.7)
//   kMaxDigits   ett listnummer är högst nio siffror (CommonMark 5.2)
constexpr int         kTabStop    = 4;
constexpr int         kCodeIndent = 4;
constexpr int         kMaxNesting = 32;
constexpr std::size_t kMaxLabel   = 999;
constexpr int         kMaxDigits  = 9;

constexpr std::size_t kNpos = std::string_view::npos;

const char kReplacement[] = "\xEF\xBF\xBD";   // U+FFFD

// ------------------------------------------------------------ tecknen

bool isSpaceOrTab(char c) { return c == ' ' || c == '\t'; }
bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }
bool isAsciiAlpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool isAsciiAlnum(char c) { return isAsciiAlpha(c) || isAsciiDigit(c); }
bool isHexDigit(char c)
{
    return isAsciiDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// ASCII-skiljetecknen (CommonMark 2.1) — `!` till `/`, `:` till `@`, `[` till
// `` ` `` och `{` till `~`.
bool isAsciiPunct(char c)
{
    return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') ||
           (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

// Unicode-blanktecken (CommonMark 2.1: Zs, tabb, LF, FF, CR).
bool isUnicodeSpace(char32_t cp)
{
    return cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'\f' ||
           cp == U'\r' || cp == 0xA0 || cp == 0x1680 ||
           (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F || cp == 0x205F ||
           cp == 0x3000;
}

// SKILJETECKEN för flankeringen: BARA ASCII — en skriven gräns; Unicode-
// skiljetecken (”, «, —) räknas som bokstäver.
bool isPunct(char32_t cp) { return cp < 0x80 && isAsciiPunct(char(cp)); }

bool startsAt(std::string_view s, std::size_t pos, std::string_view what)
{
    return pos <= s.size() && s.substr(pos, what.size()) == what;
}

std::string_view trimView(std::string_view s)
{
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && (isSpaceOrTab(s[a]) || s[a] == '\n'))
        ++a;
    while (b > a && (isSpaceOrTab(s[b - 1]) || s[b - 1] == '\n'))
        --b;
    return s.substr(a, b - a);
}

// ------------------------------------------------------------- UTF-8

// Längden på en GILTIG UTF-8-sekvens vid `i`, 0 när den är ogiltig (RFC 3629:
// ingen överlång form, inga surrogat, högst U+10FFFF).
std::size_t utf8Len(std::string_view s, std::size_t i)
{
    const auto b = [&](std::size_t k) {
        return k < s.size() ? static_cast<unsigned char>(s[k]) : 0u;
    };
    const auto cont = [&](std::size_t k) { return (b(k) & 0xC0u) == 0x80u; };
    const unsigned c = b(i);
    if (c < 0x80)
        return 1;
    if (c >= 0xC2 && c <= 0xDF)
        return cont(i + 1) ? 2 : 0;
    if (c == 0xE0)
        return b(i + 1) >= 0xA0 && b(i + 1) <= 0xBF && cont(i + 2) ? 3 : 0;
    if ((c >= 0xE1 && c <= 0xEC) || c == 0xEE || c == 0xEF)
        return cont(i + 1) && cont(i + 2) ? 3 : 0;
    if (c == 0xED)
        return b(i + 1) >= 0x80 && b(i + 1) <= 0x9F && cont(i + 2) ? 3 : 0;
    if (c == 0xF0)
        return b(i + 1) >= 0x90 && b(i + 1) <= 0xBF && cont(i + 2) &&
                       cont(i + 3)
                   ? 4
                   : 0;
    if (c >= 0xF1 && c <= 0xF3)
        return cont(i + 1) && cont(i + 2) && cont(i + 3) ? 4 : 0;
    if (c == 0xF4)
        return b(i + 1) >= 0x80 && b(i + 1) <= 0x8F && cont(i + 2) &&
                       cont(i + 3)
                   ? 4
                   : 0;
    return 0;
}

// Kodpunkten vid `i` i en text som `normalize` redan gjort giltig.
char32_t codepointAt(std::string_view s, std::size_t i)
{
    const std::size_t n = utf8Len(s, i);
    const auto        c = static_cast<unsigned char>(s[i]);
    if (n <= 1)
        return c;
    char32_t cp = c & (n == 2 ? 0x1Fu : n == 3 ? 0x0Fu : 0x07u);
    for (std::size_t k = 1; k < n; ++k)
        cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
    return cp;
}

// Kodpunkten som SLUTAR före `i`.
char32_t codepointBefore(std::string_view s, std::size_t i)
{
    std::size_t k = i - 1;
    while (k > 0 && (static_cast<unsigned char>(s[k]) & 0xC0u) == 0x80u &&
           i - k < 4)
        --k;
    return codepointAt(s, k);
}

void appendUtf8(std::string& ut, char32_t cp)
{
    if (cp < 0x80) {
        ut += char(cp);
    } else if (cp < 0x800) {
        ut += char(0xC0 | (cp >> 6));
        ut += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        ut += char(0xE0 | (cp >> 12));
        ut += char(0x80 | ((cp >> 6) & 0x3F));
        ut += char(0x80 | (cp & 0x3F));
    } else {
        ut += char(0xF0 | (cp >> 18));
        ut += char(0x80 | ((cp >> 12) & 0x3F));
        ut += char(0x80 | ((cp >> 6) & 0x3F));
        ut += char(0x80 | (cp & 0x3F));
    }
}

// FÖRBEHANDLINGEN (CommonMark 2.1–2.3): CR LF och ensamt CR blir LF, NUL blir
// U+FFFD — och ogiltig UTF-8 blir U+FFFD, en per byte, så att allt efteråt
// (brytningen, ankaret, fonten) ser giltiga kodpunkter.
std::string normalize(std::string_view in)
{
    std::string ut;
    ut.reserve(in.size());
    for (std::size_t i = 0; i < in.size();) {
        const char c = in[i];
        if (c == '\r') {
            ut += '\n';
            i += (i + 1 < in.size() && in[i + 1] == '\n') ? 2 : 1;
        } else if (c == '\0') {
            ut += kReplacement;
            ++i;
        } else if (const std::size_t n = utf8Len(in, i)) {
            ut.append(in.substr(i, n));
            i += n;
        } else {
            ut += kReplacement;
            ++i;
        }
    }
    return ut;
}

// ---------------------------------------------- entiteter och escapes

// De NAMNGIVNA entiteterna i delmängden — HTML:s namn och deras tecken,
// skrivna med escapes (tecken och ingen användartext). Övriga namn står som
// text (skriven gräns).
struct Entity {
    const char* name;
    const char* utf8;
};
constexpr Entity kEntities[] = {
    {"amp", "&"},
    {"lt", "<"},
    {"gt", ">"},
    {"quot", "\""},
    {"apos", "'"},
    {"nbsp", "\xC2\xA0"},
    {"copy", "\xC2\xA9"},
    {"reg", "\xC2\xAE"},
    {"trade", "\xE2\x84\xA2"},
    {"hellip", "\xE2\x80\xA6"},
    {"mdash", "\xE2\x80\x94"},
    {"ndash", "\xE2\x80\x93"},
};

// En entitet vid `i` (som står på `&`) — dess längd och tecken i `ut`, 0 när
// det inte är någon (CommonMark 2.5). En numerisk har 1–7 decimala eller 1–6
// hexadecimala siffror; 0, surrogat och tal över U+10FFFF blir U+FFFD.
std::size_t matchEntity(std::string_view s, std::size_t i, std::string& ut)
{
    if (i + 2 >= s.size())
        return 0;
    if (s[i + 1] == '#') {
        std::size_t k   = i + 2;
        const bool  hex = s[k] == 'x' || s[k] == 'X';
        if (hex)
            ++k;
        const std::size_t start = k;
        const std::size_t max   = hex ? 6 : 7;
        std::uint32_t     v     = 0;
        while (k < s.size() && k - start < max &&
               (hex ? isHexDigit(s[k]) : isAsciiDigit(s[k]))) {
            const char c = s[k];
            const std::uint32_t d =
                isAsciiDigit(c) ? std::uint32_t(c - '0')
                                : std::uint32_t((c | 0x20) - 'a' + 10);
            v = v * (hex ? 16u : 10u) + d;
            ++k;
        }
        if (k == start || k >= s.size() || s[k] != ';')
            return 0;
        if (v == 0 || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF))
            v = 0xFFFD;
        ut.clear();
        appendUtf8(ut, char32_t(v));
        return k + 1 - i;
    }
    for (const Entity& e : kEntities) {
        const std::string_view namn(e.name);
        if (startsAt(s, i + 1, namn) && i + 1 + namn.size() < s.size() &&
            s[i + 1 + namn.size()] == ';') {
            ut = e.utf8;
            return namn.size() + 2;
        }
    }
    return 0;
}

// Bakåtsnedstreck före ett ASCII-skiljetecken och entiteterna AVKODADE —
// länkadressens och titelns regel (CommonMark 2.4, 2.5).
std::string unescape(std::string_view s)
{
    std::string ut;
    std::string ent;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '\\' && i + 1 < s.size() && isAsciiPunct(s[i + 1])) {
            ut += s[i + 1];
            i += 2;
            continue;
        }
        if (s[i] == '&') {
            if (const std::size_t n = matchEntity(s, i, ent)) {
                ut += ent;
                i += n;
                continue;
            }
        }
        ut += s[i++];
    }
    return ut;
}

// REFERENSETIKETTENS NYCKEL (CommonMark 4.7): blanktecken i följd blir ETT
// blanksteg, ändarna trimmas, och etiketten viks till gemener — för ASCII och
// Latin-1 U+00C0–U+00DE (utom ×), inte fullständig Unicode-vikning (skriven
// gräns: "ẞ" och "ss" är olika etiketter här).
std::string labelKey(std::string_view s)
{
    std::string ut;
    bool        blank = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == ' ' || c == '\t' || c == '\n') {
            blank = !ut.empty();
            continue;
        }
        if (blank) {
            ut += ' ';
            blank = false;
        }
        if (c >= 'A' && c <= 'Z') {
            ut += char(c + 32);
        } else if (c == 0xC3 && i + 1 < s.size()) {
            const auto d = static_cast<unsigned char>(s[i + 1]);
            ut += char(c);
            ut += char(d >= 0x80 && d <= 0x9E && d != 0x97 ? d + 0x20 : d);
            ++i;
        } else {
            ut += char(c);
        }
    }
    return ut;
}

// ------------------------------------------------ länkens delar (delade)

// Blanksteg och tabbar, högst EN radbrytning, och blanksteg igen.
std::size_t skipSpnl(std::string_view s, std::size_t i)
{
    while (i < s.size() && isSpaceOrTab(s[i]))
        ++i;
    if (i < s.size() && s[i] == '\n') {
        ++i;
        while (i < s.size() && isSpaceOrTab(s[i]))
            ++i;
    }
    return i;
}

// En länketikett `[…]` vid `i` (CommonMark 4.7): högst `kMaxLabel` tecken,
// inga oescapade hakparenteser. `i` flyttas förbi `]`; `ut` är råtexten.
bool linkLabel(std::string_view s, std::size_t& i, std::string& ut)
{
    if (i >= s.size() || s[i] != '[')
        return false;
    std::size_t k   = i + 1;
    std::size_t len = 0;
    while (k < s.size() && s[k] != '[' && s[k] != ']') {
        if (s[k] == '\\' && k + 1 < s.size() && isAsciiPunct(s[k + 1])) {
            k += 2;
            len += 2;
        } else {
            ++k;
            ++len;
        }
        if (len > kMaxLabel)
            return false;
    }
    if (k >= s.size() || s[k] != ']')
        return false;
    ut.assign(s.substr(i + 1, k - i - 1));
    i = k + 1;
    return true;
}

// Länkadressen vid `i` (CommonMark 6.3): `<…>` utan radbrytning och oescapat
// `<`, eller en följd utan blanktecken och kontrolltecken med balanserade
// parenteser, högst `kMaxNesting` djupa. `ut` är RÅTEXTEN (escapes kvar), `n`
// antalet förbrukade tecken. Tom är tillåten i en länk, inte i en definition
// (där bara som `<>`).
bool linkDestination(std::string_view s, std::size_t i, std::string_view& ut,
                     std::size_t& n, bool tomTillåten)
{
    const std::size_t a = i;
    if (i < s.size() && s[i] == '<') {
        ++i;
        while (i < s.size()) {
            if (s[i] == '>') {
                ut = s.substr(a + 1, i - a - 1);
                n  = i + 1 - a;
                return true;
            }
            if (s[i] == '\\' && i + 1 < s.size() && isAsciiPunct(s[i + 1])) {
                i += 2;
                continue;
            }
            if (s[i] == '\n' || s[i] == '<')
                return false;
            ++i;
        }
        return false;
    }
    int djup = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c == '\\' && i + 1 < s.size() && isAsciiPunct(s[i + 1])) {
            i += 2;
            continue;
        }
        if (c == '(') {
            if (++djup > kMaxNesting)
                return false;
            ++i;
            continue;
        }
        if (c == ')') {
            if (djup == 0)
                break;
            --djup;
            ++i;
            continue;
        }
        if (c <= 0x20 || c == 0x7F)
            break;
        ++i;
    }
    if (djup != 0 || (i == a && !tomTillåten))
        return false;
    ut = s.substr(a, i - a);
    n  = i - a;
    return true;
}

// Titeln vid `i` (CommonMark 6.3): `"…"`, `'…'` eller `(…)`, escapes tillåtna.
bool linkTitle(std::string_view s, std::size_t i, std::string_view& ut,
               std::size_t& n)
{
    if (i >= s.size())
        return false;
    const char öppna = s[i];
    char       stäng = 0;
    if (öppna == '"' || öppna == '\'')
        stäng = öppna;
    else if (öppna == '(')
        stäng = ')';
    else
        return false;
    for (std::size_t k = i + 1; k < s.size(); ++k) {
        const char c = s[k];
        if (c == '\\' && k + 1 < s.size()) {
            ++k;
            continue;
        }
        if (c == stäng) {
            ut = s.substr(i + 1, k - i - 1);
            n  = k + 1 - i;
            return true;
        }
        if (öppna == '(' && c == '(')
            return false;
    }
    return false;
}

// ------------------------------------------------------------ tabellerna

// En tabellrad delad i celler (GFM 4.10): en inledande och en avslutande `|`
// är valfria, `\|` blir `|` i cellen (andra escapes står kvar till inline),
// och cellerna trimmas.
std::vector<std::string> splitRow(std::string_view s)
{
    s = trimView(s);
    if (!s.empty() && s.front() == '|')
        s.remove_prefix(1);
    std::vector<std::string> celler;
    std::string              cell;
    bool                     sistRör = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            if (s[i + 1] != '|')
                cell += c;
            cell += s[i + 1];
            ++i;
            sistRör = false;
            continue;
        }
        if (c == '|') {
            celler.emplace_back(trimView(cell));
            cell.clear();
            sistRör = true;
            continue;
        }
        cell += c;
        sistRör = isSpaceOrTab(c) && sistRör;
    }
    if (!sistRör)
        celler.emplace_back(trimView(cell));
    return celler;
}

// AVGRÄNSNINGSRADEN (GFM 4.10): celler av bara `-` med ett valfritt `:` i
// början och slutet — och minst en `|` på raden, så att en setext-linje eller
// en tematisk linje aldrig blir en tabell.
bool delimiterRow(std::string_view s, std::vector<CellAlign>& align)
{
    if (s.find('|') == kNpos)
        return false;
    for (const std::string& cell : splitRow(s)) {
        if (cell.empty())
            return false;
        const bool        v = cell.front() == ':';
        const bool        h = cell.size() > 1 && cell.back() == ':';
        const std::size_t a = v ? 1 : 0;
        const std::size_t b = cell.size() - (h ? 1 : 0);
        if (a >= b)
            return false;
        for (std::size_t k = a; k < b; ++k)
            if (cell[k] != '-')
                return false;
        align.push_back(v && h ? CellAlign::Center
                               : h ? CellAlign::Right : CellAlign::Left);
    }
    return !align.empty();
}

// ============================================================= blockfasen

struct ListData {
    bool ordered      = false;
    char marker       = 0;   // punktens tecken, eller numrets avgränsare
    int  start        = 1;
    int  markerOffset = 0;   // markörens indrag i kolumner
    int  padding      = 0;   // markören och luften — innehållets kolumn
};

struct Node {
    enum class T : std::uint8_t {
        Document,
        Quote,
        List,
        Item,
        Paragraph,
        Heading,
        Code,
        Comment,
        Rule,
        Table
    };
    explicit Node(T t) : type(t) {}

    T                                  type;
    Node*                              parent = nullptr;
    std::vector<std::unique_ptr<Node>> kids;
    bool                               open          = true;
    bool                               lastLineBlank = false;
    int                                startLine     = 0;
    int                                depth         = 0;
    std::string                        content;   // raderna, var och en med '\n'
    ListData                           list;
    bool                               tight       = true;
    int                                level       = 0;
    bool                               setext      = false;
    bool                               fenced      = false;
    bool                               infoLine    = false;   // staketets rad väntar
    char                               fenceChar   = 0;
    int                                fenceLength = 0;
    int                                fenceOffset = 0;
    std::vector<std::string>           rows;   // tabellens rader, rad 0 = huvudet
    std::vector<CellAlign>             align;
};

bool canContain(Node::T förälder, Node::T barn)
{
    switch (förälder) {
    case Node::T::Document:
    case Node::T::Quote:
    case Node::T::Item:
        return barn != Node::T::Item;
    case Node::T::List:
        return barn == Node::T::Item;
    default:
        return false;
    }
}

// Slutar blocket med en tom rad? För en lista eller listpost avgör dess SISTA
// barn (cmark: `S_ends_with_blank_line`).
bool endsWithBlankLine(const Node* n)
{
    while (n) {
        if (n->type != Node::T::List && n->type != Node::T::Item)
            return n->lastLineBlank;
        n = n->kids.empty() ? nullptr : n->kids.back().get();
    }
    return false;
}

// TÄT ELLER GLES (CommonMark 5.3): gles när två poster skiljs av en tom rad,
// eller när en post har två block med en tom rad emellan.
bool computeTight(const Node* list)
{
    for (std::size_t i = 0; i < list->kids.size(); ++i) {
        const Node* post      = list->kids[i].get();
        const bool  sistaPost = i + 1 == list->kids.size();
        if (post->lastLineBlank && !sistaPost)
            return false;
        for (std::size_t k = 0; k < post->kids.size(); ++k) {
            const bool sistaBarn = k + 1 == post->kids.size();
            if ((!sistaPost || !sistaBarn) &&
                endsWithBlankLine(post->kids[k].get()))
                return false;
        }
    }
    return true;
}

// Tomma rader sist i en indragen kod hör inte till koden.
void trimTrailingBlankLines(std::string& c)
{
    while (!c.empty()) {
        const std::size_t slut  = c.size() - 1;   // radens '\n'
        const std::size_t förra = slut == 0 ? kNpos : c.rfind('\n', slut - 1);
        const std::size_t start = förra == kNpos ? 0 : förra + 1;
        if (c.find_first_not_of(" \t", start) < slut)
            return;
        c.erase(start);
    }
}

using Refs = std::unordered_map<std::string, Link>;

class BlockParser {
public:
    explicit BlockParser(Refs& refs)
        : refs_(refs), root_(std::make_unique<Node>(Node::T::Document))
    {
        current_ = root_.get();
    }

    void processLine(std::string_view line);
    std::unique_ptr<Node> finish();

private:
    char peek(std::size_t i) const { return i < ln_.size() ? ln_[i] : '\n'; }
    bool atLineEnd(std::size_t i) const { return i >= ln_.size(); }

    void  findFirstNonspace();
    void  advanceOffset(int count, bool columns);
    void  addLine(Node* n);
    Node* addChild(Node* parent, Node::T type);
    Node* finalize(Node* b);
    void  detach(Node* b);
    int   depthFor(const Node* c, Node::T type) const;

    Node* checkOpenBlocks(bool& allMatched, bool& lineDone);
    bool  quotePrefix();
    bool  itemPrefix(const Node* c);
    bool  codePrefix(Node* c, bool& lineDone);
    bool  openNewBlocks(Node*& container, bool allMatched);
    void  addTextToContainer(Node* container, Node* lastMatched);

    int  atxStart(std::size_t pos, int& level) const;
    int  openingFence(std::size_t pos) const;
    int  closingFence(std::size_t pos, char ch) const;
    int  setextLine(std::size_t pos) const;
    bool thematicBreak(std::size_t pos) const;
    int  listMarker(std::size_t pos, bool interrupts, ListData& d) const;
    bool tryTable(Node* para);

    bool        extractRefDefs(std::string& content);
    std::size_t refDef(std::string_view s, std::size_t start);

    Refs&                              refs_;
    std::unique_ptr<Node>              root_;
    // Ett stycke som bara bar definitioner LÖSES UR trädet — men lever här,
    // för `current_` och den senast matchade behållaren får peka på det
    // (cmark jämför pekarna efter att det frigjorts).
    std::vector<std::unique_ptr<Node>> graveyard_;
    Node*                              current_ = nullptr;   // spetsen

    std::string_view ln_;
    std::size_t      offset_              = 0;
    int              column_              = 0;
    std::size_t      firstNonspace_       = 0;
    int              firstNonspaceColumn_ = 0;
    int              indent_              = 0;
    bool             blank_               = false;
    bool             partialTab_          = false;
    int              lineNumber_          = 0;
};

void BlockParser::findFirstNonspace()
{
    std::size_t i       = offset_;
    int         col     = column_;
    int         tillTab = kTabStop - (column_ % kTabStop);
    for (;;) {
        const char c = peek(i);
        if (c == ' ') {
            ++i;
            ++col;
            if (--tillTab == 0)
                tillTab = kTabStop;
        } else if (c == '\t') {
            ++i;
            col += tillTab;
            tillTab = kTabStop;
        } else {
            break;
        }
    }
    firstNonspace_       = i;
    firstNonspaceColumn_ = col;
    indent_              = col - column_;
    blank_               = atLineEnd(i);
}

// Flytta fram `count` tecken — eller, med `columns`, `count` KOLUMNER: en tabb
// kan då förbrukas delvis (`>\tfoo` — tabben är ett blanksteg efter `>` och
// två kolumners indrag).
void BlockParser::advanceOffset(int count, bool columns)
{
    while (count > 0 && !atLineEnd(offset_)) {
        if (ln_[offset_] == '\t') {
            const int tillTab = kTabStop - (column_ % kTabStop);
            if (columns) {
                partialTab_    = tillTab > count;
                const int steg = std::min(count, tillTab);
                column_ += steg;
                offset_ += partialTab_ ? 0 : 1;
                count -= steg;
            } else {
                partialTab_ = false;
                column_ += tillTab;
                offset_ += 1;
                count -= 1;
            }
        } else {
            partialTab_ = false;
            offset_ += 1;
            column_ += 1;
            count -= 1;
        }
    }
}

void BlockParser::addLine(Node* n)
{
    if (partialTab_) {
        ++offset_;   // resten av tabben blir blanksteg
        const int tillTab = kTabStop - (column_ % kTabStop);
        n->content.append(static_cast<std::size_t>(tillTab), ' ');
    }
    if (offset_ < ln_.size())
        n->content.append(ln_.substr(offset_));
    n->content += '\n';
}

Node* BlockParser::addChild(Node* parent, Node::T type)
{
    while (!canContain(parent->type, type))
        parent = finalize(parent);
    auto n       = std::make_unique<Node>(type);
    n->parent    = parent;
    n->startLine = lineNumber_;
    n->depth     = parent->depth + 1;
    Node* p      = n.get();
    parent->kids.push_back(std::move(n));
    return p;
}

// Djupet en ny behållare av `type` får under `c` — under den närmaste som kan
// bära den, för `addChild` stänger de andra.
int BlockParser::depthFor(const Node* c, Node::T type) const
{
    while (c && !canContain(c->type, type))
        c = c->parent;
    return c ? c->depth + 1 : 0;
}

void BlockParser::detach(Node* b)
{
    if (!b->parent)
        return;
    auto& kids = b->parent->kids;
    for (auto it = kids.end(); it != kids.begin();) {
        --it;
        if (it->get() == b) {
            graveyard_.push_back(std::move(*it));
            kids.erase(it);
            return;
        }
    }
}

Node* BlockParser::finalize(Node* b)
{
    Node* parent = b->parent;
    b->open      = false;
    switch (b->type) {
    case Node::T::Paragraph:
        if (!extractRefDefs(b->content))
            detach(b);   // bara definitioner — inget stycke
        break;
    case Node::T::Code:
        if (!b->fenced)
            trimTrailingBlankLines(b->content);
        break;
    case Node::T::List:
        b->tight = computeTight(b);
        break;
    default:
        break;
    }
    return parent;
}

void BlockParser::processLine(std::string_view line)
{
    ln_                  = line;
    offset_              = 0;
    column_              = 0;
    firstNonspace_       = 0;
    firstNonspaceColumn_ = 0;
    indent_              = 0;
    blank_               = false;
    partialTab_          = false;
    ++lineNumber_;

    bool  allMatched  = false;
    bool  lineDone    = false;
    Node* lastMatched = checkOpenBlocks(allMatched, lineDone);
    if (lineDone || !lastMatched)
        return;
    Node* container = lastMatched;
    if (!openNewBlocks(container, allMatched))
        return;   // raden förbrukades (tabellens avgränsningsrad)
    addTextToContainer(container, lastMatched);
}

// De ÖPPNA behållarna uppifrån: varje som matchar sitt prefix (`>`,
// listpostens indrag, kodens) tar det ur raden. Svaret är den sista som
// matchade; `allMatched` säger om alla gjorde det, och `lineDone` att ett
// stängande staket förbrukade raden.
Node* BlockParser::checkOpenBlocks(bool& allMatched, bool& lineDone)
{
    Node* container = root_.get();
    allMatched      = false;
    lineDone        = false;
    while (!container->kids.empty() && container->kids.back()->open) {
        container = container->kids.back().get();
        findFirstNonspace();
        bool matchar = true;
        switch (container->type) {
        case Node::T::Quote:
            matchar = quotePrefix();
            break;
        case Node::T::Item:
            matchar = itemPrefix(container);
            break;
        case Node::T::Code:
            matchar = codePrefix(container, lineDone);
            break;
        case Node::T::Heading:
            matchar = false;   // en rubrik är en rad
            break;
        case Node::T::Paragraph:
        case Node::T::Table:
            matchar = !blank_;
            break;
        default:   // listan (posterna avgör), kommentaren, linjen
            break;
        }
        if (lineDone)
            return nullptr;
        if (!matchar)
            return container->parent;
    }
    allMatched = true;
    return container;
}

bool BlockParser::quotePrefix()
{
    if (indent_ > 3 || peek(firstNonspace_) != '>')
        return false;
    advanceOffset(indent_ + 1, true);
    if (isSpaceOrTab(peek(offset_)))
        advanceOffset(1, true);
    return true;
}

bool BlockParser::itemPrefix(const Node* c)
{
    const int kolumn = c->list.markerOffset + c->list.padding;
    if (indent_ >= kolumn) {
        advanceOffset(kolumn, true);
        return true;
    }
    if (blank_ && !c->kids.empty()) {
        advanceOffset(int(firstNonspace_ - offset_), false);
        return true;
    }
    return false;
}

bool BlockParser::codePrefix(Node* c, bool& lineDone)
{
    if (!c->fenced) {
        if (indent_ >= kCodeIndent) {
            advanceOffset(kCodeIndent, true);
            return true;
        }
        if (blank_) {
            advanceOffset(int(firstNonspace_ - offset_), false);
            return true;
        }
        return false;
    }
    if (indent_ <= 3 && peek(firstNonspace_) == c->fenceChar &&
        closingFence(firstNonspace_, c->fenceChar) >= c->fenceLength) {
        lineDone = true;   // det stängande staketet — raden är förbrukad
        current_ = finalize(c);
        return false;
    }
    for (int i = c->fenceOffset; i > 0 && isSpaceOrTab(peek(offset_)); --i)
        advanceOffset(1, true);
    return true;
}

int BlockParser::atxStart(std::size_t pos, int& level) const
{
    std::size_t i = pos;
    while (peek(i) == '#')
        ++i;
    const int n = int(i - pos);
    if (n < 1 || n > 6 || (!atLineEnd(i) && !isSpaceOrTab(peek(i))))
        return 0;
    level = n;
    while (isSpaceOrTab(peek(i)))
        ++i;
    return int(i - pos);
}

int BlockParser::openingFence(std::size_t pos) const
{
    const char c = peek(pos);
    if (c != '`' && c != '~')
        return 0;
    std::size_t i = pos;
    while (peek(i) == c)
        ++i;
    const int n = int(i - pos);
    if (n < 3 || (c == '`' && ln_.find('`', i) != kNpos))
        return 0;
    return n;
}

int BlockParser::closingFence(std::size_t pos, char ch) const
{
    std::size_t i = pos;
    while (peek(i) == ch)
        ++i;
    const int n = int(i - pos);
    while (isSpaceOrTab(peek(i)))
        ++i;
    return n >= 3 && atLineEnd(i) ? n : 0;
}

int BlockParser::setextLine(std::size_t pos) const
{
    const char c = peek(pos);
    if (c != '=' && c != '-')
        return 0;
    std::size_t i = pos;
    while (peek(i) == c)
        ++i;
    while (isSpaceOrTab(peek(i)))
        ++i;
    return atLineEnd(i) ? (c == '=' ? 1 : 2) : 0;
}

bool BlockParser::thematicBreak(std::size_t pos) const
{
    const char c = peek(pos);
    if (c != '*' && c != '-' && c != '_')
        return false;
    int n = 0;
    for (std::size_t i = pos; !atLineEnd(i); ++i) {
        if (ln_[i] == c)
            ++n;
        else if (!isSpaceOrTab(ln_[i]))
            return false;
    }
    return n >= 3;
}

// LISTMARKÖREN (CommonMark 5.2): `-`, `+` eller `*`, eller 1–9 siffror och
// `.`/`)`, följt av blanktecken eller radslut. En post som AVBRYTER ett stycke
// får inte vara tom, och en numrerad måste då börja på 1.
int BlockParser::listMarker(std::size_t pos, bool interrupts, ListData& d) const
{
    const std::size_t start = pos;
    const char        c     = peek(pos);
    if (c == '*' || c == '-' || c == '+') {
        ++pos;
        d.ordered = false;
        d.marker  = c;
        d.start   = 0;
    } else if (isAsciiDigit(c)) {
        int v      = 0;
        int siffor = 0;
        do {
            v = v * 10 + (peek(pos) - '0');
            ++pos;
            ++siffor;
        } while (siffor < kMaxDigits && isAsciiDigit(peek(pos)));
        if (interrupts && v != 1)
            return 0;
        const char avgr = peek(pos);
        if (avgr != '.' && avgr != ')')
            return 0;
        ++pos;
        d.ordered = true;
        d.marker  = avgr;
        d.start   = v;
    } else {
        return 0;
    }
    if (!atLineEnd(pos) && !isSpaceOrTab(peek(pos)))
        return 0;
    if (interrupts) {
        std::size_t i = pos;
        while (isSpaceOrTab(peek(i)))
            ++i;
        if (atLineEnd(i))
            return 0;
    }
    return int(pos - start);
}

// NYA BLOCK, så många som raden öppnar (`> - > text` är tre behållare). Svaret
// är false när raden förbrukades helt — tabellens avgränsningsrad.
bool BlockParser::openNewBlocks(Node*& container, bool allMatched)
{
    bool maybeLazy = current_->type == Node::T::Paragraph;
    while (container->type != Node::T::Code &&
           container->type != Node::T::Comment) {
        findFirstNonspace();
        const bool indented = indent_ >= kCodeIndent;
        const char c        = peek(firstNonspace_);
        int        n        = 0;
        int        nivå     = 0;
        ListData   data;
        if (!indented && c == '>' &&
            depthFor(container, Node::T::Quote) <= kMaxNesting) {
            advanceOffset(int(firstNonspace_ + 1 - offset_), false);
            if (isSpaceOrTab(peek(offset_)))
                advanceOffset(1, true);
            container = addChild(container, Node::T::Quote);
        } else if (!indented && (n = atxStart(firstNonspace_, nivå)) != 0) {
            advanceOffset(int(firstNonspace_ + std::size_t(n) - offset_), false);
            container        = addChild(container, Node::T::Heading);
            container->level = nivå;
        } else if (!indented && (n = openingFence(firstNonspace_)) != 0) {
            container              = addChild(container, Node::T::Code);
            container->fenced      = true;
            container->infoLine    = true;   // resten av raden: info, läses inte
            container->fenceChar   = c;
            container->fenceLength = n;
            container->fenceOffset = int(firstNonspace_ - offset_);
            advanceOffset(int(firstNonspace_ + std::size_t(n) - offset_), false);
        } else if (!indented && startsAt(ln_, firstNonspace_, "<!--")) {
            // HTML-KOMMENTAREN som block (CommonMark 4.6, typ 2) — tas bort
            // ända till raden med `-->`. Annan HTML är text.
            container = addChild(container, Node::T::Comment);
        } else if (!indented && container->type == Node::T::Paragraph &&
                   (nivå = setextLine(firstNonspace_)) != 0) {
            if (extractRefDefs(container->content)) {
                container->type   = Node::T::Heading;
                container->level  = nivå;
                container->setext = true;
                advanceOffset(int(ln_.size() - offset_), false);
            }
        } else if (!indented &&
                   !(container->type == Node::T::Paragraph && !allMatched) &&
                   thematicBreak(firstNonspace_)) {
            container = addChild(container, Node::T::Rule);
            advanceOffset(int(ln_.size() - offset_), false);
        } else if (indent_ < kCodeIndent &&
                   (n = listMarker(firstNonspace_,
                                   container->type == Node::T::Paragraph,
                                   data)) != 0 &&
                   (container->type == Node::T::List
                        ? container->depth + 1
                        : depthFor(container, Node::T::List) + 1) <=
                       kMaxNesting) {
            advanceOffset(int(firstNonspace_ + std::size_t(n) - offset_), false);
            const bool        sparadTab    = partialTab_;
            const std::size_t sparadOffset = offset_;
            const int         sparadKolumn = column_;
            while (column_ - sparadKolumn <= 5 && isSpaceOrTab(peek(offset_)))
                advanceOffset(1, true);
            const int luft = column_ - sparadKolumn;
            if (luft >= 5 || luft < 1 || atLineEnd(offset_)) {
                // Indragen kod i posten, eller en tom rad: innehållet börjar
                // ett blanksteg efter markören (CommonMark 5.2 regel 2–3).
                data.padding = n + 1;
                offset_      = sparadOffset;
                column_      = sparadKolumn;
                partialTab_  = sparadTab;
                if (luft > 0)
                    advanceOffset(1, true);
            } else {
                data.padding = n + luft;
            }
            data.markerOffset = indent_;
            if (container->type != Node::T::List ||
                container->list.ordered != data.ordered ||
                container->list.marker != data.marker) {
                container       = addChild(container, Node::T::List);
                container->list = data;
            }
            container       = addChild(container, Node::T::Item);
            container->list = data;
        } else if (indented && !maybeLazy && !blank_) {
            advanceOffset(kCodeIndent, true);
            container = addChild(container, Node::T::Code);
        } else if (!indented && container->type == Node::T::Paragraph &&
                   tryTable(container)) {
            return false;
        } else {
            break;
        }
        const Node::T t = container->type;
        if (t != Node::T::Quote && t != Node::T::List && t != Node::T::Item)
            break;   // ett löv tar inga fler block på raden
        maybeLazy = false;
    }
    return true;
}

// GFM-TABELLEN: raden är en avgränsningsrad och styckets SISTA rad ett huvud
// med lika många celler. Föregående rader står kvar som ett stycke.
bool BlockParser::tryTable(Node* para)
{
    std::vector<CellAlign> align;
    if (!delimiterRow(ln_.substr(firstNonspace_), align))
        return false;
    if (!extractRefDefs(para->content))
        return false;
    std::string_view       text(para->content);
    while (!text.empty() && text.back() == '\n')
        text.remove_suffix(1);
    const std::size_t      nl     = text.rfind('\n');
    const std::string_view huvud  = nl == kNpos ? text : text.substr(nl + 1);
    if (splitRow(huvud).size() != align.size())
        return false;
    const std::string header(huvud);
    Node*             table = nullptr;
    if (nl == kNpos) {
        // Stycket BLIR tabellen — pekarna på det står sig.
        para->type = Node::T::Table;
        para->content.clear();
        table = para;
    } else {
        para->content = std::string(text.substr(0, nl + 1));
        finalize(para);
        table = addChild(para->parent, Node::T::Table);
    }
    table->align = std::move(align);
    table->rows.push_back(header);
    table->lastLineBlank = false;
    for (Node* p = table; p->parent; p = p->parent)
        p->parent->lastLineBlank = false;
    current_ = table;
    return true;
}

void BlockParser::addTextToContainer(Node* container, Node* lastMatched)
{
    findFirstNonspace();
    if (blank_ && !container->kids.empty())
        container->kids.back()->lastLineBlank = true;
    const Node::T t = container->type;
    // En tom rad räknas inte i ett citat (raden bar `>`), en rubrik, en
    // linje, ett staket — eller en listpost som just öppnats tom.
    container->lastLineBlank =
        blank_ && t != Node::T::Quote && t != Node::T::Heading &&
        t != Node::T::Rule && !(t == Node::T::Code && container->fenced) &&
        !(t == Node::T::Item && container->kids.empty() &&
          container->startLine == lineNumber_);
    for (Node* p = container; p->parent; p = p->parent)
        p->parent->lastLineBlank = false;

    // LAT FORTSÄTTNINGSRAD (CommonMark 5.1): ett stycke fortsätter också när
    // behållarnas prefix saknas, så länge inget nytt block öppnades.
    if (current_ != lastMatched && container == lastMatched && !blank_ &&
        current_->type == Node::T::Paragraph) {
        addLine(current_);
        return;
    }
    while (current_ != lastMatched)
        current_ = finalize(current_);

    switch (t) {
    case Node::T::Code:
        if (container->infoLine)
            container->infoLine = false;
        else
            addLine(container);
        break;
    case Node::T::Comment:
        if (offset_ <= ln_.size() && ln_.find("-->", offset_) != kNpos)
            container = finalize(container);
        break;
    default:
        if (blank_)
            break;
        if (t == Node::T::Heading && !container->setext) {
            // ATX: raden utan den avslutande följden `#` (som kräver ett
            // blanksteg före sig) och utan blanksteg i ändarna.
            std::string_view rest = trimView(ln_.substr(firstNonspace_));
            const std::size_t hash = rest.find_last_not_of('#');
            if (hash == kNpos)
                rest = {};
            else if (hash + 1 < rest.size() && isSpaceOrTab(rest[hash]))
                rest = trimView(rest.substr(0, hash));
            container->content.assign(rest);
            container->content += '\n';
        } else if (t == Node::T::Paragraph || t == Node::T::Heading) {
            advanceOffset(int(firstNonspace_ - offset_), false);
            addLine(container);
        } else if (t == Node::T::Table) {
            container->rows.emplace_back(ln_.substr(firstNonspace_));
        } else {
            container = addChild(container, Node::T::Paragraph);
            advanceOffset(int(firstNonspace_ - offset_), false);
            addLine(container);
        }
        break;
    }
    current_ = container;
}

// REFERENSDEFINITIONERNA i styckets början (CommonMark 4.7) — registrerade
// (den FÖRSTA vinner) och borttagna ur texten. Svaret: finns det text kvar?
bool BlockParser::extractRefDefs(std::string& content)
{
    std::size_t pos = 0;
    while (pos < content.size() && content[pos] == '[') {
        const std::size_t n = refDef(content, pos);
        if (n == 0)
            break;
        pos += n;
    }
    if (pos)
        content.erase(0, pos);
    return content.find_first_not_of(" \t\n") != std::string::npos;
}

std::size_t BlockParser::refDef(std::string_view s, std::size_t start)
{
    std::size_t i = start;
    std::string etikett;
    if (!linkLabel(s, i, etikett) || labelKey(etikett).empty())
        return 0;
    if (i >= s.size() || s[i] != ':')
        return 0;
    i = skipSpnl(s, i + 1);
    std::string_view rå;
    std::size_t      n = 0;
    if (!linkDestination(s, i, rå, n, false))
        return 0;
    i += n;
    const std::size_t föreTitel = i;
    std::string_view  titel;
    std::size_t       tn = 0;
    const std::size_t t  = skipSpnl(s, i);
    bool harTitel = t != föreTitel && linkTitle(s, t, titel, tn);
    std::size_t j = harTitel ? t + tn : föreTitel;
    while (j < s.size() && isSpaceOrTab(s[j]))
        ++j;
    if (j < s.size() && s[j] != '\n') {
        if (!harTitel)
            return 0;
        // Titeln var inte sist på sin rad — definitionen slutar före den.
        harTitel = false;
        titel    = {};
        j        = föreTitel;
        while (j < s.size() && isSpaceOrTab(s[j]))
            ++j;
        if (j < s.size() && s[j] != '\n')
            return 0;
    }
    if (j < s.size())
        ++j;   // radbrytningen
    refs_.emplace(labelKey(etikett), Link{unescape(rå), unescape(titel)});
    return j - start;
}

std::unique_ptr<Node> BlockParser::finish()
{
    while (current_ && current_ != root_.get())
        current_ = finalize(current_);
    finalize(root_.get());
    return std::move(root_);
}

// ============================================================ inlinefasen

struct INode {
    enum class K : std::uint8_t {
        Root,
        Text,
        Code,
        Soft,
        Hard,
        Emph,
        Strong,
        Link,
        Image
    };
    K           k = K::Text;
    std::string text;
    int         ref    = -1;   // Link: Document::links · Image: Document::images
    int         parent = -1;
    int         prev   = -1;
    int         next   = -1;
    int         first  = -1;
    int         last   = -1;
};

// En följd `*` eller `_` på avgränsarstacken.
struct Delim {
    int  node;      // textnoden som bär tecknen
    char ch;
    int  count;     // kvar
    int  orig;      // följdens längd — regeln om tre
    bool canOpen;
    bool canClose;
    int  prev = -1;
    int  next = -1;
};

// En `[` eller `![` på hakparentesstacken.
struct Bracket {
    int         node;
    int         prevDelim;   // avgränsarstackens topp när den lades
    std::size_t pos;         // texten börjar här (efter `[`)
    bool        image;
    bool        active = true;
    bool        after  = false;   // en `[` kom efter den
};

class InlineParser {
public:
    InlineParser(const Refs& refs, Document& doc) : refs_(refs), doc_(doc) {}
    std::vector<Span> parse(std::string_view text);

private:
    int  newNode(INode::K k, std::string text = {});
    void appendChild(int parent, int c);
    void insertAfter(int ref, int c);
    void unlink(int c);
    void appendText(std::string_view t);
    void makeLink(const std::string& url, const std::string& text);

    bool special(std::size_t i) const;
    void textRun();
    void newline();
    void backslash();
    void entity();
    void backticks();
    void pointy();
    void delimiterRun(char c);
    void openBracket(bool image);
    void closeBracket();
    bool autolink();

    std::size_t findTicks(std::size_t from, std::size_t len);
    bool        uriAutolink(std::size_t p, std::size_t& len) const;
    bool        emailAutolink(std::size_t p, std::size_t& len) const;
    bool        extendedAutolink(std::size_t p, std::size_t& len) const;
    bool        inlineLink(std::size_t i, std::string& url, std::string& title,
                           std::size_t& end) const;

    void pushDelim(int node, char c, int count, bool open, bool close);
    void removeDelim(int i);
    void processEmphasis(int bottom);

    std::vector<Span> flatten() const;
    std::string       altText(int k) const;

    const Refs&          refs_;
    Document&            doc_;
    std::string_view     s_;
    std::size_t          p_ = 0;
    std::vector<INode>   n_;
    std::vector<Delim>   d_;
    int                  dHead_ = -1;
    int                  dTail_ = -1;
    std::vector<Bracket> b_;
    // Backtickföljderna per längd, letade EN gång — så att en kod utan
    // stängare aldrig söker om hela texten (linjärt i stället för kvadratiskt).
    bool                                                       ticksScanned_ = false;
    std::unordered_map<std::size_t, std::vector<std::size_t>> ticks_;
    std::unordered_map<std::size_t, std::size_t>              tickCursor_;
    bool noCommentEnd_ = false;   // ingen `-->` efter förra försöket
};

int InlineParser::newNode(INode::K k, std::string text)
{
    INode n;
    n.k    = k;
    n.text = std::move(text);
    n_.push_back(std::move(n));
    return int(n_.size()) - 1;
}

void InlineParser::appendChild(int parent, int c)
{
    n_[c].parent = parent;
    n_[c].prev   = n_[parent].last;
    n_[c].next   = -1;
    if (n_[parent].last >= 0)
        n_[n_[parent].last].next = c;
    else
        n_[parent].first = c;
    n_[parent].last = c;
}

void InlineParser::insertAfter(int ref, int c)
{
    const int parent = n_[ref].parent;
    const int nx     = n_[ref].next;
    n_[c].parent     = parent;
    n_[c].prev       = ref;
    n_[c].next       = nx;
    n_[ref].next     = c;
    if (nx >= 0)
        n_[nx].prev = c;
    else
        n_[parent].last = c;
}

void InlineParser::unlink(int c)
{
    INode&    n      = n_[c];
    const int parent = n.parent;
    if (n.prev >= 0)
        n_[n.prev].next = n.next;
    else if (parent >= 0)
        n_[parent].first = n.next;
    if (n.next >= 0)
        n_[n.next].prev = n.prev;
    else if (parent >= 0)
        n_[parent].last = n.prev;
    n.parent = n.prev = n.next = -1;
}

void InlineParser::appendText(std::string_view t)
{
    if (!t.empty())
        appendChild(0, newNode(INode::K::Text, std::string(t)));
}

void InlineParser::makeLink(const std::string& url, const std::string& text)
{
    doc_.links.push_back(Link{url, std::string()});
    const int ln = newNode(INode::K::Link);
    n_[ln].ref   = int(doc_.links.size()) - 1;
    appendChild(0, ln);
    appendChild(ln, newNode(INode::K::Text, text));
}

std::vector<Span> InlineParser::parse(std::string_view text)
{
    s_ = text;
    p_ = 0;
    n_.clear();
    d_.clear();
    dHead_ = dTail_ = -1;
    b_.clear();
    ticksScanned_ = false;
    ticks_.clear();
    tickCursor_.clear();
    noCommentEnd_ = false;
    newNode(INode::K::Root);

    while (p_ < s_.size()) {
        switch (s_[p_]) {
        case '\n': newline(); break;
        case '`': backticks(); break;
        case '\\': backslash(); break;
        case '&': entity(); break;
        case '<': pointy(); break;
        case '*':
        case '_': delimiterRun(s_[p_]); break;
        case '[': openBracket(false); break;
        case ']': closeBracket(); break;
        case '!':
            if (p_ + 1 < s_.size() && s_[p_ + 1] == '[') {
                openBracket(true);
                break;
            }
            textRun();
            break;
        default:
            if (!autolink())
                textRun();
            break;
        }
    }
    processEmphasis(-1);
    return flatten();
}

bool InlineParser::special(std::size_t i) const
{
    switch (s_[i]) {
    case '\n':
    case '`':
    case '\\':
    case '&':
    case '<':
    case '*':
    case '_':
    case '[':
    case ']':
        return true;
    case '!':
        return i + 1 < s_.size() && s_[i + 1] == '[';
    case 'h': {
        std::size_t len = 0;
        return extendedAutolink(i, len);
    }
    default:
        return false;
    }
}

// Vanlig text till nästa specialtecken. Före en radbrytning försvinner
// avslutande blanksteg — de avgör bara om brytningen är hård (`newline`).
void InlineParser::textRun()
{
    const std::size_t start = p_++;
    while (p_ < s_.size() && !special(p_))
        ++p_;
    std::size_t slut = p_;
    if (p_ < s_.size() && s_[p_] == '\n')
        while (slut > start && isSpaceOrTab(s_[slut - 1]))
            --slut;
    appendText(s_.substr(start, slut - start));
}

// MJUK eller HÅRD radbrytning (CommonMark 6.7–6.8): två blanksteg före den
// gör den hård; blanktecknen i nästa rads början hör inte till texten.
void InlineParser::newline()
{
    const std::size_t nl = p_++;
    const bool hård = nl >= 2 && s_[nl - 1] == ' ' && s_[nl - 2] == ' ';
    appendChild(0, newNode(hård ? INode::K::Hard : INode::K::Soft));
    while (p_ < s_.size() && isSpaceOrTab(s_[p_]))
        ++p_;
}

void InlineParser::backslash()
{
    ++p_;
    if (p_ < s_.size() && s_[p_] == '\n') {
        ++p_;
        appendChild(0, newNode(INode::K::Hard));
        while (p_ < s_.size() && isSpaceOrTab(s_[p_]))
            ++p_;
    } else if (p_ < s_.size() && isAsciiPunct(s_[p_])) {
        appendText(s_.substr(p_, 1));
        ++p_;
    } else {
        appendText("\\");
    }
}

void InlineParser::entity()
{
    std::string ut;
    if (const std::size_t n = matchEntity(s_, p_, ut)) {
        appendText(ut);
        p_ += n;
        return;
    }
    appendText("&");
    ++p_;
}

std::size_t InlineParser::findTicks(std::size_t from, std::size_t len)
{
    if (!ticksScanned_) {
        ticksScanned_ = true;
        for (std::size_t i = 0; i < s_.size();) {
            if (s_[i] != '`') {
                ++i;
                continue;
            }
            const std::size_t a = i;
            while (i < s_.size() && s_[i] == '`')
                ++i;
            ticks_[i - a].push_back(a);
        }
    }
    const auto it = ticks_.find(len);
    if (it == ticks_.end())
        return kNpos;
    std::size_t& cur = tickCursor_[len];
    while (cur < it->second.size() && it->second[cur] < from)
        ++cur;
    return cur < it->second.size() ? it->second[cur] : kNpos;
}

// KODSPANNET (CommonMark 6.1): en backtickföljd till nästa följd av SAMMA
// längd. Radbrytningar blir blanksteg, och ett blanksteg i varje ände tas
// bort när innehållet inte bara är blanksteg. Inga escapes inuti.
void InlineParser::backticks()
{
    const std::size_t start = p_;
    while (p_ < s_.size() && s_[p_] == '`')
        ++p_;
    const std::size_t len   = p_ - start;
    const std::size_t stäng = findTicks(p_, len);
    if (stäng == kNpos) {
        appendText(s_.substr(start, len));
        return;
    }
    std::string kod(s_.substr(p_, stäng - p_));
    for (char& c : kod)
        if (c == '\n')
            c = ' ';
    if (kod.size() >= 2 && kod.front() == ' ' && kod.back() == ' ' &&
        kod.find_first_not_of(' ') != std::string::npos)
        kod = kod.substr(1, kod.size() - 2);
    appendChild(0, newNode(INode::K::Code, std::move(kod)));
    p_ = stäng + len;
}

// `<`: en HTML-KOMMENTAR (tas bort), en AUTOLÄNK (`<https://…>`, `<a@b.se>`)
// — annars ett vanligt tecken. Annan HTML är text.
void InlineParser::pointy()
{
    if (startsAt(s_, p_, "<!--")) {
        std::size_t slut = kNpos;
        if (startsAt(s_, p_, "<!-->"))
            slut = p_ + 5;
        else if (startsAt(s_, p_, "<!--->"))
            slut = p_ + 6;
        else if (!noCommentEnd_) {
            const std::size_t e = s_.find("-->", p_ + 4);
            if (e == kNpos)
                noCommentEnd_ = true;
            else
                slut = e + 3;
        }
        if (slut != kNpos) {
            p_ = slut;
            return;
        }
    }
    std::size_t len = 0;
    if (uriAutolink(p_, len)) {
        const std::string uri(s_.substr(p_ + 1, len - 2));
        makeLink(uri, uri);
        p_ += len;
        return;
    }
    if (emailAutolink(p_, len)) {
        const std::string adress(s_.substr(p_ + 1, len - 2));
        makeLink("mailto:" + adress, adress);
        p_ += len;
        return;
    }
    appendText("<");
    ++p_;
}

// `<schema:…>` (CommonMark 6.5): ett schema på 2–32 tecken och sedan inga
// blanktecken, kontrolltecken eller vinkelparenteser.
bool InlineParser::uriAutolink(std::size_t p, std::size_t& len) const
{
    std::size_t i = p + 1;
    if (i >= s_.size() || !isAsciiAlpha(s_[i]))
        return false;
    const std::size_t a = i;
    while (i < s_.size() && i - a <= 32 &&
           (isAsciiAlnum(s_[i]) || s_[i] == '+' || s_[i] == '.' || s_[i] == '-'))
        ++i;
    if (i - a < 2 || i - a > 32 || i >= s_.size() || s_[i] != ':')
        return false;
    for (++i; i < s_.size() && s_[i] != '>'; ++i) {
        const auto c = static_cast<unsigned char>(s_[i]);
        if (c <= 0x20 || c == '<' || c == 0x7F)
            return false;
    }
    if (i >= s_.size())
        return false;
    len = i + 1 - p;
    return true;
}

// `<namn@domän>` (CommonMark 6.5, HTML:s e-postregel).
bool InlineParser::emailAutolink(std::size_t p, std::size_t& len) const
{
    constexpr std::string_view kTillåtna = ".!#$%&'*+/=?^_`{|}~-";
    std::size_t                i         = p + 1;
    const std::size_t          a         = i;
    while (i < s_.size() &&
           (isAsciiAlnum(s_[i]) || kTillåtna.find(s_[i]) != kNpos))
        ++i;
    if (i == a || i >= s_.size() || s_[i] != '@')
        return false;
    ++i;
    for (;;) {
        const std::size_t b = i;
        while (i < s_.size() && (isAsciiAlnum(s_[i]) || s_[i] == '-'))
            ++i;
        if (i == b || i - b > 63 || s_[b] == '-' || s_[i - 1] == '-')
            return false;
        if (i < s_.size() && s_[i] == '.' && i + 1 < s_.size() &&
            isAsciiAlnum(s_[i + 1])) {
            ++i;
            continue;
        }
        break;
    }
    if (i >= s_.size() || s_[i] != '>')
        return false;
    len = i + 1 - p;
    return true;
}

// NAKEN `http://` eller `https://` (GFM 6.9, cmark-gfm:s regel för schemat —
// ingen punkt i domänen krävs): i radens början eller efter blanktecken, `*`,
// `_`, `~` eller `(`; en domän av bokstäver, siffror, `-`, `_` och `.` (inget
// `_` i de två sista leden), sedan allt till blanktecken eller `<`. Avslutande
// `?!.,:*_~` hör inte till länken, inte heller en `)` utan sin `(` eller en
// avslutande entitet. `www.` och nakna e-postadresser länkas inte (skriven
// gräns).
bool InlineParser::extendedAutolink(std::size_t p, std::size_t& len) const
{
    if (p > 0) {
        const char f = s_[p - 1];
        if (f != ' ' && f != '\t' && f != '\n' && f != '*' && f != '_' &&
            f != '~' && f != '(')
            return false;
    }
    std::size_t i = 0;
    if (startsAt(s_, p, "http://"))
        i = p + 7;
    else if (startsAt(s_, p, "https://"))
        i = p + 8;
    else
        return false;
    const std::size_t dom = i;
    while (i < s_.size() &&
           (isAsciiAlnum(s_[i]) || s_[i] == '-' || s_[i] == '_' ||
            s_[i] == '.' || static_cast<unsigned char>(s_[i]) >= 0x80))
        ++i;
    if (i == dom)
        return false;
    // Inget `_` i domänens två sista led.
    {
        std::string_view d = s_.substr(dom, i - dom);
        while (!d.empty() && d.back() == '.')
            d.remove_suffix(1);
        const std::size_t sista = d.rfind('.');
        const std::size_t näst =
            sista == kNpos || sista == 0 ? kNpos : d.rfind('.', sista - 1);
        const std::size_t från = näst == kNpos ? 0 : näst + 1;
        if (d.substr(från).find('_') != kNpos)
            return false;
    }
    while (i < s_.size()) {
        const auto c = static_cast<unsigned char>(s_[i]);
        if (c <= 0x20 || c == '<')
            break;
        ++i;
    }
    std::size_t slut = i;
    for (;;) {
        if (slut <= dom)
            return false;
        const char c = s_[slut - 1];
        if (c == '?' || c == '!' || c == '.' || c == ',' || c == ':' ||
            c == '*' || c == '_' || c == '~') {
            --slut;
            continue;
        }
        if (c == ')') {
            int öppna = 0;
            int stängda = 0;
            for (std::size_t k = p; k < slut; ++k) {
                öppna += s_[k] == '(' ? 1 : 0;
                stängda += s_[k] == ')' ? 1 : 0;
            }
            if (stängda > öppna) {
                --slut;
                continue;
            }
            break;
        }
        if (c == ';') {
            std::size_t k = slut - 1;
            while (k > p && isAsciiAlnum(s_[k - 1]))
                --k;
            if (k > p && k < slut - 1 && s_[k - 1] == '&') {
                slut = k - 1;
                continue;
            }
        }
        break;
    }
    if (slut <= dom)
        return false;
    len = slut - p;
    return true;
}

bool InlineParser::autolink()
{
    std::size_t len = 0;
    if (s_[p_] != 'h' || !extendedAutolink(p_, len))
        return false;
    const std::string url(s_.substr(p_, len));
    makeLink(url, url);
    p_ += len;
    return true;
}

// En följd `*` eller `_` (CommonMark 6.2): vänster- och högerflankerande ur
// tecknen före och efter, och för `_` regeln om ord (ingen betoning mitt i
// `snake_case_name`).
void InlineParser::delimiterRun(char c)
{
    const std::size_t start = p_;
    while (p_ < s_.size() && s_[p_] == c)
        ++p_;
    const int      count     = int(p_ - start);
    const char32_t före      = start == 0 ? U'\n' : codepointBefore(s_, start);
    const char32_t efter     = p_ >= s_.size() ? U'\n' : codepointAt(s_, p_);
    const bool     föreBlank = isUnicodeSpace(före);
    const bool     efterBlank = isUnicodeSpace(efter);
    const bool     föreSkilj = isPunct(före);
    const bool     efterSkilj = isPunct(efter);
    const bool vänster = !efterBlank && (!efterSkilj || föreBlank || föreSkilj);
    const bool höger   = !föreBlank && (!föreSkilj || efterBlank || efterSkilj);
    const bool öppnar  = c == '*' ? vänster : vänster && (!höger || föreSkilj);
    const bool stänger = c == '*' ? höger : höger && (!vänster || efterSkilj);
    const int  node    = newNode(INode::K::Text, std::string(s_.substr(start, p_ - start)));
    appendChild(0, node);
    if (öppnar || stänger)
        pushDelim(node, c, count, öppnar, stänger);
}

void InlineParser::pushDelim(int node, char c, int count, bool open, bool close)
{
    Delim d{node, c, count, count, open, close};
    d.prev = dTail_;
    d_.push_back(d);
    const int i = int(d_.size()) - 1;
    if (dTail_ >= 0)
        d_[dTail_].next = i;
    else
        dHead_ = i;
    dTail_ = i;
}

void InlineParser::removeDelim(int i)
{
    Delim& d = d_[i];
    if (d.prev >= 0)
        d_[d.prev].next = d.next;
    else
        dHead_ = d.next;
    if (d.next >= 0)
        d_[d.next].prev = d.prev;
    else
        dTail_ = d.prev;
    d.prev = d.next = -1;
}

// PROCESS EMPHASIS (CommonMark Appendix A) över avgränsarna OVANFÖR
// `bottom` (−1 = hela stacken). `golv` är `openers_bottom` per tecken,
// stängarens längd mod 3 och om stängaren kan öppna — en sökning som en gång
// misslyckats börjar aldrig om under den nivån, så 20 000 `*a` förblir linjärt.
void InlineParser::processEmphasis(int bottom)
{
    int golv[2][3][2];
    for (auto& a : golv)
        for (auto& b : a)
            for (int& c : b)
                c = bottom;
    int cur = bottom >= 0 ? d_[bottom].next : dHead_;
    while (cur >= 0) {
        if (!d_[cur].canClose) {
            cur = d_[cur].next;
            continue;
        }
        const char ch = d_[cur].ch;
        int&       g  = golv[ch == '*' ? 0 : 1][d_[cur].orig % 3]
                       [d_[cur].canOpen ? 1 : 0];
        int  op      = d_[cur].prev;
        bool hittad  = false;
        while (op >= 0 && op != bottom && op != g) {
            const Delim& o = d_[op];
            if (o.canOpen && o.ch == ch) {
                const Delim& c = d_[cur];
                // REGELN OM TRE (6.2 regel 9–10).
                if (!(c.canOpen || o.canClose) || c.orig % 3 == 0 ||
                    (o.orig + c.orig) % 3 != 0) {
                    hittad = true;
                    break;
                }
            }
            op = d_[op].prev;
        }
        if (!hittad) {
            g            = d_[cur].prev;
            const int nx = d_[cur].next;
            if (!d_[cur].canOpen)
                removeDelim(cur);
            cur = nx;
            continue;
        }
        const int use = d_[cur].count >= 2 && d_[op].count >= 2 ? 2 : 1;
        d_[op].count -= use;
        d_[cur].count -= use;
        const int on = d_[op].node;
        const int cn = d_[cur].node;
        n_[on].text.resize(std::size_t(d_[op].count));
        n_[cn].text.resize(std::size_t(d_[cur].count));
        const int em = newNode(use == 2 ? INode::K::Strong : INode::K::Emph);
        insertAfter(on, em);
        for (int k = n_[em].next; k >= 0 && k != cn;) {
            const int nx = n_[k].next;
            unlink(k);
            appendChild(em, k);
            k = nx;
        }
        for (int k = d_[op].next; k >= 0 && k != cur;) {
            const int nx = d_[k].next;
            removeDelim(k);
            k = nx;
        }
        if (d_[op].count == 0) {
            unlink(on);
            removeDelim(op);
        }
        if (d_[cur].count == 0) {
            const int nx = d_[cur].next;
            unlink(cn);
            removeDelim(cur);
            cur = nx;
        }
    }
    for (int k = bottom >= 0 ? d_[bottom].next : dHead_; k >= 0;) {
        const int nx = d_[k].next;
        removeDelim(k);
        k = nx;
    }
}

void InlineParser::openBracket(bool image)
{
    const std::size_t len = image ? 2 : 1;
    if (int(b_.size()) >= kMaxNesting) {   // taket — djupare blir text
        appendText(s_.substr(p_, len));
        p_ += len;
        return;
    }
    const int node = newNode(INode::K::Text, std::string(s_.substr(p_, len)));
    appendChild(0, node);
    p_ += len;
    if (!b_.empty())
        b_.back().after = true;
    b_.push_back(Bracket{node, dTail_, p_, image});
}

// `](dest "titel")` (CommonMark 6.3) efter `(`.
bool InlineParser::inlineLink(std::size_t i, std::string& url,
                              std::string& title, std::size_t& end) const
{
    i = skipSpnl(s_, i);
    std::string_view rå;
    std::size_t      n = 0;
    if (!linkDestination(s_, i, rå, n, true))
        return false;
    i += n;
    const std::size_t efterUrl = i;
    i                          = skipSpnl(s_, i);
    std::string_view titel;
    if (i != efterUrl) {
        std::size_t tn = 0;
        if (linkTitle(s_, i, titel, tn))
            i = skipSpnl(s_, i + tn);
    }
    if (i >= s_.size() || s_[i] != ')')
        return false;
    url   = unescape(rå);
    title = unescape(titel);
    end   = i + 1;
    return true;
}

// `]` — LÄNKEN ELLER BILDEN (CommonMark Appendix A, "look for link or
// image"): den översta hakparentesen, och efter `]` en inline-länk, en
// fullständig referens, en kollapsad (`[]`) eller en genväg. Utan träff blir
// `]` text; med träff flyttas texten in i länken, betoningarna i den paras
// ihop, och en länk gör alla tidigare `[` overksamma (en länk i en länk).
void InlineParser::closeBracket()
{
    const std::size_t stäng = p_++;
    if (b_.empty()) {
        appendText("]");
        return;
    }
    if (!b_.back().active) {
        b_.pop_back();
        appendText("]");
        return;
    }
    const Bracket     br    = b_.back();
    const std::size_t efter = p_;
    std::string       url;
    std::string       title;
    bool              träff = false;
    if (p_ < s_.size() && s_[p_] == '(') {
        std::size_t slut = 0;
        if (inlineLink(p_ + 1, url, title, slut)) {
            p_    = slut;
            träff = true;
        }
    }
    if (!träff) {
        std::string etikett;
        std::size_t q      = p_;
        bool        hittad = linkLabel(s_, q, etikett);
        if (hittad)
            p_ = q;
        if ((!hittad || labelKey(etikett).empty()) && !br.after &&
            stäng - br.pos <= kMaxLabel) {
            etikett.assign(s_.substr(br.pos, stäng - br.pos));
            hittad = true;
        }
        if (hittad) {
            const auto it = refs_.find(labelKey(etikett));
            if (it != refs_.end()) {
                url   = it->second.url;
                title = it->second.title;
                träff = true;
            }
        }
        if (!träff)
            p_ = efter;
    }
    if (!träff) {
        b_.pop_back();
        appendText("]");
        return;
    }
    int ref = 0;
    if (br.image) {
        doc_.images.push_back(Image{url, title});
        ref = int(doc_.images.size()) - 1;
    } else {
        doc_.links.push_back(Link{url, title});
        ref = int(doc_.links.size()) - 1;
    }
    const int ln = newNode(br.image ? INode::K::Image : INode::K::Link);
    n_[ln].ref   = ref;
    insertAfter(br.node, ln);
    for (int k = n_[ln].next; k >= 0;) {
        const int nx = n_[k].next;
        unlink(k);
        appendChild(ln, k);
        k = nx;
    }
    unlink(br.node);
    processEmphasis(br.prevDelim);
    b_.pop_back();
    if (!br.image)
        for (auto it = b_.rbegin(); it != b_.rend(); ++it)
            if (!it->image) {
                if (!it->active)
                    break;
                it->active = false;
            }
}

// Bildens ALT-TEXT platt: texten i hela delträdet, betoningen borta.
std::string InlineParser::altText(int k) const
{
    std::string      ut;
    std::vector<int> st{n_[k].first};
    while (!st.empty()) {
        const int c = st.back();
        if (c < 0) {
            st.pop_back();
            continue;
        }
        st.back()     = n_[c].next;
        const INode& n = n_[c];
        switch (n.k) {
        case INode::K::Text:
        case INode::K::Code:
            ut += n.text;
            break;
        case INode::K::Soft:
        case INode::K::Hard:
            ut += ' ';
            break;
        default:
            st.push_back(n.first);
            break;
        }
    }
    return ut;
}

// TRÄDET SOM SPANS — utan rekursion (en betoning kan nästlas tusentals
// nivåer). Stilen och länken ärvs nedåt; den innersta länken vinner. Text
// får blanksteg i stället för tabbar och radbrytningar, och blanksteg i
// följd blir ett (som i HTML) — kod behåller sina. Grannar med samma stil
// och länk slås ihop.
std::vector<Span> InlineParser::flatten() const
{
    std::vector<Span> ut;
    bool              blankSist = true;
    const auto        lägg = [&](Span s) {
        if (s.kind == Span::Kind::Text) {
            if (s.text.empty())
                return;
            if (!ut.empty() && ut.back().kind == Span::Kind::Text &&
                ut.back().style == s.style && ut.back().link == s.link) {
                ut.back().text += s.text;
                return;
            }
        }
        ut.push_back(std::move(s));
    };
    const auto text = [&](const std::string& t, std::uint8_t stil, int länk) {
        Span s;
        s.style = stil;
        s.link  = länk;
        for (char c : t) {
            if (c == '\t' || c == '\n')
                c = ' ';
            if (c == ' ') {
                if (blankSist)
                    continue;
                blankSist = true;
            } else {
                blankSist = false;
            }
            s.text += c;
        }
        lägg(std::move(s));
    };
    struct Ram {
        int          nod;
        std::uint8_t stil;
        int          länk;
    };
    std::vector<Ram> st{{n_[0].first, 0, -1}};
    while (!st.empty()) {
        const Ram r = st.back();
        if (r.nod < 0) {
            st.pop_back();
            continue;
        }
        st.back().nod  = n_[r.nod].next;
        const INode& n = n_[r.nod];
        switch (n.k) {
        case INode::K::Text:
            text(n.text, r.stil, r.länk);
            break;
        case INode::K::Code: {
            Span s;
            s.style = std::uint8_t(r.stil | kCode);
            s.link  = r.länk;
            s.text  = n.text;
            std::replace(s.text.begin(), s.text.end(), '\t', ' ');
            lägg(std::move(s));
            blankSist = false;
            break;
        }
        case INode::K::Soft:
            text(" ", r.stil, r.länk);
            break;
        case INode::K::Hard: {
            Span s;
            s.kind = Span::Kind::Break;
            lägg(std::move(s));
            blankSist = true;
            break;
        }
        case INode::K::Emph:
            st.push_back({n.first, std::uint8_t(r.stil | kItalic), r.länk});
            break;
        case INode::K::Strong:
            st.push_back({n.first, std::uint8_t(r.stil | kBold), r.länk});
            break;
        case INode::K::Link:
            st.push_back({n.first, r.stil, n.ref});
            break;
        case INode::K::Image: {
            Span s;
            s.kind  = Span::Kind::Image;
            s.text  = altText(r.nod);
            s.style = r.stil;
            s.link  = r.länk;
            s.image = n.ref;
            lägg(std::move(s));
            blankSist = false;
            break;
        }
        case INode::K::Root:
            break;
        }
    }
    return ut;
}

// =============================================================== omvandlingen

// Koden: tabbarna expanderade till stoppen (`kTabStop`), räknat från radens
// början — en tabb ritas annars som fontens glyf.
std::string expandTabs(std::string_view s)
{
    std::string ut;
    int         kolumn = 0;
    for (const char c : s) {
        if (c == '\n') {
            ut += c;
            kolumn = 0;
        } else if (c == '\t') {
            const int n = kTabStop - kolumn % kTabStop;
            ut.append(static_cast<std::size_t>(n), ' ');
            kolumn += n;
        } else {
            ut += c;
            if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u)
                ++kolumn;
        }
    }
    return ut;
}

class Converter {
public:
    Converter(InlineParser& inl) : inl_(inl) {}

    void add(const Node& n, std::vector<Block>& ut)
    {
        switch (n.type) {
        case Node::T::Comment:
        case Node::T::Document:
            return;
        default:
            ut.push_back(convert(n));
        }
    }

private:
    Block convert(const Node& n)
    {
        Block b;
        switch (n.type) {
        case Node::T::Paragraph:
            b.kind  = Block::Kind::Paragraph;
            b.spans = inl_.parse(trimView(n.content));
            break;
        case Node::T::Heading: {
            b.kind  = Block::Kind::Heading;
            b.level = n.level;
            b.spans = inl_.parse(trimView(n.content));
            std::string text;
            for (const Span& s : b.spans)
                if (s.kind == Span::Kind::Text)
                    text += s.text;
            b.anchor = unique(slug(text));
            break;
        }
        case Node::T::Code: {
            b.kind = Block::Kind::Code;
            std::string_view c(n.content);
            if (!c.empty() && c.back() == '\n')
                c.remove_suffix(1);
            b.code = expandTabs(c);
            break;
        }
        case Node::T::Quote:
            b.kind = Block::Kind::Quote;
            for (const auto& k : n.kids)
                add(*k, b.children);
            break;
        case Node::T::List:
            b.kind    = Block::Kind::List;
            b.ordered = n.list.ordered;
            b.tight   = n.tight;
            b.start   = n.list.start;
            b.delim   = n.list.marker;
            for (const auto& k : n.kids)
                add(*k, b.children);
            break;
        case Node::T::Item:
            b.kind = Block::Kind::Item;
            for (const auto& k : n.kids)
                add(*k, b.children);
            break;
        case Node::T::Rule:
            b.kind = Block::Kind::Rule;
            break;
        case Node::T::Table: {
            b.kind  = Block::Kind::Table;
            b.align = n.align;
            for (const std::string& rad : n.rows) {
                std::vector<std::string> celler = splitRow(rad);
                celler.resize(b.align.size());
                std::vector<std::vector<Span>> r;
                r.reserve(celler.size());
                for (const std::string& cell : celler)
                    r.push_back(inl_.parse(cell));
                b.rows.push_back(std::move(r));
            }
            break;
        }
        case Node::T::Comment:
        case Node::T::Document:
            break;
        }
        return b;
    }

    // Ankaret UNIKT i dokumentet: dubbletter får `-1`, `-2` … (GitHub).
    std::string unique(const std::string& bas)
    {
        if (used_.insert(bas).second)
            return bas;
        for (int n = 1;; ++n) {
            std::string s = bas + "-" + std::to_string(n);
            if (used_.insert(s).second)
                return s;
        }
    }

    InlineParser&                   inl_;
    std::unordered_set<std::string> used_;
};

} // namespace

Document parse(std::string_view text)
{
    const std::string s = normalize(text);
    Refs              refs;
    BlockParser       block(refs);
    const std::string_view sv(s);
    for (std::size_t a = 0; a < sv.size();) {
        std::size_t b = sv.find('\n', a);
        if (b == kNpos)
            b = sv.size();
        block.processLine(sv.substr(a, b - a));
        a = b + 1;
    }
    const std::unique_ptr<Node> rot = block.finish();
    Document                    doc;
    InlineParser                inl(refs, doc);
    Converter                   conv(inl);
    for (const auto& k : rot->kids)
        conv.add(*k, doc.blocks);
    return doc;
}

std::string slug(std::string_view text)
{
    std::string ut;
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            if (c >= 'A' && c <= 'Z')
                ut += char(c + 32);
            else if (isAsciiAlnum(char(c)) || c == '-' || c == '_')
                ut += char(c);
            else if (c == ' ')
                ut += '-';
            ++i;
            continue;
        }
        const std::size_t n = utf8Len(text, i);
        if (n == 0) {
            ++i;
            continue;
        }
        if (n == 2 && c == 0xC2) {   // U+0080–U+00BF: Latin-1:s symboler
            i += 2;
            continue;
        }
        if (n == 2 && c == 0xC3) {
            const auto d = static_cast<unsigned char>(text[i + 1]);
            if (d != 0x97 && d != 0xB7) {   // × och ÷ försvinner
                ut += char(c);
                ut += char(d <= 0x9E ? d + 0x20 : d);
            }
            i += 2;
            continue;
        }
        ut.append(text.substr(i, n));
        i += n;
    }
    return ut;
}

std::size_t schemeLength(std::string_view url)
{
    if (url.empty() || !isAsciiAlpha(url.front()))
        return 0;
    std::size_t i = 1;
    while (i < url.size() && (isAsciiAlnum(url[i]) || url[i] == '+' ||
                              url[i] == '.' || url[i] == '-'))
        ++i;
    return i < url.size() && url[i] == ':' ? i : 0;
}

LinkAction linkAction(std::string_view url)
{
    if (!url.empty() && url.front() == '#')
        return LinkAction::Anchor;
    const std::size_t n = schemeLength(url);
    std::string       schema;
    for (std::size_t k = 0; k < n; ++k)
        schema += char(url[k] | 0x20);
    return schema == "http" || schema == "https" || schema == "mailto"
               ? LinkAction::Open
               : LinkAction::None;
}

} // namespace glaze::md
