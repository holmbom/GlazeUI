// dock_string — se dock_string.h. Parsern är ren std; skrivaren och
// tillämpningen är `DockHost`s medlemmar (`layout()`/`setLayout()`) och bor
// här av tooltip.cpp/drag_drop.cpp-skälet: en fil som implementerar en bit av
// en klass och håller den isär från resten.

#include "dock_string.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <system_error>
#include <tuple>

#include <gfx/platform.h>

#include "glaze/widgets/dock_host.h"
#include "glaze/widgets/dock_panel.h"
#include "glaze/widgets/tool_bar.h"

#include "dock_float.h"
#include "dock_model.h"

namespace glaze::detail {

namespace {

std::vector<std::string> dela(const std::string& s, char sep)
{
    std::vector<std::string> ut;
    std::size_t start = 0;
    while (true) {
        const std::size_t p = s.find(sep, start);
        ut.push_back(s.substr(start, p - start));
        if (p == std::string::npos)
            break;
        start = p + 1;
    }
    return ut;
}

// Ett TAL i strängen: `std::from_chars` (lokaloberoende, hela stycket måste
// läsas) och ett ändligt resultat. Ett tal utanför `float` svarar
// `result_out_of_range` och avvisas; NaN och oändligt läses av from_chars
// och avvisas här.
bool tal(const std::string& s, float& ut)
{
    if (s.empty())
        return false;
    float      v = 0.0f;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v,
                                   std::chars_format::general);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size() ||
        !std::isfinite(v))
        return false;
    ut = v;
    return true;
}

// Ett HELTAL >= 0 (bandindex, plats): bara siffror, hela stycket, inom `int`.
bool heltal(const std::string& s, int& ut)
{
    if (s.empty())
        return false;
    int        v = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || v < 0)
        return false;
    ut = v;
    return true;
}

bool flagga(const std::string& v, bool& ut)
{
    if (v == "0") { ut = false; return true; }
    if (v == "1") { ut = true;  return true; }
    return false;
}

// Bara de fyra kanterna — en remsa står alltid vid en kant.
bool kant(const std::string& v, Dock& ut)
{
    if (v == "left")   { ut = Dock::Left;   return true; }
    if (v == "top")    { ut = Dock::Top;    return true; }
    if (v == "right")  { ut = Dock::Right;  return true; }
    if (v == "bottom") { ut = Dock::Bottom; return true; }
    return false;
}

// En flytande panels `dock` — kanterna, och huvudytans två ord: också
// en panel utan kant kan flyta, och den går då tillbaka till huvudytan.
bool dockOrd(const std::string& v, Dock& ut)
{
    if (v == "fill") { ut = Dock::Fill; return true; }
    if (v == "none") { ut = Dock::None; return true; }
    return kant(v, ut);
}

} // namespace

bool savableName(const std::string& n)
{
    return !n.empty() && n.find_first_of(";=,") == std::string::npos;
}

const char* edgeWord(Dock d)
{
    switch (d) {
    case Dock::Left:   return "left";
    case Dock::Top:    return "top";
    case Dock::Right:  return "right";
    case Dock::Bottom: return "bottom";
    case Dock::Fill:   return "fill";
    case Dock::None:   return "none";
    }
    return "none";
}

// KORTASTE formen som läses tillbaka exakt — `std::to_chars` utan precision
// (0.6 skrivs "0.6", inte "0.600000024" som `max_digits10` hade gett), och
// lokaloberoende: appens `setlocale` ändrar aldrig talen.
std::string numberText(float v)
{
    char       buf[32];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    return std::string(buf, r.ptr);
}

bool parseLayout(const std::string& s, LayoutSpec& out, LayoutError& err)
{
    // lang_gate:av skälen nedan är LOGGTEXT — `setLayout` skriver dem i gfx::log
    auto fel = [&err](const char* varför, const std::string& vad) {
        err.why  = varför;
        err.what = vad;
        return false;
    };
    const std::vector<std::string> delar = dela(s, ';');
    if (delar.front() != "v2")
        return fel("okänd version", delar.front());

    LayoutSpec            spec;
    std::set<std::string> namn;
    // Verktygsfältens platser — två fält på samma plats i samma band avvisas.
    std::set<std::tuple<int, int, int>> platser;
    bool                  öppenRemsa = false, öppenCell = false;
    // En öppen remsa måste ha fått en cell, och en öppen cell en panel,
    // innan nästa remsa (eller strängens slut).
    auto stängRemsa = [&](const std::string& vad) {
        if (öppenRemsa && spec.strips.back().cells.empty())
            return fel("en remsa utan cell", vad);
        if (öppenCell && spec.strips.back().cells.back().panels.empty())
            return fel("en cell utan panel", vad);
        return true;
    };

    for (std::size_t i = 1; i < delar.size(); ++i) {
        const std::string& d    = delar[i];
        const std::size_t  lika = d.find('=');
        if (lika == std::string::npos || lika == 0)
            return fel("en post utan tagg", d);
        const std::string              tagg = d.substr(0, lika);
        const std::vector<std::string> fält = dela(d.substr(lika + 1), ',');

        if (tagg == "s") {
            if (!stängRemsa(d))
                return false;
            LayoutStripSpec remsa;
            if (fält.size() != 1)
                return fel("fel antal fält", d);
            if (!kant(fält[0], remsa.edge))
                return fel("okänd kant", fält[0]);
            spec.strips.push_back(std::move(remsa));
            öppenRemsa = true;
            öppenCell  = false;
        } else if (tagg == "c") {
            if (!öppenRemsa)
                return fel("en cell utan remsa", d);
            if (öppenCell && spec.strips.back().cells.back().panels.empty())
                return fel("en cell utan panel", d);
            LayoutCellSpec cell;
            if (fält.size() != 1)
                return fel("fel antal fält", d);
            if (!tal(fält[0], cell.weight) || cell.weight <= 0.0f)
                return fel("vikten är inget positivt tal", d);
            spec.strips.back().cells.push_back(std::move(cell));
            öppenCell = true;
        } else if (tagg == "p") {
            if (!öppenCell)
                return fel("en panel utan cell", d);
            if (fält.size() != 7)
                return fel("fel antal fält", d);
            LayoutPanelSpec p;
            p.name = fält[0];
            if (!savableName(p.name))
                return fel("ett ogiltigt namn", d);
            if (!flagga(fält[1], p.active) || !flagga(fält[2], p.collapsed) ||
                !flagga(fält[3], p.pinned) || !flagga(fält[4], p.closed))
                return fel("en flagga är inte 0/1", d);
            if (!tal(fält[5], p.width) || !tal(fält[6], p.height) ||
                p.width < 0.0f || p.height < 0.0f)
                return fel("ett mått är inget tal >= 0", d);
            if (!namn.insert(p.name).second)
                return fel("namnet står två gånger", p.name);
            // En ONÅLAD panel är alltid infälld — en öppen overlay är ett
            // drifttillstånd och sparas aldrig.
            if (!p.pinned)
                p.collapsed = true;
            spec.strips.back().cells.back().panels.push_back(std::move(p));
        } else if (tagg == "f") {
            // KONTEXTFRI, som `t=`.
            if (fält.size() != 9)
                return fel("fel antal fält", d);
            LayoutFloatSpec f;
            f.name = fält[0];
            if (!savableName(f.name))
                return fel("ett ogiltigt namn", d);
            const bool utanX = fält[1] == "-";
            if (utanX != (fält[2] == "-"))
                return fel("en plats är två tal eller två streck", d);
            f.hasPosition = !utanX;
            if (f.hasPosition && (!tal(fält[1], f.x) || !tal(fält[2], f.y)))
                return fel("en plats är inget tal", d);
            if (!tal(fält[3], f.w) || !tal(fält[4], f.h) || f.w < 0.0f ||
                f.h < 0.0f || !tal(fält[7], f.width) ||
                !tal(fält[8], f.height) || f.width < 0.0f || f.height < 0.0f)
                return fel("ett mått är inget tal >= 0", d);
            if (!flagga(fält[5], f.closed))
                return fel("en flagga är inte 0/1", d);
            if (!dockOrd(fält[6], f.dock))
                return fel("okänd kant", fält[6]);
            if (!namn.insert(f.name).second)
                return fel("namnet står två gånger", f.name);
            spec.floats.push_back(std::move(f));
        } else if (tagg == "t") {
            // KONTEXTFRI: rör varken den öppna remsan eller cellen.
            if (fält.size() != 5)
                return fel("fel antal fält", d);
            LayoutBarSpec t;
            t.name = fält[0];
            if (!savableName(t.name))
                return fel("ett ogiltigt namn", d);
            if (!kant(fält[1], t.edge))
                return fel("okänd kant", fält[1]);
            if (!heltal(fält[2], t.band) || !heltal(fält[3], t.pos))
                return fel("bandet eller platsen är inget heltal >= 0", d);
            if (!flagga(fält[4], t.hidden))
                return fel("en flagga är inte 0/1", d);
            if (!namn.insert(t.name).second)
                return fel("namnet står två gånger", t.name);
            if (!platser.insert({static_cast<int>(t.edge), t.band, t.pos})
                     .second)
                return fel("två verktygsfält på samma plats", d);
            spec.bars.push_back(std::move(t));
        } else if (tagg == "ft") {
            // KONTEXTFRI, som `f=` och `t=` — och ingen
            // plats i ett band, så platsunikheten gäller inte.
            if (fält.size() != 6)
                return fel("fel antal fält", d);
            LayoutFloatBarSpec t;
            t.name = fält[0];
            if (!savableName(t.name))
                return fel("ett ogiltigt namn", d);
            const bool utanX = fält[1] == "-";
            if (utanX != (fält[2] == "-"))
                return fel("en plats är två tal eller två streck", d);
            t.hasPosition = !utanX;
            if (t.hasPosition && (!tal(fält[1], t.x) || !tal(fält[2], t.y)))
                return fel("en plats är inget tal", d);
            if (!flagga(fält[3], t.hidden))
                return fel("en flagga är inte 0/1", d);
            if (!dockOrd(fält[4], t.dock))
                return fel("okänd kant", fält[4]);
            if (!heltal(fält[5], t.band))
                return fel("bandet är inget heltal >= 0", d);
            if (!namn.insert(t.name).second)
                return fel("namnet står två gånger", t.name);
            spec.floatBars.push_back(std::move(t));
        } else {
            return fel("okänd tagg", tagg);
        }
    }
    if (!stängRemsa("(slutet)"))
        return false;
    // lang_gate:på
    out = std::move(spec);
    return true;
}

} // namespace glaze::detail

namespace glaze {

// SKRIVAREN — strukturen i skärordning. Den emitterar BARA det parsern
// godtar: en panel utan sparbart namn och en
// panel vars namn redan skrivits utelämnas (loggat), en cell utan kvar-
// varande panel och en remsa utan kvarvarande cell skrivs inte, och den
// aktiva fliken väljs bland de kvarvarande ÖPPNA (ingen öppen = ingen aktiv;
// parsern tar då den första). Ett ogiltigt panelmått skrivs som AUTO
// (`panelSize`).
std::string DockHost::layout() const
{
    std::string             ut = "v2";
    std::set<std::string>   skrivna;
    const detail::DockModel modell = synced();   // fälten vann (dock_host.h)
    for (const detail::DockStrip& s : modell.strips) {
        std::string remsa;
        for (const detail::DockCell& cell : s.cells) {
            std::vector<const DockPanel*> kvar;
            for (const DockPanel* p : cell.tabs) {
                if (!detail::savableName(p->name)) {
                    gfx::log("DockHost(%s): en panel utan sparbart namn ('%s') "
                             "kan inte sparas i layouten — sätt `name`",
                             name.c_str(), p->name.c_str());
                    continue;
                }
                if (!skrivna.insert(p->name).second) {
                    gfx::log("DockHost(%s): två paneler heter '%s' — layouten "
                             "sparar den första",
                             name.c_str(), p->name.c_str());
                    continue;
                }
                kvar.push_back(p);
            }
            if (kvar.empty())
                continue;
            const DockPanel* aktiv = nullptr;
            for (const DockPanel* p : kvar)
                if (p == cell.active && p->isVisible())
                    aktiv = p;
            for (const DockPanel* p : kvar)
                if (!aktiv && p->isVisible())
                    aktiv = p;
            // Vikten är modellens — ändlig och > 0 per konstruktion (strängen
            // och operationerna normaliserar). Måtten är RÅA fält och går
            // genom `panelSize`: −1, NaN och ∞ skrivs som 0 = AUTO,
            // vilket också är vad layouten gör av dem.
            remsa += ";c=" + detail::numberText(cell.weight);
            for (const DockPanel* p : kvar)
                remsa += ";p=" + p->name + ',' + (p == aktiv ? '1' : '0') +
                         ',' + (p->collapsed_ || !p->pinned_ ? '1' : '0') +
                         ',' + (p->pinned_ ? '1' : '0') + ',' +
                         (p->isVisible() ? '0' : '1') + ',' +
                         detail::numberText(detail::panelSize(p->width)) +
                         ',' + detail::numberText(detail::panelSize(p->height));
        }
        if (!remsa.empty())
            ut += std::string(";s=") + detail::edgeWord(s.edge) + remsa;
    }
    // DE FLYTANDE i NAMNORDNING — de bor i olika träd beroende på var
    // flytten står, och ordningen ska inte bero på det. Fönstrets plats och
    // innehållsyta läses ur det levande (eller stängda) fönstret, annars ur
    // panelens `floatingBounds`; där fönster saknar skärmposition skrivs
    // platsen `-`.
    std::vector<const DockPanel*> flytande;
    for (const Widget* ch : children())
        if (const auto* p = dynamic_cast<const DockPanel*>(ch);
            p && p->isFloating())
            flytande.push_back(p);
    for (const std::unique_ptr<detail::DockFloat>& f : floats_)
        if (floatOf(f->panel) && f->panel->isFloating())
            flytande.push_back(f->panel);
    std::stable_sort(flytande.begin(), flytande.end(),
                     [](const DockPanel* a, const DockPanel* b) {
                         return a->name < b->name;
                     });
    const bool positioner = isFloatDragSupported();
    for (const DockPanel* p : flytande) {
        if (!detail::savableName(p->name)) {
            gfx::log("DockHost(%s): en flytande panel utan sparbart namn "
                     "('%s') kan inte sparas i layouten — sätt `name`",
                     name.c_str(), p->name.c_str());
            continue;
        }
        if (!skrivna.insert(p->name).second) {
            gfx::log("DockHost(%s): två objekt heter '%s' — layouten sparar "
                     "det första",
                     name.c_str(), p->name.c_str());
            continue;
        }
        gfx::Rectf r      = p->floatBounds_;
        bool       harPos = p->floatPosKnown_;
        if (const detail::DockFloat* f = floatOf(p); f && f->win->opened())
            r = f->win->bounds(harPos);
        harPos = harPos && positioner;
        ut += ";f=" + p->name + ',' +
              (harPos ? detail::numberText(r.x) : std::string("-")) + ',' +
              (harPos ? detail::numberText(r.y) : std::string("-")) + ',' +
              detail::numberText(detail::panelSize(r.w)) + ',' +
              detail::numberText(detail::panelSize(r.h)) + ',' +
              (p->isVisible() ? '0' : '1') + ',' + detail::edgeWord(p->dock) +
              ',' + detail::numberText(detail::panelSize(p->width)) + ',' +
              detail::numberText(detail::panelSize(p->height));
    }
    // VERKTYGSFÄLTEN: kant, PACKAT bandindex, plats i bandet och dold.
    // Platsen räknas bland ALLA bandets fält, så ett utelämnat (namnlöst)
    // lämnar en lucka men aldrig två fält på samma plats.
    for (const Dock kant : detail::kBandEdges)
        for (const detail::DockBand& band : modell.bandsAt(kant))
            for (std::size_t i = 0; i < band.bars.size(); ++i) {
                const ToolBar* t = band.bars[i];
                if (!detail::savableName(t->name)) {
                    gfx::log("DockHost(%s): ett verktygsfält utan sparbart namn "
                             "('%s') kan inte sparas i layouten — sätt `name`",
                             name.c_str(), t->name.c_str());
                    continue;
                }
                if (!skrivna.insert(t->name).second) {
                    gfx::log("DockHost(%s): två objekt heter '%s' — layouten "
                             "sparar det första",
                             name.c_str(), t->name.c_str());
                    continue;
                }
                ut += ";t=" + t->name + ',' + detail::edgeWord(kant) + ',' +
                      std::to_string(band.index) + ',' + std::to_string(i) +
                      ',' + (t->isVisible() ? '0' : '1');
            }
    // DE FLYTANDE VERKTYGSFÄLTEN i NAMNORDNING, sist — som
    // de flytande panelerna bor de i olika träd beroende på var flytten står.
    // Platsen läses ur det levande (eller stängda) fönstret, annars ur
    // fältets `floatingPosition`, och `-` där fönster saknar skärmposition.
    // Kanten och bandet är MINNET, och bandet skrivs som det står —
    // det packas först när fältet dockas.
    std::vector<const ToolBar*> flytFält;
    for (const Widget* ch : children())
        if (const auto* t = dynamic_cast<const ToolBar*>(ch);
            t && t->isFloating())
            flytFält.push_back(t);
    for (const std::unique_ptr<detail::DockFloat>& f : floats_)
        if (f->bar && floatOf(f->bar) && f->bar->isFloating())
            flytFält.push_back(f->bar);
    std::stable_sort(flytFält.begin(), flytFält.end(),
                     [](const ToolBar* a, const ToolBar* b) {
                         return a->name < b->name;
                     });
    for (const ToolBar* t : flytFält) {
        if (!detail::savableName(t->name)) {
            gfx::log("DockHost(%s): ett flytande verktygsfält utan sparbart "
                     "namn ('%s') kan inte sparas i layouten — sätt `name`",
                     name.c_str(), t->name.c_str());
            continue;
        }
        if (!skrivna.insert(t->name).second) {
            gfx::log("DockHost(%s): två objekt heter '%s' — layouten sparar "
                     "det första",
                     name.c_str(), t->name.c_str());
            continue;
        }
        gfx::Pointf p      = t->floatPos_;
        bool        harPos = t->floatPosKnown_;
        if (const detail::DockFloat* f = floatOf(t); f && f->win->opened()) {
            const gfx::Rectf r = f->win->bounds(harPos);
            p                  = gfx::Pointf{r.x, r.y};
        }
        harPos = harPos && positioner;
        ut += ";ft=" + t->name + ',' +
              (harPos ? detail::numberText(p.x) : std::string("-")) + ',' +
              (harPos ? detail::numberText(p.y) : std::string("-")) + ',' +
              (t->isVisible() ? '0' : '1') + ',' + detail::edgeWord(t->dock) +
              ',' + std::to_string(t->band());
    }
    return ut;
}

// TILLÄMPNINGEN. Syntaxen är ATOMISK — `parseLayout` först, och ett
// fel lämnar allt orört. Namnen är det inte: ett okänt namn hoppas över post
// för post (loggat), och en cell eller remsa som blir tom tas bort. Paneler
// som strängen inte nämner behåller sina flaggor och läggs som innersta
// remsa vid sin kant i barnordning — som vid en färsk `adopt`, så en panel
// som adopteras EFTER raden hamnar där den hade hamnat om den fanns före.
// VERKTYGSFÄLT som strängen nämner står i (kant, band, plats)-ordning;
// de onämnda behåller kant, band och inbördes ordning och står SIST i sina
// band. FLYTANDE PANELER: en `f=`-post gör panelen flytande med sin
// plats och storlek, och en `p=`-post för en panel som flyter dockar den —
// tillståndet direkt, flytten mellan fönstren i pumpens svans; en panel som
// strängen inte nämner och som flyter fortsätter att flyta. FLYTANDE
// VERKTYGSFÄLT likadant: `ft=` gör fältet flytande med
// sin plats, dold och dockningsminnet, och en `t=` för ett fält som flyter
// dockar det (riktningen följer, `turnIfNeeded`).
bool DockHost::setLayout(const std::string& s)
{
    detail::LayoutSpec  spec;
    detail::LayoutError err;
    if (!detail::parseLayout(s, spec, err)) {
        gfx::log("DockHost(%s): layouten avvisas (%s: '%s') — ingenting "
                 "ändras",
                 name.c_str(), err.why.c_str(), err.what.c_str());
        return false;
    }
    syncModel();

    // När en post inte hittar sin panel är det vanligaste felet att
    // panelerna saknar `name` — det sägs EN gång, med antalet.
    int namnlösa = 0;
    for (Widget* ch : children())
        if ((dynamic_cast<DockPanel*>(ch) || dynamic_cast<ToolBar*>(ch)) &&
            !detail::savableName(ch->name))
            ++namnlösa;
    // Panelerna är barnen OCH de som bor i mina flytande fönster.
    std::vector<DockPanel*> paneler;
    for (Widget* ch : children())
        if (auto* p = dynamic_cast<DockPanel*>(ch))
            paneler.push_back(p);
    for (const std::unique_ptr<detail::DockFloat>& f : floats_)
        if (floatOf(f->panel))
            paneler.push_back(f->panel);
    auto panelen = [&paneler](const std::string& n) -> DockPanel* {
        for (DockPanel* p : paneler)
            if (p->name == n)
                return p;   // två med samma namn: den första
        return nullptr;
    };
    // Verktygsfälten likaså — barnen och de som bor i mina flytande fönster
    //.
    std::vector<ToolBar*> fältLista;
    for (Widget* ch : children())
        if (auto* t = dynamic_cast<ToolBar*>(ch))
            fältLista.push_back(t);
    for (const std::unique_ptr<detail::DockFloat>& f : floats_)
        if (f->bar && floatOf(f->bar))
            fältLista.push_back(f->bar);
    auto fältet = [&fältLista](const std::string& n) -> ToolBar* {
        for (ToolBar* t : fältLista)
            if (t->name == n)
                return t;   // två med samma namn: det första
        return nullptr;
    };

    struct Tillämpad {
        DockPanel*                     panel;
        const detail::LayoutPanelSpec* spec;
    };
    std::vector<Tillämpad> tillämpade;
    bool                   saknas = false;
    detail::DockModel      ny;
    for (const detail::LayoutStripSpec& rs : spec.strips) {
        detail::DockStrip remsa;
        remsa.edge = rs.edge;
        for (const detail::LayoutCellSpec& cs : rs.cells) {
            detail::DockCell cell;
            cell.weight = cs.weight;
            for (const detail::LayoutPanelSpec& ps : cs.panels) {
                DockPanel* p = panelen(ps.name);
                if (!p) {
                    gfx::log("DockHost(%s): ingen panel i värden heter '%s' "
                             "— posten hoppas över",
                             name.c_str(), ps.name.c_str());
                    saknas = true;
                    continue;
                }
                cell.tabs.push_back(p);
                tillämpade.push_back({p, &ps});
                if (ps.active && !cell.active)
                    cell.active = p;   // flera aktiva: den första gäller
            }
            if (cell.tabs.empty())
                continue;
            if (!cell.active)
                cell.active = cell.tabs.front();
            remsa.cells.push_back(std::move(cell));
        }
        if (!remsa.cells.empty())
            ny.strips.push_back(std::move(remsa));
    }
    // DE FLYTANDE: `f=` gör panelen flytande — en panel som också står
    // i en `p=` är en dubblett, och parsern har redan avvisat den.
    struct Flytande {
        DockPanel*                     panel;
        const detail::LayoutFloatSpec* spec;
    };
    std::vector<Flytande> flytande;
    for (const detail::LayoutFloatSpec& fs : spec.floats) {
        DockPanel* p = panelen(fs.name);
        if (!p) {
            gfx::log("DockHost(%s): ingen panel i värden heter '%s' — posten "
                     "hoppas över",
                     name.c_str(), fs.name.c_str());
            saknas = true;
            continue;
        }
        flytande.push_back({p, &fs});
    }
    auto flyter = [&](const DockPanel* p) {
        for (const Flytande& f : flytande)
            if (f.panel == p)
                return true;
        // Onämnd och flytande: fortsätter att flyta.
        return !ny.contains(*p) && p->isFloating();
    };
    for (Widget* ch : children())
        if (auto* p = dynamic_cast<DockPanel*>(ch);
            p && !ny.contains(*p) && !flyter(p) && detail::isDockEdge(p->dock))
            ny.add(*p, p->dock);

    // VERKTYGSFÄLTEN: de nämnda i (kant, band, plats)-ordning, sedan de
    // onämnda i modellens ordning med sina egna nycklar — sist i sina band.
    struct TillämpatFält {
        ToolBar*                     bar;
        const detail::LayoutBarSpec* spec;
    };
    std::vector<TillämpatFält> fälten;
    for (const detail::LayoutBarSpec& ts : spec.bars) {
        ToolBar* t = fältet(ts.name);
        if (!t) {
            gfx::log("DockHost(%s): inget verktygsfält i värden heter '%s' "
                     "— posten hoppas över",
                     name.c_str(), ts.name.c_str());
            saknas = true;
            continue;
        }
        fälten.push_back({t, &ts});
    }
    // DE FLYTANDE FÄLTEN: `ft=` gör fältet flytande — ett
    // fält som också står i en `t=` är en dubblett, och parsern har redan
    // avvisat den.
    struct FlytandeFält {
        ToolBar*                          bar;
        const detail::LayoutFloatBarSpec* spec;
    };
    std::vector<FlytandeFält> flytFält;
    for (const detail::LayoutFloatBarSpec& fs : spec.floatBars) {
        ToolBar* t = fältet(fs.name);
        if (!t) {
            gfx::log("DockHost(%s): inget verktygsfält i värden heter '%s' "
                     "— posten hoppas över",
                     name.c_str(), fs.name.c_str());
            saknas = true;
            continue;
        }
        flytFält.push_back({t, &fs});
    }
    auto fältFlyter = [&flytFält](const ToolBar* t) {
        for (const FlytandeFält& f : flytFält)
            if (f.bar == t)
                return true;
        return false;
    };
    auto kantOrdning = [](Dock d) {
        return static_cast<int>(std::find(std::begin(detail::kBandEdges),
                                          std::end(detail::kBandEdges), d) -
                                std::begin(detail::kBandEdges));
    };
    std::stable_sort(fälten.begin(), fälten.end(),
                     [&](const TillämpatFält& a, const TillämpatFält& b) {
                         return std::make_tuple(kantOrdning(a.spec->edge),
                                                a.spec->band, a.spec->pos) <
                                std::make_tuple(kantOrdning(b.spec->edge),
                                                b.spec->band, b.spec->pos);
                     });
    for (const TillämpatFält& f : fälten)
        ny.bars.push_back(detail::DockBar{f.bar, f.spec->edge, f.spec->band});
    // De ONÄMNDA fältens nycklar i SAMMA packade indexrymd som strängens:
    // strängens band är packade index,
    // och ett namnlöst fält med den glesa nyckeln 5 hade annars hamnat efter
    // strängens band 2 i stället för där det stod — `setLayout(layout())`
    // hade bytt plats på banden.
    detail::DockModel packad = *model_;
    packad.packBarKeys();
    for (const detail::DockBar& d : packad.bars)
        if (!ny.containsBar(*d.bar) && !fältFlyter(d.bar))
            ny.bars.push_back(d);   // ett fält som `ft=` lyfter har inget band

    if (saknas && namnlösa > 0)
        gfx::log("DockHost(%s): %d paneler eller verktygsfält saknar `name` "
                 "och kan varken sparas eller läsas — sätt objektets namn",
                 name.c_str(), namnlösa);

    // FLAGGORNA — krokarna väntar (`inLayout_`): en `show()` hade annars
    // valt fliken i den gamla modellen och flyttat fokus mitt i bytet.
    inLayout_ = true;
    for (const Tillämpad& t : tillämpade) {
        // En FLYTANDE panel i en `p=` dockas — flytten hem postas nedan.
        if (t.panel->floating_) {
            t.panel->floating_ = false;
            t.panel->closeMenu();
        }
        t.panel->collapsed_ = t.spec->collapsed;
        t.panel->pinned_    = t.spec->pinned;
        t.panel->width      = t.spec->width;
        t.panel->height     = t.spec->height;
        if (t.spec->closed)
            t.panel->hide();
        else
            t.panel->show();
        t.panel->invalidate();
    }
    for (const Flytande& f : flytande) {
        DockPanel& p = *f.panel;
        if (!p.floating_)
            p.closeMenu();
        p.floating_      = true;
        p.pinned_        = true;    // en flytande panel är nålad och utfälld
        p.collapsed_     = false;
        p.floatBounds_   = gfx::Rectf{f.spec->x, f.spec->y, f.spec->w,
                                      f.spec->h};
        p.floatPosKnown_ = f.spec->hasPosition;
        p.dock           = f.spec->dock;
        p.width          = f.spec->width;
        p.height         = f.spec->height;
        if (f.spec->closed)
            p.hide();
        else
            p.show();
        p.invalidate();
        // Ett ÖPPET fönster flyttar och ändrar storlek direkt, och strängens
        // plats vinner över en väntande landnings (`floatBoundsChanged`).
        floatBoundsChanged(p);
    }
    for (const TillämpatFält& f : fälten) {
        // Ett FLYTANDE fält i en `t=` dockas — riktningen följer (en gång
        // till i `writeBackBars` om kanten byts), och flytten hem postas
        // nedan.
        if (f.bar->floating_) {
            const bool var   = f.bar->isVertical();
            f.bar->floating_ = false;
            f.bar->turnIfNeeded(var);
        }
        if (f.spec->hidden)
            f.bar->hide();
        else
            f.bar->show();
    }
    for (const FlytandeFält& f : flytFält) {
        ToolBar&   t   = *f.bar;
        const bool var = t.isVertical();
        t.floating_    = true;
        t.dock         = f.spec->dock;
        t.band_        = f.spec->band;
        t.turnIfNeeded(var);   // ett stående fält ligger när det flyter
        t.floatPos_      = gfx::Pointf{f.spec->x, f.spec->y};
        t.floatPosKnown_ = f.spec->hasPosition;
        if (f.spec->hidden)
            t.hide();
        else
            t.show();
        t.invalidate();
        // Ett ÖPPET fönster flyttar direkt — `floatBoundsChanged`, som ovan.
        floatBoundsChanged(t);
    }
    inLayout_ = false;

    *model_ = std::move(ny);
    for (const Flytande& f : flytande)
        forgetPanel(f.panel);   // pekarna till en panel som lämnade layouten
    for (const FlytandeFält& f : flytFält)
        forgetBar(f.bar);       // …och till ett fält som lämnade sitt band
    writeBackEdges();
    writeBackBars();
    // Ett återläst läge GLIDER INTE: en pågående glidning slutar, och
    // det som syns är flaggornas läge — en öppen overlay är stängd, för
    // strängen läser en onålad panel som infälld.
    anims_.clear();
    syncOverlay(nullptr);
    invalidate();
    guardFocus();
    postFloatSync();   // flytten mellan fönstren
    return true;
}

} // namespace glaze
