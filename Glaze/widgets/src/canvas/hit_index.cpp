// Ritytans träffprov — reglerna per slag och rutnätet
// står i hit_index.h.

#include "hit_index.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include <gfx/camera.h>

#include "build.h"
#include "labels.h"

namespace glaze::detail {

namespace {

// Rutnätets täthet: ungefär en form per cell (√n celler längs den längsta
// axeln, högst `kMaxCellsPerAxis`), och en form som täcker fler än
// `kBigCells` celler läggs i en egen lista i stället för i alla dem.
constexpr int kMaxCellsPerAxis = 128;
constexpr int kBigCells        = 64;

// Programmens gräns för w: en punkt närmare kameran än så har ingen
// skärmpunkt (Projection::toScreen).
constexpr double kMinW = 1e-6;

double unitPx(const HitQuery& q, Unit u)
{
    switch (u) {
    case Unit::Mm:    return q.pxPerMm;
    case Unit::World: return q.pxPerWorld;
    case Unit::Px:    break;
    }
    return q.pxPerPx;
}

// Avståndet från (px, py) till sträckan a–b.
double segmentDistance(double px, double py, double ax, double ay, double bx,
                       double by)
{
    const double dx = bx - ax, dy = by - ay;
    const double l2 = dx * dx + dy * dy;
    const double t =
        l2 > 0.0 ? std::clamp(((px - ax) * dx + (py - ay) * dy) / l2, 0.0, 1.0)
                 : 0.0;
    return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

// Världssträckan a–b på skärmen: den del som ligger framför närplanet
// (klippdjup ≥ 0) — klipprymden är linjär i världen, så skärningen görs
// där — projicerad. false när ingen del syns.
bool screenSegment(const Projection& p, const Point& a, const Point& b,
                   double& x0, double& y0, double& x1, double& y1)
{
    double ca[4], cb[4];
    p.clip(a.x, a.y, ca);
    p.clip(b.x, b.y, cb);
    if (ca[2] < 0.0 && cb[2] < 0.0)
        return false;
    const auto toNear = [](double (&c)[4], const double (&o)[4]) {
        const double t = c[2] / (c[2] - o[2]);
        for (int k = 0; k < 4; ++k)
            c[k] += (o[k] - c[k]) * t;
    };
    if (ca[2] < 0.0)
        toNear(ca, cb);
    else if (cb[2] < 0.0)
        toNear(cb, ca);
    if (!(ca[3] > kMinW) || !(cb[3] > kMinW))
        return false;
    x0 = ca[0] / ca[3];
    y0 = ca[1] / ca[3];
    x1 = cb[0] / cb[3];
    y1 = cb[1] / cb[3];
    return true;
}

// Konturernas kanter (en sluten tillbaka till början) — `fn(a, b)` per
// kant; true så fort den svarar true.
template <class Fn>
bool anyEdge(const std::vector<Shape::Contour>& contours, bool allClosed,
             Fn&& fn)
{
    for (const Shape::Contour& c : contours) {
        const std::size_t n = c.points.size();
        if (n == 0)
            continue;
        if (n == 1 && fn(c.points[0], c.points[0]))
            return true;
        for (std::size_t i = 0; i + 1 < n; ++i)
            if (fn(c.points[i], c.points[i + 1]))
                return true;
        if ((allClosed || c.closed) && n > 2 &&
            fn(c.points[n - 1], c.points[0]))
            return true;
    }
    return false;
}

// Pekaren inom `limitPx` från någon kant på skärmen — på en terräng
// (`HitQuery::hasGround`) i världens xy vid träffpunkten, med
// gränsen omräknad där.
bool nearEdges(const HitQuery& q, const std::vector<Shape::Contour>& contours,
               bool allClosed, double limitPx)
{
    if (q.hasGround) {
        if (!(q.groundPxPerUnit > 0.0))
            return false;
        const double limit = limitPx / q.groundPxPerUnit;
        return anyEdge(contours, allClosed, [&](const Point& a, const Point& b) {
            return segmentDistance(q.groundX, q.groundY, a.x, a.y, b.x, b.y) <= limit;
        });
    }
    return anyEdge(contours, allClosed, [&](const Point& a, const Point& b) {
        double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
        return screenSegment(*q.proj, a, b, x0, y0, x1, y1) &&
               segmentDistance(q.px, q.py, x0, y0, x1, y1) <= limitPx;
    });
}

// Pekarens punkt i världen — strålen mot planet, eller terrängens träffpunkt.
// false ovanför horisonten.
bool pointerWorld(const HitQuery& q, double& wx, double& wy)
{
    if (q.hasGround) {
        wx = q.groundX;
        wy = q.groundY;
        return true;
    }
    return q.proj->ground(q.px, q.py, wx, wy);
}

// Skalan vid pekaren, fysiska px per världsenhet — vid marken under den, eller
// vid terrängens punkt; 0 ovanför horisonten.
double pointerScale(const HitQuery& q)
{
    return q.hasGround ? q.groundPxPerUnit : q.proj->groundScale(q.px, q.py);
}

// Jämn–udda över alla ringar: ett hål är ute.
bool insideRings(const std::vector<Shape::Contour>& rings, double x, double y)
{
    bool in = false;
    for (const Shape::Contour& r : rings) {
        const std::vector<Point>& p = r.points;
        const std::size_t         n = p.size();
        for (std::size_t i = 0, j = n - 1; i < n; j = i++)
            if ((p[i].y > y) != (p[j].y > y) &&
                x < (p[j].x - p[i].x) * (y - p[i].y) / (p[j].y - p[i].y) +
                        p[i].x)
                in = !in;
    }
    return in;
}

bool strokeVisible(const Shape& s)
{
    return s.hasStroke() && s.stroke().paint.visible() &&
           s.stroke().width.value > 0.0f;
}

// En kontur som linje: bredden på skärmen (px, mm) prövas i skärmen, en
// bredd i VÄRLDEN i planet med toleransen omräknad vid pekaren.
bool strokeHit(const HitQuery& q, const Shape& s, bool allClosed)
{
    const Stroke& st = s.stroke();
    const double  hw = 0.5 * st.width.value;
    if (st.width.unit != Unit::World)
        return nearEdges(q, s.contours(), allClosed,
                         hw * unitPx(q, st.width.unit) + q.tolPx);
    double wx = 0.0, wy = 0.0;
    if (!pointerWorld(q, wx, wy))
        return false;
    const double scale = pointerScale(q);
    if (!(scale > 0.0))
        return false;
    const double limit = hw + q.tolPx / scale;
    return anyEdge(s.contours(), allClosed, [&](const Point& a, const Point& b) {
        return segmentDistance(wx, wy, a.x, a.y, b.x, b.y) <= limit;
    });
}

// INSIDAN RÄKNAS SOM YTA: en ytas synliga fyllning, eller
// konturens `hitInside` — på en yta och på en 2D-linje, vars konturer då är
// ringarna en fyllning hade fått (jämn–udda, en öppen sluten mellan ändarna).
bool insideCounts(const Shape& s)
{
    if (s.kind() == Shape::Kind::Area && s.hasFill() && s.fill().visible())
        return true;
    return (s.kind() == Shape::Kind::Area || s.kind() == Shape::Kind::Lines) &&
           s.hasStroke() && s.stroke().hitInside;
}

// En yta eller en linje: insidan (när den räknas) och kanterna inom
// toleransen, och den synliga konturen — en ytas sluten.
bool areaHit(const HitQuery& q, const Shape& s)
{
    if (insideCounts(s)) {
        double wx = 0.0, wy = 0.0;
        if (pointerWorld(q, wx, wy) && insideRings(s.contours(), wx, wy))
            return true;
        if (nearEdges(q, s.contours(), true, q.tolPx))
            return true;
    }
    return strokeVisible(s) && strokeHit(q, s, s.kind() == Shape::Kind::Area);
}

// En bild som den ritas: hörnen ur `imageCornersOf` (rektangeln, eller de
// fyra fria hörnen) som en ring. En fyrhörning som
// inte ritas (inte strikt konvex) har ingen. Ringens orientering spelar
// ingen roll för något av proven, så y-riktningen behövs inte.
bool imageRing(const Shape& s, std::vector<Shape::Contour>& ring)
{
    if (!imageQuadOk(s))
        return false;
    Point c[4];
    imageCornersOf(s, false, c);
    ring = {Shape::Contour{{c[0], c[1], c[3], c[2]}, true}};
    return true;
}

// Pekaren i bildens ring eller inom toleransen från en kant.
bool imageHit(const HitQuery& q, const Shape& s)
{
    std::vector<Shape::Contour> ring;
    if (!imageRing(s, ring))
        return false;
    double wx = 0.0, wy = 0.0;
    if (pointerWorld(q, wx, wy) && insideRings(ring, wx, wy))
        return true;
    return nearEdges(q, ring, true, q.tolPx);
}

// Pekaren i en vriden låda (mitt, halva mått, baslinjens riktning) med
// toleransen runt om.
bool inBox(const HitQuery& q, double cx, double cy, double halfW, double halfH,
           double ex, double ey)
{
    const double dx = q.px - cx, dy = q.py - cy;
    const double u  = dx * ex + dy * ey;     // längs baslinjen
    const double v  = -dx * ey + dy * ex;    // "nedåt" (−e.y, e.x)
    return std::abs(u) <= halfW + q.tolPx && std::abs(v) <= halfH + q.tolPx;
}

// En markörs radie på skärmen (fyllningens halva mått + en synlig kontur;
// en bildmarkör har ingen kontur — dess ruta är storleken).
double markerRadius(const HitQuery& q, const MarkerStyle& ms)
{
    const double raw = ms.size.value * unitPx(q, ms.size.unit);
    double       r   = 0.5 * raw;
    if (ms.outline.a > 0 && !ms.img.valid())
        r += ms.outlineWidth.value * unitPx(q, ms.outlineWidth.unit);
    // GOLVET OCH TAKET skalar hela markören, konturen med — som
    // Glyph-programmet, med frågans tätheter.
    if (raw > 0.0)
        r *= clampGlyphPx(raw, glyphLimits(ms.minSize, ms.maxSize,
                                           static_cast<float>(q.pxPerPx),
                                           static_cast<float>(q.pxPerMm),
                                           static_cast<float>(q.pxPerWorld))) /
             raw;
    return r;
}

// Markörrutans mitt relativt punkten i markörens EGEN ram (x längs
// baslinjen, y nedåt) för en ruta med halva måttet r — ANKARETS nio lägen,
// som byggaren lägger dem (gfx::buildMarkers): Bottom står ovanför punkten.
void anchorOffset(TextAnchor a, double r, double& ox, double& oy)
{
    switch (a) {
    case TextAnchor::TopLeft:
    case TextAnchor::Left:
    case TextAnchor::BottomLeft:  ox = r; break;
    case TextAnchor::TopRight:
    case TextAnchor::Right:
    case TextAnchor::BottomRight: ox = -r; break;
    default:                      ox = 0.0; break;
    }
    switch (a) {
    case TextAnchor::TopLeft:
    case TextAnchor::Top:
    case TextAnchor::TopRight:    oy = r; break;
    case TextAnchor::BottomLeft:
    case TextAnchor::Bottom:
    case TextAnchor::BottomRight: oy = -r; break;
    default:                      oy = 0.0; break;
    }
}

// En markör på skärmen som den ritas: rutans mitt, halva måttet, baslinjens
// riktning och om den är en SKIVA — en rund formmarkör. En BILDMARKÖR är hela
// sin ruta (bildens proportioner krymper den ritade bilden, inte rutan).
struct MarkerGeom {
    double cx = 0.0, cy = 0.0, r = 0.0;
    double ex = 1.0, ey = 0.0;
    bool   disc = false;
};

// Varje markör i formen som har en plats på skärmen — `fn(punktens index,
// geometri)`, i punkternas ordning.
template <class Fn>
void forEachMarker(const HitQuery& q, const Shape& s, Fn&& fn)
{
    const MarkerStyle& ms = s.markerStyle();
    MarkerGeom         g;
    g.r              = markerRadius(q, ms);
    g.disc           = ms.shape == MarkerShape::Circle && !ms.img.valid();
    const double  a  = ms.angle * std::numbers::pi / 180.0;
    double        ox = 0.0, oy = 0.0;
    anchorOffset(ms.anchor, g.r, ox, oy);
    const auto one = [&](std::size_t i, double x, double y, double z) {
        double sx = 0.0, sy = 0.0;
        if (!q.proj->toScreen(x, y, z, sx, sy))
            return;
        // Baslinjens riktning på skärmen: skärmens vinkel, eller kartans —
        // ankaret vrids med markören kring punkten, som i Glyph-programmet.
        g.ex = std::cos(a);
        g.ey = -std::sin(a);
        if (ms.rotation == Alignment::Map &&
            !q.proj->direction(x, y, z, std::cos(a), std::sin(a), 0.0, g.ex,
                               g.ey)) {
            g.ex = 1.0;
            g.ey = 0.0;
        }
        g.cx = sx + ox * g.ex - oy * g.ey;   // ned = (−e.y, e.x)
        g.cy = sy + ox * g.ey + oy * g.ex;
        fn(i, g);
    };
    // Punkterna — i rymden med sitt z, annars z = 0.
    if (s.kind() == Shape::Kind::Markers3) {
        const std::vector<Point3>& pts = s.points3();
        for (std::size_t i = 0; i < pts.size(); ++i)
            one(i, pts[i].x, pts[i].y, pts[i].z);
    } else {
        const std::vector<Point>& pts = s.points();
        for (std::size_t i = 0; i < pts.size(); ++i)
            one(i, pts[i].x, pts[i].y, 0.0);
    }
}

bool isMarkerShape(const Shape& s)
{
    return s.kind() == Shape::Kind::Markers || s.kind() == Shape::Kind::Markers3;
}

// Vinner (avståndet d, punkten e) över det bästa hittills? Det närmare, vid
// lika den senare punkten — den ritas överst.
bool nearer(double d, std::int64_t e, double bestD, std::int64_t bestE)
{
    return bestE < 0 || d < bestD || (d == bestD && e > bestE);
}

// Den träffade markören vars mitt ligger närmast pekaren (`nearer`), eller
// −1; avståndet i `dist`.
std::int64_t markersHit(const HitQuery& q, const Shape& s, double& dist)
{
    std::int64_t best = -1;
    forEachMarker(q, s, [&](std::size_t i, const MarkerGeom& m) {
        const double d   = std::hypot(q.px - m.cx, q.py - m.cy);
        const bool   hit = m.disc ? d <= m.r + q.tolPx
                                  : inBox(q, m.cx, m.cy, m.r, m.r, m.ex, m.ey);
        const auto   e   = static_cast<std::int64_t>(i);
        if (hit && nearer(d, e, dist, best)) {
            best = e;
            dist = d;
        }
    });
    return best;
}

// Går formen genom etiketterna (text, kolliderande markörer — också i
// rymden)?
bool viaLabels(const Shape& s)
{
    return s.kind() == Shape::Kind::Text || s.kind() == Shape::Kind::Text3 ||
           (isMarkerShape(s) && s.markerStyle().collide);
}

// Träffprovet saknar linjen i rymden och föremålen (meshar, ytor i rymden,
// instanser, punkter) till 3D-plockningen.
bool unhittable(const Shape& s)
{
    return s.kind() == Shape::Kind::Lines3 || s.kind() == Shape::Kind::Mesh ||
           s.kind() == Shape::Kind::Area3 || s.kind() == Shape::Kind::Instances ||
           s.kind() == Shape::Kind::Points3;
}

// Cellen en koordinat hamnar i, klampad till rutnätet i flyttal FÖRE
// heltalet (placeringens regel) — en låda långt utanför delar kantcellen.
int cellIndex(const HitGrid& g, double v, bool xAxis)
{
    const double o = xAxis ? g.ox : g.oy;
    const int    n = xAxis ? g.nx : g.ny;
    const double c = std::floor((v - o) / g.cell);
    return static_cast<int>(std::clamp(c, 0.0, static_cast<double>(n - 1)));
}

void noteReach(HitGrid& g, Length l, float factor)
{
    float& r = g.reach[static_cast<int>(l.unit)];
    r        = std::max(r, l.value * factor);
}

// GOLVETS räckvidd för en markör: golvet × ankarets andel, och
// konturens andel i em (`markerRadius`: konturen läggs på radien, och ett
// ankare utanför mitten flyttar rutan lika långt till).
void noteFloor(HitGrid& g, const MarkerStyle& ms, float anchorShare)
{
    const Length m = ms.minSize;
    if (!(m.value > 0.0f) || !std::isfinite(m.value))
        return;
    const int mu     = static_cast<int>(m.unit);
    g.floorReach[mu] = std::max(g.floorReach[mu], m.value * anchorShare);
    if (ms.outline.a > 0 && !ms.img.valid() && ms.size.value > 0.0f &&
        ms.outlineWidth.value > 0.0f) {
        float& r = g.floorOutline[mu][static_cast<int>(ms.outlineWidth.unit)]
                                 [static_cast<int>(ms.size.unit)];
        r = std::max(r, m.value * 2.0f * anchorShare * ms.outlineWidth.value /
                            ms.size.value);
    }
}

HitGrid& gridFor(LayerData& L)
{
    // Versionen är sanningen; antalet är ett skydd mot en ändring som
    // glömde stega den (rutnätets index pekar in i `slots`).
    if (L.hit && L.hit->version == L.shapesVersion &&
        L.hit->entries.size() == L.slots.size())
        return *L.hit;
    auto g     = std::make_shared<HitGrid>();
    g->version = L.shapesVersion;
    g->entries.resize(L.slots.size());

    bool   any = false;
    double ux0 = 0.0, uy0 = 0.0, ux1 = 0.0, uy1 = 0.0;
    for (std::size_t i = 0; i < L.slots.size(); ++i) {
        const Shape& s = L.slots[i].entry->shape;
        g->indexOf[L.slots[i].entry.get()] = static_cast<std::uint32_t>(i);
        HitGrid::Entry& e = g->entries[i];
        // En insida som räknas prövas också utan synlig del.
        if (viaLabels(s) || unhittable(s) ||
            (partKeys(s).empty() && !insideCounts(s)) ||
            !shapeBounds(s, e.x0, e.y0, e.x1, e.y1))
            continue;
        e.grid = true;
        if (strokeVisible(s)) {
            if (s.stroke().width.unit == Unit::World) {
                const double hw = 0.5 * s.stroke().width.value;
                e.x0 -= hw;
                e.y0 -= hw;
                e.x1 += hw;
                e.y1 += hw;
            } else {
                noteReach(*g, s.stroke().width, 0.5f);
            }
        }
        if (isMarkerShape(s)) {
            // Ett ANKARE utanför mitten flyttar rutan bort från punkten —
            // dess bortre hörn ligger upp till √2 storlekar därifrån.
            const float share =
                s.markerStyle().anchor == TextAnchor::Center ? 0.5f : 1.5f;
            noteReach(*g, s.markerStyle().size, share);
            if (s.markerStyle().outline.a > 0)
                noteReach(*g, s.markerStyle().outlineWidth, 1.0f);
            noteFloor(*g, s.markerStyle(), share);
        }
        if (!any) {
            ux0 = e.x0;
            uy0 = e.y0;
            ux1 = e.x1;
            uy1 = e.y1;
            any = true;
        }
        ux0 = std::min(ux0, e.x0);
        uy0 = std::min(uy0, e.y0);
        ux1 = std::max(ux1, e.x1);
        uy1 = std::max(uy1, e.y1);
    }

    std::size_t n = 0;
    for (const HitGrid::Entry& e : g->entries)
        n += e.grid ? 1 : 0;
    const double span = std::max(ux1 - ux0, uy1 - uy0);
    const int k = std::clamp(
        static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n)))), 1,
        kMaxCellsPerAxis);
    g->ox   = ux0;
    g->oy   = uy0;
    g->cell = span > 0.0 && std::isfinite(span) ? span / k : 1.0;
    g->nx   = std::clamp(static_cast<int>(std::ceil((ux1 - ux0) / g->cell)), 1,
                         kMaxCellsPerAxis);
    g->ny   = std::clamp(static_cast<int>(std::ceil((uy1 - uy0) / g->cell)), 1,
                         kMaxCellsPerAxis);
    g->cells.resize(static_cast<std::size_t>(g->nx) * g->ny);
    for (std::size_t i = 0; i < g->entries.size(); ++i) {
        const HitGrid::Entry& e = g->entries[i];
        if (!e.grid)
            continue;
        const int cx0 = cellIndex(*g, e.x0, true), cx1 = cellIndex(*g, e.x1, true);
        const int cy0 = cellIndex(*g, e.y0, false), cy1 = cellIndex(*g, e.y1, false);
        if ((cx1 - cx0 + 1) * (cy1 - cy0 + 1) > kBigCells) {
            g->big.push_back(static_cast<std::uint32_t>(i));
            continue;
        }
        for (int cy = cy0; cy <= cy1; ++cy)
            for (int cx = cx0; cx <= cx1; ++cx)
                g->cells[static_cast<std::size_t>(cy) * g->nx + cx].push_back(
                    static_cast<std::uint32_t>(i));
    }
    L.hit = std::move(g);
    return *L.hit;
}

// Kandidaterna ur rutnätet: skärmrutan (x0, y0)–(x1, y1) — pekarens punkt,
// eller rutans prov — med toleransen och det största skärmmåttet
// runt om, tillbaka till planet; en projektiv bild av en konvex ruta under
// horisonten är konvex, så hörnens låda räcker. Når den ovanför horisonten —
// eller blir den icke-ändlig — är alla kandidater.
std::vector<std::uint32_t> candidates(const HitGrid& g, const HitQuery& q,
                                      double x0, double y0, double x1,
                                      double y1)
{
    std::vector<std::uint32_t> out;
    double r = q.tolPx + g.reach[0] * q.pxPerPx + g.reach[1] * q.pxPerMm +
               g.reach[2] * q.pxPerWorld;
    // Golvets räckvidd med frågans tätheter.
    for (int mu = 0; mu < 3; ++mu) {
        const double dm = unitPx(q, static_cast<Unit>(mu));
        r += g.floorReach[mu] * dm;
        for (int ou = 0; ou < 3; ++ou)
            for (int su = 0; su < 3; ++su) {
                const double ds = unitPx(q, static_cast<Unit>(su));
                if (g.floorOutline[mu][ou][su] > 0.0f && ds > 0.0)
                    r += g.floorOutline[mu][ou][su] * dm *
                         unitPx(q, static_cast<Unit>(ou)) / ds;
            }
    }
    double wx0 = 0.0, wy0 = 0.0, wx1 = 0.0, wy1 = 0.0;
    bool   ok  = true;
    if (q.hasGround) {
        // TERRÄNGENS punkt: rutan i världens xy kring den, skärmmåtten
        // omräknade där.
        const double rw = q.groundPxPerUnit > 0.0 ? r / q.groundPxPerUnit : 0.0;
        wx0 = q.groundX - rw;
        wx1 = q.groundX + rw;
        wy0 = q.groundY - rw;
        wy1 = q.groundY + rw;
        ok  = std::isfinite(rw) && q.groundPxPerUnit > 0.0;
    }
    for (int k = 0; k < 4 && ok && !q.hasGround; ++k) {
        double wx = 0.0, wy = 0.0;
        ok = q.proj->ground((k & 1) ? x1 + r : x0 - r,
                            (k & 2) ? y1 + r : y0 - r, wx, wy) &&
             std::isfinite(wx) && std::isfinite(wy);
        if (!ok)
            break;
        if (k == 0) {
            wx0 = wx1 = wx;
            wy0 = wy1 = wy;
        }
        wx0 = std::min(wx0, wx);
        wy0 = std::min(wy0, wy);
        wx1 = std::max(wx1, wx);
        wy1 = std::max(wy1, wy);
    }
    if (!ok) {
        for (std::size_t i = 0; i < g.entries.size(); ++i)
            if (g.entries[i].grid)
                out.push_back(static_cast<std::uint32_t>(i));
        return out;
    }
    std::vector<char> seen(g.entries.size(), 0);
    const auto take = [&](std::uint32_t i) {
        const HitGrid::Entry& e = g.entries[i];
        if (seen[i] || e.x1 < wx0 || e.x0 > wx1 || e.y1 < wy0 || e.y0 > wy1)
            return;
        seen[i] = 1;
        out.push_back(i);
    };
    for (std::uint32_t i : g.big)
        take(i);
    const int cx0 = cellIndex(g, wx0, true), cx1 = cellIndex(g, wx1, true);
    const int cy0 = cellIndex(g, wy0, false), cy1 = cellIndex(g, wy1, false);
    for (int cy = cy0; cy <= cy1; ++cy)
        for (int cx = cx0; cx <= cx1; ++cx)
            for (std::uint32_t i : g.cells[static_cast<std::size_t>(cy) * g.nx + cx])
                take(i);
    return out;
}

// Formens plats i målarordningen inom lagret: i ByKind ligger texten över
// linjerna över ytorna över bilderna (formens översta del räknas).
int kindRank(const LayerData& L, const Shape& s)
{
    if (L.order != DrawOrder::ByKind)
        return 0;
    int r = 0;
    for (const RunKey& k : partKeys(s))
        r = std::max(r, static_cast<int>(k.kind));
    // En insida som räknas är ytan den är, också osynlig.
    if (insideCounts(s))
        r = std::max(r, static_cast<int>(PartKind::Fill));
    return r;
}

// ETIKETTERNA i lagret som de RITADES och syns — `fn(formens index, formen,
// etiketten, lådan, skiva)`: en rund markör är en skiva kring ankaret, allt
// annat en vriden låda.
template <class Fn>
void forEachShownLabel(LayerData& L, const HitGrid& g, const HitQuery& q,
                       const CanvasResources*             res,
                       const CanvasResources::LabelState* labels, Fn&& fn)
{
    if (!res)
        return;
    for (const std::shared_ptr<RunData>& run : L.runs) {
        if (!isGlyphKind(run->key.kind))
            continue;
        const CanvasResources::GlyphBuild* b = res->findGlyphs(*run);
        if (!b)
            continue;   // inte ritad i sin nuvarande form
        // Synligheten ur placeringen — gjord för just det här bygget.
        const CanvasResources::LabelRun* placed = nullptr;
        if (labels && labels->tex)
            for (const CanvasResources::LabelRun& r : labels->runs)
                if (r.run == run->id && r.serial == b->serial)
                    placed = &r;
        if (labels && labels->tex && !placed)
            continue;
        const double sizeUnitPx = unitPx(q, run->key.unit);
        // GOLVET OCH TAKET: lådan som Glyph-programmet ritar den.
        const GlyphLimits lim =
            glyphLimits(run->key, static_cast<float>(q.pxPerPx),
                        static_cast<float>(q.pxPerMm),
                        static_cast<float>(q.pxPerWorld));
        for (const CanvasResources::LabelInfo& li : b->labels) {
            if (li.shape >= run->glyphShapes.size())
                continue;
            const ShapeEntry& e = *run->glyphShapes[li.shape];
            if (!e.alive || !viaLabels(e.shape) ||
                !visibleAtZoom(e.shape, q.zoom))
                continue;
            if (placed) {
                const std::size_t t = placed->base + li.label;
                if (t >= labels->shown.size() || labels->shown[t] == 0)
                    continue;   // dold av kollisionen
            }
            LabelGeom geo;
            if (!labelGeometry(li, clampGlyphPx(li.size * sizeUnitPx, lim),
                               *q.proj, geo))
                continue;
            const bool disc = isMarkerShape(e.shape) &&
                              e.shape.markerStyle().shape == MarkerShape::Circle;
            const auto it = g.indexOf.find(&e);
            if (it != g.indexOf.end())
                fn(it->second, e.shape, li, geo, disc);
        }
    }
}

// ---------------------------------------------------------- RUTAN

bool inRect(const ScreenRect& r, double x, double y)
{
    return x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1;
}

// Sträckan a–b SKÄR rutan eller ligger i den — Liang–Barskys klippning: det
// som blir kvar av sträckan inom rutans fyra halvplan.
bool segmentMeetsRect(const ScreenRect& r, double ax, double ay, double bx,
                      double by)
{
    const double dx = bx - ax, dy = by - ay;
    const double p[4] = {-dx, dx, -dy, dy};
    const double d[4] = {ax - r.x0, r.x1 - ax, ay - r.y0, r.y1 - ay};
    double       t0 = 0.0, t1 = 1.0;
    for (int k = 0; k < 4; ++k) {
        if (p[k] == 0.0) {
            if (d[k] < 0.0)
                return false;   // parallell och utanför
            continue;
        }
        const double t = d[k] / p[k];
        if (p[k] < 0.0)
            t0 = std::max(t0, t);
        else
            t1 = std::min(t1, t);
        if (t0 > t1)
            return false;
    }
    return true;
}

// Konturerna på skärmen mot rutan. FÖNSTRET: varje ändlig punkt har en plats
// på skärmen och ligger i rutan — rutan är konvex och kanterna mellan två
// punkter framför närplanet raka på skärmen, så hela konturen gör det.
// KORSNINGEN: en kant, avskuren mot närplanet, når rutan.
bool contoursInRect(const HitQuery& q, const std::vector<Shape::Contour>& cs,
                    bool allClosed, const ScreenRect& r, bool window)
{
    if (!window)
        return anyEdge(cs, allClosed, [&](const Point& a, const Point& b) {
            double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
            return screenSegment(*q.proj, a, b, x0, y0, x1, y1) &&
                   segmentMeetsRect(r, x0, y0, x1, y1);
        });
    bool any = false;
    for (const Shape::Contour& c : cs)
        for (const Point& p : c.points) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y))
                continue;   // byggarna hoppar över den
            double sx = 0.0, sy = 0.0;
            if (!q.proj->toScreen(p.x, p.y, sx, sy) || !inRect(r, sx, sy))
                return false;
            any = true;
        }
    return any;
}

// Rutan INNE I ringarna: dess mitt i världen, där mittens stråle når planet.
// Skär ingen kant rutan ligger hela rutan i ytan eller hela utanför, så
// mitten avgör.
bool rectInsideRings(const HitQuery& q, const std::vector<Shape::Contour>& rings,
                     const ScreenRect& r)
{
    double wx = 0.0, wy = 0.0;
    return q.proj->ground(0.5 * (r.x0 + r.x1), 0.5 * (r.y0 + r.y1), wx, wy) &&
           insideRings(rings, wx, wy);
}

// En yta eller en linje mot rutan: konturerna — slutna när insidan räknas
// eller formen är en yta — och, i korsningen, rutan inne i insidan.
bool areaInRect(const HitQuery& q, const Shape& s, const ScreenRect& r,
                bool window)
{
    const bool inside = insideCounts(s);
    if (!inside && !strokeVisible(s))
        return false;
    const bool closed = inside || s.kind() == Shape::Kind::Area;
    return contoursInRect(q, s.contours(), closed, r, window) ||
           (!window && inside && rectInsideRings(q, s.contours(), r));
}

bool imageInRect(const HitQuery& q, const Shape& s, const ScreenRect& r,
                 bool window)
{
    std::vector<Shape::Contour> ring;
    if (!imageRing(s, ring))
        return false;
    return contoursInRect(q, ring, true, r, window) ||
           (!window && rectInsideRings(q, ring, r));
}

// En SKIVA (mitt, radie) mot rutan.
bool discInRect(const ScreenRect& r, double cx, double cy, double rad,
                bool window)
{
    if (window)
        return cx - rad >= r.x0 && cx + rad <= r.x1 && cy - rad >= r.y0 &&
               cy + rad <= r.y1;
    const double dx = std::max({r.x0 - cx, 0.0, cx - r.x1});
    const double dy = std::max({r.y0 - cy, 0.0, cy - r.y1});
    return std::hypot(dx, dy) <= rad;
}

// En VRIDEN LÅDA (mitt, halva mått, baslinjens riktning — `inBox`s ram) mot
// rutan: hörnen i den (fönstret), eller en kant som når den eller rutans
// mitt i lådan (korsningen).
bool boxInRect(const ScreenRect& r, double cx, double cy, double halfW,
               double halfH, double ex, double ey, bool window)
{
    double x[4], y[4];
    for (int k = 0; k < 4; ++k) {
        const double u = (k == 1 || k == 2) ? halfW : -halfW;   // längs baslinjen
        const double v = k >= 2 ? halfH : -halfH;               // "nedåt" (−e.y, e.x)
        x[k]           = cx + u * ex - v * ey;
        y[k]           = cy + u * ey + v * ex;
    }
    if (window) {
        for (int k = 0; k < 4; ++k)
            if (!inRect(r, x[k], y[k]))
                return false;
        return true;
    }
    for (int k = 0; k < 4; ++k)
        if (segmentMeetsRect(r, x[k], y[k], x[(k + 1) % 4], y[(k + 1) % 4]))
            return true;
    const double dx = 0.5 * (r.x0 + r.x1) - cx, dy = 0.5 * (r.y0 + r.y1) - cy;
    return std::abs(dx * ex + dy * ey) <= halfW &&
           std::abs(-dx * ey + dy * ex) <= halfH;
}

} // namespace

std::vector<LayerHit> hitLayer(LayerData& L, const HitQuery& query,
                               const CanvasResources*             res,
                               const CanvasResources::LabelState* labels)
{
    // ANROPETS TOLERANS I VÄRLDSENHETER omräknad vid pekaren —
    // varje prov nedan läser den i fysiska px.
    HitQuery     q = query;
    const double s = q.tolWorld > 0.0 ? pointerScale(q) : 0.0;
    if (s > 0.0 && std::isfinite(s))
        q.tolPx += q.tolWorld * s;

    HitGrid& g = gridFor(L);
    struct Hit {
        int          rank = 0, index = 0;
        std::int64_t element = -1;
        double       dist    = 0.0;   // markörens mitt till pekaren
    };
    std::vector<Hit> hits;
    std::vector<int> at(L.slots.size(), -1);   // formens plats i `hits`
    // En form noteras EN gång; en markörform behåller den närmaste punkten.
    const auto note = [&](std::uint32_t i, std::int64_t element, double dist) {
        if (i >= L.slots.size())
            return;
        if (at[i] >= 0) {
            Hit& h = hits[static_cast<std::size_t>(at[i])];
            if (element >= 0 && nearer(dist, element, h.dist, h.element)) {
                h.element = element;
                h.dist    = dist;
            }
            return;
        }
        at[i] = static_cast<int>(hits.size());
        hits.push_back(Hit{kindRank(L, L.slots[i].entry->shape),
                           static_cast<int>(i), element, dist});
    };

    // RUTNÄTETS former: linjer, ytor, bilder och markörer som inte kolliderar
    // — utan mark under pekaren (`noGround`) bara markörerna.
    for (std::uint32_t i : candidates(g, q, q.px, q.py, q.px, q.py)) {
        const Shape& sh = L.slots[i].entry->shape;
        switch (sh.kind()) {
        case Shape::Kind::Lines:
        case Shape::Kind::Area:
            if (!q.noGround && areaHit(q, sh))
                note(i, -1, 0.0);
            break;
        case Shape::Kind::Image:
            if (!q.noGround && imageHit(q, sh))
                note(i, -1, 0.0);
            break;
        case Shape::Kind::Markers:
        case Shape::Kind::Markers3:
            if (visibleAtZoom(sh, q.zoom)) {
                double             d = 0.0;
                const std::int64_t e = markersHit(q, sh, d);
                if (e >= 0)
                    note(i, e, d);
            }
            break;
        case Shape::Kind::Text:
        case Shape::Kind::Text3:
        case Shape::Kind::Lines3:   // `unhittable`
        case Shape::Kind::Mesh:
        case Shape::Kind::Area3:
        case Shape::Kind::Instances:
        case Shape::Kind::Points3:
        case Shape::Kind::Pass:    // helskärmspasset träffas inte
        case Shape::Kind::None:    break;
        }
    }

    // ETIKETTERNA: lådan som den RITADES, och bara när den syns — en
    // kolliderande markör med sin punkt.
    forEachShownLabel(L, g, q, res, labels,
                      [&](std::uint32_t i, const Shape& sh,
                          const CanvasResources::LabelInfo& li,
                          const LabelGeom& geo, bool disc) {
                          const double d =
                              disc ? std::hypot(q.px - geo.ax, q.py - geo.ay)
                                   : std::hypot(q.px - geo.cx, q.py - geo.cy);
                          const bool hit =
                              disc ? d <= geo.halfW + q.tolPx
                                   : inBox(q, geo.cx, geo.cy, geo.halfW,
                                           geo.halfH, geo.ex, geo.ey);
                          // −1 som int64: `li.sub` är osignerad, och
                          // `? li.sub : -1` hade gjort textens −1 till 2³² − 1.
                          if (hit)
                              note(i, isMarkerShape(sh) ? std::int64_t{li.sub} : -1, d);
                      });

    // ÖVERST FÖRST: rangen (ByKind), sedan det senare indexet.
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        return a.rank != b.rank ? a.rank > b.rank : a.index > b.index;
    });
    std::vector<LayerHit> out;
    out.reserve(hits.size());
    for (const Hit& h : hits)
        out.push_back(LayerHit{h.index, h.element});
    return out;
}

std::vector<LayerHit> hitLayerRect(LayerData& L, const HitQuery& query,
                                   const ScreenRect& rect, bool window,
                                   const CanvasResources*             res,
                                   const CanvasResources::LabelState* labels)
{
    // Rutan har ingen pekare, tolerans eller terrängpunkt (hit_index.h).
    HitQuery q  = query;
    q.tolPx     = 0.0;
    q.tolWorld  = 0.0;
    q.hasGround = false;

    HitGrid& g = gridFor(L);
    struct Hit {
        int          rank = 0, index = 0;
        std::int64_t element = -1;
    };
    std::vector<Hit> hits;
    const auto       note = [&](std::uint32_t i, std::int64_t element) {
        if (i < L.slots.size())
            hits.push_back(Hit{kindRank(L, L.slots[i].entry->shape),
                               static_cast<int>(i), element});
    };

    for (std::uint32_t i : candidates(g, q, rect.x0, rect.y0, rect.x1, rect.y1)) {
        const Shape& sh = L.slots[i].entry->shape;
        switch (sh.kind()) {
        case Shape::Kind::Lines:
        case Shape::Kind::Area:
            if (areaInRect(q, sh, rect, window))
                note(i, -1);
            break;
        case Shape::Kind::Image:
            if (imageInRect(q, sh, rect, window))
                note(i, -1);
            break;
        case Shape::Kind::Markers:
        case Shape::Kind::Markers3:
            // PUNKT FÖR PUNKT: varje markör i rutan är en träff.
            if (visibleAtZoom(sh, q.zoom))
                forEachMarker(q, sh, [&](std::size_t k, const MarkerGeom& m) {
                    if (m.disc ? discInRect(rect, m.cx, m.cy, m.r, window)
                               : boxInRect(rect, m.cx, m.cy, m.r, m.r, m.ex,
                                           m.ey, window))
                        note(i, static_cast<std::int64_t>(k));
                });
            break;
        case Shape::Kind::Text:
        case Shape::Kind::Text3:
        case Shape::Kind::Lines3:   // formerna i rymden (`unhittable`)
        case Shape::Kind::Mesh:
        case Shape::Kind::Area3:
        case Shape::Kind::Instances:
        case Shape::Kind::Points3:
        case Shape::Kind::Pass:
        case Shape::Kind::None:    break;
        }
    }

    forEachShownLabel(L, g, q, res, labels,
                      [&](std::uint32_t i, const Shape& sh,
                          const CanvasResources::LabelInfo& li,
                          const LabelGeom& geo, bool disc) {
                          if (disc ? discInRect(rect, geo.ax, geo.ay, geo.halfW,
                                                window)
                                   : boxInRect(rect, geo.cx, geo.cy, geo.halfW,
                                               geo.halfH, geo.ex, geo.ey, window))
                              note(i, isMarkerShape(sh) ? std::int64_t{li.sub} : -1);
                      });

    // ÖVERST FÖRST: rangen, det senare indexet, den senare punkten — och en
    // etikett i flera byggen en gång.
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        if (a.rank != b.rank)
            return a.rank > b.rank;
        return a.index != b.index ? a.index > b.index : a.element > b.element;
    });
    hits.erase(std::unique(hits.begin(), hits.end(),
                           [](const Hit& a, const Hit& b) {
                               return a.index == b.index && a.element == b.element;
                           }),
               hits.end());
    std::vector<LayerHit> out;
    out.reserve(hits.size());
    for (const Hit& h : hits)
        out.push_back(LayerHit{h.index, h.element});
    return out;
}

} // namespace glaze::detail
