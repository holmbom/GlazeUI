// shared_gate — DELAT BIBLIOTEK: EN INSTANS AV RAMVERKET PER PROCESS.
//
// Kör:  ./build-shared/tests/shared_gate
// Exit: 0 = grönt · 1 = ett fall föll · 77 = fönsterfallen (D8, D10) kunde
//       inte köras (ingen display) men resten var grönt — ctest räknar
//       SKIPPED, aldrig grönt · ctest-timeouten fångar ett häng
//
// Byggs bara med -DGLAZE_SHARED=ON. Programmet länkar `glaze` och
// `glaze_threads_ui` och laddar tests/shared_gate_plugin.cpp i körtid med
// OS:ets egen laddare (LoadLibraryW, sökvägen omgjord från UTF-8; dlopen med
// RTLD_NOW | RTLD_LOCAL). Pluginen avlastas aldrig. Varje fall jämför
// det PROGRAMMET ser med det PLUGINEN ser — samma objekt, samma adress:
//
//   D0  bryggan FÖRE laddningen: defaultkontexten kör inte i pooltråden — UI-
//       bryggans DLL laddades med programmet, fast programmet aldrig nämner
//       `uiContext` (ankaret ui_anchor.cpp; nämnde programmet den hade D0
//       inte mätt någonting)
//   D1  widgetregistret: pluginens typ finns inte före laddningen men skapas
//       av programmets `makeWidget` efter; registret har båda typerna, och
//       pluginen skapar programmets typ
//   D2  App: pluginens `glaze::app()` är programmets
//   D3  RTTI över modulgränsen: `dynamic_cast<glaze::Label*>` på pluginens
//       widget lyckas
//   D4  utseendet: samma tillstånd (ett fönsters `theme()` är en referens in
//       i `App::themeState()`, som är privat), och programmets
//       `setUI(Minimalistic)` syns i pluginen
//   D5  språket: efter programmets `setLanguage("fi")` är pluginens
//       `language()` samma objekt, och det säger "fi"
//   D6  resurserna: pluginen ser ingen bild före programmets `loadAssets`,
//       och ser den efter (demons assets.bin, nyckeln toolbar/gear)
//   D7  trådpaketet: samma `defaultPool()` och `defaultContext()`
//   D8  pluginens fönster i den delade App: ett AppWindow DEFINIERAT i
//       pluginen, dolt startat (`hide()` före `run()`), ser sig självt i App:s
//       register och programmets App i onLoad; en Worker-medlem svarar 42 i
//       UI-tråden genom bryggan; efteråt är registret tomt
//   D9  ren processlut: exit inom TIMEOUT med en levande defaultPool och en
//       App som programfilens ankare (app_anchor.cpp) river i `exit()` —
//       ctest mäter det, inte grinden
//   D10 timerregistret: pluginens `glaze::Timer`
//       tickar i pumpens svans — fönstret i D8 stängs först när både svaret
//       och ticken kommit; en vakthund stänger det efter 5 s, så ett tappat
//       tick blir ett fall och inte ett häng
//   D11 komponentregistret: pluginens komponenttyp
//       finns inte före laddningen och skapas av programmet efter; pluginen
//       skapar programmets
//   D12 profilerarens etikettregister: en etikett och en scope
//       i pluginen syns i programmets `scopes()`, och etiketten har samma id
//       som programmets etikett med samma namn
//   D13 sidopaketet Terminal: BARA pluginen länkar
//       `glaze_terminal_ui`. "terminal" finns inte i registret före
//       laddningen och skapas av programmet efter — pluginens ankare
//       (terminal_anchor.cpp) laddade libglaze_terminal_ui, och dess
//       registrering hamnade i libglaze:s ENDA register
//
// SABOTAGEN som ska fälla den (körs före commit, aldrig under bygget). Ett
// register i en header ger bara två kopior om också funktionerna som rör det
// är inline där — annars går både programmet och pluginen genom libglaze:s:
//   · SG1 glaze::app() inline i app.h med en statisk instans ............ D2
//   · SG2 registret (`registry()` ur gui_loader.cpp) OCH registerWidgetType/
//         makeWidget/registeredWidgetTypes inline i widget.h ............ D1
//   · SG3 `target_sources(glaze_threads_ui INTERFACE …ui_anchor.cpp)` bort
//         ur Threads/CMakeLists.txt ............................ D0 (Windows)
//   · SG4 defaultPool() inline i pool.h ................................. D7
//   · SG5 slingan med glaze_runtime_dlls sist i tests/CMakeLists.txt bort,
//         och de redan kopierade build-shared/tests/*.dll raderade ......
//         grinden startar inte (STATUS_DLL_NOT_FOUND) och ctest blir rött
//   · SG6 språkbutiken (store() + language()/setLanguage) respektive
//         AssetStore (store() + loadAssets()/img()) inline i sina headrar
//         ...................................................... D5 / D6
//   · SG7 timerregistret (`registret()` ur timer.cpp) och Timer::start/stop
//         inline i timer.h — pluginens tick når aldrig pumpens register ..
//         D10 (vakthunden stänger efter 5 s)
//   · SG8 komponentregistret och registerComponentType/makeComponent inline
//         i component.h ............................................... D11
//   · SG9 `target_sources(glaze_terminal_ui INTERFACE …terminal_anchor.cpp)`
//         bort ur Terminal/CMakeLists.txt ........................... D13
//   · SG9 profilerarens register (`registry()` ur profiler.cpp) som
//         inline-variabel i profiler.h, och ProfileLabel/ProfileScope/scopes
//         inline där — pluginens scope hamnar i pluginens kopia ...... D12
//         (dll_gate H2 fäller samma sabotage i den statiska sviten)
//
// VAD DEN INTE MÄTER (en skriven lista, inte ett förbiseende):
//   · glaze_net — inget observerbart processtillstånd; att det är SHARED är
//     garantin.
//   · Avlastning — regeln är "aldrig".
//   · Felmatchade versioner — okontrollerade med flit.
//   · Antalet exporter (mäts för hand: objdump -p på libglaze.dll).
//   · Processlut med en ikon i systemfältet eller öppna fönster
//     (handtest, doc/os/windows/TESTS.md) — D9 är ett rökprov, inget bevis.
//   · Ankaret på macOS och Debian (utan --as-needed), där ingenting stryks:
//     D0 är grön också utan det.
//   · Ett statiskt mellanbibliotek som länkar `glaze` eller `glaze_threads_ui`
//     PRIVATE och därmed SVÄLJER ankarna (`$<LINK_ONLY>` bär inga
//     INTERFACE-källor, och den orefererade arkivmedlemmen stryks) — App rivs
//     då i DETACH igen. Grinden länkar båda direkt; regeln (länka dem direkt
//     till varje program och plugin) är dokumenterad i MANUAL
//     `shared-library` och vaktas av ingen mekanism.
//   · Postköns stängning vid nedstängningen (`App::shutDown`): att det som
//     låg kvar förstörs utanför låset och att en post därefter släpps.
//   · Installerade och distribuerade layouter — bara byggträdet.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <glaze/glaze.h>
#include <glaze/component.h>
// INTE glaze_threads/ui.h: programmet får aldrig nämna `uiContext`, annars
// håller den referensen bryggans DLL kvar och D0 mäter inte ankaret.
#include <glaze_threads/threads.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

class ExeLabel : public glaze::Label {};
class ExeKomponent : public glaze::Component {};

} // namespace

GLAZE_REGISTER_WIDGET("shared_gate_exe_widget", ExeLabel)
GLAZE_REGISTER_COMPONENT("shared_gate::ExeKomponent", ExeKomponent,
                         ::glaze::PreviewForm::StandIn)

namespace {

int g_fel = 0;

void check(bool ok, const char* vad)
{
    std::printf("%-6s %s\n", ok ? "OK" : "FEL", vad);
    if (!ok)
        ++g_fel;
}

bool har(const std::vector<std::string>& v, const char* s)
{
    return std::find(v.begin(), v.end(), s) != v.end();
}

// Bitarna i gate_run_window:s svar (shared_gate_plugin.cpp).
constexpr int kOnLoad     = 1 << 0;
constexpr int kEttFönster = 1 << 1;
constexpr int kArbetet    = 1 << 2;
constexpr int kTimern     = 1 << 3;
constexpr int kUtanVakt   = 1 << 4;

// Pluginen, laddad med OS:ets laddare och ALDRIG avlastad (B10) — handtaget
// släpps aldrig.
class Plugin {
public:
    bool ladda(const char* utf8)
    {
#ifdef _WIN32
        const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
        if (n <= 0)
            return false;
        std::wstring w(static_cast<std::size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w.data(), n);
        modul_ = LoadLibraryW(w.c_str());
#else
        modul_ = dlopen(utf8, RTLD_NOW | RTLD_LOCAL);
        if (!modul_)
            std::printf("  dlopen: %s\n", dlerror());
#endif
        return modul_ != nullptr;
    }

    template <class F>
    F* fn(const char* namn) const
    {
#ifdef _WIN32
        // Via `void (*)()`, den allmänna funktionspekaren: en FARPROC görs
        // inte om rakt till en annan signatur.
        const auto p = reinterpret_cast<void (*)()>(GetProcAddress(modul_, namn));
        return reinterpret_cast<F*>(p);
#else
        return reinterpret_cast<F*>(dlsym(modul_, namn));
#endif
    }

private:
#ifdef _WIN32
    HMODULE modul_ = nullptr;
#else
    void* modul_ = nullptr;
#endif
};

class DoltFönster : public glaze::AppWindow {
public:
    DoltFönster() : glaze::AppWindow("shared_gate", 200, 120)
    {
        setOffscreen(true);
    }
};

} // namespace

int main()
{
    std::printf("=== shared_gate — delat bibliotek, en instans per process ===\n");

    // ---- D0. FÖRST, före allt som kunde ha skapat kontexten på annat sätt.
    check(!glaze::threads::defaultContext().runsInline(),
          "D0: UI-bryggan är defaultkontexten före laddningen (ankaret höll "
          "kvar dess DLL)");

    // ---- Före laddningen.
    check(glaze::makeWidget("shared_gate_plugin_widget") == nullptr,
          "D1: pluginens widgettyp finns inte före laddningen");
    check(glaze::makeComponent("shared_gate::PluginKomponent") == nullptr,
          "D11: pluginens komponenttyp finns inte före laddningen");
#ifdef SHARED_GATE_TERMINAL
    check(glaze::makeWidget("terminal") == nullptr,
          "D13: terminalen finns inte i registret före laddningen");
#endif

    // ---- Laddningen.
    Plugin plugin;
    if (!plugin.ladda(SHARED_GATE_PLUGIN)) {
        check(false, "pluginen laddades (" SHARED_GATE_PLUGIN ")");
        return 1;
    }
    const auto gateApp       = plugin.fn<const void*()>("gate_app");
    const auto gateTheme     = plugin.fn<const void*()>("gate_theme");
    const auto gateUi        = plugin.fn<int()>("gate_ui");
    const auto gateLanguage  = plugin.fn<const void*()>("gate_language_ref");
    const auto gateSees      = plugin.fn<bool(const char*)>("gate_sees");
    const auto gateSeesComp  = plugin.fn<bool(const char*)>("gate_sees_component");
    const auto gateAsset     = plugin.fn<bool(const char*, const char*)>("gate_asset");
    const auto gatePool      = plugin.fn<const void*()>("gate_pool");
    const auto gateContext   = plugin.fn<const void*()>("gate_context");
    const auto gateRunWindow = plugin.fn<int(const void**)>("gate_run_window");
    const auto gateProfile   = plugin.fn<std::uint32_t(const char*)>("gate_profile_scope");
    if (!gateApp || !gateTheme || !gateUi || !gateLanguage || !gateSees ||
        !gateSeesComp || !gateAsset || !gatePool || !gateContext ||
        !gateRunWindow || !gateProfile) {
        check(false, "pluginens ingångar hittades");
        return 1;
    }
    check(true, "pluginen laddades och dess elva ingångar hittades");

    // ---- D1 + D3. Registret, och RTTI över modulgränsen.
    {
        const auto widget = glaze::makeWidget("shared_gate_plugin_widget");
        check(widget != nullptr,
              "D1: programmets makeWidget skapar pluginens typ efter laddningen");
        const auto typer = glaze::registeredWidgetTypes();
        check(har(typer, "shared_gate_exe_widget") &&
                  har(typer, "shared_gate_plugin_widget"),
              "D1: registret har både programmets och pluginens typ");
        check(gateSees("shared_gate_exe_widget"),
              "D1: pluginen skapar programmets typ");
        check(dynamic_cast<glaze::Label*>(widget.get()) != nullptr,
              "D3: dynamic_cast<glaze::Label*> på pluginens widget lyckas");
    }

#ifdef SHARED_GATE_TERMINAL
    // ---- D13. Sidopaketet Terminal, laddat av pluginens ankare.
    check(glaze::makeWidget("terminal") != nullptr && gateSees("terminal"),
          "D13: programmet och pluginen skapar \"terminal\" efter laddningen");
#endif

    // ---- D11. Komponentregistret.
    {
        check(glaze::makeComponent("shared_gate::PluginKomponent") != nullptr,
              "D11: programmets makeComponent skapar pluginens typ");
        const auto typer = glaze::registeredComponentTypes();
        check(har(typer, "shared_gate::ExeKomponent") &&
                  har(typer, "shared_gate::PluginKomponent"),
              "D11: komponentregistret har båda typerna");
        check(gateSeesComp("shared_gate::ExeKomponent"),
              "D11: pluginen skapar programmets komponenttyp");
    }

    // ---- D2. App.
    check(gateApp() == &glaze::app(), "D2: pluginens glaze::app() är programmets");

    // ---- D4. Utseendet.
    {
        glaze::AppWindow fönster;
        check(gateTheme() == &fönster.theme(),
              "D4: pluginen och programmet läser samma utseendetillstånd");
        fönster.setUI(glaze::UI::Minimalistic);
        check(gateUi() == static_cast<int>(glaze::UI::Minimalistic),
              "D4: programmets setUI(Minimalistic) syns i pluginen");
    }

    // ---- D5. Språket.
    check(glaze::setLanguage("fi"), "D5: programmet byter språk till fi");
    check(gateLanguage() == &glaze::language(),
          "D5: pluginens language() är programmets språkbutik");
    check(*static_cast<const std::string*>(gateLanguage()) == "fi",
          "D5: pluginen läser fi");

    // ---- D6. Resurserna.
    check(!gateAsset("toolbar", "gear"),
          "D6: pluginen ser ingen bild före programmets loadAssets");
    check(glaze::loadAssets(SHARED_GATE_ASSETS), "D6: programmet laddar assets.bin");
    check(gateAsset("toolbar", "gear"), "D6: pluginen ser programmets resurser");

    // ---- D7. Trådpaketet.
    check(gatePool() == &glaze::threads::defaultPool(),
          "D7: pluginens defaultPool() är programmets");
    check(gateContext() == &glaze::threads::defaultContext(),
          "D7: pluginens defaultContext() är programmets");

    // ---- D12. Profilerarens etikettregister.
    {
        constexpr const char* kNamn = "shared_gate.plugin";
        glaze::profiler().setEnabled(true);
        glaze::profiler().scopes(true);   // nollställt
        const std::uint32_t id     = gateProfile(kNamn);
        std::uint64_t       antal  = 0;
        for (const glaze::LabelStats& s : glaze::profiler().scopes())
            if (s.name == kNamn)
                antal = s.count;
        glaze::profiler().setEnabled(false);
        check(antal == 1, "D12: pluginens scope syns i programmets scopes()");
        check(id == glaze::ProfileLabel{kNamn}.id(),
              "D12: pluginens etikett har programmets id för samma namn");
    }

    // ---- D8 + D10. Pluginens fönster i den delade App. Displaykollen på ett
    //      eget fönster, stängt av sin destruktor (threads_ui_gate-mönstret).
    bool harDisplay = false;
    {
        DoltFönster probe;
        probe.show();
        harDisplay = probe.isOpen();
    }
    if (!harDisplay) {
        std::printf("  --   D8 och D10 hoppas över: inget fönster kunde skapas "
                    "(ingen display)\n");
        std::printf("\nshared_gate: %s (%d fel)\n", g_fel ? "FÄLLD" : "SKIPPED",
                    g_fel);
        return g_fel ? 1 : 77;
    }
    const void* appenIOnLoad = nullptr;
    const int   bitar        = gateRunWindow(&appenIOnLoad);
    check((bitar & kOnLoad) != 0, "D8: pluginens fönster fick onLoad i den delade pumpen");
    check((bitar & kEttFönster) != 0,
          "D8: App:s register såg pluginens fönster (windowCount() == 1)");
    check(appenIOnLoad == &glaze::app(), "D8: onLoad såg programmets App");
    check((bitar & kArbetet) != 0,
          "D8: Worker-medlemmens svar (42) kom i UI-tråden genom bryggan");
    check((bitar & kTimern) != 0, "D10: pluginens Timer tickade i den delade pumpen");
    check((bitar & kUtanVakt) != 0,
          "D8/D10: fönstret stängdes av kedjan, inte av vakthunden (5 s)");
    check(glaze::app().windowCount() == 0, "D8: efteråt är App:s register tomt");

    // D9 — processluten — mäter ctest: exit 0 inom TIMEOUT.
    std::printf("\nshared_gate: %s (%d fel)\n", g_fel ? "FÄLLD" : "GRÖN", g_fel);
    return g_fel ? 1 : 0;
}
