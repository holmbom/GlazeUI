// VERSION_GATE — glaze/version.h är versionens ENDA källa, och rot-CMakeLists
// läser talen därifrån till project(VERSION …). Grinden mäter att de två
// faktiskt är samma: de tre talen, den sammansatta strängen och det
// jämförbara talet. Headless, inget fönster.
//
// Sabotage som ska fällas: ett handskrivet tal i project() som inte följer
// headern, ett skrivfel i GLAZE_VERSION_STRING:s sammansättning och en
// felräknad GLAZE_VERSION_OF.

#include <glaze/glaze.h>

#include <cstdio>
#include <cstring>

namespace {

int fel = 0;
int ok  = 0;

void kolla(bool villkor, const char* vad)
{
    if (villkor) {
        ++ok;
    } else {
        ++fel;
        std::printf("FEL: %s\n", vad);
    }
}

} // namespace

int main()
{
    kolla(GLAZE_VERSION_MAJOR == GLAZE_CMAKE_MAJOR, "MAJOR = CMakes");
    kolla(GLAZE_VERSION_MINOR == GLAZE_CMAKE_MINOR, "MINOR = CMakes");
    kolla(GLAZE_VERSION_PATCH == GLAZE_CMAKE_PATCH, "PATCH = CMakes");
    kolla(std::strcmp(GLAZE_VERSION_STRING, GLAZE_CMAKE_VERSION) == 0,
          "GLAZE_VERSION_STRING = CMakes PROJECT_VERSION");
    kolla(GLAZE_VERSION == GLAZE_CMAKE_MAJOR * 10000 + GLAZE_CMAKE_MINOR * 100 +
                               GLAZE_CMAKE_PATCH,
          "GLAZE_VERSION är det jämförbara talet");
    kolla(GLAZE_VERSION_OF(1, 2, 3) == 10203, "GLAZE_VERSION_OF(1, 2, 3) = 10203");
    kolla(GLAZE_VERSION_OF(0, 10, 0) > GLAZE_VERSION_OF(0, 9, 99),
          "0.10.0 > 0.9.99");

    std::printf("version_gate: %s (CMake %s) — %d OK / %d FEL\n",
                GLAZE_VERSION_STRING, GLAZE_CMAKE_VERSION, ok, fel);
    return fel == 0 ? 0 : 1;
}
