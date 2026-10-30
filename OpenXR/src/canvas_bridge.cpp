#include "canvas_bridge.h"

#include "hands.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

#include <gfx/camera.h>
#include <glaze/canvas/external_pass.h>

namespace glaze::openxr::detail {

namespace {

// Halvvinklar nära eller över 90° ger ingen perspektivprojektion.
constexpr double kMaxHalfAngle = 3.14159265358979323846 / 2.0 - 1e-3;

TrackedPose toTracked(const Anchor& anchor, const PoseSample& sample) {
    TrackedPose tracked;
    if (!sample.valid) return tracked;
    tracked.pose = toWorldPose(anchor, sample.pose);
    tracked.positionTracked = sample.positionTracked;
    tracked.orientationTracked = sample.orientationTracked;
    return tracked;
}

PoseSample sampleOf(const TrackingPose& pose, bool valid) {
    PoseSample sample;
    sample.pose = pose;
    sample.valid = valid;
    sample.positionTracked = valid;
    sample.orientationTracked = valid;
    return sample;
}

// En riktning eller hastighet i spårningsrummet som vektor i världen: vriden
// med förankringens yaw och, för en längd per tid, skalad med dess skala.
Vec3 toWorldVector(double yawDegrees, double scale, const Vec3& v) {
    const auto world = toWorldPose(Anchor{Vec3{}, yawDegrees, scale}, TrackingPose{v, Quaternion{}});
    return world ? world->position : Vec3{};
}

gfx::Vec3d toVec3d(const Vec3& v) { return gfx::Vec3d{v.x, v.y, v.z}; }

// Synfältets vinklar som tangenter till ytans kanter, eller tomt när synfältet
// inte är en användbar perspektivprojektion.
std::optional<gfx::FrustumTangents> tangentsOf(const Fov& fov) {
    const double angles[4] = {fov.left, fov.right, fov.up, fov.down};
    for (const double a : angles) {
        if (!std::isfinite(a) || std::fabs(a) >= kMaxHalfAngle) return std::nullopt;
    }
    gfx::FrustumTangents t;
    t.left = std::tan(fov.left);
    t.right = std::tan(fov.right);
    t.down = std::tan(fov.down);
    t.up = std::tan(fov.up);
    if (!(t.left < t.right) || !(t.down < t.up)) return std::nullopt;
    return t;
}

// Perspektivvyn ur en pose i världen med frustumet, klipplanen ur `options`
// (i meter, skalade med förankringens skala) och en ruta som är hela bilden
// (`size` i px).
gfx::View3D viewFrom(const WorldPose& pose, const gfx::FrustumTangents& frustum,
                     std::pair<int, int> size, const Options& options, double scale) {
    gfx::View3D view;
    view.eye = toVec3d(pose.position);
    view.forward = toVec3d(pose.forward);
    view.up = toVec3d(pose.up);
    view.projection = gfx::Projection3D::Perspective;
    view.frustum = frustum;
    view.nearZ = options.nearMetres * scale;
    view.farZ = options.farMetres * scale;
    view.box = gfx::Rectf{0.0f, 0.0f, static_cast<float>(size.first), static_cast<float>(size.second)};
    return view;
}

bool usable(const Anchor& anchor) {
    return std::isfinite(anchor.origin.x) && std::isfinite(anchor.origin.y) && std::isfinite(anchor.origin.z) &&
           std::isfinite(anchor.yawDegrees) && std::isfinite(anchor.scale) && anchor.scale > 0.0;
}

} // namespace

CanvasBridge::CanvasBridge(glaze::Canvas& canvas, const Options& options)
    : canvas_(canvas), options_(options) {}

CanvasBridge::~CanvasBridge() { release(); }

std::unique_ptr<CanvasBridge> CanvasBridge::create(glaze::Canvas& canvas, const Options& options,
                                                   std::string& error) {
    std::unique_ptr<glaze::detail::ExternalCanvasPass> pass =
        glaze::detail::ExternalCanvasPass::create(canvas, error);
    if (!pass) {
        if (error.empty()) error = "Canvas går inte att rita i ett externt mål (kräver ett öppet fönster).";
        return nullptr;
    }
    std::unique_ptr<CanvasBridge> bridge(new CanvasBridge(canvas, options));
    bridge->pass_ = std::move(pass);
    return bridge;
}

gfx::NativeDevice CanvasBridge::nativeDevice() const {
    return pass_ && pass_->current() ? pass_->renderer().nativeDevice() : gfx::NativeDevice{};
}

bool CanvasBridge::importImages(const std::array<EyeImageSet, 2>& eyes, std::string& error) {
    if (!pass_) {
        error = "Passet är redan avvecklat.";
        return false;
    }
    if (!pass_->current()) {
        error = "Ritytans fönster stängdes medan VR startade.";
        return false;
    }
    gfx::Renderer& renderer = pass_->renderer();
    const int samples = renderer.supportedSamples(std::clamp(options_.msaaSamples, 1, 8));

    for (std::size_t eye = 0; eye < 2; ++eye) {
        gfx::RenderTarget* shared = nullptr;   // ögats första bild äger de delade ytorna
        sizes_[eye] = {eyes[eye].width, eyes[eye].height};
        for (void* image : eyes[eye].images) {
            gfx::ExternalTargetDesc desc;
            desc.api = gfx::NativeGraphicsApi::D3D11;
            desc.image = image;
            desc.width = eyes[eye].width;
            desc.height = eyes[eye].height;
            desc.color = eyes[eye].bgra ? gfx::TargetColorFormat::Bgra8Unorm : gfx::TargetColorFormat::Rgba8Unorm;
            desc.target.samples = samples;
            desc.target.depth = true;

            std::string importError;
            std::unique_ptr<gfx::RenderTarget> target = renderer.importRenderTarget(desc, shared, importError);
            if (!target || !target->image().valid()) {
                error = "En swapchain-bild gick inte att importera som rit-mål" +
                        (importError.empty() ? std::string(".") : ": " + importError);
                return false;
            }
            if (!shared) shared = target.get();
            targets_[eye].push_back(std::move(target));
        }
    }
    return true;
}

// ------------------------------------------------------------ UI-TRÅDENS API

Anchor CanvasBridge::anchor() const {
    if (pendingAnchor_) return *pendingAnchor_;
    return anchor_ ? *anchor_ : Anchor{};
}

void CanvasBridge::setAnchor(const Anchor& anchor) {
    if (!usable(anchor)) return;
    pendingAnchor_ = anchor;
}

void CanvasBridge::recenter(double yawDegrees) {
    if (!std::isfinite(yawDegrees)) return;
    pendingRecenter_ = yawDegrees;
}

bool CanvasBridge::teleportTo(const Vec3& feet, const std::optional<double>& yawDegrees) {
    if (!std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z)) return false;
    if (yawDegrees && !std::isfinite(*yawDegrees)) return false;
    pendingTeleport_ = PendingTeleport{feet, yawDegrees};
    return true;
}

void CanvasBridge::setLocomotion(const Locomotion& locomotion) {
    options_.locomotion = locomotion;
}

void CanvasBridge::claimHand(Hand hand) {
    claimed_[handIndex(hand)] = true;
}

std::vector<glaze::HitResult> CanvasBridge::castRay(const glaze::Ray3& ray,
                                                    const glaze::HitOptions& options) const {
    return cast(ray, options);
}

std::vector<glaze::HitResult> CanvasBridge::cast(const glaze::Ray3& ray, const glaze::HitOptions& options) const {
    if (!pass_ || !headView_ || !pass_->current()) return {};
    // Där scenen inte träffas kan reservmarken (`Options::groundZ`) ta emot strålen.
    return withGround(pass_->castRay(ray, options, *headView_), ray, options.maxDistance, options_.groundZ);
}

// ----------------------------------------------------------------- BILDRUTAN

FrameResult CanvasBridge::runFrame(const FrameRequest& request, const Callbacks& callbacks) {
    FrameResult result;
    if (!pass_) {
        result.error = "Passet är redan avvecklat.";
        return result;
    }

    if (!anchor_) {
        if (!request.headValid) return result;   // väntar på en giltig huvudpose
        if (!createAnchor(request, result.error)) return result;
    }

    applyRequests(request);
    updateHeadView(request);
    applyLocomotion(request);
    const FrameInfo info = makeFrameInfo(request);

    try {
        if (callbacks.onInput) {
            for (const InputEvent& event : info.events) callbacks.onInput(event, info);
        }
        if (callbacks.onFrame) callbacks.onFrame(info);
    } catch (const std::exception& e) {
        result.error = std::string("En callback kastade ett undantag: ") + e.what();
        return result;
    } catch (...) {
        result.error = "En callback kastade ett undantag.";
        return result;
    }

    if (request.eyes[0].valid && request.eyes[1].valid) {
        result.rendered = renderEyes(request, result.error);
    }
    return result;
}

bool CanvasBridge::createAnchor(const FrameRequest& request, std::string& error) {
    CameraStart start;
    if (!cameraStart(start, error)) return false;
    const auto anchor = makeAnchor(request.head, start.eye, start.yawDegrees);
    if (!anchor) {
        error = "Förankringen i scenen gick inte att räkna ut (ogiltig huvudpose eller kamera).";
        return false;
    }
    anchor_ = *anchor;
    return true;
}

// Startläget ur ritytans kamera: en Camera3D (UpAxis::Z), annars kartkameran.
bool CanvasBridge::cameraStart(CameraStart& start, std::string& error) const {
    if (!canvas_.is3D()) return mapCameraStart(start, error);
    const glaze::Camera3D camera = canvas_.camera3D();
    if (camera.up != glaze::UpAxis::Z) {
        error = "Canvas kamera måste ha UpAxis::Z (x öster, y norr, z upp, en världsenhet per meter).";
        return false;
    }
    const glaze::Point3 eye = camera.eye();
    start.eye = Vec3{eye.x, eye.y, eye.z};
    start.yawDegrees = camera.azimuth;
    return true;
}

// Kartkameran som 3D-vy, genom ritytans strålar (`Canvas::rayAt`): mittstrålen
// ger blicken, och strålen genom ytans överkant ger uppåt på skärmen.
//   Perspektiv (lutad karta): alla strålar utgår från ögat.
//   Ortografiskt (rakt uppifrån): strålarna börjar på närplanet, så ögat står
//   på kartans perspektivavstånd rakt över mittpunkten i planet z = 0 — tre
//   gånger halva synliga höjden.
// Kompassriktningen är blickens vågräta riktning, och rakt uppifrån den som
// uppåt på skärmen pekar åt.
bool CanvasBridge::mapCameraStart(CameraStart& start, std::string& error) const {
    const gfx::Sizef size = canvas_.actualSize();
    if (!(size.w > 0.0f) || !(size.h > 0.0f)) {
        error = "Canvas har ingen storlek än. Starta VR när ritytan visas i fönstret.";
        return false;
    }
    const double midX = static_cast<double>(size.w) * 0.5;
    const std::optional<glaze::Ray3> center = canvas_.rayAt(glaze::Point{midX, static_cast<double>(size.h) * 0.5});
    const std::optional<glaze::Ray3> top = canvas_.rayAt(glaze::Point{midX, 0.0});
    if (!center || !top) {
        error = "Kartkameran går inte att använda som start: Canvas har ingen känd yta än. "
                "Starta VR när ritytan visas i fönstret, eller sätt en Camera3D.";
        return false;
    }

    const auto unit = [](const glaze::Vec3& v) -> std::optional<Vec3> {
        const double n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (!std::isfinite(n) || !(n > 1e-12)) return std::nullopt;
        return Vec3{v.x / n, v.y / n, v.z / n};
    };
    const std::optional<Vec3> forward = unit(center->direction);
    const std::optional<Vec3> topDirection = unit(top->direction);
    if (!forward || !topDirection) {
        error = "Kartkamerans blick går inte att räkna ut.";
        return false;
    }

    const bool perspective = center->origin.x == top->origin.x && center->origin.y == top->origin.y &&
                             center->origin.z == top->origin.z;
    Vec3 up;   // uppåt på skärmen, i världen (inte enhetlig)
    if (perspective) {
        start.eye = Vec3{center->origin.x, center->origin.y, center->origin.z};
        up = Vec3{topDirection->x - forward->x, topDirection->y - forward->y, topDirection->z - forward->z};
    } else {
        if (std::fabs(forward->z) < 1e-9) {
            error = "Kartkameran är ortografisk men tittar inte mot marken.";
            return false;
        }
        const glaze::Point3& o = center->origin;
        const double toGround = -o.z / forward->z;
        const glaze::Point3 mid{o.x + forward->x * toGround, o.y + forward->y * toGround, 0.0};
        const double halfHeight = std::sqrt(std::pow(top->origin.x - o.x, 2) + std::pow(top->origin.y - o.y, 2) +
                                            std::pow(top->origin.z - o.z, 2));
        const double stand = halfHeight / std::tan(gfx::kMapFovRad / 2.0);
        start.eye = Vec3{mid.x - forward->x * stand, mid.y - forward->y * stand, mid.z - forward->z * stand};
        up = Vec3{top->origin.x - o.x, top->origin.y - o.y, top->origin.z - o.z};
    }

    constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
    constexpr double kMinHorizontal = 1e-3;
    Vec3 heading = *forward;
    if (std::hypot(heading.x, heading.y) < kMinHorizontal) heading = up;   // rakt uppifrån: skärmens uppåt
    if (!(std::hypot(heading.x, heading.y) > 1e-9)) {
        error = "Kartkamerans kompassriktning går inte att räkna ut.";
        return false;
    }
    start.yawDegrees = std::atan2(heading.x, heading.y) * kRadToDeg;
    return std::isfinite(start.yawDegrees) && std::isfinite(start.eye.x) && std::isfinite(start.eye.y) &&
           std::isfinite(start.eye.z);
}

// Det appen begärt sedan förra bildrutan. En ny förankring ersätter den
// gamla; `recenter` ställer huvudet där det står i världen nu, vänt åt den
// begärda riktningen, med förankringens skala; `teleportTo` sätter fötterna
// på den begärda punkten på samma sätt som den inbyggda landningen.
void CanvasBridge::applyRequests(const FrameRequest& request) {
    if (pendingAnchor_) {
        anchor_ = *pendingAnchor_;
        pendingAnchor_.reset();
        mover_.cancel();
    }
    if (pendingRecenter_ && request.headValid) {
        if (const auto centered = faceYaw(*anchor_, request.head, *pendingRecenter_)) {
            anchor_ = *centered;
            mover_.cancel();
        }
        pendingRecenter_.reset();
    }
    if (pendingTeleport_ && request.headValid) {
        if (placeFeet(*anchor_, request.head, request.floorHeight, pendingTeleport_->feet,
                      pendingTeleport_->yawDegrees)) {
            mover_.cancel();
        }
        pendingTeleport_.reset();
    }
}

// Plockningens vy: huvudet, med ett synfält som rymmer båda ögonens. Den
// styr skärmstorlekarna och toleranserna i castRay.
void CanvasBridge::updateHeadView(const FrameRequest& request) {
    headView_.reset();
    if (!request.headValid) return;
    const auto world = toWorldPose(*anchor_, request.head);
    if (!world) return;
    gfx::FrustumTangents merged;   // ±1 när ögonens synfält saknas
    const auto left = tangentsOf(request.eyes[0].fov);
    const auto right = tangentsOf(request.eyes[1].fov);
    if (request.eyes[0].valid && request.eyes[1].valid && left && right) {
        merged.left = std::min(left->left, right->left);
        merged.right = std::max(left->right, right->right);
        merged.down = std::min(left->down, right->down);
        merged.up = std::max(left->up, right->up);
    }
    headView_ = viewFrom(*world, merged, sizes_[0], options_, anchor_->scale);
}

void CanvasBridge::applyLocomotion(const FrameRequest& request) {
    LocomotionInput input;
    input.deltaSeconds = request.deltaSeconds;
    input.focused = request.focused;
    input.headValid = request.headValid;
    input.head = request.head;
    input.floorHeight = request.floorHeight;
    input.rayLengthMetres = options_.rayLengthMetres;
    for (std::size_t hand = 0; hand < 2; ++hand) {
        const ControllerSample& in = request.controllers[hand];
        LocomotionHand& out = input.hands[hand];
        out.gripValid = in.grip.valid;
        out.aimValid = in.aim.valid;
        out.grip = in.grip.pose;
        out.aim = in.aim.pose;
        out.stickX = in.stickX;
        out.stickY = in.stickY;
        // Greppet avgörs när det börjar och gäller till släppet: ett som
        // börjar medan handen är tagen drar aldrig världen, ett som redan drar
        // fortsätter.
        const bool squeeze = in.buttons[static_cast<std::size_t>(Button::Squeeze)].pressed;
        if (!squeeze)
            worldSqueeze_[hand] = false;
        else if (!heldSqueeze_[hand])
            worldSqueeze_[hand] = !claimed_[hand];
        heldSqueeze_[hand] = squeeze;
        out.squeeze        = worldSqueeze_[hand];
    }
    claimed_ = {};
    const SegmentCast segment = [this](const glaze::Ray3& ray, double maxDistance) {
        glaze::HitOptions options = options_.hoverOptions;
        options.maxDistance = maxDistance;
        return cast(ray, options);
    };
    mover_.update(input, options_.locomotion, *anchor_, segment);
}

FrameInfo CanvasBridge::makeFrameInfo(const FrameRequest& request) {
    const Anchor& anchor = *anchor_;
    FrameInfo info;
    info.sequence = request.sequence;
    info.deltaSeconds = request.deltaSeconds;
    info.focused = request.focused;
    info.anchor = anchor;
    info.head = toTracked(anchor, sampleOf(request.head, request.headValid));
    for (std::size_t eye = 0; eye < 2; ++eye) {
        info.eyes[eye] = toTracked(anchor, sampleOf(request.eyes[eye].pose, request.eyes[eye].valid));
    }

    for (std::size_t hand = 0; hand < 2; ++hand) {
        const ControllerSample& in = request.controllers[hand];
        Controller& out = info.controllers[hand];
        out.grip = toTracked(anchor, in.grip);
        out.aim = toTracked(anchor, in.aim);
        out.palm = toTracked(anchor, in.palm);
        out.velocityValid = in.velocityValid;
        if (in.velocityValid) {
            out.linearVelocity = toWorldVector(anchor.yawDegrees, anchor.scale, in.linearVelocity);
            out.angularVelocity = toWorldVector(anchor.yawDegrees, 1.0, in.angularVelocity);
        }
        out.buttons = in.buttons;
        out.stickX = in.stickX;
        out.stickY = in.stickY;
        out.padX = in.padX;
        out.padY = in.padY;
        out.profile = in.profile;

        // Strålen ur siktet och vad den träffar. Räckvidden är i meter i
        // rummet, så den skalas med förankringen.
        if (out.aim.pose) {
            const WorldPose& aim = *out.aim.pose;
            glaze::Ray3 ray;
            ray.origin = glaze::Point3{aim.position.x, aim.position.y, aim.position.z};
            ray.direction = glaze::Vec3{aim.forward.x, aim.forward.y, aim.forward.z};
            out.ray = ray;
            if (options_.hover) {
                glaze::HitOptions hitOptions = options_.hoverOptions;
                const double reach = options_.rayLengthMetres * anchor.scale;
                if (reach > 0.0) {
                    hitOptions.maxDistance = hitOptions.maxDistance > 0.0 ? std::min(hitOptions.maxDistance, reach)
                                                                          : reach;
                }
                out.hover = cast(ray, hitOptions);
            }
        }

        // Teleporteringens båge är högerhandens.
        if (hand == 1) {
            out.arc = mover_.arc();
            out.arcHit = mover_.arcHit();
        }

        const HandSample& tracked = request.hands[hand];
        TrackedHand& handOut = info.hands[hand];
        handOut.active = tracked.active;
        handOut.pinch = tracked.pinch;
        handOut.grab = tracked.grab;
        if (tracked.active) {
            for (std::size_t joint = 0; joint < tracked.joints.size(); ++joint) {
                const JointSample& sample = tracked.joints[joint];
                if (!sample.valid) continue;
                const auto world = toWorldPose(anchor, sample.pose);
                if (!world) continue;
                handOut.joints[joint].pose = *world;
                handOut.joints[joint].radius = sample.radius * anchor.scale;
                handOut.joints[joint].valid = true;
            }
        }
    }
    info.events = request.events;
    return info;
}

bool CanvasBridge::renderEyes(const FrameRequest& request, std::string& error) {
    std::array<glaze::detail::ExternalCanvasView, 2> views;
    for (std::size_t eye = 0; eye < 2; ++eye) {
        const EyeView& in = request.eyes[eye];
        const auto world = toWorldPose(*anchor_, in.pose);
        const auto tangents = tangentsOf(in.fov);
        if (!world || !tangents) return false;   // en ogiltig pose: ingen ritning den här bildrutan

        const std::uint32_t index = request.imageIndex[eye];
        if (index >= targets_[eye].size()) {
            error = "Runtimen gav en bild som inte importerats.";
            return false;
        }
        views[eye] = glaze::detail::ExternalCanvasView{
            targets_[eye][index].get(), viewFrom(*world, *tangents, sizes_[eye], options_, anchor_->scale)};
    }
    std::string renderError;
    if (!pass_->render(std::span<const glaze::detail::ExternalCanvasView>(views), renderError)) {
        error = "Canvas kunde inte rita ögonen" +
                (renderError.empty() ? std::string(".") : ": " + renderError);
        return false;
    }
    return true;
}

void CanvasBridge::beginRelease() {
    if (!pass_ || completion_ || !releaseError_.empty()) return;
    // Ett stängt fönster har rivit sin renderare: ingen markör går att lägga,
    // och avvecklingen går felvägen.
    if (!pass_->current()) {
        releaseError_ = "ritytans fönster stängdes medan VR kördes";
        return;
    }
    completion_ = pass_->renderer().insertCompletion(releaseError_);
    if (!completion_ && releaseError_.empty()) releaseError_ = "renderaren gav ingen markör";
}

gfx::GpuCompletionState CanvasBridge::pollRelease(std::string& error) {
    if (!releaseError_.empty()) {
        error = releaseError_;
        return gfx::GpuCompletionState::Failed;
    }
    if (!completion_) return gfx::GpuCompletionState::Complete;
    return completion_->poll(error);
}

void CanvasBridge::release() {
    completion_.reset();
    for (auto& eye : targets_) {
        while (!eye.empty()) eye.pop_back();
    }
    pass_.reset();
}

} // namespace glaze::openxr::detail
