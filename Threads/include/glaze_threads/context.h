#pragma once
#include <functional>

// glaze_threads — KONTEXTEN: var en fortsättning körs.
//
// C#:s `SynchronizationContext` i liten form. Ett arbete körs i en
// pooltråd; när det är klart ska svaret någonstans — i en GUI-app till
// UI-tråden (bara där får widgets röras), i ett kommandoradsverktyg eller en
// grind direkt i pooltråden. Kontexten är det valet, och den är det ENDA
// stället där valet görs: `Worker` frågar sin kontext, och resten av
// paketet vet inte om att det finns en UI-tråd.
//
// TVÅ KONTEXTER FINNS:
//
//   workerContext()   fortsättningen körs DIREKT i pooltråden — kärnans
//                     default, utan något beroende alls.
//   uiContext()       fortsättningen körs i UI-tråden via
//                     `glaze::app().post` — UI-BRYGGAN, ett eget litet
//                     target (`glaze_threads_ui`) som är det enda i paketet
//                     som länkar `glaze`. Se glaze_threads/ui.h.
//
// DEFAULTEN: länkar appen `glaze_threads_ui` registrerar bryggan SIG SJÄLV
// som default (en statisk registrering, samma mekanism som
// `GLAZE_REGISTER_WIDGET` och av samma skäl länkad WHOLE_ARCHIVE i statiskt
// läge; i delat läge håller ankaret `ui_anchor.cpp` bryggans DLL kvar). En
// GUI-app behöver alltså aldrig nämna en kontext — svaren hamnar i
// UI-tråden därför att appen är en GUI-app. Ett verktyg som bara länkar
// kärnan får svaren i pooltråden. Vill man något annat skickar man en
// kontext till `Worker` uttryckligen.

namespace glaze::threads {

class Context {
public:
    virtual ~Context() = default;

    // Anropas på den tråd som STARTAR arbetet, innan något lämnat den.
    // UI-bryggan ser här till att `glaze::App` finns: App skapas lat, och
    // en App som föds i en pooltråd hade fångat FEL tråd som UI-tråd (D3D11
    // läser tråd-id:t i `gfx::init`) och sedan väckts där för evigt.
    virtual void prepare() {}

    // Se till att `fn` körs "där". Anropas från en pooltråd och får ALDRIG
    // vänta på att fn körs — en kontext som kör fn själv, direkt, ska i
    // stället svara true i `runsInline()` (då anropar `Worker` fn själv,
    // utanför sina lås).
    virtual void post(std::function<void()> fn) = 0;

    virtual bool runsInline() const { return false; }
};

// Fortsättningen körs direkt i pooltråden. Lever hela processen.
Context& workerContext();

// Den registrerade defaulten — UI-bryggan om den är länkad, annars
// `workerContext()`. Läses av `Worker` när den SKAPAS.
Context& defaultContext();

// Byter defaulten (nullptr = tillbaka till `workerContext()`). Kontexten
// måste leva resten av processen. Atomisk, men tänkt att sättas EN gång,
// före första `Worker` — UI-bryggan gör det i statisk initiering.
void setDefaultContext(Context* context);

} // namespace glaze::threads
