#pragma once
// INTERN header — ETIKETTPLACERINGEN: vilka
// etiketter som syns när de krockar. En REN funktion utan renderer, kamera
// eller widget — ritytan projicerar ankarna, bygger lådorna ur glyflayouten
// och laddar upp svaret som en synlighetstextur (en texel per etikett); allt
// det kopplas in i nästa steg. Här finns bara algoritmen, och den grindas
// headless av `tests/placement_gate.cpp`.

#include <cstdint>
#include <span>
#include <vector>

#include <gfx/geometry.h>

namespace glaze::detail {

// EN ETIKETTS LÅDA i SKÄRMRYMDEN — fysiska px, samma rymd som kamerans
// projektion och fönstrets klipp. En VRIDEN låda räknas som sin OMSLUTANDE
// axelparallella: grovt för en lång etikett i 45°, men det är den som gör
// rutnätet till ett rutnät (och MapLibre gör likadant med sina lådor).
//
// VRIDNINGEN är BASLINJENS RIKTNING på skärmen, en enhetsvektor — inte en
// vinkel: ritytan har riktningen färdig ur projektionen, och den omslutande
// lådan behöver bara dess |x| och |y|. En vinkel hade räknats fram ur
// riktningen och tillbaka igen vid varje kameraändring.
struct LabelBox {
    float cx = 0.0f, cy = 0.0f;         // lådans mitt
    float halfW = 0.0f, halfH = 0.0f;   // halva måtten, OVRIDNA
    float dirX = 1.0f, dirY = 0.0f;     // baslinjens riktning (enhetsvektor)
    // LUFTEN runt lådan (`TextStyle::collisionPadding`, px) — läggs på den
    // omslutande lådan, i skärmens axlar. Negativ krymper lådan, aldrig
    // under noll.
    float padding = 0.0f;
    int   priority = 0;   // HÖGRE vinner
    int   layer    = 0;   // lagrets index — vid lika prioritet vinner det HÖGRE
    // false: etiketten DÖLJS ALDRIG och täcker ingen annan (`collide = false`
    // på stilen eller lagret). Den deltar inte alls.
    bool  collide = true;
};

// Lådans OMSLUTANDE i skärmens axlar, med luften utanpå — det placeringen
// prövar överlappet med. Ritytan gallrar med den också: en etikett vars låda
// ligger helt utanför ytan placeras inte (labels.h).
gfx::Rectf labelBounds(const LabelBox& l);

// Placerar etiketterna GIRIGT och svarar med en flagga per etikett i
// indatans ordning (1 = synlig, 0 = dold).
//
// ORDNINGEN är hela regeln: prioritet (högst först), sedan lagret (det som
// ritas ÖVERST först), sedan indatans index (det som kom FÖRST först). Varje
// etikett i tur sätts in om dess låda inte överlappar en redan insatt; annars
// döljs den. Lagerordningen är MapLibres — etiketter i ett övre lager vinner
// över ett undre, för de är förgrunden (och hade täckt det undre ändå) — och
// indexordningen likaså: inom samma prioritet och lager behåller en tidigare
// etikett sin plats, så en tillagd etikett med samma prioritet aldrig släcker
// en som redan syns. Med index som sista nyckel är ordningen TOTAL, och
// svaret DETERMINISTISKT.
//
// RUTNÄTET (celler om 64 px över `screen`) är bara grovsållningen: en låda
// läggs i varje cell den täcker och prövas mot det som redan ligger i de
// cellerna, med EXAKT överlapp — snittet måste ha en yta, så kanter som bara
// möts krockar inte och en låda utan yta krockar med ingenting. Lådor
// utanför `screen` delar kantcellerna — långsammare, aldrig fel — så svaret
// beror inte av `screen`; det är ritytans box, och den håller rutnätet
// litet. En låda med en icke-ändlig siffra kan inte placeras och döljs
// (utom med `collide = false`).
std::vector<std::uint8_t> placeLabels(std::span<const LabelBox> labels,
                                      const gfx::Rectf&         screen);

// ORDNINGEN SOM PERMUTATION — indexen i `labels` i placeringens ordning
// (prioritet, lager, index; `placeLabels` sorterar med den). Den beror inte
// på kameran, så ritytan räknar den EN gång när etiketterna ändras
// (labels.cpp) i stället för vid varje kameraändring — sorteringen var
// hälften av placeringen med 10 000 etiketter på skärmen.
std::vector<std::uint32_t> placementOrder(std::span<const LabelBox> labels);

// `placeLabels` för lådor som REDAN står i placeringens ordning — inget
// sorteras, och svaret är i samma ordning. Samma regel och samma rutnät; en
// följd i fel ordning ger fel svar.
std::vector<std::uint8_t> placeLabelsInOrder(std::span<const LabelBox> ordered,
                                             const gfx::Rectf&         screen);

} // namespace glaze::detail
