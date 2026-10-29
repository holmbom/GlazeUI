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
#include <numeric>

#include "glaze/app_window.h"
#include "glaze/draw.h"

#include "../prop_parse.h"

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
    contextRow_  = -1;   // högertryckets rad likaså
}

void ListView::rowAppended()
{
    selected_.push_back(0);   // en ny rad är aldrig vald …
    checked_.push_back(0);    // … och aldrig kryssad
}

// `setRow` ersätter radens DATA; valet och krysset är vyns tillstånd och står
// kvar (`ListBox::setItem`-prejudikatet).
void ListView::rowReplaced(int)
{
    pendingSolo_ = -1;   // raderna byttes under trycket — inget soloval
}

bool ListView::assignSelection(const std::vector<int>& rader)
{
    std::vector<char> nytt(std::size_t(rowCount()), 0);
    for (int r : rader)
        if (r >= 0 && r < rowCount())
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
    assignSelection({row});
    anchor_ = row;
    setCurrent(row);
}

int ListView::selectedIndex() const
{
    const int akt = currentRow();
    if (akt >= 0 && isSelected(akt))
        return akt;
    for (int p = 0; p < rowCount(); ++p) {
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
    for (int p = 0; p < rowCount(); ++p) {
        const int r = rowAtDisplay(p);
        if (selected_[std::size_t(r)])
            ut.push_back(r);
    }
    return ut;
}

std::vector<int> ListView::allRows() const
{
    std::vector<int> alla(static_cast<std::size_t>(rowCount()));
    std::iota(alla.begin(), alla.end(), 0);
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
    contentChanged();   // zonen är kolumn 0:s innehåll
    invalidate();   // rutan tar plats — ikonen och texten flyttar
}

void ListView::setChecked(int row, bool on)
{
    if (row < 0 || row >= rowCount()) {
        gfx::log("ListView(%s): setChecked(%d) utanför intervallet (%d "
                 "rader) — ignoreras", name.c_str(), row, rowCount());
        return;
    }
    if (bool(checked_[std::size_t(row)]) == on)
        return;   // hård regel 1: invalidera vid ÄNDRING
    checked_[std::size_t(row)] = on ? 1 : 0;
    invalidate();
}

bool ListView::isChecked(int row) const
{
    return row >= 0 && std::size_t(row) < checked_.size() &&
           checked_[std::size_t(row)];
}

std::vector<int> ListView::checkedRows() const
{
    std::vector<int> ut;
    for (int p = 0; p < rowCount(); ++p) {
        const int r = rowAtDisplay(p);
        if (checked_[std::size_t(r)])
            ut.push_back(r);
    }
    return ut;
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
    if (position < 0 || position >= rowCount())
        return false;
    const int  rad   = rowAtDisplay(position);
    const bool shift = multiSelect_ && (mods & gfx::kModShift);
    // Ctrl ELLER Cmd: på macOS är Ctrl+klick högerklick, så Cmd är vändaren
    // där — och att båda räknas överallt kostar ingenting.
    const bool vänd  = multiSelect_ && (mods & (gfx::kModCtrl | gfx::kModCmd));

    bool ändrat = false;
    if (shift) {
        // SPANNET i visningsordningen, från ankaret (eller den aktuella
        // raden, eller raden själv) till hit. Ctrl+Shift+klick LÄGGER TILL
        // spannet; utan Ctrl ersätter det valet.
        const int ankare = anchor_ >= 0     ? anchor_
                           : currentRow() >= 0 ? currentRow()
                                               : rad;
        const int a = std::min(displayRow(ankare), position);
        const int b = std::max(displayRow(ankare), position);
        std::vector<int> rader = (vänd && !fromKey) ? selectedRows()
                                                     : std::vector<int>{};
        for (int p = a; p <= b; ++p)
            rader.push_back(rowAtDisplay(p));
        ändrat = assignSelection(rader);
        anchor_ = ankare;
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
    if (row < 0 || row >= rowCount())
        return true;
    checked_[std::size_t(row)] = checked_[std::size_t(row)] ? 0 : 1;
    changedRow_ = row;
    invalidate();
    if (!fireEvent(onCheck))   // raden läses ur sender: changedRow()
        return false;
    changedRow_ = row;
    return fireEvent(onCommit);
}

void ListView::activateFromUser()
{
    if (currentRow() < 0 || !onActivate)
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

// Rutan i TEMATS pennor — kanten och, när raden är kryssad, bocken. Färgerna
// väljer ritningens recept per UI (trädets), aldrig radens app-färg. Utan
// kolumner finns ingen första cell att rita den i.
void ListView::drawCheck(const Ctx& c, int position, int row, gfx::Color bock,
                         gfx::Color kant) const
{
    if (!showChecks_ || columnCount() == 0)
        return;
    const gfx::Rectf cr = checkRect(c, position);
    Draw::frame(c, cr, c.style->checkRadius, Pen{kant, c.style->borderWidth});
    if (isChecked(row))
        Draw::check(c, cr, Pen{bock, c.style->checkStroke});
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

// Trycket, i ordningen: huvudet och remsorna (tabellens) → RUTANS ZON
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
    if (onCheckZone(c, p, mx, my)) {
        toggleCheckFromUser(rowAtDisplay(p));
        return;
    }
    const AppWindow*    w    = appWindow();
    const std::uint32_t mods = w ? w->modifierKeys() : 0u;
    const int           rad  = rowAtDisplay(p);
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
    return p < 0 || !onCheckZone(c, p, x, y);
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
    contextRow_   = rad;
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
    const int p = positionAt(c, my);
    if (p >= 0 && !onCheckZone(c, p, mx, my) &&
        rowAtDisplay(p) == currentRow())
        activateFromUser();
}

void ListView::mouseMove(const Ctx& c, float mx, float my)
{
    Table::mouseMove(c, mx, my);   // huvudets hover och remsornas tumme
    const bool påRad = !inScrollStrip(c, mx, my) && !inHeader(c, mx, my);
    setHover(påRad ? positionAt(c, my) : -1);
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
    if (rowCount() == 0)
        return false;
    const int från = displayRow(currentRow());

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
    const int        n      = rowCount();
    // Den AKTUELLA raden ramas in när den kan skilja sig från valet —
    // alltså bara med flerval, och bara med fokus (Utforskarens fokusram).
    const int        aktuell = focused() && på && multiSelect_
                                   ? displayRow(currentRow())
                                   : -1;

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
        if (aktuell >= 0)
            Draw::frame(c, rowRect(c, aktuell), 0.0f,
                        Pen{isSelected(currentRow()) ? col.onAccent : col.text,
                            m.borderWidth});
        Draw::endClip(c);
        drawRowHeaders(c);
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
        if (aktuell >= 0)
            Draw::frame(c, rowRect(c, aktuell), 0.0f,
                        Pen{isSelected(currentRow()) ? col.accent : col.text,
                            m.borderWidth});
        Draw::endClip(c);
        drawRowHeaders(c);
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
        if (aktuell >= 0)
            Draw::frame(c, rowRect(c, aktuell), 0.0f,
                        Pen{isSelected(currentRow()) ? col.windowBg : col.text,
                            m.borderWidth});
        Draw::endClip(c);
        drawRowHeaders(c);
        drawHeader(c);
        Draw::frame(c, b, 0.0f, Pen{col.border, m.borderWidth});
        break;
    }

    if (focused() && på)
        Draw::frame(c, Draw::inset(b, -m.focusOffset), m.focusOffset,
                    Pen{col.accent, m.focusWidth});
}

} // namespace glaze
