// TEXTFÄLTET — det `TextInput` och `TextArea` delar (se text_field.h för
// gränsen mot det de INTE delar). Ytan per familj, markeringen, caretens
// bredd, UTF-8-gränserna, inmatningens filter, redigeringsgenvägarna,
// lösenordets avbildning och ångra-historiken. Inga tal och inga färger i
// ritningen: måtten kommer ur `c.style`, färgerna ur `c.theme->colors` — de
// två talen nedan är historikens djup och budget, inget utseende.

#include "text_field.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include <gfx/events.h>

namespace glaze::textfield {
namespace {

// HISTORIKENS GRÄNSER. 100 steg är RichEdits förval
// (`EM_SETUNDOLIMIT`: "by default, the maximum number of actions in the undo
// queue is 100"), och 4 MiB text en budget mot inklistringar som annars hade
// legat kvar i minnet ett helt fältliv. Det NYASTE steget behålls alltid,
// också när det ensamt överskrider budgeten — annars gick en stor
// inklistring inte att ångra alls. Talen står här och inte i `Style`: Style
// är utseende per UI-familj, och ett ångradjup som skilde mellan familjerna
// vore meningslöst (`kCaretBlinkMs` i app_window.cpp är prejudikatet).
constexpr std::size_t kMaxSteps = 100;
constexpr std::size_t kMaxBytes = std::size_t(4) << 20;

bool isCont(unsigned char b) { return (b & 0xC0) == 0x80; }

// Tecknets SORT för ordmarkeringen. Ett tecken utanför ASCII (ledbyte ≥
// 0x80) räknas som ordtecken: åäö är bokstäver, och en följd sådana ska
// markeras som ett ord. Styrtecken — radbrytningen inräknad — är ingen sort
// alls och därmed en gräns.
enum class Sort { Gräns, Ord, Blank, Skilje };

Sort sortAv(unsigned char b)
{
    if (b >= 0x80)
        return Sort::Ord;
    if (b == ' ' || b == '\t')
        return Sort::Blank;
    if (b < 0x20 || b == 0x7F)
        return Sort::Gräns;
    if ((b >= '0' && b <= '9') || (b >= 'a' && b <= 'z') ||
        (b >= 'A' && b <= 'Z') || b == '_')
        return Sort::Ord;
    return Sort::Skilje;
}

// GRUPPERINGENS ordregel: ett ordtecken direkt efter ett BLANKTECKEN
// börjar ett nytt steg. Blanktecknen är blanksteget och radbrytningen (Enter
// i det flerradiga fältet är skrivande) och tabben, som det flerradiga fältet
// skriver med `setAcceptsTab`.
bool blanktecken(unsigned char b) { return b == ' ' || b == '\t' || b == '\n'; }

std::size_t stegBytes(const std::string& borttaget, const std::string& infogat)
{
    return borttaget.size() + infogat.size();
}

} // namespace

std::size_t nextBoundary(const std::string& s, std::size_t i)
{
    if (i >= s.size())
        return s.size();
    ++i;
    while (i < s.size() && isCont(static_cast<unsigned char>(s[i])))
        ++i;
    return i;
}

std::size_t prevBoundary(const std::string& s, std::size_t i)
{
    if (i == 0)
        return 0;
    if (i > s.size())
        return s.size();
    --i;
    while (i > 0 && isCont(static_cast<unsigned char>(s[i])))
        --i;
    return i;
}

std::size_t floorBoundary(const std::string& s, std::size_t i)
{
    if (i >= s.size())
        return s.size();
    while (i > 0 && isCont(static_cast<unsigned char>(s[i])))
        --i;
    return i;
}

void wordAt(const std::string& s, std::size_t i, std::size_t& b,
            std::size_t& e)
{
    i = std::min(i, s.size());
    b = e = i;
    // Vilken sida av gränsen markeringen gäller: ett ORDTECKEN vinner, annars
    // tecknet efter gränsen och sist tecknet före den.
    const Sort efter = i < s.size() ? sortAv(static_cast<unsigned char>(s[i]))
                                    : Sort::Gräns;
    const std::size_t före = prevBoundary(s, i);
    const Sort        förS = i > 0 ? sortAv(static_cast<unsigned char>(s[före]))
                                   : Sort::Gräns;
    Sort sort = Sort::Gräns;
    if (efter == Sort::Ord || förS == Sort::Ord)
        sort = Sort::Ord;
    else if (efter != Sort::Gräns)
        sort = efter;
    else
        sort = förS;
    if (sort == Sort::Gräns)
        return;

    // Utvidga åt båda hållen så länge grannen är av samma sort — gränsen går
    // alltid på hela tecken.
    while (b > 0) {
        const std::size_t p = prevBoundary(s, b);
        if (sortAv(static_cast<unsigned char>(s[p])) != sort)
            break;
        b = p;
    }
    while (e < s.size() && sortAv(static_cast<unsigned char>(s[e])) == sort)
        e = nextBoundary(s, e);
}

std::string singleLine(const char* utf8)
{
    std::string out;
    for (const char* p = utf8; *p; ++p) {
        const unsigned char b = static_cast<unsigned char>(*p);
        if (b == '\n' || b == '\r')
            break;
        if (b >= 0x20 && b != 0x7F)   // hoppa övriga kontrolltecken
            out.push_back(*p);
    }
    return out;
}

std::string multiLine(const char* utf8, bool keepTabs)
{
    std::string out;
    for (const char* p = utf8; *p; ++p) {
        const unsigned char b = static_cast<unsigned char>(*p);
        if (b == '\r') {
            out.push_back('\n');      // CR ensamt ELLER först i ett CRLF —
            if (p[1] == '\n')         // paret blir EN radbrytning
                ++p;
            continue;
        }
        if (b == '\n')
            out.push_back('\n');
        else if (b == '\t')
            out.push_back(keepTabs ? '\t' : ' ');
        else if (b >= 0x20 && b != 0x7F)
            out.push_back(*p);
    }
    return out;
}

// ------------------------------------------------------------ TABBSTOPPEN
// kontraktet står i text_field.h. Varje väg har
// en SNABBVÄG utan tabb som är Draw:s egen fråga rakt av.

namespace {

bool harTabb(const std::string& s, float tabW)
{
    return tabW > 0.0f && s.find('\t') != std::string::npos;
}

// Nästa tabbstopp EFTER x — en tabb flyttar alltid framåt.
float nästaStopp(float x, float tabW)
{
    return (std::floor(x / tabW) + 1.0f) * tabW;
}

// Hela bitens bredd — `prefixWidth` över hela biten, samma fråga som
// prefixet i den, så en position och bitens slut aldrig mäts olika.
float bitBredd(const Ctx& c, const std::string& bit)
{
    return Draw::prefixWidth(c, bit, bit.size());
}

// Så många byte av `s` (från radens början) som ryms i `maxW`, på
// teckengräns — `Font::fitWidth`s fråga med tabbstopp, ur Draw: närmaste
// gräns och sedan bakåt tills prefixet ryms.
std::size_t tabbedFit(const Ctx& c, const std::string& s, float maxW, float tabW)
{
    float       x = 0.0f;
    std::size_t b = 0;
    for (;;) {
        const std::size_t t   = s.find('\t', b);
        const std::size_t end = t == std::string::npos ? s.size() : t;
        const std::string bit = s.substr(b, end - b);
        const float       w   = bitBredd(c, bit);
        if (x + w > maxW) {
            std::size_t k = Draw::byteIndexAtX(c, bit, maxW - x);
            while (k > 0 && Draw::prefixWidth(c, bit, k) > maxW - x)
                k = prevBoundary(bit, k);
            return b + k;
        }
        if (t == std::string::npos)
            return s.size();
        const float stopp = nästaStopp(x + w, tabW);
        if (stopp > maxW)
            return end;   // tabben själv ryms inte
        x = stopp;
        b = t + 1;
    }
}

} // namespace

float tabbedWidth(const Ctx& c, const std::string& s, float tabW)
{
    if (!harTabb(s, tabW))
        return Draw::measure(c, s).w;
    return tabbedPrefix(c, s, s.size(), tabW);
}

float tabbedPrefix(const Ctx& c, const std::string& s, std::size_t bytes,
                   float tabW)
{
    bytes = std::min(bytes, s.size());
    if (!harTabb(s, tabW))
        return Draw::prefixWidth(c, s, bytes);
    float       x = 0.0f;
    std::size_t b = 0;
    for (;;) {
        const std::size_t t = s.find('\t', b);
        if (t == std::string::npos || t >= bytes) {
            const std::size_t end = t == std::string::npos ? s.size() : t;
            return x + Draw::prefixWidth(c, s.substr(b, end - b), bytes - b);
        }
        x = nästaStopp(x + bitBredd(c, s.substr(b, t - b)), tabW);
        b = t + 1;
    }
}

std::size_t tabbedIndexAtX(const Ctx& c, const std::string& s, float x,
                           float tabW)
{
    if (!harTabb(s, tabW))
        return Draw::byteIndexAtX(c, s, x);
    float       x0 = 0.0f;
    std::size_t b  = 0;
    for (;;) {
        const std::size_t t   = s.find('\t', b);
        const std::size_t end = t == std::string::npos ? s.size() : t;
        const std::string bit = s.substr(b, end - b);
        const float       w   = bitBredd(c, bit);
        if (x < x0 + w || t == std::string::npos)
            return b + std::min(Draw::byteIndexAtX(c, bit, x - x0), bit.size());
        // Tabbens LUCKA [x0 + w, stoppet): närmaste av dess två gränser.
        const float stopp = nästaStopp(x0 + w, tabW);
        if (x < stopp)
            return x - (x0 + w) < stopp - x ? end : end + 1;
        x0 = stopp;
        b  = t + 1;
    }
}

std::vector<gfx::WrappedLine> tabbedWrapLines(const Ctx& c,
                                              const std::string& s,
                                              float maxW, float tabW)
{
    if (!harTabb(s, tabW))
        return Draw::wrapLines(c, s, maxW);
    std::vector<gfx::WrappedLine> ut;
    std::size_t                   pos = 0;
    for (;;) {
        std::size_t slut = s.find('\n', pos);
        const bool  hård = slut != std::string::npos;
        if (!hård)
            slut = s.size();
        const std::string seg = s.substr(pos, slut - pos);
        if (seg.find('\t') == std::string::npos) {
            // Ett tabbfritt stycke — exakt `Draw::wrapLines`s rader (också
            // den tomma raden för ett tomt stycke).
            for (const gfx::WrappedLine& l : Draw::wrapLines(c, seg, maxW))
                ut.push_back(gfx::WrappedLine{pos + l.begin, pos + l.end});
        } else {
            // `gfx::wrapText`s slinga, med tabbstoppens mätning.
            std::size_t i = 0;
            while (i < seg.size()) {
                const std::string rest = seg.substr(i);
                std::size_t       fit  = tabbedFit(c, rest, maxW, tabW);
                if (fit >= rest.size()) {
                    ut.push_back(gfx::WrappedLine{pos + i, pos + seg.size()});
                    break;
                }
                if (fit == 0)
                    fit = nextBoundary(rest, 0);   // minst ett tecken per rad
                const std::size_t brk = rest.rfind(' ', fit);
                if (brk == std::string::npos || brk == 0) {
                    ut.push_back(gfx::WrappedLine{pos + i, pos + i + fit});
                    i += fit;
                    while (i < seg.size() && seg[i] == ' ')
                        ++i;
                } else {
                    ut.push_back(gfx::WrappedLine{pos + i, pos + i + brk});
                    i += brk + 1;   // radbrytande blanksteg ingår inte
                }
            }
        }
        if (!hård)
            break;
        pos = slut + 1;
    }
    return ut;
}

std::string unifyNewlines(std::string s)
{
    if (s.find('\r') == std::string::npos)
        return s;   // den vanliga vägen: ingenting att göra, ingen kopia
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\r') {
            out.push_back(s[i]);
            continue;
        }
        out.push_back('\n');
        if (i + 1 < s.size() && s[i + 1] == '\n')
            ++i;
    }
    return out;
}

// ------------------------------------------------------------- GENVÄGARNA

// Plattformens regel — headern har skälen.
bool editShortcut(std::uint32_t mods)
{
#ifdef __APPLE__
    return (mods & gfx::kModCmd) != 0 &&
           (mods & (gfx::kModCtrl | gfx::kModAlt)) == 0;
#else
    return (mods & gfx::kModCtrl) != 0 && (mods & gfx::kModAlt) == 0;
#endif
}

// ------------------------------------------------------------ LÖSENORDET

std::size_t codepoints(const std::string& s, std::size_t end)
{
    end           = std::min(end, s.size());
    std::size_t n = 0;
    for (std::size_t i = 0; i < end; ++i)
        if (!isCont(static_cast<unsigned char>(s[i])))
            ++n;
    return n;
}

std::size_t byteOfCodepoint(const std::string& s, std::size_t n)
{
    std::size_t i = 0;
    while (n > 0 && i < s.size()) {
        i = nextBoundary(s, i);
        --n;
    }
    return i;
}

std::size_t maskBytes(const char* mask)
{
    return mask ? std::strlen(mask) : 0;
}

void maskInto(std::string& ut, const std::string& s, const char* mask)
{
    ut.clear();
    const std::size_t m = maskBytes(mask);
    if (m == 0)
        return;
    const std::size_t n = codepoints(s, s.size());
    ut.reserve(n * m);
    for (std::size_t i = 0; i < n; ++i)
        ut.append(mask, m);
}

// ----------------------------------------------------------- MAXLÄNGDEN

// Kontraktet står i text_field.h. `resten >= max` prövas FÖRE subtraktionen
//: en text som redan är längre
// än gränsen hade annars slagit runt i `std::size_t` och släppt igenom allt.
std::string fitMaxLength(const std::string& s, std::size_t b, std::size_t e,
                         const std::string& ins, std::size_t max)
{
    if (max == 0)
        return ins;
    const std::size_t hela     = codepoints(s, s.size());
    const std::size_t markerat = codepoints(s, e) - codepoints(s, b);
    const std::size_t resten   = hela - markerat;
    if (resten >= max)
        return std::string();
    return ins.substr(0, byteOfCodepoint(ins, max - resten));
}

// ------------------------------------------------------------- HISTORIKEN

void History::record(std::size_t pos, std::string removed, std::string inserted,
                     std::size_t anchorBefore, std::size_t caretBefore,
                     EditKind kind)
{
    if (removed == inserted)
        return;   // ingen ändring — inget att ångra

    // ORDREGELN gäller också INNE i en inmatning: en `textInput` kan bära
    // flera tecken (macOS insertText, en dödtangent), och "hej världen" i ett
    // anrop ska ge samma två steg som tecken för tecken. Delningen sker där ett
    // ordtecken följer ett blanktecken — blanktecknen är ASCII, så byten efter
    // är alltid en UTF-8-ledbyte. Svansen registreras rekursivt med sin egen
    // position; den första delen bär ev. ersatt markering.
    if (kind == EditKind::Typing) {
        for (std::size_t i = 1; i < inserted.size(); ++i) {
            if (blanktecken(static_cast<unsigned char>(inserted[i - 1])) &&
                sortAv(static_cast<unsigned char>(inserted[i])) == Sort::Ord) {
                std::string svans = inserted.substr(i);
                inserted.resize(i);
                record(pos, std::move(removed), std::move(inserted), anchorBefore,
                       caretBefore, kind);
                const std::size_t p = pos + i;
                record(p, {}, std::move(svans), p, p, kind);
                return;
            }
        }
    }

    // En ny redigering efter ett ångra KAPAR gör om-svansen: den grenen
    // finns inte längre (varje editor gör så — ett träd av historier är
    // ingen editors modell).
    while (steps_.size() > top_) {
        bytes_ -= stegBytes(steps_.back().removed, steps_.back().inserted);
        steps_.pop_back();
    }

    bytes_ += stegBytes(removed, inserted);
    if (!merge(pos, removed, inserted, kind)) {
        steps_.push_back(Edit{pos, std::move(removed), std::move(inserted),
                              anchorBefore, caretBefore, kind});
        top_ = steps_.size();
    }
    open_ = kind != EditKind::Block;

    // DJUPET och BUDGETEN släpper det äldsta steget — men aldrig det sista.
    while (steps_.size() > kMaxSteps ||
           (bytes_ > kMaxBytes && steps_.size() > 1))
        dropOldest();
}

// SAMMANSLAGNINGEN. Bara i det ÖVERSTA, öppna steget och bara med samma
// sort — ett byte från skrivande till backsteg är alltid ett nytt steg.
// Stegets markering FÖRE står kvar: ångra ska tillbaka dit gruppen började.
bool History::merge(std::size_t pos, const std::string& removed,
                    const std::string& inserted, EditKind kind)
{
    if (!open_ || steps_.empty() || kind == EditKind::Block)
        return false;
    Edit& e = steps_.back();
    if (e.kind != kind)
        return false;
    switch (kind) {
    case EditKind::Typing: {
        // Sammanhängande: börjar där förra slutade, och ingen markering
        // ersattes (det hade varit en ny handling, inte en fortsättning).
        if (!removed.empty() || pos != e.pos + e.inserted.size())
            return false;
        // ORDREGELN: ett ordtecken direkt efter ett blanktecken börjar ett
        // nytt steg — "hej " och "världen".
        if (!e.inserted.empty() && !inserted.empty() &&
            blanktecken(static_cast<unsigned char>(e.inserted.back())) &&
            sortAv(static_cast<unsigned char>(inserted.front())) == Sort::Ord)
            return false;
        e.inserted += inserted;
        return true;
    }
    case EditKind::BackDelete:
        // Backsteg i följd: det borttagna står direkt FÖRE förra.
        if (!inserted.empty() || pos + removed.size() != e.pos)
            return false;
        e.pos     = pos;
        e.removed = removed + e.removed;
        return true;
    case EditKind::FwdDelete:
        // Delete i följd: careten står still, texten efter den försvinner.
        if (!inserted.empty() || pos != e.pos)
            return false;
        e.removed += removed;
        return true;
    case EditKind::Block:
        break;
    }
    return false;
}

void History::dropOldest()
{
    bytes_ -= stegBytes(steps_.front().removed, steps_.front().inserted);
    steps_.pop_front();
    if (top_ > 0)
        --top_;
}

void History::clear()
{
    steps_.clear();
    top_   = 0;
    open_  = false;
    bytes_ = 0;
}

// ÅNGRA: steget baklänges — det infogade byts mot det borttagna, och
// markeringen går tillbaka till där den stod före redigeringen.
std::optional<Restore> History::undo()
{
    if (top_ == 0)
        return std::nullopt;
    seal();
    const Edit& e = steps_[--top_];
    return Restore{e.pos, e.inserted.size(), e.removed, e.anchorBefore,
                   e.caretBefore};
}

// GÖR OM: steget framlänges, med careten efter det infogade och ingen
// markering — där den stod när redigeringen var gjord.
std::optional<Restore> History::redo()
{
    if (top_ >= steps_.size())
        return std::nullopt;
    seal();
    const Edit&       e    = steps_[top_++];
    const std::size_t slut = e.pos + e.inserted.size();
    return Restore{e.pos, e.removed.size(), e.inserted, slut, slut};
}

// FÄLTETS YTA per familj. Kanten bär tillståndet i ALLA tre ritningarna —
// bara rollerna skiljer. SKRIVSKYDDAT rör INTE kanten: den säger
// "kontroll", och en skrivskyddad ruta är fortfarande en kontroll man kan
// fokusera, markera i och kopiera ur. Det är YTAN som säger "inte här":
// normalt `panel` (den ljusa skrivytan i Slate och Glaze), skrivskyddad den
// tysta `fieldReadOnly` — en EGEN roll, eftersom det inte fanns någon
// lugnare BEFINTLIG yta att byta till.
Selection drawSurface(const Ctx& c, const gfx::Rectf& b, bool enabled,
                      bool hovered, bool readOnly)
{
    const auto&      m   = *c.style;
    const auto&      col = c.theme->colors;
    const gfx::Color glazeEdge = !enabled ? col.border
                                 : hovered ? col.accent
                                           : col.buttonBorder;
    const gfx::Color fieldFill = readOnly ? col.fieldReadOnly : col.panel;

    switch (c.ui) {
    // GLAZE — ett FÄLT, och fält LAMINERAS INTE.
    // Det är familjens enda uttryckliga undantag från lamineringsregeln och
    // det har ett skäl: en skrivyta som fångar ljus läser som en knapp, och
    // då slutar man se var man kan skriva. Kontur + `panel`, inget mer.
    //
    // Konturen är däremot BESTÄMD (`buttonBorder`, kontrollernas kant) och
    // inte den svagare `border` Slate använder — "en Glaze-yta har alltid en
    // bestämd kant", och ett fält är en kontroll.
    //
    // De fyra tillstånden:
    //   vila       yta `panel`, kontur `buttonBorder`
    //   hover      konturen går till `accent`
    //   tryck      — (klicket sätter caret; DET är återkopplingen)
    //   fokus      fristående ring utanför kanten + blinkande caret
    //   inaktiv    kontur `border`, text och caret `textDim`
    //   skrivskyddad  ytan byter till `fieldReadOnly`, kanten står kvar,
    //              caret ritas aldrig — texten behåller full kontrast
    case UI::Glaze:
        Draw::rect(c, b, m.cornerRadius, fieldFill,
                   Pen{glazeEdge, m.borderWidth});
        return Selection{col.accent, col.windowBg};

    // MINIMALISTIC — "Outline": fältet är BARA en kant
    // — men YTAN FYLLS I VILA (se
    // button.cpp): `fieldFill` bär båda färgerna (`fieldReadOnly` respektive
    // `panel`), så ett vanligt fält inte är ett hål med en kant runt. De två
    // skiljer sig på TONEN, vilket är vad `fieldReadOnly` finns för.
    //
    // SELEKTIONEN byter par: `accentSurface`/`onAccent` i stället för
    // Slates `accent`/`windowBg`. Skälet är att markeringen här ligger mot
    // fönstrets bakgrund och inte mot en panel — `windowBg` som textfärg
    // hade då varit texten i BAKGRUNDENS färg ovanpå en accentplatta, alltså
    // rätt av en slump i ljust läge och fel i mörkt. Paret accentSurface/
    // onAccent är det som är grindat för just "text på fylld accentyta"
    // (min 4.65:1, tests/contrast_gate.cpp).
    //
    // De fyra tillstånden:
    //   vila       yta `panel`, kant `buttonBorder`
    //   hover      kanten går till `accent`
    //   tryck      —
    //   fokus      fristående ring (halv tjocklek) + caret
    //   inaktiv    kant `border`, text och caret `textDim`
    //   skrivskyddad  ytan `fieldReadOnly` INNANFÖR kanten. Att ta bort
    //              kanten i stället hade tagit bort fältet, och att lämna
    //              ytan lik den vanliga hade gjort låset osynligt — det är
    //              precis här "kanten ÄR formen" är känsligast, och därför
    //              grindas `buttonBorder` mot `fieldReadOnly` på 3:1
    //              (tests/contrast_gate.cpp).
    case UI::Minimalistic:
        Draw::rect(c, b, m.cornerRadius, fieldFill,
                   Pen{glazeEdge, m.borderWidth});
        return Selection{col.accentSurface, col.onAccent};

    // SLATE — ett FÄLT:
    // skrivytan är `panel` och
    // kanten bär tillståndet, precis som Dropdown. Kanten är basens
    // `hotBorder` (`accent` under pekaren, annars `border`).
    //
    // De fyra tillstånden:
    //   vila       yta `panel`, hårlinje `border`, text `text`
    //   hover      kanten `accent`
    //   tryck      — (ett textfält har inget tryckläge; klicket sätter
    //              caret, och DET är återkopplingen)
    //   fokus      fristående ring utanför kanten + blinkande caret
    //   inaktiv    text och caret `textDim`, kanten `border`, och ytan
    //              `fieldReadOnly` som Win32:s inaktiva fält — kanten är
    //              `border` också i vila, så utan ytan syntes ett TOMT
    //              inaktivt fält aktivt (pixel_gate)
    //   skrivskyddad  ytan byter till `fieldReadOnly` — en tystare nivå
    //              än `panel`, mellan fönstret och den försänkta
    //              arbetsytan. Texten dimmas INTE (det är inaktivt läge),
    //              och caret ritas aldrig.
    //
    // SELEKTIONSPARET `accent`/`windowBg` är oförändrat — om det och om fälten
    // ska bli försänkta avgörs i utseendepasset.
    case UI::Slate:
    default:
        Draw::rect(c, b, m.cornerRadius, enabled ? fieldFill : col.fieldReadOnly,
                   Pen{hovered && enabled ? col.accent : col.border,
                       m.borderWidth});
        return Selection{col.accent, col.windowBg};
    }
}

void drawFocusRing(const Ctx& c, const gfx::Rectf& b)
{
    const auto& m = *c.style;
    Draw::frame(c, Draw::inset(b, -m.focusOffset),
                m.cornerRadius + m.focusOffset,
                Pen{c.theme->colors.accent, m.focusWidth});
}

float caretWidth(const Ctx& c)
{
    return std::max(c.style->caretWidth, 1.0f / c.scale);
}

void drawRun(const Ctx& c, const gfx::Rectf& row, float x,
             const std::string& s, std::size_t selB, std::size_t selE,
             gfx::Color fg, const Selection& sel, float tabW)
{
    if (harTabb(s, tabW)) {
        // TABBSTOPPEN: markeringens platta mellan de två lägena, sedan
        // bitarna mellan tabbarna på sina stopp — var och en delad där
        // markeringen börjar och slutar.
        const float fh = Draw::lineHeight(c);
        if (selB < selE) {
            const float x0 = tabbedPrefix(c, s, selB, tabW);
            const float x1 = tabbedPrefix(c, s, selE, tabW);
            Draw::rect(c, {x + x0, row.y + (row.h - fh) / 2.0f, x1 - x0, fh},
                       0.0f, sel.fill);
        }
        float       px = 0.0f;
        std::size_t b  = 0;
        for (;;) {
            const std::size_t t   = s.find('\t', b);
            const std::size_t end = t == std::string::npos ? s.size() : t;
            const std::string bit = s.substr(b, end - b);
            // Bitens tre delar: före, i och efter markeringen (byte i `bit`).
            const std::size_t m0 = std::clamp(selB, b, end) - b;
            const std::size_t m1 = std::clamp(std::max(selE, selB), b, end) - b;
            const float       w0 = Draw::prefixWidth(c, bit, m0);
            const float       w1 = Draw::prefixWidth(c, bit, m1);
            const float       bx = x + px;
            Draw::text(c, {bx, row.y, w0, row.h}, bit.substr(0, m0), fg);
            if (m1 > m0)
                Draw::text(c, {bx + w0, row.y, w1 - w0, row.h},
                           bit.substr(m0, m1 - m0), sel.text);
            Draw::text(c, {bx + w1, row.y, row.w, row.h}, bit.substr(m1), fg);
            if (t == std::string::npos)
                break;
            px = nästaStopp(px + bitBredd(c, bit), tabW);
            b  = t + 1;
        }
        return;
    }
    if (selB >= selE) {
        Draw::text(c, {x, row.y, row.w, row.h}, s, fg);
        return;
    }
    const float wPre = Draw::prefixWidth(c, s, selB);
    const float wSel = Draw::prefixWidth(c, s, selE) - wPre;
    const float fh   = Draw::lineHeight(c);
    Draw::rect(c, {x + wPre, row.y + (row.h - fh) / 2.0f, wSel, fh}, 0.0f,
               sel.fill);
    Draw::text(c, {x, row.y, wPre, row.h}, s.substr(0, selB), fg);
    Draw::text(c, {x + wPre, row.y, wSel, row.h}, s.substr(selB, selE - selB),
               sel.text);
    Draw::text(c, {x + wPre + wSel, row.y, row.w, row.h}, s.substr(selE), fg);
}

} // namespace glaze::textfield
