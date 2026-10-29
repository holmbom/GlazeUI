// DockPanel — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER: ytan, rubrikraden med titel och knappar ritas av den delade
// panelrubriken (`detail::drawPanelHeader`, ../dock/dock_header.h — samma kod
// som den fria panelens), och här står knapparnas läge och en fokusring när
// den hopfällda panelen är ett tabbstopp (utan värd). Måtten kommer ur
// stilen (`dock*`), färgerna ur temat — inga tal och inga färger i den här
// filen. Geometrin bor i de privata `*Rect`-metoderna, som ritningen,
// containerkrokarna och musvägen alla läser. I en värd är det VÄRDEN som
// placerar panelen, ritar dess handtag och flik, visar den som overlay och
// räknar ut var ett rubrikdrag hamnar (dock_host.cpp); panelen ritar bara
// sig själv.

#include "glaze/widgets/dock_panel.h"

#include <algorithm>
#include <cmath>

#include <gfx/platform.h>

#include "glaze/app_window.h"
#include "glaze/draw.h"
#include "glaze/language.h"
#include "glaze/strings.h"
#include "glaze/widgets/dock_host.h"

#include "../dock/dock_header.h"
#include "../dock/dock_model.h"
#include "../prop_parse.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("dockpanel", DockPanel)

DockPanel::DockPanel(TrText t) : text_(std::move(t))
{
    dock = Dock::Left;
    // Menyn stängdes (alla vägar: ett val, Escape, ett klick utanför) — ▾
    // släpps. Bara när INGEN kedja lever: en gammal kedja som rivs synkront
    // när en ny öppnas får inte släcka den nya (verktygsknappens regel).
    menu_.onChainClosed = [this] {
        if (menuOpen_ && !menu_.isOpen()) {
            menuOpen_ = false;
            invalidate();
        }
    };
}

// Bara en panel som bor i ett av värdens flytande fönster (`floatHost_`) har
// något att säga: en dockad är värdens vanliga barn, och en värd som rivs
// först har redan nollat pekaren. Panelens lånade barn kan vara döda — värden
// rör ingenting i delträdet.
DockPanel::~DockPanel()
{
    if (floatHost_)
        floatHost_->floatingItemDestroyed(*this);
}

bool DockPanel::setProp(const std::string& key, const std::string& value)
{
    if (key == "text") {
        setText(value);
        return true;
    }
    if (key == "collapsed") {
        setCollapsed(propparse::parseBool(value));
        return true;
    }
    if (key == "pinned") {
        setPinned(propparse::parseBool(value));
        return true;
    }
    if (key == "closable") {
        setClosable(propparse::parseBool(value));
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
    if (key == "img") {
        // "lista/nyckel" — samma par som glaze::img (Buttons form). Miss ⇒
        // ogiltigt handtag (loggas av img, första gången) = ingen ikon.
        AssetHandle h;
        if (!propparse::parseImg(value, h))
            return false;
        setImg(h);
        return true;
    }
    return Widget::setProp(key, value);   // basen SIST — konventionen
}

// Titeln står i handtaget och fliken också — värden ritas om med panelen,
// och en flytande panels fönster får titeln (`panelChanged`).
void DockPanel::setText(TrText t)
{
    if (text_ == t)
        return;
    text_ = std::move(t);
    invalidate();
    if (DockHost* h = host())
        h->panelChanged(*this);
}

void DockPanel::setImg(ImageSource img)
{
    if (img_ == img)
        return;
    img_ = std::move(img);
    layoutChanged();   // ett liggande handtag blir bredare med en ikon
}

// Tjockleken ändras, alltså flyttar syskonen — och de är inte dirty
// (`relayoutSiblings`, samma väg som `hide()`/`show()` tar). I en värd blir
// panelen ett handtag eller ett handtag en panel — eller, onålad, en overlay.
void DockPanel::setCollapsed(bool on)
{
    if (collapsed_ == on)
        return;
    if (floating_) {
        gfx::log("DockPanel(%s): setCollapsed — en flytande panel är utfälld "
                 "(ignoreras)",
                 name.c_str());
        return;
    }
    const bool var = collapsed_;
    collapsed_     = on;
    stateChanged(var, pinned_);
}

// KNAPPNÅLEN: onålad är infälld — en öppen overlay är ett drifttillstånd
// som bara ett handtagsklick, `activate()` eller `setCollapsed(false)` ger.
// `setPinned(false)` på en öppen overlay fäller alltså in den.
void DockPanel::setPinned(bool on)
{
    if (pinned_ == on && (on || collapsed_))
        return;
    if (floating_) {
        gfx::log("DockPanel(%s): setPinned — en flytande panel är nålad "
                 "(ignoreras)",
                 name.c_str());
        return;
    }
    const bool varC = collapsed_;
    const bool varP = pinned_;
    pinned_ = on;
    if (!on)
        collapsed_ = true;
    stateChanged(varC, varP);
}

namespace {

// Ett mått per axel: ändligt och inte negativt (0 = ingen gräns).
bool giltigtMått(float v)
{
    return std::isfinite(v) && v >= 0.0f;
}

} // namespace

void DockPanel::setMinimumSize(float w, float h)
{
    if (!giltigtMått(w) || !giltigtMått(h)) {
        gfx::log("DockPanel(%s): setMinimumSize(%g, %g) — måtten ska vara "
                 "ändliga och >= 0 (0 = ingen gräns); ignoreras",
                 name.c_str(), static_cast<double>(w),
                 static_cast<double>(h));
        return;
    }
    if (minSize_.w == w && minSize_.h == h)
        return;
    minSize_ = {w, h};
    layoutChanged();
}

void DockPanel::setMaximumSize(float w, float h)
{
    if (!giltigtMått(w) || !giltigtMått(h)) {
        gfx::log("DockPanel(%s): setMaximumSize(%g, %g) — måtten ska vara "
                 "ändliga och >= 0 (0 = ingen gräns); ignoreras",
                 name.c_str(), static_cast<double>(w),
                 static_cast<double>(h));
        return;
    }
    if (maxSize_.w == w && maxSize_.h == h)
        return;
    maxSize_ = {w, h};
    layoutChanged();
}

void DockPanel::setClosable(bool on)
{
    if (closable_ == on)
        return;
    closable_ = on;
    invalidate();   // rubrikens knappar flyttar — inget annat
}

// FLYTANDE: TILLSTÅNDET gäller direkt — `isFloating()` och värdens
// `layout()` svarar redan — och värden flyttar panelen mellan fönstren i
// pumpens svans. En flytande panel är nålad och utfälld (dess fönster har
// ingen knappnål), och en öppen rubrikmeny stängs.
void DockPanel::setFloating(bool on)
{
    if (floating_ == on)
        return;
    floating_ = on;
    if (on) {
        pinned_    = true;
        collapsed_ = false;
    }
    closeMenu();
    invalidate();
    relayoutSiblings();
    if (DockHost* h = host())
        h->panelFloatingChanged(*this);
}

void DockPanel::setFloatable(bool on)
{
    if (floatable_ == on)
        return;
    floatable_ = on;
    invalidate();
}

void DockPanel::setFloatingBounds(float x, float y, float w, float h)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !giltigtMått(w) ||
        !giltigtMått(h)) {
        gfx::log("DockPanel(%s): setFloatingBounds(%g, %g, %g, %g) — talen "
                 "ska vara ändliga och storleken >= 0; ignoreras",
                 name.c_str(), static_cast<double>(x), static_cast<double>(y),
                 static_cast<double>(w), static_cast<double>(h));
        return;
    }
    floatBounds_   = gfx::Rectf{x, y, w, h};
    floatPosKnown_ = true;
    if (DockHost* f = host())
        f->floatBoundsChanged(*this);   // ett öppet fönster flyttar dit
}

void DockPanel::activate()
{
    if (DockHost* h = host()) {
        h->activatePanel(*this);
        return;
    }
    show();
    // Utan värd finns ingen overlay att fälla ut i — panelen fästs och
    // fälls ut.
    pinAndExpand();
}

void DockPanel::pinAndExpand()
{
    const bool varC = collapsed_;
    const bool varP = pinned_;
    pinned_    = true;
    collapsed_ = false;
    if (varC || !varP)
        stateChanged(varC, varP);
}

// Värden är föräldern — eller, medan panelen bor i sitt flytande fönster,
// den som äger fönstret (`floatHost_`).
DockHost* DockPanel::host() const
{
    if (auto* h = dynamic_cast<DockHost*>(parent()))
        return h;
    return floatHost_;
}

void DockPanel::layoutChanged()
{
    invalidate();
    relayoutSiblings();
    if (DockHost* h = host())
        h->panelChanged(*this);
}

void DockPanel::stateChanged(bool wasCollapsed, bool wasPinned)
{
    invalidate();
    relayoutSiblings();
    if (DockHost* h = host())
        h->panelStateChanged(*this, wasCollapsed, wasPinned);
}

bool DockPanel::pinShown() const
{
    return host() ? pinned_ : !collapsed_;
}

bool DockPanel::focusable() const
{
    return collapsed_ && !host();
}

bool DockPanel::showsChildren() const
{
    if (const DockHost* h = host())
        return h->showsPanel(*this);
    return !collapsed_;
}

// ---------------------------------------------------------- geometrin

float DockPanel::stripThickness(const Ctx& c, bool standing) const
{
    if (collapsed_)
        return c.style->dockHeaderH;
    // Samma regel som i en värd (`detail::panelSize`): ett mått som inte är
    // ett ändligt tal > 0 är AUTO.
    const float satt = detail::panelSize(standing ? width : height);
    return satt > 0.0f ? satt : c.style->dockDefaultSize;
}

// Hopfälld vid en STÅENDE kant är panelen UTAN värd en smal remsa: hela den
// är rubriken, och knapparna står staplade överst. (I en värd är en infälld
// panel ett handtag och placeras inte alls.)
bool DockPanel::standingStrip() const
{
    return collapsed_ && !host() && (dock == Dock::Left || dock == Dock::Right);
}

gfx::Rectf DockPanel::headerRect(const Ctx& c, const gfx::Rectf& b) const
{
    if (standingStrip())
        return b;
    return gfx::Rectf{b.x, b.y, b.w, std::min(c.style->dockHeaderH, b.h)};
}

int DockPanel::buttonCount() const
{
    return (closable_ ? 1 : 0) + (floating_ ? 1 : 2);
}

// Knapparna är KVADRATER lika höga som raden, längst till höger med
// stängknappen ytterst (Windows/Linux-ordningen — fönsterknapparnas regel:
// stäng hör till ytterkanten), knappnålen innanför och menyknappen innerst.
// Staplade uppifrån i en stående remsa (knappnålen överst — den fäller ut).
// Utan stängknapp (`setClosable(false)`) flyttar de andra ut ett steg.
gfx::Rectf DockPanel::slotRect(const Ctx& c, const gfx::Rectf& b,
                               int slot) const
{
    const float h = c.style->dockHeaderH;
    if (standingStrip())
        return gfx::Rectf{b.x, b.y + static_cast<float>(slot) * h, b.w, h};
    return gfx::Rectf{std::max(b.x, b.x + b.w - static_cast<float>(slot + 1) * h),
                      b.y, h, h};
}

gfx::Rectf DockPanel::closeRect(const Ctx& c, const gfx::Rectf& b) const
{
    if (!closable_)
        return gfx::Rectf{};
    return slotRect(c, b, standingStrip() ? 1 : 0);
}

gfx::Rectf DockPanel::pinRect(const Ctx& c, const gfx::Rectf& b) const
{
    if (floating_)
        return gfx::Rectf{};   // en flytande panel har ingen knappnål
    if (standingStrip())
        return slotRect(c, b, 0);
    return slotRect(c, b, closable_ ? 1 : 0);
}

gfx::Rectf DockPanel::menuRect(const Ctx& c, const gfx::Rectf& b) const
{
    return slotRect(c, b, buttonCount() - 1);
}

gfx::Rectf DockPanel::titleRect(const Ctx& c, const gfx::Rectf& b) const
{
    if (standingStrip())
        return gfx::Rectf{b.x, b.y, 0.0f, 0.0f};   // ingen roterad text
    const auto& m = *c.style;
    return gfx::Rectf{b.x + m.dockHeaderPadX, b.y,
                      std::max(0.0f, b.w -
                                         static_cast<float>(buttonCount()) *
                                             m.dockHeaderH -
                                         m.dockHeaderPadX),
                      std::min(m.dockHeaderH, b.h)};
}

// Innehållet under rubriken — tomt när panelen är hopfälld UTAN värd. I en
// värd uttrycks infällningen av PLACERINGEN (en tom yta), så innehållet
// följer den yta värden ger: en panel som glider in efter knappnålen har kvar
// sitt innehåll tills den är borta.
gfx::Rectf DockPanel::bodyRect(const Ctx& c, const gfx::Rectf& b) const
{
    if (collapsed_ && !host())
        return gfx::Rectf{b.x, b.y + b.h, b.w, 0.0f};
    const float h = std::min(c.style->dockHeaderH, b.h);
    return gfx::Rectf{b.x, b.y + h, b.w, b.h - h};
}

gfx::Rectf DockPanel::partRect(const Ctx& c, const gfx::Rectf& b,
                               Part p) const
{
    switch (p) {
    case Part::Menu:  return menuRect(c, b);
    case Part::Pin:   return pinRect(c, b);
    case Part::Close: return closeRect(c, b);
    default:          return gfx::Rectf{};
    }
}

gfx::Rectf DockPanel::childArea(const Ctx& c) const
{
    const gfx::Rectf r = resolve(c);
    return Draw::project(c, r, bodyRect(c, Draw::bounds(c, r)));
}

DockPanel::Part DockPanel::partAt(const Ctx& c, float x, float y) const
{
    const gfx::Rectf r  = resolve(c);
    const gfx::Rectf b  = Draw::bounds(c, r);
    const float      lx = Draw::localX(c, r, x);
    const float      ly = Draw::localY(c, r, y);
    for (const Part p : {Part::Pin, Part::Close, Part::Menu})
        if (partRect(c, b, p).contains(lx, ly))
            return p;
    if (headerRect(c, b).contains(lx, ly))
        return Part::Header;
    return Part::None;
}

gfx::Sizef DockPanel::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // AXELREGELN, med en skillnad: HOPFÄLLD vinner över ett satt mått
        // längs dockningsaxeln (måttet står kvar och gäller igen när panelen
        // fälls ut). Tjockleken beror aldrig på `c.area` längs skäraxeln —
        // kontraktet för en dockad widget (widget.h). Utan dockningskant
        // (`None`/`Fill`) fälls panelen ihop i höjdled. (I en värd placerar
        // värden panelen och frågar aldrig det här.)
        const bool står = dock == Dock::Left || dock == Dock::Right;
        gfx::Sizef s{width, height};
        if (står)
            s.w = stripThickness(c, true);
        else if (collapsed_ || dock == Dock::Top || dock == Dock::Bottom)
            s.h = stripThickness(c, false);
        if (s.w <= 0.0f) s.w = c.style->dockDefaultSize;
        if (s.h <= 0.0f) s.h = c.style->dockDefaultSize;
        return s;
    }
    }
}

// Rubrikknapparnas tooltip säger vad ett tryck GÖR: knappnålen "Dölj
// automatiskt" på en fäst panel och "Fäst" på en som döljs automatiskt,
// stängknappen "Stäng" och menyknappen "Fönsterläge" (VS:s Window Position).
TrText DockPanel::toolTipAt(const Ctx& c, float x, float y) const
{
    switch (partAt(c, x, y)) {
    case Part::Pin:   return tr(pinShown() ? str::DockAutoHide : str::DockPin);
    case Part::Close: return tr(str::DockClose);
    case Part::Menu:  return tr(str::DockPosition);
    default:          return Widget::toolTipAt(c, x, y);
    }
}

// ---------------------------------------------------------- ritningen

bool DockPanel::partHot(Part p) const
{
    return isEnabled() && hovered() && hoverPart_ == p && !pressed();
}

bool DockPanel::partDown(Part p) const
{
    if (p == Part::Menu && menuOpen_)
        return isEnabled();   // en öppen meny = intryckt ▾
    return isEnabled() && pressed() && pressPart_ == p && pressInside_;
}

// Ytan, rubrikraden, titeln och knapparna ritas av den delade panelrubriken
// (`detail::drawPanelHeader`, ../dock/dock_header.cpp — där de fyra
// tillstånden och de tre familjernas recept står); det som är panelens eget är
// VILKA knappar som finns och var, och FOKUS. Fokus finns bara på den
// HOPFÄLLDA panelen utan värd (den är då ett tabbstopp): en ring i `accent`
// innanför rubriken. I en värd är fokusringen värdens — runt handtaget eller
// fliken.
void DockPanel::draw(const Ctx& c)
{
    const gfx::Rectf b = c.bounds();
    const gfx::Rectf h = headerRect(c, b);
    const auto&      m = *c.style;
    // Linjen under rubriken — bara när det finns ett innehåll under den.
    const bool linje = !standingStrip() && bodyRect(c, b).h > 0.0f;

    // Knapparna i ritordning. Menyknappen har en nedåtpil, knappnålen står
    // STÅENDE på en fäst panel och LIGGER på en som döljs automatiskt (det
    // trycket gör är det motsatta), stäng är ett kryss. Stängknappen utan
    // `closable` och knappnålen på en flytande panel har en TOM rekt — de
    // tänds aldrig och ritas inte.
    using Glyph = detail::HeaderButton::Glyph;
    const detail::HeaderButton knappar[] = {
        {menuRect(c, b), Glyph::Menu, partHot(Part::Menu), partDown(Part::Menu)},
        {pinRect(c, b), pinShown() ? Glyph::Pinned : Glyph::Unpinned,
         partHot(Part::Pin), partDown(Part::Pin)},
        {closeRect(c, b), Glyph::Close, partHot(Part::Close),
         partDown(Part::Close)},
    };
    detail::drawPanelHeader(c,
                            {.bounds     = b,
                             .header     = h,
                             .title      = titleRect(c, b),
                             .titleColor = textColor(c),
                             .enabled    = isEnabled(),
                             .line       = linje,
                             .buttons    = knappar},
                            text());

    if (focused() && focusable() && isEnabled())
        Draw::frame(c, Draw::inset(h, m.focusWidth / 2.0f), 0.0f,
                    Pen{c.theme->colors.accent, m.focusWidth});
}

// ---------------------------------------------------------- beteendet

// KNAPPNÅLEN: en fäst panel döljs automatiskt (onålad, infälld — i en
// värd glider den in och lämnar sitt handtag); en som döljs automatiskt fästs
// — i en värd blir den öppna overlayn dockad, utan värd fälls den ut.
bool DockPanel::userPin()
{
    if (pinShown()) {
        setPinned(false);
    } else {
        const bool varC = collapsed_;
        const bool varP = pinned_;
        pinned_ = true;
        if (!host())
            collapsed_ = false;
        stateChanged(varC, varP);
    }
    return userChanged();
}

// FLYTANDE ↔ DOCKAD — menyns "Flytande"/"Docka" och dubbelklicket.
bool DockPanel::userFloat(bool on)
{
    if (floating_ == on || (on && !floatable_))
        return false;
    setFloating(on);
    return userChanged();
}

// Space/Enter på den hopfällda panelen UTAN värd — fäst och utfälld.
bool DockPanel::userExpand()
{
    pinAndExpand();
    return userChanged();
}

// STÄNG: dold = stängd. I en värd flyttar värdens fokusvakt fokus ur panelen
// (till huvudytan) redan i `hide()` — `childVisibilityChanged` — och syskonen
// ritas om av `setVisible` (relayoutSiblings).
bool DockPanel::userClose()
{
    if (!closable_)
        return false;
    closeMenu();
    hide();
    return userChanged();
}

// Panelens och värdens `onChange`, i den ordningen — värden fyrar båda, så
// en panel i en värd och en ensam panel beter sig likadant mot sin egen
// handler. Ingen kod rör panelen efter handlerna.
bool DockPanel::userChanged()
{
    if (DockHost* h = host())
        return h->userChanged({this});
    if (!onChange)
        return false;
    ChangeArgs e;
    onChange(this, e);
    return true;
}

// RUBRIKMENYN — byggs vid varje öppning, så raden för knappnålen säger vad
// den gör NU. Posternas sender är panelen (`Menu::activatePath`), och en
// handler som river panelen rör inget efteråt (menyns egen vakt).
void DockPanel::openMenu()
{
    if (menuOpen_ || !isEnabled() || !appWindow() || !appWindow()->isOpen())
        return;
    menu_.items.clear();
    // FÖNSTERLÄGET: "Flytande" på en dockad panel — släckt när
    // användaren inte får (`setFloatable(false)`) — och "Docka" på en
    // flytande. Knappnålens rad finns bara dockad.
    if (floating_) {
        menu_.addItem(tr(str::DockDock)).onClick = [this] { userFloat(false); };
    } else {
        MenuItem& flyt = menu_.addItem(tr(str::DockFloat));
        flyt.enabled   = floatable_ && host();
        flyt.onClick   = [this] { userFloat(true); };
        menu_.addItem(tr(pinShown() ? str::DockAutoHide : str::DockPin))
            .onClick = [this] { userPin(); };
    }
    menu_.addSeparator();
    MenuItem& stäng = menu_.addItem(tr(str::DockClose));
    stäng.enabled   = closable_;
    stäng.onClick   = [this] { userClose(); };

    const Ctx          c = behaviorCtx();
    const gfx::Rectf   r = resolve(c);
    const HandlerGuard vakt(*this);
    menuOpen_ = true;   // FÖRE öppningen — en gammal kedja stängs synkront
    menu_.popupAtRect(*this,
                      Draw::project(c, r, menuRect(c, Draw::bounds(c, r))));
    if (!vakt.ok())
        return;   // öppningen stängde andra popuper, och en handler rev oss
    if (!menu_.isOpen())
        menuOpen_ = false;
    invalidate();
}

void DockPanel::closeMenu()
{
    if (!menuOpen_)
        return;
    menu_.close();   // uppskjutet
    menuOpen_ = false;
    invalidate();
}

// En inaktiverad panel har ingen öppen meny — som verktygsknappens.
void DockPanel::enabledChanged()
{
    if (!isEnabled())
        closeMenu();
}

// Space/Enter fäller ut en hopfälld panel UTAN värd — det enda den gör som
// tabbstopp. I en värd är handtaget värdens tabbstopp (dock_host.cpp).
bool DockPanel::keyDown(gfx::Key k, std::uint32_t)
{
    if (!focusable() || (k != gfx::Key::Space && k != gfx::Key::Return))
        return false;
    userExpand();
    return true;
}

void DockPanel::mouseDown(const Ctx& c, float x, float y)
{
    pressPart_   = partAt(c, x, y);
    pressInside_ = true;
    headerDrag_  = false;
    pressX_      = x;
    pressY_      = y;
    if (DockHost* h = host())
        h->dragBegin();   // ett avbrutet drags mål får inte stå kvar
    // MENYKNAPPEN öppnar vid TRYCKET, och ett tryck på en öppen meny stänger
    // den — menyradens och verktygsknappens toggle.
    if (pressPart_ == Part::Menu && isEnabled()) {
        if (menuOpen_)
            closeMenu();
        else
            openMenu();
    }
}

// Två sorters tryck: en KNAPP (plattan följer om pekaren står kvar) och
// RUBRIKEN (blir ett drag när pekaren rört sig mer än stilens tröskel, och
// bara i en värd — utan värd finns ingenstans att docka). Målet räknar
// värden: `panelDragMove` per rörelse, och den visar indikatorn.
void DockPanel::mouseDrag(const Ctx& c, float x, float y)
{
    if (pressPart_ == Part::Pin || pressPart_ == Part::Close) {
        const bool inne = partAt(c, x, y) == pressPart_;
        if (inne != pressInside_) {
            pressInside_ = inne;
            const gfx::Rectf r = resolve(c);
            invalidateRect(
                Draw::project(c, r, headerRect(c, Draw::bounds(c, r))));
        }
        return;
    }
    if (pressPart_ != Part::Header)
        return;
    DockHost* h = host();
    if (!h)
        return;
    if (!headerDrag_) {
        if (!detail::pastDragThreshold(c, pressX_, pressY_, x, y))
            return;
        headerDrag_ = true;
    }
    h->panelDragMove(*this, x, y);
}

bool DockPanel::mouseUp(const Ctx& c, float x, float y)
{
    const Part tryck = pressPart_;
    const bool drag  = headerDrag_;
    pressPart_  = Part::None;
    headerDrag_ = false;

    if (drag) {
        DockHost* h = host();
        return h ? h->panelDragEnd(*this, x, y) : false;
    }
    if ((tryck != Part::Pin && tryck != Part::Close) || !isEnabled() ||
        partAt(c, x, y) != tryck)
        return false;   // släppt utanför knappen — ett ångrat klick
    return tryck == Part::Pin ? userPin() : userClose();
}

// DUBBELKLICK PÅ RUBRIKEN växlar mellan dockad och flytande (VS) — bara
// i en värd, och till flytande bara när användaren får.
void DockPanel::mouseDoubleClick(const Ctx& c, float x, float y)
{
    if (!isEnabled() || !host() || partAt(c, x, y) != Part::Header)
        return;
    userFloat(!floating_);
}

// TRYCKET AVBRÖTS — rubrikens drag och knappens tryck släpps (tryckutseendet
// läses med `pressed()`, och fönstret har redan ritat om den tryckta), och
// värden får beskedet: ett buret drag i panelens eget fönster avslutas, den
// platshållarvägens platshållare försvinner. Knappen spelar ingen roll — panelen tar
// varken mitten- eller högertryck, så beskedet gäller vänstertrycket eller den
// explicita capturen (`None`). Ingen användarkod här — värden postar dragets
// `onChange`.
bool DockPanel::mouseCancel(gfx::MouseButton)
{
    pressPart_   = Part::None;
    pressInside_ = false;
    headerDrag_  = false;
    if (DockHost* h = host())
        h->itemPressLost(*this);
    return false;
}

// Hover-delen: bara KNAPPARNA tänds, och bara rubrikraden ritas om — en
// rörelse som inte byter knapp ritar ingenting (regel 1).
void DockPanel::mouseMove(const Ctx& c, float x, float y)
{
    const Part p = partAt(c, x, y);
    if (p == hoverPart_)
        return;
    auto knapp = [](Part q) {
        return q == Part::Menu || q == Part::Pin || q == Part::Close;
    };
    const bool ritaOm = knapp(p) || knapp(hoverPart_);
    hoverPart_ = p;
    if (!ritaOm)
        return;
    const gfx::Rectf r = resolve(c);
    invalidateRect(Draw::project(c, r, headerRect(c, Draw::bounds(c, r))));
}

} // namespace glaze
