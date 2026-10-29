#pragma once
#include <gfx/color.h>

#include "glaze/theme.h"   // Hue

// FÄRGHJÄLPARNA — den matematik paletterna byggs och grindas med, publik.
// Ett sidopaket som HÄRLEDER sina färger ur temat (terminalens schema,
// `Terminal/src/ui/scheme.cpp`) kan inte länka en anonym namnrymd, och en
// andra kopia av mätningen i tests/contrast_gate.cpp hade varit en andra
// sanning om vad 4,5:1 betyder.
//
// Ren GUI-matematik utan tillstånd (dll_gate): ingen statisk variabel, inga
// tabeller — bara funktioner i src/color.cpp.

namespace glaze::color {

// HSL → sRGB. `h` i grader (0–360), `s` och `l` i 0..1. Alfan är 255.
gfx::Color hsl(float h, float s, float l);

// Blandningen a → b, `t` = andelen av vägen (0 = a, 1 = b). KLAMPAD per
// kanal, så `t` över 1 EXTRAPOLERAR bortom `b` utan att en uint8 wrappar —
// det är hur temat uttrycker "starkare än". Alfan är 255.
gfx::Color mix(gfx::Color a, gfx::Color b, float t);

// WCAG:s relativa luminans (0..1) och kontrastkvoten (1..21) — mätningen
// varje palett grindas med (text ≥ 4,5:1, kanter och markeringar ≥ 3:1).
float luminance(gfx::Color c);
float contrast(gfx::Color a, gfx::Color b);

// Kulörvinkeln (grader) en genererad palett byggs ur — samma vinklar som
// `Theme::slate(Hue)`/`Theme::minimalistic(Hue)`. Neutral svarar 0 (mättnaden
// nollas där, så vinkeln spelar ingen roll).
float hueDegrees(Hue h);

} // namespace glaze::color
