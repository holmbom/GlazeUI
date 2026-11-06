// Table — den delade maskinen bakom ListView och DataGrid.
// MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) — men Table ritar bara
// DELAR (`drawCells`, `drawSpanRows`, och basens `drawHeader`), och listvyn
// och datagriden har var sin `draw()` med var sin `switch (c.ui)` runt dem.
// Radernas geometri bor HÄR i LOGISKA px — EN sanning som ritningen läser
// rakt av och som musvägen projicerar till fysiska px (Draw::localX/localY).
// Basen ColumnPanel ger kolumnerna, huvudet, avdelardraget och
// sorteringens tillstånd, och dess bas Panel klipp, hjulscroll,
// overlay-remsorna och innehållsytan.
//
// SKRIVNA BESLUT (de långa motiveringarna står i table.h):
//   tabellen sorterar själv, inom varje avsnitt · indexen är appens,
//   sorteringen och fällningen bara visning · naturlig jämförelse +
//   sorteringsnyckel · höjden ur raderna, bredden appens · cellbredden
//   cachad per cell · ingen språkmedveten kollation.

#include "glaze/widgets/table.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "glaze/app_window.h"
#include "glaze/draw.h"

#include "../prop_parse.h"
#include "../value_cell.h"

namespace glaze {
namespace {

// Svaret utanför tabellen — en TOM sträng med livstid nog att lämna ut en
// referens till (`cell()`s kontrakt).
const std::string& tomCell()
{
    static const std::string tom;
    return tom;
}

// Svaret utanför tabellen för en cells bild (`cellImage`s kontrakt).
const ImageSource& tomBild()
{
    static const ImageSource tom;
    return tom;
}

} // namespace

Table::Table()
{
    setClip(true);          // editorerna (DataGrid) klipps mot raderna
    setBorder(true);        // tabellen ÄR en inramad yta — och `border` är
                            // det som drar in innerBounds en ramtjocklek
}

// --------------------------------------------------------------- raderna

void Table::addRow(std::vector<std::string> cells, ImageSource img,
                   std::optional<gfx::Color> color)
{
    rows_.push_back(Row{std::move(cells), std::move(img), color});
    keys_.emplace_back();
    cellColors_.emplace_back();
    cellImages_.emplace_back();
    cellW_.emplace_back();
    contentChanged();
    rowAppended();
    orderChanged();   // invaliderar — höjden ur raderna växer (mätcachen)
}

void Table::setRow(int index, std::vector<std::string> cells, ImageSource img,
                   std::optional<gfx::Color> color)
{
    if (index < 0 || index >= rowCount()) {
        gfx::log("Table(%s): setRow(%d) utanför intervallet (%d rader) — "
                 "ignoreras", name.c_str(), index, rowCount());
        return;
    }
    // OMSORTERING bara när det som sorteras ändrades: den sorterade cellens
    // text, eller en nyckel i den kolumnen som nu tappas.
    Row&      rad = rows_[std::size_t(index)];
    const int kol = sortColumn();
    bool      omsortera = false;
    if (kol >= 0 && kol < columnCount() && rad.kind == RowKind::Data) {
        const std::string& ny = std::size_t(kol) < cells.size()
                                    ? cells[std::size_t(kol)]
                                    : tomCell();
        const auto& nycklar = keys_[std::size_t(index)];
        omsortera = cell(index, kol) != ny ||
                    (std::size_t(kol) < nycklar.size() &&
                     nycklar[std::size_t(kol)].has_value());
    }
    // TILLDELNING: radens data ersätts helt — en utelämnad färg blir ingen
    // färg, inte heller cellernas eller cellbildernas — och nycklarna beskrev
    // den gamla texten. Slaget och fällningen är strukturens och står kvar,
    // som valet och den aktuella raden (vyns).
    rad.cells = std::move(cells);
    rad.img   = std::move(img);
    rad.color = color;
    keys_[std::size_t(index)].clear();
    cellColors_[std::size_t(index)].clear();
    cellImages_[std::size_t(index)].clear();
    cellW_[std::size_t(index)].clear();
    contentChanged();
    rowReplaced(index);
    if (omsortera)
        orderChanged();
    else
        invalidate();
}

void Table::setRowColor(int row, std::optional<gfx::Color> color)
{
    if (row < 0 || row >= rowCount()) {
        gfx::log("Table(%s): setRowColor(%d) utanför intervallet (%d rader) — "
                 "ignoreras", name.c_str(), row, rowCount());
        return;
    }
    std::optional<gfx::Color>& c = rows_[std::size_t(row)].color;
    if (c == color)
        return;   // hård regel 1: invalidera vid ÄNDRING
    c = color;
    invalidate();
}

std::optional<gfx::Color> Table::rowColor(int row) const
{
    return row >= 0 && row < rowCount() ? rows_[std::size_t(row)].color
                                        : std::nullopt;
}

// CELLENS FÄRGER — `setRowColor`s form. Kolumnen behöver inte finnas
// än (`setCell`s skäl: satsordningen i en `.glazeui`), och en sättning som
// inte ändrar något växer inte ens vektorn.
void Table::setCellColor(int row, int column, std::optional<gfx::Color> background,
                         std::optional<gfx::Color> text)
{
    if (row < 0 || row >= rowCount() || column < 0) {
        gfx::log("Table(%s): setCellColor(%d, %d) utanför tabellen (%d rader) "
                 "— ignoreras", name.c_str(), row, column, rowCount());
        return;
    }
    auto& färger = cellColors_[std::size_t(row)];
    if (std::size_t(column) >= färger.size()) {
        if (!background && !text)
            return;   // hård regel 1: inget att ändra
        färger.resize(std::size_t(column) + 1);
    }
    CellColors& k = färger[std::size_t(column)];
    if (k.background == background && k.text == text)
        return;   // hård regel 1: invalidera vid ÄNDRING
    k = CellColors{background, text};
    invalidate();
}

std::optional<gfx::Color> Table::cellBackground(int row, int column) const
{
    if (row < 0 || row >= rowCount() || column < 0)
        return std::nullopt;
    const auto& färger = cellColors_[std::size_t(row)];
    return std::size_t(column) < färger.size()
               ? färger[std::size_t(column)].background
               : std::nullopt;
}

std::optional<gfx::Color> Table::cellTextColor(int row, int column) const
{
    if (row < 0 || row >= rowCount() || column < 0)
        return std::nullopt;
    const auto& färger = cellColors_[std::size_t(row)];
    return std::size_t(column) < färger.size() ? färger[std::size_t(column)].text
                                               : std::nullopt;
}

// Cellens färg, annars radens, annars temats.
gfx::Color Table::appTextColor(int row, int column, gfx::Color theme) const
{
    if (const auto cell = cellTextColor(row, column))
        return *cell;
    return rowColor(row).value_or(theme);
}

void Table::setRowHeaders(bool on)
{
    if (on == rowHeaders_)
        return;   // hård regel 1: invalidera vid ÄNDRING
    rowHeaders_ = on;
    invalidate();
    clampScroll();   // rullningsytan byter bredd och rubrikkolumnen sidor
}

void Table::setRows(std::vector<Row> rows)
{
    rows_ = std::move(rows);
    keys_.assign(rows_.size(), {});
    cellColors_.assign(rows_.size(), {});
    cellImages_.assign(rows_.size(), {});
    cellW_.assign(rows_.size(), {});
    contentChanged();
    current_ = -1;
    rowsReplaced();
    orderChanged();
    clampScroll();
}

// BORTTAGNINGEN — de parallella vektorerna tappar raden, och varje radindex
// efter den räknas ned ett steg: den aktuella raden här, subklassens i
// `rowRemoved`. Den aktuella raden som togs bort landar på raden som nu står
// på samma PLATS — annars den före — och aldrig på en tomrad (navigeringens
// regel). Försvann en kategori hör raderna efter den till kategorin före, som
// kan vara ihopfälld: de dolda rapporteras som en fällnings (`reportHidden`).
void Table::removeRow(int index)
{
    if (index < 0 || index >= rowCount()) {
        gfx::log("Table(%s): removeRow(%d) utanför intervallet (%d rader) — "
                 "ignoreras", name.c_str(), index, rowCount());
        return;
    }
    std::vector<int> före  = visibleSnapshot();
    const int        plats = före[std::size_t(index)];
    const bool kategori = rows_[std::size_t(index)].kind == RowKind::Category;
    const auto at       = std::ptrdiff_t(index);
    före.erase(före.begin() + at);
    rows_.erase(rows_.begin() + at);
    keys_.erase(keys_.begin() + at);
    cellColors_.erase(cellColors_.begin() + at);
    cellImages_.erase(cellImages_.begin() + at);
    cellW_.erase(cellW_.begin() + at);

    const bool aktuellBorta = current_ == index;
    if (aktuellBorta)
        current_ = -1;
    else if (current_ > index)
        --current_;
    orderChanged();   // permutationen och höjden ur raderna
    // Kategoritillhörigheten — indraget i kolumn 0 — följer raderna efter en
    // kategori som försvann, och AutoMin-bredderna mäter inte längre raden.
    if (kategori)
        invalidateColumnWidths(0);
    contentChanged();
    // Subklassen FÖRE den nya ordningen: sorteringen frågar `cellSortValue`,
    // som läser subklassens radtillstånd (datagridens cellinställningar).
    rowRemoved(index);
    if (aktuellBorta && plats >= 0) {
        const int  n   = positionCount();
        const int  p   = std::min(plats, n - 1);
        const auto tom = [this](int pos) {
            return rows_[std::size_t(rowAtDisplay(pos))].kind == RowKind::Blank;
        };
        for (int s = p; s >= 0 && s < n && current_ < 0; ++s)
            if (!tom(s))
                current_ = rowAtDisplay(s);
        for (int s = p - 1; s >= 0 && current_ < 0; --s)
            if (!tom(s))
                current_ = rowAtDisplay(s);
    }
    ensureOrder();
    const int ny = index < rowCount() ? categoryOf_[std::size_t(index)] : -1;
    reportHidden(före, ny);
    clampScroll();
}

void Table::setCell(int row, int column, std::string text)
{
    if (row < 0 || row >= rowCount() || column < 0) {
        gfx::log("Table(%s): setCell(%d, %d) utanför tabellen (%d rader) — "
                 "ignoreras", name.c_str(), row, column, rowCount());
        return;
    }
    // Kolumnen behöver INTE finnas än — ordningen mellan satserna i en
    // `.glazeui` ska inte avgöra om en cell går att sätta. En rad som är
    // kortare än kolumnen fylls ut med tomma celler.
    auto& celler = rows_[std::size_t(row)].cells;
    if (std::size_t(column) >= celler.size())
        celler.resize(std::size_t(column) + 1);
    cellSet(row, column);
    auto& nycklar = keys_[std::size_t(row)];
    bool  nyckelTappad = false;
    if (std::size_t(column) < nycklar.size() &&
        nycklar[std::size_t(column)].has_value()) {
        nycklar[std::size_t(column)].reset();   // nyckeln beskrev gamla texten
        nyckelTappad = true;
    }
    const bool sorterad = column == sortColumn() && isData(row);
    if (celler[std::size_t(column)] == text) {
        if (nyckelTappad && sorterad)
            orderChanged();
        return;
    }
    celler[std::size_t(column)] = std::move(text);
    // PRESTANDA: bara den här cellen mäts om (table.h).
    invalidateCellWidth(row, column);
    if (sorterad)
        orderChanged();
    else
        invalidate();
}

const std::string& Table::cell(int row, int column) const
{
    if (row < 0 || row >= rowCount() || column < 0)
        return tomCell();
    const auto& celler = rows_[std::size_t(row)].cells;
    return std::size_t(column) < celler.size() ? celler[std::size_t(column)]
                                               : tomCell();
}

void Table::setSortKey(int row, int column, double key)
{
    if (row < 0 || row >= rowCount() || column < 0) {
        gfx::log("Table(%s): setSortKey(%d, %d) utanför tabellen (%d rader) "
                 "— ignoreras", name.c_str(), row, column, rowCount());
        return;
    }
    auto& nycklar = keys_[std::size_t(row)];
    if (std::size_t(column) >= nycklar.size())
        nycklar.resize(std::size_t(column) + 1);
    // NaN går inte att ordna (den är varken mindre eller större än något),
    // och en jämförelse som inte är en strikt svag ordning gör sorteringen
    // odefinierad — så den räknas som INGEN nyckel.
    if (std::isnan(key))
        nycklar[std::size_t(column)].reset();
    else
        nycklar[std::size_t(column)] = key;
    if (column == sortColumn())
        orderChanged();
}

// ------------------------------------------------------- radslag och fällning

bool Table::isData(int row) const
{
    return row >= 0 && row < rowCount() &&
           rows_[std::size_t(row)].kind == RowKind::Data;
}

void Table::setRowKind(int row, RowKind kind)
{
    if (row < 0 || row >= rowCount()) {
        gfx::log("Table(%s): setRowKind(%d) utanför intervallet (%d rader) — "
                 "ignoreras", name.c_str(), row, rowCount());
        return;
    }
    Row& rad = rows_[std::size_t(row)];
    if (rad.kind == kind)
        return;   // hård regel 1: invalidera vid ÄNDRING
    const std::vector<int> före = visibleSnapshot();
    rad.kind = kind;
    // En tomrad blir aldrig aktuell.
    if (kind == RowKind::Blank && current_ == row)
        current_ = -1;
    // Kategoritillhörigheten (indraget i kolumn 0) kan ha ändrats för raderna
    // efter den, och raden har lämnat eller kommit in bland dataraderna som
    // varje kolumns anpassade bredd mäter.
    invalidateColumnWidths(0);
    contentChanged();
    rowKindChanged(row);
    orderChanged();   // avsnitten och synligheten
    reportHidden(före, row);
    clampScroll();
}

Table::RowKind Table::rowKind(int row) const
{
    return row >= 0 && row < rowCount() ? rows_[std::size_t(row)].kind
                                        : RowKind::Data;
}

void Table::setExpanded(int row, bool on)
{
    if (row < 0 || row >= rowCount()) {
        gfx::log("Table(%s): setExpanded(%d) utanför intervallet (%d rader) — "
                 "ignoreras", name.c_str(), row, rowCount());
        return;
    }
    if (rows_[std::size_t(row)].kind != RowKind::Category) {
        gfx::log("Table(%s): setExpanded(%d) — raden är ingen kategori, "
                 "ignoreras", name.c_str(), row);
        return;
    }
    applyExpanded(row, on);
}

bool Table::isExpanded(int row) const
{
    return row >= 0 && row < rowCount() && rows_[std::size_t(row)].expanded;
}

// FÄLLNINGEN — den programmatiska och användarens väg. Synligheten räknas om,
// och de rader som just doldes släpps: den aktuella raden flyttar upp till
// kategorin (tangentbordet får inte stå på en rad som inte syns), och
// subklassen släpper valet och en redigering (`rowsHidden`).
void Table::applyExpanded(int row, bool on)
{
    Row& rad = rows_[std::size_t(row)];
    if (rad.expanded == on)
        return;   // hård regel 1: invalidera vid ÄNDRING
    const std::vector<int> före = visibleSnapshot();
    rad.expanded = on;
    visibilityChanged();
    reportHidden(före, row);
    clampScroll();   // färre platser kan ha krympt spelrummet
}

std::vector<int> Table::visibleSnapshot() const
{
    ensureOrder();
    return pos_;
}

void Table::reportHidden(const std::vector<int>& före, int category)
{
    ensureOrder();
    std::vector<int> dolda;
    for (std::size_t r = 0; r < före.size() && r < pos_.size(); ++r)
        if (före[r] >= 0 && pos_[r] < 0)
            dolda.push_back(int(r));
    if (dolda.empty())
        return;
    if (current_ >= 0 && pos_[std::size_t(current_)] < 0)
        current_ = category;
    rowsHidden(dolda);
}

bool Table::toggleCategoryFromUser(int row)
{
    if (row < 0 || row >= rowCount() ||
        rows_[std::size_t(row)].kind != RowKind::Category)
        return true;
    const bool ut = !rows_[std::size_t(row)].expanded;
    applyExpanded(row, ut);
    // Handlern SIST, med vakten: en handler får riva eller flytta tabellen.
    RowArgs e;
    e.row = row;
    return ut ? fireEvent(onExpand, e) : fireEvent(onCollapse, e);
}

// KATEGORINS INDRAG — bara i kolumn 0, bara för en datarad som hör till en
// kategori.
float Table::contentIndent(const Ctx& c, int row, int column) const
{
    if (column != 0 || !isData(row))
        return 0.0f;
    ensureOrder();
    return categoryOf_[std::size_t(row)] >= 0 ? c.style->tableCategoryIndent
                                              : 0.0f;
}

// ------------------------------------------------------------ cellens bild

void Table::setCellImage(int row, int column, ImageSource img)
{
    if (row < 0 || row >= rowCount() || column < 0) {
        gfx::log("Table(%s): setCellImage(%d, %d) utanför tabellen (%d rader) "
                 "— ignoreras", name.c_str(), row, column, rowCount());
        return;
    }
    if (column == 0) {
        ImageSource& ikon = rows_[std::size_t(row)].img;
        if (ikon == img)
            return;   // hård regel 1: invalidera vid ÄNDRING
        ikon = std::move(img);
    } else {
        auto& bilder = cellImages_[std::size_t(row)];
        if (std::size_t(column) >= bilder.size()) {
            if (!img.valid())
                return;   // inget att ändra
            bilder.resize(std::size_t(column) + 1);
        }
        ImageSource& bild = bilder[std::size_t(column)];
        if (bild == img)
            return;
        bild = std::move(img);
    }
    invalidateCellWidth(row, column);
    invalidate();
}

const ImageSource& Table::cellImage(int row, int column) const
{
    if (row < 0 || row >= rowCount() || column < 0)
        return tomBild();
    if (column == 0)
        return rows_[std::size_t(row)].img;
    const auto& bilder = cellImages_[std::size_t(row)];
    return std::size_t(column) < bilder.size() ? bilder[std::size_t(column)]
                                               : tomBild();
}

// ------------------------------------------------------------ sorteringen

// ColumnPanels krok (sorteringskolumnen, riktningen, kolumnerna) och
// tabellens egna ändringar (raderna, cellerna, nycklarna, slagen) går samma
// väg. Höjden ur raderna följer antalet platser, så mätcachen invalideras.
void Table::orderChanged()
{
    sortDirty_  = true;
    orderDirty_ = true;
    invalidate();
}

void Table::visibilityChanged()
{
    orderDirty_ = true;
    invalidate();
}

void Table::invalidateOrder()
{
    orderChanged();
}

// ---------------------------------------------------- ikonstorleken

void Table::setIconSize(float px)
{
    if (!(px >= 0.0f)) {
        gfx::log("Table(%s): setIconSize(%g) — 0 är AUTO, ignoreras",
                 name.c_str(), double(px));
        return;
    }
    if (px == iconSize_)
        return;
    iconSize_ = px;
    contentChanged();   // ikonen står i kolumn 0:s innehåll (cachen nycklar
                        // på storleken)
    invalidate();
    clampScroll();   // radhöjden kan ha ändrats
}

// EN CELLS NYCKEL: den explicita, annars subklassens värde, annars texten.
Table::SortKey Table::sortKey(int row, int column) const
{
    const auto& k = keys_[std::size_t(row)];
    if (std::size_t(column) < k.size() && k[std::size_t(column)])
        return SortKey{k[std::size_t(column)], nullptr};
    if (const auto v = cellSortValue(row, column); v && !std::isnan(*v))
        return SortKey{v, nullptr};
    return SortKey{std::nullopt, &cell(row, column)};
}

// Nyckel mot nyckel numeriskt, en cell MED nyckel före en utan, annars
// naturlig text.
int Table::compareKeys(const SortKey& a, const SortKey& b)
{
    if (a.number && b.number)
        return *a.number < *b.number ? -1 : (*b.number < *a.number ? 1 : 0);
    if (a.number)
        return -1;
    if (b.number)
        return 1;
    return compareNatural(*a.text, *b.text);
}

// ORDNINGEN räknas LATT — när den första gången behövs efter en ändring.
// SORTERINGEN: varje följd av datarader mellan två gränser (kategori- och
// tomrader, som står kvar på sin plats) sorteras stabilt för sig, så lika
// rader behåller appens ordning i BÅDA riktningarna. Nycklarna räknas EN gång.
// SYNLIGHETEN: en rad efter en ihopfälld kategori (fram till nästa) har
// ingen plats.
void Table::ensureOrder() const
{
    const std::size_t n = rows_.size();
    if (sortDirty_ || sorted_.size() != n) {
        sorted_.resize(n);
        std::iota(sorted_.begin(), sorted_.end(), 0);
        if (sortColumn() >= 0 && sortColumn() < columnCount()) {
            const int  kol = sortColumn();
            const bool upp = isSortAscending();
            std::vector<SortKey> nycklar(n);
            for (std::size_t r = 0; r < n; ++r)
                if (rows_[r].kind == RowKind::Data)
                    nycklar[r] = sortKey(int(r), kol);
            const auto före = [&nycklar, upp](int a, int b) {
                const int j = compareKeys(nycklar[std::size_t(a)],
                                          nycklar[std::size_t(b)]);
                return upp ? j < 0 : j > 0;
            };
            std::size_t i = 0;
            while (i < n) {
                if (rows_[i].kind != RowKind::Data) {
                    ++i;
                    continue;
                }
                std::size_t j = i;
                while (j < n && rows_[j].kind == RowKind::Data)
                    ++j;
                std::stable_sort(sorted_.begin() + std::ptrdiff_t(i),
                                 sorted_.begin() + std::ptrdiff_t(j), före);
                i = j;
            }
        }
        sortDirty_  = false;
        orderDirty_ = true;
    }
    if (!orderDirty_ && pos_.size() == n)
        return;
    categoryOf_.assign(n, -1);
    int kategori = -1;
    for (std::size_t r = 0; r < n; ++r) {
        if (rows_[r].kind == RowKind::Category)
            kategori = int(r);
        else
            categoryOf_[r] = kategori;
    }
    order_.clear();
    pos_.assign(n, -1);
    for (std::size_t p = 0; p < n; ++p) {
        const int  r   = sorted_[p];
        const int  kat = categoryOf_[std::size_t(r)];
        const bool syns = kat < 0 || rows_[std::size_t(kat)].expanded;
        if (syns) {
            pos_[std::size_t(r)] = int(order_.size());
            order_.push_back(r);
        }
    }
    orderDirty_ = false;
}

int Table::displayRow(int row) const
{
    if (row < 0 || row >= rowCount())
        return -1;
    ensureOrder();
    return pos_[std::size_t(row)];
}

int Table::rowAtDisplay(int position) const
{
    if (position < 0)
        return -1;
    ensureOrder();
    return std::size_t(position) < order_.size() ? order_[std::size_t(position)]
                                                 : -1;
}

int Table::positionCount() const
{
    ensureOrder();
    return int(order_.size());
}

// ---------------------------------------------------------- aktuell rad

void Table::setCurrent(int row)
{
    if (row < -1 || row >= rowCount())
        row = -1;
    if (row == current_)
        return;
    current_ = row;
    invalidate();
}

void Table::ensureVisible(int row)
{
    // Samma tvådelade vakt som `setRows`: utan ett VISAT fönster finns
    // varken renderer eller font att bygga ett Ctx ur.
    if (!appWindow() || !appWindow()->isOpen())
        return;
    const int pos = displayRow(row);
    if (pos < 0)
        return;
    const Ctx   c   = behaviorCtx();
    const float h   = rowH(c);
    const float top = float(pos) * h;
    const float vh  = scrollViewport(c).h;
    float       s   = scrollYClamped(c);
    if (top < s)
        s = top;
    else if (top + h > s + vh)
        s = top + h - vh;
    setScroll(scrollX(), s);   // invaliderar bara vid faktisk ändring
}

// ------------------------------------------------------------ setProp

bool Table::setProp(const std::string& key, const std::string& value)
{
    if (key == "iconSize") {
        float v = 0.0f;
        if (propparse::parseNumber(value, v))
            setIconSize(v);   // negativt ⇒ sättarens logg + no-op
        else
            gfx::log("Table(%s): ogiltig iconSize: %s (ignoreras)",
                     name.c_str(), value.c_str());
        return true;
    }
    if (key == "rowHeaders") {
        setRowHeaders(propparse::parseBool(value));
        return true;
    }
    return ColumnPanel::setProp(key, value);   // basen SIST — konventionen
}

// ------------------------------------------------------------- geometrin
// EN sanning i widget-lokala LOGISKA px, scroll-förskjuten (samma rymd som
// ritningen). Musvägen projicerar den — aldrig två uppsättningar mått.

float Table::baseRowH(const Ctx& c) const
{
    return c.style->listRowH;
}

// RADENS HÖJD med en satt ikonstorlek: ikonen får samma luft i raden som
// stilens egen ikon (`iconFallback`) har i grundhöjden — och raden växer
// aldrig under grundhöjden. AUTO är grundhöjden rakt av.
float Table::rowH(const Ctx& c) const
{
    const float bas = baseRowH(c);
    if (iconSize_ <= 0.0f)
        return bas;
    return std::max(bas, iconSize_ + (bas - c.style->iconFallback));
}

// CELLEN I INNEHÅLLETS RYMD — origo i innehållsytans övre vänstra hörn,
// OSCROLLAT, vilket är exakt origo för `childArea` (Panels innerRect minus
// scrollen). Det här är den ENDA formeln för var en cell ligger: `rowRect`/
// `cellRect` flyttar den till widgetens rymd, och datagridens
// `placeChild` lägger editorn på den — så det man ser och det man
// redigerar i kan aldrig hamna på två ställen.
gfx::Rectf Table::cellInContent(const Ctx& c, int position, int column) const
{
    return cellInContent(c, position, column, contentW(c));
}

gfx::Rectf Table::cellInContent(const Ctx& c, int position, int column,
                                float cw) const
{
    const float h = rowH(c);
    return gfx::Rectf{columnX(c, column, cw),
                      headerH(c, cw) + float(position) * h,
                      columnW(c, column, cw), h};
}

// Radens band är det SYNLIGA bandet — markeringen fyller det man ser, och
// Minimalistics markör står kvar i vänsterkanten när tabellen rullats i
// sidled. Cellerna (`cellRect`) följer den horisontella scrollen.
gfx::Rectf Table::rowRect(const Ctx& c, int position) const
{
    const gfx::Rectf in = innerBounds(c);
    const gfx::Rectf r  = cellInContent(c, position, 0);
    return gfx::Rectf{in.x, in.y - scrollYClamped(c) + r.y, in.w, r.h};
}

gfx::Rectf Table::cellRect(const Ctx& c, int position, int column) const
{
    const gfx::Rectf r = rowRect(c, position);
    // Radrubriken rullar inte.
    const float sx = column == 0 && frozenW(c) > 0.0f ? 0.0f : scrollXClamped(c);
    return gfx::Rectf{r.x - sx + columnX(c, column), r.y, columnW(c, column),
                      r.h};
}

// RADRUBRIKEN: första kolumnens bredd när den är fast, annars 0.
float Table::frozenW(const Ctx& c) const
{
    return rowHeaders_ && columnCount() > 0 ? columnW(c, 0) : 0.0f;
}

// KOLUMNENS CELLER: dataradernas bredaste cell — också de dolda radernas, så
// en fällning aldrig ändrar en bredd. Bredden läses ur cachen per cell, och
// bara en inaktuell cell mäts (`cellContentWidth`); svaret per kolumn cachas
// dessutom av ColumnPanel (`fittedW`) tills `contentChanged`.
float Table::cellContentW(const Ctx& c, int column) const
{
    const CellWidthEnv miljö{c.font, c.style, c.scale, Draw::lineHeight(c),
                             iconSize_};
    if (miljö.font != cellWEnv_.font || miljö.style != cellWEnv_.style ||
        miljö.scale != cellWEnv_.scale || miljö.lineH != cellWEnv_.lineH ||
        miljö.iconSize != cellWEnv_.iconSize) {
        for (auto& rad : cellW_)
            rad.clear();
        cellWEnv_ = miljö;
    }
    float w = 0.0f;
    for (int r = 0; r < rowCount(); ++r)
        if (rows_[std::size_t(r)].kind == RowKind::Data)
            w = std::max(w, cachedCellW(c, r, column));
    return w;
}

float Table::cachedCellW(const Ctx& c, int row, int column) const
{
    auto& rad = cellW_[std::size_t(row)];
    if (std::size_t(column) >= rad.size())
        rad.resize(std::size_t(column) + 1, -1.0f);
    float& w = rad[std::size_t(column)];
    if (w < 0.0f)
        w = cellContentWidth(c, row, column);
    return w;
}

// EN CELLS INNEHÅLL i `drawCell`s egen ordning: i kolumn 0 indraget, zonen
// och ikonen före texten, i de andra kolumnerna cellens bild.
float Table::cellContentWidth(const Ctx& c, int row, int column) const
{
    const auto& celler = rows_[std::size_t(row)].cells;
    float       w      = std::size_t(column) < celler.size()
                             ? Draw::measure(c, celler[std::size_t(column)]).w
                             : 0.0f;
    const bool egenBild = cellDrawsOwnImage(row, column);
    if (column == 0) {
        w += contentIndent(c, row, 0) + leadW(c);
        const float sida = egenBild ? 0.0f : iconSide(c, row);
        if (sida > 0.0f)
            w += sida + c.style->iconGap;
    } else if (const ImageSource& bild = cellImage(row, column);
               bild.valid() && !egenBild) {
        w += imageSide(c, bild) + c.style->iconGap;
    }
    return w;
}

void Table::invalidateCellWidth(int row, int column)
{
    if (row < 0 || row >= rowCount() || column < 0)
        return;
    auto& rad = cellW_[std::size_t(row)];
    if (std::size_t(column) < rad.size())
        rad[std::size_t(column)] = -1.0f;
    contentChanged(column);
}

void Table::invalidateColumnWidths(int column)
{
    if (column < 0) {
        for (auto& rad : cellW_)
            rad.clear();
        contentChanged();
        return;
    }
    for (auto& rad : cellW_)
        if (std::size_t(column) < rad.size())
            rad[std::size_t(column)] = -1.0f;
    contentChanged(column);
}

// Zonen före ikonen: cellens luft och kategorins indrag in i FÖRSTA
// kolumnen, `leadW` bred och radens hela höjd — där `drawCell` hoppar förbi
// den. Utan kolumner finns ingen första cell och ingen zon: en tom rekt, som
// ingen punkt träffar.
gfx::Rectf Table::leadZone(const Ctx& c, int position) const
{
    if (columnCount() == 0)
        return gfx::Rectf{};
    const gfx::Rectf r = cellRect(c, position, 0);
    const float      in = contentIndent(c, rowAtDisplay(position), 0);
    return gfx::Rectf{r.x + cellPadX(c) + in, r.y, leadW(c), r.h};
}

int Table::firstVisiblePosition(const Ctx& c) const
{
    const float h = rowH(c);
    if (h <= 0.0f)
        return 0;   // degenererad stil — hellre allt än div/0
    return std::clamp(int(scrollYClamped(c) / h), 0, positionCount());
}

int Table::pageRows(const Ctx& c) const
{
    const float h = rowH(c);
    return h > 0.0f ? std::max(1, int(scrollViewport(c).h / h)) : 1;
}

// ---------------------------------------------------------------- träffen

int Table::positionAt(const Ctx& c, float my) const
{
    const gfx::Rectf vp = scrollViewport(c);
    const float      ly = Draw::localY(c, resolve(c), my);
    const float      h  = rowH(c);
    if (ly < vp.y || ly >= vp.y + vp.h || h <= 0.0f)
        return -1;
    const int p = int(std::floor((ly - vp.y + scrollYClamped(c)) / h));
    return p >= 0 && p < positionCount() ? p : -1;
}

// SNABBMENYNS träffväg — LOGISKA px från tabellens
// hörn till APPENS radindex. Huvudet, remsorna och allt utanför rullningsytan
// (i x och y — `positionAt` vaktar y) är "ingen".
int Table::rowAt(float localX, float localY) const
{
    if (!appWindow() || !appWindow()->isOpen())
        return -1;
    const Ctx        c  = behaviorCtx();
    const gfx::Rectf pt = Draw::project(c, resolve(c),
                                        gfx::Rectf{localX, localY, 0.0f, 0.0f});
    const int        p  = hitPosition(c, pt.x, pt.y);
    return p < 0 ? -1 : rowAtDisplay(p);
}

// Platsen under en FYSISK punkt i HELA rullningsytan:
// huvudet, remsorna och sidramen är "ingen" — den publika träffvägen och
// listvyns högertryck frågar samma sak.
int Table::hitPosition(const Ctx& c, float mx, float my) const
{
    if (inHeader(c, mx, my) || inScrollStrip(c, mx, my))
        return -1;
    // Radrubrikens remsa är en del av raden: från innehållsytans
    // vänsterkant, inte från rullningsytans.
    const gfx::Rectf vp = scrollViewport(c);
    const float      lx = Draw::localX(c, resolve(c), mx);
    if (lx < innerBounds(c).x || lx >= vp.x + vp.w)
        return -1;
    return positionAt(c, my);   // vaktar y och intervallet
}

int Table::columnAt(const Ctx& c, float mx) const
{
    const float      lx = Draw::localX(c, resolve(c), mx);
    const gfx::Rectf vp = scrollViewport(c);
    if (lx < innerBounds(c).x || lx >= vp.x + vp.w)
        return -1;   // (radrubriken räknas in: den är kolumn 0)
    for (int i = 0; i < columnCount(); ++i) {
        const gfx::Rectf r = cellRect(c, 0, i);
        if (lx >= r.x && lx < r.x + r.w)
            return i;
    }
    return -1;
}

// ------------------------------------------------------------- ritningen

// HÖJDEN UR RADERNA (table.h). AXELREGELN: en satt axel vinner. Bredden är
// appens; AUTO-höjden är huvudet + de synliga platserna × radhöjden + ramens
// indrag — och, när bredden är känd och kolumnerna är bredare än ytan, den
// vågräta listens tjocklek, så den sista raden inte står under listen. Ingen
// fråga här läser `c.area` eller `resolve()`: bredden kommer ur `width` eller
// ur `heightForWidth`. Varje ändring av raderna, fällningen, slagen och
// huvudet invaliderar (mätcachen).
gfx::Sizef Table::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        gfx::Sizef s{width, height};
        if (s.h <= 0.0f) {
            const float ram = isBorder() ? 2.0f * c.style->panelInset : 0.0f;
            const float cw  = width > 0.0f ? std::max(0.0f, width - ram) : 0.0f;
            s.h = headerH(c, cw) + float(positionCount()) * rowH(c) + ram;
            if (width > 0.0f && columnsW(c, cw) > cw)
                s.h += c.style->scrollThick;
        }
        return s;
    }
    }
}

// Samma höjd för den bredd layouten GER (widget.h): huvudets AUTO-rader och
// Autosize-kolumnerna räknas mot den, och listen läggs till när kolumnerna
// inte ryms.
std::optional<float> Table::heightForWidth(const Ctx& c, float bredd) const
{
    if (height > 0.0f)
        return std::nullopt;   // en satt höjd vinner
    const float ram = isBorder() ? 2.0f * c.style->panelInset : 0.0f;
    const float cw  = std::max(0.0f, bredd - ram);
    float       h   = headerH(c, cw) + float(positionCount()) * rowH(c) + ram;
    if (columnsW(c, cw) > cw)
        h += c.style->scrollThick;
    return h;
}

gfx::Sizef Table::contentExtent(const Ctx& c) const
{
    // FYSISKA px (Panels kontrakt), och bara RADERNAS: huvudet ligger
    // utanför rullningsytan (`scrollViewport`) och scrollar aldrig i höjdled.
    // Radrubriken rullar inte: bredden är de rullande kolumnernas,
    // som rullningsytan är lika mycket smalare än tabellen. Höjden är de
    // SYNLIGA platserna.
    return gfx::Sizef{(columnsW(c) - frozenW(c)) * c.scale,
                      float(positionCount()) * rowH(c) * c.scale};
}

// Barnen (datagridens editorer) klipps mot RADERNA — en editor som rullats
// upp under huvudet får aldrig ritas över det.
gfx::Rectf Table::clipRect(const Ctx& c) const
{
    return Draw::project(c, resolve(c), scrollViewport(c));
}

void Table::drawCells(const Ctx& c, int position, int row, gfx::Color fg,
                      bool appColors) const
{
    if (!isData(row))
        return;   // kategori- och tomraderna ritas av `drawSpanRows`
    const gfx::Rectf vp    = scrollViewport(c);
    const bool       fasta = frozenW(c) > 0.0f;
    for (int i = 0; i < columnCount(); ++i) {
        const gfx::Rectf r = cellRect(c, position, i);
        if (r.x > vp.x + vp.w)
            break;
        // Radrubriken ritas av `drawRowHeaders`, inte i raden.
        if (r.x + r.w < vp.x || (fasta && i == 0))
            continue;
        drawCell(c, position, row, i, fg, appColors);
    }
}

// CELLENS DELAR i `drawCell`s ordning: luften och kategorins indrag, i
// kolumn 0 zonen före ikonen (`leadW` — listvyns kryssruta, som listvyn ritar
// själv) och IKONEN genom krokarna (`iconSide`), i de andra kolumnerna
// cellens bild i radens storleksklass — och värdet efter dem.
Table::CellParts Table::cellContent(const Ctx& c, const gfx::Rectf& cell,
                                    int row, int column) const
{
    const float pad      = cellPadX(c);
    const float in       = contentIndent(c, row, column);
    float       tx       = cell.x + pad + in;
    float       w        = cell.w - 2.0f * pad - in;
    const bool  egenBild = cellDrawsOwnImage(row, column);
    float       sida     = 0.0f;
    if (column == 0) {
        tx += leadW(c);
        w -= leadW(c);
        sida = egenBild ? 0.0f : iconSide(c, row);
    } else if (const ImageSource& bild = cellImage(row, column);
               bild.valid() && !egenBild) {
        sida = imageSide(c, bild);
    }
    CellParts delar;
    if (sida > 0.0f) {
        delar.image = gfx::Rectf{tx, cell.y + (cell.h - sida) / 2.0f, sida, sida};
        tx += sida + c.style->iconGap;
        w -= sida + c.style->iconGap;
    }
    delar.content = gfx::Rectf{tx, cell.y, w, cell.h};
    return delar;
}

void Table::drawCell(const Ctx& c, int position, int row, int column,
                     gfx::Color fg, bool appColors) const
{
    const gfx::Rectf r = cellRect(c, position, column);
    // APPENS FÄRGER: cellens bakgrund fylls under innehållet, och
    // texten är cellens, radens eller temats färg.
    if (appColors) {
        if (const auto bg = cellBackground(row, column))
            Draw::rect(c, r, 0.0f, *bg);
        fg = appTextColor(row, column, fg);
    }
    const CellParts delar = cellContent(c, r, row, column);
    // Kolumn 0:s ikon genom kroken (`drawIcon`), så en subklass kan rita sin
    // egen; de andra kolumnernas bild som en bild.
    if (delar.image.w > 0.0f) {
        if (column == 0)
            drawIcon(c, delar.image, row, fg);
        else
            Draw::stateGlyph(c, delar.image, cellImage(row, column), isEnabled());
    }
    if (delar.content.w > 0.0f)
        drawCellValue(c, r, delar.content, row, column, fg);
}

// CELLENS VÄRDE — standard: texten kortad med ellips i kolumnens justering.
void Table::drawCellValue(const Ctx& c, const gfx::Rectf&,
                          const gfx::Rectf& content, int row, int column,
                          gfx::Color fg) const
{
    const auto& celler = rows_[std::size_t(row)].cells;
    if (std::size_t(column) < celler.size() &&
        std::size_t(column) < columns().size())
        Draw::textElided(c, content, celler[std::size_t(column)], fg,
                         columns()[std::size_t(column)].align);
}

// KATEGORIRADERNA över HELA radens band — också över radrubrikens remsa,
// klippt mot radernas yta på hela innehållsbredden (`drawRowHeaders`s klipp).
// Tomraderna ritar ingenting: de är luft.
void Table::drawSpanRows(const Ctx& c) const
{
    const gfx::Rectf in    = innerBounds(c);
    const gfx::Rectf vp    = scrollViewport(c);
    const float      pad   = cellPadX(c);
    bool             klipp = false;
    for (int p = firstVisiblePosition(c); p < positionCount(); ++p) {
        const gfx::Rectf r = rowRect(c, p);
        if (r.y >= vp.y + vp.h)
            break;
        const int  rad = rowAtDisplay(p);
        const Row& k   = rows_[std::size_t(rad)];
        if (k.kind != RowKind::Category)
            continue;
        if (!klipp) {
            Draw::clip(c, gfx::Rectf{in.x, vp.y, in.w, vp.h});
            klipp = true;
        }
        valuecell::drawCategoryRow(c, r, cell(rad, 0), k.img, k.expanded,
                                   isEnabled(), pad);
    }
    if (klipp)
        Draw::endClip(c);
}

// RADRUBRIKERNAS REMSA: kolumn 0 i rubrikens stil — ytan och
// hårlinjen är `drawHeader`s (`drawHeaderSurface`, `headerLineColor`), namnen
// rubrikens textfärg, en hårlinje under varje rad och vid remsans högerkant.
// Remsan ritas PER DATARAD — en tomrad är luft och en kategorirad spänner
// över remsan (`drawSpanRows`) —, klippt mot radernas yta på hela
// innehållsbredden (en rad som rullats halvvägs under huvudet får aldrig
// ritas över det). Varken val, hover eller appfärger: remsan är en rubrik.
void Table::drawRowHeaders(const Ctx& c) const
{
    const float fz = frozenW(c);
    if (fz <= 0.0f)
        return;
    const gfx::Rectf in    = innerBounds(c);
    const gfx::Rectf vp    = scrollViewport(c);
    const Pen        linje{headerLineColor(c), c.style->borderWidth};
    const auto&      col   = c.theme->colors;
    const gfx::Color fg    = isEnabled() ? col.text : col.textDim;

    Draw::clip(c, gfx::Rectf{in.x, vp.y, in.w, vp.h});
    for (int p = firstVisiblePosition(c); p < positionCount(); ++p) {
        const gfx::Rectf r = cellRect(c, p, 0);
        if (r.y >= vp.y + vp.h)
            break;
        const int rad = rowAtDisplay(p);
        if (!isData(rad))
            continue;
        drawHeaderSurface(c, gfx::Rectf{in.x, r.y, fz, r.h});
        drawCell(c, p, rad, 0, fg, false);
        drawRowHeaderMarks(c, p, rad, fg);
        Draw::line(c, r.x, r.y + r.h, r.x + r.w, r.y + r.h, linje);
        Draw::line(c, in.x + fz, r.y, in.x + fz, r.y + r.h, linje);
    }
    Draw::endClip(c);
}

// Bildens sida: en satt ikonstorlek, annars assetens storleksklass
// (Button-prejudikatet) — en bild ur pixlar har ingen och får stilens
// reservsida.
float Table::imageSide(const Ctx& c, const ImageSource& img) const
{
    if (iconSize_ > 0.0f)
        return iconSize_;
    const float k = img.sizeClass();
    return k > 0.0f ? k : c.style->iconFallback;
}

// Standardikonen är radens BILD, och en inaktiv tabell tonar den som
// knapparna gör.
//
// En SATT ikonstorlek gäller VARJE rad — också en utan bild, så texten
// står i linje genom hela listan; `drawIcon` ritar bara en giltig bild.
float Table::iconSide(const Ctx& c, int row) const
{
    if (iconSize_ > 0.0f)
        return iconSize_;
    const Row& rad = rows_[std::size_t(row)];
    return rad.img.valid() ? imageSide(c, rad.img) : 0.0f;
}

void Table::drawIcon(const Ctx& c, const gfx::Rectf& box, int row,
                     gfx::Color) const
{
    const ImageSource& img = rows_[std::size_t(row)].img;
    if (img.valid())
        Draw::stateGlyph(c, box, img, isEnabled());
}

// ------------------------------------------------------------- beteendet

bool Table::mouseWheel(const Ctx& c, float, float, float wheelX, float wheelY)
{
    // Lodrätt stegar hjulet i RADER (ListBox-prejudikatet: raden är listans
    // enhet), vågrätt i Panels `wheelStep`. Bubblar vidare när ingenting
    // rörde sig — en yttre panel ska få ta hjulet i kanten.
    const gfx::Sizef r  = scrollRange(c);
    const float      nx = std::clamp(
        scrollXClamped(c) - wheelScrollPx(c, wheelX, c.style->wheelStep), 0.0f,
        r.w);
    const float      ny = std::clamp(
        scrollYClamped(c) - wheelScrollPx(c, wheelY, rowH(c)), 0.0f, r.h);
    if (nx == scrollXClamped(c) && ny == scrollYClamped(c))
        return false;
    setScroll(nx, ny);
    return true;
}

// NAVIGERINGEN — målplatsen för en tangent bland de SYNLIGA platserna.
// PageUp/PageDown flyttar en SIDA (de hela rader som syns), och numpadens
// navigationsläge viks av `AppWindow` före oss. En TOMRAD är aldrig ett mål:
// sökningen går vidare i tangentens riktning och, vid tabellens kant,
// tillbaka mot ursprunget.
int Table::navigationTarget(const Ctx& c, gfx::Key k, int from) const
{
    const int n = positionCount();
    if (n == 0)
        return -1;
    const auto tom = [this](int p) {
        return rows_[std::size_t(rowAtDisplay(p))].kind == RowKind::Blank;
    };
    const auto mål = [&](int t, int riktning) {
        t = std::clamp(t, 0, n - 1);
        for (int s = t; s >= 0 && s < n; s += riktning)
            if (!tom(s))
                return s;
        for (int s = t - riktning; s >= 0 && s < n; s -= riktning)
            if (!tom(s))
                return s;
        return -1;   // bara tomrader
    };
    switch (k) {
    case gfx::Key::Up:       return mål(from - 1, -1);
    case gfx::Key::Down:     return mål(from + 1, 1);
    case gfx::Key::Home:     return mål(0, 1);
    case gfx::Key::End:      return mål(n - 1, -1);
    case gfx::Key::PageUp:   return mål(from - pageRows(c), -1);
    case gfx::Key::PageDown: return mål(from + pageRows(c), 1);
    default:                 return -1;
    }
}

} // namespace glaze
