// glaze::Shape, Paint och ImageSource — formernas fabriker. Formen är ett VÄRDE:
// fabrikerna lägger punkterna och stilen i den, och lagret (scene.cpp) eller
// målaren (painter.cpp) bygger geometrin ur den. Kontraktet står i
// glaze/canvas/shape.h.

#include "glaze/canvas/shape.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <utility>

#include <gfx/geom3d.h>
#include <gfx/platform.h>

#include "image_data.h"   // ImageSource: bildens version (snapshotet)
#include "instances.h"
#include "mesh_data.h"
#include "transform3d.h"

namespace glaze {

namespace {

constexpr double kPi = 3.14159265358979323846;

// En hel cirkel eller ellips plattas till så många punkter. Formen byggs EN
// gång och zoomen är okänd då, så talet är valt för att stå sig långt in:
// felet är r · (1 − cos(π/128)) ≈ 0,0003 · r, under en tredjedels px för en
// cirkel med radien 1000 px på skärmen. En båge får sin andel.
constexpr int kCircleSegments = 128;

// En båge som punkter: vinklarna i grader, moturs med y uppåt (matematikens
// riktning) — i en värld med y nedåt alltså medurs på skärmen.
std::vector<Point> arcPoints(Point c, double rx, double ry, double startDeg,
                             double sweepDeg, bool fullTurn)
{
    std::vector<Point> p;
    if (!(rx > 0.0) || !(ry > 0.0) || !std::isfinite(startDeg) ||
        !std::isfinite(sweepDeg) || sweepDeg == 0.0)
        return p;
    const double sweep = std::clamp(sweepDeg, -360.0, 360.0);
    const int    n     = std::max(
        2, static_cast<int>(std::ceil(std::abs(sweep) / 360.0 * kCircleSegments)));
    // En hel varv-ring upprepar inte startpunkten — ringen sluts av ytan.
    const int count = fullTurn ? n : n + 1;
    p.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const double t = (startDeg + sweep * i / n) * (kPi / 180.0);
        p.push_back(Point{c.x + rx * std::cos(t), c.y + ry * std::sin(t)});
    }
    return p;
}

} // namespace

// ------------------------------------------------------------------ Paint

Paint Paint::linear(Point from, Point to, std::vector<GradientStop> stops)
{
    Paint p;
    p.kind_  = gfx::PaintKind::Linear;
    p.color_ = gfx::Color{255, 255, 255, 255};   // rampen bär färgen
    p.a_     = from;
    p.b_     = to;
    p.stops_ = std::move(stops);
    return p;
}

Paint Paint::radial(Point center, double r0, double r1,
                    std::vector<GradientStop> stops)
{
    Paint p;
    p.kind_  = gfx::PaintKind::Radial;
    p.color_ = gfx::Color{255, 255, 255, 255};
    p.a_     = center;
    p.r0_    = r0;
    p.r1_    = r1;
    p.stops_ = std::move(stops);
    return p;
}

bool Paint::visible() const
{
    if (!isGradient())
        return color_.a > 0;
    for (const GradientStop& s : stops_)
        if (s.color.a > 0)
            return true;
    return false;
}

bool Paint::operator==(const Paint& o) const
{
    if (kind_ != o.kind_)
        return false;
    const auto same = [](gfx::Color a, gfx::Color b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };
    if (!isGradient())
        return same(color_, o.color_);
    if (a_.x != o.a_.x || a_.y != o.a_.y || b_.x != o.b_.x ||
        b_.y != o.b_.y || r0_ != o.r0_ || r1_ != o.r1_ ||
        stops_.size() != o.stops_.size())
        return false;
    for (std::size_t i = 0; i < stops_.size(); ++i)
        if (stops_[i].offset != o.stops_[i].offset ||
            !same(stops_[i].color, o.stops_[i].color))
            return false;
    return true;
}

// ------------------------------------------------------------ ImageSource

// Versionen är ett SNAPSHOT (image_source.h): den bild värdet skapades ur, i
// det skick den hade då. Ritningen läser alltid de levande pixlarna.
ImageSource::ImageSource(AssetHandle h) : kind_(Kind::Asset), asset_(h) {}
ImageSource::ImageSource(Bitmap b)
    : kind_(Kind::Bitmap),
      bitmap_(std::move(b)),
      version_(bitmap_.d_ ? bitmap_.d_->version : 0)
{
}
ImageSource::ImageSource(RenderTexture t)
    : kind_(Kind::Texture),
      texture_(std::move(t)),
      version_(texture_.d_ ? texture_.d_->version : 0)
{
}
// En standardikon har ingen version: vektorn ändras aldrig, och färgen och
// tätheten är ritningens (glaze/stock_icon.h).
ImageSource::ImageSource(StockIcon icon, int sizeClass, StockIconStyle style)
    : kind_(Kind::Stock), stock_(icon), stockSize_(sizeClass), stockStyle_(style)
{
}

float ImageSource::sizeClass() const
{
    switch (kind_) {
    case Kind::Asset: return assetSizeClass(asset_);
    case Kind::Stock: return static_cast<float>(stockSize_);
    case Kind::Bitmap:
    case Kind::Texture:
    case Kind::None:  break;
    }
    return 0.0f;
}

bool operator==(const ImageSource& a, const ImageSource& b)
{
    // Två OGILTIGA är alltid lika — `{}` och `AssetHandle{}` säger båda
    // "ingen bild" och får inte se ut som en ändring för en settervakt.
    if (!a.valid() || !b.valid())
        return a.valid() == b.valid();
    return a.sameImage(b) && a.version_ == b.version_;
}

bool ImageSource::valid() const
{
    switch (kind_) {
    case Kind::Asset:   return asset_.valid();
    case Kind::Bitmap:  return bitmap_.valid();
    case Kind::Texture: return texture_.valid();
    case Kind::Stock:   return true;
    case Kind::None:    break;
    }
    return false;
}

bool ImageSource::sameImage(const ImageSource& o) const
{
    if (kind_ != o.kind_)
        return false;
    switch (kind_) {
    case Kind::Asset:   return asset_.id == o.asset_.id;
    case Kind::Bitmap:  return bitmap_.d_ == o.bitmap_.d_;
    case Kind::Texture: return texture_.d_ == o.texture_.d_;
    case Kind::Stock:
        return stock_ == o.stock_ && stockSize_ == o.stockSize_ &&
               stockStyle_ == o.stockStyle_;
    case Kind::None:    return true;
    }
    return false;
}

// ------------------------------------------------------------------ Shape

Shape Shape::line(Point a, Point b, Stroke stroke)
{
    return polyline({a, b}, std::move(stroke));
}

Shape Shape::polyline(std::vector<Point> points, Stroke stroke)
{
    Shape s;
    s.kind_      = Kind::Lines;
    s.contours_  = {Contour{std::move(points), false}};
    s.stroke_    = std::move(stroke);
    s.hasStroke_ = true;
    return s;
}

Shape Shape::arc(Point center, double r, double startDeg, double sweepDeg,
                 Stroke stroke)
{
    return polyline(arcPoints(center, r, r, startDeg, sweepDeg, false),
                    std::move(stroke));
}

Shape Shape::area(std::vector<Contour> rings, Paint fill,
                  std::optional<Stroke> stroke)
{
    Shape s;
    s.kind_     = Kind::Area;
    s.contours_ = std::move(rings);
    s.fill_     = std::move(fill);
    s.hasFill_  = true;
    if (stroke) {
        s.stroke_    = std::move(*stroke);
        s.hasStroke_ = true;
    }
    return s;
}

Shape Shape::polygon(std::vector<Point> outer, Paint fill,
                     std::optional<Stroke> stroke)
{
    return area({Contour{std::move(outer), true}}, std::move(fill),
                std::move(stroke));
}

Shape Shape::polygon(std::vector<std::vector<Point>> rings, Paint fill,
                     std::optional<Stroke> stroke)
{
    std::vector<Contour> c;
    c.reserve(rings.size());
    for (std::vector<Point>& r : rings)
        c.push_back(Contour{std::move(r), true});
    return area(std::move(c), std::move(fill), std::move(stroke));
}

Shape Shape::rect(Rect r, Paint fill, std::optional<Stroke> stroke)
{
    return polygon({{r.x, r.y}, {r.x + r.w, r.y}, {r.x + r.w, r.y + r.h},
                    {r.x, r.y + r.h}},
                   std::move(fill), std::move(stroke));
}

Shape Shape::ellipse(Point center, double rx, double ry, Paint fill,
                     std::optional<Stroke> stroke)
{
    return polygon(arcPoints(center, rx, ry, 0.0, 360.0, true),
                   std::move(fill), std::move(stroke));
}

Shape Shape::circle(Point center, double r, Paint fill,
                    std::optional<Stroke> stroke)
{
    return ellipse(center, r, r, std::move(fill), std::move(stroke));
}

Shape Shape::path(const Path& p, Paint fill, std::optional<Stroke> stroke)
{
    std::vector<Contour> rings = p.flatten();
    for (Contour& c : rings)
        c.closed = true;   // som yta är varje delväg en ring
    return area(std::move(rings), std::move(fill), std::move(stroke));
}

Shape Shape::path(const Path& p, Stroke stroke)
{
    Shape s;
    s.kind_      = Kind::Lines;
    s.contours_  = p.flatten();
    s.stroke_    = std::move(stroke);
    s.hasStroke_ = true;
    return s;
}

Shape Shape::text(Point at, TrText text, TextStyle style)
{
    Shape s;
    s.kind_      = Kind::Text;
    s.points_    = {at};
    s.text_      = std::move(text);
    s.textStyle_ = std::move(style);
    return s;
}

Shape Shape::text(Point at, std::vector<TextSpan> spans, TextStyle style)
{
    Shape s;
    s.kind_      = Kind::Text;
    s.points_    = {at};
    s.spans_     = std::move(spans);
    s.textStyle_ = std::move(style);
    return s;
}

Shape Shape::marker(Point at, MarkerStyle style)
{
    return points({at}, std::move(style));
}

Shape Shape::points(std::vector<Point> points, MarkerStyle style)
{
    Shape s;
    s.kind_   = Kind::Markers;
    s.points_ = std::move(points);
    s.marker_ = std::move(style);
    return s;
}

namespace {

// Bildens stil som formen lagrar den (shape.h `ImageStyle`): opaciteten 0–1
// och färgjusteringen genom fasadens regel (gfx/scene.h `adjustFactor`), ett
// tal som inte är ändligt förvalet — så två lika ritade bilder har samma
// körningsnyckel.
ImageStyle normalized(const ImageStyle& st)
{
    ImageStyle n;
    n.opacity    = std::isfinite(st.opacity) ? std::clamp(st.opacity, 0.0f, 1.0f)
                                             : 1.0f;
    n.saturation = gfx::adjustFactor(st.saturation);
    n.contrast   = gfx::adjustFactor(st.contrast);
    n.brightness = gfx::adjustFactor(st.brightness);
    return n;
}

} // namespace

Shape Shape::image(Rect r, ImageSource source, ImageStyle style)
{
    Shape s;
    s.kind_       = Kind::Image;
    s.rect_       = r;
    s.image_      = std::move(source);
    s.imageStyle_ = normalized(style);
    return s;
}

namespace {

// En ogiltig fyrhörning loggas EN gång per process — en form i `onPaint`
// hade annars loggat varje ritning. Fabrikerna får köras i vilken tråd som
// helst, därav atomic.
std::atomic<bool> g_quadLogged{false};

} // namespace

Shape Shape::image(Point topLeft, Point topRight, Point bottomRight,
                   Point bottomLeft, ImageSource source, ImageStyle style)
{
    Shape s;
    s.kind_       = Kind::Image;
    s.points_     = {topLeft, topRight, bottomRight, bottomLeft};
    s.image_      = std::move(source);
    s.imageStyle_ = normalized(style);
    // Rektangeln är LÅDAN kring hörnen — lagrens gallring och träffprovets
    // rutnät läser den, och en fyrhörning ryms alltid i den.
    double x0 = topLeft.x, y0 = topLeft.y, x1 = topLeft.x, y1 = topLeft.y;
    for (const Point& p : s.points_) {
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    }
    s.rect_ = Rect{x0, y0, x1 - x0, y1 - y0};
    // Bara strikt konvexa fyrhörningar ritas (shape.h); vikterna själva
    // räknar byggaren ur de hörn den faktiskt får (målarens transform).
    const gfx::Point2d ring[4] = {{topLeft.x, topLeft.y},
                                  {topRight.x, topRight.y},
                                  {bottomRight.x, bottomRight.y},
                                  {bottomLeft.x, bottomLeft.y}};
    float q[4];
    if (!gfx::quadWeights(ring, q) && !g_quadLogged.exchange(true))
        gfx::log("Shape::image: fyrhörningen är inte strikt konvex (tre hörn "
                 "i linje, en fluga, en inbuktning eller ett hörn som inte är "
                 "ändligt) — bilden ritas inte; loggas en gång");
    return s;
}

// ------------------------------------------------------- i rymden

Shape Shape::line3(Point3 a, Point3 b, Stroke stroke)
{
    return polyline3({a, b}, std::move(stroke));
}

Shape Shape::polyline3(std::vector<Point3> points, Stroke stroke)
{
    Shape s;
    s.kind_      = Kind::Lines3;
    s.d3_        = std::make_shared<const Shape3DData>(
        Shape3DData{std::move(points)});
    s.stroke_    = std::move(stroke);
    s.hasStroke_ = true;
    return s;
}

Shape Shape::text3(Point3 at, TrText text, TextStyle style)
{
    Shape s;
    s.kind_      = Kind::Text3;
    s.d3_        = std::make_shared<const Shape3DData>(Shape3DData{{at}});
    s.text_      = std::move(text);
    s.textStyle_ = std::move(style);
    return s;
}

Shape Shape::text3(Point3 at, std::vector<TextSpan> spans, TextStyle style)
{
    Shape s;
    s.kind_      = Kind::Text3;
    s.d3_        = std::make_shared<const Shape3DData>(Shape3DData{{at}});
    s.spans_     = std::move(spans);
    s.textStyle_ = std::move(style);
    return s;
}

Shape Shape::marker3(Point3 at, MarkerStyle style)
{
    Shape s;
    s.kind_   = Kind::Markers3;
    s.d3_     = std::make_shared<const Shape3DData>(Shape3DData{{at}});
    s.marker_ = std::move(style);
    return s;
}

const std::vector<Point3>& Shape::points3() const
{
    static const std::vector<Point3> ingen;
    return d3_ ? d3_->points : ingen;
}

// ------------------------------------------- föremålen i rymden

namespace {

// Primitivernas sidantal klampas (shape.h): cylinderns enhetsmeshar cachas per
// sidantal, och cachen ska inte kunna växa utan gräns.
constexpr int kMinSides = 3;
constexpr int kMaxSides = 256;

const gfx::Color kVit{255, 255, 255, 255};
const gfx::Color kSvart{0, 0, 0, 255};

bool finite3(const Point3& p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

// Lådans mitt kring lägena — origot en genererad mesh får (RTC).
Point3 centerOf(const std::vector<gfx::Vec3d>& pos)
{
    if (pos.empty())
        return Point3{};
    gfx::Vec3d lo = pos.front(), hi = pos.front();
    for (const gfx::Vec3d& p : pos) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    return Point3{(lo.x + hi.x) / 2.0, (lo.y + hi.y) / 2.0, (lo.z + hi.z) / 2.0};
}

// En genererad mesh (gfx/geom3d.h — lägena i världen) som `Mesh` med origot
// `origin` och normalerna, och valfria skalärer. Tom geometri ger ingen mesh.
Mesh meshOf(const gfx::MeshGeometry& g, const Point3& origin,
            std::vector<float> scalars = {})
{
    if (g.positions.empty() || g.indices.empty())
        return Mesh{};
    MeshDesc d;
    d.origin = origin;
    d.positions.reserve(g.positions.size());
    for (const gfx::Vec3d& p : g.positions)
        d.positions.push_back(Float3{static_cast<float>(p.x - origin.x),
                                     static_cast<float>(p.y - origin.y),
                                     static_cast<float>(p.z - origin.z)});
    d.normals.reserve(g.normals.size());
    for (const gfx::Vec3d& n : g.normals)
        d.normals.push_back(Float3{static_cast<float>(n.x), static_cast<float>(n.y),
                                   static_cast<float>(n.z)});
    d.indices = g.indices;
    d.scalars = std::move(scalars);
    return Mesh(std::move(d));
}

// ENHETSMESHARNA — en per process, delade av alla former (shape.h): lådan
// [−½, ½]³, sfären med diametern 1 kring origo (24 × 16) och cylindern
// med radien 1 från origo till z = 1 — en per sidantal. Placeringen skalar och
// flyttar dem. Fabrikerna får köras i vilken tråd som helst: de statiska
// initieras en gång (C++11), cylinderns cache bakom ett lås.
const Mesh& unitBox()
{
    static const Mesh m = [] {
        gfx::MeshGeometry g;
        gfx::box(gfx::Vec3d{-0.5, -0.5, -0.5}, gfx::Vec3d{0.5, 0.5, 0.5}, g);
        return meshOf(g, Point3{});
    }();
    return m;
}

const Mesh& unitSphere()
{
    static const Mesh m = [] {
        gfx::MeshGeometry g;
        gfx::sphere(gfx::Vec3d{0.0, 0.0, 0.0}, 0.5, g);
        return meshOf(g, Point3{});
    }();
    return m;
}

Mesh unitCylinder(int sides)
{
    static std::mutex          lås;
    static std::map<int, Mesh> cache;
    const std::lock_guard<std::mutex> l(lås);
    const auto it = cache.find(sides);
    if (it != cache.end())
        return it->second;
    gfx::MeshGeometry g;
    gfx::cylinder(gfx::Vec3d{0.0, 0.0, 0.0}, gfx::Vec3d{0.0, 0.0, 1.0}, 1.0, g,
                  sides);
    Mesh m = meshOf(g, Point3{});
    cache.emplace(sides, m);
    return m;
}

std::shared_ptr<const Shape3DData> data3(Shape3DData d)
{
    return std::make_shared<const Shape3DData>(std::move(d));
}

// En radiell gradient på en yta i rymden loggas EN gång per process.
std::atomic<bool> g_radialLogged{false};

} // namespace

Shape Shape::polygon3(std::vector<Point3> outer, Paint fill,
                      std::optional<Stroke> stroke)
{
    Shape       s;
    Shape3DData d;
    s.kind_ = Kind::Area3;
    if (fill.visible()) {
        // Ringen i sitt eget plan; obelyst i fyllningens färg, som 2D-ytan.
        std::vector<gfx::Vec3d> ring;
        ring.reserve(outer.size());
        for (const Point3& p : outer)
            ring.push_back(gfx::Vec3d{p.x, p.y, p.z});
        const std::vector<std::vector<gfx::Vec3d>> rings{std::move(ring)};
        gfx::MeshGeometry                          g;
        gfx::triangulatePlanar(rings, g);
        d.meshStyle.lit = false;
        std::vector<float> t;
        if (fill.kind() == gfx::PaintKind::Linear) {
            // t längs gradienten i världens xy — affint i läget, så det
            // interpoleras exakt mellan hörnen (rampen bär färgerna).
            const double dx = fill.to().x - fill.from().x;
            const double dy = fill.to().y - fill.from().y;
            const double l2 = dx * dx + dy * dy;
            t.reserve(g.positions.size());
            for (const gfx::Vec3d& p : g.positions)
                t.push_back(l2 > 0.0 ? static_cast<float>(
                                           ((p.x - fill.from().x) * dx +
                                            (p.y - fill.from().y) * dy) /
                                           l2)
                                     : 0.0f);
            d.meshStyle.ramp  = ColorRamp{fill.stops(), 0.0, 1.0};
            d.meshStyle.color = kVit;
        } else if (fill.kind() == gfx::PaintKind::Radial) {
            if (!g_radialLogged.exchange(true))
                gfx::log("Shape::polygon3: en radiell gradient ritas inte i "
                         "rymden — ytan får gradientens första färg; loggas en "
                         "gång");
            d.meshStyle.color =
                fill.stops().empty() ? kSvart : fill.stops().front().color;
        } else {
            d.meshStyle.color = fill.color();
        }
        d.mesh = meshOf(g, centerOf(g.positions), std::move(t));
    }
    s.fill_    = std::move(fill);
    s.hasFill_ = true;
    if (stroke) {
        s.stroke_    = std::move(*stroke);
        s.hasStroke_ = true;
    }
    d.points = std::move(outer);
    s.d3_    = data3(std::move(d));
    return s;
}

Shape Shape::mesh(Mesh m, Placement placement, MeshStyle style)
{
    Shape       s;
    Shape3DData d;
    s.kind_     = Kind::Mesh;
    d.mesh      = std::move(m);
    d.placement = placement;
    d.meshStyle = std::move(style);
    s.d3_       = data3(std::move(d));
    return s;
}

Shape Shape::box(Box3 b, MeshStyle style)
{
    if (!finite3(b.min) || !finite3(b.max))
        return mesh(Mesh{}, {}, std::move(style));
    Placement p;
    p.at    = Point3{(b.min.x + b.max.x) / 2.0, (b.min.y + b.max.y) / 2.0,
                     (b.min.z + b.max.z) / 2.0};
    p.scale = Float3{static_cast<float>(std::abs(b.max.x - b.min.x)),
                     static_cast<float>(std::abs(b.max.y - b.min.y)),
                     static_cast<float>(std::abs(b.max.z - b.min.z))};
    return mesh(unitBox(), p, std::move(style));
}

Shape Shape::sphere(Point3 center, Length radius, MeshStyle style)
{
    if (!finite3(center) || !std::isfinite(radius.value) || !(radius.value > 0.0f))
        return mesh(Mesh{}, {}, std::move(style));
    Placement p;
    p.at = center;
    if (radius.unit == Unit::World) {
        const float diameter = 2.0f * radius.value;
        p.scale              = Float3{diameter, diameter, diameter};
    } else {
        // Radien på skärmen: enhetssfärens diameter 1 blir diametern där.
        style.size = Length{2.0f * radius.value, radius.unit};
    }
    return mesh(unitSphere(), p, std::move(style));
}

Shape Shape::cylinder(Point3 base, double radius, double height,
                      MeshStyle style, int sides)
{
    if (!finite3(base) || !std::isfinite(radius) || !(radius > 0.0) ||
        !std::isfinite(height) || height == 0.0)
        return mesh(Mesh{}, {}, std::move(style));
    // En negativ höjd står nedåt från foten — samma cylinder från toppen, så
    // skalan aldrig speglar (Cull::Back hade gallrat utsidan).
    if (height < 0.0) {
        base.z += height;
        height = -height;
    }
    Placement p;
    p.at    = base;
    p.scale = Float3{static_cast<float>(radius), static_cast<float>(radius),
                     static_cast<float>(height)};
    return mesh(unitCylinder(std::clamp(sides, kMinSides, kMaxSides)), p,
                std::move(style));
}

Shape Shape::tube(std::vector<Point3> path, double diameter, MeshStyle style,
                  int sides)
{
    std::vector<gfx::Vec3d> p;
    p.reserve(path.size());
    for (const Point3& q : path)
        p.push_back(gfx::Vec3d{q.x, q.y, q.z});
    gfx::MeshGeometry g;
    gfx::tube(p, diameter, g, std::clamp(sides, kMinSides, kMaxSides));
    return mesh(meshOf(g, centerOf(g.positions)), {}, std::move(style));
}

Shape Shape::instances(Shape prototype, std::vector<Instance> instances,
                       MeshStyle style)
{
    if (prototype.kind() != Kind::Mesh) {
        gfx::log("Shape::instances: prototypen är ingen mesh (mesh, låda, sfär "
                 "eller cylinder) — formen blir tom");
        return Shape{};
    }
    const Shape3DData& pd = *prototype.d3_;
    Shape              s;
    Shape3DData        d;
    s.kind_     = Kind::Instances;
    d.mesh      = pd.mesh;
    d.placement = pd.placement;
    d.meshStyle = std::move(style);
    if (!(d.meshStyle.size.value > 0.0f) && pd.meshStyle.size.value > 0.0f)
        d.meshStyle.size = pd.meshStyle.size;   // en skärmkonstant prototyp
    // Prototypens låda i instansens ram: meshens lokala låda genom
    // prototypens placering, med origot som förskjutning (instances.h).
    Box3 lokal{};
    if (pd.mesh.valid()) {
        const Box3            b = pd.mesh.bounds();
        const Point3          o = detail::MeshAccess::data(pd.mesh)->origin;
        const Placement&      q = pd.placement;
        const detail::Affine3 modell = detail::rotationScale(
            q.yaw, q.pitch, q.roll, q.scale, o.x + q.at.x, o.y + q.at.y,
            o.z + q.at.z);
        lokal = detail::transformBox(
            modell, Box3{{b.min.x - o.x, b.min.y - o.y, b.min.z - o.z},
                         {b.max.x - o.x, b.max.y - o.y, b.max.z - o.z}});
    }
    d.instanceSet = detail::buildInstanceSet(lokal, instances);
    d.instances   = std::move(instances);
    s.d3_         = data3(std::move(d));
    return s;
}

Shape Shape::points3(std::vector<Point3> points, PointStyle style)
{
    return pointCloud(PointCloud::fromWorld(points), std::move(style));
}

Shape Shape::pointCloud(PointCloud cloud, PointStyle style)
{
    Shape       s;
    Shape3DData d;
    s.kind_      = Kind::Points3;
    d.cloud      = std::move(cloud);
    d.pointStyle = std::move(style);
    s.d3_        = data3(std::move(d));
    return s;
}

// ------------------------------------------------- helskärmspasset

Shape Shape::screenPass(std::string program, PassStyle style)
{
    // Ett tomt eller okänt namn är ingen felaktig form: passet ritar sin
    // reservfärg (ritningen loggar det, src/canvas/scene3d.cpp).
    Shape       s;
    Shape3DData d;
    s.kind_       = Kind::Pass;
    d.passProgram = std::move(program);
    d.passStyle   = std::move(style);
    s.d3_         = data3(std::move(d));
    return s;
}

namespace {

// Läsvägens förval för en form utan föremålsdata.
const Shape3DData& tom3()
{
    static const Shape3DData ingen;
    return ingen;
}

} // namespace

const Mesh& Shape::mesh() const
{
    return (d3_ ? *d3_ : tom3()).mesh;
}

const Placement& Shape::placement() const
{
    return (d3_ ? *d3_ : tom3()).placement;
}

const MeshStyle& Shape::meshStyle() const
{
    return (d3_ ? *d3_ : tom3()).meshStyle;
}

const std::vector<Instance>& Shape::instances() const
{
    return (d3_ ? *d3_ : tom3()).instances;
}

const PointCloud& Shape::pointCloud() const
{
    return (d3_ ? *d3_ : tom3()).cloud;
}

const PointStyle& Shape::pointStyle() const
{
    return (d3_ ? *d3_ : tom3()).pointStyle;
}

const std::string& Shape::passProgram() const
{
    return (d3_ ? *d3_ : tom3()).passProgram;
}

const PassStyle& Shape::passStyle() const
{
    return (d3_ ? *d3_ : tom3()).passStyle;
}

const std::shared_ptr<const detail::InstanceSet>& Shape::instanceSet() const
{
    return (d3_ ? *d3_ : tom3()).instanceSet;
}

} // namespace glaze
