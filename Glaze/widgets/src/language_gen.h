#pragma once
#include <cstdint>

namespace glaze::detail {

// Språkgenerationen `TrText` jämför mot: stegas av varje språkbyte och varje
// laddning eller släppning av appens språkfil. Mätcachen (`Widget::measure`)
// har den i nyckeln, för en översatt text mäter olika i olika språk. Bor i
// language.cpp — inget statiskt tillstånd i en header.
std::uint64_t languageGeneration();

} // namespace glaze::detail
