# MANUAL — GlazeUI för agenter

Den här filen är skriven för AI-agenter som ska BYGGA APPAR med GlazeUI —
inte för den som utvecklar ramverket (det står i [CLAUDE.md](CLAUDE.md) och
mapparnas `CLAUDE.md`). Läs den i bitar, aldrig från början till slut.

## Så hittar du rätt avsnitt

Varje avsnitt står mellan två markörer, och aliasen — orden du kan söka på,
på engelska och svenska — står på startraden:

```
    <!-- BEGIN:widget-dropdown | dropdown combobox combo select rullgardin … -->
    …
    <!-- END:widget-dropdown -->
```
(i filen står markörerna först på raden — exemplet är indraget för att inte
synas i sökningarna nedan).

1. **Hela innehållsförteckningen, med radnummer som alltid stämmer:**
   `grep -n "^<!-- BEGIN:" MANUAL.md`
2. **Sök på ett ord** — klassnamn, synonym från ett annat ramverk, svenskt ord:
   `grep -n -i "^<!-- BEGIN:.*combobox" MANUAL.md`
3. **Läs avsnittet** från dess BEGIN-rad till dess END-rad:
   `grep -n -E "^<!-- (BEGIN|END):widget-dropdown " MANUAL.md` ger två
   radnummer; läs det intervallet (Read med offset/limit, eller
   `sed -n 'A,Bp' MANUAL.md`). Mellanslaget efter id:t hindrar att
   `networking` också träffar `networking-http`.

Radnummer skrivs aldrig in för hand — de ändras när filen växer, markörerna
står kvar. Varje avsnitt har samma delar: **Alias**, **Include/CMake/Källa**,
en kort beskrivning, **API**, **Exempel** (koden kompilerar), **.glazeui**
(samma sak i designerns fil) och **Fallgropar**.

**Ny app?** Läs `start`, `three-files`, `app-window`, `events` och `layout`,
sedan avsnitten för de widgets du behöver. **Lägger du ut ett fönster** — läs
[UI_GUIDELINES.md](UI_GUIDELINES.md) först: avstånden, marginalerna och
linjeringen (den är kort och läses hel). **Headrarna är sanningen:** säger
manualen något annat än en header gäller headern — och manualen ska rättas.

## Avsnitten

Id:n ändras inte när filen växer — radnumren gör det. Grep:a id:t (`grep -n -E "^<!-- (BEGIN|END):<id> " MANUAL.md`) och läs intervallet.

- **Kom igång:** `start` · `cmake` · `shared-library` · `three-files` · `glazeui-grammar` · `rules`
- **Appen, fönstret och händelserna:** `app` · `app-window` · `window-placement` · `timer` · `events` · `cursors` · `layout` · `collections`
- **Widgets:** `widgets` · `widget-label` · `widget-button` · `widget-link` · `widget-checkbox` · `widget-radio` · `widget-text-input` · `widget-text-area` · `widget-markdown-view` · `widget-dropdown` · `widget-list-box` · `widget-slider` · `widget-spin-box` · `widget-progress` · `widget-scrollbar` · `widget-picture` · `widget-separator` · `widget-panel` · `widget-group-box` · `widget-floating-panel` · `widget-tab-control` · `widget-splitter` · `widget-dock` · `widget-tool-bar` · `widget-bars` · `widget-list-view` · `widget-data-grid` · `widget-tree-view` · `widget-property-grid` · `widget-color-picker` · `widget-date-picker` · `widget-file-picker`
- **Menyer och dialoger:** `menus` · `app-icon` · `tray-icon` · `message-box` · `file-dialog` · `color-dialog` · `input-box` · `tooltip` · `drag-drop`
- **Ritytan (2D och 3D):** `canvas` · `canvas-shapes` · `canvas-layers` · `canvas-camera` · `canvas-units` · `canvas-painter` · `canvas-hit-test` · `canvas-images` · `canvas-3d` · `canvas-meshes` · `canvas-terrain` · `canvas-programs` · `canvas-shaders` · `canvas-views` · `canvas-first-person`
- **Utseende, egna widgets, språk och resurser:** `appearance` · `custom-widget` · `language` · `assets`
- **Sidopaketen:** `threads` · `networking` · `networking-http` · `networking-tcp` · `networking-udp` · `networking-dns-ping` · `terminal` · `openxr`
- **Fasaden, plattformarna och verktygen:** `gfx` · `platforms` · `profiler` · `testing` · `glaze-render` · `glazebuilder` · `version`

<!-- BEGIN:start | start quickstart snabbstart hello-world minimal-app main run bygga build köra cmake ninja smoke-test röktest timeout first-app kom-igång getting-started -->
## Snabbstart — minsta appen (`start`)

**Alias:** start, quickstart, snabbstart, hello-world, minimal-app, main, run, bygga, build, köra, cmake, ninja, smoke-test, röktest, timeout, first-app, kom-igång, getting-started
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [glaze.h](Glaze/widgets/include/glaze/glaze.h), [app_window.h](Glaze/widgets/include/glaze/app_window.h), [CLAUDE.md](CLAUDE.md)

En GlazeUI-app är en klass som ärver `glaze::AppWindow` plus en `main` som kör den. `run()` visar fönstret och pumpar händelser tills det stängs; `glaze::App` skapas lat vid första fönstret, så `main` deklarerar ingen app. Det här är den handskrivna miniformen — fönster som ska kunna öppnas i GlazeBuilder och previewas skrivs i tre filer (`three-files`). Paraplyet `<glaze/glaze.h>` är den enda include en app behöver.

### API
- `explicit AppWindow(TrText title = {}, int width = 1024, int height = 640, gfx::Chrome chrome = gfx::defaultChrome());` — titel och klientstorlek i logiska px, eget chrome som förval.
- `template <class T, class... Args> T* add(Args&&... args)` — skapa och äg en widget i fönstrets rot-panel.
- `int run();` — visa och pumpa tills fönstret stängts; 0 = normal stängning, 1 = plattformen kunde inte initieras eller fönstret var redan öppet.

### Exempel
```cpp
#include <glaze/glaze.h>

class MainWindow : public glaze::AppWindow {
public:
    MainWindow() : glaze::AppWindow("Hej", 480, 300)
    {
        auto* hello = add<glaze::Label>("Hej världen");
        hello->x = 16;
        hello->y = 16;

        auto* closeBtn = add<glaze::Button>("Stäng");
        closeBtn->anchors = {false, false, true, true};   // höger + botten
        closeBtn->right   = 16;
        closeBtn->bottom  = 16;
        closeBtn->onClick = [this] { close(); };
    }
};

int main()
{
    MainWindow win;
    return win.run();   // visar + pumpar tills fönstret stängts
}
```
```cmake
cmake_minimum_required(VERSION 3.24)
project(hej LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(GlazeUI
    GIT_REPOSITORY https://github.com/holmbom/GlazeUI.git
    GIT_TAG        main)
FetchContent_MakeAvailable(GlazeUI)

add_executable(hej WIN32 MACOSX_BUNDLE main.cpp)
target_link_libraries(hej PRIVATE glaze)
```
```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/hej.exe                       # Windows (MSYS2 UCRT64)
./build/hej.app/Contents/MacOS/hej    # macOS
./build/hej                           # Linux — Wayland eller X11, väljs i körtid
timeout 3 ./build/hej.exe; echo $?    # röktest: 124 = levde till timeout = grönt
```

### .glazeui
Inget — fönstret ovan är handkodat och syns varken i GlazeBuilder eller i previewn. Samma sorts fönster i designerns form: `three-files` och `glazeui-grammar`.

### Fallgropar
- Startlogik bor i `onLoad`/`onShow`, aldrig i en genererad konstruktor (`app-window`).
- `WIN32` gör exen till en GUI-app: `gfx::log` hamnar då i debug-utgången (och i en stderr ingen konsol visar). Bygg utan `WIN32` medan du felsöker.
- Under Git Bash på Windows har `timeout`s SIGTERM ibland missat en nybyggd native exe, som då levde kvar (uppmätt). Robust form: `timeout -k 2 3 ./build/hej.exe` — 124 eller 137 = levde till timeout; se efter med `tasklist` att inget hänger kvar.
- Länka `glaze` och aldrig `glaze_impl`: utan WHOLE_ARCHIVE-länken (statiskt läge) försvinner widgetregistreringarna (`cmake`).
- Ingen egen event-loop och ingen `App app;` — pumpen är ramverkets och sover när inget ändras (`rules`).
<!-- END:start -->

<!-- BEGIN:cmake | cmake fetchcontent add_subdirectory glaze_add_window target_link_libraries targets glaze glaze_threads glaze_threads_ui glaze_net glaze_terminal glaze_terminal_ui glaze_backend glaze_networking glaze_terminal_option msys2 ucrt64 apt pkg-config linux-paket beroenden dependencies -->
## CMake — GlazeUI i ett eget projekt (`cmake`)

**Alias:** cmake, fetchcontent, add_subdirectory, glaze_add_window, target_link_libraries, targets, glaze, glaze_threads, glaze_threads_ui, glaze_net, glaze_terminal, glaze_terminal_ui, glaze_backend, glaze_networking, msys2, ucrt64, apt, pkg-config, linux-paket, beroenden, dependencies
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze`, `glaze_threads_ui`, `glaze_net` · **Källa:** [CMakeLists.txt](CMakeLists.txt), [GlazeWindow.cmake](cmake/GlazeWindow.cmake), [apps/demo/CMakeLists.txt](apps/demo/CMakeLists.txt), [Threads/CLAUDE.md](Threads/CLAUDE.md), [Networking/CLAUDE.md](Networking/CLAUDE.md)

GlazeUI tas in med `FetchContent` (snippeten i `start`) eller med `add_subdirectory` på en utcheckning; sedan finns targets och funktionen `glaze_add_window`. Apparna under `apps/`, testerna och CLI-verktygen byggs bara när GlazeUI självt är toppprojekt. Bygget genererar ingenting: `.glazeui` är committad C++, och `assets.bin`/`language.bin` är committade filer som bara kopieras till körplatsen.

### API
- `glaze` — ramverket: ett INTERFACE-target som i statiskt läge länkar `glaze_impl` WHOLE_ARCHIVE och i delat läge länkar libglaze (`glaze_impl` som DLL/.so/.dylib). Det enda en vanlig app länkar.
- `-DGLAZE_SHARED=ON` — GlazeUI och sidopaketen som delade bibliotek, EN instans per process för ett program som laddar egna DLL:er; statiskt är förval (`shared-library`).
- `glaze_runtime_dlls(<target>)` — GlazeUI:s DLL:er bredvid programmet vid varje bygge (Windows i delat läge; annars en no-op) (`shared-library`).
- `glaze_threads_ui` — sidopaketet Threads med UI-bryggan (svaren hamnar i UI-tråden); `glaze_threads` är kärnan utan glaze (`threads`).
- `glaze_net` — sidopaketet Networking: TCP/UDP/DNS/PING och HTTP(S) genom OS:ets stack (`networking`).
- `glaze_terminal_ui` — sidopaketet Terminal: widgeten `glaze::Terminal` (ett program i OS:ets pseudoterminal); `glaze_terminal` är kärnan utan glaze (`terminal`). Länkas DIREKT till programmet, som `glaze_threads_ui`.
- `glaze_add_window(<var> <bas>...)` — lägger `<bas>.cpp` och `<bas>.glazeui` i listan `<var>` och kompilerar `.glazeui` som C++ (`LANGUAGE CXX`).
- `-DGLAZE_BACKEND=auto|d3d11|metal|linux` — auto väljer D3D11 (Windows), Metal (macOS), linux (Wayland + X11); okänt värde ⇒ `FATAL_ERROR`.
- `-DGLAZE_NETWORKING=OFF` — bygg inte `glaze_net` (på Linux kräver den annars systemets libcurl).
- `-DGLAZE_TERMINAL=OFF` — bygg inte sidopaketet Terminal (inga externa beroenden på något OS; verktygens preview visar då en terminal som stand-in).
- `-DGLAZE_TOOLS=ON` — bygg verktygen (`glaze-render`, `GlazeBuilder`, `glazeassets`, `glazelanguage`) också när GlazeUI är ett beroende. Länka in appens egna widgets med `target_link_libraries(glaze-render PRIVATE <OBJECT-bibliotek>)` (och `GlazeBuilder`) — då ritar verktygen dem på riktigt i stället för som stand-ins (`custom-widget`).
- `-DFETCHCONTENT_SOURCE_DIR_GLAZEUI=<sökväg>` — CMakes egen override: använd en lokal utcheckning, ingen kloning.

### Exempel
```cmake
set(APP_WINDOWS "")
glaze_add_window(APP_WINDOWS src/main_window)
add_executable(minapp WIN32 MACOSX_BUNDLE src/main.cpp ${APP_WINDOWS})
target_link_libraries(minapp PRIVATE glaze glaze_threads_ui glaze_net)

# assets.bin och language.bin är committade KÄLLOR — filkopia till körplatsen.
if(APPLE)
    set(RES_DST "$<TARGET_BUNDLE_CONTENT_DIR:minapp>/Resources")
else()
    set(RES_DST "$<TARGET_FILE_DIR:minapp>")
endif()
add_custom_command(TARGET minapp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RES_DST}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets.bin"
            "${CMAKE_CURRENT_SOURCE_DIR}/language.bin" "${RES_DST}")
```
**Windows:** MSYS2 UCRT64 (`pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja`); D3D11/DXGI/DirectWrite/WIC är OS:et. **macOS:** Apple clang + Homebrew `cmake ninja`; Metal/AppKit/Core Text är OS:et. **Linux** (Debian/Ubuntu — CMake skriver också Fedora- och Arch-listan när något saknas):
```sh
sudo apt install pkg-config libwayland-dev libxkbcommon-dev libx11-dev libxrandr-dev \
    libxi-dev libegl-dev libgl-dev libfontconfig-dev libfreetype-dev libpng-dev libdbus-1-dev
sudo apt install libcurl4-openssl-dev   # bara för glaze_net — annars -DGLAZE_NETWORKING=OFF
```

### Fallgropar
- Länka aldrig `glaze_impl` direkt: i statiskt läge strippas självregistreringarna och `makeWidget` hittar inga typer, och i delat läge tappar målet programfilens ankare (`shared-library`).
- `glaze_add_window` finns först efter `FetchContent_MakeAvailable(GlazeUI)`. `.h`-filen listas inte — den inkluderas av `.cpp` och `.glazeui`.
- `GlazeUI_VERSION` är tom i ditt scope efter FetchContent (uppmätt); läs versionen i koden (`version`).
- `glazelanguage`, `glazeassets`, `glaze-render` och `GlazeBuilder` byggs i en utcheckning där GlazeUI är toppprojekt (`cmake -S GlazeUI -B GlazeUI/build -G Ninja && cmake --build GlazeUI/build --target glazelanguage glazeassets glaze-render`) eller i ditt eget bygge med `-DGLAZE_TOOLS=ON` — de hamnar då under GlazeUI:s byggkatalog (`_deps/glazeui-build/apps/…` med FetchContent).
- Appens widgets in i verktygen som ett OBJECT-bibliotek: ur ett statiskt bibliotek stryker länkaren `GLAZE_REGISTER_WIDGET`-registreringarna, och typen blir en stand-in.
- Repot byggs och grindas med MSYS2 UCRT64 (g++) på Windows; MSVC är ingen provad väg (rot-CMakeLists ger den bara `/utf-8`, och bara inne i GlazeUI:s kataloger), och `GLAZE_SHARED` med MSVC stoppas med `FATAL_ERROR`.
- Resurskopian är en `POST_BUILD`-filkopia, ingen kodgenerering. Saknas filerna vid körning blir ikonerna tomma och appens texter platshållare `⟦id⟧` (loggas en gång).
- Programfilens ikon kräver en uttrycklig rad: `glaze_app_icon(<target>)` (GlazeUI-ikonen) eller med egna `ICO`/`ICNS`/`PNG_DIR`+`PNG_NAME`/`DESKTOP`/`APP_ID` — windres på Windows (`mingw-w64-ucrt-x86_64-binutils`; saknas den stoppar CMake), `.icns` i bundlen på macOS, `install()` av `.desktop` och hicolor-PNG:erna på Linux (`app-icon`).
<!-- END:cmake -->

<!-- BEGIN:shared-library | shared-library dll so dylib delat-bibliotek plugin tillägg modul loadlibrary dlopen glaze_shared glaze_runtime_dlls freelibrary dlclose -->
## Delat bibliotek — plugins som delar ramverket (`shared-library`)

**Alias:** shared-library, dll, so, dylib, delat-bibliotek, plugin, tillägg, modul, loadlibrary, dlopen, glaze_shared, glaze_runtime_dlls, freelibrary, dlclose
**Include:** — · **CMake:** `-DGLAZE_SHARED=ON`, `glaze_runtime_dlls(<target>)` · **Källa:** [GlazeShared.cmake](cmake/GlazeShared.cmake), [shared_gate.cpp](tests/shared_gate.cpp), [shared_gate_plugin.cpp](tests/shared_gate_plugin.cpp)

Statiskt är förval: varje program som länkar GlazeUI får en egen kopia av ramverket. Ett program som laddar egna DLL:er i körtid — där DLL:erna också skapar fönster och widgets — behöver EN instans: samma `glaze::app()`, samma utseende, språk, resurser och widget-, komponent- och timerregister, samma `defaultPool()`/`defaultContext()`. Med `-DGLAZE_SHARED=ON` blir ramverket (med gfx) och sidopaketen delade bibliotek — `libglaze`, `libglaze_threads`, `libglaze_threads_ui`, `libglaze_net`, `libglaze_terminal` och `libglaze_terminal_ui` som `.dll`/`.so`/`.dylib` — och varje modul i processen delar dem. Koden ändras inte: programmet och dess plugins länkar `glaze` som förut. DLL:en laddar appen med OS:ets egen laddare (`LoadLibraryW`/`dlopen`); GlazeUI har ingen `loadModule`.

### API
- `-DGLAZE_SHARED=ON` — HELA GlazeUI delat, ramverket och sidopaketen tillsammans (en delad glaze med statisk threads hade gett en pool per modul). `BUILD_SHARED_LIBS` följs inte. Med MSVC ⇒ `FATAL_ERROR` (ingen auto-export).
- `glaze_runtime_dlls(<target>)` — GlazeUI:s DLL:er (de programmet beror på) kopieras bredvid programmet vid varje bygge, före länkningen. Windows i delat läge; en no-op annars. Bara för en programfil (`FATAL_ERROR` annars). macOS och Linux behöver ingen kopia: byggträdets rpath pekar på biblioteken där de byggs.
- Inget körtids-API: växeln är en byggegenskap och har ingen form i `.glazeui`.

### Exempel
```cmake
set(GLAZE_SHARED ON)                      # FÖRE MakeAvailable — och samma GlazeUI för allt
FetchContent_MakeAvailable(GlazeUI)

add_executable(vard WIN32 MACOSX_BUNDLE src/main.cpp)
target_link_libraries(vard PRIVATE glaze glaze_threads_ui)
glaze_runtime_dlls(vard)

add_library(minplugin MODULE plugin/plugin.cpp)    # laddas i körtid, länkas inte
target_link_libraries(minplugin PRIVATE glaze glaze_threads_ui)
```
```cpp
// plugin/plugin.cpp — en widgettyp och en dialog i värdens App
#include <glaze/glaze.h>

#if defined(_WIN32)
#define PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

class Mätare : public glaze::Label {};
GLAZE_REGISTER_WIDGET("matare", Mätare)        // syns i värdens makeWidget

class PluginDialog : public glaze::AppWindow {
public:
    PluginDialog() : glaze::AppWindow("Plugin", 320, 160) { setModal(true); }
};

PLUGIN_EXPORT int pluginKör(glaze::AppWindow* ägare)
{
    PluginDialog dialog;
    return static_cast<int>(dialog.run(*ägare));   // samma App, samma pump
}
```
```cpp
// src/main.cpp i värden — pluginen laddas på UI-tråden och avlastas aldrig
#include <glaze/glaze.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

using PluginKör = int (*)(glaze::AppWindow*);

PluginKör laddaPlugin(const char* sökväg)
{
#ifdef _WIN32
    HMODULE modul = LoadLibraryA(sökväg);       // LoadLibraryW för sökvägar utanför ANSI
    if (!modul)
        return nullptr;
    HMODULE pinnad = nullptr;                   // aldrig avlastad, inte ens av misstag
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                       reinterpret_cast<LPCSTR>(modul), &pinnad);
    return reinterpret_cast<PluginKör>(
        reinterpret_cast<void (*)()>(GetProcAddress(modul, "pluginKör")));
#else
    void* modul = dlopen(sökväg, RTLD_NOW | RTLD_NODELETE);
    return modul ? reinterpret_cast<PluginKör>(dlsym(modul, "pluginKör")) : nullptr;
#endif
}
```

### .glazeui
Inget — delat eller statiskt är ett byggval. Ett fönster i en plugin är ett vanligt fönster med sina tre filer (`three-files`).

### Fallgropar
- **Samma GlazeUI, alltid.** Det finns INGEN versionskontroll mellan program och DLL (Eriks beslut): programmet och varje plugin byggs mot samma GlazeUI — samma commit, kompilator och `GLAZE_*`-val — och en enda libglaze skeppas. Ett statiskt program med en delad plugin, eller två olika GlazeUI-byggen, är odefinierat beteende: allt eller inget. Biblioteken har inget versionsnummer i filnamnet.
- **Avlasta aldrig en DLL som rört GlazeUI** (`FreeLibrary`/`dlclose`): den har lämnat pekare in i sin kod — registrerade widget- och komponenttyper, kopplade handlers (lambdor), fönster och widgets (vtabeller), funktioner postade med `App::post`, Worker-fortsättningar, en egen `Context` och `TrayIcon`-händelser. Pinna den: `GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, …)`, `dlopen(…, RTLD_NOW | RTLD_NODELETE)` eller pluginen länkad med `-Wl,-z,nodelete`.
- **Ladda pluginen på UI-tråden.** Dess statiska initiering (`GLAZE_REGISTER_WIDGET`) körs på den laddande tråden, och registret är olåst. Den som först anropar `glaze::app()` bestämmer UI-tråden.
- **Länka `glaze` (och `glaze_threads_ui`, `glaze_terminal_ui`) DIREKT till varje program och plugin — inte bara via ett statiskt mellanbibliotek.** Båda bär ett ANKARE, en INTERFACE-källa som kompileras in i det mål som länkar dem. `glaze`s ankare river App i programmets `exit()` medan trådarna lever; utan det rivs App med libglaze:s statiska objekt i `DLL_PROCESS_DETACH` — efter att Windows dödat de andra trådarna, och under laddarlåset — och `~App` och `gfx::shutdown()` körs där. `glaze_threads_ui`s ankare håller bryggans DLL laddad; utan det hamnar fortsättningarna tyst i pooltråden. `glaze_terminal_ui`s ankare håller terminalens DLL laddad; utan det finns "terminal" inte i registret. Ett statiskt mellanbibliotek som länkar dem PRIVATE sväljer ankarna: INTERFACE-källan följer inte med genom `$<LINK_ONLY>`, och en orefererad arkivmedlem stryks. Ingenting i bygget vaktar det — regeln är byggarens.
- Låt `Worker`s och `TrayIcon`s dö före `main` returnerar (de är medlemmar eller lokala, aldrig globala): App rivs i `exit()`, och vid nedstängningen stängs postkön.
- **Delad C++-runtime**: ingen `-static` eller `-static-libstdc++` i program eller plugin — två libstdc++ har var sina globaler. MSYS2:s förval är delat.
- **Appens egna inline-singletons**: en statisk lokal i en inline-funktion, eller en inline-variabel, i en av DINA headrar får en kopia per modul på Windows om både programmet och pluginen använder den. Lägg den i en cpp i en modul — samma regel som GlazeUI följer (`tests/dll_gate.cpp` mäter ramverkets).
- **Länka aldrig `common_gfx`** — bara libglaze gör det. Ett eget mål med `common_gfx` får egna gfx-objekt och ett eget backendtillstånd.
- **Provat med GCC i MSYS2 UCRT64.** Clang/lld (CLANG64) och `-mcmodel=small` är oprövade: auto-importerad data kan där ge "32 bit pseudo relocation … out of range" i körtid. En PE-DLL rymmer 65 535 exporter (GNU ld); libglaze i Debug har drygt 27 000 — når den gränsen faller länkningen högljutt.
- **Skeppa för hand** — ingen `install()` än. Windows: `libglaze*.dll` bredvid programmet, plus verktygskedjans `libstdc++-6.dll`, `libgcc_s_seh-1.dll` och `libwinpthread-1.dll` (de kommer ur MSYS2:s PATH, och `glaze_runtime_dlls`/`TARGET_RUNTIME_DLLS` tar inte med dem). macOS/Linux: rpath sätts av den som paketerar. VS Code-extensionen buntar alltid det statiska bygget.
<!-- END:shared-library -->

<!-- BEGIN:three-files | three-files tre-fils-modellen triplett triplet fönsterfil window-file designer pragma-region glazebuilder-region initializecomponent glazeui genererad generated handler handkodat partial-class -->
## Tre-fils-modellen — ett fönster i tre filer (`three-files`)

**Alias:** three-files, tre-fils-modellen, triplett, triplet, fönsterfil, window-file, designer, pragma-region, glazebuilder-region, initializecomponent, glazeui, genererad, generated, handler, handkodat, partial-class
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` + `glaze_add_window` · **Källa:** [CLAUDE.md](CLAUDE.md), [apps/demo/src/CLAUDE.md](apps/demo/src/CLAUDE.md), [GlazeBuilder/src/CLAUDE.md](apps/GlazeBuilder/src/CLAUDE.md), [cpp_reader.h](apps/GlazeBuilder/src/cpp_reader.h)

Ett fönster = tre committade filer som alla kompileras som vanlig C++ — WinForms-modellen med `Form.Designer.cs`. `<bas>.h` har EN klass med EN `#pragma region GlazeBuilder` (konstruktorn, widget-medlemmarna, `initializeComponent`) som GlazeBuilder äger; allt utanför regionen är ditt. `<bas>.glazeui` är 100 % genererad C++ (konstruktorn + `initializeComponent()`, se `glazeui-grammar`). `<bas>.cpp` är 100 % ditt: handler-kropparna.

### Exempel
`src/main_window.h` — regionen först, dina deklarationer efter `#pragma endregion`:
```cpp
#pragma once
#include <glaze/glaze.h>

class MainWindow : public glaze::AppWindow {
#pragma region GlazeBuilder
    // GENERERAD av GlazeBuilder — redigera i GlazeBuilder, inte för hand.
public:
    MainWindow();

private:
    glaze::Label status;
    glaze::Button okBtn;
    void initializeComponent();
#pragma endregion

    // Allt härunder är användarens — GlazeBuilder rör det aldrig (den
    // appendar bara nya handler-deklarationer).
    void onLoad(glaze::Widget* sender, glaze::EventArgs& e);
    void okBtnClicked(glaze::Widget* sender, glaze::ClickArgs& e);

    int clicks_ = 0;
};
```
`src/main_window.cpp` — en medlemsfunktion per event, formen `(sender, args)`:
```cpp
#include "main_window.h"

#include <string>

void MainWindow::onLoad(glaze::Widget*, glaze::EventArgs&)
{
    status.setText("Redo");   // startlogik bor här — aldrig i konstruktorn
}

void MainWindow::okBtnClicked(glaze::Widget*, glaze::ClickArgs&)
{
    ++clicks_;
    status.setText("Klick: " + std::to_string(clicks_));   // settern invaliderar
}
```
`main.cpp` är `MainWindow win; return win.run();`, och CMake-raden `glaze_add_window(APP_WINDOWS src/main_window)` (`cmake`). Den tredje filen, `src/main_window.glazeui`, står i `glazeui-grammar`.

### .glazeui
En knapp och en handler FÖR HAND, så att GlazeBuilder och previewn kan läsa filen:
1. I regionen i `.h`: `glaze::Button saveBtn;` — standardkonstruerad, ingen initierare, ingen kommentar på raden.
2. I `.glazeui`, ett eget block: `saveBtn.name = "saveBtn";`, sedan settrar och layoutfält — en sats per rad.
3. I `.glazeui`: `saveBtn.onClick = [this](glaze::Widget* s, glaze::ClickArgs& e) { saveBtnClicked(s, e); };`
4. I `.glazeui`, i trädbygget: `root().adopt(saveBtn);` där knappen ska ligga — adopt-ordningen är z-ordningen och dock-ordningen.
5. Efter `#pragma endregion` i `.h`: `void saveBtnClicked(glaze::Widget* sender, glaze::ClickArgs& e);` — och kroppen i `.cpp`.

### Fallgropar
- Du FÅR skriva fritt utanför regionen och i `.cpp`, och ändra `.glazeui` med satser i grammatikens form — medlem, satser och adopt i samma ändring.
- Du får INTE lägga logik i `.glazeui` (if, loopar, lokala variabler, hjälpanrop), skriva startlogik i konstruktorn (den är `{ initializeComponent(); }`), lägga till ett andra `#pragma region GlazeBuilder` eller nästla regioner. Logik hör till `onLoad` i `.cpp`.
- Regionen tar bara `public:`/`private:`/`protected:`, hela `//`-rader, `MainWindow();`, `void initializeComponent();` och `<Typ> <namn>;`. `glaze::Button okBtn{"OK"};` eller en kommentar efter `;` gör filen fail-safe (uppmätt med glaze-render).
- `name`-raden är objektets namn: exakt EN `x.name = "x";` per widget, med medlemsnamnet som värde. En avvikande eller dubblerad rad gör att GlazeBuilder vägrar öppna filen; saknas den lägger buildern in den.
- `menuBar` och `statusBar` deklareras ALDRIG i regionen och adopteras aldrig — de finns redan på `AppWindow` (`statusBar.show();` räcker).
- GlazeBuilder raderar aldrig din kod: vid spara skriver den om regionen och hela `.glazeui`, och APPENDAR bara nya handler-deklarationer efter `#pragma endregion` och stubbar i `.cpp`. En raderad handler blir ett kompileringsfel — avsiktligt.
- Handkodade fönster (`start`) är lika tillåtna men syns inte i previewn; allt ska gå att göra i båda vägarna (`rules`).
<!-- END:three-files -->

<!-- BEGIN:glazeui-grammar | glazeui-grammar glazeui grammatik grammar syntax sats statement fältrad setter metodanrop fönsteranrop event-koppling adopt designrad fail-safe opak opaque cpp_reader regel-4 -->
## Grammatiken i `.glazeui` (`glazeui-grammar`)

**Alias:** glazeui-grammar, glazeui, grammatik, grammar, syntax, sats, statement, fältrad, setter, metodanrop, fönsteranrop, event-koppling, adopt, designrad, fail-safe, opak, opaque, cpp_reader, regel-4
**Include:** — (filen inkluderar sin egen `.h`) · **CMake:** `glaze_add_window` · **Källa:** [cpp_reader.cpp](apps/GlazeBuilder/src/cpp_reader.cpp), [cpp_writer.cpp](apps/GlazeBuilder/src/cpp_writer.cpp), [GlazeBuilder/src/CLAUDE.md](apps/GlazeBuilder/src/CLAUDE.md), [tests/fixtures/calls.glazeui](tests/fixtures/calls.glazeui)

`.glazeui` är riktig C++ som kompileras OCH en strikt radgrammatik som GlazeBuilder, glaze-render och VS Code-previewn läser. En sats per rad, och varje rad måste passa en av formerna nedan. Okända FÄLT och METODER bevaras ordagrant och ignoreras (regel 4), men en rad som inte passar någon form gör att verktygen vägrar HELA filen (fail-safe). Kontrollera med `glaze-render <fil.h>`: exit 0 = läsbar, exit 1 = fail-safe med `fil:rad: orsak` på stderr.

### API
Före kroppen: `// …`-rader, `#include "<bas>.h"`, `MainWindow::MainWindow() { initializeComponent(); }`, `void MainWindow::initializeComponent()` och `{`. I kroppen (tomma rader och hela `//`-rader är fria):
- **Naket fönsteranrop** `<metod>(<arg>);` — `setTitle("…")` eller `setTitle(glaze::tr(1000))`, `setClientSize(w, h)` (två heltal), `setContentSize`, `setMinimumSize`, `setMaximumSize`, `setControlBox`, `setMinimizeBox`, `setMaximizeBox`, `setModal`, `setPopup`, `setTransparent`, `setTopmost`, `setShowInTaskbar`, `setChrome(gfx::Chrome::None)`, `setStartPosition(glaze::StartPosition::CenterScreen)`, `setLocation(x, y)`, `setWindowState(gfx::WindowState::Maximized)`, `setUseWaitCursor(true)`, och med en MEDLEM som argument `setAcceptButton(okBtn)`/`setCancelButton(avbrytBtn)` (eller `nullptr`). Okänt namn bevaras.
- **Fältrad** `<id>.<fält> = <värde>;` — bara LAYOUTFÄLTEN: `name`, `x`, `y`, `width`, `height`, `right`, `bottom`, `dock` (`glaze::Dock::Top`), `anchors` (`{false, false, true, true}`), `anchorLeft`/`Top`/`Right`/`Bottom` (`{"namn", 8}`), `tabIndex`. Mellanslag runt `=` krävs.
- **Setter** `<id>.setX(<värde>);` — tillståndet: `setText`, `setChecked`, `setValue`, `setItems({"a", "b"})`, `setImg(glaze::img("lista", "nyckel"))`, `setToolTip`, `setBorder`, `setAllowDrop` …
- **Metodanrop** `<id>.<metod>(<arg>);` — `statusBar.show()`, `btn.disable()`, `lista.addItem("x")`, `menuBar.addItem("Arkiv", "Spara", "Ctrl+S")`. Okänd metod bevaras och utförs inte i previewn.
- **Koppling, fullform** `<id>.onX = [this](glaze::Widget* s, glaze::<Args>& e) { <handler>(s, e); };` — för fönstret `glaze::AppWindow::onLoad = …`. Tecken för tecken så, men SENDERTYPEN är den som avfyrar: `[this](glaze::Timer* s, glaze::TimerArgs& e)`, `[this](glaze::TrayIcon* s, glaze::ClickArgs& e)`.
- **Icke-visuell medlem** — en `glaze::Timer` eller `glaze::TrayIcon` (eller en egen `glaze::Component`) deklarerad i regionen som en widget men ALDRIG adopterad, med samma setter-, anrops- och kopplingsrader. Previewn utför det som lagrar men aldrig `start()`/`show()` (`timer`, `tray-icon`).
- **Koppling, kort form** `<id>.onX = [this] { <handler>(); };` — bara för kända event-namn, och kroppen är ett naket anrop utan argument.
- **Trädbygge** `root().adopt(<id>);` eller `<förälder>.adopt(<id>);` — varje medlem högst en gång; ordningen är z-ordning OCH dock-ordning.
- **Designrad** `// glazeui-design: ui=… theme=… style=… lang=…` — en kommentar som bara verktygen läser (`appearance`).
- **Värden:** strängar med escapes `\" \\ \n \t \r` (UTF-8 rakt av), `glaze::tr(<decimalt tal>)`, `glaze::img("lista", "nyckel")`, `gfx::Color{r, g, b, a}`, enum med fullt prefix.

### Exempel
`src/main_window.glazeui` till fönstret i `three-files`:
```cpp
// GENERERAD av GlazeBuilder — redigera i GlazeBuilder, inte här.
// glazeui-design: ui=Slate theme=slate(Teal) style=0
#include "main_window.h"

MainWindow::MainWindow() { initializeComponent(); }

void MainWindow::initializeComponent()
{
    setTitle("Räknare");
    setContentSize(360, 160);
    setMinimumSize(300, 140);

    statusBar.show();

    status.name = "status";
    status.setText("Inga klick ännu");
    status.x = 16;
    status.y = 16;

    okBtn.name = "okBtn";
    okBtn.setText("Tryck här");
    okBtn.setToolTip("Räknar klicken");
    okBtn.anchors = {false, false, true, true};
    okBtn.right = 16;
    okBtn.bottom = 16;

    okBtn.onClick = [this](glaze::Widget* s, glaze::ClickArgs& e) { okBtnClicked(s, e); };
    glaze::AppWindow::onLoad = [this](glaze::Widget* s, glaze::EventArgs& e) { onLoad(s, e); };

    root().adopt(status);
    root().adopt(okBtn);
}
```

### Fallgropar
- **Fail-safe** (uppmätt med glaze-render): en sats utan avslutande `;`, en sats över flera rader, en `//`-kommentar EFTER en sats, `x=16;` utan mellanslag, `if`/loopar/lokala variabler, adopt av odeklarerad medlem eller två gånger, två designrader, innehåll efter `}`.
- **Opakt** (bevaras, utförs inte i previewn): okänt fält, okänd metod, kort form med argument (`[this] { f(1); }`), kort form på ett event utanför verktygens lista. Listan bär varje event ramverkets widgets har (`onCommit`, `onFocus`/`onFocusLost` och `onCheck` sedan 0.1.2, `onKeyDown`/`onKeyUp` sedan REQ2 och `onMouseDown`/`onMouseUp`/`onMouseCancel` på varje widget sedan TODO 86), så det som blir en opak fältrad är den korta formen på en EGEN widgets event (`min.onFoo = [this] { f(); };`) — skriv fullformen där.
- En TEXT (`setText`, `setTitle`, `setToolTip`, `setPlaceholder`) som varken är en strängliteral eller `glaze::tr(<tal>)` är opak: `setText(glaze::tr(app::str::Hello))` och `setText(kNamn)` kompilerar, men previewn ritar dem inte (verktygen kan inte lösa en konstant) — skriv NUMRET: `setText(glaze::tr(1001))`.
- Strängar med andra escapes (`\x41`, `\u00e5`) blir opaka — skriv tecknen som UTF-8.
- Läsaren och kompilatorn är två oberoende grindar: bygg OCH kör glaze-render efter varje handredigering.
- Syskonankare får bara peka på TIDIGARE adopterade syskon; ett namn längre fram faller tyst tillbaka på `x`/`y` (`layout`).
- Fönstrets INBYGGDA medlemmar `menuBar`, `statusBar` och `appIcon` deklareras aldrig i regionen och adopteras aldrig — de bär bara satser (`appIcon.setLarge(true);`, `appIcon.addItem({}, "Nytt");`). `{}` är en laglig stig (programmenyns rot) och en laglig bild (`appIcon.setIcon({})` = förvalet).
- Ett nakent `hide();` kompilerar och ger DOLD START i appen, men previewn utför det medvetet inte — ritbordet ska visa fönstret, och dold start hör hemma i `main`.
- Komponenterna står SIST i regionen (writerns nodordning: inbyggda, adopt-ordningen, sedan de icke-adopterade) — en annan ordning läses men skrivs om vid nästa spara.
- `setAcceptButton(okBtn);` pekar på en medlem med NAMN: byter noden namn i GlazeBuilder följer argumentet med, och tas noden bort försvinner raden. Ett namn som inte är en `glaze::Button` i trädet bevaras opakt (loggas, utförs inte). glaze-render och VS Code-previewn utför satsen, så standardknappens accent syns där; GlazeBuilders canvas utför inga fönsteranrop.
- `onResize` är en event-koppling i båda formerna, `setSizeToContent(glaze::SizeToContent::Height);` ett naket fönsteranrop som previewn utför (glaze-render ritar den anpassade storleken; builderns canvas tar modellens) och `verktyg.setAllowedEdges({glaze::Dock::Top, glaze::Dock::Bottom});` ett metodanrop — ett okänt enumnamn eller en lista med `Fill` är opak.
- `setSizeToContentMode(glaze::SizeToContentMode::GrowOnly);` är ett naket fönsteranrop i samma form (också `SizeToContentMode::GrowOnly` och bart), och `lblNamn.setWidthGroup("form");` en setter med en STRÄNGLITERAL — ett uttryck (`setWidthGroup(grupp)`) kompilerar men är opakt, för previewn kan inte räkna ut det. Luften efter en rad är den RÅA fältraden `rad.bottom = 12;`.
<!-- END:glazeui-grammar -->

<!-- BEGIN:rules | rules regler hårda-regler hard-rules agent-regler checklista checklist konventioner conventions förbjudet forbidden do-dont best-practice -->
## Regler för agenter — kortformen (`rules`)

**Alias:** rules, regler, hårda-regler, hard-rules, agent-regler, checklista, checklist, konventioner, conventions, förbjudet, forbidden, do-dont, best-practice
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [CLAUDE.md](CLAUDE.md) (hårda reglerna 1–8), [doc/VISION.md](doc/VISION.md)

De hårda reglerna i `CLAUDE.md` är skrivna för den som utvecklar ramverket; här är vad de betyder för den som BYGGER en app. Ett brott fungerar ofta en stund och blir sedan en bugg som är svår att hitta — UI som inte ritas om, en preview som visar fel, verktyg som vägrar filen.

1. **Händelsestyrd rendering.** Ingen render-loop, ingen polling, inget `while (true)`. Ändra tillstånd via settrar (de invaliderar); ändrar du ett layoutfält utanför en handler: `invalidate()`. Animation = `setAnimating(true)` och av igen; tid = `window().startTimer` (`app-window`).
2. **Ingen kodgenerering i bygget.** `.glazeui`, språkets konstant-header, `assets.bin` och `language.bin` är committade filer; bygget kompilerar och kopierar (`cmake`).
3. **`.glazeui` och GlazeBuilder-regionen redigeras bara i grammatikens form** — en sats per rad, ingen logik, inga kommentarer efter satser. Startlogik i `onLoad`, aldrig i konstruktorn (`three-files`, `glazeui-grammar`).
4. **Designer och kod är likvärdiga.** Allt en komponent kan ska gå att skriva som satser i `.glazeui`: välj platta former (kollektionsvokabulären, settrar, `menuBar.addItem`) framför uttryck med returvärden (`collections`).
5. **Mått i logiska px**, aldrig `* scale`. Placera med dock, anchors och syskonankare hellre än spikade koordinater (`layout`).
6. **Text som `TrText`:** `setText(glaze::tr(id))` i kod, talet (`glaze::tr(1001)`) i `.glazeui`; posta `setLanguage` ur popup-handlers (`language`).
7. **Händelser är `(sender, args)`:** värden ur sender, nollställning med `= nullptr`; programmatiska settrar fyrar inga events (`events`).
8. **Bara `<glaze/glaze.h>`.** Inga `gfx/`-includes och inga backend-typer (Win32, AppKit, Wayland, X11, EGL) i appen (`gfx`).
9. **Utseendet är fyrdelat:** färger ur `Theme::Colors`, mått ur `Style`, ritning via `Draw::` — inga hårdkodade färger eller tal i en egen widget (`custom-widget`, `appearance`).
10. **Dialogerna är våra:** `glaze::messageBox`, `glaze::FileDialog`, `glaze::ColorDialog` och `glaze::inputBox`, aldrig OS:ets (`message-box`, `file-dialog`, `color-dialog`, `input-box`).
11. **Trådar:** bara `glaze::app().post` får anropas från en annan tråd; bakgrundsarbete går genom `glaze::threads::Worker` (`app`, `threads`).
12. **Fönstren ägs av appen:** medlemmar eller lokala variabler; `close()` är uppskjuten; modal = `setModal(true)` + `run(owner)` (`app-window`).
13. **Länka `glaze`, aldrig `glaze_impl`;** registrera egna widgets i exens egna källfiler (`cmake`, `custom-widget`).
14. **Luft och linjering enligt [UI_GUIDELINES.md](UI_GUIDELINES.md):** skalan 4 · 8 · 12 · 16 · 24, aldrig under 8 px mellan klickbara kontroller eller mot en klippande kant (fokusringen och skuggan ritas utanför kontrollen), 16 px marginal, ingen satt `height` på kontroller.
15. **Verifiera:** bygg med noll varningar, kör glaze-render på varje handredigerad triplett och röktesta med `timeout` (`start`, `glaze-render`, `testing`).

### Fallgropar
- Säger manualen och en header olika saker gäller headern.
- Före v1.00 ändras API:t utan deprecation: pinna en tagg och läs `VERSION.md` (`version`).
- Okänt i `.glazeui` bevaras men utförs inte (regel 4) — ett skyddsnät för filer från en nyare version, ingen ursäkt för en form previewn inte kan utföra.
<!-- END:rules -->

<!-- BEGIN:app | app glaze-app application singleton pump event-loop händelseloop post thread-safe trådsäker ui-tråd main-thread windows fönsterregister find userscale användarskala zoom kalibrering setshowindock setpostbudget postbudget postkö budget setwheelinvert setcanvasbinding -->
## `glaze::app()` — pumpen, posten och app-globala inställningar (`app`)

**Alias:** app, glaze-app, application, singleton, pump, event-loop, händelseloop, post, thread-safe, trådsäker, ui-tråd, main-thread, windows, fönsterregister, find, userscale, användarskala, zoom, kalibrering, setshowindock, setpostbudget, postbudget, postkö, budget, setwheelinvert, setcanvasbinding
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [app.h](Glaze/widgets/include/glaze/app.h), [include/glaze/CLAUDE.md](Glaze/widgets/include/glaze/CLAUDE.md)

`glaze::App` är loop-ägaren: EN per process, en nåbar Meyers-singleton som skapas lat vid första `show()`/`run()`. Den äger den händelsestyrda pumpen över alla fönster (den sover i `gfx::waitEvent` när inget ändrats), fönsterregistret och de app-globala inställningarna. Appen deklarerar den aldrig och pumpar aldrig själv — `AppWindow::run()` gör det.

### API
- `App& app();` — singletonen. Första anropet skapar App och initierar plattformen.
- `void post(std::function<void()> fn);` — den ENDA glaze-funktionen som får anropas från en annan tråd: `fn` körs i UI-tråden, i pumpens svans (efter dispatch, före uppskjutna stängningar), i postordning inom postköns budget. En postad funktion som HANN köras i varvet stänger i samma varv (`close()` i den tas där).
- `void setPostBudget(int ms);` · `int postBudget() const;` — hur länge svansen högst kör appens postade funktioner per varv, förval 4 ms; 0 = obegränsat (allt som låg i kön när varvet började), ett negativt tal loggas och ändrar inget. Varvets första funktion körs alltid, och det som inte hann körs i nästa varv — efter en ritning och FÖRE allt som postats sedan. En skur av trådarnas svar fördelas så över bildrutorna i stället för att frysa en. Ramverkets egna poster för nästa bild (ritytans tick, kamerans händelse, `onResize`) går i en egen kö som körs först och inte räknas. App-global, från UI-tråden.
- `const std::vector<AppWindow*>& windows() const` · `std::size_t windowCount() const` · `AppWindow* find(const std::string& title) const;` — de öppna fönstren (icke-ägande pekare).
- `float userScale() const;` · `void setUserScale(float s);` — appens zoom ovanpå OS-skalan, 0.5–2.0, app-global; ALLT växer. Appen äger lagringen.
- `void setDisplayCalibration(const std::string& id, float pxPerMm);` — appens mätning av en skärm (`AppWindow::display().id`) i fysiska px per mm; 0 tar bort. Ritytans mm följer.
- `void setCanvasMemoryBudget(std::size_t bytes);` (förval 256 MB — FÖRVALET PER RITYTA, `Canvas::setMemoryBudget` sätter en rityta egen, och taket för fönstrets bilder utanför ritytorna) · `void setCanvasUploadBudget(std::size_t bytes);` (förval 16 MiB per fönster och frame) — ritytans budgetar (`canvas`, `canvas-images`).
- `void setWheelInvert(InputDevice d, bool vertical, bool horizontal);` · `bool wheelInverted(InputDevice d, bool horizontal) const;` — rullningens riktning i HELA APPEN, för musens hjul och för styrplattan var för sig, lodrätt och i sidled: förvalet varje widget ärver när varken den eller en förfader satt något (`input`). Relativ mot OS:et. `Keyboard` loggas.
- `void setCanvasBinding(const std::string& mode, InputSource s, std::uint32_t mods, InputAction a, InputDirection dir = Normal, float speed = 1.0f);` · `void removeCanvasBinding(mode, s, mods);` · `void resetCanvasBindings();` (ramverkets förval, rader och vändningar) · `std::vector<InputBinding> canvasBindings() const;` · `void setCanvasSourceInvert(InputSource s, bool on);` · `void setCanvasActionInvert(InputDevice d, InputAction a, bool on);` — RITYTORNAS bindningstabell för hela appen, som varje rityta ärver rad för rad (`input`).
- FRIA funktioner i `glaze/app.h`, anropade i `main` FÖRE första fönstret: `void setAppId(const std::string& id);` (`app-icon`) · `void setShowInDock(bool show);` · `bool showInDock();` — false = ingen ikon i macOS-dockan, för en app som bor i menyradens systemfält (`tray-icon`). Windows och Linux har ingen docka: begäran lagras och false loggas en gång — fönstrets knapp i aktivitetsfältet styrs per fönster med `AppWindow::setShowInTaskbar`.

### Exempel
```cpp
class WorkWindow : public glaze::AppWindow {
public:
    WorkWindow() : glaze::AppWindow("Arbete", 320, 120)
    {
        status_.x = 16;
        status_.y = 16;
        root().adopt(status_);
        glaze::AppWindow::onLoad = [this] {
            worker_ = std::jthread([this] {
                const std::string svar = "Klart";   // tungt arbete i en annan tråd
                glaze::app().post([this, svar] { status_.setText(svar); });
            });
        };
    }

private:
    glaze::Label status_;
    std::jthread worker_;   // joinas när fönstret förstörs — innan appen rivs
};

void appGlobals()
{
    glaze::app().setUserScale(1.25f);                    // allt växer, app-globalt
    glaze::app().setPostBudget(8);                       // postkön: högst 8 ms per varv
    for (glaze::AppWindow* w : glaze::app().windows())   // de öppna fönstren
        gfx::log("öppet: %s", w->title().c_str());
    if (glaze::AppWindow* w = glaze::app().find("Arbete"))
        w->close();                                      // uppskjuten, pump-svansen
}
```

### .glazeui
Ingen form — `app()` är app-global drift och inget fönstertillstånd. Användarskalan, kalibreringen, postköns budget, rullningens riktning och ritytornas bindningar sätts i kod, typiskt i `main` eller `onLoad` ur appens sparade inställningar; `setAppId` och `setShowInDock` i `main`. (Per fönster, widget och rityta har riktningen och bindningarna en form — `input`.)

### Fallgropar
- Rör aldrig en widget från en annan tråd — posta. `post` invaliderar ingenting själv; det gör settrarna du anropar i lambdan.
- Alla trådar som postar måste vara stoppade eller joinade innan appen rivs vid processlut. En `std::jthread`-medlem deklarerad efter widgetarna joinas när fönstret förstörs. Vid nedstängningen stängs postkön: det som ligger kvar förstörs utan att köras, och en post efter det släpps tyst.
- En postad lambda som fångar `this` kan överleva sitt fönster om den köas efter att `run()` returnerat och en senare pump kör den. `glaze::threads::Worker` levererar aldrig efter ägaren — använd den för bakgrundsarbete (`threads`).
- Kön körs bara medan någon pumpar: utan `run()` ligger funktionerna kvar.
- Budgeten delar inte en lång funktion: en enda post som tar 50 ms tar 50 ms (den syns som profilerarens `postedMax`, `profiler`). Dela tungt arbete i en arbetstråd och posta bara resultatet.
- `glaze::threads::Worker`s fortsättningar går genom `post` och följer budgeten som allt annat.
- Två poster körs inte garanterat i samma varv — och en funktion som postas inifrån en postad körs tidigast i nästa varv, efter det som redan låg i kön. Behöver två ändringar synas i samma bildruta, gör dem i samma funktion.
- `setLanguage` ur en dropdowns eller menys handler ska postas, annars rivs popupen mitt i sin egen dispatch (`language`).
- `App::languageChangedAll()` är publik av tekniska skäl — appar anropar `glaze::setLanguage` i stället.
- `glaze::setShowInDock(false)` hör hemma först i `main`: då läser plattformen den vid starten och dockikonen blinkar aldrig till. Efter första fönstret gäller den direkt men kan lägga fönstren bakåt.
- `glaze::app().activeWindow()` är det fönster som har tangentbordsfokus (`AppWindow::isActive()`), nullptr när appen inte har det. En popup blir aldrig aktiv — ägaren är det.
<!-- END:app -->

<!-- BEGIN:app-window | app-window appwindow window fönster form dialog modal owner ägare onload onshow onclose run close closewithresult dialogresult setcontentsize topmost showintaskbar dold-start statusbar commitedits validate setminimumcontentsize setmaximumcontentsize setfixedcontentsize fast-storlek popup setpopuptakeskeys kommandopalett sökruta -->
## `AppWindow` — fönstret, livscykeln och dialogerna (`app-window`)

**Alias:** app-window, appwindow, window, fönster, form, dialog, modal, owner, ägare, onload, onshow, onclose, run, close, closewithresult, dialogresult, setcontentsize, topmost, showintaskbar, dold-start, statusbar, commitedits, validate, setminimumcontentsize, setmaximumcontentsize, setfixedcontentsize, fast-storlek, popup, setpopuptakeskeys, kommandopalett, sökruta
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [app_window.h](Glaze/widgets/include/glaze/app_window.h), [gfx/window.h](Glaze/gfx/include/gfx/window.h), [include/glaze/CLAUDE.md](Glaze/widgets/include/glaze/CLAUDE.md)

Basklassen varje fönster ärver. Konstruktorn skapar inget OS-fönster (tvåfas) — det öppnas i `show()`/`run()`. Fönsterobjekten ägs av appen (stack eller medlemmar); `close()` river OS-fönstret uppskjutet men raderar aldrig objektet. En modal dialog är ett vanligt `AppWindow` med `setModal(true)`, en lokal variabel och `run(owner)` — knapparna bär svaret (`Button::setDialogResult`) och fönstret har en standard- och en avbrytknapp (`setAcceptButton`/`setCancelButton`), som i WinForms.

### API
- `void show();` · `void show(AppWindow& owner);` (ÄGT: kaskadstängs med ägaren) · `void hide();` (FÖRE `show()`/`run()`: öppna DOLT — `onLoad` men inte `onShow`) · `int run();` · `DialogResult run(AppWindow& owner);` · `void closeWithResult(DialogResult r);` · `DialogResult dialogResult() const;` (svaret, också efter den ägarlösa `run()`; None före, och efter ett `onClose`-VETO) · `void close();` · `bool isOpen() const` · `bool isHidden() const` · `enum class DialogResult { None, OK, Cancel, Yes, No, Abort, Retry, Ignore };` (i `widget.h`)
- STANDARD- OCH AVBRYTKNAPPEN: `void setAcceptButton(Button& b);` · `void setAcceptButton(std::nullptr_t);` · `Button* acceptButton() const;` · `void setCancelButton(Button& b);` · `void setCancelButton(std::nullptr_t);` · `Button* cancelButton() const;` · `bool isDefaultButton(const Button& b) const;` — Enter trycker acceptknappen och Escape avbrytknappen (`performClick`: `onClick` + knappens `dialogResult`) när INGET närmare fokus tog tangenten: steget ligger SIST före Tab, efter den fokuserade widgeten, förfäderna och acceleratorerna. Enter med Ctrl/Alt/Cmd är inte vår (Ctrl+Enter når appen), Shift spelar ingen roll; Escape tas bara omodifierad. Knappen tar tangenten bara när den lever, sitter i fönstret, syns i trädet och är aktiverad. Är en popup öppen (en färg- eller datumväljare eller en dropdown som en handler öppnat med `open()`) får den Enter och Escape först, också när dess kontroll saknar fokus — Escape stänger popupen och inte dialogen. STANDARDKNAPPEN (den Enter trycker just nu — en fokuserad knapp, annars acceptknappen) ritas på accentytan; ett fönster utan acceptknapp ser ut som förut.
- `Handler<EventArgs> onLoad;` (EN gång, när OS-fönstret finns men INNAN det syns — startlogiken; plats, storlek och läge som sätts här gäller redan vid den första visningen, WinForms `Load`) · `onShow` · `onHide` · `Handler<CloseArgs> onClose;` (`e.cancel = true` stoppar hela kaskaden; `e.reason` säger varför — `glaze::CloseReason::UserClosing` (fönstrets egen stängning: krysset, Alt+F4, `close()`), `AppQuit` (appavslutet, Cmd+Q) eller `OwnerClosing` (ägaren stängs))
- `void setTitle(TrText t);` · `void setContentSize(int w, int h);` (ytan mellan namnlist och statusrad, räknas om vid stilbyte) · `void setClientSize(int w, int h)` · `void setMinimumSize(int w, int h);` · `void setMaximumSize(int w, int h);` — logiska px, 0 = obegränsat, min == max = fast storlek.
- I INNEHÅLLSMÅTT, som `setContentSize`: `void setMinimumContentSize(int w, int h);` · `void setMaximumContentSize(int w, int h);` · `void setFixedContentSize(int w, int h);` (innehållsytan exakt w × h och inte ändringsbar — `isFixedSize()`) · `gfx::Sizef minimumContentSize() const;` · `gfx::Sizef maximumContentSize() const;` — chromets höjd räknas på av ramverket och följer statusraden och stilen; `minimumSize()`/`maximumSize()` svarar alltid i klientmått. Klient- och innehållsformen utesluter varandra per gräns: den som sätts SIST gäller.
- `void setSizeToContent(SizeToContent s);` (`Manual`, `Width`, `Height`, `WidthAndHeight`) · `void setSizeToContentMode(SizeToContentMode m);` (`GrowAndShrink` = låst, förvalet · `GrowOnly` = innehållet som minsta storlek) — se fallgroparna.
- `void setModal(bool on)` · `void setPopup(bool on)` (utan fokus, stängs vid klick utanför) · `void setPopupTakesKeys(bool on);` + `bool popupTakesKeys() const;` (en popup som TAR TANGENTBORDET — en sökruta, en kommandopalett; före `show()`, se fallgroparna) · `void setTransparent(bool on)` (per-pixel-alfa, inget chrome) · `void setTopmost(bool on);` + `bool topmost() const;` · `void setShowInTaskbar(bool on);` + `bool showInTaskbar() const;` (false = ingen knapp i aktivitetsfältet — attribut, före `show()`; `tray-icon`) · `void setControlBox(bool on);` (och `setMinimizeBox`/`setMaximizeBox`) · `void minimize();` `maximize();` `restore();` · ctorns `gfx::Chrome`: `Custom` (förval, vår namnlist — `TopBar* topBar()`), `Native` (OS:ets), `None` — eller `setChrome(c)` före `show()`
- Ramen, startplatsen, läget och geometrin: `setChrome` · `setStartPosition` · `setLocation` · `setWindowState`/`windowState()` · `WindowGeometry geometry() const` (`window-placement`) · väntepekaren `setUseWaitCursor(bool)` (`cursors`) · en timer som hör till APPEN är `glaze::Timer` (`timer`)
- `StatusBar statusBar;` · `MenuBar menuBar;` — publika fält, dolda tills `show()`. `template <class T, class... Args> T* add(Args&&... args)` · `Widget& root()` · `template <class T = Widget> T* find(const std::string& name)`
- `void invalidate()` (full omritning) · `void setAnimating(bool on)` (vsync-takt tills du stänger av) · `gfx::Window& window()` → `startTimer(std::uint32_t id, std::uint32_t ms)`/`stopTimer(std::uint32_t id)`, ticken som `gfx::EventType::Timer` i `event()`
- `std::uint32_t modifierKeys() const` · `std::optional<gfx::Pointf> mousePosition() const` (pekarens senast kända position i klientytan, fysiska px; tom när den lämnat fönstret) · `bool isKeyHeld(gfx::Key k) const;` · `const gfx::DisplayMetrics& display() const` · `std::vector<Widget*> focusOrder();` · `void clearFocus();` (ingen widget har tangentbordet — WinForms `ActiveControl = null`; nästa Tab börjar på första stoppet)
- `void commitEdits();` — committa det som redigeras i den fokuserade widgetens kedja, SYNKRONT (`events`). Också en commit som en handler i kedjan gjorde asynkron (den flyttade fokus, och kompositen ovanför committade i sin fokusförlust) är levererad när den återvänder, och fönstret ritas om bara om någon handler körde. Spara-handlern i en verktygsknapp, ett menyval eller en accelerator börjar här: `spara.onClick = [this] { commitEdits(); skrivFil(); };`
- Protected krokar: `virtual void frame(const Ctx&)` (egen ritning under widgetarna, deterministisk) · `virtual bool event(const gfx::Event&)` (råa events; true = ätet + full omritning) · `virtual void overlay(const Ctx&)` · `virtual void onLanguageChanged()` · `virtual bool acceleratorFallback(gfx::Key k, std::uint32_t mods)` (en tangent ingen widget och ingen av fönstrets egna genvägar tog, före Tab — ett ägt verktygsfönster låter ägarens meny svara; true = tagen) · `virtual void ownerVisibilityChanged(bool shown)` (ägaren `hide()`:ades eller visades igen — ett verktygsfönster som ska följa sin ägare döljer och visar sig här)

### Exempel
```cpp
class AboutDialog : public glaze::AppWindow {
public:
    AboutDialog() : glaze::AppWindow("Om", 320, 140)
    {
        setModal(true);                // attribut sätts FÖRE run(owner)
        setMinimumSize(320, 140);
        setMaximumSize(320, 140);      // min == max = ej ändringsbar
        auto* ok = add<glaze::Button>(glaze::tr(glaze::str::Ok));
        ok->setDialogResult(glaze::DialogResult::OK);       // ett klick stänger med OK
        auto* avbryt = add<glaze::Button>(glaze::tr(glaze::str::Cancel));
        avbryt->setDialogResult(glaze::DialogResult::Cancel);
        setAcceptButton(*ok);          // Enter — och OK ritas som standard
        setCancelButton(*avbryt);      // Escape
    }
};

class EditorWindow : public glaze::AppWindow {
public:
    EditorWindow() : glaze::AppWindow("Editor", 640, 400)
    {
        statusBar.show();              // dold tills show()
        statusBar.adopt(clock_);
        auto* about = add<glaze::Button>("Om…");
        about->onClick = [this] {
            AboutDialog dlg;           // lokal variabel runt run(owner)
            if (dlg.run(*this) == glaze::DialogResult::OK)
                clock_.setText("OK");
        };
        glaze::AppWindow::onLoad = [this] { window().startTimer(kTick, 1000); };
        glaze::AppWindow::onClose = [this](glaze::Widget*, glaze::CloseArgs& e) {
            e.cancel = unsaved_;       // veto — stoppar hela kaskaden
        };
    }

protected:
    bool event(const gfx::Event& e) override
    {
        if (e.type == gfx::EventType::Timer && e.timerId == kTick)
            clock_.setText(std::to_string(++seconds_) + " s");   // settern invaliderar
        return false;                  // true = eventet ätet + full omritning
    }

private:
    static constexpr std::uint32_t kTick = 1;   // appens timer-id: 1 … 0x7FFFFFFF
    glaze::Label clock_;
    int  seconds_ = 0;
    bool unsaved_ = false;
};
```

### .glazeui
Nakna fönsteranrop och de inbyggda fälten — standard- och avbrytknappen med MEDLEMMEN som argument (ett bortdöpt namn ger ett kompileringsfel):
```cpp
    setContentSize(640, 400);
    setMinimumSize(480, 300);
    setMinimumContentSize(480, 260);      // eller i innehållsmått — sist satt gäller
    setFixedContentSize(360, 140);        // en dialog: innehållsytan låst
    setPopupTakesKeys(true);              // i en popup: tar tangentbordet
    setMaximizeBox(false);
    setAcceptButton(okBtn);
    setCancelButton(avbrytBtn);
    statusBar.show();
    okBtn.setDialogResult(glaze::DialogResult::OK);
    avbrytBtn.setDialogResult(glaze::DialogResult::Cancel);
    glaze::AppWindow::onClose = [this](glaze::Widget* s, glaze::CloseArgs& e) { closing(s, e); };
    glaze::AppWindow::onShow = [this] { shown(); };
```

### Fallgropar
- `window().startTimer` före `show()` är en no-op (inget OS-fönster än) — starta i `onLoad`/`onShow`. Timern dör med OS-fönstret och `onLoad` körs en gång per objekt, så ett fönster som visas igen startar om i `onShow`. Id 1 … 0x7FFFFFFF är appens. En timer som ska gå före fönstret, eller utan `event()`-krok, är `glaze::Timer` (`timer`).
- Returnera `false` ur `event()` för en timer-tick och låt settrarna invalidera; `true` ritar om HELA fönstret varje tick.
- Huvudfönstret körs med `run()`, övriga med `show(*this)` eller `run(*this)`; `run()` på ett öppet fönster loggar och returnerar 1.
- En lokal variabel är rätt bara för MODALA dialoger. Ett icke-modalt fönster måste vara en medlem — en lokal dör när handlern returnerar.
- Ändrar du ett layoutfält (`x`, `width` …) utanför en handler syns det först vid nästa omritning — anropa `invalidate()`.
- Wayland kan inte `setTopmost`, skärmpositioner eller av-minimering (`platforms`).
- En verktygsknapp, ett menyval och en accelerator tar inte fokus, så ett halvskrivet tal eller en öppen cell i egenskapslistan har inte committats när deras handler körs — anropa `commitEdits()` först. Ett rekursivt `commitEdits()` (Spara anropad ur en commits handler) levererar också det den yttre samlat, i ordning.
- Att RIVA fönstret i en handler (`delete`, en `unique_ptr` som nollas) är tillåtet: varje dispatchväg frågar fönstrets liv efter varje handler och slutar där. Ritningens krokar `frame()`/`overlay()` är undantaget. `close()` är ändå det rekommenderade — uppskjutet, med onClose-vetot, och fönstret kan visas igen.
- `appIcon` (programikonen och programmenyn) är ett publikt fält som `menuBar`/`statusBar`, men SYNLIGT som förval — GlazeUI-ikonen i namnlistens hörn och på OS-fönstret. `appIcon.setNoIcon(true)` för ingen ikon, `appIcon.hide()` för ingen i fönstret (`app-icon`).
- APPAVSLUTET (Cmd+Q, `Quit` från OS:et) frågar bara fönster UTAN ägare (`e.reason == AppQuit`); ett ägt fönster stängs med sin ägares kaskad (`OwnerClosing`, vad ägaren än stängs för), och ett veto i ägarens `onClose` stoppar också de ägda. Ett fönster som stänger till systemfältet vetar bara `UserClosing` — annars går appen inte att avsluta med Cmd+Q (`tray-icon`). Appens egen `close()` är också `UserClosing` (WinForms `Form.Close()`): en "Avsluta" som ska förbi vetot sätter en egen flagga.
- `hide()` FÖRE `show()`/`run()` — eller i `onLoad` — är DOLD START: OS-fönstret skapas utan att visas, `onLoad` fyras men inte `onShow`, och det `show()` som tar fram fönstret fyrar `onShow`. `close()` i `onLoad` stänger fönstret innan det synts (ingen `onShow`) — vetar `onClose` den stängningen visas fönstret efter vetot, utom om vetots handler anropar `hide()` —, och `show()` i `onLoad` gör ingenting (visningen kommer efter `onLoad`). Ett ägt fönster som `onLoad` öppnar med `show(*this)` behåller aktiveringen när ägaren sedan visas. En modal startar aldrig dold (begäran loggas). Utan systemfält (`glaze::TrayIcon::isSupported()` false — Linux) blir en dold app osynlig (`tray-icon`).
- `setShowInTaskbar(false)` läses när OS-fönstret skapas: efter `show()` loggas anropet och gäller nästa öppning. Windows ger fönstret ett dolt ägarfönster, så det står kvar i Alt+Tab; macOS (dockan är appens — `glaze::setShowInDock`) och Linux saknar formen och loggar. Ett dolt fönster har ingen knapp ändå.
- En OK som VALIDERAR sätter inget `dialogResult` på knappen — dess `onClick` kontrollerar värdena och anropar `closeWithResult(OK)` själv (filväljarens mönster), eller så valideras det i `onClose` (`e.cancel = true`). Ett veto nollställer resultatet, så ett senare kryss svarar None.
- Enter i ett textfält COMMITTAR fältet först (`onCommit`) och trycker standardknappen sedan — OK:s handler läser den committade texten. Öppnar en `onCommit` en dialog vid Enter klickas standardknappen när den stängts; validera i OK eller `onClose` i stället. Talfältet och datumväljaren gör likadant; ett `TextArea` tar Enter som en ny rad, en fokuserad knapp sitt eget, en lista med `onActivate` sin aktivering, och kryssrutan, radioknappen, den stängda dropdownen och färgväljaren SLÄPPER Enter till standardknappen (Space växlar och öppnar; färgväljaren öppnar också på Ned och F4).
- Standardknappens utseende följer ANVÄNDBARHETEN: en knapp under en container som döljer innehåll utan `hide()` (en infälld dockningspanel) tar inte Enter, men utseendet ritas om först vid nästa omritning av knappen. macOS Cmd+. (plattformens "avbryt") ingår inte. En Enter som hålls kvar efter att dialogen stängts når ägaren och kan trycka DESS standardknapp, som i Win32.
- `setSizeToContent(glaze::SizeToContent::Height)` (`Width`, `WidthAndHeight`, `Manual` = förvalet) låter fönstret FÖLJA innehållet: axeln räknas ur rotens barn (utom menyraden och statusraden) med bredden känd och följer när en rad visas eller döljs, en text ändras eller stilen byts. Raderna får vara DOCKADE, flöda UPPIFRÅN (`x`/`y`, syskonankare) och stå MOT NEDERKANTEN (en knapprad med `anchors.bottom` eller `anchorBottom` står under allt uppifrån), och ett SPÄNT barn (en lista mellan fälten och knapparna) bidrar med sitt mätta mått som minsta — ge det `height = 96`. LUFTEN mellan raderna uppifrån och knapparna är `bottom` på den sista raden uppifrån (`visa.bottom = 12;`); regeln står i `layout` ("När föräldern räknas ur innehållet"). En `Panel` utan eget mått bidrar med 0, `WidthAndHeight` mäter en radbruten etikett som en rad, och `Width` ensamt med rader mot nederkanten kan låta dem överlappa raderna uppifrån — använd `WidthAndHeight`. `setContentSize`/`setClientSize` ger den fria axeln, användarens drag vinner på den; maximerat anpassas inte. I `.glazeui`: `setSizeToContent(glaze::SizeToContent::Height);`.
- `setSizeToContentMode(glaze::SizeToContentMode::GrowOnly)` gör innehållet till fönstrets MINSTA storlek i stället för ett lås (WinForms `AutoSizeMode`): dialogen går att dra upp men aldrig så litet att innehåll döljs, och maximeringen är tillåten. Storleken på en anpassad axel är max(`setContentSize`/`setClientSize` eller den aktuella storleken, innehållet) — fönstret VÄXER när innehållet passerar det och KRYMPER ALDRIG SJÄLVT (en dold rad sänker golvet men lämnar storleken). Utan `setContentSize` startar det i konstruktorns 1024 × 640 — sätt alltid storleken (genererad kod gör det). Appens `setMaximumSize` gäller men aldrig under innehållet. Förvalet `GrowAndShrink` låser (min = max); ett byte i drift gäller direkt. I `.glazeui`: `setSizeToContentMode(glaze::SizeToContentMode::GrowOnly);`.
- `isActive()`, `focusedWidget()` (kan vara en komposits inre fält) och `tryShortcut(tangent, mods)` — ett eget ägt verktygsfönster låter ägarens genvägar svara med `bool acceleratorFallback(gfx::Key k, std::uint32_t m) override { return owner() && owner()->isOpen() && owner()->tryShortcut(k, m); }`.
- TEXT VINNER ÖVER GENVÄGEN: ett tryck som skriver text i ett fokuserat textfält når inte fönstrets genvägar, ägarens reserv eller standardknappen — en genväg `"M"` körs inte medan man skriver m i ett fält, och AltGr+2 kör ingen Ctrl+Alt+2. Ctrl-genvägar, F-tangenter, Delete och Enter går som förut; ett skrivskyddat fält släpper bokstavsgenvägen.
- GRÄNSERNA I INNEHÅLLSMÅTT (`setMinimumContentSize`/`setMaximumContentSize`/`setFixedContentSize`) är `setContentSize`s tvillingar: `setMinimumSize` talar KLIENTYTAN (namnlisten och statusraden ingår), de här innehållsytan — och klientgränsen räknas om när statusraden visas eller döljs och vid ett stilbyte, så innehållet står still. `setFixedContentSize(w, h)` är `setContentSize(w, h)` plus min = max i samma mått; lås upp med `setMinimumContentSize(0, 0)` och `setMaximumContentSize(0, 0)`. Ett negativt mått (och för den låsta storleken 0) loggas och ändrar ingenting. En axel som följer innehållet (`setSizeToContent`) behandlar dem som appens gränser.
- EN POPUP SOM TAR TANGENTBORDET (`setPopup(true)` + `setPopupTakesKeys(true)` före `show(ägare)`): en popup får aldrig OS-fokus, så tangenterna landar i ÄGARFÖNSTRET — attributet låter ägaren skicka varje tangent, släpp och tecken till popupens egen ingång medan den är öppen, så popupens fokuserade widget (`sok.focus()` efter `show`), Tab och `onKeyDown`/`onKeyUp` fungerar som i ett vanligt fönster, på alla plattformar. Ägaren ser dem inte — inte heller dess `event()`, fokus eller genvägar. ESCAPE som ingenting i popupen tog stänger den (en avbrytknapp i popupen vinner). En tangent som hölls när popupen öppnades släpps också hos ägaren. Vill popupen låta ägarens genvägar svara: `acceleratorFallback` som ett verktygsfönster. Färg- och datumväljarens popuper är byggda så.
<!-- END:app-window -->

<!-- BEGIN:window-placement | window-placement fönsterplats position startposition startplats centerscreen centerowner manual setlocation location setwindowstate windowstate maximerad minimerad maximized minimized geometry geometri windowgeometry spara-position restorebounds setchrome ramlös borderless splash screens skärmar -->
## Fönstrets plats, läge och ram (`window-placement`)

**Alias:** window-placement, fönsterplats, position, startposition, startplats, centerscreen, centerowner, manual, setlocation, location, setwindowstate, windowstate, maximerad, minimerad, maximized, minimized, geometry, geometri, windowgeometry, spara-position, restorebounds, setchrome, ramlös, borderless, splash, screens, skärmar
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [app_window.h](Glaze/widgets/include/glaze/app_window.h), [gfx/window.h](Glaze/gfx/include/gfx/window.h), [gfx/platform.h](Glaze/gfx/include/gfx/platform.h), [apps/demo/src/window_lab.cpp](apps/demo/src/window_lab.cpp), [apps/demo/src/splash_window.glazeui](apps/demo/src/splash_window.glazeui)

Var ett fönster hamnar när det öppnas, i vilket läge, med vilken ram — och hur appen sparar och återställer geometrin (WinForms `StartPosition`/`Location`/`WindowState`/`RestoreBounds`). Allt sätts FÖRE `show()`/`run()` eller i `onLoad`: fönstret skapas dolt, `onLoad` fyras, och startplatsen räknas om precis före den första visningen — så fönstret aldrig hoppar.

### API
- `void setChrome(gfx::Chrome c);` · `gfx::Chrome chrome() const;` — `Custom` (förval, vår namnlist), `Native` (OS:ets) eller `None` (ingen ram — en splash). Ett ATTRIBUT: efter `show()` loggas anropet och gäller nästa öppning. `topBar()` är nullptr utom vid `Custom`.
- `enum class StartPosition { Default, CenterScreen, CenterOwner, Manual };` · `void setStartPosition(StartPosition p);` · `StartPosition startPosition() const;` — `Default` = OS:et väljer; `CenterScreen` = mitt i arbetsytan på ägarens skärm, utan ägare primärskärmens; `CenterOwner` = mitt över ägarens klientyta (en dold eller minimerad ägare ⇒ som `CenterScreen`); `Manual` = `setLocation`s plats.
- `void setLocation(int x, int y);` — LOGISKA px, KLIENTYTANS hörn för eget chrome (förvalet) — storleksramen utanför den synliga kanten räknas bort (`platforms`); med `Native` på Windows ramens yttre hörn. Före den FÖRSTA VISNINGEN (före `show()`, i `onLoad` och i en dold start som inte visats): startplatsen (sätter `Manual`), klämd in i skärmens ARBETSYTA när fönstret visas. Efter: flyttar fönstret dit i normalläge, utan klämning; maximerat eller minimerat loggas och ignoreras.
- `enum class gfx::WindowState { Normal, Minimized, Maximized };` · `void setWindowState(gfx::WindowState s);` · `gfx::WindowState windowState() const;` — före den första visningen startläget (vid första synligheten, också efter en dold start — och `minimize()`/`maximize()`/`restore()` byter då startläget i stället för att visa fönstret); därefter = `minimize()`/`maximize()`/`restore()`. `windowState()` svarar med begäran före den första visningen och med det FAKTISKA läget sedan.
- `struct WindowGeometry { int x, y, width, height; gfx::WindowState state; bool hasPosition; };` · `WindowGeometry geometry() const;` — NORMALLÄGETS plats och klientyta (logiska px) och läget: begäran före den första visningen (också i `onLoad`), OS-fönstrets spårade normalruta när det är öppet (också under maximering) och ett KORT efter stängningen. `hasPosition` är false på Wayland.
- `std::vector<gfx::ScreenInfo> gfx::screens();` — `ScreenInfo { Rectf bounds; Rectf work; float scale; bool primary; }` i placeringsrymden; tom på Wayland och före första fönstret (`gfx`).

### Exempel
```cpp
int main()
{
    MainWindow win;
    if (const std::optional<glaze::WindowGeometry> g = loadGeometry()) {   // appens egen lagring
        if (g->hasPosition)
            win.setLocation(g->x, g->y);
        win.setClientSize(g->width, g->height);
        win.setWindowState(g->state == gfx::WindowState::Minimized
                               ? gfx::WindowState::Normal
                               : g->state);
    }
    const int r = win.run();
    saveGeometry(win.geometry());       // läsbar efter stängningen — KORTET
    return r;
}
```
Ett ramlöst, centrerat fönster utan knapp i aktivitetsfältet:
```cpp
SplashWindow::SplashWindow() : glaze::AppWindow("Startar", 420, 220)
{
    setChrome(gfx::Chrome::None);
    setStartPosition(glaze::StartPosition::CenterScreen);
    setShowInTaskbar(false);
}
```

### .glazeui
Nakna fönsteranrop, som previewn utför (offscreen-fönstret får aldrig plats och läge, men `setChrome(gfx::Chrome::None)` ger en bild utan namnlist):
```cpp
    setChrome(gfx::Chrome::None);
    setStartPosition(glaze::StartPosition::CenterScreen);
    setLocation(120, 80);
    setWindowState(gfx::WindowState::Maximized);
```

### Fallgropar
- **Återställ FÖRE `show()`/`run()` eller i `onLoad`** — fönstret syns först efter `onLoad` (WinForms `Load`), och startplatsen räknas om med den storlek `onLoad` gav, så `CenterOwner`/`CenterScreen` centrerar den riktiga storleken. I `onShow` och senare flyttar `setLocation` ett fönster som redan syns.
- Logiska px är placeringsrymden delad med PRIMÄRSKÄRMENS skala, linjärt över alla skärmar: exakt när skalan är ett heltal, ±1 px annars. Storleken räknas med skärmen fönstret hamnar på.
- Platsen är KLIENTYTANS hörn också när fönstret har en storleksram utanför den synliga kanten (Windows osynliga ram, Linux genomskinliga marginal): `geometry()` och `setLocation` talar samma hörn, så en sparad plats återställs exakt — räkna aldrig in ramens bredd själv.
- En sparad plats på en skärm som kopplats ur hamnar inne i den NÄRMASTE skärmens arbetsyta — klämningen sker bara vid öppningen; `setLocation` efter `show()` klämmer aldrig.
- Spara inte `Minimized` som startläge — exemplet byter till `Normal`. En MODAL startar aldrig minimerad (loggas), och popuper och offscreen-fönster får inget startläge.
- `geometry()`/`windowState()` efter stängningen svarar med KORTET tills fönstret öppnas igen — också om du satt en ny plats däremellan. Stängdes fönstret maximerat öppnas det nästa gång i normalstorleken, och maximerat bara om du ber om det.
- Wayland har ingen skärmrymd: `setLocation` och `setStartPosition` gör ingenting (loggas), `hasPosition` är false, och en minimerad start blir normal (`platforms`).
- `setChrome(gfx::Chrome::None)` har ingen namnlist att dra i och inga fönsterknappar — appen flyttar och stänger fönstret själv, eller låter det stå där startplatsen la det.
- Meddelanderutan och filväljaren öppnas mitt över ägaren av sig själva.
<!-- END:window-placement -->

<!-- BEGIN:timer | timer glaze-timer tick ontick interval intervall singleshot en-gång upprepad periodic qtimer winforms-timer settimeout setinterval polling klocka clock timerargs -->
## Appens timer — `glaze::Timer` (`timer`)

**Alias:** timer, glaze-timer, tick, ontick, interval, intervall, singleshot, en-gång, upprepad, periodic, qtimer, winforms-timer, settimeout, setinterval, polling, klocka, clock, timerargs
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [timer.h](Glaze/widgets/include/glaze/timer.h), [gfx/timer.h](Glaze/gfx/include/gfx/timer.h), [apps/demo/src/splash_window.glazeui](apps/demo/src/splash_window.glazeui), [apps/demo/src/window_lab.cpp](apps/demo/src/window_lab.cpp)

En timer som hör till APPEN och inte till ett fönster (WinForms `Timer`, Qt `QTimer`): den går före det första fönstret och efter det sista, och den väcker pumpen bara när den förfaller (regel 1). En ICKE-VISUELL MEDLEM (`glaze::Component`), så den står i `.glazeui` som en widget — men adopteras aldrig.

### API
- `Timer();` · `~Timer();` (stoppar) — varken kopierbar eller flyttbar. `std::string name;` (basens).
- `void setInterval(int ms);` · `int interval() const;` — förval `kDefaultIntervalMs` = 100, klampas till `kMinIntervalMs` = 10 (loggat); ett byte medan timern går startar om den.
- `void setSingleShot(bool on);` · `bool isSingleShot() const;` — en gång eller upprepad (förval).
- `void start();` · `void stop();` · `bool isRunning() const;` — `start()` skapar `glaze::App` lat och startar om en timer som redan går. Vägrar OS:et loggas det och `isRunning()` förblir false.
- `Handler<TimerArgs, Timer> onTick;` — SENDER ÄR TIMERN: `[this](glaze::Timer* s, glaze::TimerArgs& e)` eller `[this] { … }`. `struct TimerArgs : EventArgs {};`

### Exempel
```cpp
class ClockWindow : public glaze::AppWindow {
public:
    ClockWindow() : glaze::AppWindow("Klocka", 240, 80)
    {
        label_ = add<glaze::Label>("0 s");
        clock_.setInterval(1000);
        clock_.onTick = [this] { label_->setText(std::to_string(++seconds_) + " s"); };
        glaze::AppWindow::onShow = [this] { clock_.start(); };   // bara medan den syns
        glaze::AppWindow::onHide = [this] { clock_.stop(); };
    }

private:
    glaze::Timer  clock_;
    glaze::Label* label_   = nullptr;
    int           seconds_ = 0;
};
```
En gång, med omstart i handlern:
```cpp
    retry_.setInterval(2000);
    retry_.setSingleShot(true);
    retry_.onTick = [this](glaze::Timer* t, glaze::TimerArgs&) {
        if (!tryConnect())
            t->start();               // isRunning() är false här — starta om
    };
```

### .glazeui
Medlemmen `glaze::Timer stangTimer;` i regionen (aldrig adopterad) och raderna i kroppen — kopplingen bär sendertypen:
```cpp
    stangTimer.name = "stangTimer";
    stangTimer.setInterval(1500);
    stangTimer.setSingleShot(true);
    stangTimer.start();

    stangTimer.onTick = [this](glaze::Timer* s, glaze::TimerArgs& e) { stangTimerTick(s, e); };
```
Previewn skapar timern och sätter dess värden men startar den aldrig — ingen timer tickar i ett förhandsvisat fönster.

### Fallgropar
- Ticken levereras i pumpens svans, när NÅGON pump går — `run()` av ett synligt, dolt eller offscreen-fönster, eller en modal dialog. En timer som startas innan något fönster körs väntar och kommer sedan som EN tick.
- Förfaller timern flera gånger innan svansen hinner fram (en handler som tar längre tid än intervallet, ett fönsterdrag på Windows) blir det EN tick och aldrig en kö — mät tiden själv om du räknar.
- En en-gångs-timer är stoppad när handlern körs (`isRunning()` false) — starta om den där om den ska gå igen.
- En modal dialog i en handler pumpar vidare: timern (också den som öppnade dialogen) fortsätter ticka i dialogens pump. Stoppa den före `run()` om det inte ska ske. Ingen tick levereras två gånger.
- En handler får riva sin egen timer och stoppa, starta om eller riva en annan — en tick för en körning som inte längre gäller levereras aldrig.
- Timern invaliderar ingenting själv — settrarna du anropar gör det.
- Starta i `onShow` och stoppa i `onHide`/`onClose` om timern bara gäller ett synligt fönster: annars väcker den appen även när ingen ser resultatet.
- `window().startTimer` finns kvar för FÖNSTRETS egna tidsförlopp (`event()`-kroken, no-op före `show()`); `glaze::Timer` är appens.
<!-- END:timer -->

<!-- BEGIN:events | events händelser handler sender args onclick onchange onhover oncontextmenu högerklick nullptr lambda callback eventargs clickargs changeargs closeargs dubbelklick double-click hjul oncommit commit klar onfocus onfocuslost focusargs fokus focus enter leave lostfocus selectall onkeydown onkeyup tangent keyargs keydown keyup repeat cancelled ctrl-enter avbrott mousecancel onmousecancel onmousedown onmouseup mouseargs mus mouse closereason onlinkclick linkclickargs onexit ontitlechange -->
## Händelsekontraktet — `(sender, args)` (`events`)

**Alias:** events, händelser, handler, sender, args, onclick, onchange, onhover, oncontextmenu, högerklick, nullptr, lambda, callback, eventargs, clickargs, changeargs, closeargs, dubbelklick, double-click, hjul, oncommit, commit, klar, onfocus, onfocuslost, focusargs, fokus, focus, enter, leave, lostfocus, selectall, onkeydown, onkeyup, tangent, keyargs, keydown, keyup, repeat, cancelled, ctrl-enter, avbrott, mousecancel, onmousecancel, onmousedown, onmouseup, mouseargs, mus, mouse, closereason, onlinkclick, linkclickargs
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [widget.h](Glaze/widgets/include/glaze/widget.h), [CLAUDE.md](CLAUDE.md) (hård regel 6)

Varje event är ett fält av typen `Handler<Args>` med C#-formen `(Widget* sender, Args& e)`: `sender` är widgeten som avfyrade eventet, VÄRDEN läses ur sender (`checkbox.isChecked()`), och `e` bär bara det sender inte har — eller utdata som `e.cancel`. Ny event-data blir ett fält i args och aldrig en ny parameter, så handlers behöver aldrig skrivas om. Den parameterlösa formen `[this] { … }` finns bredvid. Sender är den som AVFYRADE också när den inte är en widget: systemfältets ikon har `Handler<ClickArgs, TrayIcon>`, så dess handlers tar `(glaze::TrayIcon* sender, glaze::ClickArgs& e)` (`tray-icon`), och appens timer `Handler<TimerArgs, Timer>` (`timer`). I `.glazeui` bär fullformen sendertypen: `[this](glaze::Timer* s, glaze::TimerArgs& e) { tick(s, e); }`.

**`onChange` och `onCommit` är två frågor.** `onChange` svarar på "ändrades värdet?", `onCommit` på "är användaren KLAR?" — och varje widget man matar in något i har båda, med samma args-typ. Den som räknar om något dyrt, validerar eller bygger om en lista lyssnar på `onCommit`. `onCommit` fyras bara när värdet ändrats sedan redigeringen började, och när båda fyras för samma handling kommer `onChange` först:

| Widget | `onChange` | `onCommit` |
|---|---|---|
| `TextInput`, `FilePicker`s fält | varje tangent | Enter (konsumeras inte) eller när fokus lämnar fältet |
| `TextArea` | varje tangent | när fokus lämnar fältet (Enter är en ny rad) |
| `SpinBox`, `DatePicker`s fält, `ColorPicker`s hexfält | när texten committas och vid varje steg | samtidigt |
| `Slider`, `Scrollbar`, `ColorPicker`s reglage | levande under draget | när musknappen släpps, och vid varje tangent- eller hjulsteg |
| `Checkbox`, `Radio`, `ToolButton` (växlingsbar), `Dropdown`, `ListBox` (val och kryss), `ListView`, `TreeView`, paletten, kalendern, filvalet i dialogen | vid valet | samtidigt |
| `PropertyGrid`, `DataGrid` | när ett värde sparas | samtidigt |

**`onFocus`/`onFocusLost` finns på varje widget och betyder fokus INOM** (WinForms `Enter`/`Leave`): en panel eller ett talfält får `onFocus` när fokus går in i det, oavsett vilken del som tar emot det, och `onFocusLost` när fokus lämnar hela delträdet. En flytt inom delträdet fyrar ingenting på det. Ordningen vid en flytt är `onFocusLost` innerst först, sedan `onFocus` ytterst först.

**`onKeyDown`/`onKeyUp` finns på varje widget** och får tangenten FÖRE widgetens eget beteende. Trycket går till FOKUSENHETEN: den fokuserade widgeten plus de närmaste förfäder som själva är ett Tab-stopp för sitt innehåll (talfältet, datumväljaren, datagriden), innerst först; utan fokus till fönstrets rot (`root().onKeyDown`, WinForms `Form.KeyDown`). `e.handled = true` tar tangenten — widgeten, förfäderna, menyns genvägar och Tab får den inte. OS:ets upprepningar (`e.repeat`) och släppet går till samma kedja som trycket, också om fokus flyttat — den som får en upprepning får släppet — och tappar fönstret fokus medan tangenten hålls kommer ett eget släpp med `e.cancelled = true`. Så skickar ett anteckningsfält med Ctrl+Enter och slår av ett läge när mellanslaget släpps.

**`onMouseDown`/`onMouseUp`/`onMouseCancel` finns på varje widget** (WinForms `Control.MouseDown`/`MouseUp`) med `MouseArgs` — knappen, punkten i widgetens LOGISKA px, modifierarna och `handled`. Fönstret fyrar dem för tryckets MÅL: widgeten som håller explicit capture, annars den aktiverade widgeten under pekaren, på tom fönsteryta fönstrets rot (`root().onMouseDown`). INGEN bubbling — en inaktiverad widget och dess förälder får ingenting. `onMouseDown` kommer FÖRE widgetens eget beteende (och efter att ett klick flyttat fokus): med `e.handled = true` TAR appen trycket, och widgeten får ingenting av det — inget klick, inget nedtryckt utseende, ingen markering, inget drag, ingen snabbmeny. `onMouseUp` kommer till samma widget när knappen släpps, före widgetens eget släpp; `handled` där tar SLÄPPET: widgeten som höll trycket får ett avbrott i stället för sitt släpp — en knapp klickas inte, delarens skikt återgår, och ett reglage committar inte (`onCommit` uteblir, men värdet det dragits till står kvar). `onMouseCancel` kommer i stället för `onMouseUp` när trycket slutar utan släpp (fokusförlust, OS:et tog musen, ett nytt tryck med samma knapp, högertrycket som blev snabbmenyn). Varje `onMouseDown` följs av exakt ett av dem — utom när widgeten tas ur trädet mitt i trycket.

### API
- `template <class Args, class Sender = Widget> class Handler` — `operator=(F&& f)` tar fullformen `(Sender*, Args&)` ELLER en parameterlös callable; `Handler& operator=(std::nullptr_t)` nollställer; `explicit operator bool() const`. Varje `Handler<X>` är `Handler<X, Widget>`; en klass med egna events som inte är en widget är sin egen `Sender` (`glaze::TrayIcon`).
- `struct EventArgs {};` · `ClickArgs` · `HoverArgs { bool entered; }` · `ChangeArgs` · `ActivateArgs` (dubbelklick/Enter på en post) · `FocusArgs` (tom — `sender` är den som fick eller tappade fokus) · `CloseArgs { bool cancel; CloseReason reason; }` (`UserClosing`/`AppQuit`/`OwnerClosing` — `app-window`) · `MouseArgs { gfx::MouseButton button; Point local; std::uint32_t mods; bool handled; }` (polymorf: ritytans `CanvasMouseArgs` ärver och lägger till världen och strålen) · `ContextMenuArgs { float x; float y; Widget* source; }` · `KeyArgs { gfx::Key key; std::uint32_t mods; bool handled; bool repeat; bool cancelled; }` (`key` den vikta tangenten — numpadens End är `End` —, `handled` utdata, `repeat` OS:ets upprepning medan tangenten hålls, `cancelled` släppet som fokusförlusten gav)
- På varje widget: `Handler<ClickArgs> onClick;` · `Handler<HoverArgs> onHover;` · `Handler<ContextMenuArgs> onContextMenu;` · `Handler<KeyArgs> onKeyDown;` · `Handler<KeyArgs> onKeyUp;` · `Handler<FocusArgs> onFocus;` · `Handler<FocusArgs> onFocusLost;` · `Handler<MouseArgs> onMouseDown;` · `Handler<MouseArgs> onMouseUp;` · `Handler<MouseArgs> onMouseCancel;` + drag-and-drops sex (`drag-drop`). Ett fält med polymorfa args tar också fullformen med en ÄTTLING (`[](glaze::Widget*, glaze::CanvasMouseArgs& e) { … }`): den körs bara när widgeten skickar den typen — på en annan widget loggas det en gång och handlern körs inte.
- Per widget: `onChange` ("värdet ändrades" heter alltid så), `onCommit` (tabellen ovan — samma args-typ som widgetens `onChange`), `ListBox::onCheck`, `ListView`/`TreeView::onActivate`, `TreeView::onExpand`/`onCollapse`, `TabControl::onTabClose`, `DataGrid::onChange`/`onCommit` (`CellChangeArgs`: rad och kolumn) och `DataGrid::onSelectionChange` (radvalet), ritytans `onPaint`/`onMouseMove`/`onMouseWheel`/`onDoubleClick`/`onTick`/`onGesture` … (`canvas`).
- Markeringen i textfälten: `selectAll()` och `setSelection(int begin, int end)` på `TextInput`, `TextArea`, `SpinBox`, `DatePicker` och `FilePicker` — byte-offset i UTF-8-texten, klampade och justerade till teckengräns, utan events.
- `virtual void commitEdit()` på varje widget — committa det som redigeras NU, synkront (basen gör ingenting; fälten, talfältet, datumväljaren, färgväljarens hexfält, filväljarfältet, egenskapslistan och datagriden overridar) · `void AppWindow::commitEdits()` — `commitEdit()` på den fokuserade widgeten och varje förfader, innerst först. Ett anrop i handlerkod och inget tillstånd: ingen `.glazeui`-form.
- Fönstret: `onLoad`, `onShow`, `onHide`, `onClose` — samma `Handler`, samma två former (`app-window`).

### Exempel
```cpp
class EventsWindow : public glaze::AppWindow {
public:
    EventsWindow() : glaze::AppWindow("Händelser", 400, 240)
    {
        auto* ok    = add<glaze::Button>("OK");
        auto* check = add<glaze::Checkbox>("Aktiv");
        check->y = 40;

        // Fullformen (sender, args) — värdet läses ur sender, inte ur args.
        check->onChange = [](glaze::Widget* sender, glaze::ChangeArgs&) {
            auto* c = static_cast<glaze::Checkbox*>(sender);
            gfx::log("aktiv: %d", c->isChecked() ? 1 : 0);
        };
        ok->onHover = [](glaze::Widget*, glaze::HoverArgs& e) {
            gfx::log("%s", e.entered ? "in" : "ut");
        };
        ok->onClick = [this] { close(); };   // den parameterlösa formen
        ok->onClick = nullptr;               // nollställ — aldrig `= {}`

        // Högerklicket: ROUTAD uppslagning — första förfader med handler svarar.
        root().onContextMenu = [](glaze::Widget*, glaze::ContextMenuArgs& e) {
            gfx::log("högerklick på %s", e.source ? e.source->name.c_str() : "tom yta");
        };

        // onChange vid varje tangent, onCommit när användaren är klar (Enter
        // eller fokus lämnar fältet) — och markera allt när fältet får fokus,
        // både vid Tab och vid klick.
        auto* namn = add<glaze::TextInput>("Anna");
        namn->y = 80;
        namn->onCommit = [](glaze::Widget* sender, glaze::ChangeArgs&) {
            gfx::log("klar: %s", static_cast<glaze::TextInput*>(sender)->text().c_str());
        };
        namn->onFocus = [namn] { namn->selectAll(); };

        // Tangenterna: fältets onKeyDown får tangenten FÖRE fältet. Ctrl/Cmd+Enter
        // skickar — utan Alt, för AltGr är Ctrl+Alt på Windows — och vanlig Enter
        // lämnas åt fältet (en ny rad). Upprepningen tas men skickar inte igen.
        auto* anteckning = add<glaze::TextArea>();
        anteckning->y = 120;
        anteckning->onKeyDown = [anteckning](glaze::Widget*, glaze::KeyArgs& e) {
            const bool ctrl = (e.mods & (gfx::kModCtrl | gfx::kModCmd)) != 0 &&
                              (e.mods & gfx::kModAlt) == 0;
            if (e.key != gfx::Key::Return || !ctrl)
                return;
            e.handled = true;   // ingen ny rad, ingen menygenväg
            if (!e.repeat)
                gfx::log("skicka: %s", anteckning->text().c_str());
        };
    }
};
```

### .glazeui
Båda formerna läses och skrivs tillbaka som de står; handlern är en medlemsfunktion deklarerad efter `#pragma endregion`:
```cpp
    check.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { checkChanged(s, e); };
    side.onContextMenu = [this] { sideMenu(); };
    namn.onCommit = [this] { namnKlart(); };
    namn.onFocus = [this](glaze::Widget* s, glaze::FocusArgs& e) { namnFokus(s, e); };
    namn.selectAll();
    anteckning.onKeyDown = [this](glaze::Widget* s, glaze::KeyArgs& e) { anteckningTangent(s, e); };
    anteckning.onKeyUp = [this] { anteckningSlapp(); };
    ok.onMouseDown = [this](glaze::Widget* s, glaze::MouseArgs& e) { okNer(s, e); };
    ok.onMouseCancel = [this] { okAvbrutet(); };
```
`onCommit`, `onFocus`, `onFocusLost`, `onKeyDown`, `onKeyUp` och de tre mushandlarna är kända events i grammatiken (båda formerna), och `selectAll()`/`setSelection(b, e)` är metodanropssatser som previewn utför.

### Fallgropar
- `onClick = {}` och en generisk lambda `[](auto...)` är tvetydiga och kompilerar inte — nollställ med `= nullptr`, skriv ut parametrarna.
- Programmatiska settrar (`setChecked`, `setSelectedIndex`, `setText` …) fyrar ALDRIG `onChange` eller `onCommit` — bara användaren gör det. En setter som BYTER värdet medan användaren redigerar flyttar commit-baslinjen, så appens värde räknas inte som användarens ändring; en handler som skriver tillbaka samma text äter inte användarens commit.
- `onFocus`, `onFocusLost` och den commit som kommer av att fokus lämnar ett fält är POSTADE: de körs i pumpens svans, efter fokusbytet och efter musknappens tryck som orsakade det, aldrig mitt i det. Därför fungerar `x.onFocus = [this] { x.selectAll(); };` vid klick — trycket lägger careten först, handlern markerar sedan — och ett tryck som GAV fokus väntar ut dragtröskeln, så en darrig hand mellan tryck och släpp rör inte markeringen (ett riktigt drag markerar från trycket). Fältets commit postas före fokuseventen och kommer alltså före `onFocusLost`.
- `onCommit` och fokuseventen fyras INTE när fönstret tappar OS-fokus (fokus står kvar i fältet), och inte när en fokuserad widget göms eller inaktiveras — ramverket flyttar inte fokus vid `hide()`/`disable()`. Klick, Tab, `focus()` och `unadopt` flyttar fokus och fyrar.
- Ett klick som INTE tar fokus — en `ToolButton`, ett menyval, en accelerator — committar ingenting av sig självt, för fokus står kvar i fältet. Talfältets `value()`, datumväljarens `date()` och egenskapslistans `rows()` bär då det förra värdet medan fältet visar det skrivna. En "Spara" därifrån börjar med `commitEdits()` (fönstrets, WinForms `ValidateChildren`): det som redigeras i den fokuserade widgetens kedja committas SYNKRONT, med `onChange`/`onCommit` som vanligt, innan nästa rad i handlern läser värdet.
- En handler får lossa eller riva widgeten som avfyrade den. Ramverket fyrar då inte det andra eventet i paret (`onCommit` efter `onChange`, `onClick` efter en kryssrutas växling) och rör den inte igen; köade events (en fokusförlusts commit, fokuseventen) slopas för en widget som lossats — också om den sätts in igen — eller vars fönster dött. Handlern själv kör färdigt med sina captures intakta, också när den river sin egen widget.
- En KOPIA av ett `Handler`-fält (`a.onClick = b.onClick;`) delar det anropsbara: en muterbar lambda har ETT tillstånd var den än anropas ifrån. Tilldelning skapar alltid ett nytt.
- Fokuseventen levereras i den ordning bytena skedde, också om en handler pumpar nästlat (en modal dialog ur `onFocusLost`) och fokus byts under tiden.
- En knapps `onClick` och fältets postade `onCommit` har ingen garanterad inbördes ordning (kommer trycket och släppet i samma pumpvarv körs `onClick` först). Läs värdet ur fältet i knappens handler — det är redan skrivet när fokus flyttat.
- `onContextMenu` är en ROUTAD uppslagning, ingen bubbling: första förfader med satt handler svarar och ÄR `sender`; den träffade widgeten är `e.source`, som kan vara `nullptr` (tom fönsteryta). `e.x`/`e.y` är fysiska px i fönstret — exakt vad `Menu::popupAt` tar (`menus`).
- Dubbelklick räknas av ramverket, för alla tre musknapparna: `onActivate` på listor och träd, `onDoubleClick` på ritytan (med `e.button`) och krokarna `mouseDoubleClick`/`middleDoubleClick`/`rightDoubleClick` i en egen widget. Hjul och styrplattans gester når widgetkrokar och ritytans `onMouseWheel`/`onGesture`; råa events finns i `AppWindow::event()`.
- Ett tryck vars släpp aldrig kommer — fönstret tappar fokus, OS:et tar musen (en systemdialog, Alt+Tab), eller ett nytt tryck med samma knapp kommer först — ger `onMouseCancel` (på varje widget) och kroken `mouseCancel` i en egen widget, ALDRIG ett `onMouseUp`. Spara i släppet, avbryt i avbrottet.
- `onKeyDown` når bara FOKUSENHETEN: en vanlig panel eller en rityta ser inte sina barns tangenter (ritytans WASD tar inget W ur ett fält på ritytan), och en komposit med två tabbstopp (`FilePicker`) ser inte fältets. `handled` tar tangenten som KOMMANDO — tecknet kommer ändå som text till ett textfält. En handler som flyttar fokus avslutar tangentens väg, och med en öppen popup i enheten (en utfälld vallista) körs inga `onKeyDown`-handlers.
- Ett `onKeyUp` utan föregående `onKeyDown` kan komma — tangenten trycktes i ett annat fönster, åts av en meny eller av `event()` — så handlern ska tåla det. Modifierartangenterna själva (`Key::Unknown`) paras aldrig och hålls aldrig: ett läge knutet till Ctrl läser `modifierKeys()`. En handler som tar vanlig Enter i ett `TextInput` tar också fältets commit vid Enter (den kommer då när fokus lämnar fältet).
- Efter en körd handler för en DISKRET händelse (ett klick, en tangent, ett val) ritas hela fönstret om — handlern behöver ingen `invalidate()`. De LÖPANDE händelserna är undantagen: ett drags rörelser (reglagets `onChange`) och ritytans `onTick`, `onCameraChange` (från ett drag, ett hjulsteg, en gest, en tröghet eller ett snäpp), `onGesture` och hjul, efter vilka bara widgeten respektive ritytan ritas om. Där ändrar handlern en annan widget genom dess sättare (`setText` …, som invaliderar den), och den som ändrat ett eget fält som en annan widgets ritning läser — en annan ritytas `onPaint` — anropar den widgetens `invalidate()` (`canvas-camera`).
- Musens modifierare finns i `MouseArgs::mods` för de tre mushandlarna; i övriga events (`onClick`, `onHover` …) läser du `appWindow()->modifierKeys()` mot `gfx::kModCtrl` …
- Ett tryck appen TOG (`handled` i `onMouseDown`) är appens till släppet: widgeten får varken rörelserna eller släppet. `handled` i `onMouseUp` efter ett taget tryck gör ingenting. En widget som tas ur trädet mitt i ett tryck får varken `onMouseUp` eller `onMouseCancel`.
- Ett mitten- eller högertryck på ett BARN fyrar barnets `onMouseDown`, inte förälderns — också när föräldern (en rityta) panorerar på det: lyssna på den widget man trycker på.
- `onResize` (varje widget, `ResizeArgs` med `previous` och `first`; storleken ur `sender->actualSize()`, logiska px) upptäcks i ritningen och levereras i pumpens svans: bildrutan där storleken ändrades ritas med det gamla innehållet, handlern körs före nästa. Mellanlägen sammanfogas (A → B → A är ingenting), den första kommer före fönstrets första synliga bildruta (efter `onLoad`, före `onShow`), och en dold widget eller en inaktiv dockningsflik får ingen. Under ett FÖNSTERDRAG på Windows och macOS kommer den först vid släppet; en delare levererar per rörelse. Det som måste följa i samma bildruta räknas i ritningen. En handler som håller fönstret ritande i 64 bildrutor i följd utan inmatning stoppas till nästa händelse (loggat).
- `KeyArgs::text` är texten trycket SKRIVER ("å", "@" med AltGr, "é" på e efter ´) — tom för modifierare, navigering, Enter/Tab/Escape/Backspace, en död tangent och Ctrl/Alt/Cmd-kombinationer; upprepningen bär den igen, `onKeyUp` har ingen. `handled` sväljer inte tecknet.
- `ContextMenuArgs` bär ingen nod eller rad: läs den ur KÄLLAN — `TreeView::contextPath()`, `ListBox`/`ListView::contextRow()` (högertrycket har valt den) — regel 6, och `e.source` är den träffade widgeten när handlern sitter på en förfader.
<!-- END:events -->

<!-- BEGIN:input | input inmatning bindningar bindings keymap mus mouse styrplatta touchpad trackpad hjul wheel rullning scroll vänd invert natural-scrolling naturlig-rullning shift-hjul sidled setwheelinvert setbinding setinputmode lägen modes cad orbit förstaperson firstperson -->
## Inmatningen — rullningens riktning och ritytans bindningar (`input`)

**Alias:** input, inmatning, bindningar, bindings, keymap, mus, mouse, styrplatta, touchpad, trackpad, hjul, wheel, rullning, scroll, vänd, invert, natural-scrolling, naturlig-rullning, shift-hjul, sidled, setwheelinvert, setbinding, setinputmode, lägen, modes, cad, orbit, förstaperson, firstperson
**Include:** `#include <glaze/glaze.h>` (vokabulären i `glaze/input.h`) · **CMake:** `glaze` · **Källa:** [input.h](Glaze/widgets/include/glaze/input.h), [widget.h](Glaze/widgets/include/glaze/widget.h), [canvas.h](Glaze/widgets/include/glaze/widgets/canvas.h), [app.h](Glaze/widgets/include/glaze/app.h)

Två saker, båda GLOBALA för appen som förval och ställbara PER WIDGET med arv:

1. **Rullningens riktning** i listor, tabeller, textfält, `Panel`, `Scrollbar`, `Slider`, `SpinBox`, menyer och dockningens paneler — lodrätt och i sidled, för musens hjul och för styrplattan var för sig. En widget som inte satt något ärver från närmaste förfader som satt något (till och med fönstrets rot), annars från appen. En popup (meny, rullgardinens lista, kalendern) är ett eget fönster och ärver från appen.
2. **Ritytans bindningar**: en tabell (läge, källa, modifierare) → (handling, riktning, fart). Förvalet är ramverkets gester (`canvas-camera`, `canvas-3d`); appens tabell ärvs av varje rityta RAD FÖR RAD, och en rityta som satt en rad med samma nyckel har sin egen.

**RIKTNINGEN ÄR RELATIV MOT OS:ET**: det OS:et redan vänt — naturlig rullning på macOS, i GNOME/KDE, styrplattans riktning i Windows — är redan tillämpat, och en vändning här vänder därifrån. Samma regel på alla tre OS; glaze vänder aldrig tillbaka OS:ets val. **Shift + mushjul rullar i sidled** på alla tre (hjulet ned = höger); ett äkta sidohjul och styrplattan byts aldrig. Ritytan följer sina bindningar och inte rullningens riktning, så zoom och rullning ställs var för sig.

### Vokabulären (`glaze/input.h`)
- `InputDevice { Mouse, Touchpad, Keyboard }` — musens hjul är det icke-precisa hjulet (`AppWindow::isPreciseWheel` false), styrplattan det precisa (tvåfingersdraget, och Magic Mouse som macOS märker så) plus nypningen och vridningen.
- `InputSource` — en AXEL, tecknet är källans +: `LeftDragX/Y`, `MiddleDragX/Y`, `RightDragX/Y` (logiska px, höger/ned +) · `WheelX/Y` (rader, höger/upp +) · `ScrollX/Y` (styrplattans innehållsrörelse i logiska px, höger/ned +) · `Pinch` (ln(skalan), isär +) · `Twist` (grader, medurs +) · `ArrowsX/Y` (ett steg, höger/ned +) · `PlusMinus` (plus +). `deviceOf(källa)` ger enheten.
- `InputAction { None, PanX, PanY, Zoom, Rotate, Tilt, Fly, Lift }` och `InputDirection { Normal, Inverted }`, samlade i `InputBinding { mode, source, mods, action, direction, speed }`.

| Handling | + betyder | Karta (`"map"`) | Orbit (`"orbit"`) | Förstaperson (`"firstPerson"`) |
|---|---|---|---|---|
| PanX / PanY | innehållet åt höger / nedåt | panorering, greppet följer | målplanets punkt följer | i sidled / lodrätt i bildplanet |
| Zoom | in | zoom mot pekaren (tangenterna: mitten, draget: trycket) | dolly mot målet | = Fly |
| Rotate | bäringen/azimuten ökar | bäring kring mitten (vridningen kring pekaren) | azimut kring målet | gir kring ÖGAT |
| Tilt | uppåt i elevationen | tilten minskar | elevationen ökar | blicken sänks |
| Fly | framåt | ingenting | längs blicken | framåt längs marken |
| Lift | uppåt | ingenting | längs upp-axeln | uppåt |

MÄNGDEN: ett px är ett px för panoreringen, ett hjulsteg ett pilsteg (`Style::canvasPanStep`); zoomen är en halv nivå per rad och en hel per tangent; vridningen 0,8°/px och 15° per pil, tilten 0,5°/px och 10° per pil; i förstapersonen en världsenhet per hjulsteg. `speed` multiplicerar.

### API
- **Widget** (varje widget): `void setWheelInvert(InputDevice d, bool vertical, bool horizontal);` · `void clearWheelInvert(InputDevice d);` (ärv igen) · `bool isWheelInverted(InputDevice d, bool horizontal) const;` (upplöst). En egen widget med egen hjultolkning overridar `virtual bool followsWheelDirection() const` (false = det råa hjulet, som ritytan).
- **AppWindow**: `setWheelInvert`/`clearWheelInvert` — på fönstrets rot, alltså för allt i fönstret utan egen.
- **App**: `setWheelInvert`/`wheelInverted` och `setCanvasBinding`/`removeCanvasBinding`/`resetCanvasBindings`/`canvasBindings`/`setCanvasSourceInvert`/`setCanvasActionInvert` (`app`).
- **Canvas**: `void setInputMode(std::string mode);` (`""` = automatiskt: `"map"` utan och `"orbit"` med en `Camera3D`; `"firstPerson"` väljer appen; ett EGET namn bär bara sina avvikelser och faller annars tillbaka på det automatiska) · `std::string activeInputMode() const;` · `void setBinding(mode, InputSource, std::uint32_t mods, InputAction, InputDirection = Normal, float speed = 1.0f);` · `void removeBinding(mode, source, mods);` · `void clearBindings();` · `std::vector<InputBinding> bindings() const;` (ritytans egna) · `std::optional<InputBinding> effectiveBinding(mode, source, mods) const;` (den som gäller, ärvd och med reserven — en inställningsdialogs fråga) · `setSourceInvert`/`clearSourceInvert(InputSource)` · `setActionInvert`/`clearActionInvert(InputDevice, InputAction)`.

UPPSLAGET: en rad matchar när dess modifierare är en DELMÄNGD av de tryckta (Cmd = Ctrl), och den mest specifika vinner — flest bitar, vid lika Alt före Shift före Ctrl. Ett DRAG läser raden vid varje rörelse med de aktuella modifierarna; hjulet, styrplattan och tangenterna per händelse. `setPannable`/`setZoomable`/`setRotatable`/`setPitchable` säger OM en handling får verka: ett drag tar då nästa mindre specifika rad, en händelse bubblar. `None` är obunden — gesten gör ingenting, når appens handler och bubblar — och skuggar de mindre specifika raderna för sin källa, också när ett drag faller tillbaka. Hjulet och styrplattan tas bara av en axel som RÖR SIG och har en verksam rad; en förmåga som stängs av mitt i ett drag stänger bara av sina handlingar. Med en påslagen `RightDragX/Y` tar ritytan högertrycket, och då öppnas ingen snabbmeny vid det. VÄNDNINGARNA är multiplikativa med radens riktning: KÄLLANS (`setSourceInvert(ScrollY, true)`, i alla lägen) och HANDLINGENS PER ENHET (`setActionInvert(Mouse, Zoom, true)` vänder hjulets zoom och ett zoomande musdrag; `Keyboard` + och −, `Touchpad` nypningen).

### Exempel
```cpp
int main()
{
    // Appens förval ur användarens inställningar: lodrätt omvänt mushjul,
    // och CAD-vana på varje rityta — mitten orbitar, vänsterdraget är ledigt.
    glaze::app().setWheelInvert(glaze::InputDevice::Mouse, true, false);
    glaze::app().setCanvasBinding("orbit", glaze::InputSource::MiddleDragX, 0, glaze::InputAction::Rotate);
    glaze::app().setCanvasBinding("orbit", glaze::InputSource::MiddleDragY, 0, glaze::InputAction::Tilt);
    glaze::app().setCanvasBinding("orbit", glaze::InputSource::LeftDragX, 0, glaze::InputAction::None);
    glaze::app().setCanvasBinding("orbit", glaze::InputSource::LeftDragY, 0, glaze::InputAction::None);
    StartWindow win;
    return win.run();
}

void StartWindow::onLoad(glaze::Widget*, glaze::EventArgs&)
{
    logg.setWheelInvert(glaze::InputDevice::Mouse, false, false);   // just den här listan som OS:et
    // Ett eget läge: vänsterdraget mäter (appens handlers), resten ärvs.
    karta.setBinding("mäta", glaze::InputSource::LeftDragX, 0, glaze::InputAction::None);
    karta.setBinding("mäta", glaze::InputSource::LeftDragY, 0, glaze::InputAction::None);
    karta.setBinding("orbit", glaze::InputSource::WheelY, 0, glaze::InputAction::Zoom,
                     glaze::InputDirection::Inverted, 1.5f);   // omvänd och snabbare
}

void StartWindow::matverktygClicked(glaze::Widget*, glaze::ClickArgs&)
{
    karta.setInputMode(karta.inputMode() == "mäta" ? "" : "mäta");
}
```

### .glazeui
Widgetens och ritytans former är satser — en rad per bindning, platt:
```cpp
lista.setWheelInvert(glaze::InputDevice::Mouse, true, false);
setWheelInvert(glaze::InputDevice::Touchpad, false, true);   // fönstret, naket
karta.setInputMode("orbit");
karta.setBinding("orbit", glaze::InputSource::ScrollX, gfx::kModCtrl | gfx::kModShift, glaze::InputAction::Rotate);
karta.setSourceInvert(glaze::InputSource::ScrollY, true);
karta.setActionInvert(glaze::InputDevice::Touchpad, glaze::InputAction::Zoom, true);
```
Previewn utför och lagrar dem. Appens globala (`App::…`) har ingen form — de är app-global drift (`app`).

### Fallgropar
- RELATIV, inte absolut: `setWheelInvert(Touchpad, true, …)` betyder "motsatt mot vad OS:et ger", så en användare med naturlig rullning i OS:et får vanlig rullning i appen. Glaze kan inte säga vad OS:et valt.
- Shift + mushjul är SIDLED för det som rullar men `WheelY` med Shift för ritytan — bind `WheelY` med `gfx::kModShift` om ritytan ska göra något eget med det. På macOS blir ett äkta sidohjul som rullas med Shift nedtryckt lodrätt.
- Ett drags plan läses vid varje rörelse: Shift mitt i ett mittendrag i orbiten byter från panorering till flygning. En handling som stängs av mitt i draget slutar verka vid nästa rörelse.
- Orbitens vänsterdrag med både vridning och tilt avstängda gör INGENTING (förr panorerade det) — sätt en rad om det ska panorera.
- `"firstPerson"` räknar med att kameran står där ögat är (`Camera3D::lookFrom`): giren och tilten vrider kring ögat, och flygningen flyttar ögat och målet lika mycket.
- En egen widget med egen hjultolkning (en karta i en egen widget) ska svara `followsWheelDirection() == false`, annars får den Shift-bytta och vända rader.
<!-- END:input -->

<!-- BEGIN:cursors | cursors pekare muspekare cursor pointer setcursor resetcursor pointershape closedhand stängd-hand grabbing hand wait vänta timglas hourglass hidden dold usewaitcursor setusewaitcursor ibeam crosshair -->
## Muspekaren (`cursors`)

**Alias:** cursors, pekare, muspekare, cursor, pointer, setcursor, resetcursor, pointershape, closedhand, stängd-hand, grabbing, hand, wait, vänta, timglas, hourglass, hidden, dold, usewaitcursor, setusewaitcursor, ibeam, crosshair
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [widget.h](Glaze/widgets/include/glaze/widget.h), [app_window.h](Glaze/widgets/include/glaze/app_window.h), [gfx/window.h](Glaze/gfx/include/gfx/window.h), [apps/demo/src/window_lab.cpp](apps/demo/src/window_lab.cpp)

Varje widget har en egen pekare — I-balken i ett textfält, handen över en länk, hårkorset över en rityta. `setCursor` sätter en annan för en widget (WinForms `Control.Cursor`), och `setUseWaitCursor` lägger väntepekaren över hela fönstret (WinForms `UseWaitCursor`). Båda syns DIREKT, utan att musen rör sig.

### API
- `enum class gfx::PointerShape { Arrow, IBeam, Hand, ResizeWE, ResizeNS, NotAllowed, Crosshair, Move, ClosedHand, Hidden, Wait };`
- `void Widget::setCursor(gfx::PointerShape s);` · `void resetCursor();` · `std::optional<gfx::PointerShape> cursor() const;` — gäller widgeten SJÄLV, aldrig barnen; en inaktiv widget visar pilen.
- `void AppWindow::setUseWaitCursor(bool on);` · `bool useWaitCursor() const;` — slår varje widgets form.

### Exempel
```cpp
class Editor : public glaze::AppWindow {
public:
    Editor() : glaze::AppWindow("Editor", 480, 320)
    {
        auto* grepp = add<glaze::Panel>();
        grepp->setBorder(true);
        grepp->setCursor(gfx::PointerShape::ClosedHand);
        auto* spara = add<glaze::Button>("Spara");
        spara->onClick = [this] {
            setUseWaitCursor(true);    // syns direkt
            startaSparning();          // en Worker — svaret i UI-tråden (threads)
        };
    }
    void sparat() { setUseWaitCursor(false); }   // ur Workerns fortsättning

private:
    void startaSparning();
};
```

### .glazeui
```cpp
    status.setCursor(gfx::PointerShape::Wait);
    knapp.resetCursor();
    setUseWaitCursor(true);
```

### Plattformarna
| | Windows | macOS | Linux X11 | Linux Wayland |
|---|---|---|---|---|
| `ClosedHand` | GlazeUI:s egen bild | `closedHandCursor` | GlazeUI:s egen bild | temats `grabbing` |
| `Hidden` | ingen pekare | ingen pekare | ingen pekare | ingen pekare |
| `Wait` | timglaset | **pilen** — macOS har ingen publik väntepekare | klockan | temats `wait` |
| `Move` | fyrpilen | den stängda handen | fyrpilen | temats flytta-pekare |

### Fallgropar
- Pekaren kommer från widgeten UNDER musen — en etikett ovanpå en panel visar etikettens pekare, inte panelens. Sätt formen på den widget musen träffar.
- Ett tryck äger pekaren: medan en widget hålls (eller fångar musen) visar den sin form också utanför sig själv.
- Ritytan visar själv hårkorset över en yta med gester, den stängda handen medan den panoreras och fyrpilen medan den vrids (`canvas-camera`).
- `setUseWaitCursor` hindrar ingen input — inaktivera det som inte får klickas.
<!-- END:cursors -->

<!-- BEGIN:layout | layout placering placement dock anchors ankare syskonankare sibling-anchor anchorleft anchortop name fill stretch centrera autosize autostorlek logiska-px tabindex bar spacer -->
## Placeringen — Dock, Anchors och syskonankare (`layout`)

**Alias:** layout, placering, placement, dock, anchors, ankare, syskonankare, sibling-anchor, anchorleft, anchortop, name, fill, stretch, centrera, autosize, autostorlek, logiska-px, tabindex, bar, spacer
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [widget.h](Glaze/widgets/include/glaze/widget.h), [bar.h](Glaze/widgets/include/glaze/widgets/bar.h), [CLAUDE.md](CLAUDE.md), [tests/layout_gate.cpp](tests/layout_gate.cpp)

WinForms-placering i LOGISKA px (DPI och användarskala läggs på av ramverket). Prioriteten i `resolve()`: en förälder som placerar själv (`Bar`) > `dock` > ankare per kant, där ett syskonankare vinner över förälderankaret på samma kant. Allt räknas om vid varje layout — ingen cachad layout att hålla i synk.

### API
- `enum class Dock { None, Left, Top, Right, Bottom, Fill };` — dockade barn skär remsor ur förälderns yta i BARNORDNING (= adopt-ordningen); `Fill` tar resten utan att förbruka den. Remsorna skär även för ICKE-dockade syskon, som bor i "resten".
- `struct Anchors { bool left = true; bool top = true; bool right = false; bool bottom = false; };` — dubbla ankare på en axel sträcker, inget ankare centrerar; `float x, y` och `float right, bottom` är avstånden.
- `struct SiblingAnchor { std::string to; float gap = 0; };` · `SiblingAnchor anchorLeft, anchorTop, anchorRight, anchorBottom;` — `b.anchorLeft = {"a", 8}` = b:s vänsterkant 8 px efter a:s högerkant; `anchorRight` = vänster om, `anchorTop` = under, `anchorBottom` = över.
- `std::string name;` — det syskonankare och `find<T>()` slår upp; måste vara satt på MÅLET.
- `float width = 0, height = 0;` — per AXEL: satt (> 0) vinner, 0 = AUTO ur stilen och innehållet. En AUTO-höjd som beror på bredden (en radbruten `Label`) räknas med den bredd LAYOUTEN ger — sträckt mellan två kanter eller dockad `Top`/`Bottom` — och syskonen ser samma höjd.
- `int tabIndex = -1;` — -1 = trädordningen; ett värde ≥ 0 sorteras mot de andras positioner.
- `Spacer* addSpacer(float grow = 1.0f);` på en `Bar` (`statusBar`, `ToolBar`) — baren placerar barnen i rad och IGNORERAR deras dock/anchors. En bar med `dock` Left/Right STÅR (`isVertical()`): barnen uppifrån och ned, var och ett med barens fulla bredd och sin egen höjd.

### När föräldern räknas ur innehållet
Ett fönster med `setSizeToContent` (`app-window`) och en `GroupBox` utan mått räknar sin storlek ur barnen (`Widget::childrenExtent`). Dockade remsor läggs på i barnordning. De odockade räknas med kanter åt BÅDA hållen:
- Rader som flödar UPPIFRÅN (`x`/`y`, `anchorTop`/`anchorLeft` mot syskon) bidrar med sin nederkant, och rader MOT NEDERKANTEN (`anchors.bottom`, `anchorBottom` mot en sådan) med sin höjd plus `bottom`. Höjden är de två FLÖDENA tillsammans: allt mot nederkanten står UNDER allt som flödar uppifrån — fälten och en knapprad nere till höger blir en dialog där knapparna aldrig hamnar över fälten.
- I sidled läggs ett barn från vänster och ett från höger ihop bara när de står i SAMMA RAD ("Namn" uppe till vänster och en "Hjälp"-länk uppe till höger); fälten uppifrån och OK nere till höger står i olika rader och ger den bredaste av dem.
- Ett SPÄNT barn (bundet i början och mot slutet — en lista mellan rubriken och knapparna) bidrar med sitt mätta mått som MINSTA: `lista.height = 96;` håller dialogen så hög att listan aldrig blir lägre. En `Panel` eller `ListBox` utan mått bidrar med 0.
- LUFTEN: `right`/`bottom` på ett barn bundet BARA uppifrån eller från vänster är avståndet efter det — `sista.bottom = 12;` ger 12 px mellan sista raden uppifrån och knappraden. `resolve` läser inte fälten på den fria sidan, så de syns bara här. `x`/`y` på ett barn bundet mot slutet räknas INTE (designern lämnar gamla värden kvar där).
- Ett DOLT led i en syskonkedja lämnar sitt hål också här, som i layouten. Obestämt (utan bidrag) är ett ankare mot ett DOCKAT syskon och ett omvänt par (vänsterkanten från höger, högerkanten från vänster).

### Exempel
```cpp
class LayoutWindow : public glaze::AppWindow {
public:
    LayoutWindow() : glaze::AppWindow("Layout", 640, 400)
    {
        auto* side = add<glaze::Panel>();
        side->dock  = glaze::Dock::Left;                // remsa längs vänsterkanten
        side->width = 180;                              // remsans tjocklek
        side->setBorder(true);

        auto* field = add<glaze::TextInput>();          // bor i "resten" bredvid side
        field->name    = "field";                       // syskonankare slår upp namnet
        field->x       = 12;
        field->y       = 12;
        field->anchors = {true, true, true, false};     // vänster + höger = sträck
        field->right   = 12;

        auto* save = add<glaze::Button>("Spara");
        save->name      = "save";
        save->x         = 12;
        save->anchorTop = {"field", 8};                 // 8 px under field

        auto* cancel = add<glaze::Button>("Avbryt");
        cancel->anchorTop  = {"field", 8};
        cancel->anchorLeft = {"save", 8};               // 8 px till höger om save

        auto* hint = add<glaze::Label>("Mitten");
        hint->anchors = {false, false, false, false};   // inga ankare = centrerad

        statusBar.show();                               // Bar: barnen i rad, flex
        statusBar.add<glaze::Label>("Klar");
        statusBar.addSpacer();                          // trycker resten åt höger
        statusBar.add<glaze::Label>("Rad 1");
    }
};
```

### .glazeui
Layoutfälten är RÅA fältrader (allt annat tillstånd är settrar):
```cpp
    side.dock = glaze::Dock::Left;
    side.width = 180;
    field.anchors = {true, true, true, false};
    field.right = 12;
    field.tabIndex = 0;
    save.anchorTop = {"field", 8};
    cancel.anchorLeft = {"save", 8};
    save.bottom = 12;                 // luften under raden när föräldern räknas ur innehållet
```

### Fallgropar
- För tätt: fokusringen (upp till 5 px) och Glazes skugga ritas UTANFÖR widgetens yta och kapas av en klippande förälder — håll minst 8 px mellan klickbara kontroller och mot kanten. Vilka tal man väljer står i [UI_GUIDELINES.md](UI_GUIDELINES.md).
- Ett spikat `x = 210` är bara sant i EN Style (knapphöjden är 26.7 px i Slate och 18 i Minimalistic) — ankra mot grannen, så följer layouten stilbytet.
- Syskonankare träffar bara TIDIGARE syskon i adopt-ordningen; framåtreferens, självreferens och okänt namn faller TYST tillbaka på `x`/`y`. Kontrollera ordningen och att målet har `name`.
- Containrar och listor (`Panel`, `ListBox`, `ListView`, `TreeView`, `PropertyGrid`, `Splitter`, `TabControl` …) har ingen autostorlek — ge dem mått eller docka/sträck dem.
- Ett dolt ankarmål räknas ändå (följaren lämnar ett hål, också när föräldern räknas ur innehållet); en dold dockad remsa skär ingenting.
- Ett gammalt `right`/`bottom` på ett barn bundet bara uppifrån eller från vänster ändrar ingenting i layouten men blir LUFT när föräldern räknas ur innehållet — ta bort det om det inte är menat.
- Layoutfälten har inga settrar: ändrar du dem utanför en handler, anropa `invalidate()`.
- `menuBar` (Top) och `statusBar` (Bottom) skär sina remsor ur innehållsytan, så inget hamnar under dem. Multiplicera aldrig med skalan i appkod.
- `actualSize()` är widgetens storlek i logiska px ur samma layout som ritningen, och `toWindow(punkt)`/`fromWindow(punkt)` räknar mellan widgetens logiska punkt (`MouseArgs::local`) och fönstrets FYSISKA px (`ContextMenuArgs`, `Menu::popupAt`) — tomt utan ett öppet fönster. Storleksändringen själv är `onResize` (`events`).
<!-- END:layout -->

<!-- BEGIN:collections | collections kollektioner kollektionsvokabulär additem setitem setitems addrow setrow setrows addcolumn addnode addfilter addlayer addshape items rader index -->
## Kollektionsvokabulären — `addX` · `setX(index, …)` · `setXs(lista)` (`collections`)

**Alias:** collections, kollektioner, kollektionsvokabulär, additem, setitem, setitems, addrow, setrow, setrows, addcolumn, addnode, addfilter, addlayer, addshape, items, rader, index
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [listbox.h](Glaze/widgets/include/glaze/widgets/listbox.h), [property_grid.h](Glaze/widgets/include/glaze/widgets/property_grid.h), [table.h](Glaze/widgets/include/glaze/widgets/table.h), [tree_view.h](Glaze/widgets/include/glaze/widgets/tree_view.h), [CLAUDE.md](CLAUDE.md) (hård regel 8)

Varje listbärande komponent talar SAMMA tre former, indexbaserat: `addX(…)` lägger till sist, `setX(index, …)` ERSÄTTER en befintlig post helt, `setXs(lista)` fyller om allt. Styckeformen är det designern kan skriva rad för rad i `.glazeui`, massformen det man skriver i kod — och båda ger exakt samma tillstånd. Index utanför ⇒ `gfx::log` och ingenting, aldrig tyst tillväxt.

### API
- `ListBox`, `Dropdown`: `void addItem(std::string text);` · `void setItem(int index, std::string text);` · `void setItems(std::vector<std::string> v);` (+ `ListBox::setChecked(int row, bool on)`).
- `PropertyGrid`: `void addRow(std::string name, std::string value = "", bool readOnly = false, std::string tag = "");` · `void setRow(int index, std::string name, std::string value = "", bool readOnly = false, std::string tag = "");` · `void setRows(std::vector<Row> rows);`
- `ListView`, `DataGrid` (basen `Table`): `void addColumn(TrText text, float width = 0.0f, Align align = Align::Left, std::optional<Align> headerAlign = std::nullopt);` · `setColumn(int index, …)` · `void setColumns(std::vector<Column> columns);` och `void addRow(std::vector<std::string> cells, ImageSource img = {}, std::optional<gfx::Color> color = std::nullopt);` · `void setRow(int index, std::vector<std::string> cells, ImageSource img = {}, std::optional<gfx::Color> color = std::nullopt);` · `void setRows(std::vector<Row> rows);` (+ `setRowColor(index, färg)` och `setCellColor(rad, kolumn, bakgrund, text)`)
- `TreeView` — samma tre med en STIG där listan har ett index: `TreePath addNode(const TreePath& parent, TrText text, ImageSource img = {}, std::optional<gfx::Color> color = std::nullopt);` · `void setNode(const TreePath& path, TrText text, ImageSource img = {}, std::optional<gfx::Color> color = std::nullopt);` · `void setNodes(std::vector<TreeNode> nodes);` (+ `setChecked`/`setSelected`/`setNodeColor(stig, …)`)
- `FileDialog`, `FilePicker`: `void addFilter(TrText name, std::string patterns);` · `void setFilter(int index, TrText name, std::string patterns);` · `void setFilters(std::vector<FileFilter> filters);`
- `TabControl` (sidorna): `TabPage& addPage(TrText text = {});` · `void setPage(int index, TrText text);` · `void setPages(std::vector<TrText> texts);` (`widget-tab-control`).
- `StatusBar` (fälten): `void addPanel(TrText text = {}, float width = 0.0f, bool fill = false);` · `void setPanel(int index, TrText text, float width = 0.0f, bool fill = false);` · `void setPanels(std::vector<StatusPanel> panels);` (+ `setPanelText(index, text)`, `widget-bars`).
- `Canvas`: `addLayer`/`setLayer`/`setLayers`, `addShape`/`setShape`/`setShapes` (lagrets index först, annars lager 0) och `addView`/`setView`/`setViews` (`canvas`). Lagren FLYTTAS med `bool moveLayer(int from, int to)` — `TreeView::moveNode`s konvention: `to` räknas i ordningen FÖRE flytten och ligger i [0, antal] (`canvas-layers`).
- `MenuBar` står UTANFÖR med flit: menyer är hierarkiska och adresseras med titelstig — `menuBar.addItem("Arkiv", "Spara", "Ctrl+S")` (`menus`).

### Exempel
```cpp
void fillCollections(glaze::ListBox& list, glaze::Dropdown& choice,
                     glaze::PropertyGrid& grid, glaze::ListView& files)
{
    list.setItems({"Alfa", "Beta"});           // setXs: fyll om allt
    list.addItem("Gamma");                     // addX: lägg till sist
    list.setItem(0, "ALFA");                   // setX: ersätt post 0 helt
    list.setItem(9, "finns inte");             // utanför: loggas, ingenting händer

    choice.setItems({"Liten", "Mellan", "Stor"});
    choice.setSelectedIndex(1);                // programmatiskt: inget onChange

    grid.setRows({{"Bredd", "120"}, {"Höjd", "80"}});
    grid.addRow("Djup", "40");
    grid.setRow(0, "Bredd", "160");            // ERSÄTTER hela raden

    files.addColumn("Namn", 160);
    files.addColumn("Storlek", 70, glaze::Align::Right);
    files.addRow({"Rapport.pdf", "12 kB"});
    files.setRow(0, {"Rapport.pdf", "13 kB"});
}
```

### .glazeui
Styckeformen som satser — previewn utför dem:
```cpp
    list.setItems({"Alfa", "Beta"});
    list.addItem("Gamma");
    list.setItem(0, "ALFA");
    grid.setRows({{"Bredd", "120"}, {"Höjd", "80"}});
    grid.addRow("Djup", "40");
    files.addColumn("Namn", 160);
    files.addRow({"Rapport.pdf", "12 kB"});
```

### Fallgropar
- `setX` är TILLDELNING: `grid.setRow(0, "Bredd", "160")` tappar radens gamla `tag`/`readOnly`, och `ListView::setRow` tappar radens sorteringsnycklar.
- Indexen är APPENS även när en `ListView` är sorterad — `setRow(3, …)` är samma rad före och efter en sortering.
- Listornas poster är `std::string`, inte `TrText`: fyll om dem i `onLanguageChanged()` vid språkbyte (`language`).
- `setSelectedIndex` och de andra programmatiska settrarna fyrar inget `onChange`.
- En egen komponent med en lista följer samma vokabulär — en avvikelse är ett fel, inte en variant.
- GCC:s `-Wextra` varnar för aggregat med utelämnade sista fält (`{"Bredd", "120"}`); formen är avsiktlig — lägg till `-Wno-missing-field-initializers`.
<!-- END:collections -->

<!-- BEGIN:widgets | widgets widget komponenter komponent kontroller controls lista översikt overview katalog catalog alla-widgets registreringsnamn typnamn verktygslåda toolbox palette -->
## Alla widgets — översikt (`widgets`)

**Alias:** widgets, widget, komponenter, komponent, kontroller, controls, lista, översikt, overview, katalog, catalog, alla-widgets, registreringsnamn, typnamn, verktygslåda, toolbox, palette
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [headers](Glaze/widgets/include/glaze/widgets/), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

Varje inbyggd widget är en klass i `glaze::` med en egen header — men appen inkluderar bara paraplyet `<glaze/glaze.h>`. Registreringsnamnet är typnamnet i registret (`makeWidget("button")`, GlazeBuilders palett). Tabellen pekar på avsnittet med API, exempel och `.glazeui`-form.

| Klass | Registreringsnamn | Avsnitt | Vad |
|---|---|---|---|
| `glaze::Label` | `label` | `widget-label` | Text; roll (färg och rubrikstorlek), justering, radbrytning |
| `glaze::Button` | `button` | `widget-button` | Tryckknapp med text och/eller ikon |
| `glaze::Link` | `link` | `widget-link` | Klickbar länk som öppnar `url` |
| `glaze::Checkbox` | `checkbox` | `widget-checkbox` | Kryssruta av/på |
| `glaze::Radio` | `radio` | `widget-radio` | Envalsknapp; grupp = syskon med samma `group` |
| `glaze::TextInput` | `textinput` | `widget-text-input` | Enradigt textfält, valfritt skrivskyddat |
| `glaze::TextArea` | `textarea` | `widget-text-area` | Flerradigt textfält med brytning och rullning |
| `glaze::MarkdownView` | `markdownview` | `widget-markdown-view` | Formaterad Markdown-text i en rullande yta (release notes, hjälp) |
| `glaze::Dropdown` | `dropdown` | `widget-dropdown` | Vallista i popup (ComboBox, DropDownList-läget) |
| `glaze::ListBox` | `listbox` | `widget-list-box` | Rullande lista, enkelval och valfria kryss |
| `glaze::Slider` | `slider` | `widget-slider` | Dragreglage |
| `glaze::SpinBox` | `spinbox` | `widget-spin-box` | Talfält med stegare |
| `glaze::Progress` | `progress` | `widget-progress` | Förloppsindikator, bestämd eller obestämd |
| `glaze::Scrollbar` | `scrollbar` | `widget-scrollbar` | Fristående rullningslist |
| `glaze::Picture` | `picture` | `widget-picture` | Bild ur `assets.bin` eller ur pixlar (`Bitmap`) |
| `glaze::Separator` | `separator` | `widget-separator` | Skiljelinje, vågrät eller lodrät |
| `glaze::Panel` | `panel` | `widget-panel` | Container: barn, klipp, ram, rullning |
| `glaze::GroupBox` | `groupbox` | `widget-group-box` | Ram med rubrik; storleken ur barnen |
| `glaze::FloatingPanel` | `floatingpanel` | `widget-floating-panel` | Panel med rubrik som användaren flyttar och storleksändrar; förankras på en rityta |
| `glaze::TabControl` | `tabcontrol` | `widget-tab-control` | Flikar; sidorna är `TabPage`-barn |
| `glaze::TabPage` | `tabpage` | `widget-tab-control` | En fliksida (en `Panel`) |
| `glaze::Splitter` | `splitter` | `widget-splitter` | Två rutor med dragbart skikt |
| `glaze::DockHost` | `dockhost` | `widget-dock` | Värd för dockningsbara paneler; layouten som sträng |
| `glaze::DockPanel` | `dockpanel` | `widget-dock` | Panel med rubrik, fäll- och stängknapp |
| `glaze::ToolBar` | `toolbar` | `widget-tool-bar` | Verktygsfält (en `Bar`, dockad överst) |
| `glaze::ToolButton` | `toolbutton` | `widget-tool-bar` | Platt verktygsknapp; växlingsbar, inget tabbstopp |
| `glaze::Bar` | `bar` | `widget-bars` | Flexrad: barnen packas vänster → höger |
| `glaze::TopBar` | `topbar` | `widget-bars` | Fönstrets namnlist (skapas av `AppWindow`) |
| `glaze::StatusBar` | `statusbar` | `widget-bars` | Statusrad; fältet `statusBar` på varje fönster |
| `glaze::Spacer` | `spacer` | `widget-bars` | Växande eller fast lucka i en `Bar` |
| `glaze::CaptionButton` | `captionbutton` | `widget-bars` | Fönsterknapp minimera/maximera/stäng |
| `glaze::MenuBar` | `menubar` | `menus` | Menyraden; fältet `menuBar` på varje fönster |
| `glaze::ListView` | `listview` | `widget-list-view` | Tabell med kolumnhuvud, sortering, val och textfärg per rad |
| `glaze::DataGrid` | `datagrid` | `widget-data-grid` | Tabell med redigerbara celler |
| `glaze::Table` | — (oregistrerad bas) | `widget-list-view` | Delad bas för `ListView` och `DataGrid` |
| `glaze::TreeView` | `treeview` | `widget-tree-view` | Träd; noderna är data, adress = indexstig; flerval, kryss och textfärg per nod |
| `glaze::PropertyGrid` | `propertygrid` | `widget-property-grid` | Tvåkolumners egenskapslista |
| `glaze::ColorPicker` | `colorpicker` | `widget-color-picker` | Färgfält med popup: palett, RGB, hex |
| `glaze::DatePicker` | `datepicker` | `widget-date-picker` | Datumfält (ISO) med kalender-popup |
| `glaze::FilePicker` | `filepicker` | `widget-file-picker` | Sökvägsfält + "…" som öppnar filväljaren |
| `glaze::Canvas` | `canvas` | `canvas` | Rityta: 2D- och 3D-scen med kamera |

Meddelanderutan, filväljardialogen, färgdialogen, inmatningsrutan och systemfältets ikon (`glaze::TrayIcon`) är inga widgets (se `message-box`, `file-dialog`, `color-dialog`, `input-box`, `tray-icon`), och menyer är data (se `menus`). En egen widget skrivs enligt `custom-widget`.

### API
- **Tillståndet är privat:** `setX(…)` / `x()` / `isX()`. Settern invaliderar, så en ändring från en timer, en annan tråd (via `glaze::app().post`) eller en handler syns direkt.
- **Programmatisk sättning fyrar aldrig `onChange` eller `onCommit`** — bara användarens handlingar gör det. Varje inmatande widget har båda: `onChange` när värdet ändrades, `onCommit` när användaren är klar. Värdet läses ur `sender`, inte ur args (se `events`).
- **Layoutfälten är råa publika fält:** `name`, `x`, `y`, `width`, `height`, `right`, `bottom`, `dock`, `anchors`, `anchorLeft/Top/Right/Bottom`, `tabIndex` (se `layout`).
- **`width`/`height` 0 = AUTO per axel** ur stilen och innehållet. Ingen autostorlek har `Panel`, `PropertyGrid`, `ListBox`, `ListView`, `DataGrid`, `TreeView`, `TabControl`, `Splitter` och `DockHost` (dimensionera dem eller docka dem); `Bar` har bara en standardhöjd.
- **Etiketternas text är `glaze::TrText`:** `setText("Hej")` eller `setText(glaze::tr(id))`, som följer språket (se `language`). Användarens text (`TextInput`, `TextArea`) och listornas poster (`items`, celler) är `std::string`.
- **Listor talar kollektionsvokabulären** `addX` · `setX(index, …)` · `setXs(lista)`; index utanför ⇒ `gfx::log` och ingenting händer (se `collections`).
- **Varje widget** har `enable()`/`disable()`, `show()`/`hide()`, `onClick`, `onHover`, `onContextMenu`, `setToolTip(…)` (se `tooltip`) och `setAllowDrag`/`setAllowDrop` (se `drag-drop`).
- **`.glazeui`-blocken** i avsnitten är satser ur `initializeComponent()`. Medlemmen deklareras som värde i GlazeBuilder-regionen i `.h` (`glaze::Label rubrik;`) och handlern utanför regionen (se `three-files`, `glazeui-grammar`). Skriv inga kommentarer i filen — GlazeBuilder genererar om den.

### Fallgropar
- **`-Wextra` varnar för ramverkets aggregatliteraler** (`{"Namn", 160}`, `{"Bredd", "120"}`): `-Wmissing-field-initializers` säger till om varje utelämnat sista fält, men formen är avsiktlig. Lägg till `-Wno-missing-field-initializers` om appen bygger med `-Wextra`.
- Spika inte `x`/`y` för en widget bredvid en annan — stilarna har olika mått. Använd syskonankare (`b.anchorLeft = {"a", 8};`), och sätt `name` på målet.
- `disable()` på en container kaskadar inte till barnen (`Panel`, `GroupBox`, `Splitter`, `DockHost`); kompositerna (`SpinBox`, `DatePicker`, `FilePicker`) tar med sin inredning.
- Ändras ett råt layoutfält utanför en händelsehandler syns det inte förrän något annat ritar om — anropa `invalidate()`.

<!-- END:widgets -->

<!-- BEGIN:widget-label | label etikett text statictext textlabel rubrik heading caption textblock qlabel wxstatictext align justering radbrytning wrap roll role widthgroup etikettkolumn sizegroup sharedsizegroup halo gloria kontur outline sethalo -->
## Label — etiketten (`widget-label`)

**Alias:** label, etikett, text, statictext, textlabel, rubrik, heading, caption, textblock, qlabel, wxstatictext, align, justering, radbrytning, wrap, roll, role, widthgroup, etikettkolumn, sizegroup, sharedsizegroup, halo, gloria, kontur, outline, sethalo
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/label.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

En rad text, eller flera med `setWrap(true)`. Färgen är en ROLL som följer temat, och `Role::Heading` ger en rubrik i stilens rubrikstorlek — det finns inget fontstorleksfält. Registreringsnamn `label`. Använd `TextInput` med `setReadOnly(true)` när användaren ska kunna markera och kopiera texten.

### API
- `explicit Label(TrText t = {})` — texten direkt i konstruktorn.
- `void setText(TrText t)` / `const std::string& text() const` — texten; getteren ger den UPPLÖSTA strängen.
- `const TrText& textToken() const` — identiteten (literal eller översättnings-id).
- `enum class Role { Text, Dim, Accent, Titlebar, Heading, Custom }` · `void setRole(Role r)` / `Role role() const`.
- `void setColor(gfx::Color c)` / `gfx::Color color() const` — fast färg; sätter rollen `Custom` själv.
- `void setAlign(Align a)` / `Align align() const` — `glaze::Align::Left/Center/Right` inom widgetens yta.
- `void setWrap(bool on)` / `bool wrap() const` — bryt på ordgräns mot bredden; `\n` bryter alltid. Höjden följer den bredd etiketten FÅR: en satt `width`, eller en bredd ur layouten (sträckt mellan två kanter, eller dockad `Top`/`Bottom`).
- `void setWidthGroup(std::string grupp)` / `const std::string& widthGroup() const` — en ETIKETTKOLUMN: etiketter med samma gruppnamn får den bredaste synliga medlemmens bredd som sin AUTO-bredd, i hela fönstret (också i olika gruppramar), så fälten som ankras efter dem står i linje i varje UI, stil och språk. Tomt = ingen grupp. Med `setAlign(glaze::Align::Right)` blir kolumnen högerställd.
- `void setHalo(std::optional<gfx::Color> col)` / `std::optional<gfx::Color> halo() const` — en GLORIA: en kontur runt texten i en egen färg, så text över en bild med både ljusa och mörka partier läses överallt. `std::nullopt` eller `{}` (förval) = ingen. Ritas i alla tre textlägen (en rad, `setWrap`, `setElide`); ge färgen full alfa.
- `void setHaloWidth(float px)` / `float haloWidth() const` — glorians bredd i logiska px; 0 (förval) = stilens `labelHaloWidth`, och den begränsas till stilens `overdraw`. Negativt loggas och ignoreras.

### Exempel
```cpp
class Etiketter : public glaze::AppWindow {
public:
    Etiketter() : glaze::AppWindow("Etiketter", 360, 200)
    {
        rubrik.setText("Inställningar");
        rubrik.setRole(glaze::Label::Role::Heading);   // storleken ur stilen
        rubrik.name = "rubrik";
        rubrik.x = 12;
        rubrik.y = 12;

        info.setText("En lång förklaring som bryts mot etikettens bredd.");
        info.setRole(glaze::Label::Role::Dim);
        info.setWrap(true);
        info.anchors = {true, true, true, false};   // sträckt: höjden följer
        info.x = 12;                                // bredden när fönstret
        info.right = 12;                            // ändrar storlek
        info.name = "info";
        info.anchorTop = {"rubrik", 6};

        summa.setText("1 234,50 kr");
        summa.setAlign(glaze::Align::Right);
        summa.width = 120;              // justering kräver en bredare yta
        summa.x = 12;
        summa.anchorTop = {"info", 6};

        root().adopt(rubrik);
        root().adopt(info);
        root().adopt(summa);
    }

private:
    glaze::Label rubrik, info, summa;
};

// EN ETIKETTKOLUMN över två gruppramar — appen mäter ingenting.
lblNamn.setWidthGroup("form");          // i ramen "Kontakt"
lblNamn.setBuddy("namn");               // lika hög som fältet
namn.anchorLeft = {"lblNamn", 8};       // fältet börjar efter kolumnen
lblOrt.setWidthGroup("form");           // i ramen "Adress"
lblOrt.setBuddy("ort");
ort.anchorLeft = {"lblOrt", 8};         // samma x som namn
```

### .glazeui
```cpp
rubrik.setText("Inställningar");
rubrik.setRole(glaze::Label::Role::Heading);
rubrik.x = 12;
rubrik.y = 12;
rubrik.name = "rubrik";
info.setWrap(true);
info.anchors = {true, true, true, false};
info.right = 12;
info.anchorTop = {"rubrik", 6};
summa.setAlign(glaze::Align::Right);
etikett.setColor(gfx::Color{40, 90, 180, 255});
etikett.setText(glaze::tr(1023));
lblNamn.setWidthGroup("form");
bildtext.setHalo(gfx::Color{255, 255, 255, 255});
bildtext.setHaloWidth(2);
root().adopt(rubrik);
```
Previewn utför alla raderna; `glaze::tr(id)` skrivs med NUMRET i `.glazeui`, och `setWidthGroup` tar bara en strängliteral (ett uttryck är en opak rad). `setHalo` tar `gfx::Color{…}`, `std::nullopt` eller `{}` — en bar `{255, 255, 255}` kompilerar inte till en optional och är en opak rad.

### Fallgropar
- AUTO-höjden med `setWrap(true)` följer BREDDEN: en satt `width`, en sträckt etikett (båda sidokanterna bundna) eller en `Top`/`Bottom`-dockad — och syskon som ankras under den flyttar med. En satt `height` vinner. I `Dock::Fill`/`Left`/`Right` och i en `Bar` ger ytan höjden: texten bryts mot bredden och klipps vid kanten. Utan bredd (varken satt eller sträckt) är etiketten så bred som texten — en rad.
- En sträckt radbruten etikett i en komposit som räknar sin AUTO-höjd ur barnen (`GroupBox` utan `height`) räknas som EN rad där: kompositens mått har ingen bredd att bryta mot än. Ge kompositen en höjd.
- `setAlign` syns bara när ytan är bredare än texten (satt `width`, dubbelankare eller dockad remsa).
- `setColor` aktiverar färgen direkt (rollen blir `Custom`); tillbaka till temat med `setRole(glaze::Label::Role::Text)`.
- Ingen fontstorlek per widget: rubrikstorlek = `Role::Heading`, resten är stilens.
- `setElide(true)` kortar EN rad med "…" vid etikettens bredd (satt, sträckt eller dockad) och visar hela texten som tooltip när den faktiskt kortats — en egen `setToolTip` vinner; med `setWrap(true)` gäller brytningen.
- `setVAlign(glaze::VAlign::Top)`/`Bottom` lägger texten mot kanten i en hög etikett (förval `Center`).
- Glorian sticker UTANFÖR etikettens yta (den växer inte med glorian) och ritas som förskjutna kopior av texten, en per punkt på en ring — för en etikett över en bild, inte för löpande text. `setHalo({})` är INGEN gloria, men `setColor({})` är svart.
- `setBuddy("namnFalt")` gör etikettens AUTO-höjd lika med ett syskons mätta höjd (på `name`), så "Namn" står mitt för fältet i varje UI och stil. En Label som granne ignoreras; ett okänt namn ger egen höjd.
- `setWidthGroup`: DOLDA medlemmar räknas inte (en dold flik i en `TabControl` är dold, en inaktiv dockningsflik inte), så kolumnen kan flytta när en sektion visas — ge en medlem en satt `width` (den räknas in som golv) om kolumnen ska stå still. Den egna etiketten räknas alltid. Räckvidden är fönstrets träd: två oberoende formulär i samma fönster tar olika namn.
- `setWidthGroup` mäter varje medlem som EN rad med sin roll — lägg inte brödtext med `wrap` i en kolumngrupp. Adopterar eller lossar du en grupperad etikett UTANFÖR en handler ritas de andra medlemmarna inte om förrän nästa omritning — anropa `invalidate()` på fönstret. En grupperad etikett söker igenom fönstrets träd vid varje mätning; hundratals grupperade etiketter i ett fönster kostar.

<!-- END:widget-label -->

<!-- BEGIN:widget-button | button knapp tryckknapp pushbutton commandbutton qpushbutton wxbutton ikonknapp imagebutton bildknapp onclick klick click -->
## Button — tryckknappen (`widget-button`)

**Alias:** button, knapp, tryckknapp, pushbutton, commandbutton, qpushbutton, wxbutton, ikonknapp, imagebutton, bildknapp, onclick, klick, click
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/button.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

En knapp med text, en ikon ur `assets.bin` eller båda. Den är ett tabbstopp och aktiveras med klick eller Space/Enter när den har fokus. Storleken är AUTO ur stilen (radhöjd + luft), så låt bli att sätta `height`. Registreringsnamn `button`; i ett verktygsfält används `ToolButton` i stället (se `widget-tool-bar`).

### API
- `explicit Button(TrText t = {})` — texten direkt.
- `void setText(TrText t)` / `const std::string& text() const` / `const TrText& textToken() const`.
- `void setImg(ImageSource img)` / `const ImageSource& img() const` — ikonen: `glaze::img("lista", "nyckel")` (se `assets`) eller en `glaze::Bitmap` ur pixlar (se `canvas-images`); en asset ritas i sin storleksklass, en Bitmap i stilens `iconFallback`.
- `void setImagePos(ImagePos p)` / `ImagePos imagePos() const` — `glaze::ImagePos::Left/Right/Above/Below`.
- `Handler<ClickArgs> onClick` — ärvd från `Widget`; fyras vid klick och Space/Enter.
- `void setDialogResult(DialogResult r)` / `DialogResult dialogResult() const` — inte `None` ⇒ ett klick stänger knappens fönster med `r` EFTER `onClick` (WinForms `Button.DialogResult`); stängningen är uppskjuten, så `onClose`-vetot gäller.
- `void performClick()` — klicka programmatiskt: `onClick` och sedan `dialogResult`, en no-op när knappen är inaktiverad (WinForms `PerformClick`). Fönstrets Enter/Escape går den här vägen.

### Exempel
```cpp
class Knappar : public glaze::AppWindow {
public:
    Knappar() : glaze::AppWindow("Knappar", 360, 140)
    {
        spara.setText("Spara");
        spara.setImg(glaze::img("toolbar", "ok"));   // ikon ur assets.bin
        spara.name = "spara";
        spara.x = 12;
        spara.y = 12;
        spara.onClick = [this] { status.setText("Sparat"); };

        avbryt.setText("Avbryt");
        avbryt.anchorLeft = {"spara", 8};            // 8 px höger om spara
        avbryt.y = 12;
        avbryt.onClick = [this](glaze::Widget*, glaze::ClickArgs&) { close(); };

        status.x = 12;
        status.anchorTop = {"spara", 12};

        root().adopt(spara);
        root().adopt(avbryt);
        root().adopt(status);
    }

private:
    glaze::Button spara, avbryt;
    glaze::Label  status;
};
```

### .glazeui
```cpp
spara.setText("Spara");
spara.setImg(glaze::img("toolbar", "ok"));
spara.setImagePos(glaze::ImagePos::Above);
spara.x = 12;
spara.y = 12;
spara.name = "spara";
avbryt.anchorLeft = {"spara", 8};
spara.onClick = [this](glaze::Widget* s, glaze::ClickArgs& e) { sparaClicked(s, e); };
avbryt.onClick = [this] { avbrytClicked(); };
```
Händelsekopplingen står efter fältsättningarna och före trädbygget (`root().adopt(spara);`). Den korta formen (`[this] { avbrytClicked(); }`) tas emot när kroppen är EXAKT ett parameterlöst anrop; skriv inga kommentarer i `.glazeui` — filen är genererad och kommentarer följer inte med när GlazeBuilder sparar. En dialogknapp bär sitt svar som setter-fält, och fönstret pekar ut den med ett naket anrop: `okBtn.setDialogResult(glaze::DialogResult::OK);` och `setAcceptButton(okBtn);` (`app-window`). `okBtn.performClick();` känns igen men utförs inte i previewn — den kör användarkod.

### Fallgropar
- STANDARDKNAPPEN är fönstrets: `setAcceptButton(knapp)` (Enter) och `setCancelButton(knapp)` (Escape) på `AppWindow` (`app-window`). Enter når den när fokus står i ett fält, en kryssruta, en lista utan `onActivate` m.fl.; en fokuserad knapp tar sitt eget Enter och ritas då som standard (accentytan) i stället för acceptknappen. Utan acceptknapp ser alla knappar ut som förut.
- `setCancelButton` sätter INTE knappens `dialogResult` — ge den `setDialogResult(Cancel)` eller en `onClick`.
- En OK som validerar har inget `dialogResult`: `onClick` kontrollerar och anropar `closeWithResult(OK)` själv.
- Ikonen kräver `glaze::loadAssets()` i `main`; en saknad nyckel ritar bara texten och loggas en gång.
- Tom text = bara ikon. En inaktiverad knapp tonar ikonen själv.
- `setIconSize(32)` ger ikonen en egen sida i logiska px (0 = AUTO: assetens storleksklass, för en Bitmap `iconFallback`).
- `setColor(gfx::Color{200, 40, 40})` färgar TEXTEN (plattan följer temat); `setColor(std::nullopt)` eller `{}` ger temat igen. Standardknappens accentyta vinner, och en inaktiv knapp dimmas. I `.glazeui` är raden en anropssats — en bar `{200, 40, 40}` kompilerar inte. (Labels `setColor({})` är svart: där tar sättaren en färg, inte en valfri.)

<!-- END:widget-button -->

<!-- BEGIN:widget-link | link länk hyperlink hyperlänk linklabel url href webblänk wxhyperlinkctrl openurl webbläsare browser -->
## Link — länken (`widget-link`)

**Alias:** link, länk, hyperlink, hyperlänk, linklabel, url, href, webblänk, wxhyperlinkctrl, openurl, webbläsare, browser
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/link.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

Klickbar text i temats länkfärg. Med `url` satt öppnas adressen i systemets standardprogram (`gfx::openUrl`) EFTER att `onClick` körts; utan `url` är den en ren klickhanterare. Fokuserbar som en knapp, och pekaren är handen. Registreringsnamn `link`.

### API
- `explicit Link(TrText t = {})`
- `void setText(TrText t)` / `const std::string& text() const` / `const TrText& textToken() const`.
- `void setUrl(std::string u)` / `const std::string& url() const` — tom = ingen webbläsare.
- `Handler<ClickArgs> onClick` — körs före `gfx::openUrl`.

### Exempel
```cpp
class Om : public glaze::AppWindow {
public:
    Om() : glaze::AppWindow("Om", 360, 120)
    {
        hemsida.setText("Projektets hemsida");
        hemsida.setUrl("https://github.com/holmbom/GlazeUI");
        hemsida.name = "hemsida";
        hemsida.x = 12;
        hemsida.y = 12;
        hemsida.onClick = [this] { besok.setText("Öppnar webbläsaren…"); };

        hjalp.setText("Visa hjälpen");   // ingen url = ren klickhanterare
        hjalp.name = "hjalp";
        hjalp.x = 12;
        hjalp.anchorTop = {"hemsida", 8};
        hjalp.onClick = [this] { besok.setText("Hjälp"); };

        besok.x = 12;
        besok.anchorTop = {"hjalp", 12};

        root().adopt(hemsida);
        root().adopt(hjalp);
        root().adopt(besok);
    }

private:
    glaze::Link  hemsida, hjalp;
    glaze::Label besok;
};
```

### .glazeui
```cpp
hemsida.setText("Projektets hemsida");
hemsida.setUrl("https://github.com/holmbom/GlazeUI");
hemsida.x = 12;
hemsida.y = 12;
hemsida.onClick = [this](glaze::Widget* s, glaze::ClickArgs& e) { hemsidaClicked(s, e); };
```

### Fallgropar
- `onClick` kan inte stoppa öppnandet. Ska appen avgöra själv: lämna `url` tom och anropa `gfx::openUrl` i handlern.
- Färgen är temats `link`/`linkHover`, inte `accent` — sätt ingen egen färg.

<!-- END:widget-link -->

<!-- BEGIN:widget-checkbox | checkbox kryssruta kryss checkruta checked bocka toggle switch qcheckbox wxcheckbox onchange ischecked tristate threestate indeterminate mellanläge checkstate -->
## Checkbox — kryssrutan (`widget-checkbox`)

**Alias:** checkbox, kryssruta, kryss, checkruta, checked, bocka, toggle, switch, qcheckbox, wxcheckbox, onchange, ischecked, tristate, threestate, indeterminate, mellanläge, checkstate
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/checkbox.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

En ruta med text som slås av och på med klick eller Space. Enter växlar INTE — den bubblar till fönstrets standardknapp, som i Win32, WinForms och macOS (`app-window`). `onChange` fyras när ANVÄNDAREN växlar; värdet läses ur `sender`. Rutan har ett TREDJE läge, mellanläget (ett streck — "några av dem"), som appen sätter med `setCheckState`; med `setThreeState(true)` når användaren det också (WinForms `ThreeState`: av → på → mellanläge → av). Registreringsnamn `checkbox`. Kryss i en lista görs med `ListBox::setShowChecks`, inte med en `Checkbox` per rad.

### API
- `explicit Checkbox(TrText t = {})`
- `void setText(TrText t)` / `const std::string& text() const` / `const TrText& textToken() const`.
- `void setChecked(bool on)` / `bool isChecked() const` — programmatiskt: inget `onChange`. `isChecked()` är sann också i mellanläget.
- `enum class CheckState { Unchecked, Checked, Indeterminate }` (i `glaze`) · `void setCheckState(CheckState s)` / `CheckState checkState() const`
- `void setThreeState(bool on)` / `bool isThreeState() const` — styr bara ANVÄNDARENS klick; förval av.
- `Handler<ChangeArgs> onChange` · `Handler<ChangeArgs> onCommit` — båda vid användarens växling, `onCommit` direkt efter.

### Exempel
```cpp
class Val : public glaze::AppWindow {
public:
    Val() : glaze::AppWindow("Val", 360, 120)
    {
        autospara.setText("Spara automatiskt");
        autospara.setChecked(true);
        autospara.name = "autospara";
        autospara.x = 12;
        autospara.y = 12;
        autospara.onChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            auto* cb = static_cast<glaze::Checkbox*>(s);   // värdet ur sender
            if (cb->isChecked())
                intervall.enable();
            else
                intervall.disable();
        };

        intervall.setText("Var femte minut");
        intervall.x = 32;
        intervall.anchorTop = {"autospara", 4};

        root().adopt(autospara);
        root().adopt(intervall);
    }

private:
    glaze::Checkbox autospara, intervall;
};
```

### .glazeui
```cpp
autospara.setText("Spara automatiskt");
autospara.setChecked(true);
autospara.x = 12;
autospara.y = 12;
intervall.disable();
autospara.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { autosparaChanged(s, e); };
allaLager.setThreeState(true);
allaLager.setCheckState(glaze::CheckState::Indeterminate);
```

### Fallgropar
- `isChecked()` är sann i mellanläget (WinForms och Qt säger samma) — fråga `checkState()` när de tre ska skiljas åt. Utan `setThreeState` går ett klick ur mellanläget till av.
- En "markera alla"-ruta räknar fram sitt mellanläge i barnens `onChange` (`setCheckState` fyrar inget, så handlarna väcker aldrig varandra).
- `setChecked` i `onLoad` fyrar inget — kör samma logik som handlern själv om något beror på värdet.
- `setColor(gfx::Color{…})` färgar texten och `std::nullopt` ger temat (`Button`s regel); rutan och bocken följer alltid temat.

<!-- END:widget-checkbox -->

<!-- BEGIN:widget-radio | radio radioknapp envalsknapp radiobutton optionbutton alternativknapp group grupp qradiobutton wxradiobutton enval -->
## Radio — envalsknappen (`widget-radio`)

**Alias:** radio, radioknapp, envalsknapp, radiobutton, optionbutton, alternativknapp, group, grupp, qradiobutton, wxradiobutton, enval
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/radio.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

En av flera. En grupp är SYSKON under samma förälder med samma `group`-sträng; tom sträng betyder alla radios i den föräldern (som WinForms). Gruppen är ETT tabbstopp, piltangenterna flyttar valet och Space väljer; Enter bubblar till fönstrets standardknapp (`app-window`). Registreringsnamn `radio`.

### API
- `explicit Radio(TrText t = {})`
- `void setText(TrText t)` / `const std::string& text() const` / `const TrText& textToken() const`.
- `void setChecked(bool on)` / `bool isChecked() const` — programmatiskt, inget `onChange`. Gruppen VISAR den radio som senast fick `setChecked(true)`; `isChecked()` svarar för det visade läget. Samma tillstånd i appen och i previewn, också när `.glazeui` kör sättarna före trädbygget.
- `void setGroup(std::string g)` / `const std::string& group() const`
- `Handler<ChangeArgs> onChange` · `Handler<ChangeArgs> onCommit` — fyras på den radio som BLEV vald, `onCommit` direkt efter.

### Exempel
```cpp
class Storlek : public glaze::AppWindow {
public:
    Storlek() : glaze::AppWindow("Storlek", 360, 140)
    {
        liten.setText("Liten");
        liten.setGroup("storlek");
        liten.setChecked(true);
        liten.name = "liten";
        liten.x = 12;
        liten.y = 12;

        stor.setText("Stor");
        stor.setGroup("storlek");       // samma grupp + samma förälder
        stor.name = "stor";
        stor.x = 12;
        stor.anchorTop = {"liten", 4};

        // onChange fyras bara på den radio som BLEV vald.
        auto vald = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            status.setText("Vald: " + static_cast<glaze::Radio*>(s)->text());
        };
        liten.onChange = vald;
        stor.onChange  = vald;

        status.x = 12;
        status.anchorTop = {"stor", 12};

        root().adopt(liten);
        root().adopt(stor);
        root().adopt(status);
    }

private:
    glaze::Radio liten, stor;
    glaze::Label status;
};
```

### .glazeui
```cpp
liten.setText("Liten");
liten.setGroup("storlek");
liten.setChecked(true);
stor.setText("Stor");
stor.setGroup("storlek");
liten.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { storlekChanged(s, e); };
stor.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { storlekChanged(s, e); };
```

### Fallgropar
- `setChecked(true)` på en annan radio byter det visade valet, men syskonens EGNA flaggor står kvar: `setChecked(false)` på den visade utan ett användarval emellan visar den som var satt före. En tom grupp = `setChecked(false)` på alla. Användarens val (klick, Space, pilarna) släcker syskonen — också ett val av den som redan visas, som inte fyrar något event.
- Två grupper i samma panel kräver olika `group`; radios i olika paneler är alltid olika grupper.
- Flytta gruppen i tabbordningen genom att sätta SAMMA `tabIndex` på alla medlemmar.
- `setColor(gfx::Color{…})` färgar texten och `std::nullopt` ger temat; ringen och pricken följer alltid temat.

<!-- END:widget-radio -->

<!-- BEGIN:widget-text-input | textinput textfält textbox edit lineedit input inmatning entry readonly skrivskyddad qlineedit wxtextctrl enradig singleline fält oncommit enter selectall setselection markering selection lösenord password passwordchar echomode ångra undo redo gör-om canundo -->
## TextInput — det enradiga textfältet (`widget-text-input`)

**Alias:** textinput, textfält, textbox, edit, lineedit, input, inmatning, entry, readonly, skrivskyddad, qlineedit, wxtextctrl, enradig, singleline, fält, oncommit, enter, selectall, setselection, markering, selection, lösenord, password, passwordchar, echomode, ångra, undo, redo, gör-om, canundo
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/textinput.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

En rad redigerbar UTF-8-text med caret, markering (Shift+pil, musdrag, Ctrl+A), Home/End, urklipp (Ctrl+C/X/V) och ångra/gör om (Ctrl+Z, Ctrl+Y, Ctrl+Shift+Z) — genvägarna är Cmd på macOS. `onChange` kommer vid varje tangent, `onCommit` när användaren är KLAR — Enter eller att fokus lämnar fältet. Markeringen går att sätta från koden. `setReadOnly(true)` visar ett värde som går att markera och kopiera men inte ändra — skilt från `disable()`. `setPassword(true)` gör det till ett lösenordsfält, `setPlaceholder` ger en dämpad hint i det tomma fältet och `setMaxLength` en gräns i tecken. För flera rader: `TextArea`. Registreringsnamn `textinput`.

### API
- `explicit TextInput(std::string t = "")`
- `void setText(std::string t)` / `const std::string& text() const` — användarens text, ingen `TrText`. En text som BYTS tömmer ångra-historiken; samma text är en no-op.
- `void setReadOnly(bool on)` / `bool isReadOnly() const`
- `void setPassword(bool on)` / `bool isPassword() const` — lösenordsfältet: texten visas som stilens mask (`Style::passwordMask`, "•") en gång per kodpunkt, medan `text()`, careten och markeringen är den riktiga texten. Kopiera och klipp ut är avstängda (tangenterna konsumeras och gör ingenting), och fältet har ingen ångra-historik.
- `void setMaxLength(int n)` / `int maxLength() const` — högst `n` tecken räknade i KODPUNKTER (tio åäö i ett fält för tio; en emoji är ett tecken); `0` = obegränsat (förvalet), negativt loggas och ignoreras. Gäller det användaren SKRIVER och KLISTRAR IN: en insättning kapas så att den ryms (markeringen räknas som frigjord), och ryms inget händer ingenting. `setText` och ångra kapar inte.
- `void setPlaceholder(TrText t)` / `const std::string& placeholder() const` / `const TrText& placeholderToken() const` — en dämpad hint (`textDim`) i ett TOMT fält, i fältets justering och kortad med "…"; syns också MED FOKUS tills man skriver, och maskeras aldrig i lösenordsläge. `glaze::tr(id)` följer språket.
- `bool undo()` / `bool redo()` — samma steg som Ctrl+Z respektive Ctrl+Y/Ctrl+Shift+Z (Cmd på macOS); `true` om något ångrades/gjordes om. Båda fyrar `onChange`.
- `bool canUndo() const` / `bool canRedo() const` — släck en Redigera-menys post med dem; alltid `false` i skrivskyddat läge och i lösenordsläge.
- `void setAlign(Align a)` / `Align align() const` — `glaze::Align::Left/Center/Right`.
- `void setTextSize(Length size)` / `Length textSize() const` — fältets TEXTSTORLEK i stället för stilens: `glaze::px(18)` (logiska px) eller `glaze::mm(3)` (millimeter på skärmen, som ritytans etiketter — ett fält på en rityta får ritytans textstorlek); `0` = stilens (förvalet). Radhöjden, careten, markeringen, platshållaren och AUTO-höjden följer, luften och kanten är fältets. Världsenheter och negativt loggas och ignoreras. Samma form på `TextArea`, `ListBox` och `Dropdown`.
- `Handler<ChangeArgs> onChange` — vid VARJE användarändring (tangent, radering, klistra in, ångra/gör om).
- `Handler<ChangeArgs> onCommit` — Enter, eller att fokus lämnar fältet; bara när texten ändrats sedan redigeringen började (baslinjen sätts när fokus kommer, efter varje commit och när `setText` byter texten). Fokusförlustens commit postas till pumpens svans.
- `void selectAll()` · `void setSelection(int begin, int end)` — byte-offset i texten, klampade och justerade till teckengräns; ankaret på `begin`, careten på `end`. Inga events.
- `std::size_t caret() const`, `selBegin()`, `selEnd()`, `bool hasSelection() const` — läsvägar, index i byte.
- Ärvt från `Widget`: `onFocus`/`onFocusLost` (`events`).

### Exempel
```cpp
class Profil : public glaze::AppWindow {
public:
    Profil() : glaze::AppWindow("Profil", 360, 140)
    {
        namn.setText("Erik");
        namn.setPlaceholder("Ditt namn");   // syns när fältet töms
        namn.setMaxLength(40);              // 40 tecken, också vid inklistring
        namn.width = 220;               // AUTO-bredden är stilens fieldDefaultW
        namn.name = "namn";
        namn.x = 12;
        namn.y = 12;
        namn.onChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            hej.setText("Hej " + static_cast<glaze::TextInput*>(s)->text());
        };
        // Klar: Enter eller fokus lämnar fältet — bara om texten ändrats.
        namn.onCommit = [this] { setTitle("Profil — " + namn.text()); };
        // Markera allt när fältet får fokus, vid Tab OCH vid klick.
        namn.onFocus = [this] { namn.selectAll(); };

        id.setText("A-1042");
        id.setReadOnly(true);           // går att markera och kopiera
        id.setAlign(glaze::Align::Right);
        id.width = 220;
        id.name = "id";
        id.x = 12;
        id.anchorTop = {"namn", 8};

        hej.x = 12;
        hej.anchorTop = {"id", 8};

        losen.setPassword(true);        // "••••" — text() är den riktiga
        losen.width = 220;
        losen.name = "losen";
        losen.x = 12;
        losen.anchorTop = {"hej", 8};
        losen.onCommit = [this] { loggaIn(losen.text()); };

        // En Redigera-meny: menyraden stjäl inte fokus, så det fokuserade
        // fältet är det man arbetar i. Ctrl+Z i ett fokuserat fält går ALLTID
        // till fältet — menyns genväg körs bara när inget fält har fokus.
        menuBar.show();
        menuBar.addMenu("Redigera");
        menuBar.addItem("Redigera", "Ångra", "Ctrl+Z");
        menuBar.setHandler({"Redigera", "Ångra"}, [this] {
            if (namn.focused())
                namn.undo();
        });

        root().adopt(namn);
        root().adopt(id);
        root().adopt(hej);
        root().adopt(losen);
    }

private:
    void loggaIn(const std::string&) {}
    glaze::TextInput namn, id, losen;
    glaze::Label     hej;
};
```

### .glazeui
```cpp
namn.setText("Erik");
namn.setPlaceholder(glaze::tr(1830));
namn.setMaxLength(40);
namn.width = 220;
namn.x = 12;
namn.y = 12;
id.setReadOnly(true);
id.setAlign(glaze::Align::Right);
id.setTextSize(glaze::mm(3));
losen.setPassword(true);
namn.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { namnChanged(s, e); };
namn.onCommit = [this] { namnKlart(); };
namn.onFocus = [this] { namnFokus(); };
```
`namn.selectAll();`, `namn.setSelection(0, 4);`, `namn.undo();` och `namn.redo();` är metodanropssatser som previewn utför (historiken är tom där, som i `initializeComponent`). GlazeBuilder emitterar aldrig `undo`/`redo` själv.

### Fallgropar
- Ingen IME. Platshållaren är bara FÖRSTA raden (en `\n` i hinten klipper den), och maxlängden räknar kodpunkter, inte grafemkluster: e + U+0301 är två.
- MAXLÄNGDEN gäller inmatningen och aldrig appens data: `setText` med en längre text, en sänkt gräns under en längre text och ångra står kvar — skrivandet spärras och backsteg fungerar. En inklistring som inte ryms kapas på teckengräns utan att något sägs; vill appen säga till jämför den `text()` i `onChange`.
- ÅNGRA grupperar efter ORD och inte efter tid: "hej världen" är två steg ("hej " och "världen"), backsteg i följd ett, en inklistring ett eget. En caret- eller markeringsflytt, Enter och fokusförlust börjar ett nytt steg. Högst 100 steg och 4 MiB text. Ångra återställer markeringen — skrev man över en markering är den markerad igen.
- `setText` med en NY text tömmer historiken. En `onChange` som skriver om texten (versaler, formatering) tömmer den alltså vid varje tangent — som i WinForms. Det finns inget inbyggt textsläpp och ingen publik insättningsmetod: en text som appen sätter i `onDrop` (via `setText`) går inte att ångra.
- Genvägarna (A/C/X/V/Z/Y och Shift+Z) följer plattformen. Windows och Linux: Ctrl UTAN Alt — AltGr är Ctrl+Alt på Windows, så AltGr+Z skriver ż i fältet i stället för att ångra. macOS: Cmd UTAN Ctrl och UTAN Option — Option är Macens AltGr, och Cmd+Option+V är AppKits "klistra in utan format"; Ctrl+A/C/X/V är inte fältets där och går vidare till menyernas genvägar. `gfx::Key` är level-0-symbolen — på tysk layout är Ctrl+Z tangenten som skriver "z".
- I ett fokuserat fält konsumeras Ctrl+Z/Ctrl+Y (Cmd på macOS) ALLTID — också med tom historik, skrivskyddat och i lösenordsläge — så en Redigera-menys accelerator når aldrig fram medan ett fält har fokus. Kompositerna (`SpinBox`, `DatePicker`, `FilePicker`) har inga `undo()`-metoder; tangenterna når deras fält ändå.
- Lösenordsfältet: masken är stilens och inte appens val (ingen `setPasswordChar`), och ett grafemkluster av flera kodpunkter (e + U+0301) blir två prickar.
- `onChange` är LIVE per tangent; den som räknar om eller validerar lyssnar på `onCommit`. Enter (`gfx::Key::Return`, numpadens Enter inräknad) committar men KONSUMERAS INTE: tangenten går vidare till förfäderna, menyns genvägar och fönstrets standardknapp (`app-window`) efter committen, så Ctrl+Enter som "skicka" och OK i en dialog ser den committade texten. Fältets eget `onKeyDown` får tangenten FÖRE fältet (`events`): tar handlern vanlig Enter (`e.handled`) committas ingenting vid Enter, utan först när fokus lämnar fältet.
- Enter på en orörd text och en fokusflytt utan ändring fyrar ingen `onCommit` — eventet betyder "användaren ändrade och är klar", inte "fältet lämnades".
- `setSelection` räknar i BYTE i UTF-8-texten, inte i tecken: `"Höjd"` är fem byte, och `setSelection(0, 2)` markerar "H" (offset 2 ligger mitt i "ö" och justeras ned till teckengränsen).
- Ett tryck som GER fältet fokus blir ett markeringsdrag först bortom stilens `dragThreshold` och markerar då från trycket — så `onFocus → selectAll()` överlever en darrig hand. Ett tryck i ett fält som redan har fokus markerar som vanligt från första rörelsen.
- `setText` tar `std::string`; `setText(glaze::tr(id))` kompilerar inte här (det är etiketternas form).
- TEXTSTORLEKEN i mm följer SKÄRMENS mått (EDID och kalibreringen, `canvas-units`) och inte OS-skalan — px följer skalan. En storlek lika med stilens delar fönstrets font; varje annan laddar en egen, delad av alla widgets i fönstret med samma storlek.

<!-- END:widget-text-input -->

<!-- BEGIN:widget-text-area | textarea flerradig multiline anteckning anteckningsfält memo textedit richtext plaintextedit wxte_multiline textruta wordwrap radbrytning wrap oncommit selectall setselection markering ångra undo redo gör-om canundo -->
## TextArea — det flerradiga textfältet (`widget-text-area`)

**Alias:** textarea, flerradig, multiline, anteckning, anteckningsfält, memo, textedit, richtext, plaintextedit, wxte_multiline, textruta, wordwrap, radbrytning, wrap, oncommit, selectall, setselection, markering, ångra, undo, redo, gör-om, canundo
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/text_area.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

Flera rader UTF-8-text (`\n` skiljer raderna), med brytning (på från start), lodrät rullning och tangenterna man väntar sig: Upp/Ner, PageUp/PageDown, Home/End, Ctrl+Home/End, Enter = ny rad, Tab flyttar fokus, Ctrl+Z ångrar och Ctrl+Y/Ctrl+Shift+Z gör om (Cmd på macOS). Den ärver `Panel`, så remsorna och hjulet kommer därifrån — hjulet rullar även utan fokus. Registreringsnamn `textarea`.

### API
- `explicit TextArea(std::string t = "")`
- `void setText(std::string t)` / `const std::string& text() const` — radsluten normaliseras till `\n`. En text som BYTS tömmer ångra-historiken.
- `void setReadOnly(bool on)` / `bool isReadOnly() const`
- `void setWrap(bool on)` / `bool wrap() const` — av: raderna rullar i sidled.
- `void setMaxLength(int n)` / `int maxLength() const` — `TextInput`s kontrakt (kodpunkter, `0` obegränsat); en radbrytning är en kodpunkt, och Enter vid gränsen ger ingen rad.
- `void setPlaceholder(TrText t)` / `const std::string& placeholder() const` / `const TrText& placeholderToken() const` — `TextInput`s hint, radbruten mot textens bredd med `wrap` (annars kortad på första raden).
- `void setTextSize(Length size)` / `Length textSize() const` — `TextInput`s textstorlek (`glaze::px(…)`/`glaze::mm(…)`, `0` = stilens): raderna, careten, markeringen, tabbstoppen och AUTO-höjden (`heightForRows`) följer.
- `bool undo()` / `bool redo()` / `bool canUndo() const` / `bool canRedo() const` — `TextInput`s kontrakt: samma steg som tangenterna, `onChange` fyras, och fältet rullar till careten. Enter är skrivande, så ordet efter en radbrytning börjar ett nytt steg.
- `Handler<ChangeArgs> onChange` — vid varje användarändring (också ångra/gör om).
- `Handler<ChangeArgs> onCommit` — när fokus lämnar fältet och texten ändrats sedan det fick fokus (Enter är en ny rad, ingen commit). Postad till pumpens svans.
- `void selectAll()` · `void setSelection(int begin, int end)` — byte-offset, klampade och justerade till teckengräns; inga events, ingen rullning.
- AUTO-storlek: stilens `textAreaDefaultW` × `textAreaRows` rader.

### Exempel
```cpp
class Anteckning : public glaze::AppWindow {
public:
    Anteckning() : glaze::AppWindow("Anteckning", 480, 320)
    {
        text.setText("Första raden\nAndra raden");
        text.dock = glaze::Dock::Fill;  // fyller fönstret
        text.onChange = [this] { setTitle("Anteckning *"); };   // osparat
        text.onCommit = [this] { setTitle("Anteckning"); };     // spara här

        logg.setText("Skrivskyddad logg");
        logg.setReadOnly(true);
        logg.setWrap(false);            // rullar i sidled i stället
        logg.height = 80;
        logg.dock = glaze::Dock::Bottom;

        root().adopt(logg);             // dockade remsor skärs i adopt-ordning
        root().adopt(text);
    }

private:
    glaze::TextArea text, logg;
};
```

### .glazeui
```cpp
text.setText("Första raden\nAndra raden");
text.dock = glaze::Dock::Fill;
logg.setReadOnly(true);
logg.setWrap(false);
logg.setTextSize(glaze::px(11));
logg.dock = glaze::Dock::Bottom;
text.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { textChanged(s, e); };
root().adopt(logg);
root().adopt(text);
```
`\n` i literalen round-trippar byte-exakt.

### Fallgropar
- Dockade syskon skär i adopt-ordning: adoptera `Bottom`-remsan FÖRE `Fill`-fältet, annars täcker fältet remsan.
- Saknas: IME, justering, tabbtecken (`AcceptsTab`), ordhopp med Ctrl+pil, Shift+klick och lösenordsläge (bara `TextInput` har det). En hög radbruten platshållare klipps mot textytan — rullningen räknar den inte.
- Enter är fältets egen (en ny rad) och når därför inte en dialogs standardknapp — utom i ett SKRIVSKYDDAT fält, som inte har någon rad att infoga. Enter med Ctrl, Alt eller Cmd når den aldrig (den är appens, se sista punkten).
- Ångrans gruppering, historikens gränser och genvägarnas regler (Ctrl utan Alt, Cmd utan Ctrl och Option på macOS) är `TextInput`s — se fallgroparna där.
- `setSelection` rullar inte fram markeringen — den syns först när användaren flyttar careten eller rullar dit.
- Klistrad text får CRLF/CR → `\n` och tabb → blanksteg — utom med `setAcceptsTab(true)`, där tabben behålls.
- En tabb ritas till nästa TABBSTOPP (`Style::textAreaTabSpaces` blanksteg, räknat från radens början), också en tabb ur `setText`. `setAcceptsTab(true)` låter Tab skriva en tabb; Shift+Tab och Ctrl+Tab lämnar fältet, och i skrivskyddat läge släpps Tab.
- Enter med Ctrl, Alt eller Cmd är inte fältets: fältets `onKeyDown` får tangenten FÖRE fältet, så ett meddelandefält skickar med Ctrl+Enter och `e.handled = true` (exemplet i `events`). Pröva att Alt SAKNAS — AltGr är Ctrl+Alt på Windows — och låt `e.repeat` ta tangenten utan att skicka igen.

<!-- END:widget-text-area -->

<!-- BEGIN:widget-markdown-view | markdown markdownview md commonmark gfm richtext formaterad-text release-notes hjälp help dokument viewer html qtextbrowser wxhtmlwindow richtextbox länk onlinkclick linkclickargs bild tabell rubrik anchor ankare -->
## MarkdownView — formaterad text i en rullande yta (`widget-markdown-view`)

**Alias:** markdown, markdownview, md, commonmark, gfm, richtext, formaterad-text, release-notes, hjälp, help, dokument, viewer, html, qtextbrowser, wxhtmlwindow, richtextbox, länk, onlinkclick, linkclickargs, bild, tabell, rubrik, anchor, ankare
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/markdown_view.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

Visar ett Markdown-dokument — release notes, hjälp, en beskrivning — med rubriker, stycken, listor, fet/kursiv, kod, citat, linjer, länkar, bilder och tabeller, i en rullande yta. Delmängden är CommonMark 0.31.2:s kärna plus GFM-tabeller och nakna `http(s)://`-adresser. Den ärver `Panel`, så remsorna, hjulet (utan fokus), `setBorder` och klippet kommer därifrån. Registreringsnamn `markdownview`.

### API
- `explicit MarkdownView(TrText t = {})`
- `void setText(TrText t)` / `const std::string& text() const` (den upplösta texten) / `const TrText& textToken() const` — dokumentet. `setText(glaze::tr(N))` gör det översättbart; ett språkbyte tolkar om det av sig självt. En NY text rullar till toppen.
- `void setImage(const std::string& name, ImageSource img)` / `ImageSource image(const std::string& name) const` — vyns BILDTABELL, nycklad på namn; `{}` tar bort posten. `![alt](namn)` slår upp tabellen först och sedan `lista/nyckel` som asset — aldrig ett namn med schema (`https://…`): vyn läser inga filer och inget nät.
- `void scrollToAnchor(std::string anchor)` — rulla till rubriken med den slugen (GitHubs regel: "Kom igång" → `kom-igång`, dubbletter `-1`, `-2`), utan `#`. Löses vid nästa ritning, så ordningen mot `setText` spelar ingen roll.
- `std::string linkAt(float localX, float localY) const` — länken under en punkt i vyns lokala logiska px (`MouseArgs::local`), tomt utan länk eller fönster.
- `Handler<LinkClickArgs> onLinkClick` — när trycket OCH släppet träffar samma länk. `LinkClickArgs { std::string url; bool handled = false; }`: sätt `handled = true` för att ta länken själv. Annars förvalet: `#ankare` rullar, `http`/`https`/`mailto` öppnas i systemet, allt annat gör ingenting.
- `setProp`: `text` (+ Panels `border`/`clip`). AUTO-storlek: bredden `Style::markdownDefaultW`, höjden dokumentets i den bredden — en sträckt eller Top/Bottom-dockad vy växer med sitt innehåll (`heightForWidth`); med satt höjd eller `Dock::Fill` rullar den.

### Exempel
```cpp
class Hjalp : public glaze::AppWindow {
public:
    Hjalp() : glaze::AppWindow("Hjälp", 640, 480)
    {
        doc.setText("# Nyheter\n\n- **Snabbare** start\n- Se [installationen](#installation)\n\n"
                    "## Installation\n\nKör `make` och sedan [Inställningar](app:inst).\n\n"
                    "![](logo) [GlazeUI](https://github.com/holmbom/GlazeUI)");
        doc.setImage("logo", glaze::img("toolbar", "info"));
        doc.setBorder(true);
        doc.dock = glaze::Dock::Fill;
        doc.onLinkClick = [this](glaze::Widget*, glaze::LinkClickArgs& e) {
            if (e.url == "app:inst") {      // appens egna länkar: ta dem själv
                visaInstallningar();
                e.handled = true;
            }                               // #ankare och http(s) går till förvalet
        };
        root().adopt(doc);
    }

private:
    void visaInstallningar() {}
    glaze::MarkdownView doc;
};
```

### .glazeui
```cpp
hjalp.setText("# Nyheter\n\n- **Snabbare** start\n- Se [installationen](#installation)");
hjalp.setText(glaze::tr(1972));
hjalp.setImage("logo", glaze::img("toolbar", "info"));
hjalp.scrollToAnchor("installation");
hjalp.setBorder(true);
hjalp.onLinkClick = [this](glaze::Widget* s, glaze::LinkClickArgs& e) { hjalpLank(s, e); };
root().adopt(hjalp);
```
Dokumentet är EN strängliteral med `\n`, `\"` och `\\` (sammanslagna literaler och råsträngar går inte i grammatiken) — ett långt dokument bor bättre i språkfilen som `glaze::tr(N)`. `setImage` och `scrollToAnchor` utförs av previewn.

### Fallgropar
- HTML tolkas ALDRIG — `<b>` står som text; bara kommentarer `<!-- -->` tas bort. Genomstrykning, uppgiftslistor, fotnoter, matematik och emoji-koder står också som text, och `www.`- och nakna e-postadresser länkas inte.
- Länkarnas förval öppnar bara `http`, `https` och `mailto`: ett dokument utifrån kan bära `file:///…` eller `javascript:`, och de gör ingenting. Vill appen ha egna scheman (`app:…`) tar handlern dem med `handled = true`.
- Vyn är inte fokuserbar och har ingen markering eller kopiering — länkarna nås bara med musen.
- Ankaret är rubrikens slug ur den visade texten (gemener för ASCII och Latin-1, blanksteg → `-`, skiljetecken bort); procentkodade fragment avkodas inte, och flera blanksteg i rad blir ett.
- En tabell som inte ryms klipps (ingen vågrät rullning); kodblockens rader bryts. En fet stil som fonten saknar ritas i vanlig vikt.
- Byter en `Bitmap` storlek efter `setImage` syns det först vid nästa `setImage` eller text.

<!-- END:widget-markdown-view -->

<!-- BEGIN:widget-dropdown | dropdown vallista rullgardin combobox combo select dropdownlist choice wxchoice qcombobox picker items selectedindex popup -->
## Dropdown — vallistan (`widget-dropdown`)

**Alias:** dropdown, vallista, rullgardin, combobox, combo, select, dropdownlist, choice, wxchoice, qcombobox, picker, items, selectedindex, popup
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/dropdown.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

Ett fält som visar det valda och öppnar en lista i ett riktigt popup-fönster (det kan sticka ut utanför ägarfönstret och vänder uppåt vid skärmkanten). Värdet VÄLJS — det går inte att skriva in ett eget (WinForms DropDownList). Raderna är strängar i kollektionsvokabulären. Registreringsnamn `dropdown`.

### API
- `void setItems(std::vector<std::string> v)` / `const std::vector<std::string>& items() const`
- `void addItem(std::string text)` · `void setItem(int index, std::string text)` — index utanför ⇒ logg + no-op.
- `void setSelectedIndex(int i)` / `int selectedIndex() const` — `-1` = inget valt (förvalet).
- `std::string selectedText() const` — tom utan val.
- `void setAlign(Align a)` / `Align align() const` — fältets text.
- `void setTextSize(Length size)` / `Length textSize() const` — `TextInput`s textstorlek (`glaze::px(…)`/`glaze::mm(…)`, `0` = stilens), för fältet OCH listans rader; ett byte stänger en öppen lista.
- `Handler<ChangeArgs> onChange` · `Handler<ChangeArgs> onCommit` — klick på rad, Upp/Ner i stängt läge, Enter i ÖPPEN lista; `onCommit` direkt efter `onChange`.
- `void open()` · `void close()` · `bool isOpen() const` · `int provisionalRow() const` (markören i öppen lista, `-1` stängd).
- Tangenterna: STÄNGD öppnar Space och pilarna byter valet direkt — Enter öppnar inte, den bubblar till fönstrets standardknapp (`app-window`); ÖPPEN committar Enter/Space och Escape stänger, före fönstrets avbrytknapp.

### Exempel
```cpp
class Format : public glaze::AppWindow {
public:
    Format() : glaze::AppWindow("Format", 360, 140)
    {
        format.setItems({"PNG", "JPEG"});
        format.addItem("WebP");
        format.setSelectedIndex(0);     // förvalet är -1 = inget valt
        format.width = 160;
        format.name = "format";
        format.x = 12;
        format.y = 12;
        format.onChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            auto* dd = static_cast<glaze::Dropdown*>(s);
            status.setText("Valt: " + dd->selectedText());
        };

        status.x = 12;
        status.anchorTop = {"format", 8};

        root().adopt(format);
        root().adopt(status);
    }

private:
    glaze::Dropdown format;
    glaze::Label    status;
};
```

### .glazeui
```cpp
format.setItems({"PNG", "JPEG"});
format.addItem("WebP");
format.setItem(1, "JPG");
format.setSelectedIndex(0);
format.setTextSize(glaze::mm(3));
format.width = 160;
format.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { formatChanged(s, e); };
```

### Fallgropar
- `setItems`/`addItem`/`setItem` STÄNGER en öppen lista.
- Posterna är `std::string`, inte `TrText`: fyll om listan själv vid språkbyte.
- En dropdown med öppen lista kan inte `unadopt`as — `close()` och låt pumpen gå ett varv först.

<!-- END:widget-dropdown -->

<!-- BEGIN:widget-list-box | listbox lista listruta list checkedlistbox kryssruteslista checks enkelval flerval multiselect selectedrows items selectedindex wxlistbox qlistwidget oncheck changedrow onactivate dubbelklick doubleclick -->
## ListBox — den rullande listan (`widget-list-box`)

**Alias:** listbox, lista, listruta, list, checkedlistbox, kryssruteslista, checks, enkelval, flerval, multiselect, selectedrows, items, selectedindex, wxlistbox, qlistwidget, oncheck, changedrow, onactivate, dubbelklick, doubleclick
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/listbox.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

En rullande lista med enkelval — eller FLERVAL med `setMultiSelect(true)`, med listvyns ord och tangenter — och valfria kryssrutor (WinForms ListBox/CheckedListBox). Raderna är data, inte widgets, så listan är ETT tabbstopp och tusentals rader ritas i O(synliga). Den ärver `Panel` (klipp, hjul, remsor). Behövs flera kolumner: `ListView`. Registreringsnamn `listbox`.

### API
- `void setItems(std::vector<std::string> v)` / `const std::vector<std::string>& items() const`
- `void addItem(std::string text)` · `void setItem(int index, std::string text)`
- `void setSelectedIndex(int i)` / `int selectedIndex() const` / `std::string selectedText() const` — i flerval den aktuella raden om den är vald, annars den första valda.
- FLERVALET: `void setMultiSelect(bool on)` / `bool isMultiSelect() const` · `void setSelected(int row, bool on)` / `bool isSelected(int row) const` · `std::vector<int> selectedRows() const` (stigande) · `void selectAll()` · `void clearSelection()` · `int currentRow() const`. Användaren: klick väljer bara raden, Ctrl/Cmd+klick vänder, Shift+klick ger spannet från ankaret, Shift+pil utökar, Ctrl+pil flyttar utan att välja, Ctrl+Space vänder, Ctrl+A väljer alla.
- `void setShowChecks(bool on)` / `bool showsChecks() const`
- `void setTextSize(Length size)` / `Length textSize() const` — `TextInput`s textstorlek (`glaze::px(…)`/`glaze::mm(…)`, `0` = stilens); RADHÖJDEN växer och krymper lika mycket som textens, kryssrutan står kvar i stilens mått.
- `void setChecked(int row, bool on)` / `bool isChecked(int row) const`
- `int changedRow() const` — raden vars kryss ändrades; läs i `onCheck`.
- `Handler<ChangeArgs> onChange` (valet) · `Handler<ChangeArgs> onCheck` (ett kryss) — bara användarens. `Handler<ChangeArgs> onCommit` fyras direkt efter båda. `Handler<ActivateArgs> onActivate` (REQ5) — dubbelklick på den valda raden (utanför kryss-zonen) eller Enter med en vald rad; raden läses ur sender (`selectedIndex()`). Enter tas bara när `onActivate` är satt och en rad är vald, annars når den fönstrets standardknapp.
- `void ensureVisible(int row)` — rulla raden i synfältet.

### Exempel
```cpp
class Lager : public glaze::AppWindow {
public:
    Lager() : glaze::AppWindow("Lager", 360, 200)
    {
        lager.setItems({"Vägar", "Byggnader", "Vatten"});
        lager.addItem("Höjdkurvor");
        lager.setShowChecks(true);
        lager.setChecked(0, true);
        lager.setSelectedIndex(0);
        lager.width = 200;              // ingen autostorlek: sätt båda axlarna
        lager.height = 120;
        lager.name = "lager";
        lager.x = 12;
        lager.y = 12;
        lager.onChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            status.setText(static_cast<glaze::ListBox*>(s)->selectedText());
        };
        lager.onCheck = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            auto*     lb  = static_cast<glaze::ListBox*>(s);
            const int rad = lb->changedRow();   // raden vars kryss ändrades
            status.setText("Rad " + std::to_string(rad) +
                           (lb->isChecked(rad) ? " på" : " av"));
        };
        lager.onActivate = [this](glaze::Widget* s, glaze::ActivateArgs&) {
            // dubbelklick på den valda raden eller Enter — raden ur sender
            status.setText("Öppnar " + static_cast<glaze::ListBox*>(s)->selectedText());
        };

        status.x = 12;
        status.anchorTop = {"lager", 8};

        root().adopt(lager);
        root().adopt(status);
    }

private:
    glaze::ListBox lager;
    glaze::Label   status;
};
```

### .glazeui
```cpp
lager.setItems({"Vägar", "Byggnader", "Vatten"});
lager.addItem("Höjdkurvor");
lager.setShowChecks(true);
lager.setChecked(0, true);
lager.setSelectedIndex(0);
lager.setTextSize(glaze::mm(2.5));
lager.width = 200;
lager.height = 120;
lager.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { lagerChanged(s, e); };
lager.onCheck = [this](glaze::Widget* s, glaze::ChangeArgs& e) { lagerChecked(s, e); };
lager.onActivate = [this](glaze::Widget* s, glaze::ActivateArgs& e) { lagerActivated(s, e); };
val.setMultiSelect(true);
val.setSelected(1, true);
val.setSelected(2, true);
```

### Fallgropar
- Ingen autostorlek: sätt `width` och `height` (eller docka). I en `Bar` hjälper inte `Dock::Fill` — bredden blir 0 om den inte sätts.
- Flervalets `onChange` fyras EN gång per handling — läs valet ur `selectedRows()`. Ett tryck på en VALD rad väljer den ensam först när knappen släpps, så hela valet går att dra. `setMultiSelect(false)` behåller bara `selectedIndex()`.
- Ingen vågrät rullning (lång text klipps), ingen PageUp/PageDown, ingen typeahead och inget eget `onClick`.
- `setSelectedIndex` rullar inte — anropa `ensureVisible(i)`.
- `onActivate` tar Enter bara när handlern är satt OCH en rad är vald — annars går Enter vidare till fönstrets standardknapp (`setAcceptButton`), så en vallista i en dialog bekräftar dialogen som förut. Ett dubbelklick i kryss-zonen aktiverar inte.
- SNABBMENYN: ett högertryck väljer raden som Utforskaren (utanför valet ensam, i valet står valet), och `contextRow()` är raden i `onContextMenu` — −1 på tom yta och när listan är inaktiverad. `rowAt(e.local.x, e.local.y)` är träffvägen för egna gester.

<!-- END:widget-list-box -->

<!-- BEGIN:widget-slider | slider reglage dragreglage trackbar skjutreglage range intervall wxslider qslider vertical lodrät steg step oncommit släpp release -->
## Slider — dragreglaget (`widget-slider`)

**Alias:** slider, reglage, dragreglage, trackbar, skjutreglage, range, intervall, wxslider, qslider, vertical, lodrät, steg, step, oncommit, släpp, release
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/slider.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

Ett värde i `[min, max]` som dras med musen, stegas med piltangenterna och Home/End — och med hjulet när reglaget har fokus. Värdet är `float` i valfria enheter och kvantiseras mot `step` (0 = steglöst). `onChange` följer draget, `onCommit` kommer när musknappen släpps. Registreringsnamn `slider`.

### API
- `void setMin(float v)` · `void setMax(float v)` · `void setValue(float v)` · `void setStep(float v)` · `void setVertical(bool on)`
- `float min() const` · `float max() const` · `float value() const` (klampat till `[min, max]`) · `float step() const` · `bool isVertical() const`
- `Handler<ChangeArgs> onChange` — användarens ändringar, också under draget.
- `Handler<ChangeArgs> onCommit` — när musknappen släpps (om värdet ändrats sedan trycket), och vid varje tangent- eller hjulsteg, direkt efter `onChange`.
- Förval: `min` 0, `max` 100, `value` 0, `step` 1.

### Exempel
```cpp
class Zoom : public glaze::AppWindow {
public:
    Zoom() : glaze::AppWindow("Zoom", 360, 120)
    {
        zoom.setMin(10);
        zoom.setMax(400);
        zoom.setStep(10);               // 0 = steglöst
        zoom.setValue(100);
        zoom.width = 240;
        zoom.name = "zoom";
        zoom.x = 12;
        zoom.y = 12;
        zoom.onChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            const float v = static_cast<glaze::Slider*>(s)->value();
            procent.setText(std::to_string(static_cast<int>(v)) + " %");
        };
        // Det dyra (rita om, spara) när användaren släppt reglaget.
        zoom.onCommit = [this] { setTitle("Zoom " + procent.text()); };

        procent.setText("100 %");
        procent.anchorLeft = {"zoom", 8};
        procent.y = 12;

        root().adopt(zoom);
        root().adopt(procent);
    }

private:
    glaze::Slider zoom;
    glaze::Label  procent;
};
```

### .glazeui
```cpp
zoom.setMin(10);
zoom.setMax(400);
zoom.setStep(10);
zoom.setValue(100);
zoom.width = 240;
zoom.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { zoomChanged(s, e); };
zoom.onCommit = [this] { zoomKlar(); };
```

### Fallgropar
- `value()` KLAMPAS VID LÄSNING: `setValue(150)` med `max` 100 läser 100, och en senare `setMax(200)` ger 150 — satsordningen i en `.glazeui` spelar ingen roll.
- Ett drag som slutar där det började fyrar ingen `onCommit`. Ett drag som avbryts av att fönstret tappar fokus får inget släpp och alltså ingen commit då — den kommer vid nästa släpp eller steg, för värdet skiljer sig fortfarande från det förra committade.
- Hjulet stegar bara med fokus; utan fokus bubblar det till en rullande förälder.
- Lodrätt: `min` står NEDERST.

<!-- END:widget-slider -->

<!-- BEGIN:widget-spin-box | spinbox talfält numericupdown spinctrl spinner nummer number siffror decimaler decimals stegare updown wxspinctrldouble qspinbox qdoublespinbox numberbox oncommit selectall -->
## SpinBox — talfältet (`widget-spin-box`)

**Alias:** spinbox, talfält, numericupdown, spinctrl, spinner, nummer, number, siffror, decimaler, decimals, stegare, updown, wxspinctrldouble, qspinbox, qdoublespinbox, numberbox, oncommit, selectall
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/spin_box.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

Ett textfält för ett tal med en stegare bredvid — ETT tabbstopp. Stegaren, Upp/Ner och hjulet (med fokus) stegar; det man skriver blir värdet när man trycker Enter eller lämnar fältet, tolkat och klampat till intervallet (NumberBox/NumericUpDown-modellen). Varje ändring av värdet fyrar `onChange` och `onCommit` tillsammans. Värdet är `double`, och decimaltecknet följer språket (`,` på sv/nb/fi, `.` på en; båda godtas när man skriver). Registreringsnamn `spinbox`.

### API
- `void setValue(double v)` · `void setMin(double v)` · `void setMax(double v)`
- `void setStep(double v)` — `<= 0` ⇒ minsta visade steget (10^−decimals).
- `void setDecimals(int d)` — klampas till 0–9.
- `double value() const` — det EFFEKTIVA värdet, klampat och avrundat vid läsning. Medan användaren skriver är det det förra värdet.
- `double min() const` · `double max() const` · `double step() const` · `int decimals() const`
- `const std::string& text() const` — det fältet visar (ingen sättare); under redigering användarens text.
- `Handler<ChangeArgs> onChange` · `Handler<ChangeArgs> onCommit` — när användaren ändrat det effektiva värdet, i den ordningen: vid Enter, när fokus lämnar talfältet (postat) och vid varje steg.
- `void selectAll()` · `void setSelection(int begin, int end)` — markeringen i det inre fältet.
- Ärvt: `onFocus`/`onFocusLost` på talfältet som helhet (fokus inom — en flytt mellan fältet och stegaren fyrar inget).

### Exempel
```cpp
class Order : public glaze::AppWindow {
public:
    Order() : glaze::AppWindow("Order", 360, 120)
    {
        antal.setMin(1);
        antal.setMax(99);
        antal.setValue(3);
        antal.name = "antal";
        antal.x = 12;
        antal.y = 12;

        pris.setDecimals(2);            // decimaltecknet följer språket
        pris.setStep(0.5);
        pris.setMax(10000);
        pris.setValue(12.5);
        pris.anchorLeft = {"antal", 8};
        pris.y = 12;

        auto summera = [this] {
            summa.setText(std::to_string(antal.value() * pris.value()));
        };
        antal.onChange = summera;
        pris.onChange  = summera;
        pris.onFocus   = [this] { pris.selectAll(); };   // skriv över priset direkt

        summa.x = 12;
        summa.anchorTop = {"antal", 8};

        root().adopt(antal);
        root().adopt(pris);
        root().adopt(summa);
    }

private:
    glaze::SpinBox antal, pris;
    glaze::Label   summa;
};
```

### .glazeui
```cpp
pris.setDecimals(2);
pris.setStep(0.5);
pris.setMax(10000);
pris.setValue(12.5);
pris.anchorLeft = {"antal", 8};
pris.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { prisChanged(s, e); };
```
Satsernas ordning spelar ingen roll — `value()` klampas när det läses.

### Fallgropar
- Inga mellanvärden: det man skriver blir värdet först vid Enter eller när fokus lämnar talfältet — "5" + Tab med `min` 10 ger 10, och en text som inte går att tolka ("-", tom) skrivs tillbaka till det förra värdet utan events. Ett steg medan man skriver utgår från den skrivna texten om den går att tolka.
- Ett klick på en `ToolButton`, ett menyval och en accelerator tar inte fokus och committar alltså inte av sig självt — `value()` är det förra värdet medan fältet visar det skrivna. Handlern anropar `commitEdits()` (fönstrets) eller `commitEdit()` här först (`events`).
- Ingen upprepning medan man håller stegaren, inget hexläge, inga tusentalsavgränsare.
- Enter committar och går SEDAN vidare till fönstrets standardknapp (`app-window`), som i ett `TextInput` — dialogens OK läser alltså redan det skrivna värdet.

<!-- END:widget-spin-box -->

<!-- BEGIN:widget-progress | progress förlopp förloppsindikator progressbar gauge wxgauge laddning loading indeterminate obestämd marquee busy spinner -->
## Progress — förloppsindikatorn (`widget-progress`)

**Alias:** progress, förlopp, förloppsindikator, progressbar, gauge, wxgauge, laddning, loading, indeterminate, obestämd, marquee, busy, spinner
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/progress.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

En ränna som fylls efter `value` i `[min, max]`, eller — med `setIndeterminate(true)` — en bit som glider fram och tillbaka när längden är okänd. Den har ingen interaktion och inget event. Registreringsnamn `progress`.

### API
- `void setMin(float v)` · `void setMax(float v)` · `void setValue(float v)`
- `float min() const` · `float max() const` · `float value() const`
- `void setIndeterminate(bool on)` / `bool indeterminate() const`
- Förval: `min` 0, `max` 100, `value` 0.

### Exempel
```cpp
class Export : public glaze::AppWindow {
public:
    Export() : glaze::AppWindow("Export", 360, 140)
    {
        steg.setMax(10);
        steg.setValue(3);
        steg.width = 240;
        steg.name = "steg";
        steg.x = 12;
        steg.y = 12;

        vantar.setIndeterminate(true);  // glider tills den slås av eller göms
        vantar.width = 240;
        vantar.name = "vantar";
        vantar.x = 12;
        vantar.anchorTop = {"steg", 8};

        klar.setText("Klar");
        klar.x = 12;
        klar.anchorTop = {"vantar", 12};
        klar.onClick = [this] {
            vantar.setIndeterminate(false);
            steg.setValue(10);
        };

        root().adopt(steg);
        root().adopt(vantar);
        root().adopt(klar);
    }

private:
    glaze::Progress steg, vantar;
    glaze::Button   klar;
};
```

### .glazeui
```cpp
steg.setMax(10);
steg.setValue(3);
vantar.setIndeterminate(true);
vantar.width = 240;
```

### Fallgropar
- Det obestämda läget animerar SJÄLV och stannar när widgeten göms — slå inte på `setAnimating` för det.
- Uppdateras värdet från en bakgrundstråd: gå via `glaze::app().post` eller `Worker::post` (se `threads`), aldrig direkt.

<!-- END:widget-progress -->

<!-- BEGIN:widget-scrollbar | scrollbar rullningslist scroll rullning hscrollbar vscrollbar scrollbalk page tumme thumb orientation wxscrollbar qscrollbar -->
## Scrollbar — den fristående rullningslisten (`widget-scrollbar`)

**Alias:** scrollbar, rullningslist, scroll, rullning, hscrollbar, vscrollbar, scrollbalk, page, tumme, thumb, orientation, wxscrollbar, qscrollbar
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/scrollbar.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

En fristående rullningslist för egen rullning (tumdrag, rännklick sidvis, hjul). Den vanliga vägen är annars `Panel::showScrollbars(true)` eller de rullande widgetarna (`ListBox`, `TextArea`, tabellerna), som har egna remsor. Registreringsnamn `scrollbar`.

### API
- `enum class Orientation { Horizontal, Vertical }` · `void setOrientation(Orientation o)` / `Orientation orientation() const` — förval lodrät.
- `void setMin(float v)` · `void setMax(float v)` · `void setValue(float v)` · `void setPage(float v)`
- `float min() const` · `float max() const` · `float value() const` · `float page() const`
- `Handler<ChangeArgs> onChange` — användarens ändringar, också under draget · `Handler<ChangeArgs> onCommit` — när musknappen släpps efter ett tumdrag eller rännklick (om värdet ändrats sedan förra commiten) och vid varje hjulsteg. Listen tar inte fokus.
- `value` ∈ `[min, max]`; `page` (synlig andel) styr bara tummens längd. Tjockleken är AUTO.

### Exempel
```cpp
class Bladdra : public glaze::AppWindow {
public:
    Bladdra() : glaze::AppWindow("Bläddra", 360, 260)
    {
        rad.setMin(0);
        rad.setMax(990);
        rad.setPage(10);                // tummens längd: synlig andel
        rad.height = 200;               // tjockleken är AUTO
        rad.name = "rad";
        rad.x = 12;
        rad.y = 12;
        rad.onChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            const float v = static_cast<glaze::Scrollbar*>(s)->value();
            status.setText("Första rad: " + std::to_string(static_cast<int>(v)));
        };

        tid.setOrientation(glaze::Scrollbar::Orientation::Horizontal);
        tid.setMax(100);
        tid.setPage(25);
        tid.width = 200;
        tid.name = "tid";
        tid.anchorLeft = {"rad", 12};
        tid.y = 12;

        status.anchorLeft = {"rad", 12};
        status.anchorTop = {"tid", 12};

        root().adopt(rad);
        root().adopt(tid);
        root().adopt(status);
    }

private:
    glaze::Scrollbar rad, tid;
    glaze::Label     status;
};
```

### .glazeui
```cpp
tid.setOrientation(glaze::Scrollbar::Orientation::Horizontal);
tid.setMax(100);
tid.setPage(25);
tid.width = 200;
rad.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { radChanged(s, e); };
```

### Fallgropar
- Hjulets riktning följer rullningens inställning — listens egen, närmaste förfaders, fönstrets eller appens (`setWheelInvert`, `input`).
- Inget tabbstopp och inga piltangenter — den är musens.
- `value()` klampas vid läsning till `[min, max]`, som reglagets — det lagrade står kvar tills en ny `setValue`.

<!-- END:widget-scrollbar -->

<!-- BEGIN:widget-picture | picture bild bildruta picturebox image imageview ikon icon logo logga wxstaticbitmap pixmap fit stretch -->
## Picture — bildrutan (`widget-picture`)

**Alias:** picture, bild, bildruta, picturebox, image, imageview, ikon, icon, logo, logga, wxstaticbitmap, pixmap, fit, stretch
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/picture.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

Visar EN bild — ur appens `assets.bin` (se `assets`) eller ur pixlar appen räknat fram (`glaze::Bitmap`, se `canvas-images`) — utan interaktion och utan tabbstopp. `fit` väljer hur bilden möter ytan: `Fit` (ryms, proportionerna bevaras — förval), `Fill` (täcker, överskottet klipps), `Stretch` (dras ut) och `Center` (naturlig storlek). AUTO-storleken är bildens naturliga mått. Registreringsnamn `picture`.

### API
- `void setImg(ImageSource img)` / `const ImageSource& img() const` — `glaze::img("lista", "nyckel")`, en `glaze::Bitmap` eller en `RenderTexture`.
- `enum class Fit { Fit, Fill, Stretch, Center }` · `void setFit(Fit f)` / `Fit fit() const`

### Exempel
```cpp
class Logga : public glaze::AppWindow {
public:
    Logga() : glaze::AppWindow("Logga", 360, 200)
    {
        logga.setImg(glaze::img("bilder", "logga"));   // lista, nyckel
        logga.setFit(glaze::Picture::Fit::Fit);        // förvalet
        logga.width = 128;              // AUTO = bildens naturliga mått
        logga.height = 128;
        logga.x = 12;
        logga.y = 12;

        root().adopt(logga);
    }

private:
    glaze::Picture logga;
};
```

### .glazeui
```cpp
logga.setImg(glaze::img("bilder", "logga"));
logga.setFit(glaze::Picture::Fit::Fill);
logga.width = 128;
logga.height = 128;
```

Pixlar räknade i drift har ingen `.glazeui`-form (regel 8 gäller formen, inte data); de sätts i koden:

```cpp
glaze::Bitmap bild(64, 64);                 // appens egna RGBA-pixlar
bild.fill(gfx::Color{44, 112, 196});
logga.setImg(bild);                         // handtag: pixlarna delas
logga.width = logga.height = 32;            // 64 px skarpt på skala 2
```

### Fallgropar
- Det naturliga måttet är bildens PIXLAR räknade som fysiska px: logiskt = pixlar / skala. En oförändrad 64 × 64-Bitmap blir 64 logiska px på skala 1 men 32 på skala 2 (samma regel som en asset) — sätt `width`/`height` för en fast logisk storlek.
- Ändrade pixlar (`Bitmap::setPixels`) syns efter `setImg(sammaBild)` (versionen gör det till en ändring) eller `invalidate()`.
- Ett ogiltigt handtag (assets inte laddade, okänd nyckel) ritar ingenting och loggas en gång.
- Ritas bilden MINDRE än sitt naturliga mått räknas den om till rutans storlek med ytans medelvärde (`Draw::glyphResampled`), så en stor bild i en liten ruta inte blir grynig; 1:1 och uppåt ritas med GPU:ns filter som förut. Det kostar en omräknad textur per storlek och fönster — en bild som ritas i många storlekar (en ruta som dras) laddar upp en per storlek.

<!-- END:widget-picture -->

<!-- BEGIN:widget-separator | separator skiljelinje linje avdelare divider rule hr line wxstaticline vertical lodrät -->
## Separator — skiljelinjen (`widget-separator`)

**Alias:** separator, skiljelinje, linje, avdelare, divider, rule, hr, line, wxstaticline, vertical, lodrät
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/separator.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

Ett tunt streck, vågrätt eller lodrätt, i temats färg. Tjockleken är stilens; längden är AUTO = stilens `separatorDefaultLen` tills layouten sträcker den (dubbelankare, dock eller satt `width`/`height`). Registreringsnamn `separator`.

### API
- `void setVertical(bool on)` / `bool isVertical() const` — förval vågrät. I en `Bar` (ett verktygsfält, statusraden) står separatorn ALLTID tvärs baren och värdet spelar ingen roll.

### Exempel
```cpp
class Avsnitt : public glaze::AppWindow {
public:
    Avsnitt() : glaze::AppWindow("Avsnitt", 360, 140)
    {
        rubrik.setText("Avancerat");
        rubrik.name = "rubrik";
        rubrik.x = 12;
        rubrik.y = 12;

        linje.anchors = {true, true, true, false};   // vänster+höger = sträckt
        linje.x = 12;
        linje.right = 12;
        linje.anchorTop = {"rubrik", 4};

        root().adopt(rubrik);
        root().adopt(linje);
    }

private:
    glaze::Label     rubrik;
    glaze::Separator linje;
};
```

### .glazeui
```cpp
linje.anchors = {true, true, true, false};
linje.x = 12;
linje.right = 12;
linje.anchorTop = {"rubrik", 4};
skiljelinje.setVertical(true);
```

### Fallgropar
- Utan sträckning, dock eller satt längd får linjen stilens standardlängd — inte förälderns bredd.
- I ett verktygsfält står den tvärs remsan av sig själv — lodrät i ett liggande fält, vågrät i ett stående (se `widget-tool-bar`).

<!-- END:widget-separator -->

<!-- BEGIN:widget-panel | panel container behållare yta frame ram border scroll rullning scrollable showscrollbars clip klipp barn children wxpanel qframe add adopt -->
## Panel — containern (`widget-panel`)

**Alias:** panel, container, behållare, yta, frame, ram, border, scroll, rullning, scrollable, showscrollbars, clip, klipp, barn, children, wxpanel, qframe, add, adopt
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/panel.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

Container-primitivet: barnen placeras FÖRÄLDER-RELATIVT (x/y/dock/anchors räknas mot panelens innehållsyta), klipps (på från start) och rullas med hjulet när innehållet är större än ytan. `setBorder(true)` ritar yta och ram; utan är panelen en osynlig placeringsyta. Barn läggs till med `adopt(värdemedlem)` (genererad kod) eller `add<T>(…)` (ägt barn). Registreringsnamn `panel`.

### API
- `void setBorder(bool on)` / `bool isBorder() const` — förval av.
- `void setClip(bool on)` — klipp barnen (förval på); läses med `clipsChildren()`.
- `void showScrollbars(bool on)` / `bool scrollbarsShown() const` — synliga overlay-lister vid överflöd.
- `void setScroll(float x, float y)` / `float scrollX() const` / `float scrollY() const` — logiska px.
- `template <class T, class... Args> T* add(Args&&... args)` — skapa och äg ett barn (ärvt från `Widget`).
- `Widget* adopt(Widget& w)` · `bool unadopt(Widget& w)` — låna in respektive släpp ett barn (ärvt).

### Exempel
```cpp
class Alternativ : public glaze::AppWindow {
public:
    Alternativ() : glaze::AppWindow("Alternativ", 420, 300)
    {
        ram.setBorder(true);            // utan: en osynlig placeringsyta
        ram.showScrollbars(true);       // synliga lister vid överflöd
        ram.anchors = {true, true, true, true};   // sträckt åt alla håll
        ram.x = 12;
        ram.y = 12;
        ram.right = 12;
        ram.bottom = 12;

        std::string forra;              // barnen i panelens koordinater
        for (int i = 1; i <= 20; ++i) {
            auto* rad = ram.add<glaze::Checkbox>("Alternativ " + std::to_string(i));
            rad->name = "alt" + std::to_string(i);
            rad->x = 8;
            if (forra.empty())
                rad->y = 8;
            else
                rad->anchorTop = {forra, 4};
            forra = rad->name;
        }

        root().adopt(ram);
    }

private:
    glaze::Panel ram;
};
```

### .glazeui
```cpp
ram.anchors = {true, true, true, true};
ram.x = 12;
ram.y = 12;
ram.right = 12;
ram.bottom = 12;
ram.setBorder(true);
ram.showScrollbars(true);
root().adopt(ram);
ram.adopt(alt1);
ram.adopt(alt2);
```
Loopen ur kodexemplet har ingen `.glazeui`-form — där står varje barn som en egen nod med `adopt`.

### Fallgropar
- Hjulets riktning ÄRVS: `panel.setWheelInvert(glaze::InputDevice::Mouse, true, false)` vänder hjulet i panelen och i varje barn som inte satt något själv; det panelen inte satt ärvs från dess förfäder och appen (`input`). Shift+mushjul rullar i sidled.
- Ingen autostorlek: en panel utan `width`/`height`, dock eller dubbelankare är 0 × 0 och visar ingenting.
- `disable()` på panelen inaktiverar inte barnen.
- Barn under en overlay-remsa vinner musträffen — lägg inte interaktiva barn i kanten.

<!-- END:widget-panel -->

<!-- BEGIN:widget-group-box | groupbox gruppram ram grupp fieldset legend staticbox wxstaticbox qgroupbox rubrikram frame titel -->
## GroupBox — gruppramen (`widget-group-box`)

**Alias:** groupbox, gruppram, ram, grupp, fieldset, legend, staticbox, wxstaticbox, qgroupbox, rubrikram, frame, titel
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/group_box.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

En ram med rubriken I linjen (HTML `fieldset`/`legend`) och barnen innanför, klippta mot ramen. Utan satta mått blir ramen exakt så stor som barnen och rubriken kräver. Den är INTE en `Panel`: den rullar aldrig och har ingen `setBorder`/`setClip`. Registreringsnamn `groupbox`.

### API
- `explicit GroupBox(TrText t = {})`
- `void setText(TrText t)` / `const std::string& text() const` / `const TrText& textToken() const` — rubriken.
- Barnen med `adopt`/`add<T>` som i en panel; (0, 0) är ramens innerhörn.

### Exempel
```cpp
class Utskrift : public glaze::AppWindow {
public:
    Utskrift() : glaze::AppWindow("Utskrift", 360, 200)
    {
        ram.setText("Utskrift");        // rubriken står i linjen
        ram.x = 12;                     // storleken är AUTO ur barnen
        ram.y = 12;

        dubbel.setText("Dubbelsidig");
        dubbel.name = "dubbel";         // (0, 0) = ramens innerhörn

        svartvit.setText("Svartvit");
        svartvit.anchorTop = {"dubbel", 4};

        root().adopt(ram);
        ram.adopt(dubbel);
        ram.adopt(svartvit);
    }

private:
    glaze::GroupBox ram;
    glaze::Checkbox dubbel, svartvit;
};
```

### .glazeui
```cpp
ram.setText("Utskrift");
ram.x = 12;
ram.y = 12;
dubbel.setText("Dubbelsidig");
svartvit.anchorTop = {"dubbel", 4};
root().adopt(ram);
ram.adopt(dubbel);
ram.adopt(svartvit);
```

### Fallgropar
- Tom rubrik flyttar ingenting: rubrikbandet är reserverat ändå.
- `disable()` dimmar rubriken men inte barnen.
- AUTO-storleken räknas med `layout`s regel för en förälder ur innehållet: barn nere till höger (`anchors.right`/`bottom`) och SPÄNDA barn (deras mätta mått som minsta) ryms, och `right`/`bottom` på ett barn bundet bara uppifrån/från vänster är luft. Ett gammalt `right`/`bottom` gör ramen större — sätt måtten om ramen ska vara exakt.

<!-- END:widget-group-box -->

<!-- BEGIN:widget-floating-panel | floatingpanel flyttbar panel minifönster inforuta anteckning note popup-panel verktygsfönster drag flytta storleksändra resize grepp grip rubrik stängknapp closable mdi -->
## FloatingPanel — den flyttbara panelen (`widget-floating-panel`)

**Alias:** floatingpanel, flyttbar panel, minifönster, inforuta, anteckning, note, verktygsfönster, drag, flytta, storleksändra, resize, grepp, grip, rubrik, stängknapp, closable, mdi
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/floating_panel.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

En `Panel` med en RUBRIK (text och valfri stängknapp, som dockningspanelens) som ANVÄNDAREN flyttar genom att dra rubriken och storleksändrar i GREPPET i nedre högra hörnet — ett minifönster inne i fönstret, i vilken förälder som helst. Barnen bor mellan rubriken och greppets remsa. På en rityta med `Canvas::setChildAnchor` står panelen i en världspunkt, och draget flyttar då punkten. Registreringsnamn `floatingpanel`.

### API
- `explicit FloatingPanel(TrText t = {})`
- `void setText(TrText t)` / `const std::string& text() const` / `const TrText& textToken() const` — rubriken.
- `void setClosable(bool on)` / `bool isClosable() const` — stängknappen, AV som förval; ett tryck döljer panelen.
- `Handler<ChangeArgs> onChange` — användaren SLÄPPTE ett drag som flyttade eller ändrade panelen, eller stängde den. Läs `x`/`y`/`width`/`height` (förälderns logiska px), `isVisible()` eller ritytans `childAnchor(panel)` ur sender.
- Barnen, `setBorder`, `setClip` och rullningen som i en `Panel` (`widget-panel`).
- Minsta måttet och AUTO-måttet är stilens `floatingPanelMinW`/`floatingPanelMinH`.

### Exempel
```cpp
class Karta : public glaze::AppWindow {
public:
    Karta() : glaze::AppWindow("Karta", 800, 600)
    {
        karta.dock = glaze::Dock::Fill;
        karta.setYUp(true);
        karta.setCenter(674000, 6580000);
        karta.setPannable(true);

        info.setText("Stolpe 12");
        info.setClosable(true);
        info.width  = 180;
        info.height = 110;
        info.onChange = [this] { spara(info.isVisible()); };
        hojd.setText("Höjd 9 m");
        hojd.dock = glaze::Dock::Fill;

        root().adopt(karta);
        karta.adopt(info);              // inforutan är ritytans barn …
        info.adopt(hojd);
        karta.setChildAnchor(info, {674000, 6580100},   // … i en världspunkt
                             glaze::TextAnchor::BottomLeft);
    }

private:
    void spara(bool synlig);
    glaze::Canvas        karta;
    glaze::FloatingPanel info;
    glaze::Label         hojd;
};
```

### .glazeui
```cpp
info.setText("Stolpe 12");
info.setClosable(true);
info.width = 180;
info.height = 110;
root().adopt(karta);
karta.adopt(info);
info.adopt(hojd);
karta.setChildAnchor(info, {674000, 6580100}, glaze::TextAnchor::BottomLeft);
```

### Fallgropar
- En DOCKAD panel (`dock`) flyttas och ändras inte.
- Ett avbrutet drag (fokusförlusten) återgår till läget vid trycket och fyrar ingenting.
- Pekaren byter inte form över rubriken eller greppet — fasaden har ingen diagonal storlekspekare.

<!-- END:widget-floating-panel -->

<!-- BEGIN:widget-tab-control | tabcontrol tabpage flikar flik tabs tab notebook wxnotebook qtabwidget sidor page closable stängbar ontabclose selectedindex -->
## TabControl + TabPage — flikarna (`widget-tab-control`)

**Alias:** tabcontrol, tabpage, flikar, flik, tabs, tab, notebook, wxnotebook, qtabwidget, sidor, page, closable, stängbar, ontabclose, selectedindex
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/tab_control.h), [header](Glaze/widgets/include/glaze/widgets/tab_page.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

En rad flikar överst och den valda sidans innehåll under. Sidorna är `TabPage`-BARN i adopt-ordning och fliktiteln är sidans `text`; sidans innehåll är sidans barn. Den valda sidan syns, de andra är dolda och nås inte med Tab. Användaren byter med klick, Vänster/Höger (flikkontrollen i fokus) och Ctrl+Tab/Ctrl+PageDown (även inifrån sidan). Registreringsnamn `tabcontrol` och `tabpage`.

### API
- `int pageCount() const` · `TabPage* page(int index) const` · `int pageIndex(const Widget& page) const`
- SIDORNA i kollektionsvokabulären: `TabPage& addPage(TrText text = {})` (en ÄGD sida sist — `unadopt` förstör den) · `void setPage(int index, TrText text)` (rubriken; sidans barn står) · `void setPages(std::vector<TrText> texts)` (alla sidor byts; det lagrade valet står kvar).
- `void setSelectedIndex(int index)` / `int selectedIndex() const` (klampat, `-1` utan sidor) · `TabPage* selectedPage() const`
- `void setClosable(bool on)` / `bool isClosable() const` — ett ✕ per flik.
- `Handler<ChangeArgs> onChange` — användaren bytte flik.
- `Handler<TabCloseArgs> onTabClose` — `struct TabCloseArgs : EventArgs { int index = -1; }`.
- `TabPage`: `explicit TabPage(TrText t = {})` · `void setText(TrText t)` · `const std::string& text() const` — en `Panel` (`setBorder`, `showScrollbars` …).

### Exempel
```cpp
class Installningar : public glaze::AppWindow {
public:
    Installningar() : glaze::AppWindow("Inställningar", 480, 320)
    {
        flikar.dock = glaze::Dock::Fill;
        flikar.setClosable(true);       // ett ✕ per flik
        flikar.onChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            auto* tc = static_cast<glaze::TabControl*>(s);
            setTitle("Inställningar — " + tc->selectedPage()->text());
        };
        flikar.onTabClose = [this](glaze::Widget*, glaze::TabCloseArgs& e) {
            flikar.unadopt(*flikar.page(e.index));   // appen tar bort sidan
        };

        allmant.setText("Allmänt");     // fliktiteln är sidans text
        avancerat.setText("Avancerat");
        loggning.setText("Logga till fil");
        loggning.x = 12;
        loggning.y = 12;

        root().adopt(flikar);
        flikar.adopt(allmant);          // sidorna är barn, i adopt-ordning
        flikar.adopt(avancerat);
        avancerat.adopt(loggning);      // sidans innehåll är sidans barn

        glaze::TabPage& ny = flikar.addPage("Ny flik");   // ägd: unadopt förstör
        flikar.setSelectedIndex(flikar.pageIndex(ny));
    }

private:
    glaze::TabControl flikar;
    glaze::TabPage    allmant, avancerat;
    glaze::Checkbox   loggning;
};
```

### .glazeui
```cpp
flikar.dock = glaze::Dock::Fill;
flikar.setSelectedIndex(1);
flikar.setClosable(true);
allmant.setText("Allmänt");
avancerat.setText("Avancerat");
flikar.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { flikarChanged(s, e); };
flikar.onTabClose = [this](glaze::Widget* s, glaze::TabCloseArgs& e) { flikStangs(s, e); };
root().adopt(flikar);
flikar.adopt(allmant);
flikar.adopt(avancerat);
avancerat.adopt(loggning);
flikar.addPage("Logg");
```
`setSelectedIndex(1)` får stå före trädbygget — valet gäller när den andra sidan kommer. `addPage`/`setPage`/`setPages` står EFTER trädbygget (previewn bygger trädet först).

### Fallgropar
- Flikkontrollen tar ALDRIG bort en sida själv: gör `unadopt` i `onTabClose` (en ägd sida förstörs, en lånad släpps).
- Medlemssidor med `adopt` och `addPage` ger samma sorts sida. `setPages` släpper ALLA sidor (en lånad lever vidare och syns, en ägd förstörs) och ändrar ingenting alls om en sida har en öppen popup. Ett barn som inte är en `TabPage` loggas, döljs och placeras inte.
- Anropa inte `show()` på en ovald sida — välj med `setSelectedIndex`.
- Ingen autostorlek; flikarna står alltid överst; inga ikoner i flikarna; ingen omordning med musen.

<!-- END:widget-tab-control -->

<!-- BEGIN:widget-splitter | splitter delare splitcontainer splitterwindow wxsplitterwindow qsplitter panes rutor skikt sash panesize fixedpane delning -->
## Splitter — delaren (`widget-splitter`)

**Alias:** splitter, delare, splitcontainer, splitterwindow, wxsplitterwindow, qsplitter, panes, rutor, skikt, sash, panesize, fixedpane, delning
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/splitter.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

Två rutor med ett dragbart skikt emellan. Rutorna är de TVÅ FÖRSTA barnen (oftast två `Panel`) och delaren placerar dem själv — deras x/y/dock/anchors ignoreras. `paneSize` är den FASTA rutans storlek, så den rutan står still när delaren växer. Registreringsnamn `splitter`.

### API
- `void setVertical(bool on)` / `bool isVertical() const` — förval `true` = stående skikt, rutorna bredvid varandra.
- `enum class Pane { First, Second }` · `void setFixedPane(Pane p)` / `Pane fixedPane() const` — förval `First`.
- `void setPaneSize(float logical)` / `float paneSize() const` — `<= 0` = AUTO (halva).
- `void setFirstMin(float logical)` · `void setSecondMin(float logical)` (+ getters) — minsta mått per ruta.
- `Handler<ChangeArgs> onChange` — EN gång när användaren släppt ett drag som flyttade skiktet.

### Exempel
```cpp
class Utforskare : public glaze::AppWindow {
public:
    Utforskare() : glaze::AppWindow("Utforskare", 640, 400)
    {
        delare.dock = glaze::Dock::Fill;
        delare.setPaneSize(200);        // den FASTA rutans bredd (förval: första)
        delare.setFirstMin(120);
        delare.setSecondMin(200);
        delare.onChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            const float b = static_cast<glaze::Splitter*>(s)->paneSize();
            info.setText("Mapprutan: " + std::to_string(static_cast<int>(b)) + " px");
        };

        mappar.setBorder(true);
        innehall.setBorder(true);
        info.x = 8;
        info.y = 8;

        root().adopt(delare);
        delare.adopt(mappar);           // första rutan
        delare.adopt(innehall);         // andra rutan
        innehall.adopt(info);
    }

private:
    glaze::Splitter delare;
    glaze::Panel    mappar, innehall;
    glaze::Label    info;
};
```

### .glazeui
```cpp
delare.dock = glaze::Dock::Fill;
delare.setVertical(false);
delare.setFixedPane(glaze::Splitter::Pane::Second);
delare.setPaneSize(200);
delare.setSecondMin(120);
delare.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { delareChanged(s, e); };
root().adopt(delare);
delare.adopt(mappar);
delare.adopt(innehall);
```

### Fallgropar
- `paneSize` är INTE WinForms `SplitterDistance` (avståndet från vänster) utan den fasta rutans mått.
- Under ett drag svarar `paneSize()` med det GAMLA läget; det nya skrivs vid släppet.
- Ett tredje barn placeras inte; en dold ruta lämnar ett hål (ingen hopfällning). Inget tabbstopp, ingen autostorlek.

<!-- END:widget-splitter -->

<!-- BEGIN:widget-dock | dockhost dockpanel dockning docking dock paneler panels aui wxauimanager layout perspective spara-layout verktygsfönster tool-window flikar tabs stapel stack handtag handle auto-hide huvudyta band verktygsfält toolbar-band grepp knappnål pin pinned overlay dölj-automatiskt glidning animation snapp snap flytande floating float flytande-verktygsfält floating-toolbar levande-drag lossning tear-off platshållare placeholder allowededges tillåtna-kanter -->
## DockHost + DockPanel — dockningen (`widget-dock`)

**Alias:** dockhost, dockpanel, dockning, docking, dock, paneler, panels, aui, wxauimanager, layout, perspective, spara-layout, verktygsfönster, tool-window, flikar, tabs, stapel, stack, handtag, handle, auto-hide, huvudyta, band, verktygsfält, toolbar-band, grepp, knappnål, pin, pinned, overlay, dölj-automatiskt, glidning, animation, snapp, snap, flytande, floating, float, flytande-verktygsfält, floating-toolbar, levande-drag, lossning, tear-off, platshållare, placeholder, allowededges, tillåtna-kanter
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/dock_host.h), [header](Glaze/widgets/include/glaze/widgets/dock_panel.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

`DockPanel` är en panel med rubrik — titel, menyknapp ▾, KNAPPNÅL och stängknapp; `dock` väljer kanten och `width`/`height` tjockleken. `DockHost` ordnar panelerna i REMSOR längs sina kanter, i FLIKGRUPPER och STAPLAR i remsorna, och ger allt annat HUVUDYTAN (mitten). En infälld panel blir ett HANDTAG på kanten, och ett klick fäller ut den igen. KNAPPNÅLEN är Visual Studios Auto Hide: en onålad panel är ett handtag som fäller ut panelen som en OVERLAY ovanpå huvudytan — den trycker inte undan något, och den fälls in igen när både pekaren och fokus lämnat den. Panelerna GLIDER ut och in. Användaren drar i skikten (de SNAPPAR mot minsta, största och förvalda måttet, och under det minsta fälls panelen in), och ett drag i en rubrik eller flik är VS:s LEVANDE DRAG: panelen LOSSNAR vid tröskeln till ett eget fönster som följer pekaren, PLATSHÅLLAREN visar exakt var den hamnar (mitt på en panel = flik, vid dess kant = stapel eller ny remsa, vid värdens kant = ny remsa), och släppt någon annanstans flyter den där — och hela läget sparas som sträng. VERKTYGSFÄLT (`ToolBar`) som är värdens barn står i BAND ytterst längs kanterna (VS och Office); deras GREPP lossar fältet på samma sätt, och platshållaren visar ett annat band, en annan plats eller en annan kant. En panel kan också FLYTA med ett dubbelklick på rubriken eller ▾ ▸ Flytande, och ett verktygsfält med ett dubbelklick på greppet (i sitt fönster har det en rubrik med titel och ✕ och aktiveras aldrig); ett flytande objekts rubrik dras likadant tillbaka. Registreringsnamn `dockhost` och `dockpanel`.

### API
- `DockHost`: `std::string layout() const` · `bool setLayout(const std::string& s)` (false + logg om strängen avvisades, inget ändrat) · `void dockPanel(DockPanel& p, Dock edge)` (ytterst vid kanten) · `void dockPanel(DockPanel& p, DockPanel& target, Dock where)` (`Dock::Fill` = flik, en kant = bredvid; en FLYTANDE panel dockas tillbaka) · `static bool isFloatDragSupported()` (false på Wayland och headless — inget levande drag där, bara platshållaren) · `Handler<ChangeArgs> onChange`.
- `DockPanel`: `explicit DockPanel(TrText t = {})` (sätter `dock = Dock::Left`) · `void setText(TrText t)` · `void setImg(ImageSource)` (handtagets och flikens ikon) · `void setCollapsed(bool on)` / `bool isCollapsed() const` · `void setPinned(bool on)` / `bool isPinned() const` (knappnålen; `setPinned(false)` fäller också in panelen) · `void setMinimumSize(float w, float h)` / `setMaximumSize` (logiska px, 0 = ingen gräns) · `void setClosable(bool)` · `void setFloating(bool on)` / `bool isFloating() const` (eget fönster) · `void setFloatable(bool on)` / `bool isFloatable() const` (användarens vägar till flytande; appen kan alltid) · `void setFloatingBounds(float x, float y, float w, float h)` / `gfx::Rectf floatingBounds() const` (fönstrets plats och innehållsyta i LOGISKA px, `AppWindow::setLocation`s rymd; storleken 0 = panelens egen) · `void activate()` (öppna, välj fliken, fäll ut — en onålad som overlay, med fokus i den; en flytande i sitt fönster, som lyfts) · `Handler<ChangeArgs> onChange`.
- Knappnålen och infällningen är två flaggor: nålad + utfälld = dockad · nålad + infälld = handtag, ett klick fäller ut dockad · onålad + infälld = handtag, ett klick öppnar OVERLAYN · onålad + utfälld = overlayn är öppen (ett drifttillstånd — `layout()` skriver panelen infälld, och att öppna eller fälla in overlayn fyrar inget `onChange`). Högst en overlay är öppen åt gången; Escape inifrån fäller in den direkt, och ett klick på dess handtag fäller in den. Rubrikmenyn ▾ har "Flytande" (eller "Docka"), "Dölj automatiskt"/"Fäst" och "Stäng".
- FLYTANDE: panelen lämnar layouten och bor i ett eget fönster som värdens fönster ÄGER — det ligger ovanpå, stängs, döljs och minimeras med ägaren och har ingen knapp i aktivitetsfältet. Rubriken är fönstrets titelrad (titel, ▾ och stäng — ingen knappnål: en flytande panel är alltid nålad och utfälld). `isFloating()` och `layout()` gäller DIREKT; flytten mellan fönstren görs i pumpens svans (en öppen popup i panelen skjuter upp den tills popupen stängts). En tangent ingen i det flytande fönstret tog går till huvudfönstrets genvägar. Det flytande fönstrets rubrik dras som en dockad panels: fönstret följer pekaren, och släppt där platshållaren står dockas panelen — annars flyter den kvar (ingen reserv "hem"). Ett nytt panelfönster stjäl inte fokus när det visas första gången. Fönstrets egen stängning (Alt+F4) stänger panelen; stängs huvudfönstret rörs panelens läge inte. `onChange` fyras EN gång per drag, vid släppet; ett drag som avbryts (fokusförlust) lämnar panelen flytande där den står och fyrar `onChange` strax efter. Escape avbryter inte ett drag.
- Stäng = `hide()`, fram igen med `show()` (blir då den aktiva fliken). Tjockleken AUTO = stilens `dockDefaultSize`; en remsas tjocklek är det största måttet bland dess öppna paneler, klampat mot deras min/max.
- Layoutsträngen v2: `v2;s=<kant>;c=<vikt>;p=<namn>,<aktiv>,<infälld>,<nålad>,<stängd>,<bredd>,<höjd>;…;f=<namn>,<x>,<y>,<b>,<h>,<stängd>,<dock>,<bredd>,<höjd>;t=<namn>,<kant>,<band>,<plats>,<dold>;ft=<namn>,<x>,<y>,<dold>,<dock>,<band>` — `s=` en remsa, `c=` en flikgrupp i den, `p=` en flik (i skärordning), `f=` en flytande panel (fönstrets plats och storlek i logiska px, `-` för en plats som inte finns), `t=` ett verktygsfält, `ft=` ett FLYTANDE verktygsfält (fönstrets plats, och kanten och bandet det dockas tillbaka till). Nyckeln är objektets `name`; en `p=` eller `t=` för ett objekt som flyter dockar det, och ett som flyter utan att nämnas fortsätter att flyta.
- Verktygsfälten: ett `ToolBar`-barn med en kant (förval `Top`) står i band `band()` (0 = yttersta; `setBand(n)`), i adoptionsordning. Banden skärs ytterst — Top, Bottom, Left, Right — före panelerna; ett fält vid Left/Right STÅR. Ett fält adopterat under körning hamnar sist i sitt band, ett band utanför antalet blir ett nytt innersta, och ett dolt fält tar ingen plats. Ett FLYTANDE fält (`setFloating(true)`, dubbelklick på greppet) bor i ett eget fönster med en rubrik — titeln är fältets `text` — och fast storlek som följer innehållet; det LIGGER alltid, och dubbelklick på rubriken dockar det sist i band `band()` vid `dock`; ett drag i rubriken flyttar fönstret med pekaren och dockar fältet där platshållaren står. Rubrikens ✕ döljer fältet (`show()` tar fram det) och fyrar värdens `onChange`. `ToolBar::setAllowedEdges(std::vector<Dock>)` (REQ3 21, förval alla fyra, `{}` = bara flyta) begränsar kanterna ANVÄNDARENS drag når: en otillåten kant är inget mål — inget band och ingen ny bandzon där, ingen platshållare — och ett buret fält som släpps där flyter. Appen kan alltid (`dock`, `setLayout`); kanterna är fältets attribut och står inte i `layout()` (`widget-tool-bar`).
- Stilen bär beteendet: `dockAnimMs` (glidningens längd, 0 = ingen), `dockHideDelayMs` (hur ofta overlayns lämna-prövning görs) och `dockSnapZone` (snappens räckvidd).

### Exempel
```cpp
class Editor : public glaze::AppWindow {
public:
    Editor() : glaze::AppWindow("Editor", 900, 560)
    {
        vard.dock = glaze::Dock::Fill;
        vard.onChange = [this] { sparad = vard.layout(); };   // spara strängen

        utforskare.setText("Utforskare");
        utforskare.name = "utforskare";       // nyckeln i layoutsträngen
        utforskare.width = 240;               // dock = Left är förvalet
        utforskare.setMinimumSize(160, 0);    // aldrig smalare än 160
        sok.setText("Sök");
        sok.name = "sok";
        utdata.setText("Utdata");
        utdata.name = "utdata";
        utdata.dock = glaze::Dock::Bottom;
        utdata.height = 120;

        dokument.setBorder(true);             // allt som inte är en kantpanel = huvudytan

        root().adopt(vard);
        vard.adopt(utforskare);
        vard.adopt(sok);
        vard.adopt(utdata);
        vard.adopt(dokument);
        vard.dockPanel(sok, utforskare, glaze::Dock::Fill);   // en flik hos utforskare
        utforskare.activate();                                 // välj den fliken
    }

    // T.ex. ur menyn "Visa → Utdata" och "Visa → Återställ layout".
    void visaUtdata() { utdata.activate(); }   // öppnar en stängd, fäller ut en infälld
    void aterstall()
    {
        if (!sparad.empty())
            vard.setLayout(sparad);     // false + logg om strängen är trasig
    }

private:
    glaze::DockHost  vard;
    glaze::DockPanel utforskare, sok, utdata;
    glaze::Panel     dokument;
    std::string      sparad;
};
```

### .glazeui
```cpp
utforskare.setText("Utforskare");
utforskare.width = 240;
utforskare.setMinimumSize(160, 0);
utforskare.name = "utforskare";
sok.setText("Sök");
sok.setClosable(false);
sok.name = "sok";
utdata.dock = glaze::Dock::Bottom;
utdata.height = 120;
utdata.setImg(glaze::img("toolbar", "info"));
utdata.setPinned(false);           // ett handtag — fälls ut som overlay
utdata.name = "utdata";
bevakning.setText("Bevakning");
bevakning.dock = glaze::Dock::Right;   // dit den dockas tillbaka
bevakning.setFloating(true);           // startar i ett eget fönster
bevakning.name = "bevakning";
felsok.name = "felsok";            // ett ToolBar — dock = Top är förvalet
felsok.setText("Felsök");          // titeln i rubriken när det flyter
felsok.setBand(1);                 // band 1, innanför band 0 — dit det dockas
felsok.setFloating(true);          // startar i ett eget fönster
felsok.setFloatingPosition(900, 140);   // valfri, logiska px (annars mitt över)
felsok.setAllowedEdges({glaze::Dock::Top, glaze::Dock::Bottom});   // användaren dockar bara där
vard.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { vardChanged(s, e); };
root().adopt(vard);
vard.adopt(felsok);
felsok.adopt(kor);
vard.adopt(utforskare);
vard.adopt(sok);
vard.adopt(utdata);
vard.adopt(bevakning);
vard.adopt(dokument);
vard.dockPanel(sok, utforskare, glaze::Dock::Fill);
utforskare.activate();
```

### Fallgropar
- En panel utan `name` kan inte sparas i `layout()`, och `setLayout` hittar den inte (loggen säger hur många som saknar namn).
- `dockPanel`, `activate` och `setLayout` i en handskriven `.glazeui` ska stå EFTER trädbygget — appen kör satserna i ordning och känner annars inte panelerna. Panelerna i `dockPanel` är identifierare (medlemsnamnen).
- En inaktiv flik och en infälld panel är INTE dolda (`isVisible()` är fortfarande true) — de har ingen yta, och Tab går inte in i dem. `hide()` betyder stängd.
- Fokus stannar aldrig i en panel ingen ser, också när APPEN gör ändringen (`show()` på en annan flik, `hide()`, `setCollapsed`, `setPinned`, `setLayout`): fokus går till den visade flikens fält, handtaget, huvudytans första stopp eller värden — och finns inget stopp står fönstret utan fokus (`AppWindow::clearFocus`).
- Overlayn fälls in av sig själv bara när pekaren (över overlayn eller dess handtag), tangentfokus, ett tryck och popuperna i den ALLA är utanför den — `setCollapsed(false)` på en onålad panel utan att flytta fokus in i den öppnar alltså en overlay som fälls in strax. `activate()` flyttar fokus in.
- En overlay som STÄNGS (`hide()`, stängknappen) fälls in — ett `show()` senare ger handtaget, inte overlayn. En overlay som dras till en kant eller panel blir dockad och nålad.
- `setLayout` glider inte, och en panel i en remsa vars andra celler står kvar byts utan glidning.
- Ett okänt namn i strängen hoppas över, resten gäller; ett syntaxfel lämnar allt orört. `v1`-strängar avvisas.
- Ett verktygsfälts `band()` är det app satte tills användaren flyttar fältet; då skriver värden tillbaka bandets index (packat — inga luckor) och kanten. Banden är packade: ingen fri position i ett band, och ett fält som dockas tillbaka ur sitt flytande fönster hamnar SIST i sitt band (platsen i bandet minns inte, som i VS).
- En otillåten kant (`setAllowedEdges`) gäller också fältets NUVARANDE: ett fält som appen dockat vid en kant användaren inte får välja kan inte flyttas inom sitt band där — bara bort till en tillåten kant eller till flytande.
- Att flytta ett verktygsfält en plats i sitt band gör det FLYTANDE under draget (det lossnar vid tröskeln, som en panel), och bandet lägger om sig medan det är lyft. Ett fält eller en panel som inte ska lossna med musen får `setFloatable(false)`: då visar draget bara platshållaren, och objektet står kvar tills släppet.
- `appWindow()` på en FLYTANDE panel är dess eget fönster, och `find<>()` på huvudfönstret hittar den inte — nå panelen genom medlemmen. Drag-och-släpp fungerar inte mellan ett flytande fönster och huvudfönstret (DnD gäller inom ett fönster).
- Inget flytande fönster skapas förrän värdens fönster är öppet, och aldrig i ett offscreen-fönster (glaze-render, previewn) — där lämnar panelen bara layouten. Ett dolt startat huvudfönster skapar sina flytande fönster dolda.
- På Wayland (`isFloatDragSupported()` false) finns inget levande drag: draget visar platshållaren medan objektet står kvar, flytande går via menyn eller dubbelklicket, kompositören placerar fönstret och dess namnlist syns som flytthandtag; en sparad plats tillämpas inte (loggas en gång) och `layout()` skriver `-`.
- En värd som lossas ur sitt fönster medan paneler flyter lämnar deras fönster ägda av det gamla fönstret tills värden rivs.
- Stängknappen har inget veto — visa panelen igen om den inte fick stängas. Overlayn öppnas inte med hover, och den har inget eget skikt att dra i (måttet är panelens `width`/`height`).

<!-- END:widget-dock -->

<!-- BEGIN:widget-tool-bar | toolbar toolbutton verktygsfält verktygsknapp toolstrip ribbon knapprad wxauitoolbar qtoolbar checkable växlingsbar ikonknapp toggle rullgardin rullgardinsknapp dropdownbutton splitbutton delad-knapp ondropdown knappmeny band grepp grip lodrät vertikal toolstripcontainer flytande floating överflöde overflow canoverflow performclick -->
## ToolBar + ToolButton — verktygsfältet (`widget-tool-bar`)

**Alias:** toolbar, toolbutton, verktygsfält, verktygsknapp, toolstrip, ribbon, knapprad, wxauitoolbar, qtoolbar, checkable, växlingsbar, ikonknapp, toggle, rullgardin, rullgardinsknapp, dropdownbutton, splitbutton, delad-knapp, ondropdown, knappmeny, band, grepp, grip, lodrät, vertikal, toolstripcontainer, flytande, floating, överflöde, overflow, canoverflow, performclick
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/tool_bar.h), [header](Glaze/widgets/include/glaze/widgets/tool_button.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

`ToolBar` är en `Bar` dockad överst (förval) vars barn är vanliga widgets, packade vänster → höger — eller uppifrån och ned när fältet står vid Left/Right (WinForms `ToolStrip.Dock = Left`). Utan satt längd är fältet så långt som sitt innehåll. I en `DockHost` står verktygsfälten i BAND längs kanterna, med ett GREPP som användaren drar dem i — fältet LOSSNAR då till ett eget fönster som följer pekaren, och platshållaren visar var det dockas — och de kan FLYTA i ett eget fönster med en rubrik (VS:s flytande verktygsfält — se `widget-dock`). Det fönstret aktiveras aldrig. `ToolButton` är platt, tar ALDRIG fokus och är inget tabbstopp — ett klick på "Fet" lämnar caret i textfältet — och `setCheckable(true)` gör den till ett läge som slås av och på. Med MENYPOSTER blir den en RULLGARDINSKNAPP (WinForms `ToolStripDropDownButton`): en pil till höger, och ett tryck öppnar menyn under knappen; `setSplit(true)` gör den DELAD (`ToolStripSplitButton`) — knappen klickar, pilen öppnar. Ryms barnen inte i fältet FLÖDAR DET ÖVER (WinForms `ToolStrip.CanOverflow`, på som förval): de sista döljs och en ÖVERFLÖDSKNAPP ⋯ i fältets ände öppnar en meny med dem — knapparna med text, ikon, kryss och inaktivt läge, rullgardinerna som undermenyer, och en inbäddad kontroll (en `Dropdown`, ett sökfält) som en post som ger den fokus, varpå den visas i fältet tills fokus lämnar den. Registreringsnamn `toolbar` och `toolbutton`.

### API
- `ToolBar()` — `dock = Dock::Top`; höjd, luft och lucka ur stilen. `dock` Left/Right = STÅENDE (`bool isVertical() const`). Längden är innehållets när `width` (liggande) eller `height` (stående) är 0.
- `void setBand(int b)` / `int band() const` — bandet vid kanten i en `DockHost` (0 = yttersta; ett band utanför antalet blir ett nytt innersta). Värden skriver tillbaka det efter användarens drag.
- FLYTANDE (i en `DockHost`): `void setFloating(bool on)` / `bool isFloating() const` (eget fönster: en rubrik med titeln och ✕ över fältet, fast storlek) · `void setFloatable(bool on)` / `bool isFloatable() const` (användarens vägar — dubbelklicket på greppet och draget som lossar det; utan dem visar greppdraget bara platshållaren medan fältet står kvar. Appen kan alltid) · `void setFloatingPosition(float x, float y)` / `gfx::Pointf floatingPosition() const` (fönstrets plats i LOGISKA px, `AppWindow::setLocation`s rymd) · `void setText(TrText t)` / `const std::string& text() const` / `const TrText& textToken() const` (titeln i rubriken; tom = ramverkets "Verktygsfält"). Ett flytande fält LIGGER (`isVertical()` false); `dock` och `band()` är minnet för var det dockas tillbaka.
- `ToolButton`: `explicit ToolButton(TrText t = {})` · `void setText(TrText t)` · `void setImg(ImageSource img)` (en asset eller en `glaze::Bitmap` — en inaktiv ikon tonas lika) · `void setImagePos(ImagePos p)`
- `void setCheckable(bool on)` / `bool isCheckable() const` · `void setChecked(bool on)` / `bool isChecked() const`
- `Handler<ChangeArgs> onChange` · `Handler<ChangeArgs> onCommit` — användaren växlade (båda fyras FÖRE `onClick`, i den ordningen) · `Handler<ClickArgs> onClick`.
- RULLGARDINEN, systemfältsikonens menyvokabulär med den TOMMA stigen `{}` som roten och INGET genvägsargument: `void addItem(const MenuPath& path, TrText text)` · `void addCheck(const MenuPath& path, TrText text, bool checked = false)` · `void addRadio(…)` · `void addSeparator(const MenuPath& path)` · `void addSubmenu(const MenuPath& path, TrText text)` · `template <class F> void setHandler(const MenuPath& path, F&& f)` (postens sender är knappen) · `MenuItem* itemAtPath(const MenuPath& path)` · `Menu& menu()` · postens tillstånd `setItemIcon`/`setItemEnabled`/`setItemChecked(stig, …)`.
- `void setSplit(bool on)` / `bool isSplit() const` · `void openMenu()` · `void closeMenu()` · `bool isMenuOpen() const` · `Handler<EventArgs> onDropDown` — ANVÄNDAREN trycker fram menyn; fyras FÖRE den öppnas (fyll en dynamisk meny här), inte vid `openMenu()`.
- `void ToolButton::performClick()` — klicka programmatiskt (WinForms `PerformClick`): en växlingsbar knapp växlar och fyrar `onChange`/`onCommit`, sedan `onClick`; no-op när knappen är inaktiverad. Också på en knapp med poster är det KLICKET — `openMenu()` öppnar menyn.
- ÖVERFLÖDET: `void setOverflow(bool on)` / `bool overflow() const` (förval `true`; `false` = barnen rinner över och klipps; i `.glazeui` `verktyg.setOverflow(false);`). Menyn byggs vid varje tryck på ⋯ ur de dolda barnen i ordning: en `ToolButton` blir en post med sin text (annars sitt verktygstips), ikon, kryss och inaktiva läge som klickar knappen (`performClick`, sender är knappen); en knapp med poster blir en undermeny som aktiverar knappens egna poster (en delad knapp först med sitt klick) och dess `onDropDown` fyras innan; en `Separator` blir en skiljelinje; en kontroll som kan ta fokus blir en post med sitt verktygstips som text (annars ramverkets "Kontroll") som ger den fokus. I en `DockHost` krymper fälten i ett band bakifrån ned till greppet och knappen, så varje fält får sin ⋯.

### Exempel
```cpp
class Rita : public glaze::AppWindow {
public:
    Rita() : glaze::AppWindow("Rita", 640, 400)
    {
        spara.setImg(glaze::img("toolbar", "save"));
        spara.setToolTip("Spara (Ctrl+S)");     // en ikonknapp behöver en tooltip
        spara.onClick = [this] { setTitle("Rita — sparad"); };

        // `linje` är en Separator — i en bar står den tvärs baren av sig själv

        rutnat.setText("Rutnät");
        rutnat.setCheckable(true);      // ett läge som slås av och på
        rutnat.onChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            const bool pa = static_cast<glaze::ToolButton*>(s)->isChecked();
            setTitle(pa ? "Rita — rutnät" : "Rita");
        };

        zoom.setItems({"50 %", "100 %", "200 %"});   // vilken widget som helst
        zoom.setSelectedIndex(1);
        zoom.width = 90;

        ny.setText("Ny");               // med poster: en pil, trycket öppnar menyn
        ny.addItem({}, "Ritning");      // `{}` är roten
        ny.addItem({}, "Mall");
        ny.setHandler("Ritning", [this] { setTitle("Rita — ny ritning"); });

        angra.setText("Ångra");
        angra.setSplit(true);           // DELAD: knappen klickar, pilen öppnar
        angra.addItem({}, "Allt");
        angra.onClick = [this] { setTitle("Rita — ångrat"); };
        angra.onDropDown = [this] {     // FÖRE menyn öppnas — anpassa den här
            angra.setItemEnabled("Allt", rutnat.isChecked());
        };

        root().adopt(verktyg);          // Dock::Top är förvalet
        verktyg.adopt(spara);
        verktyg.adopt(linje);
        verktyg.adopt(rutnat);
        verktyg.adopt(zoom);
        verktyg.adopt(ny);
        verktyg.adopt(angra);
    }

private:
    glaze::ToolBar    verktyg;
    glaze::ToolButton spara, rutnat, ny, angra;
    glaze::Separator  linje;
    glaze::Dropdown   zoom;
};
```

### .glazeui
```cpp
spara.setImg(glaze::img("toolbar", "save"));
spara.setToolTip("Spara (Ctrl+S)");
rutnat.setText("Rutnät");
rutnat.setCheckable(true);
ny.setText("Ny");
ny.addItem({}, "Ritning");
ny.addItem({}, "Mall");
ny.setItemEnabled("Mall", false);
angra.setText("Ångra");
angra.setSplit(true);
angra.addItem({}, "Allt");
spara.onClick = [this](glaze::Widget* s, glaze::ClickArgs& e) { sparaClicked(s, e); };
angra.onDropDown = [this](glaze::Widget* s, glaze::EventArgs& e) { angraDropDown(s, e); };
root().adopt(verktyg);
verktyg.adopt(spara);
verktyg.adopt(linje);
verktyg.adopt(rutnat);
verktyg.adopt(ny);
verktyg.adopt(angra);
```
Menyns STRUKTUR står i filen (previewn ritar pilen och bygger menyn — den öppnar den aldrig); postens handler sätts i kod med `setHandler`, som på menyraden.

### Fallgropar
- Verktygsknappar nås inte med tangentbordet — ge varje funktion också en menypost med genväg (se `menus`).
- En skiljelinje är en `Separator` — i en bar står den tvärs baren, också när fältet vänder (`setVertical` spelar ingen roll där).
- Barnens dock/anchors/x/y ignoreras i remsan; ett barn utan autostorlek (t.ex. `ListBox`) måste ha `width`.
- Radiogrupper av verktyg finns inte inbyggt — bygg dem med `setCheckable` + `setChecked` i `onClick`.
- En knapp MED poster fyrar inget `onClick` och växlar inte — trycket är menyns. Ska knappen både göra något och ha en meny: `setSplit(true)`.
- Knappmenyn har INGA genvägar (inget fönster frågar den) — `addItem` tar inget tredje argument. Lägg genvägen i menyraden.
- En knapp UTAN poster är en vanlig knapp (ingen pil, inget `onDropDown`). En dynamisk meny som fylls i `onDropDown` behöver därför minst en post från början.
- Menyn öppnas alltid UNDER knappen, också i ett lodrätt verktygsfält. `onDropDown` fyras inte av `openMenu()`.
- Ett fält som byter riktning (draget från överkanten till vänsterkanten i en `DockHost`) byter också `width` och `height` — tjockleken förblir tjocklek. Läs måtten efter `onChange`, inte före. Detsamma när ett STÅENDE fält börjar flyta (det ligger då) och dockas tillbaka.
- Ett flytande fälts fönster skapas först när värdens fönster är öppet och aldrig offscreen (glaze-render, previewn) — där lämnar fältet bara sitt band. Fönstrets egen stängning (Alt+F4) och rubrikens ✕ DÖLJER fältet; `show()` tar fram det igen. `appWindow()` på ett flytande fält är dess eget fönster.
- På Wayland syns det flytande fönstrets namnlist (kompositören placerar det och den är flytthandtaget), rubriken står under den, och ett drag i rubriken har inget mål — dockningen är dubbelklicket. Greppdraget lossar inte fältet där (inget levande drag): platshållaren visas medan det står kvar.
- `setAllowedEdges({glaze::Dock::Top, glaze::Dock::Bottom})` begränsar kanterna ANVÄNDAREN får docka fältet vid (förval alla fyra, `{}` = bara flyta); ett drag till en annan kant visar ingen platshållare, och ett buret fält som släpps där flyter. Appen kan alltid (`dock`, `setLayout`). En otillåten kant är inget mål — också fältets nuvarande. I `.glazeui` samma anrop.
- `ToolButton::setIconSize(32)` ger knappen en större ikon, och fältets AUTO-tjocklek växer till den högsta synliga verktygsknappen (aldrig under stilens `toolBarH`; andra barn räknas inte).
- Ge varje ikonknapp OCH varje inbäddad kontroll ett verktygstips — det är postens text i överflödsmenyn (en kontroll utan tips heter "Kontroll" där). En `Label` eller `Progress` i fältet har ingen post: den går inte att nå, bara att se.
- Ett dolt barn i överflödet behåller sitt `isVisible()` — det är bara layouten som döljer det. Läs aldrig "syns knappen?" ur `isVisible()`. En kontroll i överflödet är fortfarande ett tabbstopp: når Tab den visas den i fältet medan den har fokus.
- Ett fält flödar bara över när det är kortare än sitt innehåll: dockat i ett vanligt fönster (bredden är fönstrets), i ett band, eller med en satt längd. Ett flytande fält och ett i sin egen längd (`Dock::None`) flödar aldrig över. Överflödsknappen är fältets egen — den går inte att lossa och står inte bland appens barn i `.glazeui`.

<!-- END:widget-tool-bar -->

<!-- BEGIN:widget-bars | bar topbar statusbar spacer captionbutton statusrad namnlist titlebar titelrad remsa flex fönsterknappar caption chrome statusstrip statuspanel statusbarpanel fält panels plain ren-rad knapprad setplain -->
## Bar, TopBar, StatusBar, Spacer, CaptionButton — fönstrets remsor (`widget-bars`)

**Alias:** bar, topbar, statusbar, spacer, captionbutton, statusrad, namnlist, titlebar, titelrad, remsa, flex, fönsterknappar, caption, chrome, statusstrip, statuspanel, statusbarpanel, fält, panels, plain, ren-rad, knapprad, setplain
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [bar.h](Glaze/widgets/include/glaze/widgets/bar.h), [top_bar.h](Glaze/widgets/include/glaze/widgets/top_bar.h), [status_bar.h](Glaze/widgets/include/glaze/widgets/status_bar.h), [spacer.h](Glaze/widgets/include/glaze/widgets/spacer.h), [caption_button.h](Glaze/widgets/include/glaze/widgets/caption_button.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

`Bar` är en flexrad: barnen packas vänster → höger i adopt-ordning med sin mätta bredd, och `Spacer`-barn delar den lediga luckan efter vikt. `StatusBar` är en `Bar` dockad nederst och finns som FÄLTET `statusBar` på varje `AppWindow`, dold tills `statusBar.show();`. `TopBar` är namnlisten som fönstret skapar självt; `CaptionButton` är dess fönsterknappar. Registreringsnamn `bar`, `statusbar`, `topbar`, `spacer`, `captionbutton`.

### API
- `Bar`: `Spacer* addSpacer(float grow = 1.0f)` — höjden AUTO = stilens `barHeight`.
- `Bar`: `void setPlain(bool on)` / `bool isPlain() const` — REN RAD: bara flexen, utan fyllning och hårlinje, och barnen behåller sin MÄTTA höjd (centrerad i baren) i stället för att fylla den — en knapprad i en dialog som inte ska se ut som ett verktygsfält. Av som förval. Ett barn utan eget mått (en `Spacer`) fyller baren som förut.
- `Spacer`: `explicit Spacer(float g = 1.0f)` · `void setGrow(float g)` / `float grow() const` — grow 0 + `width` = fast lucka.
- `AppWindow::statusBar` (värdefält, `glaze::StatusBar`) · `TopBar* AppWindow::topBar()` — `nullptr` med `Chrome::Native`/`None`.
- `StatusBar`-FÄLTEN (WinForms `StatusBarPanel`): `struct StatusPanel { TrText text; float width = 0.0f; bool fill = false; }` · `void addPanel(TrText text = {}, float width = 0.0f, bool fill = false)` · `void setPanel(int index, TrText text, float width = 0.0f, bool fill = false)` (tilldelning) · `void setPanels(std::vector<StatusPanel> panels)` · `void setPanelText(int index, TrText text)` (bara texten) · `int panelCount() const` · `StatusPanel panel(int index) const` · `const std::string& panelText(int index) const`. Bredden `> 0` är fast (logiska px, ellips), `<= 0` AUTO (texten + luften), och `fill` lägger en lika andel av det LEDIGA ovanpå bredden — den är golvet, så ett fyllande fält kapar aldrig sin text när raden rymmer den. Ryms inte raden rinner den över åt höger; ett fyllande fält som hellre ska kortas får en liten satt bredd.
- `TopBar`: `Label* titleLabel() const` · `CaptionButton* minButton() const` · `maxButton()` · `closeButton()`.
- `CaptionButton`: `enum class Kind { Minimize, Maximize, Close }` · `enum class Shape { Glyph, Circle, Capsule }` · `setKind`/`setShape`/`setRestoreGlyph`.

### Exempel
```cpp
class Huvud : public glaze::AppWindow {
public:
    Huvud() : glaze::AppWindow("Huvudfönster", 640, 400)
    {
        luft.setGrow(0);                // grow 0 + width = fast lucka
        luft.width = 8;
        status.setText("Klar");
        status.setRole(glaze::Label::Role::Dim);
        position.setText("Rad 1, kolumn 1");

        statusBar.show();               // fönstrets statusrad, dold tills show()
        statusBar.adopt(luft);          // barnen packas vänster → höger
        statusBar.adopt(status);
        statusBar.adopt(fyll);          // Spacer med grow 1 tar luckan
        statusBar.adopt(position);

        if (glaze::TopBar* tb = topBar())   // nullptr utan eget chrome
            tb->add<glaze::Button>("Hjälp");   // SIST: efter fönsterknapparna (Win/Linux)
    }

private:
    glaze::Spacer luft, fyll;
    glaze::Label  status, position;
};
```

### .glazeui
```cpp
luft.setGrow(0);
luft.width = 8;
statusBar.show();
statusBar.adopt(luft);
statusBar.adopt(status);
statusBar.adopt(fyll);
statusBar.adopt(position);
knappar.setPlain(true);
```
Med FÄLT i stället för egna barn:
```cpp
statusBar.show();
statusBar.setPanels({{"Klar", 0, true}, {"Rad 1, kol 1", 110}});
statusBar.addPanel("100 %");
statusBar.setPanelText(1, "Rad 12, kol 4");
```
`statusBar` (och `menuBar`) deklareras inte i GlazeBuilder-regionen — de finns redan på fönstret. Namnlisten anpassas bara i kod.

### Fallgropar
- `topBar()->add<T>(…)` lägger barnet SIST — på Windows/Linux efter fönsterknapparna; det finns ingen insättning på plats.
- `setPlain(true)` gäller en vanlig `Bar`; `TopBar`/`StatusBar`/`ToolBar` är chrome och inte gjorda för den. Höjden är densamma som förut (`height`, annars `barHeight`): en rad med högre barn behöver en satt höjd.
- Barnens dock/anchors/x/y ignoreras i en `Bar`; bara mätt bredd och `Spacer`-vikt räknas. En fristående `Bar` har ingen AUTO-bredd — docka den eller sätt `width`.
- Fönsterknapparna döljs med `setControlBox`/`setMinimizeBox`/`setMaximizeBox` på fönstret (se `app-window`), inte genom att röra `CaptionButton`.
- Statusradens fält är BARN i barnordning bland appens egna (en förloppsindikator står där den adopteras) — i en handskriven `.glazeui` står fältsatserna EFTER trädbygget när raden också bär appens barn. `setPanels` ändrar de befintliga fälten på plats. En inaktiverad rad dämpar sina fält; appens egna barn släcks för sig.
- Namnlisten bär programikonen (`appIcon`, ett rotbarn i chrome-trädet och INTE ett barn i `TopBar`): vänster på Windows/Linux, höger på macOS, med en motvikt på andra sidan så titeln förblir centrerad. Den STORA ikonen täcker namnlist + menyrad och namnlistens hårlinje hoppar över den (`app-icon`).
<!-- END:widget-bars -->

<!-- BEGIN:widget-list-view | listview listvy table tabell kolumner columns details report gridview wxlistctrl qtableview multiselect flerval sortering sort onactivate kryss checkboxes checks oncheck -->
## ListView — listvyn, och basen Table (`widget-list-view`)

**Alias:** listview, listvy, table, tabell, kolumner, columns, details, report, gridview, wxlistctrl, qtableview, multiselect, flerval, sortering, sort, onactivate, kryss, checkboxes, checks, oncheck
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [list_view.h](Glaze/widgets/include/glaze/widgets/list_view.h), [table.h](Glaze/widgets/include/glaze/widgets/table.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

En rullande tabell med kolumnhuvud: rader av celler, valfri ikon per rad, ETT tabbstopp och tusentals rader i O(synliga). Klick i huvudet sorterar (tabellen sorterar SJÄLV), avdelarna dras för bredd, och valet är enkelt eller — med `setMultiSelect(true)` — Utforskarens flerval. Kolumnerna, raderna och sorteringen ärvs från den oregistrerade basen `glaze::Table`, som `DataGrid` delar. Registreringsnamn `listview`.

**Kryssen** (`setShowChecks(true)`, WinForms `ListView.CheckBoxes`) är en ruta per rad FÖRST i första kolumnen, före ikonen — ListBoxens och trädets ord rakt av. De är OBEROENDE av valet: ett klick i rutan vänder krysset utan att flytta markeringen, Space vänder den aktuella radens (också i enkelval), Ctrl+Space vänder valet som förut, och ett dubbelklick i rutan är två vändningar och ingen aktivering. Krysset hör till RADEN (appens index) och följer den genom sorteringen.

### API
- `void addColumn(TrText text, float width = 0.0f, Align align = Align::Left, std::optional<Align> headerAlign = std::nullopt)` — `width` i logiska px: `0` = stilens `columnDefaultW`, `Column::autoWidth` (−1) = EFTER INNEHÅLLET (rubriken och cellerna, med luft; se fallgroparna); `headerAlign` utelämnad = som cellerna.
- `void setColumn(int index, TrText text, float width = 0.0f, Align align = Align::Left, std::optional<Align> headerAlign = std::nullopt)` · `void setColumns(std::vector<Column> columns)` — `struct Column { static constexpr float autoWidth = -1.0f; TrText text; float width = 0.0f; Align align = Align::Left; std::optional<Align> headerAlign; }`.
- `void addRow(std::vector<std::string> cells, ImageSource img = {}, std::optional<gfx::Color> color = std::nullopt)` · `void setRow(int index, std::vector<std::string> cells, ImageSource img = {}, std::optional<gfx::Color> color = std::nullopt)` · `void setRows(std::vector<Row> rows)` — `struct Row { std::vector<std::string> cells; ImageSource img{}; std::optional<gfx::Color> color{}; }`, ikonen (asset eller `Bitmap`) i första kolumnen.
- `void setRowColor(int row, std::optional<gfx::Color> color)` / `std::optional<gfx::Color> rowColor(int row) const` — radens TEXTFÄRG, appens (en status): syns på omarkerade rader, följer raden genom sorteringen; `setRow` utan färg nollar, `setCell` lämnar den.
- `void setCellColor(int row, int column, std::optional<gfx::Color> background, std::optional<gfx::Color> text)` / `std::optional<gfx::Color> cellBackground(int row, int column) const` / `std::optional<gfx::Color> cellTextColor(int row, int column) const` — EN cells bakgrund och textfärg, för matriser där varje cell har ett eget tillstånd. Tomt (`std::nullopt`/`{}`) = radens eller stilens färg: en tom text är radens färg (`setRowColor`) och annars temats, en tom bakgrund är ingen. Tilldelning (en utelämnad färg nollas); kolumnen behöver inte finnas än.
- `void setRowHeaders(bool on)` / `bool rowHeaders() const` — RADRUBRIKER: första kolumnen blir en fast kolumn i rubrikens stil som står kvar när tabellen rullas i sidled; cellerna i kolumn 0 är radernas namn.
- `void setCell(int row, int column, std::string text)` · `const std::string& cell(int row, int column) const` · `int rowCount() const` · `int columnCount() const`
- `void setSortKey(int row, int column, double key)` · `void setSortColumn(int column)` (`-1` = ingen) · `void setSortAscending(bool on)` · `void setSortable(bool on)`
- `void setHeaderLines(int lines)` (0 = AUTO) · `void setHeaderVAlign(VAlign v)` · `void setColumnWidth(int index, float width)`
- `void setMultiSelect(bool on)` · `void setSelectedIndex(int row)` / `int selectedIndex() const` · `void setSelected(int row, bool on)` · `std::vector<int> selectedRows() const` · `void selectAll()` · `void clearSelection()`
- `int currentRow() const` · `int displayRow(int row) const` · `int rowAtDisplay(int position) const` · `void ensureVisible(int row)`
- `void setShowChecks(bool on)` / `bool showsChecks() const` · `void setChecked(int row, bool on)` / `bool isChecked(int row) const` (utanför ⇒ logg + no-op / false; fyrar aldrig) · `std::vector<int> checkedRows() const` (visningsordning) · `int changedRow() const` (kryssets rad i `onCheck`/`onCommit`; −1 efter ett val och efter `setRows`)
- `Handler<ChangeArgs> onChange` (valet) · `Handler<ChangeArgs> onCheck` (ANVÄNDAREN vände ett kryss) · `Handler<ChangeArgs> onCommit` (samma val eller samma kryss, direkt efter — skilj med `changedRow()`) · `Handler<ActivateArgs> onActivate` (dubbelklick eller Enter — Enter tas bara när `onActivate` är satt och en rad är aktuell, annars når den fönstrets standardknapp).

### Exempel
```cpp
class Filer : public glaze::AppWindow {
public:
    Filer() : glaze::AppWindow("Filer", 520, 320)
    {
        lista.dock = glaze::Dock::Fill;   // ingen autostorlek
        lista.addColumn("Namn", 200);
        lista.addColumn("Storlek", 80, glaze::Align::Right);
        lista.addColumn("Ändrad", 110);
        lista.addRow({"Rapport.pdf", "12 kB", "2026-10-07"}, glaze::img("ikoner", "pdf"));
        lista.addRow({"Semester.png", "3 MB", "2026-09-30"});
        lista.setSortKey(0, 1, 12288);    // sorteras på bytes, visas som text
        lista.setSortKey(1, 1, 3145728);
        lista.setSortColumn(0);           // ett klick i huvudet gör samma sak
        lista.setRowColor(1, gfx::Color{200, 40, 40});   // appens status
        lista.setMultiSelect(true);
        lista.setShowChecks(true);        // en ruta per rad, före ikonen
        lista.setChecked(1, true);        // RADEN, inte platsen
        lista.onChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            auto* lv = static_cast<glaze::ListView*>(s);
            setTitle(std::to_string(lv->selectedRows().size()) + " valda");
        };
        lista.onCheck = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            auto* lv = static_cast<glaze::ListView*>(s);   // VILKEN: changedRow()
            setTitle(lv->cell(lv->changedRow(), 0) +
                     (lv->isChecked(lv->changedRow()) ? " kryssad" : " avkryssad"));
        };
        lista.onActivate = [this](glaze::Widget* s, glaze::ActivateArgs&) {
            auto* lv = static_cast<glaze::ListView*>(s);
            setTitle("Öppnar " + lv->cell(lv->currentRow(), 0));   // appens index
        };

        root().adopt(lista);
    }

private:
    glaze::ListView lista;
};
```

### .glazeui
```cpp
lista.addColumn("Namn", 200);
lista.addColumn("Storlek", 80, glaze::Align::Right);
lista.addRow({"Rapport.pdf", "12 kB", "2026-10-07"}, glaze::img("ikoner", "pdf"));
lista.setSortKey(0, 1, 12288);
lista.setSortColumn(0);
lista.setMultiSelect(true);
lista.setColumns({{"Namn", 200}, {"Storlek", 80, glaze::Align::Right}});
lista.setRows({{{"a.txt", "12 kB"}}, {{"b.png", "3 MB"}, glaze::img("ikoner", "bild"), gfx::Color{0, 140, 0}}, {.cells = {"c.txt"}, .color = std::nullopt}});
lista.setRowColor(0, gfx::Color{200, 40, 40});
lista.setCellColor(1, 1, gfx::Color{255, 224, 224}, gfx::Color{160, 0, 0});
lista.setCellColor(1, 0, std::nullopt, {});
lista.setRowHeaders(true);
lista.setColumnWidth(1, glaze::Table::Column::autoWidth);
lista.setShowChecks(true);
lista.setChecked(1, true);
lista.onActivate = [this](glaze::Widget* s, glaze::ActivateArgs& e) { listaActivated(s, e); };
lista.onCheck = [this] { listaKryssad(); };
```
`setShowChecks` och `setRowHeaders` är setter-fält (modellen bär dem som `showChecks` och `rowHeaders`); `setChecked(rad, på)` och `setCellColor(rad, kolumn, bakgrund, text)` är anropssatser, som ListBoxens `setChecked`. Konstanten för bredden efter innehållet skrivs `glaze::Table::Column::autoWidth` (eller talet `-1`) — i kolumnlistan, i `addColumn`/`setColumn` och i `setColumnWidth`.

### Fallgropar
- INDEXEN ÄR APPENS: sorteringen ändrar bara visningsordningen, så `setRow(3, …)`, valet, krysset, radens färg och `currentRow()` följer raden. Skärmplats ↔ rad med `displayRow`/`rowAtDisplay`.
- Med flerval väljs en REDAN vald rad ensam först när knappen SLÄPPS — så att hela valet går att dra (`setAllowDrag(true)` + `onDragStart` fyller lasten ur `selectedRows()`). Ett drag ger ingen `mouseUp`. Ett drag börjar ALDRIG i huvudet, i en kolumnavdelare, i en kryssruta eller i en rullningslist, också när `onDragStart` fyller lasten.
- KRYSSEN och MASSFORMEN: `setRows` nollar kryssen (som valet), `setRow` behåller dem och `addRow` ger en okryssad rad — kryssa med `setChecked` efter `setRows`.
- Radens färg gäller texten på en omarkerad rad; markeringens färg vinner på en vald, och en inaktiverad tabell dimmar allt. Rutan och bocken följer alltid temat. Kontrasten är appens ansvar.
- CELLFÄRGERNA (`setCellColor`) följer radfärgens regler: de ligger i RADEN (följer sorteringen), `setRow`/`setRows` nollar dem och `setCell` lämnar dem, MARKERINGEN VINNER (en vald rad visar bara markeringens färger — varken cellens bakgrund eller text) och en inaktiv tabell visar inga appfärger alls. Cellens bakgrund fylls över radens hover och under rutnätet; med radrubriker ritas kolumn 0 utan appfärger.
- RADRUBRIKER (`setRowHeaders(true)`): kolumn 0 står kvar vid sidledsrullning, ritas som kolumnhuvudet (yta och hårlinjer) och tonas aldrig av valet eller hovern; ett klick på ett namn väljer raden som ett klick på dess data. Den är en vanlig kolumn i övrigt (sorteras på sin rubrik, byter bredd, kan vara `autoWidth`), men får inga cellfärger, och kryssrutan (`setShowChecks`) står kvar i remsan.
- KOLUMNBREDD EFTER INNEHÅLLET: `Column::autoWidth` följer rubrikens bredaste rad och cellernas bredaste innehåll (med luft, ikon och kryssrutans plats i kolumn 0), plus en hårlinjes andrum (så ingen text får en ellips), aldrig smalare än stilens `columnMinW`, och räknas om när rader, rubriker eller stil ändras. Sorteringspilens plats är alltid reserverad (ett klick i huvudet ändrar aldrig en bredd). Alla rader mäts — en tabell med hundratusentals rader ska ha fasta bredder. En dragen avdelare skriver en vanlig fast bredd, och ett DUBBELKLICK på avdelaren skriver den anpassade bredden SOM en fast bredd ("anpassa nu", inte "följ"); fyllkolumnens avdelare går inte att dra.
- Cellerna är `std::string` (fyll om vid språkbyte); kolumnrubrikerna är `TrText`.
- Sorteringen är naturlig ("fil2" före "fil10") men jämför å/ä/ö bytevis — sätt en `setSortKey` när visningen och ordningen skiljer sig.
- Ingen autostorlek, ingen typeahead, ingen kryssruta i huvudet (markera alla), ingen inbyggd omordning genom drag; kolumnerna kan inte flyttas eller döljas, och den sista sträcks inte. En första kolumn smalare än rutan låter rutan sticka ut i nästa kolumn.
- `setFillColumn(0)` låter kolumnen ta resten av bredden (den satta bredden är dess minimum, och dess egen avdelare går inte att dra); `setIconSize(48)` ger miniatyrer — varje rad reserverar ikonens plats och raden växer med den. Båda finns på `DataGrid` också.
- SNABBMENYN: ett högertryck väljer raden som Utforskaren, och `contextRow()` (appens index, −1 i huvudet och på tom yta) är raden i `onContextMenu`; `rowAt(e.local.x, e.local.y)` är träffvägen.

<!-- END:widget-list-view -->

<!-- BEGIN:widget-data-grid | datagrid datagridview grid rutnät tabell kalkylblad spreadsheet redigerbar editable celler cells wxgrid qtablewidget cellchangeargs columneditor oncommit selectionmode fullrow radval multiselect onselectionchange -->
## DataGrid — tabellen med redigerbara celler (`widget-data-grid`)

**Alias:** datagrid, datagridview, grid, rutnät, tabell, kalkylblad, spreadsheet, redigerbar, editable, celler, cells, wxgrid, qtablewidget, cellchangeargs, columneditor, oncommit, selectionmode, fullrow, radval, multiselect, onselectionchange
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [data_grid.h](Glaze/widgets/include/glaze/widgets/data_grid.h), [table.h](Glaze/widgets/include/glaze/widgets/table.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

Listvyns tabell (samma bas `glaze::Table`: kolumner, rader, sortering, huvud) där cellerna går att redigera. Varje kolumn är fritext, en vallista eller skrivskyddad. Man börjar med dubbelklick, F2 eller genom att skriva; Enter sparar och går ner, Tab sparar och går åt höger, Escape avbryter. `onChange` säger VILKEN cell användaren sparade, och `onCommit` kommer direkt efter med samma cell. Registreringsnamn `datagrid`.

### API
- Kolumner, rader, celler, radens färg (`setRowColor`, `Row::color`), cellens färger (`setCellColor`), radrubriker (`setRowHeaders`), bredd efter innehållet (`Column::autoWidth`, dubbelklick på avdelaren) och sortering: som `ListView` (se `widget-list-view`). Den AKTUELLA cellen behåller markeringens färger (i `FullRow`: den valda raden); resten bär appens — cellens, annars radens.
- `enum class Editor { Text, Choice, None }` · `void setColumnEditor(int column, Editor editor)` / `Editor columnEditor(int column) const`
- `void setColumnChoices(int column, std::vector<std::string> choices)`
- `void setCurrentCell(int row, int column)` · `int currentColumn() const` · `int currentRow() const`
- `void beginEdit()` · `void commitEdit()` (fyrar `onChange` och `onCommit` om cellen ändrades) · `void cancelEdit()` · `bool isEditing() const`
- `Handler<CellChangeArgs> onChange` · `Handler<CellChangeArgs> onCommit` — `struct CellChangeArgs : ChangeArgs { int row = -1; int column = -1; }`; båda för samma sparade cell, `onChange` först.
- RADVALET (WinForms `SelectionMode`/`MultiSelect`): `enum class SelectionMode { Cell, FullRow }` · `void setSelectionMode(SelectionMode m)` / `SelectionMode selectionMode() const` (förval `Cell`) · `void setMultiSelect(bool on)` / `bool isMultiSelect() const` · `void setSelected(int row, bool on)` / `bool isSelected(int row) const` · `std::vector<int> selectedRows() const` (visningsordning) · `void selectAll()` · `void clearSelection()` · `Handler<ChangeArgs> onSelectionChange` — en gång per användarhandling som ändrade valet. I `FullRow` gör ett klick cellen aktuell OCH väljer raden, en flytt flyttar valet, Ctrl/Shift med flerval, Ctrl+A alla.

### Exempel
```cpp
class Uppgifter : public glaze::AppWindow {
public:
    Uppgifter() : glaze::AppWindow("Uppgifter", 520, 320)
    {
        tabell.dock = glaze::Dock::Fill;
        tabell.setColumns({{"Uppgift", 220}, {"Status", 100},
                           {"Id", 60, glaze::Align::Right}});
        tabell.setColumnEditor(1, glaze::DataGrid::Editor::Choice);
        tabell.setColumnChoices(1, {"Ny", "Pågår", "Klar"});
        tabell.setColumnEditor(2, glaze::DataGrid::Editor::None);   // skrivskyddad
        tabell.addRow({"Skriv grinden", "Pågår", "24"});
        tabell.addRow({"Uppdatera TODO", "Ny", "3"});
        tabell.onChange = [this](glaze::Widget*, glaze::CellChangeArgs& e) {
            // VILKEN cell står i args, värdet läses ur widgeten.
            setTitle("Rad " + std::to_string(e.row) + ": " +
                     tabell.cell(e.row, e.column));
        };

        root().adopt(tabell);
    }

private:
    glaze::DataGrid tabell;
};
```

### .glazeui
```cpp
tabell.setColumnEditor(1, glaze::DataGrid::Editor::Choice);
tabell.setColumnChoices(1, {"Ny", "Pågår", "Klar"});
tabell.setColumns({{"Uppgift", 220}, {"Status", 100}, {"Id", 60, glaze::Align::Right}});
tabell.setColumnEditor(2, glaze::DataGrid::Editor::None);
tabell.addRow({"Skriv grinden", "Pågår", "24"});
tabell.setCurrentCell(0, 1);
tabell.setSelectionMode(glaze::DataGrid::SelectionMode::FullRow);
tabell.setMultiSelect(true);
tabell.setSelected(0, true);
tabell.onChange = [this](glaze::Widget* s, glaze::CellChangeArgs& e) { tabellChanged(s, e); };
tabell.onSelectionChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { tabellValt(s, e); };
```
Kolumntypen hör till INDEXET och överlever `setColumns`, så satsordningen spelar ingen roll.

### Fallgropar
- En val-kolumn UTAN val går inte att redigera.
- `setRows`, `setColumns` och `setCurrentCell` AVBRYTER en pågående redigering (spara först med `commitEdit`); `setRow`, `setCell` och `addRow` gör det inte.
- När fokus lämnar editorn sparas cellen direkt, men `onChange` och `onCommit` kommer i pump-svansen — och uteblir om raderna bytts innan.
- I `Cell`-läget (förval) är inga rader valda — `selectedRows()` är tom även efter `setSelected`, som lagras och syns när läget blir `FullRow`. Valets event heter `onSelectionChange`, inte `onChange` (som är cellernas). `setRows` nollar valet, `setRow` behåller det.
- Ingen områdesmarkering, ingen validering, inga kryssrutekolumner (listvyn har kryss per RAD, `widget-list-view` — i datagriden vore ett kryss ett cellvärde); Delete tömmer inte en cell.
- Med `setRowHeaders(true)` är kolumn 0 en rubrik: den går inte att redigera, är aldrig den aktuella cellen (ett klick på ett namn gör raden aktuell och lämnar kolumnen; Left och Home stannar i kolumn 1) och står kvar vid sidledsrullning.

<!-- END:widget-data-grid -->

<!-- BEGIN:widget-tree-view | treeview träd trädvy tree noder nodes hierarki hierarchy treepath treenode wxtreectrl qtreeview expand fäll utfällning onexpand multiselect flerval kryss checks oncheck nodfärg setnodecolor kolumner columns treelistview celler cells setcell sortering sort columnpanel -->
## TreeView — trädet (`widget-tree-view`)

**Alias:** treeview, träd, trädvy, tree, noder, nodes, hierarki, hierarchy, treepath, treenode, wxtreectrl, qtreeview, expand, fäll, utfällning, onexpand, multiselect, flerval, kryss, checks, oncheck, nodfärg, setnodecolor, kolumner, columns, treelistview, celler, cells, setcell, sortering, sort, columnpanel
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/tree_view.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

Noder med text, valfri ikon och egna barn, fällda med pilen eller Vänster/Höger. Noderna är DATA (ETT tabbstopp), och en nod adresseras med en INDEXSTIG — `glaze::TreePath` = `std::vector<int>`, `{0, 2}` = första toppnodens tredje barn, `{}` = roten. Valet, krysset och utfällningen följer NODEN, inte platsen. Dubbelklick eller Enter aktiverar den AKTUELLA noden (`onActivate`). Registreringsnamn `treeview`.

**Flerval** (`setMultiSelect(true)`, listvyns form): klick väljer bara noden (en redan vald nod väljs ensam först vid SLÄPPET, så hela valet går att dra) · Ctrl+klick vänder (Cmd på macOS) · Shift+klick spänner från ankaret i SYNLIG ordning · Shift/Ctrl+pilar · Ctrl+Space vänder den aktuella · Ctrl+A väljer de synliga. **Kryssen** (`setShowChecks(true)`) är OBEROENDE per nod — ingen kaskad — och en ruta efter pilen: ett klick i den vänder krysset utan att flytta valet, Space vänder den aktuellas (också i enkelval). **Textfärgen per nod** (`setNodeColor`) är appens: den syns på omarkerade rader, markeringens färg vinner på en vald, och pilen och bocken följer temat.

**Appens godkännande av draget** (med `setAllowDrag` + `setAllowDrop`): trädet FÖRESLÅR en flytt (`Move`) på varje plats där det går, och appen AVGÖR. Regeln per plats står i `onDragOver` — den fyras också direkt efter `onDragEnter`, så den gäller redan på den första platsen — och `e.effect = None` släcker linjen och ger förbudsmärket. Släppet frågar appen FÖRST: `onDrop` ser det OFLYTTADE trädet och kan neka (`e.effect = None`, trädet rörs inte), utföra släppet själv (`moveNodes`/`setNodes` och `e.handled = true`) eller låta trädet flytta till EXAKT den lucka appen såg. Vad som dras läses ur `draggedPaths()` och var det hamnar ur `dropParent()`/`dropIndex()` — luckan hålls vid nodernas id, så en handler som ändrar trädet flyttar aldrig noderna till fel ställe (finns luckan inte längre blir släppet `None`).

**Kolumner** (REQ8 13 — tabelldata i en hierarki): tabellens kolumner ur den delade basen `glaze::ColumnPanel` — samma `Column`-aggregat, `addColumn`/`setColumn(index, …)`/`setColumns(lista)`, `setColumnWidth`, `setFillColumn`, `setHeaderLines`, avdelardraget och sorteringspilen (se `widget-list-view`). KOLUMN 0 är nodens text med indrag, pil, kryss och ikon (kortad med "…" mot kolumnens kant), kolumn 1… nodens CELLER: `TreeNode::cells` (`cells[0]` = kolumn 1), `setCell(stig, kolumn, text)` och `cell(stig, kolumn)` (kolumn 0 = nodens text). `setSortColumn(n)`/`setSortAscending`/`setSortable` och ett klick i huvudet sorterar VARJE syskonlista naturligt ("9" före "12") — STIGARNA förblir appens, sorteringen är bara visning. Utan kolumner har trädet inget huvud och ritar som förut; med kolumner rullar det i sidled som tabellen. BREDD EFTER INNEHÅLLET som tabellens: `Column::autoWidth` (−1) följer rubriken och de SYNLIGA nodernas innehåll — kolumn 0 mäts med indrag, pil, kryss och ikon, kolumn 1… med cellerna; en hopfälld grens noder räknas inte, och en utfällning växer kolumnen — och ett dubbelklick på en avdelare skriver den anpassade bredden som en fast bredd (`widget-list-view`).

### API
- `struct TreeNode { TrText text; std::vector<TreeNode> children; ImageSource img{}; bool expanded = false; bool allowDropInto = true; bool checked = false; std::optional<gfx::Color> color{}; std::vector<std::string> cells{}; }`
- `TreePath addNode(const TreePath& parent, TrText text, ImageSource img = {}, std::optional<gfx::Color> color = std::nullopt, std::vector<std::string> cells = {})` — svarar med den nya stigen.
- `void setNode(const TreePath& path, TrText text, ImageSource img = {}, std::optional<gfx::Color> color = std::nullopt, std::vector<std::string> cells = {})` · `void setNodes(std::vector<TreeNode> nodes)` · `void removeNode(const TreePath& path)`
- Kolumnerna (ärvda från `ColumnPanel`): `addColumn(TrText, float width = 0, Align = Left, std::optional<Align> headerAlign = {})` (`width`: `Column::autoWidth` = efter innehållet) · `setColumn(int, …)` · `setColumns(std::vector<Column>)` · `setColumnWidth(int, float)` · `setSortColumn(int)` (−1 = ingen) · `setSortAscending(bool)` · `setSortable(bool)` · `setFillColumn(int)` · `setHeaderLines(int)` · `ensureColumnVisible(int)`
- `void setCell(const TreePath& path, int column, std::string text)` (kolumn ≥ 1; kolumnen behöver inte finnas än) · `const std::string& cell(const TreePath& path, int column) const`
- `bool moveNode(const TreePath& from, const TreePath& toParent, int index)` — index räknat FÖRE flytten.
- `bool moveNodes(const std::vector<TreePath>& from, const TreePath& toParent, int index)` — flera på en gång, som släppet: FÖRE-flytten-index, en nod inuti en annan i listan följer med sin förfader, insättning i TRÄDORDNING, atomiskt (ett fel ⇒ `false` + logg och ingenting ändrat).
- `void setExpanded(const TreePath& path, bool on)` · `void setSelectedPath(const TreePath& path)` / `TreePath selectedPath() const` · `std::string selectedText() const`
- `void setMultiSelect(bool on)` · `void setSelected(const TreePath&, bool)` / `bool isSelected(const TreePath&) const` · `std::vector<TreePath> selectedPaths() const` · `TreePath currentPath() const` · `void selectAll()` · `void clearSelection()`
- `void setShowChecks(bool on)` · `void setChecked(const TreePath&, bool)` / `bool isChecked(const TreePath&) const` · `std::vector<TreePath> checkedPaths() const` (trädordning, också i stängda grenar)
- `void setNodeColor(const TreePath&, std::optional<gfx::Color>)` / `std::optional<gfx::Color> nodeColor(const TreePath&) const` · `const ImageSource& nodeImg(const TreePath&) const`
- `int nodeCount(const TreePath& parent = {}) const` · `bool hasNode(const TreePath& path) const` · `const std::string& nodeText(const TreePath& path) const` · `void ensureVisible(const TreePath& path)`
- `void setAllowDropInto(const TreePath& path, bool on)` · `TreePath dropParent() const` · `int dropIndex() const` (luckan, löst vid varje fråga; `{}`/−1 utanför ett drag) · `std::vector<TreePath> draggedPaths() const` (de dragna i trädordning, från `onDragStart` till och med `onDrop`; tom i `onDragEnd`)
- `Handler<ChangeArgs> onChange` · `Handler<ChangeArgs> onCommit` (samma val, eller samma kryss, direkt efter) · `onCheck` · `onExpand` · `onCollapse` (läs `changedPath()`) · `Handler<ActivateArgs> onActivate` (läs `currentPath()`; Enter tas bara när den är satt och en nod är aktuell, annars når den fönstrets standardknapp)

### Exempel
```cpp
class Projekt : public glaze::AppWindow {
public:
    Projekt() : glaze::AppWindow("Projekt", 360, 400)
    {
        trad.dock = glaze::Dock::Fill;    // ingen autostorlek
        trad.setNodes({{"Dokument", {{"Brev.txt"}, {"Rapport.pdf"}}, {}, true},
                       {"Bilder"}});
        const glaze::TreePath musik = trad.addNode({}, "Musik");   // svaret = stigen
        trad.addNode(musik, "Spår 1.flac");
        trad.setSelectedPath({0, 1});     // Dokument → Rapport.pdf
        trad.setAllowDrag(true);          // flytta noder med musen
        trad.setAllowDrop(true);
        trad.setAllowDropInto({0, 0}, false);   // en fil får inga barn
        trad.setMultiSelect(true);        // Ctrl/Shift väljer flera
        trad.setShowChecks(true);         // en ruta per nod
        trad.setNodeColor({0, 0}, gfx::Color{200, 40, 40});   // appens status
        trad.onChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            auto* t = static_cast<glaze::TreeView*>(s);
            setTitle(std::to_string(t->selectedPaths().size()) + " valda");
        };
        trad.onCheck = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            auto* t = static_cast<glaze::TreeView*>(s);   // VILKEN: changedPath()
            setTitle(t->nodeText(t->changedPath()) +
                     (t->isChecked(t->changedPath()) ? " kryssad" : " avkryssad"));
        };
        trad.onActivate = [this](glaze::Widget* s, glaze::ActivateArgs&) {
            const glaze::TreePath p = static_cast<glaze::TreeView*>(s)->currentPath();
            setTitle("Öppnar nod på djup " + std::to_string(p.size()));
        };
        // APPENS GODKÄNNANDE: regeln per plats i onDragOver (fyras också vid
        // inträdet) — ingen fil överst.
        trad.onDragOver = [](glaze::Widget* s, glaze::DragArgs& e) {
            auto* t = static_cast<glaze::TreeView*>(s);
            if (!t->dropParent().empty() || t->dropIndex() < 0)
                return;                               // inte överst
            for (const glaze::TreePath& p : t->draggedPaths())
                if (!t->allowDropInto(p))             // en fil
                    e.effect = glaze::DragEffect::None;   // förbudsmärket
        };
        // Släppet frågas FÖRE trädets flytt: i Bilder lägger appen noderna
        // först SJÄLV, annars flyttar trädet till luckan efter handlern.
        trad.onDrop = [](glaze::Widget* s, glaze::DragArgs& e) {
            auto* t = static_cast<glaze::TreeView*>(s);   // det OFLYTTADE trädet
            if (t->dropParent() == glaze::TreePath{1} &&
                t->moveNodes(t->draggedPaths(), t->dropParent(), 0))
                e.handled = true;                     // ingen andra flytt
        };

        root().adopt(trad);
    }

private:
    glaze::TreeView trad;
};
```

### .glazeui
```cpp
trad.setNodes({{"Dokument", {{.text = "Brev.txt", .checked = true}, {"Rapport.pdf", {}, {}, false, false, false, gfx::Color{200, 40, 40}}}, {}, true}, {"Bilder"}});
trad.addNode({}, "Musik", {});
trad.addNode({2}, "Spår 1.flac", glaze::img("ikoner", "ljud"));
trad.setNode({1}, "Foton");
trad.moveNode({0, 1}, {1}, 0);
trad.setExpanded({2}, true);
trad.setSelectedPath({0, 0});
trad.setAllowDrag(true);
trad.setAllowDrop(true);
trad.setAllowDropInto({0, 0}, false);
trad.setMultiSelect(true);
trad.setShowChecks(true);
trad.setChecked({1, 0}, true);
trad.setNodeColor({0, 0}, gfx::Color{0, 140, 0});
trad.setSelected({2, 0}, true);
trad.moveNodes({{0, 1}, {2}}, {1}, 0);
trad.onExpand = [this](glaze::Widget* s, glaze::ChangeArgs& e) { tradExpand(s, e); };
trad.onCheck = [this] { tradKryss(); };
trad.onDragOver = [this](glaze::Widget* s, glaze::DragArgs& e) { tradOver(s, e); };
trad.onDrop = [this](glaze::Widget* s, glaze::DragArgs& e) { tradSlapp(s, e); };
prof.setColumns({{"Funktion", 180}, {"ms", 60, glaze::Align::Right}, {"Anrop", 60, glaze::Align::Right}});
prof.setNodes({{.text = "main", .children = {{.text = "parse", .cells = {"12", "3"}}}, .expanded = true, .cells = {"52", "1"}}});
prof.addNode({0}, "draw", {}, std::nullopt, {"40", "1"});
prof.setCell({0, 1}, 2, "2");
prof.setSortColumn(1);
prof.setSortAscending(false);
```
`setMultiSelect`/`setShowChecks` och sorteringen (`setSortColumn`/`setSortAscending`/`setSortable`, `setHeaderLines`, `setFillColumn`) är setter-fält. Aggregatet tar sina åtta fält positionellt ELLER designerat, och färgen `gfx::Color{…}`, `std::nullopt` eller `{}`; cellerna är en strängliste-literal. `moveNodes` tar en STIGLISTA — `{{0, 1}, {2}}`; `{0}` som lista kompilerar inte och är opak i previewn.

### Fallgropar
- `setNode` ersätter text, bild, färg OCH celler (utelämnad = ingen bild, temats färg, inga celler); barnen, utfällningen, valet och krysset står kvar.
- Med kolumner: `setCell(stig, 0, …)` är en no-op — kolumn 0 är nodens text (`setNode`). Ett SLÄPP i ett sorterat träd hamnar i luckan i DATA men visas på sin sorterade plats. Ingen sorteringsnyckel per cell (tabellens `setSortKey`): skriv talen så att de jämförs naturligt.
- Drag och släpp är opt-in (`setAllowDrag` + `setAllowDrop`): egna noder flyttas inbyggt — från en VALD nod HELA valet, beskuret till sina översta noder — och en främmande last tar appen emot i `onDrop` med `dropParent()`/`dropIndex()` (se `drag-drop`; lasten är stigarna åtskilda med `;`). Ett drag börjar aldrig på pilen eller i rutan.
- Vetot hör hemma i `onDragOver` och inte i `onDragEnter`: trädet föreslår om platsen vid VARJE rörelse. `onDrop` som ändrar trädet UTAN `handled` får det inbyggda att flytta till samma lucka (hållen vid id:n) — eller ingenstans (`None`) om luckan eller en dragen nod är borta; ett `Copy` i `onDrop` flyttar inget, kopian är appens.
- `setSelectedPath({})` släpper också den AKTUELLA noden (listvyn behåller sin) — Enter bubblar då.
- `selectAll()`/Ctrl+A väljer de SYNLIGA noderna, inte de i en stängd gren; en ihopfällning som gömmer valda noder väljer föräldern i stället.
- Ingen autostorlek, ingen vågrät rullning utan kolumner, ingen kaskad mellan kryssen och inget "delvis"-läge, ingen gummibandsmarkering, inga linjer, ingen typeahead; dragbilden visar inte antalet.
- SNABBMENYN mot noden: ett högertryck väljer noden som Utforskaren (utanför valet ensam, i valet står valet), och `contextPath()` är noden i `onContextMenu` och i menyns handlers — `{}` på tom yta, när trädet är inaktiverat och när noden försvunnit. `pathAt(e.local.x, e.local.y)` är träffvägen.
- Ett drag nära trädets kant AUTOSKROLLAR (ramverkets, `Widget::dragScroll`); det finns ingen auto-utfällning.

<!-- END:widget-tree-view -->

<!-- BEGIN:widget-property-grid | propertygrid egenskaper egenskapslista properties propertyeditor inspector wxpropertygrid namn-värde key-value rows changedrow namewidth oncommit commit redigering -->
## PropertyGrid — egenskapslistan (`widget-property-grid`)

**Alias:** propertygrid, egenskaper, egenskapslista, properties, propertyeditor, inspector, wxpropertygrid, namn-värde, key-value, rows, changedrow, namewidth, oncommit, commit, redigering
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/property_grid.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

Två kolumner à la Visual Studio: namnet (alltid läsbart) och värdet (fritext eller vallista per rad). Raderna är DATA (`Row`), och EN textruta och EN vallista flyttas till cellen som redigeras. Redigeringen COMMITTAR, som `DataGrid`s: ett klick på ett värde öppnar editorn med texten markerad; Enter, Tab, ett klick på en annan rad eller att fokus går någon annanstans sparar — radens `value` skrivs och `onChange` + `onCommit` fyras en gång var, bara om värdet ändrades — och Escape ångrar. En vallista sparar vid valet, och Enter (med listans val om den är öppen) och Escape avslutar den. Registreringsnamn `propertygrid`.

### API
- `struct Row { std::string name; std::string value; std::vector<std::string> choices; bool readOnly = false; std::string tag; }` — tom `choices` = fritext; `tag` är din opaka nyckel.
- `void setRows(std::vector<Row> rows)` · `const std::vector<Row>& rows() const`
- `void addRow(std::string name, std::string value = "", bool readOnly = false, std::string tag = "")`
- `void setRow(int index, std::string name, std::string value = "", bool readOnly = false, std::string tag = "")` — ERSÄTTER hela raden.
- `void setNameWidth(float w)` / `float nameWidth() const` — logiska px, 0 = stilens `gridNameW`.
- `int changedRow() const` — läs i `onChange`/`onCommit` · `int activeRow() const` · `Handler<ChangeArgs> onChange` · `Handler<ChangeArgs> onCommit` (samma sparade värde, direkt efter `onChange`)

### Exempel
```cpp
class Egenskaper : public glaze::AppWindow {
public:
    Egenskaper() : glaze::AppWindow("Egenskaper", 360, 260)
    {
        grid.dock = glaze::Dock::Fill;    // ingen autostorlek
        grid.setNameWidth(120);
        grid.setRows({{"Bredd", "120"},
                      {"Justering", "Vänster", {"Vänster", "Mitten", "Höger"}},
                      {"Id", "A-17", {}, true}});   // choices, readOnly
        grid.addRow("Höjd", "80");
        grid.addRow("Senast ändrad", "", true);   // rad 4, skrivskyddad
        grid.onChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            auto*       g   = static_cast<glaze::PropertyGrid*>(s);
            const auto& rad = g->rows()[static_cast<std::size_t>(g->changedRow())];
            setTitle(rad.name + " = " + rad.value);
        };
        // Klar: räkna om och skriv tillbaka. Editorn är redan stängd, så
        // `setRow` och `setRows` här river ingen redigering.
        grid.onCommit = [this] {
            const auto& rad = grid.rows()[static_cast<std::size_t>(grid.changedRow())];
            grid.setRow(4, "Senast ändrad", rad.name, true);
        };

        root().adopt(grid);
    }

private:
    glaze::PropertyGrid grid;
};
```

### .glazeui
```cpp
grid.setRows({{"Bredd", "120"}, {"Justering", "Vänster", {"Vänster", "Mitten", "Höger"}}, {"Id", "A-17", {}, true}});
grid.addRow("Höjd", "80");
grid.setRow(0, "Bredd", "160");
grid.setNameWidth(120);
grid.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { gridChanged(s, e); };
grid.onCommit = [this] { gridKlar(); };
```

### Fallgropar
- `setRows`, och `setRow` på raden som redigeras, AVBRYTER redigeringen utan att spara. Det är ofarligt i `onChange`/`onCommit` — där är editorn redan stängd — men en timer eller ett nätsvar som bygger om raderna mitt i en redigering kastar det användaren skrivit. `addRow` avbryter inte.
- Fokusförlustens `onChange`/`onCommit` postas till pumpens svans och slopas om raderna bytts (`setRows`) eller just den raden ersatts (`setRow`) innan — de beskrev en rad som inte längre finns i den formen. Enter och klicket på en annan rad fyrar direkt, så en handler som bygger om raderna är klar innan nästa rad öppnas.
- `changedRow()` i `onCommit` är den sparade radens index också om `onChange` byggt om raderna — samma rad om ombyggnaden behöll ordningen, annars en annan eller ingen; läs `rows()` med en gränskontroll i en sådan handler.
- Gridden är inget tabbstopp: efter Enter eller Escape står fokus kvar i den dolda editorn, och nästa Tab börjar från fönstrets första stopp. Ett klick på en värdecell tar upp redigeringen igen.
- I en dialog avslutar Enter och Escape redigeringen först — både fritexten och vallistan tar dem — och först NÄSTA Enter eller Escape når dialogens standard- respektive avbrytknapp (`app-window`).
- Ett klick på en `ToolButton`, ett menyval och en accelerator tar inte fokus — editorn står öppen och `rows()` bär det förra värdet tills handlern anropar `commitEdits()` (eller `commitEdit()` här), som sparar och stänger editorn som Enter (`events`).
- `setRow` ersätter HELA raden — en utelämnad `tag` eller `choices` blir tom.
- `choices` går bara att uttrycka i `setRows`-literalen; det finns ingen egen metod.
- Ingen autostorlek (0 × 0 utan mått). `rows()` är en LÄSVÄG: raderna ändras bara med `setRows`, `addRow` och `setRow`, så gridden ser varje ändring (omritningen, den öppna editorn och en köad sparning av en rad som byts).

<!-- END:widget-property-grid -->

<!-- BEGIN:widget-color-picker | colorpicker färgväljare färg color colour wxcolourpickerctrl qcolordialog hex rgb rgba alpha alfa palett palette swatch oncommit -->
## ColorPicker — färgväljaren (`widget-color-picker`)

**Alias:** colorpicker, färgväljare, färg, color, colour, wxcolourpickerctrl, qcolordialog, hex, rgb, rgba, alpha, alfa, palett, palette, swatch, oncommit
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/color_picker.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

Ett fält med en färgruta; klick, Space, Ned eller F4 öppnar en popup med palett, reglage för röd/grön/blå och ett hexfält (Enter på den stängda kontrollen bubblar till fönstrets standardknapp, `app-window`). Valet gäller DIREKT: varje reglageläge är färgen och fyrar `onChange` (`onCommit` när reglaget släpps och vid varje tangentsteg); en skriven hexfärg gäller när man trycker Enter, går vidare med Tab eller stänger popupen; ett klick i paletten väljer och stänger. Med `setAllowAlpha(true)` får popupen ett fjärde reglage, A, och hexfältet åtta siffror. Popupen är vår, aldrig operativsystemets; en MODAL färgdialog är `glaze::ColorDialog` (`color-dialog`). Registreringsnamn `colorpicker`.

### API
- `void setColor(gfx::Color c)` · `void setColor(std::string_view text)` — `#RRGGBB` eller `#RRGGBBAA` (`#` valfritt); ogiltig text loggas och lämnar färgen orörd.
- `gfx::Color color() const` — opak utan alfan · `std::string colorText() const` — `#` och sex versala hexsiffror, åtta med alfan.
- `void setAllowAlpha(bool on)` / `bool allowAlpha() const` — förval av.
- `Handler<ChangeArgs> onChange` · `Handler<ChangeArgs> onCommit` (tabellen i `events`) · `void open()` · `void close()` · `bool isOpen() const`

### Exempel
```cpp
class Farg : public glaze::AppWindow {
public:
    Farg() : glaze::AppWindow("Färg", 360, 120)
    {
        farg.setColor("#3366FF");         // eller setColor(gfx::Color{…})
        farg.name = "farg";
        farg.x = 12;
        farg.y = 12;
        farg.onChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            auto* cp = static_cast<glaze::ColorPicker*>(s);
            prov.setColor(cp->color());   // gfx::Color, alfa 255 utan setAllowAlpha
            prov.setText(cp->colorText());   // "#3366FF"
        };

        prov.setText("#3366FF");
        prov.anchorLeft = {"farg", 12};
        prov.y = 12;

        root().adopt(farg);
        root().adopt(prov);
    }

private:
    glaze::ColorPicker farg;
    glaze::Label       prov;
};
```

### .glazeui
```cpp
farg.setColor("#3366FF");
farg.setColor(gfx::Color{76, 175, 80, 255});
skugga.setColor("#00000080");
skugga.setAllowAlpha(true);
farg.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { fargChanged(s, e); };
farg.onCommit = [this] { fargKlar(); };
```
Hextexten är designerns form, `gfx::Color{…}` kodens — previewn utför båda.

### Fallgropar
- Utan `setAllowAlpha(true)` läses färgen OPAK — också en satt `#RRGGBBAA`: alfan lagras och syns när flaggan slås på, så ordningen spelar ingen roll. Paletten byter kulören och behåller alfan.
- Escape och ett klick utanför ÅTERSTÄLLER inte — reglagens värde har redan gått live, och en skriven hexfärg som går att läsa committas när popupen stängs, också med Escape.
- Ingen kortform (`#36F`), ingen egen palett, inga senast använda, ingen pipett.

<!-- END:widget-color-picker -->

<!-- BEGIN:widget-date-picker | datepicker datumväljare datum date kalender calendar datetimepicker wxdatepickerctrl qdateedit year_month_day iso mindate maxdate oncommit selectall time tid klockslag showtime datetime -->
## DatePicker — datumväljaren (`widget-date-picker`)

**Alias:** datepicker, datumväljare, datum, date, kalender, calendar, datetimepicker, wxdatepickerctrl, qdateedit, year_month_day, iso, mindate, maxdate, oncommit, selectall, time, tid, klockslag, showtime, datetime
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/date_picker.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

Ett fält för ett datum i ISO-formen `ÅÅÅÅ-MM-DD` och en kalenderknapp som öppnar ett månadsrutnät (titeln går upp till månader och år). Värdet är `std::chrono::year_month_day`, klampat till gränserna när det läses, och det finns ALLTID ett datum — utan `setDate` är det dagens. Det man skriver gäller vid Enter, när fokus lämnar datumväljaren eller när kalendern öppnas; ett val i kalendern gäller direkt. Varje ändring fyrar `onChange` och `onCommit` tillsammans. Med `setShowTime(true)` bär fältet också ett KLOCKSLAG, `ÅÅÅÅ-MM-DD HH:MM`. ETT tabbstopp. Registreringsnamn `datepicker`.

### API
- `void setDate(int year, int month, int day)` · `void setDate(std::chrono::year_month_day d)`
- `void setMinDate(int year, int month, int day)` · `void setMinDate(std::chrono::year_month_day d)` · `void setMaxDate(int year, int month, int day)` · `void setMaxDate(std::chrono::year_month_day d)`
- `std::chrono::year_month_day date() const` — det EFFEKTIVA (klampat) · `std::chrono::year_month_day minDate() const` · `maxDate() const`
- `std::string dateText() const` — det effektiva datumet i ISO-form · `const std::string& text() const` (det fältet visar; under redigering användarens text).
- `Handler<ChangeArgs> onChange` · `Handler<ChangeArgs> onCommit` — när användaren ändrat det effektiva datumet, `onChange` först · `void open()` · `void close()` · `bool isOpen() const`
- `void selectAll()` · `void setSelection(int begin, int end)` — markeringen i fältet.
- KLOCKSLAGET: `void setShowTime(bool on)` / `bool showsTime() const` · `void setTime(int hour, int minute)` · `void setTime(std::chrono::minutes sinceMidnight)` · `std::chrono::minutes time() const` · `std::string timeText() const` (`HH:MM`). Fältet tar `T` eller blanksteg mellan datum och tid, och en text med bara datumet behåller tiden.

### Exempel
```cpp
class Leverans : public glaze::AppWindow {
public:
    Leverans() : glaze::AppWindow("Leverans", 360, 120)
    {
        datum.setMinDate(2026, 1, 1);
        datum.setMaxDate(2026, 12, 31);
        datum.setDate(2026, 10, 8);       // ordningen spelar ingen roll
        datum.x = 12;
        datum.y = 12;
        datum.onChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            auto* dp = static_cast<glaze::DatePicker*>(s);
            const std::chrono::year_month_day d = dp->date();   // klampat
            setTitle("Leverans " + dp->dateText() + " (" +
                     std::to_string(static_cast<unsigned>(d.month())) + ")");
        };

        root().adopt(datum);
    }

private:
    glaze::DatePicker datum;
};
```

### .glazeui
```cpp
datum.setMinDate(2026, 1, 1);
datum.setMaxDate(2026, 12, 31);
datum.setDate(2026, 10, 8);
mote.setShowTime(true);
mote.setDate(2026, 10, 13);
mote.setTime(14, 30);
datum.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { datumChanged(s, e); };
```
Tidpunkten byggs av appen ur paret: `std::chrono::local_days{mote.date()} + mote.time()` (eller `sys_days` för UTC).

### Fallgropar
- Ett datum som inte finns (`setDate(2026, 2, 30)`) loggas och ändrar ingenting.
- Medan man skriver är `date()` det förra datumet; en text som inte går att läsa (halvskriven, `2026-02-30`) skrivs tillbaka vid commit utan events. Ett klick på en `ToolButton`, ett menyval och en accelerator committar inte av sig självt — handlern anropar `commitEdits()` först (`events`).
- Klockslaget är hela minuter sedan midnatt UTAN klocka — appen väljer lokal tid eller UTC. Utan `setTime` är tiden NU, som datumet är i dag. Gränserna gäller datumet, inte tiden. Ett klockslag som inte finns (`25:00`) lämnar värdet orört.
- Inget "inget datum", inga veckonummer, inga sekunder, inget lokalt datumformat i fältet (alltid ISO). Veckan börjar på måndag.
- `setDate` stänger en öppen popup.
- Enter i fältet committar och går SEDAN vidare till fönstrets standardknapp (`app-window`); med kalendern öppen är Enter kalenderns och Escape stänger bara den.

<!-- END:widget-date-picker -->

<!-- BEGIN:widget-file-picker | filepicker filväljarfält fil file path sökväg filepath browse bläddra openfile savefile folderpicker mappväljare wxfilepickerctrl wxdirpickerctrl filter oncommit selectall -->
## FilePicker — filväljarfältet (`widget-file-picker`)

**Alias:** filepicker, filväljarfält, fil, file, path, sökväg, filepath, browse, bläddra, openfile, savefile, folderpicker, mappväljare, wxfilepickerctrl, wxdirpickerctrl, filter, oncommit, selectall
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [header](Glaze/widgets/include/glaze/widgets/file_picker.h), [CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

Ett textfält för en sökväg och en "…"-knapp som öppnar ramverkets filväljardialog (se `file-dialog`) i lägena öppna, spara eller mapp. Man kan skriva sökvägen eller välja i dialogen, som öppnar där sökvägen pekar och minns valt filter. Två riktiga barn = två tabbstopp. Registreringsnamn `filepicker`.

### API
- `void setPath(std::string utf8)` / `const std::string& path() const` — värdet heter `path`, inte `text`.
- `void setMode(FileDialogMode m)` / `FileDialogMode mode() const` — `glaze::FileDialogMode::Open/Save/Folder`.
- `void addFilter(TrText name, std::string patterns)` · `void setFilter(int index, TrText name, std::string patterns)` · `void setFilters(std::vector<FileFilter> filters)` — mönster skiljs med `;`.
- `bool browse()` — öppnar dialogen (modal) från kod; kräver ett öppet fönster.
- `Handler<ChangeArgs> onChange` — LIVE när man skriver, och en gång när dialogen gav en ny sökväg.
- `Handler<ChangeArgs> onCommit` — Enter eller när fokus lämnar fältet (om texten ändrats; postat vid fokusförlust), och när dialogen gav en ny sökväg (direkt efter `onChange`).
- `void selectAll()` · `void setSelection(int begin, int end)` — markeringen i fältet.

### Exempel
```cpp
class Import : public glaze::AppWindow {
public:
    Import() : glaze::AppWindow("Import", 480, 120)
    {
        bild.setMode(glaze::FileDialogMode::Open);   // Save · Folder
        bild.addFilter("Bilder", "*.png;*.jpg");
        bild.addFilter(glaze::tr(glaze::str::FileAllFiles), "*.*");
        bild.setPath("C:/Bilder/karta.png");
        bild.anchors = {true, true, true, false};
        bild.x = 12;
        bild.y = 12;
        bild.right = 12;
        bild.onChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
            setTitle("Import — " + static_cast<glaze::FilePicker*>(s)->path());
        };

        root().adopt(bild);
    }

private:
    glaze::FilePicker bild;
};
```

### .glazeui
```cpp
bild.setMode(glaze::FileDialogMode::Save);
bild.setPath("C:/Bilder/karta.png");
bild.addFilter("Bilder", "*.png;*.jpg");
bild.addFilter(glaze::tr(43), "*.*");
bild.setFilters({{"Text", "*.txt"}, {glaze::tr(43), "*.*"}});
bild.setFilter(0, "Textfiler", "*.txt;*.md");
bild.onChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { bildChanged(s, e); };
```
`glaze::tr(43)` är ramverkets "Alla filer" (`glaze::str::FileAllFiles` i kod).

### Fallgropar
- Filtren används inte i mappläget.
- `setPath` fyrar inget; sökvägen är UTF-8 rakt igenom.
- Fältet och "…"-knappen är två egna tabbstopp: en flytt mellan dem fyrar inte `onFocus`/`onFocusLost` på filväljarfältet (fokus inom), men fältets commit kommer — fältet tappade fokus.
- `setDialogTitle("Välj CSV-fil")` (eller `glaze::tr(N)`) är dialogens titel och knappens tooltip; tom = lägets titel.

<!-- END:widget-file-picker -->

<!-- BEGIN:menus | menus meny menyer menubar menyrad menu menuitem snabbmeny context-menu högerklick popupat shortcut genväg accelerator kortkommando addmenu sethandler menupath -->
## Menyer — menyraden, `Menu` och snabbmenyn (`menus`)

**Alias:** menus, meny, menyer, menubar, menyrad, menu, menuitem, snabbmeny, context-menu, högerklick, popupat, shortcut, genväg, accelerator, kortkommando, addmenu, sethandler, menupath
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [menu.h](Glaze/widgets/include/glaze/menu.h), [menu_bar.h](Glaze/widgets/include/glaze/widgets/menu_bar.h), [apps/demo/src/start_window.cpp](apps/demo/src/start_window.cpp)

En meny är REN DATA: `glaze::Menu` med `glaze::MenuItem`-poster (text, genväg, kryss/radio, separator, undermeny och en `onClick` PER POST). Samma `Menu` visas som menyrad (`menuBar`, ett publikt fält på `AppWindow`, dolt tills `menuBar.show()`), som snabbmeny (`popupAt` ur `onContextMenu`) och som undermeny. Genvägarna fungerar även när menyn är stängd.

### API
- `Menu& addMenu(TrText title);` (MenuBar — stabil referens) · `Menu* menuAt(int i);` · `void clearMenus();`
- `MenuItem& addItem(TrText text);` · `MenuItem& addCheck(TrText text, bool checked = false);` · `MenuItem& addRadio(TrText text, bool checked = false);` · `MenuItem& addSeparator();` · `MenuItem& addSubmenu(TrText text);` (fyll postens `submenu`) — referensen gäller till NÄSTA add.
- `struct MenuItem { TrText text; ImageSource icon; Shortcut shortcut; bool enabled, checkable, checked, radio, separator; std::vector<MenuItem> submenu; Handler<ClickArgs> onClick; }` · `struct Shortcut { gfx::Key key; std::uint32_t mods; }` — ikonen är en asset eller en `glaze::Bitmap`.
- `void popupAt(Widget& host, float x, float y);` · `void popupAtRect(Widget& host, const gfx::Rectf& anchor);` — icke-blockerande; x/y i värdfönstrets fysiska px (`ContextMenuArgs`).
- Platt form på `MenuBar` (för `.glazeui`): `void addItem(const MenuPath& path, TrText text, const std::string& shortcut = "");` · `void addCheck(const MenuPath& path, TrText text, bool checked = false);` · `addRadio` · `void addSeparator(const MenuPath& path);` · `void addSubmenu(const MenuPath& path, TrText text);` · `template <class F> void setHandler(const MenuPath& path, F&& f)` · `MenuItem* itemAtPath(const MenuPath& path);`
- POSTENS TILLSTÅND, platt (samma tre på `MenuBar`, `AppIcon`, `TrayIcon` och `ToolButton`): `void setItemIcon(const MenuPath& path, ImageSource icon);` · `void setItemEnabled(const MenuPath& path, bool on);` · `void setItemChecked(const MenuPath& path, bool on);` — EXAKT samma tillstånd som `itemAtPath(stig)->icon/enabled/checked` (krysset syns bara på en kryss- eller radiopost); okänd stig ⇒ logg + no-op.
- `MenuPath` — ett segment (`"Arkiv"`, `glaze::tr(id)`) eller flera (`{"Visa", "Läge"}`); segmenten matchas token mot token.

### Exempel
```cpp
class MenuWindow : public glaze::AppWindow {
public:
    MenuWindow() : glaze::AppWindow("Menyer", 640, 400)
    {
        glaze::AppWindow::onLoad = [this] { buildMenus(); };
    }

private:
    void buildMenus()
    {
        glaze::Menu& file = menuBar.addMenu("Arkiv");      // stabil referens
        glaze::MenuItem& save = file.addItem("Spara");     // gäller till nästa add
        save.shortcut = {gfx::Key::S, gfx::kModCtrl};      // visas som Ctrl+S / ⌘S
        save.onClick  = [this] { saved_ = true; };
        file.addSeparator();
        file.addItem("Avsluta").onClick = [this] { close(); };

        glaze::Menu& view = menuBar.addMenu("Visa");
        view.addCheck("Rutnät", true).onClick = [this] { grid_ = !grid_; };
        menuBar.show();                                    // dold tills show()

        context_.addItem("Rensa").onClick = [this] { saved_ = false; };
        root().onContextMenu = [this](glaze::Widget* s, glaze::ContextMenuArgs& e) {
            context_.popupAt(*s, e.x, e.y);                // icke-blockerande
        };
    }

    glaze::Menu context_;   // MEDLEM — en lokal Menu dör och stänger menyn
    bool saved_ = false;
    bool grid_  = true;
};

void wireFlatMenu(glaze::AppWindow& win)   // samma tillstånd som .glazeui-formen
{
    win.menuBar.addMenu("Arkiv");
    win.menuBar.addItem("Arkiv", "Spara", "Ctrl+S");
    win.menuBar.setHandler({"Arkiv", "Spara"}, [] { gfx::log("spara"); });
    win.menuBar.setItemEnabled({"Arkiv", "Spara"}, false);   // = itemAtPath(…)->enabled
}
```

### .glazeui
STRUKTUREN som platta satser (previewn visar menyraden), HANDLERNA i kod med `setHandler` som i `wireFlatMenu`, typiskt i `onLoad`:
```cpp
    menuBar.show();
    menuBar.addMenu("Arkiv");
    menuBar.addItem("Arkiv", "Spara", "Ctrl+S");
    menuBar.addSeparator("Arkiv");
    menuBar.addMenu("Visa");
    menuBar.addCheck("Visa", "Rutnät", true);
    menuBar.addSubmenu("Visa", "Läge");
    menuBar.addRadio({"Visa", "Läge"}, "Ljust", true);
    menuBar.setItemIcon({"Arkiv", "Spara"}, glaze::img("ikoner", "spara"));
    menuBar.setItemEnabled({"Arkiv", "Spara"}, false);
    menuBar.setItemChecked({"Visa", "Rutnät"}, false);
```

### Fallgropar
- Snabbmenyn MÅSTE vara en medlem: `popupAt` returnerar direkt, och en `Menu` som förstörs stänger sina öppna presentationer.
- Mutera FÖRE `popupAt` — en öppen meny visar en snapshot och ser inga senare ändringar.
- En kryss- eller radiopost växlar sitt eget `checked` FÖRE `onClick`; synka appens tillstånd därifrån. En radiogrupp är de sammanhängande radioposterna mellan separatorer.
- Stigarna är segment, aldrig `"Visa/Läge"` (ett `/` i ett segment avvisas med en loggrad). Blanda inte literal och `glaze::tr(id)` i samma matchning — id matchar bara id.
- Genvägen är level-0-symbolen: "Ctrl+Plus" är `{gfx::Key::Plus, gfx::kModCtrl}` på svensk layout men `Equals` + Shift på amerikansk; numpadens `KP_End` är en egen tangent (`gfx`). `.glazeui` skriver genvägen med engelska kanoniska namn ("Ctrl+S"); visningen (Ctrl+S, ⌘S) härleds.
- Acceleratorerna frågas efter den fokuserade widgeten: Ctrl+C i ett `TextInput` går till fältet, inte till menyn — och Ctrl+Z/Ctrl+Y likaså, ALLTID, också när fältet inte har något att ångra (på macOS är det Cmd-varianterna som är fältets, och Ctrl+C når menyn). En Redigera-menys "Ångra" anropar därför det fokuserade fältets `undo()` i sin handler (`widget-text-input`).
- Efter acceleratorerna, sist före Tab, kommer fönstrets standard- och avbrytknapp (`app-window`): en menypost med genvägen "Enter" eller "Esc" vinner över dem — men bara medan menyraden syns, för en dold menyrads acceleratorer frågas inte.
- Undvik Ctrl+Alt+bokstav som genväg: AltGr ÄR Ctrl+Alt på Windows. Textfälten tackar nej till Ctrl+Alt (så AltGr+Z skriver ż), men tangenten fortsätter då till acceleratorerna — en Ctrl+Alt+Z-genväg körs av en polsk användare som skriver ż. Microsofts riktlinje säger detsamma.
- Programmenyn på programikonen har samma vokabulär med den TOMMA stigen `{}` som roten: `appIcon.addItem({}, "Nytt")`, `appIcon.setHandler("Nytt", …)`. På menyraden är en tom stig fortfarande ett fel (`app-icon`). Samma genväg i menyraden och programmenyn: menyraden vinner.
- Systemfältets meny (`glaze::TrayIcon`) har programmenyns vokabulär med `{}` som roten men inget genvägsargument — inget fönster tar dess acceleratorer. Den visas i ett internt värdfönster, så en posts `sender` är värdfönstrets rot och inte ikonen: fånga ikonen i lambdan (`tray-icon`).
- Verktygsknappens rullgardin (`glaze::ToolButton`) har samma vokabulär som systemfältets — `{}` som roten, inget genvägsargument — och en posts `sender` är knappen (`widget-tool-bar`).
- En snabbmeny från ett SLÄPP (en rityta som tar högertrycket själv): `if (auto p = yta.toWindow(e.local)) meny.popupAt(yta, p->x, p->y);` — `toWindow` ger fönstrets fysiska px som `popupAt` tar.
- En genväg utan modifierare (`"M"`) körs inte medan ett textfält har fokus och tangenten skriver — text vinner över genvägen (`app-window`).
- En snabbmeny MOT EN NOD eller RAD: högertrycket har redan valt den, så läs den ur sender — `trad.onContextMenu = [this](glaze::Widget*, glaze::ContextMenuArgs& e) { if (!trad.contextPath().empty()) meny.popupAt(trad, e.x, e.y); };` (`ListBox`/`ListView`: `contextRow()`).
<!-- END:menus -->

<!-- BEGIN:app-icon | app-icon appicon programikon ikon icon programmeny program-menu office-knapp setlarge setsmallicon setnoicon noicon fönsterikon window-icon aktivitetsfält taskbar dock alt-tab setappid app_id glaze_app_icon ico icns desktop -->
## Programikonen och programmenyn (`app-icon`)

**Alias:** app-icon, appicon, programikon, ikon, icon, programmeny, program-menu, office-knapp, setlarge, setsmallicon, setnoicon, noicon, fönsterikon, window-icon, aktivitetsfält, taskbar, dock, alt-tab, setappid, app_id, glaze_app_icon, ico, icns, desktop
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` (+ `glaze_app_icon(<target>)` för programfilen) · **Källa:** [app_icon.h](Glaze/widgets/include/glaze/widgets/app_icon.h), [app_window.h](Glaze/widgets/include/glaze/app_window.h), [cmake/GlazeAppIcon.cmake](cmake/GlazeAppIcon.cmake), [apps/demo/src/start_window.glazeui](apps/demo/src/start_window.glazeui)

Varje `AppWindow` har FÄLTET `appIcon` (`glaze::AppIcon`), SYNLIGT som förval: en liten ikon i namnlistens hörn — vänster på Windows/Linux, HÖGER på macOS. Samma bild sätts på OS-fönstret (aktivitetsfältet, Alt+Tab, dockan). Sätter appen ingen bild visas **GlazeUI-ikonen**, som är inbäddad i libglaze. EN bild räcker (t.ex. 256×256 i `assets.bin`) — ramverket skalar fram varje storlek; en egen handritad liten (16/32 px) kan anges vid sidan av. Med en SYNLIG menyrad kan ikonen göras STOR (namnlist + menyrad, kvadratisk) och menyraden börjar då bredvid. Ett klick öppnar PROGRAMMENYN (Office 2007-knappen) — finns ingen meny händer ingenting. Ytan är klickbar, inte dragyta. Registreringsnamn `appicon`.

### API
- `void setIcon(ImageSource img);` / `const ImageSource& icon() const;` — en asset eller en `glaze::Bitmap`; ogiltig (`{}`) = FÖRVALET (GlazeUI-ikonen). Ändrade Bitmap-pixlar når fönstret OCH OS-ikonen med ett nytt `setIcon(sammaBild)`; en `RenderTexture` duger i fönstret men inte som OS-ikon (pixlarna finns bara på GPU:n — avvisas med en logg).
- `void setSmallIcon(ImageSource img);` / `const ImageSource& smallIcon() const;` — egen liten bild; annars skalas `icon()` ner.
- `void setNoIcon(bool on);` / `bool isNoIcon() const;` — INGEN ikon alls: ingen plats i namnlisten, ingen OS-ikon.
- `void setLarge(bool on);` / `bool isLarge() const;` — en BEGÄRAN: gäller bara medan menyraden är synlig.
- `appIcon.hide()` — döljer ikonen i fönstret men OS-ikonen står kvar (WinForms `ShowIcon = false`).
- `std::pair<ImageSource, ImageSource> effectiveIcons() const;` — bildparet som GÄLLER (egen bild eller förvalet); `static constexpr bool onRightSide();` — true på macOS.
- Programmenyn har menyradens vokabulär med den TOMMA stigen `{}` som roten: `addItem(path, text, shortcut = "")` · `addCheck` · `addRadio` · `addSeparator(path)` · `addSubmenu(path, text)` · `setHandler(path, f)` · `setItemIcon`/`setItemEnabled`/`setItemChecked(path, …)` (postens tillstånd, `menus`) · `MenuItem* itemAtPath(path)` · `Menu& menu()` (träd-API:t — samma tillstånd) · `openMenu()`/`closeMenu()`/`isMenuOpen()`.
- `glaze::setAppId(id)` (`glaze/app.h`) FÖRE första fönstret — Linux app_id/`WM_CLASS`; tomt = programfilens namn. `[A-Za-z0-9._-]`, högst 255.

### Exempel
```cpp
class Huvud : public glaze::AppWindow {
public:
    Huvud() : glaze::AppWindow("Ritprogram", 800, 500)
    {
        menuBar.show();
        menuBar.addMenu("Arkiv");
        appIcon.setIcon(glaze::img("app", "logo"));     // EN bild, t.ex. 256×256
        appIcon.setLarge(true);                         // täcker namnlist + menyrad
        appIcon.addItem({}, "Nytt", "Ctrl+N");          // {} = programmenyns rot
        appIcon.addSubmenu({}, "Exportera");
        appIcon.addItem("Exportera", "PDF");
        appIcon.addSeparator({});
        appIcon.addItem({}, "Avsluta");
        appIcon.setHandler("Nytt", [this] { nytt(); }); // rotpost: sin egen text
        appIcon.setHandler({"Exportera", "PDF"}, [this] { pdf(); });
        appIcon.setHandler("Avsluta", [this] { close(); });
    }
private:
    void nytt();
    void pdf();
};
// En dialog utan ikon:   appIcon.setNoIcon(true);
// Ikonen bara på OS:et:  appIcon.hide();
```

### .glazeui
```cpp
    appIcon.setIcon(glaze::img("app", "logo"));
    appIcon.setSmallIcon(glaze::img("app", "logo16"));
    appIcon.setLarge(true);
    appIcon.addItem({}, glaze::tr(1002), "Ctrl+N");
    appIcon.addSubmenu({}, "Exportera");
    appIcon.addItem("Exportera", "PDF");
    appIcon.addSeparator({});
```
`appIcon` deklareras inte i GlazeBuilder-regionen — fönstret har fältet (som `menuBar`/`statusBar`). `setIcon({})` är laglig och betyder förvalet; `setNoIcon(true)` och `hide()` är också satser. `setHandler` står i `.cpp` (lambdan körs aldrig av previewn).

### Programfilens ikon (CMake)
```cmake
glaze_app_icon(min_app)                                   # GlazeUI-ikonen
glaze_app_icon(min_app ICO icon/app.ico ICNS icon/app.icns
               PNG_DIR icon PNG_NAME app DESKTOP min_app.desktop)
```
Utan argument: GlazeUI:s `Glaze/assets/icon/*`. Egna filer görs EN gång med `glazeassets icon <assets.bin> app:logo [--small app:logo16] icon app` (`.ico`, `.icns`, `app-<N>.png`) och committas — bygget genererar ingenting. Windows: exe:ns ikonresurs via windres (MSYS2-paketet `mingw-w64-ucrt-x86_64-binutils`). macOS: `.icns` i bundlen. Linux med `DESKTOP`: `cmake --install` lägger `.desktop` i `share/applications` och PNG:erna i hicolor som `<app_id>.png` — `.desktop`-filen ska heta `<app_id>.desktop` och bära `Icon=<app_id>` och `StartupWMClass=<app_id>`.

### Fallgropar
- `setLarge(true)` utan synlig menyrad ger den LILLA ikonen — begäran står kvar och slår igenom när `menuBar.show()` körs.
- Rotposter adresseras med sin egen text som ENDA segment (`setHandler("Nytt", …)`), poster i en undermeny med hela stigen (`{"Exportera", "PDF"}`). `addMenu` finns inte på ikonen.
- macOS har EN ikon för hela appen (dockan): bundlens `.icns` vinner ALLTID, och utan den äger det första fönster som satt en ikon dockan — ett fönsters egen bild syns i dess namnlist men byter inte dockikonen.
- Wayland visar fönstrets ikon bara med `xdg-toplevel-icon-v1` (KWin, Labwc); GNOME tar ikonen ur en INSTALLERAD `.desktop` som matchar app_id:t.
- Ramverkets dialoger (meddelanderutan, filväljaren) döljer sin ikon i fönstret.
<!-- END:app-icon -->

<!-- BEGIN:tray-icon | tray-icon trayicon systemfält systemfältsikon meddelandefältet notification-area notifyicon statusitem nsstatusitem tray animation close-to-tray stäng-till-systemfältet dold-start setshowintaskbar showintaskbar setshowindock dock -->
## Systemfältet — `glaze::TrayIcon` (`tray-icon`)

**Alias:** tray-icon, trayicon, systemfält, systemfältsikon, meddelandefältet, notification-area, notifyicon, statusitem, nsstatusitem, tray, animation, close-to-tray, stäng-till-systemfältet, dold-start, setshowintaskbar, showintaskbar, setshowindock, dock
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [tray_icon.h](Glaze/widgets/include/glaze/tray_icon.h), [app_window.h](Glaze/widgets/include/glaze/app_window.h), [app.h](Glaze/widgets/include/glaze/app.h), [apps/demo/src/tray_window.cpp](apps/demo/src/tray_window.cpp)

Appens ikon vid klockan — Windows meddelandefält och macOS menyradens statusposter (WinForms `NotifyIcon`). En KLASS och ingen widget: ikonen sitter i OS:ets fält och inte i ett fönsters träd, och den är APPENS och inte ett fönsters. Varje objekt är en ikon (flera går bra), DOLD tills `show()`. Bilden, tooltipen och animationen är settrar, menyn är VÅR (`glaze::Menu` i `appIcon`s vokabulär) och händelserna har IKONEN som `sender`. En app som BOR i fältet bygger dessutom på tre saker i fönstret: "stäng till systemfältet" (`onClose`-vetot + `hide()`), dold start (`hide()` före `run()`) och ingen knapp i aktivitetsfältet (`setShowInTaskbar(false)`, på macOS `glaze::setShowInDock(false)`).

### API
- `TrayIcon();` · `~TrayIcon();` — varken kopierbar eller flyttbar; destruktorn tar bort ikonen ur fältet och stänger menyn. `static bool isSupported();` — Windows och macOS true, Linux false, och det går att fråga innan någon `App` finns. `std::string name;` — basens (`glaze::Component`), objektets namn för loggarna och verktygen.
- `void show();` · `void hide();` · `bool isVisible() const;` — `show()` skapar `glaze::App` lat; `isVisible()` svarar med BEGÄRAN, också på Linux där ingen ikon syns.
- `void setIcon(ImageSource img);` · `void setSmallIcon(ImageSource img);` (+ `icon()`/`smallIcon()`) — en asset eller en `glaze::Bitmap` (ändrade pixlar når fältet med ett nytt `setIcon(sammaBild)`); ogiltig = FÖRVALET, GlazeUI-ikonen (programfilens egen ikon vinner där den finns); en bild som inte går att avkoda — och en `RenderTexture` — ger också förvalet. `void setTemplateImage(bool on);` — mallbild på macOS (menyraden färgar den efter läget), ingen verkan annars.
- `void setToolTip(TrText t);` · `const std::string& toolTip() const;` · `const TrText& toolTipToken() const;` — OS:ets egen tooltip, och `glaze::tr(id)` följer språkbytet.
- Animationen i kollektionsvokabulären: `void addFrame(ImageSource img);` · `void setFrame(int index, ImageSource img);` · `void setFrames(std::vector<ImageSource> frames);` · `int frameCount() const;` · `ImageSource frame(int index) const;` · `void setInterval(int ms);` (förval `kDefaultIntervalMs` = 500, klampas till `kMinIntervalMs` = 16) · `void startAnimation();` · `void stopAnimation();` · `bool isAnimating() const;` (BEGÄRAN).
- Menyn — `appIcon`s platta form med `{}` som roten, utan genvägsargument: `addItem(const MenuPath& path, TrText text)` · `addCheck(path, text, checked = false)` · `addRadio` · `addSeparator(path)` · `addSubmenu(path, text)` · `setHandler(path, f)` · `setItemIcon`/`setItemEnabled`/`setItemChecked(path, …)` (postens tillstånd, `menus`) · `MenuItem* itemAtPath(path)` · `Menu& menu()` (träd-API:t, samma tillstånd) · `void openMenu();` (vid ikonen, utan `onContextMenu`) · `void closeMenu();` · `bool isMenuOpen() const;`
- `Handler<ClickArgs, TrayIcon> onClick;` · `Handler<ClickArgs, TrayIcon> onDoubleClick;` · `Handler<TrayMenuArgs, TrayIcon> onContextMenu;` (FÖRE menyn, som öppnas efteråt om den har poster) · `Handler<HoverArgs, TrayIcon> onHover;` (`e.entered`) — `struct TrayMenuArgs : EventArgs {};`
- I fönstret och appen: `void AppWindow::setShowInTaskbar(bool on);` · `bool showInTaskbar() const;` (ett attribut, FÖRE `show()`) · `AppWindow::hide()` FÖRE `show()`/`run()` = dold start (`onLoad` men inte `onShow`) · `void glaze::setShowInDock(bool show);` · `bool glaze::showInDock();` (FÖRE första fönstret).

### Exempel
```cpp
class TrayApp : public glaze::AppWindow {
public:
    TrayApp() : glaze::AppWindow("Mätningen", 360, 160)
    {
        // Ingen knapp i aktivitetsfältet när ikonen är vägen tillbaka — utan
        // systemfält står knappen kvar. Ett ATTRIBUT: före run()/show().
        setShowInTaskbar(!glaze::TrayIcon::isSupported());

        tray_.name = "tray";
        tray_.setToolTip("Mätningen pågår");
        tray_.setFrames({glaze::img("tray", "busy1"), glaze::img("tray", "busy2")});
        tray_.setInterval(250);

        tray_.addItem({}, "Visa fönstret");           // {} = menyns rot
        tray_.addCheck({}, "Mät");
        tray_.addSeparator({});
        tray_.addItem({}, "Avsluta");
        tray_.setHandler("Visa fönstret", [this] { show(); });
        tray_.setHandler("Mät", [this] { mat(!tray_.isAnimating()); });
        tray_.setHandler("Avsluta", [this] { avsluta(); });

        // SENDER ÄR IKONEN — båda formerna.
        tray_.onClick = [this] { isHidden() ? show() : hide(); };
        tray_.onContextMenu = [this](glaze::TrayIcon* s, glaze::TrayMenuArgs&) {
            s->menu().items[0].enabled = isHidden();   // FÖRE menyn öppnas
            s->menu().items[1].checked = s->isAnimating();
        };

        // Ikonen läggs i fältet när fönstret ÖPPNAS — också vid en dold
        // start, där onLoad fyras men inte onShow.
        glaze::AppWindow::onLoad = [this] { tray_.show(); };

        // STÄNG TILL SYSTEMFÄLTET: krysset döljer, "Avsluta" och ett
        // appavslut (Cmd+Q — `CloseReason::AppQuit`) släpper igenom.
        glaze::AppWindow::onClose = [this](glaze::Widget*, glaze::CloseArgs& e) {
            e.cancel = e.reason == glaze::CloseReason::UserClosing &&
                       !avslutar_ && glaze::TrayIcon::isSupported();
            if (e.cancel)
                hide();
        };
    }

private:
    void mat(bool pa)
    {
        if (pa)
            tray_.startAnimation();   // går bara medan ikonen syns
        else
            tray_.stopAnimation();    // tillbaka till den stilla ikonen
    }

    void avsluta()
    {
        avslutar_ = true;
        close();                      // uppskjutet — onClose ser flaggan
    }

    glaze::TrayIcon tray_;            // MEDLEM — ikonen tas bort när fönstret dör
    bool avslutar_ = false;
};

int main(int argc, char** argv)
{
    const bool dold = argc > 1 && std::string(argv[1]) == "--hidden";
    const bool falt = glaze::TrayIcon::isSupported();
    if (falt)
        glaze::setShowInDock(false);  // macOS: ingen dockikon — FÖRE första fönstret
    TrayApp w;
    if (falt && dold)
        w.hide();                     // DOLD START: före run() öppnas fönstret dolt
    return w.run();                   // pumpar också med fönstret dolt
}
```
På macOS öppnar en statuspost normalt sin meny med ett vänsterklick — det är en rad: `tray_.onClick = [this] { tray_.openMenu(); };`

### .glazeui
Ikonen är en ICKE-VISUELL MEDLEM (`glaze::Component`): `glaze::TrayIcon ikon;` i regionen, aldrig adopterad, med platta settrar, kollektionsvokabulären för bilderna, `{}`-roten för menyn och kopplingar med ikonen som sender. Previewn skapar den och utför allt som bara LAGRAR — men aldrig `show()`/`openMenu()`: ritbordet lägger aldrig en ikon i OS:ets fält. `setHandler` (en lambda) bevaras och utförs inte. Aktivitetsfältet är ett naket fönsteranrop:
```cpp
    setShowInTaskbar(false);

    ikon.name = "ikon";
    ikon.setToolTip(glaze::tr(1001));
    ikon.setInterval(200);
    ikon.addFrame(glaze::img("ikoner", "a"));
    ikon.addItem({}, "Visa");
    ikon.addSubmenu({}, "Läge");
    ikon.addRadio("Läge", "Tyst", true);
    ikon.show();

    ikon.onClick = [this](glaze::TrayIcon* s, glaze::ClickArgs& e) { ikonClicked(s, e); };
    ikon.onHover = [this] { ikonHover(); };
```
Dold start hör hemma i `main`: ett nakent `hide();` i `.glazeui` kompilerar och gäller i appen, men previewn utför det medvetet inte — ritbordet ska visa fönstret.

### Plattformarna
| | Windows | macOS (blint skriven) | Linux |
|---|---|---|---|
| `TrayIcon::isSupported()` | true | true | **false** — `show()` loggar en gång, ingen ikon |
| Ikonen står | i meddelandefältet vid klockan | bland menyradens statusposter | — |
| Bilden | fältets lilla ikon, 16–48 px efter skalan | 18 pt; `setTemplateImage(true)` = mallbild | — |
| Tooltipen | kapas vid 127 tecken | knappens egen | — |
| `onClick` | vänsterklick, och Enter på en ikon vald med tangentbordet (Win+B) | vänsterklick | — |
| `onContextMenu` och menyn | högerklick, Skift+F10, menytangenten; menyn ovanför aktivitetsfältet | högerklick, Ctrl+klick; menyn under menyraden | — |
| `onHover`, "ut" | upp till 100 ms sent | direkt | — |
| `setShowInTaskbar(false)` | ingen knapp, Alt+Tab kvar | ingen verkan (dockan är appens) — loggas | ingen verkan — loggas |
| `glaze::setShowInDock(false)` | ingen verkan — loggas | ingen dockikon | ingen verkan — loggas |

### Fallgropar
- **Windows 11 lägger nya ikoner bland de DOLDA** (pilen vid klockan). Det är OS:ets policy och inget appen kan ändra — användaren fäster ikonen. Ramverket ger ikonen samma id varje körning (det lägsta lediga), så Windows minns valet.
- **Fråga `isSupported()` före en dold start och innan krysset döljer i stället för att stänga:** Linux har inget fält, och ett dolt fönster utan ikon går inte att nå — appen blir osynlig och lever tills den dödas.
- **Lägg ikonen som medlem i huvudfönstret eller som lokal i `main`**, aldrig som global eller statisk: då överlever den `App`. Det kraschar inte — fasaden är säker efter avslutet — men ikonen gör ingenting där.
- **En menypost har INTE ikonen som `sender`** utan menyns interna värdfönsters rot — fånga ikonen i lambdan. Ikonens egna events har ikonen som sender.
- Menyn öppnas i pumpens svans, efter att dess interna värdfönster tagit fokus: `isMenuOpen()` är true redan medan öppningen väntar, och `closeMenu()`/`hide()` avbryter den. Klick utanför och Escape stänger den som vilken meny som helst. Den har inga genvägar — inget fönster tar dess acceleratorer.
- Medan en MODAL dialog är öppen fyras klicken inte — dialogen lyfts i stället, som vid ett klick på ett blockerat fönster; hovern går igenom. Kör en dialog ur systemfältsmenyn ÄGARLÖS (`dlg.run()`): fönstret kan vara dolt.
- Ett dubbelklick ger `onClick` OCH sedan `onDoubleClick` (som WinForms och Qt) — en `onClick` som växlar fönstret har redan växlat det när `onDoubleClick` kommer.
- `isAnimating()` är en BEGÄRAN: animationen går först när ikonen syns och har minst två bilder (en enda står still), och annars sover timern (regel 1).
- `setShowInTaskbar` läses när OS-fönstret skapas — efter `show()` loggas anropet och gäller nästa öppning. "Stäng till systemfältet" behöver det inte: ett dolt fönster har ingen knapp. Minimerat utan knapp blir fönstret på Windows den klassiska remsan nere till vänster.
- En modal dialog startar aldrig dold — `hide()` före dess `run()` loggas och ignoreras. `glaze::setShowInDock` hör hemma före första fönstret; ett senare byte gäller direkt men kan lägga fönstren bakåt.
- macOS-vägen är skriven blint — det som återstår står i `doc/os/macos/TESTS.md`.
<!-- END:tray-icon -->

<!-- BEGIN:message-box | message-box messagebox meddelanderuta alert dialog fråga confirm bekräfta yes-no ja-nej messageboxbuttons messageboxtype dialogresult felmeddelande varning -->
## Meddelanderutan — `glaze::messageBox` (`message-box`)

**Alias:** message-box, messagebox, meddelanderuta, alert, dialog, fråga, confirm, bekräfta, yes-no, ja-nej, messageboxbuttons, messageboxtype, dialogresult, felmeddelande, varning
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [message_box.h](Glaze/widgets/include/glaze/message_box.h), [apps/demo/src/second_window.cpp](apps/demo/src/second_window.cpp)

En fri funktion som visar en MODAL ruta och returnerar knappen användaren valde — `wxMessageBox`/`MessageBox.Show` på en rad. Rutan är VÅR (ritad av Glaze, följer tema och språk), aldrig OS:ets. Den är ingen widget: den registreras inte, adopteras inte och finns aldrig i `.glazeui`.

### API
- `DialogResult messageBox(AppWindow& owner, const std::string& text, const std::string& caption = "", MessageBoxButtons buttons = MessageBoxButtons::OK, MessageBoxType type = MessageBoxType::Information, const ImageSource& customIcon = {});`
- `DialogResult messageBox(const std::string& text, const std::string& caption = "", MessageBoxButtons buttons = MessageBoxButtons::OK, MessageBoxType type = MessageBoxType::Information, const ImageSource& customIcon = {});` — ägarlös, för fel innan ett fönster finns.
- `enum class MessageBoxButtons { OK, OKCancel, YesNo, YesNoCancel, RetryCancel, AbortRetryIgnore };`
- `enum class MessageBoxType { Custom, Information, Confirmation, Warning, Error };` — styr bara ikonen; `Custom` = ingen ikon, eller `customIcon` (`glaze::img(…)`).

### Exempel
```cpp
void reportSaveError(glaze::AppWindow& owner)
{
    const glaze::DialogResult r =
        glaze::messageBox(owner, "Filen kunde inte sparas.", "Fel",
                          glaze::MessageBoxButtons::RetryCancel,
                          glaze::MessageBoxType::Error);
    if (r == glaze::DialogResult::Retry)
        gfx::log("försöker igen");
}

bool confirmDelete(glaze::AppWindow& owner, std::uint32_t textId)
{
    const glaze::TrText text = glaze::tr(textId);   // messageBox tar std::string
    return glaze::messageBox(owner, text.str(), "", glaze::MessageBoxButtons::YesNo,
                             glaze::MessageBoxType::Confirmation) ==
           glaze::DialogResult::Yes;
}
```

### .glazeui
Ingen form — rutan är ett anrop i en handler i `.cpp`, inte ett fönstertillstånd.

### Fallgropar
- `DialogResult::None` kommer aldrig tillbaka: Escape, krysset och ägarens kaskadstängning blir uppsättningens avbryt — `Cancel` (OKCancel, YesNoCancel, RetryCancel), `No` (YesNo), `Abort` (AbortRetryIgnore), `OK` (OK).
- `text`/`caption` är `std::string`, inte `TrText`: en översatt text skickas som `str()` av ett NAMNGIVET `TrText` (`glaze::tr(id).str()` på en temporär kompilerar inte).
- Knapptexterna är ramverkets egna och följer språket. Rutan är en vanlig dialog med standard- och avbrytknapp (`app-window`): första knappen har fokus och är standardknappen, Enter trycker den knapp som har fokus, och Escape trycker avbryt-knappen. YesNo och AbortRetryIgnore har ingen — där gör Escape ingenting och krysset är släckt. Det finns ingen `defaultButton`-parameter.
- Anropet blockerar med en nästlad pump; andra fönster ritas om men tar ingen input. Skicka ägaren när den finns — då hålls rutan över den och stängs med den.
- `\n` i texten bryter raden, och lång text bryts på ordgräns.
<!-- END:message-box -->

<!-- BEGIN:file-dialog | file-dialog filedialog filväljare open-file öppna-fil save-file spara-som folder-picker välj-mapp openfiledialog savefiledialog filter multiselect flerval dolda-filer showhidden -->
## Filväljardialogen — `glaze::FileDialog` (`file-dialog`)

**Alias:** file-dialog, filedialog, filväljare, open-file, öppna-fil, save-file, spara-som, folder-picker, välj-mapp, openfiledialog, savefiledialog, filter, multiselect, flerval, dolda-filer, showhidden
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [file_dialog.h](Glaze/widgets/include/glaze/file_dialog.h), [include/glaze/CLAUDE.md](Glaze/widgets/include/glaze/CLAUDE.md)

Vår egen filväljare (aldrig OS:ets) i tre lägen över en implementation: `Open`, `Save` och `Folder`. En KLASS med egenskaper och `run(ägare)`, WinForms-formen: kända platser till vänster (`gfx::knownPlaces`), mappen i en sorterbar listvy, adressrad, namnfält och filtyp. Behåll objektet som medlem, så öppnar nästa `run` där användaren slutade.

### API
- `explicit FileDialog(FileDialogMode mode = FileDialogMode::Open);` · `enum class FileDialogMode { Open, Save, Folder };` · `void setMode(FileDialogMode m)`
- `void setTitle(TrText t)` (tom = lägets egen) · `void setFolder(std::string utf8)` (startmappen; efter OK mappen användaren slutade i) · `void setFileName(std::string utf8)` (förslaget i Save)
- `void addFilter(TrText name, std::string patterns);` · `void setFilter(int index, TrText name, std::string patterns);` · `void setFilters(std::vector<FileFilter> filters);` · `void setFilterIndex(int i)` · `int filterIndex() const` — mönster som `"*.png;*.jpg"`, skiftlägesokänsliga; utan filter gäller "Alla filer (*.*)".
- `void setMultiSelect(bool on)` (bara Open) · `void setOverwritePrompt(bool on)` (bara Save, PÅ) · `void setShowHidden(bool on)` · `bool showHidden() const`
- `DialogResult run(AppWindow& owner);` · `DialogResult run();` — `OK` eller `Cancel`, aldrig `None`.
- `const std::string& path() const;` · `const std::vector<std::string>& paths() const` — UTF-8, absoluta; tömda vid varje `run`.

### Exempel
```cpp
class DocWindow : public glaze::AppWindow {
public:
    DocWindow() : glaze::AppWindow("Dokument", 640, 400)
    {
        openDlg_.setFilters({{"Bilder", "*.png;*.jpg"}, {"Alla filer", "*"}});
        openDlg_.setMultiSelect(true);
        auto* open = add<glaze::Button>("Öppna…");
        open->onClick = [this] {
            if (openDlg_.run(*this) == glaze::DialogResult::OK)   // modal, OK/Cancel
                for (const std::string& path : openDlg_.paths())  // UTF-8, absoluta
                    gfx::log("öppna %s", path.c_str());
        };
    }

private:
    glaze::FileDialog openDlg_{glaze::FileDialogMode::Open};   // medlem: minns mappen
};

void saveAs(glaze::AppWindow& owner)
{
    glaze::FileDialog dlg(glaze::FileDialogMode::Save);
    dlg.setFileName("Rapport.txt");
    dlg.addFilter("Text", "*.txt");
    if (dlg.run(owner) == glaze::DialogResult::OK)
        gfx::log("spara till %s", dlg.path().c_str());
}
```

### .glazeui
Dialogen har ingen form — den är ingen widget. Fältet som öppnar den, `glaze::FilePicker`, har (`widget-file-picker`):
```cpp
    picker.setMode(glaze::FileDialogMode::Save);
    picker.addFilter("Bilder", "*.png;*.jpg");
```

### Fallgropar
- Använd aldrig OS:ets dialog — den spräcker illusionen, samma regel som för namnlisten.
- `path()` är det FÖRSTA valet vid flerval; läs `paths()` för alla.
- I Save-läget läggs filtrets ändelse på, och ett befintligt namn ger en Ja/Nej-fråga om ersättning — stäng av med `setOverwritePrompt(false)` om appen frågar själv.
- `showHidden()` skrivs tillbaka även när dialogen avbryts (en vyinställning); `folder()`/`fileName()` bara vid OK.
- Fel (ingen behörighet, borttagen mapp) visas som en mening i dialogen — den kraschar aldrig.
- Tangenterna är en vanlig dialogs: Enter trycker OK (standardknappen) och Escape Avbryt, utom där kontrollen med fokus tar tangenten själv — adressfältet (Enter går till mappen), fillistan med en aktuell rad (Enter öppnar mappen eller väljer filen) och en öppen filtypslista (Escape stänger bara listan). Backspace utanför textfälten och Alt+Upp går uppåt, F5 läser om.
<!-- END:file-dialog -->

<!-- BEGIN:color-dialog | color-dialog colordialog färgdialog välj-färg pick-color wxcolourdialog qcolordialog modal alfa alpha -->
## Färgdialogen — `glaze::ColorDialog` (`color-dialog`)

**Alias:** color-dialog, colordialog, färgdialog, välj-färg, pick-color, wxcolourdialog, qcolordialog, modal, alfa, alpha
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [color_dialog.h](Glaze/widgets/include/glaze/color_dialog.h), [include/glaze/CLAUDE.md](Glaze/widgets/include/glaze/CLAUDE.md)

En MODAL färgdialog (WinForms `ColorDialog`) — vår, aldrig OS:ets. Innehållet är färgväljarens yta (palett, R/G/B-reglage, A med alfan, hexfält) plus en förhandsruta och OK/Avbryt. I dialogen stänger ett val ingenting: paletten och pilarna VÄLJER, OK eller Enter tar färgen, Avbryt, Escape eller krysset kastar den. En KLASS som `FileDialog`: behåll objektet som medlem, så öppnar nästa `run` med förra valet. Ingen widget — för en färg i ett formulär finns `ColorPicker` (`widget-color-picker`).

### API
- `void setColor(gfx::Color c)` / `gfx::Color color() const` — färgen dialogen öppnar med, och efter OK den användaren valde; opak utan alfan.
- `void setAllowAlpha(bool on)` / `bool allowAlpha() const` — A-reglaget och `#RRGGBBAA`.
- `void setTitle(TrText t)` / `const std::string& title() const` — tom = ramverkets "Färg".
- `DialogResult run(AppWindow& owner);` · `DialogResult run();` — `OK` eller `Cancel`, aldrig `None`.

### Exempel
```cpp
class Ritare : public glaze::AppWindow {
public:
    Ritare() : glaze::AppWindow("Ritare", 480, 320)
    {
        fargDlg_.setAllowAlpha(true);
        auto* farg = add<glaze::Button>("Linjefärg…");
        farg->onClick = [this] {
            fargDlg_.setColor(linje_);
            if (fargDlg_.run(*this) == glaze::DialogResult::OK)   // modal
                linje_ = fargDlg_.color();
        };
    }

private:
    glaze::ColorDialog fargDlg_;   // medlem: öppnar med förra valet
    gfx::Color         linje_{51, 102, 255, 255};
};
```

### .glazeui
Ingen form — dialogen är ett anrop i en handler i `.cpp`. Färgväljarfältet `ColorPicker` är formens färg i `.glazeui`.

### Fallgropar
- Färgen skrivs bara vid OK: efter Avbryt är `color()` den gamla.
- Ett skrivet hexvärde som inte bekräftats med Enter committas av OK, också när fokus står kvar i fältet.
<!-- END:color-dialog -->

<!-- BEGIN:input-box | input-box inputbox inmatningsruta fråga-text prompt rename byt-namn wxgettextfromuser qinputdialog textdialog multiline flerrad inputboxmode -->
## Inmatningsrutan — `glaze::inputBox` (`input-box`)

**Alias:** input-box, inputbox, inmatningsruta, fråga-text, prompt, rename, byt-namn, wxgettextfromuser, qinputdialog, textdialog, multiline, flerrad, inputboxmode
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [input_box.h](Glaze/widgets/include/glaze/input_box.h), [include/glaze/CLAUDE.md](Glaze/widgets/include/glaze/CLAUDE.md)

En fri funktion som visar en MODAL ruta med en ledtext, ett textfält och OK/Avbryt — meddelanderutans syskon (VB `InputBox`, `wxGetTextFromUser`). Förvalet står MARKERAT med fokus i fältet, så man skriver över det eller trycker Enter. Vår, aldrig OS:ets; ingen widget.

### API
- `std::optional<std::string> inputBox(AppWindow& owner, const TrText& prompt, const TrText& caption = {}, const std::string& initial = {}, InputBoxMode mode = InputBoxMode::SingleLine);`
- `std::optional<std::string> inputBox(const TrText& prompt, const TrText& caption = {}, const std::string& initial = {}, InputBoxMode mode = InputBoxMode::SingleLine);` — ägarlös.
- `enum class InputBoxMode { SingleLine, MultiLine }` — fältets form: ett `TextInput` (Enter är OK) eller ett `TextArea` (Enter är en ny rad, rutan stängs med OK-knappen; fältet är stilens `textAreaRows` rader högt).
- Svaret: texten vid OK — också en TOM text, som är ett svar — och `std::nullopt` vid Avbryt, Escape, krysset eller ägarens kaskadstängning.

### Exempel
```cpp
void renameLayer(glaze::AppWindow& owner, std::string& name)
{
    if (auto svar = glaze::inputBox(owner, "Lagrets namn:", "Byt namn", name))
        name = *svar;   // nullopt = avbrutet
}

void editComment(glaze::AppWindow& owner, std::string& comment)
{
    // Flera rader: Enter ger en ny rad, OK trycks med knappen.
    if (auto svar = glaze::inputBox(owner, "Kommentar:", "Redigera", comment,
                                    glaze::InputBoxMode::MultiLine))
        comment = *svar;
}
```

### .glazeui
Ingen form — rutan är ett anrop i en handler i `.cpp`.

### Fallgropar
- Enter är OK och Escape Avbryt (fönstrets standard- och avbrytknapp, `app-window`) — utom i `MultiLine`, där Enter är en ny rad och bara OK-knappen stänger med svar (Escape är fortfarande Avbryt).
- Ledtexten och rubriken är `TrText` (`glaze::tr(id)` går rakt in), förvalet en `std::string` — användarens text. I `MultiLine` är svaret textens LF-form (`TextArea` normaliserar CRLF).
- Inget lösenordsläge, ingen validering medan man skriver — en sådan dialog byggs av samma widgets (`app-window`).
<!-- END:input-box -->

<!-- BEGIN:print | print printjob utskrift skriv-ut skrivare printer pdf papper paper a4 letter orientering liggande landscape marginaler margins utskrivbart-område printdialog utskriftsdialog sidintervall kopior painter -->
## Utskriften — `glaze::PrintJob` (`print`)

**Alias:** print, printjob, utskrift, skriv-ut, skrivare, printer, pdf, papper, paper, a4, letter, orientering, liggande, landscape, marginaler, margins, utskrivbart-område, printdialog, utskriftsdialog, sidintervall, kopior, painter
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [print.h](Glaze/widgets/include/glaze/print.h), [gfx/print.h](Glaze/gfx/include/gfx/print.h)

Utskrift genom OPERATIVSYSTEMETS utskriftssystem — en PDF fås genom systemets PDF-skrivare. Dialogen är SYSTEMETS (skrivare, kopior, sidintervall och skrivarens egna inställningar) — ett medvetet undantag från ramverkets egna dialoger. Sidan ritas med ritytans `Painter` i sidans MILLIMETER: (0, 0) i pappersets övre vänstra hörn, y nedåt, och `glaze::mm(…)` är mm på papperet — samma ritfunktion kan rita förhandsvisningen på en `Canvas` och sidan på skrivaren. En KLASS som `FileDialog`: behåll objektet, så skriver nästa `run` ut på samma skrivare. Windows och macOS (BLINT); Linux svarar `isSupported() == false`.

### API
- `static bool isSupported();`
- `void setTitle(TrText t);` — dokumentets namn i skrivarkön; tomt = ägarens titel.
- `void setPaper(PaperSize p);` (`A3`, `A4`, `A5`, `A6`, `Letter`, `Legal`, `Tabloid`) · `void setPaperSize(double bMm, double hMm);` (egna, stående) · `PaperSize paper() const;` (`Custom` för egna mått)
- `void setOrientation(PageOrientation o);` (`Portrait`, `Landscape`) · `double paperWidth() const;` / `double paperHeight() const;` — de ORIENTERADE måtten, sidans rymd.
- `void setCopies(int n);` · `void setPageRange(int från, int till);` (från 1; 0, 0 = alla) · `void setPageCount(int n);` (dialogens tak) · `void setResolution(int dpi);` (förval 300, 72–1200, aldrig över skrivarens)
- `bool showDialog(AppWindow& ägare);` — true = skriv ut; valen står sedan i egenskaperna (`printerName()`, `copies()`, `fromPage()`/`toPage()`, papperet och orienteringen). Avbruten = false och egenskaperna orörda.
- `Rect printableArea() const;` · `PageMargins margins() const;` — det utskrivbara området och marginalerna i mm (hela papperet före en skrivare valts).
- `bool run(AppWindow& ägare, int sidor, PageFn rita);` — `sidor` > 0, eller 0 = sida för sida tills `rita` svarar false. `PageFn` är `bool(int sida, Painter& p)`, sidan från 1; false = sista sidan. Utan dialog först: standardskrivaren. Ett modalt förloppsfönster med Avbryt visas medan jobbet går.
- `void cancel();` · `bool isRunning() const;` · `bool isCancelled() const;` · `int pagesPrinted() const;`

### Exempel
```cpp
void ritaSida(int sida, glaze::Painter& p, const glaze::PrintJob& jobb)
{
    const glaze::Rect r = jobb.printableArea();
    p.drawRect({r.x + 5, r.y + 5, r.w - 10, r.h - 10}, {.width = glaze::mm(0.3)});
    p.drawText({r.x + 10, r.y + 15}, "Sida " + std::to_string(sida),
               {.size = glaze::mm(5)});
}

utskrift->onClick = [this] {
    jobb_.setPaper(glaze::PaperSize::A4);
    jobb_.setPageCount(3);
    if (!jobb_.showDialog(*this))
        return;   // avbruten
    jobb_.run(*this, 3, [this](int sida, glaze::Painter& p) {
        ritaSida(sida, p, jobb_);
        return true;
    });
};
// glaze::PrintJob jobb_;   — medlem: nästa run skriver ut på samma skrivare
```

### .glazeui
Ingen form — utskriften är ett anrop i en handler, som en dialog.

### Fallgropar
- SIDAN RASTRERAS: den skickas som en bild i `resolution()` dpi (högst `PrintJob::kMaxPagePx` punkter på längsta sidan), så linjer och text blir inte vektorer i en PDF-skrivare än. Bilderna är fullt upplösta och väntar aldrig på uppladdningen.
- `rita` kan köras MER ÄN EN GÅNG för samma sida (när en bild väntade på uppladdningen) — rita deterministiskt, som i `onPaint`, och ändra ingenting där.
- Sidintervallet sida för sida: sidorna före `fromPage()` ritas (för att veta var jobbet är) men skickas inte.
- `rita` får varken stänga ägaren eller öppna en modal.
- Bara det utskrivbara området kommer ut — rita innanför `printableArea()`.
- `setPaper`/`setPaperSize`/`setOrientation`/`setCopies` EFTER dialogen öppnar samma skrivare på nytt vid nästa `run`, och `printableArea()` är hela papperet till dess — sätt formatet före `showDialog`.
- macOS: dialogen är appmodal (inget ark), och jobbet skickas när sista sidan ritats, med systemets förloppspanel.
<!-- END:print -->

<!-- BEGIN:tooltip | tooltip tooltips verktygstips hjälptext hover-text settooltip tooltipwidget hint tips förklaring -->
## Tooltips — `setToolTip` på varje widget (`tooltip`)

**Alias:** tooltip, tooltips, verktygstips, hjälptext, hover-text, settooltip, tooltipwidget, hint, tips, förklaring
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [widget.h](Glaze/widgets/include/glaze/widget.h), [app_window.h](Glaze/widgets/include/glaze/app_window.h), [tooltip.cpp](Glaze/widgets/src/tooltip.cpp)

Tooltipen är en egenskap på BASEN, ingen egen komponent: varje widget kan ha en. När pekaren VILAR över widgeten i `Style::toolTipDelayMs` visas en platta under pekaren — ett popup-fönster, så det fungerar även på Wayland — och den göms vid rörelse ut, klick, tangent, hjul och fokusförlust. Texten är en `TrText`, så `glaze::tr(id)` följer språket.

### API
- `void setToolTip(TrText t);` · `const std::string& toolTip() const` · `const TrText& toolTipToken() const` — tom text = ingen tooltip.
- `Widget* toolTipWidget() const` (AppWindow) — vems tooltip som visas just nu, annars `nullptr`.
- `virtual TrText toolTipAt(const Ctx& c, float x, float y) const` — tooltip PER DEL för en egen widget vars delar är data (handtag, rader): basen svarar med `toolTipToken()`, en override med delens text under pekaren (fysiska px, widgetens behavior-Ctx). Källan är widget OCH text — två delar är två källor.
- `Style::toolTipDelayMs` · `Style::toolTipReshowMs` — fördröjningen och den kortare väntan när pekaren går direkt till nästa; stilens tal, inte per widget.

### Exempel
```cpp
void toolTips(glaze::Button& save, glaze::Panel& tools)
{
    save.setToolTip("Spara filen — Ctrl+S");   // tom text = ingen tooltip
    tools.setToolTip(glaze::tr(1205));         // barn utan egen tooltip visar panelens
    if (glaze::AppWindow* w = save.appWindow())
        if (w->toolTipWidget() == &save)       // vems tooltip visas just nu?
            gfx::log("visar: %s", save.toolTip().c_str());
}
```

### .glazeui
Ett setter-fält på varje typ, literal eller översatt:
```cpp
    save.setToolTip("Spara filen — Ctrl+S");
    side.setToolTip(glaze::tr(1205));
```

### Fallgropar
- ROUTAD uppslagning: en widget utan egen tooltip visar närmaste förfaders. Sätt den på en panel för att förklara barnen; en komposits inre delar visar kompositens.
- En INAKTIVERAD widget visar sin tooltip — det är där förklaringen behövs ("kräver en öppen fil").
- Efter klick eller tangent visas den inte igen förrän pekaren lämnat widgeten.
- Byts texten medan tooltipen visas stängs den; nästa vila visar den nya. (En dels text ur `toolTipAt` som byts syns först vid nästa vila.)
- Ren text, ingen formatering; lång text bryts på ordgräns vid `Style::toolTipMaxW`.
- En egen widget som överrider `setTextToken` måste avsluta med `return Widget::setTextToken(key, value);`, annars blir `setToolTip(glaze::tr(…))` opak i previewn.
<!-- END:tooltip -->

<!-- BEGIN:drag-drop | drag-drop drag-and-drop dra-och-släpp dnd setallowdrag setallowdrop ondragstart ondragenter ondrop ondragend dragdata drageffect dragargs släppmål källa handled candragfrom godkännande -->
## Dra och släpp inom fönstret (`drag-drop`)

**Alias:** drag-drop, drag-and-drop, dra-och-släpp, dnd, setallowdrag, setallowdrop, ondragstart, ondragenter, ondrop, ondragend, dragdata, drageffect, dragargs, släppmål, källa, handled, candragfrom, godkännande
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [widget.h](Glaze/widgets/include/glaze/widget.h), [include/glaze/CLAUDE.md](Glaze/widgets/include/glaze/CLAUDE.md), [tests/dnd_gate.cpp](tests/dnd_gate.cpp)

WinForms-formen på `Widget`-basen: `setAllowDrag(true)` gör en widget till KÄLLA, `setAllowDrop(true)` till MÅL, och sex events bär resten. Ett drag startar när pekaren rört sig mer än `Style::dragThreshold` från trycket; ramverket ritar dragbilden, märket och ramen runt ett accepterande mål i varje UI. Räckvidden är fönstret — inte mellan fönster och inte till andra appar.

**Ordningen:** `onDragStart` → på varje mål `onDragEnter` och DIREKT därefter `onDragOver`, sedan `onDragOver` per rörelse, `onDragLeave` när pekaren lämnar → `onDrop` → alltid sist `onDragEnd` på källan. Varje plats möter alltså `onDragOver`, också den första. En widget med INBYGGT beteende (`TreeView`) föreslår först i varje steg och appen justerar — utom i SLÄPPET, där `onDrop` frågas FÖRE det inbyggda: `e.effect = None` nekar, och `e.handled = true` betyder att appen utförde släppet själv, så det inbyggda hoppar över.

### API
- `void setAllowDrag(bool on)` · `void setAllowDrop(bool on)`
- Källan: `Handler<DragStartArgs> onDragStart;` (fyll `e.data`; tom last = inget drag) · `Handler<DragEndArgs> onDragEnd;` (`e.effect` = utfallet, `e.target`)
- Målet: `Handler<DragArgs> onDragEnter;` · `onDragOver` (vid inträdet och per musrörelse) · `onDragLeave` · `onDrop` — sätt `e.effect` för att acceptera.
- `struct DragStartArgs { DragData data; DragEffect allowed; std::string text; ImageSource img; float x, y, localX, localY; }` (dragbildens ikon: en asset eller en `glaze::Bitmap`) · `struct DragArgs { const DragData& data; DragEffect allowed; DragEffect effect; float x, y, localX, localY; Widget* source; bool handled; }` (`handled` läses bara efter `onDrop`)
- EGNA widgets: den skyddade kroken `virtual bool canDragFrom(const Ctx&, float x, float y) const` — får ett drag börja där TRYCKET skedde? Frågas en gång, före `dragStart`-kroken och `onDragStart`; `false` = trycket förblir ett tryck. Kroken `drop` körs efter `onDrop` med den validerade effekten, och `isDragSource()` säger om widgeten är källan till ett drag som startar eller pågår.
- `enum class DragEffect : std::uint8_t { None, Copy, Move, Link };` (bitmask) · `constexpr bool allows(DragEffect mask, DragEffect e)`
- `class DragData` — `set(format, value)`, `has(format)`, `get(format)`, `setText(std::string utf8)`, `text()`; `kText = "text/plain"`. Formaten är MIME-typer, värdena bytes.
- `bool isDragging() const` (AppWindow).

### Exempel
```cpp
class DragWindow : public glaze::AppWindow {
public:
    DragWindow() : glaze::AppWindow("Dra och släpp", 480, 240)
    {
        auto* source = add<glaze::Button>("Rapport.pdf");
        source->setAllowDrag(true);                          // KÄLLA
        source->onDragStart = [source](glaze::Widget*, glaze::DragStartArgs& e) {
            e.data.setText(source->text());                  // tom last = inget drag
            e.allowed = glaze::DragEffect::Copy;
        };

        auto* zone = add<glaze::Panel>();
        zone->x      = 200;
        zone->width  = 200;
        zone->height = 120;
        zone->setBorder(true);
        zone->setAllowDrop(true);                            // MÅL
        zone->onDragEnter = [](glaze::Widget*, glaze::DragArgs& e) {
            if (e.data.has(glaze::DragData::kText) &&
                glaze::allows(e.allowed, glaze::DragEffect::Copy))
                e.effect = glaze::DragEffect::Copy;          // acceptera; None = avvisa
        };
        zone->onDrop = [this](glaze::Widget*, glaze::DragArgs& e) {
            setTitle(e.data.text());                         // e.data gäller under anropet
        };
    }
};
```

### .glazeui
```cpp
    save.setAllowDrag(true);
    side.setAllowDrop(true);
    save.onDragStart = [this](glaze::Widget* s, glaze::DragStartArgs& e) { saveDragStart(s, e); };
    side.onDrop = [this](glaze::Widget* s, glaze::DragArgs& e) { sideDrop(s, e); };
```

### Fallgropar
- Källans handler MÅSTE fylla lasten — den korta formen `[this] { … }` kan inte det, så skriv fullformen för `onDragStart`.
- En effekt utanför källans `allowed` räknas som `None`; `effect` bärs mellan enter och over, så ett mål kan svara en gång i `onDragEnter`.
- `e.data` är en referens som bara gäller under anropet — kopiera det du behöver.
- `Move` betyder att posten ska LÄMNA källan: ta bort den i källans `onDragEnd` när `e.effect == glaze::DragEffect::Move`.
- En FLYTANDE dockningspanel är ett eget fönster: ett drag mellan den och huvudfönstret fungerar inte (räckvidden är fönstret — `widget-dock`).
- Ett tryck som blev ett drag ger inget `onClick`. Escape och fokusförlust avbryter (leave + end med `None`).
- `onDragOver` körs per musrörelse utan full omritning — ändra bara via settrar där. Tas källan eller målet bort ur trädet mitt i ett drag körs inga fler handlers.
- En regel PER PLATS hör hemma i `onDragOver` (den fyras också vid inträdet); en regel om VAD som dras räcker i `onDragEnter`, men en widget som föreslår om vid varje rörelse (`TreeView`) skriver över ett veto där.
- `onDrop` körs FÖRE det inbyggda släppet: `effect = None` nekar (och vinner över `handled`), `handled = true` hoppar över det inbyggda, och ett `Copy` får en widget som bara flyttar att låta bli — kopian är appens. Tar `onDrop` bort MÅLET ur trädet utan `handled` blir utfallet `None`: ingen utförde släppet, och källan behåller det som drogs.
- Ett drag från en VALD nod i ett `TreeView` med flerval drar hela valet: lasten `TreeView::kNodeFormat` är stigarna åtskilda med `;` i trädordning (`"0,1;2"`), `text()` nodernas texter radvis; läs dem som stigar med `draggedPaths()`. I en `ListView` fyller appen lasten ur `selectedRows()` — båda väljer en vald post ensam först vid SLÄPPET, så trycket inte väljer bort resten.
- Ett drag börjar aldrig i en listvys huvud, kolumnavdelare eller kryssruta, eller på trädets pil, ruta eller rullningslist — också när `onDragStart` fyller lasten (`canDragFrom`).
- AUTOSKROLLEN är inbyggd: nära kanten av en rullande yta (träd, listor, tabeller, paneler) rullar den med fönstrets timer medan draget pågår, och släppets plats och `onDragOver` följer med. En egen rullande widget får den genom att overrida `Widget::dragScroll(c, x, y, apply)` (eller ärva `Panel`).
<!-- END:drag-drop -->

<!-- BEGIN:canvas | canvas rityta karta map ritning drawing graf graph chart plot scen scene kamera camera 2d 3d qgraphicsview wxglcanvas opengl mapview memorybudget minnesbudget -->
## Canvas — ritytan, översikt (`canvas`)

**Alias:** canvas, rityta, karta, map, ritning, drawing, graf, graph, chart, plot, scen, scene, kamera, camera, 2d, 3d, qgraphicsview, wxglcanvas, opengl, mapview, memorybudget, minnesbudget
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [canvas.h](Glaze/widgets/include/glaze/widgets/canvas.h), [canvas/CLAUDE.md](Glaze/widgets/include/glaze/canvas/CLAUDE.md)

EN widget för kartor, grafer och egen ritning, i 2D och 3D. Innehållet är FORMER (`glaze::Shape`) i LAGER som laddas upp till GPU:n en gång; panorering, zoom, vridning och tilt är bara KAMERAN, så inget ritas om eller laddas upp när användaren drar. Det som ändras varje ritning (en HUD, ett gummiband) ritas med en `Painter` i `onPaint`/`onPaintOverlay`. Koordinaterna är `double` — SWEREF-meter går rakt in. Registreringsnamn `canvas`.

**Delavsnitt:** `canvas-shapes` (formerna och stilarna) · `canvas-layers` (lagren och formlistan) · `canvas-camera` (kartkameran och gesterna) · `canvas-units` (px, mm, world) · `canvas-painter` (omedelbar ritning) · `canvas-hit-test` (träffprov och mus) · `canvas-images` (Bitmap, RenderTexture) · `canvas-3d` · `canvas-meshes` · `canvas-terrain` · `canvas-programs` · `canvas-views` · `canvas-first-person`.

### API
- `void setBackground(gfx::Color c)` · `bool hasBackground() const` — osatt följer temat; alfa 0 = genomskinlig.
- `void setBorder(bool on)` / `bool border() const` — förval på.
- `void setFlat(bool on)` / `bool flat() const` — PLATT YTA (REQ8 4), kant i kant med grannarna (ritytor staplade i en delare eller en dockningspanel): ingen kant, inga rundade hörn — bakgrunden, scenen, overlayn och barnen klipps rakt till ytan — och fokusmarkeringen INNANFÖR ytan, så den aldrig ritas över grannen. `border()` står kvar och gäller igen när ytan inte längre är platt. Förval av.
- `void setGridSize(double size)` / `double gridSize() const` — rutnät var `size` världsenhet, 0 = inget.
- `Handler<PaintArgs> onPaint` · `onPaintLayer` (i ett lagers ordning, `canvas-painter`) · `onPaintOverlay` · `Handler<CanvasMouseArgs> onMouseMove` · `onMouseWheel` · `onDoubleClick` — och basens `onMouseDown`/`onMouseUp`/`onMouseCancel` (`Handler<MouseArgs>` på varje widget, `events`), vars fullform med `glaze::CanvasMouseArgs&` får ritytans args
- `Handler<KeyArgs> onKeyDown` · `Handler<GestureArgs> onGesture` · `Handler<TickArgs> onTick` · `Handler<ChangeArgs> onCameraChange`
- `void setMemoryBudget(std::size_t bytes)` / `std::size_t memoryBudget() const` — MINNET (REQ3 10): ritytans budget i byte för bilderna, render-texturerna och de omräknade bilderna den ritar (räknade på den rityta som ritade dem SENAST) och dess draperingar; 0 = `app().canvasMemoryBudget()`. Lagrar och ritar inte om — gäller från nästa ritning (`canvas-images`).
- AUTO-måtten är stilens `canvasDefaultW`/`canvasDefaultH`; ritytan är ett tabbstopp och kan ha widgets som barn.
- `void setChildAnchor(Widget& barn, Point världen, TextAnchor ankare = TextAnchor::Center)` · `void clearChildAnchor(Widget& barn)` · `std::optional<Canvas::ChildAnchor> childAnchor(const Widget& barn) const` — FÖRANKRADE BARN (REQ8 14): ett barn till ritytan står med sitt mätta mått och `ankare`-hörnet i världspunkten (`BottomLeft` = nedre vänstra hörnet i punkten) och följer kameran i SAMMA bildruta som scenen, också under tröghet och glidning. Barnets dock/anchors/x/y ignoreras; utanför ritytan klipps det, och en punkt bakom kameran eller ovanför horisonten ger en tom yta (synligheten rörs inte). Får stå före `adopt`. En inforuta eller anteckning är en riktig widget — `FloatingPanel` (`widget-floating-panel`) flyttas av användaren, och då flyttar ankaret.

### Exempel
```cpp
class Karta : public glaze::AppWindow {
public:
    Karta() : glaze::AppWindow("Karta", 800, 600)
    {
        karta.dock = glaze::Dock::Fill;   // AUTO-måtten är canvasDefaultW/H
        karta.setYUp(true);               // en karta: y uppåt (förval: nedåt)
        karta.setCenter(674000, 6580000); // SWEREF 99 TM i meter, double
        karta.setMapScale(2000);          // 1:2000 med linjal
        karta.setPannable(true);          // gesterna är AV från start
        karta.setZoomable(true);

        const int vagar = karta.addLayer("Vägar");   // lager 0 finns redan
        karta.addShape(vagar, glaze::Shape::polyline(
            {{673900, 6579950}, {674000, 6580000}, {674100, 6580020}},
            {.paint = gfx::Color{210, 70, 40}, .width = glaze::mm(0.8f)}));
        karta.addShape(vagar, glaze::Shape::text({674000, 6580000}, "Storgatan",
            {.size = glaze::mm(2.2f), .halo = gfx::Color{255, 255, 255}}));

        karta.onDoubleClick = [this](glaze::Widget*, glaze::CanvasMouseArgs& e) {
            const std::vector<glaze::HitResult> h = karta.hitTest(e.local);
            if (!h.empty())   // överst först
                setTitle("Lager " + std::to_string(h.front().layer) + ", form " +
                         std::to_string(h.front().index));
        };

        root().adopt(karta);
    }

private:
    glaze::Canvas karta;
};
```

### .glazeui
```cpp
karta.setYUp(true);
karta.setCenter(674000, 6580000);
karta.setMapScale(2000);
karta.setPannable(true);
karta.setGridSize(10);
karta.setFlat(true);
karta.addLayer("Vägar");
karta.addShape(1, glaze::Shape::polyline({{673900, 6579950}, {674000, 6580000}}, {.paint = gfx::Color{210, 70, 40}, .width = glaze::mm(0.8)}));
karta.setMemoryBudget(268435456);
karta.setChildAnchor(info, {674000, 6580100}, glaze::TextAnchor::BottomLeft);
karta.onDoubleClick = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) { kartaDblClick(s, e); };
```
Formlistan, kameran, lagren, minnesbudgeten (ett heltal i byte), den platta ytan och förankringen (barnet med sitt medlemsnamn — det måste vara ritytans barn, `karta.adopt(info);`) står i `.glazeui` som i koden och previewn utför dem. `addLayer` i filen använder inte svaret, så lagret anges med sitt INDEX (lager 0 finns från start). Handlerna (`onPaint` …) kör previewn aldrig.

### Fallgropar
- Gesterna är AV från start — `setPannable(true)` m.fl. (se `canvas-camera`).
- Utan kamera är världen ritytans lokala logiska px, y nedåt. Sätt `setYUp(true)` för en karta FÖRE formerna med bilder.
- Formernas färger följer inte temat (förval svart); bara ytan bakom gör det.
- `onMouseUp` kommer också efter en panorering — skilj klick från drag (se `canvas-hit-test`).
- `setBorder(false)` tar bara bort kanten — hörnen klipps fortfarande runda och fokusringen ritas UTANFÖR ytan, över grannen. Ytor som ska stå kant i kant vill ha `setFlat(true)`.
- En rityta UTAN YTA (en inaktiv dockningsflik, ett mått på 0) ritas inte och TICKAR inte — `onTick`, glidningarna och trögheten pausar tills den syns igen. `zoomToRect`/`zoomToBox`/`panBy` räknas bara mot ytan den har JUST NU: utan yta väntar de till nästa ritning med yta, också efter att ritytan ritats en gång (REQ9 6).

<!-- END:canvas -->

<!-- BEGIN:canvas-shapes | shape form former polyline polygon linje line rect circle path text marker points stroke paint gradient markerstyle streck dash halo maxlines ellips truncate minsize maxsize golv tak minzoom maxzoom -->
## Ritytans former och stilar (`canvas-shapes`)

**Alias:** shape, form, former, polyline, polygon, linje, line, rect, circle, path, text, marker, points, stroke, paint, gradient, markerstyle, streck, dash, halo, maxlines, ellips, truncate, minsize, maxsize, golv, tak, minzoom, maxzoom
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [shape.h](Glaze/widgets/include/glaze/canvas/shape.h), [path.h](Glaze/widgets/include/glaze/canvas/path.h), [canvas/CLAUDE.md](Glaze/widgets/include/glaze/canvas/CLAUDE.md)

En `glaze::Shape` är ett VÄRDE byggt med en fabrik; den kopieras in i lagret. Linjer har bredd, skarvar, ändar och streck; ytor har fyllning (färg eller gradient fast i världen), hål och valfri kontur; text och markörer står upprätta på skärmen och kolliderar. Samma former ritas omedelbart med `Painter::draw` (se `canvas-painter`). Formerna i rymden står i `canvas-3d` och `canvas-meshes`.

### API
- `static Shape line(Point a, Point b, Stroke stroke = {})` · `polyline(std::vector<Point> points, Stroke stroke = {})` · `arc(Point center, double r, double startDeg, double sweepDeg, Stroke stroke = {})`
- `static Shape polygon(std::vector<Point> outer, Paint fill, std::optional<Stroke> stroke = std::nullopt)` · `polygon(std::vector<std::vector<Point>> rings, Paint fill, std::optional<Stroke> stroke = std::nullopt)` — första ringen ytter, resten hål.
- `static Shape rect(Rect r, Paint fill, std::optional<Stroke> stroke = std::nullopt)` · `static Shape circle(Point center, double r, Paint fill, std::optional<Stroke> stroke = std::nullopt)` · `static Shape ellipse(Point center, double rx, double ry, Paint fill, std::optional<Stroke> stroke = std::nullopt)`
- `static Shape path(const Path& p, Paint fill, std::optional<Stroke> stroke = std::nullopt)` · `path(const Path& p, Stroke stroke)` — `Path` med `moveTo`/`lineTo`/`quadTo`/`cubicTo`/`close`.
- `static Shape text(Point at, TrText text, TextStyle style = {})` · `marker(Point at, MarkerStyle style = {})` · `points(std::vector<Point> points, MarkerStyle style = {})` · `image(Rect r, ImageSource source, float opacity = 1.0f)`
- `static Shape text(Point at, std::vector<TextSpan> spans, TextStyle style = {})` — en etikett i DELAR med var sin färg och fet/kursiv, i ett rit-anrop; radbrytningen går över delarna, och storleken, familjen, glorian, plattan och ankaret är etikettens. `text3` har samma överlagring.
- `static Shape image(Point topLeft, Point topRight, Point bottomRight, Point bottomLeft, ImageSource source, float opacity = 1.0f)` — bilden PROJEKTIVT på fyra fria hörn (ett snett flygfoto, en georefererad plåt), hörnen i ringordning; se `canvas-images`.
- `TextSpan` i fältordning: `text` (`TrText`), `color`, `bold`, `italic`, `background`, `tabStop` — alla utom texten `std::optional`; osatt `color`/`bold`/`italic` ärver etikettens `TextStyle`, osatt `background`/`tabStop` är inget.
- DELENS BAKGRUND OCH TABBSTOPP (REQ8 16): `background` (`gfx::Color`) är en ruta bakom delen på varje rad den står på, radhöjden hög (följande delar i samma färg blir en ruta). `tabStop` (`Length`) är x där delen BÖRJAR, från radens början (med `Align::Left` textens vänsterkant) — står texten före redan längre fram börjar delen direkt efter den, och efter en radbrytning blir stoppet ett indrag. Radbrytningen, bredden och `measureText` räknar med luckan.
- `Paint`: implicit ur `gfx::Color` · `static Paint linear(Point from, Point to, std::vector<GradientStop> stops)` · `static Paint radial(Point center, double r0, double r1, std::vector<GradientStop> stops)`
- `Stroke` i fältordning: `paint`, `width` (`Length`, förval 1 px), `cap` (`LineCap::Butt/Round/Square`), `join` (`LineJoin::Round/Miter/Bevel`), `miterLimit`, `dash`, `dashUnit`, `dashOffset`.
- `TextStyle` i fältordning: `size`, `family`, `bold`, `italic`, `color`, `halo`, `haloWidth`, `background`, `padding`, `anchor`, `angle`, `rotation` (`Alignment::Viewport/Map/Ground`), `offsetX`, `offsetY`, `wrapWidth`, `align`, `collide`, `priority`, `collisionPadding`, `maxLines`, `maxHeight`, `minSize`, `maxSize`, `minZoom`, `maxZoom`, `outline`, `outlineWidth`, `lineSpacing`.
- PLATTANS KONTUR (REQ8 15): `gfx::Color outline` (alfa 0 = ingen) och `Length outlineWidth` (förval 1 px) — ett band UTANFÖR plattan (`background` + `padding`), som markörens kontur; utan bakgrund blir det en ram. Lådan växer med bandet, så kollisionen och träffprovet räknar med det.
- RADAVSTÅNDET (REQ8 15): `float lineSpacing` — baslinje till baslinje är radhöjden × faktorn (förval 1; ≤ 0 räknas som 1), och textblocket är en radhöjd plus ett radavstånd per rad efter den första. Taket (`maxHeight`), plattan och `measureText` (`height`) räknar med det.
- TAKET (REQ3 7): `int maxLines` och `Length maxHeight` (textblocket utan plattans luft; 0 = inget tak, minst en rad står alltid, det strängaste vinner). Det som inte ryms kortas med "…" efter `Draw::elideLines` regel — den sista raden bär resten av sitt stycke, blanksteg närmast ellipsen bort — i den sista synliga delens färg och stil; `measureText` svarar med `truncated`.
- GOLVET OCH TAKET (REQ3 8): `Length minSize`/`maxSize` — en em på SKÄRMEN hålls i [min, max] (0 = ingen gräns, taket vinner), i px, mm eller world. Hela etiketten följer (gloria, platta, förskjutning) vid varje kameraändring utan ombyggnad; kollisionen, träffprovet och `measureText` räknar med den.
- SYNLIGHETEN EFTER ZOOM (REQ3 8): `double minZoom`/`maxZoom` — etiketten syns bara när ritytans zoom ligger i intervallet (`setLayerZoomRange`s regel, 0 = obegränsat); dold tar den ingen plats, träffas inte och tonar med `setLabelFade`.
- `MarkerStyle` i fältordning: `shape` (`MarkerShape::Circle/Square`), `size`, `fill`, `outline`, `outlineWidth`, `angle`, `rotation`, `anchor`, `img`, `collide` (förval av), `priority`, `collisionPadding`, `minSize`, `maxSize`, `minZoom`, `maxZoom` — de fyra sista som textens.

### Exempel
```cpp
// karta är en glaze::Canvas utan kamera: världen = ritytans logiska px
const int ytor   = karta.addLayer("Ytor");
const int linjer = karta.addLayer("Linjer");
const int text   = karta.addLayer("Text");
karta.setLayerDrawOrder(ytor, glaze::DrawOrder::ByKind);   // få rit-anrop

// En yta med HÅL och gradient; konturen är valfri (std::nullopt = ingen).
karta.addShape(ytor, glaze::Shape::polygon(
    std::vector<std::vector<glaze::Point>>{
        {{10, 10}, {110, 10}, {110, 70}, {10, 70}},   // ytterringen
        {{50, 30}, {70, 30}, {70, 50}}},              // hålet
    glaze::Paint::linear({10, 10}, {110, 10},
                         {{0, gfx::Color{205, 232, 176}},
                          {1, gfx::Color{150, 200, 120}}}),
    glaze::Stroke{.paint = gfx::Color{96, 140, 72}, .width = 1.5f,
                  .dash = {6, 3}}));
karta.addShape(ytor, glaze::Shape::circle({160, 40}, 20, gfx::Color{52, 120, 186}));
karta.addShape(ytor, glaze::Shape::rect({200, 20, 40, 30}, gfx::Color{217, 201, 182},
                                        glaze::Stroke{.paint = gfx::Color{156, 138, 116}}));

// Linjer: bredd i px, mm eller world, skarvar, ändar och streck.
karta.addShape(linjer, glaze::Shape::polyline({{10, 100}, {120, 110}, {240, 90}},
    {.paint = gfx::Color{120, 112, 98}, .width = glaze::mm(1.3f),
     .cap = glaze::LineCap::Round, .join = glaze::LineJoin::Miter}));
karta.addShape(linjer, glaze::Shape::arc({160, 40}, 30, 0, 90, {.width = 2}));

// Text och markörer står upprätta på skärmen och kolliderar.
karta.addShape(text, glaze::Shape::text({60, 40}, "Parken",
    {.size = glaze::mm(2.6f), .italic = true, .color = gfx::Color{46, 107, 31},
     .halo = gfx::Color{255, 255, 255}, .priority = 5}));
karta.addShape(text, glaze::Shape::marker({160, 40},
    {.size = glaze::mm(2.4f), .fill = gfx::Color{2, 136, 209},
     .outline = gfx::Color{255, 255, 255}, .outlineWidth = glaze::mm(0.4f)}));
karta.addShape(text, glaze::Shape::points({{20, 130}, {30, 135}, {40, 128}},
                                          {.size = 6}));   // ETT anrop

// En etikett i DELAR: numret fetstilt och rött, samma gloria och kollision.
karta.addShape(text, glaze::Shape::text({200, 70},
    {glaze::TextSpan{.text = "Storgatan "},
     {.text = "12", .color = gfx::Color{200, 0, 0}, .bold = true}},
    {.size = glaze::mm(2.2f), .halo = gfx::Color{255, 255, 255}}));

karta.setShape(text, 0, glaze::Shape::text({60, 40}, "Ekparken"));   // ERSÄTTER
karta.setLayerVisible(linjer, false);

// TAK, GOLV OCH ZOOM: en anteckning som kortas med "…", ett namn i
// världsenheter som stannar mellan 2 och 6 mm, och ett namn som syns först
// när kartan zoomats in.
karta.addShape(text, glaze::Shape::text({20, 160}, "Grävtillstånd: ledningsarbete hela hösten …",
    {.size = glaze::world(6.5f), .anchor = glaze::TextAnchor::TopLeft,
     .wrapWidth = glaze::world(100), .align = glaze::Align::Left,
     .collide = false, .maxHeight = glaze::world(32)}));
karta.addShape(text, glaze::Shape::text({160, 90}, "Kyrkbacken",
    {.size = glaze::world(14), .minSize = glaze::mm(2), .maxSize = glaze::mm(6)}));
karta.addShape(text, glaze::Shape::text({200, 40}, "Skolan",
    {.size = glaze::mm(1.6f), .minZoom = 2.0}));

// EN INFORUTA: platta med ram och luftigare rader, och kolumner med färgade
// fält — värdena börjar vid 18 mm oavsett hur långa namnen är.
karta.addShape(text, glaze::Shape::text({240, 120},
    {glaze::TextSpan{.text = "Stolpe"},
     {.text = "12,4 m\n", .background = gfx::Color{255, 230, 150},
      .tabStop = glaze::mm(18)},
     {.text = "Material"},
     {.text = "Trä", .tabStop = glaze::mm(18)}},
    {.size = glaze::mm(2), .background = gfx::Color{255, 255, 255},
     .padding = glaze::mm(1), .anchor = glaze::TextAnchor::TopLeft,
     .align = glaze::Align::Left, .outline = gfx::Color{60, 60, 60},
     .outlineWidth = glaze::px(1), .lineSpacing = 1.3f}));
```

### .glazeui
```cpp
karta.addLayer("Linjer");
karta.addLayer("Ytor");
karta.addShape(2, glaze::Shape::polygon({{{0, 0}, {40, 0}, {40, 40}, {0, 40}}, {{10, 10}, {20, 10}, {20, 20}}}, glaze::Paint::linear({0, 0}, {40, 0}, {{0, gfx::Color{255, 0, 0}}, {1, gfx::Color{0, 0, 255}}}), std::nullopt));
karta.addShape(1, glaze::Shape::polyline({{0, 80}, {120, 90}}, glaze::Stroke{.paint = gfx::Color{200, 60, 40}, .width = glaze::mm(0.7), .join = glaze::LineJoin::Miter, .dash = {4, 2}, .dashUnit = glaze::Unit::Mm}));
karta.addShape(glaze::Shape::text({60, 40}, "Parken", glaze::TextStyle{.size = glaze::mm(2.5), .halo = gfx::Color{255, 255, 255}, .anchor = glaze::TextAnchor::Bottom}));
karta.addShape(2, glaze::Shape::path(glaze::Path{{glaze::PathVerb::Move, {0, 0}}, {glaze::PathVerb::Line, {40, 0}}, {glaze::PathVerb::Close}}, gfx::Color{0, 0, 0}));
karta.addShape(glaze::Shape::text({60, 60}, {glaze::TextSpan{.text = "Storgatan "}, {.text = "12", .color = gfx::Color{200, 0, 0}, .bold = true}}, glaze::TextStyle{.size = glaze::mm(2.5)}));
karta.addShape(glaze::Shape::image({0, 0}, {100, 10}, {90, 90}, {10, 80}, glaze::img("kartor", "flygfoto"), {.opacity = 0.6}));
karta.addShape(glaze::Shape::text({674000, 6580070}, "Kyrkbacken", glaze::TextStyle{.size = glaze::world(14), .minSize = glaze::mm(2), .maxSize = glaze::mm(6), .minZoom = 0.5, .maxZoom = 20}));
karta.addShape(glaze::Shape::marker({674100, 6580050}, glaze::MarkerStyle{.size = glaze::world(3), .minSize = glaze::px(4), .maxSize = glaze::px(24), .minZoom = 1.5}));
karta.addShape(glaze::Shape::text({240, 120}, {glaze::TextSpan{.text = "Stolpe"}, {.text = "12,4 m", .background = gfx::Color{255, 230, 150}, .tabStop = glaze::mm(18)}}, glaze::TextStyle{.size = glaze::mm(2), .background = gfx::Color{255, 255, 255}, .padding = glaze::mm(1), .outline = gfx::Color{60, 60, 60}, .outlineWidth = glaze::px(1), .lineSpacing = 1.3}));
```
`Path` skrivs som segmentlista i `.glazeui` (kedjan går inte att emittera); designerade initierare måste stå i fältordning. Delarna skrivs `glaze::TextSpan{…}` eller som bara `{…}` med designerare eller positionellt (`{"Ek", gfx::Color{30, 95, 150}}`).

### Fallgropar
- En ytas valfria kontur är `glaze::Stroke{…}` eller `std::nullopt` — en bar klammerlista kompilerar inte till `std::optional`.
- Polygon med hål i koden: skriv typen `std::vector<std::vector<glaze::Point>>{…}` så att rätt överlagring väljs.
- Strecken förkortas med djupet under tilt; högst 8 längder i `dash`.
- Markörer kolliderar inte från start (`collide = false`), text gör det.
- `Shape::text(p, {}, …)` och en lista med EN bar del, `{{"abc"}}`, är TVETYDIGA mellan texten och delarna och kompilerar inte — `{"abc"}` är text, `{glaze::TextSpan{"abc"}}` eller `{{.text = "abc"}}` är delar. Previewn bevarar en tvetydig rad opakt i stället för att gissa.
- Ett tecken UI-fonten saknar (−, ≈, kinesiska …) ritas med en reservfont ur OS:et — men en tom ruta eller ersättningstecknet betyder att ingen font på maskinen har det (`gfx::Font::hasGlyph`, se `gfx`).
- GOLVET SKALAR RUTAN: radantalet och brytningen avgörs när etiketten byggs, och golvet och taket skalar hela etiketten — en världstext vid golvet blir större än sin ruta i världen. `maxHeight` i px för världstext räknas om vid byggets zoom, som `wrapWidth`.
- Synligheten efter zoom gäller text och markörer; linjer, ytor och bilder har lagrets `setLayerZoomRange`. Liggande text på terräng (draperingen) följer inte zoomintervallet, och golvet där räknas i draperingens px.

<!-- END:canvas-shapes -->

<!-- BEGIN:canvas-layers | layer layers lager lagerordning addlayer setlayers movelayer flytta-lager bring-forward send-backward addshape setshape setshapes removeshape draworder setlayerdraworder bykind strict opacity zoomrange collision kollision labelfade setlayerpainter målarens-plats onpaintlayer -->
## Ritytans lager och formlista (`canvas-layers`)

**Alias:** layer, layers, lager, lagerordning, addlayer, setlayers, movelayer, flytta-lager, bring-forward, send-backward, addshape, setshape, setshapes, removeshape, draworder, setlayerdraworder, bykind, strict, opacity, zoomrange, collision, kollision, labelfade, setlayerpainter, målarens-plats, onpaintlayer
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [canvas.h](Glaze/widgets/include/glaze/widgets/canvas.h), [canvas/CLAUDE.md](Glaze/widgets/include/glaze/canvas/CLAUDE.md)

Lagren och formerna följer kollektionsvokabulären (se `collections`): `addX`/`setX(index, …)`/`setXs(lista)` plus `removeX`. Lager 0 finns från start (namnlöst), och en form utan lagerindex hamnar där. Lagren ritas i indexordning (sist överst); varje lager har synlighet, opacitet, zoomintervall, etikettkollision och ritordning. `moveLayer` byter lagrens ordning utan att något byggs om eller laddas upp — bara ritordningen byts.

### API
- `int addLayer(TrText name)` · `void setLayer(int layer, TrText name)` (ERSÄTTER: nytt namn, förvalda egenskaper, inga former) · `void setLayers(std::vector<TrText> names)` · `void removeLayer(int layer)` · `int layerCount() const` · `const std::string& layerName(int layer) const`
- `bool moveLayer(int from, int to)` — flytta lagret `from` till platsen `to`, räknad i ordningen FÖRE flytten och i [0, `layerCount()`]: lagret läggs in UNDER det som stod på `to`, och `layerCount()` lägger det överst (`TreeView::moveNode`s konvention). Ett steg upp är `moveLayer(i, i + 2)`, ett steg ned `moveLayer(i, i - 1)`. `false` + logg utanför; till samma plats `true` utan omritning.
- `int addShape(Shape s)` · `int addShape(int layer, Shape s)` — svarar med indexet i lagret (`-1` utan lager).
- `void setShape(int index, Shape s)` · `void setShape(int layer, int index, Shape s)` · `void setShapes(std::vector<Shape> shapes)` · `void setShapes(int layer, std::vector<Shape> shapes)`
- `void removeShape(int index)` · `void removeShape(int layer, int index)` · `int shapeCount(int layer = 0) const` · `const Shape* shape(int layer, int index) const`
- `void setLayerVisible(int layer, bool visible)` · `void setLayerOpacity(int layer, float opacity)` (0–1) · `void setLayerZoomRange(int layer, double minZoom, double maxZoom)` (0 = obegränsat) — och PER ETIKETT och markör samma regel i stilen, `TextStyle::minZoom`/`maxZoom` (REQ3 8, `canvas-shapes`): etiketten ligger kvar i lagret och döljs genom synlighetsbilden, så den tonar och tar ingen plats.
- `void setLayerCollision(int layer, bool on)` · `void setLabelFade(bool on)` — etiketterna tonar i stället för att växla.
- `enum class DrawOrder : std::uint8_t { Strict, ByKind }` · `void setLayerDrawOrder(int layer, DrawOrder order)` · `DrawOrder layerDrawOrder(int layer) const` — ordningen INOM lagret. `Strict` (förval): index = målarordning; `ByKind`: bilder, ytor, linjer, text var för sig — få rit-anrop.
- `void setLayerPainter(int layer, bool on)` · `bool layerPainter(int layer) const` — MÅLARENS PLATS (REQ8 2): ritytans `onPaintLayer` ritas i lagret, efter dess egna former, i stället för ovanpå allt — det som ritas om varje bildruta (ett rutnät, en skala) kan ligga UNDER ett tungt lager som ligger still. Lagrets synlighet, opacitet och zoomintervall gäller målaren, och i 3D-målet lagrets djup (se `canvas-painter`). Av från start; följer lagret i `moveLayer`, och `setLayer`/`setLayers` släcker den.

### Exempel
```cpp
// karta är en glaze::Canvas
karta.setLayers({"Plattor", "Vägar", "Etiketter"});   // lager 0, 1, 2 — ersätter alla
karta.setLayerDrawOrder(1, glaze::DrawOrder::ByKind); // ritas per slag: få anrop
karta.moveLayer(0, karta.layerCount());               // plattorna överst: Vägar, Etiketter, Plattor
karta.moveLayer(2, 0);                                // och tillbaka underst
karta.setLayerZoomRange(2, 0.5, 0);                    // bara vid zoom ≥ 0,5
karta.setLayerOpacity(0, 0.8f);
karta.setLayerCollision(2, false);                    // etiketterna alltid synliga

const int vag = karta.addShape(1, glaze::Shape::line({0, 0}, {100, 0}));   // index
karta.setShape(1, vag, glaze::Shape::line({0, 0}, {120, 0}));             // ERSÄTT
karta.setShapes(2, {glaze::Shape::text({50, 0}, "Väg 1"),
                    glaze::Shape::text({50, 20}, "Väg 2")});
karta.addShape(glaze::Shape::circle({0, 0}, 5, gfx::Color{255, 0, 0}));   // lager 0
karta.removeShape(1, vag);

if (const glaze::Shape* s = karta.shape(2, 0); s && s->kind() == glaze::Shape::Kind::Text)
    setTitle(karta.layerName(2) + ": " + std::to_string(karta.shapeCount(2)));
```

### .glazeui
```cpp
karta.setLayers({"Plattor", "Vägar", glaze::tr(2)});
karta.setLayer(2, "Hus");
karta.setLayerVisible(2, true);
karta.setLayerOpacity(1, 0.8);
karta.setLayerZoomRange(1, 0.5, 8);
karta.setLayerCollision(2, false);
karta.setLayerDrawOrder(1, glaze::DrawOrder::ByKind);
karta.setLayerPainter(0, true);
karta.addShape(1, glaze::Shape::line({0, 0}, {100, 0}));
karta.addShape(1, glaze::Shape::line({0, 10}, {100, 10}));
karta.setShape(1, 0, glaze::Shape::marker({50, 0}));
karta.removeShape(1, 1);
karta.moveLayer(0, 3);
```
`moveLayer` och `setLayerPainter` är TILLSTÅND och utförs i previewn (handlingarna som `easeTo` gör det inte); det `onPaintLayer` ritar kör previewn aldrig.

### Fallgropar
- Index utanför ⇒ `gfx::log` och ingenting händer — listan växer aldrig tyst.
- `Strict` med tusentals former som byter slag (yta + kontur, väg + namn) blir tusentals rit-anrop; ett stort kartlager vill ha `ByKind`.
- Lagrets placering i 3D (`setLayerPlacement`) står i `canvas-3d`.
- **Lagerindexen är POSITIONER.** Efter `moveLayer` har lagren nya index — i `HitResult::layer`, `setLayerViews(i, …)`, `PaintArgs::layer` i `onPaintLayer` och för formerna utan lagerindex, som alltid går till plats 0. En app som visar lagren i en lista håller sin egen spegling plats → lager.
- `moveLayer(i, i + 1)` står kvar: platsen räknas FÖRE flytten, så det är samma plats.
- Etiketterna placeras om efter en flytt UTAN toning (också med `setLabelFade(true)`), och synlighetsbilden laddas upp hel — en byte per etikett; med terräng ritas draperingarna om (GPU-arbete, ingen data). Anropa den inte i `onPaint`.

<!-- END:canvas-layers -->

<!-- BEGIN:canvas-camera | camera kamera kartkamera center zoom bearing bäring vridning pitch tilt lutning pan panorera gester gestures easeto zoomtorect mapscale skala oncamerachange onresize synligt-utsnitt visible-extent -->
## Kartkameran och gesterna (`canvas-camera`)

**Alias:** camera, kamera, kartkamera, center, zoom, bearing, bäring, vridning, pitch, tilt, lutning, pan, panorera, gester, gestures, easeto, zoomtorect, mapscale, skala, oncamerachange, onresize, synligt-utsnitt, visible-extent
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [camera.h](Glaze/widgets/include/glaze/canvas/camera.h), [canvas.h](Glaze/widgets/include/glaze/widgets/canvas.h)

Kartkameran är `glaze::Camera {center, zoom, bearing, pitch}` — zoom i logiska px per världsenhet, vinklarna i grader. Första sättaren fryser den härledda kameran (lokala px) till en riktig. Gesterna är AV från start; påslagna följer de MapLibre: dra (vänster/mitten) panorerar, hjulet zoomar mot pekaren, Ctrl+drag vrider (vågrätt) och tiltar (lodrätt), pilarna panorerar, Shift+pilar vrider/tiltar, `+`/`−` zoomar; styrplattans två fingrar panorerar, nyper och vrider. Det är FÖRVALET — vilken rörelse som gör vad, åt vilket håll och hur fort är en bindningstabell som appen ändrar globalt eller per rityta (`input`).

### API
- `struct Camera { Point center{}; double zoom = 1.0; float bearing = 0.0f; float pitch = 0.0f; }` · `void setCamera(const Camera& c)` · `Camera camera() const` · `bool hasCamera() const`
- `void setCenter(double x, double y)` · `void setZoom(double zoom)` · `void setZoomRange(double minZoom, double maxZoom)` · `void setBearing(float deg)` · `void setPitch(float deg)` · `void setMaxPitch(float deg)` (förval 60, tak 70) · `void setYUp(bool on)`
- `void easeTo(const Camera& target, int ms)` · `bool isEasing() const` · `void zoomToRect(const Rect& r, Length pad = {})`
- `void setMapScale(double denominator, double metersPerUnit = 1.0)` · `double mapScale() const` · `bool mapScaleIsExact() const`
- `std::optional<Point> toWorld(Point local) const` · `std::optional<Point> toLocal(Point world) const` · `std::optional<double> unitsPerPixelAt(Point local) const`
- `void setPannable(bool on)` · `setZoomable` · `setRotatable` · `setPitchable` · `void setInertia(bool on)` (förval på)
- `void setRotationSnap(float stepDeg)` (0 = av, 360 = bara norr — förval, 90 = kvartsvarv) · `void setRotationSnapTolerance(float deg)` (förval 7°)
- `Handler<ChangeArgs> onCameraChange` — bara ANVÄNDARENS rörelser.
- `Handler<ResizeArgs> onResize` — basens (`events`, REQ3 4): ritytans STORLEK ändrades, i logiska px ur `actualSize()`. Det SYNLIGA UTSNITTET är kameran plus storleken, så en app som laddar plattor för det som syns lyssnar på båda: `onResize` för ytan och `onCameraChange` för användarens rörelser. Utsnittet räknas med `toWorld` på hörnen (`toLocal` åt andra hållet), och under tilt med `visibleRegion` (`canvas-3d`).

### Exempel
```cpp
// karta är en glaze::Canvas i ett glaze::AppWindow
karta.setYUp(true);                            // FÖRE bilderna
karta.setCamera(glaze::Camera{.center = {674100, 6580050}, .zoom = 2.5,
                              .bearing = 15, .pitch = 30});
karta.setZoomRange(0.2, 40);                   // 0 = obegränsat
karta.setMaxPitch(50);                         // förval 60°, taket 70°
karta.setPannable(true);
karta.setZoomable(true);
karta.setRotatable(true);                      // Ctrl+drag vrider …
karta.setPitchable(true);                      // … och tiltar lodrätt
karta.setRotationSnap(90);                     // 0 = av, 360 = bara norr
karta.onCameraChange = [this](glaze::Widget* s, glaze::ChangeArgs&) {
    auto* k = static_cast<glaze::Canvas*>(s);  // bara användarens rörelser
    setTitle("1:" + std::to_string(std::lround(k->mapScale())));
};

karta.easeTo(glaze::Camera{.center = {674000, 6580000}, .zoom = 4}, 400);
karta.zoomToRect({673900, 6579950, 256, 256}, glaze::mm(5));   // med luft

// Det SYNLIGA UTSNITTET: samma funktion för ytans storlek och kamerans rörelser.
auto utsnitt = [this] {
    const gfx::Sizef yta = karta.actualSize();              // logiska px
    auto hornA = karta.toWorld({0, 0});                      // övre vänstra hörnet
    auto hornB = karta.toWorld({yta.w, yta.h});              // nedre högra
    if (hornA && hornB)
        gfx::log("syns: %.0f,%.0f – %.0f,%.0f", hornA->x, hornA->y, hornB->x, hornB->y);
};
karta.onResize       = utsnitt;     // efter ritningen, i pumpens svans
karta.onCameraChange = utsnitt;     // bara användarens rörelser
```

### .glazeui
```cpp
karta.setYUp(true);
karta.setCamera(glaze::Camera{.center = {674100, 6580050}, .zoom = 2.5, .bearing = 15, .pitch = 30});
karta.setZoomRange(0.5, 40);
karta.setMapScale(2000, 1);
karta.zoomToRect(glaze::Rect{674000, 6579950, 256, 256}, glaze::mm(5));
karta.setRotatable(true);
karta.setRotationSnap(90);
karta.onCameraChange = [this](glaze::Widget* s, glaze::ChangeArgs& e) { kartaFlyttad(s, e); };
karta.onResize = [this](glaze::Widget* s, glaze::ResizeArgs& e) { kartaStorlek(s, e); };
```
`easeTo` är en handling och bevaras opakt i `.glazeui`.

### Fallgropar
- Sättarna och `easeTo` fyrar ALDRIG `onCameraChange`; en sättare mitt i en glidning avbryter den.
- `setMapScale` och `zoomToRect` före första ritningen väntar tills ytan och skärmens mm är kända.
- `mapScale()` är skalan i MITTEN; under tilt varierar den — en skalstock frågar `unitsPerPixelAt(lokal)` där den står.
- Bäringen vrider innehållet MOTURS; pitch 0 = rakt uppifrån.
- Pekaren följer gesten: hårkorset över en yta med gester, den STÄNGDA HANDEN medan den panoreras och fyrpilen medan den vrids (`cursors`).
- Ingen av de två händelserna täcker ensam utsnittet: `onCameraChange` fyras inte för en ny storlek, och `onResize` inte för en kamerarörelse — och appens EGNA sättare (`setCamera`, `easeTo`, `zoomToRect`) fyrar ingen av dem, så läs utsnittet där appen ändrar kameran. `onResize` kommer efter bildrutan där storleken ändrades (den ritas med de gamla plattorna) och, under ett fönsterdrag på Windows och macOS, först vid släppet.
- Efter `onCameraChange` från ett drag, ett hjulsteg, en styrplattegest, en tröghet eller ett snäpp — och efter `onGesture` — ritas bara RITYTAN om (lagren, `onPaint` och dess 3D-mål), inte resten av fönstret, och en annan rityta i samma fönster ritar inte om sitt 3D-mål. Det handlern ändrar i en annan widget gör den med dess sättare (`setTitle`, `Label::setText` — de ritas om); ett eget fält som en ANNAN ritytas `onPaint` läser kräver `annanYta.invalidate()`. En TANGENT som flyttar kameran (pilarna, + och −) är en diskret händelse och ritar om hela fönstret.

<!-- END:canvas-camera -->

<!-- BEGIN:canvas-units | units enheter length px pixel mm millimeter world världsenheter linjal ruler kalibrering calibration dpi displaymetrics pxpermm skärmmått edid -->
## Mått på ritytan: px, mm och world (`canvas-units`)

**Alias:** units, enheter, length, px, pixel, mm, millimeter, world, världsenheter, linjal, ruler, kalibrering, calibration, dpi, displaymetrics, pxpermm, skärmmått, edid
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [geometry.h](Glaze/widgets/include/glaze/canvas/geometry.h), [canvas.h](Glaze/widgets/include/glaze/widgets/canvas.h), [app.h](Glaze/widgets/include/glaze/app.h)

Varje mått på ritytan är en `glaze::Length` med enhet. Ett NAKET TAL är logiska px (följer OS- och användarskalan); `glaze::mm(x)` är millimeter på skärmen med linjal, ur skärmens EDID och appens kalibrering (oberoende av skalan); `glaze::world(x)` följer zoomen (3 m i terrängen). Linjebredd, text, markörer, streck och luft tar alla tre.

### API
- `Length` — `value` + `unit`; `constexpr Length(float v)` är implicit (= px) · `constexpr Length px(float v)` · `constexpr Length mm(float v)` · `constexpr Length world(float v)` · `enum class Unit : std::uint8_t { Px, Mm, World }`
- `gfx::DisplayMetrics display() const` (ritytan) / `const gfx::DisplayMetrics& AppWindow::display() const` — `pxPerMm` (FYSISKA px per mm), `source`, `widthMm`, `heightMm`, `id`.
- `enum class DisplaySource : std::uint8_t { Assumed, Measured, Calibrated }` (i `gfx::`) — `Assumed` = 96 dpi i FYSISKA px (`gfx::kAssumedPxPerMm`, 3,78 px/mm), oberoende av OS- och användarskalan.
- `void App::setDisplayCalibration(const std::string& id, float pxPerMm)` — appens egen mätning per skärm-id; 0 tar bort.
- `bool mapScaleIsExact() const` — mätt eller kalibrerad skärm; annars visa "≈1:2000".

### Exempel
```cpp
// karta är en glaze::Canvas med kamera i meter (se canvas-camera)
const std::vector<glaze::Point> spar = {{674000, 6580000}, {674100, 6580040}};
karta.addShape(glaze::Shape::polyline(spar, {.width = 2}));                // 2 px
karta.addShape(glaze::Shape::polyline(spar, {.width = glaze::mm(0.7f)}));  // 0,7 mm
karta.addShape(glaze::Shape::polyline(spar, {.width = glaze::world(3)}));  // 3 m
karta.addShape(glaze::Shape::text(spar[0], "Spår",
    {.size = glaze::mm(2.5f), .background = gfx::Color{255, 255, 255},
     .padding = 4}));                          // mått FÅR blandas

const gfx::DisplayMetrics d = karta.display();    // skärmens px per mm
setTitle(d.source == gfx::DisplaySource::Assumed ? "Antagen skala (96 dpi)"
                                                 : "Mätt skärm");
if (!d.id.empty())   // appens linjalmätning: 100 mm = 378 fysiska px
    glaze::app().setDisplayCalibration(d.id, 3.78f);
```

### .glazeui
```cpp
karta.addLayer("Spår");
karta.addShape(1, glaze::Shape::polyline({{674000, 6580000}, {674100, 6580040}}, {.width = glaze::mm(0.7)}));
karta.addShape(1, glaze::Shape::arc({674050, 6580050}, 20, 0, 90, glaze::Stroke{gfx::Color{0, 128, 0}, glaze::world(2)}));
karta.zoomToRect(glaze::Rect{674000, 6579950, 256, 256}, glaze::mm(5));
```
Kalibreringen är ingen ritytegenskap utan appens (`glaze::app()`), och hör hemma i kod.

### Fallgropar
- mm i logiska px är `mm * pxPerMm / uiScale` — `pxPerMm` talar FYSISKA px.
- En ANTAGEN skärm (ingen EDID — en VM, RDP, en docka som skriver om EDID) räknas som 96 dpi i fysiska px vid varje skala, så en millimeter ändras inte med skalningsvalet; på en tät panel blir den antagna millimetern för liten. Visa "≈1:N" (`mapScaleIsExact()`) och låt användaren kalibrera.
- Kalibreringen är app-global per skärm-id och APPEN äger lagringen: spara talet och sätt det igen vid start. Värden utanför 50–800 dpi avvisas och loggas.
- Mått i en `TextStyle` får blandas (text i mm, luft i px); de räknas om till storlekens enhet när etiketten byggs.

<!-- END:canvas-units -->

<!-- BEGIN:canvas-painter | painter målare onpaint onpaintlayer onpaintoverlay paintargs hud overlay gummiband rubberband omedelbar immediate drawtext drawline drawpolyline invalidateoverlay screenspace measuretext textmetrics textmått textmätning heightforwidth setlayerpainter målarens-plats under-lager klipp klippning clip clipping cliprect save-rect -->
## Omedelbar ritning: onPaint, onPaintLayer, onPaintOverlay och Painter (`canvas-painter`)

**Alias:** painter, målare, onpaint, onpaintlayer, onpaintoverlay, paintargs, hud, overlay, gummiband, rubberband, omedelbar, immediate, drawtext, drawline, drawpolyline, invalidateoverlay, screenspace, measuretext, textmetrics, textmått, textmätning, heightforwidth, setlayerpainter, målarens-plats, under-lager, klipp, klippning, clip, clipping, cliprect, save-rect
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [painter.h](Glaze/widgets/include/glaze/canvas/painter.h), [canvas.h](Glaze/widgets/include/glaze/widgets/canvas.h)

Det som ändras varje ritning ritas i handlers med en `glaze::Painter` (i `e.painter`) och samma former som lagren. `onPaint` ritar i VÄRLDEN, ovanpå lagren; `onPaintLayer` ritar i världen PÅ EN VALD PLATS I LAGERORDNINGEN — i varje lager med `setLayerPainter`, efter lagrets egna former (ett rutnät som följer vyn UNDER ett tungt punktmoln); `onPaintOverlay` ritar i ritytans lokala logiska px i fönstrets overlay-lager — en HUD eller ett gummiband som uppdateras med `invalidateOverlay()` utan att lagren ritas om. Det som ligger still hör hemma i lagren.

### API
- `PaintArgs` — `e.painter` (`Painter&`), giltig under anropet, och `e.layer`: lagrets plats i `onPaintLayer` (efter en `moveLayer` den nya), −1 i `onPaint` och `onPaintOverlay`.
- `Handler<PaintArgs> Canvas::onPaintLayer` · `void Canvas::setLayerPainter(int layer, bool on)` · `bool Canvas::layerPainter(int layer) const` — MÅLARENS PLATS (REQ8 2): handlern körs EN gång per lager med platsen, i lagrens ordning. Lagrets synlighet, opacitet (tonar målarens former) och zoomintervall gäller den, och den hör till huvudvyn och `renderToBitmap`, som `onPaint`. I 3D-målet följer den LAGRETS DJUP: i ett Ground-lager (också ett Auto-lager — målarens former räknas inte) ritas den på marken utan djup i lagrens ordning, i ett World- eller Terrain-lager med lagrets renderläge som målarens start (`renderState()`), djuptestad som lagrets former.
- `void draw(const Shape& s)` · `void drawLine(Point a, Point b, const Stroke& stroke = {})` · `void drawPolyline(const std::vector<Point>& points, const Stroke& stroke = {})`
- `void fillPolygon(const std::vector<Point>& outer, const Paint& fill, std::optional<Stroke> stroke = std::nullopt)` · `void fillRect(const Rect& r, const Paint& fill)` · `void drawRect(const Rect& r, const Stroke& stroke = {})` · `void fillCircle(Point center, double r, const Paint& fill)`
- `void drawText(Point at, const TrText& text, const TextStyle& style = {})` · `void drawMarker(Point at, const MarkerStyle& style = {})` · `void drawImage(const Rect& r, const ImageSource& source, const ImageStyle& style = {})` — opaciteten och FÄRGJUSTERINGEN (kontrast, ljushet, mättnad) som `Shape::image` (se `canvas-images`), gånger målarens ton
- `void drawText(Point at, const std::vector<TextSpan>& spans, const TextStyle& style = {})` — en etikett i DELAR (se `canvas-shapes`) · `void drawImage(Point topLeft, Point topRight, Point bottomRight, Point bottomLeft, const ImageSource& source, const ImageStyle& style = {})` — bilden på fyra fria hörn, hörnen genom målarens transform.
- `[[nodiscard]] Saved save()` · `void translate(double dx, double dy)` · `void rotate(double deg)` · `void scale(double sx, double sy)` · `void screenSpace()`
- `[[nodiscard]] Saved save(const Rect& clip)` — KLIPPET (REQ8 1): samma sparade läge, och allt som ritas till lägets slut klipps till rektangeln i målarens transform och rymd — vriden en fyrhörning, under tilt eller med en 3D-kamera dess bild på skärmen. Nästlat är det snittet med det som redan gäller (det växer aldrig), och lägets slut lägger tillbaka det förra. Linjer, ytor, text, markörer och bilder — också i rymden — klipps i varje pixel och kantutjämnas; det som ligger utanför ritas inte och skriver inget djup. Föremålen i rymden (meshar, instanser, punkter) klipps till klippets omskrivna rektangel på skärmen. Högst 8 kanter (två godtyckligt vridna rektanglar ryms); en rektangel utan yta klipper bort allt.
- `gfx::Sizef measureText(const TrText& text, const TextStyle& style = {})` · `gfx::Sizef measureText(const std::vector<TextSpan>& spans, const TextStyle& style = {})` (logiska px — delarna med sina egna stilar, samma låda som lagrets etikett; inget laddas upp) · `gfx::Sizef size() const`
- `std::optional<TextMetrics> Canvas::measureText(const TrText& text, const TextStyle& style = {}, Unit unit = Unit::Px) const` · `… measureText(const std::vector<TextSpan>& spans, …) const` — SAMMA mätning UTANFÖR en ritning (i en handler, i `onLoad`), i px, mm eller världsenheter; `nullopt` utan öppet fönster. `struct TextMetrics { double width, height, lineHeight, baseline; int lines; bool truncated; }` — textblocket utan platta, luft och ankare; `baseline` är första radens under överkanten, en tom text är en rad. TAKET (`maxLines`/`maxHeight`) räknas med: `lines` är de synliga raderna och `truncated` att texten kortades med "…" (visa hela i en tooltip). Golvet och taket för storleken (`minSize`/`maxSize`) räknas vid kamerans mitt; zoomintervallet inte — måttet säger hur texten ser ut, inte om den syns.
- `std::optional<TextMetrics> Canvas::measureText(const Ctx& c, const TrText& text, const TextStyle& style = {}, Unit unit = Unit::Px) const` · `… measureText(const Ctx& c, const std::vector<TextSpan>& spans, …) const` — samma mått i ritytans EGEN layout, med det Ctx layouten redan har (en subklass `heightForWidth`/`contentSize` — ett dokument som bryts efter bredden). Frågar aldrig layouten: tätheterna och fonterna ur `c` (`nullopt` utan `c.canvas`), kameran som den är satt eller härledd och en väntande `setMapScale` — men inte en väntande inramning (`zoomToRect`/`zoomToBox`/`panBy`). En 3D-ytas världsmått räknas vid den senast ritade ytan; en 3D-yta som aldrig ritats och har AUTO-mått svarar `nullopt` för ett mått som behöver världsenheten (px och mm mäts).
- `void Canvas::invalidateOverlay()` — ny present, ingen omritning av lagren.
- 3D: `drawLine3`, `drawPolyline3`, `drawText3`, `drawMarker3`, `drawTriangles3`, `drawPoints3`, `void setRenderState(const RenderState& state)` (se `canvas-3d`).

### Exempel
```cpp
class Spar : public glaze::AppWindow {
public:
    Spar() : glaze::AppWindow("Spår", 640, 480)
    {
        karta.dock = glaze::Dock::Fill;
        karta.setPannable(true);
        spar = {{20, 20}, {200, 120}, {320, 60}};

        karta.onPaint = [this](glaze::Widget*, glaze::PaintArgs& e) {   // i VÄRLDEN
            e.painter.drawPolyline(spar, {.paint = gfx::Color{0, 120, 255},
                                          .width = glaze::mm(0.5f)});
        };
        karta.onPaintOverlay = [this](glaze::Widget*, glaze::PaintArgs& e) {   // HUD
            if (!pekare)
                return;
            e.painter.drawText({8, 8}, "x = " + std::to_string(pekare->x),
                               {.background = gfx::Color{255, 255, 255, 220},
                                .padding = 4, .anchor = glaze::TextAnchor::TopLeft});
        };
        karta.onMouseMove = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) {
            pekare = e.world;             // tom ovanför horisonten under tilt
            static_cast<glaze::Canvas*>(s)->invalidateOverlay();   // bara HUD:en
        };

        root().adopt(karta);
    }

private:
    glaze::Canvas               karta;
    std::vector<glaze::Point>   spar;
    std::optional<glaze::Point> pekare;
};
```

Ett rutnät som följer vyn UNDER ett punktmoln — målaren i ett eget lager, under molnets:
```cpp
karta.setLayers({"Flygfoto", "Rutnät", "Punktmoln"});   // 1 = rutnätets plats
karta.setLayerPainter(1, true);
karta.onPaintLayer = [this](glaze::Widget*, glaze::PaintArgs& e) {
    if (e.layer != 1)
        return;                               // handlern körs en gång per plats
    const gfx::Sizef s = e.painter.size();    // ytan i logiska px
    const auto a = karta.toWorld({0, 0}), b = karta.toWorld({s.w, s.h});
    if (!a || !b)
        return;                               // ovanför horisonten under tilt
    for (double x = std::floor(std::min(a->x, b->x) / 10) * 10;
         x < std::max(a->x, b->x); x += 10)   // en linje var tionde meter
        e.painter.drawLine({x, a->y}, {x, b->y}, {.paint = gfx::Color{0, 0, 0, 60}});
};
```

Innehåll som hålls inom en ram — en panel i HUD:en och en vriden ruta i världen:
```cpp
karta.onPaintOverlay = [this](glaze::Widget*, glaze::PaintArgs& e) {
    const glaze::Rect panel{8, 8, 220, 120};
    e.painter.drawRect(panel, {.width = 1.0f});
    auto k = e.painter.save(panel);             // allt till `k`:s slut klipps
    for (std::size_t i = 0; i < rader.size(); ++i)
        e.painter.drawText({16, 20 + 18.0 * i}, rader[i],
                           {.anchor = glaze::TextAnchor::Left});   // långa rader kapas
};
karta.onPaint = [this](glaze::Widget*, glaze::PaintArgs& e) {
    auto s = e.painter.save();
    e.painter.translate(fastighet.x, fastighet.y);
    e.painter.rotate(vridning);                 // klippet blir en fyrhörning
    auto k = e.painter.save(glaze::Rect{-50, -30, 100, 60});
    e.painter.drawImage({-80, -60, 160, 120}, ortofoto);   // bara inom rutan
};
```

Textens mått utanför ritningen — en ram precis runt en etikett i världsenheter:
```cpp
// i onLoad eller en handler: fönstret är öppet, fonterna finns
if (auto m = karta.measureText("Lunden 1:4", {.size = glaze::world(6)},
                               glaze::Unit::World))
    karta.addShape(2, glaze::Shape::rect({x - m->width / 2, y - m->height / 2,
                                          m->width, m->height},
                                         gfx::Color{0, 0, 0, 0},
                                         glaze::Stroke{.width = 1}));
```

Ett dokument på en rityta — höjden följer den bredd layouten ger, mätt i layouten med dess Ctx:
```cpp
struct Dokument : glaze::Canvas {
protected:
    std::optional<float> heightForWidth(const glaze::Ctx& c, float w) const override
    {
        if (height > 0)
            return std::nullopt;               // en satt höjd vinner
        glaze::TextStyle s = stil_;
        s.wrapWidth        = glaze::px(w);
        const auto m = measureText(c, text_, s);   // MED c — aldrig formen utan
        return m ? std::optional<float>(static_cast<float>(m->height)) : std::nullopt;
    }
    glaze::TrText    text_ = "…";
    glaze::TextStyle stil_{.size = 14.0f};
};
```

### .glazeui
```cpp
karta.onPaint = [this](glaze::Widget* s, glaze::PaintArgs& e) { kartaRitas(s, e); };
karta.setLayerPainter(1, true);
karta.onPaintLayer = [this](glaze::Widget* s, glaze::PaintArgs& e) { kartaLager(s, e); };
karta.onPaintOverlay = [this](glaze::Widget* s, glaze::PaintArgs& e) { kartaHud(s, e); };
```
Bara kopplingen står i filen; det handlern ritar syns aldrig i previewn. Målarens plats (`setLayerPainter`) är lagrets tillstånd och utförs. `Canvas::measureText` är en FRÅGA (som `hitTest`) och har ingen `.glazeui`-form.

### Fallgropar
- Ritningen MÅSTE vara deterministisk och får varken ändra trädet eller ritytans former — handlern kan köras en gång per skadad yta.
- Allt i `onPaint` byggs och laddas upp på nytt varje ritning; tusentals stillastående former ska ligga i ett lager.
- `onPaintLayer` körs en gång PER lager med `setLayerPainter` — med två platser pröva `e.layer`. Utan någon plats körs den aldrig; `onPaint` ritar fortfarande ovanpå allt.
- Ett lager med bara målaren är ett Auto-lager utan former i rymden, alltså Ground: i 3D-målet ritas målaren då på marken UTAN djup, under World-lagren. Ska den djuptestas mot meshar och moln: `setLayerPlacement(i, glaze::LayerPlacement::World)`.
- Med terräng draperas lagrets målare inte (draperingen ritas inte om varje bildruta): i ett Ground-lager ritas den i markens steg på z = 0, utan djup.
- `translate`/`rotate`/`scale` verkar på punkterna, inte på måtten: en linje i mm är lika bred efter `scale(2, 2)`.
- `{ auto s = e.painter.save(); e.painter.screenSpace(); … }` — rymden och transformen läggs tillbaka när `s` går ur räckvidd.
- `save(rect)` tolkar rektangeln i den transform och rymd som gäller NÄR den anropas, och klippet är sedan en yta på skärmen: en `translate`/`rotate`/`screenSpace` efteråt flyttar det inte. Lägg alltså transformen först och klippet sist. Utan `auto k =` dör klippet direkt (`[[nodiscard]]` varnar).
- Föremålen i rymden (meshar, instanser, punkter) klipps bara till klippets OMSKRIVNA rektangel — under ett vridet klipp syns de i dess hörn. Linjer, ytor, text och bilder klipps exakt.
- Fler än 8 kanter (tre eller fler nästlade klipp i olika vridning) loggas en gång, och de minsta hörnen skärs av: klippet blir något mindre, aldrig större.
- `Canvas::measureText` i en KONSTRUKTOR ger `nullopt` (inget fönster än) — mät i `onLoad`/`onShow`. Mått i mm och världsenheter räknas med skärmens täthet och kamerans skala i MITTEN, som etiketten byggs; under tilt är ett världsmått etikettens tänkta storlek.
- I ritytans EGEN `heightForWidth`/`contentSize`: `measureText(c, …)` med krokens `c`. Formen utan Ctx frågar layouten (ytan resolvas, och en Panel resolvar sina barn), så därifrån hade den gått runt i sig själv — den loggar i stället en gång och svarar `nullopt`.
- `{}` och `{{"abc"}}` är tvetydiga mellan texten och delarna (som i `Shape::text`); `{"abc"}` är text.

<!-- END:canvas-painter -->

<!-- BEGIN:canvas-hit-test | hittest träffprov hit pick plockning klick click mus mouse canvasmouseargs shapeat hitresult hitoptions dragthreshold world local button mittenknapp högerknapp högerdrag right onmousecancel avbrott dubbelklick rutmarkering gummiband hitmode window crossing tolerance tolerans hitinside element snap setlayersnap layersnap hörn kant -->
## Träffprov och mus på ritytan (`canvas-hit-test`)

**Alias:** hittest, träffprov, hit, pick, plockning, klick, click, mus, mouse, canvasmouseargs, shapeat, hitresult, hitoptions, dragthreshold, world, local, button, mittenknapp, högerknapp, högerdrag, right, onmousecancel, avbrott, dubbelklick, rutmarkering, gummiband, hitmode, window, crossing, tolerance, tolerans, hitinside, element, snap, setlayersnap, layersnap, hörn, kant
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [canvas.h](Glaze/widgets/include/glaze/widgets/canvas.h)

Musens events bär punkten i tre former: `e.world` (världen), `e.local` (ritytans logiska px) och knappen/modifierarna. `hitTest(lokal)` svarar med alla former under punkten, ÖVERST FÖRST, med skärmens tolerans — exakt också under tilt. I 3D plockar `hitTest(lokal, HitOptions)` längs strålen (se `canvas-3d`), och `HitOptions::tolerance` ger anropet en egen tolerans (px, mm eller världsenheter) i stället för stilens app-globala. Sätter handlern `e.handled = true` gör ritytan inget eget med händelsen (ingen panorering startar).

RUTMARKERINGEN är `hitTest(Rect lokal, HitMode)`: rektangeln på skärmen (ett gummiband, också draget baklänges) mot det formerna ritar, rätt vriden och tiltad — `HitMode::Window` tar formerna HELT INOM, `HitMode::Crossing` dem som skär eller ligger inom. Ett PUNKTLAGER kan vara EN form: `HitResult::element` säger vilken punkt i `Shape::points` (eller markören) som träffades, och rutan svarar med en träff per punkt. En kontur utan fyllning väljs inifrån med `Stroke::hitInside` — också en helt osynlig form, som då är en träffzon.

HÖGERKNAPPEN når samma handlers: `onMouseDown` med `e.button == Right` — och sätter handlern `e.handled = true` TAR appen trycket, så `onMouseMove`/`onMouseUp` följer med `Right` och ingen snabbmeny öppnas (ett verktyg som drar med höger knapp). Tas trycket inte går det vidare till snabbmenyn, med `onMouseCancel` och utan `onMouseUp`. Ett taget tryck (vilken knapp som helst) ger ritytans `onMouseMove` för rörelserna och `onDoubleClick` för det andra trycket, men ingen gest. `onMouseDown`/`onMouseUp`/`onMouseCancel` är basens och BUBBLAR INTE: ett mitten- eller högertryck på ett BARN till ritytan (ett fält på kartan) fyrar barnets och inte ritytans. `onDoubleClick` kommer för alla tre knapparna (`e.button`), och ett tryck vars släpp aldrig kommer (fokusförlust, OS:et tog musen) ger `onMouseCancel` i stället för `onMouseUp`: panoreringen står still där den är, utan tröghet.

### API
- `CanvasMouseArgs : MouseArgs` — basens `local` (`Point`, logiska px), `button` (`gfx::MouseButton::Left/Middle/Right/None`), `mods` (`gfx::kMod*`) och utdata `handled`, plus `world` (`std::optional<Point>`), `wheelX`/`wheelY`, `precise` (styrplattan) och `ray` (`std::optional<Ray3>`).
- `onMouseCancel` (basens, `events`) — trycket som avbröts (`e.button`; `None` = en capture utan knapp), med pekarens senast kända läge i `local`/`world` (tomma när pekaren lämnat fönstret).
- `std::vector<HitResult> hitTest(Point local) const` · `int shapeAt(Point local) const` (lager 0) · `int shapeAt(int layer, Point local) const` — `-1` = ingen.
- `HitResult` — `layer`, `index` (formen i lagret), `view` (del-vyn), `instance`, `element` (en markörforms PUNKT — den träffade närmast pekaren, vid lika den senare; −1 = hela formen), `kind` (`HitKind::Shape/Surface/Vertex/Edge/Point/Label`), `point` (`std::optional<Point3>`), `normal`, `distance`.
- `std::vector<HitResult> hitTest(Point local, const HitOptions& options) const` — 3D, närmast först.
- `HitOptions` i fältordning — `opaqueOnly`, `occlusion`, `vertexTolerance`, `edgeTolerance`, `pointTolerance` (`Length`, 0 = `tolerance`), `maxDistance` (världsenheter, 0 = obegränsat), `tolerance` (`Length` — anropets, för 2D-formerna, etiketterna och linjerna i rymden; 0 = stilens `canvasHitTolerance`).
- `std::vector<HitResult> hitTest(Rect local, HitMode mode) const` — `HitMode::Window` (helt inom) / `HitMode::Crossing` (skär eller inom); överst först, markörformerna en träff per punkt i rutan.
- `Stroke::hitInside` (`bool`, sist i `Stroke`) — insidan räknas som yta i träffprovet, också utan fyllning och med en osynlig kontur.
- `std::vector<HitResult> castRay(const Ray3& ray, const HitOptions& options = {}) const` · `std::optional<Ray3> rayAt(Point local) const`
- `void setLayerSnap(int layer, bool on)` · `bool layerSnap(int layer) const` — HÖRNEN OCH KANTERNA i lagret, på från start: av ger lagret inga `Vertex`- eller `Edge`-träffar i `hitTest(lokal, HitOptions)`, medan ytan, punkterna, linjerna och 2D-formerna träffas som förut. Följer lagret i `moveLayer`; `setLayer`/`setLayers` slår på den; ritar inte om (bara träffprovet läser den); ett ogiltigt lager loggas.

### Exempel
```cpp
class Klick : public glaze::AppWindow {
public:
    Klick() : glaze::AppWindow("Klick", 640, 480)
    {
        karta.dock = glaze::Dock::Fill;
        karta.setPannable(true);
        karta.addShape(glaze::Shape::circle({100, 100}, 30, gfx::Color{52, 120, 186}));
        karta.addShape(glaze::Shape::rect({200, 80, 60, 40}, gfx::Color{217, 201, 182}));

        karta.onMouseDown = [this](glaze::Widget*, glaze::CanvasMouseArgs& e) {
            if (e.button == gfx::MouseButton::Left)
                ned = e.local;
        };
        karta.onMouseUp = [this](glaze::Widget*, glaze::CanvasMouseArgs& e) {
            const double dx = e.local.x - ned.x, dy = e.local.y - ned.y;
            const double t  = style().dragThreshold;
            if (e.button != gfx::MouseButton::Left || dx * dx + dy * dy > t * t)
                return;                   // ett drag (panorering), inget klick
            const int i = karta.shapeAt(e.local);   // lager 0; -1 = ingen
            setTitle(i < 0 ? "Inget" : "Form " + std::to_string(i));
        };

        root().adopt(karta);
    }

private:
    glaze::Canvas karta;
    glaze::Point  ned;
};
```

Gummibandet, en osynlig träffzon och en större fångst:
```cpp
// Korsningen när bandet dras åt vänster, fönstret åt höger (CAD:s regel).
const glaze::HitMode läge = slut.x < start.x ? glaze::HitMode::Crossing : glaze::HitMode::Window;
const glaze::Rect    band{start.x, start.y, slut.x - start.x, slut.y - start.y};
for (const glaze::HitResult& h : karta.hitTest(band, läge))
    välj(h.layer, h.index, h.element);   // element = punkten i ett punktlager, annars −1

karta.addShape(glaze::Shape::polygon(zon, gfx::Color{0, 0, 0, 0},
    glaze::Stroke{.paint = gfx::Color{0, 0, 0, 0}, .hitInside = true}));
const auto träffar = karta.hitTest(e.local, glaze::HitOptions{.tolerance = glaze::mm(2)});
```

Stolparnas hörn fångas, höjdmodellens inte:
```cpp
// scen är en glaze::Canvas med en Camera3D; lager 1 är Terrain (canvas-terrain)
scen.setLayerSnap(1, false);   // DEM:en: bara ytan, inga hörn eller kanter
for (const glaze::HitResult& h : scen.hitTest(e.local, glaze::HitOptions{}))
    if (h.kind == glaze::HitKind::Vertex)
        fångaHörn(h.layer, h.index, *h.point);   // aldrig från lager 1
```

### .glazeui
```cpp
karta.onMouseDown = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) { kartaNed(s, e); };
karta.onMouseUp = [this](glaze::Widget* s, glaze::CanvasMouseArgs& e) { kartaSlapp(s, e); };
karta.onMouseCancel = [this] { kartaAvbruten(); };
karta.addShape(glaze::Shape::polygon({{0, 0}, {80, 0}, {80, 50}, {0, 50}}, gfx::Color{0, 0, 0, 0}, glaze::Stroke{.width = glaze::mm(0.5), .hitInside = true}));
karta.setLayerSnap(0, false);
```

### Fallgropar
- `onMouseDown`/`onMouseMove`/`onMouseUp` får vänster-, MITTEN- och HÖGERKNAPPEN, och `onDoubleClick` alla tre — pröva `e.button`. Ett högertryck som handlern inte tar ger snabbmenyn och inget `onMouseUp`.
- En handler med `glaze::CanvasMouseArgs&` på basens fält körs bara när ritytan skickar ritytans args; satt på en annan widget loggas det en gång och handlern körs inte. Behöver den inte världen räcker `glaze::MouseArgs&`.
- `onMouseUp` SPARAR och `onMouseCancel` AVBRYTER: två olika handlingar, aldrig båda för samma tryck.
- `hitTest` kräver ett öppet fönster (etiketternas lådor byggs i ritningen); utan svarar den tomt.
- Dolda lager, lager utanför sitt zoomintervall, genomskinliga lager och etiketter som kollisionen gömt träffas inte — inte heller text och markörer utanför sitt EGET zoomintervall (`TextStyle::minZoom`/`maxZoom`, också en markör utan kollision). En etikett med golv eller tak (`minSize`/`maxSize`) träffas i den storlek den ritas.
- `e.world` är tom ovanför horisonten under tilt.
- En tolerans i världsenheter räknas om vid pekaren — ovanför horisonten blir den 0.
- Rutan räknar linjerna med sin MITTLINJE (bredden räknas inte), och en form som närplanet skär av ligger aldrig helt inom. Formerna i rymden (linjer, meshar, ytor, instanser, punktmoln) prövas inte av rutan, och med terräng i 3D-målet inte Ground-lagren — text och markörer i rymden prövas.
- Slå av snap på höjdmodellens lager (`setLayerSnap(terräng, false)`): annars söker varje `hitTest(lokal, HitOptions)` — en hover gör ett per musrörelse — hörn och kanter bland trianglarna nära strålen i en mesh om tiotusentals trianglar, fast ingen ska fånga dem.
- `castRay` med `occlusion` plockar inte bortom den första ogenomskinliga ytan: det som står bakom en vägg kommer inte i svaret alls (och dess sökstruktur byggs aldrig). Utan `occlusion` kommer allt längs strålen.

<!-- END:canvas-hit-test -->

<!-- BEGIN:canvas-images | bitmap rendertexture imagesource bild image pixlar pixels rgba plattor tiles kartplatta textur texture decode png setpixels nål kartnål pin keeppixels memorybudget minnesbudget imagestyle färgjustering kontrast contrast ljushet brightness mättnad saturation gråskala grayscale -->
## Bilder på ritytan: Bitmap, RenderTexture, ImageSource (`canvas-images`)

**Alias:** bitmap, rendertexture, imagesource, bild, image, pixlar, pixels, rgba, plattor, tiles, kartplatta, textur, texture, decode, png, setpixels, nål, kartnål, pin, keeppixels, memorybudget, minnesbudget, imagestyle, färgjustering, kontrast, contrast, ljushet, brightness, mättnad, saturation, gråskala, grayscale
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [bitmap.h](Glaze/widgets/include/glaze/canvas/bitmap.h), [render_texture.h](Glaze/widgets/include/glaze/canvas/render_texture.h), [image_source.h](Glaze/widgets/include/glaze/canvas/image_source.h)

En bild på ritytan kommer ur en `ImageSource`: en asset (`glaze::img`), appens pixlar (`glaze::Bitmap`, RGBA8 med rak alfa, rad 0 överst) eller en målning (`glaze::RenderTexture`, ritad med `Draw` i fysiska px). Konstruktorerna är implicita, så `Shape::image`, `MarkerStyle::img` och `MeshStyle::texture` tar vilken som helst. `Bitmap` och `RenderTexture` är HANDTAG — kopior delar bilden, och texturen bor per fönster.

`ImageSource` är RAMVERKETS bildvärde, inte bara ritytans: varje widget som visar en bild tar det — `Picture`, `Button`, `ToolButton`, trädets noder, tabellernas rader, menyposter, `messageBox`, dragbilden, `appIcon` och `TrayIcon` (se `assets`). Det jämförs som ett VÄRDE: två bilder är lika när de är samma källa i samma VERSION, så `setImg(sammaBitmap)` efter `setPixels` är en ändring som ritar om (`sameImage` frågar bara om källan). `sizeClass()` är assetens storleksklass och 0 för pixlar — en widget ritar då ikonen i stilens `iconFallback`.

### API
- `Bitmap(int w, int h)` · `Bitmap(int w, int h, const std::uint8_t* rgba, std::size_t stride = 0)` · `static Bitmap decode(const void* data, std::size_t size)` (bara PNG, trådsäker)
- `void setPixels(int x, int y, int w, int h, const std::uint8_t* rgba, std::size_t stride = 0)` · `void fill(gfx::Color c)` · `void setFilter(gfx::ImageFilter f)` (`Linear`/`Nearest`) · `bool valid() const` · `int width() const` · `int height() const`
- MIP-KEDJAN (REQ8 5): en `Bitmap` och en `RenderTexture` har den i varje fönster, omräknad på GPU:n när pixlarna ändras eller texturen målas om, så en stor bild ritad utzoomad blir varken grynig eller får moiré. Filtret gäller också mellan nivåerna: `Linear` är MJUKT (trilinjärt), `Nearest` SKARPT (närmaste pixel i närmaste nivå — pixlarna syns inzoomat). Budgeten räknar kedjan (en tredjedel till).
- STÖRRE ÄN GPU:NS STÖRSTA TEXTUR (`gfx::Renderer::maxTextureSize()`, 16384 på de flesta): en `Bitmap` av valfri storlek delas automatiskt i RUTOR med en kantpixel från grannen (ingen söm), och `Shape::image`/`Painter::drawImage` ritar den som en fyrhörning per ruta med samma projektiva avbildning — också på fyra fria hörn och under tilt.
- `RenderTexture(int w, int h)` · `void paint(PaintFn fn, gfx::Color clear = gfx::Color{0, 0, 0, 0})` · `void repaint()` — `using PaintFn = std::function<void(const Ctx&)>`.
- `static Shape image(Rect r, ImageSource source, ImageStyle style = {})` — en världsrektangel, perspektivkorrekt under tilt. `Painter::drawImage(r, källa, stil)` likadant.
- `struct ImageStyle { float opacity = 1, saturation = 1, contrast = 1, brightness = 1; }` (fälten i den ordningen — kontrakt för designerade initierare) — `opacity` (0–1) är BILDENS egen och ritas GÅNGER lagrets (`setLayerOpacity`, `canvas-layers`): en höjdmodell på 0.6 i ett lager på 0.5 syns med 0.3 — också draperad på terrängen. FÄRGJUSTERINGEN (REQ8 3) verkar på bildens raka färg: `brightness` multiplicerar (0 svart), `contrast` sträcker kring mittgrått (0 grått), `saturation` blandar mot luminansen (0 = GRÅSKALA), i den ordningen och klampad till 0–1 efter varje steg. 1 är orört, över 1 förstärker; under 0 räknas som 0 och ett tal som inte är ändligt som 1. Värdena är UNIFORMER: att byta stil med `setShape` bygger om fyra hörn och laddar aldrig upp pixlarna igen. Samma namn och samma betydelse som `MeshStyle`s (`canvas-meshes`).
- `static Shape image(Point topLeft, Point topRight, Point bottomRight, Point bottomLeft, ImageSource source, ImageStyle style = {})` — bilden på FYRA FRIA HÖRN: bildens övre vänstra, övre högra, nedre högra och nedre vänstra hörn i världen, i ringordning (spegelvänt går). Avbildningen är PROJEKTIV — raka linjer i bilden förblir raka, utan knäck längs diagonalen — och perspektivkorrekt under tilt. `Painter::drawImage` har samma överlagring.
- `const ImageStyle& Shape::imageStyle() const` — stilen som fabriken normerat den.
- `void setKeepPixels(bool keep)` · `bool keepPixels() const` (REQ3 9, förval true) — med false SLÄPPS CPU-kopian i slutet av det pumpvarv där bilden första gången laddats upp hel och aktuell i ett fönster (varje fönster som ritar den i samma varv hinner få den). Efter släppet: `pixels()` ger nullptr, `valid()`/`width()`/`height()` står kvar, `setPixels`/`fill` loggar och gör ingenting, och fönstren som har texturen ritar den så länge något handtag lever — budgetens LRU vräker den aldrig (den räknas, och loggas när taket inte hålls).
- `void Canvas::setMemoryBudget(std::size_t bytes)` (REQ3 10, 0 = förvalet) · `void App::setCanvasMemoryBudget(std::size_t bytes)` (förval 256 MB — förvalet PER RITYTA, och taket för fönstrets bilder utanför ritytorna) · `void App::setCanvasUploadBudget(std::size_t bytes)` (förval 16 MiB per fönster och frame). En bild räknas på den rityta som RITADE den senast; draperingarna på ritytans budget med.

### Exempel
```cpp
// karta är en glaze::Canvas med y uppåt (setYUp) och kamera i meter
glaze::Bitmap platta(256, 256);                   // RGBA8, rak alfa
platta.fill(gfx::Color{236, 239, 220});
std::vector<std::uint8_t> rad(256 * 4, 120);      // en grå rad
platta.setPixels(0, 128, 256, 1, rad.data());     // laddar upp BARA den
karta.addShape(glaze::Shape::image({674000, 6579950, 256, 256}, platta));

karta.addShape(glaze::Shape::marker({674100, 6580050},   // kartnål PÅ punkten
    {.size = glaze::mm(6), .anchor = glaze::TextAnchor::Bottom,
     .img = glaze::img("nalar", "rod")}));

glaze::RenderTexture ruta(128, 128);              // FYSISKA px
ruta.paint([](const glaze::Ctx& c) {              // körs lat, deterministiskt
    glaze::Draw::rect(c, c.bounds(), 12, gfx::Color{61, 139, 253});
});
karta.addShape(glaze::Shape::image({674150, 6579950, 64, 64}, ruta, {.opacity = 0.8f}));

// Ett snett flygfoto på fyra georefererade hörn (y uppåt: övre = norr).
karta.addShape(glaze::Shape::image({674000, 6580100}, {674120, 6580110},
                                   {674110, 6580010}, {674010, 6580000},
                                   platta, {.opacity = 0.6f}));

// FÄRGJUSTERINGEN live: ett reglage (glaze::Slider) ändrar stilen, aldrig
// pixlarna — lager `foto`, form 0.
kontrast.onChange = [this] {
    karta.setShape(foto, 0, glaze::Shape::image(ruta2, flygfoto,
        {.saturation = gråskala.isChecked() ? 0.0f : 1.0f,
         .contrast = kontrast.value(), .brightness = 1.1f}));
};

// En TUNG karta och en miniatyr i samma fönster: var sin budget, och
// plattorna släpper sina pixlar när de kommit upp (ett fönster som inte
// stängs och öppnas igen — se fallgroparna).
karta.setMemoryBudget(std::size_t{2} << 30);
miniatyr.setMemoryBudget(std::size_t{64} << 20);
glaze::Bitmap skugga = glaze::Bitmap::decode(png.data(), png.size());   // valfri tråd
skugga.setKeepPixels(false);                                            // före posten
```

### .glazeui
```cpp
karta.addShape(glaze::Shape::image({674000, 6579950, 256, 256}, glaze::img("toolbar", "info"), glaze::ImageStyle{.opacity = 0.9, .saturation = 0, .contrast = 1.2, .brightness = 1.1}));
karta.addShape(glaze::Shape::image({674000, 6580100}, {674120, 6580110}, {674110, 6580010}, {674010, 6580000}, glaze::img("toolbar", "info"), {.opacity = 0.6}));
karta.addShape(glaze::Shape::marker({674100, 6580050}, glaze::MarkerStyle{.size = glaze::mm(3), .anchor = glaze::TextAnchor::Bottom, .img = glaze::img("toolbar", "info")}));
karta.setMemoryBudget(268435456);
```
Bara asset-bilder har en `.glazeui`-form; pixlar och målningar räknas fram i drift och är koddata (`setKeepPixels` också). Minnesbudgeten är ett setter-fält i byte. `ImageStyle` skrivs med typnamnet eller som bar klammerlista, designerat eller positionellt — previewn ritar färgjusteringen.

### Fallgropar
- Ändrar appen pixlarna i en SYNLIG bild ska den invalidera ritytan (`karta.invalidate()`) — eller, för en widget, sätta bilden igen (`setImg(sammaBild)`); `setPixels` laddar bara upp den ändrade rektangeln.
- En Bitmaps LOGISKA storlek i en widget är pixlarna / skalan (`Picture`s autostorlek): räkna fram 2× pixlar för en skarp bild på skala 2. En OS-ikon (`appIcon`, `TrayIcon`) ur en Bitmap räknas om på CPU:n ur de levande pixlarna; en `RenderTexture` kan inte bli en OS-ikon.
- `RenderTexture::paint` ritar inte direkt — funktionen körs lat vid nästa ritning som behöver den och måste vara deterministisk. Innehållet är premultiplicerat.
- En bild står alltid rättvänd: med `setYUp(true)` är rektangelns ÖVRE kant den med störst y.
- Bildavkodning utöver PNG är appens — pixlarna in via `Bitmap(w, h, rgba)`.
- En bild på fyra hörn måste vara STRIKT KONVEX: tre hörn i linje, en fluga (korsade kanter), en inbuktning eller ett hörn som inte är ändligt ritar INGENTING, träffas inte av `hitTest` och loggas en gång per process. Fyra hörn MED HÖJD har ingen egen fabrik: en texturerad `Mesh` (`canvas-meshes`, uv per hörn) täcker en plan parallellogram, men en fri fyrhörning blir där affin per triangel.
- `setKeepPixels(false)` hör till plattor i EN rityta i ett fönster som lever: ett fönster UTAN textur — ett nytt fönster, en rityta flyttad till ett flytande dockningsfönster, ett fönster som stängts och öppnats igen — ritar INGENTING för bilden (loggas en gång), och ingen OS-ikon kan göras av den. Ett fönster en version efter står kvar på sin version. Sätt flaggan före posten till UI-tråden; ingen annan tråd får läsa pixlarna efteråt.
- Flera ritytor med förvalets budget kan tillsammans ta mer än `App::setCanvasMemoryBudget` — budgeten är per rityta. Det som ritades i förra framen och bilder utan pixlar vräks aldrig.
- En bild i RUTOR ritas bara av ritytans bilder: en widget (`Picture` …), en bildmarkör, en meshtextur och ett draperat lager (`LayerPlacement::Terrain`) ritar INGENTING för en Bitmap större än `maxTextureSize()` (loggas en gång per fönster). Den laddas upp hel i en frame, som varje ny bild. En asset har ingen mip-kedja.

<!-- END:canvas-images -->

<!-- BEGIN:canvas-3d | 3d camera3d kamera3d orbit perspektiv perspective ortografisk orthographic line3 polyline3 text3 marker3 layerplacement renderstate djup depth msaa azimuth elevation -->
## Ritytan i 3D: Camera3D, former i rymden, lagrens läge (`canvas-3d`)

**Alias:** 3d, camera3d, kamera3d, orbit, perspektiv, perspective, ortografisk, orthographic, line3, polyline3, text3, marker3, layerplacement, renderstate, djup, depth, msaa, azimuth, elevation
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [camera.h](Glaze/widgets/include/glaze/canvas/camera.h), [style3d.h](Glaze/widgets/include/glaze/canvas/style3d.h), [canvas.h](Glaze/widgets/include/glaze/widgets/canvas.h), [canvas/CLAUDE.md](Glaze/widgets/include/glaze/canvas/CLAUDE.md)

3D är SAMMA widget och samma lager: så fort ritytan har en form i rymden, en `Camera3D` eller en del-vy ritar den i ett eget 3D-mål med djup och 4× MSAA. `Camera3D` kretsar kring ett mål (avstånd, azimut, elevation); världen är högerhänt med x öster, y norr och z UPP, i double. Med en `Camera3D` betyder gesterna ORBIT: vänsterdrag = azimut/elevation (`setRotatable`/`setPitchable`), mittendrag eller Shift+drag = panorering (`setPannable`), hjulet = dolly mot målet (`setZoomable`). Mittendragets panorering låter punkten under pekaren följa pekaren i båda axlarna (bara målet flyttas), och med Shift FLYGER draget fram/bak längs blicken; uppåtpilen flyger. Allt det är FÖRVALDA BINDNINGAR i läget `"orbit"` (`input`): flygläget — utan Shift flyger draget (ett drag nedåt = framåt), med Shift höjer och sänker det kameran — är två rader, `setBinding("orbit", glaze::InputSource::MiddleDragY, 0, glaze::InputAction::Fly)` och samma med `gfx::kModShift` och `Lift` (och likadant för `ScrollY`). `panBy` (verktyget) flyttar i planet vinkelrätt mot blicken.

### API
- `Camera3D` i fältordning: `target` (`Point3`), `distance` (10), `azimuth` (0 = blicken mot norr, medurs), `elevation` (30; 90 = rakt uppifrån), `roll`, `projection` (`Projection::Perspective/Orthographic`), `fov` (45), `orthoHeight` (0 = matchad), `nearPlane`, `farPlane` (0 = ur scenen), `up` (`UpAxis::Z/Y`).
- `static Camera3D lookFrom(Point3 eye, float yaw, float pitch, float fov = 45.0f, UpAxis up = UpAxis::Z)` · `Point3 eye() const`
- `void setCamera(const Camera3D& c)` · `Camera3D camera3D() const` · `bool is3D() const` — `setCamera(Camera)` tar tillbaka kartkameran.
- `void orbitBy(float dAzimuth, float dElevation)` · `void panBy(Point deltaLocal)` · `void dollyBy(double factor)` · `void easeTo(const Camera3D& target, int ms)` · `void zoomToBox(const Box3& box, Length pad = {})`
- `void setElevationRange(float minDeg, float maxDeg)` (förval ±85°) · `void setOrbitTumble(bool on)` · `void setDistanceRange(double minDistance, double maxDistance)`
- Vad dragen, hjulet, styrplattan och tangenterna gör i orbiten — och i förstapersonen (`setInputMode("firstPerson")`, giren kring ögat) — är bindningarnas (`Canvas::setBinding`, `input`). `OrbitPan`/`setOrbitPan` finns inte längre: flygläget är två rader.
- `static Shape line3(Point3 a, Point3 b, Stroke stroke = {})` · `polyline3(std::vector<Point3> points, Stroke stroke = {})` · `text3(Point3 at, TrText text, TextStyle style = {})` · `marker3(Point3 at, MarkerStyle style = {})`
- `enum class LayerPlacement : std::uint8_t { Auto, Ground, World, Terrain }` · `void setLayerPlacement(int layer, LayerPlacement placement)`
- `RenderState` i fältordning: `depth` (`DepthTest::Less`), `depthWrite` (true), `depthBias`, `slopeBias`, `blend` (`Blend::Alpha`), `cull` (`Cull::None`) · `void setLayerRenderState(int layer, const RenderState& state)`
- `void setMultisample(int samples)` (1, 2, 4, 8) · `void setDepth(bool on)`
- `std::optional<Point> project(Point3 world) const` · `std::optional<Point3> toWorld(Point local, const Plane3& plane) const` · `std::optional<double> worldPerPixel(Point3 world) const` · `std::vector<Point3> visibleRegion(const Plane3& plane, double maxDistance = 0.0) const`

### Exempel
```cpp
// scen är en glaze::Canvas; koordinaterna är världens (x öst, y norr, z upp)
scen.setCamera(glaze::Camera3D{.target = {674000, 6580000, 8}, .distance = 90,
                               .azimuth = 30, .elevation = 25});
scen.setPannable(true);           // mittendrag panorerar, Skift flyger (bindningarna, input)
scen.setZoomable(true);           // hjulet: dolly mot målet
scen.setRotatable(true);          // vänsterdrag vågrätt: azimut
scen.setPitchable(true);          // vänsterdrag lodrätt: elevation (±85°)
scen.setDistanceRange(5, 500);

const int ledning = scen.addLayer("Ledning");   // en form i rymden ⇒ World
scen.addShape(ledning, glaze::Shape::line3({673998, 6580000, 11.6},
                                           {674040, 6580030, 11.6},
    {.paint = gfx::Color{40, 40, 40}, .width = glaze::world(0.04f)}));
scen.addShape(ledning, glaze::Shape::marker3({674040, 6580030, 11.6},
                                             {.size = 6}));

const int etiketter = scen.addLayer("Etiketter");
scen.setLayerPlacement(etiketter, glaze::LayerPlacement::Ground);   // överst
scen.addShape(etiketter, glaze::Shape::text3({674000, 6580000, 13.5}, "Stolpe 1",
    {.size = glaze::mm(2.5f), .halo = gfx::Color{255, 255, 255}}));

scen.setLayerRenderState(ledning, glaze::RenderState{.depthWrite = false});
scen.orbitBy(15, 0);              // appens egen rörelse: inget onCameraChange
scen.easeTo(glaze::Camera3D{.target = {674000, 6580000, 8}, .distance = 40,
                            .azimuth = 45, .elevation = 25}, 250);
```

### .glazeui
```cpp
scen.addLayer("Ledning");
scen.addLayer("Etiketter");
scen.setCamera(glaze::Camera3D{.target = {674100, 6580050, 12}, .distance = 80, .azimuth = 30, .elevation = 25});
scen.setCamera(glaze::Camera3D::lookFrom({674100, 6580000, 2}, 45, -5));
scen.addShape(1, glaze::Shape::line3({674000, 6580000, 0}, {674000, 6580000, 30}, glaze::Stroke{.paint = gfx::Color{90, 90, 90}, .width = glaze::world(0.5)}));
scen.addShape(1, glaze::Shape::text3({674000, 6580000, 31}, "Stolpe 12", glaze::TextStyle{.size = glaze::mm(2.5), .halo = gfx::Color{255, 255, 255}}));
scen.setLayerPlacement(2, glaze::LayerPlacement::Ground);
scen.setLayerRenderState(1, glaze::RenderState{.depth = glaze::DepthTest::LessEqual, .depthWrite = false});
scen.setElevationRange(-30, 60);
scen.setMultisample(4);
```
`orbitBy`, `panBy`, `dollyBy` och `easeTo` är handlingar och bevaras opakt; previewn utför kameran, formerna och lagrens läge.

### Fallgropar
- Skriv alltid typen: `setCamera(glaze::Camera3D{…})`. En bar positionell klammerlista eller `{}` är tvetydig mellan `Camera` och `Camera3D` och kompilerar inte.
- En form i rymden gör sitt lager till World (`Auto`); etiketter i ett World-lager djuptestas och kan skymmas. Lägg etiketter som ska synas överst i ett `Ground`-lager.
- Verktygen och `easeTo` fyrar aldrig `onCameraChange`; de kräver en `Camera3D` (annars logg).
- Med en `Camera3D` räknar `toWorld(lokal)`, `unitsPerPixelAt` och musens `e.world` mot marken z = 0.
- Sidopaketet `openxr` ritar huvudvyns lager i headsetets bilder med ett asymmetriskt frustum per öga (`View3D::frustum`) i stället för kameran; `Camera3D` själv har inget frustum. Skrivbordets kamera, mål och cache rörs inte.

<!-- END:canvas-3d -->

<!-- BEGIN:canvas-meshes | mesh meshdesc meshar instanser instances punktmoln pointcloud points3 box sphere cylinder tube polygon3 lighting ljus meshstyle pointstyle colorramp lod färgjustering kontrast contrast ljushet brightness mättnad saturation -->
## Meshar, primitiver, instanser och punktmoln (`canvas-meshes`)

**Alias:** mesh, meshdesc, meshar, instanser, instances, punktmoln, pointcloud, points3, box, sphere, cylinder, tube, polygon3, lighting, ljus, meshstyle, pointstyle, colorramp, lod, färgjustering, kontrast, contrast, ljushet, brightness, mättnad, saturation
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [mesh.h](Glaze/widgets/include/glaze/canvas/mesh.h), [shape.h](Glaze/widgets/include/glaze/canvas/shape.h), [style3d.h](Glaze/widgets/include/glaze/canvas/style3d.h)

Föremålen i rymden är former som allt annat. Primitiverna (`box`, `sphere`, `cylinder`) är delade enhetsmeshar som placeringen skalar och flyttar — tusen lådor är EN mesh på GPU:n. En egen `glaze::Mesh` byggs ur en `MeshDesc` (lägen relativt ett origo i double, normaler, färger, uv, skalärer, index, detaljnivåer) i vilken tråd som helst. `instances` ritar en prototyp på många platser; `pointCloud` ritar laserskanningar med en färgramp. Ljuset är ETT riktat ljus för hela ritytan.

### API
- `explicit Mesh(MeshDesc desc)` · `static Mesh fromWorld(std::span<const Point3> positions, std::vector<std::uint32_t> indices = {})` · `static Mesh triangles(std::vector<Point3> positions, gfx::Color color)` · `static Mesh indexed(std::vector<Point3> positions, std::vector<std::uint32_t> indices, gfx::Color color)`
- `MeshDesc` — `positions` (`Float3` relativt `origin`), `normals`, `colors`, `uvs`, `scalars`, `user`, `indices` (tomt = varje tre lägen), `lods`, `origin` (`Point3`), `keepCpuCopy` (true).
- `void setPositions(std::size_t first, std::span<const Float3> positions)` · `void setColors(std::size_t first, std::span<const gfx::Color> colors)` · `void preparePicking() const` · `Box3 bounds() const` · `bool valid() const`
- `static Shape mesh(Mesh m, Placement placement = {}, MeshStyle style = {})` · `box(Box3 box, MeshStyle style = {})` · `sphere(Point3 center, Length radius, MeshStyle style = {})` · `cylinder(Point3 base, double radius, double height, MeshStyle style = {}, int sides = gfx::kCylinderSides)` · `tube(std::vector<Point3> path, double diameter, MeshStyle style = {}, int sides = gfx::kTubeSides)` · `polygon3(std::vector<Point3> outer, Paint fill, std::optional<Stroke> stroke = std::nullopt)`
- `static Shape instances(Shape prototype, std::vector<Instance> instances, MeshStyle style = {})` · `points3(std::vector<Point3> points, PointStyle style = {})` · `pointCloud(PointCloud cloud, PointStyle style = {})`
- `PointCloud(std::vector<Float3> local, Point3 origin, std::vector<gfx::Color> colors = {}, std::vector<float> scalars = {})` · `static PointCloud fromWorld(std::span<const Point3> points, std::vector<gfx::Color> colors = {}, std::vector<float> scalars = {})`
- `Placement`: `at`, `yaw` (medurs ovanifrån), `pitch`, `roll`, `scale` · `Instance`: samma + `color` (multiplicerar, alfa 0 = ingen).
- `MeshStyle` i fältordning: `color`, `opacity`, `lit`, `fog`, `saturation`, `texture`, `ramp`, `range`, `size` (> 0 = skärmkonstant), `program`, `uniforms`, `textures`, `contrast`, `brightness`.
- FÄRGJUSTERINGEN (REQ8 3) — `brightness`, `contrast` och `saturation` verkar på basen (stilens färg × vertexfärgen × instansens × TEXTUREN × rampen, med draperingen), FÖRE ljuset och dimman, med samma regel som en bilds `ImageStyle` (`canvas-images`): ljusheten multiplicerar (0 svart), kontrasten sträcker kring mittgrått (0 grått), mättnaden blandar mot luminansen (0 = GRÅSKALA), i den ordningen och klampad efter varje steg; 1 är orört och över 1 förstärker. Uniformer — `setShape` med en ny stil laddar varken upp meshen eller texturen igen. Ett eget program (`canvas-shaders`) har samma funktion i förspelet: `glaze_colorAdjust(c, a)`.
- `PointStyle`: `size` (förval 3 px), `shape`, `color`, `ramp`, `fog`, `program`, `uniforms` · `ColorRamp`: `stops`, `min`, `max`.
- `void setLighting(const Lighting& l)` — `Lighting`: `direction` (ljusets FÄRDRIKTNING), `ambient`, `diffuse`, `exponent`, `rimDirection`, `rim`, `fog`, `fogStart`, `fogEnd`, `fogPower`, `twoSided`.

### Exempel
```cpp
// scen är en glaze::Canvas med en Camera3D (se canvas-3d)
const int lager = scen.addLayer("Föremål");
scen.setLighting(glaze::Lighting{.ambient = 0.45f, .exponent = 1.5f});

// PRIMITIVERNA — delade enhetsmeshar: tusen lådor är EN mesh på GPU:n.
scen.addShape(lager, glaze::Shape::cylinder({0, 0, 0}, 0.25, 12,
                                            {.color = gfx::Color{140, 110, 80}}));
scen.addShape(lager, glaze::Shape::box({{-2, -0.2, 11.6}, {2, 0.2, 12}},
                                       {.color = gfx::Color{170, 170, 180}}));
scen.addShape(lager, glaze::Shape::sphere({0, 0, 13}, glaze::mm(2)));   // skärmkonstant
scen.addShape(lager, glaze::Shape::tube({{-2, 0, 12}, {20, 0, 10}, {42, 0, 12}}, 0.05));

// EN EGEN MESH ur data — byggs i valfri tråd. Moturs = framsidan.
glaze::MeshDesc d;
d.positions = {{0, 0, 0}, {4, 0, 0}, {4, 3, 0}, {0, 3, 0}};   // relativt origin
d.colors    = std::vector<gfx::Color>(4, gfx::Color{226, 214, 190});
d.indices   = {0, 1, 2, 0, 2, 3};
d.origin    = {10, 10, 0};        // världen i double
scen.addShape(lager, glaze::Shape::mesh(glaze::Mesh(std::move(d)),
                                        {.yaw = 25}, {.lit = false}));

// INSTANSER — en prototyp på många platser; cellerna byggs EN gång.
std::vector<glaze::Instance> skog;
for (int i = 0; i < 100; ++i)
    skog.push_back({.at = {-30.0 + i % 10 * 6, 20.0 + i / 10 * 6, 0},
                    .scale = {1, 1, 0.8f + i % 5 * 0.1f}});
scen.addShape(lager, glaze::Shape::instances(glaze::Shape::cylinder({0, 0, 0}, 1.2, 5),
                                             std::move(skog),
                                             {.color = gfx::Color{50, 120, 60}}));

// PUNKTMOLN med en färgramp på skalären (här höjden).
std::vector<glaze::Point3> p;
std::vector<float>         hojd;
for (int i = 0; i < 1000; ++i) {
    p.push_back({40.0 + i % 40, i / 40.0, (i % 7) * 1.5});
    hojd.push_back(static_cast<float>((i % 7) * 1.5));
}
scen.addShape(lager, glaze::Shape::pointCloud(
    glaze::PointCloud::fromWorld(p, {}, std::move(hojd)),
    {.size = glaze::px(3), .color = gfx::Color{255, 255, 255},
     .ramp = {{{0, gfx::Color{0, 0, 128}}, {1, gfx::Color{255, 0, 0}}}, 0, 9}}));
```

### .glazeui
```cpp
scen.addLayer("Föremål");
scen.addShape(1, glaze::Shape::cylinder({674000, 6580010, 0}, 0.2, 12, {.color = gfx::Color{120, 90, 60}}, 16));
scen.addShape(1, glaze::Shape::mesh(glaze::Mesh::indexed({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}, {0, 1, 2, 0, 2, 3}, gfx::Color{0, 200, 0})));
scen.addShape(1, glaze::Shape::box({{674000, 6580000, 0}, {674002, 6580002, 30}}, glaze::MeshStyle{.color = gfx::Color{180, 180, 190}, .saturation = 0.5, .texture = glaze::img("toolbar", "info"), .contrast = 1.2, .brightness = 0.9}));
scen.addShape(1, glaze::Shape::instances(glaze::Shape::cylinder({0, 0, 0}, 0.3, 6), {{.at = {674000, 6580000, 0}, .yaw = 10}, {.at = {674005, 6580003, 0}}}, {.color = gfx::Color{255, 255, 255}}));
scen.addShape(1, glaze::Shape::points3({{674000, 6580000, 1}, {674001, 6580001, 2}}, glaze::PointStyle{.size = glaze::px(3), .shape = glaze::MarkerShape::Circle}));
scen.setLighting(glaze::Lighting{.ambient = 0.45, .diffuse = 0.7, .exponent = 1.5});
```
Meshar och punktmoln ur arrayer (`MeshDesc`, `fromWorld`, `PointCloud`) är koddata utan `.glazeui`-form; små meshar skrivs med `Mesh::triangles`/`Mesh::indexed`.

### Fallgropar
- Ogiltig data (fel längd på en ström, index utanför, inte hela trianglar) ger INGEN mesh och en logg. Framsidan är moturs sett utifrån.
- Bygg i valfri tråd, men ändra, rita och plocka i UI-tråden; ändrar appen en synlig mesh (`setPositions`/`setColors`) ska den invalidera ritytan.
- `keepCpuCopy = false` gör meshen oplockbar (och sparar inget minne).
- `Lod`-nivåerna står i växande `maxDistance`; 0 (obegränsat) bara sist — utan en sådan försvinner meshen på avstånd.
- PUNKTERNA STÅR PÅ MARKEN (sedan REQ9 7 också vid horisonten): deras nedre halva dras fram mot planet genom punkten så att marken inte skär dem, men högst en fjärdedel av djupet och högst fyra radier i världen — en punkt skyms av det som ligger framför, också av en närmare punkt i molnet. Inom fyra radier bakom en annan punkt, i en låg blick, kan kanten ändå titta fram.

<!-- END:canvas-meshes -->

<!-- BEGIN:canvas-terrain | terrain terräng mark ground drape drapering heightat markhöjd dem höjdmodell elevation-model liggande-text alignment-ground vägmarkering -->
## Terräng, drapering och liggande text (`canvas-terrain`)

**Alias:** terrain, terräng, mark, ground, drape, drapering, heightat, markhöjd, dem, höjdmodell, elevation-model, liggande-text, alignment-ground, vägmarkering
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [style3d.h](Glaze/widgets/include/glaze/canvas/style3d.h), [shape.h](Glaze/widgets/include/glaze/canvas/shape.h), [canvas.h](Glaze/widgets/include/glaze/widgets/canvas.h)

Ett lager med `LayerPlacement::Terrain` ÄR marken: dess meshar ritas med lagrets renderläge, och Ground-lagrens bilder, ytor, linjer och LIGGANDE text (`Alignment::Ground`) draperas på dem som vägmarkeringar. Stående etiketter och markörer i Ground-lagren står vid markhöjd. `heightAt(xy)` svarar med terrängens höjd — det en markföljande kamera står på.

### API
- `setLayerPlacement(lager, glaze::LayerPlacement::Terrain)` — meshar och ytor i rymden i lagret blir mark.
- `TextStyle::rotation = glaze::Alignment::Ground` — texten ligger i markplanet vid ankaret, med kartans vinkel (`angle`).
- `std::optional<double> heightAt(Point xy) const` — översta ytan i de synliga Terrain-lagren, finaste detaljnivån; tom där ingen terräng finns. Kostar ett uppslag i terrängens rutnät plus plattans sökstruktur (O(1) för plattor i ett rutnät), utan allokering.
- DRAPERINGEN (ingen API — så beter den sig): en platta draperas om när innehållet över den ändrats eller dess upplösning byts vid zoom, men BARA när den syns — en dold platta behåller sin gamla drapering och ritas om när den kommer i bild. Per bild ritas högst 8 Mi px (≈ två mål om 2048²) om: först synliga plattor utan drapering, sedan ändrat innehåll, sedan bytt upplösning, de största först. Det som inte hann visar den gamla draperingen och ritas i nästa bild, som ritytan ber om själv. `renderToBitmap` ritar alltid komplett.

### Exempel
```cpp
// scen är en glaze::Canvas med en Camera3D (se canvas-3d)
const int mark  = scen.addLayer("Terräng");
const int vagar = scen.addLayer("Vägar");   // bara 2D-former ⇒ Ground
scen.setLayerPlacement(mark, glaze::LayerPlacement::Terrain);

std::vector<glaze::Point3> horn;            // 9 × 9 hörn i världen
for (int j = 0; j < 9; ++j)
    for (int i = 0; i < 9; ++i)
        horn.push_back({i * 5.0, j * 5.0, 2.0 * std::sin(i * 0.4) * std::cos(j * 0.3)});
std::vector<std::uint32_t> index;
for (std::uint32_t j = 0; j < 8; ++j)
    for (std::uint32_t i = 0; i < 8; ++i) {
        const std::uint32_t a = j * 9 + i;
        index.insert(index.end(), {a, a + 1, a + 10, a, a + 10, a + 9});
    }
scen.addShape(mark, glaze::Shape::mesh(glaze::Mesh::fromWorld(horn, std::move(index)),
                                       {}, {.color = gfx::Color{104, 146, 78}}));

// Ground-lagrets väg och LIGGANDE text draperas på terrängen.
scen.addShape(vagar, glaze::Shape::polyline({{2, 2}, {20, 25}, {38, 30}},
    {.paint = gfx::Color{176, 152, 118}, .width = glaze::mm(1.4f)}));
scen.addShape(vagar, glaze::Shape::text({20, 22}, "Grusväg",
    {.size = glaze::mm(2.2f), .angle = 35, .rotation = glaze::Alignment::Ground}));

if (const std::optional<double> z = scen.heightAt({20, 20}))   // tom utanför
    setTitle("Markhöjd " + std::to_string(*z) + " m");
```

### .glazeui
```cpp
scen.addLayer("Föremål");
scen.addLayer("Terräng");
scen.addLayer("Vägar");
scen.setLayerPlacement(2, glaze::LayerPlacement::Terrain);
scen.addShape(3, glaze::Shape::text({674050, 6580020}, "Storgatan", glaze::TextStyle{.size = glaze::mm(2), .rotation = glaze::Alignment::Ground}));
```
Terrängens meshar ur höjddata är koddata; liggande text och draperade vägar är vanliga former.

### Fallgropar
- Med terräng finns Ground-lagrens geometri BARA genom draperingen — ett hål i terrängen visar det som ligger bakom.
- En liggande etikett på terräng varken kolliderar eller träffas; px och mm i draperingen mäts i dess eget mål.
- Instanser i ett Terrain-lager draperas inte.
- Lägg inga former i rymden i ett lager som ska draperas: en sådan form gör lagret till World (`Auto`).
- Innehållet kan SLÄPA några bilder under en stor ändring — en brickbatch över hela kartan, ett klassbyte vid zoom: det som hunnit draperas om visas nytt, resten i sin gamla upplösning eller med sitt förra innehåll tills budgeten hunnit ikapp.
- Varje ändring i ett Ground-lager draperar om plattorna under det, och `setShapes` på HELA lagret gör det under hela lagret. Ändra bara det som ändrats (`setShape`, `addShape`/`removeShape` per bricka), och lägg en markör eller ett hover-sken som flyttas varje bild i ett eget litet lager eller i overlayn (`onPaintOverlay` med `project`) — inte i ett lager med tusentals former.
- Den FÖRSTA `heightAt` efter en terrängändring (en ny platta, `setShape`, `Mesh::setPositions`, synlighet, placering, ett nytt Terrain-lager) bygger om rutnätet. Tusentals uppslag per bild (en redigerare som lägger en linje på marken) hör hemma i en cache i appen per geometriversion, inte i varje bild.

<!-- END:canvas-terrain -->

<!-- BEGIN:canvas-programs | program egna-program addprogram setprogram screenpass passstyle helskärmspass himmel sky skybox bakgrund programerror fallback reserv custom-shader shader -->
## Egna program och helskärmspass (`canvas-programs`)

**Alias:** program, egna-program, addprogram, setprogram, screenpass, passstyle, helskärmspass, himmel, sky, skybox, bakgrund, programerror, fallback, reserv, custom-shader, shader
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [style3d.h](Glaze/widgets/include/glaze/canvas/style3d.h), [canvas.h](Glaze/widgets/include/glaze/widgets/canvas.h), [canvas/CLAUDE.md](Glaze/widgets/include/glaze/canvas/CLAUDE.md)

Appens egna shaders registreras på ritytan med `addProgram` och nämns vid NAMN av `Shape::screenPass` (ett pass över hela ytan FÖRST i bildrutan — en himmel, en toning), `MeshStyle::program` och `PointStyle::program`. Ramverket har med flit ingen himmel och ingen skybox: appen bygger dem med verktyget. Kan programmet inte köras ritas RESERVEN — det inbyggda programmet för en mesh eller punkterna, passets `fallback`-färg — och felet loggas en gång. Källkoden och dess kontrakt står i `canvas-shaders`.

### API
- `struct Program { std::string name, hlsl, glslVertex, glslFragment, msl; std::string vertexEntry = "vsMain"; std::string fragmentEntry = "fsMain"; int textures = 0; int uniformFloats = 0; }`
- `int addProgram(Program p)` (−1 vid avslag) · `void setProgram(int index, Program p)` (ny version, kompileras om) · `void setPrograms(std::vector<Program> programs)` · `void removeProgram(int index)` · `int programCount() const` · `const Program* program(int index) const`
- `std::string programError(const std::string& name) const` — kompilatorns fel med appens radnummer; tomt för ett giltigt program och utan öppet fönster.
- `static Shape screenPass(std::string program, PassStyle style = {})` · `PassStyle` i fältordning: `uniforms` (GlazeUser, float4-packat), `textures`, `fallback` (alfa 0 = ingenting).
- `MeshStyle::program`/`uniforms`/`textures` och `PointStyle::program`/`uniforms` — samma program på en mesh eller punkter (se `canvas-meshes`).

### Exempel
```cpp
// scen är en glaze::Canvas med en Camera3D; toningProgram() i canvas-shaders
scen.addProgram(toningProgram());   // -1 + logg vid ogiltigt namn eller tak
scen.addShape(0, glaze::Shape::screenPass("toning",
    {.uniforms = {0.84f, 0.89f, 0.93f, 1, 0.36f, 0.55f, 0.81f, 1},
     .fallback = gfx::Color{214, 226, 238}}));   // när programmet inte kan köras

// I en handler, med fönstret öppet: kompilatorns fel med appens radnummer.
if (const std::string fel = scen.programError("toning"); !fel.empty())
    setTitle(fel);
```

### .glazeui
```cpp
scen.addLayer("Föremål");
scen.addShape(0, glaze::Shape::screenPass("himmel", glaze::PassStyle{.uniforms = {0.25, 0.45, 0.8, 1}, .fallback = gfx::Color{140, 180, 230}}));
scen.addShape(1, glaze::Shape::box(glaze::Box3{{674000, 6580000, 0}, {674001, 6580001, 1}}, glaze::MeshStyle{.color = gfx::Color{255, 255, 255}, .program = "glas", .uniforms = {1, 0, 0, 0.5}}));
```
`addProgram` och källorna är koddata utan `.glazeui`-form; previewn kör aldrig ett program utan ritar reserven.

### Fallgropar
- Namnen är unika på ritytan; högst 4 texturer och 1024 flyttal i GlazeUser — annars `gfx::log` och ingenting ändras.
- Egna program ritar inte instanser (det inbyggda tar dem, loggat).
- Programmet kompileras per fönster första gången det används; `programError` före `show()` svarar tomt.
- Passet ritas utan djup, träffas aldrig och ritas inte av `Painter`.

<!-- END:canvas-programs -->

<!-- BEGIN:canvas-shaders | shaders shader-källa hlsl glsl msl glazeframe glazeuser vsmain fsmain sv_vertexid gl_vertexid vertex_id attribut attributes location register d3d11 metal -->
## Shaderkällorna: HLSL, GLSL och MSL (`canvas-shaders`)

**Alias:** shaders, shader-källa, hlsl, glsl, msl, glazeframe, glazeuser, vsmain, fsmain, sv_vertexid, gl_vertexid, vertex_id, attribut, attributes, location, register, d3d11, metal
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [gfx/program.h](Glaze/gfx/include/gfx/program.h), [style3d.h](Glaze/widgets/include/glaze/canvas/style3d.h)

Ett program bär källan per backend: HLSL (D3D11, Windows), GLSL 330 med två `main()` (GL, Linux) och MSL (Metal, macOS) — bara den som kör behöver sin, men saknas den blir det reserven. Förspelet ger blocket `GlazeFrame` som `glaze`: `viewProj`, `invViewProj`, `eye`, strålbasen `forward`/`right`/`up`, `target`, `scale` (`.z` = lagrets opacitet), `pixelSize` och `rayCenter`. Blocket är std140 och 256 byte, med `rayCenter` sist. Strålen genom NDC (x, y) har riktningen `rayCenter + x·right + y·up`: `rayCenter` är lika med `forward` för ett symmetriskt frustum men förskjuten för ett asymmetriskt (ett headsets öga), så bygg alltid strålen ur `rayCenter`. `forward` är den optiska axeln — djupet längs blicken och `pixelSize`. Ett helskärmspass gör sina tre vertex själv ur vertex-id.

### API
- GlazeFrame: `register(b3)` · GL bindningspunkt 3 · Metal `constant GlazeFrame& glaze [[buffer(2)]]`.
- GlazeUser (appens std140-block, `uniformFloats` flyttal): `cbuffer GlazeUser : register(b4)` · `layout(std140) uniform GlazeUser` · Metal `[[buffer(3)]]`.
- Texturer: `t0–t3`/`s0–s3` · `uniform sampler2D GlazeTexture0`–`3` · Metal `texture(0–3)`.
- Meshens strömmar per PLATS: `ATTR<N>` · `layout(location = N)` · `[[attribute(N)]]` — läge 0, normal 1, färg 2, uv 3, skalär 4, user0–3 = 5–8.
- FÄRGJUSTERINGEN i förspelet (REQ8 3) — samma funktion som bilderna och det inbyggda Mesh-programmet kör: `float3 glaze_colorAdjust(float3 c, float3 a)` (HLSL, MSL) · `vec3 glaze_colorAdjust(vec3 c, vec3 a)` (GLSL), med en RAK färg 0–1 och `a` = (mättnad, kontrast, ljushet); (1, 1, 1) lämnar färgen exakt orörd.
- DE SKÄRMVÄNDA HÖRNENS DJUP i förspelet (REQ9 7) — samma funktion som Line-, Glyph- och Points-programmen kör, så ett eget program kan STÅ PÅ MARKEN som ramverkets primitiver: `float glaze_standingZ(float4x4 m, float4 c, float2 off)` (HLSL, MSL) · `float glaze_standingZ(mat4 m, vec4 c, vec2 off)` (GLSL). `c` är ankarets pixelklipp (`m` gånger punkten, oftast `glaze.viewProj`), `off` hörnets förskjutning i målets px (y nedåt); svaret är hörnets z i klippet bredvid `c.xy + off * c.w` och `c.w`. Hörnet får djupet av planet genom ankaret parallellt med xy där det ligger närmare — men draget är högst en ANDEL av ankarets djup (helt upp till ¼, inget från ½), så ett plan som nästan går genom ögat aldrig drar det mot närplanet. Gränsen: den nedre delen kan fortfarande dras fram ungefär en fjärdedel av sitt djup, mot det som ligger under planet genom ankaret. Prefixet `glaze_` är förspelets — ge aldrig egna funktioner det.

### Exempel
```cpp
glaze::Program toningProgram()
{
    glaze::Program p;
    p.name = "toning";                    // nyckeln: MeshStyle::program, screenPass
    p.hlsl = R"hlsl(
cbuffer GlazeUser : register(b4) { float4 uNere; float4 uUppe; };
struct VsOut { float4 pos : SV_Position; float2 ndc : TEXCOORD0; };
VsOut vsMain(uint id : SV_VertexID)
{
    VsOut o;
    float2 q = float2((id << 1) & 2, id & 2);
    o.ndc = q * float2(2.0, -2.0) + float2(-1.0, 1.0);
    o.pos = float4(o.ndc, 0.0, 1.0);
    return o;
}
float4 fsMain(VsOut i) : SV_Target
{
    float3 dir = normalize(glaze.rayCenter.xyz + i.ndc.x * glaze.right.xyz +
                           i.ndc.y * glaze.up.xyz);
    return float4(lerp(uNere.rgb, uUppe.rgb, saturate(dir.z)), glaze.scale.z);
}
)hlsl";
    p.glslVertex = R"glsl(
out vec2 vNdc;
void main()
{
    vec2 q = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vNdc        = q * vec2(2.0, -2.0) + vec2(-1.0, 1.0);
    gl_Position = vec4(vNdc, 0.0, 1.0);
}
)glsl";
    p.glslFragment = R"glsl(
layout(std140) uniform GlazeUser { vec4 uNere; vec4 uUppe; };
in vec2 vNdc;
out vec4 frag;
void main()
{
    vec3 dir = normalize(glaze.rayCenter.xyz + vNdc.x * glaze.right.xyz +
                         vNdc.y * glaze.up.xyz);
    frag = vec4(mix(uNere.rgb, uUppe.rgb, clamp(dir.z, 0.0, 1.0)), glaze.scale.z);
}
)glsl";
    p.msl = R"msl(
struct User  { float4 nere; float4 uppe; };
struct VsOut { float4 pos [[position]]; float2 ndc; };
vertex VsOut vsMain(uint id [[vertex_id]])
{
    VsOut  o;
    float2 q = float2((id << 1) & 2, id & 2);
    o.ndc = q * float2(2.0, -2.0) + float2(-1.0, 1.0);
    o.pos = float4(o.ndc, 0.0, 1.0);
    return o;
}
fragment float4 fsMain(VsOut i [[stage_in]],
                       constant GlazeFrame& glaze [[buffer(2)]],
                       constant User& user [[buffer(3)]])
{
    float3 dir = normalize(glaze.rayCenter.xyz + i.ndc.x * glaze.right.xyz +
                           i.ndc.y * glaze.up.xyz);
    return float4(mix(user.nere.rgb, user.uppe.rgb, saturate(dir.z)), glaze.scale.z);
}
)msl";
    p.uniformFloats = 8;                  // två float4 i GlazeUser
    return p;
}
```

### Fallgropar
- Ett fel rapporteras med raden i appens EGEN källa (förspelet räknas inte) — läs `programError(namn)`.
- En stråle byggd som `forward + x·right + y·up` stämmer bara för ett symmetriskt frustum; i ett asymmetriskt (ett externt mål med eget frustum) blir riktningen fel. Använd `rayCenter`.
- I GLSL: läs aldrig fragmentets läge ur `gl_FragCoord.y` (målen är y-vända); ta det ur en varying.
- Läser programmet en plats meshen saknar ritas anropet med reserven.

<!-- END:canvas-shaders -->

<!-- BEGIN:canvas-views | view views del-vy delvy subview viewport navigeringskub viewcube minikarta minimap followrotation addview setlayerviews rendertobitmap miniatyr thumbnail screenshot -->
## Del-vyer och 3D till bild (`canvas-views`)

**Alias:** view, views, del-vy, delvy, subview, viewport, navigeringskub, viewcube, minikarta, minimap, followrotation, addview, setlayerviews, rendertobitmap, miniatyr, thumbnail, screenshot
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [style3d.h](Glaze/widgets/include/glaze/canvas/style3d.h), [canvas.h](Glaze/widgets/include/glaze/widgets/canvas.h)

En DEL-VY (`glaze::View`) är en egen kamera i en ruta ovanpå huvudvyn — en navigeringskub, en axeltriad, en andra vy — med eget djup och egen MSAA. Lagren ritas i de vyer `setLayerViews` säger (huvudvyn är 0 och har alla lager från start). `followRotation` vrider vyn som huvudvyn men låter mål och avstånd vara vyns. `renderToBitmap` ritar huvudvyns lager med en egen kamera i en `Bitmap` — en miniatyr.

### API
- `View` i fältordning: `anchor` (`TextAnchor`, förval `TopLeft`), `offset` (`Point`), `width`, `height` (ritytans lokala logiska px, 0 = hela), `camera` (`Camera3D`), `followRotation`, `background` (alfa 0 = genomskinlig).
- `int addView(View v)` (svarar med numret 1, 2 … eller −1) · `void setView(int view, View v)` · `void setViews(std::vector<View> views)` · `void removeView(int view)` · `int viewCount() const` · `const View* view(int view) const`
- `void setLayerViews(int layer, std::vector<int> views)` · `std::vector<int> layerViews(int layer) const` — `{0}` från start, `{}` = ingen vy.
- `std::optional<Bitmap> renderToBitmap(int w, int h, const Camera3D& camera)` — fysiska px; tomt + logg utan öppet fönster.

### Exempel
```cpp
class Kub : public glaze::AppWindow {
public:
    Kub() : glaze::AppWindow("Kub", 800, 600)
    {
        scen.dock = glaze::Dock::Fill;
        scen.setCamera(glaze::Camera3D{.distance = 12, .azimuth = 35, .elevation = 25});
        scen.setRotatable(true);
        scen.setPitchable(true);
        scen.addShape(glaze::Shape::box({{-3, -3, 0}, {3, 3, 2}}));   // lager 0

        const int kub = scen.addLayer("Kub");
        scen.addShape(kub, glaze::Shape::box({{-1, -1, -1}, {1, 1, 1}},
                                             {.color = gfx::Color{200, 208, 222}}));
        const int vy = scen.addView(glaze::View{
            .anchor = glaze::TextAnchor::TopRight, .offset = {-8, 8},
            .width = 96, .height = 96,
            .camera = glaze::Camera3D{.distance = 6,
                                      .projection = glaze::Projection::Orthographic},
            .followRotation = true});     // vrids med huvudvyn, flyttas inte
        scen.setLayerViews(kub, {vy});    // BARA i vyn (huvudvyn är 0)

        bild.setText("Miniatyr");
        bild.dock = glaze::Dock::Bottom;
        bild.onClick = [this] {           // kräver ett öppet fönster
            const std::optional<glaze::Bitmap> b = scen.renderToBitmap(256, 256,
                glaze::Camera3D{.distance = 14, .azimuth = 45, .elevation = 30});
            if (b)
                setTitle("Miniatyr " + std::to_string(b->width()) + " px");
        };

        root().adopt(bild);
        root().adopt(scen);
    }

private:
    glaze::Canvas scen;
    glaze::Button bild;
};
```

### .glazeui
```cpp
scen.addLayer("Föremål");
scen.addLayer("Kub");
scen.addView(glaze::View{.anchor = glaze::TextAnchor::TopRight, .offset = {-8, 8}, .width = 96, .height = 96, .camera = glaze::Camera3D{.distance = 6, .projection = glaze::Projection::Orthographic}, .followRotation = true});
scen.setLayerViews(2, {1});
scen.removeView(1);
```

### Fallgropar
- Ett lager som läggs i `{1}` försvinner ur huvudvyn — skriv `{0, 1}` för båda.
- `removeView` flyttar senare vyer ett nummer ned, och lagrens vylistor följer med.
- Rutnätet och `onPaint` hör till huvudvyn. `renderToBitmap` tar inte med del-vyerna och får inte anropas inifrån en ritning — anropa den i en handler (fönstret får vara dolt men måste vara öppet).
- Sidopaketet `openxr` ritar bara HUVUDVYNS lager (rutnätet, terrängen, djupet, världen, etiketterna och `onPaint`/`onPaintLayer`) i headsetets bilder; del-vyerna, kanten och fokusringen hör till fönstret och följer inte med. Etiketterna placeras per öga utan att röra skärmens eller bitmappens placering.

<!-- END:canvas-views -->

<!-- BEGIN:canvas-first-person | förstaperson first-person fps wasd gå walk promenad ontick tick iskeyheld lookfrom heightat markföljning setticking onkeydown onkeyup tangenter keys animation spelloop repeat -->
## Förstaperson och egen rörelse: onTick, isKeyHeld, lookFrom (`canvas-first-person`)

**Alias:** förstaperson, first-person, fps, wasd, gå, walk, promenad, ontick, tick, iskeyheld, lookfrom, heightat, markföljning, setticking, onkeydown, onkeyup, tangenter, keys, animation, spelloop, repeat
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [canvas.h](Glaze/widgets/include/glaze/widgets/canvas.h), [camera.h](Glaze/widgets/include/glaze/canvas/camera.h), [app_window.h](Glaze/widgets/include/glaze/app_window.h)

Förstapersonen är APPENS kod, inte en inbyggd gest — ramverket ger byggstenarna. En tangent som `onKeyDown` TAR (`e.handled = true`) håller ritytan tickande så länge den hålls nere; `onTick` körs en gång per bildruta med tiden sedan förra (`e.seconds`), läser de hållna tangenterna med `AppWindow::isKeyHeld` och sätter en ny kamera med `Camera3D::lookFrom`. När ingen tangent hålls ber ritytan inte om fler bildrutor — ingen render-loop.

### API
- `Handler<KeyArgs> onKeyDown` / `Handler<KeyArgs> onKeyUp` — basens tangenthändelser (`events`), alltså samma fält på ritytan som på varje widget; `KeyArgs`: `key`, `mods`, utdata `handled`, `repeat` (OS:ets upprepning medan tangenten hålls) och `cancelled` (släppet som fokusförlusten gav).
- `Handler<TickArgs> onTick` — `TickArgs`: `double seconds` (högst 0,1).
- `void setTicking(bool on)` / `bool ticking() const` — tickning utan tangent (t.ex. medan något glider).
- `bool AppWindow::isKeyHeld(gfx::Key k) const` — töms vid fokusförlust.
- `static Camera3D lookFrom(Point3 eye, float yaw, float pitch, float fov = 45.0f, UpAxis up = UpAxis::Z)` — yaw 0 = norr, medurs; pitch UPPÅT; elevationen blir −pitch.
- `std::optional<double> heightAt(Point xy) const` — marken att följa (se `canvas-terrain`).
- `Handler<GestureArgs> onGesture` — styrplattans nypning/vridning före den inbyggda gesten (`scale`, `rotation`, `local`, `handled`).

### Exempel
```cpp
class Promenad : public glaze::AppWindow {
public:
    Promenad() : glaze::AppWindow("Promenad", 800, 600)
    {
        scen.dock = glaze::Dock::Fill;
        scen.setCamera(glaze::Camera3D::lookFrom({0, -20, 2}, 0, 0));   // öga, yaw, pitch
        scen.addShape(glaze::Shape::box({{-2, -2, 0}, {2, 2, 4}}));

        scen.onKeyDown = [](glaze::Widget*, glaze::KeyArgs& e) {
            // En TAGEN tangent håller ritytan tickande så länge den hålls nere.
            e.handled = e.key == gfx::Key::W || e.key == gfx::Key::S ||
                        e.key == gfx::Key::A || e.key == gfx::Key::D;
        };
        scen.onTick = [this](glaze::Widget*, glaze::TickArgs& e) {
            const auto hall = [this](gfx::Key k) { return isKeyHeld(k) ? 1.0 : 0.0; };
            const double fram = hall(gfx::Key::W) - hall(gfx::Key::S);
            const double vrid = hall(gfx::Key::D) - hall(gfx::Key::A);
            const glaze::Camera3D c = scen.camera3D();
            const float  yaw = c.azimuth + static_cast<float>(vrid * 60.0 * e.seconds);
            const double a   = yaw * std::numbers::pi / 180.0;   // 0 = norr, medurs
            glaze::Point3 oga = c.eye();
            oga.x += std::sin(a) * fram * 5.0 * e.seconds;        // 5 m/s
            oga.y += std::cos(a) * fram * 5.0 * e.seconds;
            oga.z = scen.heightAt({oga.x, oga.y}).value_or(0.0) + 2.0;   // följ marken
            scen.setCamera(glaze::Camera3D::lookFrom(oga, yaw, -c.elevation, c.fov));
        };
        glaze::AppWindow::onLoad = [this] { scen.focus(); };   // tangenterna hit

        root().adopt(scen);
    }

private:
    glaze::Canvas scen;
};
```

### .glazeui
```cpp
scen.setCamera(glaze::Camera3D::lookFrom({0, -20, 2}, 0, 0));
scen.setTicking(false);
scen.onKeyDown = [this](glaze::Widget* s, glaze::KeyArgs& e) { scenTangent(s, e); };
scen.onTick = [this](glaze::Widget* s, glaze::TickArgs& e) { scenTick(s, e); };
```

### Fallgropar
- Tangenterna går bara till ritytan när den har FOKUS — `scen.focus()` (i `onLoad`) eller ett klick.
- Utan `onTick`-handler tickar ritytan inte alls.
- Slå av orbitgesterna (`setRotatable(false)` …) eller ta musen (`e.handled = true`) när draget ska vara blicken.
- Numpadens tangenter är egna poster (`gfx::Key::KP_…`); `isKeyHeld(gfx::Key::End)` svarar också för numpadens tvilling.
- `isKeyHeld` är för det som frågas PER BILDRUTA (gå så länge W hålls). Ett LÄGE som slås på av trycket och av av släppet — panorera medan mellanslaget hålls — lyssnar på `onKeyUp`, som också kommer (med `e.cancelled`) när fönstret tappar fokus mitt i. `e.repeat` skiljer OS:ets upprepning från ett nytt tryck.
- Ett textfält på ritytan behåller sina tangenter: ritytans `onKeyDown` ser bara det som går till ritytan själv.
- Efter `onTick` ritas bara RITYTAN om, inte fönstret. En HUD i en `Label` eller statusraden uppdateras med sättaren (`hud.setText(…)`), som ritar om den; ett eget fält som en annan widgets ritning läser (en annan ritytas `onPaint`) kräver den widgetens `invalidate()`. Overlay-lagret (`onPaintOverlay`) ritas vid varje present och följer med.
- Ticken körs i pumpens svans FÖRE appens postade funktioner och utan postköns budget (`app`), så en skur av trådarnas svar skjuter inte upp rörelsen till en bild utan steg.

<!-- END:canvas-first-person -->

<!-- BEGIN:appearance | appearance utseende theme tema style stil ui setui settheme setstyle setthememode dark-mode mörkt-läge palett colors färger slate minimalistic hue designrad -->
## Utseendet — UI, Theme, Style och läget (`appearance`)

**Alias:** appearance, utseende, theme, tema, style, stil, ui, setui, settheme, setstyle, setthememode, dark-mode, mörkt-läge, palett, colors, färger, slate, minimalistic, hue, designrad
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [style.h](Glaze/widgets/include/glaze/style.h), [theme.h](Glaze/widgets/include/glaze/theme.h), [app_window.h](Glaze/widgets/include/glaze/app_window.h), [CLAUDE.md](CLAUDE.md) (hård regel 3)

Utseendet är tre oberoende axlar plus läget, och det är APP-GLOBALT: en setter på vilket fönster som helst ritar om alla öppna fönster. **UI** väljer ritningen (formen), **Theme** paletten (alla färger, en ljus och en mörk variant), **Style** måtten i logiska px (även UI-fontens storlek), **ThemeMode** System/Light/Dark. Varje UI äger sina egna teman och styles — uppslaget går alltid via UI:t.

### API
- `enum class UI { Glaze, Slate, Minimalistic };` · `void setUI(UI u);` (ÅTERSTÄLLER tema och stil till familjens förval) · `UI ui() const;`
- `void setTheme(const Theme& t);` · `const Theme& theme() const;` · `void setColorsLight(const Theme::Colors& c);` · `void setColorsDark(const Theme::Colors& c);`
- `void setStyle(const Style& s);` · `const Style& style() const;` · `void setThemeMode(ThemeMode m);` · `enum class ThemeMode { System, Light, Dark };`
- `int themeCount(UI u);` · `const Theme& themeAt(UI u, int index);` · `const Theme& defaultTheme(UI u);` — Slate: "Windows" + nio hues, Minimalistic: nio hues, Glaze: "Lavender". Index 0 = förval.
- `int styleCount(UI u);` · `const Style& styleAt(UI u, int index);` · `const Style& defaultStyle(UI u);` — i dag en per UI, alla heter "Regular".
- `static const Theme& slate(Hue h);` · `static const Theme& minimalistic(Hue h);` · `enum class Hue { Neutral, Red, Orange, Amber, Green, Teal, Blue, Indigo, Violet };`

### Exempel
```cpp
void pickAppearance(glaze::AppWindow& win)
{
    win.setUI(glaze::UI::Slate);                           // ritningen — återställer tema + stil
    win.setTheme(glaze::Theme::slate(glaze::Hue::Teal));   // en av Slates tio paletter
    win.setStyle(glaze::styleAt(win.ui(), 0));             // måtten ("Regular")
    win.setThemeMode(glaze::ThemeMode::Dark);              // läget: System/Light/Dark

    for (int i = 0; i < glaze::themeCount(win.ui()); ++i)  // underlag för en väljare
        gfx::log("tema %d: %s", i, glaze::themeAt(win.ui(), i).name.c_str());

    glaze::Theme::Colors c = win.theme().light.colors;     // egen accent i ljust läge
    c.accent = gfx::Color{0x33, 0x66, 0xFF, 255};
    win.setColorsLight(c);
}
```

### .glazeui
Ingen sats — `setUI` i genererad kod vore per-fönster-tema bakvägen och är förkastat. Filen kan bära en DESIGNRAD, en kommentar som bara GlazeBuilder, glaze-render och VS Code läser (ritbordets utseende, inte appens):
```cpp
// glazeui-design: ui=Minimalistic theme=minimalistic(Violet) style=0 lang=sv
```
Nycklar: `ui=Glaze|Slate|Minimalistic`, `theme=<familj>(<Namn>)`, `style=<index>`, `lang=sv|en|nb|fi`. Inga blanksteg i värdena, högst en designrad per fil.

### Fallgropar
- Ordningen: `setUI` först, sedan `setTheme`/`setStyle` — `setUI` skriver över de två andra.
- Förvalet är `UI::Glaze` med "Lavender" och läget `System`, som följer OS:ets ljus/mörkt även i drift.
- Namn får sammanfalla mellan UI:n och betyda olika saker ("Regular" finns tre gånger, "Neutral" två) — identifiera ett tema med UI + namn, och jämför `Style::name` med `std::string_view` (det är en `const char*`).
- Egna färger i en app: ändra rollerna i `Theme::Colors` (`setColorsLight`/`setColorsDark`), aldrig hårdkodade färger i en widget. De inbyggda paletterna är kontrastgrindade; dina egna är det inte.
- Designraden styr bara previewn. Ska appen se likadan ut i drift anropar den `setUI`/`setTheme` själv, t.ex. i `onLoad`.
<!-- END:appearance -->

<!-- BEGIN:custom-widget | custom-widget egen-widget own-widget usercontrol kontroll widget draw contentsize glaze_register_widget registrera makewidget setprop settexttoken ctx stand-in palett capture pointercapture mousecancel rightdown middledown keyup keytaken -->
## Egen widget — ärv `Widget` (`custom-widget`)

**Alias:** custom-widget, egen-widget, own-widget, usercontrol, kontroll, widget, draw, contentsize, glaze_register_widget, registrera, makewidget, setprop, settexttoken, ctx, stand-in, palett, capture, pointercapture, mousecancel, rightdown, middledown, keyup, keytaken
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [widget.h](Glaze/widgets/include/glaze/widget.h), [draw.h](Glaze/widgets/include/glaze/draw.h), [button.h](Glaze/widgets/include/glaze/widgets/button.h), [button.cpp](Glaze/widgets/src/widgets/button.cpp), [widgets/CLAUDE.md](Glaze/widgets/include/glaze/widgets/CLAUDE.md)

En egen widget är en header + en cpp och ser ut exakt som de inbyggda (mallen är `button.h`/`button.cpp`): `draw()` och `contentSize()` i widgetens egen cpp, formen vald med `switch (c.ui)` där `default:` är Slate-vägen, färger ur `c.theme->colors`, mått ur `c.style`, ritning via `Draw::` i widget-relativa logiska px. Inga tal och inga färger i widgetfilen.

### API
- Överrid `virtual void draw(const Ctx& c) = 0;` (rita mot `c.bounds()`, aldrig `resolve()` eller `* c.scale`), `virtual gfx::Sizef contentSize(const Ctx& c) const;` (LOGISKA px, läs aldrig `c.box`), `virtual bool setProp(const std::string& key, const std::string& value);` (basen SIST) och vid översatta textfält `virtual bool setTextToken(const std::string& key, const TrText& value)`.
- Beror höjden på bredden (text som bryts, en bild med bevarat bildförhållande): överrid `virtual std::optional<float> heightForWidth(const Ctx& c, float width) const` — LOGISKA px in och ut, `std::nullopt` = "beror inte på bredden" (basens svar), och svara `nullopt` när en `height` är satt (axelregeln). Layouten frågar den BARA med en bredd den gett: när widgeten är sträckt mellan två kanter, och för en `Top`/`Bottom`-dockad med remsans bredd. `Widget::measureHeightForWidth(w, c, width)` är `measure`s parhäst för en komposit som vill fråga ett barn. Mät i kroken med krokens `c` — aldrig en fråga som själv resolvar dig (den hade gått runt i layouten); en rityta mäter sin text med `measureText(c, …)` (`canvas-painter`).
- `GLAZE_REGISTER_WIDGET(typeName, Class)` i widgetens cpp ⇒ `std::unique_ptr<Widget> makeWidget(const std::string& type);` känner typen. Basens hjälp: `void invalidate();`, `gfx::Color textColor(const Ctx& c) const`, `gfx::Color hotBorder(const Ctx& c) const`, `hovered()`, `pressed()`, `focused()`; primitiverna `Draw::rect`/`frame`/`text`/`content`/`measure`/`lineHeight`/`clip`/`icon`, och `static Ctx Draw::withFont(const Ctx& c, gfx::FontFamily family, gfx::FontStyle style = gfx::FontStyle::Regular, Length size = {})` — Ctx:t med Mono, fet/kursiv eller en egen storlek (px/mm) ur fönstrets textfonter; mät och rita med det svarade Ctx:t (utan textfonter, eller i världsenheter, svaras `c`). Färghjälparna för en widget som HÄRLEDER färger ur temat: `glaze/color.h` — `glaze::color::hsl`/`mix`/`luminance`/`contrast`/`hueDegrees(Hue)`.
- Beteende: `virtual bool focusable() const`, `virtual bool keyDown(gfx::Key, std::uint32_t)`, `virtual void mouseDown(const Ctx&, float, float)`, `virtual bool mouseUp(const Ctx&, float, float)`, `mouseDrag`, `mouseDoubleClick`, `virtual bool mouseWheel(const Ctx&, float, float, float, float)`, `virtual gfx::PointerShape cursorAt(const Ctx&, float, float) const` — krokarnas koordinater är FYSISKA fönster-px.
- MITTEN- OCH HÖGERKNAPPEN (protected, samma form): `virtual bool middleDown(const Ctx&, float, float)` / `rightDown` — svara true för att TA trycket (det bubblar annars till förfäderna; ett otaget högertryck blir snabbmenyn) — och sedan `middleDrag`/`rightDrag` för varje rörelse och `middleUp`/`rightUp` en gång vid släppet (true = en handler kördes, hela fönstret ritas om). Ingen fokusflytt och ingen `pressed()`: läs `middlePressed()`/`rightPressed()` — som redan är sanna INNE i din `middleDown`/`rightDown`: öppnar kroken en modal dialog och fönstret tappar fokus kommer `mouseCancel` medan kroken kör, och ett tryck som tog slut så återställs aldrig vad kroken än svarar (fråga `middlePressed()` innan du börjar något efter en handler). `virtual void middleDoubleClick(const Ctx&, float, float)` / `rightDoubleClick` kommer efter det andra trycket du tog.
- AVBROTTET (protected): `virtual bool mouseCancel(gfx::MouseButton b)` — ett tryck du skulle ha fått släppet för kom aldrig att släppas: fönstret tappade fokus, OS:et tog musen, ett annat fönster tog över trycket eller ett nytt tryck med samma knapp kom först. Bokföringen är redan nollad (`pressed()`, `middlePressed()`, `rightPressed()` falska), inget `mouseUp`/`…Up` kommer, så AVBRYT draget (committa inte) och posta ett eventuellt event med `postEvent`; svara true om en användar-handler kördes. `b` är knappen — `None` för en capture utan knapp nere. Kroken anropas inte när widgeten tas bort ur trädet eller när drag-and-drop tog trycket.
- APPENS MUS (protected): fönstret fyrar `onMouseDown`/`onMouseUp`/`onMouseCancel` FÖRE dina krokar (`events`). Ett tryck appen TOG (`handled`) når inte `mouseDown`/`middleDown`/`rightDown` utan `virtual void appPress(const Ctx&, float x, float y, gfx::MouseButton b, AppPressStep steg)` — `Begin` vid trycket, `Move` vid varje rörelse var pekaren än står, `DoubleClick` vid det andra trycket; förval ingenting. Tog appen SLÄPPET från ditt eget tryck får du `mouseCancel` i stället för `mouseUp`. `virtual bool fireMouse(const Handler<MouseArgs>& h, const Ctx& c, std::optional<gfx::Pointf> pos, gfx::MouseButton b)` bygger args: overrida för en ättling till `MouseArgs` (ritytans form), anropa `h(this, e)` och svara `e.handled`.
- TANGENTERNA (protected): `virtual bool keyUp(gfx::Key, std::uint32_t)` — släppet av en tangent din widget tog (också det syntetiska vid fokusförlust), efter appens `onKeyUp`-handlers; `virtual void keyTaken(gfx::Key)` — en `onKeyDown`-handler på dig satte `handled` (ritytan håller sin tickning vid liv på det). Appens `onKeyDown` körs FÖRE din `keyDown`.
- Musens CAPTURE (protected): `void setPointerCapture()` / `void releasePointerCapture()` — ALL mus till mig tills jag släpper, också efter knappsläppet (`mouseUp` kommer; släpp själv). `bool takePointerCapture()` — samma sak för ett tryck som började i ett ANNAT av appens fönster: rörelserna och släppet kommer hit, och det fönstret avbryter sitt tryck (dess widgets får `mouseCancel`); true bara på Windows och bara när appen är förgrund (macOS och Linux ger redan fönstret där trycket började hela trycket), annars false och ingenting ändrat. Stänger, flyttar eller tar en annan widgets krok trycket vidare under övertagandet blir svaret också false, och övertagandet städas. `bool hasPointerCapture() const` (publik) svarar om jag håller capturen just nu. Tas capturen ifrån mig får jag `mouseCancel` (`Left` med vänsterknappen nere, annars `None`) — inte när jag själv släpper.
- En CONTAINER som håller tillstånd om sina barn: `virtual void childAdded(Widget&)` · `childRemoved(Widget&)` · `childVisibilityChanged(Widget&)` (ett barns `show()`/`hide()`, efter bytet) · `childLayoutChanged(Widget&)` (barnets plats i flödet ändrades — det kallade `relayoutSiblings()`: synlighet, remstjocklek eller eget mått, som verktygsknappens text och bild; för en container vars EGNA mått beror på barnens) — bara eget tillstånd, aldrig en användar-handler. En egen widget vars sättare ändrar dess mått i en förälder som placerar själv kallar `relayoutSiblings()` efter `invalidate()`. `virtual bool showsChildren() const` (false = mina barn syns inte just nu — Tab går inte in i dem, fast jag själv syns; en inaktiv flik), `virtual TrText toolTipAt(const Ctx&, float x, float y) const` (tooltip per DEL för delar som är data) och `virtual Widget* frontChild() const` (barnet som ritas SIST och träffas FÖRST utan att barnordningen rörs — en overlay som glider fram, en utfälld låda; nullptr = inget). `static bool subtreeHasOpenPopup(const Widget&)` (protected) frågar om en lista eller meny i ett delträd är öppen. `virtual void shownChanged()` säger att widgeten eller fönstret visades eller doldes (också när fönstret öppnas) — för det som bara ska gå medan något syns.
- FLYTTA ett barn till en annan förälder, också i ett annat fönster: `bool transferChild(Widget& child, Widget& to)` (WinForms `control.Parent = annan`). ÄGANDET följer med — ett ägt barn förblir ägt, ett lånat förblir lånat — och en nekad flytt (en öppen popup i delträdet, målet är barnet självt eller ligger i dess delträd) lämnar båda träden orörda och loggar. Fokus och tryck städas i det gamla fönstret. `appWindow()` svarar `nullptr` också när fönstret widgeten satt i har rivits.
- Events (protected): `bool fireEvent(Handler<Args>& h, Args args = {})` kör en handler och `bool firePair(Handler<Args>& first, Handler<Args>& second, const Args& args = {})` ett par för samma handling (`onChange` + `onCommit`); båda svarar om widgeten går att röra efteråt — false betyder att en handler rev den, lossade den eller rev dess fönster, och då returnerar du direkt. `HandlerGuard(Widget&)` + `ok()` är samma fråga för egen kod (en modal pump, en nästlad handler); `lifeToken()` är livstecknet den bygger på.
- Fokus: `virtual void focusGained()` · `virtual void focusLost()` (anropas mitt i fokusbytet) och `void postEvent(std::function<void()> fire)` — kör `fire` i pumpens svans om vakten från posten står sig (widgeten lever, sitter kvar i samma fönster, fönstret lever), och ritar sedan om fönstret (utan ägarfönster: no-op). En egen inmatande widget skriver sitt värde direkt i `focusLost` och fyrar så: `postEvent([this] { firePair(onChange, onCommit); });`. `virtual void commitEdit()` committar det som väntar NU (`AppWindow::commitEdits()` når den). `bool pressGaveFocus() const` säger i `mouseDown` om trycket gav fokus.

### Exempel
`meter.h` (efter `#pragma once` och `#include <glaze/glaze.h>`) och `meter.cpp`:
```cpp
class Meter : public glaze::Widget {
public:
    void  setValue(float v);                     // settern invaliderar
    float value() const { return value_; }
    bool  setProp(const std::string& key, const std::string& value) override;

protected:
    gfx::Sizef contentSize(const glaze::Ctx& c) const override;
    void       draw(const glaze::Ctx& c) override;

private:
    float value_ = 0.0f;
};
```
```cpp
#include "meter.h"

#include <cstdlib>

GLAZE_REGISTER_WIDGET("meter", Meter)   // makeWidget("meter") hittar typen

void Meter::setValue(float v)
{
    if (v == value_)
        return;
    value_ = v;
    invalidate();                       // bara den här widgeten ritas om
}

bool Meter::setProp(const std::string& key, const std::string& value)
{
    if (key == "value") {
        setValue(std::strtof(value.c_str(), nullptr));
        return true;
    }
    return glaze::Widget::setProp(key, value);   // basen SIST
}

gfx::Sizef Meter::contentSize(const glaze::Ctx& c) const   // LOGISKA px, satt axel vinner
{
    gfx::Sizef s{width, height};
    if (s.w <= 0.0f) s.w = c.style->fieldDefaultW;
    if (s.h <= 0.0f) s.h = glaze::Draw::lineHeight(c) + 2.0f * c.style->fieldPadY;
    return s;
}

void Meter::draw(const glaze::Ctx& c)   // MÅTTEN ur c.style, FÄRGERNA ur c.theme
{
    const gfx::Rectf b = c.bounds();                         // eget rum, origo i eget hörn
    const gfx::Rectf fill{b.x, b.y, b.w * value_, b.h};
    const glaze::Pen pen{hotBorder(c), c.style->borderWidth};
    switch (c.ui) {
    case glaze::UI::Minimalistic:                            // kanten är formen
        glaze::Draw::rect(c, fill, c.style->cornerRadius, c.theme->colors.accent);
        glaze::Draw::frame(c, b, c.style->cornerRadius, pen);
        break;
    default:                                                 // Slate-vägen (Glaze faller hit)
        glaze::Draw::rect(c, b, c.style->cornerRadius, c.theme->colors.panel, pen);
        glaze::Draw::rect(c, fill, c.style->cornerRadius, c.theme->colors.accentSurface);
        break;
    }
}
```

### .glazeui
Medlemsraden `Meter level;` i regionen och satserna `level.name = "level";`, `level.width = 160;`, `level.setValue(0.4f);` fungerar som för `glaze::`-typer. Verktygen skapar typen ur REGISTRET på dess FULLA klassnamn — så som medlemsraden skriver det, med namnrymden (`app::Meter level;` för en klass i `namespace app`): i ett glaze-render- eller GlazeBuilder-bygge som länkar appens widgets (`-DGLAZE_TOOLS=ON`, `cmake`) ritas den på riktigt, och `set<Fält>(v)` utförs genom widgetens `setProp` när fältnamnet är ett av ramverkets setter-fält (`value`, `text` …). Den buntade glaze-render i VS Code känner bara ramverkets typer och ritar en STAND-IN (ruta + klassnamn).

### Fallgropar
- En app kan inte lägga fält i `Style` eller roller i `Theme::Colors` (de är ramverkets) — återanvänd befintliga mått och roller som betyder rätt sak.
- Settern invaliderar BARA vid ändring; tillstånd som ändras i en mus- eller tangentkrok invaliderar widgeten själv (ingen handler körs där).
- En egen dragflagga som bara nollas i `mouseUp` står kvar efter Alt+Tab mitt i draget — ett avbrutet tryck får ingen `mouseUp`. Nolla den i `mouseCancel` (eller läs draget TILLSAMMANS med `pressed()`).
- Den fångande widgeten får `mouseDrag` bara med vänsterknappen nere efter sin egen `middleDrag`/`rightDrag` i samma rörelse — EN rörelse per rörelse och knapp.
- `focusGained()`/`focusLost()` får ALDRIG köra användarkod — fokusbytet pågår. Ett event därifrån går genom `postEvent`.
- Rör aldrig `this` efter en handler utan att fråga svaret (`fireEvent`/`firePair`/`HandlerGuard::ok()`): en handler får riva widgeten, och ett tvåstegsevent som fyras rakt av läser det andra eventets handler ur frigjort minne.
- Registrera i en källfil som länkas in i EXE:n — i ett eget statiskt bibliotek kan länkaren stryka registreringen. Typnamnet måste vara unikt (första vinner, dubbletten loggas). En widget i en plugin-DLL registrerar sig likadant i delat läge och syns i programmets `makeWidget` när DLL:en laddats (`shared-library`).
- Ritar widgeten utanför sin rect (skugga, fokusring) måste `virtual gfx::Rectf paintBounds(const Ctx& c) const` täcka det, annars blir spökpixlar kvar.
- `draw()`, `drawAfterChildren()` och `drawOverlay()` anropas INTE när widgetens rect är tom (bredden eller höjden ≤ 0 — en dold överflödsknapp, en inaktiv dockningsflik); barnen vandras ändå. Lägg alltså inget tillstånd i `draw()` som måste stegas utan yta: det hör hemma i `virtual void frameStep()` (utan Ctx — den resolvar ingenting), som fönstret anropar en gång per bild för varje synlig widget, före skadeinsamlingen och oavsett skadan och ytan (gör den billig, och invalidera bara vid en ändring — en ovillkorlig invalidering där är en render-loop).
- En override av `setTextToken` avslutar med `return Widget::setTextToken(key, value);` — basen bär `toolTip`.
- **En egen ICKE-VISUELL komponent** (det som står som medlem utan att ritas, som `glaze::Timer`) ärver `glaze::Component` (`name`, `setProp`, `setTextToken`) och registreras med `GLAZE_REGISTER_COMPONENT("MinTyp", MinTyp, ::glaze::PreviewForm::Inert)` — previewformen saknar förval: `Inert` lovar att konstruktorn och settrarna inte gör något mot OS:et (previewn skapar den och kör dess setter-rader), `StandIn` att previewn aldrig ska skapa den (raderna bevaras och utförs inte). Den står i regionen och adopteras aldrig.
- `childrenExtent(c, bredd)` är autostorleken ur barnen med bredden GIVEN — Top/Bottom-remsorna och barnen med två kända sidokanter tar `heightForWidth` med sin bredd. `wantsTextInput()` är publik ("tar widgeten text?").
- `childrenExtent(c)` (protected, för en komposit vars AUTO-mått kommer ur barnen — `GroupBox`s mönster) räknar med `layout`s regel "När föräldern räknas ur innehållet": barn nere till höger, spända barn med sitt mätta mått som minsta och `right`/`bottom` som luft på ett barn bundet bara uppifrån/från vänster. En komposit som byggts mot den äldre regeln (där sådana barn inte bidrog) kan alltså bli större än förut.
<!-- END:custom-widget -->

<!-- BEGIN:language | language språk translation översättning i18n l10n localization tr trtext setlanguage loadlanguage language.bin langtxt glazelanguage strings.h fallback -->
## Språk — `glaze::tr`, `TrText` och `language.bin` (`language`)

**Alias:** language, språk, translation, översättning, i18n, l10n, localization, tr, trtext, setlanguage, loadlanguage, language.bin, langtxt, glazelanguage, strings.h, fallback
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [language.h](Glaze/widgets/include/glaze/language.h), [strings.h](Glaze/widgets/include/glaze/strings.h), [Glaze/language/CLAUDE.md](Glaze/language/CLAUDE.md), [apps/demo/demo.langtxt](apps/demo/demo.langtxt), [apps/demo/src/demo_strings.h](apps/demo/src/demo_strings.h)

Nyckeln är ett NUMERISKT id och språket är dimensionen: id 1–999 är RAMVERKETS (inbäddade, i sv/en/nb/fi — `glaze::str::Ok` …), id 1000+ är APPENS ur en committad `language.bin`. Textfälten tar en `TrText` som bär en literal eller ett id och löser upp sig vid varje ritning, så `glaze::setLanguage("fi")` byter texten i hela appen. Startspråket: explicit `setLanguage` → OS-lokalen → engelska.

### API
- `TrText tr(std::uint32_t id);` — saknas id:t i det aktiva språket: fallbacken, sist platshållaren `⟦id⟧` + en loggrad.
- `class TrText` — implicit ur `const char*`/`std::string` (literal) · `const std::string& str() const&;` · `std::uint32_t id() const`
- `bool loadLanguage(const std::string& path = "language.bin", const std::string& fallbackTag = {});` — appens fil; validerar HELT innan den byts in. `bool setLanguage(const std::string& tag);` · `bool setFallbackLanguage(const std::string& tag);` · `int languageCount();` · `LanguageInfo languageAt(int i);` (`tag`, `name` = självbenämningen)
- Tar `TrText`: `setTitle`, `setToolTip`, `setText` på Button/Label/Checkbox/Radio/Link/GroupBox/TabPage/ToolButton/DockPanel, `MenuItem::text`, menystigar, kolumnrubriker, filternamn. Tar `std::string`: `TextInput`/`TextArea::setText`, list- och tabellposter, `messageBox`.

### Exempel
`app.langtxt` (källan, committad), kommandot som packar den och skriver headern, och appen:
```text
default en

lang en English
id 1000 Title "My app"
id 1001 Hello "Hello!"

lang sv Svenska
id 1000 "Mitt program"
id 1001 "Hej!"
```
```sh
glazelanguage pack --header src/app_strings.h --ns app::str --reflang en app.langtxt language.bin
```
```cpp
class LangWindow : public glaze::AppWindow {
public:
    LangWindow() : glaze::AppWindow(glaze::tr(app::str::Title), 420, 200)
    {
        add<glaze::Label>(glaze::tr(app::str::Hello));          // appens id 1000+
        add<glaze::Button>(glaze::tr(glaze::str::Ok))->y = 32;   // ramverkets 1–999
        langs_ = add<glaze::Dropdown>();
        langs_->y = 64;
        langs_->onChange = [this] {
            const int i = langs_->selectedIndex();
            if (i >= 0)                                          // POSTA bytet
                glaze::app().post([t = glaze::languageAt(i).tag] { glaze::setLanguage(t); });
        };
        glaze::AppWindow::onLoad = [this] { fillLanguages(); };
    }

protected:
    void onLanguageChanged() override { fillLanguages(); }   // listor är strängar

private:
    void fillLanguages()
    {
        std::vector<std::string> names;
        int current = 0;
        for (int i = 0; i < glaze::languageCount(); ++i) {
            names.push_back(glaze::languageAt(i).name);          // självbenämningen
            if (glaze::languageAt(i).tag == glaze::language())
                current = i;
        }
        langs_->setItems(std::move(names));
        langs_->setSelectedIndex(current);                       // fyrar inget onChange
    }

    glaze::Dropdown* langs_ = nullptr;
};

int main()
{
    glaze::loadAssets();                         // assets.bin, FÖRE första fönstret
    glaze::loadLanguage("language.bin", "en");   // fallback en ⇒ filen komplett på en
    LangWindow win;
    return win.run();
}
```

### .glazeui
NUMRET, aldrig konstanten — verktygen kan inte slå upp `app::str::Hello`:
```cpp
    setTitle(glaze::tr(1000));
    status.setText(glaze::tr(1001));
```

### Fallgropar
- Fallbacken är `"sv"` från start, och `loadLanguage` AVVISAR en fil som inte är komplett i fallbackspråket — en app utan komplett svenska laddar med `glaze::loadLanguage("language.bin", "en")`.
- Posta språkbytet ur en dropdowns eller menys handler: `setLanguage` river popups synkront, mitt i popupens egen dispatch.
- `glaze::tr(id).str()` på en temporär kompilerar inte — lägg `tr(id)` i ett namngivet `TrText` först.
- Det som INTE är `TrText` (listornas poster, ihopsatta strängar) fyller du om i `onLanguageChanged()`; titeln och `TrText`-fälten byts av ramverket.
- I `.glazeui` är `setTitle(glaze::tr(app::str::Title))` och `setText(glaze::tr(app::str::Hello))` OPAKA rader: de kompilerar och bevaras, men previewn ritar dem inte (ingen titel, ingen text) — verktygen läser bara numret. `glazelanguage` byggs i GlazeUI-repot (`cmake`); headern genereras vid `pack` och committas.
<!-- END:language -->

<!-- BEGIN:assets | assets bilder images icons ikoner assets.bin loadassets img assethandle glazeassets resx resurser resources png setimg -->
## Bilder — `assets.bin`, `loadAssets` och `img` (`assets`)

**Alias:** assets, bilder, images, icons, ikoner, assets.bin, loadassets, img, assethandle, glazeassets, resx, resurser, resources, png, setimg
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [assets.h](Glaze/widgets/include/glaze/assets.h), [Glaze/assets/CLAUDE.md](Glaze/assets/CLAUDE.md), [apps/glazeassets/src/main.cpp](apps/glazeassets/src/main.cpp)

Appens bilder bor i EN committad containerfil, `assets.bin` — filen ÄR källan (som `.resx`); käll-PNG:erna committas inte och kan exporteras byte-identiskt ur filen. En bild refereras med paret (lista, nyckel) och blir ett `AssetHandle`, inte en textur: texturen avkodas lat per fönster. Listan bär storleksklassen i logiska px.

### API
- `bool loadAssets(const std::string& path = "assets.bin");` — i `main` FÖRE första fönstret; en relativ sökväg provas mot exe-katalogen, mot `../Resources/` (macOS-bundlen) och sist som den står.
- `AssetHandle img(const std::string& list, const std::string& key);` — slå upp EN gång (konstruktor, `initializeComponent`), inte per frame.
- `struct AssetHandle { std::uint32_t id; bool valid() const; }` — 0 = ogiltigt = ingen bild. `float assetSizeClass(AssetHandle h);` — listans storleksklass.
- Konsumenterna tar ett `glaze::ImageSource` — en asset, en `glaze::Bitmap` ur pixlar eller en `RenderTexture` (`canvas-images`) — och ett handtag går in rakt av genom den implicita konverteringen: `Button::setImg` + `setImagePos(ImagePos p)`, `Picture::setImg`, `ToolButton`, `MenuItem::icon` (och `setItemIcon`), raderna i `ListView`/`TreeView`, `messageBox(…, customIcon)`, dragbilden, `appIcon.setIcon` och `TrayIcon`.

### Exempel
`assets.txt` och kommandona (`glazeassets` byggs i GlazeUI-repot, `cmake`):
```text
list toolbar 16          # ny lista; storleksklass 16 LOGISKA px
img  ok      ok.png @2x  # post i senaste listan; @2x = 32×32-källa
```
```sh
glazeassets pack assets.txt assets.bin     # packa; sökvägarna relativt manifestet
glazeassets list assets.bin                # listor, poster och storlekar
glazeassets verify assets.bin              # struktur + CRC
glazeassets extract assets.bin ut          # källfilerna tillbaka, byte-identiska
```
```cpp
void useAssets(glaze::Button& save, glaze::Picture& logo)
{
    save.setImg(glaze::img("toolbar", "ok"));     // ett handtag, ingen textur
    save.setImagePos(glaze::ImagePos::Left);
    logo.setImg(glaze::img("toolbar", "logo"));   // okänd nyckel: ogiltigt + en loggrad
    if (!logo.img().valid())
        gfx::log("logon saknas i assets.bin");
}
```

### .glazeui
```cpp
    save.setImg(glaze::img("toolbar", "ok"));
```
glaze-render och GlazeBuilder letar `assets.bin` uppåt från `.h`-katalogen (fyra nivåer), så previewn visar ikonen.

### Fallgropar
- Saknad fil eller okänd nyckel är TYST i ritningen (knappen ritar bara text) men loggas en gång.
- Filen måste ligga vid exen — på macOS i bundlens `Resources/` — via `POST_BUILD`-kopian (`cmake`).
- Nycklarna är unika PER LISTA: samma nyckel i två listor är två bilder.
- Bara PNG avkodas, på alla plattformar.
- Ikonens sida är listans storleksklass; `@2x`-källor (dubbel upplösning) är husstandard för skarpa ikoner i hög DPI.
- Programikonens bild är ett vanligt handtag ur `assets.bin`: `appIcon.setIcon(glaze::img("app", "logo"))` — EN stor bild räcker, ramverket skalar varje storlek (`app-icon`). Programfilens `.ico`/`.icns`/PNG:er görs ur samma post med `glazeassets icon <assets.bin> app:logo [--small app:logo16] <utkatalog> <bas>` och committas.
<!-- END:assets -->

<!-- BEGIN:stock-icons | stock-icons standardikoner stockicon stockicons inbyggda-ikoner builtin-icons systemikoner ikonuppsättning icon-set spara save öppna open ångra undo sök search zoom verktygsikoner toolbar-icons symbol segoe material lucide cad autocad blender 3d vyer views snap fästpunkt osnap kub cube filtyp filtyper file-type extension ändelse dwg dxf shp geojson gpkg pdf png -->
## Standardikoner — `glaze::stockIcon` (`stock-icons`)

**Alias:** stock-icons, standardikoner, stockicon, inbyggda ikoner, builtin icons, ikonuppsättning, icon set
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [stock_icon.h](Glaze/widgets/include/glaze/stock_icon.h), [stock_icon.cpp](Glaze/widgets/src/stock_icon.cpp)

Ramverket har 392 egna ikoner för det varje app behöver — spara, ångra, sök, zooma, spela, lås, användare … — för CAD- och 3D-verktyg (rita, ändra, fästpunkter, vyer, former, visningslägen, booleska operationer), för bild, dockning, kartor, ordning av objekt, hårdvara och tid, och för FILTYPER — 74 format med ändelsen på en etikett (geodata, CAD/BIM, 3D, punktmoln, bilder, dokument, data, arkiv, media) plus tolv kategorier, så appen slipper lägga dem i sin `assets.bin`. De är VEKTORER i koden (en enfärgad kontur i ramverkets egen stil), rastrerade när de ritas i fönstrets fysiska px och färgade med temats textfärg: de följer ljust och mörkt läge och 1×, 2× och fraktionerad skala utan att appen gör något. En inaktiv kontroll tonar dem som varje annan bild. Färgvarianten (`StockIconStyle::Color`) är samma kontur med ACCENTDELAR i en semantisk färg — mappen gul, spara och information blå, fel och ta bort röda, varningen bärnsten, klar och lägg till gröna.

### API
- `ImageSource stockIcon(StockIcon icon, int sizeClass = 16, StockIconStyle style = StockIconStyle::Mono);` — klasserna är 16, 24, 32 och 48 logiska px (`sizeClass()` på svaret, så widgetarna ger ikonen den sidan); ≤ 0 loggas och blir 16. Svaret går överallt där ett `ImageSource` tas emot: `Button::setImg`, `ToolButton`, `MenuItem::icon`, raderna i `ListView`/`TreeView`, `Picture`, ritytans `Shape::image`, `appIcon.setIcon` och `TrayIcon`.
- `enum class StockIconStyle { Mono, Color }` — konturen, eller samma kontur med färgade accentdelar (en ikon utan accent ritas likadant i båda).
- `enum class StockIcon` — en post per ikon, namngiven efter vad den BETYDER: `Back`, `Forward`, `Up`, `Down`, `Home`, `First`, `Previous`, `Next`, `Last`, `Refresh`, `Stop`, `GoTo` · `ArrowLeft/Right/Up/Down`, `ChevronLeft/Right/Up/Down`, `ChevronsLeft/Right/Up/Down`, `Flip`, `RotateClockwise`, `RotateCounterClockwise`, `Swap` · `New`, `Open`, `Save`, `SaveAs`, `SaveAll`, `Close`, `Print`, `PrintPreview`, `Import`, `Export`, `Properties`, `File`, `Document`, `Folder`, `FolderOpen`, `NewFolder`, `Recent` · `Drive`, `Network`, `Cloud`, `HomeFolder`, `Desktop`, `Trash`, `Download`, `Upload`, `Sync` · `Undo`, `Redo`, `Cut`, `Copy`, `Paste`, `Delete`, `SelectAll`, `Search`, `Replace`, `Clear`, `Edit`, `Rename`, `Duplicate` · `Add`, `Remove`, `MoveUp`, `MoveDown`, `MoveFirst`, `MoveLast`, `SortAscending`, `SortDescending`, `Filter`, `Group`, `Expand`, `Collapse`, `ExpandAll`, `CollapseAll` · `ListView`, `DetailsView`, `IconView`, `Grid`, `SplitView`, `Sidebar`, `FullScreen`, `ExitFullScreen` · `ZoomIn`, `ZoomOut`, `ZoomToFit`, `ActualSize`, `Show`, `Hide`, `Lock`, `Unlock`, `Pin`, `Unpin` · `Bold`, `Italic`, `Underline`, `AlignLeft`, `AlignCenter`, `AlignRight`, `AlignJustify`, `BulletList`, `NumberedList`, `Font`, `TextColor`, `FillColor` · `Information`, `Warning`, `Error`, `Question`, `Done`, `Cancel`, `Help`, `Tip`, `Loading` · `Menu`, `MoreHorizontal`, `MoreVertical`, `Settings`, `CloseWindow`, `Minimize`, `Maximize`, `Restore`, `NewWindow`, `ExternalLink` · `Pointer`, `Pan`, `Measure`, `Pen`, `Eraser`, `Eyedropper`, `Shape`, `Line`, `Rectangle`, `Ellipse`, `Polygon`, `Text`, `Layers`, `Anchor`, `Magnet` · `Play`, `Pause`, `MediaStop`, `Record`, `PreviousTrack`, `NextTrack`, `Repeat`, `Shuffle`, `SoundOn`, `SoundOff` · `User`, `Users`, `Mail`, `Message`, `Notification`, `Share`, `Link`, `Attach` · `Calendar`, `Clock`, `History`, `Chart`, `Table`, `Database`, `Funnel`, `Tag`, `Bookmark`, `Star`, `Flag` · `SecurityLock`, `Key`, `Shield`, `LogIn`, `LogOut` · CAD: `Point`, `Polyline`, `Arc`, `Circle`, `Spline`, `Hatch`, `Dimension`, `AngleDimension` · `Move`, `Rotate`, `Scale`, `Offset`, `Trim`, `Extend`, `Fillet`, `Chamfer`, `Array` · `SnapEndpoint`, `SnapMidpoint`, `SnapCenter`, `SnapIntersection`, `SnapPerpendicular`, `SnapGrid`, `Ortho`, `Crosshair` · 3D: `ViewTop`, `ViewFront`, `ViewRight`, `ViewIsometric`, `Perspective`, `Orthographic`, `Orbit`, `Camera`, `Light`, `Axes`, `ZoomWindow` · `Box`, `Sphere`, `Cylinder`, `Cone`, `Torus`, `Plane`, `Mesh` · `Wireframe`, `Solid`, `Rendered`, `SelectVertex`, `SelectEdge`, `SelectFace`, `Extrude`, `Union`, `Subtract`, `Intersect` · Bild: `Image`, `Crop`, `Resize`, `Brightness`, `Contrast` · Dockning: `PanelLeft`, `PanelRight`, `PanelBottom`, `Float`, `Tabs`, `SplitHorizontal` · Kod: `Terminal`, `Code`, `Bug`, `Package` · Karta: `MapPin`, `MyLocation`, `Compass`, `Globe`, `Route`, `Terrain` · Ordna: `BringToFront`, `SendToBack`, `BringForward`, `SendBackward`, `Ungroup`, `AlignObjectsLeft`/`Center`/`Right`/`Top`/`Middle`/`Bottom`, `DistributeHorizontal`, `DistributeVertical` · 3D: `ViewBottom`, `ViewLeft`, `ViewBack` (de dolda sidornas kanter streckade), `Walk`, `Section` · CAD: `Explode`, `Block`, `Note`, `RevisionCloud`, `Area` · Text: `Strikethrough`, `Indent`, `Outdent`, `Heading`, `Quote`, `CodeBlock` · Diagram: `LineChart`, `PieChart`, `Sum` · Status: `AddCircle`, `RemoveCircle`, `CheckCircle`, `Blocked`, `Online`, `Offline` · Hårdvara: `Laptop`, `Phone`, `Keyboard`, `Mouse`, `Cpu`, `Battery`, `Power`, `Wifi`, `Usb` · Tid: `Timer` (timglaset), `Stopwatch` · `ArrowUpLeft`, `ArrowUpRight`, `ArrowDownLeft`, `ArrowDownRight` · Filkategorier: `FileImage`, `FileVideo`, `FileAudio`, `FileArchive`, `FileCode`, `FileSpreadsheet`, `FilePresentation`, `FileGeo`, `FileCad`, `FileModel`, `FilePointCloud`, `FileDatabase` · Filformat, `File` + ändelsen: geodata `FileShp`, `FileGeoJson`, `FileKml`, `FileKmz`, `FileGpx`, `FileGpkg`, `FileGml`, `FileGdb`, `FileOsm`, `FileGeoTiff`, `FileMbTiles`, `FileTab`, `FileQgz` · CAD/BIM `FileDwg`, `FileDxf`, `FileDgn`, `FileIfc`, `FileStep`, `FileIges`, `FileRvt` · 3D `FileObj`, `FileFbx`, `FileGltf`, `FileGlb`, `FileStl`, `File3mf`, `FileDae`, `FileSkp`, `FileUsd` · punktmoln `FileLas`, `FileLaz`, `FileE57`, `FilePly`, `FileXyz` · bilder `FilePng`, `FileJpg`, `FileGif`, `FileBmp`, `FileTiff`, `FileWebp`, `FileSvg`, `FileHeic`, `FileIco`, `FilePsd`, `FileRaw` · dokument `FilePdf`, `FileDoc`, `FileDocx`, `FileOdt`, `FileRtf`, `FileTxt`, `FileMd`, `FileXls`, `FileXlsx`, `FileOds`, `FileCsv`, `FilePpt`, `FilePptx`, `FileOdp` · data `FileJson`, `FileYaml`, `FileXml`, `FileHtml`, `FileSql`, `FileSqlite` · arkiv `FileZip`, `File7z`, `FileRar`, `FileTar`, `FileGz` · media `FileMp3`, `FileWav`, `FileMp4`, `FileMov`. Formatikonen är sidan med en liten symbol och ÄNDELSEN UTSTANSAD ur en etikett — läsbar från 24 px, i 16 px bär etikettens färg typen: geodata grön, CAD/BIM och PDF röd, 3D och punktmoln bärnsten, bilder gul, text blå, kalkyl grön, presentation bärnsten, data, arkiv och media neutrala. Spegla är `Flip` och vrid åt ett håll `RotateClockwise`; i färgvarianten är CAD-greppen och resultatet blå, det som tas bort rött, fästpunkternas märken gröna och axlarna X röd, Y grön, Z blå.
- `int stockIconCount();` · `std::string_view stockIconName(StockIcon);` · `std::optional<StockIcon> stockIconByName(std::string_view);` — för en palett eller ett galleri.

### Exempel
```cpp
void useStockIcons(glaze::Button& save, glaze::ToolButton& zoom)
{
    save.setImg(glaze::stockIcon(glaze::StockIcon::Save));
    zoom.setImg(glaze::stockIcon(glaze::StockIcon::ZoomIn, 24,
                                 glaze::StockIconStyle::Color));
}
```

### .glazeui
```cpp
    saveBtn.setImg(glaze::stockIcon(glaze::StockIcon::Save));
    zoomBtn.setImg(glaze::stockIcon(glaze::StockIcon::ZoomIn, 24, glaze::StockIconStyle::Color));
```
Previewn och GlazeBuilder ritar dem — inget `assets.bin` behövs.

### Fallgropar
- Färgen är temats TEXTFÄRG (`Theme::Colors::text`), inte radens eller knappens: en ikon i en markerad rad med inverterad text byter inte färg. Färgvariantens accenter är temats `stockBlue`/`stockGreen`/`stockRed`/`stockAmber`/`stockYellow` — härledda ur paletten (ljusare i mörkt läge), och en app får skriva över dem som vilken annan roll som helst.
- En OS-ikon (`appIcon.setIcon`, `TrayIcon`) av en standardikon rastreras när den sätts, i appens textfärg i OS:ets läge — den följer inte ett senare temabyte.
- Arket med alla ikoner i alla klasser, ljust och mörkt: `build-test/tests/stockicon_sheet ut.png` (GlazeUI-repot, `GLAZE_TESTS=ON`).
<!-- END:stock-icons -->

<!-- BEGIN:threads | threads trådar tråd worker bakgrund background backgroundworker async asynkron pool trådpool threadpool result stop_token cancel avbryt uicontext glaze_threads_ui -->
## Threads — bakgrundsarbete med svar i UI-tråden (`threads`)

**Alias:** threads, trådar, tråd, worker, bakgrund, background, backgroundworker, async, asynkron, pool, trådpool, threadpool, result, stop_token, cancel, avbryt, uicontext, glaze_threads_ui
**Include:** `#include <glaze_threads/threads.h>` · **CMake:** `glaze_threads_ui` (UI-bryggan; kärnan ensam: `glaze_threads`) · **Källa:** [worker.h](Threads/include/glaze_threads/worker.h), [result.h](Threads/include/glaze_threads/result.h), [pool.h](Threads/include/glaze_threads/pool.h), [CLAUDE.md](Threads/CLAUDE.md)

Sidopaketet utanför ramverket (`glaze` länkar det aldrig). `glaze::threads::Worker` är WinForms `BackgroundWorker` utan `object`: ARBETET körs i en pooltråd och dess `Result<T>` levereras till en FORTSÄTTNING i UI-tråden. Fortsättningen körs aldrig efter att Workern (fönstret) dött, undantag i arbetet blir ett fel i `Result`, och `cancel()` lovar att det avbrutna svaret aldrig kommer. Utan paketet: `glaze::app().post(fn)` är den enda glaze-funktion som får anropas från en annan tråd.

### API
- `Worker()` · `explicit Worker(Pool& pool)` · `explicit Worker(Context& context)` · `Worker(Pool& pool, Context& context)`
- `template <class Work, class Done> void run(Work work, Done done)` — `work(std::stop_token)` (eller utan argument) returnerar `T`, `void` eller `Result<T>` (plattas ut); `done(Result<T>&)` är obligatorisk.
- `void cancel()` · `bool busy() const` · `void wait()` (aldrig på UI-tråden)
- `template <class Fn> void post(std::stop_token stop, Fn fn)` — mellanbesked från arbetet till UI-tråden.
- `Result<T>`: `bool ok() const` · `explicit operator bool() const` · `bool cancelled() const` · `const std::error_code& code() const` · `const std::string& error() const` · `const Error& failure() const` · `T& value() &` · `operator*` · `operator->`
- `struct Error { std::error_code code; std::string message; }` · `enum class Code { Ok = 0, Cancelled = 1, Exception = 2 }` · `Result<void>` default = lyckat.
- `explicit Pool(unsigned threads = 0)` (0 = hårdvarans antal, minst 2) · `Pool& defaultPool()` · `Context& uiContext()` (`glaze_threads/ui.h`) · `Context& workerContext()`

### Exempel
```cpp
class Summa : public glaze::AppWindow {
public:
    Summa() : glaze::AppWindow("Summa", 360, 140)
    {
        rakna.setText("Räkna");
        rakna.name = "rakna";
        rakna.x = 12;
        rakna.y = 12;
        rakna.onClick = [this] { starta(1'000'000); };

        forlopp.setMax(1'000'000);
        forlopp.width = 240;
        forlopp.name = "forlopp";
        forlopp.x = 12;
        forlopp.anchorTop = {"rakna", 8};

        status.x = 12;
        status.anchorTop = {"forlopp", 8};

        root().adopt(rakna);
        root().adopt(forlopp);
        root().adopt(status);
    }

private:
    void starta(int n)
    {
        bakgrund.cancel();                // "senaste vinner" — det gamla svaret kommer aldrig
        bakgrund.run([this, n](std::stop_token stop) -> long long {   // POOLTRÅD
            long long summa = 0;          // rör ALDRIG en widget här
            for (int i = 1; i <= n && !stop.stop_requested(); ++i) {
                summa += i;
                if (i % 100'000 == 0)     // mellanbesked, körs i UI-tråden
                    bakgrund.post(stop, [this, i] { forlopp.setValue(static_cast<float>(i)); });
            }
            return summa;
        }, [this](glaze::threads::Result<long long>& r) {   // UI-TRÅDEN
            status.setText(r ? std::to_string(*r) : r.error());
        });
    }

    glaze::Button          rakna;
    glaze::Progress        forlopp;
    glaze::Label           status;
    glaze::threads::Worker bakgrund;      // MEDLEM — dör med fönstret
};
```
```cmake
target_link_libraries(minapp PRIVATE glaze glaze_threads_ui)
```

### .glazeui
Ingen form — en `Worker` är ingen widget utan en medlem utanför GlazeBuilder-regionen, och arbetet startas i handlers i `.cpp`.

### Fallgropar
- Arbetet får bara fånga VÄRDEN och aldrig röra en widget, inte ens läsa; fortsättningen och `post`-beskeden får röra allt.
- En `Worker` ska vara MEDLEM (eller lokal), aldrig global eller statisk — den måste dö före `glaze::App`. En egen `Pool` deklareras FÖRE de Workers som använder den.
- Länka `glaze_threads_ui` (aldrig `_impl`); annars hamnar fortsättningarna tyst i pooltråden. I delat läge håller ett ankare (en INTERFACE-källa) bryggans DLL kvar — länka därför bryggan direkt till varje program och plugin som ska ha den, inte genom ett statiskt mellanbibliotek (`shared-library`).
- `defaultPool()` rivs aldrig: den lever till processlut, och OS:et tar dess trådar.
- Skicka mellanbesked med arbetets EGEN token (`post(stop, …)`), så släpps de om `cancel()` hinner före. Ett jobb direkt i en `Pool` som kastar är `std::terminate`.

<!-- END:threads -->

<!-- BEGIN:networking | networking nätverk nät net glaze_net internet http-klient async-get request worker fel error code timeout glaze_networking libcurl winhttp nsurlsession -->
## Networking — nätverk, översikt och felhantering (`networking`)

**Alias:** networking, nätverk, nät, net, glaze_net, internet, http-klient, async-get, request, worker, fel, error, code, timeout, glaze_networking, libcurl, winhttp, nsurlsession
**Include:** `#include <glaze_net/net.h>` · **CMake:** `glaze_net` (+ `glaze_threads_ui`) · **Källa:** [net.h](Networking/include/glaze_net/net.h), [error.h](Networking/include/glaze_net/error.h), [async.h](Networking/include/glaze_net/async.h), [CLAUDE.md](Networking/CLAUDE.md)

Sidopaketet för TCP, UDP, DNS, PING och HTTP(S) — HTTP genom OS:ets egen stack (WinHTTP, NSURLSession, libcurl). Det SYNKRONA lagret blockerar med tidsgräns och `std::stop_token` och körs i en pooltråd; de ASYNKRONA formerna (`get`, `request`, `resolve`, `ping` med en `Worker` först) levererar svaret till UI-tråden. Varje anrop svarar `Result<T>` (samma typ som `threads`): en `Code` (vad) och en text (varför).

### API
- `enum class Code { Ok = 0, Timeout, HostNotFound, Refused, Reset, Closed, Unreachable, AddressInUse, InvalidArgument, Tls, Protocol, Permission, Failed }`
- `template <class T> using Result = threads::Result<T>` · `using Error = threads::Error` · `constexpr std::chrono::milliseconds kForever`
- `template <class Done> void get(threads::Worker& worker, std::string url, Done done)`
- `template <class Done> void request(threads::Worker& worker, HttpRequest req, Done done)`
- `template <class Done> void resolve(threads::Worker& worker, std::string host, Done done)` · `template <class Done> void ping(threads::Worker& worker, std::string host, Done done)`
- Avbrott: `r.cancelled()` (`glaze::threads::Code::Cancelled`) — ingen nätkod; ett avbrutet svar levereras ändå aldrig.

### Exempel
```cpp
class Karta : public glaze::AppWindow {
public:
    Karta() : glaze::AppWindow("Karta", 480, 160)
    {
        hamta.setText("Hämta");
        hamta.name = "hamta";
        hamta.x = 12;
        hamta.y = 12;
        hamta.onClick = [this] { hamtaKarta(); };
        status.x = 12;
        status.anchorTop = {"hamta", 8};
        root().adopt(hamta);
        root().adopt(status);
    }

private:
    void hamtaKarta()
    {
        nat.cancel();                     // bara den senaste förfrågan räknas
        glaze::net::get(nat, "https://example.com/karta.png",
                        [this](glaze::net::Result<glaze::net::HttpResponse>& r) {
            if (!r)                       // nätfel: r.code() + r.error()
                status.setText(r.error());
            else if (r->success())        // 2xx
                status.setText(std::to_string(r->body.size()) + " byte");
            else                          // 4xx/5xx är ett SVAR, inte ett fel
                status.setText("HTTP " + std::to_string(r->status));
        });
    }

    glaze::Button          hamta;
    glaze::Label           status;
    glaze::threads::Worker nat;           // MEDLEM — inga svar efter fönstret
};
```
```cmake
target_link_libraries(minapp PRIVATE glaze glaze_threads_ui glaze_net)
```

### .glazeui
Ingen form — nätanrop görs i handlers i `.cpp`.

### Fallgropar
- Aldrig ett synkront anrop på UI-tråden (hård regel 1) — använd de asynkrona formerna eller `worker.run`.
- Linux kräver systemets libcurl (`apt install libcurl4-openssl-dev`); en app utan behov konfigurerar med `-DGLAZE_NETWORKING=OFF`.
- TCP och UDP har ingen asynkron form: ett utbyte är rak kod i `worker.run` (se `networking-tcp`, `networking-udp`).
- Felets text är teknisk och för loggen; välj UI-texten själv ur `r.code()`.

<!-- END:networking -->

<!-- BEGIN:networking-http | http https get post rest api httprequest httpresponse header headers percentencode url omdirigering redirect statuskod status json webbanrop -->
## HTTP(S) (`networking-http`)

**Alias:** http, https, get, post, rest, api, httprequest, httpresponse, header, headers, percentencode, url, omdirigering, redirect, statuskod, status, json, webbanrop
**Include:** `#include <glaze_net/net.h>` · **CMake:** `glaze_net` · **Källa:** [http.h](Networking/include/glaze_net/http.h), [async.h](Networking/include/glaze_net/async.h), [CLAUDE.md](Networking/CLAUDE.md)

"Appen ber om en resurs och får ett svar" genom OS:ets HTTP-stack — certifikat, proxy och TLS-versioner sköter OS:et. Paketet likriktar: `timeout` gäller HELA förfrågan, högst 10 omdirigeringar, https → http följs aldrig, kroppen avkomprimeras och 4xx/5xx är SVAR, inte fel. I en GUI-app körs den i en `Worker`.

### API
- `struct HttpRequest { std::string method = "GET"; std::string url; std::vector<Header> headers; std::string body; std::chrono::milliseconds timeout{std::chrono::seconds{30}}; bool followRedirects = true; }`
- `HttpResponse` — `int status`, `std::vector<Header> headers` (det slutliga svarets), `std::string body` (avkomprimerad), `std::string url` (efter omdirigeringar) · `std::string header(std::string_view name) const` (skiftlägesokänsligt, tom om den saknas) · `bool success() const` (2xx).
- `struct Header { std::string name; std::string value; }`
- `Result<HttpResponse> request(const HttpRequest& req, std::stop_token stop)` · `Result<HttpResponse> get(std::string_view url, std::stop_token stop, std::chrono::milliseconds timeout = std::chrono::seconds{30})`
- `Result<HttpResponse> post(std::string_view url, std::string body, std::string_view contentType, std::stop_token stop, std::chrono::milliseconds timeout = std::chrono::seconds{30})`
- `std::string percentEncode(std::string_view text)` — för ett VÄRDE i en frågesträng.
- Asynkront: `get(worker, url, done)` · `request(worker, req, done)` (se `networking`).

### Exempel
```cpp
// status är en glaze::Label, nat en glaze::threads::Worker (medlemmar)
glaze::net::HttpRequest req;
req.method  = "POST";
req.url     = "https://api.example.com/v1/sok?q=" + glaze::net::percentEncode("väg å");
req.headers = {{"Authorization", "Bearer abc123"},
               {"Content-Type", "application/json"}};
req.body    = R"({"id": 17})";
req.timeout = std::chrono::seconds{10};   // taket för HELA förfrågan

glaze::net::request(nat, std::move(req),
                    [this](glaze::net::Result<glaze::net::HttpResponse>& r) {
    if (!r && r.code() == glaze::net::Code::Timeout)
        status.setText("Tidsgränsen passerades");
    else if (!r)
        status.setText(r.error());
    else if (r->success())
        status.setText(r->header("Content-Type"));   // skiftlägesokänsligt
    else
        status.setText("HTTP " + std::to_string(r->status));
});

// Det SYNKRONA lagret i ett eget arbete — Result<T> plattas ut.
nat.run([](std::stop_token stop) {
    return glaze::net::post("https://api.example.com/v1/logg", "rad=1",
                            "application/x-www-form-urlencoded", stop);
}, [this](glaze::net::Result<glaze::net::HttpResponse>& r) {
    if (!r)
        status.setText(r.error());
});
```

### Fallgropar
- URL:en ska vara ASCII — procentkoda värden med `percentEncode`. Användarnamn i URL:en vägras; skicka en `Authorization`-header.
- Inga kakor och ingen cache. GET/HEAD skickar ingen kropp; en kropp utan Content-Type får `application/octet-stream`.
- macOS App Transport Security stoppar `http://` (okrypterat) utan undantag i Info.plist ⇒ `Code::Permission`.
- `Content-Length` gäller den KOMPRIMERADE längden när svaret avkomprimerats.

<!-- END:networking-http -->

<!-- BEGIN:networking-tcp | tcp socket sockets tcpsocket tcplistener server klient client connect anslut send receive accept listen port ström stream -->
## TCP: TcpSocket och TcpListener (`networking-tcp`)

**Alias:** tcp, socket, sockets, tcpsocket, tcplistener, server, klient, client, connect, anslut, send, receive, accept, listen, port, ström, stream
**Include:** `#include <glaze_net/net.h>` · **CMake:** `glaze_net` · **Källa:** [tcp.h](Networking/include/glaze_net/tcp.h), [address.h](Networking/include/glaze_net/address.h), [CLAUDE.md](Networking/CLAUDE.md)

Blockerande TCP med tidsgräns och avbrytning: varje anrop tar en `std::stop_token` och väntar högst `timeout`, och ett avbrott verkar direkt utan att någon tråd pollar. Ett utbyte skrivs som rak kod i `worker.run`; ett `Result<T>` som arbetet returnerar plattas ut till fortsättningen. En server som ska leva länge får en EGEN tråd (`Pool{1}` + `Worker`).

### API
- `Result<void> connect(std::string_view host, std::uint16_t port, std::stop_token stop, std::chrono::milliseconds timeout = std::chrono::seconds{10})` · `Result<void> connect(const Endpoint& to, std::stop_token stop, std::chrono::milliseconds timeout = std::chrono::seconds{10})`
- `Result<void> send(std::string_view data, std::stop_token stop, std::chrono::milliseconds timeout = std::chrono::seconds{10})` — skickar ALLT.
- `Result<std::string> receive(std::stop_token stop, std::chrono::milliseconds timeout = std::chrono::seconds{10}, std::size_t max = 65536)` — minst en byte; stängd motpart = `Code::Closed`.
- `Result<void> shutdownSend()` · `void close()` · `bool isOpen() const` · `Result<Endpoint> localEndpoint() const` · `Result<Endpoint> remoteEndpoint() const`
- `TcpListener`: `Result<void> listen(const Endpoint& local, int backlog = 16)` · `Result<TcpSocket> accept(std::stop_token stop, std::chrono::milliseconds timeout = kForever)`
- `Endpoint` — `Address address` + `std::uint16_t port` · `std::string toString() const` (`"1.2.3.4:80"`) · `static Address loopbackV4()` · `static Address anyV4()`

### Exempel
```cpp
// status är en glaze::Label, nat en glaze::threads::Worker (medlemmar)
nat.run([](std::stop_token stop) -> glaze::net::Result<std::string> {
    glaze::net::TcpSocket s;      // ett utbyte = rak kod i ett arbete
    if (auto r = s.connect("example.com", 7, stop); !r)
        return r.failure();
    if (auto r = s.send("hej\n", stop); !r)
        return r.failure();
    return s.receive(stop);       // minst en byte, eller Code::Closed
}, [this](glaze::net::Result<std::string>& svar) {
    status.setText(svar ? *svar : svar.error());
});

// En server: egen tråd (Pool{1}) så den inte lånar den delade poolen för evigt.
struct Server {
    glaze::threads::Pool   lyssnarTrad{1};   // FÖRE workern: poolen ska leva längst
    glaze::threads::Worker lyssnare{lyssnarTrad};
    void starta()
    {
        lyssnare.run([](std::stop_token stop) -> glaze::net::Result<void> {
            glaze::net::TcpListener l;
            if (auto r = l.listen({glaze::net::Address::loopbackV4(), 5000}); !r)
                return r.failure();       // Code::AddressInUse om porten är tagen
            for (;;) {
                auto klient = l.accept(stop);   // väntar tills någon ansluter
                if (!klient)
                    return klient.failure();    // ett avbrott levereras aldrig
                if (auto data = klient->receive(stop))
                    klient->send("OK " + *data, stop);
            }
        }, [](glaze::net::Result<void>& r) { gfx::log("server: %s", r.error().c_str()); });
    }
};
```

### Fallgropar
- `close()` medan en annan tråd väntar i ett anrop är förbjudet — avbryt med stop_token (`worker.cancel()`).
- `send` och `receive` får köras samtidigt från två trådar (full duplex); allt annat en tråd i taget.
- Lyssna på `Address::loopbackV4()` när bara den egna datorn ska nå porten — alla gränssnitt får Windows brandvägg att fråga. Port 0 = valfri ledig (läs `localEndpoint()`).

<!-- END:networking-tcp -->

<!-- BEGIN:networking-udp | udp udpsocket datagram datagrams broadcast sendto bind lyssnare listener multicast paket packet -->
## UDP: UdpSocket och Datagram (`networking-udp`)

**Alias:** udp, udpsocket, datagram, datagrams, broadcast, sendto, bind, lyssnare, listener, multicast, paket, packet
**Include:** `#include <glaze_net/net.h>` · **CMake:** `glaze_net` · **Källa:** [udp.h](Networking/include/glaze_net/udp.h), [CLAUDE.md](Networking/CLAUDE.md)

Samma form som TCP: blockerande med tidsgräns och avbrytning, för en pooltråd. Ett datagram är helt eller inget, högst 64 KiB. En lyssnare som ska leva länge får en egen tråd (`Pool{1}` + `Worker`), och socketen FLYTTAS in i arbetet — den rörs bara där.

### API
- `struct Datagram { Endpoint from; std::string data; }`
- `Result<void> bind(const Endpoint& local)` — port 0 = valfri; frivillig för en avsändare.
- `Result<void> sendTo(const Endpoint& to, std::string_view data, std::stop_token stop, std::chrono::milliseconds timeout = std::chrono::seconds{10})`
- `Result<Datagram> receive(std::stop_token stop, std::chrono::milliseconds timeout = kForever)` — kräver en öppen socket.
- `void close()` · `bool isOpen() const` · `Result<Endpoint> localEndpoint() const`

### Exempel
```cpp
struct Fonster : glaze::AppWindow {
    glaze::Label           logg;
    glaze::threads::Pool   lyssnarTrad{1};   // medlemmar, i DEN ordningen
    glaze::threads::Worker lyssnare{lyssnarTrad};
    glaze::threads::Worker nat;              // den delade poolen, för korta jobb
    void lyssna()
    {
        glaze::net::UdpSocket s;
        if (auto r = s.bind({glaze::net::Address::loopbackV4(), 5000}); !r) {
            logg.setText(r.error());        // t.ex. Code::AddressInUse
            return;
        }
        lyssnare.run([s = std::move(s), this](std::stop_token stop) mutable
                         -> glaze::net::Result<void> {
            for (;;) {
                auto d = s.receive(stop);   // ett helt datagram, högst 64 KiB
                if (!d)
                    return d.failure();
                lyssnare.post(stop, [this, text = d->data] { logg.setText(text); });
            }
        }, [this](glaze::net::Result<void>& r) { logg.setText(r.error()); });
    }
    void skicka()
    {
        // INTE på lyssnaren: dess enda tråd är upptagen av loopen.
        nat.run([](std::stop_token stop) {
            glaze::net::UdpSocket ut;      // ingen bind behövs för att skicka
            return ut.sendTo({glaze::net::Address::loopbackV4(), 5000}, "hej", stop);
        }, [](glaze::net::Result<void>&) {});
    }
};
```

### Fallgropar
- Deklarera `Pool` FÖRE `Worker` — poolen ska leva längre än workern. En `Pool{1}` med en evig loop har ingen tråd över: kör andra jobb på en annan `Worker`.
- Ett avbrott (`cancel()`, fönstret stängs) levereras aldrig till fortsättningen; felgrenen ser bara riktiga fel.

<!-- END:networking-udp -->

<!-- BEGIN:networking-dns-ping | dns resolve namnuppslag lookup ping icmp echo address adress ipv4 ipv6 endpoint pingreply getaddrinfo ping_group_range -->
## DNS, PING och adresser (`networking-dns-ping`)

**Alias:** dns, resolve, namnuppslag, lookup, ping, icmp, echo, address, adress, ipv4, ipv6, endpoint, pingreply, getaddrinfo, ping_group_range
**Include:** `#include <glaze_net/net.h>` · **CMake:** `glaze_net` · **Källa:** [dns.h](Networking/include/glaze_net/dns.h), [ping.h](Networking/include/glaze_net/ping.h), [address.h](Networking/include/glaze_net/address.h), [CLAUDE.md](Networking/CLAUDE.md)

`resolve` ger adresserna i OS:ets ordning; en numerisk adress svaras direkt. `ping` skickar ett ICMP-eko utan administratörsrättigheter (Windows IcmpSendEcho, annars en ICMP-datagramsocket) och mäter tiden. `Address`/`Endpoint` är rena värden — text ↔ adress är ren std och tolkas likadant på alla plattformar.

### API
- `Result<std::vector<Address>> resolve(std::string_view host, std::stop_token stop, std::chrono::milliseconds timeout = std::chrono::seconds{10})`
- `Result<PingReply> ping(std::string_view host, std::stop_token stop, std::chrono::milliseconds timeout = std::chrono::seconds{4})` — `struct PingReply { Address address; std::chrono::microseconds time{}; }`
- Asynkront: `resolve(worker, host, done)` · `ping(worker, host, done)` (se `networking`).
- `static std::optional<Address> parse(std::string_view text)` · `static Address v4(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d)` · `loopbackV4()` · `loopbackV6()` · `anyV4()` · `anyV6()`
- `bool isV4() const` · `bool isV6() const` · `bool isLoopback() const` · `std::string toString() const` (RFC 5952 för IPv6)

### Exempel
```cpp
// status är en glaze::Label, nat en glaze::threads::Worker (medlemmar)
glaze::net::resolve(nat, "example.com",
                    [this](glaze::net::Result<std::vector<glaze::net::Address>>& r) {
    if (r && !r->empty())
        status.setText(r->front().toString());   // OS:ets ordning
});
glaze::net::ping(nat, "example.com", [this](glaze::net::Result<glaze::net::PingReply>& r) {
    if (r)
        status.setText(std::to_string(r->time.count() / 1000) + " ms");
    else if (r.code() == glaze::net::Code::Permission)
        status.setText(r.error());   // Linux: net.ipv4.ping_group_range
});
if (const auto a = glaze::net::Address::parse("192.168.1.10"); a && !a->isLoopback())
    status.setText(glaze::net::Endpoint{*a, 80}.toString());   // "192.168.1.10:80"
```

### Fallgropar
- Linux tillåter ICMP-socketen bara för grupper inom `net.ipv4.ping_group_range` — annars `Code::Permission` med sysctl-namnet i texten, aldrig en tyst timeout.
- IDN-namn (åäö) skickas som punycode; IPv4 tolkas strikt (inga inledande nollor) och zon-id bara numeriskt (`fe80::1%3`).
- OS:ets namnuppslag går inte att avbryta: anropet återvänder direkt vid stopp eller tidsgräns, och uppslaget löper klart i bakgrunden.

<!-- END:networking-dns-ping -->

<!-- BEGIN:terminal | terminal konsol console pty pseudoterminal conpty forkpty posix_openpt shell skal cmd powershell pwsh bash zsh claude vim htop less ansi vt100 xterm scrollback historik bracketed-paste onexit ontitlechange glaze_terminal glaze_terminal_ui terminalscheme terminalcolor solarized tango kopiera klistra -->
## Terminal — ett program i en inbyggd terminal (`terminal`)

**Alias:** terminal, konsol, console, pty, pseudoterminal, conpty, forkpty, posix_openpt, shell, skal, cmd, powershell, pwsh, bash, zsh, claude, vim, htop, less, ansi, vt100, xterm, scrollback, historik, bracketed-paste, onexit, ontitlechange, glaze_terminal, glaze_terminal_ui, terminalscheme, terminalcolor, solarized, tango, kopiera, klistra
**Include:** `#include <glaze_terminal/terminal.h>` · **CMake:** `glaze_terminal_ui` · **Källa:** [terminal.h](Terminal/include/glaze_terminal/terminal.h), [scheme.h](Terminal/include/glaze_terminal/scheme.h), [keys.h](Terminal/src/ui/keys.h), [CLAUDE.md](Terminal/CLAUDE.md)

`glaze::Terminal` kör ett program — ett skal, `claude`, ett byggkommando — i OS:ets pseudoterminal (ConPTY på Windows 10 1809+, `posix_openpt` på macOS och Linux) och visar det med GlazeUI:s egen VT-emulering i Mono-fonten: markören, raderingen, rullningsområden och den alternativa skärmen (vim, less, htop), 16/256/24-bitars färger, fet/kursiv/understruken/omvänd, UTF-8 med breda tecken och emoji, bracketed paste, fokushändelser, musrapportering och historik med markering. Widgeten är en `Panel` (remsorna och hjulet), läsningen sker i en egen tråd och ritningen bara när något ändrats. Sidopaketet `Terminal/` — `glaze` länkar det aldrig.

### API
- Programmet (läses vid `start()`): `void setProgram(std::string path)` (tomt = OS:ets skal: `%ComSpec%`, `$SHELL` annars `/bin/sh`) · `void setArguments(std::string commandLine)` (EN kommandorad som .NET:s `Arguments` — ordagrant på Windows, delad med CommandLineToArgvW:s regler på POSIX) · `void setWorkingDirectory(std::string dir)` · `void setEnvironmentVariable(const std::string& name, std::string value)` · `void unsetEnvironmentVariable(const std::string& name)` · `std::optional<std::string> environmentVariable(const std::string& name) const`. Förvalet lägger till `TERM=xterm-256color` och `COLORTERM=truecolor`; appens värde vinner.
- `bool start()` (false ⇒ `const std::string& lastError() const`; startar om en terminal som kör) · `bool isRunning() const` · `int exitCode() const` (−1 före slutet; POSIX: 128 + signalen för en dödad process) · `void kill()` (hela processkedjan) · `void write(std::string_view bytes)` (rått till indatan) · `void paste(std::string_view text)` (radsluten som CR, bracketed paste) · `void feedOutput(std::string_view vt)` (utdata utan program — en loggström med färger)
- `const std::string& title() const` (OSC 0/2) · `int columns() const` · `int rows() const`
- Historik och markering: `void setScrollback(int lines)` (förval 10 000) · `void clear()` · `void scrollToBottom()` · `bool hasSelection() const` · `std::string selectedText() const` · `void selectAll()` · `void clearSelection()` · `void copy()`
- Utseendet: `void setColorMode(TerminalColorMode m)` (`Auto` följer appens läge och OS:et direkt, `Light`/`Dark` låser) · `void setColorScheme(TerminalScheme s)` (`Theme` — härlett ur appens tema, `Solarized`, `One`, `Tango`) · `void setSchemeColor(ThemeMode variant, TerminalColor role, gfx::Color c)` (en egen färg ovanpå schemat, per variant) · `void clearSchemeColors()` · `void setTextSize(Length size)` (px/mm; 0 = stilens) · `void setAltAsMeta(bool on)` (förval på på Windows/Linux, av på macOS) · `TerminalPalette palette() const`
- `Handler<EventArgs> onExit` (läs `exitCode()`) · `Handler<EventArgs> onTitleChange` (läs `title()`) — sender är terminalen
- Kärnan (`glaze_terminal`, `glaze::term`) är publik för headless bruk: `Pty`, `Session`, `Screen`, `Parser`, `cellWidth`, `splitCommandLine`/`quoteArgument`.

### Exempel
```cpp
#include <glaze/glaze.h>
#include <glaze_terminal/terminal.h>   // UTANFÖR GlazeBuilder-regionen

class Konsol : public glaze::AppWindow {
public:
    Konsol() : glaze::AppWindow("Konsol", 800, 480)
    {
        term.name = "term";
        term.dock = glaze::Dock::Fill;
        term.setColorScheme(glaze::TerminalScheme::Theme);
        term.onTitleChange = [this] { setTitle(term.title()); };
        term.onExit = [this] { statusBar.show(); status.setText("Klar: " + std::to_string(term.exitCode())); };
        term.onContextMenu = [this](glaze::Widget* s, glaze::ContextMenuArgs& e) {
            meny.items[0].enabled = term.hasSelection();
            meny.popupAt(*s, e.x, e.y);
        };
        meny.addItem("Kopiera").onClick = [this] { term.copy(); };
        meny.addItem("Klistra in").onClick = [this] { term.paste(gfx::clipboardText()); };
        statusBar.adopt(status);
        root().adopt(term);
        onLoad = [this] {
            if (!term.start())             // startlogik i onLoad, aldrig i konstruktorn
                status.setText(term.lastError());
        };
    }

private:
    glaze::Terminal term;
    glaze::Label    status;
    glaze::Menu     meny;                  // terminalen har ingen egen snabbmeny
};
```
```cmake
target_link_libraries(minapp PRIVATE glaze glaze_terminal_ui)
```

### .glazeui
```cpp
term.setProgram("pwsh");
term.setArguments("-NoLogo");
term.setWorkingDirectory("C:/src");
term.setColorScheme(glaze::TerminalScheme::Solarized);
term.setColorMode(glaze::TerminalColorMode::Auto);
term.setScrollback(5000);
term.setTextSize(glaze::px(12));
term.setEnvironmentVariable("LANG", "sv_SE.UTF-8");
term.setSchemeColor(glaze::ThemeMode::Dark, glaze::TerminalColor::Red, gfx::Color{230, 90, 80});
term.onExit = [this](glaze::Widget* s, glaze::EventArgs& e) { termSlut(s, e); };
term.onTitleChange = [this] { visaTitel(); };
```
Previewn (glaze-render, GlazeBuilder) visar en TOM terminal i rätt färger och storlek och startar ALDRIG ett program: `start()`, `write(…)`, `kill()`, `paste(…)` är opaka där. Utan sidopaketet i verktygsbygget (`-DGLAZE_TERMINAL=OFF`) är typen en stand-in.

### Fallgropar
- **Tangenterna går till PROGRAMMET**: Tab, Escape, Ctrl+C (avbrottet), pilarna och F-tangenterna — före fönstrets acceleratorer, avbrytknapp och fokusvandring. Vägarna ut är Ctrl+Tab, musen och appens `onKeyDown` med `handled` (fokusenheten frågas först). Kopiera/klistra in: Ctrl+Shift+C/V och Ctrl/Shift+Insert (Windows, Linux), Cmd+C/V (macOS).
- Ctrl+Alt är AltGr på Windows: `@` och `€` skrivs som text, och Ctrl+Alt+bokstav når aldrig programmet som ett styrtecken.
- Ingen inbyggd snabbmeny — paketet äger inga strängar; appen bygger sin på `onContextMenu` med `copy()`/`paste()`.
- `start()` kräver ett ÖPPET fönster (storleken tas ur layouten) — anropa den i `onLoad`/`onShow`.
- Inte i första versionen: omflödning vid storleksändring, IME-kandidatfönstrets plats, BEL (ingen ljudsignal, ingen blinkning), OSC 8-länkar, Sixel och bilder, DECKPAM för numpaden, den primära markeringen på Linux och OSC 52 (programmet skriver aldrig i urklippet).
- Breda tecken där ConPTY:s egen tabell och vår räknar olika ger förskjutna rader på Windows (en känd ConPTY-egenhet).
- En ZWJ-sekvens (👨‍👩‍👧, en hudton, en flagga) tar rätt antal celler, men ritas glyf för glyf av textvägen — inte som ett format kluster. Ett kluster rymmer högst 32 kodpunkter; resten sväljs.
- `onExit` kommer först när programmet avslutats OCH dess sista utdata är levererad. Lever ett bakgrundsjobb kvar med terminalen öppen (POSIX) kommer händelsen när också det släppt den.

<!-- END:terminal -->

<!-- BEGIN:openxr | openxr vr xr headset virtual-reality pc-vr stereo huvudspårning head-tracking handkontroller controllers stråle ray castray förflyttning locomotion glaze_openxr glaze_openxr_ui session -->
## OpenXR — en Canvas-scen i ett PC-VR-headset (`openxr`)

**Alias:** openxr, vr, xr, headset, virtual-reality, pc-vr, stereo, huvudspårning, head-tracking, handkontroller, controllers, stråle, ray, castray, förflyttning, locomotion, glaze_openxr, glaze_openxr_ui, session
**Include:** `#include <glaze_openxr/session.h>` · **CMake:** `glaze_openxr_ui` (med `-DGLAZE_OPENXR=ON`) · **Källa:** [session.h](OpenXR/include/glaze_openxr/session.h), [math.h](OpenXR/include/glaze_openxr/math.h), [CLAUDE.md](OpenXR/CLAUDE.md)

`glaze::openxr::Session` visar en befintlig `glaze::Canvas`-scen med `Camera3D` i ett PC-VR-headset genom OpenXR — den runtime som är aktiv på datorn, oftast registrerad av headsetets PC-programvara. Huvudet spåras i stereo och världen står still, vänster spak går och sidstegar, höger spak vrider, och avtryckaren skjuter en stråle genom `Canvas::castRay` till appens `onPick`. Canvas ritar scenen DIREKT i headsetets bilder: på fönstrets egen D3D11-enhet, med varje ögas asymmetriska frustum, i runtimens fulla rekommenderade upplösning (aldrig nedskalad) och med MSAA. Paketet har ingen egen 3D-motor och ingen CPU-kopia av bilderna. OpenXR-anropen för väntan och bildinhämtning sköts i en egen tråd; allt som rör Canvas och grafikenheten sker i UI-tråden. Medan en session kör ritas skrivbordet osynkat och högst 30 gånger per sekund, så att fönstrets present aldrig blockerar headsetets takt. Sidopaketet `OpenXR/` — `glaze` länkar det aldrig, och det finns bara på Windows.

### API
- `Session(glaze::Canvas& canvas, Callbacks callbacks)` · `~Session()` (stoppar och väntar in sin tråd) — ej kopierbar eller flyttbar.
- `void start(const Options& options = {})` — asynkront; förankringen tas ur canvasens kamera (huvudet i `eye()`, blicken mot `azimuth`) · `void requestStop()` — blockerar inte · `State state() const` — `Stopped`, `Starting`, `Waiting`, `Running`, `Stopping`, `Failed`.
- `Options` i fältordning: `msaaSamples` (4; 4, 2 eller 1 — kantutjämningen i ögonen; bilderna har alltid runtimens rekommenderade mått), `moveMetresPerSecond` (1), `turnDegreesPerSecond` (60), `stickDeadzone` (0,2), `rayLengthMetres` (20), `nearMetres` (0,05), `farMetres` (1000).
- `Callbacks`: `onStatus(const Status&)` (`state` + `message` — felen som text) · `onFrame(const FrameInfo&)` (före ritningen: `controllers[0]` vänster, `[1]` höger, med `grip`/`aim` som `WorldPose` i världen, spak och avtryckare) · `onPick(const PickEvent&)` (en gång per nytt avtryck: `hand`, `ray`, `hits` närmast först). Alla i UI-tråden.
- Kärnan (`glaze_openxr`, `<glaze_openxr/math.h>`) är ren std: `makeAnchor`, `toWorldPose`, `cameraAngles`.
- Kortet väljs av APPEN före första fönstret, med `gfx::setGpuPreference(gfx::GpuPreference::HighPerformance)` eller `gfx::setGpuAdapter(id)` (`gfx`). Paketet kontaktar runtimen först när `start()` körs; då kontrolleras att fönstrets renderare ritar med D3D11 på det kort runtimen kräver och med tillräcklig funktionsnivå.

### Exempel
```cpp
#include <glaze/glaze.h>
#include <glaze_openxr/session.h>   // UTANFÖR GlazeBuilder-regionen

class Vr : public glaze::AppWindow {
public:
    Vr() : glaze::AppWindow("VR", 1000, 640)
    {
        vrKnapp.name = "vrKnapp";
        vrKnapp.setText("Starta VR");
        vrKnapp.onClick = [this] {
            if (session_->state() == glaze::openxr::State::Stopped ||
                session_->state() == glaze::openxr::State::Failed)
                session_->start();
            else
                session_->requestStop();
        };
        verktyg.adopt(vrKnapp);

        scen.name = "scen";
        scen.dock = glaze::Dock::Fill;
        scen.setCamera(glaze::Camera3D::lookFrom({0, -6, 1.7}, 0, -5));   // meter, z upp
        scen.addShape(glaze::Shape::box({{-0.5, -0.5, 0}, {0.5, 0.5, 1}}));

        glaze::openxr::Callbacks cb;
        cb.onStatus = [this](const glaze::openxr::Status& s) { setTitle("VR — " + s.message); };
        cb.onPick   = [this](const glaze::openxr::PickEvent& p) {
            if (!p.hits.empty()) setTitle("Träff i lager " + std::to_string(p.hits.front().layer));
        };
        session_ = std::make_unique<glaze::openxr::Session>(scen, std::move(cb));

        root().adopt(verktyg);
        root().adopt(scen);
    }

private:
    glaze::ToolBar    verktyg;
    glaze::ToolButton vrKnapp;
    glaze::Canvas     scen;                               // FÖRE sessionen
    std::unique_ptr<glaze::openxr::Session> session_;    // förstörs först
};

void valjKort()   // först i main, före det första fönstret
{
    gfx::setGpuPreference(gfx::GpuPreference::HighPerformance);
}
```
```cmake
set(GLAZE_OPENXR ON CACHE BOOL "" FORCE)   # före FetchContent_MakeAvailable(GlazeUI)
target_link_libraries(vr PRIVATE glaze glaze_openxr_ui)
glaze_openxr_runtime(vr)                   # openxr_loader.dll bredvid programmet
```

### .glazeui
Ingen — sessionen är varken widget eller komponent och skapas i användarkoden (som `glaze::Worker`). Canvas och dess scen har sina vanliga former (`canvas-3d`).

### Fallgropar
- Canvas ska ha en `Camera3D` med `UpAxis::Z` och en världsenhet per METER — annars `Failed` med skälet.
- Deklarera Canvas FÖRE sessionen: medlemmarna förstörs baklänges, och sessionen ska dö först.
- Stäng inte fönstret medan VR går: veta `onClose` (`e.cancel = true`), anropa `requestStop()` och stäng när `onStatus` säger `Stopped` eller `Failed`.
- `GLAZE_OPENXR` är AV som förval och hämtar OpenXR-loadern med FetchContent; på annat OS än Windows stoppar den konfigureringen.
- Runtimen är den som är registrerad som aktiv i Windows (`HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime`) — paketet väljer ingen själv. Khronos `openxr_runtime_list.exe` och `hello_xr.exe -g D3D11` (i loader-zipen, `_deps/openxrloader-src/x64/bin/`) visar vilken runtime som är aktiv och om headsetet svarar, innan appens kod är inblandad.
- Fel kort: sessionen går till `Failed` med det förväntade och det faktiska kortets namn och id. Välj kortet före första fönstret (`gfx::setGpuPreference`/`setGpuAdapter`); enheten byggs aldrig om automatiskt.
- Skrivbordet ritas högst 30 gånger per sekund medan sessionen kör och går tillbaka till full takt efter stopp. En ändring som väntar ritas vid nästa tillåtna tillfälle.
- Ritningen kör huvudvyns lager med appens `onPaint`/`onPaintLayer` en gång per öga och bildruta — de får inte ändra tillstånd per anrop. Del-vyerna, kantlinjen och fokusringen hör till fönstret och ritas inte i headsetet. `castRay` träffar ytor, inte linjer eller etiketter, så strålar och markeringar i egna linjelager blir aldrig träffar.
- `msaaSamples` kostar GPU-tid: 4 är förvalet, och 2 eller 1 kan behövas om runtimen missar sina bildperioder. Antalet begränsas av vad kortet stöder.
- Stoppet tar några bildrutor: GPU:n blir först klar med det som skickats, sedan släpps bilderna. `Stopped` eller `Failed` kommer i `onStatus` när allt är avvecklat.
- Efter en fokusförlust (en meny i headsetet) ger avtryckaren ett val först efter ett släpp.

<!-- END:openxr -->

<!-- BEGIN:gfx | gfx fasad facade color färg key tangent keyboard kmodctrl modifierare event log openurl gfx-openfile standardprogram knownplaces displaymetrics setgpupreference gpuadapters setgpuadapter grafikkort decodeimage encodepng urklipp clipboard keydown-text tangentens-text devicename gpu adapter renderstats gputime -->
## `gfx::` — det en app rör i fasaden (`gfx`)

**Alias:** gfx, fasad, facade, color, färg, key, tangent, keyboard, kmodctrl, modifierare, event, log, openurl, gfx-openfile, standardprogram, knownplaces, displaymetrics, setgpupreference, gpuadapters, setgpuadapter, grafikkort, decodeimage, encodepng, urklipp, clipboard, keydown-text, tangentens-text, devicename, gpu, adapter, renderstats, gputime
**Include:** `#include <glaze/glaze.h>` (drar in `gfx::`-typerna — inkludera aldrig `gfx/` direkt) · **CMake:** `glaze` · **Källa:** [events.h](Glaze/gfx/include/gfx/events.h), [platform.h](Glaze/gfx/include/gfx/platform.h), [window.h](Glaze/gfx/include/gfx/window.h), [renderer.h](Glaze/gfx/include/gfx/renderer.h), [native.h](Glaze/gfx/include/gfx/native.h), [image.h](Glaze/gfx/include/gfx/image.h), [color.h](Glaze/gfx/include/gfx/color.h)

`gfx::` är render-fasaden under `glaze::`: egna POD-typer och fria plattformsfunktioner som varje backend implementerar. En app använder en handfull — färgen, tangenterna, loggen, URL:er, urklippet, kända platser, skärmens mått och PNG — och aldrig några backend-typer (Win32, AppKit, Wayland, X11).

### API
- `struct Color { std::uint8_t r; std::uint8_t g; std::uint8_t b; std::uint8_t a = 255; };` · `struct Rectf { float x, y, w, h; }` · `Sizef` · `Pointf` — geometrin i fysiska px.
- `enum class Key` — LEVEL-0-SYMBOLEN i användarens layout: `A`–`Z`, `N0`–`N9`, `F1`–`F12`, `Return`, `Escape`, `Tab`, `Space`, pilarna, `Home`, `End`, `PageUp`, `PageDown`, `Insert`, `Delete`, skiljetecken (`Plus`, `Equals` …), numpaden (`KP_N0` …, `KP_End` …).
- `kModCtrl` · `kModShift` · `kModAlt` · `kModCmd` — `Event::mods`-masken (Cmd = Command/Windows-tangenten/Super).
- `struct Event { EventType type; float mouseX, mouseY; MouseButton button; Key key; std::uint32_t mods; float wheelX, wheelY; std::uint64_t timerId; char text[32]; … }` — det `AppWindow::event()` ser; tecken kommer som `EventType::TextInput`, kommandon som `KeyDown`. Sedan REQ3 20 BÄR `KeyDown` också `text`: det trycket SKRIVER, UTF-8 — exakt det (de) `TextInput`-event som köas direkt efter i samma fönster. Tom för modifierare, navigering, funktionstangenter, Enter/Tab/Escape/Backspace, en död tangent (tecknet kommer med nästa tryck) och en kombination med Ctrl, Alt eller Cmd/Win/Super — men inte AltGr eller macOS Option, som är skrivtangenter. Upprepningen bär texten igen, `KeyUp` har ingen. Widgetarnas `onKeyDown` får den som `KeyArgs::text` (`events`).
- `EventType::MouseCaptureLost` — OS:et tog musen mitt i ett tryck och släppet kommer aldrig (Windows `WM_CAPTURECHANGED`; Wayland när pekaren lämnar fönstret under ett tryck, utom till appens egen popup; X11, macOS och headless aldrig — där bär fokusförlusten avbrottet). `AppWindow` gör det till kroken `mouseCancel` och appens `onMouseCancel`; en app ser själva eventet bara i `event()`.
- `void log(const char* fmt, ...);` · `bool openUrl(const std::string& url);` · `bool openFile(const std::string& path);` — en lokal UTF-8-sökväg i standardprogrammet (ingen URL-kodning i appen) · `std::string clipboardText();` · `void setClipboardText(const std::string& utf8);` · `std::uint64_t ticksMs();` · `std::string basePath();` · `SystemTheme systemTheme();` · `std::string systemLocale();`
- `std::vector<Place> knownPlaces();` (`Place { PlaceKind kind; std::string path; std::string label; }`) · `bool isHiddenFile(const std::string& utf8Path);`
- `void setGpuPreference(GpuPreference p);` — `Default`/`HighPerformance`/`LowPower`, FÖRST i `main`: före första fönstret, före appens egna trådar och före andra EGL-användare (på Linux sätts valet som miljövariabler i `gfx::init`, och `setenv` är inte trådsäkert). Windows och macOS: vid varje ny renderare; Linux: bara i `gfx::init`.
- GRAFIKKORTEN (sedan 0.1.10): `std::vector<GpuAdapter> gpuAdapters();` — `GpuAdapter { std::string id, name, vendor; std::uint32_t vendorId, deviceId; GpuKind kind; std::uint64_t memoryBytes; bool selectable, isDefault; std::optional<std::uint64_t> localAdapterId; }` och `enum class GpuKind { Unknown, Integrated, Discrete, External, Software }`. Listan har inga sidoeffekter och är säker före första fönstret. `id` HÅLLER ÖVER OMSTART och är det appen sparar: `pci:vvvv:dddd:ssssssss:rr` (samma på Windows och Linux; `@bb:dd.f` bara när två kort annars får samma id), på macOS `metal:<namn>`. `selectable` = kortet kan väljas här; `isDefault` = det `Default` ritar med (UPPSKATTAT på macOS och Linux). `localAdapterId` är kortets id för DEN HÄR körningen, så som ett annat grafik-API anger det (D3D11: LUID:ens två halvor, HighPart i de övre 32 bitarna); tomt där det saknas, och det byter värde mellan körningar — det som sparas är `id`.
- `void setGpuAdapter(const std::string& id);` · `std::string gpuAdapter();` — det uttryckliga kortet ("" = inget), samma tidpunkt och trådförkrav som preferensen. Ett id som finns och är valbart vinner; ett som saknas eller inte går att välja loggas en gång, och då väljer preferensen bland de valbara. `gpuAdapter()` svarar med BEGÄRAN.
- `std::vector<std::string> childEnvironmentRemovals();` — miljövariablerna GlazeUI själv satt (Linux: `DRI_PRIME`/`__NV_PRIME_RENDER_OFFLOAD`), som en barnprocess inte ska ärva. `openUrl`/`openFile` och terminalen tar redan bort dem; en app som startar processer själv gör det också. Tom på Windows och macOS.
- `const std::string& Renderer::deviceName() const` — GPU:n som FAKTISKT ritar fönstret, nådd som `win.window().renderer().deviceName()`: DXGI-adapterns beskrivning, `MTLDevice.name` eller `GL_RENDERER`. Backenden skriver det också i loggen: D3D11 en gång per process varje adapter (id, namn, slag, tillverkare, eget minne), och alla tre en rad `D3D11:`/`Metal:`/`GL: ritar med <GPU> [<id>] (preferensen <val>, kortet <id>)` när en enhet skapas på en ANNAN GPU än den förra — en popup på samma GPU loggar ingenting.
- `const std::string& Renderer::adapterId() const` — `GpuAdapter::id` för kortet som FAKTISKT ritar. Tomt när det inte går att avgöra: mjukvarurendering (llvmpipe, WARP utan träff) och EGL utan `EGL_EXT_device_query`.
- `const RenderStats& Renderer::stats() const` — renderarens räknare sedan start (`drawCalls`, `programSwitches`, `buffersCreated`, `bytesUploaded`, `vertices`, `instances`, `programsBuilt`, `uploadBreaks`) och sedan 0.1.9 `presentNs` (presentationens väntan — vsync, en ledig drawable; nära 0 i ett dolt fönster), `compileNs` (tiden att kompilera program) och `targetsCreated`. `std::uint64_t frameSerial() const` — framens löpnummer.
- GPU-TIDEN per frame: `void setGpuTiming(bool on)` · `bool gpuTiming() const` · `bool gpuTimingSupported() const` · `void takeGpuTimes(std::vector<GpuTime>& out)` (`GpuTime { std::uint64_t serial; std::uint64_t ns; }`, svaret 2–3 frames senare, högst `Renderer::kMaxGpuTimes` i brevlådan). Profileraren gör allt detta åt appen (`profiler`). Ett offscreen-block (`beginOffscreen`/`endOffscreen`) mäts på D3D11 som en egen post under sitt `frameSerial`; Metal och GL mäter bara frames.
- INTEGRATION MED ETT ANNAT GRAFIK-API (`gfx/native.h`, för ett bibliotek som tar emot bildrutor på appens egen enhet; alla anrop i UI-tråden, bara D3D11): `NativeDevice Renderer::nativeDevice() const` — `{ NativeGraphicsApi api; void* device; std::uint64_t localAdapterId; std::uint32_t featureLevel; }`, där `device` är LÅNAD (D3D11: en `ID3D11Device*`, giltig lika länge som renderern) och `api == None` i en renderare utan stöd; renderarens kontext lämnas aldrig ut. `std::unique_ptr<RenderTarget> importRenderTarget(const ExternalTargetDesc&, RenderTarget* sharedAttachments, std::string& error)` — ett mål runt en bild som anroparen äger (`ExternalTargetDesc { api, image, width, height, TargetColorFormat color, TargetDesc target }`; D3D11: en `ID3D11Texture2D*` på renderarens enhet med en RGBA8- eller BGRA8-familj som matchar `color`, bindbar som render target och shaderresurs), med MSAA och djup enligt `target`. Värdena skrivs utan sRGB-kodning (färgerna är redan kodade), bilden rensas inte vid importen och målet binds med `pushTarget`/`popTarget` som ett vanligt. Fel enhet, mått eller format ger ett ogiltigt mål (`image().valid()` false) och skälet i `error`. `sharedAttachments` är ett tidigare importerat mål med samma mått och `target`; det nya delar då dess provtagna färgyta och djup. `void setExternalFramePacing(bool on)` — presenterar fönstret osynkat och utan att vänta (en present som inte hinner hoppas över), för en annan klocka än skärmens. `std::unique_ptr<GpuCompletion> insertCompletion(std::string& error)` — en GPU-markör efter allt som skickats; `GpuCompletion::poll(error)` svarar `Pending`, `Complete` eller `Failed` utan att vänta. Metal, Linux/GL och `NullRenderer` svarar "stöds inte": `api == None`, ett ogiltigt mål med feltext, ingen åtgärd respektive `nullptr` med feltext.
- `std::vector<ScreenInfo> screens();` — skärmarna i placeringsrymden (`bounds`, `work`, `scale`, `primary`; tom på Wayland och före första fönstret) · `enum class WindowState { Normal, Minimized, Maximized };` · `enum class PointerShape` med `ClosedHand`, `Hidden` och `Wait` sist (`window-placement`, `cursors`). Appens timer i fasaden (`gfx/timer.h`, `EventType::Timer` med windowId 0) är `glaze::Timer`s maskineri — använd `glaze::Timer` (`timer`).
- `bool encodePng(const std::string& path, const std::vector<std::uint8_t>& rgba, int w, int h);` · `bool decodeImage(const void* data, std::size_t size, std::vector<std::uint8_t>& rgba, int& w, int& h);` — RGBA8, rad 0 överst; BARA PNG; `decodeImage` är trådsäker.
- `struct DisplayMetrics { float pxPerMm; DisplaySource source; int widthMm; int heightMm; std::string id; std::string reason; }` — läs `AppWindow::display()` (med appens kalibrering).
- `virtual bool gfx::Font::hasGlyph(char32_t cp) const` — har fonten ELLER en reservfont ur OS:et en riktig glyf för kodpunkten? En font är en fil, men ett tecken den saknar (−, ≈, kinesiska …) ritas och mäts med OS:ets reservkedja (DirectWrite, Core Text, fontconfig); bara när INGEN font har det blir det ersättningstecknet U+FFFD, och då svarar `hasGlyph` false. Styrtecken (tabb, radbrytning) svarar alltid false.

### Exempel
```cpp
class KeyWindow : public glaze::AppWindow {
public:
    KeyWindow() : glaze::AppWindow("Tangenter", 400, 200) {}

protected:
    bool event(const gfx::Event& e) override
    {
        if (e.type != gfx::EventType::KeyDown)
            return false;
        const bool zoomIn = e.key == gfx::Key::Plus ||                      // sv: egen tangent
                            (e.key == gfx::Key::Equals && (e.mods & gfx::kModShift));  // US
        const bool toEnd = e.key == gfx::Key::End || e.key == gfx::Key::KP_End;   // numpad
        if (e.text[0] != '\0')                // trycket skriver något ("å", "@" med AltGr)
            gfx::log("skriver %s", e.text);
        if (zoomIn || toEnd)
            gfx::log("zoom %d, slut %d", zoomIn ? 1 : 0, toEnd ? 1 : 0);
        return zoomIn || toEnd;                // true = ätet + full omritning
    }
};

void beforeFirstWindow(const std::string& sparatKort)   // först i main
{
    gfx::setGpuPreference(gfx::GpuPreference::HighPerformance);   // FÖRE första fönstret
    gfx::setGpuAdapter(sparatKort);   // id:t ur appens inställningar; vinner om det finns
}

// Inställningarnas lista: bara de valbara korten, förvalet markerat.
std::vector<std::string> gpuChoices()
{
    std::vector<std::string> rader;
    for (const gfx::GpuAdapter& a : gfx::gpuAdapters())
        if (a.selectable)
            rader.push_back(a.name + (a.isDefault ? " (förval)" : ""));   // spara a.id
    return rader;
}

void gfxTour(glaze::AppWindow& win)
{
    gfx::log("GlazeUI %s, %.2f px/mm", GLAZE_VERSION_STRING,
             static_cast<double>(win.display().pxPerMm));
    gfx::log("ritar med %s [%s]", win.window().renderer().deviceName().c_str(),
             win.window().renderer().adapterId().c_str());   // efter show()
    gfx::openUrl("https://github.com/holmbom/GlazeUI");
    gfx::openFile("rapporter/Årsrapport 2026.pdf");   // sökvägen som den är
    for (const gfx::Place& p : gfx::knownPlaces())
        if (p.kind == gfx::PlaceKind::Documents)
            gfx::log("dokument: %s", p.path.c_str());
    gfx::setClipboardText("kopierat");
    const std::vector<std::uint8_t> rgba(2 * 2 * 4, 255);   // 2×2 vita pixlar, RGBA8
    gfx::encodePng("vit.png", rgba, 2, 2);
}
```

### .glazeui
Ingen form — fasaden är kod. En färg i `.glazeui` skrivs `gfx::Color{40, 90, 180, 255}` (heltal 0–255).

### Fallgropar
- Bind den SYMBOL du menar: "Ctrl+Plus" är `Key::Plus` på svensk layout men `Equals` + `kModShift` på amerikansk — bind båda. `Key` flimrar inte med Shift: Shift+1 är `N1` + `kModShift`.
- `KeyDown`s text är LIKA på de tre sedan REQ3 20: på Linux skriver Alt+a och Ctrl+1 inte längre något (som på Windows och macOS). Inte IME, och inte Windows Alt+numpad-koden — dess tecken kommer som ett `TextInput` vid Alt-släppet, utan ett `KeyDown` med text. Ett fält med fokus tar ett tryck med text FÖRE fönstrets genvägar, så en genväg utan modifierare (`{Key::M, 0}`) körs inte medan man skriver (`menus`).
- Numpaden med NumLock av ger EGNA poster (`KP_End`, `KP_Home` …) på Windows och Linux; macOS ger alltid siffrorna. `event()` och menyernas genvägar ser den oviktade posten — bind `End` OCH `KP_End`; widgetarna får den vikt.
- `gfx::log` hamnar i stderr och debug-utgången på Windows, i unified log + stderr på macOS, i stderr på Linux.
- `openUrl` och `openFile` svarar "skickad", inte "visad"; `openFile` ger false för en tom sökväg, en fil som inte finns och en typ utan standardprogram. `decodeImage` tar bara PNG — annan avkodning är appens.
- Reservfonten väljs av OS:et, så ett tecken utanför UI-fonten kan se olika ut på de tre plattformarna — och saknas det på maskinen (CJK på en Linux utan CJK-fonter) ritas ersättningstecknet. Färgemoji är inget mål: Windows ritar emojifontens enfärgade glyf, macOS färgglyfens alfa, och Linux väljer bort färgfonter.
- Fasaden är UI-trådens. Undantagen är `decodeImage` och väckningen under `glaze::app().post`.
- Fönstrets ikon (`gfx::Window::setIcon(WindowIcon)`, `iconApplied()`) och app_id (`gfx::setAppId`/`appId`) sköts av `AppWindow` via `appIcon` — en app rör dem inte direkt. `iconApplied()` svarar vad som FAKTISKT gäller: false på Wayland utan `xdg-toplevel-icon-v1`.
- Systemfältets fasad (`gfx/tray.h`) och `gfx::setShowInDock` sköts av `glaze::TrayIcon` respektive `glaze::setShowInDock`. Ett `EventType::Tray` når aldrig `AppWindow::event()` — det bär windowId 0, och App routar det till ikonen (`tray-icon`).
- `deviceName()` är tom innan fönstret öppnats (enheten skapas i `show()`). Vilken GPU som ritar är OS:ets val med `GpuPreference::Default` — på en dator med två grafikkort kan det bli det integrerade, också när programmet exporterar `NvOptimusEnablement`. Välj `HighPerformance` uttryckligen och läs `deviceName()` (eller loggraden) för att veta.
- LINUX VÄLJER KORTET BARA VID START: `gfx::init` sätter `__NV_PRIME_RENDER_OFFLOAD=1` (NVIDIA:s drivrutin — aldrig `DRI_PRIME`, då hade Mesa ritat i mjukvara) eller `DRI_PRIME=pci-…` (Mesa) innan EGL laddas. Ett senare `setGpuAdapter`/`setGpuPreference` gäller nästa start (loggat). En variabel ANVÄNDAREN satt (`DRI_PRIME`, `__NV_PRIME_RENDER_OFFLOAD`, `__GLX_VENDOR_LIBRARY_NAME`, `__EGL_VENDOR_LIBRARY_*`, `LIBGL_ALWAYS_SOFTWARE`) vinner helt — då är inget kort valbart. En variabel som är satt men TOM räknas som osatt och skrivs över. Ett uttryckligt Mesa-kort får `DRI_PRIME` också när förvalskortet (`boot_vga`, uppskattat) drivs av NVIDIA:s drivrutin — kompositorn kan köra på Mesa; bara PREFERENSEN undviker det. Ett NVIDIA-kort bland flera går inte att välja.
- TRÅDARNA OCH `setenv`: glibc:s `getenv` är inte låst mot `setenv`. Välj kortet först i `main` och starta appens trådar (en `Worker` som läser miljön, libcurl:s proxy) efter första `show()`.
- Slaget är en HEURISTIK utanför Windows hybridflaggor: på Linux listas en APU med minst 1 GiB UMA-buffert som Discrete och Intel Arc som Integrated; på macOS är `memoryBytes` en andel av RAM när minnet är delat. Id:t är inte LUID:en (den byts vid omstart) och inte namnet (det byts med drivrutinen).
- `stats()` nollställs aldrig av sig själv (`resetStats()` är mätarens). Profileraren läser den som en differens över varje bild och nollställer aldrig, så appens egna läsningar störs inte. `takeGpuTimes` däremot TÖMMER brevlådan: en app som hämtar GPU-tiden själv tar svaren från profileraren.
<!-- END:gfx -->

<!-- BEGIN:platforms | platforms plattformar windows macos mac linux wayland x11 d3d11 metal backend skillnader differences degradering numlock fönsterknappar rundade-hörn storleksram resize-ram -->
## Plattformsskillnader (`platforms`)

**Alias:** platforms, plattformar, windows, macos, mac, linux, wayland, x11, d3d11, metal, backend, skillnader, differences, degradering, numlock, fönsterknappar, rundade-hörn, storleksram, resize-ram
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` (`GLAZE_BACKEND`) · **Källa:** [CLAUDE.md](CLAUDE.md), [gfx/window.h](Glaze/gfx/include/gfx/window.h), [gfx/events.h](Glaze/gfx/include/gfx/events.h), [doc/os/README.md](doc/os/README.md)

Glaze ritar varje pixel självt — widgets, namnlisten och dialogerna — så appen ser likadan ut överallt. EN backend byggs in per plattform: D3D11 + DirectWrite (Windows), Metal + Core Text (macOS) och på Linux Wayland ELLER X11 (valt i körtid) med EGL/GL och FreeType. Skillnaderna är det OS:et inte låter oss styra, och där svarar API:t med vad som faktiskt gäller i stället för att låtsas.

### API
| Fråga | Windows | macOS | Linux X11 | Linux Wayland |
|---|---|---|---|---|
| `setTopmost` / `topmost()` | ja | ja | ja | **nej** — no-op + en loggrad, `topmost()` alltid false |
| Skärmposition (`setPosition`, `screenArea`) | ja | ja | ja | **nej** — popups placeras via popup-ankaret |
| `minimize()` och av-minimering | ja | ja | ja | enkelriktat — användaren tar fram fönstret |
| Numpaden med NumLock av | `KP_End` … | alltid siffror | `KP_End` … | `KP_End` … |
| Fönsterknapparna (kapseln) | höger: minimera · maximera · stäng | vänster: stäng · minimera · maximera | höger | höger |
| Rundade fönsterhörn | nej (eget pass) | OS:et rundar | ja (utan ARGB-visual: fyrkantigt) | ja |
| Storleksramen (eget chrome) | UTANFÖR den synliga kanten (OS:ets osynliga ram); överkanten ett band innanför | OS:et äger kanterna | utanför (genomskinlig marginal) med kompositör + WM-stöd för `_GTK_FRAME_EXTENTS`, annars 6 px innanför kanten | utanför (genomskinlig marginal); tiled: innanför |
| `gfx::log` | stderr + debug-utgången | unified log + stderr | stderr | stderr |
| `gfx::setGpuPreference` / `setGpuAdapter` | ja, per ny renderare | ja, per ny renderare | ja, vid start (NVIDIA-offload eller `DRI_PRIME=pci-…`), användarens variabel vinner | ja, vid start (som X11) |
| Systemfältet (`glaze::TrayIcon`) | ja — meddelandefältet | ja — menyraden | **nej** — `isSupported()` false | **nej** |
| `setShowInTaskbar(false)` | ja — Alt+Tab kvar | nej (dockan är appens) — loggas | nej — loggas | nej — loggas |
| `glaze::setShowInDock(false)` | nej — loggas | ja | nej — loggas | nej — loggas |
| `setLocation`, `setStartPosition`, `geometry().hasPosition` | ja | ja | ja | **nej** — loggas, `hasPosition` false |
| Start minimerad (`setWindowState`) | ja | ja | ja | **nej** — loggas, normal start |
| `PointerShape::Wait` | timglaset | **pilen** | klockan | temats `wait` |
| Avbrutet tryck (`onMouseCancel`) | fokusförlust, och när OS:et tar musen (`WM_CAPTURECHANGED`) | fokusförlust | fokusförlust (`FocusOut`) | fokusförlust, och när pekaren lämnar fönstret under trycket (inte till egen popup) |
| Integration med ett annat grafik-API (`gfx/native.h`: `nativeDevice`, `importRenderTarget`, `setExternalFramePacing`, `insertCompletion`) | ja (D3D11) | **nej** — `api == None`, ogiltigt mål med feltext, `nullptr` med feltext | **nej** — som macOS | **nej** — som macOS |
| `GpuAdapter::localAdapterId` | ja — LUID:en | tomt | tomt | tomt |

### Exempel
```cpp
void pinOnTop(glaze::AppWindow& win)
{
    win.setTopmost(true);
    if (!win.topmost())   // Wayland: alltid false — degraderingen är läsbar
        gfx::log("alltid överst stöds inte här");
}
```

### .glazeui
Ingen plattformsgren finns eller behövs — samma fil ritas likadant överallt, och previewn ritas av samma backend som appen på maskinen.

### Fallgropar
- Skriv inga `#ifdef _WIN32` för utseende eller placering. Popups placeras via ankare (`AppWindow::setPopupAnchor`), aldrig i skärmkoordinater.
- Läs svaret efter ett plattformsberoende anrop (`topmost()`) i stället för att anta att det gick fram.
- Storleksramen ligger UTANFÖR den synliga kanten, så innehåll vid kanten (ett verktygsfält mot vänsterkanten) är klickbart ända ut. Undantagen: Linux X11 utan kompositör eller utan WM-stöd för `_GTK_FRAME_EXTENTS` och ett tiled Wayland-fönster — där tar ramen de yttersta 6 px innanför kanten. På Windows är de översta px av namnlisten (och dess knappar) storleksändring uppåt, som i Windows egna fönster.
- På Wayland når modifierarna bara ett fönster med tangentbordsfokus — ett Ctrl-klick i ett inaktivt fönster kan komma utan Ctrl.
- macOS sväljer släppet av en tangent som släpps medan Cmd hålls; glaze levererar det själv i pumpen, så `onKeyUp` och `isKeyHeld` stämmer också för Cmd+W (skrivet blint, oprövat på Mac).
- macOS-backenden är kompilerad och grindad sedan 2026-10-12, men det som kräver händer och ögon är oprövat där, och nyare delar (systemfältet) är skrivna blint; Windows och Linux grindas. Det som återstår per OS står i [doc/os/](doc/os/README.md).
- På Linux ritas ett tecken som UI-fonten saknar som "�" (ingen glyfreserv än, TODO 51) — undvik ovanliga tecken som − och ≈ i UI-text.
- glaze-render och GlazeBuilder kräver en skärmsession (WindowServer på macOS, en display på Linux) — ingen ren headless-CI.
- Programikonen sitter till VÄNSTER på Windows/Linux och till HÖGER på macOS. macOS har EN ikon för hela appen (dockan), Wayland visar fönstrets ikon bara med `xdg-toplevel-icon-v1` (KWin, Labwc) och GNOME tar den ur en installerad `.desktop` som matchar app_id:t (`app-icon`).
- Systemfältet finns på Windows och macOS men inte på Linux än: fråga `glaze::TrayIcon::isSupported()` innan appen gömmer sig — en dold app utan ikon går inte att nå (`tray-icon`).
- En dockningspanel kan flyta på alla plattformar, men det LEVANDE DRAGET (en panel eller ett verktygsfält lossnar och följer pekaren) kräver skärmpositioner: `glaze::DockHost::isFloatDragSupported()` är false på Wayland, där draget bara visar platshållaren, menyn och dubbelklicket gör jobbet och kompositören placerar fönstret (`widget-dock`).
- Tangentens text (`KeyArgs::text`) är densamma på alla tre: Alt+bokstav och Ctrl+siffra skriver ingenting på Linux heller (AltGr gör det), Option skriver på macOS. `onResize` kommer under ett fönsterdrag först vid släppet på Windows och macOS (OS:ets modala loop), per bildruta på Linux.
<!-- END:platforms -->

<!-- BEGIN:profiler | profiler profilerare prestanda performance frametid bildtakt fps mätning scope gpu-tid -->
## Profileraren — var tiden i en bild går (`profiler`)

**Alias:** profiler, profilerare, prestanda, performance, frametid, bildtakt, fps, mätning, scope, gpu-tid
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` · **Källa:** [profiler.h](Glaze/widgets/include/glaze/profiler.h)

`glaze::profiler()` mäter varje fönsters bilder och varje pumpvarv — fasernas tid, renderarens och ritytans räknare per bild och GPU:ns tid — och appens egna block under egna etiketter. Den är ALLTID inkompilerad och AV som förval, också i Release, så en app mäts där den körs: `glaze::profiler().setEnabled(true)` från vilken tråd som helst, eller miljövariabeln `GLAZE_PROFILE` (satt till något annat än tomt eller `0`) vid start. Den läses genom HÄMTNING — `frames`, `pump`, `summary`, `scopes`, `report` — och ber aldrig själv om en bild och väcker aldrig pumpen; en app som loggar en gång per sekund gör det med en `glaze::Timer`. Klockan är `steady_clock`, och ingenting klampas.

### API
- `Profiler& profiler();` — den enda; läser `GLAZE_PROFILE` vid första anropet och när App skapas.
- `void setEnabled(bool on)` · `bool enabled() const` — från vilken tråd som helst; avstängningen tömmer ingenting.
- `void setCapacity(std::size_t frames)` · `std::size_t capacity() const` — ringarnas storlek i poster, varje fönsters och pumpens; förval 4096.
- `std::vector<FrameProfile> frames(const AppWindow& w, std::size_t max = 0) const` · `std::vector<PumpProfile> pump(std::size_t max = 0) const` — den äldsta först, de `max` senaste (0 = hela ringen). Ett fönsters ring släpps när fönstret stängs.
- `WindowSummary summary(const AppWindow& w, double seconds = 0) const` — fönstrets bilder de senaste `seconds` sekunderna (0 = hela ringen).
- `std::vector<LabelStats> scopes(bool reset = false) const` — etiketterna i namnordning; `reset` nollar dem efter läsningen.
- `std::string report(double seconds = 0) const` — allt ovan som TEXT för appens logg: varje fönster med titel och GPU, pumpen och etiketterna. Avstängd: en rad som säger det.
- `void clear()` — tömmer ringarna och etiketterna.
- `struct FrameProfile` — `window` (fönstrets id), `frame` (renderarens `frameSerial`), `iteration` (pumpvarvet, 0 = utanför pumpen), `at`, `interval` (ms sedan fönstrets förra bild), `total`, `inputLatency` (den första inmatningen sedan förra bilden → bildens slut; 0 = ingen), `continued`, `outsidePump`, `ms` (per `ProfilePhase`) och `count` (per `ProfileCount`).
- `enum class ProfilePhase` — `Content` (innehållspasset, med layouten och ritytans fyra faser), `CanvasPrepare` (lagren, texten och rutnätet förbereds), `CanvasDrapes` (terrängens draperingar), `CanvasDraw` (scenen med lagrens målare, utan `onPaint`), `CanvasPaint` (`onPaint`), `Overlay`, `Present` (från overlayn till bildens slut, med väntan), `PresentWait` (vsync och en ledig drawable), `Gpu` (GPU:ns tid, i efterhand), `Compile` (program och varianter som byggdes).
- `enum class ProfileCount` — renderarens `DrawCalls`, `BytesUploaded`, `BuffersCreated`, `UploadBreaks`, `ProgramsBuilt`, `TargetsCreated`; ritytans `TargetRenders`, `DrapeRenders`, `DrapePixels`, `DrapeKeys`, `LayerRebuilds`, `GlyphBuilds`; skadan `DamageRects` och `FullDamage`.
- `struct PumpProfile` — `iteration`, `at`, och delarna i ms: `wait` (`waitEvent`), `events`, `posted`, `postedMax` (den längsta postade funktionen), `timers`, `commits`, `closes`; `eventCount`, `postedCount` och `depth` (1 = den yttersta pumpen).
- `struct WindowSummary` — `window`, `deviceName`, `frames`, `paced` (intervallens underlag), `intervalP50`/`intervalP95`/`intervalP99`/`intervalMax`, `missed50Hz` (intervall över 20 ms), `missed150Hz` (över 6,7 ms), `phaseP50`/`phaseP95` och `perFrame` (räknarnas medel per bild).
- `struct LabelStats` — `name`, `count`, `totalMs`, `p50`, `p95`, `p99`, `maxMs`.
- `class ProfileLabel` (`explicit ProfileLabel(std::string_view name)`, `id()`) · `class ProfileScope` (`explicit ProfileScope(const ProfileLabel&) noexcept` — tiden till blockets slut) · `void profileCount(const ProfileLabel& label, std::int64_t n = 1) noexcept` — appens egna mått, från vilken tråd som helst. Ramverkets verktyg är förregistrerade etiketter: `glaze.hitTest`, `glaze.castRay`, `glaze.heightAt`.
- `const char* profilePhaseName(ProfilePhase p)` · `const char* profileCountName(ProfileCount c)` — namnen i `report()` (`content`, `wait`, `draw_calls` …), för en app som skriver egna tabeller.

### Exempel
```cpp
class KartFonster : public glaze::AppWindow {
public:
    KartFonster() : glaze::AppWindow("Karta", 800, 600)
    {
        glaze::profiler().setEnabled(true);   // eller GLAZE_PROFILE=1 i miljön
        logg_.setInterval(1000);
        logg_.onTick = [this] {
            gfx::log("%s", glaze::profiler().report(1.0).c_str());   // den senaste sekunden
            const glaze::WindowSummary s = glaze::profiler().summary(*this, 1.0);
            if (s.missed50Hz > 0)
                gfx::log("%zu av %zu bilder över 20 ms (%s)", s.missed50Hz, s.paced,
                         s.deviceName.c_str());
        };
        glaze::AppWindow::onLoad = [this] { logg_.start(); };
    }

    void läsPlatta()   // anropas i en arbetstråd
    {
        static const glaze::ProfileLabel kLäs{"app.läsPlatta"};
        static const glaze::ProfileLabel kByte{"app.plattbyte"};
        glaze::ProfileScope mät(kLäs);        // tiden till blockets slut
        const std::int64_t byte = 4096;       // … läs och avkoda …
        glaze::profileCount(kByte, byte);     // räknar utan tid
    }

private:
    glaze::Timer logg_;
};
```

### .glazeui
Ingen form — profileraren är infrastruktur som `App::post`: den har inget utseende och mäter hela pumpen, inte ett fönster (utanför regel 8 med flit). Slå på den i kod eller med `GLAZE_PROFILE=1`.

### Fallgropar
- INTERVALLET räknas bara efter en bild med `continued` — nästa bild var begärd när den här blev klar (ritytan tickar, något glider, något låg postat). Pausen mellan två händelser är inte takten och räknas inte (`paced` är underlaget). **Ett känt mätfel:** `continued` frågar om NÅGOT ligger postat, inte om det gäller fönstret, så ett vilande fönster kan få en bild räknad som följd när ett annat fönsters poster väntar — enstaka intervall på hundratals ms i ett fönster som står still är det, inte hack.
- `Content` omfattar LAYOUTEN — placeringen görs i ritvandringen och har inget eget pass — och ritytans fyra faser ligger inom den. Med del-vyer läggs vyerna ihop.
- GPU-tiden kommer i efterhand, 2–3 bilder senare, och är 0 i en bild utan svar (`phaseP50`/`phaseP95` räknar `Gpu` bara över bilder med svar). En renderare som inte kan mäta svarar `gpuTimingSupported()` false (`gfx`). Metal och GL är skrivna blint.
- Ett dolt fönster väntar inte på vsync: `PresentWait` är nära 0 där. Mät takten med synliga fönster.
- En bild ritad UTANFÖR pumpen (D3D11:s modala storleksloop vid ett fönsterdrag) har `outsidePump`; `total` är då innehåll + overlay.
- Etiketternas percentiler kommer ur ett histogram med 8 fack per fördubbling och kan vara upp till ~9 % för höga; antalet, summan och maxvärdet är exakta. `summary` räknar bildernas percentiler ur själva värdena.
- `frames`, `pump`, `summary` och `report` läses i UI-tråden; `setEnabled`, `ProfileLabel`, `ProfileScope`, `profileCount` och `scopes` från vilken tråd som helst.
- Skapa en `ProfileLabel` EN gång (en medlem eller en `static`): konstruktionen slår upp namnet under registrets lås. Samma namn ger samma id i varje tråd och modul, och namnet kopieras in — en plugin delar registret med programmet.
- Avstängd kostar en `ProfileScope` ett funktionsanrop och en atomisk läsning, och pumpens krokar likadant; en scope som skapades avstängd spelar inte in, också om profileraren slås på under blocket.
- En NÄSTLAD pump — en modal dialog öppnad i en handler eller en postad funktion — har egna `PumpProfile`-poster med `depth` + 1 och räknas inte in i den yttre delens tid.
<!-- END:profiler -->

<!-- BEGIN:testing | testing test tester headless ctest add_test makewidget find resolve offscreen setoffscreen renderframe captureframe pixeltest skärmdump handleevent grind gate -->
## Testa en app utan att klicka (`testing`)

**Alias:** testing, test, tester, headless, ctest, add_test, makewidget, find, resolve, offscreen, setoffscreen, renderframe, captureframe, pixeltest, skärmdump, handleevent, grind, gate
**Include:** `#include <glaze/glaze.h>` · **CMake:** `glaze` + `enable_testing()`/`add_test` · **Källa:** [tests/CMakeLists.txt](tests/CMakeLists.txt), [tests/window_gate.cpp](tests/window_gate.cpp), [tests/post_gate.cpp](tests/post_gate.cpp), [tests/layout_gate.cpp](tests/layout_gate.cpp), [app_window.h](Glaze/widgets/include/glaze/app_window.h)

Det mesta går att testa utan ett synligt fönster. Konstruktorn av ett `AppWindow` skapar inget OS-fönster (tvåfas), så trädet, `find<T>`, settrarna och tab-ordningen kan provas rakt av, och `makeWidget` skapar widgets ur registret. Med `setOffscreen(true)` öppnas ett DOLT men riktigt fönster som ritar en frame och läses tillbaka till pixlar — samma väg som glaze-render.

### API
- `std::unique_ptr<Widget> makeWidget(const std::string& type);` · `std::vector<std::string> registeredWidgetTypes();` · `virtual bool setProp(const std::string& key, const std::string& value);`
- `template <class T = Widget> T* find(const std::string& name)` · `std::vector<Widget*> focusOrder();` · `Widget* nextFocus(int dir);` · `gfx::Sizef contentSize() const;`
- `void setOffscreen(bool on)` (före `show()`) · `window().renderFrame()` · `bool captureFrame(std::vector<std::uint8_t>& rgba, int& w, int& h)` (fysiska px, RGBA8) · `gfx::encodePng(…)`
- `gfx::Rectf resolve(const Ctx& c) const;` — geometri utan fönster mot en handbyggd `Ctx` med falsk font (mönstret i `tests/layout_gate.cpp`, som ärver `gfx::NullFont`).
- `void handleEvent(const gfx::Event& e);` (protected) — en subklass matar sin egen ingång med syntetiska events som går samma väg som OS:ets.
- `void Button::performClick();` — knappens klick utan mus (WinForms `PerformClick`): `onClick` och sedan knappens `DialogResult`, ingenting på en inaktiverad knapp. Testar en dialogs svar utan ett fönster på skärmen (`widget-button`).
- Pumpprov: `glaze::app().post(steg)` i kedja och sedan `run()`, sista steget anropar `close()`; ctests `TIMEOUT` fångar en hängning.

### Exempel
```cpp
// window_test.cpp — ett ctest-program: exit 0 = grönt, 1 = fel, 77 = hoppat
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include <glaze/glaze.h>

class TestWindow : public glaze::AppWindow {
public:
    TestWindow() : glaze::AppWindow("test", 320, 200)
    {
        auto* ok = add<glaze::Button>("OK");
        ok->name = "okBtn";
    }
};

int main()
{
    int  fel   = 0;
    auto check = [&](bool ok, const char* vad) {
        std::printf("%s %s\n", ok ? "OK " : "FEL", vad);
        fel += ok ? 0 : 1;
    };

    // 1. HEADLESS — konstruktorn skapar inget OS-fönster (tvåfas).
    TestWindow w;
    check(w.find<glaze::Button>("okBtn") != nullptr, "find<Button>(\"okBtn\")");
    check(w.focusOrder().size() == 1, "ett tabbstopp");
    std::unique_ptr<glaze::Widget> lbl = glaze::makeWidget("label");
    check(lbl && lbl->setProp("text", "Hej"), "makeWidget + setProp");

    // 2. DOLT FÖNSTER — rita EN frame och läs tillbaka pixlarna.
    w.setOffscreen(true);
    w.show();
    if (!w.isOpen()) {
        std::puts("ingen display — fönsterfallen hoppas över");
        return fel ? 1 : 77;
    }
    w.window().renderFrame();
    std::vector<std::uint8_t> px;
    int pw = 0, ph = 0;
    check(w.window().captureFrame(px, pw, ph) && pw > 0 && ph > 0, "captureFrame");
    gfx::encodePng("window_test.png", px, pw, ph);
    return fel ? 1 : 0;   // fönstret rivs i destruktorn — ingen pump behövs
}
```
```cmake
enable_testing()
add_executable(window_test tests/window_test.cpp)
target_link_libraries(window_test PRIVATE glaze)
add_test(NAME window_test COMMAND window_test)
set_tests_properties(window_test PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)
```

### .glazeui
Testa tripletten med glaze-render: `glaze-render src/main_window.h -o ut.png` — exit 0 = läsbar, 1 = fail-safe med diagnos på stderr, 2 = render- eller IO-fel (`glaze-render`).

### Fallgropar
- Testprogrammet är en KONSOL-exe (ingen `WIN32`), annars syns inte utskriften på Windows.
- Ett dolt fönster kräver en display: kolla `isOpen()` efter `show()` och returnera 77 (`SKIP_RETURN_CODE`) när den saknas, i stället för att göra testet rött.
- `run()` vägrar ett redan öppet fönster — anropa inte `show()` före `run()` på samma objekt. Utan pump rivs fönstret i destruktorn.
- `close()` och `post` verkställs i pump-svansen — utan `run()` händer de inte.
- Programmatiska settrar fyrar inga events: testa handlern genom att anropa den, eller via syntetiska events.
- Mät tillstånd (`isChecked()`, `text()`, `focusOrder()`) och pixlar för det som ritas — aldrig mot en kopia av produktionens egna formler.
<!-- END:testing -->

<!-- BEGIN:glaze-render | glaze-render render png preview förhandsvisning skärmdump screenshot cli offscreen triplett stdout exit-kod scale referensbild -->
## `glaze-render` — fönster-triplett → PNG (`glaze-render`)

**Alias:** glaze-render, render, png, preview, förhandsvisning, skärmdump, screenshot, cli, offscreen, triplett, stdout, exit-kod, scale, referensbild
**Include:** — (CLI) · **CMake:** target `glaze-render` (bara i GlazeUI-repot som toppprojekt) · **Källa:** [apps/glaze-render/CLAUDE.md](apps/glaze-render/CLAUDE.md), [doc/os/README.md](doc/os/README.md)

Ett konsolprogram som läser en triplett med samma läsare som GlazeBuilder, bygger widget-trädet och ritar EN frame i ett dolt fönster med exakt samma backend som appen — bilden är pixelidentisk med det körda fönstret (native skala, chrome med). Det är previewns motor i VS Code och agentens snabbaste sätt att SE en layout och att kontrollera att en handredigerad `.glazeui` är läsbar.

### API
- `glaze-render <fil.h|.glazeui|.cpp> [-o ut.png] [--scale <faktor>]` — valfri fil i tripletten; utan `-o` hamnar `<bas>.png` bredvid källan.
- `--scale <faktor>` — ABSOLUT skala (`--scale 1.0` ger samma pixelmått på alla maskiner); klampas till OS-skalan × 0.5–2.0.
- Exit: **0** = OK · **1** = läsarens fail-safe (diagnos `fil:rad: orsak` på stderr) · **2** = render- eller IO-fel, eller fel användning.
- Stdout vid OK är ett KONTRAKT på en rad: `<utfil>: <B>x<H> px (scale <s>)`; loggar går till stderr.

### Exempel
```sh
cmake --build GlazeUI/build --target glaze-render
GlazeUI/build/apps/glaze-render/glaze-render src/main_window.h -o /tmp/main_window.png --scale 1.0
# stdout: /tmp/main_window.png: 360x216 px (scale 1)
```
Agenten kan sedan läsa PNG:en (Read-verktyget visar bilder).

### .glazeui
Bilden visar det `.glazeui` uttrycker och previewn utför: layoutfält, settrar, kända metodanrop, menyraden, statusraden, designraden och ramen (`setChrome(gfx::Chrome::None)` ger en bild utan namnlist). Kopplingar och användarens `.cpp` körs aldrig. De icke-visuella medlemmarna (`glaze::Timer`, `glaze::TrayIcon`) skapas och får sina värden men startas och visas aldrig — de syns inte i bilden.

### Fallgropar
- `assets.bin` och `language.bin` letas uppåt från `.h`-katalogen (fyra nivåer) och laddas före fönstret; saknas de blir ikonerna tomma och appens texter `⟦id⟧`.
- Designradens `ui=`/`theme=`/`style=`/`lang=` styr bilden; ljust eller mörkt följer betraktarens OS.
- Egna widgettyper ritas som stand-in (ruta + klassnamn) av ett bygge som inte länkar dem — det buntade; ett eget bygge med `-DGLAZE_TOOLS=ON` och appens widgets ritar dem (`custom-widget`).
- Opaka rader ritas inte — läs stderr: `glaze-render(<id>): opakt metodanrop '<namn>' (utförs inte)`.
- Det som byggs i `onLoad` (listor och menyer i kod) syns inte; bara det som står i filen.
- Kräver en skärmsession: fungerar inte över SSH eller på headless-CI utan display.
<!-- END:glaze-render -->

<!-- BEGIN:glazebuilder | glazebuilder builder designer editor wysiwyg form-designer vscode vs-code extension preview stand-in palett fas-2 phase-2 -->
## GlazeBuilder och VS Code-extensionen (`glazebuilder`)

**Alias:** glazebuilder, builder, designer, editor, wysiwyg, form-designer, vscode, vs-code, extension, preview, stand-in, palett, fas-2, phase-2
**Include:** — (verktyg) · **CMake:** target `GlazeBuilder` (bara i GlazeUI-repot som toppprojekt) · **Källa:** [apps/GlazeBuilder/src/CLAUDE.md](apps/GlazeBuilder/src/CLAUDE.md), [tools/vscode-glazeui/README.md](tools/vscode-glazeui/README.md), [TODO.md](TODO.md)

GlazeBuilder är WYSIWYG-editorn för tre-fils-modellen, byggd med Glaze själv: den läser regionen och `.glazeui` med samma grammatik som glaze-render, ritar previewn med samma widgetkod som appen och skriver tillbaka vid Ctrl+S. VS Code-extensionen visar en `.glazeui` som en renderad bild (via en buntad glaze-render) och kan öppna filen i GlazeBuilder. Editorn är **fas 2** — ramverket och `.glazeui`-vägen (fas 1) ska fungera först.

### API
- `GlazeBuilder [fil.h|.cpp|.glazeui]` — öppnar tripletten (valfri fil av de tre); utan fil ett nytt fönster, vars filer skapas först vid spara (lägg sedan `glaze_add_window`-raden själv).
- Kan i dag: verktygslådan (klicka typ, klicka canvas), markera, dra, storleksgreppen, Delete, dubbelklick = koppla `<namn>Clicked`, egenskapspanelen för kända fält, kopplingsläget för syskonankare (Ctrl+L), Assets… och Ctrl+S.
- Saknas (fas 2, TODO.md): alla egenskaper i panelen, en kollektionseditor, placering i containrar, fönsteregenskaper i UI:t, att SÄTTA designraden, ångra/gör om.
- VS Code: tre lägen — Bild (förval), Text och Text + preview vid sidan — och "Öppna i GlazeBuilder". Inställningarna `glazeui.renderPath` och `glazeui.builderPath`; på Windows installeras den med `tools/vscode-glazeui/install-ext.bat`.

### .glazeui
Allt som går att skriva i filen kan GlazeBuilder öppna, visa och spara; att KLICKA fram det är editorns krav, inte ramverkets (hård regel 8). En agent redigerar därför `.glazeui` direkt i grammatikens form (`glazeui-grammar`) och verifierar med glaze-render.

### Fallgropar
- Spara skriver om regionen och HELA `.glazeui`; okända rader bevaras ordagrant, men blankrader och designradens plats normaliseras.
- Skadat stängsel eller en oläsbar sats ⇒ buildern vägrar redigera och visar diagnosen (fail-safe) — den gissar aldrig och raderar aldrig användarkod.
- Döper du om en widget i buildern byts medlemsnamnet och `name`-raden; referenser i din `.cpp` rättas INTE (kompileringsfel, som i C#).
- Egna widgettyper visas som stand-ins i den buntade editorn och i VS Code, som bara känner ramverkets typer; ett eget verktygsbygge (`-DGLAZE_TOOLS=ON` + appens widgets, `glazeui.builderPath`/`renderPath`) ritar dem.
- Extensionen fungerar inte i remote eller headless (SSH, Codespaces, WSL) — glaze-render behöver en skärmsession.
<!-- END:glazebuilder -->

<!-- BEGIN:version | version versionsnummer glaze_version glaze_version_string glaze_version_of semver git-tag version.md changelog ändringslogg brytande breaking-changes -->
## Versionen — `glaze/version.h` (`version`)

**Alias:** version, versionsnummer, glaze_version, glaze_version_string, glaze_version_of, semver, git-tag, version.md, changelog, ändringslogg, brytande, breaking-changes
**Include:** `#include <glaze/glaze.h>` (eller `<glaze/version.h>`) · **CMake:** `glaze` · **Källa:** [version.h](Glaze/widgets/include/glaze/version.h), [VERSION.md](VERSION.md)

Versionen står på ETT ställe, `glaze/version.h`, och rot-CMakeLists läser den till `project(GlazeUI VERSION …)`. Formen är semantisk `MAJOR.MINOR.PATCH`, varje version har git-taggen `vX.Y.Z`, och `VERSION.md` listar ändringarna mellan versionerna (Fix/New, en rad per sak). Före 1.0.0 är brytande ändringar gratis — namn och signaturer kan ändras mellan två versioner utan deprecation.

### API
- `GLAZE_VERSION_MAJOR` · `GLAZE_VERSION_MINOR` · `GLAZE_VERSION_PATCH` — heltalsmakron.
- `GLAZE_VERSION_STRING` — strängliteralen, t.ex. `"0.1.2"`.
- `GLAZE_VERSION_OF(major, minor, patch)` — ett jämförbart tal (0.1.0 → 100, 1.2.3 → 10203); `GLAZE_VERSION` är den aktuella versionens.

### Exempel
```cpp
#include <glaze/glaze.h>

#if GLAZE_VERSION < GLAZE_VERSION_OF(0, 1, 2)
#error "Appen kräver GlazeUI 0.1.2 eller senare (onCommit)"
#endif

void logVersion()
{
    gfx::log("GlazeUI %s (%d)", GLAZE_VERSION_STRING, GLAZE_VERSION);
}
```

### .glazeui
Ingen form — versionen är en kompileringstidsfråga i appens kod.

### Fallgropar
- Pinna en tagg (`GIT_TAG v0.1.2` — `git tag` visar vilka som finns) i stället för `main` när appen ska byggas reproducerbart; `main` kan bryta API:t när som helst före 1.0.
- Läs `VERSION.md` när du byter version: där står vad som ändrats.
- `GlazeUI_VERSION` (CMake) syns inte i konsumentens scope efter FetchContent — jämför i C++ med makrona.
- En app ändrar aldrig `version.h`; versionen sätts av ramverket.
- Ingen kontroll mellan ett program och dess plugins i delat läge (`shared-library`): att alla moduler byggs mot SAMMA GlazeUI är byggarens ansvar — en felmatchad libglaze upptäcks inte.
<!-- END:version -->
