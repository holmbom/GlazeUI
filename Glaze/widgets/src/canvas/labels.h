#pragma once
#include <cstdint>
#include <memory>
#include <span>

#include <gfx/image.h>

#include "camera_math.h"
#include "resources.h"
#include "scene.h"

// INTERN — ETIKETTKOLLISIONEN INKOPPLAD. Algoritmen
// är placement.h:s rena funktion; här är det runt den:
//
//   SAMLA     de RITADE Glyph-körningarna i ritordning — ett dolt lager, ett
//             lager utanför sitt zoomintervall och ett genomskinligt lager
//             har inga etiketter som syns och tar därför ingen plats.
//   PROJICERA varje etiketts ankare med kameran i DOUBLE (`Projection`) och
//             lägg dess låda på skärmen ur glyflayouten (`TextBox`, em ×
//             storleken i px), vriden efter skärmen eller kartan — exakt det
//             Glyph-programmet ritar.
//   PLACERA   med `placeLabelsInOrder`: prioriteten, lagret som ritas
//             överst, indexet — ordningen räknad när etiketterna ändrades,
//             inte vid varje kameraändring. En låda HELT UTANFÖR
//             ytan placeras inte: den döljs och tränger inte undan någon
//             som syns. Lagrets `setLayerCollision(false)` och stilens
//             `collide = false` gör etiketten alltid synlig.
//   LADDA UPP synligheten — en R8-texel per etikett, rad för rad — men bara
//             de texlar som ändrats: en kameraändring kostar högst N byte,
//             aldrig geometri. Ingen kolliderande etikett = ingen bild.
//
// INDEXET ÄR GLOBALT PER RITYTA OCH FÖNSTER: körningens bas (dess plats i
// ritordningen) + etikettens index i bygget. Vertexen bär bygget index och
// basen går som uniform per rit-anrop, så en körning som byggs om flyttar
// ingen annan körnings vertex — bara baserna, som är tal.
//
// SAMMA INDATA PLACERAR INTE OM: kameran, tätheten och körningarna (med
// byggets löpnummer) sparas, och en ritning med samma — en partiell
// omritning, två damage-ytor i en frame — återanvänder svaret utan att
// räkna eller ladda upp.

namespace glaze::detail {

class DrapeFrame;   // drape.h — framens draperingar

// En ritad Glyph-körning: bygget, lagrets plats i ritordningen och om lagret
// låter sina etiketter kollidera.
struct LabelSource {
    const RunData*                     run   = nullptr;
    const CanvasResources::GlyphBuild* build = nullptr;
    int                                layer = 0;
    bool                               collide = true;
};

// Svaret för en ritning: synlighetsbilden (nullptr = alla synliga) och
// körningarnas baser.
struct LabelFrame {
    const CanvasResources::LabelState* state = nullptr;
    const gfx::Image*                  tex   = nullptr;
    bool                               animating = false;   // tonar fortfarande

    // Körningens första texel, eller −1 när den inte placerades.
    std::int64_t baseOf(std::uint64_t runId) const;
};

// Samlar, placerar och laddar upp för ritytan `scene` — se filhuvudet.
// `fade`: tonar in och ut (`Canvas::setLabelFade`) i stället för att växla.
// I 3D-målet (`PaintContext::view3D`) projiceras ankarna med 3D-vyn och
// sitt z. Placeringen sparas i ritningens plats
// (`PaintContext::labelSlot` — vyns, eller bildens).
LabelFrame updateLabels(const PaintContext& f,
                        const std::shared_ptr<SceneData>& scene,
                        std::span<const LabelSource> sources, bool fade);

// LAGRENS FÖRBEREDELSE (scene.h `prepareLayers`) — etiketternas svar och
// varje lagers placering med Auto avgjord. Står här och inte i scene.h: den
// bär `LabelFrame`, och scene.h kan inte se den här headern (resources.h
// inkluderar scene.h).
struct LayerFrame {
    LabelFrame                  labels;      // synligheten och körningarnas baser
    std::vector<LayerPlacement> placement;   // per lager, Auto avgjord
    // Ett ritat Ground-lager målar marken (scene.h `paintsGround`) — ett
    // tomt gör det inte. Villkoret för markdjupet (target3d.h steg 3).
    bool                        groundDrawn = false;
    // TERRÄNGEN (drape.h): en terräng ritas i vyn — då
    // ritas Ground-lagrens geometri genom draperingen, deras stående
    // etiketter byggs vid markhöjd (med terrängens region under körningen i
    // byggets nyckel) och markdjupet uteblir.
    bool                        terrain = false;
    // Framens draperingar (target3d.cpp sätter den efter förberedelsen) —
    // terrängens meshar binder dem; nullptr utan terräng.
    const DrapeFrame*           drapes = nullptr;
    // En toning pågår — ritytan ritar nästa frame också (den invaliderar
    // sig själv).
    bool animating() const { return labels.animating; }
};

// EN ETIKETTS LÅDA PÅ SKÄRMEN — ankaret projicerat, lådan i em × storleken
// (`sizePx`, fysiska px per em), vriden efter skärmen eller kartans
// projicerade riktning vid ankaret: exakt det Glyph-programmet ritar. En
// LIGGANDE etikett (`Alignment::Ground`) svarar med den
// projicerade quadens axelparallella skärmlåda (riktningen (1, 0)). Delad av
// placeringen och träffprovet. false bakom närplanet.
struct LabelGeom {
    double cx = 0.0, cy = 0.0;         // lådans mitt, målets fysiska px
    double halfW = 0.0, halfH = 0.0;   // halva måtten, ovridna
    double ex = 1.0, ey = 0.0;         // baslinjens riktning (y nedåt)
    double ax = 0.0, ay = 0.0;         // ankaret på skärmen
};
bool labelGeometry(const CanvasResources::LabelInfo& li, double sizePx,
                   const Projection& p, LabelGeom& out);

} // namespace glaze::detail
