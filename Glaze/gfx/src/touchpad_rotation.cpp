// Styrplattans vridning ur råa kontakter — se touchpad_rotation.h.

#include "touchpad_rotation.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gfx::touchpad {

bool angleDeg(const Contact& a, const Contact& b, double& deg)
{
    const double dx = b.x - a.x, dy = b.y - a.y;
    if (!(std::hypot(dx, dy) > 0.0) || !std::isfinite(dx) || !std::isfinite(dy))
        return false;
    deg = std::atan2(dy, dx) * 180.0 / std::numbers::pi;
    return true;
}

double catchUp(double step, double& debt)
{
    const double extra =
        std::copysign(std::min(std::abs(debt), kCatchUp * std::abs(step)), debt);
    debt -= extra;
    return step + extra;
}

double wrapDeltaDeg(double a, double b)
{
    double d = std::fmod(b - a, 360.0);
    if (d <= -180.0)
        d += 360.0;
    if (d > 180.0)
        d -= 360.0;
    return d;
}

// -------------------------------------------------------------- ramarna

bool FrameAssembler::add(int contactCount, std::span<const Contact> slots)
{
    if (contactCount > 0) {
        // En ny ram börjar — också mitt i en som aldrig blev komplett (en
        // tappad fortsättningsrapport): den gamla kastas hellre än blandas.
        expected_ = contactCount;
        collecting_.clear();
    } else if (expected_ == 0) {
        // Räknaren 0 och ingen påbörjad ram: en ram UTAN kontakter.
        frame_.clear();
        return true;
    }
    const std::size_t saknas =
        static_cast<std::size_t>(expected_) - collecting_.size();
    const std::size_t tas = std::min(saknas, slots.size());
    collecting_.insert(collecting_.end(), slots.begin(),
                       slots.begin() + static_cast<std::ptrdiff_t>(tas));
    if (collecting_.size() < static_cast<std::size_t>(expected_))
        return false;
    frame_.swap(collecting_);
    collecting_.clear();
    expected_ = 0;
    return true;
}

void FrameAssembler::reset()
{
    collecting_.clear();
    frame_.clear();
    expected_ = 0;
}

// EXAKT två fingrar (tip OCH confidence — en handflata räknas inte), det
// med lägre id först; false för allt annat.
static bool tvaFingrar(std::span<const Contact> frame, const Contact*& a,
                       const Contact*& b)
{
    const Contact* par[2] = {nullptr, nullptr};
    int            n      = 0;
    for (const Contact& c : frame) {
        if (!c.tip || !c.confidence)
            continue;
        if (n < 2)
            par[n] = &c;
        ++n;
    }
    if (n != 2)
        return false;
    a = par[0];
    b = par[1];
    if (b->id < a->id)
        std::swap(a, b);
    return true;
}

// ----------------------------------------------------------- klassaren

ClassifyStep TwoFingerClassifier::update(std::span<const Contact> frame)
{
    ClassifyStep   steg;
    const Contact* a = nullptr;
    const Contact* b = nullptr;
    if (!tvaFingrar(frame, a, b)) {
        steg.ended = mode_ != TwoFingerMode::None;
        reset();
        return steg;
    }
    const double cx = (a->x + b->x) / 2.0, cy = (a->y + b->y) / 2.0;
    const double d  = std::hypot(b->x - a->x, b->y - a->y);
    double       vinkel = 0.0;
    if (mode_ == TwoFingerMode::None || a->id != idA_ || b->id != idB_) {
        // NY BASLINJE: en ny beröring, eller ett finger byttes.
        steg.ended = mode_ != TwoFingerMode::None;
        if (!angleDeg(*a, *b, vinkel)) {
            reset();
            return steg;
        }
        mode_ = TwoFingerMode::Undecided;
        idA_  = a->id;
        idB_  = b->id;
        cx0_  = cx;
        cy0_  = cy;
        d0_   = d;
        a0_   = vinkel;
        steg.mode = mode_;
        return steg;
    }
    steg.mode = mode_;
    if (mode_ != TwoFingerMode::Undecided || !angleDeg(*a, *b, vinkel))
        return steg;   // LÅST — eller punkterna sammanföll den här ramen
    const double pan  = std::hypot(cx - cx0_, cy - cy0_);
    const double zoom = std::abs(d - d0_) / 2.0;
    const double vrid = std::abs(wrapDeltaDeg(a0_, vinkel)) *
                        std::numbers::pi / 180.0 * (d0_ + d) / 4.0;
    const double störst = std::max({pan, zoom, vrid});
    if (!(störst >= kDecideFraction * d0_))
        return steg;   // inte tillräckligt än
    mode_        = pan >= kPanDominance * std::max(zoom, vrid) ? TwoFingerMode::Pan
                   : vrid >= kRotateDominance * zoom          ? TwoFingerMode::Rotate
                                                              : TwoFingerMode::Zoom;
    steg.mode    = mode_;
    steg.decided = true;
    return steg;
}

void TwoFingerClassifier::reset()
{
    mode_ = TwoFingerMode::None;
    idA_ = idB_ = 0;
    cx0_ = cy0_ = d0_ = a0_ = 0.0;
}

// ------------------------------------------------------------ spåraren

RotationStep RotationTracker::update(std::span<const Contact> frame)
{
    RotationStep   steg;
    const Contact* a = nullptr;
    const Contact* b = nullptr;
    if (!tvaFingrar(frame, a, b)) {
        steg.ended = gripped_;
        reset();
        return steg;
    }

    double grader = 0.0;
    if (!haveBase_ || a->id != idA_ || b->id != idB_) {
        // NY BASLINJE: ett finger kom eller gick. Vridningen börjar om från
        // tröskeln, och en som var gripen tar slut här.
        steg.ended = gripped_;
        reset();
        if (angleDeg(*a, *b, grader)) {
            haveBase_ = true;
            idA_      = a->id;
            idB_      = b->id;
            lastDeg_  = grader;
        }
        return steg;
    }
    if (!angleDeg(*a, *b, grader))
        return steg;   // punkterna sammanföll — ingen riktning den här ramen

    total_ += wrapDeltaDeg(lastDeg_, grader);
    lastDeg_ = grader;
    if (!gripped_ && std::abs(total_) >= kGripDeg) {
        gripped_   = true;   // GREPP — sekvensen börjar i den här ramen
        steg.began = true;
    }
    // Gripen följer kartan fingrarnas TOTALA vridning sedan baslinjen —
    // greppets ram hinner ikapp (högst tröskelns grader, knappt synligt), och
    // när fingrarna vridits tillbaka står kartan EXAKT där den började, så
    // att vridsnäppet fångar (det som samlats före greppet levereras, så
    // kartan hamnar inte tröskelns grader fel på vägen tillbaka).
    if (gripped_) {
        steg.degrees = total_ - delivered_;
        delivered_   = total_;
    }
    return steg;
}

void RotationTracker::reset()
{
    haveBase_    = false;
    idA_         = 0;
    idB_         = 0;
    lastDeg_     = 0.0;
    total_       = 0.0;
    delivered_   = 0.0;
    gripped_     = false;
}

} // namespace gfx::touchpad
