#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>

#include <gfx/camera.h>
#include <gfx/color.h>
#include <gfx/geometry.h>

#include "glaze/canvas/bitmap.h"
#include "glaze/canvas/camera.h"
#include "glaze/canvas/style3d.h"   // View
#include "scene.h"

// INTERN — RITYTANS 3D-MÅL. Ritytan ritar sin scen i ett EGET render-mål
// med djup (D32F) och
// MSAA när den har ett World- eller Terrain-lager, en `Camera3D`,
// `setMultisample(> 1)` eller `setDepth(true)` (canvas.h) — annars 2D-vägen,
// oförändrad. Bildrutan:
//
//   FÖRBERED (utanför målet — allt som laddar upp sker här)
//     de märkta lagren byggs om, scenens låda ger 3D-vyns klipplan, chunkarna
//     synkas, bilderna, ramperna, textbyggena, etikettplaceringen (med
//     3D-vyn), rutnätets mesh och markplanet
//   RITA (bara när cachen är inaktuell, se nedan) — pushTarget(mål, genomskinligt)
//     1  HELSKÄRMSPASSEN (`Shape::screenPass`) i lagrens ordning,
//        utan djup — en himmel ligger under allt
//     2  MARKEN: rutnätet och Ground-lagrens bilder/ytor/linjer, utan djup —
//        och deras MÅLARE (`onPaintLayer`) i lagrens ordning
//     3  MARKDJUPET: ett plan z = 0 kring ögat, 2 × far stort, som bara
//        skriver djup (Blend::DepthOnly) och är förskjutet BAKÅT med en
//        djupbias — det som ligger PÅ marken (2D-former i ett World-lager,
//        en yta i rymden på z = 0) ska vinna Less mot det, det som ligger
//        under döljs. Bara när ett ritat Ground-lager MÅLAR MARKEN (scene.h
//        `paintsGround` — ritytans lager 0 finns alltid, och ett tomt eller
//        ett med bara stående etiketter gör ingen mark) och ingen terräng
//        ritas. Det som STÅR på marken — de skärmvända primitiverna:
//        punkterna, linjebanden i px och markörerna och texten — skär planet
//        aldrig: deras hörn får planets djup genom ankaret där det ligger
//        närmare (förspelets `glaze_standingZ`, gfx/scene.h `MeshProgram`).
//        Priset är skrivet: draget är högst ungefär en fjärdedel av ankarets
//        djup och inget alls när planet nästan går genom ögat, och
//        punkterna dras dessutom högst fyra radier i världen
//        (programs3d.cpp). Bakom en mesh alldeles intill kan primitivens
//        nedre kant titta fram så mycket; vid horisonten kan en markör på
//        marken skäras av markdjupet med några px kvar.
//     4  WORLD-LAGREN i ordning, med sitt renderläge (style3d.h) — och deras
//        målare med samma läge som start
//     5  Ground-lagrens ETIKETTER, utan djup — ÖVERST (MapLibres regel;
//        en etikett som ska kunna skymmas läggs i ett
//        World-lager)
//     6  `onPaint` (målaren i världen, utan djup)
//     popTarget → MSAA-upplösningen
//   DEL-VYERNA — varje vy i ett EGET mål med eget
//     djup och egen MSAA, med SAMMA ordning (2–5) för de lager som ritas i
//     vyn (`LayerData::views`) och vyns kamera, och målet rensat till vyns
//     bakgrund. Rutnätet, `onPaint` och lagrens målare hör till huvudvyn.
//   KOMPOSIT (varje gång) — målets bild 1:1 på ritytans pixlar, med tonen som
//     opacitet, under det klipp ritytan har satt (det rundade): bilden är
//     premultiplicerad och `drawImage` av-premultiplicerar den. Sedan
//     vyernas mål i nummerordning, var och en på sin ruta — den högsta
//     överst.
//
// Terrain ritas som World tills draperingen finns.
//
// CACHEN: målet ritas om bara när ritytans `Widget::damageStamp`
// ändrats (dess egna `invalidate()` och fönstrets fulla invalideringar — en
// körd handler får ha ändrat vad `onPaint` ritar), när rutan, skalan eller
// mm-tätheten ändrats, eller när målet är nytt. En barnwidget som
// invaliderar sig själv ger bara kompositen. Räknaren är
// `CanvasResources::targetRenders()` — varje vys mål räknas. Samma regel för
// varje vy (med vyns ruta): ritytans sättare — också `setView` och
// kamerorna, som en vy med `followRotation` följer — invaliderar ritytan.
//
// MÅLETS PIXLAR: målet börjar på ritytans heltalshörn (`floor` av rutans
// övre vänstra hörn) och slutar på dess heltalsslut — scenen ritas med vyns
// ruta förskjuten därefter (`targetBox`), så den hamnar på samma pixlar som i
// 2D-vägen och kompositen är en ren kopia.

namespace glaze::detail {

// Ritytans ruta (fönstrets fysiska px) i MÅLETS px — samma mått, hörnet
// förskjutet med målets heltalsorigo.
gfx::Rectf targetBox(const gfx::Rectf& box);

// PROVTAGNINGEN (`Canvas::setMultisample`): förvalet 4× när 3D-målet används
// av något annat skäl, och de giltiga talen 1, 2, 4 och 8.
int  defaultMultisample();
bool validMultisample(int samples);

// Det 3D-målet behöver av ritytan utöver ritningens läge.
struct Target3DArgs {
    std::uint64_t   stamp   = 0;         // Widget::damageStamp — cachens nyckel
    int             samples = 1;         // begärd provtagning (1 = ingen MSAA)
    const Camera3D* camera  = nullptr;   // satt: 3D-kameran; annars kartkamerans
                                         // `toView3D` över scenens z-spann
    bool            labelFade = false;   // Canvas::setLabelFade
    // Rutnätet (Canvas::setGridSize) — ritas först på marken.
    double          gridSize = 0.0;
    gfx::Color      gridColor{};
    float           gridMinPx = 0.0f, gridWidthPx = 0.0f;
    // `onPaint`, anropad med målets ritläge (målaren i världen, i målet).
    std::function<void(PaintContext&)> paint;
    // `onPaintLayer`: lagrens målare i steg 2 och 4 (scene.h
    // `LayerPaint`) — tom utan handler.
    LayerPaint layerPaint;
    // Del-vyerna — vy 1 först.
    std::span<const View> views;
};

// Det en ritning av målen lämnar efter sig — båda ber om nästa bild (ritytan
// invaliderar sig själv, Progress-mönstret, regel 1): etiketterna TONAR, och
// draperingar VÄNTAR på nästa bilds budget (drape.h — innehållet släpar
// några bilder under en stor ändring; en dold platta väntar aldrig).
struct Target3DState {
    bool labelsAnimating = false;
    bool drapesPending   = false;
};

// Ritar scenen i ritytans 3D-mål — och varje del-vy i sitt — om cachen är
// inaktuell, och kompositar målen i fönstret. `window` är ritningens läge i
// fönstrets px (kompositen, tonen); `target` samma rityta med rutan i målets
// px (`targetBox`) — dess 3D-vy och opacitet sätts här.
Target3DState drawTarget3D(const PaintContext& window, PaintContext& target,
                           const std::shared_ptr<SceneData>& scene,
                           const Target3DArgs& args);

// HUVUDVYNS 3D-VY i kartvyns ruta (`world.box`): 3D-kamerans när `camera` är
// satt, annars kartkamerans `toView3D` över huvudvyns z-spann — den vy
// 3D-målet ritar med, och som en del-vy med `followRotation` vrids som.
// Klipplanen ur huvudvyns lager (scene.h `sceneBox`).
gfx::View3D mainView3D(const Camera3D* camera, const gfx::MapView& world,
                       const SceneData& scene);

// ----------------------------------------------------------- DEL-VYERNA
// — EN definition av en vys ruta och kamera, delad av ritningen
// här och träffprovets routning (canvas.cpp).

// En vy ritytan tar emot: ankaret ett av de nio, förskjutningen ändlig,
// måtten ändliga och ≥ 0, kameran giltig (camera_math `validCamera3D`).
bool validView(const View& v);
// Vyns ruta i fönstrets FYSISKA px ur ritytans ruta `box` och skalan —
// ankaret, förskjutningen och måtten (0 = ritytans hela) som style3d.h säger.
// Kan sticka ut ur ritytan; det som syns är snittet med `box`.
gfx::Rectf viewRect(const View& v, const gfx::Rectf& box, float scale);
// Vyns kamera. Med `followRotation` vriden som huvudvyn: en 3D-kameras
// (`main3D`) azimut, elevation, roll och upp-axel rakt av, annars
// huvudvyns 3D-vy (`mainView` — kartkameran i 3D-målet) genom camera_math
// `orientedAs`. Utan någon av dem: vyns egen.
Camera3D viewCamera(const View& v, const Camera3D* main3D,
                    const gfx::View3D* mainView);
// Den ÖVERSTA del-vyn under den fysiska fönsterpunkten (px, py) — vyerna
// ritas i nummerordning, så den med högst nummer vars synliga ruta (vyns ∩
// ritytans) innehåller punkten — eller `kMainView`.
int viewAt(std::span<const View> views, const gfx::Rectf& box, float scale,
           double px, double py);
// Vyns 3D-vy i rutan `rect` — fönstrets px för träffprovet, målets för
// ritningen — med klipplanen ur de lager som ritas i vyn (scene.h
// `sceneBox`), så en liten kub aldrig får en SWEREF-kartas djupintervall.
gfx::View3D viewView3D(const Camera3D& cam, const gfx::Rectf& rect,
                       const SceneData& scene, int view);

// --------------------------------------------------------------- BILDEN
// `Canvas::renderToBitmap`: HUVUDVYNS lager med kameran
// `*args.camera` i ett eget mål w × h px (fönstrets fysiska px — mått i px
// och mm blir lika stora som på skärmen), rensat till `background`, i
// bildrutans ordning med rutnätet och `onPaint` — UTANFÖR en frame
// (`beginOffscreen`), också med fönstret dolt — och läst tillbaka
// (`readTarget`). `canvas` är ritytans ritläge med fönstrets Ctx (renderaren,
// bildcachen, skalan, mm) — dess resurser, ljus och y-riktning gäller bilden.
// Etiketterna placeras i bildens egen plats (`kBitmapLabels`), så skärmens
// placering står orörd. Draperingarna ritas KOMPLETTA (drape.h
// `DrapeMode::Complete`, utan budget) — en bild släpar aldrig. Tomt (+ logg)
// inuti en frame, när målet inte går att skapa eller inte går att läsa.
std::optional<Bitmap> renderBitmap3D(const PaintContext& canvas,
                                     const std::shared_ptr<SceneData>& scene,
                                     const Target3DArgs& args, int w, int h,
                                     gfx::Color background);

} // namespace glaze::detail
