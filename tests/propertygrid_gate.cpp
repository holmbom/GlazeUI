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
//                tooltipen per rad (PG4), sättarna (PG5) och det tomma talet
//                med kolumnernas sättare (PG11) — mot ett Ctx utan
//                renderer, som measure_gate, och en falsk font — och
//                färgcellens tre lägen (PG15) mot en inspelande renderer.
//   MED FÖNSTER  ett DOLT fönster (`setOffscreen`), händelserna genom fönstrets
//                RIKTIGA ingång (`AppWindow::handleEvent`) och stegen som en
//                KEDJA AV POSTADE STEG (commit_gate-prejudikatet: fokusförlustens
//                event och popupens stängning verkställs i pump-svansen):
//                staplade gridar i en Panel (PG3), kryssrutan (PG6), talet
//                (PG7), färgen (PG8), fällning/inaktivering under en
//                redigering (PG9) med användarens fällningshändelser (PG11),
//                kolumnlägena (PG12), det tomma talet i fältet (PG13),
//                vallistans ikoner (PG14), färgcellens AutoMin i de tre
//                lägena (PG16) och `selectOnEdit` (PG17).
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
//   * PG11 `toggleCategoryFromUser` som fyrar fel händelse eller ingen ·
//          `setExpanded` som fyrar `onExpand`/`onCollapse` · `isEmpty` som
//          ignorerar `allowEmpty` (en tom text i en rad utan blir inget fel,
//          eller ett tomt värde ett fel) · `setColumnWidth`/`setColumnSizing`
//          utan intervallkontroll
//   * PG12 `fittedW` som räknar kategoriraderna · `columnW` som läser
//          `colWidth_` för AutoMin · `valueBounds` som inte klipper kolumn 1
//          mot gridden (den rullar inte i sidled)
//   * PG13 `displayNumber` som visar appens råtext i stället för `emptyText`
//          · `editNumberText` som börjar med `emptyText` · `sameNumber` som
//          ser två tomma värden som olika (Enter fyrar)
//   * PG14 `openEditor` som fyller vallistan med `setItems` (ikonerna nollas)
//          · som behåller förra radens ikoner
//   * PG15 `drawColor` som ritar hextexten i Swatch · rutan i `checkSize` i
//          Swatch · rutan också i Hex
//   * PG16 `valueContentW` som inte skickar `colorDisplay` · `colorW` som
//          svarar samma bredd i två lägen
//   * PG17 `openEditor` som alltid markerar allt eller aldrig gör det ·
//          `setProp` utan `selectOnEdit`
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
           a.enabled == b.enabled && a.expanded == b.expanded &&
           a.allowEmpty == b.allowEmpty && a.emptyText == b.emptyText &&
           a.choiceIcons == b.choiceIcons && a.colorDisplay == b.colorDisplay;
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
                   {.name = "H", .kind = K::Category, .expanded = false},
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
        g.setRows({{.name = "H1", .kind = K::Category, .expanded = false},
                   {"A", "1"},
                   {.name = "H2", .kind = K::Category},
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
                   {.name = "H1", .kind = K::Category},
                   {"B", "2"},
                   {.name = "H2", .kind = K::Category},
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
        {.name = "H", .kind = K::Category, .toolTip = "Rubrikens"},            // 3
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
    check(t2.id() == glaze::str::InvalidValue,
          "PG4: felet (token 430) vinner över radens egen tooltip",
          std::to_string(t2.id()));
    const glaze::TrText t3 = vid(3);
    check(t3.str() == "Rubrikens", "PG4: rubrikradens tooltip", t3.str());
    const glaze::TrText t4 = vid(4);
    check(t4.id() == glaze::str::InvalidValue,
          "PG4: en färgrad med otolkbart värde bär felets tooltip också utan "
          "egen text", std::to_string(t4.id()));
    const glaze::TrText t5 = g.toolTipAt(c, 40.0f, 5.0f * rad + 3.0f);
    check(t5.str() == "Gridens",
          "PG4: utanför raderna svarar gridens egen", t5.str());

    // En dold rad (under en infälld rubrik) har ingen plats att peka på.
    g.setRows({{.name = "H", .kind = K::Category, .expanded = false},
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
               {.name = "H", .kind = K::Category},
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
        {.name = "Grupp", .kind = K::Category, .expanded = false},
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
    b.addRow({.name = "Grupp", .kind = K::Category});
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

// PG11. DET TOMMA TALET och KOLUMNERNAS sättare.
void fallTomtOchKolumner()
{
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);
    const glaze::Ctx    c = ctxFör(st, f, 1.0f);

    Grid g;
    g.width  = 300.0f;
    g.height = 200.0f;
    g.setRows({
        {.name = "Tom", .value = "", .kind = K::Number, .allowEmpty = true,
         .emptyText = "Auto"},                                               // 0
        {.name = "Blank", .value = "  ", .kind = K::Number,
         .allowEmpty = true},                                                // 1
        {.name = "Krav", .value = "", .kind = K::Number},                    // 2
        {.name = "Tal", .value = "3", .kind = K::Number, .allowEmpty = true}, // 3
    });
    check(!g.numberValue(0) && !g.hasError(0) && !g.numberValue(1) &&
              !g.hasError(1),
          "PG11: allowEmpty — en tom eller blank text är ett värde: "
          "numberValue nullopt och inget fel");
    check(g.hasError(2), "PG11: utan allowEmpty är en tom text ett fel");
    check(g.numberValue(3) == 3.0 && !g.hasError(3),
          "PG11: allowEmpty rör inte ett tal");
    const glaze::TrText t =
        g.toolTipAt(c, 40.0f, 0.5f * st.gridRowH);
    check(t.id() != glaze::str::InvalidValue,
          "PG11: ett tomt värde bär inte felets tooltip", std::to_string(t.id()));

    check(g.columnSizing(0) == glaze::ColumnSizing::Manual &&
              g.columnSizing(1) == glaze::ColumnSizing::Autosize &&
              g.columnWidth(0) == 0.0f && g.columnWidth(1) == 0.0f,
          "PG12: förvalen — namnet Manual, värdet Autosize, bredderna 0");
    g.setColumnWidth(0, 120.0f);
    g.setColumnSizing(1, glaze::ColumnSizing::AutoMin);
    g.setColumnWidth(2, 50.0f);
    g.setColumnWidth(0, -5.0f);
    g.setColumnSizing(-1, glaze::ColumnSizing::Autosize);
    g.setColumnSizing(2, glaze::ColumnSizing::Autosize);
    check(g.columnWidth(0) == 120.0f &&
              g.columnSizing(1) == glaze::ColumnSizing::AutoMin &&
              g.columnSizing(0) == glaze::ColumnSizing::Manual &&
              g.columnWidth(2) == 0.0f,
          "PG12: sättarna — kolumn utanför 0–1 och negativ bredd ⇒ no-op");
}

// Renderer som SPELAR IN fyllningarna och texterna — resten är
// NullRendererns no-op.
struct Inspelning : gfx::NullRenderer {
    struct Fyll {
        gfx::Rectf r;
        gfx::Color c;
    };
    std::vector<Fyll>        fyll;
    std::vector<std::string> texter;

    void fillRect(const gfx::Rectf& r, gfx::Color c) override
    {
        fyll.push_back({r, c});
    }
    void fillRoundedRect(const gfx::Rectf& r, float, gfx::Color c) override
    {
        fyll.push_back({r, c});
    }
    float drawText(gfx::Font&, float, float, const std::string& s,
                   gfx::Color) override
    {
        texter.push_back(s);
        return 0.0f;
    }
    // Den första fyllningen i färgen `c` (nullptr = ingen).
    const Fyll* fyllning(gfx::Color c) const
    {
        for (const Fyll& f : fyll)
            if (f.c.r == c.r && f.c.g == c.g && f.c.b == c.b && f.c.a == c.a)
                return &f;
        return nullptr;
    }
    bool ritade(const std::string& s) const
    {
        return std::find(texter.begin(), texter.end(), s) != texter.end();
    }
};

struct Ritbar : Grid {
    using Grid::draw;
};

// PG15. FÄRGCELLENS TRE LÄGEN, ritade mot en inspelande renderer: Swatch
// (förvalet) en ruta `colorCellSwatchW` bred och `checkSize` hög utan
// hextext, Hex hextexten utan ruta, Both en ruta i `checkSize` och
// hextexten. SABOTAGE: `drawColor` som ritar hextexten i Swatch, rutan i
// `checkSize` i Swatch, eller rutan också i Hex.
void fallFargcell()
{
    const glaze::Style& st = glaze::defaultStyle(glaze::UI::Slate);
    FalskFont           f(7.0f, st.uiFontSize);
    const gfx::Color    blå{0x33, 0x66, 0xFF, 0xFF};

    struct Läge {
        glaze::ColorDisplay d;
        const char*         namn;
    };
    for (const Läge l : {Läge{glaze::ColorDisplay::Swatch, "Swatch"},
                         Läge{glaze::ColorDisplay::Hex, "Hex"},
                         Läge{glaze::ColorDisplay::Both, "Both"}}) {
        Inspelning rec;
        glaze::Ctx c = ctxFör(st, f, 1.0f);
        c.r          = &rec;
        Ritbar g;
        g.width  = 300.0f;
        g.height = 100.0f;
        g.setRows({{.name = "Färg", .value = "#3366FF", .kind = K::Color,
                    .colorDisplay = l.d}});
        c.box = g.resolve(c);
        g.draw(c);
        const Inspelning::Fyll* ruta = rec.fyllning(blå);
        const bool text = rec.ritade("#3366FF");
        const std::string vad = std::string(l.namn) + ": " +
                                (ruta ? std::to_string(ruta->r.w) + " × " +
                                            std::to_string(ruta->r.h)
                                      : std::string("ingen ruta")) +
                                (text ? ", hextext" : ", ingen hextext");
        switch (l.d) {
        case glaze::ColorDisplay::Swatch:
            check(ruta && nära(ruta->r.w, st.colorCellSwatchW) &&
                      nära(ruta->r.h, st.checkSize) && !text,
                  "PG15: Swatch — rutan colorCellSwatchW × checkSize, ingen "
                  "hextext", vad);
            break;
        case glaze::ColorDisplay::Hex:
            check(!ruta && text, "PG15: Hex — hextexten, ingen ruta", vad);
            break;
        case glaze::ColorDisplay::Both:
            check(ruta && nära(ruta->r.w, st.checkSize) &&
                      nära(ruta->r.h, st.checkSize) && text,
                  "PG15: Both — rutan checkSize × checkSize och hextexten", vad);
            break;
        }
    }
}

// ================================================================ MED FÖNSTER

struct Vard : glaze::AppWindow {
    glaze::Panel stapel;
    Grid         gridA, gridB;   // PG3
    Grid         grid;           // PG6–PG9
    Grid         kol;            // PG12–PG14
    Lista        fäll;           // gridens onExpand/onCollapse
    Lista        kolLogg;        // kol:s onChange/onCommit
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
        ställ(kol, "kol", 450, 330, 300, 200);

        gridA.dock = glaze::Dock::Top;
        gridB.dock = glaze::Dock::Top;
        gridA.name = "gridA";
        gridB.name = "gridB";
        gridA.setRows({{.name = "H", .kind = K::Category}, {"A1", "1"}, {"A2", "2"}});
        gridB.setRows({{"B1", "1"}, {"B2", "2"}, {"B3", "3"}});

        grid.setColumnWidth(0, 80);
        grid.setRows({
            {.name = "Grupp", .kind = K::Category},                              // 0
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
        grid.onExpand = [this](glaze::Widget*, glaze::RowArgs& e) {
            fäll.push_back("expand " + std::to_string(e.row));
        };
        grid.onCollapse = [this](glaze::Widget*, glaze::RowArgs& e) {
            fäll.push_back("collapse " + std::to_string(e.row));
        };
        kol.onChange = [this] {
            kolLogg.push_back("change " + std::to_string(kol.changedRow()));
        };
        kol.onCommit = [this] {
            kolLogg.push_back("commit " + std::to_string(kol.changedRow()));
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
        root().adopt(kol);
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
    // namnkolumnens satta bredd 80), aldrig ur widgetens egna rect-funktioner.
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
    glaze::Widget* editor(Grid& g)
    {
        for (glaze::Widget* b : g.children())
            if (b->isVisible())
                return b;
        return nullptr;
    }
    glaze::Widget* editor() { return editor(grid); }
    // Ett tryck i `kol`s första rad, `lx` logiska px in — och editorn som öppnades.
    glaze::Widget* tryckKol(float lx, int pos = 0)
    {
        const gfx::Rectf r = kol.resolve(kol.behaviorCtx());
        const float      s = window().uiScale();
        klick(gfx::Pointf{r.x + lx * s,
                          r.y + (float(pos) + 0.5f) * style().gridRowH * s});
        return editor(kol);
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
                check(t.id() == glaze::str::InvalidValue,
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
            // Popupens stängning avslutar redigeringen: väljaren är gömd.
            check(w.färg() == nullptr && w.grid.activeRow() == -1,
                  "PG8: valet stängde popupen och avslutade redigeringen",
                  std::to_string(w.grid.activeRow()));
            glaze::ColorPicker* cp = nullptr;
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
            w.fäll.clear();
            w.klick(w.namn(w.grid, 0));
            check(w.fäll == Lista{"collapse 0"},
                  "PG11: användarens ihopfällning fyrar onCollapse en gång, med "
                  "raden i e.row", text(w.fäll));
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
                  "PG9: ett tryck på rubriken fäller ut igen, utan onChange");
            check(w.fäll == Lista{"collapse 0", "expand 0"},
                  "PG11: användarens utfällning fyrar onExpand en gång",
                  text(w.fäll));

            // setExpanded(false) under redigering: stängs UTAN att spara.
            w.klick(w.värde(w.grid, 6));
            w.skriv("q");
            w.grid.setExpanded(0, false);
            check(w.grid.activeRow() == -1 && w.synliga() == 0 &&
                      w.grid.rows()[6].value == "z" && w.logg.size() == märke,
                  "PG9: setExpanded(false) stänger den dolda radens editor utan "
                  "att spara", w.grid.rows()[6].value);
            w.grid.setExpanded(0, true);
            check(w.fäll == Lista{"collapse 0", "expand 0"},
                  "PG11: setExpanded fyrar varken onExpand eller onCollapse",
                  text(w.fäll));

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
            w.flytt.focus();   // gridens dolda editor släpper fokus
            break;
        }
        case 8: {
            // ---- PG12. KOLUMNLÄGENA — editorns plats och bredd i `kol` (ingen
            //            ram, 300 bred) är kolumnernas, i logiska px.
            const auto&       m   = w.style();
            const glaze::Ctx  c   = w.kol.behaviorCtx();
            const std::string lång = "Ett mycket längre namn";
            const std::string bred = "bbbbbbbbbbbbbbbb";
            w.kol.setRows({
                {"Kort", "a"},
                {lång, bred},
                {.name = "En ännu mycket mycket längre kategorirad",
                 .kind = K::Category},
            });
            const float cw = 300.0f - m.scrollThick;   // innehållsbredden
            auto        plats = [&](float lx) {
                glaze::Widget* e = w.tryckKol(lx);
                const std::pair<float, float> p =
                    e ? std::pair<float, float>{e->x, e->width}
                      : std::pair<float, float>{-1.0f, -1.0f};
                w.tangent(gfx::Key::Escape);
                return p;
            };
            const auto txt = [](std::pair<float, float> p) {
                return std::to_string(p.first) + " / " + std::to_string(p.second);
            };

            auto p = plats(m.gridNameW + 5.0f);
            check(nära(p.first, m.gridNameW) && nära(p.second, cw - m.gridNameW),
                  "PG12: förvalet — namnet gridNameW, värdet Autosize tar resten",
                  txt(p));

            const float namnFit = std::max(
                m.columnMinW, glaze::Draw::measure(c, lång).w + 2.0f * m.gridPadX);
            w.kol.setColumnSizing(0, glaze::ColumnSizing::AutoMin);
            p = plats(namnFit + 5.0f);
            check(nära(p.first, namnFit) && nära(p.second, cw - namnFit),
                  "PG12: AutoMin för namnet — det bredaste namnet med luft, "
                  "kategoriraden räknas inte", txt(p) + " mot " +
                                                   std::to_string(namnFit));

            w.kol.setColumnSizing(0, glaze::ColumnSizing::Manual);
            w.kol.setColumnWidth(0, 50.0f);
            p = plats(55.0f);
            check(nära(p.first, 50.0f) && nära(p.second, cw - 50.0f),
                  "PG12: Manual — den satta bredden", txt(p));

            const float värdeFit = std::max(
                m.columnMinW, glaze::Draw::measure(c, bred).w + 2.0f * m.gridPadX);
            w.kol.setColumnSizing(1, glaze::ColumnSizing::AutoMin);
            p = plats(55.0f);
            check(nära(p.first, 50.0f) && nära(p.second, värdeFit),
                  "PG12: AutoMin för värdet — den bredaste värdecellen med luft",
                  txt(p) + " mot " + std::to_string(värdeFit));

            w.kol.setColumnSizing(1, glaze::ColumnSizing::Manual);
            w.kol.setColumnWidth(1, 90.0f);
            p = plats(55.0f);
            check(nära(p.first, 50.0f) && nära(p.second, 90.0f),
                  "PG12: Manual för värdet — den satta bredden", txt(p));

            w.kol.setColumnSizing(1, glaze::ColumnSizing::Autosize);
            w.kol.setColumnWidth(1, 400.0f);
            p = plats(55.0f);
            check(nära(p.first, 50.0f) && nära(p.second, cw - 50.0f),
                  "PG12: Autosize med ett golv bredare än gridden klipps — "
                  "gridden rullar inte i sidled", txt(p));
            w.kol.setColumnWidth(1, 0.0f);
            break;
        }
        case 9: {
            // ---- PG13. DET TOMMA TALET i fältet: börjar tomt, sparas tomt, och
            //            `emptyText` är det som visas (AutoMin-bredden är dess).
            const auto&       m   = w.style();
            const glaze::Ctx  c   = w.kol.behaviorCtx();
            const std::string tom = "Automatiskt värde";
            w.kol.setRows({{.name = "Tal", .value = "", .kind = K::Number,
                            .allowEmpty = true, .emptyText = tom}});
            w.kol.setColumnSizing(1, glaze::ColumnSizing::AutoMin);
            glaze::Widget* e = w.tryckKol(55.0f);
            const float    visad = std::max(
                m.columnMinW, glaze::Draw::measure(c, tom).w + 2.0f * m.gridPadX);
            // Fältet når cellens högerkant med sin egen luft i stället för
            // cellens (numberFieldRect utan suffix).
            check(e && nära(e->x + e->width,
                            50.0f + visad - m.gridPadX + m.fieldPadX),
                  "PG13: det tomma värdet visas som emptyText (värdecellens "
                  "bredd är dess)",
                  e ? std::to_string(e->x + e->width) : "ingen editor");
            w.kol.setColumnSizing(1, glaze::ColumnSizing::Autosize);
            glaze::TextInput* f = dynamic_cast<glaze::TextInput*>(e);
            check(f && f->text().empty(),
                  "PG13: fältet börjar tomt, inte med emptyText",
                  f ? f->text() : "ingen editor");
            märke = w.kolLogg.size();
            w.tangent(gfx::Key::Return);
            check(w.kol.rows()[0].value.empty() && !w.kol.hasError(0) &&
                      w.kolLogg.size() == märke,
                  "PG13: tomt + Enter ⇒ inget fel och inga events (samma tomma "
                  "värde)", w.kol.rows()[0].value);
            w.tryckKol(55.0f);
            w.skriv("5");
            w.tangent(gfx::Key::Return);
            check(w.kol.rows()[0].value == "5" && w.kol.numberValue(0) == 5.0,
                  "PG13: ett tal sparas som vanligt", w.kol.rows()[0].value);
            märke = w.kolLogg.size();
            w.tryckKol(55.0f);
            w.tangent(gfx::Key::Backspace);   // den markerade texten bort
            w.tangent(gfx::Key::Return);
            check(w.kol.rows()[0].value.empty() && !w.kol.hasError(0) &&
                      !w.kol.numberValue(0) &&
                      Lista(w.kolLogg.begin() + long(märke), w.kolLogg.end()) ==
                          Lista{"change 0", "commit 0"},
                  "PG13: talet tömt + Enter ⇒ det tomma värdet sparas, ett par, "
                  "inget fel", w.kol.rows()[0].value);
            break;
        }
        case 10: {
            // ---- PG14. VALLISTANS IKONER når listan.
            const glaze::ImageSource hel =
                glaze::stockIcon(glaze::StockIcon::Save);
            const glaze::ImageSource streck =
                glaze::stockIcon(glaze::StockIcon::Open);
            w.kol.setRows({{.name = "Linje", .value = "Streckad",
                            .choices = {"Heldragen", "Streckad"},
                            .choiceIcons = {hel, streck}}});
            glaze::Dropdown* dd = dynamic_cast<glaze::Dropdown*>(w.tryckKol(55.0f));
            check(dd && dd->items() == Lista{"Heldragen", "Streckad"} &&
                      dd->itemIcon(0) == hel && dd->itemIcon(1) == streck &&
                      dd->selectedIndex() == 1,
                  "PG14: choiceIcons når vallistan, post för post");
            w.tangent(gfx::Key::Escape);
            w.kol.setRows({{.name = "Linje", .value = "Heldragen",
                            .choices = {"Heldragen", "Streckad"}}});
            dd = dynamic_cast<glaze::Dropdown*>(w.tryckKol(55.0f));
            check(dd && !dd->itemIcon(0).valid() && !dd->itemIcon(1).valid(),
                  "PG14: en vallista utan ikoner har inga kvar från förra raden");
            w.tangent(gfx::Key::Escape);
            break;
        }
        case 11: {
            // ---- PG16. FÄRGCELLENS AutoMin i de tre lägena. Editorn
            //            (färgväljaren) är värdekolumnens bredd, och raderna
            //            står i tre gridar efter varandra: Swatch (förvalet) är
            //            rutan `colorCellSwatchW`, Hex hextexten och Both rutan
            //            i `checkSize`, luften och hextexten — med cellens
            //            luft. SABOTAGE: `valueContentW` som inte skickar
            //            `colorDisplay`, eller `colorW` som svarar samma bredd i
            //            två lägen.
            const auto& m = w.style();
            w.kol.setColumnSizing(1, glaze::ColumnSizing::AutoMin);
            w.kol.setRows({{.name = "Färg", .value = "#3366FF", .kind = K::Color}});
            glaze::Widget* e    = w.tryckKol(55.0f);
            const float    ruta = std::max(m.columnMinW,
                                           m.colorCellSwatchW + 2.0f * m.gridPadX);
            check(e && nära(e->width, ruta),
                  "PG16: AutoMin för en färgrad i Swatch (förvalet) är rutan "
                  "colorCellSwatchW med luft",
                  (e ? std::to_string(e->width) : std::string("ingen editor")) +
                      " mot " + std::to_string(ruta));
            w.kol.setRows({{.name = "Färg", .value = "#3366FF", .kind = K::Color,
                            .colorDisplay = glaze::ColorDisplay::Hex}});
            break;
        }
        case 12: {
            const auto&      m = w.style();
            const glaze::Ctx c = w.kol.behaviorCtx();
            glaze::Widget*   e = w.tryckKol(55.0f);
            const float      hex = std::max(
                m.columnMinW,
                glaze::Draw::measure(c, "#3366FF").w + 2.0f * m.gridPadX);
            check(e && nära(e->width, hex),
                  "PG16: AutoMin för en färgrad i Hex är hextexten med luft",
                  (e ? std::to_string(e->width) : std::string("ingen editor")) +
                      " mot " + std::to_string(hex));
            w.kol.setRows({{.name = "Färg", .value = "#3366FF", .kind = K::Color,
                            .colorDisplay = glaze::ColorDisplay::Both}});
            break;
        }
        case 13: {
            const auto&      m = w.style();
            const glaze::Ctx c = w.kol.behaviorCtx();
            glaze::Widget*   e = w.tryckKol(55.0f);
            const float      båda = std::max(
                m.columnMinW, m.checkSize + m.checkGap +
                                  glaze::Draw::measure(c, "#3366FF").w +
                                  2.0f * m.gridPadX);
            check(e && nära(e->width, båda),
                  "PG16: AutoMin för en färgrad i Both är rutan, luften och "
                  "hextexten",
                  (e ? std::to_string(e->width) : std::string("ingen editor")) +
                      " mot " + std::to_string(båda));
            w.kol.setColumnSizing(1, glaze::ColumnSizing::Autosize);
            w.kol.setRows({{"Namn", "abc"},
                           {.name = "Tal", .value = "12", .kind = K::Number}});
            break;
        }
        case 14: {
            // ---- PG17. TEXTEN MARKERAD VID REDIGERING. Förvalet (true): ett
            //            klick öppnar fritext- och talfältet med HELA texten
            //            markerad. Med false: markören sist och ingenting
            //            markerat, och det man skriver hamnar efter texten.
            //            (Gridden börjar en redigering bara med ett klick — den
            //            är inget tabbstopp.) SABOTAGE: `openEditor` som alltid
            //            markerar allt (inte frågar `selectOnEdit_`), eller som
            //            aldrig gör det.
            const auto helaMarkerad = [&](glaze::Widget* e, const std::string& v) {
                const glaze::TextInput* f = dynamic_cast<glaze::TextInput*>(e);
                return f && f->text() == v && f->hasSelection() &&
                       f->selBegin() == 0 && f->selEnd() == v.size();
            };
            const auto sistUtan = [&](glaze::Widget* e, const std::string& v) {
                const glaze::TextInput* f = dynamic_cast<glaze::TextInput*>(e);
                return f && f->text() == v && !f->hasSelection() &&
                       f->caret() == v.size();
            };
            check(w.kol.selectOnEdit(), "PG17: selectOnEdit är true som förval");
            check(helaMarkerad(w.tryckKol(55.0f, 0), "abc"),
                  "PG17: selectOnEdit — klicket öppnar fritexten med hela texten "
                  "markerad");
            w.tangent(gfx::Key::Escape);
            check(helaMarkerad(w.tryckKol(55.0f, 1), "12"),
                  "PG17: selectOnEdit — klicket öppnar talet med hela texten "
                  "markerad");
            w.tangent(gfx::Key::Escape);

            w.kol.setSelectOnEdit(false);
            check(sistUtan(w.tryckKol(55.0f, 0), "abc"),
                  "PG17: utan selectOnEdit — fritexten med markören sist och "
                  "inget markerat");
            w.skriv("d");
            {
                const glaze::TextInput* f =
                    dynamic_cast<glaze::TextInput*>(w.editor(w.kol));
                check(f && f->text() == "abcd",
                      "PG17: utan selectOnEdit hamnar det skrivna efter texten",
                      f ? f->text() : "ingen editor");
            }
            w.tangent(gfx::Key::Escape);
            check(sistUtan(w.tryckKol(55.0f, 1), "12"),
                  "PG17: utan selectOnEdit — talet med markören sist och inget "
                  "markerat");
            w.tangent(gfx::Key::Escape);
            w.kol.setSelectOnEdit(true);
            check(w.kol.setProp("selectOnEdit", "false") && !w.kol.selectOnEdit() &&
                      w.kol.setProp("selectOnEdit", "true") && w.kol.selectOnEdit(),
                  "PG17: setProp selectOnEdit når gridden");
            break;
        }
        case 15:
            w.close();
            return;
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
    fallTomtOchKolumner();
    fallFargcell();
    fallMedFonster();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
