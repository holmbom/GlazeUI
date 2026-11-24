# glaze-icons — glyfrecept (GLAZE_REGISTER_ICON i en fil) → ikonark som PNG.
# KONSOLPROGRAM: slutkoderna och stdout-raden är gränssnittet, loggen går till
# stderr. Länkar glaze, så arket ritas av samma motor och samma backend som
# apparna på maskinen. Ramverkets ark (detail::renderIconSheet) nås genom
# widgetarnas interna katalog, som tests/glyph_sheet gör.
add_executable(glaze-icons
    src/main.cpp
)
target_link_libraries(glaze-icons PRIVATE glaze)
target_include_directories(glaze-icons PRIVATE ${PROJECT_SOURCE_DIR}/Glaze/widgets/src)
# GlazeUI:s DLL:er bredvid exe:n i delat läge (GLAZE_SHARED) — annars en no-op.
glaze_runtime_dlls(glaze-icons)
