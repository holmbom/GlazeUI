// datagrid_gate — DATAGRIDDENS grind.
//
// Kör:  ./build/tests/datagrid_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = ett block kunde
//       inte köras (ingen display, eller vallistans popup kom aldrig upp) —
//       ctest räknar det som SKIPPED, aldrig grönt
//
// TVÅDELAD:
//
//   HEADLESS     registret och kompositen (två DOLDA editorer som inte går
//                att lossa), KOLUMNTYPEN (per INDEX — den överlever
//                `setColumn`/`setColumns`, så satsordningen avgör inte), den
//                AKTUELLA CELLEN (utanför ⇒ no-op, (−1, −1) nollar, `setRows`/
//                `setColumns` släpper den), redigering utan fönster (no-op)
//                och RITNINGEN mot en inspelande renderer med en EGEN stil
//                där `gridRowH` skiljer sig från `listRowH` (de är lika
//                i alla tre stilarna, så bara en sådan stil kan mäta att
//                datagriden räknar med gridraden). R6 mäter
//                JUSTERINGEN: cellernas Left/Center/Right och rubrikernas
//                EGEN justering som textens x-läge, i alla tre familjerna.
//                R7 mäter RADENS FÄRG: de omarkerade
//                cellerna i radens färg, den aktuella cellen i markeringens
//                och allt i `textDim` inaktiverat, i alla tre familjerna.
//                S1–S5 mäter RADVALET headless:
//                i `Cell`-läget är inga rader valda (men valet LAGRAS, så
//                satsordningen inte avgör), i `FullRow` väljer ett klick
//                bara raden, Ctrl vänder och Shift ger spannet från ankaret i
//                VISNINGSORDNINGEN (med flerval), en flytt av den aktuella
//                cellen flyttar valet (Shift+Home/End utökar), Ctrl+A väljer
//                alla, `onSelectionChange` fyras EN gång per handling som
//                ändrade valet och aldrig av settrarna, `setRows` nollar,
//                `setRow` behåller, och valet följer raderna genom
//                sorteringen. Sabotagen: `isSelected` utan lägesvakten (S1),
//                `setCurrentPosition` utan `selectRowFromUser` (S3),
//                `assignSelection` utan ändringsvakten (S4), `rowsReplaced`
//                som behåller valet (S5).
//   MED FÖNSTER  ett DOLT fönster (`setOffscreen`), händelserna genom
//                fönstrets RIKTIGA ingång (`AppWindow::handleEvent`) och
//                stegen som en KEDJA AV POSTADE STEG (dropdown_gate-
//                prejudikatet: popupens stängning och fokusförlustens
//                postade `onChange` verkställs i pump-svansen, så ett test
//                som läser dem i samma steg mäter ingenting). Redigeringens
//                tre starter (dubbelklick, F2, att skriva), dess avslut
//                (Enter, Escape, Tab, Upp/Ned, fokusförlust, vallistans val),
//                editorns PLATS (på cellen, och den följer sortering och
//                scroll), Tab ut ur tabellen i kanten, skrivskyddade
//                kolumner, inaktivering och programmatiska avbrott — och
//                E24: ett huvud på tre rader (klicket och editorns
//                plats mot förväntat värde) och att fältet och vallistan står i
//                KOLUMNENS justering. E25: editorn börjar varje
//                cell med TOM ångra-historik, också när nästa cell har samma
//                text som den förra fick.
//
// GEOMETRIN MÄTS MOT ETT OBEROENDE FÖRVÄNTAT VÄRDE: cellerna räknas i grinden ur
// KONTRAKTET (ramens indrag `panelInset`, huvudets `tableHeaderH`, radens
// `gridRowH`, de satta kolumnbredderna och scrollen) — aldrig ur widgetens
// egna `cellRect`/`cellInContent`. Editorns rect jämförs mot samma förväntat värde.
//
// SABOTAGEN den fäller:
//   * `endEdit` utan "texten ändrades"-kontrollen (E4: Enter utan ändring
//     fyrar `onChange`)
//   * fokusförlustens `onChange` fyrat DIREKT i stället för postat (E12)
//   * det postade eventet utan radgenerationen (E13: fyras efter `setRows`)
//   * `placeChild` utan scrollen (E15) och mot fel rad efter en sortering
//     (E14)
//   * Escape i en ÖPPEN vallista som bara stänger listan (E7)
//   * fältets Upp/Ned som inte avslutar redigeringen (E11)
//   * Tab som slår runt i tabellens sista cell i stället för att lämna den
//     (E9, E22)
//   * en `None`-kolumn som ändå går att redigera (E10, E21)
//   * ritningen som inte frågar `editable` (R1: alla kolumner ritas som
//     redigerbara, eller alla som skrivskyddade)
//   * Glaze-grenen med Slates `accent` i stället för det grindade paret
//     `accentSurface`/`onAccent` (R5)
//   * `DataGrid::rowH` som inte överrids (R1 med den egna stilen)
//   * `setColumns` som nollar kolumntyperna (H3)
//   * `headerH` som inte bryr sig om
//     radantalet (E24), rubriken i cellernas justering (R6), fältets och
//     vallistans `setAlign` bortplockade ur `startEdit` (E24)
//   * `TextEditor::börja` utan
//     `resetHistory` — E25 (förra cellens ångra följer med)
//   * ritningen som ignorerar
//     radens färg (`col.text` i stället för `appTextColor`) — R7; radens
//     färg som vinner över den aktuella cellens — R7
//
// S1–S4: CELLENS
// FÄRGER och RADRUBRIKER i datagriden. Sabotage som ska fälla dem: en
// cellbakgrund som ritas på den aktuella cellen eller i en inaktiv tabell
// (S2, S3) · cellens text som inte slår radens (S1) · `firstColumn()` som
// glöms i `setCurrentPosition`/`setCurrentCell`/`tabFromUser` (S4) · radnamnet
// som rullar med (S4). Mäts inte: redigering på kolumn 1 med radrubriker (kräver
// ett öppet fönster) och hur remsan SER UT (listview_gate R7 mäter ytan och
// hårlinjerna på ListView, samma `drawRowHeaders`).
//
// VAD DEN INTE MÄTER, skrivet så anspråket inte överdrivs:
//   * UTSEENDET — att datagriden RITAR något i varje UI × tema × läge mäter
//     `pixel_gate` (galleri-fixturen); recepten är granskade renderade i
//     alla tre UI, ljust och mörkt. R-fallen mäter bara de beslut som är
//     LOGIK (vilka kolumner som fylls, var den aktuella cellen markeras,
//     textfärgen per kolumntyp).
//   * ett MUSKLICK i vallistans popup — valet görs med tangenterna, som går
//     samma väg (`Dropdown::commit` → `onChange` → `choicePicked`).
//   * att datagriden dör innan det postade eventet hunnit fram: vakten
//     (`weak_ptr`) finns, men ett sabotage där är odefinierat beteende och
//     fälls inte säkert — fallet mäts inte.
//   * editorn över den lodräta remsan (skriven begränsning i data_grid.h).
//   * hur editorns text RITAS i kolumnens justering — E24 mäter att
//     editorn fått den; fältets ritning är textinput:s, dropdownens mäts i
//     dropdown_gate.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <gfx/null_renderer.h>
#include <gfx/platform.h>

#include <glaze/glaze.h>

namespace {

int  g_fails   = 0;
int  g_cases   = 0;
int  g_skips   = 0;
bool g_verbose = false;

// Textfältens redigeringsgenvägar (cellens editor) är plattformens — Cmd på
// macOS, Ctrl annars. Datagriddens egen Ctrl+A (radvalet) tar båda
// och rörs inte.
#ifdef __APPLE__
constexpr std::uint32_t kRedigera = gfx::kModCmd;
#else
constexpr std::uint32_t kRedigera = gfx::kModCtrl;
#endif

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

std::string fog(const gfx::Rectf& r)
{
    char buf[96];
    std::snprintf(buf, sizeof buf, "{%.2f, %.2f, %.2f, %.2f}", r.x, r.y, r.w,
                  r.h);
    return buf;
}

bool lika(gfx::Color a, gfx::Color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

using Editor = glaze::DataGrid::Editor;

// De skyddade vägarna anropbara — det en subklass får göra — och eventet
// RÄKNAT, med raden, kolumnen och värdet ur sender (regel 6).
struct Prov : glaze::DataGrid {
    using glaze::DataGrid::draw;
    // Radrubrikens påverkan på den aktuella cellen.
    using glaze::DataGrid::moveFromUser;
    using glaze::DataGrid::tabFromUser;

    int         ändringar = 0;
    int         rad       = -1;
    int         kol       = -1;
    std::string värde;
    bool        avsändare = true;   // var sender datagriden varje gång?
    Prov()
    {
        onChange = [this](glaze::Widget* s, glaze::CellChangeArgs& e) {
            ++ändringar;
            rad   = e.row;
            kol   = e.column;
            värde = cell(e.row, e.column);
            avsändare = avsändare && s == this;
        };
    }
};

// RADVALETS användarvägar — protected, så en subklass når dem.
struct Radval : glaze::DataGrid {
    using glaze::DataGrid::keyDown;
    using glaze::DataGrid::moveFromUser;
    using glaze::DataGrid::selectRowFromUser;
    int val = 0;
    Radval()
    {
        setColumns({{"Namn"}});
        setRows({{{"c"}}, {{"a"}}, {{"d"}}, {{"b"}}});
        onSelectionChange = [this] { ++val; };
    }
};

std::string radlista(const std::vector<int>& v)
{
    std::string s = "{";
    for (std::size_t i = 0; i < v.size(); ++i)
        s += (i ? "," : "") + std::to_string(v[i]);
    return s + "}";
}

// ------------------------------------------------------ falsk font
// Fast teckensteg — ritningen behöver bara en font som svarar.
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
        float       x = 0.0f;   // fysiska px — justeringen
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
    float drawText(gfx::Font&, float x, float, const std::string& s,
                   gfx::Color c) override
    {
        texter.push_back({s, c, x});
        return 0.0f;
    }
    // Var texten `s` började (första förekomsten); NaN om den inte ritades.
    float textX(const std::string& s) const
    {
        for (const Text& t : texter)
            if (t.s == s)
                return t.x;
        return std::nanf("");
    }

    // Fyllningar i färgen `c` med exakt recten `r`.
    bool harFyll(const gfx::Rectf& r, gfx::Color c) const
    {
        for (const Fyll& f : fyll)
            if (lika(f.c, c) && sammaRect(f.r, r))
                return true;
        return false;
    }
    // Någon fyllning i färgen `c` som SKÄR intervallet [x0, x1) i sidled
    // och [y0, y1) i höjdled.
    bool fyllSkär(gfx::Color c, float x0, float x1, float y0, float y1) const
    {
        for (const Fyll& f : fyll)
            if (lika(f.c, c) && f.r.x < x1 - 0.01f && f.r.x + f.r.w > x0 + 0.01f &&
                f.r.y < y1 - 0.01f && f.r.y + f.r.h > y0 + 0.01f)
                return true;
        return false;
    }
    // Färgen texten `s` ritades i (första förekomsten).
    bool textFärg(const std::string& s, gfx::Color c) const
    {
        for (const Text& t : texter)
            if (t.s == s)
                return lika(t.c, c);
        return false;
    }
};

// ============================================================== HEADLESS

void fallRegistret()
{
    auto w = glaze::makeWidget("datagrid");
    check(w && dynamic_cast<glaze::DataGrid*>(w.get()) != nullptr,
          "H1: registret — \"datagrid\" ger en riktig glaze::DataGrid");
    check(w && dynamic_cast<glaze::Table*>(w.get()) != nullptr &&
              dynamic_cast<glaze::ListView*>(w.get()) == nullptr,
          "H1: datagriden är en Table — men INTE en ListView (var sitt "
          "onChange)");
    glaze::DataGrid g;
    check(g.focusable() && g.focusesChildren(),
          "H1: ETT tabbstopp — editorerna är inredning");
    bool dolda = g.children().size() == 2;
    for (const glaze::Widget* b : g.children())
        dolda = dolda && !b->isVisible();
    check(dolda, "H1: exakt två editorer, båda dolda i vila");
    check(!g.unadopt(*g.children()[0]) && g.children().size() == 2,
          "H1: en editor går inte att lossa utifrån (canReleaseChild)");
    check(!g.isEditing() && g.editRow() == -1 && g.editColumn() == -1 &&
              g.currentRow() == -1 && g.currentColumn() == -1,
          "H1: default — ingen redigering, ingen aktuell cell");
    check(g.setProp("sortColumn", "1") && g.sortColumn() == 1 &&
              !g.setProp("columnEditor", "1"),
          "H2: setProp — Tables sortering når basen, inga egna nycklar");
}

void fallKolumntypen()
{
    glaze::DataGrid g;
    check(g.columnEditor(0) == Editor::Text && g.columnEditor(9) == Editor::Text &&
              g.columnEditor(-1) == Editor::Text && g.columnChoices(3).empty(),
          "H3: ett index utan inställning är fritext utan val");
    g.setColumnEditor(2, Editor::None);
    g.setColumnEditor(1, Editor::Choice);
    g.setColumnChoices(1, {"a", "b"});
    g.setColumnEditor(-1, Editor::None);
    g.setColumnChoices(-2, {"x"});
    check(g.columnEditor(2) == Editor::None &&
              g.columnEditor(1) == Editor::Choice &&
              g.columnChoices(1) == std::vector<std::string>{"a", "b"} &&
              g.columnEditor(0) == Editor::Text,
          "H3: typen sätts FÖRE kolumnerna finns (satsordningen avgör inte)");
    g.addColumn("A");
    g.addColumn("B");
    g.addColumn("C");
    g.setColumn(1, "B2", 50);
    g.setColumns({{"X"}, {"Y"}, {"Z"}, {"W"}});
    check(g.columnEditor(1) == Editor::Choice &&
              g.columnEditor(2) == Editor::None &&
              g.columnChoices(1).size() == 2 &&
              g.columnEditor(3) == Editor::Text,
          "H3: typen hör till INDEXET — den överlever addColumn, setColumn "
          "och setColumns");
    g.setColumnEditor(1, Editor::Text);
    g.setColumnChoices(1, {});
    check(g.columnEditor(1) == Editor::Text && g.columnChoices(1).empty(),
          "H3: typen och valen går att nollställa");
}

void fallAktuellCell()
{
    glaze::DataGrid g;
    g.setColumns({{"A"}, {"B"}, {"C"}});
    g.setRows({{{"a0", "b0", "c0"}}, {{"a1", "b1", "c1"}}, {{"a2"}}});
    g.setCurrentCell(1, 2);
    check(g.currentRow() == 1 && g.currentColumn() == 2,
          "H4: setCurrentCell sätter rad OCH kolumn");
    g.setCurrentCell(3, 0);
    g.setCurrentCell(0, 3);
    g.setCurrentCell(-1, 1);
    g.setCurrentCell(1, -1);
    check(g.currentRow() == 1 && g.currentColumn() == 2,
          "H4: utanför tabellen (och halvt −1) ⇒ logg + no-op");
    g.setCurrentCell(-1, -1);
    check(g.currentRow() == -1 && g.currentColumn() == -1,
          "H4: (−1, −1) nollar den aktuella cellen");
    g.setCurrentCell(2, 1);
    g.setColumns({{"A"}, {"B"}});
    check(g.currentRow() == 2 && g.currentColumn() == -1,
          "H4: setColumns släpper den aktuella KOLUMNEN (raden står)");
    g.setCurrentCell(2, 1);
    g.setRows({{{"x"}}});
    check(g.currentRow() == -1 && g.currentColumn() == -1,
          "H4: setRows släpper den aktuella cellen");

    // Utan ett visat fönster finns ingen editor att visa.
    g.setCurrentCell(0, 0);
    g.beginEdit();
    check(!g.isEditing(), "H5: beginEdit utan fönster ⇒ no-op");
    g.commitEdit();
    g.cancelEdit();
    check(!g.isEditing() && g.cell(0, 0) == "x",
          "H5: commitEdit/cancelEdit utan redigering ⇒ no-op");
}

// RITNINGEN — de beslut som är LOGIK, mot en inspelande renderer och en EGEN
// stil där gridraden (31) skiljer sig från listraden.
void fallRadvalet()
{
    using V  = std::vector<int>;
    using SM = glaze::DataGrid::SelectionMode;
    // S1 — Cell-läget: lagrat, men inga rader valda.
    {
        Radval g;
        g.setSelected(1, true);
        check(g.selectionMode() == SM::Cell && g.selectedRows().empty() &&
                  !g.isSelected(1),
              "S1: i Cell-läget är inga rader valda");
        g.setSelectionMode(SM::FullRow);
        check(g.selectedRows() == V{1},
              "S1: det lagrade valet syns när läget blir FullRow (ordningen)",
              radlista(g.selectedRows()));
        check(g.val == 0, "S4: settrarna fyrar inget onSelectionChange");
    }
    // S2 — klick, Ctrl, Shift (med flerval) och spannet i VISNINGSORDNING.
    {
        Radval g;
        g.setSelectionMode(SM::FullRow);
        g.setMultiSelect(true);
        g.selectRowFromUser(0, 0, false);
        check(g.selectedRows() == V{0} && g.val == 1,
              "S2/S4: klick väljer bara raden — ETT onSelectionChange");
        g.selectRowFromUser(2, gfx::kModCtrl, false);
        check(g.selectedRows() == V{0, 2} && g.val == 2,
              "S2: Ctrl+klick vänder raden in", radlista(g.selectedRows()));
        g.setSortColumn(0);   // a(1) b(3) c(0) d(2)
        g.selectRowFromUser(1, 0, false);
        g.selectRowFromUser(0, gfx::kModShift, false);
        // visningsordningen: 1, 3, 0 ⇒ spannet {1, 3, 0} i visningsordning
        check(g.selectedRows() == V{1, 3, 0},
              "S2/S5: Shift ger spannet i VISNINGSORDNINGEN (sorterat)",
              radlista(g.selectedRows()));
        const int före = g.val;
        g.selectRowFromUser(0, gfx::kModShift, false);
        check(g.val == före, "S4: en handling som inte ändrar valet fyrar inget");
        g.setMultiSelect(false);
        check(g.selectedRows().size() == 1,
              "S2: setMultiSelect(false) behåller EN rad",
              radlista(g.selectedRows()));
        g.selectRowFromUser(2, gfx::kModCtrl, false);
        check(g.selectedRows() == V{2},
              "S2: utan flerval är Ctrl+klick ett vanligt klick",
              radlista(g.selectedRows()));
    }
    // S3 — flytten av den aktuella cellen flyttar valet; Shift+End utökar;
    // Ctrl+A väljer alla.
    {
        Radval g;
        g.setSelectionMode(SM::FullRow);
        g.setMultiSelect(true);
        g.setCurrentCell(0, 0);
        g.moveFromUser(1, 0);
        check(g.currentRow() == 1 && g.selectedRows() == V{1} && g.val == 1,
              "S3: en flytt nedåt väljer den nya raden",
              radlista(g.selectedRows()));
        check(g.keyDown(gfx::Key::End, gfx::kModShift | gfx::kModCtrl) &&
                  g.currentRow() == 3 && g.selectedRows() == V{1, 2, 3},
              "S3: Ctrl+Shift+End utökar spannet till sista raden",
              radlista(g.selectedRows()));
        check(g.keyDown(gfx::Key::A, gfx::kModCtrl) &&
                  g.selectedRows() == V{0, 1, 2, 3},
              "S3: Ctrl+A väljer alla", radlista(g.selectedRows()));
        Radval c;   // Cell-läget: Ctrl+A är inte datagriddens
        check(!c.keyDown(gfx::Key::A, gfx::kModCtrl),
              "S3: Ctrl+A i Cell-läget bubblar vidare");
    }
    // S5 — `setRows` nollar, `setRow` behåller, `addRow` ger en ovald rad.
    {
        Radval g;
        g.setSelectionMode(SM::FullRow);
        g.setMultiSelect(true);
        g.setSelected(1, true);
        g.setSelected(3, true);
        g.setRow(1, {"A"});
        check(g.selectedRows() == V{1, 3}, "S5: setRow behåller valet",
              radlista(g.selectedRows()));
        g.addRow({"e"});
        check(!g.isSelected(4), "S5: en ny rad är aldrig vald");
        g.setRows({{{"x"}}, {{"y"}}});
        check(g.selectedRows().empty(), "S5: setRows nollar valet",
              radlista(g.selectedRows()));
        g.setSelected(7, true);
        check(g.selectedRows().empty(), "S5: setSelected utanför är no-op");
    }
    // setProp — läget med enumets gemena namn och flervalet.
    {
        Radval g;
        check(g.setProp("selectionMode", "fullrow") &&
                  g.selectionMode() == SM::FullRow &&
                  g.setProp("multiSelect", "true") && g.isMultiSelect(),
              "S: setProp selectionMode/multiSelect");
        check(!g.setProp("selectionMode", "kolumn") &&
                  g.selectionMode() == SM::FullRow,
              "S: ett okänt läge ⇒ false, orört");
    }
}

void fallRitningen()
{
    StegFont     f;
    Inspelning   rec;
    glaze::Style stil = glaze::defaultStyle(glaze::UI::Slate);
    stil.gridRowH     = 31.0f;
    stil.listRowH     = 19.0f;
    const float in    = stil.panelInset;
    const float hh    = stil.tableHeaderH;
    const float rh    = stil.gridRowH;

    Prov g;
    g.width  = 300;
    g.height = hh + 3.0f * rh + 2.0f * in + 40.0f;   // luft under raderna
    g.setColumns({{"Uppgift", 80}, {"Status", 70}, {"Id", 60}, {"Tom", 50}});
    g.setColumnEditor(1, Editor::Choice);
    g.setColumnChoices(1, {"Ny", "Klar"});
    g.setColumnEditor(2, Editor::None);
    g.setColumnEditor(3, Editor::Choice);   // UTAN val — inte redigerbar
    g.setRows({{{"a0", "Ny", "i0", "t0"}},
               {{"a1", "Klar", "i1", "t1"}},
               {{"a2", "Ny", "i2", "t2"}}});
    g.setCurrentCell(1, 0);

    const auto ctxFör = [&](glaze::UI ui, const glaze::Theme::Variant& v) {
        glaze::Ctx c{};
        c.r     = &rec;
        c.font  = &f;
        c.scale = 1.0f;
        c.ui    = ui;
        c.style = &stil;
        c.theme = &v;
        c.area  = {0, 0, 400, 300};
        c.box   = g.resolve(c);
        return c;
    };
    // Förväntat värde ur KONTRAKTET: kolumn k:s x-intervall och radernas band.
    const float x[] = {in, in + 80, in + 150, in + 210, in + 260};
    const float y0  = in + hh;   // radernas överkant

    // ---- SLATE ljust
    const glaze::Theme::Variant& slate = glaze::defaultTheme(glaze::UI::Slate).light;
    g.draw(ctxFör(glaze::UI::Slate, slate));
    const gfx::Color ro = slate.colors.fieldReadOnly;
    check(rec.harFyll({x[2], y0, 60, 3 * rh}, ro) &&
              rec.harFyll({x[3], y0, 50, 3 * rh}, ro),
          "R1: Slate — den skrivskyddade kolumnen och val-kolumnen UTAN val "
          "ritas som skrivskyddade fält, ner till sista raden (gridraden 31, "
          "inte listradens 19)");
    check(!rec.fyllSkär(ro, x[0], x[2], y0, y0 + 3 * rh),
          "R1: Slate — de redigerbara kolumnerna fylls inte som skrivskyddade");
    check(rec.harFyll({x[0], y0 + rh, 80, rh}, slate.colors.accent),
          "R2: Slate — den aktuella cellen (1, 0) fylls med accent");
    check(rec.textFärg("a1", slate.colors.windowBg) &&
              rec.textFärg("a0", slate.colors.text) &&
              rec.textFärg("i1", slate.colors.text),
          "R2: Slate — den aktuella cellens text i windowBg, resten (även "
          "den skrivskyddade) i text");

    // ---- MINIMALISTIC — samma ytor; markören i vänsterkanten.
    rec.fyll.clear();
    rec.texter.clear();
    const glaze::Theme::Variant& mini =
        glaze::defaultTheme(glaze::UI::Minimalistic).light;
    g.draw(ctxFör(glaze::UI::Minimalistic, mini));
    check(rec.harFyll({x[2], y0, 60, 3 * rh}, mini.colors.fieldReadOnly) &&
              !rec.fyllSkär(mini.colors.fieldReadOnly, x[0], x[2], y0,
                            y0 + 3 * rh),
          "R3: Minimalistic — samma ytor som de andra (fältets)");
    check(rec.harFyll({x[0], y0 + rh, stil.rowMarkerW, rh}, mini.colors.accent) &&
              !rec.harFyll({x[0], y0 + rh, 80, rh}, mini.colors.accent) &&
              rec.textFärg("a1", mini.colors.accent),
          "R3: Minimalistic — ett accent-STRECK (ingen fyllning) och "
          "accent-text i den aktuella cellen");
    check(rec.textFärg("i0", mini.colors.text),
          "R3: Minimalistic — den skrivskyddade kolumnens text i full kontrast");

    // ---- GLAZE — samma ytor; den aktuella cellen i det GRINDADE paret.
    rec.fyll.clear();
    rec.texter.clear();
    const glaze::Theme::Variant& glz = glaze::defaultTheme(glaze::UI::Glaze).light;
    g.draw(ctxFör(glaze::UI::Glaze, glz));
    check(rec.harFyll({x[2], y0, 60, 3 * rh}, glz.colors.fieldReadOnly) &&
              !rec.fyllSkär(glz.colors.fieldReadOnly, x[0], x[2], y0,
                            y0 + 3 * rh),
          "R5: Glaze — samma ytor som de andra (fältets)");
    check(rec.harFyll({x[0], y0 + rh, 80, rh}, glz.colors.accentSurface) &&
              rec.textFärg("a1", glz.colors.onAccent) &&
              rec.textFärg("a0", glz.colors.text),
          "R5: Glaze — den aktuella cellen i accentSurface med onAccent-text, "
          "resten i text");

    // ---- INAKTIVERAD (Slate) — markeringen faller, all text dämpas.
    rec.fyll.clear();
    rec.texter.clear();
    g.disable();
    g.draw(ctxFör(glaze::UI::Slate, slate));
    check(rec.harFyll({x[0], y0 + rh, 80, rh}, slate.colors.buttonDisabled) &&
              rec.textFärg("a1", slate.colors.textDim) &&
              rec.textFärg("a0", slate.colors.textDim),
          "R4: inaktiverad — den aktuella cellen i buttonDisabled, texten "
          "textDim");

    // ---- R6 JUSTERINGEN — cellerna Left/Center/Right och
    //      rubrikerna med en EGEN justering (en annan än cellernas, så ingen
    //      passerar på den andras), i alla tre familjerna. Förväntat värde ur
    //      kontraktet: kolumnens kant, cellens luft `gridPadX` och textens
    //      bredd (8 px per tecken).
    const float pad = stil.gridPadX;
    Prov        j;
    j.width  = 3.0f * 80.0f + 2.0f * in;
    j.height = hh + rh + 2.0f * in;
    j.setColumns({{"H1", 80, glaze::Align::Left, glaze::Align::Center},
                  {"H2", 80, glaze::Align::Center, glaze::Align::Right},
                  {"H3", 80, glaze::Align::Right, glaze::Align::Left}});
    j.addRow({"l", "cc", "rrr"});
    const std::pair<glaze::UI, const glaze::Theme::Variant*> familjer[] = {
        {glaze::UI::Slate, &slate},
        {glaze::UI::Glaze, &glz},
        {glaze::UI::Minimalistic, &mini}};
    for (const auto& [ui, tema] : familjer) {
        rec.fyll.clear();
        rec.texter.clear();
        glaze::Ctx c{};
        c.r     = &rec;
        c.font  = &f;
        c.scale = 1.0f;
        c.ui    = ui;
        c.style = &stil;
        c.theme = tema;
        c.area  = {0, 0, 400, 300};
        c.box   = j.resolve(c);
        j.draw(c);
        const std::string fam = std::string(" (") +
                                (ui == glaze::UI::Slate   ? "Slate"
                                 : ui == glaze::UI::Glaze ? "Glaze"
                                                          : "Minimalistic") +
                                ")";
        const auto vid = [&](const std::string& s, float facit,
                             const std::string& vad) {
            const float x = rec.textX(s);
            check(nära(x, facit), vad + fam,
                  std::isnan(x) ? "\"" + s + "\" ritades inte"
                                : std::to_string(x) + " mot " +
                                      std::to_string(facit));
        };
        vid("l", in + pad, "R6: cell Left");
        vid("cc", in + 80 + pad + (80 - 2 * pad - 16) / 2, "R6: cell Center");
        vid("rrr", in + 160 + 80 - pad - 24, "R6: cell Right");
        vid("H1", in + pad + (80 - 2 * pad - 16) / 2,
            "R6: rubrik Center över Left-celler");
        vid("H2", in + 80 + 80 - pad - 16, "R6: rubrik Right över Center-celler");
        vid("H3", in + 160 + pad, "R6: rubrik Left över Right-celler");
    }
}

// R7. RADENS FÄRG i datagriden: de OMARKERADE
// cellerna i en färgad rad ritas i radens färg — också den skrivskyddade
// kolumnen — den AKTUELLA cellen behåller markeringens text, och ett
// inaktiverat rutnät dimmar allt till `textDim`. I alla tre familjerna, med
// den aktuella cellens färg skriven här ur kontraktet (R2/R3/R5).
void fallRadfargen()
{
    const gfx::Color röd{201, 13, 77, 255};   // ingen temafärg
    for (const glaze::UI ui :
         {glaze::UI::Slate, glaze::UI::Glaze, glaze::UI::Minimalistic}) {
        const std::string n = ui == glaze::UI::Slate   ? " (Slate)"
                              : ui == glaze::UI::Glaze ? " (Glaze)"
                                                       : " (Minimalistic)";
        const glaze::Theme::Variant& v   = glaze::defaultTheme(ui).light;
        const auto&                  col = v.colors;
        const gfx::Color aktFg = ui == glaze::UI::Slate   ? col.windowBg
                                 : ui == glaze::UI::Glaze ? col.onAccent
                                                          : col.accent;
        StegFont   f;
        Inspelning rec;
        Prov       g;
        g.width  = 300;
        g.height = 200;
        g.setColumns({{"Uppgift", 80}, {"Id", 60}});
        g.setColumnEditor(1, Editor::None);
        g.setRows({{{"a0", "i0"}, {}, röd}, {{"a1", "i1"}}, {{"a2", "i2"}, {}, röd}});
        g.setCurrentCell(0, 0);

        glaze::Ctx c{};
        c.r     = &rec;
        c.font  = &f;
        c.scale = 1.0f;
        c.ui    = ui;
        c.style = &glaze::defaultStyle(ui);
        c.theme = &v;
        c.area  = {0, 0, 400, 300};
        c.box   = g.resolve(c);
        g.draw(c);
        check(rec.textFärg("i0", röd) && rec.textFärg("a2", röd) &&
                  rec.textFärg("i2", röd),
              "R7: omarkerade celler i en färgad rad — radens färg, också "
              "den skrivskyddade kolumnen" + n);
        check(rec.textFärg("a0", aktFg),
              "R7: den AKTUELLA cellen behåller markeringens text" + n);
        check(rec.textFärg("a1", col.text),
              "R7: en rad utan färg — temats textfärg" + n);

        rec.texter.clear();
        g.setRowColor(1, röd);
        g.draw(c);
        check(rec.textFärg("a1", röd) && rec.textFärg("i1", röd),
              "R7: setRowColor slår igenom i nästa ritning" + n);

        rec.texter.clear();
        g.disable();
        g.draw(c);
        check(rec.textFärg("i0", col.textDim) && rec.textFärg("a0", col.textDim) &&
                  rec.textFärg("a1", col.textDim),
              "R7: inaktiverat — allt i textDim, också de färgade raderna" + n);
    }
}

// S1–S4. CELLENS FÄRGER OCH RADRUBRIKER i datagriden: cellens
// bakgrund fyller exakt cellen och dess text slår radens, MARKERINGEN VINNER
// (den aktuella cellen, och i radläget den valda raden), inaktivt inga
// appfärger; med radrubriker är kolumn 0 en rubrik — den aktuella cellen
// klampas till kolumn 1, Left/Home/Shift+Tab hamnar inte på den, och namnet
// står kvar vid sidledsrullning.
void fallCellfargerOchRadrubriker()
{
    const gfx::Color blå{12, 34, 200, 255};
    const gfx::Color grön{0, 140, 0, 255};
    const gfx::Color gul{240, 220, 10, 255};
    for (const glaze::UI ui :
         {glaze::UI::Slate, glaze::UI::Glaze, glaze::UI::Minimalistic}) {
        const std::string n = ui == glaze::UI::Slate   ? " (Slate)"
                              : ui == glaze::UI::Glaze ? " (Glaze)"
                                                       : " (Minimalistic)";
        const glaze::Theme::Variant& v   = glaze::defaultTheme(ui).light;
        const auto&                  col = v.colors;
        const auto&                  m   = glaze::defaultStyle(ui);
        const gfx::Color aktFg = ui == glaze::UI::Slate   ? col.windowBg
                                 : ui == glaze::UI::Glaze ? col.onAccent
                                                          : col.accent;
        StegFont   f;
        Inspelning rec;
        Prov       g;
        g.width  = 300;
        g.height = 200;
        g.setColumns({{"Uppgift", 80}, {"Id", 60}});
        g.setRows({{{"a0", "i0"}}, {{"a1", "i1"}}, {{"a2", "i2"}}});
        g.setCellColor(1, 0, blå, grön);
        g.setCellColor(2, 1, gul, std::nullopt);
        g.setCurrentCell(0, 0);
        g.setCellColor(0, 0, gul, blå);   // den aktuella cellen

        glaze::Ctx c{};
        c.r     = &rec;
        c.font  = &f;
        c.scale = 1.0f;
        c.ui    = ui;
        c.style = &m;
        c.theme = &v;
        c.area  = {0, 0, 400, 300};
        c.box   = g.resolve(c);
        const float x0 = c.box.x + m.panelInset;
        const float y0 = c.box.y + m.panelInset + m.tableHeaderH;
        g.draw(c);
        check(rec.harFyll({x0, y0 + m.gridRowH, 80.0f, m.gridRowH}, blå) &&
                  rec.textFärg("a1", grön),
              "S1: cellens bakgrund fyller exakt cellen och dess text vinner" + n);
        check(rec.harFyll({x0 + 80.0f, y0 + 2.0f * m.gridRowH, 60.0f, m.gridRowH},
                          gul) &&
                  rec.textFärg("i2", col.text),
              "S1: en cell med bara bakgrund — texten är temats" + n);
        check(!rec.fyllSkär(gul, x0, x0 + 80.0f, y0, y0 + m.gridRowH) &&
                  rec.textFärg("a0", aktFg),
              "S2: den AKTUELLA cellen visar bara markeringens färger" + n);

        rec.fyll.clear();
        rec.texter.clear();
        g.disable();
        g.draw(c);
        check(!rec.fyllSkär(blå, x0, x0 + 400.0f, y0, y0 + 400.0f) &&
                  rec.textFärg("a1", col.textDim),
              "S3: inaktivt — inga appfärger" + n);
    }

    {
        const auto& m = glaze::defaultStyle(glaze::UI::Slate);
        StegFont    f;
        Inspelning  rec;
        Prov        g;
        g.width  = 140;
        g.height = 200;
        g.setColumns({{"Rad", 60}, {"A", 60}, {"B", 60}});
        g.setRows({{{"n0", "x", "y"}}, {{"n1", "x", "y"}}, {{"n2", "x", "y"}}});
        g.setRowHeaders(true);
        g.setCurrentCell(1, 0);
        check(g.currentRow() == 1 && g.currentColumn() == 1,
              "S4: setCurrentCell(…, 0) — radrubriken är aldrig den aktuella "
              "cellen");
        g.moveFromUser(0, -1);
        check(g.currentColumn() == 1,
              "S4: Left från första datakolumnen stannar");
        g.tabFromUser(-1);
        check(g.currentRow() == 0 && g.currentColumn() == 2,
              "S4: Shift+Tab från första datakolumnen går till föregående "
              "rads sista cell");
        g.tabFromUser(1);
        check(g.currentRow() == 1 && g.currentColumn() == 1,
              "S4: Tab från sista cellen går till nästa rads första datakolumn");
        g.setRowHeaders(false);
        g.setCurrentCell(1, 0);
        check(g.currentColumn() == 0, "S4: utan radrubriker gäller kolumn 0");
        g.setRowHeaders(true);

        glaze::Ctx c{};
        c.r     = &rec;
        c.font  = &f;
        c.scale = 1.0f;
        c.ui    = glaze::UI::Slate;
        c.style = &m;
        c.theme = &glaze::defaultTheme(glaze::UI::Slate).light;
        c.area  = {0, 0, 400, 300};
        c.box   = g.resolve(c);
        g.setScroll(1000.0f, 0.0f);
        g.draw(c);
        check(nära(rec.textX("n1"), c.box.x + m.panelInset + m.gridPadX),
              "S4: radnamnet står kvar vid sidledsrullning");
    }
}

// ============================================================ MED FÖNSTER

const char* const kStatus[] = {"Ny", "Pågår", "Klar"};

struct Vard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    Prov             grid;
    glaze::TextInput annat;   // ett andra stopp — Tab ska kunna lämna griden

    Vard() : glaze::AppWindow("datagrid_gate", 520, 360, gfx::Chrome::None)
    {
        grid.x      = 10;
        grid.y      = 10;
        grid.width  = 320;
        grid.height = 220;
        grid.name   = "grid";
        grid.addColumn("Uppgift", 120);
        grid.addColumn("Status", 90);
        grid.addColumn("Id", 60, glaze::Align::Right);
        grid.setColumnEditor(1, Editor::Choice);
        grid.setColumnChoices(1, {"Ny", "Pågår", "Klar"});
        grid.setColumnEditor(2, Editor::None);
        for (int i = 0; i < 30; ++i)
            grid.addRow({"uppgift " + std::to_string(i), kStatus[i % 3],
                         std::to_string(i)});
        root().adopt(grid);

        annat.x     = 350;
        annat.y     = 10;
        annat.width = 120;
        annat.name  = "annat";
        root().adopt(annat);
    }

    // ---- KONTRAKTET, i grindens egen aritmetik (se filhuvudet).
    float      skala() { return window().uiScale(); }
    gfx::Rectf yta() { return grid.resolve(grid.behaviorCtx()); }
    gfx::Rectf inre()
    {
        const gfx::Rectf r = yta();
        const float      d = style().panelInset * skala();
        return gfx::Rectf{r.x + d, r.y + d, r.w - 2 * d, r.h - 2 * d};
    }
    // Huvudets höjd för `rader` rader: `tableHeaderH` och en
    // radhöjd till per rad därutöver — FONTENS radhöjd ur fönstrets egen
    // font, aldrig ur tabellens geometri.
    int   rader = 1;
    float huvudH()
    {
        const float extra =
            rader > 1 ? float(rader - 1) *
                            glaze::Draw::lineHeight(grid.behaviorCtx())
                      : 0.0f;
        return (style().tableHeaderH + extra) * skala();
    }
    float radH() { return style().gridRowH * skala(); }
    // Cellen på PLATS p i kolumn k, fysiska px, med scrollen.
    gfx::Rectf cellYta(int p, int k)
    {
        float x = inre().x - grid.scrollX() * skala();
        for (int i = 0; i < k; ++i)
            x += grid.columnWidth(i) * skala();
        return gfx::Rectf{x,
                          inre().y + huvudH() + float(p) * radH() -
                              grid.scrollY() * skala(),
                          grid.columnWidth(k) * skala(), radH()};
    }
    gfx::Pointf cell(int p, int k)
    {
        const gfx::Rectf r = cellYta(p, k);
        return gfx::Pointf{r.x + r.w / 2.0f, r.y + r.h / 2.0f};
    }
    // Den SYNLIGA editorn (nullptr = ingen).
    glaze::Widget* editorn()
    {
        for (glaze::Widget* b : grid.children())
            if (b->isVisible())
                return b;
        return nullptr;
    }
    gfx::Rectf editorYta()
    {
        glaze::Widget* e = editorn();
        return e ? e->resolve(e->behaviorCtx()) : gfx::Rectf{};
    }
    glaze::Dropdown* vallistan()
    {
        return dynamic_cast<glaze::Dropdown*>(editorn());
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
    void klickAnnat()
    {
        const gfx::Rectf r = annat.resolve(annat.behaviorCtx());
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
        gfx::Event e;
        e.type     = gfx::EventType::TextInput;
        e.windowId = window().id();
        std::snprintf(e.text, sizeof e.text, "%s", utf8);
        handleEvent(e);
    }
};

// ---- Q1–Q2: EDITORN I FYLLKOLUMNEN OCH MED IKONSTORLEK.
//      Datagridens `placeChild` har BARNETS Ctx och skickar innehållets bredd
//      uttryckligen — förväntat värde räknas här ur kontraktet: fyllkolumnen är
//      innehållsytans bredd minus de andra kolumnernas, radhöjden max(gridRowH,
//      storleken + gridRowH − iconFallback). SABOTAGE: `cellInContent` med
//      `resolve()` ur barnets Ctx (Q1 — editorn hamnar fel eller i fel bredd).
//      MÄTER INTE hur editorn ser ut i den högre raden.
void fallFyllkolumnEditor()
{
    Vard w;
    w.setOffscreen(true);
    Prov& g = w.grid;
    // STRÄCKT: bredden kommer ur fönstret, inte ur ett satt mått.
    g.anchors = {true, true, true, false};
    g.right   = 190;
    g.setFillColumn(0);

    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        const float s    = w.skala();
        const float fyll = w.inre().w / s - (g.columnWidth(1) + g.columnWidth(2));
        const auto  facit = [&](int p, float radH) {
            return gfx::Rectf{w.inre().x, w.inre().y + w.huvudH() + float(p) * radH,
                              fyll * s, radH};
        };
        // ---- Q1. Editorn i fyllkolumnen ligger på den EFFEKTIVA cellen.
        const gfx::Rectf c2 = facit(2, w.radH());
        w.klick({c2.x + c2.w / 2.0f, c2.y + c2.h / 2.0f});
        w.klick({c2.x + c2.w / 2.0f, c2.y + c2.h / 2.0f});
        check(g.isEditing() && g.editColumn() == 0 &&
                  sammaRect(w.editorYta(), c2),
              "Q1: editorn i fyllkolumnen == den effektiva cellen",
              fog(w.editorYta()) + " mot " + fog(c2));
        w.tangent(gfx::Key::Escape);

        // ---- Q2. Med `setIconSize` är editorn lika hög som den högre raden.
        g.setIconSize(40.0f);
        const float rh = std::max(w.style().gridRowH,
                                  40.0f + w.style().gridRowH -
                                      w.style().iconFallback) * s;
        const gfx::Rectf c1 = facit(1, rh);
        w.klick({c1.x + c1.w / 2.0f, c1.y + c1.h / 2.0f});
        w.klick({c1.x + c1.w / 2.0f, c1.y + c1.h / 2.0f});
        check(g.isEditing() && std::abs(w.editorYta().h - rh) < 0.51f &&
                  sammaRect(w.editorYta(), c1),
              "Q2: editorns höjd == radhöjden med ikonstorleken",
              fog(w.editorYta()) + " mot " + fog(c1));
        w.tangent(gfx::Key::Escape);
        w.close();
    });
    w.run();
    if (!kordes) {
        std::printf("  --   Q1–Q2 hoppades över: inget fönster kunde skapas\n");
        ++g_skips;
    }
}

void fallMedFonster()
{
    Vard w;
    w.setOffscreen(true);
    Prov& g = w.grid;

    bool                  kordes   = false;
    bool                  valHoppas = false;   // popupen kom aldrig upp
    int                   steg     = 0;
    int                   före     = 0;        // ändringar före ett postat fall
    std::function<void()> nästa;
    nästa = [&] {
        switch (steg++) {
        case 0: {
            kordes = true;
            // ---- E1. KLICK på en cell gör den aktuell (förväntat värde ur
            //          kontraktet: ram + huvud + p × gridrad + kolumnerna).
            w.klick(w.cell(2, 0));
            check(g.currentRow() == 2 && g.currentColumn() == 0 &&
                      g.focused() && !g.isEditing(),
                  "E1: klick på cell (2, 0) — aktuell, fokus, ingen redigering",
                  std::to_string(g.currentRow()) + "," +
                      std::to_string(g.currentColumn()));

            // ---- E2. DUBBELKLICK (det andra trycket) börjar redigera.
            w.klick(w.cell(2, 0));
            auto* fält = dynamic_cast<glaze::TextInput*>(w.editorn());
            check(g.isEditing() && g.editRow() == 2 && g.editColumn() == 0 &&
                      fält && fält->focused() && fält->text() == "uppgift 2",
                  "E2: dubbelklick börjar redigera med cellens text");
            check(sammaRect(w.editorYta(), w.cellYta(2, 0)),
                  "E2: editorn ligger EXAKT på cellen",
                  fog(w.editorYta()) + " mot " + fog(w.cellYta(2, 0)));

            // ---- E3. SKRIV och spara med Enter — ett event, ner en rad.
            w.skriv("-x");
            w.tangent(gfx::Key::Return);
            check(g.cell(2, 0) == "uppgift 2-x" && g.ändringar == 1 &&
                      g.rad == 2 && g.kol == 0 && g.värde == "uppgift 2-x" &&
                      g.avsändare,
                  "E3: Enter sparar — EXAKT ett onChange med rad, kolumn och "
                  "värdet ur sender", std::to_string(g.ändringar));
            check(!g.isEditing() && !w.editorn() && g.focused() &&
                      g.currentRow() == 3 && g.currentColumn() == 0,
                  "E3: …redigeringen är slut, fokus tillbaka, ner en rad");

            // ---- E4. ESCAPE kastar; Enter UTAN ändring fyrar inget.
            w.tangent(gfx::Key::F2);
            w.skriv("zzz");
            w.tangent(gfx::Key::Escape);
            check(!g.isEditing() && g.cell(3, 0) == "uppgift 3" &&
                      g.ändringar == 1 && g.currentRow() == 3 && g.focused(),
                  "E4: F2 + Escape — texten kastas, inget event, kvar på cellen");
            w.tangent(gfx::Key::F2);
            w.tangent(gfx::Key::Return);
            check(g.ändringar == 1 && g.cell(3, 0) == "uppgift 3" &&
                      g.currentRow() == 4,
                  "E4: Enter utan ändring sparar inget och fyrar inget",
                  std::to_string(g.ändringar));
            w.tangent(gfx::Key::Up);

            // ---- E5. ATT SKRIVA börjar redigera — tecknet ERSÄTTER.
            w.skriv("Q");
            fält = dynamic_cast<glaze::TextInput*>(w.editorn());
            check(g.isEditing() && fält && fält->text() == "Q",
                  "E5: ett tecken på en fritext-cell börjar redigera och "
                  "ersätter texten", fält ? fält->text() : "ingen editor");
            w.tangent(gfx::Key::Tab);
            check(g.cell(3, 0) == "Q" && g.ändringar == 2 &&
                      g.currentRow() == 3 && g.currentColumn() == 1 &&
                      !g.isEditing() && g.focused(),
                  "E5: Tab sparar och flyttar EN cell åt höger");

            // ---- E6. VAL-CELLEN: F2 öppnar listan, Ned + Enter väljer.
            w.tangent(gfx::Key::F2);
            glaze::Dropdown* dd = w.vallistan();
            if (!dd || !dd->isOpen()) {
                std::printf("  --   vallistans fall hoppas över: popupen kom "
                            "aldrig upp\n");
                valHoppas = true;
                ++g_skips;
                g.cancelEdit();
                steg = 3;   // vidare till fallen utan popup
                break;
            }
            check(g.isEditing() && dd->focused() &&
                      dd->selectedIndex() == 0,   // rad 3: "Ny"
                  "E6: F2 på en val-cell öppnar listan på cellens värde");
            check(sammaRect(w.editorYta(), w.cellYta(3, 1)),
                  "E6: vallistan ligger på cellen",
                  fog(w.editorYta()) + " mot " + fog(w.cellYta(3, 1)));
            w.tangent(gfx::Key::Down);
            w.tangent(gfx::Key::Return);
            check(g.cell(3, 1) == "Pågår" && g.ändringar == 3 &&
                      g.rad == 3 && g.kol == 1 && !g.isEditing() &&
                      g.focused() && g.currentRow() == 3,
                  "E6: ett NYTT val sparar, fyrar och avslutar — cellen står",
                  g.cell(3, 1));
            break;
        }
        case 1: {
            // ---- E7. ESCAPE i en ÖPPEN lista avbryter i ETT steg.
            w.tangent(gfx::Key::F2);
            glaze::Dropdown* dd = w.vallistan();
            check(dd && dd->isOpen(), "E7: listan öppen igen");
            w.tangent(gfx::Key::Escape);
            check(!g.isEditing() && !w.editorn() && g.focused() &&
                      g.cell(3, 1) == "Pågår" && g.ändringar == 3,
                  "E7: Escape i öppen lista — redigeringen avbruten, inget "
                  "event");
            break;
        }
        case 2: {
            // ---- E8. STÄNGD lista: Upp avslutar och flyttar.
            w.tangent(gfx::Key::F2);
            if (glaze::Dropdown* dd = w.vallistan())
                dd->close();   // uppskjuten — läses i nästa steg
            break;
        }
        case 3: {
            if (!valHoppas) {
                glaze::Dropdown* dd = w.vallistan();
                check(g.isEditing() && dd && !dd->isOpen(),
                      "E8: listan stängd, redigeringen pågår");
                w.tangent(gfx::Key::Up);
                check(!g.isEditing() && g.currentRow() == 2 &&
                          g.currentColumn() == 1 && g.cell(3, 1) == "Pågår" &&
                          g.ändringar == 3,
                      "E8: Upp på en STÄNGD lista avslutar (utan ändring) och "
                      "flyttar upp — värdet byts aldrig av att man navigerar");
            }

            // ---- E9. TAB i tabellens sista cell lämnar griden.
            g.setCurrentCell(29, 2);
            w.tangent(gfx::Key::Tab);
            check(w.annat.focused() && g.currentRow() == 29,
                  "E9: Tab i sista cellen lämnar griden till nästa stopp");
            w.tangent(gfx::Key::Tab, gfx::kModShift);
            check(g.focused(), "E9: Shift+Tab tillbaka — griden är ETT stopp");

            // ---- E10. SKRIVSKYDDAD kolumn: varken F2 eller skrivande.
            g.setCurrentCell(4, 2);
            w.tangent(gfx::Key::F2);
            w.skriv("a");
            check(!g.isEditing() && g.cell(4, 2) == "4",
                  "E10: None-kolumnen går inte att redigera");

            // ---- E11. Upp/Ned i fältet sparar och flyttar.
            g.setCurrentCell(5, 0);
            w.tangent(gfx::Key::F2);
            w.skriv("u");
            w.tangent(gfx::Key::Down);
            check(g.cell(5, 0) == "uppgift 5u" && g.ändringar == 4 &&
                      !g.isEditing() && g.currentRow() == 6 &&
                      g.currentColumn() == 0,
                  "E11: Ned i fältet sparar och flyttar ner en rad",
                  std::to_string(g.ändringar));

            // ---- E12. FOKUSFÖRLUST: cellen skrivs DIREKT, eventet POSTAS.
            g.setCurrentCell(7, 0);
            w.tangent(gfx::Key::F2);
            w.skriv("f");
            före = g.ändringar;
            w.klickAnnat();
            check(!g.isEditing() && g.cell(7, 0) == "uppgift 7f" &&
                      w.annat.focused() && g.ändringar == före,
                  "E12: klick utanför — cellen skriven, men onChange INTE i "
                  "fokusbytet", std::to_string(g.ändringar - före));
            break;
        }
        case 4: {
            check(g.ändringar == före + 1 && g.rad == 7 && g.kol == 0,
                  "E12: …det postade onChange kom i pump-svansen",
                  std::to_string(g.ändringar - före));

            // ---- E13. SLOPAT: raderna byts innan det postade eventet.
            //           (Rad 6 — rad 8 ligger under synfältet, och ett
            //           klick där hade inte träffat griden alls.)
            w.klick(w.cell(6, 0));
            w.tangent(gfx::Key::F2);
            w.skriv("g");
            före = g.ändringar;
            w.klickAnnat();
            std::vector<glaze::Table::Row> kopia = g.rows();
            g.setRows(kopia);
            break;
        }
        case 5: {
            check(g.ändringar == före && g.cell(6, 0) == "uppgift 6g",
                  "E13: setRows före pump-svansen — det postade eventet "
                  "slopas", std::to_string(g.ändringar - före));

            // ---- E14. SORTERING under redigering: editorn följer RADEN.
            w.klick(w.cell(1, 0));
            w.tangent(gfx::Key::F2);
            w.skriv("s");
            g.setSortColumn(0);
            g.setSortAscending(false);
            const int plats = g.displayRow(1);
            check(g.isEditing() && g.editRow() == 1 && plats != 1 &&
                      sammaRect(w.editorYta(), w.cellYta(plats, 0)),
                  "E14: sorterad under redigeringen — editorn på radens NYA "
                  "plats (" + std::to_string(plats) + ")",
                  fog(w.editorYta()) + " mot " + fog(w.cellYta(plats, 0)));
            w.tangent(gfx::Key::Return);
            check(g.cell(1, 0) == "uppgift 1s" && g.rad == 1,
                  "E14: Enter sparar i RADEN (appens index), inte på platsen");
            g.setSortColumn(-1);

            // ---- E15. SCROLL under redigering: editorn följer.
            g.setScroll(0, 0);
            g.setCurrentCell(3, 0);
            w.tangent(gfx::Key::F2);
            const gfx::Rectf r0 = w.editorYta();
            g.setScroll(0, 2.0f * w.style().gridRowH);
            const gfx::Rectf r1 = w.editorYta();
            check(sammaRect(r1, w.cellYta(3, 0)) &&
                      nära(r0.y - r1.y, 2.0f * w.radH()),
                  "E15: scrollad två rader — editorn flyttade med",
                  fog(r0) + " → " + fog(r1));
            w.tangent(gfx::Key::Escape);

            // ---- E16. INAKTIVERAD under redigering — avbryts.
            g.setCurrentCell(2, 0);
            w.tangent(gfx::Key::F2);
            w.skriv("d");
            före = g.ändringar;
            g.disable();
            check(!g.isEditing() && !w.editorn() &&
                      g.cell(2, 0) == "uppgift 2-x" && g.ändringar == före,
                  "E16: disable() avbryter redigeringen utan att spara");
            g.enable();

            // ---- E17. F2 rullar cellen i synfältet.
            g.setScroll(0, 0);
            g.setCurrentCell(25, 0);
            w.tangent(gfx::Key::F2);
            const float vyH = (w.inre().h - w.huvudH()) / w.skala();
            const float väntat = 26.0f * w.style().gridRowH - vyH;
            check(g.isEditing() && nära(g.scrollY(), väntat),
                  "E17: F2 på en utrullad cell rullar den i synfältet",
                  std::to_string(g.scrollY()) + " mot " +
                      std::to_string(väntat));
            w.tangent(gfx::Key::Escape);

            // ---- E19. setCurrentCell under redigering AVBRYTER.
            g.setScroll(0, 0);
            g.setCurrentCell(2, 0);
            w.tangent(gfx::Key::F2);
            w.skriv("c");
            före = g.ändringar;
            g.setCurrentCell(0, 0);
            check(!g.isEditing() && g.cell(2, 0) == "uppgift 2-x" &&
                      g.ändringar == före && g.currentRow() == 0,
                  "E19: setCurrentCell under redigering avbryter utan event");

            // ---- E21. DUBBELKLICK på en skrivskyddad cell redigerar inte.
            w.klick(w.cell(4, 2));
            w.klick(w.cell(4, 2));
            check(!g.isEditing() && g.currentRow() == 4 &&
                      g.currentColumn() == 2,
                  "E21: dubbelklick på None-kolumnen — aktuell, men ingen "
                  "redigering");

            // ---- E22. TAB i sista cellen UNDER redigering: spara + lämna.
            g.setColumnEditor(2, Editor::Text);
            g.setCurrentCell(29, 2);
            w.tangent(gfx::Key::F2);
            w.skriv("t");
            före = g.ändringar;
            w.tangent(gfx::Key::Tab);
            check(g.cell(29, 2) == "29t" && g.ändringar == före + 1 &&
                      !g.isEditing() && w.annat.focused(),
                  "E22: Tab i sista cellen sparar OCH lämnar griden");
            g.setColumnEditor(2, Editor::None);

            // ---- E23. NAVIGERINGEN utan redigering (E22 rullade till
            //           botten — förväntat värde räknar med scrollen, men cellen ska
            //           synas för att klicket ska träffa den).
            g.setScroll(0, 0);
            w.klick(w.cell(5, 1));
            w.tangent(gfx::Key::Return, gfx::kModShift);
            check(g.currentRow() == 4 && g.currentColumn() == 1,
                  "E23: Shift+Enter flyttar upp");
            w.tangent(gfx::Key::End);
            check(g.currentRow() == 4 && g.currentColumn() == 2,
                  "E23: End — radens sista cell");
            w.tangent(gfx::Key::Home);
            check(g.currentRow() == 4 && g.currentColumn() == 0,
                  "E23: Home — radens första cell");
            w.tangent(gfx::Key::End, gfx::kModCtrl);
            check(g.currentRow() == 29 && g.currentColumn() == 2,
                  "E23: Ctrl+End — tabellens sista cell");
            w.tangent(gfx::Key::Home, gfx::kModCtrl);
            check(g.currentRow() == 0 && g.currentColumn() == 0 &&
                      g.scrollY() == 0.0f,
                  "E23: Ctrl+Home — första cellen, rullad tillbaka");
            w.tangent(gfx::Key::Left);
            w.tangent(gfx::Key::Right);
            check(g.currentRow() == 0 && g.currentColumn() == 1,
                  "E23: Vänster i kanten stannar, Höger flyttar");
            w.tangent(gfx::Key::Tab, gfx::kModShift);
            check(g.focused() && g.currentColumn() == 0 && g.currentRow() == 0,
                  "E23: Shift+Tab bakåt i raden");
            w.tangent(gfx::Key::Tab, gfx::kModShift);
            check(!g.focused(),
                  "E23: Shift+Tab i FÖRSTA cellen lämnar griden");

            // ---- E24. DET HÖGA HUVUDET och JUSTERINGEN: under
            //      ett huvud på tre rader ligger editorn på cellen (förväntat värde
            //      följer med, `w.rader`), ett klick träffar rätt cell, och
            //      editorn står i KOLUMNENS justering — fältet och vallistan.
            g.setScroll(0, 0);
            g.setHeaderLines(3);
            w.rader = 3;
            w.klick(w.cell(1, 0));
            check(g.currentRow() == 1 && g.currentColumn() == 0,
                  "E24: ett klick på cell (1, 0) under det höga huvudet "
                  "träffar den", std::to_string(g.currentRow()) + "," +
                                     std::to_string(g.currentColumn()));
            g.setColumn(0, "Uppgift", 120, glaze::Align::Center);
            g.setCurrentCell(2, 0);
            w.tangent(gfx::Key::F2);
            auto* f24 = dynamic_cast<glaze::TextInput*>(w.editorn());
            check(f24 && sammaRect(w.editorYta(), w.cellYta(2, 0)),
                  "E24: editorn ligger EXAKT på cellen under ett huvud på tre "
                  "rader", fog(w.editorYta()) + " mot " + fog(w.cellYta(2, 0)));
            check(f24 && f24->align() == glaze::Align::Center,
                  "E24: fältet står i kolumnens justering (Center)");
            w.tangent(gfx::Key::Escape);
            g.setColumn(0, "Uppgift", 120, glaze::Align::Right);
            w.tangent(gfx::Key::F2);
            f24 = dynamic_cast<glaze::TextInput*>(w.editorn());
            check(f24 && f24->align() == glaze::Align::Right,
                  "E24: …och följer med när kolumnen byter justering (Right)");
            w.tangent(gfx::Key::Escape);
            g.setColumn(0, "Uppgift", 120);   // tillbaka till Left
            w.tangent(gfx::Key::F2);
            f24 = dynamic_cast<glaze::TextInput*>(w.editorn());
            check(f24 && f24->align() == glaze::Align::Left,
                  "E24: …och tillbaka till Left");
            w.tangent(gfx::Key::Escape);
            g.setColumn(1, "Status", 90, glaze::Align::Right);
            g.setCurrentCell(1, 1);
            w.tangent(gfx::Key::F2);
            glaze::Dropdown* dd24 = w.vallistan();
            check(dd24 && dd24->align() == glaze::Align::Right &&
                      sammaRect(w.editorYta(), w.cellYta(1, 1)),
                  "E24: vallistan står i kolumnens justering (Right), på cellen",
                  fog(w.editorYta()) + " mot " + fog(w.cellYta(1, 1)));
            g.cancelEdit();
            g.setHeaderLines(1);
            w.rader = 1;

            // ---- E25. Editorns ÅNGRA-HISTORIK
            //      följer inte med till nästa cell: datagriden återanvänder
            //      sin editor, och `setText`s likhetsvakt hade bevarat
            //      historiken när nästa cell har samma text. Cell (0, 0) "a"
            //      → "b", cell (1, 0) är "b": Ctrl+Z där får inte ge "a".
            g.setScroll(0, 0);
            g.setCell(0, 0, "a");
            g.setCell(1, 0, "b");
            g.setCurrentCell(0, 0);
            w.tangent(gfx::Key::F2);
            w.tangent(gfx::Key::Backspace);
            w.skriv("b");
            w.tangent(gfx::Key::Return);   // sparar och går ner en rad
            w.tangent(gfx::Key::F2);
            auto* f25 = dynamic_cast<glaze::TextInput*>(w.editorn());
            const bool fanns = f25 && f25->canUndo();
            w.tangent(gfx::Key::Z, kRedigera);
            check(g.cell(0, 0) == "b" && g.editRow() == 1 && f25 && !fanns &&
                      f25->text() == "b",
                  "E25: en ny cell börjar med TOM historik — Ctrl+Z spelar inte "
                  "upp förra cellens redigering",
                  f25 ? "'" + f25->text() + "'" : std::string("ingen editor"));
            g.cancelEdit();
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

    std::printf("datagrid_gate\n");
    fallRegistret();
    fallKolumntypen();
    fallAktuellCell();
    fallRadvalet();
    fallRitningen();
    fallRadfargen();
    fallCellfargerOchRadrubriker();
    fallMedFonster();
    fallFyllkolumnEditor();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
