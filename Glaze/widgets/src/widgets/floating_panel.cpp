// FloatingPanel — MÖNSTER: widgeten ritar SIG SJÄLV (hård regel 3) med FÅ
// RADER: ytan, rubrikraden med titel och stängknapp ritas av dockningspanelens
// delade rubrik (`detail::drawPanelHeader`, ../dock/dock_header.h), och här
// står bara greppet i nedre högra hörnet. Måtten kommer ur stilen (`dock*`
// för rubriken, `floatingPanel*` för greppet och det minsta måttet),
// färgerna ur temat — inga tal och inga färger i den här filen. Geometrin bor
// i de privata `*Rect`-metoderna, som ritningen, containerkrokarna och
// musvägen alla läser.

#include "glaze/widgets/floating_panel.h"

#include <algorithm>

#include "glaze/draw.h"
#include "glaze/language.h"
#include "glaze/strings.h"
#include "glaze/widgets/canvas.h"

#include "../dock/dock_header.h"
#include "../dock/dock_model.h"
#include "../prop_parse.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("floatingpanel", FloatingPanel)

bool FloatingPanel::setProp(const std::string& key, const std::string& value)
{
    if (key == "text") {
        setText(value);
        return true;
    }
    if (key == "closable") {
        setClosable(propparse::parseBool(value));
        return true;
    }
    return Panel::setProp(key, value);   // basen SIST — konventionen
}

// ---------------------------------------------------------- geometrin

gfx::Rectf FloatingPanel::headerRect(const Ctx& c, const gfx::Rectf& b) const
{
    return gfx::Rectf{b.x, b.y, b.w, std::min(c.style->dockHeaderH, b.h)};
}

// Stängknappen är en KVADRAT lika hög som raden längst till höger — en tom
// rekt utan `closable`, så den varken tänds eller träffas.
gfx::Rectf FloatingPanel::closeRect(const Ctx& c, const gfx::Rectf& b) const
{
    if (!closable_)
        return gfx::Rectf{};
    const gfx::Rectf h = headerRect(c, b);
    return gfx::Rectf{h.x + h.w - h.h, h.y, h.h, h.h};
}

gfx::Rectf FloatingPanel::titleRect(const Ctx& c, const gfx::Rectf& b) const
{
    const gfx::Rectf h    = headerRect(c, b);
    const float      pad  = c.style->dockHeaderPadX;
    const float      höger = closable_ ? h.h : pad;
    return gfx::Rectf{h.x + pad, h.y, std::max(0.0f, h.w - pad - höger), h.h};
}

gfx::Rectf FloatingPanel::gripRect(const Ctx& c, const gfx::Rectf& b) const
{
    const float g = std::min({c.style->floatingPanelGrip, b.w, b.h});
    return gfx::Rectf{b.x + b.w - g, b.y + b.h - g, g, g};
}

// KROPPEN: innehållsytan (`innerBounds` — ramen inräknad) under rubriken och
// ovanför greppets remsa, i widget-lokala LOGISKA px.
gfx::Rectf FloatingPanel::bodyBounds(const Ctx& c) const
{
    const gfx::Rectf in  = innerBounds(c);
    const gfx::Rectf b   = Draw::bounds(c, resolve(c));
    const float      top = std::max(in.y, b.y + headerRect(c, b).h);
    const float      bot = std::min(in.y + in.h, gripRect(c, b).y);
    return gfx::Rectf{in.x, top, in.w, std::max(0.0f, bot - top)};
}

// Panelens rullning flyttar kroppen precis som den flyttar Panels
// innehållsyta: förskjutningen är skillnaden mellan basens childArea och
// innerRect.
gfx::Rectf FloatingPanel::childArea(const Ctx& c) const
{
    const gfx::Rectf a     = Panel::childArea(c);
    const gfx::Rectf inner = innerRect(c);
    gfx::Rectf       kropp = Draw::project(c, resolve(c), bodyBounds(c));
    kropp.x += a.x - inner.x;
    kropp.y += a.y - inner.y;
    return kropp;
}

gfx::Rectf FloatingPanel::clipRect(const Ctx& c) const
{
    return Draw::project(c, resolve(c), bodyBounds(c));
}

gfx::Rectf FloatingPanel::scrollViewport(const Ctx& c) const
{
    return bodyBounds(c);
}

FloatingPanel::Part FloatingPanel::partAt(const Ctx& c, float x, float y) const
{
    const gfx::Rectf r  = resolve(c);
    const gfx::Rectf b  = Draw::bounds(c, r);
    const float      lx = Draw::localX(c, r, x);
    const float      ly = Draw::localY(c, r, y);
    if (closeRect(c, b).contains(lx, ly))
        return Part::Close;
    if (headerRect(c, b).contains(lx, ly))
        return Part::Header;
    if (gripRect(c, b).contains(lx, ly))
        return Part::Grip;
    return Part::None;
}

gfx::Sizef FloatingPanel::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default:
        // Axelregeln: en satt axel vinner, en auto-axel får panelens minsta
        // mått — det greppet aldrig går under.
        return gfx::Sizef{width > 0.0f ? width : c.style->floatingPanelMinW,
                          height > 0.0f ? height : c.style->floatingPanelMinH};
    }
}

TrText FloatingPanel::toolTipAt(const Ctx& c, float x, float y) const
{
    if (partAt(c, x, y) == Part::Close)
        return tr(str::DockClose);
    return Widget::toolTipAt(c, x, y);
}

// ---------------------------------------------------------- ritningen

bool FloatingPanel::partHot(Part p) const
{
    return isEnabled() && hovered() && hoverPart_ == p && !pressed();
}

bool FloatingPanel::partDown(Part p) const
{
    return isEnabled() && pressed() && pressPart_ == p && pressInside_;
}

// Ytan, rubrikraden, titeln och stängknappen ritas av dockningspanelens
// rubrik (`detail::drawPanelHeader`, ../dock/dock_header.cpp — samma kod, för
// det är samma rubrik, och där står de fyra tillstånden och de tre familjernas
// recept). Panelen STÅR FRITT på en annan yta och får kanten runt om i varje
// familj; det som är eget är greppet, som dimmas som tecknen (`textDim`/
// `border`). Panelen är inget tabbstopp och har ingen fokusring.
void FloatingPanel::draw(const Ctx& c)
{
    const gfx::Rectf b = c.bounds();

    // Stängknappen utan `closable` har en TOM rekt — den tänds aldrig och
    // ritas inte.
    const detail::HeaderButton knappar[] = {
        {closeRect(c, b), detail::HeaderButton::Glyph::Close,
         partHot(Part::Close), partDown(Part::Close)},
    };
    detail::drawPanelHeader(c,
                            {.bounds       = b,
                             .header       = headerRect(c, b),
                             .title        = titleRect(c, b),
                             .titleColor   = textColor(c),
                             .enabled      = isEnabled(),
                             .freestanding = true,
                             .buttons      = knappar},
                            text());
    Draw::grip(c, gripRect(c, b), false,
               detail::panelGlyphColor(c, isEnabled()));
}

// ---------------------------------------------------------- beteendet

Canvas* FloatingPanel::anchorCanvas() const
{
    auto* yta = dynamic_cast<Canvas*>(parent());
    return yta && yta->childAnchor(*this) ? yta : nullptr;
}

// Flyttar draget något? Inte dockad, och inte i en förälder som placerar
// panelen själv (`placesChild` — en Bar, en delare) — utom ritytan med en
// förankring, där draget flyttar ankaret.
bool FloatingPanel::movable() const
{
    if (dock != Dock::None)
        return false;
    return anchored_ || !parent() || !parent()->placesChild(*this);
}

// TRYCKET: rubriken och greppet blir ett drag när pekaren rört sig mer än
// stilens tröskel; stängknappen följer om pekaren står kvar. Läget vid
// trycket sparas — draget räknas alltid från det, och avbrottet återgår dit.
// En förankrad panel vars punkt inte har någon plats på skärmen dras inte.
void FloatingPanel::mouseDown(const Ctx& c, float x, float y)
{
    pressPart_   = Part::None;
    pressInside_ = true;
    drag_        = false;
    if (inScrollStrip(c, x, y)) {
        Panel::mouseDown(c, x, y);   // remsorna ligger överst
        return;
    }
    if (!isEnabled())
        return;
    pressPart_ = partAt(c, x, y);
    pressX_    = x;
    pressY_    = y;
    const gfx::Rectf r = resolve(c);
    startW_            = r.w / c.scale;
    startH_            = r.h / c.scale;
    const float fält[6] = {this->x, this->y, right, bottom, width, height};
    std::copy(std::begin(fält), std::end(fält), std::begin(start_));
    const float luft[4] = {anchorLeft.gap, anchorTop.gap, anchorRight.gap,
                           anchorBottom.gap};
    std::copy(std::begin(luft), std::end(luft), std::begin(startGap_));
    anchored_ = false;
    if (Canvas* yta = anchorCanvas()) {
        const Canvas::ChildAnchor a = *yta->childAnchor(*this);
        const std::optional<Point> l = yta->toLocal(a.world);
        if (!l) {
            pressPart_ = pressPart_ == Part::Close ? Part::Close : Part::None;
        } else {
            anchored_    = true;
            startWorldX_ = a.world.x;
            startWorldY_ = a.world.y;
            startLocalX_ = l->x;
            startLocalY_ = l->y;
            startAnchor_ = a.anchor;
        }
    }
    if (pressPart_ == Part::Close)
        invalidate();
}

void FloatingPanel::mouseDrag(const Ctx& c, float x, float y)
{
    if (pressPart_ == Part::Close) {
        const bool inne = partAt(c, x, y) == Part::Close;
        if (inne != pressInside_) {
            pressInside_ = inne;
            invalidate();
        }
        return;
    }
    if (pressPart_ == Part::None) {
        Panel::mouseDrag(c, x, y);   // remsornas tumdrag
        return;
    }
    if (!drag_) {
        if (!detail::pastDragThreshold(c, pressX_, pressY_, x, y))
            return;
        drag_ = true;
    }
    const float dx = (x - pressX_) / c.scale;
    const float dy = (y - pressY_) / c.scale;
    if (pressPart_ == Part::Header)
        moveTo(dx, dy);
    else
        resizeTo(dx, dy, c.style->floatingPanelMinW, c.style->floatingPanelMinH);
}

bool FloatingPanel::mouseUp(const Ctx& c, float x, float y)
{
    const Part tryck = pressPart_;
    const bool drag  = drag_;
    pressPart_       = Part::None;
    drag_            = false;
    if (drag)
        return fireChange();
    if (tryck != Part::Close)
        return false;
    invalidate();
    if (!closable_ || !isEnabled() || partAt(c, x, y) != Part::Close)
        return false;   // släppt utanför knappen — ett ångrat klick
    hide();
    return fireChange();
}

// Hover: bara stängknappen tänds, och en rörelse som inte byter del ritar
// ingenting (regel 1).
void FloatingPanel::mouseMove(const Ctx& c, float x, float y)
{
    Panel::mouseMove(c, x, y);   // remsornas hover
    const Part p = partAt(c, x, y);
    if (p == hoverPart_)
        return;
    const bool ritaOm = p == Part::Close || hoverPart_ == Part::Close;
    hoverPart_        = p;
    if (ritaOm)
        invalidate();
}

// AVBROTTET (widget.h `mouseCancel`): ett drag AVBRYTS — panelen återgår till
// läget vid trycket — och ingenting fyras.
bool FloatingPanel::mouseCancel(gfx::MouseButton)
{
    const Part tryck = pressPart_;
    const bool drag  = drag_;
    pressPart_       = Part::None;
    pressInside_     = false;
    drag_            = false;
    if (drag && tryck == Part::Header)
        moveTo(0.0f, 0.0f);
    else if (drag && tryck == Part::Grip)
        resizeTo(0.0f, 0.0f, 0.0f, 0.0f);
    if (tryck == Part::Close)
        invalidate();
    return false;
}

// FLYTTEN: den lösta ytan följer pekaren. På ritytan med en förankring är det
// ANKARET som flyttar — punkten under det nya läget, ur kameran. Annars per
// axel: en kant bunden från början (förälderankaret eller ett syskon) och en
// från slutet flyttar var för sig, och en obunden axel (centrerad + offset)
// flyttar med sin offset.
void FloatingPanel::moveTo(float dx, float dy)
{
    if (!movable())
        return;
    if (anchored_) {
        // (0, 0) är TRYCKETS punkt, exakt — avbrottet räknar inte om den ur
        // en kamera som kan ha rört sig.
        Canvas* yta = anchorCanvas();
        const std::optional<Point> v =
            !yta ? std::nullopt
            : dx == 0.0f && dy == 0.0f
                ? std::optional<Point>{Point{startWorldX_, startWorldY_}}
                : yta->toWorld(Point{startLocalX_ + dx, startLocalY_ + dy});
        if (v)
            yta->setChildAnchor(*this, *v, startAnchor_);
        return;
    }
    const bool nära[2]   = {anchors.left || !anchorLeft.to.empty(),
                            anchors.top || !anchorTop.to.empty()};
    const bool bort[2]   = {anchors.right || !anchorRight.to.empty(),
                            anchors.bottom || !anchorBottom.to.empty()};
    const float d[2]     = {dx, dy};
    float*      fält[4]  = {&x, &y, &right, &bottom};
    float*      luft[4]  = {&anchorLeft.gap, &anchorTop.gap, &anchorRight.gap,
                            &anchorBottom.gap};
    const bool  syskon[4] = {!anchorLeft.to.empty(), !anchorTop.to.empty(),
                             !anchorRight.to.empty(), !anchorBottom.to.empty()};
    for (int a = 0; a < 2; ++a) {
        // Närkanten: x/y eller syskonets luft växer med förskjutningen.
        if (nära[a] || !bort[a]) {
            if (syskon[a])
                *luft[a] = startGap_[a] + d[a];
            else
                *fält[a] = start_[a] + d[a];
        }
        // Fjärrkanten: right/bottom eller luften krymper med den.
        if (bort[a]) {
            if (syskon[a + 2])
                *luft[a + 2] = startGap_[a + 2] - d[a];
            else
                *fält[a + 2] = start_[a + 2] - d[a];
        }
    }
    invalidate();
}

// GREPPET: måttet växer med pekaren och det övre vänstra hörnet står still —
// en fjärrkant som är bunden flyttar ut, och en obunden axel (centrerad)
// flyttar sin offset ett halvt steg. På ritytan flyttar ankaret så att hörnet
// står kvar (`gfx::anchorCorner` före och efter).
void FloatingPanel::resizeTo(float dx, float dy, float minW, float minH)
{
    if (!movable())
        return;
    const float nyW = std::max(minW, startW_ + dx);
    const float nyH = std::max(minH, startH_ + dy);
    const bool  återställ = dx == 0.0f && dy == 0.0f;
    width  = återställ ? start_[4] : nyW;
    height = återställ ? start_[5] : nyH;
    if (anchored_) {
        float ox0 = 0.0f, oy0 = 0.0f, ox1 = 0.0f, oy1 = 0.0f;
        gfx::anchorCorner(startAnchor_, startW_, startH_, ox0, oy0);
        gfx::anchorCorner(startAnchor_, nyW, nyH, ox1, oy1);
        Canvas* yta = anchorCanvas();
        const std::optional<Point> v =
            !yta ? std::nullopt
            : återställ
                ? std::optional<Point>{Point{startWorldX_, startWorldY_}}
                : yta->toWorld(Point{startLocalX_ + ox0 - ox1,
                                     startLocalY_ + oy0 - oy1});
        if (v)
            yta->setChildAnchor(*this, *v, startAnchor_);
        invalidate();
        return;
    }
    const float tillväxt[2] = {nyW - startW_, nyH - startH_};
    const bool  nära[2] = {anchors.left || !anchorLeft.to.empty(),
                           anchors.top || !anchorTop.to.empty()};
    const bool  bort[2] = {anchors.right || !anchorRight.to.empty(),
                           anchors.bottom || !anchorBottom.to.empty()};
    float*      fält[4] = {&x, &y, &right, &bottom};
    float*      luft[2] = {&anchorRight.gap, &anchorBottom.gap};
    for (int a = 0; a < 2; ++a) {
        const float g = återställ ? 0.0f : tillväxt[a];
        if (bort[a]) {
            if (!(a == 0 ? anchorRight.to.empty() : anchorBottom.to.empty()))
                *luft[a] = startGap_[a + 2] - g;
            else
                *fält[a + 2] = start_[a + 2] - g;
        } else if (!nära[a]) {
            *fält[a] = start_[a] + g / 2.0f;
        }
    }
    invalidate();
}

bool FloatingPanel::fireChange()
{
    if (!onChange)
        return false;
    fireEvent(onChange);
    return true;
}

} // namespace glaze
