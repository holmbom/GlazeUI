# glazelanguage — CLI för language.bin (pack/list/verify/export/embed).
# KONSOLPROGRAM med flit: inget WIN32 (utskriften ska synas i terminalen —
# apparna är WIN32-subsystem och deras loggar är osynliga), inget
# MACOSX_BUNDLE (ett verktyg, ingen app). Länkar BARA glaze_language —
# aldrig glaze, som skulle dra in en render-backend i ett CLI.
add_executable(glazelanguage
    src/main.cpp
)
target_link_libraries(glazelanguage PRIVATE glaze_language)
