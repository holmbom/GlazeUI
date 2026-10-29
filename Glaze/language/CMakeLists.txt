# glaze_language — language.bin-formatet (läsare/packare/manifest/dump).
# REN STD: inga target_link_libraries, ingen koppling till glaze/gfx.
# Konsumeras av glazelanguage-CLI:t, glaze_impl (PRIVATE) och lang_gate.
# Headless-kompilerbar precis som glaze_assets — och den får ALDRIG
# länkas av builder_core, som förblir ren std utan ens den kopplingen.
add_library(glaze_language STATIC
    src/language_file.cpp
)
target_include_directories(glaze_language PUBLIC include)
