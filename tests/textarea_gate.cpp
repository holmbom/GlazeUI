// textarea_gate — DET FLERRADIGA FÄLTETS grind.
//
// Kör:  ./build/tests/textarea_gate [-v] [--png <katalog>]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — headless-delen är då ändå mätt
//
// Grinden är TVÅDELAD (som spinbox_gate):
//
//   HEADLESS      fältets DELADE kod (src/text_field.h, intern): filtren
//                 (en rad / flera rader — CRLF och ensamt CR blir LF, tabben
//                 ett blanksteg), ordet kring en position (dubbelklicket),
//                 teckengränserna · widgetens API: registret, att `setText`
//                 normaliserar radsluten och ALDRIG fyrar onChange, setProp
//                 (text med radbrytning, readOnly, wrap, inget title-alias) ·
//                 att en position ur en ANNAN text landar på teckengräns —
//                 för BÅDA fälten.
//   MED FÖNSTER   ett DOLT fönster (`setOffscreen`) och ALLT genom fönstrets
//                 riktiga ingång (`handleEvent`, protected —
//                 numpaden vikt, fokus och routing som i drift):
//                 F1  skrivning, Enter, radering över en radbrytning, UTF-8
//                     (åäö raderas helt) och EXAKT ett onChange per ändring;
//                     Ctrl+Enter är inte fältets
//                 F2  Upp/Ner med BEVARAT x-läge över en kortare rad, och
//                     stopp vid första/sista raden
//                 F3  BRYTNINGEN: en bredd mellan ett och två ord ger ett ord
//                     per rad — Upp/Ner går VISUELLA rader, Home/End gäller
//                     den visuella raden, Ctrl+Home/End hela texten; utan
//                     brytning är samma text en rad
//                 F4  HÅRDBRYTNINGENS sida: End på en hårdbruten rad står
//                     kvar på RADEN (och Home går till dess början)
//                 F5  markering över rader (Shift+Ner, Shift+pil) och
//                     URKLIPPET: kopiera ger LF, klipp ut, klistra in med
//                     CRLF/CR/tabb normaliserat; på Windows ligger CRLF i
//                     OS-urklippet (fasadens översättning)
//                 F6  SKRIVSKYDDAT: ingenting ändrar texten, navigering,
//                     Ctrl+A och Ctrl+C fungerar, inget onChange
//                 F7  RULLNINGEN: AUTO-höjden rymmer `textAreaRows` rader
//                     utan remsa, en rad till ger en; careten hålls synlig
//                     (Ctrl+End/Ctrl+Home); PageDown flyttar en sida och vyn
//                     följer; en bortrullad caret ger en TOM blink-damage
//                 F8  HJULET utan fokus rullar fältet och bubblar till den
//                     rullande föräldern när kanten är nådd
//                 F9  MUSEN: tryck på en rad, drag över rader, drag förbi
//                     nederkanten RULLAR (autoscroll), dubbelklick markerar
//                     ordet
//                 F10 den INKREMENTELLA layouten (en redigering lägger bara
//                     om sina stycken) ger SAMMA visuella rader som en hel
//                     omläggning — efter skrivning, Enter, en sammanfogning
//                     och en inklistring mitt i texten
//                 F11 UTF-8: Upp/Ner landar alltid på teckengräns
//                 F12 Tab flyttar fokus och lämnar texten orörd
//                 F13 ÅNGRA och GÖR OM efter F10:s redigeringar:
//                     Ctrl+Z, Ctrl+Y och Ctrl+Shift+Z genom ingången, ett
//                     onChange per steg, careten syns efter ångra (fältet
//                     rullar dit), och radstarterna efter båda är lika med
//                     en hel omläggning — F10:s jämförelse en gång till
//
// URKLIPPET ÄR DET RIKTIGA: grinden läser det som står där innan, och
// skriver tillbaka TEXTEN efteråt — en bild på urklippet går förlorad. På
// WAYLAND tar kompositören bara emot ett urklipp med en färsk input-serial,
// och ett dolt fönster i en grind har aldrig fått någon: där hoppas fallen
// som LÄSER urklippet över med en skriven rad (ett överhoppat block — exit 77
// när inget annat föll). Urklippets normalisering mäts ändå, genom textvägen
// (samma filter), och på Windows och X11 körs allt.
//
// SABOTAGEN den fäller (prövade ett i taget):
//   * x-läget glöms vid varje steg (`goalX_` nollas i `moveLines`) — F2, F11
//   * hårdbrytningens sida bortplockad (`lineAt` ignorerar `upstream`) — F4
//   * `relayout` som inte förskjuter raderna efter redigeringen — F10
//   * inklistringen utan radslutsnormalisering (`singleLine`-lik eller
//     rå text) — F5
//   * hjulet som kräver fokus i fältet — F8
//   * `scrollToCaret` borttagen ur tangentvägen — F7
//   * Enter som infogar i ett skrivskyddat fält — F6
//   * Windows-backendens CRLF-översättning borttagen — F5 (bara Windows)
//   * `restore` som skriver i `text_` förbi `replaceRaw`/`relayout` — F13
//   * `follow` bortplockad ur `edited` — F13 (careten syns inte)
//
// VAD DEN INTE MÄTER, så anspråket inte överdrivs:
//   - backendernas egna tangent- och mus-events — grinden matar ingången
//     direkt (numpad_gate mäter översättningen på Windows och Linux).
//   - hur det SER UT (pixel_gate och ögat via glaze-render), och att caret-
//     blinket ritar om just caretens yta (TextInputs mönster, oförändrat).
//   - `.glazeui`-formen och previewn — preview_gate (setter-fälten, kedjan
//     text → reader → modell → satsväg) och roundtrip_gate (fixturen med
//     radbrytningar, byte-exakt).
//   - tiden: att en anteckning på tusentals rader känns omedelbar mäts inte
//     med en klocka; F10 mäter att layouten är INKREMENTELL och rätt.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <gfx/null_renderer.h>
#include <gfx/platform.h>

#include <glaze/glaze.h>

#include "text_field.h"   // intern (tests/CMakeLists.txt pekar hit)

#ifdef GLAZE_BACKEND_D3D11
// Samma vakter som backendens d3d11_backend.h (som i numpad_gate).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

std::string tal(double v)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}

// Synliga styrtecken i en felutskrift.
std::string syns(const std::string& s)
{
    std::string ut = "\"";
    for (char c : s) {
        if (c == '\n')
            ut += "\\n";
        else if (c == '\r')
            ut += "\\r";
        else if (c == '\t')
            ut += "\\t";
        else
            ut += c;
    }
    return ut + "\"";
}

bool påGräns(const std::string& s, std::size_t i)
{
    return i <= s.size() &&
           (i == s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80);
}

// Det skyddade som en subklass får nå (som i listbox_gate).
struct Prov : glaze::TextArea {
    using glaze::Panel::scrollRange;
    using glaze::TextArea::contentExtent;
    using glaze::TextArea::keyDown;   // T4–T7
};

struct ProvRad : glaze::TextInput {
    using glaze::TextInput::keyDown;
};

// =============================================================== HEADLESS

void fallFiltren()
{
    using glaze::textfield::multiLine;
    using glaze::textfield::singleLine;
    const struct {
        const char* in;
        const char* ut;
        const char* vad;
    } flera[] = {
        {"a\r\nb", "a\nb", "CRLF blir LF"},
        {"a\rb", "a\nb", "ett ensamt CR blir LF"},
        {"a\r\r\nb", "a\n\nb", "CR + CRLF blir två radbrytningar"},
        {"a\n\nb", "a\n\nb", "LF står kvar, också två i rad"},
        {"a\tb", "a b", "tabben blir ett blanksteg"},
        {"a\x01" "b\x7F" "c", "abc", "övriga styrtecken försvinner"},
        {"\xC3\xA5\xC3\xA4\xC3\xB6\r\n\xF0\x9F\x99\x82",
         "\xC3\xA5\xC3\xA4\xC3\xB6\n\xF0\x9F\x99\x82", "UTF-8 orört"},
        {"", "", "tom text"},
    };
    for (const auto& f : flera) {
        const std::string fick = multiLine(f.in);
        check(fick == f.ut, std::string("multiLine: ") + f.vad, syns(fick));
    }
    check(singleLine("ab\ncd") == "ab" && singleLine("ab\r\ncd") == "ab" &&
              singleLine("a\tb") == "ab",
          "singleLine: klipps vid första radbrytningen, tabben försvinner "
          "(TextInputs filter oförändrat)");
    check(glaze::textfield::unifyNewlines("a\r\nb\rc\nd\t") == "a\nb\nc\nd\t",
          "unifyNewlines: bara radsluten — tabben står kvar");
}

void fallGränser()
{
    using namespace glaze::textfield;
    const std::string s = "a\xC3\xA5" "b";   // a å b — å är två byte
    check(nextBoundary(s, 1) == 3 && prevBoundary(s, 3) == 1,
          "nästa/föregående teckengräns hoppar över åets två byte");
    check(floorBoundary(s, 2) == 1 && floorBoundary(s, 3) == 3 &&
              floorBoundary(s, 99) == s.size(),
          "floorBoundary: mitt i å → före det, utanför → slutet");

    const struct {
        const char* s;
        std::size_t i, b, e;
        const char* vad;
    } ord[] = {
        {"hej alfa", 5, 4, 8, "inuti ett ord"},
        {"hej alfa", 4, 4, 8, "vid ordets början"},
        {"hej alfa", 3, 0, 3, "direkt efter ett ord — ORDET vinner över blanksteget"},
        {"hej  alfa", 4, 3, 5, "mellan två blanksteg — blankstegen"},
        {"\xC3\xA5\xC3\xA4\xC3\xB6 x", 2, 0, 6, "åäö är ett ord"},
        {"a\nb", 1, 0, 1, "radbrytningen är en gräns"},
        {"x...y", 2, 1, 4, "skiljetecken i följd"},
        {"\n\n", 1, 1, 1, "mellan två radbrytningar — inget"},
    };
    for (const auto& o : ord) {
        std::size_t b = 99;
        std::size_t e = 99;
        wordAt(o.s, o.i, b, e);
        check(b == o.b && e == o.e, std::string("wordAt: ") + o.vad,
              "[" + std::to_string(b) + ", " + std::to_string(e) + ")");
    }
}

void fallApi()
{
    std::unique_ptr<glaze::Widget> w = glaze::makeWidget("textarea");
    auto* ta = dynamic_cast<glaze::TextArea*>(w.get());
    check(ta != nullptr, "registret: \"textarea\" ger en riktig glaze::TextArea");
    check(ta && ta->focusable() && ta->wrap() && !ta->isReadOnly(),
          "fokuserbar, brytning PÅ och skrivbar som standard");

    glaze::TextArea t;
    int n     = 0;
    t.onChange = [&n] { ++n; };
    t.setText("a\r\nb\rc\n");
    check(t.text() == "a\nb\nc\n", "setText normaliserar radsluten",
          syns(t.text()));
    t.setText("x");
    check(n == 0, "programmatisk setText fyrar ALDRIG onChange",
          std::to_string(n));

    check(t.setProp("text", "rad 1\nrad 2") && t.text() == "rad 1\nrad 2",
          "setProp(text) tar en radbrytning", syns(t.text()));
    check(t.setProp("readOnly", "true") && t.isReadOnly(),
          "setProp(readOnly)");
    check(t.setProp("wrap", "false") && !t.wrap(), "setProp(wrap)");
    check(!t.setProp("title", "nej") && t.text() == "rad 1\nrad 2",
          "inget title-alias — en ny widget har inga äldre filer");
    check(t.setProp("border", "true"), "Panels nycklar når basen");
    check(n == 0, "…och fortfarande inget onChange", std::to_string(n));

    // En position ur en ANNAN text landar på teckengräns — båda fälten.
    {
        ProvRad r;
        r.setText("abcd");
        r.keyDown(gfx::Key::End, 0);
        r.keyDown(gfx::Key::Left, 0);   // caret 3
        r.setText("\xC3\xA5\xC3\xA4\xC3\xB6");   // åäö — 3 är mitt i ä
        check(r.caret() == 2 && !r.hasSelection(),
              "TextInput::setText: caret mitt i ett tecken flyttas till "
              "gränsen före",
              std::to_string(r.caret()));
    }
}

// ------------------------------------------- AUTO-höjden i flera skalor
//
// En FALSK font med fast teckensteg (som i measure_gate) och ett Ctx
// utan renderer: AUTO-höjden ska rymma EXAKT `textAreaRows` rader utan att
// fältet rullar — i varje UI och skala, också där `(x · s) / s` inte är x i
// flyttal. Ett spelrum på en tusendels pixel hade ritat en remsa med en tumme
// lika lång som rännan och låtit hjulet "rulla" ingenting.
class FalskFont final : public gfx::NullFont {
public:
    FalskFont(float adv, float h) : adv_(adv), h_(h) {}

    gfx::Sizef measure(const std::string& s) const override
    {
        return gfx::Sizef{adv_ * float(tecken(s, s.size())), h_};
    }
    float height() const override { return h_; }
    bool  valid() const override { return true; }
    float prefixWidth(const std::string& s, std::size_t bytes) const override
    {
        return adv_ * float(tecken(s, std::min(bytes, s.size())));
    }
    std::size_t byteIndexAtX(const std::string& s, float x) const override
    {
        std::size_t i = 0;
        float       x0 = 0.0f;
        while (i < s.size()) {
            const std::size_t j = glaze::textfield::nextBoundary(s, i);
            if (x < x0 + adv_ / 2.0f)
                return i;
            x0 += adv_;
            i = j;
        }
        return s.size();
    }
    std::size_t fitWidth(const std::string& s, float maxW) const override
    {
        std::size_t i  = 0;
        float       x0 = 0.0f;
        while (i < s.size() && x0 + adv_ <= maxW) {
            x0 += adv_;
            i = glaze::textfield::nextBoundary(s, i);
        }
        return i;
    }

private:
    static std::size_t tecken(const std::string& s, std::size_t n)
    {
        std::size_t k = 0;
        for (std::size_t i = 0; i < n; ++i)
            if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
                ++k;
        return k;
    }
    float adv_;
    float h_;
};

void fallSkalor()
{
    int prövade = 0;
    for (const glaze::UI ui :
         {glaze::UI::Glaze, glaze::UI::Slate, glaze::UI::Minimalistic}) {
        const glaze::Style& st = glaze::defaultStyle(ui);
        // Radhöjder i LOGISKA px: de kända (13 → 14.72, 10 → 12.16) och en
        // ojämn.
        for (const float lh : {14.72f, 12.16f, 17.3f}) {
            for (const float s : {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f}) {
                FalskFont  f(7.0f * s, lh * s);
                glaze::Ctx c{};
                c.font  = &f;
                c.scale = s;
                c.ui    = ui;
                c.style = &st;
                c.theme = &glaze::Theme::slateDefault().light;
                c.area  = gfx::Rectf{0.0f, 0.0f, 600.0f * s, 400.0f * s};
                Prov t;
                t.setWrap(false);
                std::string text = "1";
                for (int i = 2; i <= st.textAreaRows; ++i)
                    text += "\n" + std::to_string(i);
                t.setText(text);
                const float exakt = t.scrollRange(c).h;
                t.setText(text + "\nen till");
                const float enTill = t.scrollRange(c).h;
                ++prövade;
                if (exakt != 0.0f || enTill <= 0.0f)
                    check(false,
                          "AUTO-höjden rymmer textAreaRows rader exakt (skala " +
                              tal(s) + ", radhöjd " + tal(lh) + ")",
                          tal(exakt) + " / " + tal(enTill));
            }
        }
    }
    check(prövade == 54,
          "AUTO-höjden: textAreaRows rader ryms utan remsa och en till rullar "
          "— tre UI × tre radhöjder × sex skalor",
          std::to_string(prövade));
}

// ======================================== TABBSTOPPEN
//
// T1–T8, headless med den falska fonten (fast teckensteg 7): tabbstoppens
// vägar i `text_field` ur KONTRAKTET (en tabb flyttar x till nästa multipel
// av tabbredden), deras inverser, brytningen bit för bit, och fältets Tab med
// `acceptsTab`. SABOTAGEN: `Layout` utan `tabW` i nyckeln (T8 — bredden står
// still), Tab konsumerad med Shift (T5), filtret som behåller tabbar utan
// flaggan (T6). MÄTER INTE att stoppen SER jämna ut i en proportionell font,
// eller blankstegets bredd per backend.
void fallTabbar()
{
    using namespace glaze::textfield;
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, 14.0f);
    glaze::Ctx          c{};
    c.font  = &f;
    c.scale = 1.0f;
    c.ui    = glaze::UI::Slate;
    c.style = &st;
    c.theme = &glaze::Theme::slateDefault().light;
    c.area  = gfx::Rectf{0.0f, 0.0f, 600.0f, 400.0f};
    const float tab = 28.0f;   // fyra blanksteg à 7

    // ---- T1. Positionerna efter tabbar: k · tabW, och text som korsar ett
    //          stopp går till NÄSTA.
    check(tabbedPrefix(c, "a\tb", 2, tab) == tab &&
              tabbedPrefix(c, "a\tb", 3, tab) == tab + 7.0f,
          "T1: efter en tabb står x på tabbstoppet");
    check(tabbedPrefix(c, "ab\t\tc", 4, tab) == 2.0f * tab,
          "T1: två tabbar — två stopp");
    check(tabbedPrefix(c, "abcde\tx", 6, tab) == 2.0f * tab,
          "T1: text som korsat ett stopp — tabben till nästa",
          tal(tabbedPrefix(c, "abcde\tx", 6, tab)));
    check(tabbedPrefix(c, "abc", 2, tab) == glaze::Draw::prefixWidth(c, "abc", 2) &&
              tabbedWidth(c, "abc", tab) == glaze::Draw::measure(c, "abc").w,
          "T1: utan tabb — exakt Draw:s svar");

    // ---- T2. Inverserna: varje teckengräns tillbaka ur sitt x.
    const std::string s = "ab\tcd\te";
    bool inv = true;
    for (std::size_t i = 0; i <= s.size(); ++i)
        inv = inv && tabbedIndexAtX(c, s, tabbedPrefix(c, s, i, tab), tab) == i;
    check(inv, "T2: tabbedIndexAtX(tabbedPrefix(i)) == i för varje gräns");

    // ---- T3. Brytningen: blanksteget bryter, tabben gör det inte; ett
    //          tabbfritt stycke bryts exakt som Draw.
    const auto rader = tabbedWrapLines(c, "aa\tbb cc", 50.0f, tab);
    check(rader.size() == 2 && rader[0].begin == 0 && rader[0].end == 5 &&
              rader[1].begin == 6 && rader[1].end == 8,
          "T3: \"aa\\tbb cc\" vid 50 — brytningen vid blanksteget",
          std::to_string(rader.size()) + " rader");
    const auto draw = glaze::Draw::wrapLines(c, "hej du glada\nnu", 50.0f);
    const auto våra = tabbedWrapLines(c, "hej du glada\nnu", 50.0f, tab);
    bool lika = draw.size() == våra.size();
    for (std::size_t i = 0; lika && i < draw.size(); ++i)
        lika = draw[i].begin == våra[i].begin && draw[i].end == våra[i].end;
    check(lika, "T3: utan tabb — exakt Draw::wrapLines");

    // ---- T4–T5. Tab skriver en tabb med `acceptsTab`, bara omodifierad.
    Prov t;
    int  n = 0;
    t.onChange = [&n] { ++n; };
    t.setText("ab");
    t.setSelection(2, 2);
    check(!t.keyDown(gfx::Key::Tab, 0) && t.text() == "ab",
          "T5: utan acceptsTab — Tab är inte fältets");
    t.setAcceptsTab(true);
    check(t.keyDown(gfx::Key::Tab, 0) && t.text() == "ab\t" && n == 1,
          "T4: Tab skriver \\t — ett onChange", syns(t.text()));
    check(t.undo() && t.text() == "ab", "T4: ett ångra-steg tar bort tabben",
          syns(t.text()));
    check(!t.keyDown(gfx::Key::Tab, gfx::kModShift) &&
              !t.keyDown(gfx::Key::Tab, gfx::kModCtrl) && t.text() == "ab",
          "T5: Shift+Tab och Ctrl+Tab flyttar fokus som förut");
    t.setReadOnly(true);
    check(!t.keyDown(gfx::Key::Tab, 0) && t.text() == "ab",
          "T5: skrivskyddat — Tab släpps");
    t.setReadOnly(false);

    // ---- T6. Filtret: tabbar behålls BARA med flaggan.
    check(multiLine("a\tb", true) == "a\tb" && multiLine("a\tb") == "a b",
          "T6: multiLine behåller tabben med keepTabs, annars ett blanksteg");

    // ---- T7. `maxLength` räknar tabben som en kodpunkt.
    t.setMaxLength(3);
    t.setSelection(2, 2);
    t.keyDown(gfx::Key::Tab, 0);
    t.keyDown(gfx::Key::Tab, 0);
    check(t.text() == "ab\t", "T7: maxLength 3 — en tabb ryms, inte två",
          syns(t.text()));
    t.setMaxLength(0);

    // ---- T8. En annan `textAreaTabSpaces` lägger om layouten (nyckeln).
    Prov l;
    l.setWrap(false);
    l.setText("a\tb");
    const float bred4 = l.contentExtent(c).w;
    glaze::Style åtta = st;
    åtta.textAreaTabSpaces = 8;
    glaze::Ctx c8 = c;
    c8.style      = &åtta;
    const float bred8 = l.contentExtent(c8).w;
    check(std::abs((bred8 - bred4) - 4.0f * 7.0f) < 0.01f,
          "T8: åtta blanksteg i stället för fyra — raden 28 bredare",
          tal(bred4) + " → " + tal(bred8));
}

// ============================================================ MED FÖNSTER

struct Fonster : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    glaze::Button före{"före"};
    Prov          ta;
    glaze::Button efter{"efter"};
    Prov          ref;    // F10: samma text, satt med setText
    glaze::Panel  sida;   // F8: en rullande förälder
    Prov          inre;
    glaze::Button botten{"botten"};
    int           ändringar = 0;

    Fonster() : glaze::AppWindow("textarea_gate", 900, 640)
    {
        före.x  = 10;
        före.y  = 10;
        ta.x    = 10;
        ta.y    = 50;
        efter.x = 10;
        efter.y = 420;
        ref.x   = 460;
        ref.y   = 50;
        sida.x      = 460;
        sida.y      = 420;
        sida.width  = 300;
        sida.height = 160;
        sida.showScrollbars(true);
        inre.x       = 10;
        inre.y       = 10;
        inre.width   = 220;
        inre.setWrap(false);
        botten.x     = 10;
        botten.y     = 500;   // långt ner: sidan har något att rulla
        ta.onChange  = [this] { ++ändringar; };
        root().adopt(före);
        root().adopt(ta);
        root().adopt(efter);
        root().adopt(ref);
        root().adopt(sida);
        sida.adopt(inre);
        sida.adopt(botten);
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
    void tangenter(gfx::Key k, int n, std::uint32_t mods = 0)
    {
        for (int i = 0; i < n; ++i)
            tangent(k, mods);
    }
    void skriv(const char* utf8)
    {
        gfx::Event e;
        e.type     = gfx::EventType::TextInput;
        e.windowId = window().id();
        std::strncpy(e.text, utf8, sizeof(e.text) - 1);
        handleEvent(e);
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
    void klick(gfx::Pointf p)
    {
        mus(gfx::EventType::MouseButtonDown, p);
        mus(gfx::EventType::MouseButtonUp, p);
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
};

// Geometrin grinden RÄKNAR SJÄLV ur stilen — fältets skrivna kontrakt
// (text_area.h): innehållsytan är ramen indragen `panelInset`, texten står
// `fieldPadX`/`fieldPadY` in, och brytbredden är innehållsytan minus
// `fieldPadX` och max(`fieldPadX`, `scrollThick`) — remsans plats lämnas
// alltid. Inte produktionens funktioner: en grind som frågade dem hade mätt
// dem mot sig själva.
struct Mått {
    float lh, padX, padY, inset, padR, scale;
};

Mått mått(Prov& t)
{
    const glaze::Ctx c = t.behaviorCtx();
    const auto&      m = *c.style;
    return Mått{glaze::Draw::lineHeight(c),
                m.fieldPadX,
                m.fieldPadY,
                t.isBorder() ? m.panelInset : 0.0f,
                std::max(m.fieldPadX, m.scrollThick),
                c.scale};
}

// Sätt fältets bredd så att BRYTBREDDEN blir `brytbredd` (logiska px) och
// höjden till `rader` textrader.
void storlek(Prov& t, float brytbredd, int rader)
{
    const Mått k = mått(t);
    t.width      = brytbredd + k.padX + k.padR + 2.0f * k.inset;
    t.height     = float(rader) * k.lh + 2.0f * (k.padY + k.inset);
    t.invalidate();
}

float textBredd(Prov& t, const char* s)
{
    return glaze::Draw::measure(t.behaviorCtx(), s).w;
}

// Mitt på rad `rad` (oscrollad), `x` logiska px in från textens vänsterkant
// — FYSISKA px, musens rymd.
gfx::Pointf punkt(Prov& t, int rad, float x)
{
    const glaze::Ctx c = t.behaviorCtx();
    const gfx::Rectf r = t.resolve(c);
    const Mått       k = mått(t);
    return gfx::Pointf{
        r.x + (k.inset + k.padX + x) * k.scale - t.scrollX() * k.scale,
        r.y + (k.inset + k.padY + (float(rad) + 0.5f) * k.lh) * k.scale -
            t.scrollY() * k.scale};
}

float caretY(Prov& t)
{
    return t.caretRect(t.behaviorCtx()).y;
}

#ifdef GLAZE_BACKEND_D3D11
// OS-urklippet RÅTT — förbi fasaden, för att se radsluten den skrev.
std::wstring råttUrklipp()
{
    // Samma omförsök som backenden: urklippshistoriken öppnar urklippet en
    // kort stund efter varje ändring.
    std::wstring ut;
    bool         öppet = false;
    for (int försök = 0; försök < 10 && !öppet; ++försök) {
        öppet = OpenClipboard(nullptr) != 0;
        if (!öppet)
            Sleep(5);
    }
    if (!öppet)
        return ut;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* w = static_cast<const wchar_t*>(GlobalLock(h))) {
            ut = w;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return ut;
}
#endif

void fallMedFonster()
{
    glaze::setLanguage("sv");
    Fonster w;
    w.setOffscreen(true);

    bool kedjanKordes = false;
    glaze::app().post([&] {
        kedjanKordes = true;
        if (!w.isOpen()) {
            std::printf("  --   fönsterfallen hoppas över: fönstret kom "
                        "aldrig upp\n");
            ++g_skips;
            w.close();
            return;
        }
        Prov& ta = w.ta;
        using gfx::Key;
        const std::uint32_t kCtrl  = gfx::kModCtrl;
        const std::uint32_t kShift = gfx::kModShift;
        // Fältets redigeringsgenvägar (A/C/X/V/Z/Y) är plattformens — Cmd på
        // macOS, Ctrl annars; navigeringen (Ctrl+Home/End) är Ctrl.
#ifdef __APPLE__
        const std::uint32_t kRedigera = gfx::kModCmd;
#else
        const std::uint32_t kRedigera = gfx::kModCtrl;
#endif
        const float         w4     = textBredd(ta, "abcd");

        // F1. SKRIVNING, ENTER, RADERING — och onChange per ändring.
        ta.setWrap(false);
        storlek(ta, 12.0f * w4, 8);
        ta.setText("");
        ta.focus();
        w.ändringar = 0;
        w.skriv("a");
        w.skriv("\xC3\xA5");          // å
        w.tangent(Key::Return);
        w.skriv("b");
        check(ta.text() == "a\xC3\xA5\nb" && w.ändringar == 4 &&
                  ta.caret() == ta.text().size(),
              "F1: skriv a, å, Enter, b — en ny rad och fyra onChange",
              syns(ta.text()) + ", " + std::to_string(w.ändringar));
        w.tangent(Key::Return, kCtrl);
        w.tangent(Key::Return, gfx::kModAlt);
        check(ta.text() == "a\xC3\xA5\nb" && w.ändringar == 4,
              "F1: Ctrl+Enter och Alt+Enter är inte fältets", syns(ta.text()));
        w.tangent(Key::Backspace);
        w.tangent(Key::Backspace);   // radbrytningen — raderna går ihop
        check(ta.text() == "a\xC3\xA5" && w.ändringar == 6,
              "F1: Backspace raderar över radbrytningen", syns(ta.text()));
        w.tangent(Key::Backspace);
        check(ta.text() == "a" && w.ändringar == 7,
              "F1: Backspace tar hela å (två byte)", syns(ta.text()));

        // F2. UPP/NER MED BEVARAT X-LÄGE (utan brytning).
        ta.setText("abcd\nab\nabcd");
        w.tangent(Key::Home, kCtrl);
        w.tangenter(Key::Right, 3);
        check(ta.caret() == 3, "F2: caret efter \"abc\"",
              std::to_string(ta.caret()));
        w.tangent(Key::Down);
        check(ta.caret() == 7, "F2: Ner till den KORTA raden — dess slut",
              std::to_string(ta.caret()));
        w.tangent(Key::Down);
        check(ta.caret() == 11,
              "F2: Ner igen — x-läget efter \"abc\" är BEVARAT",
              std::to_string(ta.caret()));
        w.tangent(Key::Down);
        check(ta.caret() == 11, "F2: Ner på sista raden — står kvar",
              std::to_string(ta.caret()));
        w.tangent(Key::Up);
        w.tangent(Key::Up);
        check(ta.caret() == 3, "F2: Upp två gånger — tillbaka efter \"abc\"",
              std::to_string(ta.caret()));
        w.tangent(Key::Up);
        check(ta.caret() == 3, "F2: Upp på första raden — står kvar",
              std::to_string(ta.caret()));

        // F3. BRYTNINGEN: ett ord per rad.
        ta.setWrap(true);
        storlek(ta, 1.5f * w4, 8);
        ta.setText("abcd abcd abcd");
        w.tangent(Key::Home, kCtrl);
        w.tangenter(Key::Right, 2);
        w.tangent(Key::Down);
        check(ta.caret() == 7, "F3: Ner går en VISUELL rad (\"ab|cd\" → 7)",
              std::to_string(ta.caret()));
        w.tangent(Key::Down);
        check(ta.caret() == 12, "F3: Ner igen → 12", std::to_string(ta.caret()));
        w.tangent(Key::End);
        check(ta.caret() == 14, "F3: End → den visuella radens slut",
              std::to_string(ta.caret()));
        w.tangent(Key::Home);
        check(ta.caret() == 10, "F3: Home → den visuella radens början",
              std::to_string(ta.caret()));
        w.tangent(Key::Up);
        w.tangent(Key::End);
        check(ta.caret() == 9, "F3: Upp + End → mittradens slut (9)",
              std::to_string(ta.caret()));
        w.tangent(Key::End, kCtrl);
        const std::size_t slut = ta.caret();
        w.tangent(Key::Home, kCtrl);
        check(slut == 14 && ta.caret() == 0,
              "F3: Ctrl+End/Ctrl+Home → hela textens slut och början",
              std::to_string(slut) + " / " + std::to_string(ta.caret()));
        ta.setWrap(false);
        w.tangent(Key::Down);
        check(ta.caret() == 0, "F3: utan brytning är texten EN rad",
              std::to_string(ta.caret()));

        // F4. HÅRDBRYTNINGENS SIDA — ett ord längre än raden.
        ta.setWrap(true);
        storlek(ta, 10.5f * textBredd(ta, "a"), 8);
        ta.setText(std::string(24, 'a'));   // rader [0,10) [10,20) [20,24)
        w.tangent(Key::Home, kCtrl);
        const float y0 = caretY(ta);
        w.tangent(Key::End);
        check(ta.caret() == 10 && caretY(ta) == y0,
              "F4: End på en hårdbruten rad står kvar på RADEN",
              std::to_string(ta.caret()) + ", y " + tal(caretY(ta)) + " mot " +
                  tal(y0));
        w.tangent(Key::Home);
        check(ta.caret() == 0, "F4: …och Home går till DESS början",
              std::to_string(ta.caret()));
        w.tangent(Key::Down);
        const float y1 = caretY(ta);
        check(ta.caret() == 10 && y1 > y0,
              "F4: Ner → nästa rads början, på nästa rad",
              std::to_string(ta.caret()));
        w.tangent(Key::End);
        check(ta.caret() == 20 && caretY(ta) == y1,
              "F4: End på mittraden står kvar på den",
              std::to_string(ta.caret()));

        // F5. MARKERING ÖVER RADER + URKLIPPET.
        //
        // URKLIPPET MÅSTE GÅ ATT SÄTTA HÄR. Wayland-kompositören tar bara
        // emot ett urklipp med en färsk input-serial (ett riktigt tryck
        // eller en tangent), och ett dolt fönster i en grind har aldrig fått
        // någon — där når urklippet varken det här fältet eller TextInput.
        // Fallen som LÄSER urklippet hoppas då över med en skriven rad (ett
        // överhoppat block, exit 77). Normaliseringen mäts ändå, genom
        // textvägen, och Ctrl+X tar bort texten oavsett.
        const std::string sparat = gfx::clipboardText();
        gfx::setClipboardText("textarea_gate: prov");
        const bool urklipp = gfx::clipboardText() == "textarea_gate: prov";
        if (!urklipp) {
            std::printf("  --   urklippsfallen hoppas över: urklippet tar "
                        "inte emot text här (Wayland utan input-serial)\n");
            ++g_skips;
        }
        ta.setWrap(false);
        storlek(ta, 12.0f * w4, 8);
        ta.setText("ett\ntv\xC3\xA5\ntre");
        w.ändringar = 0;
        w.tangent(Key::Home, kCtrl);
        w.tangent(Key::Down, kShift);
        w.tangenter(Key::Right, 3, kShift);
        check(ta.selBegin() == 0 && ta.selEnd() == 8,
              "F5: Shift+Ner och Shift+Höger markerar över radbrytningen",
              std::to_string(ta.selBegin()) + "–" + std::to_string(ta.selEnd()));
        w.tangent(Key::C, kRedigera);
        if (urklipp) {
            check(gfx::clipboardText() == "ett\ntv\xC3\xA5",
                  "F5: Ctrl+C — urklippet bär raderna med LF",
                  syns(gfx::clipboardText()));
#ifdef GLAZE_BACKEND_D3D11
            check(råttUrklipp() == L"ett\r\ntvå",
                  "F5: Windows OS-urklipp har CRLF (fasaden översätter)");
#endif
        }
        w.tangent(Key::X, kRedigera);
        check(ta.text() == "\ntre" && w.ändringar == 1,
              "F5: Ctrl+X klipper ut markeringen (ett onChange)",
              syns(ta.text()));
        if (urklipp) {
            gfx::setClipboardText("x\r\ny\rz\tw");
            w.tangent(Key::V, kRedigera);
            check(ta.text() == "x\ny\nz w\ntre" && w.ändringar == 2,
                  "F5: Ctrl+V — CRLF och CR blir LF, tabben ett blanksteg",
                  syns(ta.text()));
            gfx::setClipboardText(sparat);
        }
        ta.setText("tre");
        w.tangent(Key::Home, kCtrl);
        w.ändringar = 0;
        w.skriv("x\r\ny\rz\tw\n");   // samma filter som inklistringen
        check(ta.text() == "x\ny\nz w\ntre" && w.ändringar == 1,
              "F5: inmatad text normaliseras som en inklistring — CRLF och CR "
              "blir LF, tabben ett blanksteg, ett onChange",
              syns(ta.text()));

        // F6. SKRIVSKYDDAT.
        ta.setText("abc\ndef");
        ta.setReadOnly(true);
        w.ändringar = 0;
        w.tangent(Key::Home, kCtrl);
        w.skriv("X");
        w.tangent(Key::Return);
        w.tangent(Key::Delete);
        w.tangent(Key::Backspace);
        w.tangent(Key::V, kRedigera);
        w.tangent(Key::X, kRedigera);
        check(ta.text() == "abc\ndef" && w.ändringar == 0,
              "F6: skrivskyddat — text, Enter, radering och klipp/klistra "
              "ändrar ingenting",
              syns(ta.text()));
        w.tangent(Key::Down);
        check(ta.caret() == 4, "F6: navigeringen fungerar (Ner)",
              std::to_string(ta.caret()));
        w.tangent(Key::A, kRedigera);
        check(ta.selBegin() == 0 && ta.selEnd() == 7 && !ta.caretVisible(),
              "F6: Ctrl+A markerar allt, och ingen caret ritas");
        if (urklipp) {
            w.tangent(Key::C, kRedigera);
            check(gfx::clipboardText() == "abc\ndef",
                  "F6: Ctrl+C kopierar ur det skrivskyddade fältet",
                  syns(gfx::clipboardText()));
            gfx::setClipboardText(sparat);
        }
        ta.setReadOnly(false);

        // F7. RULLNINGEN.
        ta.setWrap(false);
        ta.width  = 12.0f * w4;
        ta.height = 0;   // AUTO: `textAreaRows` rader
        ta.invalidate();
        const int rader = ta.behaviorCtx().style->textAreaRows;
        auto      text  = [](int n) {
            std::string s;
            for (int i = 1; i <= n; ++i)
                s += (i > 1 ? "\nRad " : "Rad ") + std::to_string(i);
            return s;
        };
        ta.setText(text(rader));
        check(ta.scrollRange(ta.behaviorCtx()).h == 0.0f,
              "F7: AUTO-höjden rymmer textAreaRows rader utan att rulla",
              tal(ta.scrollRange(ta.behaviorCtx()).h));
        ta.setText(text(rader + 1));
        check(ta.scrollRange(ta.behaviorCtx()).h > 0.0f,
              "F7: en rad till ⇒ fältet rullar (remsan finns)");
        ta.setText(text(20));
        const Mått        k     = mått(ta);
        const gfx::Sizef  range = ta.scrollRange(ta.behaviorCtx());
        w.tangent(Key::Home, kCtrl);
        w.tangent(Key::End, kCtrl);
        check(std::fabs(ta.scrollY() - range.h) < 0.01f,
              "F7: Ctrl+End — careten i synfältet, vyn längst ner",
              tal(ta.scrollY()) + " av " + tal(range.h));
        w.tangent(Key::Home, kCtrl);
        check(ta.scrollY() == 0.0f, "F7: Ctrl+Home — vyn längst upp",
              tal(ta.scrollY()));
        w.tangent(Key::PageDown);
        const std::size_t rad5 = ta.text().find("Rad " + std::to_string(rader + 1));
        check(ta.caret() == rad5 &&
                  std::fabs(ta.scrollY() - float(rader) * k.lh) < 0.01f,
              "F7: PageDown — en sida (textAreaRows rader) ner, vyn följer",
              std::to_string(ta.caret()) + " / " + tal(ta.scrollY()));
        w.tangent(Key::Home, kCtrl);
        ta.setScroll(0.0f, range.h);
        check(ta.caretRect(ta.behaviorCtx()).w == 0.0f,
              "F7: bortrullad caret ⇒ TOM blink-damage");
        ta.setScroll(0.0f, 0.0f);
        check(ta.caretRect(ta.behaviorCtx()).w > 0.0f,
              "F7: …och synlig igen när den rullats fram");

        // F8. HJULET UTAN FOKUS — och bubblingen vid kanten.
        w.inre.setText(text(20));
        w.före.focus();
        w.sida.setScroll(0.0f, 0.0f);
        w.inre.setScroll(0.0f, 0.0f);
        const gfx::Rectf ir = w.inre.resolve(w.inre.behaviorCtx());
        const gfx::Pointf mitt{ir.x + ir.w / 2.0f, ir.y + ir.h / 2.0f};
        w.hjul(mitt, -1.0f);
        check(w.inre.scrollY() > 0.0f && w.sida.scrollY() == 0.0f,
              "F8: hjul över fältet UTAN fokus — fältet rullar, sidan står",
              tal(w.inre.scrollY()) + " / " + tal(w.sida.scrollY()));
        w.inre.setScroll(0.0f, w.inre.scrollRange(w.inre.behaviorCtx()).h);
        w.hjul(mitt, -1.0f);
        check(w.sida.scrollY() > 0.0f,
              "F8: i fältets nederkant BUBBLAR hjulet — sidan rullar",
              tal(w.sida.scrollY()));
        w.sida.setScroll(0.0f, 0.0f);

        // F9. MUSEN.
        ta.setText("ett\ntv\xC3\xA5\ntre\nfyra");
        storlek(ta, 12.0f * w4, 8);
        ta.setScroll(0.0f, 0.0f);
        ta.focus();
        w.tangent(Key::End, kCtrl);
        w.klick(punkt(ta, 2, 1.0f));
        check(ta.caret() == 9 && !ta.hasSelection(),
              "F9: tryck vid tredje radens början → caret där (9)",
              std::to_string(ta.caret()));
        w.mus(gfx::EventType::MouseButtonDown, punkt(ta, 0, 1.0f));
        w.mus(gfx::EventType::MouseMotion, punkt(ta, 3, 40.0f * w4));
        w.mus(gfx::EventType::MouseButtonUp, punkt(ta, 3, 40.0f * w4));
        check(ta.selBegin() == 0 && ta.selEnd() == ta.text().size(),
              "F9: drag från första radens början till bortom sista radens "
              "slut markerar allt",
              std::to_string(ta.selBegin()) + "–" + std::to_string(ta.selEnd()));

        ta.setText(text(20));
        ta.height = 0;   // AUTO — rullar
        ta.invalidate();
        ta.setScroll(0.0f, 0.0f);
        w.mus(gfx::EventType::MouseButtonDown, punkt(ta, 0, 1.0f));
        const gfx::Rectf tr = ta.resolve(ta.behaviorCtx());
        const gfx::Pointf under{tr.x + tr.w / 2.0f, tr.y + tr.h + 3.0f * k.lh * k.scale};
        w.mus(gfx::EventType::MouseMotion, under);
        const float rull1 = ta.scrollY();
        w.mus(gfx::EventType::MouseMotion, gfx::Pointf{under.x + 1.0f, under.y + 1.0f});
        const float rull2 = ta.scrollY();
        w.mus(gfx::EventType::MouseButtonUp, under);
        check(rull1 > 0.0f && rull2 > rull1 && ta.selBegin() == 0 &&
                  ta.selEnd() > 0,
              "F9: drag FÖRBI nederkanten rullar fältet, en gång till per "
              "rörelse, och markeringen följer",
              tal(rull1) + " → " + tal(rull2));

        ta.setText("hej alfa beta");
        ta.setScroll(0.0f, 0.0f);
        const gfx::Pointf ialfa =
            punkt(ta, 0, textBredd(ta, "hej al"));
        w.klick(ialfa);
        w.klick(ialfa);   // inom dubbelklickstiden — ramverket räknar
        check(ta.selBegin() == 4 && ta.selEnd() == 8,
              "F9: dubbelklick markerar ordet (\"alfa\" [4, 8))",
              std::to_string(ta.selBegin()) + "–" + std::to_string(ta.selEnd()));

        // F10. DEN INKREMENTELLA LAYOUTEN = EN HEL OMLÄGGNING.
        const float bryt = 3.6f * textBredd(ta, "abcd ");
        ta.setWrap(true);
        w.ref.setWrap(true);
        storlek(ta, bryt, 12);
        storlek(w.ref, bryt, 12);
        ta.setText("");
        ta.focus();
        for (const char* ch : {"a", "b", "c", "d", " ", "e", "f", "g", " ",
                               "h", "i", "j", "k", " ", "l", "m", " ", "n",
                               "o", "p", "q", " ", "r", "s", "t"})
            w.skriv(ch);
        w.tangent(Key::Return);
        for (const char* ch : {"u", "v", " ", "x", "y", "z", " ", "\xC3\xA5",
                               "\xC3\xA4", "\xC3\xB6", " ", "a", "b", "c",
                               " ", "d", "e", "f", "g", "h"})
            w.skriv(ch);
        w.tangent(Key::Return);
        w.skriv("i");
        // Sammanfoga stycke ett och två: caret direkt efter första `\n`.
        w.tangent(Key::Home, kCtrl);
        const std::size_t nl = ta.text().find('\n');
        while (ta.caret() < nl + 1)
            w.tangent(Key::Right);
        w.tangent(Key::Backspace);
        w.tangenter(Key::Right, 4);
        // FLERA STYCKEN PÅ EN GÅNG mitt i texten — inklistringens form, men
        // genom textvägen (samma filter, samma `replace`), så fallet inte
        // beror på att urklippet går att sätta. Inget ORDTECKEN står direkt
        // efter ett blanktecken: textvägen är `Typing`, och ordregeln delar
        // en sådan inmatning där ett ordtecken följer ett blanktecken (`\n`
        // inräknat — se textfield_gate H1). "1 2 3\n6"
        // hade blivit åtta steg, och F13 ska ångra hela den flerstyckiga
        // insättningen i EN `restore`.
        w.skriv("1 -2 -3\n-4 -5\n-6");
        w.ref.setText(ta.text());

        auto vandring = [&](Prov& t) {
            std::vector<std::size_t> ut;
            t.focus();
            w.tangent(Key::Home, kCtrl);
            for (int steg = 0; steg < 200; ++steg) {
                ut.push_back(t.caret());
                const std::size_t före = t.caret();
                w.tangent(Key::Down);
                if (t.caret() == före)
                    break;
            }
            return ut;
        };
        const std::vector<std::size_t> inkr = vandring(ta);
        const std::vector<std::size_t> hel  = vandring(w.ref);
        std::string rad = "inkrementellt ";
        for (std::size_t i : inkr)
            rad += std::to_string(i) + " ";
        rad += "· helt ";
        for (std::size_t i : hel)
            rad += std::to_string(i) + " ";
        // Fler visuella rader än stycken: brytningen har slagit till, så
        // jämförelsen mäter brutna rader och inte bara styckena.
        const std::size_t stycken =
            1 + std::size_t(std::count(ta.text().begin(), ta.text().end(), '\n'));
        check(inkr.size() > stycken && inkr == hel,
              "F10: radstarterna efter redigeringen = en hel omläggnings",
              rad + "· " + syns(ta.text()));
        check(ta.contentExtent(ta.behaviorCtx()).h ==
                  w.ref.contentExtent(w.ref.behaviorCtx()).h,
              "F10: …och lika många rader");

        // F13. ÅNGRA OCH GÖR OM genom fönstrets ingång. F10:s
        // SISTA steg är den flerstyckiga insättningen (den kom efter en
        // caretflytt, alltså ett eget steg). Ångra går genom `replaceRaw`,
        // så den INKREMENTELLA layouten ska ge samma rader som en hel
        // omläggning också efteråt — och fältet ska rulla till careten: det
        // görs två rader högt och rullas längst ned först, så careten vid
        // insättningen ligger utanför synfältet innan Ctrl+Z.
        {
            const std::string efterF10 = ta.text();
            storlek(ta, bryt, 2);
            ta.focus();
            ta.setScroll(0.0f, ta.scrollRange(ta.behaviorCtx()).h);
            w.ändringar = 0;
            w.tangent(Key::Z, kRedigera);
            const std::string efterÅngra = ta.text();
            check(efterÅngra != efterF10 &&
                      efterÅngra.find("1 -2 -3") == std::string::npos &&
                      w.ändringar == 1,
                  "F13: Ctrl+Z ångrar den flerstyckiga insättningen — ett "
                  "onChange",
                  syns(efterÅngra) + ", " + std::to_string(w.ändringar));
            check(ta.caretRect(ta.behaviorCtx()).w > 0.0f,
                  "F13: careten SYNS efter ångra — fältet rullade dit");
            w.ref.setText(efterÅngra);
            const std::vector<std::size_t> inkrÅ = vandring(ta);
            const std::vector<std::size_t> helÅ  = vandring(w.ref);
            check(inkrÅ == helÅ,
                  "F13: radstarterna efter ångra = en hel omläggnings",
                  std::to_string(inkrÅ.size()) + " mot " +
                      std::to_string(helÅ.size()) + " rader");
            ta.focus();
            w.ändringar = 0;
            w.tangent(Key::Y, kRedigera);
            check(ta.text() == efterF10 && w.ändringar == 1,
                  "F13: Ctrl+Y gör om — F10:s text igen, ett onChange",
                  syns(ta.text()));
            w.ref.setText(efterF10);
            const std::vector<std::size_t> inkrG = vandring(ta);
            const std::vector<std::size_t> helG  = vandring(w.ref);
            check(inkrG == helG,
                  "F13: radstarterna efter gör om = en hel omläggnings");
            ta.focus();
            w.tangent(Key::Z, kRedigera);
            w.tangent(Key::Z, kRedigera | kShift);
            check(ta.text() == efterF10,
                  "F13: Ctrl+Shift+Z gör också om", syns(ta.text()));
            storlek(ta, bryt, 12);
        }

        // F11. UTF-8 — Upp/Ner landar på teckengräns.
        ta.setWrap(false);
        ta.setText("\xC3\xA5\xC3\xA4\xC3\xB6\xC3\xA5\nab\n\xC3\xA5\xC3\xA4\xC3\xB6\xC3\xA5");
        ta.focus();
        w.tangent(Key::Home, kCtrl);
        w.tangenter(Key::Right, 2);
        w.tangent(Key::Down);
        const bool g1 = påGräns(ta.text(), ta.caret());
        w.tangent(Key::Down);
        check(g1 && påGräns(ta.text(), ta.caret()) && ta.caret() == 16,
              "F11: Upp/Ner över åäö landar på teckengräns, x bevarat (16)",
              std::to_string(ta.caret()));

        // F12. TAB flyttar fokus och lämnar texten.
        ta.setText("abc");
        ta.focus();
        w.tangent(Key::Tab);
        check(w.efter.focused() && ta.text() == "abc",
              "F12: Tab flyttar fokus till nästa stopp, texten orörd");

        w.close();
    });
    w.run();

    if (!kedjanKordes) {
        std::printf("  --   fönsterfallen kördes ALDRIG (pumpen startade inte)\n");
        ++g_skips;
    }
}

// ================================================================ --png
//
// ATT ÖGNA (inte en del av grinden): det som kräver FOKUS och därför aldrig
// syns i pixel_gate eller glaze-render — en markering över tre rader med
// careten i dess slut — bredvid ett fält utan brytning rullat i sidled, ett
// skrivskyddat och ett inaktiverat, per UI × läge.
std::string g_png;

void skrivPng()
{
    struct Ögon : glaze::AppWindow {
        using glaze::AppWindow::handleEvent;
        Prov markerad, rullad, skyddad, av;
        Ögon() : glaze::AppWindow("textarea_gate --png", 560, 220)
        {
            setContentSize(560, 220);
            markerad.x = 10;
            markerad.y = 10;
            markerad.width = 260;
            markerad.setText("Anteckningar\nRad två med åäö.\nEn rad som "
                             "bryts mot fältets bredd när den är lång.\nSista");
            rullad.x = 290;
            rullad.y = 10;
            rullad.width = 260;
            rullad.setWrap(false);
            rullad.setText("Utan brytning: en rad som är längre än fältet\n"
                           "Kort\nTredje raden, också den lång nog att rulla\n"
                           "Fyra\nFem\nSex");
            skyddad.x = 10;
            skyddad.y = 120;
            skyddad.width = 260;
            skyddad.setReadOnly(true);
            skyddad.setText("Skrivskyddad:\nmarkera och kopiera");
            av.x = 290;
            av.y = 120;
            av.width = 260;
            av.setText("Inaktiverad:\ntexten dimmas");
            av.disable();
            root().adopt(markerad);
            root().adopt(rullad);
            root().adopt(skyddad);
            root().adopt(av);
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
    } w;
    w.setOffscreen(true);

    const glaze::UI        ui[]    = {glaze::UI::Glaze, glaze::UI::Slate,
                                      glaze::UI::Minimalistic};
    const char*            uiN[]   = {"Glaze", "Slate", "Minimalistic"};
    const glaze::ThemeMode läge[]  = {glaze::ThemeMode::Light,
                                      glaze::ThemeMode::Dark};
    const char*            lägeN[] = {"ljust", "morkt"};
    int                    i       = 0;
    std::function<void()>  nästa;
    nästa = [&] {
        if (!w.isOpen() || i >= 6) {
            w.close();
            return;
        }
        w.setUI(ui[i / 2]);
        w.setThemeMode(läge[i % 2]);
        w.markerad.focus();
        w.tangent(gfx::Key::Home, gfx::kModCtrl);
        w.tangent(gfx::Key::Down);
        for (int k = 0; k < 4; ++k)
            w.tangent(gfx::Key::Right);
        w.tangent(gfx::Key::Down, gfx::kModShift);
        w.tangent(gfx::Key::Down, gfx::kModShift);
        w.rullad.setScroll(120.0f, 0.0f);
        std::vector<std::uint8_t> px;
        int                       pw = 0, ph = 0;
        w.invalidate();
        w.window().renderFrame();
        if (w.window().captureFrame(px, pw, ph))
            gfx::encodePng(g_png + "/textarea_" + uiN[i / 2] + "_" +
                               lägeN[i % 2] + ".png",
                           px, pw, ph);
        ++i;
        glaze::app().post(nästa);
    };
    glaze::app().post(nästa);
    w.run();
    std::printf("     --png: bilderna i %s\n", g_png.c_str());
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
        else if (std::strcmp(argv[i], "--png") == 0 && i + 1 < argc)
            g_png = argv[++i];
    }

    std::printf("textarea_gate — det flerradiga fältet\n");

    fallFiltren();
    fallGränser();
    fallApi();
    fallSkalor();
    fallTabbar();
    fallMedFonster();
    if (!g_png.empty())
        skrivPng();

    std::printf("\n%s — %d fall, %d fel, %d överhoppade block\n",
                g_fails == 0 ? "TEXTAREA_GATE GRÖN" : "TEXTAREA_GATE FÄLLD",
                g_cases, g_fails, g_skips);
    if (g_fails != 0)
        return 1;
    return g_skips == 0 ? 0 : 77;
}
