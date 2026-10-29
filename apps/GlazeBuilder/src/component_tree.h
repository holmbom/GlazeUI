#pragma once
#include <cstddef>
#include <memory>
#include <vector>

#include <glaze/component.h>

#include "window_model.h"

// component_tree — `.glazeui`-vägens UTFÖRANDE för de ICKE-VISUELLA
// medlemmarna: `glaze::Timer`,
// `glaze::TrayIcon` och egna `glaze::Component`. EN sanning, delad av
// glaze-render och GlazeBuilders preview — som trädet (window_tree.h) och
// satserna (apply_props.h).
//
// En komponent står i regionen som en medlem men ADOPTERAS ALDRIG, så
// trädbyggarna hoppar över den. Här skapas den ur komponentregistret
// (`glaze::makeComponent`), får sitt namn och sina satser i KÄLLORDNING
// (`applyComponentStmt`) — och hålls sedan vid liv av anroparen bredvid
// trädet, för en `TrayIcon`s meny och en `Timer`s värden är det previewn
// visar sanningen om.
//
// PREVIEWKONTRAKTET: BARA en typ vars registrering säger
// `glaze::PreviewForm::Inert` skapas. En typ som registrerats som
// `StandIn`, och en typ som inte registrerats alls, skapas INTE: noden och
// satserna står kvar i modellen och round-trippas ordagrant (regel 4) men
// utförs inte, och en loggrad säger det. Ingen preview kör någonsin en
// komponents sidoeffekter — de inbyggda är inerta per konstruktion
// (konstruktorn rör inte gfx, och `start()`/`show()` utförs inte).

namespace bld {

// En skapad komponent och dess nod (index i `WindowModel::nodes`).
struct PreviewComponent {
    std::size_t                       node = 0;
    std::unique_ptr<glaze::Component> object;
};

// Varje nod som varken är inbyggd eller adopterad: en INERT komponent
// skapas och får sina satser; allt annat loggas EN gång per nod och
// utförs inte. `origin` är bara loggprefixet ("glaze-render", "preview").
std::vector<PreviewComponent> buildComponents(const WindowModel& m,
                                              const char*        origin);

} // namespace bld
