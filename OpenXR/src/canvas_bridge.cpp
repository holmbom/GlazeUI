#include "canvas_bridge.h"

#include <cmath>
#include <cstring>
#include <exception>

namespace glaze::openxr::detail {

namespace {

constexpr double kRad = 3.14159265358979323846 / 180.0;

// Bildrutor i följd utan bild som tolereras innan Canvas anses oförmöget att
// rita (renderToBitmap kan lämna tomt medan bildcachen laddar upp).
constexpr int kMaxEmptyFrames = 60;

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

} // namespace

CanvasBridge::CanvasBridge(glaze::Canvas& canvas, const Options& options)
    : canvas_(canvas), options_(options) {}

FrameResult CanvasBridge::runFrame(const FrameRequest& request, const Callbacks& callbacks) {
    FrameResult result;

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
        result.hasImages = renderEyes(request, result);
        if (!result.hasImages) {
            result.images = {};
            if (++emptyFrames_ > kMaxEmptyFrames) {
                result.error = "Canvas ger ingen bild (renderToBitmap lämnade tomt "
                               "i följd; kräver ett öppet fönster och en giltig kamera).";
            }
        } else {
            emptyFrames_ = 0;
        }
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
    eyeYaw_ = {camera.azimuth, camera.azimuth};
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
    for (std::size_t hand = 0; hand < 2; ++hand) {
        const bool down = request.focused && request.controllers[hand].selectDown;
        const bool pressed = down && !previousSelect_[hand];
        previousSelect_[hand] = down;
        if (!pressed) continue;

        const TrackedPose& aim = info.controllers[hand].aim;
        if (!aim.pose) continue;

        PickEvent event;
        event.hand = hand == 0 ? Hand::Left : Hand::Right;
        event.ray.origin = glaze::Point3{aim.pose->position.x, aim.pose->position.y, aim.pose->position.z};
        event.ray.direction = glaze::Vec3{aim.pose->forward.x, aim.pose->forward.y, aim.pose->forward.z};
        glaze::HitOptions hitOptions;
        hitOptions.occlusion = true;
        hitOptions.maxDistance = options_.rayLengthMetres;
        event.hits = canvas_.castRay(event.ray, hitOptions);
        if (callbacks.onPick) callbacks.onPick(event);
    }
}

bool CanvasBridge::renderEyes(const FrameRequest& request, FrameResult& result) {
    for (std::size_t eye = 0; eye < 2; ++eye) {
        const EyeRequest& in = request.eyes[eye];
        const auto world = toWorldPose(*anchor_, in.pose);
        if (!world) return false;
        const auto angles = cameraAngles(*world, eyeYaw_[eye]);
        if (!angles) return false;
        eyeYaw_[eye] = angles->yawDegrees;

        glaze::Camera3D camera = glaze::Camera3D::lookFrom(
            glaze::Point3{world->position.x, world->position.y, world->position.z},
            static_cast<float>(angles->yawDegrees), static_cast<float>(angles->pitchDegrees),
            in.view.verticalFovDegrees, glaze::UpAxis::Z);
        camera.roll = static_cast<float>(angles->rollDegrees);
        camera.nearPlane = options_.nearMetres;
        camera.farPlane = options_.farMetres;

        const int width = static_cast<int>(in.view.width);
        const int height = static_cast<int>(in.view.height);
        const std::optional<glaze::Bitmap> bitmap = canvas_.renderToBitmap(width, height, camera);
        if (!bitmap || !bitmap->valid() || bitmap->width() != width || bitmap->height() != height ||
            bitmap->pixels() == nullptr) {
            return false;
        }
        // Egna pixlar: Bitmap är ett handtag som bara UI-tråden får röra.
        EyeImage& image = result.images[eye];
        image.width = width;
        image.height = height;
        image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
        std::memcpy(image.pixels.data(), bitmap->pixels(), image.pixels.size());
    }
    return true;
}

} // namespace glaze::openxr::detail
