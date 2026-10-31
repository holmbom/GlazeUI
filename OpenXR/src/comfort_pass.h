#pragma once

// Komfortens ritning: vinjetten och svarttoningen, per öga OVANPÅ den färdiga
// scenen i swapchain-bilden. Båda är en och samma fullskärmsyta med svart
// alfa — en mjuk radiell kant efter vinkeln från blickens axel (ögats
// ASYMMETRISKA synfält) kombinerad med en jämn svärta över hela bilden.
//
// Varje swapchain-bild importeras en gång till som ett EGET enprovsmål utan
// djup, och ytan ritas med `Renderer::pushTargetPreserved` så att scenen
// ligger kvar. Scenens MSAA-mål återbinds aldrig här: dess hjälpytor delas
// mellan bilderna. Kräver D3D11 (programmet är HLSL); övriga backends ger ett
// ogiltigt program och `usable()` falskt.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gfx/camera.h>
#include <gfx/program.h>
#include <gfx/renderer.h>

#include "comfort_logic.h"
#include "session_state.h"

namespace glaze::openxr::detail {

class ComfortPass {
public:
    // Ett ögas del av en ritning.
    struct Eye {
        std::uint32_t        imageIndex = 0;   // swapchain-bilden att rita i
        gfx::FrustumTangents tangents;         // ögats synfält
        VignetteShape        shape;            // öppningen i vinjetten
    };

    // Programmet byggs mot `renderer`, som måste leva längre än passet
    // (`release` före renderaren).
    explicit ComfortPass(gfx::Renderer& renderer);
    ~ComfortPass();

    ComfortPass(const ComfortPass&)            = delete;
    ComfortPass& operator=(const ComfortPass&) = delete;

    // Går programmet att rita med?
    bool usable() const;

    // Importerar ögats swapchain-bilder (`images`) som enprovsmål utan djup.
    // false + `error` om en bild inte går att importera.
    bool importImages(std::size_t eye, const EyeImageSet& images, std::string& error);

    // Ritar ytan i båda ögonens bilder, i ett eget offscreen-block: vinjetten
    // enligt `Eye::shape` och en jämn svärta `blackAlpha` (0 … 1) över allt.
    // false + `error` när blocket eller en bindning misslyckas.
    bool draw(const std::array<Eye, 2>& eyes, double blackAlpha, std::string& error);

    // Släpper målen (i omvänd ordning) och programmet.
    void release();

private:
    gfx::Renderer*                       renderer_;
    std::unique_ptr<gfx::ShaderProgram>  program_;
    gfx::VertexLayout                    layout_;   // tom: fullskärmstriangeln gör sina egna hörn
    std::array<std::vector<std::unique_ptr<gfx::RenderTarget>>, 2> targets_;
};

} // namespace glaze::openxr::detail
