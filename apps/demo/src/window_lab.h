#pragma once
#include <memory>
#include <optional>

#include <glaze/glaze.h>

#include "demo_text.h"
#include "form_dialog.h"

// FÖNSTERLABBET — `demo --window-lab`: fönstrets
// geometri, läge och pekare, HANDKODAT (kodvägen; splashen visar samma
// former i `.glazeui`). Det labbet visar, rad för rad:
//   * GEOMETRIN live — `geometry()` läst av en upprepande `glaze::Timer`
//     (250 ms) som bara går medan fönstret syns: position och storlek i
//     NORMALLÄGET och läget, så en maximering visar normalrutan som sparas.
//   * LÄGET — Maximera / Minimera / Normal (`setWindowState`).
//   * SPARA OCH ÅTERSTÄLL — "Spara geometrin" läser `geometry()`, och "Öppna
//     nytt med den sparade" sätter platsen, storleken och läget på ett ANNAT
//     labb FÖRE `show()` — mönstret en app använder vid start. En sparad
//     minimering öppnas i normalläge.
//   * PEKARNA — fyra rutor med `setCursor` (hand, stängd hand, dold, vänta)
//     och en kryssruta för väntepekaren över hela fönstret
//     (`setUseWaitCursor`).
//   * STORLEKEN OCH FÖNSTRET — "Innehåll" ur
//     `root().onResize` (under ett fönsterdrag på Windows och macOS först vid
//     släppet), "Aktivt fönster" ur `glaze::app().activeWindow()` i timerns
//     tick, och "Följ innehållet": en ägd dialog med
//     `setSizeToContent(Height)` som växer och krymper när tre rader visas och
//     döljs, och vars höjd följer den radbrutna texten när den görs bredare.
//   * FORMULÄRET — "Formulär…" öppnar `FormDialog`, en
//     `.glazeui`-triplett: rader uppifrån och mot nederkanten, innehållet som
//     GOLV (`SizeToContentMode::GrowOnly`) och en etikettkolumn över två
//     gruppramar (`Label::setWidthGroup`).
class WindowLab : public glaze::AppWindow {
public:
    WindowLab();

private:
    void visaGeometri();   // timerns tick — och efter varje lägesbyte
    void sparaGeometri();
    void oppnaSparad();

    // Handlers behöver nå fem widgets — bara de sparas (galleriets regel).
    glaze::Label*    geometri_ = nullptr;
    glaze::Label*    sparad_   = nullptr;
    glaze::Checkbox* vanta_    = nullptr;
    glaze::Label*    innehall_ = nullptr;   // root().onResize
    glaze::Label*    aktivt_   = nullptr;   // app().activeWindow()

    glaze::Timer                         klocka_;   // går bara medan fönstret syns
    std::optional<glaze::WindowGeometry> sparadGeo_;
    // Labbet som öppnas med den sparade geometrin — ÄGT av det här (kaskad-
    // stängs med det) och ÅTERANVÄNT: ett stängt fönster visas igen med en
    // ny geometri satt före `show()`.
    std::unique_ptr<WindowLab> nytt_;
    // "Följ innehållet"-dialogen — ägd och återanvänd som `nytt_`.
    std::unique_ptr<glaze::AppWindow> folj_;
    // Formuläret — ägt och återanvänt likadant.
    std::unique_ptr<FormDialog> form_;
};
