// binding_gate — RITYTANS BINDNINGSTABELL: uppslaget
// headless, mängderna headless, och tabellen genom FÖNSTRETS ingång
// (`AppWindow::handleEvent`, som i canvas_gate) i ett DOLT fönster med
// en riktig `glaze::Canvas`.
//
// Kör:  ./build/tests/binding_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//
// VAD GRINDEN PÅSTÅR:
//   T   uppslaget (src/canvas/input_map): en rad matchar när dess
//       modifierare är en DELMÄNGD av de tryckta, den mest specifika vinner,
//       vid lika Alt före Shift före Ctrl, Cmd är Ctrl, ritytans rad ersätter
//       appens med SAMMA nyckel men ärver de andra, `remove` återställer
//       arvet, ett eget läge utan träff faller tillbaka på det automatiska
//       (ramverkets lägen aldrig), `None` skuggar och är obunden, dragets
//       plan faller till en mindre specifik rad när allt är avstängt, och en
//       ogiltig rad (fart ≤ 0, NaN, tomt läge) avvisas.
//   A   mängderna (camera_math `bindingAmount`) mot handräknade tal per
//       källa och handling, dollyn 2^(−nivåer), och tecknet: fart ×
//       riktning × källans vändning × handlingens vändning, ritytans före
//       appens.
//   D   FÖRVALEN ÄR RITYTANS GESTER — med värden ur canvas_gate, trackpad_gate
//       och canvas3d_gate (deras egna handräkningar, aldrig tabellen):
//       vänsterdraget greppar, Ctrl+drag 0,8°/0,5° per px, hjulet √2 per rad
//       mot pekaren, och i orbiten orbitar Alt+vänsterdrag som utan Alt.
//   I   vändningarna: källans (hjulet, inte + och −), handlingens per enhet
//       (+ och −, inte hjulet), mus och styrplatta var för sig, appens
//       ärvs och ritytans vinner.
//   R   omflyttningarna: ScrollX → Rotate vrider, WheelY → PanY panorerar ett
//       pilsteg per rad, en KORSAD bindning (LeftDragY → PanX) panorerar i
//       sidled, farten 2 på den virtuella pekaren, orbitens vänsterdrag
//       panorerar och mittendraget orbitar, och högerknappen: bunden TAR
//       trycket (ingen snabbmeny), `None` och obunden ger snabbmenyn.
//   B   bara en axel som RÖR SIG och har en verksam bindning tar hjulet:
//       ScrollY → None med ScrollX → PanX låter ett lodrätt drag bubbla.
//   S   en förmåga som stängs av mitt i ett drag stänger bara av sina
//       handlingar: tiltens sättare stoppar inte ett zoomdrag, zoomens gör.
//   M   ett eget läge ("mäta") med vänsterdraget `None` gör draget overksamt
//       och lämnar hjulet åt det automatiska; tillbaka till "" panorerar det.
//   F   förstapersonen: draget ger gir kring ÖGAT (ögat står kvar), hjulet
//       flyttar ögat en enhet framåt, Shift+Upp höjer en enhet — Z och Y.
//   O   flygläget som rader (tests/orbit_fly.h, ersättningen för
//       `OrbitPan::Fly`): ett mittendrag nedåt flyger utan att ändra höjden,
//       och Shift tryckt MITT I draget byter till höjning (läget läses per
//       rörelse).
//
// VAD DEN INTE MÄTER: riktiga mushjul och styrplattor (OS:ens naturliga
// rullning, Direct Manipulation, AppKit) — fasaden matas med syntetiska
// event; hur förvalens fart KÄNNS; att förstapersonens förval är de rätta
// (en smaksak); orbitens fullständiga panorering (canvas3d_gate P mäter
// det med samma rader); pekarens form under ett drag.
//
// SABOTAGEN som ska fälla den (skrivna): specificitetssorteringen
// borttagen (T), exakt matchning i stället för delmängd (T, D Alt+drag),
// ritytans rader ersätter hela appens tabell (T, I), handlingsvändningen
// nycklad på källan i stället för enheten (I), `None` som inte skuggar i
// dragets reserv (T), hjulet taget av en axel utan rörelse (B), sättarna som
// avbryter hela draget (S), den virtuella pekaren utan
// faktor (R), dollyn utan exponentens tecken (A), `lookAround` kring målet
// (F), flygläget läst bara vid trycket (O).

#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

#include <gfx/camera.h>
#include <glaze/glaze.h>

#include "canvas/camera_math.h"   // intern — mängderna (A)
#include "canvas/input_map.h"     // intern — uppslaget (T)
#include "orbit_fly.h"            // flygläget som rader (O)

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
            std::printf("  ok   %s\n", vad.c_str());
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

bool näraP(const glaze::Point& a, const glaze::Point& b, double e)
{
    return nära(a.x, b.x, e) && nära(a.y, b.y, e);
}

std::string pt(const glaze::Point& p)
{
    return "(" + tal(p.x) + ", " + tal(p.y) + ")";
}

using S = glaze::InputSource;
using A = glaze::InputAction;
using D = glaze::InputDirection;
namespace d = glaze::detail;

constexpr std::uint32_t kCtrl  = gfx::kModCtrl;
constexpr std::uint32_t kShift = gfx::kModShift;
constexpr std::uint32_t kAlt   = gfx::kModAlt;

glaze::InputBinding rad(const char* mode, S s, std::uint32_t mods, A a,
                        D dir = D::Normal, float fart = 1.0f)
{
    return glaze::InputBinding{mode, s, mods, a, dir, fart};
}

A handling(const glaze::InputBinding* b)
{
    return b ? b->action : static_cast<A>(255);
}

// ================================================================ T
void fallT()
{
    d::InputMap app;
    app.set(rad("m", S::WheelY, 0, A::Zoom));
    app.set(rad("m", S::WheelY, kCtrl, A::Rotate));
    check(handling(d::lookupBinding(nullptr, app, "m", S::WheelY, kCtrl | kShift)) ==
                  A::Rotate &&
              handling(d::lookupBinding(nullptr, app, "m", S::WheelY, kShift)) == A::Zoom,
          "T delmängd: Ctrl+Shift matchar Ctrl-raden, Shift bara {}");
    app.set(rad("m", S::WheelY, kCtrl | kShift, A::Tilt));
    check(handling(d::lookupBinding(nullptr, app, "m", S::WheelY,
                                    kCtrl | kShift | kAlt)) == A::Tilt,
          "T den mest specifika vinner (Ctrl+Shift före Ctrl och {})");
    d::InputMap lika;
    lika.set(rad("m", S::WheelY, kCtrl, A::Rotate));
    lika.set(rad("m", S::WheelY, kShift, A::PanY));
    lika.set(rad("m", S::WheelY, kAlt, A::PanX));
    check(handling(d::lookupBinding(nullptr, lika, "m", S::WheelY,
                                    kCtrl | kShift | kAlt)) == A::PanX &&
              handling(d::lookupBinding(nullptr, lika, "m", S::WheelY,
                                        kCtrl | kShift)) == A::PanY,
          "T vid lika antal bitar: Alt före Shift före Ctrl");
    check(handling(d::lookupBinding(nullptr, app, "m", S::WheelY, gfx::kModCmd)) ==
                  A::Rotate,
          "T Cmd räknas som Ctrl");
    d::InputMap cmd;
    cmd.set(rad("m", S::WheelX, gfx::kModCmd, A::Zoom));
    check(cmd.find("m", S::WheelX, kCtrl) != nullptr,
          "T en rad med Cmd lagras som Ctrl");

    // Arvet per rad.
    d::InputMap egen;
    egen.set(rad("m", S::WheelY, 0, A::PanY));
    check(handling(d::lookupBinding(&egen, app, "m", S::WheelY, 0)) == A::PanY &&
              handling(d::lookupBinding(&egen, app, "m", S::WheelY, kCtrl)) ==
                  A::Rotate,
          "T ritytans rad ersätter appens med samma nyckel — de andra ärvs");
    egen.remove("m", S::WheelY, 0);
    check(handling(d::lookupBinding(&egen, app, "m", S::WheelY, 0)) == A::Zoom,
          "T remove återställer arvet");

    // Lägenas reserv.
    const d::InputMap förval = d::InputMap::defaults();
    check(d::groupMode(nullptr, förval, "mäta", "map", S::WheelY, 0) == "map",
          "T ett eget läge utan träff faller tillbaka på det automatiska");
    d::InputMap mät;
    mät.set(rad("mäta", S::WheelX, 0, A::None));
    check(d::groupMode(&mät, förval, "mäta", "map", S::WheelY, 0) == "mäta" &&
              d::groupMode(&mät, förval, "mäta", "map", S::ArrowsY, 0) == "map",
          "T en träff i gruppen (hjulets andra axel) håller läget; en annan grupp "
          "faller tillbaka");
    check(d::groupMode(nullptr, förval, "firstPerson", "orbit", S::RightDragX, 0) ==
              "firstPerson",
          "T ramverkets lägen faller aldrig tillbaka");

    // None: skuggar den mindre specifika och är obunden.
    d::InputMap ingen = förval;
    ingen.set(rad("map", S::WheelY, kShift, A::None));
    const std::uint32_t allt = ~0u;
    check(handling(d::lookupBinding(nullptr, ingen, "map", S::WheelY, kShift)) ==
                  A::None &&
              d::resolveEvent(nullptr, ingen, "map", S::WheelY, kShift, allt) ==
                  nullptr,
          "T None skuggar {}-raden och är obunden (fynd 3)");

    // Dragets plan.
    const auto bit = [](A a) { return 1u << static_cast<unsigned>(a); };
    const d::DragPlan p1 = d::resolveDrag(nullptr, förval, "map", S::LeftDragX,
                                          S::LeftDragY, kCtrl, bit(A::PanX) | bit(A::PanY));
    const d::DragPlan p2 = d::resolveDrag(nullptr, förval, "map", S::LeftDragX,
                                          S::LeftDragY, kCtrl,
                                          bit(A::PanX) | bit(A::PanY) | bit(A::Rotate));
    check(handling(p1.x) == A::PanX && handling(p1.y) == A::PanY &&
              handling(p2.x) == A::Rotate && p2.y == nullptr,
          "T dragets plan: Ctrl utan vridning och tilt faller till panoreringen; "
          "med bara vridningen vrider X och Y står still");
    d::InputMap höger = förval;
    höger.set(rad("map", S::RightDragX, 0, A::PanX));
    höger.set(rad("map", S::RightDragY, 0, A::PanY));
    höger.set(rad("map", S::RightDragX, kShift, A::None));
    höger.set(rad("map", S::RightDragY, kShift, A::None));
    const d::DragPlan h1 = d::resolveDrag(nullptr, höger, "map", S::RightDragX,
                                          S::RightDragY, kShift, ~0u);
    const d::DragPlan h0 = d::resolveDrag(nullptr, höger, "map", S::RightDragX,
                                          S::RightDragY, 0, ~0u);
    check(!h1.any() && handling(h0.x) == A::PanX,
          "T None SKUGGAR också i dragets reserv: Shift+högerdraget är obundet fast "
          "{}-raden panorerar");

    // Ogiltigt.
    d::InputMap ogiltig;
    check(!ogiltig.set(rad("m", S::WheelY, 0, A::Zoom, D::Normal, 0.0f)) &&
              !ogiltig.set(rad("m", S::WheelY, 0, A::Zoom, D::Normal, NAN)) &&
              !ogiltig.set(rad("", S::WheelY, 0, A::Zoom)) && ogiltig.rows().empty(),
          "T fart 0, NaN och ett tomt läge avvisas");
}

// ================================================================ A
void fallA()
{
    using K = d::MotionKind;
    const float steg = 40.0f, pil = 48.0f;
    const auto  m = [&](A a, S s, double r, K k = K::Map) {
        return d::bindingAmount(a, s, r, k, steg, pil);
    };
    check(m(A::PanX, S::LeftDragX, 12.0) == 12.0 && m(A::PanY, S::ScrollY, -7.0) == -7.0,
          "A dragets och styrplattans px är panoreringens px");
    check(m(A::PanY, S::WheelY, 2.0) == 2.0 * pil && m(A::PanX, S::ArrowsX, -1.0) == -pil,
          "A hjulets rader och pilarnas steg panorerar ett pilsteg var");
    check(m(A::Zoom, S::WheelY, 1.0) == 0.5 && m(A::Zoom, S::PlusMinus, 1.0) == 1.0 &&
              nära(m(A::Zoom, S::Pinch, std::log(2.0)), 1.0, 1e-12) &&
              nära(m(A::Zoom, S::LeftDragY, 80.0), 1.0, 1e-12),
          "A zoomen: en rad ½ nivå, + en nivå, nypningen ln 2 en nivå, draget två "
          "hjulsteg en nivå");
    check(nära(m(A::Rotate, S::LeftDragX, 10.0), 8.0, 1e-5) &&
              nära(m(A::Tilt, S::MiddleDragY, 10.0), 5.0, 1e-5) &&
              m(A::Rotate, S::ArrowsX, 1.0) == 15.0 && m(A::Tilt, S::ArrowsY, 1.0) == 10.0 &&
              m(A::Rotate, S::Twist, 7.0) == 7.0 && m(A::Rotate, S::WheelY, 1.0) == 15.0,
          "A vridningen och tilten: 0,8°/0,5° per px, 15°/10° per pil, vridningen "
          "i grader, hjulet ett pilsteg");
    check(m(A::Fly, S::WheelY, 2.0, K::FirstPerson) == 2.0 &&
              nära(m(A::Fly, S::LeftDragY, 120.0, K::FirstPerson), 3.0, 1e-12) &&
              m(A::Lift, S::ArrowsY, -1.0, K::FirstPerson) == -1.0,
          "A förstapersonen: en världsenhet per hjulsteg");
    check(d::zoomLevelsFactor(1.0) == 2.0 && d::dollyLevelsFactor(1.0) == 0.5 &&
              d::dollyLevelsFactor(-2.0) == 4.0,
          "A zoomen 2^nivåer och dollyn 2^(−nivåer)");
    d::InputMap app;
    app.setSourceInvert(S::WheelY, true);
    const glaze::InputBinding b = rad("m", S::WheelY, 0, A::Zoom, D::Inverted, 1.5f);
    d::InputMap egen;
    check(d::bindingFactor(nullptr, app, b) == 1.5,
          "A tecknet: Inverted × källans vändning = +, gånger farten");
    egen.setSourceInvert(S::WheelY, false);
    egen.setActionInvert(glaze::InputDevice::Mouse, A::Zoom, true);
    check(d::bindingFactor(&egen, app, b) == 1.5,
          "A ritytans satta vändning vinner över appens (källan av, handlingen på)");
    check(d::bindingFactor(&egen, d::InputMap{}, rad("m", S::Pinch, 0, A::Zoom)) == 1.0,
          "A handlingens vändning gäller enheten: musens rör inte nypningen");
}

// ================================================================ fönstret

using P = gfx::GesturePhase;

struct Värd : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    glaze::Panel  rullpanel;   // en rullande förälder — bubblingens mål
    glaze::Label  fyll;        // gör panelen högre än sin yta
    glaze::Canvas yta;
    int           menyer = 0;

    Värd() : glaze::AppWindow("binding_gate", 640, 480, gfx::Chrome::None)
    {
        yta.name   = "yta";
        yta.x      = 20;
        yta.y      = 20;
        yta.width  = 400;
        yta.height = 300;
        yta.setBorder(false);
        yta.onContextMenu = [this] { ++menyer; };
        rullpanel.name   = "rullpanel";
        rullpanel.width  = 640;
        rullpanel.height = 480;
        root().adopt(rullpanel);
        fyll.name = "fyll";
        fyll.x    = 10;
        fyll.y    = 1200;
        fyll.setText("fyll");
        rullpanel.adopt(fyll);
        rullpanel.adopt(yta);
    }

    gfx::Pointf fys(double lx, double ly)
    {
        const gfx::Rectf b = yta.resolve(yta.behaviorCtx());
        const float      s = window().uiScale();
        return gfx::Pointf{static_cast<float>(b.x + lx * s),
                           static_cast<float>(b.y + ly * s)};
    }
    void mus(gfx::EventType t, double lx, double ly, std::uint32_t mods = 0,
             gfx::MouseButton b = gfx::MouseButton::Left)
    {
        const gfx::Pointf p = fys(lx, ly);
        gfx::Event        e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = t == gfx::EventType::MouseMotion ? gfx::MouseButton::None : b;
        e.mods     = mods;
        handleEvent(e);
    }
    // Ett helt drag från (ax, ay) till (bx, by), lokala logiska px.
    void drag(double ax, double ay, double bx, double by, std::uint32_t mods = 0,
              gfx::MouseButton b = gfx::MouseButton::Left)
    {
        mus(gfx::EventType::MouseButtonDown, ax, ay, mods, b);
        mus(gfx::EventType::MouseMotion, bx, by, mods, b);
        mus(gfx::EventType::MouseButtonUp, bx, by, mods, b);
    }
    void hjul(double lx, double ly, float wx, float wy, bool precise = false,
              std::uint32_t mods = 0)
    {
        const gfx::Pointf p = fys(lx, ly);
        gfx::Event        e;
        e.type     = gfx::EventType::MouseWheel;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.wheelX   = wx;
        e.wheelY   = wy;
        e.precise  = precise;
        e.phase    = precise ? P::Update : P::None;
        e.mods     = mods;
        handleEvent(e);
    }
    void tangent(gfx::Key k, std::uint32_t mods = 0)
    {
        for (const gfx::EventType t : {gfx::EventType::KeyDown, gfx::EventType::KeyUp}) {
            gfx::Event e;
            e.type     = t;
            e.windowId = window().id();
            e.key      = k;
            e.mods     = mods;
            handleEvent(e);
        }
    }
    void rita() { window().renderFrame(); }
    glaze::Point värld(double lx, double ly)
    {
        const auto w = yta.toWorld(glaze::Point{lx, ly});
        return w ? *w : glaze::Point{NAN, NAN};
    }
    glaze::Point lokal(const glaze::Point& w)
    {
        const auto p = yta.toLocal(w);
        return p ? *p : glaze::Point{NAN, NAN};
    }

    // Varje fall börjar från samma läge: kartkameran rakt uppifrån, alla
    // gester på, inga egna rader eller vändningar, appens förval.
    void nollställ()
    {
        glaze::app().resetCanvasBindings();
        yta.clearBindings();
        for (const S s : {S::WheelX, S::WheelY, S::ScrollX, S::ScrollY})
            yta.clearSourceInvert(s);
        for (const glaze::InputDevice dv : {glaze::InputDevice::Mouse,
                                            glaze::InputDevice::Touchpad,
                                            glaze::InputDevice::Keyboard})
            yta.clearActionInvert(dv, A::Zoom);
        yta.setInputMode("");
        yta.setPannable(true);
        yta.setZoomable(true);
        yta.setRotatable(true);
        yta.setPitchable(true);
        yta.setInertia(false);
        yta.setRotationSnap(0.0f);
        yta.setYUp(false);
        yta.setCamera(glaze::Camera{{0.0, 0.0}, 2.0, 0.0f, 0.0f});
        yta.focus();
        menyer = 0;
        rullpanel.setScroll(0.0f, 0.0f);
        invalidate();
        rita();
    }
    void orbitKamera(float el)
    {
        glaze::Camera3D k;
        k.target    = {100.0, 200.0, 5.0};
        k.distance  = 60.0;
        k.azimuth   = 20.0f;
        k.elevation = el;
        yta.setCamera(k);
        rita();
    }

    void kör();
};

void Värd::kör()
{
    const double tol = 0.03;   // en trettiondels logisk px

    // ---- D. FÖRVALEN ÄR RITYTANS GESTER
    nollställ();
    {
        const glaze::Point w = värld(100, 100);
        drag(100, 100, 140, 130);
        check(näraP(lokal(w), glaze::Point{140, 130}, tol),
              "D vänsterdraget greppar: världspunkten följer pekaren",
              pt(lokal(w)));
    }
    nollställ();
    drag(200, 150, 250, 130, kCtrl);
    check(nära(yta.bearing(), 40.0, 0.05) && nära(yta.pitch(), 10.0, 0.05),
          "D Ctrl+drag: 0,8° bäring per px vågrätt och 0,5° tilt per px uppåt "
          "(canvas_gate R)",
          tal(yta.bearing()) + "° / " + tal(yta.pitch()) + "°");
    nollställ();
    {
        const glaze::Point w  = värld(310, 70);
        const double       z0 = yta.zoom();
        hjul(310, 70, 0.0f, 1.0f);
        rita();
        check(nära(yta.zoom(), z0 * std::sqrt(2.0), 1e-9) &&
                  näraP(värld(310, 70), w, 1e-6),
              "D hjulet: en rad √2 mot pekaren (canvas_gate Z)");
    }
    nollställ();
    orbitKamera(30.0f);
    drag(200, 150, 250, 170, kAlt);
    check(nära(yta.camera3D().azimuth, 60.0, 0.05) &&
              nära(yta.camera3D().elevation, 40.0, 0.05),
          "D orbiten: Alt+vänsterdrag orbitar som utan Alt (delmängden — "
          "canvas3d_gate O)",
          tal(yta.camera3D().azimuth) + "° / " + tal(yta.camera3D().elevation) + "°");

    // ---- I. VÄNDNINGARNA
    nollställ();
    glaze::app().setCanvasSourceInvert(S::WheelY, true);
    double z0 = yta.zoom();
    hjul(200, 150, 0.0f, 1.0f);
    const bool ärvd = nära(yta.zoom(), z0 / std::sqrt(2.0), 1e-9);
    yta.setSourceInvert(S::WheelY, false);
    z0 = yta.zoom();
    hjul(200, 150, 0.0f, 1.0f);
    const bool egen = nära(yta.zoom(), z0 * std::sqrt(2.0), 1e-9);
    yta.clearSourceInvert(S::WheelY);
    z0 = yta.zoom();
    hjul(200, 150, 0.0f, 1.0f);
    const bool igen = nära(yta.zoom(), z0 / std::sqrt(2.0), 1e-9);
    z0 = yta.zoom();
    tangent(gfx::Key::Plus);
    check(ärvd && egen && igen && nära(yta.zoom(), 2.0 * z0, 1e-9),
          "I appens källvändning ärvs, ritytans vinner, clear ärver igen — och + "
          "påverkas inte av hjulets vändning");
    nollställ();
    yta.setActionInvert(glaze::InputDevice::Keyboard, A::Zoom, true);
    z0 = yta.zoom();
    tangent(gfx::Key::Plus);
    const bool tangentVänd = nära(yta.zoom(), 0.5 * z0, 1e-9);
    z0 = yta.zoom();
    hjul(200, 150, 0.0f, 1.0f);
    check(tangentVänd && nära(yta.zoom(), z0 * std::sqrt(2.0), 1e-9),
          "I handlingens vändning för tangentbordet vänder + men inte hjulet");
    nollställ();
    yta.setBinding("map", S::ScrollY, 0, A::Zoom);
    yta.setActionInvert(glaze::InputDevice::Touchpad, A::Zoom, true);
    z0 = yta.zoom();
    hjul(200, 150, 0.0f, 2.0f, true);
    const bool platta = yta.zoom() < z0;
    z0 = yta.zoom();
    hjul(200, 150, 0.0f, 1.0f, false);
    check(platta && nära(yta.zoom(), z0 * std::sqrt(2.0), 1e-9),
          "I mus och styrplatta var för sig: styrplattans zoom vänd, mushjulets inte");

    // ---- R. OMFLYTTNINGARNA
    nollställ();
    yta.setBinding("map", S::ScrollX, 0, A::Rotate);
    hjul(200, 150, -2.0f, 0.0f, true);   // innehållet +20 logiska px åt höger
    check(nära(yta.bearing(), 2.0 * gfx::kPrecisePxPerRow * 0.8, 1e-3),
          "R tvåfingersdraget i sidled VRIDER: 0,8° per px", tal(yta.bearing()));
    nollställ();
    yta.setBinding("map", S::WheelY, 0, A::PanY);
    {
        const glaze::Point w = värld(200, 150);
        hjul(200, 150, 0.0f, 1.0f);
        rita();
        const double steg = style().canvasPanStep;
        check(näraP(lokal(w), glaze::Point{200, 150 + steg}, tol),
              "R hjulet PANORERAR: en rad flyttar innehållet ett pilsteg nedåt",
              pt(lokal(w)));
    }
    nollställ();
    yta.setBinding("map", S::LeftDragY, 0, A::PanX);
    yta.setBinding("map", S::LeftDragX, 0, A::PanY);
    {
        const glaze::Point w = värld(150, 100);
        drag(150, 100, 150, 130);
        check(näraP(lokal(w), glaze::Point{180, 100}, tol),
              "R en KORSAD bindning: ett drag nedåt panorerar i sidled (fynd 1)",
              pt(lokal(w)));
    }
    nollställ();
    yta.setBinding("map", S::LeftDragX, 0, A::PanX, D::Normal, 2.0f);
    {
        const glaze::Point w = värld(150, 100);
        drag(150, 100, 170, 100);
        check(näraP(lokal(w), glaze::Point{190, 100}, tol),
              "R farten 2: den virtuella pekaren går dubbelt så långt",
              pt(lokal(w)));
    }
    nollställ();
    orbitKamera(30.0f);
    yta.setBinding("orbit", S::LeftDragX, 0, A::PanX);
    yta.setBinding("orbit", S::LeftDragY, 0, A::PanY);
    yta.setBinding("orbit", S::MiddleDragX, 0, A::Rotate);
    yta.setBinding("orbit", S::MiddleDragY, 0, A::Tilt);
    {
        const glaze::Camera3D k0 = yta.camera3D();
        drag(200, 150, 250, 150);
        const glaze::Camera3D k1 = yta.camera3D();
        drag(200, 150, 250, 150, 0, gfx::MouseButton::Middle);
        const glaze::Camera3D k2 = yta.camera3D();
        check(k1.azimuth == k0.azimuth && k1.target.x != k0.target.x &&
                  nära(k2.azimuth, k0.azimuth + 40.0, 0.05),
              "R orbiten: vänsterdraget panorerar och mittendraget orbitar");
    }
    nollställ();
    yta.setBinding("map", S::RightDragX, 0, A::PanX);
    yta.setBinding("map", S::RightDragY, 0, A::PanY);
    {
        const glaze::Point w = värld(150, 100);
        drag(150, 100, 190, 100, 0, gfx::MouseButton::Right);
        const bool tog = menyer == 0 && näraP(lokal(w), glaze::Point{190, 100}, tol);
        yta.setBinding("map", S::RightDragX, 0, A::None);
        yta.setBinding("map", S::RightDragY, 0, A::None);
        drag(150, 100, 150, 100, 0, gfx::MouseButton::Right);
        const int medNone = menyer;
        yta.clearBindings();
        drag(150, 100, 150, 100, 0, gfx::MouseButton::Right);
        check(tog && medNone == 1 && menyer == 2,
              "R högerdraget bundet TAR trycket (ingen snabbmeny); None och "
              "obundet ger snabbmenyn",
              std::to_string(menyer) + " menyer");
    }

    // ---- B. BARA EN AXEL SOM RÖR SIG TAR HJULET: ScrollY är
    // None och ScrollX panorerar — ett rent lodrätt tvåfingersdrag bubblar till
    // den rullande panelen, och kameran står still.
    nollställ();
    yta.setBinding("map", S::ScrollY, 0, A::None);
    {
        const glaze::Camera före = yta.camera();
        hjul(200, 150, 0.0f, -1.0f, true);
        check(rullpanel.scrollY() > 0.0f && yta.camera().center.x == före.center.x &&
                  yta.camera().center.y == före.center.y,
              "B ett lodrätt drag med ScrollY → None bubblar fast ScrollX panorerar",
              tal(rullpanel.scrollY()));
    }

    // ---- S. EN FÖRMÅGA AV MITT I DRAGET: vridningens och
    // tiltens sättare stoppar inte ett rent zoomdrag; zoomens gör det.
    nollställ();
    yta.setBinding("map", S::LeftDragX, 0, A::None);
    yta.setBinding("map", S::LeftDragY, 0, A::Zoom);
    {
        mus(gfx::EventType::MouseButtonDown, 200, 150);
        mus(gfx::EventType::MouseMotion, 200, 190);
        const double z1 = yta.zoom();
        yta.setRotatable(false);
        yta.setPitchable(false);
        mus(gfx::EventType::MouseMotion, 200, 230);
        const double z2 = yta.zoom();
        yta.setZoomable(false);
        mus(gfx::EventType::MouseMotion, 200, 270);
        const double z3 = yta.zoom();
        mus(gfx::EventType::MouseButtonUp, 200, 270);
        check(z1 > 2.0 && z2 > z1 && z3 == z2,
              "S vridningens och tiltens sättare stoppar inte zoomdraget; zoomens gör",
              tal(z1) + " → " + tal(z2) + " → " + tal(z3));
    }

    // ---- M. LÄGENA
    nollställ();
    yta.setBinding("mäta", S::LeftDragX, 0, A::None);
    yta.setBinding("mäta", S::LeftDragY, 0, A::None);
    yta.setInputMode("mäta");
    {
        const glaze::Camera före = yta.camera();
        drag(150, 100, 200, 140);
        const bool stilla = yta.camera().center.x == före.center.x &&
                            yta.camera().center.y == före.center.y;
        const double z = yta.zoom();
        hjul(200, 150, 0.0f, 1.0f);
        const bool zoom = nära(yta.zoom(), z * std::sqrt(2.0), 1e-9);
        yta.setInputMode("");
        const glaze::Point w = värld(150, 100);
        drag(150, 100, 200, 140);
        check(stilla && zoom && näraP(lokal(w), glaze::Point{200, 140}, tol) &&
                  yta.activeInputMode() == "map",
              "M \"mäta\": vänsterdraget overksamt, hjulet det automatiska; "
              "tillbaka till \"\" panorerar det");
    }

    // ---- F. FÖRSTAPERSONEN
    nollställ();
    yta.setInputMode("firstPerson");
    yta.setCamera(glaze::Camera3D::lookFrom({10.0, 20.0, 2.0}, 0.0f, 0.0f, 60.0f));
    rita();
    {
        const glaze::Point3 öga = yta.camera3D().eye();
        drag(200, 150, 250, 150);
        const glaze::Point3 e1  = yta.camera3D().eye();
        const bool          gir = nära(yta.camera3D().azimuth, 40.0, 0.05) &&
                         nära(e1.x, öga.x, 1e-6) && nära(e1.y, öga.y, 1e-6) &&
                         nära(e1.z, öga.z, 1e-6);
        yta.setCamera(glaze::Camera3D::lookFrom({10.0, 20.0, 2.0}, 0.0f, 0.0f, 60.0f));
        hjul(200, 150, 0.0f, 1.0f);
        const glaze::Point3 e2 = yta.camera3D().eye();
        tangent(gfx::Key::Up, kShift);
        const glaze::Point3 e3 = yta.camera3D().eye();
        check(gir && nära(e2.y, 21.0, 1e-9) && nära(e2.x, 10.0, 1e-9) &&
                  nära(e3.z, 3.0, 1e-9),
              "F draget girar kring ögat, hjulet en enhet framåt (norr), "
              "Shift+Upp en enhet uppåt");
        yta.setCamera(glaze::Camera3D::lookFrom({0.0, 2.0, 0.0}, 0.0f, 0.0f, 60.0f,
                                                glaze::UpAxis::Y));
        tangent(gfx::Key::Up, kShift);
        check(nära(yta.camera3D().eye().y, 3.0, 1e-9),
              "F Shift+Upp höjer längs Y med UpAxis::Y");
    }

    // ---- O. FLYGLÄGET SOM RADER, läget per rörelse
    nollställ();
    orbitKamera(0.0f);
    gate::orbitFly(yta);
    {
        const glaze::Camera3D k0 = yta.camera3D();
        mus(gfx::EventType::MouseButtonDown, 200, 100, 0, gfx::MouseButton::Middle);
        mus(gfx::EventType::MouseMotion, 200, 140);
        const glaze::Camera3D k1 = yta.camera3D();
        mus(gfx::EventType::MouseMotion, 200, 180, kShift);
        const glaze::Camera3D k2 = yta.camera3D();
        mus(gfx::EventType::MouseButtonUp, 200, 180, kShift, gfx::MouseButton::Middle);
        const double fram = std::hypot(k1.target.x - k0.target.x, k1.target.y - k0.target.y);
        check(fram > 0.0 && nära(k1.target.z, k0.target.z, 1e-9) &&
                  k2.target.z > k1.target.z && nära(k2.target.x, k1.target.x, 1e-6) &&
                  nära(k2.target.y, k1.target.y, 1e-6),
              "O mittendraget nedåt flyger utan höjd, och Shift MITT I draget höjer "
              "(planen läses per rörelse)",
              tal(fram) + " fram, " + tal(k2.target.z - k1.target.z) + " upp");
    }
    nollställ();
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== binding_gate — ritytans bindningstabell ===\n");
    fallT();
    fallA();
    {
        Värd w;
        w.setOffscreen(true);
        bool kördes = false;
        glaze::app().post([&] {
            kördes = true;
            w.kör();
            w.close();
        });
        w.run();
        if (!kördes) {
            std::printf("  --   fönsterfallen hoppades över: inget fönster kunde "
                        "skapas (ingen display)\n");
            ++g_skips;
        }
    }
    std::printf(g_fel ? "=== binding_gate: %d FEL av %d ===\n"
                      : "=== binding_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    if (g_fel)
        return 1;
    return g_skips ? 77 : 0;
}
