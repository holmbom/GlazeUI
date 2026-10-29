// DataGrid — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ RADER:
// Draw ritar, c.style bär måtten och temat färgerna. Kolumnerna, raderna,
// sorteringen och huvudet är `Table`s (table.cpp); här bor kolumntypen,
// REDIGERINGEN och de tre recepten runt cellerna.
//
// SKRIVNA BESLUT (de långa motiveringarna står i data_grid.h):
//   två editorer som riktiga barn, placerade av datagriden (`placeChild`) ·
//   en sparning per avslutad redigering (inte live) · fokusförlustens
//   `onChange` postas · en aktuell cell i taget · kolumntypen hör till
//   INDEXET · radvalet är listvyns, med ett eget event.

#include "glaze/widgets/data_grid.h"

#include <algorithm>

#include "glaze/app_window.h"
#include "glaze/draw.h"
#include "glaze/widgets/dropdown.h"
#include "glaze/widgets/textinput.h"

#include "../prop_parse.h"

namespace glaze {

// ----------------------------------------------------------- editorerna
//
// En tangent går till den FOKUSERADE widgeten och bubblar aldrig vidare —
// så utan subklasserna hade Enter, Escape och Tab aldrig nått datagriden
// medan en cell redigeras (talfältets `Field` är prejudikatet). Det enda de
// gör är att lämna över tangenterna som AVSLUTAR en redigering, och att
// berätta när fokus lämnar dem. Allt annat — caret, markering, klippbord,
// listan — ärvs orört.

namespace {

// Tangenten bär en modifierare som gör den till någon annans genväg
// (Ctrl/Cmd/Alt) — då är den inte redigeringens.
bool annansGenväg(std::uint32_t mods)
{
    return (mods & (gfx::kModCtrl | gfx::kModCmd | gfx::kModAlt)) != 0;
}

} // namespace

class DataGrid::TextEditor final : public TextInput {
public:
    explicit TextEditor(DataGrid& owner) : owner_(owner) {}

    // En NY CELL börjar: texten och en TOM ångra-historik. Editorn
    // ÅTERANVÄNDS mellan cellerna, och `setText`s
    // likhetsvakt bevarar historiken när nästa cell råkar ha samma text —
    // då hade Ctrl+Z i den nya cellen spelat upp redigeringen av den förra.
    void börja(const std::string& text)
    {
        setText(text);
        resetHistory();
    }

    // Caret SIST — genom fältets egen End-väg, så det finns ingen andra
    // caretlogik här.
    void caretSist() { TextInput::keyDown(gfx::Key::End, 0); }

    // Det FÖRSTA tecknet (skrivandet som börjar redigeringen) går samma väg
    // som alla följande — fältets egen filtrering och caret.
    void skriv(const char* utf8) { TextInput::textInput(utf8); }

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

    void focusLost() override
    {
        TextInput::focusLost();
        owner_.editorLostFocus();
    }

private:
    DataGrid& owner_;
};

class DataGrid::ChoiceEditor final : public Dropdown {
public:
    explicit ChoiceEditor(DataGrid& owner) : owner_(owner) {}

protected:
    // Med listan ÖPPEN är pilarna listans (markören) och Enter/Space dess
    // val. STÄNGD är tangenterna datagridens — Upp/Ned flyttar en rad i
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
                // handlers lät datagriden och oss leva.
                const HandlerGuard vakt(*this);
                Dropdown::keyDown(k, mods);
                if (vakt.ok())
                    owner_.saveEdit();
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

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("datagrid", DataGrid)

DataGrid::DataGrid()
{
    text_ = add<TextEditor>(*this);
    text_->hide();
    choice_ = add<ChoiceEditor>(*this);
    choice_->hide();
    // Ett VAL av ett nytt värde i listan är en sparning — användarens
    // handling, som Enter i ett fält. Programmatisk sättning av listan
    // (`startEdit`) fyrar inte dropdownens onChange, så ingen eko-loop.
    choice_->onChange = [this] { choicePicked(); };
}

// ------------------------------------------------------------ kolumntypen

void DataGrid::setColumnEditor(int column, Editor editor)
{
    if (column < 0) {
        gfx::log("DataGrid(%s): setColumnEditor(%d) — negativt index, "
                 "ignoreras", name.c_str(), column);
        return;
    }
    if (std::size_t(column) >= columnEdits_.size()) {
        if (editor == Editor::Text)
            return;   // standardtypen — inget att lagra
        columnEdits_.resize(std::size_t(column) + 1);
    }
    ColumnEdit& e = columnEdits_[std::size_t(column)];
    if (e.editor == editor)
        return;
    e.editor = editor;
    if (editColumn_ == column)
        endEdit(false);
    invalidate();   // en skrivskyddad kolumn ritas på en annan yta
}

DataGrid::Editor DataGrid::columnEditor(int column) const
{
    return column >= 0 && std::size_t(column) < columnEdits_.size()
               ? columnEdits_[std::size_t(column)].editor
               : Editor::Text;
}

void DataGrid::setColumnChoices(int column, std::vector<std::string> choices)
{
    if (column < 0) {
        gfx::log("DataGrid(%s): setColumnChoices(%d) — negativt index, "
                 "ignoreras", name.c_str(), column);
        return;
    }
    if (std::size_t(column) >= columnEdits_.size()) {
        if (choices.empty())
            return;
        columnEdits_.resize(std::size_t(column) + 1);
    }
    ColumnEdit& e = columnEdits_[std::size_t(column)];
    if (e.choices == choices)
        return;
    e.choices = std::move(choices);
    // En ÖPPEN lista visar de gamla valen — stängd är bättre (Dropdowns
    // `setItems`-prejudikat).
    if (editColumn_ == column && e.editor == Editor::Choice)
        endEdit(false);
    invalidate();   // en val-kolumn utan val är skrivskyddad
}

const std::vector<std::string>& DataGrid::columnChoices(int column) const
{
    static const std::vector<std::string> inga;
    return column >= 0 && std::size_t(column) < columnEdits_.size()
               ? columnEdits_[std::size_t(column)].choices
               : inga;
}

// Går kolumnen att redigera? Det är den här frågan ritningen ställer också,
// så en val-kolumn utan val ser skrivskyddad ut — för det är den.
bool DataGrid::editable(int column) const
{
    if (column < 0 || column >= columnCount())
        return false;
    if (column == 0 && rowHeaders())
        return false;   // radrubriken är en rubrik, och står i remsan
    switch (columnEditor(column)) {
    case Editor::Text:   return true;
    case Editor::Choice: return !columnChoices(column).empty();
    case Editor::None:   return false;
    }
    return false;
}

void DataGrid::columnsReplaced()
{
    endEdit(false);
    currentColumn_ = -1;
}

void DataGrid::rowsReplaced()
{
    // `Table::setRows` har redan nollat den aktuella raden — kolumnen följer,
    // och radvalet nollas (listvyns `setRows`: en rad som inte finns kvar kan
    // inte vara vald).
    ++rowsGen_;
    endEdit(false);
    currentColumn_ = -1;
    selected_.assign(std::size_t(rowCount()), 0);
    anchor_ = -1;
}

void DataGrid::rowAppended()
{
    selected_.push_back(0);   // en ny rad är aldrig vald
}

// ------------------------------------------------------ radvalet

bool DataGrid::assignSelection(const std::vector<int>& rows)
{
    std::vector<char> nytt(std::size_t(rowCount()), 0);
    for (int r : rows)
        if (r >= 0 && r < rowCount())
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
        for (int p = 0; kvar < 0 && p < rowCount(); ++p)
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
    for (int p = 0; p < rowCount(); ++p)
        if (isSelected(rowAtDisplay(p)))
            ut.push_back(rowAtDisplay(p));
    return ut;
}

void DataGrid::selectAll()
{
    if (!multiSelect_)
        return;   // enkelval har ingen "alla"
    std::vector<int> alla(static_cast<std::size_t>(rowCount()));
    for (std::size_t r = 0; r < alla.size(); ++r)
        alla[r] = int(r);
    assignSelection(alla);
}

void DataGrid::clearSelection()
{
    assignSelection({});
}

// ANVÄNDARVÄGEN — `ListView::selectFromUser` i datagridens form: raden har
// redan gjorts aktuell av anroparen, spannet räknas i VISNINGSORDNINGEN från
// ankaret, och händelsen fyras bara när valet ändrades.
bool DataGrid::selectRowFromUser(int row, std::uint32_t mods, bool fromKey)
{
    if (selectionMode_ != SelectionMode::FullRow || row < 0 ||
        row >= rowCount())
        return true;
    const bool shift = multiSelect_ && (mods & gfx::kModShift);
    const bool vänd  = multiSelect_ && !fromKey &&
                      (mods & (gfx::kModCtrl | gfx::kModCmd));
    bool ändrat = false;
    if (shift) {
        const int ankare = anchor_ >= 0 && anchor_ < rowCount() ? anchor_
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
    return Table::setProp(key, value);   // basen SIST — konventionen
}

bool DataGrid::canReleaseChild(const Widget& child) const
{
    return &child != static_cast<const Widget*>(text_) &&
           &child != static_cast<const Widget*>(choice_);
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

void DataGrid::setCurrentPosition(int position, int column,
                                  std::uint32_t mods, bool fromKey)
{
    const int n  = rowCount();
    const int kn = columnCount();
    if (n == 0 || kn == 0)
        return;
    const int rad = rowAtDisplay(std::clamp(position, 0, n - 1));
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
    selectRowFromUser(rad, mods, fromKey);
}

void DataGrid::moveFromUser(int dRow, int dColumn)
{
    if (rowCount() == 0 || columnCount() == 0)
        return;
    const int pos = displayRow(currentRow());
    // Utan en hel aktuell cell landar förflyttningen i den del som finns —
    // eller i första cellen.
    if (pos < 0 || currentColumn_ < 0) {
        setCurrentPosition(std::max(pos, 0), std::max(currentColumn_, 0));
        return;
    }
    setCurrentPosition(pos + dRow, currentColumn_ + dColumn);
}

// Tab cell för cell, med radbyte i kanten — och false i tabellens första
// respektive sista cell, så att Tab lämnar datagriden (AppWindow går då
// vidare i fokusordningen).
bool DataGrid::tabFromUser(int dir)
{
    const int n  = rowCount();
    const int kn = columnCount();
    if (n == 0 || kn == 0)
        return false;
    int pos = displayRow(currentRow());
    if (pos < 0 || currentColumn_ < 0) {
        setCurrentPosition(dir > 0 ? 0 : n - 1, dir > 0 ? 0 : kn - 1);
        return true;
    }
    const int första = firstColumn();
    int       kol    = currentColumn_ + dir;
    if (kol >= kn) {
        kol = första;
        ++pos;
    } else if (kol < första) {
        kol = kn - 1;
        --pos;
    }
    if (pos < 0 || pos >= n)
        return false;
    setCurrentPosition(pos, kol);
    return true;
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

void DataGrid::startEdit(const std::string* start)
{
    const int r = currentRow();
    const int k = currentColumn_;
    if (editRow_ >= 0 || !isEnabled() || r < 0 || !editable(k) ||
        !appWindow() || !appWindow()->isOpen())
        return;
    if (start && columnEditor(k) != Editor::Text)
        return;   // en val-cell har inget att skriva i
    editRow_    = r;
    editColumn_ = k;
    ensureVisible(r);
    ensureColumnVisible(k);
    // Editorn står i KOLUMNENS justering — en centrerad eller
    // högerjusterad cell ska inte hoppa till vänster när den börjar
    // redigeras. Båda editorerna bär `align` med samma namn (regel 6).
    const Align just = columns()[std::size_t(k)].align;

    if (columnEditor(k) == Editor::Choice) {
        const auto& val = columnChoices(k);
        choice_->setAlign(just);
        choice_->setItems(val);
        const auto it = std::find(val.begin(), val.end(), cell(r, k));
        choice_->setSelectedIndex(it == val.end() ? -1
                                                  : int(it - val.begin()));
        choice_->show();
        choice_->focus();
        choice_->open();   // dubbelklicket/F2 ÄR intentionen — listan direkt
    } else {
        text_->setAlign(just);
        text_->börja(start ? std::string() : cell(r, k));
        text_->show();
        text_->focus();
        if (start)
            text_->skriv(start->c_str());
        else
            text_->caretSist();
    }
    invalidate();
}

void DataGrid::commitEdit()
{
    endEdit(true);
}

void DataGrid::cancelEdit()
{
    endEdit(false);
}

// AVSLUTET — med eller utan sparning. ORDNINGEN är bärande:
//   1. tillståndet nollas FÖRST, så editorns `focusLost` (som fokusbytet i
//      punkt 2 utlöser) ser att ingen redigering pågår och inte avslutar igen;
//   2. datagriden tar fokus INNAN editorn göms — en dold widget med fokus
//      hade lämnat tangenterna i tomma luften. Kommer vi hit UR en
//      `focusLost` har fokus redan lämnat editorn, och då rörs det inte;
//   3. cellen skrivs och `onChange` fyras SIST, när allt är städat: en
//      handler får göra vad som helst, även bygga om tabellen. Ur en
//      `focusLost` POSTAS eventet i stället (data_grid.h).
// En val-cell vars text inte fanns bland valen, och där inget valts, bär
// inget värde — den sparar ingenting. Svarar om datagriden går att röra
// efteråt (en direkt avfyrning kör handlers som får riva den); editorerna
// frågar svaret innan de flyttar den aktuella cellen.
bool DataGrid::endEdit(bool spara, bool postad)
{
    if (editRow_ < 0)
        return true;
    const int         r        = editRow_;
    const int         k        = editColumn_;
    const bool        val      = choice_->isVisible();
    const bool        harVärde = !val || choice_->selectedIndex() >= 0;
    const std::string ny       = val ? choice_->selectedText() : text_->text();
    editRow_    = -1;
    editColumn_ = -1;
    if (text_->focused() || choice_->focused())
        focus();
    choice_->close();
    text_->hide();
    choice_->hide();
    invalidate();
    if (!spara || !harVärde || ny == cell(r, k))
        return true;
    setCell(r, k, ny);
    if (!postad)
        return fireCellChange(r, k);
    // POSTAD avfyrning (`postEvent` — basens vakt) som SLOPAS om raderna
    // bytts (`setRows`) i samma dispatch: då beskriver (r, k) inte längre
    // cellen som sparades.
    const unsigned gen = rowsGen_;
    postEvent([this, gen, r, k] {
        if (gen != rowsGen_)
            return;
        fireCellChange(r, k);
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

// `onChange` och sedan `onCommit` — en sparad cell är en avslutad
// handling, så de två kommer alltid tillsammans och med samma args: cellen
// står i args, så en onChange-handler som bygger om tabellen kan inte flytta
// den. Vakten emellan (`Widget::firePair`) — och svaret säger om datagriden
// går att röra efteråt.
bool DataGrid::fireCellChange(int row, int column)
{
    CellChangeArgs a;
    a.row    = row;
    a.column = column;
    return firePair(onChange, onCommit, a);   // värdet: cell(e.row, e.column)
}

void DataGrid::enabledChanged()
{
    if (!isEnabled())
        endEdit(false);   // en inaktiv datagrid redigeras inte
}

// ------------------------------------------------------- tangenter, mus

bool DataGrid::keyDown(gfx::Key k, std::uint32_t mods)
{
    if (mods & gfx::kModAlt)
        return false;   // Alt är menyernas och appens
    if (rowCount() == 0 || columnCount() == 0)
        return false;
    const bool styr  = (mods & (gfx::kModCtrl | gfx::kModCmd)) != 0;
    const bool shift = (mods & gfx::kModShift) != 0;
    const int  pos   = displayRow(currentRow());

    switch (k) {
    case gfx::Key::F2:
        if (styr || shift)
            return false;
        beginEdit();
        return isEditing();   // en skrivskyddad cell släpper tangenten
    case gfx::Key::Return:
        if (styr)
            return false;
        moveFromUser(shift ? -1 : 1, 0);
        return true;
    case gfx::Key::Tab:
        if (styr)
            return false;   // Ctrl+Tab lämnar datagriden (WinForms)
        return tabFromUser(shift ? -1 : 1);
    case gfx::Key::Left:
    case gfx::Key::Right:
        moveFromUser(0, k == gfx::Key::Left ? -1 : 1);
        return true;
    case gfx::Key::Home:
        setCurrentPosition(styr ? 0 : std::max(pos, 0), 0,
                           mods & gfx::kModShift);
        return true;
    case gfx::Key::End:
        setCurrentPosition(styr ? rowCount() - 1 : std::max(pos, 0),
                           columnCount() - 1, mods & gfx::kModShift);
        return true;
    case gfx::Key::A:
        // Ctrl+A väljer alla RADER (radläget med flerval) — en
        // användarväg: händelsen bara när valet ändrades. Annars bubblar den.
        if (!styr || selectionMode_ != SelectionMode::FullRow || !multiSelect_)
            return false;
        {
            std::vector<int> alla(static_cast<std::size_t>(rowCount()));
            for (std::size_t r = 0; r < alla.size(); ++r)
                alla[r] = int(r);
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
// ett kalkylark. Bara en fritext-cell: en val-cell har inget att skriva i.
bool DataGrid::textInput(const char* utf8)
{
    if (columnEditor(currentColumn_) != Editor::Text)
        return false;
    beginEditWith(utf8);
    return isEditing();
}

void DataGrid::mouseDown(const Ctx& c, float mx, float my)
{
    if (headerMouseDown(c, mx, my))
        return;   // remsan eller huvudet tog trycket
    const int p = positionAt(c, my);
    int       k = columnAt(c, mx);
    // Ett klick på ett radnamn gör raden aktuell men flyttar inte
    // kolumnen — radrubriken är aldrig en aktuell cell.
    if (k == 0 && rowHeaders())
        k = std::max(currentColumn_, firstColumn());
    // Musens modifierare ur fönstret (widget.h) — radvalets Ctrl och Shift.
    const AppWindow*    w    = appWindow();
    const std::uint32_t mods = w ? w->modifierKeys() : 0u;
    if (p >= 0 && k >= 0)
        setCurrentPosition(p, k, mods, false);
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
// räknats mot fel yta här. Därför går innehållsbredden (fyllkolumnens,
// bredd) in UTTRYCKLIGEN: barnets area ÄR datagridens
// `childArea`, som har `innerRect`s bredd. De dolda editorerna får en tom
// rect.
gfx::Rectf DataGrid::placeChild(const Widget& child, const Ctx& c) const
{
    const int pos = editRow_ >= 0 && child.isVisible() ? displayRow(editRow_)
                                                       : -1;
    if (pos < 0)
        return gfx::Rectf{c.area.x, c.area.y, 0.0f, 0.0f};
    return Draw::project(c, c.area,
                         cellInContent(c, pos, editColumn_, c.area.w / c.scale));
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

// YTORNA är lika i alla tre, och de är FÄLTETS: en cell man kan redigera är
// en SKRIVYTA och ritas på `panel` (TextInputs skrivyta i alla tre
// familjerna), en kolumn som INTE går att redigera som ett SKRIVSKYDDAT fält
// (`fieldReadOnly`, TextInputs `readOnly`) — med texten i full kontrast, för
// skrivskyddat betyder "läs gärna", inte "inte nu". Under sista raden är
// ytan bara yta. Det som skiljer familjerna är den AKTUELLA CELLEN, som
// markeras som listvyns val:
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
//   hover    — (en cell markeras inte av musen; huvudet gör det)
//   aktuell  se ovan; under redigering ritar editorn sig själv i stället
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
    const int        n       = rowCount();
    const int        kn      = columnCount();
    const int        första  = firstVisiblePosition(c);
    // Rutnätets nederkant och högerkant — under sista raden och till höger
    // om sista kolumnen är ytan bara yta.
    const gfx::Rectf sista   = n > 0 ? rowRect(c, n - 1) : gfx::Rectf{};
    const float      nedre   = n > 0 ? std::min(vp.y + vp.h, sista.y + sista.h)
                                     : vp.y;
    const gfx::Rectf kolSist = kn > 0 ? cellRect(c, 0, kn - 1) : gfx::Rectf{};
    const float      höger   = kn > 0 ? kolSist.x + kolSist.w : vp.x;
    // Den aktuella cellens plats — −1 när ingen ska markeras (ingen
    // aktuell cell, eller en editor som täcker den).
    const int        akt     = !isEditing() && currentColumn_ >= 0 &&
                                      currentColumn_ < kn
                                  ? displayRow(currentRow())
                                  : -1;
    const gfx::Rectf aktR    = akt >= 0 ? cellRect(c, akt, currentColumn_)
                                        : gfx::Rectf{};
    // RADVALET: i radläget bär de valda RADERNA markeringen, och
    // den aktuella cellen fylls inte utan ramas in (efter rutnätet nedan).
    const bool       radval  = selectionMode_ == SelectionMode::FullRow;
    // En vald rads band, fram till rutnätets högerkant — `rowRect` är hela
    // det synliga bandet, och till höger om sista kolumnen är ytan bara yta.
    const auto valdaRader = [&](auto rita) {
        for (int p = första; p < n; ++p) {
            const gfx::Rectf r = rowRect(c, p);
            if (r.y >= vp.y + vp.h)
                break;
            const float slut = std::min(höger, r.x + r.w);
            if (isSelected(rowAtDisplay(p)))
                rita(gfx::Rectf{r.x, r.y, std::max(0.0f, slut - r.x), r.h});
        }
    };

    Draw::rect(c, b, 0.0f, col.panel);
    Draw::clip(c, vp);
    for (int k = 0; k < kn; ++k) {
        if (editable(k))
            continue;
        const gfx::Rectf r = cellRect(c, 0, k);
        Draw::rect(c, {r.x, vp.y, r.w, nedre - vp.y}, 0.0f, col.fieldReadOnly);
    }

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

    // CELLERNA och RUTNÄTET — lika i alla tre. Radloopen startar vid FÖRSTA
    // SYNLIGA platsen och bryter vid nederkanten (O(synliga), som listvyn).
    const Pen linje{col.border, m.borderWidth};
    Draw::clip(c, vp);
    for (int p = första; p < n; ++p) {
        const gfx::Rectf r = rowRect(c, p);
        if (r.y >= vp.y + vp.h)
            break;
        const int rad = rowAtDisplay(p);
        for (int k = 0; k < kn; ++k) {
            const gfx::Rectf cr = cellRect(c, p, k);
            if (cr.x >= vp.x + vp.w)
                break;
            if (cr.x + cr.w <= vp.x)
                continue;
            // Den aktuella cellen (i radläget: en VALD rad) behåller
            // markeringens färg; resten bär APPENS (cellens, annars radens,
            // table.h) eller temats — `drawCell` fyller också cellens
            // bakgrund, över ytan och under rutnätet.
            const bool markerad = radval ? isSelected(rad)
                                         : p == akt && k == currentColumn_;
            const gfx::Color fg = !på       ? col.textDim
                                  : markerad ? aktFg
                                             : col.text;
            drawCell(c, p, rad, k, fg, på && !markerad);
        }
        Draw::line(c, r.x, r.y + r.h, std::min(höger, r.x + r.w), r.y + r.h,
                   linje);
    }
    for (int k = 0; k < kn; ++k) {
        const gfx::Rectf r = cellRect(c, 0, k);
        Draw::line(c, r.x + r.w, vp.y, r.x + r.w, nedre, linje);
    }
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
    drawHeader(c);
    Draw::frame(c, b, 0.0f, Pen{kant, m.borderWidth});
    if (focused() && på)
        Draw::frame(c, Draw::inset(b, -m.focusOffset), m.focusOffset,
                    Pen{col.accent, m.focusWidth});
}

} // namespace glaze
