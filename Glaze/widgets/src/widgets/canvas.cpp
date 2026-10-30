// Canvas — RITYTAN. MÖNSTER: widgeten ritar SIG SJÄLV
// (hård regel 3) med FÅ RADER — ytan via `Draw`, scenen via ritytans egen
// motor i src/canvas/ (lagren, rutnätet, målarna, etiketternas kollision,
// träffprovet). Måtten kommer ur stilen (`canvas*`), färgerna ur temat
// (`panel`, `border`, `canvasGrid`) — inga tal och inga färger i den här
// filen, och ingen `c.r`: scenens GPU-arbete bor i src/canvas/scene.cpp och
// painter.cpp, gesternas tal i src/canvas/camera_math.cpp.
//
// YTAN ÄR ETT FÄLT, LIKA I ALLA TRE RITNINGAR — beslutet står i canvas.h. De
// fyra tillstånden:
//   vila       bakgrunden (`panel` eller appens) med fältets kant `border`
//   hover      — (ytan tänds inte; pekaren är ett hårkors när gesterna är
//              på — `cursorAt`)
//   tryck      — (ytan trycks inte ned; ett drag som panorerar visar den
//              stängda handen, ett som vrider flytta-pekaren)
//   fokus      fristående ring utanför kanten, som fältets — på en PLATT yta
//              rak och innanför ytan, dockningens markering
//   inaktiv    innehållet (rutnät, lager, målarna) tonat till 1 − iconFade;
//              bakgrunden och kanten står kvar, och gesterna står still
// En platt yta har varken kant eller rundade hörn i något tillstånd.
//
// KAMERAN räknas i GLAZES mått (zoom i logiska px per världsenhet, grader)
// och görs om till fasadens `MapView` (fysiska px) här, i `view()` — EN
// sanning för ritningen, musens världspunkter, gesterna, träffprovet och
// toWorld/toLocal.
//
// GESTERNA bor i krokarna: appens handler först, därefter gesten om
// handlern inte satt `handled`. Ett drag ankrar världspunkten under pekaren
// (`gfx::anchorWorldAt` — exakt också under tilt), hjulet zoomar mot pekaren
// på samma sätt, och trögheten och vridsnäppet stegas i ritningen och
// invaliderar ritytan tills de är framme (Progress-mönstret, regel 1).
// STYRPLATTAN går samma vägar: tvåfingersdraget är ett PRECIST
// hjul som panorerar med dragets grepp, nypningen och vridningen är
// `gesture`-kroken med pekaren som ankare.
//
// BINDNINGARNA: VILKEN handling en rörelse gör slås upp i
// tabellen (src/canvas/input_map — ritytans rader över appens, läget, de
// tryckta modifierarna) och HUR MYCKET i camera_math (`bindingAmount`);
// `addMotion` summerar händelsens källor per handling och `applyMotion` utför
// dem med kamerans sort — kartan, orbiten eller förstapersonen. Ett drag läser
// planen vid varje rörelse, och kartans panorering greppar under en VIRTUELL
// pekare som flyttas med panoreringens mängd (med förvalen den riktiga).
//
// VÄGVALET: `drawScene` ritar i 2D-vägen, eller — med ett World-lager, en `Camera3D`, `setMultisample(> 1)`
// eller `setDepth(true)` — genom 3D-målet (src/canvas/target3d.h), som ritar
// scenen i ett eget mål och kompositar det i klippet ovan. Tal och GPU-arbete
// för 3D-målet bor där.
//
// ORBITEN: med en `Camera3D` går samma krokar till
// 3D-kamerans rörelser — `orbitCamera`/`panCamera`/`dollyCamera` över
// camera_math (spannen och tumlingen), ingen tröghet och inget snäpp. Appens
// del: mittenknappen i `onMouseDown`/`Move`/`Up`, `onGesture` före
// styrplattans inbyggda gest och TICKEN — `stepTick` postar `onTick` från
// ritningen så länge `setTicking` eller en tagen tangent (`isKeyHeld`) håller
// den vid liv, och därefter ingenting (regel 1).

#include "glaze/widgets/canvas.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include <gfx/camera.h>
#include <gfx/platform.h>

#include "../canvas/build.h"
#include "../canvas/camera_math.h"
#include "../canvas/drape.h"
#include "../canvas/hit_index.h"
#include "../canvas/input_map.h"
#include "../canvas/labels.h"
#include "../canvas/pick3d.h"
#include "../canvas/resources.h"
#include "../canvas/scene.h"
#include "../canvas/target3d.h"
#include "../profiler_hooks.h"
#include "../prop_parse.h"
#include "glaze/app.h"
#include "glaze/app_window.h"
#include "glaze/draw.h"

namespace glaze {

// Registrerar typnamnet i widget-registret — hela widgeten bor här.
GLAZE_REGISTER_WIDGET("canvas", Canvas)

using detail::wrapBearing;

namespace {

// MÅLARNA — `onPaint`, `onPaintLayer` och `onPaintOverlay` går EN väg: en
// målare i ritningens läge (i världen, eller i de lokala px för overlayn),
// lagret i args (−1 utanför ett lager) och, för ett lager i rymden, lagrets
// renderläge som målarens start. Målaren tömmer sin sista sats när
// den går ur räckvidd — innan anroparen släpper ett klipp.
void måla(Canvas& yta, const Handler<PaintArgs>& h, detail::PaintContext& p,
          bool screen, int lager = -1, const RenderState* läge = nullptr)
{
    Painter painter(p, screen);
    if (läge)
        painter.setRenderState(*läge);
    PaintArgs e(painter);
    e.layer = lager;
    h(&yta, e);
}

// LAGRENS MÅLARE (scene.h `LayerPaint`) — tom utan `onPaintLayer`.
detail::LayerPaint lagerMålare(Canvas& yta)
{
    if (!yta.onPaintLayer)
        return {};
    return [&yta](detail::PaintContext& p, int lager, const RenderState* läge) {
        måla(yta, yta.onPaintLayer, p, false, lager, läge);
    };
}

} // namespace

namespace detail {

// 3D-MÅLETS ARGUMENT som ritningen, `renderToBitmap` och de externa målen
// (`ExternalCanvasPass`) delar — se target3d.h.
Target3DArgs canvasTarget3DArgs(Canvas& yta, const Ctx& c)
{
    Target3DArgs a;
    a.samples     = yta.multisample();
    a.gridSize    = yta.gridSize();
    a.gridColor   = c.theme->colors.canvasGrid;
    a.gridMinPx   = c.style->canvasGridMin;
    a.gridWidthPx = c.style->canvasGridWidth;
    if (yta.onPaint)
        a.paint = [&yta](PaintContext& p) {
            måla(yta, yta.onPaint, p, false);
        };
    a.layerPaint = lagerMålare(yta);
    return a;
}

} // namespace detail

Canvas::Canvas()
    : scene_(std::make_shared<detail::SceneData>()),
      minElevation_(detail::defaultElevationMin()),
      maxElevation_(detail::defaultElevationMax()),
      rotationSnap_(detail::defaultRotationSnap()),
      rotationSnapTolerance_(detail::defaultRotationSnapTolerance())
{
    // Lager 0 finns från start — `addShape(form)` lägger där.
    scene_->layers.push_back(std::make_shared<detail::LayerData>());
}

Canvas::~Canvas() = default;

// ---------------------------------------------------------------- egenskaper

bool Canvas::setProp(const std::string& key, const std::string& value)
{
    const auto ogiltig = [&] {
        gfx::log("setProp(%s): ogiltigt %s: %s (ignoreras)", name.c_str(),
                 key.c_str(), value.c_str());
        return true;   // nyckeln är känd — värdet var fel
    };
    double a = 0.0, b = 0.0;
    float  f = 0.0f;
    if (key == "background") {
        gfx::Color c;
        if (!propparse::parseHexColor(value, c))
            return ogiltig();
        setBackground(c);
        return true;
    }
    if (key == "border") {
        setBorder(propparse::parseBool(value));
        return true;
    }
    if (key == "flat") {
        setFlat(propparse::parseBool(value));
        return true;
    }
    if (key == "yUp") {
        setYUp(propparse::parseBool(value));
        return true;
    }
    // 3D-målet — djupet och provtagningen.
    if (key == "depth") {
        setDepth(propparse::parseBool(value));
        return true;
    }
    if (key == "multisample") {
        if (!propparse::parseDouble(value, a) || a != std::floor(a))
            return ogiltig();
        setMultisample(static_cast<int>(a));
        return true;
    }
    // MINNET: byte — ett heltal ≥ 0 som ryms i size_t, prövat FÖRE
    // omvandlingen (1e20 hade annars varit odefinierat).
    if (key == "memoryBudget") {
        if (!propparse::parseDouble(value, a) || !std::isfinite(a) || a < 0.0 ||
            a != std::floor(a) ||
            !(a < static_cast<double>(std::numeric_limits<std::size_t>::max())))
            return ogiltig();
        setMemoryBudget(static_cast<std::size_t>(a));
        return true;
    }
    // Gesterna, etiketternas toning och orbitens tumling
    // — en sanningsflagga var.
    if (key == "pannable" || key == "zoomable" || key == "rotatable" ||
        key == "pitchable" || key == "inertia" || key == "labelFade" ||
        key == "orbitTumble") {
        const bool on = propparse::parseBool(value);
        if (key == "pannable")
            setPannable(on);
        else if (key == "zoomable")
            setZoomable(on);
        else if (key == "rotatable")
            setRotatable(on);
        else if (key == "pitchable")
            setPitchable(on);
        else if (key == "inertia")
            setInertia(on);
        else if (key == "orbitTumble")
            setOrbitTumble(on);
        else
            setLabelFade(on);
        return true;
    }
    // BINDNINGARNAS LÄGE — namnet rakt av, tomt =
    // automatiskt; raderna själva är Call-satser.
    if (key == "inputMode") {
        setInputMode(value);
        return true;
    }
    // Orbitens spann — ett tal utanför sitt spann loggar sättaren.
    if (key == "elevationRange" || key == "distanceRange") {
        if (!propparse::parseDoublePair(value, a, b))
            return ogiltig();
        if (key == "elevationRange")
            setElevationRange(static_cast<float>(a), static_cast<float>(b));
        else
            setDistanceRange(a, b);
        return true;
    }
    if (key == "gridSize") {
        if (!propparse::parseDouble(value, a))
            return ogiltig();
        setGridSize(a);
        return true;
    }
    if (key == "center") {
        if (!propparse::parseDoublePair(value, a, b))
            return ogiltig();
        setCenter(a, b);
        return true;
    }
    if (key == "zoom") {
        if (!propparse::parseDouble(value, a))
            return ogiltig();
        setZoom(a);
        return true;
    }
    if (key == "zoomRange") {
        if (!propparse::parseDoublePair(value, a, b))
            return ogiltig();
        setZoomRange(a, b);
        return true;
    }
    if (key == "bearing" || key == "pitch" || key == "maxPitch") {
        if (!propparse::parseNumber(value, f))
            return ogiltig();
        if (key == "bearing")
            setBearing(f);
        else if (key == "pitch")
            setPitch(f);
        else
            setMaxPitch(f);
        return true;
    }
    // Vridsnäppet: ett tal som inte går att läsa är ogiltigt här, ett tal
    // utanför sitt spann loggar sättaren själv.
    if (key == "rotationSnap" || key == "rotationSnapTolerance") {
        if (!propparse::parseNumber(value, f))
            return ogiltig();
        if (key == "rotationSnap")
            setRotationSnap(f);
        else
            setRotationSnapTolerance(f);
        return true;
    }
    if (key == "mapScale") {
        // "nämnare" eller "nämnare, meter per enhet" — setMapScale har ett
        // förvalt andra argument, så båda formerna är riktig C++.
        if (propparse::parseDoublePair(value, a, b)) {
            setMapScale(a, b);
            return true;
        }
        if (!propparse::parseDouble(value, a))
            return ogiltig();
        setMapScale(a);
        return true;
    }
    return Widget::setProp(key, value);
}

// ------------------------------------------------------------------ måtten

gfx::Sizef Canvas::contentSize(const Ctx& c) const
{
    switch (c.ui) {
    case UI::Slate:
    default: {
        // Axelregeln: en satt axel vinner, en auto-axel får stilens mått —
        // en rityta har inget innehåll att mäta (den visar en värld).
        gfx::Sizef s{width, height};
        if (s.w <= 0.0f)
            s.w = c.style->canvasDefaultW;
        if (s.h <= 0.0f)
            s.h = c.style->canvasDefaultH;
        return s;
    }
    }
}

// ----------------------------------------------------------------- ritningen

void Canvas::draw(const Ctx& c)
{
    lastBox_   = c.box;
    lastScale_ = c.scale;
    applyPending(c);
    stepEase();
    stepEase3D();
    stepSnap();
    stepCoast(c);
    stepTick();

    const gfx::Rectf b   = c.bounds();
    const auto&      m   = *c.style;
    const auto&      col = c.theme->colors;
    const float      rad = surfaceRadius(c);
    switch (c.ui) {
    // ALLA TRE — ritytan är ett fält och lika i varje ritning (canvas.h):
    // ytan, scenen i ett klipp med fältets hörn, kanten OVANPÅ scenen (den
    // ska synas mot vilket innehåll som helst) och fokusringen utanför. En
    // PLATT yta: raka hörn och ingen kant.
    case UI::Glaze:
    case UI::Minimalistic:
    case UI::Slate:
    default:
        Draw::rect(c, b, rad, background(c));
        Draw::clip(c, b, rad);
        drawScene(c);
        Draw::endClip(c);
        if (border_ && !flat_)
            Draw::frame(c, b, rad, Pen{col.border, m.borderWidth});
        break;
    }
    if (!focused() || !isEnabled())
        return;
    // Den platta ytans markering ligger INNANFÖR ytan, rak, så den aldrig
    // ritas över grannen (dockningens form).
    if (flat_)
        Draw::frame(c, Draw::inset(b, m.focusWidth / 2.0f), rad,
                    Pen{col.accent, m.focusWidth});
    else
        Draw::frame(c, Draw::inset(b, -m.focusOffset), rad + m.focusOffset,
                    Pen{col.accent, m.focusWidth});
}

float Canvas::surfaceRadius(const Ctx& c) const
{
    return flat_ ? 0.0f : c.style->cornerRadius;
}

void Canvas::paintContext(const Ctx& c, detail::PaintContext& pc,
                          const gfx::Rectf& box) const
{
    pc.c       = &c;
    pc.res     = c.canvas;
    // Med 3D-kameran mäts "mitten" vid dess mål (camera_math `mapViewAt`) —
    // 3D-målet ritar genom 3D-vyn, som det sätter självt.
    pc.world   = worldView(cameraFor(box, c.scale), cam3D_, box, c.scale);
    pc.screen  = screenView(box, c.scale);
    pc.zoom    = is3D_ ? pc.world.pxPerUnit / c.scale
                       : cameraFor(box, c.scale).zoom;
    pc.opacity = isEnabled() ? 1.0f : 1.0f - c.style->iconFade;
    pc.lighting = &lighting_;   // meshernas ljus (src/canvas/scene3d.h)
}

bool Canvas::uses3DTarget() const
{
    return is3D_ || multisample_ > 1 || depth_ || !views_.empty() ||
           detail::hasWorldLayer(*scene_) || detail::hasScreenPass(*scene_);
}

void Canvas::drawScene(const Ctx& c)
{
    if (!c.canvas)
        return;   // en Ctx utan fönstrets resurser (ingen renderer att ladda upp i)
    // Bilderna ritningen hämtar räknas på ritytans konto.
    const detail::ImageAccount konto(c, scene_);
    detail::PaintContext pc;
    paintContext(c, pc, c.box);

    if (uses3DTarget()) {
        // 3D-MÅLET (src/canvas/target3d.h): scenen i målets px, kompositen
        // i fönstrets.
        detail::PaintContext tc;
        paintContext(c, tc, detail::targetBox(c.box));
        detail::Target3DArgs a = detail::canvasTarget3DArgs(*this, c);
        a.stamp          = damageStamp();
        a.camera         = is3D_ ? &cam3D_ : nullptr;
        a.labelFade      = labelFade_;
        a.views          = views_;
        // Etiketterna tonar, eller draperingar väntar på nästa bilds budget
        // (src/canvas/drape.h): nästa frame tar nästa steg (Progress-
        // mönstret) — och när inget väntar ber ritytan inte om fler (regel 1).
        const detail::Target3DState läge = detail::drawTarget3D(pc, tc, scene_, a);
        labelsAnimating_ = läge.labelsAnimating;
        if (läge.labelsAnimating || läge.drapesPending)
            invalidate();
        return;
    }

    // 2D-VÄGEN: 3D-målen från föregående
    // ritning släpps. Profilerarens faser följer stegen:
    // rutnätet och lagren ritas, lagren förbereds däremellan, och `onPaint`
    // målar sist.
    c.canvas->dropViews(scene_->id, detail::kMainView);
    std::optional<detail::PhaseScope> fas{std::in_place, ProfilePhase::CanvasDraw};
    detail::drawGrid(pc, gridSize_, c.theme->colors.canvasGrid,
                     c.style->canvasGridMin, c.style->canvasGridWidth);
    // Etiketterna tonar: nästa frame tar nästa steg (Progress-mönstret) —
    // och när toningen är framme ber ritytan inte om fler (regel 1).
    fas.emplace(ProfilePhase::CanvasPrepare);
    const detail::LayerFrame lf =
        detail::prepareLayers(pc, scene_, labelFade_, false);
    fas.emplace(ProfilePhase::CanvasDraw);
    // Lagrens målare i lagrens ordning, `onPaint` ovanpå dem.
    const detail::LayerPaint lagren = lagerMålare(*this);
    detail::drawGround(pc, *scene_, lf, detail::GroundPart::All, &lagren);
    labelsAnimating_ = lf.animating();
    if (labelsAnimating_)
        invalidate();
    // Ritningen laddade upp det som syns; resten med det som blir kvar av
    // framens budget (src/canvas/scene.h `prepareUploads`).
    fas.emplace(ProfilePhase::CanvasPrepare);
    detail::prepareUploads(pc, *scene_);
    fas.emplace(ProfilePhase::CanvasPaint);
    if (onPaint)
        måla(*this, onPaint, pc, false);
}

// OVERLAY-LAGRET: `onPaintOverlay` vid varje present, ovanpå allt
// innehåll — fönstret ger förfädernas klipp, och ritytan klipper själv till
// sin egen yta med ytans hörn (lagret klipps inte av widgetens rect; raka på
// en platt yta).
bool Canvas::hasOverlay() const
{
    return static_cast<bool>(onPaintOverlay);
}

void Canvas::drawOverlay(const Ctx& c)
{
    if (!c.canvas || !onPaintOverlay)
        return;
    const detail::ImageAccount konto(c, scene_);
    detail::PaintContext pc;
    paintContext(c, pc, c.box);
    Draw::clip(c, c.bounds(), surfaceRadius(c));
    // Målaren tömmer sin sista sats innanför klippet (`måla`).
    måla(*this, onPaintOverlay, pc, true);
    Draw::endClip(c);
}

void Canvas::invalidateOverlay()
{
    if (AppWindow* w = appWindow())
        w->invalidateOverlay();
}

// Barnklippets radie i FYSISKA px — samma rundade yta som scenen klipps till
// i `draw()` (ytans hörn, ingen inset), så ett barn i hörnet klipps likadant.
float Canvas::clipRadius(const Ctx& c) const
{
    return surfaceRadius(c) * c.scale;
}

void Canvas::setBackground(gfx::Color c)
{
    hasBackground_ = true;
    background_    = c;
    invalidate();
}

gfx::Color Canvas::background(const Ctx& c) const
{
    return hasBackground_ ? background_ : c.theme->colors.panel;
}

void Canvas::setBorder(bool on)
{
    if (border_ == on)
        return;
    border_ = on;
    invalidate();
}

// PLATT YTA. En fokusring utanför ytan försvinner med bytet:
// `invalidate` skadar också den senast ritade ytan, överritningen inräknad.
void Canvas::setFlat(bool on)
{
    if (flat_ == on)
        return;
    flat_ = on;
    invalidate();
}

void Canvas::setGridSize(double size)
{
    const double s = std::isfinite(size) && size > 0.0 ? size : 0.0;
    if (gridSize_ == s)
        return;
    gridSize_ = s;
    invalidate();
}

// ------------------------------------------------------------------ kameran

Camera Canvas::cameraFor(const gfx::Rectf& box, float scale) const
{
    if (hasCamera_)
        return cam_;
    // UTAN KAMERA: världen är de lokala logiska px — den lokala rymdens mitt,
    // zoom 1, ingen vridning och ingen tilt. Före första ritningen (ingen
    // skala) är ytan de satta måtten.
    Camera           d;
    const gfx::MapView local =
        scale > 0.0f ? detail::localView(box, scale)
                     : detail::localView(gfx::Rectf{0.0f, 0.0f, width, height},
                                         1.0f);
    d.center = Point{local.cx, local.cy};
    d.zoom   = clampZoom(d.zoom);   // zoomens gränser gäller också här
    return d;
}

gfx::MapView Canvas::view(const gfx::Rectf& box, float scale) const
{
    return detail::mapView(cameraFor(box, scale), yUp_, maxPitch_, box, scale);
}

gfx::MapView Canvas::screenView(const gfx::Rectf& box, float scale) const
{
    return detail::localView(box, scale);
}

gfx::MapView Canvas::worldView(const Camera& cam, const Camera3D& cam3,
                               const gfx::Rectf& box, float scale) const
{
    return is3D_ ? detail::mapViewAt(cam3, box, yUp_)
                 : detail::mapView(cam, yUp_, maxPitch_, box, scale);
}

bool Canvas::knownBox(gfx::Rectf& box, float& scale) const
{
    if (lastScale_ > 0.0f) {
        box   = lastBox_;
        scale = lastScale_;
        return true;
    }
    if (const AppWindow* w = appWindow(); w && w->isOpen()) {
        const Ctx c = behaviorCtx();
        box         = resolve(c);
        scale       = c.scale;
        return true;
    }
    return false;
}

bool Canvas::currentBox(gfx::Rectf& box, float& scale, float& pxPerMm) const
{
    const AppWindow* w = appWindow();
    if (!w || !w->isOpen())
        return false;
    const Ctx        c = behaviorCtx();   // knownBox:s färska resolve
    const gfx::Rectf r = resolve(c);
    if (!(r.w > 0.0f) || !(r.h > 0.0f))
        return false;
    box     = r;
    scale   = c.scale;
    pxPerMm = c.pxPerMm;
    return true;
}

void Canvas::materialize()
{
    if (hasCamera_)
        return;
    gfx::Rectf box{};
    float      scale = 0.0f;
    knownBox(box, scale);
    cam_       = cameraFor(box, scale);
    hasCamera_ = true;
}

double Canvas::clampZoom(double z) const
{
    if (minZoom_ > 0.0)
        z = std::max(z, minZoom_);
    if (maxZoom_ > 0.0)
        z = std::min(z, maxZoom_);
    return z;
}

void Canvas::setCenter(double x, double y)
{
    if (!std::isfinite(x) || !std::isfinite(y)) {
        gfx::log("Canvas(%s)::setCenter: icke-ändlig punkt (ignoreras)",
                 name.c_str());
        return;
    }
    stopMotion();
    materialize();
    cam_.center = Point{x, y};
    invalidate();
}

Point Canvas::center() const
{
    return camera().center;
}

void Canvas::setZoom(double zoom)
{
    if (!std::isfinite(zoom) || !(zoom > 0.0)) {
        gfx::log("Canvas(%s)::setZoom: zoomen måste vara > 0 (%g ignoreras)",
                 name.c_str(), zoom);
        return;
    }
    stopMotion();
    materialize();
    cam_.zoom = clampZoom(zoom);
    invalidate();
}

double Canvas::zoom() const
{
    return camera().zoom;
}

void Canvas::setZoomRange(double minZoom, double maxZoom)
{
    minZoom_ = std::isfinite(minZoom) && minZoom > 0.0 ? minZoom : 0.0;
    maxZoom_ = std::isfinite(maxZoom) && maxZoom > 0.0 ? maxZoom : 0.0;
    if (minZoom_ > 0.0 && maxZoom_ > 0.0 && maxZoom_ < minZoom_)
        std::swap(minZoom_, maxZoom_);
    if (hasCamera_)
        cam_.zoom = clampZoom(cam_.zoom);
    invalidate();   // den härledda kameran klampas också (cameraFor)
}

void Canvas::setBearing(float deg)
{
    stopMotion();
    stopSnap();
    materialize();
    cam_.bearing = wrapBearing(deg);
    invalidate();
}

float Canvas::bearing() const
{
    return camera().bearing;
}

void Canvas::setPitch(float deg)
{
    stopMotion();
    materialize();
    cam_.pitch = std::isfinite(deg) ? std::clamp(deg, 0.0f, maxPitch_) : 0.0f;
    invalidate();
}

float Canvas::pitch() const
{
    return camera().pitch;
}

void Canvas::setMaxPitch(float deg)
{
    maxPitch_ = std::isfinite(deg)
                    ? std::clamp(deg, 0.0f, static_cast<float>(gfx::kMaxPitchDeg))
                    : 0.0f;
    if (hasCamera_ && cam_.pitch > maxPitch_)
        cam_.pitch = maxPitch_;
    invalidate();
}

void Canvas::setYUp(bool on)
{
    if (yUp_ == on)
        return;
    yUp_        = on;
    scene_->yUp = on;
    // Bilderna står rättvända efter y-riktningen (Shape::image) — deras uv
    // bakas in i geometrin, så lagren med bilder byggs om.
    for (const auto& L : scene_->layers)
        for (const auto& run : L->runs)
            if (run->key.kind == detail::PartKind::Image) {
                L->dirty = true;
                break;
            }
    invalidate();
}

void Canvas::setCamera(const Camera& c)
{
    stopMotion();
    stopSnap();
    if (is3D_)
        stopDrags();   // ett pågående orbitdrag slutar med 3D-kameran — kartans
                       // gester greppar på sitt eget sätt
    dropPendingFits();
    hasCamera_ = true;
    is3D_      = false;   // kartkameran tar över från en 3D-kamera
    cam_.center = std::isfinite(c.center.x) && std::isfinite(c.center.y)
                      ? c.center
                      : cam_.center;
    cam_.zoom    = std::isfinite(c.zoom) && c.zoom > 0.0 ? clampZoom(c.zoom)
                                                         : cam_.zoom;
    cam_.bearing = wrapBearing(c.bearing);
    cam_.pitch   = std::isfinite(c.pitch) ? std::clamp(c.pitch, 0.0f, maxPitch_)
                                          : 0.0f;
    invalidate();
}

Camera Canvas::camera() const
{
    if (hasCamera_)
        return cam_;
    gfx::Rectf box{};
    float      scale = 0.0f;
    knownBox(box, scale);
    return cameraFor(box, scale);
}

// ----------------------------------------------------------------- 3D-läget

void Canvas::setCamera(const Camera3D& c)
{
    if (!detail::validCamera3D(c)) {
        gfx::log("Canvas(%s)::setCamera: ogiltig Camera3D (icke-ändliga tal, "
                 "avståndet ≤ 0, synfältet utanför (0, 180) eller klipplanen) — "
                 "ignoreras",
                 name.c_str());
        return;
    }
    stopMotion();
    stopSnap();
    if (!is3D_)
        stopDrags();   // ett pågående kartdrag slutar med kartkameran
                       // (setCamera(Camera)s regel åt andra hållet)
    dropPendingFits();
    cam3D_ = c;
    is3D_  = true;
    invalidate();
}

void Canvas::setMultisample(int samples)
{
    if (!detail::validMultisample(samples)) {
        gfx::log("Canvas(%s)::setMultisample: %d prov — bara 1, 2, 4 eller 8 "
                 "(ignoreras)",
                 name.c_str(), samples);
        return;
    }
    multisample_ = samples;
    invalidate();
}

int Canvas::multisample() const
{
    return multisample_ > 0 ? multisample_ : detail::defaultMultisample();
}

void Canvas::setDepth(bool on)
{
    if (depth_ == on)
        return;
    depth_ = on;
    invalidate();
}

void Canvas::setLighting(const Lighting& l)
{
    const auto fin3 = [](const Vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    if (!fin3(l.direction) || !fin3(l.rimDirection) || !std::isfinite(l.ambient) ||
        !std::isfinite(l.diffuse) || !std::isfinite(l.exponent) ||
        !std::isfinite(l.rim) || !std::isfinite(l.fogStart) ||
        !std::isfinite(l.fogEnd) || !std::isfinite(l.fogPower)) {
        gfx::log("Canvas(%s)::setLighting: ett tal i ljuset är inte ändligt "
                 "(ignoreras)",
                 name.c_str());
        return;
    }
    lighting_ = l;
    invalidate();
}

gfx::View3D Canvas::view3DFor(const gfx::Rectf& box) const
{
    return detail::view3D(cam3D_, box, detail::sceneBox(*scene_));
}

detail::Projection Canvas::projection(const gfx::Rectf& box, float scale) const
{
    if (is3D_)
        return detail::Projection(view3DFor(box));
    const gfx::MapView v = view(box, scale);
    if (!uses3DTarget())
        return detail::Projection(v);
    // Kartkameran i 3D-målet ritas som `toView3D` över scenens höjder —
    // samma bild i x/y, men djupet (och därmed vad som ligger bakom
    // närplanet) är 3D-vyns (target3d.h `mainView3D`).
    return detail::Projection(detail::mainView3D(nullptr, v, *scene_));
}

// --------------------------------------------------- 3D-kamerans verktyg
// Gesterna och verktygen går genom samma tre rörelser
// nedan; spannen och tumlingen gäller båda, `onCameraChange` bara gesterna.

bool Canvas::orbitCamera(float dAzimuth, float dElevation)
{
    stopMotion();   // en ny rörelse tar kameran ur en glidning
    const Camera3D k = detail::orbitCamera3D(cam3D_, dAzimuth, dElevation,
                                             orbitTumble_, minElevation_,
                                             maxElevation_);
    if (k.azimuth == cam3D_.azimuth && k.elevation == cam3D_.elevation)
        return false;
    cam3D_ = k;
    invalidate();
    return true;
}

bool Canvas::panCamera(const gfx::Rectf& box, double fx, double fy, double tx,
                       double ty, detail::PanVertical vertical)
{
    stopMotion();
    Camera3D k = cam3D_;
    if (!detail::panCamera3D(k, box, fx, fy, tx, ty, vertical) ||
        (k.target.x == cam3D_.target.x && k.target.y == cam3D_.target.y &&
         k.target.z == cam3D_.target.z))
        return false;
    cam3D_ = k;
    invalidate();
    return true;
}

bool Canvas::dollyCamera(double factor)
{
    stopMotion();
    const Camera3D k =
        detail::dollyCamera3D(cam3D_, factor, minDistance_, maxDistance_);
    if (k.distance == cam3D_.distance)
        return false;
    cam3D_ = k;
    invalidate();
    return true;
}

void Canvas::orbitBy(float dAzimuth, float dElevation)
{
    if (!is3D_) {
        gfx::log("Canvas(%s)::orbitBy: ingen 3D-kamera (ignoreras)", name.c_str());
        return;
    }
    orbitCamera(dAzimuth, dElevation);
}

void Canvas::panBy(Point deltaLocal)
{
    if (!is3D_ || !std::isfinite(deltaLocal.x) || !std::isfinite(deltaLocal.y)) {
        gfx::log("Canvas(%s)::panBy: ingen 3D-kamera eller en förflyttning som "
                 "inte är ändlig (ignoreras)",
                 name.c_str());
        return;
    }
    gfx::Rectf box{};
    float      scale = 0.0f, pxPerMm = 0.0f;
    if (pendingBox_ || !currentBox(box, scale, pxPerMm)) {
        // Ingen yta nu — målplanets skala beror på den — eller en inramning
        // som väntar och ska komma FÖRE (`pendingCameras`s ordning). Nästa
        // ritning med yta.
        pendingPan_ = true;
        panDelta_.x += deltaLocal.x;
        panDelta_.y += deltaLocal.y;
        invalidate();
        return;
    }
    const double cx = box.x + box.w / 2.0, cy = box.y + box.h / 2.0;
    panCamera(box, cx, cy, cx + deltaLocal.x * scale, cy + deltaLocal.y * scale,
              detail::PanVertical::Plane);   // verktyget: exakt i målplanet
}

void Canvas::dollyBy(double factor)
{
    if (!is3D_ || !std::isfinite(factor) || !(factor > 0.0)) {
        gfx::log("Canvas(%s)::dollyBy: ingen 3D-kamera eller en faktor som inte "
                 "är > 0 (ignoreras)",
                 name.c_str());
        return;
    }
    dollyCamera(factor);
}

void Canvas::easeTo(const Camera3D& target, int ms)
{
    // Utan en 3D-kamera att glida ifrån, utan tid eller med en ogiltig kamera
    // gör sättaren jobbet — och loggar det ogiltiga.
    if (ms <= 0 || !is3D_ || !detail::validCamera3D(target)) {
        setCamera(target);
        return;
    }
    const Camera3D from = cam3D_;
    stopMotion();
    stopSnap();
    ease3From_  = from;
    ease3To_    = target;
    ease3Start_ = gfx::ticksMs();
    ease3Ms_    = ms;
    easing3D_   = true;
    invalidate();
}

void Canvas::stepEase3D()
{
    if (!easing3D_)
        return;
    const double t =
        static_cast<double>(gfx::ticksMs() - ease3Start_) / ease3Ms_;
    if (t >= 1.0) {
        cam3D_    = ease3To_;
        easing3D_ = false;   // framme — ingen mer självinvalidering (regel 1)
        cameraLanded();
        return;
    }
    cam3D_ = detail::easeCamera3D(ease3From_, ease3To_, t);
    invalidate();   // nästa frame tar nästa steg — Progress-mönstret
}

void Canvas::cameraLanded()
{
    if (uses3DTarget())
        invalidate();
}

void Canvas::zoomToBox(const Box3& b, Length pad)
{
    if (!(b.min.x <= b.max.x) || !(b.min.y <= b.max.y) || !(b.min.z <= b.max.z) ||
        !is3D_) {
        gfx::log("Canvas(%s)::zoomToBox: omvänd låda eller ingen 3D-kamera "
                 "(ignoreras)",
                 name.c_str());
        return;
    }
    stopMotion();
    gfx::Rectf box{};
    float      scale = 0.0f, pxPerMm = 0.0f;
    if (currentBox(box, scale, pxPerMm)) {
        cam3D_      = fitBox3D(cam3D_, b, pad, box, scale, pxPerMm);
        pendingBox_ = pendingPan_ = false;   // äldre väntande är ersatta
        panDelta_   = Point{};
    } else {
        pendingBox_ = true;   // ingen yta nu — nästa ritning med yta
        fitBox_     = b;
        boxPad_     = pad;
    }
    invalidate();
}

Camera3D Canvas::fitBox3D(const Camera3D& cam, const Box3& b, Length pad,
                          const gfx::Rectf& box, float scale,
                          float pxPerMm) const
{
    // Luften i FYSISKA px (rutans enhet); i världsenheter växer lådan i
    // stället, åt alla håll.
    Box3   g     = b;
    double padPx = 0.0;
    if (pad.unit == Unit::Mm) {
        padPx = pad.value * pxPerMm;
    } else if (pad.unit == Unit::World) {
        g.min = {b.min.x - pad.value, b.min.y - pad.value, b.min.z - pad.value};
        g.max = {b.max.x + pad.value, b.max.y + pad.value, b.max.z + pad.value};
    } else {
        padPx = pad.value * scale;
    }
    return detail::fitBox(cam, g, box, padPx);
}

void Canvas::setOrbitTumble(bool on)
{
    orbitTumble_ = on;   // beteende — nästa orbit, ingenting ritas om
}

void Canvas::setElevationRange(float minDeg, float maxDeg)
{
    if (!detail::validElevationLimit(minDeg) ||
        !detail::validElevationLimit(maxDeg)) {
        gfx::log("Canvas(%s)::setElevationRange: %g–%g ligger utanför ±180 grader "
                 "(ignoreras)",
                 name.c_str(), static_cast<double>(minDeg),
                 static_cast<double>(maxDeg));
        return;
    }
    minElevation_ = std::min(minDeg, maxDeg);
    maxElevation_ = std::max(minDeg, maxDeg);
}

void Canvas::setDistanceRange(double minDistance, double maxDistance)
{
    minDistance_ = std::isfinite(minDistance) && minDistance > 0.0 ? minDistance : 0.0;
    maxDistance_ = std::isfinite(maxDistance) && maxDistance > 0.0 ? maxDistance : 0.0;
    if (minDistance_ > 0.0 && maxDistance_ > 0.0 && maxDistance_ < minDistance_)
        std::swap(minDistance_, maxDistance_);
}

// ------------------------------------------------------------ tickningen
// Kontraktet står vid `onTick` i canvas.h.

void Canvas::setTicking(bool on)
{
    if (ticking_ == on)
        return;
    ticking_ = on;
    if (on)
        invalidate();   // nästa ritning postar den första ticken
}

bool Canvas::tickAlive()
{
    if (!onTick)
        return false;
    // En tangent som släppts — eller som fokusförlusten tömt ur fönstrets
    // mängd — håller inte längre tickningen vid liv.
    const AppWindow* w = appWindow();
    std::erase_if(tickKeys_,
                  [w](gfx::Key k) { return !w || !w->isKeyHeld(k); });
    return ticking_ || !tickKeys_.empty();
}

void Canvas::stepTick()
{
    if (!tickAlive()) {
        tickRunning_ = false;   // ingenting håller den vid liv — inga fler frames
        return;
    }
    if (!tickRunning_) {
        tickRunning_ = true;    // en ny följd: sekunderna räknas härifrån
        tickLast_    = gfx::ticksMs();
    }
    if (tickPosted_)
        return;
    // Ur RITNINGEN: ingen handler får köras mitt i en frame, så ticken går
    // till pump-svansen — en åt gången, och aldrig till en rityta som hunnit
    // dö (livstecknet; trögheten och snäppet gör likadant med sin
    // onCameraChange). I RAMKÖN: svansen kör den före appens
    // postade funktioner och utan deras budget, så ticken aldrig väntar
    // bakom en skur av trådarnas svar.
    tickPosted_                   = true;
    const std::weak_ptr<char> liv = life_;
    detail::postFrameWork([this, liv] {
        if (liv.expired())
            return;
        tickPosted_ = false;
        if (!tickAlive()) {
            tickRunning_ = false;   // släppt mellan framen och svansen — en ny
            return;                 // följd räknar sina sekunder från början
        }
        const std::uint64_t nu = gfx::ticksMs();
        TickArgs            e;
        e.seconds = detail::tickSeconds(tickLast_, nu);
        tickLast_ = nu;
        onTick(this, e);
        // Efter handlern ritas RITYTAN om, inte fönstret (
        // kontraktet vid `onTick` i canvas.h) — och den omritningen ger
        // nästa tick.
        invalidate();
    });
}

void Canvas::easeTo(const Camera& target, int ms)
{
    if (ms <= 0) {
        setCamera(target);
        return;
    }
    materialize();
    easeFrom_ = cam_;
    // Målet genom sättarens rensning — samma klampningar som setCamera.
    const Camera from = cam_;
    setCamera(target);
    easeTo_    = cam_;
    cam_       = from;
    easeStart_ = gfx::ticksMs();
    easeMs_    = ms;
    easing_    = true;
    easeUser_  = false;   // appens glidning — vridsnäppet sätter den själv
    invalidate();
}

void Canvas::stepEase()
{
    if (!easing_)
        return;
    const double t = static_cast<double>(gfx::ticksMs() - easeStart_) / easeMs_;
    if (easeUser_)
        postCameraChange();   // vridsnäppet är användarens rörelse
    if (t >= 1.0) {
        cam_    = easeTo_;
        easing_ = false;   // framme — ingen mer självinvalidering (regel 1)
        cameraLanded();
        return;
    }
    cam_ = detail::easeCamera(easeFrom_, easeTo_, t);
    invalidate();   // nästa frame tar nästa steg — Progress-mönstret
}

// TRÖGHETEN: den greppade världspunkten följer släppets fart, som klingar
// av (camera_math.cpp), och står still vid slutet — där ber ritytan inte om
// fler frames (regel 1).
void Canvas::stepCoast(const Ctx& c)
{
    if (!coasting_)
        return;
    const double t  = static_cast<double>(gfx::ticksMs() - coastStart_);
    const detail::InertiaPlan plan{coastVx_, coastVy_, coastMs_};
    double dx = 0.0, dy = 0.0;
    detail::inertiaOffset(plan, t, dx, dy);
    gfx::MapView v = view(c.box, c.scale);
    if (!gfx::anchorWorldAt(v, coastGrab_.x, coastGrab_.y, coastX_ + dx,
                            coastY_ + dy)) {
        coasting_ = false;   // punkten nådde horisonten — stanna där
        return;
    }
    cam_.center = Point{v.cx, v.cy};
    postCameraChange();
    if (t >= coastMs_) {
        coasting_ = false;
        cameraLanded();
        return;
    }
    invalidate();
}

void Canvas::zoomToRect(const Rect& r, Length pad)
{
    if (!(std::abs(r.w) > 0.0) && !(std::abs(r.h) > 0.0)) {
        gfx::log("Canvas(%s)::zoomToRect: tom rektangel (ignoreras)",
                 name.c_str());
        return;
    }
    stopMotion();
    gfx::Rectf box{};
    float      scale = 0.0f, pxPerMm = 0.0f;
    if (currentBox(box, scale, pxPerMm)) {
        materialize();
        cam_        = fitRect(cam_, r, pad, box, scale, pxPerMm);
        pendingFit_ = false;   // en äldre väntande inramning är ersatt
    } else {
        pendingFit_ = true;   // ingen yta nu — nästa ritning med yta
        fitRect_    = r;
        fitPad_     = pad;
    }
    invalidate();
}

Camera Canvas::fitRect(Camera cam, const Rect& r, Length pad,
                       const gfx::Rectf& box, float scale, float pxPerMm) const
{
    // Luften i LOGISKA px (mm ur skärmen); i världsenheter växer
    // rektangeln i stället.
    double padPx = 0.0, padWorld = 0.0;
    if (pad.unit == Unit::Mm)
        padPx = pad.value * pxPerMm / scale;
    else if (pad.unit == Unit::World)
        padWorld = pad.value;
    else
        padPx = pad.value;
    // Luften dras från båda sidor; i världen växer rektangeln åt båda håll.
    const double z = detail::fitZoom(
        std::abs(r.w) + 2.0 * padWorld, std::abs(r.h) + 2.0 * padWorld,
        cam.bearing, box.w / scale - 2.0 * padPx, box.h / scale - 2.0 * padPx);
    if (z > 0.0)
        cam.zoom = clampZoom(z);
    cam.center = Point{r.x + r.w / 2.0, r.y + r.h / 2.0};
    return cam;
}

bool Canvas::windowScale(float& scale, float& pxPerMm) const
{
    AppWindow* w = appWindow();
    if (!w || !w->isOpen())
        return false;
    scale   = lastScale_ > 0.0f ? lastScale_ : w->window().uiScale();
    pxPerMm = w->display().pxPerMm;
    return scale > 0.0f;
}

void Canvas::setMapScale(double denominator, double metersPerUnit)
{
    if (!std::isfinite(denominator) || !(denominator > 0.0) ||
        !std::isfinite(metersPerUnit) || !(metersPerUnit > 0.0)) {
        gfx::log("Canvas(%s)::setMapScale: skalan och enheten måste vara > 0 "
                 "(ignoreras)", name.c_str());
        return;
    }
    stopMotion();
    scaleDenominator_ = denominator;
    metersPerUnit_    = metersPerUnit;
    float scale = 0.0f, pxPerMm = 0.0f;
    if (windowScale(scale, pxPerMm)) {
        materialize();
        cam_.zoom     = mapScaleZoom(scale, pxPerMm);
        pendingScale_ = false;
    } else {
        pendingScale_ = true;   // ingen skärm än — nästa ritning
    }
    invalidate();
}

double Canvas::mapScale() const
{
    float scale = 0.0f, pxPerMm = 0.0f;
    if (!windowScale(scale, pxPerMm)) {
        scale   = lastScale_ > 0.0f ? lastScale_ : 1.0f;
        pxPerMm = display().pxPerMm;
    }
    const double z = zoom();
    return z > 0.0 ? metersPerUnit_ * 1000.0 * pxPerMm / (z * scale) : 0.0;
}

bool Canvas::mapScaleIsExact() const
{
    return display().source != gfx::DisplaySource::Assumed;
}

double Canvas::mapScaleZoom(float scale, float pxPerMm) const
{
    // 1 världsenhet = mpu m i terrängen = mpu · 1000 / N mm på skärmen.
    return clampZoom(metersPerUnit_ * 1000.0 / scaleDenominator_ * pxPerMm /
                     scale);
}

// DE VÄNTANDE BEGÄRANDENA i rutan, på KOPIOR och i verkställandets ordning:
// kartskalan, inramningen, 3D-inramningen och 3D-panoreringen — de två sista
// bara om 3D-kameran fortfarande gäller. Utan väntande är svaret
// de kameror som gäller.
void Canvas::pendingCameras(const gfx::Rectf& box, float scale, float pxPerMm,
                            Camera& cam, Camera3D& cam3) const
{
    cam  = cameraFor(box, scale);   // den satta, eller den `materialize` fryser
    cam3 = cam3D_;
    if (pendingScale_)
        cam.zoom = mapScaleZoom(scale, pxPerMm);
    if (pendingFit_)
        cam = fitRect(cam, fitRect_, fitPad_, box, scale, pxPerMm);
    if (pendingBox_ && is3D_)
        cam3 = fitBox3D(cam3, fitBox_, boxPad_, box, scale, pxPerMm);
    if (pendingPan_ && is3D_) {
        const double cx = box.x + box.w / 2.0, cy = box.y + box.h / 2.0;
        detail::panCamera3D(cam3, box, cx, cy, cx + panDelta_.x * scale,
                            cy + panDelta_.y * scale,
                            detail::PanVertical::Plane);   // panBy:s väntande
    }
}

void Canvas::applyPending(const Ctx& c)
{
    if (!pendingScale_ && !pendingFit_ && !pendingBox_ && !pendingPan_)
        return;
    Camera   cam;
    Camera3D cam3;
    pendingCameras(c.box, c.scale, c.pxPerMm, cam, cam3);
    if (pendingScale_ || pendingFit_) {
        cam_       = cam;   // en satt kamera från och med nu
        hasCamera_ = true;
    }
    if ((pendingBox_ || pendingPan_) && is3D_) {
        if (pendingPan_)
            stopMotion();
        cam3D_ = cam3;
        invalidate();   // 3D-målets stämpel — ritningen ser den nya kameran
    }
    pendingScale_ = pendingFit_ = pendingBox_ = pendingPan_ = false;
    panDelta_     = Point{};
}

std::optional<Point> Canvas::toWorld(Point local) const
{
    gfx::Rectf box{};
    float      scale = 0.0f;
    if (!knownBox(box, scale))
        return std::nullopt;
    double       wx = 0.0, wy = 0.0;
    const double px = box.x + local.x * scale, py = box.y + local.y * scale;
    // Med 3D-kameran: marken (z = 0) under pixeln.
    const bool ok = is3D_ ? projection(box, scale).ground(px, py, wx, wy)
                          : gfx::unproject(view(box, scale), px, py, wx, wy);
    if (!ok)
        return std::nullopt;
    return Point{wx, wy};
}

bool Canvas::projectIn(const gfx::Rectf& box, float scale, Point world,
                       double& px, double& py) const
{
    return is3D_ ? gfx::project(view3DFor(box),
                                gfx::Vec3d{world.x, world.y, 0.0}, px, py)
                 : gfx::project(view(box, scale), world.x, world.y, px, py);
}

std::optional<Point> Canvas::toLocal(Point world) const
{
    gfx::Rectf box{};
    float      scale = 0.0f;
    double     px = 0.0, py = 0.0;
    if (!knownBox(box, scale) || !projectIn(box, scale, world, px, py))
        return std::nullopt;
    return Point{(px - box.x) / scale, (py - box.y) / scale};
}

// ------------------------------------------------------ de förankrade barnen
// Ritytan placerar BARA de förankrade (`placesChild`); de andra
// barnen går dock/anchors-vägen. Platsen räknas tillståndslöst ur kameran som
// den står när barnet resolvas — i ritningen EFTER ritytans `draw()`, som
// stegar trögheten och glidningarna, så barnet och scenen delar bildruta.

const Canvas::ChildAnchor* Canvas::anchorOf(const Widget& child) const
{
    for (const Anchored& a : anchored_)
        if (a.child == &child && !a.life.expired())
            return &a.at;
    return nullptr;
}

void Canvas::setChildAnchor(Widget& child, Point world, TextAnchor anchor)
{
    if (!std::isfinite(world.x) || !std::isfinite(world.y)) {
        gfx::log("Canvas(%s)::setChildAnchor(%s): icke-ändlig punkt "
                 "(ignoreras)",
                 name.c_str(), child.name.c_str());
        return;
    }
    // Döda barns poster städas här — de läses aldrig (livstecknet).
    std::erase_if(anchored_, [](const Anchored& a) { return a.life.expired(); });
    auto it = std::find_if(anchored_.begin(), anchored_.end(),
                           [&](const Anchored& a) { return a.child == &child; });
    if (it == anchored_.end())
        it = anchored_.insert(anchored_.end(),
                              Anchored{&child, child.lifeToken(), {}});
    it->at = ChildAnchor{world, anchor};
    // Barnets gamla och nya yta — scenen under är oförändrad (3D-målets
    // stämpel står, kompositen ritas om i barnets damage).
    child.invalidate();
}

void Canvas::clearChildAnchor(Widget& child)
{
    const auto n = std::erase_if(anchored_, [&](const Anchored& a) {
        return a.child == &child || a.life.expired();
    });
    if (n > 0)
        child.invalidate();
}

std::optional<Canvas::ChildAnchor> Canvas::childAnchor(const Widget& child) const
{
    if (const ChildAnchor* a = anchorOf(child))
        return *a;
    return std::nullopt;
}

bool Canvas::placesChild(const Widget& child) const
{
    return anchorOf(child) != nullptr;
}

// `c.area` är ritytans childArea — dess egen yta, fysisk, samma ruta som
// `draw()` fick — och barnets mått är dess mätta, logiska (`measure`). Hörnet
// ur ankaret är etiketternas regel (`gfx::anchorCorner`).
gfx::Rectf Canvas::placeChild(const Widget& child, const Ctx& c) const
{
    const ChildAnchor* a  = anchorOf(child);
    double             px = 0.0, py = 0.0;
    if (!a || !projectIn(c.area, c.scale, a->world, px, py))
        return gfx::Rectf{c.area.x, c.area.y, 0.0f, 0.0f};
    const gfx::Sizef m = measure(child, c);
    const gfx::Sizef sz{m.w * c.scale, m.h * c.scale};
    float            ox = 0.0f, oy = 0.0f;
    gfx::anchorCorner(a->anchor, sz.w, sz.h, ox, oy);
    return gfx::Rectf{static_cast<float>(px) + ox, static_cast<float>(py) + oy,
                      sz.w, sz.h};
}

std::optional<double> Canvas::unitsPerPixelAt(Point local) const
{
    // Fasadens skala vid pixeln (fysiska px per världsenhet längs bildraden,
    // räknad relativt kamerans mitt — SWEREF tappar inga siffror), med samma
    // vy som toWorld; 0 betyder ovanför horisonten.
    gfx::Rectf box{};
    float      scale = 0.0f;
    if (!knownBox(box, scale))
        return std::nullopt;
    const double px = box.x + local.x * scale, py = box.y + local.y * scale;
    // Med 3D-kameran: skalan vid markpunkten under pixeln.
    const double ppu = is3D_ ? projection(box, scale).groundScale(px, py)
                             : gfx::pxPerUnitAt(view(box, scale), px, py);
    if (!std::isfinite(ppu) || !(ppu > 0.0))
        return std::nullopt;
    return scale / ppu;   // logiska px per enhet = ppu / scale
}

gfx::DisplayMetrics Canvas::display() const
{
    if (const AppWindow* w = appWindow())
        return w->display();
    return gfx::DisplayMetrics{};
}

// ----------------------------------------------------------------- 3D-matten
// Huvudvyns kamera som den ritas — 3D-kamerans, eller
// kartkamerans `toView3D` — i den senast kända ytan; fasadens matte
// (gfx/camera.h) gör räkningen.

gfx::View3D Canvas::mainView3DFor(const gfx::Rectf& box, float scale) const
{
    return detail::mainView3D(is3D_ ? &cam3D_ : nullptr, view(box, scale), *scene_);
}

std::optional<Ray3> Canvas::rayAt(Point local) const
{
    gfx::Rectf box{};
    float      scale = 0.0f;
    gfx::Ray3d r;
    if (!knownBox(box, scale) ||
        !gfx::rayThrough(mainView3DFor(box, scale), box.x + local.x * scale,
                         box.y + local.y * scale, r))
        return std::nullopt;
    return Ray3{{r.origin.x, r.origin.y, r.origin.z}, {r.dir.x, r.dir.y, r.dir.z}};
}

std::optional<Point> Canvas::project(Point3 world) const
{
    gfx::Rectf box{};
    float      scale = 0.0f;
    double     px = 0.0, py = 0.0;
    if (!knownBox(box, scale) ||
        !gfx::project(mainView3DFor(box, scale), gfx::Vec3d{world.x, world.y, world.z},
                      px, py))
        return std::nullopt;
    return Point{(px - box.x) / scale, (py - box.y) / scale};
}

std::optional<Point3> Canvas::toWorld(Point local, const Plane3& plane) const
{
    const std::optional<Ray3> r = rayAt(local);
    if (!r)
        return std::nullopt;
    const std::optional<detail::PlaneHit> h = detail::rayPlane(*r, plane);
    if (!h)
        return std::nullopt;
    return h->point;
}

std::optional<double> Canvas::worldPerPixel(Point3 world) const
{
    gfx::Rectf box{};
    float      scale = 0.0f;
    if (!knownBox(box, scale))
        return std::nullopt;
    const double ppu = gfx::pxPerUnitAt(mainView3DFor(box, scale),
                                        gfx::Vec3d{world.x, world.y, world.z});
    if (!std::isfinite(ppu) || !(ppu > 0.0))
        return std::nullopt;
    return scale / ppu;   // per LOGISK px — fysiska px per enhet / skalan
}

std::vector<Point3> Canvas::visibleRegion(const Plane3& plane, double maxDistance) const
{
    std::vector<Point3> out;
    gfx::Rectf          box{};
    float               scale = 0.0f;
    if (!knownBox(box, scale))
        return out;
    const gfx::Plane3d p{{plane.point.x, plane.point.y, plane.point.z},
                         {plane.normal.x, plane.normal.y, plane.normal.z}};
    for (const gfx::Vec3d& v :
         gfx::visiblePlaneRegion(mainView3DFor(box, scale), p, maxDistance))
        out.push_back(Point3{v.x, v.y, v.z});
    return out;
}

// -------------------------------------------------------------------- lagren

bool Canvas::layerOk(int layer, const char* op) const
{
    if (layer >= 0 && static_cast<std::size_t>(layer) < scene_->layers.size())
        return true;
    gfx::log("Canvas(%s)::%s: lager %d finns inte (%zu lager) — ignoreras",
             name.c_str(), op, layer, scene_->layers.size());
    return false;
}

int Canvas::addLayer(TrText name)
{
    auto L  = std::make_shared<detail::LayerData>();
    L->name = std::move(name);
    scene_->layers.push_back(std::move(L));
    ++scene_->layerSetVersion;   // lagrens mängd (src/canvas/scene.h)
    return static_cast<int>(scene_->layers.size()) - 1;
}

void Canvas::setLayer(int layer, TrText name)
{
    if (!layerOk(layer, "setLayer"))
        return;
    auto L  = std::make_shared<detail::LayerData>();
    L->name = std::move(name);
    scene_->layers[static_cast<std::size_t>(layer)] = std::move(L);
    invalidate();
}

void Canvas::setLayers(std::vector<TrText> names)
{
    scene_->layers.clear();
    for (TrText& n : names) {
        auto L  = std::make_shared<detail::LayerData>();
        L->name = std::move(n);
        scene_->layers.push_back(std::move(L));
    }
    ++scene_->layerSetVersion;
    invalidate();
}

void Canvas::removeLayer(int layer)
{
    if (!layerOk(layer, "removeLayer"))
        return;
    scene_->layers.erase(scene_->layers.begin() + layer);
    ++scene_->layerSetVersion;
    invalidate();
}

// LAGRENS ORDNING — bara vektorn roteras: lagren behåller sina
// körningar och chunkar, och fönstrets GPU-kopia är nycklad på deras id och
// inte på platsen (scene.h). Det som beror på platsen räknas om i nästa
// ritning — lagrens placering i `LayerFrame`, etiketternas källor och
// draperingarnas nyckel — så inget lager märks för ombyggnad.
bool Canvas::moveLayer(int from, int to)
{
    const int n = layerCount();
    if (from < 0 || from >= n || to < 0 || to > n) {
        gfx::log("Canvas(%s)::moveLayer: lager %d eller plats %d finns inte "
                 "(%d lager, platserna 0–%d) — ignoreras",
                 name.c_str(), from, to, n, n);
        return false;
    }
    if (to == from || to == from + 1)
        return true;   // samma plats — ingenting ritas om (regel 1)
    auto& v = scene_->layers;
    if (to > from)
        std::rotate(v.begin() + from, v.begin() + from + 1, v.begin() + to);
    else
        std::rotate(v.begin() + to, v.begin() + from, v.begin() + from + 1);
    ++scene_->layerSetVersion;
    invalidate();
    return true;
}

int Canvas::layerCount() const
{
    return static_cast<int>(scene_->layers.size());
}

const std::string& Canvas::layerName(int layer) const
{
    static const std::string tom;
    if (layer < 0 || static_cast<std::size_t>(layer) >= scene_->layers.size())
        return tom;
    return scene_->layers[static_cast<std::size_t>(layer)]->name.str();
}

void Canvas::setLayerVisible(int layer, bool visible)
{
    if (!layerOk(layer, "setLayerVisible"))
        return;
    scene_->layers[static_cast<std::size_t>(layer)]->visible = visible;
    ++scene_->layerSetVersion;
    invalidate();
}

bool Canvas::layerVisible(int layer) const
{
    return layer >= 0 && static_cast<std::size_t>(layer) < scene_->layers.size() &&
           scene_->layers[static_cast<std::size_t>(layer)]->visible;
}

void Canvas::setLayerOpacity(int layer, float opacity)
{
    if (!layerOk(layer, "setLayerOpacity"))
        return;
    scene_->layers[static_cast<std::size_t>(layer)]->opacity =
        std::isfinite(opacity) ? std::clamp(opacity, 0.0f, 1.0f) : 1.0f;
    invalidate();
}

float Canvas::layerOpacity(int layer) const
{
    if (layer < 0 || static_cast<std::size_t>(layer) >= scene_->layers.size())
        return 0.0f;
    return scene_->layers[static_cast<std::size_t>(layer)]->opacity;
}

void Canvas::setLayerZoomRange(int layer, double minZoom, double maxZoom)
{
    if (!layerOk(layer, "setLayerZoomRange"))
        return;
    detail::LayerData& L = *scene_->layers[static_cast<std::size_t>(layer)];
    L.minZoom = std::isfinite(minZoom) && minZoom > 0.0 ? minZoom : 0.0;
    L.maxZoom = std::isfinite(maxZoom) && maxZoom > 0.0 ? maxZoom : 0.0;
    invalidate();
}

void Canvas::setLayerCollision(int layer, bool on)
{
    if (!layerOk(layer, "setLayerCollision"))
        return;
    scene_->layers[static_cast<std::size_t>(layer)]->collision = on;
    invalidate();   // placeringen görs om vid nästa ritning
}

void Canvas::setLabelFade(bool on)
{
    if (labelFade_ == on)
        return;
    labelFade_ = on;
    invalidate();   // en pågående toning landar på målet direkt
}

bool Canvas::layerCollision(int layer) const
{
    return layer >= 0 && static_cast<std::size_t>(layer) < scene_->layers.size() &&
           scene_->layers[static_cast<std::size_t>(layer)]->collision;
}

// HÖRNEN OCH KANTERNA — en flagga på lagret, så den följer
// `moveLayer`; bara träffprovet läser den, så ingenting ritas om (regel 1).
void Canvas::setLayerSnap(int layer, bool on)
{
    if (!layerOk(layer, "setLayerSnap"))
        return;
    scene_->layers[static_cast<std::size_t>(layer)]->snap = on;
}

bool Canvas::layerSnap(int layer) const
{
    return layer >= 0 && static_cast<std::size_t>(layer) < scene_->layers.size() &&
           scene_->layers[static_cast<std::size_t>(layer)]->snap;
}

void Canvas::setLayerDrawOrder(int layer, DrawOrder order)
{
    if (!layerOk(layer, "setLayerDrawOrder"))
        return;
    detail::LayerData& L = *scene_->layers[static_cast<std::size_t>(layer)];
    if (L.order == order)
        return;
    L.order = order;
    L.dirty = true;   // körningarna formas om
    invalidate();
}

DrawOrder Canvas::layerDrawOrder(int layer) const
{
    if (layer < 0 || static_cast<std::size_t>(layer) >= scene_->layers.size())
        return DrawOrder::Strict;
    return scene_->layers[static_cast<std::size_t>(layer)]->order;
}

void Canvas::setLayerPlacement(int layer, LayerPlacement placement)
{
    if (!layerOk(layer, "setLayerPlacement"))
        return;
    if (static_cast<unsigned>(placement) >
        static_cast<unsigned>(LayerPlacement::Terrain)) {
        gfx::log("Canvas(%s)::setLayerPlacement: placering %u finns inte "
                 "(ignoreras)",
                 name.c_str(), static_cast<unsigned>(placement));
        return;
    }
    scene_->layers[static_cast<std::size_t>(layer)]->placement = placement;
    ++scene_->layerSetVersion;
    invalidate();
}

LayerPlacement Canvas::layerPlacement(int layer) const
{
    if (layer < 0 || static_cast<std::size_t>(layer) >= scene_->layers.size())
        return LayerPlacement::Auto;
    return scene_->layers[static_cast<std::size_t>(layer)]->placement;
}

void Canvas::setLayerRenderState(int layer, const RenderState& state)
{
    if (!layerOk(layer, "setLayerRenderState"))
        return;
    // Fasadens regler (scene.h `validRenderState`) — annars hade varje
    // rit-anrop i lagret vägrats.
    if (!detail::validRenderState(state)) {
        gfx::log("Canvas(%s)::setLayerRenderState: ogiltigt renderläge — en "
                 "uppräkning utanför sina värden eller en bias som inte är "
                 "ändlig (ignoreras)",
                 name.c_str());
        return;
    }
    scene_->layers[static_cast<std::size_t>(layer)]->state = state;
    invalidate();
}

RenderState Canvas::layerRenderState(int layer) const
{
    if (layer < 0 || static_cast<std::size_t>(layer) >= scene_->layers.size())
        return RenderState{};
    return scene_->layers[static_cast<std::size_t>(layer)]->state;
}

void Canvas::setLayerViews(int layer, std::vector<int> views)
{
    if (!layerOk(layer, "setLayerViews"))
        return;
    for (const int v : views)
        if (v < detail::kMainView || v > viewCount()) {
            gfx::log("Canvas(%s)::setLayerViews: vy %d finns inte (huvudvyn 0 "
                     "och %d del-vyer) — ignoreras",
                     name.c_str(), v, viewCount());
            return;
        }
    std::sort(views.begin(), views.end());
    views.erase(std::unique(views.begin(), views.end()), views.end());
    scene_->layers[static_cast<std::size_t>(layer)]->views = std::move(views);
    ++scene_->layerSetVersion;
    invalidate();
}

std::vector<int> Canvas::layerViews(int layer) const
{
    if (layer < 0 || static_cast<std::size_t>(layer) >= scene_->layers.size())
        return {};
    return scene_->layers[static_cast<std::size_t>(layer)]->views;
}

// MÅLARENS PLATS — en flagga på lagret, så den följer `moveLayer`;
// ritningen frågar den (scene.cpp `paintLayer`).
void Canvas::setLayerPainter(int layer, bool on)
{
    if (!layerOk(layer, "setLayerPainter"))
        return;
    scene_->layers[static_cast<std::size_t>(layer)]->painter = on;
    invalidate();
}

bool Canvas::layerPainter(int layer) const
{
    return layer >= 0 && static_cast<std::size_t>(layer) < scene_->layers.size() &&
           scene_->layers[static_cast<std::size_t>(layer)]->painter;
}

// ------------------------------------------------------------------ formerna

int Canvas::addShape(Shape s)
{
    return addShape(0, std::move(s));
}

int Canvas::addShape(int layer, Shape s)
{
    if (!layerOk(layer, "addShape"))
        return -1;
    detail::LayerData& L = *scene_->layers[static_cast<std::size_t>(layer)];
    detail::appendShape(L, std::move(s), yUp_);
    invalidate();
    return static_cast<int>(L.slots.size()) - 1;
}

void Canvas::setShape(int index, Shape s)
{
    setShape(0, index, std::move(s));
}

void Canvas::setShape(int layer, int index, Shape s)
{
    if (!layerOk(layer, "setShape"))
        return;
    detail::LayerData& L = *scene_->layers[static_cast<std::size_t>(layer)];
    if (index < 0 || static_cast<std::size_t>(index) >= L.slots.size()) {
        gfx::log("Canvas(%s)::setShape: form %d finns inte i lager %d (%zu "
                 "former) — ignoreras", name.c_str(), index, layer,
                 L.slots.size());
        return;
    }
    detail::replaceShapeAt(L, static_cast<std::size_t>(index), std::move(s), yUp_);
    invalidate();
}

void Canvas::setShapes(std::vector<Shape> shapes)
{
    setShapes(0, std::move(shapes));
}

void Canvas::setShapes(int layer, std::vector<Shape> shapes)
{
    if (!layerOk(layer, "setShapes"))
        return;
    detail::assignShapes(*scene_->layers[static_cast<std::size_t>(layer)],
                         std::move(shapes));
    invalidate();
}

void Canvas::removeShape(int index)
{
    removeShape(0, index);
}

void Canvas::removeShape(int layer, int index)
{
    if (!layerOk(layer, "removeShape"))
        return;
    detail::LayerData& L = *scene_->layers[static_cast<std::size_t>(layer)];
    if (index < 0 || static_cast<std::size_t>(index) >= L.slots.size()) {
        gfx::log("Canvas(%s)::removeShape: form %d finns inte i lager %d (%zu "
                 "former) — ignoreras", name.c_str(), index, layer,
                 L.slots.size());
        return;
    }
    detail::removeShapeAt(L, static_cast<std::size_t>(index));
    invalidate();
}

int Canvas::shapeCount(int layer) const
{
    if (layer < 0 || static_cast<std::size_t>(layer) >= scene_->layers.size())
        return 0;
    return static_cast<int>(scene_->layers[static_cast<std::size_t>(layer)]->slots.size());
}

const Shape* Canvas::shape(int layer, int index) const
{
    if (layer < 0 || static_cast<std::size_t>(layer) >= scene_->layers.size())
        return nullptr;
    const detail::LayerData& L = *scene_->layers[static_cast<std::size_t>(layer)];
    if (index < 0 || static_cast<std::size_t>(index) >= L.slots.size())
        return nullptr;
    return &L.slots[static_cast<std::size_t>(index)].entry->shape;
}

// ------------------------------------------------------------- textens mått
//
// Målarens mätning (../canvas/build.h `measureLabel` — inget laddas
// upp) utanför en ritning: fönstrets atlas och tätheter, och kameran som
// ritytan senast ritades med (eller räknas fram nu) — exakt de tal en
// etikett byggs med. Utan öppet fönster finns ingen font att mäta med
// (programError-mönstret).
//
// TVÅ FORMER, en mätning (`measureWith`). Formen UTAN Ctx frågar
// layouten — `behaviorCtx` går genom förfädernas `childArea` (en Panel
// resolvar då varje barn) och `resolve` frågar `heightForWidth` — så anropad
// ur ritytans egen layout hade den gått runt i sig själv. VAKTEN bryter
// varvet med en loggrad som pekar på formen MED Ctx, som aldrig frågar
// layouten: tätheten ur `layoutPxPerUnit`.

std::optional<TextMetrics> Canvas::measureText(const TrText& text,
                                               const TextStyle& style,
                                               Unit unit) const
{
    // En del med osatta fält är exakt textens enda del (build.h `textParts`).
    return measureText(std::vector<TextSpan>{TextSpan{text}}, style, unit);
}

std::optional<TextMetrics> Canvas::measureText(const std::vector<TextSpan>& spans,
                                               const TextStyle& style,
                                               Unit unit) const
{
    if (measuring_) {
        if (!measureLoopLogged_) {
            measureLoopLogged_ = true;
            gfx::log("Canvas(%s)::measureText: anropad ur ritytans egen layout "
                     "— mät med measureText(c, …) i heightForWidth/contentSize "
                     "(nullopt)",
                     name.c_str());
        }
        return std::nullopt;
    }
    const AppWindow* win = appWindow();
    if (!win || !win->isOpen())
        return std::nullopt;
    // Vakten gäller hela layoutvandringen nedan, också en tidig retur.
    measuring_ = true;
    struct Släpp {
        bool& flagga;
        ~Släpp() { flagga = false; }
    } släpp{measuring_};
    const Ctx c = behaviorCtx();
    if (!c.canvas)
        return std::nullopt;
    // Den AKTUELLA ytan med Ctx:ts egen skala — en sammanhängande ruta och
    // skala, den nästa ritning får — och kamerorna med de väntande
    // begärandena inräknade (en `setMapScale` före öppningen gäller redan),
    // räknade på kopior: frågan ändrar ingenting.
    const gfx::Rectf box = resolve(c);
    Camera           cam;
    Camera3D         cam3;
    pendingCameras(box, c.scale, c.pxPerMm, cam, cam3);
    return measureWith(c, worldView(cam, cam3, box, c.scale).pxPerUnit, spans,
                       style, unit);
}

std::optional<TextMetrics> Canvas::measureText(const Ctx& c, const TrText& text,
                                               const TextStyle& style,
                                               Unit unit) const
{
    return measureText(c, std::vector<TextSpan>{TextSpan{text}}, style, unit);
}

std::optional<TextMetrics> Canvas::measureText(const Ctx& c,
                                               const std::vector<TextSpan>& spans,
                                               const TextStyle& style,
                                               Unit unit) const
{
    return measureWith(c, layoutPxPerUnit(c), spans, style, unit);
}

double Canvas::layoutPxPerUnit(const Ctx& c) const
{
    // Ytan: den senast ritade, annars de satta måtten — aldrig `knownBox`,
    // `resolve` eller `contentSize` (en subklass kan mäta i den): layouten
    // håller på att räkna fram den yta en resolve hade gett.
    const gfx::Rectf box = lastScale_ > 0.0f
                               ? lastBox_
                               : gfx::Rectf{0.0f, 0.0f, width * c.scale,
                                            height * c.scale};
    // 3D-kamerans skala beror på ytan; kartkamerans gör det inte.
    if (is3D_ && !(box.w > 0.0f && box.h > 0.0f))
        return 0.0;
    // Kameran som den är, med en väntande kartskala (den behöver bara skalan
    // och skärmens mm) — en väntande inramning kräver ytan och räknas inte.
    Camera cam = cameraFor(box, c.scale);
    if (pendingScale_)
        cam.zoom = mapScaleZoom(c.scale, c.pxPerMm);
    return worldView(cam, cam3D_, box, c.scale).pxPerUnit;
}

std::optional<TextMetrics> Canvas::measureWith(const Ctx& c, double pxPerUnit,
                                               const std::vector<TextSpan>& spans,
                                               const TextStyle& style,
                                               Unit unit) const
{
    if (!c.canvas)
        return std::nullopt;   // ett Ctx utan fönstrets resurser
    // Utan världens täthet hade `toUnit` gett 0 för varje världsmått — ett
    // tyst fel; ett mått som inte behöver den mäts ändå.
    const bool känd = std::isfinite(pxPerUnit) && pxPerUnit > 0.0;
    if (!känd && detail::measureNeedsWorld(style, spans, unit))
        return std::nullopt;
    const detail::GlyphUnits u{style.size.unit, c.scale, c.pxPerMm,
                               känd ? static_cast<float>(pxPerUnit) : 0.0f};
    return detail::measureLabel(*c.canvas->atlas(style.family),
                                detail::textParts(spans, style, u), style, u,
                                unit);
}

// ---------------------------------------------------------------- del-vyerna
//
// VYNS NUMMER är 1, 2 … (canvas.h) — vy 0 är huvudvyn, ritytan själv. Ett
// nummer utanför och en ogiltig vy (target3d.h `validView`) loggas, och
// ingenting ändras.

namespace {

bool viewNumberOk(int view, int count, const std::string& name, const char* op)
{
    if (view > detail::kMainView && view <= count)
        return true;
    if (view == detail::kMainView)
        gfx::log("Canvas(%s)::%s: vy 0 är huvudvyn — ritytan själv, ingen View "
                 "(ignoreras)",
                 name.c_str(), op);
    else
        gfx::log("Canvas(%s)::%s: vy %d finns inte (%d del-vyer) — ignoreras",
                 name.c_str(), op, view, count);
    return false;
}

bool viewValid(const View& v, const std::string& name, const char* op)
{
    if (detail::validView(v))
        return true;
    gfx::log("Canvas(%s)::%s: ogiltig vy — ett ankare utanför de nio, mått som "
             "är negativa eller inte ändliga, eller en ogiltig kamera (ignoreras)",
             name.c_str(), op);
    return false;
}

} // namespace

int Canvas::addView(View v)
{
    if (!viewValid(v, name, "addView"))
        return -1;
    views_.push_back(std::move(v));
    invalidate();
    return viewCount();
}

void Canvas::setView(int view, View v)
{
    if (!viewNumberOk(view, viewCount(), name, "setView") ||
        !viewValid(v, name, "setView"))
        return;
    views_[static_cast<std::size_t>(view - 1)] = std::move(v);
    invalidate();
}

void Canvas::setViews(std::vector<View> views)
{
    for (const View& v : views)
        if (!viewValid(v, name, "setViews"))
            return;   // atomiskt — ingen vy sätts
    views_ = std::move(views);
    // Nummer som inte längre finns lämnar lagrens vylistor.
    const int n = viewCount();
    for (const std::shared_ptr<detail::LayerData>& L : scene_->layers)
        std::erase_if(L->views, [n](int v) { return v > n; });
    invalidate();
}

void Canvas::removeView(int view)
{
    if (!viewNumberOk(view, viewCount(), name, "removeView"))
        return;
    views_.erase(views_.begin() + (view - 1));
    // Lagren lämnar den borttagna vyn och följer de senare ett nummer ned.
    for (const std::shared_ptr<detail::LayerData>& L : scene_->layers) {
        std::erase(L->views, view);
        for (int& v : L->views)
            if (v > view)
                --v;
    }
    invalidate();
}

const View* Canvas::view(int view) const
{
    if (view <= detail::kMainView || view > viewCount())
        return nullptr;
    return &views_[static_cast<std::size_t>(view - 1)];
}

// ------------------------------------------------------------ egna program
//
// Ett avslag loggas och ändrar ingenting
// (kollektionsvokabulären); namnen är unika, för stilarna och passen nämner
// programmet vid namn. Varje ändring ger en ny version (scene.h
// `ProgramSlot`), och fönstrets kompilerade kopia av den gamla släpps.

namespace {

// Ett program ritytan tar emot: ett namn och tal inom fasadens tak — så att
// ett fel i beskrivningen syns här och inte först som ett ogiltigt program.
bool programValid(const Program& p, const std::string& name, const char* op)
{
    if (p.name.empty()) {
        gfx::log("Canvas(%s)::%s: programmet saknar namn — stilarna och passen "
                 "nämner det vid namn (ignoreras)",
                 name.c_str(), op);
        return false;
    }
    const int maxFloats = static_cast<int>(gfx::kMaxProgramUserBytes / sizeof(float));
    if (p.textures < 0 || p.textures > gfx::kMaxProgramTextures ||
        p.uniformFloats < 0 || p.uniformFloats > maxFloats) {
        gfx::log("Canvas(%s)::%s: programmet '%s' har textures %d och "
                 "uniformFloats %d — tillåtet är 0–%d och 0–%d (ignoreras)",
                 name.c_str(), op, p.name.c_str(), p.textures, p.uniformFloats,
                 gfx::kMaxProgramTextures, maxFloats);
        return false;
    }
    return true;
}

// Finns namnet redan på ett ANNAT index än `except`?
bool programNameTaken(const std::vector<detail::ProgramSlot>& programs,
                      const std::string& programName, int except)
{
    for (std::size_t i = 0; i < programs.size(); ++i)
        if (static_cast<int>(i) != except && programs[i].program.name == programName)
            return true;
    return false;
}

} // namespace

int Canvas::addProgram(Program p)
{
    if (!programValid(p, name, "addProgram"))
        return -1;
    if (programNameTaken(scene_->programs, p.name, -1)) {
        gfx::log("Canvas(%s)::addProgram: programmet '%s' finns redan — namnen "
                 "är unika (ignoreras; setProgram ersätter)",
                 name.c_str(), p.name.c_str());
        return -1;
    }
    scene_->programs.push_back(detail::ProgramSlot{std::move(p)});
    invalidate();
    return programCount() - 1;
}

void Canvas::setProgram(int index, Program p)
{
    if (index < 0 || index >= programCount()) {
        gfx::log("Canvas(%s)::setProgram: program %d finns inte (%d program) — "
                 "ignoreras",
                 name.c_str(), index, programCount());
        return;
    }
    if (!programValid(p, name, "setProgram"))
        return;
    if (programNameTaken(scene_->programs, p.name, index)) {
        gfx::log("Canvas(%s)::setProgram: programmet '%s' finns redan på ett "
                 "annat index — namnen är unika (ignoreras)",
                 name.c_str(), p.name.c_str());
        return;
    }
    scene_->programs[static_cast<std::size_t>(index)] =
        detail::ProgramSlot{std::move(p)};
    invalidate();
}

void Canvas::setPrograms(std::vector<Program> programs)
{
    for (std::size_t i = 0; i < programs.size(); ++i) {
        if (!programValid(programs[i], name, "setPrograms"))
            return;   // atomiskt — inget program sätts
        for (std::size_t j = 0; j < i; ++j)
            if (programs[j].name == programs[i].name) {
                gfx::log("Canvas(%s)::setPrograms: programmet '%s' står två "
                         "gånger — namnen är unika (ignoreras)",
                         name.c_str(), programs[i].name.c_str());
                return;
            }
    }
    scene_->programs.clear();
    for (Program& p : programs)
        scene_->programs.push_back(detail::ProgramSlot{std::move(p)});
    invalidate();
}

void Canvas::removeProgram(int index)
{
    if (index < 0 || index >= programCount()) {
        gfx::log("Canvas(%s)::removeProgram: program %d finns inte (%d program) "
                 "— ignoreras",
                 name.c_str(), index, programCount());
        return;
    }
    scene_->programs.erase(scene_->programs.begin() + index);
    invalidate();
}

int Canvas::programCount() const
{
    return static_cast<int>(scene_->programs.size());
}

const Program* Canvas::program(int index) const
{
    if (index < 0 || index >= programCount())
        return nullptr;
    return &scene_->programs[static_cast<std::size_t>(index)].program;
}

std::string Canvas::programError(const std::string& programName) const
{
    const AppWindow* win = appWindow();
    if (!win || !win->isOpen())
        return {};   // renderaren är fönstrets — utan fönster inget att fråga
    const Ctx c = behaviorCtx();
    if (!c.canvas)
        return {};
    const detail::ProgramSlot* slot = detail::findProgram(*scene_, programName);
    if (!slot)
        return "programmet '" + programName +   // lang_gate:rad utvecklarens fel (canvas.h)
               "' finns inte på ritytan (Canvas::addProgram)";   // lang_gate:rad som ovan
    return c.canvas->programError(*slot);
}

int Canvas::viewUnder(const gfx::Rectf& box, float scale, double px,
                      double py) const
{
    return detail::viewAt(views_, box, scale, px, py);
}

detail::Projection Canvas::viewProjection(int vy, const gfx::Rectf& box,
                                          float scale) const
{
    if (vy <= detail::kMainView || vy > viewCount())
        return projection(box, scale);
    const View& v = views_[static_cast<std::size_t>(vy - 1)];
    // En vy som följer kartkamerans rotation vrids som huvudvyns 3D-vy —
    // den 3D-målet ritar med.
    const gfx::View3D huvud =
        v.followRotation && !is3D_
            ? detail::mainView3D(nullptr, view(box, scale), *scene_)
            : gfx::View3D{};
    const Camera3D cam = detail::viewCamera(v, is3D_ ? &cam3D_ : nullptr, &huvud);
    return detail::Projection(
        detail::viewView3D(cam, detail::viewRect(v, box, scale), *scene_, vy));
}

// --------------------------------------------------------------------- bilden

std::optional<Bitmap> Canvas::renderToBitmap(int w, int h, const Camera3D& camera)
{
    if (w <= 0 || h <= 0) {
        gfx::log("Canvas(%s)::renderToBitmap: bilden %d × %d har ingen yta — "
                 "ingen bild",
                 name.c_str(), w, h);
        return std::nullopt;
    }
    if (!detail::validCamera3D(camera)) {
        gfx::log("Canvas(%s)::renderToBitmap: ogiltig Camera3D (camera.h:s "
                 "regler) — ingen bild",
                 name.c_str());
        return std::nullopt;
    }
    const AppWindow* win = appWindow();
    if (!win || !win->isOpen()) {
        gfx::log("Canvas(%s)::renderToBitmap: ritytan sitter inte i ett öppet "
                 "fönster (renderaren är fönstrets) — ingen bild",
                 name.c_str());
        return std::nullopt;
    }
    const Ctx c = behaviorCtx();
    if (!c.canvas)
        return std::nullopt;   // fönstret är mitt i öppning eller stängning
    // Scenen som 3D-målet ritar den — men med bildens kamera och utan
    // toning (bilden är ett ögonblick).
    detail::Target3DArgs a = detail::canvasTarget3DArgs(*this, c);
    a.camera               = &camera;
    // Ritytans ritläge bär fönstrets resurser, ljuset och y-riktningen —
    // rutan är bildens (target3d.h).
    detail::PaintContext pc;
    paintContext(c, pc, gfx::Rectf{0.0f, 0.0f, static_cast<float>(w),
                                   static_cast<float>(h)});
    const detail::ImageAccount konto(c, scene_);
    return detail::renderBitmap3D(pc, scene_, a, w, h, background(c));
}

// ------------------------------------------------------------------ minnet

void Canvas::setMemoryBudget(std::size_t bytes)
{
    // LAGRAR — nästa ritning öppnar kontot med den (App:s prejudikat).
    scene_->memoryBudget = bytes;
}

std::size_t Canvas::memoryBudget() const
{
    return scene_->memoryBudget;
}

// ---------------------------------------------------------------- gesterna

void Canvas::setPannable(bool on)
{
    if (pannable_ == on)
        return;
    pannable_ = on;
    if (!on)
        coasting_ = false;   // trögheten slutar med förmågan
    reviewDrags();
}

void Canvas::setZoomable(bool on)
{
    zoomable_ = on;
    reviewDrags();
}

void Canvas::setRotatable(bool on)
{
    rotatable_ = on;
    reviewDrags();
}

void Canvas::setPitchable(bool on)
{
    pitchable_ = on;
    reviewDrags();
}

void Canvas::setInertia(bool on)
{
    inertia_ = on;
    if (!on)
        coasting_ = false;
}

// VRIDSNÄPPET är beteende och inte utseende: inget ritas om när det ändras —
// det gäller nästa vridande gest som släpps.
void Canvas::setRotationSnap(float stepDeg)
{
    if (!detail::validRotationSnap(stepDeg)) {
        gfx::log("Canvas(%s)::setRotationSnap: steget %g ligger utanför 0–360 "
                 "grader (ignoreras)", name.c_str(), static_cast<double>(stepDeg));
        return;
    }
    rotationSnap_ = stepDeg;
}

void Canvas::setRotationSnapTolerance(float deg)
{
    if (!detail::validRotationSnapTolerance(deg)) {
        gfx::log("Canvas(%s)::setRotationSnapTolerance: toleransen %g ligger "
                 "utanför 0–180 grader (ignoreras)", name.c_str(),
                 static_cast<double>(deg));
        return;
    }
    rotationSnapTolerance_ = deg;
}

// ------------------------------------------------------------ bindningarna
//
// Tabellen och uppslaget bor i src/canvas/input_map (ren, grindad headless),
// mängderna i camera_math (`bindingAmount`). Här står det ritytan själv vet:
// läget, flaggorna och vad handlingarna gör med just den här kameran.

detail::InputMap& Canvas::ownInput()
{
    if (!ownInput_)
        ownInput_ = std::make_unique<detail::InputMap>();
    return *ownInput_;
}

// Läget och raderna LAGRAS: nästa händelse läser dem, inget ritas om.
void Canvas::setInputMode(std::string mode)
{
    inputMode_ = std::move(mode);
}

const char* Canvas::autoInputMode() const
{
    return is3D_ ? detail::kModeOrbit : detail::kModeMap;
}

std::string Canvas::activeInputMode() const
{
    return inputMode_.empty() ? std::string(autoInputMode()) : inputMode_;
}

void Canvas::setBinding(const std::string& mode, InputSource s,
                        std::uint32_t mods, InputAction a, InputDirection dir,
                        float speed)
{
    const InputBinding b{mode, s, mods, a, dir, speed};
    const char*        varför = nullptr;
    if (!detail::validBinding(b, varför)) {
        gfx::log("Canvas(%s)::setBinding(\"%s\"): %s (ignoreras)", name.c_str(),
                 mode.c_str(), varför);
        return;
    }
    ownInput().set(b);
}

void Canvas::removeBinding(const std::string& mode, InputSource s,
                           std::uint32_t mods)
{
    if (ownInput_)
        ownInput_->remove(mode, s, mods);
}

void Canvas::clearBindings()
{
    if (ownInput_)
        ownInput_->clearRows();
}

std::vector<InputBinding> Canvas::bindings() const
{
    return ownInput_ ? ownInput_->rows() : std::vector<InputBinding>{};
}

std::optional<InputBinding> Canvas::effectiveBinding(const std::string& mode,
                                                     InputSource s,
                                                     std::uint32_t mods) const
{
    const detail::InputMap& app  = detail::canvasInput();
    const std::string       läge = detail::groupMode(
        ownInput_.get(), app, mode, autoInputMode(), s, mods);
    if (const InputBinding* b =
            detail::lookupBinding(ownInput_.get(), app, läge, s, mods))
        return *b;
    return std::nullopt;
}

void Canvas::setSourceInvert(InputSource s, bool on)
{
    ownInput().setSourceInvert(s, on);
}

void Canvas::clearSourceInvert(InputSource s)
{
    if (ownInput_)
        ownInput_->clearSourceInvert(s);
}

void Canvas::setActionInvert(InputDevice d, InputAction a, bool on)
{
    ownInput().setActionInvert(d, a, on);
}

void Canvas::clearActionInvert(InputDevice d, InputAction a)
{
    if (ownInput_)
        ownInput_->clearActionInvert(d, a);
}

detail::MotionKind Canvas::motionKind() const
{
    if (!is3D_)
        return detail::MotionKind::Map;
    return activeInputMode() == detail::kModeFirstPerson
               ? detail::MotionKind::FirstPerson
               : detail::MotionKind::Orbit;
}

// FLAGGORNA säger OM en handling får verka: panoreringen gäller PanX, PanY,
// Fly och Lift (de två senare gör ingenting med kartkameran och räknas inte
// där, så en sådan rad bubblar), zoomningen Zoom.
std::uint32_t Canvas::enabledActions(detail::MotionKind k) const
{
    const auto    bit = [](InputAction a) { return 1u << static_cast<unsigned>(a); };
    std::uint32_t m   = 0;
    if (pannable_) {
        m |= bit(InputAction::PanX) | bit(InputAction::PanY);
        if (k != detail::MotionKind::Map)
            m |= bit(InputAction::Fly) | bit(InputAction::Lift);
    }
    if (zoomable_)
        m |= bit(InputAction::Zoom);
    if (rotatable_)
        m |= bit(InputAction::Rotate);
    if (pitchable_)
        m |= bit(InputAction::Tilt);
    return m;
}

void Canvas::addMotion(CameraMotion& m, const InputBinding& b, double raw,
                       detail::MotionKind k, const Ctx& c) const
{
    // Förstapersonens zoom ÄR flygningen — farten och tecknet är radens.
    const InputAction a = k == detail::MotionKind::FirstPerson &&
                                  b.action == InputAction::Zoom
                              ? InputAction::Fly
                              : b.action;
    const double v =
        detail::bindingAmount(a, b.source, raw, k, c.style->wheelStep,
                              c.style->canvasPanStep) *
        detail::bindingFactor(ownInput_.get(), detail::canvasInput(), b);
    switch (a) {
    case InputAction::PanX:   m.panX += v; break;
    case InputAction::PanY:   m.panY += v; break;
    case InputAction::Fly:    m.fly += v; break;
    case InputAction::Lift:   m.lift += v; break;
    case InputAction::Zoom:   m.zoom += v; break;
    case InputAction::Rotate: m.rotate += v; break;
    case InputAction::Tilt:   m.tilt += v; break;
    case InputAction::None:   break;
    }
}

bool Canvas::applyMotion(const Ctx& c, const CameraMotion& m, double ax,
                         double ay, bool toAnchor,
                         std::optional<gfx::Pointf> zoomAt,
                         std::optional<gfx::Pointf> turnAt, bool mapPan,
                         bool* turned)
{
    const gfx::Rectf         box = resolve(c);
    const detail::MotionKind k   = motionKind();
    bool                     ändrad = false;
    // Förflyttningens två punkter (fysiska px) för `d` logiska px.
    const auto ändar = [&](double dx, double dy, double& fx, double& fy,
                           double& tx, double& ty) {
        const double px = dx * c.scale, py = dy * c.scale;
        fx = toAnchor ? ax - px : ax;
        fy = toAnchor ? ay - py : ay;
        tx = toAnchor ? ax : ax + px;
        ty = toAnchor ? ay : ay + py;
    };

    if (k == detail::MotionKind::Map) {
        // VRIDNINGEN kring pekaren (styrplattans) eller kamerans egen
        // (draget, tangenterna, hjulet), och TILTEN — bäringen ökar med
        // Rotate, tilten minskar med Tilt (glaze/input.h).
        if (m.rotate != 0.0 && turnAt &&
            rotateAround(box, c.scale, turnAt->x, turnAt->y,
                         static_cast<float>(-m.rotate))) {
            ändrad = true;
            if (turned)
                *turned = true;
        }
        const double vrid = turnAt ? 0.0 : m.rotate;
        if (vrid != 0.0 || m.tilt != 0.0) {
            stopMotion();
            stopSnap();
            materialize();
            const Camera före = cam_;
            cam_.bearing = wrapBearing(cam_.bearing + static_cast<float>(vrid));
            cam_.pitch   = std::clamp(cam_.pitch - static_cast<float>(m.tilt),
                                      0.0f, maxPitch_);
            if (cam_.bearing != före.bearing || cam_.pitch != före.pitch) {
                invalidate();
                ändrad = true;
                if (turned && cam_.bearing != före.bearing)
                    *turned = true;
            }
        }
        // ZOOMEN mot sitt ankare (pekaren, trycket) eller ytans mitt.
        if (m.zoom != 0.0)
            ändrad = zoomAround(box, c.scale,
                                zoomAt ? zoomAt->x : box.x + box.w / 2.0,
                                zoomAt ? zoomAt->y : box.y + box.h / 2.0,
                                detail::zoomLevelsFactor(m.zoom)) ||
                     ändrad;
        // PANORERINGEN — utan grepp (ett drag greppar själv).
        if (mapPan && (m.panX != 0.0 || m.panY != 0.0)) {
            double fx = 0.0, fy = 0.0, tx = 0.0, ty = 0.0;
            ändar(m.panX, m.panY, fx, fy, tx, ty);
            ändrad = panAround(box, c.scale, fx, fy, tx, ty) || ändrad;
        }
        return ändrad;
    }

    if (k == detail::MotionKind::Orbit) {
        if (m.rotate != 0.0 || m.tilt != 0.0)
            ändrad = orbitCamera(static_cast<float>(m.rotate),
                                 static_cast<float>(m.tilt)) ||
                     ändrad;
        if (m.zoom != 0.0)
            ändrad = dollyCamera(detail::dollyLevelsFactor(m.zoom)) || ändrad;
        // FÖRFLYTTNINGEN: ett `panCamera` per lodrätt läge — målplanet (PanY),
        // blicken (Fly), upp-axeln (Lift) — och sidled i det första.
        const std::pair<double, detail::PanVertical> lodräta[] = {
            {m.panY, detail::PanVertical::Plane},
            {m.fly, detail::PanVertical::Fly},
            {m.lift, detail::PanVertical::Lift}};
        double sidled = m.panX;
        bool   flyttad = false;
        for (const auto& [v, läge] : lodräta) {
            if (v == 0.0)
                continue;
            double fx = 0.0, fy = 0.0, tx = 0.0, ty = 0.0;
            ändar(sidled, v, fx, fy, tx, ty);
            ändrad  = panCamera(box, fx, fy, tx, ty, läge) || ändrad;
            sidled  = 0.0;
            flyttad = true;
        }
        if (!flyttad && sidled != 0.0) {
            double fx = 0.0, fy = 0.0, tx = 0.0, ty = 0.0;
            ändar(sidled, 0.0, fx, fy, tx, ty);
            ändrad = panCamera(box, fx, fy, tx, ty, detail::PanVertical::Plane) ||
                     ändrad;
        }
        return ändrad;
    }

    // FÖRSTAPERSONEN: gir och tilt kring ÖGAT, förflyttningen i
    // världsenheter — PanX + flyttar innehållet åt höger, alltså ögat åt
    // vänster, och PanY + innehållet nedåt, alltså ögat uppåt i bildplanet.
    if (m.rotate != 0.0 || m.tilt != 0.0) {
        const Camera3D k3 =
            detail::lookAround(cam3D_, static_cast<float>(m.rotate),
                               static_cast<float>(m.tilt), minElevation_,
                               maxElevation_);
        if (k3.azimuth != cam3D_.azimuth || k3.elevation != cam3D_.elevation) {
            stopMotion();
            cam3D_ = k3;
            invalidate();
            ändrad = true;
        }
    }
    if (m.panX != 0.0 || m.panY != 0.0 || m.fly != 0.0 || m.lift != 0.0) {
        stopMotion();
        detail::moveEye(cam3D_, m.fly, -m.panX, m.lift, m.panY);
        invalidate();
        ändrad = true;
    }
    return ändrad;
}

// ------------------------------------------------------------------ dragen

namespace {

// Knappens två dragaxlar.
std::pair<InputSource, InputSource> dragkällor(gfx::MouseButton b)
{
    if (b == gfx::MouseButton::Middle)
        return {InputSource::MiddleDragX, InputSource::MiddleDragY};
    if (b == gfx::MouseButton::Right)
        return {InputSource::RightDragX, InputSource::RightDragY};
    return {InputSource::LeftDragX, InputSource::LeftDragY};
}

bool förflyttar(const InputBinding* b)
{
    return b && (b->action == InputAction::PanX || b->action == InputAction::PanY ||
                 b->action == InputAction::Fly || b->action == InputAction::Lift);
}

bool vrider(const InputBinding* b)
{
    return b && (b->action == InputAction::Rotate || b->action == InputAction::Tilt);
}

} // namespace

Canvas::DragState& Canvas::dragOf(gfx::MouseButton b)
{
    return drags_[b == gfx::MouseButton::Middle  ? 1
                  : b == gfx::MouseButton::Right ? 2
                                                 : 0];
}

const Canvas::DragState* Canvas::activeDrag() const
{
    if (drags_[0].active && pressed())
        return &drags_[0];
    if (drags_[1].active && middlePressed())
        return &drags_[1];
    if (drags_[2].active && rightPressed())
        return &drags_[2];
    return nullptr;
}

detail::DragPlan Canvas::dragPlan(gfx::MouseButton b) const
{
    const AppWindow*        w    = appWindow();
    const std::uint32_t     mods = w ? w->modifierKeys() : 0;
    const auto [sx, sy]          = dragkällor(b);
    const detail::InputMap& app  = detail::canvasInput();
    const std::string       läge = detail::groupMode(
        ownInput_.get(), app, activeInputMode(), autoInputMode(), sx, mods);
    return detail::resolveDrag(ownInput_.get(), app, läge, sx, sy, mods,
                               enabledActions(motionKind()));
}

bool Canvas::beginDrag(const Ctx& c, gfx::MouseButton b, float x, float y)
{
    DragState& d = dragOf(b);
    d.active     = false;
    d.samples.clear();
    const detail::DragPlan p = dragPlan(b);
    if (!p.any())
        return false;
    const bool pans   = förflyttar(p.x) || förflyttar(p.y);
    const bool others = (p.x && !förflyttar(p.x)) || (p.y && !förflyttar(p.y));
    const bool turns  = vrider(p.x) || vrider(p.y);
    stopMotion();   // ett nytt grepp tar kameran ur en glidning eller tröghet
    if (turns)
        stopSnap();   // … och en ny vridning ur vridsnäppet
    d.grabbed = false;
    if (motionKind() == detail::MotionKind::Map) {
        materialize();
        if (pans) {
            // PANORERINGEN GREPPAR världspunkten under pekaren — den följer
            // den virtuella pekaren sedan, också under tilt.
            double wx = 0.0, wy = 0.0;
            if (gfx::unproject(view(resolve(c), c.scale), x, y, wx, wy)) {
                d.grab    = Point{wx, wy};
                d.grabbed = true;
                d.samples.push_back(detail::PanSample{gfx::ticksMs(), x, y});
            } else if (!others) {
                return false;   // ovanför horisonten finns inget att greppa
            }
        }
    }
    d.lastX = d.pressX = x;
    d.lastY = d.pressY = y;
    d.offX = d.offY = 0.0;
    d.pans          = pans;
    d.others        = others;
    d.turned        = turns;
    d.active        = true;
    return true;
}

void Canvas::moveDrag(const Ctx& c, gfx::MouseButton b, float x, float y)
{
    DragState& d = dragOf(b);
    if (!d.active)
        return;
    // PLANEN PER RÖRELSE: Shift mitt i ett mittendrag byter till
    // flygning, och en avstängd handling slutar verka.
    const detail::DragPlan   p  = dragPlan(b);
    const detail::MotionKind k  = motionKind();
    const float              fx = d.lastX, fy = d.lastY;
    d.lastX = x;
    d.lastY = y;
    // Rörelsen i LOGISKA px — varje källas belopp till sin handling (till exempel
    // `LeftDragY → PanX` panorerar i sidled).
    const double dx = (static_cast<double>(x) - fx) / c.scale;
    const double dy = (static_cast<double>(y) - fy) / c.scale;
    CameraMotion m;
    if (p.x)
        addMotion(m, *p.x, dx, k, c);
    if (p.y)
        addMotion(m, *p.y, dy, k, c);
    const bool pans = förflyttar(p.x) || förflyttar(p.y);
    d.pans   = pans;
    d.others = (p.x && !förflyttar(p.x)) || (p.y && !förflyttar(p.y));
    d.turned = d.turned || vrider(p.x) || vrider(p.y);

    bool ändrad = false;
    if (k == detail::MotionKind::Map) {
        // Vridning, tilt och zoom (kring trycket) först — därefter greppet.
        ändrad = applyMotion(c, m, fx, fy, false, gfx::Pointf{d.pressX, d.pressY},
                             std::nullopt, false);
        if (pans) {
            const gfx::Rectf box = resolve(c);
            // Den VIRTUELLA PEKAREN: där den stod förra rörelsen, flyttad
            // med panoreringens mängd. En panorering som börjar efter en annan
            // handling greppar om under den.
            const double px = fx + d.offX, py = fy + d.offY;
            if (!d.grabbed) {
                double wx = 0.0, wy = 0.0;
                if (gfx::unproject(view(box, c.scale), px, py, wx, wy)) {
                    d.grab    = Point{wx, wy};
                    d.grabbed = true;
                    d.samples.clear();
                }
            }
            const double vx = px + m.panX * c.scale, vy = py + m.panY * c.scale;
            d.offX = vx - x;
            d.offY = vy - y;
            if (d.grabbed) {
                detail::addPanSample(d.samples,
                                     detail::PanSample{gfx::ticksMs(),
                                                       static_cast<float>(vx),
                                                       static_cast<float>(vy)});
                gfx::MapView v = view(box, c.scale);
                if (gfx::anchorWorldAt(v, d.grab.x, d.grab.y, vx, vy) &&
                    !(v.cx == cam_.center.x && v.cy == cam_.center.y)) {
                    cam_.center = Point{v.cx, v.cy};
                    invalidate();
                    ändrad = true;
                }
            }
        } else {
            d.grabbed = false;   // nästa panorering greppar om
        }
    } else {
        // ORBITEN OCH FÖRSTAPERSONEN följer pekaren från rörelse till
        // rörelse: målplanets punkt under förra pekaren hamnar i sidled under
        // den nya (fram/bak som bindningen säger), vinklarna i grader per
        // logisk px.
        ändrad = applyMotion(c, m, fx, fy, false, gfx::Pointf{d.pressX, d.pressY},
                             std::nullopt, true);
    }
    if (ändrad)
        fireCameraChange();
}

void Canvas::endDrag(const Ctx& c, gfx::MouseButton b)
{
    DragState& d = dragOf(b);
    if (!d.active)
        return;
    d.active = false;
    // Inaktiverad under draget: släppet avslutar det, men inget glider ut.
    if (!isEnabled() || motionKind() != detail::MotionKind::Map) {
        d.samples.clear();
        return;   // orbitens och förstapersonens drag glider inte ut och snäpper inte
    }
    if (d.pans && d.grabbed && inertia_ && !d.samples.empty()) {
        // TRÖGHETEN ur rörelsens sista sampel — från där den virtuella
        // pekaren SIST var (där kameran står), inte från släppets punkt.
        detail::InertiaPlan plan;
        const std::uint64_t nu = gfx::ticksMs();
        if (detail::inertiaStart(d.samples, nu, c.scale, plan)) {
            coasting_   = true;
            coastStart_ = nu;
            coastVx_    = plan.vx;
            coastVy_    = plan.vy;
            coastMs_    = plan.durationMs;
            coastX_     = d.samples.back().x;
            coastY_     = d.samples.back().y;
            coastGrab_  = d.grab;
            invalidate();
        }
    }
    d.samples.clear();
    if (d.turned && rotatable_)
        snapRotation();
}

// EN FÖRMÅGA SOM STÄNGS AV MITT I ETT DRAG stänger bara av sina
// handlingar: planen läses om med de nya flaggorna, och draget slutar bara när
// ingen verksam handling återstår — tiltens sättare stoppar inte ett zoomdrag.
void Canvas::reviewDrags()
{
    const gfx::MouseButton knappar[] = {gfx::MouseButton::Left,
                                        gfx::MouseButton::Middle,
                                        gfx::MouseButton::Right};
    for (const gfx::MouseButton b : knappar) {
        DragState& d = dragOf(b);
        if (!d.active)
            continue;
        const detail::DragPlan p = dragPlan(b);
        if (!p.any()) {
            d.active = false;
            d.samples.clear();
            continue;
        }
        d.pans   = förflyttar(p.x) || förflyttar(p.y);
        d.others = (p.x && !förflyttar(p.x)) || (p.y && !förflyttar(p.y));
        if (!d.pans)
            d.grabbed = false;   // en panorering som slås på igen greppar om
    }
}

void Canvas::stopDrags()
{
    for (DragState& d : drags_) {
        d.active = false;
        d.samples.clear();
    }
}

void Canvas::snapRotation()
{
    materialize();
    const std::optional<float> mål =
        detail::snapBearing(cam_.bearing, rotationSnap_, rotationSnapTolerance_);
    if (!mål)
        return;
    // En glidning som är ANVÄNDARENS (onCameraChange per steg) — och EGEN, så
    // att tröghetens panorering efter släppet inte stoppar den (canvas.h).
    snapping_  = true;
    snapFrom_  = cam_.bearing;
    snapTo_    = *mål;
    snapStart_ = gfx::ticksMs();
    invalidate();
}

void Canvas::stepSnap()
{
    if (!snapping_)
        return;
    const double t = static_cast<double>(gfx::ticksMs() - snapStart_) /
                     detail::bearingSnapMs();
    cam_.bearing   = t >= 1.0 ? snapTo_ : detail::easeBearing(snapFrom_, snapTo_, t);
    postCameraChange();   // vridsnäppet är användarens rörelse
    if (t >= 1.0) {
        snapping_ = false;   // framme — ingen mer självinvalidering (regel 1)
        cameraLanded();
        return;
    }
    invalidate();   // nästa frame tar nästa steg — Progress-mönstret
}

bool Canvas::zoomAround(const gfx::Rectf& box, float scale, double px,
                        double py, double factor)
{
    stopMotion();
    materialize();
    // Världspunkten under (px, py) står kvar där — exakt också under tilt.
    // Ovanför horisonten finns ingen, och då zoomar kameran kring mitten.
    double     wx = 0.0, wy = 0.0;
    const bool ankrad = gfx::unproject(view(box, scale), px, py, wx, wy);
    const double z    = clampZoom(cam_.zoom * factor);
    if (!(z > 0.0) || z == cam_.zoom)
        return false;
    cam_.zoom = z;
    if (ankrad) {
        gfx::MapView v = view(box, scale);
        if (gfx::anchorWorldAt(v, wx, wy, px, py))
            cam_.center = Point{v.cx, v.cy};
    }
    invalidate();
    return true;
}

bool Canvas::rotateAround(const gfx::Rectf& box, float scale, double px,
                          double py, float deg)
{
    stopMotion();
    materialize();
    // Världspunkten under (px, py) står kvar — zoomAround:s ankring. Bäringen
    // vrider innehållet MOTURS (gfx/camera.h), så kartan som ska följa
    // fingrarna MEDURS får bäringen minskad med samma grader.
    stopSnap();   // en ny vridning tar över bäringen från snäppet
    double     wx = 0.0, wy = 0.0;
    const bool ankrad = gfx::unproject(view(box, scale), px, py, wx, wy);
    const float före  = cam_.bearing;
    cam_.bearing      = wrapBearing(cam_.bearing - deg);
    if (cam_.bearing == före)
        return false;
    if (ankrad) {
        gfx::MapView v = view(box, scale);
        if (gfx::anchorWorldAt(v, wx, wy, px, py))
            cam_.center = Point{v.cx, v.cy};
    }
    invalidate();
    return true;
}

bool Canvas::panAround(const gfx::Rectf& box, float scale, double fx,
                       double fy, double tx, double ty)
{
    if (fx == tx && fy == ty)
        return false;   // ett End-steg utan förflyttning
    stopMotion();
    materialize();
    // DRAGPANORERINGENS GREPP: världspunkten under (fx, fy) hamnar under
    // (tx, ty) — exakt också vriden och tiltad. Står punkten ovanför
    // horisonten greppas ytans mitt, som alltid ligger på marken.
    gfx::MapView v  = view(box, scale);
    double       gx = fx, gy = fy, wx = 0.0, wy = 0.0;
    if (!gfx::unproject(v, gx, gy, wx, wy)) {
        gx = box.x + box.w / 2.0;
        gy = box.y + box.h / 2.0;
        if (!gfx::unproject(v, gx, gy, wx, wy))
            return false;
    }
    if (!gfx::anchorWorldAt(v, wx, wy, gx + (tx - fx), gy + (ty - fy)) ||
        (v.cx == cam_.center.x && v.cy == cam_.center.y))
        return false;
    cam_.center = Point{v.cx, v.cy};
    invalidate();
    return true;
}

bool Canvas::fireCameraChange()
{
    if (!onCameraChange)
        return false;
    ChangeArgs e;
    onCameraChange(this, e);
    return true;
}

void Canvas::postCameraChange()
{
    // Ur RITNINGEN (tröghetens och snäppets steg): ingen handler får köras
    // mitt i en frame, så händelsen går till pump-svansen — en åt gången,
    // och aldrig till en rityta som hunnit dö (livstecknet). I RAMKÖN, som
    // ticken.
    if (!onCameraChange || cameraPosted_)
        return;
    cameraPosted_                 = true;
    const std::weak_ptr<char> liv = life_;
    detail::postFrameWork([this, liv] {
        if (liv.expired())
            return;
        cameraPosted_ = false;
        if (fireCameraChange())
            invalidate();   // ritytan, inte fönstret (canvas.h)
    });
}

// ------------------------------------------------------------------ träffprov

std::vector<HitResult> Canvas::hitTest(Point local) const
{
    const ProfileScope mät(detail::frameworkLabel(detail::FrameworkLabel::HitTest));
    std::vector<HitResult> out;
    const AppWindow*       w = appWindow();
    if (!w || !w->isOpen())
        return out;
    const Ctx          c   = behaviorCtx();
    const gfx::Rectf   box = resolve(c);
    // Ritningens projektion (3D-kameran, kartkameran i 3D-målet eller i
    // 2D), och kamerans skala i mitten — ritningens läge.
    const detail::Projection proj = projection(box, c.scale);
    detail::PaintContext     pc;
    paintContext(c, pc, box);
    detail::HitQuery   q;
    q.proj       = &proj;
    q.px         = box.x + local.x * c.scale;
    q.py         = box.y + local.y * c.scale;
    q.tolPx      = c.style->canvasHitTolerance * c.scale;
    q.pxPerPx    = c.scale;
    q.pxPerMm    = c.pxPerMm;
    q.pxPerWorld = pc.world.pxPerUnit;
    const double zoom = pc.zoom;
    q.zoom            = zoom;   // text och markörer utanför sitt intervall
    const CanvasResources::LabelState* labels =
        c.canvas ? c.canvas->findLabelState(scene_->id) : nullptr;
    // Lagren uppifrån — bara de som ritas.
    for (int li = static_cast<int>(scene_->layers.size()) - 1; li >= 0; --li) {
        detail::LayerData& L = *scene_->layers[static_cast<std::size_t>(li)];
        if (!L.visible || !(L.opacity > 0.0f) ||
            (L.minZoom > 0.0 && zoom < L.minZoom) ||
            (L.maxZoom > 0.0 && zoom > L.maxZoom))
            continue;
        for (const detail::LayerHit& h : detail::hitLayer(L, q, c.canvas, labels)) {
            HitResult r{li, h.index};
            r.element = h.element;   // markörens punkt
            out.push_back(r);
        }
    }
    return out;
}

int Canvas::shapeAt(Point local) const
{
    return shapeAt(0, local);
}

int Canvas::shapeAt(int layer, Point local) const
{
    for (const HitResult& h : hitTest(local))
        if (h.layer == layer)
            return h.index;
    return -1;
}

// ------------------------------------------------------------ 3D-plockningen
// Formerna i rymden plockas i ../canvas/pick3d.cpp och
// 2D-formerna och etiketterna i ../canvas/hit_index.cpp; här samlas lagren,
// vyn, sorteringen och skymningen.

namespace {

// Går formen genom etiketterna (hit_index.cpp `viaLabels`)?
bool isLabel(const Shape& s)
{
    return s.kind() == Shape::Kind::Text || s.kind() == Shape::Kind::Text3 ||
           ((s.kind() == Shape::Kind::Markers || s.kind() == Shape::Kind::Markers3) &&
            s.markerStyle().collide);
}

// Plockas formen i rymden (pick3d.h)?
using detail::isObject3D;

// Ritas lagret i vyn vid zoomen (scene.cpp `layerDrawn`, och synligheten)?
bool drawnIn(const detail::LayerData& L, int view, double zoom)
{
    return L.visible && detail::layerInView(L, view) && L.opacity > 0.0f &&
           !(L.minZoom > 0.0 && zoom < L.minZoom) &&
           !(L.maxZoom > 0.0 && zoom > L.maxZoom);
}

double rayParam(const Ray3& r, const Point3& p)
{
    return (p.x - r.origin.x) * r.direction.x + (p.y - r.origin.y) * r.direction.y +
           (p.z - r.origin.z) * r.direction.z;
}

using detail::unitRay;

} // namespace

std::vector<HitResult> Canvas::hitTest(Point local, const HitOptions& options) const
{
    const ProfileScope mät(detail::frameworkLabel(detail::FrameworkLabel::HitTest));
    std::vector<HitResult> out;
    const AppWindow*       w = appWindow();
    if (!w || !w->isOpen())
        return out;
    const Ctx        c   = behaviorCtx();
    const gfx::Rectf box = resolve(c);
    const double     px  = box.x + local.x * c.scale;
    const double     py  = box.y + local.y * c.scale;

    // VYN under pekaren — den översta del-vyn, annars huvudvyn (target3d.h:s
    // definition av vyns ruta och kamera, delad med ritningen) — och dess
    // kamera: 3D-vyn för strålen och formerna i rymden, projektionen och
    // skalan "i mitten" för 2D-formerna och etiketterna.
    const int         vy    = viewUnder(box, c.scale, px, py);
    const gfx::View3D huvud = mainView3DFor(box, c.scale);
    gfx::View3D       v3    = huvud;
    double            zoom = 0.0, pxPerWorld = 0.0;
    std::optional<detail::Projection> proj2d;
    if (vy == detail::kMainView) {
        detail::PaintContext pc;
        paintContext(c, pc, box);
        zoom       = pc.zoom;
        pxPerWorld = pc.world.pxPerUnit;
        proj2d.emplace(projection(box, c.scale));
    } else {
        // Vyns kamera i fönstrets px (strålen och formerna i rymden) och dess
        // skugga "i mitten" (text och markörer i världsenheter, lagrens
        // zoomintervall) — target3d.h:s definitioner, som ritningens.
        const View&      vw   = views_[static_cast<std::size_t>(vy - 1)];
        const Camera3D   cam  = detail::viewCamera(vw, is3D_ ? &cam3D_ : nullptr, &huvud);
        const gfx::Rectf rect = detail::viewRect(vw, box, c.scale);
        v3                    = detail::viewView3D(cam, rect, *scene_, vy);
        const gfx::MapView mv = detail::mapViewAt(cam, rect, yUp_);
        zoom                  = mv.pxPerUnit / c.scale;
        pxPerWorld            = mv.pxPerUnit;
        proj2d.emplace(viewProjection(vy, box, c.scale));
    }
    gfx::Ray3d gr;
    if (!gfx::rayThrough(v3, px, py, gr))
        return out;
    const Ray3               ray{{gr.origin.x, gr.origin.y, gr.origin.z},
                                 {gr.dir.x, gr.dir.y, gr.dir.z}};
    const detail::Projection proj3(v3);
    const bool               target3D = uses3DTarget();

    // TOLERANSERNA: ANROPETS `tolerance` är basen — 0 där är
    // stilens `canvasHitTolerance` — och hörnens, kanternas och punkternas
    // egna gäller när de inte är 0. Px och mm i fysiska px, världsenheter
    // omräknade vid träffen (pick3d.h `ScreenTol`) — för 2D-formerna vid
    // pekaren (hit_index.h `HitQuery::tolWorld`).
    const auto tol = [&](Length l, detail::ScreenTol annars) {
        detail::ScreenTol t;
        if (!(l.value > 0.0f))
            return annars;
        if (l.unit == Unit::Mm)
            t.px = l.value * c.pxPerMm;
        else if (l.unit == Unit::World)
            t.world = l.value;
        else
            t.px = l.value * c.scale;
        return t;
    };
    detail::ScreenTol stil;
    stil.px                    = c.style->canvasHitTolerance * c.scale;
    const detail::ScreenTol bas = tol(options.tolerance, stil);
    // HÖRNENS OCH KANTERNAS toleranser sätts per lager nedan: ett lager utan
    // snap (`setLayerSnap`) söker dem inte.
    const detail::ScreenTol hörnTol = tol(options.vertexTolerance, bas);
    const detail::ScreenTol kantTol = tol(options.edgeTolerance, bas);
    detail::PickContext pk;
    pk.ray         = ray;
    pk.view        = &v3;
    pk.proj        = &proj3;
    pk.px          = px;
    pk.py          = py;
    pk.pxPerPx     = c.scale;
    pk.pxPerMm     = c.pxPerMm;
    pk.pointTol    = tol(options.pointTolerance, bas);
    pk.lineTol     = bas;
    pk.opaqueOnly  = options.opaqueOnly;
    pk.maxDistance = options.maxDistance;

    // TERRÄNGEN under pekaren (den närmaste ytan i ett Terrain-lager): där
    // ligger Ground-lagrens 2D-former. Och SKYMMARNA — ytorna i lager som
    // skriver djup i 3D-målet — när skymningen efterfrågas (bara i 3D-målet;
    // 2D-vägen har inget djup).
    const bool                      skym = options.occlusion && target3D;
    std::optional<detail::SceneHit> terräng;
    std::vector<detail::Occluder>   skymmare;
    // Ett ritat Ground-lager MÅLAR MARKEN (scene.h `paintsGround` — ritningens
    // egen fråga; ett tomt gör det inte).
    bool                            markRitas = false;
    // En TERRÄNG ritas i vyn (src/canvas/drape.h — en mesh
    // eller yta i rymden i ett Terrain-lager): då ligger Ground-lagren bara i
    // draperingen, och markdjupet uteblir.
    bool                            terrängRitas = false;
    // TERRÄNGFORMERNAS YTOR plockas EN gång, här: formerna i
    // rymden nedan lägger dem bland kandidaterna och söker bara hörnen och
    // kanterna. Gäller en mesh eller yta i rymden — drape.h:s terrängformer,
    // med högst en yta och den först — och bara utan `opaqueOnly`, för här
    // plockas ytan utan.
    struct Yta {
        const Shape*     form;
        detail::SceneHit träff;
    };
    std::vector<Yta> ytor;
    const auto       förplockad = [&](const Shape& s) {
        return !options.opaqueOnly &&
               (s.kind() == Shape::Kind::Mesh || s.kind() == Shape::Kind::Area3);
    };
    for (const std::shared_ptr<detail::LayerData>& Lp : scene_->layers) {
        const detail::LayerData& L = *Lp;
        if (!drawnIn(L, vy, zoom))
            continue;
        const LayerPlacement place = detail::effectivePlacement(L);
        if (place == LayerPlacement::Ground && detail::paintsGround(L))
            markRitas = true;
        const bool djup = target3D && place != LayerPlacement::Ground &&
                          L.state.depthWrite;
        for (const detail::ShapeSlot& slot : L.slots) {
            const Shape& s = slot.entry->shape;
            if (!isObject3D(s))
                continue;
            if (skym && djup)
                skymmare.push_back(detail::occluder(*slot.entry, L.opacity));
            if (place != LayerPlacement::Terrain)
                continue;
            if ((s.kind() == Shape::Kind::Mesh || s.kind() == Shape::Kind::Area3) &&
                s.mesh().valid())
                terrängRitas = target3D;
            detail::PickContext tk = pk;
            tk.proj       = nullptr;   // bara ytan
            tk.opaqueOnly = false;
            tk.layerOpacity = L.opacity;
            std::vector<detail::SceneHit> th;
            detail::pickShape3D(*slot.entry, tk, th);
            for (const detail::SceneHit& h : th) {
                if (h.kind != HitKind::Surface)
                    continue;
                if (!terräng || h.distance < terräng->distance)
                    terräng = h;
                if (förplockad(s))
                    ytor.push_back(Yta{&s, h});
            }
        }
    }
    // MARKDJUPET (target3d.cpp): i 3D-målet skymmer planet z = 0 det
    // som ligger under marken när ett Ground-lager med markinnehåll ritas i
    // vyn — och ingen terräng (då finns inget markplan, och ett hål visar det
    // bakom).
    std::optional<double> markplan;
    if (target3D && markRitas && !terrängRitas)
        if (const auto h = detail::rayPlane(ray, Plane3{}))
            markplan = h->t;

    // KANDIDATERNA — grupp 0 är Ground-lagrens etiketter i 3D-målet (överst,
    // skyms aldrig), resten sorteras på avståndet; lika avstånd behåller
    // lagrens ordning uppifrån (2D-vägens "överst först").
    struct Kandidat {
        HitResult   h;
        int         grupp    = 1;
        std::size_t ordning  = 0;
        bool        påStrålen = true;   // ligger på pekarens stråle
        // Ett av FLERA inom toleransen (hörn, kant, punkt, linjesegment):
        // det närmaste SYNLIGA på skärmen per form och sort står kvar.
        bool   urval = false;
        double skärm = 0.0;   // skärmavståndet, fysiska px
    };
    std::vector<Kandidat> kand;
    const CanvasResources::LabelState* labels =
        c.canvas ? c.canvas->findLabelState(scene_->id, vy) : nullptr;
    for (int li = static_cast<int>(scene_->layers.size()) - 1; li >= 0; --li) {
        detail::LayerData& L = *scene_->layers[static_cast<std::size_t>(li)];
        if (!drawnIn(L, vy, zoom))
            continue;
        const LayerPlacement place  = detail::effectivePlacement(L);
        const bool           ground = place == LayerPlacement::Ground;

        // 2D-FORMERNA OCH ETIKETTERNA (hit_index.h) — på terrängen när den
        // ligger under pekaren (där Ground-lagret draperas), annars på
        // planet z = 0, där en träff ligger på strålens skärning.
        detail::HitQuery q;
        q.proj       = &*proj2d;
        q.px         = px;
        q.py         = py;
        q.tolPx      = bas.px;
        q.tolWorld   = bas.world;
        q.pxPerPx    = c.scale;
        q.pxPerMm    = c.pxPerMm;
        q.pxPerWorld = pxPerWorld;
        q.zoom       = zoom;
        std::optional<Point3> mark;   // markpunkten under pekaren
        double                markAvst = 0.0;
        if (ground && terräng) {
            q.hasGround       = true;
            q.groundX         = terräng->point.x;
            q.groundY         = terräng->point.y;
            q.groundPxPerUnit = gfx::pxPerUnitAt(
                v3, gfx::Vec3d{terräng->point.x, terräng->point.y, terräng->point.z});
            mark     = terräng->point;
            markAvst = terräng->distance;
        } else if (ground && terrängRitas) {
            // Terräng ritas men inte under pekaren (ett hål, bortom den): då
            // finns marken bara i draperingen, och här ritas inget av lagrets
            // geometri — bara markörerna och etiketterna prövas (hit_index.h
            // `noGround`).
            q.noGround = true;
        } else if (const auto h = detail::rayPlane(ray, Plane3{})) {
            // Planet z = 0.
            mark     = h->point;
            markAvst = h->t;
        }
        for (const detail::LayerHit& lh : detail::hitLayer(L, q, c.canvas, labels)) {
            const int    i = lh.index;
            const Shape& s = L.slots[static_cast<std::size_t>(i)].entry->shape;
            Kandidat     k;
            k.h.layer   = li;
            k.h.index   = i;
            k.h.element = lh.element;   // markörens punkt
            k.h.view    = vy;
            k.ordning   = kand.size();
            const bool etikett = isLabel(s);
            const bool iRymden = s.kind() == Shape::Kind::Text3 ||
                                 s.kind() == Shape::Kind::Markers3;
            // LIGGANDE text och markörer på terräng ligger i draperingen — en
            // del av marken, som varken kolliderar eller träffas (drape.h).
            const bool liggande =
                ((s.kind() == Shape::Kind::Text &&
                  s.textStyle().rotation == Alignment::Ground) ||
                 (s.kind() == Shape::Kind::Markers &&
                  s.markerStyle().rotation == Alignment::Ground));
            if (ground && terrängRitas && liggande)
                continue;
            k.h.kind = etikett ? HitKind::Label : HitKind::Shape;
            if (etikett || s.kind() == Shape::Kind::Markers ||
                s.kind() == Shape::Kind::Markers3) {
                // Etiketten och markören vid sitt ANKARE — den träffade
                // punkten för en markörform — på terräng vid
                // markhöjd, som de ritas (drape.h).
                const std::size_t pi =
                    lh.element > 0 ? static_cast<std::size_t>(lh.element) : 0;
                const double z0 = ground && terrängRitas && !iRymden
                                      ? detail::terrainHeight(*scene_,
                                                              s.points()[pi].x,
                                                              s.points()[pi].y)
                                            .value_or(0.0)
                                      : 0.0;
                const Point3 a = iRymden ? s.points3()[pi]
                                         : Point3{s.points()[pi].x,
                                                  s.points()[pi].y, z0};
                k.h.point = a;
                // På marken i ett Ground-lager räknas de som marken under
                // pekaren (målarordningen gäller där); i rymden längs
                // strålen till ankarets fotpunkt.
                k.påStrålen  = ground;
                k.h.distance = ground ? markAvst : rayParam(ray, a);
                // Ground-lagrens text och markörer ritas ÖVERST i 3D-målet
                // (target3d.h) — de står först och skyms aldrig.
                k.grupp = target3D && ground ? 0 : 1;
            } else {
                k.h.point    = mark;
                k.h.distance = markAvst;
            }
            if (options.maxDistance > 0.0 && k.grupp != 0 &&
                k.h.distance > options.maxDistance)
                continue;
            kand.push_back(k);
        }

        // FORMERNA I RYMDEN (pick3d.h — formplatsen, så att dess cachade låda
        // sållar först) — hörnen och kanterna bara i ett lager med snap, och
        // terrängformernas yta ur loopen ovan (`ytor`), före deras hörn och
        // kanter som när den plockas här.
        pk.layerOpacity = L.opacity;
        pk.occluder     = target3D && !ground && L.state.depthWrite;
        pk.vertexTol    = L.snap ? hörnTol : detail::ScreenTol{};
        pk.edgeTol      = L.snap ? kantTol : detail::ScreenTol{};
        std::vector<detail::SceneHit> träffar;
        for (std::size_t i = 0; i < L.slots.size(); ++i) {
            const Shape& s = L.slots[i].entry->shape;
            if (!isObject3D(s))
                continue;
            träffar.clear();
            pk.surface = !(place == LayerPlacement::Terrain && förplockad(s));
            if (!pk.surface)
                for (const Yta& y : ytor)
                    if (y.form == &s)
                        träffar.push_back(y.träff);
            detail::pickShape3D(*L.slots[i].entry, pk, träffar);
            for (const detail::SceneHit& t : träffar) {
                Kandidat k;
                k.h.layer    = li;
                k.h.index    = static_cast<int>(i);
                k.h.view     = vy;
                k.h.instance = t.instance;
                k.h.element  = t.element;
                k.h.kind     = t.kind;
                k.h.point    = t.point;
                k.h.normal   = t.normal;
                k.h.distance = t.distance;
                k.påStrålen  = t.kind == HitKind::Surface;
                k.urval      = !k.påStrålen;
                k.skärm      = t.screen;
                k.ordning    = kand.size();
                kand.push_back(k);
            }
        }
    }

    // SKYMNINGEN (`occlusion`, `skym` ovan): det på strålen bakom den första
    // ogenomskinliga ytan — eller marken — faller bort; det bredvid strålen
    // prövas längs SIN synlinje (genom sin egen pixel).
    const auto skymd = [&](const Point3& p) {
        double     sx = 0.0, sy = 0.0;
        gfx::Ray3d los;
        if (!gfx::project(v3, gfx::Vec3d{p.x, p.y, p.z}, sx, sy) ||
            !gfx::rayThrough(v3, sx, sy, los))
            return false;
        const Ray3   sikt{{los.origin.x, los.origin.y, los.origin.z},
                          {los.dir.x, los.dir.y, los.dir.z}};
        const double avst  = rayParam(sikt, p);
        const double grans = avst - detail::occlusionSlack(avst);
        if (const auto o = detail::nearestOccluder(skymmare, sikt, &v3); o && *o < grans)
            return true;
        if (markplan) {
            const auto m = detail::rayPlane(sikt, Plane3{});
            return m && m->t < grans;
        }
        return false;
    };
    if (skym) {
        std::optional<double> närmast = detail::nearestOccluder(skymmare, ray, &v3);
        if (markplan && (!närmast || *markplan < *närmast))
            närmast = markplan;
        std::erase_if(kand, [&](const Kandidat& k) {
            if (k.grupp == 0 || !k.h.point || k.urval)
                return false;   // urvalet prövas nedan, ett i taget
            if (k.påStrålen)
                return närmast &&
                       *närmast < k.h.distance - detail::occlusionSlack(k.h.distance);
            return skymd(*k.h.point);
        });
    }
    // URVALET: per form och sort det NÄRMASTE på skärmen (därefter längs
    // strålen, instansen, elementet) — det närmaste SYNLIGA med skymningen,
    // där synlinjerna prövas i den ordningen tills ett syns.
    {
        std::vector<std::size_t> rad;
        for (std::size_t i = 0; i < kand.size(); ++i)
            if (kand[i].urval)
                rad.push_back(i);
        std::sort(rad.begin(), rad.end(), [&](std::size_t a, std::size_t b) {
            const Kandidat& x = kand[a];
            const Kandidat& y = kand[b];
            if (x.h.layer != y.h.layer)
                return x.h.layer < y.h.layer;
            if (x.h.index != y.h.index)
                return x.h.index < y.h.index;
            if (x.h.kind != y.h.kind)
                return x.h.kind < y.h.kind;
            if (x.skärm != y.skärm)
                return x.skärm < y.skärm;
            if (x.h.distance != y.h.distance)
                return x.h.distance < y.h.distance;
            if (x.h.instance != y.h.instance)
                return x.h.instance < y.h.instance;
            return x.h.element < y.h.element;
        });
        std::vector<char> behåll(kand.size(), 1);
        for (std::size_t i : rad)
            behåll[i] = 0;
        for (std::size_t r = 0; r < rad.size();) {
            const Kandidat& först = kand[rad[r]];
            std::size_t     slut  = r;
            while (slut < rad.size() && kand[rad[slut]].h.layer == först.h.layer &&
                   kand[rad[slut]].h.index == först.h.index &&
                   kand[rad[slut]].h.kind == först.h.kind)
                ++slut;
            for (std::size_t j = r; j < slut; ++j) {
                const Kandidat& k = kand[rad[j]];
                if (!skym || !skymd(*k.h.point)) {
                    behåll[rad[j]] = 1;
                    break;
                }
            }
            r = slut;
        }
        std::vector<Kandidat> kvar;
        kvar.reserve(kand.size());
        for (std::size_t i = 0; i < kand.size(); ++i)
            if (behåll[i])
                kvar.push_back(kand[i]);
        kand = std::move(kvar);
    }

    std::stable_sort(kand.begin(), kand.end(), [](const Kandidat& a, const Kandidat& b) {
        if (a.grupp != b.grupp)
            return a.grupp < b.grupp;
        if (a.h.distance != b.h.distance)
            return a.h.distance < b.h.distance;
        return a.ordning < b.ordning;
    });
    out.reserve(kand.size());
    for (const Kandidat& k : kand)
        out.push_back(k.h);
    return out;
}

std::vector<HitResult> Canvas::hitTest(Rect local, HitMode mode) const
{
    const ProfileScope mät(detail::frameworkLabel(detail::FrameworkLabel::HitTest));
    std::vector<HitResult> out;
    if (!std::isfinite(local.x) || !std::isfinite(local.y) ||
        !std::isfinite(local.w) || !std::isfinite(local.h)) {
        gfx::log("Canvas(%s)::hitTest: rutan är inte ändlig — inga träffar",
                 name.c_str());
        return out;
    }
    const AppWindow* w = appWindow();
    if (!w || !w->isOpen())
        return out;
    const Ctx        c   = behaviorCtx();
    const gfx::Rectf box = resolve(c);
    // Ritningens projektion och läge i huvudvyn — `hitTest(Point)`s.
    const detail::Projection proj = projection(box, c.scale);
    detail::PaintContext     pc;
    paintContext(c, pc, box);
    detail::HitQuery q;
    q.proj       = &proj;
    q.pxPerPx    = c.scale;
    q.pxPerMm    = c.pxPerMm;
    q.pxPerWorld = pc.world.pxPerUnit;
    q.zoom       = pc.zoom;
    // Rutan i målets fysiska px med hörnen i ordning — ett gummiband dras åt
    // vilket håll som helst.
    const double ax = box.x + local.x * c.scale;
    const double bx = box.x + (local.x + local.w) * c.scale;
    const double ay = box.y + local.y * c.scale;
    const double by = box.y + (local.y + local.h) * c.scale;
    const detail::ScreenRect r{std::min(ax, bx), std::min(ay, by),
                               std::max(ax, bx), std::max(ay, by)};
    // Med terräng i 3D-målet finns Ground-lagren bara i draperingen (drape.h)
    // — de prövas inte (canvas.h).
    const bool terräng = uses3DTarget() && detail::hasTerrain(pc, *scene_);
    const CanvasResources::LabelState* labels =
        c.canvas ? c.canvas->findLabelState(scene_->id) : nullptr;
    // Lagren uppifrån — bara de som ritas i huvudvyn.
    for (int li = static_cast<int>(scene_->layers.size()) - 1; li >= 0; --li) {
        detail::LayerData& L = *scene_->layers[static_cast<std::size_t>(li)];
        if (!drawnIn(L, detail::kMainView, q.zoom) ||
            (terräng && detail::effectivePlacement(L) == LayerPlacement::Ground))
            continue;
        for (const detail::LayerHit& h :
             detail::hitLayerRect(L, q, r, mode == HitMode::Window, c.canvas,
                                  labels)) {
            HitResult t{li, h.index};
            t.element = h.element;
            out.push_back(t);
        }
    }
    return out;
}

std::vector<HitResult> Canvas::castRay(const Ray3& ray, const HitOptions& options) const
{
    const ProfileScope mät(detail::frameworkLabel(detail::FrameworkLabel::CastRay));
    // Kameran som ritar ger detaljnivån och de skärmkonstanta meshernas
    // storlek; utan en känd yta den finaste nivån (och inga skärmkonstanta).
    gfx::Rectf                 box{};
    float                      scale = 0.0f;
    std::optional<gfx::View3D> v3;
    if (knownBox(box, scale))
        v3 = mainView3DFor(box, scale);
    double pxPerPx = 0.0, pxPerMm = 0.0;
    if (const AppWindow* w = appWindow(); w && w->isOpen()) {
        const Ctx c = behaviorCtx();
        pxPerPx     = c.scale;
        pxPerMm     = c.pxPerMm;
    }
    return detail::castRayInScene(*scene_, ray, options, v3 ? &*v3 : nullptr,
                                  pxPerPx, pxPerMm);
}

std::optional<double> Canvas::heightAt(Point xy) const
{
    const ProfileScope mät(detail::frameworkLabel(detail::FrameworkLabel::HeightAt));
    // EN definition av terrängens höjd: draperingens etiketter vid markhöjd
    // frågar samma (src/canvas/drape.h `terrainHeight` — den lodräta strålen
    // genom terrängformerna, finaste nivån).
    return detail::terrainHeight(*scene_, xy.x, xy.y);
}

// ---------------------------------------------------------- mus och tangent
//
// APPENS HANDLER FÖRST, därefter gesten — om handlern inte satt `handled`. Ett
// tryck appen tog startar ingen gest, och ett drag eller släpp den tog gör
// ingenting med kameran just då. TRYCKET, SLÄPPET OCH AVBROTTET fyrar
// fönstret (`Widget::onMouseDown`/`onMouseUp`/`onMouseCancel`) med
// ritytans args (`fireMouse`), före krokarna nedan: ett tryck appen tog når
// aldrig `mouseDown`/`middleDown` utan kommer genom `appPress`, och ett släpp
// appen tog från ritytans egen gest blir `mouseCancel` — ingen tröghet.

CanvasMouseArgs Canvas::mouseArgs(const Ctx& c, float x, float y) const
{
    CanvasMouseArgs  e;
    const gfx::Rectf box = resolve(c);
    e.local = Point{Draw::localX(c, box, x), Draw::localY(c, box, y)};
    // Världspunkten — med 3D-kameran marken (z = 0) under pekaren.
    double     wx = 0.0, wy = 0.0;
    const bool ok = is3D_ ? projection(box, c.scale).ground(x, y, wx, wy)
                          : gfx::unproject(view(box, c.scale), x, y, wx, wy);
    if (ok)
        e.world = Point{wx, wy};
    // Strålen genom pekaren — huvudvyns, som `rayAt`.
    gfx::Ray3d r;
    if (gfx::rayThrough(mainView3DFor(box, c.scale), x, y, r))
        e.ray = Ray3{{r.origin.x, r.origin.y, r.origin.z}, {r.dir.x, r.dir.y, r.dir.z}};
    if (const AppWindow* w = appWindow())
        e.mods = w->modifierKeys();
    return e;
}

// Basens tre mushandlers med RITYTANS args — världen, strålen och de lokala
// px ur pekaren (utan en känd pekare bara modifierarna).
bool Canvas::fireMouse(const Handler<MouseArgs>& h, const Ctx& c,
                       std::optional<gfx::Pointf> pos, gfx::MouseButton b)
{
    CanvasMouseArgs e;
    if (pos)
        e = mouseArgs(c, pos->x, pos->y);
    else if (const AppWindow* w = appWindow())
        e.mods = w->modifierKeys();
    e.button = b;
    h(this, e);
    return e.handled;
}

void Canvas::mouseDown(const Ctx& c, float x, float y)
{
    // Appens `onMouseDown` har redan fyrats, och den tog inte trycket.
    coasting_ = false;   // ett tryck fångar en kastad karta
    beginDrag(c, gfx::MouseButton::Left, x, y);
}

// `onMouseMove` med knappen under ett drag ritytan håller — per rörelse, ingen
// full omritning (Sliderns prejudikat): det handlern ändrar invaliderar sina
// sättare själva. false när handlern tog rörelsen eller rev eller lossade
// ritytan — då rörs varken draget eller `this`.
bool Canvas::dragMoveHandler(const Ctx& c, float x, float y, gfx::MouseButton b)
{
    if (!onMouseMove)
        return true;
    CanvasMouseArgs e = mouseArgs(c, x, y);
    e.button          = b;
    const HandlerGuard vakt(*this);
    onMouseMove(this, e);
    return vakt.ok() && !e.handled;
}

void Canvas::mouseDrag(const Ctx& c, float x, float y)
{
    if (dragMoveHandler(c, x, y, gfx::MouseButton::Left))
        moveDrag(c, gfx::MouseButton::Left, x, y);
}

void Canvas::mouseMove(const Ctx& c, float x, float y)
{
    // Håller ritytan sitt EGET tryck — vänster, mitten eller höger — har
    // rörelsen redan kommit genom `mouseDrag`/`middleDrag`/`rightDrag`, med
    // knappen: fönstrets hover-krok frågar widgeten under pekaren också under
    // ett tryck, och samma rörelse ska nå appen EN gång — annars ser ett
    // gummiband en knapplös rörelse mitt i sitt drag. (Ett tryck appen tog
    // frågar fönstret aldrig hover-kroken om — rörelsen kommer genom
    // `appPress`.)
    if (!onMouseMove || pressed() || middlePressed() || rightPressed())
        return;
    CanvasMouseArgs e = mouseArgs(c, x, y);
    onMouseMove(this, e);
}

bool Canvas::mouseUp(const Ctx& c, float, float)
{
    // Appens `onMouseUp` har redan fyrats och tog inte släppet (då hade
    // fönstret gett `mouseCancel` i stället).
    endDrag(c, gfx::MouseButton::Left);
    return false;
}

// HJULET OCH STYRPLATTANS TVÅFINGERSDRAG — bindningarna per händelse:
// mushjulets rader (`WheelX`/`WheelY`) eller tvåfingersdragets innehållsrörelse
// i logiska px (`ScrollX`/`ScrollY`, fasadens kontrakt). Ritytan får det RÅA
// hjulet (`followsWheelDirection`), så Shift+mushjul är `WheelY` med Shift och
// appens rullningsriktning rör det inte. Utan en påslagen bindning för någon
// av axlarna bubblar hjulet — till en rullande förälder. Mushjulets zoom går
// mot pekaren och orbitens dolly mot målet; tvåfingersdraget panorerar med
// pekaren som grepp, och OS:ets egen tröghet (fasen Momentum) panorerar
// likadant — ritytans tröghet startar inte. Efter handlern ritas ritytan om,
// inte fönstret (canvas.h).
bool Canvas::mouseWheel(const Ctx& c, float x, float y, float wx, float wy)
{
    // Hjulets SORT ur fönstret (modifierKeys-mönstret — ingen ny parameter).
    const AppWindow* w       = appWindow();
    const bool       precise = w && w->isPreciseWheel();
    if (onMouseWheel) {
        CanvasMouseArgs e = mouseArgs(c, x, y);
        e.wheelX          = wx;
        e.wheelY          = wy;
        e.precise         = precise;
        const HandlerGuard vakt(*this);
        onMouseWheel(this, e);
        if (!vakt.ok() || e.handled)
            return true;   // tagen — eller ritytan riven: fönstret frågar sin vakt
        w = appWindow();
    }
    const std::uint32_t mods = w ? w->modifierKeys() : 0;
    const InputSource   sx   = precise ? InputSource::ScrollX : InputSource::WheelX;
    const InputSource   sy   = precise ? InputSource::ScrollY : InputSource::WheelY;
    double rx = wx, ry = wy;
    if (precise)
        detail::preciseScroll(wx, wy, rx, ry);   // logiska px
    const detail::MotionKind k    = motionKind();
    const std::uint32_t      på   = enabledActions(k);
    const detail::InputMap&  app  = detail::canvasInput();
    const std::string        läge = detail::groupMode(
        ownInput_.get(), app, activeInputMode(), autoInputMode(), sx, mods);
    // Bara en axel som RÖR SIG och har en verksam bindning tar hjulet:
    // `ScrollY → None` med `ScrollX → PanX` låter ett rent lodrätt drag
    // bubbla, och ett End-steg utan förflyttning tas inte.
    const InputBinding* bx =
        rx != 0.0 ? detail::resolveEvent(ownInput_.get(), app, läge, sx, mods, på)
                  : nullptr;
    const InputBinding* by =
        ry != 0.0 ? detail::resolveEvent(ownInput_.get(), app, läge, sy, mods, på)
                  : nullptr;
    if (!bx && !by)
        return false;   // bubblar vidare — till en rullande förälder
    CameraMotion m;
    if (bx)
        addMotion(m, *bx, rx, k, c);
    if (by)
        addMotion(m, *by, ry, k, c);
    if (applyMotion(c, m, x, y, false, gfx::Pointf{x, y}, std::nullopt, true) &&
        fireCameraChange())
        invalidate();   // ritytan, inte fönstret (canvas.h)
    return true;
}

// STYRPLATTANS NYPNING OCH VRIDNING — fönstrets gestsekvens
// (`Widget::gesture`), genom bindningarna `Pinch` och `Twist` (förval: zoom
// och vridning kring PEKAREN — världspunkten under den står still, kartan
// vrids MED fingrarna; orbiten dollyar och vrider azimuten). Ritytan tar
// sekvensen vid Begin bara när den kan göra något med den, och en sekvens som
// VRED kartan snäpper när den släpps (`setRotationSnap`) — som Ctrl+draget.
// Varje steg som flyttade kameran är användarens rörelse (`onCameraChange`);
// inget animeras (regel 1) utom snäppets glidning.
bool Canvas::gesture(const Ctx& c, float x, float y, gfx::GesturePhase phase,
                     float scale, float rotationDeg)
{
    using P = gfx::GesturePhase;
    // Den VERKSAMMA raden för nypningen och vridningen. Slås upp på nytt EFTER
    // appens handler: handlern får ändra tabellerna
    // (`App::resetCanvasBindings`), och en pekare in i dem får inte överleva den.
    const auto bunden = [this](InputSource s) {
        const AppWindow*        f    = appWindow();
        const std::uint32_t     mods = f ? f->modifierKeys() : 0;
        const detail::InputMap& app  = detail::canvasInput();
        const std::string       läge = detail::groupMode(
            ownInput_.get(), app, activeInputMode(), autoInputMode(), s, mods);
        return detail::resolveEvent(ownInput_.get(), app, läge, s, mods,
                                    enabledActions(motionKind()));
    };
    if (phase == P::Begin) {
        // Ritytan tar sekvensen när den kan göra något med den — eller när
        // appen vill ha den (`onGesture`).
        if (!isEnabled() ||
            (!onGesture && !bunden(InputSource::Pinch) && !bunden(InputSource::Twist)))
            return false;   // bubblar — ingen kameraändring
        gestureTurned_   = false;
        gestureDecided_  = false;
        gestureLogScale_ = 0.0;
        gestureDegrees_  = 0.0;
        gestureDebt_     = 0.0;
        coasting_        = false;   // en ny gest fångar en kastad karta
    }
    // APPENS HANDLER FÖRST: ett steg den tar gör den inbyggda
    // gesten ingenting med. Efter en körd handler ritas RITYTAN om, inte
    // fönstret (`Widget::gesture`s regel).
    bool tagen = false;
    if (onGesture) {
        const gfx::Rectf box = resolve(c);
        GestureArgs      e;
        e.phase    = phase;
        e.scale    = scale;
        e.rotation = rotationDeg;
        e.local    = Point{Draw::localX(c, box, x), Draw::localY(c, box, y)};
        const HandlerGuard vakt(*this);
        onGesture(this, e);
        if (!vakt.ok())
            return true;   // ritytan riven eller lossad — fönstret frågar sin vakt
        tagen = e.handled;
        invalidate();
    }
    bool ändrad = false;
    if (!tagen && phase != P::Cancel && isEnabled()) {
        const detail::MotionKind k    = motionKind();
        const InputBinding*      nyp  = bunden(InputSource::Pinch);
        const InputBinding*      vrid = bunden(InputSource::Twist);
        // Det steget bär av det ritytan får göra — en obunden eller avstängd
        // källa räknas inte, och kan därför aldrig vinna beslutet.
        const double ln = nyp && std::isfinite(scale) && scale > 0.0f
                              ? std::log(static_cast<double>(scale))
                              : 0.0;
        const double gr = vrid && std::isfinite(rotationDeg)
                              ? static_cast<double>(rotationDeg)
                              : 0.0;
        double nypNu = 0.0, vridNu = 0.0;   // det som ska utföras nu
        if (!gestureDecided_) {
            // FÖRE BESLUTET hålls båda. När det faller utförs STEGET som
            // fattade det, och det som hölls av de föregående blir en SKULD som
            // betalas av med de följande stegen (`detail::catchUp`) — kartan
            // hinner ikapp utan hopp. En gest som släpps
            // obestämd gör ingenting: också det vore ett hopp.
            const std::optional<detail::GestureKind> val =
                detail::decideGesture(gestureLogScale_ + ln, gestureDegrees_ + gr);
            if (val) {
                gestureDecided_ = true;
                gestureZooms_   = *val == detail::GestureKind::Zoom;
                gestureDebt_    = gestureZooms_ ? gestureLogScale_ : gestureDegrees_;
                if (gestureZooms_)
                    nypNu = detail::catchUp(ln, gestureDebt_);
                else
                    vridNu = detail::catchUp(gr, gestureDebt_);
            } else {
                gestureLogScale_ += ln;
                gestureDegrees_ += gr;
            }
        } else if (gestureZooms_) {
            nypNu = detail::catchUp(ln, gestureDebt_);   // vridningen kastas
        } else {
            vridNu = detail::catchUp(gr, gestureDebt_);  // nypningen kastas
        }
        CameraMotion m;
        if (nypNu != 0.0)
            addMotion(m, *nyp, nypNu, k, c);
        if (vridNu != 0.0)
            addMotion(m, *vrid, vridNu, k, c);
        bool vred = false;
        ändrad    = applyMotion(c, m, x, y, false, gfx::Pointf{x, y},
                                gfx::Pointf{x, y}, true, &vred);
        if (vred)
            gestureTurned_ = true;
    }
    if (ändrad && fireCameraChange())
        invalidate();   // ritytan, inte fönstret (canvas.h)
    if (phase == P::End || phase == P::Cancel) {
        // Släppet: en sekvens som vred snäpper — men inte en som ritytan
        // inaktiverats under (mouseUp:s regel: inget glider ut då), och inte
        // ett släpp appen tog.
        if (!tagen && gestureTurned_ && rotatable_ && isEnabled())
            snapRotation();
        gestureTurned_ = false;
    }
    return true;
}

// Ett DUBBELKLICK — `onDoubleClick` med knappen. Ett tryck är en diskret
// händelse: fönstret ritas om efteråt, om ritytan lever.
void Canvas::fireDoubleClick(const Ctx& c, float x, float y, gfx::MouseButton b)
{
    if (!onDoubleClick)
        return;
    CanvasMouseArgs e = mouseArgs(c, x, y);
    e.button          = b;
    const HandlerGuard vakt(*this);
    onDoubleClick(this, e);
    if (!vakt.ok())
        return;
    if (AppWindow* w = appWindow())
        w->invalidate();
}

void Canvas::mouseDoubleClick(const Ctx& c, float x, float y)
{
    fireDoubleClick(c, x, y, gfx::MouseButton::Left);
}

// MITTENKNAPPEN: dragets bindningar, som vänsterknappens.
// Appens `onMouseDown` med `button` Middle har fönstret redan fyrat
// — ett tryck den TOG kommer aldrig hit. Trycket bubblar (widget.h), så
// ritytan TAR det bara när draget har en påslagen handling; annars går det
// vidare till förfäderna.
bool Canvas::middleDown(const Ctx& c, float x, float y)
{
    coasting_ = false;
    return beginDrag(c, gfx::MouseButton::Middle, x, y);
}

void Canvas::middleDrag(const Ctx& c, float x, float y)
{
    if (dragMoveHandler(c, x, y, gfx::MouseButton::Middle))
        moveDrag(c, gfx::MouseButton::Middle, x, y);
}

bool Canvas::middleUp(const Ctx& c, float, float)
{
    // Appens `onMouseUp` har redan fyrats och tog inte släppet.
    endDrag(c, gfx::MouseButton::Middle);
    return false;
}

// DUBBELKLICKET MED MITTENKNAPPEN — efter det andra trycket som ritytans
// gest tog. Ett tryck appen tog ger sitt dubbelklick genom `appPress`.
void Canvas::middleDoubleClick(const Ctx& c, float x, float y)
{
    fireDoubleClick(c, x, y, gfx::MouseButton::Middle);
}

// HÖGERKNAPPEN — som mittenknappen, men trycket tas BARA
// med en påslagen bindning för `RightDragX`/`RightDragY` i läget: utan en
// går det till förfäderna och snabbmenyn. Ett taget tryck öppnar
// ingen snabbmeny; den som vill ha en efter ett
// klick utan drag öppnar den själv i `onMouseUp`.
bool Canvas::rightDown(const Ctx& c, float x, float y)
{
    return beginDrag(c, gfx::MouseButton::Right, x, y);
}

void Canvas::rightDrag(const Ctx& c, float x, float y)
{
    if (dragMoveHandler(c, x, y, gfx::MouseButton::Right))
        moveDrag(c, gfx::MouseButton::Right, x, y);
}

bool Canvas::rightUp(const Ctx& c, float, float)
{
    endDrag(c, gfx::MouseButton::Right);
    return false;
}

void Canvas::rightDoubleClick(const Ctx& c, float x, float y)
{
    fireDoubleClick(c, x, y, gfx::MouseButton::Right);
}

// AVBROTTET — släppet kommer aldrig, och bokföringen är redan nollad
// (`pressed()`/`middlePressed()`/`rightPressed()` falska). Gesten STANNAR där
// den står: ingen `endDrag`, alltså ingen tröghet och inget vridsnäpp, och en
// kastad karta som redan glider ut fångas. Appens `onMouseCancel` fyrar
// fönstret efteråt, med pekarens senast kända läge.
bool Canvas::mouseCancel(gfx::MouseButton b)
{
    if (b == gfx::MouseButton::None)
        return false;
    DragState& d = dragOf(b);
    d.active     = false;
    d.samples.clear();
    if (b == gfx::MouseButton::Left)
        coasting_ = false;
    return false;
}

// ETT TRYCK APPEN TOG — ritytans egna apphändelser för det:
// rörelserna till `onMouseMove` (per rörelse, ingen full omritning —
// `mouseDrag`s regel) och dubbelklicket till `onDoubleClick` (ett tryck är en
// diskret händelse: fönstret ritas om), båda med knappen. Vid trycket fångas en
// kastad karta, med vänster- och mittenknappen.
void Canvas::appPress(const Ctx& c, float x, float y, gfx::MouseButton b,
                      AppPressStep steg)
{
    switch (steg) {
    case AppPressStep::Begin:
        if (b != gfx::MouseButton::Right)
            coasting_ = false;
        return;
    case AppPressStep::Move:
        if (onMouseMove) {
            CanvasMouseArgs e = mouseArgs(c, x, y);
            e.button          = b;
            onMouseMove(this, e);
        }
        return;
    case AppPressStep::DoubleClick:
        fireDoubleClick(c, x, y, b);
        return;
    }
}

gfx::PointerShape Canvas::cursorAt(const Ctx&, float, float) const
{
    // Medan ett drag pågår (läst ur trycken som fönstret håller, så ett
    // avbrutet drag släcker pekaren): ett drag som BARA förflyttar visar den
    // STÄNGDA HANDEN — man håller i innehållet och drar det — och ett som
    // vrider, tiltar eller zoomar flytta-pekaren. Över
    // en rityta med gester: hårkorset.
    if (const DragState* d = activeDrag())
        return d->pans && !d->others ? gfx::PointerShape::ClosedHand
                                     : gfx::PointerShape::Move;
    if (pannable_ || zoomable_ || rotatable_ || pitchable_)
        return gfx::PointerShape::Crosshair;
    return gfx::PointerShape::Arrow;
}

// En TAGEN tangent håller ritytan tickande så länge den hålls —
// fönstret ritar om efter en tagen tangent, och den ritningen postar den första
// ticken. `onKeyDown` är basens och går FÖRE `keyDown`
// nedan; fönstret berättar här att handlern tog tangenten.
void Canvas::keyTaken(gfx::Key k)
{
    if (std::find(tickKeys_.begin(), tickKeys_.end(), k) == tickKeys_.end())
        tickKeys_.push_back(k);
}

// RITYTANS EGNA TANGENTER — pilarna (`ArrowsX`/`ArrowsY`, ett steg) och plus
// och minus (`PlusMinus`) genom bindningarna. En tangent appens `onKeyDown`
// tog kommer aldrig hit (fönstret stoppar den före). Förvalen: pilarna
// panorerar ett stilsteg — världspunkten ett steg bort i pilens riktning
// hamnar i mitten, exakt också under tilt; i orbiten flyger upp/ner
// — Shift+pilarna vrider och tiltar (uppåt = mer tilt), och + och − zoomar
// kring mitten respektive dollyar mot målet.
bool Canvas::keyDown(gfx::Key k, std::uint32_t mods)
{
    const AppWindow* w = appWindow();
    if (!w || !w->isOpen() || !isEnabled())
        return false;

    InputSource s   = InputSource::ArrowsX;
    double      raw = 0.0;
    switch (k) {
    case gfx::Key::Left:  raw = -1.0; break;
    case gfx::Key::Right: raw = 1.0; break;
    case gfx::Key::Up:    s = InputSource::ArrowsY; raw = -1.0; break;
    case gfx::Key::Down:  s = InputSource::ArrowsY; raw = 1.0; break;
    case gfx::Key::Plus:
    case gfx::Key::Equals:   // + på en layout där plus är skiftat (gfx/events.h)
    case gfx::Key::KP_Add:
        s   = InputSource::PlusMinus;
        raw = 1.0;
        break;
    case gfx::Key::Minus:
    case gfx::Key::KP_Subtract:
        s   = InputSource::PlusMinus;
        raw = -1.0;
        break;
    default:
        return false;
    }
    const detail::MotionKind kind = motionKind();
    const detail::InputMap&  app  = detail::canvasInput();
    const std::string        läge = detail::groupMode(
        ownInput_.get(), app, activeInputMode(), autoInputMode(), s, mods);
    const InputBinding* b = detail::resolveEvent(ownInput_.get(), app, läge, s,
                                                 mods, enabledActions(kind));
    if (!b)
        return false;
    const Ctx        c   = behaviorCtx();
    const gfx::Rectf box = resolve(c);
    CameraMotion     m;
    addMotion(m, *b, raw, kind, c);
    if (applyMotion(c, m, box.x + box.w / 2.0, box.y + box.h / 2.0, true,
                    std::nullopt, std::nullopt, true))
        fireCameraChange();
    return true;
}

void Canvas::languageChanged()
{
    // Etiketter med ett översättnings-id har bytt text: fönstren bygger om
    // texten (formerna och körningarna står kvar).
    for (const auto& L : scene_->layers)
        detail::touchText(*L);
}

} // namespace glaze
