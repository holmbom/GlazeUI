# Registreringens länk-fälla: de inbyggda
# widgetarna självregistrerar via statiska objekt i sina EGNA cpp:er
# (GLAZE_REGISTER_WIDGET). I en statisk lib stryker länkaren orefererade
# TU:er — en konsument som inte namnger typerna (headless-verktyg,
# GlazeBuilders palett) skulle tappa registreringarna. Därför länkas
# libglaze WHOLE_ARCHIVE i STATISKT läge, via INTERFACE-targetet "glaze"
# nedan: apparna fortsätter skriva target_link_libraries(app PRIVATE glaze)
# och märker inget.
#
# I DELAT läge (GLAZE_SHARED, cmake/GlazeShared.cmake) ÄR glaze_impl
# libglaze — libglaze.dll/.so/.dylib, med common_gfx:s objekt inlänkade. Ett
# delat bibliotek länkar alla sina egna objekt, så ingen registrering kan
# strykas och WHOLE_ARCHIVE behövs inte. `glaze` är INTERFACE i båda lägena.
add_library(glaze_impl ${_glaze_lib_type}
    src/app.cpp
    src/assets.cpp
    # RAMVERKETS BILDER — framework_assets.bin inbäddad som
    # bytes, GENERERAD av `glazeassets embed` och committad (som lang_builtin).
    src/assets_builtin.cpp
    src/image_cache.cpp
    # RITYTANS BILDKÄLLOR — pixlarna och deras
    # ändringslogg; fönstrets texturer bor i image_cache.cpp.
    src/canvas/bitmap.cpp
    src/canvas/render_texture.cpp
    # RITYTANS SCEN — formerna och vägarna, deras delar till
    # byggarna, lagren (körningar, chunkar, gallring), fönstrets GPU-resurser,
    # kameraräkningen och målaren. Widgeten är src/widgets/canvas.cpp.
    src/canvas/shape.cpp
    src/canvas/path.cpp
    src/canvas/build.cpp
    src/canvas/scene.cpp
    src/canvas/target3d.cpp
    src/canvas/resources.cpp
    src/canvas/camera_math.cpp
    # BINDNINGSTABELLEN — rader, specificitet, lägenas
    # reserv och vändningarna; ren std, grindad headless.
    src/canvas/input_map.cpp
    src/canvas/painter.cpp
    # ETIKETTPLACERINGEN — ren algoritm, grindad headless — och
    # dess koppling: lådorna på skärmen och synlighetsbilden.
    src/canvas/placement.cpp
    src/canvas/labels.cpp
    # TRÄFFPROVET — rutnätet per lager och provet per slag.
    src/canvas/hit_index.cpp
    # 3D-DATAN — meshen och punktmolnet ur data
    # (segmenten, ändringsloggen, nivåerna, chunkarna), plockningens BVH och
    # plockningens kärna. Ren CPU-kod, ingen renderer.
    src/canvas/mesh.cpp
    src/canvas/point_cloud.cpp
    src/canvas/bvh.cpp
    src/canvas/pick3d.cpp
    # DE INBYGGDA 3D-PROGRAMMEN — Mesh och Points i
    # HLSL, GLSL och MSL, varianterna och GlazeUser-blocken; kompilerade
    # genom fasadens egen väg (`createProgram`).
    src/canvas/programs3d.cpp
    # FÖREMÅLEN I RITYTAN — placeringens transform,
    # instansernas Morton-celler, residensen per fönster (segment, budget,
    # dubbelbuffring) och förberedelsen och ritningen.
    src/canvas/transform3d.cpp
    src/canvas/instances.cpp
    src/canvas/residency.cpp
    src/canvas/scene3d.cpp
    # TERRÄNGEN OCH DRAPERINGEN — höjden under en punkt,
    # draperingsmålen per terrängmesh, deras klass och minne.
    src/canvas/drape.cpp
    src/widget.cpp
    # INMATNINGENS VOKABULÄR — glaze/input.h.
    src/input.cpp
    src/draw.cpp
    src/style.cpp
    src/widgets/label.cpp
    src/widgets/button.cpp
    src/widgets/checkbox.cpp
    src/widgets/dropdown.cpp
    src/widgets/textinput.cpp
    src/widgets/text_area.cpp
    src/widgets/panel.cpp
    src/widgets/property_grid.cpp
    src/widgets/listbox.cpp
    src/widgets/scrollbar.cpp
    src/widgets/separator.cpp
    src/widgets/picture.cpp
    src/widgets/link.cpp
    src/widgets/radio.cpp
    src/widgets/slider.cpp
    src/widgets/progress.cpp
    src/widgets/bar.cpp
    src/widgets/spacer.cpp
    src/widgets/caption_button.cpp
    src/widgets/top_bar.cpp
    src/widgets/status_bar.cpp
    src/widgets/menu_bar.cpp
    src/widgets/app_icon.cpp
    src/widgets/tool_bar.cpp
    src/widgets/tool_button.cpp
    src/widgets/group_box.cpp
    src/widgets/spin_box.cpp
    # KOLUMNHUVUDET — den delade basen för Table och TreeView.
    src/widgets/column_panel.cpp
    src/widgets/tree_view.cpp
    src/widgets/table.cpp
    src/widgets/list_view.cpp
    src/widgets/data_grid.cpp
    src/widgets/splitter.cpp
    src/widgets/dock_panel.cpp
    src/widgets/dock_host.cpp
    # DOCKNINGENS MODELL, STRÄNG OCH FÖNSTER (intern — se
    # src/dock/): strukturen kant → remsa → cell → flik, geometrin ur den,
    # layoutsträngen v2 (parsern ren std; skrivaren och tillämpningen är
    # DockHosts medlemmar), och de flytande panelernas fönster och
    # konturdragets spöke. Panelrubrikens ritning (dock_header) är delad av
    # DockPanel och FloatingPanel.
    src/dock/dock_float.cpp
    src/dock/dock_header.cpp
    src/dock/dock_model.cpp
    src/dock/dock_string.cpp
    src/widgets/tab_page.cpp
    src/widgets/tab_control.cpp
    src/widgets/file_picker.cpp
    src/widgets/color_picker.cpp
    src/widgets/date_picker.cpp
    src/widgets/canvas.cpp
    src/widgets/floating_panel.cpp
    src/widgets/markdown_view.cpp
    # MARKDOWN-VYNS TOLKNING OCH LAYOUT (intern — se
    # src/markdown/): CommonMark till ett dokument (ren std, grindad headless)
    # och dokumentet lagt i en bredd — rader, delar, ytor och ankare.
    src/markdown/md_parse.cpp
    src/markdown/md_layout.cpp
    # SKIKTET — delarens mekanik, delad av Splitter och DockHost (intern,
    # se src/sash.h).
    src/sash.cpp
    # TEXTFÄLTET — ytan, markeringen, caretens bredd, UTF-8-gränserna och
    # filtren, delade av TextInput och TextArea (intern, se src/text_field.h).
    src/text_field.cpp
    # TEXTFONTERNA — fönstrets fonter för formaterad text, tre storlekar ×
    # två familjer × fyra stilar, och fältens fria textstorlekar (intern, se
    # src/font_set.h). Datumväljarens kalenderräkning
    # är header-only och ren std (src/date_model.h).
    src/font_set.cpp
    # FÄRGYTAN — paletten, reglagen och hexfältet, delade av färgväljarens
    # popup och färgdialogen (intern, se src/color_panel.h).
    src/color_panel.cpp
    src/app_window.cpp
    src/drag_drop.cpp
    src/tooltip.cpp
    src/language.cpp
    src/lang_builtin.cpp
    src/gui_loader.cpp
    src/message_box.cpp
    # INMATNINGSRUTAN och FÄRGDIALOGEN — modala
    # dialoger av meddelanderutans form; fönstren i de interna
    # src/input_box_window.h och src/color_dialog_window.h, för grindens skull.
    src/input_box.cpp
    src/color_dialog.cpp
    # FILVÄLJARDIALOGEN — fönstret och flödet, och den rena
    # std-modellen bredvid (katalogläsningen, filtren, ordningen,
    # sökvägarna), som grindas headless.
    src/file_dialog.cpp
    src/file_dialog_model.cpp
    src/menu.cpp
    # PROGRAMIKONENS RAMAR TILL OS-FÖNSTRET — ur förvalet eller
    # appens bild, en ram per ikonstorlek (intern, se src/window_icon.h).
    src/window_icon.cpp
    # STANDARDIKONERNA — ritbeskrivningarna och rastreringen bakom
    # `glaze::stockIcon` (vektorer i koden, inga bildfiler).
    src/stock_icon.cpp
    # SYSTEMFÄLTETS IKON — `TrayIcon`, registret, menyns värdfönster
    # och dispatchen från App; den rena modellen bredvid är header-only
    # (src/tray_model.h) och grindas headless.
    src/tray_icon.cpp
    # ICKE-VISUELLA KOMPONENTER — registret för det som
    # står som medlem i `.glazeui` utan att vara en widget, och appens timer
    # (registret, leveransen i pump-svansen — intern, se src/timer_registry.h).
    src/component.cpp
    src/timer.cpp
    # UTSKRIFTEN — `PrintJob`, förloppsfönstret och sidornas
    # rastrering med ritytans motor.
    src/print.cpp
    # PROFILERAREN — ringarna per fönster och pumpen,
    # etikettregistret och rapporten; pumpens och ritningens krokar i den
    # interna src/profiler_hooks.h.
    src/profiler.cpp
    src/theme.cpp
    # FÄRGHJÄLPARNA — hsl, mix, luminans, kontrast och
    # kulörvinklarna, publika i glaze/color.h.
    src/color.cpp
)
target_include_directories(glaze_impl PUBLIC include)
target_link_libraries(glaze_impl PUBLIC common_gfx)
# assets.bin-formatet — PRIVATE med flit: glzassets-typerna är en
# implementationsdetalj (bara assets.cpp rör dem), apparna ser bara
# glaze::AssetHandle/img/loadAssets ur glaze/assets.h.
target_link_libraries(glaze_impl PRIVATE glaze_assets)
# language.bin-formatet — PRIVATE av exakt samma skäl: glzlang-typerna är
# en implementationsdetalj (bara language.cpp rör dem), apparna ser bara
# glaze::tr/TrText/setLanguage ur glaze/language.h.
target_link_libraries(glaze_impl PRIVATE glaze_language)

add_library(glaze INTERFACE)
if(GLAZE_SHARED)
    _glaze_shared_library(glaze_impl glaze)            # libglaze.dll/.so/.dylib
    target_link_libraries(glaze INTERFACE glaze_impl)  # ingen arkivlänk ⇒ ingen WHOLE_ARCHIVE
    # PROGRAMFILENS ANKARE — kompileras in i varje mål som
    # länkar `glaze`: dess statiska objekt river App i programfilens exit(),
    # med trådarna vid liv, i stället för i libglaze:s DLL_PROCESS_DETACH.
    # Varför och hur står i filen.
    target_sources(glaze INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/app_anchor.cpp)
else()
    target_link_libraries(glaze INTERFACE
        $<LINK_LIBRARY:WHOLE_ARCHIVE,glaze_impl>)
endif()
