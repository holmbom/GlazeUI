#pragma once
#include <glaze/glaze.h>

#include "demo_text.h"

// SYSTEMFÄLTET — `demo --tray`: en app som BOR i
// systemfältet. Ikonen vid klockan är appens (`glaze::TrayIcon`, en medlem
// här), fönstret kan döljas och visas igen, och krysset stänger till fältet i
// stället för att avsluta. `demo --tray --hidden` startar DOLT — bara ikonen
// syns, som en app som startar med datorn.
//
// Det fönstret visar, rad för rad:
//   * INGEN KNAPP i aktivitetsfältet (`setShowInTaskbar`) — på macOS är det
//     dockan som tas bort, app-globalt i main (`glaze::setShowInDock`).
//   * ANIMATIONEN: fyra av demons bilder och ett intervall; den går bara
//     medan ikonen syns och kryssrutan är ikryssad (regel 1).
//   * HÄNDELSERNA i formen (sender, args) med IKONEN som sender: klick växlar
//     fönstret, dubbelklick visar det, högerklick sätter menyns poster innan
//     den öppnas, och hovern skrivs i statusraden.
//   * MENYN är VÅR, byggd med `appIcon`s platta vokabulär (`{}` är roten) —
//     och "Om GlazeUI…" öppnar en MODAL dialog, så att ett klick på ikonen
//     medan den är öppen lyfter dialogen i stället för att fyras.
//   * AVSLUTA är en egen flagga och `close()` förbi vetot — utan den hade
//     "stäng till systemfältet" aldrig släppt appen.
//
// Utan systemfält (Linux i dag) beter sig fönstret som vilket fönster som
// helst: knappen i aktivitetsfältet står kvar, dölj-knappen och kryssrutorna
// är av, och krysset avslutar — en app som döljer sig utan en ikon att visa
// sig igen med hade blivit osynlig.
//
// HANDKODAT, som `about_dialog` och `free_window` — kodvägen. Ikonen KAN stå
// i `.glazeui` (en icke-visuell medlem, se
// tests/fixtures/components.*), men det här fönstret visar formen i C++.
class TrayWindow : public glaze::AppWindow {
public:
    TrayWindow();

private:
    // Systemfältets händelser — sender är IKONEN (`Handler<Args, TrayIcon>`).
    void ikonKlickad(glaze::TrayIcon* sender, glaze::ClickArgs& e);
    void ikonDubbelklickad(glaze::TrayIcon* sender, glaze::ClickArgs& e);
    void ikonMeny(glaze::TrayIcon* sender, glaze::TrayMenuArgs& e);
    void ikonHover(glaze::TrayIcon* sender, glaze::HoverArgs& e);
    // Fönstrets kryss: stäng till fältet, eller låt det gå när appen avslutas.
    void stangs(glaze::Widget* sender, glaze::CloseArgs& e);

    void animera(bool pa);   // kryssrutan, och ikonen startas eller stoppas
    void avsluta();          // flaggan + close() — vetot släpper igenom

    // Handlers behöver nå tre widgets — bara de sparas (galleriets regel).
    glaze::Checkbox* animera_   = nullptr;
    glaze::Checkbox* stangTill_ = nullptr;
    glaze::Label*    status_    = nullptr;

    glaze::TrayIcon tray_;            // appens ikon — tas bort när fönstret dör
    bool            avslutar_ = false;
};
