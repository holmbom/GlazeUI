#pragma once

// TECKENBREDDEN — hur många celler en
// kodpunkt tar i ett teckenrutnät. Ren std; tabellen är genererad EN gång ur
// Unicode 15.1 (Terminal/tools/width_table.cmake) och committad.
//
//   2  East Asian Width W/F och Emoji_Presentation (kinesiska, emoji)
//   0  Mn, Me, Cf och hangulens U+1160–U+11FF — fogas till klustret före
//   1  allt annat (också styrtecken: skärmen ser dem aldrig som tecken)
//
// KLUSTREN byggs av skärmen (screen.cpp), inte här: VS16 (U+FE0F) breddar en
// pictografisk bas till 2, ZWJ (U+200D) fogar nästa pictogram till klustret,
// en hudton fogas till sin bas och två regionala indikatorer blir en flagga.
// `isPictographic` är den fråga de reglerna ställer.

namespace glaze::term {

int  cellWidth(char32_t cp);
bool isPictographic(char32_t cp);   // Extended_Pictographic

} // namespace glaze::term
