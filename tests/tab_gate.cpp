// tab_gate — FLIKARNAS grind: `TabControl` + `TabPage`,
// och ramverksytorna den bygger på — trädkrokarna `childAdded`/
// `childRemoved` och tangentvägen `descendantKeyDown`.
//
// Kör:  ./build/tests/tab_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterblocket kunde
//       inte köras (ingen display) — ctest räknar det som SKIPPED, aldrig
//       grönt
//
// TREDELAD:
//
//   HEADLESS     registret, SIDORNA och VALET (det lagrade talet mot det
//                effektiva, satsordningen — `setSelectedIndex` FÖRE sidorna
//                — och vilken sida som syns), BORTTAGNINGEN (valet följer
//                sidan, den valda ersätts av grannen, en borttagen sida
//                lämnar synlig, `releaseOwned` och återadoption), ett barn
//                som inte är en sida, sidans rubrik, och RITNINGEN mot en
//                inspelande renderer: vilken flik som fylls/markeras, var
//                den står (också när raden är full och förskjuten),
//                rubrikernas färg per familj och kortningen.
//   MED FÖNSTER  ett DOLT fönster (`setOffscreen`), händelserna genom
//                fönstrets RIKTIGA ingång (`AppWindow::handleEvent`) och
//                stegen som en kedja av POSTADE steg (datagrid_gate-
//                prejudikatet): tryck på flikar, tangenterna med
//                flikkontrollen i fokus, Ctrl+Tab INIFRÅN sidan (fokus
//                följer till den nya sidans första tabbstopp — också genom
//                en datagrid, en komposit som själv tackar nej till
//                Ctrl+Tab), tabbordningen, ett programmatiskt byte som
//                räddar fokus ur den gömda sidan, stängknappen (eventet,
//                ingen urval, dra bort för att ångra, borttagning ur
//                handlern) och en inaktiverad flikkontroll.
//   PIXLAR ramens SIDLINJE och flikens HÖRN genom
//                den riktiga backenden, i alla tre UI × ljust/mörkt × skalorna
//                1,0/1,25/1,5 — och `Draw::frameGap`s kapning direkt. Se
//                blocket PIXLARNA längst ned för vad som mäts och inte.
//
// GEOMETRIN MÄTS MOT ETT OBEROENDE FÖRVÄNTAT VÄRDE: flikarnas bredd räknas i grinden
// ur KONTRAKTET (rubriken + 2 × `tabPadX`, aldrig under `tabMinW`, luckan
// `tabGap`, raden `tabHeaderH`) — aldrig ur widgetens privata `tabRects`.
//
// SIDORNAS KOLLEKTIONSVOKABULÄR — headless, H6–H9:
//   H6  `addPage` lägger en ÄGD sida sist (referensen tillbaka; `unadopt`
//       förstör den) med och utan text, och efter en medlemssida
//   H7  `setPage` byter RUBRIKEN och lämnar sidans barn; utanför ⇒ no-op
//   H8  `setPages` byter ALLA sidor — en lånad släpps (lever, synlig, utan
//       förälder), ett barn som inte är en sida står kvar — och det LAGRADE
//       valet står kvar (`setSelectedIndex` före `setPages` gäller efteråt)
//   H9  ATOMISKT: en sida med en öppen popup gör att `setPages` inte ändrar
//       någonting
// Sabotagen för dem: `setPage` som river barnen (H7), `setPages` utan
// förprövningen (H9: sidorna före den med popup är borta), utan att skriva
// tillbaka valet (H8: valet blir 0).
//
// SABOTAGEN den fäller:
//   * `TabControl::descendantKeyDown` som svarar false (F4: Ctrl+Tab inifrån
//     sidan byter inte flik)
//   * AppWindows förfadersloop bortplockad (F4, F8)
//   * `syncPages` utan fokusräddningen (F6: fokus står kvar i den gömda
//     sidan)
//   * `childRemoved` utan "valet följer sidan" (H4)
//   * `childAdded` som inte synkar (H3: valet före sidorna gäller inte)
//   * ett tryck på stängknappen som också väljer fliken (F7)
//   * Minimalistics streck på varje flik i stället för den valda (R2)
//   * radförskjutningen bortplockad (R4: den valda fliken utanför raden)
//   * rubrikerna ritas inte alls (R1, R2, R3, R5 — och pixel_gate fäller
//     samma sabotage på den inaktiverade flikkontrollens av/på i Slate)
//
// VAD DEN INTE MÄTER, skrivet så anspråket inte överdrivs:
//   * UTSEENDET — att flikkontrollen RITAR något i varje UI × tema × läge
//     mäter `pixel_gate` (galleri-fixturen); recepten är granskade renderade
//     i alla tre UI, ljust och mörkt. R-fallen mäter bara besluten som är
//     LOGIK (vilken flik, vilken färg, var), och PIXLAR-blocket bara ramens
//     sidlinje och flikens hörn.
//   * hovern och stängknappens platta i pixlar — bara att trycket når rätt
//     knapp.
//   * en sida som bär en ÖPPEN popup när fliken byts med Ctrl+Tab: fokus
//     flyttar, och dropdownens `focusLost` stänger listan — samma väg som
//     ett klick utanför, mätt i dropdown_gate.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <gfx/null_renderer.h>
#include <gfx/platform.h>

#include <glaze/glaze.h>

namespace {

int  g_fails   = 0;
int  g_cases   = 0;
int  g_skips   = 0;
int  g_hörn    = 0;   // P2-mätningar på hela pixlar (se PIXLARNA)
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

bool nära(float a, float b) { return std::abs(a - b) < 0.01f; }

bool sammaRect(const gfx::Rectf& a, const gfx::Rectf& b)
{
    return nära(a.x, b.x) && nära(a.y, b.y) && nära(a.w, b.w) && nära(a.h, b.h);
}

bool lika(gfx::Color a, gfx::Color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// De skyddade delarna anropbara — det en subklass får göra — och eventen
// RÄKNADE (regel 6: sender är flikkontrollen, värdet läses ur den).
struct Prov : glaze::TabControl {
    using glaze::TabControl::draw;

    int  byten     = 0;
    int  senaste   = -1;   // selectedIndex() i senaste onChange
    int  stängda   = 0;
    int  stängIdx  = -1;
    bool avsändare = true;
    Prov()
    {
        onChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            ++byten;
            senaste   = selectedIndex();
            avsändare = avsändare && s == this;
        };
    }
};

// ------------------------------------------------------ falsk font
// Fast teckensteg per BYTE — rubrikerna i ritfallen är ASCII.
class StegFont final : public gfx::NullFont {
public:
    gfx::Sizef  measure(const std::string& s) const override
    {
        return gfx::Sizef{8.0f * float(s.size()), 16.0f};
    }
    float       height() const override { return 16.0f; }
    bool        valid() const override { return true; }
    float       prefixWidth(const std::string&, std::size_t b) const override
    {
        return 8.0f * float(b);
    }
    std::size_t byteIndexAtX(const std::string& s, float x) const override
    {
        return fitWidth(s, x + 4.0f);
    }
    std::size_t fitWidth(const std::string& s, float maxW) const override
    {
        const auto n = std::size_t(std::max(0.0f, maxW) / 8.0f);
        return std::min(n, s.size());
    }
};

// Renderer som SPELAR IN fyllningarna och texten (med färg) — resten är
// NullRendererns no-op.
struct Inspelning : gfx::NullRenderer {
    struct Fyll {
        gfx::Rectf r;
        gfx::Color c;
    };
    struct Text {
        std::string s;
        gfx::Color  c;
    };
    std::vector<Fyll> fyll;
    std::vector<Text> texter;

    void fillRect(const gfx::Rectf& r, gfx::Color c) override
    {
        fyll.push_back({r, c});
    }
    void fillRoundedRect(const gfx::Rectf& r, float, gfx::Color c) override
    {
        fyll.push_back({r, c});
    }
    float drawText(gfx::Font&, float, float, const std::string& s,
                   gfx::Color c) override
    {
        texter.push_back({s, c});
        return 0.0f;
    }

    void töm()
    {
        fyll.clear();
        texter.clear();
    }
    bool harFyll(const gfx::Rectf& r, gfx::Color c) const
    {
        for (const Fyll& f : fyll)
            if (lika(f.c, c) && sammaRect(f.r, r))
                return true;
        return false;
    }
    // Ritades texten `s`, och i så fall i färgen `c` (första förekomsten)?
    bool textFärg(const std::string& s, gfx::Color c) const
    {
        for (const Text& t : texter)
            if (t.s == s)
                return lika(t.c, c);
        return false;
    }
    bool harText(const std::string& s) const
    {
        for (const Text& t : texter)
            if (t.s == s)
                return true;
        return false;
    }
};

// ============================================================== HEADLESS

void fallRegistret()
{
    auto t = glaze::makeWidget("tabcontrol");
    auto p = glaze::makeWidget("tabpage");
    check(t && dynamic_cast<glaze::TabControl*>(t.get()) != nullptr,
          "H1: registret — \"tabcontrol\" ger en riktig glaze::TabControl");
    check(p && dynamic_cast<glaze::TabPage*>(p.get()) != nullptr &&
              dynamic_cast<glaze::Panel*>(p.get()) != nullptr,
          "H1: registret — \"tabpage\" ger en TabPage, och sidan ÄR en Panel");
    glaze::TabControl tc;
    check(tc.focusable() && !tc.focusesChildren() && tc.placesChildren() &&
              tc.clipsChildren(),
          "H1: ett tabbstopp vars sidor FÖLJER efter det; placerar och klipper "
          "sina barn");
    check(tc.pageCount() == 0 && tc.selectedIndex() == -1 &&
              !tc.selectedPage() && !tc.isClosable(),
          "H1: utan sidor — inget val, inte stängbar");
    check(tc.setProp("selectedIndex", "2") && tc.setProp("closable", "true") &&
              tc.isClosable() && !tc.setProp("text", "x"),
          "H1: setProp — selectedIndex och closable, ingen text");
}

void fallSidornaOchValet()
{
    glaze::TabControl tc;
    auto* a = tc.add<glaze::TabPage>("A");
    auto* b = tc.add<glaze::TabPage>("B");
    auto* c = tc.add<glaze::TabPage>("C");
    check(tc.pageCount() == 3 && tc.page(0) == a && tc.page(2) == c &&
              !tc.page(3) && !tc.page(-1) && tc.pageIndex(*b) == 1,
          "H2: sidorna i adopt-ordning — page/pageIndex, utanför ⇒ nullptr");
    check(tc.selectedIndex() == 0 && a->isVisible() && !b->isVisible() &&
              !c->isVisible(),
          "H2: default — den första sidan vald och synlig, de andra dolda");
    tc.setSelectedIndex(2);
    check(tc.selectedIndex() == 2 && tc.selectedPage() == c && c->isVisible() &&
              !a->isVisible() && !b->isVisible(),
          "H2: setSelectedIndex byter vilken sida som syns");
    tc.setSelectedIndex(-1);
    check(tc.selectedIndex() == 2, "H2: negativt index ⇒ logg + no-op");
    tc.setSelectedIndex(7);
    check(tc.selectedIndex() == 2 && c->isVisible(),
          "H2: ett index BORTOM sidorna visar den sista (det lagrade talet "
          "klampas vid läsning)");
}

// SATSORDNINGEN: en `.glazeui` sätter valet i widgetens block, FÖRE
// trädbyggets adopt-rader.
void fallOrdningen()
{
    glaze::TabControl tc;
    tc.setSelectedIndex(1);
    glaze::TabPage a("A"), b("B");
    tc.adopt(a);
    check(tc.selectedIndex() == 0 && a.isVisible(),
          "H3: med EN sida visas den (talet 1 klampas)");
    tc.adopt(b);
    check(tc.selectedIndex() == 1 && b.isVisible() && !a.isVisible(),
          "H3: valet satt FÖRE sidorna gäller när den andra sidan kommer");
}

void fallBorttagningen()
{
    glaze::TabControl tc;
    glaze::TabPage    a("A"), b("B"), c("C"), d("D");
    tc.adopt(a);
    tc.adopt(b);
    tc.adopt(c);
    tc.adopt(d);
    tc.setSelectedIndex(2);   // C
    check(tc.unadopt(a) && tc.selectedPage() == &c && tc.selectedIndex() == 1,
          "H4: en sida FÖRE den valda tas bort — valet FÖLJER sidan",
          std::to_string(tc.selectedIndex()));
    check(a.isVisible() && !a.parent(),
          "H4: den borttagna lämnar flikkontrollen SYNLIG");
    check(tc.unadopt(c) && tc.selectedPage() == &d && d.isVisible(),
          "H4: den VALDA tas bort — grannen till höger glider in och visas");
    check(tc.unadopt(d) && tc.selectedPage() == &b && b.isVisible(),
          "H4: den valda var SIST — grannen till vänster väljs");
    check(tc.unadopt(b) && tc.selectedIndex() == -1 && tc.pageCount() == 0,
          "H4: sista sidan borta — inget val");

    // ÄGT: `releaseOwned` ger sidan tillbaka synlig; åter in blir den dold.
    glaze::TabControl t2;
    auto* e = t2.add<glaze::TabPage>("E");
    auto* f = t2.add<glaze::TabPage>("F");
    std::unique_ptr<glaze::Widget> ut = t2.releaseOwned(*f);
    check(ut && ut.get() == f && f->isVisible() && t2.pageCount() == 1 &&
              t2.selectedPage() == e,
          "H4: releaseOwned av en dold sida — tillbaka synlig, valet orört");
    t2.adopt(std::move(ut));
    check(t2.pageCount() == 2 && !f->isVisible() && e->isVisible(),
          "H4: åter adopterad blir den dold (den är inte vald)");
}

// En widget som säger att den har en ÖPPEN popup — borttagningens
// deny-check utan ett fönster (unadopt_gates fall 5b-prejudikat).
// `öppen = false` STÄNGER den: `unadopt` nekar ett delträd med en öppen popup
// (dess kontroll 4), så popupen måste stängas innan den går att ta ut.
struct ÖppenPopup : glaze::Widget {
    bool öppen = true;

protected:
    void draw(const glaze::Ctx&) override {}
    bool hasOpenPopup() const override { return öppen; }
};

void fallSidornasVokabular()
{
    // H6
    glaze::TabControl tc;
    glaze::TabPage    medlem("Medlem");
    tc.adopt(medlem);
    glaze::TabPage& ny  = tc.addPage("Ny");
    glaze::TabPage& tom = tc.addPage();
    check(tc.pageCount() == 3 && tc.page(1) == &ny && tc.page(2) == &tom &&
              ny.text() == "Ny" && tom.text().empty() && tc.page(0) == &medlem,
          "H6: addPage lägger ägda sidor sist, efter medlemssidan");
    glaze::Button knapp("I sidan");
    ny.adopt(knapp);
    // H7
    tc.setPage(1, glaze::tr(2));
    check(ny.textToken() == glaze::tr(2) && knapp.parent() == &ny &&
              ny.children().size() == 1,
          "H7: setPage byter rubriken och lämnar barnen");
    tc.setPage(9, "X");
    check(tc.pageCount() == 3, "H7: setPage utanför ⇒ no-op");
    ny.unadopt(knapp);
    check(tc.unadopt(tom) && tc.pageCount() == 2,
          "H6: en ägd sida går att ta bort (och förstörs)");

    // H8
    glaze::Button fel("Ingen sida");
    tc.adopt(fel);
    tc.setSelectedIndex(1);
    tc.setPages({"A", "B", "C"});
    check(tc.pageCount() == 3 && tc.page(0)->text() == "A" &&
              tc.page(2)->text() == "C" && tc.selectedIndex() == 1 &&
              tc.page(1)->isVisible(),
          "H8: setPages byter alla sidor, och det lagrade valet står kvar",
          std::to_string(tc.selectedIndex()));
    check(!medlem.parent() && medlem.isVisible(),
          "H8: den lånade medlemssidan släpps — synlig, utan förälder");
    check(fel.parent() == &tc, "H8: ett barn som inte är en sida står kvar");
    tc.unadopt(fel);

    // H9
    glaze::TabControl t2;
    t2.addPage("Ett");
    glaze::TabPage& två = t2.addPage("Två");
    ÖppenPopup      popup;
    två.adopt(popup);
    t2.setPages({"X"});
    check(t2.pageCount() == 2 && t2.page(0)->text() == "Ett" &&
              t2.page(1) == &två && popup.parent() == &två,
          "H9: en sida med öppen popup ⇒ setPages ändrar INGENTING");
    // Stängd först: en öppen popup hade nekat också `unadopt` (samma
    // kontroll), och popupen hade då suttit kvar i sidan.
    popup.öppen = false;
    check(två.unadopt(popup) && !popup.parent(),
          "H9: (förutsättning) den stängda popupen går att ta ut ur sidan");
    t2.setPages({"X"});
    check(t2.pageCount() == 1 && t2.page(0)->text() == "X",
          "H9: utan popupen går bytet igenom");
}

void fallInteEnSida()
{
    glaze::TabControl tc;
    tc.add<glaze::TabPage>("A");
    glaze::Button knapp("Fel förälder");
    tc.adopt(knapp);
    check(tc.pageCount() == 1 && !knapp.isVisible(),
          "H5: ett barn som inte är en TabPage blir ingen flik — det döljs");
    check(tc.unadopt(knapp) && knapp.isVisible(),
          "H5: …och lämnar synligt");
}

void fallRubriken()
{
    glaze::setLanguage("sv");
    glaze::TabPage p;
    check(p.setProp("text", "Allmänt") && p.text() == "Allmänt",
          "H6: sidans rubrik är `text` (setProp)");
    check(p.setTextToken("text", glaze::tr(2)) && p.textToken().id() == 2 &&
              p.text() == "Avbryt",
          "H6: glaze::tr(N) sätter rubrikens TOKEN");
    check(p.setTextToken("toolTip", glaze::tr(3)) && p.toolTipToken().id() == 3 &&
              p.textToken().id() == 2,
          "H6: setTextToken kedjar till basen (tooltipen) och rör inte rubriken");
    check(!p.setProp("title", "x"), "H6: inget title-alias");
}

// RITNINGEN — besluten som är LOGIK, mot en inspelande renderer.
struct Ritfixtur {
    StegFont      f;
    Inspelning    rec;
    Prov          tc;
    glaze::Style  stil;

    explicit Ritfixtur(glaze::UI ui, float bredd)
        : stil(glaze::defaultStyle(ui))
    {
        tc.width  = bredd;
        tc.height = 150;
        for (const char* t : {"Alfa", "Beta", "Gamma"})
            tc.add<glaze::TabPage>(t);
    }
    void rita(glaze::UI ui, const glaze::Theme::Variant& v)
    {
        rec.töm();
        glaze::Ctx c{};
        c.r     = &rec;
        c.font  = &f;
        c.scale = 1.0f;
        c.ui    = ui;
        c.style = &stil;
        c.theme = &v;
        c.area  = {0, 0, 600, 300};
        c.box   = tc.resolve(c);
        tc.draw(c);
    }
    // FÖRVÄNTAT VÄRDE ur kontraktet: flik i:s rekt när raden INTE är full.
    gfx::Rectf flik(int i) const
    {
        const char* t[] = {"Alfa", "Beta", "Gamma"};
        float       x   = stil.tabGap;
        for (int k = 0;; ++k) {
            const float w = std::max(stil.tabMinW,
                                     2.0f * stil.tabPadX +
                                         8.0f * float(std::strlen(t[k])));
            if (k == i)
                return gfx::Rectf{x, 0.0f, w, stil.tabHeaderH};
            x += w + stil.tabGap;
        }
    }
};

void fallRitningen()
{
    // ---- SLATE — den valda fliken är sidans yta som sticker upp: `panel`,
    //      ritad som en rundad rekt en hel flikhöjd NEDANFÖR fliken (de
    //      nedre hörnen klipps bort). Ytan under raden fylls också.
    {
        Ritfixtur fx(glaze::UI::Slate, 300);
        const glaze::Theme::Variant& v = glaze::defaultTheme(glaze::UI::Slate).light;
        fx.tc.setSelectedIndex(1);
        fx.rita(glaze::UI::Slate, v);
        const gfx::Rectf t = fx.flik(1);
        const float      h = fx.stil.tabHeaderH;
        check(fx.rec.harFyll({t.x, t.y, t.w, 2.0f * t.h}, v.colors.panel),
              "R1: Slate — den VALDA fliken fylls med panel, på facits plats");
        check(!fx.rec.harFyll({fx.flik(0).x, 0, fx.flik(0).w, 2.0f * h},
                              v.colors.panel),
              "R1: Slate — en ovald flik har ingen platta i vila");
        check(fx.rec.harFyll({0, h, 300, 150 - h}, v.colors.panel),
              "R1: Slate — visningsytan under raden fylls med panel");
        check(fx.rec.textFärg("Alfa", v.colors.text) &&
                  fx.rec.textFärg("Beta", v.colors.text) &&
                  fx.rec.textFärg("Gamma", v.colors.text),
              "R1: Slate — varje sidas rubrik ritas, i text");
    }

    // ---- MINIMALISTIC — ingen flikform: den valda bär radens streck i
    //      underkanten och rubriken i accent.
    {
        Ritfixtur fx(glaze::UI::Minimalistic, 300);
        const glaze::Theme::Variant& v =
            glaze::defaultTheme(glaze::UI::Minimalistic).light;
        fx.tc.setSelectedIndex(2);
        fx.rita(glaze::UI::Minimalistic, v);
        const float      mw = fx.stil.rowMarkerW;
        const gfx::Rectf t  = fx.flik(2);
        check(fx.rec.harFyll({t.x, t.h - mw, t.w, mw}, v.colors.accent),
              "R2: Minimalistic — strecket under den VALDA fliken");
        check(!fx.rec.harFyll({fx.flik(0).x, t.h - mw, fx.flik(0).w, mw},
                              v.colors.accent) &&
                  !fx.rec.harFyll({fx.flik(1).x, t.h - mw, fx.flik(1).w, mw},
                                  v.colors.accent),
              "R2: Minimalistic — inget streck under de ovalda");
        check(fx.rec.textFärg("Gamma", v.colors.accent) &&
                  fx.rec.textFärg("Alfa", v.colors.text),
              "R2: Minimalistic — den valda rubriken i accent, de andra i text");
    }

    // ---- GLAZE — Slates form; rubrikerna i text.
    {
        Ritfixtur fx(glaze::UI::Glaze, 300);
        const glaze::Theme::Variant& v = glaze::defaultTheme(glaze::UI::Glaze).light;
        fx.rita(glaze::UI::Glaze, v);
        const gfx::Rectf t = fx.flik(0);
        check(fx.rec.harFyll({t.x, t.y, t.w, 2.0f * t.h}, v.colors.panel) &&
                  fx.rec.textFärg("Alfa", v.colors.text),
              "R3: Glaze — den valda fliken fylls med panel, rubrikerna i text");
    }

    // ---- INAKTIVERAD (Slate) — all text dämpas.
    {
        Ritfixtur fx(glaze::UI::Slate, 300);
        const glaze::Theme::Variant& v = glaze::defaultTheme(glaze::UI::Slate).light;
        fx.tc.disable();
        fx.rita(glaze::UI::Slate, v);
        check(fx.rec.textFärg("Alfa", v.colors.textDim) &&
                  fx.rec.textFärg("Beta", v.colors.textDim),
              "R5: inaktiverad — rubrikerna i textDim");
    }

    // ---- FULL RAD (Slate, 120 breda): flikarna krymper till `tabMinW`,
    //      rubrikerna kortas, och raden förskjuts så att den VALDA syns hel.
    {
        Ritfixtur fx(glaze::UI::Slate, 120);
        const glaze::Theme::Variant& v = glaze::defaultTheme(glaze::UI::Slate).light;
        const float min = fx.stil.tabMinW;
        const float gap = fx.stil.tabGap;
        fx.rita(glaze::UI::Slate, v);
        check(fx.rec.harFyll({gap, 0, min, 2.0f * fx.stil.tabHeaderH},
                             v.colors.panel),
              "R4: full rad — den valda (första) fliken krympt till tabMinW, "
              "oförskjuten");
        check(!fx.rec.harText("Alfa") && !fx.rec.harText("Gamma"),
              "R4: full rad — rubrikerna kortas (ritas inte hela)");
        fx.tc.setSelectedIndex(2);
        fx.rita(glaze::UI::Slate, v);
        // Den tredje flikens högerkant hamnar på radens högerkant.
        check(fx.rec.harFyll({120 - min, 0, min, 2.0f * fx.stil.tabHeaderH},
                             v.colors.panel),
              "R4: full rad — raden förskjuts så att den VALDA (sista) syns "
              "hel vid högerkanten");
    }
}

// ============================================================ MED FÖNSTER

struct Vard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    Prov             flikar;
    glaze::TextInput fältA;       // sida 0 — ett tabbstopp
    glaze::Button    knappB;      // sida 1 — dess första tabbstopp
    glaze::DataGrid  gridB;       // sida 1 — en komposit som tackar nej
                                  // till Ctrl+Tab
    glaze::Label     tomC;        // sida 2 — inget tabbstopp alls
    glaze::TabPage   sidaA{"Alfa"}, sidaB{"Beta"}, sidaC{"Gamma"};
    glaze::TextInput efter;       // efter flikkontrollen
    int              meny = 0;    // menyradens Ctrl+Tab-accelerator

    Vard() : glaze::AppWindow("tab_gate", 560, 360, gfx::Chrome::None)
    {
        flikar.x      = 10;
        flikar.y      = 10;
        flikar.width  = 400;
        flikar.height = 260;
        flikar.name   = "flikar";
        fältA.name    = "faltA";
        fältA.x       = 10;
        fältA.y       = 10;
        knappB.name   = "knappB";
        knappB.setText("Knapp");
        knappB.x      = 10;
        knappB.y      = 10;
        gridB.name    = "gridB";
        gridB.x       = 10;
        gridB.y       = 60;
        gridB.width   = 300;
        gridB.height  = 120;
        gridB.addColumn("Kol", 120);
        gridB.addRow({"rad"});
        tomC.setText("Tom sida");
        tomC.x = 10;
        tomC.y = 10;
        sidaA.adopt(fältA);
        sidaB.adopt(knappB);
        sidaB.adopt(gridB);
        sidaC.adopt(tomC);
        flikar.adopt(sidaA);
        flikar.adopt(sidaB);
        flikar.adopt(sidaC);
        root().adopt(flikar);
        efter.name = "efter";
        efter.x    = 430;
        efter.y    = 10;
        root().adopt(efter);

        // En ACCELERATOR på samma tangent — flikkontrollen ska vinna när
        // fokus står i den (fokus närmast vinner).
        menuBar.addMenu("Arkiv");
        menuBar.addItem("Arkiv", "Nästa", "Ctrl+Tab");
        menuBar.setHandler({"Arkiv", "Nästa"}, [this] { ++meny; });
        menuBar.show();
    }

    float skala() { return window().uiScale(); }
    gfx::Rectf yta() { return flikar.resolve(flikar.behaviorCtx()); }
    // FÖRVÄNTAT VÄRDE ur kontraktet — flik i:s mitt i fysiska px, med fönstrets font.
    gfx::Rectf flik(int i)
    {
        const glaze::Style& st = style();
        const char*         t[] = {"Alfa", "Beta", "Gamma", "Delta"};
        const float         s   = skala();
        float               x   = st.tabGap;
        for (int k = 0;; ++k) {
            const float text = font().measure(t[k]).w / s;
            const float knapp =
                flikar.isClosable() ? st.tabHeaderH - st.tabPadX : 0.0f;
            const float w =
                std::max(st.tabMinW + knapp, 2.0f * st.tabPadX + text + knapp);
            if (k == i)
                return gfx::Rectf{yta().x + x * s, yta().y, w * s,
                                  st.tabHeaderH * s};
            x += w + st.tabGap;
        }
    }
    gfx::Pointf mitt(const gfx::Rectf& r)
    {
        return gfx::Pointf{r.x + r.w / 2.0f, r.y + r.h / 2.0f};
    }
    // Stängknappens mitt — en kvadrat lika hög som raden, längst till höger.
    gfx::Pointf kryss(int i)
    {
        const gfx::Rectf r = flik(i);
        return gfx::Pointf{r.x + r.w - r.h / 2.0f, r.y + r.h / 2.0f};
    }

    void mus(gfx::EventType t, float x, float y)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = x;
        e.mouseY   = y;
        e.button   = gfx::MouseButton::Left;
        handleEvent(e);
    }
    void klick(gfx::Pointf p)
    {
        mus(gfx::EventType::MouseButtonDown, p.x, p.y);
        mus(gfx::EventType::MouseButtonUp, p.x, p.y);
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
    bool iOrdning(const glaze::Widget* w)
    {
        const std::vector<glaze::Widget*> o = focusOrder();
        return std::find(o.begin(), o.end(), w) != o.end();
    }
};

void fallMedFonster()
{
    Vard w;
    w.setOffscreen(true);
    Prov& f = w.flikar;

    bool                  kordes = false;
    int                   steg   = 0;
    std::function<void()> nästa;
    nästa = [&] {
        switch (steg++) {
        case 0: {
            kordes = true;
            // ---- F1. TRYCK på en flik väljer den — ett onChange, och
            //          flikkontrollen får fokus.
            w.klick(w.mitt(w.flik(1)));
            check(f.selectedIndex() == 1 && f.byten == 1 && f.senaste == 1 &&
                      f.avsändare && f.focused(),
                  "F1: tryck på flik 1 — vald, EXAKT ett onChange, fokus",
                  std::to_string(f.selectedIndex()) + "/" +
                      std::to_string(f.byten));
            check(w.sidaB.isVisible() && !w.sidaA.isVisible() &&
                      !w.sidaC.isVisible(),
                  "F1: den valda sidan syns, de andra är dolda");
            w.klick(w.mitt(w.flik(1)));
            check(f.byten == 1, "F1: tryck på den redan valda — inget event");

            // ---- F2. TABBORDNINGEN: flikkontrollen, sedan den VALDA sidans
            //          kontroller — en dold sidas finns inte.
            check(w.iOrdning(&f) && w.iOrdning(&w.knappB) &&
                      w.iOrdning(&w.gridB) && !w.iOrdning(&w.fältA),
                  "F2: tabbordningen — flikkontrollen och den valda sidans "
                  "stopp, aldrig en dold sidas");
            w.tangent(gfx::Key::Tab);
            check(w.knappB.focused(),
                  "F2: Tab från flikkontrollen går till sidans första stopp");
            w.tangent(gfx::Key::Tab, gfx::kModShift);
            check(f.focused(), "F2: Shift+Tab tillbaka till flikkontrollen");

            // ---- F3. TANGENTERNA med flikkontrollen i fokus.
            w.tangent(gfx::Key::Right);
            check(f.selectedIndex() == 2 && f.byten == 2,
                  "F3: Höger — nästa flik, ett event");
            w.tangent(gfx::Key::Right);
            check(f.selectedIndex() == 2 && f.byten == 2,
                  "F3: Höger i kanten — stannar, inget event");
            w.tangent(gfx::Key::Home);
            check(f.selectedIndex() == 0 && f.byten == 3, "F3: Home — första");
            w.tangent(gfx::Key::End);
            check(f.selectedIndex() == 2, "F3: End — sista");
            w.tangent(gfx::Key::Tab, gfx::kModCtrl);
            check(f.selectedIndex() == 0 && f.focused() && w.meny == 0,
                  "F3: Ctrl+Tab i sista — RUNT OM till första, fokus kvar, "
                  "inte menyns accelerator");
            w.tangent(gfx::Key::Tab, gfx::kModCtrl | gfx::kModShift);
            check(f.selectedIndex() == 2, "F3: Ctrl+Shift+Tab — runt om bakåt");
            w.tangent(gfx::Key::PageUp, gfx::kModCtrl);
            check(f.selectedIndex() == 1, "F3: Ctrl+PageUp — föregående");
            w.tangent(gfx::Key::PageDown, gfx::kModCtrl);
            check(f.selectedIndex() == 2, "F3: Ctrl+PageDown — nästa");

            // ---- F4. Ctrl+Tab INIFRÅN SIDAN: fokus följer till den nya
            //          sidans första tabbstopp.
            w.tangent(gfx::Key::Home);              // sida 0
            w.klick(w.mitt(w.fältA.resolve(w.fältA.behaviorCtx())));
            check(w.fältA.focused(), "F4: fältet i sida 0 har fokus");
            const int före = f.byten;
            w.tangent(gfx::Key::Tab, gfx::kModCtrl);
            check(f.selectedIndex() == 1 && f.byten == före + 1 &&
                      w.knappB.focused() && !w.fältA.focused() && w.meny == 0,
                  "F4: Ctrl+Tab i sidan — nästa flik, ETT event, fokus till "
                  "den nya sidans första stopp, menyn tog den inte");
            // …genom en KOMPOSIT som själv tackar nej till Ctrl+Tab.
            w.klick(w.mitt(w.gridB.resolve(w.gridB.behaviorCtx())));
            check(w.gridB.focused(), "F4: datagriden i sida 1 har fokus");
            w.tangent(gfx::Key::Tab, gfx::kModCtrl);
            check(f.selectedIndex() == 2 && f.focused(),
                  "F4: Ctrl+Tab genom datagriden — sida 2 saknar tabbstopp, så "
                  "fokus landar i flikkontrollen");
            w.tangent(gfx::Key::Tab, gfx::kModCtrl);   // runt om till sida 0
            check(f.selectedIndex() == 0 && f.focused(),
                  "F4: Ctrl+Tab med flikkontrollen i fokus — fokus står kvar");
            break;
        }
        case 1: {
            // ---- F6. PROGRAMMATISKT byte med fokus i sidan — fokus räddas
            //          till flikkontrollen, inget event.
            w.klick(w.mitt(w.fältA.resolve(w.fältA.behaviorCtx())));
            const int före = f.byten;
            f.setSelectedIndex(1);
            check(f.selectedIndex() == 1 && !w.fältA.focused() && f.focused() &&
                      f.byten == före,
                  "F6: setSelectedIndex med fokus i den gömda sidan — fokus "
                  "till flikkontrollen, inget onChange");

            // ---- F9. En DOLD sidas kontroll träffas inte.
            const gfx::Pointf p = w.mitt(w.knappB.resolve(w.knappB.behaviorCtx()));
            f.setSelectedIndex(2);
            w.klick(p);
            check(!w.knappB.focused() && f.selectedIndex() == 2,
                  "F9: tryck där en dold sidas knapp stod — ingenting");

            // ---- F10. INAKTIVERAD — varken tryck eller Ctrl+Tab inifrån
            //           sidan; tangenten går vidare till menyns accelerator.
            f.setSelectedIndex(1);
            w.knappB.focus();
            f.disable();
            w.klick(w.mitt(w.flik(0)));
            check(f.selectedIndex() == 1, "F10: inaktiverad — trycket byter inte");
            const int menyFöre = w.meny;
            w.tangent(gfx::Key::Tab, gfx::kModCtrl);
            check(f.selectedIndex() == 1 && w.meny == menyFöre + 1,
                  "F10: inaktiverad — Ctrl+Tab inifrån frågar den inte, och "
                  "menyns accelerator får tangenten");
            f.enable();
            w.efter.focus();

            // ---- F7. STÄNGKNAPPEN: eventet med fliken, inget val, och appen
            //          tar bort sidan i handlern.
            f.setClosable(true);
            f.setSelectedIndex(0);
            f.onTabClose = [&](glaze::Widget* s, glaze::TabCloseArgs& e) {
                ++f.stängda;
                f.stängIdx = e.index;
                if (s == &f && e.index == 1)
                    f.unadopt(*f.page(e.index));
            };
            const int byten = f.byten;
            // Pekaren står över flik 2 — då syns dess kryss — och trycket
            // släpps PÅ det.
            w.mus(gfx::EventType::MouseMotion, w.kryss(2).x, w.kryss(2).y);
            w.mus(gfx::EventType::MouseButtonDown, w.kryss(2).x, w.kryss(2).y);
            check(f.selectedIndex() == 0, "F7: ett tryck på krysset väljer INTE "
                                          "fliken");
            w.mus(gfx::EventType::MouseButtonUp, w.kryss(2).x, w.kryss(2).y);
            check(f.stängda == 1 && f.stängIdx == 2 && f.pageCount() == 3 &&
                      f.byten == byten,
                  "F7: släppt på krysset — onTabClose(2), sidan står kvar (appen "
                  "tog inte bort den), inget onChange",
                  std::to_string(f.stängIdx));
            // Dra bort för att ångra.
            w.mus(gfx::EventType::MouseButtonDown, w.kryss(2).x, w.kryss(2).y);
            w.mus(gfx::EventType::MouseMotion, w.kryss(2).x - w.flik(2).w,
                  w.kryss(2).y);
            w.mus(gfx::EventType::MouseButtonUp, w.kryss(2).x - w.flik(2).w,
                  w.kryss(2).y);
            check(f.stängda == 1, "F7: dragen bort från krysset — inget event");
            // Handlern TAR BORT sidan — trädets eget API, ur handlern.
            f.setSelectedIndex(2);   // Gamma — sidan efter den som stängs
            w.mus(gfx::EventType::MouseMotion, w.kryss(1).x, w.kryss(1).y);
            w.klick(w.kryss(1));
            check(f.stängda == 2 && f.pageCount() == 2 &&
                      f.selectedPage() == &w.sidaC && w.sidaC.isVisible() &&
                      w.sidaB.isVisible() && !w.sidaB.parent(),
                  "F7: handlern tog bort sida 1 — valet följde Gamma, den "
                  "borttagna lämnade synlig");
            w.close();
            return;
        }
        default:
            w.close();
            return;
        }
        glaze::app().post(nästa);
    };
    glaze::app().post(nästa);
    w.run();

    if (!kordes) {
        std::printf("  --   fönsterfallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    }
}

// =============================================================== PIXLARNA
//
// SIDLINJEN OCH HÖRNEN, mätt i PIXLAR genom den RIKTIGA backenden
// (glaze-renders väg: `setOffscreen` → `show` → `renderFrame` →
// `captureFrame`), i alla tre UI, ljust och mörkt, vid skalorna 1,0 · 1,25 ·
// 1,5 (användarskalan räknad mot ett provfönsters OS-skala, som glaze-render
// gör).
//
//   P1  SIDLINJEN ÄR HEL när den valda fliken står i ramens HÖGRA HÖRN
//       (raden full, den sista vald). Förväntat värde är linjens
//       EGEN nederdel: varje rad längs ramens högra
//       kant ska se ut som raden längst ned, och där ska linjen synas mot
//       ytan. En saknad övre halva ska fälla.
//   P2  HÖRNEN ÄR SLUTNA där den valda flikens sidlinjer möter ramens
//       överkant (Slate och Glaze — Minimalistic har ingen flikform):
//       hörnpixeln har linjens färg och inte ytans. Mäts där geometrin står
//       på HELA pixlar (1,0 och 1,5 med fixturens mått, prövat i grinden).
//       På en bråkdelsposition är hörnet kantutjämnat, och "slutet" hade
//       blivit ett tröskelval — men det mäter P1 för sidlinjen ändå.
//   P3  PRIMITIVEN SJÄLV (`Draw::frameGap`): en lucka som går FÖRBI ramens
//       högra hörn tar inte sidlinjen — med raka hörn och med gruppramens
//       radie. Flikkontrollens lucka är flikens insida och når därför aldrig
//       ända ut på hela pixlar; utan P3 hade primitivens kapning kunnat
//       försvinna utan att någon mätning märkte det.
//
// SABOTAGEN den fäller: primitiven
// utan kapningen (P3, 36 fel — P1 står, för flikens insida når aldrig
// linjen), flikkontrollens lucka över HELA fliken (P2, 20 fel), och båda
// tillsammans — koden utan sidlinjens fix (P1 12 fel i Slate och Glaze, P2, P3).
//
// INTE MÄTT här: kantutjämningens kvalitet, flikens rundade övre hörn och
// gruppramens lucka i en riktig gruppram (luckan når aldrig ett hörn där —
// rubriken är indragen; groupbox_gate och pixel_gate ser att den ritar).

namespace pixlar {

constexpr int kTol = 6;   // pixel_gates "syns"-tröskel, samma skäl

struct Bild {
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;
    int                       kanal(int x, int y, int k) const
    {
        return px[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4 +
                  std::size_t(k)];
    }
};

// Största kanalskillnaden mellan två pixlar.
int diff(const Bild& b, int x1, int y1, int x2, int y2)
{
    int d = 0;
    for (int k = 0; k < 3; ++k)
        d = std::max(d, std::abs(b.kanal(x1, y1, k) - b.kanal(x2, y2, k)));
    return d;
}

// Ett fysiskt mått som står på en HEL pixel (inom flyttalsbruset).
bool hel(float v) { return std::abs(v - std::round(v)) < 0.01f; }

// P3 — en ram ritad direkt med primitiven, vars lucka börjar mitt i
// överkanten och slutar en hel bredd FÖRBI det högra hörnet.
struct LuckRam : glaze::Widget {
    float radie = 0.0f;

protected:
    void draw(const glaze::Ctx& c) override
    {
        const gfx::Rectf b = c.bounds();
        glaze::Draw::rect(c, b, radie, c.theme->colors.panel);
        glaze::Draw::frameGap(c, b, radie,
                              glaze::Pen{c.theme->colors.border,
                                         c.style->borderWidth},
                              b.w / 2.0f, b.w);
    }
};

// Ramens högra kant hel mellan raderna `topp` och `botten` (fysiska): varje
// rad ser ut som raden `botten`, och där syns linjen mot ytan. −1 = linjen
// syns inte ens där; annars antalet rader som saknar den. Anroparen håller
// båda raderna utanför hörnbågarna.
int saknadeRader(const Bild& b, const gfx::Rectf& r, int topp, int botten)
{
    const int kol = int(std::floor(r.x + r.w - 0.5f));
    if (kol < 3 || kol >= b.w || botten >= b.h || topp >= botten)
        return -1;
    if (diff(b, kol, botten, kol - 3, botten) <= kTol)
        return -1;
    int fel = 0;
    for (int y = topp; y < botten; ++y)
        if (diff(b, kol, y, kol, botten) > kTol)
            ++fel;
    return fel;
}

struct Fonster : glaze::AppWindow {
    glaze::TabControl full;   // raden full, den SISTA vald ⇒ högra hörnet
    glaze::TabControl mitt;   // raden ryms, den MITTERSTA vald ⇒ två hörn
    glaze::TabPage    f[6];
    glaze::TabPage    m[3];
    LuckRam           rak;    // P3, raka hörn
    LuckRam           rund;   // P3, gruppramens radie

    Fonster() : glaze::AppWindow("tab_gate pixlar", 224, 300, gfx::Chrome::None)
    {
        rak.x       = 8;
        rak.y       = 236;
        rak.width   = 96;
        rak.height  = 56;
        rund.x      = 112;
        rund.y      = 236;
        rund.width  = 96;
        rund.height = 56;
        rund.radie  = 6.0f;
        root().adopt(rak);
        root().adopt(rund);
        const char* rubrik[] = {"Allmänt", "Avancerat", "Om programmet",
                                "Fjärde",  "Femte",     "Sjätte"};
        full.x      = 8;
        full.y      = 8;
        full.width  = 200;
        full.height = 100;
        for (int i = 0; i < 6; ++i) {
            f[i].setText(rubrik[i]);
            full.adopt(f[i]);
        }
        full.setSelectedIndex(5);
        // EN bokstav per rubrik: flikarna står då på `tabMinW`, och deras
        // kanter hamnar på hela pixlar vid 1,0 och 1,5 (P2).
        mitt.x      = 8;
        mitt.y      = 124;
        mitt.width  = 200;
        mitt.height = 100;
        for (int i = 0; i < 3; ++i) {
            m[i].setText(std::string(1, char('A' + i)));
            mitt.adopt(m[i]);
        }
        mitt.setSelectedIndex(1);
        root().adopt(full);
        root().adopt(mitt);
    }
};

void mät(Fonster& w, const std::string& namn)
{
    Bild b;
    w.invalidate();
    w.window().renderFrame();
    if (!w.window().captureFrame(b.px, b.w, b.h) || b.w <= 0 || b.h <= 0) {
        check(false, namn + ": captureFrame misslyckades");
        return;
    }
    const glaze::Style& st = w.style();
    const float         s  = w.window().uiScale();

    // ---- P1. Ramens högra kant under flikraden. Linjens kolumn är den som
    //          rymmer linjens MITT (en hårlinje innanför kanten).
    const bool full = 6.0f * (st.tabMinW + st.tabGap) > w.full.width;
    check(full, namn + " P1: fixturens rad är full (den valda står i hörnet)");
    {
        const gfx::Rectf r = w.full.resolve(w.full.behaviorCtx());
        const int fel = saknadeRader(b, r, int(std::ceil(r.y + st.tabHeaderH * s)) + 2,
                                     int(std::floor(r.y + r.h)) - 3);
        check(fel == 0,
              namn + " P1: sidlinjen hel när den valda fliken står i ramens "
                     "högra hörn",
              fel < 0 ? std::string("linjen syns inte ens längst ned")
                      : std::to_string(fel) + " rader saknar linjen");
    }

    // ---- P3. Primitiven: luckan förbi högra hörnet, raka och rundade hörn.
    for (LuckRam* lr : {&w.rak, &w.rund}) {
        const gfx::Rectf r   = lr->resolve(lr->behaviorCtx());
        const float      båge = lr->radie * s;
        const int        fel  = saknadeRader(b, r, int(std::ceil(r.y + båge)) + 2,
                                             int(std::floor(r.y + r.h - båge)) - 3);
        check(fel == 0,
              namn + " P3: frameGap — en lucka förbi hörnet tar inte sidlinjen" +
                  (lr->radie > 0.0f ? " (rundade hörn)" : " (raka hörn)"),
              fel < 0 ? std::string("linjen syns inte ens längst ned")
                      : std::to_string(fel) + " rader saknar linjen");
    }

    // ---- P2. Hörnen vid den valda (mittersta) fliken — förväntat värde ur
    //          KONTRAKTET: flikarna står på `tabMinW` med `tabGap` före var.
    if (w.ui() == glaze::UI::Minimalistic)
        return;
    const gfx::Rectf r  = w.mitt.resolve(w.mitt.behaviorCtx());
    const float      tx = r.x + (2.0f * st.tabGap + st.tabMinW) * s;   // flik 1
    const float      tr = tx + st.tabMinW * s;
    const float      fy = r.y + st.tabHeaderH * s;
    if (!hel(tx) || !hel(tr) || !hel(fy))
        return;   // en bråkdelsposition — se filhuvudet
    const int vx = int(std::round(tx));
    const int hx = int(std::round(tr)) - 1;
    const int y  = int(std::round(fy));
    if (vx < 3 || hx + 3 >= b.w || y + 3 >= b.h)
        return;
    check(diff(b, vx - 3, y, vx + 3, y + 3) > kTol,
          namn + " P2: ramens överkant syns mot ytan");
    check(diff(b, vx, y, vx - 3, y) <= kTol,
          namn + " P2: VÄNSTRA hörnet slutet — flikens sidlinje möter "
                 "ramens överkant",
          std::to_string(diff(b, vx, y, vx - 3, y)));
    check(diff(b, hx, y, hx + 3, y) <= kTol,
          namn + " P2: HÖGRA hörnet slutet",
          std::to_string(diff(b, hx, y, hx + 3, y)));
    ++g_hörn;
}

} // namespace pixlar

void fallPixlar()
{
    using pixlar::Fonster;
    // OS-skalan ur ett PROVFÖNSTER — användarskalan är 1 när grinden börjar.
    float os = 0.0f;
    {
        Fonster p;
        p.setOffscreen(true);
        p.show();
        if (!p.isOpen()) {
            std::printf("  --   pixelfallen hoppades över: inget fönster kunde "
                        "skapas (ingen display)\n");
            ++g_skips;
            return;
        }
        os = p.window().uiScale() / gfx::userScale();
    }
    const glaze::UI ui[]   = {glaze::UI::Slate, glaze::UI::Glaze,
                              glaze::UI::Minimalistic};
    const char*     uiN[]  = {"Slate", "Glaze", "Minimalistic"};
    const float     skala[] = {1.0f, 1.25f, 1.5f};
    for (int u = 0; u < 3; ++u)
        for (float mål : skala) {
            gfx::setUserScale(mål / os);   // inga fönster öppna — glaze-renders väg
            Fonster w;
            w.setUI(ui[u]);
            w.setOffscreen(true);
            w.show();
            if (!w.isOpen()) {
                check(false, std::string(uiN[u]) + ": fönstret kom inte upp");
                continue;
            }
            for (const glaze::ThemeMode läge :
                 {glaze::ThemeMode::Light, glaze::ThemeMode::Dark}) {
                w.setThemeMode(läge);
                char namn[64];
                std::snprintf(namn, sizeof namn, "%s %s ×%.2f", uiN[u],
                              läge == glaze::ThemeMode::Light ? "ljust"
                                                              : "mörkt",
                              double(w.window().uiScale()));
                pixlar::mät(w, namn);
            }
        }
    gfx::setUserScale(1.0f);
    check(g_hörn >= 8,
          "P2: hörnen mättes på hela pixlar i minst 8 fall (1,0 och 1,5 × "
          "Slate/Glaze × ljust/mörkt)",
          std::to_string(g_hörn));
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("tab_gate\n");
    fallRegistret();
    fallSidornaOchValet();
    fallOrdningen();
    fallBorttagningen();
    fallInteEnSida();
    fallSidornasVokabular();
    fallRubriken();
    fallRitningen();
    fallMedFonster();
    fallPixlar();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
