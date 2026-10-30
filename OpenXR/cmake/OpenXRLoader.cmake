# OpenXR-LOADERN — Khronos färdigbyggda Windows-loader, låst med SHA-256.
# Zipen ger headers (include/openxr) och x64/bin/openxr_loader.dll. DLL:en
# laddas i körtid av glaze_openxr_ui, så ingen import-lib länkas. Hämtas bara
# när GLAZE_OPENXR är på — GlazeUI:s vanliga bygge förblir beroendefritt.
include(FetchContent)

FetchContent_Declare(OpenXRLoader
    URL      https://github.com/KhronosGroup/OpenXR-SDK-Source/releases/download/release-1.1.65/openxr_loader_windows-1.1.65.zip
    URL_HASH SHA256=859d7de35b7a968112db770b09ed8bddeed3cb16fd8ae070643a857676bea6fd
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(OpenXRLoader)

set(GLAZE_OPENXR_INCLUDE_DIR "${openxrloader_SOURCE_DIR}/include"
    CACHE INTERNAL "OpenXR-headers")
set(GLAZE_OPENXR_LOADER_DLL "${openxrloader_SOURCE_DIR}/x64/bin/openxr_loader.dll"
    CACHE INTERNAL "OpenXR-loaderns DLL")

if(NOT EXISTS "${GLAZE_OPENXR_INCLUDE_DIR}/openxr/openxr.h"
   OR NOT EXISTS "${GLAZE_OPENXR_LOADER_DLL}")
    message(FATAL_ERROR "OpenXR: zipen saknar include/openxr eller x64/bin/openxr_loader.dll.")
endif()

# glaze_openxr_runtime(<target>) — loaderns DLL bredvid programmet vid varje
# bygge (glaze_runtime_dlls form).
function(glaze_openxr_runtime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GLAZE_OPENXR_LOADER_DLL}" "$<TARGET_FILE_DIR:${target}>")
endfunction()
