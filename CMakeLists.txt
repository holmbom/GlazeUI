cmake_minimum_required(VERSION 3.24)

# Länkraden UTAN upprepade statiska bibliotek där länkaren inte behöver dem.
# Utan CMP0156 upprepar CMake dem som för GNU ld — de direkta beroendena
# först, de transitiva efter (glaze_assets står både som GlazeBuilders eget
# och som glaze_impl:s) — och Apples länkare varnar för varje dubblett.
# Avdupliceringen gäller bara länkare med en egen modul i CMake (Apple, lld,
# mold, MSVC); GNU ld i MSYS2 och på Linux behåller upprepningen. CMP0179:
# den FÖRSTA förekomsten behålls, på alla plattformar.
if(POLICY CMP0156)
    cmake_policy(SET CMP0156 NEW)
endif()
if(POLICY CMP0179)
    cmake_policy(SET CMP0179 NEW)
endif()

# VERSIONEN står på ETT ställe — Glaze/widgets/include/glaze/version.h — och
# läses därifrån, så CMake och koden aldrig glider isär (en ändring i
# headern konfigurerar om). Går den inte att läsa är det ett riktigt fel,
# aldrig en tyst version 0.
set(_glaze_version_h "${CMAKE_CURRENT_LIST_DIR}/Glaze/widgets/include/glaze/version.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_glaze_version_h}")
file(STRINGS "${_glaze_version_h}" _glaze_version_lines
     REGEX "^#define GLAZE_VERSION_(MAJOR|MINOR|PATCH) ")
foreach(_part MAJOR MINOR PATCH)
    if(NOT _glaze_version_lines MATCHES "GLAZE_VERSION_${_part} ([0-9]+)")
        message(FATAL_ERROR "glaze/version.h saknar GLAZE_VERSION_${_part}")
    endif()
    set(_glaze_version_${_part} "${CMAKE_MATCH_1}")
endforeach()
project(GlazeUI
        VERSION ${_glaze_version_MAJOR}.${_glaze_version_MINOR}.${_glaze_version_PATCH}
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Källkoden är UTF-8 (åäö i strängliteraler) — GCC/Clang antar det redan,
# MSVC behöver flaggan.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Tre-fils-modellens CMake-helper (glaze_add_window) — synlig i app-
# subkatalogerna. Kompilerar *.glazeui (genererad layout) som C++.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/GlazeWindow.cmake)

# Programfilens ikon (glaze_app_icon) — exe:ns ikonresurs via windres på
# Windows, bundlens .icns på macOS, .desktop + hicolor-PNG:er vid install på
# Linux. Utan egna filer: GlazeUI-ikonen (Glaze/assets/icon/).
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/GlazeAppIcon.cmake)

# DELAT BIBLIOTEK (GLAZE_SHARED) — statiskt som förval; med
# -DGLAZE_SHARED=ON blir libglaze och sidopaketen DLL/.so/.dylib, EN instans
# per process. FÖRE Glaze/: biblioteksvalet läses när målen skapas. Och
# glaze_runtime_dlls(<target>), som lägger DLL:erna bredvid programmet.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/GlazeShared.cmake)

# Backend-val: auto = native D3D11 på Windows, native Metal på macOS,
# native Wayland/X11+EGL på Linux. Alla tre är beroendefria — bara
# OS-komponenter resp. systembibliotek varje Linux-skrivbord redan har.
set(GLAZE_BACKEND "auto" CACHE STRING
    "Render-backend: auto, d3d11, metal eller linux")
if(GLAZE_BACKEND STREQUAL "auto")
    if(WIN32)
        set(GLAZE_BACKEND_RESOLVED "d3d11")
    elseif(APPLE)
        set(GLAZE_BACKEND_RESOLVED "metal")
    else()
        set(GLAZE_BACKEND_RESOLVED "linux")
    endif()
else()
    set(GLAZE_BACKEND_RESOLVED "${GLAZE_BACKEND}")
endif()

# Validering HÄR (roten äger backendvalet — gfx-CMaken dispatchar bara på
# redan validerade värden): ett felstavat GLAZE_BACKEND får ALDRIG bli en
# tyst trasig build där inga backend-källor kompileras.
if(NOT GLAZE_BACKEND_RESOLVED MATCHES "^(d3d11|metal|linux)$")
    message(FATAL_ERROR
        "Okänd GLAZE_BACKEND '${GLAZE_BACKEND}'. Giltiga värden: auto, "
        "d3d11 (Windows), metal (macOS), linux (Linux) — utelämna "
        "GLAZE_BACKEND så väljer auto rätt.")
endif()

# Native Linux-backenden (Wayland/X11 + EGL/GL) är Linux-ENDAST —
# Windows/macOS har egna native backends (Windows=d3d11, macOS=metal).
if(GLAZE_BACKEND_RESOLVED STREQUAL "linux" AND (WIN32 OR APPLE))
    message(FATAL_ERROR
        "GLAZE_BACKEND=linux stöds bara på Linux. Windows använder d3d11, "
        "macOS använder metal — utelämna GLAZE_BACKEND (auto väljer rätt).")
endif()

message(STATUS "GlazeUI render-backend: ${GLAZE_BACKEND_RESOLVED}")
if(GLAZE_SHARED)
    message(STATUS "GlazeUI-biblioteken: delade (GLAZE_SHARED)")
else()
    message(STATUS "GlazeUI-biblioteken: statiska")
endif()

# Biblioteket — följer med när GlazeUI tas in via FetchContent. Ingen
# kodgenerering i byggtid: layouten är genererad C++ som committas och
# kompileras som vanlig källkod (GlazeBuilder genererar vid redigering).
add_subdirectory(Glaze)

# SIDOPAKETEN — UTANFÖR ramverket: `glaze` länkar dem
# aldrig, och en app tar in dem med egna target-namn (glaze_threads,
# glaze_threads_ui, glaze_net). Efter Glaze/, för UI-bryggan skapas bara när
# `glaze` redan finns. Paketkatalogerna bygger också fristående
# (`cmake -S Threads`, `cmake -S Networking`).
#
# Networking kräver systemets libcurl på Linux (HTTP(S) går genom OS:ets
# stack). En konsument som inte vill ha paketet stänger av det — utan
# libcurl-dev stoppar konfigureringen med paketnamnet, aldrig tyst.
option(GLAZE_NETWORKING "Bygg sidopaketet Networking (glaze_net)" ON)
add_subdirectory(Threads)
if(GLAZE_NETWORKING)
    add_subdirectory(Networking)
endif()
# Terminal — glaze_terminal (pty:n, VT-tolken, skärmen) och
# glaze_terminal_ui (widgeten `glaze::Terminal`). Inga externa beroenden på
# något OS. FÖRE apps/: glaze-render och GlazeBuilder länkar widgeten när
# targetet finns, så att previewn visar en riktig terminal.
option(GLAZE_TERMINAL "Bygg sidopaketet Terminal (glaze_terminal)" ON)
if(GLAZE_TERMINAL)
    add_subdirectory(Terminal)
endif()

# VERKTYGEN SOM BEROENDE: glaze-render och
# GlazeBuilder slår upp widgetarna i REGISTRET, så ett verktygsbygge som länkar
# appens egna widgets ritar dem på riktigt. `-DGLAZE_TOOLS=ON` hos en
# konsument bygger de fyra verktygen också när GlazeUI är ett beroende; appen
# länkar sina widgets in med `target_link_libraries(glaze-render PRIVATE
# <objektbibliotek>)` — ett OBJECT-bibliotek, för ur ett statiskt hade länkaren
# strukit registreringarna (MANUAL `cmake`).
option(GLAZE_TOOLS "Bygg glaze-render, GlazeBuilder, glazeassets och glazelanguage också som beroende" OFF)

# Apparna byggs bara när GlazeUI är toppprojektet, inte som beroende —
# verktygen också med GLAZE_TOOLS.
if(PROJECT_IS_TOP_LEVEL)
    add_subdirectory(apps/demo)
endif()
if(PROJECT_IS_TOP_LEVEL OR GLAZE_TOOLS)
    add_subdirectory(apps/GlazeBuilder)
    add_subdirectory(apps/glazeassets)   # CLI för assets.bin — bara glaze_assets
    add_subdirectory(apps/glazelanguage) # CLI för language.bin — bara glaze_language
    add_subdirectory(apps/glaze-render)  # CLI fönster-.h → PNG — glaze + builder_core
endif()
if(PROJECT_IS_TOP_LEVEL)
    # Grindarna (tests/) — bara i repot självt, aldrig hos den som
    # konsumerar GlazeUI, och bara när de BEGÄRS: de är ~55 program, och
    # utan valet fyller de Launch-listan och förlänger varje bygge. Testerna
    # körs bara före en commit (CLAUDE.md), i en egen byggkatalog:
    #   cmake -S . -B build-test -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGLAZE_TESTS=ON
    option(GLAZE_TESTS "Bygg grindarna i tests/ (ctest)" OFF)
    if(GLAZE_TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()
endif()
