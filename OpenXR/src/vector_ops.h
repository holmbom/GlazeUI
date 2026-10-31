#pragma once

// Vektor- och vinkelräkningen som paketets källfiler delar. Ren std, så
// kärnan (`glaze_openxr`) och sessionsdelen använder samma.

#include <algorithm>
#include <cmath>
#include <numbers>

#include <glaze_openxr/math.h>

namespace glaze::openxr::detail {

inline constexpr double kPi  = std::numbers::pi;
inline constexpr double kRad = kPi / 180.0;   // grader → radianer

inline Vec3 vadd(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 vsub(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 vmul(Vec3 a, double s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
inline double vdot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 vcross(Vec3 a, Vec3 b) noexcept
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double vlength(Vec3 a) noexcept { return std::sqrt(vdot(a, a)); }
// Nollvektorn (och en nästan noll) förblir noll.
inline Vec3 vnormalized(Vec3 a) noexcept
{
    const double len = vlength(a);
    return len > 1e-12 ? vmul(a, 1.0 / len) : Vec3{};
}

inline bool allFinite(Vec3 a) noexcept
{
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}
inline bool allFinite(const Quaternion& q) noexcept
{
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

// Vinkeln i (−180, 180].
inline double wrapDegrees(double d) noexcept
{
    d = std::fmod(d, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d <= -180.0) d += 360.0;
    return d;
}

// Värdet begränsat till 0 … 1; icke-ändligt blir 0.
inline double unitValue(double value) noexcept
{
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
}

} // namespace glaze::openxr::detail
