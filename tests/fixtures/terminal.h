#pragma once
#include <glaze/glaze.h>
#include <glaze_terminal/terminal.h>   // sidopaketet Terminal — UTANFÖR regionen

// FIXTUR för terminalen. Kompileras av `terminal_fixture`
// (bara när sidopaketet byggs — en grön round-trip bevisar bara att reader och
// writer är överens, inte att raden kompilerar), och läses av roundtrip_gate
// (byte-identiteten, setter-fälten som fält, anropen och båda
// kopplingsformerna) och preview_gate (`fallTerminal`: en riktig terminal,
// ingen process).
//
// Vad fixturen täcker:
//   term   programmet, argumenten och katalogen, schemat och läget som enum,
//          historiken, Alt som Meta och textstorleken som mått — alla
//          setter-fält; miljön och en egen färg som anrop med flera
//          argument; `start()` (känt, utförs aldrig i en preview); och de två
//          kopplingarna, fullformen och den korta formen.
class TerminalFixture : public glaze::AppWindow {
#pragma region GlazeBuilder
    // GENERERAD av GlazeBuilder — redigera i GlazeBuilder, inte för hand.
public:
    TerminalFixture();

private:
    glaze::Terminal term;
    void initializeComponent();
#pragma endregion

    // Allt härunder är användarens — GlazeBuilder rör det aldrig.
    void termSlut(glaze::Widget* sender, glaze::EventArgs& e);
    void visaTitel();
};
