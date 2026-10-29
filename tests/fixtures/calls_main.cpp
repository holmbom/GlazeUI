// calls_fixture — KOMPILERINGSGRINDEN för metodanropssatsen. Programmet
// byggs av `cmake --build build` och behöver inte köras: dess enda uppgift
// är att bevisa att den `.glazeui` som roundtrip_gate läser och skriver
// också går att BYGGA.
//
// Reader och writer kan vara helt överens om en rad som kompilatorn avvisar
// (`statusBar.visible = true;` — `visible_` är privat). En grön
// round-trip är alltså inget bevis på att formatet är riktig C++.
//
// Fönstret VISAS aldrig — ingen pump, ingen `gfx::init`, inget
// OS-fönster. `main` returnerar direkt.

#include "calls.h"
#include "components.h"   // komponenterna, samma skäl
#include "order.h"   // satsordningens fixtur, samma skäl

int main()
{
    return 0;
}
