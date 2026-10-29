# common_gfx — render-fasaden + EN inkompilerad backend (valet görs i
# rot-CMakeLists: GLAZE_BACKEND_RESOLVED). Apparna ser bara fasaden i
# include/gfx/ och märker inte vilken backend som ligger bakom.
#
# I DELAT LÄGE (GLAZE_SHARED, cmake/GlazeShared.cmake) är det ett OBJECT-
# bibliotek vars objekt hamnar i libglaze — glaze_impl länkar det direkt, och
# include-katalogerna, GLAZE_BACKEND_* och systemlänkarna (PRIVATE ⇒
# $<LINK_ONLY:…>) når libglaze som de når konsumenterna i statiskt läge.
# BARA glaze_impl får länka common_gfx i delat läge: ett mål som länkar det
# direkt får en EGEN uppsättning gfx-objekt — och därmed ett eget
# backendtillstånd (händelsekön, UI-trådens id, shadercachen) bredvid
# libglaze:s. Allt annat länkar `glaze`.
if(GLAZE_BACKEND_RESOLVED STREQUAL "d3d11")
    add_library(common_gfx ${_glaze_part_type}
        src/platform.cpp                      # backend-neutralt (ticksMs)
        src/font_catalog.cpp                  # backend-neutralt (fontkandidater)
        src/renderer.cpp                      # backend-neutralt (klipp-stacken)
        src/text_util.cpp                     # backend-neutralt (wrapText)
        src/mat4.cpp                          # backend-neutralt (scenens matris)
        src/camera.cpp                        # backend-neutralt (kameran, RTC)
        src/camera3d.cpp                      # backend-neutralt (3D-vyn, kartkameran som 3D)
        src/geom2d.cpp                        # backend-neutralt (linje- och ytbyggarna)
        src/earcut.cpp                        # backend-neutralt (ytornas triangulering)
        src/geom3d.cpp                        # backend-neutralt (3D-generatorerna, polygonen i ett plan)
        src/sdf.cpp                           # backend-neutralt (SDF-text, atlas, byggarna)
        src/display_util.cpp                  # backend-neutralt (skärmens mm)
        src/gpu_util.cpp                      # backend-neutralt (grafikkorten, Linux-valets regel)
        src/backend_d3d11/d3d11_platform.cpp
        src/backend_d3d11/d3d11_window.cpp
        src/backend_d3d11/d3d11_renderer.cpp
        src/backend_d3d11/d3d11_text.cpp
        src/backend_d3d11/d3d11_image.cpp
        src/backend_d3d11/d3d11_scene.cpp     # scenen: buffertar + Line-programmet
        src/backend_d3d11/d3d11_fill.cpp      # Fill-programmet (ytorna)
        src/backend_d3d11/d3d11_glyph.cpp     # Glyph-programmet (texten)
        src/backend_d3d11/d3d11_target.cpp    # render-målen
        src/backend_d3d11/d3d11_program.cpp   # egna program
        src/backend_d3d11/d3d11_touchpad.cpp  # styrplattan
        src/backend_d3d11/d3d11_tray.cpp      # systemfältet
        src/backend_d3d11/d3d11_timer.cpp     # appens timer
        src/backend_d3d11/d3d11_print.cpp     # utskriften
        src/backend_d3d11/d3d11_gpu.cpp       # grafikkorten
    )
    # Enbart OS-komponenter — inga externa beroenden. d3dcompiler binder
    # mot d3dcompiler_47.dll (OS-komponent i System32 frÃ¥n Windows 8.1).
    # advapi32: registry-läsningen i gfx::systemTheme(). windowscodecs +
    # ole32: WIC-bildavkodningen (loadImage). dwmapi:
    # DwmSetWindowAttribute (rundade hörn på Custom-chrome, Win11).
    # shell32: ShellExecuteW (gfx::openUrl) och SHGetKnownFolderPath
    # (gfx::knownPlaces), och systemfältets Shell_NotifyIcon. uuid:
    # FOLDERID_*-konstanterna, som knownfolders.h bara DEKLARERAR utan
    # INITGUID. CoTaskMemFree kommer ur ole32 ovan.
    # hid: HidP_* — styrplattans råa kontakter (vridningen), ur
    # hid.dll. Direct Manipulation (styrplattans drag och nypning) skapas
    # med CoCreateInstance ur ole32 och behöver ingen importlib.
    # shcore: GetDpiForMonitor — systemfältets ikon i
    # aktivitetsfältets DPI, som kan byta skärm och skala i drift, och
    # skärmarnas skala i gfx::screens().
    # comdlg32 + winspool: utskriftsdialogen PrintDlgEx och
    # skrivarens DEVMODE (GetDefaultPrinter, DocumentProperties).
    target_link_libraries(common_gfx PRIVATE
        d3d11 dxgi dxguid dwrite d3dcompiler user32 gdi32 advapi32
        windowscodecs ole32 dwmapi shell32 uuid hid shcore comdlg32 winspool)
    # Backend-markör i kompileringstid (spegel av metal-/linux-grenarna)
    target_compile_definitions(common_gfx PUBLIC GLAZE_BACKEND_D3D11)
elseif(GLAZE_BACKEND_RESOLVED STREQUAL "metal")
    # Native macOS-backend: Metal + Core Text + Image I/O — Objective-C++
    # (.mm, ARC), enbart systemramverk (spegel av d3d11-grenens
    # beroendefrihet). MSL kompileras i runtime (newLibraryWithSource) —
    # ingen .metallib-verktygskedja, bygget är bara en kompilering.
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    enable_language(OBJCXX)
    add_library(common_gfx ${_glaze_part_type}
        src/platform.cpp                      # backend-neutralt (ticksMs)
        src/font_catalog.cpp                  # backend-neutralt (fontkandidater)
        src/renderer.cpp                      # backend-neutralt (klipp-stacken)
        src/text_util.cpp                     # backend-neutralt (wrapText)
        src/mat4.cpp                          # backend-neutralt (scenens matris)
        src/camera.cpp                        # backend-neutralt (kameran, RTC)
        src/camera3d.cpp                      # backend-neutralt (3D-vyn, kartkameran som 3D)
        src/geom2d.cpp                        # backend-neutralt (linje- och ytbyggarna)
        src/earcut.cpp                        # backend-neutralt (ytornas triangulering)
        src/geom3d.cpp                        # backend-neutralt (3D-generatorerna, polygonen i ett plan)
        src/sdf.cpp                           # backend-neutralt (SDF-text, atlas, byggarna)
        src/display_util.cpp                  # backend-neutralt (skärmens mm)
        src/gpu_util.cpp                      # backend-neutralt (grafikkorten, Linux-valets regel)
        src/backend_metal/metal_platform.mm
        src/backend_metal/metal_window.mm
        src/backend_metal/metal_renderer.mm
        src/backend_metal/metal_text.mm
        src/backend_metal/metal_image.mm
        src/backend_metal/metal_scene.mm      # scenen: buffertar + Line-programmet
        src/backend_metal/metal_fill.mm       # Fill-programmet (ytorna)
        src/backend_metal/metal_glyph.mm      # Glyph-programmet (texten)
        src/backend_metal/metal_target.mm     # render-målen
        src/backend_metal/metal_program.mm    # egna program
        src/backend_metal/metal_tray.mm       # systemfältet
        src/backend_metal/metal_timer.mm      # appens timer
        src/backend_metal/metal_print.mm      # utskriften
        src/backend_metal/metal_gpu.mm        # grafikkorten
    )
    # ARC på .mm-filerna — ObjC-pekare i C++-medlemmar blir implicit
    # __strong (inga manuella retain/release; memcpy:a aldrig sådana objekt)
    set_source_files_properties(
        src/backend_metal/metal_platform.mm
        src/backend_metal/metal_window.mm
        src/backend_metal/metal_renderer.mm
        src/backend_metal/metal_text.mm
        src/backend_metal/metal_image.mm
        src/backend_metal/metal_scene.mm
        src/backend_metal/metal_fill.mm
        src/backend_metal/metal_glyph.mm
        src/backend_metal/metal_target.mm
        src/backend_metal/metal_program.mm
        src/backend_metal/metal_tray.mm
        src/backend_metal/metal_timer.mm
        src/backend_metal/metal_print.mm
        src/backend_metal/metal_gpu.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(common_gfx PRIVATE
        "-framework Metal" "-framework QuartzCore" "-framework AppKit"
        "-framework CoreText" "-framework CoreGraphics" "-framework ImageIO"
        "-framework CoreFoundation" "-framework Foundation"
        # Carbon: BARA tangentbordslayouten (UCKeyTranslate) — level-0-
        # kontraktet för gfx::Key.
        "-framework Carbon")
    target_compile_definitions(common_gfx PUBLIC GLAZE_BACKEND_METAL)
elseif(GLAZE_BACKEND_RESOLVED STREQUAL "linux")
    # Native Linux-backend: Wayland/X11 + EGL/GL + FreeType/fontconfig/
    # libpng/dbus — bara bibliotek varje Linux-skrivbord redan har (GTK/Qt
    # drar in dem). INGEN FetchContent: saknas paketen är det ett riktigt
    # fel (systemkomponenter, inte vendorbara beroenden).
    # protocols/ är wayland-scanner-utdata genererad EN gång och COMMITTAD
    # (hård regel: bygget genererar ingenting).
    find_package(PkgConfig QUIET)
    if(NOT PKG_CONFIG_FOUND)
        message(FATAL_ERROR "GLAZE_BACKEND=linux kräver pkg-config "
            "(apt: pkg-config · dnf: pkgconf-pkg-config · pacman: pkgconf)")
    endif()
    # xrandr: skärmens millimeter på X11 — utgången fönstret
    # står på och dess mått (Window::displayMetrics).
    # xi: XInput 2 på X11 — pekaren, styrplattans skrollvalutorer
    # och XI 2.4-nypningen. 1.8 är första libXi med gesterna.
    pkg_check_modules(GLAZE_LINUX_DEPS IMPORTED_TARGET
        wayland-client wayland-egl wayland-cursor xkbcommon x11 xrandr xi>=1.8
        egl gl fontconfig freetype2 libpng dbus-1)
    if(NOT GLAZE_LINUX_DEPS_FOUND)
        message(FATAL_ERROR "GLAZE_BACKEND=linux: systempaket saknas. "
            "Debian/Ubuntu: apt install libwayland-dev libxkbcommon-dev "
            "libx11-dev libxrandr-dev libxi-dev libegl-dev libgl-dev "
            "libfontconfig-dev libfreetype-dev libpng-dev libdbus-1-dev · "
            "Fedora: dnf install wayland-devel libxkbcommon-devel "
            "libX11-devel libXrandr-devel libXi-devel mesa-libEGL-devel "
            "mesa-libGL-devel fontconfig-devel freetype-devel libpng-devel "
            "dbus-devel · "
            "Arch: pacman -S wayland libxkbcommon libx11 libxrandr libxi mesa "
            "fontconfig freetype2 libpng dbus (libXi måste vara minst 1.8)")
    endif()
    add_library(common_gfx ${_glaze_part_type}
        src/platform.cpp                      # backend-neutralt (ticksMs)
        src/font_catalog.cpp                  # backend-neutralt (fontkandidater)
        src/renderer.cpp                      # backend-neutralt (klipp-stacken)
        src/text_util.cpp                     # backend-neutralt (wrapText)
        src/mat4.cpp                          # backend-neutralt (scenens matris)
        src/camera.cpp                        # backend-neutralt (kameran, RTC)
        src/camera3d.cpp                      # backend-neutralt (3D-vyn, kartkameran som 3D)
        src/geom2d.cpp                        # backend-neutralt (linje- och ytbyggarna)
        src/earcut.cpp                        # backend-neutralt (ytornas triangulering)
        src/geom3d.cpp                        # backend-neutralt (3D-generatorerna, polygonen i ett plan)
        src/sdf.cpp                           # backend-neutralt (SDF-text, atlas, byggarna)
        src/display_util.cpp                  # backend-neutralt (skärmens mm)
        src/gpu_util.cpp                      # backend-neutralt (grafikkorten, Linux-valets regel)
        src/backend_linux/linux_platform.cpp
        src/backend_linux/xkb_keys.cpp        # xkb → gfx::Key, DELAD av båda
                                              # drivrutinerna
        src/backend_linux/gl_common.cpp       # GL-loadern + det programmen delar
        src/backend_linux/linux_renderer.cpp
        src/backend_linux/linux_text.cpp
        src/backend_linux/linux_image.cpp
        src/backend_linux/linux_scene.cpp     # scenen: buffertar + Line-programmet
        src/backend_linux/linux_fill.cpp      # Fill-programmet (ytorna)
        src/backend_linux/linux_glyph.cpp     # Glyph-programmet (texten)
        src/backend_linux/linux_target.cpp    # render-målen
        src/backend_linux/linux_program.cpp   # egna program
        src/backend_linux/wayland_display.cpp
        src/backend_linux/wayland_window.cpp
        src/backend_linux/x11_display.cpp
        src/backend_linux/x11_window.cpp
        src/backend_linux/linux_tray.cpp      # systemfältet — kommenterad dummy
        src/backend_linux/linux_timer.cpp     # appens timer + timerfd-källan
        src/backend_linux/linux_print.cpp     # utskriften — läsbar degradering
        src/backend_linux/linux_gpu.cpp       # grafikkorten och valet i init
        src/backend_linux/protocols/xdg-shell-protocol.c
        src/backend_linux/protocols/viewporter-protocol.c
        src/backend_linux/protocols/xdg-decoration-unstable-v1-protocol.c
        src/backend_linux/protocols/fractional-scale-v1-protocol.c
        src/backend_linux/protocols/xdg-activation-v1-protocol.c
        src/backend_linux/protocols/pointer-gestures-unstable-v1-protocol.c
        src/backend_linux/protocols/xdg-toplevel-icon-v1-protocol.c   # programikonen
    )
    target_link_libraries(common_gfx PRIVATE PkgConfig::GLAZE_LINUX_DEPS)
    target_compile_definitions(common_gfx PUBLIC GLAZE_BACKEND_LINUX)
endif()
# Okänt GLAZE_BACKEND_RESOLVED kan inte nå hit — roten FATAL_ERROR:ar
# på allt utom d3d11/metal/linux innan Glaze/ läggs till.
# Backend-neutralt för ALLA tre: styrplattans vridning ur råa
# kontakter. D3D11 matar den med HID-rapporter; den kompileras överallt så
# att trackpad_gate mäter den headless på varje plattform.
target_sources(common_gfx PRIVATE src/touchpad_rotation.cpp)
# earcut UTAN FMA: orienteringstestet `turn(a, b, c)` = (b−a)×(c−a) är två
# produkter och en subtraktion. Avrundas båda produkterna ger tre punkter på
# en linje exakt 0 och samma punkter i omvänd ordning exakt motsatt tecken.
# Slår kompilatorn ihop en av dem med subtraktionen till FMA avrundas bara
# den andra: en linje ger ett litet tal ≠ 0, tecknet kan bero på ordningen,
# och klippningen fastnar (geom_gate:s slumpade ytor, observerat på macOS).
# Clang slår ihop inom ett uttryck (fp-contract=on) och GCC över satser
# (fast), så fort målet har FMA: arm64 alltid, x86-64 med -march.
if(NOT MSVC)
    set_source_files_properties(src/earcut.cpp PROPERTIES
                                COMPILE_OPTIONS "-ffp-contract=off")
endif()
target_include_directories(common_gfx PUBLIC include)
target_compile_features(common_gfx PUBLIC cxx_std_20)
