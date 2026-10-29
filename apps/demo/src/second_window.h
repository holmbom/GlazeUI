#pragma once
#include <glaze/glaze.h>

#include "demo_text.h"
#include "free_window.h"

// Andra fönstret i demon — referensen för multi-fönster: ett vanligt
// AppWindow (samma tre-fils-mönster som StartWindow) som huvudfönstret
// visar med show(*this) — ÄGT, så det kaskad-stängs med ägaren.
class SecondWindow : public glaze::AppWindow {
#pragma region GlazeBuilder
    // GENERERAD av GlazeBuilder — redigera i GlazeBuilder, inte för hand.
public:
    SecondWindow();

private:
    glaze::Label info;
    glaze::Label tips;
    glaze::Label typLbl;
    glaze::Dropdown typDd;
    glaze::Label knappLbl;
    glaze::Dropdown knappDd;
    glaze::Checkbox egenBild;
    glaze::Button om;
    glaze::Button fritt;
    glaze::Button stang;
    glaze::Button medd;
    void initializeComponent();
#pragma endregion

    // Allt härunder är användarens — GlazeBuilder rör det aldrig.
    void omClicked(glaze::Widget* sender, glaze::ClickArgs& e);
    void frittClicked(glaze::Widget* sender, glaze::ClickArgs& e);
    void stangClicked(glaze::Widget* sender, glaze::ClickArgs& e);
    void meddClicked(glaze::Widget* sender, glaze::ClickArgs& e);

    FreeWindow fritt_;   // användaren äger fönsterobjekten (medlem/stack)
};
