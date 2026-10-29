# glazeassets — CLI för assets.bin (pack/list/verify/extract).
# KONSOLPROGRAM med flit: inget WIN32 (utskriften ska synas i terminalen —
# apparna är WIN32-subsystem och deras loggar är osynliga), inget
# MACOSX_BUNDLE (ett verktyg, ingen app). Länkar BARA glaze_assets —
# aldrig glaze, som skulle dra in en render-backend i ett CLI.
add_executable(glazeassets
    src/main.cpp
)
target_link_libraries(glazeassets PRIVATE glaze_assets)
