// DockHost — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER: arbetsytan, verktygsfältens band, handtagen på kanterna, ett skikt
// per remsa och cell (den DELADE ritningen i `../sash.cpp`), flikraderna och
// — ovanpå allt — overlayns hårlinje och släppindikatorn medan en rubrik, en
// flik eller ett grepp dras. Modellen och geometrin bor i `../dock/`
// (`computeLayout` är EN sanning för ritningen, placeringen, träffprovet,
// dragen och glidningen); layoutsträngen i `../dock/dock_string.cpp` och de
// flytande panelernas och verktygsfältens fönster i
// `../dock/dock_float.cpp`. Det LEVANDE DRAGET bor
// här: värden äger det. Inga tal och inga färger i den här filen.

#include "glaze/widgets/dock_host.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include <gfx/platform.h>

#include "glaze/app.h"
#include "glaze/app_window.h"
#include "glaze/draw.h"
#include "glaze/widgets/dock_panel.h"
#include "glaze/widgets/tool_bar.h"

#include "../dock/dock_float.h"
#include "../dock/dock_model.h"
#include "../ease.h"
#include "../menu_query.h"
#include "../sash.h"
#include "../text_field.h"
#include "../window_placement.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("dockhost", DockHost)

using detail::DockGeometry;
using detail::docksOpen;
using detail::isDockEdge;
using detail::isStandingEdge;

namespace {

// Håller widgeten, eller någon i dess delträd, tangentfokus?
bool hållerFokus(const Widget& w)
{
    if (w.focused())
        return true;
    for (const Widget* b : w.children())
        if (hållerFokus(*b))
            return true;
    return false;
}

// Pågår ett TRYCK i widgeten eller dess delträd?
bool tryckI(const Widget& w)
{
    if (w.pressed())
        return true;
    for (const Widget* b : w.children())
        if (tryckI(*b))
            return true;
    return false;
}

// Ligger `w` någonstans under `förfader`?
bool liggerUnder(const Widget& w, const Widget& förfader)
{
    for (const Widget* p = w.parent(); p; p = p->parent())
        if (p == &förfader)
            return true;
    return false;
}

// Titelns FÖRSTA TECKEN — en hel UTF-8-sekvens, aldrig en halv.
std::string förstaTecknet(const std::string& s)
{
    return s.substr(0, textfield::nextBoundary(s, 0));
}

} // namespace

DockHost::DockHost() : model_(std::make_unique<detail::DockModel>())
{
    // LÄMNA-PRÖVNINGEN — timern går bara medan en overlay är öppen.
    hideTimer_.onTick = [this] { hideCheck(); };
}

// I cpp:n av unique_ptr-skälet: modellen, posterna och spöket är
// ofullständiga typer i headern.
//
// RIVNINGEN: de flytande
// objekten — paneler och verktygsfält — lämnar sina
// fönsters kroppar UTAN att deras delträd vandras — ett levande objekts
// LÅNADE barn kan redan ha dött (medlemsordningen i fönsterklassen), så bara
// objektets egen förälderlänk och fönsterpekare nollas (`Widget::severChild`),
// och ett dött objekt jämförs bara. Ett ÄGT objekt förstörs HÄR, med värden
// som ägde det; ett lånat lever vidare utan förälder. Därefter rivs fönstren.
// Ordningen mellan värden och objektet som medlemmar spelar alltså ingen roll.
// Ett BURET drag lämnar aldrig sin capture kvar hos en riven värd —
// inga handlers här, draget bara släpps.
DockHost::~DockHost()
{
    if (carry_.active() && carry_.hostPress && hasPointerCapture())
        releasePointerCapture();
    for (const std::unique_ptr<detail::DockFloat>& f : floats_) {
        const bool              lever = !f->life.expired();
        std::unique_ptr<Widget> ägd   = f->win->body.release(f->item(), lever);
        if (lever) {
            if (f->panel)
                f->panel->floatHost_ = nullptr;
            else
                f->bar->floatHost_ = nullptr;
        }
        ägd.reset();
    }
    floats_.clear();
}

bool DockHost::setProp(const std::string& key, const std::string& value)
{
    if (key == "layout") {
        setLayout(value);   // avvisad sträng loggas av setLayout
        return true;        // nyckeln är känd — värdet kan vara fel
    }
    return Widget::setProp(key, value);   // basen SIST — konventionen
}

// ------------------------------------------------------------ modellen

bool DockHost::owns(const DockPanel* p) const
{
    return p && std::find(children().begin(), children().end(),
                          static_cast<const Widget*>(p)) != children().end();
}

bool DockHost::ownsBar(const ToolBar* t) const
{
    return t && std::find(children().begin(), children().end(),
                          static_cast<const Widget*>(t)) != children().end();
}

bool DockHost::syncModel()
{
    bool ändrad = pruneModel(*model_);
    ändrad      = model_->normalize() || ändrad;
    for (Widget* ch : children()) {
        if (auto* p = dynamic_cast<DockPanel*>(ch)) {
            if (!p->isFloating() && isDockEdge(p->dock) &&
                !model_->contains(*p)) {
                model_->add(*p, p->dock);   // fick en kant efter adopt
                ändrad = true;
            }
        } else if (auto* t = dynamic_cast<ToolBar*>(ch)) {
            // Ett FLYTANDE fält har inget band.
            if (!t->isFloating() && !model_->containsBar(*t)) {
                model_->addBar(*t);   // sist i ordningen — med eller utan kant
                ändrad = true;
            }
        }
    }
    return ändrad;
}

detail::DockModel DockHost::synced() const
{
    detail::DockModel m = *model_;
    pruneModel(m);
    m.normalize();
    for (Widget* ch : children()) {
        if (auto* p = dynamic_cast<DockPanel*>(ch)) {
            if (!p->isFloating() && isDockEdge(p->dock) && !m.contains(*p))
                m.add(*p, p->dock);
        } else if (auto* t = dynamic_cast<ToolBar*>(ch)) {
            if (!t->isFloating() && !m.containsBar(*t))
                m.addBar(*t);
        }
    }
    return m;
}

// `owns`/`ownsBar` och `floatOf` jämför pekaren — `floatOf` derefererar den
// först när postens livstecken lever — så ett dött objekt rörs aldrig. Samma
// invariant för verktygsfälten: bara barn och levande
// hemvändare.
bool DockHost::pruneModel(detail::DockModel& m) const
{
    bool borttagen = false;
    for (const DockPanel* q : m.panels())
        if (!owns(q) && !floatOf(q))
            borttagen = m.remove(q) || borttagen;
    std::vector<const ToolBar*> döda;
    for (const detail::DockBar& d : m.bars)
        if (!ownsBar(d.bar) && !floatOf(d.bar))
            döda.push_back(d.bar);
    for (const ToolBar* t : döda)
        borttagen = m.removeBar(t) || borttagen;
    return borttagen;
}

void DockHost::writeBackEdges()
{
    for (const detail::DockStrip& s : model_->strips)
        for (const detail::DockCell& c : s.cells)
            for (DockPanel* p : c.tabs)
                p->dock = s.edge;
}

// Verktygsfälten: kanten och det PACKADE bandindexet tillbaka till fälten. Ett
// fält som byter riktning byter `width` och `height` — en liggande remsas
// höjd är dess tjocklek, en ståendes bredd — så tjockleken förblir tjocklek
// och en satt längd förblir längd: fältets EN regel (`turnIfNeeded`),
// samma som när det börjar eller slutar flyta.
void DockHost::writeBackBars()
{
    model_->packBarKeys();
    for (const detail::DockBar& d : model_->bars) {
        ToolBar* t = d.bar;
        if (!isDockEdge(d.edge))
            continue;   // huvudytans fält — inget band att skriva tillbaka
        const bool var = t->isVertical();
        t->dock        = d.edge;
        t->band_       = d.key;
        t->turnIfNeeded(var);
    }
}

detail::DockLive DockHost::live(const detail::DockModel& m) const
{
    detail::DockLive l;
    if (liveStripSash()) {
        l.strip      = m.stripIndexOf(*stripDrag_);
        l.stripThick = dragThick_;
    }
    // Cellskiktets vikter gäller bara remsan de räknades för — har den fått
    // eller tappat en cell mitt i draget visas modellens egna.
    if (liveCellSash() && !cellWeights_.empty()) {
        const int si = m.stripIndexOf(*cellDragA_);
        if (si >= 0 && si == m.stripIndexOf(*cellDragB_) &&
            m.strips[std::size_t(si)].cells.size() == cellWeights_.size()) {
            l.cellStrip   = si;
            l.cellWeights = cellWeights_;
        }
    }
    // GLIDNINGARNA — andelarna från senaste steget i `draw()`.
    for (const Anim& a : anims_) {
        if (!owns(a.panel))
            continue;
        if (a.overlay) {
            l.overlay         = a.panel;
            l.overlayFraction = a.now;
        } else {
            l.grow.push_back({a.panel, a.now});
        }
    }
    if (!l.overlay)
        l.overlay = overlayPanel();
    // SNAPPENS INFÄLLNING: remsans utfällda paneler, eller cellens.
    auto infällda = [&](const detail::DockCell& cell) {
        for (DockPanel* q : cell.tabs)
            if (docksOpen(q))
                l.collapsed.push_back(q);
    };
    if (liveStripSash() && stripCollapse_) {
        if (const int si = m.stripIndexOf(*stripDrag_); si >= 0)
            for (const detail::DockCell& cell : m.strips[std::size_t(si)].cells)
                infällda(cell);
    }
    if (liveCellSash() && cellCollapse_ != 0)
        if (const detail::DockCell* cell =
                m.cellOf(cellCollapse_ == 1 ? *cellDragA_ : *cellDragB_))
            infällda(*cell);
    return l;
}

// EN väg till geometrin: verktygsfälten mäts i den riktning bandet de står i
// har — i en simulering kan det vara en annan än fältets egen — och MED
// greppet de har i ett band, också ett flytande fält som simuleras in i ett
// (platshållaren är
// slutgeometrin, och ett flytande fält har inget grepp förrän det dockats).
// Den kortaste längden mäts likadant — bandet krymper fälten
// bakifrån ned till den när de inte ryms.
DockGeometry DockHost::layoutIn(const detail::DockModel& m, const Ctx& c,
                                const gfx::Rectf& b,
                                const detail::DockLive& l) const
{
    return detail::computeLayout(
        m, b, c, l, [&c](const ToolBar& t, bool stående) {
            return detail::BarExtent{t.sizeFor(c, stående, true),
                                     t.minLength(c, stående, true)};
        });
}

DockGeometry DockHost::geometry(const Ctx& c, const gfx::Rectf& b) const
{
    const detail::DockModel m = synced();
    return layoutIn(m, c, b, live(m));
}

// Bar-formen: `c.area` är värdens childArea — dess EGEN rekt, fysisk — och
// barnet får sin del projicerad ur den logiska layouten. En panel i modellen
// som inte visas (stängd, infälld, en inaktiv flik) får en TOM rekt i
// hörnet, och likaså en FLYTANDE panel eller ett flytande verktygsfält som
// väntar på sitt fönster. Den TOMMA rekten ritas inte
// (AppWindow) och klippet tar barnen. Allt annat får huvudytan.
gfx::Rectf DockHost::placeChild(const Widget& child, const Ctx& c) const
{
    const auto* p  = dynamic_cast<const DockPanel*>(&child);
    const auto* tb = dynamic_cast<const ToolBar*>(&child);
    if ((p && p->isFloating()) || (tb && tb->isFloating()))
        return gfx::Rectf{c.area.x, c.area.y, 0.0f, 0.0f};
    const detail::DockModel m = synced();
    const DockGeometry g = layoutIn(m, c, Draw::bounds(c, c.area), live(m));
    if (p && m.contains(*p)) {
        // Cellens yta — eller overlayns, för overlaypanelen.
        const gfx::Rectf y = g.rectOf(p);
        if (y.w <= 0.0f && y.h <= 0.0f)
            return gfx::Rectf{c.area.x, c.area.y, 0.0f, 0.0f};
        return Draw::project(c, c.area, y);
    }
    // Ett verktygsfält i ett band får sin plats där; ett dolt har ingen.
    if (tb && m.barInBand(*tb)) {
        const gfx::Rectf r = g.barRectOf(tb);
        if (r.w <= 0.0f && r.h <= 0.0f)
            return gfx::Rectf{c.area.x, c.area.y, 0.0f, 0.0f};
        return Draw::project(c, c.area, r);
    }
    return Draw::project(c, c.area, g.center);
}

gfx::Sizef DockHost::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default:
        // INGEN autostorlek — värden är en PLACERINGSYTA
        // som Panel och dimensioneras av layouten (oftast `Dock::Fill`).
        // Remsorna och mitten STRÄCKS till ytan, så barnens mått säger
        // ingenting om värdens.
        return gfx::Sizef{width, height};
    }
}

// ------------------------------------------------------------ krokarna

// En ADOPTERAD panel med en kant blir innersta remsa vid den (adopt-regeln)
// — så `.glazeui`s trädbygge är startlayoutens ordning.
void DockHost::childAdded(Widget& child)
{
    if (auto* p = dynamic_cast<DockPanel*>(&child)) {
        if (p->isFloating()) {
            postFloatSync();   // flytande redan vid adoptionen — ut i sitt fönster
        } else if (isDockEdge(p->dock)) {
            model_->add(*p, p->dock);
            // En onålad, utfälld panel öppnas som overlay — adoptionen är den
            // senaste öppningen (fälten är sanningen, också i previewn). En
            // undanträngd overlay kan ha haft fokus.
            if (syncOverlay(p))
                guardFocus();
        }
    } else if (auto* t = dynamic_cast<ToolBar*>(&child)) {
        if (t->isFloating()) {
            postFloatSync();   // flytande redan vid adoptionen
        } else {
            // Sist i ordningen — alltså sist i band `band()` vid sin kant
            // — och med sin adoptionsplats kvar om det saknar kant ännu.
            // Ett fält som kommer HEM ur sitt flytande fönster står redan i
            // modellen och behåller platsen.
            model_->addBar(*t);
        }
    }
    invalidate();
}

// Objektet lämnar modellen, och varje pekare till det glöms — det LEVER här
// (widget.h), men ett ägt barn förstörs strax efteråt.
void DockHost::childRemoved(Widget& child)
{
    if (auto* p = dynamic_cast<DockPanel*>(&child)) {
        model_->remove(*p);
        forgetPanel(p);
    }
    if (auto* t = dynamic_cast<ToolBar*>(&child)) {
        model_->removeBar(*t);
        forgetBar(t);
    }
    invalidate();
}

// `show()` på en stängd panel gör den till den AKTIVA fliken i sin grupp
// ("visa panelen" ska visa just den). Därefter FOKUSVAKTEN:
// fliken som var vald kan just ha blivit inaktiv,
// eller panelen stängts, med fokus kvar i ett fält ingen ser.
void DockHost::childVisibilityChanged(Widget& child)
{
    if (dynamic_cast<ToolBar*>(&child)) {
        invalidate();   // ett dolt fält skär inget — banden räknas om
        if (!inLayout_)
            guardFocus();   // fokus i ett fält som just doldes
        return;
    }
    auto* p = dynamic_cast<DockPanel*>(&child);
    if (!p || inLayout_)
        return;
    if (p->isVisible()) {
        model_->activate(*p);
    } else {
        // EN STÄNGD OVERLAY FÄLLS IN: ett `show()` senare ger handtaget,
        // inte en overlay som lämna-prövningen fäller in direkt.
        if (!p->isPinned() && !p->isCollapsed() && model_->contains(*p))
            p->collapsed_ = true;
        dropAnim(*p);
    }
    syncOverlay(p);
    invalidate();
    guardFocus();
}

bool DockHost::showsPanel(const DockPanel& p) const
{
    if (!p.isVisible())
        return false;
    // I SITT FLYTANDE FÖNSTER syns panelen hela — också på väg tillbaka,
    // tills flytten hem är gjord. På väg UT syns den ingenstans än.
    if (p.parent() != this)
        return true;
    if (p.isFloating())
        return false;
    if (&p == overlayPanel())
        return true;   // den öppna overlayn
    const detail::DockModel m    = synced();
    const detail::DockCell* cell = m.cellOf(p);
    if (!cell)
        return !p.isCollapsed();   // en panel i huvudytan
    return detail::shownTab(*cell) == &p;
}

void DockHost::panelChanged(DockPanel& p)
{
    // En flytande panels fönster bär titeln och gränserna.
    if (detail::DockFloat* f = floatOf(&p))
        f->win->itemChanged();
    invalidate();
    if (!inLayout_)
        guardFocus();
}

// KNAPPNÅLEN OCH INFÄLLNINGEN. Vad som hände avgör glidningen:
//   ut, nålad      remsan glider ut — om den kommer med panelen (den enda
//                  dockade i den); annars byts den utan glidning
//   ut, onålad     overlayn glider fram
//   in, overlay    overlayn glider in mot sitt handtag
//   in, dockad     remsan glider in — om den går med panelen
//   overlay fästs  ingen glidning (den står redan ute)
// Sedan EN overlay åt gången (den här vann — den öppnades senast) och
// fokusvakten.
void DockHost::panelStateChanged(DockPanel& p, bool wasCollapsed,
                                 bool wasPinned)
{
    invalidate();
    if (inLayout_)
        return;
    const bool varOverlay = !wasCollapsed && !wasPinned;
    syncModel();
    if (model_->contains(p) && p.isVisible()) {
        const bool nuC = p.isCollapsed();
        const bool nuP = p.isPinned();
        if (wasCollapsed && !nuC) {
            if (!nuP)
                startAnim(p, true, true);
            else if (stripAloneWith(p))
                startAnim(p, false, true);
        } else if (!wasCollapsed && nuC) {
            if (varOverlay)
                startAnim(p, true, false);
            else if (stripAloneWith(p))
                startAnim(p, false, false);
        } else if (!nuC && varOverlay && nuP) {
            dropAnim(p);
        }
    }
    syncOverlay(&p);
    guardFocus();
}

void DockHost::activatePanel(DockPanel& p)
{
    // EN FLYTANDE PANEL öppnas i sitt fönster, som lyfts, och fokus går
    // in i den — väntar fönstret ännu skapar synken det.
    if (p.isFloating()) {
        p.show();
        if (detail::DockFloat* f = floatOf(&p); f && f->win->isOpen()) {
            f->win->syncVisible();
            if (!f->win->isHidden())
                f->win->window().raise();
            focusFirstStopIn(p);
        }
        return;
    }
    syncModel();
    p.show();                 // en stängd panel öppnas (och blir aktiv)
    p.setCollapsed(false);    // en infälld fälls ut — onålad som overlay
    model_->activate(p);      // och dess flik väljs
    invalidate();
    // EN OVERLAY SOM APPEN ÖPPNAR FÅR FOKUS: annars fäller
    // lämna-prövningen in den så fort den öppnats — pekaren står på menyn som
    // bad om den, och fokus någon annanstans.
    if (&p == overlayPanel() && focusFirstStopIn(p))
        return;
    guardFocus();
}

// ------------------------------------------------------------ overlayn

DockPanel* DockHost::overlayPanel() const
{
    DockPanel* o = overlay_;
    if (!o || !owns(o) || !o->isVisible() || o->isPinned() ||
        o->isCollapsed() || !model_->contains(*o))
        return nullptr;
    return o;
}

bool DockHost::syncOverlay(DockPanel* recent)
{
    pruneModel(*model_);   // kandidaterna läses — ingen död panel bland dem
    std::vector<DockPanel*> kandidater;
    for (DockPanel* q : model_->panels())
        if (q->isVisible() && !q->isPinned() && !q->isCollapsed())
            kandidater.push_back(q);
    auto kandidat = [&](const DockPanel* q) {
        return q && std::find(kandidater.begin(), kandidater.end(), q) !=
                        kandidater.end();
    };
    DockPanel* vinnare = kandidat(recent)     ? recent
                         : kandidat(overlay_) ? overlay_
                         : kandidater.empty() ? nullptr
                                              : kandidater.front();
    // De andra fälls in utan glidning — två overlays hade legat på varandra.
    bool undanträngd = false;
    for (DockPanel* q : kandidater)
        if (q != vinnare) {
            q->collapsed_ = true;
            dropAnim(*q);
            q->invalidate();
            undanträngd = true;
        }
    // En NY overlay: en annan som glider in försvinner direkt — geometrin har
    // plats för en overlay åt gången.
    if (vinnare && vinnare != overlay_)
        anims_.erase(std::remove_if(anims_.begin(), anims_.end(),
                                    [vinnare](const Anim& a) {
                                        return a.overlay && a.panel != vinnare;
                                    }),
                     anims_.end());
    overlay_ = vinnare;
    syncHideTimer();
    return undanträngd;
}

bool DockHost::watching() const
{
    const AppWindow* f = appWindow();
    if (!overlayPanel() || !f || !f->isOpen() || f->isHidden())
        return false;
    for (const Widget* w = this; w; w = w->parent())
        if (!w->isVisible())
            return false;
    return true;
}

// Värden eller fönstret visades eller doldes (också när fönstret ÖPPNAS):
// lämna-prövningen följer, och flytande paneler som väntar på sina
// fönster får dem.
void DockHost::shownChanged()
{
    syncHideTimer();
    if (floatsNeedSync())
        postFloatSync();
}

void DockHost::syncHideTimer()
{
    const AppWindow* f = appWindow();
    if (!watching()) {
        hideTimer_.stop();   // no-op när den inte går
        return;
    }
    // 0 = timerns minsta intervall: ingen händelsekrok säger att pekaren
    // eller fokus lämnat delträdet (och förkastade dem), så prövningen är
    // alltid timerns.
    const int ms = std::max(f->style().dockHideDelayMs, Timer::kMinIntervalMs);
    if (hideTimer_.isRunning() && hideTimer_.interval() == ms)
        return;
    hideTimer_.setInterval(ms);   // en gående timer startas om med perioden
    if (!hideTimer_.isRunning())
        hideTimer_.start();
}

// LÄMNAR = alla fyra är utanför panelen samtidigt: pekaren (över
// overlayn eller dess handtag), tangentfokus, ett tryck och en öppen popup
// eller meny — rubrikmenyn är panelens egen popup.
void DockHost::hideCheck()
{
    DockPanel* o = overlayPanel();
    if (!o || !watching()) {
        hideTimer_.stop();   // värden eller fönstret syns inte — inga fler tick
        return;
    }
    if (pointerOverOverlay(*o) || hållerFokus(*o) || tryckI(*o) ||
        subtreeHasOpenPopup(*o) || menuPresentationHostedIn(*o))
        return;
    collapseOverlay();
}

// Pekaren GEOMETRISKT över overlayn eller dess handtag — `hovered()` säger
// bara vilken aktiverad widget som är överst, och över ett inaktiverat barn
// i overlayn är ingen hovrad. Overlayns HELA yta räknas också medan den
// glider fram: den är redan logiskt öppen.
bool DockHost::pointerOverOverlay(DockPanel& o) const
{
    const AppWindow*                 f = appWindow();
    const std::optional<gfx::Pointf> p =
        f ? f->mousePosition() : std::optional<gfx::Pointf>{};
    if (!p)
        return false;   // pekaren står utanför fönstret
    const Ctx               c = behaviorCtx();
    const gfx::Rectf        r = resolve(c);
    const detail::DockModel m = synced();
    detail::DockLive        l = live(m);
    l.overlay                 = &o;
    l.overlayFraction         = 1.0f;
    const DockGeometry g  = layoutIn(m, c, Draw::bounds(c, r), l);
    const float        lx = Draw::localX(c, r, p->x);
    const float        ly = Draw::localY(c, r, p->y);
    if (g.overlay.panel == &o && g.overlay.rect.contains(lx, ly))
        return true;
    for (const DockGeometry::Handle& h : g.handles)
        if (h.panel == &o && h.rect.contains(lx, ly))
            return true;
    return false;
}

void DockHost::collapseOverlay()
{
    if (DockPanel* o = overlayPanel())
        o->setCollapsed(true);   // glidningen, timern och fokusvakten följer
}

Widget* DockHost::frontChild() const
{
    for (const Anim& a : anims_)
        if (a.overlay && owns(a.panel))
            return a.panel;
    return overlayPanel();
}

// ------------------------------------------------------------ glidningen

// Bara när fönstret är öppet och syns, värden redan har ritats och
// stilen har en längd — annars ändras läget direkt.
bool DockHost::canAnimate() const
{
    const AppWindow* f = appWindow();
    return drawn_ && f && f->isOpen() && !f->isHidden() &&
           f->style().dockAnimMs > 0;
}

void DockHost::startAnim(DockPanel& p, bool overlay, bool out)
{
    const auto gammal =
        std::find_if(anims_.begin(), anims_.end(),
                     [&p](const Anim& a) { return a.panel == &p; });
    Anim a;
    a.panel   = &p;
    a.overlay = overlay;
    a.to      = out ? 1.0f : 0.0f;
    // ETT OMSLAG MITT I FÖRLOPPET glider från där det står, och tiden är
    // resten av vägen.
    a.from = gammal != anims_.end() && gammal->overlay == overlay
                 ? gammal->now
                 : 1.0f - a.to;
    a.now   = a.from;
    a.start = gfx::ticksMs();
    if (gammal != anims_.end())
        anims_.erase(gammal);
    if (!canAnimate())
        return;
    a.ms = static_cast<float>(appWindow()->style().dockAnimMs) *
           std::abs(a.to - a.from);
    if (a.ms <= 0.0f)
        return;
    anims_.push_back(a);
    invalidate();
}

void DockHost::dropAnim(const DockPanel& p)
{
    anims_.erase(std::remove_if(anims_.begin(), anims_.end(),
                                [&p](const Anim& a) { return a.panel == &p; }),
                 anims_.end());
}

// Ett steg per bild, i pump-svansen FÖRE nästa bild (postat av `draw()`) —
// och nästa bild bara så länge något glider. Varje steg ritar om hela värden,
// också det som når slutläget: nästa bilds skada räknas efter steget, så
// ingen del av värden står kvar i ett gammalt läge. Framme = ingen post och
// ingen invalidering, och appen somnar (regel 1).
void DockHost::stepAnims()
{
    if (anims_.empty())
        return;
    const std::uint64_t nu = gfx::ticksMs();
    for (auto it = anims_.begin(); it != anims_.end();) {
        const double t = static_cast<double>(nu - it->start) /
                         static_cast<double>(it->ms);
        if (!owns(it->panel) || t >= 1.0) {
            it = anims_.erase(it);
            continue;
        }
        it->now = it->from +
                  (it->to - it->from) * static_cast<float>(detail::easeInOut(t));
        ++it;
    }
    invalidate();
}

bool DockHost::stripAloneWith(const DockPanel& p) const
{
    const int si = model_->stripIndexOf(p);
    if (si < 0)
        return false;
    for (const detail::DockCell& cell : model_->strips[std::size_t(si)].cells)
        for (const DockPanel* q : cell.tabs)
            if (q != &p && docksOpen(q))
                return false;
    return true;
}

bool DockHost::collapseByUser(const std::vector<DockPanel*>& panels)
{
    for (DockPanel* q : panels) {
        q->collapsed_ = true;
        q->invalidate();
    }
    invalidate();
    guardFocus();
    return userChanged(panels);
}

// ------------------------------------------------------------ flytande

bool DockHost::isFloatDragSupported()
{
    // 78:s skärmlista är tom där fönster saknar skärmposition (Wayland,
    // headless) och före init — samma fråga som `setLocation`s degradering.
    return !gfx::screens().empty();
}

namespace {

// Flyter objektet i posten? Bara medan livstecknet lever.
bool flyter(const detail::DockFloat& f)
{
    return f.panel ? f.panel->isFloating() : f.bar->isFloating();
}

// Ett barn som FLYTER — en panel eller ett verktygsfält —
// och väntar på att flyttas ut i sitt fönster.
bool flytandeBarn(const Widget* w)
{
    if (const auto* p = dynamic_cast<const DockPanel*>(w))
        return p->isFloating();
    if (const auto* t = dynamic_cast<const ToolBar*>(w))
        return t->isFloating();
    return false;
}

} // namespace

detail::DockFloat* DockHost::floatOf(const Widget* w) const
{
    if (!w)
        return nullptr;
    for (const std::unique_ptr<detail::DockFloat>& f : floats_)
        if (f->item() == w && !f->life.expired() &&
            w->parent() == &f->win->body)
            return f.get();
    return nullptr;
}

bool DockHost::mine(const Widget* w) const
{
    return (w && std::find(children().begin(), children().end(), w) !=
                     children().end()) ||
           floatOf(w);
}

void DockHost::forgetPanel(const DockPanel* p)
{
    auto glöm = [p](DockPanel*& q) {
        if (q == p)
            q = nullptr;
    };
    glöm(stripDrag_);
    if (cellDragA_ == p || cellDragB_ == p)
        cellWeights_.clear();
    glöm(cellDragA_);
    glöm(cellDragB_);
    glöm(pressTab_);
    glöm(pressHandle_);
    glöm(dragPanel_);
    glöm(overlay_);
    dropAnim(*p);
    syncHideTimer();
    if (drop_.target == p || dragPanel_ == nullptr)
        drop_ = Drop{};
    if (hover_.panel == p || hover_.other == p)
        hover_ = Hit{};
}

// Verktygsfältets motsvarighet: greppdraget, rubrikdraget
// och deras mål — vid borttagningen och när fältet börjar flyta.
void DockHost::forgetBar(const ToolBar* t)
{
    if (toolDrag_ == t || toolDrop_.bandOf == t || toolDrop_.before == t) {
        toolDrag_ = nullptr;
        toolDrop_ = ToolDrop{};
    }
}

// TILLSTÅNDET FÖRST: en panel som börjar flyta lämnar modellen direkt
// — `layout()` och huvudytan svarar redan — och en som dockas får en
// innersta remsa vid sin `dock` (adopt-regeln; en panel utan kant går till
// huvudytan). Flytten mellan träden postas.
void DockHost::panelFloatingChanged(DockPanel& p)
{
    if (p.isFloating()) {
        model_->remove(p);
        forgetPanel(&p);
        syncOverlay(nullptr);
    } else if (isDockEdge(p.dock)) {
        model_->add(p, p.dock);
        model_->activate(p);
    }
    invalidate();
    guardFocus();   // fokus i en panel på väg ut flyttar till huvudytan
    postFloatSync();
}

// Samma regel för ett VERKTYGSFÄLT: ett som börjar flyta
// lämnar sitt band direkt, och ett som dockas står sist i band `band()` vid
// sin `dock` — adoptionsregeln (platsen i bandet minns inte, som i VS).
void DockHost::barFloatingChanged(ToolBar& t)
{
    if (t.isFloating()) {
        model_->removeBar(t);
        forgetBar(&t);
    } else {
        model_->addBar(t);
    }
    invalidate();
    guardFocus();
    postFloatSync();
}

// Fältets EGNA mått ändrades — ett barn kom, gick, visades eller doldes, ett
// barns eget mått ändrades (`ToolBar::childLayoutChanged` — verktygsknappens
// text och bild), språket byttes, eller titeln: banden räknas om, och ett
// flytande fälts fönster får sin titel och sin fasta storlek i svansen
// (`syncFloats`) — kroken körs mitt i en trädmutation, och ett fönster som
// byter storlek kan rita direkt (D3D11:s WM_SIZE). Synken postas HÄR och inte
// först vid nästa ritning: ett dolt fönster ritas aldrig, och dess storlek
// och gränser hade annars stått kvar.
void DockHost::barChanged(ToolBar& t)
{
    invalidate();
    if (floatOf(&t))
        postFloatSync();
}

// `setFloatingBounds`/`setFloatingPosition` — ett öppet fönster flyttar dit,
// och appens plats vinner över en väntande landnings (lyftet står
// kvar).
void DockHost::floatBoundsChanged(Widget& w)
{
    for (Landing& g : landings_)
        if (g.item() == &w)
            g.at.reset();
    if (detail::DockFloat* f = floatOf(&w))
        f->win->applyBounds();
}

// Fönstrets egen stängning är panelens stängknapp: `onChange` fyras, och
// kroppens krok döljer fönstret. En panel som inte får stängas står kvar.
void DockHost::closeFloatingPanel(DockPanel& p)
{
    if (p.isVisible() && p.isClosable())
        p.userClose();
}

// Det flytande FÄLTETS stängning — rubrikens ✕ och fönstrets egen (Alt+F4):
// fältet DÖLJS, kroppens krok döljer fönstret, och värdens
// `onChange` fyras (ett fält har inget eget). `show()` visar det igen.
bool DockHost::closeFloatingBar(ToolBar& t)
{
    if (!t.isVisible())
        return false;
    t.hide();
    return userChanged({});
}

// FLYTANDE ↔ DOCKAT med musen — greppets och rubrikens dubbelklick. Till
// flytande bara när användaren får (`setFloatable`).
bool DockHost::userFloatBar(ToolBar& t, bool on)
{
    if (t.isFloating() == on || (on && !t.isFloatable()))
        return false;
    t.setFloating(on);
    return userChanged({});
}

// ETT FLYTANDE OBJEKT DÖR (också ett verktygsfält) — i samma ögonblick och inte postat:
// fönstret hade annars kunnat rita, träffa eller ge fokus åt det döda
// objektet innan synken hann fram. Fönstret släpper det (pekaren jämförs) och
// glömmer allt det håller, och döljs; modellen tappar en hemvändare; posten
// och fönstret rivs i svansen, när livstecknet gått ut. Posten vet vilken
// sort objektet är — den här körs från objektets destruktor, och `w` frågas
// aldrig om sin typ. Ett objekt som BÄRS avslutar draget här och nu:
// livstecknet släpps först — under
// destruktorn lever det ännu — så att slutet varken skriver i objektet eller
// ger det ett `onChange`, och värdens capture släpps.
void DockHost::floatingItemDestroyed(Widget& w)
{
    if (carry_.active() && carriedItem() == &w) {
        carry_.life.reset();
        finishCarry(true);
    }
    for (const std::unique_ptr<detail::DockFloat>& f : floats_) {
        if (f->item() != &w)
            continue;
        f->win->itemDestroyed();
        if (f->panel) {
            model_->remove(f->panel);
            forgetPanel(f->panel);
        } else {
            model_->removeBar(f->bar);
            forgetBar(f->bar);
        }
    }
    postFloatSync();
}

// Ett hinder är det `transferChild` hade nekat för: en öppen popup i objektet,
// eller en meny med objektet som värd (panelens rubrikmeny, en verktygsknapps
// rullgardin — två pumpvarv, menu_query.h).
bool DockHost::floatBlocked(const Widget& w) const
{
    return subtreeHasOpenPopup(w) || menuPresentationHostedIn(w);
}

bool DockHost::floatsNeedSync() const
{
    const AppWindow* hem   = appWindow();
    const bool       öppet = hem && hem->isOpen() && !hem->offscreen();
    for (const std::unique_ptr<detail::DockFloat>& f : floats_) {
        if (f->life.expired())
            return true;
        if (f->item()->parent() != &f->win->body || !flyter(*f))
            return true;
        if (öppet && !f->win->isOpen())
            return true;
        // Fältets mått stämmer inte med fönstrets fasta storlek —
        // frågan ställs vid varje ritning av värden och av
        // fönstret. Det är RESERVEN för det som ändrar måtten utan en
        // händelse (ett stilbyte, ett rått layoutfält, en sättare som inte
        // kallar `relayoutSiblings`): barnen och språkbytet postar synken
        // själva (`barChanged`), och ett dolt fönster passas före visningen.
        if (f->bar && f->win->isOpen() && f->win->fitWanted())
            return true;
    }
    if (öppet)
        for (const Widget* ch : children())
            if (flytandeBarn(ch))
                return true;
    return false;
}

// EN synk åt gången, och bara när värden står i ett öppet fönster — då finns
// `App` och en pump som når svansen; utan fönster frågar öppningen igen
// (`shownChanged`). Den postade uppgiften vaktar värdens livstid.
void DockHost::postFloatSync()
{
    const AppWindow* hem = appWindow();
    if (floatSyncPosted_ || !hem || !hem->isOpen())
        return;
    floatSyncPosted_              = true;
    const std::weak_ptr<char> liv = lifeToken();
    glaze::app().post([this, liv] {
        if (liv.expired())
            return;   // värden dog innan synken kom fram
        syncFloats();
    });
}

// SYNKEN — träden och fönstren efter tillståndet, i
// pump-svansen, för paneler och verktygsfält:
//   1. posterna: ett dött objekt lämnar boken (pekaren jämförs bara), ett som
//      någon annan tagit ur kroppen likaså, och ett som inte längre flyter
//      flyttas HEM — fönstrets plats (och panelens storlek) blir dess
//      flytande minne
//   2. mina flytande barn flyttas UT, vart och ett till ett nytt fönster ägt
//      av mitt — bara när mitt fönster är öppet och inte offscreen
//   3. fönstren följer: ett som stängts med mitt öppnas igen, ett fälts
//      fönster får titeln och den fasta storleken, och synligheten följer
//      objektet och ägaren
// En flytt som ett HINDER nekar (en öppen popup eller meny i objektet) VÄNTAR
// utan logg — en väntande operation, inga omförsök: nästa ritning av värden eller det flytande fönstret
// postar synken igen, och det som stängde popupen ritade om objektet.
void DockHost::syncFloats()
{
    floatSyncPosted_     = false;
    AppWindow* const hem = appWindow();
    const bool öppet     = hem && hem->isOpen() && !hem->offscreen();
    bool       ändrad    = false;
    auto       radera    = [this](std::size_t i) {
        floats_.erase(floats_.begin() + static_cast<std::ptrdiff_t>(i));
    };

    for (std::size_t i = 0; i < floats_.size();) {
        detail::DockFloat& f = *floats_[i];
        if (f.life.expired()) {
            // Ett dött objekt på väg hem lämnar modellen — pekaren jämförs.
            if (f.panel)
                model_->remove(f.panel);
            else
                model_->removeBar(f.bar);
            f.win->body.release(f.item(), false);
            radera(i);
            ändrad = true;
            continue;
        }
        Widget& w = *f.item();
        if (w.parent() != &f.win->body) {
            // Någon annan tog objektet ur kroppen — det lämnar boken, och ett
            // på väg hem lämnar modellen (det är varken barn eller flytande
            // längre).
            if (f.panel) {
                if (f.panel->floatHost_ == this)
                    f.panel->floatHost_ = nullptr;
                model_->remove(*f.panel);
                forgetPanel(f.panel);
            } else {
                if (f.bar->floatHost_ == this)
                    f.bar->floatHost_ = nullptr;
                model_->removeBar(*f.bar);
                forgetBar(f.bar);
            }
            radera(i);
            ändrad = true;
            continue;
        }
        if (flyter(f) || floatBlocked(w)) {
            ++i;
            continue;
        }
        if (f.win->opened()) {
            bool             harPos = false;
            const gfx::Rectf r      = f.win->bounds(harPos);
            if (f.panel) {
                f.panel->floatBounds_.w = r.w;
                f.panel->floatBounds_.h = r.h;
                if (harPos) {
                    f.panel->floatBounds_.x = r.x;
                    f.panel->floatBounds_.y = r.y;
                    f.panel->floatPosKnown_ = true;
                }
            } else if (harPos) {
                f.bar->floatPos_      = gfx::Pointf{r.x, r.y};
                f.bar->floatPosKnown_ = true;
            }
        }
        if (!f.win->body.transferChild(w, *this)) {
            ++i;
            continue;
        }
        // Ett objekt som bars i sitt eget fönster (fall B) tappade trycket med
        // flytten, utan krok — det lämnade fönstrets träd — så draget avslutas
        // här och inte först vid en ritning.
        if (carrying(w) && !carry_.hostPress)
            finishCarry(true);
        if (f.panel)
            f.panel->floatHost_ = nullptr;
        else
            f.bar->floatHost_ = nullptr;
        radera(i);
        ändrad = true;
    }

    if (öppet) {
        std::vector<Widget*> ut;
        for (Widget* ch : children())
            if (flytandeBarn(ch) && !floatBlocked(*ch))
                ut.push_back(ch);
        for (Widget* w : ut) {
            auto f = std::make_unique<detail::DockFloat>();
            if (auto* p = dynamic_cast<DockPanel*>(w)) {
                f->panel      = p;
                f->life       = p->lifeToken();
                f->win        = std::make_unique<DockFloatWindow>(*this, *p);
                p->floatHost_ = this;
            } else if (auto* t = dynamic_cast<ToolBar*>(w)) {
                f->bar        = t;
                f->life       = t->lifeToken();
                f->win        = std::make_unique<DockFloatWindow>(*this, *t);
                t->floatHost_ = this;
            }
            if (!transferChild(*w, f->win->body)) {
                if (f->panel)
                    f->panel->floatHost_ = nullptr;
                else
                    f->bar->floatHost_ = nullptr;
                continue;
            }
            // Ett BURET objekt öppnas där bärandet står — eller där ett
            // släpp före svansen lämnade det (landningen) — annars på
            // sin sparade plats.
            DockFloatWindow& win = *f->win;
            floats_.push_back(std::move(f));
            win.open(*hem, carryPlacement(*w));
            landed(*w, win);
            ändrad = true;
        }
        for (const std::unique_ptr<detail::DockFloat>& f : floats_) {
            if (!f->win->isOpen()) {
                Widget& w = *f->item();
                f->win->open(*hem, carryPlacement(w));
                landed(w, *f->win);
                continue;
            }
            if (f->bar)
                f->win->itemChanged();   // titeln och den fasta storleken
            f->win->syncVisible();
        }
    }
    pruneLandings();
    if (ändrad) {
        invalidate();
        guardFocus();
    }
}

// ------------------------------------------------------------ träffprovet

DockHost::Hit DockHost::hitAt(const Ctx& c, float x, float y) const
{
    const gfx::Rectf   r  = resolve(c);
    const DockGeometry g  = geometry(c, Draw::bounds(c, r));
    const float        lx = Draw::localX(c, r, x);
    const float        ly = Draw::localY(c, r, y);
    // Under overlayn är ingen av värdens delar — den ligger ovanpå dem.
    if (g.overlay.panel && g.overlay.rect.contains(lx, ly))
        return Hit{};
    for (const DockGeometry::Handle& h : g.handles)
        if (h.rect.contains(lx, ly))
            return Hit{Part::Handle, h.panel, nullptr};
    for (const DockGeometry::Strip& s : g.strips) {
        if (!s.sash.contains(lx, ly))
            continue;
        // Remsan identifieras av sin första visade panel.
        for (const DockGeometry::Cell& cell : g.cells)
            if (cell.strip == s.index)
                return Hit{Part::StripSash, cell.shown, nullptr};
    }
    for (const DockGeometry::CellSash& cs : g.cellSashes)
        if (cs.rect.contains(lx, ly))
            return Hit{Part::CellSash, g.cells[std::size_t(cs.before)].shown,
                       g.cells[std::size_t(cs.after)].shown};
    for (const DockGeometry::Cell& cell : g.cells)
        for (const auto& [p, tr] : cell.tabs)
            if (tr.contains(lx, ly))
                return Hit{Part::Tab, p, nullptr};
    return Hit{};
}

// Delens yta i geometrin (logiska px) — det en hover-ändring ritar om.
gfx::Rectf DockHost::hitRect(const DockGeometry& g, const Hit& h) const
{
    switch (h.part) {
    case Part::Handle:
        for (const DockGeometry::Handle& hh : g.handles)
            if (hh.panel == h.panel)
                return hh.rect;
        break;
    case Part::Tab:
        for (const DockGeometry::Cell& cell : g.cells)
            for (const auto& [p, tr] : cell.tabs)
                if (p == h.panel)
                    return tr;
        break;
    case Part::StripSash:
        if (const DockGeometry::Cell* cell = g.cellShowing(h.panel))
            for (const DockGeometry::Strip& s : g.strips)
                if (s.index == cell->strip)
                    return s.sash;
        break;
    case Part::CellSash:
        for (const DockGeometry::CellSash& cs : g.cellSashes)
            if (g.cells[std::size_t(cs.before)].shown == h.panel)
                return cs.rect;
        break;
    case Part::None:
        break;
    }
    return gfx::Rectf{};
}

void DockHost::invalidateHit(const Ctx& c, const Hit& h)
{
    if (h.part == Part::None)
        return;
    const gfx::Rectf r = resolve(c);
    const gfx::Rectf y = hitRect(geometry(c, Draw::bounds(c, r)), h);
    if (y.w > 0.0f && y.h > 0.0f)
        invalidateRect(Draw::project(c, r, y));
}

// ------------------------------------------------------------ ritningen

// BILDENS STEG — det värden
// håller i takt med bilden, före `draw()` och ÄVEN när värdens rekt är tom
// (en inaktiv flik ritas inte, men en väntande flytt ska inte vänta på den).
void DockHost::frameStep()
{
    // Ett BURET drag vars tryck försvann utan att en krok hörde det —
    // avslutas först, så platshållaren inte ritas för ett drag som inte lever.
    checkCarry();
    // Fälten ändrades utanför en handler (ett rått `dock`) — modellen följer
    // nu, och nästa bild ritar om hela värden mot den.
    if (syncModel())
        invalidate();
    // EN OVERLAY ÅT GÅNGEN också för en panel som fick sin kant efter
    // adoptionen (syncen ovan) — en undanträngd kan ha haft fokus — och
    // lämna-prövningen går om fönstret öppnats efter overlayn.
    if (syncOverlay(nullptr))
        guardFocus();
    // FLYTANDE PANELER: står träden eller fönstren inte efter
    // tillståndet — en flytt som väntade på att en popup skulle stängas, ett
    // fönster som stängdes med värdens — postas synken.
    if (floatsNeedSync())
        postFloatSync();
    // GLIDNINGEN stegas INTE här: steget görs per skadeområde i en
    // delvis omritning, och efter skadeinsamlingen. Det postar i stället ETT
    // steg till pump-svansen (ritytans tick-mönster), och varje ritning i den
    // här bilden läser samma andelar.
    if (!anims_.empty() && !stepPosted_) {
        stepPosted_                   = true;
        const std::weak_ptr<char> liv = lifeToken();
        glaze::app().post([this, liv] {
            if (liv.expired())
                return;   // värden dog innan steget kom fram
            stepPosted_ = false;
            stepAnims();
        });
    }
}

// De fyra tillstånden (princip 7):
//   vila     arbetsytan `appWorkspace`; handtagsremsan `windowBg` med
//            handtagen som plattor i `panel` (Minimalistic: bara konturen);
//            flikraden `windowBg` med den VISADE fliken som en del av panelen
//            (`panel`, öppen mot den — Minimalistic: radens accentstreck)
//   hover    handtaget och den ovalda fliken får `buttonHover` (Glaze
//            laminerad, med kontur); skiktet tänds (sash.cpp)
//   tryck    handtaget `buttonPress` — också den ÖPPNA overlayns handtag, som
//            står intryckt medan den är ute (▾-knappens regel); skiktet
//            följer handen
//   inaktiv  texten `textDim`, inga tända delar
// FOKUS (värden är ett tabbstopp när den har handtag eller flikgrupper): en
// ring i `accent` innanför tangentbordets valda flik eller handtag. Valet
// följer också musens tryck (`selectKbdItem` i `mouseDown`) — ringen står på
// den flik som just valdes, inte kvar på listans första post.
void DockHost::draw(const Ctx& c)
{
    const gfx::Rectf   b   = c.bounds();
    const DockGeometry g   = geometry(c, b);
    const auto&        m   = *c.style;
    const auto&        col = c.theme->colors;
    const bool         på  = isEnabled();

    // Tangentbordets val — fokusringen ritas runt det.
    KbdItem valt{};
    if (focused() && på) {
        const std::vector<KbdItem> val = kbdItems();
        if (!val.empty())
            valt = val[std::size_t(std::clamp(kbdSel_, 0,
                                              static_cast<int>(val.size()) - 1))];
    }

    auto tänd = [&](Part part, const DockPanel* p) {
        return på && hovered() && !pressed() && hover_.part == part &&
               hover_.panel == p && (!p || p->isEnabled());
    };
    // En INAKTIVERAD panels handtag och flik dämpas som värdens egen text —
    // handtaget är inert (ett klick fäller inte ut den, som fäll-knappen).
    auto textFärg = [&](const DockPanel& p) {
        return på && p.isEnabled() ? col.text : col.textDim;
    };
    const DockPanel* öppen = overlayPanel();
    auto             nere  = [&](const DockPanel* p) {
        return på && ((pressed() && pressHandle_ == p && pressInside_) ||
                      p == öppen);
    };
    auto ring = [&](const gfx::Rectf& r) {
        Draw::frame(c, Draw::inset(r, m.focusWidth / 2.0f), 0.0f,
                    Pen{col.accent, m.focusWidth});
    };
    auto skikten = [&] {
        for (const DockGeometry::Strip& s : g.strips) {
            const DockGeometry::Cell* första = nullptr;
            for (const DockGeometry::Cell& cell : g.cells)
                if (cell.strip == s.index && !första)
                    första = &cell;
            const DockPanel* p = första ? första->shown : nullptr;
            const bool het =
                på && (liveStripSash()
                           ? model_->stripIndexOf(*stripDrag_) == s.index
                           : tänd(Part::StripSash, p));
            sash::draw(c, s.sash, isStandingEdge(s.edge), het);
        }
        for (const DockGeometry::CellSash& cs : g.cellSashes) {
            const DockGeometry::Cell& a = g.cells[std::size_t(cs.before)];
            const DockGeometry::Cell& e = g.cells[std::size_t(cs.after)];
            const bool stående =
                isStandingEdge(model_->strips[std::size_t(a.strip)].edge);
            const bool het =
                på && (liveCellSash()
                           ? cellDragA_ == a.shown && cellDragB_ == e.shown
                           : tänd(Part::CellSash, a.shown));
            // I en STÅENDE remsa ligger cellerna på varandra — skiktet
            // mellan dem är liggande.
            sash::draw(c, cs.rect, !stående, het);
        }
    };
    // Flikens och handtagets INNEHÅLL — ikonen och titeln, kortad med
    // ellips; ett stående handtag har ingen plats för text (ingen roterad
    // text) och visar ikonen eller titelns första tecken.
    auto innehåll = [&](const DockPanel& p, const gfx::Rectf& r, float pad,
                        bool stående, gfx::Color färg) {
        const float ikon     = std::min(m.iconFallback, r.h);
        const float opacitet = på && p.isEnabled() ? 1.0f : 1.0f - m.iconFade;
        if (stående) {
            if (p.img().valid())
                Draw::glyph(c, {r.x + (r.w - ikon) / 2.0f,
                                r.y + (r.h - ikon) / 2.0f, ikon, ikon},
                            p.img(), opacitet);
            else
                Draw::text(c, r, förstaTecknet(p.text()), färg, Align::Center);
            return;
        }
        gfx::Rectf t{r.x + pad, r.y, std::max(0.0f, r.w - 2.0f * pad), r.h};
        if (!p.img().valid()) {
            Draw::textElided(c, t, p.text(), färg, Align::Center);
            return;
        }
        Draw::glyph(c, {t.x, r.y + (r.h - ikon) / 2.0f, ikon, ikon}, p.img(),
                    opacitet);
        t.x += ikon + m.iconGap;
        t.w = std::max(0.0f, t.w - ikon - m.iconGap);
        Draw::textElided(c, t, p.text(), färg, Align::Left);
    };
    auto handtagsremsor = [&] {
        for (const DockGeometry::HandleStrip& hs : g.handleStrips)
            Draw::rect(c, hs.rect, 0.0f, col.windowBg);
    };
    // BANDEN — verktygsfältens remsa längs hela kanten (VS:s bricka):
    // barens yta `windowBg` och hårlinjen mot innehållet i `frame`, så att
    // fälten och luckan efter det sista läser som EN remsa. Fälten ritar sig
    // själva ovanpå (`Bar::draw`), i samma två roller i alla tre familjerna.
    auto bandytor = [&] {
        const Pen pen{col.frame, m.borderWidth};
        for (const DockGeometry::Band& band : g.bands) {
            const gfx::Rectf& r = band.rect;
            Draw::rect(c, r, 0.0f, col.windowBg);
            switch (band.edge) {
            case Dock::Top:
                Draw::line(c, r.x, r.y + r.h, r.x + r.w, r.y + r.h, pen);
                break;
            case Dock::Bottom:
                Draw::line(c, r.x, r.y, r.x + r.w, r.y, pen);
                break;
            case Dock::Left:
                Draw::line(c, r.x + r.w, r.y, r.x + r.w, r.y + r.h, pen);
                break;
            default:   // Right
                Draw::line(c, r.x, r.y, r.x, r.y + r.h, pen);
                break;
            }
        }
    };

    switch (c.ui) {
    // GLAZE — "bestämd kontur": handtagen och den
    // visade fliken bär kontrollkanten `buttonBorder`, och en tänd del är en
    // tänd Glaze-platta — `buttonHover`, laminerad, med kontur.
    case UI::Glaze: {
        Draw::rect(c, b, 0.0f, col.appWorkspace);
        bandytor();
        handtagsremsor();
        const gfx::Color kant = på ? col.buttonBorder : col.border;
        for (const DockGeometry::Handle& h : g.handles) {
            const bool het = tänd(Part::Handle, h.panel);
            Draw::rect(c, h.rect, m.cornerRadius,
                       nere(h.panel) ? col.buttonPress
                       : het         ? col.buttonHover
                                     : col.panel);
            if (het)
                Draw::laminate(c, h.rect, m.cornerRadius, col.sheen,
                               m.sheenLower, col.sheenHigh, m.sheenUpper);
            Draw::frame(c, h.rect, m.cornerRadius, Pen{kant, m.borderWidth});
            innehåll(*h.panel, h.rect, m.dockHandlePadX,
                     isStandingEdge(h.edge), textFärg(*h.panel));
        }
        skikten();
        for (const DockGeometry::Cell& cell : g.cells) {
            if (cell.tabs.empty())
                continue;
            Draw::rect(c, cell.tabRow, 0.0f, col.windowBg);
            Draw::line(c, cell.tabRow.x, cell.tabRow.y,
                       cell.tabRow.x + cell.tabRow.w, cell.tabRow.y,
                       Pen{kant, m.borderWidth});
            for (const auto& [p, tr] : cell.tabs) {
                if (p == cell.shown) {
                    Draw::rect(c, tr, 0.0f, col.panel);
                    Draw::frameGap(c, tr, 0.0f, Pen{kant, m.borderWidth}, tr.x,
                                   tr.w);
                } else if (tänd(Part::Tab, p)) {
                    Draw::rect(c, tr, 0.0f, col.buttonHover);
                    Draw::laminate(c, tr, 0.0f, col.sheen, m.sheenLower,
                                   col.sheenHigh, m.sheenUpper);
                }
                innehåll(*p, tr, m.tabPadX, false, textFärg(*p));
            }
        }
        break;
    }

    // MINIMALISTIC — "Outline": ingen yta i
    // vila. Handtaget är en kontur, och en tänd eller tryckt del fyllning
    // innanför den; den visade fliken bär radens accentstreck (`rowMarkerW`)
    // mot innehållet och texten i `accent` — flikkontrollens och menyradens
    // recept.
    case UI::Minimalistic:
        Draw::rect(c, b, 0.0f, col.appWorkspace);
        bandytor();
        handtagsremsor();
        for (const DockGeometry::Handle& h : g.handles) {
            const bool het = tänd(Part::Handle, h.panel);
            if (het || nere(h.panel))
                Draw::rect(c, h.rect, m.cornerRadius,
                           nere(h.panel) ? col.buttonPress : col.buttonHover,
                           Pen{nere(h.panel) ? col.accent : col.buttonBorder,
                               m.borderWidth});
            else
                Draw::frame(c, h.rect, m.cornerRadius,
                            Pen{col.border, m.borderWidth});
            innehåll(*h.panel, h.rect, m.dockHandlePadX,
                     isStandingEdge(h.edge), textFärg(*h.panel));
        }
        skikten();
        for (const DockGeometry::Cell& cell : g.cells) {
            if (cell.tabs.empty())
                continue;
            Draw::rect(c, cell.tabRow, 0.0f, col.panel);
            Draw::line(c, cell.tabRow.x, cell.tabRow.y,
                       cell.tabRow.x + cell.tabRow.w, cell.tabRow.y,
                       Pen{col.border, m.borderWidth});
            for (const auto& [p, tr] : cell.tabs) {
                const bool vald = p == cell.shown;
                if (!vald && tänd(Part::Tab, p))
                    Draw::rect(c, tr, 0.0f, col.buttonHover);
                if (vald)
                    Draw::rect(c, {tr.x, tr.y, tr.w, m.rowMarkerW}, 0.0f,
                               på ? col.accent : col.textDim);
                innehåll(*p, tr, m.tabPadX, false,
                         !på || !p->isEnabled() ? col.textDim
                         : vald                 ? col.accent
                                                : col.text);
            }
        }
        break;

    // SLATE — YTOR: handtagen är plattor i `panel`
    // med hårlinjen `border`, och den visade fliken är panelens yta som
    // fortsätter ned i flikraden (`frameGap` — öppen mot panelen), som
    // flikkontrollens flik åt andra hållet.
    case UI::Slate:
    default:
        Draw::rect(c, b, 0.0f, col.appWorkspace);
        bandytor();
        handtagsremsor();
        for (const DockGeometry::Handle& h : g.handles) {
            Draw::rect(c, h.rect, m.cornerRadius,
                       nere(h.panel)                   ? col.buttonPress
                       : tänd(Part::Handle, h.panel) ? col.buttonHover
                                                     : col.panel,
                       Pen{col.border, m.borderWidth});
            innehåll(*h.panel, h.rect, m.dockHandlePadX,
                     isStandingEdge(h.edge), textFärg(*h.panel));
        }
        skikten();
        for (const DockGeometry::Cell& cell : g.cells) {
            if (cell.tabs.empty())
                continue;
            Draw::rect(c, cell.tabRow, 0.0f, col.windowBg);
            Draw::line(c, cell.tabRow.x, cell.tabRow.y,
                       cell.tabRow.x + cell.tabRow.w, cell.tabRow.y,
                       Pen{col.border, m.borderWidth});
            for (const auto& [p, tr] : cell.tabs) {
                if (p == cell.shown) {
                    Draw::rect(c, tr, 0.0f, col.panel);
                    Draw::frameGap(c, tr, 0.0f, Pen{col.border, m.borderWidth},
                                   tr.x, tr.w);
                } else if (tänd(Part::Tab, p)) {
                    Draw::rect(c, tr, 0.0f, col.buttonHover);
                }
                innehåll(*p, tr, m.tabPadX, false, textFärg(*p));
            }
        }
        break;
    }

    // Fokusringen runt tangentbordets val.
    if (valt.panel) {
        if (valt.handle) {
            for (const DockGeometry::Handle& h : g.handles)
                if (h.panel == valt.panel)
                    ring(h.rect);
        } else {
            for (const DockGeometry::Cell& cell : g.cells)
                for (const auto& [p, tr] : cell.tabs)
                    if (p == valt.panel)
                        ring(tr);
        }
    }
    drawn_ = true;
}

// PLATSHÅLLAREN — exakt den yta den dragna panelen FÅR: värden
// tillämpar operationen på en kopia av modellen och räknar layouten.
// Genomskinlig `dropHint` så innehållet under syns (användaren siktar mot
// det), och en `accent`-ram i `dropLineW` — drag-och-släppets målram, samma
// betydelse. Bara medan draget LEVER — det burna (`liveCarry`) eller
// platshållarvägens tryck (`liveDrag`): ett avbrutet tryck släcker den av sig
// självt.
void DockHost::drawAfterChildren(const Ctx& c)
{
    const auto& m = *c.style;
    // OVERLAYNS HÅRLINJE mot huvudytan — fönsterkantens roll `frame`,
    // ingen skugga: overlayn är en yta som står mot innehållet.
    const DockGeometry g = geometry(c, c.bounds());
    if (const gfx::Rectf& o = g.overlay.rect;
        g.overlay.panel && o.w > 0.0f && o.h > 0.0f) {
        const Pen pen{c.theme->colors.frame, m.borderWidth};
        switch (g.overlay.edge) {
        case Dock::Left:
            Draw::line(c, o.x + o.w, o.y, o.x + o.w, o.y + o.h, pen);
            break;
        case Dock::Right:
            Draw::line(c, o.x, o.y, o.x, o.y + o.h, pen);
            break;
        case Dock::Top:
            Draw::line(c, o.x, o.y + o.h, o.x + o.w, o.y + o.h, pen);
            break;
        default:   // Bottom
            Draw::line(c, o.x, o.y, o.x + o.w, o.y, pen);
            break;
        }
    }

    // En panel ELLER ett verktygsfält dras — ett tryck i taget.
    gfx::Rectf r{};
    if (liveCarry()) {
        if (carry_.panel && carry_.drop.kind != Drop::Kind::None)
            r = simulated(c, c.bounds(), *carry_.panel, carry_.drop);
        else if (carry_.bar && carry_.toolDrop.kind != ToolDrop::Kind::None)
            r = toolSimulated(c, c.bounds(), *carry_.bar, carry_.toolDrop);
    } else if (liveDrag() && drop_.kind != Drop::Kind::None) {
        r = simulated(c, c.bounds(), *dragPanel_, drop_);
    } else if (liveToolDrag() && toolDrop_.kind != ToolDrop::Kind::None) {
        r = toolSimulated(c, c.bounds(), *toolDrag_, toolDrop_);
    }
    if (r.w <= 0.0f || r.h <= 0.0f)
        return;
    Draw::rect(c, r, 0.0f, c.theme->colors.dropHint);
    Draw::frame(c, Draw::inset(r, m.dropLineW / 2.0f), 0.0f,
                Pen{c.theme->colors.accent, m.dropLineW});
}

// ------------------------------------------------------------ rubrik- och flikdraget

// Ett nytt tryck: ett BURET drag som fortfarande står kvar har tappat sitt
// tryck utan att någon hörde det — det avslutas som avbrutet, aldrig
// bara nollat.
void DockHost::dragBegin()
{
    if (carry_.active())
        finishCarry(true);
    dragPanel_ = nullptr;
    drop_      = Drop{};
    toolDrag_  = nullptr;
    toolDrop_  = ToolDrop{};
}

bool DockHost::liveDrag() const
{
    if (!dragPanel_ || !mine(dragPanel_))
        return false;
    return dragFromTab_ ? pressed() && tabDrag_
                        : dragPanel_->pressed() && dragPanel_->headerDrag_;
}

// MÅLET under en FYSISK punkt. `hc` är VÄRDENS behavior-Ctx. Geometrin
// är den OSIMULERADE — målet väljs i det användaren ser.
DockHost::Drop DockHost::dropAt(const Ctx& hc, const DockPanel& dragged,
                                float x, float y) const
{
    const gfx::Rectf r = resolve(hc);
    if (!r.contains(x, y))
        return Drop{};   // utanför värden — ingen platshållare
    const gfx::Rectf        b  = Draw::bounds(hc, r);
    const detail::DockModel mm = synced();
    const DockGeometry      g  = layoutIn(mm, hc, b, {});
    const float             lx = Draw::localX(hc, r, x);
    const float        ly = Draw::localY(hc, r, y);
    const auto&        m  = *hc.style;

    // ÖVER EN PANEL: en sidozon inom `dockDropZone` från cellens kant (som
    // mest andelen `dockDropShare` av sidan), annars en flik.
    for (const DockGeometry::Cell& cell : g.cells) {
        const gfx::Rectf& k = cell.rect;
        if (!k.contains(lx, ly))
            continue;
        if (cell.shown == &dragged)
            return Drop{};   // över panelen själv — en ryckning dockar inget
        const float zx = std::min(m.dockDropZone, k.w * m.dockDropShare);
        const float zy = std::min(m.dockDropZone, k.h * m.dockDropShare);
        const std::pair<float, Dock> avstånd[] = {
            {zx - (lx - k.x), Dock::Left},
            {zx - (k.x + k.w - lx), Dock::Right},
            {zy - (ly - k.y), Dock::Top},
            {zy - (k.y + k.h - ly), Dock::Bottom},
        };
        // Djupast INNE i sin zon vinner (störst marginal), så hörnet går
        // till den kant pekaren står närmast i förhållande till zonen.
        std::pair<float, Dock> bäst{0.0f, Dock::Fill};
        for (const auto& a : avstånd)
            if (a.first > bäst.first)
                bäst = a;
        return Drop{Drop::Kind::Panel, bäst.second, cell.shown};
    }

    // En panel i HUVUDYTAN (utan kant) som dras över sig själv: ingenting —
    // samma regel som för en dockad, annars hade en ryckning i rubriken nära
    // värdens kant dockat den. Bara en panel som FAKTISKT står i huvudytan —
    // ett barn utanför modellen som inte flyter: en FLYTANDE panel prövar
    // kantzonerna nedan, också en som just
    // lossnat och ännu väntar på sitt fönster.
    if (dragged.parent() == this && !dragged.isFloating() &&
        !mm.contains(dragged) && g.center.contains(lx, ly))
        return Drop{};

    // INTE ÖVER NÅGON PANEL: närmaste kant på värden inom zonen — en ny
    // remsa ytterst där (den vanliga regeln).
    const std::pair<float, Dock> avstånd[] = {
        {lx - b.x, Dock::Left},
        {b.x + b.w - lx, Dock::Right},
        {ly - b.y, Dock::Top},
        {b.y + b.h - ly, Dock::Bottom},
    };
    std::pair<float, Dock> bäst{m.dockDropZone, Dock::None};
    for (const auto& a : avstånd)
        if (a.first < bäst.first)
            bäst = a;
    if (bäst.second == Dock::None)
        return Drop{};
    return Drop{Drop::Kind::Edge, bäst.second, nullptr};
}

// SIMULERINGEN: operationen på en KOPIA av modellen, och den yta panelen får
// i den. Tom = operationen ändrar ingenting (då finns inget mål).
gfx::Rectf DockHost::simulated(const Ctx& c, const gfx::Rectf& b,
                               DockPanel& dragged, const Drop& d) const
{
    detail::DockModel kopia  = synced();
    const bool        ändrad =
        d.kind == Drop::Kind::Edge ? kopia.dockAtEdge(dragged, d.where)
        : d.kind == Drop::Kind::Panel && d.target && owns(d.target)
            ? kopia.dockOn(dragged, *d.target, d.where)
            : false;
    if (!ändrad)
        return gfx::Rectf{};
    // Den dragna panelen läggs ut nålad och utfälld — så blir en overlay
    // som dras dit (`apply`).
    detail::DockLive l;
    l.dragged = &dragged;
    return layoutIn(kopia, c, b, l).rectOf(&dragged);
}

// MÅLET PRÖVAT: ett mål vars simulering inte ändrar något är inget mål —
// samma prövning för platshållarvägen och det burna draget.
DockHost::Drop DockHost::panelTargetAt(const Ctx& hc, DockPanel& p, float x,
                                       float y) const
{
    const Drop d = dropAt(hc, p, x, y);
    if (d.kind == Drop::Kind::None)
        return d;
    const gfx::Rectf r = simulated(hc, Draw::bounds(hc, resolve(hc)), p, d);
    return r.w > 0.0f && r.h > 0.0f ? d : Drop{};
}

// RUBRIKDRAGET (DockPanel anropar): ett drag som redan bärs följer pekaren;
// vid tröskeln lossnar panelen där draget kan bäras (`canCarry`); annars
// platshållarvägen.
void DockHost::panelDragMove(DockPanel& p, float x, float y)
{
    if (carryStep(p, x, y))
        return;
    dragFromTab_ = false;
    panelDragTo(behaviorCtx(), p, x, y);
}

// PLATSHÅLLARVÄGEN: platshållaren i värdens fönster medan en DOCKAD
// panel står kvar. En flytande panel har inget mål här — den bärs alltid där
// fönster har en plats, och utan platser finns ingen väg in med musen.
void DockHost::panelDragTo(const Ctx& hc, DockPanel& p, float x, float y)
{
    dragPanel_   = &p;
    const Drop d = p.parent() == this && !p.isFloating()
                       ? panelTargetAt(hc, p, x, y)
                       : Drop{};
    if (d == drop_)
        return;   // regel 1: samma mål — ingen omritning
    drop_ = d;
    invalidate();   // platshållaren flyttar — värden ritar den
}

// SLÄPPET: ett buret drag slutar i `carryEnd`; platshållarvägens mål räknas
// om vid släppets punkt och tillämpas på den riktiga modellen. Svarar om en
// handler kördes (`Widget::mouseUp`s svar).
bool DockHost::panelDragEnd(DockPanel& p, float x, float y)
{
    if (carrying(p))
        return carryRelease(p, x, y);
    const Drop d = p.parent() == this && !p.isFloating()
                       ? panelTargetAt(behaviorCtx(), p, x, y)
                       : Drop{};
    if (drop_.kind != Drop::Kind::None)
        invalidate();   // platshållaren ska bort (regel 1: bara om den stod)
    dragPanel_ = nullptr;
    drop_      = Drop{};
    if (!isEnabled() || !mine(&p) || d.kind == Drop::Kind::None ||
        !apply(p, d))
        return false;
    return userChanged({&p});
}

// En FLYTANDE panel som dockas: tillståndet först — den står i modellen
// direkt — och flytten hem postas.
bool DockHost::apply(DockPanel& p, const Drop& d)
{
    syncModel();
    const bool flöt   = p.isFloating();
    bool       ändrad = false;
    if (d.kind == Drop::Kind::Edge)
        ändrad = model_->dockAtEdge(p, d.where);
    else if (d.kind == Drop::Kind::Panel && d.target && owns(d.target))
        ändrad = model_->dockOn(p, *d.target, d.where);
    if (!ändrad)
        return false;
    if (flöt) {
        p.floating_ = false;
        p.closeMenu();
        p.invalidate();
        postFloatSync();
    }
    writeBackEdges();
    model_->activate(p);
    // EN OVERLAY SOM DRAS DIT DOCKAS: den släpps nålad och utfälld, som
    // indikatorn visade (`simulated`).
    if (!p.isPinned() || p.isCollapsed()) {
        p.pinned_    = true;
        p.collapsed_ = false;
        dropAnim(p);
        p.invalidate();
        syncOverlay(nullptr);
    }
    invalidate();   // alla remsor kan flytta — värdens yta omsluter dem
    guardFocus();
    return true;
}

// ------------------------------------------------------------ greppdraget

// Platshållarvägens greppdrag. Platshållaren läses med fältets tryck, så
// ett avbrutet tryck släcker den av sig själv.
bool DockHost::liveToolDrag() const
{
    return toolDrag_ && ownsBar(toolDrag_) && toolDrag_->pressed() &&
           toolDrag_->gripDrag_;
}

// MÅLET för ett greppdrag under en FYSISK punkt. `hc` är VÄRDENS
// behavior-Ctx, och geometrin den OSIMULERADE — målet väljs i det användaren
// ser.
DockHost::ToolDrop DockHost::toolDropAt(const Ctx& hc, const ToolBar& dragged,
                                        float x, float y) const
{
    const gfx::Rectf r = resolve(hc);
    if (!r.contains(x, y))
        return ToolDrop{};   // utanför värden — inget mål
    const detail::DockModel mm = synced();
    const DockGeometry      g  = layoutIn(mm, hc, Draw::bounds(hc, r), {});
    const float             lx = Draw::localX(hc, r, x);
    const float             ly = Draw::localY(hc, r, y);

    // ÖVER ETT BAND: MITTPUNKTSREGELN — före det första andra fält vars mitt
    // ligger bortom pekaren, annars sist. Ett band där bara det dragna
    // fältet syns är inget mål (det ligger redan där). En kant fältet inte
    // får dockas vid (`ToolBar::allowsEdge`) är
    // inget mål — varken dess band eller dess nya bandzon, också fältets
    // nuvarande kant.
    for (const DockGeometry::Band& band : g.bands) {
        if (!band.rect.contains(lx, ly) || !dragged.allowsEdge(band.edge))
            continue;
        const bool     stående = isStandingEdge(band.edge);
        const float    p       = stående ? ly : lx;
        const ToolBar* annat   = nullptr;
        const ToolBar* före    = nullptr;
        for (const auto& [tb, tr] : band.bars) {
            if (tb == &dragged)
                continue;
            if (!annat)
                annat = tb;
            const float mitt = stående ? tr.y + tr.h / 2.0f : tr.x + tr.w / 2.0f;
            if (!före && mitt > p)
                före = tb;
        }
        if (!annat)
            return ToolDrop{};
        return ToolDrop{ToolDrop::Kind::Band, band.edge, annat, före};
    }

    // INNANFÖR BANDOMRÅDET vid en kant (inom `dockDropZone`): ett NYTT
    // innersta band där. I ett hörn vinner zonen vars yttre kant — mot
    // bandområdet — pekaren står närmast.
    ToolDrop bäst{};
    float    närmast = 0.0f;
    for (int ki = 0; ki < 4; ++ki) {
        const gfx::Rectf& z = g.newBandZone[ki];
        const Dock        kant = detail::kBandEdges[ki];
        if (z.w <= 0.0f || z.h <= 0.0f || !z.contains(lx, ly) ||
            !dragged.allowsEdge(kant))
            continue;
        const float d    = kant == Dock::Top      ? ly - z.y
                           : kant == Dock::Bottom ? z.y + z.h - ly
                           : kant == Dock::Left   ? lx - z.x
                                                  : z.x + z.w - lx;
        if (bäst.kind == ToolDrop::Kind::None || d < närmast) {
            närmast = d;
            bäst    = ToolDrop{ToolDrop::Kind::NewBand, kant, nullptr, nullptr};
        }
    }
    return bäst;
}

// SIMULERINGEN: flytten på en KOPIA av modellen, och den yta
// fältet får i den — mätt i det nya bandets riktning och med greppet
// (`layoutIn`). Ett FLYTANDE fält sätts in i kopian (`moveBar`): dess
// mått läses ur fälten i den riktning det har nu, så simuleringen rör inte
// originalet och ger ändå slutgeometrin. Tom = ingen ändring.
gfx::Rectf DockHost::toolSimulated(const Ctx& c, const gfx::Rectf& b,
                                   ToolBar& dragged, const ToolDrop& d) const
{
    if (d.kind == ToolDrop::Kind::None)
        return gfx::Rectf{};
    detail::DockModel kopia = synced();
    if (!kopia.moveBar(dragged, d.edge,
                       d.kind == ToolDrop::Kind::Band ? d.bandOf : nullptr,
                       d.before))
        return gfx::Rectf{};
    return layoutIn(kopia, c, b, {}).barRectOf(&dragged);
}

// Målet under en punkt i VÄRDENS fysiska px, PRÖVAT: ett mål vars simulering
// inte ändrar något är inget mål. Gemensam för greppdraget och rubrikdraget.
DockHost::ToolDrop DockHost::toolTargetAt(const Ctx& hc, ToolBar& t, float x,
                                          float y) const
{
    ToolDrop d = toolDropAt(hc, t, x, y);
    if (d.kind != ToolDrop::Kind::None) {
        const gfx::Rectf r =
            toolSimulated(hc, Draw::bounds(hc, resolve(hc)), t, d);
        if (r.w <= 0.0f || r.h <= 0.0f)
            d = ToolDrop{};
    }
    return d;
}

// SLÄPPET PÅ ETT MÅL. Ett FLYTANDE fält dockas (panelvägens form, `apply`):
// tillståndet först — det
// sätts in i modellen på målets plats och slutar flyta, riktningen följer
// (`turnIfNeeded`) — och flytten hem ur dess fönster postas.
bool DockHost::applyTool(ToolBar& t, const ToolDrop& d)
{
    // En otillåten kant vägras också här — försvaret bakom
    // `toolDropAt`, som aldrig ger ett sådant mål.
    if (d.kind == ToolDrop::Kind::None || !mine(&t) || !t.allowsEdge(d.edge) ||
        (d.bandOf && !ownsBar(d.bandOf)) || (d.before && !ownsBar(d.before)))
        return false;
    syncModel();
    if (!model_->moveBar(t, d.edge,
                         d.kind == ToolDrop::Kind::Band ? d.bandOf : nullptr,
                         d.before))
        return false;
    if (t.isFloating()) {
        const bool var = t.isVertical();
        t.floating_    = false;
        t.turnIfNeeded(var);
        t.invalidate();
        postFloatSync();
    }
    writeBackBars();
    invalidate();   // banden och allt innanför dem flyttar
    return true;
}

// GREPPDRAGET (ToolBar anropar): ett drag som redan bärs följer pekaren; vid
// tröskeln lossnar fältet där draget kan bäras (`canCarry`); annars
// platshållarvägen — platshållaren medan fältet står kvar.
void DockHost::toolDragMove(ToolBar& t, float x, float y)
{
    if (carryStep(t, x, y))
        return;
    toolDrag_        = &t;
    const ToolDrop d = toolTargetAt(behaviorCtx(), t, x, y);
    if (d == toolDrop_)
        return;   // regel 1: samma mål — ingen omritning
    toolDrop_ = d;
    invalidate();
}

// SLÄPPET: ett buret drag slutar i `carryEnd`; platshållarvägens mål räknas om
// vid släppets punkt och tillämpas på den riktiga modellen; värdens
// `onChange` fyras (fältet har inget eget).
bool DockHost::toolDragEnd(ToolBar& t, float x, float y)
{
    if (carrying(t))
        return carryRelease(t, x, y);
    const ToolDrop d = toolTargetAt(behaviorCtx(), t, x, y);
    if (toolDrop_.kind != ToolDrop::Kind::None)
        invalidate();   // platshållaren ska bort
    toolDrag_ = nullptr;
    toolDrop_ = ToolDrop{};
    if (d.kind == ToolDrop::Kind::None || !isEnabled() || !applyTool(t, d))
        return false;
    return userChanged({});
}

// ------------------------------------------------------------ det flytande fältets rubrik

// RUBRIKDRAGET i det flytande fältets fönster (fall B) —
// fysiska px i fönstret: fönstret följer pekaren och platshållaren visas i
// värden. Där draget inte kan bäras (fönster utan skärmposition, en
// inaktiverad värd) gör det ingenting — fältet dockas med rubrikens
// dubbelklick.
void DockHost::toolHeaderDrag(ToolBar& t, float x, float y)
{
    carryStep(t, x, y);   // ingen platshållarväg här
}

bool DockHost::toolHeaderDragEnd(ToolBar& t, float x, float y)
{
    return carrying(t) && carryRelease(t, x, y);
}

// ------------------------------------------------------------ det levande draget

// De tre dragens gemensamma steg (rubriken, greppet, det flytande fältets
// rubrik) — källan är objektets fönster just nu.
bool DockHost::carryStep(Widget& item, float x, float y)
{
    AppWindow* const källa = item.appWindow();
    if (carrying(item)) {
        if (källa)
            carryMove(*källa, x, y);
        return true;
    }
    if (källa && canCarry(item)) {
        beginCarry(item, *källa, x, y, false);
        return true;
    }
    return false;
}

bool DockHost::carryRelease(Widget& item, float x, float y)
{
    AppWindow* const källa = item.appWindow();
    return källa ? carryEnd(*källa, x, y) : finishCarry(true);
}

Widget* DockHost::carriedItem() const
{
    return carry_.panel ? static_cast<Widget*>(carry_.panel)
                        : static_cast<Widget*>(carry_.bar);
}

const Widget* DockHost::Landing::item() const
{
    return panel ? static_cast<const Widget*>(panel)
                 : static_cast<const Widget*>(bar);
}

bool DockHost::carrying(const Widget& item) const
{
    return carry_.active() && carriedItem() == &item && !carry_.life.expired();
}

// Kan draget BÄRAS? Värden står i ett öppet fönster som inte är
// offscreen — där finns flytande fönster — och är aktiverad, och fönster har
// en skärmposition. Ett dockat objekt bärs bara när användaren får göra det
// flytande; ett flytande bärs i sitt eget fönster oavsett.
bool DockHost::canCarry(const Widget& item) const
{
    const AppWindow* hem = appWindow();
    if (!isFloatDragSupported() || !isEnabled() || !hem || !hem->isOpen() ||
        hem->offscreen())
        return false;
    if (const auto* p = dynamic_cast<const DockPanel*>(&item))
        return p->isFloating() || p->isFloatable();
    if (const auto* t = dynamic_cast<const ToolBar*>(&item))
        return t->isFloating() || t->isFloatable();
    return false;
}

// LOSSNINGEN. GREPPET räknas i källans fysiska px — trycket mot
// objektets hörn, klampat in i fönstrets blivande yta — och görs om till
// placeringsenheter med källans `clientToScreen` (`toClientOf`s räkning: k ur
// (0,0) och (1,1)). En flik tog i fliken under panelen, och ett dockat fält
// får ett fönster med rubriken överst: där håller handen rubriken, mitt på
// dess höjd. Platsen sparas direkt i objektets minne, så ett släpp före
// svansen öppnar fönstret där. FALL A tar capture och gör objektet flytande
// utan `onChange` — fönstret skapas i svansen på bärandets plats.
void DockHost::beginCarry(Widget& item, AppWindow& källa, float x, float y,
                          bool frånFlik)
{
    auto* const p = dynamic_cast<DockPanel*>(&item);
    auto* const t = dynamic_cast<ToolBar*>(&item);
    if (!p && !t)
        return;
    if (carry_.active())
        finishCarry(true);   // ett äldre drag som aldrig fick sitt släpp

    const Ctx        oc   = item.behaviorCtx();
    const gfx::Rectf r    = item.resolve(oc);
    const float      s    = oc.scale;
    const auto&      st   = *oc.style;
    const float      mitt = st.dockHeaderH * s / 2.0f;
    const bool       flöt = p ? p->isFloating() : t->isFloating();
    gfx::Pointf      grepp{};
    gfx::Sizef       yta{};      // fönstrets innehållsyta, källans fysiska px
    gfx::Sizef       logisk{};   // panelens flytande innehållsyta, logiska px
    if (p) {
        // Panelens fönster: den förra flytande storleken, annars den dockade;
        // ett flytande fönster som det står.
        const bool tidigare = !flöt && p->floatBounds_.w > 0.0f &&
                              p->floatBounds_.h > 0.0f;
        logisk = tidigare ? gfx::Sizef{p->floatBounds_.w, p->floatBounds_.h}
                          : gfx::Sizef{r.w / s, r.h / s};
        yta    = gfx::Sizef{logisk.w * s, logisk.h * s};
        const gfx::Pointf tryck = frånFlik
                                      ? gfx::Pointf{pressX_, pressY_}
                                      : gfx::Pointf{p->pressX_, p->pressY_};
        grepp = gfx::Pointf{tryck.x - r.x, frånFlik ? mitt : tryck.y - r.y};
    } else if (flöt) {
        // Det flytande fältets rubrik — fönstrets klienthörn är rubrikens.
        if (const detail::DockFloat* f = floatOf(t))
            grepp = f->win->body.header().pressPoint();
        const gfx::Rectf ca = källa.window().contentArea();
        yta                 = gfx::Sizef{ca.w, ca.h};
    } else {
        // Ett dockat fält: längs fältet där handen tog, på rubrikens höjd i
        // det liggande fönstret (`DockFloatWindow::fitToolBar`s mått).
        const gfx::Sizef m = t->sizeFor(oc, false, false);
        yta   = gfx::Sizef{std::max(m.w, st.dockHeaderPadX + st.dockHeaderH) * s,
                           (st.dockHeaderH + m.h) * s};
        grepp = gfx::Pointf{t->isVertical() ? t->pressY_ - r.y
                                            : t->pressX_ - r.x,
                            mitt};
    }
    grepp.x = std::clamp(grepp.x, 0.0f, std::max(0.0f, yta.w));
    grepp.y = std::clamp(grepp.y, 0.0f, std::max(0.0f, yta.h));
    const gfx::Pointf o  = källa.window().clientToScreen(0.0f, 0.0f);
    const gfx::Pointf e  = källa.window().clientToScreen(1.0f, 1.0f);
    const gfx::Pointf ps = källa.window().clientToScreen(x, y);

    carry_        = Carry{};
    carry_.panel  = p;
    carry_.bar    = t;
    carry_.life   = p ? p->lifeToken() : t->lifeToken();
    carry_.serial = ++carrySerial_;
    carry_.grab   = gfx::Pointf{grepp.x * (e.x - o.x), grepp.y * (e.y - o.y)};
    carry_.at     = gfx::Pointf{ps.x - carry_.grab.x, ps.y - carry_.grab.y};

    const gfx::Pointf l =
        detail::toLogical(carry_.at, detail::primaryScale(gfx::screens()));
    if (p) {
        p->floatBounds_   = gfx::Rectf{l.x, l.y, logisk.w, logisk.h};
        p->floatPosKnown_ = true;
    } else {
        t->floatPos_      = l;
        t->floatPosKnown_ = true;
    }

    if (!flöt) {
        // FALL A: värden håller trycket — rörelserna och släppet kommer hit
        // också när objektet bytt fönster — och objektets egen dragbokföring
        // släpps, så att dess släpp (om det kommer före flytten) inte avslutar
        // något.
        setPointerCapture();
        carry_.hostPress = true;
        if (p) {
            p->headerDrag_ = false;
            p->pressPart_  = DockPanel::Part::None;
        } else {
            t->gripPress_ = false;
            t->gripDrag_  = false;
        }
        if (frånFlik) {
            pressTab_ = nullptr;
            tabDrag_  = false;
        }
        // Tillståndet: flytande, `setFloating`s väg — utan `onChange`.
        if (p)
            p->setFloating(true);
        else
            t->setFloating(true);
    } else if (p) {
        // FALL B för en panel: trycket ligger i dess eget fönster, och panelen
        // håller explicit capture där — fönstrets avbrott når då värden
        // (`itemPressLost`) också när inget ritas.
        p->setPointerCapture();
    } else if (!takeOverToolPress(*t, källa)) {
        return;   // draget slutade under övertagandet — eller värden dog
    }
    carryMove(källa, x, y);
}

// FALL B för ett verktygsfält: dess fönster aktiveras aldrig, så värdens
// fönster tar över trycket EN gång och draget går som A. Svarar
// den false fortsätter draget i fältets fönster, där rubriken då håller
// explicit capture. ÖVERTAGANDET KÖR DE ANDRA FÖNSTRENS
// AVBROTTSKROKAR — drag-and-drop-avbrottets
// handlers är användarkod och får riva värden, källan eller draget — så
// allt prövas igen efteråt, också när svaret blev false: värden (vakten),
// källan och fältets plats i den (fältets vakt — källan är fältets fönster)
// och dragets identitet. Svarar om draget fortsätter.
bool DockHost::takeOverToolPress(ToolBar& t, AppWindow& källa)
{
    const HandlerGuard  värd(*this);
    const HandlerGuard  fält(t);
    const std::uint64_t id = carry_.serial;
    // Källfönstret släpper rubrikens tryck under övertagandet, och rubriken
    // får `mouseCancel` — det avbrottet ÄR övertagandet.
    carry_.takingOver = true;
    const bool tog    = takePointerCapture();
    if (!värd.ok())
        return false;   // värden dog eller flyttade — dess bok rörs inte
    if (carry_.serial == id)
        carry_.takingOver = false;
    const bool samma = carry_.serial == id && fält.ok() && mine(&t) &&
                       t.appWindow() == &källa;
    if (tog && samma)
        carry_.hostPress = true;
    else if (tog && hasPointerCapture())
        releasePointerCapture();   // inget drag kvar att bära åt
    if (carry_.serial != id)
        return false;   // draget slutade under krokarna — slutet är skrivet
    if (!samma || !liveCarry()) {
        finishCarry(true);   // källan eller trycket försvann
        return false;
    }
    if (!tog)
        if (detail::DockFloat* f = floatOf(&t))
            f->win->body.header().holdCarry();
    return true;
}

// EN RÖRELSE: fönstrets klienthörn är punkten minus greppet, i
// placeringsrymden — punkten räknad ur KÄLLANS `clientToScreen` (ingen
// global pekarfråga). Målet räknas i värdens fönster.
void DockHost::carryMove(AppWindow& källa, float x, float y)
{
    if (!carry_.active())
        return;
    if (carry_.life.expired() || !mine(carriedItem())) {
        finishCarry(true);   // objektet dog eller togs bort mitt i draget
        return;
    }
    const gfx::Pointf ps = källa.window().clientToScreen(x, y);
    carry_.at = gfx::Pointf{ps.x - carry_.grab.x, ps.y - carry_.grab.y};
    const std::uint64_t id = carry_.serial;
    if (detail::DockFloat* f = floatOf(carriedItem()); f && f->win->isOpen()) {
        f->win->window().setPosition(
            static_cast<int>(std::lround(carry_.at.x)),
            static_cast<int>(std::lround(carry_.at.y)));
        // ETT BURET FÄLT LIGGER ÖVERST — vid
        // lossningen (det första anropet kommer ur `beginCarry`) och under hela
        // flytten: dess fönster aktiveras aldrig, så en flytande panel som
        // klickats senast låg annars över det. `raise()` på ett noActivate-
        // fönster rör bara Z-ordningen (fasaden) och tar inte fokus från
        // fönstret som håller trycket. En PANELS fönster lyfts inte här — dess
        // `raise()` aktiverar, och det hade avbrutit fall A — utan vid släppet.
        if (carry_.bar)
            f->win->window().raise();
    }
    // EN FLYTT KAN RITA SYNKRONT (en skärm med annan DPI — D3D11:s WM_SIZE),
    // och ritningen frågar `checkCarry`:
    // draget prövas igen innan något av det läses.
    if (carry_.serial != id)
        return;   // slutet är redan skrivet
    if (carry_.life.expired() || !mine(carriedItem())) {
        finishCarry(true);
        return;
    }
    // MÅLET i värdens fönster: punkten direkt när källan är det, annars genom
    // skärmen (`toClientOf`).
    AppWindow* const hem = appWindow();
    if (!hem || !hem->isOpen())
        return;
    const gfx::Pointf hp = &källa == hem
                               ? gfx::Pointf{x, y}
                               : detail::toClientOf(hem->window(), ps);
    const Ctx hc = behaviorCtx();
    if (carry_.panel) {
        const Drop d = panelTargetAt(hc, *carry_.panel, hp.x, hp.y);
        if (d == carry_.drop)
            return;   // regel 1: samma mål — ingen omritning
        carry_.drop = d;
    } else {
        const ToolDrop d = toolTargetAt(hc, *carry_.bar, hp.x, hp.y);
        if (d == carry_.toolDrop)
            return;
        carry_.toolDrop = d;
    }
    invalidate();   // platshållaren flyttar — värden ritar den
}

// SLÄPPET: målet räknas om vid släppets punkt. På ett mål dockas objektet —
// tillståndet först, flytten hem postas (`apply`/`applyTool`);
// annars förblir det flytande där det står, och dess fönster lyfts — en
// panels aktiveras, ett fälts lyfts bara i Z-ordningen. Sist
// slutet: EN `onChange` per drag.
bool DockHost::carryEnd(AppWindow& källa, float x, float y)
{
    if (!carry_.active())
        return false;
    // SLÄPPET PÅGÅR: objektets tryck och dragflagga är redan
    // släckta, och en ritning mitt i det får inte avsluta draget som avbrutet.
    carry_.releasing = true;
    carryMove(källa, x, y);
    if (!carry_.active())
        return false;   // objektet försvann — slutet har postat sitt besked
    bool dockad = false;
    if (isEnabled()) {
        if (carry_.panel && carry_.drop.kind != Drop::Kind::None)
            dockad = apply(*carry_.panel, carry_.drop);
        else if (carry_.bar && carry_.toolDrop.kind != ToolDrop::Kind::None)
            dockad = applyTool(*carry_.bar, carry_.toolDrop);
    }
    if (!dockad)
        if (detail::DockFloat* f = floatOf(carriedItem());
            f && f->win->isOpen() && !f->win->isHidden())
            f->win->window().raise();
    return finishCarry(false);
}

bool DockHost::liveCarry() const
{
    if (!carry_.active() || carry_.life.expired() || !mine(carriedItem()))
        return false;
    if (carry_.releasing)
        return true;   // släppet avslutar draget självt
    if (carry_.hostPress)
        return hasPointerCapture();
    if (carry_.panel)
        return carry_.panel->pressed() && carry_.panel->headerDrag_;
    const detail::DockFloat* f = floatOf(carry_.bar);
    return f && f->win->body.header().dragging();
}

void DockHost::checkCarry()
{
    if (carry_.active() && !liveCarry())
        finishCarry(true);
}

bool DockHost::finishCarry(bool avbrutet)
{
    if (!carry_.active())
        return false;
    const Carry c = carry_;   // dragets identitet, före städningen
    carry_        = Carry{};
    if (c.hostPress) {
        if (hasPointerCapture())
            releasePointerCapture();
    } else if (c.panel) {
        // FALL B: objektets explicita capture i dess fönster.
        if (!c.life.expired() && c.panel->hasPointerCapture())
            c.panel->releasePointerCapture();
    } else {
        // Fältets rubrik hör till fönstret och lever med det, också när
        // fältet dött — posten letas med pekaren, som bara jämförs.
        for (const std::unique_ptr<detail::DockFloat>& f : floats_)
            if (f->bar == c.bar)
                f->win->body.header().releaseCarry();
    }
    if (c.drop.kind != Drop::Kind::None ||
        c.toolDrop.kind != ToolDrop::Kind::None)
        invalidate();   // platshållaren ska bort
    rememberCarryPlace(c, !avbrutet);
    DockPanel* const p = c.life.expired() ? nullptr : c.panel;
    if (!avbrutet)
        return p ? userChanged({p}) : userChanged({});
    // AVBRUTET: berättelsen postas under värdens vakt (`postEvent`), och
    // panelen är med bara om den lever då.
    const std::weak_ptr<char> objekt = p ? c.life : std::weak_ptr<char>{};
    postEvent([this, objekt, p] {
        if (p && !objekt.expired())
            userChanged({p});
        else
            userChanged({});
    });
    return false;
}

// PLATSEN för ett objekt som förblir flytande — ur fönstret när det är öppet
// (placeringsrymden, `DockFloatWindow::bounds`), annars ur bärandet: ett släpp före
// svansen. Då har fönstret inte öppnats, och en LANDNING väntar på det
// — bärandets plats i placeringsrymden —
// den sparade logiska klämmer `setLocation` in på skärmen — och, för ett
// släppt drag, lyftet (en panels och ett fälts). Ett objekt
// som dog eller dockades rörs inte.
void DockHost::rememberCarryPlace(const Carry& c, bool släppt)
{
    if (c.life.expired())
        return;
    const Widget* w = c.panel ? static_cast<const Widget*>(c.panel)
                              : static_cast<const Widget*>(c.bar);
    if (!(c.panel ? c.panel->isFloating() : c.bar->isFloating()))
        return;
    gfx::Pointf l =
        detail::toLogical(c.at, detail::primaryScale(gfx::screens()));
    gfx::Sizef yta{};
    bool       harYta = false;
    if (detail::DockFloat* f = floatOf(w); f && f->win->isOpen()) {
        bool             harPos = false;
        const gfx::Rectf r      = f->win->bounds(harPos);
        if (harPos)
            l = gfx::Pointf{r.x, r.y};
        yta    = gfx::Sizef{r.w, r.h};
        harYta = true;
    } else {
        std::erase_if(landings_,
                      [w](const Landing& g) { return g.item() == w; });
        landings_.push_back(Landing{c.panel, c.bar, c.life, c.at, släppt});
    }
    if (c.panel) {
        c.panel->floatBounds_.x = l.x;
        c.panel->floatBounds_.y = l.y;
        if (harYta) {
            c.panel->floatBounds_.w = yta.w;
            c.panel->floatBounds_.h = yta.h;
        }
        c.panel->floatPosKnown_ = true;
    } else {
        c.bar->floatPos_      = l;
        c.bar->floatPosKnown_ = true;
    }
}

std::optional<gfx::Pointf> DockHost::carryPlacement(const Widget& item) const
{
    if (carrying(item))
        return carry_.at;
    for (const Landing& g : landings_)
        if (g.item() == &item && !g.life.expired())
            return g.at;
    return std::nullopt;
}

// Fönstret öppnades (`syncFloats`): landningen är förbrukad. Ett släppt drag
// lyfter fönstret nu, som `carryEnd` gör med ett öppet (en panel eller ett fält)
// — ett dolt fönster lyfts inte (`raise` lämnar det dolt). Ett FÄLT
// som fortfarande BÄRS (fall A: fönstret öppnas i svansen mitt i draget) lyfts
// också: det ska ligga överst redan när det dyker upp, och `raise()` på dess
// noActivate-fönster tar inte fokus från värden.
void DockHost::landed(const Widget& item, DockFloatWindow& win)
{
    if (carrying(item) && carry_.bar && win.isOpen() && !win.isHidden())
        win.window().raise();
    const auto g = std::find_if(landings_.begin(), landings_.end(),
                                [&item](const Landing& l) {
                                    return l.item() == &item &&
                                           !l.life.expired();
                                });
    if (g == landings_.end())
        return;
    const bool lyft = g->raise;
    landings_.erase(g);
    if (lyft && win.isOpen() && !win.isHidden())
        win.window().raise();
}

// En landning gäller ett levande, flytande objekt i boken vars fönster ännu
// inte är öppet — och den bär fortfarande något (plats eller lyft).
void DockHost::pruneLandings()
{
    std::erase_if(landings_, [this](const Landing& g) {
        if (g.life.expired() || (!g.at && !g.raise))
            return true;
        if (!(g.panel ? g.panel->isFloating() : g.bar->isFloating()) ||
            !mine(g.item()))
            return true;
        const detail::DockFloat* f = floatOf(g.item());
        return f && f->win->isOpen();
    });
}

// TRYCKET ELLER CAPTUREN AVBRÖTS (värdens `mouseCancel`):
// fokusförlusten, OS:et (`MouseCaptureLost`),
// ett annat fönster eller ett nytt tryck — bokföringen är redan nollad. Höll
// värden ett buret drag (fall A) avslutas det som avbrutet och `onChange`
// postas, ingen användarkod i kroken; knappen spelar ingen roll, för draget är
// värdens capture. Annars släpps värdens egna vänstertryck — ett skikt
// mitt i sitt drag, ett flikdrag på platshållarvägen — och förhandsvisningen
// ritas bort NU, inte vid nästa ritning. Fall B:s motsvarighet är
// `itemPressLost`.
bool DockHost::mouseCancel(gfx::MouseButton b)
{
    if (carry_.active() && carry_.hostPress) {
        finishCarry(true);
        return false;   // beskedet är postat
    }
    if (b == gfx::MouseButton::Left && clearPress())
        invalidate();
    return false;
}

// OBJEKTETS TRYCK AVBRÖTS — kontraktet står vid deklarationen. Fall B: objektet
// håller explicit capture i sitt eget fönster, och fönstret tog den — samma
// avbrott som värdens krok, och samma postade `onChange`; utan det hade draget
// avslutats först vid en ritning, och ett dolt fönster ritas aldrig. Under
// övertagandet släpper källfönstret objektets tryck MED FLIT — det är
// övertagandet, inte ett avbrott. Platshållarvägens mål jämförs bara (pekaren
// derefereras inte).
void DockHost::itemPressLost(const Widget& item)
{
    if (carrying(item) && !carry_.hostPress && !carry_.takingOver)
        finishCarry(true);
    bool släckt = false;
    if (dragPanel_ && !dragFromTab_ &&
        static_cast<const Widget*>(dragPanel_) == &item) {
        släckt     = drop_.kind != Drop::Kind::None;
        dragPanel_ = nullptr;
        drop_      = Drop{};
    }
    if (toolDrag_ && static_cast<const Widget*>(toolDrag_) == &item) {
        släckt    = släckt || toolDrop_.kind != ToolDrop::Kind::None;
        toolDrag_ = nullptr;
        toolDrop_ = ToolDrop{};
    }
    if (släckt)
        invalidate();   // platshållaren ska bort
}

// ------------------------------------------------------------ programmatiskt

void DockHost::dockPanel(DockPanel& p, Dock edge)
{
    if (!mine(&p)) {
        gfx::log("DockHost(%s): dockPanel — '%s' är inget barn (står raden "
                 "före adopt?); ingenting ändras",
                 name.c_str(), p.name.c_str());
        return;
    }
    if (!isDockEdge(edge)) {
        gfx::log("DockHost(%s): dockPanel('%s', …) — kanten ska vara Left, "
                 "Top, Right eller Bottom; ingenting ändras",
                 name.c_str(), p.name.c_str());
        return;
    }
    // En FLYTANDE panel dockas tillbaka — `apply`s väg.
    if (p.isFloating()) {
        apply(p, Drop{Drop::Kind::Edge, edge, nullptr});
        return;
    }
    syncModel();
    if (model_->dockAtEdge(p, edge))
        writeBackEdges();
    model_->activate(p);
    invalidate();
    guardFocus();
}

void DockHost::dockPanel(DockPanel& p, DockPanel& target, Dock where)
{
    if (!mine(&p) || !owns(&target)) {
        gfx::log("DockHost(%s): dockPanel('%s', '%s', …) — båda måste vara "
                 "barn (står raden före adopt?); ingenting ändras",
                 name.c_str(), p.name.c_str(), target.name.c_str());
        return;
    }
    if (&p == &target || where == Dock::None) {
        gfx::log("DockHost(%s): dockPanel('%s', '%s', …) — en panel dockas "
                 "inte på sig själv, och Dock::None är ingen plats; "
                 "ingenting ändras",
                 name.c_str(), p.name.c_str(), target.name.c_str());
        return;
    }
    syncModel();
    if (!model_->contains(target)) {
        gfx::log("DockHost(%s): dockPanel('%s', '%s', …) — målet är inte "
                 "dockat vid en kant; ingenting ändras",
                 name.c_str(), p.name.c_str(), target.name.c_str());
        return;
    }
    if (p.isFloating()) {
        apply(p, Drop{Drop::Kind::Panel, where, &target});
        return;
    }
    if (model_->dockOn(p, target, where))
        writeBackEdges();
    model_->activate(p);
    invalidate();
    guardFocus();
}

// ------------------------------------------------------------ musen

// VÄRDENS EGNA TRYCK — kontraktet står vid deklarationen. "Levde" betyder att
// något ritades för trycket: ett skikts förhandsvisning, ett handtags
// tryckutseende, en platshållare.
bool DockHost::clearPress()
{
    const bool levde = stripDrag_ || cellDragA_ || cellDragB_ ||
                       !cellWeights_.empty() || pressTab_ || pressHandle_ ||
                       tabDrag_ || drop_.kind != Drop::Kind::None ||
                       stripCollapse_ || cellCollapse_ != 0;
    stripDrag_   = nullptr;
    cellDragA_   = nullptr;
    cellDragB_   = nullptr;
    cellWeights_.clear();
    pressTab_    = nullptr;
    pressHandle_ = nullptr;
    pressInside_ = false;
    tabDrag_     = false;
    dragPanel_   = nullptr;
    drop_        = Drop{};
    stripCollapse_ = false;
    cellCollapse_  = 0;
    return levde;
}

void DockHost::mouseDown(const Ctx& c, float x, float y)
{
    // Ett BURET drag som står kvar har tappat sitt släpp — det avslutas som
    // avbrutet innan det nya trycket räknas.
    if (carry_.active())
        finishCarry(true);
    // Ett äldre tryck som avbröts utan att någon hörde det lämnade
    // pekarna kvar; nollas FÖRE träffen, annars räknades den mot den gamla
    // förhandsvisningen. (Ett avbrott med `mouseCancel` har redan nollat dem.)
    clearPress();
    pressX_      = x;
    pressY_      = y;
    syncModel();

    const Hit h = hitAt(c, x, y);
    // ETT SKIKTDRAG AVBRYTER REMSANS GLIDNING (ett kantfall): slutvärdet
    // gäller från trycket, och draget räknas mot det.
    if ((h.part == Part::StripSash || h.part == Part::CellSash) && h.panel) {
        const int si = model_->stripIndexOf(*h.panel);
        anims_.erase(std::remove_if(anims_.begin(), anims_.end(),
                                    [&](const Anim& a) {
                                        return !a.overlay &&
                                               model_->stripIndexOf(*a.panel) ==
                                                   si;
                                    }),
                     anims_.end());
    }
    const gfx::Rectf   r = resolve(c);
    const DockGeometry g = geometry(c, Draw::bounds(c, r));
    switch (h.part) {
    case Part::StripSash:
        for (const DockGeometry::Strip& s : g.strips) {
            const DockGeometry::Cell* cell = g.cellShowing(h.panel);
            if (!cell || cell->strip != s.index)
                continue;
            const gfx::Rectf skikt   = Draw::project(c, r, s.sash);
            const bool       stående = isStandingEdge(s.edge);
            grip_       = stående ? x - skikt.x : y - skikt.y;
            dragThick_  = stående ? s.rect.w : s.rect.h;
            startThick_ = dragThick_;
            stripDrag_  = h.panel;
        }
        break;
    case Part::CellSash:
        for (const DockGeometry::CellSash& cs : g.cellSashes) {
            const DockGeometry::Cell& a = g.cells[std::size_t(cs.before)];
            if (a.shown != h.panel)
                continue;
            const bool stående =
                isStandingEdge(model_->strips[std::size_t(a.strip)].edge);
            const gfx::Rectf skikt = Draw::project(c, r, cs.rect);
            grip_      = stående ? y - skikt.y : x - skikt.x;
            cellFirst_ = stående ? a.rect.h : a.rect.w;
            cellStart_ = cellFirst_;
            cellDragA_ = h.panel;
            cellDragB_ = h.other;
        }
        break;
    case Part::Tab:
        // Fliken väljs vid TRYCKET (Windows flikar, TabControl) — och ett
        // drag kan följa. Tangentbordets val följer trycket, så fokusringen
        // står på den här fliken.
        pressTab_ = h.panel;
        selectKbdItem(false, h.panel);
        userActivate(*h.panel, false);
        break;
    case Part::Handle:
        pressHandle_ = h.panel;
        pressInside_ = true;
        selectKbdItem(true, h.panel);
        invalidateHit(c, h);
        break;
    case Part::None:
        break;
    }
}

void DockHost::mouseDrag(const Ctx& c, float x, float y)
{
    // DET BURNA DRAGET som värden håller (fall A, och fall B där trycket togs
    // över): varje rörelse kommer hit genom capturen, i värdens fönster.
    if (carry_.active() && carry_.hostPress) {
        if (AppWindow* const f = appWindow())
            carryMove(*f, x, y);
        return;
    }
    const gfx::Rectf r = resolve(c);
    if (liveStripSash()) {
        // Remsans mått ur layouten UTAN snappens infällning — förhandsvisningen
        // tar bort remsan, men draget räknas mot den.
        const detail::DockModel mm = synced();
        detail::DockLive        l  = live(mm);
        l.collapsed.clear();
        const DockGeometry g  = layoutIn(mm, c, Draw::bounds(c, r), l);
        const int          si = mm.stripIndexOf(*stripDrag_);
        const DockGeometry::Strip* s = nullptr;
        for (const DockGeometry::Strip& t : g.strips)
            if (t.index == si)
                s = &t;
        if (!s)
            return;   // remsan stängdes eller fälldes in mitt i draget
        const bool       stående = isStandingEdge(s->edge);
        const bool       före = s->edge == Dock::Left || s->edge == Dock::Top;
        const gfx::Rectf ytan = Draw::project(c, r, s->before);
        // Remsans YTTRE kant står still; skiktet följer handen.
        const float kant  = före ? (stående ? ytan.x : ytan.y)
                                 : (stående ? ytan.x + ytan.w : ytan.y + ytan.h);
        const float skikt = stående ? s->sash.w : s->sash.h;
        const float vill  = sash::paneFromPointer(stående ? x : y, kant, före,
                                                  grip_, skikt * c.scale,
                                                  c.scale);
        // SNAPPENS INFÄLLNING: under golvet minus zonen visar
        // förhandsvisningen remsans paneler infällda.
        const float zon = c.style->dockSnapZone;
        if (vill < s->floor - zon) {
            if (!stripCollapse_) {
                stripCollapse_ = true;
                invalidate();
            }
            return;
        }
        if (stripCollapse_) {
            stripCollapse_ = false;   // tillbaka ovanför golvet
            invalidate();
        }
        // MAGNETERNA: golvet, taket och det förvalda måttet.
        float snäppt = vill;
        float bäst   = zon;
        for (const float mål : {s->floor, s->maxThick, c.style->dockDefaultSize})
            if (mål > 0.0f && std::abs(vill - mål) <= bäst) {
                bäst   = std::abs(vill - mål);
                snäppt = mål;
            }
        // Golvet är panelernas minsta mått (`dockHeaderH` utan ett satt),
        // taket deras största — och alltid så att skiktet ryms.
        const float längs = stående ? s->before.w : s->before.h;
        float       tak   = std::max(0.0f, längs - skikt);
        if (s->maxThick > 0.0f)
            tak = std::min(tak, std::max(s->maxThick, s->floor));
        const float ny = sash::clampPane(snäppt, s->floor, 0.0f, tak);
        if (ny == dragThick_)
            return;   // regel 1: en rörelse som inte flyttar skiktet ritar inget
        dragThick_ = ny;
        invalidate();   // remsan och allt innanför den flyttar
        return;
    }
    if (liveCellSash()) {
        // BASEN är layouten ur modellens EGNA vikter (ingen förhandsvisning,
        // ingen infällning): skiktets början och parets längd står still
        // medan handen rör sig.
        const detail::DockModel m = synced();
        detail::DockLive        l = live(m);
        l.cellStrip = -1;
        l.cellWeights.clear();
        l.collapsed.clear();
        const DockGeometry        g = layoutIn(m, c, Draw::bounds(c, r), l);
        const DockGeometry::Cell* a = g.cellShowing(cellDragA_);
        const DockGeometry::Cell* e = g.cellShowing(cellDragB_);
        if (!a || !e || a->strip != e->strip)
            return;   // en av cellerna försvann mitt i draget
        const detail::DockStrip& s = m.strips[std::size_t(a->strip)];
        const bool               stående = isStandingEdge(s.edge);
        const gfx::Rectf         ap      = Draw::project(c, r, a->rect);
        const float              par     = stående ? a->rect.h + e->rect.h
                                                   : a->rect.w + e->rect.w;
        const float vill =
            ((stående ? y : x) - grip_ - (stående ? ap.y : ap.x)) / c.scale;
        // SNAPPENS INFÄLLNING: en av de två cellerna under sitt golv
        // minus zonen — förhandsvisningen visar den cellens flikar infällda.
        const float zon    = c.style->dockSnapZone;
        const int   infäll = vill < a->floor - zon         ? 1
                             : par - vill < e->floor - zon ? 2
                                                           : 0;
        if (infäll != 0) {
            if (cellCollapse_ != infäll) {
                cellCollapse_ = infäll;
                invalidate();
            }
            return;
        }
        if (cellCollapse_ != 0) {
            cellCollapse_ = 0;   // tillbaka ovanför golven
            invalidate();
        }
        // MAGNETERNA: båda cellernas golv och tak — sedda från den
        // första — och den jämna delningen, cellskiktets förvalda mått (en
        // ny stapel delar målets längd lika, `dockOn`).
        float snäppt = vill;
        float bäst   = zon;
        auto  magnet = [&](float mål) {
            if (mål > 0.0f && std::abs(vill - mål) <= bäst) {
                bäst   = std::abs(vill - mål);
                snäppt = mål;
            }
        };
        magnet(a->floor);
        magnet(a->maxLen);
        magnet(par - e->floor);
        if (e->maxLen > 0.0f)
            magnet(par - e->maxLen);
        magnet(par / 2.0f);
        // ÅTERSKAPBARA LÄNGDER: båda cellernas
        // golv och tak. Golvet är > 0 (panelernas minsta mått eller
        // `dockHeaderH`), så ingen av dem blir 0 — en längd ingen positiv
        // vikt ger. Ryms inte båda golven står skiktet still.
        const float lo = std::max(a->floor,
                                  e->maxLen > 0.0f
                                      ? par - std::max(e->maxLen, e->floor)
                                      : 0.0f);
        float hi = par - e->floor;
        if (a->maxLen > 0.0f)
            hi = std::min(hi, std::max(a->maxLen, a->floor));
        if (lo > hi)
            return;
        const float ny = std::clamp(snäppt, lo, hi);
        if (ny == cellFirst_)
            return;   // regel 1: samma längd — ingen omritning
        // VIKTERNA för ALLA synliga celler i remsan: paret får dragets
        // längder, de andra behåller sina. Förhandsvisningen är layouten ur
        // dem (`DockLive`), och släppet skriver just dem.
        std::vector<std::pair<int, float>> längder;
        for (const DockGeometry::Cell& gc : g.cells) {
            if (gc.strip != a->strip)
                continue;
            float l = stående ? gc.rect.h : gc.rect.w;
            if (&gc == a)
                l = ny;
            else if (&gc == e)
                l = par - ny;
            längder.push_back({gc.cell, l});
        }
        std::vector<float> w = detail::weightsForLengths(s, längder);
        if (w.empty())
            return;   // en hopklämd cell (0 px) — remsan är överfull
        cellFirst_   = ny;
        cellWeights_ = std::move(w);
        invalidate();
        return;
    }
    if (pressTab_ && owns(pressTab_) && pressTab_->isEnabled()) {
        if (!tabDrag_) {
            if (!detail::pastDragThreshold(c, pressX_, pressY_, x, y))
                return;
            tabDrag_     = true;
            dragFromTab_ = true;
        }
        // FLIKEN LOSSNAR vid tröskeln där draget kan bäras; annars
        // platshållarvägen med platshållaren medan panelen står kvar.
        AppWindow* const f = appWindow();
        if (f && canCarry(*pressTab_))
            beginCarry(*pressTab_, *f, x, y, true);
        else
            panelDragTo(c, *pressTab_, x, y);
        return;
    }
    if (pressHandle_) {
        const Hit  h    = hitAt(c, x, y);
        const bool inne = h.part == Part::Handle && h.panel == pressHandle_;
        if (inne == pressInside_)
            return;
        pressInside_ = inne;
        invalidateHit(c, Hit{Part::Handle, pressHandle_, nullptr});
    }
}

// SLÄPPET skriver måttet eller fäller ut — bara när värden fortfarande är
// aktiverad (inaktiverad mitt i draget = draget gäller inte;
// förhandsvisningen faller tillbaka av sig själv).
bool DockHost::mouseUp(const Ctx& c, float x, float y)
{
    // DET BURNA DRAGET som värden håller slutar här (capturen ger släppet
    // till värden, var pekaren än står).
    if (carry_.active() && carry_.hostPress) {
        AppWindow* const f = appWindow();
        return f ? carryEnd(*f, x, y) : finishCarry(true);
    }
    // REMSSKIKTET: tjockleken till ALLA paneler i remsan — eller, under
    // snappens infällning, ALLA dess utfällda paneler infällda.
    if (DockPanel* p = stripDrag_) {
        stripDrag_        = nullptr;
        const bool infäll = stripCollapse_;
        stripCollapse_    = false;
        if (infäll)
            invalidate();   // förhandsvisningen ska bort, hur det än går
        // Remsans paneler SKRIVS — en hemvändare som dog under draget rensas
        // först, och remsan slås upp efteråt.
        syncModel();
        const int si = owns(p) ? model_->stripIndexOf(*p) : -1;
        if (si < 0 || !isEnabled())
            return false;
        if (infäll) {
            std::vector<DockPanel*> ändrade;
            for (const detail::DockCell& cell :
                 model_->strips[std::size_t(si)].cells)
                for (DockPanel* q : cell.tabs)
                    if (docksOpen(q))
                        ändrade.push_back(q);
            return collapseByUser(ändrade);
        }
        if (dragThick_ == startThick_)
            return false;
        const bool stående =
            isStandingEdge(model_->strips[std::size_t(si)].edge);
        std::vector<DockPanel*> ändrade;
        for (const detail::DockCell& cell : model_->strips[std::size_t(si)].cells)
            for (DockPanel* q : cell.tabs) {
                (stående ? q->width : q->height) = dragThick_;
                ändrade.push_back(q);
            }
        invalidate();
        return userChanged(ändrade);
    }

    // CELLSKIKTET: förhandsvisningens vikter — exakt de som visades — till
    // remsans ALLA celler.
    if (cellDragA_ || cellDragB_) {
        DockPanel*         a      = cellDragA_;
        DockPanel*         e      = cellDragB_;
        std::vector<float> w      = std::move(cellWeights_);
        const int          infäll = cellCollapse_;
        cellDragA_                = nullptr;
        cellDragB_                = nullptr;
        cellCollapse_             = 0;
        cellWeights_.clear();
        if (infäll != 0)
            invalidate();   // förhandsvisningen ska bort, hur det än går
        if (!a || !e || !owns(a) || !owns(e) || !isEnabled())
            return false;
        syncModel();   // som remsskiktet ovan
        // SNAPPENS INFÄLLNING: cellens utfällda flikar fälls in, och
        // vikterna står kvar — cellen fälls ut i sin gamla längd.
        if (infäll != 0) {
            std::vector<DockPanel*> ändrade;
            if (const detail::DockCell* cell =
                    model_->cellOf(infäll == 1 ? *a : *e))
                for (DockPanel* q : cell->tabs)
                    if (docksOpen(q))
                        ändrade.push_back(q);
            return collapseByUser(ändrade);
        }
        if (cellFirst_ == cellStart_ || w.empty())
            return false;
        syncModel();
        const int si = model_->stripIndexOf(*a);
        if (si < 0 || si != model_->stripIndexOf(*e))
            return false;
        detail::DockStrip& s = model_->strips[std::size_t(si)];
        if (s.cells.size() != w.size())
            return false;   // remsan fick eller tappade en cell mitt i draget
        for (std::size_t i = 0; i < w.size(); ++i)
            s.cells[i].weight = w[i];
        invalidate();
        return userChanged({a, e});
    }

    // FLIKDRAGET: släppet dockar fliken där indikatorn stod.
    if (tabDrag_) {
        DockPanel* p = pressTab_;
        tabDrag_     = false;
        pressTab_    = nullptr;
        return p && owns(p) ? panelDragEnd(*p, x, y) : false;
    }
    pressTab_ = nullptr;

    // HANDTAGET: ett klick (tryck och släpp på samma) fäller ut panelen.
    if (DockPanel* p = pressHandle_) {
        pressHandle_ = nullptr;
        pressInside_ = false;
        invalidateHit(c, Hit{Part::Handle, p, nullptr});
        const Hit h = hitAt(c, x, y);
        if (h.part != Part::Handle || h.panel != p || !isEnabled() ||
            !owns(p) || !p->isEnabled())
            return false;   // släppt utanför (ett ångrat klick) eller inert
        return userExpand(*p, true);
    }
    return false;
}

// Hover: bara DELEN under pekaren tänds, och bara dess yta ritas om när den
// byts (regel 1: en rörelse inom samma del ritar ingenting).
void DockHost::mouseMove(const Ctx& c, float x, float y)
{
    const Hit h = hitAt(c, x, y);
    if (h == hover_)
        return;
    invalidateHit(c, hover_);
    hover_ = h;
    invalidateHit(c, hover_);
}

gfx::PointerShape DockHost::cursorAt(const Ctx& c, float x, float y) const
{
    if (liveStripSash()) {
        const int si = model_->stripIndexOf(*stripDrag_);
        if (si >= 0)
            return sash::cursor(
                isStandingEdge(model_->strips[std::size_t(si)].edge));
    }
    if (liveCellSash()) {
        const int si = model_->stripIndexOf(*cellDragA_);
        if (si >= 0)
            return sash::cursor(
                !isStandingEdge(model_->strips[std::size_t(si)].edge));
    }
    const Hit h = hitAt(c, x, y);
    if ((h.part != Part::StripSash && h.part != Part::CellSash) || !h.panel)
        return gfx::PointerShape::Arrow;
    const bool stående = isStandingEdge(h.panel->dock);   // fälten är sanningen
    return sash::cursor(h.part == Part::StripSash ? stående : !stående);
}

bool DockHost::liveStripSash() const
{
    return stripDrag_ && pressed() && owns(stripDrag_) &&
           model_->contains(*stripDrag_);
}

bool DockHost::liveCellSash() const
{
    return cellDragA_ && cellDragB_ && pressed() && owns(cellDragA_) &&
           owns(cellDragB_);
}

TrText DockHost::toolTipAt(const Ctx& c, float x, float y) const
{
    const Hit h = hitAt(c, x, y);
    if ((h.part == Part::Handle || h.part == Part::Tab) && h.panel)
        return h.panel->textToken();
    return toolTipToken();
}

// ------------------------------------------------------------ användaren

bool DockHost::userActivate(DockPanel& p, bool intoPanel)
{
    if (!model_->activate(p))
        return false;   // redan vald — ingen ändring, inget onChange
    invalidate();
    if (!intoPanel || !focusFirstStopIn(p)) {
        if (intoPanel)
            focusHostOrClear();   // fliken saknar stopp — värden, annars inget
        else
            guardFocus();
    }
    return userChanged({&p});
}

// Utfälld (ett klick på handtaget eller Enter): fokus in i panelen när den
// har ett stopp — annars prövar vakten om fokus står kvar på en värd som
// just slutade vara ett stopp (det sista handtaget). En ONÅLAD panel fälls
// ut som overlay, och den ÖPPNA overlayns handtag fäller in den igen — en
// växel, som menyknappen. Overlayn är ett DRIFTTILLSTÅND: `layout()`
// bär den inte, så att öppna eller fälla in den fyrar inget `onChange`.
bool DockHost::userExpand(DockPanel& p, bool focusInside)
{
    if (&p == overlayPanel()) {
        collapseOverlay();
        return false;
    }
    p.setCollapsed(false);
    model_->activate(p);
    invalidate();
    if (!focusInside || !focusFirstStopIn(p))
        guardFocus();
    if (&p == overlayPanel())
        return false;
    return userChanged({&p});
}

// Panelernas `onChange` och därefter värdens, i den ordningen — med vakten
// frågad mellan varje: en handler får riva värden eller ta bort en panel,
// och då rörs ingenting efteråt.
bool DockHost::userChanged(const std::vector<DockPanel*>& panels)
{
    bool               körd = false;
    const HandlerGuard vakt(*this);
    for (DockPanel* p : panels) {
        if (!vakt.ok())
            return körd;
        if (!mine(p) || !p->onChange)
            continue;
        körd = true;
        p->fireEvent(p->onChange);
    }
    if (vakt.ok() && onChange) {
        körd = true;
        fireEvent(onChange);
    }
    return körd;
}

// ------------------------------------------------------------ tangentbordet

std::vector<DockHost::KbdItem> DockHost::kbdItems() const
{
    std::vector<KbdItem>    ut;
    const detail::DockModel m = synced();
    for (const detail::DockStrip& s : m.strips)
        for (const detail::DockCell& cell : s.cells) {
            if (detail::openTabs(cell) < 2)
                continue;
            for (DockPanel* p : cell.tabs)
                if (docksOpen(p))
                    ut.push_back(KbdItem{false, p});
        }
    for (const auto& [kant, p] : detail::handlesInOrder(m))
        ut.push_back(KbdItem{true, p});
    return ut;
}

// Listans ordning beror inte på vilken flik som är vald (alla öppna flikar i
// varje grupp, därefter alla handtag), så indexet står sig genom aktiveringen
// som följer trycket. Ringen flyttar — värden ritas om.
void DockHost::selectKbdItem(bool handle, const DockPanel* p)
{
    const std::vector<KbdItem> val = kbdItems();
    for (std::size_t i = 0; i < val.size(); ++i) {
        if (val[i].handle != handle || val[i].panel != p)
            continue;
        if (kbdSel_ != static_cast<int>(i)) {
            kbdSel_ = static_cast<int>(i);
            invalidate();
        }
        return;
    }
}

bool DockHost::focusable() const
{
    return !kbdItems().empty();
}

bool DockHost::keyDown(gfx::Key k, std::uint32_t mods)
{
    if (mods & (gfx::kModAlt | gfx::kModCtrl))
        return false;   // menyernas och appens
    const std::vector<KbdItem> val = kbdItems();
    if (val.empty())
        return false;
    const int n = static_cast<int>(val.size());
    int       i = std::clamp(kbdSel_, 0, n - 1);
    switch (k) {
    case gfx::Key::Left:
    case gfx::Key::Up:    i = std::max(i - 1, 0); break;
    case gfx::Key::Right:
    case gfx::Key::Down:  i = std::min(i + 1, n - 1); break;
    case gfx::Key::Home:  i = 0; break;
    case gfx::Key::End:   i = n - 1; break;
    case gfx::Key::Escape:
        // Escape på värden med en öppen overlay fäller in den.
        if (!overlayPanel())
            return false;
        collapseOverlay();
        return true;
    case gfx::Key::Space:
    case gfx::Key::Return: {
        kbdSel_ = i;
        const KbdItem it = val[std::size_t(i)];
        if (it.handle && it.panel->isEnabled())
            userExpand(*it.panel, true);   // fokus till panelens första stopp
        else if (!it.handle)
            userActivate(*it.panel, false);
        return true;
    }
    default:
        return false;
    }
    if (i != kbdSel_) {
        kbdSel_ = i;
        invalidate();
    }
    return true;   // också i kanten — tangenten är värdens
}

// FOKUS STÅR I EN PANEL: Ctrl+PageUp/PageDown byter flik i panelens cell,
// runt om, och fokus följer med in i den nya fliken.
bool DockHost::descendantKeyDown(gfx::Key k, std::uint32_t mods)
{
    // ESCAPE INIFRÅN OVERLAYN fäller in den direkt, och fokus går till dess
    // handtag (fokusvakten i `panelStateChanged`).
    if (k == gfx::Key::Escape &&
        !(mods & (gfx::kModCtrl | gfx::kModAlt | gfx::kModShift)))
        if (DockPanel* o = overlayPanel(); o && hållerFokus(*o)) {
            collapseOverlay();
            return true;
        }
    if (!(mods & gfx::kModCtrl) || (mods & gfx::kModAlt) ||
        (k != gfx::Key::PageUp && k != gfx::Key::PageDown))
        return false;
    // Cellens flikar LÄSES — en hemvändare som dog under väntan rensas först
    // och cellen hämtas efteråt.
    syncModel();
    for (Widget* ch : children()) {
        auto* p = dynamic_cast<DockPanel*>(ch);
        if (!p || !hållerFokus(*p))
            continue;
        const detail::DockCell* cell = model_->cellOf(*p);
        if (!cell)
            return false;
        std::vector<DockPanel*> öppna;
        for (DockPanel* q : cell->tabs)
            if (docksOpen(q))
                öppna.push_back(q);
        const auto it = std::find(öppna.begin(), öppna.end(), p);
        if (öppna.size() < 2 || it == öppna.end())
            return false;
        const int n   = static_cast<int>(öppna.size());
        const int dir = k == gfx::Key::PageDown ? 1 : -1;
        const int i   = static_cast<int>(it - öppna.begin());
        userActivate(*öppna[std::size_t(((i + dir) % n + n) % n)], true);
        return true;
    }
    return false;
}

// ------------------------------------------------------------ fokus

// Panelens EGET fönster — värdens, eller det flytande.
bool DockHost::focusFirstStopIn(const Widget& w)
{
    AppWindow* fönster = w.appWindow();
    if (!fönster)
        return false;
    for (Widget* f : fönster->focusOrder())
        if (liggerUnder(*f, w)) {
            f->focus();
            return true;
        }
    return false;
}

bool DockHost::focusCenter()
{
    AppWindow* fönster = appWindow();
    if (!fönster)
        return false;
    for (Widget* f : fönster->focusOrder()) {
        const Widget* topp = f;
        while (topp && topp->parent() != this)
            topp = topp->parent();
        if (!topp)
            continue;   // utanför värden
        // Huvudytan är det som varken är en dockad panel, ett flytande
        // objekt på väg ut eller ett fält i ett band — ett fält i ett
        // verktygsfält är inte huvudytans stopp.
        const auto* tp = dynamic_cast<const DockPanel*>(topp);
        const auto* tb = dynamic_cast<const ToolBar*>(topp);
        if ((tp && (tp->isFloating() || model_->contains(*tp))) ||
            (tb && (tb->isFloating() || model_->barInBand(*tb))))
            continue;
        f->focus();
        return true;
    }
    return false;
}

// Värden är ett giltigt fokusmål bara som tabbstopp — annars hade fokus
// stått på en widget Tab inte känner, och tangenterna gått till den.
void DockHost::focusHostOrClear()
{
    if (focusable() && isEnabled() && isVisible())
        focus();
    else if (AppWindow* fönster = appWindow())
        fönster->clearFocus();
}

// FOKUS STANNAR ALDRIG där ingen ser det (TabControl-regeln):
// en infälld panel lämnar det till sitt
// handtag, en inaktiv flik till den visade flikens första stopp, och en
// STÄNGD panel — av användaren eller av appen — till huvudytans första
// stopp. Reserven är värden när den är ett tabbstopp, och annars ett
// fönster utan fokus: ett textfält i en panel ingen ser får aldrig ta emot
// det användaren skriver. Står fokus på värden själv när den slutat vara ett
// stopp (det sista handtaget fälldes ut) gäller samma reserv.
void DockHost::guardFocus()
{
    AppWindow* fönster = appWindow();
    if (!fönster)
        return;
    if (syncModel())
        invalidate();
    if (focused()) {
        if ((!focusable() || !isEnabled()) && !focusCenter())
            fönster->clearFocus();
        return;
    }
    for (Widget* ch : children()) {
        // ETT DOLT VERKTYGSFÄLT: `hide()`
        // eller `t=…,1` med fokus i ett fält i det — huvudytans första stopp,
        // annars värden som stopp, annars inget. Likaså ett fält PÅ VÄG UT
        // till sitt flytande fönster (det har ingen yta
        // här längre, som en flytande panel på väg ut). Ett synligt, dockat
        // fält får stå.
        if (auto* t = dynamic_cast<ToolBar*>(ch); t && hållerFokus(*t)) {
            if ((!t->isVisible() || t->isFloating()) && !focusCenter())
                focusHostOrClear();
            return;
        }
        auto* p = dynamic_cast<DockPanel*>(ch);
        if (!p || !hållerFokus(*p))
            continue;
        if (p->isVisible() && showsPanel(*p))
            return;   // panelen syns — fokus får stå
        const bool dockad = model_->contains(*p);
        if (dockad && p->isVisible() && p->isCollapsed()) {
            // INFÄLLD: handtaget väljs, och värden tar fokus.
            const std::vector<KbdItem> val = kbdItems();
            for (std::size_t i = 0; i < val.size(); ++i)
                if (val[i].handle && val[i].panel == p)
                    kbdSel_ = static_cast<int>(i);
            focusHostOrClear();
            return;
        }
        if (dockad && p->isVisible()) {
            // EN INAKTIV FLIK: den visade flikens första stopp.
            if (const detail::DockCell* cell = model_->cellOf(*p))
                if (DockPanel* v = detail::shownTab(*cell))
                    if (focusFirstStopIn(*v))
                        return;
            focusHostOrClear();
            return;
        }
        // STÄNGD (eller en hopfälld panel i huvudytan, som inget handtag
        // har): huvudytans första stopp.
        if (!focusCenter())
            focusHostOrClear();
        return;
    }
}

} // namespace glaze
