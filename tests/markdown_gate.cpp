// markdown_gate — MARKDOWN-VYNS grind.
//
// Kör:  ./build/tests/markdown_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) och inget annat föll
//
// TRE DELAR:
//
//   P  PARSERN, ren std (`Glaze/widgets/src/markdown/md_parse.h`). Ett
//      dokument dumpas i grindens EGEN textform (`dumpBlock`/`dumpSpans`
//      nedan — den står inte i biblioteket) och jämförs med ett HANDSKRIVET
//      förväntat värde, härlett ur CommonMark 0.31.2:s och GFM:s regler (avsnitten står
//      vid fallen). Specens egna exempel är INTE inkopierade (CC-BY-SA och
//      ingen licensfil i repot).
//   L  LAYOUTEN headless (`src/markdown/md_layout.h` och widgetens mätning)
//      mot en INSPELANDE renderer och en `StegFont` per stil — fasta
//      teckensteg som skiljer stilarna åt (vanlig 8, fet 9, mono 7) och egna
//      höjder och ascent — som `Inspelning::loadFont` lämnar till fönstrets
//      `FontSet`. Förväntat värde räknas ur kontraktet och inte ur produktionens
//      geometri.
//   W  WIDGETEN i ett DOLT fönster genom fönstrets riktiga ingång
//      (`handleEvent`, label/textarea-prejudikatet) — länkarna, pekaren,
//      tooltipen, ankarna och vakten — och headless där inspelningen behövs
//      (färgerna, antalet ritade rader, språkbytet).
//
// FALLEN:
//   P1 ATX-rubriker · P2 setext · P3 brytningar · P4 linjer · P5 punktlistor ·
//   P6 numrerade · P7 kodblock · P8 citat · P9 betoning · P10 kodspann ·
//   P11 inline-länkar och bilder · P12 referenser · P13 autolänkar ·
//   P14 escapes och entiteter · P15 HTML och kommentarer · P16 tabeller ·
//   P17 taket, patologisk indata, ogiltig UTF-8 · P18 ankaret · P19 länkens
//   förval och schemat
//   L1 brytningen · L2 fet och mono på baslinjen · L3 höjderna ur Style ·
//   L4 cachen · L5 skalan · L6 bilderna · L7 tabellen · L8 höjd för bredd
//   W1 handen och tooltipen · W2 klicket · W3 handled · W4 url:en kopieras ·
//   W5 vakten · W6 inaktiv · W7 linkAt · W8 synliga rader och hjulet ·
//   W9 språkbytet · W10 setText och det väntande ankaret · W11 FontSet efter
//   close()
//
// SABOTAGEN som ska fälla den:
//   `_` mitt i ett ord blir betoning ⇒ P9 · regeln om tre struken ⇒ P9 ·
//   `---` efter ett stycke blir en linje ⇒ P2 · indragen kod avbryter ett
//   stycke ⇒ P7 · "3." avbryter ett stycke ⇒ P6 · betoning i ett kodspann ⇒
//   P10 · skiftlägeskänsliga etiketter ⇒ P12 · punkten med i en naken URL ⇒
//   P13 · `&#0;` ger NUL ⇒ P14 · kommentaren kvar ⇒ P15 · en tabell utan
//   avgränsningsrad ⇒ P16 · taket borttaget ⇒ P17 (stackspill = fall) · Å
//   viks inte ⇒ P18 · `javascript:` → Open ⇒ P19 · fet mätt med vanlig font ⇒
//   L2 · toppjustering i stället för baslinje ⇒ L2 · ett tal i stället för
//   `markdownBlockGap` ⇒ L3 · nyckeln utan `Metrics` ⇒ L4 · en cacheplats ⇒
//   L4 · `* c.scale` i mätningen ⇒ L5 · ingen krympning ⇒ L6 · tabellen EFTER
//   asseten ⇒ L6 · `heightForWidth` via `behaviorCtx` ⇒ L8 (rekursion) ·
//   klick vid trycket ⇒ W2 · `handled` ignoreras ⇒ W3 · url:en läst ur
//   dokumentet efter handlern ⇒ W4 · ingen `HandlerGuard` ⇒ W5 · alla rader
//   ritas ⇒ W8 · `fontSet_` inte nollad i `executeClose` ⇒ W11.
//   INTE FÄLLBART HÄR: `parseImg` FÖRE schemakollen — utfallet är detsamma
//   (ett namn med schema blir aldrig en giltig asset, `lista` slutar på `:`),
//   bara loggraden skiljer, och grinden ser inte loggen. Schemats regel mäts
//   i P19.
//
// VAD DEN INTE MÄTER: hur texten SER UT (pixel_gate ritar fixturens grupp 28,
// och utseendet är parkerat) · full CommonMark-konformitet (bara delmängden,
// med handskrivet förväntat värde) · att webbläsaren öppnas (`gfx::openUrl` körs aldrig
// här — varje extern länk i fönsterdelen tas av handlern; klassningen är P19)
// · prestandan (`-v` skriver layouttiden för 200 KB, utan tröskel) · Windows
// och Linux (doc/os) · de riktiga fonternas mått (fönsterdelen räknar sina mål
// ur produktionens layout i samma Ctx — det den mäter är BETEENDET).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gfx/null_renderer.h>
#include <gfx/platform.h>

#include <glaze/glaze.h>

#include "font_set.h"            // intern (tests/CMakeLists.txt pekar hit)
#include "markdown/md_layout.h"
#include "markdown/md_parse.h"

namespace {

namespace md = glaze::md;

int  g_fails   = 0;
int  g_cases   = 0;
int  g_skips   = 0;
bool g_verbose = false;

void check(bool ok, const std::string& what, const std::string& fick = "")
{
    ++g_cases;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", what.c_str());
        return;
    }
    std::printf("  FEL  %s%s\n", what.c_str(),
                fick.empty() ? "" : (" — fick " + fick).c_str());
    std::fflush(stdout);
    ++g_fails;
}

std::string tal(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}

bool nära(float a, float b)
{
    return std::fabs(a - b) < 1e-3f;
}

template <class F>
double millisekunder(F&& f)
{
    const auto t0 = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - t0)
        .count();
}

// ================================================================ DEL P
//
// DUMPEN — grindens egen form av ett dokument, en rad per block och två
// blanksteg per nivå:
//   P <delar>                stycke
//   H<n>#<ankare> <delar>    rubrik
//   CODE <kod>               kodblock (`\n` och `\t` synliga)
//   QUOTE                    citat (barnen under)
//   UL<tecken>[ loose]       punktlista med källans markör
//   OL<start><avgr>[ loose]  numrerad lista
//   LI                       listpost (barnen under)
//   HR                       linje
//   TABLE <LCR…>             tabell, en ROW [cell][cell]… per rad
// Delarna: vanlig text som den står; en stilad eller länkad följd som
// `{<b|i|c…>[@länk]:text}` (grannar med samma stil och länk slås ihop HÄR, så
// dumpen inte beror på var parsern råkar dela en text); en bild som
// `![alt]{index}`; en hård brytning som `<br>`.

std::string flaggor(std::uint8_t st)
{
    std::string f;
    if (st & md::kBold)
        f += 'b';
    if (st & md::kItalic)
        f += 'i';
    if (st & md::kCode)
        f += 'c';
    return f;
}

std::string dumpSpans(const std::vector<md::Span>& v)
{
    std::string ut;
    for (std::size_t i = 0; i < v.size();) {
        const md::Span& s = v[i];
        if (s.kind == md::Span::Kind::Break) {
            ut += "<br>";
            ++i;
            continue;
        }
        if (s.kind == md::Span::Kind::Image) {
            ut += "![" + s.text + "]{" + std::to_string(s.image) +
                  (s.link >= 0 ? "@" + std::to_string(s.link) : "") + "}";
            ++i;
            continue;
        }
        std::string text = s.text;
        std::size_t j    = i + 1;
        while (j < v.size() && v[j].kind == md::Span::Kind::Text &&
               v[j].style == s.style && v[j].link == s.link)
            text += v[j++].text;
        if (s.style == 0 && s.link < 0)
            ut += text;
        else
            ut += "{" + flaggor(s.style) +
                  (s.link >= 0 ? "@" + std::to_string(s.link) : "") + ":" +
                  text + "}";
        i = j;
    }
    return ut;
}

std::string synlig(const std::string& s)
{
    std::string ut;
    for (const char c : s) {
        if (c == '\n')
            ut += "\\n";
        else if (c == '\t')
            ut += "\\t";
        else
            ut += c;
    }
    return ut;
}

void dumpBlock(const md::Block& b, int nivå, std::string& ut)
{
    const std::string in(std::size_t(nivå) * 2, ' ');
    using K = md::Block::Kind;
    switch (b.kind) {
    case K::Paragraph:
        ut += in + "P " + dumpSpans(b.spans) + "\n";
        return;
    case K::Heading:
        ut += in + "H" + std::to_string(b.level) + "#" + b.anchor + " " +
              dumpSpans(b.spans) + "\n";
        return;
    case K::Code:
        ut += in + "CODE " + synlig(b.code) + "\n";
        return;
    case K::Quote:
        ut += in + "QUOTE\n";
        break;
    case K::List:
        ut += in + (b.ordered ? "OL" + std::to_string(b.start) : std::string("UL")) +
              std::string(1, b.delim) + (b.tight ? "" : " loose") + "\n";
        break;
    case K::Item:
        ut += in + "LI\n";
        break;
    case K::Rule:
        ut += in + "HR\n";
        return;
    case K::Table: {
        ut += in + "TABLE ";
        for (const md::CellAlign a : b.align)
            ut += a == md::CellAlign::Left     ? 'L'
                  : a == md::CellAlign::Center ? 'C'
                                               : 'R';
        ut += "\n";
        for (const auto& rad : b.rows) {
            ut += in + "  ROW ";
            for (const auto& cell : rad)
                ut += "[" + dumpSpans(cell) + "]";
            ut += "\n";
        }
        return;
    }
    }
    for (const md::Block& barn : b.children)
        dumpBlock(barn, nivå + 1, ut);
}

std::string dumpBlocks(const md::Document& d)
{
    std::string ut;
    for (const md::Block& b : d.blocks)
        dumpBlock(b, 0, ut);
    return ut;
}

std::string dumpLänkar(const md::Document& d)
{
    std::string ut;
    for (std::size_t i = 0; i < d.links.size(); ++i)
        ut += "L" + std::to_string(i) + " " + d.links[i].url +
              (d.links[i].title.empty() ? "" : " \"" + d.links[i].title + "\"") +
              "\n";
    for (std::size_t i = 0; i < d.images.size(); ++i)
        ut += "I" + std::to_string(i) + " " + d.images[i].src +
              (d.images[i].title.empty() ? "" : " \"" + d.images[i].title + "\"") +
              "\n";
    return ut;
}

// Ett fall: blocken mot förväntat värde, och länkarna och bilderna när ett förväntat värde ges.
void fall(const std::string& namn, const std::string& in, const std::string& block,
          const char* länkar = nullptr)
{
    const md::Document d = md::parse(in);
    const std::string  b = dumpBlocks(d);
    check(b == block, namn + ": blocken", "\n" + b);
    if (länkar) {
        const std::string l = dumpLänkar(d);
        check(l == länkar, namn + ": länkarna och bilderna", "\n" + l);
    }
}

void delP()
{
    std::printf("\n=== P. parsern ===\n");

    // P1 — ATX-RUBRIKER (CommonMark 4.2): nivå 1–6, den avslutande följden `#`
    // (efter ett blanksteg) försvinner men bara EN, sju `#` och `#5` är inga
    // rubriker utan stycketext, och `\#` är ett escapat tecken. En rubrik
    // avbryter ett stycke.
    fall("P1",
         "# Ett\n## Två ##\n###### Sex\n####### Sju\n#5 inte\n\\# inte heller\n"
         "### Avslutande ### # ",
         "H1#ett Ett\n"
         "H2#två Två\n"
         "H6#sex Sex\n"
         "P ####### Sju #5 inte # inte heller\n"
         "H3#avslutande- Avslutande ###\n");

    // P2 — SETEXT (4.3): `===` och `---` under ett stycke gör det till en
    // rubrik (före linjens regel), och `---` efter en TOM rad är en linje.
    fall("P2", "Rubrik ett\n==========\n\nRubrik två\n---\n\nStycke\n\n---",
         "H1#rubrik-ett Rubrik ett\n"
         "H2#rubrik-två Rubrik två\n"
         "P Stycke\n"
         "HR\n");

    // P3 — BRYTNINGARNA (6.7–6.8): en radbrytning i ett stycke är mjuk (ett
    // blanksteg), två blanksteg eller ett `\` före den gör den hård.
    fall("P3", "mjuk\nbrytning  \nhård\\\nockså hård",
         "P mjuk brytning<br>hård<br>också hård\n");

    // P4 — LINJERNA (4.1): tre eller fler `*`, `-` eller `_`, med blanksteg
    // emellan; två är text.
    fall("P4", "***\n- - -\n___\n--", "HR\nHR\nHR\nP --\n");

    // P5 — PUNKTLISTOR (5.2–5.3): ett BYTT markörtecken börjar en ny lista,
    // nästlingen följer innehållets kolumn, och en lista med en tom rad mellan
    // två poster är gles — en tom rad efter den sista posten gör den inte gles.
    fall("P5", "- a\n- b\n  - c\n+ d\n\n- e\n\n- f",
         "UL-\n"
         "  LI\n"
         "    P a\n"
         "  LI\n"
         "    P b\n"
         "    UL-\n"
         "      LI\n"
         "        P c\n"
         "UL+\n"
         "  LI\n"
         "    P d\n"
         "UL- loose\n"
         "  LI\n"
         "    P e\n"
         "  LI\n"
         "    P f\n");

    // P6 — NUMRERADE (5.2): startnumret bärs, en annan avgränsare (`)`)
    // börjar en ny lista, och bara "1." får avbryta ett stycke ("3." är text).
    fall("P6", "3. tre\n4. fyra\n\n1) ett\n\nText\n3. inte\n\nText\n1. ja",
         "OL3.\n"
         "  LI\n"
         "    P tre\n"
         "  LI\n"
         "    P fyra\n"
         "OL1)\n"
         "  LI\n"
         "    P ett\n"
         "P Text 3. inte\n"
         "P Text\n"
         "OL1.\n"
         "  LI\n"
         "    P ja\n");

    // P7 — KODBLOCKEN (4.4–4.5): staketets innehåll ordagrant (ingen
    // betoning, indraget kvar); ett kortare staket stänger inte och ett
    // ostängt löper till slutet; indragen kod avbryter INTE ett stycke; en
    // tabb i koden expanderas till nästa stopp av fyra.
    fall("P7 staket", "```\nkod *inte*\n  indrag\n```",
         "CODE kod *inte*\\n  indrag\n");
    fall("P7 ostängt", "~~~~\n~~~\n```", "CODE ~~~\\n```\n");
    fall("P7 indrag", "Stycke\n    inte kod\n\n\tab\tc",
         "P Stycke inte kod\n"
         "CODE ab  c\n");

    // P8 — CITATEN (5.1): en LAT fortsättningsrad hör till citatets stycke,
    // och `> >` nästlar.
    fall("P8", "> citat\nlat rad\n> > nästlat",
         "QUOTE\n"
         "  P citat lat rad\n"
         "  QUOTE\n"
         "    P nästlat\n");

    // P9 — BETONINGEN (6.2): kursiv, fet och båda; `_` mitt i ett ord är
    // ingen betoning (regel 1–2); regeln om tre (9–10) parar `*foo**bar**baz*`
    // som kursiv runt en fet; ett överblivet `*` står kvar som text.
    fall("P9",
         "*a*\n\n**a**\n\n***a***\n\nsnake_case_name\n\n*foo**bar**baz*\n\n**a*",
         "P {i:a}\n"
         "P {b:a}\n"
         "P {bi:a}\n"
         "P snake_case_name\n"
         "P {i:foo}{bi:bar}{i:baz}\n"
         "P *{i:a}\n");

    // P10 — KODSPANNET (6.1): en följd backticks till nästa av SAMMA längd,
    // ett blanksteg bort i varje ände, och ingen betoning inuti.
    fall("P10", "`` a ` b `` och `*a*`", "P {c:a ` b} och {c:*a*}\n");

    // P11 — INLINE-LÄNKAR OCH BILDER (6.3–6.4): titeln, `<dest>` med
    // blanksteg, hakparenteser i texten, länk i betoning och betoning i länk;
    // bildens alt-text är PLATT. Länkarna numreras i den ordning de stängs.
    fall("P11",
         "[text](/url \"titel\") [<en dest>](<a b>) [a [b] c](u) *[em](e)* "
         "[**fet**](f) ![alt *x*](bild.png)",
         "P {@0:text} {@1:<en dest>} {@2:a [b] c} {i@3:em} {b@4:fet} ![alt x]{0}\n",
         "L0 /url \"titel\"\n"
         "L1 a b\n"
         "L2 u\n"
         "L3 e\n"
         "L4 f\n"
         "I0 bild.png\n");

    // P12 — REFERENSERNA (4.7, 6.3): full, kollapsad och genväg; etiketten är
    // skiftlägesokänslig — också för Ä mot ä —; den FÖRSTA definitionen vinner,
    // och definitionerna syns inte.
    fall("P12",
         "[full][Ref] [kollapsad][] [genväg] [Äpple]\n\n"
         "[ref]: /full \"T\"\n[kollapsad]: /koll\n[genväg]: /genv\n"
         "[REF]: /andra\n[äpple]: /a",
         "P {@0:full} {@1:kollapsad} {@2:genväg} {@3:Äpple}\n",
         "L0 /full \"T\"\n"
         "L1 /koll\n"
         "L2 /genv\n"
         "L3 /a\n");

    // P13 — AUTOLÄNKARNA (6.5 och GFM 6.9): `<url>`, `<e-post>` (blir
    // `mailto:`), en naken adress utan den avslutande punkten, en naken inom
    // parentes utan sin `)` — och en med balanserade parenteser behåller dem.
    fall("P13",
         "<https://x.se/a> <a@b.se> https://x.se/a. (https://x.se) "
         "http://y.z/a_(b)",
         "P {@0:https://x.se/a} {@1:a@b.se} {@2:https://x.se/a}. "
         "({@3:https://x.se}) {@4:http://y.z/a_(b)}\n",
         "L0 https://x.se/a\n"
         "L1 mailto:a@b.se\n"
         "L2 https://x.se/a\n"
         "L3 https://x.se\n"
         "L4 http://y.z/a_(b)\n");

    // P14 — ESCAPES OCH ENTITETER (2.4–2.5): `\*` är text, de namngivna och
    // numeriska avkodas, `&#0;` blir U+FFFD, ett okänt namn och `\a` står
    // kvar som de står.
    fall("P14", "\\*inte\\* &amp; &#228; &#xE4; &#0; &okänd; \\a",
         "P *inte* & ä ä \xEF\xBF\xBD &okänd; \\a\n");

    // P15 — HTML ÄR TEXT, kommentarerna försvinner — inline och som block.
    fall("P15", "<b>x</b> a<!-- c -->b\n\n<!--\nblock\n-->\n\nefter",
         "P <b>x</b> ab\n"
         "P efter\n");

    // P16 — TABELLERNA (GFM 4.10): justeringen ur avgränsningsraden, `\|` i
    // en cell, en kort rad fylls ut och en lång kapas; utan avgränsningsrad
    // ingen tabell; och styckets tidigare rader står kvar som ett stycke.
    fall("P16",
         "| A | B | C |\n|:--|:-:|--:|\n| a \\| b | **c** |\n| 1 | 2 | 3 | 4 |",
         "TABLE LCR\n"
         "  ROW [A][B][C]\n"
         "  ROW [a | b][{b:c}][]\n"
         "  ROW [1][2][3]\n");
    fall("P16 utan avgränsning", "a | b\nc | d", "P a | b c | d\n");
    fall("P16 efter stycke", "Text\n| x |\n|---|",
         "P Text\n"
         "TABLE L\n"
         "  ROW [x]\n");

    // P17 — TAKET OCH PATOLOGISK INDATA: 1 000 `>` blir 32 nivåer (kMaxNesting)
    // och resten text; 20 000 `*a` och 50 000 `[` under en sekund även i Debug
    // (generöst — det visar linjäritet, inte fart); CR, CR LF, NUL och ogiltig
    // UTF-8.
    {
        const md::Document d = md::parse(std::string(1000, '>') + " text");
        int                djup = 0;
        const md::Block*   b    = d.blocks.empty() ? nullptr : &d.blocks[0];
        while (b && b->kind == md::Block::Kind::Quote) {
            ++djup;
            b = b->children.empty() ? nullptr : &b->children[0];
        }
        check(djup == 32, "P17: 1 000 `>` ger 32 citatnivåer", std::to_string(djup));
        check(b && b->kind == md::Block::Kind::Paragraph &&
                  dumpSpans(b->spans) == std::string(968, '>') + " text",
              "P17: resten av raden är stycketext");
    }
    {
        std::string a;
        for (int i = 0; i < 20000; ++i)
            a += "*a";
        md::Document d;
        const double t = millisekunder([&] { d = md::parse(a); });
        check(t < 1000.0 && d.blocks.size() == 1,
              "P17: 20 000 `*a` tolkas under en sekund", tal(t) + " ms");
    }
    {
        md::Document d;
        const std::string a(50000, '[');
        const double t = millisekunder([&] { d = md::parse(a); });
        check(t < 1000.0 && d.blocks.size() == 1 &&
                  dumpSpans(d.blocks[0].spans) == a,
              "P17: 50 000 `[` tolkas under en sekund och blir text", tal(t) + " ms");
    }
    fall("P17 bytes",
         std::string("a\r\nb\rc\xFF") + std::string(1, '\0') + "d\xC3",
         "P a b c\xEF\xBF\xBD\xEF\xBF\xBD" "d\xEF\xBF\xBD\n");

    // P18 — ANKARET (GitHubs regel): gemener för ASCII och Latin-1, blanksteg
    // blir `-`, skiljetecknen försvinner, dubbletter får `-1`, `-2`, och en
    // rubriks kod räknas som text.
    check(md::slug("Översikt & mer") == "översikt--mer",
          "P18: slug(\"Översikt & mer\") == \"översikt--mer\"",
          md::slug("Översikt & mer"));
    fall("P18 dubbletter", "# A\n# A\n# A\n## Kör `make`!",
         "H1#a A\n"
         "H1#a-1 A\n"
         "H1#a-2 A\n"
         "H2#kör-make Kör {c:make}!\n");

    // P19 — LÄNKENS FÖRVAL och SCHEMAT: `#` rullar, http/https/mailto öppnas
    // (skiftlägesokänsligt), allt annat gör ingenting; schemat
    // `[A-Za-z][A-Za-z0-9+.-]*:` — ett asset-namn har inget.
    using A = md::LinkAction;
    check(md::linkAction("#x") == A::Anchor, "P19: #x → Anchor");
    check(md::linkAction("HTTPS://x.se") == A::Open &&
              md::linkAction("http://x") == A::Open &&
              md::linkAction("mailto:a@b.se") == A::Open,
          "P19: HTTPS://, http:// och mailto: → Open");
    check(md::linkAction("file:///etc/passwd") == A::None &&
              md::linkAction("javascript:alert(1)") == A::None &&
              md::linkAction("docs/a.md") == A::None &&
              md::linkAction("") == A::None,
          "P19: file:, javascript:, en relativ stig och tomt → None");
    check(md::schemeLength("toolbar/info") == 0 &&
              md::schemeLength("https://x/y.png") == 5 &&
              md::schemeLength("a+b.c-d:x") == 7 && md::schemeLength(":x") == 0,
          "P19: schemets längd — ett asset-namn har inget");
}

// ================================================================ DEL L

// Fast teckensteg PER KODPUNKT, egen höjd och ascent och en stil — det
// layouten läser ur en font. Räknar sina mätningar (L4: en träff i cachen
// mäter ingenting).
class StegFont final : public gfx::NullFont {
public:
    StegFont(float steg, float höjd, float asc, gfx::FontStyle stil)
        : steg_(steg), höjd_(höjd), asc_(asc), stil_(stil)
    {
    }
    gfx::Sizef measure(const std::string& s) const override
    {
        ++mätningar;
        return gfx::Sizef{steg_ * float(tecken(s, s.size())), höjd_};
    }
    float          height() const override { return höjd_; }
    float          ascent() const override { return asc_; }
    gfx::FontStyle style() const override { return stil_; }
    bool           valid() const override { return true; }
    float prefixWidth(const std::string& s, std::size_t bytes) const override
    {
        return steg_ * float(tecken(s, std::min(bytes, s.size())));
    }
    std::size_t byteIndexAtX(const std::string& s, float x) const override
    {
        return fitWidth(s, x + steg_ / 2.0f);
    }
    std::size_t fitWidth(const std::string& s, float maxW) const override
    {
        std::size_t i = 0;
        float       w = 0.0f;
        while (i < s.size()) {
            std::size_t j = i + 1;
            while (j < s.size() &&
                   (static_cast<unsigned char>(s[j]) & 0xC0) == 0x80)
                ++j;
            if (w + steg_ > maxW)
                break;
            w += steg_;
            i = j;
        }
        return i;
    }
    float steg() const { return steg_; }

    static inline int mätningar = 0;

private:
    static std::size_t tecken(const std::string& s, std::size_t bytes)
    {
        std::size_t n = 0;
        for (std::size_t i = 0; i < bytes; ++i)
            if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
                ++n;
        return n;
    }
    float          steg_, höjd_, asc_;
    gfx::FontStyle stil_;
};

// Brödtextens mått i LOGISKA px — stegen ovan; en font i en annan storlek
// eller skala skalas med px / `uiFontSize`.
constexpr float kSteg    = 8.0f;   // vanlig
constexpr float kStegFet = 9.0f;
constexpr float kStegMono = 7.0f;
constexpr float kHöjd    = 16.0f;
constexpr float kHöjdFet = 18.0f;
constexpr float kHöjdMono = 15.0f;
constexpr float kAsc     = 12.0f;
constexpr float kAscFet  = 13.0f;
constexpr float kAscMono = 11.0f;

// Texten (med plats, färg och font) — och fonterna `FontSet` laddar.
struct Inspelning : gfx::NullRenderer {
    struct Text {
        std::string      s;
        float            x = 0.0f, y = 0.0f;
        gfx::Color       färg{};
        const gfx::Font* font = nullptr;
    };
    std::vector<Text> texter;
    float             uiFont = 1.0f;   // stilens `uiFontSize` — fonternas bas

    float drawText(gfx::Font& f, float x, float y, const std::string& s,
                   gfx::Color c) override
    {
        texter.push_back({s, x, y, c, &f});
        return 0.0f;
    }
    std::unique_ptr<gfx::Font> loadFont(gfx::FontFamily fam, float px,
                                        gfx::FontStyle stil) override
    {
        const float k    = px / uiFont;
        const bool  mono = fam == gfx::FontFamily::Mono;
        const bool  fet  = gfx::isBold(stil);
        return std::make_unique<StegFont>(
            (mono ? kStegMono : fet ? kStegFet : kSteg) * k,
            (mono ? kHöjdMono : fet ? kHöjdFet : kHöjd) * k,
            (mono ? kAscMono : fet ? kAscFet : kAsc) * k, stil);
    }
};

// En scen för del L: renderern, brödtextens font, fönstrets textfonter och
// stilen — i skalan `skala`.
struct Bänk {
    explicit Bänk(float skala = 1.0f,
                  const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate))
        : stil(st),
          bröd(kSteg * skala, kHöjd * skala, kAsc * skala, gfx::FontStyle::Regular)
    {
        r.uiFont = stil.uiFontSize;
        c        = glaze::Ctx{};
        c.r      = &r;
        c.font   = &bröd;
        c.scale  = skala;
        c.area   = gfx::Rectf{0.0f, 0.0f, 900.0f * skala, 700.0f * skala};
        c.theme  = &glaze::Theme::slateDefault().light;
        c.ui     = glaze::UI::Slate;
        c.style  = &stil;
        c.textFonts = &fonter;
    }
    Bänk(const Bänk&)            = delete;
    Bänk& operator=(const Bänk&) = delete;

    md::Layout lägg(const std::string& text, float bredd,
                    std::vector<glaze::ImageSource> bilder = {})
    {
        return md::build(c, md::parse(text), bilder, bredd);
    }

    glaze::Style   stil;
    Inspelning     r;
    StegFont       bröd;
    glaze::FontSet fonter;
    glaze::Ctx     c{};
};

// Widgetens skyddade krokar, åtkomliga för grinden.
struct Vy : glaze::MarkdownView {
    using glaze::MarkdownView::MarkdownView;
    using glaze::MarkdownView::contentExtent;
    using glaze::MarkdownView::contentSize;
    using glaze::MarkdownView::cursorAt;
    using glaze::MarkdownView::draw;
    using glaze::MarkdownView::heightForWidth;
    using glaze::MarkdownView::toolTipAt;
    using glaze::Panel::scrollRange;
};

// Widgetens bredd för en INNEHÅLLSBREDD — vyns skrivna kontrakt (markdown_
// view.h): ramen, luften och högerluften max(`markdownPad`, `scrollThick`).
// Grindens egen räkning, inte produktionens funktion.
float vyBredd(const glaze::Style& m, float innehåll, bool ram = false)
{
    return innehåll + (ram ? 2.0f * m.panelInset : 0.0f) + m.markdownPad +
           std::max(m.markdownPad, m.scrollThick);
}

// Raderna som text, i ordning, och en del med en viss text.
std::vector<std::string> radtexter(const md::Layout& L)
{
    std::vector<std::string> ut;
    for (const md::Line& l : L.lines) {
        std::string s;
        for (std::uint32_t i = l.first; i < l.first + l.count; ++i)
            s += L.items[i].text;
        ut.push_back(s);
    }
    return ut;
}

const md::Item* delMed(const md::Layout& L, const std::string& text)
{
    for (const md::Item& it : L.items)
        if (it.text == text)
            return &it;
    return nullptr;
}

std::string lista(const std::vector<std::string>& v)
{
    std::string ut;
    for (const std::string& s : v)
        ut += "[" + s + "]";
    return ut;
}

void delL()
{
    std::printf("\n=== L. layouten ===\n");

    // L1 — BRYTNINGEN vid bredden 100 med steget 8: "aaaa bbbb" ryms (72),
    // "cccc" hade gett 112; ett ord på 26 tecken bryts där Draw::wrapLines
    // bryter det (12 + 12 + 2).
    {
        Bänk             b;
        const md::Layout L = b.lägg("aaaa bbbb cccc dddd", 100.0f);
        check(radtexter(L) == std::vector<std::string>{"aaaa bbbb", "cccc dddd"},
              "L1: två rader, blankstegen sist på raden borta", lista(radtexter(L)));
        bool inom = true;
        for (const md::Item& it : L.items)
            inom = inom && it.x + it.w <= 100.0f + 1e-3f;
        check(inom, "L1: ingen del går förbi bredden");

        const std::string ord = "abcdefghijklmnopqrstuvwxyz";
        const md::Layout  H   = b.lägg(ord, 100.0f);
        std::vector<std::string> facit;
        for (const gfx::WrappedLine& w : glaze::Draw::wrapLines(b.c, ord, 100.0f))
            facit.push_back(ord.substr(w.begin, w.end - w.begin));
        check(facit.size() == 3 && radtexter(H) == facit,
              "L1: ett för långt ord ger samma bitar som Draw::wrapLines",
              lista(radtexter(H)) + " mot " + lista(facit));
    }

    // L2 — FET OCH MONO PÅ BASLINJEN: "fet" mäts med den feta fonten (3 × 9),
    // kodens font är mono, och varje del står med sin överkant på baslinjen
    // minus SIN ascent (fet 13, vanlig 12, mono 11 — raden 13 + 5). Samma sak
    // i ritningen: `drawText`s font är den feta respektive mono, och y +
    // ascent är lika för alla tre.
    {
        Bänk             b;
        const md::Layout L = b.lägg("**fet** vanlig `kod`", 500.0f);
        const md::Item*  fet = delMed(L, "fet");
        const md::Item*  kod = delMed(L, "kod");
        check(fet && nära(fet->w, 3.0f * kStegFet) && gfx::isBold(fet->font.style),
              "L2: \"fet\" mäts med den feta fonten (27)",
              fet ? tal(fet->w) : "saknas");
        check(kod && kod->font.family == gfx::FontFamily::Mono &&
                  nära(kod->w, 3.0f * kStegMono),
              "L2: koden mäts med monofonten (21)", kod ? tal(kod->w) : "saknas");
        check(L.lines.size() == 1 && nära(L.lines[0].h, kAscFet + (kHöjdFet - kAscFet)) &&
                  nära(L.lines[0].baseline, kAscFet),
              "L2: radens höjd och baslinje ur den största ascenten och nedre delen",
              L.lines.empty() ? "" : tal(L.lines[0].h) + "/" + tal(L.lines[0].baseline));
        bool baslinje = !L.lines.empty();
        for (const md::Item& it : L.items)
            baslinje = baslinje &&
                       nära(it.top + md::fontFor(b.c, it.font)->ascent(),
                            L.lines[0].baseline);
        check(baslinje, "L2: varje del: överkant + egen ascent == baslinjen");

        Vy v;
        v.width  = 500.0f;
        v.height = 100.0f;
        v.setText("**fet** vanlig `kod`");
        glaze::Ctx c = b.c;
        c.box        = v.resolve(c);
        v.draw(c);
        const Inspelning::Text* tf = nullptr;
        const Inspelning::Text* tk = nullptr;
        for (const Inspelning::Text& t : b.r.texter) {
            if (t.s == "fet")
                tf = &t;
            if (t.s == "kod")
                tk = &t;
        }
        check(tf && tf->font->style() == gfx::FontStyle::Bold,
              "L2: drawText ritar \"fet\" med en fet font");
        const auto* mono = tk ? dynamic_cast<const StegFont*>(tk->font) : nullptr;
        check(mono && nära(mono->steg(), kStegMono),
              "L2: drawText ritar koden med monofonten");
        bool lika = b.r.texter.size() == 3;
        for (const Inspelning::Text& t : b.r.texter)
            lika = lika && nära(t.y + t.font->ascent(),
                                b.r.texter[0].y + b.r.texter[0].font->ascent());
        check(lika, "L2: ritningens y + ascent är lika för alla tre delarna",
              std::to_string(b.r.texter.size()) + " texter");
    }

    // L3 — HÖJDERNA UR STYLE: tre stycken har två mellanrum, så
    // `markdownBlockGap` + 5 ger höjden + 10; H1 står i `headingFontSize`, H2
    // i `subheadingFontSize` och H3 i brödtextens — alla feta.
    {
        Bänk             b;
        const float      h0 = b.lägg("a\n\nb\n\nc", 300.0f).height;
        glaze::Style     st = glaze::defaultStyle(glaze::UI::Slate);
        st.markdownBlockGap += 5.0f;
        Bänk             b5(1.0f, st);
        const float      h5 = b5.lägg("a\n\nb\n\nc", 300.0f).height;
        check(nära(h5, h0 + 10.0f), "L3: blockluften + 5 ger höjden + 10",
              tal(h0) + " → " + tal(h5));

        const glaze::Style& m = b.stil;
        const auto rubrik = [&](const char* text, float storlek) {
            const md::Layout L = b.lägg(text, 300.0f);
            return !L.lines.empty() &&
                   nära(L.lines[0].h, kHöjdFet * storlek / m.uiFontSize);
        };
        check(rubrik("# R", m.headingFontSize),
              "L3: H1 i headingFontSize, fet");
        check(rubrik("## R", m.subheadingFontSize),
              "L3: H2 i subheadingFontSize, fet");
        check(rubrik("### R", m.uiFontSize), "L3: H3 i brödtextens storlek, fet");
    }

    // L4 — CACHEN: samma nyckel lägger inte om (inga mätningar), två bredder i
    // följd ryms i de två platserna, och ett ÄNDRAT värde i SAMMA Style-objekt
    // lägger om — `c.style` är samma pekare efter ett stilbyte.
    {
        Bänk b;
        Vy   v;
        v.setText("Ett stycke med några ord.\n\n- och en lista\n- med två poster");
        const auto mät = [&](auto&& f) {
            StegFont::mätningar = 0;
            f();
            return StegFont::mätningar;
        };
        const int a1 = mät([&] { v.contentSize(b.c); });
        const int b1 = mät([&] { v.heightForWidth(b.c, 200.0f); });
        const int a2 = mät([&] { v.contentSize(b.c); });
        const int b2 = mät([&] { v.heightForWidth(b.c, 200.0f); });
        const int a3 = mät([&] { v.contentSize(b.c); });
        check(a1 > 0 && b1 > 0, "L4: (fixturen) en ny bredd mäter",
              std::to_string(a1) + "/" + std::to_string(b1));
        check(a2 == 0 && b2 == 0 && a3 == 0,
              "L4: två bredder i följd lägger inte om (två platser)",
              std::to_string(a2) + "/" + std::to_string(b2) + "/" +
                  std::to_string(a3));
        const float före = v.contentSize(b.c).h;
        b.stil.markdownBlockGap += 5.0f;
        const int ändrad = mät([&] { v.contentSize(b.c); });
        check(ändrad > 0 && v.contentSize(b.c).h > före,
              "L4: ett ändrat värde i SAMMA Style-objekt lägger om",
              std::to_string(ändrad));
    }

    // L5 — SKALAN: skala 1 och 2, med fonterna gånger skalan, ger SAMMA
    // logiska geometri — raderna, delarna och ytorna.
    {
        const std::string text =
            "# Rubrik\n\nEtt stycke med **fet** och `kod` som bryts över flera "
            "rader.\n\n- en\n- två\n\n> citat\n\n| A | B |\n|---|--:|\n| 1 | 22 |";
        Bänk             b1(1.0f);
        Bänk             b2(2.0f);
        const md::Layout L1 = b1.lägg(text, 180.0f);
        const md::Layout L2 = b2.lägg(text, 180.0f);
        bool lika = L1.lines.size() == L2.lines.size() &&
                    L1.items.size() == L2.items.size() &&
                    L1.decos.size() == L2.decos.size() && nära(L1.height, L2.height);
        for (std::size_t i = 0; lika && i < L1.lines.size(); ++i)
            lika = nära(L1.lines[i].y, L2.lines[i].y) &&
                   nära(L1.lines[i].h, L2.lines[i].h) &&
                   nära(L1.lines[i].baseline, L2.lines[i].baseline);
        for (std::size_t i = 0; lika && i < L1.items.size(); ++i)
            lika = nära(L1.items[i].x, L2.items[i].x) &&
                   nära(L1.items[i].w, L2.items[i].w) &&
                   nära(L1.items[i].top, L2.items[i].top);
        for (std::size_t i = 0; lika && i < L1.decos.size(); ++i)
            lika = nära(L1.decos[i].r.x, L2.decos[i].r.x) &&
                   nära(L1.decos[i].r.y, L2.decos[i].r.y) &&
                   nära(L1.decos[i].r.w, L2.decos[i].r.w) &&
                   nära(L1.decos[i].r.h, L2.decos[i].r.h);
        check(lika, "L5: skala 1 och 2 ger samma logiska geometri",
              tal(L1.height) + " mot " + tal(L2.height));
    }

    // L6 — BILDERNA: en Bitmap på 200 × 100 i skala 2 är 100 × 50 logiska px;
    // en för bred krymps med proportionerna; en som saknas blir ikonen
    // (`iconFallback`) och alt-texten i den dämpade rollen efter `iconGap`; och
    // vyns tabell slår en asset med samma namn.
    {
        Bänk             b2(2.0f);
        const md::Layout L = b2.lägg("![](x)", 400.0f, {glaze::Bitmap(200, 100)});
        const md::Item*  bild = L.items.empty() ? nullptr : &L.items[0];
        check(bild && bild->kind == md::Item::Kind::Image && nära(bild->w, 100.0f) &&
                  nära(bild->h, 50.0f),
              "L6: Bitmap 200 × 100 i skala 2 är 100 × 50",
              bild ? tal(bild->w) + " × " + tal(bild->h) : "saknas");

        Bänk             b;
        const md::Layout K = b.lägg("![](x)", 100.0f, {glaze::Bitmap(400, 200)});
        const md::Item*  krympt = K.items.empty() ? nullptr : &K.items[0];
        check(krympt && nära(krympt->w, 100.0f) && nära(krympt->h, 50.0f),
              "L6: för bred krymps till raden med bevarade proportioner",
              krympt ? tal(krympt->w) + " × " + tal(krympt->h) : "saknas");

        const md::Layout S = b.lägg("![alt text](x)", 300.0f, {glaze::ImageSource{}});
        const glaze::Style& m = b.stil;
        const md::Item*     ikon = S.items.empty() ? nullptr : &S.items[0];
        const md::Item*     alt  = delMed(S, "alt text");
        check(ikon && ikon->kind == md::Item::Kind::Missing &&
                  nära(ikon->w, m.iconFallback) && nära(ikon->h, m.iconFallback),
              "L6: en saknad bild är ikonen i iconFallback");
        check(alt && alt->role == md::Role::Dim &&
                  nära(alt->x, m.iconFallback + m.iconGap),
              "L6: alt-texten står efter iconGap i den dämpade rollen",
              alt ? tal(alt->x) : "saknas");

        const bool laddad = glaze::loadAssets(std::string(GLAZE_TEST_ROOT) +
                                              "/apps/demo/assets.bin");
        check(laddad && glaze::img("toolbar", "info").valid(),
              "L6: (fixturen) toolbar/info finns i demons assets");
        Vy v;
        v.width = 300.0f;
        v.setText("![](toolbar/info)");
        v.setImage("toolbar/info", glaze::Bitmap(40, 80));
        const float h = v.contentSize(b.c).h;
        // Bilden står på baslinjen: radens höjd är bildens 80 + brödtextens
        // nedre del. En asset utan bildcache mäter 0 och hade blivit ikonen.
        check(nära(h, 80.0f + (kHöjd - kAsc) + 2.0f * m.markdownPad),
              "L6: vyns tabell före asseten — tabellens bild bär raden", tal(h));
        Vy https;
        https.width = 300.0f;
        https.setText("![](https://example.com/x.png)");
        const md::Layout H = b.lägg("![](https://example.com/x.png)", 300.0f,
                                    {glaze::ImageSource{}});
        check(nära(https.contentSize(b.c).h, H.height + 2.0f * m.markdownPad),
              "L6: ett namn med schema slås inte upp — samma som en saknad bild");
    }

    // L7 — TABELLEN: kolumnernas naturliga bredd (bredaste cellen på en rad)
    // och minsta (längsta ordet), båda med cellens luft. Bred nog: naturligt;
    // smalare: minsta + resten efter (naturlig − minsta); smalare än minsta:
    // minsta (tabellen går ut och klipps). Justeringen per rad i cellen.
    {
        Bänk                b;
        const glaze::Style& m    = b.stil;
        const float         luft = 2.0f * m.markdownCellPadX;
        const std::string   text = "| A | BB |\n|:-|--:|\n| x | yy zz |";
        // A (fet 9) / x (8) ⇒ 9 + luft båda. BB (fet 18) / "yy zz" (40, längsta
        // ordet 16) ⇒ naturlig 40 + luft, minsta 18 + luft.
        const float n0 = kStegFet + luft, n1 = 5.0f * kSteg + luft;
        const float m0 = kStegFet + luft, m1 = 2.0f * kStegFet + luft;
        const auto  bredder = [&](float bredd) {
            std::vector<float> ut;
            for (const md::Deco& d : b.lägg(text, bredd).decos)
                if (d.kind == md::Deco::Kind::HeaderCell)
                    ut.push_back(d.r.w);
            return ut;
        };
        const auto lika = [](const std::vector<float>& v, float a, float c) {
            return v.size() == 2 && nära(v[0], a) && nära(v[1], c);
        };
        const auto text2 = [](const std::vector<float>& v) {
            std::string s;
            for (float f : v)
                s += tal(f) + " ";
            return s;
        };
        const std::vector<float> nat = bredder(400.0f);
        check(lika(nat, n0, n1), "L7: bred nog ⇒ naturliga bredder", text2(nat));
        const float mitten = m0 + m1 + 1.0f;
        const std::vector<float> för = bredder(mitten);
        check(lika(för, m0, m1 + (n1 - m1) * 1.0f / ((n0 + n1) - (m0 + m1))),
              "L7: smalare ⇒ minsta + resten efter (naturlig − minsta)", text2(för));
        const std::vector<float> min = bredder(m0 + m1 - 20.0f);
        check(lika(min, m0, m1), "L7: smalare än minsta ⇒ minsta", text2(min));

        const md::Layout L  = b.lägg(text, 400.0f);
        const md::Item*  bb = delMed(L, "BB");
        const md::Item*  yy = delMed(L, "yy zz");
        check(bb && nära(bb->x, n0 + m.markdownCellPadX + (n1 - luft - 2.0f * kStegFet)),
              "L7: högerjusterat huvud står mot cellens högerkant",
              bb ? tal(bb->x) : "saknas");
        check(yy && nära(yy->x, n0 + m.markdownCellPadX),
              "L7: den bredaste cellen börjar vid cellens luft",
              yy ? tal(yy->x) : "saknas");
    }

    // L8 — HÖJD FÖR BREDD = innehållets höjd vid samma bredd (utan ram), och
    // med ram två `panelInset` till. Ingen `resolve` i den första frågan.
    {
        Bänk b;
        for (const bool ram : {false, true}) {
            Vy v;
            v.width = 240.0f;
            v.setBorder(ram);
            v.setText("Ett stycke som är långt nog att brytas över flera rader "
                      "i en smal vy.\n\n# Rubrik\n\n- post");
            const std::optional<float> h = v.heightForWidth(b.c, 240.0f);
            const float e = v.contentExtent(b.c).h / b.c.scale;
            check(h && nära(*h, e + (ram ? 2.0f * b.stil.panelInset : 0.0f)),
                  std::string("L8: heightForWidth == contentExtent-höjden") +
                      (ram ? " + ramen" : ""),
                  h ? tal(*h) + " mot " + tal(e) : "nullopt");
        }
        Vy satt;
        satt.height = 50.0f;
        check(!satt.heightForWidth(b.c, 240.0f).has_value(),
              "L8: en satt höjd vinner — kroken svarar inte");
    }

    // TIDEN för ett syntetiskt dokument på 200 KB — utskriven, utan tröskel.
    if (g_verbose) {
        std::string dok;
        while (dok.size() < 200 * 1024)
            dok += "Ett stycke med **fet** text, `kod` och en [länk](#a) som "
                   "bryts över raderna.\n\n";
        Bänk         b;
        md::Document d;
        md::Layout   L;
        const double tp = millisekunder([&] { d = md::parse(dok); });
        const double tl = millisekunder([&] { L = md::build(b.c, d, {}, 600.0f); });
        std::printf("  --   200 KB: tolkning %.1f ms, layout %.1f ms, %zu rader\n",
                    tp, tl, L.lines.size());
    }
}

// ================================================================ DEL W

// Headless (inspelningen): färgerna, raderna som ritas och språkbytet.
void delWHeadless()
{
    std::printf("\n=== W (headless). widgeten mot inspelningen ===\n");
    const glaze::Theme::Colors& col = glaze::Theme::slateDefault().light.colors;

    // W6 — INAKTIV: all text i `textDim`. Aktiv: länken i `link`, resten `text`.
    {
        Bänk b;
        Vy   v;
        v.width  = 400.0f;
        v.height = 120.0f;
        v.setText("Text och en [länk](#a).");
        glaze::Ctx c = b.c;
        c.box        = v.resolve(c);
        v.draw(c);
        bool aktiv = !b.r.texter.empty();
        for (const Inspelning::Text& t : b.r.texter)
            aktiv = aktiv && t.färg == (t.s == "länk" ? col.link : col.text);
        check(aktiv, "W6: aktiv — länken i link, resten i text");
        b.r.texter.clear();
        v.disable();
        v.draw(c);
        bool dim = !b.r.texter.empty();
        for (const Inspelning::Text& t : b.r.texter)
            dim = dim && t.färg == col.textDim;
        check(dim, "W6: inaktiv — all text i textDim");
    }

    // W8 — 1 000 STYCKEN: varje ritning besöker de SYNLIGA raderna (högst
    // innehållets höjd / radhöjden + 1), också efter en rullning.
    {
        Bänk        b;
        Vy          v;
        std::string dok;
        for (int i = 0; i < 1000; ++i)
            dok += "Stycke " + std::to_string(i) + "\n\n";
        v.width  = 300.0f;
        v.height = 200.0f;
        v.setText(dok);
        glaze::Ctx c = b.c;
        c.box        = v.resolve(c);
        const std::size_t tak = std::size_t(std::ceil(200.0f / kHöjd)) + 1;
        v.draw(c);
        check(!b.r.texter.empty() && b.r.texter.size() <= tak,
              "W8: överst ritas bara de synliga raderna",
              std::to_string(b.r.texter.size()));
        b.r.texter.clear();
        v.setScroll(0.0f, 9000.0f);
        v.draw(c);
        check(!b.r.texter.empty() && b.r.texter.size() <= tak &&
                  b.r.texter[0].s != "Stycke 0",
              "W8: rullad — bara de synliga, och inte de första",
              std::to_string(b.r.texter.size()) +
                  (b.r.texter.empty() ? "" : " från " + b.r.texter[0].s));
    }

    // W9 — SPRÅKBYTET: `setText(glaze::tr(46))` tolkas om när språket byts —
    // svenskans text ryms på en rad i 160 px, engelskans bryts på två.
    {
        Bänk b;
        Vy   v;
        v.width = vyBredd(b.stil, 160.0f);
        glaze::setLanguage("sv");
        v.setText(glaze::tr(46));
        const float sv = v.contentSize(b.c).h;
        glaze::setLanguage("en");
        const float en = v.contentSize(b.c).h;
        glaze::setLanguage("sv");
        check(nära(en, sv + kHöjd), "W9: språkbytet ger den nya texten och höjden",
              tal(sv) + " → " + tal(en));
    }

    // W7 — linkAt utan fönster är tomt.
    {
        Vy v;
        v.setText("[länk](#a)");
        check(v.linkAt(10.0f, 10.0f).empty(), "W7: linkAt utan fönster är tomt");
    }
}

std::string stycken(int n, const char* prefix)
{
    std::string ut;
    for (int i = 0; i < n; ++i)
        ut += std::string(prefix) + " " + std::to_string(i) + "\n\n";
    return ut;
}

// Dokumentet fönsterdelen klickar i: länk 0 till `#b`, länk 1 extern med en
// titel; `# b` långt ner, och nog med text efter för att ankaret aldrig ska
// klampas.
const std::string kDok1 = "[till b](#b) och [extern](https://example.com \"Exempel\")\n\n"
                          "# a\n\n" +
                          stycken(20, "Stycke") + "# b\n\n" + stycken(60, "Mer");
// Efter bytet (W4): länk 0 är `#a`, och `# b` står högt upp.
const std::string kDok2 =
    "[till a](#a)\n\n# b\n\nkort\n\n# a\n\n" + stycken(80, "Rad");

struct Fonster : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;
    using glaze::AppWindow::makeCtx;

    Vy           vy;
    Vy           inaktiv;
    glaze::Panel rull;
    Vy           vyA;   // AUTO-hög och sträckt i en rullande panel (L8)

    Fonster() : glaze::AppWindow("markdown_gate", 900, 640)
    {
        vy.x            = 10;
        vy.y            = 10;
        vy.width        = 420;
        vy.height       = 240;
        inaktiv.x       = 450;
        inaktiv.y       = 10;
        inaktiv.width   = 400;
        inaktiv.height  = 100;
        rull.x          = 450;
        rull.y          = 140;
        rull.width      = 300;
        rull.height     = 150;
        rull.showScrollbars(true);
        vyA.anchors = glaze::Anchors{true, true, true, false};
        vyA.x       = 0;
        vyA.y       = 0;
        vyA.right   = 0;
        root().adopt(vy);
        root().adopt(inaktiv);
        root().adopt(rull);
        rull.adopt(vyA);
    }

    void mus(gfx::EventType t, gfx::Pointf p)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = gfx::MouseButton::Left;
        handleEvent(e);
    }
    void klick(gfx::Pointf ned, gfx::Pointf upp)
    {
        mus(gfx::EventType::MouseMotion, ned);
        mus(gfx::EventType::MouseButtonDown, ned);
        mus(gfx::EventType::MouseMotion, upp);
        mus(gfx::EventType::MouseButtonUp, upp);
    }
    void hjul(gfx::Pointf p, float rader)
    {
        gfx::Event e;
        e.type     = gfx::EventType::MouseWheel;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.wheelY   = rader;
        handleEvent(e);
    }
    void rita()
    {
        invalidate();
        window().renderFrame();
    }
};

// Vyns innehållsbredd i ett öppet fönster — samma kontrakt som `vyBredd`.
float innehåll(const Vy& v, const glaze::Ctx& c)
{
    const glaze::Style& m = *c.style;
    return v.actualSize().w - (v.isBorder() ? 2.0f * m.panelInset : 0.0f) -
           m.markdownPad - std::max(m.markdownPad, m.scrollThick);
}

md::Layout layoutFör(const Vy& v, const std::string& text)
{
    const glaze::Ctx c = v.behaviorCtx();
    return md::build(c, md::parse(text), {}, innehåll(v, c));
}

struct Punkt {
    gfx::Pointf fys;     // fönstrets fysiska px — musens rymd
    gfx::Pointf lokal;   // vyns lokala logiska px — `linkAt`s rymd
};

// Mitten av den första delen `vill` tar, mätt ur vyns layout i SAMMA Ctx som
// vyn — målet räknas, beteendet mäts.
std::optional<Punkt> delen(const Vy& v, const std::function<bool(const md::Item&)>& vill)
{
    const glaze::Ctx    c     = v.behaviorCtx();
    const md::Layout    L     = layoutFör(v, v.text());
    const gfx::Rectf    r     = v.resolve(c);
    const glaze::Style& m     = *c.style;
    const float         inset = v.isBorder() ? m.panelInset : 0.0f;
    for (const md::Line& l : L.lines)
        for (std::uint32_t i = l.first; i < l.first + l.count; ++i) {
            const md::Item& it = L.items[i];
            if (!vill(it))
                continue;
            const gfx::Pointf lokal{inset + m.markdownPad + it.x + it.w / 2.0f -
                                        v.scrollX(),
                                    inset + m.markdownPad + l.y + l.h / 2.0f -
                                        v.scrollY()};
            return Punkt{{r.x + lokal.x * c.scale, r.y + lokal.y * c.scale}, lokal};
        }
    return std::nullopt;
}

float ankaret(const Vy& v, const std::string& text, const std::string& namn)
{
    for (const auto& [n, y] : layoutFör(v, text).anchors)
        if (n == namn)
            return y;
    return -1.0f;
}

std::string formNamn(gfx::PointerShape s)
{
    return s == gfx::PointerShape::Hand    ? "Hand"
           : s == gfx::PointerShape::Arrow ? "Arrow"
                                           : "annan";
}

void delWFonster()
{
    std::printf("\n=== W. widgeten i ett dolt fönster ===\n");
    glaze::setLanguage("sv");
    Fonster w;
    w.setOffscreen(true);
    bool kördes = false;

    glaze::app().post([&] {
        kördes = true;
        if (!w.isOpen()) {
            std::printf("  --   fönsterfallen hoppas över: fönstret kom aldrig upp\n");
            ++g_skips;
            w.close();
            return;
        }
        Vy& vy = w.vy;
        vy.setText(kDok1);
        w.inaktiv.setText("[länk](#a) i en inaktiv vy");
        w.inaktiv.disable();
        w.vyA.setText("En AUTO-hög vy i en rullande panel, med text som bryts "
                      "över flera rader när panelen är smal nog. "
                      "Och en mening till för att den ska bli hög.");
        w.rita();

        const auto länk = [](int n) {
            return [n](const md::Item& it) { return it.link == n; };
        };
        const std::optional<Punkt> l0 = delen(vy, länk(0));
        const std::optional<Punkt> l1 = delen(vy, länk(1));
        const std::optional<Punkt> och = delen(vy, [](const md::Item& it) {
            return it.link < 0 && it.text.find("och") != std::string::npos;
        });
        const std::optional<Punkt> li = delen(w.inaktiv, länk(0));
        check(l0 && l1 && och && li, "W: (fixturen) länkarna och texten hittas");
        if (!(l0 && l1 && och && li)) {
            w.close();
            return;
        }

        // W1 — HANDEN OCH TOOLTIPEN.
        w.mus(gfx::EventType::MouseMotion, l1->fys);
        check(w.window().cursor() == gfx::PointerShape::Hand,
              "W1: handen över en länk", formNamn(w.window().cursor()));
        {
            const glaze::TrText t1 = vy.toolTipAt(vy.behaviorCtx(), l1->fys.x, l1->fys.y);
            const glaze::TrText t0 = vy.toolTipAt(vy.behaviorCtx(), l0->fys.x, l0->fys.y);
            check(t1.str() == "Exempel" && t0.str() == "#b",
                  "W1: tooltipen är länkens titel, annars dess adress",
                  t1.str() + " / " + t0.str());
        }
        w.mus(gfx::EventType::MouseMotion, och->fys);
        check(w.window().cursor() == gfx::PointerShape::Arrow,
              "W1: pilen bredvid länken", formNamn(w.window().cursor()));
        w.mus(gfx::EventType::MouseMotion, li->fys);
        check(w.window().cursor() == gfx::PointerShape::Arrow,
              "W1: en inaktiv vy ger ingen hand", formNamn(w.window().cursor()));

        // W2 — KLICKET ÄR EN KNAPPS.
        int               antal  = 0;
        glaze::Widget*    sender = nullptr;
        std::string       url;
        bool              tar    = true;
        std::function<void()> iHandlern;
        vy.onLinkClick = [&](glaze::Widget* s, glaze::LinkClickArgs& e) {
            ++antal;
            sender    = s;
            url       = e.url;
            e.handled = tar;
            if (iHandlern)
                iHandlern();
        };
        w.klick(l0->fys, l0->fys);
        check(antal == 1 && sender == &vy && url == "#b",
              "W2: tryck och släpp på samma länk — ett onLinkClick, sender == vyn",
              std::to_string(antal) + " " + url);
        w.klick(l0->fys, l1->fys);
        check(antal == 1, "W2: tryck på en länk och släpp på en annan — inget",
              std::to_string(antal));
        w.klick(l0->fys, gfx::Pointf{5.0f, 600.0f});
        check(antal == 1, "W2: tryck och dra ut — inget", std::to_string(antal));
        w.mus(gfx::EventType::MouseMotion, li->fys);

        // W7 — linkAt i LOKALA LOGISKA px.
        check(vy.linkAt(l0->lokal.x, l0->lokal.y) == "#b" &&
                  vy.linkAt(och->lokal.x, och->lokal.y).empty(),
              "W7: linkAt ger länken i logiska px, tomt bredvid",
              vy.linkAt(l0->lokal.x, l0->lokal.y));

        // W6 — inget event från en inaktiv vy.
        int inaktiva = 0;
        w.inaktiv.onLinkClick = [&] { ++inaktiva; };
        w.klick(li->fys, li->fys);
        check(inaktiva == 0, "W6: en inaktiv vy fyrar inget onLinkClick");

        // W3 — `handled`: sann ⇒ ingen rullning; falsk ⇒ förvalet rullar till
        // ankaret (klampat) vid nästa ritning.
        tar = true;
        w.klick(l0->fys, l0->fys);
        w.rita();
        check(vy.scrollY() == 0.0f, "W3: handled = true ⇒ rullningen orörd",
              tal(vy.scrollY()));
        tar = false;
        w.klick(l0->fys, l0->fys);
        w.rita();
        const float yb = ankaret(vy, kDok1, "b");
        const float vb = std::min(yb, vy.scrollRange(vy.behaviorCtx()).h);
        check(yb > 0.0f && nära(vy.scrollY(), vb),
              "W3: handled = false ⇒ rullningen = ankarets y (klampat)",
              tal(vy.scrollY()) + " mot " + tal(vb));

        // W4 — URL:EN KOPIERAS FÖRE HANDLERN: handlern byter dokument, där
        // länk 0 är `#a` och `# b` står högt upp — förvalet följer `#b`.
        vy.setScroll(0.0f, 0.0f);
        w.rita();
        iHandlern = [&] { vy.setText(kDok2); };
        w.klick(l0->fys, l0->fys);
        iHandlern = nullptr;
        w.rita();
        const float y2 = ankaret(vy, kDok2, "b");
        const float ya = ankaret(vy, kDok2, "a");
        check(y2 > 0.0f && !nära(y2, ya) && nära(vy.scrollY(), y2),
              "W4: förvalet följer den KOPIERADE url:en, i det nya dokumentet",
              tal(vy.scrollY()) + " mot " + tal(y2));

        // W5 — VAKTEN: handlern lossar den lånade vyn och låter den leva;
        // inget förval körs, så rullningen är orörd också när vyn sitter i
        // fönstret igen och ritas.
        vy.setText(kDok1);
        vy.setScroll(0.0f, 0.0f);
        w.rita();
        iHandlern = [&] { w.root().unadopt(vy); };
        w.klick(l0->fys, l0->fys);
        iHandlern = nullptr;
        const bool lossad = vy.appWindow() == nullptr;
        w.root().adopt(vy);
        w.rita();
        check(lossad && vy.scrollY() == 0.0f,
              "W5: en handler som lossar vyn stoppar förvalet",
              std::string(lossad ? "lossad" : "kvar") + ", " + tal(vy.scrollY()));

        // W10 — setText nollar rullningen, och ett scrollToAnchor FÖRE setText
        // gäller ändå (det löses vid ritningen).
        vy.setScroll(0.0f, 300.0f);
        vy.setText(kDok2);
        check(vy.scrollY() == 0.0f, "W10: en ny text rullar till toppen",
              tal(vy.scrollY()));
        vy.scrollToAnchor("b");
        vy.setText(kDok1);
        w.rita();
        check(nära(vy.scrollY(), vb),
              "W10: scrollToAnchor före setText gäller det nya dokumentet",
              tal(vy.scrollY()) + " mot " + tal(vb));

        // W8 — HJULET RULLAR vyn utan fokus.
        vy.setScroll(0.0f, 0.0f);
        w.rita();
        w.hjul(l0->fys, -3.0f);
        check(vy.scrollY() > 0.0f, "W8: hjulet rullar", tal(vy.scrollY()));

        // L8 i fönstret — en AUTO-hög sträckt vy i en rullande panel mäts
        // utan rekursion och får höjden för sin bredd.
        {
            const glaze::Ctx         c = w.vyA.behaviorCtx();
            const gfx::Sizef         s = w.vyA.actualSize();
            const std::optional<float> h = w.vyA.heightForWidth(c, s.w);
            check(h && s.h > 0.0f && std::fabs(s.h - *h) < 1.0f,
                  "L8: AUTO-hög vy i en rullande panel — höjden för bredden",
                  tal(s.h) + (h ? " mot " + tal(*h) : std::string(" mot nullopt")));
        }

        // W11 — före stängningen har fönstret sina textfonter.
        check(w.makeCtx().textFonts != nullptr,
              "W11: ett öppet fönster har textfonter (Ctx::textFonts)");
        w.close();
    });
    w.run();

    if (!kördes) {
        std::printf("  --   fönsterfallen kördes ALDRIG (pumpen startade inte)\n");
        ++g_skips;
        return;
    }
    // W11 — STÄNGT: `executeClose` river textfonterna med rollfonten.
    if (g_skips == 0)
        check(w.makeCtx().textFonts == nullptr,
              "W11: efter close() är Ctx::textFonts nullptr");
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("markdown_gate — Markdown-vyn\n");
    delP();
    delL();
    delWHeadless();
    delWFonster();

    std::printf("\n%s — %d fall, %d fel, %d överhoppade block\n",
                g_fails == 0 ? "MARKDOWN_GATE GRÖN" : "MARKDOWN_GATE FÄLLD",
                g_cases, g_fails, g_skips);
    if (g_fails != 0)
        return 1;
    return g_skips == 0 ? 0 : 77;
}
