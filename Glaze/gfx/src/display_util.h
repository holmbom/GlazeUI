#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "gfx/window.h"

// SKÄRMENS FYSISKA UPPLÖSNING — det backend-neutrala. INTERN header: de sex
// `Window::displayMetrics`-implementationerna läser HÄR, så formeln, spannet
// och reserven är EN sanning — och tests/display_gate.cpp når filen och mäter
// den headless mot ett HANDSKRIVET svar (msgbox_gate-prejudikatet). Ren std,
// inga OS-typer.
//
// Metoden: täthet ur DIAGONALERNA, ett rimlighetsspann, EDID:s
// timing-deskriptor med en vakt mot monitor-deskriptorer, och RESERVEN: 96 dpi
// i FYSISKA px (fönstrets buffert), oberoende av OS- och användarskalan —
// aldrig 96 × skalfaktorn. En millimeter ändras då inte med skalningsvalet.
// Reserven antar alltså inte 96 dpi i LOGISKA px (96/25,4 × OS-skalan), som
// hade tagit OS-skalan som mått på panelens täthet när EDID saknas. Priset:
// på en tät panel utan EDID (en VM, RDP, WSLg) blir en antagen millimeter
// mindre än i verkligheten — "≈1:N" och kalibreringen är vägen ut.

namespace gfx::display {

// RIMLIGHETSSPANNET i dpi. Utanför det är måtten skräp — projektor, VM, en
// KVM-switch eller docka som skriver om EDID, eller ingen EDID alls. Wayland
// tillåter dessutom uttryckligen 0 när måttet saknar mening.
inline constexpr float kMinDpi    = 50.0f;
inline constexpr float kMaxDpi    = 800.0f;
inline constexpr float kMmPerInch = 25.4f;

// EDID:s bildstorlek och skärmens identitet. `widthMm`/`heightMm` 0 = inget
// mått; `id` är tillverkare-produkt-serie ("BOE-0B8B-00000000") — tom när
// blocket inte är EDID.
struct Edid {
    int         widthMm  = 0;
    int         heightMm = 0;
    std::string id;
};

// EDID-PARSERN. Måtten tas ur den FÖRSTA detaljerade TIMING-deskriptorn
// (millimeter, bytes 12–14 i blocket); basfälten 21/22 (hela centimeter) är
// reserven. VAKTEN: ett deskriptorblock med pixelklocka 0 är en MONITOR-
// deskriptor (namn, serienummer, gränser) och hoppas över — läst blint hade en
// skärm som lägger sitt namn först gett millimeter ur bokstäver. false när
// blocket är för kort eller saknar EDID-huvudet; `ut` orörd då.
bool parseEdid(const std::uint8_t* edid, std::size_t len, Edid& ut);

// Skärmens identitet ur EDID:s tillverkarkod (3 bokstäver à 5 bitar, stor
// endian), produktkod och serienummer — samma form på varje plattform, så en
// kalibrering följer skärmen. macOS ger talen ur CoreGraphics i stället.
std::string edidId(std::uint16_t manufacturer, std::uint16_t product,
                   std::uint32_t serial);

// DIAGONALFORMELN: fysiska px per mm ur skärmens pixlar och millimeter. EDID
// lagrar ofta hela centimeter, och felen per axel pekar åt olika håll och tar
// till stor del ut varandra i diagonalen (en mätning: 302,9 dpi mot
// 301,9 verkligt; bredden ensam gav 306,0). 0 = något mått saknas eller
// svaret ligger utanför spannet.
float pxPerMmFromDiagonal(int pxW, int pxH, double mmW, double mmH);

// APPLES 72-DPI-UPPSKATTNING. `CGDisplayScreenSize` svarar, när skärmen saknar
// EDID, med en storlek RÄKNAD ur 72 dpi (2,835 px/mm) — ett tal som ser mätt ut
// men inte är det. Sant när måtten är just den uppskattningen för pixlarna
// (inom en millimeter per axel, avrundningens marginal).
bool isApple72Estimate(int pxW, int pxH, double mmW, double mmH);

// MÄTNINGEN SOM HELHET: ett giltigt diagonalsvar ger Measured, allt annat
// Assumed med reserven och en läsbar orsak. `mmW`/`mmH` lagras som OS:et gav
// dem (0 = okänt), också när svaret blev Assumed.
DisplayMetrics measure(int pxW, int pxH, int mmW, int mmH, std::string id);

// Assumed med en uttalad orsak — när inget mått ens finns att pröva. RESERVEN
// är `kAssumedPxPerMm` (window.h), 96 dpi i fysiska px, vid varje skala (se
// filhuvudet).
DisplayMetrics assumed(std::string id, std::string reason);

} // namespace gfx::display
