// dll_gate_fixture — DEL A:s KONTROLLARKIV (tests/dll_gate.cpp). Ett
// statiskt bibliotek som aldrig länkas in någonstans: grinden läser bara dess
// objekt med objdump/readelf, som den läser ramverkets arkiv.
//
// Den ODR-använder exakt det del A ska hitta — räknare(), delad, perTråd,
// Medlem::antal, karta och ägd ur fixtures/dll_gate/dll_gate_bad.h — och det
// den ALDRIG ska hitta: konstanterna (`kOk`, `kKonst`, de konstanta lokala)
// och en VANLIG global definierad här. Biblioteket byggs med -fdata-sections
// (tests/CMakeLists.txt), så den vanliga globalen får en egen sektion med
// samma namnform som en COMDAT (`.data$<sym>` på PE, `.bss.<sym>` på ELF):
// utan COMDAT-kontrollen hade den räknats.

#include "dll_gate_bad.h"

namespace glaze::dll_gate_fixture {

int vanligGlobal = 0;

int använd()
{
    return räknare() + delad + perTråd + Medlem::antal +
           static_cast<int>(karta.size()) + *ägd + kOk[1] + kKonst +
           undantagnaLokala() + vanligGlobal;
}

const int* adressen() { return kOk; }

} // namespace glaze::dll_gate_fixture
