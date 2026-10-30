#pragma once

// Bryggan mellan spårningen och Canvas, i UI-tråden: förankringen i scenen,
// förflyttning och vridning med spakarna, callbacks, plockningen med
// handkontrollens stråle och ritningen av båda ögonen till CPU-pixlar.
// Rör varken OpenXR eller D3D11.

#include <array>
#include <optional>

#include <glaze_openxr/session.h>
#include "session_state.h"

namespace glaze::openxr::detail {

class CanvasBridge {
public:
    CanvasBridge(glaze::Canvas& canvas, const Options& options);

    // Kör en bildruta i UI-tråden: skapar förankringen första gången, tillämpar
    // spakarna, anropar `onFrame`, plockar vid ny tryckflank (`onPick`) och
    // ritar båda ögonen. Ett fel som stoppar sessionen lämnas i
    // `FrameResult::error`; en bildruta utan bild (ogiltig pose, bilden
    // saknas) ger tom `images` utan fel.
    FrameResult runFrame(const FrameRequest& request, const Callbacks& callbacks);

private:
    bool createAnchor(const FrameRequest& request, std::string& error);
    void applyLocomotion(const FrameRequest& request);
    FrameInfo makeFrameInfo(const FrameRequest& request) const;
    void pick(const FrameRequest& request, const FrameInfo& info, const Callbacks& callbacks);
    // Ritar båda ögonen; false när en bild saknas.
    bool renderEyes(const FrameRequest& request, FrameResult& result);

    glaze::Canvas&         canvas_;
    Options                options_;
    std::optional<Anchor>  anchor_;
    std::array<bool, 2>    previousSelect_{};
    std::uint64_t          focusEpoch_ = 0;     // FrameRequest::focusEpoch senast
    std::array<double, 2>  eyeYaw_{};           // för blickar rakt upp eller ned
    double                 headYaw_ = 0.0;
    int                    emptyFrames_ = 0;    // bildrutor i följd utan bild
};

} // namespace glaze::openxr::detail
