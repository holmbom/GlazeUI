# width_table.cmake — TECKENBREDDENS TABELL ur Unicode-databasen.
# Körs FÖR HAND, en gång per Unicode-version — aldrig av
# bygget (ingen kodgenerering i byggtid, hård regel 7). Utdata committas som
# Terminal/src/text_width_table.h; UCD-filerna committas inte.
#
#   cmake -DUCD=<katalog> -DOUT=Terminal/src/text_width_table.h -P Terminal/tools/width_table.cmake
#
# <katalog> ska innehålla tre filer ur https://www.unicode.org/Public/<ver>/ucd/:
#   EastAsianWidth.txt                    W och F ger bredden 2
#   emoji/emoji-data.txt                  Emoji_Presentation ger 2,
#                                         Extended_Pictographic är tabellen VS16
#                                         (U+FE0F) breddar och ZWJ fogar ihop
#   extracted/DerivedGeneralCategory.txt  Mn, Me och Cf ger 0
# Dessutom U+1160–U+11FF (hangulens vokaler och slutkonsonanter, som fogas till
# stavelsen) som 0. Versionen läses ur EastAsianWidth.txt:s första rad.

cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED UCD OR NOT DEFINED OUT)
    message(FATAL_ERROR "width_table.cmake: -DUCD=<katalog> och -DOUT=<fil> krävs")
endif()
foreach(_f EastAsianWidth.txt emoji-data.txt DerivedGeneralCategory.txt)
    if(NOT EXISTS "${UCD}/${_f}")
        message(FATAL_ERROR "width_table.cmake: ${UCD}/${_f} saknas")
    endif()
endforeach()

# Ett intervall som sorterbar text: sex hexsiffror per gräns, "FFFFFF-FFFFFF".
function(_pad hex out)
    string(TOUPPER "${hex}" hex)
    string(LENGTH "${hex}" n)
    while(n LESS 6)
        string(PREPEND hex "0")
        math(EXPR n "${n} + 1")
    endwhile()
    set(${out} "${hex}" PARENT_SCOPE)
endfunction()

# Raderna i `fil` vars egenskap matchar `prop` (ett reguljärt uttryck) — som
# intervall i listan `out`.
function(_ranges fil prop out)
    file(STRINGS "${fil}" rader REGEX "^[0-9A-F]+(\\.\\.[0-9A-F]+)? *; *(${prop}) *(#|$)")
    set(lista "")
    foreach(rad IN LISTS rader)
        string(REGEX MATCH "^([0-9A-F]+)(\\.\\.([0-9A-F]+))?" _ "${rad}")
        set(a "${CMAKE_MATCH_1}")
        set(b "${CMAKE_MATCH_3}")
        if(b STREQUAL "")
            set(b "${a}")
        endif()
        _pad("${a}" a)
        _pad("${b}" b)
        list(APPEND lista "${a}-${b}")
    endforeach()
    set(${out} "${lista}" PARENT_SCOPE)
endfunction()

# Sortera och FOGA IHOP överlappande och angränsande intervall, och skriv dem
# som C++-rader.
function(_merge lista out)
    list(SORT lista)
    set(rader "")
    set(cur_a "")
    foreach(r IN LISTS lista)
        string(REPLACE "-" ";" ab "${r}")
        list(GET ab 0 a)
        list(GET ab 1 b)
        math(EXPR an "0x${a}")
        math(EXPR bn "0x${b}")
        if(cur_a STREQUAL "")
            set(cur_a ${an})
            set(cur_b ${bn})
        else()
            math(EXPR next "${cur_b} + 1")
            if(an LESS_EQUAL next)
                if(bn GREATER cur_b)
                    set(cur_b ${bn})
                endif()
            else()
                math(EXPR ha "${cur_a}" OUTPUT_FORMAT HEXADECIMAL)
                math(EXPR hb "${cur_b}" OUTPUT_FORMAT HEXADECIMAL)
                string(APPEND rader "    {${ha}, ${hb}},\n")
                set(cur_a ${an})
                set(cur_b ${bn})
            endif()
        endif()
    endforeach()
    if(NOT cur_a STREQUAL "")
        math(EXPR ha "${cur_a}" OUTPUT_FORMAT HEXADECIMAL)
        math(EXPR hb "${cur_b}" OUTPUT_FORMAT HEXADECIMAL)
        string(APPEND rader "    {${ha}, ${hb}},\n")
    endif()
    set(${out} "${rader}" PARENT_SCOPE)
endfunction()

file(STRINGS "${UCD}/EastAsianWidth.txt" _forsta LIMIT_COUNT 1)
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" _version "${_forsta}")

_ranges("${UCD}/EastAsianWidth.txt" "W|F" _wide)
_ranges("${UCD}/emoji-data.txt" "Emoji_Presentation" _emoji)
_ranges("${UCD}/emoji-data.txt" "Extended_Pictographic" _pict)
_ranges("${UCD}/DerivedGeneralCategory.txt" "Mn|Me|Cf" _zero)
list(APPEND _wide ${_emoji})
list(APPEND _zero "001160-0011FF")

_merge("${_wide}" _wide_rader)
_merge("${_zero}" _zero_rader)
_merge("${_pict}" _pict_rader)

file(WRITE "${OUT}" "#pragma once
// GENERERAD av Terminal/tools/width_table.cmake ur Unicode ${_version} — rör
// inte för hand, kör skriptet igen (filhuvudet där säger hur). INTERN: bara
// text_width.cpp inkluderar den. Intervallen är sorterade och åtskilda, så en
// binärsökning svarar.

namespace glaze::term::detail {

struct WidthRange {
    char32_t first;
    char32_t last;
};

// East Asian Width W/F och Emoji_Presentation — bredden 2.
inline constexpr WidthRange kWide[] = {
${_wide_rader}};

// Mn, Me, Cf och U+1160–U+11FF — bredden 0 (fogas till klustret före).
inline constexpr WidthRange kZero[] = {
${_zero_rader}};

// Extended_Pictographic — basen U+FE0F breddar och ZWJ fogar ihop.
inline constexpr WidthRange kPictographic[] = {
${_pict_rader}};

} // namespace glaze::term::detail
")
message(STATUS "width_table: ${OUT} skriven ur Unicode ${_version}")
