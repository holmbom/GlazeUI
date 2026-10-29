// glaze — PROGRAMFILENS ANKARE i DELAT läge.
//
// INGEN DEL AV LIBGLAZE. En INTERFACE-källa på `glaze` (Glaze/widgets/
// CMakeLists.txt): den kompileras in i varje mål som länkar `glaze` när
// GlazeUI byggs delat (GLAZE_SHARED), med målets egna flaggor — och inte alls
// i statiskt läge, där App rivs i programmets `exit()` ändå.
//
// VARFÖR DEN FINNS: App-singletonen bor i libglaze, och Windows river en
// DLL:s statiska objekt i `DllMain(DLL_PROCESS_DETACH)` — EFTER att
// `ExitProcess` dödat processens övriga trådar, och under laddarlåset. App:s
// destruktor stänger fönster och kör `gfx::shutdown()` (D3D11-enheterna,
// systemfältets ikoner), och inget av det får ske där. Programfilens egna
// statiska objekt rivs i stället i `exit()`, med trådarna vid liv: objektet
// nedan river App DÄR (`detail::shutdownApp`), och App:s destruktor har
// sedan ingenting kvar att göra.
//
// BARA I PROGRAMFILEN: samma källa hamnar också i en plugin-DLL och i
// libglaze_threads_ui, och deras statiska objekt rivs i just den DETACH som
// ska undvikas. `shutdownApp` frågar därför vilken modul objektet ligger i
// (`gfx::isInProgramFile`) och gör ingenting utanför programfilen.
//
// På Linux och macOS körs alla modulers atexit i en gemensam omvänd ordning:
// App skapas lat i `main`, efter det här objektet, och rivs alltså före det.
// Ankaret har då ingenting att göra — det skadar inte och gör lägena lika.
//
// LÄNKA `glaze` DIREKT till varje program och plugin. Ett statiskt
// mellanbibliotek som länkar `glaze` PRIVATE sväljer källan: `$<LINK_ONLY>`
// bär inga INTERFACE-källor, och ankarobjektet i det statiska biblioteket är
// en orefererad arkivmedlem som stryks. Då rivs App i DETACH igen. Samma
// regel som UI-bryggans ankare (Threads/src/ui_anchor.cpp); ingen mekanism
// vaktar den (MANUAL `shared-library`).

#include "glaze/app.h"

namespace {

struct ProgramAnkare {
    ~ProgramAnkare() { glaze::detail::shutdownApp(this); }
};

[[maybe_unused]] ProgramAnkare glazeProgramAnkare;

} // namespace
