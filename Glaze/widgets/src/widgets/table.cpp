// Table — den delade maskinen bakom ListView och DataGrid.
// MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) — men Table ritar bara
// DELAR (`drawCells`, och basens `drawHeader`), och listvyn och datagriden
// har var sin `draw()` med var sin `switch (c.ui)` runt dem. Radernas
// geometri bor HÄR i LOGISKA px — EN sanning som ritningen läser rakt av och
// som musvägen projicerar till fysiska px (Draw::localX/localY). Basen
// ColumnPanel ger kolumnerna, huvudet, avdelardraget och
// sorteringens tillstånd, och dess bas Panel klipp, hjulscroll,
// overlay-remsorna och innehållsytan.
//
// SKRIVNA BESLUT (de långa motiveringarna står i table.h):
//   tabellen sorterar själv · indexen är appens, sorteringen bara visning
//   · naturlig jämförelse + sorteringsnyckel · ingen autostorlek · ingen
//   språkmedveten kollation.

#include "glaze/widgets/table.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "glaze/app_window.h"
#include "glaze/draw.h"

#include "../prop_parse.h"

namespace glaze {
namespace {

// Svaret utanför tabellen — en TOM sträng med livstid nog att lämna ut en
// referens till (`cell()`s kontrakt).
const std::string& tomCell()
{
    static const std::string tom;
    return tom;
}

} // namespace

Table::Table()
{
    setClip(true);          // editorerna (DataGrid) klipps mot raderna
    setBorder(true);        // tabellen ÄR en inramad yta — och `border` är
                            // det som drar in innerBounds en ramtjocklek
    showScrollbars(true);   // överflöd ska SYNAS (ListBox-prejudikatet)
}

// --------------------------------------------------------------- raderna

void Table::addRow(std::vector<std::string> cells, ImageSource img,
                   std::optional<gfx::Color> color)
{
    rows_.push_back(Row{std::move(cells), std::move(img), color});
    keys_.emplace_back();
    cellColors_.emplace_back();
    contentChanged();
    rowAppended();
    orderChanged();
}

void Table::setRow(int index, std::vector<std::string> cells, ImageSource img,
                   std::optional<gfx::Color> color)
{
    if (index < 0 || index >= rowCount()) {
        gfx::log("Table(%s): setRow(%d) utanför intervallet (%d rader) — "
                 "ignoreras", name.c_str(), index, rowCount());
        return;
    }
    // TILLDELNING: raden ersätts helt — en utelämnad färg blir ingen färg,
    // inte heller cellernas — och nycklarna beskrev den gamla texten. Valet
    // och den aktuella raden är vyns och följer med.
    rows_[std::size_t(index)] = Row{std::move(cells), std::move(img), color};
    keys_[std::size_t(index)].clear();
    cellColors_[std::size_t(index)].clear();
    contentChanged();
    rowReplaced(index);
    orderChanged();
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
    contentChanged();
    current_ = -1;
    rowsReplaced();
    orderChanged();
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
    auto& nycklar = keys_[std::size_t(row)];
    if (std::size_t(column) < nycklar.size())
        nycklar[std::size_t(column)].reset();   // nyckeln beskrev gamla texten
    if (celler[std::size_t(column)] == text)
        return;
    celler[std::size_t(column)] = std::move(text);
    contentChanged();
    if (column == sortColumn())
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

// ------------------------------------------------------------ sorteringen

// ColumnPanels krok (sorteringskolumnen, riktningen, kolumnerna) och
// tabellens egna ändringar (raderna, cellerna, nycklarna) går samma väg.
void Table::orderChanged()
{
    orderDirty_ = true;
    invalidate();
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
    contentChanged();   // ikonen står i kolumn 0:s innehåll
    invalidate();
    clampScroll();   // radhöjden kan ha ändrats
}

// Jämför RAD a med RAD b i kolumnen: nyckel mot nyckel numeriskt, en cell
// MED nyckel före en utan, annars naturlig text.
int Table::compareRows(int a, int b, int column) const
{
    const auto nyckel = [this, column](int r) -> std::optional<double> {
        const auto& k = keys_[std::size_t(r)];
        return std::size_t(column) < k.size() ? k[std::size_t(column)]
                                               : std::nullopt;
    };
    const std::optional<double> ka = nyckel(a);
    const std::optional<double> kb = nyckel(b);
    if (ka && kb)
        return *ka < *kb ? -1 : (*kb < *ka ? 1 : 0);
    if (ka)
        return -1;
    if (kb)
        return 1;
    return compareNatural(cell(a, column), cell(b, column));
}

// ORDNINGEN räknas LATT — när den första gången behövs efter en ändring.
// Stabil sortering: lika rader behåller appens ordning i BÅDA riktningarna.
void Table::ensureOrder() const
{
    const std::size_t n = rows_.size();
    if (!orderDirty_ && order_.size() == n)
        return;
    order_.resize(n);
    std::iota(order_.begin(), order_.end(), 0);
    if (sortColumn() >= 0 && sortColumn() < columnCount()) {
        const int  kol = sortColumn();
        const bool upp = isSortAscending();
        std::stable_sort(order_.begin(), order_.end(),
                         [this, kol, upp](int a, int b) {
                             const int r = compareRows(a, b, kol);
                             return upp ? r < 0 : r > 0;
                         });
    }
    pos_.resize(n);
    for (std::size_t p = 0; p < n; ++p)
        pos_[std::size_t(order_[p])] = int(p);
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
    if (position < 0 || position >= rowCount())
        return -1;
    ensureOrder();
    return order_[std::size_t(position)];
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

// KOLUMNENS CELLER: bredaste texten, och i kolumn 0 zonen och ikonen
// före den (`drawCell`s egen ordning). Alla rader mäts — svaret cachas av
// ColumnPanel (`fittedW`) tills `contentChanged`.
float Table::cellContentW(const Ctx& c, int column) const
{
    float w = 0.0f;
    for (int r = 0; r < rowCount(); ++r) {
        const auto& celler = rows_[std::size_t(r)].cells;
        float       rad    = std::size_t(column) < celler.size()
                                 ? Draw::measure(c, celler[std::size_t(column)]).w
                                 : 0.0f;
        if (column == 0) {
            rad += leadW(c);
            const float sida = iconSide(c, r);
            if (sida > 0.0f)
                rad += sida + c.style->iconGap;
        }
        w = std::max(w, rad);
    }
    return w;
}

// Zonen före ikonen: cellens luft in i FÖRSTA kolumnen,
// `leadW` bred och radens hela höjd — där `drawCell` hoppar förbi den. Utan
// kolumner finns ingen första cell och ingen zon: en tom rekt, som ingen
// punkt träffar.
gfx::Rectf Table::leadZone(const Ctx& c, int position) const
{
    if (columnCount() == 0)
        return gfx::Rectf{};
    const gfx::Rectf r = cellRect(c, position, 0);
    return gfx::Rectf{r.x + cellPadX(c), r.y, leadW(c), r.h};
}

int Table::firstVisiblePosition(const Ctx& c) const
{
    const float h = rowH(c);
    if (h <= 0.0f)
        return 0;   // degenererad stil — hellre allt än div/0
    return std::clamp(int(scrollYClamped(c) / h), 0, rowCount());
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
    return p >= 0 && p < rowCount() ? p : -1;
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

gfx::Sizef Table::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default:
        // INGEN AUTOSTORLEK — ListBox/PropertyGrid-beslutet: en tabell är en
        // yta APPEN dimensionerar, och stilen kan inte gissa hur många rader
        // som ska synas.
        return gfx::Sizef{width, height};
    }
}

gfx::Sizef Table::contentExtent(const Ctx& c) const
{
    // FYSISKA px (Panels kontrakt), och bara RADERNAS: huvudet ligger
    // utanför rullningsytan (`scrollViewport`) och scrollar aldrig i höjdled.
    // Radrubriken rullar inte: bredden är de rullande kolumnernas,
    // som rullningsytan är lika mycket smalare än tabellen.
    return gfx::Sizef{(columnsW(c) - frozenW(c)) * c.scale,
                      float(rowCount()) * rowH(c) * c.scale};
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

void Table::drawCell(const Ctx& c, int position, int row, int column,
                     gfx::Color fg, bool appColors) const
{
    const auto&      m   = *c.style;
    const float      pad = cellPadX(c);
    const Row&       rad = rows_[std::size_t(row)];
    const gfx::Rectf r   = cellRect(c, position, column);
    // APPENS FÄRGER: cellens bakgrund fylls under innehållet, och
    // texten är cellens, radens eller temats färg.
    if (appColors) {
        if (const auto bg = cellBackground(row, column))
            Draw::rect(c, r, 0.0f, *bg);
        fg = appTextColor(row, column, fg);
    }
    float            tx  = r.x + pad;
    float            w   = r.w - 2.0f * pad;
    // ZONEN FÖRE IKONEN (`leadW`) — listvyns kryssruta,
    // som listvyn ritar själv. Ikonen och texten börjar efter den.
    if (column == 0) {
        tx += leadW(c);
        w -= leadW(c);
    }
    // IKONEN i första kolumnen, före texten — krokarna (`iconSide`/
    // `drawIcon`), så en subklass kan rita sin egen.
    const float sida = column == 0 ? iconSide(c, row) : 0.0f;
    if (sida > 0.0f) {
        drawIcon(c, {tx, r.y + (r.h - sida) / 2.0f, sida, sida}, row, fg);
        tx += sida + m.iconGap;
        w -= sida + m.iconGap;
    }
    if (w > 0.0f && std::size_t(column) < rad.cells.size())
        Draw::textElided(c, {tx, r.y, w, r.h}, rad.cells[std::size_t(column)],
                         fg, columns()[std::size_t(column)].align);
}

// RADRUBRIKERNAS REMSA: kolumn 0 i rubrikens stil — ytan och
// hårlinjen är `drawHeader`s (`drawHeaderSurface`, `headerLineColor`), namnen
// rubrikens textfärg, en hårlinje under varje rad och en vid remsans högerkant.
// Allt i EN pass över de synliga raderna, klippt mot radernas yta på hela
// innehållsbredden (en rad som rullats halvvägs under huvudet får aldrig
// ritas över det, och högerkantens linje ligger på klippkanten till
// rullningsytan). Varken val, hover eller appfärger: remsan är en rubrik.
void Table::drawRowHeaders(const Ctx& c) const
{
    const float fz = frozenW(c);
    if (fz <= 0.0f)
        return;
    const gfx::Rectf in    = innerBounds(c);
    const gfx::Rectf vp    = scrollViewport(c);
    const gfx::Rectf remsa{in.x, vp.y, fz, vp.h};
    const Pen        linje{headerLineColor(c), c.style->borderWidth};
    const auto&      col   = c.theme->colors;
    const gfx::Color fg    = isEnabled() ? col.text : col.textDim;

    Draw::clip(c, gfx::Rectf{in.x, vp.y, in.w, vp.h});
    drawHeaderSurface(c, remsa);
    for (int p = firstVisiblePosition(c); p < rowCount(); ++p) {
        const gfx::Rectf r = cellRect(c, p, 0);
        if (r.y >= remsa.y + remsa.h)
            break;
        const int rad = rowAtDisplay(p);
        drawCell(c, p, rad, 0, fg, false);
        drawRowHeaderMarks(c, p, rad, fg);
        Draw::line(c, r.x, r.y + r.h, r.x + r.w, r.y + r.h, linje);
    }
    Draw::line(c, remsa.x + remsa.w, remsa.y, remsa.x + remsa.w,
               remsa.y + remsa.h, linje);
    Draw::endClip(c);
}

// Standardikonen är radens BILD: sidan är assetens storleksklass
// (Button-prejudikatet) — en bild ur pixlar har ingen och får stilens
// reservsida — och en inaktiv tabell tonar den som knapparna gör.
//
// En SATT ikonstorlek gäller VARJE rad — också en utan bild, så texten
// står i linje genom hela listan; `drawIcon` ritar bara en giltig bild.
float Table::iconSide(const Ctx& c, int row) const
{
    if (iconSize_ > 0.0f)
        return iconSize_;
    const Row& rad = rows_[std::size_t(row)];
    if (!rad.img.valid())
        return 0.0f;
    const float k = rad.img.sizeClass();
    return k > 0.0f ? k : c.style->iconFallback;
}

void Table::drawIcon(const Ctx& c, const gfx::Rectf& box, int row,
                     gfx::Color) const
{
    const ImageSource& img = rows_[std::size_t(row)].img;
    if (img.valid())
        Draw::glyph(c, box, img, isEnabled() ? 1.0f : 1.0f - c.style->iconFade);
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

// NAVIGERINGEN — målplatsen för en tangent. PageUp/PageDown flyttar en SIDA
// (de hela rader som syns), och numpadens navigationsläge viks av
// `AppWindow` före oss.
int Table::navigationTarget(const Ctx& c, gfx::Key k, int from) const
{
    const int n = rowCount();
    if (n == 0)
        return -1;
    switch (k) {
    case gfx::Key::Up:       return std::clamp(from - 1, 0, n - 1);
    case gfx::Key::Down:     return std::clamp(from + 1, 0, n - 1);
    case gfx::Key::Home:     return 0;
    case gfx::Key::End:      return n - 1;
    case gfx::Key::PageUp:   return std::clamp(from - pageRows(c), 0, n - 1);
    case gfx::Key::PageDown: return std::clamp(from + pageRows(c), 0, n - 1);
    default:                 return -1;
    }
}

} // namespace glaze
