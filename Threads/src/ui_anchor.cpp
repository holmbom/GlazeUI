// glaze_threads — UI-BRYGGANS ANKARE i DELAT läge.
//
// INGEN DEL AV BRYGGAN. En INTERFACE-källa på `glaze_threads_ui`
// (Threads/CMakeLists.txt): den kompileras in i varje program och DLL som
// länkar bryggan när GlazeUI byggs delat (GLAZE_SHARED), med målets egna
// flaggor — och inte alls i statiskt läge, där WHOLE_ARCHIVE gör jobbet.
//
// VARFÖR DEN FINNS: bryggans självregistrering (`setDefaultContext(&uiContext())`
// i ui.cpp) körs när bryggans DLL laddas — men appen refererar ingen symbol i
// den. PE-länkaren importerar bara en DLL vars symbol används, och GNU ld:s
// --as-needed (förval på Ubuntu) gör likadant: utan det här anropet laddas
// DLL:en aldrig, och fortsättningarna hamnar tyst i pooltråden. Anropet är en
// no-op i DLL:en; det är REFERENSEN som håller den kvar. Ankaret registrerar
// ingenting själv — `setDefaultContext` är en ovillkorlig skrivning och hade
// skrivit över en kontext som appen valt.
//
// Länkas bryggan PRIVATE av ett statiskt mellanbibliotek följer källan inte
// med (`$<LINK_ONLY>` bär inga källor), och ankarobjektet i det statiska
// biblioteket stryks: länka `glaze_threads_ui` direkt till varje program och
// DLL som ska ha bryggan.

#include "glaze_threads/ui.h"

namespace {

[[maybe_unused]] const bool glazeThreadsUiAnkare = [] {
    glaze::threads::detail::uiBridgeAnchor();
    return true;
}();

} // namespace
