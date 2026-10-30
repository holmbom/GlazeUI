#pragma once

// Vektorräkningen som paketets källfiler delar. Ren std, så kärnan
// (`glaze_openxr`) och sessionsdelen använder samma.

#include <cmath>
#include <numbers>

#include <glaze_openxr/math.h>

namespace glaze::openxr::detail {

inline constexpr double kPi = std::numbers::pi;

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

} // namespace glaze::openxr::detail
