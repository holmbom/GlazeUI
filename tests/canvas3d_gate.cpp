// canvas3d_gate — RITYTANS INTERAKTION I 3D:
// orbitens gester, de tagna tangenterna som håller ritytan tickande,
// mittenknappen och styrplattans gest till appen och widgets PÅ ritytan.
//
// Kör:  ./build/tests/canvas3d_gate [-v] [--hand]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//
// `--hand` grindar INGENTING: det öppnar ett synligt fönster för testpasset
// (doc/os/*/TESTS.md) — orbiten, förstapersonen med WASD och textfälten på
// ritytan med riktig mus, styrplatta och tangentbord (längst ner i filen).
//
// GESTERNA ÄR ROUTING OCH KAMERA, och därför matar grinden FÖNSTRETS ingång
// (`AppWindow::handleEvent`, som i canvas_gate) i ett DOLT fönster med
// en riktig `glaze::Canvas` med en `Camera3D`. REFERENSEN ÄR GRINDENS EGEN
// HÅLKAMERA, skriven ur konventionen i glaze/canvas/camera.h (azimut som
// kompass, upp längs elevationens tangent, rollen medurs) — aldrig ritytans
// eller camera_math:s formel: panoreringens målplanspunkt tas ur grindens
// egen stråle, och var den hamnar efteråt ur grindens egen projektion.
// Ritytans FAKTISKA projektion läses genom dess publika `toLocal` (en punkt
// på marken), som går genom samma 3D-vy som ritningen.
//
// VAD GRINDEN PÅSTÅR:
//   H   orbitens tal headless (camera_math): dragets 0,8°/0,5° per logisk px
//       och tecknen, dollyns steg (en rad 1/√2, en tangent 1/2), tickens tak
//       0,1 s — och att en kamera UTANFÖR spannen aldrig dras in med ett
//       hopp (elevationen och avståndet).
//   P   PANORERINGEN i FLYGLÄGET — de
//       bindningar som motsvarar ett flygläge (tests/orbit_fly.h), med
//       samma referens: i SIDLED följer målplanets punkt pekaren, och fram/bak
//       FLYGER längs blicken (mittendraget) eller HÖJS längs z
//       (Shift+mittendrag, Shift+vänsterdrag) med målplanets belopp — målet
//       inom 1e-6 från referensen, i perspektiv och ortografiskt, vid SWEREF med
//       azimut, elevation och roll; avståndet och vinklarna orörda;
//       onCameraChange. Ett drag NEDÅT flyger FRAMÅT. Vänsterdraget utan
//       orbit (vridning och tilt av) gör INGENTING.
//   O   ORBITEN: vänsterdrag ger 0,8° azimut per px åt höger och 0,5°
//       elevation per px nedåt, målet och avståndet orörda; Ctrl+mittendrag
//       likaså; bara azimut utan pitchable, bara elevation utan rotatable.
//   E   ELEVATIONEN: klampas till förvalet 85° och till setElevationRange;
//       en kamera satt utanför spannet hoppar inte; med TUMLING passerar den
//       90° i steg om 1° och ritytans egen projektion av en markpunkt följer
//       referensen och rör sig kontinuerligt (upp-vektorn vänder aldrig).
//   D   DOLLYN: hjulet 1/√2 per rad mot målet (målet orört), klampad till
//       setDistanceRange åt båda håll, + och − en hel nivå.
//   K   tangenterna: högerpilen panorerar ett stilsteg i målplanet,
//       uppåtpilen flyger FRAMÅT längs blicken, Shift+pilarna ger
//       15° azimut och 10° elevation.
//   V   verktygen: orbitBy/panBy/dollyBy fyrar INTE onCameraChange och gäller
//       med gesterna av; utan 3D-kamera ingenting; zoomToBox rymmer lådan
//       tätt med luften; easeTo(Camera3D) landar exakt och 3D-målet ritas
//       om med den SISTA kameran (cachen ser landningen), därefter vila.
//   M   MITTENKNAPPEN TILL APPEN: en handler som TAR mittentrycket — kameran
//       står still, handlern får ner/drag/upp med `button` Middle och
//       fönstret ritas om helt; utan `handled` panorerar draget och handlern
//       ser varje rörelse EN gång (inte en gång till genom hover-kroken).
//   G   onGesture: handlern får faserna, skalan, vridningen och pekaren i
//       lokala px; `handled` stoppar den inbyggda dollyn, utan den dollyar
//       nypningen; med gesterna av tar ritytan ändå sekvensen åt handlern.
//   C   ett barn-TextInput PÅ ritytan: klicket ger fokus, tecknen och
//       tangenterna går till fältet och inte till ritytans onKeyDown, och
//       barnen klipps till ritytan (en del utanför träffas inte; klippets
//       radie är fältets hörn).
//   T   TICKNINGEN genom PUMPEN (asynkront): en tagen tangent som hålls ger
//       onTick (sekunderna ≤ 0,1); efter KeyUp kommer inga fler tick och inga
//       fler bildrutor (regel 1); WindowFocusLost tömmer isKeyHeld och tar
//       slut på tickningen; setTicking på och av; utan onTick ingen tickning.
//   T2  TICKEN RITAR OM RITYTAN, INTE
//       FÖNSTRET: två ritytor med 3D-mål i ett eget fönster, den ena tickar
//       tio gånger — fönstrets `targetRenders()` ökar med tio, inte tjugo.
//       Och synkront före det: hjulets och nypningens dolly med
//       `onCameraChange` ritar om ETT mål, inte två.
//   B   CARET-BLINKET genom pumpen: fältets riktiga blinktimer ritar ritytan
//       (kompositen) men 3D-målet ritas inte om (`targetRenders` +0).
//
// VAD DEN INTE MÄTER, skrivet så anspråket inte överdrivs:
//   * riktig mus och styrplatta — att backenderna levererar mittenknappen,
//     modifierarna under ett hjul och gesterna (doc/os/*/TESTS.md; hjulets
//     och gestens mask genom `AppWindow`, Win32:s WndProc och Linux
//     modifierartangent mäts i trackpad_gate del O och B);
//   * känslan i orbitens tal (0,8°/px är kartans, inte mätt i en hand);
//   * tickens takt — den följer bildrutorna, och här ritar ett dolt fönster
//     så fort pumpen hinner;
//   * pixlarna i ritytans rundade barnklipp (scene3d_pixel_gate G2 mäter
//     scenens hörn; här mäts radien och hit-testens rektangel);
//   * i T2: en apps `onPaint` på en ANNAN rityta som läser ett fält ticken
//     ändrat utan sättare — det är D1:s medvetna kontraktsbrott (canvas.h),
//     och den ytan ritas inte om; styrplattans tvåfingersdrag och orbit i 3D
//     (samma rad i `mouseWheel` som hjulets dolly); `onGesture` och
//     snäppets postade händelse mäts i canvas_gate D1.
//
// SABOTAGEN den fäller (prövade i en
// privat kopia, alla fällda): tickningen fortsätter efter
// släppet (T), mängden töms inte vid fokusförlust (T), mittenhandlern
// ignoreras (M), hover-kroken ger mittendragets rörelse en gång till (M),
// panoreringen i markplanet i stället för målplanet (P) och i världsenheter
// per ytans höjd (P, K, V), tumlingen vänder upp-vektorn (E) och tumlingen
// ignoreras (E), landningen stegar inte 3D-målets stämpel (V) och barnen
// klipps inte (C). Styrplattans tre — Ctrl ignoreras på draget, nypningens
// dolly och vridningens azimut åt fel håll — fälls i trackpad_gate del O.
// Och för T2 (skrivna): `appWindow()->invalidate()`
// tillbaka efter `onTick` i `Canvas::stepTick` ⇒ T2 får tjugo målritningar;
// detsamma efter `onCameraChange` i 3D-grenen av `mouseWheel` eller i
// `gesture` ⇒ T2:s dolly ritar två mål.
// Och för P och K (skrivna): gesterna
// som panorerar i målplanet (`gestPanorering` alltid Plane) ⇒ P
// faller utom de rent vågräta stegen; Shift ignorerad (alltid Fly) ⇒ P
// Shift-fallen faller; pilarna med Plane ⇒ K uppåtpilen faller; den lodräta
// delens tecken vänt ⇒ P "nedåt flyger framåt" och K faller.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gfx/camera.h>
#include <glaze/glaze.h>

#include "canvas/camera_math.h"   // intern — orbitens tal (del H)
#include "canvas/resources.h"     // intern — 3D-målets räknare (del V, B)
#include "orbit_fly.h"             // flygläget som bindningar (del P)

namespace {

int  g_fel     = 0;
int  g_fall    = 0;
int  g_skips   = 0;
bool g_verbose = false;

void check(bool ok, const std::string& vad, const std::string& fick = {})
{
    ++g_fall;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                        fick.c_str());
        return;
    }
    std::printf("  FEL  %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                fick.c_str());
    ++g_fel;
}

std::string tal(double v)
{
    char b[48];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

bool nära(double a, double b, double e)
{
    return std::abs(a - b) <= e;
}

constexpr double kPi = 3.14159265358979323846;

double rad(double deg)
{
    return deg * kPi / 180.0;
}

// ------------------------------------------------------- grindens hålkamera
struct V3 {
    double x = 0.0, y = 0.0, z = 0.0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V3 v3(glaze::Point3 p) { return {p.x, p.y, p.z}; }

// KONVENTIONEN (glaze/canvas/camera.h), skriven för grinden: azimuten är
// kompassen blicken pekar mot (sin az öster, cos az norr), elevationen hur
// högt ögat står — blicken lutar lika mycket nedåt — och upp är riktningen
// ögat rör sig när elevationen ökar. Rollen vrider kameran medurs kring
// blicken: upp lutar mot höger. Z upp.
struct Hal {
    V3         eye, f, r, u;
    bool       ortho = false;
    double     fovY  = 0.0, halfH = 0.0;
    gfx::Rectf box{};
};

Hal hal(const glaze::Camera3D& k, gfx::Rectf box)
{
    const double az = rad(k.azimuth), el = rad(k.elevation), ro = rad(k.roll);
    const V3     f{std::sin(az) * std::cos(el), std::cos(az) * std::cos(el),
               -std::sin(el)};
    const V3     u0{std::sin(az) * std::sin(el), std::cos(az) * std::sin(el),
                std::cos(el)};
    const V3     r0 = cross(f, u0);
    Hal          h;
    h.f     = f;
    h.r     = r0 * std::cos(ro) - u0 * std::sin(ro);
    h.u     = u0 * std::cos(ro) + r0 * std::sin(ro);
    h.eye   = v3(k.target) - f * k.distance;
    h.ortho = k.projection == glaze::Projection::Orthographic;
    h.fovY  = rad(k.fov);
    h.halfH = k.orthoHeight > 0.0 ? k.orthoHeight
                                  : k.distance * std::tan(h.fovY / 2.0);
    h.box   = box;
    return h;
}

// Skalan px per enhet i djupet `d` (ortografiskt samma överallt).
double halSkala(const Hal& h, double d)
{
    return h.ortho ? (h.box.h / 2.0) / h.halfH
                   : (h.box.h / 2.0) / std::tan(h.fovY / 2.0) / d;
}

bool halProj(const Hal& h, V3 p, double& px, double& py)
{
    const V3     q = p - h.eye;
    const double d = dot(q, h.f);
    if (!h.ortho && !(d > 0.0))
        return false;
    const double s = halSkala(h, d);
    px             = h.box.x + h.box.w / 2.0 + s * dot(q, h.r);
    py             = h.box.y + h.box.h / 2.0 - s * dot(q, h.u);
    return true;
}

// MÅLPLANETS punkt under en pixel (fysiska px): grindens egen stråle —
// perspektiv från ögat, ortografiskt längs blicken — mot planet genom målet
// vinkelrätt mot blicken.
V3 målplanspunkt(const Hal& h, V3 mål, double px, double py)
{
    const double cx = h.box.x + h.box.w / 2.0, cy = h.box.y + h.box.h / 2.0;
    V3           o, d;
    if (h.ortho) {
        const double s = halSkala(h, 1.0);
        o = h.eye + h.r * ((px - cx) / s) - h.u * ((py - cy) / s);
        d = h.f;
    } else {
        const double F = (h.box.h / 2.0) / std::tan(h.fovY / 2.0);
        o = h.eye;
        d = h.f + h.r * ((px - cx) / F) - h.u * ((py - cy) / F);
    }
    const double t = dot(mål - o, h.f) / dot(d, h.f);
    return o + d * t;
}

// ================================================================ HEADLESS

void fallH()
{
    using namespace glaze::detail;
    check(nära(orbitAzimuthDelta(10.0f), 8.0, 1e-6) &&
              nära(orbitElevationDelta(10.0f), 5.0, 1e-6) &&
              nära(orbitElevationDelta(-4.0f), -2.0, 1e-6),
          "H dragets orbit: 0,8° azimut per logisk px åt höger, 0,5° elevation "
          "per px NEDÅT");
    check(nära(wheelDollyFactor(1.0f), 1.0 / std::sqrt(2.0), 1e-12) &&
              nära(wheelDollyFactor(-2.0f), 2.0, 1e-12) &&
              keyDollyFactor(true) == 0.5 && keyDollyFactor(false) == 2.0,
          "H dollyn: en hjulrad 1/√2 (en halv zoomnivå), en tangent en hel");
    check(tickSeconds(1000, 1016) == 0.016 && tickSeconds(1000, 3000) == 0.1 &&
              tickSeconds(1000, 1000) == 0.0 && tickSeconds(2000, 1000) == 0.0,
          "H ticken: sekunderna mellan tiderna, högst 0,1");

    glaze::Camera3D k;
    k.elevation           = 89.0f;   // satt utanför ±85
    const glaze::Camera3D ned = orbitCamera3D(k, 0.0f, -2.0f, false, -85.0f, 85.0f);
    const glaze::Camera3D upp = orbitCamera3D(k, 0.0f, 3.0f, false, -85.0f, 85.0f);
    k.elevation           = 80.0f;
    const glaze::Camera3D in = orbitCamera3D(k, 0.0f, 10.0f, false, -85.0f, 85.0f);
    check(nära(ned.elevation, 87.0, 1e-5) && nära(upp.elevation, 89.0, 1e-5) &&
              nära(in.elevation, 85.0, 1e-5),
          "H en elevation satt utanför spannet hoppar inte in (89 → 87, och "
          "inte längre ut), inifrån klampas den (80 + 10 → 85)",
          tal(ned.elevation) + " / " + tal(upp.elevation) + " / " +
              tal(in.elevation));
    glaze::Camera3D d;
    d.distance              = 500.0;   // satt utanför [5, 50]
    const glaze::Camera3D d1 = dollyCamera3D(d, 0.5, 5.0, 50.0);
    const glaze::Camera3D d2 = dollyCamera3D(d, 2.0, 5.0, 50.0);
    check(nära(d1.distance, 250.0, 1e-9) && d2.distance == 500.0,
          "H ett avstånd satt utanför spannet får gå mot det men inte längre "
          "ut — inget hopp till 50",
          tal(d1.distance) + " / " + tal(d2.distance));
}

// ============================================================= MED FÖNSTER

constexpr std::uint32_t kTimer = 7411;   // grindens egen (appens intervall)

struct Yta : glaze::Canvas {
    int           ritningar = 0;
    std::uint64_t senast    = 0;   // ticksMs vid senaste ritningen

protected:
    void draw(const glaze::Ctx& c) override
    {
        ++ritningar;
        senast = gfx::ticksMs();
        glaze::Canvas::draw(c);
    }
};

struct Fält : glaze::TextInput {
    int ritningar = 0;

protected:
    void draw(const glaze::Ctx& c) override
    {
        ++ritningar;
        glaze::TextInput::draw(c);
    }
};

struct Etikett : glaze::Label {
    int ritningar = 0;

protected:
    void draw(const glaze::Ctx& c) override
    {
        ++ritningar;
        glaze::Label::draw(c);
    }
};

struct Värd : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    Yta                   yta;
    Fält                  fält;      // PÅ ritytan (dess barn)
    Etikett               etikett;   // bredvid — full omritning syns i den
    std::function<void()> vidTimer;
    int                   ändringar = 0;

    Värd() : glaze::AppWindow("canvas3d_gate", 720, 480, gfx::Chrome::None)
    {
        yta.name   = "yta";
        yta.x      = 20;
        yta.y      = 20;
        yta.width  = 480;
        yta.height = 360;
        yta.setBorder(false);
        root().adopt(yta);
        fält.name   = "fält";
        fält.x      = 30;
        fält.y      = 30;
        fält.width  = 140;
        fält.height = 26;
        yta.adopt(fält);
        etikett.name = "etikett";
        etikett.x    = 540;
        etikett.y    = 40;
        etikett.setText("etikett");
        root().adopt(etikett);
    }

    bool event(const gfx::Event& e) override
    {
        if (e.type == gfx::EventType::Timer && e.timerId == kTimer && vidTimer) {
            window().stopTimer(kTimer);
            auto f   = std::move(vidTimer);
            vidTimer = nullptr;
            f();
        }
        return false;
    }

    gfx::Rectf box() { return yta.resolve(yta.behaviorCtx()); }
    float      skala() { return window().uiScale(); }
    gfx::Pointf fys(double lx, double ly)
    {
        const gfx::Rectf b = box();
        return gfx::Pointf{static_cast<float>(b.x + lx * skala()),
                           static_cast<float>(b.y + ly * skala())};
    }
    void mus(gfx::EventType t, gfx::Pointf p, gfx::MouseButton b,
             std::uint32_t mods = 0)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = b;
        e.mods     = mods;
        handleEvent(e);
    }
    void ner(gfx::Pointf p, std::uint32_t mods = 0,
             gfx::MouseButton b = gfx::MouseButton::Left)
    {
        mus(gfx::EventType::MouseButtonDown, p, b, mods);
    }
    void upp(gfx::Pointf p, std::uint32_t mods = 0,
             gfx::MouseButton b = gfx::MouseButton::Left)
    {
        mus(gfx::EventType::MouseButtonUp, p, b, mods);
    }
    void flytta(gfx::Pointf p, std::uint32_t mods = 0)
    {
        mus(gfx::EventType::MouseMotion, p, gfx::MouseButton::None, mods);
    }
    void hjul(gfx::Pointf p, float rader)
    {
        gfx::Event e;
        e.type     = gfx::EventType::MouseWheel;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.wheelY   = rader;
        handleEvent(e);
    }
    void gest(gfx::Pointf p, gfx::GesturePhase fas, float skala, float grader)
    {
        gfx::Event e;
        e.type            = gfx::EventType::Gesture;
        e.windowId        = window().id();
        e.mouseX          = p.x;
        e.mouseY          = p.y;
        e.phase           = fas;
        e.gestureScale    = skala;
        e.gestureRotation = grader;
        handleEvent(e);
    }
    void tangent(gfx::EventType t, gfx::Key k, std::uint32_t mods = 0)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.key      = k;
        e.mods     = mods;
        handleEvent(e);
    }
    void ned(gfx::Key k, std::uint32_t mods = 0)
    {
        tangent(gfx::EventType::KeyDown, k, mods);
    }
    void släpp(gfx::Key k) { tangent(gfx::EventType::KeyUp, k); }
    void skriv(const char* s)
    {
        gfx::Event e;
        e.type     = gfx::EventType::TextInput;
        e.windowId = window().id();
        std::snprintf(e.text, sizeof e.text, "%s", s);
        handleEvent(e);
    }
    void enkel(gfx::EventType t)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        handleEvent(e);
    }
    void rita() { window().renderFrame(); }
    std::uint64_t målRitningar()
    {
        const glaze::Ctx c = yta.behaviorCtx();
        return c.canvas ? c.canvas->targetRenders() : 0;
    }

    // Varje fall börjar från samma läge: en 3D-kamera, gesterna av,
    // förvalda spann och inga handlers.
    void nollställ(const glaze::Camera3D& k)
    {
        yta.setPannable(false);
        yta.setZoomable(false);
        yta.setRotatable(false);
        yta.setPitchable(false);
        yta.setOrbitTumble(false);
        yta.setElevationRange(glaze::detail::defaultElevationMin(),
                              glaze::detail::defaultElevationMax());
        yta.setDistanceRange(0.0, 0.0);
        yta.setTicking(false);
        yta.setCamera(k);
        yta.onMouseDown = nullptr;
        yta.onMouseMove = nullptr;
        yta.onMouseUp   = nullptr;
        yta.onKeyDown   = nullptr;
        yta.onGesture   = nullptr;
        yta.onTick      = nullptr;
        rita();
    }

    // EN FÖLJD STEG GENOM PUMPEN: steg i körs i fönstrets timer efter sin
    // väntan (ms), och pumpen ritar och postar emellan.
    std::vector<std::pair<int, std::function<void()>>> följd;
    std::size_t                                        nästa = 0;
    void fortsätt()
    {
        if (nästa >= följd.size())
            return;
        vidTimer = [this] {
            auto f = std::move(följd[nästa].second);
            ++nästa;
            f();
            fortsätt();
        };
        window().startTimer(kTimer, följd[nästa].first);
    }

    void synkront();
    void tick1();
    void blink();
    void klar() { close(); }
};

glaze::Camera3D kamera(glaze::Point3 mål, double avst, float az, float el,
                       float roll = 0.0f, bool orto = false)
{
    glaze::Camera3D k;
    k.target     = mål;
    k.distance   = avst;
    k.azimuth    = az;
    k.elevation  = el;
    k.roll       = roll;
    k.projection = orto ? glaze::Projection::Orthographic
                        : glaze::Projection::Perspective;
    return k;
}

void Värd::synkront()
{
    yta.onCameraChange = [this] { ++ändringar; };
    const float s = skala();

    // ---- P. PANORERINGEN: målplanets punkt följer pekaren i
    // SIDLED, och fram/bak FLYGER längs blicken — med Shift höjs och sänks
    // kameran längs z — med målplanets lodräta belopp. Referens ur grindens
    // hålkamera: målplanets punkter under trycket och släppet, skillnaden
    // uppdelad på hålkamerans höger och upp (draget går i två steg, och
    // målplanets punkt relativt målet beror bara på pixeln — stegen summeras).
    const auto panorera = [&](const char* vad, gfx::MouseButton knapp,
                              std::uint32_t mods, bool lyft) {
        const gfx::Pointf    a     = fys(140, 120);
        const gfx::Pointf    b1    = fys(260, 60);
        const gfx::Pointf    b     = fys(330, 250);
        const glaze::Camera3D före = yta.camera3D();
        const Hal             h    = hal(före, box());
        const V3 D = målplanspunkt(h, v3(före.target), a.x, a.y) -
                     målplanspunkt(h, v3(före.target), b.x, b.y);
        const V3 väntad = v3(före.target) + h.r * dot(D, h.r) +
                          (lyft ? V3{0.0, 0.0, 1.0} : h.f) * dot(D, h.u);
        const int ä = ändringar;
        ner(a, mods, knapp);
        flytta(b1, mods);
        flytta(b, mods);
        upp(b, mods, knapp);
        const glaze::Camera3D efter = yta.camera3D();
        const V3     skillnad = v3(efter.target) - väntad;
        const double fel      = std::sqrt(dot(skillnad, skillnad));
        check(fel < 1e-6 && efter.distance == före.distance &&
                  efter.azimuth == före.azimuth &&
                  efter.elevation == före.elevation && ändringar > ä,
              std::string("P ") + vad +
                  ": sidled följer målplanets punkt, fram/bak " +
                  (lyft ? "höjer längs z" : "flyger längs blicken") +
                  ", vinklarna och avståndet orörda, onCameraChange",
              tal(fel) + " fel, " + std::to_string(ändringar - ä) +
                  " onCameraChange");
    };
    nollställ(kamera({674000.0, 6580000.0, 12.0}, 90.0, 50.0f, 33.0f, 5.0f));
    yta.setPannable(true);
    yta.setRotatable(true);
    yta.setPitchable(true);
    gate::orbitFly(yta);   // P mäter flygläget; PS nedan förvalet
    panorera("mittendrag, perspektiv vid SWEREF med roll",
             gfx::MouseButton::Middle, 0, false);
    panorera("Shift+mittendrag", gfx::MouseButton::Middle, gfx::kModShift, true);
    // Vänsterdraget panorerar bara med Shift (orbiten annars) — och höjer.
    panorera("Shift+vänsterdrag", gfx::MouseButton::Left, gfx::kModShift, true);
    nollställ(kamera({674000.0, 6580000.0, 12.0}, 90.0, -120.0f, 61.0f, 0.0f, true));
    yta.setPannable(true);
    gate::orbitFly(yta);
    panorera("mittendrag, ortografiskt", gfx::MouseButton::Middle, 0, false);
    // Utan rotation och tilt gör vänsterdraget INGENTING:
    // {} är orbitens minst specifika rad för
    // vänsterknappen, och under den finns ingen panorering att falla till.
    {
        const glaze::Camera3D före = yta.camera3D();
        ner(fys(140, 120), 0, gfx::MouseButton::Left);
        flytta(fys(330, 250), 0);
        upp(fys(330, 250), 0, gfx::MouseButton::Left);
        const glaze::Camera3D efter = yta.camera3D();
        check(efter.target.x == före.target.x && efter.target.y == före.target.y &&
                  efter.target.z == före.target.z && efter.azimuth == före.azimuth &&
                  efter.elevation == före.elevation,
              "P vänsterdraget utan vridning och tilt står still (skriven skillnad)");
    }
    // GREPPET: ett rent drag NEDÅT drar världen emot en — kameran flyger
    // FRAMÅT längs blicken, ingenting i sidled eller längs upp.
    {
        const glaze::Camera3D före = yta.camera3D();
        const Hal             h    = hal(före, box());
        ner(fys(200, 100), 0, gfx::MouseButton::Middle);
        flytta(fys(200, 220), 0);
        upp(fys(200, 220), 0, gfx::MouseButton::Middle);
        const V3 dv = v3(yta.camera3D().target) - v3(före.target);
        check(dot(dv, h.f) > 0.0 && std::abs(dot(dv, h.r)) < 1e-6 &&
                  std::abs(dot(dv, h.u)) < 1e-6,
              "P ett mittendrag NEDÅT flyger FRAMÅT längs blicken",
              tal(dot(dv, h.f)) + " framåt");
    }

    // ---- PS. FÖRVALET: målplanets punkt
    // under trycket hamnar under släppet i BÅDA axlarna — bara målet flyttas,
    // avstånd, azimut och elevation står still; `clearBindings` fyrar inget
    // onCameraChange. Shift flyger (fram/bak längs blicken). Sabotage: förvalets
    // rad MiddleDragY → Fly i stället för PanY fäller PS1; raden Shift → Fly
    // borttagen fäller PS2.
    {
        nollställ(kamera({674000.0, 6580000.0, 12.0}, 90.0, 50.0f, 33.0f, 5.0f));
        yta.setPannable(true);
        yta.setRotatable(true);
        yta.setPitchable(true);
        const int ä0 = ändringar;
        yta.clearBindings();
        check(yta.bindings().empty() && ändringar == ä0,
              "PS0 clearBindings ger förvalet tillbaka och fyrar inget "
              "onCameraChange", "");
        const auto skärm = [&](const char* vad, std::uint32_t mods, bool flyg) {
            const gfx::Pointf     a    = fys(140, 120);
            const gfx::Pointf     b    = fys(330, 250);
            const glaze::Camera3D före = yta.camera3D();
            const Hal             h    = hal(före, box());
            const V3 D = målplanspunkt(h, v3(före.target), a.x, a.y) -
                         målplanspunkt(h, v3(före.target), b.x, b.y);
            const V3 väntad = v3(före.target) + h.r * dot(D, h.r) +
                              (flyg ? h.f : h.u) * dot(D, h.u);
            ner(a, mods, gfx::MouseButton::Middle);
            flytta(b, mods);
            upp(b, mods, gfx::MouseButton::Middle);
            const glaze::Camera3D efter    = yta.camera3D();
            const V3              skillnad = v3(efter.target) - väntad;
            const double          fel      = std::sqrt(dot(skillnad, skillnad));
            check(fel < 1e-6 && efter.distance == före.distance &&
                      efter.azimuth == före.azimuth &&
                      efter.elevation == före.elevation,
                  std::string(vad), tal(fel) + " fel");
        };
        skärm("PS1 Screen: punkten följer pekaren i båda axlarna, bara målet flyttas",
              0, false);
        skärm("PS2 Screen + Shift: fram/bak flyger längs blicken (Fly)",
              gfx::kModShift, true);
        gate::orbitFly(yta);
    }

    // ---- O. ORBITEN: vänsterdrag
    const glaze::Camera3D o0 = kamera({100.0, 200.0, 5.0}, 60.0, 20.0f, 30.0f);
    const auto orbita = [&](gfx::MouseButton knapp, std::uint32_t mods) {
        ner(fys(200, 150), mods, knapp);
        flytta(fys(225, 160), mods);
        flytta(fys(250, 170), mods);   // +50 vågrätt, +20 nedåt
        upp(fys(250, 170), mods, knapp);
    };
    nollställ(o0);
    yta.setRotatable(true);
    yta.setPitchable(true);
    yta.setPannable(true);
    int ä = ändringar;
    orbita(gfx::MouseButton::Left, 0);
    glaze::Camera3D k = yta.camera3D();
    check(nära(k.azimuth, 20.0 + 50.0 * 0.8, 0.05) &&
              nära(k.elevation, 30.0 + 20.0 * 0.5, 0.05) &&
              k.target.x == o0.target.x && k.target.y == o0.target.y &&
              k.distance == o0.distance && ändringar > ä,
          "O vänsterdrag: 0,8° azimut per logisk px åt höger, 0,5° elevation "
          "per px nedåt, målet och avståndet orörda",
          tal(k.azimuth) + "° / " + tal(k.elevation) + "°");
    nollställ(o0);
    yta.setRotatable(true);
    yta.setPitchable(true);
    yta.setPannable(true);
    orbita(gfx::MouseButton::Middle, gfx::kModCtrl);
    k = yta.camera3D();
    check(nära(k.azimuth, 60.0, 0.05) && nära(k.elevation, 40.0, 0.05),
          "O Ctrl+mittendrag orbitar som vänsterdraget",
          tal(k.azimuth) + "° / " + tal(k.elevation) + "°");
    nollställ(o0);
    yta.setRotatable(true);
    orbita(gfx::MouseButton::Left, 0);
    const glaze::Camera3D bara = yta.camera3D();
    nollställ(o0);
    yta.setPitchable(true);
    orbita(gfx::MouseButton::Left, 0);
    check(nära(bara.azimuth, 60.0, 0.05) && bara.elevation == 30.0f &&
              yta.camera3D().azimuth == 20.0f &&
              nära(yta.camera3D().elevation, 40.0, 0.05),
          "O utan pitchable bara azimut, utan rotatable bara elevation");

    // ---- E. ELEVATIONEN: spärren, spannet, inget hopp och tumlingen
    nollställ(kamera({0.0, 0.0, 0.0}, 50.0, 0.0f, 80.0f));
    yta.setRotatable(true);
    yta.setPitchable(true);
    ner(fys(200, 50));
    flytta(fys(200, 300));   // 250 px nedåt = 125° — långt förbi
    upp(fys(200, 300));
    const float spärr = yta.camera3D().elevation;
    yta.setElevationRange(60.0f, -30.0f);   // omkastat byts
    ner(fys(200, 300));
    flytta(fys(200, 20));   // 280 px uppåt = −140°
    upp(fys(200, 20));
    const float botten = yta.camera3D().elevation;
    check(spärr == 85.0f && botten == -30.0f && yta.minElevation() == -30.0f &&
              yta.maxElevation() == 60.0f,
          "E elevationen klampas till förvalet 85° och till setElevationRange "
          "(omkastat byts)",
          tal(spärr) + " / " + tal(botten));
    yta.setElevationRange(-90.0f, 200.0f);   // ogiltigt — loggas, står kvar
    check(yta.maxElevation() == 60.0f, "E ett spann utanför ±180 ignoreras");
    nollställ(kamera({0.0, 0.0, 0.0}, 50.0, 0.0f, 89.0f));
    yta.setPitchable(true);
    ner(fys(200, 150));
    flytta(fys(200, 146));   // 4 px uppåt = −2°
    upp(fys(200, 146));
    const float in = yta.camera3D().elevation;
    ner(fys(200, 150));
    flytta(fys(200, 160));   // +5° — längre ut ur spannet
    upp(fys(200, 160));
    check(nära(in, 87.0, 1e-3) && nära(yta.camera3D().elevation, 87.0, 1e-3),
          "E en kamera satt vid 89° hoppar inte till 85: draget för den mot "
          "spannet (87°) men inte längre ut",
          tal(in) + " / " + tal(yta.camera3D().elevation));
    // TUMLINGEN: 80 → 100 i steg om 1° (2 px nedåt per steg). Ritytans egen
    // projektion av en markpunkt (toLocal) mot grindens hålkamera, och dess
    // väg — kontinuerlig, ingen vändning vid polen.
    nollställ(kamera({100.0, 200.0, 0.0}, 50.0, 20.0f, 80.0f));
    yta.setPitchable(true);
    yta.setOrbitTumble(true);
    {
        const glaze::Point mark{106.0, 209.0};
        double maxFacit = 0.0, maxSteg = 0.0;
        bool   ok       = true;
        glaze::Point förra{};
        ner(fys(200, 100));
        for (int i = 1; i <= 20; ++i) {
            flytta(fys(200, 100 + 2.0 * i));
            rita();
            const glaze::Camera3D kk = yta.camera3D();
            ok = ok && nära(kk.elevation, 80.0 + i, 1e-3);
            const auto lok = yta.toLocal(mark);
            double fx = 0.0, fy = 0.0;
            ok = ok && lok && halProj(hal(kk, box()), {mark.x, mark.y, 0.0}, fx, fy);
            if (!lok)
                break;
            const glaze::Point facit{(fx - box().x) / s, (fy - box().y) / s};
            maxFacit = std::max(maxFacit, std::hypot(lok->x - facit.x, lok->y - facit.y));
            if (i > 1)
                maxSteg = std::max(maxSteg, std::hypot(lok->x - förra.x, lok->y - förra.y));
            förra = *lok;
        }
        upp(fys(200, 140));
        check(ok && maxFacit < 1e-3 && maxSteg < 15.0,
              "E TUMLINGEN 80 → 100° i steg om 1°: elevationen följer, ritytans "
              "projektion är facits och markpunkten rör sig kontinuerligt",
              "facitfel " + tal(maxFacit) + " px, största steg " + tal(maxSteg) +
                  " px");
    }

    // ---- D. DOLLYN — hjulet mot målet, spannet, tangenterna
    nollställ(kamera({10.0, 20.0, 3.0}, 40.0, 30.0f, 25.0f));
    yta.setZoomable(true);
    ä = ändringar;
    hjul(fys(380, 60), 1.0f);   // pekaren långt från mitten
    k = yta.camera3D();
    check(nära(k.distance, 40.0 / std::sqrt(2.0), 1e-9) && k.target.x == 10.0 &&
              k.target.y == 20.0 && k.target.z == 3.0 && ändringar == ä + 1,
          "D en hjulrad dollyar 1/√2 MOT MÅLET (målet orört, var pekaren än står)",
          tal(k.distance));
    yta.setDistanceRange(50.0, 5.0);   // omkastat byts
    for (int i = 0; i < 20; ++i)
        hjul(fys(200, 150), 1.0f);
    const double nära5 = yta.camera3D().distance;
    for (int i = 0; i < 30; ++i)
        hjul(fys(200, 150), -1.0f);
    check(nära5 == 5.0 && yta.camera3D().distance == 50.0,
          "D dollyn klampas till setDistanceRange åt båda håll",
          tal(nära5) + " / " + tal(yta.camera3D().distance));
    yta.setDistanceRange(0.0, 0.0);
    yta.focus();
    ned(gfx::Key::Plus);
    const double plus = yta.camera3D().distance;
    ned(gfx::Key::Minus);
    ned(gfx::Key::Minus);
    check(nära(plus, 25.0, 1e-9) && nära(yta.camera3D().distance, 100.0, 1e-9),
          "D + och − dollyar en hel zoomnivå (halva och dubbla avståndet)",
          tal(plus) + " / " + tal(yta.camera3D().distance));
    yta.setZoomable(false);
    hjul(fys(200, 150), 1.0f);
    check(yta.camera3D().distance == 100.0, "D utan zoom rör hjulet inte kameran");

    // ---- K. TANGENTERNA: pilen panorerar ett stilsteg, Shift+pilarna orbitar
    nollställ(kamera({0.0, 0.0, 0.0}, 80.0, 35.0f, 40.0f));
    yta.setPannable(true);
    yta.setRotatable(true);
    yta.setPitchable(true);
    yta.focus();
    {
        const glaze::Camera3D före  = yta.camera3D();
        const glaze::Style&   st    = style();
        const gfx::Pointf     mitt  = fys(240, 180);
        const gfx::Pointf     höger = fys(240 + st.canvasPanStep, 180);
        const V3 P = målplanspunkt(hal(före, box()), v3(före.target), höger.x, höger.y);
        ned(gfx::Key::Right);
        double px = 0.0, py = 0.0;
        halProj(hal(yta.camera3D(), box()), P, px, py);
        check(std::hypot(px - mitt.x, py - mitt.y) < 1e-3,
              "K högerpilen: målplanets punkt ett stilsteg till höger hamnar i "
              "mitten",
              tal(std::hypot(px - mitt.x, py - mitt.y)) + " px");
        // UPPÅTPILEN FLYGER FRAMÅT: målplanets punkt ett steg upp
        // förs till mitten — ett drag nedåt — och den lodräta delen går längs
        // blicken.
        const glaze::Camera3D f2   = yta.camera3D();
        const Hal             h2   = hal(f2, box());
        const gfx::Pointf     ovan = fys(240, 180 - st.canvasPanStep);
        const V3 D2 = målplanspunkt(h2, v3(f2.target), ovan.x, ovan.y) -
                      målplanspunkt(h2, v3(f2.target), mitt.x, mitt.y);
        ned(gfx::Key::Up);
        const V3 dv   = v3(yta.camera3D().target) - v3(f2.target);
        const V3 vänt = h2.r * dot(D2, h2.r) + h2.f * dot(D2, h2.u);
        const V3 rest = dv - vänt;
        check(dot(D2, h2.u) > 0.0 && dot(dv, h2.f) > 0.0 &&
                  std::sqrt(dot(rest, rest)) < 1e-7,
              "K uppåtpilen flyger FRAMÅT längs blicken med stilstegets belopp",
              tal(dot(dv, h2.f)) + " framåt");
        ned(gfx::Key::Left, gfx::kModShift);
        ned(gfx::Key::Up, gfx::kModShift);
        check(nära(yta.camera3D().azimuth, 20.0, 1e-4) &&
                  nära(yta.camera3D().elevation, 30.0, 1e-4),
              "K Shift+vänster −15° azimut, Shift+upp −10° elevation (uppåt = mer "
              "tilt, som kartan)",
              tal(yta.camera3D().azimuth) + " / " + tal(yta.camera3D().elevation));
        // SLÄPP pilarna: en tangent som hålls gör nästa tryck på den till
        // OS:ets UPPREPNING, och upprepningen går till TRYCKETS kedja — här
        // ritytan — och inte till fokus nu. C:s
        // vänsterpil i fältet hade annars nått ritytans onKeyDown.
        släpp(gfx::Key::Right);
        släpp(gfx::Key::Up);
        släpp(gfx::Key::Left);
    }

    // ---- V. VERKTYGEN
    nollställ(kamera({0.0, 0.0, 0.0}, 80.0, 35.0f, 40.0f));
    ä = ändringar;
    yta.orbitBy(10.0f, -5.0f);
    yta.dollyBy(0.5);
    const glaze::Camera3D v1 = yta.camera3D();
    {
        const gfx::Pointf mitt = fys(240, 180);
        const V3          M    = målplanspunkt(hal(v1, box()), v3(v1.target), mitt.x, mitt.y);
        yta.panBy(glaze::Point{30.0, -20.0});
        double px = 0.0, py = 0.0;
        halProj(hal(yta.camera3D(), box()), M, px, py);
        const gfx::Pointf facit = fys(270, 160);
        check(nära(v1.azimuth, 45.0, 1e-4) && nära(v1.elevation, 35.0, 1e-4) &&
                  nära(v1.distance, 40.0, 1e-9) &&
                  std::hypot(px - facit.x, py - facit.y) < 1e-3 && ändringar == ä,
              "V orbitBy/dollyBy/panBy gör orbitens rörelser med gesterna av, och "
              "fyrar inte onCameraChange",
              tal(std::hypot(px - facit.x, py - facit.y)) + " px, " +
                  std::to_string(ändringar - ä) + " onCameraChange");
    }
    {
        // Spannen gäller verktygen också.
        yta.setDistanceRange(30.0, 0.0);
        yta.dollyBy(0.1);
        check(yta.camera3D().distance == 30.0, "V dollyBy klampas till spannet",
              tal(yta.camera3D().distance));
        yta.setDistanceRange(0.0, 0.0);
    }
    // INRAMNINGEN: lådans hörn ryms med luften, tätt (ett hörn vid luften).
    {
        nollställ(kamera({0.0, 0.0, 0.0}, 7.0, 40.0f, 30.0f));
        const glaze::Box3 låda{{674000.0, 6580000.0, 0.0},
                               {674030.0, 6580012.0, 18.0}};
        yta.zoomToBox(låda, glaze::px(20.0f));
        const glaze::Camera3D f = yta.camera3D();
        const gfx::Rectf      b = box();
        const double          luft = 20.0 * s;
        double                ute = 0.0, tätast = 1e30;
        bool                  alla = true;
        for (int i = 0; i < 8; ++i) {
            const V3 h{(i & 1) ? låda.max.x : låda.min.x,
                       (i & 2) ? låda.max.y : låda.min.y,
                       (i & 4) ? låda.max.z : låda.min.z};
            double px = 0.0, py = 0.0;
            alla = alla && halProj(hal(f, b), h, px, py);
            const double kant = std::min({px - (b.x + luft), b.x + b.w - luft - px,
                                          py - (b.y + luft), b.y + b.h - luft - py});
            ute    = std::min(ute, kant);
            tätast = std::min(tätast, std::abs(kant));
        }
        check(alla && ute > -1e-6 && tätast < 1e-6 && f.azimuth == 40.0f &&
                  f.elevation == 30.0f && nära(f.target.x, 674015.0, 1e-9),
              "V zoomToBox: lådans hörn ryms med luften 20 px och sitter tätt, "
              "rotationen orörd",
              "utanför " + tal(ute) + " px, närmast kanten " + tal(tätast) + " px");
        yta.zoomToBox(glaze::Box3{{1.0, 0.0, 0.0}, {0.0, 1.0, 1.0}});
        check(yta.camera3D().distance == f.distance,
              "V en omvänd låda ändrar ingenting");
    }
    // GLIDNINGEN: landar exakt — och 3D-målet ritas om med den sista kameran.
    {
        nollställ(kamera({0.0, 0.0, 0.0}, 80.0, 35.0f, 40.0f));
        const glaze::Camera3D mål = kamera({50.0, -20.0, 4.0}, 30.0, 170.0f, 55.0f);
        ä = ändringar;
        yta.easeTo(mål, 120);
        const bool glider = yta.isEasing() && yta.isAnimating();
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        rita();
        const bool halvvägs = yta.isEasing() &&
                              yta.camera3D().distance < 80.0 &&
                              yta.camera3D().distance > 30.0;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const std::uint64_t r0 = målRitningar();
        rita();   // landningen
        const std::uint64_t r1 = målRitningar();
        const glaze::Camera3D slut = yta.camera3D();
        rita();   // cachens extra bild
        rita();   // vila: ingenting nytt
        const std::uint64_t r2 = målRitningar();
        check(glider && halvvägs && !yta.isEasing() && slut.distance == 30.0 &&
                  slut.azimuth == 170.0f && slut.target.x == 50.0 &&
                  ändringar == ä,
              "V easeTo(Camera3D) glider och landar EXAKT, utan onCameraChange");
        check(r1 == r0 + 1 && r2 == r1,
              "V landningens bild ritas om i 3D-målet (cachen ser den sista "
              "kameran), sedan ingenting",
              std::to_string(r1 - r0) + " / " + std::to_string(r2 - r1));
    }
    {
        // Utan 3D-kamera: verktygen gör ingenting (och loggar).
        glaze::Canvas plan;
        const glaze::Camera3D före = plan.camera3D();
        plan.orbitBy(30.0f, 10.0f);
        plan.dollyBy(0.5);
        plan.panBy(glaze::Point{10.0, 10.0});
        check(!plan.is3D() && plan.camera3D().azimuth == före.azimuth &&
                  plan.camera3D().distance == före.distance,
              "V utan 3D-kamera gör verktygen ingenting");
    }

    // ---- M. MITTENKNAPPEN TILL APPEN
    nollställ(kamera({0.0, 0.0, 0.0}, 80.0, 35.0f, 40.0f));
    yta.setPannable(true);
    {
        int ner_ = 0, rör = 0, upp_ = 0, utanKnapp = 0;
        bool alltMitten = true;
        yta.onMouseDown = [&](glaze::Widget*, glaze::CanvasMouseArgs& e) {
            ++ner_;
            alltMitten = alltMitten && e.button == gfx::MouseButton::Middle;
            e.handled  = true;
        };
        yta.onMouseMove = [&](glaze::Widget*, glaze::CanvasMouseArgs& e) {
            ++rör;
            if (e.button != gfx::MouseButton::Middle)
                ++utanKnapp;
        };
        yta.onMouseUp = [&](glaze::Widget*, glaze::CanvasMouseArgs& e) {
            ++upp_;
            alltMitten = alltMitten && e.button == gfx::MouseButton::Middle;
        };
        const glaze::Camera3D före = yta.camera3D();
        rita();
        const int etikettFöre = etikett.ritningar;
        ner(fys(100, 100), 0, gfx::MouseButton::Middle);
        rita();
        const bool helOmritning = etikett.ritningar > etikettFöre;
        flytta(fys(200, 160));
        flytta(fys(260, 200));
        upp(fys(260, 200), 0, gfx::MouseButton::Middle);
        const glaze::Camera3D efter = yta.camera3D();
        check(efter.target.x == före.target.x && efter.target.y == före.target.y &&
                  efter.target.z == före.target.z,
              "M en mittenhandler som TAR trycket: kameran står still");
        check(ner_ == 1 && rör == 2 && upp_ == 1 && utanKnapp == 0 && alltMitten,
              "M handlern får ner, varje rörelse EN gång och upp — med button "
              "Middle",
              std::to_string(ner_) + "/" + std::to_string(rör) + "/" +
                  std::to_string(upp_) + ", utan knapp " + std::to_string(utanKnapp));
        check(helOmritning,
              "M fönstret ritas om helt efter mittenhandlern (en etikett bredvid "
              "ritas också)");
        // Utan `handled`: draget panorerar, handlern ser ändå varje rörelse.
        yta.onMouseDown = [&](glaze::Widget*, glaze::CanvasMouseArgs&) { ++ner_; };
        ner_ = rör = upp_ = utanKnapp = 0;
        ner(fys(100, 100), 0, gfx::MouseButton::Middle);
        flytta(fys(200, 160));
        flytta(fys(260, 200));
        upp(fys(260, 200), 0, gfx::MouseButton::Middle);
        check(yta.camera3D().target.x != före.target.x && ner_ == 1 && rör == 2 &&
                  upp_ == 1 && utanKnapp == 0,
              "M utan handled panorerar mittendraget, och handlern ser trycket, "
              "rörelserna EN gång och släppet",
              std::to_string(rör) + " rörelser");
        // Vänsterknappen kommer med Left — en rörelse utan tryck utan knapp.
        int vänster = 0;
        yta.onMouseDown = [&](glaze::Widget*, glaze::CanvasMouseArgs& e) {
            if (e.button == gfx::MouseButton::Left)
                ++vänster;
            e.handled = true;
        };
        ner(fys(120, 120));
        upp(fys(120, 120));
        rör = utanKnapp = 0;
        flytta(fys(130, 130));
        check(vänster == 1 && rör == 1 && utanKnapp == 1,
              "M vänstertrycket kommer med Left, en rörelse utan tryck utan knapp");
        yta.onMouseDown = nullptr;
        yta.onMouseMove = nullptr;
        yta.onMouseUp   = nullptr;
    }

    // ---- G. onGesture — styrplattans gest till appen
    nollställ(kamera({0.0, 0.0, 0.0}, 80.0, 35.0f, 40.0f));
    yta.setZoomable(true);
    {
        std::vector<gfx::GesturePhase> faser;
        float        skalor = 1.0f, grader = 0.0f;
        glaze::Point lok{};
        bool         ta = true;
        yta.onGesture = [&](glaze::Widget*, glaze::GestureArgs& e) {
            faser.push_back(e.phase);
            skalor *= e.scale;
            grader += e.rotation;
            lok       = e.local;
            e.handled = ta;
        };
        const gfx::Pointf p = fys(150, 110);
        gest(p, gfx::GesturePhase::Begin, 1.2f, 0.0f);
        gest(p, gfx::GesturePhase::Update, 1.25f, 2.0f);
        gest(p, gfx::GesturePhase::End, 1.0f, 0.0f);
        check(faser.size() == 3 && faser[0] == gfx::GesturePhase::Begin &&
                  faser[2] == gfx::GesturePhase::End && nära(skalor, 1.5, 1e-6) &&
                  nära(grader, 2.0, 1e-6) && nära(lok.x, 150.0, 1e-3) &&
                  nära(lok.y, 110.0, 1e-3),
              "G onGesture får faserna, skalan, vridningen och pekaren i lokala px");
        check(yta.camera3D().distance == 80.0,
              "G `handled` stoppar den inbyggda dollyn");
        ta = false;
        gest(p, gfx::GesturePhase::Begin, 1.25f, 0.0f);
        gest(p, gfx::GesturePhase::End, 1.0f, 0.0f);
        check(nära(yta.camera3D().distance, 80.0 / 1.25, 1e-6),
              "G utan handled dollyar nypningen (avståndet / skalan)",
              tal(yta.camera3D().distance));
        // Med gesterna AV tar ritytan ändå sekvensen åt handlern.
        yta.setZoomable(false);
        faser.clear();
        gest(p, gfx::GesturePhase::Begin, 1.1f, 0.0f);
        gest(p, gfx::GesturePhase::End, 1.0f, 0.0f);
        check(faser.size() == 2, "G med gesterna av får handlern ändå gesten");
        yta.onGesture = nullptr;
        faser.clear();
        gest(p, gfx::GesturePhase::Begin, 1.1f, 0.0f);
        gest(p, gfx::GesturePhase::End, 1.0f, 0.0f);
        check(faser.empty() && nära(yta.camera3D().distance, 64.0, 1e-6),
              "G utan handler och utan gester tar ritytan ingen gest");
    }

    // ---- C. ETT BARN-TextInput PÅ RITYTAN
    nollställ(kamera({0.0, 0.0, 0.0}, 80.0, 35.0f, 40.0f));
    {
        int tagna = 0;
        yta.onKeyDown = [&](glaze::Widget*, glaze::KeyArgs&) { ++tagna; };
        fält.setText("");
        const gfx::Rectf fb = fält.resolve(fält.behaviorCtx());
        const gfx::Pointf mitt{fb.x + fb.w / 2.0f, fb.y + fb.h / 2.0f};
        ner(mitt);
        upp(mitt);
        const bool fokus = fält.focused() && !yta.focused();
        skriv("a");
        skriv("b");
        ned(gfx::Key::Left);
        skriv("x");
        check(fokus && fält.text() == "axb" && tagna == 0,
              "C ett klick på fältet ger det fokus; tecknen och tangenterna går "
              "till fältet och inte till ritytans onKeyDown",
              "\"" + fält.text() + "\", " + std::to_string(tagna) + " till ritytan");
        yta.onKeyDown = nullptr;
        // KLIPPET: barnen klipps till ritytan — en del utanför träffas inte.
        fält.x = 400;   // 400 + 140 > 480: sticker ut till höger
        rita();
        const gfx::Rectf ub = fält.resolve(fält.behaviorCtx());
        const gfx::Rectf yb = box();
        const glaze::Ctx  cc = yta.behaviorCtx();
        bool              utanför = false;
        {
            const float x = yb.x + yb.w + 10.0f * s;   // utanför ritytan, på fältet
            const float y = ub.y + ub.h / 2.0f;
            yta.focus();
            ner(gfx::Pointf{x, y});
            upp(gfx::Pointf{x, y});
            utanför = !fält.focused();
        }
        check(yta.clipsChildren() && utanför &&
                  nära(yta.clipRadius(cc), cc.style->cornerRadius * cc.scale, 1e-6),
              "C barnen klipps till ritytan (en del utanför träffas inte), och "
              "klippets radie är fältets hörn");
        fält.x = 30;
        rita();
    }
}

// T. TICKNINGEN genom pumpen — en följd steg i fönstrets timer, så pumpen
// ritar och postar emellan. Varje "slutar"-fall mäts i TVÅ steg: först en
// paus där det som redan var på väg får landa, därefter ett fönster där
// INGENTING får hända — ett fast tidskrav efter släppet hade fallerat på en
// belastad maskin, där en enda bildruta kan ta hundratals ms.
void Värd::tick1()
{
    nollställ(kamera({0.0, 0.0, 0.0}, 80.0, 35.0f, 40.0f));
    struct Läge {
        int    tick = 0;
        double maxSek = 0.0, minSek = 1.0;
        double förstaSek = -1.0;          // första ticken efter `vid`
        std::uint64_t vidMs = 0, förstaMs = 0;   // när `vid` och den kom
        int    tickVid = 0, ritVid = 0;   // vid släppet / efter pausen
        bool   ok = true;
    };
    auto L = std::make_shared<Läge>();
    yta.onTick = [L](glaze::Widget*, glaze::TickArgs& e) {
        ++L->tick;
        L->maxSek = std::max(L->maxSek, e.seconds);
        L->minSek = std::min(L->minSek, e.seconds);
        if (L->tick == L->tickVid + 1) {
            L->förstaSek = e.seconds;
            L->förstaMs  = gfx::ticksMs();
        }
    };
    yta.onKeyDown = [](glaze::Widget*, glaze::KeyArgs& e) {
        e.handled = e.key == gfx::Key::W;
    };
    const auto vid = [this, L] {
        L->tickVid   = L->tick;
        L->vidMs     = gfx::ticksMs();
        L->förstaSek = -1.0;
    };
    // DEN NYA FÖLJDENS FÖRSTA TICK räknar från sin början: dess sekunder får
    // inte överstiga tiden efter omstarten (förra följdens sista tick ligger
    // 0,8 s bort och hade gett taket 0,1). Kom ticken först efter 0,1 s — en
    // långsam bildruta — är svaret taket i båda fallen och provet säger
    // ingenting; det mäter bara det det kan.
    const auto frånBörjan = [L] {
        return L->förstaSek >= 0.0 &&
               L->förstaSek <=
                   static_cast<double>(L->förstaMs - L->vidMs) / 1000.0 + 0.002;
    };
    const auto vilar = [this, L] {   // efter pausen: utgångsläget för vilan
        L->ok     = L->tick == L->tickVid;
        L->tickVid = L->tick;
        L->ritVid  = yta.ritningar;
    };
    const auto stilla = [this, L](const char* vad) {
        check(L->ok && L->tick == L->tickVid && yta.ritningar == L->ritVid, vad,
              std::to_string(L->tick - L->tickVid) + " tick och " +
                  std::to_string(yta.ritningar - L->ritVid) +
                  " ritningar i vilan");
    };
    constexpr int kHåll = 800, kPaus = 300, kVila = 500;
    yta.focus();
    ned(gfx::Key::W);
    följd = {
        {kHåll,
         [=, this] {
             check(L->tick >= 2 && L->maxSek <= 0.1 && L->minSek >= 0.0 &&
                       isKeyHeld(gfx::Key::W),
                   "T en tagen tangent som hålls ger onTick (sekunderna ≤ 0,1)",
                   std::to_string(L->tick) + " tick, största " + tal(L->maxSek) +
                       " s");
             släpp(gfx::Key::W);
             vid();
         }},
        {kPaus, vilar},
        {kVila,
         [=, this] {
             stilla("T efter KeyUp: inga fler tick och inga fler bildrutor (regel 1)");
             ned(gfx::Key::W);   // tangenten hålls igen …
             vid();
         }},
        {kHåll,
         [=, this] {
             const bool tickar = L->tick > L->tickVid;
             enkel(gfx::EventType::WindowFocusLost);   // … och fönstret tappar fokus
             // Släppet ovan upptäcktes i pump-svansen (ticken som redan var
             // postad) och inte i en ritning — också då börjar en ny följd
             // räkna från början.
             check(tickar && frånBörjan() &&
                       !isKeyHeld(gfx::Key::W),
                   "T en ny följd tickar och räknar från sin början, och "
                   "WindowFocusLost tömmer isKeyHeld",
                   std::string(tickar ? "" : "TICKADE INTE, ") + "första " +
                       tal(L->förstaSek) + " s efter " +
                       std::to_string(L->förstaMs - L->vidMs) + " ms");
             vid();
         }},
        {kPaus, vilar},
        {kVila,
         [=, this] {
             stilla("T efter fokusförlusten: tickningen har tagit slut — inga fler "
                    "bildrutor");
             enkel(gfx::EventType::WindowFocusGained);
             yta.setTicking(true);
             vid();
         }},
        {kHåll,
         [=, this] {
             // En NY följd räknar sina sekunder från sin början — inte från
             // förra följdens sista tick, 0,8 s bort (som taket hade kapat
             // till 0,1).
             check(L->tick >= L->tickVid + 2 && frånBörjan(),
                   "T setTicking(true) tickar, och den nya följdens första tick "
                   "räknar från sin början",
                   std::to_string(L->tick - L->tickVid) + " tick, första " +
                       tal(L->förstaSek) + " s efter " +
                       std::to_string(L->förstaMs - L->vidMs) + " ms");
             yta.setTicking(false);
             vid();
         }},
        {kPaus, vilar},
        {kVila,
         [=, this] {
             stilla("T setTicking(false) slutar — inga fler bildrutor");
             yta.onTick = nullptr;   // UTAN onTick tickar ingenting
             ned(gfx::Key::W);
             vid();
         }},
        {kPaus, vilar},
        {kVila,
         [=, this] {
             stilla("T utan onTick ger en tagen tangent inga bildrutor");
             släpp(gfx::Key::W);
             yta.onKeyDown = nullptr;
             blink();
         }},
    };
    nästa = 0;
    fortsätt();
}

// B. CARET-BLINKET genom pumpen: fältet på ritytan får fokus, och dess
// riktiga blinktimer (fönstrets, ~0,5 s) invaliderar bara careten. Ritytan
// under den ritas — kompositen — men 3D-målet ritas inte om.
void Värd::blink()
{
    nollställ(kamera({0.0, 0.0, 0.0}, 80.0, 35.0f, 40.0f));
    const gfx::Rectf  fb = fält.resolve(fält.behaviorCtx());
    const gfx::Pointf mitt{fb.x + fb.w / 2.0f, fb.y + fb.h / 2.0f};
    ner(mitt);
    upp(mitt);
    rita();
    const std::uint64_t r0 = målRitningar();
    const int           y0 = yta.ritningar, f0 = fält.ritningar;
    vidTimer = [this, r0, y0, f0] {
        check(fält.focused() && fält.ritningar >= f0 + 1 &&
                  yta.ritningar >= y0 + 1 && målRitningar() == r0,
              "B caret-blinket ritar ritytan (kompositen) men inte om 3D-målet",
              std::to_string(fält.ritningar - f0) + " blink, " +
                  std::to_string(yta.ritningar - y0) + " ritningar av ytan, " +
                  std::to_string(målRitningar() - r0) + " av målet");
        klar();
    };
    window().startTimer(kTimer, 1400);
}

void fallMedFönster()
{
    Värd w;
    w.setOffscreen(true);
    bool kördes = false;
    glaze::app().post([&] {
        kördes = true;
        w.synkront();
        // TVÅ steg: händelserna det synkrona steget postade körs före
        // tickningens mätning.
        glaze::app().post([&w] { w.tick1(); });   // stänger fönstret till sist
    });
    w.run();
    if (!kördes) {
        std::printf("  --   fönsterfallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    }
}

// T2. TICKEN RITAR OM RITYTAN, INTE FÖNSTRET:
// två ritytor med 3D-mål i samma fönster, och den ena tickar tio gånger.
// Fönstrets 3D-mål ritas om TIO gånger — den tickande ytans, ett per tick —
// och inte tjugo: en helfönster-invalidering efter handlern hade stegat den
// andra ytans stämpel också (`damageStamp`), och dess mål hade ritats om i
// varje bildruta.
struct TvåYtor : Värd {
    static constexpr int kTickar = 10;

    Yta           andra;          // en egen 3D-yta bredvid, som aldrig tickar
    int           tickar  = 0;
    std::uint64_t mål0    = 0;    // fönstrets målritningar vid första ticken
    int           andra0  = 0;    // den andra ytans ritningar likaså
    std::uint64_t gräns   = 0;    // ticksMs då väntan ger upp

    TvåYtor()
    {
        andra.name   = "andra";
        andra.x      = 520;
        andra.y      = 120;
        andra.width  = 180;
        andra.height = 140;
        andra.setBorder(false);
        root().adopt(andra);
    }

    void fall();
    void vänta();
};

void TvåYtor::fall()
{
    nollställ(kamera({0.0, 0.0, 0.0}, 80.0, 35.0f, 40.0f));
    andra.setCamera(kamera({0.0, 0.0, 0.0}, 60.0, 120.0f, 30.0f));

    // Först HJULETS och NYPNINGENS dolly, synkront: efter onCameraChange
    // ritas den ytans mål om, inte den andras — ett, inte två.
    yta.setZoomable(true);
    yta.onCameraChange = [this] { ++ändringar; };
    const gfx::Pointf p    = fys(200, 150);
    const auto        prov = [&](const char* vad,
                          const std::function<void()>& händelse) {
        rita();
        const std::uint64_t r = målRitningar();
        const int           ä = ändringar;
        händelse();
        rita();
        check(ändringar == ä + 1 && målRitningar() - r == 1, vad,
              std::to_string(ändringar - ä) + " ändringar, " +
                  std::to_string(målRitningar() - r) + " målritningar");
    };
    prov("T2 hjulets dolly i den ena av två 3D-ytor: onCameraChange ritar om "
         "ETT mål, inte två",
         [&] { hjul(p, 1.0f); });
    prov("T2 nypningens dolly likaså: ett mål, inte två", [&] {
        gest(p, gfx::GesturePhase::Begin, 1.2f, 0.0f);
        gest(p, gfx::GesturePhase::End, 1.0f, 0.0f);
    });
    yta.setZoomable(false);
    yta.onCameraChange = nullptr;

    // Därefter TICKEN, genom pumpen.
    rita();   // båda målen ritade — räkningen börjar efter dem
    yta.onTick = [this](glaze::Widget*, glaze::TickArgs&) {
        // Bildrutan som postade den första ticken (setTicking) räknas inte:
        // från här ritar varje tick EN bildruta.
        if (++tickar == 1) {
            mål0   = målRitningar();
            andra0 = andra.ritningar;
        }
        if (tickar == kTickar)
            yta.setTicking(false);
    };
    gräns = gfx::ticksMs() + 5000;
    yta.setTicking(true);
    vänta();
}

// Fönstrets timer frågar var 100:e ms tills den tionde ticken kommit. Dess
// bildruta är då redan ritad: timerns händelse kommer före svansen i ett
// varv, och pumpen ritar i samma varv som svansen körde ticken.
void TvåYtor::vänta()
{
    vidTimer = [this] {
        if (tickar < kTickar && gfx::ticksMs() < gräns) {
            vänta();
            return;
        }
        const std::uint64_t ritade = målRitningar() - mål0;
        check(tickar == kTickar && ritade == std::uint64_t(kTickar),
              "T2 tio tick i den ena av två 3D-ytor ritar om fönstrets 3D-mål "
              "tio gånger, inte tjugo — efter handlern ritas ritytan om, inte "
              "fönstret",
              std::to_string(tickar) + " tick, " + std::to_string(ritade) +
                  " målritningar, den andra ytan ritad " +
                  std::to_string(andra.ritningar - andra0) + " gånger");
        yta.onTick = nullptr;
        klar();
    };
    window().startTimer(kTimer, 100);
}

void fallTvåYtor()
{
    TvåYtor w;
    w.setOffscreen(true);
    bool kördes = false;
    glaze::app().post([&] {
        kördes = true;
        w.fall();   // stänger fönstret till sist
    });
    w.run();
    if (!kördes) {
        std::printf("  --   T2 hoppades över: inget fönster kunde skapas (ingen "
                    "display)\n");
        ++g_skips;
    }
}

// ================================================================ FÖR HAND
//
// `--hand`: ett SYNLIGT fönster för testpasset (doc/os/*/TESTS.md) — det
// grinden inte kan mäta, med riktig mus, styrplatta och tangentbord. Orbiten
// kring en kulle; FÖRSTAPERSONEN (verktygsknappen, som inte tar fokus) genom
// `lookFrom`, där ögat följer marken med `heightAt`; W/A/S/D (och Q/E upp och
// ner i orbiten) genom `onKeyDown`, som TAR tangenterna, och `onTick`, som
// läser `isKeyHeld`; och två textfält PÅ ritytan, det ena delvis utanför dess
// hörn (barnklippet). HUD:en räknar tickarna — står talet still står
// tickningen. Inget grindas, och fönstret stängs för hand.

constexpr double kÖgonhöjd = 1.8;   // m över marken i förstapersonen
constexpr double kGång     = 6.0;   // m/s i förstapersonen

// Kullen: markens höjd, och terrängmeshens lägen.
double kulle(double x, double y)
{
    const double dx = x - 30.0, dy = y - 20.0;
    return 12.0 * std::exp(-(dx * dx + dy * dy) / 1800.0);
}

struct Hand : glaze::AppWindow {
    glaze::Canvas     yta;
    glaze::TextInput  fält, klippt;
    glaze::ToolButton gåKnapp;
    long              tick = 0;

    Hand() : glaze::AppWindow("canvas3d_gate --hand", 1000, 680)
    {
        yta.name    = "yta";
        yta.x       = 12;
        yta.y       = 12;
        yta.right   = 12;
        yta.bottom  = 12;
        yta.anchors = {true, true, true, true};
        root().adopt(yta);
        fält.name  = "fält";
        fält.x     = 12;
        fält.y     = 12;
        fält.width = 240;
        fält.setText("skriv här: W A S D");
        yta.adopt(fält);
        gåKnapp.name = "gåKnapp";
        gåKnapp.x    = 264;
        gåKnapp.y    = 12;
        gåKnapp.setText("Förstaperson");
        gåKnapp.setCheckable(true);
        gåKnapp.onChange = [this] { växla(gåKnapp.isChecked()); };
        yta.adopt(gåKnapp);
        klippt.name    = "klippt";
        klippt.width   = 220;
        klippt.anchors = {false, false, true, true};
        klippt.right   = -90;   // delvis UTANFÖR ritytans hörn
        klippt.bottom  = -12;
        klippt.setText("klipps av ritytans hörn");
        yta.adopt(klippt);

        // MARKEN: en terrängmesh 200 × 200 m med 2 m rutor, färgad efter
        // höjden — ett Terrain-lager, så `heightAt` ser den.
        constexpr int   n = 101;
        glaze::MeshDesc d;
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                const double x = -100.0 + 2.0 * i, y = -100.0 + 2.0 * j;
                const double z = kulle(x, y);
                d.positions.push_back({static_cast<float>(x), static_cast<float>(y),
                                       static_cast<float>(z)});
                const auto t = static_cast<std::uint8_t>(z / 12.0 * 110.0);
                d.colors.push_back(gfx::Color{static_cast<std::uint8_t>(80 + t),
                                              static_cast<std::uint8_t>(150 - t / 2), 70,
                                              255});
            }
        for (int j = 0; j + 1 < n; ++j)
            for (int i = 0; i + 1 < n; ++i) {
                const auto a = static_cast<std::uint32_t>(j * n + i);
                const auto c = a + static_cast<std::uint32_t>(n);
                d.indices.insert(d.indices.end(), {a, a + 1, c + 1, a, c + 1, c});
            }
        yta.setLayers({glaze::TrText("mark"), glaze::TrText("lådor")});
        yta.setLayerPlacement(0, glaze::LayerPlacement::Terrain);
        yta.addShape(0, glaze::Shape::mesh(glaze::Mesh(std::move(d))));
        // LANDMÄRKENA: lådor om 4 × 4 × 10 m på marken.
        const struct {
            double     x, y;
            gfx::Color färg;
        } lådor[] = {{-40, -30, {200, 60, 50, 255}},  {10, 60, {50, 90, 200, 255}},
                     {60, -50, {230, 180, 40, 255}},  {30, 20, {240, 240, 240, 255}},
                     {-70, 50, {150, 70, 190, 255}}};
        for (const auto& l : lådor) {
            const double z = kulle(l.x, l.y);
            yta.addShape(1, glaze::Shape::box(glaze::Box3{{l.x - 2, l.y - 2, z},
                                                          {l.x + 2, l.y + 2, z + 10}},
                                              glaze::MeshStyle{.color = l.färg}));
        }

        yta.setCamera(glaze::Camera3D{.target    = {0, 0, 0},
                                      .distance  = 140,
                                      .azimuth   = 30,
                                      .elevation = 30});
        yta.setPannable(true);
        yta.setZoomable(true);
        yta.setRotatable(true);
        yta.setPitchable(true);
        yta.setDistanceRange(5, 600);
        yta.onKeyDown = [](glaze::Widget*, glaze::KeyArgs& e) {
            switch (e.key) {
            case gfx::Key::W:
            case gfx::Key::A:
            case gfx::Key::S:
            case gfx::Key::D:
            case gfx::Key::Q:
            case gfx::Key::E:
                e.handled = true;   // tagen ⇒ ritytan tickar medan den hålls
                break;
            default:
                break;
            }
        };
        yta.onTick = [this](glaze::Widget*, glaze::TickArgs& e) { gå(e.seconds); };
        yta.onPaintOverlay = [this](glaze::Widget*, glaze::PaintArgs& e) {
            hud(e.painter);
        };
    }

    // Förstapersonen står i orbitens mål och tittar åt samma håll; orbiten
    // tillbaka ställer målet där ögat står.
    void växla(bool gående)
    {
        const glaze::Camera3D k = yta.camera3D();
        if (gående) {
            const double z = yta.heightAt({k.target.x, k.target.y}).value_or(0.0);
            yta.setCamera(glaze::Camera3D::lookFrom({k.target.x, k.target.y, z + kÖgonhöjd},
                                                    k.azimuth, 0.0f));
        } else {
            const glaze::Point3 öga = k.eye();
            const double        z   = yta.heightAt({öga.x, öga.y}).value_or(0.0);
            yta.setCamera(glaze::Camera3D{.target    = {öga.x, öga.y, z},
                                          .distance  = 140,
                                          .azimuth   = k.azimuth,
                                          .elevation = 30});
        }
    }

    void gå(double sekunder)
    {
        ++tick;
        const auto   håll  = [this](gfx::Key t) { return isKeyHeld(t) ? 1.0 : 0.0; };
        const double fram  = håll(gfx::Key::W) - håll(gfx::Key::S);
        const double höger = håll(gfx::Key::D) - håll(gfx::Key::A);
        const double upp   = håll(gfx::Key::E) - håll(gfx::Key::Q);
        glaze::Camera3D k  = yta.camera3D();
        const bool   gående = gåKnapp.isChecked();
        const double v      = (gående ? kGång : 0.5 * k.distance) * sekunder;
        // Kompassriktningen: azimuten 0 = norr (+y), medurs.
        const double az = rad(k.azimuth);
        const double dx = (std::sin(az) * fram + std::cos(az) * höger) * v;
        const double dy = (std::cos(az) * fram - std::sin(az) * höger) * v;
        if (gående) {
            glaze::Point3 öga = k.eye();
            öga.x += dx;
            öga.y += dy;
            öga.z = yta.heightAt({öga.x, öga.y}).value_or(0.0) + kÖgonhöjd;
            yta.setCamera(glaze::Camera3D::lookFrom(öga, k.azimuth, -k.elevation));
        } else {
            k.target.x += dx;
            k.target.y += dy;
            k.target.z += upp * v;
            yta.setCamera(k);
        }
    }

    void hud(glaze::Painter& p)
    {
        glaze::TextStyle s;
        s.anchor     = glaze::TextAnchor::TopRight;
        s.align      = glaze::Align::Left;
        s.color      = gfx::Color{20, 20, 20, 255};
        s.background = gfx::Color{255, 255, 255, 220};
        s.padding    = glaze::px(6.0f);
        s.collide    = false;
        const std::string text =
            std::string("W/A/S/D gå · Q/E upp och ner (orbit)\n"
                        "dra: kretsa · mitten/Skift+dra: panorera · hjul: närmare\n"
                        "styrplattan: dra panorerar · Ctrl+dra kretsar · nyp · vrid\n") +
            "tick: " + std::to_string(tick);
        p.drawText({p.size().w - 12.0, 12.0}, glaze::TrText(text), s);
    }
};

int förHand()
{
    Hand w;
    return w.run();
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
        if (std::strcmp(argv[i], "--hand") == 0)
            return förHand();
    }
    std::printf("=== canvas3d_gate — ritytans interaktion i 3D ===\n");
    fallH();
    fallMedFönster();
    fallTvåYtor();
    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf(g_fel ? "=== canvas3d_gate: %d FEL av %d ===\n"
                      : "=== canvas3d_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    if (g_fel)
        return 1;
    return g_skips ? 77 : 0;
}
