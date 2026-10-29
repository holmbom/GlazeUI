// En placerings transform — konventionen och
// kontraktet står i transform3d.h och style3d.h.

#include "transform3d.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace glaze::detail {

namespace {

double radianer(float grader)
{
    return static_cast<double>(grader) * std::numbers::pi / 180.0;
}

void mul3(const double (&a)[3][3], const double (&b)[3][3], double (&out)[3][3])
{
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            out[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
}

} // namespace

void Affine3::toRows(float (&rows)[12]) const
{
    for (int i = 0; i < 3; ++i) {
        rows[i * 4 + 0] = static_cast<float>(m[i][0]);
        rows[i * 4 + 1] = static_cast<float>(m[i][1]);
        rows[i * 4 + 2] = static_cast<float>(m[i][2]);
        rows[i * 4 + 3] = static_cast<float>(t[i]);
    }
}

Affine3 rotationScale(float yawDeg, float pitchDeg, float rollDeg,
                      const Float3& scale, double tx, double ty, double tz)
{
    const double y = radianer(yawDeg), p = radianer(pitchDeg),
                 r = radianer(rollDeg);
    const double cy = std::cos(y), sy = std::sin(y);
    const double cp = std::cos(p), sp = std::sin(p);
    const double cr = std::cos(r), sr = std::sin(r);
    // Rz(−yaw): +y mot bäringen yaw, medurs ovanifrån.
    const double rz[3][3] = {{cy, sy, 0.0}, {-sy, cy, 0.0}, {0.0, 0.0, 1.0}};
    // Rx(pitch): +y uppåt.
    const double rx[3][3] = {{1.0, 0.0, 0.0}, {0.0, cp, -sp}, {0.0, sp, cp}};
    // Ry(roll): +x nedåt.
    const double ry[3][3] = {{cr, 0.0, sr}, {0.0, 1.0, 0.0}, {-sr, 0.0, cr}};
    double zx[3][3], rot[3][3];
    mul3(rz, rx, zx);
    mul3(zx, ry, rot);
    Affine3     a;
    const double s[3] = {scale.x, scale.y, scale.z};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            a.m[i][j] = rot[i][j] * s[j];
    a.t[0] = tx;
    a.t[1] = ty;
    a.t[2] = tz;
    return a;
}

Affine3 compose(const Affine3& a, const Affine3& b)
{
    Affine3 c;
    mul3(a.m, b.m, c.m);
    for (int i = 0; i < 3; ++i)
        c.t[i] = a.m[i][0] * b.t[0] + a.m[i][1] * b.t[1] + a.m[i][2] * b.t[2] +
                 a.t[i];
    return c;
}

bool invert(const Affine3& a, Affine3& out)
{
    const auto& m   = a.m;
    const double c00 = m[1][1] * m[2][2] - m[1][2] * m[2][1];
    const double c01 = m[1][2] * m[2][0] - m[1][0] * m[2][2];
    const double c02 = m[1][0] * m[2][1] - m[1][1] * m[2][0];
    const double det = m[0][0] * c00 + m[0][1] * c01 + m[0][2] * c02;
    if (!(std::abs(det) > 0.0) || !std::isfinite(det))
        return false;
    const double k = 1.0 / det;
    Affine3      inv;
    inv.m[0][0] = c00 * k;
    inv.m[1][0] = c01 * k;
    inv.m[2][0] = c02 * k;
    inv.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * k;
    inv.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * k;
    inv.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * k;
    inv.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * k;
    inv.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * k;
    inv.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * k;
    for (int i = 0; i < 3; ++i)
        inv.t[i] = -(inv.m[i][0] * a.t[0] + inv.m[i][1] * a.t[1] +
                     inv.m[i][2] * a.t[2]);
    out = inv;
    return true;
}

Box3 transformBox(const Affine3& a, const Box3& local)
{
    Box3 out{};
    bool first = true;
    for (int k = 0; k < 8; ++k) {
        const Point3 c{(k & 1) ? local.max.x : local.min.x,
                       (k & 2) ? local.max.y : local.min.y,
                       (k & 4) ? local.max.z : local.min.z};
        const Point3 w = a.apply(c);
        if (first) {
            out.min = out.max = w;
            first = false;
            continue;
        }
        out.min = {std::min(out.min.x, w.x), std::min(out.min.y, w.y),
                   std::min(out.min.z, w.z)};
        out.max = {std::max(out.max.x, w.x), std::max(out.max.y, w.y),
                   std::max(out.max.z, w.z)};
    }
    return out;
}

double distanceToBox(const Point3& p, const Box3& b)
{
    const double dx = std::max({b.min.x - p.x, 0.0, p.x - b.max.x});
    const double dy = std::max({b.min.y - p.y, 0.0, p.y - b.max.y});
    const double dz = std::max({b.min.z - p.z, 0.0, p.z - b.max.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace glaze::detail
