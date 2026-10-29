# GlazeShared.cmake — GlazeUI som DELAT BIBLIOTEK.
#
#   -DGLAZE_SHARED=ON              libglaze (ramverket + gfx) och sidopaketen
#                                  (glaze_threads, glaze_threads_ui, glaze_net,
#                                  glaze_terminal, glaze_terminal_ui)
#                                  som DLL/.so/.dylib — EN instans av ramverket
#                                  per process, delad av programmet och de
#                                  DLL:er det laddar i körtid
#   glaze_runtime_dlls(<target>)   GlazeUI:s DLL:er bredvid programmet
#                                  (Windows i delat läge; annars en no-op)
#
# STATISKT ÄR FÖRVALET: vardagsbygget, den ordinarie
# testsviten och de buntade verktygen (glaze-render och GlazeBuilder i
# VSIX:en) är statiska. Växeln är GLAZE_SHARED och ingenting annat —
# BUILD_SHARED_LIBS följs INTE: en konsument som vill ha sina egna
# bibliotek delade hade annars tyst fått GlazeUI delat.
#
# INGEN VERSIONSKONTROLL — det är upp till den som programmerar att skeppa
# rätt libglaze. Program och
# DLL:er byggs mot SAMMA GlazeUI (samma commit, kompilator och GLAZE_*-val),
# och en enda libglaze skeppas. Därför får biblioteken heller ingen
# VERSION/SOVERSION — sonamnet på Linux och `compatibility_version` i dyld
# vägrar båda en DLL med fel nummer, och det hade varit en versionskontroll
# i laddaren. Filnamnen är onumrerade.
#
# AUTO-EXPORT, INGA GLAZE_API-MAKRON. MinGW exporterar alla globala
# symboler när inget objekt bär `dllexport`; `--export-all-symbols` gör det
# uttryckligt, så att en framtida `dllexport` i någon header inte tyst stänger
# av det. Makron hade inte löst det som är farligt: statiska lokala i
# inline-funktioner, inline-variabler och mallars statiska medlemmar får en
# kopia PER MODUL oavsett exportsätt — det enda botemedlet är att de bor i en
# cpp (mätt av tests/dll_gate). På ELF och Mach-O gäller
# standardsynlighet; `-fvisibility=hidden` hade återinfört dupliceringen för
# allt omärkt och tagit bort de interna symboler grindarna når. Data
# importeras med GCC:s `.refptr`-pekare; det delade läget är provat med GCC i
# MSYS2 UCRT64 och inget annat (clang/lld, `-mcmodel=small`: oprövat). GNU
# ld:s ordinaltabell rymmer 65 535 exporter — når libglaze dit faller
# länkningen högljutt.
#
# Roten, Threads/, Networking/ och Terminal/ inkluderar filen: paketen bygger
# också fristående. DIRECTORY-vakten (inte GLOBAL) ger varje sådan katalog
# och allt under den variablerna — med GLOBAL hade två syskonkataloger inte
# fått dem båda.
include_guard(DIRECTORY)

option(GLAZE_SHARED
       "Bygg GlazeUI och sidopaketen som delade bibliotek (DLL/.so/.dylib)" OFF)

# MSVC har ingen auto-export, och MANUAL säger redan att MSVC är en oprövad
# väg: ett tydligt stopp är bättre än hundratals odefinierade referenser.
if(GLAZE_SHARED AND MSVC)
    message(FATAL_ERROR
        "GLAZE_SHARED vilar på MinGW:s auto-export (--export-all-symbols) och "
        "på ELF/Mach-O:s standardsynlighet — MSVC är ingen provad väg och har "
        "ingen auto-export. Bygg delat med GCC i MSYS2 UCRT64, eller statiskt "
        "(utelämna GLAZE_SHARED) med MSVC.")
endif()

if(GLAZE_SHARED)
    set(_glaze_lib_type  SHARED)   # glaze_impl och sidopaketens bibliotek
    set(_glaze_part_type OBJECT)   # common_gfx — objekten länkas DIREKT in i libglaze
    # common_gfx (OBJECT) och glaze_assets/glaze_language (statiska) länkas
    # in i ett delat bibliotek och måste därför vara positionsoberoende —
    # CMake sätter det bara själv på SHARED/MODULE. MinGW har ingen flagga
    # (PE flyttas med relokeringar); på ELF krävs -fPIC.
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
else()
    set(_glaze_lib_type  STATIC)
    set(_glaze_part_type STATIC)
endif()

# Internt — samma för alla delade GlazeUI-bibliotek. OUTPUT_NAME ger
# libglaze.dll i stället för libglaze_impl.dll (glaze_impl ÄR libglaze), och
# `--enable-auto-import` når konsumenterna: auto-import är redan förval i GNU
# ld, och flaggan tystar bara dess meddelanden, så kravet på 0 varningar håller.
function(_glaze_shared_library target output_name)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME ${output_name})
    if(MINGW)
        target_link_options(${target} PRIVATE   "LINKER:--export-all-symbols")
        target_link_options(${target} INTERFACE "LINKER:--enable-auto-import")
    endif()
endfunction()

# glaze_runtime_dlls(<target>) — GlazeUI:s DLL:er BREDVID PROGRAMMET.
# Windows hittar en DLL i programmets katalog, och en plugin i en annan
# katalog får samma modul: laddaren återanvänder en redan laddad modul med
# samma namn oavsett katalog, och en DLL:s beroenden slås upp på modulnamn.
# macOS och Linux behöver ingen kopia — CMakes byggträds-rpath
# (@rpath/libglaze.dylib, RUNPATH) pekar på biblioteken där de byggs.
#
# UTTRYCKLIG som `glaze_app_icon`: glöms anropet säger
# Windows högt till ("libglaze.dll was not found"), ingenting blir fel i
# tysthet. No-op i statiskt läge och utanför Windows. En FILKOPIA, som
# demons assets.bin — ingen kodgenerering (regel 7).
#
# KOPIAN KÖRS VID VARJE BYGGE, som ett eget mål
# programmet beror på — inte som POST_BUILD på programmet: en DLL som byggts
# om utan att programmet länkats om hade då aldrig kopierats. Kopieringsmålet
# måste i sin tur bero UTTRYCKLIGEN på DLL-målen: `$<TARGET_RUNTIME_DLLS>` ger
# inga målberoenden, och utan dem kör kopian före länkningen av DLL:en (första
# bygget faller på en fil som inte finns).
#
# BARA GLAZEUI:S DLL:ER, de av dem programmet faktiskt beror på
# (`$<TARGET_RUNTIME_DLLS>` snittat med GlazeUI-målen). Appens egna DLL:er är
# appens — de har inga beroenden här att ordnas efter.
#
# KOPIORNA KÖRS EN I TAGET: varje kopieringsmål väntar på det förra. Många
# program delar ofta en katalog (tests/, eller en konsuments bin/), och två
# `copy_if_different` som skriver samma DLL samtidigt faller på Windows.
function(glaze_runtime_dlls target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "glaze_runtime_dlls: '${target}' är inget target")
    endif()
    get_target_property(_type ${target} TYPE)
    if(NOT _type STREQUAL "EXECUTABLE")
        message(FATAL_ERROR
            "glaze_runtime_dlls(${target}): bara en programfil får DLL:erna "
            "bredvid sig")
    endif()
    if(NOT GLAZE_SHARED OR NOT WIN32)
        return()
    endif()

    set(_dlls "")
    set(_anyDll "")
    set(_libs "")
    foreach(_lib IN ITEMS glaze_impl glaze_threads glaze_threads_ui_impl glaze_net
                          glaze_terminal glaze_terminal_ui_impl)
        if(TARGET ${_lib})
            set(_in "$<IN_LIST:$<TARGET_FILE:${_lib}>,$<TARGET_RUNTIME_DLLS:${target}>>")
            list(APPEND _dlls "$<${_in}:$<TARGET_FILE:${_lib}>>")
            list(APPEND _anyDll "${_in}")
            list(APPEND _libs ${_lib})
        endif()
    endforeach()
    if(NOT _libs)
        return()
    endif()
    list(JOIN _anyDll "," _anyDll)

    add_custom_target(${target}_glaze_dlls ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target}>
        COMMAND ${CMAKE_COMMAND} -E
                $<IF:$<OR:${_anyDll}>,copy_if_different,true>
                ${_dlls} $<TARGET_FILE_DIR:${target}>
        COMMAND_EXPAND_LISTS
        COMMENT "Kopierar GlazeUI:s DLL:er bredvid ${target}")
    add_dependencies(${target}_glaze_dlls ${_libs})
    add_dependencies(${target} ${target}_glaze_dlls)

    get_property(_prev GLOBAL PROPERTY _GLAZE_LAST_DLL_COPY)
    if(_prev)
        add_dependencies(${target}_glaze_dlls ${_prev})
    endif()
    set_property(GLOBAL PROPERTY _GLAZE_LAST_DLL_COPY ${target}_glaze_dlls)
endfunction()
