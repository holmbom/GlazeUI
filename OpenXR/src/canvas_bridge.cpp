#include "canvas_bridge.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

#include <glaze/canvas/external_pass.h>

namespace glaze::openxr::detail {

namespace {

constexpr double kRad = 3.14159265358979323846 / 180.0;
// Halvvinklar nära eller över 90° ger ingen perspektivprojektion.
constexpr double kMaxHalfAngle = 3.14159265358979323846 / 2.0 - 1e-3;

double wrapDegrees(double d) {
    d = std::fmod(d, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d <= -180.0) d += 360.0;
    return d;
}

TrackedPose toTracked(const Anchor& anchor, const PoseSample& sample) {
    TrackedPose tracked;
    if (!sample.valid) return tracked;
    tracked.pose = toWorldPose(anchor, sample.pose);
    tracked.positionTracked = sample.positionTracked;
    tracked.orientationTracked = sample.orientationTracked;
    return tracked;
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
// och en ruta som är hela bilden (`size` i px).
gfx::View3D viewFrom(const WorldPose& pose, const gfx::FrustumTangents& frustum,
                     std::pair<int, int> size, const Options& options) {
    gfx::View3D view;
    view.eye = toVec3d(pose.position);
    view.forward = toVec3d(pose.forward);
    view.up = toVec3d(pose.up);
    view.projection = gfx::Projection3D::Perspective;
    view.frustum = frustum;
    view.nearZ = options.nearMetres;
    view.farZ = options.farMetres;
    view.box = gfx::Rectf{0.0f, 0.0f, static_cast<float>(size.first), static_cast<float>(size.second)};
    return view;
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

    applyLocomotion(request);
    const FrameInfo info = makeFrameInfo(request);

    try {
        if (callbacks.onFrame) callbacks.onFrame(info);
        pick(request, info, callbacks);
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
    if (!canvas_.is3D()) {
        error = "Canvas har ingen Camera3D. Sätt en 3D-kamera (setCamera(Camera3D)) innan VR startas.";
        return false;
    }
    const glaze::Camera3D camera = canvas_.camera3D();
    if (camera.up != glaze::UpAxis::Z) {
        error = "Canvas kamera måste ha UpAxis::Z (x öster, y norr, z upp, en världsenhet per meter).";
        return false;
    }
    const glaze::Point3 eye = camera.eye();
    const auto anchor = makeAnchor(request.head, Vec3{eye.x, eye.y, eye.z}, camera.azimuth);
    if (!anchor) {
        error = "Förankringen i scenen gick inte att räkna ut (ogiltig huvudpose eller kamera).";
        return false;
    }
    anchor_ = *anchor;
    headYaw_ = camera.azimuth;
    return true;
}

void CanvasBridge::applyLocomotion(const FrameRequest& request) {
    if (!anchor_ || !request.focused || !request.headValid || request.deltaSeconds <= 0.0) return;
    const ControllerSample& left = request.controllers[0];
    const ControllerSample& right = request.controllers[1];

    // Vridningen: höger spak i sidled vrider förankringen kring huvudet, så
    // huvudet står kvar i världen.
    if (right.stickActive && right.stickX != 0.0) {
        if (const auto head = toWorldPose(*anchor_, request.head)) {
            Anchor turned = *anchor_;
            turned.yawDegrees = wrapDegrees(turned.yawDegrees + options_.turnDegreesPerSecond *
                                                                    request.deltaSeconds * right.stickX);
            const auto pivot = toWorldPose(Anchor{Vec3{}, turned.yawDegrees}, request.head);
            if (pivot) {
                turned.origin = Vec3{head->position.x - pivot->position.x,
                                     head->position.y - pivot->position.y,
                                     head->position.z - pivot->position.z};
                anchor_ = turned;
            }
        }
    }

    // Förflyttningen: vänster spak i huvudets horisontella riktning.
    if (left.stickActive && (left.stickX != 0.0 || left.stickY != 0.0)) {
        if (const auto head = toWorldPose(*anchor_, request.head)) {
            if (const auto angles = cameraAngles(*head, headYaw_)) {
                headYaw_ = angles->yawDegrees;
                const double yaw = angles->yawDegrees * kRad;
                const double distance = options_.moveMetresPerSecond * request.deltaSeconds;
                const double forwardX = std::sin(yaw);
                const double forwardY = std::cos(yaw);
                const double rightX = std::cos(yaw);
                const double rightY = -std::sin(yaw);
                anchor_->origin.x += distance * (forwardX * left.stickY + rightX * left.stickX);
                anchor_->origin.y += distance * (forwardY * left.stickY + rightY * left.stickX);
            }
        }
    }
}

FrameInfo CanvasBridge::makeFrameInfo(const FrameRequest& request) const {
    FrameInfo info;
    info.sequence = request.sequence;
    info.deltaSeconds = request.deltaSeconds;
    info.focused = request.focused;
    info.anchor = *anchor_;
    for (std::size_t hand = 0; hand < 2; ++hand) {
        const ControllerSample& in = request.controllers[hand];
        Controller& out = info.controllers[hand];
        out.grip = toTracked(*anchor_, in.grip);
        out.aim = toTracked(*anchor_, in.aim);
        out.stickX = in.stickX;
        out.stickY = in.stickY;
        out.trigger = in.trigger;
        out.selectDown = in.selectDown;
        out.stickActive = in.stickActive;
    }
    return info;
}

void CanvasBridge::pick(const FrameRequest& request, const FrameInfo& info, const Callbacks& callbacks) {
    // Efter en fokusförlust ger först ett nytt tryck efter ett släpp ett val:
    // en avtryckare som redan är nere när fokus kommer tillbaka väljer inget.
    if (request.focusEpoch != focusEpoch_) {
        focusEpoch_ = request.focusEpoch;
        previousSelect_ = {true, true};
    }

    // Plockningens vy: huvudet, med ett synfält som rymmer båda ögonens. Den
    // styr skärmstorlekarna och toleranserna i castRay.
    std::optional<gfx::View3D> headView;
    const auto buildHeadView = [&]() -> const std::optional<gfx::View3D>& {
        if (headView) return headView;
        const auto world = toWorldPose(*anchor_, request.head);
        if (!world || !request.headValid) return headView;
        gfx::FrustumTangents merged;   // ±1 när ögonens synfält saknas
        const auto left = tangentsOf(request.eyes[0].fov);
        const auto right = tangentsOf(request.eyes[1].fov);
        if (request.eyes[0].valid && request.eyes[1].valid && left && right) {
            merged.left = std::min(left->left, right->left);
            merged.right = std::max(left->right, right->right);
            merged.down = std::min(left->down, right->down);
            merged.up = std::max(left->up, right->up);
        }
        headView = viewFrom(*world, merged, sizes_[0], options_);
        return headView;
    };

    for (std::size_t hand = 0; hand < 2; ++hand) {
        const bool down = request.focused && request.controllers[hand].selectDown;
        const bool pressed = down && !previousSelect_[hand];
        previousSelect_[hand] = down;
        if (!pressed) continue;

        const TrackedPose& aim = info.controllers[hand].aim;
        if (!aim.pose) continue;
        const std::optional<gfx::View3D>& view = buildHeadView();
        if (!view) continue;

        PickEvent event;
        event.hand = hand == 0 ? Hand::Left : Hand::Right;
        event.ray.origin = glaze::Point3{aim.pose->position.x, aim.pose->position.y, aim.pose->position.z};
        event.ray.direction = glaze::Vec3{aim.pose->forward.x, aim.pose->forward.y, aim.pose->forward.z};
        glaze::HitOptions hitOptions;
        hitOptions.occlusion = true;
        hitOptions.maxDistance = options_.rayLengthMetres;
        event.hits = pass_->castRay(event.ray, hitOptions, *view);
        if (callbacks.onPick) callbacks.onPick(event);
    }
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
            targets_[eye][index].get(), viewFrom(*world, *tangents, sizes_[eye], options_)};
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
