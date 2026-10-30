#pragma once

// Bryggan mellan spårningen och Canvas, i UI-tråden: ritytans pass mot ett
// externt mål, runtimens swapchain-bilder som importerade mål, förankringen i
// scenen (med skala), den inbyggda förflyttningen, callbacks, strålarna mot
// scenen och ritningen av båda ögonen direkt i bilderna.
// Rör varken OpenXR eller D3D11-typer: bilderna kommer som opaka pekare.

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gfx/native.h>
#include <gfx/renderer.h>

#include <glaze_openxr/session.h>
#include "locomotion.h"
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

    // Kör en bildruta: skapar förankringen första gången, tillämpar det
    // appen begärt (`setAnchor`, `recenter`, `teleportTo`), den inbyggda
    // förflyttningen, strålarna (med reservmarken `Options::groundZ`),
    // `onInput` per händelse och `onFrame`, och ritar båda ögonen
    // i bilderna `request.imageIndex`. Ett fel som stoppar sessionen lämnas
    // i `FrameResult::error`; en bildruta utan ritning (ogiltig pose) ger
    // `rendered == false` utan fel.
    FrameResult runFrame(const FrameRequest& request, const Callbacks& callbacks);

    // FÖRANKRINGEN, från UI-tråden. `setAnchor`, `recenter` och `teleportTo`
    // gäller från nästa bildruta (de två sista när huvudet har ett läge, i den
    // ordningen). `anchor()` ger det som just begärts, annars det som gäller.
    // `teleportTo` är false för ett värde som inte är ändligt.
    Anchor anchor() const;
    void   setAnchor(const Anchor& anchor);
    void   recenter(double yawDegrees);
    bool   teleportTo(const Vec3& feet, const std::optional<double>& yawDegrees);
    void   setLocomotion(const Locomotion& locomotion);

    // En stråle mot scenen, räknad mot headsetets vy från senaste bildrutan.
    // Tom innan första bildrutan.
    std::vector<glaze::HitResult> castRay(const glaze::Ray3& ray, const glaze::HitOptions& options) const;

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
    // Ögat och kompassriktningen ur ritytans kamera — en Camera3D (UpAxis::Z)
    // eller kartkameran. false + `error` när kameran inte går att använda.
    struct CameraStart {
        Vec3   eye;
        double yawDegrees = 0.0;
    };
    bool cameraStart(CameraStart& start, std::string& error) const;
    bool mapCameraStart(CameraStart& start, std::string& error) const;
    void applyRequests(const FrameRequest& request);
    void applyLocomotion(const FrameRequest& request);
    void updateHeadView(const FrameRequest& request);
    FrameInfo makeFrameInfo(const FrameRequest& request);
    std::vector<glaze::HitResult> cast(const glaze::Ray3& ray, const glaze::HitOptions& options) const;
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
    std::optional<Anchor>  pendingAnchor_;      // `setAnchor`, till nästa bildruta
    std::optional<double>  pendingRecenter_;    // `recenter`, till nästa bildruta med ett huvud
    struct PendingTeleport {
        Vec3                  feet;
        std::optional<double> yawDegrees;
    };
    std::optional<PendingTeleport> pendingTeleport_;   // `teleportTo`, till nästa bildruta med ett huvud
    Mover                  mover_;
    std::optional<gfx::View3D> headView_;       // plockningens vy från senaste bildrutan
};

} // namespace glaze::openxr::detail
