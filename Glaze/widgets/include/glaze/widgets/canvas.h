#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gfx/camera.h>   // gfx::MapView — kameran i fasadens form
#include <gfx/events.h>
#include <gfx/window.h>   // gfx::DisplayMetrics

#include "glaze/canvas/bitmap.h"   // renderToBitmap
#include "glaze/canvas/camera.h"
#include "glaze/canvas/geometry.h"
#include "glaze/canvas/painter.h"
#include "glaze/canvas/shape.h"
#include "glaze/canvas/style3d.h"
#include "glaze/input.h"
#include "glaze/widget.h"

// Canvas — RITYTAN. En SCEN MED KAMERA: innehållet laddas upp till GPU:n i
// LAGER, och panorering, zoom,
// vridning och tilt är bara kameran — inget ritas om och inget laddas upp
// på nytt. Det som ändras varje ritning (en HUD, ett gummiband) ritas i
// `onPaint`/`onPaintLayer`/`onPaintOverlay` med en `Painter`.
//
//     karta.setYUp(true);                        // världens y uppåt (karta)
//     karta.setCenter(674100, 6580050);          // SWEREF-meter — double
//     karta.setMapScale(2000);                   // 1:2000 med linjal
//     int vagar = karta.addLayer("Vägar");
//     karta.addShape(vagar, glaze::Shape::polyline(
//         {{674000, 6580000}, {674150, 6580060}},
//         {.paint = gfx::Color{210, 70, 40}, .width = glaze::mm(0.8)}));
//
// MÖNSTER: widgeten ritar SIG SJÄLV — draw()/contentSize() bor i
// src/widgets/canvas.cpp (hård regel 3); scenens GPU-arbete bor i
// src/canvas/ och widgeten anropar det, så ingen `c.r` står i widget-filen.
//
// YTAN ÄR ETT FÄLT, LIKA I ALLA TRE RITNINGAR — ett beslut och inte en lucka:
// en rityta är innehåll, inte en kontroll med ett formspråk. Bakgrunden är
// fältets yta (`panel`, eller `setBackground`), kanten fältets (`border`),
// och en INAKTIVERAD rityta tonar sitt innehåll med 1 − `Style::iconFade` —
// samma ton som en inaktiv ikon, mot vilken bakgrund som helst. En PLATT
// rityta (`setFlat`) har varken kant eller rundade hörn och sin
// fokusmarkering innanför ytan — den står kant i kant med grannarna.
//
// LAGREN följer kollektionsvokabulären (hård regel 8): `addLayer`/
// `setLayer`/`setLayers`/`removeLayer`, och formerna `addShape`/`setShape`/
// `setShapes`/`removeShape` — med lagrets index först, eller utan, och då
// gäller lager 0. Lager 0 finns från start (namnlöst). Ett index utanför ⇒
// `gfx::log` och ingenting händer. `moveLayer(från, till)` byter lagrens
// ordning utan att något byggs om eller laddas upp — `till` räknas i
// ordningen före flytten, som `TreeView::moveNode`s index.
//
// KAMERAN: utan explicit kamera är världen ritytans lokala logiska px (samma
// rymd som `Draw`, (0, 0) i övre vänstra hörnet). Första sättaren (`setCenter`,
// `setZoom`, `setCamera` …) fryser den till en riktig kamera. Sättarna fyrar
// ALDRIG `onCameraChange` — den är användarens rörelser (gesterna).
//
// GESTERNA (MapLibres konventioner) är AV från start — en rityta
// som inte slagit på dem rör aldrig kameran själv. Tabellen nedan är
// FÖRVALET: gesterna är en BINDNINGSTABELL (läge, källa,
// modifierare) → (handling, riktning, fart) som appen ändrar, för hela appen
// (`App::setCanvasBinding`) eller per rityta (`setBinding`, se BINDNINGARNA
// nedan). Flaggorna säger OM en handling får verka, tabellen HUR:
//
//     vänsterdrag, mittendrag          panorera            setPannable
//     hjulet                           zoom mot pekaren    setZoomable
//     Ctrl+drag (vänster eller mitten) vrid (vågrätt) och  setRotatable
//                                      tilta (lodrätt)     setPitchable
//     pilarna / Shift+pilarna          panorera / vrid och tilta
//     + och −                          zoom kring mitten
//   STYRPLATTAN:
//     tvåfingersdrag                   panorera — exakt    setPannable
//                                      i px, OS:ets tröghet
//     nypning                          zoom mot pekaren    setZoomable
//     tvåfingervridning                vrid runt pekaren   setRotatable
//
//   MED EN Camera3D betyder samma fyra flaggor ORBITEN
//   kring kamerans mål — panorering, dolly, azimut och elevation:
//     vänsterdrag (också Ctrl+drag)    orbit: vågrätt azimut,  setRotatable
//                                      lodrätt elevation       setPitchable
//     mittendrag                       panorera — punkten i      setPannable
//                                      målplanet under pekaren
//                                      följer pekaren
//     Shift+mittendrag, Shift+drag     panorera i sidled, fram/bak
//                                      FLYGER längs blicken (Shift
//                                      läst per rörelse)
//     Ctrl+mittendrag                  orbit
//     hjulet, + och −                  dolly mot målet         setZoomable
//     pilarna / Shift+pilarna          flyg (upp = framåt) och
//                                      sidled / orbit
//     tvåfingersdrag (+ Shift)         som mittendraget (+ Shift) setPannable
//     Ctrl + tvåfingersdrag            orbit                   setRotatable …
//     nypning                          dolly                   setZoomable
//     tvåfingervridning                azimut                  setRotatable
//   Elevationen spärras till `setElevationRange` (förval ±85°) — utom med
//   `setOrbitTumble(true)`, där kameran går över polen utan att vända bilden
//   — och dollyn till `setDistanceRange`. En kamera som koden satt UTANFÖR
//   spannen dras aldrig in med ett hopp: en gest får föra den mot spannet men
//   inte längre ut. Orbitgesterna glider inte ut och snäpper inte.
//
// (Ctrl eller Cmd — båda räknas på alla plattformar.) STYRPLATTANS DRAG är
// ett PRECIST HJUL (`AppWindow::isPreciseWheel`) och ritytan panorerar på
// det i stället för att zooma: innehållet följer fingrarna px för px, också
// vridet och tiltat, och OS:ets egen tröghet panorerar vidare — ritytans
// tröghet används inte där. Nypningen och vridningen är fönstrets
// gestsekvens (`Widget::gesture`): världspunkten under pekaren står still,
// och kartan vrids MED fingrarna. APPENS HANDLER GÅR
// FÖRST: sätter `onMouseDown`/`onMouseMove`/`onMouseUp`/`onMouseWheel`/
// `onKeyDown`/`onGesture` `e.handled` gör ritytan ingenting med just den
// händelsen — ett tryck appen tog startar ingen panorering (`onKeyDown` är
// basens, `Widget::onKeyDown`, och tar tangenten före ritytans pilar). Ett
// AVBRUTET tryck (`onMouseCancel` — fokusförlusten, OS:et tog musen) stannar
// gesten utan tröghet. En kastad
// panorering GLIDER UT
// (`setInertia`, på), och en VRIDANDE gest — Ctrl+draget och styrplattans
// vridning — SNÄPPER när den släpps nära en vinkel i `setRotationSnap`s steg
// (förval: inom 7° från norr); båda invaliderar ritytan tills de är framme
// och STANNAR sedan (hård regel 1). Pekaren är ett hårkors över en rityta
// med gester, den STÄNGDA HANDEN medan den panoreras och en flytta-pekare
// medan den vrids.
//
// INTERAKTIONEN ÄR APPENS NÄR DEN VILL: gesterna är av
// från start, `onMouseDown`/`onMouseMove`/`onMouseUp` levererar vänster-,
// MITTEN- och HÖGERKNAPPEN och `onDoubleClick` alla tre
// (`e.button` — en handler som bara menar vänsterknappen prövar den), ett
// högertryck handlern TAR öppnar ingen snabbmeny (verktyget som drar med
// höger knapp), `onGesture` får styrplattans nypning och vridning före den
// inbyggda gesten, och `onTick` driver det som ska röra sig så länge en
// tangent hålls (förstapersonens WASD med `AppWindow::isKeyHeld`) eller
// `setTicking(true)` gäller. Widgets kan ligga PÅ ritytan som dess barn
// (ett fält vid en projicerad punkt): de klipps till ritytans rundade yta,
// tar fokus och tangenter som vanligt, och deras blinkande caret ritar inte
// om scenen.
//
// ETIKETTERNA KOLLIDERAR: en text eller markör vars låda
// krockar med en viktigare döljs — prioriteten (`TextStyle::priority`) först,
// därefter lagret som ritas överst, därefter ordningen. En etikett vars låda ligger
// HELT UTANFÖR ritytan placeras inte: den syns inte, och den tränger inte
// undan en som syns. Stilens `collide = false` och lagrets
// `setLayerCollision(i, false)` gör etiketten alltid synlig. Det räknas om
// vid varje kameraändring, och det enda som laddas upp är synligheten — en
// byte per etikett. `setLabelFade(true)` tonar in och ut i stället för att
// växla — också en etikett som panoreras in över kanten. En etikett eller
// markör med ett ZOOMINTERVALL (`TextStyle::minZoom`/`maxZoom`) döljs
// på samma väg utanför det: den tar ingen plats och träffas inte.
//
// MINNET: bilderna och draperingarna ritytan ritar ligger under
// RITYTANS budget (`setMemoryBudget`, förval `App::canvasMemoryBudget()`) —
// två ritytor i samma fönster vräker inte varandras bilder.
//
// I RYMDEN: formerna med höjd (`Shape::line3`,
// `text3` …) ligger i samma scen. Ritytan ritar då i ett EGET 3D-MÅL med
// djup och MSAA — när den har ett World- eller Terrain-lager
// (`setLayerPlacement`; Auto blir World av en form i rymden), en `Camera3D`,
// en del-vy, ett helskärmspass (`Shape::screenPass`), `setMultisample(> 1)`
// eller `setDepth(true)`. Annars ritas 2D-vägen. I 3D-målet ligger Ground-lagren på marken (utan djup, i
// målarordning), World-lagren djuptestas med sitt renderläge
// (`setLayerRenderState`), och GROUND-LAGRENS ETIKETTER RITAS ÖVERST — efter
// World — medan 2D-vägen ritar etiketterna i lagrens ordning. Ett lagers
// målare (`onPaintLayer`) ritas med sitt lagers djup. Målet ritas om
// bara när något ritytan ritar kan ha ändrats (ritytans egna invalideringar
// och fönstrets fulla — en körd handler får ha ändrat vad `onPaint` ritar);
// ett barns invalidering, som ett blinkande textfält ovanpå, kostar bara
// kompositen. `onPaint` måste alltså vara deterministisk också här.
// Kartkameran står kvar och ritas i 3D-målet som `gfx::toView3D` över
// scenens höjder; med en `Camera3D` gäller den kameran, gesterna är orbitens
// (tabellen ovan) och `toWorld`/`toLocal`/`unitsPerPixelAt`/musens
// världspunkt räknar mot marken (z = 0).
//
// DEL-VYERNA (style3d.h `View`) är egna kameror i rutor
// ovanpå huvudvyn — navigeringskuben, axeltriaden — i kollektionsvokabulären
// (`addView`/`setView`/`setViews`/`removeView`), varje vy med eget djup och
// egen MSAA. Ett lager ritas i de vyer `setLayerViews` säger, huvudvyn (0)
// från start. `renderToBitmap` ritar huvudvyns lager med en egen kamera i en
// bild — en miniatyr — utanför en frame, också med fönstret dolt.

namespace glaze {

namespace detail {
struct SceneData;      // src/canvas/scene.h
struct PaintContext;   //   …ritningens delade läge
struct PanSample;      // src/canvas/camera_math.h
class  Projection;     //   …en vys projektion
enum class PanVertical : std::uint8_t;   // …3D-panoreringens fram/bak
enum class MotionKind : std::uint8_t;    // …kamerans sort för bindningarna
class  InputMap;       // src/canvas/input_map.h
struct DragPlan;       //   …ett drags två axlar
class  ExternalCanvasPass;   // glaze/canvas/external_pass.h
}

// Hur ett lager ritar sina former (src/canvas/scene.h har detaljerna):
//   Strict  formens index ÄR målarordningen — det man lägger till sist
//           ligger överst. På varandra följande former av samma slag delar
//           rit-anrop; varje BYTE av slag är ett nytt. En yta med kontur
//           byter två gånger, och en väg följd av sitt namn likaså — ett
//           lager med tusen sådana är tusentals anrop.
//   ByKind  bilderna, ytorna, linjerna och texten i den ordningen, var för
//           sig grupperade — få rit-anrop i ett stort kartlager, men två
//           överlappande former av samma slag kan ritas i en annan ordning
//           än indexens. Kartans ordning: canvas_perf visar samma
//           karta på 60 anrop i ByKind och 10 663 i Strict.
enum class DrawOrder : std::uint8_t { Strict, ByKind };

// Args till ritytans mushändelser — BASENS `MouseArgs` (widget.h: `button`,
// `local`, `mods`, `handled`) och det ritytan vet utöver den. Positionen
// kommer i TRE former: världen (det appen
// räknar i), ritytans lokala logiska px och strålen. `button` är Left, Middle
// eller Right i `onMouseDown`/`onMouseUp`/`onDoubleClick` och i `onMouseMove`
// under ett drag (WinForms `MouseEventArgs.Button`), None för en
// rörelse utan tryck, och i
// `onMouseCancel` det AVBRUTNA trycket. `handled` (UTDATA): handlern TOG
// händelsen — då gör ritytan inget eget med den, ingen gest startar,
// fortsätter eller glider ut på en händelse appen tagit.
struct CanvasMouseArgs : MouseArgs {
    // Världspunkten under pekaren — TOM ovanför horisonten under tilt.
    std::optional<Point> world;
    float                wheelX = 0.0f, wheelY = 0.0f;   // onMouseWheel: rader
    // onMouseWheel: STYRPLATTANS tvåfingersdrag (och andra precisa hjul) —
    // raderna är då logiska px / `gfx::kPrecisePxPerRow`, och ritytan
    // panorerar på dem i stället för att zooma.
    bool                 precise = false;
    // Strålen genom pekaren i världen (`Canvas::rayAt`) —
    // med 3D-kameran eller kartkameran; tom utan känd yta.
    std::optional<Ray3> ray;
};

// Args till `Canvas::onTick`: tiden efter föregående tick —
// eller efter att tickningen började — i sekunder, högst en tiondel (ett fönster
// som legat dolt eller blockerats ska inte ge ett hopp).
struct TickArgs : EventArgs {
    double seconds = 0.0;
};

// Args till `Canvas::onGesture` — styrplattans nypning och vridning
// (`Widget::gesture`s sekvens) före den inbyggda gesten. `scale` är den
// multiplikativa ändringen efter förra steget (> 1 = isärdragna fingrar),
// `rotation` grader efter förra steget, POSITIVT = MEDURS på skärmen; `local` är
// pekaren — gestens ankare — i ritytans lokala logiska px.
struct GestureArgs : EventArgs {
    gfx::GesturePhase phase{};
    float             scale    = 1.0f;
    float             rotation = 0.0f;
    Point             local{};
    // UTDATA: handlern TOG steget — den inbyggda gesten gör ingenting med det.
    bool handled = false;
};

// VAD EN TRÄFF ÄR:
//   Shape    en form: en 2D-form (på marken under pekaren), en linje i rymden
//            (`element` = segmentet) eller en markör som inte kolliderar
//   Surface  en triangel i en mesh, en instans eller en yta i rymden
//            (`element` = ORIGINALETS triangelindex, `normal` = ytans normal)
//   Vertex   ett meshhörn inom toleransen på skärmen (`element` = läget)
//   Edge     en meshkant inom toleransen (`element` = 3 · triangeln + k, kanten
//            från triangelns hörn k till hörn k + 1 mod 3)
//   Point    en punkt i ett punktmoln inom toleransen (`element` = punktens
//            plats i indatan) — den närmaste på skärmen, en per form
//   Label    en etikett — text, en kolliderande markör — som den ritades
// En MARKÖRFORM (`Shape::marker`, `points`, `marker3`) bär PUNKTEN i `element`
// som Shape och som Label: platsen i formens `points()` — den
// träffade vars mitt ligger närmast pekaren, vid lika den senare (den ritas
// överst). Ett punktlager kan då vara EN form. −1 = hela formen.
enum class HitKind : std::uint8_t { Shape, Surface, Vertex, Edge, Point, Label };

// En träff i `Canvas::hitTest`: lagret och formens index där — och
// vad som träffades och var. `hitTest(Point)` (2D) och
// `hitTest(Rect, HitMode)` fyller lagret, indexet och markörens punkt
// (`element`); `hitTest(Point, HitOptions)` och `castRay` allt.
struct HitResult {
    int          layer    = -1;
    int          index    = -1;
    int          view     = 0;    // del-vyn pekaren stod i (0 = huvudvyn)
    int          instance = -1;   // instansens plats i indatan (`Shape::instances`)
    std::int64_t element  = -1;   // se `HitKind`
    HitKind      kind     = HitKind::Shape;
    // Träffen i världen: på triangeln, hörnet, punkten, kantens närmaste
    // punkt mot strålen, marken under pekaren för en 2D-form, ankaret för en
    // etikett.
    std::optional<Point3> point;
    Vec3                  normal{};   // Surface: enhetsnormalen efter vindningen
    // Längs strålen från dess början (ögat i perspektiv, närplanet
    // ortografiskt) — till `point`, eller dess projektion på strålen för det
    // som ligger bredvid den (ett hörn, en punkt inom toleransen).
    double distance = 0.0;
};

// HUR `hitTest(lokal, HitOptions)` och `castRay` plockar.
// Toleranserna är på SKÄRMEN (px eller mm; i världsenheter vid träffen).
// `tolerance` är ANROPETS — 2D-formerna, etiketterna och linjerna i
// rymden prövas med den, och hörnen, kanterna och punkterna när deras egen
// är 0; 0 där är stilens `canvasHitTolerance` (app-global — en karta vill ha
// större fångst än en modellvy). En tolerans i världsenheter räknas för
// 2D-formerna om vid pekaren (ovanför horisonten blir den 0), i rymden vid
// träffen.
struct HitOptions {
    // Bara OGENOMSKINLIGA ytor: en triangel vars hörn har alfa < 255, och en
    // form vars stil eller lager inte är helt täckande, träffas inte.
    bool opaqueOnly = false;
    // Det som ligger BAKOM den första ogenomskinliga ytan längs synlinjen
    // träffas inte — det man inte ser (marken skymmer under sig när ett
    // Ground-lager ritas i 3D-målet). Ground-lagrens etiketter, som ritas
    // överst, skyms aldrig.
    bool   occlusion = false;
    Length vertexTolerance;
    Length edgeTolerance;
    Length pointTolerance;
    // > 0: ingenting längre bort längs strålen (världsenheter).
    double maxDistance = 0.0;
    Length tolerance;
};

// RUTANS LÄGE i `Canvas::hitTest(Rect, HitMode)`, CAD-programmens
// två: FÖNSTRET tar formerna som ligger HELT INOM rutan, KORSNINGEN dem som
// skär den eller ligger inom.
enum class HitMode : std::uint8_t { Window, Crossing };

class Canvas : public Widget {
public:
    Canvas();
    ~Canvas() override;

    // ------------------------------------------------------------- EVENTS
    // Det som ritas varje gång — i VÄRLDEN (onPaint, ovanpå lagren), i
    // LAGRENS ORDNING (onPaintLayer — i varje lager med
    // `setLayerPainter`, efter lagrets egna former, med lagrets plats i
    // `e.layer`) och i ritytans lokala px (onPaintOverlay). Deterministiskt,
    // se glaze/canvas/painter.h. `onPaintOverlay` ritas i fönstrets OVERLAY-
    // LAGER: vid varje present, ovanpå allt innehåll och klippt
    // till ritytan — ett gummiband eller en HUD som ändras behöver bara en
    // ny present (`invalidateOverlay()`), inte en omritning av lagren.
    Handler<PaintArgs> onPaint;
    Handler<PaintArgs> onPaintLayer;
    Handler<PaintArgs> onPaintOverlay;

    // Musen. TRYCKET, SLÄPPET OCH AVBROTTET är BASENS (widget.h:
    // `onMouseDown`/`onMouseUp`/`onMouseCancel` på varje widget, fyrade av
    // fönstret) — ritytan bygger deras args, en `CanvasMouseArgs` med världen
    // och strålen, så en handler som väntar sig dem
    // (`[](glaze::Widget*, glaze::CanvasMouseArgs& e) { … }`) sätts på basens
    // fält och körs här. Med `handled` i `onMouseDown` TAR appen trycket — med
    // vilken knapp som helst: ingen gest startar, inget mitten- eller
    // högertryck bubblar vidare och ingen snabbmeny öppnas (den som vill ha en
    // efter ett klick utan drag öppnar den själv i `onMouseUp`,
    // `Menu::popupAt`), och tryckets rörelser och dubbelklick kommer sedan
    // hit med knappen. Med `handled` i `onMouseUp` glider ingenting ut. Ett
    // mittentryck appen inte tar panorerar när en gest kan börja (och bubblar
    // annars till förfäderna); ett högertryck den inte tar går till
    // förfäderna och snabbmenyn. ETT AVBROTT (`onMouseCancel` —
    // fönstret tappade fokus, OS:et tog musen, ett nytt tryck med samma knapp
    // kom innan förra släppet) stannar gesten där den står, utan tröghet, och
    // `local`/`world`/`ray` är pekarens senast kända läge (tomma när pekaren
    // lämnat fönstret). Ett mitten- eller
    // högertryck på ett BARN till ritytan (ett fält på kartan) fyrar inte
    // ritytans `onMouseDown` — basens fält bubblar inte.
    //
    // Ritytans EGNA mushändelser: pekaren rör sig (med och utan knapp — EN gång
    // per rörelse: under ett drag ritytan håller, och under ett tryck appen
    // tog, med knappen; annars utan), hjulet och dubbelklicket med var och en
    // av de tre knapparna.
    //
    // Tangenterna är basens: `onKeyDown`/`onKeyUp` (widget.h, ärvda med samma
    // namn och typ). En tangent `onKeyDown` TAR (`handled`) håller ritytan
    // TICKANDE så länge den hålls nere (`onTick`), och den går inte till
    // ritytans egna pilar och plus/minus.
    Handler<CanvasMouseArgs> onMouseMove;
    Handler<CanvasMouseArgs> onMouseWheel;
    Handler<CanvasMouseArgs> onDoubleClick;
    // STYRPLATTANS GEST: nypningen och vridningen, steg
    // för steg, före den inbyggda gesten — appens egen kamera. Med en
    // handler tar ritytan varje gestsekvens som börjar över den, också med
    // gesterna av.
    Handler<GestureArgs>     onGesture;
    // TICKEN — det som rör sig av sig självt: en gång per
    // ritad bildruta, i pump-svansen efter framen — före appens postade
    // funktioner och utan deras budget (`App::setPostBudget`) — så länge
    // `setTicking(true)` gäller ELLER en tangent `onKeyDown` tog fortfarande
    // hålls (`AppWindow::isKeyHeld` — släppet och fokusförlusten tar slut på
    // den). Utan handler tickar ritytan inte. Efter handlern ritas RITYTAN om
    // — inte fönstret, se nedan — och då kommer nästa tick; när ingenting
    // håller tickningen vid liv ber ritytan inte om fler bildrutor (hård
    // regel 1).
    Handler<TickArgs>        onTick;
    // ANVÄNDAREN flyttade kameran: ett drag, ett hjulsteg, en tangent, varje
    // steg i en styrplattegest — och varje steg i en tröghet eller ett
    // vridsnäpp, som då kommer i pump-svansen efter framen (de stegas i
    // ritningen, där ingen handler får köras). Sättarna och `easeTo` fyrar
    // den aldrig.
    //
    // RITYTANS LÖPANDE HÄNDELSER RITAR OM RITYTAN, INTE FÖNSTRET (ett
    // medvetet undantag från "efter en handler ritas
    // hela fönstret om", widget.h): efter `onTick`, `onGesture` och
    // `onCameraChange` från ett drag, ett hjulsteg, en styrplattegest, en
    // tröghet eller ett snäpp ritas ritytan om — lagren, `onPaint` och dess
    // 3D-mål — men inte resten av fönstret, och en annan rityta i samma
    // fönster ritar inte om sitt 3D-mål. Det handlern ändrar i en ANNAN
    // widget gör den genom dess sättare (`Label::setText` …), som invaliderar
    // den; den som ändrat ett eget fält som en annan widgets ritning läser
    // (en annan ritytas `onPaint`) anropar den widgetens `invalidate()` själv.
    // Diskreta händelser — ett klick, ett dubbelklick, en tangent, också en
    // som flyttar kameran — ritar om hela fönstret.
    Handler<ChangeArgs>      onCameraChange;

    // --------------------------------------------------------------- YTAN
    // Bakgrunden. Osatt följer den temat (fältets `panel`); alfa 0 är en
    // genomskinlig yta.
    void       setBackground(gfx::Color c);
    gfx::Color background(const Ctx& c) const;
    bool       hasBackground() const { return hasBackground_; }

    void setBorder(bool on);
    bool border() const { return border_; }

    // PLATT YTA — kant i kant med grannarna, ritytor staplade i en
    // delare eller en dockningspanel: ingen kant och inga rundade hörn
    // (bakgrunden, scenen, overlayn och barnen klipps rakt till ytan), och
    // fokusmarkeringen INNANFÖR ytan, så den aldrig ritas över grannen.
    // `border()` står kvar och gäller igen när ytan inte längre är platt.
    void setFlat(bool on);
    bool flat() const { return flat_; }

    // RUTNÄTET var `size` världsenhet, 0 = inget. Glesas ut (×2, ×5, ×10 …)
    // när linjerna skulle hamna tätare än `Style::canvasGridMin`.
    void   setGridSize(double size);
    double gridSize() const { return gridSize_; }

    // ------------------------------------------------------------- KAMERAN
    void   setCenter(double x, double y);
    Point  center() const;
    void   setZoom(double zoom);           // logiska px per världsenhet
    double zoom() const;
    // Zoomens gränser; 0 = obegränsat åt det hållet.
    void   setZoomRange(double minZoom, double maxZoom);
    double minZoom() const { return minZoom_; }
    double maxZoom() const { return maxZoom_; }
    void   setBearing(float deg);
    float  bearing() const;
    void   setPitch(float deg);            // klampas till [0, maxPitch]
    float  pitch() const;
    // Tiltens tak, förval 60°; kamerans eget tak är 70° (gfx::kMaxPitchDeg).
    void   setMaxPitch(float deg);
    float  maxPitch() const { return maxPitch_; }
    // Världens y uppåt (en karta) — annars nedåt, som `Draw`.
    void   setYUp(bool on);
    bool   yUp() const { return yUp_; }

    // Kartkameran — och ritytan ritar med den (en satt `Camera3D` släpps).
    void   setCamera(const Camera& c);
    Camera camera() const;
    // Har appen satt en kamera? Annars är världen de lokala logiska px.
    bool   hasCamera() const { return hasCamera_; }

    // 3D-KAMERAN — en ÖVERLAGRING av `setCamera`, så en
    // literal bär alltid sin typ (`glaze::Camera3D{…}`): ritytan ritar med
    // den i 3D-målet tills `setCamera(Camera)` tar tillbaka kartkameran.
    // Kartkamerans sättare (`setCenter`, `setZoom` …) ändrar kartkameran och
    // syns först när den gäller igen. En ogiltig kamera (camera.h:s regler)
    // ⇒ `gfx::log` och ingenting ändras.
    void     setCamera(const Camera3D& c);
    // Den senast satta 3D-kameran (förvalet utan en) — med gesterna inräknade.
    Camera3D camera3D() const { return cam3D_; }
    // Ritar ritytan med 3D-kameran?
    bool     is3D() const { return is3D_; }

    // 3D-KAMERANS VERKTYG — samma rörelser som orbitens
    // gester, för appens egen interaktion (tangenter, en navigeringskub).
    // De gäller med en `Camera3D` (annars `gfx::log` och ingenting händer),
    // oavsett om gesterna är på, och fyrar ALDRIG `onCameraChange` — den är
    // användarens. Spannen och tumlingen nedan gäller också här.
    //   orbitBy  azimut och elevation, grader (elevationen spärrad eller
    //            tumlande som orbitens)
    //   panBy    MÅLPLANETS punkt mitt i ytan flyttar `deltaLocal` logiska
    //            px — innehållet följer i BÅDA leden, i planet vinkelrätt mot
    //            blicken (utan yta just nu väntar den till nästa ritning med
    //            yta, efter en väntande inramning). Det är
    //            inte gestens form: draget flyger fram/bak längs blicken
    //            (camera_math); verktyget flyttar exakt det appen ber om.
    //   dollyBy  avståndet × `factor` (> 0)
    void orbitBy(float dAzimuth, float dElevation);
    void panBy(Point deltaLocal);
    void dollyBy(double factor);
    // GLID till 3D-kameran på `ms` millisekunder — målet linjärt, avståndet
    // geometriskt, azimuten den korta vägen (camera_math `easeCamera3D`).
    // Utan en 3D-kamera, eller `ms` ≤ 0, sätts den direkt (`setCamera`).
    // Ritytan invaliderar sig själv tills den är framme (hård regel 1), och
    // en sättare eller en gest mitt i glidningen avbryter den.
    void easeTo(const Camera3D& target, int ms);
    // INRAMNINGEN: lådans mitt blir målet och avståndet det
    // minsta där lådan ryms med luften `pad` (px eller mm) runt om —
    // rotationen orörd. Bara med en 3D-kamera; utan yta just nu (före första
    // ritningen, i en inaktiv flik) görs den vid nästa ritning med yta. En
    // omvänd låda ⇒ `gfx::log` och ingenting ändras.
    void zoomToBox(const Box3& box, Length pad = {});

    // TUMLINGEN: elevationen spärras inte — orbiten går över polen och
    // upp-vektorn följer elevationens tangent, så bilden vänder aldrig.
    void  setOrbitTumble(bool on);
    bool  orbitTumble() const { return orbitTumble_; }
    // Elevationens spann för orbiten utan tumling, grader inom ±180; förval
    // ±85°. Omkastade byts, icke-ändliga eller utanför
    // ±180 ⇒ `gfx::log` och ingenting ändras. Kamerans sättare spärras inte.
    void  setElevationRange(float minDeg, float maxDeg);
    float minElevation() const { return minElevation_; }
    float maxElevation() const { return maxElevation_; }
    // Dollyns spann i världsenheter; 0 = obegränsat åt det hållet (som
    // `setZoomRange`). Kamerans sättare spärras inte.
    void   setDistanceRange(double minDistance, double maxDistance);
    double minDistance() const { return minDistance_; }
    double maxDistance() const { return maxDistance_; }

    // TICKNINGEN (`onTick`): på tickar ritytan en gång per bildruta tills den
    // slås av — utöver den tickning tagna tangenter ger.
    void setTicking(bool on);
    bool ticking() const { return ticking_; }

    // 3D-MÅLETS PROVTAGNING: 1 (ingen MSAA), 2, 4 eller 8 — renderaren ger
    // närmaste den klarar under (llvmpipe har 4). Förvalet är 4 när 3D-målet
    // används av något annat skäl; ett SATT värde över 1 tar ritytan till
    // 3D-målet. Annat ⇒ `gfx::log` och ingenting ändras.
    void setMultisample(int samples);
    int  multisample() const;
    // DJUPET: tar ritytan till 3D-målet (som alltid har djup) också utan
    // World-lager och 3D-kamera.
    void setDepth(bool on);
    bool depth() const { return depth_; }
    // LJUSET för ritytans meshar (style3d.h `Lighting`):
    // ett riktat ljus fast i världen, rimljuset och dimman — för hela
    // ritytan. Ett ljus med ett tal som inte är ändligt ⇒ `gfx::log` och
    // ingenting ändras.
    void     setLighting(const Lighting& l);
    Lighting lighting() const { return lighting_; }

    // GLID till kameran på `ms` millisekunder (mjuk start och landning).
    // Ritytan invaliderar sig själv tills den är framme (hård regel 1 — ingen
    // timer, ingen loop), och en sättare mitt i glidningen avbryter den.
    void easeTo(const Camera& target, int ms);
    // Glider kameran — `easeTo` (kartkamerans eller 3D-kamerans) eller
    // vridsnäppets egen glidning?
    bool isEasing() const { return easing_ || easing3D_ || snapping_; }
    // Rör sig något av sig självt — en glidning, en tröghet eller en
    // etikettoning? Det är så länge ritytan invaliderar sig själv. (Ticken
    // räknas inte: den är appens rörelse, `ticking()`.)
    bool isAnimating() const
    {
        return easing_ || easing3D_ || snapping_ || coasting_ ||
               labelsAnimating_;
    }

    // Visa rektangeln, med luften `pad` runt om (px eller mm). Räknas utan
    // tilt; vridningen tas med. Saknar ritytan yta JUST NU (före första
    // ritningen, i en inaktiv flik) görs det vid nästa ritning med yta —
    // aldrig mot den senast ritade.
    void zoomToRect(const Rect& r, Length pad = {});

    // KARTSKALAN 1 : `denominator` med linjal — en meter på skärmen är
    // `denominator` meter i terrängen, ur skärmens mm. `metersPerUnit`
    // är världsenhetens längd i meter (1 för SWEREF). Utan fönster väntar den
    // till nästa ritning.
    void   setMapScale(double denominator, double metersPerUnit = 1.0);
    double mapScale() const;
    // Är skärmen MÄTT (eller kalibrerad)? Annars är skalan antagen — visa
    // "≈1:2000".
    bool   mapScaleIsExact() const;

    // Lokala logiska px ↔ världen, med kameran som den senast ritades (eller
    // som den räknas fram nu, före första ritningen). Tomt ovanför
    // horisonten under tilt, respektive bakom kameran.
    std::optional<Point> toWorld(Point local) const;
    std::optional<Point> toLocal(Point world) const;

    // SKALAN DÄR PUNKTEN STÅR: världsenheter per LOGISK px längs skärmens
    // vågräta axel vid den lokala punkten — det en skalstock ska mäta. Rakt
    // uppifrån är den 1/zoom överallt, också vriden; under tilt är den lika
    // längs en bildrad men krymper mot underkanten (närmare kameran), så
    // `mapScale()` — mittens skala — stämmer bara i mitten. Samma kamera som
    // `toWorld`; tom ovanför horisonten och utan känd yta.
    std::optional<double> unitsPerPixelAt(Point local) const;

    // 3D-MATTEN — verktygen appen ritar rutnät, etiketter
    // och sin egen plockning med. HUVUDVYNS kamera som den ritas nu (en
    // `Camera3D`, eller kartkameran som 3D-vy); tomt utan känd yta (före
    // första ritningen i ett stängt fönster).
    //   rayAt          strålen genom den lokala punkten, enhetlig riktning:
    //                  perspektiv från ögat, ortografiskt från närplanet
    //   project        världspunkten i lokala logiska px; tom bakom kameran
    //   toWorld        där strålen genom punkten når planet (framför
    //                  kameran); tom parallellt eller bakom
    //   worldPerPixel  världsenheter per LOGISK px vinkelrätt mot blicken
    //                  vid punkten — perspektiv efter djupet, ortografiskt
    //                  lika överallt; tom i eller bakom ögats plan
    //   visibleRegion  den del av planet kameran ser, ett konvext polygon
    //                  moturs sett från normalens sida — till det bortre
    //                  klipplanet, eller till djupet `maxDistance` när det är
    //                  närmare; tomt när planet inte syns
    std::optional<Ray3>   rayAt(Point local) const;
    std::optional<Point>  project(Point3 world) const;
    std::optional<Point3> toWorld(Point local, const Plane3& plane) const;
    std::optional<double> worldPerPixel(Point3 world) const;
    std::vector<Point3>   visibleRegion(const Plane3& plane,
                                        double maxDistance = 0.0) const;

    // Skärmen ritytan står på (fönstrets, med kalibreringen); utan fönster
    // det antagna 96 dpi.
    gfx::DisplayMetrics display() const;

    // ------------------------------------------------------------ GESTERNA
    // Se klasshuvudet. Alla av från start; trögheten på.
    void setPannable(bool on);
    bool pannable() const { return pannable_; }
    void setZoomable(bool on);
    bool zoomable() const { return zoomable_; }
    void setRotatable(bool on);
    bool rotatable() const { return rotatable_; }
    void setPitchable(bool on);
    bool pitchable() const { return pitchable_; }
    void setInertia(bool on);
    bool inertia() const { return inertia_; }

    // ---------------------------------------------------------- BINDNINGARNA
    // VAD musen, styrplattan och tangenterna gör
    // med kameran, i varje LÄGE: en tabell (läge, källa, modifierare) →
    // (handling, riktning, fart), vokabulären i glaze/input.h. Förvalet är
    // ramverkets gester (tabellen i klasshuvudet), och appens globala tabell
    // (`App::setCanvasBinding`) är vad ritytan ärver, RAD FÖR RAD: en rad
    // ritytan satt med samma nyckel ersätter appens, och resten ärvs.
    //
    // LÄGET: "map" (kartkameran), "orbit" (`Camera3D` kring målet),
    // "firstPerson" (`Camera3D` kring ögat — `Camera3D::lookFrom`; kameran
    // kan inte veta det själv, så appen väljer det) eller ett EGET namn
    // ("mäta", "rita"). `setInputMode("")` (förvalet) är AUTOMATISKT: "map"
    // utan och "orbit" med en `Camera3D`. Ett eget läge bär bara sina
    // avvikelser — saknar det en träff för gesten faller den tillbaka på det
    // automatiska. Bytet gäller från nästa händelse.
    //
    // UPPSLAGET: en rad MATCHAR när dess modifierare är en DELMÄNGD av de
    // tryckta (Cmd räknas som Ctrl), och den mest specifika vinner — flest
    // bitar, vid lika Alt före Shift före Ctrl. Ett DRAG läser raden vid varje
    // rörelse med de aktuella modifierarna (Shift mitt i ett mittendrag byter
    // till flygning); hjulet, styrplattan och tangenterna per händelse. En
    // handling som `setPannable`/`setZoomable`/`setRotatable`/`setPitchable`
    // stängt av verkar inte: ett drag tar då nästa mindre specifika rad, och
    // en händelse bubblar — och en flagga som slås av mitt i ett drag stänger
    // bara av sina handlingar. `InputAction::None` är OBUNDEN — gesten gör
    // ingenting, når appens handler och bubblar (högertrycket öppnar
    // snabbmenyn) — och SKUGGAR de mindre specifika raderna för sin källa;
    // utan rad alls är det likadant. Hjulet tas bara av en axel som rör sig. Med en påslagen bindning för
    // högerknappens drag (`RightDragX`/`RightDragY`) TAR ritytan högertrycket,
    // och då öppnas ingen snabbmeny vid det.
    //
    // `setBinding` ERSÄTTER raden med nyckeln helt (en ADRESS, inte ett
    // index); `removeBinding` tar bort ritytans rad, så appens gäller igen;
    // `clearBindings` tar bort ritytans alla rader. Ogiltigt — tomt läge,
    // fart ≤ 0 eller inte ändlig — loggas och ändrar ingenting. Sättarna
    // lagrar och ritar inte om. I `.glazeui` en rad per bindning:
    //     karta.setBinding("orbit", glaze::InputSource::WheelY, 0, glaze::InputAction::Zoom, glaze::InputDirection::Inverted, 1.5f);
    //     karta.setBinding("orbit", glaze::InputSource::ScrollX, gfx::kModCtrl | gfx::kModShift, glaze::InputAction::Rotate);
    void               setInputMode(std::string mode);
    const std::string& inputMode() const { return inputMode_; }
    // Läget som gäller nu: `inputMode()`, eller det automatiska.
    std::string        activeInputMode() const;
    void setBinding(const std::string& mode, InputSource s, std::uint32_t mods,
                    InputAction a, InputDirection dir = InputDirection::Normal,
                    float speed = 1.0f);
    void removeBinding(const std::string& mode, InputSource s,
                       std::uint32_t mods);
    void clearBindings();
    // Ritytans EGNA rader.
    std::vector<InputBinding> bindings() const;
    // Raden som gäller för (läge, källa, tryckta modifierare) — ritytans eller
    // ärvd, med lägets reserv och utan flaggorna: inställningsdialogens fråga.
    std::optional<InputBinding> effectiveBinding(const std::string& mode,
                                                 InputSource s,
                                                 std::uint32_t mods) const;
    // VÄNDNINGARNA, utöver radens riktning och multiplikativa med den:
    // KÄLLANS (`ScrollY` — i alla lägen och med alla modifierare, användarens
    // vana) och HANDLINGENS PER ENHET (`Mouse` + `Zoom` vänder hjulet OCH ett
    // zoomande musdrag; `Keyboard` vänder + och −, `Touchpad` nypningen). Att
    // vända en handling oavsett källa är tre anrop. En osatt vändning ärvs från
    // appen (`App::setCanvasSourceInvert`/`setCanvasActionInvert`), och
    // `clear…` ärver igen.
    void setSourceInvert(InputSource s, bool on);
    void clearSourceInvert(InputSource s);
    void setActionInvert(InputDevice d, InputAction a, bool on);
    void clearActionInvert(InputDevice d, InputAction a);

    // VRIDSNÄPPET — ställbart och valbart; snäpp till hela grader gör att man kan få tillbaka rotationen. När en
    // VRIDANDE gest släpps (Ctrl/Cmd+draget, styrplattans vridning) inom
    // toleransen från en vinkel i stegets följd glider kameran dit på en
    // femtedels sekund, och glidningen är användarens (`onCameraChange`).
    // Steget i grader: 0 = av, 360 = bara norr (förvalet — ritytans
    // beteende före snäppet blev ställbart), 90 = 0/90/180/270, 45 =
    // åttondelar; vinkeln räknas modulo 360, så 268° snäpper till 270° och
    // 355° till norr. Toleransen är fångstzonen runt varje vinkel, förval 7°.
    // Shift+pilarnas steg och `setBearing` snäpps INTE — de ställer en
    // bäring, de släpper ingen. Ogiltigt (steget utanför 0–360, toleransen
    // utanför 0–180, icke-ändligt) ⇒ `gfx::log` och ingenting ändras.
    void  setRotationSnap(float stepDeg);
    float rotationSnap() const { return rotationSnap_; }
    void  setRotationSnapTolerance(float deg);
    float rotationSnapTolerance() const { return rotationSnapTolerance_; }

    // Bara overlay-lagret har ändrats (`onPaintOverlay` — gummibandet, en
    // HUD): en ny present utan omritning av lagren.
    void invalidateOverlay();

    // ----------------------------------------------------------- TRÄFFPROV
    // Formerna under punkten (ritytans lokala logiska px), ÖVERST FÖRST:
    // lagren uppifrån, och i varje lager i målarordning bakifrån. Avståndet
    // räknas i skärmens px — exakt också under tilt — med stilens
    // `canvasHitTolerance`; ett lager som inte ritas (dolt, utanför sitt
    // zoomintervall, genomskinligt) och en etikett som kollisionen dolt
    // träffas inte. Kameran är den som gäller nu. Kräver ett öppet fönster
    // (en etiketts låda är glyflayoutens, som byggs i ritningen) — utan
    // svaras tomt.
    std::vector<HitResult> hitTest(Point local) const;
    // Den översta formen under punkten i lagret (utan lager: lager 0), eller
    // −1.
    int shapeAt(Point local) const;
    int shapeAt(int layer, Point local) const;
    // FORMERNA I RUTAN — rutmarkeringen: rektangeln i ritytans
    // lokala logiska px (en negativ bredd eller höjd räknas från andra hörnet,
    // som ett gummiband dras) prövas PÅ SKÄRMEN mot det formen ritar med
    // kameran som gäller nu, så rutan är rätt också vriden och tiltad.
    // `Window` tar formerna HELT INOM rutan — allt av dem syns, så en form som
    // närplanet skär av ligger inte inom — och `Crossing` dem som skär den
    // eller ligger inom. Linjerna och konturerna räknas med sin MITTLINJE
    // (bredden räknas inte), ytorna och bilderna med sin yta (en kontur med
    // `Stroke::hitInside` som yta), markörerna och etiketterna med lådan de
    // ritas i. En MARKÖRFORM prövas PUNKT FÖR PUNKT: varje punkt i rutan är en
    // egen träff med punkten i `element`. Ordningen och lagren som
    // `hitTest(Point)`: överst först, bara det som ritas i huvudvyn. SKRIVNA
    // GRÄNSER: formerna i rymden (linjerna, meshar, ytor, instanser, punktmoln
    // — text och markörer i rymden prövas) och, med terräng i 3D-målet,
    // Ground-lagren (de finns bara i draperingen) prövas inte. Kräver ett
    // öppet fönster; en ruta som inte är ändlig ⇒ `gfx::log` och tomt.
    std::vector<HitResult> hitTest(Rect local, HitMode mode) const;

    // 3D-PLOCKNINGEN: allt under punkten längs strålen
    // genom den — i den ÖVERSTA DEL-VYN punkten står i (`HitResult::view`,
    // med vyns kamera och de lager den ritar), annars huvudvyn — SORTERAT PÅ
    // AVSTÅNDET längs strålen, närmast först. Meshar, ytor i rymden och
    // instanser träffas på triangeln (BVH:n, originalets triangelindex, den
    // RITADE detaljnivån och stilens intervall), deras hörn och kanter (inte
    // i ett lager utan snap, `setLayerSnap`) och punkterna inom toleranserna
    // på skärmen, linjerna i rymden inom halva bredden + toleransen,
    // etiketterna som de ritades och 2D-formerna där strålen når marken —
    // eller terrängen, när ett Terrain-lager ligger under pekaren.
    // Ground-lagrens etiketter, som ritas överst i 3D-målet, står först. Lager
    // som inte ritas träffas inte. Kräver ett öppet fönster som
    // `hitTest(Point)`.
    std::vector<HitResult> hitTest(Point local, const HitOptions& options) const;
    // YTORNA längs en godtycklig stråle i världen — meshar, ytor i rymden,
    // instanser och terräng i de synliga lagren — sorterade på avståndet,
    // med `opaqueOnly`, `occlusion` och `maxDistance` (toleranserna gäller
    // skärmen och används inte). Med `occlusion` plockas det som ligger bakom
    // den första ogenomskinliga ytan inte alls. Kräver ingen kamera.
    std::vector<HitResult> castRay(const Ray3& ray,
                                   const HitOptions& options = {}) const;
    // TERRÄNGENS HÖJD vid (x, y): den översta ytan i de synliga
    // Terrain-lagren rakt under punkten, på den finaste detaljnivån; tom där
    // ingen terräng finns (ett hål, utanför, inget Terrain-lager). Det
    // ritytans etiketter på terräng och en markföljande kamera står på.
    std::optional<double> heightAt(Point xy) const;

    // -------------------------------------------------------------- LAGREN
    int  addLayer(TrText name);
    // ERSÄTTER lagret helt (tilldelning): nytt namn, förvalda egenskaper och
    // inga former.
    void setLayer(int layer, TrText name);
    void setLayers(std::vector<TrText> names);
    void removeLayer(int layer);
    // LAGRENS ORDNING: flytta lagret `from` till platsen `to`,
    // räknad i ordningen FÖRE flytten och i [0, layerCount()] — lagret läggs
    // in UNDER det som stod på `to`, och `layerCount()` lägger det överst
    // (TreeView::moveNode:s konvention, så `moveLayer(i, i + 1)` står kvar).
    // Lagret byggs inte om och laddas inte upp: bara ritordningen byts.
    // Etiketterna placeras om utan toning (synlighetsbilden laddas upp hel —
    // en byte per etikett), och draperingarna på terräng ritas om. Indexen
    // är POSITIONER: efter flytten har lagren nya index, också i HitResult,
    // `setLayerViews` och formerna utan lagerindex (de går till plats 0).
    // Utanför ⇒ gfx::log och false; till samma plats ⇒ true utan omritning.
    // Inte i onPaint. Ordningen INOM ett lager är `setLayerDrawOrder`s.
    bool moveLayer(int from, int to);
    int  layerCount() const;
    const std::string& layerName(int layer) const;

    void      setLayerVisible(int layer, bool visible);
    bool      layerVisible(int layer) const;
    void      setLayerOpacity(int layer, float opacity);   // 0–1
    float     layerOpacity(int layer) const;
    // Lagret ritas bara när zoomen ligger i [min, max]; 0 = obegränsat.
    void      setLayerZoomRange(int layer, double minZoom, double maxZoom);
    // Etikettkollisionen i lagret, på från start: av gör lagrets
    // etiketter alltid synliga — de varken döljs eller tar plats.
    void      setLayerCollision(int layer, bool on);
    bool      layerCollision(int layer) const;
    // HÖRNEN OCH KANTERNA i lagret, på från start: av ger
    // lagret inga Vertex- eller Edge-träffar i `hitTest(lokal, HitOptions)` —
    // ytan, punkterna, linjerna och 2D-formerna träffas som vanligt. En terräng
    // vars hörn ingen ska fånga (höjdmodellen under stolparna) slipper då
    // sökningen bland trianglarna nära strålen. Följer lagret i `moveLayer`;
    // `setLayer`/`setLayers` slår på den. Ritar inte om — bara träffprovet
    // läser den.
    void      setLayerSnap(int layer, bool on);
    bool      layerSnap(int layer) const;
    // Hur lagret ritar SINA former (`DrawOrder` ovan) — lagrens inbördes
    // ordning är `moveLayer`s.
    void      setLayerDrawOrder(int layer, DrawOrder order);
    DrawOrder layerDrawOrder(int layer) const;
    // I RYMDEN (style3d.h): var lagret ligger i 3D-målet
    // — Auto (förval) blir World av en form i rymden, annars Ground — och
    // dess renderläge där (förval: Less med skrivning). Ett ogiltigt
    // renderläge (en uppräkning utanför sina värden, en bias som inte är
    // ändlig) ⇒ `gfx::log` och ingenting ändras.
    void           setLayerPlacement(int layer, LayerPlacement placement);
    LayerPlacement layerPlacement(int layer) const;
    void           setLayerRenderState(int layer, const RenderState& state);
    RenderState    layerRenderState(int layer) const;
    // VYERNA lagret ritas i: 0 är huvudvyn, 1, 2 … är
    // del-vyerna (`addView`) — `{0}` från start, `{1}` bara i den första
    // del-vyn, `{}` i ingen. Ett nummer som inte är en vy ⇒ `gfx::log` och
    // ingenting ändras; dubbletter räknas en gång.
    void             setLayerViews(int layer, std::vector<int> views);
    std::vector<int> layerViews(int layer) const;
    // MÅLARENS PLATS: lagret är en plats för `onPaintLayer` — det
    // som ritas varje bildruta ritas DÄR, efter lagrets egna former, i
    // stället för ovanpå allt (ett rutnät som följer vyn under ett tungt
    // punktmoln eller en bild). Lagrets synlighet, opacitet och zoomintervall
    // gäller målaren som formerna, och målaren hör till huvudvyn och
    // `renderToBitmap`, som `onPaint`. I 3D-målet följer den lagrets
    // placering: på marken utan djup (Ground — också ett Auto-lager, för
    // målarens former räknas inte), i ett World- eller Terrain-lager med
    // lagrets renderläge som målarens start, djuptestad som lagret. Av från
    // start; följer lagret i `moveLayer`, och `setLayer`/`setLayers` släcker den.
    void setLayerPainter(int layer, bool on);
    bool layerPainter(int layer) const;
    // Etiketterna TONAR in och ut när kollisionen visar eller döljer dem, i
    // stället för att växla (av från start).
    void      setLabelFade(bool on);
    bool      labelFade() const { return labelFade_; }

    // ------------------------------------------------------------ FORMERNA
    // Index i lagret, eller −1 när lagret inte finns.
    int  addShape(Shape s);
    int  addShape(int layer, Shape s);
    void setShape(int index, Shape s);
    void setShape(int layer, int index, Shape s);
    void setShapes(std::vector<Shape> shapes);
    void setShapes(int layer, std::vector<Shape> shapes);
    void removeShape(int index);
    void removeShape(int layer, int index);
    int  shapeCount(int layer = 0) const;
    // Formen, eller nullptr utanför.
    const Shape* shape(int layer, int index) const;

    // ------------------------------------------------------ TEXTENS MÅTT
    // Hur stor texten blir i stilen — samma rader, radhöjd och delar
    // som `Shape::text` får, mätt med ritytans egna fonter UTAN att något
    // laddas upp (får anropas i en handler, i onLoad …). Enheten: logiska px
    // (förval, som `Painter::measureText`), mm eller världsenheter — omräknat
    // med skärmens täthet och kamerans skala i MITTEN, som etiketten byggs
    // (ytan och kameran som nästa ritning får dem, också med en väntande
    // `setMapScale` eller inramning); samma enhet som storlekens är exakt.
    // Ett världsmått under tilt är etikettens tänkta storlek (världstext ritas
    // i mittens skala). Kräver ett öppet fönster (fonterna är fönstrets) —
    // annars nullopt. TAKET (`maxLines`/`maxHeight`) räknas med —
    // `lines` är de synliga raderna och `truncated` att texten kortades med
    // "…" — och golvet och taket för storleken (`minSize`/`maxSize`) vid
    // kamerans mitt; zoomintervallet inte (måttet säger hur texten ser ut,
    // inte om den syns). FALLGROP som
    // `Shape::text`: `{}` och `{{"abc"}}` är tvetydiga; `{"abc"}` är text.
    // En FRÅGA och ingen `.glazeui`-form, som `hitTest`. Formen FRÅGAR
    // LAYOUTEN (ytan resolvas), så den får inte anropas ur ritytans egen
    // layout: där loggar den en gång och svarar nullopt — mät i
    // `heightForWidth`/`contentSize` med formen med Ctx nedan.
    std::optional<TextMetrics> measureText(const TrText& text,
                                           const TextStyle& style = {},
                                           Unit unit = Unit::Px) const;
    std::optional<TextMetrics> measureText(const std::vector<TextSpan>& spans,
                                           const TextStyle& style = {},
                                           Unit unit = Unit::Px) const;
    // MÄTNINGEN I LAYOUTEN: samma mått med det Ctx layouten redan har
    // — i en egen ritytas `heightForWidth`/`contentSize` (ett dokument som
    // bryts efter bredden). Frågar ALDRIG layouten: tätheterna och fonterna ur
    // `c` (nullopt utan `c.canvas`), kameran som den är satt eller härledd,
    // med en väntande `setMapScale` — men UTAN en väntande inramning
    // (`zoomToRect`/`zoomToBox`/`panBy` kräver ytan som layouten räknar fram).
    // 3D-kamerans skala vid den senast ritade ytan (annars de satta måtten);
    // utan yta svarar ett mått som behöver världsenheten nullopt. Ett `c` ur
    // ett annat fönster mäter med det fönstrets fonter.
    std::optional<TextMetrics> measureText(const Ctx& c, const TrText& text,
                                           const TextStyle& style = {},
                                           Unit unit = Unit::Px) const;
    std::optional<TextMetrics> measureText(const Ctx& c,
                                           const std::vector<TextSpan>& spans,
                                           const TextStyle& style = {},
                                           Unit unit = Unit::Px) const;

    // ---------------------------------------------------------- DEL-VYERNA
    // (style3d.h `View`) i kollektionsvokabulären. VYNS
    // NUMMER är 1, 2 … — 0 är huvudvyn, ritytan själv, och sätts inte här —
    // och samma nummer står i `setLayerViews`. `addView` svarar med det nya
    // numret (−1 för en ogiltig vy); `setView` ERSÄTTER vyn helt; `setViews`
    // fyller om alla (listans första blir vy 1). `removeView` flyttar de
    // senare vyerna ett nummer ned, och lagrens vylistor följer med — ett
    // lager i den borttagna vyn lämnar den. Färre vyer efter `setViews`
    // tar likadant bort de nummer som inte längre finns. Ett nummer utanför
    // eller en ogiltig vy (target3d.h `validView` — ett ankare utanför de
    // nio, mått som är negativa eller inte ändliga, en ogiltig kamera) ⇒
    // `gfx::log` och ingenting ändras. En del-vy tar ritytan till 3D-målet.
    int         addView(View v);
    void        setView(int view, View v);
    void        setViews(std::vector<View> views);
    void        removeView(int view);
    int         viewCount() const { return static_cast<int>(views_.size()); }
    // Vyn med numret, eller nullptr (också för 0 — huvudvyn är ingen `View`).
    const View* view(int view) const;

    // ---------------------------------------------------- EGNA PROGRAM
    // (style3d.h `Program`) i kollektionsvokabulären —
    // appens shaders, nämnda vid NAMN av `MeshStyle::program`,
    // `PointStyle::program` och `Shape::screenPass`. Namnen är UNIKA på
    // ritytan. `addProgram` svarar med indexet (−1 för ett avslag);
    // `setProgram` ERSÄTTER programmet helt (en ny version — fönstret
    // kompilerar om); `setPrograms` fyller om alla. Ett index utanför, ett
    // tomt eller upprepat namn, `textures` utanför 0–4 eller `uniformFloats`
    // utanför 0–1024 ⇒ `gfx::log` och ingenting ändras. Programmet kompileras
    // per fönster första gången det används — eller när `programError` frågar.
    // Koddata: ingen `.glazeui`-form.
    int            addProgram(Program p);
    void           setProgram(int index, Program p);
    void           setPrograms(std::vector<Program> programs);
    void           removeProgram(int index);
    int            programCount() const;
    // Programmet på indexet, eller nullptr.
    const Program* program(int index) const;
    // FELET för programmet med namnet i ritytans fönster: kompilatorns text
    // med raden i appens EGEN källa (förspelet räknas inte), en saknad källa
    // för backenden ("programmet 'himmel' saknar GLSL-källa (Linux)") eller
    // att programmet inte finns. Tomt för ett giltigt program — och utan ett
    // öppet fönster (renderaren är fönstrets). Kompilerar programmet om det
    // inte gjorts.
    std::string    programError(const std::string& name) const;

    // ------------------------------------------------------------ BILDEN
    // 3D-SCENEN TILL EN BILD (till exempel en miniatyr av en modell):
    // huvudvyns lager med kameran `camera` i w × h bildpunkter, rensade till
    // ritytans bakgrund (`setBackground` med alfa 0 ger en genomskinlig
    // bild), i 3D-målets ordning med rutnätet, `onPaint` och lagrens
    // `onPaintLayer` (handlerna körs alltså — appen vet själv att den bad om
    // en bild). Del-vyerna, kanten, fokusringen och den inaktiverade tonen hör
    // inte till scenen och syns inte; etiketterna placeras för bildens kamera,
    // utan att röra skärmens.
    // Bildpunkterna är fönstrets FYSISKA px: mått i px och mm blir lika stora
    // som på skärmen, och samma kamera i ritytans storlek ger samma pixlar
    // som ritytan. Ritas UTANFÖR en frame — i en handler, `onLoad` … — också
    // med fönstret dolt, men kräver ett öppet fönster (dess renderare). Tomt
    // (+ `gfx::log`) utan fönster, inuti en ritning, med w eller h ≤ 0, en
    // ogiltig kamera eller ett mål som inte gick att skapa. En bild som
    // väntar på bildcachens uppladdningsbudget saknas (loggas).
    std::optional<Bitmap> renderToBitmap(int w, int h, const Camera3D& camera);

    // ------------------------------------------------------------ MINNET
    // RITYTANS MINNESBUDGET i byte: bilderna, render-texturerna och
    // de omräknade bilderna ritytan ritar — räknade på den rityta som ritade
    // dem SENAST — och dess draperingar på terräng, i fönstrets bildcache. Över
    // budgeten vräks det som legat oritat längst (det som ritades i förra
    // framen och bilder utan pixlar aldrig — det loggas). 0 = förvalet
    // `App::canvasMemoryBudget()`. Sättaren LAGRAR och ritar inte om; den
    // gäller från ritytans nästa ritning. En tung karta och en miniatyr i
    // samma fönster får var sin: `karta.setMemoryBudget(std::size_t{2} << 30)`.
    void        setMemoryBudget(std::size_t bytes);
    std::size_t memoryBudget() const;

    bool focusable() const override { return true; }

    // WIDGETS PÅ RITYTAN: barnen klipps till ritytans
    // yta med fältets rundade hörn (raka på en platt yta) — samma klipp som
    // scenen ritas i.
    bool  clipsChildren() const override { return true; }
    float clipRadius(const Ctx& c) const override;

    // FÖRANKRADE BARN — en widget som STÅR I EN VÄRLDSPUNKT: en
    // inforuta, en anteckning, ett fält på kartan. Barnet är ritytans
    // (`karta.adopt(info)`), och ritytan placerar det själv ur kamerans
    // projektion — med sitt mätta mått och `anchor`-hörnet i punkten, samma
    // nio lägen som etiketternas (`BottomLeft` = nedre vänstra hörnet). Det
    // räknas i SAMMA bildruta som scenen ritas, också under trögheten,
    // glidningen och vridsnäppet (de stegas i ritytans ritning, som går före
    // barnens), och dess dock/anchors/x/y ignoreras. Utanför ritytan klipps det
    // som varje barn; en punkt utan plats på skärmen (bakom kameran, ovanför
    // horisonten) ger en TOM yta, och synligheten rörs inte. Sättaren får stå
    // FÖRE `adopt` (`.glazeui` bygger trädet sist): förankringen gäller så
    // länge widgeten är ritytans barn och lever, tills `clearChildAnchor`.
    // En punkt som inte är ändlig ⇒ `gfx::log` och ingenting ändras. I
    // `.glazeui`: `karta.setChildAnchor(info, {674000, 6580100},
    // glaze::TextAnchor::BottomLeft);`.
    struct ChildAnchor {
        Point      world{};
        TextAnchor anchor = TextAnchor::Center;
    };
    void setChildAnchor(Widget& child, Point world,
                        TextAnchor anchor = TextAnchor::Center);
    void clearChildAnchor(Widget& child);
    // Punkten och hörnet barnet står i, eller nullopt (ingen förankring).
    std::optional<ChildAnchor> childAnchor(const Widget& child) const;
    bool       placesChild(const Widget& child) const override;
    gfx::Rectf placeChild(const Widget& child, const Ctx& c) const override;

    // setProp-egenskaper (setter-fälten i textform): background (#RRGGBB),
    // border, gridSize, center ("x, y"), zoom, zoomRange ("min, max"),
    // bearing, pitch, maxPitch, yUp, mapScale ("nämnare" eller "nämnare,
    // meter per enhet"), pannable, zoomable, rotatable, pitchable, inertia,
    // labelFade, rotationSnap, rotationSnapTolerance, multisample, depth,
    // orbitTumble, elevationRange ("min, max"), distanceRange ("min, max"),
    // memoryBudget (byte, ett heltal ≥ 0), flat, inputMode (lägets namn).
    bool setProp(const std::string& key, const std::string& value) override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;
    bool       hasOverlay() const override;
    void       drawOverlay(const Ctx& c) override;

    void mouseDown(const Ctx& c, float x, float y) override;
    void mouseDrag(const Ctx& c, float x, float y) override;
    void mouseMove(const Ctx& c, float x, float y) override;
    bool mouseUp(const Ctx& c, float x, float y) override;
    bool mouseWheel(const Ctx& c, float x, float y, float wx,
                    float wy) override;
    // Ritytan får det RÅA hjulet — dess bindningar avgör.
    bool followsWheelDirection() const override { return false; }
    bool gesture(const Ctx& c, float x, float y, gfx::GesturePhase phase,
                 float scale, float rotationDeg) override;
    void mouseDoubleClick(const Ctx& c, float x, float y) override;
    bool middleDown(const Ctx& c, float x, float y) override;
    void middleDrag(const Ctx& c, float x, float y) override;
    bool middleUp(const Ctx& c, float x, float y) override;
    // `onDoubleClick` med `Middle`/`Right` efter ett tryck ritytans gest tog.
    void middleDoubleClick(const Ctx& c, float x, float y) override;
    // HÖGERKNAPPEN: ritytan tar trycket bara när
    // `RightDragX`/`RightDragY` har en påslagen bindning i läget — annars går
    // det till förfäderna och snabbmenyn, och ett högertryck appen tog är
    // dess (`appPress`).
    bool rightDown(const Ctx& c, float x, float y) override;
    void rightDrag(const Ctx& c, float x, float y) override;
    bool rightUp(const Ctx& c, float x, float y) override;
    void rightDoubleClick(const Ctx& c, float x, float y) override;
    // AVBROTTET: gesten stannar utan tröghet. Appens `onMouseCancel`
    // fyrar fönstret efteråt.
    bool mouseCancel(gfx::MouseButton b) override;
    // ETT TRYCK APPEN TOG: en kastad karta fångas vid trycket
    // (vänster och mitten), och rörelserna och dubbelklicket går
    // till `onMouseMove`/`onDoubleClick` med knappen.
    void appPress(const Ctx& c, float x, float y, gfx::MouseButton b,
                  AppPressStep steg) override;
    // Basens tre mushandlers med ritytans args (`CanvasMouseArgs`).
    bool fireMouse(const Handler<MouseArgs>& h, const Ctx& c,
                   std::optional<gfx::Pointf> pos, gfx::MouseButton b) override;
    gfx::PointerShape cursorAt(const Ctx& c, float x, float y) const override;
    bool keyDown(gfx::Key k, std::uint32_t mods) override;
    // En tangent `onKeyDown` TOG håller tickningen vid liv.
    void keyTaken(gfx::Key k) override;
    void languageChanged() override;

private:
    // Ritningen i ett externt mål med en uttrycklig vy (ett VR-headsets ögon)
    // når scenen inifrån — se glaze/canvas/external_pass.h.
    friend class detail::ExternalCanvasPass;

    // Scenen innanför klippet: rutnätet, lagren och målaren i världen — i
    // 2D-vägen direkt, annars genom 3D-målet (src/canvas/target3d.h).
    void drawScene(const Ctx& c);
    // Ytans hörnradie i logiska px: fältets, och ingen på en platt yta —
    // bakgrundens, scenens och overlayns klipp och barnklippets.
    float surfaceRadius(const Ctx& c) const;
    // Ritningens delade läge (målarna och lagren) ur Ctx:t, med rutan `box`
    // (fönstrets fysiska px, eller 3D-målets).
    void paintContext(const Ctx& c, detail::PaintContext& pc,
                      const gfx::Rectf& box) const;
    // Ritas scenen i 3D-målet (klasshuvudet)?
    bool uses3DTarget() const;
    // Ritningens projektion i rutan — 3D-kamerans, kartkamerans `toView3D` i
    // 3D-målet, annars kartkamerans (träffprovet).
    detail::Projection projection(const gfx::Rectf& box, float scale) const;
    // DEL-VYERNA FÖR TRÄFFPROVET:
    // den ÖVERSTA vyn under den fysiska fönsterpunkten (px, py) i ritytans
    // ruta `box` — 0 = huvudvyn — och en vys projektion i fönstrets px, med
    // SAMMA ruta och kamera som ritningen (target3d.h `viewAt`/`viewRect`/
    // `viewCamera`, med `followRotation` och klipplanen ur vyns lager).
    // Huvudvyns projektion är `projection` ovan.
    int                viewUnder(const gfx::Rectf& box, float scale, double px,
                                 double py) const;
    detail::Projection viewProjection(int view, const gfx::Rectf& box,
                                      float scale) const;
    // 3D-kamerans vy i rutan, med scenens låda för klipplanen.
    gfx::View3D view3DFor(const gfx::Rectf& box) const;
    // HUVUDVYNS 3D-vy som 3D-målet ritar med (target3d.h `mainView3D`) —
    // 3D-kamerans, annars kartkamerans `toView3D`: 3D-mattens och
    // plockningens kamera.
    gfx::View3D mainView3DFor(const gfx::Rectf& box, float scale) const;
    // Kameran som fasaden vill ha den — fysiska px i rutan `box` — och den
    // lokala rymden (logiska px från övre vänstra hörnet).
    gfx::MapView view(const gfx::Rectf& box, float scale) const;
    gfx::MapView screenView(const gfx::Rectf& box, float scale) const;
    // Kamerornas vy "i mitten" i rutan — den text och markörer i
    // världsenheter byggs med: 3D-kamerans skala vid målet när den gäller,
    // annars kartkamerans (ritningens `PaintContext::world`).
    gfx::MapView worldView(const Camera& cam, const Camera3D& cam3,
                           const gfx::Rectf& box, float scale) const;
    // Kameran som gäller: den satta, eller den härledda (lokala px).
    Camera cameraFor(const gfx::Rectf& box, float scale) const;
    // VÄRLDSPUNKTEN I RUTAN `box` (fysiska fönster-px) — `toLocal`s och de
    // förankrade barnens EN projektion (3D-kamerans eller kartkamerans);
    // false bakom kameran.
    bool projectIn(const gfx::Rectf& box, float scale, Point world, double& px,
                   double& py) const;
    // Den levande förankringen för barnet, eller nullptr.
    const ChildAnchor* anchorOf(const Widget& child) const;
    // Den senast kända ytan (fysiska px) och skalan — ritningens, eller en
    // färsk resolve när widgeten sitter i ett öppet fönster.
    bool knownBox(gfx::Rectf& box, float& scale) const;
    // Ytan JUST NU — en färsk resolve i ett öppet fönster, och bara när den
    // har bredd och höjd — med skalan och mm-tätheten
    // ur SAMMA färska Ctx. Inramningen och 3D-panoreringen räknas bara mot
    // den; annars väntar de på en ritning med yta (en inaktiv flik ritas
    // inte, och den cachade ytan och skalan är gamla).
    bool currentBox(gfx::Rectf& box, float& scale, float& pxPerMm) const;
    // Fryser den härledda kameran (utan explicit kamera: mitten i ytans mitt
    // i lokala px) innan en sättare ändrar den.
    void   materialize();
    double clampZoom(double z) const;
    // Glidningens och tröghetens steg i ritningen; båda avbryts av sättarna
    // och av en ny gest.
    void stepEase();
    void stepEase3D();
    void stepCoast(const Ctx& c);
    void stepSnap();
    // Panorering och zoom stoppar glidningarna och trögheten men INTE
    // vridsnäppet — det stoppas bara av en ny vridning och av en kamera som
    // sätts från koden (`stopSnap`).
    void stopMotion()
    {
        easing_   = false;
        easing3D_ = false;
        coasting_ = false;
    }
    void stopSnap() { snapping_ = false; }
    // En SATT kamera ersätter de väntande inramningarna: en äldre
    // zoomToRect/zoomToBox/panBy hade annars skrivit över den vid nästa
    // ritning med yta.
    void dropPendingFits()
    {
        pendingFit_ = pendingBox_ = pendingPan_ = false;
        panDelta_   = Point{};
    }
    // Väntande begäranden som kräver ytan eller skärmen (zoomToRect,
    // zoomToBox, panBy, setMapScale före första ritningen). `pendingCameras`
    // räknar kamerorna de ger i rutan på KOPIOR — en fråga före första
    // ritningen (`measureText`) ser den kamera ritningen får — och
    // `applyPending` verkställer samma räkning.
    void applyPending(const Ctx& c);
    void pendingCameras(const gfx::Rectf& box, float scale, float pxPerMm,
                        Camera& cam, Camera3D& cam3) const;
    // MÄTNINGENS TÄTHET I LAYOUTEN: världens fysiska px per enhet
    // med kameran som den är (och en väntande `setMapScale`), vid den senast
    // ritade ytan eller de satta måtten — aldrig en resolve. 0 = okänd (en
    // 3D-yta utan yta).
    double layoutPxPerUnit(const Ctx& c) const;
    // Mätningen med en given täthet — de två formerna av `measureText` delar
    // den. nullopt utan `c.canvas`, och för ett mått som behöver världens
    // täthet när den är okänd (build.h `measureNeedsWorld`).
    std::optional<TextMetrics> measureWith(const Ctx& c, double pxPerUnit,
                                           const std::vector<TextSpan>& spans,
                                           const TextStyle& style,
                                           Unit unit) const;
    // Kartkameran `cam` inramad kring rektangeln i rutan (zoomToRect), och
    // zoomen kartskalan ger (setMapScale).
    Camera fitRect(Camera cam, const Rect& r, Length pad, const gfx::Rectf& box,
                   float scale, float pxPerMm) const;
    double mapScaleZoom(float scale, float pxPerMm) const;
    // Skalan i fönstret som ritytan sitter i (en ritad eller öppen) — false
    // utan, och då väntar sättaren till nästa ritning.
    bool windowScale(float& scale, float& pxPerMm) const;
    // Musens args ur en fysisk fönsterposition.
    CanvasMouseArgs mouseArgs(const Ctx& c, float x, float y) const;
    // `onDoubleClick` med knappen — ett dubbelklick är en diskret händelse:
    // fönstret ritas om efteråt.
    void fireDoubleClick(const Ctx& c, float x, float y, gfx::MouseButton b);
    // `onMouseMove` under ett drag ritytan håller, VAKTAD: false när handlern
    // tog rörelsen eller rev eller lossade ritytan — draget rörs då inte.
    bool dragMoveHandler(const Ctx& c, float x, float y, gfx::MouseButton b);

    // ---- gesterna — genom bindningstabellen
    // (src/canvas/input_map.h). Vad handlingarna gör med kameran beror på
    // kamerans SORT: kartan, orbiten kring målet eller förstapersonen kring
    // ögat (camera_math `MotionKind`).
    detail::MotionKind motionKind() const;
    // Läget de automatiska raderna står i: "orbit" med en `Camera3D`, annars
    // "map".
    const char*        autoInputMode() const;
    // Handlingarna flaggorna släpper fram, en bit per `InputAction`.
    std::uint32_t      enabledActions(detail::MotionKind k) const;
    // ETT DRAG per knapp: planen läses ur tabellen vid trycket och vid
    // varje rörelse med de aktuella modifierarna. Kartans panorering greppar
    // världspunkten under en VIRTUELL PEKARE, som flyttas med panoreringens
    // mängd — med farten 1 och Normal ÄR den den riktiga pekaren, och
    // trögheten samplar den.
    struct DragState {
        bool   active = false;
        float  lastX = 0.0f, lastY = 0.0f;     // förra pekaren (fysiska px)
        float  pressX = 0.0f, pressY = 0.0f;   // trycket — dragzoomens ankare
        double offX = 0.0, offY = 0.0;         // virtuell − riktig pekare
        bool   grabbed = false;                // `grab` gäller
        Point  grab{};                         // kartans världspunkt
        bool   pans   = false;   // senaste planen panorerade
        bool   others = false;   // … och gjorde något annat
        bool   turned = false;   // draget vred eller tiltade (snäppet)
        // Panoreringens senaste rörelser (trögheten) — en ring.
        std::vector<detail::PanSample> samples;
    };
    DragState&       dragOf(gfx::MouseButton b);
    // Dragets plan för knappen med fönstrets modifierare just nu.
    detail::DragPlan dragPlan(gfx::MouseButton b) const;
    // Draget som pågår (läst ur trycken fönstret håller), eller nullptr.
    const DragState* activeDrag() const;
    // Trycket: false när planen saknar en påslagen handling (då tas det inte).
    bool beginDrag(const Ctx& c, gfx::MouseButton b, float x, float y);
    void moveDrag(const Ctx& c, gfx::MouseButton b, float x, float y);
    // Släppet: trögheten eller vridsnäppet (båda stegas sedan i ritningen).
    void endDrag(const Ctx& c, gfx::MouseButton b);
    void stopDrags();
    // Flaggorna ändrades: varje pågående drag läser om sin plan och slutar
    // bara när ingen verksam handling återstår.
    void reviewDrags();
    // KAMERANS RÖRELSE ur en händelse — handlingarnas mängder summerade per
    // handling (två källor till samma handling adderas), i handlingens enhet
    // (camera_math `bindingAmount`).
    struct CameraMotion {
        double panX = 0.0, panY = 0.0, fly = 0.0, lift = 0.0;
        double zoom = 0.0, rotate = 0.0, tilt = 0.0;
    };
    // Raden `b` med källans råa värde in i rörelsen.
    void addMotion(CameraMotion& m, const InputBinding& b, double raw,
                   detail::MotionKind k, const Ctx& c) const;
    // UTFÖR rörelsen: vridning och tilt, därefter zoom, därefter förflyttning.
    // `ax`/`ay` (fysiska px) är förflyttningens ankare — `toAnchor` true:
    // panoreringen SLUTAR där (tangenterna, ett drag), false: den BÖRJAR där
    // (hjulet). `zoomAt`/`turnAt` är zoomens och vridningens ankare (pekaren,
    // trycket), tomma = ytans mitt respektive kamerans egen vridning. Kartans
    // panorering görs här bara när `mapPan` är sann (ett drag greppar
    // själv). true = kameran ändrades; `turned` sätts när kartans bäring
    // ändrades.
    bool applyMotion(const Ctx& c, const CameraMotion& m, double ax, double ay,
                     bool toAnchor, std::optional<gfx::Pointf> zoomAt,
                     std::optional<gfx::Pointf> turnAt, bool mapPan,
                     bool* turned = nullptr);
    // VRIDSNÄPPET efter en vridande gest: en glidning till närmaste vinkel i
    // stegets följd när bäringen ligger inom toleransen (camera_math) — den
    // egna glidningen ovan, kring ytans mitt.
    void    snapRotation();
    // Zoom med faktorn kring den fysiska punkten (som står kvar). false när
    // zoomen inte ändrades (gränserna).
    bool    zoomAround(const gfx::Rectf& box, float scale, double px, double py,
                       double factor);
    // Vrid bäringen `deg` grader MEDURS på skärmen kring den fysiska punkten
    // (världspunkten under den står kvar). false när bäringen inte ändrades.
    bool    rotateAround(const gfx::Rectf& box, float scale, double px,
                         double py, float deg);
    // KARTANS PANORERING utan grepp: världspunkten under (fx, fy) hamnar
    // under (tx, ty) — fysiska px, exakt också vriden och tiltad. Står (fx,
    // fy) ovanför horisonten greppas ytans mitt, som alltid ligger på marken.
    // false när kameran inte flyttades.
    bool    panAround(const gfx::Rectf& box, float scale, double fx, double fy,
                      double tx, double ty);
    // ---- orbiten — 3D-kamerans rörelser med spannen
    // och tumlingen; false när kameran inte ändrades. Ingen fyrar
    // `onCameraChange` — anroparen gör det för en gest.
    // Azimut och elevation, grader.
    bool    orbitCamera(float dAzimuth, float dElevation);
    // Målplanets punkt under (fx, fy) hamnar i sidled under (tx, ty) —
    // fysiska px i rutan `box` — och fram/bak går som `vertical` säger
    // (camera_math: målplanet, blicken eller världens upp-axel).
    bool    panCamera(const gfx::Rectf& box, double fx, double fy, double tx,
                      double ty, detail::PanVertical vertical);
    // Avståndet × `factor`.
    bool    dollyCamera(double factor);
    // 3D-kameran `cam` inramad kring lådan i rutan `box` (fysiska px) med
    // luften `pad`.
    Camera3D fitBox3D(const Camera3D& cam, const Box3& b, Length pad,
                      const gfx::Rectf& box, float scale, float pxPerMm) const;
    // DET SISTA STEGET i en rörelse som stegas i ritningen (glidningarna,
    // trögheten, snäppet) ber inte om fler frames — men 3D-målets cache
    // läser `damageStamp()`, så kamerans sista läge måste stega den, annars
    // står målet kvar på förra stegets kamera. En frame till, bara i
    // 3D-målet (2D-vägen ritar om varje gång ändå).
    void    cameraLanded();
    // TICKNINGEN: håller något den vid liv — `setTicking` eller en tagen
    // tangent som fortfarande hålls? Släppta tangenter glöms här.
    bool    tickAlive();
    // Ritningens del: en tick postas till pump-svansen (en åt gången).
    void    stepTick();
    // Kamerans ändring var användarens: fyra `onCameraChange` nu (i en
    // händelse) respektive i pump-svansen (ur ritningen — trögheten, snäppet).
    bool    fireCameraChange();
    void    postCameraChange();
    // Finns lagret? Annars loggas anropet (kollektionsvokabulären: logg +
    // ingenting händer).
    bool layerOk(int layer, const char* op) const;

    std::shared_ptr<detail::SceneData> scene_;

    bool       hasBackground_ = false;
    gfx::Color background_{};
    bool       border_   = true;
    bool       flat_     = false;
    double     gridSize_ = 0.0;

    Camera cam_{};
    bool   hasCamera_ = false;
    bool   yUp_       = false;
    double minZoom_   = 0.0;
    double maxZoom_   = 0.0;
    float  maxPitch_  = 60.0f;

    // 3D: kameran, om den gäller, och 3D-målets val.
    // `multisample_` 0 = förvalet (target3d.h `defaultMultisample`), som
    // inte i sig tar ritytan till 3D-målet.
    Camera3D cam3D_{};
    bool     is3D_        = false;
    int      multisample_ = 0;
    bool     depth_       = false;
    Lighting lighting_{};   // meshernas ljus
    // Del-vyerna — vy 1 först.
    std::vector<View> views_;
    // Orbiten: tumlingen och spannen — elevationens förval sätts
    // i konstruktorn ur camera_math (inga tal i widgetens filer).
    bool     orbitTumble_ = false;
    float  minElevation_ = 0.0f;
    float  maxElevation_ = 0.0f;
    double minDistance_  = 0.0;
    double maxDistance_  = 0.0;

    bool          easing_ = false;
    bool          easeUser_ = false;   // glidningen är användarens
    Camera        easeFrom_{}, easeTo_{};
    std::uint64_t easeStart_ = 0;
    int           easeMs_    = 0;
    // 3D-kamerans glidning (`easeTo(Camera3D)`) — appens, aldrig användarens.
    bool          easing3D_ = false;
    Camera3D      ease3From_{}, ease3To_{};
    std::uint64_t ease3Start_ = 0;
    int           ease3Ms_    = 0;

    // TICKNINGEN: appens `setTicking`, tangenterna `onKeyDown` tog
    // (så som `keyDown` fick dem — `isKeyHeld` svarar för dem), om en tick
    // väntar i pump-svansen och när den förra kördes.
    bool                  ticking_     = false;
    std::vector<gfx::Key> tickKeys_;
    bool                  tickRunning_ = false;   // en följd ticks pågår
    bool                  tickPosted_  = false;
    std::uint64_t         tickLast_    = 0;

    // VRIDSNÄPPETS EGEN GLIDNING — bara bäringen. Den kan inte vara `easeTo`:
    // efter en tvåfingersvridning panorerar OS:ets tröghet nästan alltid
    // vidare (fingrarna drog också lite), och varje panoreringssteg stoppar
    // `easeTo`, annars blir snäppet stående halvvägs.
    bool          snapping_  = false;
    float         snapFrom_  = 0.0f;
    float         snapTo_    = 0.0f;
    std::uint64_t snapStart_ = 0;

    bool    pannable_  = false;
    bool    zoomable_  = false;
    bool    rotatable_ = false;
    bool    pitchable_ = false;
    bool    inertia_   = true;
    // Bindningarna: läget och ritytans EGEN tabell — skapad
    // först när appen sätter en rad eller en vändning; dessförinnan ärvs allt.
    std::string                       inputMode_;
    std::unique_ptr<detail::InputMap> ownInput_;
    detail::InputMap&                 ownInput();
    // Dragen — vänster, mitten och höger (gäller medan knappen hålls).
    DragState drags_[3];
    // Vridsnäppets steg och tolerans i grader — förvalen sätts i
    // konstruktorn ur camera_math (inga tal i widgetens filer).
    float rotationSnap_          = 0.0f;
    float rotationSnapTolerance_ = 0.0f;
    // Styrplattans gestsekvens VRED — då snäpper släppet.
    bool  gestureTurned_         = false;
    // ZOOM ELLER VRIDNING i en styrplattegest — aldrig båda (camera_math:
    // `decideGesture`). Före beslutet HÅLLS båda (ln av faktorn, grader) och
    // släpps ikapp när det faller; den andra kastas tills gesten slutar.
    bool   gestureDecided_  = false;
    bool   gestureZooms_    = false;   // beslutet: zoom (annars vridning)
    double gestureLogScale_ = 0.0;
    double gestureDegrees_  = 0.0;
    double gestureDebt_     = 0.0;     // det hållna, betalas av med fingrarna
    // Trögheten: farten och längden ur släppet, var den släpptes och
    // världspunkten som följer med.
    bool          coasting_ = false;
    std::uint64_t coastStart_ = 0;
    double        coastVx_ = 0.0, coastVy_ = 0.0, coastMs_ = 0.0;
    float         coastX_ = 0.0f, coastY_ = 0.0f;
    Point         coastGrab_{};

    // Etiketterna: toningen, och om den pågår (ritytan invaliderar sig själv).
    bool labelFade_       = false;
    bool labelsAnimating_ = false;

    // En `onCameraChange` väntar i pump-svansen; livstecknet släpper den om
    // ritytan dött innan den hann fram.
    bool                  cameraPosted_ = false;
    std::shared_ptr<char> life_ = std::make_shared<char>();

    bool   pendingFit_ = false;
    Rect   fitRect_{};
    Length fitPad_{};
    // zoomToBox och panBy före första ritningen (3D-kameran).
    bool   pendingBox_ = false;
    Box3   fitBox_{};
    Length boxPad_{};
    bool   pendingPan_ = false;
    Point  panDelta_{};   // logiska px, summerade
    bool   pendingScale_ = false;
    double scaleDenominator_ = 0.0;
    double metersPerUnit_    = 1.0;

    // Den senast ritade ytan (fysiska px) och skalan — toWorld/toLocal.
    gfx::Rectf lastBox_{};
    float      lastScale_ = 0.0f;

    // DE FÖRANKRADE BARNEN: pekaren och barnets livstecken (fönstrets
    // `acceptBtn_`-mönster) — pekaren läses bara medan tecknet lever, och en
    // satt förankring före `adopt` väntar här på barnet.
    struct Anchored {
        const Widget*       child = nullptr;
        std::weak_ptr<char> life;
        ChildAnchor         at;
    };
    std::vector<Anchored> anchored_;

    // VAKTEN för `measureText` utan Ctx: sann medan formens egen
    // layoutvandring pågår — ett anrop som kommer under den (ur ritytans
    // `heightForWidth`) svarar nullopt, loggat EN gång per rityta. `mutable`:
    // mätningen är en const-fråga.
    mutable bool measuring_         = false;
    mutable bool measureLoopLogged_ = false;
};

} // namespace glaze
