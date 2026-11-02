// TopBar — typidentiteten, registreringen och placeringen när namnlisten bär
// menyer och glyfknappar: plattforms-defaulterna och wiringen till
// fönsterkontrollerna byggs av AppWindow (app_window.cpp, buildTopBar) och
// ritningen är Bars (bar.cpp).

#include "glaze/widgets/top_bar.h"

#include <algorithm>
#include <vector>

#include "glaze/app_window.h"
#include "glaze/draw.h"
#include "glaze/widgets/caption_button.h"
#include "glaze/widgets/label.h"
#include "glaze/widgets/spacer.h"
#include "glaze/widgets/tool_button.h"

#include "../prop_parse.h"

namespace glaze {

GLAZE_REGISTER_WIDGET("topbar", TopBar)

// STRUKTURELLA BARN — kontraktet och skälet står i headern. Pekarna
// är RÅA och läses av fönstret utan kontroll, så baren nekar
// att de lossas utifrån. Andra barn (en app som lägger en egen knapp i
// namnlisten) går att ta bort som vanligt.
bool TopBar::canReleaseChild(const Widget& child) const
{
    return !isStandard(child);
}

bool TopBar::isStandard(const Widget& child) const
{
    const Widget* c = &child;
    return c == static_cast<const Widget*>(titleLabel_) ||
           c == static_cast<const Widget*>(minButton_) ||
           c == static_cast<const Widget*>(maxButton_) ||
           c == static_cast<const Widget*>(closeButton_) ||
           c == static_cast<const Widget*>(balance_) ||
           c == static_cast<const Widget*>(titleBefore_) ||
           c == static_cast<const Widget*>(titleAfter_) ||
           c == static_cast<const Widget*>(edgeGap_);
}

bool TopBar::isWindowBar() const
{
    const AppWindow* w = appWindow();
    return w && &w->topBar == this;
}

bool TopBar::bearsExtras() const
{
    return std::any_of(children().begin(), children().end(), [this](const Widget* c) {
        return c->isVisible() && !isStandard(*c);
    });
}

void TopBar::setShowTitle(bool on)
{
    if (showTitle_ == on)
        return;
    showTitle_ = on;
    invalidate();
}

bool TopBar::setProp(const std::string& key, const std::string& value)
{
    if (key == "showTitle") {
        setShowTitle(propparse::parseBool(value));
        return true;
    }
    return Bar::setProp(key, value);
}

gfx::Rectf TopBar::placeChild(const Widget& child, const Ctx& c) const
{
    if (isWindowBar() && (!showTitle_ || bearsExtras()))
        return extrasRect(child, c);
    return Bar::placeChild(child, c);
}

// RADEN, vänster till höger, med titeln i området som blir kvar:
//     [ikon] [menyer] [start …]   Titel   [… slut] [– □ ✕] [kantluft]
//     [kantluft] [✕ – □] [… slut]   Titel   [start …] [menyer] [ikon]   (macOS)
// Fönsterknapparna och glyferna har företräde. Av det lediga får menyerna det
// titelns kortaste form och marginaler lämnar, ner till sin kortaste bredd; titeln
// får resten, och ryms inte ens dess kortaste form visas den inte.
gfx::Rectf TopBar::extrasRect(const Widget& child, const Ctx& c) const
{
    const AppWindow& w      = *appWindow();
    const float      s      = c.scale;
    const bool       spegel = AppIcon::onRightSide();
    const float      ikon   = w.appIcon.reservedWidth(c, AppIcon::Row::Title) * s;
    const float      lucka  = c.style->toolGap * s;
    const gfx::Rectf tom{c.area.x, c.area.y, 0.0f, 0.0f};
    const Widget*    meny =
        w.menuBar.parent() == this && w.menuBar.isVisible() ? &w.menuBar : nullptr;

    // Grupperna i radens ordning. Fönsterknapparna och kantluften i barnordning:
    // den följer plattformen redan (`buildTopBar`).
    std::vector<const Widget*> start, slut, knappar;
    for (const Widget* ch : children()) {
        if (!ch->isVisible() || ch == meny)
            continue;
        if (!isStandard(*ch))
            (ch->dock == Dock::Right ? slut : start).push_back(ch);
        else if (ch == minButton_ || ch == maxButton_ || ch == closeButton_ ||
                 ch == edgeGap_)
            knappar.push_back(ch);
    }
    std::vector<const Widget*> vanster, hoger;
    const auto lagg = [](std::vector<const Widget*>& till,
                         const std::vector<const Widget*>& från) {
        till.insert(till.end(), från.begin(), från.end());
    };
    if (spegel) {
        lagg(vanster, knappar);
        lagg(vanster, slut);
        lagg(hoger, start);
        if (meny)
            hoger.push_back(meny);
    } else {
        if (meny)
            vanster.push_back(meny);
        lagg(vanster, start);
        lagg(hoger, slut);
        lagg(hoger, knappar);
    }

    // Luckan står mellan två barn, utom mellan fönsterknapparna: de är ett kluster.
    const auto iKlustret = [&](const Widget* a) {
        return std::find(knappar.begin(), knappar.end(), a) != knappar.end();
    };
    const auto mellan = [&](const Widget* a, const Widget* b) {
        return iKlustret(a) && iKlustret(b) ? 0.0f : lucka;
    };
    const auto summa = [&](const std::vector<const Widget*>& rad) {
        float l = 0.0f;
        for (std::size_t i = 0; i < rad.size(); ++i) {
            if (i > 0)
                l += mellan(rad[i - 1], rad[i]);
            if (rad[i] != meny)
                l += measure(*rad[i], c).w * s;
        }
        return l;
    };

    const bool visaTitel = showTitle_ && titleLabel_ && titleLabel_->isVisible();
    const float marginal = visaTitel ? c.style->titleMinGap * s : 0.0f;
    const float kortast  = visaTitel ? Draw::measure(c, "T…").w * s : 0.0f;
    const float ledigt   = c.area.w - ikon - summa(vanster) - summa(hoger);

    float menyBredd = 0.0f;
    if (meny) {
        const float naturlig = measure(*meny, c).w * s;
        const float minst =
            std::min(naturlig, w.menuBar.titleBarMinWidth(c) * s);
        menyBredd = std::clamp(ledigt - kortast - 2.0f * marginal, minst, naturlig);
    }

    const float vanster0 = c.area.x + (spegel ? 0.0f : ikon);
    const float hoger1   = c.area.x + c.area.w - (spegel ? ikon : 0.0f);
    const auto  bredd    = [&](const Widget* v) {
        return v == meny ? menyBredd : measure(*v, c).w * s;
    };
    const auto  rekt = [&](float x, float l) {
        const auto [y, h] = crossSpan(child, c, true);
        return gfx::Rectf{x, y, l, h};
    };

    float x = vanster0;
    for (std::size_t i = 0; i < vanster.size(); ++i) {
        if (i > 0)
            x += mellan(vanster[i - 1], vanster[i]);
        const float l = bredd(vanster[i]);
        if (vanster[i] == &child)
            return rekt(x, l);
        x += l;
    }
    const float regionStart = x;

    x = hoger1;
    for (std::size_t i = hoger.size(); i-- > 0;) {
        const float l = bredd(hoger[i]);
        x -= l;
        if (hoger[i] == &child)
            return rekt(x, l);
        if (i > 0)
            x -= mellan(hoger[i - 1], hoger[i]);
    }
    const float regionSlut = x;

    if (&child != static_cast<const Widget*>(titleLabel_) || !visaTitel)
        return tom;
    const float rum      = regionSlut - regionStart - 2.0f * marginal;
    const float naturlig = measure(child, c).w * s;
    const float l        = std::min(naturlig, rum);
    if (l < std::min(naturlig, kortast))
        return tom;
    return rekt(regionStart + (regionSlut - regionStart - l) / 2.0f, l);
}

std::pair<float, float> TopBar::crossSpan(const Widget& child, const Ctx& c,
                                          bool row) const
{
    if (isWindowBar() && dynamic_cast<const ToolButton*>(&child))
        return naturalSpan(child, c, row);
    return Bar::crossSpan(child, c, row);
}

const Theme::Variant* TopBar::childTheme(const Widget& child, const Ctx& c) const
{
    return isWindowBar() && !isStandard(child) ? mutedChildTheme(child, c) : nullptr;
}

// Fönstrets hörnradie, i LOGISKA px (Bar ritar i ritrymden medan
// AppWindow räknar i fysiska). Utan ägarfönster — GlazeBuilders preview,
// headless-tester — är baren fyrkantig, precis som ett fönster utan
// rundning.
float TopBar::topRadius(const Ctx& c) const
{
    const AppWindow* w = appWindow();
    return w ? w->cornerRadius() / c.scale : 0.0f;
}

// Luckan är ikonens plats på MENYRADEN — noll för den lilla ikonen, som
// slutar där namnlisten slutar och alltså står ovanför linjen.
Bar::EdgeInsets TopBar::innerEdgeInsets(const Ctx& c) const
{
    if (!isWindowBar())
        return EdgeInsets{};
    AppWindow*  w     = appWindow();
    const float lucka = w->appIcon.reservedWidth(c, AppIcon::Row::Menu);
    EdgeInsets  in;
    (w->appIcon.onRightSide() ? in.right : in.left) = lucka;
    return in;
}

} // namespace glaze
