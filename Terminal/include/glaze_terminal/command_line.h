#pragma once
#include <string>
#include <string_view>
#include <vector>

// KOMMANDORADEN — `Terminal::setArguments` tar EN sträng,
// .NET:s `ProcessStartInfo.Arguments`-form. Windows får strängen ORDAGRANT
// (programmet tolkar sin egen kommandorad); på macOS och Linux delas den här,
// med CommandLineToArgvW:s regler, som .NET gör på Unix. Ren std, grindad
// (terminal_gate C) mot Microsofts dokumenterade exempel.
//
// REGLERNA (Microsoft, "Parsing C++ command-line arguments", 2008-formen):
//   · blanksteg och tabbar skiljer argumenten utanför citat
//   · `"` växlar citatläget; inne i citat är `""` ett bokstavligt `"`
//   · 2n bakstreck + `"` = n bakstreck och ett växlande citat
//   · 2n+1 bakstreck + `"` = n bakstreck och ett bokstavligt `"`
//   · bakstreck som inte följs av `"` står som de står
// PROGRAMNAMNET (argv[0]) har egna, enklare regler i Windows och tolkas inte
// här — strängen är bara argumenten.

namespace glaze::term {

// Argumenten i `commandLine`, delade med reglerna ovan.
std::vector<std::string> splitCommandLine(std::string_view commandLine);

// Ett argument CITERAT så att `splitCommandLine` (och CommandLineToArgvW)
// ger tillbaka exakt det: orört när det varken är tomt eller bär blanksteg,
// tabb, radbrytning eller `"`; annars inom citat med bakstrecken före ett
// `"` och före slutcitatet dubblade.
std::string quoteArgument(std::string_view argument);

} // namespace glaze::term
