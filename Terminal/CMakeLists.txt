# Terminal — SIDOPAKETET för en inbyggd terminal.
#
# UTANFÖR ramverket: `glaze` länkar ALDRIG det här — pty, processer och
# VT-tolkning är inte GUI, och en app som inte behöver terminalen får den inte
# med sig. Två target, Threads-formen:
#
#   glaze_terminal     KÄRNAN — pty-lagret (ConPTY / posix_openpt), VT-tolken,
#                      skärmmodellen, teckenbredden och sessionen med sina
#                      trådar. Ren std + OS (kernel32 på Windows, libc på
#                      POSIX). Bygger fristående: `cmake -S Terminal -B b`,
#                      och tests/terminal_gate länkar bara den.
#   glaze_terminal_ui  WIDGETEN `glaze::Terminal`, tangentkodningen och
#                      färgschemana. Det enda som länkar `glaze`, och finns
#                      därför bara när `glaze` gör det (byggt inifrån
#                      GlazeUI-roten).
#
# Ingen kodgenerering i byggtid: teckenbreddens tabell (src/text_width_table.h)
# är genererad EN gång för hand av tools/width_table.cmake och committad.
cmake_minimum_required(VERSION 3.24)
project(GlazeTerminal LANGUAGES CXX)

# Delat eller statiskt (GLAZE_SHARED, cmake/GlazeShared.cmake) — samma växel
# som ramverket och de andra sidopaketen. Inifrån GlazeUI-roten är filen redan
# inkluderad (DIRECTORY-vakten); fristående inkluderas den här.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/GlazeShared.cmake)

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

find_package(Threads REQUIRED)   # std::thread — sessionens tre trådar

add_library(glaze_terminal ${_glaze_lib_type}
    src/command_line.cpp   # kommandoradens regler (CommandLineToArgvW)
    src/text_width.cpp     # teckenbredden ur den committade tabellen
    src/parser.cpp         # Williams DEC ANSI-maskin
    src/screen.cpp         # skärmmodellen, historiken och svaren
    src/session.cpp        # trådarna, inkorgen och rivningsordningen
)
if(GLAZE_SHARED)
    _glaze_shared_library(glaze_terminal glaze_terminal)   # libglaze_terminal.dll/.so/.dylib
endif()
target_include_directories(glaze_terminal PUBLIC include)
target_compile_features(glaze_terminal PUBLIC cxx_std_20)
target_link_libraries(glaze_terminal PUBLIC Threads::Threads)
# Källan är UTF-8 (svenska feltexter) — fristående finns inte rotens flagga.
if(MSVC)
    target_compile_options(glaze_terminal PRIVATE /utf-8)
endif()

# PTY-LAGRET per OS (Networkings form: en intern header, src/pty_impl.h, och
# en implementation per OS). Bara OS-komponenter: kernel32 på Windows, libc
# på macOS och Linux (ingen forkpty, alltså ingen -lutil).
if(WIN32)
    target_sources(glaze_terminal PRIVATE src/pty_win.cpp)
else()
    target_sources(glaze_terminal PRIVATE src/pty_posix.cpp)
endif()

if(TARGET glaze)
    # Widgetens självregistrering (GLAZE_REGISTER_WIDGET) är ett statiskt
    # objekt i terminal.cpp som ingen app refererar — en vanlig statisk
    # länkning stryker TU:n. Därför glaze_threads_ui:s mönster ordagrant: ett
    # _impl-bibliotek och ett INTERFACE-target som i STATISKT läge länkar det
    # WHOLE_ARCHIVE. Apparna skriver bara
    # `target_link_libraries(app PRIVATE glaze_terminal_ui)`.
    add_library(glaze_terminal_ui_impl ${_glaze_lib_type}
        src/ui/terminal.cpp   # widgeten och registreringen
        src/ui/keys.cpp       # tangentkodningen (ren funktion)
        src/ui/scheme.cpp     # färgschemana och härledningen ur temat
    )
    target_link_libraries(glaze_terminal_ui_impl PUBLIC glaze_terminal glaze)
    if(MSVC)
        target_compile_options(glaze_terminal_ui_impl PRIVATE /utf-8)
    endif()

    add_library(glaze_terminal_ui INTERFACE)
    if(GLAZE_SHARED)
        # I DELAT läge refererar appen kanske ingen symbol i
        # libglaze_terminal_ui (en terminal skapad ur registret eller
        # previewn) — PE-länkaren och GNU ld:s --as-needed hade då strukit
        # importen, DLL:en aldrig laddats och registreringen aldrig körts.
        # ANKARET är en INTERFACE-källa som anropar en exporterad no-op i
        # DLL:en (Threads/src/ui_anchor.cpp-mönstret).
        _glaze_shared_library(glaze_terminal_ui_impl glaze_terminal_ui)
        target_link_libraries(glaze_terminal_ui INTERFACE glaze_terminal_ui_impl)
        target_sources(glaze_terminal_ui INTERFACE
                       ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/terminal_anchor.cpp)
    else()
        target_link_libraries(glaze_terminal_ui INTERFACE
            $<LINK_LIBRARY:WHOLE_ARCHIVE,glaze_terminal_ui_impl>)
    endif()
endif()
