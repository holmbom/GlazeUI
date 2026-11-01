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

// Hur nära ögats plan en punkt får ligga vid projektionen, som andel av
// ringens största W — närmare går dess bild mot oändligheten.
constexpr double kNearW = 1e-6;

// Maskens tomma hörn: utanför varje rutnät.
constexpr double kEmptyOrigin = -8.0;

const gfx::Color kTransparent{0, 0, 0, 0};

// ---- hashen över ytornas koordinater

void mixAreas(KeyHash& k, const std::vector<ClipArea>& areas)
{
    k.mix(static_cast<std::uint64_t>(areas.size()));
    for (const ClipArea& a : areas) {
        k.mix(static_cast<std::uint64_t>(a.size()));
        for (const ClipRing& r : a) {
            k.mix(static_cast<std::uint64_t>(r.size()));
            for (const gfx::Point2d& p : r) {
                k.mix(p.x);
                k.mix(p.y);
            }
        }
    }
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

// Ytan i rymdens enhet, rakt av. false (+ logg) när en punkt inte är ändlig.
bool plainArea(const ClipPolygon& p, ClipArea& out)
{
    out.assign(1 + p.holes.size(), ClipRing{});
    if (!cleanRing(p.outer, out[0]))
        return false;
    for (std::size_t i = 0; i < p.holes.size(); ++i)
        if (!cleanRing(p.holes[i], out[1 + i]))
            return false;
    return true;
}

// Ringen genom kameran till målets px: i det HOMOGENA klippt mot ögats plan
// (W ≥ kNearW × ringens största W — en punkt bakom ögat har ingen bild) och
// sedan delad med W. Tom när hela ringen ligger bakom ögat.
void projectRing(const Projection& proj, const ClipRing& in, ClipRing& out)
{
    struct Hom {
        double x, y, w;
    };
    std::vector<Hom> h;
    h.reserve(in.size());
    double maxW = 0.0;
    for (const gfx::Point2d& p : in) {
        double c[4];
        proj.clip(p.x, p.y, 0.0, c);
        h.push_back(Hom{c[0], c[1], c[3]});
        maxW = std::max(maxW, c[3]);
    }
    out.clear();
    if (!(maxW > 0.0))
        return;
    const double golv = maxW * kNearW;
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
    void   add(const ClipRing& r)
    {
        for (const gfx::Point2d& p : r) {
            x0 = std::min(x0, p.x);
            y0 = std::min(y0, p.y);
            x1 = std::max(x1, p.x);
            y1 = std::max(y1, p.y);
        }
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

// ---- rutnäten

int roundUp(int v, int step)
{
    return (v + step - 1) / step * step;
}

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
    out.w  = i1 - i0;
    out.h  = j1 - j0;
    const int qw = roundUp(out.w, kMaskStep), qh = roundUp(out.h, kMaskStep);
    if (qw <= kMaxMaskPx)
        out.w = qw;
    if (qh <= kMaxMaskPx)
        out.h = qh;
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

// Förälderns ruta i barnets maskpx (heltal — barnet ligger på förälderns gitter).
gfx::Rectf parentRect(const MaskGrid& parent, const MaskGrid& child)
{
    return gfx::Rectf{
        static_cast<float>(std::round((parent.x0 - child.x0) * child.s)),
        static_cast<float>(std::round((parent.y0 - child.y0) * child.s)),
        static_cast<float>(parent.w), static_cast<float>(parent.h)};
}

// Allt bortklippt på skärmen resp. i världen.
ClipLook emptyScreen(ClipMasks& pool, const ClipLook& parent)
{
    ClipLook out = parent;
    out.screen   = pool.empty();
    out.sg       = MaskGrid{kEmptyOrigin, kEmptyOrigin, 1.0, 1, 1};
    out.sKey     = 1;
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

// ---- ytorna i maskens rymd

// Ytorna i LOKALA logiska px till målets px. false när en punkt inte är ändlig.
bool screenAreas(const PaintContext& f, std::span<const ClipPolygon> polys,
                 std::vector<ClipArea>& out)
{
    for (const ClipPolygon& p : polys) {
        ClipArea a;
        if (!plainArea(p, a))
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
// ingenting, ett hål som faller bort täcker ingenting. false när en punkt
// inte är ändlig.
bool projectedAreas(const PaintContext& f, std::span<const ClipPolygon> polys,
                    std::vector<ClipArea>& out)
{
    const Projection proj = f.view3D ? Projection(*f.view3D) : Projection(f.world);
    for (const ClipPolygon& p : polys) {
        ClipArea world;
        if (!plainArea(p, world))
            return false;
        ClipArea a;
        for (std::size_t i = 0; i < world.size(); ++i) {
            ClipRing r;
            projectRing(proj, world[i], r);
            if (r.size() >= 3)
                a.push_back(std::move(r));
            else if (i == 0)
                break;   // ytterringen syns inte — hålen inte heller
        }
        if (!a.empty())
            out.push_back(std::move(a));
    }
    return true;
}

// Ytorna i världen, rakt av.
bool worldAreas(std::span<const ClipPolygon> polys, std::vector<ClipArea>& out)
{
    for (const ClipPolygon& p : polys) {
        ClipArea a;
        if (!plainArea(p, a))
            return false;
        out.push_back(std::move(a));
    }
    return true;
}

// Ytorna i maskens px: (x − x0) · s.
std::vector<ClipArea> inMask(const std::vector<ClipArea>& areas, const MaskGrid& g)
{
    std::vector<ClipArea> out = areas;
    for (ClipArea& a : out)
        for (ClipRing& r : a)
            for (gfx::Point2d& p : r) {
                p.x = (p.x - g.x0) * g.s;
                p.y = (p.y - g.y0) * g.s;
            }
    return out;
}

// ---- masken för ett klipp

ClipLook screenLook(const PaintContext& f, ClipMasks& pool, const ClipLook& parent,
                    ClipSpace space, std::span<const ClipPolygon> polys)
{
    std::vector<ClipArea> areas;
    const bool ok = space == ClipSpace::Screen ? screenAreas(f, polys, areas)
                                               : projectedAreas(f, polys, areas);
    if (!ok) {
        gfx::log("Canvas: ett klipp har en punkt som inte är ändlig — allt "
                 "klipps bort");
        return emptyScreen(pool, parent);
    }
    const Bounds b = boundsOf(areas);
    MaskGrid     g;
    const MaskGrid lat = parent.screen ? parent.sg : screenLimit(f);
    if (!b.any() || !fitGrid(b, lat, g))
        return emptyScreen(pool, parent);
    KeyHash k;
    k.mix(std::uint64_t{1});
    mixAreas(k, areas);
    mixGrid(k, g);
    k.mix(parent.sKey);
    ClipLook out = parent;
    out.sg       = g;
    out.sKey     = k.h == 0 ? 1 : k.h;
    out.screen   = pool.mask(
        out.sKey, g.w, g.h, parent.screen,
        parent.screen ? parentRect(parent.sg, g) : gfx::Rectf{},
        [&](ClipMasks::Areas& into) { into = inMask(areas, g); });
    return out;
}

// Markytans gitter i 3D-målet: den del av planen z = scenens lägsta..högsta
// höjd som vyn ser, i en upplösning som följer skärmen (maskpx per världsenhet
// vid markpunkten längst ner i bilden). false när vyn inte ser någon mark.
bool worldLimit(const PaintContext& f, MaskGrid& out)
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
        for (const gfx::Vec3d& p : reg) {
            b.x0 = std::min(b.x0, p.x);
            b.y0 = std::min(b.y0, p.y);
            b.x1 = std::max(b.x1, p.x);
            b.y1 = std::max(b.y1, p.y);
        }
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
    const double ex = b.x1 - b.x0, ey = b.y1 - b.y0;
    if (!(ex > 0.0) || !(ey > 0.0))
        return false;
    const gfx::Rectf& box = v.box;
    const Projection  proj(v);
    double s = proj.groundScale(box.x + box.w / 2.0, box.y + box.h - 1.0);
    if (!(s > 0.0) || !std::isfinite(s))
        s = f.world.pxPerUnit;
    s = std::min(s, kMaxWorldMaskPx / std::max(ex, ey));
    if (!(s > 0.0) || !std::isfinite(s))
        return false;
    out = MaskGrid{b.x0, b.y0, s, std::max(1, static_cast<int>(std::ceil(ex * s))),
                   std::max(1, static_cast<int>(std::ceil(ey * s)))};
    return true;
}

ClipLook worldLook(const PaintContext& f, ClipMasks& pool, const ClipLook& parent,
                   std::span<const ClipPolygon> polys)
{
    std::vector<ClipArea> areas;
    if (!worldAreas(polys, areas)) {
        gfx::log("Canvas: ett klipp har en punkt som inte är ändlig — allt "
                 "klipps bort");
        return emptyWorld(pool, parent);
    }
    const Bounds b = boundsOf(areas);
    MaskGrid     g;
    MaskGrid lat = parent.wg;
    if (!parent.world && !worldLimit(f, lat))
        return emptyWorld(pool, parent);   // ingen synlig mark
    if (!b.any() || !fitGrid(b, lat, g))
        return emptyWorld(pool, parent);
    KeyHash k;
    k.mix(std::uint64_t{2});
    mixAreas(k, areas);
    mixGrid(k, g);
    k.mix(parent.wKey);
    ClipLook out = parent;
    out.wg       = g;
    out.wKey     = k.h == 0 ? 1 : k.h;
    out.world    = pool.mask(
        out.wKey, g.w, g.h, parent.world,
        parent.world ? parentRect(parent.wg, g) : gfx::Rectf{},
        [&](ClipMasks::Areas& into) { into = inMask(areas, g); });
    return out;
}

// Samma skärmmask i båda utseendena?
bool sameScreen(const ClipLook& a, const ClipLook& b)
{
    return a.screen == b.screen && a.sKey == b.sKey;
}

} // namespace

// ------------------------------------------------------------------ ClipLook

void ClipLook::apply(gfx::SceneClip& c, double ox, double oy) const
{
    if (screen) {
        c.screen     = screen;
        c.screenRect = gfx::Rectf{static_cast<float>(sg.x0), static_cast<float>(sg.y0),
                                  static_cast<float>(sg.w), static_cast<float>(sg.h)};
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

ClipMasks::Entry* ClipMasks::take(std::uint64_t key, int w, int h)
{
    const std::uint64_t serial = r_->frameSerial();
    Entry*              reuse  = nullptr;
    for (const std::unique_ptr<Entry>& e : entries_) {
        if (e->w != w || e->h != h)
            continue;
        if (e->built && e->key == key)
            return e.get();
        if (!reuse && e->used != serial)
            reuse = e.get();
    }
    if (reuse) {
        reuse->built = false;
        return reuse;
    }
    trim();
    auto e = std::make_unique<Entry>();
    e->w   = w;
    e->h   = h;
    e->target = r_->createRenderTarget(w, h);
    if (!e->target || !e->target->image().valid()) {
        gfx::log("Canvas: en klippmask (%d × %d) gick inte att skapa — klippet "
                 "klipper bort allt",
                 w, h);
        return nullptr;
    }
    entries_.push_back(std::move(e));
    return entries_.back().get();
}

void ClipMasks::fill(Entry& e, const gfx::Image* parent, const gfx::Rectf& parentRect,
                     const std::function<void(Areas&)>& areas)
{
    Areas list;
    areas(list);
    gfx::MapView mv;
    mv.cx        = e.w / 2.0;
    mv.cy        = e.h / 2.0;
    mv.pxPerUnit = 1.0;
    mv.yUp       = false;
    mv.box       = gfx::Rectf{0.0f, 0.0f, static_cast<float>(e.w),
                              static_cast<float>(e.h)};
    r_->pushTarget(*e.target, kTransparent);
    for (const ClipArea& a : list) {
        std::vector<gfx::FillChunk> chunks;
        if (!gfx::buildClipPolygon(a, chunks)) {
            gfx::log("Canvas: en klippyta går inte att fylla (en självkorsning, "
                     "ett hål utanför ytterringen eller för få punkter) — den "
                     "klipper bort sin del");
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

const gfx::Image* ClipMasks::mask(std::uint64_t key, int w, int h,
                                  const gfx::Image* parent,
                                  const gfx::Rectf& parentRect,
                                  const std::function<void(Areas&)>& areas)
{
    if (w <= 0 || h <= 0)
        return empty();
    Entry* e = take(key, w, h);
    if (!e)
        return empty();
    e->used = r_->frameSerial();
    if (!e->built) {   // `take` ger en träff byggd för nyckeln, annars obyggd
        e->key = key;
        fill(*e, parent, parentRect, areas);
        e->built = true;
    }
    return &e->target->image();
}

const gfx::Image* ClipMasks::empty()
{
    if (!empty_) {
        auto e    = std::make_unique<Entry>();
        e->w      = 1;
        e->h      = 1;
        e->target = r_->createRenderTarget(1, 1);
        e->built  = true;
        if (!e->target || !e->target->image().valid()) {
            gfx::log("Canvas: den tomma klippmasken gick inte att skapa");
            return nullptr;
        }
        empty_ = std::move(e);
    }
    return &empty_->target->image();
}

void ClipMasks::trim()
{
    const std::uint64_t serial = r_->frameSerial();
    std::erase_if(entries_, [serial](const std::unique_ptr<Entry>& e) {
        return e->used + 2 < serial;
    });
}

// -------------------------------------------------------------------- klippet

ClipScope refineClip(const PaintContext& f, const ClipScope& parent,
                     ClipSpace space, std::span<const ClipPolygon> polys)
{
    ClipScope out = parent;
    if (!f.res)
        return out;
    ClipMasks& pool = f.res->clips();
    out.flat        = screenLook(f, pool, parent.flat, space, polys);
    if (!f.view3D) {
        out.tall = out.flat;
        return out;
    }
    if (space == ClipSpace::World) {
        out.tall = worldLook(f, pool, parent.tall, polys);
        return out;
    }
    // Skärmklippet ser 2D-programmen och 3D-programmen lika — samma mask, om
    // föräldrarna har samma.
    if (sameScreen(parent.tall, parent.flat)) {
        out.tall        = parent.tall;
        out.tall.screen = out.flat.screen;
        out.tall.sg     = out.flat.sg;
        out.tall.sKey   = out.flat.sKey;
    } else {
        out.tall = screenLook(f, pool, parent.tall, space, polys);
    }
    return out;
}

ClipFrame prepareClips(const PaintContext& f, const SceneData& scene, ClipUse use)
{
    ClipFrame                cf;
    std::vector<ClipPolygon> giltiga;
    if (use != ClipUse::Drape && clipApplies(scene.clip, scene.invalidClip))
        cf.canvas = refineClip(f, cf.canvas, scene.clip.space,
                               validPolygons(scene.clip, giltiga));
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
        cf.layers[i] = refineClip(f, cf.canvas, L.clip.space, validPolygons(L.clip, giltiga));
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

// Punkt i ringen (jämn-udda regeln).
bool inRing(const std::vector<Point>& r, Point p)
{
    bool in = false;
    for (std::size_t i = 0, j = r.size() - 1; i < r.size(); j = i++) {
        const Point& a = r[i];
        const Point& b = r[j];
        if ((a.y > p.y) != (b.y > p.y) &&
            p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            in = !in;
    }
    return in;
}

bool inPolygon(const ClipPolygon& poly, Point p)
{
    if (poly.outer.size() < 3 || !inRing(poly.outer, p))
        return false;
    for (const std::vector<Point>& h : poly.holes)
        if (h.size() >= 3 && inRing(h, p))
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

bool clipContains(std::span<const ClipPolygon> polys, Point p)
{
    if (!finite(p))
        return false;
    for (const ClipPolygon& poly : polys)
        if (inPolygon(poly, p))
            return true;
    return false;
}

bool clipTouches(std::span<const ClipPolygon> polys, std::span<const Point> quad)
{
    if (quad.size() < 3)
        return false;
    const std::vector<Point> ring(quad.begin(), quad.end());
    for (const ClipPolygon& poly : polys) {
        for (const Point& q : quad)
            if (inPolygon(poly, q))
                return true;
        for (const Point& p : poly.outer)
            if (inRing(ring, p))
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

bool clipPolygonValid(const ClipPolygon& p)
{
    ClipArea a;
    if (!plainArea(p, a))
        return false;
    std::vector<gfx::FillChunk> chunks;
    return gfx::buildClipPolygon(a, chunks);
}

void revalidateClip(ClipState& k, std::size_t first, std::size_t last)
{
    k.valid.resize(k.polygons.size(), 1);
    for (std::size_t i = first; i < last && i < k.polygons.size(); ++i)
        k.valid[i] = clipPolygonValid(k.polygons[i]) ? 1 : 0;
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

std::span<const ClipPolygon> validPolygons(const ClipState& k,
                                           std::vector<ClipPolygon>& tmp)
{
    if (allValid(k))
        return k.polygons;
    tmp.clear();
    for (std::size_t i = 0; i < k.polygons.size(); ++i)
        if (i >= k.valid.size() || k.valid[i])
            tmp.push_back(k.polygons[i]);
    return tmp;
}

bool clipContains(const ClipState& k, Point p)
{
    if (!finite(p))
        return false;
    for (std::size_t i = 0; i < k.polygons.size(); ++i)
        if ((i >= k.valid.size() || k.valid[i]) && inPolygon(k.polygons[i], p))
            return true;
    return false;
}

bool clipTouches(const ClipState& k, std::span<const Point> quad)
{
    std::vector<ClipPolygon> tmp;
    return clipTouches(validPolygons(k, tmp), quad);
}

} // namespace glaze::detail
