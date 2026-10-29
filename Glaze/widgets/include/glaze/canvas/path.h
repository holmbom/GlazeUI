#pragma once
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "glaze/canvas/geometry.h"

// glaze::Path — EN VÄG AV RAKA OCH KRÖKTA SEGMENT. Formen
// bakom `Shape::path`: delvägar som börjar med Move, går med Line/Quad/Cubic
// och kan slutas med Close.
//
// TVÅ LIKVÄRDIGA FORMER (hård regel 8) som ger EXAKT samma tillstånd —
// segmentlistan:
//
//     glaze::Path p;                                     // koden: kedjan
//     p.moveTo({0, 0}).lineTo({40, 0}).cubicTo({60, 0}, {60, 30}, {40, 30})
//      .close();
//
//     glaze::Path{{glaze::PathVerb::Move, {0, 0}},       // .glazeui: listan
//                 {glaze::PathVerb::Line, {40, 0}},
//                 {glaze::PathVerb::Cubic, {60, 0}, {60, 30}, {40, 30}},
//                 {glaze::PathVerb::Close}}
//
// Kedjan finns för handkoden; listan är den form grammatiken kan bära (ett
// kedjat returvärde går inte att emittera, regel 8).
//
// KURVORNA PLATTAS EN GÅNG, när formen byggs — till raka segment med en
// tolerans RELATIV kurvans egen storlek (zoomen är inte känd när formen
// läggs till, och geometrin laddas upp en gång). Följden, skriven: en kurva
// som förstoras långt bortom sin storlek på skärmen visar till slut sina
// hörn.
//
// Som yta (`Shape::path(p, fyllning)`) är den FÖRSTA delvägen ytterringen
// och resten HÅL (samma regel som `Shape::polygon` med ringar); som kontur
// ritas varje delväg för sig, sluten om den slutade med Close.

namespace glaze {

enum class PathVerb : std::uint8_t { Move, Line, Quad, Cubic, Close };

// Ett segment. Punkterna står i SVG:s ordning:
//   Move, Line   p0 = punkten
//   Quad         p0 = kontrollpunkten, p1 = slutet
//   Cubic        p0, p1 = kontrollpunkterna, p2 = slutet
//   Close        inga punkter
struct PathSegment {
    PathVerb verb = PathVerb::Move;
    Point    p0{};
    Point    p1{};
    Point    p2{};
};

class Path {
public:
    Path() = default;
    Path(std::initializer_list<PathSegment> segments);
    explicit Path(std::vector<PathSegment> segments);

    // Kedjan — varje anrop lägger till ett segment. Ett Line/Quad/Cubic utan
    // föregående Move börjar i (0, 0), som i SVG.
    Path& moveTo(Point p);
    Path& lineTo(Point p);
    Path& quadTo(Point control, Point end);
    Path& cubicTo(Point c1, Point c2, Point end);
    Path& close();

    const std::vector<PathSegment>& segments() const { return segments_; }
    bool                            empty() const { return segments_.empty(); }

    // EN DELVÄG, plattad till punkter. `closed` = slutad med Close.
    struct Contour {
        std::vector<Point> points;
        bool               closed = false;
    };

    // Delvägarna plattade (src/canvas/path.cpp). Räknas om ur segmenten vid
    // varje anrop — formen anropar den EN gång när den byggs.
    std::vector<Contour> flatten() const;

private:
    std::vector<PathSegment> segments_;
};

} // namespace glaze
