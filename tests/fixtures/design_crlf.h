#pragma once
#include <glaze/glaze.h>

// FIXTUR — inte kompilerad. Läses av tests/roundtrip_gate och renderas av
// glaze-render i designgrindarna. Kroppen är identisk i alla
// design_*-fixturer; bara designtaggen i *.glazeui skiljer dem, så en
// PNG-skillnad kan bara komma av taggen.
//
// Formen är vald FÖR ATT KUNNA MÄTAS: `btn` autosizar båda axlarna (höjden
// blir stilens — 26.7 i Slate, 18 i Minimalistic), och `row1`/`row2` är
// dockade Top i `rows`, så avståndet mellan deras textrader ÄR
// Draw::lineHeight (14.72 vid font 13, 12.16 vid font 10). Knapphöjden
// ensam duger inte som bevis: den härleds ur BÅDE buttonPadY och radhöjden.
class DesignFixture : public glaze::AppWindow {
#pragma region GlazeBuilder
    // GENERERAD av GlazeBuilder — redigera i GlazeBuilder, inte för hand.
public:
    DesignFixture();

private:
    glaze::Button btn;
    glaze::Panel rows;
    glaze::Label row1;
    glaze::Label row2;
    void initializeComponent();
#pragma endregion
};
