#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <map>
#include <optional>

#include <gfx/camera.h>
#include <gfx/image.h>
#include <gfx/renderer.h>
#include <gfx/scene.h>
#include <gfx/sdf.h>

#include "glaze/canvas/shape.h"
#include "scene.h"

// INTERN — RITYTANS GPU-RESURSER PER FÖNSTER. Lever
// bredvid `ImageCache` i `AppWindow` och följer samma livstidsregel: allt här
// hör till fönstrets renderer. `Ctx::canvas` pekar hit.
//
//   CHUNKARNA   lagrens CPU-chunkar (scene.h) som buffertar på GPU:n, synkade
//               mot chunkens ändringslogg: en ny chunk laddas upp hel, ett
//               tillägg bara svansen, en borttagning bara indexintervallet.
//               En kameraändring rör ingenting här.
//   TEXTEN      Glyph-körningarna byggda mot FÖNSTRETS atlas och täthet —
//               en text kan inte byggas utan fontens rasterisering, och den
//               är rendererns. Byggs om när körningen ändrats, tätheten
//               ändrats eller atlasen tömts.
//   ATLASERNA   en SDF-atlas per FAMILJ — alla fyra stilarna i samma atlas, så
//               en etikett med flera stilar
//               ritas i ett anrop.
//   ETIKETTERNA kollisionens placering per RITYTA och vy eller
//               bild: vilka etiketter som syns, och
//               synligheten som en R8-bild med en texel per etikett — det
//               enda en kameraändring laddar upp.
//   RAMPERNA    gradienternas 256 × 1-ramper, ur stoppen (LRU, tak).
//   RUTNÄTET    en statisk mesh med linjer på heltalspositioner, ritad med
//               en skala — panorering och zoom laddar aldrig upp den igen.
//   3D-MÅLEN ett render-mål med djup och MSAA per
//               (rityta, vy) — huvudvyn och varje del-vy —
//               och vad det senast ritades med, 3D-cachens nyckel
//               (target3d.h). MARKPLANET är en statisk enhetsruta (Fill),
//               ritad med en skala som rutnätet.
//   PROGRAMMEN ritytornas egna program, kompilerade ett
//               per programversion.
//   DRAPERINGEN ett mål per terrängmesh — drape.h.
//
// Bildtexturerna (Shape::image) är INTE här: de är bildcachens (budget, LRU,
// tak per frame), och den transienta ringen är rendererns
// (`uploadTransient`). Ritytan använder dem som de är.
//
// LIVSTIDEN: skapas i `AppWindow::showOwned` efter `images_`, `trim()` körs
// ÖVERST i `renderContent` — det ENDA stället något släpps, så en resurs dör
// aldrig mitt i en frame — och rivs i `App::executeClose` FÖRE `images_`.
// En post släpps när dess KÄLLA dött (chunken eller körningen byggdes om,
// lagret eller ritytan revs). En rityta KAN byta fönster — en flytande
// dockningspanel (`Widget::transferChild`) — och laddas då upp
// på nytt i det nya fönstret (versionsstyrt), medan buffertarna i det gamla
// står kvar tills det fönstret stängs eller ritytan rivs (skriven gräns).

namespace glaze {

namespace detail {
class ObjectResidency;   // residency.h — föremålen på GPU:n
class DrapeCache;        // drape.h — terrängens draperingsmål
}

class CanvasResources {
public:
    explicit CanvasResources(gfx::Renderer& r);
    ~CanvasResources();
    CanvasResources(const CanvasResources&)            = delete;
    CanvasResources& operator=(const CanvasResources&) = delete;

    gfx::Renderer& renderer() { return *r_; }

    // FÖREMÅLEN: meshar, instanser och punktmoln på
    // GPU:n och de inbyggda 3D-programmen — residency.h. Rivs och trimmas med
    // resten.
    detail::ObjectResidency& objects() { return *objects_; }
    // DRAPERINGARNA: terrängens draperingsmål — drape.h.
    // Rivs och trimmas med resten.
    detail::DrapeCache& drapes() { return *drapes_; }

    // Framens början: släpper det vars källa dött (en chunk eller körning
    // som byggts om, en rityta som rivits), tömmer atlaserna över taket
    // (texten byggs då om nästa ritning) och håller rampernas tak.
    void trim();

    // ---- CHUNKARNA
    struct GpuChunk {
        std::unique_ptr<gfx::GpuBuffer> vb, ib;
        std::size_t                     vbCap = 0, ibCap = 0;   // byte
    };
    // Chunkens buffertar, ikapp med dess ändringslogg. nullptr för en tom
    // chunk eller när buffertarna inte gick att skapa.
    const GpuChunk* sync(const std::shared_ptr<detail::ChunkData>& chunk);

    // ---- TEXTEN
    // EN ETIKETT (eller markör) i ett textbygge — det etikettkollisionen
    // räknar på. `label` är indexet i bygget, det som står i
    // vertexens `GlyphVertex::label`; ritytans synlighetsbild har texeln
    // `LabelRun::base + label` för den (index GLOBALT per rityta och fönster,
    // basen per rit-anrop — ett ombyggt bygge flyttar ingen annan körnings
    // vertex). `shape` är formens plats i körningens `glyphShapes` och `sub`
    // markörens punkt — träffprovet går tillbaka till formen med dem.
    struct LabelInfo {
        std::uint32_t label = 0;
        Point         anchor{};
        double        z = 0.0;        // ankarets höjd (Glyph3; plan text 0)
        gfx::TextBox  box{};          // i em relativt ankaret, ovriden
        float         size = 0.0f;    // em i körningens enhet
        // Vinkeln (moturs) som cos och sin, räknade EN gång när etiketten
        // byggs — inte i varje placering och varje träffprov.
        double        cosA = 1.0, sinA = 0.0;
        Alignment     rotation = Alignment::Viewport;
        bool          collide  = true;
        int           priority = 0;
        float         padding  = 0.0f;   // körningens enhet
        std::uint32_t shape    = 0;      // körningens glyphShapes[shape]
        std::uint32_t sub      = 0;      // markörens punkt (text: 0)
        // SYNLIGHETEN EFTER ZOOM — stilens intervall; 0 = obegränsat.
        double        minZoom  = 0.0, maxZoom = 0.0;
    };
    // En chunk text på GPU:n — `GlyphVertex` eller, i en Glyph3-körning och
    // för etiketter vid markhöjd, `GlyphVertex3` med
    // origot och ankarnas låda i z. `program` är chunkens — Glyph eller Glyph3.
    struct GlyphGpu {
        gfx::MeshProgram                program = gfx::MeshProgram::Glyph;
        double                          originX = 0.0, originY = 0.0;
        double                          originZ = 0.0;
        int                             page    = -1;
        const gfx::SdfAtlas*            atlas   = nullptr;
        std::unique_ptr<gfx::GpuBuffer> vb, ib;
        std::uint32_t                   count = 0;
        double                          minX = 0.0, minY = 0.0, maxX = 0.0,
                                        maxY = 0.0;
        double                          minZ = 0.0, maxZ = 0.0;
        float                           reach = 0.0f;   // körningens enhet
        float                           reachEm = 0.0f; // samma i em (golvet)
    };
    struct GlyphBuild {
        std::vector<GlyphGpu>  chunks;
        std::vector<LabelInfo> labels;
        std::uint64_t          serial = 0;   // löpnummer: ett nytt bygge är
                                             // nya etiketter (placeringen)
    };
    // Det fönstret behöver veta för att bygga en Glyph-körning.
    struct GlyphEnv {
        float pxPerPx        = 1.0f;   // skalan
        float pxPerMm        = 1.0f;
        float pxPerWorldUnit = 1.0f;   // kamerans i mitten
        // TERRÄNGEN: satt för ett Ground-lagers stående
        // etiketter när en terräng ritas — varje ankare får terrängens höjd
        // under sig (drape.h `terrainHeight`, 0 utanför) och byggs i rymdens
        // chunkar (Glyph3). REGIONEN under körningens ankarlåda (drape.h
        // `terrainRegionKey`) är en del av byggets nyckel: en ny
        // platta bygger om bara körningarna den når.
        const detail::SceneData* terrain    = nullptr;
        std::uint64_t            terrainKey = 0;
    };
    // Körningens text byggd för det här fönstret (byggs vid behov).
    const GlyphBuild& glyphs(const std::shared_ptr<detail::RunData>& run,
                             const GlyphEnv& env);
    // Bygget SOM DET RITADES senast — bara om det byggdes ur körningens
    // nuvarande version (`RunData::glyphVersion`), annars nullptr. Bygger
    // aldrig: träffprovet frågar utanför en frame, där en atlas inte får
    // laddas upp.
    const GlyphBuild* findGlyphs(const detail::RunData& run) const;

    // ---- ETIKETTERNA
    // En ritad Glyph-körning i placeringen: var dess etiketter börjar i
    // synlighetsbilden och vad som avgör dem.
    struct LabelRun {
        std::uint64_t run     = 0;      // RunData::id
        std::uint64_t serial  = 0;      // byggets löpnummer
        std::uint32_t base    = 0;      // första texeln
        std::uint32_t count   = 0;      // etiketter
        int           layer   = 0;      // lagrets plats (placeringens ordning)
        bool          collide = true;   // lagrets `setLayerCollision`

        bool operator==(const LabelRun&) const = default;
    };
    // Placeringen för EN rityta och PLATS i det här fönstret — platsen är
    // vyns nummer (huvudvyn 0, del-vyerna 1 …) eller
    // bildens (`detail::kBitmapLabels`, renderToBitmap): varje kamera
    // placerar för sig. INDATAN (kameran, tätheten och körningarna) sparas,
    // så en ritning med samma indata — en partiell omritning, två
    // damage-ytor i en frame — inte placerar eller laddar upp igen.
    struct LabelState {
        std::weak_ptr<const detail::SceneData> src;
        bool                  placed  = false;
        gfx::MapView          view{};
        std::optional<gfx::View3D> view3D;   // 3D-målets vy
        float                 pxPerPx = 0.0f, pxPerMm = 0.0f;
        std::vector<LabelRun> runs;
        // De kolliderande etiketternas texlar i PLACERINGENS ORDNING
        // (prioritet, lager, index) — beror inte på kameran, så den räknas
        // när `runs` ändras och inte vid varje placering.
        std::vector<std::uint32_t> order;
        // SVARET: målet (0 dold, 255 synlig) och det bilden BÄR (tonas mot
        // målet när ritytan tonar, annars samma).
        std::vector<std::uint8_t>   target, shown;
        std::uint64_t               fadeMs = 0;   // senaste tonsteget
        std::unique_ptr<gfx::Image> tex;   // nullptr = ingen etikett kolliderar
    };
    // Bildens bredd i texlar; raderna växer med antalet etiketter.
    static constexpr int kLabelTexWidth = 256;
    LabelState&       labelState(const std::shared_ptr<detail::SceneData>& scene,
                                 int slot = detail::kMainView);
    const LabelState* findLabelState(std::uint64_t sceneId,
                                     int slot = detail::kMainView) const;
    // Laddar upp `shown[lo, hi)` till bilden — den skapas (eller ersätts av
    // en med fler rader) när den saknas eller är för liten, och då laddas
    // allt upp. Bara de byte som ändrats går till GPU:n: högst hi − lo.
    void uploadLabels(LabelState& s, std::size_t lo, std::size_t hi);
    // Ritytan har ingen kolliderande etikett: bilden släpps (efter framen).
    void dropLabels(LabelState& s);

    // ---- ATLASERNA, RAMPERNA, RUTNÄTET
    gfx::SdfAtlas*    atlas(gfx::FontFamily family);
    const gfx::Image* ramp(const std::vector<GradientStop>& stops);
    struct GridMesh {
        std::unique_ptr<gfx::GpuBuffer> vb, ib;
        std::uint32_t                   count = 0;
    };
    // Linjerna x = k och y = k för |k| ≤ `kGridHalfLines`, mellan −och +
    // samma tal, i färgen och bredden (vertexens; enheten är anropets).
    static constexpr int kGridHalfLines = 512;
    const GridMesh* grid(gfx::Color color, float width);
    // MARKPLANET: enhetsrutan [−1, 1]² i planet som en
    // Fill-yta, en gång per fönster — 3D-målet ritar den skalad kring ögat
    // (target3d.cpp). nullptr när buffertarna inte gick att skapa.
    const GridMesh* groundPlane();

    // ---- 3D-MÅLEN — ett per (rityta, vy).
    // Det cachen jämför (target3d.cpp): vad målet senast RITADES med. Målet
    // är W × H fysiska px med djup och provtagningen `samples`.
    struct Target3D {
        std::weak_ptr<const detail::SceneData> src;
        std::unique_ptr<gfx::RenderTarget>     target;
        int                                    w = 0, h = 0, samples = 0;
        bool                                   drawn = false;   // bär en ritning
        std::uint64_t                          stamp = 0;       // damageStamp
        gfx::Rectf                             box{};           // ritytans ruta
        float                                  pxPerPx = 0.0f, pxPerMm = 0.0f;
    };
    // Målet för ritytan och vyn — skapat, eller ersatt (och då oritat) när
    // måtten eller provtagningen ändrats. `target` är ogiltigt när målet inte
    // gick att skapa.
    Target3D& target3D(const std::shared_ptr<detail::SceneData>& scene,
                       int view, int w, int h, int samples);
    // Ritytan har inga vyer från `firstView` och uppåt:
    // deras mål släpps (efter framen), och del-vyernas etikettplacering —
    // huvudvyns placering hör också till 2D-vägen och bildens till
    // renderToBitmap, så de står kvar. 0 = ritytan ritar i 2D igen.
    void          dropViews(std::uint64_t sceneId, int firstView);
    std::size_t   targetCount() const { return targets_.size(); }
    // Hur många gånger ett 3D-mål RITATS (inte kompositats) — 3D-cachens
    // räknare (scene3d_pixel_gate G3).
    std::uint64_t targetRenders() const { return targetRenders_; }
    void          noteTargetRender() { ++targetRenders_; }

    // ---- EGNA PROGRAM — ritytans `glaze::Program`
    // kompilerade för fönstrets renderer, ETT per programversion
    // (scene.h `ProgramSlot`): latent första gången det frågas efter —
    // förberedelsen frågar före målet, så kompileringen aldrig sker inne i ett
    // mål — och kvar tills programmet ersätts eller ritytan rivs (livstecknet,
    // släppt i `trim`). Det kompilerade programmet, eller nullptr när det inte
    // kompilerade (backenden har loggat felet, en gång).
    const gfx::ShaderProgram* program(const detail::ProgramSlot& slot);
    // Programmets fel — kompilatorns text med raden i appens EGEN källa, en
    // saknad källa för backenden eller en ogiltig beskrivning; tomt för ett
    // giltigt program. Kompilerar det om det inte gjorts.
    std::string   programError(const detail::ProgramSlot& slot);
    std::size_t   programCount() const { return programs_.size(); }

    // ATLASERNAS EPOK — stegas när atlaserna töms över taket (`trim`), och
    // varje textbygge blir då inaktuellt. Draperingens markstämpel
    // (drape.cpp) läser den.
    std::uint64_t atlasEpoch() const { return atlasEpoch_; }

    // ---- räknarna (grindarna)
    std::size_t   chunkCount() const { return chunks_.size(); }
    std::size_t   glyphBuildCount() const { return glyphs_.size(); }
    std::uint64_t glyphBuilds() const { return glyphBuildsMade_; }
    std::uint64_t labelPlacements() const { return placements_; }
    void          notePlacement() { ++placements_; }

private:
    gfx::Renderer* r_;

    struct ChunkEntry {
        std::weak_ptr<const detail::ChunkData> src;
        GpuChunk                               gpu;
        std::uint64_t                          version = 0;
    };
    std::unordered_map<std::uint64_t, ChunkEntry> chunks_;

    struct GlyphEntry {
        std::weak_ptr<const detail::RunData> src;
        std::uint64_t                        version = 0;
        float                                pxPerPx = 0.0f, pxPerMm = 0.0f;
        std::uint64_t                        atlasEpoch = 0;
        bool                                 onTerrain  = false;
        std::uint64_t                        terrain    = 0;       // regionen
        GlyphBuild                           build;
    };
    std::unordered_map<std::uint64_t, GlyphEntry> glyphs_;
    std::uint64_t                                 glyphBuildsMade_ = 0;

    // Per (rityta, plats) — se `LabelState`.
    std::map<std::pair<std::uint64_t, int>, LabelState> labels_;
    std::uint64_t                                       placements_ = 0;

    void buildGlyphs(const detail::RunData& run, const GlyphEnv& env,
                     GlyphBuild& out);
    // En byggd chunk (`gfx::GlyphChunk` eller `GlyphChunk3`) till GPU:n.
    template <class Chunk>
    void uploadGlyphChunk(const Chunk& c, const gfx::SdfAtlas* at,
                          GlyphBuild& out);

    // 3D-målen per (rityta, vy) och hur många gånger de ritats.
    std::map<std::pair<std::uint64_t, int>, Target3D> targets_;
    std::uint64_t                                     targetRenders_ = 0;
    std::unique_ptr<GridMesh>                         groundPlane_;

    // De egna programmen per version — släppta när
    // programmets livstecken dött.
    struct CustomProgram {
        std::weak_ptr<const char>           life;
        std::unique_ptr<gfx::ShaderProgram> program;
    };
    std::unordered_map<std::uint64_t, CustomProgram> programs_;
    CustomProgram& customProgram(const detail::ProgramSlot& slot);

    // Föremålen — `objects` — och terrängens
    // draperingar — `drapes`.
    std::unique_ptr<detail::ObjectResidency> objects_;
    std::unique_ptr<detail::DrapeCache>      drapes_;

    // [familj] (UI, Mono); tömda (nollställda) över taket i `trim`.
    std::array<std::unique_ptr<gfx::SdfAtlas>, 2> atlases_{};
    std::uint64_t                                 atlasEpoch_  = 1;
    bool                                          atlasLogged_ = false;

    // Ramperna i LRU-ordning (främst = senast använd).
    struct RampEntry {
        std::string                 key;
        std::unique_ptr<gfx::Image> image;
    };
    std::list<RampEntry> ramps_;

    std::unique_ptr<GridMesh> grid_;
    gfx::Color                gridColor_{};
    float                     gridWidth_ = 0.0f;

    // Det som ersatts MITT I en frame (en buffert som växte, ett textbygge
    // som byggdes om, en ramp som föll ur taket) lever till nästa `trim` —
    // ett rit-anrop tidigare i framen kan peka på det.
    std::vector<std::unique_ptr<gfx::GpuBuffer>> retiredBuffers_;
    std::vector<GlyphBuild>                      retiredBuilds_;
    std::vector<std::unique_ptr<gfx::Image>>     retiredImages_;
    std::vector<std::unique_ptr<GridMesh>>       retiredGrids_;
    std::vector<std::unique_ptr<gfx::RenderTarget>> retiredTargets_;

    bool reserveAndUpload(std::unique_ptr<gfx::GpuBuffer>& buf,
                          std::size_t& cap, gfx::BufferKind kind,
                          const void* data, std::size_t bytes);
};

} // namespace glaze
