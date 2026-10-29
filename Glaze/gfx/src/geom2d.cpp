// Geometribyggarna — backend-neutrala: polylinjen till
// Line-programmets bitar och ytan till Fill-programmets trianglar
// och kantremsor. Kontraktet står i include/gfx/geom2d.h;
// vertexformaten i include/gfx/scene.h, trianguleringen i earcut.cpp.

#include "gfx/geom2d.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

#include "earcut.h"
#include "gfx/platform.h"

namespace gfx {
namespace {

// En bit är fyra vertex och två trianglar — ett segment eller en rund bit.
constexpr std::size_t kPieceVertices = 4;

// Chunken nästa bit hamnar i: den sista om biten ryms där, annars en ny med
// origo i bitens punkt. Biten delas aldrig — hela quaden ritas med samma
// matris.
LineChunk& chunkFor(std::vector<LineChunk>& chunks, const Point2d& at)
{
    if (chunks.empty() ||
        chunks.back().vertices.size() + kPieceVertices > kMaxChunkVertices) {
        LineChunk c;
        c.originX = at.x;
        c.originY = at.y;
        chunks.push_back(std::move(c));
    }
    return chunks.back();
}

// En vertex, relativt chunkens origo: subtraktionen i double, först
// resultatet avrundas till float.
void push(LineChunk& c, const Point2d& p, const Point2d& prev,
          const Point2d& next, double dist, LineKind kind, int side,
          std::uint8_t ends, const PolylineStyle& st)
{
    LineVertex v;
    v.x     = static_cast<float>(p.x - c.originX);
    v.y     = static_cast<float>(p.y - c.originY);
    v.prevX = static_cast<float>(prev.x - c.originX);
    v.prevY = static_cast<float>(prev.y - c.originY);
    v.nextX = static_cast<float>(next.x - c.originX);
    v.nextY = static_cast<float>(next.y - c.originY);
    v.dist  = static_cast<float>(dist);
    v.width = st.width;
    v.color = st.color;
    v.side  = static_cast<std::int8_t>(side);
    v.kind  = static_cast<std::uint8_t>(kind);
    v.ends  = ends;
    c.vertices.push_back(v);
}

// De fyra senaste vertexen som quad: 0–1–2 och 2–1–3. Ordningen är
// segmentets (start −, start +, slut −, slut +) och den runda bitens hörn
// (bit 0 = +x, bit 1 = +y) — i båda fallen två motstående par.
void quad(LineChunk& c)
{
    const auto b = static_cast<std::uint32_t>(c.vertices.size() - 4);
    c.indices.insert(c.indices.end(), {b, b + 1, b + 2, b + 2, b + 1, b + 3});
}

// Ett segment: början −/+ och slutet −/+. `ends` (lineflag) är densamma i
// alla fyra — varje vertex räknar båda ändarnas kanter.
void segmentPiece(std::vector<LineChunk>& chunks, const Point2d& a,
                  const Point2d& b, const Point2d& prevA, const Point2d& nextB,
                  double distA, double distB, std::uint8_t ends,
                  const PolylineStyle& st)
{
    LineChunk& c = chunkFor(chunks, a);
    push(c, a, prevA, b, distA, LineKind::SegmentStart, -1, ends, st);
    push(c, a, prevA, b, distA, LineKind::SegmentStart, +1, ends, st);
    push(c, b, a, nextB, distB, LineKind::SegmentEnd, -1, ends, st);
    push(c, b, a, nextB, distB, LineKind::SegmentEnd, +1, ends, st);
    quad(c);
}

// En bit i en punkt — en skarv (Round/Miter/Bevel) eller en rund ände:
// fyra hörn 0–3 med punkten och dess grannar; formen räknas i vertexshadern.
void cornerPiece(std::vector<LineChunk>& chunks, const Point2d& p,
                 const Point2d& prev, const Point2d& next, double dist,
                 LineKind kind, const PolylineStyle& st)
{
    LineChunk& c = chunkFor(chunks, p);
    for (int corner = 0; corner < 4; ++corner)
        push(c, p, prev, next, dist, kind, corner, 0, st);
    quad(c);
}

LineKind joinKind(LineJoin j)
{
    switch (j) {
    case LineJoin::Miter: return LineKind::Miter;
    case LineJoin::Bevel: return LineKind::Bevel;
    case LineJoin::Round: break;
    }
    return LineKind::Round;
}

std::uint8_t capFlag(LineCap c)
{
    switch (c) {
    case LineCap::Round:  return lineflag::kCapRound;
    case LineCap::Square: return lineflag::kCapSquare;
    case LineCap::Butt:   break;
    }
    return 0;
}

} // namespace

void buildPolyline(std::span<const Point2d> points, const PolylineStyle& style,
                   std::vector<LineChunk>& chunks)
{
    // Rensa: bara ändliga punkter, och ingen punkt lika med den förra — ett
    // segment utan längd har ingen riktning att extrudera längs.
    std::vector<Point2d> p;
    p.reserve(points.size());
    for (const Point2d& q : points) {
        if (!std::isfinite(q.x) || !std::isfinite(q.y))
            continue;
        if (!p.empty() && p.back().x == q.x && p.back().y == q.y)
            continue;
        p.push_back(q);
    }
    // En SLUTEN linje går själv tillbaka till början — en upprepad slutpunkt
    // är samma punkt, och med två punkter finns ingen ring (den blir öppen).
    bool closed = style.closed;
    while (closed && p.size() > 1 && p.back().x == p.front().x &&
           p.back().y == p.front().y)
        p.pop_back();
    const std::size_t n = p.size();
    if (closed && n < 3)
        closed = false;
    if (n < 2)
        return;

    // Längden fram till varje segments början, i double hela vägen; den
    // sista posten är hela längden (för en sluten linje med sista segmentet
    // tillbaka till början).
    const std::size_t   segs = closed ? n : n - 1;
    std::vector<double> dist(segs + 1, 0.0);
    for (std::size_t i = 1; i <= segs; ++i) {
        const Point2d& a = p[i - 1];
        const Point2d& b = p[i % n];
        dist[i]          = dist[i - 1] + std::hypot(b.x - a.x, b.y - a.y);
    }

    // En punkt utan granne bär sig själv (scene.h); i en sluten linje är
    // första och sista punkten grannar.
    const auto prevOf = [&](std::size_t i) {
        return i > 0 ? p[i - 1] : (closed ? p[n - 1] : p[i]);
    };
    const auto nextOf = [&](std::size_t i) {
        return i + 1 < n ? p[i + 1] : (closed ? p[0] : p[i]);
    };

    const LineKind     join = joinKind(style.join);
    const std::uint8_t cap  = capFlag(style.cap);
    if (closed)
        cornerPiece(chunks, p[0], p[n - 1], p[1], 0.0, join, style);
    else if (style.cap == LineCap::Round)
        cornerPiece(chunks, p[0], p[0], p[1], 0.0, LineKind::Round, style);
    for (std::size_t i = 0; i < segs; ++i) {
        const std::size_t j    = (i + 1) % n;   // segmentets slutpunkt
        std::uint8_t      ends = cap;
        if (!closed && i == 0)
            ends |= lineflag::kStartsLine;
        if (!closed && i + 1 == segs)
            ends |= lineflag::kEndsLine;
        segmentPiece(chunks, p[i], p[j], prevOf(i), nextOf(j), dist[i],
                     dist[i + 1], ends, style);
        // Skarven i segmentets slut — där linjen fortsätter. Den slutna
        // linjens sista segment slutar i första punkten, vars skarv redan
        // ligger först.
        if (i + 1 < segs)
            cornerPiece(chunks, p[j], p[i], nextOf(j), dist[i + 1], join,
                        style);
    }
    if (!closed && style.cap == LineCap::Round)
        cornerPiece(chunks, p[n - 1], p[n - 2], p[n - 1], dist[segs],
                    LineKind::Round, style);
}

bool makeDash(std::span<const float> pattern, float offset, SceneDash& out)
{
    out = SceneDash{};
    if (pattern.empty())
        return true;
    double sum = 0.0;
    for (const float v : pattern) {
        if (!std::isfinite(v) || v < 0.0f)
            return true;   // SVG: ett ogiltigt mönster ritas heldraget
        sum += v;
    }
    if (!(sum > 0.0))
        return true;
    // Ett udda antal upprepas, så att på och av alltid står i par.
    const std::size_t n =
        pattern.size() % 2 != 0 ? pattern.size() * 2 : pattern.size();
    if (n > static_cast<std::size_t>(kMaxDashes))
        return false;
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        acc += pattern[i % pattern.size()];
        out.ends[i] = static_cast<float>(acc);
    }
    out.count  = static_cast<int>(n);
    out.offset = std::isfinite(offset) ? offset : 0.0f;
    return true;
}

// ------------------------------------------------------------------ ytorna

namespace {

// En ytvertex under bygget, i DOUBLE — den blir relativ sin chunks origo
// först när chunken är vald.
struct FillPiece {
    Point2d      p, prev, next;
    FillKind     kind = FillKind::EdgeStart;
    std::uint8_t side = 0;
};

FillVertex toVertex(const FillPiece& q, const FillChunk& c, Color color)
{
    FillVertex v;
    v.x     = static_cast<float>(q.p.x - c.originX);
    v.y     = static_cast<float>(q.p.y - c.originY);
    v.prevX = static_cast<float>(q.prev.x - c.originX);
    v.prevY = static_cast<float>(q.prev.y - c.originY);
    v.nextX = static_cast<float>(q.next.x - c.originX);
    v.nextY = static_cast<float>(q.next.y - c.originY);
    v.color = color;
    v.kind  = static_cast<std::uint8_t>(q.kind);
    v.side  = q.side;
    return v;
}

// Ytan i den sista chunken om HELA ytan ryms där, annars i en ny med origo i
// `origin`. En yta som inte ryms ens i en tom chunk fördelas över flera nya
// med samma origo, trianglarna så att varje chunk håller sig under taket (en
// vertex som behövs i två chunkar kopieras).
void appendFill(std::vector<FillChunk>& chunks,
                const std::vector<FillPiece>& v,
                const std::vector<std::uint32_t>& idx, Color color,
                const Point2d& origin)
{
    const auto fresh = [&] {
        FillChunk c;
        c.originX = origin.x;
        c.originY = origin.y;
        chunks.push_back(std::move(c));
    };
    if (v.size() <= kMaxChunkVertices) {
        if (chunks.empty() ||
            chunks.back().vertices.size() + v.size() > kMaxChunkVertices)
            fresh();
        FillChunk& c    = chunks.back();
        const auto base = static_cast<std::uint32_t>(c.vertices.size());
        for (const FillPiece& q : v)
            c.vertices.push_back(toVertex(q, c, color));
        for (std::uint32_t i : idx)
            c.indices.push_back(base + i);
        return;
    }

    std::vector<std::int64_t> local(v.size(), -1);   // index i aktuell chunk
    fresh();
    for (std::size_t t = 0; t + 2 < idx.size(); t += 3) {
        std::size_t nya = 0;
        for (std::size_t k = 0; k < 3; ++k)
            nya += local[idx[t + k]] < 0 ? 1 : 0;
        if (chunks.back().vertices.size() + nya > kMaxChunkVertices) {
            fresh();
            std::fill(local.begin(), local.end(), -1);
        }
        FillChunk& c = chunks.back();
        for (std::size_t k = 0; k < 3; ++k) {
            std::int64_t& l = local[idx[t + k]];
            if (l < 0) {
                l = static_cast<std::int64_t>(c.vertices.size());
                c.vertices.push_back(toVertex(v[idx[t + k]], c, color));
            }
            c.indices.push_back(static_cast<std::uint32_t>(l));
        }
    }
}

// Den tecknade arean (positiv moturs med y uppåt), relativt punkten o —
// SWEREF-talen hade annars ätit siffrorna.
double signedArea(std::span<const Point2d> r, const Point2d& o)
{
    double a = 0.0;
    for (std::size_t i = 0, j = r.size() - 1; i < r.size(); j = i++)
        a += (r[j].x - o.x) * (r[i].y - o.y) - (r[i].x - o.x) * (r[j].y - o.y);
    return a / 2.0;
}

// Solfjäderns OTECKNADE area från första punkten: noll bara när alla punkter
// ligger på en linje. Den tecknade arean räcker inte som prov — en fluga med
// lika stora öglor har tecknad area noll men syns.
double spannedArea(std::span<const Point2d> r)
{
    double a = 0.0;
    for (std::size_t i = 1; i + 1 < r.size(); ++i)
        a += std::abs((r[i].x - r[0].x) * (r[i + 1].y - r[0].y) -
                      (r[i].y - r[0].y) * (r[i + 1].x - r[0].x));
    return a / 2.0;
}

// En misslyckad yta loggas EN gång per episod — en yta som byggs om varje
// frame (Painter) hade annars dränkt loggen. Byggarna kan köras i en
// arbetartråd, därav atomic.
std::atomic<bool> g_polygonLogged{false};

} // namespace

bool buildPolygon(std::span<const std::vector<Point2d>> rings,
                  const PolygonStyle& style, std::vector<FillChunk>& chunks)
{
    // Rensa ringarna till EN platt punktlista: bara ändliga punkter, ingen
    // lika med den förra och ingen upprepad slutpunkt. Ytterringen MOTURS och
    // hålen MEDURS, så ytan ligger till vänster om varje kant — det är det
    // kantremsans "utåt" bygger på. Ringar utan area tas bort: deras kanter
    // hade ritat en remsa men ingen yta.
    std::vector<Point2d>       pts;
    std::vector<std::uint32_t> starts;
    Point2d                    o{};
    double                     outerSpan = 0.0;
    for (std::size_t k = 0; k < rings.size(); ++k) {
        std::vector<Point2d> r;
        r.reserve(rings[k].size());
        for (const Point2d& q : rings[k]) {
            if (!std::isfinite(q.x) || !std::isfinite(q.y))
                continue;
            if (!r.empty() && r.back().x == q.x && r.back().y == q.y)
                continue;
            r.push_back(q);
        }
        while (r.size() > 1 && r.back().x == r.front().x &&
               r.back().y == r.front().y)
            r.pop_back();
        if (k == 0) {
            if (r.size() < 3)
                return true;   // ingen ytterring: ingen yta, inget fel
            o = r.front();
        }
        if (r.size() < 3)
            continue;
        const double span = spannedArea(r);
        if (k == 0)
            outerSpan = span;
        if (span <= 1e-12 * outerSpan || outerSpan == 0.0)
            continue;   // på en linje — för ytterringen: ingen yta alls
        if ((signedArea(r, o) > 0.0) != (k == 0))
            std::reverse(r.begin(), r.end());
        starts.push_back(static_cast<std::uint32_t>(pts.size()));
        pts.insert(pts.end(), r.begin(), r.end());
    }
    if (starts.empty() || outerSpan == 0.0)
        return true;

    std::vector<std::uint32_t> tri;
    const bool ok = triangulate(pts, starts, tri);

    // Kantquadarna: kanten g → h i en ring får vertex 4g … 4g+3 (början
    // inne/ute, slutet inne/ute), och de inre trianglarna använder kantens
    // början inne (4g) — samma punkt.
    std::vector<FillPiece> v(pts.size() * 4);
    std::vector<std::uint32_t> idx;
    idx.reserve(tri.size() + pts.size() * 6);
    for (std::uint32_t t : tri)
        idx.push_back(t * 4);
    for (std::size_t k = 0; k < starts.size(); ++k) {
        const std::size_t b = starts[k];
        const std::size_t e = k + 1 < starts.size() ? starts[k + 1] : pts.size();
        const std::size_t n = e - b;
        for (std::size_t j = 0; j < n; ++j) {
            const std::size_t g  = b + j;
            const std::size_t h  = b + (j + 1) % n;
            const std::size_t gp = b + (j + n - 1) % n;
            const std::size_t hn = b + (j + 2) % n;
            for (std::uint8_t s = 0; s < 2; ++s) {
                v[4 * g + s]     = FillPiece{pts[g], pts[gp], pts[h],
                                             FillKind::EdgeStart, s};
                v[4 * g + 2 + s] = FillPiece{pts[h], pts[g], pts[hn],
                                             FillKind::EdgeEnd, s};
            }
            const auto q = static_cast<std::uint32_t>(4 * g);
            idx.insert(idx.end(), {q, q + 1, q + 2, q + 2, q + 1, q + 3});
        }
    }
    appendFill(chunks, v, idx, style.color, o);

    if (ok) {
        g_polygonLogged.store(false);
    } else if (!g_polygonLogged.exchange(true)) {
        log("buildPolygon: ringarna är ingen enkel yta (självkorsning eller "
            "ett hål utanför) — ytan ritas approximativt; loggas en gång tills "
            "en yta lyckas igen");
    }
    return ok;
}

// ------------------------------------------------------- bilden på fyra hörn

bool quadWeights(const Point2d (&ring)[4], float (&q)[4])
{
    for (const Point2d& p : ring)
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
            return false;
    // Diagonalerna d1 = p2 − p0 och d2 = p3 − p1 skär varandra där
    // p0 + t·d1 = p1 + s·d2: med w = p1 − p0 är t = (w × d2)/(d1 × d2) och
    // s = (w × d1)/(d1 × d2).
    const double d1x = ring[2].x - ring[0].x, d1y = ring[2].y - ring[0].y;
    const double d2x = ring[3].x - ring[1].x, d2y = ring[3].y - ring[1].y;
    const double wx = ring[1].x - ring[0].x, wy = ring[1].y - ring[0].y;
    const double kryss = d1x * d2y - d1y * d2x;
    // Parallella (eller utan längd) diagonaler — relativt deras längder, så
    // talen i SWEREF och i px avgörs lika.
    const double skala = std::hypot(d1x, d1y) * std::hypot(d2x, d2y);
    if (!(skala > 0.0) || !(std::abs(kryss) > 1e-12 * skala))
        return false;
    const double t = (wx * d2y - wy * d2x) / kryss;
    const double s = (wx * d1y - wy * d1x) / kryss;
    // STRIKT KONVEX och inte självkorsande: skärningen ligger inne i BÅDA
    // diagonalerna. ε i parameterns enhet fäller också hörn som bara nästan
    // ligger i linje — deras vikter hade blivit oändliga i float.
    constexpr double kEps = 1e-9;
    if (!(t > kEps && t < 1.0 - kEps && s > kEps && s < 1.0 - kEps))
        return false;
    const double w[4] = {1.0 / (1.0 - t), 1.0 / (1.0 - s), 1.0 / t, 1.0 / s};
    const double störst = std::max(std::max(w[0], w[1]), std::max(w[2], w[3]));
    for (int i = 0; i < 4; ++i)
        q[i] = static_cast<float>(w[i] / störst);
    return true;
}

// ------------------------------------------------------------ gradienterna

void buildGradientRamp(std::span<const GradientStop> stops, GradientRamp& out)
{
    std::vector<GradientStop> s;
    s.reserve(stops.size());
    for (const GradientStop& g : stops)
        if (std::isfinite(g.offset))
            s.push_back(GradientStop{std::clamp(g.offset, 0.0f, 1.0f),
                                     g.color});
    std::stable_sort(s.begin(), s.end(),
                     [](const GradientStop& a, const GradientStop& b) {
                         return a.offset < b.offset;
                     });

    const auto put = [&](int i, double r, double g, double b, double a) {
        const auto byte = [](double v) {
            return static_cast<std::uint8_t>(
                std::lround(std::clamp(v, 0.0, 255.0)));
        };
        out[i * 4 + 0] = byte(r);
        out[i * 4 + 1] = byte(g);
        out[i * 4 + 2] = byte(b);
        out[i * 4 + 3] = byte(a);
    };
    for (int i = 0; i < kGradientRampSize; ++i) {
        if (s.empty()) {
            put(i, 0, 0, 0, 0);
            continue;
        }
        const double t = i / double(kGradientRampSize - 1);
        if (t <= s.front().offset || t >= s.back().offset) {
            const Color& c = t <= s.front().offset ? s.front().color
                                                   : s.back().color;
            put(i, c.r, c.g, c.b, c.a);
            continue;
        }
        // Det SISTA stoppet på eller före t — vid två stopp på samma offset
        // vinner det senare, och gränsen blir skarp.
        std::size_t k = 0;
        while (k + 1 < s.size() && s[k + 1].offset <= t)
            ++k;
        const GradientStop& a  = s[k];
        const GradientStop& b  = s[k + 1];
        const double        f  = (t - a.offset) / (b.offset - a.offset);
        const double        aa = a.color.a / 255.0, ba = b.color.a / 255.0;
        const double        pa = aa * (1.0 - f) + ba * f;
        if (pa <= 0.0) {
            put(i, 0, 0, 0, 0);
            continue;
        }
        // Premultiplicerat: färgen viktas med sin alfa, och delas tillbaka.
        put(i, (a.color.r * aa * (1.0 - f) + b.color.r * ba * f) / pa,
            (a.color.g * aa * (1.0 - f) + b.color.g * ba * f) / pa,
            (a.color.b * aa * (1.0 - f) + b.color.b * ba * f) / pa, pa * 255.0);
    }
}

ScenePaint linearPaint(Point2d from, Point2d to, double originX,
                       double originY)
{
    ScenePaint p;
    p.kind = PaintKind::Linear;
    p.x0   = static_cast<float>(from.x - originX);
    p.y0   = static_cast<float>(from.y - originY);
    p.x1   = static_cast<float>(to.x - originX);
    p.y1   = static_cast<float>(to.y - originY);
    return p;
}

ScenePaint radialPaint(Point2d center, double r0, double r1, double originX,
                       double originY)
{
    ScenePaint p;
    p.kind = PaintKind::Radial;
    p.x0   = static_cast<float>(center.x - originX);
    p.y0   = static_cast<float>(center.y - originY);
    p.r0   = static_cast<float>(r0);
    p.r1   = static_cast<float>(r1);
    return p;
}

} // namespace gfx
