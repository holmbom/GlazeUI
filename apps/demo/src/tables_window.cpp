#include "tables_window.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

// ------------------------------------------------------------- måtten

constexpr int   kFonsterB      = 1560;   // innehållsytan
constexpr int   kFonsterH      = 980;
constexpr int   kFonsterMinB   = 1100;
constexpr int   kFonsterMinH   = 700;
constexpr float kReglageBredd  = 430.0f;   // reglagekolumnen
constexpr float kReglageMin    = 320.0f;
constexpr float kTabellMin     = 480.0f;
constexpr float kLoggHojd      = 240.0f;
constexpr float kKant          = 12.0f;    // luft mot ytornas kanter
constexpr float kLuft          = 8.0f;     // luft mellan rader
constexpr float kRubrikLuft    = 20.0f;    // luft ovanför en ny widget
constexpr float kRadGap        = 8.0f;     // luft mellan kontroller i en rad
constexpr float kValBredd      = 130.0f;   // vallistorna
constexpr float kTalBredd      = 90.0f;    // talfälten
constexpr float kFastHojd      = 260.0f;   // "fast höjd"
constexpr double kBreddMaxSpin = 600.0;    // talfältens kolumnbredd

// Datagridens kolumner.
constexpr float kNamnW      = 120.0f;
constexpr float kHojdW      = 96.0f;
constexpr float kLinjeW     = 120.0f;
constexpr float kKommentarW = 120.0f;
// Listvyns kolumner.
constexpr float kLvNamnW  = 170.0f;
constexpr float kLvTypW   = 80.0f;
constexpr float kLvDatumW = 100.0f;
// Loggens kolumner.
constexpr float kLoggNrW       = 48.0f;
constexpr float kLoggKallaW    = 100.0f;
constexpr float kLoggHandelseW = 120.0f;
constexpr float kLoggArgsW     = 300.0f;
constexpr int   kLoggTak       = 2000;   // rader innan den äldsta hälften går
constexpr int   kLoggGallring  = kLoggTak / 2;

// Gränserna i lagerlistan och egenskapslistan.
constexpr double kBreddMax       = 20.0;   // linjebredd i px
constexpr int    kBreddDecimaler = 1;
constexpr double kProcentMax     = 100.0;
constexpr double kSkalaMin       = 1.0;
constexpr double kMmMax          = 500.0;  // en cells egna gränser
constexpr int    kHojdDecimaler  = 1;

// Uppdateringen 60 gånger i sekunden och mängdfyllningen.
constexpr int      kTaktMs      = 1000 / 60;
constexpr int      kSlumpForsok = 8;      // försök att träffa en datarad
constexpr unsigned kSlumpFro    = 2026u;
constexpr int      kMassRader   = 20000;
constexpr int      kAvsnitt     = 500;    // rader per kategori i mängden
constexpr int      kFargStegR   = 37;     // mängdens färger, ett steg per rad
constexpr int      kFargStegG   = 53;
constexpr int      kFargStegB   = 91;
constexpr int      kFargKanal   = 256;
constexpr int      kSkalaSteg   = 100;
constexpr int      kSkalaAntal  = 10;
constexpr int      kTiondelar   = 10;
constexpr std::size_t kHexRgbLangd = 7;   // "#RRGGBB" ur "#RRGGBBAA"

// Radfälten: −1 är "ingen rad", och den största mängden ryms.
constexpr double kIngetIndex = -1.0;
constexpr double kIndexMax   = double(kMassRader + kMassRader / kAvsnitt);

// En tillagd fil i listvyn är 1 kB, och sorteras på det.
constexpr double kNyFilByte = 1024.0;
// Egenskapslistans rad 2 (Linjetyp), som reglaget inaktiverar.
constexpr int kPgLinjetypRad = 2;
// Steget när egenskapslistans `setRow` byter ett tal.
constexpr double kVardeSteg = 1.0;
// Reglagekolumnens fot: en px bred, så den finns i ytans utsträckning.
constexpr float kFotBredd = 1.0f;

constexpr gfx::Color kRod{204, 68, 68, 255};        // radens färg
constexpr gfx::Color kBlaYta{210, 228, 250, 255};   // cellens bakgrund
constexpr gfx::Color kBlaText{30, 70, 150, 255};    // cellens text

// Listvyns ikonstorlekar: AUTO och tre satta.
constexpr float kIkonStorlekar[] = {0.0f, 16.0f, 24.0f, 32.0f};
// Huvudets rader: 1, 2 och AUTO — AUTO är bygget (`setHeaderLines(0)`).
constexpr int kHuvudRader[]    = {1, 2, 0};
constexpr int kHuvudRaderStart = 2;
// Huvudets läge i höjdled, i enumens ordning.
constexpr glaze::VAlign kHuvudLagen[] = {glaze::VAlign::Top, glaze::VAlign::Center,
                                         glaze::VAlign::Bottom};
// Färgkolumnens visning, i vallistans ordning.
constexpr glaze::ColorDisplay kFargVisningar[] = {glaze::ColorDisplay::Swatch,
                                                  glaze::ColorDisplay::Hex,
                                                  glaze::ColorDisplay::Both};

constexpr glaze::UI        kUIs[]   = {glaze::UI::Glaze, glaze::UI::Slate,
                                       glaze::UI::Minimalistic};
constexpr int              kUIStart = 1;   // Slate, som fönstret sätter
constexpr glaze::ThemeMode kLagen[] = {glaze::ThemeMode::System,
                                       glaze::ThemeMode::Light,
                                       glaze::ThemeMode::Dark};

// ------------------------------------------------------------ kolumnerna

using E = glaze::DataGrid::Editor;
using S = glaze::ColumnSizing;
using K = glaze::Table::RowKind;

enum DgKolumn : int {
    kNamn, kSynlig, kFarg, kFargAlfa, kBredd, kOpacitet, kSkala, kHojd, kLinje,
    kKommentar, kAntal, kTaBort,
};

enum LvKolumn : int { kLvKolNamn, kLvKolTyp, kLvKolStorlek, kLvKolDatum };

// Radens slag i enumens ordning.
const char* const kSlagNamn[] = {"Data", "Category", "Blank"};

const std::vector<std::string> kDgNamn = {
    "Namn", "Synlig", "Färg", "Färg med alfa", "Bredd", "Opacitet", "Skala",
    "Höjd", "Linjetyp", "Kommentar", "Antal", "Ta bort"};
const std::vector<std::string> kLvNamn = {"Namn", "Typ", "Storlek", "Ändrad"};
const std::vector<std::string> kLagesNamn = {"Manual", "AutoMin", "Autosize"};

// Redigerarnas namn i enumens ordning.
const char* const kEditorNamn[] = {"Text", "Choice", "Check", "Color",
                                   "Number", "Button", "None"};

std::vector<std::string> medIngen(const std::vector<std::string>& namn)
{
    std::vector<std::string> v{"Ingen"};
    v.insert(v.end(), namn.begin(), namn.end());
    return v;
}

glaze::ImageSource ikon(glaze::StockIcon i)
{
    return glaze::stockIcon(i);
}

// Linjetypens vallista, med en ikon per val.
glaze::DataGrid::EditorSpec linjeSpec()
{
    return {.editor      = E::Choice,
            .choices     = {"Linje", "Polylinje", "Punkter", "Kurva"},
            .choiceIcons = {ikon(glaze::StockIcon::Line),
                            ikon(glaze::StockIcon::Polyline),
                            ikon(glaze::StockIcon::Point),
                            ikon(glaze::StockIcon::Spline)}};
}

// ----------------------------------------------------------- loggens text

std::string lista(const std::vector<int>& v)
{
    std::string s = "{";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i)
            s += ", ";
        s += std::to_string(v[i]);
    }
    return s + "}";
}

std::string citat(const std::string& s)
{
    return "\"" + s + "\"";
}

std::string tal(double v)
{
    char b[32];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

std::string hex(gfx::Color c)
{
    char b[16];
    std::snprintf(b, sizeof b, "#%02X%02X%02X%02X", c.r, c.g, c.b, c.a);
    return b;
}

std::string sant(bool b)
{
    return b ? "true" : "false";
}

// ------------------------------------------------------------- byggstenar

// En rad reglage: en ren Bar med luft mellan kontrollerna.
class Rad final : public glaze::Bar {
public:
    Rad() { setPlain(true); }

protected:
    float childGap(const glaze::Ctx&) const override { return kRadGap; }
};

// Staplar widgets i en yta, uppifrån och ned och sträckta mellan ytans kanter,
// med syskonankare: varje widget fäster under den förra.
class Stapel {
public:
    Stapel(glaze::Widget& yta, std::string prefix, float kant)
        : yta_(yta), prefix_(std::move(prefix)), kant_(kant)
    {
    }

    template <class W, class... A>
    W* lagg(float gap, A&&... a)
    {
        W* w       = yta_.add<W>(std::forward<A>(a)...);
        w->name    = prefix_ + std::to_string(n_++);
        w->x       = kant_;
        w->right   = kant_;
        w->anchors = {true, true, true, false};
        if (over_.empty())
            w->y = gap;
        else
            w->anchorTop = {over_, gap};
        over_ = w->name;
        return w;
    }

    Rad* rad() { return lagg<Rad>(over_.empty() ? 0.0f : kLuft); }

private:
    glaze::Widget& yta_;
    std::string    prefix_;
    float          kant_;
    std::string    over_;
    int            n_ = 0;
};

glaze::Checkbox* kryss(glaze::Bar& r, const char* text, bool pa,
                       std::function<void(bool)> fn)
{
    auto* k = r.add<glaze::Checkbox>(text);
    k->setChecked(pa);
    k->onChange = [k, fn = std::move(fn)] { fn(k->isChecked()); };
    return k;
}

glaze::Dropdown* val(glaze::Bar& r, const char* etikett,
                     std::vector<std::string> poster, int index,
                     std::function<void(int)> fn)
{
    r.add<glaze::Label>(etikett);
    auto* d  = r.add<glaze::Dropdown>();
    d->width = kValBredd;
    d->setItems(std::move(poster));
    d->setSelectedIndex(index);
    d->onChange = [d, fn = std::move(fn)] { fn(d->selectedIndex()); };
    return d;
}

glaze::SpinBox* talfalt(glaze::Bar& r, const char* etikett, double varde,
                        std::function<void(double)> fn)
{
    r.add<glaze::Label>(etikett);
    auto* s  = r.add<glaze::SpinBox>();
    s->width = kTalBredd;
    s->setMin(0.0);
    s->setMax(kBreddMaxSpin);
    s->setDecimals(0);
    s->setValue(varde);
    s->onCommit = [s, fn = std::move(fn)] { fn(s->value()); };
    return s;
}

void knapp(glaze::Bar& r, const char* text, std::function<void()> fn)
{
    auto* b    = r.add<glaze::Button>(text);
    b->onClick = [fn = std::move(fn)] { fn(); };
}

// En verktygsknapp tar inte fokus, så en pågående redigering står kvar.
void verktyg(glaze::Bar& r, const char* text, const char* tips,
             std::function<void()> fn)
{
    auto* b = r.add<glaze::ToolButton>(text);
    b->setToolTip(tips);
    b->onClick = [fn = std::move(fn)] { fn(); };
}

glaze::GroupBox* grupp(Stapel& s, const char* rubrik)
{
    return s.lagg<glaze::GroupBox>(kLuft, rubrik);
}

// Ett radfält: ett heltal från −1 (ingen rad) som en knapp bredvid läser.
glaze::SpinBox* indexfalt(glaze::Bar& r, const char* etikett)
{
    r.add<glaze::Label>(etikett);
    auto* s  = r.add<glaze::SpinBox>();
    s->width = kTalBredd;
    s->setMin(kIngetIndex);
    s->setMax(kIndexMax);
    s->setDecimals(0);
    s->setValue(0.0);
    return s;
}

// Fönstrets loggrad: händelsen och args.
using Logg = std::function<void(const std::string&, const std::string&)>;

// Tabellens aktuella rad, eller −1 med en loggrad när ingen finns.
int aktuellRad(const glaze::Table& t, const Logg& logg)
{
    const int row = t.currentRow();
    if (row < 0)
        logg("Ingen aktuell rad", "klicka i en rad först");
    return row;
}

// VALET från koden, för DataGrid och ListView (samma namn, olika klasser):
// den aktuella raden väljs eller väljs bort, och alla eller inga.
template <class T>
void valKnappar(Stapel& i, T* t, const Logg& logg)
{
    auto rapport = [t, logg](const std::string& handelse, const std::string& args) {
        logg(handelse, args + " selectedRows=" + lista(t->selectedRows()));
    };
    Rad* r = i.rad();
    knapp(*r, "Välj aktuell rad", [t, logg, rapport] {
        const int row = aktuellRad(*t, logg);
        if (row < 0)
            return;
        t->setSelected(row, true);
        rapport("setSelected", "row=" + std::to_string(row) + " on=true");
    });
    knapp(*r, "Välj bort", [t, logg, rapport] {
        const int row = aktuellRad(*t, logg);
        if (row < 0)
            return;
        t->setSelected(row, false);
        rapport("setSelected", "row=" + std::to_string(row) + " on=false");
    });
    r = i.rad();
    knapp(*r, "selectAll", [t, rapport] {
        t->selectAll();
        rapport("selectAll", "multiSelect=" + sant(t->isMultiSelect()));
    });
    knapp(*r, "clearSelection", [t, rapport] {
        t->clearSelection();
        rapport("clearSelection", "");
    });
}

// RADENS SLAG på den aktuella raden: den blir en data-, kategori- eller tomrad.
void slagKnappar(Stapel& i, glaze::Table* t, const Logg& logg)
{
    Rad* r = i.rad();
    r->add<glaze::Label>("Aktuell rad blir");
    const std::pair<const char*, K> slag[] = {
        {"Data", K::Data}, {"Kategori", K::Category}, {"Tomrad", K::Blank}};
    for (const auto& [text, kind] : slag) {
        knapp(*r, text, [t, logg, kind = kind] {
            const int row = aktuellRad(*t, logg);
            if (row < 0)
                return;
            const K fore = t->rowKind(row);
            t->setRowKind(row, kind);
            logg("setRowKind", "row=" + std::to_string(row) + " " +
                                   kSlagNamn[int(fore)] + " -> " +
                                   kSlagNamn[int(t->rowKind(row))]);
        });
    }
}

// RADENS OCH CELLENS FÄRG av och på: raden är den aktuella, och kolumnen
// svarar `kolumn` (−1 = ingen cell, med en loggrad).
void fargKnappar(Stapel& i, glaze::Table* t, std::function<int()> kolumn,
                 const Logg& logg)
{
    Rad* r = i.rad();
    knapp(*r, "Radfärg på", [t, logg] {
        const int row = aktuellRad(*t, logg);
        if (row < 0)
            return;
        t->setRowColor(row, kRod);
        logg("setRowColor", "row=" + std::to_string(row) + " " + hex(kRod));
    });
    knapp(*r, "Radfärg av", [t, logg] {
        const int row = aktuellRad(*t, logg);
        if (row < 0)
            return;
        t->setRowColor(row, std::nullopt);
        logg("setRowColor", "row=" + std::to_string(row) + " nullopt");
    });
    r = i.rad();
    auto cellen = [t, logg, kolumn](int& row, int& col) {
        row = aktuellRad(*t, logg);
        col = kolumn();
        if (row >= 0 && col < 0)
            logg("Ingen kolumn", "välj en kolumn först");
        return row >= 0 && col >= 0;
    };
    knapp(*r, "Cellfärg på", [t, logg, cellen] {
        int row = 0, col = 0;
        if (!cellen(row, col))
            return;
        t->setCellColor(row, col, kBlaYta, kBlaText);
        logg("setCellColor", "row=" + std::to_string(row) + " column=" +
                                 std::to_string(col) + " " + hex(kBlaYta) + " " +
                                 hex(kBlaText));
    });
    knapp(*r, "Cellfärg av", [t, logg, cellen] {
        int row = 0, col = 0;
        if (!cellen(row, col))
            return;
        t->setCellColor(row, col, std::nullopt, std::nullopt);
        logg("setCellColor", "row=" + std::to_string(row) + " column=" +
                                 std::to_string(col) + " nullopt nullopt");
    });
}

} // namespace

// ------------------------------------------------------------------ fönstret

TablesWindow::TablesWindow() : slump_(kSlumpFro)
{
    setUI(kUIs[kUIStart]);
    setTitle("Tabeller — DataGrid, ListView och PropertyGrid");
    setContentSize(kFonsterB, kFonsterH);
    setMinimumContentSize(kFonsterMinB, kFonsterMinH);
    setStartPosition(glaze::StartPosition::CenterScreen);

    byggLogg();

    auto* delare = add<glaze::Splitter>();
    delare->name = "delare";
    delare->dock = glaze::Dock::Fill;
    delare->setFixedPane(glaze::Splitter::Pane::First);
    delare->setPaneSize(kReglageBredd);
    delare->setFirstMin(kReglageMin);
    delare->setSecondMin(kTabellMin);
    auto* vanster = delare->add<glaze::Panel>();
    vanster->name = "reglage";
    auto* hoger   = delare->add<glaze::Panel>();
    hoger->name   = "tabeller";

    byggTabeller(*hoger);
    byggReglage(*vanster);
    kopplaEvents();

    takt_.setInterval(kTaktMs);
    takt_.onTick = [this] { uppdateraSlumpcell(); };
}

// ---------------------------------------------------------------- loggen

void TablesWindow::byggLogg()
{
    auto* yta   = add<glaze::Panel>();
    yta->name   = "loggYta";
    yta->dock   = glaze::Dock::Bottom;
    yta->height = kLoggHojd;
    yta->setBorder(true);

    Stapel s(*yta, "logg", kLuft);
    Rad*   topp = s.lagg<Rad>(kLuft);
    topp->add<glaze::Label>("Händelser")->setRole(glaze::Label::Role::Heading);
    topp->addSpacer();
    knapp(*topp, "Töm", [this] {
        logg_->setRows({});
        loggNr_ = 0;
    });

    auto* regler = s.lagg<glaze::Label>(
        kLuft,
        "Prova tangenterna: Enter öppnar en redigerbar cell · Enter i "
        "redigeraren sparar och flyttar ner · Tab går till nästa redigerbara "
        "cell · Space växlar ett kryss och klickar en knapp · Vänster/Höger "
        "fäller en kategori · F2 börjar redigera · Escape ångrar · "
        "Ctrl/Shift+klick väljer rader i FullRow med flerval");
    regler->setWrap(true);
    regler->setRole(glaze::Label::Role::Dim);

    logg_ = s.lagg<glaze::ListView>(kLuft);
    logg_->anchors = {true, true, true, true};
    logg_->bottom  = kLuft;
    logg_->setColumns({{"#", kLoggNrW, glaze::Align::Right},
                       {.text = "Källa", .width = kLoggKallaW},
                       {.text = "Händelse", .width = kLoggHandelseW},
                       {.text = "Args", .width = kLoggArgsW, .sizing = S::Autosize}});
    logg_->setSortable(false);
}

void TablesWindow::logga(const std::string& kalla, const std::string& handelse,
                         const std::string& args)
{
    if (logg_->rowCount() >= kLoggTak) {
        std::vector<glaze::Table::Row> rader = logg_->rows();
        rader.erase(rader.begin(), rader.begin() + kLoggGallring);
        logg_->setRows(std::move(rader));
    }
    logg_->addRow({std::to_string(++loggNr_), kalla, handelse, args});
    logg_->ensureVisible(logg_->rowCount() - 1);
}

// ------------------------------------------------------------- tabellerna

void TablesWindow::byggTabeller(glaze::Panel& yta)
{
    Stapel s(yta, "tab", kKant);

    s.lagg<glaze::Label>(kKant, "DataGrid — en lagerlista med typade celler")
        ->setRole(glaze::Label::Role::Heading);
    grid_ = s.lagg<glaze::DataGrid>(kLuft);
    grid_->setColumns({
        {.text = "Namn", .width = kNamnW},
        {.text    = "Synlig",
         .sizing  = S::AutoMin,
         .toolTip = "Visas lagret? Rubrikens kryss slår på eller av alla",
         .headerCheck = true},
        {.text = "Färg", .sizing = S::AutoMin},
        {.text    = "Färg\nmed alfa",
         .sizing  = S::AutoMin,
         .toolTip = "Färgväljaren med alfa: #RRGGBBAA"},
        {.text = "Bredd", .align = glaze::Align::Right, .sizing = S::AutoMin},
        {.text    = "Opac.",
         .align   = glaze::Align::Right,
         .sizing  = S::AutoMin,
         .toolTip = "Opacitet i procent"},
        {.text    = "Skala",
         .align   = glaze::Align::Right,
         .sizing  = S::AutoMin,
         .toolTip = "Skalan som 1:n, ett heltal"},
        {.text    = "Höjd",
         .width   = kHojdW,
         .align   = glaze::Align::Right,
         .toolTip = "Tom höjd är Automatisk"},
        {.text = "Linjetyp", .width = kLinjeW},
        {.text = "Kommentar", .width = kKommentarW, .sizing = S::Autosize},
        {.text = "Antal", .align = glaze::Align::Right, .sizing = S::AutoMin},
        {.text = "", .sizing = S::AutoMin, .toolTip = "Ta bort raden"},
    });
    grid_->setColumnEditor(kSynlig, {.editor = E::Check});
    grid_->setColumnEditor(kFarg, {.editor = E::Color});
    grid_->setColumnEditor(kFargAlfa, {.editor       = E::Color,
                                       .allowAlpha   = true,
                                       .colorDisplay = glaze::ColorDisplay::Both});
    grid_->setColumnEditor(kBredd, {.editor   = E::Number,
                                    .min      = 0.0,
                                    .max      = kBreddMax,
                                    .decimals = kBreddDecimaler,
                                    .suffix   = " px"});
    grid_->setColumnEditor(kOpacitet, {.editor   = E::Number,
                                       .min      = 0.0,
                                       .max      = kProcentMax,
                                       .decimals = 0,
                                       .suffix   = " %"});
    grid_->setColumnEditor(kSkala, {.editor   = E::Number,
                                    .min      = kSkalaMin,
                                    .decimals = 0,
                                    .prefix   = "1:"});
    grid_->setColumnEditor(kHojd, {.editor     = E::Number,
                                   .min        = 0.0,
                                   .decimals   = kHojdDecimaler,
                                   .suffix     = " m",
                                   .allowEmpty = true,
                                   .emptyText  = "Automatisk"});
    grid_->setColumnEditor(kLinje, linjeSpec());
    grid_->setColumnEditor(kKommentar, {.editor = E::Text});
    grid_->setColumnEditor(kAntal, {.editor = E::None});
    grid_->setColumnEditor(kTaBort, {.editor = E::Button});
    grid_->setHeaderLines(0);
    grid_->setToolTip("Datagridens egen tooltip");
    fyllDataGrid();

    s.lagg<glaze::Label>(kRubrikLuft, "ListView — filer i kategorier")
        ->setRole(glaze::Label::Role::Heading);
    lista_ = s.lagg<glaze::ListView>(kLuft);
    lista_->setColumns({
        {.text = "Namn", .width = kLvNamnW},
        {.text = "Typ", .width = kLvTypW},
        {.text    = "Storlek",
         .align   = glaze::Align::Right,
         .sizing  = S::AutoMin,
         .toolTip = "Sorteras på antalet byte"},
        {.text = "Ändrad", .width = kLvDatumW, .sizing = S::Autosize},
    });
    lista_->setShowChecks(true);
    lista_->setColumnHeaderCheck(0, true);
    lista_->setColumnToolTip(0, "Rubrikens kryss slår på eller av alla synliga rader");
    fyllListView();

    s.lagg<glaze::Label>(kRubrikLuft, "PropertyGrid — ett lagers egenskaper")
        ->setRole(glaze::Label::Role::Heading);
    egen_ = s.lagg<glaze::PropertyGrid>(kLuft);
    egen_->setToolTip("Egenskapslistans egen tooltip");
    fyllPropertyGrid();

    // Luft under den sista widgeten när ytan rullar.
    auto* fot = s.lagg<glaze::Spacer>(0.0f);
    fot->height = kKant;
}

void TablesWindow::fyllDataGrid()
{
    std::vector<glaze::Table::Row> r;
    auto lager = [&](std::vector<std::string> celler, glaze::ImageSource img = {},
                     std::optional<gfx::Color> farg = std::nullopt) {
        r.push_back({std::move(celler), std::move(img), farg});
    };
    auto kategori = [&](const char* text, bool utfalld) {
        r.push_back({.cells = {text}, .kind = K::Category, .expanded = utfalld});
    };
    const glaze::ImageSource lagerIkon = ikon(glaze::StockIcon::Layers);

    // En rad före den första kategorin hör inte till någon och syns alltid.
    lager({"Rutnät", "true", "#A0A0A0", "#A0A0A040", "0.5", "40", "1000", "",
           "Linje", "Hjälplinjer", "12", "Ta bort"});
    kategori("Bakgrund", true);
    lager({"Plattor", "true", "#C8C8C8", "#C8C8C8FF", "1", "100", "500", "",
           "Linje", "", "340", "Ta bort"},
          lagerIkon);
    lager({"Ytor", "true", "#7FB77E", "#7FB77E99", "0.5", "60", "500", "2.5",
           "Polylinje", "Låst linjetyp", "88", "Ta bort"},
          lagerIkon);
    lager({"Vatten", "false", "#3C78D8", "#3C78D8CC", "0", "80", "2000", "",
           "Kurva", "Med en bild", "17", "Ta bort"},
          lagerIkon);
    r.push_back({.kind = K::Blank});
    kategori("Förgrund", true);
    lager({"Vägar", "true", "#D04030", "#D04030FF", "2.5", "100", "500", "",
           "Polylinje", "Går inte att ta bort", "1204", "Ta bort"});
    lager({"Etiketter", "true", "#202020", "#202020E6", "1", "90", "500", "",
           "Linje", "Höjden är tom", "560", "Ta bort"});
    lager({"Stolpar", "false", "#8040C0", "#8040C0FF", "120", "100", "250",
           "10", "Punkter", "Bredd i mm", "73", "Ta bort"});
    lager({"Ledningar", "true", "#E0A020", "#E0A02080", "1.5", "75", "1000",
           "8.5", "Kurva", "Radens färg", "45", "Ta bort"},
          {}, kRod);
    kategori("Text", false);
    lager({"Rubriker", "true", "#101010", "#101010FF", "1", "100", "500", "",
           "Linje", "", "9", "Ta bort"});
    lager({"Noter", "true", "#505050", "#505050FF", "fel", "100", "500", "",
           "Linje", "Bredden går inte att tolka", "31", "Ta bort"});

    grid_->setRows(std::move(r));
    tillampaCeller();
}

void TablesWindow::tillampaCeller()
{
    for (int i = 0; i < grid_->rowCount(); ++i) {
        if (grid_->rowKind(i) != K::Data)
            continue;
        grid_->setCellImage(i, kTaBort, ikon(glaze::StockIcon::Delete));
        const std::string& namn = grid_->cell(i, kNamn);
        if (namn == "Plattor") {
            grid_->setCellToolTip(i, kFarg, "Plattornas färg — cellens egen tooltip");
        } else if (namn == "Ytor") {
            glaze::DataGrid::EditorSpec last = linjeSpec();
            last.readOnly                    = true;
            grid_->setCellEditor(i, kLinje, std::move(last));
            grid_->setCellToolTip(i, kLinje,
                                  "Låst cell: kolumnens vallista med readOnly");
        } else if (namn == "Vatten") {
            grid_->setCellImage(i, kKommentar, ikon(glaze::StockIcon::Image));
            grid_->setCellColor(i, kAntal, kBlaYta, kBlaText);
        } else if (namn == "Vägar") {
            grid_->setCellEnabled(i, kTaBort, false);
            grid_->setCellToolTip(i, kTaBort, "Inaktiv cell: vägarna går inte att ta bort");
        } else if (namn == "Stolpar") {
            grid_->setCellEditor(i, kBredd, {.editor   = E::Number,
                                             .min      = 0.0,
                                             .max      = kMmMax,
                                             .decimals = 0,
                                             .suffix   = " mm"});
            grid_->setCellToolTip(i, kBredd, "Egna gränser 0–500 och enheten mm");
        } else if (namn == "Etiketter") {
            grid_->setCellEditor(i, kLinje, {.editor  = E::Choice,
                                             .choices = {"Linje", "Streckad", "Prickad"}});
            grid_->setCellToolTip(i, kLinje, "Cellens egen vallista, utan ikoner");
        } else if (namn == "Rubriker") {
            grid_->setCellImage(i, kKommentar, ikon(glaze::StockIcon::Image));
            grid_->setCellToolTip(i, kKommentar, "Bara en bild, ingen text");
        } else if (namn == "Noter") {
            grid_->setCellToolTip(i, kKommentar, "Appens text i Bredd går inte att tolka");
        }
    }
}

void TablesWindow::fyllMassor()
{
    std::vector<glaze::Table::Row> r;
    r.reserve(std::size_t(kMassRader + kMassRader / kAvsnitt));
    const std::vector<std::string>& linjer = linjeSpec().choices;
    for (int i = 0; i < kMassRader; ++i) {
        if (i % kAvsnitt == 0)
            r.push_back({.cells = {"Avsnitt " + std::to_string(i / kAvsnitt + 1)},
                         .kind  = K::Category});
        const gfx::Color f{std::uint8_t(i * kFargStegR % kFargKanal),
                           std::uint8_t(i * kFargStegG % kFargKanal),
                           std::uint8_t(i * kFargStegB % kFargKanal)};
        const std::string farg  = hex(f).substr(0, kHexRgbLangd);
        const double      bredd = double(i % (int(kBreddMax) * kTiondelar + 1)) /
                               kTiondelar;
        r.push_back({{"Lager " + std::to_string(i + 1), sant(i % 2 == 0), farg,
                      hex(f), tal(bredd), std::to_string(i % (int(kProcentMax) + 1)),
                      std::to_string(kSkalaSteg * (1 + i % kSkalaAntal)), "",
                      linjer[std::size_t(i) % linjer.size()], "",
                      std::to_string(i), "Ta bort"}});
    }
    grid_->setRows(std::move(r));
    tillampaCeller();
    // Med höjden ur raderna vore tabellen 20 000 rader hög: den får en fast
    // höjd och rullar själv (reglaget slår av det igen).
    dgFastHojd_->setChecked(true);
    grid_->height = kFastHojd;
    grid_->invalidate();
    logga("Fönstret", "Fyll", std::to_string(grid_->rowCount()) + " rader");
}

void TablesWindow::taBortRad(int row)
{
    if (row < 0 || row >= grid_->rowCount())
        return;
    // `removeRow` direkt i knappens handler: raderna efter flyttar upp ett
    // index, och cellernas inställningar, valet och den aktuella cellen följer
    // sina rader — inget behöver läggas om.
    const std::string namn = grid_->cell(row, kNamn);
    grid_->removeRow(row);
    logga("Fönstret", "Ta bort", "row=" + std::to_string(row) + " " + citat(namn));
}

void TablesWindow::fyllListView()
{
    struct Fil {
        K                kind;
        const char*      namn;
        const char*      typ;
        const char*      storlek;
        const char*      datum;
        double           byte;
        glaze::StockIcon ikon;
        bool             kryss;
        bool             utfalld;
    };
    const glaze::StockIcon dok  = glaze::StockIcon::Document;
    const glaze::StockIcon bild = glaze::StockIcon::FileImage;
    const glaze::StockIcon ljud = glaze::StockIcon::FileAudio;
    const glaze::StockIcon kalk = glaze::StockIcon::FileSpreadsheet;
    const Fil filer[] = {
        {K::Data, "Läsmig.txt", "Text", "2 kB", "2026-11-01", 2048, dok, false, true},
        {K::Category, "Dokument", "", "", "", 0, dok, false, true},
        {K::Data, "Rapport.pdf", "PDF", "12 kB", "2026-10-07", 12288, dok, true, true},
        {K::Data, "Brev.txt", "Text", "900 B", "2026-08-19", 900, dok, false, true},
        {K::Data, "Budget.xlsx", "Kalkyl", "48 kB", "2026-08-21", 49152, kalk, true, true},
        {K::Blank, "", "", "", "", 0, dok, false, true},
        {K::Category, "Bilder", "", "", "", 0, bild, false, true},
        {K::Data, "Semester.png", "Bild", "3 MB", "2026-09-30", 3145728, bild, false, true},
        {K::Data, "Karta.png", "Bild", "1,2 MB", "2026-09-12", 1258291, bild, false, true},
        {K::Category, "Musik", "", "", "", 0, ljud, false, false},
        {K::Data, "01.flac", "Ljud", "24 MB", "2026-01-02", 25165824, ljud, false, true},
        {K::Data, "02.flac", "Ljud", "31 MB", "2026-01-02", 32505856, ljud, true, true},
    };
    std::vector<glaze::Table::Row> r;
    for (const Fil& f : filer) {
        if (f.kind == K::Data)
            r.push_back({{f.namn, f.typ, f.storlek, f.datum}, ikon(f.ikon)});
        else
            r.push_back({.cells = {f.namn}, .kind = f.kind, .expanded = f.utfalld});
    }
    lista_->setRows(std::move(r));
    for (int i = 0; i < lista_->rowCount(); ++i) {
        const Fil& f = filer[std::size_t(i)];
        if (f.kind != K::Data)
            continue;
        lista_->setSortKey(i, kLvKolStorlek, f.byte);
        lista_->setChecked(i, f.kryss);
        if (f.ikon == bild)
            lista_->setCellImage(i, kLvKolStorlek, ikon(glaze::StockIcon::Image));
    }
}

void TablesWindow::fyllPropertyGrid()
{
    using P = glaze::PropertyGrid::Kind;
    std::vector<glaze::PropertyGrid::Row> r;
    r.push_back({.name = "Utseende", .kind = P::Category,
                 .toolTip = "Kategorirad: ett klick fäller"});
    r.push_back({.name = "Namn", .value = "Vägar", .toolTip = "Fritext"});
    r.push_back({.name        = "Linjetyp",
                 .value       = "Polylinje",
                 .choices     = linjeSpec().choices,
                 .toolTip     = "Vallista med ikoner",
                 .choiceIcons = linjeSpec().choiceIcons});
    r.push_back({.name = "Synlig", .value = "true", .kind = P::Check,
                 .toolTip = "Kryss: ett klick eller Space växlar"});
    r.push_back({.name = "Färg", .value = "#D04030", .kind = P::Color,
                 .toolTip = "Färg utan alfa"});
    r.push_back({.name       = "Färg med alfa",
                 .value      = "#3C78D880",
                 .kind       = P::Color,
                 .allowAlpha   = true,
                 .toolTip      = "Färg med alfa, rutan och hextexten",
                 .colorDisplay = glaze::ColorDisplay::Both});
    r.push_back({.name = "Mått", .kind = P::Category});
    r.push_back({.name     = "Bredd",
                 .value    = "2.5",
                 .kind     = P::Number,
                 .min      = 0.0,
                 .max      = kBreddMax,
                 .decimals = kBreddDecimaler,
                 .suffix   = " px",
                 .toolTip  = "0–20 med en decimal"});
    r.push_back({.name     = "Opacitet",
                 .value    = "100",
                 .kind     = P::Number,
                 .min      = 0.0,
                 .max      = kProcentMax,
                 .decimals = 0,
                 .suffix   = " %",
                 .toolTip  = "0–100 i heltal"});
    r.push_back({.name     = "Skala",
                 .value    = "500",
                 .kind     = P::Number,
                 .min      = kSkalaMin,
                 .decimals = 0,
                 .prefix   = "1:",
                 .toolTip  = "Text före talet"});
    r.push_back({.name       = "Höjd",
                 .value      = "",
                 .kind       = P::Number,
                 .min        = 0.0,
                 .decimals   = kHojdDecimaler,
                 .suffix     = " m",
                 .toolTip    = "Tomt är ett värde",
                 .allowEmpty = true,
                 .emptyText  = "Automatisk"});
    r.push_back({.name = "Id", .value = "L-17", .readOnly = true,
                 .toolTip = "Skrivskyddad rad"});
    r.push_back({.name    = "Spärrad",
                 .value   = "Går inte att ändra nu",
                 .toolTip = "Inaktiv rad",
                 .enabled = false});
    r.push_back({.name = "Övrigt", .kind = P::Category, .expanded = false});
    r.push_back({.name = "Kommentar", .value = "Ihopfälld från början"});
    egen_->setRows(std::move(r));
}

// ---------------------------------------------------------------- eventen

void TablesWindow::kopplaEvents()
{
    auto cellArgs = [this](const glaze::CellChangeArgs& e) {
        std::string s = "row=" + std::to_string(e.row) +
                        " column=" + std::to_string(e.column) +
                        " rows=" + lista(e.rows);
        if (e.row >= 0 && e.column >= 0)
            s += " text=" + citat(grid_->cell(e.row, e.column));
        else if (!e.rows.empty() && e.column >= 0)
            s += " text[" + std::to_string(e.rows.front()) + "]=" +
                 citat(grid_->cell(e.rows.front(), e.column));
        return s;
    };
    grid_->onChange = [this, cellArgs](glaze::Widget*, glaze::CellChangeArgs& e) {
        logga("DataGrid", "onChange", cellArgs(e));
    };
    grid_->onCommit = [this, cellArgs](glaze::Widget*, glaze::CellChangeArgs& e) {
        logga("DataGrid", "onCommit", cellArgs(e));
    };
    grid_->onCellClick = [this](glaze::Widget*, glaze::CellClickArgs& e) {
        logga("DataGrid", "onCellClick",
              "row=" + std::to_string(e.row) + " column=" + std::to_string(e.column));
        if (e.column == kTaBort)
            taBortRad(e.row);
    };
    grid_->onSelectionChange = [this] {
        logga("DataGrid", "onSelectionChange",
              "selectedRows=" + lista(grid_->selectedRows()));
    };
    grid_->onExpand = [this](glaze::Widget*, glaze::RowArgs& e) {
        logga("DataGrid", "onExpand",
              "row=" + std::to_string(e.row) + " " + citat(grid_->cell(e.row, kNamn)));
    };
    grid_->onCollapse = [this](glaze::Widget*, glaze::RowArgs& e) {
        logga("DataGrid", "onCollapse",
              "row=" + std::to_string(e.row) + " " + citat(grid_->cell(e.row, kNamn)));
    };

    lista_->onChange = [this] {
        logga("ListView", "onChange",
              "selectedIndex=" + std::to_string(lista_->selectedIndex()) +
                  " selectedRows=" + lista(lista_->selectedRows()));
    };
    lista_->onCheck = [this] {
        logga("ListView", "onCheck",
              "changedRow=" + std::to_string(lista_->changedRow()) +
                  " changedRows=" + lista(lista_->changedRows()) +
                  " checkedRows=" + lista(lista_->checkedRows()));
    };
    lista_->onCommit = [this] {
        logga("ListView", "onCommit",
              "changedRow=" + std::to_string(lista_->changedRow()) +
                  " changedRows=" + lista(lista_->changedRows()) +
                  " selectedRows=" + lista(lista_->selectedRows()));
    };
    lista_->onActivate = [this] {
        const int r = lista_->currentRow();
        logga("ListView", "onActivate",
              "currentRow=" + std::to_string(r) + " " + citat(lista_->cell(r, 0)));
    };
    lista_->onExpand = [this](glaze::Widget*, glaze::RowArgs& e) {
        logga("ListView", "onExpand",
              "row=" + std::to_string(e.row) + " " + citat(lista_->cell(e.row, 0)));
    };
    lista_->onCollapse = [this](glaze::Widget*, glaze::RowArgs& e) {
        logga("ListView", "onCollapse",
              "row=" + std::to_string(e.row) + " " + citat(lista_->cell(e.row, 0)));
    };

    auto egenArgs = [this] {
        const int r = egen_->changedRow();
        std::string s = "changedRow=" + std::to_string(r);
        if (r < 0 || std::size_t(r) >= egen_->rows().size())
            return s;
        const glaze::PropertyGrid::Row& rad = egen_->rows()[std::size_t(r)];
        s += " " + citat(rad.name) + " value=" + citat(rad.value);
        switch (rad.kind) {
        case glaze::PropertyGrid::Kind::Check:
            s += " isChecked=" + sant(egen_->isChecked(r));
            break;
        case glaze::PropertyGrid::Kind::Color: {
            const auto f = egen_->colorValue(r);
            s += " colorValue=" + (f ? hex(*f) : std::string("nullopt"));
            break;
        }
        case glaze::PropertyGrid::Kind::Number: {
            const auto v = egen_->numberValue(r);
            s += " numberValue=" + (v ? tal(*v) : std::string("nullopt"));
            break;
        }
        default:
            break;
        }
        return s + " hasError=" + sant(egen_->hasError(r));
    };
    egen_->onChange = [this, egenArgs] { logga("PropertyGrid", "onChange", egenArgs()); };
    egen_->onCommit = [this, egenArgs] { logga("PropertyGrid", "onCommit", egenArgs()); };
    egen_->onExpand = [this](glaze::Widget*, glaze::RowArgs& e) {
        logga("PropertyGrid", "onExpand",
              "row=" + std::to_string(e.row) + " " +
                  citat(egen_->rows()[std::size_t(e.row)].name));
    };
    egen_->onCollapse = [this](glaze::Widget*, glaze::RowArgs& e) {
        logga("PropertyGrid", "onCollapse",
              "row=" + std::to_string(e.row) + " " +
                  citat(egen_->rows()[std::size_t(e.row)].name));
    };
}

// --------------------------------------------------------------- reglagen

void TablesWindow::byggReglage(glaze::Panel& yta)
{
    reglageUtseende(yta);
    reglageDataGrid(yta);
    reglageListView(yta);
    reglagePropertyGrid(yta);
    reglageDropdown(yta);
}

void TablesWindow::reglageUtseende(glaze::Panel& yta)
{
    Stapel s(yta, "utseende", kKant);
    glaze::GroupBox* g = grupp(s, "Utseende");
    Stapel           i(*g, "utseendeRad", 0.0f);
    Rad*             r = i.rad();
    val(*r, "UI", {"Glaze", "Slate", "Minimalistic"}, kUIStart,
        [this](int v) { setUI(kUIs[v]); });
    r = i.rad();
    val(*r, "Läge", {"System", "Ljust", "Mörkt"}, 0,
        [this](int v) { setThemeMode(kLagen[v]); });
}

void TablesWindow::reglageDataGrid(glaze::Panel& yta)
{
    Stapel s(yta, "dgGrupp", kKant);
    // Gruppramarna står under varandra: den förra ramen är `utseende0`.
    glaze::GroupBox* g = s.lagg<glaze::GroupBox>(kLuft, "DataGrid");
    g->anchorTop       = {"utseende0", kLuft};
    Stapel i(*g, "dgRad", 0.0f);
    using SM = glaze::DataGrid::SelectionMode;

    Rad* r = i.rad();
    kryss(*r, "Aktiv", true, [this](bool on) { on ? grid_->enable() : grid_->disable(); });
    dgFastHojd_ = kryss(*r, "Fast höjd", false, [this](bool on) {
        grid_->height = on ? kFastHojd : 0.0f;
        grid_->invalidate();
    });

    r = i.rad();
    val(*r, "Val", {"Cell", "FullRow"}, 0, [this](int v) {
        grid_->setSelectionMode(v == 0 ? SM::Cell : SM::FullRow);
    });
    kryss(*r, "Flerval", false, [this](bool on) { grid_->setMultiSelect(on); });

    r = i.rad();
    kryss(*r, "Flera rader (multiEdit)", false,
          [this](bool on) { grid_->setMultiEdit(on); });
    r = i.rad();
    kryss(*r, "Redigera med ett klick (editOnClick)", false,
          [this](bool on) { grid_->setEditOnClick(on); });
    r = i.rad();
    kryss(*r, "Markera texten vid redigering", true,
          [this](bool on) { grid_->setSelectOnEdit(on); });
    r = i.rad();
    val(*r, "Färgens visning", {"Swatch", "Hex", "Both"}, 0, [this](int v) {
        glaze::DataGrid::EditorSpec spec = grid_->columnEditor(kFarg);
        spec.colorDisplay                = kFargVisningar[v];
        grid_->setColumnEditor(kFarg, std::move(spec));
    });

    r = i.rad();
    kryss(*r, "Radrubriker", false, [this](bool on) { grid_->setRowHeaders(on); });
    kryss(*r, "Sorterbar", true, [this](bool on) { grid_->setSortable(on); });

    r = i.rad();
    val(*r, "Sortera", medIngen(kDgNamn), 0,
        [this](int v) { grid_->setSortColumn(v - 1); });
    kryss(*r, "Stigande", true, [this](bool on) { grid_->setSortAscending(on); });

    r = i.rad();
    val(*r, "Huvudets rader", {"1", "2", "Auto"}, kHuvudRaderStart,
        [this](int v) { grid_->setHeaderLines(kHuvudRader[v]); });
    kryss(*r, "Rubrikkryss", true,
          [this](bool on) { grid_->setColumnHeaderCheck(kSynlig, on); });
    r = i.rad();
    val(*r, "Huvudets läge", {"Top", "Center", "Bottom"}, int(grid_->headerVAlign()),
        [this](int v) { grid_->setHeaderVAlign(kHuvudLagen[v]); });
    r = i.rad();
    val(*r, "Ikonstorlek", {"Auto", "16", "24", "32"}, 0,
        [this](int v) { grid_->setIconSize(kIkonStorlekar[v]); });

    r = i.rad();
    dgKolumn_ = val(*r, "Kolumn", kDgNamn, 0, [this](int v) {
        dgLage_->setSelectedIndex(int(grid_->columnSizing(v)));
        dgBredd_->setValue(grid_->columnWidth(v));
    });
    r = i.rad();
    dgLage_ = val(*r, "Läge", kLagesNamn, int(grid_->columnSizing(0)), [this](int v) {
        grid_->setColumnSizing(dgKolumn_->selectedIndex(), glaze::ColumnSizing(v));
    });
    r = i.rad();
    dgBredd_ = talfalt(*r, "Bredd (0 = standard)", grid_->columnWidth(0), [this](double v) {
        grid_->setColumnWidth(dgKolumn_->selectedIndex(), float(v));
    });

    r = i.rad();
    knapp(*r, "Lås aktuell cell", [this] {
        int row = 0, col = 0;
        if (!aktuellCell(row, col))
            return;
        // Den redigerare som gäller, låst — cellen ritas som förut.
        glaze::DataGrid::EditorSpec last = grid_->cellEditor(row, col);
        last.readOnly                    = true;
        grid_->setCellEditor(row, col, std::move(last));
        logga("Fönstret", "setCellEditor readOnly",
              "row=" + std::to_string(row) + " column=" + std::to_string(col));
    });
    knapp(*r, "Lås upp", [this] {
        int row = 0, col = 0;
        if (!aktuellCell(row, col))
            return;
        grid_->clearCellEditor(row, col);
        logga("Fönstret", "clearCellEditor",
              "row=" + std::to_string(row) + " column=" + std::to_string(col));
    });
    r = i.rad();
    knapp(*r, "Inaktivera aktuell cell", [this] {
        int row = 0, col = 0;
        if (!aktuellCell(row, col))
            return;
        grid_->setCellEnabled(row, col, false);
        logga("Fönstret", "setCellEnabled false",
              "row=" + std::to_string(row) + " column=" + std::to_string(col));
    });
    knapp(*r, "Aktivera", [this] {
        int row = 0, col = 0;
        if (!aktuellCell(row, col))
            return;
        grid_->setCellEnabled(row, col, true);
        logga("Fönstret", "setCellEnabled true",
              "row=" + std::to_string(row) + " column=" + std::to_string(col));
    });

    const Logg logg = [this](const std::string& handelse, const std::string& args) {
        logga("Fönstret", handelse, args);
    };

    // Den aktuella cellen och synfältet ur rad- och kolumnfälten.
    r         = i.rad();
    dgRadTal_ = indexfalt(*r, "Rad");
    dgKolTal_ = indexfalt(*r, "Kolumn");
    r         = i.rad();
    knapp(*r, "setCurrentCell", [this] {
        grid_->setCurrentCell(int(dgRadTal_->value()), int(dgKolTal_->value()));
        logga("Fönstret", "setCurrentCell",
              "currentRow=" + std::to_string(grid_->currentRow()) +
                  " currentColumn=" + std::to_string(grid_->currentColumn()));
    });
    knapp(*r, "ensureVisible", [this] {
        const int row = int(dgRadTal_->value());
        const int col = int(dgKolTal_->value());
        grid_->ensureVisible(row);
        grid_->ensureColumnVisible(col);
        logga("Fönstret", "ensureVisible",
              "row=" + std::to_string(row) + " column=" + std::to_string(col));
    });

    // Valet från koden (det syns i FullRow), radens slag och färgerna.
    valKnappar(i, grid_, logg);
    slagKnappar(i, grid_, logg);
    fargKnappar(i, grid_, [this] { return grid_->currentColumn(); }, logg);

    r = i.rad();
    knapp(*r, "Lägg till rad", [this] {
        const std::string n = std::to_string(++nyRad_);
        grid_->addRow({"Nytt lager " + n, "true", "#4080C0", "#4080C0FF", "1",
                       "100", "500", "", "Linje", "", n, "Ta bort"});
        grid_->setCellImage(grid_->rowCount() - 1, kTaBort,
                            ikon(glaze::StockIcon::Delete));
        logga("Fönstret", "addRow", "row=" + std::to_string(grid_->rowCount() - 1));
    });
    knapp(*r, "Kategori", [this] {
        grid_->addRow({"Kategori " + std::to_string(++nyRad_)});
        grid_->setRowKind(grid_->rowCount() - 1, K::Category);
        logga("Fönstret", "addRow + setRowKind Category",
              "row=" + std::to_string(grid_->rowCount() - 1));
    });
    knapp(*r, "Tomrad", [this] {
        grid_->addRow({});
        grid_->setRowKind(grid_->rowCount() - 1, K::Blank);
        logga("Fönstret", "addRow + setRowKind Blank",
              "row=" + std::to_string(grid_->rowCount() - 1));
    });

    r = i.rad();
    knapp(*r, "Fäll ihop alla", [this] {
        for (int k = 0; k < grid_->rowCount(); ++k)
            if (grid_->rowKind(k) == K::Category)
                grid_->setExpanded(k, false);
    });
    knapp(*r, "Fäll ut alla", [this] {
        for (int k = 0; k < grid_->rowCount(); ++k)
            if (grid_->rowKind(k) == K::Category)
                grid_->setExpanded(k, true);
    });

    r = i.rad();
    knapp(*r, "Fyll 20 000 rader", [this] { fyllMassor(); });
    knapp(*r, "Återställ", [this] {
        fyllDataGrid();
        logga("Fönstret", "Återställ", std::to_string(grid_->rowCount()) + " rader");
    });

    r = i.rad();
    kryss(*r, "Uppdatera en cell 60 gånger/s", false, [this](bool on) {
        on ? takt_.start() : takt_.stop();
    });

    // Verktygsknapparna tar inte fokus: en öppen cell står kvar tills
    // knappen når den.
    r = i.rad();
    verktyg(*r, "Spara (commitEdits)",
            "Fönstrets commitEdits() sparar cellen som redigeras", [this] {
                const bool fore = grid_->isEditing();
                commitEdits();
                logga("Fönstret", "commitEdits",
                      "redigerade=" + sant(fore) + " isEditing=" + sant(grid_->isEditing()));
            });
    verktyg(*r, "Läs cellen", "Läsvägarna för den aktuella cellen",
            [this] { lasCellen(); });
    r = i.rad();
    verktyg(*r, "beginEdit", "Börjar redigera den aktuella cellen", [this] {
        grid_->beginEdit();
        logga("Fönstret", "beginEdit", "isEditing=" + sant(grid_->isEditing()));
    });
    verktyg(*r, "cancelEdit", "Kastar den pågående redigeringen", [this] {
        grid_->cancelEdit();
        logga("Fönstret", "cancelEdit", "isEditing=" + sant(grid_->isEditing()));
    });
}

void TablesWindow::reglageListView(glaze::Panel& yta)
{
    Stapel s(yta, "lvGrupp", kKant);
    glaze::GroupBox* g = s.lagg<glaze::GroupBox>(kLuft, "ListView");
    g->anchorTop       = {"dgGrupp0", kLuft};
    Stapel i(*g, "lvRad", 0.0f);

    Rad* r = i.rad();
    kryss(*r, "Aktiv", true, [this](bool on) { on ? lista_->enable() : lista_->disable(); });
    kryss(*r, "Fast höjd", false, [this](bool on) {
        lista_->height = on ? kFastHojd : 0.0f;
        lista_->invalidate();
    });

    r = i.rad();
    kryss(*r, "Flerval", false, [this](bool on) { lista_->setMultiSelect(on); });
    kryss(*r, "Kryss (showChecks)", true, [this](bool on) { lista_->setShowChecks(on); });

    r = i.rad();
    kryss(*r, "Rubrikkryss", true,
          [this](bool on) { lista_->setColumnHeaderCheck(0, on); });
    kryss(*r, "Radrubriker", false, [this](bool on) { lista_->setRowHeaders(on); });

    r = i.rad();
    kryss(*r, "Sorterbar", true, [this](bool on) { lista_->setSortable(on); });
    val(*r, "Sortera", medIngen(kLvNamn), 0,
        [this](int v) { lista_->setSortColumn(v - 1); });

    r = i.rad();
    val(*r, "Ikonstorlek", {"Auto", "16", "24", "32"}, 0,
        [this](int v) { lista_->setIconSize(kIkonStorlekar[v]); });

    r = i.rad();
    lvKolumn_ = val(*r, "Kolumn", kLvNamn, 0, [this](int v) {
        lvLage_->setSelectedIndex(int(lista_->columnSizing(v)));
        lvBredd_->setValue(lista_->columnWidth(v));
    });
    r = i.rad();
    lvLage_ = val(*r, "Läge", kLagesNamn, int(lista_->columnSizing(0)), [this](int v) {
        lista_->setColumnSizing(lvKolumn_->selectedIndex(), glaze::ColumnSizing(v));
    });
    r = i.rad();
    lvBredd_ = talfalt(*r, "Bredd (0 = standard)", lista_->columnWidth(0), [this](double v) {
        lista_->setColumnWidth(lvKolumn_->selectedIndex(), float(v));
    });

    const Logg logg = [this](const std::string& handelse, const std::string& args) {
        logga("Fönstret", handelse, args);
    };

    // Valet och synfältet ur radfältet.
    r         = i.rad();
    lvRadTal_ = indexfalt(*r, "Rad");
    knapp(*r, "setSelectedIndex", [this] {
        lista_->setSelectedIndex(int(lvRadTal_->value()));
        logga("Fönstret", "setSelectedIndex",
              "selectedIndex=" + std::to_string(lista_->selectedIndex()) +
                  " currentRow=" + std::to_string(lista_->currentRow()));
    });
    knapp(*r, "ensureVisible", [this] {
        const int row = int(lvRadTal_->value());
        lista_->ensureVisible(row);
        logga("Fönstret", "ensureVisible", "row=" + std::to_string(row));
    });

    // Valet från koden, radens slag och färgerna (cellen i kolumnen ovan).
    valKnappar(i, lista_, logg);
    slagKnappar(i, lista_, logg);
    fargKnappar(i, lista_, [this] { return lvKolumn_->selectedIndex(); }, logg);

    r = i.rad();
    knapp(*r, "Ta bort aktuell rad", [this, logg] {
        const int row = aktuellRad(*lista_, logg);
        if (row < 0)
            return;
        const std::string namn = lista_->cell(row, kLvKolNamn);
        lista_->removeRow(row);
        logga("Fönstret", "removeRow",
              "row=" + std::to_string(row) + " " + citat(namn) +
                  " currentRow=" + std::to_string(lista_->currentRow()));
    });

    r = i.rad();
    knapp(*r, "Lägg till rad", [this] {
        const std::string n = std::to_string(++nyRad_);
        lista_->addRow({"Ny fil " + n + ".txt", "Text", "1 kB", "2026-11-06"},
                       ikon(glaze::StockIcon::Document));
        lista_->setSortKey(lista_->rowCount() - 1, kLvKolStorlek, kNyFilByte);
        logga("Fönstret", "addRow", "row=" + std::to_string(lista_->rowCount() - 1));
    });
    knapp(*r, "Kategori", [this] {
        lista_->addRow({"Kategori " + std::to_string(++nyRad_)});
        lista_->setRowKind(lista_->rowCount() - 1, K::Category);
        logga("Fönstret", "addRow + setRowKind Category",
              "row=" + std::to_string(lista_->rowCount() - 1));
    });
    knapp(*r, "Tomrad", [this] {
        lista_->addRow({});
        lista_->setRowKind(lista_->rowCount() - 1, K::Blank);
        logga("Fönstret", "addRow + setRowKind Blank",
              "row=" + std::to_string(lista_->rowCount() - 1));
    });

    r = i.rad();
    knapp(*r, "Fäll ihop alla", [this] {
        for (int k = 0; k < lista_->rowCount(); ++k)
            if (lista_->rowKind(k) == K::Category)
                lista_->setExpanded(k, false);
    });
    knapp(*r, "Fäll ut alla", [this] {
        for (int k = 0; k < lista_->rowCount(); ++k)
            if (lista_->rowKind(k) == K::Category)
                lista_->setExpanded(k, true);
    });
}

void TablesWindow::reglagePropertyGrid(glaze::Panel& yta)
{
    Stapel s(yta, "pgGrupp", kKant);
    glaze::GroupBox* g = s.lagg<glaze::GroupBox>(kLuft, "PropertyGrid");
    g->anchorTop       = {"lvGrupp0", kLuft};
    Stapel i(*g, "pgRad", 0.0f);

    Rad* r = i.rad();
    kryss(*r, "Aktiv", true, [this](bool on) { on ? egen_->enable() : egen_->disable(); });
    kryss(*r, "Fast höjd", false, [this](bool on) {
        egen_->height = on ? kFastHojd : 0.0f;
        egen_->invalidate();
    });

    r = i.rad();
    val(*r, "Namnkolumn", kLagesNamn, int(egen_->columnSizing(0)),
        [this](int v) { egen_->setColumnSizing(0, glaze::ColumnSizing(v)); });
    r = i.rad();
    talfalt(*r, "Namnets bredd (0 = standard)", egen_->columnWidth(0),
            [this](double v) { egen_->setColumnWidth(0, float(v)); });
    r = i.rad();
    val(*r, "Värdekolumn", kLagesNamn, int(egen_->columnSizing(1)),
        [this](int v) { egen_->setColumnSizing(1, glaze::ColumnSizing(v)); });
    r = i.rad();
    talfalt(*r, "Värdets bredd (0 = standard)", egen_->columnWidth(1),
            [this](double v) { egen_->setColumnWidth(1, float(v)); });

    r = i.rad();
    kryss(*r, "Inaktivera rad 2", false,
          [this](bool on) { egen_->setRowEnabled(kPgLinjetypRad, !on); });
    r = i.rad();
    kryss(*r, "Markera texten vid redigering", true,
          [this](bool on) { egen_->setSelectOnEdit(on); });

    // En rad byts med `setRow` och ett ändrat aggregat: raden med öppen
    // editor (verktygsknapparna tar inte fokus), annars radfältets.
    r         = i.rad();
    pgRadTal_ = indexfalt(*r, "Rad");
    r         = i.rad();
    verktyg(*r, "readOnly på", "setRow med readOnly = true",
            [this] { pgSkrivskydd(true); });
    verktyg(*r, "readOnly av", "setRow med readOnly = false",
            [this] { pgSkrivskydd(false); });
    verktyg(*r, "setRow (nytt värde)", "setRow med ett nytt värde i raden",
            [this] { pgNyttVarde(); });
    r = i.rad();
    knapp(*r, "addRow", [this] {
        const std::string n = std::to_string(++nyRad_);
        egen_->addRow({.name    = "Ny egenskap " + n,
                       .value   = "addRow " + n,
                       .toolTip = "Tillagd med addRow"});
        logga("Fönstret", "addRow",
              "row=" + std::to_string(int(egen_->rows().size()) - 1));
    });

    r = i.rad();
    knapp(*r, "Fäll ihop alla", [this] {
        for (std::size_t k = 0; k < egen_->rows().size(); ++k)
            if (egen_->rows()[k].kind == glaze::PropertyGrid::Kind::Category)
                egen_->setExpanded(int(k), false);
    });
    knapp(*r, "Fäll ut alla", [this] {
        for (std::size_t k = 0; k < egen_->rows().size(); ++k)
            if (egen_->rows()[k].kind == glaze::PropertyGrid::Kind::Category)
                egen_->setExpanded(int(k), true);
    });
}

void TablesWindow::reglageDropdown(glaze::Panel& yta)
{
    Stapel s(yta, "ddGrupp", kKant);
    glaze::GroupBox* g = s.lagg<glaze::GroupBox>(kLuft, "Dropdown");
    g->anchorTop       = {"pgGrupp0", kLuft};
    Stapel i(*g, "ddRad", 0.0f);

    // En fristående vallista med en ikon per post (`addItem(text, ikon)`).
    Rad* r = i.rad();
    r->add<glaze::Label>("Med ikoner");
    auto* dd  = r->add<glaze::Dropdown>();
    dd->width = kValBredd;
    const glaze::DataGrid::EditorSpec spec = linjeSpec();
    for (std::size_t k = 0; k < spec.choices.size(); ++k)
        dd->addItem(spec.choices[k], spec.choiceIcons[k]);
    dd->setSelectedIndex(0);
    dd->onChange = [this, dd] {
        const int v = dd->selectedIndex();
        logga("Dropdown", "onChange",
              "selectedIndex=" + std::to_string(v) + " text=" +
                  citat(dd->selectedText()) + " ikon=" + sant(bool(dd->itemIcon(v))));
    };

    // Massformen bär bara texterna och nollar ikonerna; `setItem` sätter
    // tillbaka dem en post i taget.
    r = i.rad();
    knapp(*r, "setItems (utan ikoner)", [this, dd] {
        dd->setItems(linjeSpec().choices);
        logga("Fönstret", "setItems",
              std::to_string(dd->items().size()) + " poster, ikonerna nollade");
    });
    knapp(*r, "Ikoner tillbaka", [this, dd] {
        const glaze::DataGrid::EditorSpec l = linjeSpec();
        for (std::size_t k = 0; k < l.choices.size(); ++k)
            dd->setItem(int(k), l.choices[k], l.choiceIcons[k]);
        logga("Fönstret", "setItem",
              std::to_string(l.choices.size()) + " poster med ikoner");
    });

    // Luft under den sista ramen när kolumnen rullar.
    auto* fot      = yta.add<glaze::Spacer>();
    fot->name      = "reglageFot";
    fot->x         = 0;
    fot->width     = kFotBredd;
    fot->height    = kKant;
    fot->anchorTop = {"ddGrupp0", 0.0f};
}

// ------------------------------------------------------- läsvägarna

bool TablesWindow::aktuellCell(int& row, int& column)
{
    row    = grid_->currentRow();
    column = grid_->currentColumn();
    if (row >= 0 && column >= 0)
        return true;
    logga("Fönstret", "Ingen aktuell cell", "klicka i en cell först");
    return false;
}

void TablesWindow::lasCellen()
{
    int row = 0, col = 0;
    if (!aktuellCell(row, col))
        return;
    const glaze::DataGrid::EditorSpec& spec = grid_->cellEditor(row, col);
    const auto                         f    = grid_->colorValue(row, col);
    const auto                         v    = grid_->numberValue(row, col);
    logga("DataGrid", "läsvägarna",
          "row=" + std::to_string(row) + " column=" + std::to_string(col) +
              " editor=" + kEditorNamn[int(spec.editor)] +
              " text=" + citat(grid_->cell(row, col)) +
              " enabled=" + sant(grid_->isCellEnabled(row, col)) +
              " isChecked=" + sant(grid_->isChecked(row, col)) +
              " colorValue=" + (f ? hex(*f) : std::string("nullopt")) +
              " numberValue=" + (v ? tal(*v) : std::string("nullopt")) +
              " hasError=" + sant(grid_->hasError(row, col)));
}

int TablesWindow::pgMalrad()
{
    const int r = egen_->activeRow() >= 0 ? egen_->activeRow()
                                          : int(pgRadTal_->value());
    if (r >= 0 && std::size_t(r) < egen_->rows().size())
        return r;
    logga("Fönstret", "Ingen rad", "row=" + std::to_string(r) + " finns inte");
    return -1;
}

void TablesWindow::pgSkrivskydd(bool on)
{
    const int r = pgMalrad();
    if (r < 0)
        return;
    glaze::PropertyGrid::Row rad = egen_->rows()[std::size_t(r)];
    rad.readOnly                 = on;
    egen_->setRow(r, std::move(rad));
    logga("Fönstret", "setRow",
          "row=" + std::to_string(r) + " readOnly=" + sant(on) +
              " activeRow=" + std::to_string(egen_->activeRow()));
}

void TablesWindow::pgNyttVarde()
{
    using P     = glaze::PropertyGrid::Kind;
    const int r = pgMalrad();
    if (r < 0)
        return;
    glaze::PropertyGrid::Row rad = egen_->rows()[std::size_t(r)];
    switch (rad.kind) {
    case P::Category:
        logga("Fönstret", "setRow", "row=" + std::to_string(r) +
                                        " är en kategorirad utan värde");
        return;
    case P::Check:
        rad.value = sant(!egen_->isChecked(r));
        break;
    case P::Color:
        rad.value = rad.allowAlpha ? hex(kRod) : hex(kRod).substr(0, kHexRgbLangd);
        break;
    case P::Number: {
        const auto v = egen_->numberValue(r);
        rad.value    = tal((v ? *v : 0.0) + kVardeSteg);
        break;
    }
    case P::Text:
        if (rad.choices.empty()) {
            rad.value = "setRow " + std::to_string(++nyRad_);
        } else {
            // Nästa val i listan, efter det som står.
            std::size_t k = 0;
            while (k < rad.choices.size() && rad.choices[k] != rad.value)
                ++k;
            rad.value = rad.choices[k < rad.choices.size() ? (k + 1) % rad.choices.size()
                                                           : 0];
        }
        break;
    }
    const std::string varde = rad.value;
    egen_->setRow(r, std::move(rad));
    logga("Fönstret", "setRow",
          "row=" + std::to_string(r) + " value=" + citat(varde) +
              " activeRow=" + std::to_string(egen_->activeRow()));
}

void TablesWindow::uppdateraSlumpcell()
{
    const int n = grid_->rowCount();
    if (n == 0)
        return;
    std::uniform_int_distribution<int> rad(0, n - 1);
    std::uniform_int_distribution<int> varde(0, int(kProcentMax));
    for (int forsok = 0; forsok < kSlumpForsok; ++forsok) {
        const int r = rad(slump_);
        if (grid_->rowKind(r) != K::Data)
            continue;
        grid_->setCell(r, kOpacitet, std::to_string(varde(slump_)));
        return;
    }
}
