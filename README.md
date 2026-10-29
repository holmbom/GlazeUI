**English** · [Svenska](README.sv.md)

# GlazeUI

A cross-platform GUI framework for C++20 with native rendering on every
platform: Direct3D 11 + DirectWrite on Windows, Metal + Core Text on macOS,
and Wayland/X11 + EGL/GL + FreeType on Linux. GlazeUI draws every pixel
itself (widgets, window chrome and dialogs), so an app looks the same on all
three platforms.

The model is WinForms: a WYSIWYG editor (GlazeBuilder), one member function
per event, and a layout that is generated C++, committed and compiled like
any other source file.

> GlazeUI is before 1.0. The API can still change between minor versions.

## Features

- **Widgets:** Label, Button, Checkbox, Radio, TextInput, TextArea, SpinBox,
  Slider, Progress, Dropdown, ListBox, ListView, TreeView, DataGrid,
  PropertyGrid, TabControl, GroupBox, Splitter, ColorPicker, DatePicker,
  FilePicker, MarkdownView, Picture, Link, MenuBar, ToolBar, StatusBar and
  more.
- **Docking:** panels with tabs, pinning and floating windows (`DockHost`).
- **Our own dialogs:** message box, file dialog, color dialog and input box,
  drawn by GlazeUI, never by the OS.
- **Canvas:** a 2D and 3D scene with a camera: shapes in layers on the GPU,
  text and images, meshes, point clouds, terrain, custom shader programs and
  picking.
- **Look:** three UI families (Glaze, Slate, Minimalistic), each with its own
  themes, light and dark mode, and sizes.
- **Languages:** the framework's strings are built in for Swedish, English,
  Norwegian and Finnish; an app adds its own in a `language.bin`.
- **App features:** tray icon, app timer, program icon, window placement and
  state, and our own menus.
- **Side packages** (optional, outside the framework): `Threads` (a worker
  pool with results posted back to the UI thread), `Networking` (TCP, UDP,
  DNS, ping and HTTP(S) through the OS's own stack) and `Terminal` (a
  terminal widget on the OS's pseudoterminal).
- **Event-driven rendering:** the app sleeps at ~0 % CPU until something
  changes, and only the changed area is redrawn.

## A window in three files

```
start_window.h        one class; the #pragma region GlazeBuilder belongs to
                      the editor, everything outside it is yours
start_window.glazeui  generated C++: initializeComponent() with the layout,
                      properties and event bindings
start_window.cpp      your code: one member function per event
```

```cpp
// start_window.glazeui (generated)
void StartWindow::initializeComponent()
{
    setTitle("Start");
    setClientSize(800, 520);

    okBtn.setText("Press me");
    okBtn.anchors = {false, false, true, true};   // right + bottom
    okBtn.right = 12;
    okBtn.bottom = 12;
    okBtn.onClick = [this](glaze::Widget* s, glaze::ClickArgs& e) { okBtnClicked(s, e); };

    root().adopt(okBtn);
}

// start_window.cpp (yours)
void StartWindow::okBtnClicked(glaze::Widget*, glaze::ClickArgs&)
{
    okBtn.setText("Clicked!");
}

int main()
{
    StartWindow win;
    return win.run();
}
```

The same window can be written entirely by hand. Designer and code are
equal paths, and everything the designer writes is code you could have
written yourself. The build generates nothing; GlazeBuilder generates when
it saves, and it never touches code outside its own region.

## Using GlazeUI in your project

```cmake
include(FetchContent)
FetchContent_Declare(GlazeUI
    GIT_REPOSITORY https://github.com/holmbom/GlazeUI.git
    GIT_TAG        main)   # or a release tag, e.g. vX.Y.Z
FetchContent_MakeAvailable(GlazeUI)

glaze_add_window(WINDOWS start_window)   # start_window.cpp + .glazeui
add_executable(myapp WIN32 MACOSX_BUNDLE main.cpp ${WINDOWS})
target_link_libraries(myapp PRIVATE glaze)
```

Only the library is built in your project; the apps and tests are not. The
side packages are linked when you want them:

```cmake
target_link_libraries(myapp PRIVATE glaze glaze_threads_ui glaze_net glaze_terminal_ui)
```

Static linking is the default. `set(GLAZE_SHARED ON)` before
`FetchContent_MakeAvailable` builds the framework as a shared library, for an
app that loads plugins which also create windows.

## Building

Requirements: CMake 3.24+, Ninja and a C++20 compiler.

- **Windows:** MSYS2 UCRT64 (GCC). No external libraries.
- **macOS:** Apple clang, with CMake and Ninja from Homebrew. No external
  libraries.
- **Linux:** the system libraries every desktop already has. On
  Debian/Ubuntu:
  `apt install pkg-config libwayland-dev libxkbcommon-dev libx11-dev libxrandr-dev libxi-dev libegl-dev libgl-dev libfontconfig-dev libfreetype-dev libpng-dev libdbus-1-dev`,
  plus `libcurl4-openssl-dev` for Networking (or `-DGLAZE_NETWORKING=OFF`).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/apps/demo/demo              # the start page, built in the designer
./build/apps/demo/demo --gallery    # every widget, written by hand
./build/apps/demo/demo --dock       # docking
```

On Windows the programs end in `.exe`; on macOS the demo is
`demo.app/Contents/MacOS/demo`. The tests are built with `-DGLAZE_TESTS=ON`
and run with `ctest`.

## Tools

- **GlazeBuilder:** the WYSIWYG editor, built with GlazeUI itself
  (`GlazeBuilder start_window.glazeui`).
- **glaze-render:** renders a window to PNG without running the app, with
  the same backend the app uses.
- **VS Code extension** (`tools/vscode-glazeui`): shows a `.glazeui` file as
  a rendered image.
- **glazeassets** and **glazelanguage:** command-line tools for an app's
  `assets.bin` (images and icons) and `language.bin` (translations).

## Documentation

- [MANUAL.md](MANUAL.md): the reference for every package and widget, one
  section each, easy to search with grep. Written for both people and AI
  coding tools. Currently in Swedish.
- [UI_GUIDELINES.md](UI_GUIDELINES.md): spacing and alignment when laying out
  a window. Currently in Swedish.
- [VERSION.md](VERSION.md): the changes between versions.

## License

[MIT](LICENSE) © 2026 Erik Holmbom

Third-party material (the Wayland protocols and Unicode data) is listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
