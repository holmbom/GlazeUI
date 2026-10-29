# Threads — SIDOPAKETET för trådar.
#
# UTANFÖR ramverket: `glaze` länkar ALDRIG det här — ett GUI-bibliotek som
# drar in en trådmodell blir svårare att konsumera. Två target:
#
#   glaze_threads     KÄRNAN — ren std + systemets trådbibliotek. Bygger
#                     fristående: `cmake -S Threads -B b` utan resten av
#                     GlazeUI, och tests/threads_gate länkar bara den.
#   glaze_threads_ui  UI-BRYGGAN — `glaze::app().post` som en Context. Det
#                     enda som länkar `glaze`, och finns därför bara när
#                     `glaze` gör det (byggt inifrån GlazeUI-roten).
#
# Ingen kodgenerering, inga externa beroenden på något OS.
cmake_minimum_required(VERSION 3.24)
project(GlazeThreads LANGUAGES CXX)

# Delat eller statiskt (GLAZE_SHARED, cmake/GlazeShared.cmake) — samma växel
# som ramverket: en delad glaze med statisk threads hade gett `defaultPool`
# och `defaultContext` per modul. Inifrån GlazeUI-roten är filen redan
# inkluderad (DIRECTORY-vakten); fristående inkluderas den här.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/GlazeShared.cmake)

find_package(Threads REQUIRED)

# STOPPTOKEN-PROVET. Paketet vilar på C++20:s P0660 (`std::stop_token`,
# `std::jthread`, avbrytbar `condition_variable_any::wait`) — det första i
# repot som gör det. libstdc++ och MSVC har haft den länge, men libc++ (Apple
# clang) släppte den från `-fexperimental-library` först i LLVM 20. Provet
# kompilerar en minimal användning; går det bara med flaggan läggs den på
# targetet (PUBLIC — headrarna använder typerna), och går det inte alls
# stoppar konfigureringen med ett begripligt fel i stället för ett
# obegripligt kompileringsfel långt senare.
include(CheckCXXSourceCompiles)
# try_compile följer CMAKE_CXX_STANDARD (CMP0067) — från roten är den 20,
# fristående sätts den här.
if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()
set(_glaze_stopp_prov [[
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <thread>
int main() {
    std::stop_source s;
    std::condition_variable_any cv;
    std::mutex m;
    std::unique_lock<std::mutex> l(m);
    cv.wait_for(l, s.get_token(), std::chrono::milliseconds(0), [] { return true; });
    std::jthread t([](std::stop_token) {});
    return 0;
}
]])
set(CMAKE_REQUIRED_LIBRARIES Threads::Threads)
check_cxx_source_compiles("${_glaze_stopp_prov}" GLAZE_THREADS_STOP_TOKEN)
set(_glaze_threads_flaggor "")
if(NOT GLAZE_THREADS_STOP_TOKEN)
    set(CMAKE_REQUIRED_FLAGS "-fexperimental-library")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fexperimental-library")
    check_cxx_source_compiles("${_glaze_stopp_prov}" GLAZE_THREADS_STOP_TOKEN_EXPERIMENTELL)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if(GLAZE_THREADS_STOP_TOKEN_EXPERIMENTELL)
        set(_glaze_threads_flaggor "-fexperimental-library")
    else()
        message(FATAL_ERROR "Threads: standardbiblioteket saknar C++20:s "
            "std::stop_token/std::jthread (P0660) — även med "
            "-fexperimental-library. libc++ har dem sedan LLVM 20; uppdatera "
            "Xcode respektive kompilatorn.")
    endif()
endif()
unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LIBRARIES)

add_library(glaze_threads ${_glaze_lib_type}
    src/result.cpp
    src/context.cpp
    src/pool.cpp
    src/worker.cpp
)
if(GLAZE_SHARED)
    _glaze_shared_library(glaze_threads glaze_threads)   # libglaze_threads.dll/.so/.dylib
endif()
target_include_directories(glaze_threads PUBLIC include)
target_compile_features(glaze_threads PUBLIC cxx_std_20)
target_link_libraries(glaze_threads PUBLIC Threads::Threads)
if(_glaze_threads_flaggor)
    target_compile_options(glaze_threads PUBLIC ${_glaze_threads_flaggor})
    target_link_options(glaze_threads PUBLIC ${_glaze_threads_flaggor})
endif()
# Källan är UTF-8 (svenska felbeskrivningar) — fristående finns inte rotens
# flagga, så den sätts här också.
if(MSVC)
    target_compile_options(glaze_threads PRIVATE /utf-8)
endif()

if(TARGET glaze)
    # Bryggans självregistrering är ett statiskt objekt i ui.cpp som ingen
    # app refererar — en vanlig statisk länkning stryker TU:n. Därför samma
    # mönster som libglaze: ett _impl-bibliotek och ett INTERFACE-target som
    # i STATISKT läge länkar det WHOLE_ARCHIVE. Apparna skriver bara
    # `target_link_libraries(app PRIVATE glaze_threads_ui)`.
    add_library(glaze_threads_ui_impl ${_glaze_lib_type} src/ui.cpp)
    target_link_libraries(glaze_threads_ui_impl PUBLIC glaze_threads glaze)
    if(MSVC)
        target_compile_options(glaze_threads_ui_impl PRIVATE /utf-8)
    endif()

    add_library(glaze_threads_ui INTERFACE)
    if(GLAZE_SHARED)
        # I DELAT läge refererar appen ingen symbol i libglaze_threads_ui:
        # PE-länkaren importerar bara en DLL vars symbol används, och GNU
        # ld:s --as-needed (förval på Ubuntu) gör likadant — DLL:en hade
        # aldrig laddats, självregistreringen aldrig körts och svaren tyst
        # hamnat i pooltråden. ANKARET är en INTERFACE-källa som
        # kompileras in i varje program och DLL som länkar bryggan och
        # refererar en exporterad no-op i den; det är referensen som håller
        # DLL:en kvar. Registreringen själv stannar i ui.cpp.
        _glaze_shared_library(glaze_threads_ui_impl glaze_threads_ui)
        target_link_libraries(glaze_threads_ui INTERFACE glaze_threads_ui_impl)
        target_sources(glaze_threads_ui INTERFACE
                       ${CMAKE_CURRENT_SOURCE_DIR}/src/ui_anchor.cpp)
    else()
        target_link_libraries(glaze_threads_ui INTERFACE
            $<LINK_LIBRARY:WHOLE_ARCHIVE,glaze_threads_ui_impl>)
    endif()
endif()
