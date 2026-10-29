#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "glaze/draw.h"
#include "glaze/widget.h"

// INTERN header (`sash.h`-prejudikatet) — TEXTFÄLTET, delat av `TextInput`
// (en rad) och `TextArea` (flera rader).
//
// De två är OLIKA widgets — en rad med vågrät scroll mot rader, brytning och
// lodrät rullning, och den flerradiga ärver dessutom Panels remsor — men de
// är SAMMA sorts fält: samma yta per familj, samma kant som bär tillståndet,
// samma markering, samma caret och samma UTF-8-regler. Det skrivs därför EN
// gång, här, i stället för en gång i varje widget, där de två hade kunnat
// glida isär i just det man ser (kanten, markeringens färgpar, caretens
// bredd) och i det man inte ser (en teckengräns, ett radslut).
//
// Här bor också det de två delar UTANFÖR ytan:
// redigeringsgenvägarnas fråga (`editShortcut` — AltGr-skälet), lösenordets
// avbildning mellan riktig och visad text, och ÅNGRA-HISTORIKEN
// (`History`) — samma gruppering och samma djup i båda fälten.
//
// Vad som INTE bor här, med skäl: GEOMETRIN — var texten börjar, hur den
// scrollar, vilken rad careten står på. Den är respektive widgets egen
// (`TextInput::textOriginX`, `TextArea`s radlayout) och olika i grunden: en
// rad med justering mot ett radblock med brytning.
//
// Den är INTE publik: en app har inget ärende hit, och en EGEN widget som
// vill vara ett textfält komponerar en `TextInput` eller en `TextArea`.

namespace glaze::textfield {

// ------------------------------------------------------------------ UTF-8
// Caret och markering flyttas alltid HELA teckengränser — aldrig in i ett
// flerbyte-tecken (åäö, emoji …). Fortsättningsbyten är 10xxxxxx. Utanför
// strängen klampas svaret till dess ändar.
std::size_t nextBoundary(const std::string& s, std::size_t i);
std::size_t prevBoundary(const std::string& s, std::size_t i);
// Teckengränsen VID ELLER FÖRE `i` (klampad till strängens längd) — för en
// position som kom ur en ANNAN text: `setText` byter strängen under
// careten, och en gräns i den gamla kan ligga mitt i ett åäö i den nya.
std::size_t floorBoundary(const std::string& s, std::size_t i);

// ------------------------------------------------------------------- ORDET
// Ordet kring teckengränsen `i` — dubbelklickets markering, [b, e). Ett ord
// är en sammanhängande följd tecken av SAMMA SORT: ordtecken (ASCII-
// bokstäver och siffror, `_` och varje tecken utanför ASCII — åäö är
// bokstäver), blanktecken eller skiljetecken. Ett ordtecken på EN av gränsens
// sidor vinner, så ett dubbelklick på ett ords högra halva (gränsen hamnar
// efter sista bokstaven) markerar ordet och inte blanksteget efter.
// Radbrytningen ingår aldrig: `\n` och övriga styrtecken är en gräns, och
// står `i` mellan två sådana blir svaret tomt (b == e == i).
void wordAt(const std::string& s, std::size_t i, std::size_t& b,
            std::size_t& e);

// ---------------------------------------------------------------- FILTREN
// Det som SKRIVS och KLISTRAS IN — aldrig det appen sätter med `setText`.
// Backendens tecken-events är redan rena; filtret gör urklippsvägen lika
// säker.

// EN rad (`TextInput`): klipps vid första radbrytningen, övriga styrtecken
// (under 0x20, och 0x7F) försvinner.
std::string singleLine(const char* utf8);

// FLERA rader (`TextArea`): radsluten blir LF (CRLF och ett ensamt CR —
// Windows och klassisk Mac), en TABB blir ett blanksteg (att stryka den hade
// klistrat ihop orden i en inklistrad tabell) — eller BEHÅLLS med `keepTabs`,
// fältets `setAcceptsTab` (fältet har tabbstopp) —
// och övriga styrtecken försvinner.
std::string multiLine(const char* utf8, bool keepTabs = false);

// Bara RADSLUTEN: CRLF och ensamt CR blir LF, allt annat orört — det
// flerradiga fältets `setText`. Radslutet är fältets STRUKTUR (det delar
// raderna), resten av texten är appens och lämnas som den är.
std::string unifyNewlines(std::string s);

// ------------------------------------------------------------ TABBSTOPPEN
// Det flerradiga fältets tabbar. En tabb flyttar x
// till NÄSTA multipel av `tabW` (logiska px, räknat från den VISUELLA radens
// början), och texten mellan tabbarna mäts med Draw som vanligt — aldrig med
// `c.font` direkt. Utan `\t` i strängen, eller med `tabW <= 0`, är svaren
// EXAKT Draw:s (`measure`/`prefixWidth`/`byteIndexAtX`/`wrapLines`), så det
// enradiga fältet och tabbfri text rörs inte. Kerning över en tabb finns inte
// (bitarna mäts var för sig).

// Strängens bredd (en hel visuell rad).
float tabbedWidth(const Ctx& c, const std::string& s, float tabW);
// Bredden av de första `bytes` byten — caret och markering.
float tabbedPrefix(const Ctx& c, const std::string& s, std::size_t bytes,
                   float tabW);
// Närmaste teckengräns vid x (logiska px från radens början) — musen. Inne i
// en tabbs lucka vinner den närmaste av tabbens två gränser.
std::size_t tabbedIndexAtX(const Ctx& c, const std::string& s, float x,
                           float tabW);
// Raderna för `s` vid brytbredden `maxW` — `gfx::wrapText`s regel bit för
// bit: `\n` bryter alltid, blanksteg är brytpunkt (och ingår inte i någon
// rad), ett för långt ord hårdbryts på teckengräns, och TABBEN BRYTER INTE.
// Ett stycke utan tabb ges exakt `Draw::wrapLines`s rader.
std::vector<gfx::WrappedLine> tabbedWrapLines(const Ctx& c,
                                              const std::string& s,
                                              float maxW, float tabW);

// ------------------------------------------------------------- GENVÄGARNA
// Fältens redigeringsgenvägar — A/C/X/V/Z/Y och Shift+Z, i BÅDA fälten —
// frågas HÄR och ingen annanstans, och regeln är
// PLATTFORMENS:
//
//   Windows/Linux  Ctrl UTAN Alt. Skälet är AltGr: Windows levererar AltGr som
//                  Ctrl+Alt (backendens `currentMods()`), och tangenten `A`
//                  blir `Key::A` vad layouten än skriver med den. Med polsk
//                  layout kommer AltGr+A därför först som `keyDown(A,
//                  Ctrl|Alt)` och sedan som tecknet ą: utan Alt-villkoret hade
//                  fältet markerat allt och tecknet ersatt hela texten — och
//                  AltGr+C/X/Z (ć ź ż) hade kopierat, klippt ut och ångrat.
//                  Win32:s egna fält ignorerar Ctrl+Alt av samma skäl.
//   macOS          Cmd UTAN Ctrl och UTAN Option (Alt). Option är Macens AltGr
//                  — Option+bokstav skriver tecken — och Cmd+Option+V är
//                  "klistra in utan format" i AppKit, så Option utesluts av
//                  samma skäl. Ctrl+A/C/X/V är INTE fältets där (macOS
//                  Emacs-tangenter, Ctrl+A = radens början, ingår inte): de går
//                  vidare till förfäderna och acceleratorerna.
//
// Garantin gäller I FÄLTET: tangenten fältet tackar nej till går vidare till
// förfäderna och menyernas acceleratorer, så en app ska inte binda
// Ctrl+Alt+bokstav (MANUAL, menyavsnittet).
bool editShortcut(std::uint32_t mods);

// ------------------------------------------------------------ LÖSENORDET
// Ett lösenordsfält (`TextInput::setPassword`) VISAR stilens mask en gång per
// KODPUNKT i texten, medan caret och markering stannar i den RIKTIGA texten
// (byte) — så `text()`, `caret()` och `setSelection` talar samma rymd med och
// utan mask. Bara ritningen och träffen avbildas, genom de här tre.
// Ett grafemkluster av flera kodpunkter (e + U+0301) blir två prickar.

// Antalet kodpunkter som BÖRJAR i [0, end) — `end` klampas till längden.
std::size_t codepoints(const std::string& s, std::size_t end);

// Byten där kodpunkt nummer `n` börjar; bortom sista kodpunkten textens
// längd.
std::size_t byteOfCodepoint(const std::string& s, std::size_t n);

// Maskens längd i BYTE — 0 för en tom eller saknad (`nullptr`) mask. En egen
// `Style` utan `passwordMask` är ogiltig men får inte krascha: fältet visar då
// ingen text och lägger careten på origo, i stället för att dela med noll.
std::size_t maskBytes(const char* mask);

// `mask` × antalet kodpunkter i `s`, skrivet i `ut` (som töms först — en
// befintlig buffert återanvänds). En tom mask ger en tom sträng.
void maskInto(std::string& ut, const std::string& s, const char* mask);

// ----------------------------------------------------------- MAXLÄNGDEN
// Fältens `setMaxLength` räknar KODPUNKTER — lösenordets
// enhet ovan: ett fält för tio tecken tar tio åäö (byte hade gett fem) och
// en emoji är ett tecken (UTF-16 hade sagt två). Grafemkluster finns inte i
// huset, så e + U+0301 är två, som två prickar i masken.
//
// Insättningen `ins` som RYMS när [b, e) i `s` ersätts av den: `max == 0`
// ⇒ obegränsat och hela `ins`; annars är resten texten utom markeringen
// (`codepoints(s, s.size()) − (codepoints(s, e) − codepoints(s, b))`), och
// `ins` kapas på en kodpunktsgräns så att resten och insättningen tillsammans
// blir högst `max`. Ryms inget — också när resten redan är `max` eller mer,
// efter `setText`, en sänkt gräns eller ångra (historiken är sanningen) —
// blir svaret tomt. `b` och `e` är teckengränser med `b <= e`.
std::string fitMaxLength(const std::string& s, std::size_t b, std::size_t e,
                         const std::string& ins, std::size_t max);

// ------------------------------------------------------------- HISTORIKEN
// ÅNGRA och GÖR OM, per fält. Historiken lagrar
// DIFFERENSER — var, vad som togs bort, vad som sattes in och markeringen
// FÖRE — och aldrig ögonblicksbilder, så ett steg i en lång anteckning kostar
// sin egen text och inte hela fältets.
//
// GRUPPERINGEN följer ORD och inte en klocka: skrivande slås ihop med
// steget före så länge insättningen är sammanhängande (den börjar där förra
// slutade och ersatte ingen markering), inget har förseglat gruppen och ett
// ordtecken inte följer direkt efter ett blanktecken (blanksteg eller `\n`) —
// "hej världen" är två steg, "hej " och "världen". Backsteg i följd är ett
// steg, Delete i följd likaså. `Block` (klistra in, klipp ut, radera en
// markering) är alltid ett eget steg och förseglar efter sig. En klocka hade
// gjort beteendet beroende av skrivhastigheten och gått att grinda först med
// en falsk tid.
//
// FÖRSEGLINGEN är fältets: caret- och markeringsflytt, Enter i det enradiga
// fältet och fokusförlust anropar `seal()`, och ångra/gör om förseglar
// själva. Ett byte av redigeringssort förseglar av sig självt (sorten måste
// vara densamma för att slå ihop).
enum class EditKind : std::uint8_t {
    Typing,       // skrivande (och Enter i det flerradiga fältet)
    BackDelete,   // Backsteg utan markering
    FwdDelete,    // Delete utan markering
    Block,        // klistra in, klipp ut, radera en markering — eget steg
};

// Svaret från ångra/gör om: "ersätt [pos, pos + len) med `text` och sätt
// markeringen till (anchor, caret)" — fältet utför det i sin egen
// redigeringsväg, så layouten och `onChange` följer med.
struct Restore {
    std::size_t pos    = 0;
    std::size_t len    = 0;
    std::string text;
    std::size_t anchor = 0;
    std::size_t caret  = 0;
};

class History {
public:
    // EN redigering: [pos, pos + removed.size()) ersattes av `inserted`, och
    // markeringen FÖRE den var (anchorBefore, caretBefore). Kapar gör
    // om-svansen, slår ihop eller lägger ett nytt steg, och håller djupet och
    // textbudgeten (text_field.cpp). Borttaget == infogat registreras inte.
    void record(std::size_t pos, std::string removed, std::string inserted,
                std::size_t anchorBefore, std::size_t caretBefore,
                EditKind kind);
    void seal() { open_ = false; }   // nästa redigering blir ett nytt steg
    void clear();

    bool canUndo() const { return top_ > 0; }
    bool canRedo() const { return top_ < steps_.size(); }

    // Ett steg bakåt eller framåt, eller inget. Båda förseglar.
    std::optional<Restore> undo();
    std::optional<Restore> redo();

private:
    struct Edit {
        std::size_t pos = 0;
        std::string removed;
        std::string inserted;
        std::size_t anchorBefore = 0;
        std::size_t caretBefore  = 0;
        EditKind    kind         = EditKind::Block;
    };

    // Försök lägga redigeringen i det översta steget — false = ett nytt.
    bool merge(std::size_t pos, const std::string& removed,
               const std::string& inserted, EditKind kind);
    void dropOldest();

    std::deque<Edit> steps_;
    std::size_t      top_   = 0;       // [0, top_) är gjorda, resten gör om
    bool             open_  = false;   // översta steget tar emot nästa
    std::size_t      bytes_ = 0;       // texten stegen håller (budgeten)
};

// ------------------------------------------------------------------- YTAN
// Markeringens FÄRGPAR — det enda familjerna är oense om inne i fältet.
struct Selection {
    gfx::Color fill;   // markeringens platta
    gfx::Color text;   // den markerade texten
};

// Fältets YTA och KANT i `b` (widget-lokala LOGISKA px), med `switch (c.ui)`
// inuti — så den per-UI-form som annars hade stått i två widgets draw()
// står på ett ställe. Tillståndstabellerna står vid implementationen.
// Svaret är markeringens färgpar för samma familj.
Selection drawSurface(const Ctx& c, const gfx::Rectf& b, bool enabled,
                      bool hovered, bool readOnly);

// Fokusringen UTANFÖR fältet — lika i alla tre familjerna (den hör till
// ramverkets tangentnavigering, inte till formspråket; tjockleken är ett
// MÅTT och skiljer sig därför ändå).
void drawFocusRing(const Ctx& c, const gfx::Rectf& b);

// Caretens bredd i LOGISKA px: stilens mått, men aldrig under en FYSISK px —
// en caret som rundas till noll pixlar försvinner helt. Sanktionerat
// rasterminimum, inte ett hårdkodat fysiskt mått (hård regel 3).
float caretWidth(const Ctx& c);

// EN TEXTRAD med en valfri markering [selB, selE) (byte i `s`, båda på
// teckengräns; selB == selE = ingen). `x` är textens vänsterkant och `row`
// raden texten v-centreras i — markeringens platta är en radhöjd hög och
// centrerad i den. Tre segment när något är markerat (före, markerat,
// efter): kerning ÖVER en segmentgräns går förlorad — försumbart i
// UI-fonter. Med `tabW > 0` och tabbar i raden ritas bitarna mellan tabbarna
// på sina tabbstopp, och markeringen spänner `tabbedPrefix`-läget.
void drawRun(const Ctx& c, const gfx::Rectf& row, float x,
             const std::string& s, std::size_t selB, std::size_t selE,
             gfx::Color fg, const Selection& sel, float tabW = 0.0f);

} // namespace glaze::textfield
