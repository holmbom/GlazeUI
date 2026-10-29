#pragma once
#include <vector>

#include <glaze/glaze.h>

#include "window_model.h"

// window_tree — `.glazeui`-vägens UTFÖRANDE i ett RIKTIGT fönster: modellen
// blir widget-trädet i ett `glaze::AppWindow`, och satserna körs mot det.
// EN sanning, delad av glaze-render och tests/pixel_gate.cpp.
//
// Pixelgrinden behöver exakt samma träd som verktyget bygger — en kopia i
// grinden hade mätt KOPIAN och inte verktyget; samma skäl som för `applyProp`
// (apply_props.h) och previewvandringen (preview_tree.h).
//
// GlazeBuilders canvas bygger INTE härigenom: den bygger mot en Panel i
// editorfönstret och inte mot ett eget AppWindow (se preview_tree.h).

namespace bld {

// Bygger `win`s träd ur `m` i TVÅ PASS, och delningen är
// hela poängen med satslistan: TRÄDET byggs i NODORDNING (inbyggda först,
// sedan adopt-ordningen = z-ordningen), medan SATSERNA körs i KÄLLORDNING.
// Det är två olika ordningar och de får inte slås ihop: annars flyttas ett
// handskrivet anrop efter `adopt()` i emissionen.
//
// Returnerar widgeten PER NOD, med samma index som `m.nodes`: nullptr för
// en nod som varken är adopterad eller en känd inbyggd fönstermedlem. En
// klass registret inte känner blir en `StandInWidget` (regel 4,
// `makeNodeWidget`) — en anropare som måste skilja den från en riktig widget
// gör det med `dynamic_cast`.
//
// `origin` är bara loggprefixet ("glaze-render", "pixel_gate").
std::vector<glaze::Widget*> buildWindowTree(glaze::AppWindow&  win,
                                            const WindowModel& m,
                                            const char*        origin);

} // namespace bld
