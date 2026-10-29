// dock_model — se dock_model.h. Strukturen och geometrin; inga fönster och
// ingen ritning. Inga tal och inga färger: måtten kommer ur `c.style`, och
// det enda som räknas här är fördelningen av dem.

#include "dock_model.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "glaze/draw.h"
#include "glaze/widgets/dock_panel.h"
#include "glaze/widgets/tool_bar.h"

namespace glaze::detail {

// ---------------------------------------------------------- hjälparna

bool isDockEdge(Dock d)
{
    return d == Dock::Left || d == Dock::Top || d == Dock::Right ||
           d == Dock::Bottom;
}

bool isStandingEdge(Dock d)
{
    return d == Dock::Left || d == Dock::Right;
}

bool docksOpen(const DockPanel* p)
{
    return p && p->isVisible() && p->isPinned() && !p->isCollapsed();
}

DockPanel* shownTab(const DockCell& c)
{
    if (docksOpen(c.active) &&
        std::find(c.tabs.begin(), c.tabs.end(), c.active) != c.tabs.end())
        return c.active;
    for (DockPanel* p : c.tabs)
        if (docksOpen(p))
            return p;
    return nullptr;
}

int openTabs(const DockCell& c)
{
    int n = 0;
    for (const DockPanel* p : c.tabs)
        if (docksOpen(p))
            ++n;
    return n;
}

namespace {

bool iListan(const std::vector<const DockPanel*>& v, const DockPanel* p)
{
    return std::find(v.begin(), v.end(), p) != v.end();
}

// En remsa vid `edge` med panelen ensam i en cell — `add`s och `dockAtEdge`s.
DockStrip ensamRemsa(DockPanel& p, Dock edge)
{
    DockCell cell;
    cell.tabs.push_back(&p);
    cell.active = &p;
    DockStrip s;
    s.edge = edge;
    s.cells.push_back(std::move(cell));
    return s;
}

} // namespace

// ---------------------------------------------------------- talen

namespace {

bool giltigVikt(float v)
{
    return std::isfinite(v) && v > 0.0f;
}

// Vikterna relativt den största, golvade — i double hela vägen till floaten.
std::vector<float> normaliserade(const std::vector<double>& w)
{
    double störst = 0.0;
    for (const double v : w)
        störst = std::max(störst, v);
    std::vector<float> ut(w.size(), 1.0f);
    if (störst <= 0.0)
        return ut;
    for (std::size_t i = 0; i < w.size(); ++i)
        ut[i] = static_cast<float>(std::max(w[i] / störst,
                                            static_cast<double>(kMinWeight)));
    return ut;
}

} // namespace

void normalizeWeights(DockStrip& s)
{
    std::vector<double> w;
    w.reserve(s.cells.size());
    double störst = 0.0;
    for (const DockCell& c : s.cells)
        if (giltigVikt(c.weight))
            störst = std::max(störst, static_cast<double>(c.weight));
    for (const DockCell& c : s.cells)
        w.push_back(giltigVikt(c.weight) ? static_cast<double>(c.weight)
                                         : std::max(störst, 1.0));
    const std::vector<float> ut = normaliserade(w);
    for (std::size_t i = 0; i < s.cells.size(); ++i)
        s.cells[i].weight = ut[i];
}

std::vector<float> weightsForLengths(
    const DockStrip& s, const std::vector<std::pair<int, float>>& lengths)
{
    std::vector<double> w;
    w.reserve(s.cells.size());
    for (const DockCell& c : s.cells)
        w.push_back(giltigVikt(c.weight) ? static_cast<double>(c.weight) : 1.0);
    // De synligas gamla SUMMA är skalan — de dolda cellernas vikter står i
    // den, och de nya ska stå i samma.
    double gammal = 0.0, summaL = 0.0;
    for (const auto& [ci, l] : lengths) {
        if (ci < 0 || static_cast<std::size_t>(ci) >= w.size() ||
            !std::isfinite(l) || l <= 0.0f)
            return {};
        gammal += w[static_cast<std::size_t>(ci)];
        summaL += static_cast<double>(l);
    }
    if (lengths.empty() || gammal <= 0.0 || summaL <= 0.0)
        return {};
    for (const auto& [ci, l] : lengths)
        w[static_cast<std::size_t>(ci)] =
            gammal * static_cast<double>(l) / summaL;
    return normaliserade(w);
}

float panelSize(float v)
{
    return std::isfinite(v) && v > 0.0f ? v : 0.0f;
}

// ---------------------------------------------------------- strukturen

void DockModel::add(DockPanel& p, Dock edge)
{
    if (!isDockEdge(edge) || contains(p))
        return;
    strips.push_back(ensamRemsa(p, edge));
}

bool DockModel::remove(const DockPanel* p)
{
    for (DockStrip& s : strips)
        for (DockCell& c : s.cells) {
            const auto it = std::find(c.tabs.begin(), c.tabs.end(), p);
            if (it == c.tabs.end())
                continue;
            const std::size_t i = static_cast<std::size_t>(it - c.tabs.begin());
            c.tabs.erase(it);
            // Grannen till höger glider in på platsen — den till vänster när
            // den borttagna var sist (TabControls regel).
            if (c.active == p)
                c.active = c.tabs.empty()
                               ? nullptr
                               : c.tabs[std::min(i, c.tabs.size() - 1)];
            prune();
            return true;
        }
    return false;
}

bool DockModel::dockAtEdge(DockPanel& p, Dock edge)
{
    if (!isDockEdge(edge))
        return false;
    const DockModel före = *this;
    remove(p);
    strips.insert(strips.begin(), ensamRemsa(p, edge));
    if (sameShape(före)) {
        *this = före;   // redan ensam ytterst vid kanten — vikten står kvar
        return false;
    }
    return true;
}

bool DockModel::dockOn(DockPanel& p, DockPanel& target, Dock where)
{
    if (&p == &target || where == Dock::None || !contains(target))
        return false;
    if (where == Dock::Fill) {
        const DockCell* hem = cellOf(target);
        if (hem && std::find(hem->tabs.begin(), hem->tabs.end(), &p) !=
                       hem->tabs.end())
            return false;   // redan en flik i målets cell
    }
    const DockModel före = *this;
    remove(p);   // målet står kvar: p är inte målet, så dess cell lever

    const int si = stripIndexOf(target);
    DockStrip& s  = strips[static_cast<std::size_t>(si)];
    std::size_t ci = 0;
    while (std::find(s.cells[ci].tabs.begin(), s.cells[ci].tabs.end(),
                     &target) == s.cells[ci].tabs.end())
        ++ci;

    const bool stående = isStandingEdge(s.edge);
    const bool längs   = stående ? (where == Dock::Top || where == Dock::Bottom)
                                 : (where == Dock::Left || where == Dock::Right);
    if (where == Dock::Fill) {
        s.cells[ci].tabs.push_back(&p);
        s.cells[ci].active = &p;
    } else if (längs) {
        // En ny cell före eller efter målets, och målets vikt delas lika:
        // de andra cellerna behåller sin längd. Remsan NORMALISERAS först:
        // målets vikt är då minst `kMinWeight`, så halvan är ett
        // normalt, strikt positivt float och ingen underflödad nolla som
        // `layout()` hade skrivit och `setLayout()` avvisat.
        normalizeWeights(s);
        const bool förstMål = where == Dock::Bottom || where == Dock::Right;
        s.cells[ci].weight /= 2.0f;
        DockCell ny;
        ny.tabs.push_back(&p);
        ny.active = &p;
        ny.weight = s.cells[ci].weight;
        s.cells.insert(s.cells.begin() +
                           static_cast<std::ptrdiff_t>(förstMål ? ci + 1 : ci),
                       std::move(ny));
    } else {
        // TVÄRS: en ny remsa vid SAMMA kant, direkt utanför målets (mot
        // kanten — skärs före) eller innanför (skärs efter). Panelen byter
        // alltså kant till målets.
        const bool utanför = where == s.edge;
        strips.insert(strips.begin() +
                          static_cast<std::ptrdiff_t>(utanför ? si : si + 1),
                      ensamRemsa(p, s.edge));
    }
    if (sameShape(före)) {
        *this = före;
        return false;
    }
    return true;
}

bool DockModel::activate(DockPanel& p)
{
    DockCell* c = cellOf(p);
    if (!c || c->active == &p)
        return false;
    c->active = &p;
    return true;
}

bool DockModel::normalize()
{
    std::vector<DockPanel*> flytta;
    std::vector<DockPanel*> flyter;
    for (const DockStrip& s : strips)
        for (const DockCell& c : s.cells)
            for (DockPanel* p : c.tabs) {
                if (p->isFloating())
                    flyter.push_back(p);
                else if (p->dock != s.edge)
                    flytta.push_back(p);
            }
    // EN FLYTANDE PANEL står i sitt eget fönster — ingen remsa.
    for (DockPanel* p : flyter)
        remove(*p);
    // …och ett FLYTANDE VERKTYGSFÄLT likaså — inget band.
    // Det kommer tillbaka sist i band `band()` vid `dock` när det dockas.
    const std::size_t fältFöre = bars.size();
    bars.erase(std::remove_if(bars.begin(), bars.end(),
                              [](const DockBar& d) {
                                  return d.bar->isFloating();
                              }),
               bars.end());
    // I MODELLENS ordning, och var och en blir innersta vid sin nya kant —
    // så den inbördes ordningen står kvar (previewn, som sätter fälten EFTER
    // trädbygget, får samma remsor som appen, som sätter dem före).
    for (DockPanel* p : flytta) {
        remove(*p);
        if (isDockEdge(p->dock))
            add(*p, p->dock);
    }
    bool ändrad =
        !flytta.empty() || !flyter.empty() || bars.size() != fältFöre;

    // VERKTYGSFÄLTEN: kant och nyckel ur fälten — platsen i ordningen står
    // kvar, så bandet härleds ur samma tal och samma ordning i previewn och
    // i appen. Ett fält UTAN kant står kvar på sin plats i ordningen (det
    // står i huvudytan och i inget band): får det en kant senare hamnar det
    // där adoptionen satte det, i appen som i previewn.
    for (DockBar& d : bars) {
        const ToolBar* tb = d.bar;
        if (d.edge != tb->dock || d.key != tb->band()) {
            d.edge = tb->dock;
            d.key  = tb->band();
            ändrad = true;
        }
    }
    return ändrad;
}

// ---------------------------------------------------------- verktygsfälten

void DockModel::addBar(ToolBar& b)
{
    if (b.isFloating() || containsBar(b))
        return;
    bars.push_back(DockBar{&b, b.dock, b.band()});
}

bool DockModel::barInBand(const ToolBar& b) const
{
    for (const DockBar& d : bars)
        if (d.bar == &b)
            return isDockEdge(d.edge);
    return false;
}

bool DockModel::removeBar(const ToolBar* b)
{
    for (auto it = bars.begin(); it != bars.end(); ++it)
        if (it->bar == b) {
            bars.erase(it);
            return true;
        }
    return false;
}

bool DockModel::containsBar(const ToolBar& b) const
{
    for (const DockBar& d : bars)
        if (d.bar == &b)
            return true;
    return false;
}

std::vector<DockBand> DockModel::bandsAt(Dock edge) const
{
    std::vector<int> nycklar;
    for (const DockBar& d : bars)
        if (d.edge == edge &&
            std::find(nycklar.begin(), nycklar.end(), d.key) == nycklar.end())
            nycklar.push_back(d.key);
    std::sort(nycklar.begin(), nycklar.end());
    std::vector<DockBand> ut;
    for (std::size_t i = 0; i < nycklar.size(); ++i) {
        DockBand band;
        band.edge  = edge;
        band.index = static_cast<int>(i);
        band.key   = nycklar[i];
        for (const DockBar& d : bars)
            if (d.edge == edge && d.key == nycklar[i])
                band.bars.push_back(d.bar);
        ut.push_back(std::move(band));
    }
    return ut;
}

void DockModel::packBarKeys()
{
    for (const Dock kant : kBandEdges) {
        const std::vector<DockBand> band = bandsAt(kant);
        for (DockBar& d : bars) {
            if (d.edge != kant)
                continue;
            for (const DockBand& b : band)
                if (b.key == d.key) {
                    d.key = b.index;
                    break;
                }
        }
    }
}

bool DockModel::moveBar(ToolBar& b, Dock edge, const ToolBar* inBandOf,
                       const ToolBar* before)
{
    if (!isDockEdge(edge) || inBandOf == &b || before == &b)
        return false;
    const DockModel före    = *this;
    DockBar         flyttad = DockBar{&b, edge, 0};
    // EN FLYTT — eller en INSÄTTNING av ett fält som saknas: ett
    // flytande fält har ingen plats i modellen förrän det dockas.
    if (const auto it =
            std::find_if(bars.begin(), bars.end(),
                         [&b](const DockBar& d) { return d.bar == &b; });
        it != bars.end()) {
        flyttad = *it;
        bars.erase(it);
    }
    packBarKeys();   // 0, 1, 2 … utan fältet — ett nytt band får antalet
    auto post = [this](const ToolBar* t) {
        return std::find_if(bars.begin(), bars.end(),
                            [t](const DockBar& d) { return d.bar == t; });
    };
    flyttad.edge = edge;
    if (!inBandOf) {
        // ETT NYTT INNERSTA BAND (nya band skapas bara innerst).
        flyttad.key = static_cast<int>(bandsAt(edge).size());
        bars.push_back(flyttad);
    } else {
        const auto mål = post(inBandOf);
        if (mål == bars.end() || mål->edge != edge) {
            *this = före;
            return false;   // bandet finns inte vid den kanten
        }
        flyttad.key = mål->key;
        // Före `before` när det står i samma band; annars efter bandets
        // sista fält i ordningen.
        auto plats = before ? post(before) : bars.end();
        if (plats == bars.end() || plats->edge != edge ||
            plats->key != flyttad.key) {
            plats = bars.end();
            for (auto i = bars.begin(); i != bars.end(); ++i)
                if (i->edge == edge && i->key == flyttad.key)
                    plats = std::next(i);
        }
        bars.insert(plats, flyttad);
    }
    packBarKeys();
    if (sameShape(före)) {
        *this = före;   // samma band, samma plats — nycklarna står kvar
        return false;
    }
    return true;
}

bool DockModel::contains(const DockPanel& p) const
{
    return cellOf(p) != nullptr;
}

DockCell* DockModel::cellOf(const DockPanel& p)
{
    for (DockStrip& s : strips)
        for (DockCell& c : s.cells)
            if (std::find(c.tabs.begin(), c.tabs.end(), &p) != c.tabs.end())
                return &c;
    return nullptr;
}

const DockCell* DockModel::cellOf(const DockPanel& p) const
{
    for (const DockStrip& s : strips)
        for (const DockCell& c : s.cells)
            if (std::find(c.tabs.begin(), c.tabs.end(), &p) != c.tabs.end())
                return &c;
    return nullptr;
}

int DockModel::stripIndexOf(const DockPanel& p) const
{
    for (std::size_t i = 0; i < strips.size(); ++i)
        for (const DockCell& c : strips[i].cells)
            if (std::find(c.tabs.begin(), c.tabs.end(), &p) != c.tabs.end())
                return static_cast<int>(i);
    return -1;
}

std::vector<DockPanel*> DockModel::panels() const
{
    std::vector<DockPanel*> ut;
    for (const DockStrip& s : strips)
        for (const DockCell& c : s.cells)
            ut.insert(ut.end(), c.tabs.begin(), c.tabs.end());
    return ut;
}

bool DockModel::sameShape(const DockModel& o) const
{
    if (strips.size() != o.strips.size())
        return false;
    for (std::size_t i = 0; i < strips.size(); ++i) {
        const DockStrip& a = strips[i];
        const DockStrip& b = o.strips[i];
        if (a.edge != b.edge || a.cells.size() != b.cells.size())
            return false;
        for (std::size_t j = 0; j < a.cells.size(); ++j)
            if (a.cells[j].tabs != b.cells[j].tabs ||
                a.cells[j].active != b.cells[j].active)
                return false;
    }
    for (const Dock kant : kBandEdges) {
        const std::vector<DockBand> a = bandsAt(kant);
        const std::vector<DockBand> b = o.bandsAt(kant);
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (a[i].bars != b[i].bars)
                return false;
    }
    return true;
}

void DockModel::prune()
{
    for (DockStrip& s : strips) {
        s.cells.erase(std::remove_if(s.cells.begin(), s.cells.end(),
                                     [](const DockCell& c) {
                                         return c.tabs.empty();
                                     }),
                      s.cells.end());
        for (DockCell& c : s.cells)
            if (std::find(c.tabs.begin(), c.tabs.end(), c.active) ==
                c.tabs.end())
                c.active = c.tabs.front();
    }
    strips.erase(std::remove_if(strips.begin(), strips.end(),
                                [](const DockStrip& s) {
                                    return s.cells.empty();
                                }),
                 strips.end());
}

std::vector<std::pair<Dock, DockPanel*>> handlesInOrder(const DockModel& m,
                                                        const DockLive* live)
{
    std::vector<std::pair<Dock, DockPanel*>> ut;
    for (const Dock kant : {Dock::Top, Dock::Bottom, Dock::Left, Dock::Right})
        for (const DockStrip& s : m.strips) {
            if (s.edge != kant)
                continue;
            for (const DockCell& c : s.cells)
                for (DockPanel* p : c.tabs) {
                    if (!p->isVisible() || (live && live->dragged == p))
                        continue;
                    if (p->isCollapsed() || !p->isPinned() ||
                        (live && iListan(live->collapsed, p)))
                        ut.push_back({kant, p});
                }
        }
    return ut;
}

// ------------------------------------------------------------ geometrin

gfx::Rectf DockGeometry::rectOf(const DockPanel* p) const
{
    if (p && p == overlay.panel)
        return overlay.rect;
    const Cell* c = cellShowing(p);
    return c ? c->body : gfx::Rectf{};
}

const DockGeometry::Cell* DockGeometry::cellShowing(const DockPanel* p) const
{
    for (const Cell& c : cells)
        if (c.shown == p)
            return &c;
    return nullptr;
}

gfx::Rectf DockGeometry::barRectOf(const ToolBar* b) const
{
    for (const Band& band : bands)
        for (const auto& [tb, r] : band.bars)
            if (tb == b)
                return r;
    return gfx::Rectf{};
}

namespace {

// En remsa av tjockleken `t` skuren ur `yta` vid kanten — `yta` krymper.
gfx::Rectf skär(gfx::Rectf& yta, Dock kant, float t)
{
    gfx::Rectf r{};
    switch (kant) {
    case Dock::Left:
        r = {yta.x, yta.y, t, yta.h};
        yta.x += t;
        yta.w -= t;
        break;
    case Dock::Right:
        r = {yta.x + yta.w - t, yta.y, t, yta.h};
        yta.w -= t;
        break;
    case Dock::Top:
        r = {yta.x, yta.y, yta.w, t};
        yta.y += t;
        yta.h -= t;
        break;
    default:   // Bottom
        r = {yta.x, yta.y + yta.h - t, yta.w, t};
        yta.h -= t;
        break;
    }
    yta.w = std::max(0.0f, yta.w);
    yta.h = std::max(0.0f, yta.h);
    return r;
}

// Ett handtags LÄNGD längs kanten. Stående: en kvadrat. Liggande: titeln
// (och ikonen, med listornas sida och luft) med luft på var sida.
float handtagsLängd(const Ctx& c, const DockPanel& p, bool stående)
{
    const auto& m = *c.style;
    if (stående)
        return m.dockHandleH;
    const float text = p.text().empty() ? 0.0f : Draw::measure(c, p.text()).w;
    float ikon = 0.0f;
    if (p.img().valid())
        ikon = m.iconFallback + (text > 0.0f ? m.iconGap : 0.0f);
    return 2.0f * m.dockHandlePadX + ikon + text;
}

// FÖRDELNINGEN längs en remsa: `rum` delas efter vikt; en cell som hamnar
// under sitt minsta eller över sitt största mått LÅSES där, och resten
// fördelas om mellan de olåsta. Varje varv låser den sida som bryter mest
// (summan av underskotten mot summan av överskotten — den vanliga
// vattenfyllningen med gränser), så minst en cell låses per varv och n varv
// räcker. `störst` 0 = ingen gräns; min vinner en krock.
//
// I DOUBLE, med vikterna relativt den största:
// en giltig vikt kan vara 3e38, och `kvar * vikt` i float hade blivit
// oändligt — och summan av två sådana likaså. Svaret är ändligt, aldrig
// negativt, och dess summa går aldrig förbi `rum`.
std::vector<float> fördela(float rum, const std::vector<float>& vikt,
                           const std::vector<float>& minst,
                           const std::vector<float>& störst)
{
    const std::size_t n = vikt.size();
    double            toppVikt = 0.0;
    for (const float v : vikt)
        if (giltigVikt(v))
            toppVikt = std::max(toppVikt, static_cast<double>(v));
    std::vector<double> w(n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
        if (toppVikt > 0.0 && giltigVikt(vikt[i]))
            w[i] = static_cast<double>(vikt[i]) / toppVikt;
    auto golv = [&](std::size_t i) { return static_cast<double>(minst[i]); };
    auto tak  = [&](std::size_t i) {
        return störst[i] > 0.0f ? std::max(static_cast<double>(störst[i]), golv(i))
                                : 0.0;
    };

    const double        helt = std::max(0.0, static_cast<double>(rum));
    std::vector<double> l(n, 0.0);
    std::vector<bool>   låst(n, false);
    double              kvar = helt;
    bool                ryms = false;
    for (std::size_t varv = 0; varv <= n && !ryms; ++varv) {
        double summa = 0.0;
        for (std::size_t i = 0; i < n; ++i)
            if (!låst[i])
                summa += w[i];
        if (summa <= 0.0)
            break;
        double under = 0.0, över = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            if (låst[i])
                continue;
            l[i] = kvar * w[i] / summa;
            if (l[i] < golv(i))
                under += golv(i) - l[i];
            else if (tak(i) > 0.0 && l[i] > tak(i))
                över += l[i] - tak(i);
        }
        if (under <= 0.0 && över <= 0.0) {
            ryms = true;   // alla ryms inom sina gränser
            break;
        }
        const bool låsMinsta = under >= över;
        for (std::size_t i = 0; i < n; ++i) {
            if (låst[i])
                continue;
            if (låsMinsta ? l[i] < golv(i) : (tak(i) > 0.0 && l[i] > tak(i))) {
                l[i]    = låsMinsta ? golv(i) : tak(i);
                låst[i] = true;
                kvar -= l[i];
            }
        }
    }
    if (!ryms) {
        // ALLA LÅSTA. Ett överskott (alla vid sitt tak) fördelas ändå efter
        // vikt — längs remsan finns ingen huvudyta som tar det; ett
        // underskott (minimimåtten ryms inte) klipper cellerna i ordning.
        double summaVikt = 0.0, summaL = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            summaVikt += w[i];
            summaL += l[i];
        }
        if (summaL < helt && summaVikt > 0.0) {
            const double extra = helt - summaL;
            for (std::size_t i = 0; i < n; ++i)
                l[i] += extra * w[i] / summaVikt;
        } else if (summaL > helt) {
            double kvarRum = helt;
            for (std::size_t i = 0; i < n; ++i) {
                l[i] = std::min(l[i], kvarRum);
                kvarRum -= l[i];
            }
        }
    }
    // TILL FLOAT — och avrundningen av varje längd får inte låta cellerna
    // tillsammans gå förbi rummet: överskottet tas från slutet.
    std::vector<float> ut(n, 0.0f);
    float              summa = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        ut[i] = static_cast<float>(std::max(0.0, l[i]));
        summa += ut[i];
    }
    for (std::size_t i = n; i > 0 && summa > rum; --i) {
        const float bort = std::min(ut[i - 1], summa - rum);
        ut[i - 1] -= bort;
        summa -= bort;
    }
    return ut;
}

} // namespace

DockGeometry computeLayout(const DockModel& m, const gfx::Rectf& b,
                           const Ctx& c, const DockLive& live,
                           const BarSize& barSize)
{
    const auto&  st = *c.style;
    DockGeometry g;
    gfx::Rectf   rest = b;

    // DOCKAD I LAYOUTEN — `docksOpen` med förhandsvisningarna inräknade: den
    // dragna panelen läggs ut nålad och utfälld (så blir den vid släppet),
    // snappens infällda läggs inte ut, och en panel vars remsa GLIDER står
    // kvar i den — också när den just fällts in. En sanning för
    // remsornas tjocklek, cellerna och flikraderna nedan.
    auto glider = [&](const DockPanel* p) {
        for (const auto& [q, f] : live.grow)
            if (q == p)
                return true;
        return false;
    };
    auto dockad = [&](const DockPanel* p) {
        if (!p || !p->isVisible())
            return false;
        if (p == live.dragged || glider(p))
            return true;
        return !iListan(live.collapsed, p) && docksOpen(p);
    };
    auto visadFlik = [&](const DockCell& cell) -> DockPanel* {
        if (dockad(cell.active) &&
            std::find(cell.tabs.begin(), cell.tabs.end(), cell.active) !=
                cell.tabs.end())
            return cell.active;
        for (DockPanel* p : cell.tabs)
            if (dockad(p))
                return p;
        return nullptr;
    };
    auto dockadeFlikar = [&](const DockCell& cell) {
        int n = 0;
        for (const DockPanel* p : cell.tabs)
            if (dockad(p))
                ++n;
        return n;
    };
    // Remsans ANDEL av sin fulla tjocklek medan den glider (1 = ingen
    // glidning). Två glidningar i samma remsa: den minsta gäller.
    std::vector<float> remsansAndel(m.strips.size(), 1.0f);
    for (const auto& [p, f] : live.grow) {
        const int si = p ? m.stripIndexOf(*p) : -1;
        if (si >= 0)
            remsansAndel[std::size_t(si)] =
                std::min(remsansAndel[std::size_t(si)], std::clamp(f, 0.0f, 1.0f));
    }

    // 1. BANDEN — verktygsfälten skärs ytterst: Top, Bottom, Left,
    // Right, band 0 ytterst vid varje kant. Ett band utan synligt fält skär
    // inget. Efter varje kants band står zonen där ett NYTT innersta band
    // skapas — `dockDropZone` in i det som är kvar.
    for (int ki = 0; ki < 4; ++ki) {
        const Dock kant    = kBandEdges[ki];
        const bool stående = isStandingEdge(kant);
        for (const DockBand& band : m.bandsAt(kant)) {
            // Fältet, dess kortaste längd och den längd det får — först den
            // egna, sedan krympt bakifrån (nedan).
            struct Mått {
                ToolBar* tb;
                float    min;
                float    längd;
            };
            std::vector<Mått> mått;
            float             tjock = 0.0f;
            for (ToolBar* tb : band.bars) {
                if (!tb->isVisible())
                    continue;
                const BarExtent e = barSize ? barSize(*tb, stående)
                                            : BarExtent{};
                const float     l = std::max(0.0f, stående ? e.size.h : e.size.w);
                mått.push_back({tb, std::clamp(e.minLength, 0.0f, l), l});
                tjock = std::max(tjock, stående ? e.size.w : e.size.h);
            }
            if (mått.empty() || tjock <= 0.0f)
                continue;
            const float t =
                std::min(tjock, std::max(0.0f, stående ? rest.w : rest.h));
            DockGeometry::Band gb;
            gb.edge  = kant;
            gb.index = band.index;
            gb.rect  = skär(rest, kant, t);
            // RYMS FÄLTEN INTE KRYMPER DE BAKIFRÅN: det sista först,
            // ned till sin kortaste längd (`BarExtent::minLength` — med
            // överflödet greppet, luften och överflödsknappen, så knapparna
            // som inte får plats nås i fältets meny), sedan det före. Ett fält
            // utan överflöde krymper inte (dess kortaste längd är hela).
            const float bandLängd = stående ? gb.rect.h : gb.rect.w;
            float       behov     = st.toolGap * float(mått.size() - 1);
            for (const Mått& f : mått)
                behov += f.längd;
            for (auto it = mått.rbegin(); it != mått.rend() && behov > bandLängd;
                 ++it) {
                const float krymp = std::min(behov - bandLängd, it->längd - it->min);
                it->längd -= krymp;
                behov     -= krymp;
            }
            // Fälten packas i ordning — och ingen rektangel når förbi BANDETS
            // SLUT: ett fält som ändå inte
            // ryms kortas där, och de efter det får längden 0. Ritningen,
            // träffprovet och indikatorn läser samma ytor, så ett överfullt
            // stående fält ritas aldrig in i ett band vid nederkanten.
            const float slut = stående ? gb.rect.y + gb.rect.h
                                       : gb.rect.x + gb.rect.w;
            float pos = stående ? gb.rect.y : gb.rect.x;
            for (const Mått& f : mått) {
                ToolBar* const tb    = f.tb;
                const float    start = std::min(pos, slut);
                const float    l = std::clamp(f.längd, 0.0f, slut - start);
                gb.bars.push_back(
                    {tb, stående ? gfx::Rectf{gb.rect.x, start, gb.rect.w, l}
                                 : gfx::Rectf{start, gb.rect.y, l, gb.rect.h}});
                pos = start + l + st.toolGap;
            }
            g.bands.push_back(std::move(gb));
        }
        const float z =
            std::min(st.dockDropZone, std::max(0.0f, stående ? rest.w : rest.h));
        gfx::Rectf kvar = rest;   // en kopia — zonen skär inget ur layouten
        g.newBandZone[ki] = skär(kvar, kant, z);
    }

    // 2. HANDTAGEN — en remsa per kant med infällda eller onålade paneler,
    // utanför alla panelremsor.
    const auto handtag = handlesInOrder(m, &live);
    for (const Dock kant : {Dock::Top, Dock::Bottom, Dock::Left, Dock::Right}) {
        bool finns = false;
        for (const auto& h : handtag)
            finns = finns || h.first == kant;
        if (!finns)
            continue;
        const bool  stående = isStandingEdge(kant);
        const float t = std::min(st.dockHandleH,
                                 std::max(0.0f, stående ? rest.w : rest.h));
        const gfx::Rectf remsa = skär(rest, kant, t);
        g.handleStrips.push_back({kant, remsa});
        float pos = stående ? remsa.y : remsa.x;
        for (const auto& h : handtag) {
            if (h.first != kant)
                continue;
            const float l = handtagsLängd(c, *h.second, stående);
            g.handles.push_back(
                {h.second, kant,
                 stående ? gfx::Rectf{remsa.x, pos, remsa.w, l}
                         : gfx::Rectf{pos, remsa.y, l, remsa.h}});
            pos += l + st.dockHandleGap;
        }
    }

    // 2b. OVERLAYN — vid sin kant i ytan innanför handtagen, OVANPÅ
    // remsorna och huvudytan. Den skär ingenting: allt annat räknas som om den
    // inte fanns, och därför trycker den inte undan huvudytan. Tjockleken är
    // panelens föredragna (AUTO = `dockDefaultSize`) klampad mot dess min/max
    // (min vinner) och mot ytan — gånger andelen medan den glider.
    if (DockPanel* op = live.overlay;
        op && op->isVisible() && m.stripIndexOf(*op) >= 0) {
        const Dock  kant    = m.strips[std::size_t(m.stripIndexOf(*op))].edge;
        const bool  stående = isStandingEdge(kant);
        const float satt    = panelSize(stående ? op->width : op->height);
        const float mx = stående ? op->maximumSize().w : op->maximumSize().h;
        const float mn = stående ? op->minimumSize().w : op->minimumSize().h;
        float       t  = satt > 0.0f ? satt : st.dockDefaultSize;
        if (mx > 0.0f)
            t = std::min(t, mx);
        t = std::max(t, mn);
        t = std::clamp(t, 0.0f, std::max(0.0f, stående ? rest.w : rest.h));
        t *= std::clamp(live.overlayFraction, 0.0f, 1.0f);
        gfx::Rectf yta = rest;   // en kopia — overlayn skär inget
        g.overlay      = {op, kant, skär(yta, kant, t)};
    }

    // 3. PANELREMSORNA i skärordning.
    for (std::size_t si = 0; si < m.strips.size(); ++si) {
        const DockStrip& s = m.strips[si];
        std::vector<std::size_t> synliga;   // celler med en visad flik
        for (std::size_t ci = 0; ci < s.cells.size(); ++ci)
            if (visadFlik(s.cells[ci]))
                synliga.push_back(ci);
        if (synliga.empty())
            continue;   // hela remsan stängd eller infälld — ingen plats

        const bool stående = isStandingEdge(s.edge);
        auto axel = [stående](gfx::Sizef v) { return stående ? v.w : v.h; };
        auto längsAxel = [stående](gfx::Sizef v) { return stående ? v.h : v.w; };

        // TJOCKLEKEN — det största föredragna måttet, klampat.
        float vill = 0.0f, minsta = 0.0f, största = 0.0f, golv = 0.0f;
        for (const DockCell& cell : s.cells)
            for (const DockPanel* p : cell.tabs) {
                if (!dockad(p))
                    continue;
                // Ett mått som inte är ett ändligt tal > 0 är AUTO — samma
                // regel som strängen sparar efter (`panelSize`).
                const float satt = panelSize(stående ? p->width : p->height);
                vill = std::max(vill, satt > 0.0f ? satt : st.dockDefaultSize);
                const float mn = axel(p->minimumSize());
                const float mx = axel(p->maximumSize());
                minsta = std::max(minsta, mn);
                if (mx > 0.0f)
                    största = största > 0.0f ? std::min(största, mx) : mx;
                golv = std::max(golv, mn > 0.0f ? mn : st.dockHeaderH);
            }
        float t = vill;
        if (största > 0.0f)
            t = std::min(t, största);
        t = std::max(t, minsta);   // min vinner en krock
        if (live.strip == static_cast<int>(si))
            t = live.stripThick;

        const float längs = stående ? rest.w : rest.h;
        float       skikt = std::min(st.splitterThick, std::max(0.0f, längs));
        t = std::clamp(t, 0.0f, std::max(0.0f, längs - skikt));
        // GLIDNINGEN: remsan och dess skikt växer med andelen — och
        // huvudytan följer med, bild för bild.
        t *= remsansAndel[si];
        skikt *= remsansAndel[si];

        DockGeometry::Strip gs;
        gs.index    = static_cast<int>(si);
        gs.edge     = s.edge;
        gs.before   = rest;
        gs.floor    = golv;
        gs.maxThick = största;
        gs.rect     = skär(rest, s.edge, t);
        gs.sash     = skär(rest, s.edge, skikt);
        g.strips.push_back(gs);

        // CELLERNA längs remsan — vikterna, klampade.
        const std::size_t k = synliga.size();
        const float L = stående ? gs.rect.h : gs.rect.w;
        const float cskikt =
            k > 1 ? std::min(st.splitterThick,
                             std::max(0.0f, L) / static_cast<float>(k - 1))
                  : 0.0f;
        const float rum =
            std::max(0.0f, L - static_cast<float>(k - 1) * cskikt);
        // Cellskiktets FÖRHANDSVISNING är dragets vikter (`DockLive`): det
        // som visas är layouten ur exakt de vikter släppet skriver.
        const bool förhand = live.cellStrip == static_cast<int>(si) &&
                             live.cellWeights.size() == s.cells.size();
        std::vector<float> vikt(k), minst(k), störst(k), cgolv(k);
        for (std::size_t j = 0; j < k; ++j) {
            const DockCell& cell = s.cells[synliga[j]];
            vikt[j] = förhand ? live.cellWeights[synliga[j]] : cell.weight;
            float mn = 0.0f, mx = 0.0f, gv = 0.0f;
            for (const DockPanel* p : cell.tabs) {
                if (!dockad(p))
                    continue;
                const float pmn = längsAxel(p->minimumSize());
                const float pmx = längsAxel(p->maximumSize());
                mn = std::max(mn, pmn);
                if (pmx > 0.0f)
                    mx = mx > 0.0f ? std::min(mx, pmx) : pmx;
                gv = std::max(gv, pmn > 0.0f ? pmn : st.dockHeaderH);
            }
            minst[j]  = mn;
            störst[j] = mx;
            cgolv[j]  = gv;
        }
        std::vector<float> l = fördela(rum, vikt, minst, störst);

        float       pos  = stående ? gs.rect.y : gs.rect.x;
        const float slut = pos + std::max(0.0f, L);
        for (std::size_t j = 0; j < k; ++j) {
            const DockCell& cell = s.cells[synliga[j]];
            // Den SISTA cellen slutar senast vid remsans ände — positionernas
            // egen avrundning får inte skjuta den förbi.
            if (j + 1 == k)
                l[j] = std::clamp(l[j], 0.0f, std::max(0.0f, slut - pos));
            DockGeometry::Cell gc;
            gc.strip  = static_cast<int>(si);
            gc.cell   = static_cast<int>(synliga[j]);
            gc.rect   = stående ? gfx::Rectf{gs.rect.x, pos, gs.rect.w, l[j]}
                                : gfx::Rectf{pos, gs.rect.y, l[j], gs.rect.h};
            gc.shown  = visadFlik(cell);
            gc.floor  = cgolv[j];
            gc.maxLen = störst[j];
            gc.body   = gc.rect;

            // FLIKRADEN — nederst, bara vid minst två dockade flikar, med
            // lika breda flikar (texten kortas med ellips vid ritningen).
            const int n = dockadeFlikar(cell);
            if (n > 1) {
                const float th = std::min(st.tabHeaderH, gc.rect.h);
                gc.tabRow = {gc.rect.x, gc.rect.y + gc.rect.h - th, gc.rect.w,
                             th};
                gc.body.h = gc.rect.h - th;
                const float bredd =
                    std::max(0.0f, (gc.tabRow.w -
                                    static_cast<float>(n - 1) * st.tabGap) /
                                       static_cast<float>(n));
                float x = gc.tabRow.x;
                for (DockPanel* p : cell.tabs) {
                    if (!dockad(p))
                        continue;
                    gc.tabs.push_back(
                        {p, gfx::Rectf{x, gc.tabRow.y, bredd, gc.tabRow.h}});
                    x += bredd + st.tabGap;
                }
            }
            g.cells.push_back(std::move(gc));
            pos += l[j];
            if (j + 1 < k) {
                DockGeometry::CellSash cs;
                cs.before = static_cast<int>(g.cells.size()) - 1;
                cs.after  = cs.before + 1;
                cs.rect   = stående
                                ? gfx::Rectf{gs.rect.x, pos, gs.rect.w, cskikt}
                                : gfx::Rectf{pos, gs.rect.y, cskikt, gs.rect.h};
                g.cellSashes.push_back(cs);
                pos += cskikt;
            }
        }
    }

    // 4. HUVUDYTAN — det som blir kvar.
    g.center = rest;
    return g;
}

} // namespace glaze::detail
