// 3D-plockningen: kärnan — strålen mot en mesh genom
// BVH:n och strålen mot ett plan — och ritytans plockning ovanpå den, formerna
// i rymden med kamerans skärmtoleranser. Kontraktet står i pick3d.h.

#include "pick3d.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <gfx/camera.h>

#include "gfx/platform.h"
#include "instances.h"
#include "mesh_data.h"
#include "scene.h"
#include "transform3d.h"

namespace glaze::detail {
namespace {

bool finite3(double x, double y, double z)
{
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

// MÖLLER–TRUMBORE i double, båda sidorna: strålen o + t · d mot triangeln
// (a, b, c). Svaret: träff med t ≥ 0, och u, v (barycentriska) och
// determinanten — positiv när strålen kommer från framsidan.
bool intersectTriangle(const double o[3], const double d[3], const Float3& A,
                       const Float3& B, const Float3& C, double& t, double& u,
                       double& v, double& det)
{
    const double a[3]  = {A.x, A.y, A.z};
    const double e1[3] = {B.x - a[0], B.y - a[1], B.z - a[2]};
    const double e2[3] = {C.x - a[0], C.y - a[1], C.z - a[2]};
    const double p[3]  = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2],
                          d[0] * e2[1] - d[1] * e2[0]};
    det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (det == 0.0)
        return false;   // strålen parallell med triangeln (eller ingen area)
    const double inv  = 1.0 / det;
    const double s[3] = {o[0] - a[0], o[1] - a[1], o[2] - a[2]};
    u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
    if (u < 0.0 || u > 1.0)
        return false;
    const double q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2],
                         s[0] * e1[1] - s[1] * e1[0]};
    v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
    if (v < 0.0 || u + v > 1.0)
        return false;
    t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    return t >= 0.0;
}

// De RITADE trianglarna [t0, t1): nivåns element snittat med intervallets.
// false = ingenting ritas (kNoLevel) eller en ogiltig nivå/ett ogiltigt
// intervall (loggat en gång per mesh).
bool drawnTriangles(MeshData& d, int lod, const IndexRange& range,
                    std::uint32_t& t0, std::uint32_t& t1)
{
    const std::uint32_t ne = d.elementCount();
    std::uint64_t       lo = 0, hi = ne;
    if (lod == kNoLevel)
        return false;
    if (lod != kWholeMesh) {
        if (lod < 0 || static_cast<std::size_t>(lod) >= d.lods.size()) {
            if (!d.loggedRange.exchange(true))
                gfx::log("pickMesh: nivå %d finns inte i meshen (%zu nivåer) — "
                         "ingen träff; loggas en gång per mesh",
                         lod, d.lods.size());
            return false;
        }
        lo = d.lods[static_cast<std::size_t>(lod)].first;
        hi = lo + d.lods[static_cast<std::size_t>(lod)].count;
    }
    const std::uint32_t rc =
        range.first <= ne ? std::min(range.count, ne - range.first) : 0;
    if (range.first % 3 != 0 || range.first > ne || rc % 3 != 0) {
        if (!d.loggedRange.exchange(true))
            gfx::log("pickMesh: intervallet %u + %u är inte hela trianglar i "
                     "meshens %u element — ingen träff; loggas en gång per mesh",
                     range.first, range.count, ne);
        return false;
    }
    lo = std::max<std::uint64_t>(lo, range.first);
    hi = std::min<std::uint64_t>(hi, std::uint64_t{range.first} + rc);
    t0 = static_cast<std::uint32_t>(lo / 3);
    t1 = lo < hi ? static_cast<std::uint32_t>(hi / 3) : t0;
    return true;
}

} // namespace

std::optional<MeshHit> pickMesh(const Mesh& mesh, const Ray3& ray,
                                const MeshPick& opts)
{
    const std::shared_ptr<MeshData>& d = MeshAccess::data(mesh);
    if (!d)
        return std::nullopt;
    if (!d->keepCpuCopy) {
        if (!d->loggedNoCopy.exchange(true))
            gfx::log("pickMesh: meshen saknar CPU-kopia (keepCpuCopy = false) "
                     "och kan inte plockas; loggas en gång per mesh");
        return std::nullopt;
    }
    const Vec3&  dir = ray.direction;
    const double dl  = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (!(dl > 0.0) || !std::isfinite(dl) ||
        !finite3(ray.origin.x, ray.origin.y, ray.origin.z))
        return std::nullopt;
    std::uint32_t t0 = 0, t1 = 0;
    if (!drawnTriangles(*d, opts.lod, opts.range, t0, t1) || t0 >= t1)
        return std::nullopt;

    // Strålen i meshens lokala rymd: början minus origot, i double.
    const double o[3]  = {ray.origin.x - d->origin.x, ray.origin.y - d->origin.y,
                          ray.origin.z - d->origin.z};
    const double dd[3] = {dir.x, dir.y, dir.z};
    const double tMax  = opts.maxDistance > 0.0
                             ? opts.maxDistance / dl
                             : std::numeric_limits<double>::infinity();

    const std::shared_ptr<MeshPicking> hold = d->picking;
    const Bvh&                         bvh  = meshBvh(*d, *hold);

    std::uint32_t bestTri = std::numeric_limits<std::uint32_t>::max();
    double        bestT = tMax, bestU = 0.0, bestV = 0.0, bestDet = 0.0;
    const bool    alpha = opts.opaqueOnly && !d->colors.empty();
    traverseRay(bvh, o, dd, tMax, [&](std::uint32_t tri, double& limit) {
        if (tri < t0 || tri >= t1)
            return;
        const std::uint32_t a = d->corner(tri, 0), b = d->corner(tri, 1),
                            c = d->corner(tri, 2);
        if (alpha && std::min({d->colors[a].a, d->colors[b].a, d->colors[c].a}) < 255)
            return;
        double t = 0.0, u = 0.0, v = 0.0, det = 0.0;
        if (!intersectTriangle(o, dd, d->positions[a], d->positions[b],
                               d->positions[c], t, u, v, det))
            return;
        if (t < bestT || (t == bestT && tri < bestTri)) {
            bestTri = tri;
            bestT   = t;
            bestU   = u;
            bestV   = v;
            bestDet = det;
            limit   = t;
        }
    });
    if (bestTri == std::numeric_limits<std::uint32_t>::max())
        return std::nullopt;

    MeshHit h;
    h.triangle  = bestTri;
    h.t         = bestT;
    h.distance  = bestT * dl;
    h.point     = {ray.origin.x + bestT * dir.x, ray.origin.y + bestT * dir.y,
                   ray.origin.z + bestT * dir.z};
    h.u         = bestU;
    h.v         = bestV;
    h.frontFace = bestDet > 0.0;
    const Float3& A = d->positions[d->corner(bestTri, 0)];
    const Float3& B = d->positions[d->corner(bestTri, 1)];
    const Float3& C = d->positions[d->corner(bestTri, 2)];
    const double  e1[3] = {double(B.x) - A.x, double(B.y) - A.y, double(B.z) - A.z};
    const double  e2[3] = {double(C.x) - A.x, double(C.y) - A.y, double(C.z) - A.z};
    const double  n[3]  = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                           e1[0] * e2[1] - e1[1] * e2[0]};
    const double  nl    = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    h.normal            = {n[0] / nl, n[1] / nl, n[2] / nl};
    return h;
}

std::optional<PlaneHit> rayPlane(const Ray3& ray, const Plane3& plane)
{
    const gfx::Ray3d r{{ray.origin.x, ray.origin.y, ray.origin.z},
                       {ray.direction.x, ray.direction.y, ray.direction.z}};
    const gfx::Plane3d p{{plane.point.x, plane.point.y, plane.point.z},
                         {plane.normal.x, plane.normal.y, plane.normal.z}};
    gfx::Vec3d hit{};
    double     t = 0.0;
    if (!gfx::intersect(r, p, hit, &t))
        return std::nullopt;
    return PlaneHit{{hit.x, hit.y, hit.z}, t};
}

// ------------------------------------------------------ ritytans plockning

namespace {

// Programmens gräns för w: en punkt närmare kameran än så har ingen
// skärmpunkt (Projection::toScreen, hit_index.cpp).
constexpr double kMinW = 1e-6;

// PLOCKNINGENS TAK I MESHENS RAM: strålens parameter räknas om
// genom riktningens längd där (`pickMesh` delar med den), och avrundningen får
// aldrig göra taket kortare än anroparens — en aning längre är ofarligt, för
// det exakta provet står efter plockningen.
constexpr double kLimitSlack = 1.0 + 0x1p-40;

gfx::Vec3d g3(const Point3& p)
{
    return {p.x, p.y, p.z};
}

Point3 toPoint(const gfx::Vec3d& v)
{
    return {v.x, v.y, v.z};
}

Point3 along(const Ray3& r, double t)
{
    return {r.origin.x + t * r.direction.x, r.origin.y + t * r.direction.y,
            r.origin.z + t * r.direction.z};
}

// Strålens parameter för punktens fotpunkt — avståndet längs den enhetliga
// riktningen till det som ligger bredvid strålen.
double rayParam(const Ray3& r, const Point3& p)
{
    return (p.x - r.origin.x) * r.direction.x + (p.y - r.origin.y) * r.direction.y +
           (p.z - r.origin.z) * r.direction.z;
}

// Skalan vid punkten, fysiska px per världsenhet (0 utan kamera).
double ppu(const PickContext& c, const Point3& p)
{
    return c.view ? gfx::pxPerUnitAt(*c.view, g3(p)) : 0.0;
}

// Ett skärmmått i fysiska px vid punkten.
double tolAt(const PickContext& c, const ScreenTol& t, const Point3& p)
{
    return t.px + (t.world > 0.0 ? t.world * ppu(c, p) : 0.0);
}

// Världspunkten på skärmen — framför närplanet och före det bortre, som
// programmen ritar den.
bool screenOf(const Projection& proj, const Point3& p, double& sx, double& sy)
{
    double q[4];
    proj.clip(p.x, p.y, p.z, q);
    if (!(q[2] >= 0.0) || !(q[2] <= q[3]) || !(q[3] > kMinW))
        return false;
    sx = q[0] / q[3];
    sy = q[1] / q[3];
    return std::isfinite(sx) && std::isfinite(sy);
}

// Världssträckan a–b på skärmen: den del som ligger framför närplanet
// (klippdjup ≥ 0 — klipprymden är affin i världen, så skärningen görs där),
// projicerad. Svarar med skärmavståndet från (px, py) och världspunkten där —
// PERSPEKTIVKORREKT: skärmens parameter u blir klipprymdens
// s = u · Wa / ((1 − u) · Wb + u · Wa). false när ingen del syns.
bool segmentNear(const Projection& proj, Point3 a, Point3 b, double px, double py,
                 double& dist, Point3& near)
{
    double ca[4], cb[4];
    proj.clip(a.x, a.y, a.z, ca);
    proj.clip(b.x, b.y, b.z, cb);
    if ((ca[2] < 0.0 && cb[2] < 0.0) || (ca[2] > ca[3] && cb[2] > cb[3]))
        return false;
    const auto tillNära = [](double (&c)[4], const double (&o)[4], Point3& p,
                             const Point3& q) {
        const double t = c[2] / (c[2] - o[2]);
        for (int k = 0; k < 4; ++k)
            c[k] += (o[k] - c[k]) * t;
        p = {p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t, p.z + (q.z - p.z) * t};
    };
    if (ca[2] < 0.0)
        tillNära(ca, cb, a, b);
    else if (cb[2] < 0.0)
        tillNära(cb, ca, b, a);
    if (!(ca[3] > kMinW) || !(cb[3] > kMinW))
        return false;
    const double x0 = ca[0] / ca[3], y0 = ca[1] / ca[3];
    const double x1 = cb[0] / cb[3], y1 = cb[1] / cb[3];
    const double dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
    const double u =
        l2 > 0.0 ? std::clamp(((px - x0) * dx + (py - y0) * dy) / l2, 0.0, 1.0)
                 : 0.0;
    dist = std::hypot(px - (x0 + u * dx), py - (y0 + u * dy));
    const double den = (1.0 - u) * cb[3] + u * ca[3];
    const double s   = den > 0.0 ? u * ca[3] / den : u;
    near = {a.x + (b.x - a.x) * s, a.y + (b.y - a.y) * s, a.z + (b.z - a.z) * s};
    return std::isfinite(dist);
}

// HUR LÅNGT FRÅN STRÅLEN en punkt kan ligga och ändå vara inom `tolPx`
// fysiska px på skärmen (+ `tolWorld` världsenheter), på avståndet r från
// strålens början: perspektivets världsenheter per px växer med djupet, som
// är högst r; ortografiskt är de lika överallt.
double reachWorld(const gfx::View3D& v, double tolPx, double tolWorld, double r)
{
    const double h = std::max(static_cast<double>(v.box.h), 1.0);
    const double perPx = v.projection == gfx::Projection3D::Orthographic
                             ? 2.0 * v.orthoHalfHeight / h
                             : 2.0 * std::tan(v.fovY / 2.0) * r / h;
    return tolPx * perPx + tolWorld;
}

double frobenius(const double (&m)[3][3])
{
    double s = 0.0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            s += m[i][j] * m[i][j];
    return std::sqrt(s);
}

double det3(const double (&m)[3][3])
{
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

// Lådans största avstånd från strålens början.
double farCorner(const Ray3& r, const Box3& b)
{
    double r2 = 0.0;
    for (int k = 0; k < 8; ++k) {
        const double x = ((k & 1) ? b.max.x : b.min.x) - r.origin.x;
        const double y = ((k & 2) ? b.max.y : b.min.y) - r.origin.y;
        const double z = ((k & 4) ? b.max.z : b.min.z) - r.origin.z;
        r2             = std::max(r2, x * x + y * y + z * z);
    }
    return std::sqrt(r2);
}

// VAR STRÅLEN (t ≥ 0) GÅR IN i lådan uppblåst med `reach` — tomt när den inte
// når den. Plattprovet i double med samma NaN-regel som BVH:ns (bvh.h
// `rayHitsNode`).
std::optional<double> rayBoxEntry(const Ray3& r, const Box3& b, double reach)
{
    const double o[3]   = {r.origin.x, r.origin.y, r.origin.z};
    const double d[3]   = {r.direction.x, r.direction.y, r.direction.z};
    const double lo[3]  = {b.min.x - reach, b.min.y - reach, b.min.z - reach};
    const double hi[3]  = {b.max.x + reach, b.max.y + reach, b.max.z + reach};
    double       t0 = 0.0, t1 = std::numeric_limits<double>::infinity();
    for (int a = 0; a < 3; ++a) {
        double tn = (lo[a] - o[a]) / d[a];
        double tf = (hi[a] - o[a]) / d[a];
        if (tn > tf)
            std::swap(tn, tf);
        t0 = tn > t0 ? tn : t0;
        t1 = tf < t1 ? tf : t1;
    }
    if (t0 > t1)
        return std::nullopt;
    return t0;
}

// Når strålen (t ≥ 0) lådan uppblåst med `reach`?
bool rayNearBox(const Ray3& r, const Box3& b, double reach)
{
    return rayBoxEntry(r, b, reach).has_value();
}

// Når strålen lådan (uppblåst med `reach`) FÖRE plockningens tak? Varje träff i
// lådan — en yta, ett hörns eller en kants fotpunkt i konen — ligger längre bort
// än där strålen går in, så en låda bortom taket har inget att svara med.
// Lådan räknas i världen och träffen i meshens ram, så taket
// prövas med skymningens marginal mot avrundningen; provet efter plockningen
// är det exakta.
bool reaches(const PickContext& c, const Box3& b, double reach)
{
    const std::optional<double> in = rayBoxEntry(c.ray, b, reach);
    return in && (!(c.maxDistance > 0.0) ||
                  *in <= c.maxDistance + occlusionSlack(c.maxDistance));
}

// Meshens lägen relativt origot som låda.
Box3 localBox(const MeshData& d)
{
    return Box3{{d.bounds.min.x - d.origin.x, d.bounds.min.y - d.origin.y,
                 d.bounds.min.z - d.origin.z},
                {d.bounds.max.x - d.origin.x, d.bounds.max.y - d.origin.y,
                 d.bounds.max.z - d.origin.z}};
}

// Stilen helt täckande (färgens alfa, opaciteten och lagrets)?
bool styleOpaque(const MeshStyle& st, const PickContext& c)
{
    return st.color.a == 255 && st.opacity >= 1.0f && c.layerOpacity >= 1.0f;
}

bool triangleOpaque(const MeshData& d, std::uint32_t tri)
{
    if (d.colors.empty())
        return true;
    for (std::uint32_t k = 0; k < 3; ++k)
        if (d.colors[d.corner(tri, k)].a < 255)
            return false;
    return true;
}

// Den SKÄRMKONSTANTA meshens skala (Mesh-programmets SCREEN_SIZED): px och mm
// × världen per fysisk px vid ankaret, världsenheter rakt av. 0 utan kamera.
double screenScale(const PickContext& c, Length size, const Point3& anchor)
{
    if (size.unit == Unit::World)
        return size.value;
    const double k = ppu(c, anchor);
    if (!(k > 0.0))
        return 0.0;
    return size.value * (size.unit == Unit::Mm ? c.pxPerMm : c.pxPerPx) / k;
}

void scaleLinear(Affine3& a, double k)
{
    for (auto& rad : a.m)
        for (double& v : rad)
            v *= k;
}

// En placerad meshs ram: avbildningen läge → värld, inversen och normerna
// (konens sållning i den lokala ramen) och determinantens tecken
// (vindningen).
struct MeshFrame {
    Affine3 A, inv;
    double  normA = 0.0, normInv = 0.0;
    bool    mirror = false;
};

bool meshFrame(const Affine3& A, MeshFrame& f)
{
    f.A = A;
    if (!invert(A, f.inv))
        return false;
    f.normA   = frobenius(A.m);
    f.normInv = frobenius(f.inv.m);
    f.mirror  = det3(A.m) < 0.0;
    return true;
}

// Konens toleranser för hörnen och kanterna.
double vertexEdgePx(const PickContext& c)
{
    return std::max(c.vertexTol.px, c.edgeTol.px);
}
double vertexEdgeWorld(const PickContext& c)
{
    return std::max(c.vertexTol.world, c.edgeTol.world);
}
bool wantsVertexEdge(const PickContext& c)
{
    return c.proj && c.view && (c.vertexTol.any() || c.edgeTol.any());
}

// EN PLACERAD MESH (ramen `f`): triangeln längs strålen, och hörnen och
// kanterna inom toleransen med sina skärmavstånd (ett delat hörn kommer en
// gång per triangel — ritytan väljer).
void pickPlacedMesh(const Mesh& mesh, MeshData& d, const MeshFrame& f, int lod,
                    const IndexRange& range, bool opaque, int instance,
                    const PickContext& c, std::vector<SceneHit>& out)
{
    const Point3 lo = f.inv.apply(c.ray.origin);
    const Vec3   ld = f.inv.applyDir(c.ray.direction);
    // YTAN — kärnans Möller–Trumbore i den lokala ramen (pickMesh drar
    // origot av igen, så strålen ges i meshens "världs"-koordinater) — när den
    // efterfrågas.
    if (c.surface && (!c.opaqueOnly || opaque)) {
        MeshPick mp;
        mp.opaqueOnly = c.opaqueOnly;
        mp.lod        = lod;
        mp.range      = range;
        // Taket i BVH:n: avståndet i den lokala strålens enhet,
        // där en världsenhet längs strålen är |ld| lång.
        if (c.maxDistance > 0.0)
            mp.maxDistance = c.maxDistance * kLimitSlack *
                             std::sqrt(ld.x * ld.x + ld.y * ld.y + ld.z * ld.z);
        const Ray3 lr{{d.origin.x + lo.x, d.origin.y + lo.y, d.origin.z + lo.z}, ld};
        if (const std::optional<MeshHit> h = pickMesh(mesh, lr, mp)) {
            if (!(c.maxDistance > 0.0) || h->t <= c.maxDistance) {
                SceneHit s;
                s.kind     = HitKind::Surface;
                s.element  = h->triangle;
                s.instance = instance;
                s.point    = along(c.ray, h->t);
                s.distance = h->t;
                // Normalen genom invers-transponatet, vänd under en
                // speglande ram (vindningen vänds med den).
                const Vec3& n  = h->normal;
                const auto& im = f.inv.m;
                Vec3 w{im[0][0] * n.x + im[1][0] * n.y + im[2][0] * n.z,
                       im[0][1] * n.x + im[1][1] * n.y + im[2][1] * n.z,
                       im[0][2] * n.x + im[1][2] * n.y + im[2][2] * n.z};
                const double l = std::sqrt(w.x * w.x + w.y * w.y + w.z * w.z);
                const double k = (f.mirror ? -1.0 : 1.0) / (l > 0.0 ? l : 1.0);
                s.normal       = {w.x * k, w.y * k, w.z * k};
                s.occludes     = c.occluder && opaque && triangleOpaque(d, h->triangle);
                out.push_back(s);
            }
        }
    }
    // HÖRNEN OCH KANTERNA inom toleransen — konen i BVH:n, provet på skärmen.
    if (!wantsVertexEdge(c) || !d.keepCpuCopy)
        return;
    std::uint32_t t0 = 0, t1 = 0;
    if (!drawnTriangles(d, lod, range, t0, t1) || t0 >= t1)
        return;
    const std::shared_ptr<MeshPicking> hold = d.picking;
    const Bvh&                         bvh  = meshBvh(d, *hold);
    const double o3[3] = {lo.x, lo.y, lo.z}, d3[3] = {ld.x, ld.y, ld.z};
    const double tolPx = vertexEdgePx(c), tolW = vertexEdgeWorld(c);
    traverseNear(
        bvh, o3, d3,
        [&](double r) { return reachWorld(*c.view, tolPx, tolW, f.normA * r) * f.normInv; },
        [&](std::uint32_t tri) {
            if (tri < t0 || tri >= t1)
                return;
            Point3        P[3];
            std::uint32_t v[3];
            for (std::uint32_t k = 0; k < 3; ++k) {
                v[k]            = d.corner(tri, k);
                const Float3& q = d.positions[v[k]];
                P[k]            = f.A.apply(Point3{q.x, q.y, q.z});
            }
            if (c.vertexTol.any())
                for (int k = 0; k < 3; ++k) {
                    double sx = 0.0, sy = 0.0;
                    if (!screenOf(*c.proj, P[k], sx, sy))
                        continue;
                    const double s = std::hypot(c.px - sx, c.py - sy);
                    const double t = rayParam(c.ray, P[k]);
                    if (s > tolAt(c, c.vertexTol, P[k]) ||
                        (c.maxDistance > 0.0 && t > c.maxDistance))
                        continue;
                    SceneHit h;
                    h.kind     = HitKind::Vertex;
                    h.element  = v[k];
                    h.instance = instance;
                    h.point    = P[k];
                    h.distance = t;
                    h.screen   = s;
                    out.push_back(h);
                }
            if (c.edgeTol.any())
                for (int k = 0; k < 3; ++k) {
                    double s = 0.0;
                    Point3 q{};
                    if (!segmentNear(*c.proj, P[k], P[(k + 1) % 3], c.px, c.py, s, q))
                        continue;
                    const double t = rayParam(c.ray, q);
                    if (s > tolAt(c, c.edgeTol, q) ||
                        (c.maxDistance > 0.0 && t > c.maxDistance))
                        continue;
                    SceneHit h;
                    h.kind     = HitKind::Edge;
                    h.element  = std::int64_t{3} * tri + k;
                    h.instance = instance;
                    h.point    = q;
                    h.distance = t;
                    h.screen   = s;
                    out.push_back(h);
                }
        });
}

// Lådans uppblåsning för hörnens och kanternas kon vid lådans bortre hörn.
double boxReach(const PickContext& c, const Box3& b, double extraPx = 0.0,
                double extraWorld = 0.0)
{
    const bool tol = wantsVertexEdge(c);
    if (!c.view || (!tol && extraPx <= 0.0 && extraWorld <= 0.0))
        return 0.0;
    return reachWorld(*c.view, (tol ? vertexEdgePx(c) : 0.0) + extraPx,
                      (tol ? vertexEdgeWorld(c) : 0.0) + extraWorld,
                      farCorner(c.ray, b));
}

// LINJEN I RYMDEN (Line3, och ytans kontur i rymden): segmenten inom halva
// bredden + toleransen.
void pickLine3(const Shape& s, const std::vector<Point3>& pts, bool closed,
               const PickContext& c, std::vector<SceneHit>& out)
{
    if (!c.proj || !c.view || pts.size() < 2 || !s.hasStroke())
        return;
    const Stroke& st = s.stroke();
    if (!st.paint.visible() || !(st.width.value > 0.0f))
        return;
    const double halv = 0.5 * st.width.value;
    const double hPx  = st.width.unit == Unit::Px   ? halv * c.pxPerPx
                      : st.width.unit == Unit::Mm   ? halv * c.pxPerMm
                                                    : 0.0;
    const double hW   = st.width.unit == Unit::World ? halv : 0.0;
    const std::size_t n = pts.size(), segs = closed ? n : n - 1;
    for (std::size_t i = 0; i < segs; ++i) {
        double d = 0.0;
        Point3 q{};
        if (!segmentNear(*c.proj, pts[i], pts[(i + 1) % n], c.px, c.py, d, q))
            continue;
        const double tol = tolAt(c, c.lineTol, q) + hPx + (hW > 0.0 ? hW * ppu(c, q) : 0.0);
        const double t   = rayParam(c.ray, q);
        if (d > tol || (c.maxDistance > 0.0 && t > c.maxDistance))
            continue;
        SceneHit h;
        h.kind     = HitKind::Shape;
        h.element  = static_cast<std::int64_t>(i);
        h.point    = q;
        h.distance = t;
        h.screen   = d;
        out.push_back(h);
    }
}

// MESHEN OCH YTAN I RYMDEN.
void pickMeshShape(const Shape& s, const PickContext& c, std::vector<SceneHit>& out)
{
    const std::shared_ptr<MeshData>& dp = MeshAccess::data(s.mesh());
    // Meshen bara när något i den efterfrågas — ytan eller hörnen och
    // kanterna (en terräng utan snap vars yta redan plockats har inget kvar).
    if (dp && (c.surface || wantsVertexEdge(c))) {
        MeshData&        d  = *dp;
        const Placement& pl = s.placement();
        const MeshStyle& st = s.meshStyle();
        const Point3     origin{d.origin.x + pl.at.x, d.origin.y + pl.at.y,
                            d.origin.z + pl.at.z};
        Affine3 A = rotationScale(pl.yaw, pl.pitch, pl.roll, pl.scale, origin.x,
                                  origin.y, origin.z);
        const bool screen = st.size.value > 0.0f && std::isfinite(st.size.value);
        bool       ok     = true;
        if (screen) {
            const double k = screenScale(c, st.size, origin);
            ok             = k > 0.0;
            scaleLinear(A, k);
        }
        const Box3 box = transformBox(A, localBox(d));
        int        lod = kWholeMesh;
        if (c.finest)
            lod = d.lods.empty() ? kWholeMesh : 0;
        else if (c.view)
            // Den RITADE nivån (scene3d.cpp): avståndet från ögat till den
            // placerade lådan — eller till origot för en skärmkonstant mesh.
            lod = lodLevel(d, distanceToBox(toPoint(c.view->eye),
                                            screen ? Box3{origin, origin} : box));
        MeshFrame f;
        if (ok && lod != kNoLevel && reaches(c, box, boxReach(c, box)) &&
            meshFrame(A, f))
            pickPlacedMesh(s.mesh(), d, f, lod, st.range, styleOpaque(st, c), -1,
                           c, out);
    }
    if (s.kind() == Shape::Kind::Area3)
        pickLine3(s, s.points3(), true, c, out);
}

// INSTANSERNA: cellerna sållar, varje instans med sin ram (instansens ram
// utanpå prototypens placering, instances.h), nivån per cell som ritningen.
void pickInstances(const Shape& s, const PickContext& c, std::vector<SceneHit>& out)
{
    const InstanceSet*               set = s.instanceSet().get();
    const std::shared_ptr<MeshData>& dp  = MeshAccess::data(s.mesh());
    if (!set || set->rows.empty() || !dp)
        return;
    MeshData&                    d     = *dp;
    const Placement&             pl    = s.placement();
    const MeshStyle&             st    = s.meshStyle();
    const std::vector<Instance>& insts = s.instances();
    const Affine3 P = rotationScale(pl.yaw, pl.pitch, pl.roll, pl.scale,
                                    d.origin.x + pl.at.x, d.origin.y + pl.at.y,
                                    d.origin.z + pl.at.z);
    const Point3  protoOrigin{P.t[0], P.t[1], P.t[2]};
    const bool    screen = st.size.value > 0.0f && std::isfinite(st.size.value);
    const Box3    local  = localBox(d);
    // En skärmkonstant prototyps räckvidd på skärmen (scene3d.cpp:s gallring):
    // storleken × prototypens radie × den största instansskalan.
    double screenPx = 0.0, screenW = 0.0;
    if (screen) {
        double r = 0.0;
        const Box3 pb = transformBox(P, local);
        for (int k = 0; k < 8; ++k) {
            const double x = ((k & 1) ? pb.max.x : pb.min.x) - P.t[0];
            const double y = ((k & 2) ? pb.max.y : pb.min.y) - P.t[1];
            const double z = ((k & 4) ? pb.max.z : pb.min.z) - P.t[2];
            r              = std::max(r, std::sqrt(x * x + y * y + z * z));
        }
        const double ext = st.size.value * r * set->maxScale;
        if (st.size.unit == Unit::World)
            screenW = ext;
        else
            screenPx = ext * (st.size.unit == Unit::Mm ? c.pxPerMm : c.pxPerPx);
    }
    for (const InstanceCell& cell : set->cells) {
        const Box3& cb = screen ? cell.anchors : cell.box;
        if (!rayNearBox(c.ray, cb, boxReach(c, cb, screenPx, screenW)))
            continue;
        int lod = kWholeMesh;
        if (c.finest)
            lod = d.lods.empty() ? kWholeMesh : 0;
        else if (c.view)
            lod = lodLevel(d, distanceToBox(toPoint(c.view->eye), cb));
        if (lod == kNoLevel)
            continue;
        for (std::uint32_t row = cell.first; row < cell.first + cell.count; ++row) {
            const std::uint32_t idx = set->order[row];
            const Instance&     in  = insts[idx];
            const Affine3 I = rotationScale(in.yaw, in.pitch, in.roll, in.scale,
                                            in.at.x, in.at.y, in.at.z);
            Affine3 Pk = P;
            if (screen) {
                const double k = screenScale(c, st.size, I.apply(protoOrigin));
                if (!(k > 0.0))
                    continue;
                scaleLinear(Pk, k);
            }
            const Affine3 A  = compose(I, Pk);
            const Box3    wb = transformBox(A, local);
            MeshFrame     f;
            if (!reaches(c, wb, boxReach(c, wb)) || !meshFrame(A, f))
                continue;
            // Instansens färg multiplicerar alfan — 0 är ingen färg.
            const bool opaque =
                styleOpaque(st, c) && (in.color.a == 0 || in.color.a == 255);
            pickPlacedMesh(s.mesh(), d, f, lod, st.range, opaque,
                           static_cast<int>(idx), c, out);
        }
    }
}

// PUNKTERNA: chunkarna sållar med konen, punkterna prövas på skärmen inom
// toleransen + halva den ritade storleken.
void pickPoints(const Shape& s, const PickContext& c, std::vector<SceneHit>& out)
{
    if (!c.proj || !c.view)
        return;
    const std::shared_ptr<PointCloudData>& pd = MeshAccess::data(s.pointCloud());
    const PointStyle&                      st = s.pointStyle();
    if (!pd || !(st.size.value > 0.0f) || !std::isfinite(st.size.value))
        return;
    const double halv = 0.5 * st.size.value;
    const double hPx  = st.size.unit == Unit::Px   ? halv * c.pxPerPx
                      : st.size.unit == Unit::Mm   ? halv * c.pxPerMm
                                                   : 0.0;
    const double hW   = st.size.unit == Unit::World ? halv : 0.0;
    for (const PointChunk& ch : pd->chunks) {
        if (ch.count == 0 ||
            !rayNearBox(c.ray, ch.bounds,
                        reachWorld(*c.view, c.pointTol.px + hPx, c.pointTol.world + hW,
                                   farCorner(c.ray, ch.bounds))))
            continue;
        for (std::uint32_t j = ch.first; j < ch.first + ch.count; ++j) {
            const Float3& q = pd->positions[j];
            const Point3  P{ch.origin.x + q.x, ch.origin.y + q.y, ch.origin.z + q.z};
            double        sx = 0.0, sy = 0.0;
            if (!screenOf(*c.proj, P, sx, sy))
                continue;
            const double d   = std::hypot(c.px - sx, c.py - sy);
            const double tol = tolAt(c, c.pointTol, P) + hPx +
                               (hW > 0.0 ? hW * ppu(c, P) : 0.0);
            const double t = rayParam(c.ray, P);
            if (d > tol || (c.maxDistance > 0.0 && t > c.maxDistance))
                continue;
            SceneHit h;
            h.kind     = HitKind::Point;
            h.element  = pd->order.empty() ? j : pd->order[j];
            h.point    = P;
            h.distance = t;
            h.screen   = d;
            out.push_back(h);
        }
    }
}

} // namespace

void pickShape3D(const Shape& s, const PickContext& c, std::vector<SceneHit>& out)
{
    switch (s.kind()) {
    case Shape::Kind::Mesh:
    case Shape::Kind::Area3:     pickMeshShape(s, c, out); break;
    case Shape::Kind::Instances: pickInstances(s, c, out); break;
    case Shape::Kind::Points3:   pickPoints(s, c, out); break;
    case Shape::Kind::Lines3:    pickLine3(s, s.points3(), false, c, out); break;
    case Shape::Kind::None:
    case Shape::Kind::Lines:
    case Shape::Kind::Area:
    case Shape::Kind::Text:
    case Shape::Kind::Markers:
    case Shape::Kind::Image:
    case Shape::Kind::Text3:
    case Shape::Kind::Markers3:
    case Shape::Kind::Pass:      break;   // helskärmspasset plockas inte
    }
}

void pickShape3D(const ShapeEntry& entry, const PickContext& c,
                 std::vector<SceneHit>& out)
{
    const Shape& s = entry.shape;
    // Bara en mesh och en instansmängd i världens enhet: varje träff ligger i
    // lådan uppblåst med konen — ytan, och hörnens och kanternas fotpunkter.
    // En yta i rymden har sin kontur och ett punktmoln sina punkters storlek
    // utanför den, och en skärmkonstant mesh har ingen låda i världen.
    if ((s.kind() == Shape::Kind::Mesh || s.kind() == Shape::Kind::Instances) &&
        !(s.meshStyle().size.value > 0.0f)) {
        const std::optional<Box3>& b = objectBox(entry);
        if (!b || !rayNearBox(c.ray, *b, boxReach(c, *b)))
            return;
    }
    pickShape3D(s, c, out);
}

Occluder occluder(const ShapeEntry& entry, float layerOpacity)
{
    Occluder o;
    o.shape        = &entry.shape;
    o.layerOpacity = layerOpacity;
    // En skärmkonstant mesh har sin låda i skärmens enhet: objectBox svarar
    // med origot, som inte rymmer ytan, så den får ingen.
    if (!(entry.shape.meshStyle().size.value > 0.0f))
        o.box = objectBox(entry);
    return o;
}

std::optional<double> nearestOccluder(std::span<const Occluder> shapes,
                                      const Ray3& ray, const gfx::View3D* view)
{
    // Bara ytor (ingen skärmprojektion) och bara de ogenomskinliga — en
    // genomskinlig triangel framför en ogenomskinlig får inte dölja den.
    PickContext c;
    c.ray        = ray;
    c.view       = view;
    c.opaqueOnly = true;
    c.occluder   = true;
    // FRAMIFRÅN: var strålen går in i varje låda — en form utan låda först,
    // och de lådor strålen inte når inte alls.
    struct Kö {
        double          in;
        const Occluder* o;
    };
    std::vector<Kö> kö;
    kö.reserve(shapes.size());
    for (const Occluder& o : shapes) {
        if (!o.shape)
            continue;
        if (!o.box)
            kö.push_back({0.0, &o});
        else if (const std::optional<double> in = rayBoxEntry(ray, *o.box, 0.0))
            kö.push_back({*in, &o});
    }
    std::sort(kö.begin(), kö.end(),
              [](const Kö& a, const Kö& b) { return a.in < b.in; });
    std::vector<SceneHit> hits;
    std::optional<double> bäst;
    for (const Kö& k : kö) {
        // En låda som börjar bortom den närmaste ytan har inget närmare att
        // svara med — och inte heller någon låda efter den. Med skymningens
        // marginal mot avrundningen, som plockningens tak (`reaches`).
        if (bäst && k.in > *bäst + occlusionSlack(*bäst))
            break;
        c.layerOpacity = k.o->layerOpacity;
        c.maxDistance  = bäst ? *bäst : 0.0;
        hits.clear();
        pickShape3D(*k.o->shape, c, hits);
        for (const SceneHit& h : hits)
            if (h.occludes && (!bäst || h.distance < *bäst))
                bäst = h.distance;
    }
    return bäst;
}

double occlusionSlack(double d)
{
    // Relativt avståndet (avrundningen i en träff långt bort) plus ett golv
    // för det som ligger nära strålens början.
    return std::abs(d) * 1e-6 + 1e-9;
}

double occlusionReach(double d)
{
    // Två marginaler bortom d: en träff där står mer än sin egen marginal
    // bakom skymmaren (marginalen växer bara med en miljondel av avståndet),
    // och allt längre bort likaså — taket är aldrig för kort, och provet efter
    // plockningen är det exakta.
    return d + 2.0 * occlusionSlack(d);
}

Ray3 verticalRayDown(double x, double y, const Box3& box)
{
    // Lådans höjd ovanför toppen, minst en världsenhet — en platt låda har
    // ingen höjd att räkna med.
    const double topp = box.max.z + std::max(1.0, box.max.z - box.min.z);
    return Ray3{{x, y, topp}, {0.0, 0.0, -1.0}};
}

} // namespace glaze::detail
