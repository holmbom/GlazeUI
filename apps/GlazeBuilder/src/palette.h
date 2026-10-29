#pragma once
#include <cstddef>
#include <string>

// Paletten: VERKTYGSLÅDANS
// METADATA per ramverkstyp — C++-klassnamnet, verktygslåde-namnet och
// default-storleken. Den SKAPAR inga widgets: trädbyggarna frågar registret
// med nodens klass (`bld::makeNodeWidget`, standin_widget.h) — en andra
// sanning om typ ↔ klass hade gjort en registrerad EGEN widget till en
// stand-in. Ny ramverkstyp = en rad här (preview_gate fäller en
// registrerad `glaze::`-klass utan rad).

namespace bld {

struct PaletteEntry {
    const char* cppType;   // "glaze::Button" (medlemsdeklarationens typ)
    const char* namn;      // verktygslåde-etikett + default-text
    float       defW, defH;   // default-storlek (0 = autosize)
};

// Tabellen + antal (extern array har obunden storlek utanför sin TU —
// iterera med kPaletteCount, inte range-for).
extern const PaletteEntry kPalette[];
extern const std::size_t  kPaletteCount;

// C++-klassnamn → palettrad; nullptr = ingen ramverkstyp i verktygslådan.
const PaletteEntry* paletteByCpp(const std::string& cppType);

} // namespace bld
