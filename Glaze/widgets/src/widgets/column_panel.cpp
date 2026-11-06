// ColumnPanel — kolumnhuvudet som delad maskin bakom Table (ListView,
// DataGrid) och TreeView. MÖNSTER: widgeten ritar SIG SJÄLV (hård
// regel 3) — men ColumnPanel ritar bara sin DEL (`drawHeader`), och
// subklasserna har var sin `draw()` med var sin `switch (c.ui)` runt den.
// Geometrin bor HÄR i LOGISKA px — EN sanning som ritningen läser rakt av och
// som musvägen projicerar till fysiska px (Draw::localX/localY);
// `scrollViewport` gör att remsorna och spelrummet gäller RADERNA och inte
// huvudet.
//
// SKRIVNA BESLUT (de långa motiveringarna står i column_panel.h):
//   huvudet på n rader är n PLATSER, AUTO räknar utan sorteringspilen, och
//   pilen står där en enradsrubrik står · basen vet ATT en sortering gäller,
//   subklassen VAD som sorteras (`orderChanged`).

#include "glaze/widgets/column_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

#include "glaze/app_window.h"
#include "glaze/draw.h"

#include "../column_sizing.h"
#include "../prop_parse.h"
#include "../value_cell.h"

namespace glaze {
namespace {

bool siffra(char ch) { return ch >= '0' && ch <= '9'; }

// ASCII-versalen VIKT till gemen — UTF-8 över 0x7F lämnas orörd (se
// begränsningen i table.h: kollation per språk är språkpaketets sak).
unsigned char vikt(char ch)
{
    const auto u = static_cast<unsigned char>(ch);
    return (u >= 'A' && u <= 'Z') ? static_cast<unsigned char>(u - 'A' + 'a')
                                  : u;
}

// HUVUDETS PLATSER. Ett huvud på `n` rader har n platser en
// radhöjd isär, och plats 0 står där ett enradshuvuds text står — luften
// över den första och under den sista är en rads (`tableHeaderH` mot
// radhöjden). En rubrik på `k` rader börjar på plats `förstaPlats`: överst,
// nederst eller i mitten (en halv plats är tillåten — tre rader i ett huvud
// på fyra står mitt emellan).
float förstaPlats(VAlign v, int k, int n)
{
    switch (v) {
    case VAlign::Top:    return 0.0f;
    case VAlign::Bottom: return float(n - k);
    case VAlign::Center:
    default:             return float(n - k) / 2.0f;
    }
}

// Platsens förskjutning från huvudets MITT, i logiska px. Ritningen lägger
// en rad i en rect lika hög som huvudet, flyttad så här långt, och
// `Draw::text` centrerar den i den — så för EN rad i ett enradshuvud är
// förskjutningen exakt 0 och recten exakt den rubriken alltid ritats i.
float platsSkift(float plats, int n, float textH)
{
    return (plats - float(n - 1) / 2.0f) * textH;
}

} // namespace

// NATURLIG JÄMFÖRELSE (column_panel.h) — tabellens rader och trädets syskon
// jämförs med samma regel.
int ColumnPanel::compareNatural(const std::string& a, const std::string& b)
{
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (siffra(a[i]) && siffra(b[j])) {
            std::size_t ia = i, jb = j;
            while (ia < a.size() && a[ia] == '0')
                ++ia;
            while (jb < b.size() && b[jb] == '0')
                ++jb;
            std::size_t ea = ia, eb = jb;
            while (ea < a.size() && siffra(a[ea]))
                ++ea;
            while (eb < b.size() && siffra(b[eb]))
                ++eb;
            // Fler siffror = större tal; lika många jämförs siffra för
            // siffra (tecknen är redan i talordning).
            if (ea - ia != eb - jb)
                return ea - ia < eb - jb ? -1 : 1;
            for (std::size_t k = 0; k < ea - ia; ++k)
                if (a[ia + k] != b[jb + k])
                    return a[ia + k] < b[jb + k] ? -1 : 1;
            i = ea;
            j = eb;
            continue;
        }
        const unsigned char ca = vikt(a[i]);
        const unsigned char cb = vikt(b[j]);
        if (ca != cb)
            return ca < cb ? -1 : 1;
        ++i;
        ++j;
    }
    if (i < a.size())
        return 1;
    if (j < b.size())
        return -1;
    return 0;
}

// ------------------------------------------------------------- kolumnerna

void ColumnPanel::addColumn(TrText text, float bredd, Align align,
                            std::optional<Align> headerAlign,
                            ColumnSizing sizing)
{
    columns_.push_back(
        Column{std::move(text), bredd, align, headerAlign, sizing, {}, false});
    headerChanged();
    contentChanged();
    orderChanged();   // en sorteringskolumn som inte fanns kan börja gälla
}

void ColumnPanel::setColumn(int index, TrText text, float bredd, Align align,
                            std::optional<Align> headerAlign,
                            ColumnSizing sizing)
{
    if (index < 0 || index >= columnCount()) {
        gfx::log("ColumnPanel(%s): setColumn(%d) utanför intervallet (%d "
                 "kolumner) — ignoreras", name.c_str(), index, columnCount());
        return;
    }
    // TILLDELNING: tooltip och rubrikkryss nollas (column_panel.h).
    columns_[std::size_t(index)] =
        Column{std::move(text), bredd, align, headerAlign, sizing, {}, false};
    headerChanged();
    contentChanged();
    invalidate();
    clampScroll();   // ett nytt läge kan ha krympt spelrummet i sidled
}

void ColumnPanel::setColumns(std::vector<Column> columns)
{
    columns_ = std::move(columns);
    headerChanged();
    contentChanged();
    columnsReplaced();
    // `sortColumn` lämnas ORÖRD även när den hamnar utanför: satserna i en
    // `.glazeui` körs i källordning, och en sortering som står före
    // kolumnerna ska gälla när de kommer. Utanför kolumnerna sorteras inget.
    orderChanged();
    // Färre kolumner — eller trädets huvud som försvann — kan ha krympt
    // spelrummet.
    clampScroll();
}

void ColumnPanel::setColumnWidth(int index, float bredd)
{
    if (index < 0 || index >= columnCount()) {
        gfx::log("ColumnPanel(%s): setColumnWidth(%d) utanför intervallet (%d "
                 "kolumner) — ignoreras", name.c_str(), index, columnCount());
        return;
    }
    if (bredd < 0.0f) {
        gfx::log("ColumnPanel(%s): setColumnWidth(%d, %g) — en negativ bredd, "
                 "ignoreras", name.c_str(), index, double(bredd));
        return;
    }
    Column& k = columns_[std::size_t(index)];
    if (k.width == bredd)
        return;   // hård regel 1: invalidera vid ÄNDRING
    k.width = bredd;
    headerChanged();   // en ny bredd bryter rubriken på nytt (AUTO-höjden)
    invalidate();
}

float ColumnPanel::columnWidth(int index) const
{
    return index >= 0 && index < columnCount()
               ? columns_[std::size_t(index)].width
               : 0.0f;
}

void ColumnPanel::setManualWidth(int index, float bredd)
{
    Column& k = columns_[std::size_t(index)];
    if (k.sizing == ColumnSizing::Manual && k.width == bredd)
        return;   // hård regel 1: invalidera vid ÄNDRING
    k.sizing = ColumnSizing::Manual;
    k.width  = bredd;
    headerChanged();
    invalidate();
    clampScroll();   // en Autosize-kolumn som blev fast kan ha krympt spelrummet
}

void ColumnPanel::setColumnSizing(int index, ColumnSizing sizing)
{
    if (index < 0 || index >= columnCount()) {
        gfx::log("ColumnPanel(%s): setColumnSizing(%d) utanför intervallet (%d "
                 "kolumner) — ignoreras", name.c_str(), index, columnCount());
        return;
    }
    Column& k = columns_[std::size_t(index)];
    if (k.sizing == sizing)
        return;   // hård regel 1: invalidera vid ÄNDRING
    k.sizing = sizing;
    headerChanged();
    invalidate();
    clampScroll();
}

ColumnSizing ColumnPanel::columnSizing(int index) const
{
    return index >= 0 && index < columnCount()
               ? columns_[std::size_t(index)].sizing
               : ColumnSizing::Manual;
}

void ColumnPanel::setColumnToolTip(int index, TrText toolTip)
{
    if (index < 0 || index >= columnCount()) {
        gfx::log("ColumnPanel(%s): setColumnToolTip(%d) utanför intervallet (%d "
                 "kolumner) — ignoreras", name.c_str(), index, columnCount());
        return;
    }
    TrText& t = columns_[std::size_t(index)].toolTip;
    if (t == toolTip)
        return;
    t = std::move(toolTip);
    // Ingen ritning bär den; tooltipen läses vid nästa vila (widget.h).
}

void ColumnPanel::setColumnHeaderCheck(int index, bool on)
{
    if (index < 0 || index >= columnCount()) {
        gfx::log("ColumnPanel(%s): setColumnHeaderCheck(%d) utanför intervallet "
                 "(%d kolumner) — ignoreras", name.c_str(), index, columnCount());
        return;
    }
    bool& k = columns_[std::size_t(index)].headerCheck;
    if (k == on)
        return;   // hård regel 1: invalidera vid ÄNDRING
    k = on;
    headerChanged();       // zonen tar rubrikens plats (AUTO-höjden)
    contentChanged(index); // och en del av den anpassade bredden
    invalidate();
}

bool ColumnPanel::columnHeaderCheck(int index) const
{
    return index >= 0 && index < columnCount() &&
           columns_[std::size_t(index)].headerCheck;
}

void ColumnPanel::contentChanged()
{
    ++fitGen_;
    ++contentGen_;
}

void ColumnPanel::contentChanged(int column)
{
    if (column < 0)
        return;
    if (std::size_t(column) >= colGen_.size())
        colGen_.resize(std::size_t(column) + 1, 0u);
    ++colGen_[std::size_t(column)];
    ++contentGen_;
}

// RUBRIKENS TOOLTIP: över en kolumns rubrik dess egen, annars widgetens.
// Remsorna hör inte till rubriken.
TrText ColumnPanel::toolTipAt(const Ctx& c, float x, float y) const
{
    if (!inScrollStrip(c, x, y) && inHeader(c, x, y)) {
        const int k = headerColumnAt(c, x);
        if (k >= 0 && !columns_[std::size_t(k)].toolTip.empty())
            return columns_[std::size_t(k)].toolTip;
    }
    return Widget::toolTipAt(c, x, y);
}

// ------------------------------------------------------------ sorteringen

void ColumnPanel::setSortColumn(int column)
{
    if (column < -1)
        column = -1;   // varje negativt index betyder "ingen"
    if (column == sortColumn_)
        return;
    sortColumn_ = column;
    orderChanged();
}

void ColumnPanel::setSortAscending(bool on)
{
    if (sortAscending_ == on)
        return;
    sortAscending_ = on;
    orderChanged();
}

void ColumnPanel::setSortable(bool on)
{
    if (sortable_ == on)
        return;
    sortable_ = on;
    invalidate();   // huvudets hover-affordans följer med
}

// ---------------------------------------------------------- kolumnhuvudet

void ColumnPanel::setHeaderLines(int lines)
{
    if (lines < 0) {
        gfx::log("ColumnPanel(%s): setHeaderLines(%d) — ett negativt "
                 "radantal, ignoreras", name.c_str(), lines);
        return;
    }
    if (lines == headerLines_)
        return;   // hård regel 1: invalidera vid ÄNDRING
    headerLines_ = lines;
    invalidate();
    // Ett lägre huvud ger raderna mer plats och kan krympa spelrummet.
    clampScroll();
}

void ColumnPanel::setHeaderVAlign(VAlign v)
{
    if (v == headerVAlign_)
        return;
    headerVAlign_ = v;
    invalidate();   // bara ritningen — höjden beror inte på justeringen
}

void ColumnPanel::languageChanged()
{
    headerChanged();
    contentChanged();   // rubrikerna är etiketter — den anpassade bredden följer
}

// Scrollen klampas OMEDELBART (ListBox::setItems — samma tvådelade vakt:
// Ctx:t byggs ur fönstrets renderer och font, som skapas först i `show()`).
// Geometrin klampar ändå vid varje läsning; det här är för att remsorna och
// hjulet ska läsa samma tal.
void ColumnPanel::clampScroll()
{
    if (!appWindow() || !appWindow()->isOpen())
        return;
    const Ctx        c = behaviorCtx();
    const gfx::Sizef r = scrollRange(c);
    setScroll(std::min(scrollX(), r.w), std::min(scrollY(), r.h));
}

void ColumnPanel::ensureColumnVisible(int column)
{
    if (!appWindow() || !appWindow()->isOpen() || column < 0 ||
        column >= columnCount())
        return;
    const Ctx   c  = behaviorCtx();
    const float fz = frozenW(c);
    if (column == 0 && fz > 0.0f)
        return;   // den fasta kolumnen syns alltid
    const float x0 = columnX(c, column);
    const float w  = columnW(c, column);
    const float vw = scrollViewport(c).w;
    float       s  = scrollXClamped(c);
    // Rullningsytan visar innehållets [s + fz, s + fz + vw): den fasta
    // kolumnen tar de första `fz`.
    if (x0 < s + fz)
        s = x0 - fz;
    else if (x0 + w > s + fz + vw)
        s = std::min(x0 - fz, x0 + w - fz - vw);   // en kolumn bredare än vyn: dess vänsterkant
    setScroll(s, scrollY());
}

// ------------------------------------------------------------ setProp

bool ColumnPanel::setProp(const std::string& key, const std::string& value)
{
    if (key == "sortColumn") {
        setSortColumn(std::atoi(value.c_str()));   // oparsbart ⇒ 0
        return true;
    }
    if (key == "sortAscending") {
        setSortAscending(propparse::parseBool(value));
        return true;
    }
    if (key == "sortable") {
        setSortable(propparse::parseBool(value));
        return true;
    }
    if (key == "headerLines") {
        // Ett HELTAL ur en C++-literal (talfältets `decimals`-prejudikat):
        // `atoi`s "oparsbart ⇒ 0" hade gjort skräp till AUTO, alltså ett
        // annat huvud än någon skrev.
        double v = 0.0;
        if (!propparse::parseDouble(value, v) || v != std::floor(v) ||
            std::abs(v) > double(std::numeric_limits<int>::max())) {
            gfx::log("ColumnPanel(%s): '%s' är inget radantal för "
                     "headerLines (ignoreras)", name.c_str(), value.c_str());
            return true;
        }
        setHeaderLines(int(v));   // negativt ⇒ sättarens logg + no-op
        return true;
    }
    if (key == "headerVAlign") {
        VAlign v = VAlign::Center;
        if (propparse::parseVAlign(value, v))
            setHeaderVAlign(v);
        else
            gfx::log("ColumnPanel(%s): ogiltig headerVAlign: %s (ignoreras)",
                     name.c_str(), value.c_str());
        return true;
    }
    return Panel::setProp(key, value);   // basen SIST — konventionen
}

// ------------------------------------------------------------- geometrin
// EN sanning i widget-lokala LOGISKA px, scroll-förskjuten (samma rymd som
// ritningen). Musvägen projicerar den — aldrig två uppsättningar mått.

float ColumnPanel::cellPadX(const Ctx& c) const
{
    return c.style->listPadX;
}

// Huvudets RADANTAL — det satta, eller AUTO:s: den rubrik som behöver flest
// rader i sin textbredd (kolumnen minus luften på båda sidor, UTAN
// sorteringspilen — column_panel.h). Cachat mot allt svaret beror på, för
// frågan ställs för varje cell som ritas.
int ColumnPanel::headerLineCount(const Ctx& c) const
{
    // AUTO behöver bredderna; det satta antalet gör det inte, och då frågas
    // inte heller innehållsbredden.
    return headerLines_ > 0 ? headerLines_ : headerLineCount(c, contentW(c));
}

int ColumnPanel::headerLineCount(const Ctx& c, float cw) const
{
    if (headerLines_ > 0)
        return headerLines_;
    const float  pad   = cellPadX(c);
    const float  lh    = Draw::lineHeight(c);
    const float  nyckelCw = hasAutosize() ? cw : -1.0f;
    HeaderCache& k     = headerCache_;
    if (k.valid && k.font == c.font && k.scale == c.scale && k.lineH == lh &&
        k.padX == pad && k.defaultW == c.style->columnDefaultW &&
        k.cw == nyckelCw && k.gen == headerGen_ && k.content == contentGen_)
        return k.lines;
    int n = 1;
    for (int i = 0; i < columnCount(); ++i) {
        // Rubrikkryssets zon står före texten (`drawHeader`).
        const float w = columnW(c, i, cw) - 2.0f * pad - headerCheckW(c, i);
        if (w > 0.0f)   // ritningen hoppar över en så smal kolumn
            n = std::max(n, int(Draw::elideLines(
                                    c, columns_[std::size_t(i)].text.str(),
                                    w, 0)
                                    .size()));
    }
    k = HeaderCache{c.font, c.scale, lh, pad, c.style->columnDefaultW,
                    nyckelCw, headerGen_, contentGen_, n, true};
    return n;
}

// EN rad är stilens `tableHeaderH` rakt av — utan att fråga fonten, så ett
// enradshuvud mäts exakt som det alltid gjort. Varje rad därutöver lägger
// till en RADHÖJD: luften över och under förblir en rads, och raderna står
// lika tätt som i all annan radbruten text (`Draw::textWrapped`). Utan
// huvud (`hasHeader`) är höjden 0.
float ColumnPanel::headerH(const Ctx& c) const
{
    return headerLines_ > 0 ? headerH(c, 0.0f) : headerH(c, contentW(c));
}

float ColumnPanel::headerH(const Ctx& c, float cw) const
{
    if (!hasHeader())
        return 0.0f;
    const int n = headerLineCount(c, cw);
    return n == 1 ? c.style->tableHeaderH
                  : c.style->tableHeaderH + float(n - 1) * Draw::lineHeight(c);
}

// Huvudets remsa — klampad till innehållsytan, så en lista lägre än sitt
// huvud aldrig ritar utanför sig själv.
gfx::Rectf ColumnPanel::headerRect(const Ctx& c) const
{
    const gfx::Rectf in = innerBounds(c);
    return gfx::Rectf{in.x, in.y, in.w, std::min(headerH(c), in.h)};
}

// RADERNAS yta — innehållsytan under huvudet och till höger om den fasta
// kolumnen (`frozenW`). Det är den Panel scrollar (`scrollViewport`),
// och den tabellens barn klipps mot (`clipRect`).
gfx::Rectf ColumnPanel::scrollViewport(const Ctx& c) const
{
    const gfx::Rectf in = innerBounds(c);
    const float      h  = std::min(headerH(c), in.h);
    const float      fz = std::min(frozenW(c), in.w);
    return gfx::Rectf{in.x + fz, in.y + h, in.w - fz, in.h - h};
}

// Innehållsbredden ur listans EGEN Ctx — bara när en Autosize-kolumn finns,
// så en lista utan den aldrig frågar `resolve()` här.
float ColumnPanel::contentW(const Ctx& c) const
{
    return hasAutosize() ? innerBounds(c).w : 0.0f;
}

bool ColumnPanel::hasAutosize() const
{
    return std::any_of(columns_.begin(), columns_.end(), [](const Column& k) {
        return k.sizing == ColumnSizing::Autosize;
    });
}

// KOLUMNENS BAS efter läget (column_sizing.h). Den anpassade bredden frågas
// bara för en AutoMin-kolumn — den kan mäta alla rader.
float ColumnPanel::columnBaseW(const Ctx& c, int column) const
{
    const Column& k = columns_[std::size_t(column)];
    return columnsizing::baseWidth(
        k.sizing, k.width, c.style->columnDefaultW,
        k.sizing == ColumnSizing::AutoMin ? fittedW(c, column) : 0.0f);
}

// DET SOM BLIR ÖVER per Autosize-kolumn: `cw` minus ALLA kolumners baser,
// delat lika. O(k).
float ColumnPanel::autosizeShare(const Ctx& c, float cw) const
{
    float baser = 0.0f;
    int   antal = 0;
    for (int i = 0; i < columnCount(); ++i) {
        baser += columnBaseW(c, i);
        if (columns_[std::size_t(i)].sizing == ColumnSizing::Autosize)
            ++antal;
    }
    return columnsizing::autosizeShare(cw, baser, antal);
}

// DEN ANPASSADE BREDDEN. Rubrikens bredaste rad (`\n` bryter, och en
// rubrik bryts aldrig mot bredden — den är ju det som mäts) och cellernas
// bredaste innehåll (`cellContentW`, subklassens), båda med cellens luft på
// båda sidor — rubriken dessutom med sorteringspilens plats (luft + pil +
// luft, drawHeader) när pilen kan visas på kolumnen, alltid reserverad så ett
// klick i huvudet aldrig ändrar en bredd — plus kantens tjocklek som andrum
// (se nedan). Aldrig smalare än `columnMinW`. Cachat per kolumn mot allt
// svaret beror på (`FitCache`).
float ColumnPanel::fittedW(const Ctx& c, int column) const
{
    if (column < 0 || column >= columnCount())
        return c.style->columnMinW;
    FitCache& k = fitCache_;
    if (!k.valid || k.font != c.font || k.style != c.style ||
        k.scale != c.scale || k.sortable != sortable_ ||
        k.sortCol != sortColumn_ || k.gen != fitGen_ ||
        k.w.size() != columns_.size()) {
        k = FitCache{c.font, c.style, c.scale, sortable_, sortColumn_, fitGen_,
                     std::vector<float>(columns_.size(), -1.0f),
                     std::vector<unsigned>(columns_.size(), 0u), true};
    }
    float&         w   = k.w[std::size_t(column)];
    unsigned&      gen = k.colGen[std::size_t(column)];
    const unsigned nu  = colGenOf(column);
    if (w >= 0.0f && gen == nu)
        return w;
    gen = nu;

    const auto& m   = *c.style;
    const float pad = cellPadX(c);
    float       rub = 0.0f;
    const std::string& text = columns_[std::size_t(column)].text.str();
    for (std::size_t b = 0; b <= text.size();) {
        std::size_t e = text.find('\n', b);
        if (e == std::string::npos)
            e = text.size();
        rub = std::max(rub, Draw::measure(c, text.substr(b, e - b)).w);
        b = e + 1;
    }
    rub += 2.0f * pad + headerCheckW(c, column);
    if (sortable_ || column == sortColumn_)
        rub += pad + 2.0f * m.chevronW;
    // ANDRUM: kantens tjocklek (en hårlinje = 1 fysisk px). Ritningen kortar
    // en text med ett strikt `<=` mot kolumnens bredd minus luften, och
    // (mått + luft) − luft ger inte alltid måttet tillbaka i flyttal — utan
    // andrum fick den bredaste texten en ellips på varannan kolumn.
    const float lucka =
        Draw::penWidth(c, Pen{c.theme->colors.border, m.borderWidth});
    const float inh = cellContentW(c, column);
    w = std::max({m.columnMinW, rub + lucka,
                  inh > 0.0f ? inh + 2.0f * pad + lucka : 0.0f});
    return w;
}

float ColumnPanel::columnW(const Ctx& c, int column) const
{
    return columnW(c, column, contentW(c));
}

// En AUTOSIZE-kolumn är sin bas plus sin andel av det som blir över; alla
// andra kolumner är sin bas. O(k) för en Autosize-kolumn.
float ColumnPanel::columnW(const Ctx& c, int column, float cw) const
{
    const float        bas = columnBaseW(c, column);
    const ColumnSizing l   = columns_[std::size_t(column)].sizing;
    if (l != ColumnSizing::Autosize)
        return bas;
    return columnsizing::effectiveWidth(l, bas, autosizeShare(c, cw));
}

float ColumnPanel::columnX(const Ctx& c, int column) const
{
    return columnX(c, column, contentW(c));
}

// Andelen räknas EN gång för hela summan — O(k) och inte O(k²).
float ColumnPanel::columnX(const Ctx& c, int column, float cw) const
{
    const float andel = hasAutosize() ? autosizeShare(c, cw) : 0.0f;
    float       summa = 0.0f;
    for (int i = 0; i < column && i < columnCount(); ++i)
        summa += columnsizing::effectiveWidth(columns_[std::size_t(i)].sizing,
                                              columnBaseW(c, i), andel);
    return summa;
}

float ColumnPanel::columnsW(const Ctx& c) const
{
    return columnsW(c, contentW(c));
}

float ColumnPanel::columnsW(const Ctx& c, float cw) const
{
    return columnX(c, columnCount(), cw);
}

float ColumnPanel::scrollXClamped(const Ctx& c) const
{
    return std::clamp(scrollX(), 0.0f, scrollRange(c).w);
}

float ColumnPanel::scrollYClamped(const Ctx& c) const
{
    return std::clamp(scrollY(), 0.0f, scrollRange(c).h);
}

gfx::Rectf ColumnPanel::headerCellRect(const Ctx& c, int column) const
{
    const gfx::Rectf h = headerRect(c);
    // Den fasta kolumnen rullar inte.
    const float sx = column == 0 && frozenW(c) > 0.0f ? 0.0f : scrollXClamped(c);
    return gfx::Rectf{h.x - sx + columnX(c, column), h.y,
                      columnW(c, column), h.h};
}

// ---------------------------------------------------------- rubrikkrysset

bool ColumnPanel::hasHeaderCheck(int column) const
{
    return column >= 0 && column < columnCount() &&
           columns_[std::size_t(column)].headerCheck &&
           headerCheckState(column).has_value();
}

float ColumnPanel::headerCheckW(const Ctx& c, int column) const
{
    return hasHeaderCheck(column) ? c.style->checkSize + c.style->checkGap
                                  : 0.0f;
}

gfx::Rectf ColumnPanel::headerCheckZone(const Ctx& c, int column) const
{
    const gfx::Rectf r = headerCellRect(c, column);
    return gfx::Rectf{r.x + cellPadX(c), r.y, headerCheckW(c, column), r.h};
}

int ColumnPanel::headerCheckAt(const Ctx& c, float mx, float my) const
{
    if (!inHeader(c, mx, my))
        return -1;
    const int k = headerColumnAt(c, mx);
    if (k < 0 || !hasHeaderCheck(k))
        return -1;
    const float lx = Draw::localX(c, resolve(c), mx);
    const gfx::Rectf z = headerCheckZone(c, k);
    return lx >= z.x && lx < z.x + z.w ? k : -1;
}

// ---------------------------------------------------------------- träffen

bool ColumnPanel::inHeader(const Ctx& c, float mx, float my) const
{
    const gfx::Rectf base = resolve(c);
    return headerRect(c).contains(Draw::localX(c, base, mx),
                                  Draw::localY(c, base, my));
}

int ColumnPanel::headerColumnAt(const Ctx& c, float mx) const
{
    const float lx = Draw::localX(c, resolve(c), mx);
    for (int i = 0; i < columnCount(); ++i) {
        const gfx::Rectf r = headerCellRect(c, i);
        if (lx >= r.x && lx < r.x + r.w)
            return i;
    }
    return -1;
}

// Avdelaren är en kolumns HÖGRA kant, greppbar `columnGrip` åt båda håll.
// Sökningen går bakifrån, så när två zoner möts vinner den högra kolumnen —
// den man ser närmast pekaren när en kolumn dragits smal.
int ColumnPanel::dividerAt(const Ctx& c, float mx) const
{
    const float      lx = Draw::localX(c, resolve(c), mx);
    const float      g  = c.style->columnGrip;
    const gfx::Rectf hb = headerRect(c);
    const float      fz = frozenW(c);
    for (int i = columnCount() - 1; i >= 0; --i) {
        if (columns_[std::size_t(i)].sizing == ColumnSizing::Autosize)
            continue;   // en Autosize-kolumns kant följer bredden (Qt:s Stretch)
        const gfx::Rectf r    = headerCellRect(c, i);
        const float      kant = r.x + r.w;
        // Utrullad — ingen synlig avdelare att greppa; en rullande kolumns
        // kant under den fasta kolumnen är också utrullad.
        if (kant < hb.x || kant > hb.x + hb.w ||
            (fz > 0.0f && i > 0 && kant <= hb.x + fz))
            continue;
        if (std::abs(lx - kant) <= g)
            return i;
    }
    return -1;
}

// ------------------------------------------------------------- ritningen

// HUVUDET, per UI. Det som är gemensamt: rubriken står på huvudets rader
// (`Draw::elideLines` — `\n`, ordgräns, ellips) i kolumnens RUBRIKjustering
// och på platserna `headerVAlign` väljer, sorteringskolumnen bär
// dropdownens pil (∧ stigande, ∨ fallande) i högerkanten där en enradsrubrik
// står, och avdelarna är hårlinjer över hela huvudets höjd som TÄNDS i
// accent under pekaren och medan de dras — affordansen före pekarformen och
// kvar efter den.
//
//   SLATE         huvudet en remsa i `windowBg` (barernas yta — huvudet är
//                 en liten bar ovanpå listan), hårlinje `border` mot raderna
//   GLAZE         samma remsa, men linjerna i den BESTÄMDA `buttonBorder`
//                 som listans kontur (familjens kant bär formen)
//   MINIMALISTIC  ingen yta i vila — huvudet är sin text och sin linje
//
//   vila     rubrik `text`, pil `textDim`, avdelare `border`
//   hover    rubriken (när sorterbar) `buttonHover`, avdelaren `accent`
//   tryck    rubriken `buttonPress`
//   inaktiv  rubrik `textDim`, pil `border`, ingen hover
void ColumnPanel::drawHeader(const Ctx& c) const
{
    const gfx::Rectf hb = headerRect(c);
    if (hb.h <= 0.0f)
        return;
    const auto&      m    = *c.style;
    const auto&      col  = c.theme->colors;
    const bool       på   = isEnabled();
    const bool       tänd = på && sortable_;
    const float      pad  = cellPadX(c);
    const gfx::Color fg   = på ? col.text : col.textDim;
    const gfx::Color pil  = på ? col.textDim : col.border;
    const gfx::Color linje = headerLineColor(c);
    // Huvudets rader: n platser en radhöjd isär, och pilen på den
    // plats en ENRADSrubrik hade fått. Ett enradshuvud ger skiftet 0.
    const int   n     = headerLineCount(c);
    const float textH = Draw::lineHeight(c);   // en TEXTrad, inte `rowH`
    const float pilSkift =
        platsSkift(förstaPlats(headerVAlign_, 1, n), n, textH);

    Draw::clip(c, hb);
    drawHeaderSurface(c, hb);

    // EN rubrik. Dess ritning är lika för den rullande och den fasta kolumnen
    // — bara klippet runt dem skiljer.
    const auto rubrik = [&](int i) {
        const gfx::Rectf r = headerCellRect(c, i);
        if (r.x > hb.x + hb.w || r.x + r.w < hb.x)
            return;
        const bool dras    = på && pressed() && resizeColumn_ == i;
        const bool tryckt  = tänd && pressed() && pressHeader_ == i &&
                            pressHeaderStill_;
        const bool het     = tänd && hovered() && hotHeader_ == i &&
                         hotDivider_ < 0 && resizeColumn_ < 0;
        if (tryckt)
            Draw::rect(c, r, 0.0f, col.buttonPress);
        else if (het)
            Draw::rect(c, r, 0.0f, col.buttonHover);

        float textHöger = r.x + r.w - pad;
        if (i == sortColumn_) {
            const gfx::Rectf chev{r.x + r.w - pad - 2.0f * m.chevronW,
                                  r.y + r.h / 2.0f + pilSkift - m.chevronH,
                                  2.0f * m.chevronW, 2.0f * m.chevronH};
            Draw::chevron(c, chev, sortAscending_ ? Dir::Up : Dir::Down,
                          Pen{pil, m.borderWidth});
            textHöger = chev.x - pad;
        }
        // RUBRIKKRYSSET först i rubriken, i tre lägen, där en enradsrubrik
        // står (pilens plats i höjdled); texten börjar efter zonen.
        float tx = r.x + pad;
        if (const auto läge = hasHeaderCheck(i) ? headerCheckState(i)
                                                : std::nullopt) {
            const float s = m.checkSize;
            valuecell::drawCheckBox(
                c, {tx, r.y + r.h / 2.0f + pilSkift - s / 2.0f, s, s}, *läge,
                på ? col.buttonBorder : col.border, fg);
            tx += headerCheckW(c, i);
        }
        // RUBRIKENS RADER: var och en i en rect lika hög som huvudet,
        // flyttad till sin plats (`platsSkift`) — `Draw::text` centrerar den
        // där. En rubrik på EN rad i ett enradshuvud ritas därmed i exakt den
        // rect och med exakt den text (`elide`) den alltid ritats med.
        if (textHöger > tx) {
            const Column& k     = columns_[std::size_t(i)];
            const auto    rader = Draw::elideLines(c, k.text.str(),
                                                   textHöger - tx, n);
            const float   först =
                förstaPlats(headerVAlign_, int(rader.size()), n);
            for (std::size_t j = 0; j < rader.size(); ++j)
                Draw::text(c,
                           {tx, r.y + platsSkift(först + float(j), n, textH),
                            textHöger - tx, r.h},
                           rader[j], fg, k.headerAlign.value_or(k.align));
        }

        // Avdelaren i kolumnens HÖGRA kant — tänd under pekaren och medan
        // den dras (`focusWidth`: en markering som ska synas, som ringen).
        const float kx    = r.x + r.w;
        const bool  tänds = dras || (på && hovered() && hotDivider_ == i &&
                                      !pressed());
        Draw::line(c, kx, r.y, kx, r.y + r.h,
                   tänds ? Pen{col.accent, m.focusWidth}
                         : Pen{linje, m.borderWidth});
    };
    // Den FASTA kolumnens rubrik står kvar och ritas sist; de
    // rullande klipps mot ytan till höger om den, så ingen glider in under den.
    const float fz = frozenW(c);
    if (fz > 0.0f) {
        Draw::clip(c, gfx::Rectf{hb.x + fz, hb.y, hb.w - fz, hb.h});
        for (int i = 1; i < columnCount(); ++i)
            rubrik(i);
        Draw::endClip(c);
        rubrik(0);
    } else {
        for (int i = 0; i < columnCount(); ++i)
            rubrik(i);
    }
    Draw::line(c, hb.x, hb.y + hb.h, hb.x + hb.w, hb.y + hb.h,
               Pen{linje, m.borderWidth});
    Draw::endClip(c);
}

// HUVUDETS YTA och hårlinjens färg — `drawHeader`s recept (tabellen ovan),
// delat med radrubrikernas remsa.
void ColumnPanel::drawHeaderSurface(const Ctx& c, const gfx::Rectf& r) const
{
    switch (c.ui) {
    case UI::Minimalistic:
        break;   // ingen yta i vila — familjens grundsats
    case UI::Glaze:
    case UI::Slate:
    default:
        Draw::rect(c, r, 0.0f, c.theme->colors.windowBg);
        break;
    }
}

gfx::Color ColumnPanel::headerLineColor(const Ctx& c) const
{
    const auto& col = c.theme->colors;
    return c.ui == UI::Glaze && isEnabled() ? col.buttonBorder : col.border;
}

// ------------------------------------------------------------- beteendet

// HUVUDETS TRYCK. Remsorna ligger överst och får trycket först (ListBoxens
// remsa-först, och `Panel::mouseDown` nollar dess drag-läge även utanför
// remsan). Därefter avdelaren — den slår rubriken, för den ligger delvis över
// grannens rubrik — och sist rubriken, som blir ett SORTERINGSKLICK om
// trycket släpps över den.
bool ColumnPanel::headerMouseDown(const Ctx& c, float mx, float my)
{
    pressHeader_  = -1;
    resizeColumn_ = -1;
    const bool remsa = inScrollStrip(c, mx, my);
    Panel::mouseDown(c, mx, my);
    if (remsa)
        return true;
    if (!inHeader(c, mx, my))
        return false;

    const int avd = dividerAt(c, mx);
    if (avd >= 0) {
        resizeColumn_ = avd;
        resizeStartX_ = Draw::localX(c, resolve(c), mx);
        resizeStartW_ = columnW(c, avd);
    } else if (const int kryss = headerCheckAt(c, mx, my); kryss >= 0) {
        // RUBRIKKRYSSET vänds direkt och sorterar aldrig — inte heller vid
        // släppet, för trycket är ingen rubriks. En handler som rev listan
        // lämnar den orörd.
        invalidate();
        headerCheckToggled(kryss);
        return true;
    } else {
        pressHeader_      = headerColumnAt(c, mx);
        pressHeaderStill_ = pressHeader_ >= 0;
    }
    invalidate();
    return true;
}

// HUVUDETS DUBBELKLICK — kommer EFTER det andra trycket, som redan
// har börjat ett avdelardrag (`headerMouseDown`). Dubbelklicket på en avdelare
// skriver den anpassade bredden som en fast bredd och AVSLUTAR det draget, så
// att en musrörelse medan knappen hålls inte skriver över den med draget från
// den gamla bredden. Remsan och resten av huvudet är också huvudets: true, så
// subklassen aktiverar ingen rad.
bool ColumnPanel::headerDoubleClick(const Ctx& c, float mx, float my)
{
    if (inScrollStrip(c, mx, my))
        return true;
    if (!inHeader(c, mx, my))
        return false;
    const int avd = dividerAt(c, mx);
    if (avd >= 0) {
        resizeColumn_ = -1;
        setManualWidth(avd, fittedW(c, avd));   // "anpassa nu" — Manual
    }
    return true;   // i rubrikkryssets zon har båda trycken redan vänt det
}

void ColumnPanel::mouseDrag(const Ctx& c, float mx, float my)
{
    Panel::mouseDrag(c, mx, my);   // tumdraget i en remsa
    if (resizeColumn_ >= 0) {
        // Bredden följer pekaren från greppets punkt, aldrig smalare än
        // stilens `columnMinW`, och kolumnen blir `Manual`. Invaliderar vid
        // ändring.
        const float lx = Draw::localX(c, resolve(c), mx);
        setManualWidth(resizeColumn_,
                       std::max(c.style->columnMinW,
                                resizeStartW_ + (lx - resizeStartX_)));
        return;
    }
    if (pressHeader_ >= 0) {
        const bool kvar =
            inHeader(c, mx, my) && headerColumnAt(c, mx) == pressHeader_;
        if (kvar != pressHeaderStill_) {
            pressHeaderStill_ = kvar;
            invalidate();
        }
    }
}

void ColumnPanel::mouseMove(const Ctx& c, float mx, float my)
{
    Panel::mouseMove(c, mx, my);   // het tumme (invaliderar vid ändring)
    int rubrik = -1, avd = -1;
    if (!inScrollStrip(c, mx, my) && inHeader(c, mx, my)) {
        avd    = dividerAt(c, mx);
        // Rubrikkryssets zon är ingen sortering — ingen rubrikhover där.
        rubrik = avd < 0 && headerCheckAt(c, mx, my) < 0 ? headerColumnAt(c, mx)
                                                          : -1;
    }
    if (rubrik != hotHeader_ || avd != hotDivider_) {
        hotHeader_  = rubrik;
        hotDivider_ = avd;
        invalidate();   // hård regel 1: bara vid ÄNDRING
    }
}

// SLÄPPET (`mouseUp`). Ett avdelardrag är klart — bredden
// sattes redan under draget, så det enda som återstår är att släcka det. Ett
// tryck på en rubrik SORTERAR om det släpps över samma rubrik (en knapps
// semantik: dra bort för att ångra); samma kolumn igen vänder riktningen,
// en ny börjar stigande. Ingen användarkod körs — svaret är false och
// invalideringen vår egen (regel 1).
bool ColumnPanel::mouseUp(const Ctx& c, float mx, float my)
{
    const int rubrik = pressHeader_;
    const bool drog  = resizeColumn_ >= 0;
    pressHeader_     = -1;
    resizeColumn_    = -1;
    if (drog || rubrik >= 0)
        invalidate();
    if (rubrik < 0 || !isEnabled() || !sortable_ || !inHeader(c, mx, my) ||
        headerColumnAt(c, mx) != rubrik || dividerAt(c, mx) >= 0)
        return false;
    if (rubrik == sortColumn_) {
        setSortAscending(!sortAscending_);
    } else {
        sortAscending_ = true;   // en ny kolumn börjar alltid stigande
        setSortColumn(rubrik);
    }
    // Den aktuella raden står kvar som RAD — subklassen ser till att den
    // syns på sin nya plats.
    headerSorted();
    return false;
}

// ↔ över en avdelare, och under HELA draget — AppWindow frågar den som
// håller trycket först, så pekaren behåller formen även när den hunnit
// före avdelaren. Samma geometri som träffen (`dividerAt`).
gfx::PointerShape ColumnPanel::cursorAt(const Ctx& c, float mx, float my) const
{
    if (resizeColumn_ >= 0 && pressed())
        return gfx::PointerShape::ResizeWE;
    if (!inScrollStrip(c, mx, my) && inHeader(c, mx, my) &&
        dividerAt(c, mx) >= 0)
        return gfx::PointerShape::ResizeWE;
    return gfx::PointerShape::Arrow;
}

} // namespace glaze
