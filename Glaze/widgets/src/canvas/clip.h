#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <gfx/geom2d.h>
#include <gfx/image.h>
#include <gfx/renderer.h>
#include <gfx/scene.h>

#include "glaze/canvas/geometry.h"
#include "target_pool.h"

// INTERN — RITYTANS KLIPP SOM MASKER. Polygonerna (`Polygon`, ritytans,
// lagrets och målarens) blir bilder i render-mål där ALFA är täckningen, och
// rit-anropen binder dem som `gfx::SceneClip` (gfx/scene.h). Två slag:
//
//   SKÄRMMASKEN   målets fysiska px. Klipp i `ClipSpace::Screen` (lokala
//                 logiska px), i 2D också `ClipSpace::World` PROJICERAT genom
//                 ritningens kamera, och i 3D-målet världsklippet för det som
//                 ritas i SKÄRMRYMDEN (`ClipScope::onScreen`). Ytorna är
//                 projektiva bilder av världens, så en rät kant förblir rät;
//                 en kant som går bakom ögat klipps homogent, och ett hål klipps
//                 en bit längre från ögat än sin ytterring, så att det aldrig
//                 når ytterringens kant där båda korsar ögats plan.
//   VÄRLDSMASKEN  världens XY (PRISMA), i 3D-målet för ALLT som ritas i
//                 världen: varje program läser den i sin världspunkt
//                 (`glaze_clip` — Line, Fill och Image i fragmentets punkt,
//                 Glyph och Glyph3 på ankaret, Line3, meshar, punkter och de
//                 egna programmen i sin). Rutnätet följer INTE vyn: det är
//                 klippets låda i en täthet som en tvåpotens (ur markskalan
//                 längst ner i bilden), och ryms lådan inte i
//                 `kMaxWorldMaskPx` den synliga markytan ur rutor om
//                 `kWorldTile` maskpx, i lägre täthet tills den ryms. En
//                 kamerarörelse bygger därför ingen ny mask, bara ett nytt
//                 oktavsteg eller en ny ruta gör det.
//
// Ett rit-anrop ser EN `ClipLook`: `flat` för 2D-programmen och `tall` för
// det med höjd. Utan 3D-vy är de lika och skärmens; i 3D-målet är de lika
// och bär skärmklippens skärmmask och världsklippens världsmask.
//
// STORLEKSKLASSER: en masks mått avrundas uppåt till en klass (en åttondel
// av måttets tvåpotens, minst `kMaskStep`), så ett mått som glider med
// kameran byter mål sällan och poolen kan ta ett mål av samma klass igen.
//
// ETT VÄRLDSKLIPP I 2D är en VÄRLDSMASK bunden som skärmmask när kartvyn är
// ortografisk och axelparallell (tilt 0 och bäring 0° eller 180° — bilden av
// världen är då x · skala + förskjutning per axel): masken byggs i världens
// XY i en täthet om minst en maskpx per skärmpx (2^⌈log₂ skalan⌉ per enhet),
// och bara rutan den binds med följer kameran — en panorering bygger ingen ny
// mask, en zoom bara när tätheten byter tvåpotens. Den gäller när klippets
// låda i den tätheten ryms i `kMaxWorldMaskPx` per sida och i
// `kMaxWorldMaskShare` gånger ytans px, och när föräldern inte har en
// skärmmask (eller själv är en sådan världsmask, på vars gitter barnet
// läggs). Annars — en vriden kamera, en låda som vid djup zoom hade krävt en
// större mask eller ett grövre gitter, ett världsklipp i ett skärmklipp —
// blir klippet en SKÄRMMASK som ovan, byggd om för varje kamera. Kanten blir
// aldrig grövre än skärmmaskens. (En LUTAD kartkamera ritar i 3D-målet.)
//
// NÄSTLING: en mask för ett klipp som ligger inuti ett annat fylls medan
// förälderns mask är bunden som klipp — Fill-programmet multiplicerar då in
// den, och snittet kräver inget nytt blandningsläge. Barnets rutnät ligger
// på förälderns (samma skala, hela maskpixlar) och är aldrig större än dess.
// Flera ytor i ett klipp ritas i samma mask: en UNION.
//
// POOLEN (`ClipMasks`) bor per fönster, bredvid bilderna: en mask nycklas på
// polygonernas KÄLLA (ett klipps innehåll, eller målarens hash över hörnen),
// rymden, kamerans parametrar, gittret och förälderns nyckel, och byggs om
// bara när något av det ändras. Nyckeln räknas FÖRST, utan att röra hörnen:
// en mask som redan finns — och dess rutnät — tas ur poolen, och ytorna
// projiceras bara när den saknas. En yta som inte går att triangulera strikt
// (en självkorsning, ett hål utanför) ger en TOM mask och en logg — aldrig
// ett oklippt. Lika innehåll är lika mask: åtta lager med samma yta delar EN
// mask (och i 3D en världsmask), och en sättare som ger lagret samma ytor
// igen ger tillbaka den gamla nyckeln. Nyckeln är en 64-bitars FNV-1a, samma
// som målarens hash över hörnen redan bär — två olika klipp med samma hash
// skulle dela mask, och oddsen för det är försumbara.

namespace glaze::detail {

struct PaintContext;   // scene.h
struct SceneData;      // scene.h
struct ClipState;      // scene.h

// En yta i maskens px: ytterringen först, sedan hålen.
using ClipRing = std::vector<gfx::Point2d>;
using ClipArea = std::vector<ClipRing>;

// Maskens rutnät: `s` maskpx per enhet i rymden (1 för skärmen), origo
// (x0, y0) i rymdens enhet och måtten i maskpx.
struct MaskGrid {
    double x0 = 0.0, y0 = 0.0;
    double s  = 1.0;
    int    w  = 0, h = 0;
};

// Vad ett rit-anrop binder: skärmmasken och världsmasken med sina rutnät och
// nycklar (nycklarna går in i en barnmasks egen). Skärmmasken binds med
// `sRect` (målets px; en negativ storlek speglar), och `sg` är dess gitter i
// målets px — det en skärmmask inuti den läggs på. En världsmask i 2D bunden
// som skärmmask (`planned`) har sitt gitter i världen i `plan`, med nyckeln
// `planKey`: ett världsklipp inuti den läggs på det.
struct ClipLook {
    const gfx::Image* screen = nullptr;
    MaskGrid          sg;
    gfx::Rectf        sRect{};
    std::uint64_t     sKey = 0;
    bool              planned = false;
    MaskGrid          plan;
    std::uint64_t     planKey = 0;
    const gfx::Image* world = nullptr;
    MaskGrid          wg;
    std::uint64_t     wKey = 0;

    // Lägger maskerna i anropets `SceneClip`. (ox, oy) är chunkens (eller
    // ramens) origo i världen, i double.
    void apply(gfx::SceneClip& c, double ox, double oy) const;
};

struct ClipSnapshot;   // nedan

// ETT STEG I KLIPPETS KEDJA i 3D-målet: ytorna, rymden och källan för ett
// klipp, och klippet det ligger inuti. Skärmrymdens innehåll (`onScreen`)
// bygger sin mask ur kedjan först när det ritas.
struct ClipStep {
    std::shared_ptr<const ClipStep>     parent;
    ClipSpace                           space = ClipSpace::World;
    std::shared_ptr<const ClipSnapshot> snapshot;
    std::uint64_t                       source = 0;
};

// Klippet som gäller en ritning: det för 2D-programmen och det för det med
// höjd. I 3D-målet är de LIKA — alla program läser världsmasken i sin
// världspunkt — och ett världsklipp projiceras aldrig till skärmen för det som
// ritas i världen.
struct ClipScope {
    ClipLook flat, tall;

    // Kedjan av klipp i 3D-målet (tom i 2D och utan klipp).
    std::shared_ptr<const ClipStep> chain;

    // Rit-anropets klipp: `tallProgram` för Line3, Glyph3 och de egna 3D-
    // programmen.
    void apply(gfx::SceneClip& c, bool tallProgram, double ox, double oy) const
    {
        (tallProgram ? tall : flat).apply(c, ox, oy);
    }

    // Klippet för det som ritas i SKÄRMRYMDEN — målarens `screenSpace` och
    // helskärmspassen. I 2D är det `flat`, som redan är skärmens. I 3D-målet
    // har sådant ingen världspunkt, så kedjans världsklipp projiceras genom
    // vyn till en skärmmask — byggd här, först när något ritas i skärmrymden,
    // och därefter ur poolen så länge vyn står still.
    ClipLook onScreen(const PaintContext& f) const;
};

// Ritytans klipp och varje lagers (snittet med ritytans) för en ritning.
struct ClipFrame {
    ClipScope              canvas;
    std::vector<ClipScope> layers;
    // Lagrets klipp, eller ritytans när lagret inte har något (eller inte
    // ritas).
    const ClipScope& of(std::size_t layer) const
    {
        return layer < layers.size() ? layers[layer] : canvas;
    }
};

// Poolen av masker per fönster.
class ClipMasks {
public:
    explicit ClipMasks(gfx::Renderer& r) : r_(&r) {}
    ~ClipMasks();
    ClipMasks(const ClipMasks&)            = delete;
    ClipMasks& operator=(const ClipMasks&) = delete;

    using Areas = std::vector<ClipArea>;

    // Det poolen minns om en nyckel: maskens rutnät och bild — eller att
    // nyckeln inte ger någon mask (allt bortklippt), eller ingen världsmask i
    // 2D (skärmmasken gäller).
    enum class Answer : std::uint8_t { Mask, Empty, Fallback };
    struct Found {
        Answer            answer = Answer::Empty;
        MaskGrid          grid;
        const gfx::Image* image = nullptr;   // Mask: maskens bild
    };
    // Svaret för nyckeln från den här framen eller en av de två förra, eller
    // inget — då räknar anroparen ut det.
    std::optional<Found> find(std::uint64_t key);
    // Nyckeln ger ingen mask (`Empty`) eller ingen världsmask (`Fallback`).
    void remember(std::uint64_t key, Answer a);

    // Maskens bild (rutnätet `g`, g.w × g.h px) för nyckeln: den som byggts i
    // den här framen eller en av de två förra, annars fylld nu — ytorna ur
    // `areas` (i maskens px) i en union, och multiplicerade med `parent`
    // (bunden som klipp med `parentRect`, förälderns ruta i den NYA maskens
    // px). Ritar i ett render-mål (`pushTarget`) — bara i en frame eller ett
    // `beginOffscreen`-block. Går målet inte att skapa svarar den med den
    // tomma masken; nullptr bara om inte ens den gick att skapa (loggat).
    const gfx::Image* mask(std::uint64_t key, const MaskGrid& g,
                           const gfx::Image* parent, const gfx::Rectf& parentRect,
                           const std::function<void(Areas&)>& areas);
    // Allt bortklippt: en mask 1 × 1 utan täckning.
    const gfx::Image* empty();

    // VÄRLDSMASKENS GITTER som senast användes för ett klipp utan förälder
    // (`key`: klippets källa och vyn) — `worldGrid` behåller det så länge det
    // täcker det som behövs i en täthet inom en oktav, så en kamerarörelse
    // inte ger ett nytt gitter. nullptr när inget finns.
    const MaskGrid* lastGrid(std::uint64_t key);
    void            rememberGrid(std::uint64_t key, const MaskGrid& g);

    // Går målarens yta att fylla? Svaret per ytans nyckel (`polygonKey`),
    // räknat med `valid` första gången och kvar så länge nyckeln används — en
    // rityta som målar samma klipp varje bildruta trianguleras inte varje
    // bildruta.
    bool valid(std::uint64_t key, const std::function<bool()>& valid);

    // Släpper det som inte använts på två framer.
    void trim();

    // Hur många gånger en mask fyllts (grindarna).
    std::uint64_t fills() const { return fills_; }
    // Hur många ytor som inte gått att fylla (loggade — de klipper bort sin
    // del; grindarna).
    std::uint64_t unfilled() const { return unfilled_; }
    // Hur många maskmål som lever, och deras px tillsammans (mätningarna).
    std::size_t   masks() const { return targets_.size(); }
    std::uint64_t maskPixels() const { return targets_.pixels(); }

private:
    struct Memo {
        std::uint64_t key    = 0;
        Answer        answer = Answer::Empty;
        MaskGrid      grid;
        std::uint64_t used = 0;   // framens löpnummer
    };
    struct Validity {
        bool          ok   = false;
        std::uint64_t used = 0;
    };
    struct GridMemo {
        MaskGrid      grid;
        std::uint64_t used = 0;
    };
    Memo& memo(std::uint64_t key);
    void  fill(TargetPool::Slot& s, const gfx::Image* parent,
               const gfx::Rectf& parentRect,
               const std::function<void(Areas&)>& areas);

    gfx::Renderer*                              r_;
    TargetPool                                  targets_;
    std::vector<Memo>                           memos_;
    std::unordered_map<std::uint64_t, Validity> validity_;
    std::unordered_map<std::uint64_t, GridMemo> grids_;
    std::unique_ptr<gfx::RenderTarget>          empty_;
    std::uint64_t                               fills_    = 0;
    std::uint64_t                               unfilled_ = 0;
};

// MASKENS STORLEKSKLASS: ett mått i maskpx avrundat uppåt till sitt
// klassteg (`sizeClassStep` — tvåpotensen under måttet delad med
// `kSizeClassDivisor`, minst `kMaskStep`), så ett mått som glider med kameran
// byter mål sällan. Över taket står måttet kvar. Grindarna läser regeln här.
int sizeClassStep(int v);
int sizeClass(int v);

// Klippets ytor och vilka av dem som ritas: alla i `polys` utom de `valid`
// säger nej till (tom = alla — `ClipState::valid`s form).
struct ClipPolys {
    std::span<const Polygon>      polys;
    std::span<const std::uint8_t> valid;
    bool drawn(std::size_t i) const { return i >= valid.size() || valid[i]; }
};

// ETT KLIPPS YTOR SOM ÄGT VÄRDE — det `refineClip` får och kedjan (`ClipStep`)
// håller: ritytans och lagrens (`ClipState::snapshot`, kopierad av sättaren)
// och målarens (`save(…)`). Delad och oföränderlig, så en kedja som lever
// genom en ritning läser samma ytor också om appen ändrar klippet under den.
struct ClipSnapshot {
    std::vector<Polygon>      polygons;
    std::vector<std::uint8_t> valid;   // `ClipState::valid`s form; tom = alla
    ClipPolys polys() const { return ClipPolys{polygons, valid}; }
};

// Hur ett klipp används: `Scene` är ritytans och lagrens; `Drape` är
// draperingens bakning uppifrån, där bara lagrens världsklipp gäller.
enum class ClipUse : std::uint8_t { Scene, Drape };

// Ritytans och lagrens klipp för ritningen `f` (kamera, mål, vy) —
// maskerna byggs här, UTANFÖR målet som ritas. Tomt (inga masker) när inget
// klipp är satt.
ClipFrame prepareClips(const PaintContext& f, const SceneData& scene, ClipUse use);

// Ett klipp till inuti `parent`: ytorna i `snapshot` i rymden `space`
// (världen, eller ritningens lokala logiska px) snittas med det som gäller.
// Målarens `save(…)` och förberedelsen går båda hit. `source` är ytornas
// IDENTITET i maskens nyckel — lika källa är lika ytor: ett klipps innehåll
// (`ClipState::content`, en hash över rymden och de ritade ytornas nycklar)
// eller målarens hash över hörnen. Ytor som inte går att fylla ger en tom
// mask för sin del, och utan ytor (nullptr) klipps allt bort.
ClipScope refineClip(const PaintContext& f, const ClipScope& parent, ClipSpace space,
                     const std::shared_ptr<const ClipSnapshot>& snapshot,
                     std::uint64_t source);

// Ritytans och lagrens klippversioner som EN nyckel — 3D-målets cache ritar
// om när den ändras.
std::uint64_t clipVersions(const SceneData& scene);

// ---- giltigheten (`InvalidClip`)

// Går ytan att fylla — ringarna en enkel yta med hål? Samma strikta
// triangulering som masken gör (`gfx::clipPolygonOk`).
bool clipPolygonValid(const Polygon& p);
// Ytans nyckel för `ClipMasks::valid` och målarens maskkälla: en hash över
// ringarnas hörn.
std::uint64_t polygonKey(const Polygon& p);
// Räknar om `k.valid` och `k.keys` för ytorna [first, last) och sedan
// `k.content` och `k.snapshot` över alla — sättarnas sista steg (de andra
// ytornas svar och nycklar står kvar). Ett tomt intervall,
// `revalidateClip(k, 0, 0)`, räknar bara om innehållet och ögonblicksbilden:
// det gäller en ändring av rymden.
void revalidateClip(ClipState& k, std::size_t first, std::size_t last);
// Gäller klippet? Avslaget gäller det inte, och med `InvalidClip::Show`
// inte heller när någon av dess ytor är ogiltig.
bool clipApplies(const ClipState& k, InvalidClip mode);
// Klippets GILTIGA ytor utan kopia: en ogiltig yta täcker ingenting
// (`InvalidClip::Hide`) och hoppas över.
ClipPolys validPolygons(const ClipState& k);

// ---- träffprovet (CPU)

// Ligger punkten i någon yta (en union), inte i dess hål?
bool clipContains(const ClipPolys& polys, Point p);
// Rör rektangeln eller fyrhörningen `quad` (moturs eller medurs) ytorna?
bool clipTouches(const ClipPolys& polys, std::span<const Point> quad);
// Samma frågor mot ett klipps GILTIGA ytor.
bool clipContains(const ClipState& k, Point p);
bool clipTouches(const ClipState& k, std::span<const Point> quad);

} // namespace glaze::detail
