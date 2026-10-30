// Ritytans kameraräkning — se camera_math.h.

#include "camera_math.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include <gfx/events.h>   // gfx::kPrecisePxPerRow — styrplattans rader

#include "../ease.h"   // glidningarnas EN kurva (delad med dockningen)

namespace glaze::detail {

namespace {

// GESTERNAS TAL — MapLibres där det finns ett.
//
// Hjulet: en rad är en HALV zoomnivå (MapLibre ger ungefär det för ett
// mushjulssteg); tangenterna en hel.
constexpr double kWheelZoomLevels = 0.5;
constexpr double kKeyZoomLevels   = 1.0;
// Ctrl+drag (MapLibres mus-vridning och -tilt): 0,8° bäring och 0,5° tilt
// per logisk px.
constexpr float kDragBearingPerPx = 0.8f;
constexpr float kDragPitchPerPx   = 0.5f;
// Shift+pilar.
constexpr float kKeyBearingStep = 15.0f;
constexpr float kKeyPitchStep   = 10.0f;
// FÖRSTAPERSONENS STEG: en världsenhet per hjulsteg.
constexpr double kFirstPersonUnit = 1.0;
// VRIDSNÄPPET: förvalet är MapLibres bearingSnap — bara norr (ett steg på ett
// helt varv), inom 7° — och glidningen dit tar 200 ms. Ett steg är högst ett
// varv och toleransen högst ett halvt.
constexpr float kFullTurnDeg      = 360.0f;
constexpr float kHalfTurnDeg      = 180.0f;
constexpr float kSnapToleranceDeg = 7.0f;
constexpr int   kBearingSnapMs    = 200;
// ZOOM ELLER VRIDNING. Har bara EN sort kommit (Windows skickar bara den
// fingrarna valt, macOS skickar magnify och rotate var för sig) avgörs det
// när den nått 0,02 — ln 1,02 eller ≈ 1,1° — utan att vänta. Har båda kommit
// faller beslutet vid 0,08 (8 % zoom eller ≈ 4,6°), och vridningen måste då
// vara dubbelt så stor som zoomen: en nypning vrider nästan alltid lite
// (vid 0,05 och lika vikt avgörs det för snabbt och zoomen blir svår att få till).
constexpr double kGestureMin      = 0.02;
constexpr double kGestureDecide   = 0.08;
constexpr double kRotateDominance = 2.0;
// Ikapp: skulden betalas med lika mycket som steget (dubbel fart).
constexpr double kCatchUp = 1.0;
// Etiketternas tona in/ut.
constexpr int kLabelFadeMs = 200;
// Ticken: högst en tiondels sekund per tick.
constexpr double kTickMaxSeconds = 0.1;
constexpr double kMsPerSecond    = 1000.0;

// TRÖGHETEN. Farten mäts över rörelsens sista `kInertiaWindowMs` och bara om
// pekaren rörde sig inom `kInertiaIdleMs` före släppet (en hand som stannat
// och sedan släppt ska inte kasta kartan). Den klingar av med tidskonstanten
// `kInertiaTauMs` och tar slut när det som återstår är under en halv fysisk
// px, eller senast efter `kInertiaMaxMs`. Fartens gränser i LOGISKA px per
// ms: under den undre ingen tröghet, över den övre kapad (MapLibres 1400
// px/s).
constexpr double kInertiaWindowMs  = 100.0;
constexpr double kInertiaIdleMs    = 50.0;
constexpr double kInertiaTauMs     = 260.0;
constexpr double kInertiaMaxMs     = 1200.0;
constexpr double kInertiaMinSpeed  = 0.1;
constexpr double kInertiaMaxSpeed  = 1.4;
constexpr double kInertiaRestPx    = 0.5;
// Bufferns tak i antal — ett skydd, inte fönstret (som är tiden ovan).
constexpr std::size_t kMaxPanSamples = 512;

// 3D-KAMERAN. Klipplanen ur scenens låda i
// perspektiv: det bortre 5 % bortom det djupaste hörnet, närplanet en
// tiotusendel av det (D32F med standard-Z vill ha far/near ≤ 10⁴) men högst
// halva avståndet till målet.
constexpr double kFarSlack    = 1.05;
constexpr double kNearOverFar = 1e-4;
constexpr double kNearMaxPart = 0.5;
// Ortografiskt omsluter klipplanen djupen med den här andelen av spannet
// (minst av avståndet) till godo.
constexpr double kOrthoDepthSlack = 0.05;
// Ett satt närplan bortom det härledda bortre planet: det bortre på
// dubbla närplanets djup.
constexpr double kFarOverNear = 2.0;
// Elevationens förvalda spärr.
constexpr float kElevationLimit = 85.0f;
// Inramningen håller varje hörn minst halva avståndet framför ögat, alltså
// bortom det närmaste närplan ett härlett plan kan få: avståndet är minst
// så här många gånger det längsta ett hörn sticker ut mot ögat från målet.
constexpr double kFitFront = 2.0;
// SÅ NÄRA RAKT UPP- ELLER NEDÅT (blickens horisontella del, av en
// enhetsvektor) bär upp-vektorn kompassriktningen i stället för blicken —
// kameran vriden som en vy (`orientedAs`: rollen blir då 0 och inte en
// vridning som tar ut en godtycklig azimut) och förstapersonens gång framåt
// (`moveEye`).
constexpr double kStraightDown = 1e-9;

double radians(double deg)
{
    return deg * std::numbers::pi / 180.0;
}

Vec3 add(Vec3 a, Vec3 b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 sub(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 mul(Vec3 a, double s)
{
    return {a.x * s, a.y * s, a.z * s};
}

double dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

Vec3 fromPoint(Point3 p)
{
    return {p.x, p.y, p.z};
}

gfx::Vec3d toGfx(Vec3 a)
{
    return {a.x, a.y, a.z};
}

// Kompassramen (öster, norr, upp) → världen. Med Z upp är den rak; med Y
// upp är norr −z och upp +y — en ROTATION och inte en spegling, så världen
// är högerhänt i båda.
Vec3 fromCompass(UpAxis up, double e, double n, double u)
{
    return up == UpAxis::Y ? Vec3{e, u, -n} : Vec3{e, n, u};
}

} // namespace

gfx::MapView mapView(const Camera& cam, bool yUp, float maxPitch,
                     const gfx::Rectf& box, float scale)
{
    gfx::MapView v;
    v.cx         = cam.center.x;
    v.cy         = cam.center.y;
    v.pxPerUnit  = cam.zoom * scale;
    v.bearingDeg = cam.bearing;
    v.pitchDeg   = std::clamp(cam.pitch, 0.0f, maxPitch);
    v.yUp        = yUp;
    v.box        = box;
    return v;
}

gfx::MapView localView(const gfx::Rectf& box, float scale)
{
    gfx::MapView v;
    v.cx        = box.w / scale / 2.0;
    v.cy        = box.h / scale / 2.0;
    v.pxPerUnit = scale;
    v.box       = box;
    return v;
}

float wrapBearing(float deg)
{
    if (!std::isfinite(deg))
        return 0.0f;
    double d = std::fmod(static_cast<double>(deg), 360.0);
    if (d <= -180.0)
        d += 360.0;
    if (d > 180.0)
        d -= 360.0;
    return static_cast<float>(d);
}

float easeBearing(float from, float to, double t)
{
    const float db = wrapBearing(to - from);
    return wrapBearing(static_cast<float>(from + db * easeInOut(t)));
}

Camera easeCamera(const Camera& from, const Camera& to, double t)
{
    const double e = easeInOut(t);
    Camera c;
    c.center.x = from.center.x + (to.center.x - from.center.x) * e;
    c.center.y = from.center.y + (to.center.y - from.center.y) * e;
    c.zoom     = from.zoom * std::pow(to.zoom / from.zoom, e);
    const float db = wrapBearing(to.bearing - from.bearing);
    c.bearing      = wrapBearing(static_cast<float>(from.bearing + db * e));
    c.pitch = static_cast<float>(from.pitch + (to.pitch - from.pitch) * e);
    return c;
}

double fitZoom(double w, double h, double bearingDeg, double aw, double ah)
{
    const double b  = bearingDeg * std::numbers::pi / 180.0;
    const double ew = std::abs(w) * std::abs(std::cos(b)) +
                      std::abs(h) * std::abs(std::sin(b));
    const double eh = std::abs(w) * std::abs(std::sin(b)) +
                      std::abs(h) * std::abs(std::cos(b));
    if (!(ew > 0.0) && !(eh > 0.0))
        return 0.0;
    double z = ew > 0.0 ? aw / ew : ah / eh;
    if (eh > 0.0)
        z = std::min(z, ah / eh);
    return z;
}

// --------------------------------------------------------------- projektionen

Projection::Projection(const gfx::MapView& v)
    : v_(v), m_(gfx::viewProjection(v, v.cx, v.cy)), box_(v.box), ox_(v.cx),
      oy_(v.cy), oz_(0.0), map_(true)
{
}

Projection::Projection(const gfx::View3D& v)
    : v3_(v), m_(gfx::viewProjection(v, v.eye.x, v.eye.y, v.eye.z)),
      box_(v.box), ox_(v.eye.x), oy_(v.eye.y), oz_(v.eye.z), map_(false)
{
}

bool Projection::ground(double px, double py, double& wx, double& wy) const
{
    if (map_)
        return gfx::unproject(v_, px, py, wx, wy);
    gfx::Ray3d ray;
    gfx::Vec3d hit;
    if (!gfx::rayThrough(v3_, px, py, ray) ||
        !gfx::intersect(ray, gfx::Plane3d{}, hit))
        return false;
    wx = hit.x;
    wy = hit.y;
    return true;
}

double Projection::groundScale(double px, double py) const
{
    if (map_)
        return gfx::pxPerUnitAt(v_, px, py);
    double wx = 0.0, wy = 0.0;
    if (!ground(px, py, wx, wy))
        return 0.0;
    return gfx::pxPerUnitAt(v3_, gfx::Vec3d{wx, wy, 0.0});
}

void Projection::clip(double wx, double wy, double wz, double out[4]) const
{
    m_.transform(wx - ox_, wy - oy_, wz - oz_, 1.0, out);
}

bool Projection::toScreen(double wx, double wy, double wz, double& sx,
                          double& sy) const
{
    double c[4];
    clip(wx, wy, wz, c);
    // Programmens gallring: bakom närplanet (klippdjup < 0) eller w ≤ 0.
    if (!(c[2] >= 0.0) || !(c[3] > 1e-6))
        return false;
    sx = c[0] / c[3];
    sy = c[1] / c[3];
    return std::isfinite(sx) && std::isfinite(sy);
}

bool Projection::direction(double wx, double wy, double wz, double dx,
                           double dy, double dz, double& ex, double& ey) const
{
    double c[4], d[4];
    clip(wx, wy, wz, c);
    if (!(c[2] >= 0.0) || !(c[3] > 1e-6))
        return false;
    m_.transform(dx, dy, dz, 0.0, d);
    // d(X/W) = (dX · W − X · dW) / W² — samma derivata som programmet.
    const double sx = (d[0] * c[3] - c[0] * d[3]) / (c[3] * c[3]);
    const double sy = (d[1] * c[3] - c[1] * d[3]) / (c[3] * c[3]);
    const double l  = std::hypot(sx, sy);
    if (!(l > 1e-12))
        return false;
    ex = sx / l;
    ey = sy / l;
    return true;
}

// ---------------------------------------------------------------- gesterna

double zoomLevelsFactor(double levels)
{
    return std::isfinite(levels) ? std::exp2(levels) : 1.0;
}

double dollyLevelsFactor(double levels)
{
    return 1.0 / zoomLevelsFactor(levels);
}

double wheelZoomFactor(float rows)
{
    return zoomLevelsFactor(static_cast<double>(rows) * kWheelZoomLevels);
}

double keyZoomFactor(bool in)
{
    return zoomLevelsFactor(in ? kKeyZoomLevels : -kKeyZoomLevels);
}

float dragBearingDelta(float dxLogical)
{
    return dxLogical * kDragBearingPerPx;
}

float dragPitchDelta(float dyLogical)
{
    return -dyLogical * kDragPitchPerPx;   // uppåt (y minskar) = mer tilt
}

float keyBearingStep()
{
    return kKeyBearingStep;
}

float keyPitchStep()
{
    return kKeyPitchStep;
}

float orbitAzimuthDelta(float dxLogical)
{
    return dragBearingDelta(dxLogical);
}

float orbitElevationDelta(float dyLogical)
{
    return -dragPitchDelta(dyLogical);   // nedåt (y ökar) = högre elevation
}

double wheelDollyFactor(float rows)
{
    return dollyLevelsFactor(static_cast<double>(rows) * kWheelZoomLevels);
}

double keyDollyFactor(bool in)
{
    return dollyLevelsFactor(in ? kKeyZoomLevels : -kKeyZoomLevels);
}

// ------------------------------------------------- bindningarnas mängd

namespace {

// Källor vars råa värde är LOGISKA px på skärmen — dragen och styrplattans
// tvåfingersdrag.
bool ärPx(InputSource s)
{
    switch (s) {
    case InputSource::LeftDragX:
    case InputSource::LeftDragY:
    case InputSource::MiddleDragX:
    case InputSource::MiddleDragY:
    case InputSource::RightDragX:
    case InputSource::RightDragY:
    case InputSource::ScrollX:
    case InputSource::ScrollY:
        return true;
    default:
        return false;
    }
}

bool ärTangent(InputSource s)
{
    return s == InputSource::ArrowsX || s == InputSource::ArrowsY ||
           s == InputSource::PlusMinus;
}

// Källans värde i HJULSTEG — den gemensamma valutan.
double steg(InputSource s, double raw, float wheelStepPx)
{
    if (ärPx(s))
        return wheelStepPx > 0.0f ? raw / wheelStepPx : 0.0;
    if (s == InputSource::Pinch)
        return raw / std::log(wheelZoomFactor(1.0f));
    if (s == InputSource::Twist)
        return raw / kKeyBearingStep;
    return raw;   // hjulets rader och tangenternas steg
}

} // namespace

double bindingAmount(InputAction a, InputSource s, double raw, MotionKind k,
                     float wheelStepPx, float panStepPx)
{
    if (!std::isfinite(raw))
        return 0.0;
    switch (a) {
    case InputAction::PanX:
    case InputAction::PanY:
    case InputAction::Fly:
    case InputAction::Lift:
        // Förstapersonen: en världsenhet per steg.
        if (k == MotionKind::FirstPerson)
            return steg(s, raw, wheelStepPx) * kFirstPersonUnit;
        return ärPx(s) ? raw : steg(s, raw, wheelStepPx) * panStepPx;
    case InputAction::Zoom:
        if (s == InputSource::Pinch)
            return raw / std::numbers::ln2;
        if (ärTangent(s))
            return raw * kKeyZoomLevels;
        return steg(s, raw, wheelStepPx) * kWheelZoomLevels;
    case InputAction::Rotate:
        if (ärPx(s))
            return raw * kDragBearingPerPx;
        if (s == InputSource::Twist)
            return raw;
        if (ärTangent(s))
            return raw * kKeyBearingStep;
        return steg(s, raw, wheelStepPx) * kKeyBearingStep;
    case InputAction::Tilt:
        if (ärPx(s))
            return raw * kDragPitchPerPx;
        if (s == InputSource::Twist)
            return raw;
        if (ärTangent(s))
            return raw * kKeyPitchStep;
        return steg(s, raw, wheelStepPx) * kKeyPitchStep;
    case InputAction::None:
        break;
    }
    return 0.0;
}

double tickSeconds(std::uint64_t fromMs, std::uint64_t toMs)
{
    if (toMs <= fromMs)
        return 0.0;
    return std::min(static_cast<double>(toMs - fromMs) / kMsPerSecond,
                    kTickMaxSeconds);
}

std::optional<float> snapBearing(float bearingDeg, float stepDeg,
                                 float toleranceDeg)
{
    if (!(stepDeg > 0.0f) || !std::isfinite(bearingDeg))
        return std::nullopt;   // av
    // Vinkeln MODULO ett varv, i [0, 360): 268° och −92° är samma bäring.
    double b = std::fmod(static_cast<double>(bearingDeg), kFullTurnDeg);
    if (b < 0.0)
        b += kFullTurnDeg;
    // De två snäppvinklarna runt bäringen. Den övre är aldrig bortom ett helt
    // varv — 360 är norr igen, också när steget inte delar varvet jämnt.
    const double steg = stepDeg;
    const double lo   = std::floor(b / steg) * steg;
    const double hi   = std::min(lo + steg, static_cast<double>(kFullTurnDeg));
    const double mål  = b - lo <= hi - b ? lo : hi;
    const double fel  = std::abs(b - mål);
    if (fel > toleranceDeg || fel == 0.0)
        return std::nullopt;   // utanför fångstzonen, eller redan där
    return wrapBearing(static_cast<float>(mål));
}

int bearingSnapMs()
{
    return kBearingSnapMs;
}

float defaultRotationSnap()
{
    return kFullTurnDeg;
}

float defaultRotationSnapTolerance()
{
    return kSnapToleranceDeg;
}

bool validRotationSnap(float stepDeg)
{
    return std::isfinite(stepDeg) && stepDeg >= 0.0f && stepDeg <= kFullTurnDeg;
}

bool validRotationSnapTolerance(float toleranceDeg)
{
    return std::isfinite(toleranceDeg) && toleranceDeg >= 0.0f &&
           toleranceDeg <= kHalfTurnDeg;
}

namespace {
// Gestens två mått i samma enhet: |ln s| och |vinkeln i radianer|.
void gestureAmounts(double logScale, double rotationDeg, double& zoom, double& vrid)
{
    zoom = std::isfinite(logScale) ? std::abs(logScale) : 0.0;
    vrid = std::isfinite(rotationDeg) ? std::abs(rotationDeg) * std::numbers::pi / 180.0
                                      : 0.0;
}
} // namespace

double catchUp(double step, double& debt)
{
    const double extra =
        std::copysign(std::min(std::abs(debt), kCatchUp * std::abs(step)), debt);
    debt -= extra;
    return step + extra;
}

GestureKind dominantGesture(double logScale, double rotationDeg)
{
    double zoom = 0.0, vrid = 0.0;
    gestureAmounts(logScale, rotationDeg, zoom, vrid);
    return vrid > 0.0 && vrid >= kRotateDominance * zoom ? GestureKind::Rotate
                                                         : GestureKind::Zoom;
}

std::optional<GestureKind> decideGesture(double logScale, double rotationDeg)
{
    double zoom = 0.0, vrid = 0.0;
    gestureAmounts(logScale, rotationDeg, zoom, vrid);
    if (!(std::max(zoom, vrid) >= kGestureMin))
        return std::nullopt;
    if (vrid == 0.0)
        return GestureKind::Zoom;     // bara zoom har kommit
    if (zoom == 0.0)
        return GestureKind::Rotate;   // bara vridning har kommit
    if (!(std::max(zoom, vrid) >= kGestureDecide))
        return std::nullopt;
    return dominantGesture(logScale, rotationDeg);
}

void preciseScroll(float wheelX, float wheelY, double& dx, double& dy)
{
    const double px = static_cast<double>(gfx::kPrecisePxPerRow);
    dx = -static_cast<double>(wheelX) * px;
    dy = static_cast<double>(wheelY) * px;
}

void precisePanPx(float wheelX, float wheelY, float scale, double& dx,
                  double& dy)
{
    preciseScroll(wheelX, wheelY, dx, dy);
    dx *= scale;
    dy *= scale;
}

int labelFadeMs()
{
    return kLabelFadeMs;
}

void addPanSample(std::vector<PanSample>& samples, const PanSample& s)
{
    samples.push_back(s);
    // Det äldsta som behövs är det SISTA samplet som ligger minst ett
    // fönster bak — så att fönstret alltid kan täckas helt.
    std::size_t släpp = 0;
    while (släpp + 1 < samples.size() &&
           static_cast<double>(s.ms - samples[släpp + 1].ms) >= kInertiaWindowMs)
        ++släpp;
    if (samples.size() - släpp > kMaxPanSamples)
        släpp = samples.size() - kMaxPanSamples;
    samples.erase(samples.begin(),
                  samples.begin() + static_cast<std::ptrdiff_t>(släpp));
}

bool inertiaStart(std::span<const PanSample> samples, std::uint64_t releaseMs,
                  float scale, InertiaPlan& out)
{
    out = InertiaPlan{};
    if (samples.size() < 2 || !(scale > 0.0f))
        return false;
    const PanSample& last = samples.back();
    if (releaseMs < last.ms ||
        static_cast<double>(releaseMs - last.ms) > kInertiaIdleMs)
        return false;   // handen stannade innan den släppte
    // Det äldsta samplet inom fönstret före det sista.
    std::size_t i = samples.size() - 1;
    while (i > 0 &&
           static_cast<double>(last.ms - samples[i - 1].ms) <= kInertiaWindowMs)
        --i;
    const PanSample& first = samples[i];
    const double     dt    = static_cast<double>(last.ms - first.ms);
    if (!(dt > 0.0))
        return false;
    double vx = (last.x - first.x) / dt;
    double vy = (last.y - first.y) / dt;
    const double speed = std::hypot(vx, vy);
    if (!(speed >= kInertiaMinSpeed * scale))
        return false;
    if (speed > kInertiaMaxSpeed * scale) {
        vx *= kInertiaMaxSpeed * scale / speed;
        vy *= kInertiaMaxSpeed * scale / speed;
    }
    // Hela sträckan är v · τ; slutet när resten understiger en halv px.
    const double total = std::hypot(vx, vy) * kInertiaTauMs;
    if (!(total > kInertiaRestPx))
        return false;
    out.vx         = vx;
    out.vy         = vy;
    out.durationMs = std::min(kInertiaTauMs * std::log(total / kInertiaRestPx),
                              kInertiaMaxMs);
    return out.durationMs > 0.0;
}

void inertiaOffset(const InertiaPlan& p, double tMs, double& dx, double& dy)
{
    // v · τ · (1 − e^(−t/τ)) / (1 − e^(−T/τ)): normerat så att HELA
    // sträckan v · τ nås exakt vid slutet T — trögheten stannar på en
    // bestämd punkt i stället för att krypa sina sista bråkdelar av en px.
    const double T    = std::max(p.durationMs, 0.0);
    const double norm = 1.0 - std::exp(-T / kInertiaTauMs);
    const double t    = std::clamp(tMs, 0.0, T);
    const double k    = norm > 0.0 ? kInertiaTauMs *
                                         (1.0 - std::exp(-t / kInertiaTauMs)) /
                                         norm
                                   : 0.0;
    dx = p.vx * k;
    dy = p.vy * k;
}

// ---------------------------------------------------------------- 3D-kameran

bool validCamera3D(const Camera3D& c)
{
    const auto ändligt = [](double x) { return std::isfinite(x); };
    if (!ändligt(c.target.x) || !ändligt(c.target.y) || !ändligt(c.target.z) ||
        !ändligt(c.azimuth) || !ändligt(c.elevation) || !ändligt(c.roll))
        return false;
    if (!(c.distance > 0.0) || !ändligt(c.distance))
        return false;
    if (!(c.fov > 0.0f) || !(c.fov < kHalfTurnDeg))
        return false;
    if (!(c.orthoHeight >= 0.0) || !ändligt(c.orthoHeight) ||
        !(c.nearPlane >= 0.0) || !ändligt(c.nearPlane) ||
        !(c.farPlane >= 0.0) || !ändligt(c.farPlane))
        return false;
    if (c.nearPlane > 0.0 && c.farPlane > 0.0 && !(c.farPlane > c.nearPlane))
        return false;
    return (c.projection == glaze::Projection::Perspective ||
            c.projection == glaze::Projection::Orthographic) &&
           (c.up == UpAxis::Z || c.up == UpAxis::Y);
}

CameraBasis cameraBasis(const Camera3D& c)
{
    const double az = radians(c.azimuth);
    const double el = radians(c.elevation);
    const double ro = radians(c.roll);
    // Blickens kompassriktning i horisontalplanet: sin(az) öster, cos(az)
    // norr. Blicken lutar elevationen nedåt, och upp är elevationens TANGENT
    // (−∂blick/∂el): den vänder kontinuerligt förbi polen i stället för att
    // vika, som en upp-vektor ur världens z hade gjort.
    const double he = std::sin(az);
    const double hn = std::cos(az);
    const double ce = std::cos(el);
    const double se = std::sin(el);
    const Vec3   f  = fromCompass(c.up, he * ce, hn * ce, -se);
    const Vec3   u0 = fromCompass(c.up, he * se, hn * se, ce);
    const Vec3   r0 = cross(f, u0);
    // Rollen vrider kameran medurs kring blicken: upp lutar mot höger.
    const double cr = std::cos(ro);
    const double sr = std::sin(ro);
    return {f, sub(mul(r0, cr), mul(u0, sr)), add(mul(u0, cr), mul(r0, sr))};
}

gfx::View3D view3D(const Camera3D& cam, const gfx::Rectf& box,
                   const std::optional<Box3>& scene)
{
    const CameraBasis b = cameraBasis(cam);
    const Vec3        eye =
        sub(fromPoint(cam.target), mul(b.forward, cam.distance));
    gfx::View3D v;
    v.eye     = toGfx(eye);
    v.forward = toGfx(b.forward);
    v.up      = toGfx(b.up);
    v.box     = box;
    const double fov = radians(cam.fov);
    const bool   persp = cam.projection == glaze::Projection::Perspective;
    if (persp) {
        v.projection = gfx::Projection3D::Perspective;
        v.fovY       = fov;
    } else {
        v.projection      = gfx::Projection3D::Orthographic;
        v.orthoHalfHeight = cam.orthoHeight > 0.0
                                ? cam.orthoHeight
                                : cam.distance * std::tan(fov / 2.0);
    }

    // Djupen längs blicken som klipplanen ska rymma: målets (= avståndet)
    // och lådans hörn, relativt ögat i double.
    double dMin = cam.distance;
    double dMax = cam.distance;
    if (scene && scene->min.x <= scene->max.x &&
        scene->min.y <= scene->max.y && scene->min.z <= scene->max.z) {
        for (int i = 0; i < 8; ++i) {
            const Vec3 hörn{(i & 1) ? scene->max.x : scene->min.x,
                            (i & 2) ? scene->max.y : scene->min.y,
                            (i & 4) ? scene->max.z : scene->min.z};
            const double d = dot(sub(hörn, eye), b.forward);
            dMin           = std::min(dMin, d);
            dMax           = std::max(dMax, d);
        }
    }
    double nearZ, farZ;
    if (persp) {
        farZ  = cam.farPlane > 0.0 ? cam.farPlane : kFarSlack * dMax;
        nearZ = cam.nearPlane > 0.0
                    ? cam.nearPlane
                    : std::min(farZ * kNearOverFar,
                               cam.distance * kNearMaxPart);
    } else {
        const double pad =
            kOrthoDepthSlack * std::max(dMax - dMin, cam.distance);
        farZ  = cam.farPlane > 0.0 ? cam.farPlane : dMax + pad;
        nearZ = cam.nearPlane > 0.0 ? cam.nearPlane : dMin - pad;
    }
    // Ett satt plan som krockar med ett härlett: det härledda flyttas.
    if (!(farZ > nearZ)) {
        if (cam.farPlane > 0.0)
            nearZ = farZ - cam.distance;
        else
            farZ = nearZ * kFarOverNear;
    }
    v.nearZ = nearZ;
    v.farZ  = farZ;
    return v;
}

gfx::MapView mapViewAt(const Camera3D& cam, const gfx::Rectf& box, bool yUp)
{
    // Skalan vid målet beror inte på klipplanen — vyn utan scenens låda
    // räcker.
    const gfx::View3D v = view3D(cam, box, std::nullopt);
    gfx::MapView      m;
    m.cx        = cam.target.x;
    m.cy        = cam.target.y;
    m.pxPerUnit = gfx::pxPerUnitAt(
        v, gfx::Vec3d{cam.target.x, cam.target.y, cam.target.z});
    m.yUp = yUp;
    m.box = box;
    return m;
}

gfx::MapView mapViewAtView(const gfx::View3D& v, const std::optional<Box3>& scene,
                           bool yUp)
{
    const double fl = std::sqrt(v.forward.x * v.forward.x + v.forward.y * v.forward.y +
                                v.forward.z * v.forward.z);
    gfx::MapView m;
    m.yUp = yUp;
    m.box = v.box;
    if (!(fl > 0.0))
        return m;   // en oanvändbar vy: skalan 0
    const gfx::Vec3d dir{v.forward.x / fl, v.forward.y / fl, v.forward.z / fl};
    // Djupet längs blicken: klipplanens mitt, eller lådans mitt när den ligger
    // mellan dem.
    double depth = v.nearZ > 0.0 && v.farZ > v.nearZ
                       ? std::sqrt(v.nearZ * v.farZ)
                       : (v.nearZ + v.farZ) / 2.0;
    if (scene && scene->min.x <= scene->max.x && scene->min.y <= scene->max.y &&
        scene->min.z <= scene->max.z) {
        const double d = ((scene->min.x + scene->max.x) / 2.0 - v.eye.x) * dir.x +
                         ((scene->min.y + scene->max.y) / 2.0 - v.eye.y) * dir.y +
                         ((scene->min.z + scene->max.z) / 2.0 - v.eye.z) * dir.z;
        if (d >= v.nearZ && d <= v.farZ)
            depth = d;
    }
    const gfx::Vec3d p{v.eye.x + dir.x * depth, v.eye.y + dir.y * depth,
                       v.eye.z + dir.z * depth};
    m.cx        = p.x;
    m.cy        = p.y;
    m.pxPerUnit = gfx::pxPerUnitAt(v, p);
    return m;
}

gfx::Vec3d axisPoint(const gfx::View3D& v, double depth)
{
    const double fl = std::sqrt(v.forward.x * v.forward.x + v.forward.y * v.forward.y +
                                v.forward.z * v.forward.z);
    const double k  = fl > 0.0 ? depth / fl : 0.0;
    return {v.eye.x + v.forward.x * k, v.eye.y + v.forward.y * k,
            v.eye.z + v.forward.z * k};
}

Camera3D orbitCamera3D(const Camera3D& cam, float dAzimuth, float dElevation,
                       bool tumble, float minElevation, float maxElevation)
{
    Camera3D c = cam;
    if (std::isfinite(dAzimuth))
        c.azimuth = wrapBearing(c.azimuth + dAzimuth);
    if (std::isfinite(dElevation)) {
        const float el = c.elevation + dElevation;
        if (tumble) {
            c.elevation = wrapBearing(el);   // samma normalform som bäringen
        } else {
            if (minElevation > maxElevation)
                std::swap(minElevation, maxElevation);
            // Inget hopp in i spannet från en elevation utanför det.
            c.elevation = std::clamp(el, std::min(minElevation, cam.elevation),
                                     std::max(maxElevation, cam.elevation));
        }
    }
    return c;
}

float defaultElevationMin()
{
    return -kElevationLimit;
}

float defaultElevationMax()
{
    return kElevationLimit;
}

bool validElevationLimit(float deg)
{
    return std::isfinite(deg) && std::abs(deg) <= kHalfTurnDeg;
}

bool panCamera3D(Camera3D& cam, const gfx::Rectf& box, double fromX,
                 double fromY, double toX, double toY, PanVertical vertical)
{
    // Klipplanen hör inte till handens rörelse: med de härledda ligger
    // strålarnas början alltid framför målplanet (ortografiskt närplanet).
    Camera3D utanPlan  = cam;
    utanPlan.nearPlane = 0.0;
    utanPlan.farPlane  = 0.0;
    const gfx::View3D  v = view3D(utanPlan, box, std::nullopt);
    const CameraBasis  b = cameraBasis(cam);
    const gfx::Plane3d målplan{toGfx(fromPoint(cam.target)), toGfx(b.forward)};
    gfx::Ray3d         från, till;
    gfx::Vec3d         a, c;
    if (!gfx::rayThrough(v, fromX, fromY, från) ||
        !gfx::rayThrough(v, toX, toY, till) ||
        !gfx::intersect(från, målplan, a) || !gfx::intersect(till, målplan, c))
        return false;
    // Flyttas kameran med a − c ligger a där c låg: under (toX, toY). Båda
    // ligger i målplanet, så planet glider i sig självt.
    const Vec3 d{a.x - c.x, a.y - c.y, a.z - c.z};
    Vec3       steg = d;
    if (vertical != PanVertical::Plane) {
        // Sidled är målplanets (kamerans höger), och den LODRÄTA
        // delen — längs kamerans upp i planet, positiv när draget går nedåt —
        // byter riktning: blicken (Fly) eller världens upp-axel (Lift), med
        // samma belopp. Den vågräta delen är exakt planets, så punkten under
        // pekaren följer fortfarande i sidled.
        const double sidled = dot(d, b.right);
        const double lodrät = dot(d, b.up);
        const Vec3   riktning = vertical == PanVertical::Fly
                                    ? b.forward
                                    : fromCompass(cam.up, 0.0, 0.0, 1.0);
        steg = add(mul(b.right, sidled), mul(riktning, lodrät));
    }
    cam.target.x += steg.x;
    cam.target.y += steg.y;
    cam.target.z += steg.z;
    return true;
}

Camera3D dollyCamera3D(const Camera3D& cam, double factor, double minDistance,
                       double maxDistance)
{
    Camera3D c = cam;
    if (!(factor > 0.0) || !std::isfinite(factor))
        return c;
    double d = std::max(cam.distance * factor,
                        std::numeric_limits<double>::min());
    // Gränserna vidgade till det nuvarande avståndet — inget hopp in i
    // spannet från ett avstånd utanför det.
    if (maxDistance > 0.0)
        d = std::min(d, std::max(maxDistance, cam.distance));
    if (minDistance > 0.0)
        d = std::max(d, std::min(minDistance, cam.distance));
    if (cam.orthoHeight > 0.0)
        c.orthoHeight = cam.orthoHeight * (d / cam.distance);
    c.distance = d;
    return c;
}

Camera3D lookAround(const Camera3D& cam, float dYaw, float dTilt, float minEl,
                    float maxEl)
{
    // Vinklarna som orbitens (samma spärr, aldrig ett hopp in i spannet) —
    // men det är ÖGAT som står kvar: målet läggs framför det på samma avstånd.
    Camera3D     c   = orbitCamera3D(cam, dYaw, dTilt, false, minEl, maxEl);
    const Point3 öga = cam.eye();
    const Vec3   f   = cameraBasis(c).forward;
    c.target = {öga.x + f.x * c.distance, öga.y + f.y * c.distance,
                öga.z + f.z * c.distance};
    return c;
}

void moveEye(Camera3D& cam, double forward, double right, double lift,
             double rise)
{
    const CameraBasis b  = cameraBasis(cam);
    const Vec3        up = fromCompass(cam.up, 0.0, 0.0, 1.0);
    // Blickens VÅGRÄTA del — gången framåt längs marken. Rakt ned- eller
    // uppåt är den noll, och då bär upp-vektorn riktningen (orientedAs regel).
    Vec3   fram = sub(b.forward, mul(up, dot(b.forward, up)));
    double l    = std::sqrt(dot(fram, fram));
    if (!(l > kStraightDown)) {
        fram = sub(b.up, mul(up, dot(b.up, up)));
        l    = std::sqrt(dot(fram, fram));
    }
    if (l > 0.0)
        fram = mul(fram, 1.0 / l);
    const Vec3 steg = add(add(mul(fram, forward), mul(b.right, right)),
                          add(mul(up, lift), mul(b.up, rise)));
    cam.target.x += steg.x;
    cam.target.y += steg.y;
    cam.target.z += steg.z;
}

Camera3D easeCamera3D(const Camera3D& from, const Camera3D& to, double t)
{
    if (t >= 1.0)
        return to;
    const double e = easeInOut(t);
    Camera3D     c = from;
    c.target.x  = from.target.x + (to.target.x - from.target.x) * e;
    c.target.y  = from.target.y + (to.target.y - from.target.y) * e;
    c.target.z  = from.target.z + (to.target.z - from.target.z) * e;
    c.distance  = from.distance * std::pow(to.distance / from.distance, e);
    c.azimuth   = wrapBearing(static_cast<float>(
        from.azimuth + wrapBearing(to.azimuth - from.azimuth) * e));
    c.roll      = wrapBearing(static_cast<float>(
        from.roll + wrapBearing(to.roll - from.roll) * e));
    c.elevation = static_cast<float>(from.elevation +
                                     (to.elevation - from.elevation) * e);
    c.fov       = static_cast<float>(from.fov + (to.fov - from.fov) * e);
    // Matchad i båda ändar förblir matchad (den följer avståndet och
    // synfältet ovan); annars halvhöjderna som de faktiskt är, geometriskt.
    if (from.orthoHeight > 0.0 || to.orthoHeight > 0.0) {
        const auto halv = [](const Camera3D& k) {
            return k.orthoHeight > 0.0
                       ? k.orthoHeight
                       : k.distance * std::tan(radians(k.fov) / 2.0);
        };
        c.orthoHeight = halv(from) * std::pow(halv(to) / halv(from), e);
    }
    return c;
}

Camera3D fitBox(const Camera3D& cam, const Box3& b, const gfx::Rectf& box,
                double padPx)
{
    if (!(b.min.x <= b.max.x) || !(b.min.y <= b.max.y) || !(b.min.z <= b.max.z))
        return cam;   // omvänd (eller NaN) — ingen låda att rama in
    Camera3D c = cam;
    c.target   = {(b.min.x + b.max.x) / 2.0, (b.min.y + b.max.y) / 2.0,
                  (b.min.z + b.max.z) / 2.0};
    // Det som är kvar av rutan efter luften, som andel av halva rutan.
    const double pad = std::max(padPx, 0.0);
    const double sx  = (box.w - 2.0 * pad) / box.w;
    const double sy  = (box.h - 2.0 * pad) / box.h;
    if (!(sx > 0.0) || !(sy > 0.0))
        return c;   // luften äter rutan — bara målet flyttas
    const CameraBasis k = cameraBasis(cam);
    // Per hörn relativt mitten: sidled (höger, upp) och hur långt det
    // sticker ut mot ögat.
    double hx = 0.0, hy = 0.0, front = 0.0;
    double behövt = 0.0;   // perspektivets avstånd
    const double ty = std::tan(radians(cam.fov) / 2.0);
    const double tx = ty * (static_cast<double>(box.w) / box.h);
    for (int i = 0; i < 8; ++i) {
        const Vec3 q{((i & 1) ? b.max.x : b.min.x) - c.target.x,
                     ((i & 2) ? b.max.y : b.min.y) - c.target.y,
                     ((i & 4) ? b.max.z : b.min.z) - c.target.z};
        const double qr = std::abs(dot(q, k.right));
        const double qu = std::abs(dot(q, k.up));
        const double qf = dot(q, k.forward);
        hx     = std::max(hx, qr);
        hy     = std::max(hy, qu);
        front  = std::max(front, -qf);
        // Hörnet ryms när |q·höger| ≤ (avståndet + q·blicken) · tan × andel,
        // och likadant uppåt.
        behövt =
            std::max(behövt, std::max(qr / (tx * sx), qu / (ty * sy)) - qf);
    }
    if (!(hx > 0.0) && !(hy > 0.0) && !(front > 0.0))
        return c;   // en punkt — inget avstånd att räkna fram
    if (cam.projection == glaze::Projection::Perspective) {
        c.distance = std::max(behövt, kFitFront * front);
    } else {
        // Halvhöjden som rymmer hörnen uppåt och — genom bildförhållandet —
        // åt sidan. Ögats läge spelar ingen roll ortografiskt: klipplanen
        // omsluter lådan var det än står.
        const double halv =
            std::max(hy / sy, hx / (sx * static_cast<double>(box.w) / box.h));
        if (!(halv > 0.0))
            return c;   // lådan är bara djup — ortografiskt syns ingen storlek
        if (cam.orthoHeight > 0.0)
            c.orthoHeight = halv;
        else
            c.distance = halv / ty;
    }
    return c;
}

namespace {

double degrees(double rad)
{
    return rad * 180.0 / std::numbers::pi;
}

} // namespace

Camera3D orientedAs(const Camera3D& cam, const gfx::Vec3d& forward,
                    const gfx::Vec3d& up)
{
    const Vec3   f0{forward.x, forward.y, forward.z};
    const double fl = std::sqrt(dot(f0, f0));
    if (!(fl > 0.0) || !std::isfinite(fl))
        return cam;
    const Vec3 f  = mul(f0, 1.0 / fl);
    const Vec3 u0{up.x, up.y, up.z};
    Vec3       u  = sub(u0, mul(f, dot(u0, f)));   // uppåt i blickens plan
    const double ul = std::sqrt(dot(u, u));
    if (!(ul > 0.0) || !std::isfinite(ul))
        return cam;
    u = mul(u, 1.0 / ul);

    // Elevationen är hur långt blicken lutar nedåt; kompassriktningen är
    // blickens horisontella del — rakt upp- eller nedåt upp-vektorns, som
    // är elevationens tangent och pekar längs kompassriktningen ovanifrån
    // och mot den underifrån (cameraBasis).
    Camera3D c  = cam;
    c.up        = UpAxis::Z;
    c.elevation = static_cast<float>(degrees(std::asin(std::clamp(-f.z, -1.0, 1.0))));
    const double az = std::hypot(f.x, f.y) > kStraightDown ? std::atan2(f.x, f.y)
                      : f.z < 0.0                          ? std::atan2(u.x, u.y)
                                                           : std::atan2(-u.x, -u.y);
    c.azimuth = wrapBearing(static_cast<float>(degrees(az)));
    c.roll    = 0.0f;
    // Rollen: vinkeln från den rollfria upp-vektorn till den givna, kring
    // blicken (cameraBasis vrider upp mot höger).
    const CameraBasis b0 = cameraBasis(c);
    c.roll = wrapBearing(
        static_cast<float>(degrees(std::atan2(dot(u, b0.right), dot(u, b0.up)))));
    return c;
}

} // namespace glaze::detail

namespace glaze {

Camera3D Camera3D::lookFrom(Point3 eye, float yaw, float pitch, float fov,
                            UpAxis up)
{
    Camera3D c;
    c.azimuth   = yaw;
    c.elevation = -pitch;
    c.fov       = fov;
    c.up        = up;
    c.distance  = 1.0;
    const detail::CameraBasis b = detail::cameraBasis(c);
    c.target = {eye.x + b.forward.x, eye.y + b.forward.y, eye.z + b.forward.z};
    return c;
}

Point3 Camera3D::eye() const
{
    const detail::CameraBasis b = detail::cameraBasis(*this);
    return {target.x - b.forward.x * distance,
            target.y - b.forward.y * distance,
            target.z - b.forward.z * distance};
}

} // namespace glaze
