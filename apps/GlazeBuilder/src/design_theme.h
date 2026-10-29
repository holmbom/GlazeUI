#pragma once
#include <string>

#include <glaze/glaze.h>

#include "window_model.h"

// design_theme — designtaggens AVKODNING: strängar → glaze::UI / Theme /
// Style.
//
// Två nivåer, hårt isärhållna:
//
//   builder_core (window_model)  REN std — splittrar raden i nyckel/värde
//                                (bld::parseDesignLine). Vet inget om glaze.
//   design_theme (den här filen) beror på `glaze` och kompileras därför av
//                                BÅDE GlazeBuilder och glaze-render, precis
//                                som palette.cpp/standin_widget.cpp — inte
//                                av rena builder_core.
//
// VÄRDESYNTAXEN speglar C++-API:t, för namnrymden är PER UI och inte global
// ("Neutral" betyder olika saker i Slate och Minimalistic):
//
//   ui=Glaze | Slate | Minimalistic
//   theme=<familj>(<namn>)   t.ex. slate(Teal), slate(Windows),
//                            minimalistic(Neutral), glaze(Lavender)
//   style=<index>            index i familjens style-lista (0 = default)
//   lang=<BCP-47-tagg>       sv · en · nb · fi — RITBORDETS språk
//
// Odekodbart värde ⇒ LOGG + axeln behandlas som osatt. Det gäller även ett
// style-index utanför intervallet: `styleAt` klampar tyst till 0, och en
// tyst klampning döljer gammal eller skadad metadata — därför loggar
// avkodaren INNAN klampningen får ske.

namespace bld {

// Avkodad tagg. `has*` skiljer "osatt" från "satt till familjens default" —
// partiell tagg följer setUI-resetens semantik och behöver den skillnaden.
struct DesignTag {
    bool               hasUI    = false;
    glaze::UI          ui       = glaze::UI::Slate;
    bool               hasTheme = false;
    const glaze::Theme* theme   = nullptr;
    bool               hasStyle = false;
    int                styleIndex = 0;
    // SPRÅKET — ritbordets fjärde axel. Lagras som
    // TAGGSTRÄNG och inte som ett index: språkbutiken äger listan, och
    // `decodeDesignLine` får inte behöva fråga den.
    bool               hasLang = false;
    std::string        lang;
};

// Avkodar designradens TEXT (den som ligger i WindowModel::designLine).
// Tom rad ⇒ tom tagg. Trasig rad (bld::parseDesignLine == false) ⇒ logg +
// tom tagg; raden bevaras ändå av writern.
DesignTag decodeDesignLine(const std::string& designLine);

// Sätter det APP-GLOBALA utseendet ur taggen. ORDNINGEN är bärande:
// `setUI` återställer tema OCH mått till familjens defaults, så UI först,
// därefter theme, därefter style. Det är också semantiken för en PARTIELL
// tagg: bara `ui=Minimalistic` ⇒ Minimalistics defaults, aldrig verktygets
// egna Slate/Teal på Minimalistics form.
void applyDesignTag(glaze::AppWindow& w, const DesignTag& tag);

// Kort människoläsbar beskrivning av det som faktiskt gäller efter
// applyDesignTag — statusradens text i GlazeBuilder.
std::string describeDesign(const glaze::AppWindow& w,
                           const std::string& designLine);

} // namespace bld
