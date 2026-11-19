#pragma once
// EN BILD AV RITYTAN SOM ETT JOBB — INTERN. Ritytan ritas inte i UI-tråden:
// `Canvas::draw` beskriver bilden som ett `CanvasFrameJob` — allt ritningen
// behöver, som VÄRDEN och frysta data — och renderingstråden
// (render_loop.h) ritar den i en delad bild med `renderCanvasFrame`.
//
// JOBBET ÄR TRÅDNEUTRALT: scenen är en ögonblicksbild (scene.h
// `publishScene`), appens målare är inspelade (paint_list.h), temat och
// stilen är kopior, och budgetarna är tal. Ingenting i det pekar på en
// widget, på appen eller på fönstrets renderare.
//
// SVARET (`CanvasFrameResult`) går tillbaka med bilden: om ritytan vill ha
// en bild till (toningen, draperingar eller uppladdningar som väntar), minnet
// och det träffprovet läser om etiketterna.

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gfx/camera.h>
#include <gfx/color.h>
#include <gfx/geometry.h>
#include <gfx/image.h>

#include "../memory_ledger.h"
#include "../profiler_hooks.h"
#include "glaze/canvas/bitmap.h"
#include "glaze/canvas/camera.h"
#include "glaze/canvas/style3d.h"
#include "glaze/style.h"
#include "glaze/theme.h"
#include "paint_list.h"
#include "resources.h"
#include "scene.h"

namespace glaze::detail {

struct CanvasFrameJob {
    // BILDEN: löpnumret (växande per rityta — det senaste vinner) och måtten
    // i fysiska px. Ritytans ruta i bilden börjar i (`originX`, `originY`):
    // rutans bråkdel i fönstret, så scenen hamnar på samma pixlar som när den
    // ritades direkt (target3d.h `targetBox`).
    std::uint64_t serial = 0;
    int           width = 0, height = 0;
    gfx::Rectf    box{};   // ritytans ruta i BILDENS px

    // SCENEN och appens målare.
    std::shared_ptr<SceneData>            scene;
    std::shared_ptr<const PaintRecording> paints;

    // RITLÄGET — det `Canvas::paintContext` räknar, för `box`.
    gfx::MapView world;
    gfx::MapView screen;
    double       zoom = 1.0;
    Lighting     lighting;

    // MILJÖN — fönstrets, som kopior.
    float          scale   = 1.0f;
    float          pxPerMm = gfx::kAssumedPxPerMm;
    UI             ui      = UI::Slate;
    Theme::Variant theme;
    Style          style;

    // VÄGEN: 3D-målet (target3d.h) eller 2D-vägen.
    bool                    target3D = false;
    std::optional<Camera3D> camera3;        // satt: 3D-kameran
    int                     samples = 1;
    std::uint64_t           stamp   = 0;    // 3D-målens cachenyckel
    std::vector<View>       views;
    bool                    labelFade = false;
    double                  gridSize  = 0.0;
    gfx::Color              gridColor{};
    float                   gridMinPx = 0.0f, gridWidthPx = 0.0f;

    // BUDGETARNA (`App::canvasMemoryBudget`, `canvasUploadBudget`) — tal,
    // så ritningen aldrig frågar appen.
    std::size_t memoryBudget = 0;
    std::size_t uploadBudget = 0;

    // BILDTAKTEN (`Canvas::setFramePacing`): tiden en scenbild får ta, i ms.
    // 0 = bilden ritas alltid hel. Med en tid ritas bilden i STEG
    // (render_loop.h): under rörelse det som ryms, störst på skärmen först,
    // och med stilla kamera fylls resten på i de följande stegen.
    double frameBudgetMs = 0.0;
    // Huvudvyns 3D-vy för jobbet — det renderingstråden jämför för att se om
    // två jobb visar samma vy. Satt för ett jobb med 3D-mål.
    std::optional<gfx::View3D> paceView;
};

struct CanvasFrameResult {
    std::uint64_t serial = 0;
    bool          drawn  = false;   // bilden ritades (annars gick målet inte att ta)
    // Bilden visar ALLT som syns. Falskt för en bild av bildtakten som
    // fortfarande fylls på — renderingstråden ritar då vidare av sig själv.
    bool          complete = true;
    // Ritytan vill ha EN BILD TILL (regel 1 — Progress-mönstret): etiketterna
    // tonar, draperingar väntar på nästa bilds budget, eller en uppladdning
    // väntar på framens.
    bool labelsAnimating = false;
    bool drapesPending   = false;
    bool uploadsDeferred = false;
    // Scenen har en bild som bara UI-tråden kan hämta (en `RenderTexture` —
    // resources.h `refuseTexture`): bilden visas inte, och ritytan ritar
    // scenen själv i fönstrets tråd från och med nu.
    bool needsUiThread = false;
    // Ritytans konto efter bilden (`Canvas::memoryUsed`).
    MemoryUse memory;
    // Det träffprovet läser (resources.h `LabelExport`).
    CanvasResources::LabelExport labels;
    // Renderingstrådens faser och räknare för bilden (profiler_hooks.h) —
    // tomt när profileraren är av.
    ThreadFrameProfile profile;

    bool wantsMore() const
    {
        return labelsAnimating || drapesPending || uploadsDeferred;
    }
};

// BILDTAKTENS TAL (`Canvas::setFramePacing`): förvalet för en satt takt,
// prövningen av en takt, och tiden en bild får ta vid takten `hz` (ms).
double defaultFrameRate();
bool   validFrameRate(double fps);
double frameBudgetFor(double hz);
// Skärmens bildtakt vid punkten `p` i placeringsrymden (`gfx::screens()`) —
// förvalet när skärmen inte säger sin. Frågan till skärmarna ställs högst en
// gång i sekunden; däremellan svarar `cache`.
struct RefreshCache {
    double        hz    = 0.0;
    std::uint64_t at    = 0;
    bool          valid = false;
};
double refreshRateAt(RefreshCache& cache, gfx::Pointf p);

// Hur många lämnade jobbs kameror en rityta minns i väntan på deras bilder
// (den visade bildens kamera placerar de förankrade barnen). Över taket
// släpps de äldsta — deras bilder ersattes av nyare jobb.
inline constexpr std::size_t kFrameCameraHistory = 64;

// BILDENS PLATS för en rityta med rutan `windowBox` (fönstrets fysiska px):
// bilden börjar på rutans heltalshörn (`x`, `y`) och når dess heltalsslut,
// och `box` är rutan i bildens px (target3d.h `targetBox`) — så scenen
// hamnar på samma pixlar som när den ritas direkt i fönstret.
struct CanvasFrameBox {
    gfx::Rectf box{};
    int        width = 0, height = 0;
    float      x = 0.0f, y = 0.0f;
};
CanvasFrameBox canvasFrameBox(const gfx::Rectf& windowBox);

// RITAR JOBBET i `out` — ett mål med jobbets mått, rensat genomskinligt.
// `c` är ritningens Ctx: scen-renderaren, DESS bildcache och ritytsresurser
// (`c.r`, `c.images`, `c.canvas`) — resten fyller funktionen ur jobbet. Ett
// offscreen-block (eller en frame) är öppet på renderaren. Tonen (en
// inaktiverad rityta) ingår inte: den är kompositens.
// `pace` är bildtaktens steg (scene.h `FramePace`) — nullptr ritar bilden
// hel. Ett steg i ritordning som inte avslutar passet ritar bara i ritytans
// egna mål: `out` rörs inte och svaret är inte `drawn`.
CanvasFrameResult renderCanvasFrame(const CanvasFrameJob& job, const Ctx& c,
                                    gfx::RenderTarget& out,
                                    FramePace* pace = nullptr);

// RITAR JOBBET SOM EN BILD AV PIXLAR (`Canvas::renderToBitmap`): huvudvyns
// lager med jobbets `camera3` i `width` × `height` px, rensat till
// `background`, läst tillbaka (target3d.h `renderBitmap3D`). `c` som ovan,
// men INGET offscreen-block får vara öppet — funktionen öppnar sitt eget.
// Tomt (+ logg) när bilden inte gick att rita eller läsa.
std::optional<Bitmap> renderCanvasBitmap(const CanvasFrameJob& job, const Ctx& c,
                                         gfx::Color background);

// EN SIDA SOM ETT JOBB (utskriften, `renderPageImage`): sidans målare,
// INSPELAD i UI-tråden (paint_list.h), och rastret den ritas i — vitt, i
// sidans mm med mitten i (`cx`, `cy`). Trådneutralt som bildens jobb.
struct PageJob {
    PaintList      paints;
    int            width = 0, height = 0;   // px
    double         pxPerMm = 0.0;
    double         cx = 0.0, cy = 0.0;      // områdets mitt i sidans mm
    float          scale = 1.0f;            // logiska px → px (målets täthet)
    UI             ui = UI::Slate;
    Theme::Variant theme;
    Style          style;
    bool           read = true;             // läs tillbaka bilden
    int            maxRounds = 1;           // varv tills ingen bild väntar
    std::size_t    memoryBudget = 0;
    std::size_t    uploadBudget = 0;
};

// Sidans ritläge i ett mål med jobbets mått — för inspelningen (UI-trådens
// Ctx) och för ritningen (`sc` är Ctx:t med sidans ruta och täthet).
Ctx          pageCtx(const PageJob& job, const Ctx& base);
PaintContext pagePaint(const PageJob& job, const Ctx& sc);

// RITAR SIDAN: om tills ingen bild väntar på uppladdningen (högst
// `maxRounds` varv — `complete` false när taket nåddes), i ett eget
// offscreen-block per varv; `rgba` fylls när jobbet ska läsas. `c` som för
// bildens jobb. false = ett fel (loggat).
bool renderPageJob(const PageJob& job, const Ctx& c,
                   std::vector<std::uint8_t>& rgba, bool& complete);

} // namespace glaze::detail
