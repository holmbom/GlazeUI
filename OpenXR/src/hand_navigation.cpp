#include "hand_navigation.h"

#include "hand_logic.h"
#include "vector_ops.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace glaze::openxr::detail {

namespace {

constexpr double kDefaultRangeMetres = 0.12;

// Huvudets vågräta blick och dess höger i spårningsrummet (y upp, −z framåt).
// Blickar man (nästan) rakt upp eller ned pekar huvudets upp framåt respektive
// bakåt. Tomt vid en ogiltig pose.
struct Frame {
    Vec3 forward;
    Vec3 right;
};
std::optional<Frame> frameOf(const TrackingPose& head) {
    if (!allFinite(head.orientation)) return std::nullopt;
    const Pose orientation{Vec3{}, head.orientation};   // en nollkvaternion är identiteten
    constexpr double kMinHorizontal = 1e-3;
    const Vec3 look = transformDirection(orientation, Vec3{0.0, 0.0, -1.0});
    double x = look.x;
    double z = look.z;
    double horizontal = std::hypot(x, z);
    if (horizontal < kMinHorizontal) {
        const Vec3 up = transformDirection(orientation, Vec3{0.0, 1.0, 0.0});
        const double sign = look.y < 0.0 ? 1.0 : -1.0;
        x = sign * up.x;
        z = sign * up.z;
        horizontal = std::hypot(x, z);
        if (horizontal < kMinHorizontal) return std::nullopt;
    }
    Frame frame;
    frame.forward = Vec3{x / horizontal, 0.0, z / horizontal};
    frame.right = Vec3{-frame.forward.z, 0.0, frame.forward.x};
    return frame;
}

} // namespace

void HandNavigator::reset() noexcept {
    mode_ = HandNavigation::None;
    gestures_ = {};
}

NavigationOutput HandNavigator::update(const NavigationInput& input) {
    NavigationOutput out;
    // En gest som bryts utan släpp kräver ett neutralt läge och markeras, så
    // att en pågående teleportering avbryts i stället för att landa.
    const auto abort = [&out](Gesture& g, std::size_t hand) {
        if (g.active) out.aborted[hand] = true;
        g.active = false;
        g.armed = false;
    };

    // Ett byte av läge kräver ett neutralt läge, i båda händerna.
    if (input.mode != mode_) {
        mode_ = input.mode;
        for (std::size_t hand = 0; hand < 2; ++hand) abort(gestures_[hand], hand);
    }

    const double range = std::isfinite(input.rangeMetres) && input.rangeMetres > 1e-6 ? input.rangeMetres
                                                                                    : kDefaultRangeMetres;
    // Spakens radiella dödzon, som kontrollernas (`applyDeadzone`).
    const double deadzone = std::isfinite(input.deadzone) ? std::clamp(input.deadzone, 0.0, 0.95) : 0.2;

    for (std::size_t hand = 0; hand < 2; ++hand) {
        Gesture& g = gestures_[hand];
        const NavigationHand& in = input.hands[hand];

        // Vilka händer som styr: MoveTurn båda, Teleport bara den högra.
        const bool applies = input.mode != HandNavigation::None && in.source == Controller::Source::Hand &&
                             (input.mode == HandNavigation::MoveTurn || hand == 1);
        out.drives[hand] = applies;

        // Ett byte av källa (eller ett avbrott i den) avbryter gesten.
        if (!g.epochKnown || g.epoch != in.epoch) {
            g.epoch = in.epoch;
            g.epochKnown = true;
            abort(g, hand);
        }
        if (!applies) {
            abort(g, hand);
            continue;
        }

        const bool tracked = in.tracked && allFinite(in.pinchPoint);
        if (!input.focused || !tracked) {
            abort(g, hand);
            continue;
        }
        if (!in.pinching) {
            g.active = false;
            g.armed = true;
            continue;
        }

        if (!g.active) {
            // Ett nyp på en panel är panelens: ingen gest förrän nypet släppts.
            if (in.claimed) {
                g.armed = false;
                continue;
            }
            if (!g.armed || !input.headValid) continue;
            const auto frame = frameOf(input.head);
            if (!frame) continue;
            g.active = true;
            g.start = in.pinchPoint;
            g.forward = frame->forward;
            g.right = frame->right;
        }
        out.reserved[hand] = true;

        if (input.mode == HandNavigation::Teleport) {
            out.stickY[hand] = 1.0;   // spaken helt framåt: vägen visas, släppet landar
            continue;
        }
        const Vec3 moved = vsub(in.pinchPoint, g.start);
        double x = vdot(moved, g.right) / range;
        double y = hand == 0 ? vdot(moved, g.forward) / range : 0.0;   // höger hand vrider bara
        applyDeadzone(x, y, deadzone);
        out.stickX[hand] = x;
        out.stickY[hand] = y;
    }
    return out;
}

} // namespace glaze::openxr::detail
