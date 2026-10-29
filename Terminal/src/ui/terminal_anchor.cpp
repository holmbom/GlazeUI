// glaze_terminal — WIDGETENS ANKARE i DELAT läge (Threads/src/ui_anchor.cpp-
// mönstret).
//
// INGEN DEL AV WIDGETEN. En INTERFACE-källa på `glaze_terminal_ui`
// (Terminal/CMakeLists.txt): den kompileras in i varje program och DLL som
// länkar paketets UI-del när GlazeUI byggs delat (GLAZE_SHARED), och inte
// alls i statiskt läge, där WHOLE_ARCHIVE gör jobbet.
//
// VARFÖR DEN FINNS: registreringen av "terminal" (`GLAZE_REGISTER_WIDGET` i
// terminal.cpp) körs när libglaze_terminal_ui laddas — men ett program som
// bara skapar terminalen ur registret (previewn, glaze-render) refererar
// ingen symbol i den. PE-länkaren importerar bara en DLL vars symbol används,
// och GNU ld:s --as-needed gör likadant: utan det här anropet laddas DLL:en
// aldrig och typen blir en stand-in. Anropet är en no-op; det är REFERENSEN
// som håller DLL:en kvar.
//
// Länkas `glaze_terminal_ui` PRIVATE av ett statiskt mellanbibliotek följer
// källan inte med (`$<LINK_ONLY>` bär inga källor): länka den direkt till
// varje program och DLL som ska ha terminalen (regel 5 i rot-CLAUDE.md).

#include "glaze_terminal/terminal.h"

namespace {

[[maybe_unused]] const bool glazeTerminalUiAnkare = [] {
    glaze::term::detail::uiAnchor();
    return true;
}();

} // namespace
