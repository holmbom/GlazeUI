#pragma once
#include <cstdint>
#include <string>
#include <vector>

// IKONARKETS BILD — det `glaze::writeIconSheet` skriver som PNG, och det
// verktyget för ramverkets egna glyfer (tests/glyph_sheet) lägger under sina
// Draw-former. En väg för båda arken.

namespace glaze::detail {

struct IconSheetImage {
    std::vector<std::uint8_t> px;   // RGBA8, rak alfa, täta rader
    int                       w = 0, h = 0;
};

// Ikonerna `names` (appens namn eller standardikonernas — "Save") i 16, 24, 32
// och 48 med namnet under, i fyra rutor: mono till vänster och färg till
// höger, ljust överst och mörkt under, ritade i ett dolt fönster i skärmens
// skala. Tom lista = alla appens ikoner. Svarar false (och loggar) om ett namn
// är okänt eller om fönstret inte gick att skapa eller läsa. UI-tråden.
bool renderIconSheet(const std::vector<std::string>& names, IconSheetImage& out);

} // namespace glaze::detail
