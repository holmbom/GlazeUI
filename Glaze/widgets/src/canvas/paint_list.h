#pragma once
// INSPELADE MÅLARE — INTERN. Appens `onPaint` och `onPaintLayer` är appkod
// och körs i UI-tråden, men det de ritar ska ritas av ritytans motor, som kan
// gå i en annan tråd. Målaren har därför ett INSPELNINGSLÄGE: handlern körs
// med en målare som inte ritar utan antecknar varje anrop som en post, och
// listan spelas sedan upp mot en vanlig målare i ritningens läge — samma
// `PainterState`, alltså samma vertex som om handlern ritat direkt.
//
// Det handlern FRÅGAR under inspelningen (`inScreenSpace`, `renderState`,
// `size`, `measureText`) svarar målaren på som vanligt: läget följs med, och
// texten mäts mot inspelningens ritläge utan uppladdning.
//
// EN LISTA ÄR TRÅDNEUTRAL: formernas texter är upplösta till literaler när de
// spelas in (`Shape::withResolvedText`), så uppspelningen aldrig rör
// språkbutiken, och en `drawCanvas`-post bär allt den andra ritytan lämnar —
// dess scen, kamera, ljus och dess egna inspelade lagermålare.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "build.h"   // Xform
#include "glaze/canvas/camera.h"
#include "glaze/canvas/geometry.h"
#include "glaze/canvas/shape.h"
#include "glaze/canvas/style3d.h"
#include "scene.h"   // SceneData, PaintContext, LayerPaint

namespace glaze {
class Canvas;
class Painter;
} // namespace glaze

namespace glaze::detail {

struct PaintRecording;

// `Painter::drawCanvas` som data: den andra ritytans scen och ritläge, tagna
// när anropet spelades in, och rektangeln och kameran ur anropet.
struct CanvasDraw {
    std::string                           name;    // loggens namn
    std::shared_ptr<SceneData>            scene;
    std::uint64_t                         stamp = 0;   // ritytans damageStamp
    Rect                                  rect;
    std::optional<Camera>                 camera;     // den ena av de två
    std::optional<Camera3D>               camera3;
    Lighting                              lighting;
    bool                                  yUp       = false;
    float                                 maxPitch  = 0.0f;
    int                                   samples   = 1;
    bool                                  labelFade = false;
    // Den andra ritytans `onPaintLayer`, inspelad med anropet.
    std::shared_ptr<const PaintRecording> layers;
};

// ETT ANROP till målaren.
struct PaintOp {
    enum class Kind : std::uint8_t {
        Draw,          // draw(Shape) och bekvämligheterna ovanpå
        State,         // setRenderState
        Save,          // save()
        Clip,          // klippet i det läge som just sparades
        Restore,       // lägets slut
        Transform,     // translate/rotate/scale — en affin, sammansatt
        ScreenSpace,   // screenSpace()
        Canvas,        // drawCanvas
    };
    Kind                              kind = Kind::Draw;
    Shape                             shape;        // Draw
    RenderState                       state;        // State
    std::vector<Polygon>              clip;         // Clip — i anropets rymd
    bool                              clipValid = false;   // ytorna redan prövade
    Xform                             xf;           // Transform
    std::shared_ptr<const CanvasDraw> canvas;       // Canvas
};

using PaintList = std::vector<PaintOp>;

// En ritnings målare: `onPaint` och en lista per lager (lagrets plats; tom för
// ett lager utan målare).
struct PaintRecording {
    PaintList              main;
    std::vector<PaintList> layers;
};

// SPELAR IN ritytans målare — UI-tråden, EN körning av `onPaint` (med
// `withMain`) och en av `onPaintLayer` per lager med `setLayerPainter`. `p` är
// ritningens läge (mätningen och `size()` läser det). `target3D`: ritningen
// går i 3D-målet, där målaren i ett World- eller Terrain-lager börjar med
// lagrets renderläge.
std::shared_ptr<const PaintRecording> recordPaints(Canvas& yta, PaintContext& p,
                                                   bool withMain, bool target3D);

// Motorns två krokar (target3d.h `Target3DArgs::paint`, scene.h `LayerPaint`).
struct CanvasPaints {
    std::function<void(PaintContext&)> main;     // tom utan `onPaint`
    LayerPaint                         layers;   // tom utan `onPaintLayer`
};

// Krokarna som SPELAR UPP en inspelning — fria från ritytan och appkoden.
CanvasPaints paintsOf(std::shared_ptr<const PaintRecording> rec);

// Krokarna som spelar in vid FÖRSTA anropet och sedan spelar upp — ritningen i
// UI-tråden, där en cachad bild aldrig kör appens målare.
CanvasPaints lazyPaintsOf(Canvas& yta, bool target3D);

} // namespace glaze::detail
