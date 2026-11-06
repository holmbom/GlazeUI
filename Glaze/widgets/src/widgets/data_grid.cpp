// DataGrid — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ RADER:
// Draw ritar, c.style bär måtten och temat färgerna. Kolumnerna, raderna,
// sorteringen, kategorierna och huvudet är `Table`s (table.cpp); här bor
// cellernas redigerare, REDIGERINGEN och recepten runt cellerna. Tolkningen,
// måtten och ritningen av en kryss-, färg-, tal- och knappcell är de typade
// cellernas (`src/value_cell.h`), delade med PropertyGrid.
//
// SKRIVNA BESLUT (de långa motiveringarna står i data_grid.h):
//   tre editorer som riktiga barn, placerade av datagriden (`placeChild`) ·
//   kryss och knapp utan editor (datagriden är tabbstoppet) · en sparning per
//   avslutad redigering (inte live) · fokusförlustens `onChange` postas · en
//   aktuell cell i taget · kolumnens redigerare hör till INDEXET, cellens
//   till raden · radvalet är listvyns, med ett eget event.

#include "glaze/widgets/data_grid.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include "glaze/app_window.h"
#include "glaze/draw.h"
#include "glaze/strings.h"
#include "glaze/widgets/color_picker.h"
#include "glaze/widgets/dropdown.h"
#include "glaze/widgets/textinput.h"

#include "../number_text.h"
#include "../prop_parse.h"
#include "../value_cell.h"

namespace glaze {

// ----------------------------------------------------------- editorerna
//
// En tangent går till den FOKUSERADE widgeten och bubblar aldrig vidare —
// så utan subklasserna hade Enter, Escape och Tab aldrig nått datagriden
// medan en cell redigeras (talfältets `Field` är prejudikatet). Det enda de
// gör är att lämna över tangenterna som AVSLUTAR en redigering, och att
// berätta när fokus lämnar dem. Allt annat — caret, markering, klippbord,
// listan, popupen — ärvs orört.

namespace {

// Tangenten bär en modifierare som gör den till någon annans genväg
// (Ctrl/Cmd/Alt) — då är den inte redigeringens.
bool annansGenväg(std::uint32_t mods)
{
    return (mods & (gfx::kModCtrl | gfx::kModCmd | gfx::kModAlt)) != 0;
}

// Redigerarens talregler i de typade cellernas form.
valuecell::NumberSpec numberSpec(const DataGrid::EditorSpec& s)
{
    valuecell::NumberSpec n;
    n.min        = s.min;
    n.max        = s.max;
    n.decimals   = s.decimals;
    n.prefix     = s.prefix;
    n.suffix     = s.suffix;
    n.allowEmpty = s.allowEmpty;
    n.emptyText  = s.emptyText;
    return n;
}

// Ändrar redigeraren cellens VÄRDE? En val-cell utan val gör det inte, och
// inte heller en knapp, en skrivskyddad cell eller en låst cell.
bool skriverVärde(const DataGrid::EditorSpec& s)
{
    if (s.readOnly)
        return false;
    switch (s.editor) {
    case DataGrid::Editor::Text:
    case DataGrid::Editor::Number:
    case DataGrid::Editor::Check:
    case DataGrid::Editor::Color:  return true;
    case DataGrid::Editor::Choice: return !s.choices.empty();
    case DataGrid::Editor::Button:
    case DataGrid::Editor::None:   return false;
    }
    return false;
}

// Ett index utan inställning — fritext.
const DataGrid::EditorSpec& standardSpec()
{
    static const DataGrid::EditorSpec s;
    return s;
}

} // namespace

class DataGrid::TextEditor final : public TextInput {
public:
    explicit TextEditor(DataGrid& owner) : owner_(owner) {}

    // En NY CELL börjar: texten och en TOM ångra-historik. Editorn
    // ÅTERANVÄNDS mellan cellerna, och `setText`s
    // likhetsvakt bevarar historiken när nästa cell råkar ha samma text —
    // då hade Ctrl+Z i den nya cellen spelat upp redigeringen av den förra.
    // `tal` = en talcell: bara taltecken tas emot.
    void börja(const std::string& text, bool tal)
    {
        numeric_ = tal;
        setText(text);
        resetHistory();
    }

    // Hela texten markerad (det man skriver ERSÄTTER den), eller markören
    // sist utan markering.
    void markera(bool allt)
    {
        if (allt) {
            selectAll();
        } else {
            const int sist = int(text().size());
            setSelection(sist, sist);
        }
    }

    // Det FÖRSTA tecknet (skrivandet som börjar redigeringen) går samma väg
    // som alla följande — fältets egen filtrering och caret.
    void skriv(const char* utf8) { textInput(utf8); }

protected:
    // `saveEdit` svarar om datagriden lever efter sparningens handlers
    // — en som rev den, och oss med den, lämnar ingen cell att flytta.
    bool keyDown(gfx::Key k, std::uint32_t mods) override
    {
        const bool shift = (mods & gfx::kModShift) != 0;
        if (annansGenväg(mods))
            return TextInput::keyDown(k, mods);
        switch (k) {
        case gfx::Key::Return:
            if (owner_.saveEdit())
                owner_.moveFromUser(shift ? -1 : 1, 0);
            return true;
        case gfx::Key::Escape:
            owner_.cancelEdit();
            return true;
        case gfx::Key::Tab:
            if (!owner_.saveEdit())
                return true;
            return owner_.tabFromUser(shift ? -1 : 1);
        case gfx::Key::Up:
        case gfx::Key::Down:
            if (shift)
                break;   // fältets egen markering
            if (owner_.saveEdit())
                owner_.moveFromUser(k == gfx::Key::Up ? -1 : 1, 0);
            return true;
        default:
            break;
        }
        return TextInput::keyDown(k, mods);
    }

    // Ett tecken som inte ingår i ett tal sväljs redan vid tangenttrycket i
    // en talcell; en inklistring prövas av tolkningen när cellen sparas.
    bool textInput(const char* utf8) override
    {
        if (!numeric_)
            return TextInput::textInput(utf8);
        const std::string ok = numbertext::filterNumberChars(utf8);
        if (ok.empty())
            return true;
        return TextInput::textInput(ok.c_str());
    }

    void focusLost() override
    {
        TextInput::focusLost();
        owner_.editorLostFocus();
    }

private:
    DataGrid& owner_;
    bool      numeric_ = false;
};

class DataGrid::ChoiceEditor final : public Dropdown {
public:
    explicit ChoiceEditor(DataGrid& owner) : owner_(owner) {}

protected:
    // Med listan ÖPPEN är pilarna listans (markören) och Enter/Space dess
    // val, som Enter också sparar och flyttar ner. STÄNGD är tangenterna
    // datagridens — Upp/Ned flyttar en rad i
    // stället för att byta värde, för en vallista i en tabell ska inte ändra
    // en cell av att man navigerar förbi den. Escape avbryter i ETT steg,
    // öppen eller stängd.
    bool keyDown(gfx::Key k, std::uint32_t mods) override
    {
        const bool shift = (mods & gfx::kModShift) != 0;
        if (annansGenväg(mods))
            return Dropdown::keyDown(k, mods);
        switch (k) {
        case gfx::Key::Escape:
            owner_.cancelEdit();
            return true;
        case gfx::Key::Tab:
            if (!owner_.saveEdit())
                return true;
            return owner_.tabFromUser(shift ? -1 : 1);
        case gfx::Key::Return:
        case gfx::Key::Space:
            if (isOpen()) {
                // Listans val: ett NYTT värde avslutar redan redigeringen
                // (`choicePicked`), samma värde avslutas här — om valets
                // handlers lät datagriden och oss leva. Enter flyttar sedan
                // ner en rad (Shift+Enter upp), som Enter i fältet; Space
                // står kvar.
                const HandlerGuard vakt(*this);
                Dropdown::keyDown(k, mods);
                if (!vakt.ok() || !owner_.saveEdit())
                    return true;
                if (k == gfx::Key::Return)
                    owner_.moveFromUser(shift ? -1 : 1, 0);
                return true;
            }
            if (k == gfx::Key::Space)
                break;   // öppnar listan igen — dropdownens egen väg
            if (owner_.saveEdit())
                owner_.moveFromUser(shift ? -1 : 1, 0);
            return true;
        case gfx::Key::Up:
        case gfx::Key::Down:
            if (isOpen())
                break;
            if (owner_.saveEdit())
                owner_.moveFromUser(k == gfx::Key::Up ? -1 : 1, 0);
            return true;
        default:
            break;
        }
        return Dropdown::keyDown(k, mods);
    }

    void focusLost() override
    {
        Dropdown::focusLost();   // stänger en öppen lista
        owner_.editorLostFocus();
    }

private:
    DataGrid& owner_;
};

// FÄRGCELLENS väljare — PropertyGrids form: en riktig `ColorPicker` över
// cellen med popupen öppen direkt. Med popupen STÄNGD är Enter, Escape, Tab
// och Upp/Ned datagridens (som fältets); öppen tar popupen tangenterna själv.
// Datagriden lyssnar bara på `onCommit`. En dold editor som behållit fokus
// öppnar ingen popup på Space.
class DataGrid::ColorEditor final : public ColorPicker {
public:
    explicit ColorEditor(DataGrid& owner) : owner_(owner) {}

protected:
    bool keyDown(gfx::Key k, std::uint32_t mods) override
    {
        if (!isVisible())
            return false;
        const bool shift = (mods & gfx::kModShift) != 0;
        if (!annansGenväg(mods) && !isOpen()) {
            switch (k) {
            case gfx::Key::Escape:
                owner_.cancelEdit();
                return true;
            case gfx::Key::Return:
                if (owner_.saveEdit())
                    owner_.moveFromUser(shift ? -1 : 1, 0);
                return true;
            case gfx::Key::Tab:
                if (!owner_.saveEdit())
                    return true;
                return owner_.tabFromUser(shift ? -1 : 1);
            case gfx::Key::Up:
            case gfx::Key::Down:
                if (owner_.saveEdit())
                    owner_.moveFromUser(k == gfx::Key::Up ? -1 : 1, 0);
                return true;
            default:
                break;
            }
        }
        return ColorPicker::keyDown(k, mods);
    }

    void focusLost() override
    {
        ColorPicker::focusLost();   // stänger en öppen popup
        if (isVisible())
            owner_.editorLostFocus();
    }

    // POPUPEN STÄNGDES: valet är redan sparat (`commitColor`), så
    // redigeringen avslutas — väljaren göms och fokus går tillbaka till
    // datagriden, där pilarna flyttar vidare direkt. En redigering som redan
    // slutat (stängningen kom ur `endEdit`) är orörd.
    void popupDismissed() override
    {
        if (isVisible())
            owner_.endEdit(true);
    }

private:
    DataGrid& owner_;
};

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("datagrid", DataGrid)

DataGrid::DataGrid() : sep_(tr(str::DecimalSeparator))
{
    text_ = add<TextEditor>(*this);
    text_->hide();
    choice_ = add<ChoiceEditor>(*this);
    choice_->hide();
    // Ett VAL av ett nytt värde i listan är en sparning — användarens
    // handling, som Enter i ett fält. Programmatisk sättning av listan
    // (`startEdit`) fyrar inte dropdownens onChange, så ingen eko-loop.
    choice_->onChange = [this] { choicePicked(); };
    color_ = add<ColorEditor>(*this);
    color_->hide();
    // Färgen: bara commiten skrivs till cellen — reglagens live-händelser
    // rör inte värdet (`commitColor`).
    color_->onCommit = [this] { commitColor(); };
}

// ------------------------------------------------- kolumnens redigerare

void DataGrid::setColumnEditor(int column, EditorSpec spec)
{
    if (column < 0) {
        gfx::log("DataGrid(%s): setColumnEditor(%d) — negativt index, "
                 "ignoreras", name.c_str(), column);
        return;
    }
    if (std::size_t(column) >= columnEdits_.size())
        columnEdits_.resize(std::size_t(column) + 1);
    columnEdits_[std::size_t(column)] = std::move(spec);
    // En ÖPPEN editor visar den gamla redigeraren — stängd är bättre
    // (Dropdowns `setItems`-prejudikat), och en väntande färgcommit beskriver
    // en annan cell.
    if (editColumn_ == column)
        endEdit(false);
    if (colorColumn_ == column)
        colorRow_ = -1;
    invalidateColumnWidths(column);
    // Sorteringsvärdet följer redigeraren (`cellSortValue`).
    if (column == sortColumn())
        invalidateOrder();
    invalidate();   // ytan, värdet och redigerbarheten ritas om
}

const DataGrid::EditorSpec& DataGrid::columnEditor(int column) const
{
    return column >= 0 && std::size_t(column) < columnEdits_.size()
               ? columnEdits_[std::size_t(column)]
               : standardSpec();
}

// ------------------------------------------------------------- cellerna

const DataGrid::CellSetting* DataGrid::setting(int row, int column) const
{
    if (row < 0 || std::size_t(row) >= cells_.size() || column < 0)
        return nullptr;
    const auto& rad = cells_[std::size_t(row)];
    return std::size_t(column) < rad.size() ? &rad[std::size_t(column)]
                                            : nullptr;
}

DataGrid::CellSetting* DataGrid::settingFor(int row, int column)
{
    if (std::size_t(row) >= cells_.size())
        cells_.resize(std::size_t(rowCount()));
    auto& rad = cells_[std::size_t(row)];
    if (std::size_t(column) >= rad.size())
        rad.resize(std::size_t(column) + 1);
    return &rad[std::size_t(column)];
}

const std::string* DataGrid::pendingFor(int row, int column) const
{
    const CellSetting* s = setting(row, column);
    return s && s->pending ? &*s->pending : nullptr;
}

// EN CELL SATTES utifrån — det väntande felet beskrev den gamla texten.
void DataGrid::cellSet(int row, int column)
{
    clearPending(row, column);
}

// KNAPPCELLEN bär sin bild i knappen.
bool DataGrid::cellDrawsOwnImage(int row, int column) const
{
    return rowKind(row) == RowKind::Data &&
           cellEditor(row, column).editor == Editor::Button;
}

void DataGrid::clearPending(int row, int column)
{
    const CellSetting* s = setting(row, column);
    if (!s || !s->pending)
        return;
    cells_[std::size_t(row)][std::size_t(column)].pending.reset();
    invalidateCellWidth(row, column);   // visningen byter text
    invalidate();
}

void DataGrid::setCellEditor(int row, int column, EditorSpec spec)
{
    if (row < 0 || row >= rowCount() || column < 0) {
        gfx::log("DataGrid(%s): setCellEditor(%d, %d) utanför tabellen (%d "
                 "rader) — ignoreras", name.c_str(), row, column, rowCount());
        return;
    }
    settingFor(row, column)->spec = std::move(spec);
    if (editRow_ == row && editColumn_ == column)
        endEdit(false);
    if (colorRow_ == row && colorColumn_ == column)
        colorRow_ = -1;
    invalidateCellWidth(row, column);
    if (column == sortColumn())
        invalidateOrder();   // sorteringsvärdet följer redigeraren
    invalidate();
}

void DataGrid::clearCellEditor(int row, int column)
{
    if (row < 0 || row >= rowCount() || column < 0) {
        gfx::log("DataGrid(%s): clearCellEditor(%d, %d) utanför tabellen (%d "
                 "rader) — ignoreras", name.c_str(), row, column, rowCount());
        return;
    }
    const CellSetting* s = setting(row, column);
    if (!s || !s->spec)
        return;
    cells_[std::size_t(row)][std::size_t(column)].spec.reset();
    if (editRow_ == row && editColumn_ == column)
        endEdit(false);
    if (colorRow_ == row && colorColumn_ == column)
        colorRow_ = -1;
    invalidateCellWidth(row, column);
    if (column == sortColumn())
        invalidateOrder();   // sorteringsvärdet följer redigeraren
    invalidate();
}

const DataGrid::EditorSpec& DataGrid::cellEditor(int row, int column) const
{
    const CellSetting* s = setting(row, column);
    return s && s->spec ? *s->spec : columnEditor(column);
}

void DataGrid::setCellEnabled(int row, int column, bool on)
{
    if (row < 0 || row >= rowCount() || column < 0) {
        gfx::log("DataGrid(%s): setCellEnabled(%d, %d) utanför tabellen (%d "
                 "rader) — ignoreras", name.c_str(), row, column, rowCount());
        return;
    }
    if (isCellEnabled(row, column) == on)
        return;
    settingFor(row, column)->enabled = on;
    if (!on) {
        // Cellen som redigeras eller trycks går inte längre att ändra.
        if (editRow_ == row && editColumn_ == column)
            endEdit(false);
        if (colorRow_ == row && colorColumn_ == column)
            colorRow_ = -1;
        if (pressRow_ == row && pressColumn_ == column)
            pressRow_ = pressColumn_ = -1;
    }
    invalidate();   // dämpad eller inte
}

bool DataGrid::isCellEnabled(int row, int column) const
{
    if (row < 0 || row >= rowCount() || column < 0)
        return false;
    const CellSetting* s = setting(row, column);
    return !s || s->enabled;
}

void DataGrid::setCellToolTip(int row, int column, TrText toolTip)
{
    if (row < 0 || row >= rowCount() || column < 0) {
        gfx::log("DataGrid(%s): setCellToolTip(%d, %d) utanför tabellen (%d "
                 "rader) — ignoreras", name.c_str(), row, column, rowCount());
        return;
    }
    settingFor(row, column)->toolTip = std::move(toolTip);
}

TrText DataGrid::cellToolTip(int row, int column) const
{
    const CellSetting* s = setting(row, column);
    return s ? s->toolTip : TrText{};
}

// LÄSVÄGARNA — ur cellens text och den redigerare som gäller, aldrig ur
// editorernas tillstånd.
bool DataGrid::isChecked(int row, int column) const
{
    return row >= 0 && row < rowCount() &&
           cellEditor(row, column).editor == Editor::Check &&
           valuecell::parseCheck(cell(row, column));
}

std::optional<gfx::Color> DataGrid::colorValue(int row, int column) const
{
    if (row < 0 || row >= rowCount())
        return std::nullopt;
    const EditorSpec& s = cellEditor(row, column);
    if (s.editor != Editor::Color)
        return std::nullopt;
    return valuecell::parseColor(cell(row, column), s.allowAlpha);
}

std::optional<double> DataGrid::numberValue(int row, int column) const
{
    if (row < 0 || row >= rowCount())
        return std::nullopt;
    const EditorSpec& s = cellEditor(row, column);
    if (s.editor != Editor::Number)
        return std::nullopt;
    return valuecell::numberValue(cell(row, column), numberSpec(s));
}

bool DataGrid::hasError(int row, int column) const
{
    if (row < 0 || row >= rowCount())
        return false;
    const EditorSpec& s = cellEditor(row, column);
    switch (s.editor) {
    case Editor::Number:
        return pendingFor(row, column) != nullptr ||
               valuecell::numberError(cell(row, column), numberSpec(s));
    case Editor::Color:
        return !valuecell::parseColor(cell(row, column), s.allowAlpha);
    default:
        return false;
    }
}

void DataGrid::setEditOnClick(bool on)
{
    editOnClick_ = on;
}

void DataGrid::setSelectOnEdit(bool on)
{
    selectOnEdit_ = on;
}

void DataGrid::setMultiEdit(bool on)
{
    multiEdit_ = on;
}

// Går cellen att ANVÄNDA nu? Det är frågan både beteendet och ritningen
// ställer (en inaktiv cell ritas dämpad).
bool DataGrid::cellActive(int row, int column) const
{
    return isEnabled() && row >= 0 && row < rowCount() && column >= 0 &&
           column < columnCount() && !(column == 0 && rowHeaders()) &&
           rowKind(row) == RowKind::Data && isCellEnabled(row, column);
}

// Går cellen att redigera? En val-cell utan val ser skrivskyddad ut — för
// det är den.
bool DataGrid::editable(int row, int column) const
{
    return cellActive(row, column) && skriverVärde(cellEditor(row, column));
}

bool DataGrid::pressable(int row, int column) const
{
    const EditorSpec& s = cellEditor(row, column);
    return cellActive(row, column) && s.editor == Editor::Button && !s.readOnly;
}

// Ytan följer REDIGERAREN och inte aktiveringen: en inaktiv cell är en
// skrivyta man inte når nu (dämpad), en skrivskyddad eller låst cell en
// läsyta — också en låst knapp.
bool DataGrid::readOnlySurface(int row, int column) const
{
    const EditorSpec& s = cellEditor(row, column);
    return rowKind(row) == RowKind::Data &&
           (s.readOnly || (s.editor != Editor::Button && !skriverVärde(s)));
}

void DataGrid::columnsReplaced()
{
    endEdit(false);
    currentColumn_ = -1;
    pressRow_ = pressColumn_ = -1;
    hoverRow_ = hoverColumn_ = -1;
}

void DataGrid::rowsReplaced()
{
    // `Table::setRows` har redan nollat den aktuella raden — kolumnen följer,
    // och radvalet nollas (listvyns `setRows`: en rad som inte finns kvar kan
    // inte vara vald). Cellernas inställningar beskrev de gamla raderna.
    ++rowsGen_;
    endEdit(false);
    currentColumn_ = -1;
    selected_.assign(std::size_t(rowCount()), 0);
    cells_.assign(std::size_t(rowCount()), {});
    anchor_      = -1;
    pendingSolo_ = -1;
    colorRow_    = -1;
    pressRow_ = pressColumn_ = -1;
    hoverRow_ = hoverColumn_ = -1;
}

void DataGrid::rowAppended()
{
    selected_.push_back(0);   // en ny rad är aldrig vald
    cells_.emplace_back();
}

// `setRow` är tilldelning: radens cellinställningar nollas, som
// cellfärgerna. Redigeringen följer raden — utom när cellens redigerare
// därmed blev en annan sort. Bar sorteringskolumnens cell en egen redigerare
// har dess sorteringsvärde ändrats, också med samma text.
void DataGrid::rowReplaced(int row)
{
    const CellSetting* sorterad = setting(row, sortColumn());
    if (sorterad && sorterad->spec)
        invalidateOrder();
    if (row >= 0 && std::size_t(row) < cells_.size()) {
        const std::size_t n = cells_[std::size_t(row)].size();
        cells_[std::size_t(row)].clear();
        for (std::size_t k = 0; k < n; ++k)
            invalidateCellWidth(row, int(k));
    }
    if (pendingSolo_ == row)
        pendingSolo_ = -1;
    if (pressRow_ == row)
        pressRow_ = pressColumn_ = -1;
    if (editRow_ == row && cellEditor(row, editColumn_).editor != editKind_)
        endEdit(false);
    if (colorRow_ == row &&
        cellEditor(row, colorColumn_).editor != Editor::Color)
        colorRow_ = -1;
}

// `removeRow`: radens val och cellinställningar försvinner med den, och varje
// radindex efter den räknas ned — redigeringen, färgväljarens cell, knappens
// tryck och hover, ankaret och det väntande solovalet följer sina rader. På den
// borttagna raden avbryts redigeringen utan events, och färgväljarens cell
// släpps FÖRST, så att popupens stängning inte committar till en annan rad.
// Radgenerationen stegas: ett postat event beskriver en rad i den gamla
// numreringen och slopas (`endEdit`), medan en öppen färgväljare på en annan
// rad följer med till den nya generationen. Radens poster tas bort FÖRST:
// redigeringens avslut flyttar fokus och döljer editorn, och det som då frågar
// tabellen (sorteringens `cellSortValue`) ska läsa cellinställningarna i den
// nya numreringen. En tom tabell har ingen aktuell kolumn (`setRows` form).
void DataGrid::rowRemoved(int row)
{
    ++rowsGen_;
    const auto at = std::ptrdiff_t(row);
    if (std::size_t(row) < selected_.size())
        selected_.erase(selected_.begin() + at);
    if (std::size_t(row) < cells_.size())
        cells_.erase(cells_.begin() + at);
    const auto följ = [row](int& r) {
        if (r == row)
            r = -1;
        else if (r > row)
            --r;
    };
    följ(colorRow_);
    if (colorRow_ >= 0)
        colorGen_ = rowsGen_;
    if (editRow_ == row)
        endEdit(false);
    else if (editRow_ > row)
        --editRow_;
    if (pressRow_ == row)
        pressColumn_ = -1;
    följ(pressRow_);
    if (hoverRow_ == row)
        hoverColumn_ = -1;
    följ(hoverRow_);
    följ(anchor_);
    följ(pendingSolo_);
    if (rowCount() == 0)
        currentColumn_ = -1;
    invalidate();
}

// En rad som INTE längre är en synlig datarad släpper valet, en redigering,
// ett tryck och ett väntande soloval — utan events.
void DataGrid::rowKindChanged(int row)
{
    if (row < 0 || row >= rowCount() || rowKind(row) == RowKind::Data)
        return;
    rowsHidden({row});
}

void DataGrid::rowsHidden(const std::vector<int>& rows)
{
    for (const int r : rows) {
        if (r < 0 || r >= rowCount())
            continue;
        if (std::size_t(r) < selected_.size() && selected_[std::size_t(r)]) {
            selected_[std::size_t(r)] = 0;
            hiddenDeselected_         = true;   // användarens fällning fyrar
            invalidate();
        }
        if (editRow_ == r)
            endEdit(false);
        if (colorRow_ == r)
            colorRow_ = -1;
        if (pressRow_ == r)
            pressRow_ = pressColumn_ = -1;
        if (pendingSolo_ == r)
            pendingSolo_ = -1;
    }
}

// ------------------------------------------------------ radvalet

// Bara SYNLIGA DATArader kan vara valda — en kategori- eller tomrad aldrig,
// och inte en rad i en ihopfälld kategori.
bool DataGrid::assignSelection(const std::vector<int>& rows)
{
    std::vector<char> nytt(std::size_t(rowCount()), 0);
    for (int r : rows)
        if (r >= 0 && r < rowCount() && rowKind(r) == RowKind::Data &&
            displayRow(r) >= 0)
            nytt[std::size_t(r)] = 1;
    if (nytt == selected_)
        return false;
    selected_ = std::move(nytt);
    invalidate();
    return true;
}

void DataGrid::setSelectionMode(SelectionMode m)
{
    if (selectionMode_ == m)
        return;
    selectionMode_ = m;
    invalidate();   // raderna eller cellen markeras
}

void DataGrid::setMultiSelect(bool on)
{
    if (multiSelect_ == on)
        return;
    multiSelect_ = on;
    // Av med flera valda: bara EN står kvar — den aktuella raden om den är
    // vald, annars den första (listvyns regel). Programmatiskt, inget event.
    if (!on) {
        int kvar = -1;
        if (currentRow() >= 0 && std::size_t(currentRow()) < selected_.size() &&
            selected_[std::size_t(currentRow())])
            kvar = currentRow();
        for (int p = 0; kvar < 0 && p < positionCount(); ++p)
            if (selected_[std::size_t(rowAtDisplay(p))])
                kvar = rowAtDisplay(p);
        assignSelection(kvar >= 0 ? std::vector<int>{kvar}
                                  : std::vector<int>{});
    }
    invalidate();
}

void DataGrid::setSelected(int row, bool on)
{
    if (row < 0 || row >= rowCount()) {
        gfx::log("DataGrid(%s): setSelected(%d) utanför intervallet (%d "
                 "rader) — ignoreras", name.c_str(), row, rowCount());
        return;
    }
    if (on && (rowKind(row) != RowKind::Data || displayRow(row) < 0)) {
        gfx::log("DataGrid(%s): setSelected(%d) — raden är ingen synlig "
                 "datarad, ignoreras", name.c_str(), row);
        return;
    }
    if (on && !multiSelect_) {
        assignSelection({row});   // enkelval: den här och ingen annan
        return;
    }
    if (bool(selected_[std::size_t(row)]) == on)
        return;
    selected_[std::size_t(row)] = on ? 1 : 0;
    invalidate();
}

// EFFEKTIVT: bara i radläget är en rad vald (WinForms `SelectedRows` är tom i
// cellvalet) — det lagrade står kvar, så satsordningen inte avgör.
bool DataGrid::isSelected(int row) const
{
    return selectionMode_ == SelectionMode::FullRow && row >= 0 &&
           std::size_t(row) < selected_.size() && selected_[std::size_t(row)];
}

std::vector<int> DataGrid::selectedRows() const
{
    std::vector<int> ut;
    for (int p = 0; p < positionCount(); ++p)
        if (isSelected(rowAtDisplay(p)))
            ut.push_back(rowAtDisplay(p));
    return ut;
}

void DataGrid::selectAll()
{
    if (!multiSelect_)
        return;   // enkelval har ingen "alla"
    std::vector<int> alla;
    for (int p = 0; p < positionCount(); ++p)
        alla.push_back(rowAtDisplay(p));
    assignSelection(alla);
}

void DataGrid::clearSelection()
{
    assignSelection({});
}

// ANVÄNDARVÄGEN — `ListView::selectFromUser` i datagridens form: raden har
// redan gjorts aktuell av anroparen, spannet räknas i VISNINGSORDNINGEN från
// ankaret (bara de synliga dataraderna), och händelsen fyras bara när valet
// ändrades. En kategorirad väljs aldrig.
bool DataGrid::selectRowFromUser(int row, std::uint32_t mods, bool fromKey)
{
    if (selectionMode_ != SelectionMode::FullRow || row < 0 ||
        row >= rowCount() || rowKind(row) != RowKind::Data)
        return true;
    const bool shift = multiSelect_ && (mods & gfx::kModShift);
    const bool vänd  = multiSelect_ && !fromKey &&
                      (mods & (gfx::kModCtrl | gfx::kModCmd));
    bool ändrat = false;
    if (shift) {
        const int ankare = anchor_ >= 0 && anchor_ < rowCount() &&
                                   displayRow(anchor_) >= 0
                               ? anchor_
                               : row;
        const int a = std::min(displayRow(ankare), displayRow(row));
        const int b = std::max(displayRow(ankare), displayRow(row));
        std::vector<int> rader =
            vänd ? selectedRows() : std::vector<int>{};
        for (int p = a; p <= b; ++p)
            rader.push_back(rowAtDisplay(p));
        ändrat  = assignSelection(rader);
        anchor_ = ankare;
    } else if (vänd) {
        selected_[std::size_t(row)] = selected_[std::size_t(row)] ? 0 : 1;
        ändrat  = true;
        anchor_ = row;
        invalidate();
    } else {
        ändrat  = assignSelection({row});
        anchor_ = row;
    }
    if (!ändrat)
        return true;
    return fireEvent(onSelectionChange);   // valet läses ur sender
}

// ANVÄNDARENS FÄLLNING: tabellens väg (`onExpand`/`onCollapse`), och när en
// ihopfällning valde bort dolda rader (`rowsHidden` sätter flaggan) i
// radläget `onSelectionChange` efter den — listvyns regel. Svarar som
// `fireEvent`.
bool DataGrid::toggleCategory(int row)
{
    hiddenDeselected_ = false;
    if (!toggleCategoryFromUser(row))
        return false;
    const bool valdesBort = hiddenDeselected_;
    hiddenDeselected_     = false;
    if (!valdesBort || selectionMode_ != SelectionMode::FullRow)
        return true;
    return fireEvent(onSelectionChange);
}

bool DataGrid::setProp(const std::string& key, const std::string& value)
{
    // Enumets namn GEMENA (`parseEnumTail`s form) — ett annat namn är ett
    // okänt värde och nyckeln svarar false, som Labels `align`.
    if (key == "selectionMode") {
        if (value == "cell")         setSelectionMode(SelectionMode::Cell);
        else if (value == "fullrow") setSelectionMode(SelectionMode::FullRow);
        else return false;
        return true;
    }
    if (key == "multiSelect") {
        setMultiSelect(propparse::parseBool(value));
        return true;
    }
    if (key == "editOnClick") {
        setEditOnClick(propparse::parseBool(value));
        return true;
    }
    if (key == "selectOnEdit") {
        setSelectOnEdit(propparse::parseBool(value));
        return true;
    }
    if (key == "multiEdit") {
        setMultiEdit(propparse::parseBool(value));
        return true;
    }
    return Table::setProp(key, value);   // basen SIST — konventionen
}

bool DataGrid::canReleaseChild(const Widget& child) const
{
    return &child != static_cast<const Widget*>(text_) &&
           &child != static_cast<const Widget*>(choice_) &&
           &child != static_cast<const Widget*>(color_);
}

// TOOLTIPEN PER CELL — felet vinner, sedan cellens egen, sedan rubrikens och
// datagridens (basens).
TrText DataGrid::toolTipAt(const Ctx& c, float x, float y) const
{
    const int p = hitPosition(c, x, y);
    if (p >= 0) {
        const int r = rowAtDisplay(p);
        const int k = columnAt(c, x);
        if (r >= 0 && k >= 0 && rowKind(r) == RowKind::Data) {
            if (hasError(r, k))
                return valuecell::invalidToolTip();
            const CellSetting* s = setting(r, k);
            if (s && !s->toolTip.empty())
                return s->toolTip;
        }
    }
    return Table::toolTipAt(c, x, y);
}

// ---------------------------------------------------------- aktuell cell

void DataGrid::setCurrentCell(int row, int column)
{
    const bool ingen = row == -1 && column == -1;
    if (!ingen && (row < 0 || row >= rowCount() || column < 0 ||
                   column >= columnCount())) {
        gfx::log("DataGrid(%s): setCurrentCell(%d, %d) utanför tabellen (%d × "
                 "%d) — ignoreras", name.c_str(), row, column, rowCount(),
                 columnCount());
        return;
    }
    if (!ingen && rowKind(row) == RowKind::Blank) {
        gfx::log("DataGrid(%s): setCurrentCell(%d, %d) — en tom rad är aldrig "
                 "aktuell, ignoreras", name.c_str(), row, column);
        return;
    }
    // En rad i en ihopfälld kategori har ingen plats: editorn hade inte synts
    // men ändå tagit tangenterna.
    if (!ingen && displayRow(row) < 0) {
        gfx::log("DataGrid(%s): setCurrentCell(%d, %d) — raden är dold i en "
                 "ihopfälld kategori, ignoreras", name.c_str(), row, column);
        return;
    }
    // Programmatiskt: en pågående redigering AVBRYTS — att spara den hade
    // fyrat `onChange`, och en sättare fyrar aldrig.
    endEdit(false);
    setCurrent(row);
    // Radrubriken är aldrig den aktuella cellen.
    const int kol = ingen ? column : std::max(column, firstColumn());
    if (currentColumn_ != kol) {
        currentColumn_ = kol;
        invalidate();
    }
}

// Första kolumnen som kan vara aktuell cell: 1 när radrubriken tar
// kolumn 0 och det finns en kolumn till, annars 0.
int DataGrid::firstColumn() const
{
    return rowHeaders() && columnCount() > 1 ? 1 : 0;
}

int DataGrid::landing(int position, int dir) const
{
    const int n = positionCount();
    if (n == 0)
        return -1;
    const int  d     = dir < 0 ? -1 : 1;
    const int  start = std::clamp(position, 0, n - 1);
    const auto tom   = [this](int p) {
        return rowKind(rowAtDisplay(p)) == RowKind::Blank;
    };
    for (int p = start; p >= 0 && p < n; p += d)
        if (!tom(p))
            return p;
    for (int p = start - d; p >= 0 && p < n; p -= d)
        if (!tom(p))
            return p;
    return -1;
}

// Märket i radrubrikens remsa: remsan tonas aldrig av radvalet, men
// Minimalistics valstreck — valets enda yta i den familjen — står kvar vid
// radens vänsterkant (receptet står i `draw`).
void DataGrid::drawRowHeaderMarks(const Ctx& c, int position, int row,
                                  gfx::Color) const
{
    if (c.ui != UI::Minimalistic || !isSelected(row))
        return;
    const gfx::Rectf r   = rowRect(c, position);
    const auto&      col = c.theme->colors;
    Draw::rect(c, {r.x, r.y, c.style->rowMarkerW, r.h}, 0.0f,
               isEnabled() ? col.accent : col.textDim);
}

bool DataGrid::setCurrentPosition(int position, int column, std::uint32_t mods,
                                  bool fromKey)
{
    const int n  = positionCount();
    const int kn = columnCount();
    if (n == 0 || kn == 0 || position < 0)
        return true;
    const int rad = rowAtDisplay(std::clamp(position, 0, n - 1));
    if (rad < 0 || rowKind(rad) == RowKind::Blank)
        return true;   // en tom rad är aldrig aktuell
    const int kol = std::clamp(column, firstColumn(), kn - 1);
    setCurrent(rad);
    if (currentColumn_ != kol) {
        currentColumn_ = kol;
        invalidate();
    }
    ensureVisible(rad);
    ensureColumnVisible(kol);
    // RADVALET följer den aktuella raden — sist, för en handler
    // får göra vad som helst; anroparna rör inte datagriden efteråt.
    return selectRowFromUser(rad, mods, fromKey);
}

// En rad i taget i höjdled, förbi de tomma raderna; i kanten står cellen kvar.
void DataGrid::moveFromUser(int dRow, int dColumn)
{
    const int n = positionCount();
    if (n == 0 || columnCount() == 0)
        return;
    const int pos = displayRow(currentRow());
    // Utan en hel aktuell cell landar förflyttningen i den del som finns —
    // eller i första cellen.
    if (pos < 0 || currentColumn_ < 0) {
        setCurrentPosition(landing(std::max(pos, 0), 1),
                           std::max(currentColumn_, 0));
        return;
    }
    int mål = pos;
    if (dRow != 0) {
        const int d = dRow < 0 ? -1 : 1;
        for (int p = std::clamp(pos + dRow, 0, n - 1); p >= 0 && p < n; p += d)
            if (rowKind(rowAtDisplay(p)) != RowKind::Blank) {
                mål = p;
                break;
            }
    }
    setCurrentPosition(mål, currentColumn_ + dColumn);
}

// Tab till nästa REDIGERBARA och aktiva cell i visningsordningen, med
// radbyte i kanten — kategori-, tom- och knappceller hoppas över. Finns
// ingen sådan cell kvar svarar den false, så att Tab lämnar datagriden
// (AppWindow går då vidare i fokusordningen).
bool DataGrid::tabFromUser(int dir)
{
    const int n  = positionCount();
    const int kn = columnCount();
    if (n == 0 || kn == 0)
        return false;
    const int d      = dir < 0 ? -1 : 1;
    const int första = firstColumn();
    int       pos    = displayRow(currentRow());
    int       kol    = currentColumn_;
    if (pos < 0 || kol < 0) {
        // Utan aktuell cell börjar sökningen före den första (efter den sista).
        pos = d > 0 ? 0 : n - 1;
        kol = d > 0 ? första - 1 : kn;
    }
    for (;;) {
        kol += d;
        if (kol >= kn) {
            kol = första;
            ++pos;
        } else if (kol < första) {
            kol = kn - 1;
            --pos;
        }
        if (pos < 0 || pos >= n)
            return false;
        if (editable(rowAtDisplay(pos), kol)) {
            setCurrentPosition(pos, kol);
            return true;
        }
    }
}

// ----------------------------------------------------------- redigeringen

void DataGrid::beginEdit()
{
    startEdit(nullptr);
}

void DataGrid::beginEditWith(const std::string& start)
{
    startEdit(&start);
}

// Redigeraren öppnas på den aktuella cellen. Ett kryss och en knapp har
// ingen editor — de ändras direkt (`toggleCheckFromUser`,
// `clickCellFromUser`). En rad utan plats (dold) redigeras inte.
void DataGrid::startEdit(const std::string* start)
{
    const int r = currentRow();
    const int k = currentColumn_;
    if (editRow_ >= 0 || !editable(r, k) || displayRow(r) < 0 ||
        !appWindow() || !appWindow()->isOpen())
        return;
    const EditorSpec& s = cellEditor(r, k);
    if (s.editor == Editor::Check)
        return;
    if (start && s.editor != Editor::Text && s.editor != Editor::Number)
        return;   // en val- eller färgcell har inget att skriva i
    editRow_    = r;
    editColumn_ = k;
    editKind_   = s.editor;
    ensureVisible(r);
    ensureColumnVisible(k);
    // Editorn står i KOLUMNENS justering — en centrerad eller
    // högerjusterad cell ska inte hoppa till vänster när den börjar
    // redigeras. Editorerna bär `align` med samma namn (regel 6).
    const Align just = columns()[std::size_t(k)].align;

    switch (s.editor) {
    case Editor::Choice: {
        const auto& val = s.choices;
        choice_->setAlign(just);
        if (s.choiceIcons.empty()) {
            choice_->setItems(val);
        } else {
            choice_->setItems({});
            for (std::size_t i = 0; i < val.size(); ++i)
                choice_->addItem(val[i], i < s.choiceIcons.size()
                                             ? s.choiceIcons[i]
                                             : ImageSource{});
        }
        const auto it = std::find(val.begin(), val.end(), cell(r, k));
        choice_->setSelectedIndex(it == val.end() ? -1
                                                  : int(it - val.begin()));
        choice_->show();
        choice_->focus();
        choice_->open();   // klicket/F2 ÄR intentionen — listan direkt
        break;
    }
    case Editor::Color:
        color_->setAllowAlpha(s.allowAlpha);
        color_->setColor(colorValue(r, k).value_or(gfx::Color{}));
        colorRow_    = r;
        colorColumn_ = k;
        colorGen_    = rowsGen_;
        color_->show();
        color_->focus();
        color_->open();    // klicket ÄR intentionen — popupen direkt
        break;
    case Editor::Number:
        text_->setAlign(just);
        text_->börja(start ? std::string()
                           : valuecell::editNumberText(cell(r, k),
                                                       pendingFor(r, k),
                                                       numberSpec(s),
                                                       sep_.str()),
                     true);
        text_->show();
        text_->focus();
        if (start)
            text_->skriv(start->c_str());
        else
            text_->markera(selectOnEdit_);
        break;
    default:
        text_->setAlign(just);
        text_->börja(start ? std::string() : cell(r, k), false);
        text_->show();
        text_->focus();
        if (start)
            text_->skriv(start->c_str());
        else
            text_->markera(selectOnEdit_);
        break;
    }
    invalidate();
}

// `Widget::commitEdit` — `AppWindow::commitEdits()` når också en öppen
// färgväljare: det skrivna i dess hexfält committas, och färgen den visar
// sparas, innan redigeringen avslutas.
void DataGrid::commitEdit()
{
    if (editRow_ >= 0 && editKind_ == Editor::Color) {
        const HandlerGuard vakt(*this);
        color_->commitEdit();
        if (!vakt.ok())
            return;
        commitColor();
        if (!vakt.ok())
            return;
    }
    endEdit(true);
}

// Escape: texten kastas, och ett väntande fel i cellen tas bort.
void DataGrid::cancelEdit()
{
    const int r = editRow_;
    const int k = editColumn_;
    endEdit(false);
    if (r >= 0)
        clearPending(r, k);
}

std::vector<int> DataGrid::editTargets(int row, int column) const
{
    if (!multiEdit_ || selectionMode_ != SelectionMode::FullRow ||
        !multiSelect_ || !isSelected(row))
        return {row};
    const Editor     sort = cellEditor(row, column).editor;
    std::vector<int> ut;
    for (const int t : selectedRows())
        if (t == row || (editable(t, column) &&
                         cellEditor(t, column).editor == sort))
            ut.push_back(t);
    return ut;
}

// AVSLUTET — med eller utan sparning. ORDNINGEN är bärande:
//   1. tillståndet nollas FÖRST, så editorns `focusLost` (som fokusbytet i
//      punkt 2 utlöser) ser att ingen redigering pågår och inte avslutar igen;
//   2. datagriden tar fokus INNAN editorn göms — en dold widget med fokus
//      hade lämnat tangenterna i tomma luften. Kommer vi hit UR en
//      `focusLost` har fokus redan lämnat editorn, och då rörs det inte;
//   3. cellerna skrivs och `onChange` fyras SIST, när allt är städat: en
//      handler får göra vad som helst, även bygga om tabellen. Ur en
//      `focusLost` POSTAS eventet i stället (data_grid.h).
// En val-cell vars text inte fanns bland valen, och där inget valts, bär
// inget värde — den sparar ingenting. En färgcell sparas vid väljarens commit
// (`commitColor`), inte här. Ett OTOLKBART tal sparar ingenting heller: den
// skrivna texten blir cellens väntande FEL. Svarar om datagriden går att röra
// efteråt (en direkt avfyrning kör handlers som får riva den); editorerna
// frågar svaret innan de flyttar den aktuella cellen.
bool DataGrid::endEdit(bool spara, bool postad)
{
    if (editRow_ < 0)
        return true;
    const int         r      = editRow_;
    const int         k      = editColumn_;
    const Editor      sort   = editKind_;
    const bool        harVal = choice_->selectedIndex() >= 0;
    const std::string ny     = sort == Editor::Choice ? choice_->selectedText()
                                                      : text_->text();
    editRow_    = -1;
    editColumn_ = -1;
    if (text_->focused() || choice_->focused() || color_->focused())
        focus();
    choice_->close();
    color_->close();
    text_->hide();
    choice_->hide();
    color_->hide();
    invalidate();
    if (!spara || sort == Editor::Color || r >= rowCount() ||
        (sort == Editor::Choice && !harVal))
        return true;

    std::vector<int> ändrade;
    if (sort == Editor::Number) {
        const valuecell::NumberCommit svar =
            valuecell::commitNumber(ny, numberSpec(cellEditor(r, k)));
        if (svar.result == valuecell::Commit::Invalid) {
            settingFor(r, k)->pending = ny;
            invalidateCellWidth(r, k);
            return true;
        }
        // Varje rad med SIN EGEN cells regler — klampningen och avrundningen
        // kan skilja.
        for (const int t : editTargets(r, k)) {
            const valuecell::NumberSpec ns = numberSpec(cellEditor(t, k));
            const valuecell::NumberCommit egen =
                t == r ? svar : valuecell::commitNumber(ny, ns);
            if (egen.result == valuecell::Commit::Invalid)
                continue;
            clearPending(t, k);
            const bool samma =
                egen.result == valuecell::Commit::Empty
                    ? valuecell::isEmpty(cell(t, k), ns)
                    : valuecell::sameNumber(cell(t, k), egen.canonical, ns);
            if (samma)
                continue;
            setCell(t, k, egen.canonical);
            ändrade.push_back(t);
        }
    } else {
        for (const int t : editTargets(r, k)) {
            if (sort == Editor::Choice) {
                const auto& val = cellEditor(t, k).choices;
                if (std::find(val.begin(), val.end(), ny) == val.end())
                    continue;   // värdet finns inte bland den cellens val
            }
            if (ny == cell(t, k))
                continue;
            setCell(t, k, ny);
            ändrade.push_back(t);
        }
    }
    if (ändrade.empty())
        return true;
    if (!postad)
        return fireCellChange(r, k, std::move(ändrade));
    // POSTAD avfyrning (`postEvent` — basens vakt) som SLOPAS om raderna
    // bytts (`setRows`) i samma dispatch: då beskriver (r, k) inte längre
    // cellen som sparades.
    const unsigned gen = rowsGen_;
    postEvent([this, gen, r, k, rader = std::move(ändrade)] {
        if (gen != rowsGen_)
            return;
        fireCellChange(r, k, rader);
    });
    return true;
}

// Editorernas sparning — `commitEdit`s väg, med svaret kvar.
bool DataGrid::saveEdit()
{
    return endEdit(true);
}

// FOKUS LÄMNADE EDITORN — ett klick på en annan widget eller i datagriden
// själv. Texten SPARAS (WinForms och Excel gör likadant), men vi står mitt
// i `AppWindow::setFocus`, och fokuskrokarna får aldrig köra användarkod
// (widget.h) — så eventet postas.
void DataGrid::editorLostFocus()
{
    endEdit(true, true);
}

// Listan valde ett NYTT värde (dropdownens `onChange` — samma värde fyrar
// inget). Vi står i dropdownens commit, i händelseflödet: sparningen och
// eventet är synkrona, som Enter.
void DataGrid::choicePicked()
{
    if (editRow_ >= 0 && choice_->isVisible())
        endEdit(true);
}

// FÄRGVÄLJARENS commit → cellen (och de valda raderna). Generationsvakten:
// en commit som kommer senare (hexfältet, stängningen) gäller bara om
// raderna är desamma som när popupen öppnades och cellen fortfarande är en
// aktiv färgcell. En effektivt oförändrad färg (`#3366ff` mot `#3366FF`)
// skriver och fyrar ingenting.
void DataGrid::commitColor()
{
    const int r = colorRow_;
    const int k = colorColumn_;
    if (r < 0 || r >= rowCount() || colorGen_ != rowsGen_ ||
        cellEditor(r, k).editor != Editor::Color || !cellActive(r, k))
        return;
    const gfx::Color nu = color_->color();
    std::vector<int> ändrade;
    for (const int t : editTargets(r, k)) {
        const bool        alfa  = cellEditor(t, k).allowAlpha;
        const std::string text  = valuecell::colorText(nu, alfa);
        const auto        förut = colorValue(t, k);
        const auto        efter = valuecell::parseColor(text, alfa);
        if (förut && efter && *förut == *efter)
            continue;
        setCell(t, k, text);
        ändrade.push_back(t);
    }
    if (!ändrade.empty())
        fireCellChange(r, k, std::move(ändrade));
}

// KRYSSET växlar och sparar direkt — klicket och Space. Alla valda rader med
// en aktiv krysscell får SAMMA nya värde (den klickade cellens motsats). En
// rad utan plats (dold) växlas inte.
bool DataGrid::toggleCheckFromUser(int row, int column)
{
    if (!editable(row, column) || displayRow(row) < 0 ||
        cellEditor(row, column).editor != Editor::Check)
        return true;
    const bool       ny = !isChecked(row, column);
    std::vector<int> ändrade;
    for (const int t : editTargets(row, column)) {
        if (isChecked(t, column) == ny)
            continue;
        setCell(t, column, valuecell::checkText(ny));
        ändrade.push_back(t);
    }
    if (ändrade.empty())
        return true;
    return fireCellChange(row, column, std::move(ändrade));
}

// KNAPPCELLEN trycktes — `onCellClick`, värdet ändras aldrig.
bool DataGrid::clickCellFromUser(int row, int column)
{
    if (!pressable(row, column) || displayRow(row) < 0)
        return true;
    CellClickArgs a;
    a.row    = row;
    a.column = column;
    return fireEvent(onCellClick, a);
}

bool DataGrid::activateCell(bool space, bool& alive)
{
    alive       = true;
    const int r = currentRow();
    const int k = currentColumn_;
    if (r < 0 || r >= rowCount() || displayRow(r) < 0)
        return false;   // ingen rad, eller en utan plats
    if (rowKind(r) == RowKind::Category) {
        alive = toggleCategory(r);
        return true;
    }
    switch (cellEditor(r, k).editor) {
    case Editor::Button:
        if (!pressable(r, k))
            return false;
        alive = clickCellFromUser(r, k);
        return true;
    case Editor::Check:
        if (!space || !editable(r, k))
            return false;
        alive = toggleCheckFromUser(r, k);
        return true;
    case Editor::Choice:
    case Editor::Color:
        startEdit(nullptr);
        return isEditing();
    case Editor::Text:
    case Editor::Number:
        if (space)
            return false;   // Space är ett tecken — skrivandet börjar
        startEdit(nullptr);
        return isEditing();
    case Editor::None:
        return false;
    }
    return false;
}

// `onChange` och sedan `onCommit` — en sparad cell är en avslutad
// handling, så de två kommer alltid tillsammans och med samma args: cellen
// och de ändrade raderna står i args, så en onChange-handler som bygger om
// tabellen kan inte flytta dem. Vakten emellan (`Widget::firePair`) — och
// svaret säger om datagriden går att röra efteråt.
bool DataGrid::fireCellChange(int row, int column, std::vector<int> rows)
{
    CellChangeArgs a;
    a.row    = row;
    a.column = column;
    a.rows   = std::move(rows);
    return firePair(onChange, onCommit, a);   // värdet: cell(e.row, e.column)
}

void DataGrid::enabledChanged()
{
    if (!isEnabled()) {
        endEdit(false);   // en inaktiv datagrid redigeras inte
        pressRow_ = pressColumn_ = -1;
    }
}

// --------------------------------------------------------- rubrikkrysset

// Läget över kolumnens AKTIVA, olåsta kryssceller i de SYNLIGA dataraderna — räknat
// en gång per ändring av datagriden (skadestämpeln), inte per ritning.
std::optional<CheckState> DataGrid::headerCheckState(int column) const
{
    if (column < 0 || column >= columnCount() ||
        columnEditor(column).editor != Editor::Check)
        return std::nullopt;
    const std::uint64_t stämpel = damageStamp();
    if (!headerChecks_.valid || headerChecks_.stamp != stämpel ||
        headerChecks_.state.size() != std::size_t(columnCount())) {
        headerChecks_.state.assign(std::size_t(columnCount()), std::nullopt);
        for (int k = 0; k < columnCount(); ++k) {
            if (columnEditor(k).editor != Editor::Check)
                continue;
            int alla = 0;
            int på   = 0;
            for (int p = 0; p < positionCount(); ++p) {
                const int r = rowAtDisplay(p);
                if (rowKind(r) != RowKind::Data || (k == 0 && rowHeaders()) ||
                    !isCellEnabled(r, k) ||
                    cellEditor(r, k).editor != Editor::Check ||
                    cellEditor(r, k).readOnly)
                    continue;
                ++alla;
                if (isChecked(r, k))
                    ++på;
            }
            headerChecks_.state[std::size_t(k)] =
                på == 0 ? CheckState::Unchecked
                : på == alla ? CheckState::Checked
                             : CheckState::Indeterminate;
        }
        headerChecks_.stamp = stämpel;
        headerChecks_.valid = true;
    }
    return headerChecks_.state[std::size_t(column)];
}

// En VÄNDNING: en pågående redigering sparas först, sedan får alla aktiva
// kryssceller i de synliga dataraderna samma värde — kryssade om inte alla
// redan var det — och ETT event fyras, med `row` −1 och de ändrade i `rows`.
// Raderna samlas FÖRST och skrivs sedan: är kolumnen sorteringskolumnen
// sorterar varje `setCell` om, och en loop över platserna hade då hoppat
// över rader.
bool DataGrid::headerCheckToggled(int column)
{
    if (isEditing() && !endEdit(true))
        return false;
    if (!isEnabled() || column < 0 || column >= columnCount() ||
        columnEditor(column).editor != Editor::Check)
        return true;
    const bool ny = headerCheckState(column) != CheckState::Checked;
    std::vector<int> ändrade;
    for (int p = 0; p < positionCount(); ++p) {
        const int r = rowAtDisplay(p);
        if (editable(r, column) &&
            cellEditor(r, column).editor == Editor::Check &&
            isChecked(r, column) != ny)
            ändrade.push_back(r);
    }
    for (const int r : ändrade)
        setCell(r, column, valuecell::checkText(ny));
    if (ändrade.empty())
        return true;
    return fireCellChange(-1, column, std::move(ändrade));
}

// ------------------------------------------------------- tangenter, mus

bool DataGrid::keyDown(gfx::Key k, std::uint32_t mods)
{
    if (mods & gfx::kModAlt)
        return false;   // Alt är menyernas och appens
    if (positionCount() == 0 || columnCount() == 0)
        return false;
    const bool styr     = (mods & (gfx::kModCtrl | gfx::kModCmd)) != 0;
    const bool shift    = (mods & gfx::kModShift) != 0;
    const int  pos      = displayRow(currentRow());
    const int  r        = currentRow();
    const bool kategori = r >= 0 && rowKind(r) == RowKind::Category;

    switch (k) {
    case gfx::Key::F2:
        if (styr || shift)
            return false;
        beginEdit();
        return isEditing();   // en skrivskyddad cell släpper tangenten
    case gfx::Key::Return: {
        if (styr)
            return false;
        // Enter ÖPPNAR redigeraren, klickar en knapp och fäller en
        // kategori; i ett kryss, en skrivskyddad eller inaktiv cell flyttar
        // den ner (Shift+Enter upp).
        bool lever = true;
        if (!shift && activateCell(false, lever))
            return true;
        moveFromUser(shift ? -1 : 1, 0);
        return true;
    }
    case gfx::Key::Space: {
        if (styr || shift)
            return false;
        bool lever = true;
        return activateCell(true, lever);
    }
    case gfx::Key::Tab:
        if (styr)
            return false;   // Ctrl+Tab lämnar datagriden (WinForms)
        return tabFromUser(shift ? -1 : 1);
    case gfx::Key::Left:
    case gfx::Key::Right:
        if (kategori) {
            // På en kategorirad fäller Vänster ihop och Höger ut.
            if (isExpanded(r) != (k == gfx::Key::Right))
                toggleCategory(r);
            return true;
        }
        moveFromUser(0, k == gfx::Key::Left ? -1 : 1);
        return true;
    case gfx::Key::Home:
        setCurrentPosition(styr ? landing(0, 1) : std::max(pos, 0), 0,
                           mods & gfx::kModShift);
        return true;
    case gfx::Key::End:
        setCurrentPosition(styr ? landing(positionCount() - 1, -1)
                                : std::max(pos, 0),
                           columnCount() - 1, mods & gfx::kModShift);
        return true;
    case gfx::Key::A:
        // Ctrl+A väljer alla synliga datarader (radläget med flerval) — en
        // användarväg: händelsen bara när valet ändrades. Annars bubblar den.
        if (!styr || selectionMode_ != SelectionMode::FullRow || !multiSelect_)
            return false;
        {
            std::vector<int> alla;
            for (int p = 0; p < positionCount(); ++p)
                alla.push_back(rowAtDisplay(p));
            if (assignSelection(alla))
                fireEvent(onSelectionChange);
        }
        return true;
    default:
        break;
    }
    if (!appWindow() || !appWindow()->isOpen())
        return false;
    const int mål = navigationTarget(behaviorCtx(), k, pos);
    if (mål < 0)
        return false;
    // Shift utökar radvalet längs navigeringen (radläget med flerval).
    setCurrentPosition(mål, std::max(currentColumn_, 0), mods & gfx::kModShift);
    return true;
}

// ATT BÖRJA SKRIVA börjar redigera — tecknet ersätter cellens text, som i
// ett kalkylark. En fritext-cell tar varje tecken, en talcell bara taltecken;
// de andra har inget att skriva i.
bool DataGrid::textInput(const char* utf8)
{
    const Editor sort = cellEditor(currentRow(), currentColumn_).editor;
    if (sort == Editor::Number && numbertext::filterNumberChars(utf8).empty())
        return false;
    if (sort != Editor::Text && sort != Editor::Number)
        return false;
    beginEditWith(utf8);
    return isEditing();
}

// TRYCKET, i ordningen: en öppen färgpopup (trycket stänger bara den) →
// huvudet och remsorna → en pågående redigering (sparas först) → en
// kategorirad (fälls) → den aktuella cellen och radvalet → cellens handling.
// Utan modifierare: ett kryss växlar, en färg- eller valcell öppnar sin
// redigerare, en knapp trycks ned (klicket kommer vid släppet), text och tal
// börjar redigera bara med `editOnClick`. Ctrl/Shift väljer bara.
void DataGrid::mouseDown(const Ctx& c, float mx, float my)
{
    pendingSolo_ = -1;   // ett gammalt väntande soloval släpps
    if (pressRow_ >= 0) {
        pressRow_ = pressColumn_ = -1;
        invalidate();
    }
    if (color_->isVisible() && color_->isOpen()) {
        endEdit(true);   // väljarens commit sparar (`commitColor`)
        return;
    }
    if (headerMouseDown(c, mx, my))
        return;   // remsan eller huvudet tog trycket
    // ETT TRYCK I DATAGRIDEN SPARAR FÖRST — direkt, vi står i
    // händelseflödet. Rev eller flyttade handlern datagriden är trycket slut.
    if (isEditing() && !endEdit(true))
        return;
    const int p = positionAt(c, my);
    if (p < 0)
        return;
    const int rad = rowAtDisplay(p);
    if (rad < 0 || rowKind(rad) == RowKind::Blank)
        return;   // en tom rad är aldrig aktuell
    if (rowKind(rad) == RowKind::Category) {
        setCurrent(rad);
        ensureVisible(rad);
        invalidate();
        toggleCategory(rad);
        return;
    }
    int        k        = columnAt(c, mx);
    // Ett klick på ett radnamn gör raden aktuell men flyttar inte
    // kolumnen — radrubriken är aldrig en aktuell cell.
    const bool påRubrik = k == 0 && rowHeaders();
    if (påRubrik)
        k = std::max(currentColumn_, firstColumn());
    if (k < 0)
        return;
    // Musens modifierare ur fönstret (widget.h) — radvalets Ctrl och Shift.
    const AppWindow*    w    = appWindow();
    const std::uint32_t mods = w ? w->modifierKeys() : 0u;
    const bool modifierad =
        (mods & (gfx::kModCtrl | gfx::kModCmd | gfx::kModShift)) != 0;
    if (selectionMode_ == SelectionMode::FullRow && multiSelect_ &&
        isSelected(rad) && !modifierad) {
        // SOLO VID SLÄPPET (listvyns form): trycket gör bara cellen
        // aktuell, så att ett tryck som börjar en redigering i en vald rad
        // inte tömmer valet.
        setCurrent(rad);
        const int kol = std::clamp(k, firstColumn(), columnCount() - 1);
        if (currentColumn_ != kol) {
            currentColumn_ = kol;
            invalidate();
        }
        ensureVisible(rad);
        ensureColumnVisible(kol);
        pendingSolo_ = rad;
    } else if (!setCurrentPosition(p, k, mods, false)) {
        return;
    }
    if (modifierad || påRubrik || currentRow() != rad || currentColumn_ != k)
        return;
    switch (cellEditor(rad, k).editor) {
    case Editor::Check:
        if (editable(rad, k)) {
            pendingSolo_ = -1;
            toggleCheckFromUser(rad, k);
        }
        return;
    case Editor::Button:
        if (pressable(rad, k)) {
            pendingSolo_ = -1;
            pressRow_    = rad;
            pressColumn_ = k;
            pressInside_ = true;
            invalidate();
        }
        return;
    case Editor::Color:
    case Editor::Choice:
        startEdit(nullptr);
        break;
    case Editor::Text:
    case Editor::Number:
        if (editOnClick_)
            startEdit(nullptr);
        break;
    case Editor::None:
        break;
    }
    if (isEditing())
        pendingSolo_ = -1;
}

// SLÄPPET: huvudets först, sedan knappcellen — klicket gäller bara när
// trycket släpps över SAMMA cell (dra bort för att ångra) — och sist
// radvalets väntande soloval.
bool DataGrid::mouseUp(const Ctx& c, float mx, float my)
{
    const bool bas = Table::mouseUp(c, mx, my);
    const int  br  = pressRow_;
    const int  bk  = pressColumn_;
    if (br >= 0) {
        pressRow_ = pressColumn_ = -1;
        invalidate();
        const int p = hitPosition(c, mx, my);
        if (p >= 0 && rowAtDisplay(p) == br && columnAt(c, mx) == bk) {
            clickCellFromUser(br, bk);
            return true;   // en handler har kört
        }
    }
    const int rad = pendingSolo_;
    pendingSolo_  = -1;
    if (rad < 0 || rad >= rowCount() || displayRow(rad) < 0)
        return bas;
    return selectRowFromUser(rad, 0u, false) || bas;
}

// DRAGET under ett tryck: basens avdelare, och för en tryckt knappcell om
// pekaren står kvar över den — ritkosmetik, invaliderad bara vid ändring.
void DataGrid::mouseDrag(const Ctx& c, float mx, float my)
{
    Table::mouseDrag(c, mx, my);
    if (pressRow_ < 0)
        return;
    const int  p   = hitPosition(c, mx, my);
    const bool kvar = p >= 0 && rowAtDisplay(p) == pressRow_ &&
                      columnAt(c, mx) == pressColumn_;
    if (kvar != pressInside_) {
        pressInside_ = kvar;
        invalidate();
    }
}

// HOVERN över en knappcell — ritkosmetik, invaliderad bara vid ändring.
void DataGrid::mouseMove(const Ctx& c, float mx, float my)
{
    Table::mouseMove(c, mx, my);
    int       hr = -1;
    int       hk = -1;
    const int p  = hitPosition(c, mx, my);
    if (p >= 0) {
        const int r = rowAtDisplay(p);
        const int k = columnAt(c, mx);
        if (pressable(r, k)) {
            hr = r;
            hk = k;
        }
    }
    if (hr != hoverRow_ || hk != hoverColumn_) {
        hoverRow_    = hr;
        hoverColumn_ = hk;
        invalidate();
    }
}

// Ett AVBRUTET tryck: knappen klickas inte, och inget soloval görs.
bool DataGrid::mouseCancel(gfx::MouseButton b)
{
    if (b == gfx::MouseButton::Left) {
        pendingSolo_ = -1;
        if (pressRow_ >= 0) {
            pressRow_ = pressColumn_ = -1;
            invalidate();
        }
    }
    return Table::mouseCancel(b);
}

// DUBBELKLICKET på den aktuella cellen börjar redigera — trycket före har
// redan gjort den aktuell (AppWindow skickar dubbelklicket EFTER trycket).
void DataGrid::mouseDoubleClick(const Ctx& c, float mx, float my)
{
    if (headerDoubleClick(c, mx, my))
        return;   // remsan eller huvudet — en avdelare anpassar sin kolumn
    const int p = positionAt(c, my);
    if (p >= 0 && rowAtDisplay(p) == currentRow() &&
        columnAt(c, mx) == currentColumn_)
        beginEdit();
}

// EDITORN på cellen — ur `cellInContent`, samma formel som ritningen, mot
// `c.area` (datagridens `childArea`: innehållets origo minus den klampade
// scrollen). Räknad vid varje `resolve()`, så den följer scroll, sortering
// och en dragen kolumnbredd utan en sparad koordinat. Bara Ctx:ets egna
// fält läses — `c` är BARNETS Ctx, och datagridens egen `resolve()` hade
// räknats mot fel yta här. Därför går innehållsbredden (Autosize-kolumnernas
// bredd) in UTTRYCKLIGEN: barnets area ÄR datagridens `childArea`, som har
// `innerRect`s bredd. Editorn står där VÄRDET ritas (`Table::cellContent`):
// förbi kategorins indrag, radens ikon och cellens bild, med sin egen luft
// där cellens luft stod och fram till cellens högerkant — och talets fält
// mellan prefixet och suffixet (`numberFieldRect`). De dolda editorerna får
// en tom rect.
gfx::Rectf DataGrid::placeChild(const Widget& child, const Ctx& c) const
{
    const int pos = editRow_ >= 0 && child.isVisible() ? displayRow(editRow_)
                                                       : -1;
    if (pos < 0)
        return gfx::Rectf{c.area.x, c.area.y, 0.0f, 0.0f};
    const gfx::Rectf cell =
        cellInContent(c, pos, editColumn_, c.area.w / c.scale);
    gfx::Rectf innehåll = cellContent(c, cell, editRow_, editColumn_).content;
    innehåll.w          = std::max(0.0f, innehåll.w);
    gfx::Rectf r;
    if (&child == static_cast<const Widget*>(text_) &&
        editKind_ == Editor::Number) {
        r = valuecell::numberFieldRect(
            c, innehåll, numberSpec(cellEditor(editRow_, editColumn_)));
    } else {
        const float x = std::min(innehåll.x - cellPadX(c), cell.x + cell.w);
        r = gfx::Rectf{x, cell.y, cell.x + cell.w - x, cell.h};
    }
    return Draw::project(c, c.area, r);
}

// ------------------------------------------------------------- ritningen

float DataGrid::baseRowH(const Ctx& c) const
{
    return c.style->gridRowH;
}

float DataGrid::cellPadX(const Ctx& c) const
{
    return c.style->gridPadX;
}

// EN CELLS BREDD i `AutoMin`: basens del före värdet (indraget, zonen och
// ikonen i kolumn 0, cellens bild) och värdet som det RITAS — kryssrutan,
// färgen i sitt `colorDisplay`, talet med prefix och enhet, knappen, och för en
// val-cell det valdas ikon före texten.
float DataGrid::cellContentWidth(const Ctx& c, int row, int column) const
{
    const float bas = Table::cellContentWidth(c, row, column);
    if (rowKind(row) != RowKind::Data)
        return bas;
    const EditorSpec&  s    = cellEditor(row, column);
    const std::string& text = cell(row, column);
    const float        före =
        std::max(0.0f, bas - (text.empty() ? 0.0f : Draw::measure(c, text).w));
    switch (s.editor) {
    case Editor::Check:
        return före + valuecell::checkW(c);
    case Editor::Color:
        return före + valuecell::colorW(c, text, s.allowAlpha, s.colorDisplay);
    case Editor::Number: {
        const valuecell::NumberSpec ns = numberSpec(s);
        return före + valuecell::numberW(
                          c,
                          valuecell::displayNumber(text, pendingFor(row, column),
                                                   ns, sep_.str()),
                          ns);
    }
    case Editor::Button:
        return före + valuecell::buttonW(c, text, cellImage(row, column));
    case Editor::Choice: {
        const auto it = std::find(s.choices.begin(), s.choices.end(), text);
        const std::size_t i = std::size_t(it - s.choices.begin());
        if (it == s.choices.end() || i >= s.choiceIcons.size() ||
            !s.choiceIcons[i].valid())
            return bas;
        const float k = s.choiceIcons[i].sizeClass();
        return bas + (k > 0.0f ? k : c.style->iconFallback) + c.style->iconGap;
    }
    case Editor::Text:
    case Editor::None:
        return bas;
    }
    return bas;
}

// SORTERINGSVÄRDET ur redigeraren: talet (ett tomt eller otolkbart står sist),
// krysset som 0/1 och färgen som RGBA packat till ett tal.
std::optional<double> DataGrid::cellSortValue(int row, int column) const
{
    if (row < 0 || row >= rowCount())
        return std::nullopt;
    switch (cellEditor(row, column).editor) {
    case Editor::Number:
        return numberValue(row, column);
    case Editor::Check:
        return isChecked(row, column) ? 1.0 : 0.0;
    case Editor::Color: {
        const std::optional<gfx::Color> f = colorValue(row, column);
        if (!f)
            return std::nullopt;
        const double steg =
            double(std::numeric_limits<std::uint8_t>::max()) + 1.0;
        return ((double(f->r) * steg + double(f->g)) * steg + double(f->b)) *
                   steg +
               double(f->a);
    }
    default:
        return std::nullopt;
    }
}

// CELLENS VÄRDE per redigerare — de typade cellernas ritning. Ett fel fyller
// cellen med `errorSurface` och skriver texten i felets textfärg; en val-cell
// visar det valdas ikon före texten; text, val utan ikon och skrivskyddat är
// basens text.
void DataGrid::drawCellValue(const Ctx& c, const gfx::Rectf& cellR,
                             const gfx::Rectf& content, int row, int column,
                             gfx::Color fg) const
{
    const EditorSpec&  s    = cellEditor(row, column);
    const std::string& text = cell(row, column);
    const bool         på   = isEnabled() && isCellEnabled(row, column);
    const Align        just = columns()[std::size_t(column)].align;
    if (hasError(row, column)) {
        valuecell::drawError(c, cellR);
        fg = valuecell::errorText(c);
    }
    switch (s.editor) {
    case Editor::Check:
        valuecell::drawCheck(c, content, just, isChecked(row, column), på, fg);
        return;
    case Editor::Color:
        valuecell::drawColor(c, content, just, text, s.allowAlpha,
                             s.colorDisplay, på, fg);
        return;
    case Editor::Number: {
        const valuecell::NumberSpec ns = numberSpec(s);
        valuecell::drawNumber(
            c, content, just,
            valuecell::displayNumber(text, pendingFor(row, column), ns,
                                     sep_.str()),
            ns, fg);
        return;
    }
    case Editor::Button:
        valuecell::drawCellButton(
            c, cellR, text, cellImage(row, column), på,
            pressed() && pressInside_ && pressRow_ == row &&
                pressColumn_ == column,
            hovered() && hoverRow_ == row && hoverColumn_ == column);
        return;
    case Editor::Choice: {
        const auto it = std::find(s.choices.begin(), s.choices.end(), text);
        const std::size_t i = std::size_t(it - s.choices.begin());
        if (it != s.choices.end() && i < s.choiceIcons.size() &&
            s.choiceIcons[i].valid()) {
            const ImageSource& img  = s.choiceIcons[i];
            const float        k    = img.sizeClass();
            const float        sida =
                std::min(k > 0.0f ? k : c.style->iconFallback, content.h);
            Draw::stateGlyph(c,
                             {content.x, content.y + (content.h - sida) / 2.0f,
                              sida, sida},
                             img, på);
            const float steg = sida + c.style->iconGap;
            Table::drawCellValue(c, cellR,
                                 {content.x + steg, content.y,
                                  std::max(0.0f, content.w - steg), content.h},
                                 row, column, fg);
            return;
        }
        Table::drawCellValue(c, cellR, content, row, column, fg);
        return;
    }
    case Editor::Text:
    case Editor::None:
        Table::drawCellValue(c, cellR, content, row, column, fg);
        return;
    }
}

// YTORNA är lika i alla tre, och de är FÄLTETS — per CELL: en cell man kan
// redigera är en SKRIVYTA och ritas på `panel` (TextInputs skrivyta i alla tre
// familjerna), en cell som INTE går att redigera som ett SKRIVSKYDDAT fält
// (`fieldReadOnly`, TextInputs `readOnly`) — med texten i full kontrast, för
// skrivskyddat betyder "läs gärna", inte "inte nu". En INAKTIV cell är en
// skrivyta med dämpad text. Under sista raden är ytan bara yta, och en
// kategori- eller tomrad har inga celler och inget rutnät (`drawSpanRows`).
// Det som skiljer familjerna är den AKTUELLA CELLEN, som markeras som
// listvyns val:
//
//   SLATE         fylld med `accent`, texten `windowBg`
//   GLAZE         det GRINDADE paret `accentSurface`/`onAccent` (listvyn,
//                 PropertyGrids aktiva rad), inte laminerad — en cell har
//                 ingen kontur. Ramen i den bestämda `buttonBorder`.
//   MINIMALISTIC  ett accent-STRECK i vänsterkanten (`rowMarkerW`) och
//                 texten i `accent` — en cell är ingen kontroll, så
//                 "fyllning betyder tillstånd" lånas inte hit. Ramen
//                 `buttonBorder`, som listvyns.
//
//   vila     celler i `text`, rutnät `border`
//   hover    — (en cell markeras inte av musen; huvudet och knappcellen gör)
//   aktuell  se ovan; under redigering ritar editorn sig själv i stället. En
//            aktuell KATEGORIRAD ramas in över hela raden
//   fokus    fristående ring utanför kanten; under redigering editorns
//   inaktiv  all text `textDim`, den aktuella cellen `buttonDisabled`
//            (Minimalistic: strecket i `textDim`)
//   radval   (`FullRow`) de VALDA RADERNA bär markeringen i stället
//            för cellen — samma recept, fram till rutnätets högerkant, med
//            Minimalistics streck vid radens vänsterkant — och den aktuella
//            cellen ramas in med en hårlinje i radens textfärg (listvyns
//            aktuella rad)
void DataGrid::draw(const Ctx& c)
{
    const gfx::Rectf b       = c.bounds();
    const gfx::Rectf vp      = scrollViewport(c);
    const auto&      m       = *c.style;
    const auto&      col     = c.theme->colors;
    const bool       på      = isEnabled();
    const int        n       = positionCount();
    const int        kn      = columnCount();
    const int        första  = firstVisiblePosition(c);
    const gfx::Rectf kolSist = kn > 0 ? cellRect(c, 0, kn - 1) : gfx::Rectf{};
    const float      höger   = kn > 0 ? kolSist.x + kolSist.w : vp.x;
    const int        aktRad  = currentRow();
    const RowKind    aktSlag = aktRad >= 0 && aktRad < rowCount()
                                   ? rowKind(aktRad)
                                   : RowKind::Data;
    // Den aktuella cellens plats — −1 när ingen ska markeras (ingen
    // aktuell cell, en kategorirad, eller en editor som täcker den).
    const int        akt     = !isEditing() && currentColumn_ >= 0 &&
                                      currentColumn_ < kn &&
                                      aktSlag == RowKind::Data
                                  ? displayRow(aktRad)
                                  : -1;
    const gfx::Rectf aktR    = akt >= 0 ? cellRect(c, akt, currentColumn_)
                                        : gfx::Rectf{};
    // RADVALET: i radläget bär de valda RADERNA markeringen, och
    // den aktuella cellen fylls inte utan ramas in (efter rutnätet nedan).
    const bool       radval  = selectionMode_ == SelectionMode::FullRow;
    // De synliga platserna, en i taget — från första synliga till
    // nederkanten (O(synliga), som listvyn).
    const auto synliga = [&](auto gör) {
        for (int p = första; p < n; ++p) {
            const gfx::Rectf r = rowRect(c, p);
            if (r.y >= vp.y + vp.h)
                break;
            gör(p, rowAtDisplay(p), r);
        }
    };
    // En vald rads band, fram till rutnätets högerkant — `rowRect` är hela
    // det synliga bandet, och till höger om sista kolumnen är ytan bara yta.
    const auto valdaRader = [&](auto rita) {
        synliga([&](int, int rad, const gfx::Rectf& r) {
            const float slut = std::min(höger, r.x + r.w);
            if (isSelected(rad))
                rita(gfx::Rectf{r.x, r.y, std::max(0.0f, slut - r.x), r.h});
        });
    };
    // Cellerna i en DATArad inom synfältet i sidled.
    const auto celler = [&](int p, auto gör) {
        for (int k = 0; k < kn; ++k) {
            const gfx::Rectf cr = cellRect(c, p, k);
            if (cr.x >= vp.x + vp.w)
                break;
            if (cr.x + cr.w <= vp.x)
                continue;
            gör(k, cr);
        }
    };

    Draw::rect(c, b, 0.0f, col.panel);
    Draw::clip(c, vp);
    synliga([&](int p, int rad, const gfx::Rectf&) {
        if (rowKind(rad) != RowKind::Data)
            return;
        celler(p, [&](int k, const gfx::Rectf& cr) {
            if (!(k == 0 && rowHeaders()) && readOnlySurface(rad, k))
                Draw::rect(c, cr, 0.0f, col.fieldReadOnly);
        });
    });

    gfx::Color aktFg = col.text;     // den aktuella cellens (radens) text
    gfx::Color kant  = col.border;   // ramen
    switch (c.ui) {
    case UI::Minimalistic:
        if (radval)
            valdaRader([&](const gfx::Rectf& r) {
                Draw::rect(c, {r.x, r.y, m.rowMarkerW, r.h}, 0.0f,
                           på ? col.accent : col.textDim);
            });
        else if (akt >= 0)
            Draw::rect(c, {aktR.x, aktR.y, m.rowMarkerW, aktR.h}, 0.0f,
                       på ? col.accent : col.textDim);
        aktFg = col.accent;
        kant  = på ? col.buttonBorder : col.border;
        break;
    case UI::Glaze:
        if (radval)
            valdaRader([&](const gfx::Rectf& r) {
                Draw::rect(c, r, 0.0f,
                           på ? col.accentSurface : col.buttonDisabled);
            });
        else if (akt >= 0)
            Draw::rect(c, aktR, 0.0f,
                       på ? col.accentSurface : col.buttonDisabled);
        aktFg = col.onAccent;
        kant  = på ? col.buttonBorder : col.border;
        break;
    case UI::Slate:
    default:
        if (radval)
            valdaRader([&](const gfx::Rectf& r) {
                Draw::rect(c, r, 0.0f, på ? col.accent : col.buttonDisabled);
            });
        else if (akt >= 0)
            Draw::rect(c, aktR, 0.0f, på ? col.accent : col.buttonDisabled);
        aktFg = col.windowBg;
        break;
    }
    Draw::endClip(c);

    // CELLERNA och RUTNÄTET — lika i alla tre, per DATArad: linjen under och
    // de lodräta linjerna i radens höjd, så en kategori- eller tomrad inte
    // korsas av kolumnernas linjer.
    const Pen linje{col.border, m.borderWidth};
    Draw::clip(c, vp);
    synliga([&](int p, int rad, const gfx::Rectf& r) {
        if (rowKind(rad) != RowKind::Data)
            return;
        celler(p, [&](int k, const gfx::Rectf& cr) {
            // Den aktuella cellen (i radläget: en VALD rad) behåller
            // markeringens färg; resten bär APPENS (cellens, annars radens,
            // table.h) eller temats — `drawCell` fyller också cellens
            // bakgrund, över ytan och under rutnätet. En inaktiv cell dämpas.
            const bool markerad = radval ? isSelected(rad)
                                         : p == akt && k == currentColumn_;
            const bool cellPå   = isCellEnabled(rad, k);
            const gfx::Color fg = !på        ? col.textDim
                                  : markerad ? aktFg
                                  : !cellPå  ? col.textDim
                                             : col.text;
            drawCell(c, p, rad, k, fg, på && cellPå && !markerad);
            Draw::line(c, cr.x + cr.w, r.y, cr.x + cr.w, r.y + r.h, linje);
        });
        Draw::line(c, r.x, r.y + r.h, std::min(höger, r.x + r.w), r.y + r.h,
                   linje);
    });
    // I radläget RAMAS den aktuella cellen in, i radens textfärg (listvyns
    // aktuella rad) — efter rutnätet, så linjerna inte täcker ramen.
    if (radval && akt >= 0)
        Draw::frame(c, aktR, 0.0f,
                    Pen{!på                         ? col.textDim
                        : isSelected(currentRow()) ? aktFg
                                                    : col.text,
                        m.borderWidth});
    Draw::endClip(c);

    drawRowHeaders(c);
    drawSpanRows(c);
    // En aktuell KATEGORIRAD ramas in över hela raden — efter raden själv.
    if (!isEditing() && aktSlag == RowKind::Category) {
        const int pa = displayRow(aktRad);
        if (pa >= 0) {
            const gfx::Rectf r = rowRect(c, pa);
            Draw::clip(c, vp);
            Draw::frame(c,
                        {r.x, r.y, std::max(0.0f, std::min(höger, r.x + r.w) - r.x),
                         r.h},
                        0.0f, Pen{på ? col.accent : col.textDim, m.borderWidth});
            Draw::endClip(c);
        }
    }
    drawHeader(c);
    Draw::frame(c, b, 0.0f, Pen{kant, m.borderWidth});
    if (focused() && på)
        Draw::frame(c, Draw::inset(b, -m.focusOffset), m.focusOffset,
                    Pen{col.accent, m.focusWidth});
}

} // namespace glaze
