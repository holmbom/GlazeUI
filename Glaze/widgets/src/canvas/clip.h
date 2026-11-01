#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include <gfx/geom2d.h>
#include <gfx/image.h>
#include <gfx/renderer.h>
#include <gfx/scene.h>

#include "glaze/canvas/geometry.h"

// INTERN — RITYTANS KLIPP SOM MASKER. Polygonerna (`ClipPolygon`, ritytans,
// lagrets och målarens) blir bilder i render-mål där ALFA är täckningen, och
// rit-anropen binder dem som `gfx::SceneClip` (gfx/scene.h). Två slag:
//
//   SKÄRMMASKEN   målets fysiska px. Klipp i `ClipSpace::Screen` (lokala
//                 logiska px) och — för det som ligger i planet z = 0, alltså
//                 allt i 2D och markens former i 3D — `ClipSpace::World`
//                 PROJICERAT genom ritningens kamera. Ytorna är projektiva
//                 bilder av världens, så en rät kant förblir rät; en kant som
//                 går bakom ögat klipps homogent.
//   VÄRLDSMASKEN  världens XY (PRISMA), bara i 3D-målet, för det som har
//                 höjd: Line3, Glyph3 och de 3D-program som anropar
//                 `glaze_clip`. Rutnätet täcker den synliga markytan (och den
//                 höjd scenen når) i en upplösning som följer skärmen.
//
// Ett rit-anrop ser EN `ClipLook`: `flat` för 2D-programmen (Line, Fill,
// Glyph, Image — skärmmasken med det projicerade världsklippet i) och `tall`
// för det med höjd (skärmmasken bara med skärmklippen, plus världsmasken).
// Utan 3D-vy är de lika.
//
// NÄSTLING: en mask för ett klipp som ligger inuti ett annat fylls medan
// förälderns mask är bunden som klipp — Fill-programmet multiplicerar då in
// den, och snittet kräver inget nytt blandningsläge. Barnets rutnät ligger
// på förälderns (samma skala, hela maskpixlar) och är aldrig större än dess.
// Flera ytor i ett klipp ritas i samma mask: en UNION.
//
// POOLEN (`ClipMasks`) bor per fönster, bredvid bilderna: en mask nycklas på
// polygonerna, rymden, kameran/rutnätet och förälderns nyckel, och byggs om
// bara när något av det ändras. En yta som inte går att triangulera strikt
// (en självkorsning, ett hål utanför) ger en TOM mask och en logg — aldrig
// ett oklippt.

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
// nycklar (nycklarna går in i en barnmasks egen).
struct ClipLook {
    const gfx::Image* screen = nullptr;
    MaskGrid          sg;
    std::uint64_t     sKey = 0;
    const gfx::Image* world = nullptr;
    MaskGrid          wg;
    std::uint64_t     wKey = 0;

    // Lägger maskerna i anropets `SceneClip`. (ox, oy) är chunkens (eller
    // ramens) origo i världen, i double.
    void apply(gfx::SceneClip& c, double ox, double oy) const;
};

// Klippet som gäller en ritning: det för 2D-programmen och det för det med
// höjd.
struct ClipScope {
    ClipLook flat, tall;

    // Rit-anropets klipp: `tallProgram` för Line3, Glyph3 och de egna 3D-
    // programmen.
    void apply(gfx::SceneClip& c, bool tallProgram, double ox, double oy) const
    {
        (tallProgram ? tall : flat).apply(c, ox, oy);
    }
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
    // Maskens bild (w × h px) för nyckeln: den som byggts i den här framen
    // eller en av de två förra, annars fylld nu — ytorna ur `areas` (i
    // maskens px) i en union, och multiplicerade med `parent` (bunden som
    // klipp med `parentRect`, förälderns ruta i den NYA maskens px). Ritar i
    // ett render-mål (`pushTarget`) — bara i en frame eller ett
    // `beginOffscreen`-block. Går målet inte att skapa svarar den med den
    // tomma masken; nullptr bara om inte ens den gick att skapa (loggat).
    const gfx::Image* mask(std::uint64_t key, int w, int h,
                           const gfx::Image* parent, const gfx::Rectf& parentRect,
                           const std::function<void(Areas&)>& areas);
    // Allt bortklippt: en mask 1 × 1 utan täckning.
    const gfx::Image* empty();
    // Släpper det som inte använts på två framer.
    void trim();

    // Hur många gånger en mask fyllts (grindarna).
    std::uint64_t fills() const { return fills_; }

private:
    struct Entry {
        std::uint64_t                      key  = 0;
        int                                w = 0, h = 0;
        bool                               built = false;
        std::uint64_t                      used = 0;   // framens löpnummer
        std::unique_ptr<gfx::RenderTarget> target;
    };
    Entry* take(std::uint64_t key, int w, int h);
    void   fill(Entry& e, const gfx::Image* parent, const gfx::Rectf& parentRect,
                const std::function<void(Areas&)>& areas);

    gfx::Renderer*                     r_;
    std::vector<std::unique_ptr<Entry>> entries_;
    std::unique_ptr<Entry>              empty_;
    std::uint64_t                       fills_ = 0;
};

// Hur ett klipp används: `Scene` är ritytans och lagrens; `Drape` är
// draperingens bakning uppifrån, där bara lagrens världsklipp gäller.
enum class ClipUse : std::uint8_t { Scene, Drape };

// Ritytans och lagrens klipp för ritningen `f` (kamera, mål, vy) —
// maskerna byggs här, UTANFÖR målet som ritas. Tomt (inga masker) när inget
// klipp är satt.
ClipFrame prepareClips(const PaintContext& f, const SceneData& scene, ClipUse use);

// Ett klipp till inuti `parent`: ytorna `polys` i rymden `space` (världen,
// eller ritningens lokala logiska px) snittas med det som gäller. Målarens
// `save(…)` och förberedelsen går båda hit. Ogiltiga ytor loggas och ger en
// tom mask för sin del.
ClipScope refineClip(const PaintContext& f, const ClipScope& parent,
                     ClipSpace space, std::span<const ClipPolygon> polys);

// Ritytans och lagrens klippversioner som EN nyckel — 3D-målets cache ritar
// om när den ändras.
std::uint64_t clipVersions(const SceneData& scene);

// ---- giltigheten (`InvalidClip`)

// Går ytan att fylla — ringarna en enkel yta med hål? Samma strikta
// triangulering som masken gör (`gfx::buildClipPolygon`).
bool clipPolygonValid(const ClipPolygon& p);
// Räknar om `k.valid` för ytorna [first, last) — sättarnas sista steg (de
// andra ytornas svar står kvar).
void revalidateClip(ClipState& k, std::size_t first, std::size_t last);
// Gäller klippet? Avslaget gäller det inte, och med `InvalidClip::Show`
// inte heller när någon av dess ytor är ogiltig.
bool clipApplies(const ClipState& k, InvalidClip mode);
// Klippets GILTIGA ytor: `k.polygons` själv när alla är giltiga, annars de
// giltiga i `tmp` (en ogiltig yta täcker ingenting — `InvalidClip::Hide`).
std::span<const ClipPolygon> validPolygons(const ClipState& k,
                                           std::vector<ClipPolygon>& tmp);

// ---- träffprovet (CPU)

// Ligger punkten i någon yta (en union), inte i dess hål?
bool clipContains(std::span<const ClipPolygon> polys, Point p);
// Rör rektangeln eller fyrhörningen `quad` (moturs eller medurs) ytorna?
bool clipTouches(std::span<const ClipPolygon> polys, std::span<const Point> quad);
// Samma frågor mot ett klipps GILTIGA ytor.
bool clipContains(const ClipState& k, Point p);
bool clipTouches(const ClipState& k, std::span<const Point> quad);

} // namespace glaze::detail
