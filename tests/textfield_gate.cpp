// textfield_gate — TEXTFÄLTENS grind: ångra/gör om, lösenordsfältet och redigerings-
// genvägarnas AltGr-regel. Radbrytningen som följer layoutens bredd
// mäts i layout_gate fall 20 och measure_gate avsnitt 3, och TextAreas
// ångra genom fönstret i textarea_gate F13.
//
// Kör:  ./build/tests/textfield_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = ett fönsterblock
//       kunde inte köras (ingen display, eller urklippet tar inte emot text —
//       Wayland utan input-serial) — headless-delen är då ändå mätt
//
// TVÅDELAD (textarea_gate-prejudikatet):
//
//   HEADLESS — fälten ändras utan fönster: redigeringsvägen behöver inget
//   Ctx, och TextAreas `relayout` avstår utan ett öppet fönster.
//     H1  HISTORIKEN (`textfield::History`, intern — src/text_field.h): "hej
//         världen" är två steg, backsteg i följd ett, Delete i följd ett,
//         `Block` ett eget steg som förseglar efter sig, `seal()` bryter, en
//         ny redigering KAPAR gör om-svansen, 101 steg ⇒ 100 kvar, budgeten
//         behåller det nyaste (också ett ensamt för stort), och borttaget ==
//         infogat registreras inte
//     H2  TEXTINPUT genom sina krokar (`keyDown`/`textInput`/`focusLost`):
//         Ctrl+Z, Ctrl+Y och Ctrl+Shift+Z · EXAKT ett onChange per ångra och
//         gör om · markeringen ÅTERSTÄLLD efter skrivning över en markering
//         (också en bakvänd) · Vänster+Höger, Enter och fokusförlust bryter
//         gruppen · `setText` tömmer, samma text gör det inte · skrivskyddat:
//         konsumerat, ingen ändring, och historiken gäller igen efteråt
//     H3  TEXTAREA på textnivå: Enter och stycken, ångra/gör om ger rätt
//         `text()` OCH caret — också där stegets caret inte är där
//         ändringen slutar (Delete i följd, en bakvänd markering)
//     H4  LÖSENORDET mot en testfont där "•" har ett EGET teckensteg (6 mot
//         10): caretens läge vid varje gräns i en blandad 1/2/3/4-byte-text
//         är prickar och inte tecken · ett klick på en gräns mellan prickar
//         ger den RIKTIGA byten · en bakvänd markering · att RITNINGENS
//         strängar (det `drawContent` skickar till raden) är masken · ingen
//         historik i lösenordsläge · `setPassword(true)` tömmer · Ctrl+C och
//         Ctrl+X konsumeras och klipp ut raderar inte
//     H5  GENVÄGEN, PLATTFORMENS: Windows/Linux — `editShortcut`
//         sann för Ctrl och Ctrl+Shift, falsk för Ctrl+Alt och Super, och
//         Ctrl+Alt+A/Z och Super+A/Z markerar och ångrar inte; macOS — sann
//         för Cmd och Cmd+Shift, falsk för Cmd+Option, Cmd+Ctrl och Ctrl, och
//         Ctrl+A/Z och Cmd+Option+A/Z gör ingenting — i BÅDA fälten; och
//         plattformens genväg markerar allt. Fallen som DRIVER ett fält med en
//         genväg (H1–H4, H6, W1, W2) använder plattformens modifierare
//         (`kRedigera`, skriven av grinden — inte läst ur produktionen)
//     H6  MAXLÄNGDEN: `fitMaxLength` mot förväntat värde — också en
//         text redan ÖVER gränsen (ingen runtslagning) · "åäö" med gränsen 2
//         ⇒ "åä" · en inklistring kapas, och ångra/gör om tar det KAPADE
//         steget · skrivande över en markering inom budget · vid gränsen
//         avvisas skrivandet · `setText` över gränsen står kvar, ryms inget
//         står markeringen kvar, backsteg fungerar · lösenordsläget räknar
//         kodpunkter · −1 ignoreras, 0 obegränsat, `setProp` bara ≥ 0 ·
//         TextArea: `\n` är en kodpunkt, Enter vid gränsen ger ingen rad,
//         en inklistring kapas med radbrytningarna kvar
//     H7  PLATSHÅLLAREN mot en inspelande renderer med FÄRG: ett
//         tomt fält ritar den i `textDim` · med text inte · med fokus den OCH
//         caretens rekt · i lösenordsläge i klartext · bara första raden i
//         TextInput · `glaze::tr(N)` följer `setLanguage` · `setTextToken`
//         tar "placeholder" men inte "text" · TextArea med och utan brytning
//   MED FÖNSTER — ett DOLT fönster (`setOffscreen`), ALLT genom fönstrets
//   riktiga ingång (`handleEvent`):
//     W1  menyradens accelerator Ctrl+Z: med fältet fokuserat ångrar FÄLTET
//         och menyns handler körs INTE, också med tom historik; med fokus på
//         en knapp körs menyn
//     W2  lösenordsfältet: Ctrl+C och Ctrl+X ändrar varken urklippet eller
//         texten. URKLIPPET ÄR DET RIKTIGA (textarea_gates skäl): på Wayland
//         tar kompositören bara emot ett urklipp med en färsk input-serial,
//         och där hoppas blocket över med en skriven rad (exit 77)
//     W3  TEXTSTORLEKEN — ett eget dolt fönster med ett av varje i
//         stilens storlek och ett med `setTextSize(glaze::px(28))`: caretens
//         höjd växer (≥ 1,5×), fältets AUTO-höjd växer LIKA mycket som
//         caretens (± 1 fysisk px), vallistans AUTO-höjd är fältets i båda
//         storlekarna, det flerradiga fältets `heightForRows(1)` växer lika
//         mycket, listans rad 0 täcker 1,5 stilrader i den stora storleken ·
//         mm(8) ger en högre caret än mm(4) · world(2) och px(−3) ignoreras ·
//         0 ger stilens igen
//
// SABOTAGEN den ska fälla (ett i taget):
//   * sammanslagning utan ordregeln — H1 ("hej världen" blir ett steg)
//   * ingen kapning av gör om-svansen i `record` — H1 (tre ångra i stället
//     för två)
//   * budgeten och djupet bortplockade — H1 (101 steg kvar, två stora)
//   * `seal` saknas i `movedCaret` — H2 (Vänster+Höger slår ihop grupperna)
//   * `restore` utan `edited` — H2 (inget onChange vid ångra)
//   * markeringen ur steget ignorerad i `restore` — H2, H3
//   * `setText` som inte tömmer historiken — H2
//   * Enter utan `seal` — H2
//   * `caretBounds` mot `text_` i stället för `shown` — H4
//   * `indexFromX` utan `fromShown` — H4 (byten i masken i stället för i
//     texten), och mot `text_` i stället för `shown` — H4 (gränsen 2|3)
//   * omaskerad rad i `drawContent` — H4 (ritningens strängar)
//   * `replace` som registrerar i lösenordsläge — H4 (`canUndo()` svarar
//     sant: lösenordsläget har ingen egen spärr i `canUndo`, för där FINNS
//     ingen historik — se textinput.cpp)
//   * `setPassword` som inte tömmer — H4
//   * Ctrl+X utan lösenordsspärren — H4 (texten raderas), W2; Ctrl+C utan
//     den — W2
//   * `editShortcut = mods & kModCtrl` — H5; macOS-grenen bort (Ctrl på
//     macOS) — H5 på macOS, och varje fall som driver ett fält med Cmd;
//     Option inte utesluten på macOS — H5 (Cmd+Option)
//   * maxlängden räknad i BYTE — H6 ("åäö" blir "å")
//   * markeringen raderad när inget ryms — H6
//   * `setText` som kapar — H6; `fitMaxLength` utan `resten >= max`-vakten —
//     H6 (texten över gränsen släpper igenom allt)
//   * TextAreas insättning enradsfiltrerad eller historiken med det okapade —
//     H6 (radbrytningarna, ångra)
//   * platshållaren ritad också med text — H7; maskerad — H7; i `text` i
//     stället för `textDim` — H7; utan `setTextToken`-grenen — H7
//   * acceleratorerna flyttade FÖRE den fokuserade widgeten i
//     `AppWindow::handleEvent` — W1
//   * `caretBounds` med `c` i stället för `textCtx(c)` — W3 (caretens höjd
//     är stilens) · `TextInput::contentSize` med `c` — W3 (AUTO-höjden
//     växer inte) · `Dropdown::contentSize` med `c` — W3 · `ListBox::rowH`
//     utan textstorleken — W3 (rowAt) · `TextArea::heightForRows` med `c` —
//     W3 · `textSizeValue` som släpper igenom världsenheter — W3.
//
// VAD DEN INTE MÄTER, så anspråket inte överdrivs:
//   - PIXLAR: hur masken SER UT, och om glyfen finns i UI-fonten på varje
//     plattform (raderna i doc/os/*/TESTS.md och ögat).
//   - backendernas tangentöversättning — grinden matar `Ctrl|Alt` direkt; att
//     Windows levererar AltGr så, och att WM_CHAR sedan ger ą, provas med
//     polsk layout (doc/os/windows/TESTS.md).
//   - macOS Cmd-genvägen genom ett riktigt tangentbord, och IME.
//   - hur grupperingen "känns" — bara att den följer reglerna.
//   - W3: att texten RITAS i storleken (pixlarna — ögat),
//     mm mot en LINJAL (skärmens mått är `display_gate`s), vallistans POPUP i
//     storleken, de fria fonternas tak (`FontSet::trim`) och att en storlek
//     lika med stilens delar fönstrets font.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <gfx/null_renderer.h>
#include <gfx/platform.h>

#include <glaze/glaze.h>

#include "text_field.h"   // intern (tests/CMakeLists.txt pekar hit)

namespace {

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
    ++g_fails;
}

bool nära(float a, float b) { return (a > b ? a - b : b - a) < 0.01f; }

std::string tal(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}

constexpr std::uint32_t kCtrl  = gfx::kModCtrl;
constexpr std::uint32_t kShift = gfx::kModShift;
constexpr std::uint32_t kAlt   = gfx::kModAlt;
constexpr std::uint32_t kCmd   = gfx::kModCmd;

// FÄLTENS GENVÄGAR: plattformens regel, skriven
// av grinden själv och inte läst ur `editShortcut` — Cmd på macOS, Ctrl annars.
// Fallen som DRIVER ett fält med en genväg använder `kRedigera`; H5 prövar
// regeln, och menyns kolliderande genväg (W1) är samma tangent.
#ifdef __APPLE__
constexpr std::uint32_t kRedigera   = kCmd;
constexpr const char*   kÅngraNamn  = "Cmd+Z";
#else
constexpr std::uint32_t kRedigera   = kCtrl;
constexpr const char*   kÅngraNamn  = "Ctrl+Z";
#endif

// ============================================================== H1 HISTORIKEN

using glaze::textfield::EditKind;
using glaze::textfield::History;
using glaze::textfield::Restore;

// En TEXT och dess historik — grindens egen redigering, skriven ur
// kontraktet (ersätt [pos, pos + borttaget) med infogat, steget registrerat
// med markeringen före), aldrig ur fältens kod.
struct Redigering {
    History     h;
    std::string s;
    std::size_t caret = 0;

    void gör(std::size_t pos, std::size_t len, const std::string& ins,
             EditKind kind)
    {
        h.record(pos, s.substr(pos, len), ins, caret, caret, kind);
        s.replace(pos, len, ins);
        caret = pos + ins.size();
    }
    void skriv(const std::string& ord)   // ett tecken i taget, vid caret
    {
        for (std::size_t i = 0; i < ord.size();) {
            std::size_t j = i + 1;
            while (j < ord.size() &&
                   (static_cast<unsigned char>(ord[j]) & 0xC0) == 0x80)
                ++j;
            gör(caret, 0, ord.substr(i, j - i), EditKind::Typing);
            i = j;
        }
    }
    bool ångra()
    {
        const auto r = h.undo();
        if (!r)
            return false;
        s.replace(r->pos, r->len, r->text);
        caret = r->caret;
        return true;
    }
    bool görOm()
    {
        const auto r = h.redo();
        if (!r)
            return false;
        s.replace(r->pos, r->len, r->text);
        caret = r->caret;
        return true;
    }
};

void fallHistorik()
{
    std::printf("\n=== H1. historiken ===\n");

    // "hej världen" — TVÅ steg: "hej " och "världen".
    {
        Redigering r;
        r.skriv("hej v\xC3\xA4rlden");
        const bool a1 = r.ångra();
        check(a1 && r.s == "hej " && r.caret == 4,
              "H1: \"hej världen\" — första ångra tar \"världen\"", r.s);
        const bool a2 = r.ångra();
        check(a2 && r.s.empty() && !r.h.canUndo(),
              "H1: …och andra ångra \"hej \" — två steg, inget mer", r.s);
        const bool g1 = r.görOm();
        const bool g2 = r.görOm();
        check(g1 && g2 && r.s == "hej v\xC3\xA4rlden" && !r.h.canRedo() &&
                  r.caret == r.s.size(),
              "H1: gör om två gånger — hela texten, careten sist", r.s);
    }
    // ORDREGELN INNE I EN INMATNING: "hej världen" i ETT
    // `textInput`-anrop ger samma två steg, och "he" + "j världen" likaså —
    // `skriv` ovan matar tecken för tecken och hade dolt felet.
    {
        Redigering r;
        r.gör(0, 0, "hej v\xC3\xA4rlden", EditKind::Typing);
        const bool a1 = r.ångra();
        check(a1 && r.s == "hej " && r.caret == 4,
              "H1: en inmatning \"hej världen\" — första ångra tar \"världen\"",
              r.s);
        const bool a2 = r.ångra();
        check(a2 && r.s.empty() && !r.h.canUndo(),
              "H1: …och andra ångra \"hej \"", r.s);
    }
    {
        Redigering r;
        r.skriv("he");
        r.gör(r.caret, 0, "j v\xC3\xA4rlden", EditKind::Typing);
        const bool a1 = r.ångra();
        check(a1 && r.s == "hej ",
              "H1: \"he\" + \"j världen\" — första ångra tar \"världen\"", r.s);
        const bool a2 = r.ångra();
        check(a2 && r.s.empty() && !r.h.canUndo(),
              "H1: …och andra ångra \"hej \" (sammanslaget med \"he\")", r.s);
    }
    // BACKSTEG i följd = ETT steg, och ångra lägger careten där den stod.
    {
        Redigering r;
        r.s     = "abcdef";
        r.caret = 6;
        for (int i = 0; i < 3; ++i)
            r.gör(r.caret - 1, 1, "", EditKind::BackDelete);
        check(r.s == "abc", "H1: tre backsteg ger \"abc\"", r.s);
        const bool a = r.ångra();
        check(a && r.s == "abcdef" && r.caret == 6 && !r.h.canUndo(),
              "H1: backsteg i följd är ETT steg — careten tillbaka på 6",
              r.s + " / " + std::to_string(r.caret));
    }
    // DELETE i följd = ETT steg.
    {
        Redigering r;
        r.s     = "abcd";
        r.caret = 1;
        r.gör(1, 1, "", EditKind::FwdDelete);
        r.caret = 1;
        r.gör(1, 1, "", EditKind::FwdDelete);
        const bool a = r.ångra();
        check(a && r.s == "abcd" && !r.h.canUndo(),
              "H1: Delete i följd är ETT steg", r.s);
    }
    // BLOCK är ett eget steg och förseglar efter sig.
    {
        Redigering r;
        r.skriv("ab");
        r.gör(2, 0, "XY", EditKind::Block);   // inklistring vid caret
        r.skriv("c");
        r.ångra();
        check(r.s == "abXY", "H1: skrivandet efter ett Block är ett eget steg",
              r.s);
        r.ångra();
        check(r.s == "ab", "H1: Blocket är ett eget steg", r.s);
        r.ångra();
        check(r.s.empty() && !r.h.canUndo(), "H1: \"ab\" är det första steget",
              r.s);
    }
    // SEAL bryter gruppen.
    {
        Redigering r;
        r.skriv("ab");
        r.h.seal();
        r.skriv("cd");
        r.ångra();
        check(r.s == "ab", "H1: seal() bryter — \"cd\" är ett eget steg", r.s);
    }
    // En NY redigering efter ångra KAPAR gör om-svansen.
    {
        Redigering r;
        r.skriv("a");
        r.h.seal();
        r.skriv("b");
        r.ångra();   // "a", "b" går att göra om
        check(r.h.canRedo(), "H1: efter ångra finns ett steg att göra om");
        r.skriv("c");   // "ac"
        int ångrade = 0;
        while (r.ångra())
            ++ångrade;
        check(!r.h.canRedo() || ångrade > 0, "H1: (räkningen gick)");
        check(ångrade == 2 && r.s.empty(),
              "H1: den nya redigeringen kapade \"b\" — två steg kvar, inte tre",
              std::to_string(ångrade) + " / \"" + r.s + "\"");
    }
    // DJUPET: 101 steg ⇒ 100 kvar.
    {
        Redigering r;
        for (int i = 0; i < 101; ++i)
            r.gör(r.caret, 0, "x", EditKind::Block);
        int ångrade = 0;
        while (r.ångra())
            ++ångrade;
        check(ångrade == 100 && r.s == "x",
              "H1: 101 steg — de 100 senaste går att ångra, det äldsta är släppt",
              std::to_string(ångrade));
    }
    // BUDGETEN (4 MiB): två steg à 3 MiB ⇒ bara det nyaste kvar; ett ensamt
    // på 5 MiB behålls.
    {
        const std::string stor(std::size_t(3) << 20, 'x');
        Redigering        r;
        r.gör(0, 0, stor, EditKind::Block);
        r.gör(r.caret, 0, stor, EditKind::Block);
        int ångrade = 0;
        while (r.ångra())
            ++ångrade;
        check(ångrade == 1 && r.s.size() == stor.size(),
              "H1: budgeten släppte det äldre steget och behöll det nyaste",
              std::to_string(ångrade));
        Redigering ensam;
        ensam.gör(0, 0, std::string(std::size_t(5) << 20, 'y'),
                  EditKind::Block);
        check(ensam.h.canUndo() && ensam.ångra() && ensam.s.empty(),
              "H1: ett ensamt steg över budgeten behålls och går att ångra");
    }
    // NO-OP: borttaget == infogat registreras inte.
    {
        History h;
        h.record(0, "a", "a", 0, 1, EditKind::Block);
        check(!h.canUndo(), "H1: borttaget == infogat registreras inte");
    }
}

// ============================================================ H2 TEXTINPUT

// De skyddade krokarna anropbara — det en subklass får nå — och onChange
// RÄKNAT.
struct ProvFält : glaze::TextInput {
    using glaze::TextInput::focusLost;
    using glaze::TextInput::keyDown;
    using glaze::TextInput::textInput;
    int ändringar = 0;
    ProvFält()
    {
        onChange = [this] { ++ändringar; };
    }
    void skriv(const std::string& ord)
    {
        for (std::size_t i = 0; i < ord.size();) {
            std::size_t j = i + 1;
            while (j < ord.size() &&
                   (static_cast<unsigned char>(ord[j]) & 0xC0) == 0x80)
                ++j;
            textInput(ord.substr(i, j - i).c_str());
            i = j;
        }
    }
};

std::string sel(const glaze::TextInput& f)
{
    return std::to_string(f.selBegin()) + ".." + std::to_string(f.selEnd()) +
           " caret " + std::to_string(f.caret());
}

void fallTextInput()
{
    std::printf("\n=== H2. TextInput ===\n");

    // Ctrl+Z, Ctrl+Y och Ctrl+Shift+Z — och EXAKT ett onChange per steg.
    {
        ProvFält f;
        f.skriv("hej v\xC3\xA4rlden");
        f.ändringar = 0;
        const bool tog = f.keyDown(gfx::Key::Z, kRedigera);
        check(tog && f.text() == "hej " && f.ändringar == 1,
              "H2: Ctrl+Z ångrar ordet — konsumerad, ett onChange", f.text());
        f.keyDown(gfx::Key::Z, kRedigera);
        check(f.text().empty() && f.ändringar == 2 && !f.canUndo(),
              "H2: Ctrl+Z igen — tom, två onChange", f.text());
        f.keyDown(gfx::Key::Y, kRedigera);
        check(f.text() == "hej " && f.ändringar == 3 && f.canRedo(),
              "H2: Ctrl+Y gör om", f.text());
        f.keyDown(gfx::Key::Z, kRedigera | kShift);
        check(f.text() == "hej v\xC3\xA4rlden" && f.ändringar == 4 &&
                  !f.canRedo(),
              "H2: Ctrl+Shift+Z gör också om", f.text());
        const bool tom = f.keyDown(gfx::Key::Y, kRedigera);
        check(tom && f.ändringar == 4,
              "H2: Ctrl+Y utan något att göra om — konsumerad, inget onChange");
        check(f.undo() && f.text() == "hej " && f.ändringar == 5 &&
                  f.redo() && f.ändringar == 6,
              "H2: undo()/redo() är samma väg som tangenterna");
    }
    // MARKERINGEN återställs efter skrivning över en markering — också en
    // BAKVÄND (careten först).
    {
        ProvFält f;
        f.setText("abcdef");
        f.setSelection(1, 4);
        f.skriv("X");
        check(f.text() == "aXef", "H2: skrivande ersätter markeringen",
              f.text());
        f.keyDown(gfx::Key::Z, kRedigera);
        check(f.text() == "abcdef" && f.selBegin() == 1 && f.selEnd() == 4 &&
                  f.caret() == 4,
              "H2: ångra återställer texten OCH markeringen (1..4, caret 4)",
              sel(f));
        f.setSelection(4, 1);   // bakvänd: careten på 1
        f.skriv("Y");
        f.keyDown(gfx::Key::Z, kRedigera);
        check(f.text() == "abcdef" && f.selBegin() == 1 && f.selEnd() == 4 &&
                  f.caret() == 1,
              "H2: …också en bakvänd markering (caret 1)", sel(f));
    }
    // GRUPPEN bryts av Vänster+Höger, av Enter och av fokusförlust.
    {
        ProvFält f;
        f.skriv("ab");
        f.keyDown(gfx::Key::Left, 0);
        f.keyDown(gfx::Key::Right, 0);
        f.skriv("cd");
        f.keyDown(gfx::Key::Z, kRedigera);
        check(f.text() == "ab",
              "H2: Vänster+Höger förseglar — \"cd\" är ett eget steg", f.text());
    }
    {
        ProvFält f;
        f.skriv("ab");
        const bool enter = f.keyDown(gfx::Key::Return, 0);
        f.skriv("cd");
        f.keyDown(gfx::Key::Z, kRedigera);
        check(!enter && f.text() == "ab",
              "H2: Enter (inte konsumerad) förseglar", f.text());
    }
    {
        ProvFält f;
        f.skriv("ab");
        f.focusLost();
        f.skriv("cd");
        f.keyDown(gfx::Key::Z, kRedigera);
        check(f.text() == "ab", "H2: fokusförlusten förseglar", f.text());
    }
    // setText TÖMMER — men samma text gör det inte.
    {
        ProvFält f;
        f.skriv("abc");
        f.setText("abc");
        check(f.canUndo(), "H2: setText med SAMMA text tömmer inte historiken");
        f.setText("xyz");
        const bool tog = f.keyDown(gfx::Key::Z, kRedigera);
        check(!f.canUndo() && tog && f.text() == "xyz",
              "H2: setText med en ny text tömmer — Ctrl+Z är no-op", f.text());
    }
    // SKRIVSKYDDAT: konsumerat, ingen ändring — och historiken gäller igen
    // när fältet låses upp.
    {
        ProvFält f;
        f.skriv("abc");
        f.setReadOnly(true);
        f.ändringar = 0;
        const bool z = f.keyDown(gfx::Key::Z, kRedigera);
        const bool y = f.keyDown(gfx::Key::Y, kRedigera);
        check(z && y && f.text() == "abc" && f.ändringar == 0 && !f.canUndo(),
              "H2: skrivskyddat — Ctrl+Z/Ctrl+Y konsumeras och ändrar inget",
              f.text());
        f.setReadOnly(false);
        f.keyDown(gfx::Key::Z, kRedigera);
        check(f.text().empty(),
              "H2: upplåst igen — historiken står kvar och gäller", f.text());
    }
}

// ============================================================ H3 TEXTAREA

struct ProvYta : glaze::TextArea {
    using glaze::TextArea::keyDown;
    using glaze::TextArea::textInput;
};

void fallTextArea()
{
    std::printf("\n=== H3. TextArea ===\n");

    {
        ProvYta t;
        t.textInput("ett");
        t.keyDown(gfx::Key::Return, 0);
        t.textInput("tv\xC3\xA5");   // två — ett ord efter radbrytningen
        check(t.text() == "ett\ntv\xC3\xA5", "H3: skrivande och Enter",
              t.text());
        t.keyDown(gfx::Key::Z, kRedigera);
        check(t.text() == "ett\n" && t.caret() == 4,
              "H3: Ctrl+Z tar ordet efter radbrytningen — caret 4", t.text());
        t.keyDown(gfx::Key::Z, kRedigera);
        check(t.text().empty() && t.caret() == 0 && !t.canUndo(),
              "H3: …och sedan \"ett\\n\" — radbrytningen hör till ordet före",
              t.text());
        t.keyDown(gfx::Key::Y, kRedigera);
        t.keyDown(gfx::Key::Z, kRedigera | kShift);
        check(t.text() == "ett\ntv\xC3\xA5" && t.caret() == t.text().size(),
              "H3: Ctrl+Y och Ctrl+Shift+Z gör om — caret sist",
              std::to_string(t.caret()));
    }
    // CARETEN ur steget där den INTE är där ändringen slutar.
    {
        ProvYta t;
        t.setText("abcd");
        t.setSelection(1, 1);
        t.keyDown(gfx::Key::Delete, 0);
        t.keyDown(gfx::Key::Delete, 0);
        check(t.text() == "ad", "H3: Delete två gånger", t.text());
        t.keyDown(gfx::Key::Z, kRedigera);
        check(t.text() == "abcd" && t.caret() == 1 && !t.canUndo(),
              "H3: Delete i följd ångras som ETT steg — careten tillbaka på 1",
              std::to_string(t.caret()));
        t.setSelection(3, 1);   // bakvänd
        t.textInput("X");
        t.keyDown(gfx::Key::Z, kRedigera);
        check(t.text() == "abcd" && t.selBegin() == 1 && t.selEnd() == 3 &&
                  t.caret() == 1,
              "H3: en bakvänd markering återställs (caret 1)",
              std::to_string(t.selBegin()) + ".." + std::to_string(t.selEnd()) +
                  " caret " + std::to_string(t.caret()));
    }
}

// ============================================================ H4 LÖSENORDET

// TESTFONTEN: "•" (U+2022) är 6 px brett, varje annan kodpunkt 10 — så en
// mätning av den RIKTIGA texten och en av MASKEN ger olika svar, och ett
// fält som mäter fel sträng syns i varje tal. Fonten talar fysiska px, och
// grinden kör skala 1.
class MaskFont final : public gfx::NullFont {
public:
    gfx::Sizef measure(const std::string& s) const override
    {
        return gfx::Sizef{bredd(s, s.size()), kHöjd};
    }
    float height() const override { return kHöjd; }
    bool  valid() const override { return true; }
    float prefixWidth(const std::string& s, std::size_t bytes) const override
    {
        return bredd(s, bytes);
    }
    std::size_t byteIndexAtX(const std::string& s, float x) const override
    {
        std::size_t i  = 0;
        float       x0 = 0.0f;
        while (i < s.size()) {
            const float a = steg(s, i);
            if (x < x0 + a / 2.0f)
                return i;
            x0 += a;
            i = nästa(s, i);
        }
        return s.size();
    }
    std::size_t fitWidth(const std::string& s, float maxW) const override
    {
        std::size_t i  = 0;
        float       x0 = 0.0f;
        while (i < s.size() && x0 + steg(s, i) <= maxW) {
            x0 += steg(s, i);
            i = nästa(s, i);
        }
        return i;
    }

    static constexpr float kMask  = 6.0f;
    static constexpr float kAnnat = 10.0f;
    static constexpr float kHöjd  = 16.0f;

private:
    static float steg(const std::string& s, std::size_t i)
    {
        return s.compare(i, 3, "\xE2\x80\xA2") == 0 ? kMask : kAnnat;
    }
    static std::size_t nästa(const std::string& s, std::size_t i)
    {
        ++i;
        while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
            ++i;
        return i;
    }
    static float bredd(const std::string& s, std::size_t bytes)
    {
        bytes   = bytes < s.size() ? bytes : s.size();
        float w = 0.0f;
        for (std::size_t i = 0; i < bytes; i = nästa(s, i))
            w += steg(s, i);
        return w;
    }
};

// Renderer som SPELAR IN texten — och dess färg bredvid (H7: platshållaren
// är `textDim`), och de fyllda ytorna (H7: caretens rekt) — resten är
// NullRendererns no-op.
struct Inspelning : gfx::NullRenderer {
    std::vector<std::string> texter;
    std::vector<gfx::Color>  färger;   // en per text, i samma ordning
    std::vector<gfx::Rectf>  ytor;     // `fillRoundedRect` — Draw::rect
    float drawText(gfx::Font&, float, float, const std::string& s,
                   gfx::Color c) override
    {
        texter.push_back(s);
        färger.push_back(c);
        return 0.0f;
    }
    void fillRoundedRect(const gfx::Rectf& r, float, gfx::Color) override
    {
        ytor.push_back(r);
    }
    void töm()
    {
        texter.clear();
        färger.clear();
        ytor.clear();
    }
};

// Ctx utan fönster, skala 1: fältet ligger i (0, 0) och caretens x är alltså
// stilens `fieldPadX` + prefixet (vänsterjusterat, ingen scroll).
glaze::Ctx maskCtx(gfx::Font& f, gfx::Renderer* r)
{
    glaze::Ctx c{};
    c.r           = r;
    c.font        = &f;
    c.headingFont = nullptr;
    c.scale       = 1.0f;
    c.ui          = glaze::UI::Slate;
    c.style       = &glaze::defaultStyle(glaze::UI::Slate);
    c.theme       = &glaze::Theme::slateDefault().light;
    c.area        = gfx::Rectf{0.0f, 0.0f, 400.0f, 100.0f};
    return c;
}

struct ProvLösen : glaze::TextInput {
    using glaze::TextInput::draw;
    using glaze::TextInput::keyDown;
    using glaze::TextInput::mouseDown;
    using glaze::TextInput::textInput;
    // Markeringen ritas bara med fokus, och headless finns inget fönster som
    // ger det — en subklass får sätta det skyddade tillståndet.
    void fokusera() { focused_ = true; }
};

void fallLösenord()
{
    std::printf("\n=== H4. lösenordet ===\n");

    MaskFont         f;
    Inspelning       inspelning;
    const glaze::Ctx c   = maskCtx(f, &inspelning);
    const float      pad = c.style->fieldPadX;

    // En BLANDAD text: 1, 2, 3 och 4 byte per kodpunkt — gränserna 0, 1, 3, 6
    // och 10.
    const std::string        blandad = "a\xC3\xA5\xE2\x82\xAC\xF0\x9F\x98\x80";
    const std::size_t        gräns[] = {0, 1, 3, 6, 10};
    ProvLösen                p;
    p.width = 200.0f;
    p.setText(blandad);
    p.setPassword(true);
    check(p.isPassword() && p.text() == blandad,
          "H4: lösenordsläget — text() är den RIKTIGA texten");
    const float x0 = p.resolve(c).x;
    for (int k = 0; k <= 4; ++k) {
        p.setSelection(int(gräns[k]), int(gräns[k]));
        const float väntat = x0 + pad + float(k) * MaskFont::kMask;
        check(nära(p.caretRect(c).x, väntat),
              "H4: caret vid byte " + std::to_string(gräns[k]) + " står efter " +
                  std::to_string(k) + " prickar",
              tal(p.caretRect(c).x) + " mot " + tal(väntat));
    }
    for (int k = 0; k <= 4; ++k) {
        p.mouseDown(c, x0 + pad + float(k) * MaskFont::kMask, 10.0f);
        check(p.caret() == gräns[k],
              "H4: klick på gränsen efter prick " + std::to_string(k) +
                  " ger byte " + std::to_string(gräns[k]),
              std::to_string(p.caret()));
    }
    p.mouseDown(c, x0 + pad + 100.0f, 10.0f);
    check(p.caret() == blandad.size(),
          "H4: klick bortom sista pricken ger textens slut",
          std::to_string(p.caret()));

    // "åäö": tre prickar, klicket MELLAN prick 1 och 2 ger byten efter å —
    // och mellan 2 och 3 byten efter ä, där en mätning av den RIKTIGA texten
    // hade gett en annan gräns.
    ProvLösen å;
    å.width = 200.0f;
    å.setText("\xC3\xA5\xC3\xA4\xC3\xB6");
    å.setPassword(true);
    å.setSelection(6, 6);
    check(nära(å.caretRect(c).x, x0 + pad + 3.0f * MaskFont::kMask),
          "H4: caretRect vid \"åäö\" — efter TRE prickar",
          tal(å.caretRect(c).x));
    å.mouseDown(c, x0 + pad + 1.0f * MaskFont::kMask, 10.0f);
    check(å.caret() == 2, "H4: klick mellan prick 1 och 2 ⇒ byten efter å (2)",
          std::to_string(å.caret()));
    å.mouseDown(c, x0 + pad + 2.0f * MaskFont::kMask, 10.0f);
    check(å.caret() == 4, "H4: klick mellan prick 2 och 3 ⇒ byten efter ä (4)",
          std::to_string(å.caret()));

    // En BAKVÄND markering och RITNINGENS strängar: ankaret på 6, careten på
    // 2 — markeringen är prickarna 2 och 3, och varje sträng som ritas är
    // masken (eller tom).
    å.fokusera();
    å.setSelection(6, 2);
    glaze::Ctx dc = c;
    dc.box        = å.resolve(c);
    inspelning.texter.clear();
    å.draw(dc);
    bool        baraMask = true;
    std::string ritat;
    for (const std::string& t : inspelning.texter) {
        ritat += "[" + t + "]";
        for (std::size_t i = 0; i < t.size(); i += 3)
            if (t.compare(i, 3, "\xE2\x80\xA2") != 0)
                baraMask = false;
    }
    const auto ritad = [&](const std::string& s) {
        for (const std::string& t : inspelning.texter)
            if (t == s)
                return true;
        return false;
    };
    check(!inspelning.texter.empty() && baraMask,
          "H4: ritningens strängar är MASKEN — ingen klartext", ritat);
    check(ritad("\xE2\x80\xA2") && ritad("\xE2\x80\xA2\xE2\x80\xA2"),
          "H4: den bakvända markeringen ritas som prick 2–3 (en före, två "
          "markerade)",
          ritat);
    check(å.caret() == 2 && å.selBegin() == 2 && å.selEnd() == 6 &&
              nära(å.caretRect(c).x, x0 + pad + MaskFont::kMask),
          "H4: …och careten står efter första pricken (byte 2)",
          sel(å));

    // INGEN HISTORIK i lösenordsläge — och `setPassword(true)` tömmer.
    {
        ProvLösen l;
        l.setPassword(true);
        l.textInput("a");
        l.textInput("b");
        l.textInput("c");
        const bool tog = l.keyDown(gfx::Key::Z, kRedigera);
        check(!l.canUndo() && tog && l.text() == "abc",
              "H4: i lösenordsläge registreras inget — Ctrl+Z konsumeras, "
              "inget ångras",
              l.text());
        ProvLösen m;
        m.textInput("a");
        m.textInput("b");
        check(m.canUndo(), "H4: (utan lösenord finns ett steg)");
        m.setPassword(true);
        check(!m.canUndo(), "H4: setPassword(true) tömmer historiken");
        m.setPassword(false);
        check(!m.canUndo() && m.text() == "ab",
              "H4: …och den kommer inte tillbaka när läget slås av", m.text());
    }
    // KOPIERA och KLIPP UT konsumeras och gör ingenting — klipp ut raderar
    // inte. (Att urklippet står orört mäter W2, med det riktiga urklippet.)
    {
        ProvLösen l;
        l.setPassword(true);
        l.setText("hemligt");
        l.keyDown(gfx::Key::A, kRedigera);
        const bool x = l.keyDown(gfx::Key::X, kRedigera);
        check(x && l.text() == "hemligt" && l.hasSelection(),
              "H4: Ctrl+X i lösenordsläge — konsumerad, texten står kvar",
              l.text());
    }
}

// ============================================================== H5 GENVÄGEN

void fallGenvag()
{
    std::printf("\n=== H5. genvägen (AltGr, Cmd på macOS) ===\n");

    using glaze::textfield::editShortcut;
#ifdef __APPLE__
    // macOS: Cmd UTAN Ctrl och Option — Option är Macens
    // AltGr, och Ctrl+bokstav är inte fältets.
    check(editShortcut(kCmd) && editShortcut(kCmd | kShift),
          "H5: Cmd och Cmd+Shift är fältets genväg (macOS)");
    check(!editShortcut(kCmd | kAlt) && !editShortcut(kCmd | kCtrl) &&
              !editShortcut(kCtrl) && !editShortcut(kCtrl | kShift) &&
              !editShortcut(0) && !editShortcut(kAlt),
          "H5: Cmd+Option, Cmd+Ctrl och Ctrl ensam är det INTE (macOS)");
    // Det fältet INTE ska ta: Ctrl (Emacs-tangenterna) och Cmd+Option.
    const std::uint32_t inte[] = {kCtrl, kCmd | kAlt};
#else
    check(editShortcut(kCtrl) && editShortcut(kCtrl | kShift),
          "H5: Ctrl och Ctrl+Shift är fältets genväg");
    check(!editShortcut(kCtrl | kAlt) && !editShortcut(kCtrl | kAlt | kShift) &&
              !editShortcut(0) && !editShortcut(kAlt) && !editShortcut(kCmd),
          "H5: Ctrl+Alt (AltGr på Windows) är det INTE, och inte Cmd (Super)");
    // Det fältet INTE ska ta: AltGr (Ctrl+Alt) och Super.
    const std::uint32_t inte[] = {kCtrl | kAlt, kCmd};
#endif

    for (const std::uint32_t m : inte) {
        ProvFält f;
        f.skriv("abc");
        const bool a = f.keyDown(gfx::Key::A, m);
        const bool z = f.keyDown(gfx::Key::Z, m);
        check(!a && !z && !f.hasSelection() && f.text() == "abc",
              "H5: TextInput — en annan modifierare markerar inte och ångrar "
              "inte, och tangenterna går vidare (mask " + tal(m) + ")",
              sel(f) + " \"" + f.text() + "\"");

        ProvYta t;
        t.textInput("abc");
        const bool ta = t.keyDown(gfx::Key::A, m);
        const bool tz = t.keyDown(gfx::Key::Z, m);
        check(!ta && !tz && !t.hasSelection() && t.text() == "abc",
              "H5: TextArea — samma regel (mask " + tal(m) + ")", t.text());
    }
    // …och plattformens genväg TAR dem (Cmd+A på macOS, Ctrl+A annars).
    ProvFält f;
    f.skriv("abc");
    check(f.keyDown(gfx::Key::A, kRedigera) && f.hasSelection(),
          "H5: plattformens genväg markerar allt", sel(f));
}

// ============================================================ H6 MAXLÄNGDEN

void fallMaxlangd()
{
    std::printf("\n=== H6. maxlängden ===\n");

    // HJÄLPAREN (`textfield::fitMaxLength`) mot handskrivet förväntat värde.
    using glaze::textfield::fitMaxLength;
    check(fitMaxLength("abc", 3, 3, "de", 0) == "de",
          "H6: fitMaxLength — max 0 är obegränsat");
    check(fitMaxLength("abc", 3, 3, "de", 4) == "d",
          "H6: fitMaxLength — insättningen kapas så att resten och den ryms");
    check(fitMaxLength("\xC3\xA5\xC3\xA4", 0, 2, "\xC3\xB6\xC3\xB6", 2) ==
              "\xC3\xB6",
          "H6: fitMaxLength — markeringen räknas som frigjord, i kodpunkter");
    check(fitMaxLength("abcd", 0, 1, "x", 3).empty() &&
              fitMaxLength("abcdef", 6, 6, "x", 3).empty(),
          "H6: fitMaxLength — en text redan ÖVER gränsen ger tomt (ingen "
          "runtslagning i std::size_t)");

    // "åäö" med gränsen 2 ⇒ "åä" — KODPUNKTER, inte byte.
    {
        ProvFält f;
        f.setMaxLength(2);
        f.skriv("\xC3\xA5\xC3\xA4\xC3\xB6");
        check(f.text() == "\xC3\xA5\xC3\xA4",
              "H6: \"åäö\" med gränsen 2 ⇒ \"åä\" (kodpunkter, inte byte)",
              f.text());
    }
    // En inklistringslik insättning kapas — och ÅNGRA tar det kapade steget;
    // sedan skrivande över en markering inom budget.
    {
        ProvFält f;
        f.setMaxLength(3);
        f.textInput("abcdef");
        check(f.text() == "abc", "H6: en insättning med sex tecken kapas till tre",
              f.text());
        f.keyDown(gfx::Key::Z, kRedigera);
        check(f.text().empty(), "H6: ångra efter en kapad insättning — tom",
              f.text());
        f.keyDown(gfx::Key::Y, kRedigera);
        check(f.text() == "abc",
              "H6: …och gör om ger det kapade, inte det inklistrade", f.text());
        f.setSelection(0, 3);
        f.textInput("xy");
        check(f.text() == "xy",
              "H6: skrivande över en markering inom budget — markeringen är "
              "frigjord",
              f.text());
    }
    // VID GRÄNSEN avvisas skrivandet; ingenting ändras.
    {
        ProvFält f;
        f.setMaxLength(3);
        f.textInput("abc");
        f.ändringar = 0;
        f.textInput("d");
        check(f.text() == "abc" && f.ändringar == 0,
              "H6: vid gränsen avvisas skrivandet — inget onChange", f.text());
    }
    // setText ÖVER gränsen står kvar, ryms inget raderas markeringen INTE, och
    // backsteg fungerar.
    {
        ProvFält f;
        f.setMaxLength(3);
        f.setText("abcd");
        check(f.text() == "abcd",
              "H6: setText med en längre text står kvar (appens data är ingen "
              "inmatning)",
              f.text());
        f.setSelection(0, 1);
        f.textInput("x");
        check(f.text() == "abcd" && f.selBegin() == 0 && f.selEnd() == 1,
              "H6: ryms ingenting står markeringen kvar (den raderas inte)",
              f.text() + " " + sel(f));
        f.keyDown(gfx::Key::Backspace, 0);
        check(f.text() == "bcd", "H6: backsteg fungerar över gränsen", f.text());
    }
    // LÖSENORDSLÄGET räknar den riktiga texten — en mask per kodpunkt.
    {
        ProvFält f;
        f.setPassword(true);
        f.setMaxLength(2);
        f.textInput("\xC3\xA5\xC3\xA4\xC3\xB6");
        check(f.text() == "\xC3\xA5\xC3\xA4",
              "H6: lösenordsläget räknar kodpunkter", f.text());
    }
    // −1 IGNORERAS, 0 är obegränsat, och setProp tar bara ett heltal ≥ 0.
    {
        ProvFält f;
        f.setMaxLength(2);
        f.setMaxLength(-1);
        check(f.maxLength() == 2, "H6: setMaxLength(-1) ignoreras",
              std::to_string(f.maxLength()));
        f.setMaxLength(0);
        f.textInput("abcdef");
        check(f.text() == "abcdef", "H6: 0 är obegränsat", f.text());
        check(f.setProp("maxLength", "8") && f.maxLength() == 8,
              "H6: setProp(\"maxLength\", \"8\") ⇒ 8");
        check(!f.setProp("maxLength", "-1") && !f.setProp("maxlength", "8x") &&
                  f.maxLength() == 8,
              "H6: setProp med -1 eller skräp ⇒ false (en opak rad) och orört",
              std::to_string(f.maxLength()));
    }
    // DET FLERRADIGA FÄLTET: radbrytningen är en kodpunkt, Enter vid gränsen
    // ger ingen rad, och en inklistring kapas med radbrytningarna kvar.
    {
        ProvYta t;
        t.setMaxLength(4);
        t.textInput("ab");
        t.keyDown(gfx::Key::Return, 0);
        t.textInput("cdef");
        check(t.text() == "ab\nc",
              "H6: TextArea — radbrytningen räknas som en kodpunkt", t.text());
        const bool tog = t.keyDown(gfx::Key::Return, 0);
        check(tog && t.text() == "ab\nc",
              "H6: TextArea — Enter vid gränsen ger ingen rad (men tangenten är "
              "fältets)",
              t.text());
        ProvYta u;
        u.setMaxLength(5);
        u.textInput("a\nb\nc\nd");
        check(u.text() == "a\nb\nc",
              "H6: TextArea — en inklistring kapas med radbrytningarna kvar",
              u.text());
        // Ordregeln (H1) delar `Typing`-inmatningen där ett ordtecken följer
        // ett blanktecken, och `\n` är ett blanktecken i det flerradiga
        // fältet: den kapade "a\nb\nc" är TRE steg ("a\n", "b\n", "c").
        // Ångra tar det SISTA kapade steget först — hade det okapade
        // registrerats ("…c\n", "d") hade ångra pekat förbi textens slut —
        // och tre ångra tömmer fältet.
        u.keyDown(gfx::Key::Z, kRedigera);
        check(u.text() == "a\nb\n", "H6: TextArea — ångra tar det kapade steget",
              u.text());
        u.keyDown(gfx::Key::Z, kRedigera);
        u.keyDown(gfx::Key::Z, kRedigera);
        check(u.text().empty() && !u.canUndo(),
              "H6: TextArea — tre ångra (ordregelns tre steg) tömmer fältet",
              u.text());
        check(u.setProp("maxLength", "2") && u.maxLength() == 2 &&
                  !u.setProp("maxLength", "-3"),
              "H6: TextArea — setProp som TextInputs");
    }
}

// ============================================================ H7 PLATSHÅLLAREN

struct ProvHint : glaze::TextInput {
    using glaze::TextInput::draw;
    using glaze::TextInput::keyDown;
    using glaze::TextInput::textInput;
    void fokusera() { focused_ = true; }   // headless: inget fönster ger fokus
};

struct ProvYtaHint : glaze::TextArea {
    using glaze::TextArea::draw;
};

void fallPlatshallare()
{
    std::printf("\n=== H7. platshållaren ===\n");

    MaskFont         f;
    Inspelning       ins;
    const glaze::Ctx c   = maskCtx(f, &ins);
    const gfx::Color dim = c.theme->colors.textDim;
    const gfx::Color txt = c.theme->colors.text;

    const auto rita = [&](auto& w) {
        glaze::Ctx dc = c;
        dc.box        = w.resolve(c);
        ins.töm();
        w.draw(dc);
    };
    // Ritades `s` — och i vilken färg?
    const auto ritad = [&](const std::string& s, const gfx::Color& färg) {
        for (std::size_t i = 0; i < ins.texter.size(); ++i)
            if (ins.texter[i] == s && ins.färger[i] == färg)
                return true;
        return false;
    };
    const auto någonInnehåller = [&](const std::string& s) {
        for (const std::string& t : ins.texter)
            if (t.find(s) != std::string::npos)
                return true;
        return false;
    };
    const auto alla = [&] {
        std::string a;
        for (const std::string& t : ins.texter)
            a += "[" + t + "]";
        return a;
    };

    ProvHint h;
    h.width = 300.0f;
    h.setPlaceholder("Namn");
    check(h.placeholder() == "Namn" && h.placeholderToken().id() == 0,
          "H7: placeholder()/placeholderToken() — en literal");
    rita(h);
    check(ritad("Namn", dim), "H7: ett tomt fält ritar platshållaren i textDim",
          alla());
    check(!ritad("Namn", txt),
          "H7: …och inte i textens färg");
    h.setText("x");
    rita(h);
    check(!någonInnehåller("Namn"), "H7: med text ritas ingen platshållare",
          alla());

    // MED FOKUS (VAL 3): hinten syns tills man skriver, och careten ritas
    // ovanpå. Ett tecken och ett backsteg ger caretens synliga fas med tom
    // text — headless finns ingen blinktimer.
    h.setText("");
    h.fokusera();
    h.textInput("a");
    h.keyDown(gfx::Key::Backspace, 0);
    check(h.text().empty() && h.caretVisible(),
          "H7: (förutsättning: tomt, fokuserat, careten synlig)");
    rita(h);
    const gfx::Rectf caret = h.caretRect(c);
    bool             caretRitad = false;
    for (const gfx::Rectf& r : ins.ytor)
        caretRitad = caretRitad || (std::abs(r.x - caret.x) < 0.01f &&
                                    std::abs(r.y - caret.y) < 0.01f &&
                                    std::abs(r.w - caret.w) < 0.01f &&
                                    std::abs(r.h - caret.h) < 0.01f);
    check(ritad("Namn", dim) && caretRitad,
          "H7: med fokus ritas platshållaren OCH caretens rekt (VAL 3)",
          alla());

    // LÖSENORDSLÄGET maskerar inte hinten — den är ingen hemlighet.
    h.setPassword(true);
    rita(h);
    check(ritad("Namn", dim) && !någonInnehåller("\xE2\x80\xA2"),
          "H7: i lösenordsläge ritas platshållaren i KLARTEXT", alla());
    h.setPassword(false);

    // FÖRSTA RADEN i det enradiga fältet.
    h.setPlaceholder("Rad ett\nRad tv\xC3\xA5");
    rita(h);
    check(ritad("Rad ett", dim) && !någonInnehåller("Rad tv"),
          "H7: TextInput ritar bara platshållarens första rad", alla());

    // ETT TOKEN följer språket.
    h.setPlaceholder(glaze::tr(glaze::str::Cancel));
    glaze::setLanguage("sv");
    std::string svText;
    {
        const glaze::TrText t = glaze::tr(glaze::str::Cancel);
        svText                = t.str();
    }
    rita(h);
    const bool svRitad = ritad(svText, dim);
    glaze::setLanguage("en");
    std::string enText;
    {
        const glaze::TrText t = glaze::tr(glaze::str::Cancel);
        enText                = t.str();
    }
    rita(h);
    const bool enRitad = ritad(enText, dim);
    glaze::setLanguage("sv");
    check(svText != enText && svRitad && enRitad &&
              h.placeholderToken().id() == glaze::str::Cancel,
          "H7: setPlaceholder(glaze::tr(N)) följer setLanguage",
          svText + " / " + enText);
    check(h.setTextToken("placeholder", glaze::tr(glaze::str::Ok)) &&
              h.placeholderToken().id() == glaze::str::Ok &&
              !h.setTextToken("text", glaze::tr(glaze::str::Ok)),
          "H7: setTextToken tar \"placeholder\" men inte \"text\" (användarens "
          "text tar inget token)");

    // DET FLERRADIGA FÄLTET — radbrutet och kortat.
    ProvYtaHint t;
    t.width  = 300.0f;
    t.height = 100.0f;
    t.setPlaceholder("Anteckning");
    rita(t);
    check(ritad("Anteckning", dim),
          "H7: ett tomt TextArea ritar platshållaren i textDim (radbrytning)",
          alla());
    t.setWrap(false);
    rita(t);
    check(ritad("Anteckning", dim),
          "H7: …och utan radbrytning (kortad på första raden)", alla());
    t.setText("a");
    rita(t);
    check(!någonInnehåller("Anteckning"),
          "H7: TextArea med text ritar ingen platshållare", alla());
    check(t.setTextToken("placeholder", glaze::tr(glaze::str::Ok)) &&
              t.placeholderToken().id() == glaze::str::Ok,
          "H7: TextArea — setTextToken(\"placeholder\")");
}

// ============================================================ MED FÖNSTER

struct Fonster : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    glaze::TextInput falt;
    glaze::TextInput losen;
    glaze::Button    knapp{"knapp"};
    int              menyÅngra = 0;

    Fonster() : glaze::AppWindow("textfield_gate", 420, 240)
    {
        falt.name   = "falt";
        falt.x      = 10;
        falt.y      = 10;
        falt.width  = 200;
        losen.name  = "losen";
        losen.x     = 10;
        losen.y     = 50;
        losen.width = 200;
        losen.setPassword(true);
        knapp.name = "knapp";
        knapp.x    = 10;
        knapp.y    = 90;
        root().adopt(falt);
        root().adopt(losen);
        root().adopt(knapp);
        // En Redigera-meny med appens EGEN Ctrl+Z — den som kolliderar (Cmd+Z
        // på macOS, fältets genväg där).
        menuBar.addMenu("Redigera");
        menuBar.addItem("Redigera", "\xC3\x85ngra", kÅngraNamn);
        menuBar.setHandler({"Redigera", "\xC3\x85ngra"}, [this] { ++menyÅngra; });
        menuBar.show();
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
};

void fallMedFonster()
{
    std::printf("\n=== W. genom fönstrets ingång ===\n");

    Fonster w;
    w.setOffscreen(true);

    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        if (!w.isOpen()) {
            std::printf("  --   fönsterfallen hoppas över: fönstret kom "
                        "aldrig upp\n");
            ++g_skips;
            w.close();
            return;
        }

        // W1. Fältets Ctrl+Z vinner över menyns.
        w.falt.focus();
        w.skriv("a");
        w.skriv("b");
        w.skriv("c");
        w.tangent(gfx::Key::Z, kRedigera);
        check(w.falt.text().empty() && w.menyÅngra == 0,
              "W1: fokuserat fält — Ctrl+Z ångrar FÄLTET, menyns handler körs "
              "inte",
              "\"" + w.falt.text() + "\", meny " + std::to_string(w.menyÅngra));
        w.tangent(gfx::Key::Z, kRedigera);
        check(w.menyÅngra == 0,
              "W1: …också med tom historik — fältet konsumerar tangenten",
              std::to_string(w.menyÅngra));
        w.knapp.focus();
        w.tangent(gfx::Key::Z, kRedigera);
        check(w.menyÅngra == 1,
              "W1: fokus på en knapp — menyns Ctrl+Z körs",
              std::to_string(w.menyÅngra));

        // W2. Lösenordsfältet släpper aldrig texten till urklippet.
        const std::string sparat = gfx::clipboardText();
        gfx::setClipboardText("textfield_gate: prov");
        if (gfx::clipboardText() != "textfield_gate: prov") {
            std::printf("  --   urklippsfallen hoppas över: urklippet tar inte "
                        "emot text här (Wayland utan input-serial)\n");
            ++g_skips;
        } else {
            w.losen.focus();
            w.skriv("hemligt");
            w.tangent(gfx::Key::A, kRedigera);
            w.tangent(gfx::Key::C, kRedigera);
            check(gfx::clipboardText() == "textfield_gate: prov",
                  "W2: Ctrl+C i lösenordsfältet lämnar urklippet orört",
                  gfx::clipboardText());
            w.tangent(gfx::Key::X, kRedigera);
            check(gfx::clipboardText() == "textfield_gate: prov" &&
                      w.losen.text() == "hemligt",
                  "W2: Ctrl+X lämnar urklippet OCH texten orörda",
                  w.losen.text());
            gfx::setClipboardText(sparat);
        }
        w.close();
    });
    w.run();

    if (!kordes) {
        std::printf("  --   fönsterfallen kördes ALDRIG (pumpen startade inte)\n");
        ++g_skips;
    }
}

// ====================================================== W3 TEXTSTORLEKEN
//
// TEXTSTORLEKEN — ett DOLT fönster med ett av varje i stilens storlek och ett med
// `setTextSize(glaze::px(28))`: fält, vallista, lista och flerradigt fält.
// Mätt som EGENSKAPER mellan widgetarna, aldrig mot en kopia av formeln:
// caretens höjd (= radhöjden, fysiska px) växer, och AUTO-höjden växer EXAKT
// lika mycket (± en fysisk px), för båda är radhöjden och luften är fältets.
struct StorlekFonster : glaze::AppWindow {
    glaze::TextInput litet, stort;
    glaze::Dropdown  valLitet, valStort;
    glaze::ListBox   listaLiten, listaStor;
    glaze::TextArea  ytaLiten, ytaStor;

    StorlekFonster() : glaze::AppWindow("textfield_gate W3", 700, 420)
    {
        glaze::Widget* const alla[] = {&litet,      &stort,     &valLitet,
                                       &valStort,   &listaLiten, &listaStor,
                                       &ytaLiten,   &ytaStor};
        const char* const namn[] = {"litet", "stort", "valLitet", "valStort",
                                    "listaLiten", "listaStor", "ytaLiten",
                                    "ytaStor"};
        for (int i = 0; i < 8; ++i) {
            alla[i]->name = namn[i];
            alla[i]->x    = 10.0f + float(i % 2) * 340.0f;
            alla[i]->y    = 10.0f + float(i / 2) * 100.0f;
            root().adopt(*alla[i]);
        }
        listaLiten.width  = listaStor.width  = 200;
        listaLiten.height = listaStor.height = 90;
        listaLiten.setItems({"a", "b", "c"});
        listaStor.setItems({"a", "b", "c"});
        valLitet.setItems({"a"});
        valStort.setItems({"a"});
        stort.setTextSize(glaze::px(28));
        valStort.setTextSize(glaze::px(28));
        listaStor.setTextSize(glaze::px(28));
        ytaStor.setTextSize(glaze::px(28));
    }
};

void fallTextstorlek()
{
    std::printf("\n=== W3. textstorleken ===\n");

    StorlekFonster w;
    w.setOffscreen(true);
    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        if (!w.isOpen()) {
            std::printf("  --   W3 hoppas över: fönstret kom aldrig upp\n");
            ++g_skips;
            w.close();
            return;
        }
        const float s  = w.window().uiScale();
        const float hL = w.litet.caretRect(w.litet.behaviorCtx()).h;
        const float hS = w.stort.caretRect(w.stort.behaviorCtx()).h;
        check(hS >= 1.5f * hL,
              "W3: px(28) ger en högre caret än stilens (radhöjden)",
              std::to_string(hS) + " mot " + std::to_string(hL));
        const float aL = w.litet.actualSize().h;
        const float aS = w.stort.actualSize().h;
        check(std::fabs((aS - aL) * s - (hS - hL)) <= 1.0f,
              "W3: fältets AUTO-höjd växer lika mycket som caretens",
              std::to_string((aS - aL) * s) + " mot " + std::to_string(hS - hL));
        check(std::fabs(w.valLitet.actualSize().h - aL) < 0.01f &&
                  std::fabs(w.valStort.actualSize().h - aS) < 0.01f,
              "W3: vallistans AUTO-höjd följer samma storlek som fältets",
              std::to_string(w.valStort.actualSize().h) + " mot " +
                  std::to_string(aS));
        const float d =
            w.ytaStor.heightForRows(w.ytaStor.behaviorCtx(), 1) -
            w.ytaLiten.heightForRows(w.ytaLiten.behaviorCtx(), 1);
        check(std::fabs(d * s - (hS - hL)) <= 1.0f,
              "W3: det flerradiga fältets rad växer lika mycket",
              std::to_string(d * s) + " mot " + std::to_string(hS - hL));
        const float rad = w.style().listRowH;
        check(w.listaLiten.rowAt(5.0f, 1.5f * rad) == 1 &&
                  w.listaStor.rowAt(5.0f, 1.5f * rad) == 0,
              "W3: listans rader blir högre i den större storleken",
              std::to_string(w.listaLiten.rowAt(5.0f, 1.5f * rad)) + " / " +
                  std::to_string(w.listaStor.rowAt(5.0f, 1.5f * rad)));
        // mm: större storlek, högre caret — monotont, oberoende av skärmen.
        w.stort.setTextSize(glaze::mm(4));
        const float h4 = w.stort.caretRect(w.stort.behaviorCtx()).h;
        w.stort.setTextSize(glaze::mm(8));
        const float h8 = w.stort.caretRect(w.stort.behaviorCtx()).h;
        check(h8 > h4 && w.stort.textSize().unit == glaze::Unit::Mm &&
                  w.stort.textSize().value == 8.0f,
              "W3: mm(8) ger en högre caret än mm(4)",
              std::to_string(h8) + " mot " + std::to_string(h4));
        // Världsenheter och negativt ignoreras; 0 är stilens igen.
        w.stort.setTextSize(glaze::world(2));
        w.stort.setTextSize(glaze::px(-3));
        check(w.stort.textSize().value == 8.0f,
              "W3: world(2) och px(−3) ignoreras");
        w.stort.setTextSize(0);
        check(std::fabs(w.stort.caretRect(w.stort.behaviorCtx()).h - hL) < 0.01f &&
                  std::fabs(w.stort.actualSize().h - aL) < 0.01f,
              "W3: 0 ger stilens storlek igen");
        w.close();
    });
    w.run();
    if (!kordes) {
        std::printf("  --   W3 kördes ALDRIG (pumpen startade inte)\n");
        ++g_skips;
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("textfield_gate — lösenord, ångra och AltGr\n");

    fallHistorik();
    fallTextInput();
    fallTextArea();
    fallLösenord();
    fallGenvag();
    fallMaxlangd();
    fallPlatshallare();
    fallMedFonster();
    fallTextstorlek();

    std::printf("\n%s — %d fall, %d fel, %d överhoppade block\n",
                g_fails == 0 ? "TEXTFIELD_GATE GRÖN" : "TEXTFIELD_GATE FÄLLD",
                g_cases, g_fails, g_skips);
    if (g_fails != 0)
        return 1;
    return g_skips == 0 ? 0 : 77;
}
