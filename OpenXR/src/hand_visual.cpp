#include <glaze_openxr/hand_visual.h>

#include "hand_logic.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace glaze::openxr {

using namespace detail;

namespace {

// Fingrarna: handleden och sedan fingrets leder i följd, från metakarpalleden
// till toppen (OpenXR:s ordning). Varje led mot nästa är en linje.
struct Finger {
    int first;
    int last;
};
constexpr std::array<Finger, 5> kFingers = {{
    {kJointThumbMetacarpal, kJointThumbTip},
    {kJointIndexMetacarpal, kJointIndexTip},
    {kJointMiddleMetacarpal, kJointMiddleTip},
    {kJointRingMetacarpal, kJointRingTip},
    {kJointLittleMetacarpal, kJointLittleTip},
}};

// Handflatans kontur: handleden och de fyra fingrarnas metakarpalleder.
constexpr std::array<int, 6> kPalmOutline = {kJointWrist, kJointIndexMetacarpal, kJointMiddleMetacarpal,
                                             kJointRingMetacarpal, kJointLittleMetacarpal, kJointWrist};

} // namespace

HandVisual::HandVisual(glaze::Canvas& canvas, int layer) : canvas_(canvas), layer_(layer) {}

HandVisual::~HandVisual() {
    if (drawn_) canvas_.setShapes(layer_, {});
}

void HandVisual::setVisible(bool visible) {
    if (visible == visible_) return;
    visible_ = visible;
    if (!visible_ && drawn_) {
        canvas_.setShapes(layer_, {});
        drawn_ = false;
    }
}

void HandVisual::setStyle(const HandVisualStyle& style) { style_ = style; }

void HandVisual::update(const FrameInfo& frame) {
    std::vector<glaze::Shape> shapes;
    if (visible_) {
        const double scale = std::isfinite(frame.anchor.scale) && frame.anchor.scale > 0.0 ? frame.anchor.scale : 1.0;
        // En led utan radie (runtimen ger ingen) får en tunn standardlinje.
        const double minWidth = 0.003 * scale;
        const double radiusScale = std::isfinite(style_.radiusScale) && style_.radiusScale > 0.0 ? style_.radiusScale : 1.0;

        for (const TrackedHand& hand : frame.hands) {
            if (!hand.active) continue;
            const auto joint = [&](int index) -> const HandJoint* {
                const HandJoint& j = hand.joints[static_cast<std::size_t>(index)];
                return j.valid ? &j : nullptr;
            };
            const auto addLine = [&](const HandJoint& a, const HandJoint& b) {
                const double radius = 0.5 * (std::max(0.0, a.radius) + std::max(0.0, b.radius));
                glaze::Stroke stroke;
                stroke.paint = style_.color;
                stroke.cap   = glaze::LineCap::Round;
                stroke.width = glaze::world(static_cast<float>(std::max(minWidth, 2.0 * radius * radiusScale)));
                shapes.push_back(glaze::Shape::line3(
                    glaze::Point3{a.pose.position.x, a.pose.position.y, a.pose.position.z},
                    glaze::Point3{b.pose.position.x, b.pose.position.y, b.pose.position.z}, stroke));
            };
            const auto segment = [&](int from, int to) {
                const HandJoint* a = joint(from);
                const HandJoint* b = joint(to);
                if (a && b) addLine(*a, *b);
            };
            for (const Finger& finger : kFingers) {
                segment(kJointWrist, finger.first);
                for (int j = finger.first; j < finger.last; ++j) segment(j, j + 1);
            }
            for (std::size_t i = 0; i + 1 < kPalmOutline.size(); ++i) segment(kPalmOutline[i], kPalmOutline[i + 1]);
        }
    }

    if (shapes.empty()) {
        if (drawn_) {
            canvas_.setShapes(layer_, {});
            drawn_ = false;
        }
        return;
    }
    canvas_.setShapes(layer_, std::move(shapes));
    drawn_ = true;
}

} // namespace glaze::openxr
