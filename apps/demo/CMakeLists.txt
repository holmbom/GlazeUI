# WIN32 = inget konsolfönster på Windows, MACOSX_BUNDLE = .app på Mac.
# Båda ignoreras på övriga plattformar. *.glazeui är den GENERERADE layouten
# (av GlazeBuilder) — committad C++ som kompileras via glaze_add_window
# (sätter LANGUAGE CXX). Bygget genererar ingenting.
set(DEMO_WINDOWS "")
glaze_add_window(DEMO_WINDOWS
    src/start_window      # → src/start_window.cpp + src/start_window.glazeui
    src/second_window     # → src/second_window.cpp + src/second_window.glazeui
    src/splash_window     # splashen, `demo --splash` — en Timer i .glazeui
    src/dock_window       # dockningen, `demo --dock` — flikar, staplar, handtag
    src/form_dialog       # formuläret, fönsterlabbets "Formulär…"
)
# HANDKODADE fönster — vanliga källfiler, ingen triplett, ingen region.
# Startfönstret visar `.glazeui`-vägen med dock/ankare/syskonankare, galleriet
# visar samma ramverk byggt helt i C++ (hård regel 8). Galleriets triplett
# finns kvar som `tests/fixtures/gallery.*` — den är roundtrip_gates bredaste
# referens (alla widgettyper).
add_executable(demo WIN32 MACOSX_BUNDLE
    src/main.cpp
    ${DEMO_WINDOWS}
    src/widget_gallery.cpp
    src/gallery_canvas.cpp    # galleriets flik "Rityta"
    src/gallery_3d.cpp        # galleriets flik "3D"
    src/gallery_3d_tools.cpp  # galleriets flik "3D-verktyg"
    src/about_dialog.cpp
    src/free_window.cpp
    src/tray_window.cpp       # systemfältsappen, `demo --tray`
    src/window_lab.cpp        # fönsterlabbet, `demo --window-lab`
)
target_link_libraries(demo PRIVATE glaze)
# TERMINALEN — dockningsfönstrets flik "Terminal". Länkad
# DIREKT (regel 5 i rot-CLAUDE.md: ankaret är en INTERFACE-källa). Demon visar
# vad ramverket kan och kräver därför sidopaketet; utan det stoppar
# konfigureringen med skälet i stället för ett kompileringsfel långt senare.
if(NOT TARGET glaze_terminal_ui)
    message(FATAL_ERROR "demo: dockningsfönstret har en terminal och kräver "
        "sidopaketet Terminal — utelämna -DGLAZE_TERMINAL=OFF.")
endif()
target_link_libraries(demo PRIVATE glaze_terminal_ui)

# PROGRAMFILENS IKON: utan bilder ⇒ GlazeUI-ikonen
# (Glaze/assets/icon/*) — exe:ns ikonresurs på Windows, bundlens .icns på
# macOS. `demo.desktop` är appens EGEN, committade skrivbordsfil (Linux:
# GNOME visar ingen ikon för ett app_id utan en installerad .desktop);
# den och hicolor-PNG:erna installeras med `cmake --install`. Filnamnet
# är app_id:t — programfilens namn, `demo`.
glaze_app_icon(demo DESKTOP demo.desktop)

# GlazeUI:s DLL:er bredvid exe:n när GlazeUI byggs DELAT (GLAZE_SHARED,
# cmake/GlazeShared.cmake) — en no-op i statiskt läge och utanför Windows.
glaze_runtime_dlls(demo)

# assets.bin (committad — filen ÄR källan)
# kopieras till körplatsen: bredvid exe:n (Windows/Linux) resp. in i
# bundlens Resources/ (Mac — basePath() är …/Contents/MacOS/, loadAssets
# provar ../Resources/). FILKOPIA, inte kodgenerering — bygget genererar
# ingenting (hård regel 7); GlazeBuilder skriver assets.bin vid sparning.
if(APPLE)
    set(DEMO_ASSETS_DST "$<TARGET_BUNDLE_CONTENT_DIR:demo>/Resources")
else()
    set(DEMO_ASSETS_DST "$<TARGET_FILE_DIR:demo>")
endif()
add_custom_command(TARGET demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEMO_ASSETS_DST}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets.bin" "${DEMO_ASSETS_DST}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/language.bin" "${DEMO_ASSETS_DST}"
    COMMENT "Kopierar assets.bin och language.bin till körplatsen")
