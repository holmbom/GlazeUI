#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "camera_math.h"
#include "resources.h"
#include "scene.h"

// INTERN — RITYTANS TRÄFFPROV: vilka former pekaren står
// på, överst först. Avståndet räknas i SKÄRMENS fysiska px med projektionen
// i double (`Projection`) — exakt också under tilt: en rak världskant är en
// rak skärmkant, och den del som ligger bakom närplanet skärs bort före
// projektionen. Toleransen är stilens `canvasHitTolerance`.
//
// PER SLAG:
//   linje     skärmavståndet till varje segment ≤ halva bredden + toleransen
//             (bredden i px eller mm på skärmen; en bredd i VÄRLDEN prövas i
//             planet, med toleransen omräknad till världsenheter vid pekaren)
//   yta       pekaren i ytan (jämn–udda över alla ringar, hålen ute) — i
//             världen, där pekarens stråle når planet — eller inom
//             toleransen från en kant; konturen som en linje. INSIDAN
// är en synlig fyllning eller konturens `hitInside` —
//             då också på en linje och utan någon synlig del (`insideCounts`)
//   bild      i rektangeln eller inom toleransen från dess kant
//   markör    skärmavståndet till punkten ≤ radien + toleransen (en kvadrat
//             vriden som den ritas)
//   text      etikettens låda som den ritas (`labelGeometry`) — bara en
//             SYNLIG etikett: en som kollisionen dolt träffas inte, och
//             inte heller en text som aldrig ritats i fönstret (lådan är
//             glyflayoutens, och den byggs i ritningen). Kolliderande
//             markörer går samma väg. Text och markörer I RYMDEN likaså,
//             med ankarets z.
//   linje i rymden  träffas inte här — 3D-plockningen
//             (pick3d.h) tar den.
// En MARKÖRFORM svarar med PUNKTEN (`LayerHit::element`): den
// träffade vars mitt ligger närmast pekaren, vid lika den senare.
//
// RUTAN (`hitLayerRect`) prövar samma former mot en rektangel på
// skärmen med samma geometri — konturerna med mittlinjen, avskurna mot
// närplanet; insidan, bildens ring, markörens och etikettens låda —, i
// FÖNSTER (helt inom) eller KORSNING (skär eller inom), och markörerna punkt
// för punkt.
//
// RUTNÄTET byggs LAT per lager (`LayerData::hit`) ur formernas världslådor
// och gäller för formernas version (`LayerData::shapesVersion`). En fråga
// tar pekarens ruta, uppblåst med toleransen och det största skärmmått en
// form i lagret når utanför sin låda (linjens halva bredd, markörens radie),
// projicerar den tillbaka till planet och prövar bara formerna i de celler
// den täcker. Når rutan ovanför horisonten prövas alla.

namespace glaze::detail {

// En fråga: kameran, pekaren och toleransen i målets fysiska px, och
// tätheten (fysiska px per logisk px, per mm och per världsenhet i kamerans
// MITT — text och markörer i världsenheter). Projektionen är ritningens —
// kartkamerans eller, med en `Camera3D` eller i 3D-målet,
// 3D-vyns; frågorna mot planet z = 0 går genom `Projection::ground`/
// `groundScale`, så de gäller båda.
struct HitQuery {
    const Projection* proj   = nullptr;
    double            px     = 0.0, py = 0.0;
    double            tolPx  = 0.0;
    double            pxPerPx = 1.0, pxPerMm = 1.0;
    double            pxPerWorld = 1.0;
    // MARKEN UNDER PEKAREN när den inte är planet z = 0 (TERRÄNGENS
    // träffpunkt, där Ground-lagret draperas): ytorna och bilderna
    // prövas i världens xy vid punkten, och linjerna och kanterna som ett
    // avstånd i världens xy med skärmmåtten omräknade där
    // (`groundPxPerUnit`, fysiska px per världsenhet). Markörerna och
    // etiketterna står kvar där de ritas.
    bool   hasGround       = false;
    double groundX         = 0.0, groundY = 0.0;
    double groundPxPerUnit = 0.0;
    // INGEN MARK under pekaren: terräng ritas men inte där (ett
    // hål, bortom den), och ett Ground-lagers ytor, linjer och bilder finns
    // bara i draperingen — de prövas inte. Markörerna och etiketterna står
    // kvar där de ritas och prövas som vanligt.
    bool   noGround        = false;
    // RITYTANS ZOOM (logiska px per världsenhet): text och markörer
    // utanför sitt zoomintervall träffas inte. 0 = okänd — då döljs inget.
    double zoom = 0.0;
    // ANROPETS TOLERANS I VÄRLDSENHETER — läggs till `tolPx`
    // omräknad vid pekaren (marken under den, eller terrängens punkt; 0
    // ovanför horisonten). Rutans prov (`hitLayerRect`) läser varken pekaren,
    // toleranserna eller marken — bara projektionen, tätheterna och zoomen.
    double tolWorld = 0.0;
};

// En träff i lagret: formens index och, för en MARKÖRFORM, punkten
// (−1 = hela formen).
struct LayerHit {
    int          index   = -1;
    std::int64_t element = -1;
};

struct HitGrid {
    std::uint64_t version = 0;

    // Formens låda i världen (en linjebredd i världen inräknad) — och om
    // formen alls prövas här (text och kolliderande markörer går genom
    // etiketterna, en form utan punkter eller utan synlig del inte alls).
    struct Entry {
        double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
        bool   grid = false;
    };
    std::vector<Entry> entries;   // index = formens

    // Rutnätet över lådornas union.
    double                                  ox = 0.0, oy = 0.0, cell = 1.0;
    int                                     nx = 1, ny = 1;
    std::vector<std::vector<std::uint32_t>> cells;
    std::vector<std::uint32_t>              big;   // täcker för många celler

    // Det största skärmmått en form når utanför sin låda, per enhet (px,
    // mm, världsenheter — `Unit`): linjens halva bredd och markörens radie
    // med kontur.
    float reach[3] = {0.0f, 0.0f, 0.0f};
    // GOLVETS räckvidd: en markör som golvet förstorat når golvet ×
    // hela sitt mått i em — ankarets andel per golvets enhet, och konturens
    // andel per (golvets, konturens, storlekens) enhet, för markerRadius
    // räknar konturen relativt storleken med frågans tätheter. Summan av
    // allt är konservativ.
    float floorReach[3]         = {0.0f, 0.0f, 0.0f};
    float floorOutline[3][3][3] = {};

    // Formen bakom en etikett: posten → formens index.
    std::unordered_map<const ShapeEntry*, std::uint32_t> indexOf;
};

// Formerna i lagret som pekaren träffar, ÖVERST FÖRST, som formernas index.
// `res` och `labels` är fönstrets resurser och ritytans placering där (FÅR
// vara null — då träffas varken text eller kolliderande markörer).
std::vector<LayerHit> hitLayer(LayerData& L, const HitQuery& q,
                               const CanvasResources*             res,
                               const CanvasResources::LabelState* labels);

// Formerna i lagret i RUTAN — `rect` i målets fysiska px med
// x0 ≤ x1 och y0 ≤ y1 —, ÖVERST FÖRST och markörformerna en träff per punkt
// i rutan (den senare punkten först). `window`: helt inom, annars korsning.
struct ScreenRect {
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
};
std::vector<LayerHit> hitLayerRect(LayerData& L, const HitQuery& q,
                                   const ScreenRect& rect, bool window,
                                   const CanvasResources*             res,
                                   const CanvasResources::LabelState* labels);

} // namespace glaze::detail
