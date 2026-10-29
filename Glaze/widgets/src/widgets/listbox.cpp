// ListBox — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER: Draw ritar, c.style bär måtten och temat färgerna. Rad-geometrin
// bor HÄR i LOGISKA px (rowRect/checkRect/textX) — EN sanning som
// ritningen läser rakt av och som musvägen projicerar till fysiska px.
// Basen Panel ger klipp, hjulscroll, overlay-remsorna och innehållsytan;
// contentExtent överrids så radhöjden (data, inte barn) styr scrollen.
//
// Dropdownens popup-lista (`DropdownRows`, dropdown.cpp) är en SUBKLASS till
// den här — samma geometri, samma tre recept. Det som skiljer dem är
// tillstånd→färg-mappningen: i en popup är pekarens rad den starka
// markeringen, i en listbox är det VALDA raden. Modellen som löser det
// står i listbox.h.
//
// SKRIVNA BESLUT (de långa motiveringarna står i listbox.h):
//   radie 0 · ingen autostorlek · ingen horisontell scroll · ingen ellips
//   · ingen PageUp/PageDown · ingen numpad-gren · ingen typeahead · flervalet
//   är listvyns, och enkelvalet står orört bredvid.

#include "glaze/widgets/listbox.h"

#include <algorithm>
#include <cmath>

#include "glaze/app_window.h"
#include "glaze/draw.h"

#include "../font_set.h"
#include "../prop_parse.h"

namespace glaze {

GLAZE_REGISTER_WIDGET("listbox", ListBox)

ListBox::ListBox()
{
    setClip(true);          // raderna klipps mot innehållsytan
    setBorder(true);        // listan ÄR en inramad yta — och `border` är
                            // det som drar in innerBounds en ramtjocklek
    showScrollbars(true);   // överflöd ska SYNAS (PropertyGrid-prejudikatet)
}

// ------------------------------------------------------------ tillstånd

void ListBox::setItems(std::vector<std::string> v)
{
    if (items_ == v)
        return;
    items_ = std::move(v);
    // Kryssen följer listan: nya rader är okryssade, borttagna tappar sitt.
    // Flervalet likadant — samma regel i samma widget.
    checked_.resize(items_.size(), false);
    selected_.resize(items_.size(), 0);
    changedRow_  = -1;
    pendingSolo_ = -1;   // ett väntande soloval gällde de gamla raderna
    contextRow_  = -1;   // högertryckets rad likaså
    // Scrollen klampas OMEDELBART — annars ritar raderna mot ett
    // värde och remsan mot ett annat tills nästa childArea-anrop.
    // VAKTEN ÄR TVÅDELAD och det är inte överdrivet: `behaviorCtx()`
    // bygger sitt Ctx ur fönstrets RENDERER och FONT, som skapas först i
    // `show()`. En widget kan mycket väl sitta i ett träd i ett fönster som
    // ännu inte visats — dropdownens popup-lista gör exakt det när
    // `openAt` fyller den FÖRE första `show()`.
    if (appWindow() && appWindow()->isOpen()) {
        const Ctx c = behaviorCtx();
        setScroll(scrollX(), std::min(scrollY(), scrollRange(c).h));
    }
    invalidate();
}

// STYCKEFORMEN — kontraktet och skälet står i headern.
// APPEND: listan VÄXER, så scrollintervallet kan bara bli större och
// behöver ingen klampning (till skillnad från `setItems`, som kan krympa
// den). Kryssvektorns invariant "alltid lika lång som items" hålls här.
void ListBox::addItem(std::string text)
{
    items_.push_back(std::move(text));
    checked_.resize(items_.size(), false);
    selected_.resize(items_.size(), 0);   // en ny rad är aldrig vald
    invalidate();
}

// ERSÄTT EN RADS TEXT. Längden är oförändrad, så varken kryssen, valet
// eller scrollen rörs — att byta text på en rad är inte att byta lista.
void ListBox::setItem(int index, std::string text)
{
    if (index < 0 || std::size_t(index) >= items_.size()) {
        gfx::log("ListBox(%s): setItem(%d) utanför intervallet (%zu rader) "
                 "— ignoreras", name.c_str(), index, items_.size());
        return;
    }
    if (items_[std::size_t(index)] == text)
        return;                       // hård regel 1: invalidera vid ÄNDRING
    items_[std::size_t(index)] = std::move(text);
    invalidate();
}

void ListBox::setSelectedIndex(int i)
{
    if (multiSelect_) {
        // FLERVAL: välj BARA raden och gör den aktuell — listvyns
        // `setSelectedIndex`; −1 tömmer valet och lämnar den aktuella.
        if (i < 0) {
            clearSelection();
            return;
        }
        if (std::size_t(i) >= items_.size()) {
            gfx::log("ListBox(%s): setSelectedIndex(%d) utanför intervallet "
                     "(%zu rader) — ignoreras", name.c_str(), i,
                     items_.size());
            return;
        }
        assignSelection({i});
        anchor_ = i;
        if (selectedIndex_ != i) {
            selectedIndex_ = i;
            invalidate();
        }
        return;
    }
    if (selectedIndex_ == i)
        return;
    selectedIndex_ = i;
    invalidate();
}

// Enkelval: den valda (rå). Flerval: den AKTUELLA om den är vald, annars den
// första valda — listvyns svar, så `selectedText()` alltid har en mening.
int ListBox::selectedIndex() const
{
    if (!multiSelect_)
        return selectedIndex_;
    if (isSelected(selectedIndex_))
        return selectedIndex_;
    for (std::size_t r = 0; r < selected_.size(); ++r)
        if (selected_[r])
            return int(r);
    return -1;
}

std::string ListBox::selectedText() const
{
    const int i = selectedIndex();
    return (i >= 0 && std::size_t(i) < items_.size()) ? items_[std::size_t(i)]
                                                       : std::string();
}

// ------------------------------------------------------- flervalet

void ListBox::setMultiSelect(bool on)
{
    if (multiSelect_ == on)
        return;
    // Valet följer med åt båda hållen — programmatiskt, alltså inget event.
    // PÅ: den valda raden blir den första valda (och den aktuella). AV: bara
    // `selectedIndex()` står kvar, listvyns regel.
    const int kvar = selectedIndex();
    multiSelect_   = on;
    selected_.assign(items_.size(), 0);
    if (on && kvar >= 0 && std::size_t(kvar) < items_.size())
        selected_[std::size_t(kvar)] = 1;
    selectedIndex_ = kvar;
    anchor_        = kvar;
    pendingSolo_   = -1;
    invalidate();
}

bool ListBox::assignSelection(const std::vector<int>& rader)
{
    std::vector<char> nytt(items_.size(), 0);
    for (int r : rader)
        if (r >= 0 && std::size_t(r) < items_.size())
            nytt[std::size_t(r)] = 1;
    if (nytt == selected_)
        return false;
    selected_ = std::move(nytt);
    invalidate();
    return true;
}

void ListBox::setSelected(int row, bool on)
{
    if (row < 0 || std::size_t(row) >= items_.size()) {
        gfx::log("ListBox(%s): setSelected(%d) utanför intervallet (%zu "
                 "rader) — ignoreras", name.c_str(), row, items_.size());
        return;
    }
    if (!multiSelect_) {
        // Enkelval: att välja en rad är `setSelectedIndex`, att välja bort
        // den valda tömmer.
        if (on)
            setSelectedIndex(row);
        else if (row == selectedIndex_)
            setSelectedIndex(-1);
        return;
    }
    if (bool(selected_[std::size_t(row)]) == on)
        return;   // hård regel 1: invalidera vid ÄNDRING
    selected_[std::size_t(row)] = on ? 1 : 0;
    invalidate();
}

bool ListBox::isSelected(int row) const
{
    if (row < 0 || std::size_t(row) >= items_.size())
        return false;
    return multiSelect_ ? selected_[std::size_t(row)] != 0
                        : row == selectedIndex_;
}

std::vector<int> ListBox::selectedRows() const
{
    std::vector<int> ut;
    for (int r = 0; r < int(items_.size()); ++r)
        if (isSelected(r))
            ut.push_back(r);
    return ut;
}

void ListBox::selectAll()
{
    if (!multiSelect_)
        return;   // enkelval har ingen "alla"
    std::vector<int> alla(items_.size());
    for (std::size_t r = 0; r < alla.size(); ++r)
        alla[r] = int(r);
    assignSelection(alla);
}

void ListBox::clearSelection()
{
    if (!multiSelect_) {
        setSelectedIndex(-1);
        return;
    }
    assignSelection({});
}

void ListBox::setShowChecks(bool on)
{
    if (showChecks_ == on)
        return;
    showChecks_ = on;
    invalidate();
}

// TEXTSTORLEKEN — regeln i `textSizeValue`. Radhöjden följer, så
// hela listan ritas om.
void ListBox::setTextSize(Length size)
{
    const std::optional<Length> s = textSizeValue(size, "ListBox", name);   // lang_gate:rad typnamnet i loggraden
    if (!s || (s->value == textSize_.value && s->unit == textSize_.unit))
        return;
    textSize_ = *s;
    invalidate();
}

Ctx ListBox::textCtx(const Ctx& c) const
{
    return sizedText(c, textSize_);
}

void ListBox::setChecked(int row, bool on)
{
    if (row < 0 || std::size_t(row) >= checked_.size() ||
        checked_[std::size_t(row)] == on)
        return;
    checked_[std::size_t(row)] = on;
    invalidate();
}

bool ListBox::isChecked(int row) const
{
    return row >= 0 && std::size_t(row) < checked_.size() &&
           checked_[std::size_t(row)];
}

void ListBox::setHoverRow(int row)
{
    if (row == hover_)
        return;
    hover_ = row;
    invalidate();   // hård regel 1: bara vid ÄNDRING
}

bool ListBox::setProp(const std::string& key, const std::string& value)
{
    if (key == "items") {
        // ';'-separerad TEXTform — nyckel/värde-API:ts vokabulär. Den
        // FÖRLUSTFRIA vägen (items med semikolon, tomma element, escapes)
        // går via `bld::parseStringList` + `setItems` i verktygsvägen och
        // rör aldrig den här strängen.
        std::vector<std::string> nya;
        std::size_t start = 0;
        while (start <= value.size()) {
            const auto sep = value.find(';', start);
            if (sep == std::string::npos) {
                if (start < value.size())
                    nya.push_back(value.substr(start));
                break;
            }
            nya.push_back(value.substr(start, sep - start));
            start = sep + 1;
        }
        setItems(std::move(nya));
        return true;
    }
    if (key == "selectedIndex") {
        setSelectedIndex(std::atoi(value.c_str()));   // oparsbart ⇒ 0
        return true;
    }
    if (key == "showChecks") {
        setShowChecks(propparse::parseBool(value));
        return true;
    }
    if (key == "multiSelect") {
        setMultiSelect(propparse::parseBool(value));
        return true;
    }
    return Panel::setProp(key, value);
}

// ------------------------------------------------------------- geometrin
// EN sanning i widget-lokala LOGISKA px, scroll-förskjuten (samma rymd som
// ritningen). Musvägen projicerar dem med Draw::project — aldrig två
// uppsättningar mått. `Checkbox::boxRect` är prejudikatet.

// RADHÖJDEN — kontraktet alla sex beräkningarna går igenom.
// Default är stilens `listRowH` för varje rad; `uniformRows()` säger
// att den är konstant och slår om O(n)-summorna till ett steg. Med en
// TEXTSTORLEK växer eller krymper raden lika mycket som textens
// radhöjd — luften runt texten är stilens — men aldrig under texten själv.
float ListBox::rowH(const Ctx& c, int) const
{
    if (textSize_.value <= 0.0f)
        return c.style->listRowH;
    const float text = Draw::lineHeight(textCtx(c));
    return std::max(text, c.style->listRowH + text - Draw::lineHeight(c));
}

float ListBox::rowTop(const Ctx& c, int row) const
{
    if (row <= 0)
        return 0.0f;
    if (uniformRows())
        return float(row) * rowH(c, 0);
    const int n = int(items_.size());
    float     y = 0.0f;
    for (int i = 0; i < row && i < n; ++i)
        y += rowH(c, i);
    return y;
}

float ListBox::totalRowsH(const Ctx& c) const
{
    const int n = int(items_.size());
    if (n == 0)
        return 0.0f;
    if (uniformRows())
        return float(n) * rowH(c, 0);
    float y = 0.0f;
    for (int i = 0; i < n; ++i)
        y += rowH(c, i);
    return y;
}

int ListBox::rowAtOffset(const Ctx& c, float y) const
{
    const int n = int(items_.size());
    if (n == 0 || y < 0.0f)
        return -1;
    if (uniformRows()) {
        const float h = rowH(c, 0);
        if (h <= 0.0f)
            return -1;   // degenererad stil — hellre "ingen rad" än div/0
        const int row = int(std::floor(y / h));
        return row >= 0 && row < n ? row : -1;
    }
    float top = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float h = rowH(c, i);
        if (y < top + h)
            return i;
        top += h;
    }
    return -1;   // nedanför sista raden
}

int ListBox::firstVisibleRow(const Ctx& c, float scroll) const
{
    const int n = int(items_.size());
    if (n == 0 || scroll <= 0.0f)
        return 0;
    if (uniformRows()) {
        const float h = rowH(c, 0);
        if (h <= 0.0f)
            return 0;
        return std::clamp(int(scroll / h), 0, n);
    }
    float top = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float h = rowH(c, i);
        if (scroll < top + h)
            return i;
        top += h;
    }
    return n;   // hela listan är ovanför — radloopen ritar ingenting
}

gfx::Rectf ListBox::rowRect(const Ctx& c, int row) const
{
    const gfx::Rectf in = innerBounds(c);
    return gfx::Rectf{in.x, in.y - scrollY() + rowTop(c, row), in.w,
                      rowH(c, row)};
}

gfx::Rectf ListBox::checkRect(const Ctx& c, int row) const
{
    const gfx::Rectf r = rowRect(c, row);
    const float      s = c.style->checkSize;
    // V-CENTRERAD i raden även när en Style ger checkSize > listRowH —
    // då sticker rutan ut lika mycket åt båda hållen i stället för att
    // hänga i överkanten.
    return gfx::Rectf{r.x + c.style->listPadX, r.y + (r.h - s) / 2.0f, s, s};
}

float ListBox::textX(const Ctx& c, int row) const
{
    if (!showChecks_)
        return rowRect(c, row).x + c.style->listPadX;
    const gfx::Rectf cr = checkRect(c, row);
    return cr.x + cr.w + c.style->checkGap;
}

gfx::Rectf ListBox::checkZone(const Ctx& c, int row) const
{
    const gfx::Rectf r  = rowRect(c, row);
    const gfx::Rectf cr = checkRect(c, row);
    return gfx::Rectf{cr.x, r.y, cr.w + c.style->checkGap, r.h};
}

// Trycket i radens kryss-zon (fysiska px) — via SAMMA geometri som
// ritningen, bara projicerad till musens rymd. Trycket och dubbelklicket
// delar den.
bool ListBox::inCheckZone(const Ctx& c, int row, float mx, float my) const
{
    return showChecks_ &&
           Draw::project(c, resolve(c), checkZone(c, row)).contains(mx, my);
}

int ListBox::rowAtY(const Ctx& c, float my) const
{
    const gfx::Rectf base = resolve(c);
    const gfx::Rectf in   = innerBounds(c);
    const float      y    = Draw::localY(c, base, my);
    if (y < in.y || y > in.y + in.h)
        return -1;   // ovanför/nedanför innehållsytan (ramen räknas inte)
    return rowAtOffset(c, y - in.y + scrollY());   // vaktar intervallet själv
}

// Raden i HELA innehållsytan: remsan och sidramen är
// "ingen" för både den publika träffvägen och högertrycket.
int ListBox::hitRow(const Ctx& c, float mx, float my) const
{
    return inRowColumn(c, mx, my) ? rowAtY(c, my) : -1;
}

// SNABBMENYNS träffväg — LOGISKA px från listans
// hörn, projicerade till musens fysiska rymd. Kräver ett öppet fönster
// (`behaviorCtx`), som `ensureVisible`.
int ListBox::rowAt(float localX, float localY) const
{
    if (!appWindow() || !appWindow()->isOpen())
        return -1;
    const Ctx        c  = behaviorCtx();
    const gfx::Rectf pt = Draw::project(c, resolve(c),
                                        gfx::Rectf{localX, localY, 0.0f, 0.0f});
    return hitRow(c, pt.x, pt.y);
}

// HÖGERTRYCKET VÄLJER SOM UTFORSKAREN (trädets regel): en rad utanför
// valet väljs ensam genom användarvägen, en rad i valet blir bara aktuell.
// Ingen fokusflytt, och svaret är ALLTID false — snabbmenyn fyras.
bool ListBox::rightDown(const Ctx& c, float mx, float my)
{
    const int row = hitRow(c, mx, my);
    contextRow_   = row;
    if (row < 0)
        return false;   // tom yta, remsan, ramen
    if (!isSelected(row)) {
        HandlerGuard vakt(*this);
        const bool   fyrat = selectFromUser(row, 0u, false);
        if (!vakt.ok())
            return false;   // en handler rev eller lossade listan
        if (fyrat)
            appWindow()->invalidate();   // en handler har kört (widget.h)
    } else if (selectedIndex_ != row) {
        selectedIndex_ = row;   // flervalets aktuella rad — ramen flyttar
        invalidate();
    }
    return false;
}

void ListBox::ensureVisible(int row)
{
    // Samma tvådelade vakt som `setItems`: utan ett VISAT fönster finns
    // varken renderer eller font att bygga ett Ctx ur.
    if (!appWindow() || !appWindow()->isOpen() || row < 0 ||
        std::size_t(row) >= items_.size())
        return;
    const Ctx   c   = behaviorCtx();
    const float top = rowTop(c, row);
    const float rh  = rowH(c, row);       // RADENS egen höjd, inte listans
    const float h   = innerBounds(c).h;
    float       s   = scrollY();
    if (top < s)
        s = top;
    else if (top + rh > s + h)
        s = top + rh - h;
    setScroll(scrollX(), s);   // invaliderar bara vid faktisk ändring
}

// ----------------------------------------------------------- ritningen

gfx::Sizef ListBox::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default:
        // INGEN AUTOSTORLEK — samma beslut som Panel och PropertyGrid
        // (axelregeln, del A): en lista är en placeringsyta APPEN
        // dimensionerar, och stilen kan inte gissa hur många rader som
        // ska synas.
        return gfx::Sizef{width, height};
    }
}

gfx::Sizef ListBox::contentExtent(const Ctx& c) const
{
    // FYSISKA px (Panels kontrakt). Bredden är VIEWPORTENS: ingen
    // horisontell scroll, så Panels andra overlayaxel aldrig aktiveras.
    const gfx::Rectf in    = innerRect(c);
    const float      rader = totalRowsH(c) * c.scale;
    return gfx::Sizef{in.w, std::max(Panel::contentExtent(c).h, rader)};
}

// Radens INNEHÅLL: kryss (när det visas) + text, i radens egen textfärg.
// DELAS av alla tre ritningarna — det som skiljer familjerna åt är
// MARKERINGEN runt raden, inte hur en rad är uppbyggd. Tre kopior hade
// varit tre chanser att glida isär i kryssgeometrin.
void ListBox::drawRow(const Ctx& c, int row, gfx::Color fg,
                      gfx::Color boxPen) const
{
    const gfx::Rectf r = rowRect(c, row);
    if (showChecks_) {
        const gfx::Rectf cr = checkRect(c, row);
        Draw::frame(c, cr, c.style->checkRadius,
                    Pen{boxPen, c.style->borderWidth});
        if (isChecked(row))
            Draw::check(c, cr, Pen{fg, c.style->checkStroke});
    }
    const float tx = textX(c, row);
    Draw::text(textCtx(c), {tx, r.y, r.x + r.w - tx, r.h},
               items_[std::size_t(row)], fg);
}

void ListBox::draw(const Ctx& c)
{
    const gfx::Rectf b    = c.bounds();
    const gfx::Rectf in   = innerBounds(c);
    const auto&      m    = *c.style;
    const auto&      col  = c.theme->colors;
    const bool       på   = isEnabled();
    // Hovern gäller bara medan pekaren FAKTISKT är över listan — annars
    // hade den sista raden lyst kvar när musen lämnat (det finns ingen
    // leave-krok på widgeten; `hovered()` är AppWindows sanning).
    const int hov = hovered() && på ? hover_ : -1;
    // Radloopen startar vid FÖRSTA SYNLIGA raden och bryter vid
    // nederkanten — O(synliga), inte O(n). Det är svaret på tusentals
    // rader, och skälet att PropertyGrids O(n)-loop inte kopierades.
    const int första = firstVisibleRow(c, scrollY());
    const int sista  = int(items_.size());
    // Den AKTUELLA raden ramas in när den kan skilja sig från valet — alltså
    // bara med flerval och med fokus (listvyns recept).
    const int aktuell = focused() && på && multiSelect_ &&
                                selectedIndex_ >= 0 && selectedIndex_ < sista
                            ? selectedIndex_
                            : -1;

    switch (c.ui) {
    // GLAZE — listan följer FÄLTETS recept (`panel`-yta, BESTÄMD kontur
    // `buttonBorder`), precis som dropdownens popup gör: en lista är en
    // yta man läser i, inte en kropp man trycker på.
    //
    // Den VALDA raden fylls med `accentSurface` och texten blir
    // `onAccent` — det GRINDADE paret (i mörkt läge är accenten två
    // kulörer, och det är `accentSurface` som kan bära text). RADEN
    // LAMINERAS INTE: en rad har ingen KONTUR, och familjens regel är att
    // laminering kräver en — det vita ljusfältet sänker `onAccent` mot
    // raden från 5.67:1 till **2.91:1** i ljust läge, långt under 4.5:1
    // för TEXT.
    //
    //   vila     ingen radfyllning, text `text`
    //   hover    `buttonHover` (SVAG — pekaren är inte ett val)
    //   vald     `accentSurface`-rad, text `onAccent`
    //   fokus    fristående ring utanför kanten; med flerval en hårlinje
    //            runt den AKTUELLA raden i radens textfärg (listvyns)
    //   inaktiv  markeringen faller till `buttonDisabled`, all text `textDim`
    case UI::Glaze:
        Draw::rect(c, b, 0.0f, col.panel);
        Draw::clip(c, in);
        for (int i = första; i < sista; ++i) {
            const gfx::Rectf r = rowRect(c, i);
            if (r.y > in.y + in.h)
                break;
            const bool vald = isSelected(i);
            if (vald)
                Draw::rect(c, r, 0.0f, på ? col.accentSurface
                                          : col.buttonDisabled);
            else if (i == hov)
                Draw::rect(c, r, 0.0f, col.buttonHover);
            const gfx::Color fg = !på              ? col.textDim
                                  : vald           ? col.onAccent
                                                   : col.text;
            drawRow(c, i, fg, vald ? fg : på ? col.buttonBorder : col.border);
        }
        if (aktuell >= 0)
            Draw::frame(c, rowRect(c, aktuell), 0.0f,
                        Pen{isSelected(aktuell) ? col.onAccent : col.text,
                            m.borderWidth});
        Draw::endClip(c);
        Draw::frame(c, b, 0.0f,
                    Pen{på ? col.buttonBorder : col.border, m.borderWidth});
        break;

    // MINIMALISTIC — "Outline" i listform: INGEN radfyllning för valet,
    // som markeras med ett accent-STRECK i vänsterkanten (`rowMarkerW`).
    // Samma markör som PropertyGrids aktiva rad — en rad är ingen
    // kontroll, så "fyllning betyder tillstånd" gäller inte här och får
    // inte lånas.
    //
    // YTAN fylls INTE i vila: familjens grundsats är att bakgrunden är
    // fönstrets. Dropdownens popup är undantaget (`ownsSurface()`) — den
    // ÄR ett eget OS-fönster och har ingenting bakom sig att lysa igenom.
    //
    //   vila     ingen yta, ingen radfyllning, text `text`
    //   hover    `buttonHover` innanför listkanten
    //   vald     accent-markör i vänsterkanten, text `accent`
    //   fokus    fristående ring (halv tjocklek — stilens focusWidth); med
    //            flerval en hårlinje runt den aktuella raden
    //   inaktiv  markör och text `textDim`, kanten `border`
    case UI::Minimalistic:
        if (ownsSurface())
            Draw::rect(c, b, 0.0f, col.panel);
        Draw::clip(c, in);
        for (int i = första; i < sista; ++i) {
            const gfx::Rectf r = rowRect(c, i);
            if (r.y > in.y + in.h)
                break;
            const bool vald = isSelected(i);
            if (i == hov)
                Draw::rect(c, r, 0.0f, col.buttonHover);
            const gfx::Color fg = !på    ? col.textDim
                                  : vald ? col.accent
                                         : col.text;
            if (vald)
                Draw::rect(c, {r.x, r.y, m.rowMarkerW, r.h}, 0.0f, fg);
            drawRow(c, i, fg, på ? col.buttonBorder : col.border);
        }
        if (aktuell >= 0)
            Draw::frame(c, rowRect(c, aktuell), 0.0f,
                        Pen{isSelected(aktuell) ? col.accent : col.text,
                            m.borderWidth});
        Draw::endClip(c);
        Draw::frame(c, b, 0.0f,
                    Pen{på ? col.buttonBorder : col.border, m.borderWidth});
        break;

    // SLATE — ett FÄLT: `panel`-yta, hårlinje
    // `border`, och den VALDA raden fylls med `accent` medan texten går
    // till `windowBg`. Det är samma selektion dropdownens popup alltid
    // haft; skillnaden mot popupen är BARA vilket tillstånd som är
    // starkt (val, inte pekarposition).
    //
    //   vila     ingen radfyllning, text `text`
    //   hover    `buttonHover`
    //   vald     `accent`-rad, text `windowBg`
    //   fokus    fristående ring utanför kanten; med flerval en hårlinje
    //            runt den aktuella raden
    //   inaktiv  markeringen faller till `buttonDisabled`, all text `textDim`
    case UI::Slate:
    default:
        Draw::rect(c, b, 0.0f, col.panel);
        Draw::clip(c, in);
        for (int i = första; i < sista; ++i) {
            const gfx::Rectf r = rowRect(c, i);
            if (r.y > in.y + in.h)
                break;
            const bool vald = isSelected(i);
            if (vald)
                Draw::rect(c, r, 0.0f, på ? col.accent : col.buttonDisabled);
            else if (i == hov)
                Draw::rect(c, r, 0.0f, col.buttonHover);
            const gfx::Color fg = !på              ? col.textDim
                                  : vald           ? col.windowBg
                                                   : col.text;
            drawRow(c, i, fg, vald ? fg : på ? col.buttonBorder : col.border);
        }
        if (aktuell >= 0)
            Draw::frame(c, rowRect(c, aktuell), 0.0f,
                        Pen{isSelected(aktuell) ? col.windowBg : col.text,
                            m.borderWidth});
        Draw::endClip(c);
        Draw::frame(c, b, 0.0f, Pen{col.border, m.borderWidth});
        break;
    }

    if (focused() && på)
        Draw::frame(c, Draw::inset(b, -m.focusOffset), m.focusOffset,
                    Pen{col.accent, m.focusWidth});
}

// --------------------------------------------------------- användarvägarna
// De ENDA två platser som fyrar event. `Checkbox::clicked()` är
// prejudikatet: en användarväg, en avfyrning.

bool ListBox::selectFromUser(int row, std::uint32_t mods, bool fromKey)
{
    const int n = int(items_.size());
    if (row < 0 || row >= n)
        return false;
    if (!multiSelect_) {
        if (row == selectedIndex_)
            return false;
        selectedIndex_ = row;
        // En REN valhändelse är ingen checkhändelse: `changedRow` måste
        // släppa sitt gamla värde, annars läser nästa `onChange` en rad som
        // kryssades för länge sedan. Kontraktet står i listbox.h.
        changedRow_ = -1;
        ensureVisible(row);
        invalidate();
        firePair(onChange, onCommit);   // samma val; läses ur sender
        return true;
    }

    // FLERVALET — `ListView::selectFromUser` rad för rad: Shift är
    // spannet från ANKARET (eller den aktuella, eller raden själv), och
    // Ctrl+Shift+klick LÄGGER TILL spannet; Ctrl/Cmd vänder vid klick och
    // flyttar bara den aktuella från tangentbordet (Ctrl+Space vänder sedan);
    // utan modifierare väljs bara raden.
    const bool shift  = (mods & gfx::kModShift) != 0;
    const bool vänd   = (mods & (gfx::kModCtrl | gfx::kModCmd)) != 0;
    bool       ändrat = false;
    if (shift) {
        const int ankare = anchor_ >= 0 && anchor_ < n ? anchor_
                           : selectedIndex_ >= 0 && selectedIndex_ < n
                               ? selectedIndex_
                               : row;
        std::vector<int> rader = (vänd && !fromKey) ? selectedRows()
                                                     : std::vector<int>{};
        for (int r = std::min(ankare, row); r <= std::max(ankare, row); ++r)
            rader.push_back(r);
        ändrat  = assignSelection(rader);
        anchor_ = ankare;
    } else if (vänd) {
        if (!fromKey) {
            selected_[std::size_t(row)] = selected_[std::size_t(row)] ? 0 : 1;
            ändrat = true;
            invalidate();
        }
        anchor_ = row;
    } else {
        ändrat  = assignSelection({row});
        anchor_ = row;
    }
    if (selectedIndex_ != row) {
        selectedIndex_ = row;   // den aktuella — ramen flyttar
        invalidate();
    }
    changedRow_ = -1;   // en ren valhändelse (ovan)
    ensureVisible(row);
    if (!ändrat)
        return false;
    firePair(onChange, onCommit);   // EN gång per handling, läses ur sender
    return true;
}

void ListBox::toggleCheckFromUser(int row)
{
    if (row < 0 || std::size_t(row) >= checked_.size())
        return;
    // Ett kryssklick FLYTTAR INTE VALET — de två är olika handlingar, och
    // en lista med kryss används just för att bocka av utan att byta rad.
    checked_[std::size_t(row)] = !checked_[std::size_t(row)];
    changedRow_ = row;
    invalidate();
    // onCheck och därefter onCommit för samma kryss. Raden SÄTTS IGEN
    // emellan: en onCheck-handler som fyller om listan har nollat den, och
    // onCommit ska läsa samma rad (headern).
    if (!fireEvent(onCheck))   // raden läses ur sender: changedRow()
        return;
    changedRow_ = row;
    fireEvent(onCommit);
}

// AKTIVERINGEN — listvyns form: bara med en handler och en vald
// rad, och raden läses ur sender (`selectedIndex()`). Svaret är om den TOG
// handlingen — Enter bubblar annars till standardknappen.
bool ListBox::activateFromUser()
{
    if (!onActivate || selectedIndex_ < 0 ||
        std::size_t(selectedIndex_) >= items_.size())
        return false;
    fireEvent(onActivate);
    return true;
}

// ------------------------------------------------------------- beteendet

bool ListBox::handleStripFirst(const Ctx& c, float mx, float my, bool down)
{
    const bool iRemsa = inScrollStrip(c, mx, my);
    if (down)
        Panel::mouseDown(c, mx, my);   // tumgrepp/rännklick — OCH nollar
                                       // drag-läget även utanför remsan
    else
        Panel::mouseMove(c, mx, my);   // het tumme (invaliderar vid ändring)
    return iRemsa;
}

void ListBox::mouseDown(const Ctx& c, float mx, float my)
{
    pendingSolo_ = -1;   // ett gammalt väntande soloval släpps
    if (handleStripFirst(c, mx, my, true))
        return;
    const int row = rowAtY(c, my);
    if (row < 0)
        return;
    // Kryss-zonen togglar, resten av raden väljer.
    if (inCheckZone(c, row, mx, my)) {
        toggleCheckFromUser(row);
        return;
    }
    // Musens modifierare ur fönstret (widget.h — krokarna bär inga).
    const AppWindow*    w    = appWindow();
    const std::uint32_t mods = w ? w->modifierKeys() : 0u;
    if (multiSelect_ && isSelected(row) &&
        !(mods & (gfx::kModCtrl | gfx::kModCmd | gfx::kModShift))) {
        // SOLO VID SLÄPPET (listvyns): trycket gör bara raden aktuell —
        // annars hade det valt bort de andra innan ett drag av hela valet
        // hunnit börja.
        pendingSolo_ = row;
        if (selectedIndex_ != row) {
            selectedIndex_ = row;
            invalidate();
        }
        return;
    }
    selectFromUser(row, mods, false);
}

// DUBBELKLICKET kommer efter det andra trycket, som redan valt
// raden. I kryss-zonen har båda trycken vänt krysset — två vändningar, och
// ingen aktivering (listvyns regel). Remsan och ramen är "ingen rad".
void ListBox::mouseDoubleClick(const Ctx& c, float mx, float my)
{
    const int row = hitRow(c, mx, my);
    if (row >= 0 && row == selectedIndex_ && !inCheckZone(c, row, mx, my))
        activateFromUser();
}

bool ListBox::mouseUp(const Ctx&, float, float)
{
    const int rad = pendingSolo_;
    pendingSolo_  = -1;
    if (rad < 0 || std::size_t(rad) >= items_.size())
        return false;
    return selectFromUser(rad, 0u, false);
}

void ListBox::dragStart(const Ctx& c, DragStartArgs& e)
{
    pendingSolo_ = -1;   // draget tar trycket — ingen `mouseUp` kommer
    Panel::dragStart(c, e);
}

void ListBox::mouseMove(const Ctx& c, float mx, float my)
{
    if (handleStripFirst(c, mx, my, false)) {
        setHoverRow(-1);   // pekaren är på remsan, inte på en rad
        return;
    }
    setHoverRow(rowAtY(c, my));
}

bool ListBox::mouseWheel(const Ctx& c, float, float, float, float wheelY)
{
    // Hjulet stegar i RADER och inte i Panels generella `wheelStep`:
    // raden är listans egen enhet, och en halv synlig rad läser sämre än
    // hela. Bubblar vidare (false) när det inte finns något att scrolla
    // eller när kanten redan är nådd — Panels konvention, så en yttre
    // panel får ta hjulet.
    const float spelrum = scrollRange(c).h;
    if (spelrum <= 0.0f)
        return false;
    // STEGET är radens egen höjd och läses genom radkontraktet, inte ur
    // `listRowH` direkt: med variabel radhöjd (menyn) är "en rad" olika
    // långt, och steget ska vara den rad som faktiskt rullar förbi —
    // alltså den i scrollens överkant. För en vanlig lista är det exakt
    // `listRowH`.
    const int topp =
        std::clamp(firstVisibleRow(c, scrollY()), 0, int(items_.size()) - 1);
    // Hjul upp (positiv rad) = mot listans början.
    const float ny = std::clamp(
        scrollY() - wheelScrollPx(c, wheelY, rowH(c, topp)), 0.0f, spelrum);
    if (ny == scrollY())
        return false;
    setScroll(scrollX(), ny);
    return true;
}

bool ListBox::keyDown(gfx::Key k, std::uint32_t mods)
{
    // Alt är menyernas och appens. Ctrl/Cmd konsumeras bara i FLERVAL
    // (det är flervalets tangenter); i enkelval lämnas de åt genvägarna.
    if (mods & gfx::kModAlt)
        return false;
    const bool styr = (mods & (gfx::kModCtrl | gfx::kModCmd)) != 0;
    if (styr && !multiSelect_)
        return false;
    const int n = int(items_.size());
    if (n == 0)
        return false;
    // Navigeringen bär modifierarna: i flerval utökar Shift spannet och
    // flyttar Ctrl bara den aktuella raden (`selectFromUser`).
    const auto gå = [&](int rad) {
        selectFromUser(std::clamp(rad, 0, n - 1), mods, true);
        return true;
    };

    switch (k) {
    case gfx::Key::Up:   return gå(selectedIndex_ - 1);
    case gfx::Key::Down: return gå(selectedIndex_ + 1);
    case gfx::Key::Home: return gå(0);
    case gfx::Key::End:  return gå(n - 1);
    case gfx::Key::Return:
        // Tangenten är vår bara när den GÖR något (listvyns regel): utan
        // vald rad eller utan `onActivate` bubblar den till standardknappen.
        return activateFromUser();
    case gfx::Key::A: {
        // Ctrl+A väljer alla (bara i flerval — enkelvalet har bubblat ovan),
        // en ANVÄNDARväg: händelsen bara när valet faktiskt ändrades.
        if (!styr)
            return false;
        std::vector<int> alla(items_.size());
        for (std::size_t r = 0; r < alla.size(); ++r)
            alla[r] = int(r);
        if (assignSelection(alla)) {
            changedRow_ = -1;
            firePair(onChange, onCommit);
        }
        return true;
    }
    case gfx::Key::Space:
        // Space togglar den valda (i flerval: den AKTUELLA) radens kryss —
        // men bara när kryssen VISAS; annars finns inget tillstånd att
        // toggla och tangenten ska bubbla vidare. I flerval vänder Ctrl+Space
        // VALET av den aktuella, och Space utan kryss väljer den.
        if (selectedIndex_ < 0 || selectedIndex_ >= n)
            return false;
        if (showChecks_ && !styr) {
            toggleCheckFromUser(selectedIndex_);
            return true;
        }
        if (!multiSelect_)
            return false;
        if (styr)
            selectFromUser(selectedIndex_, gfx::kModCtrl, false);   // vänd
        else if (!isSelected(selectedIndex_))
            selectFromUser(selectedIndex_, 0u, true);
        return true;
    default:
        return false;
    }
}

} // namespace glaze
