#include "hand_logic.h"

#include "hands.h"
#include "vector_ops.h"

#include <algorithm>
#include <cmath>

namespace glaze::openxr::detail {

// ------------------------------------------------------------------ KNAPPAR

bool analogPressed(double value, bool wasPressed) noexcept {
    if (value >= kPressThreshold) return true;
    if (value <= kReleaseThreshold) return false;
    return wasPressed;
}

ButtonState resolveButton(const RawButton& raw, bool wasPressed, bool available) noexcept {
    ButtonState state;
    state.available = available;
    if (!available) return state;
    if (raw.hasClick) {
        state.pressed = raw.click;
    } else if (raw.hasValue) {
        state.pressed = analogPressed(raw.value, wasPressed);
    }
    state.value = raw.hasValue ? std::clamp(raw.value, 0.0, 1.0) : (state.pressed ? 1.0 : 0.0);
    state.touched = raw.hasTouch && raw.touch;
    return state;
}

void applyDeadzone(double& x, double& y, double deadzone) noexcept {
    const double magnitude = std::hypot(x, y);
    if (magnitude <= deadzone || magnitude <= 0.0) {
        x = 0.0;
        y = 0.0;
        return;
    }
    const double span = std::max(1e-6, 1.0 - deadzone);
    const double scaled = std::min(1.0, (magnitude - deadzone) / span);
    x = x / magnitude * scaled;
    y = y / magnitude * scaled;
}

void ButtonTracker::reset() noexcept {
    state_ = {};
    wasFocused_ = {};
}

void ButtonTracker::cut(Hand hand, std::vector<InputEvent>& events) {
    const std::size_t index = handIndex(hand);
    std::array<Per, kButtonCount>& states = state_[index];
    for (std::size_t b = 0; b < kButtonCount; ++b) {
        Per& per = states[b];
        if (per.pressed) events.push_back(InputEvent{hand, static_cast<Button>(b), InputKind::Released});
        if (per.touched) events.push_back(InputEvent{hand, static_cast<Button>(b), InputKind::Untouched});
        per = Per{};
    }
    // Nästa fokuserade bildruta läses som en återkomst: det som redan hålls
    // spärras tills det släpps.
    wasFocused_[index] = false;
}

void ButtonTracker::update(Hand hand, bool focused,
                           const std::array<RawButton, kButtonCount>& raw,
                           const std::array<bool, kButtonCount>& available,
                           std::array<ButtonState, kButtonCount>& out,
                           std::vector<InputEvent>& events) {
    const std::size_t index = handIndex(hand);
    std::array<Per, kButtonCount>& states = state_[index];
    const bool regained = focused && !wasFocused_[index];
    wasFocused_[index] = focused;

    for (std::size_t b = 0; b < kButtonCount; ++b) {
        Per& per = states[b];
        ButtonState next;
        next.available = available[b];
        if (focused && available[b]) next = resolveButton(raw[b], per.rawPressed, true);

        const bool rawPressed = next.pressed;
        const bool rawTouched = next.touched;
        if (regained) {
            per.blockPressed = rawPressed;
            per.blockTouched = rawTouched;
        }
        per.rawPressed = rawPressed;
        per.rawTouched = rawTouched;
        if (per.blockPressed) {
            if (rawPressed) next.pressed = false;
            else per.blockPressed = false;
        }
        if (per.blockTouched) {
            if (rawTouched) next.touched = false;
            else per.blockTouched = false;
        }

        const auto event = [&](InputKind kind) {
            events.push_back(InputEvent{hand, static_cast<Button>(b), kind});
        };
        if (next.touched && !per.touched) event(InputKind::Touched);
        if (next.pressed && !per.pressed) event(InputKind::Pressed);
        if (!next.pressed && per.pressed) event(InputKind::Released);
        if (!next.touched && per.touched) event(InputKind::Untouched);
        per.pressed = next.pressed;
        per.touched = next.touched;
        out[b] = next;
    }
}

// ------------------------------------------------------------ HÄRLETT SIKTE

namespace {

// Ledens plats, när den är giltig och ändlig.
std::optional<Vec3> jointPosition(const HandSample& hand, int joint) noexcept {
    const JointSample& j = hand.joints[static_cast<std::size_t>(joint)];
    if (!j.valid || !allFinite(j.pose.position)) return std::nullopt;
    return j.pose.position;
}

} // namespace

Vec3 rotateByQuaternion(const Quaternion& q, Vec3 v) noexcept {
    return transformDirection(Pose{Vec3{}, q}, v);
}

std::optional<Quaternion> orientationLookingAlong(Vec3 forward) noexcept {
    if (!allFinite(forward)) return std::nullopt;
    const Vec3 f = vnormalized(forward);
    if (vlength(f) < 0.5) return std::nullopt;
    Vec3 right = vcross(f, Vec3{0.0, 1.0, 0.0});
    if (vlength(right) < 1e-3) right = vcross(f, Vec3{0.0, 0.0, -1.0});
    right = vnormalized(right);

    // Rotationsmatrisen har kolumnerna right, up, back: i `toPose` (Glazes
    // lokala axlar x, y, z) blir de höger, framåt och upp.
    WorldPose axes;
    axes.right = right;
    axes.forward = vcross(right, f);
    axes.up = vmul(f, -1.0);
    return toPose(axes).orientation;
}

RawAim deriveAim(const HandSample& hand, bool leftHand, const TrackingPose* head) noexcept {
    if (!hand.active || !head || !allFinite(head->position) || !allFinite(head->orientation)) return {};

    // Huvudets högra axel i det vågräta planet; ett huvud rullat på sidan
    // (axeln nästan lodrät) ger en antagen blick längs −z.
    const Vec3 headRight = rotateByQuaternion(head->orientation, Vec3{1.0, 0.0, 0.0});
    Vec3 side{headRight.x, 0.0, headRight.z};
    side = vlength(side) < 0.3 ? Vec3{1.0, 0.0, 0.0} : vnormalized(side);
    const double sign = leftHand ? -1.0 : 1.0;
    const Vec3 shoulder = vadd(vadd(head->position, Vec3{0.0, -kShoulderDropMetres, 0.0}),
                               vmul(side, sign * kShoulderSideMetres));

    std::optional<Vec3> through = jointPosition(hand, kJointIndexProximal);
    if (!through) through = jointPosition(hand, kJointWrist);
    if (!through) return {};

    const Vec3 direction = vsub(*through, shoulder);
    const double length = vlength(direction);
    if (!(length > 0.05) || !std::isfinite(length)) return {};

    RawAim aim;
    aim.valid = true;
    aim.position = *through;
    aim.forward = vmul(direction, 1.0 / length);
    return aim;
}

RawAim AimSmoother::step(const RawAim& raw, double dt) noexcept {
    if (!raw.valid || !allFinite(raw.position) || !allFinite(raw.forward)) {
        has_ = false;
        return {};
    }
    if (!has_) {
        last_ = raw;
        has_ = true;
        return last_;
    }
    const double alpha = (std::isfinite(dt) && dt > 0.0) ? 1.0 - std::exp(-dt / kAimSmoothingSeconds) : 0.0;
    last_.position = vadd(last_.position, vmul(vsub(raw.position, last_.position), alpha));
    const Vec3 blended = vadd(vmul(last_.forward, 1.0 - alpha), vmul(raw.forward, alpha));
    // Motsatta riktningar tar ut varandra: ta då det nya.
    last_.forward = vlength(blended) > 1e-6 ? vnormalized(blended) : raw.forward;
    last_.valid = true;
    return last_;
}

std::optional<TrackingPose> aimPoseOf(const RawAim& aim) noexcept {
    if (!aim.valid) return std::nullopt;
    const auto orientation = orientationLookingAlong(aim.forward);
    if (!orientation) return std::nullopt;
    return TrackingPose{aim.position, *orientation};
}

std::optional<Vec3> pinchPointOf(const HandSample& hand) noexcept {
    const auto thumb = jointPosition(hand, kJointThumbTip);
    const auto index = jointPosition(hand, kJointIndexTip);
    if (!hand.active || !thumb || !index) return std::nullopt;
    return vmul(vadd(*thumb, *index), 0.5);
}

std::optional<Vec3> pokePointOf(const HandSample& hand) noexcept {
    if (!hand.active) return std::nullopt;
    return jointPosition(hand, kJointIndexTip);
}

// ------------------------------------------------------------ TRYCKVÄRDEN

double pinchStrengthOf(const HandSample& hand) noexcept {
    return hand.aimValid && !hand.systemGesture ? unitValue(hand.aimPinch) : unitValue(hand.pinch);
}

double graspStrengthOf(const HandSample& hand) noexcept { return unitValue(hand.grab); }

double gateByReady(double value, bool hasReady, bool ready) noexcept {
    return hasReady && !ready ? 0.0 : value;
}

// ------------------------------------------------------------------ KÄLLAN

double HandSource::trigger(const HandInputs& in) noexcept {
    const double a = unitValue(in.triggerAim);
    const double p = unitValue(in.triggerPinch);
    const double top = std::max(a, p);
    if (waitLow_) {
        if (top <= kReleaseThreshold) waitLow_ = false;
        return waitLow_ ? 0.0 : top;
    }
    if (!pressed_) {
        if (top >= kPressThreshold) {
            pressed_ = true;
            viaPinch_ = p > a;
        }
        return top;
    }
    const double value = viaPinch_ ? p : a;
    if (value <= kReleaseThreshold) {
        pressed_ = false;
        waitLow_ = top > kReleaseThreshold;
        return waitLow_ ? 0.0 : value;
    }
    return value;
}

HandOutput HandSource::update(double dt, const HandInputs& in) noexcept {
    using Source = Controller::Source;
    Source want = Source::None;
    if (in.controllerProfile && in.controllerPose) want = Source::Controller;
    else if (in.handAim && !in.suspended) want = Source::Hand;

    Source next = want;
    if (source_ == Source::Controller && want != Source::Controller) {
        // En kontrollprofil som fortfarande är aktiv men tappat sin pose får
        // en kort frist; en profil som lämnat gör det inte.
        if (in.controllerProfile) {
            if (std::isfinite(dt) && dt > 0.0) lostSeconds_ += dt;
            if (lostSeconds_ < kControllerHoldSeconds) next = Source::Controller;
        }
    } else {
        lostSeconds_ = 0.0;
    }

    HandOutput out;
    if (next != source_) {
        source_ = next;
        ++epoch_;
        lostSeconds_ = 0.0;
        pressed_ = false;
        waitLow_ = false;
        out.cut = true;
    }
    out.source = source_;
    out.epoch = epoch_;
    if (source_ == Source::Hand) {
        out.trigger = trigger(in);
        out.squeeze = unitValue(in.squeeze);
    }
    return out;
}

} // namespace glaze::openxr::detail
