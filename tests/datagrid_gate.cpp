// datagrid_gate — DATAGRIDDENS grind.
//
// Kör:  ./build/tests/datagrid_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = ett block kunde
//       inte köras (ingen display, eller vallistans popup kom aldrig upp) —
//       ctest räknar det som SKIPPED, aldrig grönt
//
// TVÅDELAD:
//
//   HEADLESS     registret och kompositen (tre DOLDA editorer som inte går
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
// DE TYPADE CELLERNA: H6 (cellens redigerare och aktivering före kolumnens,
// `setRow` nollar, `setCell` lämnar), T1–T9 headless (krysset, läsvägarna,
// låst och inaktiv cell, knappen från tangentbordet, Tab, rubrikkrysset,
// flera rader, talsorteringen, kategorier och tomrad) och W1–W12 med fönster
// (krysset och knappen med musen, `editOnClick`, Enter, vallistan med ett
// klick, talet med klampning, fel och tooltip, `allowEmpty`, cellens gränser
// och enhet, färgväljaren och `commitEdits()`, flera rader med musen och
// kategorins klick som sparar först). D1–D6 headless (rubrikkrysset i den
// sorterade kryss-kolumnen, en dold rad som aldrig blir aktuell eller vald,
// valets event efter användarens ihopfällning och en ny redigerare i
// sorteringskolumnen som sorterar om), AM1–AM5 (AutoMin för kryss, färg i
// de tre lägena, tal med och utan enhet och knapp) och TT1–TT5 (cellens
// tooltip: fel > cell > rubrik > widget). L1–L2 headless och L3 med fönster
// (en LÅST cell, `readOnly` i specen: rubrikkrysset räknar och vänder den
// inte, Space växlar den inte, Tab hoppar över den, och Enter, F2, ett
// skrivet tal och ett klick redigerar den inte) och P11 (den låsta talcellen
// ritar prefixet, talet och suffixet på `fieldReadOnly`). M1–M2 med fönster (`selectOnEdit`:
// hela texten markerad eller markören sist efter Enter och ett klick, och ett
// skrivet tecken som ersätter texten i båda lägena). W13–W14 med fönster
// (Enter i den öppna vallistan och den stängda färgväljaren, editorn bredvid
// cellens bild och radens ikon) och P1–P11 som räknar PIXLAR i fönsterbilden
// (kryssrutan, färgrutan, talet med prefix och suffix, felets yta, knappens
// yta, valets ikon, kategoriraden, tomraden, rubrikkryssets tre lägen,
// färgcellens tre lägen och den låsta talcellen). Sabotaget står vid varje fall. Provets
// datagrid (`Prov`) börjar med `selectOnEdit` av, så redigeringsfallen skriver
// efter cellens text.
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
//     textfärgen per kolumntyp); P-fallen mäter de typade cellernas pixlar
//     i fönstrets förvalda UI och tema, inte i alla.
//   * att en handler som RIVER datagriden i `onCollapse` stoppar valets event
//     (D4): vakten finns (`fireEvent`s svar), men ett sabotage där är
//     odefinierat beteende och fälls inte säkert.
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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
    // De typade cellernas tangenter och rubrikkrysset.
    using glaze::DataGrid::keyDown;
    using glaze::DataGrid::textInput;
    using glaze::DataGrid::headerCheckState;
    using glaze::DataGrid::headerCheckToggled;
    // AutoMin-bredden för de typade cellerna.
    using glaze::DataGrid::columnW;

    int              ändringar = 0;
    int              commits   = 0;
    int              rad       = -1;
    int              kol       = -1;
    std::vector<int> rader;          // `e.rows` i senaste onChange
    std::string      värde;
    bool             avsändare = true;   // var sender datagriden varje gång?
    int              klick     = 0;      // onCellClick
    int              klickRad  = -1;
    int              klickKol  = -1;
    int              ut        = 0;      // onExpand
    int              in        = 0;      // onCollapse
    // Redigeringsfallen skriver EFTER cellens text (F2 + "e" ger
    // "uppgift 6e"), så provet börjar med markören sist och utan markering;
    // M1–M2 mäter förvalet och båda lägena uttryckligen.
    Prov()
    {
        setSelectOnEdit(false);
        onChange = [this](glaze::Widget* s, glaze::CellChangeArgs& e) {
            ++ändringar;
            rad   = e.row;
            kol   = e.column;
            rader = e.rows;
            värde = cell(e.row, e.column);
            avsändare = avsändare && s == this;
        };
        onCommit = [this](glaze::Widget*, glaze::CellChangeArgs&) {
            ++commits;
        };
        onCellClick = [this](glaze::Widget*, glaze::CellClickArgs& e) {
            ++klick;
            klickRad = e.row;
            klickKol = e.column;
        };
        onExpand   = [this] { ++ut; };
        onCollapse = [this] { ++in; };
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

// ------------------------------------------------------ fönstrets bild
// dropdown_gate-formen: fönstret ritas nu och läses tillbaka (RGBA8, rad 0
// överst, fysiska px). Tom vid ett fel.
struct Bild {
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;
    std::uint32_t at(int x, int y) const
    {
        if (x < 0 || y < 0 || x >= w || y >= h)
            return 0;
        const std::size_t i =
            (std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4;
        return std::uint32_t(px[i]) | std::uint32_t(px[i + 1]) << 8 |
               std::uint32_t(px[i + 2]) << 16;
    }
};
Bild bildAv(glaze::AppWindow& w)
{
    Bild b;
    w.invalidate();
    w.window().renderFrame();
    if (!w.window().captureFrame(b.px, b.w, b.h))
        b = Bild{};
    return b;
}
// Pixeln är färgen `c`, inom `tol` per kanal.
bool nästan(std::uint32_t p, gfx::Color c, int tol = 3)
{
    return std::abs(int(p & 0xFFu) - int(c.r)) <= tol &&
           std::abs(int((p >> 8) & 0xFFu) - int(c.g)) <= tol &&
           std::abs(int((p >> 16) & 0xFFu) - int(c.b)) <= tol;
}
// Pixlarna i rutan (fysiska px) som skiljer sig från ytans pixel — bläcket.
int bläck(const Bild& b, const gfx::Rectf& r, std::uint32_t yta)
{
    int n = 0;
    for (int y = int(r.y); y < int(r.y + r.h); ++y)
        for (int x = int(r.x); x < int(r.x + r.w); ++x)
            n += b.at(x, y) != yta ? 1 : 0;
    return n;
}
// Pixlarna i rutan i färgen `c` — och de som INTE är den.
int iFärg(const Bild& b, const gfx::Rectf& r, gfx::Color c)
{
    int n = 0;
    for (int y = int(r.y); y < int(r.y + r.h); ++y)
        for (int x = int(r.x); x < int(r.x + r.w); ++x)
            n += nästan(b.at(x, y), c) ? 1 : 0;
    return n;
}
int utanförFärg(const Bild& b, const gfx::Rectf& r, gfx::Color c)
{
    int n = 0;
    for (int y = int(r.y); y < int(r.y + r.h); ++y)
        for (int x = int(r.x); x < int(r.x + r.w); ++x)
            n += nästan(b.at(x, y), c) ? 0 : 1;
    return n;
}
// Bläckets högraste kolumn i rutan, −1 utan bläck.
int högerkant(const Bild& b, const gfx::Rectf& r, std::uint32_t yta)
{
    int x1 = -1;
    for (int y = int(r.y); y < int(r.y + r.h); ++y)
        for (int x = int(r.x); x < int(r.x + r.w); ++x)
            if (b.at(x, y) != yta)
                x1 = std::max(x1, x);
    return x1;
}
// Två bilder som skiljer sig i rutan — antalet olika pixlar.
int skillnad(const Bild& a, const Bild& b, const gfx::Rectf& r)
{
    int n = 0;
    for (int y = int(r.y); y < int(r.y + r.h); ++y)
        for (int x = int(r.x); x < int(r.x + r.w); ++x)
            n += a.at(x, y) != b.at(x, y) ? 1 : 0;
    return n;
}
gfx::Rectf indragen(const gfx::Rectf& r, float d)
{
    return gfx::Rectf{r.x + d, r.y + d, r.w - 2.0f * d, r.h - 2.0f * d};
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
    bool dolda = g.children().size() == 3;
    for (const glaze::Widget* b : g.children())
        dolda = dolda && !b->isVisible();
    check(dolda, "H1: exakt tre editorer (fält, vallista, färgväljare), alla "
                 "dolda i vila");
    bool fasta = true;
    for (std::size_t i = 0; i < 3 && i < g.children().size(); ++i)
        fasta = fasta && !g.unadopt(*g.children()[i]);
    check(fasta && g.children().size() == 3,
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
    check(g.columnEditor(0).editor == Editor::Text &&
              g.columnEditor(9).editor == Editor::Text &&
              g.columnEditor(-1).editor == Editor::Text &&
              g.columnEditor(3).choices.empty(),
          "H3: ett index utan inställning är fritext utan val");
    g.setColumnEditor(2, {.editor = Editor::None});
    g.setColumnEditor(1, {.editor = Editor::Choice, .choices = {"a", "b"}});
    g.setColumnEditor(-1, {.editor = Editor::None});
    check(g.columnEditor(2).editor == Editor::None &&
              g.columnEditor(1).editor == Editor::Choice &&
              g.columnEditor(1).choices == std::vector<std::string>{"a", "b"} &&
              g.columnEditor(0).editor == Editor::Text,
          "H3: typen sätts FÖRE kolumnerna finns (satsordningen avgör inte)");
    g.addColumn("A");
    g.addColumn("B");
    g.addColumn("C");
    g.setColumn(1, "B2", 50);
    g.setColumns({{"X"}, {"Y"}, {"Z"}, {"W"}});
    check(g.columnEditor(1).editor == Editor::Choice &&
              g.columnEditor(2).editor == Editor::None &&
              g.columnEditor(1).choices.size() == 2 &&
              g.columnEditor(3).editor == Editor::Text,
          "H3: typen hör till INDEXET — den överlever addColumn, setColumn "
          "och setColumns");
    g.setColumnEditor(1, {});
    check(g.columnEditor(1).editor == Editor::Text &&
              g.columnEditor(1).choices.empty(),
          "H3: typen och valen går att nollställa");

    // H6 — CELLENS redigerare går före kolumnens, `clearCellEditor` lämnar
    // tillbaka cellen; `setRow` nollar cellinställningarna, `setCell` lämnar
    // dem. SABOTAGE: `cellEditor` som alltid svarar kolumnens, eller
    // `rowReplaced` som inte nollar radens inställningar.
    g.setRows({{{"a", "b"}}, {{"c", "d"}}});
    g.setColumnEditor(1, {.editor = Editor::Number, .max = 10});
    g.setCellEditor(0, 1, {.editor = Editor::None});
    g.setCellEnabled(1, 1, false);
    g.setCellEditor(5, 1, {.editor = Editor::None});   // utanför: no-op
    check(g.cellEditor(0, 1).editor == Editor::None &&
              g.cellEditor(1, 1).editor == Editor::Number &&
              g.cellEditor(1, 1).max == 10 && !g.isCellEnabled(1, 1) &&
              g.isCellEnabled(0, 1) && !g.isCellEnabled(7, 1),
          "H6: cellens redigerare och aktivering går före kolumnens");
    g.setCell(0, 1, "x");
    check(g.cellEditor(0, 1).editor == Editor::None,
          "H6: setCell lämnar cellens inställningar");
    g.setRow(0, {"a", "b"});
    g.setRow(1, {"c", "d"});
    check(g.cellEditor(0, 1).editor == Editor::Number && g.isCellEnabled(1, 1),
          "H6: setRow nollar radens cellinställningar (tilldelning)");
    g.setCellEditor(0, 1, {.editor = Editor::Check});
    g.clearCellEditor(0, 1);
    check(g.cellEditor(0, 1).editor == Editor::Number,
          "H6: clearCellEditor lämnar tillbaka cellen till kolumnens");
    check(g.setProp("editOnClick", "true") && g.editOnClick() &&
              g.setProp("multiEdit", "true") && g.isMultiEdit() &&
              g.setProp("editOnClick", "false") && !g.editOnClick(),
          "H6: setProp editOnClick och multiEdit");
    // SABOTAGE: `selectOnEdit_` med förvalet false, eller `setProp` utan
    // grenen för `selectOnEdit`.
    {
        glaze::DataGrid f;
        check(f.selectOnEdit() && f.setProp("selectOnEdit", "false") &&
                  !f.selectOnEdit() && f.setProp("selectOnEdit", "true") &&
                  f.selectOnEdit(),
              "H6: selectOnEdit är true som förval, och setProp når den");
    }
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
    g.setColumnEditor(1, {.editor = Editor::Choice, .choices = {"Ny", "Klar"}});
    g.setColumnEditor(2, {.editor = Editor::None});
    g.setColumnEditor(3, {.editor = Editor::Choice});   // UTAN val — inte redigerbar
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
    bool roCeller = true;
    for (int p = 0; p < 3; ++p)
        roCeller = roCeller &&
                   rec.harFyll({x[2], y0 + float(p) * rh, 60, rh}, ro) &&
                   rec.harFyll({x[3], y0 + float(p) * rh, 50, rh}, ro);
    check(roCeller && !rec.fyllSkär(ro, x[2], x[4], y0 + 3 * rh, y0 + 4 * rh),
          "R1: Slate — den skrivskyddade kolumnen och val-kolumnen UTAN val "
          "ritas som skrivskyddade fält PER CELL, till sista raden (gridraden "
          "31, inte listradens 19) och inte under den");
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
    check(rec.harFyll({x[2], y0 + rh, 60, rh}, mini.colors.fieldReadOnly) &&
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
    check(rec.harFyll({x[2], y0 + rh, 60, rh}, glz.colors.fieldReadOnly) &&
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
        g.setColumnEditor(1, {.editor = Editor::None});
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

// T1–T9. DE TYPADE CELLERNA headless — tangenterna (som inte kräver ett
// fönster), läsvägarna, aktiveringen per cell, knappen, Tab, rubrikkrysset,
// flera rader, talsorteringen, kategorierna och tomraden.
void fallTypadeCeller()
{
    using V       = std::vector<int>;
    using SM      = glaze::DataGrid::SelectionMode;
    using RowKind = glaze::Table::RowKind;
    using CS      = glaze::CheckState;

    // T1 KRYSSET. SABOTAGE: `activateCell` som släpper Space i en krysscell
    // (ingen växling), eller som växlar på Enter (flyttar inte ner).
    {
        Prov g;
        g.setColumns({{"Namn"}, {"På"}});
        g.setColumnEditor(1, {.editor = Editor::Check});
        g.setRows({{{"a", "true"}}, {{"b", "false"}}, {{"c", "yes"}}});
        g.setCurrentCell(1, 1);
        check(g.keyDown(gfx::Key::Space, 0) && g.cell(1, 1) == "true" &&
                  g.isChecked(1, 1) && g.ändringar == 1 && g.commits == 1 &&
                  g.rad == 1 && g.kol == 1 && g.rader == V{1},
              "T1: Space i en krysscell växlar och sparar — ETT onChange + "
              "onCommit med rows {1}", g.cell(1, 1));
        g.setCurrentCell(0, 1);
        g.keyDown(gfx::Key::Return, 0);
        check(g.currentRow() == 1 && g.cell(0, 1) == "true" && g.ändringar == 1,
              "T1: Enter i en krysscell flyttar ner utan att växla");
        check(g.isChecked(2, 1) && !g.isChecked(0, 0) && !g.isChecked(9, 1),
              "T1: isChecked läser \"yes\"; aldrig i en textcell eller utanför");
    }
    // T2 LÄSVÄGARNA. SABOTAGE: `colorValue` utan opak-regeln, `numberValue`
    // utan klampningen, eller `hasError` som inte frågar `allowEmpty`.
    {
        Prov g;
        g.setColumns({{"Färg"}, {"Tal"}});
        g.setColumnEditor(0, {.editor = Editor::Color});
        g.setColumnEditor(1, {.editor   = Editor::Number,
                              .min      = 0,
                              .max      = 10,
                              .decimals = 1});
        g.setRows({{{"#3366FF80", "12.34"}}, {{"lila", "abc"}}, {{"#3366FF", ""}}});
        const auto f = g.colorValue(0, 0);
        check(f && f->a == 255 && f->b == 255,
              "T2: colorValue utan allowAlpha är opak");
        g.setCellEditor(0, 0, {.editor = Editor::Color, .allowAlpha = true});
        const auto fa = g.colorValue(0, 0);
        check(fa && fa->a == 0x80, "T2: …med cellens allowAlpha bär den alfan");
        check(g.hasError(1, 0) && !g.colorValue(1, 0) && !g.hasError(2, 0),
              "T2: en otolkbar färg är ett fel");
        const auto tal = g.numberValue(0, 1);
        check(tal && *tal == 10.0, "T2: numberValue klampas (12.34 → 10)");
        check(g.hasError(1, 1) && !g.numberValue(1, 1) && g.hasError(2, 1),
              "T2: otolkbar text och tom text utan allowEmpty är fel");
        g.setCellEditor(2, 1, {.editor     = Editor::Number,
                               .allowEmpty = true,
                               .emptyText  = "auto"});
        check(!g.hasError(2, 1) && !g.numberValue(2, 1),
              "T2: tom text med allowEmpty — inget fel och inget tal");
        g.setCell(0, 1, "1,25");
        const auto komma = g.numberValue(0, 1);
        g.setCell(0, 1, "1.25");
        const auto punkt = g.numberValue(0, 1);
        check(komma && punkt && *komma == *punkt && *punkt == 1.3,
              "T2: \",\" och \".\" läses lika, avrundat till en decimal");
        check(!g.hasError(0, 7) && !g.hasError(9, 0) && !g.numberValue(9, 1),
              "T2: utanför — inget fel och inget tal");
    }
    // T3 LÅST OCH INAKTIV CELL i en kryss-kolumn. SABOTAGE: `editable` som
    // inte frågar cellens redigerare eller `isCellEnabled`.
    {
        Prov g;
        g.setColumns({{"Namn"}, {"På"}});
        g.setColumnEditor(1, {.editor = Editor::Check});
        g.setRows({{{"a", "false"}}, {{"b", "false"}}, {{"c", "false"}}});
        g.setCellEditor(0, 1, {.editor = Editor::None});
        g.setCellEnabled(1, 1, false);
        g.setCurrentCell(0, 1);
        g.keyDown(gfx::Key::Space, 0);
        g.setCurrentCell(1, 1);
        g.keyDown(gfx::Key::Space, 0);
        check(g.cell(0, 1) == "false" && g.cell(1, 1) == "false" &&
                  g.ändringar == 0,
              "T3: en låst None-cell och en inaktiv cell växlar inte");
        g.setCurrentCell(0, 1);
        g.keyDown(gfx::Key::Return, 0);
        check(g.currentRow() == 1, "T3: Enter i en låst cell flyttar ner");
    }
    // T4 KNAPPEN från tangentbordet. SABOTAGE: `clickCellFromUser` utan
    // `pressable` (en inaktiv knapp fyrar), eller Enter som flyttar ner.
    {
        Prov g;
        g.setColumns({{"Namn"}, {"Ta bort"}});
        g.setColumnEditor(1, {.editor = Editor::Button});
        g.setRows({{{"a", "X"}}, {{"b", "X"}}});
        g.setCurrentCell(0, 1);
        g.keyDown(gfx::Key::Return, 0);
        g.keyDown(gfx::Key::Space, 0);
        check(g.klick == 2 && g.klickRad == 0 && g.klickKol == 1 &&
                  g.cell(0, 1) == "X" && g.ändringar == 0 && g.currentRow() == 0,
              "T4: Enter och Space på en knappcell fyrar onCellClick — värdet "
              "orört, cellen står", std::to_string(g.klick));
        g.setCellEnabled(0, 1, false);
        g.keyDown(gfx::Key::Space, 0);
        check(g.klick == 2, "T4: en inaktiv knappcell fyrar inte");
    }
    // T5 TAB till nästa redigerbara cell. SABOTAGE: `tabFromUser` utan
    // `editable` (landar i None-, knapp- eller kategoricellen) eller som slår
    // runt i sista cellen.
    {
        Prov g;
        g.setColumns({{"A"}, {"B"}, {"C"}, {"D"}});
        g.setColumnEditor(1, {.editor = Editor::None});
        g.setColumnEditor(2, {.editor = Editor::Button});
        g.setColumnEditor(3, {.editor = Editor::Check});
        g.setRows({{{"a0", "b", "c", "false"}},
                   {.cells = {"Grupp"}, .kind = RowKind::Category},
                   {{"a2", "b", "c", "false"}}});
        g.setCurrentCell(0, 0);
        const bool t1 = g.tabFromUser(1);
        const int  r1 = g.currentRow(), k1 = g.currentColumn();
        const bool t2 = g.tabFromUser(1);
        const int  r2 = g.currentRow(), k2 = g.currentColumn();
        const bool t3 = g.tabFromUser(1);
        const int  r3 = g.currentRow(), k3 = g.currentColumn();
        const bool t4 = g.tabFromUser(1);
        check(t1 && r1 == 0 && k1 == 3,
              "T5: Tab hoppar över None- och knappcellen",
              std::to_string(r1) + "," + std::to_string(k1));
        check(t2 && r2 == 2 && k2 == 0, "T5: …och över kategoriraden",
              std::to_string(r2) + "," + std::to_string(k2));
        check(t3 && r3 == 2 && k3 == 3 && !t4 && g.currentRow() == 2,
              "T5: ingen redigerbar cell kvar — Tab lämnar datagriden");
        g.tabFromUser(-1);
        const int r5 = g.currentRow(), k5 = g.currentColumn();
        g.tabFromUser(-1);
        check(r5 == 2 && k5 == 0 && g.currentRow() == 0 &&
                  g.currentColumn() == 3,
              "T5: Shift+Tab samma väg bakåt");
    }
    // T6 RUBRIKKRYSSET. SABOTAGE: läget räknat en gång och aldrig igen (den
    // andra frågan svarar Delvis), en vändning som fyrar per rad, eller som
    // räknar och ändrar en inaktiv cell.
    {
        Prov g;
        g.setColumns({{"Namn"}, {"På"}});
        g.setColumnEditor(1, {.editor = Editor::Check});
        g.setColumnHeaderCheck(1, true);
        g.setRows({{{"a", "true"}}, {{"b", "false"}}, {{"c", "true"}}});
        check(g.headerCheckState(1) == CS::Indeterminate &&
                  !g.headerCheckState(0),
              "T6: Delvis kryssad; ingen kryssruta i en textkolumn");
        check(g.headerCheckToggled(1) && g.ändringar == 1 && g.commits == 1 &&
                  g.rad == -1 && g.kol == 1 && g.rader == V{1} &&
                  g.isChecked(1, 1),
              "T6: vändningen från Delvis kryssar alla — ETT event, row −1, "
              "rows = de ändrade", radlista(g.rader));
        check(g.headerCheckState(1) == CS::Checked,
              "T6: alla kryssade — Kryssad (räkningen följer ändringen)");
        g.headerCheckToggled(1);
        check(g.ändringar == 2 && g.rader == V{0, 1, 2} &&
                  g.headerCheckState(1) == CS::Unchecked,
              "T6: vändningen från Kryssad tar bort alla — ETT event",
              radlista(g.rader));
        g.setCell(0, 1, "true");
        g.setCell(1, 1, "true");
        g.setCellEnabled(2, 1, false);
        check(g.headerCheckState(1) == CS::Checked,
              "T6: en inaktiv krysscell räknas inte");
        g.headerCheckToggled(1);
        check(g.rader == V{0, 1} && g.cell(2, 1) == "false",
              "T6: …och ändras inte av vändningen", radlista(g.rader));
    }
    // L1 EN LÅST KRYSSCELL (`readOnly` i cellens spec) räknas inte av
    // rubrikkrysset, ändras inte av vändningen och växlar inte med Space.
    // SABOTAGE: `skriverVärde` som inte frågar `readOnly` (cellen räknas och
    // vänds som en vanlig krysscell).
    {
        Prov g;
        g.setColumns({{"Namn"}, {"På"}});
        g.setColumnEditor(1, {.editor = Editor::Check});
        g.setColumnHeaderCheck(1, true);
        g.setRows({{{"a", "true"}}, {{"b", "true"}}, {{"c", "false"}}});
        g.setCellEditor(2, 1, {.editor = Editor::Check, .readOnly = true});
        check(g.headerCheckState(1) == CS::Checked,
              "L1: rubrikkrysset räknar inte den låsta krysscellen");
        g.headerCheckToggled(1);
        check(g.rader == V{0, 1} && g.cell(2, 1) == "false" &&
                  !g.isChecked(0, 1) && !g.isChecked(1, 1),
              "L1: …och vändningen rör den inte", radlista(g.rader));
        g.setCurrentCell(2, 1);
        g.keyDown(gfx::Key::Space, 0);
        check(g.cell(2, 1) == "false" && g.ändringar == 1,
              "L1: Space växlar inte en låst krysscell", g.cell(2, 1));
    }
    // L2 TAB HOPPAR ÖVER EN LÅST CELL, åt båda hållen, och dess läsväg är
    // orörd. SABOTAGE: som L1 (`editable` svarar sant för den låsta cellen,
    // och Tab stannar på den).
    {
        Prov g;
        g.setColumns({{"A"}, {"B"}, {"C"}});
        g.setColumnEditor(1, {.editor   = Editor::Number,
                              .prefix   = "ca ",
                              .suffix   = " mm",
                              .readOnly = true});
        g.setRows({{{"x", "12", "y"}}});
        g.setCurrentCell(0, 0);
        g.tabFromUser(1);
        const int fram = g.currentColumn();
        g.tabFromUser(-1);
        check(fram == 2 && g.currentColumn() == 0 && !g.isEditing() &&
                  g.numberValue(0, 1) == 12.0,
              "L2: Tab och Shift+Tab hoppar över den låsta talcellen",
              std::to_string(fram) + " / " + std::to_string(g.currentColumn()));
    }
    // T7 FLERA RADER PÅ EN GÅNG. SABOTAGE: `editTargets` som bara svarar den
    // redigerade raden, som tar med en cell med en annan redigerare, eller
    // `rows` med alla valda i stället för de ändrade.
    {
        Prov g;
        g.setColumns({{"Namn"}, {"På"}});
        g.setColumnEditor(1, {.editor = Editor::Check});
        g.setRows({{{"a", "false"}}, {{"b", "false"}}, {{"c", "false"}},
                   {{"d", "false"}}, {{"e", "true"}}});
        g.setSelectionMode(SM::FullRow);
        g.setMultiSelect(true);
        g.setMultiEdit(true);
        g.setSelected(0, true);
        g.setSelected(2, true);
        g.setSelected(3, true);
        g.setSelected(4, true);
        g.setCellEditor(3, 1, {.editor = Editor::None});
        g.setCurrentCell(2, 1);
        g.keyDown(gfx::Key::Space, 0);
        check(g.isChecked(0, 1) && !g.isChecked(1, 1) && g.isChecked(2, 1) &&
                  g.cell(3, 1) == "false" && g.ändringar == 1 && g.rad == 2 &&
                  g.rader == V{0, 2},
              "T7: multiEdit — de valda med samma sorts redigerare skrivs, ETT "
              "event med de ÄNDRADE i rows (visningsordning); None-cellen och "
              "den redan kryssade hoppas över", radlista(g.rader));
        g.setMultiEdit(false);
        g.keyDown(gfx::Key::Space, 0);
        check(!g.isChecked(2, 1) && g.isChecked(0, 1) && g.rader == V{2},
              "T7: utan multiEdit bara cellen", radlista(g.rader));
    }
    // T8 TALSORTERINGEN. SABOTAGE: `cellSortValue` som svarar nullopt för en
    // talcell (naturlig textordning: "1.5" före "1.25", "-1" före "-5").
    {
        Prov g;
        g.setColumns({{"Tal"}});
        g.setColumnEditor(0, {.editor = Editor::Number});
        g.setRows({{{"1.5"}}, {{"1.25"}}, {{"-5"}}, {{"3"}}, {{""}}, {{"-1"}}});
        g.setSortColumn(0);
        V ordning;
        for (int p = 0; p < 6; ++p)
            ordning.push_back(g.rowAtDisplay(p));
        check(ordning == V{2, 5, 1, 0, 3, 4},
              "T8: talcellen sorteras på värdet — \"-5\" före \"3\", \"1.25\" "
              "före \"1.5\", och den tomma sist", radlista(ordning));
    }
    // T9 KATEGORIER OCH TOMRAD. SABOTAGE: `moveFromUser` som landar på den
    // tomma raden, `rowsHidden` som inte släpper valet, Vänster/Höger som
    // flyttar kolumnen på en kategorirad, eller ett val av kategoriraden.
    {
        Prov g;
        g.setColumns({{"A"}, {"B"}});
        g.setRows({{{"x", "1"}},
                   {.cells = {"Grupp"}, .kind = RowKind::Category},
                   {{"y", "2"}},
                   {.kind = RowKind::Blank},
                   {{"z", "3"}}});
        g.setCurrentCell(3, 0);
        check(g.currentRow() == -1, "T9: en tom rad blir aldrig aktuell");
        g.setCurrentCell(2, 0);
        g.moveFromUser(1, 0);
        check(g.currentRow() == 4, "T9: Ned hoppar över den tomma raden");
        g.moveFromUser(-1, 0);
        check(g.currentRow() == 2, "T9: Upp likaså");
        g.moveFromUser(-1, 0);
        check(g.currentRow() == 1, "T9: en kategorirad kan vara aktuell");
        g.setSelectionMode(SM::FullRow);
        g.setMultiSelect(true);
        g.setSelected(2, true);
        g.setCurrentCell(1, 1);
        g.keyDown(gfx::Key::Left, 0);
        check(!g.isExpanded(1) && g.in == 1 && g.ut == 0 && !g.isSelected(2) &&
                  g.displayRow(2) == -1 && g.currentColumn() == 1,
              "T9: Vänster på kategoriraden fäller ihop — onCollapse, den "
              "dolda raden väljs bort");
        g.keyDown(gfx::Key::Left, 0);
        g.keyDown(gfx::Key::Right, 0);
        check(g.isExpanded(1) && g.in == 1 && g.ut == 1,
              "T9: Vänster på en ihopfälld gör inget, Höger fäller ut — "
              "onExpand");
        g.keyDown(gfx::Key::Return, 0);
        check(!g.isExpanded(1) && g.in == 2 && g.currentRow() == 1,
              "T9: Enter fäller en kategorirad");
        g.setSelected(1, true);
        check(!g.isSelected(1), "T9: en kategorirad väljs aldrig");
    }
}

// D1–D6. RUBRIKKRYSSET I SORTERINGSKOLUMNEN, DOLDA RADER OCH ORDNINGEN —
// headless. Sabotaget står vid varje fall.
void fallDoldaRaderOchOrdningen()
{
    using V       = std::vector<int>;
    using SM      = glaze::DataGrid::SelectionMode;
    using RowKind = glaze::Table::RowKind;

    // D1 RUBRIKKRYSSET när kryss-kolumnen är SORTERINGSKOLUMNEN: varje
    // `setCell` sorterar om. SABOTAGE: `headerCheckToggled` som skriver medan
    // den går över `rowAtDisplay` (rad 2 hoppas över, och `rows` blir {0, 3}).
    {
        Prov g;
        g.setColumns({{"Namn"}, {"På"}});
        g.setColumnEditor(1, {.editor = Editor::Check});
        g.setColumnHeaderCheck(1, true);
        g.setRows({{{"a", "false"}}, {{"b", "true"}}, {{"c", "false"}},
                   {{"d", "false"}}, {{"e", "true"}}});
        g.setSortColumn(1);
        const bool falskaFörst = g.rowAtDisplay(0) == 0 && g.rowAtDisplay(1) == 2;
        g.headerCheckToggled(1);
        bool alla = true;
        for (int r = 0; r < g.rowCount(); ++r)
            alla = alla && g.isChecked(r, 1);
        check(falskaFörst && alla && g.ändringar == 1 && g.rader == V{0, 2, 3},
              "D1: rubrikkrysset i den sorterade kryss-kolumnen kryssar ALLA — "
              "ETT event med de ändrade i den ordning de stod",
              radlista(g.rader));
    }
    // D2 EN DOLD RAD blir aldrig aktuell. SABOTAGE: `setCurrentCell` utan
    // platsvakten (raden i den ihopfällda kategorin blir aktuell, och en
    // redigering där hade varit osynlig men tagit tangenterna).
    {
        Prov g;
        g.setColumns({{"A"}, {"B"}});
        g.setRows({{{"x", "1"}},
                   {.cells = {"K"}, .kind = RowKind::Category},
                   {{"y", "2"}},
                   {{"z", "3"}}});
        g.setExpanded(1, false);
        g.setCurrentCell(0, 1);
        g.setCurrentCell(2, 1);
        check(g.displayRow(2) == -1 && g.currentRow() == 0 &&
                  g.currentColumn() == 1,
              "D2: setCurrentCell på en dold rad — logg + no-op",
              std::to_string(g.currentRow()));
    }
    // D4 ANVÄNDARENS IHOPFÄLLNING som väljer bort dolda rader fyrar
    // `onSelectionChange` EFTER `onCollapse`; `setExpanded` och Cell-läget
    // aldrig. SABOTAGE: `toggleCategory` utan valets event (spåret "C"), eller
    // med det före fällningen ("SC"), eller ett event också i Cell-läget.
    {
        Prov g;
        g.setColumns({{"A"}, {"B"}});
        g.setRows({{.cells = {"K"}, .kind = RowKind::Category},
                   {{"a", "1"}},
                   {{"b", "2"}},
                   {.cells = {"L"}, .kind = RowKind::Category},
                   {{"c", "3"}}});
        g.setSelectionMode(SM::FullRow);
        g.setMultiSelect(true);
        std::string spår;
        g.onCollapse        = [&] { spår += "C"; };
        g.onExpand          = [&] { spår += "E"; };
        g.onSelectionChange = [&] { spår += "S"; };
        g.setSelected(1, true);
        g.setSelected(2, true);
        g.setSelected(4, true);
        g.setCurrentCell(0, 1);
        g.keyDown(gfx::Key::Left, 0);
        check(spår == "CS" && g.selectedRows() == V{4},
              "D4: ihopfällningen väljer bort de dolda — onCollapse, sedan "
              "onSelectionChange", spår + " " + radlista(g.selectedRows()));
        g.keyDown(gfx::Key::Right, 0);
        g.keyDown(gfx::Key::Left, 0);
        check(spår == "CSEC",
              "D4: utan valda dolda rader fyrar fällningen bara sitt eget event",
              spår);
        g.setExpanded(0, true);
        g.setSelected(1, true);
        g.setExpanded(0, false);
        check(spår == "CSEC" && !g.isSelected(1),
              "D4: setExpanded väljer bort tyst", spår);
        g.setExpanded(0, true);
        g.setSelected(1, true);
        g.setSelectionMode(SM::Cell);
        g.setCurrentCell(0, 1);
        g.keyDown(gfx::Key::Left, 0);
        check(spår == "CSECC",
              "D4: i Cell-läget fyras inget valevent (valet syns inte där)",
              spår);
    }
    // D5 BARA SYNLIGA datarader kan väljas. SABOTAGE: `assignSelection` utan
    // platsvakten (enkelvalets väg), eller `setSelected` utan den (flervalets
    // direkta väg) — `isSelected` sant medan `selectedRows()` är tomt.
    {
        Prov g;
        g.setColumns({{"A"}});
        g.setRows({{{"x"}},
                   {.cells = {"K"}, .kind = RowKind::Category},
                   {{"y"}},
                   {{"z"}}});
        g.setSelectionMode(SM::FullRow);
        g.setExpanded(1, false);
        g.setSelected(2, true);
        const bool enkel = !g.isSelected(2) && g.selectedRows().empty();
        g.setMultiSelect(true);
        g.setSelected(3, true);
        check(enkel && !g.isSelected(3) && g.selectedRows().empty(),
              "D5: setSelected på en dold rad — logg + no-op, i enkel- och "
              "flerval");
        g.setSelected(0, true);
        check(g.selectedRows() == V{0}, "D5: en synlig datarad väljs",
              radlista(g.selectedRows()));
    }
    // D6 EN NY REDIGERARE I SORTERINGSKOLUMNEN sorterar om, också när
    // texten står still. Textordningen ("-1" < "-5" < "3") och talordningen
    // (-5 < -1 < 3) skiljer. SABOTAGE: `setColumnEditor`, `setCellEditor`,
    // `clearCellEditor` eller `rowReplaced` utan `invalidateOrder()`.
    {
        Prov g;
        g.setColumns({{"V"}});
        g.setRows({{{"-5"}}, {{"3"}}, {{"-1"}}});
        g.setSortColumn(0);
        const auto ordning = [&g] {
            V v;
            for (int p = 0; p < g.positionCount(); ++p)
                v.push_back(g.rowAtDisplay(p));
            return v;
        };
        const V text = ordning();
        g.setColumnEditor(0, {.editor = Editor::Number});
        const V tal = ordning();
        g.setColumnEditor(0, {.editor = Editor::Text});
        const V åter = ordning();
        check(text == V{2, 0, 1} && tal == V{0, 2, 1} && åter == V{2, 0, 1},
              "D6: setColumnEditor i sorteringskolumnen sorterar om",
              radlista(text) + " " + radlista(tal) + " " + radlista(åter));
        g.setCellEditor(0, 0, {.editor = Editor::Number});
        const V cell = ordning();
        g.clearCellEditor(0, 0);
        const V rensad = ordning();
        check(cell == V{0, 2, 1} && rensad == V{2, 0, 1},
              "D6: setCellEditor och clearCellEditor sorterar om",
              radlista(cell) + " " + radlista(rensad));
        g.setCellEditor(0, 0, {.editor = Editor::Number});
        const V före = ordning();
        g.setRow(0, {"-5"});
        const V efter = ordning();
        check(före == V{0, 2, 1} && efter == V{2, 0, 1},
              "D6: setRow som nollar cellens redigerare med samma text sorterar "
              "om", radlista(före) + " " + radlista(efter));
    }
}

// AM1–AM5 och TT1–TT5. AUTOMIN FÖR DE TYPADE CELLERNA och CELLENS TOOLTIP,
// headless mot en egen Ctx (fast teckensteg 8 px). Förväntat ur kontraktet:
// kolumnen är innehållet + 2 × `gridPadX` + kantens lucka (1 px vid skala 1)
// när innehållet är bredare än rubriken. SABOTAGE: `numberW` utan
// `fieldPadX` före suffixet (AM3 — kolumnen blir smalare än det ritade),
// `numberW` med luften också utan suffix (AM4), `cellContentWidth` som mäter
// cellens text i stället för kryssrutan, färgrutan eller knappen (AM1, AM2,
// AM5); `toolTipAt` där cellens tooltip vinner över felet (TT1) eller
// rubrikens/widgetens över cellens (TT2).
void fallTypadeBredderOchTooltip()
{
    StegFont                     f;
    Inspelning                   rec;
    const glaze::Style           stil = glaze::defaultStyle(glaze::UI::Slate);
    const glaze::Theme::Variant& tema = glaze::defaultTheme(glaze::UI::Slate).light;
    const auto ctxFör = [&](glaze::Widget& w) {
        glaze::Ctx c{};
        c.r     = &rec;
        c.font  = &f;
        c.scale = 1.0f;
        c.ui    = glaze::UI::Slate;
        c.style = &stil;
        c.theme = &tema;
        c.area  = {0, 0, 1000, 400};
        c.box   = w.resolve(c);
        return c;
    };
    {
        Prov g;
        g.width  = 900;
        g.height = 200;
        g.setColumns({{"T"}, {"T"}, {"T"}, {"T"}, {"T"}, {"T"}, {"T"}});
        for (int k = 0; k < 7; ++k)
            g.setColumnSizing(k, glaze::ColumnSizing::AutoMin);
        g.setColumnEditor(0, {.editor = Editor::Check});
        g.setColumnEditor(1, {.editor       = Editor::Color,
                              .colorDisplay = glaze::ColorDisplay::Both});
        g.setColumnEditor(2, {.editor = Editor::Number,
                              .prefix = "ca ",
                              .suffix = " mm"});
        g.setColumnEditor(3, {.editor = Editor::Number, .prefix = "ca "});
        g.setColumnEditor(4, {.editor = Editor::Button});
        g.setColumnEditor(5, {.editor = Editor::Color});
        g.setColumnEditor(6, {.editor       = Editor::Color,
                              .colorDisplay = glaze::ColorDisplay::Hex});
        g.setRows({{{"true", "#123456", "1234.5", "123456", "Kör nu",
                     "#123456", "#123456"}}});
        const glaze::Ctx c     = ctxFör(g);
        const float      pad   = stil.gridPadX;
        const float      lucka = 1.0f;
        const auto inne = [&](int k) { return g.columnW(c, k) - 2.0f * pad; };
        const auto tal  = [](float v) { return std::to_string(v); };
        const glaze::TrText decimal = glaze::tr(glaze::str::DecimalSeparator);
        const float         sep     = 8.0f * float(decimal.str().size());

        check(inne(0) >= stil.checkSize - 0.01f,
              "AM1: kryss-kolumnen rymmer kryssrutan", tal(inne(0)));
        const float färg = stil.checkSize + stil.checkGap + 8.0f * 7.0f;
        check(nära(inne(1), färg + lucka),
              "AM2: färg-kolumnen i Both är rutan, luften och hextexten",
              tal(inne(1)) + " mot " + tal(färg + lucka));
        // SABOTAGE: `colorW` som inte frågar läget (rutan, luften och
        // hextexten i alla tre), som svarar `checkSize` i Swatch, eller
        // `cellContentWidth` som inte skickar `colorDisplay`.
        check(nära(inne(5), stil.colorCellSwatchW + lucka),
              "AM2b: färg-kolumnen i Swatch (förvalet) är bara rutan, "
              "colorCellSwatchW",
              tal(inne(5)) + " mot " + tal(stil.colorCellSwatchW + lucka));
        check(nära(inne(6), 8.0f * 7.0f + lucka),
              "AM2c: färg-kolumnen i Hex är bara hextexten",
              tal(inne(6)) + " mot " + tal(8.0f * 7.0f + lucka));
        const float medEnhet =
            8.0f * 3.0f + (8.0f * 5.0f + sep) + stil.fieldPadX + 8.0f * 3.0f;
        check(nära(inne(2), medEnhet + lucka),
              "AM3: talet med suffix — prefixet, talet, fältets luft och "
              "suffixet", tal(inne(2)) + " mot " + tal(medEnhet + lucka));
        const float utanEnhet = 8.0f * 3.0f + 8.0f * 6.0f;
        check(nära(inne(3), utanEnhet + lucka),
              "AM4: talet utan suffix — prefixet och talet, ingen luft efter",
              tal(inne(3)) + " mot " + tal(utanEnhet + lucka));
        const float knapp =
            8.0f * 7.0f + 2.0f * (stil.buttonPadX + stil.cellButtonInset);
        check(g.columnW(c, 4) >= knapp - 0.01f,
              "AM5: knapp-kolumnen rymmer knappen med dess luft och indrag",
              tal(g.columnW(c, 4)) + " mot " + tal(knapp));
    }
    {
        Prov g;
        g.width  = 300;
        g.height = 200;
        g.setColumns({{"A", 100}, {"B", 100}});
        g.setColumnToolTip(0, "rubrik");
        g.setColumnEditor(1, {.editor = Editor::Number});
        g.setRows({{{"x", "abc"}}, {{"y", "5"}}});
        g.setCellToolTip(0, 1, "egen0");
        g.setCellToolTip(1, 1, "egen1");
        g.setToolTip("grid");
        const glaze::Ctx c  = ctxFör(g);
        const float      in = stil.panelInset;
        const float      hh = stil.tableHeaderH;
        const float      rh = stil.gridRowH;
        const auto tips = [&](float x, float y) {
            const glaze::TrText t = g.toolTipAt(c, x, y);
            return t.str();
        };
        const auto cell = [&](int p, int k) {
            return tips(in + 100.0f * float(k) + 50.0f,
                        in + hh + float(p) * rh + rh / 2.0f);
        };
        const glaze::TrText felet = glaze::tr(glaze::str::InvalidValue);
        const std::string   fel   = felet.str();
        check(cell(0, 1) == fel, "TT1: felet vinner över cellens egen",
              cell(0, 1));
        check(cell(1, 1) == "egen1", "TT2: cellens egen tooltip", cell(1, 1));
        check(cell(1, 0) == "grid",
              "TT3: en cell utan egen — datagridens (inte rubrikens)",
              cell(1, 0));
        check(tips(in + 50.0f, in + hh / 2.0f) == "rubrik",
              "TT4: över huvudet — kolumnrubrikens",
              tips(in + 50.0f, in + hh / 2.0f));
        check(tips(in + 150.0f, in + hh / 2.0f) == "grid",
              "TT5: över en rubrik utan egen — datagridens",
              tips(in + 150.0f, in + hh / 2.0f));
    }
}

// ============================================================ MED FÖNSTER

const char* const kStatus[] = {"Ny", "Pågår", "Klar"};

// Fönstret med en datagrid och ett andra stopp — kolumnerna och raderna
// sätts av de två fönstren nedan.
struct VardBas : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    Prov             grid;
    glaze::TextInput annat;   // ett andra stopp — Tab ska kunna lämna griden

    VardBas(int fönsterW, float gridW)
        : glaze::AppWindow("datagrid_gate", fönsterW, 360, gfx::Chrome::None)
    {
        grid.x      = 10;
        grid.y      = 10;
        grid.width  = gridW;
        grid.height = 220;
        grid.name   = "grid";
        root().adopt(grid);

        annat.x     = gridW + 30;
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
    glaze::ColorPicker* färgväljaren()
    {
        return dynamic_cast<glaze::ColorPicker*>(editorn());
    }
};

// Fritext, en vallista och en skrivskyddad kolumn över 30 rader.
struct Vard : VardBas {
    Vard() : VardBas(520, 320)
    {
        grid.addColumn("Uppgift", 120);
        grid.addColumn("Status", 90);
        grid.addColumn("Id", 60, glaze::Align::Right);
        grid.setColumnEditor(1, {.editor  = Editor::Choice,
                                 .choices = {"Ny", "Pågår", "Klar"}});
        grid.setColumnEditor(2, {.editor = Editor::None});
        for (int i = 0; i < 30; ++i)
            grid.addRow({"uppgift " + std::to_string(i), kStatus[i % 3],
                         std::to_string(i)});
    }
};

// ---- Q1–Q2: EDITORN I EN AUTOSIZE-KOLUMN OCH MED IKONSTORLEK.
//      Datagridens `placeChild` har BARNETS Ctx och skickar innehållets bredd
//      uttryckligen — förväntat värde räknas här ur kontraktet: den enda
//      Autosize-kolumnen (vars bas ryms) är innehållsytans bredd minus de
//      andra kolumnernas, radhöjden max(gridRowH,
//      storleken + gridRowH − iconFallback). SABOTAGE: `cellInContent` med
//      `resolve()` ur barnets Ctx (Q1 — editorn hamnar fel eller i fel bredd).
//      Med en satt ikonstorlek reserverar kolumn 0 ikonens kvadrat på varje
//      rad, och editorn står där värdet ritas: storleken + `iconGap` in.
//      MÄTER INTE hur editorn ser ut i den högre raden.
void fallAutosizeEditor()
{
    Vard w;
    w.setOffscreen(true);
    Prov& g = w.grid;
    // STRÄCKT: bredden kommer ur fönstret, inte ur ett satt mått.
    g.anchors = {true, true, true, false};
    g.right   = 190;
    g.setColumnSizing(0, glaze::ColumnSizing::Autosize);

    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        const float s    = w.skala();
        const float fyll = w.inre().w / s - (g.columnWidth(1) + g.columnWidth(2));
        const auto  facit = [&](int p, float radH) {
            return gfx::Rectf{w.inre().x, w.inre().y + w.huvudH() + float(p) * radH,
                              fyll * s, radH};
        };
        // ---- Q1. Editorn i Autosize-kolumnen ligger på den EFFEKTIVA cellen.
        const gfx::Rectf c2 = facit(2, w.radH());
        w.klick({c2.x + c2.w / 2.0f, c2.y + c2.h / 2.0f});
        w.klick({c2.x + c2.w / 2.0f, c2.y + c2.h / 2.0f});
        check(g.isEditing() && g.editColumn() == 0 &&
                  sammaRect(w.editorYta(), c2),
              "Q1: editorn i Autosize-kolumnen == den effektiva cellen",
              fog(w.editorYta()) + " mot " + fog(c2));
        w.tangent(gfx::Key::Escape);

        // ---- Q2. Med `setIconSize` är editorn lika hög som den högre raden.
        g.setIconSize(40.0f);
        const float rh = std::max(w.style().gridRowH,
                                  40.0f + w.style().gridRowH -
                                      w.style().iconFallback) * s;
        const gfx::Rectf c1   = facit(1, rh);
        const float      ikon = (40.0f + w.style().iconGap) * s;
        const gfx::Rectf e1{c1.x + ikon, c1.y, c1.w - ikon, c1.h};
        w.klick({c1.x + c1.w / 2.0f, c1.y + c1.h / 2.0f});
        w.klick({c1.x + c1.w / 2.0f, c1.y + c1.h / 2.0f});
        check(g.isEditing() && std::abs(w.editorYta().h - rh) < 0.51f &&
                  sammaRect(w.editorYta(), e1),
              "Q2: editorns höjd == radhöjden med ikonstorleken, och den står "
              "efter ikonens kvadrat",
              fog(w.editorYta()) + " mot " + fog(e1));
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
                      g.focused() && g.currentRow() == 4,
                  "E6: Enter på ett NYTT val sparar, fyrar, avslutar och "
                  "flyttar ner", g.cell(3, 1) + " rad " +
                                     std::to_string(g.currentRow()));
            g.setCurrentCell(3, 1);   // E7 och E8 börjar på den sparade cellen
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
            g.setColumnEditor(2, {.editor = Editor::Text});
            g.setCurrentCell(29, 2);
            w.tangent(gfx::Key::F2);
            w.skriv("t");
            före = g.ändringar;
            w.tangent(gfx::Key::Tab);
            check(g.cell(29, 2) == "29t" && g.ändringar == före + 1 &&
                      !g.isEditing() && w.annat.focused(),
                  "E22: Tab i sista cellen sparar OCH lämnar griden");
            g.setColumnEditor(2, {.editor = Editor::None});

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

// ---- B1–B6: BORTTAGNINGEN (`removeRow`) i ett dolt fönster, som en kedja av
//      postade steg (fokusförlustens sparning postas). Raderna efter den
//      borttagna flyttar upp ett index, och det som hör till dem följer med.
void fallBorttagningen()
{
    Vard w;
    w.setOffscreen(true);
    Prov& g = w.grid;

    bool                  kordes = false;
    int                   steg   = 0;
    int                   före   = 0;
    std::function<void()> nästa;
    nästa = [&] {
        switch (steg++) {
        case 0: {
            kordes = true;
            // ---- B1. CELLINSTÄLLNINGARNA följer sina rader. Sabotage:
            //      `cells_` som inte tappar posten i `rowRemoved` — den låsta
            //      cellen, den egna redigeraren, den inaktiva cellen och
            //      tooltipen står kvar på sina gamla index.
            g.setCellEditor(10, 0, {.editor = Editor::None});
            g.setCellEditor(11, 2, {.editor = Editor::Text});
            g.setCellEnabled(12, 0, false);
            g.setCellToolTip(13, 0, "tips");
            g.removeRow(2);
            const glaze::TrText tips = g.cellToolTip(12, 0);
            const glaze::TrText tom  = g.cellToolTip(13, 0);
            check(g.rowCount() == 29 && g.cell(2, 0) == "uppgift 3" &&
                      g.cellEditor(9, 0).editor == Editor::None &&
                      g.cellEditor(10, 0).editor == Editor::Text &&
                      g.cellEditor(10, 2).editor == Editor::Text &&
                      g.cellEditor(11, 2).editor == Editor::None &&
                      !g.isCellEnabled(11, 0) && g.isCellEnabled(12, 0) &&
                      tips.str() == "tips" && tom.str().empty() &&
                      g.ändringar == 0,
                  "B1: låst cell, egen redigerare, inaktiv cell och tooltip "
                  "följer sina rader ett index upp");

            // ---- B2. En redigering på en ANNAN rad följer sin rad. Sabotage:
            //      `editRow_` som inte räknas ned — Enter sparar i raden under.
            w.klick(w.cell(5, 0));
            w.tangent(gfx::Key::F2);
            w.skriv("e");
            g.removeRow(1);
            check(g.isEditing() && g.editRow() == 4 && g.editColumn() == 0 &&
                      g.currentRow() == 4 && g.ändringar == 0 &&
                      sammaRect(w.editorYta(), w.cellYta(4, 0)),
                  "B2: redigeringen följer sin rad — editorn på dess nya plats",
                  std::to_string(g.editRow()) + " " + fog(w.editorYta()));
            w.tangent(gfx::Key::Return);
            check(g.cell(4, 0) == "uppgift 6e" && g.cell(3, 0) == "uppgift 5" &&
                      g.ändringar == 1 && g.rad == 4 && g.kol == 0,
                  "B2: …och Enter sparar i rätt rad med rätt index i eventet",
                  g.cell(4, 0) + " rad " + std::to_string(g.rad));

            // ---- B3. En redigering PÅ den borttagna raden avbryts utan
            //      events. Sabotage: `endEdit(true)` i `rowRemoved` — texten
            //      skrivs i raden som flyttar in, och onChange fyras.
            g.setCurrentCell(2, 0);
            w.tangent(gfx::Key::F2);
            w.skriv("z");
            const int commits = g.commits;
            g.removeRow(2);
            check(!g.isEditing() && !w.editorn() && g.focused() &&
                      g.ändringar == 1 && g.commits == commits &&
                      g.cell(2, 0) == "uppgift 5" && g.currentRow() == 2 &&
                      g.currentColumn() == 0,
                  "B3: redigeringen på raden avbryts utan events — fokus i "
                  "griden, den aktuella på raden som nu står på platsen",
                  g.cell(2, 0));

            // ---- B4. Fokusförlustens POSTADE onChange slopas. Sabotage:
            //      `rowRemoved` som inte stegar `rowsGen_` — eventet fyras med
            //      raden i den gamla numreringen.
            g.setCurrentCell(3, 0);
            w.tangent(gfx::Key::F2);
            w.skriv("p");
            före = g.ändringar;
            w.klickAnnat();
            g.removeRow(0);
            break;
        }
        case 1: {
            check(g.ändringar == före && g.cell(2, 0) == "uppgift 6ep",
                  "B4: removeRow före pump-svansen — cellen skriven, det "
                  "postade eventet slopat",
                  std::to_string(g.ändringar - före) + " " + g.cell(2, 0));

            // ---- B5. `removeRow` INIFRÅN `onCellClick`, direkt i handlern
            //      som demon gör. Sabotage: `Table::removeRow` utan landningen
            //      (den aktuella blir −1), eller ett släpp som rör raden efter
            //      handlern.
            g.setColumnWidth(0, 80);
            g.addColumn("", 60);
            g.setColumnEditor(3, {.editor = Editor::Button});
            for (int i = 0; i < g.rowCount(); ++i)
                g.setCell(i, 3, "x");
            int klick = 0;
            g.onCellClick = [&](glaze::Widget* s, glaze::CellClickArgs& e) {
                ++klick;
                static_cast<glaze::DataGrid*>(s)->removeRow(e.row);
            };
            const int n = g.rowCount();
            w.klick(w.cell(1, 3));
            check(klick == 1 && g.rowCount() == n - 1 &&
                      g.cell(1, 0) == "uppgift 6ep" && g.currentRow() == 1 &&
                      g.currentColumn() == 3 && !g.isEditing() && g.focused(),
                  "B5: knappcellens klick tar bort sin rad i handlern — den "
                  "aktuella står kvar på platsen",
                  std::to_string(g.rowCount()) + " " + g.cell(1, 0));

            // ---- B6. Samma med Enter på den aktuella knappcellen.
            w.tangent(gfx::Key::Return);
            check(klick == 2 && g.rowCount() == n - 2 &&
                      g.cell(1, 0) == "uppgift 7" && g.currentRow() == 1 &&
                      g.focused(),
                  "B6: Enter på knappcellen tar bort raden — griden lever och "
                  "står på raden som flyttade in",
                  std::to_string(g.rowCount()) + " " + g.cell(1, 0));
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
        std::printf("  --   B1–B6 hoppades över: inget fönster kunde skapas\n");
        ++g_skips;
    }
}

// Kryss, färg, tal med enhet, en vallista och en knapp — med en kategori och
// en tom rad.
struct TypVard : VardBas {
    TypVard() : VardBas(680, 480)
    {
        using RowKind = glaze::Table::RowKind;
        grid.height = 300;
        grid.addColumn("Namn", 100);
        grid.addColumn("På", 50);
        grid.addColumn("Färg", 90);
        grid.addColumn("Bredd", 80, glaze::Align::Right);
        grid.addColumn("Typ", 70);
        grid.addColumn("Knapp", 70);
        grid.setColumnEditor(1, {.editor = Editor::Check});
        grid.setColumnEditor(2, {.editor = Editor::Color});
        grid.setColumnEditor(3, {.editor   = Editor::Number,
                                 .min      = 0,
                                 .max      = 100,
                                 .decimals = 1,
                                 .suffix   = "px"});
        grid.setColumnEditor(4, {.editor  = Editor::Choice,
                                 .choices = {"Hel", "Streck"}});
        grid.setColumnEditor(5, {.editor = Editor::Button});
        grid.setRows({{{"a", "false", "#FF0000", "10", "Hel", "X"}},
                      {{"b", "false", "#00FF00", "20", "Streck", "X"}},
                      {{"c", "false", "#0000FF", "30", "Hel", "X"}},
                      {.cells = {"Grupp"}, .kind = RowKind::Category},
                      {{"d", "true", "#FFFFFF", "40", "Hel", "X"}},
                      {.kind = RowKind::Blank},
                      {{"e", "false", "#000000", "50", "Hel", "X"}}});
    }
};

// W1–W12. DE TYPADE CELLERNA MED FÖNSTER — musen, editorerna och
// `commitEdits()`. Stegen är en kedja av postade steg (färgpopupens
// stängning och fokusförlustens postade event verkställs i pump-svansen).
void fallTypadeCellerMedFonster()
{
    using V = std::vector<int>;
    TypVard w;
    w.setOffscreen(true);
    Prov& g = w.grid;

    bool                  kordes = false;
    int                   steg   = 0;
    int                   före   = 0;
    std::function<void()> nästa;
    const auto talfält = [&] {
        return dynamic_cast<glaze::TextInput*>(w.editorn());
    };
    nästa = [&] {
        switch (steg++) {
        case 0: {
            kordes = true;
            // ---- W1 KRYSSET med ett klick. SABOTAGE: `mouseDown` utan
            //      Check-grenen (klicket gör bara cellen aktuell).
            w.klick(w.cell(0, 1));
            check(g.cell(0, 1) == "true" && g.ändringar == 1 &&
                      g.commits == 1 && g.rader == V{0} && !g.isEditing(),
                  "W1: ett klick i en krysscell växlar och sparar — ETT par",
                  g.cell(0, 1));

            // ---- W2 KNAPPEN. SABOTAGE: `mouseUp` som fyrar utan att fråga
            //      cellen under släppet (dra bort ångrar inte), eller
            //      `mouseDown` som trycker en inaktiv knapp.
            w.klick(w.cell(1, 5));
            check(g.klick == 1 && g.klickRad == 1 && g.klickKol == 5 &&
                      g.cell(1, 5) == "X" && g.ändringar == 1,
                  "W2: ett tryck som släpps över knappcellen fyrar onCellClick",
                  std::to_string(g.klick));
            w.mus(gfx::EventType::MouseButtonDown, w.cell(2, 5).x,
                  w.cell(2, 5).y);
            w.mus(gfx::EventType::MouseButtonUp, w.cell(1, 0).x, w.cell(1, 0).y);
            check(g.klick == 1, "W2: dra bort från knappen ångrar klicket");
            g.setCellEnabled(2, 5, false);
            w.klick(w.cell(2, 5));
            check(g.klick == 1, "W2: en inaktiv knappcell fyrar inte");

            // ---- W3 `editOnClick`. SABOTAGE: Text-grenen i `mouseDown` som
            //      inte frågar `editOnClick_` (alltid eller aldrig).
            g.setEditOnClick(true);
            w.klick(w.cell(0, 0));
            check(g.isEditing() && g.editRow() == 0 && g.editColumn() == 0,
                  "W3: med editOnClick börjar ett klick redigera en textcell");
            w.tangent(gfx::Key::Escape);
            g.setEditOnClick(false);
            w.klick(w.cell(1, 0));
            check(!g.isEditing() && g.currentRow() == 1,
                  "W3: utan editOnClick gör klicket bara cellen aktuell");

            // ---- W4 ENTER öppnar redigeraren; i en krysscell flyttar den
            //      ner. SABOTAGE: Return-grenen som alltid flyttar.
            g.setCurrentCell(2, 0);
            w.tangent(gfx::Key::Return);
            check(g.isEditing() && g.editRow() == 2,
                  "W4: Enter på en textcell öppnar redigeraren");
            w.tangent(gfx::Key::Escape);
            g.setCurrentCell(0, 1);
            w.tangent(gfx::Key::Return);
            check(!g.isEditing() && g.currentRow() == 1 &&
                      g.cell(0, 1) == "true",
                  "W4: Enter i en krysscell flyttar ner utan att växla");

            // ---- M1 `selectOnEdit` true: Enter och ett klick (`editOnClick`)
            //      öppnar fältet med HELA texten markerad, och ett skrivet
            //      tecken ersätter den. SABOTAGE: `startEdit` som alltid
            //      ställer markören sist (inte frågar `selectOnEdit_`).
            {
                const auto helaMarkerad = [&](const std::string& väntad) {
                    const glaze::TextInput* f = talfält();
                    return f && f->text() == väntad && f->hasSelection() &&
                           f->selBegin() == 0 && f->selEnd() == väntad.size();
                };
                const auto sistUtan = [&](const std::string& väntad) {
                    const glaze::TextInput* f = talfält();
                    return f && f->text() == väntad && !f->hasSelection() &&
                           f->caret() == väntad.size();
                };
                g.setSelectOnEdit(true);
                g.setCurrentCell(2, 0);
                w.tangent(gfx::Key::Return);
                check(g.isEditing() && helaMarkerad("c"),
                      "M1: selectOnEdit — Enter öppnar fältet med hela texten "
                      "markerad");
                w.tangent(gfx::Key::Escape);
                g.setEditOnClick(true);
                w.klick(w.cell(2, 0));
                check(g.isEditing() && helaMarkerad("c"),
                      "M1: selectOnEdit — ett klick (editOnClick) öppnar fältet "
                      "med hela texten markerad");
                w.tangent(gfx::Key::Escape);
                g.setEditOnClick(false);
                // Att SKRIVA ersätter texten oavsett valet.
                g.setCurrentCell(2, 0);
                w.skriv("k");
                check(g.isEditing() && sistUtan("k"),
                      "M1: ett skrivet tecken ersätter texten med valet på");
                w.tangent(gfx::Key::Escape);

                // ---- M2 `selectOnEdit` false: markören sist och ingenting
                //      markerat, efter Enter och efter ett klick. SABOTAGE:
                //      `markera` som alltid markerar allt (inte frågar valet).
                g.setSelectOnEdit(false);
                g.setCurrentCell(2, 0);
                w.tangent(gfx::Key::Return);
                check(g.isEditing() && sistUtan("c"),
                      "M2: utan selectOnEdit — Enter öppnar fältet med markören "
                      "sist och inget markerat");
                w.tangent(gfx::Key::Escape);
                g.setEditOnClick(true);
                w.klick(w.cell(2, 0));
                check(g.isEditing() && sistUtan("c"),
                      "M2: utan selectOnEdit — ett klick (editOnClick) öppnar "
                      "fältet med markören sist och inget markerat");
                w.tangent(gfx::Key::Escape);
                g.setEditOnClick(false);
                g.setCurrentCell(2, 0);
                w.skriv("k");
                check(g.isEditing() && sistUtan("k"),
                      "M2: ett skrivet tecken ersätter texten med valet av");
                w.tangent(gfx::Key::Escape);
            }

            // ---- L3 EN LÅST TALCELL redigeras inte — varken med Enter, F2,
            //      ett klick (`editOnClick`) eller ett skrivet tal — och
            //      värdet står kvar. SABOTAGE: `startEdit` utan
            //      `editable`-frågan för `readOnly` (`skriverVärde` som
            //      inte frågar `readOnly`).
            {
                g.setCellEditor(6, 3, {.editor   = Editor::Number,
                                       .max      = 100,
                                       .decimals = 1,
                                       .suffix   = "px",
                                       .readOnly = true});
                const int ändr = g.ändringar;
                g.setCurrentCell(6, 3);
                w.tangent(gfx::Key::Return);
                const bool enter = g.isEditing();
                if (enter)
                    w.tangent(gfx::Key::Escape);
                g.setCurrentCell(6, 3);
                w.tangent(gfx::Key::F2);
                const bool f2 = g.isEditing();
                if (f2)
                    w.tangent(gfx::Key::Escape);
                g.setCurrentCell(6, 3);
                w.skriv("7");
                const bool skriv = g.isEditing();
                if (skriv)
                    w.tangent(gfx::Key::Escape);
                g.setEditOnClick(true);
                w.klick(w.cell(6, 3));
                const bool klick = g.isEditing();
                if (klick)
                    w.tangent(gfx::Key::Escape);
                g.setEditOnClick(false);
                check(!enter && !f2 && !skriv && !klick &&
                          g.cell(6, 3) == "50" && g.ändringar == ändr,
                      "L3: en låst talcell redigeras inte med Enter, F2, ett "
                      "skrivet tal eller ett klick",
                      std::string(enter ? "Enter " : "") + (f2 ? "F2 " : "") +
                          (skriv ? "skriv " : "") + (klick ? "klick" : ""));
                g.clearCellEditor(6, 3);
            }

            // ---- W5 VALLISTAN öppnas med ETT klick. SABOTAGE: Choice-grenen
            //      i `mouseDown` borttagen.
            w.klick(w.cell(0, 4));
            {
                glaze::Dropdown* dd = w.vallistan();
                check(g.isEditing() && dd && dd->isOpen(),
                      "W5: ett klick på en val-cell öppnar listan");
            }
            w.tangent(gfx::Key::Escape);

            // ---- W6 TALET: klampat och avrundat, ",", "." lika, fel med
            //      tooltip. SABOTAGE: `endEdit` som skriver texten rått (ingen
            //      `commitNumber`), som jämför texten i stället för värdet
            //      ("12,5" fyrar), eller ett Invalid som ändå skriver.
            g.setCurrentCell(0, 3);
            w.skriv("x");
            check(!g.isEditing(), "W6: en bokstav börjar ingen redigering av "
                                  "en talcell");
            w.skriv("150");
            check(g.isEditing() && talfält() && talfält()->text() == "150",
                  "W6: ett taltecken börjar redigera talcellen");
            w.tangent(gfx::Key::Return);
            const auto hundra = g.numberValue(0, 3);
            check(g.cell(0, 3) == "100.0" && hundra && *hundra == 100.0 &&
                      g.ändringar == 2 && g.rader == V{0},
                  "W6: 150 klampas till max och skrivs kanoniskt (\"100.0\")",
                  g.cell(0, 3));
            g.setCell(1, 3, "12.5");
            g.setCurrentCell(1, 3);
            w.skriv("12,5");
            w.tangent(gfx::Key::Return);
            check(g.cell(1, 3) == "12.5" && g.ändringar == 2,
                  "W6: \"12,5\" mot sparade \"12.5\" är samma tal — inget event");
            g.setCurrentCell(2, 3);
            w.skriv("1-2");
            w.tangent(gfx::Key::Return);
            check(g.hasError(2, 3) && g.cell(2, 3) == "30" && g.ändringar == 2,
                  "W6: en otolkbar text är ett fel — värdet står kvar, inget "
                  "fyras");
            {
                const glaze::TrText felet = glaze::tr(glaze::str::InvalidValue);
                const gfx::Pointf   p     = w.cell(2, 3);
                const glaze::TrText tips  = g.toolTipAt(g.behaviorCtx(), p.x, p.y);
                check(tips.str() == felet.str(),
                      "W6: felets tooltip över cellen", tips.str());
            }
            g.setCurrentCell(2, 3);
            w.tangent(gfx::Key::F2);
            check(talfält() && talfält()->text() == "1-2",
                  "W6: redigeraren börjar med den väntande texten");
            w.tangent(gfx::Key::Escape);
            check(!g.hasError(2, 3), "W6: Escape tar bort felet");
            w.skriv("1-2");
            w.tangent(gfx::Key::Return);
            g.setCell(2, 3, "31");
            check(!g.hasError(2, 3) && g.cell(2, 3) == "31",
                  "W6: setCell på cellen tar bort felet");

            // ---- W7 `allowEmpty`. SABOTAGE: `commitNumber`s Empty behandlat
            //      som Invalid, eller tom text utan allowEmpty som giltig.
            g.setCellEditor(1, 3, {.editor     = Editor::Number,
                                   .allowEmpty = true,
                                   .emptyText  = "auto"});
            g.setCurrentCell(1, 3);
            före = g.ändringar;
            w.tangent(gfx::Key::F2);
            w.tangent(gfx::Key::A, kRedigera);
            w.tangent(gfx::Key::Backspace);
            w.tangent(gfx::Key::Return);
            check(g.cell(1, 3).empty() && !g.hasError(1, 3) &&
                      !g.numberValue(1, 3) && g.ändringar == före + 1,
                  "W7: med allowEmpty är en tom text ett giltigt tomt värde");
            g.setCurrentCell(0, 3);
            w.tangent(gfx::Key::F2);
            w.tangent(gfx::Key::A, kRedigera);
            w.tangent(gfx::Key::Backspace);
            w.tangent(gfx::Key::Return);
            check(g.hasError(0, 3) && g.cell(0, 3) == "100.0" &&
                      g.ändringar == före + 1,
                  "W7: utan allowEmpty är en tom text ett fel");

            // ---- W8 CELLENS egna gränser och enhet. Cellens värde står
            //      innanför dess gränser först: ett lagrat 40 är under max 10
            //      effektivt 10, och samma effektiva värde skrivs inte.
            //      SABOTAGE: `endEdit` som läser kolumnens redigerare i
            //      stället för cellens.
            g.setCellEditor(4, 3, {.editor = Editor::Number,
                                   .max    = 10,
                                   .suffix = "mm"});
            g.setCell(4, 3, "7");
            g.setCurrentCell(4, 3);
            w.skriv("50");
            w.tangent(gfx::Key::Return);
            g.setCurrentCell(2, 3);
            w.skriv("50");
            w.tangent(gfx::Key::Return);
            check(g.cell(4, 3) == "10" && g.cell(2, 3) == "50.0" &&
                      g.cellEditor(4, 3).suffix == "mm",
                  "W8: cellens max 10 och kortaste form, kolumnens max 100 med "
                  "en decimal", g.cell(4, 3) + " / " + g.cell(2, 3));

            // ---- W9 TAB hoppar över knappcellen. SABOTAGE: som T5.
            g.setCurrentCell(0, 4);
            w.tangent(gfx::Key::Tab);
            check(g.currentRow() == 1 && g.currentColumn() == 0,
                  "W9: Tab från val-cellen hoppar över knappen till nästa rad");

            // ---- W10 FÄRGEN: ett klick öppnar popupen, och `commitEdits()`
            //      sparar en öppen färg. SABOTAGE: `commitEdit` utan
            //      färgväljarens del (cellen orörd).
            w.klick(w.cell(0, 2));
            glaze::ColorPicker* fp = w.färgväljaren();
            if (!fp || !fp->isOpen()) {
                std::printf("  --   färgfallen hoppas över: popupen kom "
                            "aldrig upp\n");
                ++g_skips;
                g.cancelEdit();
                steg = 3;   // vidare till fallen utan popup
                break;
            }
            check(g.isEditing() && g.editColumn() == 2,
                  "W10: ett klick på en färgcell öppnar färgväljaren");
            före = g.ändringar;
            fp->setColor("#112233");
            w.commitEdits();
            check(g.cell(0, 2) == "#112233" && g.ändringar == före + 1 &&
                      g.rader == V{0} && !g.isEditing(),
                  "W10: commitEdits() sparar färgen i väljaren — ETT event",
                  g.cell(0, 2));
            break;
        }
        case 1: {
            // ---- W10 oförändrad färg. SABOTAGE: `commitColor` utan
            //      jämförelsen (fyrar varje gång).
            w.klick(w.cell(0, 2));
            före = g.ändringar;
            w.commitEdits();
            check(g.cell(0, 2) == "#112233" && g.ändringar == före,
                  "W10: en oförändrad färg sparar och fyrar ingenting");
            break;
        }
        case 2: {
            // ---- W10 alfan per cell. SABOTAGE: `commitColor` med kolumnens
            //      `allowAlpha` (alfan tappas).
            g.setCellEditor(1, 2, {.editor = Editor::Color, .allowAlpha = true});
            w.klick(w.cell(1, 2));
            if (glaze::ColorPicker* fp = w.färgväljaren())
                fp->setColor("#11223380");
            w.commitEdits();
            check(g.cell(1, 2) == "#11223380",
                  "W10: en cell med allowAlpha sparar #RRGGBBAA", g.cell(1, 2));
            break;
        }
        case 3: {
            // ---- W11 FLERA RADER. SABOTAGE: solovalet vid trycket i stället
            //      för vid släppet (valet töms innan krysset växlar), eller
            //      en sparning som bara skriver den redigerade raden.
            g.setSelectionMode(glaze::DataGrid::SelectionMode::FullRow);
            g.setMultiSelect(true);
            g.setMultiEdit(true);
            g.clearSelection();
            g.setSelected(0, true);
            g.setSelected(1, true);
            g.setSelected(2, true);
            g.setCell(0, 1, "false");
            g.setCell(1, 1, "false");
            g.setCell(2, 1, "false");
            g.setCellEnabled(2, 1, false);
            före = g.ändringar;
            w.klick(w.cell(0, 1));
            check(g.isChecked(0, 1) && g.isChecked(1, 1) &&
                      g.cell(2, 1) == "false" && g.ändringar == före + 1 &&
                      g.rader == V{0, 1},
                  "W11: ett klick i en vald rad kryssar alla valda — den "
                  "inaktiva hoppas över, ETT event", radlista(g.rader));
            check(g.isSelected(0) && g.isSelected(1) && g.isSelected(2),
                  "W11: trycket som ändrade cellen tömde inte valet",
                  radlista(g.selectedRows()));
            g.setCurrentCell(0, 3);
            w.skriv("5");
            w.tangent(gfx::Key::Return);
            check(g.cell(0, 3) == "5.0" && g.cell(1, 3) == "5" &&
                      g.cell(2, 3) == "5.0" && g.rader == V{0, 1, 2} &&
                      !g.hasError(0, 3),
                  "W11: talet i alla valda rader, var och en med sin egen "
                  "cells regler", g.cell(0, 3) + " " + g.cell(1, 3) + " " +
                                      g.cell(2, 3));
            g.setMultiEdit(false);
            g.setSelectionMode(glaze::DataGrid::SelectionMode::Cell);

            // ---- W12 KATEGORIN: ett klick sparar först och fäller sedan.
            //      SABOTAGE: kategorigrenen i `mouseDown` före sparningen som
            //      avbryter (cellen orörd), eller utan `toggleCategoryFromUser`.
            g.setCurrentCell(2, 0);
            w.tangent(gfx::Key::F2);
            w.skriv("q");
            före = g.ändringar;
            w.klick(w.cell(3, 0));
            check(g.cell(2, 0) == "cq" && !g.isEditing() && !g.isExpanded(3) &&
                      g.in == 1,
                  "W12: ett klick på kategoriraden sparar cellen och fäller "
                  "ihop", g.cell(2, 0));
            break;
        }
        case 4: {
            check(g.ändringar == före + 1 && g.rad == 2 && g.kol == 0,
                  "W12: …det sparade eventet kom",
                  std::to_string(g.ändringar - före));
            g.setExpanded(3, true);
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
        std::printf("  --   W-fallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    }
}

// W13–W14. ENTER I DEN ÖPPNA VALLISTAN, FÄRGPOPUPEN SOM AVSLUTAR SIN REDIGERING, och
// EDITORNS PLATS bredvid cellens bild och radens ikon. Förväntat ur
// kontraktet: värdet står `gridPadX` + bildens sida + `iconGap` in i cellen
// (bildens sida = storleksklassen, annars `iconFallback`), editorn med sin
// luft där cellens luft stod och fram till cellens högerkant, talets fält
// `fieldPadX` före värdet och fram till suffixet.
void fallEditornOchEnter()
{
    TypVard w;
    w.setOffscreen(true);
    Prov&                    g    = w.grid;
    const glaze::ImageSource ikon = glaze::stockIcon(glaze::StockIcon::Save);

    bool                  kordes = false;
    int                   steg   = 0;
    glaze::ColorPicker*   fp     = nullptr;
    std::function<void()> nästa;
    nästa = [&] {
        switch (steg++) {
        case 0: {
            kordes = true;
            // ---- W13 ENTER I DEN ÖPPNA VALLISTAN sparar, avslutar och
            //      flyttar ner (Shift+Enter upp), också med samma värde;
            //      Space sparar och står kvar. Klicket på nästa val-cell
            //      kommer i SAMMA varv som den förra listans stängning.
            //      SABOTAGE: Return-grenen i `ChoiceEditor::keyDown` utan
            //      `moveFromUser`, eller `Dropdown::open` som inte öppnar en
            //      lista vars stängning väntar (den gamla popupens
            //      provisoriska val committas).
            w.klick(w.cell(0, 4));
            glaze::Dropdown* dd = w.vallistan();
            if (!dd || !dd->isOpen()) {
                std::printf("  --   W13 hoppas över: vallistans popup kom "
                            "aldrig upp\n");
                ++g_skips;
                g.cancelEdit();
                steg = 3;
                break;
            }
            w.tangent(gfx::Key::Down);
            w.tangent(gfx::Key::Return);
            check(g.cell(0, 4) == "Streck" && !g.isEditing() &&
                      g.currentRow() == 1 && g.currentColumn() == 4,
                  "W13: Enter i den öppna listan sparar ett nytt värde och "
                  "flyttar ner", g.cell(0, 4) + " rad " +
                                     std::to_string(g.currentRow()));
            w.klick(w.cell(2, 4));
            w.tangent(gfx::Key::Return, gfx::kModShift);
            check(g.cell(2, 4) == "Hel" && !g.isEditing() && g.currentRow() == 1,
                  "W13: Shift+Enter med samma värde avslutar och flyttar upp",
                  std::to_string(g.currentRow()));
            w.klick(w.cell(1, 4));
            w.tangent(gfx::Key::Space);
            check(!g.isEditing() && g.currentRow() == 1,
                  "W13: Space i den öppna listan avslutar och står kvar");

            // ---- W13 FÄRGEN: när popupen STÄNGS är redigeringen slut —
            //      väljaren göms, cellen står kvar som aktuell, och pilen
            //      flyttar vidare direkt (inget andra Enter eller Escape).
            //      SABOTAGE: `ColorEditor::popupDismissed` borttagen.
            w.klick(w.cell(0, 2));
            fp = w.färgväljaren();
            if (!fp || !fp->isOpen()) {
                std::printf("  --   W13 färgen hoppas över: popupen kom "
                            "aldrig upp\n");
                ++g_skips;
                g.cancelEdit();
                steg = 3;
                break;
            }
            fp->close();
            break;
        }
        case 1:
            break;   // popupens stängning verkställs i pump-svansen
        case 2: {
            check(fp && !fp->isOpen() && !g.isEditing() &&
                      g.currentRow() == 0 && g.currentColumn() == 2,
                  "W13: färgpopupen stängd — redigeringen är slut och cellen "
                  "står kvar som aktuell");
            w.tangent(gfx::Key::Down);
            check(!g.isEditing() && g.currentRow() == 1 &&
                      g.currentColumn() == 2,
                  "W13: efter popupen flyttar pilen vidare direkt",
                  std::to_string(g.currentRow()));
            break;
        }
        case 3: {
            // ---- W14 EDITORN STÅR DÄR VÄRDET RITAS — förbi radens ikon
            //      (kolumn 0) och cellens bild, för text, tal, val och färg.
            //      SABOTAGE: `placeChild` på hela cellen (editorn täcker
            //      bilden) eller talets fält utan bildens steg.
            g.setCellImage(1, 0, ikon);
            g.setCellImage(0, 3, ikon);
            g.setCellImage(2, 4, ikon);
            g.setCellImage(1, 2, ikon);
            const auto&      m    = w.style();
            const float      s    = w.skala();
            const float      pad  = m.gridPadX * s;
            const float      k    = ikon.sizeClass();
            const float      förb = ((k > 0.0f ? k : m.iconFallback) + m.iconGap) * s;
            const auto efter = [&](int p, int kol) {
                const gfx::Rectf c = w.cellYta(p, kol);
                return gfx::Rectf{c.x + förb, c.y, c.w - förb, c.h};
            };

            g.setCurrentCell(1, 0);
            w.tangent(gfx::Key::F2);
            check(g.isEditing() && sammaRect(w.editorYta(), efter(1, 0)),
                  "W14: textfältet står efter radens ikon",
                  fog(w.editorYta()) + " mot " + fog(efter(1, 0)));
            w.tangent(gfx::Key::Escape);

            g.setCurrentCell(0, 3);
            w.tangent(gfx::Key::F2);
            const gfx::Rectf c03 = w.cellYta(0, 3);
            const float      v   = c03.x + pad + förb - m.fieldPadX * s;
            const float      h   = c03.x + c03.w - pad -
                            glaze::Draw::measure(g.behaviorCtx(), "px").w * s;
            const gfx::Rectf fält{v, c03.y, h - v, c03.h};
            check(g.isEditing() && sammaRect(w.editorYta(), fält),
                  "W14: talets fält står efter cellens bild och före suffixet",
                  fog(w.editorYta()) + " mot " + fog(fält));
            w.tangent(gfx::Key::Escape);

            w.klick(w.cell(2, 4));
            check(w.vallistan() && sammaRect(w.editorYta(), efter(2, 4)),
                  "W14: vallistan står efter cellens bild",
                  fog(w.editorYta()) + " mot " + fog(efter(2, 4)));
            g.cancelEdit();

            w.klick(w.cell(1, 2));
            check(w.färgväljaren() && sammaRect(w.editorYta(), efter(1, 2)),
                  "W14: färgväljaren står efter cellens bild",
                  fog(w.editorYta()) + " mot " + fog(efter(1, 2)));
            g.cancelEdit();
            break;
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
        std::printf("  --   W13–W14 hoppades över: inget fönster kunde skapas\n");
        ++g_skips;
    }
}

// P1–P9. RITNINGEN AV DE TYPADE CELLERNA — pixlar i fönsterbilden
// (dropdown_gate-formen), mot kontraktets geometri. Ytan är pixeln i cellens
// luft (`gridPadX` före värdet); "bläck" är pixlar som skiljer sig från den.
// SABOTAGEN står vid varje fall.
void fallTypadeCellernasRitning()
{
    TypVard w;
    w.setOffscreen(true);
    Prov&                    g    = w.grid;
    const glaze::ImageSource ikon = glaze::stockIcon(glaze::StockIcon::Save);
    g.setColumnEditor(4, {.editor      = Editor::Choice,
                          .choices     = {"Hel", "Streck"},
                          .choiceIcons = {ikon, {}}});
    g.setCellEditor(2, 4, {.editor = Editor::Choice, .choices = {"Hel", "Streck"}});
    g.setCellEditor(1, 3, {.editor = Editor::Number,
                           .prefix = "ca",
                           .suffix = "px"});
    g.setCell(2, 3, "abc");   // ett fel
    g.setColumnHeaderCheck(1, true);

    bool kordes = false;
    glaze::app().post([&] {
        kordes             = true;
        const Bild b       = bildAv(w);
        if (b.px.empty()) {
            std::printf("  --   P-fallen hoppas över: fönstrets bild gick inte "
                        "att läsa\n");
            ++g_skips;
            w.close();
            return;
        }
        const glaze::Ctx bc  = g.behaviorCtx();
        const auto&      col = bc.theme->colors;
        const auto&      m   = w.style();
        const float      s   = w.skala();
        const float      pad = m.gridPadX * s;
        const auto yta = [&](const Bild& bb, int p, int k) {
            const gfx::Rectf c = w.cellYta(p, k);
            return bb.at(int(c.x + 3.0f * s), int(c.y + 3.0f * s));
        };
        // Rutan (kryssets och färgens) vid cellens vänstra luft, v-centrerad.
        const auto ruta = [&](int p, int k) {
            const gfx::Rectf c    = w.cellYta(p, k);
            const float      sida = m.checkSize * s;
            return gfx::Rectf{c.x + pad, c.y + (c.h - sida) / 2.0f, sida, sida};
        };

        // ---- P1 KRYSSRUTAN: bocken i en kryssad cell, bara kanten i en
        //      okryssad. SABOTAGE: `drawCheck` med `on` alltid falskt.
        const float kant  = (m.checkRadius + m.borderWidth + 1.0f) * s;
        const int   inre  = bläck(b, indragen(ruta(0, 1), kant), yta(b, 0, 1));
        const int   falsk = bläck(b, ruta(0, 1), yta(b, 0, 1));
        const int   sann  = bläck(b, ruta(4, 1), yta(b, 4, 1));
        check(inre == 0 && sann > falsk,
              "P1: kryssrutan — bocken i den kryssade, ingen i den okryssade",
              std::to_string(sann) + " mot " + std::to_string(falsk) +
                  ", inre " + std::to_string(inre));

        // ---- P2 FÄRGRUTAN bär cellens färg. SABOTAGE: `drawColor` som fyller
        //      rutan med ytan (eller ritar texten utan ruta).
        const gfx::Color röd{255, 0, 0, 255};
        const gfx::Color grön{0, 255, 0, 255};
        const float      kantF = (m.colorSwatchRadius + m.borderWidth + 1.0f) * s;
        const gfx::Rectf rödRuta = indragen(ruta(0, 2), kantF);
        const int        röda    = iFärg(b, rödRuta, röd);
        check(2 * röda >= int(rödRuta.w) * int(rödRuta.h) &&
                  iFärg(b, ruta(1, 2), röd) == 0 &&
                  iFärg(b, indragen(ruta(1, 2), kantF), grön) > 0,
              "P2: färgrutan har cellens färg (röd i den röda, grön i den gröna)",
              std::to_string(röda));

        // ---- P10 FÄRGCELLENS TRE LÄGEN. Swatch (förvalet): rutan
        //      `colorCellSwatchW` bred och `checkSize` hög, ingenting efter.
        //      Both: rutan i `checkSize` och hextexten efter `checkGap`. Hex:
        //      hextexten, ingen ruta. SABOTAGE: `drawColor` som ritar
        //      hextexten i Swatch, rutan i `checkSize` i Swatch, eller rutan
        //      också i Hex.
        {
            const gfx::Rectf c     = w.cellYta(0, 2);
            const float      sida  = m.checkSize * s;
            const float      bred  = m.colorCellSwatchW * s;
            const float      y0    = c.y + (c.h - sida) / 2.0f;
            const float      slut  = c.x + c.w - pad;
            const gfx::Rectf bredRuta{c.x + pad, y0, bred, sida};
            const gfx::Rectf inne = indragen(bredRuta, kantF);
            const gfx::Rectf efter{c.x + pad + bred + 2.0f * s, c.y + 3.0f * s,
                                   std::max(0.0f, slut - (c.x + pad + bred +
                                                          2.0f * s)),
                                   c.h - 6.0f * s};
            const int rödaBred = iFärg(b, inne, röd);
            const int iEfter   = bläck(b, efter, yta(b, 0, 2));
            check(2 * rödaBred >= int(inne.w) * int(inne.h) && iEfter == 0,
                  "P10: Swatch — rutan är colorCellSwatchW bred och ingen "
                  "hextext ritas",
                  std::to_string(rödaBred) + " röda, " + std::to_string(iEfter) +
                      " efter");
            g.setColumnEditor(2, {.editor       = Editor::Color,
                                  .colorDisplay = glaze::ColorDisplay::Both});
            const Bild       bH = bildAv(w);
            const float      tx = c.x + pad + sida + m.checkGap * s;
            const gfx::Rectf text{tx, c.y + 3.0f * s, std::max(0.0f, slut - tx),
                                  c.h - 6.0f * s};
            const gfx::Rectf bortom{c.x + pad + sida + 2.0f * s, y0,
                                    m.checkGap * s - 4.0f * s, sida};
            check(!bH.px.empty() && bläck(bH, text, yta(bH, 0, 2)) > 0 &&
                      iFärg(bH, indragen(ruta(0, 2), kantF), röd) > 0 &&
                      iFärg(bH, bortom, röd) == 0,
                  "P10: Both — rutan är checkSize och hextexten står efter den",
                  std::to_string(bläck(bH, text, yta(bH, 0, 2))));
            g.setColumnEditor(2, {.editor       = Editor::Color,
                                  .colorDisplay = glaze::ColorDisplay::Hex});
            const Bild       bX = bildAv(w);
            const gfx::Rectf hela{c.x + pad, c.y + 3.0f * s,
                                  std::max(0.0f, slut - (c.x + pad)),
                                  c.h - 6.0f * s};
            check(!bX.px.empty() && iFärg(bX, hela, röd) == 0 &&
                      bläck(bX, hela, yta(bX, 0, 2)) > 0,
                  "P10: Hex — hextexten ritas och ingen ruta",
                  std::to_string(iFärg(bX, hela, röd)) + " röda");
            g.setColumnEditor(2, {.editor = Editor::Color});
        }

        // ---- P3 TALET MED PREFIX OCH SUFFIX: prefixet vänster, suffixet
        //      höger, talet (högerjusterat) `fieldPadX` före suffixet och tomt
        //      emellan. SABOTAGE: `drawNumber` utan fältets luft före
        //      suffixet, eller med talet ovanpå prefixet.
        {
            const gfx::Rectf c   = w.cellYta(1, 3);
            const float      y0  = c.y + 3.0f * s;
            const float      hz  = c.h - 6.0f * s;
            const float      cx  = c.x + pad;
            const float      cr  = c.x + c.w - pad;
            const float      pW  = glaze::Draw::measure(bc, "ca").w * s;
            const float      sW  = glaze::Draw::measure(bc, "px").w * s;
            const float      nW  = glaze::Draw::measure(bc, "20").w * s;
            const float      fp  = m.fieldPadX * s;
            const float      slut = cr - sW - fp;
            const auto zon = [&](float a, float e) {
                return gfx::Rectf{a, y0, std::max(0.0f, e - a), hz};
            };
            const std::uint32_t y = yta(b, 1, 3);
            const int iPrefix = bläck(b, zon(cx, cx + pW), y);
            const int iTal    = bläck(b, zon(slut - nW, slut), y);
            const int iSuffix = bläck(b, zon(cr - sW, cr), y);
            const int iLucka  = bläck(b, zon(slut + 2.0f * s, cr - sW - 2.0f * s), y);
            const int iMellan =
                bläck(b, zon(cx + pW + 2.0f * s, slut - nW - 2.0f * s), y);
            check(iPrefix > 0 && iTal > 0 && iSuffix > 0 && iLucka == 0 &&
                      iMellan == 0,
                  "P3: prefixet, talet och suffixet på sina platser — talet "
                  "mellan dem",
                  std::to_string(iPrefix) + "/" + std::to_string(iTal) + "/" +
                      std::to_string(iSuffix) + " lucka " +
                      std::to_string(iLucka) + " mellan " +
                      std::to_string(iMellan));

            // ---- P11 EN LÅST TALCELL ritas som sitt slag — prefixet, talet
            //      och suffixet på samma platser — på det skrivskyddade
            //      fältets yta. SABOTAGE: `drawCellValue` som ritar en låst
            //      cell som text (basens), eller `readOnlySurface` som inte
            //      frågar `readOnly` (ytan är cellens vanliga).
            g.setCellEditor(1, 3, {.editor   = Editor::Number,
                                   .prefix   = "ca",
                                   .suffix   = "px",
                                   .readOnly = true});
            const Bild          bL = bildAv(w);
            const std::uint32_t yL = yta(bL, 1, 3);
            check(!bL.px.empty() && !lika(col.fieldReadOnly, col.panel) &&
                      nästan(yL, col.fieldReadOnly) &&
                      bläck(bL, zon(cx, cx + pW), yL) > 0 &&
                      bläck(bL, zon(slut - nW, slut), yL) > 0 &&
                      bläck(bL, zon(cr - sW, cr), yL) > 0 &&
                      bläck(bL, zon(cx + pW + 2.0f * s, slut - nW - 2.0f * s),
                            yL) == 0,
                  "P11: den låsta talcellen visar prefixet, talet och suffixet "
                  "på fieldReadOnly");
            g.setCellEditor(1, 3, {.editor = Editor::Number,
                                   .prefix = "ca",
                                   .suffix = "px"});
        }

        // ---- P4 FELETS YTA: en otolkbar talcell fylls med `errorSurface`.
        //      SABOTAGE: `drawCellValue` utan `drawError`.
        check(nästan(yta(b, 2, 3), col.errorSurface) &&
                  !nästan(yta(b, 0, 3), col.errorSurface),
              "P4: felets cell på errorSurface, en giltig cell inte");

        // ---- P5 KNAPPENS YTA: `button` innanför cellens indrag. SABOTAGE:
        //      `drawCellButton` som bara ritar texten.
        {
            const gfx::Rectf c  = w.cellYta(0, 5);
            const float      in = m.cellButtonInset * s;
            check(!lika(col.button, col.panel) &&
                      nästan(b.at(int(c.x + in + 3.0f * s), int(c.y + c.h / 2.0f)),
                             col.button),
                  "P5: knappcellen har knappens yta");
        }

        // ---- P6 VALETS IKON före texten: samma text med ikon står ikonens
        //      sida + `iconGap` längre till höger. SABOTAGE: Choice-grenen i
        //      `drawCellValue` utan ikonen.
        {
            const auto innehåll = [&](int p) {
                const gfx::Rectf c = w.cellYta(p, 4);
                return gfx::Rectf{c.x + pad, c.y + 2.0f * s, c.w - 2.0f * pad,
                                  c.h - 4.0f * s};
            };
            const float k    = ikon.sizeClass();
            const float sida = std::min(k > 0.0f ? k : m.iconFallback, m.gridRowH);
            const float steg = (sida + m.iconGap) * s;
            const int   h0   = högerkant(b, innehåll(0), yta(b, 0, 4));
            const int   h2   = högerkant(b, innehåll(2), yta(b, 2, 4));
            check(h0 >= 0 && h2 >= 0 && std::abs(float(h0 - h2) - steg) <= 1.5f &&
                      bläck(b, innehåll(0), yta(b, 0, 4)) >
                          bläck(b, innehåll(2), yta(b, 2, 4)),
                  "P6: valets ikon före texten — texten flyttad ikonens steg",
                  std::to_string(h0 - h2) + " mot " + std::to_string(steg));
        }

        // ---- P7 KATEGORIRADEN: `panelHeader` över hela bredden, utan
        //      rutnätets linjer. SABOTAGE: kategoriraden ritad som en datarad
        //      (`drawCells` utan slagvakten) eller rutnätet över den.
        const gfx::Rectf ri = w.inre();
        {
            const gfx::Rectf r = w.cellYta(3, 0);
            const gfx::Rectf remsa{ri.x + s, r.y + 2.0f * s, ri.w - 2.0f * s,
                                   2.0f * s};
            check(!lika(col.panelHeader, col.panel) &&
                      utanförFärg(b, remsa, col.panelHeader) == 0 &&
                      iFärg(b, remsa, col.panelHeader) > 0,
                  "P7: kategoriraden i panelHeader över hela bredden",
                  std::to_string(utanförFärg(b, remsa, col.panelHeader)));
        }

        // ---- P8 TOMRADEN: inga rutnätslinjer och inget innehåll — bara
        //      ytan; datraden ovanför har linjer. SABOTAGE: rutnätet utan
        //      slagvakten i `draw`.
        {
            const gfx::Rectf r5 = w.cellYta(5, 0);
            const gfx::Rectf r4 = w.cellYta(4, 0);
            const gfx::Rectf tom{ri.x + s, r5.y + 2.0f * s, ri.w - 2.0f * s,
                                 r5.h - 4.0f * s};
            const gfx::Rectf data{ri.x + s, r4.y + 2.0f * s, ri.w - 2.0f * s,
                                  r4.h - 4.0f * s};
            check(utanförFärg(b, tom, col.panel) == 0 &&
                      utanförFärg(b, data, col.panel) > 0,
                  "P8: tomraden har varken rutnät eller innehåll",
                  std::to_string(utanförFärg(b, tom, col.panel)));
        }

        // ---- P9 RUBRIKKRYSSET i tre lägen: Delvis (strecket) och Kryssad
        //      (bocken) har mer bläck än Okryssad, och de två skiljer sig.
        //      SABOTAGE: `drawHeader` som ritar rutan utan läget, eller
        //      `headerCheckState` som svarar två lägen.
        {
            const float      hx = ri.x + g.columnWidth(0) * s;
            const gfx::Rectf zon{hx + pad, ri.y + 2.0f * s, m.checkSize * s,
                                 w.huvudH() - 4.0f * s};
            const auto ytaH = [&](const Bild& bb) {
                return bb.at(int(hx + 2.0f * s), int(ri.y + w.huvudH() / 2.0f));
            };
            const int iDelvis = bläck(b, zon, ytaH(b));
            g.setCell(4, 1, "false");
            const Bild bU = bildAv(w);
            const int  iAv = bläck(bU, zon, ytaH(bU));
            for (const int r : {0, 1, 2, 4, 6})
                g.setCell(r, 1, "true");
            const Bild bC = bildAv(w);
            const int  iPå = bläck(bC, zon, ytaH(bC));
            check(iAv < iDelvis && iAv < iPå && skillnad(b, bC, zon) > 0,
                  "P9: rubrikkrysset — okryssat, delvis och kryssat ritas olika",
                  std::to_string(iAv) + "/" + std::to_string(iDelvis) + "/" +
                      std::to_string(iPå));
        }
        w.close();
    });
    w.run();
    if (!kordes) {
        std::printf("  --   P-fallen hoppades över: inget fönster kunde skapas\n");
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
    fallTypadeCeller();
    fallDoldaRaderOchOrdningen();
    fallTypadeBredderOchTooltip();
    fallMedFonster();
    fallAutosizeEditor();
    fallBorttagningen();
    fallTypadeCellerMedFonster();
    fallEditornOchEnter();
    fallTypadeCellernasRitning();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
