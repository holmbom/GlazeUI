// ListView — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ RADER:
// Draw ritar, c.style bär måtten och temat färgerna. Kolumnerna, raderna,
// sorteringen och huvudet är `Table`s (table.cpp); här bor VALET, KRYSSEN
// och de tre recepten för hur en rad markeras — samma recept som ListBox,
// för en listvys rad ÄR en listrad.
//
// SKRIVNA BESLUT (de långa motiveringarna står i list_view.h och table.h):
//   enkelval som standard, flerval i Utforskarens form · en redan vald rad
//   väljs ensam vid SLÄPPET (trädets regel — så hela valet går att dra) ·
//   valet och kryssen följer RADEN och inte platsen · kryssen är oberoende
//   av valet, rutan först i första kolumnen och bocken i temats färg ·
//   `setRows` nollar kryssen som valet · radens färg på omarkerade rader ·
//   aktivering = dubbelklick eller Enter · inget drag från huvudet, remsorna
//   eller rutan · ingen typeahead.

#include "glaze/widgets/list_view.h"

#include <algorithm>
#include <cstdlib>

#include "glaze/app_window.h"
#include "glaze/draw.h"

#include "../prop_parse.h"
#include "../value_cell.h"

namespace glaze {

GLAZE_REGISTER_WIDGET("listview", ListView)

ListView::ListView() = default;

// ---------------------------------------------------------------- valet

void ListView::rowsReplaced()
{
    // Valet OCH kryssen nollas: massformen bär inga kryss, och en
    // rad som inte finns kvar kan inte vara kryssad.
    selected_.assign(std::size_t(rowCount()), 0);
    checked_.assign(std::size_t(rowCount()), 0);
    anchor_      = -1;
    hover_       = -1;
    pendingSolo_ = -1;   // ett gammalt radindex får aldrig välja en ny rad
    changedRow_  = -1;   // ListBoxens kontrakt efter `setItems`
    changedRows_.clear();
    contextRow_  = -1;   // högertryckets rad likaså
    checksChanged();
}

void ListView::rowAppended()
{
    selected_.push_back(0);   // en ny rad är aldrig vald …
    checked_.push_back(0);    // … och aldrig kryssad
    checksChanged();
}

// `setRow` ersätter radens DATA; valet och krysset är vyns tillstånd och står
// kvar (`ListBox::setItem`-prejudikatet).
void ListView::rowReplaced(int)
{
    pendingSolo_ = -1;   // raderna byttes under trycket — inget soloval
}

// `removeRow`: valet och krysset försvinner med raden, och varje radindex
// efter den räknas ned — ankaret, det väntande solovalet, kryssets rader och
// högertryckets rad följer sina rader; de som pekade på den borttagna släpps.
// Hovern är en PLATS, och platserna under pekaren har flyttat.
void ListView::rowRemoved(int row)
{
    const auto at = std::ptrdiff_t(row);
    if (std::size_t(row) < selected_.size())
        selected_.erase(selected_.begin() + at);
    if (std::size_t(row) < checked_.size())
        checked_.erase(checked_.begin() + at);
    const auto följ = [row](int& r) {
        if (r == row)
            r = -1;
        else if (r > row)
            --r;
    };
    följ(anchor_);
    följ(pendingSolo_);
    följ(changedRow_);
    följ(contextRow_);
    std::vector<int> kvar;
    for (int r : changedRows_) {
        följ(r);
        if (r >= 0)
            kvar.push_back(r);
    }
    changedRows_ = std::move(kvar);
    hover_       = -1;
    checksChanged();
    invalidate();
}

// Varje slagbyte ändrar vilka rader rubrikkrysset räknar — också en rad som
// BLIR en datarad —, och platserna kan ha flyttat. En rad som inte längre är
// en DATArad varken väljs eller kryssas.
void ListView::rowKindChanged(int row)
{
    checksChanged();
    hover_ = -1;
    invalidate();
    if (isDataRow(row))
        return;
    selected_[std::size_t(row)] = 0;
    checked_[std::size_t(row)]  = 0;
    if (pendingSolo_ == row)
        pendingSolo_ = -1;
    if (anchor_ == row)
        anchor_ = -1;
}

// En fällning DOLDE raderna: de lämnar valet (utan event — användarens
// fällning fyrar valets par själv, `toggleCategory`), och ankaret och ett
// väntande soloval på dem släpps. Kryssen står kvar.
void ListView::rowsHidden(const std::vector<int>& rows)
{
    bool ändrat = false;
    for (int r : rows) {
        if (selected_[std::size_t(r)]) {
            selected_[std::size_t(r)] = 0;
            ändrat                    = true;
        }
        if (pendingSolo_ == r)
            pendingSolo_ = -1;
        if (anchor_ == r)
            anchor_ = -1;
    }
    hover_ = -1;
    if (ändrat)
        hiddenDeselected_ = true;
    checksChanged();
    invalidate();
}

// Bara en SYNLIG DATArad kan väljas.
bool ListView::assignSelection(const std::vector<int>& rader)
{
    std::vector<char> nytt(std::size_t(rowCount()), 0);
    for (int r : rader)
        if (isDataRow(r) && displayRow(r) >= 0)
            nytt[std::size_t(r)] = 1;
    if (nytt == selected_)
        return false;
    selected_ = std::move(nytt);
    invalidate();
    return true;
}

void ListView::setMultiSelect(bool on)
{
    if (multiSelect_ == on)
        return;
    multiSelect_ = on;
    // Av med flera valda: bara EN får stå kvar — den aktuella om den är
    // vald, annars den första. Programmatiskt, alltså inget event.
    if (!on) {
        const int kvar = selectedIndex();
        assignSelection(kvar >= 0 ? std::vector<int>{kvar}
                                  : std::vector<int>{});
    }
    invalidate();
}

void ListView::setSelectedIndex(int row)
{
    if (row < 0) {
        clearSelection();
        return;
    }
    if (row >= rowCount()) {
        gfx::log("ListView(%s): setSelectedIndex(%d) utanför intervallet (%d "
                 "rader) — ignoreras", name.c_str(), row, rowCount());
        return;
    }
    if (!isDataRow(row) || displayRow(row) < 0) {
        gfx::log("ListView(%s): setSelectedIndex(%d) — raden är ingen synlig "
                 "datarad, ignoreras", name.c_str(), row);
        return;
    }
    assignSelection({row});
    anchor_ = row;
    setCurrent(row);
}

int ListView::selectedIndex() const
{
    const int akt = currentRow();
    if (akt >= 0 && isSelected(akt))
        return akt;
    for (int p = 0; p < positionCount(); ++p) {
        const int r = rowAtDisplay(p);
        if (selected_[std::size_t(r)])
            return r;
    }
    return -1;
}

void ListView::setSelected(int row, bool on)
{
    if (row < 0 || row >= rowCount()) {
        gfx::log("ListView(%s): setSelected(%d) utanför intervallet (%d "
                 "rader) — ignoreras", name.c_str(), row, rowCount());
        return;
    }
    if (on && (!isDataRow(row) || displayRow(row) < 0)) {
        gfx::log("ListView(%s): setSelected(%d) — raden är ingen synlig "
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

bool ListView::isSelected(int row) const
{
    return row >= 0 && std::size_t(row) < selected_.size() &&
           selected_[std::size_t(row)];
}

std::vector<int> ListView::selectedRows() const
{
    std::vector<int> ut;
    for (int p = 0; p < positionCount(); ++p) {
        const int r = rowAtDisplay(p);
        if (selected_[std::size_t(r)])
            ut.push_back(r);
    }
    return ut;
}

// "ALLA" är de synliga dataraderna — kategori- och tomrader väljs aldrig, och
// en dold rad lämnade valet när den doldes.
std::vector<int> ListView::allRows() const
{
    std::vector<int> alla;
    for (int p = 0; p < positionCount(); ++p) {
        const int r = rowAtDisplay(p);
        if (isDataRow(r))
            alla.push_back(r);
    }
    return alla;
}

void ListView::selectAll()
{
    if (!multiSelect_)
        return;   // enkelval har ingen "alla"
    assignSelection(allRows());
}

void ListView::clearSelection()
{
    assignSelection({});
}

// --------------------------------------------------------------- kryssen

void ListView::setShowChecks(bool on)
{
    if (showChecks_ == on)
        return;
    showChecks_ = on;
    // Zonen är kolumn 0:s innehåll i varje cell, och rubrikkrysset kommer
    // eller går med den.
    invalidateColumnWidths(0);
    invalidate();   // rutan tar plats — ikonen och texten flyttar
}

void ListView::setChecked(int row, bool on)
{
    if (row < 0 || row >= rowCount()) {
        gfx::log("ListView(%s): setChecked(%d) utanför intervallet (%d "
                 "rader) — ignoreras", name.c_str(), row, rowCount());
        return;
    }
    if (on && !isDataRow(row)) {
        gfx::log("ListView(%s): setChecked(%d) — raden är ingen datarad, "
                 "ignoreras", name.c_str(), row);
        return;
    }
    if (bool(checked_[std::size_t(row)]) == on)
        return;   // hård regel 1: invalidera vid ÄNDRING
    checked_[std::size_t(row)] = on ? 1 : 0;
    checksChanged();
    invalidate();
}

bool ListView::isChecked(int row) const
{
    return row >= 0 && std::size_t(row) < checked_.size() &&
           checked_[std::size_t(row)];
}

// De synliga i visningsordningen, och sedan de kryssade rader som en
// ihopfälld kategori döljer, i appens ordning — krysset står kvar på en dold
// rad.
std::vector<int> ListView::checkedRows() const
{
    std::vector<int> ut;
    for (int p = 0; p < positionCount(); ++p) {
        const int r = rowAtDisplay(p);
        if (checked_[std::size_t(r)])
            ut.push_back(r);
    }
    for (int r = 0; r < rowCount(); ++r)
        if (checked_[std::size_t(r)] && displayRow(r) < 0)
            ut.push_back(r);
    return ut;
}

// RUBRIKKRYSSET (list_view.h): bara i kolumn 0 och bara med synliga rutor;
// läget räknas över de SYNLIGA dataraderna.
std::optional<CheckState> ListView::headerCheckState(int column) const
{
    if (column != 0 || !showChecks_)
        return std::nullopt;
    HeaderState& k = headerState_;
    if (k.valid && k.gen == checkGen_ && k.positions == positionCount())
        return k.state;
    int alla = 0, kryss = 0;
    for (int p = 0; p < positionCount(); ++p) {
        const int r = rowAtDisplay(p);
        if (!isDataRow(r))
            continue;
        ++alla;
        if (checked_[std::size_t(r)])
            ++kryss;
    }
    const CheckState s = kryss == 0      ? CheckState::Unchecked
                         : kryss == alla ? CheckState::Checked
                                         : CheckState::Indeterminate;
    k = HeaderState{checkGen_, positionCount(), s, true};
    return s;
}

// En VÄNDNING: alla kryssade blir okryssade, annars (inga eller några) blir
// alla kryssade — EN `onCheck` med raderna i `changedRows()`, följd av
// `onCommit`, och raderna SÄTTS IGEN emellan (`toggleCheckFromUser`s regel).
bool ListView::headerCheckToggled(int column)
{
    const auto läge = headerCheckState(column);
    if (!läge)
        return true;
    const bool       på = *läge != CheckState::Checked;
    std::vector<int> ändrade;
    for (int p = 0; p < positionCount(); ++p) {
        const int r = rowAtDisplay(p);
        if (isDataRow(r) && bool(checked_[std::size_t(r)]) != på) {
            checked_[std::size_t(r)] = på ? 1 : 0;
            ändrade.push_back(r);
        }
    }
    if (ändrade.empty())
        return true;
    checksChanged();
    invalidate();
    changedRow_  = -1;
    changedRows_ = ändrade;
    if (!fireEvent(onCheck))
        return false;
    changedRow_  = -1;
    changedRows_ = std::move(ändrade);
    return fireEvent(onCommit);
}

bool ListView::setProp(const std::string& key, const std::string& value)
{
    if (key == "selectedIndex") {
        setSelectedIndex(std::atoi(value.c_str()));   // oparsbart ⇒ 0
        return true;
    }
    if (key == "multiSelect") {
        setMultiSelect(propparse::parseBool(value));
        return true;
    }
    if (key == "showChecks") {
        setShowChecks(propparse::parseBool(value));
        return true;
    }
    return Table::setProp(key, value);   // basen SIST — konventionen
}

// --------------------------------------------------------- användarvägarna
// De ENDA platser som fyrar event. `ListBox::selectFromUser` är
// prejudikatet: en användarväg, högst en avfyrning per handling.

bool ListView::selectFromUser(int position, std::uint32_t mods, bool fromKey)
{
    if (position < 0 || position >= positionCount())
        return false;
    const int rad = rowAtDisplay(position);
    if (rowKind(rad) == RowKind::Blank)
        return false;   // en tomrad blir aldrig aktuell
    const bool shift = multiSelect_ && (mods & gfx::kModShift);
    // Ctrl ELLER Cmd: på macOS är Ctrl+klick högerklick, så Cmd är vändaren
    // där — och att båda räknas överallt kostar ingenting.
    const bool vänd  = multiSelect_ && (mods & (gfx::kModCtrl | gfx::kModCmd));

    bool ändrat = false;
    if (shift) {
        // SPANNET i visningsordningen, från ankaret (eller den aktuella
        // raden, eller raden själv) till hit — bara dataraderna i det.
        // Ctrl+Shift+klick LÄGGER TILL spannet; utan Ctrl ersätter det valet.
        const int ankare = anchor_ >= 0     ? anchor_
                           : currentRow() >= 0 ? currentRow()
                                               : rad;
        const int från = displayRow(ankare) >= 0 ? displayRow(ankare) : position;
        const int a = std::min(från, position);
        const int b = std::max(från, position);
        std::vector<int> rader = (vänd && !fromKey) ? selectedRows()
                                                     : std::vector<int>{};
        for (int p = a; p <= b; ++p)
            rader.push_back(rowAtDisplay(p));   // `assignSelection` filtrerar
        ändrat = assignSelection(rader);
        anchor_ = ankare;
    } else if (!isDataRow(rad)) {
        // En KATEGORIrad blir bara aktuell — den väljs aldrig, och valet
        // står kvar.
        setCurrent(rad);
        ensureVisible(rad);
        return false;
    } else if (vänd) {
        // Ctrl+pil flyttar bara den aktuella raden — valet står kvar, och
        // Ctrl+Space vänder därefter raden. Ctrl+klick vänder den direkt.
        if (!fromKey) {
            selected_[std::size_t(rad)] = selected_[std::size_t(rad)] ? 0 : 1;
            ändrat = true;
            invalidate();
        }
        anchor_ = rad;
    } else {
        ändrat  = assignSelection({rad});
        anchor_ = rad;
    }
    setCurrent(rad);
    // En REN valhändelse är ingen kryssning: `changedRow` släpper sitt gamla
    // värde (ListBoxens kontrakt — trädets `changedPath` likadant).
    changedRow_ = -1;
    changedRows_.clear();
    ensureVisible(rad);
    if (!ändrat)
        return false;
    fireChange();   // valet läses ur sender (regel 6)
    return true;
}

// `onChange` och sedan `onCommit` — samma val, med vakten emellan.
void ListView::fireChange()
{
    firePair(onChange, onCommit);
}

// KRYSSET (förlagan `ListBox::toggleCheckFromUser`): vänd,
// och `onCheck` följt av `onCommit` för samma kryss — raden SÄTTS IGEN
// emellan, för en onCheck-handler som fyller om listan har nollat den. Ett
// kryss FLYTTAR INTE VALET: de två är olika handlingar.
bool ListView::toggleCheckFromUser(int row)
{
    if (!isDataRow(row))
        return true;   // en kategori- eller tomrad kryssas aldrig
    checked_[std::size_t(row)] = checked_[std::size_t(row)] ? 0 : 1;
    changedRow_  = row;
    changedRows_ = {row};
    checksChanged();
    invalidate();
    if (!fireEvent(onCheck))   // raden läses ur sender: changedRow()
        return false;
    changedRow_  = row;
    changedRows_ = {row};
    return fireEvent(onCommit);
}

// ANVÄNDARENS FÄLLNING: tabellens väg (`onExpand`/`onCollapse`), och när en
// ihopfällning valde bort dolda rader valets par efter den — trädets regel.
bool ListView::toggleCategory(int row)
{
    hiddenDeselected_ = false;
    hover_            = -1;   // platserna under pekaren flyttar
    if (!toggleCategoryFromUser(row))
        return false;
    if (!hiddenDeselected_)
        return true;
    hiddenDeselected_ = false;
    changedRow_       = -1;   // en valhändelse
    changedRows_.clear();
    return firePair(onChange, onCommit);
}

void ListView::activateFromUser()
{
    if (!isDataRow(currentRow()) || !onActivate)
        return;
    ActivateArgs e;
    onActivate(this, e);   // raden läses ur sender: currentRow()
}

// ------------------------------------------------------ kryssets geometri
// EN sanning i widget-lokala LOGISKA px: zonen är tabellens (`leadZone` —
// cellens luft in i första kolumnen, `leadW` bred, radens hela höjd, och den
// följer kolumn 0 i sidled), rutan sitter v-centrerad i den. ListBoxens
// `checkRect`/`checkZone` är prejudikatet.

float ListView::leadW(const Ctx& c) const
{
    return showChecks_ ? c.style->checkSize + c.style->checkGap : 0.0f;
}

gfx::Rectf ListView::checkRect(const Ctx& c, int position) const
{
    const gfx::Rectf zon = leadZone(c, position);
    const float      s   = c.style->checkSize;
    return gfx::Rectf{zon.x, zon.y + (zon.h - s) / 2.0f, s, s};
}

// Träffar ett tryck (fysiska px) rutans ZON? Bara när kryssen syns, och bara
// inom radernas yta — en zon som rullats in under huvudet eller ut ur
// synfältet i sidled träffas inte (`Table::columnAt`s projektion). Radernas
// yta räknas från innehållsytans vänsterkant: med radrubriker står
// rutan i den fasta remsan, som ligger utanför rullningsytan.
bool ListView::onCheckZone(const Ctx& c, int position, float mx,
                           float my) const
{
    if (!showChecks_)
        return false;
    const gfx::Rectf base = resolve(c);
    const float      lx   = Draw::localX(c, base, mx);
    const float      ly   = Draw::localY(c, base, my);
    const gfx::Rectf in   = innerBounds(c);
    const gfx::Rectf vp   = scrollViewport(c);
    return gfx::Rectf{in.x, vp.y, in.w, vp.h}.contains(lx, ly) &&
           leadZone(c, position).contains(lx, ly);
}

// Rutan i TEMATS pennor — kanten och, när raden är kryssad, bocken (den
// delade cellrutan). Färgerna väljer ritningens recept per UI (trädets),
// aldrig radens app-färg. Utan kolumner finns ingen första cell att rita den
// i, och en kategori- eller tomrad har ingen ruta.
void ListView::drawCheck(const Ctx& c, int position, int row, gfx::Color bock,
                         gfx::Color kant) const
{
    if (!showChecks_ || columnCount() == 0 || !isDataRow(row))
        return;
    valuecell::drawCheckBox(c, checkRect(c, position),
                            isChecked(row) ? CheckState::Checked
                                           : CheckState::Unchecked,
                            kant, bock);
}

// Märkena i radrubrikens remsa: remsan tonas aldrig av valet, så
// rutan ritas i rubrikens färger — kanten som i alla tre recepten för en ovald
// rad, bocken i rubrikens textfärg — och Minimalistics valstreck, valets enda
// yta i den familjen, står kvar vid radens vänsterkant (receptet står i
// `draw`).
void ListView::drawRowHeaderMarks(const Ctx& c, int position, int row,
                                  gfx::Color fg) const
{
    const auto& col = c.theme->colors;
    if (c.ui == UI::Minimalistic && isSelected(row)) {
        const gfx::Rectf r = rowRect(c, position);
        Draw::rect(c, {r.x, r.y, c.style->rowMarkerW, r.h}, 0.0f,
                   isEnabled() ? col.accent : col.textDim);
    }
    drawCheck(c, position, row, fg,
              isEnabled() ? col.buttonBorder : col.border);
}

// ------------------------------------------------------------- beteendet

void ListView::setHover(int position)
{
    if (position == hover_)
        return;
    hover_ = position;
    invalidate();   // hård regel 1: bara vid ÄNDRING
}

// Trycket, i ordningen: huvudet och remsorna (tabellens) → en KATEGORIrad
// (fälls, och blir aktuell) eller en TOMrad (ingenting) → RUTANS ZON
// (vänder krysset; valet, den aktuella raden och solovalet rörs inte) →
// solovalet → valet.
void ListView::mouseDown(const Ctx& c, float mx, float my)
{
    pendingSolo_ = -1;   // ett gammalt väntande soloval släpps
    if (headerMouseDown(c, mx, my))
        return;   // remsan eller huvudet tog trycket
    const int p = positionAt(c, my);
    if (p < 0)
        return;   // nedanför sista raden: ingenting (ListBox-beslutet)
    const int rad = rowAtDisplay(p);
    if (rowKind(rad) == RowKind::Blank)
        return;
    if (rowKind(rad) == RowKind::Category) {
        setCurrent(rad);
        toggleCategory(rad);
        return;
    }
    if (onCheckZone(c, p, mx, my)) {
        toggleCheckFromUser(rad);
        return;
    }
    const AppWindow*    w    = appWindow();
    const std::uint32_t mods = w ? w->modifierKeys() : 0u;
    if (multiSelect_ && isSelected(rad) &&
        !(mods & (gfx::kModCtrl | gfx::kModCmd | gfx::kModShift))) {
        // SOLO VID SLÄPPET: trycket gör bara raden aktuell — annars
        // hade det valt bort de andra innan ett drag av hela valet startat.
        pendingSolo_ = rad;
        setCurrent(rad);
        return;
    }
    selectFromUser(p, mods, false);
}

bool ListView::mouseUp(const Ctx& c, float mx, float my)
{
    const bool bas = Table::mouseUp(c, mx, my);   // huvudet först
    const int  rad = pendingSolo_;
    pendingSolo_   = -1;
    if (rad < 0 || rad >= rowCount())
        return bas;
    return selectFromUser(displayRow(rad), 0u, false) || bas;
}

// VAR FÅR ETT DRAG BÖRJA? Inte i remsorna (de rullar), inte i huvudet
// (en avdelare drar en kolumnbredd, en rubrik sorterar) och inte i rutans
// zon (den kryssar) — samma frågor som trycket ställer. Lasten är alltid
// appens, så utan svaret här hade ett avdelardrag blivit ett raddrag.
bool ListView::canDragFrom(const Ctx& c, float x, float y) const
{
    if (inScrollStrip(c, x, y) || inHeader(c, x, y))
        return false;
    const int p = positionAt(c, y);
    if (p < 0)
        return true;
    // En kategorirad fälls av trycket och en tomrad är luft — inget raddrag.
    return isDataRow(rowAtDisplay(p)) && !onCheckZone(c, p, x, y);
}

void ListView::dragStart(const Ctx& c, DragStartArgs& e)
{
    pendingSolo_ = -1;   // draget tar trycket — ingen `mouseUp` kommer
    Table::dragStart(c, e);
}

// HÖGERTRYCKET VÄLJER SOM UTFORSKAREN (trädets
// regel): huvudet, remsorna och tom yta är "ingen", en rad utanför valet
// väljs ensam genom användarvägen, en rad i valet blir bara aktuell. Ingen
// fokusflytt, och svaret är ALLTID false — snabbmenyn fyras.
bool ListView::rightDown(const Ctx& c, float mx, float my)
{
    const int p = hitPosition(c, mx, my);
    if (p < 0) {
        contextRow_ = -1;
        return false;
    }
    const int rad = rowAtDisplay(p);
    if (rowKind(rad) == RowKind::Blank) {
        contextRow_ = -1;   // en tomrad är tom yta
        return false;
    }
    contextRow_ = rad;
    if (!isDataRow(rad)) {
        setCurrent(rad);   // en kategorirad väljs aldrig
        return false;
    }
    if (!isSelected(rad)) {
        HandlerGuard vakt(*this);
        const bool   fyrat = selectFromUser(p, 0u, false);
        if (!vakt.ok())
            return false;   // en handler rev eller lossade listan
        if (fyrat)
            appWindow()->invalidate();   // en handler har kört (widget.h)
    } else {
        setCurrent(rad);   // invaliderar vid ändring
    }
    return false;
}

// DUBBELKLICKET aktiverar raden det andra trycket landade på — `mouseDown`
// har redan gjort den aktuell (och med Ctrl kanske vänt den; aktiveringen
// frågar inte efter valet utan efter vilken rad som avses). I rutans ZON har
// båda trycken redan vänt krysset — två vändningar, och ingen aktivering.
void ListView::mouseDoubleClick(const Ctx& c, float mx, float my)
{
    if (headerDoubleClick(c, mx, my))
        return;   // remsan eller huvudet — en avdelare anpassar sin kolumn
    // En kategorirad har fällts av båda trycken — ingen aktivering.
    const int p = positionAt(c, my);
    if (p >= 0 && isDataRow(rowAtDisplay(p)) && !onCheckZone(c, p, mx, my) &&
        rowAtDisplay(p) == currentRow())
        activateFromUser();
}

void ListView::mouseMove(const Ctx& c, float mx, float my)
{
    Table::mouseMove(c, mx, my);   // huvudets hover och remsornas tumme
    const bool påRad = !inScrollStrip(c, mx, my) && !inHeader(c, mx, my);
    int        p     = påRad ? positionAt(c, my) : -1;
    if (p >= 0 && rowKind(rowAtDisplay(p)) == RowKind::Blank)
        p = -1;   // en tomrad får ingen hover
    setHover(p);
}

bool ListView::keyDown(gfx::Key k, std::uint32_t mods)
{
    if (mods & gfx::kModAlt)
        return false;   // Alt är menyernas och appens
    const bool styr = mods & (gfx::kModCtrl | gfx::kModCmd);
    // Utan flerval har Ctrl ingen mening här — tangenten lämnas åt
    // genvägarna, precis som ListBox gör.
    if (styr && !multiSelect_)
        return false;
    if (positionCount() == 0)
        return false;
    const int  från     = displayRow(currentRow());
    const bool kategori = currentRow() >= 0 &&
                          rowKind(currentRow()) == RowKind::Category;

    // EN AKTUELL KATEGORIRAD: Enter och Space fäller den, Vänster fäller ihop
    // och Höger ut (en redan ihopfälld respektive utfälld bubblar).
    if (kategori) {
        const bool ut = isExpanded(currentRow());
        if (k == gfx::Key::Return || k == gfx::Key::Space ||
            (k == gfx::Key::Left && ut) || (k == gfx::Key::Right && !ut)) {
            toggleCategory(currentRow());
            return true;
        }
    }

    switch (k) {
    case gfx::Key::Return:
        // Tangenten är vår bara när den GÖR något: utan
        // aktuell rad eller utan `onActivate` bubblar den — en "välj en post,
        // tryck Enter"-dialog når då sin standardknapp.
        if (currentRow() < 0 || !onActivate)
            return false;
        activateFromUser();
        return true;
    case gfx::Key::Space:
        if (currentRow() < 0)
            return false;
        // TRÄDETS REGEL: med synliga rutor vänder Space den aktuella
        // radens KRYSS — också i enkelval, och före valet. Ctrl+Space vänder
        // VALET (bara i flerval — enkelvalet har bubblat ovan).
        if (showChecks_ && !styr) {
            toggleCheckFromUser(currentRow());
            return true;
        }
        if (styr) {
            selectFromUser(från, gfx::kModCtrl, false);   // vänd raden
        } else if (!isSelected(currentRow())) {
            selectFromUser(från, 0u, true);
        }
        return true;
    case gfx::Key::A:
        if (!styr)
            return false;
        // Ctrl+A är en ANVÄNDARväg: samma val som `selectAll`, men med
        // event när valet faktiskt ändrades — och en ren valhändelse, så
        // `changedRow` släpps före paret.
        if (assignSelection(allRows())) {
            changedRow_ = -1;
            changedRows_.clear();
            fireChange();
        }
        return true;
    default:
        break;
    }

    if (!appWindow() || !appWindow()->isOpen())
        return false;   // sidan kräver ett Ctx, och ett Ctx ett öppet fönster
    const int mål = navigationTarget(behaviorCtx(), k, från);
    if (mål < 0)
        return false;
    selectFromUser(mål, mods, true);
    return true;
}

// ----------------------------------------------------------- ritningen

void ListView::draw(const Ctx& c)
{
    const gfx::Rectf b      = c.bounds();
    const gfx::Rectf vp     = scrollViewport(c);
    const auto&      m      = *c.style;
    const auto&      col    = c.theme->colors;
    const bool       på     = isEnabled();
    // Hovern gäller bara medan pekaren FAKTISKT är över listan (ListBox).
    const int        hov    = hovered() && på ? hover_ : -1;
    // Radloopen startar vid FÖRSTA SYNLIGA platsen och bryter vid
    // nederkanten — O(synliga), som ListBox.
    const int        första = firstVisiblePosition(c);
    const int        n      = positionCount();
    // Den AKTUELLA raden ramas in när den kan skilja sig från valet —
    // alltså med flerval, eller när den är en kategorirad (som aldrig väljs),
    // och bara med fokus (Utforskarens fokusram). Ramen ritas efter
    // kategoriraderna; en kategorirads ram spänner som raden över
    // radrubrikens remsa.
    const bool       kategori = currentRow() >= 0 &&
                                rowKind(currentRow()) == RowKind::Category;
    const int        aktuell  = focused() && på && (multiSelect_ || kategori)
                                    ? displayRow(currentRow())
                                    : -1;
    const gfx::Rectf in       = innerBounds(c);
    const auto       ramaAktuell = [&](gfx::Color färg) {
        if (aktuell < 0)
            return;
        Draw::clip(c, kategori ? gfx::Rectf{in.x, vp.y, in.w, vp.h} : vp);
        Draw::frame(c, rowRect(c, aktuell), 0.0f, Pen{färg, m.borderWidth});
        Draw::endClip(c);
    };

    switch (c.ui) {
    // GLAZE — listans recept (ListBox): `panel`-yta och BESTÄMD kontur
    // `buttonBorder`; den valda raden fylls med det GRINDADE paret
    // `accentSurface`/`onAccent` och lamineras INTE (en rad har ingen kontur,
    // och lamineringen kräver en — 2.91:1 mätt i ListBox). Kryssrutan är
    // trädets: kanten `buttonBorder`, bocken i radens TEMAfärg — på den
    // valda raden båda i `onAccent`.
    //
    //   vila     ingen radfyllning, text `text` eller RADENS färg (appens)
    //   hover    `buttonHover` (SVAG — pekaren är inte ett val)
    //   vald     `accentSurface`-rad, text `onAccent`
    //   fokus    fristående ring utanför kanten; med flerval en hårlinje
    //            runt den AKTUELLA raden i radens textfärg
    //   inaktiv  markeringen faller till `buttonDisabled`, all text `textDim`,
    //            rutans kant `border`
    case UI::Glaze:
        Draw::rect(c, b, 0.0f, col.panel);
        Draw::clip(c, vp);
        for (int p = första; p < n; ++p) {
            const gfx::Rectf r = rowRect(c, p);
            if (r.y >= vp.y + vp.h)
                break;
            const int  rad  = rowAtDisplay(p);
            if (!isDataRow(rad))
                continue;   // kategori- och tomrader: `drawSpanRows`
            const bool vald = isSelected(rad);
            if (vald)
                Draw::rect(c, r, 0.0f, på ? col.accentSurface
                                          : col.buttonDisabled);
            else if (p == hov)
                Draw::rect(c, r, 0.0f, col.buttonHover);
            const gfx::Color fg = !på    ? col.textDim
                                  : vald ? col.onAccent
                                         : col.text;
            drawCells(c, p, rad, fg, på && !vald);
            drawCheck(c, p, rad, fg,
                      vald ? fg : på ? col.buttonBorder : col.border);
        }
        Draw::endClip(c);
        drawRowHeaders(c);
        drawSpanRows(c);
        ramaAktuell(isSelected(currentRow()) ? col.onAccent : col.text);
        drawHeader(c);
        Draw::frame(c, b, 0.0f,
                    Pen{på ? col.buttonBorder : col.border, m.borderWidth});
        break;

    // MINIMALISTIC — "Outline" i listform (ListBox): ingen yta i vila och
    // INGEN radfyllning för valet, som bärs av ett accent-STRECK i
    // vänsterkanten (`rowMarkerW`) och av textfärgen — en rad är ingen
    // kontroll, så "fyllning betyder tillstånd" lånas inte hit. Kryssrutans
    // kant är alltid `buttonBorder` (trädets och ListBoxens), bocken i radens
    // TEMAfärg — `accent` på den valda.
    //
    //   vila     ingen yta, ingen radfyllning, text `text` eller RADENS färg
    //   hover    `buttonHover` innanför kanten
    //   vald     accent-markör i vänsterkanten, text `accent`
    //   fokus    fristående ring; med flerval en hårlinje runt den aktuella
    //   inaktiv  markör och text `textDim`, kanten och rutans kant `border`
    case UI::Minimalistic:
        Draw::clip(c, vp);
        for (int p = första; p < n; ++p) {
            const gfx::Rectf r = rowRect(c, p);
            if (r.y >= vp.y + vp.h)
                break;
            const int  rad  = rowAtDisplay(p);
            if (!isDataRow(rad))
                continue;   // kategori- och tomrader: `drawSpanRows`
            const bool vald = isSelected(rad);
            if (p == hov)
                Draw::rect(c, r, 0.0f, col.buttonHover);
            const gfx::Color fg = !på    ? col.textDim
                                  : vald ? col.accent
                                         : col.text;
            if (vald)
                Draw::rect(c, {r.x, r.y, m.rowMarkerW, r.h}, 0.0f, fg);
            drawCells(c, p, rad, fg, på && !vald);
            drawCheck(c, p, rad, fg, på ? col.buttonBorder : col.border);
        }
        Draw::endClip(c);
        drawRowHeaders(c);
        drawSpanRows(c);
        ramaAktuell(isSelected(currentRow()) ? col.accent : col.text);
        drawHeader(c);
        Draw::frame(c, b, 0.0f,
                    Pen{på ? col.buttonBorder : col.border, m.borderWidth});
        break;

    // SLATE — ett FÄLT (ListBox): `panel`-yta, hårlinje `border`, och den
    // VALDA raden fylls med `accent` medan texten går till `windowBg`.
    // Kryssrutan som Glazes: kanten `buttonBorder`, bocken i radens
    // TEMAfärg — på den valda raden båda i `windowBg`.
    //
    //   vila     ingen radfyllning, text `text` eller RADENS färg (appens)
    //   hover    `buttonHover`
    //   vald     `accent`-rad, text `windowBg`
    //   fokus    fristående ring; med flerval en hårlinje runt den aktuella
    //   inaktiv  markeringen faller till `buttonDisabled`, all text `textDim`,
    //            rutans kant `border`
    case UI::Slate:
    default:
        Draw::rect(c, b, 0.0f, col.panel);
        Draw::clip(c, vp);
        for (int p = första; p < n; ++p) {
            const gfx::Rectf r = rowRect(c, p);
            if (r.y >= vp.y + vp.h)
                break;
            const int  rad  = rowAtDisplay(p);
            if (!isDataRow(rad))
                continue;   // kategori- och tomrader: `drawSpanRows`
            const bool vald = isSelected(rad);
            if (vald)
                Draw::rect(c, r, 0.0f, på ? col.accent : col.buttonDisabled);
            else if (p == hov)
                Draw::rect(c, r, 0.0f, col.buttonHover);
            const gfx::Color fg = !på    ? col.textDim
                                  : vald ? col.windowBg
                                         : col.text;
            drawCells(c, p, rad, fg, på && !vald);
            drawCheck(c, p, rad, fg,
                      vald ? fg : på ? col.buttonBorder : col.border);
        }
        Draw::endClip(c);
        drawRowHeaders(c);
        drawSpanRows(c);
        ramaAktuell(isSelected(currentRow()) ? col.windowBg : col.text);
        drawHeader(c);
        Draw::frame(c, b, 0.0f, Pen{col.border, m.borderWidth});
        break;
    }

    if (focused() && på)
        Draw::frame(c, Draw::inset(b, -m.focusOffset), m.focusOffset,
                    Pen{col.accent, m.focusWidth});
}

} // namespace glaze
