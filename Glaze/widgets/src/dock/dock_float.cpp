// dock_float — se dock_float.h. Fönstren, kroppen och verktygsfältets
// rubrik; objektets tillstånd och flytten mellan träden äger `DockHost`
// (dock_host.cpp). Inga tal och inga färger: måtten ur `style()`/`c.style`,
// färgerna ur temat.

#include "dock_float.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <gfx/platform.h>

#include "glaze/app.h"
#include "glaze/draw.h"
#include "glaze/language.h"
#include "glaze/strings.h"
#include "glaze/widgets/dock_host.h"
#include "glaze/widgets/dock_panel.h"
#include "glaze/widgets/tool_bar.h"
#include "glaze/widgets/top_bar.h"

#include "dock_model.h"

namespace glaze {

namespace {

// Ett mått i LOGISKA px som heltal för fönstrets API — avrundat.
int heltal(float v)
{
    return static_cast<int>(std::lround(v));
}

} // namespace

// ------------------------------------------------------------ rubriken

ToolFloatHeader::ToolFloatHeader(DockFloatWindow& win) : win_(win) {}

gfx::Sizef ToolFloatHeader::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default:
        // Kroppen placerar rubriken (`placesChildren`), så bara höjden har
        // en mening: rubrikradens.
        return gfx::Sizef{width, c.style->dockHeaderH};
    }
}

gfx::Rectf ToolFloatHeader::closeRect(const Ctx& c, const gfx::Rectf& b) const
{
    const float h = std::min(c.style->dockHeaderH, b.h);
    return gfx::Rectf{std::max(b.x, b.x + b.w - h), b.y, std::min(h, b.w), h};
}

gfx::Rectf ToolFloatHeader::titleRect(const Ctx& c, const gfx::Rectf& b) const
{
    const auto&      m = *c.style;
    const gfx::Rectf x = closeRect(c, b);
    const float      v = b.x + m.dockHeaderPadX;
    return gfx::Rectf{v, b.y, std::max(0.0f, x.x - v), x.h};
}

ToolFloatHeader::Part ToolFloatHeader::partAt(const Ctx& c, float x,
                                              float y) const
{
    const gfx::Rectf r  = resolve(c);
    const gfx::Rectf b  = Draw::bounds(c, r);
    const float      lx = Draw::localX(c, r, x);
    const float      ly = Draw::localY(c, r, y);
    if (closeRect(c, b).contains(lx, ly))
        return Part::Close;
    if (b.contains(lx, ly))
        return Part::Header;
    return Part::None;
}

// Fältets `text`, och ramverkets "Verktygsfält" när den är tom — samma
// regel som fönstrets titel (`barTitle`).
std::string ToolFloatHeader::title() const
{
    const ToolBar* t = win_.bar();
    if (t && !t->text().empty())
        return t->text();
    const TrText reserv = tr(str::DockToolBar);
    return reserv.str();
}

// De fyra tillstånden — panelrubrikens recept (dock_header.cpp),
// och stängknappen är verktygsknappens platta knapp i varje familj:
//   vila       rubrikraden (Glaze/Slate `windowBg`, Minimalistic `panel`)
//              med en hårlinje under, titeln i `text`, korset i `textDim`
//   hover      plattan `buttonHover` (familjens form), korset i `text`
//   tryck      plattan `buttonPress` (Minimalistic: kanten i `accent`)
//   inaktiv    fältet är inaktiverat — titeln `textDim`, korset `border`,
//              inga plattor och ingen träff
// Ingen fokus: rubriken är chrome.
void ToolFloatHeader::draw(const Ctx& c)
{
    const gfx::Rectf b    = c.bounds();
    const auto&      m    = *c.style;
    const auto&      col  = c.theme->colors;
    const ToolBar*   t    = win_.bar();
    const bool       på   = t && t->isEnabled();
    const gfx::Rectf x    = closeRect(c, b);
    const gfx::Rectf pl   = Draw::inset(x, m.dockButtonInset);
    const float      g    = m.dockGlyph;
    const gfx::Rectf kors = {x.x + (x.w - g) / 2.0f, x.y + (x.h - g) / 2.0f,
                             g, g};
    const bool nere = på && pressed() && press_ == Part::Close && pressInside_;
    const bool het  = på && hovered() && hover_ == Part::Close && !pressed();
    auto linjeUnder = [&](gfx::Color f) {
        Draw::line(c, b.x, b.y + b.h, b.x + b.w, b.y + b.h,
                   Pen{f, m.borderWidth});
    };

    switch (c.ui) {
    // GLAZE — en YTA med BESTÄMD KANT: rubrikraden i
    // `windowBg` med fönsterkantens hårlinje `frame` under, och den tända
    // plattan laminerad med kontrollkanten — som panelrubrikens knappar.
    case UI::Glaze:
        Draw::rect(c, b, 0.0f, col.windowBg);
        linjeUnder(col.frame);
        if (het || nere) {
            Draw::rect(c, pl, m.cornerRadius,
                       nere ? col.buttonPress : col.buttonHover);
            if (het)
                Draw::laminate(c, pl, m.cornerRadius, col.sheen, m.sheenLower,
                               col.sheenHigh, m.sheenUpper);
            Draw::frame(c, pl, m.cornerRadius,
                        Pen{col.buttonBorder, m.borderWidth});
        }
        break;

    // MINIMALISTIC — "Outline": ytan är
    // `panel`, bara hårlinjen under rubriken, och den tända plattan är
    // fyllningen innanför en kant (`buttonBorder`, `accent` vid tryck).
    case UI::Minimalistic:
        Draw::rect(c, b, 0.0f, col.panel);
        linjeUnder(col.border);
        if (het || nere)
            Draw::rect(c, pl, m.cornerRadius,
                       nere ? col.buttonPress : col.buttonHover,
                       Pen{nere ? col.accent : col.buttonBorder,
                           m.borderWidth});
        break;

    // SLATE — en YTA: rubrikraden i `windowBg` med
    // kontrollkanten under, och den tända plattan utan kontur.
    case UI::Slate:
    default:
        Draw::rect(c, b, 0.0f, col.windowBg);
        linjeUnder(col.border);
        if (het || nere)
            Draw::rect(c, pl, m.cornerRadius,
                       nere ? col.buttonPress : col.buttonHover);
        break;
    }
    Draw::textElided(c, titleRect(c, b), title(), på ? col.text : col.textDim,
                     Align::Left);
    Draw::cross(c, kors,
                Pen{!på ? col.border : (het || nere) ? col.text : col.textDim,
                    m.borderWidth});
}

TrText ToolFloatHeader::toolTipAt(const Ctx& c, float x, float y) const
{
    if (partAt(c, x, y) == Part::Close)
        return tr(str::DockClose);
    return Widget::toolTipAt(c, x, y);
}

// Två sorters tryck, panelrubrikens: STÄNGKNAPPEN (plattan följer om pekaren
// står kvar) och YTAN (blir ett drag när pekaren rört sig mer än stilens
// tröskel — målet räknar värden).
void ToolFloatHeader::mouseDown(const Ctx& c, float x, float y)
{
    press_       = partAt(c, x, y);
    pressInside_ = true;
    drag_        = false;
    pressX_      = x;
    pressY_      = y;
    if (press_ == Part::Header)
        win_.host().dragBegin();   // ett avbrutet drags mål får inte stå kvar
    if (press_ == Part::Close)
        invalidate();
}

void ToolFloatHeader::mouseDrag(const Ctx& c, float x, float y)
{
    if (press_ == Part::Close) {
        const bool inne = partAt(c, x, y) == Part::Close;
        if (inne != pressInside_) {
            pressInside_ = inne;
            invalidate();
        }
        return;
    }
    ToolBar* t = win_.bar();
    if (press_ != Part::Header || !t || !t->isEnabled())
        return;
    if (!drag_) {
        if (!detail::pastDragThreshold(c, pressX_, pressY_, x, y))
            return;
        drag_ = true;
    }
    win_.host().toolHeaderDrag(*t, x, y);
}

bool ToolFloatHeader::mouseUp(const Ctx& c, float x, float y)
{
    const Part tryck = press_;
    const bool drag  = drag_;
    press_           = Part::None;
    drag_            = false;
    if (tryck == Part::Close)
        invalidate();
    ToolBar* t = win_.bar();
    if (!t)
        return false;
    if (drag)
        return win_.host().toolHeaderDragEnd(*t, x, y);
    if (tryck != Part::Close || !t->isEnabled() ||
        partAt(c, x, y) != Part::Close)
        return false;   // släppt utanför knappen — ett ångrat klick
    return win_.host().closeFloatingBar(*t);
}

// Hover: bara stängknappen tänds, och en rörelse som inte byter del ritar
// ingenting (regel 1).
void ToolFloatHeader::mouseMove(const Ctx& c, float x, float y)
{
    const Part p = partAt(c, x, y);
    if (p == hover_)
        return;
    const bool ritaOm = p == Part::Close || hover_ == Part::Close;
    hover_            = p;
    if (ritaOm)
        invalidate();
}

// DUBBELKLICK PÅ RUBRIKEN dockar fältet hem — sist i band `band()`
// vid `dock`.
void ToolFloatHeader::mouseDoubleClick(const Ctx& c, float x, float y)
{
    ToolBar* t = win_.bar();
    if (!t || !t->isEnabled() || partAt(c, x, y) != Part::Header)
        return;
    win_.host().userFloatBar(*t, false);
}

// TRYCKET AVBRÖTS — rubrikens drag och ✕:s tryck släpps
// (✕ ritas om, dess tryckta platta står inte kvar), och värden får beskedet:
// bärs fältet i fönstret utan övertagande (`holdCarry`) avslutas
// draget som avbrutet; under värdens övertagande är beskedet övertagandet
// självt (`DockHost::itemPressLost`). Ingen användarkod: värden postar sitt
// `onChange`.
bool ToolFloatHeader::mouseCancel(gfx::MouseButton)
{
    const bool stäng = press_ == Part::Close;
    press_           = Part::None;
    pressInside_     = false;
    drag_            = false;
    if (stäng)
        invalidate();
    if (ToolBar* t = win_.bar())
        win_.host().itemPressLost(*t);
    return false;
}

// ------------------------------------------------------------ kroppen

FloatBody::FloatBody(DockFloatWindow& win, bool withHeader)
    : win_(win), header_(win)
{
    dock = Dock::Fill;
    if (withHeader)
        adopt(header_);
}

// Panelen fyller ytan. Fältet står under rubriken, som har rubrikradens höjd
// (fysiska px här — `c.area` är fysisk).
gfx::Rectf FloatBody::placeChild(const Widget& child, const Ctx& c) const
{
    if (header_.parent() != this)
        return c.area;
    const float h = std::min(c.style->dockHeaderH * c.scale, c.area.h);
    if (&child == &header_)
        return gfx::Rectf{c.area.x, c.area.y, c.area.w, h};
    return gfx::Rectf{c.area.x, c.area.y + h, c.area.w, c.area.h - h};
}

void FloatBody::draw(const Ctx&)
{
    win_.requestSync();
}

gfx::Sizef FloatBody::contentSize(const Ctx&) const
{
    // Ingen autostorlek — kroppen fyller fönstret (`Dock::Fill`).
    return gfx::Sizef{width, height};
}

// Objektet visades eller doldes — fönstret följer, i pump-svansen: kroken
// körs mitt i en sättare (stängknappens `hide()` i fönstrets egen händelse).
void FloatBody::childVisibilityChanged(Widget& child)
{
    if (&child == &header_)
        return;
    const std::weak_ptr<char> liv = lifeToken();
    glaze::app().post([this, liv] {
        if (!liv.expired())
            win_.syncVisible();
    });
}

// ------------------------------------------------------------ fönstret

// Storleken sätts när fönstret öppnas (`open`) — konstruktorns är förvalet.
DockFloatWindow::DockFloatWindow(DockHost& host, DockPanel& panel)
    : AppWindow(panel.textToken()), body(*this, false), host_(host),
      panel_(&panel), life_(panel.lifeToken())
{
    setUp();
}

DockFloatWindow::DockFloatWindow(DockHost& host, ToolBar& bar)
    : AppWindow(bar.textToken()), body(*this, true), host_(host), bar_(&bar),
      life_(bar.lifeToken())
{
    setUp();
    setTitle(barTitle());
}

DockFloatWindow::~DockFloatWindow() = default;

void DockFloatWindow::setUp()
{
    // EGET chrome, och objektets rubrik ÄR titelraden där fönster går att
    // placera. Annars (Wayland) är namnlisten fönstrets enda flytthandtag och
    // står kvar.
    setChrome(gfx::Chrome::Custom);
    if (DockHost::isFloatDragSupported()) {
        if (TopBar* t = topBar())
            t->hide();
        appIcon.hide();
    }
    setMinimizeBox(false);
    setMaximizeBox(false);
    setShowInTaskbar(false);
    // AKTIVERINGEN: panelens fönster stjäl inte
    // fokus vid sin första visning, fältets aldrig.
    if (panel_) {
        showInactive_ = true;
    } else {
        noActivate_ = true;
        toolWindow_ = true;
    }
    root().adopt(body);
    onClose = [this](Widget*, CloseArgs& e) {
        // En KASKAD (ägaren eller någon längre upp stänger — skälet säger det):
        // inget veto, och objektets tillstånd rörs inte — en
        // `layout()` i huvudfönstrets `onClose` säger fortfarande "flytande,
        // öppen".
        if (e.reason == CloseReason::OwnerClosing)
            return;
        // FÖNSTRETS EGEN stängning (Alt+F4, namnlisten på Wayland) betyder
        // att OBJEKTET stängs — som panelens stängknapp och fältets ✕.
        // Fönstret lever vidare, dolt, så objektet kan visas igen.
        e.cancel = true;
        if (DockPanel* p = panel())
            host_.closeFloatingPanel(*p);
        else if (ToolBar* t = bar())
            host_.closeFloatingBar(*t);
    };
}

DockPanel* DockFloatWindow::panel() const
{
    return life_.expired() ? nullptr : panel_;
}

ToolBar* DockFloatWindow::bar() const
{
    return life_.expired() ? nullptr : bar_;
}

Widget* DockFloatWindow::item() const
{
    if (life_.expired())
        return nullptr;
    return panel_ ? static_cast<Widget*>(panel_) : static_cast<Widget*>(bar_);
}

TrText DockFloatWindow::barTitle() const
{
    const ToolBar* t = bar();
    if (t && !t->text().empty())
        return t->textToken();
    return tr(str::DockToolBar);
}

// Ägarens genvägar (`tryShortcut`, publik —
// en apps egna verktygsfönster gör likadant). Ett tecken i panelens fält når
// aldrig hit: text vinner över genvägen före reserven.
bool DockFloatWindow::acceleratorFallback(gfx::Key k, std::uint32_t mods)
{
    AppWindow* o = owner();
    return o && o->isOpen() && o->tryShortcut(k, mods);
}

void DockFloatWindow::ownerVisibilityChanged(bool)
{
    syncVisible();
}

void DockFloatWindow::requestSync()
{
    host_.checkCarry();
    if (host_.floatsNeedSync())
        host_.postFloatSync();
}

// Den pekare kroppen håller jämförs bara — under objektets destruktor lever
// livstecknet ännu, men delträdet får inte röras (ett lånat barn kan redan
// vara dött).
void DockFloatWindow::itemDestroyed()
{
    body.release(panel_ ? static_cast<Widget*>(panel_)
                        : static_cast<Widget*>(bar_),
                 false);
    forgetWidgets();
    if (isOpen() && !isHidden())
        hide();
}

void DockFloatWindow::syncVisible()
{
    if (!isOpen())
        return;
    const Widget*    w   = item();
    const AppWindow* o   = owner();
    const bool       ska = w && w->isVisible() && o && o->isOpen() &&
                     !o->isHidden();
    if (ska && isHidden()) {
        if (bar())
            fitToolBar();
        show();
    } else if (!ska && !isHidden()) {
        hide();
    }
}

void DockFloatWindow::open(AppWindow& ägare,
                           std::optional<gfx::Pointf> placering)
{
    if (isOpen() || life_.expired())
        return;
    const Style& st     = style();
    gfx::Rectf   r{};
    bool         harPos = false;
    if (panel_) {
        r      = panel_->floatBounds_;
        harPos = panel_->floatPosKnown_;
    } else {
        r.x    = bar_->floatPos_.x;
        r.y    = bar_->floatPos_.y;
        harPos = bar_->floatPosKnown_;
    }
    // ETT FÖNSTER SOM STÄNGTS MED SIN ÄGARE (kaskaden) öppnas igen där det
    // stod — kortet `geometry()` läser efter stängningen.
    if (opened_) {
        const WindowGeometry g = geometry();
        if (g.hasPosition) {
            r.x    = static_cast<float>(g.x);
            r.y    = static_cast<float>(g.y);
            harPos = true;
        }
        r.w = contentSize().w;
        r.h = contentSize().h;
    }
    if (panel_) {
        // STORLEKEN — panelens flytande innehållsyta, annars dess föredragna
        // dockade mått (AUTO = `dockDefaultSize`).
        const DockPanel& p = *panel_;
        const float      b = r.w > 0.0f ? r.w
                             : detail::panelSize(p.width) > 0.0f
                                 ? detail::panelSize(p.width)
                                 : st.dockDefaultSize;
        const float h = r.h > 0.0f ? r.h
                        : detail::panelSize(p.height) > 0.0f
                            ? detail::panelSize(p.height)
                            : st.dockDefaultSize;
        setContentSize(std::max(1, heltal(b)), std::max(1, heltal(h)));
    } else {
        // FÄLTETS FASTA STORLEK — ur dess egna mått, mätt i värdens Ctx
        // tills fönstret självt är öppet.
        fitToolBar();
    }
    // PLATSEN — ett BURET objekts i placeringsrymden (fönstret skapas där
    // bärandet står, utan avrundade logiska px och utan klämning — fönstret
    // som följer pekaren får stå delvis utanför skärmen). Annars i LOGISKA px
    // genom fönstrets API, där positioner
    // finns, och annars mitt över ägaren; på Wayland placerar kompositören,
    // och en sparad plats sägs en gång.
    const bool positioner = DockHost::isFloatDragSupported();
    if (placering && positioner) {
        setStartPosition(StartPosition::Default);
        initX_ = static_cast<int>(std::lround(placering->x));
        initY_ = static_cast<int>(std::lround(placering->y));
    } else if (harPos && positioner) {
        setLocation(heltal(r.x), heltal(r.y));
    } else {
        setStartPosition(StartPosition::CenterOwner);
        static bool sagt = false;
        if (harPos && !sagt) {
            sagt = true;
            gfx::log("DockHost(%s): '%s' — ett flytande fönsters sparade "
                     "plats tillämpas inte — fönster har ingen skärmposition "
                     "här (kompositören placerar); storleken gäller",
                     host_.name.c_str(), item()->name.c_str());
        }
    }
    // PANELENS GRÄNSER — innehållsytans begäran är redan upplöst
    // (`setContentSize`), så chromet går att mäta före öppningen. (Fältets
    // fasta gränser satte `fitToolBar`.)
    if (panel_)
        applyLimits();
    // DOLT när ägaren är dold eller objektet stängt (dold start — `onLoad`
    // fyras, `onShow` först när fönstret visas).
    if (ägare.isHidden() || !item()->isVisible())
        hide();
    opened_ = true;
    show(ägare);
}

void DockFloatWindow::applyLimits()
{
    const DockPanel* p = panel();
    if (!p)
        return;
    const gfx::Sizef chrome{clientSize().w - contentSize().w,
                            clientSize().h - contentSize().h};
    const gfx::Sizef mn = p->minimumSize();
    const gfx::Sizef mx = p->maximumSize();
    setMinimumSize(mn.w > 0.0f ? heltal(mn.w + chrome.w) : 0,
                   mn.h > 0.0f ? heltal(mn.h + chrome.h) : 0);
    setMaximumSize(mx.w > 0.0f ? heltal(mx.w + chrome.w) : 0,
                   mx.h > 0.0f ? heltal(mx.h + chrome.h) : 0);
}

bool DockFloatWindow::wantedBarSize(gfx::Sizef& ut) const
{
    const ToolBar* t = bar();
    if (!t)
        return false;
    const Widget* mätare = isOpen() ? static_cast<const Widget*>(&body)
                                    : static_cast<const Widget*>(&host_);
    const AppWindow* f = mätare->appWindow();
    if (!f || !f->isOpen())
        return false;
    const Ctx        c = mätare->behaviorCtx();
    const auto&      m = *c.style;
    const gfx::Sizef s = t->sizeFor(c, false, false);
    ut = gfx::Sizef{std::max(s.w, m.dockHeaderPadX + m.dockHeaderH),
                    m.dockHeaderH + s.h};
    return true;
}

bool DockFloatWindow::fitWanted() const
{
    gfx::Sizef s{};
    return wantedBarSize(s) &&
           (!fitKnown_ || s.w != fitted_.w || s.h != fitted_.h);
}

// Gränserna LYFTS först: en fast storlek (min == max) hade annars hållit kvar
// den gamla medan innehållsytan sätts. Sedan innehållsytan, och sist min ==
// max ur den uppmätta klientytan (chromet räknat — namnlisten på Wayland).
// Det som jämförs nästa gång är det ÖNSKADE måttet, inte det avrundade, så en
// halv pixel aldrig passar om fönstret i en slinga.
void DockFloatWindow::fitToolBar()
{
    gfx::Sizef s{};
    if (!wantedBarSize(s) ||
        (fitKnown_ && s.w == fitted_.w && s.h == fitted_.h))
        return;
    fitted_   = s;
    fitKnown_ = true;
    setMinimumSize(0, 0);
    setMaximumSize(0, 0);
    setContentSize(std::max(1, heltal(s.w)), std::max(1, heltal(s.h)));
    const int b = heltal(clientSize().w);
    const int h = heltal(clientSize().h);
    setMinimumSize(b, h);
    setMaximumSize(b, h);
}

void DockFloatWindow::itemChanged()
{
    if (DockPanel* p = panel()) {
        setTitle(p->textToken());
        applyLimits();
        return;
    }
    if (!bar())
        return;
    // Bara en VERKLIG ändring ritar om rubriken: synken kommer också medan en
    // flytt väntar på en popup (varje ritning frågar igen), och en ovillkorlig
    // omritning hade då snurrat pumpen (regel 1).
    const TrText t = barTitle();
    if (!(titleToken() == t)) {
        setTitle(t);
        body.header().invalidate();
    }
    fitToolBar();
}

void DockFloatWindow::applyBounds()
{
    if (!isOpen() || life_.expired())
        return;
    const bool positioner = DockHost::isFloatDragSupported();
    if (bar_) {
        if (bar_->floatPosKnown_ && positioner)
            setLocation(heltal(bar_->floatPos_.x), heltal(bar_->floatPos_.y));
        return;   // storleken är fältets egen (`fitToolBar`)
    }
    const gfx::Rectf r = panel_->floatBounds_;
    if (r.w > 0.0f && r.h > 0.0f)
        setContentSize(heltal(r.w), heltal(r.h));
    if (panel_->floatPosKnown_ && positioner)
        setLocation(heltal(r.x), heltal(r.y));
}

// Ett ÖPPET fönster svarar med OS-fönstret också FÖRE sin första visning (en
// dold start under en dold ägare): `geometry()` svarar då med begäran,
// men värden ställer själv OS-fönstret — bärandet flyttar det med
// `setPosition` — så begäran är inte platsen, och `layout()` skulle skriva `-`.
gfx::Rectf DockFloatWindow::bounds(bool& hasPosition) const
{
    const WindowGeometry g = isOpen() ? liveGeometry() : geometry();
    const gfx::Sizef     c = contentSize();
    hasPosition            = g.hasPosition && opened_;
    return gfx::Rectf{static_cast<float>(g.x), static_cast<float>(g.y), c.w,
                      c.h};
}

// ------------------------------------------------------------ posten

namespace detail {

DockFloat::DockFloat()  = default;
DockFloat::~DockFloat() = default;

Widget* DockFloat::item() const
{
    return panel ? static_cast<Widget*>(panel) : static_cast<Widget*>(bar);
}

gfx::Pointf toClientOf(const gfx::Window& w, gfx::Pointf screen)
{
    const gfx::Pointf o  = w.clientToScreen(0.0f, 0.0f);
    const gfx::Pointf e  = w.clientToScreen(1.0f, 1.0f);
    const float       sx = e.x - o.x;
    const float       sy = e.y - o.y;
    if (sx == 0.0f || sy == 0.0f)
        return gfx::Pointf{std::numeric_limits<float>::quiet_NaN(),
                           std::numeric_limits<float>::quiet_NaN()};
    return gfx::Pointf{(screen.x - o.x) / sx, (screen.y - o.y) / sy};
}

} // namespace detail

} // namespace glaze
