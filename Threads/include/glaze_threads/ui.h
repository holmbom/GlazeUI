#pragma once
#include "glaze_threads/context.h"

// glaze_threads — UI-BRYGGAN. Kräver targetet
// `glaze_threads_ui`, paketets ENDA del som länkar `glaze`.
//
// Bryggan uppfinner ingenting: den är `glaze::app().post(fn)` inslagen i en
// `Context`. Fortsättningen körs alltså i UI-tråden, i
// pump-SVANSEN, aldrig mitt i en dispatch — samma plats och samma
// garantier som en handskriven `post`.
//
// LÄNKAD = DEFAULT. Bryggan registrerar sig själv som `defaultContext()` vid
// statisk initiering, så en GUI-app som länkar `glaze_threads_ui` aldrig
// behöver nämna en kontext — det är hela bryggans jobb. Registreringen är
// ett statiskt objekt i bryggans egen TU, och därför länkas den
// WHOLE_ARCHIVE i statiskt läge (samma fälla och samma lösning som
// widgetregistren i libglaze); i delat läge håller ankaret `ui_anchor.cpp`
// bryggans DLL kvar (se `detail::uiBridgeAnchor` nedan).
//
// `prepare()` skapar `glaze::App` på den tråd som startar arbetet (UI-tråden)
// om den inte redan finns — en App som föds i en pooltråd hade fångat fel
// tråd-id i `gfx::init` och väckts där för evigt.

namespace glaze::threads {

Context& uiContext();

namespace detail {

// ANKARETS REFERENS i DELAT läge — en no-op i bryggans
// DLL. Ankaret (src/ui_anchor.cpp, en INTERFACE-källa på `glaze_threads_ui`)
// anropar den vid statisk initiering i varje program och DLL som länkar
// bryggan: utan en använd symbol stryker PE-länkaren och GNU ld:s
// --as-needed importen, DLL:en laddas aldrig och självregistreringen körs
// aldrig. Den registrerar INGENTING själv — `setDefaultContext` är en
// ovillkorlig skrivning och hade skrivit över en kontext som appen valt.
void uiBridgeAnchor();

} // namespace detail

} // namespace glaze::threads
