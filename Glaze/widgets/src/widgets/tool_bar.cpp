// ToolBar — typidentiteten, registreringen och verktygsfältets MÅTT: höjden,
// luften runt barnen, luckan mellan dem och greppet. Ritningen (ytan +
// hårlinjen mot innehållet) och flexen är Bars (bar.cpp) — verktygsfältet är
// samma sorts remsa som menyraden och statusraden och ska se ut så, precis
// som menu_bar.cpp säger om sig själv. I en dockningsyta är fältet en del av
// ett BAND, och greppdraget räknar värden (dock_host.cpp). Det FLYTANDE
// läget är tillstånd här — fönstret, rubriken och flytten
// mellan träden äger värden (src/dock/dock_float.*).
//
// ÖVERFLÖDET bor också här, och det LÅNAR allt det kan: knappen är
// en vanlig `ToolButton` (trycket, plattan, toggeln och menyns liv är dess),
// menyn en vanlig `Menu`, en dold knapps klick `ToolButton::performClick`
// och dess poster dess egen `Menu::activatePath`. Det som är fältets eget är
// SNITTET — vilka barn som ryms, räknat per fråga som flexen — och att bygga
// menyn ur det vid varje tryck.

#include "glaze/widgets/tool_bar.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <system_error>
#include <utility>

#include <gfx/platform.h>

#include "glaze/app_window.h"   // fokus och Tab-ordningen (överflödet)
#include "glaze/draw.h"
#include "glaze/language.h"
#include "glaze/stock_icon.h"
#include "glaze/strings.h"
#include "glaze/widgets/dock_host.h"
#include "glaze/widgets/separator.h"
#include "glaze/widgets/spacer.h"
#include "glaze/widgets/tool_button.h"   // AUTO-tjockleken, överflödet

#include "../dock/dock_model.h"   // dragtröskeln, delad med dockningens drag
#include "../prop_parse.h"

namespace glaze {

GLAZE_REGISTER_WIDGET("toolbar", ToolBar)

// ÖVERFLÖDSKNAPPEN — en VANLIG verktygsknapp, så trycket, plattan,
// toggeln och hela menyns liv är `ToolButton`s och inget av det kopieras:
// ramverkets ikon och tooltip, ingen pil (`showsArrow` — ikonen säger att den
// öppnar en meny) och längden `toolOverflowW` längs fältet. Fältet bygger
// menyn vid varje tryck som öppnar den (`fillOverflowMenu`), FÖRE knappens
// eget tryck; ett tryck medan menyn står öppen stänger den som vanligt.
class ToolBar::OverflowButton final : public ToolButton {
public:
    OverflowButton()
    {
        setImg(stockIcon(StockIcon::MoreHorizontal));
        setToolTip(tr(str::ToolBarMore));
    }

protected:
    gfx::Sizef contentSize(const Ctx& c) const override
    {
        switch (c.ui) {
        case UI::Slate:
        default: {
            // Verktygsknappens mått tvärs (fältet ger ändå sin innerhöjd) och
            // stilens längs fältet, i båda riktningarna.
            gfx::Sizef s = ToolButton::contentSize(c);
            (bar().isVertical() ? s.h : s.w) = c.style->toolOverflowW;
            return s;
        }
        }
    }

    void mouseDown(const Ctx& c, float mx, float my) override
    {
        if (!isMenuOpen() && !bar().fillOverflowMenu(c))
            return;   // knappen dog i en `onDropDown` — rör den inte mer
        ToolButton::mouseDown(c, mx, my);
    }

    bool showsArrow() const override { return false; }

private:
    // Föräldern är alltid fältet: knappen skapas där och går inte att lossa.
    const ToolBar& bar() const { return static_cast<const ToolBar&>(*parent()); }
    ToolBar&       bar() { return static_cast<ToolBar&>(*parent()); }
};

ToolBar::ToolBar()
{
    dock      = Dock::Top;
    overflow_ = add<OverflowButton>();
}

// Bara ett fält som bor i ett av värdens flytande fönster (`floatHost_`) har
// något att säga: ett dockat är värdens vanliga barn, och en värd som rivs
// först har redan nollat pekaren. Fältets lånade barn kan vara döda — värden
// rör ingenting i delträdet.
ToolBar::~ToolBar()
{
    if (floatHost_)
        floatHost_->floatingItemDestroyed(*this);
}

void ToolBar::setText(TrText t)
{
    if (text_ == t)
        return;
    text_ = std::move(t);
    notifyHost();   // det flytande fönstrets rubrik och titel
}

void ToolBar::setBand(int b)
{
    if (b < 0) {
        gfx::log("ToolBar(%s): setBand(%d) — bandet är ett index >= 0; "
                 "ignoreras",
                 name.c_str(), b);
        return;
    }
    if (b == band_)
        return;
    band_ = b;
    // I en värd flyttar fältet vid värdens nästa synk (fälten vann,
    // dock_host.h) — syskonen och värden ritas om.
    invalidate();
    relayoutSiblings();
    if (DockHost* h = host())
        h->invalidate();
}

// FLYTANDE: TILLSTÅNDET gäller direkt — `isFloating()`
// och värdens `layout()` svarar redan — och värden flyttar fältet mellan
// fönstren i pumpens svans. Den effektiva riktningen kan bytas (ett stående
// fält ligger när det flyter), och då byts bredd och höjd.
void ToolBar::setFloating(bool on)
{
    if (floating_ == on)
        return;
    const bool var = isVertical();
    floating_      = on;
    turnIfNeeded(var);
    invalidate();
    relayoutSiblings();
    if (DockHost* h = host())
        h->barFloatingChanged(*this);
}

void ToolBar::setFloatable(bool on)
{
    floatable_ = on;   // ritar ingenting — det är användarens vägar som ändras
}

// DE TILLÅTNA KANTERNA — som `setFloatable` ritar
// det ingenting och invaliderar inget: bara användarens drag ändras
// (`DockHost::toolDropAt` frågar `allowsEdge`).
void ToolBar::setAllowedEdges(std::vector<Dock> edges)
{
    edgeLeft_ = edgeTop_ = edgeRight_ = edgeBottom_ = false;
    for (const Dock d : edges) {
        switch (d) {
        case Dock::Left:   edgeLeft_   = true; break;
        case Dock::Top:    edgeTop_    = true; break;
        case Dock::Right:  edgeRight_  = true; break;
        case Dock::Bottom: edgeBottom_ = true; break;
        default:
            gfx::log("ToolBar::setAllowedEdges(%s): Fill och None är inga "
                     "kanter (ignoreras)", name.c_str());
            break;
        }
    }
}

std::vector<Dock> ToolBar::allowedEdges() const
{
    std::vector<Dock> ut;
    if (edgeLeft_)
        ut.push_back(Dock::Left);
    if (edgeTop_)
        ut.push_back(Dock::Top);
    if (edgeRight_)
        ut.push_back(Dock::Right);
    if (edgeBottom_)
        ut.push_back(Dock::Bottom);
    return ut;
}

bool ToolBar::allowsEdge(Dock e) const
{
    switch (e) {
    case Dock::Left:   return edgeLeft_;
    case Dock::Top:    return edgeTop_;
    case Dock::Right:  return edgeRight_;
    case Dock::Bottom: return edgeBottom_;
    default:           return false;
    }
}

void ToolBar::setFloatingPosition(float x, float y)
{
    if (!std::isfinite(x) || !std::isfinite(y)) {
        gfx::log("ToolBar(%s): setFloatingPosition(%g, %g) — talen ska vara "
                 "ändliga; ignoreras",
                 name.c_str(), static_cast<double>(x), static_cast<double>(y));
        return;
    }
    floatPos_      = gfx::Pointf{x, y};
    floatPosKnown_ = true;
    if (DockHost* h = host())
        h->floatBoundsChanged(*this);   // ett öppet fönster flyttar dit
}

bool ToolBar::isVertical() const
{
    return !floating_ && Bar::isVertical();
}

void ToolBar::turnIfNeeded(bool wasVertical)
{
    if (wasVertical != isVertical())
        std::swap(width, height);
}

bool ToolBar::setProp(const std::string& key, const std::string& value)
{
    if (key == "text") {
        setText(value);
        return true;
    }
    if (key == "band") {
        int        b = 0;
        const auto r =
            std::from_chars(value.data(), value.data() + value.size(), b);
        if (r.ec != std::errc{} || r.ptr != value.data() + value.size() ||
            b < 0)
            return false;   // inget heltal ≥ 0 — opak rad
        setBand(b);
        return true;
    }
    if (key == "floating") {
        setFloating(propparse::parseBool(value));
        return true;
    }
    if (key == "floatable") {
        setFloatable(propparse::parseBool(value));
        return true;
    }
    if (key == "overflow") {
        setOverflow(propparse::parseBool(value));
        return true;
    }
    return Bar::setProp(key, value);
}

bool ToolBar::setTextToken(const std::string& key, const TrText& value)
{
    if (key == "text") {
        setText(value);
        return true;
    }
    return Bar::setTextToken(key, value);
}

DockHost* ToolBar::host() const
{
    if (auto* h = dynamic_cast<DockHost*>(parent()))
        return h;
    return floatHost_;
}

void ToolBar::notifyHost()
{
    if (DockHost* h = host())
        h->barChanged(*this);
}

void ToolBar::childAdded(Widget&)
{
    notifyHost();
}

void ToolBar::childRemoved(Widget&)
{
    notifyHost();
}

void ToolBar::childLayoutChanged(Widget&)
{
    notifyHost();
}

// Bara ett FLYTANDE fält: ett dockat ritas om med värdens fönster
// (`App::invalidateAll`), och banden räknas vid ritningen. Kroken körs också
// när fönstret öppnas — då postar den en synk som inte ändrar något.
void ToolBar::languageChanged()
{
    if (floating_)
        notifyHost();
}

// I ETT BAND: värdens DIREKTA barn med en kant, och inte flytande. Ett fält
// med `Dock::None`/`Fill` i en värd står i huvudytan och har inget grepp —
// det finns inget band att dra det mellan — och ett flytande fält, eller ett
// som ännu står i sitt flytande fönster, har sin rubrik i stället.
bool ToolBar::inBand() const
{
    return !floating_ && dynamic_cast<DockHost*>(parent()) &&
           (dock == Dock::Left || dock == Dock::Top || dock == Dock::Right ||
            dock == Dock::Bottom);
}

float ToolBar::gripLength(const Ctx& c) const
{
    return inBand() ? c.style->toolGripW : 0.0f;
}

// Greppet står i fältets BÖRJAN (vänster liggande, överst stående), i
// remsans innerhöjd — samma luft tvärs som barnen har.
gfx::Rectf ToolBar::gripRect(const Ctx& c, const gfx::Rectf& b) const
{
    const float g = gripLength(c);
    if (g <= 0.0f)
        return gfx::Rectf{};
    const float py = c.style->toolBarPadY;
    if (isVertical())
        return gfx::Rectf{b.x + py, b.y, std::max(0.0f, b.w - 2.0f * py),
                          std::min(g, b.h)};
    return gfx::Rectf{b.x, b.y + py, std::min(g, b.w),
                      std::max(0.0f, b.h - 2.0f * py)};
}

// DEN EGNA LÄNGDEN: greppet, luften i båda ändarna, de synliga barnen
// längs riktningen och luckorna mellan dem — flödets barn, så överflödsknappen
// räknas inte (den syns bara när fältet är kortare än det här). En separator
// är tunn längs baren i båda riktningarna (separator.h), så svaret gäller
// också för en riktning fältet inte står i än.
float ToolBar::ownLength(const Ctx& c, bool vertical, bool withGrip) const
{
    const auto& m       = *c.style;
    float       barnen  = 0.0f;
    int         synliga = 0;
    for (const Widget* ch : children()) {
        if (!inFlow(*ch))
            continue;
        const gfx::Sizef s = measure(*ch, c);
        barnen += vertical ? s.h : s.w;
        ++synliga;
    }
    if (synliga > 1)
        barnen += m.toolGap * static_cast<float>(synliga - 1);
    return (withGrip ? m.toolGripW : 0.0f) + 2.0f * m.toolBarPadX + barnen;
}

// AUTO-TJOCKLEKEN — `toolBarH`s egen definition
// (verktygsknappens naturliga höjd + luften på båda sidor, style.cpp)
// generaliserad: den HÖGSTA synliga verktygsknappen, aldrig under
// `toolBarH`. Med vanliga knappar är svaret exakt `toolBarH`; en knapp med
// `setIconSize(32)` gör fältet tjockare. Andra barn räknas inte — en
// dropdown i fältet ändrar ingenting. Samma höjd i båda riktningarna.
//
// IKONEN ger tjockleken, inte knappens hela mätta höjd: radhöjden är MÄTT
// per font och skala (17.29 logiska px vid 125 % på Windows, mot de 14.72
// `toolBarH` härleds ur), så en vanlig TEXTknapp skulle göra varje fält någon
// pixel tjockare än `toolBarH` — och tunnare igen när knappen döljs
// (pixel_gate, dock_gate). Ikonens sida är `ToolButton::content`s regel (satt
// storlek, annars storleksklassen, annars `iconFallback`), och med 16 är
// svaret exakt `toolBarH` i alla tre stilarna (style.cpp, härledningen).
float ToolBar::autoThickness(const Ctx& c) const
{
    float tjock = c.style->toolBarH;
    for (const Widget* ch : children()) {
        const auto* b = dynamic_cast<const ToolButton*>(ch);
        if (!b || !inFlow(*b) || !b->img().valid())
            continue;   // överflödsknappen står utanför flödet
        const float k    = b->iconSize() > 0.0f ? b->iconSize() : b->img().sizeClass();
        const float ikon = k > 0.0f ? k : c.style->iconFallback;
        tjock = std::max(tjock, ikon + 2.0f * (c.style->toolButtonPadY +
                                               c.style->toolBarPadY));
    }
    return tjock;
}

gfx::Sizef ToolBar::sizeFor(const Ctx& c, bool vertical, bool withGrip) const
{
    // De SATTA måtten läses i fältets nuvarande riktning: liggande är
    // `height` tjockleken och `width` längden, stående tvärtom.
    const bool  nu        = isVertical();
    const float tjockSatt = nu ? width : height;
    const float längdSatt = nu ? height : width;
    const float tjock     = tjockSatt > 0.0f ? tjockSatt : autoThickness(c);
    const float längd     = längdSatt > 0.0f ? längdSatt
                                             : ownLength(c, vertical, withGrip);
    return vertical ? gfx::Sizef{tjock, längd} : gfx::Sizef{längd, tjock};
}

gfx::Sizef ToolBar::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default:
        // Bar::contentSize med verktygsfältets egen default-tjocklek — samma
        // form som StatusBar och MenuBar — och den EGNA längden när ingen
        // är satt. En dockad remsa i ett vanligt fönster får ändå sin
        // längd av dockningen.
        return sizeFor(c, isVertical(), inBand());
    }
}

// Barnens yta = Panels (som redan räknar in scrollen — en överfull remsa
// går att hjulscrolla, precis som en vanlig Bar) minus greppet och stilens
// luft. Luften är LOGISKA px och ytan FYSISK, så omräkningen sker här på
// konsumtionssidan, som Panels egen scrollförskjutning. Klampad ≥ 0: en
// remsa som satts lägre än sin egen luft ger barnen noll höjd i stället
// för en negativ.
gfx::Rectf ToolBar::childArea(const Ctx& c) const
{
    const gfx::Rectf a  = Bar::childArea(c);
    const float      g  = gripLength(c) * c.scale;
    const float      px = c.style->toolBarPadX * c.scale;   // längs fältet
    const float      py = c.style->toolBarPadY * c.scale;   // tvärs
    if (isVertical())
        return gfx::Rectf{a.x + py, a.y + g + px,
                          std::max(0.0f, a.w - 2.0f * py),
                          std::max(0.0f, a.h - g - 2.0f * px)};
    return gfx::Rectf{a.x + g + px, a.y + py,
                      std::max(0.0f, a.w - g - 2.0f * px),
                      std::max(0.0f, a.h - 2.0f * py)};
}

float ToolBar::childGap(const Ctx& c) const
{
    return c.style->toolGap;
}

// Bars ritning, och GREPPET i en värd. Greppet har ett formspråk i alla tre
// familjerna, och det är ett BESLUT (separatorns skäl): det är STRUKTUR — ett
// ställe att ta i — och färgen (`border`) och måttet (`toolGripW`) är redan
// familjens.
void ToolBar::draw(const Ctx& c)
{
    Bar::draw(c);
    if (!inBand())
        return;
    switch (c.ui) {
    case UI::Glaze:
    case UI::Minimalistic:
    case UI::Slate:
    default:
        Draw::grip(c, gripRect(c, c.bounds()), !isVertical(),
                   c.theme->colors.border);
        break;
    }
}

// ---------------------------------------------------------- greppet

void ToolBar::mouseDown(const Ctx& c, float x, float y)
{
    gripPress_ = false;
    gripDrag_  = false;
    if (inBand() && isEnabled()) {
        const gfx::Rectf r = resolve(c);
        if (gripRect(c, Draw::bounds(c, r))
                .contains(Draw::localX(c, r, x), Draw::localY(c, r, y))) {
            gripPress_ = true;
            pressX_    = x;
            pressY_    = y;
            host()->dragBegin();   // ett avbrutet drags mål får inte stå kvar
            return;
        }
    }
    Bar::mouseDown(c, x, y);
}

// Ett tryck i greppet blir ett DRAG när pekaren rört sig mer än stilens
// tröskel; målet och indikatorn räknar värden per rörelse.
void ToolBar::mouseDrag(const Ctx& c, float x, float y)
{
    if (!gripPress_) {
        Bar::mouseDrag(c, x, y);
        return;
    }
    DockHost* h = host();
    if (!h)
        return;
    if (!gripDrag_) {
        if (!detail::pastDragThreshold(c, pressX_, pressY_, x, y))
            return;
        gripDrag_ = true;
    }
    h->toolDragMove(*this, x, y);
}

bool ToolBar::mouseUp(const Ctx& c, float x, float y)
{
    const bool tryck = gripPress_;
    const bool drag  = gripDrag_;
    gripPress_       = false;
    gripDrag_        = false;
    if (!tryck)
        return Bar::mouseUp(c, x, y);
    DockHost* h = host();
    return drag && h ? h->toolDragEnd(*this, x, y) : false;
}

// AVBROTTET: greppets tryck kommer aldrig att släppas —
// värden släcker platshållaren direkt, och ett buret drag i
// fältets fönster är värdens att avsluta. Ingen användarkod.
bool ToolBar::mouseCancel(gfx::MouseButton b)
{
    if (!gripPress_)
        return Bar::mouseCancel(b);
    gripPress_ = false;
    gripDrag_  = false;
    if (DockHost* h = host())
        h->itemPressLost(*this);
    return false;
}

// DUBBELKLICK PÅ GREPPET gör ett dockat fält flytande (VS)
// — bara i ett band, och när användaren får (`userFloatBar` prövar
// `isFloatable()`). Ett dubbelklick någon annanstans i remsan är Bars.
void ToolBar::mouseDoubleClick(const Ctx& c, float x, float y)
{
    if (inBand() && isEnabled()) {
        const gfx::Rectf r = resolve(c);
        if (gripRect(c, Draw::bounds(c, r))
                .contains(Draw::localX(c, r, x), Draw::localY(c, r, y))) {
            host()->userFloatBar(*this, true);
            return;
        }
    }
    Bar::mouseDoubleClick(c, x, y);
}

gfx::PointerShape ToolBar::cursorAt(const Ctx& c, float x, float y) const
{
    if (inBand()) {
        const gfx::Rectf r = resolve(c);
        if (gripRect(c, Draw::bounds(c, r))
                .contains(Draw::localX(c, r, x), Draw::localY(c, r, y)))
            return gfx::PointerShape::Move;
    }
    return Bar::cursorAt(c, x, y);
}

// ============================================================ ÖVERFLÖDET
// Kontraktet står i headern. Fältet räknar SNITTET per fråga, som
// flexen räknar sin rad: ingen lagrad layout, så en ändrad yta, ett nytt barn
// eller ett fokusbyte gäller vid nästa fråga.

namespace {

// Kontrollens FOKUSMÅL: den själv eller det första tabbstoppet i dess
// delträd, i fönstrets Tab-ordning — `focusOrder` är EN sanning för vad som
// kan få fokus (synligt, aktiverat, kompositernas stopp, de gömda barnen).
Widget* fokusmål(const Widget& w, const std::vector<Widget*>& ordning)
{
    for (Widget* kandidat : ordning)
        for (const Widget* p = kandidat; p; p = p->parent())
            if (p == &w)
                return kandidat;
    return nullptr;
}

// Kan kontrollen ta fokus när den är aktiverad? Den själv eller något synligt
// i dess delträd är fokuserbart. En inaktiverad kontroll får en släckt post,
// en som aldrig kan få fokus (en etikett) ingen.
bool kanTaFokus(const Widget& w)
{
    if (!w.isVisible())
        return false;
    if (w.focusable())
        return true;
    if (!w.showsChildren())
        return false;
    for (const Widget* b : w.children())
        if (kanTaFokus(*b))
            return true;
    return false;
}

} // namespace

void ToolBar::setOverflow(bool on)
{
    if (overflowOn_ == on)
        return;
    overflowOn_ = on;
    if (!on)
        overflow_->closeMenu();
    invalidate();
    notifyHost();   // bandets kortaste längd ändrades
}

bool ToolBar::inFlow(const Widget& child) const
{
    return &child != overflow_ && Bar::inFlow(child);
}

// Knappen är INREDNING (`TopBar`s fyra, talfältets fält): fältet håller en rå
// pekare till den, så den lossas aldrig utifrån.
bool ToolBar::canReleaseChild(const Widget& w) const
{
    return &w != overflow_ && Bar::canReleaseChild(w);
}

// FOKUS KOM IN I ELLER LÄMNADE ETT BARN: ett barn i överflödet visas så länge
// det har fokus (`overflowCut`), och då flyttar syskonen — som inte är dirty.
// Fältet ritas om; dess egen längd är densamma, så värden behöver inget veta.
void ToolBar::childFocusChanged(Widget&)
{
    if (overflowOn_)
        invalidate();
}

// Knappen följer fältets `enabled` — ett inaktiverat fält öppnar ingen meny,
// som det inte heller har något grepp.
void ToolBar::enabledChanged()
{
    if (isEnabled())
        overflow_->enable();
    else
        overflow_->disable();
    Bar::enabledChanged();
}

const Widget* ToolBar::focusedChild() const
{
    const AppWindow* f = appWindow();
    for (const Widget* w = f ? f->focusedWidget() : nullptr; w; w = w->parent())
        if (w->parent() == this)
            return w == overflow_ ? nullptr : w;
    return nullptr;
}

float ToolBar::flowLength(const Widget& w, const Ctx& c) const
{
    const gfx::Sizef s = measure(w, c);
    return (isVertical() ? s.h : s.w) * c.scale;
}

// SNITTET — tre pass över flödet och inga allokeringar (det frågas för varje
// barn som placeras): (1) hela längden och var det fokuserade barnet står,
// (2) de första som ryms före knappen, (3) med ett fokuserat barn bortom dem
// de första som ryms TILLSAMMANS med det. Ett prefix slutar aldrig på en
// separator — den följer med till överflödet.
ToolBar::OverflowCut ToolBar::overflowCut(const Ctx& c) const
{
    OverflowCut cut;
    if (!overflowOn_)
        return cut;
    const float   gap   = childGap(c) * c.scale;
    const float   yta   = isVertical() ? c.area.h : c.area.w;
    const Widget* fokus = focusedChild();

    float       total = 0.0f, fokusLängd = 0.0f;
    std::size_t n = 0, fokusIndex = 0;
    bool        fokusIFlödet = false;
    for (const Widget* ch : children()) {
        if (!inFlow(*ch))
            continue;
        const float l = flowLength(*ch, c);
        total += (n > 0 ? gap : 0.0f) + l;
        if (ch == fokus) {
            fokusIndex   = n;
            fokusLängd   = l;
            fokusIFlödet = true;
        }
        ++n;
    }
    // Ett överskott under en hel FYSISK pixel är avrundningen mellan två
    // räknevägar för samma längd (fältets egen och flexens) — inte ett fält
    // som inte ryms.
    if (n == 0 || std::floor(total - yta) <= 0.0f)
        return cut;
    cut.active = true;

    // Platsen före knappen: ytan minus knappen och luckan till den. `prefix`
    // svarar hur många av de första (högst `högst`) som ryms med `extra`
    // efter sig, och `säkra` hur många av dem som inte slutar på en separator.
    const float plats =
        std::max(0.0f, yta - c.style->toolOverflowW * c.scale - gap);
    const auto prefix = [&](float extra, std::size_t högst,
                            std::size_t& säkra) {
        float       upptaget = 0.0f;
        std::size_t k        = 0;
        säkra                = 0;
        for (const Widget* ch : children()) {
            if (!inFlow(*ch))
                continue;
            if (k >= högst)
                break;
            const float behov =
                upptaget + (k > 0 ? gap : 0.0f) + flowLength(*ch, c);
            if (behov + extra > plats)
                break;
            upptaget = behov;
            ++k;
            if (!dynamic_cast<const Separator*>(ch))
                säkra = k;
        }
        return k;
    };
    std::size_t       säkra = 0;
    const std::size_t ryms  = prefix(0.0f, n, säkra);
    cut.shown               = säkra;
    if (fokusIFlödet && fokusIndex >= ryms) {
        cut.revealed    = fokus;
        cut.revealedLen = std::min(fokusLängd, plats);
        prefix(gap + cut.revealedLen, ryms, säkra);
        cut.shown = säkra;
    }
    return cut;
}

// FLEXEN MED ÖVERFLÖDET. Ryms barnen är det Bars flex rakt av — knappen står
// utanför den (`inFlow`) och får en tom rekt i hörnet, som inte ritas
// (AppWindow; annars hade ikonens prickar synts där). Annars packas de
// som visas från början med sina mätta längder och luckan (inget växer —
// Bars egen regel när barnen inte ryms), det fokuserade sist i sin klampade
// längd, knappen i fältets ände och de dolda i en tom rekt. Tväraxeln är
// Bars (`Bar::placeChild` för samma barn), så den är densamma med och utan
// överflöde.
gfx::Rectf ToolBar::placeChild(const Widget& child, const Ctx& c) const
{
    const OverflowCut cut = overflowCut(c);
    const gfx::Rectf  tom{c.area.x, c.area.y, 0.0f, 0.0f};
    const bool        v = isVertical();
    if (&child == overflow_) {
        if (!cut.active)
            return tom;
        const float l = std::min(c.style->toolOverflowW * c.scale,
                                 v ? c.area.h : c.area.w);
        return v ? gfx::Rectf{c.area.x, c.area.y + c.area.h - l, c.area.w, l}
                 : gfx::Rectf{c.area.x + c.area.w - l, c.area.y, l, c.area.h};
    }
    if (!cut.active)
        return Bar::placeChild(child, c);

    const float gap    = childGap(c) * c.scale;
    float       pos    = v ? c.area.y : c.area.x;
    bool        första = true;
    std::size_t i      = 0;
    for (const Widget* ch : children()) {
        if (!inFlow(*ch))
            continue;
        if (!shows(cut, i++, *ch)) {
            if (ch == &child)
                return tom;
            continue;
        }
        if (!första)
            pos += gap;
        första        = false;
        const float l = ch == cut.revealed ? cut.revealedLen : flowLength(*ch, c);
        if (ch == &child) {
            gfx::Rectf r    = Bar::placeChild(child, c);   // tväraxeln
            (v ? r.y : r.x) = pos;
            (v ? r.h : r.w) = l;
            return r;
        }
        pos += l;
    }
    return tom;   // dolt, eller inte mitt barn
}

// DEN KORTASTE LÄNGDEN i ett band (`DockHost` krymper fälten bakifrån ned
// till den): greppet, luften och knappen — aldrig mer än den egna längden, och
// utan överflöde den egna (fältet krymper inte).
float ToolBar::minLength(const Ctx& c, bool vertical, bool withGrip) const
{
    const gfx::Sizef s    = sizeFor(c, vertical, withGrip);
    const float      egen = vertical ? s.h : s.w;
    if (!overflowOn_)
        return egen;
    const auto& m = *c.style;
    return std::min(egen, (withGrip ? m.toolGripW : 0.0f) +
                              2.0f * m.toolBarPadX + m.toolOverflowW);
}

// MENYN — byggd vid varje tryck som öppnar den, ur snittet i knappens Ctx
// (`c` — mina barns yta). FÖRST fyras de dolda rullgardinsknapparnas
// `onDropDown`: deras menyer står i överflödet som undermenyer, och en
// dynamisk meny fylls där, som när användaren trycker fram den. Handlerna får
// riva vad som helst — knapparna samlas med vakter före den första, och
// snittet räknas om efteråt. Därefter posterna i barnens ordning.
bool ToolBar::fillOverflowMenu(const Ctx& c)
{
    Menu& meny = overflow_->menu();
    meny.items.clear();
    const HandlerGuard mig(*overflow_);

    std::vector<std::pair<ToolButton*, HandlerGuard>> dynamiska;
    {
        const OverflowCut cut = overflowCut(c);
        std::size_t       i   = 0;
        for (Widget* ch : children()) {
            if (!inFlow(*ch) || shows(cut, i++, *ch))
                continue;
            auto* b = dynamic_cast<ToolButton*>(ch);
            if (b && b->isEnabled() && b->onDropDown && !b->menu().items.empty())
                dynamiska.emplace_back(b, HandlerGuard(*b));
        }
    }
    bool körde = false;
    for (auto& [b, vakt] : dynamiska) {
        if (!vakt.ok())
            continue;
        körde = true;
        b->fireEvent(b->onDropDown);   // vännen (tool_button.h): knappens vakt
        if (!mig.ok())
            return false;
    }
    if (körde)
        if (AppWindow* w = appWindow())
            w->invalidate();   // klickets garanti — en handler kan ha ändrat allt

    const OverflowCut cut = overflowCut(c);
    if (!cut.active)
        return true;
    AppWindow* const           fönster = appWindow();
    const std::vector<Widget*> ordning =
        fönster ? fönster->focusOrder() : std::vector<Widget*>{};
    std::size_t i = 0;
    for (Widget* ch : children()) {
        if (!inFlow(*ch) || shows(cut, i++, *ch))
            continue;
        if (dynamic_cast<Separator*>(ch)) {
            if (!meny.items.empty() && !meny.items.back().separator)
                meny.addSeparator();
            continue;
        }
        if (dynamic_cast<Spacer*>(ch))
            continue;
        if (auto* b = dynamic_cast<ToolButton*>(ch)) {
            meny.items.push_back(buttonItem(*b));
            continue;
        }
        MenuItem post;
        if (controlItem(*ch, ordning, post))
            meny.items.push_back(std::move(post));
    }
    while (!meny.items.empty() && meny.items.back().separator)
        meny.items.pop_back();
    return true;
}

// EN DOLD VERKTYGSKNAPP SOM POST. Texten är knappens, annars dess verktygstips
// (en ren ikonknapp); bilden, krysset och det inaktiva läget är knappens.
// Posten klickar knappen; med poster blir den en undermeny — en delad knapp
// först med sitt eget klick och en skiljelinje.
MenuItem ToolBar::buttonItem(ToolButton& b) const
{
    const HandlerGuard vakt(b);
    ToolButton* const  knapp = &b;

    MenuItem klick;
    klick.text      = b.text().empty() ? b.toolTipToken() : b.textToken();
    klick.icon      = b.img();
    klick.enabled   = b.isEnabled();
    klick.checkable = b.isCheckable() || b.isChecked();
    klick.checked   = b.isChecked();
    klick.onClick   = [vakt, knapp] {
        if (vakt.ok())
            knapp->performClick();
    };
    if (b.menu().items.empty())
        return klick;

    MenuItem undermeny;
    undermeny.text    = klick.text;
    undermeny.icon    = klick.icon;
    undermeny.enabled = klick.enabled;
    if (b.isSplit()) {
        undermeny.submenu.push_back(std::move(klick));
        undermeny.submenu.emplace_back().separator = true;
    }
    std::vector<int> stig;
    mirrorItems(b.menu().items, undermeny.submenu, b, vakt, stig);
    return undermeny;
}

// KNAPPENS POSTER SOM UNDERMENY: posten kopieras hel — text, bild, genväg och
// tillstånd (menyn visar en ögonblicksbild ändå) — utom handlern och
// undermenyn, och varje post aktiveras genom KNAPPENS egen meny med knappen
// som sender: dess kryss och radiogrupp ändras där, och postens handler är
// knappens. Stigen är den i knappens meny.
void ToolBar::mirrorItems(const std::vector<MenuItem>& från,
                          std::vector<MenuItem>& till, ToolButton& b,
                          const HandlerGuard& vakt, std::vector<int>& stig)
{
    ToolButton* const knapp = &b;
    for (std::size_t i = 0; i < från.size(); ++i) {
        const MenuItem& p = från[i];
        MenuItem        k = p;
        k.submenu.clear();
        k.onClick = nullptr;
        stig.push_back(static_cast<int>(i));
        if (!p.submenu.empty())
            mirrorItems(p.submenu, k.submenu, b, vakt, stig);
        else if (!p.separator)
            k.onClick = [vakt, knapp, s = stig] {
                if (vakt.ok())
                    knapp->menu().activatePath(s, knapp);
            };
        stig.pop_back();
        till.push_back(std::move(k));
    }
}

// EN INBÄDDAD KONTROLL SOM POST — bara det som kan ta fokus har en. Posten
// ger kontrollen (eller det första tabbstoppet i den) fokus, och ett barn i
// överflödet med fokus visas i fältet (`overflowCut`). Texten är kontrollens
// verktygstips, annars ramverkets "Kontroll"; en kontroll som inte kan få
// fokus just nu (inaktiverad) har en släckt post.
bool ToolBar::controlItem(Widget& w, const std::vector<Widget*>& ordning,
                          MenuItem& ut) const
{
    if (!kanTaFokus(w))
        return false;
    ut.text    = w.toolTip().empty() ? tr(str::ToolBarControl) : w.toolTipToken();
    ut.enabled = w.isEnabled() && fokusmål(w, ordning) != nullptr;
    ut.onClick = [vakt = HandlerGuard(w), kontroll = &w] {
        if (!vakt.ok())
            return;
        AppWindow* const f = kontroll->appWindow();
        if (Widget* mål = f ? fokusmål(*kontroll, f->focusOrder()) : nullptr)
            mål->focus();
    };
    return true;
}

} // namespace glaze
