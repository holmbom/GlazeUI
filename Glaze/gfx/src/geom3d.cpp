// 3D-geometrin — backend-neutral: lådan, sfären,
// cylindern och röret som rena data, och polygonen i ett plan genom den egna
// öronklippningen — och polylinjen i rymden (Line3-programmets
// bitar). Kontraktet står i include/gfx/geom3d.h; trianguleringen i
// earcut.cpp.

#include "gfx/geom3d.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <numbers>

#include "earcut.h"
#include "gfx/platform.h"

namespace gfx {
namespace {

// En egen vektortyp för räkningen här inne — fasadens `Vec3d` är ett
// aggregat utan räknesätt, och operatorer på den hade kunnat krocka med
// andra filers.
struct V {
    double x = 0.0, y = 0.0, z = 0.0;
};

V operator+(V a, V b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V operator*(V a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V      cross(V a, V b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(V a) { return std::sqrt(dot(a, a)); }
V      unit(V a) { return a * (1.0 / length(a)); }

V      toV(const Vec3d& p) { return {p.x, p.y, p.z}; }
Vec3d  toVec(V p) { return {p.x, p.y, p.z}; }
bool   allFinite(const Vec3d& p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

// En RÄT RAM kring den enhetliga riktningen w: u ⊥ w och v = w × u, så
// (u, v, w) är högerhänt och en vinkel som växer från u mot v går MOTURS sett
// från w:s spets. u är den världsaxel som står mest vinkelrätt mot w, rätad
// mot den — x för en lodrät axel, så läge 0 i en lodrät cylinder ligger
// österut.
void frameAround(V w, V& u, V& v)
{
    const double ax = std::abs(w.x), ay = std::abs(w.y), az = std::abs(w.z);
    const V      e  = ax <= ay && ax <= az ? V{1.0, 0.0, 0.0}
                    : ay <= az             ? V{0.0, 1.0, 0.0}
                                           : V{0.0, 0.0, 1.0};
    u = unit(e - w * dot(e, w));
    v = cross(w, u);
}

// Lägger till ett läge med normal och svarar med dess index.
std::uint32_t vertex(MeshGeometry& out, V p, V n)
{
    out.positions.push_back(toVec(p));
    out.normals.push_back(toVec(n));
    return static_cast<std::uint32_t>(out.positions.size() - 1);
}

void triangle(MeshGeometry& out, std::uint32_t a, std::uint32_t b,
              std::uint32_t c)
{
    out.indices.insert(out.indices.end(), {a, b, c});
}

// Vinkeln för läge k av n i en ring.
double ringAngle(int k, int n)
{
    return 2.0 * std::numbers::pi * static_cast<double>(k) /
           static_cast<double>(n);
}

// En ring med `n` lägen kring punkten `c`: förskjutningarna r · (cos φ · u +
// sin φ · v). Normalen är förskjutningens riktning.
void ringOffsets(V u, V v, double r, int n, std::vector<V>& offs)
{
    offs.resize(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) {
        const double a = ringAngle(k, n);
        offs[static_cast<std::size_t>(k)] =
            u * (r * std::cos(a)) + v * (r * std::sin(a));
    }
}

// Ett LOCK: ringen `ring` (lägen i växande vinkel kring axeln `w`) som egen
// solfjäder med normalen `n`. `n` längs w ⇒ moturs sett från w (vinkeln
// växer moturs där); mot w ⇒ omvänd ordning, så locket alltid går moturs
// sett utifrån.
void cap(MeshGeometry& out, const std::vector<V>& ring, V n, bool alongAxis)
{
    const auto b = static_cast<std::uint32_t>(out.positions.size());
    for (const V& p : ring)
        vertex(out, p, n);
    const auto m = static_cast<std::uint32_t>(ring.size());
    for (std::uint32_t k = 1; k + 1 < m; ++k) {
        if (alongAxis)
            triangle(out, b, b + k, b + k + 1);
        else
            triangle(out, b, b + k + 1, b + k);
    }
}

// MANTELN mellan två ringar med lika många lägen, `a` före `b` längs axeln
// (ramen högerhänt kring den): fyrhörningen (a_k, a_k+1, b_k+1, b_k) — moturs
// sett utifrån.
void mantle(MeshGeometry& out, std::uint32_t a, std::uint32_t b, int n)
{
    const auto m = static_cast<std::uint32_t>(n);
    for (std::uint32_t k = 0; k < m; ++k) {
        const std::uint32_t k1 = (k + 1) % m;
        triangle(out, a + k, a + k1, b + k1);
        triangle(out, a + k, b + k1, b + k);
    }
}

// En polygon som misslyckades loggas EN gång per episod (som buildPolygon) —
// byggarna kan köras i en arbetartråd, därav atomic.
std::atomic<bool> g_planarLogged{false};

// Solfjäderns OTECKNADE area från första punkten — noll bara när alla
// punkter ligger på en linje (en fluga med lika stora öglor har tecknad area
// noll men syns).
double spannedArea(const std::vector<Point2d>& r)
{
    double a = 0.0;
    for (std::size_t i = 1; i + 1 < r.size(); ++i)
        a += std::abs((r[i].x - r[0].x) * (r[i + 1].y - r[0].y) -
                      (r[i].y - r[0].y) * (r[i + 1].x - r[0].x));
    return a / 2.0;
}

} // namespace

// ------------------------------------------------------------------ lådan

void box(const Vec3d& a, const Vec3d& b, MeshGeometry& out)
{
    if (!allFinite(a) || !allFinite(b))
        return;
    const V lo{std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
    const V hi{std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
    // Per sida: normalens axel och tecken, och de fyra hörnen MOTURS sett
    // utifrån som bitar (1 = x, 2 = y, 4 = z: biten satt ⇒ hi).
    struct Side {
        V            n;
        std::uint8_t c[4];
    };
    static constexpr Side kSides[] = {
        {{1, 0, 0}, {1, 3, 7, 5}},  {{-1, 0, 0}, {0, 4, 6, 2}},
        {{0, 1, 0}, {2, 6, 7, 3}},  {{0, -1, 0}, {0, 1, 5, 4}},
        {{0, 0, 1}, {4, 5, 7, 6}},  {{0, 0, -1}, {0, 2, 3, 1}},
    };
    for (const Side& s : kSides) {
        std::uint32_t q[4];
        for (int k = 0; k < 4; ++k) {
            const std::uint8_t c = s.c[k];
            q[k] = vertex(out,
                          {c & 1 ? hi.x : lo.x, c & 2 ? hi.y : lo.y,
                           c & 4 ? hi.z : lo.z},
                          s.n);
        }
        triangle(out, q[0], q[1], q[2]);
        triangle(out, q[0], q[2], q[3]);
    }
}

// ------------------------------------------------------------------ sfären

void sphere(const Vec3d& center, double radius, MeshGeometry& out, int slices,
            int stacks)
{
    if (!allFinite(center) || !std::isfinite(radius) || radius <= 0.0)
        return;
    slices = std::max(slices, 3);
    stacks = std::max(stacks, 2);
    const V c = toV(center);

    // Nordpolen, ringarna från norr mot söder och sydpolen.
    const std::uint32_t north = vertex(out, c + V{0, 0, radius}, {0, 0, 1});
    const std::uint32_t first = north + 1;
    for (int i = 1; i < stacks; ++i) {
        const double th = std::numbers::pi * static_cast<double>(i) /
                          static_cast<double>(stacks);
        const double st = std::sin(th), ct = std::cos(th);
        for (int j = 0; j < slices; ++j) {
            const double ph = ringAngle(j, slices);
            const V      n{st * std::cos(ph), st * std::sin(ph), ct};
            vertex(out, c + n * radius, n);
        }
    }
    const std::uint32_t south = vertex(out, c - V{0, 0, radius}, {0, 0, -1});

    const auto s    = static_cast<std::uint32_t>(slices);
    const auto ring = [&](int i) {   // ring i (1 … stacks − 1), läge 0
        return first + static_cast<std::uint32_t>(i - 1) * s;
    };
    for (std::uint32_t j = 0; j < s; ++j) {
        const std::uint32_t j1 = (j + 1) % s;
        triangle(out, north, ring(1) + j, ring(1) + j1);
        for (int i = 1; i + 1 < stacks; ++i) {
            const std::uint32_t a = ring(i) + j, d = ring(i) + j1;
            const std::uint32_t b = ring(i + 1) + j, cc = ring(i + 1) + j1;
            triangle(out, a, b, cc);
            triangle(out, a, cc, d);
        }
        triangle(out, ring(stacks - 1) + j, south, ring(stacks - 1) + j1);
    }
}

// ------------------------------------------------------------------ cylindern

void cylinder(const Vec3d& base, const Vec3d& top, double radius,
              MeshGeometry& out, int sides)
{
    if (!allFinite(base) || !allFinite(top) || !std::isfinite(radius) ||
        radius <= 0.0)
        return;
    const V b = toV(base), t = toV(top);
    const double h = length(t - b);
    if (!(h > 0.0) || !std::isfinite(h))
        return;
    sides = std::max(sides, 3);
    const V w = (t - b) * (1.0 / h);
    V       u, v;
    frameAround(w, u, v);
    std::vector<V> offs;
    ringOffsets(u, v, radius, sides, offs);

    // Manteln: den nedre ringen och den övre, normalen radiell.
    const auto lower = static_cast<std::uint32_t>(out.positions.size());
    for (const V& o : offs)
        vertex(out, b + o, unit(o));
    const auto upper = static_cast<std::uint32_t>(out.positions.size());
    for (const V& o : offs)
        vertex(out, t + o, unit(o));
    mantle(out, lower, upper, sides);

    // Locken med egna lägen: botten vänd mot −w, toppen mot +w.
    std::vector<V> ring(offs.size());
    for (std::size_t k = 0; k < offs.size(); ++k)
        ring[k] = b + offs[k];
    cap(out, ring, w * -1.0, false);
    for (std::size_t k = 0; k < offs.size(); ++k)
        ring[k] = t + offs[k];
    cap(out, ring, w, true);
}

// ------------------------------------------------------------------ röret

void tube(std::span<const Vec3d> path, double diameter, MeshGeometry& out,
          int sides)
{
    const double r = diameter / 2.0;
    if (!std::isfinite(r) || r <= 0.0)
        return;
    sides = std::max(sides, 3);

    // Rensa vägen: bara ändliga punkter, ingen lika med den förra.
    std::vector<V> p;
    p.reserve(path.size());
    for (const Vec3d& q : path) {
        if (!allFinite(q))
            continue;
        const V pq = toV(q);
        if (!p.empty() && pq.x == p.back().x && pq.y == p.back().y &&
            pq.z == p.back().z)
            continue;
        p.push_back(pq);
    }
    if (p.size() < 2)
        return;
    std::vector<V> d(p.size() - 1);
    for (std::size_t i = 0; i + 1 < p.size(); ++i)
        d[i] = unit(p[i + 1] - p[i]);

    V u, v;
    frameAround(d[0], u, v);
    std::vector<V> offs, ring;
    ringOffsets(u, v, r, sides, offs);

    // En bit börjar med en rät ring och ett lock vänt bakåt.
    std::uint32_t prevRing = 0;
    const auto startPiece = [&](V at, V dir) {
        ring.resize(offs.size());
        for (std::size_t k = 0; k < offs.size(); ++k)
            ring[k] = at + offs[k];
        cap(out, ring, dir * -1.0, false);
        prevRing = static_cast<std::uint32_t>(out.positions.size());
        for (std::size_t k = 0; k < offs.size(); ++k)
            vertex(out, ring[k], unit(offs[k]));
    };
    // …och slutar med en rät ring och ett lock vänt framåt.
    const auto endPiece = [&](V at, V dir) {
        const auto here = static_cast<std::uint32_t>(out.positions.size());
        ring.resize(offs.size());
        for (std::size_t k = 0; k < offs.size(); ++k) {
            ring[k] = at + offs[k];
            vertex(out, ring[k], unit(offs[k]));
        }
        mantle(out, prevRing, here, sides);
        cap(out, ring, dir, true);
    };

    startPiece(p[0], d[0]);
    for (std::size_t j = 1; j + 1 < p.size(); ++j) {
        const V      din = d[j - 1], dout = d[j];
        const V      bis = din + dout;
        const double bl  = length(bis);
        if (bl > 0.0 && 2.0 / bl <= kTubeMiterLimit) {
            // GERINGEN: ringen är den förra ramens prisma skuren av planet
            // med normalen `n` (knäets halveringsplan), och ramen förs vidare
            // som den ringens räta projektion mot den nya riktningen — det
            // är den minsta vridningen som tar din till dout, och den möter
            // exakt samma ring från andra hållet. v följer av u och den nya
            // riktningen (ramen är högerhänt).
            const V      n  = bis * (1.0 / bl);
            const double dn = dot(din, n);
            const auto here = static_cast<std::uint32_t>(out.positions.size());
            const V uq = u - din * (dot(u, n) / dn);
            const V u2 = unit(uq - dout * dot(uq, dout));
            const V v2 = cross(dout, u2);
            std::vector<V> offs2;
            ringOffsets(u2, v2, r, sides, offs2);
            for (std::size_t k = 0; k < offs.size(); ++k) {
                const V q = offs[k] - din * (dot(offs[k], n) / dn);
                vertex(out, p[j] + q, unit(offs[k] + offs2[k]));
            }
            mantle(out, prevRing, here, sides);
            prevRing = here;
            u = u2;
            v = v2;
            offs.swap(offs2);
        } else {
            // För skarpt (eller rakt bakåt): biten slutar här och en ny börjar
            // i samma punkt med en ram vinkelrät mot den nya riktningen —
            // den av u och v som står minst parallellt med dout, rätad.
            endPiece(p[j], din);
            const V uu = u - dout * dot(u, dout);
            const V vv = v - dout * dot(v, dout);
            u = unit(dot(uu, uu) >= dot(vv, vv) ? uu : vv);
            v = cross(dout, u);
            ringOffsets(u, v, r, sides, offs);
            startPiece(p[j], dout);
        }
    }
    endPiece(p.back(), d.back());
}

// ------------------------------------------------------------- polygonen

bool triangulatePlanar(std::span<const std::vector<Vec3d>> rings,
                       MeshGeometry&                       out)
{
    if (rings.empty())
        return true;

    // Ytterringen rensad i rymden: ändliga punkter, ingen lika med den förra,
    // ingen upprepad slutpunkt.
    std::vector<V> outer;
    for (const Vec3d& q : rings[0]) {
        if (!allFinite(q))
            continue;
        const V pq = toV(q);
        if (!outer.empty() && pq.x == outer.back().x &&
            pq.y == outer.back().y && pq.z == outer.back().z)
            continue;
        outer.push_back(pq);
    }
    while (outer.size() > 1 && outer.back().x == outer.front().x &&
           outer.back().y == outer.front().y && outer.back().z == outer.front().z)
        outer.pop_back();
    if (outer.size() < 3)
        return true;

    // PLANET: Newells normal relativt första punkten (SWEREF-talen hade
    // annars ätit siffrorna) — riktningen är den ytterringen går moturs
    // kring, och längden två gånger den projicerade arean. Solfjäderns
    // OTECKNADE area avgör om ringen alls spänner en yta (på en linje ⇒
    // ingen yta och inget fel). En ring vars tecknade area tar ut sig (en
    // symmetrisk fluga) har ingen Newell-normal men ett plan ändå: dess
    // största solfjäderstriangel ger det, med godtycklig framsida.
    const V o = outer.front();
    V       nn{}, widest{};
    double  ext = 0.0, fan = 0.0, widestLen = 0.0;
    for (std::size_t i = 0; i < outer.size(); ++i) {
        const V a = outer[i] - o;
        const V b = outer[(i + 1) % outer.size()] - o;
        nn.x += (a.y - b.y) * (a.z + b.z);
        nn.y += (a.z - b.z) * (a.x + b.x);
        nn.z += (a.x - b.x) * (a.y + b.y);
        ext = std::max(ext, dot(a, a));
        if (i > 0 && i + 1 < outer.size()) {
            const V      c = cross(a, b);
            const double l = length(c);
            fan += l;
            if (l > widestLen) {
                widestLen = l;
                widest    = c;
            }
        }
    }
    if (!(fan > 1e-12 * ext) || !std::isfinite(fan))
        return true;
    const double nl = length(nn);
    const V      n  = nl > 1e-9 * fan ? nn * (1.0 / nl) : widest * (1.0 / widestLen);
    V       u, v;
    frameAround(n, u, v);

    // Ringarna i planets koordinater (u, v) relativt o, rensade på nytt där
    // — två skilda punkter kan sammanfalla i projektionen — och med varje
    // punkts plats i indatan kvar, så lägena blir indatans egna.
    std::vector<Point2d>       pts;
    std::vector<std::uint32_t> starts;
    std::vector<V>             src;
    double                     outerSpan = 0.0;
    for (std::size_t k = 0; k < rings.size(); ++k) {
        std::vector<Point2d> r2;
        std::vector<V>       r3;
        for (const Vec3d& q : rings[k]) {
            if (!allFinite(q))
                continue;
            const V       pq = toV(q);
            const Point2d e{dot(pq - o, u), dot(pq - o, v)};
            if (!r2.empty() && r2.back().x == e.x && r2.back().y == e.y)
                continue;
            r2.push_back(e);
            r3.push_back(pq);
        }
        while (r2.size() > 1 && r2.back().x == r2.front().x &&
               r2.back().y == r2.front().y) {
            r2.pop_back();
            r3.pop_back();
        }
        if (r2.size() < 3) {
            if (k == 0)
                return true;
            continue;
        }
        const double span = spannedArea(r2);
        if (k == 0)
            outerSpan = span;
        if (outerSpan == 0.0 || span <= 1e-12 * outerSpan) {
            if (k == 0)
                return true;
            continue;
        }
        starts.push_back(static_cast<std::uint32_t>(pts.size()));
        pts.insert(pts.end(), r2.begin(), r2.end());
        src.insert(src.end(), r3.begin(), r3.end());
    }

    std::vector<std::uint32_t> tri;
    const bool                 ok = triangulate(pts, starts, tri);

    // Lägena är indatans punkter med planets normal; varje triangel vänds så
    // att den går moturs i (u, v) — alltså moturs sett från normalen.
    const auto base = static_cast<std::uint32_t>(out.positions.size());
    for (const V& q : src)
        vertex(out, q, n);
    for (std::size_t t = 0; t + 2 < tri.size(); t += 3) {
        const Point2d& a = pts[tri[t]];
        const Point2d& b = pts[tri[t + 1]];
        const Point2d& c = pts[tri[t + 2]];
        const double   s = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (s < 0.0)
            triangle(out, base + tri[t], base + tri[t + 2], base + tri[t + 1]);
        else
            triangle(out, base + tri[t], base + tri[t + 1], base + tri[t + 2]);
    }

    if (ok) {
        g_planarLogged.store(false);
    } else if (!g_planarLogged.exchange(true)) {
        log("triangulatePlanar: ringarna är ingen enkel yta i sitt plan "
            "(självkorsning eller ett hål utanför) — ytan ritas approximativt; "
            "loggas en gång tills en yta lyckas igen");
    }
    return ok;
}

// ------------------------------------------------- linjerna i rymden
//
// SPEGEL av `buildPolyline` (geom2d.cpp) — samma bitar i samma ordning, med
// punkterna i xyz. En ändring av bitarnas form görs i båda.

namespace {

// En bit är fyra vertex och två trianglar — ett segment eller en rund bit.
constexpr std::size_t kLinePieceVertices = 4;

// Chunken nästa bit hamnar i: den sista om biten ryms där, annars en ny med
// origo i bitens punkt.
LineChunk3& chunkFor3(std::vector<LineChunk3>& chunks, const Vec3d& at)
{
    if (chunks.empty() ||
        chunks.back().vertices.size() + kLinePieceVertices > kMaxChunkVertices) {
        LineChunk3 c;
        c.originX = at.x;
        c.originY = at.y;
        c.originZ = at.z;
        chunks.push_back(std::move(c));
    }
    return chunks.back();
}

// En vertex, relativt chunkens origo: subtraktionen i double, först
// resultatet avrundas till float.
void push3(LineChunk3& c, const Vec3d& p, const Vec3d& prev, const Vec3d& next,
           double dist, LineKind kind, int side, std::uint8_t ends,
           const PolylineStyle& st)
{
    LineVertex3 v;
    v.x     = static_cast<float>(p.x - c.originX);
    v.y     = static_cast<float>(p.y - c.originY);
    v.z     = static_cast<float>(p.z - c.originZ);
    v.prevX = static_cast<float>(prev.x - c.originX);
    v.prevY = static_cast<float>(prev.y - c.originY);
    v.prevZ = static_cast<float>(prev.z - c.originZ);
    v.nextX = static_cast<float>(next.x - c.originX);
    v.nextY = static_cast<float>(next.y - c.originY);
    v.nextZ = static_cast<float>(next.z - c.originZ);
    v.dist  = static_cast<float>(dist);
    v.width = st.width;
    v.color = st.color;
    v.side  = static_cast<std::int8_t>(side);
    v.kind  = static_cast<std::uint8_t>(kind);
    v.ends  = ends;
    c.vertices.push_back(v);
}

// De fyra senaste vertexen som quad: 0–1–2 och 2–1–3 (geom2d.cpp:s ordning).
void quad3(LineChunk3& c)
{
    const auto b = static_cast<std::uint32_t>(c.vertices.size() - 4);
    c.indices.insert(c.indices.end(), {b, b + 1, b + 2, b + 2, b + 1, b + 3});
}

void segmentPiece3(std::vector<LineChunk3>& chunks, const Vec3d& a,
                   const Vec3d& b, const Vec3d& prevA, const Vec3d& nextB,
                   double distA, double distB, std::uint8_t ends,
                   const PolylineStyle& st)
{
    LineChunk3& c = chunkFor3(chunks, a);
    push3(c, a, prevA, b, distA, LineKind::SegmentStart, -1, ends, st);
    push3(c, a, prevA, b, distA, LineKind::SegmentStart, +1, ends, st);
    push3(c, b, a, nextB, distB, LineKind::SegmentEnd, -1, ends, st);
    push3(c, b, a, nextB, distB, LineKind::SegmentEnd, +1, ends, st);
    quad3(c);
}

void cornerPiece3(std::vector<LineChunk3>& chunks, const Vec3d& p,
                  const Vec3d& prev, const Vec3d& next, double dist,
                  LineKind kind, const PolylineStyle& st)
{
    LineChunk3& c = chunkFor3(chunks, p);
    for (int corner = 0; corner < 4; ++corner)
        push3(c, p, prev, next, dist, kind, corner, 0, st);
    quad3(c);
}

bool samePoint(const Vec3d& a, const Vec3d& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

} // namespace

void buildPolyline3(std::span<const Vec3d> points, const PolylineStyle& style,
                    std::vector<LineChunk3>& chunks)
{
    // Rensa: bara ändliga punkter, och ingen punkt lika med den förra.
    std::vector<Vec3d> p;
    p.reserve(points.size());
    for (const Vec3d& q : points) {
        if (!allFinite(q))
            continue;
        if (!p.empty() && samePoint(p.back(), q))
            continue;
        p.push_back(q);
    }
    bool closed = style.closed;
    while (closed && p.size() > 1 && samePoint(p.back(), p.front()))
        p.pop_back();
    const std::size_t n = p.size();
    if (closed && n < 3)
        closed = false;
    if (n < 2)
        return;

    // 3D-LÄNGDEN fram till varje segments början, i double hela vägen.
    const std::size_t   segs = closed ? n : n - 1;
    std::vector<double> dist(segs + 1, 0.0);
    for (std::size_t i = 1; i <= segs; ++i)
        dist[i] = dist[i - 1] + length(toV(p[i % n]) - toV(p[i - 1]));

    const auto prevOf = [&](std::size_t i) {
        return i > 0 ? p[i - 1] : (closed ? p[n - 1] : p[i]);
    };
    const auto nextOf = [&](std::size_t i) {
        return i + 1 < n ? p[i + 1] : (closed ? p[0] : p[i]);
    };

    const LineKind join = style.join == LineJoin::Miter   ? LineKind::Miter
                          : style.join == LineJoin::Bevel ? LineKind::Bevel
                                                          : LineKind::Round;
    const std::uint8_t cap = style.cap == LineCap::Round    ? lineflag::kCapRound
                             : style.cap == LineCap::Square ? lineflag::kCapSquare
                                                            : std::uint8_t{0};
    if (closed)
        cornerPiece3(chunks, p[0], p[n - 1], p[1], 0.0, join, style);
    else if (style.cap == LineCap::Round)
        cornerPiece3(chunks, p[0], p[0], p[1], 0.0, LineKind::Round, style);
    for (std::size_t i = 0; i < segs; ++i) {
        const std::size_t j    = (i + 1) % n;
        std::uint8_t      ends = cap;
        if (!closed && i == 0)
            ends |= lineflag::kStartsLine;
        if (!closed && i + 1 == segs)
            ends |= lineflag::kEndsLine;
        segmentPiece3(chunks, p[i], p[j], prevOf(i), nextOf(j), dist[i],
                      dist[i + 1], ends, style);
        if (i + 1 < segs)
            cornerPiece3(chunks, p[j], p[i], nextOf(j), dist[i + 1], join,
                         style);
    }
    if (!closed && style.cap == LineCap::Round)
        cornerPiece3(chunks, p[n - 1], p[n - 2], p[n - 1], dist[segs],
                     LineKind::Round, style);
}

} // namespace gfx
