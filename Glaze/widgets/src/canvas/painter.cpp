// glaze::Painter — ritytans omedelbara läge. Samma
// byggare som lagren (build.h), in i renderarens TRANSIENTA ring: delarna
// samlas i en SATS så länge nyckeln (program, enhet, gradient, bild, font)
// och rymden är desamma, och satsen töms — laddas upp och ritas — vid varje
// byte och när målaren går ur räckvidd. Kontraktet står i
// glaze/canvas/painter.h.

#include "glaze/canvas/painter.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <utility>

#include <gfx/camera.h>
#include <gfx/geom2d.h>
#include <gfx/platform.h>
#include <gfx/sdf.h>

#include "../image_cache.h"
#include "build.h"
#include "camera_math.h"
#include "drape.h"
#include "instances.h"
#include "mesh_data.h"
#include "programs3d.h"
#include "residency.h"
#include "resources.h"
#include "scene.h"
#include "scene3d.h"
#include "target3d.h"
#include "transform3d.h"

namespace glaze {

namespace detail {

namespace {

// Målarens ogiltiga klippyta loggas EN gång per process (`clipTo`) — här i
// filen och inte som en statisk lokal i en inline-medlem (delat läge).
std::atomic<bool> g_ogiltigtKlippLoggat{false};
constexpr double kPi = 3.14159265358979323846;

// Målarens förval: inget djup — ovanpå lagren, som före renderläget
// (painter.h).
constexpr RenderState kNoDepth{.depth = DepthTest::Off, .depthWrite = false};

// En mesh som inte ryms i ett segment ritas inte av målaren (loggas en gång
// per process — painter.h).
std::atomic<bool> g_bigMeshLogged{false};

bool sameState(const RenderState& a, const RenderState& b)
{
    return a.depth == b.depth && a.depthWrite == b.depthWrite &&
           a.depthBias == b.depthBias && a.slopeBias == b.slopeBias &&
           a.blend == b.blend && a.cull == b.cull;
}

// Elementen detaljnivån och stilens intervall ritar ur meshen, [lo, hi) —
// samma regel som lagren (scene3d.cpp) och plockningen (mesh_data.h
// `lodLevel`). Tomt bortom den sista nivån.
bool meshElements(const MeshData& d, const IndexRange& stil, double distance,
                  std::uint32_t& lo, std::uint32_t& hi)
{
    const int nivå = lodLevel(d, distance);
    if (nivå == kNoLevel)
        return false;
    const std::uint64_t n = d.elementCount();
    std::uint64_t       a = 0, b = n;
    if (nivå >= 0) {
        const Lod& l = d.lods[static_cast<std::size_t>(nivå)];
        a            = l.first;
        b            = std::uint64_t{l.first} + l.count;
    }
    const std::uint64_t s0 = std::min<std::uint64_t>(stil.first, n);
    const std::uint64_t s1 = std::min<std::uint64_t>(s0 + stil.count, n);
    a  = std::max(a, s0);
    b  = std::min(b, s1);
    lo = static_cast<std::uint32_t>(a);
    hi = static_cast<std::uint32_t>(b);
    return lo < hi;
}

bool meshTooBig(std::size_t bytes)
{
    if (bytes <= kMeshSegmentBytes)
        return false;
    if (!g_bigMeshLogged.exchange(true))
        gfx::log("Painter::draw: en mesh på %zu byte ryms inte i ett segment "
                 "(64 MiB) och ritas inte av målaren — lägg den i ett lager; "
                 "loggas en gång",
                 bytes);
    return true;
}
} // namespace

struct PainterState {
    PainterState(PaintContext& c, bool s) : ctx(c), screen(s), clip(c.clip) {}

    PaintContext& ctx;
    bool          screen;   // ritytans lokala px (annars världen)
    Xform         xf;
    RenderState   state = kNoDepth;   // setRenderState

    // KLIPPET som gäller (clip.h): maskerna rit-anropen binder. Börjar i
    // ritningens (ritytans, lagrets) och snittas av varje `save(…)`.
    ClipScope clip;

    struct Level {
        bool        screen;
        Xform       xf;
        RenderState state;
        ClipScope   clip;
        bool        clipped = false;   // läget satte ett klipp (`clipTo`)
    };
    std::vector<Level> saved;

    // SATSEN — det som väntar på att ritas.
    bool        open = false;
    RunKey      key;
    bool        keyScreen = false;
    double      refX = 0.0, refY = 0.0;   // satsens första punkt (precisionen)
    double      refZ = 0.0;               //   …i rymden också z
    std::vector<gfx::LineChunk>   lines;
    std::vector<gfx::LineChunk3>  lines3;
    std::vector<gfx::FillChunk>   fills;
    std::vector<gfx::GlyphChunk>  glyphs;
    std::vector<gfx::GlyphChunk3> glyphs3;
    std::vector<ImageChunk>       images;
    gfx::SdfAtlas*                atlas = nullptr;
    gfx::Image*                   image = nullptr;
    // En bild större än GPU:ns största textur är RUTOR — satsens
    // plattor ritas då en gång per ruta (`splitImageQuads`).
    const std::vector<ImageTile>* imageTiles = nullptr;

    const gfx::MapView& view() const { return screen ? ctx.screen : ctx.world; }

    GlyphUnits units(Unit u) const
    {
        GlyphUnits g;
        g.unit           = u;
        g.pxPerPx        = ctx.c->scale;
        g.pxPerMm        = ctx.c->pxPerMm;
        g.pxPerWorldUnit = static_cast<float>(view().pxPerUnit);
        return g;
    }

    // Rymdens matris: den lokala rymdens (skärmen), eller världens genom
    // ritningens kamera — i 3D-målet 3D-vyn (scene.h `chunkMatrix`).
    gfx::Mat4d matrixOf(bool inScreen, double ox, double oy, double oz) const
    {
        if (inScreen)
            return gfx::viewProjection(ctx.screen, ox, oy);
        return chunkMatrix(ctx, ox, oy, oz);
    }
    // Satsens matris — dess rymd.
    gfx::Mat4d matrix(double ox, double oy, double oz) const
    {
        return matrixOf(keyScreen, ox, oy, oz);
    }

    template <class V>
    void drawTransient(gfx::MeshProgram prog, const std::vector<V>& v,
                       const std::vector<std::uint32_t>& idx, double ox,
                       double oy, const gfx::Image* img, double oz = 0.0)
    {
        if (idx.empty() || v.empty())
            return;
        gfx::Renderer&         r  = *ctx.c->r;
        const gfx::BufferSlice vs = r.uploadTransient(
            gfx::BufferKind::Vertex, v.data(), v.size() * sizeof(V));
        const gfx::BufferSlice is = r.uploadTransient(
            gfx::BufferKind::Index, idx.data(), idx.size() * sizeof(std::uint32_t));
        if (!vs.valid() || !is.valid())
            return;
        const gfx::MapView& vy     = keyScreen ? ctx.screen : ctx.world;
        const bool  worldWidth     = (prog == gfx::MeshProgram::Line ||
                                      prog == gfx::MeshProgram::Line3) &&
                                     key.unit == Unit::World;
        gfx::MeshDraw d;
        d.program    = prog;
        d.vertices   = vs;
        d.indices    = is;
        d.indexCount = static_cast<std::uint32_t>(idx.size());
        matrix(ox, oy, oz).toFloat(d.uniforms.viewProj);
        d.uniforms.pxPerWidthUnit = worldWidth ? 1.0f : pxPerUnit(ctx, vy, key.unit);
        d.uniforms.widthSpace =
            worldWidth ? gfx::WidthSpace::World : gfx::WidthSpace::Screen;
        // Målarens opacitet gånger FORMENS och bildens
        // färgjustering — `drawImage(…, ImageStyle)`.
        d.uniforms.opacity = ctx.opacity * key.opacity;
        imageUniforms(key, d.uniforms);
        strokeUniforms(key, pxPerUnit(ctx, vy, Unit::World),
                       pxPerUnit(ctx, vy, key.dashUnit), d.uniforms);
        // KLIPPET — satsen töms vid varje klippbyte, så det som gäller nu
        // är det satsen byggdes under. Det med höjd (Line3, Glyph3) ser
        // världsmasken; allt i skärmrymden bara skärmmasken.
        clip.apply(d.uniforms.clip,
                   !keyScreen && (prog == gfx::MeshProgram::Line3 ||
                                  prog == gfx::MeshProgram::Glyph3),
                   ox, oy);
        if (prog == gfx::MeshProgram::Glyph || prog == gfx::MeshProgram::Glyph3) {
            // GOLVET OCH TAKET — som lagrens `drawGlyphs`.
            const GlyphLimits lim =
                glyphLimits(key, ctx.c->scale, ctx.c->pxPerMm,
                            static_cast<float>(vy.pxPerUnit));
            d.uniforms.glyphMinPx = lim.minPx;
            d.uniforms.glyphMaxPx = lim.maxPx;
        }
        if (key.gradient) {
            const gfx::Image* ramp = ctx.res->ramp(key.paint.stops());
            if (!ramp)
                return;
            const Paint& p = key.paint;
            d.uniforms.paint =
                p.kind() == gfx::PaintKind::Radial
                    ? gfx::radialPaint(gfx::Point2d{p.from().x, p.from().y},
                                       p.r0(), p.r1(), ox, oy)
                    : gfx::linearPaint(gfx::Point2d{p.from().x, p.from().y},
                                       gfx::Point2d{p.to().x, p.to().y}, ox, oy);
            d.image = ramp;
        }
        if (img)
            d.image = img;
        // RENDERLÄGET verkar i 3D-målet och i världen (painter.h) — en text
        // eller markör skriver aldrig djup (scene.h `gfxState`).
        if (!keyScreen && ctx.view3D)
            d.state = gfxState(state, prog == gfx::MeshProgram::Glyph ||
                                          prog == gfx::MeshProgram::Glyph3);
        r.drawMesh(d);
    }

    void flush()
    {
        if (!open)
            return;
        // Texten binder sin atlassida, bildmarkörerna sin bild.
        const auto glyphImage = [this](int page) -> const gfx::Image* {
            return image ? image
                   : atlas && page >= 0
                       ? atlas->page(static_cast<std::size_t>(page))
                       : nullptr;
        };
        switch (key.kind) {
        case PartKind::Line:
            for (const gfx::LineChunk& c : lines)
                drawTransient(gfx::MeshProgram::Line, c.vertices, c.indices,
                              c.originX, c.originY, nullptr);
            break;
        case PartKind::Line3:
            for (const gfx::LineChunk3& c : lines3)
                drawTransient(gfx::MeshProgram::Line3, c.vertices, c.indices,
                              c.originX, c.originY, nullptr, c.originZ);
            break;
        case PartKind::Fill:
            for (const gfx::FillChunk& c : fills)
                drawTransient(gfx::MeshProgram::Fill, c.vertices, c.indices,
                              c.originX, c.originY, nullptr);
            break;
        case PartKind::Glyph:
            for (const gfx::GlyphChunk& c : glyphs)
                drawTransient(gfx::MeshProgram::Glyph, c.vertices, c.indices,
                              c.originX, c.originY, glyphImage(c.page));
            break;
        case PartKind::Glyph3:
            for (const gfx::GlyphChunk3& c : glyphs3)
                drawTransient(gfx::MeshProgram::Glyph3, c.vertices, c.indices,
                              c.originX, c.originY, glyphImage(c.page),
                              c.originZ);
            break;
        case PartKind::Image:
            for (const ImageChunk& c : images) {
                if (!imageTiles) {
                    drawTransient(gfx::MeshProgram::Image, c.vertices,
                                  c.indices, c.originX, c.originY, image);
                    continue;
                }
                for (const ImageTile& t : *imageTiles) {
                    std::vector<ImageChunk> delar;
                    splitImageQuads(c.vertices, c.indices, c.originX,
                                    c.originY, ImageUv{t.u0, t.v0, t.u1, t.v1},
                                    ImageUv{t.tu0, t.tv0, t.tu1, t.tv1}, delar);
                    for (const ImageChunk& del : delar)
                        drawTransient(gfx::MeshProgram::Image, del.vertices,
                                      del.indices, del.originX, del.originY,
                                      t.image);
                }
            }
            break;
        case PartKind::Mesh:     // föremålen ritas genast, aldrig i en sats
        case PartKind::Points:   // (`drawObject`)
        case PartKind::Pass:     // passet är lagrens (shape.h `screenPass`)
            break;
        }
        lines.clear();
        lines3.clear();
        fills.clear();
        glyphs.clear();
        glyphs3.clear();
        images.clear();
        atlas      = nullptr;
        image      = nullptr;
        imageTiles = nullptr;
        open       = false;
    }

    // Satsen för en del: samma nyckel och rymd, och nära satsens första
    // punkt (RTC — en sats är en chunk, och chunkens origo är den punkten;
    // i rymden också i z). Annars töms den gamla och en ny börjar.
    void begin(const RunKey& k, double x0, double y0, double x1, double y1,
               double z0, double z1)
    {
        const bool samma =
            open && keyScreen == screen && key == k &&
            std::max(std::abs(x0 - refX), std::abs(x1 - refX)) <= kChunkCell &&
            std::max(std::abs(y0 - refY), std::abs(y1 - refY)) <= kChunkCell &&
            std::max(std::abs(z0 - refZ), std::abs(z1 - refZ)) <= kChunkCell;
        if (samma)
            return;
        flush();
        open      = true;
        key       = k;
        keyScreen = screen;
        refX      = x0;
        refY      = y0;
        refZ      = z0;
    }

    void add(const Shape& s)
    {
        // SYNLIGHETEN EFTER ZOOM — i världen; skärmrymden har ingen.
        if (!screen && !visibleAtZoom(s, ctx.zoom))
            return;
        double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0, z0 = 0.0, z1 = 0.0;
        if (!shapeBounds(s, x0, y0, x1, y1, xf))
            return;
        shapeZRange(s, z0, z1);
        for (const RunKey& k : partKeys(s)) {
            if (isObjectKind(k.kind)) {
                // Ett föremål är sitt eget rit-anrop: det som väntar först.
                flush();
                drawObject(s, z0, z1);
                continue;
            }
            begin(k, x0, y0, x1, y1, z0, z1);
            switch (k.kind) {
            case PartKind::Line:
                buildLinePart(s, lines, xf);
                break;
            case PartKind::Line3:
                buildLine3Part(s, lines3, xf);
                break;
            case PartKind::Fill:
                buildFillPart(s, fills, xf);
                break;
            case PartKind::Image:
                addImage(s);
                break;
            case PartKind::Glyph:
            case PartKind::Glyph3:
                addGlyph(s, k);
                break;
            case PartKind::Mesh:     // ovan (`drawObject`)
            case PartKind::Points:
            case PartKind::Pass:
                break;
            }
        }
    }

    // ---- FÖREMÅLEN: meshen (också primitiverna och
    // ytan i rymden), instanserna och punkterna — var för sig och GENAST,
    // ur formens CPU-kopia genom den TRANSIENTA ringen (inne i 3D-målet får
    // inget annat laddas upp), med lagrens program, stilens block och
    // ritytans ljus (scene3d.h). Transformen verkar på lägena som för målarens
    // andra former: dess vridning och skala i modellen, förflyttningen i
    // anropets origo (i double — RTC).
    void drawObject(const Shape& s, double z0, double z1)
    {
        // Vyn: 3D-målets i världen, annars kartkamerans (eller den lokala
        // rymdens) som 3D-vy över formens höjder — lagrens 2D-väg.
        const gfx::View3D view =
            !screen && ctx.view3D
                ? *ctx.view3D
                : gfx::toView3D(screen ? ctx.screen : ctx.world, z0, z1);
        switch (s.kind()) {
        case Shape::Kind::Mesh:
        case Shape::Kind::Area3:
            drawMeshShape(s, view);
            break;
        case Shape::Kind::Instances:
            drawInstanceShape(s, view);
            break;
        case Shape::Kind::Points3:
            drawPointShape(s, view);
            break;
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
    }

    // Målarens transform som en affin i rymden: xy som dess, z orört.
    Affine3 xformAffine() const
    {
        Affine3 a;
        a.m[0][0] = xf.a;
        a.m[0][1] = xf.b;
        a.m[1][0] = xf.d;
        a.m[1][1] = xf.e;
        a.t[0]    = xf.c;
        a.t[1]    = xf.f;
        return a;
    }

    // Föremålens renderläge: målarens i 3D-målet och i världen (painter.h).
    gfx::RenderState objectState() const
    {
        return !screen && ctx.view3D ? gfxState(state, false) : gfx::RenderState{};
    }

    // GlazeFrame med anropets origo — skalan, tätheten och tonen som
    // lagrens (gfx/camera.h `FrameUniforms::scale`).
    gfx::FrameUniforms frameAt(const gfx::View3D& v, const Point3& o) const
    {
        gfx::FrameUniforms fr = gfx::frameUniforms(v, o.x, o.y, o.z);
        fr.scale[0] = ctx.c->pxPerMm;
        fr.scale[1] = ctx.c->scale;
        fr.scale[2] = ctx.opacity;
        fr.scale[3] = 0.0f;
        return fr;
    }

    // Programmet, bilderna och blocket för meshen med stilen — false när
    // något saknas (en textur som väntar på bildcachens tak, ett program som
    // inte kompilerade).
    struct MeshBinding {
        MeshVariant               v;
        const gfx::ShaderProgram* program = nullptr;
        std::array<const gfx::Image*, gfx::kMaxProgramTextures> textures{};
        MeshUniforms              u;
    };
    bool bindMesh(const MeshData& d, const MeshStyle& st, const Affine3& model,
                  bool instanced, MeshBinding& b) const
    {
        b.v.normals           = !d.normals.empty();
        b.v.colors            = !d.colors.empty();
        const gfx::Image* tex = nullptr;
        if (!d.uvs.empty() && st.texture.kind() != ImageSource::Kind::None) {
            tex = imageOf(st.texture);
            if (!tex)
                return false;
            b.v.uvs = true;
        }
        const gfx::Image* ramp = nullptr;
        if (!d.scalars.empty() && !st.ramp.stops.empty()) {
            ramp = ctx.res->ramp(st.ramp.stops);
            if (!ramp)
                return false;
            b.v.scalars = true;
        }
        b.v.instanced   = instanced;
        b.v.screenSized = st.size.value > 0.0f;
        b.program       = ctx.res->objects().meshProgram(b.v);
        if (!b.program)
            return false;   // backenden har loggat felet
        const Lighting förval;
        meshUniforms(st, ctx.lighting ? *ctx.lighting : förval, model, b.u);
        const ProgramSlots slots = meshSlots(b.v);
        if (tex) {
            setTexture(b.u, *tex);
            b.textures[static_cast<std::size_t>(slots.texture)] = tex;
        }
        if (ramp) {
            setRamp(b.u, st.ramp.min, st.ramp.max, *ramp);
            b.textures[static_cast<std::size_t>(slots.ramp)] = ramp;
        }
        return true;
    }

    // MESHEN I DEN TRANSIENTA RINGEN — en gång per form och ritning: alla
    // lägen i variantens packning och, indexerat, alla index. Varje rit-anrop
    // (en instansföljd på sin nivå) tar sin del ur dem (`elements`). false när
    // meshen inte ryms i ett segment (index också — D3D11:s IA-gräns gäller
    // varje buffert) eller ringen inte kunde växa.
    struct TransientMesh {
        gfx::BufferSlice vertices, indices;
        std::size_t      stride  = 0;
        bool             indexed = false;

        // Elementen [lo, hi) som rit-anropets strömmar.
        void elements(std::uint32_t lo, std::uint32_t hi, gfx::MeshDraw& d) const
        {
            const std::size_t n = hi - lo;
            if (indexed) {
                d.vertices   = vertices;
                d.indices    = gfx::BufferSlice{indices.buffer,
                                                indices.offset + lo * sizeof(std::uint32_t),
                                                n * sizeof(std::uint32_t)};
                d.indexCount = static_cast<std::uint32_t>(n);
            } else {
                d.vertices    = gfx::BufferSlice{vertices.buffer,
                                                 vertices.offset + lo * stride,
                                                 n * stride};
                d.vertexCount = static_cast<std::uint32_t>(n);
            }
        }
    };
    bool uploadMesh(const MeshData& d, const MeshVariant& v,
                    const gfx::VertexLayout& layout, TransientMesh& out) const
    {
        out.stride  = layout.stride[0];
        out.indexed = d.isIndexed();
        const std::size_t count = d.positions.size();
        if (meshTooBig(count * out.stride) ||
            meshTooBig(d.indices.size() * sizeof(std::uint32_t)))
            return false;
        std::vector<std::uint8_t> vb(count * out.stride);
        for (std::size_t j = 0; j < count; ++j) {
            MeshVertex mv;
            mv.position = d.positions[j];
            if (v.normals)
                mv.normal = d.normals[j];
            if (v.colors)
                mv.color = d.colors[j];
            if (v.uvs) {
                mv.u = d.uvs[j][0];
                mv.v = d.uvs[j][1];
            }
            if (v.scalars)
                mv.scalar = d.scalars[j];
            packMeshVertex(v, mv, vb.data() + j * out.stride);
        }
        gfx::Renderer& r = *ctx.c->r;
        out.vertices     = r.uploadTransient(gfx::BufferKind::Vertex, vb.data(),
                                             vb.size());
        if (!out.vertices.valid())
            return false;
        if (!out.indexed)
            return true;
        out.indices = r.uploadTransient(gfx::BufferKind::Index, d.indices.data(),
                                        d.indices.size() * sizeof(std::uint32_t));
        return out.indices.valid();
    }

    void drawMeshShape(const Shape& s, const gfx::View3D& view)
    {
        const std::shared_ptr<MeshData>& md = MeshAccess::data(s.mesh());
        if (!md || md->positions.empty())
            return;
        const MeshData&  d  = *md;
        const Placement& pl = s.placement();
        const MeshStyle& st = s.meshStyle();
        // Anropets origo genom transformen (double), och modellen: målarens
        // vridning och skala efter placeringens.
        const Affine3 x = xformAffine();
        Affine3       lin = x;
        lin.t[0] = lin.t[1] = lin.t[2] = 0.0;
        const Point3  origin =
            x.apply(Point3{d.origin.x + pl.at.x, d.origin.y + pl.at.y,
                           d.origin.z + pl.at.z});
        const Affine3 model =
            detail::compose(lin, rotationScale(pl.yaw, pl.pitch, pl.roll, pl.scale));
        // Detaljnivån ur avståndet till den placerade lådan (en skärmkonstant
        // mesh: till origot), som lagren väljer den.
        Box3 box{origin, origin};
        if (!(st.size.value > 0.0f)) {
            Affine3 placed = model;
            placed.t[0]    = origin.x;
            placed.t[1]    = origin.y;
            placed.t[2]    = origin.z;
            box            = transformBox(
                placed, Box3{{d.bounds.min.x - d.origin.x, d.bounds.min.y - d.origin.y,
                              d.bounds.min.z - d.origin.z},
                             {d.bounds.max.x - d.origin.x, d.bounds.max.y - d.origin.y,
                              d.bounds.max.z - d.origin.z}});
        }
        std::uint32_t lo = 0, hi = 0;
        if (!meshElements(d, st.range,
                          distanceToBox(Point3{view.eye.x, view.eye.y, view.eye.z}, box),
                          lo, hi))
            return;
        MeshBinding b;
        if (!bindMesh(d, st, model, false, b))
            return;
        const gfx::VertexLayout layout = meshLayout(b.v);
        TransientMesh           tm;
        if (!uploadMesh(d, b.v, layout, tm))
            return;
        gfx::MeshDraw dr;
        tm.elements(lo, hi, dr);
        dr.program   = gfx::MeshProgram::Custom;
        dr.shader    = b.program;
        dr.layout    = &layout;
        dr.frame     = frameAt(view, origin);
        dr.user      = &b.u;
        dr.userBytes = sizeof(MeshUniforms);
        dr.textures  = b.textures;
        dr.state     = objectState();
        clip.apply(dr.uniforms.clip, !screen, origin.x, origin.y);
        ctx.c->r->drawMesh(dr);
    }

    // INSTANSERNA: block för block — en följd celler i samma block och på
    // samma detaljnivå är ETT anrop, som i lagren (utan gallring: målaren
    // ritar det den får). Raderna går genom målarens vridning och skala, och
    // blockets origo genom hela transformen.
    void drawInstanceShape(const Shape& s, const gfx::View3D& view)
    {
        const InstanceSet*               set = s.instanceSet().get();
        const std::shared_ptr<MeshData>& md  = MeshAccess::data(s.mesh());
        if (!set || set->rows.empty() || !md || md->positions.empty())
            return;
        const MeshData&  d  = *md;
        const Placement& pl = s.placement();
        const MeshStyle& st = s.meshStyle();
        // Prototypens ram i instansens (instances.h) — som lagrens.
        const Affine3 model =
            rotationScale(pl.yaw, pl.pitch, pl.roll, pl.scale, d.origin.x + pl.at.x,
                          d.origin.y + pl.at.y, d.origin.z + pl.at.z);
        MeshBinding b;
        if (!bindMesh(d, st, model, true, b))
            return;
        const gfx::VertexLayout layout = meshLayout(b.v);
        const Affine3           x      = xformAffine();
        const bool              screen3 = st.size.value > 0.0f;
        const Point3            öga{view.eye.x, view.eye.y, view.eye.z};
        // Meshen och raderna EN gång — varje anrop tar sin del. Raderna går
        // genom målarens vridning och skala: x' = a·x + b·y och y' = d·x + e·y
        // för varje kolumn (z orört).
        TransientMesh tm;
        if (!uploadMesh(d, b.v, layout, tm))
            return;
        std::vector<MeshInstance> rader = set->rows;
        for (MeshInstance& m : rader)
            for (int k = 0; k < 4; ++k) {
                const float rx = m.rows[k], ry = m.rows[4 + k];
                m.rows[k]      = static_cast<float>(x.m[0][0] * rx + x.m[0][1] * ry);
                m.rows[4 + k]  = static_cast<float>(x.m[1][0] * rx + x.m[1][1] * ry);
            }
        const gfx::BufferSlice alla = ctx.c->r->uploadTransient(
            gfx::BufferKind::Vertex, rader.data(), rader.size() * sizeof(MeshInstance));
        if (!alla.valid())
            return;

        std::uint32_t första = 0, antal = 0, block = 0, nivåLo = 0, nivåHi = 0;
        bool          öppen = false;
        const auto    rita  = [&] {
            if (!öppen || antal == 0)
                return;
            öppen = false;
            gfx::MeshDraw dr;
            tm.elements(nivåLo, nivåHi, dr);
            dr.instances     = gfx::BufferSlice{alla.buffer,
                                                alla.offset + första * sizeof(MeshInstance),
                                                antal * sizeof(MeshInstance)};
            dr.instanceCount = antal;
            dr.program       = gfx::MeshProgram::Custom;
            dr.shader        = b.program;
            dr.layout        = &layout;
            const Point3 blockOrigin = x.apply(set->blocks[block]);
            dr.frame         = frameAt(view, blockOrigin);
            dr.user          = &b.u;
            dr.userBytes     = sizeof(MeshUniforms);
            dr.textures      = b.textures;
            dr.state         = objectState();
            clip.apply(dr.uniforms.clip, !screen, blockOrigin.x, blockOrigin.y);
            ctx.c->r->drawMesh(dr);
        };
        for (const InstanceCell& c : set->cells) {
            // Detaljnivån per cell ur avståndet till dess låda (en
            // skärmkonstant prototyp: till instansernas lägen), genom
            // transformen.
            std::uint32_t lo = 0, hi = 0;
            const Box3&   ram = screen3 ? c.anchors : c.box;
            if (!meshElements(d, st.range, distanceToBox(öga, transformBox(x, ram)), lo,
                              hi)) {
                rita();
                continue;
            }
            if (öppen && block == c.block && nivåLo == lo && nivåHi == hi &&
                första + antal == c.first) {
                antal += c.count;
                continue;
            }
            rita();
            första = c.first;
            antal  = c.count;
            block  = c.block;
            nivåLo = lo;
            nivåHi = hi;
            öppen  = true;
        }
        rita();
    }

    // PUNKTERNA: chunk för chunk, varje punkt ett element i instansströmmen
    // (programs3d.h) — lägena genom målarens vridning och skala, chunkens
    // origo genom hela transformen.
    void drawPointShape(const Shape& s, const gfx::View3D& view)
    {
        const std::shared_ptr<PointCloudData>& pd = MeshAccess::data(s.pointCloud());
        const PointStyle&                      st = s.pointStyle();
        if (!pd || !(st.size.value > 0.0f) || !std::isfinite(st.size.value))
            return;
        const PointCloudData& d = *pd;
        PointVariant          v;
        v.colors               = !d.colors.empty();
        const gfx::Image* ramp = nullptr;
        if (!d.scalars.empty() && !st.ramp.stops.empty()) {
            ramp = ctx.res->ramp(st.ramp.stops);
            if (!ramp)
                return;
            v.scalars = true;
        }
        const gfx::ShaderProgram* program = ctx.res->objects().pointProgram(v);
        if (!program)
            return;
        PointUniforms  u;
        const Lighting förval;
        const Lighting& ljus = ctx.lighting ? *ctx.lighting : förval;
        setColor(u, st.color);
        if (st.fog)
            setFog(u, ljus.fog, ljus.fogStart, ljus.fogEnd, ljus.fogPower);
        setPointSize(u, st.size.value, st.size.unit, st.shape);
        std::array<const gfx::Image*, gfx::kMaxProgramTextures> textures{};
        if (ramp) {
            setRamp(u, st.ramp.min, st.ramp.max, *ramp);
            textures[static_cast<std::size_t>(pointSlots(v).ramp)] = ramp;
        }
        const gfx::VertexLayout layout = pointLayout(v);
        const std::size_t       stride = layout.stride[1];
        const Affine3           x      = xformAffine();
        std::vector<std::uint8_t> buf;
        for (const PointChunk& c : d.chunks) {
            if (c.count == 0)
                continue;
            buf.assign(std::size_t{c.count} * stride, 0);
            for (std::uint32_t i = 0; i < c.count; ++i) {
                const std::size_t j = std::size_t{c.first} + i;
                const Float3&     p = d.positions[j];
                PointVertex       pv;
                pv.position = Float3{static_cast<float>(x.m[0][0] * p.x + x.m[0][1] * p.y),
                                     static_cast<float>(x.m[1][0] * p.x + x.m[1][1] * p.y),
                                     p.z};
                if (v.colors)
                    pv.color = d.colors[j];
                if (v.scalars)
                    pv.scalar = d.scalars[j];
                packPoint(v, pv, buf.data() + i * stride);
            }
            gfx::MeshDraw dr;
            dr.instances = ctx.c->r->uploadTransient(gfx::BufferKind::Vertex,
                                                     buf.data(), buf.size());
            if (!dr.instances.valid())
                return;
            dr.program       = gfx::MeshProgram::Custom;
            dr.shader        = program;
            dr.layout        = &layout;
            dr.vertexCount   = kPointQuadVertices;
            dr.instanceCount = c.count;
            const Point3 chunkOrigin = x.apply(c.origin);
            dr.frame         = frameAt(view, chunkOrigin);
            dr.user          = &u;
            dr.userBytes     = sizeof(PointUniforms);
            dr.textures      = textures;
            dr.state         = objectState();
            clip.apply(dr.uniforms.clip, !screen, chunkOrigin.x, chunkOrigin.y);
            ctx.c->r->drawMesh(dr);
        }
    }

    void addImage(const Shape& s)
    {
        if (!image && !imageTiles) {
            imageTiles = tilesOf(s.image());
            image      = imageTiles ? nullptr : imageOf(s.image());
            if (!image && !imageTiles)
                return;   // ogiltig, eller väntar på bildcachens tak
        }
        // Hörnen genom målarens affina transform FÖRE vikterna — byggaren
        // räknar dem ur de hörn som ritas (en affin avbildning bevarar dem).
        Point c[4];
        imageCornersOf(s, view().yUp, c);
        for (Point& p : c)
            p = xf.apply(p);
        buildImageQuad(c, images);
    }

    // En Bitmap större än GPU:ns största textur — nullptr annars.
    const std::vector<ImageTile>* tilesOf(const ImageSource& src) const
    {
        if (!ctx.c->images || src.kind() != ImageSource::Kind::Bitmap)
            return nullptr;
        return ctx.c->images->tiles(src.bitmap());
    }

    gfx::Image* imageOf(const ImageSource& src) const
    {
        if (!ctx.c->images)
            return nullptr;
        switch (src.kind()) {
        case ImageSource::Kind::Asset:   return ctx.c->images->get(src.asset());
        case ImageSource::Kind::Bitmap:  return ctx.c->images->get(src.bitmap());
        case ImageSource::Kind::Texture:
            return ctx.c->images->get(src.texture(), *ctx.c);
        case ImageSource::Kind::Icon:    return ctx.c->images->get(src, *ctx.c);
        case ImageSource::Kind::None:    break;
        }
        return nullptr;
    }

    void addGlyph(const Shape& s, const RunKey& k)
    {
        const GlyphUnits u = units(k.unit);
        if (s.kind() == Shape::Kind::Text || s.kind() == Shape::Kind::Text3) {
            if (!atlas)
                atlas = ctx.res->atlas(fontFamilyOf(k.font));
            if (!atlas || !atlas->valid())
                return;
            // Lagrets layout: etikettens stil och delarna (en vanlig text är
            // EN), i familjens atlas — samma vertex som i ett lager.
            const gfx::TextLayoutStyle         ls    = textLayout(s.textStyle(), u, 0);
            const std::vector<gfx::StyledText> parts = textParts(s, u);
            if (s.kind() == Shape::Kind::Text3) {
                const Point3 p = xf.apply3(s.points3().front());
                gfx::buildText(*atlas, parts, gfx::Vec3d{p.x, p.y, p.z}, ls,
                               glyphs3);
                return;
            }
            const Point p = xf.apply(s.points().front());
            gfx::buildText(*atlas, parts, gfx::Point2d{p.x, p.y}, ls, glyphs);
            return;
        }
        // En bildmarkörs bild — satsens nyckel bär den, så hela satsen delar
        // den. Väntar den på bildcachens tak ritas markörerna nästa gång.
        if (k.image.kind() != ImageSource::Kind::None && !image) {
            image = imageOf(k.image);
            if (!image)
                return;
        }
        const gfx::MarkerLayoutStyle ml = markerLayout(s.markerStyle(), u, 0);
        if (s.kind() == Shape::Kind::Markers3) {
            std::vector<gfx::Vec3d> pts;
            pts.reserve(s.points3().size());
            for (const Point3& q : s.points3()) {
                const Point3 p = xf.apply3(q);
                pts.push_back(gfx::Vec3d{p.x, p.y, p.z});
            }
            gfx::buildMarkers(pts, ml, glyphs3);
            return;
        }
        std::vector<gfx::Point2d> pts;
        pts.reserve(s.points().size());
        for (const Point& q : s.points()) {
            const Point p = xf.apply(q);
            pts.push_back(gfx::Point2d{p.x, p.y});
        }
        gfx::buildMarkers(pts, ml, glyphs);
    }

    // KLIPPET (painter.h `save(…)`) på det nyss sparade läget: ytorna i
    // målarens rymd genom transformen, snittade med det som redan gäller —
    // i världen ett prisma, efter `screenSpace` en yta på skärmen (clip.h).
    // Det som väntar ritas först, med det gamla klippet.
    // NYCKELN FÖRST: varje yta får en hash över sina hörn (`polygonKey`) —
    // dess giltighet slås upp på den i poolen (en yta trianguleras inte varje
    // bildruta), och hasharna tillsammans är maskens källa.
    // `kändGiltig`: ytorna är redan prövade (en rektangel med yta och
    // ändliga hörn) — ingen triangulering bara för att fråga.
    void clipTo(std::vector<Polygon> ytor, bool kändGiltig = false)
    {
        flush();
        saved.back().clipped = true;
        for (Polygon& y : ytor) {
            for (Point& p : y.outer)
                p = xf.apply(p);
            for (std::vector<Point>& h : y.holes)
                for (Point& p : h)
                    p = xf.apply(p);
        }
        // En OGILTIG yta (`Canvas::setInvalidClip`): med Show gäller klippet
        // inte alls, med Hide täcker den ytan ingenting.
        const InvalidClip         läge = ctx.scene ? ctx.scene->invalidClip : InvalidClip::Show;
        std::vector<std::uint8_t> giltig(ytor.size(), 1);
        KeyHash                   källa;
        bool                      allaGiltiga = true;
        for (std::size_t i = 0; i < ytor.size(); ++i) {
            const std::uint64_t nyckel = polygonKey(ytor[i]);
            källa.mix(nyckel);
            if (kändGiltig || !ctx.res)
                continue;
            if (!ctx.res->clips().valid(nyckel, [&] { return clipPolygonValid(ytor[i]); })) {
                giltig[i]   = 0;
                allaGiltiga = false;
            }
        }
        if (!allaGiltiga) {
            // En gång per process: ritningen upprepas varje bildruta.
            if (!g_ogiltigtKlippLoggat.exchange(true))
                gfx::log("Painter::save: en klippyta är ogiltig (en självkorsning, ett "
                         "hål utanför ytterringen eller för få punkter) — %s",
                         läge == InvalidClip::Show ? "klippet gäller inte"
                                                   : "den klipper bort sin del");
            if (läge == InvalidClip::Show)
                return;
        }
        clip = refineClip(ctx, clip, screen ? ClipSpace::Screen : ClipSpace::World,
                          ClipPolys{ytor, giltig}, källa.h);
    }

    // EN RITYTA PÅ YTAN (painter.h `drawCanvas`): ritytans lager i ett eget
    // mål i rektangelns upplösning, sedan målets bild på rektangelns fyra
    // hörn under målarens klipp. `cam2` eller `cam3` är kameran.
    bool drawCanvas(Canvas& canvas, const std::shared_ptr<SceneData>& scene,
                    std::uint64_t stamp, const Rect& rect, const Camera* cam2,
                    const Camera3D* cam3);

    // xf = xf · m — m verkar FÖRST på punkterna (som i en ritstack).
    void compose(const Xform& m)
    {
        const Xform t = xf;
        xf.a = t.a * m.a + t.b * m.d;
        xf.b = t.a * m.b + t.b * m.e;
        xf.c = t.a * m.c + t.b * m.f + t.c;
        xf.d = t.d * m.a + t.e * m.d;
        xf.e = t.d * m.b + t.e * m.e;
        xf.f = t.d * m.c + t.e * m.f + t.f;
    }
};

namespace {

// Kartkameran användbar? Ändliga tal och en zoom över noll.
bool validCamera(const Camera& c)
{
    return std::isfinite(c.center.x) && std::isfinite(c.center.y) &&
           std::isfinite(c.zoom) && c.zoom > 0.0 && std::isfinite(c.bearing) &&
           std::isfinite(c.pitch);
}

// Ritytans målare i ritningens läge (target3d.h) — EN väg för `onPaint`,
// `onPaintLayer` och `onPaintOverlay`.
const gfx::Color kPageClear{0, 0, 0, 0};

} // namespace

bool PainterState::drawCanvas(Canvas& canvas, const std::shared_ptr<SceneData>& scene,
                              std::uint64_t stamp, const Rect& rect,
                              const Camera* cam2, const Camera3D* cam3)
{
    const auto fail = [&](const char* why) {
        gfx::log("Painter::drawCanvas(%s): %s — ritytan ritas inte",
                 canvas.name.c_str(), why);
        return false;
    };
    if (!ctx.res || !ctx.c || !ctx.c->r)
        return fail("målaren har ingen renderare");   // lang_gate:rad loggens skäl, ingen användartext
    if (scene->painting)
        return fail("ritytan ritar redan (anropet kom ur dess egen ritning)");   // lang_gate:rad loggens skäl, ingen användartext
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.w) ||
        !std::isfinite(rect.h) || !(rect.w > 0.0) || !(rect.h > 0.0))
        return fail("rektangeln har ingen yta eller ett tal som inte är ändligt");   // lang_gate:rad loggens skäl, ingen användartext
    if (cam3 ? !validCamera3D(*cam3) : !validCamera(*cam2))
        return fail("ogiltig kamera");   // lang_gate:rad loggens skäl, ingen användartext

    // Det som väntar ritas före — med det gamla klippet.
    flush();

    // Rektangelns hörn (z-ordning: övre vänster, övre höger, nedre vänster,
    // nedre höger) genom transformen till målets px: målet får rektangelns
    // upplösning, och en vriden rektangel sina sidors längd.
    Point c[4];
    imageCorners(rect, view().yUp, c);
    for (Point& q : c)
        q = xf.apply(q);
    const Projection pr = screen ? Projection(ctx.screen)
                          : ctx.view3D ? Projection(*ctx.view3D)
                                       : Projection(ctx.world);
    double sx[4], sy[4];
    for (int i = 0; i < 4; ++i)
        if (!pr.toScreen(c[i].x, c[i].y, sx[i], sy[i]))
            return fail("rektangeln ligger bakom kameran");   // lang_gate:rad loggens skäl, ingen användartext
    const double bredd = std::hypot(sx[1] - sx[0], sy[1] - sy[0]);
    const double höjd  = std::hypot(sx[2] - sx[0], sy[2] - sy[0]);
    if (!std::isfinite(bredd) || !std::isfinite(höjd))
        return fail("rektangeln har inga mått på ytan");   // lang_gate:rad loggens skäl, ingen användartext
    gfx::Renderer& r   = *ctx.c->r;
    const int      max = std::max(1, r.maxTextureSize());
    const int      w   = static_cast<int>(std::clamp(std::ceil(bredd), 1.0, double(max)));
    const int      h   = static_cast<int>(std::clamp(std::ceil(höjd), 1.0, double(max)));

    // Ritytans ritläge i målet: ytans resurser och ljus, och kartkameran på
    // målets ruta (en 3D-kamera sätts av `renderCanvasTarget`).
    const gfx::Rectf box{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};
    const Lighting   ljus = canvas.lighting();
    PaintContext     pc;
    pc.c        = ctx.c;
    pc.res      = ctx.res;
    pc.lighting = &ljus;
    pc.scene    = scene.get();
    if (cam2)
        pc.world = mapView(*cam2, canvas.yUp(), canvas.maxPitch(), box, ctx.c->scale);
    else
        pc.world.yUp = canvas.yUp();
    Target3DArgs a;
    a.samples    = canvas.multisample();
    a.camera     = cam3;
    a.labelFade  = canvas.labelFade() && !ctx.res->pageRender();
    a.layerPaint = layerPaintOf(canvas);

    // MÅLETS NYCKEL — det ritningen beror på: ritytan och dess
    // `damageStamp` (varje ändring av den, och fönstrets helomritning),
    // kameran, måtten, klippversionerna, målarens skala och mm-täthet och
    // temats färger för standardikonerna. Ett
    // oförändrat anrop — samma sida igen, en minikarta som ritas om med sin
    // värd — återanvänder målet utan att rita ritytan.
    KeyHash nyckel;
    nyckel.mix(scene->id);
    nyckel.mix(stamp);
    nyckel.mix(clipVersions(*scene));
    nyckel.mix(static_cast<std::uint64_t>(w));
    nyckel.mix(static_cast<std::uint64_t>(h));
    nyckel.mix(static_cast<double>(ctx.c->scale));
    nyckel.mix(static_cast<double>(ctx.c->pxPerMm));
    if (ctx.c->theme)
        mixStockColors(nyckel, ctx.c->theme->colors);   // standardikonerna
    if (cam3) {
        for (const double v : {cam3->target.x, cam3->target.y, cam3->target.z,
                               cam3->distance, double(cam3->azimuth),
                               double(cam3->elevation), double(cam3->roll),
                               double(cam3->fov), cam3->orthoHeight,
                               cam3->nearPlane, cam3->farPlane})
            nyckel.mix(v);
        nyckel.mix(static_cast<std::uint64_t>(cam3->projection));
        nyckel.mix(static_cast<std::uint64_t>(cam3->up));
    } else {
        for (const double v : {cam2->center.x, cam2->center.y, cam2->zoom,
                               double(cam2->bearing), double(cam2->pitch)})
            nyckel.mix(v);
    }
    TargetPool::Slot& mål = ctx.res->pageTarget(nyckel.h, w, h);
    if (!mål.built) {
        const ImageAccount  konto(*ctx.c, scene);
        const PaintingScope ritar(*scene);
        bool tonar = false;
        if (!renderCanvasTarget(pc, scene, a, w, h, kPageClear, mål.target, tonar))
            return false;   // målet gick inte att skapa — loggat
        ctx.res->notePageRender();
        // En bild som väntar på uppladdningens budget saknas i målet, och
        // etiketter som tonar är ett mellanläge — målet ritas då om nästa gång
        // i stället för att återanvändas.
        mål.built = !tonar && !(ctx.c->images && ctx.c->images->uploadsDeferred());
    }

    // Bilden på de fyra hörnen, under målarens klipp. Målet lever framen ut
    // (`pageTarget`).
    std::vector<ImageChunk> quads;
    buildImageQuad(c, quads);
    key       = RunKey{};
    key.kind  = PartKind::Image;
    keyScreen = screen;
    for (const ImageChunk& q : quads)
        drawTransient(gfx::MeshProgram::Image, q.vertices, q.indices, q.originX,
                      q.originY, &mål.target->image());
    return true;
}

void paintWith(Canvas& yta, const Handler<PaintArgs>& h, PaintContext& p, bool screen,
               int layer, const RenderState* state)
{
    Painter painter(p, screen);
    if (state)
        painter.setRenderState(*state);
    PaintArgs e(painter);
    e.layer = layer;
    // Utan handler-epok: målaren körs i ritningen, varje bildruta, och
    // ändrar inga mått (widget.h `Handler`).
    callWithoutEpoch(h, &yta, e);
}

LayerPaint layerPaintOf(Canvas& yta)
{
    if (!yta.onPaintLayer)
        return {};
    return [&yta](PaintContext& p, int layer, const RenderState* state) {
        paintWith(yta, yta.onPaintLayer, p, false, layer, state);
    };
}

} // namespace detail

// ------------------------------------------------------------------ Painter

Painter::Painter(detail::PaintContext& ctx, bool screen)
    : s_(std::make_unique<detail::PainterState>(ctx, screen))
{
}

Painter::~Painter()
{
    s_->flush();
}

void Painter::draw(const Shape& s)
{
    s_->add(s);
}

void Painter::drawLine(Point a, Point b, const Stroke& stroke)
{
    s_->add(Shape::line(a, b, stroke));
}

void Painter::drawPolyline(const std::vector<Point>& points, const Stroke& stroke)
{
    s_->add(Shape::polyline(points, stroke));
}

void Painter::fillPolygon(const std::vector<Point>& outer, const Paint& fill,
                          std::optional<Stroke> stroke)
{
    s_->add(Shape::polygon(outer, fill, std::move(stroke)));
}

void Painter::fillRect(const Rect& r, const Paint& fill)
{
    s_->add(Shape::rect(r, fill));
}

void Painter::drawRect(const Rect& r, const Stroke& stroke)
{
    s_->add(Shape::path(Path{{PathVerb::Move, {r.x, r.y}},
                             {PathVerb::Line, {r.x + r.w, r.y}},
                             {PathVerb::Line, {r.x + r.w, r.y + r.h}},
                             {PathVerb::Line, {r.x, r.y + r.h}},
                             {PathVerb::Close}},
                        stroke));
}

void Painter::fillCircle(Point center, double r, const Paint& fill)
{
    s_->add(Shape::circle(center, r, fill));
}

void Painter::drawText(Point at, const TrText& text, const TextStyle& style)
{
    s_->add(Shape::text(at, text, style));
}

void Painter::drawText(Point at, const std::vector<TextSpan>& spans,
                       const TextStyle& style)
{
    s_->add(Shape::text(at, spans, style));
}

void Painter::drawMarker(Point at, const MarkerStyle& style)
{
    s_->add(Shape::marker(at, style));
}

void Painter::drawImage(const Rect& r, const ImageSource& source,
                        const ImageStyle& style)
{
    s_->add(Shape::image(r, source, style));
}

void Painter::drawImage(Point topLeft, Point topRight, Point bottomRight,
                        Point bottomLeft, const ImageSource& source,
                        const ImageStyle& style)
{
    s_->add(Shape::image(topLeft, topRight, bottomRight, bottomLeft, source,
                         style));
}

void Painter::drawLine3(Point3 a, Point3 b, const Stroke& stroke)
{
    s_->add(Shape::line3(a, b, stroke));
}

void Painter::drawPolyline3(const std::vector<Point3>& points,
                            const Stroke& stroke)
{
    s_->add(Shape::polyline3(points, stroke));
}

void Painter::drawText3(Point3 at, const TrText& text, const TextStyle& style)
{
    s_->add(Shape::text3(at, text, style));
}

void Painter::drawText3(Point3 at, const std::vector<TextSpan>& spans,
                        const TextStyle& style)
{
    s_->add(Shape::text3(at, spans, style));
}

void Painter::drawMarker3(Point3 at, const MarkerStyle& style)
{
    s_->add(Shape::marker3(at, style));
}

void Painter::drawTriangles3(const std::vector<Point3>& corners,
                             const MeshStyle& style)
{
    // En mesh ur världspunkterna (origot i lådans mitt — RTC) i stilens färg.
    s_->add(Shape::mesh(Mesh::fromWorld(corners), {}, style));
}

void Painter::drawPoints3(const std::vector<Point3>& points,
                          const PointStyle& style)
{
    s_->add(Shape::points3(points, style));
}

void Painter::setRenderState(const RenderState& state)
{
    if (!detail::validRenderState(state)) {
        gfx::log("Painter::setRenderState: ogiltigt renderläge — en uppräkning "
                 "utanför sina värden eller en bias som inte är ändlig "
                 "(ignoreras)");
        return;
    }
    // Läget är rit-anropets: det som redan väntar ritas med det gamla.
    if (!detail::sameState(s_->state, state))
        s_->flush();
    s_->state = state;
}

RenderState Painter::renderState() const
{
    return s_->state;
}

Painter::Saved Painter::save()
{
    s_->saved.push_back(
        detail::PainterState::Level{s_->screen, s_->xf, s_->state, s_->clip});
    return Saved(this);
}

Painter::Saved Painter::save(const Rect& clip)
{
    // En rektangel UTAN YTA är ett tomt klipp — allt klipps bort — och inte
    // en ogiltig yta; ett tal som inte är ändligt är ogiltigt
    // (`InvalidClip`). Annars en yta med fyra hörn, samma väg som varje klipp.
    if (std::isfinite(clip.x) && std::isfinite(clip.y) && std::isfinite(clip.w) &&
        std::isfinite(clip.h) && !(clip.w > 0.0 && clip.h > 0.0))
        return save(std::vector<Polygon>{});
    Polygon y;
    y.outer = {{clip.x, clip.y},
               {clip.x + clip.w, clip.y},
               {clip.x + clip.w, clip.y + clip.h},
               {clip.x, clip.y + clip.h}};
    Saved s = save();
    s_->clipTo({std::move(y)}, std::isfinite(clip.x) && std::isfinite(clip.y) &&
                                   std::isfinite(clip.w) && std::isfinite(clip.h));
    return s;
}

Painter::Saved Painter::save(const Polygon& clip)
{
    Saved s = save();
    s_->clipTo({clip});
    return s;
}

Painter::Saved Painter::save(const std::vector<Polygon>& clip)
{
    Saved s = save();
    s_->clipTo(clip);
    return s;
}

Painter::Saved::~Saved()
{
    if (p_)
        p_->restore();
}

void Painter::restore()
{
    if (s_->saved.empty())
        return;
    const detail::PainterState::Level& l = s_->saved.back();
    // Läget och klippet är rit-anropets: det som väntar ritas med dem.
    if (!detail::sameState(s_->state, l.state) || l.clipped)
        s_->flush();
    s_->screen = l.screen;
    s_->xf     = l.xf;
    s_->state  = l.state;
    s_->clip   = l.clip;
    s_->saved.pop_back();
}

bool Painter::drawCanvas(Canvas& canvas, const Rect& rect, const Camera& camera)
{
    return s_->drawCanvas(canvas, canvas.scene_, canvas.damageStamp(), rect, &camera,
                          nullptr);
}

bool Painter::drawCanvas(Canvas& canvas, const Rect& rect, const Camera3D& camera)
{
    return s_->drawCanvas(canvas, canvas.scene_, canvas.damageStamp(), rect, nullptr,
                          &camera);
}

void Painter::translate(double dx, double dy)
{
    detail::Xform m;
    m.c = dx;
    m.f = dy;
    s_->compose(m);
}

void Painter::rotate(double deg)
{
    const double r = deg * (detail::kPi / 180.0);
    detail::Xform m;
    m.a = std::cos(r);
    m.b = -std::sin(r);
    m.d = std::sin(r);
    m.e = std::cos(r);
    s_->compose(m);
}

void Painter::scale(double sx, double sy)
{
    detail::Xform m;
    m.a = sx;
    m.e = sy;
    s_->compose(m);
}

void Painter::screenSpace()
{
    // En ny rymd: transformen talade den gamla rymdens enheter och börjar om.
    s_->screen = true;
    s_->xf     = detail::Xform{};
}

bool Painter::inScreenSpace() const
{
    return s_->screen;
}

namespace {

// Textblockets storlek i LOGISKA px för etikettens delar — `measureText`s
// två former delar den: familjens atlas (den som ritar texten) och
// etikettens layout utan platta, luft, ankare och förskjutning, MÄTT utan
// uppladdning (build.h `measureLabel` — samma mätning som
// `Canvas::measureText`, här med målarens rymd för världsenheterna).
gfx::Sizef measureParts(detail::PainterState&             s,
                        std::span<const gfx::StyledText> parts,
                        const TextStyle&                  style)
{
    detail::PaintContext& ctx = s.ctx;
    gfx::SdfAtlas*        at  = ctx.res ? ctx.res->atlas(style.family) : nullptr;
    if (!at)
        return gfx::Sizef{};
    const std::optional<TextMetrics> m = detail::measureLabel(
        *at, parts, style, s.units(style.size.unit), Unit::Px);
    if (!m)
        return gfx::Sizef{};
    return gfx::Sizef{static_cast<float>(m->width), static_cast<float>(m->height)};
}

} // namespace

gfx::Sizef Painter::measureText(const TrText& text, const TextStyle& style)
{
    const gfx::StyledText part{text.str(), style.color,
                               gfx::fontStyle(style.bold, style.italic)};
    return measureParts(*s_, std::span<const gfx::StyledText>(&part, 1), style);
}

gfx::Sizef Painter::measureText(const std::vector<TextSpan>& spans,
                                const TextStyle&             style)
{
    return measureParts(*s_,
                        detail::textParts(spans, style,
                                          s_->units(style.size.unit)),
                        style);
}

gfx::Sizef Painter::size() const
{
    const detail::PaintContext& ctx = s_->ctx;
    return gfx::Sizef{ctx.c->box.w / ctx.c->scale, ctx.c->box.h / ctx.c->scale};
}

} // namespace glaze
