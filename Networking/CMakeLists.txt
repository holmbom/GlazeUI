# Networking — SIDOPAKETET för nätverk.
#
# UTANFÖR ramverket: `glaze` länkar ALDRIG det här. Paketet beror på
# `glaze_threads` (Result, Worker) men aldrig på `glaze` — svar till
# UI-tråden går genom trådpaketets UI-brygga.
#
# PLATTFORMSDELAT som `gfx::`: en ren fasad (include/glaze_net/) och en
# implementation per OS, vald här i byggtid. TCP/UDP/DNS-logiken skrivs EN
# gång ovanpå ett tunt OS-lager (src/sys.h). HTTP(S) går genom OS:ETS EGEN
# stack — OS-native, uppfinn inget:
#
#   Windows  Winsock + IcmpSendEcho + WinHTTP   OS-komponenter, inga beroenden
#   macOS    BSD-sockets + NSURLSession         systemramverk (ObjC++, ARC)
#   Linux    BSD-sockets + libcurl              systemets libcurl via pkg-config
#
# Bygger också fristående: `cmake -S Networking -B b` drar in Threads själv.
cmake_minimum_required(VERSION 3.24)
project(GlazeNetworking LANGUAGES CXX)

# Delat eller statiskt (GLAZE_SHARED, cmake/GlazeShared.cmake) — samma växel
# som ramverket och Threads. FÖRE add_subdirectory(../Threads), så att
# variablerna ärvs nedåt när paketet byggs fristående.
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/GlazeShared.cmake)

if(NOT TARGET glaze_threads)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Threads
                     ${CMAKE_CURRENT_BINARY_DIR}/Threads)
endif()
# Fanns glaze_threads redan (inifrån GlazeUI-roten, eller i en konsument som
# lagt till Threads själv) måste den vara byggd i SAMMA läge: en delad
# glaze_net ovanpå en statisk glaze_threads hade gett trådpaketets
# processvida tillstånd en gång per modul — och blandningen godtas aldrig
# tyst.
get_target_property(_glaze_threads_typ glaze_threads TYPE)
if(GLAZE_SHARED AND NOT _glaze_threads_typ STREQUAL "SHARED_LIBRARY")
    message(FATAL_ERROR "Networking byggs delat (GLAZE_SHARED=ON), men "
        "glaze_threads är ${_glaze_threads_typ} — bygg Threads i samma läge "
        "(samma GLAZE_SHARED för hela GlazeUI).")
elseif(NOT GLAZE_SHARED AND NOT _glaze_threads_typ STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "Networking byggs statiskt (GLAZE_SHARED=OFF), men "
        "glaze_threads är ${_glaze_threads_typ} — bygg Threads i samma läge "
        "(samma GLAZE_SHARED för hela GlazeUI).")
endif()

add_library(glaze_net ${_glaze_lib_type}
    src/error.cpp       # ren std — felkategorin
    src/address.cpp     # ren std — text ↔ adress, likadant överallt
    src/dns.cpp         # ovanpå sys + detached.h
    src/tcp.cpp         # ovanpå sys
    src/udp.cpp         # ovanpå sys
    src/http.cpp        # validering, url, likriktningen mellan stackarna
)
if(GLAZE_SHARED)
    _glaze_shared_library(glaze_net glaze_net)   # libglaze_net.dll/.so/.dylib
endif()
target_include_directories(glaze_net PUBLIC include)
target_compile_features(glaze_net PUBLIC cxx_std_20)
target_link_libraries(glaze_net PUBLIC glaze_threads)
if(MSVC)
    target_compile_options(glaze_net PRIVATE /utf-8)
endif()

if(WIN32)
    target_sources(glaze_net PRIVATE
        src/sys_win.cpp
        src/ping_win.cpp
        src/http_winhttp.cpp
    )
    # ws2_32: Winsock · iphlpapi: IcmpSendEcho/GetIpErrorString ·
    # winhttp: HTTP(S). Alla tre är OS-komponenter i System32.
    target_link_libraries(glaze_net PRIVATE ws2_32 iphlpapi winhttp)
elseif(APPLE)
    # NSURLSession är Objective-C — en .mm med ARC, som gfx-backenden.
    # Standarden sätts på TARGETET: CMAKE_OBJCXX_STANDARD verkar bara på
    # target som skapas EFTER att den satts, och glaze_net finns redan.
    enable_language(OBJCXX)
    target_sources(glaze_net PRIVATE
        src/sys_posix.cpp
        src/ping_posix.cpp
        src/http_apple.mm
    )
    set_target_properties(glaze_net PROPERTIES
        OBJCXX_STANDARD 20
        OBJCXX_STANDARD_REQUIRED ON)
    set_source_files_properties(src/http_apple.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(glaze_net PRIVATE "-framework Foundation")
else()
    # libcurl är ett SYSTEMBIBLIOTEK här, som gfx-backendens Wayland/X11 —
    # ingen FetchContent. Saknas utvecklingspaketet är det ett riktigt fel
    # med paketnamnet i meddelandet, aldrig en tyst trasig build.
    find_package(PkgConfig QUIET)
    if(NOT PKG_CONFIG_FOUND)
        message(FATAL_ERROR "Networking kräver pkg-config på Linux "
            "(apt: pkg-config · dnf: pkgconf-pkg-config · pacman: pkgconf) — "
            "eller -DGLAZE_NETWORKING=OFF om sidopaketet inte behövs.")
    endif()
    pkg_check_modules(GLAZE_NET_CURL IMPORTED_TARGET libcurl)
    if(NOT GLAZE_NET_CURL_FOUND)
        message(FATAL_ERROR "Networking: libcurl saknas (HTTP(S) går genom "
            "systemets libcurl). Debian/Ubuntu: apt install libcurl4-openssl-dev · "
            "Fedora: dnf install libcurl-devel · Arch: pacman -S curl — "
            "eller -DGLAZE_NETWORKING=OFF om sidopaketet inte behövs.")
    endif()
    target_sources(glaze_net PRIVATE
        src/sys_posix.cpp
        src/ping_posix.cpp
        src/http_curl.cpp
    )
    target_link_libraries(glaze_net PRIVATE PkgConfig::GLAZE_NET_CURL)
endif()
