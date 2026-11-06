// Ritytans klipp som masker — poolen, projektionen av ytorna, rutnäten och
// nästlingen. Kontraktet och skälen står i clip.h.

#include "clip.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <gfx/camera.h>
#include <gfx/geom2d.h>
#include <gfx/platform.h>

#include "camera_math.h"
#include "resources.h"
#include "ring.h"
#include "scene.h"

namespace glaze::detail {

namespace {

// Maskernas mått: ett rutnät klampas till så många px per sida, och en mask
// avrundas uppåt till en multipel (poolen återanvänder masker av samma mått).
constexpr int kMaxMaskPx = 8192;
constexpr int kMaskStep  = 16;

// Världsmaskens tak i maskpx per sida: upplösningen följer skärmen men en
// kamera över en hel värld får inte ge en gigantisk bild.
constexpr double kMaxWorldMaskPx = 4096.0;

// Världsmaskens RUTA i maskpx: när klippets låda inte ryms i taket täcker
// masken den synliga marken i hela rutor, så den byggs om först när marken
// når en ny ruta.
constexpr double kWorldTile = 256.0;

// Hur många gånger världsmaskens täthet halveras innan gittret ger upp.
constexpr int kMaxDensitySteps = 64;

// En OKTAV i tätheten: världsmaskens täthet halveras med den när det som
// behövs inte ryms, och ett gitter som står kvar godtas så länge dess täthet
// ligger inom en oktav åt båda hållen från den som behövs.
constexpr double kDensityOctave = 2.0;

// Världsmaskens marginal för avrundningen till hela maskpx: gittrets början
// avrundas nedåt och slutet uppåt, högst en maskpx var.
constexpr double kLatticeSlackPx = 2.0;

// Maskens STORLEKSKLASS: steget är tvåpotensen under måttet delad med detta —
// en mask blir högst en åttondel större än det den behöver.
constexpr int kSizeClassDivisor = 8;

// Ett HÅLS GOLV mot ögats plan, som multipel av ytterringens: hålet klipps
// längre från ögat, så att dess klippkant aldrig når ytterringens.
constexpr double kHoleFloorFactor = 2.0;

// En världsmask i 2D får ha högst så många gånger ritytans px — en större
// låda vid den tätheten ritas med en skärmmask (clip.h).
constexpr double kMaxWorldMaskShare = 4.0;

// Hur nära ögats plan en punkt får ligga vid projektionen, som andel av
// ringens största W — närmare går dess bild mot oändligheten.
constexpr double kNearW = 1e-6;

// Hur långt från axelparallell kartvyns bild får vara, relativt skalan, för
// att en världsmask ska bindas som skärmmask.
constexpr double kAxisTolerance = 1e-9;

// Maskens tomma hörn: utanför varje rutnät.
constexpr double kEmptyOrigin = -8.0;

const gfx::Color kTransparent{0, 0, 0, 0};

std::uint64_t nonZero(std::uint64_t h)
{
    return h == 0 ? 1 : h;
}

// ---- nyckeln: kamerans parametrar och rutnäten, aldrig hörnen

void mixRect(KeyHash& k, const gfx::Rectf& r)
{
    k.mix(static_cast<double>(r.x));
    k.mix(static_cast<double>(r.y));
    k.mix(static_cast<double>(r.w));
    k.mix(static_cast<double>(r.h));
}

void mixView(KeyHash& k, const gfx::MapView& v)
{
    k.mix(v.cx);
    k.mix(v.cy);
    k.mix(v.pxPerUnit);
    k.mix(v.bearingDeg);
    k.mix(v.pitchDeg);
    k.mix(static_cast<std::uint64_t>(v.yUp));
    mixRect(k, v.box);
}

void mixView(KeyHash& k, const gfx::View3D& v)
{
    for (const gfx::Vec3d& p : {v.eye, v.forward, v.up}) {
        k.mix(p.x);
        k.mix(p.y);
        k.mix(p.z);
    }
    k.mix(static_cast<std::uint64_t>(v.projection));
    k.mix(v.fovY);
    k.mix(v.orthoHalfHeight);
    k.mix(v.nearZ);
    k.mix(v.farZ);
    mixRect(k, v.box);
}

void mixGrid(KeyHash& k, const MaskGrid& g)
{
    k.mix(g.x0);
    k.mix(g.y0);
    k.mix(g.s);
    k.mix(static_cast<std::uint64_t>(g.w));
    k.mix(static_cast<std::uint64_t>(g.h));
}

// ---- ringarna

bool finite(const Point& p)
{
    return std::isfinite(p.x) && std::isfinite(p.y);
}

// Ringen utan upprepade punkter och utan den upprepade slutpunkten. false när
// en punkt inte är ändlig.
bool cleanRing(const std::vector<Point>& in, ClipRing& out)
{
    out.clear();
    out.reserve(in.size());
    for (const Point& p : in) {
        if (!finite(p))
            return false;
        if (out.empty() || out.back().x != p.x || out.back().y != p.y)
            out.push_back(gfx::Point2d{p.x, p.y});
    }
    while (out.size() > 1 && out.front().x == out.back().x &&
           out.front().y == out.back().y)
        out.pop_back();
    return true;
}

// Ytan i rymdens enhet, rakt av. false när en punkt inte är ändlig.
bool plainArea(const Polygon& p, ClipArea& out)
{
    out.assign(1 + p.holes.size(), ClipRing{});
    if (!cleanRing(p.outer, out[0]))
        return false;
    for (std::size_t i = 0; i < p.holes.size(); ++i)
        if (!cleanRing(p.holes[i], out[1 + i]))
            return false;
    return true;
}

// Ringens största W genom kameran — ögats avstånd längs blicken. W är affin i
// planet, så ett håls största W är aldrig större än dess ytterrings.
double ringMaxW(const Projection& proj, const ClipRing& in)
{
    double maxW = 0.0;
    for (const gfx::Point2d& p : in) {
        double c[4];
        proj.clip(p.x, p.y, 0.0, c);
        maxW = std::max(maxW, c[3]);
    }
    return maxW;
}

// Ringen genom kameran till målets px: i det HOMOGENA klippt mot planet
// W = `golv` (en punkt bakom ögat har ingen bild) och sedan delad med W. Tom
// när hela ringen ligger bakom golvet.
void projectRing(const Projection& proj, const ClipRing& in, double golv,
                 ClipRing& out)
{
    struct Hom {
        double x, y, w;
    };
    std::vector<Hom> h;
    h.reserve(in.size());
    for (const gfx::Point2d& p : in) {
        double c[4];
        proj.clip(p.x, p.y, 0.0, c);
        h.push_back(Hom{c[0], c[1], c[3]});
    }
    out.clear();
    if (!(golv > 0.0))
        return;
    for (std::size_t i = 0, n = h.size(); i < n; ++i) {
        const Hom&   u  = h[i];
        const Hom&   v  = h[(i + 1) % n];
        const double du = u.w - golv, dv = v.w - golv;
        if (du >= 0.0)
            out.push_back({u.x / u.w, u.y / u.w});
        if ((du >= 0.0) != (dv >= 0.0)) {
            const double t = du / (du - dv);
            const double w = u.w + (v.w - u.w) * t;
            out.push_back({(u.x + (v.x - u.x) * t) / w, (u.y + (v.y - u.y) * t) / w});
        }
    }
}

// Lådan kring ytorna.
struct Bounds {
    double x0 = std::numeric_limits<double>::max();
    double y0 = std::numeric_limits<double>::max();
    double x1 = std::numeric_limits<double>::lowest();
    double y1 = std::numeric_limits<double>::lowest();
    bool   any() const { return x1 >= x0 && y1 >= y0; }
    void   add(double x, double y)
    {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
    }
    void add(const ClipRing& r)
    {
        for (const gfx::Point2d& p : r)
            add(p.x, p.y);
    }
};

Bounds boundsOf(const std::vector<ClipArea>& areas)
{
    Bounds b;
    for (const ClipArea& a : areas)
        if (!a.empty())
            b.add(a[0]);   // hålen ligger inuti ytterringen
    return b;
}

// Lådan kring de ritade ytornas ytterringar i rymdens enhet, utan kopia.
// false när en punkt — också i ett hål — inte är ändlig.
bool polygonBounds(const ClipPolys& polys, Bounds& b)
{
    const auto ändlig = [](const std::vector<Point>& r) {
        return std::all_of(r.begin(), r.end(), [](const Point& p) { return finite(p); });
    };
    for (std::size_t i = 0; i < polys.polys.size(); ++i) {
        if (!polys.drawn(i))
            continue;
        const Polygon& p = polys.polys[i];
        if (!ändlig(p.outer) || !std::all_of(p.holes.begin(), p.holes.end(), ändlig))
            return false;
        for (const Point& q : p.outer)
            b.add(q.x, q.y);
    }
    return true;
}

// ---- rutnäten

int roundUp(int v, int step)
{
    return (v + step - 1) / step * step;
}

} // namespace

// MÅTTETS STORLEKSKLASS (clip.h).
int sizeClassStep(int v)
{
    int pot = kMaskStep;
    while (pot * 2 <= v && pot < kMaxMaskPx)
        pot *= 2;
    return std::max(kMaskStep, pot / kSizeClassDivisor);
}

int sizeClass(int v)
{
    const int q = roundUp(v, sizeClassStep(v));
    return q <= kMaxMaskPx ? q : v;
}

namespace {

// Rutnätet för lådan [x0, x1] × [y0, y1] på gittret `lat`: hela maskpixlar,
// inom gittrets ruta. false när lådan ligger utanför.
bool fitGrid(const Bounds& b, const MaskGrid& lat, MaskGrid& out)
{
    const double ex1 = lat.x0 + lat.w / lat.s;
    const double ey1 = lat.y0 + lat.h / lat.s;
    const double x0  = std::max(b.x0, lat.x0), y0 = std::max(b.y0, lat.y0);
    const double x1  = std::min(b.x1, ex1), y1 = std::min(b.y1, ey1);
    if (!(x1 > x0) || !(y1 > y0))
        return false;
    const int i0 = std::clamp(static_cast<int>(std::floor((x0 - lat.x0) * lat.s)),
                              0, lat.w);
    const int i1 = std::clamp(static_cast<int>(std::ceil((x1 - lat.x0) * lat.s)),
                              0, lat.w);
    const int j0 = std::clamp(static_cast<int>(std::floor((y0 - lat.y0) * lat.s)),
                              0, lat.h);
    const int j1 = std::clamp(static_cast<int>(std::ceil((y1 - lat.y0) * lat.s)),
                              0, lat.h);
    if (i1 <= i0 || j1 <= j0)
        return false;
    out.s  = lat.s;
    out.x0 = lat.x0 + i0 / lat.s;
    out.y0 = lat.y0 + j0 / lat.s;
    out.w  = sizeClass(i1 - i0);
    out.h  = sizeClass(j1 - j0);
    return true;
}

// Skärmens gitter: målets px, hela pixlar, skalan 1.
MaskGrid screenLimit(const PaintContext& f)
{
    const gfx::Rectf& b = f.screen.box;
    MaskGrid          g;
    g.x0 = std::floor(static_cast<double>(b.x));
    g.y0 = std::floor(static_cast<double>(b.y));
    g.s  = 1.0;
    g.w  = std::clamp(static_cast<int>(std::ceil(static_cast<double>(b.x) + b.w - g.x0)),
                      1, kMaxMaskPx);
    g.h  = std::clamp(static_cast<int>(std::ceil(static_cast<double>(b.y) + b.h - g.y0)),
                      1, kMaxMaskPx);
    return g;
}

// Förälderns ruta i barnets maskpx när de ligger på SAMMA gitter (heltal).
gfx::Rectf parentRect(const MaskGrid& parent, const MaskGrid& child)
{
    return gfx::Rectf{
        static_cast<float>(std::round((parent.x0 - child.x0) * child.s)),
        static_cast<float>(std::round((parent.y0 - child.y0) * child.s)),
        static_cast<float>(parent.w), static_cast<float>(parent.h)};
}

// Förälderns SKÄRMMASK i en skärmmasks px (skalan 1): rutan den binds med,
// flyttad till barnets origo — också för en världsmask bunden som skärmmask.
gfx::Rectf parentScreenRect(const ClipLook& parent, const MaskGrid& child)
{
    return gfx::Rectf{static_cast<float>(parent.sRect.x - child.x0),
                      static_cast<float>(parent.sRect.y - child.y0),
                      parent.sRect.w, parent.sRect.h};
}

gfx::Rectf gridRect(const MaskGrid& g)
{
    return gfx::Rectf{static_cast<float>(g.x0), static_cast<float>(g.y0),
                      static_cast<float>(g.w), static_cast<float>(g.h)};
}

// Allt bortklippt på skärmen resp. i världen.
ClipLook emptyScreen(ClipMasks& pool, const ClipLook& parent)
{
    ClipLook out = parent;
    out.screen   = pool.empty();
    out.sg       = MaskGrid{kEmptyOrigin, kEmptyOrigin, 1.0, 1, 1};
    out.sRect    = gridRect(out.sg);
    out.sKey     = 1;
    out.planned  = false;
    return out;
}

ClipLook emptyWorld(ClipMasks& pool, const ClipLook& parent)
{
    ClipLook out = parent;
    out.world    = pool.empty();
    out.wg       = MaskGrid{0.0, 0.0, 1.0, 1, 1};
    out.wKey     = 1;
    return out;
}

// En skärmmask på rutnätet `g` (målets px).
ClipLook screenPlaced(const ClipLook& parent, const MaskGrid& g,
                      const gfx::Image* image, std::uint64_t key)
{
    ClipLook out = parent;
    out.screen   = image;
    out.sg       = g;
    out.sRect    = gridRect(g);
    out.sKey     = key;
    out.planned  = false;
    return out;
}

ClipLook worldPlaced(const ClipLook& parent, const MaskGrid& g,
                     const gfx::Image* image, std::uint64_t key)
{
    ClipLook out = parent;
    out.world    = image;
    out.wg       = g;
    out.wKey     = key;
    return out;
}

// ---- ytorna i maskens rymd

// Ytorna i LOKALA logiska px till målets px. false när en punkt inte är ändlig.
bool screenAreas(const PaintContext& f, const ClipPolys& polys,
                 std::vector<ClipArea>& out)
{
    for (std::size_t i = 0; i < polys.polys.size(); ++i) {
        if (!polys.drawn(i))
            continue;
        ClipArea a;
        if (!plainArea(polys.polys[i], a))
            return false;
        for (ClipRing& r : a)
            for (gfx::Point2d& q : r) {
                double px = 0.0, py = 0.0;
                if (!gfx::project(f.screen, q.x, q.y, px, py))
                    return false;
                q = gfx::Point2d{px, py};
            }
        out.push_back(std::move(a));
    }
    return true;
}

// Ytorna i världen genom ritningens kamera till målets px (planet z = 0).
// Det som ligger helt bakom ögat faller bort — en yta utan ytterring ritar
// ingenting, ett hål som faller bort täcker ingenting. Ytterringen klipps
// mot W = kNearW × dess största W och hålen mot dubbla det: ett hål som
// korsar ögats plan tillsammans med ytterringen får då sin klippkant en bit
// INNANFÖR ytterringens, och ytan förblir en enkel yta med hål (klippkanten
// ligger långt utanför bilden, så skillnaden syns aldrig). false när en
// punkt inte är ändlig.
bool projectedAreas(const PaintContext& f, const ClipPolys& polys,
                    std::vector<ClipArea>& out)
{
    const Projection proj = f.view3D ? Projection(*f.view3D) : Projection(f.world);
    for (std::size_t i = 0; i < polys.polys.size(); ++i) {
        if (!polys.drawn(i))
            continue;
        ClipArea world;
        if (!plainArea(polys.polys[i], world))
            return false;
        ClipArea     a;
        const double golv = ringMaxW(proj, world[0]) * kNearW;
        for (std::size_t k = 0; k < world.size(); ++k) {
            ClipRing r;
            projectRing(proj, world[k], k == 0 ? golv : kHoleFloorFactor * golv, r);
            if (r.size() >= 3)
                a.push_back(std::move(r));
            else if (k == 0)
                break;   // ytterringen syns inte — hålen inte heller
        }
        if (!a.empty())
            out.push_back(std::move(a));
    }
    return true;
}

// Ytorna i världen, rakt av.
bool worldAreas(const ClipPolys& polys, std::vector<ClipArea>& out)
{
    for (std::size_t i = 0; i < polys.polys.size(); ++i) {
        if (!polys.drawn(i))
            continue;
        ClipArea a;
        if (!plainArea(polys.polys[i], a))
            return false;
        out.push_back(std::move(a));
    }
    return true;
}

// Ytorna till maskens px, på plats: (x − x0) · s.
void inMask(std::vector<ClipArea>& areas, const MaskGrid& g)
{
    for (ClipArea& a : areas)
        for (ClipRing& r : a)
            for (gfx::Point2d& p : r) {
                p.x = (p.x - g.x0) * g.s;
                p.y = (p.y - g.y0) * g.s;
            }
}

void logNotFinite()
{
    gfx::log("Canvas: ett klipp har en punkt som inte är ändlig — allt "
             "klipps bort");
}

// ---- masken för ett klipp

// En SKÄRMMASK: ytorna i målets px (ett skärmklipp genom de lokala px, ett
// världsklipp genom kameran), på förälderns gitter eller skärmens. Nyckeln
// räknas först; ytorna projiceras bara när poolen saknar masken.
ClipLook screenLook(const PaintContext& f, ClipMasks& pool, const ClipLook& parent,
                    ClipSpace space, const ClipPolys& polys, std::uint64_t source)
{
    const MaskGrid lat = parent.screen ? parent.sg : screenLimit(f);
    KeyHash        k;
    k.mix(std::uint64_t{1});
    k.mix(static_cast<std::uint64_t>(space));
    k.mix(source);
    if (space == ClipSpace::Screen)
        mixView(k, f.screen);
    else if (f.view3D)
        mixView(k, *f.view3D);
    else
        mixView(k, f.world);
    mixGrid(k, lat);
    k.mix(parent.sKey);
    const std::uint64_t key = nonZero(k.h);
    if (const std::optional<ClipMasks::Found> m = pool.find(key))
        return m->answer == ClipMasks::Answer::Mask
                   ? screenPlaced(parent, m->grid, m->image, key)
                   : emptyScreen(pool, parent);

    std::vector<ClipArea> areas;
    const bool ok = space == ClipSpace::Screen ? screenAreas(f, polys, areas)
                                               : projectedAreas(f, polys, areas);
    if (!ok)
        logNotFinite();
    const Bounds b = boundsOf(areas);
    MaskGrid     g;
    if (!ok || !b.any() || !fitGrid(b, lat, g)) {
        pool.remember(key, ClipMasks::Answer::Empty);
        return emptyScreen(pool, parent);
    }
    const gfx::Image* bild = pool.mask(
        key, g, parent.screen,
        parent.screen ? parentScreenRect(parent, g) : gfx::Rectf{},
        [&](ClipMasks::Areas& into) {
            inMask(areas, g);
            into = std::move(areas);
        });
    return screenPlaced(parent, g, bild, key);
}

// KARTVYNS BILD AV VÄRLDEN när den är axelparallell — ortografisk (tilt 0)
// och ovriden eller vriden ett halvt varv: px = ax + sx·(x − cx),
// py = ay + sy·(y − cy). Tomt för en vriden eller lutad vy.
struct AxisMap {
    double cx = 0.0, cy = 0.0, ax = 0.0, ay = 0.0, sx = 0.0, sy = 0.0;
};

std::optional<AxisMap> axisMap(const gfx::MapView& v)
{
    if (v.pitchDeg != 0.0)
        return std::nullopt;
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
    if (!gfx::project(v, v.cx, v.cy, x0, y0) ||
        !gfx::project(v, v.cx + 1.0, v.cy, x1, y1) ||
        !gfx::project(v, v.cx, v.cy + 1.0, x2, y2))
        return std::nullopt;
    const double sx = x1 - x0, sy = y2 - y0;
    const double skala = std::max(std::abs(sx), std::abs(sy));
    if (!(skala > 0.0) || !std::isfinite(skala) ||
        !(std::abs(y1 - y0) <= kAxisTolerance * skala) ||
        !(std::abs(x2 - x0) <= kAxisTolerance * skala))
        return std::nullopt;
    return AxisMap{v.cx, v.cy, x0, y0, sx, sy};
}

// Världsmaskens ruta i målets px — kamerans del, räknad varje ritning.
gfx::Rectf planRect(const AxisMap& m, const MaskGrid& g)
{
    return gfx::Rectf{static_cast<float>(m.ax + m.sx * (g.x0 - m.cx)),
                      static_cast<float>(m.ay + m.sy * (g.y0 - m.cy)),
                      static_cast<float>(m.sx * g.w / g.s),
                      static_cast<float>(m.sy * g.h / g.s)};
}

// Ett VÄRLDSKLIPP I 2D som världsmask bunden som skärmmask (clip.h) — tomt
// när skärmmasken ska gälla: en vriden eller lutad kamera, en förälder med
// en skärmmask, eller en låda som i skärmens täthet inte ryms.
std::optional<ClipLook> planLook(const PaintContext& f, ClipMasks& pool,
                                 const ClipLook& parent, const ClipPolys& polys,
                                 std::uint64_t source)
{
    if (parent.screen && !parent.planned)
        return std::nullopt;
    const std::optional<AxisMap> m = axisMap(f.world);
    if (!m)
        return std::nullopt;
    // TÄTHETEN: minst en maskpx per skärmpx, en tvåpotens så att en zoom
    // inom samma oktav behåller masken. Ett barn ligger på förälderns gitter.
    const double s = parent.planned
                         ? parent.plan.s
                         : std::exp2(std::ceil(std::log2(
                               std::max(std::abs(m->sx), std::abs(m->sy)))));
    if (!(s > 0.0) || !std::isfinite(s))
        return std::nullopt;
    KeyHash k;
    k.mix(std::uint64_t{4});
    k.mix(source);
    k.mix(s);
    if (parent.planned) {
        mixGrid(k, parent.plan);
        k.mix(parent.planKey);
    } else {
        k.mix(static_cast<double>(f.world.box.w));   // taket följer ytan
        k.mix(static_cast<double>(f.world.box.h));
    }
    const std::uint64_t key = nonZero(k.h);

    MaskGrid          g;
    const gfx::Image* bild = nullptr;
    if (const std::optional<ClipMasks::Found> svar = pool.find(key)) {
        if (svar->answer == ClipMasks::Answer::Fallback)
            return std::nullopt;
        if (svar->answer == ClipMasks::Answer::Empty)
            return emptyScreen(pool, parent);
        g    = svar->grid;
        bild = svar->image;
    } else {
        Bounds b;
        if (!polygonBounds(polys, b)) {
            logNotFinite();
            pool.remember(key, ClipMasks::Answer::Empty);
            return emptyScreen(pool, parent);
        }
        MaskGrid lat = parent.plan;
        if (!parent.planned && b.any()) {
            // GITTRET i världen: lådan, utåt till hela maskpixlar i tätheten.
            lat.s        = s;
            lat.x0       = std::floor(b.x0 * s) / s;
            lat.y0       = std::floor(b.y0 * s) / s;
            const double w = std::max(1.0, std::ceil((b.x1 - lat.x0) * s));
            const double h = std::max(1.0, std::ceil((b.y1 - lat.y0) * s));
            const double yta = std::max(1.0, static_cast<double>(f.world.box.w) *
                                                 static_cast<double>(f.world.box.h));
            if (w > kMaxWorldMaskPx || h > kMaxWorldMaskPx ||
                w * h > kMaxWorldMaskShare * yta) {
                pool.remember(key, ClipMasks::Answer::Fallback);
                return std::nullopt;
            }
            lat.w = static_cast<int>(w);
            lat.h = static_cast<int>(h);
        }
        if (!b.any() || !fitGrid(b, lat, g)) {
            pool.remember(key, ClipMasks::Answer::Empty);
            return emptyScreen(pool, parent);
        }
        bild = pool.mask(key, g, parent.planned ? parent.screen : nullptr,
                         parent.planned ? parentRect(parent.plan, g) : gfx::Rectf{},
                         [&](ClipMasks::Areas& into) {
                             worldAreas(polys, into);   // ändligheten är prövad
                             inMask(into, g);
                         });
    }
    ClipLook out = parent;
    out.screen   = bild;
    out.planned  = true;
    out.plan     = g;
    out.planKey  = key;
    out.sRect    = planRect(*m, g);
    // Skärmmaskernas gitter inuti den: rutan i målets px, på skärmen.
    Bounds ruta;
    ruta.add(out.sRect.x, out.sRect.y);
    ruta.add(static_cast<double>(out.sRect.x) + out.sRect.w,
             static_cast<double>(out.sRect.y) + out.sRect.h);
    if (!fitGrid(ruta, screenLimit(f), out.sg))
        out.sg = MaskGrid{kEmptyOrigin, kEmptyOrigin, 1.0, 0, 0};
    KeyHash sk;
    sk.mix(key);
    mixRect(sk, out.sRect);
    out.sKey = nonZero(sk.h);
    return out;
}

// Den SYNLIGA MARKEN i 3D-målet: den del av planen z = scenens lägsta..högsta
// höjd som vyn ser (`syn`), och tätheten som följer skärmen där (`s`, maskpx
// per världsenhet vid markpunkten längst ner i bilden). false när vyn inte ser
// någon mark.
bool visibleGround(const PaintContext& f, Bounds& syn, double& s)
{
    const gfx::View3D&  v   = *f.view3D;
    double              zlo = 0.0, zhi = 0.0;
    std::optional<Box3> scen;
    if (f.scene)
        if ((scen = sceneBox(*f.scene, f.view))) {
            zlo = std::min(0.0, scen->min.z);
            zhi = std::max(0.0, scen->max.z);
        }
    Bounds b;
    for (const double z : {zlo, zhi}) {
        const std::vector<gfx::Vec3d> reg = gfx::visiblePlaneRegion(
            v, gfx::Plane3d{gfx::Vec3d{0.0, 0.0, z}, gfx::Vec3d{0.0, 0.0, 1.0}},
            0.0);
        for (const gfx::Vec3d& p : reg)
            b.add(p.x, p.y);
        if (zlo == zhi)
            break;
    }
    // Ser vyn inget av de två planen (en vågrät blick mellan dem) gäller
    // scenens låda i XY — det synliga ligger där.
    if (!b.any() && scen) {
        b.x0 = scen->min.x;
        b.y0 = scen->min.y;
        b.x1 = scen->max.x;
        b.y1 = scen->max.y;
    }
    if (!b.any() || !std::isfinite(b.x0) || !std::isfinite(b.y0) ||
        !std::isfinite(b.x1) || !std::isfinite(b.y1))
        return false;
    if (!(b.x1 > b.x0) || !(b.y1 > b.y0))
        return false;
    const gfx::Rectf& box = v.box;
    const Projection  proj(v);
    s = proj.groundScale(box.x + box.w / 2.0, box.y + box.h - 1.0);
    if (!(s > 0.0) || !std::isfinite(s))
        s = f.world.pxPerUnit;
    if (!(s > 0.0) || !std::isfinite(s))
        return false;
    syn = b;
    return true;
}

// Täcker gittret rutan [x0, x1] × [y0, y1]?
bool covers(const MaskGrid& g, double x0, double y0, double x1, double y1)
{
    return g.x0 <= x0 && g.y0 <= y0 && g.x0 + g.w / g.s >= x1 &&
           g.y0 + g.h / g.s >= y1;
}

// Spannet [a0, a1] i världen på tätheten s: dess mått i hela maskpx från
// gittrets början (a0 nedåt till en hel maskpx).
double latticeSpan(double a0, double a1, double s)
{
    return std::ceil((a1 - std::floor(a0 * s) / s) * s);
}

// Det som behövs, [a0, a1], utvidgat på båda sidor inom budgeten
// (`kMaxWorldMaskPx` maskpx på tätheten s) i hela rutor om `kWorldTile`, och
// inom lådan [l0, l1]. Ryms ingen hel ruta på var sida står spannet som det är.
void widen(double& a0, double& a1, double l0, double l1, double s)
{
    const double t   = kWorldTile / s;                          // rutans sida
    const double rum = (kMaxWorldMaskPx - kLatticeSlackPx) / s - (a1 - a0);   // marginalen
    const double m   = rum / 2.0 - t;   // en ruta lämnas åt snäppningen per sida
    if (m <= 0.0)
        return;
    a0 = std::max(l0, std::floor((a0 - m) / t) * t);
    a1 = std::min(l1, std::ceil((a1 + m) / t) * t);
}

// VÄRLDSMASKENS GITTER utan förälder — oberoende av vyn, så en kamerarörelse
// inte bygger en ny mask. Tätheten är markskalans tvåpotens uppåt (minst en
// maskpx per skärmpx längst ner i bilden). Det som BEHÖVS är klippets låda
// (`lådan`), eller — när den inte ryms i `kMaxWorldMaskPx` per sida — det
// synliga av den; ryms inte heller det halveras tätheten tills det gör det.
// Gittret som SENAST användes för klippet (`key`, i poolen) står kvar så
// länge det täcker det som behövs i en täthet inom en oktav. Ett nytt gitter
// är lådan, eller det synliga utvidgat runt sin mitt till hela budgeten i
// rutor om `kWorldTile` maskpx — så att en kamera som rör sig inom det inte
// ger ett nytt. false när vyn inte ser någon mark eller klippet inte syns.
bool worldGrid(const PaintContext& f, ClipMasks& pool, std::uint64_t key,
               const Bounds& lådan, MaskGrid& out)
{
    Bounds syn;
    double s = 0.0;
    if (!visibleGround(f, syn, s))
        return false;
    s = std::exp2(std::ceil(std::log2(s)));
    for (int varv = 0; varv < kMaxDensitySteps && s > 0.0 && std::isfinite(s); ++varv) {
        // Det som behövs i den här tätheten.
        double x0 = lådan.x0, y0 = lådan.y0, x1 = lådan.x1, y1 = lådan.y1;
        const bool helaLådan = latticeSpan(x0, x1, s) <= kMaxWorldMaskPx &&
                               latticeSpan(y0, y1, s) <= kMaxWorldMaskPx;
        if (!helaLådan) {
            x0 = std::max(x0, syn.x0);
            y0 = std::max(y0, syn.y0);
            x1 = std::min(x1, syn.x1);
            y1 = std::min(y1, syn.y1);
            if (!(x1 > x0) || !(y1 > y0))
                return false;   // klippet ligger utanför det synliga
            if (latticeSpan(x0, x1, s) > kMaxWorldMaskPx ||
                latticeSpan(y0, y1, s) > kMaxWorldMaskPx) {
                s /= kDensityOctave;
                continue;
            }
        }
        // DET SENASTE GITTRET står kvar så länge det räcker.
        if (const MaskGrid* förra = pool.lastGrid(key);
            förra && förra->s >= s / kDensityOctave && förra->s <= s * kDensityOctave &&
            covers(*förra, x0, y0, x1, y1)) {
            out = *förra;
            return true;
        }
        // Ett nytt: lådan, eller det synliga utvidgat inom budgeten.
        if (!helaLådan) {
            widen(x0, x1, lådan.x0, lådan.x1, s);
            widen(y0, y1, lådan.y0, lådan.y1, s);
        }
        const double gx0 = std::floor(x0 * s) / s, gy0 = std::floor(y0 * s) / s;
        out = MaskGrid{gx0, gy0, s,
                       sizeClass(std::max(1, static_cast<int>(latticeSpan(x0, x1, s)))),
                       sizeClass(std::max(1, static_cast<int>(latticeSpan(y0, y1, s))))};
        pool.rememberGrid(key, out);
        return true;
    }
    return false;
}

// VÄRLDSMASKEN i 3D-målet (prismat). Nyckeln är källan och GITTRET —
// förälderns gitter och nyckel, eller det vyoberoende gittret ur
// `worldGrid` —, så samma klipp på samma gitter är samma mask; masken och dess
// rutnät tas ur poolen när de redan finns.
ClipLook worldLook(const PaintContext& f, ClipMasks& pool, const ClipLook& parent,
                   const ClipPolys& polys, std::uint64_t source)
{
    KeyHash  k;
    MaskGrid lat = parent.wg;
    k.mix(std::uint64_t{2});
    k.mix(source);
    if (parent.world) {
        k.mix(parent.wKey);
    } else {
        Bounds lådan;
        if (!polygonBounds(polys, lådan)) {
            logNotFinite();
            return emptyWorld(pool, parent);
        }
        // Gittret som står kvar hör till klippet OCH vyn: en del-vy ser en
        // annan mark än huvudvyn.
        KeyHash fäste;
        fäste.mix(std::uint64_t{5});
        fäste.mix(source);
        fäste.mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(f.view)));
        if (!lådan.any() || !worldGrid(f, pool, nonZero(fäste.h), lådan, lat))
            return emptyWorld(pool, parent);   // ingenting av klippet syns
    }
    mixGrid(k, lat);
    const std::uint64_t key = nonZero(k.h);
    if (const std::optional<ClipMasks::Found> m = pool.find(key))
        return m->answer == ClipMasks::Answer::Mask
                   ? worldPlaced(parent, m->grid, m->image, key)
                   : emptyWorld(pool, parent);

    std::vector<ClipArea> areas;
    if (!worldAreas(polys, areas)) {
        logNotFinite();
        pool.remember(key, ClipMasks::Answer::Empty);
        return emptyWorld(pool, parent);
    }
    const Bounds b = boundsOf(areas);
    MaskGrid     g;
    if (!b.any() || !fitGrid(b, lat, g)) {
        pool.remember(key, ClipMasks::Answer::Empty);
        return emptyWorld(pool, parent);
    }
    const gfx::Image* bild = pool.mask(
        key, g, parent.world,
        parent.world ? parentRect(parent.wg, g) : gfx::Rectf{},
        [&](ClipMasks::Areas& into) {
            inMask(areas, g);
            into = std::move(areas);
        });
    return worldPlaced(parent, g, bild, key);
}

} // namespace

// ------------------------------------------------------------------ ClipLook

void ClipLook::apply(gfx::SceneClip& c, double ox, double oy) const
{
    if (screen) {
        c.screen     = screen;
        c.screenRect = sRect;
    }
    if (world) {
        const double sx = wg.s / wg.w, sy = wg.s / wg.h;
        c.world      = world;
        c.worldUv[0] = static_cast<float>(sx);
        c.worldUv[1] = static_cast<float>(sy);
        c.worldUv[2] = static_cast<float>((ox - wg.x0) * sx);
        c.worldUv[3] = static_cast<float>((oy - wg.y0) * sy);
    }
}

// ----------------------------------------------------------------- ClipMasks

ClipMasks::~ClipMasks() = default;

ClipMasks::Memo& ClipMasks::memo(std::uint64_t key)
{
    for (Memo& m : memos_)
        if (m.key == key)
            return m;
    memos_.push_back(Memo{});
    memos_.back().key = key;
    return memos_.back();
}

std::optional<ClipMasks::Found> ClipMasks::find(std::uint64_t key)
{
    const std::uint64_t serial = r_->frameSerial();
    for (Memo& m : memos_) {
        if (m.key != key)
            continue;
        if (m.answer != Answer::Mask) {
            m.used = serial;
            return Found{m.answer, m.grid, nullptr};
        }
        // Målet kan ha tagits av en annan nyckel — då byggs masken om.
        const TargetPool::Slot* s = targets_.find(key, serial);
        if (!s || !s->target)
            return std::nullopt;
        m.used = serial;
        return Found{Answer::Mask, m.grid, &s->target->image()};
    }
    return std::nullopt;
}

void ClipMasks::remember(std::uint64_t key, Answer a)
{
    Memo& m  = memo(key);
    m.answer = a;
    m.grid   = MaskGrid{};
    m.used   = r_->frameSerial();
}

void ClipMasks::fill(TargetPool::Slot& s, const gfx::Image* parent,
                     const gfx::Rectf& parentRect,
                     const std::function<void(Areas&)>& areas)
{
    Areas list;
    areas(list);
    gfx::MapView mv;
    mv.cx        = s.w / 2.0;
    mv.cy        = s.h / 2.0;
    mv.pxPerUnit = 1.0;
    mv.yUp       = false;
    mv.box       = gfx::Rectf{0.0f, 0.0f, static_cast<float>(s.w),
                              static_cast<float>(s.h)};
    r_->pushTarget(*s.target, kTransparent);
    std::vector<gfx::FillChunk> chunks;
    for (const ClipArea& a : list) {
        chunks.clear();
        if (!gfx::buildClipPolygon(a, chunks)) {
            gfx::log("Canvas: en klippyta går inte att fylla (en självkorsning, "
                     "ett hål utanför ytterringen eller för få punkter) — den "
                     "klipper bort sin del");
            ++unfilled_;
            continue;
        }
        for (const gfx::FillChunk& c : chunks) {
            if (c.indices.empty())
                continue;
            gfx::MeshDraw d;
            d.program  = gfx::MeshProgram::Fill;
            d.vertices = r_->uploadTransient(gfx::BufferKind::Vertex, c.vertices.data(),
                                             c.vertices.size() * sizeof(gfx::FillVertex));
            d.indices  = r_->uploadTransient(gfx::BufferKind::Index, c.indices.data(),
                                             c.indices.size() * sizeof(std::uint32_t));
            if (!d.vertices.valid() || !d.indices.valid())
                continue;
            d.indexCount = static_cast<std::uint32_t>(c.indices.size());
            gfx::viewProjection(mv, c.originX, c.originY).toFloat(d.uniforms.viewProj);
            if (parent) {
                d.uniforms.clip.screen     = parent;
                d.uniforms.clip.screenRect = parentRect;
            }
            r_->drawMesh(d);
        }
    }
    r_->popTarget();
    ++fills_;
}

const gfx::Image* ClipMasks::mask(std::uint64_t key, const MaskGrid& g,
                                  const gfx::Image* parent,
                                  const gfx::Rectf& parentRect,
                                  const std::function<void(Areas&)>& areas)
{
    if (g.w <= 0 || g.h <= 0)
        return empty();
    const std::uint64_t serial = r_->frameSerial();
    TargetPool::Slot&   s      = targets_.take(key, g.w, g.h, serial);
    if (!s.built) {   // `take` ger en träff byggd för nyckeln, annars obyggd
        if (!s.target || !s.target->image().valid()) {
            s.target = r_->createRenderTarget(g.w, g.h);
            if (!s.target || !s.target->image().valid()) {
                s.target.reset();
                gfx::log("Canvas: en klippmask (%d × %d) gick inte att skapa — "
                         "klippet klipper bort allt",
                         g.w, g.h);
                return empty();
            }
        }
        fill(s, parent, parentRect, areas);
        s.built = true;
    }
    Memo& m  = memo(key);
    m.answer = Answer::Mask;
    m.grid   = g;
    m.used   = serial;
    return &s.target->image();
}

const gfx::Image* ClipMasks::empty()
{
    if (!empty_) {
        std::unique_ptr<gfx::RenderTarget> t = r_->createRenderTarget(1, 1);
        if (!t || !t->image().valid()) {
            gfx::log("Canvas: den tomma klippmasken gick inte att skapa");
            return nullptr;
        }
        empty_ = std::move(t);
    }
    return &empty_->image();
}

const MaskGrid* ClipMasks::lastGrid(std::uint64_t key)
{
    const auto it = grids_.find(key);
    if (it == grids_.end())
        return nullptr;
    it->second.used = r_->frameSerial();
    return &it->second.grid;
}

void ClipMasks::rememberGrid(std::uint64_t key, const MaskGrid& g)
{
    grids_[key] = GridMemo{g, r_->frameSerial()};
}

bool ClipMasks::valid(std::uint64_t key, const std::function<bool()>& valid)
{
    const auto [it, ny] = validity_.try_emplace(key);
    if (ny)
        it->second.ok = valid();
    it->second.used = r_->frameSerial();
    return it->second.ok;
}

void ClipMasks::trim()
{
    const std::uint64_t serial = r_->frameSerial();
    targets_.trim(serial);
    std::erase_if(memos_, [serial](const Memo& m) { return m.used + kKeptFrames < serial; });
    std::erase_if(validity_, [serial](const auto& v) {
        return v.second.used + kKeptFrames < serial;
    });
    std::erase_if(grids_, [serial](const auto& g) {
        return g.second.used + kKeptFrames < serial;
    });
}

// -------------------------------------------------------------------- klippet

namespace {

// Ett klipp utan ytor: allt bortklippt (aldrig ett oklippt).
const std::shared_ptr<const ClipSnapshot>& noSurfaces()
{
    static const std::shared_ptr<const ClipSnapshot> tom =
        std::make_shared<const ClipSnapshot>();
    return tom;
}

// Kedjans klipp i skärmrymden: förälderns först, sedan stegets ytor som
// skärmmask — världsklippet projicerat genom vyn.
ClipLook chainOnScreen(const PaintContext& f, ClipMasks& pool, const ClipStep& step)
{
    const ClipLook parent = step.parent ? chainOnScreen(f, pool, *step.parent) : ClipLook{};
    return screenLook(f, pool, parent, step.space, step.snapshot->polys(), step.source);
}

} // namespace

ClipLook ClipScope::onScreen(const PaintContext& f) const
{
    if (!chain || !f.view3D || !f.res)
        return flat;
    return chainOnScreen(f, f.res->clips(), *chain);
}

ClipScope refineClip(const PaintContext& f, const ClipScope& parent, ClipSpace space,
                     const std::shared_ptr<const ClipSnapshot>& snapshot,
                     std::uint64_t source)
{
    ClipScope out = parent;
    if (!f.res)
        return out;
    const std::shared_ptr<const ClipSnapshot>& ytor = snapshot ? snapshot : noSurfaces();
    const ClipPolys polys = ytor->polys();
    ClipMasks&      pool  = f.res->clips();
    if (!f.view3D) {
        std::optional<ClipLook> plan;
        if (space == ClipSpace::World)
            plan = planLook(f, pool, parent.flat, polys, source);
        out.flat = plan ? *plan : screenLook(f, pool, parent.flat, space, polys, source);
        out.tall = out.flat;
        return out;
    }
    // 3D-MÅLET: 2D-programmen och 3D-programmen ser samma klipp — ett
    // världsklipp som världsmask, ett skärmklipp som skärmmask — och kedjan
    // bär ytorna till skärmrymdens innehåll (`onScreen`).
    out.tall  = space == ClipSpace::World
                    ? worldLook(f, pool, parent.tall, polys, source)
                    : screenLook(f, pool, parent.tall, space, polys, source);
    out.flat  = out.tall;
    out.chain = std::make_shared<const ClipStep>(ClipStep{parent.chain, space, ytor, source});
    return out;
}

ClipFrame prepareClips(const PaintContext& f, const SceneData& scene, ClipUse use)
{
    ClipFrame cf;
    if (use != ClipUse::Drape && clipApplies(scene.clip, scene.invalidClip))
        cf.canvas = refineClip(f, cf.canvas, scene.clip.space, scene.clip.snapshot,
                               scene.clip.content);
    // Utan lagerklipp står listan tom — `of()` ger då ritytans (det vanliga
    // fallet kostar ingen allokering per ritning).
    const bool någotLager = std::any_of(
        scene.layers.begin(), scene.layers.end(), [&](const auto& L) {
            return clipApplies(L->clip, scene.invalidClip);
        });
    if (!någotLager)
        return cf;
    cf.layers.assign(scene.layers.size(), cf.canvas);
    for (std::size_t i = 0; i < scene.layers.size(); ++i) {
        const LayerData& L = *scene.layers[i];
        // Ett lager som inte ritas får inget eget klipp; bakningen uppifrån
        // (draperingen) har ingen skärm, så bara världsklipp gäller där.
        if (!clipApplies(L.clip, scene.invalidClip) || !L.visible || !layerDrawn(f, L) ||
            (use == ClipUse::Drape && L.clip.space != ClipSpace::World))
            continue;
        cf.layers[i] = refineClip(f, cf.canvas, L.clip.space, L.clip.snapshot,
                                  L.clip.content);
    }
    return cf;
}

std::uint64_t clipVersions(const SceneData& scene)
{
    KeyHash k;
    k.mix(scene.clip.version);
    for (const std::shared_ptr<LayerData>& L : scene.layers) {
        k.mix(L->id);
        k.mix(L->clip.version);
    }
    return k.h;
}

// ----------------------------------------------------------------- träffprovet

namespace {

bool inPolygon(const Polygon& poly, Point p)
{
    if (poly.outer.size() < 3 ||
        !inRing(std::span<const Point>(poly.outer), p.x, p.y))
        return false;
    for (const std::vector<Point>& h : poly.holes)
        if (h.size() >= 3 && inRing(std::span<const Point>(h), p.x, p.y))
            return false;
    return true;
}

double cross(Point o, Point a, Point b)
{
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// Skär sträckorna ab och cd?
bool segmentsCross(Point a, Point b, Point c, Point d)
{
    const double d1 = cross(a, b, c), d2 = cross(a, b, d);
    const double d3 = cross(c, d, a), d4 = cross(c, d, b);
    return ((d1 > 0.0) != (d2 > 0.0)) && ((d3 > 0.0) != (d4 > 0.0));
}

bool ringCrossesQuad(const std::vector<Point>& r, std::span<const Point> quad)
{
    for (std::size_t i = 0, n = r.size(); i < n; ++i)
        for (std::size_t k = 0, m = quad.size(); k < m; ++k)
            if (segmentsCross(r[i], r[(i + 1) % n], quad[k], quad[(k + 1) % m]))
                return true;
    return false;
}

} // namespace

bool clipContains(const ClipPolys& polys, Point p)
{
    if (!finite(p))
        return false;
    for (std::size_t i = 0; i < polys.polys.size(); ++i)
        if (polys.drawn(i) && inPolygon(polys.polys[i], p))
            return true;
    return false;
}

bool clipTouches(const ClipPolys& polys, std::span<const Point> quad)
{
    if (quad.size() < 3)
        return false;
    for (std::size_t i = 0; i < polys.polys.size(); ++i) {
        if (!polys.drawn(i))
            continue;
        const Polygon& poly = polys.polys[i];
        for (const Point& q : quad)
            if (inPolygon(poly, q))
                return true;
        for (const Point& p : poly.outer)
            if (inRing(quad, p.x, p.y))
                return true;
        if (ringCrossesQuad(poly.outer, quad))
            return true;
        for (const std::vector<Point>& h : poly.holes)
            if (ringCrossesQuad(h, quad))
                return true;
    }
    return false;
}

// ----------------------------------------------------------------- giltigheten

bool clipPolygonValid(const Polygon& p)
{
    ClipArea a;
    return plainArea(p, a) && gfx::clipPolygonOk(a);
}

std::uint64_t polygonKey(const Polygon& p)
{
    KeyHash k;
    const auto ring = [&k](const std::vector<Point>& r) {
        k.mix(static_cast<std::uint64_t>(r.size()));
        for (const Point& q : r) {
            k.mix(q.x);
            k.mix(q.y);
        }
    };
    ring(p.outer);
    k.mix(static_cast<std::uint64_t>(p.holes.size()));
    for (const std::vector<Point>& h : p.holes)
        ring(h);
    return k.h;
}

void revalidateClip(ClipState& k, std::size_t first, std::size_t last)
{
    k.valid.resize(k.polygons.size(), 1);
    k.keys.resize(k.polygons.size(), 0);
    for (std::size_t i = first; i < last && i < k.polygons.size(); ++i) {
        k.valid[i] = clipPolygonValid(k.polygons[i]) ? 1 : 0;
        k.keys[i]  = polygonKey(k.polygons[i]);
    }
    const ClipPolys ritade = validPolygons(k);
    KeyHash         h;
    h.mix(static_cast<std::uint64_t>(k.space));
    std::size_t antal = 0;
    for (std::size_t i = 0; i < k.polygons.size(); ++i)
        if (ritade.drawn(i))
            ++antal;
    h.mix(static_cast<std::uint64_t>(antal));
    for (std::size_t i = 0; i < k.polygons.size(); ++i)
        if (ritade.drawn(i))
            h.mix(k.keys[i]);
    k.content  = h.h;
    k.snapshot = std::make_shared<const ClipSnapshot>(ClipSnapshot{k.polygons, k.valid});
}

namespace {

bool allValid(const ClipState& k)
{
    for (std::size_t i = 0; i < k.polygons.size() && i < k.valid.size(); ++i)
        if (!k.valid[i])
            return false;
    return true;
}

} // namespace

bool clipApplies(const ClipState& k, InvalidClip mode)
{
    return k.enabled && (mode == InvalidClip::Hide || allValid(k));
}

ClipPolys validPolygons(const ClipState& k)
{
    return ClipPolys{k.polygons, k.valid};
}

bool clipContains(const ClipState& k, Point p)
{
    return clipContains(validPolygons(k), p);
}

bool clipTouches(const ClipState& k, std::span<const Point> quad)
{
    return clipTouches(validPolygons(k), quad);
}

} // namespace glaze::detail
