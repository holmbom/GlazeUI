// shared_gate_plugin — PLUGINEN i shared_gate.
//
// En MODULE som tests/shared_gate.cpp laddar i körtid med OS:ets egen laddare
// (LoadLibraryW/dlopen) — GlazeUI har ingen `loadModule`. Byggs bara
// i delat läge, och länkar `glaze` och `glaze_threads_ui` som en apps egen
// plugin hade gjort; det delade läget ska ge den SAMMA instans av ramverket
// och sidopaketen som programmet som laddar den.
//
// Ingångarna är `extern "C"` och exporteras uttryckligen (GATE_EXPORT). På
// Windows stänger en `dllexport` av MinGW:s auto-export för pluginen själv —
// avsiktligt: bara ingångarna ska synas, och libglaze når den genom
// importerna, inte genom egna exporter.
//
// Pluginen avlastas aldrig.
//
// Med sidopaketet Terminal länkar pluginen — och BARA den — också
// `glaze_terminal_ui`: ankaret i pluginen är det enda som laddar
// libglaze_terminal_ui.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <thread>

#include <glaze/glaze.h>
#include <glaze/component.h>
#include <glaze/timer.h>
#include <glaze_threads/threads.h>

#if defined(_WIN32)
#define GATE_EXPORT extern "C" __declspec(dllexport)
#else
#define GATE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

using namespace std::chrono_literals;

// D1/D3 — en widgettyp som pluginen registrerar och som ärver en av
// ramverkets: exe:ns `dynamic_cast` till `glaze::Label` går över modulgränsen.
class PluginLabel : public glaze::Label {};

// D11 — en komponenttyp i komponentregistret.
class PluginKomponent : public glaze::Component {};

} // namespace

GLAZE_REGISTER_WIDGET("shared_gate_plugin_widget", PluginLabel)
GLAZE_REGISTER_COMPONENT("shared_gate::PluginKomponent", PluginKomponent,
                         ::glaze::PreviewForm::StandIn)

namespace {

// D8/D10 — svaret från `gate_run_window` som en bitmask.
constexpr int kOnLoad     = 1 << 0;   // onLoad fyrades
constexpr int kEttFönster = 1 << 1;   // App:s register såg pluginens fönster
constexpr int kArbetet    = 1 << 2;   // Workerns svar (42) kom i UI-tråden
constexpr int kTimern     = 1 << 3;   // pluginens Timer tickade i pumpen (D10)
constexpr int kUtanVakt   = 1 << 4;   // stängt av kedjan, inte av vakthunden

void väntaPåStopp(std::stop_token stopp, std::chrono::milliseconds tak)
{
    std::mutex                   m;
    std::condition_variable_any  cv;
    std::unique_lock<std::mutex> lås(m);
    cv.wait_for(lås, stopp, tak, [] { return false; });
}

// Ett fönster DEFINIERAT I PLUGINEN, öppnat dolt i den delade App:s pump.
// Det stängs när BÅDE Workerns svar (UI-bryggan: libglaze_threads_ui:s
// kontext, postad genom App) och timerns tick (libglaze:s timerregister,
// levererat i pumpens svans) har kommit. En vakthund stänger det efter 5 s
// i stället, så ett tappat svar blir ett fall och inte ett häng.
class PluginFönster : public glaze::AppWindow {
public:
    PluginFönster() : glaze::AppWindow("shared_gate_plugin", 200, 120)
    {
        onLoad = [this] { laddad(); };
        timer_.setSingleShot(true);
        timer_.setInterval(glaze::Timer::kMinIntervalMs);
        timer_.onTick = [this] {
            bitar |= kTimern;
            kanskeStäng();
        };
    }

    int         bitar = 0;
    const void* appen = nullptr;   // &glaze::app() sett i onLoad

private:
    void laddad()
    {
        bitar |= kOnLoad;
        if (glaze::app().windowCount() == 1)
            bitar |= kEttFönster;
        appen  = &glaze::app();
        uiTråd_ = std::this_thread::get_id();
        timer_.start();
        arbete_.run([] { return 42; },
                    [this](glaze::threads::Result<int>& r) {
                        if (r && *r == 42 && std::this_thread::get_id() == uiTråd_)
                            bitar |= kArbetet;
                        svaret_ = true;
                        kanskeStäng();
                    });
        vakt_.run(
            [](std::stop_token stopp) {
                väntaPåStopp(stopp, 5s);
                return 0;
            },
            [this](glaze::threads::Result<int>&) { close(); });
    }

    void kanskeStäng()
    {
        if (svaret_ && (bitar & kTimern) != 0) {
            bitar |= kUtanVakt;
            close();
        }
    }

    glaze::Timer           timer_;
    glaze::threads::Worker arbete_;
    glaze::threads::Worker vakt_;
    std::thread::id        uiTråd_;
    bool                   svaret_ = false;
};

} // namespace

// ---- Ingångarna. Varje adress och varje svar kommer ur PLUGINENS anrop in i
//      libglaze och sidopaketen — exe:n jämför med sina egna.

GATE_EXPORT const void* gate_app()
{
    return &glaze::app();
}

// Utseendet bor i `App::themeState()`, som är privat; ett fönsters `theme()`
// är en referens rakt in i det.
GATE_EXPORT const void* gate_theme()
{
    glaze::AppWindow fönster;
    return &fönster.theme();
}

GATE_EXPORT int gate_ui()
{
    glaze::AppWindow fönster;
    return static_cast<int>(fönster.ui());
}

GATE_EXPORT const void* gate_language_ref()
{
    return &glaze::language();
}

GATE_EXPORT bool gate_sees(const char* type)
{
    return glaze::makeWidget(type) != nullptr;
}

GATE_EXPORT bool gate_sees_component(const char* cppType)
{
    return glaze::makeComponent(cppType) != nullptr;
}

GATE_EXPORT bool gate_asset(const char* list, const char* key)
{
    return glaze::img(list, key).valid();
}

GATE_EXPORT const void* gate_pool()
{
    return &glaze::threads::defaultPool();
}

GATE_EXPORT const void* gate_context()
{
    return &glaze::threads::defaultContext();
}

// D12 — profilerarens etikettregister: en etikett och en scope i pluginen.
// Svaret är etikettens id.
GATE_EXPORT std::uint32_t gate_profile_scope(const char* name)
{
    const glaze::ProfileLabel etikett{name};
    {
        const glaze::ProfileScope mät(etikett);
    }
    return etikett.id();
}

// D8/D10: pluginens fönster, dolt startat (`hide()` före `run()`) i den delade
// App:s pump. Svaret är bitmasken ovan; `appInOnLoad` får den App onLoad såg.
GATE_EXPORT int gate_run_window(const void** appInOnLoad)
{
    PluginFönster fönster;
    fönster.hide();
    fönster.run();
    if (appInOnLoad)
        *appInOnLoad = fönster.appen;
    return fönster.bitar;
}
