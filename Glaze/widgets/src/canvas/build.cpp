// En form till byggarnas geometri — delad av lagren och målaren. Kontraktet
// står i build.h; byggarna själva i gfx/geom2d.h och
// gfx/sdf.h.

#include "build.h"

#include <algorithm>
#include <atomic>
#include <cmath>

#include <gfx/platform.h>   // gfx::log

#include "instances.h"
#include "mesh_data.h"
#include "transform3d.h"

namespace glaze::detail {

namespace {

gfx::Point2d toGfx(const Point& p) { return gfx::Point2d{p.x, p.y}; }

const gfx::Color kWhite{255, 255, 255, 255};

gfx::TextAlign textAlign(Align a)
{
    switch (a) {
    case Align::Left:   return gfx::TextAlign::Left;
    case Align::Right:  return gfx::TextAlign::Right;
    case Align::Center: break;
    }
    return gfx::TextAlign::Center;
}

} // namespace

gfx::MeshProgram programOf(PartKind k)
{
    switch (k) {
    case PartKind::Image:  return gfx::MeshProgram::Image;
    case PartKind::Fill:   return gfx::MeshProgram::Fill;
    case PartKind::Glyph:  return gfx::MeshProgram::Glyph;
    case PartKind::Line3:  return gfx::MeshProgram::Line3;
    case PartKind::Glyph3: return gfx::MeshProgram::Glyph3;
    // Föremålen ritas med de inbyggda Mesh- och Points-programmen genom
    // fasadens egen väg (programs3d.h), passen med ritytans egna.
    case PartKind::Mesh:
    case PartKind::Points:
    case PartKind::Pass:   return gfx::MeshProgram::Custom;
    case PartKind::Line:   break;
    }
    return gfx::MeshProgram::Line;
}

bool RunKey::operator==(const RunKey& o) const
{
    if (kind != o.kind || unit != o.unit || gradient != o.gradient ||
        font != o.font)
        return false;
    if (gradient && !(paint == o.paint))
        return false;
    if (kind == PartKind::Image &&
        (!image.sameImage(o.image) || opacity != o.opacity ||
         saturation != o.saturation || contrast != o.contrast ||
         brightness != o.brightness))
        return false;
    if (isGlyphKind(kind) && (!image.sameImage(o.image) || ground != o.ground ||
                              minSize.value != o.minSize.value ||
                              minSize.unit != o.minSize.unit ||
                              maxSize.value != o.maxSize.value ||
                              maxSize.unit != o.maxSize.unit))
        return false;
    if (isLineKind(kind)) {
        if (miterLimit != o.miterLimit || dash.count != o.dash.count ||
            (dash.count > 0 &&
             (dashUnit != o.dashUnit || dash.offset != o.dash.offset)))
            return false;
        for (int i = 0; i < dash.count; ++i)
            if (dash.ends[i] != o.dash.ends[i])
                return false;
    }
    return true;
}

namespace {

// Ett streckmönster som inte ryms loggas EN gång per process — en form i
// `onPaint` hade annars loggat varje ritning. Byggarna kan köras i en
// arbetartråd, därav atomic.
std::atomic<bool> g_dashLogged{false};

// Konturens uniformdel av nyckeln: spetsgränsen bara för spetsiga skarvar
// (andra delar förvalet och därmed körning; en gräns under 1 eller som inte
// är ändlig är ogiltig i SVG och ger förvalet), och strecken normaliserade.
void strokeKey(const Stroke& st, RunKey& k)
{
    const float lim = st.miterLimit;
    k.miterLimit    = st.join == LineJoin::Miter && std::isfinite(lim) &&
                           lim >= 1.0f
                          ? lim
                          : Stroke{}.miterLimit;
    if (!gfx::makeDash(st.dash, st.dashOffset, k.dash) &&
        !g_dashLogged.exchange(true))
        gfx::log("Stroke: streckmönstret har fler än %d längder (ett udda "
                 "antal räknas dubbelt) — linjen ritas heldragen; loggas en "
                 "gång",
                 gfx::kMaxDashes);
    k.dashUnit = k.dash.count > 0 ? st.dashUnit : Unit::Px;
}

// En gräns i nyckeln: ingen gräns normaliseras till förvalet, så
// former utan gräns delar körning oavsett hur "ingen" skrevs.
Length limitKey(Length l)
{
    return l.value > 0.0f && std::isfinite(l.value) ? l : Length{};
}

} // namespace

std::vector<RunKey> partKeys(const Shape& s)
{
    std::vector<RunKey> keys;
    const auto lineKey = [&s](PartKind kind) {
        RunKey k;
        k.kind     = kind;
        k.unit     = s.stroke().width.unit;
        k.gradient = s.stroke().paint.isGradient();
        if (k.gradient)
            k.paint = s.stroke().paint;
        strokeKey(s.stroke(), k);
        return k;
    };
    const auto textKey = [&s](PartKind kind) {
        RunKey k;
        k.kind   = kind;
        k.unit   = s.textStyle().size.unit;
        k.font    = fontKey(s.textStyle());
        k.ground  = s.textStyle().rotation == Alignment::Ground;
        k.minSize = limitKey(s.textStyle().minSize);
        k.maxSize = limitKey(s.textStyle().maxSize);
        return k;
    };
    // En markörkörning — eller ingen, för en ogiltig bildmarkör (den ritar
    // ingenting, som `Shape::image`). En BILDMARKÖR ritas med sin bild bunden
    // — en körning per bild.
    const auto markerKey = [&s](PartKind kind, RunKey& k) {
        k.kind   = kind;
        k.unit   = s.markerStyle().size.unit;
        k.font    = 0;
        k.ground  = s.markerStyle().rotation == Alignment::Ground;
        k.minSize = limitKey(s.markerStyle().minSize);
        k.maxSize = limitKey(s.markerStyle().maxSize);
        if (s.markerStyle().img.kind() != ImageSource::Kind::None) {
            if (!s.markerStyle().img.valid())
                return false;
            k.image = s.markerStyle().img;
        }
        return true;
    };
    const bool stroked = s.hasStroke() && s.stroke().paint.visible() &&
                         s.stroke().width.value > 0.0f;
    switch (s.kind()) {
    case Shape::Kind::Lines:
        if (stroked)
            keys.push_back(lineKey(PartKind::Line));
        break;
    case Shape::Kind::Lines3:
        if (stroked)
            keys.push_back(lineKey(PartKind::Line3));
        break;
    case Shape::Kind::Area:
        if (s.hasFill() && s.fill().visible()) {
            RunKey k;
            k.kind     = PartKind::Fill;
            k.gradient = s.fill().isGradient();
            if (k.gradient)
                k.paint = s.fill();
            keys.push_back(k);
        }
        if (stroked)
            keys.push_back(lineKey(PartKind::Line));
        break;
    case Shape::Kind::Text:
    case Shape::Kind::Text3:
        // Också en TOM text får sin del: den kan få en text vid ett språkbyte
        // (en översättning), och körningarna ska inte bero på språket.
        if (s.textStyle().size.value > 0.0f)
            keys.push_back(textKey(s.kind() == Shape::Kind::Text3
                                       ? PartKind::Glyph3
                                       : PartKind::Glyph));
        break;
    case Shape::Kind::Markers:
    case Shape::Kind::Markers3: {
        const bool rymd = s.kind() == Shape::Kind::Markers3;
        RunKey     k;
        if ((rymd ? !s.points3().empty() : !s.points().empty()) &&
            s.markerStyle().size.value > 0.0f &&
            markerKey(rymd ? PartKind::Glyph3 : PartKind::Glyph, k))
            keys.push_back(k);
        break;
    }
    case Shape::Kind::Image: {
        // En bild på fyra hörn som inte är strikt konvexa ritar ingenting —
        // som en ogiltig bild (shape.h). Opaciteten och färgjusteringen är
        // anropets uniformer, alltså nyckelns.
        const ImageStyle& st = s.imageStyle();
        if (s.image().valid() && st.opacity > 0.0f && imageQuadOk(s)) {
            RunKey k;
            k.kind       = PartKind::Image;
            k.image      = s.image();
            k.opacity    = st.opacity;
            k.saturation = st.saturation;
            k.contrast   = st.contrast;
            k.brightness = st.brightness;
            keys.push_back(k);
        }
        break;
    }
    // FÖREMÅLEN: ett föremål utan data ritar ingenting.
    // Varje föremål är sitt eget rit-anrop (stilen är anropets), så nyckeln är
    // bara slaget — på varandra följande föremål delar körning.
    case Shape::Kind::Mesh:
        if (s.mesh().valid()) {
            RunKey k;
            k.kind = PartKind::Mesh;
            keys.push_back(k);
        }
        break;
    case Shape::Kind::Area3:
        if (s.mesh().valid()) {
            RunKey k;
            k.kind = PartKind::Mesh;
            keys.push_back(k);
        }
        if (stroked)
            keys.push_back(lineKey(PartKind::Line3));
        break;
    case Shape::Kind::Instances:
        if (s.mesh().valid() && s.instanceSet() && !s.instanceSet()->rows.empty()) {
            RunKey k;
            k.kind = PartKind::Mesh;
            keys.push_back(k);
        }
        break;
    case Shape::Kind::Points3:
        if (s.pointCloud().valid()) {
            RunKey k;
            k.kind = PartKind::Points;
            keys.push_back(k);
        }
        break;
    // HELSKÄRMSPASSET: alltid en del — också ett okänt program
    // ritar något (reservfärgen).
    case Shape::Kind::Pass: {
        RunKey k;
        k.kind = PartKind::Pass;
        keys.push_back(k);
        break;
    }
    case Shape::Kind::None:
        break;
    }
    return keys;
}

std::optional<Box3> objectBox(const Shape& s)
{
    switch (s.kind()) {
    case Shape::Kind::Mesh:
    case Shape::Kind::Area3: {
        if (!s.mesh().valid())
            return std::nullopt;
        const MeshData&  d = *MeshAccess::data(s.mesh());
        const Placement& p = s.placement();
        const Point3     at{d.origin.x + p.at.x, d.origin.y + p.at.y,
                            d.origin.z + p.at.z};
        if (s.meshStyle().size.value > 0.0f)
            return Box3{at, at};   // skärmkonstant: lådan är skärmens
        const Affine3 m = rotationScale(p.yaw, p.pitch, p.roll, p.scale, at.x,
                                        at.y, at.z);
        return transformBox(m, Box3{{d.bounds.min.x - d.origin.x,
                                     d.bounds.min.y - d.origin.y,
                                     d.bounds.min.z - d.origin.z},
                                    {d.bounds.max.x - d.origin.x,
                                     d.bounds.max.y - d.origin.y,
                                     d.bounds.max.z - d.origin.z}});
    }
    case Shape::Kind::Instances: {
        const InstanceSet* set = s.instanceSet().get();
        if (!set || set->rows.empty() || !s.mesh().valid())
            return std::nullopt;
        if (s.meshStyle().size.value > 0.0f) {
            // Skärmkonstant: instansernas lägen.
            Box3 b = set->cells.front().anchors;
            for (const InstanceCell& c : set->cells) {
                b.min = {std::min(b.min.x, c.anchors.min.x),
                         std::min(b.min.y, c.anchors.min.y),
                         std::min(b.min.z, c.anchors.min.z)};
                b.max = {std::max(b.max.x, c.anchors.max.x),
                         std::max(b.max.y, c.anchors.max.y),
                         std::max(b.max.z, c.anchors.max.z)};
            }
            return b;
        }
        return set->bounds;
    }
    case Shape::Kind::Points3:
        if (!s.pointCloud().valid())
            return std::nullopt;
        return s.pointCloud().bounds();
    case Shape::Kind::None:
    case Shape::Kind::Lines:
    case Shape::Kind::Area:
    case Shape::Kind::Text:
    case Shape::Kind::Markers:
    case Shape::Kind::Image:
    case Shape::Kind::Lines3:
    case Shape::Kind::Text3:
    case Shape::Kind::Markers3:
    case Shape::Kind::Pass:
        break;
    }
    return std::nullopt;
}

bool shapeBounds(const Shape& s, double& x0, double& y0, double& x1,
                 double& y1, const Xform& xf)
{
    bool       any = false;
    const auto add = [&](const Point& p) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y))
            return;
        if (!any) {
            x0 = x1 = p.x;
            y0 = y1 = p.y;
            any = true;
            return;
        }
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    };
    for (const Shape::Contour& c : s.contours())
        for (const Point& p : c.points)
            add(p);
    for (const Point& p : s.points())
        add(p);
    // I rymden: punkternas xy (en punkt som inte är ändlig i någon axel
    // hoppar byggarna över — lådan likaså).
    for (const Point3& p : s.points3())
        if (std::isfinite(p.z))
            add(Point{p.x, p.y});
    // En bild: rektangeln — för en bild på fyra hörn LÅDAN kring hörnen
    // (shape.cpp), och hörnen själva står redan i `points()` ovan.
    if (s.kind() == Shape::Kind::Image) {
        const Rect& r = s.rect();
        add(Point{r.x, r.y});
        add(Point{r.x + r.w, r.y + r.h});
    }
    // Föremålen: lådan i världen (ytan i rymden har
    // sina punkter ovan).
    if (s.kind() == Shape::Kind::Mesh || s.kind() == Shape::Kind::Instances ||
        s.kind() == Shape::Kind::Points3)
        if (const std::optional<Box3> b = objectBox(s)) {
            add(Point{b->min.x, b->min.y});
            add(Point{b->max.x, b->max.y});
        }
    if (!any)
        return false;
    // En transformerad låda: rutan kring dess fyra transformerade hörn —
    // en affin avbildning tar lådan till en parallellogram som rymmer allt.
    const Point p[4] = {xf.apply({x0, y0}), xf.apply({x1, y0}),
                        xf.apply({x0, y1}), xf.apply({x1, y1})};
    x0 = x1 = p[0].x;
    y0 = y1 = p[0].y;
    for (const Point& q : p) {
        x0 = std::min(x0, q.x);
        y0 = std::min(y0, q.y);
        x1 = std::max(x1, q.x);
        y1 = std::max(y1, q.y);
    }
    return true;
}

void shapeZRange(const Shape& s, double& z0, double& z1)
{
    z0 = z1 = 0.0;
    bool any = false;
    for (const Point3& p : s.points3()) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            continue;
        z0  = any ? std::min(z0, p.z) : p.z;
        z1  = any ? std::max(z1, p.z) : p.z;
        any = true;
    }
    if (s.kind() == Shape::Kind::Mesh || s.kind() == Shape::Kind::Instances ||
        s.kind() == Shape::Kind::Points3)
        if (const std::optional<Box3> b = objectBox(s)) {
            z0 = b->min.z;
            z1 = b->max.z;
        }
}

namespace {

// Konturens stil i byggarens form — densamma för linjen i planet och i
// rymden.
gfx::PolylineStyle polylineStyle(const Stroke& st)
{
    gfx::PolylineStyle ps;
    ps.width = st.width.value;
    ps.color = st.paint.isGradient() ? kWhite : st.paint.color();
    switch (st.cap) {
    case LineCap::Round:  ps.cap = gfx::LineCap::Round;  break;
    case LineCap::Square: ps.cap = gfx::LineCap::Square; break;
    case LineCap::Butt:   ps.cap = gfx::LineCap::Butt;   break;
    }
    switch (st.join) {
    case LineJoin::Miter: ps.join = gfx::LineJoin::Miter; break;
    case LineJoin::Bevel: ps.join = gfx::LineJoin::Bevel; break;
    case LineJoin::Round: ps.join = gfx::LineJoin::Round; break;
    }
    return ps;
}

} // namespace

void buildLine3Part(const Shape& s, std::vector<gfx::LineChunk3>& chunks,
                    const Xform& xf)
{
    gfx::PolylineStyle ps = polylineStyle(s.stroke());
    // Ytan i rymden (`polygon3`) har en SLUTEN kontur.
    ps.closed = s.kind() == Shape::Kind::Area3;
    std::vector<gfx::Vec3d>  p;
    p.reserve(s.points3().size());
    for (const Point3& q : s.points3()) {
        const Point3 t = xf.apply3(q);
        p.push_back(gfx::Vec3d{t.x, t.y, t.z});
    }
    gfx::buildPolyline3(p, ps, chunks);
}

void buildLinePart(const Shape& s, std::vector<gfx::LineChunk>& chunks,
                   const Xform& xf)
{
    gfx::PolylineStyle ps = polylineStyle(s.stroke());
    for (const Shape::Contour& c : s.contours()) {
        std::vector<gfx::Point2d> p;
        p.reserve(c.points.size());
        for (const Point& q : c.points)
            p.push_back(toGfx(xf.apply(q)));
        // En SLUTEN kontur (en ring, en ytas kant) går tillbaka till sin
        // start med en skarv av konturens slag där — byggaren sluter den.
        ps.closed = c.closed;
        gfx::buildPolyline(p, ps, chunks);
    }
}

float linePartHalfWidth(const Shape& s)
{
    const Stroke& st    = s.stroke();
    float         reach = 1.0f;
    if (st.join == LineJoin::Miter && std::isfinite(st.miterLimit))
        reach = std::max(reach, st.miterLimit);
    if (st.cap == LineCap::Square)
        reach = std::max(reach, std::sqrt(2.0f));
    return 0.5f * std::max(st.width.value, 0.0f) * reach;
}

void strokeUniforms(const RunKey& k, float pxPerWorld, float pxPerDashUnit,
                    gfx::SceneUniforms& u)
{
    if (!isLineKind(k.kind))
        return;
    u.miterLimit = k.miterLimit;
    u.dash       = k.dash;
    // Mönsterenheter per världsenhet: i världsenheter är mönstret världens
    // (1), i px och mm skärmens — kamerans skala i mitten genom enhetens.
    u.dash.perWorldUnit =
        k.dashUnit == Unit::World || !(pxPerDashUnit > 0.0f)
            ? 1.0f
            : pxPerWorld / pxPerDashUnit;
    if (!(u.dash.perWorldUnit > 0.0f) || !std::isfinite(u.dash.perWorldUnit))
        u.dash.count = 0;   // en kamera utan skala: inget mönster att mäta
}

void imageUniforms(const RunKey& k, gfx::SceneUniforms& u)
{
    if (k.kind != PartKind::Image)
        return;
    u.saturation = k.saturation;
    u.contrast   = k.contrast;
    u.brightness = k.brightness;
}

void buildFillPart(const Shape& s, std::vector<gfx::FillChunk>& chunks,
                   const Xform& xf)
{
    std::vector<std::vector<gfx::Point2d>> rings;
    rings.reserve(s.contours().size());
    for (const Shape::Contour& c : s.contours()) {
        std::vector<gfx::Point2d> r;
        r.reserve(c.points.size());
        for (const Point& q : c.points)
            r.push_back(toGfx(xf.apply(q)));
        rings.push_back(std::move(r));
    }
    gfx::PolygonStyle ps;
    ps.color = s.fill().isGradient() ? kWhite : s.fill().color();
    gfx::buildPolygon(rings, ps, chunks);
}

void imageCorners(const Rect& r, bool yUp, Point out[4])
{
    // Normalt: (x0, y0) är det minsta hörnet. Bildens ÖVERKANT är den med
    // störst y när världens y pekar uppåt (en karta), annars den med minst.
    const double x0 = std::min(r.x, r.x + r.w), x1 = std::max(r.x, r.x + r.w);
    const double y0 = std::min(r.y, r.y + r.h), y1 = std::max(r.y, r.y + r.h);
    const double top = yUp ? y1 : y0, bottom = yUp ? y0 : y1;
    out[0] = Point{x0, top};
    out[1] = Point{x1, top};
    out[2] = Point{x0, bottom};
    out[3] = Point{x1, bottom};
}

void imageCornersOf(const Shape& s, bool yUp, Point out[4])
{
    if (!s.isImageQuad()) {
        imageCorners(s.rect(), yUp, out);
        return;
    }
    // Ringen ÖV, ÖH, NH, NV i z-ordning: ÖV, ÖH, NV, NH. Hörnen är redan
    // bildens som den står, så y-riktningen spelar ingen roll.
    const std::vector<Point>& p = s.points();
    out[0] = p[0];
    out[1] = p[1];
    out[2] = p[3];
    out[3] = p[2];
}

bool imageQuadOk(const Shape& s)
{
    if (!s.isImageQuad())
        return true;
    const std::vector<Point>& p = s.points();
    const gfx::Point2d ring[4] = {toGfx(p[0]), toGfx(p[1]), toGfx(p[2]),
                                  toGfx(p[3])};
    float q[4];
    return gfx::quadWeights(ring, q);
}

void buildImageQuad(const Point corners[4], std::vector<ImageChunk>& chunks,
                    const ImageUv& uv)
{
    if (chunks.empty() ||
        chunks.back().vertices.size() + 4 > gfx::kMaxChunkVertices) {
        ImageChunk c;
        c.originX = corners[0].x;
        c.originY = corners[0].y;
        chunks.push_back(std::move(c));
    }
    // Vikterna ur ringen {c0, c1, c3, c2} (z-ordningen tillbaka till ring),
    // i double och ur hörnen SOM DE RITAS — en affin transform ändrar inte
    // vikterna, men hörnen den lämnar är de som gäller. Ingen yta ⇒ ettor.
    const gfx::Point2d ring[4] = {toGfx(corners[0]), toGfx(corners[1]),
                                  toGfx(corners[3]), toGfx(corners[2])};
    float qr[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    if (!gfx::quadWeights(ring, qr))
        qr[0] = qr[1] = qr[2] = qr[3] = 1.0f;
    const float q[4] = {qr[0], qr[1], qr[3], qr[2]};   // tillbaka i z-ordning

    ImageChunk&     c    = chunks.back();
    const auto      base = static_cast<std::uint32_t>(c.vertices.size());
    const float     u[4] = {uv.u0, uv.u1, uv.u0, uv.u1};
    const float     v[4] = {uv.v0, uv.v0, uv.v1, uv.v1};
    for (int k = 0; k < 4; ++k) {
        gfx::ImageVertex iv;
        iv.x = static_cast<float>(corners[k].x - c.originX);
        iv.y = static_cast<float>(corners[k].y - c.originY);
        iv.u = u[k];
        iv.v = v[k];
        iv.q = q[k];
        c.vertices.push_back(iv);
    }
    c.indices.insert(c.indices.end(),
                     {base, base + 1, base + 2, base + 2, base + 1, base + 3});
}

namespace {

// Plattans HOMOGRAFI (Heckbert, kvadraten till fyrhörningen): punkten (u, v)
// i bildens uv på fyrhörningen med hörnen `c` i z-ordning. En homografi är
// bestämd av sina fyra hörnpar, så det är exakt den avbildning
// Image-programmet ritar med vikterna ur `quadWeights` — och för en
// parallellogram (summan av hörnen lika) den affina.
Point imageQuadPoint(const Point c[4], double u, double v)
{
    // Ringen ÖV, ÖH, NH, NV — z-ordningens c0, c1, c3, c2.
    const Point& p0 = c[0];
    const Point& p1 = c[1];
    const Point& p2 = c[3];
    const Point& p3 = c[2];
    const double sx = p0.x - p1.x + p2.x - p3.x;
    const double sy = p0.y - p1.y + p2.y - p3.y;
    double       g = 0.0, h = 0.0;
    if (sx != 0.0 || sy != 0.0) {
        const double dx1 = p1.x - p2.x, dx2 = p3.x - p2.x;
        const double dy1 = p1.y - p2.y, dy2 = p3.y - p2.y;
        const double det = dx1 * dy2 - dx2 * dy1;
        if (det != 0.0) {
            g = (sx * dy2 - dx2 * sy) / det;
            h = (dx1 * sy - sx * dy1) / det;
        }
    }
    const double a = p1.x - p0.x + g * p1.x;
    const double b = p3.x - p0.x + h * p3.x;
    const double d = p1.y - p0.y + g * p1.y;
    const double e = p3.y - p0.y + h * p3.y;
    const double w = g * u + h * v + 1.0;
    return Point{(a * u + b * v + p0.x) / w, (d * u + e * v + p0.y) / w};
}

} // namespace

void splitImageQuads(const std::vector<gfx::ImageVertex>& vertices,
                     const std::vector<std::uint32_t>& indices, double originX,
                     double originY, const ImageUv& part, const ImageUv& uv,
                     std::vector<ImageChunk>& out)
{
    for (std::size_t i = 0; i + 6 <= indices.size(); i += 6) {
        const std::uint32_t base = indices[i];
        if (indices[i + 1] == base || base + 4 > vertices.size())
            continue;   // död platta (sex lika index)
        Point c[4];
        for (std::uint32_t k = 0; k < 4; ++k)
            c[k] = Point{originX + vertices[base + k].x,
                         originY + vertices[base + k].y};
        const Point del[4] = {imageQuadPoint(c, part.u0, part.v0),
                              imageQuadPoint(c, part.u1, part.v0),
                              imageQuadPoint(c, part.u0, part.v1),
                              imageQuadPoint(c, part.u1, part.v1)};
        buildImageQuad(del, out, uv);
    }
}

float toUnit(Length l, const GlyphUnits& u)
{
    if (l.unit == u.unit)
        return l.value;
    const auto phys = [&u](Unit unit) {
        switch (unit) {
        case Unit::Mm:    return u.pxPerMm;
        case Unit::World: return u.pxPerWorldUnit;
        case Unit::Px:    break;
        }
        return u.pxPerPx;
    };
    const float to = phys(u.unit);
    return to > 0.0f ? l.value * phys(l.unit) / to : 0.0f;
}

GlyphLimits glyphLimits(Length min, Length max, float pxPerPx, float pxPerMm,
                        float pxPerWorld)
{
    const auto phys = [&](Length l) {
        if (!(l.value > 0.0f) || !std::isfinite(l.value))
            return 0.0f;
        const float d = l.unit == Unit::Mm      ? pxPerMm
                        : l.unit == Unit::World ? pxPerWorld
                                                : pxPerPx;
        const float v = l.value * d;
        return v > 0.0f && std::isfinite(v) ? v : 0.0f;
    };
    return GlyphLimits{phys(min), phys(max)};
}

GlyphLimits glyphLimits(const RunKey& k, float pxPerPx, float pxPerMm,
                        float pxPerWorld)
{
    return glyphLimits(k.minSize, k.maxSize, pxPerPx, pxPerMm, pxPerWorld);
}

double clampGlyphPx(double sizePx, const GlyphLimits& l)
{
    if (l.minPx > 0.0f)
        sizePx = std::max(sizePx, static_cast<double>(l.minPx));
    if (l.maxPx > 0.0f)
        sizePx = std::min(sizePx, static_cast<double>(l.maxPx));
    return sizePx;
}

bool visibleAtZoom(const Shape& s, double zoom)
{
    switch (s.kind()) {
    case Shape::Kind::Text:
    case Shape::Kind::Text3:
        return zoomInRange(s.textStyle().minZoom, s.textStyle().maxZoom, zoom);
    case Shape::Kind::Markers:
    case Shape::Kind::Markers3:
        return zoomInRange(s.markerStyle().minZoom, s.markerStyle().maxZoom,
                           zoom);
    default:
        break;
    }
    return true;
}

std::uint8_t fontKey(const TextStyle& st)
{
    return static_cast<std::uint8_t>(st.family == gfx::FontFamily::Mono ? 2 : 1);
}

gfx::FontFamily fontFamilyOf(std::uint8_t key)
{
    return key >= 2 ? gfx::FontFamily::Mono : gfx::FontFamily::UI;
}

std::vector<gfx::StyledText> textParts(const std::vector<TextSpan>& spans,
                                       const TextStyle&             st,
                                       const GlyphUnits&            u)
{
    std::vector<gfx::StyledText> parts;
    parts.reserve(spans.size());
    for (const TextSpan& sp : spans)
        parts.push_back(gfx::StyledText{
            sp.text.str(), sp.color.value_or(st.color),
            gfx::fontStyle(sp.bold.value_or(st.bold),
                           sp.italic.value_or(st.italic)),
            sp.background.value_or(gfx::Color{0, 0, 0, 0}),
            sp.tabStop ? toUnit(*sp.tabStop, u) : 0.0f});
    return parts;
}

std::vector<gfx::StyledText> textParts(const Shape& s, const GlyphUnits& u)
{
    const TextStyle& st = s.textStyle();
    if (!s.spans().empty())
        return textParts(s.spans(), st, u);
    return {gfx::StyledText{s.text().str(), st.color,
                            gfx::fontStyle(st.bold, st.italic)}};
}

gfx::TextLayoutStyle textLayout(const TextStyle& st, const GlyphUnits& u,
                                std::uint32_t label)
{
    gfx::TextLayoutStyle ls;
    ls.size       = toUnit(st.size, u);
    ls.color      = st.color;
    ls.fontStyle  = gfx::fontStyle(st.bold, st.italic);
    ls.halo       = st.halo;
    ls.haloWidth  = st.halo.a > 0 ? toUnit(st.haloWidth, u) : 0.0f;
    ls.background = st.background;
    ls.padding    = toUnit(st.padding, u);
    ls.anchor     = st.anchor;
    ls.align      = textAlign(st.align);
    ls.angleDeg   = st.angle;
    ls.rotation   = st.rotation;
    ls.offsetX    = toUnit(st.offsetX, u);
    ls.offsetY    = toUnit(st.offsetY, u);
    ls.wrapWidth  = toUnit(st.wrapWidth, u);
    ls.label      = label;
    ls.maxLines   = std::max(st.maxLines, 0);
    ls.maxHeight  = st.maxHeight.value > 0.0f ? toUnit(st.maxHeight, u) : 0.0f;
    ls.outline      = st.outline;
    ls.outlineWidth = st.outline.a > 0 ? toUnit(st.outlineWidth, u) : 0.0f;
    ls.lineSpacing  = st.lineSpacing;
    return ls;
}

std::optional<TextMetrics> measureLabel(gfx::SdfAtlas& atlas,
                                        std::span<const gfx::StyledText> parts,
                                        const TextStyle& style,
                                        const GlyphUnits& u, Unit out)
{
    if (!atlas.valid())
        return std::nullopt;
    // Textblocket: ingen platta, luft, ankare eller förskjutning.
    TextStyle bare = style;
    bare.padding = Length{};
    bare.anchor  = TextAnchor::TopLeft;
    bare.offsetX = bare.offsetY = Length{};
    const gfx::TextLayoutStyle ls = textLayout(bare, u, 0);
    const gfx::TextMeasure     m  = gfx::measureText(atlas, parts, ls);
    TextMetrics                t;
    if (m.lines == 0)
        return t;   // storleken ≤ 0 — inget att mäta
    // em × storleken är storlekens enhet; den räknas om till `out` med
    // samma tätheter som etiketten byggs med — och GOLVET OCH TAKET
    // skalar den som Glyph-programmet gör vid samma tätheter.
    GlyphUnits uOut = u;
    uOut.unit       = out;
    double k        = toUnit(Length{ls.size, u.unit}, uOut);
    const double raw = static_cast<double>(ls.size) *
                       (u.unit == Unit::Mm      ? u.pxPerMm
                        : u.unit == Unit::World ? u.pxPerWorldUnit
                                                : u.pxPerPx);
    if (raw > 0.0 && std::isfinite(raw))
        k *= clampGlyphPx(raw, glyphLimits(style.minSize, style.maxSize,
                                           u.pxPerPx, u.pxPerMm,
                                           u.pxPerWorldUnit)) /
             raw;
    t.width      = m.width * k;
    t.height     = m.height * k;
    t.lineHeight = m.lineHeight * k;
    t.baseline   = m.ascent * k;
    t.lines      = m.lines;
    t.truncated  = m.truncated;
    return t;
}

bool measureNeedsWorld(const TextStyle& st, const std::vector<TextSpan>& spans,
                       Unit out)
{
    // De mått `textLayout` och `textParts` räknar om och mätningen läser
    // (padding och förskjutningen nollar `measureLabel`), och golvet och
    // taket — en gräns ≤ 0 är ingen, och ett värde ≤ 0 räknas inte om.
    const auto world = [](Length l) {
        return l.unit == Unit::World && l.value > 0.0f;
    };
    for (const TextSpan& sp : spans)
        if (sp.tabStop && world(*sp.tabStop))
            return true;
    return out == Unit::World || world(st.size) || world(st.wrapWidth) ||
           world(st.maxHeight) || world(st.minSize) || world(st.maxSize);
}

gfx::MarkerLayoutStyle markerLayout(const MarkerStyle& st, const GlyphUnits& u,
                                    std::uint32_t label)
{
    gfx::MarkerLayoutStyle ms;
    ms.shape        = st.shape;
    ms.size         = toUnit(st.size, u);
    ms.fill         = st.fill;
    ms.outline      = st.outline;
    ms.outlineWidth = st.outline.a > 0 ? toUnit(st.outlineWidth, u) : 0.0f;
    ms.angleDeg     = st.angle;
    ms.rotation     = st.rotation;
    ms.anchor       = st.anchor;
    ms.image        = st.img.valid();
    ms.label        = label;
    return ms;
}

float glyphReach(const gfx::GlyphChunk& c)
{
    float reach = 0.0f;
    for (const gfx::GlyphVertex& v : c.vertices)
        reach = std::max(reach, std::hypot(v.ox, v.oy) * v.size + v.haloWidth);
    return reach;
}

float glyphReach(const gfx::GlyphChunk3& c)
{
    float reach = 0.0f;
    for (const gfx::GlyphVertex3& v : c.vertices)
        reach = std::max(reach, std::hypot(v.ox, v.oy) * v.size + v.haloWidth);
    return reach;
}

namespace {

template <class V>
float reachEm(const std::vector<V>& vertices)
{
    float reach = 0.0f;
    for (const V& v : vertices)
        if (v.size > 0.0f)
            reach = std::max(reach, std::hypot(v.ox, v.oy) + v.haloWidth / v.size);
    return reach;
}

} // namespace

float glyphReachEm(const gfx::GlyphChunk& c) { return reachEm(c.vertices); }
float glyphReachEm(const gfx::GlyphChunk3& c) { return reachEm(c.vertices); }

} // namespace glaze::detail
