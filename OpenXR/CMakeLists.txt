# OpenXR — SIDOPAKETET som visar en glaze::Canvas-scen i ett PC-VR-headset.
#
# UTANFÖR ramverket: `glaze` länkar ALDRIG det här — OpenXR och en XR-tråd
# är inte GUI, och en app som inte behöver VR får det inte med sig. Två
# target, Threads-formen:
#
#   glaze_openxr     KÄRNAN — matematiken: OpenXR:s spårningsrum → Glaze-
#                    världen, förankringen och kameravinklarna.
#                    Ren std. Bygger fristående: `cmake -S OpenXR -B b`, och
#                    tests/openxr_math_gate länkar bara den.
#   glaze_openxr_ui  SESSIONEN `glaze::openxr::Session` — loadern, OpenXR-
#                    sessionen, D3D11-enheten, kontrollerna och bryggan mot
#                    Canvas. Det enda som länkar `glaze`, och finns därför
#                    bara när `glaze` gör det (byggt inifrån GlazeUI-roten).
#                    Bara Windows (OpenXR:s D3D11-väg).
#
# OpenXR-loadern hämtas med FetchContent (cmake/OpenXRLoader.cmake) först när
# sessionen byggs.
cmake_minimum_required(VERSION 3.24)
project(GlazeOpenXR LANGUAGES CXX)

# Delat eller statiskt (GLAZE_SHARED, cmake/GlazeShared.cmake) — samma växel
# som ramverket och de andra sidopaketen. Inifrån GlazeUI-roten är filen redan
# inkluderad (DIRECTORY-vakten); fristående inkluderas den här.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/GlazeShared.cmake)

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

add_library(glaze_openxr ${_glaze_lib_type}
    src/math.cpp   # koordinaterna, förankringen, kameravinklarna, projektionen
)
if(GLAZE_SHARED)
    _glaze_shared_library(glaze_openxr glaze_openxr)   # libglaze_openxr.dll/.so/.dylib
endif()
target_include_directories(glaze_openxr PUBLIC include)
target_compile_features(glaze_openxr PUBLIC cxx_std_20)
# Källan är UTF-8 (svenska kommentarer) — fristående finns inte rotens flagga.
if(MSVC)
    target_compile_options(glaze_openxr PRIVATE /utf-8)
endif()

if(TARGET glaze AND WIN32)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/OpenXRLoader.cmake)
    find_package(Threads REQUIRED)   # std::thread — sessionens XR-tråd

    add_library(glaze_openxr_ui ${_glaze_lib_type}
        src/session.cpp          # Session, XR-tråden och bildloopen
        src/openxr_context.cpp   # loadern, instansen, sessionen, rummen, swapchains
        src/actions.cpp          # action set, profilerna, spakarna, avtryckaren
        src/canvas_bridge.cpp    # förankringen, plockningen och ögonens ritning
    )
    if(GLAZE_SHARED)
        _glaze_shared_library(glaze_openxr_ui glaze_openxr_ui)
    endif()
    target_include_directories(glaze_openxr_ui PRIVATE src)
    target_include_directories(glaze_openxr_ui SYSTEM PRIVATE ${GLAZE_OPENXR_INCLUDE_DIR})
    target_compile_definitions(glaze_openxr_ui PRIVATE
        XR_USE_PLATFORM_WIN32
        XR_USE_GRAPHICS_API_D3D11
        XR_NO_PROTOTYPES
        WIN32_LEAN_AND_MEAN
        NOMINMAX)
    target_link_libraries(glaze_openxr_ui
        PUBLIC  glaze_openxr glaze
        PRIVATE d3d11 dxgi Threads::Threads)
    if(MSVC)
        target_compile_options(glaze_openxr_ui PRIVATE /utf-8)
    endif()
endif()
