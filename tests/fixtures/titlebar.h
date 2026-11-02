#pragma once
#include <glaze/glaze.h>

// FIXTUR för NAMNLISTEN — kompileras av targetet `calls_fixture` (en grön
// round-trip bevisar bara att reader och writer är överens — `.glazeui`
// BYGGS) och läses av roundtrip_gate (byte-identiteten, modellen) och
// preview_gate (`buildWindowTree` utför den).
//
// Vad fixturen täcker:
//   lage      en glyfknapp i namnlisten: `setGlyphOnly(true)`, tooltipen och
//             sidan som det råa fältet `dock` (Right = slutgruppen)
//   menuBar   `setInTitleBar(true)` — menyraden flyttar in i namnlisten — och
//             `show()`
//   topBar    den INBYGGDA namnlisten: `setShowTitle(false)` och adopt-raden
//             `topBar.adopt(lage);` som dess FÖRSTA referens — ingen
//             medlemsrad i regionen och ingen `name`-rad i filen
class TitlebarFixture : public glaze::AppWindow {
#pragma region GlazeBuilder
    // GENERERAD av GlazeBuilder — redigera i GlazeBuilder, inte för hand.
public:
    TitlebarFixture();

private:
    glaze::ToolButton lage;
    void initializeComponent();
#pragma endregion
};
