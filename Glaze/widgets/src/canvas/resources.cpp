// CanvasResources — ritytans GPU-resurser per fönster.
// Kontraktet och livstiden står i resources.h; chunkarna och körningarna
// (det som synkas hit) i scene.h.

#include "resources.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <numbers>
#include <type_traits>
#include <utility>

#include <gfx/geom2d.h>
#include <gfx/platform.h>

#include "../profiler_hooks.h"
#include "build.h"
#include "drape.h"
#include "residency.h"

namespace glaze {

namespace {

// En ny buffert får minst så mycket plats, och växer sedan i tvåpotenser —
// ett lager som byggs form för form laddar då upp svansen i stället för att
// skapa en ny buffert varje gång.
constexpr std::size_t kMinBufferBytes = 4096;

// Atlasernas sammanlagda tak i sidor (en sida är 1 MiB R8). Över taket töms
// alla atlaser i nästa `trim` och texten byggs om — en sida räcker till
// ~400 glyfer, så taket tar en stor flerspråkig karta innan det slår till.
constexpr std::size_t kMaxAtlasPages = 32;

// Rampernas tak: en gradient är 1 KiB, och en `onPaint` som byter stopp
// varje ritning ska inte kunna fylla minnet.
constexpr std::size_t kMaxRamps = 256;

// Grader (stilarnas enhet) till radianer — etikettens vinkel räknas i double,
// som Glyph-programmets CPU-spegel (labels.h).
double radianer(float grader)
{
    return static_cast<double>(grader) * std::numbers::pi / 180.0;
}

std::string rampKey(const std::vector<GradientStop>& stops)
{
    std::string k;
    k.reserve(stops.size() * (sizeof(float) + 4));
    for (const GradientStop& s : stops) {
        char buf[sizeof(float)];
        std::memcpy(buf, &s.offset, sizeof buf);
        k.append(buf, sizeof buf);
        k.push_back(static_cast<char>(s.color.r));
        k.push_back(static_cast<char>(s.color.g));
        k.push_back(static_cast<char>(s.color.b));
        k.push_back(static_cast<char>(s.color.a));
    }
    return k;
}

} // namespace

CanvasResources::CanvasResources(gfx::Renderer& r)
    : r_(&r), objects_(std::make_unique<detail::ObjectResidency>(r)),
      drapes_(std::make_unique<detail::DrapeCache>(r))
{
}

CanvasResources::~CanvasResources() = default;

// ------------------------------------------------------------- framens början

void CanvasResources::trim()
{
    retiredBuffers_.clear();
    retiredBuilds_.clear();
    retiredImages_.clear();
    retiredGrids_.clear();
    retiredTargets_.clear();
    objects_->trim();   // föremålen (residency.h)
    drapes_->trim();    // draperingarna (drape.h)

    // DÖTT: chunkar och körningar som byggts om eller vars rityta rivits.
    for (auto it = chunks_.begin(); it != chunks_.end();)
        it = it->second.src.expired() ? chunks_.erase(it) : std::next(it);
    for (auto it = glyphs_.begin(); it != glyphs_.end();)
        it = it->second.src.expired() ? glyphs_.erase(it) : std::next(it);
    // …och ritytor som rivits: deras placering och synlighetsbild — och
    // deras 3D-mål.
    for (auto it = labels_.begin(); it != labels_.end();)
        it = it->second.src.expired() ? labels_.erase(it) : std::next(it);
    for (auto it = targets_.begin(); it != targets_.end();)
        it = it->second.src.expired() ? targets_.erase(it) : std::next(it);
    // …och egna program som ersatts, tagits bort eller vars rityta rivits.
    for (auto it = programs_.begin(); it != programs_.end();)
        it = it->second.life.expired() ? programs_.erase(it) : std::next(it);

    // ATLASERNAS TAK: allt som byggts mot dem pekar på sidor och UV:er, så
    // varje textbygge blir inaktuellt (epoken) och byggs om när det ritas.
    std::size_t pages = 0;
    for (const auto& a : atlases_)
        if (a)
            pages += a->pageCount();
    if (pages > kMaxAtlasPages) {
        if (!atlasLogged_)
            gfx::log("CanvasResources: ritytans atlaser tar %zu sidor (tak %zu) "
                     "— de töms och texten byggs om", pages, kMaxAtlasPages);
        atlasLogged_ = true;
        for (auto& a : atlases_)
            if (a)
                a->clear();
        ++atlasEpoch_;
        for (auto& [id, e] : glyphs_)
            e.build = GlyphBuild{};
    }
}

// ------------------------------------------------------------------ chunkarna

bool CanvasResources::reserveAndUpload(std::unique_ptr<gfx::GpuBuffer>& buf,
                                       std::size_t& cap, gfx::BufferKind kind,
                                       const void* data, std::size_t bytes)
{
    if (!buf || !buf->valid() || cap < bytes) {
        std::size_t c = kMinBufferBytes;
        while (c < bytes)
            c *= 2;
        if (buf)
            retiredBuffers_.push_back(std::move(buf));
        buf = r_->createBuffer(kind, c, nullptr);
        cap = buf && buf->valid() ? c : 0;
        if (cap == 0)
            return false;
    }
    return r_->updateBuffer(*buf, 0, data, bytes);
}

const CanvasResources::GpuChunk*
CanvasResources::sync(const std::shared_ptr<detail::ChunkData>& chp)
{
    const detail::ChunkData& ch     = *chp;
    const std::size_t        vs     = ch.vertexSize();
    const std::size_t        vbytes = ch.vertexCount() * vs;
    const std::size_t        ibytes = ch.indices.size() * sizeof(std::uint32_t);
    if (vbytes == 0 || ibytes == 0)
        return nullptr;

    auto [it, fresh] = chunks_.try_emplace(ch.id);
    ChunkEntry& e    = it->second;
    if (fresh)
        e.src = chp;
    if (!fresh && e.version == ch.version && e.gpu.vb && e.gpu.ib)
        return &e.gpu;

    // IKAPP MED LOGGEN när den räcker dit, annars hela chunken. En ändring
    // som inte ryms i bufferten (svansen växte förbi kapaciteten) gör också
    // hela uppladdningen — med en större buffert.
    bool full = fresh || !e.gpu.vb || !e.gpu.ib || e.version < ch.base;
    if (!full) {
        const auto* vdata = static_cast<const std::uint8_t*>(ch.vertexData());
        const auto* idata =
            reinterpret_cast<const std::uint8_t*>(ch.indices.data());
        for (const detail::ChunkData::Change& c : ch.changes) {
            if (c.version <= e.version)
                continue;
            const std::size_t vo = c.vtxFrom * vs, vn = c.vtxCount * vs;
            const std::size_t io = c.idxFrom * sizeof(std::uint32_t);
            const std::size_t in = c.idxCount * sizeof(std::uint32_t);
            if ((vn > 0 && (vo + vn > e.gpu.vbCap ||
                            !r_->updateBuffer(*e.gpu.vb, vo, vdata + vo, vn))) ||
                (in > 0 && (io + in > e.gpu.ibCap ||
                            !r_->updateBuffer(*e.gpu.ib, io, idata + io, in)))) {
                full = true;
                break;
            }
        }
    }
    if (full &&
        (!reserveAndUpload(e.gpu.vb, e.gpu.vbCap, gfx::BufferKind::Vertex,
                           ch.vertexData(), vbytes) ||
         !reserveAndUpload(e.gpu.ib, e.gpu.ibCap, gfx::BufferKind::Index,
                           ch.indices.data(), ibytes))) {
        e.version = 0;   // försöks igen nästa ritning
        return nullptr;
    }
    e.version = ch.version;
    return &e.gpu;
}

// ---------------------------------------------------------------------- texten

const CanvasResources::GlyphBuild&
CanvasResources::glyphs(const std::shared_ptr<detail::RunData>& run,
                        const GlyphEnv& env)
{
    auto [it, fresh] = glyphs_.try_emplace(run->id);
    GlyphEntry& e    = it->second;
    if (fresh)
        e.src = run;
    // Inaktuellt: körningen ändrad, tätheten ändrad (mm ↔ px räknas om),
    // atlasen tömd eller terrängen under etiketterna ändrad (regionen under
    // körningens ankarlåda, inte hela terrängens version: en ny platta någon
    // annanstans bygger inte om).
    // Världsenhetens skala är INTE ett skäl — det hade gjort varje zoom till
    // en uppladdning (se TextStyle).
    const bool påTerräng = env.terrain != nullptr;
    if (fresh || e.version != run->glyphVersion || e.pxPerPx != env.pxPerPx ||
        e.pxPerMm != env.pxPerMm || e.atlasEpoch != atlasEpoch_ ||
        e.onTerrain != påTerräng || (påTerräng && e.terrain != env.terrainKey)) {
        retiredBuilds_.push_back(std::move(e.build));
        e.build = GlyphBuild{};
        buildGlyphs(*run, env, e.build);
        e.version    = run->glyphVersion;
        e.pxPerPx    = env.pxPerPx;
        e.pxPerMm    = env.pxPerMm;
        e.atlasEpoch = atlasEpoch_;
        e.onTerrain  = påTerräng;
        e.terrain    = påTerräng ? env.terrainKey : 0;
        e.build.serial = ++glyphBuildsMade_;
        detail::profileCount(ProfileCount::GlyphBuilds);
    }
    return e.build;
}

const CanvasResources::GlyphBuild*
CanvasResources::findGlyphs(const detail::RunData& run) const
{
    const auto it = glyphs_.find(run.id);
    if (it == glyphs_.end() || it->second.version != run.glyphVersion ||
        it->second.atlasEpoch != atlasEpoch_)
        return nullptr;
    return &it->second.build;
}

void CanvasResources::buildGlyphs(const detail::RunData& run,
                                  const GlyphEnv& env, GlyphBuild& out)
{
    gfx::SdfAtlas* at = nullptr;
    if (run.key.font != 0) {
        at = atlas(detail::fontFamilyOf(run.key.font));
        if (!at || !at->valid())
            return;   // ingen font — loggat av atlasen/fonten
    }
    const detail::GlyphUnits u{run.key.unit, env.pxPerPx, env.pxPerMm,
                               env.pxPerWorldUnit};

    // GRUPPERNA ger precisionen (scene.h): Strict i formernas ordning, en ny
    // grupp när en etikett ligger för långt från gruppens origo (i rymden
    // också i z); ByKind i rutor. En grupp är byggarens chunkvektor — dess
    // chunkar har origo i en av gruppens etiketter. En Glyph3-körning bygger
    // i rymdens chunkar (`chunks3`).
    struct Group {
        std::vector<gfx::GlyphChunk>  chunks;
        std::vector<gfx::GlyphChunk3> chunks3;
        double                        ox = 0.0, oy = 0.0, oz = 0.0;
    };
    std::vector<Group>                                     groups;
    std::map<std::pair<long long, long long>, std::size_t> cells;
    const auto groupFor = [&](double x0, double y0, double x1, double y1,
                              double z0, double z1) -> Group& {
        if (run.order == DrawOrder::ByKind) {
            const auto key = std::make_pair(
                static_cast<long long>(std::floor((x0 + x1) / 2.0 / detail::kChunkCell)),
                static_cast<long long>(std::floor((y0 + y1) / 2.0 / detail::kChunkCell)));
            auto [cit, ny] = cells.try_emplace(key, groups.size());
            if (ny)
                groups.push_back(Group{{}, {}, x0, y0, z0});
            return groups[cit->second];
        }
        if (!groups.empty()) {
            const Group& g = groups.back();
            if (std::max(std::abs(x0 - g.ox), std::abs(x1 - g.ox)) <= detail::kChunkCell &&
                std::max(std::abs(y0 - g.oy), std::abs(y1 - g.oy)) <= detail::kChunkCell &&
                std::max(std::abs(z0 - g.oz), std::abs(z1 - g.oz)) <= detail::kChunkCell)
                return groups.back();
        }
        groups.push_back(Group{{}, {}, x0, y0, z0});
        return groups.back();
    };

    std::uint32_t       label = 0;
    std::vector<double> höjder;   // formens punkters markhöjd (återanvänd)
    for (std::size_t k = 0; k < run.glyphShapes.size(); ++k) {
        const std::shared_ptr<detail::ShapeEntry>& ep = run.glyphShapes[k];
        if (!ep->alive)
            continue;
        const auto shape = static_cast<std::uint32_t>(k);
        const Shape& s = ep->shape;
        double       x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0, z0 = 0.0, z1 = 0.0;
        if (!detail::shapeBounds(s, x0, y0, x1, y1))
            continue;
        detail::shapeZRange(s, z0, z1);
        // VID MARKHÖJD: en plan etikett på terräng får
        // terrängens höjd under sina punkter och byggs i rymdens chunkar —
        // varje punkts höjd frågas EN gång, och lådans z,
        // ankaret och markörerna läser samma.
        höjder.clear();
        if (env.terrain)
            for (const Point& q : s.points())
                höjder.push_back(
                    detail::terrainHeight(*env.terrain, q.x, q.y).value_or(0.0));
        const auto markhöjd = [&höjder](std::size_t i) {
            return i < höjder.size() ? höjder[i] : 0.0;
        };
        if (!höjder.empty()) {
            const auto [lägst, högst] = std::minmax_element(höjder.begin(), höjder.end());
            z0                        = *lägst;
            z1                        = *högst;
        }
        Group& g = groupFor(x0, y0, x1, y1, z0, z1);
        const bool rymd = s.kind() == Shape::Kind::Text3 ||
                          s.kind() == Shape::Kind::Markers3 || env.terrain;
        if ((s.kind() == Shape::Kind::Text || s.kind() == Shape::Kind::Text3) &&
            at) {
            const TextStyle&           ts = s.textStyle();
            const gfx::TextLayoutStyle ls = detail::textLayout(ts, u, label);
            // Delarna (en vanlig text är EN) — strängarna lånas ur formen,
            // som lever genom bygget; ett språkbyte bygger om körningen.
            const std::vector<gfx::StyledText> parts = detail::textParts(s, u);
            Point                      p{};
            double                     z = 0.0;
            gfx::TextBox               box;
            if (rymd) {
                const Point3 q = s.kind() == Shape::Kind::Text3
                                     ? s.points3().front()
                                     : Point3{s.points().front().x,
                                              s.points().front().y, markhöjd(0)};
                p   = Point{q.x, q.y};
                z   = q.z;
                box = gfx::buildText(*at, parts, gfx::Vec3d{q.x, q.y, q.z}, ls,
                                     g.chunks3);
            } else {
                p   = s.points().front();
                box = gfx::buildText(*at, parts, gfx::Point2d{p.x, p.y}, ls,
                                     g.chunks);
            }
            const double v = radianer(ts.angle);
            out.labels.push_back(LabelInfo{label, p, z, box, ls.size,
                                           std::cos(v), std::sin(v), ts.rotation,
                                           ts.collide, ts.priority,
                                           detail::toUnit(ts.collisionPadding, u),
                                           shape, 0, ts.minZoom, ts.maxZoom});
            ++label;
        } else if (s.kind() == Shape::Kind::Markers ||
                   s.kind() == Shape::Kind::Markers3) {
            const MarkerStyle& ms = s.markerStyle();
            // Punkterna i rymden — z 0 för de plana, markhöjden på terräng.
            std::vector<gfx::Vec3d> all;
            if (s.kind() == Shape::Kind::Markers3)
                for (const Point3& q : s.points3())
                    all.push_back(gfx::Vec3d{q.x, q.y, q.z});
            else
                for (std::size_t i = 0; i < s.points().size(); ++i)
                    all.push_back(gfx::Vec3d{s.points()[i].x, s.points()[i].y,
                                             markhöjd(i)});
            // Varje markör som KOLLIDERAR är en egen etikett (en texel i
            // synlighetstexturen); de andra delar en.
            const std::size_t n = ms.collide ? all.size() : 1;
            const double      v = radianer(ms.angle);
            for (std::size_t i = 0; i < n; ++i) {
                const gfx::MarkerLayoutStyle ml = detail::markerLayout(ms, u, label);
                const std::size_t first = ms.collide ? i : 0;
                const std::size_t count = ms.collide ? 1 : all.size();
                // Lådan är byggarens — konturen inräknad och ankaret pålagt.
                gfx::TextBox box;
                if (rymd) {
                    box = gfx::buildMarkers(
                        std::span<const gfx::Vec3d>(all.data() + first, count), ml,
                        g.chunks3);
                } else {
                    std::vector<gfx::Point2d> pts;
                    for (std::size_t j = first; j < first + count; ++j)
                        pts.push_back(gfx::Point2d{all[j].x, all[j].y});
                    box = gfx::buildMarkers(pts, ml, g.chunks);
                }
                out.labels.push_back(LabelInfo{label, Point{all[i].x, all[i].y},
                                               all[i].z, box, ml.size,
                                               std::cos(v), std::sin(v),
                                               ms.rotation, ms.collide, ms.priority,
                                               detail::toUnit(ms.collisionPadding, u),
                                               shape,
                                               static_cast<std::uint32_t>(i),
                                               ms.minZoom, ms.maxZoom});
                ++label;
            }
        }
    }

    // TILL GPU:n — statiska buffertar, en per chunk (ett rit-anrop binder EN
    // atlassida).
    for (Group& g : groups) {
        for (gfx::GlyphChunk& c : g.chunks)
            uploadGlyphChunk(c, at, out);
        for (gfx::GlyphChunk3& c : g.chunks3)
            uploadGlyphChunk(c, at, out);
    }
}

template <class Chunk>
void CanvasResources::uploadGlyphChunk(const Chunk& c, const gfx::SdfAtlas* at,
                                       GlyphBuild& out)
{
    if (c.indices.empty())
        return;
    using V = typename decltype(c.vertices)::value_type;
    GlyphGpu gg;
    gg.program = std::is_same_v<V, gfx::GlyphVertex3> ? gfx::MeshProgram::Glyph3
                                                      : gfx::MeshProgram::Glyph;
    gg.originX = c.originX;
    gg.originY = c.originY;
    gg.page    = c.page;
    gg.atlas   = at;
    gg.vb      = r_->createBuffer(gfx::BufferKind::Vertex,
                                  c.vertices.size() * sizeof(V),
                                  c.vertices.data());
    gg.ib      = r_->createBuffer(gfx::BufferKind::Index,
                                  c.indices.size() * sizeof(std::uint32_t),
                                  c.indices.data());
    if (!gg.vb->valid() || !gg.ib->valid())
        return;
    gg.count = static_cast<std::uint32_t>(c.indices.size());
    bool first = true;
    for (const V& v : c.vertices) {
        const double ax = c.originX + v.ax, ay = c.originY + v.ay;
        double       az = 0.0;
        if constexpr (requires { v.az; })
            az = c.originZ + v.az;
        if (first) {
            gg.minX = gg.maxX = ax;
            gg.minY = gg.maxY = ay;
            gg.minZ = gg.maxZ = az;
            first = false;
        }
        gg.minX = std::min(gg.minX, ax);
        gg.minY = std::min(gg.minY, ay);
        gg.minZ = std::min(gg.minZ, az);
        gg.maxX = std::max(gg.maxX, ax);
        gg.maxY = std::max(gg.maxY, ay);
        gg.maxZ = std::max(gg.maxZ, az);
    }
    if constexpr (requires { c.originZ; })
        gg.originZ = c.originZ;
    gg.reach   = detail::glyphReach(c);
    gg.reachEm = detail::glyphReachEm(c);
    out.chunks.push_back(std::move(gg));
}

// ---------------------------------------------------------------- etiketterna

CanvasResources::LabelState&
CanvasResources::labelState(const std::shared_ptr<detail::SceneData>& scene,
                            int slot)
{
    auto [it, fresh] = labels_.try_emplace(std::make_pair(scene->id, slot));
    if (fresh)
        it->second.src = scene;
    return it->second;
}

const CanvasResources::LabelState*
CanvasResources::findLabelState(std::uint64_t sceneId, int slot) const
{
    const auto it = labels_.find(std::make_pair(sceneId, slot));
    return it != labels_.end() ? &it->second : nullptr;
}

void CanvasResources::uploadLabels(LabelState& s, std::size_t lo, std::size_t hi)
{
    const std::size_t n = s.shown.size();
    if (n == 0)
        return;
    constexpr std::size_t w = kLabelTexWidth;
    const std::size_t rows  = (n + w - 1) / w;
    if (!s.tex || !s.tex->valid() ||
        static_cast<std::size_t>(s.tex->height()) < rows) {
        // En ny bild (nollställd) — det som ritats med den gamla i framen
        // pekar på den till nästa trim.
        if (s.tex)
            retiredImages_.push_back(std::move(s.tex));
        s.tex = r_->createImage(kLabelTexWidth, static_cast<int>(rows),
                                gfx::ImageFormat::R8, nullptr, 0);
        lo = 0;
        hi = n;
    }
    hi = std::min(hi, n);
    if (!s.tex->valid() || lo >= hi)
        return;
    // Spannet rad för rad: en bit av första raden, hela rader, en bit av
    // den sista — exakt hi − lo byte.
    const auto upload = [&](std::size_t from, std::size_t count) {
        r_->updateImage(*s.tex, static_cast<int>(from % w),
                        static_cast<int>(from / w), static_cast<int>(count), 1,
                        s.shown.data() + from, 0);
    };
    const std::size_t r0 = lo / w, r1 = (hi - 1) / w;
    if (r0 == r1) {
        upload(lo, hi - lo);
        return;
    }
    upload(lo, (r0 + 1) * w - lo);
    if (r1 > r0 + 1)
        r_->updateImage(*s.tex, 0, static_cast<int>(r0 + 1), kLabelTexWidth,
                        static_cast<int>(r1 - r0 - 1),
                        s.shown.data() + (r0 + 1) * w, w);
    upload(r1 * w, hi - r1 * w);
}

void CanvasResources::dropLabels(LabelState& s)
{
    if (s.tex)
        retiredImages_.push_back(std::move(s.tex));
}

// ------------------------------------------- atlaserna, ramperna, rutnätet

gfx::SdfAtlas* CanvasResources::atlas(gfx::FontFamily family)
{
    std::unique_ptr<gfx::SdfAtlas>& a =
        atlases_[family == gfx::FontFamily::Mono ? 1 : 0];
    if (!a)
        a = std::make_unique<gfx::SdfAtlas>(*r_, family);
    return a.get();
}

const gfx::Image* CanvasResources::ramp(const std::vector<GradientStop>& stops)
{
    const std::string key = rampKey(stops);
    for (auto it = ramps_.begin(); it != ramps_.end(); ++it)
        if (it->key == key) {
            ramps_.splice(ramps_.begin(), ramps_, it);   // senast använd först
            return ramps_.front().image.get();
        }
    gfx::GradientRamp px{};
    gfx::buildGradientRamp(stops, px);
    std::unique_ptr<gfx::Image> img = r_->createImage(
        gfx::kGradientRampSize, 1, gfx::ImageFormat::Rgba8, px.data(), 0);
    if (!img->valid())
        return nullptr;
    ramps_.push_front(RampEntry{key, std::move(img)});
    while (ramps_.size() > kMaxRamps) {
        retiredImages_.push_back(std::move(ramps_.back().image));
        ramps_.pop_back();
    }
    return ramps_.front().image.get();
}

const CanvasResources::GridMesh* CanvasResources::grid(gfx::Color color,
                                                       float      width)
{
    const auto same = [](gfx::Color a, gfx::Color b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };
    if (grid_ && same(gridColor_, color) && gridWidth_ == width)
        return grid_->vb && grid_->vb->valid() ? grid_.get() : nullptr;

    // Linjerna x = k och y = k, |k| ≤ N, var och en från −N till N — en
    // chunk med origo (0, 0) i RUTNÄTETS enhet (den förfyllda tomma chunken
    // bestämmer origot); ritningen lägger på skalan och flyttar origot till
    // kamerans närmaste rutnätspunkt.
    const int n = kGridHalfLines;
    // Två linjer per k, ett segment (fyra vertex) var — EN chunk, så hela
    // rutnätet är ett rit-anrop med ett origo.
    static_assert(2 * (2 * kGridHalfLines + 1) * 4 <= gfx::kMaxChunkVertices,
                  "rutnätet ska rymmas i en chunk");
    std::vector<gfx::LineChunk> chunks(1);
    gfx::PolylineStyle          ps;
    ps.width = width;
    ps.color = color;
    for (int k = -n; k <= n; ++k) {
        const gfx::Point2d v[2] = {{double(k), double(-n)}, {double(k), double(n)}};
        const gfx::Point2d h[2] = {{double(-n), double(k)}, {double(n), double(k)}};
        gfx::buildPolyline(v, ps, chunks);
        gfx::buildPolyline(h, ps, chunks);
    }
    if (grid_)
        retiredGrids_.push_back(std::move(grid_));
    grid_      = std::make_unique<GridMesh>();
    gridColor_ = color;
    gridWidth_ = width;
    const gfx::LineChunk& c = chunks.front();
    grid_->vb = r_->createBuffer(gfx::BufferKind::Vertex,
                                 c.vertices.size() * sizeof(gfx::LineVertex),
                                 c.vertices.data());
    grid_->ib = r_->createBuffer(gfx::BufferKind::Index,
                                 c.indices.size() * sizeof(std::uint32_t),
                                 c.indices.data());
    grid_->count = static_cast<std::uint32_t>(c.indices.size());
    if (!grid_->vb->valid() || !grid_->ib->valid())
        return nullptr;
    return grid_.get();
}

const CanvasResources::GridMesh* CanvasResources::groundPlane()
{
    if (groundPlane_)
        return groundPlane_->vb && groundPlane_->vb->valid() ? groundPlane_.get()
                                                              : nullptr;
    // Enhetsrutan som yta, i en chunk med origo (0, 0) — den förfyllda tomma
    // chunken bestämmer origot, så ritningens skala verkar kring origo.
    std::vector<gfx::FillChunk>                  chunks(1);
    const std::vector<std::vector<gfx::Point2d>> ring = {
        {{-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}}};
    gfx::PolygonStyle ps;
    ps.color = gfx::Color{255, 255, 255, 255};   // färgen skrivs aldrig (DepthOnly)
    gfx::buildPolygon(ring, ps, chunks);
    groundPlane_            = std::make_unique<GridMesh>();
    const gfx::FillChunk& c = chunks.front();
    groundPlane_->vb = r_->createBuffer(gfx::BufferKind::Vertex,
                                        c.vertices.size() * sizeof(gfx::FillVertex),
                                        c.vertices.data());
    groundPlane_->ib = r_->createBuffer(gfx::BufferKind::Index,
                                        c.indices.size() * sizeof(std::uint32_t),
                                        c.indices.data());
    groundPlane_->count = static_cast<std::uint32_t>(c.indices.size());
    if (!groundPlane_->vb->valid() || !groundPlane_->ib->valid())
        return nullptr;
    return groundPlane_.get();
}

// ------------------------------------------------------------------ 3D-målen

CanvasResources::Target3D&
CanvasResources::target3D(const std::shared_ptr<detail::SceneData>& scene,
                          int view, int w, int h, int samples)
{
    auto [it, fresh] = targets_.try_emplace(std::make_pair(scene->id, view));
    Target3D& t      = it->second;
    if (fresh)
        t.src = scene;
    // Provtagningen som renderaren klarar (≤ den begärda) — llvmpipe har 4.
    const int s = r_->supportedSamples(samples);
    if (!t.target || t.w != w || t.h != h || t.samples != s) {
        // Ett nytt mål — det gamla kan bära ett rit-anrop i framen och lever
        // till nästa trim.
        if (t.target)
            retiredTargets_.push_back(std::move(t.target));
        gfx::TargetDesc d;
        d.samples = s;
        d.depth   = true;
        t.target  = r_->createRenderTarget(w, h, d);
        // Kompositen ritar målet 1:1 på sina pixlar — närmaste texel.
        if (t.target->image().valid())
            t.target->image().setFilter(gfx::ImageFilter::Nearest);
        t.w       = w;
        t.h       = h;
        t.samples = s;
        t.drawn   = false;
    }
    return t;
}

void CanvasResources::dropViews(std::uint64_t sceneId, int firstView)
{
    // Nycklarna sorteras på (rityta, vy), så ritytans vyer från `firstView`
    // ligger i följd.
    for (auto it = targets_.lower_bound(std::make_pair(sceneId, firstView));
         it != targets_.end() && it->first.first == sceneId;) {
        if (it->second.target)
            retiredTargets_.push_back(std::move(it->second.target));
        it = targets_.erase(it);
    }
    const int firstSub = std::max(firstView, detail::kMainView + 1);
    for (auto it = labels_.lower_bound(std::make_pair(sceneId, firstSub));
         it != labels_.end() && it->first.first == sceneId;) {
        dropLabels(it->second);
        it = labels_.erase(it);
    }
}

// ------------------------------------------------------------- programmen

CanvasResources::CustomProgram&
CanvasResources::customProgram(const detail::ProgramSlot& slot)
{
    CustomProgram& e = programs_[slot.version];
    if (!e.program) {
        // glaze::Program → fasadens beskrivning: GlazeUser-blocket i hela
        // float4 (std140), texturerna som de står. Ett tal utanför sitt spann
        // når fasaden som det är och ger ett läsbart fel där.
        const Program&   p = slot.program;
        gfx::ProgramDesc d;
        d.name          = p.name;
        d.hlsl          = p.hlsl;
        d.glslVertex    = p.glslVertex;
        d.glslFragment  = p.glslFragment;
        d.msl           = p.msl;
        d.vertexEntry   = p.vertexEntry;
        d.fragmentEntry = p.fragmentEntry;
        d.userBytes     = static_cast<std::uint32_t>(
            (std::max(p.uniformFloats, 0) + 3) / 4 * 16);
        d.textures      = static_cast<std::uint8_t>(
            std::clamp(p.textures, 0, gfx::kMaxProgramTextures + 1));
        e.life    = slot.life;
        e.program = r_->createProgram(d);   // ett ogiltigt loggas av fasaden
    }
    return e;
}

const gfx::ShaderProgram* CanvasResources::program(const detail::ProgramSlot& slot)
{
    const CustomProgram& e = customProgram(slot);
    return e.program && e.program->valid() ? e.program.get() : nullptr;
}

std::string CanvasResources::programError(const detail::ProgramSlot& slot)
{
    const CustomProgram& e = customProgram(slot);
    return e.program ? e.program->error() : std::string{};
}

} // namespace glaze
