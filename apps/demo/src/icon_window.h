#pragma once
#include <vector>

#include <glaze/glaze.h>

// IconWindow — `demo --icons`, HANDKODAT: appens EGNA glyfer, ritade med
// standardikonernas motor (`GLAZE_REGISTER_ICON`, `glaze::drawnIcon`) — en gul
// skylt med DEMO som programikon, ett verktygsfält och knappar i 16 och 32 px,
// alla i `IconStyle::Auto`: färg när knappen är aktiv, mono när den är
// inaktiv. En kryssruta inaktiverar knapparna, och en glyfknapp i namnlisten
// växlar läge. `demo --icons-sheet <fil.png>` skriver appens ikonark och
// avslutar. Texterna är literaler, som i arbetsbänken för Slate: fönstret
// ingår inte i demons översatta yta.
class IconWindow : public glaze::AppWindow {
public:
    IconWindow();

private:
    std::vector<glaze::Widget*> knappar_;   // det kryssrutan inaktiverar
};
