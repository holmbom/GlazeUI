// scrollbar_fade.cpp — fönstrets del av rullningslisternas tid
// (`ScrollbarVisibility::Auto`). Vilken lista som syns bestäms av widgeten
// (`Panel`); här bor timern som väcker fönstret när en lista ska släckas.
//
// REGEL 1: ingen ritloop. Timern är satt bara medan någon lista väntar på sin
// tid — EN timer per fönster, satt till den närmaste tidpunkten, och den
// stoppas när ingen widget behöver väckas. Widgetarna bär sina egna tidpunkter
// (`Widget::scrollbarFade`), så fönstret lagrar inga widget-pekare: en
// widget som lämnat trädet eller dött har inget att släcka, och timern går
// bara en tom runda.

#include "scrollbar_fade.h"

#include <algorithm>

#include "glaze/app_window.h"

namespace glaze {

// Flytta timern FRAMÅT mot en redan satt, tidigare tidpunkt: den som är satt
// går först och sätter om sig till nästa vid sitt varv. En satt tidpunkt som
// redan passerat blockerar inget.
void AppWindow::scrollbarArm(std::int64_t deadlineMs)
{
    if (!win_)
        return;
    const std::int64_t nu = detail::scrollbarNowMs();
    if (scrollbarArmedAt_ > nu && scrollbarArmedAt_ <= deadlineMs)
        return;
    const std::int64_t om = std::max<std::int64_t>(1, deadlineMs - nu);
    win_->startTimer(kScrollbarTimerId, static_cast<std::uint32_t>(om));
    scrollbarArmedAt_ = deadlineMs;
}

// Timern gick: varje widget vars tid gått ut släcker sig (och ritas om), och
// timern sätts om till den tidigaste av de som väntar — annars stoppas den.
void AppWindow::scrollbarTimer()
{
    win_->stopTimer(kScrollbarTimerId);   // upprepande timer — sätts om nedan
    scrollbarArmedAt_ = 0;
    const std::int64_t nu   = detail::scrollbarNowMs();
    std::int64_t       next = 0;
    forEachWidget(root_, [&](Widget& w) {
        const std::int64_t t = w.scrollbarFade(nu);
        if (t > 0 && (next == 0 || t < next))
            next = t;
    });
    if (next > 0)
        scrollbarArm(next);
}

// Pekaren rör sig över `over`: den och varje förfader som kan rulla räknar det
// som aktivitet.
void AppWindow::scrollbarPointer(Widget* over)
{
    for (Widget* w = over; w; w = w->parent())
        w->scrollbarPointerMoved();
}

} // namespace glaze
