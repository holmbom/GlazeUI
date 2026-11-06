// propertygrid_gate — EGENSKAPSLISTANS TYPADE CELLER.
//
// Kör:  ./build/tests/propertygrid_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display, eller färgpopupen kom aldrig upp) — ctest
//       räknar det som SKIPPED, aldrig grönt
//
// TVÅDELAD:
//
//   HEADLESS     läsvägarna (PG1), höjden ur raderna i skala 1 och 2 (PG2) och
//                för en inaktiv grid (PG10, infälld),
//                tooltipen per rad (PG4) och sättarna (PG5) — mot ett Ctx utan
//                renderer, som measure_gate, och en falsk font.
//   MED FÖNSTER  ett DOLT fönster (`setOffscreen`), händelserna genom fönstrets
//                RIKTIGA ingång (`AppWindow::handleEvent`) och stegen som en
//                KEDJA AV POSTADE STEG (commit_gate-prejudikatet: fokusförlustens
//                event och popupens stängning verkställs i pump-svansen):
//                staplade gridar i en Panel (PG3), kryssrutan (PG6), talet
//                (PG7), färgen (PG8) och fällning/inaktivering under en
//                redigering (PG9).
//
// SABOTAGEN den fäller (en konkret kodändring per fall):
//   * PG1  `isChecked` som läser `value == "true"` i stället för
//          `parseBool` · `colorValue` som inte gör färgen opak utan
//          `allowAlpha` · `numberValue` utan klampningen, utan avrundningen
//          eller utan −0-vakten i `numbertext::effective` · `hasError` som
//          bara kollar `pending_`
//   * PG2  `contentSize` som räknar alla rader i stället för de synliga ·
//          utan `2 × panelInset` med ram · som låter `height` förlora mot
//          raderna · `rebuildVisible` som räknar raderna före den första
//          rubriken som dolda
//   * PG10 `rebuildVisible` som inte läser `isEnabled()` (den inaktiva
//          gridden står utfälld) · `enabledChanged` som inte räknar om de
//          synliga raderna · en inaktivering som skriver `expanded` (den
//          öppnas inte som förut)
//   * PG3  `setExpanded` utan `invalidate()` (mätcachen står kvar) ·
//          `contentSize` som är 0×0 (den andra gridden hamnar på den första)
//   * PG4  `toolTipAt` som låter radens tooltip vinna över felet · som
//          hoppar över rubrikraden · som aldrig faller tillbaka på gridens
//   * PG5  `setRow` utan intervallkontroll · `setExpanded` på en vanlig rad
//          som ändrar något · `addRow` som inte räknar upp `rowGen_`/
//          `pending_` (parallellvektorerna glider isär)
//   * PG6  `mouseDown` som inte växlar värdet före `openEditor` · eventen
//          fyrade före editorn · `CheckEditor::keyDown` som inte konsumerar
//          Enter/Escape (dialogens knapp klickas) · `Space` som inte
//          växlar · en skrivskyddad eller inaktiv rad som växlar ändå · dolda
//          editorer som är tabbstopp (`collectFocusable`)
//   * PG7  `endEdit` som sparar utan klampning · som jämför texten i stället för
//          det effektiva talet (12,50 fyrar) · som sparar en otolkbar text
//          · `TextEditor::textInput` utan taltecken-filtret · Escape som inte
//          tömmer `pending_` · `setRow` som inte tömmer `pending_`
//   * PG8  `commitColor` utan generationsvakten · utan jämförelsen med
//          den effektiva färgen · `mouseDown` utan tryckregeln (ett nytt
//          editorval hinner före den postade commiten)
//   * PG9  `mouseDown` som fäller rubriken före `endEdit` (redigeringen sparas
//          inte) · `setExpanded` som inte stänger en dold rads editor ·
//          `setRowEnabled(false)` som inte stänger editorn eller inte stegar
//          `rowGen_` (den köade sparningen fyras ändå)
//
// VAD DEN INTE MÄTER: utseendet — att varje slag RITAS i varje UI × tema mäter
// `pixel_gate` (galleri-fixturen) — och färgpopupens utseende (colorpicker_gate).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <gfx/null_renderer.h>
#include <gfx/platform.h>

#include <glaze/glaze.h>

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
    std::fflush(stdout);
    ++g_fails;
}

bool nära(float a, float b) { return (a > b ? a - b : b - a) < 0.5f; }

using Grid = glaze::PropertyGrid;
using Row  = glaze::PropertyGrid::Row;
using K    = glaze::PropertyGrid::Kind;
using Lista = std::vector<std::string>;

std::string text(const Lista& v)
{
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i)
        s += (i ? ", " : "") + v[i];
    return s + "]";
}

bool radLika(const Row& a, const Row& b)
{
    return a.name == b.name && a.value == b.value && a.choices == b.choices &&
           a.readOnly == b.readOnly && a.tag == b.tag && a.kind == b.kind &&
           a.min == b.min && a.max == b.max && a.decimals == b.decimals &&
           a.prefix == b.prefix && a.suffix == b.suffix &&
           a.allowAlpha == b.allowAlpha && a.toolTip == b.toolTip &&
           a.enabled == b.enabled && a.expanded == b.expanded;
}

bool listaLika(const std::vector<Row>& a, const std::vector<Row>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!radLika(a[i], b[i]))
            return false;
    return true;
}

// ------------------------------------------------------------ falsk font
class FalskFont final : public gfx::NullFont {
public:
    FalskFont(float adv, float h) : adv_(adv), h_(h) {}
    gfx::Sizef measure(const std::string& s) const override
    {
        return gfx::Sizef{adv_ * float(s.size()), h_};
    }
    float height() const override { return h_; }
    bool  valid() const override { return true; }

private:
    float adv_;
    float h_;
};

// `Widget::measure` är en statisk protected basmedlem — se measure_gate.
struct Meter : glaze::Widget {
    using glaze::Widget::measure;
};

glaze::Ctx ctxFör(const glaze::Style& st, gfx::Font& f, float scale)
{
    glaze::Ctx c{};
    c.font        = &f;
    c.headingFont = nullptr;
    c.scale       = scale;
    c.ui          = glaze::UI::Slate;
    c.style       = &st;
    c.theme       = &glaze::Theme::slateDefault().light;
    c.area        = gfx::Rectf{0.0f, 0.0f, 600.0f * scale, 400.0f * scale};
    return c;
}

// ================================================================ HEADLESS

// PG1. LÄSVÄGARNA — typade ur `value`.
void fallLasvagar()
{
    Grid g;
    g.setRows({
        {.name = "c0", .value = "true", .kind = K::Check},                  // 0
        {.name = "c1", .value = "1", .kind = K::Check},                     // 1
        {.name = "c2", .value = "skräp", .kind = K::Check},                 // 2
        {.name = "c3", .value = "false", .kind = K::Check},                 // 3
        {.name = "t", .value = "true"},                                     // 4 text
        {.name = "f0", .value = "#3366FF", .kind = K::Color},               // 5
        {.name = "f1", .value = "#3366FF80", .kind = K::Color},             // 6 opak
        {.name = "f2", .value = "#3366FF80", .kind = K::Color,
         .allowAlpha = true},                                               // 7
        {.name = "f3", .value = "#3366ff", .kind = K::Color,
         .allowAlpha = true},                                               // 8
        {.name = "f4", .value = "skräp", .kind = K::Color},                 // 9
        {.name = "f5", .value = "#12345", .kind = K::Color},                // 10
        {.name = "n0", .value = "12.5", .kind = K::Number, .min = 0,
         .max = 10},                                                        // 11
        {.name = "n1", .value = "1.26", .kind = K::Number, .decimals = 1},  // 12
        {.name = "n2", .value = "-0", .kind = K::Number},                   // 13
        {.name = "n3", .value = "-0.04", .kind = K::Number, .decimals = 1}, // 14
        {.name = "n4", .value = "abc", .kind = K::Number},                  // 15
        {.name = "n5", .value = "1,5", .kind = K::Number},                  // 16
        {.name = "n6", .value = "1e3", .kind = K::Number},                  // 17
        {.name = "n7", .value = "5", .kind = K::Number},                    // 18
        {.name = "n8", .value = "-5", .kind = K::Number, .min = 0},         // 19
    });

    check(g.isChecked(0) && g.isChecked(1) && !g.isChecked(2) &&
              !g.isChecked(3),
          "PG1: isChecked följer parseBool — true och 1 är på, skräp och false "
          "av");
    check(!g.isChecked(4) && !g.isChecked(-1) && !g.isChecked(99),
          "PG1: isChecked på en annan sorts rad och utanför intervallet är "
          "false");

    const auto f0 = g.colorValue(5);
    check(f0 && f0->r == 0x33 && f0->g == 0x66 && f0->b == 0xFF && f0->a == 255,
          "PG1: colorValue, sex siffror — opak");
    const auto f1 = g.colorValue(6);
    check(f1 && f1->a == 255,
          "PG1: colorValue, åtta siffror utan allowAlpha — alfan läses opak",
          f1 ? std::to_string(int(f1->a)) : "nullopt");
    const auto f2 = g.colorValue(7);
    check(f2 && f2->a == 0x80 && f2->b == 0xFF,
          "PG1: colorValue, åtta siffror med allowAlpha — alfan bevaras",
          f2 ? std::to_string(int(f2->a)) : "nullopt");
    const auto f3 = g.colorValue(8);
    check(f3 && f3->a == 255 && f3->g == 0x66,
          "PG1: colorValue, sex siffror med allowAlpha är opak, gemener tas");
    check(!g.colorValue(9) && !g.colorValue(10) && !g.colorValue(0) &&
              !g.colorValue(77),
          "PG1: colorValue, skräp, fel antal siffror, en annan sorts rad och "
          "utanför ⇒ nullopt");

    const auto n0 = g.numberValue(11);
    check(n0 && *n0 == 10.0, "PG1: numberValue klampar mot max",
          n0 ? std::to_string(*n0) : "nullopt");
    const auto n1 = g.numberValue(12);
    check(n1 && std::fabs(*n1 - 1.3) < 1e-9,
          "PG1: numberValue avrundar till decimals",
          n1 ? std::to_string(*n1) : "nullopt");
    const auto n2 = g.numberValue(13);
    check(n2 && *n2 == 0.0 && !std::signbit(*n2),
          "PG1: numberValue, −0 blir 0 (aldrig −0)");
    const auto n3 = g.numberValue(14);
    check(n3 && *n3 == 0.0 && !std::signbit(*n3),
          "PG1: numberValue, −0,04 med en decimal avrundas till 0, inte −0");
    check(!g.numberValue(15) && !g.numberValue(17) && !g.numberValue(4),
          "PG1: numberValue, text, exponent och en annan sorts rad ⇒ nullopt");
    const auto n5 = g.numberValue(16);
    check(n5 && *n5 == 1.5, "PG1: numberValue tar komma som decimaltecken");
    const auto n8 = g.numberValue(19);
    check(n8 && *n8 == 0.0 && !std::signbit(*n8),
          "PG1: numberValue klampar mot min");

    check(g.hasError(9) && g.hasError(10) && g.hasError(15) && g.hasError(17),
          "PG1: hasError för appens otolkbara färg- och talvärden");
    check(!g.hasError(5) && !g.hasError(11) && !g.hasError(18) &&
              !g.hasError(2) && !g.hasError(4) && !g.hasError(99),
          "PG1: hasError är false för giltiga värden, kryssrutor, text och "
          "utanför");
}

// PG2. HÖJDEN ur raderna — skala 1 och 2, samma svar i LOGISKA px.
void fallHojd()
{
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    const float         rad = st.gridRowH;

    for (const float skala : {1.0f, 2.0f}) {
        FalskFont f(7.0f * skala, st.uiFontSize * skala);
        const glaze::Ctx c = ctxFör(st, f, skala);
        const std::string s = skala == 1.0f ? "skala 1" : "skala 2";

        Grid g;
        g.setRows({{"A", "1"}, {"B", "2"}, {"C", "3"}});
        gfx::Sizef m = Meter::measure(g, c);
        check(nära(m.h, 3.0f * rad) && m.w == 0.0f,
              "PG2: tre rader ⇒ tre radhöjder, bredden är gridens (" + s + ")",
              std::to_string(m.h) + " mot " + std::to_string(3.0f * rad));

        g.setBorder(true);
        m = Meter::measure(g, c);
        check(nära(m.h, 3.0f * rad + 2.0f * st.panelInset),
              "PG2: med ram + 2 × panelInset (" + s + ")", std::to_string(m.h));
        g.setBorder(false);

        g.height = 44.0f;
        m        = Meter::measure(g, c);
        check(nära(m.h, 44.0f), "PG2: en satt höjd vinner över raderna (" + s + ")",
              std::to_string(m.h));
        g.height = 0.0f;

        // Rader före den första rubriken räknas alltid; raderna under en
        // infälld rubrik inte; rubrikraden själv alltid.
        g.setRows({{"A", "1"},
                   {.name = "H", .kind = K::Header, .expanded = false},
                   {"B", "2"},
                   {"C", "3"}});
        m = Meter::measure(g, c);
        check(nära(m.h, 2.0f * rad),
              "PG2: A före rubriken + rubriken, B och C dolda (" + s + ")",
              std::to_string(m.h));
        g.setExpanded(1, true);
        m = Meter::measure(g, c);
        check(nära(m.h, 4.0f * rad),
              "PG2: setExpanded(true) ⇒ alla fyra räknas (" + s + ")",
              std::to_string(m.h));
        g.setExpanded(1, false);
        m = Meter::measure(g, c);
        check(nära(m.h, 2.0f * rad),
              "PG2: setExpanded(false) krymper igen — mätcachen följer (" + s +
                  ")",
              std::to_string(m.h));

        // En andra rubrik börjar en ny grupp.
        g.setRows({{.name = "H1", .kind = K::Header, .expanded = false},
                   {"A", "1"},
                   {.name = "H2", .kind = K::Header},
                   {"B", "2"}});
        m = Meter::measure(g, c);
        check(nära(m.h, 3.0f * rad),
              "PG2: H1 infälld (A dold), H2 utfälld (B syns) ⇒ tre rader (" + s +
                  ")",
              std::to_string(m.h));

        Grid tom;
        m = Meter::measure(tom, c);
        check(m.w == 0.0f && m.h == 0.0f, "PG2: en tom grid är 0×0 (" + s + ")");

        // PG10 EN INAKTIV GRID visas infälld: raderna före den första rubriken
        // och rubrikerna, inget under dem — och radernas `expanded` står kvar,
        // så den öppnas som förut när den aktiveras.
        g.setRows({{"A", "1"},
                   {.name = "H1", .kind = K::Header},
                   {"B", "2"},
                   {.name = "H2", .kind = K::Header},
                   {"C", "3"}});
        m = Meter::measure(g, c);
        check(nära(m.h, 5.0f * rad), "PG10: aktiv — alla fem rader (" + s + ")",
              std::to_string(m.h));
        g.disable();
        m = Meter::measure(g, c);
        check(nära(m.h, 3.0f * rad) && g.rows()[1].expanded && g.rows()[3].expanded,
              "PG10: inaktiv — A och de två rubrikerna, rubrikernas `expanded` "
              "orörda (" + s + ")",
              std::to_string(m.h));
        g.enable();
        m = Meter::measure(g, c);
        check(nära(m.h, 5.0f * rad), "PG10: aktiv igen — som förut (" + s + ")",
              std::to_string(m.h));
    }
}

// PG4. TOOLTIPEN per rad — felet först, sedan radens, sedan gridens.
void fallToolTip()
{
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);
    const glaze::Ctx    c = ctxFör(st, f, 1.0f);

    Grid g;
    g.width  = 300.0f;
    g.height = 200.0f;
    g.setToolTip("Gridens");
    g.setRows({
        {.name = "A", .value = "1", .toolTip = "Radens"},                    // 0
        {.name = "B", .value = "2"},                                         // 1
        {.name = "C", .value = "x", .kind = K::Number, .toolTip = "Radens"}, // 2
        {.name = "H", .kind = K::Header, .toolTip = "Rubrikens"},            // 3
        {.name = "D", .value = "skräp", .kind = K::Color},                   // 4
    });
    const float rad = st.gridRowH;
    auto        vid = [&](int i) {
        return g.toolTipAt(c, 40.0f, (float(i) + 0.5f) * rad);
    };

    const glaze::TrText t0 = vid(0);
    check(t0.id() == 0 && t0.str() == "Radens",
          "PG4: radens egen tooltip vinner över gridens", t0.str());
    const glaze::TrText t1 = vid(1);
    check(t1.str() == "Gridens",
          "PG4: en rad utan tooltip faller tillbaka på gridens", t1.str());
    const glaze::TrText t2 = vid(2);
    check(t2.id() == glaze::str::PropertyGridInvalid,
          "PG4: felet (token 430) vinner över radens egen tooltip",
          std::to_string(t2.id()));
    const glaze::TrText t3 = vid(3);
    check(t3.str() == "Rubrikens", "PG4: rubrikradens tooltip", t3.str());
    const glaze::TrText t4 = vid(4);
    check(t4.id() == glaze::str::PropertyGridInvalid,
          "PG4: en färgrad med otolkbart värde bär felets tooltip också utan "
          "egen text", std::to_string(t4.id()));
    const glaze::TrText t5 = g.toolTipAt(c, 40.0f, 5.0f * rad + 3.0f);
    check(t5.str() == "Gridens",
          "PG4: utanför raderna svarar gridens egen", t5.str());

    // En dold rad (under en infälld rubrik) har ingen plats att peka på.
    g.setRows({{.name = "H", .kind = K::Header, .expanded = false},
               {.name = "Dold", .value = "1", .toolTip = "Dolda"}});
    const glaze::TrText t6 = g.toolTipAt(c, 40.0f, 1.5f * rad);
    check(t6.str() == "Gridens",
          "PG4: en rad under en infälld rubrik syns inte — gridens tooltip",
          t6.str());
}

// PG5. SÄTTARNA.
void fallSattare()
{
    Grid g;
    g.setRows({{"A", "1"},
               {.name = "H", .kind = K::Header},
               {.name = "K", .value = "true", .kind = K::Check}});
    const std::vector<Row> före = g.rows();

    g.setRow(3, {"X", "9"});
    g.setRow(-1, {"X", "9"});
    g.setRowEnabled(3, false);
    g.setRowEnabled(-1, false);
    g.setExpanded(3, false);
    g.setExpanded(-1, false);
    check(listaLika(g.rows(), före),
          "PG5: index utanför intervallet ⇒ logg + no-op, raderna orörda");

    g.setExpanded(0, false);
    g.setExpanded(2, false);
    check(listaLika(g.rows(), före),
          "PG5: setExpanded på en rad som inte är en rubrik är en no-op");

    g.setExpanded(1, false);
    g.setRowEnabled(2, false);
    check(!g.rows()[1].expanded && !g.rows()[2].enabled && g.rows()[0].enabled &&
              g.rows()[1].enabled,
          "PG5: setExpanded och setRowEnabled ändrar bara sin rad");

    // addRow och setRow ger EXAKT samma rader som setRows.
    const std::vector<Row> mål{
        {.name = "Grupp", .kind = K::Header, .expanded = false},
        {.name = "Synlig", .value = "false", .kind = K::Check,
         .toolTip = "Visa"},
        {.name = "Bredd", .value = "12.5", .kind = K::Number, .min = 0,
         .max = 100, .decimals = 1, .prefix = "1:", .suffix = "mm",
         .enabled = false},
        {.name = "Färg", .value = "#3366FF80", .kind = K::Color,
         .allowAlpha = true},
    };
    Grid a;
    a.setRows(mål);
    Grid b;
    b.addRow({.name = "Grupp", .kind = K::Header});
    b.addRow({.name = "Synlig", .value = "true", .kind = K::Check});
    b.addRow({"Bredd", "12.5", {}, false, "", K::Number, 0, 100, 1, "1:", "mm"});
    b.addRow({.name = "Färg", .value = "#3366FF80", .kind = K::Color,
              .allowAlpha = true});
    b.setRow(1, {.name = "Synlig", .value = "false", .kind = K::Check,
                 .toolTip = "Visa"});
    b.setExpanded(0, false);
    b.setRowEnabled(2, false);
    check(listaLika(a.rows(), b.rows()),
          "PG5: addRow/setRow/setExpanded/setRowEnabled ger exakt raderna "
          "setRows ger");

    // setRow ERSÄTTER hela raden, också de typade fälten.
    b.setRow(2, {"Bredd", "3"});
    check(b.rows()[2].kind == K::Text && b.rows()[2].enabled &&
              b.rows()[2].suffix.empty() && b.rows()[2].max > 1e300,
          "PG5: setRow är tilldelning — slag, gränser och suffix nollas");
}

// ================================================================ MED FÖNSTER

struct Vard : glaze::AppWindow {
    glaze::Panel stapel;
    Grid         gridA, gridB;   // PG3
    Grid         grid;           // PG6–PG9
    glaze::Button    ok{"OK"};
    glaze::Button    avbryt{"Avbryt"};
    glaze::Button    flytt{"Flytt"};
    glaze::TextInput falt;

    Lista logg;
    int   okKlick     = 0;
    int   avbrytKlick = 0;

    Vard() : glaze::AppWindow("propertygrid_gate", 800, 600, gfx::Chrome::None)
    {
        const auto ställ = [](glaze::Widget& w, const char* namn, float x,
                              float y, float bredd = 0, float höjd = 0) {
            w.name   = namn;
            w.x      = x;
            w.y      = y;
            w.width  = bredd;
            w.height = höjd;
        };
        ställ(ok, "ok", 10, 10);
        ställ(avbryt, "avbryt", 100, 10);
        ställ(flytt, "flytt", 190, 10);
        ställ(falt, "falt", 280, 10, 120);
        ställ(stapel, "stapel", 450, 10, 300, 300);
        ställ(grid, "grid", 10, 60, 300, 260);

        gridA.dock = glaze::Dock::Top;
        gridB.dock = glaze::Dock::Top;
        gridA.name = "gridA";
        gridB.name = "gridB";
        gridA.setRows({{.name = "H", .kind = K::Header}, {"A1", "1"}, {"A2", "2"}});
        gridB.setRows({{"B1", "1"}, {"B2", "2"}, {"B3", "3"}});

        grid.setNameWidth(80);
        grid.setRows({
            {.name = "Grupp", .kind = K::Header},                              // 0
            {.name = "Synlig", .value = "true", .kind = K::Check},             // 1
            {.name = "Skala", .value = "10", .kind = K::Number, .min = 0,
             .max = 100},                                                      // 2
            {.name = "Procent", .value = "50", .kind = K::Number, .min = 0,
             .max = 100, .decimals = 0, .suffix = "%"},                        // 3
            {.name = "Färg", .value = "#3366FF", .kind = K::Color},            // 4
            {.name = "Alfa", .value = "#3366FF80", .kind = K::Color,
             .allowAlpha = true},                                              // 5
            {.name = "Namn", .value = "a"},                                    // 6
            {.name = "Fast", .value = "true", .readOnly = true,
             .kind = K::Check},                                                // 7
            {.name = "Av", .value = "true", .kind = K::Check,
             .enabled = false},                                                // 8
        });
        grid.onChange = [this] {
            logg.push_back("change " + std::to_string(grid.changedRow()));
        };
        grid.onCommit = [this] {
            logg.push_back("commit " + std::to_string(grid.changedRow()));
        };
        ok.onClick     = [this] { ++okKlick; };
        avbryt.onClick = [this] { ++avbrytKlick; };

        root().adopt(ok);
        root().adopt(avbryt);
        root().adopt(flytt);
        root().adopt(falt);
        root().adopt(stapel);
        stapel.adopt(gridA);
        stapel.adopt(gridB);
        root().adopt(grid);
        setAcceptButton(ok);
        setCancelButton(avbryt);
    }

    // ---- INGÅNGEN — syntetiska händelser genom `handleEvent`.
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
    void klick(glaze::Widget& w)
    {
        const gfx::Rectf r = w.resolve(w.behaviorCtx());
        klick(gfx::Pointf{r.x + r.w / 2.0f, r.y + r.h / 2.0f});
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
        for (const char* p = utf8; *p; ++p) {
            gfx::Event e;
            e.type     = gfx::EventType::TextInput;
            e.windowId = window().id();
            std::snprintf(e.text, sizeof e.text, "%c", *p);
            handleEvent(e);
        }
    }

    // Raden på PLATSEN `pos` bland de synliga: värdecellen och namncellen —
    // ur KONTRAKTET (ingen ram, `gridRowH` hög, värdekolumnen från
    // `nameWidth` = 80), aldrig ur widgetens egna rect-funktioner.
    gfx::Pointf värde(Grid& g, int pos)
    {
        const gfx::Rectf r = g.resolve(g.behaviorCtx());
        const float      s = window().uiScale();
        return gfx::Pointf{r.x + (80.0f + 40.0f) * s,
                           r.y + (float(pos) + 0.5f) * style().gridRowH * s};
    }
    gfx::Pointf namn(Grid& g, int pos)
    {
        const gfx::Rectf r = g.resolve(g.behaviorCtx());
        const float      s = window().uiScale();
        return gfx::Pointf{r.x + 20.0f * s,
                           r.y + (float(pos) + 0.5f) * style().gridRowH * s};
    }
    float radHöjd() { return style().gridRowH * window().uiScale(); }

    // Den SYNLIGA editorn i gridden (hög ett barn åt gången), och dess fält.
    glaze::Widget* editor()
    {
        for (glaze::Widget* b : grid.children())
            if (b->isVisible())
                return b;
        return nullptr;
    }
    int synliga()
    {
        int n = 0;
        for (glaze::Widget* b : grid.children())
            n += b->isVisible() ? 1 : 0;
        return n;
    }
    glaze::TextInput* fält() { return dynamic_cast<glaze::TextInput*>(editor()); }
    glaze::ColorPicker* färg()
    {
        return dynamic_cast<glaze::ColorPicker*>(editor());
    }
};

void fallMedFonster()
{
    glaze::setLanguage("sv");
    Vard w;
    w.setOffscreen(true);

    bool                  kordes = false;
    int                   steg   = 0;
    std::size_t           märke  = 0;
    bool                  popupFinns = true;
    std::string           radEtt;   // kryssrutans värde efter PG6
    std::function<void()> nästa;
    nästa = [&] {
        switch (steg++) {
        case 0: {
            kordes = true;
            if (!w.isOpen()) {
                std::printf("  --   fönsterfallen hoppas över: fönstret kom "
                            "aldrig upp\n");
                ++g_skips;
                w.close();
                return;
            }

            // ---- PG3. TVÅ GRIDAR dock=Top i en Panel — den andra står under
            //           den första, också efter en fällning.
            {
                const float rad = w.radHöjd();
                gfx::Rectf  a   = w.gridA.resolve(w.gridA.behaviorCtx());
                gfx::Rectf  b   = w.gridB.resolve(w.gridB.behaviorCtx());
                check(nära(a.h, 3.0f * rad) && nära(b.h, 3.0f * rad) &&
                          nära(b.y, a.y + a.h),
                      "PG3: två gridar dock=Top — höjden ur raderna och den "
                      "andra under den första",
                      std::to_string(a.h) + " / " + std::to_string(b.y - a.y));
                w.gridA.setExpanded(0, false);
                a = w.gridA.resolve(w.gridA.behaviorCtx());
                b = w.gridB.resolve(w.gridB.behaviorCtx());
                check(nära(a.h, 1.0f * rad) && nära(b.y, a.y + a.h),
                      "PG3: efter en fällning står den andra under den "
                      "första igen (mätcachen följer)",
                      std::to_string(a.h) + " / " + std::to_string(b.y - a.y));
                w.gridA.setExpanded(0, true);
                a = w.gridA.resolve(w.gridA.behaviorCtx());
                b = w.gridB.resolve(w.gridB.behaviorCtx());
                check(nära(a.h, 3.0f * rad) && nära(b.y, a.y + a.h),
                      "PG3: och efter en utfällning");
            }

            // ---- Tabbordningen: de dolda editorerna är inga tabbstopp.
            {
                bool gridBarn = false;
                for (glaze::Widget* f : w.focusOrder())
                    if (f->parent() == &w.grid)
                        gridBarn = true;
                check(!gridBarn && w.synliga() == 0,
                      "PG6: gridens dolda editorer är inga tabbstopp");
            }

            // Kontrollfallet: Enter i ett fält klickar fönstrets standardknapp —
            // grinden SER alltså ett klick när något släpper tangenten vidare.
            w.falt.focus();
            w.tangent(gfx::Key::Return);
            check(w.okKlick == 1,
                  "PG6: kontroll — Enter i ett textfält klickar standardknappen",
                  std::to_string(w.okKlick));
            w.okKlick = 0;
            w.flytt.focus();
            w.tangent(gfx::Key::Escape);
            check(w.avbrytKlick == 1,
                  "PG6: kontroll — Escape på en knapp klickar avbrytknappen",
                  std::to_string(w.avbrytKlick));
            w.avbrytKlick = 0;

            // ---- PG6. KRYSSRUTAN. Ett tryck växlar värdet, editorn tar fokus,
            //           och eventen kommer sist, en gång var.
            märke = w.logg.size();
            w.klick(w.värde(w.grid, 1));
            check(w.grid.rows()[1].value == "false" && !w.grid.isChecked(1) &&
                      w.grid.changedRow() == 1 && w.grid.activeRow() == 1,
                  "PG6: ett tryck växlar true → false, raden är aktiv",
                  w.grid.rows()[1].value);
            check(Lista(w.logg.begin() + long(märke), w.logg.end()) ==
                      Lista{"change 1", "commit 1"},
                  "PG6: onChange + onCommit en gång var, med raden",
                  text(Lista(w.logg.begin() + long(märke), w.logg.end())));
            check(w.synliga() == 1 && w.editor() && w.editor()->focused(),
                  "PG6: en fokuserad editor på cellen");

            // Space växlar igen (och fyrar).
            märke = w.logg.size();
            w.tangent(gfx::Key::Space);
            check(w.grid.rows()[1].value == "true" && w.grid.isChecked(1) &&
                      Lista(w.logg.begin() + long(märke), w.logg.end()) ==
                          Lista{"change 1", "commit 1"},
                  "PG6: Space växlar tillbaka — ett par till",
                  w.grid.rows()[1].value);

            // Enter stänger editorn och KONSUMERAS — standardknappen klickas inte.
            märke = w.logg.size();
            w.tangent(gfx::Key::Return);
            check(w.grid.activeRow() == -1 && w.synliga() == 0 &&
                      w.okKlick == 0 && w.logg.size() == märke &&
                      w.grid.rows()[1].value == "true",
                  "PG6: Enter stänger editorn utan ändring, utan event och utan "
                  "att dialogens standardknapp klickas",
                  std::to_string(w.okKlick));

            // Escape likadant.
            w.klick(w.värde(w.grid, 1));   // → "false"
            märke = w.logg.size();
            w.tangent(gfx::Key::Escape);
            check(w.grid.activeRow() == -1 && w.synliga() == 0 &&
                      w.avbrytKlick == 0 && w.logg.size() == märke &&
                      w.grid.rows()[1].value == "false",
                  "PG6: Escape stänger editorn och avbrytknappen klickas inte",
                  std::to_string(w.avbrytKlick));

            // Ett tryck på själva editorn växlar också.
            w.klick(w.värde(w.grid, 1));   // → "true" (editorn öppen)
            märke = w.logg.size();
            w.klick(w.värde(w.grid, 1));   // trycket tas av editorn → "false"
            check(w.grid.rows()[1].value == "false" &&
                      Lista(w.logg.begin() + long(märke), w.logg.end()) ==
                          Lista{"change 1", "commit 1"},
                  "PG6: ett tryck på den öppna editorn växlar — ett par",
                  w.grid.rows()[1].value);
            w.tangent(gfx::Key::Escape);

            // Skrivskyddad och inaktiv rad: inget tryck.
            märke = w.logg.size();
            w.klick(w.värde(w.grid, 7));
            check(w.grid.rows()[7].value == "true" && w.grid.activeRow() == -1 &&
                      w.logg.size() == märke && w.synliga() == 0,
                  "PG6: en skrivskyddad kryssruta växlar inte, ingen editor");
            w.klick(w.värde(w.grid, 8));
            check(w.grid.rows()[8].value == "true" && w.grid.activeRow() == -1 &&
                      w.logg.size() == märke && w.synliga() == 0,
                  "PG6: en inaktiv kryssruta växlar inte, ingen editor");
            radEtt = w.grid.rows()[1].value;
            break;
        }
        case 1: {
            // ---- PG7. TALET.
            märke = w.logg.size();
            w.klick(w.värde(w.grid, 2));
            glaze::TextInput* f = w.fält();
            check(f && w.grid.activeRow() == 2 && f->text() == "10",
                  "PG7: talraden öppnar fältet med visningsformen",
                  f ? f->text() : "ingen editor");
            w.skriv("12,5");
            check(f && f->text() == "12,5" && w.grid.rows()[2].value == "10",
                  "PG7: \"12,5\" skrivs, värdet orört tills Enter",
                  f ? f->text() : "");
            w.tangent(gfx::Key::Return);
            check(w.grid.rows()[2].value == "12.5" &&
                      w.grid.numberValue(2) == 12.5 &&
                      Lista(w.logg.begin() + long(märke), w.logg.end()) ==
                          Lista{"change 2", "commit 2"} &&
                      !w.grid.hasError(2),
                  "PG7: \"12,5\"+Enter ⇒ \"12.5\" (kanonisk form med punkt), ett "
                  "par", w.grid.rows()[2].value);

            // Bokstäver sväljs redan vid tangenten.
            w.klick(w.värde(w.grid, 2));
            f = w.fält();
            w.skriv("a");
            check(f && f->text() == "12,5",
                  "PG7: en bokstav sväljs av talfältet", f ? f->text() : "");
            w.skriv("7");
            w.tangent(gfx::Key::Escape);
            check(w.grid.rows()[2].value == "12.5" && w.grid.activeRow() == -1,
                  "PG7: Escape avbryter — värdet orört", w.grid.rows()[2].value);

            // Samma effektiva tal ("12,50" mot "12.5") fyrar inget.
            märke = w.logg.size();
            w.klick(w.värde(w.grid, 2));
            w.skriv("12,50");
            w.tangent(gfx::Key::Return);
            check(w.grid.rows()[2].value == "12.5" && w.logg.size() == märke,
                  "PG7: \"12,50\" mot det sparade \"12.5\" ⇒ inga events, värdet "
                  "orört", w.grid.rows()[2].value);

            // Klampning och avrundning.
            w.klick(w.värde(w.grid, 3));
            w.skriv("500");
            w.tangent(gfx::Key::Return);
            check(w.grid.rows()[3].value == "100" &&
                      Lista(w.logg.begin() + long(märke), w.logg.end()) ==
                          Lista{"change 3", "commit 3"},
                  "PG7: 500 med max 100 ⇒ \"100\" (klampat), ett par",
                  w.grid.rows()[3].value);
            märke = w.logg.size();
            w.klick(w.värde(w.grid, 3));
            w.skriv("42,6");
            w.tangent(gfx::Key::Return);
            check(w.grid.rows()[3].value == "43" &&
                      Lista(w.logg.begin() + long(märke), w.logg.end()) ==
                          Lista{"change 3", "commit 3"},
                  "PG7: 42,6 med noll decimaler ⇒ \"43\" (avrundat)",
                  w.grid.rows()[3].value);
            // Klampat till samma tal som redan står: inga events.
            märke = w.logg.size();
            w.klick(w.värde(w.grid, 3));
            w.skriv("43,2");
            w.tangent(gfx::Key::Return);
            check(w.logg.size() == märke && w.grid.rows()[3].value == "43",
                  "PG7: 43,2 avrundas till det sparade 43 ⇒ inga events");

            // OTOLKBAR text: inga events, värdet orört, felet visas.
            märke = w.logg.size();
            w.klick(w.värde(w.grid, 2));
            w.skriv("1,2.3");
            w.tangent(gfx::Key::Return);
            check(w.logg.size() == märke && w.grid.rows()[2].value == "12.5" &&
                      w.grid.hasError(2) && w.grid.activeRow() == -1,
                  "PG7: \"1,2.3\"+Enter ⇒ inga events, value orört, hasError",
                  w.grid.rows()[2].value);
            {
                const gfx::Pointf p = w.värde(w.grid, 2);
                const glaze::TrText t =
                    w.grid.toolTipAt(w.grid.behaviorCtx(), p.x, p.y);
                check(t.id() == glaze::str::PropertyGridInvalid,
                      "PG7: felets verktygstips (token 430)",
                      std::to_string(t.id()));
            }
            // Felet öppnar editorn med den skrivna texten, och Escape tar bort det.
            w.klick(w.värde(w.grid, 2));
            check(w.fält() && w.fält()->text() == "1,2.3",
                  "PG7: en felrad öppnar editorn med den skrivna texten",
                  w.fält() ? w.fält()->text() : "ingen editor");
            w.tangent(gfx::Key::Escape);
            check(!w.grid.hasError(2) && w.grid.rows()[2].value == "12.5" &&
                      w.logg.size() == märke,
                  "PG7: Escape tar bort felet");

            // Nästa giltiga sparning tar bort felet.
            w.klick(w.värde(w.grid, 2));
            w.skriv("1,2.3");
            w.tangent(gfx::Key::Return);
            check(w.grid.hasError(2), "PG7: felet står igen");
            w.klick(w.värde(w.grid, 2));
            w.skriv("20");
            w.tangent(gfx::Key::Return);
            check(!w.grid.hasError(2) && w.grid.rows()[2].value == "20",
                  "PG7: nästa giltiga sparning tar bort felet",
                  w.grid.rows()[2].value);

            // setRow tömmer felet.
            w.klick(w.värde(w.grid, 2));
            w.skriv("1,2.3");
            w.tangent(gfx::Key::Return);
            check(w.grid.hasError(2), "PG7: felet står en tredje gång");
            w.grid.setRow(2, w.grid.rows()[2]);
            check(!w.grid.hasError(2), "PG7: setRow på raden tömmer felet");
            w.klick(w.värde(w.grid, 2));
            w.skriv("1,2.3");
            w.tangent(gfx::Key::Return);
            w.grid.setRows(w.grid.rows());
            check(!w.grid.hasError(2), "PG7: setRows tömmer felet");
            break;
        }
        case 2: {
            // ---- PG8. FÄRGEN. Popupen är ett riktigt popupfönster.
            märke = w.logg.size();
            const std::string före = w.grid.rows()[4].value;
            w.klick(w.värde(w.grid, 4));
            glaze::ColorPicker* cp = w.färg();
            if (!cp || !cp->isOpen()) {
                std::printf("  --   färgfallen hoppas över: popupen kom aldrig "
                            "upp\n");
                ++g_skips;
                popupFinns = false;
                w.grid.setRows(w.grid.rows());
                break;
            }
            check(w.grid.activeRow() == 4 && cp->colorText() == "#3366FF",
                  "PG8: ett tryck öppnar färgväljaren med radens färg, popupen "
                  "öppen", cp->colorText());
            // Paletten: markören först, valet sedan (colorpicker_gate-formen).
            w.tangent(gfx::Key::Right);
            w.tangent(gfx::Key::Return);
            check(w.grid.rows()[4].value != före &&
                      w.grid.rows()[4].value == cp->colorText() &&
                      w.grid.rows()[4].value.size() == 7 &&
                      Lista(w.logg.begin() + long(märke), w.logg.end()) ==
                          Lista{"change 4", "commit 4"},
                  "PG8: Enter i paletten ⇒ value == colorText(), ett par",
                  w.grid.rows()[4].value + " / " +
                      text(Lista(w.logg.begin() + long(märke), w.logg.end())));
            break;
        }
        case 3: {
            if (!popupFinns) {
                steg = 6;   // vidare till PG9
                break;
            }
            glaze::ColorPicker* cp = w.färg();
            check(cp && !cp->isOpen(), "PG8: valet stängde popupen");
            // Alfa-raden: alfan bevaras vid ett val, texten är åtta siffror.
            märke = w.logg.size();
            w.klick(w.värde(w.grid, 5));
            cp = w.färg();
            if (!cp || !cp->isOpen()) {
                check(false, "PG8: alfa-radens popup öppnades inte");
                break;
            }
            w.tangent(gfx::Key::Right);
            w.tangent(gfx::Key::Return);
            check(w.grid.rows()[5].value.size() == 9 &&
                      w.grid.rows()[5].value.substr(7) == "80" &&
                      w.grid.rows()[5].value == cp->colorText() &&
                      Lista(w.logg.begin() + long(märke), w.logg.end()) ==
                          Lista{"change 5", "commit 5"},
                  "PG8: allowAlpha — åtta siffror, alfan 80 bevarad, ett par",
                  w.grid.rows()[5].value);
            break;
        }
        case 4: {
            // TRYCKREGELN: ett tryck i gridden medan popupen är öppen stänger
            // bara popupen — en ny editor öppnas inte.
            märke = w.logg.size();
            w.klick(w.värde(w.grid, 4));
            glaze::ColorPicker* cp = w.färg();
            check(cp && cp->isOpen(), "PG8: popupen öppen på raden");
            w.klick(w.värde(w.grid, 6));   // textraden under
            check(w.grid.activeRow() == -1 && w.synliga() == 0 &&
                      w.fält() == nullptr && w.logg.size() == märke,
                  "PG8: ett tryck i gridden med öppen popup stänger bara "
                  "popupen — ingen ny editor, inga events",
                  std::to_string(w.grid.activeRow()));
            break;
        }
        case 5: {
            // GENERATIONSVAKTEN: setRows medan popupen är öppen slopar en commit.
            märke = w.logg.size();
            const std::vector<Row> rader = w.grid.rows();
            w.klick(w.värde(w.grid, 4));
            glaze::ColorPicker* cp = w.färg();
            if (!cp || !cp->isOpen()) {
                check(false, "PG8: popupen öppnades inte för vakten");
                break;
            }
            w.grid.setRows(rader);
            cp->setColor("#112233");   // en ny färg som commiten skulle skriva
            glaze::ChangeArgs e;
            cp->onCommit(cp, e);       // commiten kommer efter setRows
            check(w.grid.rows()[4].value == rader[4].value &&
                      w.logg.size() == märke,
                  "PG8: en commit efter setRows slopas av generationsvakten — "
                  "raden orörd, inga events", w.grid.rows()[4].value);
            break;
        }
        case 6: {
            // ---- PG9. FÄLLNING OCH INAKTIVERING UNDER REDIGERING.
            w.grid.setRows(w.grid.rows());   // nollställ efter PG8
            märke = w.logg.size();
            // En rubrik sparar FÖRST: den pågående redigeringen, sedan fällningen.
            w.klick(w.värde(w.grid, 6));
            check(w.fält() && w.grid.activeRow() == 6,
                  "PG9: textraden öppnad", std::to_string(w.grid.activeRow()));
            w.skriv("z");
            w.klick(w.namn(w.grid, 0));
            check(w.grid.rows()[6].value == "z" && !w.grid.rows()[0].expanded &&
                      w.grid.activeRow() == -1 &&
                      Lista(w.logg.begin() + long(märke), w.logg.end()) ==
                          Lista{"change 6", "commit 6"},
                  "PG9: ett tryck på rubriken sparar redigeringen först (ett "
                  "par) och fäller sedan in",
                  w.grid.rows()[6].value);
            // Raderna under den infällda rubriken har ingen plats: ett tryck där
            // rad 1 stod träffar ingenting.
            märke = w.logg.size();
            w.klick(w.värde(w.grid, 1));
            check(w.grid.rows()[1].value == radEtt && w.logg.size() == märke &&
                      w.grid.activeRow() == -1,
                  "PG9: en rad under en infälld rubrik går inte att träffa");
            // Ut igen: ett tryck på rubriken, ingen händelse.
            w.klick(w.namn(w.grid, 0));
            check(w.grid.rows()[0].expanded && w.logg.size() == märke,
                  "PG9: ett tryck på rubriken fäller ut igen, utan event");

            // setExpanded(false) under redigering: stängs UTAN att spara.
            w.klick(w.värde(w.grid, 6));
            w.skriv("q");
            w.grid.setExpanded(0, false);
            check(w.grid.activeRow() == -1 && w.synliga() == 0 &&
                      w.grid.rows()[6].value == "z" && w.logg.size() == märke,
                  "PG9: setExpanded(false) stänger den dolda radens editor utan "
                  "att spara", w.grid.rows()[6].value);
            w.grid.setExpanded(0, true);

            // setRowEnabled(false) under redigering.
            w.klick(w.värde(w.grid, 6));
            w.skriv("w");
            w.grid.setRowEnabled(6, false);
            check(w.grid.activeRow() == -1 && w.synliga() == 0 &&
                      w.grid.rows()[6].value == "z" && w.logg.size() == märke,
                  "PG9: setRowEnabled(false) avbryter redigeringen utan att "
                  "spara", w.grid.rows()[6].value);
            // En inaktiv rad: inget tryck.
            w.klick(w.värde(w.grid, 6));
            check(w.grid.activeRow() == -1 && w.synliga() == 0,
                  "PG9: en inaktiv rad öppnar ingen editor");
            w.grid.setRowEnabled(6, true);

            // Den KÖADE sparningen: fokus bort skriver värdet direkt och postar
            // eventen; setRowEnabled(false) före svansen slopar dem.
            w.klick(w.värde(w.grid, 6));
            w.skriv("k");
            w.klick(w.flytt);
            check(w.grid.rows()[6].value == "k" && w.logg.size() == märke,
                  "PG9: fokus bort — värdet skrivet direkt, eventen köade",
                  w.grid.rows()[6].value);
            w.grid.setRowEnabled(6, false);
            break;
        }
        case 7: {
            check(w.logg.size() == märke,
                  "PG9: setRowEnabled(false) slopade den köade sparningen — "
                  "inga events i pump-svansen",
                  text(Lista(w.logg.begin() + long(märke), w.logg.end())));
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

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("propertygrid_gate — egenskapslistans typade celler\n");
    glaze::setLanguage("sv");
    fallLasvagar();
    fallHojd();
    fallToolTip();
    fallSattare();
    fallMedFonster();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
