#pragma once
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <gfx/camera.h>
#include <gfx/image.h>
#include <gfx/renderer.h>

#include "glaze/canvas/geometry.h"
#include "glaze/widgets/canvas.h"

// RITYTANS SCEN I ETT EXTERNT MÅL — för ett sidopaket som visar en Canvas i
// en annan yta än fönstret (ett VR-headsets ögon). Ingen del av appens API:
// headern står inte i glaze.h och har ingen `.glazeui`-form, precis som
// `renderToBitmap` är en handling och inte en egenskap.
//
// Passet ritar HUVUDVYNS lager — rutnätet, terrängen och draperingen, djupet,
// världen, etiketterna och `onPaint`/`onPaintLayer` — i 3D-målets ordning,
// med en UTTRYCKLIG `gfx::View3D` per mål (ett asymmetriskt frustum per öga).
// Del-vyerna, kanten och fokusringen hör till fönstret och följer inte med.
// Skrivbordets kamera, mål och cache rörs aldrig, och ingenting läses
// tillbaka till CPU:n.

namespace glaze::detail {

struct ExternalCanvasView {
    gfx::RenderTarget* target = nullptr;   // bundet av passet; ägs av anroparen
    gfx::View3D        view;               // `box` = hela målet i fysiska px
};

class ExternalCanvasPass {
public:
    // Passet för `canvas`, som måste sitta i ett ÖPPET fönster (dess
    // renderare). nullptr + `error` annars. UI-trådens; släpps före ritytan.
    static std::unique_ptr<ExternalCanvasPass> create(Canvas& canvas,
                                                      std::string& error);
    ~ExternalCanvasPass();

    ExternalCanvasPass(const ExternalCanvasPass&)            = delete;
    ExternalCanvasPass& operator=(const ExternalCanvasPass&) = delete;

    // Ritytans fönsters renderare — den som skapar och importerar målen.
    // Gäller bara medan `current()` är sann.
    gfx::Renderer& renderer() const;

    // Sitter ritytan fortfarande i ett öppet fönster vars renderare och
    // enhet är de passet skapades mot? Falskt när fönstret stängts eller
    // öppnats igen — då är `renderer()` riven och får inte användas.
    bool current() const;

    // Ritar scenen i varje mål med dess vy, i ETT block utanför fönstrets
    // frame (`beginOffscreen`/`endOffscreen`): scenens uppladdningar och
    // resursbudget gäller hela blocket, och etiketterna placeras per vy utan
    // att röra skärmens. false + `error` inuti en ritning, utan fönster, med
    // ett ogiltigt mål eller en oanvändbar vy.
    bool render(std::span<const ExternalCanvasView> views, std::string& error);

    // `Canvas::castRay` med en UTTRYCKLIG vy — den som skärmstorlekar,
    // LOD och toleranser räknas mot — i stället för fönstrets kamera.
    std::vector<HitResult> castRay(const Ray3& ray, const HitOptions& options,
                                   const gfx::View3D& view) const;

private:
    explicit ExternalCanvasPass(Canvas& canvas);
    struct State;
    Canvas&                canvas_;
    std::unique_ptr<State> state_;
};

} // namespace glaze::detail
