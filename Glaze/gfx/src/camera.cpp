// Ritytans kamera — backend-neutral double-matte.
// Rymderna och konventionerna står i include/gfx/camera.h; här är bara
// kedjan som bygger matrisen och de tre frågorna ovanpå den.

#include "gfx/camera.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gfx {
namespace {

constexpr double kPi = 3.14159265358979323846;

double radians(double deg)
{
    return deg * (kPi / 180.0);
}

// Tilten klampad till kamerans tak. En icke-ändlig tilt är ingen tilt.
double pitchRad(const MapView& v)
{
    const double p = std::isfinite(v.pitchDeg) ? v.pitchDeg : 0.0;
    return radians(std::clamp(p, 0.0, kMaxPitchDeg));
}

// En vy som går att räkna på: en yta med area, en positiv zoom och ändliga
// tal. Allt annat ger nollmatrisen och falska svar.
bool usable(const MapView& v)
{
    return v.box.w > 0.0f && v.box.h > 0.0f && v.pxPerUnit > 0.0 &&
           std::isfinite(v.pxPerUnit) && std::isfinite(v.cx) &&
           std::isfinite(v.cy) && std::isfinite(v.bearingDeg);
}

// Kamerans avstånd till planets mitt i px — det avstånd där en px i planet
// är en px på skärmen (tan(fov/2) = 1/3 ⇒ 1,5 × ytans höjd).
double cameraDistance(double boxH)
{
    return 0.5 * boxH / std::tan(kMapFovRad / 2.0);
}

} // namespace

Mat4d viewProjection(const MapView& v, double originX, double originY)
{
    if (!usable(v))
        return Mat4d{};   // W = 0 överallt — ingenting ritas

    const double k     = v.pxPerUnit;
    const double bw    = v.box.w;
    const double bh    = v.box.h;
    const double d     = cameraDistance(bh);
    const double pitch = pitchRad(v);

    // 1. Chunk-lokalt → relativt kamerans mitt. Den ENDA subtraktionen med
    //    stora tal (SWEREF ~6,6·10⁶), och den görs här i double: det som går
    //    vidare till float är skillnaden mellan två närliggande punkter.
    Mat4d m = Mat4d::translation(originX - v.cx, originY - v.cy, 0.0);

    // 2. Världsenheter → px med skärmens y nedåt. En karta (yUp) speglas i
    //    y — och i z, så basen förblir högerhänt och världens "upp" pekar
    //    mot betraktaren (planet z = 0 påverkas inte, 3D-vyn gör det).
    const double s = v.yUp ? -1.0 : 1.0;
    m = Mat4d::scale(k, s * k, s * k) * m;

    // 3. Bäringen vrider innehållet MOTURS på skärmen — i en rymd med y
    //    nedåt är det en NEGATIV vinkel kring z.
    m = Mat4d::rotationZ(-radians(v.bearingDeg)) * m;

    // 4. Rymden här är x höger, y ned, z bort från betraktaren. Ögonrymden
    //    `ortho`/`perspective` vill ha har y upp och z MOT betraktaren — ett
    //    halvt varv kring x.
    const Mat4d toEye = Mat4d::scale(1.0, -1.0, -1.0);
    if (pitch > 0.0) {
        // Tilten fäller överkanten bort (y < 0 får z > 0), och kameran
        // står på avståndet d — där planets mitt har skalan 1 px/px.
        m = Mat4d::rotationX(-pitch) * m;
        m = Mat4d::translation(0.0, 0.0, d) * m;
        m = toEye * m;
        m = Mat4d::perspective(kMapFovRad, bw / bh, bh / 50.0,
                               std::numeric_limits<double>::infinity()) *
            m;
    } else {
        // Ortografiskt: ingen kamera att stå någonstans. Djupintervallet
        // ±d är 3D-passets utrymme; planet z = 0 hamnar mitt i det.
        m = toEye * m;
        m = Mat4d::ortho(-bw / 2.0, bw / 2.0, -bh / 2.0, bh / 2.0, -d, d) * m;
    }

    // 5. Ytans NDC → målets fysiska px (y nedåt, origo uppe till vänster).
    //    Verkar på de homogena talen, så perspektivdivisionen sker först
    //    hos GPU:n — eller i `project` nedan.
    Mat4d toPx = Mat4d::identity();
    toPx.m[0]  = bw / 2.0;
    toPx.m[5]  = -bh / 2.0;
    toPx.m[12] = static_cast<double>(v.box.x) + bw / 2.0;
    toPx.m[13] = static_cast<double>(v.box.y) + bh / 2.0;
    return toPx * m;
}

bool project(const MapView& v, double wx, double wy, double& px, double& py)
{
    if (!usable(v))
        return false;
    // Origot = kamerans mitt: punkten tas relativt mitten i double, samma
    // väg som en chunk med origot där.
    double c[4];
    viewProjection(v, v.cx, v.cy).transform(wx - v.cx, wy - v.cy, 0.0, 1.0, c);
    if (!(c[3] > 0.0))
        return false;   // bakom kameran
    px = c[0] / c[3];
    py = c[1] / c[3];
    return true;
}

namespace {

// Strålen genom pixeln mot planet, i världsenheter RELATIVT kamerans mitt —
// inga stora tal, så två närliggande träffar kan subtraheras utan att
// SWEREF-koordinaternas storlek äter skillnaden (pxPerUnitAt).
bool unprojectRelative(const MapView& v, double px, double py, double& rx,
                       double& ry)
{
    if (!usable(v))
        return false;
    const Mat4d fwd = viewProjection(v, v.cx, v.cy);
    Mat4d       inv;
    if (!fwd.inverse(inv))
        return false;

    // Två punkter längs pixelns stråle: på närplanet (djup 0) och längre in
    // (djup 0,5). Båda ändliga även med det oändliga bortre planet.
    double a[4], b[4];
    inv.transform(px, py, 0.0, 1.0, a);
    inv.transform(px, py, 0.5, 1.0, b);
    if (a[3] == 0.0 || b[3] == 0.0)
        return false;
    const double ax = a[0] / a[3], ay = a[1] / a[3], az = a[2] / a[3];
    const double bx = b[0] / b[3], by = b[1] / b[3], bz = b[2] / b[3];

    // Linjen genom dem mot planet z = 0.
    const double dz = az - bz;
    if (dz == 0.0)
        return false;   // strålen parallell med planet
    const double t = az / dz;
    if (!std::isfinite(t))
        return false;
    const double hx = ax + t * (bx - ax);
    const double hy = ay + t * (by - ay);

    // Linjen träffar planet åt BÅDA håll; träffen räknas bara om den ligger
    // framför kameran. Ovanför horisonten ligger den bakom — då finns ingen
    // världspunkt under pixeln.
    double c[4];
    fwd.transform(hx, hy, 0.0, 1.0, c);
    if (!(c[3] > 0.0))
        return false;
    rx = hx;
    ry = hy;
    return true;
}

} // namespace

bool unproject(const MapView& v, double px, double py, double& wx,
               double& wy)
{
    double rx, ry;
    if (!unprojectRelative(v, px, py, rx, ry))
        return false;
    wx = v.cx + rx;
    wy = v.cy + ry;
    return true;
}

double pxPerUnitAt(const MapView& v, double px, double py)
{
    // Ett px åt vardera hållet längs bildraden — världsavståndet mellan
    // träffarna är en världsenhet per (1 / skalan) px. Relativt mitten:
    // två absoluta SWEREF-tal 1/zoom isär hade tappat sina sista siffror.
    double x0, y0, x1, y1;
    if (!unprojectRelative(v, px - 0.5, py, x0, y0) ||
        !unprojectRelative(v, px + 0.5, py, x1, y1))
        return 0.0;
    const double dist = std::hypot(x1 - x0, y1 - y0);
    return dist > 0.0 ? 1.0 / dist : 0.0;
}

bool anchorWorldAt(MapView& v, double wx, double wy, double px, double py)
{
    // Världspunkten under pixeln NU. Att flytta mitten förskjuter hela
    // planet lika mycket — inget annat i vyn beror på mitten — så en enda
    // korrigering lägger (wx, wy) exakt under pixeln.
    double ux, uy;
    if (!unproject(v, px, py, ux, uy))
        return false;
    v.cx += wx - ux;
    v.cy += wy - uy;
    return true;
}

} // namespace gfx
