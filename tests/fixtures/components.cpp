#include "components.h"

// Handler-kroppar för ComponentsFixture — tomma med flit: fixturen finns för
// att KOMPILERA den genererade layouten (se calls_main.cpp), inte för att
// göra något. Signaturerna är beviset: timerns och ikonens kopplingar tar sin
// egen typ som sender.
void ComponentsFixture::klockaTick(glaze::Timer*, glaze::TimerArgs&)
{
}

void ComponentsFixture::ikonClicked(glaze::TrayIcon*, glaze::ClickArgs&)
{
}

void ComponentsFixture::ikonHover()
{
}
