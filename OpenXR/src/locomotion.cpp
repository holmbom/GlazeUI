#include "locomotion.h"

#include "vector_ops.h"

#include <algorithm>
#include <cmath>

namespace glaze::openxr::detail {

namespace {

// Teleporteringen: spaken framåt över `kTeleportStart` visar bågen, under
// `kTeleportRelease` släpps den. En yta tar emot en landning när dess normal
// pekar minst så här mycket uppåt. Bågen provas segment för segment.
constexpr double kTeleportStart      = 0.7;
constexpr double kTeleportRelease    = 0.3;
constexpr double kLandingNormalZ     = 0.7;
constexpr double kArcStepSeconds     = 0.08;
constexpr double kArcMaxSeconds      = 3.0;
constexpr double kStraightMetres     = 20.0;   // den raka strålens längd när ingen angetts

// Stegvis vridning: ett steg när spaken går över `kSnapStart` i sidled, nästa
// först sedan den gått under `kSnapReset`.
constexpr double kSnapStart = 0.7;
constexpr double kSnapReset = 0.3;

// Vektorn vriden `yawDegrees` medurs sett uppifrån (som förankringens yaw).
Vec3 rotateYaw(double yawDegrees, const Vec3& v) {
    const double a = yawDegrees * kRad;
    const double s = std::sin(a);
    const double c = std::cos(a);
    return {v.x * c + v.y * s, -v.x * s + v.y * c, v.z};
}

// En pose i spårningsrummet som Glaze-axlar utan förankring (yaw 0, skala 1):
// rummets egen ram.
std::optional<Pose> rigPose(const TrackingPose& pose) {
    const auto world = toWorldPose(Anchor{}, pose);
    if (!world) return std::nullopt;
    return toPose(*world);
}

// Förankringen som vridits till `yawDegrees` kring huvudet, så att huvudet
// står kvar där det stod i världen.
std::optional<Anchor> turnedAround(const Anchor& anchor, double yawDegrees, const TrackingPose& head,
                                   const Vec3& headWorld) {
    Anchor turned = anchor;
    turned.yawDegrees = yawDegrees;
    const auto pivot = toWorldPose(Anchor{Vec3{}, yawDegrees, anchor.scale}, head);
    if (!pivot) return std::nullopt;
    turned.origin = vsub(headWorld, pivot->position);
    return turned;
}

// Den vågräta delen av blickens riktning som enhetsvektor. Blickar man
// (nästan) rakt upp eller ned pekar huvudets upp framåt respektive bakåt.
std::optional<Vec3> horizontalForward(const WorldPose& pose) {
    constexpr double kMinHorizontal = 1e-3;
    double x = pose.forward.x;
    double y = pose.forward.y;
    double horizontal = std::hypot(x, y);
    if (horizontal < kMinHorizontal) {
        const double sign = pose.forward.z < 0.0 ? 1.0 : -1.0;
        x = sign * pose.up.x;
        y = sign * pose.up.y;
        horizontal = std::hypot(x, y);
        if (horizontal < kMinHorizontal) return std::nullopt;
    }
    return Vec3{x / horizontal, y / horizontal, 0.0};
}

// Teleportvägens punkter ur siktet: bågen, eller start och slut på den raka
// strålen. Tomt när vägen inte går att räkna.
std::vector<Vec3> teleportPoints(const WorldPose& aim, const Locomotion& settings, const LocomotionInput& input,
                                 double scale) {
    if (settings.teleportPath == Locomotion::Path::Straight) {
        const double span = vlength(aim.forward);
        if (!(span > 1e-9)) return {};
        const double reach = (input.rayLengthMetres > 0.0 ? input.rayLengthMetres : kStraightMetres) * scale;
        return {aim.position, vadd(aim.position, vmul(aim.forward, reach / span))};
    }
    return ballisticArc(aim.position, aim.forward, settings.teleportSpeed * scale, settings.teleportGravity * scale,
                        kArcStepSeconds, kArcMaxSeconds);
}

} // namespace

std::optional<glaze::HitResult> groundHit(const glaze::Ray3& ray, double maxDistance, double groundZ) {
    const double dx = ray.direction.x;
    const double dy = ray.direction.y;
    const double dz = ray.direction.z;
    const double span = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(span) || !(span > 1e-12) || !std::isfinite(groundZ) || !std::isfinite(ray.origin.x) ||
        !std::isfinite(ray.origin.y) || !std::isfinite(ray.origin.z)) {
        return std::nullopt;
    }
    const double ux = dx / span;
    const double uy = dy / span;
    const double uz = dz / span;
    if (std::fabs(uz) < 1e-12) return std::nullopt;
    const double t = (groundZ - ray.origin.z) / uz;
    if (!std::isfinite(t) || !(t > 0.0)) return std::nullopt;
    if (maxDistance > 0.0 && t > maxDistance) return std::nullopt;

    glaze::HitResult hit;
    hit.layer = -1;
    hit.index = -1;
    hit.kind = glaze::HitKind::Surface;
    hit.point = glaze::Point3{ray.origin.x + ux * t, ray.origin.y + uy * t, groundZ};
    hit.normal = glaze::Vec3{0.0, 0.0, 1.0};
    hit.distance = t;
    return hit;
}

std::vector<glaze::HitResult> withGround(std::vector<glaze::HitResult> hits, const glaze::Ray3& ray,
                                         double maxDistance, const std::optional<double>& groundZ) {
    if (!hits.empty() || !groundZ) return hits;
    if (const auto ground = groundHit(ray, maxDistance, *groundZ)) hits.push_back(*ground);
    return hits;
}

std::optional<Anchor> faceYaw(const Anchor& anchor, const TrackingPose& head, double yawDegrees) {
    const auto world = toWorldPose(anchor, head);
    if (!world) return std::nullopt;
    // `makeAnchor` räknar med skala 1 och ger bara riktningen; skalan behålls
    // och ursprunget läggs så att huvudet står kvar.
    const auto fresh = makeAnchor(head, world->position, yawDegrees);
    if (!fresh) return std::nullopt;
    return turnedAround(anchor, fresh->yawDegrees, head, world->position);
}

bool placeFeet(Anchor& anchor, const TrackingPose& head, double floorHeight, const Vec3& feet,
               const std::optional<double>& yawDegrees) {
    if (!std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z) || !std::isfinite(floorHeight)) {
        return false;
    }
    Anchor next = anchor;
    if (yawDegrees) {
        if (!std::isfinite(*yawDegrees)) return false;
        const auto turned = faceYaw(anchor, head, *yawDegrees);
        if (!turned) return false;
        next = *turned;
    }
    const auto world = toWorldPose(next, head);
    if (!world) return false;
    const double feetZ = world->position.z - (head.position.y - floorHeight) * next.scale;
    const Vec3 current{world->position.x, world->position.y, feetZ};
    next.origin = vadd(next.origin, vsub(feet, current));
    anchor = next;
    return true;
}

void Mover::reset() noexcept {
    grab_ = Grab{};
    snapArmed_ = true;
    teleporting_ = false;
    landing_.reset();
    request_.reset();
    arc_.clear();
    arcHit_.reset();
}

void Mover::cancelTeleport() noexcept {
    teleporting_ = false;
    landing_.reset();
    request_.reset();
}

void Mover::cancel() noexcept {
    grab_.mode = GrabMode::None;
    cancelTeleport();
}

std::optional<Landing> Mover::takeLanding() noexcept {
    std::optional<Landing> taken = request_;
    request_.reset();
    return taken;
}

void Mover::update(const LocomotionInput& input, const Locomotion& settings, Anchor& anchor,
                   const SegmentCast& cast) {
    arc_.clear();
    arcHit_.reset();
    request_.reset();
    landed_  = false;
    snapped_ = false;
    if (!input.focused || !input.headValid) {
        reset();
        return;
    }
    if (input.deltaSeconds <= 0.0) return;

    if (settings.grabWorld) {
        if (updateGrab(input, settings, anchor)) {
            teleporting_ = false;
            landing_.reset();
            return;
        }
    } else {
        grab_.mode = GrabMode::None;
    }

    updateTeleport(input, settings, anchor, cast);
    if (!teleporting_) updateTurn(input, settings, anchor);
    updateMove(input, settings, anchor);
}

bool Mover::updateGrab(const LocomotionInput& input, const Locomotion& settings, Anchor& anchor) {
    std::array<std::optional<Pose>, 2> rig;
    int held = 0;
    for (std::size_t hand = 0; hand < 2; ++hand) {
        const LocomotionHand& h = input.hands[hand];
        if (h.squeeze && h.gripValid) rig[hand] = rigPose(h.grip);
        if (rig[hand]) ++held;
    }
    if (held == 0) {
        grab_.mode = GrabMode::None;
        return false;
    }

    if (held == 1) {
        // En hand: världen följer handen. Punkten i världen som handen höll
        // förblir under handen, så rummet flyttas åt andra hållet.
        const int hand = rig[0] ? 0 : 1;
        if (grab_.mode != GrabMode::One || grab_.hand != hand) {
            grab_ = Grab{};
            grab_.mode = GrabMode::One;
            grab_.hand = hand;
            grab_.anchor = anchor;
            grab_.rigAtGrab[static_cast<std::size_t>(hand)] = *rig[static_cast<std::size_t>(hand)];
        }
        const Vec3 moved = vsub(grab_.rigAtGrab[static_cast<std::size_t>(hand)].position,
                               rig[static_cast<std::size_t>(hand)]->position);
        anchor.origin = vadd(grab_.anchor.origin,
                            rotateYaw(grab_.anchor.yawDegrees, vmul(moved, grab_.anchor.scale)));
        return true;
    }

    // Två händer: världen följer båda. Den vrids kring vertikalen efter linjen
    // mellan händerna och skalas efter avståndet mellan dem; punkten mitt
    // emellan förblir mellan händerna.
    const Pose left = *rig[0];
    const Pose right = *rig[1];
    const Vec3 midNow = vmul(vadd(left.position, right.position), 0.5);
    if (grab_.mode != GrabMode::Two) {
        grab_ = Grab{};
        grab_.mode = GrabMode::Two;
        grab_.anchor = anchor;
        grab_.rigAtGrab = {left, right};
        grab_.worldMid = vadd(anchor.origin, rotateYaw(anchor.yawDegrees, vmul(midNow, anchor.scale)));
    }

    // Världen som föremål i rummets ram: dess orientering är förankringens
    // omvända vridning och dess skala 1/skalan.
    Pose world;
    world.orientation = inverse(Pose{Vec3{}, fromYawPitchRoll(YawPitchRoll{grab_.anchor.yawDegrees, 0.0, 0.0})})
                            .orientation;
    const TwoHandResult result = twoHandManipulate(grab_.rigAtGrab[0], grab_.rigAtGrab[1], world,
                                                   1.0 / grab_.anchor.scale, left, right,
                                                   TwoHandRotation::YawOnly);
    if (!(result.scale > 0.0) || !std::isfinite(result.scale)) return true;

    const double lowest = std::max(1e-9, settings.minScale);
    const double highest = std::max(lowest, settings.maxScale);
    const double scale = std::clamp(1.0 / result.scale, lowest, highest);
    const double yaw = toYawPitchRoll(inverse(Pose{Vec3{}, result.pose.orientation}).orientation).yawDegrees;

    anchor.scale = scale;
    anchor.yawDegrees = yaw;
    anchor.origin = vsub(grab_.worldMid, rotateYaw(yaw, vmul(midNow, scale)));
    return true;
}

void Mover::updateTurn(const LocomotionInput& input, const Locomotion& settings, Anchor& anchor) {
    if (settings.turn == TurnMode::None) {
        snapArmed_ = true;
        return;
    }
    const double x = input.hands[1].stickX;
    double yaw = anchor.yawDegrees;
    if (settings.turn == TurnMode::Smooth) {
        if (x == 0.0) return;
        yaw = wrapDegrees(yaw + settings.turnDegreesPerSecond * input.deltaSeconds * x);
    } else {
        if (std::fabs(x) < kSnapReset) snapArmed_ = true;
        if (!snapArmed_ || std::fabs(x) < kSnapStart) return;
        snapArmed_ = false;
        yaw = wrapDegrees(yaw + (x > 0.0 ? settings.snapDegrees : -settings.snapDegrees));
        snapped_ = true;
    }
    const auto head = toWorldPose(anchor, input.head);
    if (!head) return;
    if (const auto turned = turnedAround(anchor, yaw, input.head, head->position)) anchor = *turned;
}

void Mover::updateMove(const LocomotionInput& input, const Locomotion& settings, Anchor& anchor) {
    if (settings.move == MoveMode::None) return;
    const LocomotionHand& left = input.hands[0];
    if (left.stickX == 0.0 && left.stickY == 0.0) return;

    // Riktningen: vänster hands sikte eller huvudets blick.
    const bool byHand = settings.moveAlongHand && left.aimValid;
    const auto source = toWorldPose(anchor, byHand ? left.aim : input.head);
    if (!source) return;
    const auto flat = horizontalForward(*source);
    if (!flat) return;

    Vec3 forward = *flat;
    if (settings.move == MoveMode::Fly) {
        const double n = vlength(source->forward);
        if (n > 1e-9) forward = vmul(source->forward, 1.0 / n);
    }
    const Vec3 right{flat->y, -flat->x, 0.0};
    const double distance = settings.moveMetresPerSecond * input.deltaSeconds * anchor.scale;
    anchor.origin = vadd(anchor.origin, vmul(vadd(vmul(forward, left.stickY), vmul(right, left.stickX)), distance));
}

void Mover::updateTeleport(const LocomotionInput& input, const Locomotion& settings, Anchor& anchor,
                           const SegmentCast& cast) {
    if (!settings.teleport) {
        teleporting_ = false;
        landing_.reset();
        return;
    }
    const LocomotionHand& hand = input.hands[1];
    if (!teleporting_) {
        if (hand.stickY < kTeleportStart || !hand.aimValid) return;
        teleporting_ = true;
        landing_.reset();
    }

    if (!hand.aimValid) {
        // Siktet tappades: avbrutet, ingen landning.
        teleporting_ = false;
        landing_.reset();
        return;
    }
    if (hand.stickY < kTeleportRelease) {
        // Släppt: fötterna ska hamna där vägen landade — direkt, eller som en
        // begäran när landningen går genom komforten.
        teleporting_ = false;
        if (landing_) {
            if (input.deferLanding)
                request_ = Landing{*landing_, std::nullopt};
            else
                landed_ = placeFeet(anchor, input.head, input.floorHeight, *landing_, std::nullopt);
        }
        landing_.reset();
        return;
    }

    const auto aim = toWorldPose(anchor, hand.aim);
    if (!aim) return;
    const std::vector<Vec3> points = teleportPoints(*aim, settings, input, anchor.scale);
    landing_.reset();
    if (points.size() < 2) return;

    arc_.push_back(points.front());
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const Vec3 segment = vsub(points[i + 1], points[i]);
        const double span = vlength(segment);
        if (span < 1e-12) continue;
        glaze::Ray3 ray;
        ray.origin = glaze::Point3{points[i].x, points[i].y, points[i].z};
        ray.direction = glaze::Vec3{segment.x / span, segment.y / span, segment.z / span};
        const std::vector<glaze::HitResult> hits = cast ? cast(ray, span) : std::vector<glaze::HitResult>{};
        if (!hits.empty() && hits.front().point) {
            const glaze::HitResult& hit = hits.front();
            arc_.push_back(Vec3{hit.point->x, hit.point->y, hit.point->z});
            if (hit.kind == glaze::HitKind::Surface && hit.normal.z >= kLandingNormalZ) {
                arcHit_ = hit;
                landing_ = Vec3{hit.point->x, hit.point->y, hit.point->z};
            }
            return;
        }
        arc_.push_back(points[i + 1]);
    }
}

} // namespace glaze::openxr::detail
