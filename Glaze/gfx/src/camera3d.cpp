// Ritytans 3D-vy — backend-neutral double-matte.
// Rymderna och konventionerna står i include/gfx/camera.h: samma pixelklipp
// som kartkamerans (camera.cpp), med ett öga, en blick och en upp-vektor i
// stället för bäring och tilt.

#include "gfx/camera.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace gfx {
namespace {

// Det bortre planet i perspektiv får MapLibres marginal: furthest distance
// × 1,01, så att ett fragment precis på djupet inte klipps av avrundningen.
constexpr double kFarMargin = 1.01;
// Reserven när översta bildradens stråle inte når något plan i z-spannet
// (kan inte hända under kartans tilttak): det bortre planet så långt bort
// som djupprecisionen tillåter, far/near = 10⁴.
constexpr double kDepthRatio = 1e4;
// Det ortografiska djupet: ögat står kartans perspektivavstånd ovanför
// spannet, och klipplanen har den här andelen av avståndet till godo.
constexpr double kOrthoSlack = 0.5;
// Strålen och planet är parallella när cosinus för vinkeln mellan riktningen
// och normalen är under det här — och upp-vektorn parallell med blicken när
// kryssprodukten är det (relativt längderna).
constexpr double kParallel = 1e-12;
// Snittpolygonets hörn närmare varandra än så här, relativt pyramidens
// storlek, är samma hörn.
constexpr double kSamePoint = 1e-12;

Vec3d add(Vec3d a, Vec3d b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3d sub(Vec3d a, Vec3d b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3d mul(Vec3d a, double s)
{
    return {a.x * s, a.y * s, a.z * s};
}

double dot(Vec3d a, Vec3d b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3d cross(Vec3d a, Vec3d b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

double length(Vec3d a)
{
    return std::sqrt(dot(a, a));
}

bool finite(Vec3d a)
{
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}

// Vyns ortonormerade bas: blicken f, höger r = f × upp och den räta
// uppåtriktningen u = r × f.
struct Basis {
    Vec3d f, r, u;
};

// En vy som går att räkna på (reglerna står vid View3D) — och dess bas.
bool usable(const View3D& v, Basis& b)
{
    if (!(v.box.w > 0.0f) || !(v.box.h > 0.0f) || !finite(v.eye) ||
        !finite(v.forward) || !finite(v.up) || !std::isfinite(v.nearZ) ||
        !std::isfinite(v.farZ) || !(v.farZ > v.nearZ))
        return false;
    if (v.projection == Projection3D::Perspective) {
        if (!(v.nearZ > 0.0))
            return false;
        if (v.frustum) {
            // Ett satt frustum ersätter synfältet: tangenterna ändliga och
            // kanterna i ordning.
            const FrustumTangents& t = *v.frustum;
            if (!std::isfinite(t.left) || !std::isfinite(t.right) ||
                !std::isfinite(t.down) || !std::isfinite(t.up) ||
                !(t.left < t.right) || !(t.down < t.up) ||
                !std::isfinite(t.right - t.left) ||
                !std::isfinite(t.up - t.down))
                return false;
        } else if (!(v.fovY > 0.0) || !(v.fovY < std::numbers::pi)) {
            return false;
        }
    } else if (!(v.orthoHalfHeight > 0.0) ||
               !std::isfinite(v.orthoHalfHeight)) {
        return false;
    }
    const double fl = length(v.forward);
    if (!(fl > 0.0))
        return false;
    b.f = mul(v.forward, 1.0 / fl);
    const Vec3d  r  = cross(b.f, v.up);
    const double rl = length(r);
    if (!(rl > length(v.up) * kParallel))
        return false;   // upp är noll eller parallell med blicken
    b.r = mul(r, 1.0 / rl);
    b.u = cross(b.r, b.f);
    return true;
}

// Ytans utsträckning i bildplanet: halva spannet (hx, hy) och mittens
// förskjutning (cx, cy) från blickens axel. Perspektiv utan frustum:
// tan(fov/2) × bildförhållandet och tan(fov/2), mitten exakt 0. Perspektiv
// med frustum: tangenternas halva spann och mitt. Ortografiskt: halva
// måtten i världsenheter, mitten 0 (frustum ignoreras). Ytans kanter ligger
// på cx ± hx och cy ± hy.
struct Extents {
    double hx = 0.0;
    double hy = 0.0;
    double cx = 0.0;
    double cy = 0.0;
};

Extents extentsOf(const View3D& v)
{
    Extents e;
    if (v.projection == Projection3D::Perspective && v.frustum) {
        const FrustumTangents& t = *v.frustum;
        e.hx = (t.right - t.left) / 2.0;
        e.hy = (t.up - t.down) / 2.0;
        e.cx = (t.right + t.left) / 2.0;
        e.cy = (t.up + t.down) / 2.0;
        return e;
    }
    e.hy = v.projection == Projection3D::Perspective ? std::tan(v.fovY / 2.0)
                                                     : v.orthoHalfHeight;
    e.hx = e.hy * (static_cast<double>(v.box.w) / v.box.h);
    return e;
}

// Off-axis-perspektivet: x_ndc = (x/w − cx) / hx och y_ndc = (y/w − cy) / hy
// med w = −z, djupet i [0, 1] med samma z- och w-rader som
// `Mat4d::perspective`. Det symmetriska fallet går genom den funktionen, så
// det är oförändrat bit för bit.
Mat4d offAxisPerspective(const Extents& e, double n, double f)
{
    Mat4d p;
    p.m[0]  = 1.0 / e.hx;
    p.m[5]  = 1.0 / e.hy;
    p.m[8]  = e.cx / e.hx;   // z-kolumnens bidrag till x: cx · z / hx
    p.m[9]  = e.cy / e.hy;
    p.m[11] = -1.0;
    p.m[10] = f / (n - f);
    p.m[14] = -(f * n) / (f - n);
    return p;
}

// Ytans NDC → målets fysiska px (y nedåt, origo uppe till vänster) — samma
// steg som kartkamerans sista (camera.cpp). Likheten i
// scene_math_gate håller de två ihop.
Mat4d pixelMatrix(const Rectf& box)
{
    const double bw = box.w;
    const double bh = box.h;
    Mat4d        m  = Mat4d::identity();
    m.m[0]  = bw / 2.0;
    m.m[5]  = -bh / 2.0;
    m.m[12] = static_cast<double>(box.x) + bw / 2.0;
    m.m[13] = static_cast<double>(box.y) + bh / 2.0;
    return m;
}

// Pixeln (px, py) på NDC-djupet `depth`, genom inversen av en vys matris —
// en punkt i rymden matrisen tog in (relativt dess origo).
bool unprojectAt(const Mat4d& inv, double px, double py, double depth,
                 Vec3d& out)
{
    double h[4];
    inv.transform(px, py, depth, 1.0, h);
    if (h[3] == 0.0)
        return false;
    out = {h[0] / h[3], h[1] / h[3], h[2] / h[3]};
    return finite(out);
}

} // namespace

Mat4d viewProjection(const View3D& v, double ox, double oy, double oz)
{
    Basis b;
    if (!usable(v, b))
        return Mat4d{};   // W = 0 överallt — ingenting ritas

    // 1. Chunk-lokalt → relativt ögat. Den ENDA subtraktionen med stora tal,
    //    i double: det som går vidare till float är skillnaden mellan
    //    chunkens origo och ögat.
    Mat4d m = Mat4d::translation(ox - v.eye.x, oy - v.eye.y, oz - v.eye.z);

    // 2. Basbytet till ögonrymden — x höger, y upp, blicken längs −z.
    m = Mat4d::lookAt(0.0, 0.0, 0.0, b.f.x, b.f.y, b.f.z, b.u.x, b.u.y,
                      b.u.z) *
        m;

    // 3. Projektionen, djupet i [0, 1] mellan klipplanen.
    const Extents e = extentsOf(v);
    if (v.projection == Projection3D::Orthographic)
        m = Mat4d::ortho(-e.hx, e.hx, -e.hy, e.hy, v.nearZ, v.farZ) * m;
    else if (v.frustum)
        m = offAxisPerspective(e, v.nearZ, v.farZ) * m;
    else
        m = Mat4d::perspective(v.fovY, e.hx / e.hy, v.nearZ, v.farZ) * m;

    // 4. Ytans NDC → målets fysiska px.
    return pixelMatrix(v.box) * m;
}

bool project(const View3D& v, Vec3d w, double& px, double& py, double* depth)
{
    // Origot = ögat: punkten tas relativt ögat i double, samma väg som en
    // chunk med origot där. Nollmatrisen (oanvändbar vy) ger W = 0.
    double c[4];
    viewProjection(v, v.eye.x, v.eye.y, v.eye.z)
        .transform(w.x - v.eye.x, w.y - v.eye.y, w.z - v.eye.z, 1.0, c);
    if (!(c[3] > 0.0))
        return false;   // bakom kameran
    px = c[0] / c[3];
    py = c[1] / c[3];
    if (depth)
        *depth = c[2] / c[3];
    return true;
}

bool rayThrough(const View3D& v, double px, double py, Ray3d& out)
{
    Basis b;
    if (!usable(v, b) || !std::isfinite(px) || !std::isfinite(py))
        return false;
    const Extents e = extentsOf(v);
    // Pixeln som NDC över ytan, y uppåt — och dess förskjutning från axeln
    // i bildplanet på djupet 1 (perspektiv) respektive i världen.
    const double halfW = v.box.w / 2.0;
    const double halfH = v.box.h / 2.0;
    const double nx    = (px - (static_cast<double>(v.box.x) + halfW)) / halfW;
    const double ny    = ((static_cast<double>(v.box.y) + halfH) - py) / halfH;
    const Vec3d  side  = add(mul(b.r, e.cx + nx * e.hx),
                             mul(b.u, e.cy + ny * e.hy));
    if (v.projection == Projection3D::Perspective) {
        const Vec3d d = add(b.f, side);
        out.origin    = v.eye;
        out.dir       = mul(d, 1.0 / length(d));
    } else {
        out.origin = add(v.eye, add(mul(b.f, v.nearZ), side));
        out.dir    = b.f;
    }
    return true;
}

double pxPerUnitAt(const View3D& v, Vec3d w)
{
    Basis b;
    if (!usable(v, b))
        return 0.0;
    const Extents e     = extentsOf(v);
    const double  halfH = v.box.h / 2.0;
    if (v.projection == Projection3D::Orthographic)
        return halfH / e.hy;
    // Djupet längs blicken, relativt ögat i double — en SWEREF-punkt minus
    // ett SWEREF-öga tappar inga siffror som spelar roll.
    const double d = dot(sub(w, v.eye), b.f);
    if (!(d > 0.0))
        return 0.0;
    return halfH / (e.hy * d);
}

bool intersect(const Ray3d& r, const Plane3d& p, Vec3d& hit, double* t)
{
    if (!finite(r.origin) || !finite(r.dir) || !finite(p.point) ||
        !finite(p.normal))
        return false;
    const double nl    = length(p.normal);
    const double dl    = length(r.dir);
    const double denom = dot(p.normal, r.dir);
    if (!(nl > 0.0) || !(dl > 0.0) || !(std::abs(denom) > kParallel * nl * dl))
        return false;   // ingen normal, ingen riktning eller parallell
    const double s = dot(p.normal, sub(p.point, r.origin)) / denom;
    if (!(s >= 0.0) || !std::isfinite(s))
        return false;   // planet ligger bakom strålens början
    hit = add(r.origin, mul(r.dir, s));
    if (t)
        *t = s;
    return true;
}

std::vector<Vec3d> visiblePlaneRegion(const View3D& v, const Plane3d& p,
                                      double maxDistance)
{
    std::vector<Vec3d> out;
    Basis              b;
    if (!usable(v, b) || !finite(p.point) || !finite(p.normal))
        return out;
    const double nl = length(p.normal);
    if (!(nl > 0.0))
        return out;
    const Vec3d n = mul(p.normal, 1.0 / nl);

    double farD = v.farZ;
    if (maxDistance > 0.0 && maxDistance < farD)
        farD = maxDistance;
    if (!(farD > v.nearZ))
        return out;   // gränsen ligger framför närplanet — inget syns

    // Synpyramidens åtta hörn RELATIVT ögat (inga stora tal): närplanets
    // fyra (0–3) och det bortre planets (4–7), varvet runt i samma ordning.
    const Extents e     = extentsOf(v);
    const bool    persp = v.projection == Projection3D::Perspective;
    Vec3d         c[8];
    double        storlek = 0.0;
    for (int k = 0; k < 2; ++k) {
        const double d = k == 0 ? v.nearZ : farD;
        // Planets halva mått och mittens förskjutning från axeln: i
        // perspektiv tangenterna gånger djupet, ortografiskt världsenheter.
        const double s   = persp ? d : 1.0;
        const double sx  = e.hx * s;
        const double sy  = e.hy * s;
        const Vec3d  mid = add(add(mul(b.f, d), mul(b.r, e.cx * s)),
                               mul(b.u, e.cy * s));
        c[4 * k + 0] = sub(sub(mid, mul(b.r, sx)), mul(b.u, sy));
        c[4 * k + 1] = sub(add(mid, mul(b.r, sx)), mul(b.u, sy));
        c[4 * k + 2] = add(add(mid, mul(b.r, sx)), mul(b.u, sy));
        c[4 * k + 3] = add(sub(mid, mul(b.r, sx)), mul(b.u, sy));
        for (int i = 0; i < 4; ++i)
            storlek = std::max(storlek, length(c[4 * k + i]));
    }

    // Hörnens sida om planet; där kanterna byter sida går snittet.
    const Vec3d p0 = sub(p.point, v.eye);
    double      s[8];
    for (int i = 0; i < 8; ++i)
        s[i] = dot(n, sub(c[i], p0));
    static constexpr int kKanter[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0},
                                           {4, 5}, {5, 6}, {6, 7}, {7, 4},
                                           {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    std::vector<Vec3d> pts;
    for (int i = 0; i < 8; ++i)
        if (s[i] == 0.0)
            pts.push_back(c[i]);
    for (const auto& k : kKanter) {
        const double sa = s[k[0]];
        const double sb = s[k[1]];
        if ((sa < 0.0 && sb > 0.0) || (sa > 0.0 && sb < 0.0))
            pts.push_back(
                add(c[k[0]], mul(sub(c[k[1]], c[k[0]]), sa / (sa - sb))));
    }

    // Samma hörn två gånger (ett pyramidhörn i planet ger flera kanter).
    const double       eps = kSamePoint * storlek;
    std::vector<Vec3d> unika;
    for (const Vec3d& q : pts) {
        bool ny = true;
        for (const Vec3d& u : unika)
            if (length(sub(q, u)) <= eps)
                ny = false;
        if (ny)
            unika.push_back(q);
    }
    if (unika.size() < 3)
        return out;

    // Moturs sett från normalens sida: vinkeln kring tyngdpunkten i en bas
    // (e1, e2) i planet med e1 × e2 = n.
    Vec3d mitt{};
    for (const Vec3d& q : unika)
        mitt = add(mitt, q);
    mitt = mul(mitt, 1.0 / static_cast<double>(unika.size()));
    const Vec3d axel = std::abs(n.x) <= std::abs(n.y) &&
                               std::abs(n.x) <= std::abs(n.z)
                           ? Vec3d{1.0, 0.0, 0.0}
                       : std::abs(n.y) <= std::abs(n.z) ? Vec3d{0.0, 1.0, 0.0}
                                                        : Vec3d{0.0, 0.0, 1.0};
    Vec3d e1 = cross(axel, n);
    e1       = mul(e1, 1.0 / length(e1));
    const Vec3d e2 = cross(n, e1);
    std::vector<std::pair<double, Vec3d>> kring;
    kring.reserve(unika.size());
    for (const Vec3d& q : unika) {
        const Vec3d d = sub(q, mitt);
        kring.emplace_back(std::atan2(dot(d, e2), dot(d, e1)), q);
    }
    std::sort(kring.begin(), kring.end(),
              [](const auto& a, const auto& b2) { return a.first < b2.first; });

    // Ett polygon utan area (planet snuddar en kant) är ingen synlig del.
    double area = 0.0;
    for (std::size_t i = 0; i < kring.size(); ++i) {
        const Vec3d a  = sub(kring[i].second, mitt);
        const Vec3d b2 = sub(kring[(i + 1) % kring.size()].second, mitt);
        area += dot(cross(a, b2), n);
    }
    if (!(area > eps * eps))
        return out;

    out.reserve(kring.size());
    for (const auto& k : kring)
        out.push_back(add(v.eye, k.second));
    return out;
}

View3D toView3D(const MapView& m, double zMin, double zMax)
{
    // Ett fel svarar med en tom yta — en oanvändbar vy. Kartmatrisen tas med
    // origo i kamerans mitt, så allt nedan är relativt (cx, cy, 0) utan
    // stora tal; nollmatrisen (oanvändbar MapView) saknar invers.
    const Mat4d kart = viewProjection(m, m.cx, m.cy);
    Mat4d       inv;
    if (!kart.inverse(inv))
        return View3D{};

    // Z-spannet ∪ 0: kartans plan ritas alltid.
    double lo = std::isfinite(zMin) ? zMin : 0.0;
    double hi = std::isfinite(zMax) ? zMax : 0.0;
    if (lo > hi)
        std::swap(lo, hi);
    lo = std::min(lo, 0.0);
    hi = std::max(hi, 0.0);

    // Tre punkter ur inversen: ytans mittpixel på djupet 0 (kartans
    // närplan) och 0,5, och överkantens mitt på djupet 0,5. Punkter på
    // samma djup ligger i ett plan parallellt med bildplanet — i perspektiv
    // som ortografiskt — så mitten → överkanten är uppåt och dess längd
    // halva ytans höjd där.
    const double pxC = static_cast<double>(m.box.x) + m.box.w / 2.0;
    const double pyC = static_cast<double>(m.box.y) + m.box.h / 2.0;
    Vec3d        q0, q1, qt;
    if (!unprojectAt(inv, pxC, pyC, 0.0, q0) ||
        !unprojectAt(inv, pxC, pyC, 0.5, q1) ||
        !unprojectAt(inv, pxC, m.box.y, 0.5, qt))
        return View3D{};
    Vec3d        f  = sub(q1, q0);
    const double fl = length(f);
    const Vec3d  u  = sub(qt, q1);
    const double hh = length(u);
    if (!(fl > 0.0) || !(hh > 0.0))
        return View3D{};
    f = mul(f, 1.0 / fl);

    View3D v;
    v.forward = f;
    v.up      = mul(u, 1.0 / hh);
    Vec3d        eye;
    const double pitch = std::isfinite(m.pitchDeg) ? m.pitchDeg : 0.0;
    if (std::clamp(pitch, 0.0, kMaxPitchDeg) > 0.0) {
        // PERSPEKTIV — samma val som kartkameran gör. Ögat är projektionens
        // centrum: den enda punkt matrisen skickar till (0, 0, 1, 0), alltså
        // W = 0 med skärmläget 0/0.
        double h[4];
        inv.transform(0.0, 0.0, 1.0, 0.0, h);
        if (h[3] == 0.0)
            return View3D{};
        eye = {h[0] / h[3], h[1] / h[3], h[2] / h[3]};
        const double d1 = dot(sub(q1, eye), f);
        if (!finite(eye) || !(d1 > 0.0))
            return View3D{};
        v.projection = Projection3D::Perspective;
        v.fovY       = 2.0 * std::atan(hh / d1);
        v.nearZ      = dot(sub(q0, eye), f);
        // Det bortre planet: djupet där översta bildradens stråle når
        // spannets plan — det bortersta av dem den når framför ögat.
        Vec3d        topp = sub(qt, eye);
        const double tl   = length(topp);
        if (!(tl > 0.0))
            return View3D{};
        topp           = mul(topp, 1.0 / tl);
        double djupast = 0.0;
        for (const double z : {lo, hi}) {
            if (topp.z == 0.0)
                continue;
            const double t = (z - eye.z) / topp.z;
            if (t > 0.0)
                djupast = std::max(djupast, t * dot(topp, f));
        }
        v.farZ = djupast > v.nearZ ? djupast * kFarMargin
                                   : v.nearZ * kDepthRatio;
    } else {
        // ORTOGRAFISKT: ingen kamera att stå någonstans. Ögat ställs på
        // mittstrålen, kartans perspektivavstånd (det pitch → 0 skulle ge)
        // ovanför spannets översta punkt, och klipplanen omsluter spannet.
        if (f.z == 0.0)
            return View3D{};
        v.projection      = Projection3D::Orthographic;
        v.orthoHalfHeight = hh;
        const double stå  = hh / std::tan(kMapFovRad / 2.0);
        // Mittstrålens punkt i planet z = 0, och spannets ändar som höjd
        // längs −blicken därifrån.
        const Vec3d  c    = add(q0, mul(f, -q0.z / f.z));
        const double hLo  = -f.z * lo;
        const double hHi  = -f.z * hi;
        const double hMax = std::max(hLo, hHi);
        const double hMin = std::min(hLo, hHi);
        eye               = sub(c, mul(f, hMax + stå));
        v.nearZ           = stå * kOrthoSlack;
        v.farZ            = stå + (hMax - hMin) + stå * kOrthoSlack;
    }
    v.eye = {eye.x + m.cx, eye.y + m.cy, eye.z};
    v.box = m.box;
    return v;
}

FrameUniforms frameUniforms(const View3D& v, double ox, double oy, double oz)
{
    FrameUniforms u{};
    u.scale[1] = 1.0f;   // uiScale och opaciteten neutrala, pxPerMm okänd
    u.scale[2] = 1.0f;
    Basis b;
    if (!usable(v, b))
        return u;   // nollmatriser — ingenting ritas

    const Mat4d m = viewProjection(v, ox, oy, oz);
    m.toFloat(u.viewProj);
    Mat4d inv;
    if (m.inverse(inv))
        inv.toFloat(u.invViewProj);

    const Extents ext   = extentsOf(v);
    const bool    persp = v.projection == Projection3D::Perspective;
    // Ögat relativt origot i double (RTC) — ortografiskt närplanets mitt,
    // strålarnas början.
    const Vec3d e   = persp ? v.eye : add(v.eye, mul(b.f, v.nearZ));
    const auto  sätt = [](float out[4], double x, double y, double z,
                         double w) {
        out[0] = static_cast<float>(x);
        out[1] = static_cast<float>(y);
        out[2] = static_cast<float>(z);
        out[3] = static_cast<float>(w);
    };
    sätt(u.eye, e.x - ox, e.y - oy, e.z - oz, persp ? 1.0 : 0.0);
    sätt(u.forward, b.f.x, b.f.y, b.f.z, 0.0);
    sätt(u.right, b.r.x * ext.hx, b.r.y * ext.hx, b.r.z * ext.hx, 0.0);
    sätt(u.up, b.u.x * ext.hy, b.u.y * ext.hy, b.u.z * ext.hy, 0.0);
    // Strålens mitt: mot ytans mittpunkt — blicken plus mittens förskjutning
    // i höger- och uppåtriktningen (noll utan frustum och ortografiskt).
    const Vec3d c = add(b.f, add(mul(b.r, ext.cx), mul(b.u, ext.cy)));
    sätt(u.rayCenter, c.x, c.y, c.z, 0.0);
    // Världsenheter per fysisk px: halva höjden (på djupet 1 i perspektiv)
    // genom halva ytans höjd i px.
    const double perPx = ext.hy / (v.box.h / 2.0);
    u.pixelSize[0]     = persp ? static_cast<float>(perPx) : 0.0f;
    u.pixelSize[1]     = persp ? 0.0f : static_cast<float>(perPx);
    return u;
}

} // namespace gfx
