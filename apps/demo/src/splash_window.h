#pragma once
#include <glaze/glaze.h>

// SPLASHEN — `demo --splash`: ett RAMLÖST fönster
// (`setChrome(gfx::Chrome::None)`) mitt på skärmen
// (`setStartPosition(glaze::StartPosition::CenterScreen)`), utan knapp i
// aktivitetsfältet, som en EN-GÅNGS-TIMER stänger efter 1,5 s — varefter
// main går vidare till startfönstret.
//
// Hela fönstret, timern INKLUSIVE, står i `.glazeui`: `stangTimer` är en
// ICKE-VISUELL medlem (`glaze::Component`) i regionen, som aldrig adopteras,
// med settrar, ett anrop och en koppling vars sender är TIMERN. Det är hård
// regel 8 för en komponent: designern och koden är samma väg.
class SplashWindow : public glaze::AppWindow {
#pragma region GlazeBuilder
    // GENERERAD av GlazeBuilder — redigera i GlazeBuilder, inte för hand.
public:
    SplashWindow();

private:
    glaze::Label rubrik;
    glaze::Label info;
    glaze::Progress forlopp;
    glaze::Timer stangTimer;
    void initializeComponent();
#pragma endregion

    // Allt härunder är användarens — GlazeBuilder rör det aldrig.
    void stangTimerTick(glaze::Timer* sender, glaze::TimerArgs& e);
};
