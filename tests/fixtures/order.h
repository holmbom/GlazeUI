#pragma once
#include <glaze/glaze.h>

// FIXTUR för SATSORDNINGEN — kompileras av targetet
// `calls_fixture` tillsammans med `calls.*`, av samma skäl: `.glazeui` är
// riktig C++ som BYGGS, och en grön round-trip bevisar bara att reader och
// writer är överens.
//
// Vad fixturen täcker, och varför en fixtur i KANONISK ordning inte hade
// bevisat något:
//   ruta        VARVADE fält och anrop (`disable()` mitt i fältraderna) —
//               får inte samla fälten först och skjuta anropen
//               efter dem — det vore en omordning av exekverbar kod
//   ruta.focus  ett anrop EFTER trädbygget — får inte flyttas före hela
//               adopt-blocket, vilket regel 4 förbjuder
//   statusBar   adopt-raden är den INBYGGDA medlemmens FÖRSTA referens
//               (`statusBar.adopt(chip);`) — föräldrauppslaget måste gå
//               via `findTarget` och inte `find`, annars blir raden
//               fail-safe
//   chip        ett barn i en inbyggd medlem: noden måste byggas EFTER
//               sin förälder, och det är därför builtins ligger först i
//               NODordningen (satsordningen rörs inte)
class OrderFixture : public glaze::AppWindow {
#pragma region GlazeBuilder
    // GENERERAD av GlazeBuilder — redigera i GlazeBuilder, inte för hand.
public:
    OrderFixture();

private:
    glaze::Button ruta;
    glaze::Label chip;
    void initializeComponent();
#pragma endregion
};
