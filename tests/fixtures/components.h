#pragma once
#include <glaze/glaze.h>

// FIXTUR för de ICKE-VISUELLA MEDLEMMARNA och fönstrets anrop.
// Kompileras av `calls_fixture` (en grön
// round-trip bevisar bara att reader och writer är överens — `.glazeui`
// BYGGS) och läses av roundtrip_gate (byte-identiteten, sendertypen) och
// preview_gate (`buildComponents` utför den, och ingenting startas eller
// visas).
//
// Vad fixturen täcker:
//   klocka   en `glaze::Timer` i regionen som aldrig adopteras: setter-fälten
//            `interval`/`singleShot`, anropet `start()` (känt, utförs aldrig i
//            en preview) och en koppling vars SENDER ÄR TIMERN
//   ikon     en `glaze::TrayIcon`: setter-fälten (tooltipen som tr-id,
//            intervallet, mallbilden), animationens bilder i
//            kollektionsvokabulären, menyn med `{}`-roten, `show()` (känt,
//            utförs aldrig) och två kopplingar — fullformen med
//            `glaze::TrayIcon* s` och den korta formen
//   status   `setCursor(gfx::PointerShape::Wait)` — basens setter-fält
//   knapp    `setCursor` följt av `resetCursor()` — anropet
//   fönstret `setChrome`, `setLocation`, `setStartPosition`, `setWindowState`
//            och `setUseWaitCursor` som nakna fönsteranrop
//
// KANONISK form (writerns): komponenterna står SIST i regionen — nodordningen
// är inbyggda, adopt-ordningen och sedan de icke-adopterade.
class ComponentsFixture : public glaze::AppWindow {
#pragma region GlazeBuilder
    // GENERERAD av GlazeBuilder — redigera i GlazeBuilder, inte för hand.
public:
    ComponentsFixture();

private:
    glaze::Label status;
    glaze::Button knapp;
    glaze::Timer klocka;
    glaze::TrayIcon ikon;
    void initializeComponent();
#pragma endregion

    // Allt härunder är användarens — GlazeBuilder rör det aldrig.
    void klockaTick(glaze::Timer* sender, glaze::TimerArgs& e);
    void ikonClicked(glaze::TrayIcon* sender, glaze::ClickArgs& e);
    void ikonHover();
};
