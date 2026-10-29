#pragma once

// GLAZEUI:S VERSION — den ENDA källan. Rot-CMakeLists läser de tre talen
// härifrån till `project(GlazeUI VERSION …)`, så CMake och koden kan inte
// glida isär (och bygget genererar ingenting). Versionen höjs bara avsiktligt,
// och samma ändring får sina rader i /VERSION.md.
//
// Formen är semantisk: MAJOR.MINOR.PATCH. Före 1.0.0 är brytande ändringar
// gratis (root /CLAUDE.md), så MINOR och PATCH lovar inget om API:t än.
//
//     #if GLAZE_VERSION >= GLAZE_VERSION_OF(0, 2, 0)   // kompileringstid
//     gfx::log("GlazeUI %s", GLAZE_VERSION_STRING);    // "0.1.0"

#define GLAZE_VERSION_MAJOR 0
#define GLAZE_VERSION_MINOR 2
#define GLAZE_VERSION_PATCH 0

// Ett jämförbart tal: 0.1.0 → 100, 1.2.3 → 10203.
#define GLAZE_VERSION_OF(major, minor, patch) \
    ((major) * 10000 + (minor) * 100 + (patch))
#define GLAZE_VERSION \
    GLAZE_VERSION_OF(GLAZE_VERSION_MAJOR, GLAZE_VERSION_MINOR, GLAZE_VERSION_PATCH)

#define GLAZE_VERSION_STR_(x) #x
#define GLAZE_VERSION_STR(x)  GLAZE_VERSION_STR_(x)
#define GLAZE_VERSION_STRING                                            \
    GLAZE_VERSION_STR(GLAZE_VERSION_MAJOR) "." GLAZE_VERSION_STR(       \
        GLAZE_VERSION_MINOR) "." GLAZE_VERSION_STR(GLAZE_VERSION_PATCH)
