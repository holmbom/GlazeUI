// TopBar — bara typidentiteten + registreringen bor här: plattforms-
// defaulterna och wiringen till fönsterkontrollerna byggs av AppWindow
// (app_window.cpp, buildTopBar) och ritningen är Bars (bar.cpp).

#include "glaze/widgets/top_bar.h"

#include "glaze/app_window.h"
#include "glaze/widgets/caption_button.h"
#include "glaze/widgets/label.h"

namespace glaze {

GLAZE_REGISTER_WIDGET("topbar", TopBar)

// STRUKTURELLA BARN — kontraktet och skälet står i headern. De
// fyra pekarna är RÅA och läses av fönstret utan kontroll, så baren nekar
// att de lossas utifrån. Andra barn (en app som lägger en egen knapp i
// namnlisten) går att ta bort som vanligt.
bool TopBar::canReleaseChild(const Widget& child) const
{
    const Widget* c = &child;
    return c != static_cast<const Widget*>(titleLabel_) &&
           c != static_cast<const Widget*>(minButton_) &&
           c != static_cast<const Widget*>(maxButton_) &&
           c != static_cast<const Widget*>(closeButton_);
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
    AppWindow* w = appWindow();
    if (!w || w->topBar() != this)
        return EdgeInsets{};
    const float lucka = w->appIcon.reservedWidth(c, AppIcon::Row::Menu);
    EdgeInsets  in;
    (w->appIcon.onRightSide() ? in.right : in.left) = lucka;
    return in;
}

} // namespace glaze
