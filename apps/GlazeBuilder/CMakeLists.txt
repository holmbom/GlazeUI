# builder_core — den delbara builder-kärnan:
# WindowModel + cpp_reader/cpp_writer. Ren std, fönsterfri och headless-
# testbar (round-trip-/fail-safe-sviten). Länkas av GlazeBuilder OCH
# apps/glaze-render — ingen andra sanning om triplett-grammatiken.
add_library(builder_core STATIC
    src/window_model.cpp
    src/cpp_reader.cpp
    src/cpp_writer.cpp
)
target_include_directories(builder_core PUBLIC src)
target_compile_features(builder_core PUBLIC cxx_std_20)

# builder_preview — den tredje nivån: det som beror på BÅDE glaze och
# builder_core. Ett lib med en sanning, mätt av tests/preview_gate.cpp.
# `builder_core` förblir ren std och rörs inte.
#
# Länkar `glaze` (INTERFACE + WHOLE_ARCHIVE) och ALDRIG glaze_impl —
# widget-registreringarna strippas annars (rot-CLAUDE.md).
add_library(builder_preview STATIC
    src/apply_props.cpp
    src/palette.cpp
    src/standin_widget.cpp
    src/design_theme.cpp
    # preview_tree — previewvandringen, utbruten ur BuilderWindow så att den
    # går att grinda headless.
    src/preview_tree.cpp
    # window_tree — modellen → trädet i ett RIKTIGT AppWindow, delat av
    # glaze-render och tests/pixel_gate.cpp, så att grinden bygger exakt det
    # träd verktyget bygger och inte en kopia av det.
    src/window_tree.cpp
    # component_tree — de ICKE-VISUELLA medlemmarna (Timer, TrayIcon, egna
    # glaze::Component) skapade och utförda bredvid trädet, delad av
    # glaze-render och GlazeBuilder.
    src/component_tree.cpp
)
target_include_directories(builder_preview PUBLIC src)
target_link_libraries(builder_preview PUBLIC glaze builder_core)
# TERMINALEN — sidopaketets widget utförs av
# previewn när paketet byggs: setter-fälten går `setProp`-vägen av sig själva
# (registret har typen), och de tre anropen med flera argument
# (`setEnvironmentVariable`, `unsetEnvironmentVariable`, `setSchemeColor`) och
# textstorleken har en egen källfil bakom definitionen. Utan paketet
# (`GLAZE_TERMINAL=OFF`) blir typen en stand-in, som en apps egen widget.
if(TARGET glaze_terminal_ui)
    target_sources(builder_preview PRIVATE src/apply_terminal.cpp)
    target_link_libraries(builder_preview PUBLIC glaze_terminal_ui)
    target_compile_definitions(builder_preview PUBLIC GLAZE_PREVIEW_TERMINAL)
endif()

# GlazeBuilder — WYSIWYG-editorn.
add_executable(GlazeBuilder WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/builder_window.cpp
    src/asset_panel.cpp
)
# glaze_assets: asset-läget läser/packar den redigerade appens
# assets.bin med samma kod som glazeassets-CLI:t — ingen andra sanning.
target_link_libraries(GlazeBuilder PRIVATE glaze glaze_assets builder_core
                                           builder_preview)
# Terminalen DIREKT till programmet (regel 5 i rot-CLAUDE.md): ankaret är en
# INTERFACE-källa, och genom det statiska builder_preview hade den kunnat
# strykas.
if(TARGET glaze_terminal_ui)
    target_link_libraries(GlazeBuilder PRIVATE glaze_terminal_ui)
endif()
# Programfilens ikon: GlazeUI-ikonen, som förvalet i
# fönstret — editorn är byggd med Glaze och bär ramverkets märke.
glaze_app_icon(GlazeBuilder)
# GlazeUI:s DLL:er bredvid exe:n i delat läge (GLAZE_SHARED) — annars en
# no-op. VSIX:en buntar alltid det statiska bygget (copy-bin.mjs vägrar ett
# delat build/).
glaze_runtime_dlls(GlazeBuilder)
