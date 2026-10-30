#pragma once

// Bryggan mellan spårningen och Canvas, i UI-tråden: ritytans pass mot ett
// externt mål, runtimens swapchain-bilder som importerade mål, förankringen i
// scenen, förflyttning och vridning med spakarna, callbacks, plockningen med
// handkontrollens stråle och ritningen av båda ögonen direkt i bilderna.
// Rör varken OpenXR eller D3D11-typer: bilderna kommer som opaka pekare.

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gfx/native.h>
#include <gfx/renderer.h>

#include <glaze_openxr/session.h>
#include "session_state.h"

namespace glaze::detail {
class ExternalCanvasPass;
}

namespace glaze::openxr::detail {

class CanvasBridge {
public:
    // Ritytans pass för `canvas`, som måste sitta i ett öppet fönster.
    // nullptr + `error` annars. UI-trådens; släpps (`release`) före Canvas.
    static std::unique_ptr<CanvasBridge> create(glaze::Canvas& canvas, const Options& options,
                                                std::string& error);
    ~CanvasBridge();

    CanvasBridge(const CanvasBridge&)            = delete;
    CanvasBridge& operator=(const CanvasBridge&) = delete;

    // Renderarens enhet, kort och funktionsnivå — den enhet sessionen skapas på.
    gfx::NativeDevice nativeDevice() const;

    // Importerar varje bild EN gång som ett mål (MSAA och djup enligt
    // `Options::msaaSamples`); ett ögas första bild delar sina hjälpytor med
    // resten. false + `error` om en bild inte kan importeras.
    bool importImages(const std::array<EyeImageSet, 2>& eyes, std::string& error);

    // Kör en bildruta: skapar förankringen första gången, tillämpar spakarna,
    // anropar `onFrame`, plockar vid ny tryckflank (`onPick`) och ritar båda
    // ögonen i bilderna `request.imageIndex`. Ett fel som stoppar sessionen
    // lämnas i `FrameResult::error`; en bildruta utan ritning (ogiltig pose)
    // ger `rendered == false` utan fel.
    FrameResult runFrame(const FrameRequest& request, const Callbacks& callbacks);

    // Avvecklingen utan att blockera: `beginRelease` lägger en markör efter
    // allt som skickats till GPU:n, `pollRelease` svarar om GPU:n kommit förbi
    // den (Pending medan den inte gjort det, Complete när allt före markören
    // är klart, Failed med text i `error` när enheten förlorats eller markören
    // inte gick att lägga eller fråga), och `release` släpper målen och
    // passet — målen i omvänd ordning, så att ett ögas första mål, som de
    // andra delar hjälpytor med, släpps sist. Utan pass finns inget skickat
    // arbete: `pollRelease` ger då Complete.
    void beginRelease();
    gfx::GpuCompletionState pollRelease(std::string& error);
    void release();

private:
    explicit CanvasBridge(glaze::Canvas& canvas, const Options& options);

    bool createAnchor(const FrameRequest& request, std::string& error);
    void applyLocomotion(const FrameRequest& request);
    FrameInfo makeFrameInfo(const FrameRequest& request) const;
    void pick(const FrameRequest& request, const FrameInfo& info, const Callbacks& callbacks);
    // Ritar båda ögonen; false utan fel när en pose saknas.
    bool renderEyes(const FrameRequest& request, std::string& error);

    glaze::Canvas&         canvas_;
    Options                options_;
    std::unique_ptr<glaze::detail::ExternalCanvasPass> pass_;
    std::array<std::vector<std::unique_ptr<gfx::RenderTarget>>, 2> targets_;
    std::array<std::pair<int, int>, 2> sizes_{};
    std::unique_ptr<gfx::GpuCompletion> completion_;
    std::string            releaseError_;   // markören gick inte att lägga
    std::optional<Anchor>  anchor_;
    std::array<bool, 2>    previousSelect_{};
    std::uint64_t          focusEpoch_ = 0;     // FrameRequest::focusEpoch senast
    double                 headYaw_ = 0.0;      // för blickar rakt upp eller ned
};

} // namespace glaze::openxr::detail
