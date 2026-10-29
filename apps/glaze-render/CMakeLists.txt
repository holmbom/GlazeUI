# glaze-render — fönster-triplett → PNG.
# KONSOLPROGRAM med flit: inget WIN32 (diagnoserna på stderr och exit-
# koderna ÄR gränssnittet), inget MACOSX_BUNDLE (ett verktyg, ingen app).
# Länkar glaze — det är POÄNGEN: samma inkompilerade native-backend som
# apparna kör med på maskinen ⇒ PNG:en är pixelidentisk per konstruktion
# (WYSIWYG-garantin). Aldrig glaze_impl direkt (WHOLE_ARCHIVE-regeln i
# root-CLAUDE.md — registreringarna strippas annars). builder_core läser
# tripletten; **builder_preview** bär det som beror på
# BÅDE glaze och builder_core: paletten, stand-in-widgeten, designtaggens
# avkodning och `applyProp`.
add_executable(glaze-render
    src/main.cpp
)
target_link_libraries(glaze-render PRIVATE glaze builder_core builder_preview)
# Terminalen DIREKT (regel 5): previewn visar en riktig `glaze::Terminal` och
# inte en stand-in, också i VSIX:ens buntade glaze-render (GLAZE_TERMINAL är ON).
if(TARGET glaze_terminal_ui)
    target_link_libraries(glaze-render PRIVATE glaze_terminal_ui)
endif()
# GlazeUI:s DLL:er bredvid exe:n i delat läge (GLAZE_SHARED) — annars en
# no-op. VSIX:en buntar alltid det statiska bygget (copy-bin.mjs vägrar ett
# delat build/).
glaze_runtime_dlls(glaze-render)
