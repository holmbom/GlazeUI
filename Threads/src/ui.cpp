// glaze_threads — UI-BRYGGAN. Den enda filen i paketet som ser `glaze`.
// Kontraktet står i ui.h.

#include "glaze_threads/ui.h"

#include <glaze/app.h>

namespace glaze::threads {
namespace {

class UiContext final : public Context {
public:
    // På den startande tråden: App skapas HÄR om den inte redan finns,
    // alltså på UI-tråden och aldrig i en pooltråd.
    void prepare() override { (void)glaze::app(); }

    // Från en pooltråd. `App::post` är den ENDA glaze-funktion som får
    // anropas därifrån (app.h) — och den kör `fn` i pump-svansen.
    void post(std::function<void()> fn) override
    {
        glaze::app().post(std::move(fn));
    }
};

} // namespace

Context& uiContext()
{
    static UiContext kontext;
    return kontext;
}

// Ankarets referens (ui.h) — att den ANROPAS är hela poängen, inte vad den
// gör.
void detail::uiBridgeAnchor() {}

namespace {

// SJÄLVREGISTRERINGEN — länkad brygga = UI-tråden som default. Ett statiskt
// objekt i den här TU:n, och det är därför targetet länkas WHOLE_ARCHIVE i
// statiskt läge: ingen app refererar en symbol härifrån, så en vanlig
// statisk länkning hade strukit TU:n och registreringen med den (samma fälla
// som GLAZE_REGISTER_WIDGET). I delat läge körs den när DLL:en laddas, och
// DLL:en laddas för att ankaret (ui_anchor.cpp) använder en symbol i den.
const bool g_registrerad = [] {
    setDefaultContext(&uiContext());
    return true;
}();

} // namespace

} // namespace glaze::threads
