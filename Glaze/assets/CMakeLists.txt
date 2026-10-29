# glaze_assets — assets.bin-formatet (läsare/packare/verify/export).
# REN STD: inga target_link_libraries, ingen koppling till glaze/gfx.
# Konsumeras av glazeassets-CLI:t, GlazeBuilder och glaze_impl (PRIVATE).
# Headless-kompilerbar precis som cpp_reader/cpp_writer — det är därför
# formatet bor här och inte i widgets (dump/verify utan render-backend).
add_library(glaze_assets STATIC
    src/assets_file.cpp
    # Programikonen: skalaren och valregeln (verktyget OCH driften),
    # PNG-codecen och .ico/.icns-skrivaren (verktyget — byte-identiska filer
    # på alla OS). src/crc32.h är den delade interna CRC32:n.
    src/image.cpp
    src/png.cpp
    src/icon_file.cpp
)
target_include_directories(glaze_assets PUBLIC include)
