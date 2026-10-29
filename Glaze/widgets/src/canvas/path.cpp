// glaze::Path — segmenten och deras plattning. Kurvorna
// delas rekursivt tills kontrollpunkterna ligger nära kordan, med en tolerans
// RELATIV kurvans storlek: zoomen är okänd när formen byggs, och geometrin
// laddas upp en gång. Kontraktet står i glaze/canvas/path.h.

#include "glaze/canvas/path.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace glaze {

namespace {

// Toleransen: kontrollpunkternas avstånd till kordan får vara högst så stor
// andel av kurvans kontrollpolygons utsträckning. 1/1000 ger en kurva som
// håller sig under en halv px tills den är fem hundra px stor på skärmen —
// och en kurva som förstoras ännu mer visar sina hörn (skriven gräns).
constexpr double kFlatness = 1.0 / 1000.0;
// Delningens djup: högst 2¹² segment per kurva.
constexpr int kMaxDepth = 12;

double distToChord(Point p, Point a, Point b)
{
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double len = std::hypot(dx, dy);
    if (len <= 0.0)
        return std::hypot(p.x - a.x, p.y - a.y);
    return std::abs((p.x - a.x) * dy - (p.y - a.y) * dx) / len;
}

Point mid(Point a, Point b) { return Point{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0}; }

// De Casteljau i mitten tills kurvan är platt; ändpunkten läggs till, inte
// startpunkten (den står redan sist i konturen).
void cubic(Point a, Point c1, Point c2, Point b, double tol, int depth,
           std::vector<Point>& out)
{
    if (depth >= kMaxDepth ||
        std::max(distToChord(c1, a, b), distToChord(c2, a, b)) <= tol) {
        out.push_back(b);
        return;
    }
    const Point ab = mid(a, c1), bc = mid(c1, c2), cd = mid(c2, b);
    const Point abc = mid(ab, bc), bcd = mid(bc, cd);
    const Point m   = mid(abc, bcd);
    cubic(a, ab, abc, m, tol, depth + 1, out);
    cubic(m, bcd, cd, b, tol, depth + 1, out);
}

// Toleransen för en kurva: andelen av kontrollpolygonens utsträckning.
double tolerance(std::initializer_list<Point> pts)
{
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
    bool   first = true;
    for (const Point& p : pts) {
        if (first) {
            x0 = x1 = p.x;
            y0 = y1 = p.y;
            first = false;
        }
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    }
    return std::hypot(x1 - x0, y1 - y0) * kFlatness;
}

} // namespace

Path::Path(std::initializer_list<PathSegment> segments) : segments_(segments) {}

Path::Path(std::vector<PathSegment> segments) : segments_(std::move(segments)) {}

Path& Path::moveTo(Point p)
{
    segments_.push_back(PathSegment{PathVerb::Move, p, {}, {}});
    return *this;
}

Path& Path::lineTo(Point p)
{
    segments_.push_back(PathSegment{PathVerb::Line, p, {}, {}});
    return *this;
}

Path& Path::quadTo(Point control, Point end)
{
    segments_.push_back(PathSegment{PathVerb::Quad, control, end, {}});
    return *this;
}

Path& Path::cubicTo(Point c1, Point c2, Point end)
{
    segments_.push_back(PathSegment{PathVerb::Cubic, c1, c2, end});
    return *this;
}

Path& Path::close()
{
    segments_.push_back(PathSegment{PathVerb::Close, {}, {}, {}});
    return *this;
}

std::vector<Path::Contour> Path::flatten() const
{
    std::vector<Contour> out;
    Point                pen{};   // (0, 0) före första Move, som i SVG
    const auto current = [&]() -> Contour& {
        if (out.empty() || out.back().closed)
            out.push_back(Contour{{pen}, false});
        return out.back();
    };
    for (const PathSegment& s : segments_) {
        switch (s.verb) {
        case PathVerb::Move:
            pen = s.p0;
            out.push_back(Contour{{pen}, false});
            break;
        case PathVerb::Line:
            current().points.push_back(s.p0);
            pen = s.p0;
            break;
        case PathVerb::Quad: {
            // En andragradskurva är en tredjegrads med kontrollpunkterna två
            // tredjedelar mot kontrollpunkten.
            Contour&    c  = current();
            const Point c1 = Point{pen.x + 2.0 / 3.0 * (s.p0.x - pen.x),
                                   pen.y + 2.0 / 3.0 * (s.p0.y - pen.y)};
            const Point c2 = Point{s.p1.x + 2.0 / 3.0 * (s.p0.x - s.p1.x),
                                   s.p1.y + 2.0 / 3.0 * (s.p0.y - s.p1.y)};
            cubic(pen, c1, c2, s.p1, tolerance({pen, s.p0, s.p1}), 0, c.points);
            pen = s.p1;
            break;
        }
        case PathVerb::Cubic: {
            Contour& c = current();
            cubic(pen, s.p0, s.p1, s.p2, tolerance({pen, s.p0, s.p1, s.p2}), 0,
                  c.points);
            pen = s.p2;
            break;
        }
        case PathVerb::Close:
            if (!out.empty() && !out.back().closed) {
                out.back().closed = true;
                pen = out.back().points.front();   // SVG: pennan till starten
            }
            break;
        }
    }
    // En delväg med en enda punkt ritar ingenting.
    out.erase(std::remove_if(out.begin(), out.end(),
                             [](const Contour& c) { return c.points.size() < 2; }),
              out.end());
    return out;
}

} // namespace glaze
