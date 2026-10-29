// Mat4d — backend-neutral 4×4 i double. Konventionen
// (kolumnordning, kolumnvektorer, högerhänt med djupet i [0, 1]) står i
// include/gfx/mat4.h och är kontraktet mot de tre shaderspråken.

#include "gfx/mat4.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gfx {

Mat4d Mat4d::identity()
{
    Mat4d r;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0;
    return r;
}

Mat4d Mat4d::translation(double x, double y, double z)
{
    Mat4d r = identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
}

Mat4d Mat4d::scale(double x, double y, double z)
{
    Mat4d r;
    r.m[0]  = x;
    r.m[5]  = y;
    r.m[10] = z;
    r.m[15] = 1.0;
    return r;
}

Mat4d Mat4d::rotationX(double rad)
{
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    Mat4d r = identity();
    r.m[5]  = c;    // (1,1)
    r.m[6]  = s;    // (2,1) — rad 2, kolumn 1
    r.m[9]  = -s;   // (1,2)
    r.m[10] = c;    // (2,2)
    return r;
}

Mat4d Mat4d::rotationY(double rad)
{
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    Mat4d r = identity();
    r.m[0]  = c;    // (0,0)
    r.m[2]  = -s;   // (2,0) — rad 2, kolumn 0
    r.m[8]  = s;    // (0,2)
    r.m[10] = c;    // (2,2)
    return r;
}

Mat4d Mat4d::rotationZ(double rad)
{
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    Mat4d r = identity();
    r.m[0] = c;    // (0,0)
    r.m[1] = s;    // (1,0)
    r.m[4] = -s;   // (0,1)
    r.m[5] = c;    // (1,1)
    return r;
}

Mat4d Mat4d::lookAt(double ex, double ey, double ez, double tx, double ty,
                    double tz, double ux, double uy, double uz)
{
    // Blicken f, höger s = f × upp och den räta uppåtriktningen v = s × f —
    // ögonrymdens tre axlar som RADER (x = s, y = v, z = −f), så att matrisen
    // är basbytet efter flytten av ögat till origo.
    double fx = tx - ex, fy = ty - ey, fz = tz - ez;
    const double fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (!(fl > 0.0) || !std::isfinite(fl))
        return Mat4d{};
    fx /= fl;
    fy /= fl;
    fz /= fl;
    double sx = fy * uz - fz * uy;
    double sy = fz * ux - fx * uz;
    double sz = fx * uy - fy * ux;
    const double sl = std::sqrt(sx * sx + sy * sy + sz * sz);
    const double ul = std::sqrt(ux * ux + uy * uy + uz * uz);
    // Parallell när kryssprodukten är noll i flyttalens upplösning, räknat
    // relativt upp-vektorns längd (den behöver inte vara enhetlig).
    if (!(sl > ul * 1e-12) || !std::isfinite(sl))
        return Mat4d{};
    sx /= sl;
    sy /= sl;
    sz /= sl;
    const double vx = sy * fz - sz * fy;
    const double vy = sz * fx - sx * fz;
    const double vz = sx * fy - sy * fx;

    Mat4d m;
    m.m[0]  = sx;    // rad 0 = s
    m.m[4]  = sy;
    m.m[8]  = sz;
    m.m[1]  = vx;    // rad 1 = v
    m.m[5]  = vy;
    m.m[9]  = vz;
    m.m[2]  = -fx;   // rad 2 = −f
    m.m[6]  = -fy;
    m.m[10] = -fz;
    m.m[12] = -(sx * ex + sy * ey + sz * ez);
    m.m[13] = -(vx * ex + vy * ey + vz * ez);
    m.m[14] = fx * ex + fy * ey + fz * ez;
    m.m[15] = 1.0;
    return m;
}

Mat4d Mat4d::ortho(double l, double r, double b, double t, double n, double f)
{
    Mat4d o;
    o.m[0]  = 2.0 / (r - l);
    o.m[5]  = 2.0 / (t - b);
    o.m[10] = -1.0 / (f - n);
    o.m[12] = -(r + l) / (r - l);
    o.m[13] = -(t + b) / (t - b);
    o.m[14] = -n / (f - n);
    o.m[15] = 1.0;
    return o;
}

Mat4d Mat4d::perspective(double fovY, double aspect, double n, double f)
{
    const double t = std::tan(fovY / 2.0);
    Mat4d p;
    p.m[0]  = 1.0 / (aspect * t);
    p.m[5]  = 1.0 / t;
    p.m[11] = -1.0;   // w = avståndet framför kameran (−z i ögonrymden)
    if (std::isinf(f)) {
        // Gränsvärdet f → ∞ av raden nedan: djupet 1 − n/w, klipps aldrig.
        p.m[10] = -1.0;
        p.m[14] = -n;
    } else {
        p.m[10] = f / (n - f);
        p.m[14] = -(f * n) / (f - n);
    }
    return p;
}

Mat4d Mat4d::operator*(const Mat4d& o) const
{
    Mat4d r;
    for (int k = 0; k < 4; ++k)          // kolumn i resultatet
        for (int row = 0; row < 4; ++row) {
            double s = 0.0;
            for (int i = 0; i < 4; ++i)
                s += m[i * 4 + row] * o.m[k * 4 + i];
            r.m[k * 4 + row] = s;
        }
    return r;
}

// Gauss–Jordan med partiell pivotering, på rader (a[rad][kolumn]). Kortare
// än kofaktorformen och stabilare för kamerans matriser, där elementen
// spänner över flera storleksordningar (zoomen, kamerans avstånd i px).
bool Mat4d::inverse(Mat4d& out) const
{
    double a[4][8];
    double norm = 0.0;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            a[r][c]     = m[c * 4 + r];
            a[r][c + 4] = r == c ? 1.0 : 0.0;
            norm        = std::max(norm, std::abs(a[r][c]));
        }
    if (!(norm > 0.0) || !std::isfinite(norm))
        return false;

    for (int c = 0; c < 4; ++c) {
        int piv = c;
        for (int r = c + 1; r < 4; ++r)
            if (std::abs(a[r][c]) > std::abs(a[piv][c]))
                piv = r;
        // Relativt mot matrisens största element: en pivot som är noll i
        // flyttalens upplösning gör matrisen singulär i praktiken.
        if (std::abs(a[piv][c]) <= norm * 1e-14)
            return false;
        if (piv != c)
            for (int k = 0; k < 8; ++k)
                std::swap(a[c][k], a[piv][k]);
        const double inv = 1.0 / a[c][c];
        for (int k = 0; k < 8; ++k)
            a[c][k] *= inv;
        for (int r = 0; r < 4; ++r) {
            if (r == c)
                continue;
            const double f = a[r][c];
            if (f == 0.0)
                continue;
            for (int k = 0; k < 8; ++k)
                a[r][k] -= f * a[c][k];
        }
    }

    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            out.m[c * 4 + r] = a[r][c + 4];
    return true;
}

void Mat4d::toFloat(float out[16]) const
{
    for (int i = 0; i < 16; ++i)
        out[i] = static_cast<float>(m[i]);
}

void Mat4d::transform(double x, double y, double z, double w,
                      double out[4]) const
{
    for (int row = 0; row < 4; ++row)
        out[row] = m[row] * x + m[4 + row] * y + m[8 + row] * z +
                   m[12 + row] * w;
}

} // namespace gfx
