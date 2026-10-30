// Föremålen i ritytan — förberedelsen och ritningen.
// Kontraktet står i scene3d.h, residensen i residency.h, programmen och
// blocken i programs3d.h.

#include "scene3d.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>
#include <vector>

#include <gfx/camera.h>
#include <gfx/platform.h>

#include "../image_cache.h"
#include "build.h"
#include "camera_math.h"
#include "drape.h"
#include "instances.h"
#include "labels.h"
#include "mesh_data.h"
#include "residency.h"
#include "resources.h"

namespace glaze::detail {

namespace {

// Gallringens marginal i fysiska px: kantutjämningen och lite till (som
// lagrens, scene.cpp).
constexpr double kCullSlackPx = 2.0;

// Helskärmspassets vertex — en triangel som täcker målet.
constexpr std::uint32_t kFullScreenVertices = 3;

// Ett förval för opaciteten när talet inte är ändligt.
float unit01(float v, float förval)
{
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : förval;
}

gfx::Image* imageOf(const PaintContext& f, const ImageSource& src)
{
    if (!f.c->images)
        return nullptr;
    switch (src.kind()) {
    case ImageSource::Kind::Asset:   return f.c->images->get(src.asset());
    case ImageSource::Kind::Bitmap:  return f.c->images->get(src.bitmap());
    case ImageSource::Kind::Texture: return f.c->images->get(src.texture(), *f.c);
    case ImageSource::Kind::Stock:   return f.c->images->get(src, *f.c);
    case ImageSource::Kind::None:    break;
    }
    return nullptr;
}

// Fysiska px per enhet för en storlek på skärmen (px: skalan, mm: tätheten);
// världsenheter har ingen skärmstorlek (0).
double pxOf(const PaintContext& f, Length l)
{
    switch (l.unit) {
    case Unit::Px:    return static_cast<double>(l.value) * f.c->scale;
    case Unit::Mm:    return static_cast<double>(l.value) * f.c->pxPerMm;
    case Unit::World: break;
    }
    return 0.0;
}

Point3 toPoint(const gfx::Vec3d& v)
{
    return Point3{v.x, v.y, v.z};
}

} // namespace

// GALLRINGEN (scene3d.h): syns någon del av de åtta hörnen? Den homogena
// planprövningen i pixelklippet (X/W, Y/W målets px, Z/W djupet 0–1, W > 0
// framför ögat): lådan gallras när alla hörn ligger utanför SAMMA plan.
bool cornersVisible(const Projection& proj, const Point3 (&c)[8], double margin)
{
    const gfx::Rectf& b  = proj.box();
    const double      x0 = b.x - margin, x1 = b.x + b.w + margin;
    const double      y0 = b.y - margin, y1 = b.y + b.h + margin;
    int               ute[6] = {};
    for (const Point3& p : c) {
        double q[4];
        proj.clip(p.x, p.y, p.z, q);
        ute[0] += q[0] < x0 * q[3];
        ute[1] += q[0] > x1 * q[3];
        ute[2] += q[1] < y0 * q[3];
        ute[3] += q[1] > y1 * q[3];
        ute[4] += q[2] < 0.0;   // bakom närplanet — också bakom ögat
        ute[5] += q[2] > q[3];  // bortom det bortre
    }
    return std::none_of(std::begin(ute), std::end(ute),
                        [](int n) { return n == 8; });
}

bool boxVisible3(const Projection& proj, const Box3& b, double margin)
{
    Point3 c[8];
    for (int k = 0; k < 8; ++k)
        c[k] = Point3{(k & 1) ? b.max.x : b.min.x, (k & 2) ? b.max.y : b.min.y,
                      (k & 4) ? b.max.z : b.min.z};
    return cornersVisible(proj, c, margin);
}

namespace {

// De placerade hörnen av en lokal låda — tätare än den axelparallella lådan
// kring dem för en vriden mesh.
bool placedBoxVisible(const Projection& proj, const Affine3& m, const Box3& local,
                      double margin)
{
    Point3 c[8];
    for (int k = 0; k < 8; ++k)
        c[k] = m.apply(Point3{(k & 1) ? local.max.x : local.min.x,
                              (k & 2) ? local.max.y : local.min.y,
                              (k & 4) ? local.max.z : local.min.z});
    return cornersVisible(proj, c, margin);
}

// Lokala lådans största avstånd från origot — en skärmkonstant meshs räckvidd
// i dess lokala enhet.
double localRadius(const Box3& b)
{
    double r = 0.0;
    for (int k = 0; k < 8; ++k) {
        const double x = (k & 1) ? b.max.x : b.min.x;
        const double y = (k & 2) ? b.max.y : b.min.y;
        const double z = (k & 4) ? b.max.z : b.min.z;
        r = std::max(r, std::sqrt(x * x + y * y + z * z));
    }
    return r;
}

// Elementen nivån och stilens intervall ritar, i meshens element: [lo, hi).
// Tomt bortom den sista nivån.
bool drawnRange(const ObjectResidency::GpuMesh& g, const IndexRange& stil,
                double distance, std::uint32_t& lo, std::uint32_t& hi)
{
    const int nivå = lodLevel(std::span<const Lod>(g.lods), distance);
    if (nivå == kNoLevel)
        return false;
    std::uint64_t a = 0, b = g.elements;
    if (nivå >= 0) {
        const Lod& l = g.lods[static_cast<std::size_t>(nivå)];
        a            = l.first;
        b            = std::uint64_t{l.first} + l.count;
    }
    // Stilens intervall klampas till meshens slut (mesh.h — som substr).
    const std::uint64_t s0 = std::min<std::uint64_t>(stil.first, g.elements);
    const std::uint64_t s1 =
        std::min<std::uint64_t>(s0 + stil.count, g.elements);
    a  = std::max(a, s0);
    b  = std::min(b, s1);
    lo = static_cast<std::uint32_t>(a);
    hi = static_cast<std::uint32_t>(b);
    return lo < hi;
}

// Vyn föremålen ritas genom: 3D-målets, eller i 2D-vägen kartkamerans som
// 3D-vy över körningens höjder.
gfx::View3D objectView(const PaintContext& f, const RunData& run)
{
    if (f.view3D)
        return *f.view3D;
    double zMin = 0.0, zMax = 0.0;
    for (const std::shared_ptr<ShapeEntry>& e : run.objects)
        if (e->alive)
            if (const std::optional<Box3>& b = objectBox(*e)) {
                zMin = std::min(zMin, b->min.z);
                zMax = std::max(zMax, b->max.z);
            }
    return gfx::toView3D(f.world, zMin, zMax);
}

// Syns föremålet i vyn — uppladdningens ordning? Lådan formen
// har NU mot synpyramiden; en skärmkonstant mesh (lådan är origot, storleken
// skärmens) och ett föremål utan låda räknas som synliga.
bool objectInView(const Projection& proj, const ShapeEntry& e,
                  const std::optional<Box3>& b)
{
    const Shape& s = e.shape;
    if (!b || ((s.kind() == Shape::Kind::Mesh || s.kind() == Shape::Kind::Area3 ||
                s.kind() == Shape::Kind::Instances) &&
               s.meshStyle().size.value > 0.0f))
        return true;
    return boxVisible3(proj, *b, kCullSlackPx);
}

// Det ett rit-anrop delar inom en körning.
struct Pass {
    const PaintContext&     f;
    const gfx::View3D&      view;
    const Projection&       proj;
    const Lighting&         light;
    const gfx::RenderState& state;
    float                   op;
    ObjectResidency&        res;
    const SceneData&        scene;    // ritytans egna program
    const DrapeFrame*       drapes;   // terrängens draperingar
};

gfx::FrameUniforms frameAt(const Pass& p, const Point3& origin)
{
    gfx::FrameUniforms fr = gfx::frameUniforms(p.view, origin.x, origin.y, origin.z);
    fr.scale[0] = p.f.c->pxPerMm;
    fr.scale[1] = p.f.c->scale;
    fr.scale[2] = p.op;
    fr.scale[3] = 0.0f;
    return fr;
}

// Programmet, bilderna och blocket för en mesh med stilen — false när något
// saknas (ett program som inte kompilerade, en textur som väntar på
// bildcachens budget: nästa frame). GlazeUser är det inbyggda programmets
// block (`u`) eller ett eget programs (`custom`); `user` pekar på det
// som gäller. Bindningen kopieras aldrig — den fylls på plats.
struct MeshBinding {
    const gfx::ShaderProgram* program = nullptr;
    const gfx::VertexLayout*  layout  = nullptr;
    std::array<const gfx::Image*, gfx::kMaxProgramTextures> textures{};
    MeshUniforms       u;
    std::vector<float> custom;
    const void*        user      = nullptr;
    std::uint32_t      userBytes = 0;
};

// ---- EGNA PROGRAM

// Första gången (rityta, namn) faller tillbaka? Då loggar anroparen — en
// gång per rityta och namn (scene3d.h).
bool firstFallback(const SceneData& sc, const std::string& name)
{
    return sc.fallbackLogged.insert(name).second;
}

// Ritytans eget program med namnet, kompilerat för fönstret — eller nullptr,
// och då är reserven loggad.
const gfx::ShaderProgram* customProgram(const PaintContext& f,
                                        const SceneData& sc,
                                        const std::string& name)
{
    const ProgramSlot* slot = findProgram(sc, name);
    if (!slot) {
        if (firstFallback(sc, name))
            gfx::log("Canvas: programmet '%s' finns inte på ritytan "
                     "(Canvas::addProgram) — reserven ritar i stället (det "
                     "inbyggda programmet, eller passets reservfärg); loggas en "
                     "gång per rityta och namn",
                     name.c_str());
        return nullptr;
    }
    const gfx::ShaderProgram* prog = f.res->program(*slot);
    if (!prog && firstFallback(sc, name))
        gfx::log("Canvas: programmet '%s' är ogiltigt (felet står i "
                 "Canvas::programError) — reserven ritar i stället; loggas en "
                 "gång per rityta och namn",
                 name.c_str());
    return prog;
}

// Läser programmet bara platser som layouten har, med ett format av samma
// slag? Fasadens regel (gfx/program.h), prövad här så att reserven tar över
// i stället för ett anrop som fasaden vägrar.
bool layoutFits(const gfx::VertexLayout& l, const gfx::ShaderProgram& p)
{
    std::uint32_t har = 0, heltal = 0;
    for (const gfx::VertexAttrib& a : l.attribs) {
        har |= 1u << a.location;
        if (gfx::attribInteger(a.format))
            heltal |= 1u << a.location;
    }
    return (p.inputs() & ~har) == 0 &&
           ((heltal ^ p.integerInputs()) & p.inputs()) == 0;
}

// GlazeUser ur stilens tal: float4-packat och utfyllt med nollor till
// programmets storlek; tal utöver den ryms inte och kastas.
std::vector<float> userBlock(const std::vector<float>& uniforms,
                             std::uint32_t bytes)
{
    std::vector<float> u(bytes / sizeof(float), 0.0f);
    std::copy_n(uniforms.begin(), std::min(uniforms.size(), u.size()), u.begin());
    return u;
}

// Hur ett eget program binds: klart att rita, vänta (en textur väntar på
// bildcachens budget — nästa frame) eller reserven (loggad).
enum class Bound : std::uint8_t { Ready, Wait, Fallback };

// Det egna programmet `name` för en ström med layouten `l`: programmet,
// texturerna och GlazeUser i `b`.
Bound bindCustom(const PaintContext& f, const SceneData& sc,
                 const std::string& name, const gfx::VertexLayout& l,
                 const std::vector<float>& uniforms,
                 const std::vector<ImageSource>& textures, MeshBinding& b)
{
    const gfx::ShaderProgram* prog = customProgram(f, sc, name);
    if (!prog)
        return Bound::Fallback;
    if (!layoutFits(l, *prog)) {
        if (firstFallback(sc, name))
            gfx::log("Canvas: programmet '%s' läser en ström som formen saknar "
                     "(platskonventionen, gfx/program.h) — det inbyggda "
                     "programmet ritar formen; loggas en gång per rityta och "
                     "namn",
                     name.c_str());
        return Bound::Fallback;
    }
    const auto behöver = static_cast<std::size_t>(prog->textures());
    if (textures.size() < behöver) {
        if (firstFallback(sc, name))
            gfx::log("Canvas: programmet '%s' samplar %zu texturer men stilen "
                     "ger %zu — det inbyggda programmet ritar formen; loggas en "
                     "gång per rityta och namn",
                     name.c_str(), behöver, textures.size());
        return Bound::Fallback;
    }
    for (std::size_t i = 0; i < behöver; ++i) {
        b.textures[i] = imageOf(f, textures[i]);
        if (!b.textures[i])
            return Bound::Wait;   // väntar på bildcachens budget, eller ogiltig
    }
    b.program   = prog;
    b.layout    = &l;
    b.custom    = userBlock(uniforms, prog->userBytes());
    b.user      = b.custom.empty() ? nullptr : b.custom.data();
    b.userBytes = prog->userBytes();
    return Bound::Ready;
}

// En affin 3 × 4 som fasadens 4 × 4 (kolumnordning, gfx/mat4.h).
gfx::Mat4d toMat4(const Affine3& a)
{
    gfx::Mat4d m;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c)
            m.m[c * 4 + r] = a.m[r][c];
        m.m[12 + r] = a.t[r];
    }
    m.m[15] = 1.0;
    return m;
}

bool bindMesh(const Pass& p, const ObjectResidency::GpuMesh& g,
              const MeshStyle& st, const Affine3& model, bool instanced,
              MeshBinding& b, const DrapeView* drape = nullptr,
              const Point3& origin = {})
{
    MeshVariant v;
    v.normals             = g.streams.normals;
    v.colors              = g.streams.colors;
    const gfx::Image* tex = nullptr;
    if (g.streams.uvs && st.texture.kind() != ImageSource::Kind::None) {
        tex = imageOf(p.f, st.texture);
        if (!tex)
            return false;   // ogiltig, eller väntar på bildcachens budget
        v.uvs = true;
    }
    const gfx::Image* ramp = nullptr;
    if (g.streams.scalars && !st.ramp.stops.empty()) {
        ramp = p.f.res->ramp(st.ramp.stops);
        if (!ramp)
            return false;
        v.scalars = true;
    }
    v.instanced   = instanced;
    v.screenSized = st.size.value > 0.0f;
    v.draped      = drape && drape->image;   // terrängen
    b.program     = p.res.meshProgram(v);
    if (!b.program)
        return false;   // backenden har loggat felet
    b.layout = instanced ? &g.layoutInstanced : &g.layout;
    meshUniforms(st, p.light, model, b.u);
    b.user      = &b.u;
    b.userBytes = sizeof(MeshUniforms);
    const ProgramSlots slots = meshSlots(v);
    if (tex) {
        setTexture(b.u, *tex);
        b.textures[static_cast<std::size_t>(slots.texture)] = tex;
    }
    if (ramp) {
        setRamp(b.u, st.ramp.min, st.ramp.max, *ramp);
        b.textures[static_cast<std::size_t>(slots.ramp)] = ramp;
    }
    if (v.draped) {
        // DRAPERINGEN: lådan i anropets chunk-lokala koordinater (världen
        // minus origot, i double) — programmet räknar uv ur fragmentets läge.
        setDrape(b.u, *drape->image, drape->minX - origin.x,
                 drape->minY - origin.y, drape->maxX - origin.x,
                 drape->maxY - origin.y, drape->northUp);
        b.textures[static_cast<std::size_t>(slots.drape)] = drape->image;
    }
    return true;
}

// Ett rit-anrop per segment för elementen [lo, hi) — instansierat när
// `instCount` > 0.
void drawSegments(const Pass& p, const ObjectResidency::GpuMesh& g,
                  const MeshBinding& b, const gfx::FrameUniforms& frame,
                  std::uint32_t lo, std::uint32_t hi,
                  const gfx::GpuBuffer* instBuf, std::uint32_t firstInst,
                  std::uint32_t instCount)
{
    const std::size_t stride = g.layout.stride[0];
    for (const ObjectResidency::GpuMesh::Segment& s : g.segments) {
        const std::uint32_t a = std::max(lo, s.firstElement);
        const std::uint32_t e = std::min(hi, s.firstElement + s.elementCount);
        if (a >= e || !s.vb)
            continue;
        const std::uint32_t first = a - s.firstElement, n = e - a;
        gfx::MeshDraw       d;
        d.program = gfx::MeshProgram::Custom;
        d.shader  = b.program;
        d.layout  = b.layout;
        if (g.indexed) {
            d.vertices   = gfx::slice(*s.vb);
            d.indices    = gfx::BufferSlice{s.ib.get(), first * sizeof(std::uint32_t),
                                            n * sizeof(std::uint32_t)};
            d.indexCount = n;
        } else {
            d.vertices    = gfx::BufferSlice{s.vb.get(), first * stride, n * stride};
            d.vertexCount = n;
        }
        if (instCount > 0) {
            d.instances = gfx::BufferSlice{instBuf, firstInst * sizeof(MeshInstance),
                                           instCount * sizeof(MeshInstance)};
            d.instanceCount = instCount;
        }
        d.frame     = frame;
        d.user      = b.user;
        d.userBytes = b.userBytes;
        d.textures  = b.textures;
        d.state     = p.state;
        p.f.c->r->drawMesh(d);
    }
}

// GlazeFrame för ett EGET program på en mesh (scene3d.h): placeringens
// vridning och skala (`model`, utan förflyttning — anropets origo bär den)
// invikta i matriserna, och ögat, strålbasen och strålens mitt i meshens
// lokala ram. En singulär modell (en skala 0) får nollmatriser och ritar
// ingenting.
gfx::FrameUniforms customFrame(const Pass& p, const Point3& origin,
                               const Affine3& model)
{
    gfx::FrameUniforms fr = frameAt(p, origin);
    Affine3            inv;
    if (!invert(model, inv)) {
        std::fill(std::begin(fr.viewProj), std::end(fr.viewProj), 0.0f);
        return fr;
    }
    const gfx::Mat4d vp = gfx::viewProjection(p.view, origin.x, origin.y, origin.z);
    (vp * toMat4(model)).toFloat(fr.viewProj);
    gfx::Mat4d vpInv;
    if (vp.inverse(vpInv))
        (toMat4(inv) * vpInv).toFloat(fr.invViewProj);
    // Ögat, strålbasen och strålens mitt genom inversen — utan förflyttning,
    // så ett läge och en riktning tar samma väg.
    for (float* v : {fr.eye, fr.forward, fr.right, fr.up, fr.rayCenter}) {
        const Vec3 l = inv.applyDir(Vec3{v[0], v[1], v[2]});
        v[0]         = static_cast<float>(l.x);
        v[1]         = static_cast<float>(l.y);
        v[2]         = static_cast<float>(l.z);
    }
    return fr;
}

// ---- EN MESH (också primitiven och ytan i rymden) — på terräng med sin
// drapering (formplatsens id i `p.drapes`)
void drawMeshObject(const Pass& p, const Shape& s,
                    const ObjectResidency::GpuMesh& g, std::uint64_t entryId)
{
    const Placement& pl = s.placement();
    const MeshStyle& st = s.meshStyle();
    // RTC: anropets origo i double, rotationen och skalan i float.
    const Point3  origin{g.origin.x + pl.at.x, g.origin.y + pl.at.y,
                        g.origin.z + pl.at.z};
    const Affine3 model = rotationScale(pl.yaw, pl.pitch, pl.roll, pl.scale);
    const Affine3 placed =
        rotationScale(pl.yaw, pl.pitch, pl.roll, pl.scale, origin.x, origin.y,
                      origin.z);
    const bool screen = st.size.value > 0.0f;
    Box3       box{origin, origin};
    if (screen) {
        // Skärmkonstant: origot med meshens räckvidd i px.
        const double räckvidd = pxOf(p.f, st.size) * localRadius(g.localBox) *
                                std::max({std::abs(double(pl.scale.x)),
                                          std::abs(double(pl.scale.y)),
                                          std::abs(double(pl.scale.z))});
        if (!boxVisible3(p.proj, box, räckvidd + kCullSlackPx))
            return;
    } else {
        if (!placedBoxVisible(p.proj, placed, g.localBox, kCullSlackPx))
            return;
        box = transformBox(placed, g.localBox);
    }
    std::uint32_t lo = 0, hi = 0;
    if (!drawnRange(g, st.range, distanceToBox(toPoint(p.view.eye), box), lo, hi))
        return;
    MeshBinding b;
    if (!st.program.empty()) {
        // Ett EGET program — annars reserven nedan.
        switch (bindCustom(p.f, p.scene, st.program, g.layout, st.uniforms,
                           st.textures, b)) {
        case Bound::Ready:
            drawSegments(p, g, b, customFrame(p, origin, model), lo, hi, nullptr,
                         0, 0);
            return;
        case Bound::Wait:
            return;
        case Bound::Fallback:
            b = MeshBinding{};
            break;
        }
    }
    const DrapeView* drape = p.drapes ? p.drapes->find(entryId) : nullptr;
    if (!bindMesh(p, g, st, model, false, b, drape, origin))
        return;
    drawSegments(p, g, b, frameAt(p, origin), lo, hi, nullptr, 0, 0);
}

// ---- INSTANSERNA: cell för cell, sammanslagna per block och nivå
void drawInstanceObject(const Pass& p, const Shape& s,
                        const ObjectResidency::GpuMesh&      g,
                        const ObjectResidency::GpuInstances& gi)
{
    const InstanceSet* set = s.instanceSet().get();
    if (!set || set->rows.empty() || !gi.buf)
        return;
    const Placement& pl = s.placement();
    const MeshStyle& st = s.meshStyle();
    // Egna program ritar inte instanser (scene3d.h): prototypens placering
    // bor i det inbyggda programmets block.
    if (!st.program.empty() && firstFallback(p.scene, st.program))
        gfx::log("Canvas: programmet '%s' på instanser — egna program ritar "
                 "inte instanser (prototypens placering bor i det inbyggda "
                 "programmets block); det inbyggda programmet ritar dem; loggas "
                 "en gång per rityta och namn",
                 st.program.c_str());
    // Prototypens ram i instansens: placeringen med origot som förskjutning
    // (instances.h) — liten, så float räcker.
    const Affine3 model =
        rotationScale(pl.yaw, pl.pitch, pl.roll, pl.scale, g.origin.x + pl.at.x,
                      g.origin.y + pl.at.y, g.origin.z + pl.at.z);
    MeshBinding b;
    if (!bindMesh(p, g, st, model, true, b))
        return;
    const bool   screen = st.size.value > 0.0f;
    const double räckvidd =
        screen ? pxOf(p.f, st.size) * localRadius(transformBox(model, g.localBox)) *
                     set->maxScale
               : 0.0;
    const Point3 öga = toPoint(p.view.eye);

    // En följd celler i samma block och på samma nivå blir ETT anrop.
    struct Följd {
        std::uint32_t first = 0, count = 0, block = 0;
        std::uint32_t lo = 0, hi = 0;
        bool          open = false;
    } följd;
    const auto rita = [&] {
        if (följd.open && följd.count > 0)
            drawSegments(p, g, b, frameAt(p, set->blocks[följd.block]), följd.lo,
                         följd.hi, gi.buf.get(), följd.first, följd.count);
        följd.open = false;
    };
    for (const InstanceCell& c : set->cells) {
        const Box3& låda   = screen ? c.anchors : c.box;
        const bool  synlig = boxVisible3(p.proj, låda, räckvidd + kCullSlackPx);
        std::uint32_t lo = 0, hi = 0;
        if (!synlig || !drawnRange(g, st.range, distanceToBox(öga, låda), lo, hi)) {
            rita();
            continue;
        }
        if (följd.open && följd.block == c.block && följd.lo == lo &&
            följd.hi == hi && följd.first + följd.count == c.first) {
            följd.count += c.count;
            continue;
        }
        rita();
        följd = Följd{c.first, c.count, c.block, lo, hi, true};
    }
    rita();
}

// ---- PUNKTERNA: chunk för chunk
void drawPointObject(const Pass& p, const Shape& s,
                     const ObjectResidency::GpuCloud& gc)
{
    const std::shared_ptr<PointCloudData>& d = MeshAccess::data(s.pointCloud());
    if (!d || d->chunks.size() != gc.chunks.size())
        return;
    const PointStyle& st = s.pointStyle();
    if (!(st.size.value > 0.0f) || !std::isfinite(st.size.value))
        return;
    // Programmet och dess block: ett EGET (inga texturer) eller det
    // inbyggda — också som reserven.
    MeshBinding b;
    Bound       bunden = Bound::Fallback;
    if (!st.program.empty())
        bunden = bindCustom(p.f, p.scene, st.program, gc.layout, st.uniforms, {}, b);
    if (bunden == Bound::Wait)
        return;
    PointUniforms u;
    if (bunden == Bound::Fallback) {
        b = MeshBinding{};
        PointVariant v;
        v.colors               = gc.streams.colors;
        const gfx::Image* ramp = nullptr;
        if (gc.streams.scalars && !st.ramp.stops.empty()) {
            ramp = p.f.res->ramp(st.ramp.stops);
            if (!ramp)
                return;
            v.scalars = true;
        }
        b.program = p.res.pointProgram(v);
        if (!b.program)
            return;
        setColor(u, st.color);
        if (st.fog)
            setFog(u, p.light.fog, p.light.fogStart, p.light.fogEnd,
                   p.light.fogPower);
        setPointSize(u, st.size.value, st.size.unit, st.shape);
        if (ramp) {
            setRamp(u, st.ramp.min, st.ramp.max, *ramp);
            b.textures[static_cast<std::size_t>(pointSlots(v).ramp)] = ramp;
        }
        b.user      = &u;
        b.userBytes = sizeof(PointUniforms);
    }
    // Gallringens räckvidd: halva diametern — på skärmen i px, i världen på
    // lådan.
    const double halvPx = 0.5 * pxOf(p.f, st.size) + kCullSlackPx;
    const double halvV  = st.size.unit == Unit::World ? 0.5 * st.size.value : 0.0;
    for (std::size_t i = 0; i < d->chunks.size(); ++i) {
        const PointChunk& c = d->chunks[i];
        if (!gc.chunks[i] || c.count == 0)
            continue;
        const Box3 låda{{c.bounds.min.x - halvV, c.bounds.min.y - halvV,
                         c.bounds.min.z - halvV},
                        {c.bounds.max.x + halvV, c.bounds.max.y + halvV,
                         c.bounds.max.z + halvV}};
        if (!boxVisible3(p.proj, låda, halvPx))
            continue;
        gfx::MeshDraw dr;
        dr.program       = gfx::MeshProgram::Custom;
        dr.shader        = b.program;
        dr.layout        = &gc.layout;
        dr.vertexCount   = kPointQuadVertices;
        dr.instances     = gfx::slice(*gc.chunks[i]);
        dr.instanceCount = c.count;
        dr.frame         = frameAt(p, c.origin);
        dr.user          = b.user;
        dr.userBytes     = b.userBytes;
        dr.textures      = b.textures;
        dr.state         = p.state;
        p.f.c->r->drawMesh(dr);
    }
}

// Kopiorna residenta — i förberedelsen, och i 2D-vägens ritning.
ObjectResidency::Shown resident(const PaintContext& f,
                                const std::shared_ptr<ShapeEntry>& e, bool upload)
{
    return f.res->objects().object(e, f.c->images, upload);
}

} // namespace

void meshUniforms(const MeshStyle& st, const Lighting& light,
                  const Affine3& model, MeshUniforms& u)
{
    float rows[12];
    model.toRows(rows);
    setModel(u, rows);
    setColor(u, st.color);
    if (st.lit) {
        setLight(u, light.direction, light.ambient, light.diffuse,
                 light.exponent, light.twoSided);
        setRim(u, light.rimDirection, light.rim);
    } else {
        setLit(u, false);
    }
    if (st.fog)
        setFog(u, light.fog, light.fogStart, light.fogEnd, light.fogPower);
    u.surface[0] = unit01(st.opacity, 1.0f);
    // Färgjusteringen — Image-programmets vektor och regel.
    u.adjust[0] = gfx::adjustFactor(st.saturation);
    u.adjust[1] = gfx::adjustFactor(st.contrast);
    u.adjust[2] = gfx::adjustFactor(st.brightness);
    if (st.size.value > 0.0f && std::isfinite(st.size.value))
        setScreenSize(u, st.size.value, st.size.unit);
}

void queueObjects(const PaintContext& f, const RunData& run, bool draw,
                  std::vector<UploadItem>& visible, std::vector<UploadItem>& rest)
{
    // Ritningens vy och gallring (`drawObjects`) — ett lager som inte ritas
    // syns inte.
    const gfx::View3D view = objectView(f, run);
    const Projection  proj(view);
    const Point3      öga = toPoint(view.eye);
    for (const std::shared_ptr<ShapeEntry>& e : run.objects) {
        if (!e->alive)
            continue;
        const std::optional<Box3>& b = objectBox(*e);
        if (draw && objectInView(proj, *e, b))
            visible.push_back(UploadItem{b ? distanceToBox(öga, *b) : 0.0, &e,
                                         nullptr, true});
        else
            rest.push_back(UploadItem{0.0, &e, nullptr, draw});
    }
}

void prepareObject(const PaintContext& f, const SceneData& sc,
                   const std::shared_ptr<ShapeEntry>& e, bool draw)
{
    f.res->objects().precompile();   // en gång per fönster — de vanligaste varianterna
    const ObjectResidency::Shown sh = resident(f, e, true);
    if (!draw || !sh.shape)
        return;
    // Det ritningen binder och som laddar upp eller kompilerar: texturen,
    // rampen och ett eget program med dess texturer — det
    // inbyggda kan bli reserven, så dess delar hämtas ändå.
    const Shape& s = *sh.shape;
    if (s.kind() == Shape::Kind::Points3) {
        const PointStyle& ps = s.pointStyle();
        if (!ps.ramp.stops.empty())
            f.res->ramp(ps.ramp.stops);
        if (!ps.program.empty())
            customProgram(f, sc, ps.program);
        return;
    }
    const MeshStyle& st = s.meshStyle();
    if (st.texture.kind() != ImageSource::Kind::None)
        imageOf(f, st.texture);
    if (!st.ramp.stops.empty())
        f.res->ramp(st.ramp.stops);
    if (!st.program.empty() && s.kind() != Shape::Kind::Instances)
        if (const gfx::ShaderProgram* prog = customProgram(f, sc, st.program))
            for (std::size_t i = 0;
                 i < st.textures.size() && i < static_cast<std::size_t>(prog->textures());
                 ++i)
                imageOf(f, st.textures[i]);
}

void prepareScreenPass(const PaintContext& f, const SceneData& sc, const Shape& s)
{
    const PassStyle& ps = s.passStyle();
    if (const gfx::ShaderProgram* prog = customProgram(f, sc, s.passProgram()))
        for (std::size_t i = 0;
             i < ps.textures.size() && i < static_cast<std::size_t>(prog->textures());
             ++i)
            imageOf(f, ps.textures[i]);
}

void drawScreenPass(const PaintContext& f, const SceneData& sc, const Shape& s,
                    float op, const gfx::RenderState& state)
{
    if (!f.view3D)
        return;   // passen ritas bara i 3D-målet (canvas.h)
    // Helskärmspasset läser ingen ström — programmet gör sina tre vertex.
    static const gfx::VertexLayout ingen;
    const PassStyle& ps = s.passStyle();
    MeshBinding      b;
    const Bound      bunden =
        bindCustom(f, sc, s.passProgram(), ingen, ps.uniforms, ps.textures, b);
    if (bunden == Bound::Wait)
        return;
    if (bunden == Bound::Fallback) {
        // RESERVEN: ritytans ruta i reservfärgen, med lagrets opacitet — alfa
        // 0 (förvalet) ritar ingenting.
        gfx::Color c = ps.fallback;
        c.a = static_cast<std::uint8_t>(std::lround(c.a * std::clamp(op, 0.0f, 1.0f)));
        if (c.a > 0)
            f.c->r->fillRect(f.world.box, c);
        return;
    }
    // GlazeFrame med origot i ögat — strålbasen, matriserna och skalan.
    const gfx::View3D& v  = *f.view3D;
    gfx::FrameUniforms fr = gfx::frameUniforms(v, v.eye.x, v.eye.y, v.eye.z);
    fr.scale[0] = f.c->pxPerMm;
    fr.scale[1] = f.c->scale;
    fr.scale[2] = op;
    fr.scale[3] = 0.0f;
    gfx::MeshDraw d;
    d.program     = gfx::MeshProgram::Custom;
    d.shader      = b.program;
    d.layout      = &ingen;
    d.vertexCount = kFullScreenVertices;
    d.frame       = fr;
    d.user        = b.user;
    d.userBytes   = b.userBytes;
    d.textures    = b.textures;
    d.state       = state;
    f.c->r->drawMesh(d);
}

void drawObjects(const PaintContext& f, const SceneData& sc, const LayerFrame& lf,
                 const RunData& run, float op, bool draw,
                 const gfx::RenderState& state)
{
    // I 3D-målet är allt redan uppladdat (förberedelsen); 2D-vägen
    // laddar upp de SYNLIGA i ritningen, som chunkarna där, och resten efter
    // den med det som blir kvar av budgeten (scene.h `prepareUploads`).
    const bool upload = !f.view3D;
    if (!draw)
        return;
    const gfx::View3D view = objectView(f, run);
    const Projection  proj(view);
    const Lighting    förval;
    const Pass        p{f,  view, proj, f.lighting ? *f.lighting : förval, state,
                 op, f.res->objects(), sc, lf.drapes};
    for (const std::shared_ptr<ShapeEntry>& e : run.objects) {
        if (!e->alive)
            continue;
        const ObjectResidency::Shown sh =
            resident(f, e, upload && objectInView(proj, *e, objectBox(*e)));
        if (!sh.shape)
            continue;   // en ny plats vars data inte är uppe än
        switch (sh.shape->kind()) {
        case Shape::Kind::Mesh:
        case Shape::Kind::Area3:
            if (sh.mesh)
                drawMeshObject(p, *sh.shape, *sh.mesh, e->id);
            break;
        case Shape::Kind::Instances:
            if (sh.mesh && sh.inst)
                drawInstanceObject(p, *sh.shape, *sh.mesh, *sh.inst);
            break;
        case Shape::Kind::Points3:
            if (sh.cloud)
                drawPointObject(p, *sh.shape, *sh.cloud);
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
}

} // namespace glaze::detail
