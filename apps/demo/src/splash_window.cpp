#include "splash_window.h"

// Timern är en-gångs och redan stoppad när handlern körs (`isRunning()` är
// false här), så det enda som återstår är att stänga — main går sedan vidare
// till startfönstret.
void SplashWindow::stangTimerTick(glaze::Timer*, glaze::TimerArgs&)
{
    close();
}
