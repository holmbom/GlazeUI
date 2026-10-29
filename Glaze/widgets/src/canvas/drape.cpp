// Terrängen och draperingen — höjden och terrängens
// rutnät, regionen, draperingsmålen, deras klass, nyckel, budget, pool och
// minne. Kontraktet och skälen står i drape.h; ritningen av Ground-lagren i
// målen i scene.h (`GroundPart::Drape`) och terrängens draperade mesh i
// scene3d.cpp.

#include "drape.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <gfx/platform.h>

#include "../image_cache.h"
#include "../profiler_hooks.h"
#include "build.h"
#include "camera_math.h"
#include "glaze/app.h"
#include "labels.h"
#include "mesh_data.h"
#include "pick3d.h"
#include "resources.h"
#include "scene3d.h"

namespace glaze::detail {

namespace {

// Upplösningsklassens gränser: målets sida i px, en tvåpotens (drape.h).
constexpr int kMinDrapeClass = 64;
constexpr int kMaxDrapeClass = 2048;
// Klassen byts NED först när den behövda är så många gånger mindre — en
// tvåpotens emellan hålls (hysteres 2×).
constexpr int kShrinkFactor = 4;

// Gallringens marginal kring plattans låda, fysiska px (som föremålens).
constexpr double kCullSlackPx = 2.0;

// Målets byte per pixel (RGBA8) — minnesbudgetens räkning.
constexpr std::size_t kDrapeBytesPerPixel = 4;

// BILDENS BUDGET för draperingarna: 8 Mi px, två mål om
// 2048² — i pixlar och inte i tid, så grindarna blir deterministiska.
constexpr std::uint64_t kDrapePixelsPerFrame = std::uint64_t{8} << 20;

// POOLENS TAK (drape.h): mål som lämnat en drapering och väntar på en ny.
constexpr std::size_t kDrapePoolBytes = std::size_t{64} << 20;

// TERRÄNGENS RUTNÄT: högst så många celler per axel — med fler växer cellen
// (små plattor spridda över ett stort område).
constexpr int kTerrainCellsPerAxis = 128;

// Draperingens genomskinliga start.
const gfx::Color kTransparent{0, 0, 0, 0};

std::size_t drapeBytes(int cls)
{
    return static_cast<std::size_t>(cls) * static_cast<std::size_t>(cls) *
           kDrapeBytesPerPixel;
}

std::uint64_t drapePixels(int cls)
{
    return static_cast<std::uint64_t>(cls) * static_cast<std::uint64_t>(cls);
}

// Formen i ett Terrain-lager som är terräng — en mesh eller en yta i rymden.
bool terrainShape(const Shape& s)
{
    return (s.kind() == Shape::Kind::Mesh || s.kind() == Shape::Kind::Area3) &&
           s.mesh().valid();
}

// Formen höjdfrågan prövar — terrängen och instanser på den.
bool heightShape(const Shape& s)
{
    return s.kind() == Shape::Kind::Mesh || s.kind() == Shape::Kind::Area3 ||
           s.kind() == Shape::Kind::Instances;
}

// Når rutan [x0, x1] × [y0, y1] draperingens låda?
bool inom(const DrapeView& v, double x0, double y0, double x1, double y1)
{
    return x1 >= v.minX && x0 <= v.maxX && y1 >= v.minY && y0 <= v.maxY;
}

// Samma låda och samma håll? Bara då är en väntande platta hjälpt av sin
// gamla drapering (drape.h) — en flyttad formplats har en annan plats i den.
bool sammaLåda(const DrapeView& a, const DrapeView& b)
{
    return a.minX == b.minX && a.minY == b.minY && a.maxX == b.maxX &&
           a.maxY == b.maxY && a.northUp == b.northUp;
}

// Temats färger en standardikon ritas med — vektorn ändras aldrig, så
// innehållet är färgerna (skalan står redan i nyckeln).
void mixStockColors(KeyHash& k, const Theme::Colors& p)
{
    for (const gfx::Color t : {p.text, p.stockBlue, p.stockGreen, p.stockRed,
                               p.stockAmber, p.stockYellow})
        k.mix((std::uint64_t{t.r} << 24) | (std::uint64_t{t.g} << 16) |
              (std::uint64_t{t.b} << 8) | std::uint64_t{t.a});
}

// INNEHÅLLSNYCKELN för draperingen med lådan `v` och klassen `cls` — det
// draperingen ritar (drape.h); 0 när ingenting av Ground-lagren når lådan.
// Lagrens placering ur formerna (`effectivePlacement`) och inte ur en vys
// förberedelse: en del-vy förbereder bara sina egna lager. En NY indata här
// ska också in i markstämpeln (`groundStamp`) — annars räknas nyckeln inte
// om när den ändras (drape.h, fallgropen).
std::uint64_t contentKey(const PaintContext& f, const SceneData& sc,
                         const DrapeView& v, int cls)
{
    KeyHash       k;
    bool          någon = false;
    const double  målPx = static_cast<double>(cls) / (v.maxX - v.minX);
    ImageCache*   bilder = f.c->images;
    k.mix(static_cast<std::uint64_t>(sc.yUp));   // bildernas hörn (scene.h)
    k.mix(static_cast<double>(f.c->scale));
    k.mix(static_cast<double>(f.c->pxPerMm));
    for (const std::shared_ptr<LayerData>& Lp : sc.layers) {
        const LayerData& L = *Lp;
        if (!L.visible || effectivePlacement(L) != LayerPlacement::Ground)
            continue;
        const bool ritas = layerDrawn(f, L);
        k.mix(L.id);
        k.mix(static_cast<std::uint64_t>(ritas));
        k.mix(static_cast<double>(L.opacity));
        k.mix(static_cast<std::uint64_t>(L.order));
        if (!ritas)
            continue;
        for (const std::shared_ptr<RunData>& run : L.runs) {
            const RunKey& rk = run->key;
            if (isGlyphKind(rk.kind)) {
                // Bara den LIGGANDE texten draperas; de stående etiketterna
                // står vid markhöjd.
                if (!rk.ground || !run->hasBox ||
                    !inom(v, run->minX, run->minY, run->maxX, run->maxY))
                    continue;
                const CanvasResources::GlyphBuild* b = f.res->findGlyphs(*run);
                k.mix(run->id);
                k.mix(b ? b->serial : 0);
                någon = true;
                continue;
            }
            if (isObjectKind(rk.kind) || rk.kind == PartKind::Pass)
                continue;
            // En kontur i px eller mm når sin halva bredd utanför chunkens
            // låda — i draperingens världsenheter.
            const double unitPx  = pxPerUnit(f, f.world, rk.unit);
            bool         körning = false;
            for (const std::shared_ptr<ChunkData>& ch : run->chunks) {
                if (ch->indices.empty() || !ch->hasBox)
                    continue;
                const double m =
                    isLineKind(rk.kind) && rk.unit != Unit::World && målPx > 0.0
                        ? (ch->margin * unitPx + kCullSlackPx) / målPx
                        : 0.0;
                if (!inom(v, ch->minX - m, ch->minY - m, ch->maxX + m, ch->maxY + m))
                    continue;
                k.mix(ch->id);
                k.mix(ch->version);
                körning = true;
            }
            if (!körning)
                continue;
            någon = true;
            // Bildens innehåll i fönstrets textur — en bild som väntar på
            // budgeten ritas om när den kommer.
            if (rk.kind == PartKind::Image && bilder) {
                switch (rk.image.kind()) {
                case ImageSource::Kind::Bitmap:
                    k.mix(bilder->contentVersion(rk.image.bitmap()));
                    break;
                case ImageSource::Kind::Texture:
                    k.mix(bilder->contentVersion(rk.image.texture()));
                    break;
                case ImageSource::Kind::Asset:
                    k.mix(static_cast<std::uint64_t>(
                        bilder->get(rk.image.asset()) != nullptr));
                    break;
                case ImageSource::Kind::Stock:
                    mixStockColors(k, f.c->theme->colors);
                    k.mix(static_cast<std::uint64_t>(
                        bilder->get(rk.image, *f.c) != nullptr));
                    break;
                case ImageSource::Kind::None:
                    break;
                }
            }
        }
    }
    if (!någon)
        return 0;
    k.mix(static_cast<std::uint64_t>(cls));
    k.mix(static_cast<std::uint64_t>(v.northUp));
    k.mix(v.minX);
    k.mix(v.minY);
    k.mix(v.maxX);
    return k.h == 0 ? 1 : k.h;
}

// MARKSTÄMPELN (drape.h): allt innehållsnyckeln läser utanför
// plattans låda, i O(lager) — en kameraändring rör den inte. Oförändrad
// stämpel, klass och låda ⇒ oförändrad nyckel.
std::uint64_t groundStamp(const PaintContext& f, const SceneData& sc)
{
    KeyHash k;
    k.mix(static_cast<std::uint64_t>(sc.yUp));
    k.mix(static_cast<double>(f.c->scale));
    k.mix(static_cast<double>(f.c->pxPerMm));
    k.mix(f.c->images ? f.c->images->contentGeneration() : 0);
    k.mix(f.res->atlasEpoch());
    if (f.c->theme)
        mixStockColors(k, f.c->theme->colors);
    for (const std::shared_ptr<LayerData>& Lp : sc.layers) {
        const LayerData&     L = *Lp;
        const LayerPlacement p = effectivePlacement(L);
        k.mix(L.id);
        k.mix(static_cast<std::uint64_t>(L.visible));
        k.mix(static_cast<std::uint64_t>(p));
        if (!L.visible || p != LayerPlacement::Ground)
            continue;
        k.mix(L.shapesVersion);
        k.mix(L.textVersion);
        k.mix(static_cast<double>(L.opacity));
        k.mix(static_cast<std::uint64_t>(L.order));
        k.mix(static_cast<std::uint64_t>(layerDrawn(f, L)));
    }
    return k.h == 0 ? 1 : k.h;
}

// PLATTANS SKÄRMAVTRYCK: den största sidan av lådans projicerade skärmruta i
// fysiska px; ett hörn bakom närplanet ger taket.
double footprint(const Projection& p, const Box3& b)
{
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
    for (int k = 0; k < 8; ++k) {
        double c[4];
        p.clip((k & 1) ? b.max.x : b.min.x, (k & 2) ? b.max.y : b.min.y,
               (k & 4) ? b.max.z : b.min.z, c);
        if (!(c[3] > 0.0) || c[2] < 0.0)
            return kMaxDrapeClass;
        const double x = c[0] / c[3], y = c[1] / c[3];
        x0 = k == 0 ? x : std::min(x0, x);
        y0 = k == 0 ? y : std::min(y0, y);
        x1 = k == 0 ? x : std::max(x1, x);
        y1 = k == 0 ? y : std::max(y1, y);
    }
    return std::max(x1 - x0, y1 - y0);
}

// Ritar draperingen i målet: Ground-lagren uppifrån genom lagrens egen väg,
// med draperingens ritläge `f` (huvudvyns lager och zoom — prepareDrapes) och
// lagrens placering ur formerna, som nyckeln.
void renderDrape(const PaintContext& f, const SceneData& sc, gfx::RenderTarget& t,
                 const DrapeView& v, int cls)
{
    LayerFrame dl;
    dl.terrain = true;
    for (const std::shared_ptr<LayerData>& L : sc.layers)
        dl.placement.push_back(effectivePlacement(*L));
    PaintContext df;
    df.c                = f.c;
    df.res              = f.res;
    df.world.cx         = (v.minX + v.maxX) / 2.0;
    df.world.cy         = (v.minY + v.maxY) / 2.0;
    df.world.pxPerUnit  = static_cast<double>(cls) / (v.maxX - v.minX);
    df.world.bearingDeg = 0.0;
    df.world.pitchDeg   = 0.0;
    df.world.yUp        = v.northUp;
    df.world.box        = gfx::Rectf{0.0f, 0.0f, static_cast<float>(cls),
                                     static_cast<float>(cls)};
    df.screen           = df.world;
    df.zoom             = f.zoom;
    df.opacity          = f.opacity;
    df.view             = kMainView;
    df.labelSlot        = f.labelSlot;
    df.lighting         = f.lighting;
    gfx::Renderer& r = *f.c->r;
    r.pushTarget(t, kTransparent);
    drawGround(df, sc, dl, GroundPart::Drape);
    r.popTarget();
}

// Cellen för en koordinat längs en axel i terrängens rutnät — klampad i
// flyttal FÖRE heltalet (en punkt långt utanför spräcker ingen int) och
// MONOTON, så en låda och varje punkt i den delar cell (placement.cpp:s
// regel).
int cellOf(double v, double origin, double cell, int n)
{
    const double c = std::floor((v - origin) / cell);
    return static_cast<int>(std::clamp(c, 0.0, static_cast<double>(n - 1)));
}

} // namespace

// ---------------------------------------------------------------- terrängen

// TERRÄNGENS RUTNÄT (drape.h `terrainHeight`): de synliga Terrain-lagrens
// former i ett jämnt XY-rutnät — en form i varje cell dess låda når — med
// det rutnätet byggdes ur. Pekarna gäller så länge rutnätet är aktuellt:
// varje ändring av en form eller ett lager i det stegar en av nycklarna, och
// en riven lagerdata märks på sitt livstecken.
struct TerrainIndex {
    struct Layer {
        std::weak_ptr<const LayerData> life;
        const LayerData*               data = nullptr;
        std::uint64_t                  shapesVersion = 0;
    };
    struct Item {
        const ShapeEntry* entry = nullptr;
        Box3              box{};
        std::uint32_t     layer = 0;
    };
    bool                       built = false;
    std::uint64_t              layerSetVersion = 0, meshGeneration = 0;
    std::vector<Layer>         layers;
    std::vector<Item>          items;
    double                     x0 = 0.0, y0 = 0.0, cell = 1.0;
    int                        nx = 0, ny = 0;
    std::vector<std::uint32_t> start;   // cellens första plats i `cells` (nx · ny + 1)
    std::vector<std::uint32_t> cells;   // formernas index, cell för cell
    std::vector<SceneHit>      hits;    // höjdfrågans — återanvänd, ingen allokering
};

namespace {

// Är rutnätet byggt ur scenen som den är nu? O(Terrain-lager).
bool current(const TerrainIndex& ix, const SceneData& sc)
{
    if (!ix.built || ix.layerSetVersion != sc.layerSetVersion ||
        ix.meshGeneration != meshGeneration())
        return false;
    for (const TerrainIndex::Layer& l : ix.layers)
        if (l.life.expired() || l.data->shapesVersion != l.shapesVersion)
            return false;
    return true;
}

void build(TerrainIndex& ix, const SceneData& sc)
{
    ix.layers.clear();
    ix.items.clear();
    for (const std::shared_ptr<LayerData>& Lp : sc.layers) {
        const LayerData& L = *Lp;
        if (!L.visible || effectivePlacement(L) != LayerPlacement::Terrain)
            continue;
        const auto li = static_cast<std::uint32_t>(ix.layers.size());
        ix.layers.push_back(TerrainIndex::Layer{Lp, &L, L.shapesVersion});
        for (const ShapeSlot& slot : L.slots) {
            if (!heightShape(slot.entry->shape))
                continue;
            if (const std::optional<Box3>& b = objectBox(*slot.entry))
                ix.items.push_back(TerrainIndex::Item{slot.entry.get(), *b, li});
        }
    }
    ix.nx = ix.ny = 0;
    ix.start.clear();
    ix.cells.clear();
    if (!ix.items.empty()) {
        // CELLEN = medianformens sida, inom taket för celler per axel.
        double              x1 = ix.items.front().box.max.x, y1 = ix.items.front().box.max.y;
        std::vector<double> sidor;
        sidor.reserve(ix.items.size());
        ix.x0 = ix.items.front().box.min.x;
        ix.y0 = ix.items.front().box.min.y;
        for (const TerrainIndex::Item& it : ix.items) {
            ix.x0 = std::min(ix.x0, it.box.min.x);
            ix.y0 = std::min(ix.y0, it.box.min.y);
            x1    = std::max(x1, it.box.max.x);
            y1    = std::max(y1, it.box.max.y);
            sidor.push_back(std::max(it.box.max.x - it.box.min.x,
                                     it.box.max.y - it.box.min.y));
        }
        const auto mitt = sidor.begin() + static_cast<std::ptrdiff_t>(sidor.size() / 2);
        std::nth_element(sidor.begin(), mitt, sidor.end());
        const double ex = x1 - ix.x0, ey = y1 - ix.y0;
        double       cell = std::max({*mitt, ex / kTerrainCellsPerAxis,
                                      ey / kTerrainCellsPerAxis});
        if (!(cell > 0.0) || !std::isfinite(cell))
            cell = 1.0;
        ix.cell = cell;
        ix.nx   = std::clamp(static_cast<int>(std::ceil(ex / cell)), 1,
                             kTerrainCellsPerAxis);
        ix.ny   = std::clamp(static_cast<int>(std::ceil(ey / cell)), 1,
                             kTerrainCellsPerAxis);
        // Två pass: cellernas antal, sedan formerna på sina platser.
        ix.start.assign(static_cast<std::size_t>(ix.nx) * ix.ny + 1, 0);
        const auto varje = [&ix](const Box3& b, auto&& gör) {
            const int i0 = cellOf(b.min.x, ix.x0, ix.cell, ix.nx);
            const int i1 = cellOf(b.max.x, ix.x0, ix.cell, ix.nx);
            const int j0 = cellOf(b.min.y, ix.y0, ix.cell, ix.ny);
            const int j1 = cellOf(b.max.y, ix.y0, ix.cell, ix.ny);
            for (int j = j0; j <= j1; ++j)
                for (int i = i0; i <= i1; ++i)
                    gör(static_cast<std::size_t>(j) * ix.nx + i);
        };
        for (const TerrainIndex::Item& it : ix.items)
            varje(it.box, [&ix](std::size_t c) { ++ix.start[c + 1]; });
        for (std::size_t c = 1; c < ix.start.size(); ++c)
            ix.start[c] += ix.start[c - 1];
        ix.cells.resize(ix.start.back());
        std::vector<std::uint32_t> fyllt(ix.start.begin(), ix.start.end() - 1);
        for (std::size_t k = 0; k < ix.items.size(); ++k)
            varje(ix.items[k].box, [&](std::size_t c) {
                ix.cells[fyllt[c]++] = static_cast<std::uint32_t>(k);
            });
    }
    ix.built           = true;
    ix.layerSetVersion = sc.layerSetVersion;
    ix.meshGeneration  = meshGeneration();
    ++terrainIndexStats().builds;
}

// Scenens rutnät, aktuellt.
TerrainIndex& terrainIndex(const SceneData& sc)
{
    if (!sc.terrainIndex)
        sc.terrainIndex = std::make_shared<TerrainIndex>();
    TerrainIndex& ix = *sc.terrainIndex;
    if (!current(ix, sc))
        build(ix, sc);
    return ix;
}

} // namespace

TerrainIndexStats& terrainIndexStats()
{
    static TerrainIndexStats s;
    return s;
}

bool hasTerrain(const PaintContext& f, const SceneData& sc)
{
    for (const std::shared_ptr<LayerData>& Lp : sc.layers) {
        const LayerData& L = *Lp;
        if (!L.visible || effectivePlacement(L) != LayerPlacement::Terrain ||
            !layerDrawn(f, L))
            continue;
        for (const ShapeSlot& slot : L.slots)
            if (terrainShape(slot.entry->shape))
                return true;
    }
    return false;
}

std::optional<double> terrainHeight(const SceneData& sc, double x, double y)
{
    if (!std::isfinite(x) || !std::isfinite(y))
        return std::nullopt;
    TerrainIndex& ix = terrainIndex(sc);
    if (ix.items.empty())
        return std::nullopt;
    // En lodrät stråle uppifrån mot cellens former (pick3d.h
    // `verticalRayDown`): den första ytan är den översta. Den finaste nivån
    // och ingen kamera — höjden beror inte på var kameran står, och inte på
    // ordningen (den översta vinner).
    const std::size_t   c = static_cast<std::size_t>(cellOf(y, ix.y0, ix.cell, ix.ny)) *
                              ix.nx +
                          cellOf(x, ix.x0, ix.cell, ix.nx);
    TerrainIndexStats&  räknat = terrainIndexStats();
    PickContext         pk;
    pk.finest = true;
    std::optional<double> bäst;
    for (std::uint32_t k = ix.start[c]; k < ix.start[c + 1]; ++k) {
        ++räknat.candidates;
        const TerrainIndex::Item& it = ix.items[ix.cells[k]];
        const Box3&               b  = it.box;
        if (x < b.min.x || x > b.max.x || y < b.min.y || y > b.max.y)
            continue;
        pk.ray          = verticalRayDown(x, y, b);
        pk.layerOpacity = ix.layers[it.layer].data->opacity;
        ix.hits.clear();
        pickShape3D(it.entry->shape, pk, ix.hits);
        for (const SceneHit& t : ix.hits)
            if (t.kind == HitKind::Surface && (!bäst || t.point.z > *bäst))
                bäst = t.point.z;
    }
    return bäst;
}

std::uint64_t terrainRegionKey(const SceneData& sc, double x0, double y0,
                               double x1, double y1)
{
    // SUMMAN av formernas nycklar — ordningsoberoende (drape.h).
    const TerrainIndex& ix = terrainIndex(sc);
    std::uint64_t       summa = 0;
    for (const TerrainIndex::Item& it : ix.items) {
        const Box3& b = it.box;
        if (b.max.x < x0 || b.min.x > x1 || b.max.y < y0 || b.min.y > y1)
            continue;
        const std::shared_ptr<MeshData>& d = MeshAccess::data(it.entry->shape.mesh());
        KeyHash                          k;
        k.mix(it.entry->id);
        k.mix(it.entry->version);
        k.mix(d ? d->version : 0);
        summa += k.h;
    }
    return summa;
}

// --------------------------------------------------------------- draperingen

const DrapeView* DrapeFrame::find(std::uint64_t entryId) const
{
    const auto it = views_.find(entryId);
    return it != views_.end() ? &it->second : nullptr;
}

void DrapeFrame::add(std::uint64_t entryId, const DrapeView& v)
{
    views_[entryId] = v;
}

std::uint64_t drapeFrameBudget()
{
    return kDrapePixelsPerFrame;
}

std::size_t DrapeCache::classSlot(int cls)
{
    std::size_t k = 0;
    while (k + 1 < kClasses && (kMinDrapeClass << k) < cls)
        ++k;
    return k;
}

void DrapeCache::trim()
{
    // Målen som lämnade en drapering i förra framen — ett rit-anrop kan ha
    // pekat på dem — till poolen, eller släppta här.
    std::vector<Retired> lämnade;
    lämnade.swap(retired_);
    for (Retired& r : lämnade)
        if (r.pool)
            toPool(std::move(r.target), r.cls, r.scene);
    ++frame_;
    framePixels_ = 0;
    // Draperingar vars formplats dött (formen borttagen eller ersatt med en
    // annan plats, lagret eller ritytan riven) lämnar sina mål till poolen.
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (!it->second.src.expired()) {
            ++it;
            continue;
        }
        if (it->second.target)
            toPool(std::move(it->second.target), it->second.cls, it->second.scene);
        it = entries_.erase(it);
    }
    // Poolen bär bara mål för ritytor som fortfarande har draperingar — en
    // riven ritytas mål räknas inte i någon budget.
    std::vector<std::uint64_t> ytor;
    for (const auto& [id, e] : entries_)
        if (std::find(ytor.begin(), ytor.end(), e.scene) == ytor.end())
            ytor.push_back(e.scene);
    for (std::size_t k = 0; k < kClasses; ++k) {
        std::vector<Pooled>& klass = pool_[k];
        const std::size_t    b     = drapeBytes(kMinDrapeClass << k);
        for (auto it = klass.begin(); it != klass.end();) {
            if (std::find(ytor.begin(), ytor.end(), it->scene) != ytor.end()) {
                ++it;
                continue;
            }
            poolBytes_ -= b;
            it = klass.erase(it);
        }
    }
}

void DrapeCache::toPool(std::unique_ptr<gfx::RenderTarget> t, int cls,
                        std::uint64_t scene)
{
    const std::size_t b = drapeBytes(cls);
    if (!t || poolBytes_ + b > kDrapePoolBytes)
        return;   // poolen full — målet släpps här
    poolBytes_ += b;
    pool_[classSlot(cls)].push_back(Pooled{std::move(t), scene});
}

void DrapeCache::dropPooled(std::uint64_t scene, std::size_t& bytes,
                            std::size_t limit)
{
    for (std::size_t k = kClasses; k-- > 0 && bytes > limit;) {
        std::vector<Pooled>& klass = pool_[k];
        const std::size_t    b     = drapeBytes(kMinDrapeClass << k);
        for (auto it = klass.begin(); it != klass.end() && bytes > limit;) {
            if (it->scene != scene) {
                ++it;
                continue;
            }
            it = klass.erase(it);
            poolBytes_ -= b;
            bytes -= b;
        }
    }
}

std::unique_ptr<gfx::RenderTarget> DrapeCache::takeTarget(int cls)
{
    std::vector<Pooled>& klass = pool_[classSlot(cls)];
    if (!klass.empty()) {
        std::unique_ptr<gfx::RenderTarget> t = std::move(klass.back().target);
        klass.pop_back();
        poolBytes_ -= drapeBytes(cls);
        return t;
    }
    std::unique_ptr<gfx::RenderTarget> t = r_->createRenderTarget(cls, cls);
    if (!t->image().valid()) {
        retired_.push_back(Retired{std::move(t), cls, 0, false});
        return nullptr;   // fasaden har loggat
    }
    t->image().setFilter(gfx::ImageFilter::Linear);
    return t;
}

bool DrapeCache::render(const PaintContext& df, const SceneData& sc, Entry& e,
                        const DrapeView& v, int cls)
{
    if (!e.target || e.cls != cls) {
        // KLASSBYTET I STEG (drape.h): det nya målet ritas färdigt innan det
        // gamla lämnar draperingen — tills dess visas det gamla. Det gamla kan
        // bära ett rit-anrop i framen och går till poolen i nästa trim.
        std::unique_ptr<gfx::RenderTarget> ny = takeTarget(cls);
        if (!ny)
            return false;
        renderDrape(df, sc, *ny, v, cls);
        if (e.target)
            retired_.push_back(Retired{std::move(e.target), e.cls, e.scene, true});
        e.target = std::move(ny);
    } else {
        renderDrape(df, sc, *e.target, v, cls);
    }
    e.cls        = cls;
    e.drawnKey   = e.key;
    e.view       = v;
    e.view.image = &e.target->image();
    ++renders_;
    pixels_ += drapePixels(cls);
    framePixels_ += drapePixels(cls);
    profileCount(ProfileCount::DrapeRenders);
    profileCount(ProfileCount::DrapePixels, drapePixels(cls));
    return true;
}

std::size_t DrapeCache::count() const
{
    std::size_t n = 0;
    for (const auto& [id, e] : entries_)
        n += e.target ? 1 : 0;
    return n;
}

std::size_t DrapeCache::bytes() const
{
    std::size_t n = poolBytes_;
    for (const auto& [id, e] : entries_)
        if (e.target)
            n += drapeBytes(e.cls);
    return n;
}

std::size_t DrapeCache::bytes(std::uint64_t scene) const
{
    std::size_t n = 0;
    for (const auto& [id, e] : entries_)
        if (e.target && e.scene == scene)
            n += drapeBytes(e.cls);
    for (std::size_t k = 0; k < kClasses; ++k)
        for (const Pooled& p : pool_[k])
            if (p.scene == scene)
                n += drapeBytes(kMinDrapeClass << k);
    return n;
}

int DrapeCache::classOf(std::uint64_t entryId) const
{
    const auto it = entries_.find(entryId);
    return it != entries_.end() && it->second.target ? it->second.cls : 0;
}

std::unordered_map<std::uint64_t, int> DrapeCache::classes() const
{
    std::unordered_map<std::uint64_t, int> ut;
    for (const auto& [id, e] : entries_)
        if (e.target)
            ut.emplace(id, e.cls);
    return ut;
}

int drapeClass(int current, double footprintPx)
{
    int ideal = kMinDrapeClass;
    while (ideal < footprintPx && ideal < kMaxDrapeClass)
        ideal *= 2;
    if (current <= 0 || ideal > current || ideal * kShrinkFactor <= current)
        return ideal;
    return current;
}

DrapeFrame prepareDrapes(const PaintContext& f,
                         const std::shared_ptr<SceneData>& scene,
                         const LayerFrame& lf, DrapeMode mode)
{
    DrapeFrame ut;
    if (!lf.terrain || !f.view3D)
        return ut;
    const SceneData& sc    = *scene;
    DrapeCache&      cache = f.res->drapes();
    const Projection proj(*f.view3D);
    // DRAPERINGENS RITLÄGE (drape.h): huvudvyns lager och zoom för nyckeln OCH
    // ritningen, vilken vy som än förbereder — zoomen ur huvudvyns senaste
    // förberedelse, eller anroparens när huvudvyn inte ritat än.
    const bool klasser = mode == DrapeMode::Main;
    if (klasser)
        cache.mainZoom_ = f.zoom;
    PaintContext df = f;
    df.view         = kMainView;
    df.zoom         = cache.mainZoom_.value_or(f.zoom);
    const std::uint64_t stämpel = groundStamp(df, sc);

    // Det som ska ritas — synliga plattor utan drapering, med ändrad nyckel
    // eller bytt klass (gruppen, i den ordningen) — och avtrycket.
    struct Arbete {
        std::uint64_t      id;
        DrapeCache::Entry* e;
        DrapeView          v;
        int                cls;
        int                grupp;
        double             avtryck;
    };
    std::vector<Arbete> arbete;
    for (std::size_t li = 0; li < sc.layers.size(); ++li) {
        const LayerData& L = *sc.layers[li];
        if (!L.visible || lf.placement[li] != LayerPlacement::Terrain ||
            !layerDrawn(f, L))
            continue;
        for (const ShapeSlot& slot : L.slots) {
            if (!terrainShape(slot.entry->shape))
                continue;
            const std::optional<Box3>& b = objectBox(*slot.entry);
            if (!b)
                continue;
            // Den KVADRATISKA lådan kring formens XY-låda.
            const double side = std::max(b->max.x - b->min.x, b->max.y - b->min.y);
            if (!(side > 0.0) || !std::isfinite(side))
                continue;
            const double cx = (b->min.x + b->max.x) / 2.0;
            const double cy = (b->min.y + b->max.y) / 2.0;
            // Draperingen ritas UPPIFRÅN när kameran står ovanför plattan —
            // och underifrån (y nedåt, en kartkamera med y nedåt står under
            // planet) annars, så att den liggande texten aldrig blir
            // spegelvänd på skärmen (drape.h).
            const bool uppifrån = f.view3D->eye.z >= b->min.z;
            DrapeView  v{nullptr, cx - side / 2.0, cy - side / 2.0,
                         cx + side / 2.0, cy + side / 2.0, uppifrån};

            const std::uint64_t id = slot.entry->id;
            const auto          it = cache.entries_.find(id);
            DrapeCache::Entry*  e  = it != cache.entries_.end() ? &it->second : nullptr;
            // DOLD: ritas aldrig om — den gamla draperingen står kvar,
            // oritad i vyn, och ritas om när plattan syns.
            if (!boxVisible3(proj, *b, kCullSlackPx)) {
                if (e && e->target && e->key != 0)
                    ut.add(id, e->view);
                continue;
            }
            // KLASSEN: huvudvyn räknar den; en del-vy och bilden behåller den,
            // och utan drapering tar den avtrycket härifrån.
            const double avtryck = footprint(proj, *b);
            int          cls     = e && e->target ? e->cls : 0;
            if (klasser || cls == 0)
                cls = drapeClass(cls, avtryck);
            if (!e) {
                e      = &cache.entries_[id];
                e->src = slot.entry;
            }
            e->scene     = sc.id;
            e->lastFrame = cache.frame_;
            // NYCKELN bara när stämpeln, klassen eller lådan ändrats.
            KeyHash in;
            in.mix(stämpel);
            in.mix(static_cast<std::uint64_t>(cls));
            in.mix(static_cast<std::uint64_t>(v.northUp));
            in.mix(v.minX);
            in.mix(v.minY);
            in.mix(v.maxX);
            in.mix(v.maxY);
            const std::uint64_t indata = in.h == 0 ? 1 : in.h;
            if (e->keyInput != indata) {
                e->key      = contentKey(df, sc, v, cls);
                e->keyInput = indata;
                ++cache.keysComputed_;
                profileCount(ProfileCount::DrapeKeys);
            }
            if (e->key == 0)
                continue;   // inget av Ground-lagren når plattan
            if (e->target && e->drawnKey == e->key) {
                ut.add(id, e->view);
                continue;
            }
            // Utan drapering — eller med en ur en annan låda (en flyttad
            // formplats) — först, sedan ändrat innehåll, sedan bytt klass.
            const bool utan = !e->target || !sammaLåda(e->view, v);
            arbete.push_back(Arbete{id, e, v, cls, utan ? 0 : e->cls == cls ? 1 : 2,
                                    avtryck});
        }
    }

    // BUDGETEN: gruppens ordning, störst avtryck först; minst en platta
    // per förberedelse. Det som inte ryms visar sin gamla drapering — samma
    // låda, det förra innehållet eller den lägre upplösningen; en flyttad
    // formplats ritas utan tills den är ritad — och väntar. Bilden ritar allt.
    std::stable_sort(arbete.begin(), arbete.end(), [](const Arbete& a, const Arbete& b) {
        return a.grupp != b.grupp ? a.grupp < b.grupp : a.avtryck > b.avtryck;
    });
    bool ritat = false;
    for (const Arbete& a : arbete) {
        const bool ryms = mode == DrapeMode::Complete || !ritat ||
                          cache.framePixels_ + drapePixels(a.cls) <= kDrapePixelsPerFrame;
        if (ryms && cache.render(df, sc, *a.e, a.v, a.cls)) {
            ritat = true;
            ut.add(a.id, a.e->view);
            continue;
        }
        if (!ryms)
            ut.addPending();   // ett mål som inte gick att skapa väntar inte
        if (a.e->target && sammaLåda(a.e->view, a.v))
            ut.add(a.id, a.e->view);
    }

    // MINNET (drape.h): över RITYTANS budget — summan av dess konto i
    // bildcachen, dess draperingar och dess mål i poolen — släpps
    // först poolens mål och vräks sedan ritytans draperingar som inte syntes
    // i framen, äldst först. En platta som syns vräks aldrig. Bildcachen får
    // draperingarnas byte, så dess `trim` jämför samma summa.
    const std::size_t budget =
        sc.memoryBudget > 0 ? sc.memoryBudget : app().canvasMemoryBudget();
    const std::size_t bilder =
        f.c->images ? f.c->images->accountBytes(sc.id) : 0;
    const std::size_t gräns  = budget > bilder ? budget - bilder : 0;
    std::size_t       totalt = cache.bytes(sc.id);
    if (totalt > gräns)
        cache.dropPooled(sc.id, totalt, gräns);
    while (totalt > gräns) {
        DrapeCache::Entry* äldst = nullptr;
        for (auto& [id, e] : cache.entries_)
            if (e.target && e.scene == sc.id && e.lastFrame < cache.frame_ &&
                (!äldst || e.lastFrame < äldst->lastFrame))
                äldst = &e;
        if (!äldst) {
            if (!cache.overLogged_)
                gfx::log("Canvas: draperingarna som syns tar %zu byte över "
                         "ritytans minnesbudget (%zu byte med bilderna) — ingen "
                         "vräks; loggas en gång",
                         totalt + bilder - budget, budget);
            cache.overLogged_ = true;
            break;
        }
        totalt -= drapeBytes(äldst->cls);
        cache.retired_.push_back(DrapeCache::Retired{std::move(äldst->target),
                                                     äldst->cls, äldst->scene,
                                                     false});
        äldst->target.reset();
        äldst->cls      = 0;
        äldst->drawnKey = 0;
    }
    if (f.c->images)
        f.c->images->setAccountExtra(sc.id, totalt);
    return ut;
}

} // namespace glaze::detail
