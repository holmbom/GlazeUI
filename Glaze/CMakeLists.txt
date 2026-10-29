# Glaze — biblioteket. Det här (plus beroendena i rot-CMakeLists) är allt
# som följer med när GlazeUI tas in via FetchContent i ett annat projekt.
add_subdirectory(assets)   # assets.bin-formatet — ren std, inga beroenden
add_subdirectory(language) # language.bin-formatet — ren std, inga beroenden
add_subdirectory(gfx)      # maskinrummet: fönster, chrome, rendering, text
add_subdirectory(widgets)  # API:t apparna använder: glaze::AppWindow m.m.
