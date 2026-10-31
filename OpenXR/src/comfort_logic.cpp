#include "comfort_logic.h"

#include "vector_ops.h"

#include <algorithm>
#include <cmath>

namespace glaze::openxr::detail {

namespace {

// En stegvis vridning håller vinjetten uppe så här länge.
constexpr double kSnapPulseSeconds = 0.2;

// Så länge väntar svarttoningen på att den svarta bildrutan lämnas in innan
// den flyttar ändå.
constexpr double kBlackTimeoutSeconds = 0.5;

// Kantens minsta bredd (radianer), så att kanten alltid har två skilda gränser.
constexpr double kMinFeatherRadians = 1e-3;

bool finite(double v) { return std::isfinite(v); }

// `value` i förhållande till `full`, klampat till 0 … 1. Icke-ändliga tal och
// ett `full` som inte är positivt ger 0.
double fraction(double value, double full) {
    if (!finite(value) || !finite(full) || !(full > 0.0) || !(value > 0.0)) return 0.0;
    return std::min(1.0, value / full);
}

// Hur stor del av hela spannet 0 … 1 en bildruta på `dt` rör sig när hela
// spannet tar `seconds`. En tid som inte är positiv och ändlig tar hela
// spannet på en gång.
double stepOf(double seconds, double dt) {
    if (!finite(seconds) || !(seconds > 1e-6)) return 1.0;
    return dt / seconds;
}

} // namespace

// ------------------------------------------------------------------ VINJETT

MotionSample measureMotion(const Anchor& before, const Anchor& after, const TrackingPose& head,
                           double deltaSeconds, bool snap) {
    MotionSample sample;
    if (!finite(deltaSeconds) || !(deltaSeconds > 0.0)) return sample;
    if (!(before.scale > 0.0) || !(after.scale > 0.0) || !finite(before.scale) || !finite(after.scale) ||
        !finite(before.yawDegrees) || !finite(after.yawDegrees)) {
        return sample;
    }
    const auto from = toWorldPose(before, head);
    const auto to = toWorldPose(after, head);
    if (!from || !to) return sample;

    const double metres = vlength(vsub(to->position, from->position)) / after.scale;
    const double turn = std::fabs(wrapDegrees(after.yawDegrees - before.yawDegrees));
    const double scaleRate = std::fabs(std::log(after.scale / before.scale)) / deltaSeconds;
    if (!finite(metres) || !finite(turn) || !finite(scaleRate)) return sample;

    sample.deltaSeconds = deltaSeconds;
    sample.metresPerSecond = metres / deltaSeconds;
    sample.scaleRatePerSecond = scaleRate;
    if (snap)
        sample.snapDegrees = turn;
    else
        sample.degreesPerSecond = turn / deltaSeconds;
    return sample;
}

void Vignette::reset() noexcept {
    strength_ = 0.0;
    pulseGoal_ = 0.0;
    pulseLeft_ = 0.0;
}

double Vignette::update(const MotionSample& motion, const Comfort& comfort) {
    const double dt = motion.deltaSeconds;
    if (!finite(dt) || !(dt > 0.0)) return strength_;

    double goal = std::max({fraction(motion.metresPerSecond, comfort.fullMoveMetresPerSecond),
                            fraction(motion.degreesPerSecond, comfort.fullTurnDegreesPerSecond),
                            fraction(motion.scaleRatePerSecond, comfort.fullScaleRatePerSecond)});

    // Ett steg är en puls: den håller sitt mål en kort stund.
    const double snapGoal = fraction(motion.snapDegrees, comfort.fullTurnDegreesPerSecond);
    if (snapGoal > 0.0) {
        pulseGoal_ = std::max(pulseLeft_ > 0.0 ? pulseGoal_ : 0.0, snapGoal);
        pulseLeft_ = kSnapPulseSeconds;
    }
    if (pulseLeft_ > 0.0) {
        goal = std::max(goal, pulseGoal_);
        pulseLeft_ -= dt;
        if (pulseLeft_ <= 0.0) {
            pulseLeft_ = 0.0;
            pulseGoal_ = 0.0;
        }
    }

    if (goal > strength_)
        strength_ = std::min(goal, strength_ + stepOf(comfort.attackSeconds, dt));
    else
        strength_ = std::max(goal, strength_ - stepOf(comfort.releaseSeconds, dt));
    strength_ = unitValue(strength_);
    return strength_;
}

VignetteShape vignetteShape(const Fov& fov, double strength, const Comfort& comfort) {
    constexpr double kHalfPi = kPi / 2.0;
    VignetteShape shape;
    shape.featherRadians = kMinFeatherRadians;
    shape.innerRadians = kPi;   // ingen vinjett
    const double angles[4] = {fov.left, fov.right, fov.up, fov.down};
    for (const double a : angles) {
        if (!finite(a) || std::fabs(a) >= kHalfPi - 1e-3) return shape;
    }
    // Det bredaste hörnet, mätt från blickens axel.
    const double across = std::max(std::fabs(std::tan(fov.left)), std::fabs(std::tan(fov.right)));
    const double along = std::max(std::fabs(std::tan(fov.up)), std::fabs(std::tan(fov.down)));
    const double widest = std::atan(std::hypot(across, along));

    double goal = widest;
    if (finite(comfort.minFieldDegrees) && comfort.minFieldDegrees >= 0.0)
        goal = std::min(widest, comfort.minFieldDegrees * 0.5 * kRad);
    shape.innerRadians = widest + (goal - widest) * unitValue(strength);
    if (finite(comfort.featherDegrees) && comfort.featherDegrees > 0.0)
        shape.featherRadians = std::max(kMinFeatherRadians, comfort.featherDegrees * kRad);
    return shape;
}

// ------------------------------------------------------------- SVARTTONING

void TeleportFade::request(const Landing& landing) {
    pending_ = landing;
    if (phase_ == Phase::Idle) {
        phase_ = Phase::FadingOut;
        alpha_ = 0.0;
        submitted_ = false;
    } else if (phase_ == Phase::FadingIn) {
        phase_ = Phase::FadingOut;   // tonar ut igen från där den står
        submitted_ = false;
    }
}

void TeleportFade::cancel() noexcept {
    phase_ = Phase::Idle;
    alpha_ = 0.0;
    submitted_ = false;
    blackSeconds_ = 0.0;
    pending_.reset();
}

std::optional<Landing> TeleportFade::flush() noexcept {
    std::optional<Landing> pending = pending_;
    cancel();
    return pending;
}

TeleportFade::Step TeleportFade::advance(double deltaSeconds, bool focused, bool canMove,
                                         const Comfort& comfort) {
    Step step;
    if (!focused) {
        cancel();
        return step;
    }
    const double dt = finite(deltaSeconds) && deltaSeconds > 0.0 ? deltaSeconds : 0.0;

    switch (phase_) {
    case Phase::Idle:
        break;
    case Phase::FadingOut:
        alpha_ = std::min(1.0, alpha_ + stepOf(comfort.fadeOutSeconds, dt));
        if (alpha_ >= 1.0) {
            alpha_ = 1.0;
            phase_ = Phase::Black;
            submitted_ = false;
            blackSeconds_ = 0.0;
        }
        break;
    case Phase::Black: {
        blackSeconds_ += dt;
        const bool timedOut = blackSeconds_ >= kBlackTimeoutSeconds;
        // Utan huvudpose väntar flytten, men aldrig längre än tidsgränsen:
        // då avbryts den och bilden tonar in.
        if ((canMove && submitted_) || timedOut) {
            if (canMove) step.move = pending_;
            pending_.reset();
            submitted_ = false;
            phase_ = Phase::FadingIn;
            alpha_ = std::max(0.0, 1.0 - stepOf(comfort.fadeInSeconds, dt));
            if (alpha_ <= 0.0) {
                alpha_ = 0.0;
                phase_ = Phase::Idle;
            }
        }
        break;
    }
    case Phase::FadingIn:
        alpha_ = std::max(0.0, alpha_ - stepOf(comfort.fadeInSeconds, dt));
        if (alpha_ <= 0.0) {
            alpha_ = 0.0;
            phase_ = Phase::Idle;
        }
        break;
    }
    step.alpha = alpha_;
    return step;
}

void TeleportFade::blackSubmitted() noexcept {
    if (phase_ == Phase::Black && alpha_ >= 1.0) submitted_ = true;
}

} // namespace glaze::openxr::detail
