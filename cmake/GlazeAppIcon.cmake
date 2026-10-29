# GlazeAppIcon.cmake — PROGRAMFILENS ikon.
#
#   glaze_app_icon(<target> [ICO <fil.ico>] [ICNS <fil.icns>]
#                  [PNG_DIR <katalog> PNG_NAME <bas>]
#                  [DESKTOP <fil.desktop>] [APP_ID <id>])
#
# Fönstrets ikon i drift sätts av ramverket (appIcon / gfx::Window::setIcon);
# det här är ikonen OS:et visar för PROGRAMFILEN — utforskaren, Finder,
# programmenyn — och den måste ligga i filen själv:
#
#   Windows  appens .ico som ikonresurs nr 1 i exe:n. En committad rc-fil
#            (cmake/glaze_app_icon.rc) kompileras med windres mot en kopia
#            av .ico:n i en katalog per target, och objektet läggs till
#            targetet. INTE enable_language(RC): språket är fil-scope i
#            rotprojektet och skulle läcka in i en FetchContent-konsument;
#            windres är en KOMPILERING av committad källa, inte en
#            kodgenerering.
#   macOS    appens .icns till bundlens Resources/ + MACOSX_BUNDLE_ICON_FILE
#            (CMakes Info.plist-mall läser den).
#   Linux    ingen ikon i filen — skrivbordet hittar den genom en .desktop.
#            Med DESKTOP installerar `cmake --install` appens committade
#            .desktop till share/applications och hicolor-PNG:erna
#            (<PNG_DIR>/<PNG_NAME>-<N>.png) till
#            share/icons/hicolor/<N>x<N>/apps/<APP_ID>.png. APP_ID (förval:
#            programfilens namn) måste vara samma som appens app_id i drift
#            (glaze::setAppId, förval också programfilens namn) och som
#            .desktop-filens Icon= och StartupWMClass=.
#
# Utan bilder gäller GlazeUI-ikonen (Glaze/assets/icon/glazeui.*) — samma
# förval som fönstret får i drift. Egna filer görs med
# `glazeassets icon <assets.bin> <lista>:<nyckel> [--small …] <katalog> <bas>`
# och committas; bygget genererar ingenting.
#
# Funktionen är UTTRYCKLIG: en app utan anropet får ingen ikon i
# programfilen, men fönstret har ändå sin.

# Katalogen med GlazeUI:s egna ikonfiler — CACHE INTERNAL och inte en vanlig
# variabel, för en FetchContent-konsument anropar funktionen ur ett annat
# katalog-scope än det som inkluderade filen.
set(GLAZE_APP_ICON_DEFAULT_DIR "${CMAKE_CURRENT_LIST_DIR}/../Glaze/assets/icon"
    CACHE INTERNAL "GlazeUI:s förvalda programikoner")
set(GLAZE_APP_ICON_RC "${CMAKE_CURRENT_LIST_DIR}/glaze_app_icon.rc"
    CACHE INTERNAL "Den committade ikonresursen (glaze_app_icon)")

function(glaze_app_icon target)
    cmake_parse_arguments(PARSE_ARGV 1 GAI "" "ICO;ICNS;PNG_DIR;PNG_NAME;DESKTOP;APP_ID" "")
    if(GAI_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "glaze_app_icon(${target}): okända argument: ${GAI_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR "glaze_app_icon: '${target}' är inget target")
    endif()
    get_target_property(_type ${target} TYPE)
    if(NOT _type STREQUAL "EXECUTABLE")
        message(FATAL_ERROR "glaze_app_icon(${target}): bara en programfil kan bära en ikon")
    endif()
    if((GAI_PNG_DIR AND NOT GAI_PNG_NAME) OR (GAI_PNG_NAME AND NOT GAI_PNG_DIR))
        message(FATAL_ERROR "glaze_app_icon(${target}): PNG_DIR och PNG_NAME anges tillsammans")
    endif()

    # Förvalen — GlazeUI-ikonen.
    if(NOT GAI_ICO)
        set(GAI_ICO "${GLAZE_APP_ICON_DEFAULT_DIR}/glazeui.ico")
    endif()
    if(NOT GAI_ICNS)
        set(GAI_ICNS "${GLAZE_APP_ICON_DEFAULT_DIR}/glazeui.icns")
    endif()
    if(NOT GAI_PNG_DIR)
        set(GAI_PNG_DIR "${GLAZE_APP_ICON_DEFAULT_DIR}")
        set(GAI_PNG_NAME "glazeui")
    endif()
    if(NOT GAI_APP_ID)
        get_target_property(_out ${target} OUTPUT_NAME)
        if(_out)
            set(GAI_APP_ID "${_out}")
        else()
            set(GAI_APP_ID "${target}")
        endif()
    endif()
    foreach(_v ICO ICNS PNG_DIR DESKTOP)
        if(GAI_${_v})
            get_filename_component(GAI_${_v} "${GAI_${_v}}" ABSOLUTE
                                   BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        endif()
    endforeach()

    if(WIN32)
        if(NOT EXISTS "${GAI_ICO}")
            message(FATAL_ERROR "glaze_app_icon(${target}): ${GAI_ICO} finns inte")
        endif()
        get_filename_component(_cxxdir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        find_program(GLAZE_WINDRES NAMES windres x86_64-w64-mingw32-windres
                     HINTS "${_cxxdir}")
        if(NOT GLAZE_WINDRES)
            message(FATAL_ERROR
                "glaze_app_icon(${target}): windres hittades inte — den kompilerar "
                "programfilens ikonresurs. I MSYS2 UCRT64: "
                "pacman -S mingw-w64-ucrt-x86_64-binutils")
        endif()
        # En katalog per target: rc-filen pekar på ett FAST namn, så varje
        # app får sin egen kopia att inkludera. configure_file följer
        # källan — en ny .ico konfigurerar om.
        set(_dir "${CMAKE_CURRENT_BINARY_DIR}/glaze_app_icon/${target}")
        configure_file("${GAI_ICO}" "${_dir}/glaze_app_icon.ico" COPYONLY)
        set(_obj "${_dir}/glaze_app_icon.o")
        add_custom_command(
            OUTPUT "${_obj}"
            COMMAND "${GLAZE_WINDRES}" --input-format=rc --output-format=coff
                    "--include-dir=${_dir}" -i "${GLAZE_APP_ICON_RC}" -o "${_obj}"
            DEPENDS "${GLAZE_APP_ICON_RC}" "${_dir}/glaze_app_icon.ico"
            COMMENT "Programikonen för ${target} (windres)"
            VERBATIM)
        set_source_files_properties("${_obj}" PROPERTIES
                                    EXTERNAL_OBJECT TRUE GENERATED TRUE)
        target_sources(${target} PRIVATE "${_obj}")
    elseif(APPLE)
        if(NOT EXISTS "${GAI_ICNS}")
            message(FATAL_ERROR "glaze_app_icon(${target}): ${GAI_ICNS} finns inte")
        endif()
        get_filename_component(_icns "${GAI_ICNS}" NAME)
        set_source_files_properties("${GAI_ICNS}" PROPERTIES
                                    MACOSX_PACKAGE_LOCATION Resources)
        target_sources(${target} PRIVATE "${GAI_ICNS}")
        set_target_properties(${target} PROPERTIES MACOSX_BUNDLE_ICON_FILE "${_icns}")
    elseif(GAI_DESKTOP)
        if(NOT EXISTS "${GAI_DESKTOP}")
            message(FATAL_ERROR "glaze_app_icon(${target}): ${GAI_DESKTOP} finns inte")
        endif()
        install(FILES "${GAI_DESKTOP}" DESTINATION share/applications)
        # hicolor-storlekarna (glzassets::kHicolorSizes) — de som finns:
        # `glazeassets icon` skalar aldrig upp förbi källan.
        set(_found FALSE)
        foreach(_n 16 22 24 32 48 64 128 256 512)
            set(_png "${GAI_PNG_DIR}/${GAI_PNG_NAME}-${_n}.png")
            if(EXISTS "${_png}")
                install(FILES "${_png}"
                        DESTINATION "share/icons/hicolor/${_n}x${_n}/apps"
                        RENAME "${GAI_APP_ID}.png")
                set(_found TRUE)
            endif()
        endforeach()
        if(NOT _found)
            message(FATAL_ERROR "glaze_app_icon(${target}): inga "
                    "${GAI_PNG_NAME}-<N>.png i ${GAI_PNG_DIR}")
        endif()
    endif()
endfunction()
