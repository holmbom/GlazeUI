# GlazeWindow.cmake — tre-fils-modellen (GlazeUI).
#
# glaze_add_window(<out_sources_var> <bas>...) lägger till varje fönsters två
# KOMPILERADE, committade källfiler i <out_sources_var>:
#   <bas>.cpp       användarens handler-kroppar
#   <bas>.glazeui   100 % genererad layout (ctor + initializeComponent) —
#                   ÄR C++, men med egen extension så GlazeBuilder kan
#                   associera tripletten och VSCode nästla ihop den.
# (<bas>.h kompileras inte direkt — den inkluderas av de två TU:erna.)
#
# .glazeui måste kompileras som C++; det sker via LANGUAGE-egenskapen så att
# app-författaren aldrig behöver tänka på extensionen. Bygget genererar
# ingenting — allt är committad källa (hård regel 7).
#
# Ex:  set(WINDOWS "")
#      glaze_add_window(WINDOWS start_window second_window)
#      add_executable(demo main.cpp ${WINDOWS} ...)
function(glaze_add_window out_var)
    set(_srcs ${${out_var}})
    foreach(base ${ARGN})
        set_source_files_properties("${base}.glazeui" PROPERTIES LANGUAGE CXX)
        list(APPEND _srcs "${base}.cpp" "${base}.glazeui")
    endforeach()
    set(${out_var} "${_srcs}" PARENT_SCOPE)
endfunction()
