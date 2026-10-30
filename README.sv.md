[English](README.md) · **Svenska**

# GlazeUI

Ett plattformsoberoende GUI-ramverk i C++20 med native rendering på varje
plattform: Direct3D 11 + DirectWrite på Windows, Metal + Core Text på macOS
och Wayland/X11 + EGL/GL + FreeType på Linux. GlazeUI ritar varenda pixel
självt (widgets, fönster-chrome och dialoger), så en app ser likadan ut på
alla tre plattformarna.

Förebilden är WinForms: en WYSIWYG-editor (GlazeBuilder), en medlemsfunktion
per event och en layout som är genererad C++, committas och kompileras som
vilken källfil som helst.

> GlazeUI är före 1.0. API:t kan fortfarande ändras mellan minor-versioner.

## Funktioner

- **Widgets:** Label, Button, Checkbox, Radio, TextInput, TextArea, SpinBox,
  Slider, Progress, Dropdown, ListBox, ListView, TreeView, DataGrid,
  PropertyGrid, TabControl, GroupBox, Splitter, ColorPicker, DatePicker,
  FilePicker, MarkdownView, Picture, Link, MenuBar, ToolBar, StatusBar med
  flera.
- **Dockning:** paneler med flikar, knappnål och flytande fönster
  (`DockHost`).
- **Egna dialoger:** meddelanderuta, filväljare, färgdialog och
  inmatningsruta, ritade av GlazeUI och aldrig av OS:et.
- **Rityta:** en 2D- och 3D-scen med kamera: former i lager på GPU:n, text
  och bilder, meshar, punktmoln, terräng, egna shaderprogram och plockning.
- **Utseende:** tre UI-familjer (Glaze, Slate, Minimalistic), var och en med
  egna teman, ljust och mörkt läge och storlekar.
- **Språk:** ramverkets texter finns inbyggda på svenska, engelska, norska
  och finska; en app lägger till sina egna i en `language.bin`.
- **App-funktioner:** ikon i systemfältet, appens timer, programikon,
  fönstrets plats och läge, och egna menyer.
- **Sidopaket** (valfria, utanför ramverket): `Threads` (en trådpool vars
  svar postas tillbaka till UI-tråden), `Networking` (TCP, UDP, DNS, ping
  och HTTP(S) genom OS:ets egen stack), `Terminal` (en terminal-widget i
  OS:ets pseudoterminal) och `OpenXR` (en rityta i ett PC-VR-headset; Windows,
  slås på vid behov).
- **Händelsestyrd rendering:** appen sover på ~0 % CPU tills något ändras,
  och bara den ändrade ytan ritas om.

## Ett fönster i tre filer

```
start_window.h        en klass; #pragma region GlazeBuilder tillhör
                      editorn, allt utanför den är ditt
start_window.glazeui  genererad C++: initializeComponent() med layouten,
                      egenskaperna och event-kopplingarna
start_window.cpp      din kod: en medlemsfunktion per event
```

```cpp
// start_window.glazeui (genererad)
void StartWindow::initializeComponent()
{
    setTitle("Start");
    setClientSize(800, 520);

    okBtn.setText("Tryck här");
    okBtn.anchors = {false, false, true, true};   // höger + botten
    okBtn.right = 12;
    okBtn.bottom = 12;
    okBtn.onClick = [this](glaze::Widget* s, glaze::ClickArgs& e) { okBtnClicked(s, e); };

    root().adopt(okBtn);
}

// start_window.cpp (din)
void StartWindow::okBtnClicked(glaze::Widget*, glaze::ClickArgs&)
{
    okBtn.setText("Klick!");
}

int main()
{
    StartWindow win;
    return win.run();
}
```

Samma fönster går att skriva helt för hand. Designer och kod är likvärdiga
vägar, och allt designern skriver är kod du själv hade kunnat skriva.
Bygget genererar ingenting; GlazeBuilder genererar när den sparar, och den
rör aldrig kod utanför sin egen region.

## Använda GlazeUI i ditt projekt

```cmake
include(FetchContent)
FetchContent_Declare(GlazeUI
    GIT_REPOSITORY https://github.com/holmbom/GlazeUI.git
    GIT_TAG        main)   # eller en release-tagg, t.ex. vX.Y.Z
FetchContent_MakeAvailable(GlazeUI)

glaze_add_window(WINDOWS start_window)   # start_window.cpp + .glazeui
add_executable(myapp WIN32 MACOSX_BUNDLE main.cpp ${WINDOWS})
target_link_libraries(myapp PRIVATE glaze)
```

Bara biblioteket byggs i ditt projekt, inte apparna och testerna.
Sidopaketen länkas när du vill ha dem:

```cmake
target_link_libraries(myapp PRIVATE glaze glaze_threads_ui glaze_net glaze_terminal_ui)
```

Statisk länkning är förval. `set(GLAZE_SHARED ON)` före
`FetchContent_MakeAvailable` bygger ramverket som delat bibliotek, för en
app som laddar plugins som också skapar fönster.

## Bygga

Krav: CMake 3.24+, Ninja och en C++20-kompilator.

- **Windows:** MSYS2 UCRT64 (GCC). Inga externa bibliotek.
- **macOS:** Apple clang, med CMake och Ninja från Homebrew. Inga externa
  bibliotek.
- **Linux:** systembiblioteken som varje skrivbord redan har. På
  Debian/Ubuntu:
  `apt install pkg-config libwayland-dev libxkbcommon-dev libx11-dev libxrandr-dev libxi-dev libegl-dev libgl-dev libfontconfig-dev libfreetype-dev libpng-dev libdbus-1-dev`,
  plus `libcurl4-openssl-dev` för Networking (eller `-DGLAZE_NETWORKING=OFF`).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/apps/demo/demo              # startsidan, byggd i designern
./build/apps/demo/demo --gallery    # alla widgets, handskrivna
./build/apps/demo/demo --dock       # dockning
```

På Windows slutar programmen på `.exe`; på macOS är demon
`demo.app/Contents/MacOS/demo`. Testerna byggs med `-DGLAZE_TESTS=ON` och
körs med `ctest`.

## Verktyg

- **GlazeBuilder:** WYSIWYG-editorn, byggd med GlazeUI självt
  (`GlazeBuilder start_window.glazeui`).
- **glaze-render:** ritar ett fönster till PNG utan att köra appen, med samma
  backend som appen använder.
- **VS Code-extension** (`tools/vscode-glazeui`): visar en `.glazeui`-fil som
  en renderad bild.
- **glazeassets** och **glazelanguage:** kommandoradsverktyg för appens
  `assets.bin` (bilder och ikoner) och `language.bin` (översättningar).

## Dokumentation

- [MANUAL.md](MANUAL.md): referensen för varje paket och widget, ett avsnitt
  var, lätt att söka i med grep. Skriven för både människor och AI-verktyg.
- [UI_GUIDELINES.md](UI_GUIDELINES.md): avstånd och linjering när ett fönster
  läggs ut.
- [VERSION.md](VERSION.md): ändringarna mellan versionerna.

## Licens

[MIT](LICENSE) © 2026 Erik Holmbom

Material från tredje part (Wayland-protokollen och Unicode-data) står i
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
