#pragma once
// Intern header (src/, INTE include/) — delas av backends.
//
// Katalogen över systemfont-kandidater: DET ENDA stället i trädet där
// fontsökvägar får förekomma. En FontFamily löses till en fallback-kedja
// av FAMILJER för aktuell plattform, och varje familj bär sina fyra stilar;
// backends provar i ordning tills en laddas. Ny
// plattform, familj eller stil → utöka font_catalog.cpp, inget annat.
#include <span>

#include "gfx/text.h"

namespace gfx {

// En familj i katalogen: filen per stil, nullptr där familjen saknar stilen.
struct FontFiles {
    const char* regular    = nullptr;
    const char* bold       = nullptr;
    const char* italic     = nullptr;
    const char* boldItalic = nullptr;
};

std::span<const FontFiles> systemFontCandidates(FontFamily family);

// Familjens fil för stilen — nullptr när familjen saknar den.
const char* fontFile(const FontFiles& f, FontStyle s);

// Stilarna att pröva för en begärd stil, NÄRMAST först: fet kursiv → fet →
// kursiv → vanlig, fet → vanlig, kursiv → vanlig. En backend prövar dem
// inom EN familj innan den går till nästa — då blandas aldrig två familjer i
// samma text.
std::span<const FontStyle> nearestStyles(FontStyle wanted);

} // namespace gfx
