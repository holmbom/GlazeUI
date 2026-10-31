// Prov av sidopaketet OpenXR:s platta paneler utan headset, runtime, fönster
// eller grafikenhet. De rena delarna (flat_panel_logic.h) provas direkt, och
// `Interface3D` körs mot en rityta utan fönster med påhittade bildrutor för en
// platt panel med stillbild (en levande panel kräver ett öppet fönster och
// provas i headsetet).
//
// Prövar:
//   G  tavlans mått: ytan, ramen och tjockleken 0
//   X  träffen som pixel: hörn, mitten, utanför; ytans kanter
//   R  strålen mot ytan: framifrån, baksidan, parallell, utanför, lutad
//   T  spetsen mot ytan: framför, intryckning, genom tavlan
//   M  musens tillstånd: hover in och ut, tryck och släpp (klick), drag ut
//      ur ytan och släpp, träffen som tappas, avbrott utan klick, tryck
//      utanför ytan, hjulet
//   I  `Interface3D` med en platt stillbild: panelen läggs ut, elementen
//      ignoreras, pekaren tar inte stillbilden som mål
//
// Varje kontroll skriver vad som föll; programmet returnerar 1 om något föll.

#include "flat_panel_logic.h"

#include <glaze_openxr/interface3d.h>

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace glaze::openxr;
using namespace glaze::openxr::detail;

int g_failures = 0;
int g_checks   = 0;

void check(bool ok, const std::string& what, int line = 0)
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cerr << "FALL (rad " << line << "): " << what << "\n";
    }
}
#define CHECK(cond, what) check((cond), (what), __LINE__)

bool nearTo(double a, double b, double eps = 1e-6)
{
    return std::abs(a - b) <= eps;
}

bool nearPx(gfx::Pointf p, double x, double y, double eps = 1e-3)
{
    return nearTo(p.x, x, eps) && nearTo(p.y, y, eps);
}

// Ytan 0,8 × 0,4 m som 800 × 400 px.
constexpr double kW = 0.8, kH = 0.4;
constexpr int    kPxW = 800, kPxH = 400;

gfx::Pointf pixelOf(double x, double z)
{
    return flatPixel(Vec3{x, 0.0, z}, kW, kH, kPxW, kPxH);
}

FlatMouseInput over(double x, double z, bool down = false)
{
    FlatMouseInput in;
    in.present = true;
    in.px      = pixelOf(x, z);
    in.inside  = flatInside(in.px, kPxW, kPxH);
    in.down    = down;
    return in;
}

bool isType(const std::vector<gfx::Event>& events, std::size_t i, gfx::EventType type)
{
    return i < events.size() && events[i].type == type;
}

// ----------------------------------------------------------------- MÅTT
void geometryCases()
{
    const FlatGeometry g = flatGeometry(0.8, 0.4, 0.03, 0.01);
    CHECK(nearTo(g.surfaceWidth, 0.8) && nearTo(g.surfaceHeight, 0.4), "G1 ytans mått");
    CHECK(nearTo(g.boardWidth, 0.82) && nearTo(g.boardHeight, 0.42) && nearTo(g.thickness, 0.03),
          "G1 ramen runt ytan");
    const FlatGeometry bare = flatGeometry(0.8, 0.4, 0.0, 0.01);
    CHECK(nearTo(bare.boardWidth, 0.8) && nearTo(bare.boardHeight, 0.4), "G2 tjocklek 0: bara ytan, ingen ram");
    const FlatGeometry neg = flatGeometry(-1.0, 0.4, -0.5, -0.2);
    CHECK(neg.surfaceWidth == 0.0 && neg.thickness == 0.0 && neg.boardWidth == 0.0, "G3 negativa mått klampas");

    const ElementGeom tip = flatTipGeom(g);
    CHECK(nearTo(tip.width, 0.8) && nearTo(tip.height, 0.4) && tip.depth == 0.0 && nearTo(tip.travel, kFlatTravel) &&
              nearTo(tip.plateThickness, 0.03),
          "G4 spetsprovets mått");
}

// -------------------------------------------------------------- PIXLAR
void pixelCases()
{
    CHECK(nearPx(pixelOf(-0.4, 0.2), 0.0, 0.0), "X1 övre vänstra hörnet = (0, 0)");
    CHECK(nearPx(pixelOf(0.4, -0.2), 800.0, 400.0), "X1 nedre högra hörnet = (W, H)");
    CHECK(nearPx(pixelOf(0.4, 0.2), 800.0, 0.0) && nearPx(pixelOf(-0.4, -0.2), 0.0, 400.0), "X1 de andra hörnen");
    CHECK(nearPx(pixelOf(0.0, 0.0), 400.0, 200.0), "X2 mitten");
    CHECK(nearPx(pixelOf(0.2, 0.1), 600.0, 100.0), "X2 höger och uppåt: större x, mindre y");
    CHECK(nearPx(pixelOf(0.6, 0.0), 1000.0, 200.0) && nearPx(pixelOf(0.0, -0.4), 400.0, 600.0),
          "X3 utanför ytan ger pixlar utanför fönstret");
    CHECK(nearPx(flatPixel(Vec3{0.1, -0.5, 0.0}, kW, kH, kPxW, kPxH), 450.0, 200.0), "X4 djupet påverkar inte pixeln");
    CHECK(nearPx(flatPixel(Vec3{0.1, 0.0, 0.1}, 0.0, kH, kPxW, kPxH), 0.0, 0.0), "X5 en yta utan bredd ger (0, 0)");

    CHECK(flatInside(gfx::Pointf{0.0f, 0.0f}, kPxW, kPxH) && flatInside(gfx::Pointf{800.0f, 400.0f}, kPxW, kPxH),
          "X6 kanterna räknas in");
    CHECK(flatInside(gfx::Pointf{400.0f, 200.0f}, kPxW, kPxH), "X6 mitten är inne");
    CHECK(!flatInside(gfx::Pointf{-0.5f, 10.0f}, kPxW, kPxH) && !flatInside(gfx::Pointf{10.0f, -0.5f}, kPxW, kPxH) &&
              !flatInside(gfx::Pointf{800.5f, 10.0f}, kPxW, kPxH) && !flatInside(gfx::Pointf{10.0f, 400.5f}, kPxW, kPxH),
          "X6 utanför");
}

// -------------------------------------------------------------- STRÅLEN
void rayCases()
{
    const FlatRay centre = flatRay(Vec3{0.0, -1.0, 0.0}, Vec3{0.0, 1.0, 0.0}, kW, kH, kPxW, kPxH);
    CHECK(centre.hit && centre.inside && nearTo(centre.t, 1.0) && nearPx(centre.px, 400.0, 200.0),
          "R1 rakt framifrån träffar mitten");

    const FlatRay corner = flatRay(Vec3{-0.4, -2.0, 0.2}, Vec3{0.0, 1.0, 0.0}, kW, kH, kPxW, kPxH);
    CHECK(corner.hit && corner.inside && nearPx(corner.px, 0.0, 0.0), "R1 hörnet");

    const FlatRay back = flatRay(Vec3{0.0, 1.0, 0.0}, Vec3{0.0, -1.0, 0.0}, kW, kH, kPxW, kPxH);
    CHECK(!back.hit, "R2 baksidan: en stråle bakifrån träffar inte");
    const FlatRay behind = flatRay(Vec3{0.0, 1.0, 0.0}, Vec3{0.0, 1.0, 0.0}, kW, kH, kPxW, kPxH);
    CHECK(!behind.hit, "R2 ytan ligger bakom strålens början");
    const FlatRay parallel = flatRay(Vec3{0.0, -1.0, 0.0}, Vec3{1.0, 0.0, 0.0}, kW, kH, kPxW, kPxH);
    CHECK(!parallel.hit, "R3 parallell med ytan träffar inte");

    const FlatRay outside = flatRay(Vec3{0.6, -1.0, 0.0}, Vec3{0.0, 1.0, 0.0}, kW, kH, kPxW, kPxH);
    CHECK(outside.hit && !outside.inside && nearPx(outside.px, 1000.0, 200.0),
          "R4 utanför ytan: träffen och pixeln finns kvar för ett drag");

    // Lutad stråle: 0,2 åt höger efter 1 m framåt.
    const FlatRay tilted = flatRay(Vec3{0.0, -1.0, 0.0}, vnormalized(Vec3{0.2, 1.0, 0.0}), kW, kH, kPxW, kPxH);
    CHECK(tilted.hit && tilted.inside && nearTo(tilted.local.x, 0.2, 1e-9) && nearPx(tilted.px, 600.0, 200.0),
          "R5 lutad stråle");
}

// ------------------------------------------------------------- SPETSEN
void tipCases()
{
    const FlatGeometry g  = flatGeometry(kW, kH, 0.03, 0.01);
    const ElementGeom geom = flatTipGeom(g);
    const TipEval front = evalTip(Vec3{0.1, -0.02, 0.0}, geom, 0.05, 0.006);
    CHECK(front.inRange && nearTo(front.distance, 0.02) && front.penetration < 0.0, "T1 framför ytan: nära nog");
    CHECK(nearTo(pressDepthOf(front.penetration, geom.travel), 0.0), "T1 inget tryck framför ytan");

    const TipEval far = evalTip(Vec3{0.1, -0.08, 0.0}, geom, 0.05, 0.006);
    CHECK(!far.inRange, "T2 längre bort än hovern: inget mål");

    const TipEval pressed = evalTip(Vec3{0.1, 0.006, 0.0}, geom, 0.05, 0.006);
    const double depth    = pressDepthOf(pressed.penetration, geom.travel);
    CHECK(pressed.inRange && nearTo(depth, 0.75), "T3 6 mm in av 8 mm: 0,75");
    bool state = false;
    CHECK(stepPress(state, depth) == PressEdge::Pressed, "T3 trycket vid 0,7");
    CHECK(stepPress(state, pressDepthOf(0.004, geom.travel)) == PressEdge::None, "T3 hysteresen håller vid 0,5");
    CHECK(stepPress(state, pressDepthOf(0.002, geom.travel)) == PressEdge::Released, "T3 släppet under 0,4");

    const TipEval through = evalTip(Vec3{0.1, 0.05, 0.0}, geom, 0.05, 0.006);
    CHECK(!through.inRange, "T4 genom tavlan: ingen träff");
    const TipEval edge = evalTip(Vec3{0.403, -0.01, 0.0}, geom, 0.05, 0.006);
    CHECK(edge.inRange && !flatInside(flatPixel(Vec3{0.403, 0.0, 0.0}, kW, kH, kPxW, kPxH), kPxW, kPxH),
          "T5 marginalen räknas som mål men pixeln ligger utanför ytan");
}

// --------------------------------------------------------------- MUSEN
void mouseCases()
{
    using gfx::EventType;
    {   // hover in, rörelse, ut
        FlatMouse m;
        std::vector<gfx::Event> e = stepFlatMouse(m, over(0.0, 0.0));
        CHECK(e.size() == 1 && isType(e, 0, EventType::MouseMotion) && nearPx({e[0].mouseX, e[0].mouseY}, 400.0, 200.0),
              "M1 hover in: en MouseMotion på träffen");
        CHECK(m.inside && !m.down, "M1 musen är inne");
        CHECK(stepFlatMouse(m, over(0.0, 0.0)).empty(), "M1 samma läge: inga händelser");
        e = stepFlatMouse(m, over(0.1, 0.0));
        CHECK(e.size() == 1 && isType(e, 0, EventType::MouseMotion) && nearTo(e[0].mouseX, 500.0, 1e-3), "M1 rörelse");
        FlatMouseInput away;   // handen pekar inte längre på panelen
        e = stepFlatMouse(m, away);
        CHECK(e.size() == 1 && isType(e, 0, EventType::MouseLeave) && !m.inside, "M1 hover ut: MouseLeave");
        CHECK(stepFlatMouse(m, away).empty(), "M1 ingen ny Leave");
        FlatMouseInput outside = over(0.6, 0.0);
        CHECK(stepFlatMouse(m, outside).empty() && !m.inside, "M1 en träff utanför ytan är ingen hover");
    }
    {   // tryck och släpp över ytan = ett klick
        FlatMouse m;
        stepFlatMouse(m, over(0.0, 0.0));
        std::vector<gfx::Event> e = stepFlatMouse(m, over(0.0, 0.0, true));
        CHECK(e.size() == 1 && isType(e, 0, EventType::MouseButtonDown) && e[0].button == gfx::MouseButton::Left &&
                  m.down,
              "M2 trycket: MouseButtonDown med vänster knapp");
        CHECK(stepFlatMouse(m, over(0.0, 0.0, true)).empty(), "M2 hållet tryck på samma plats: tyst");
        e = stepFlatMouse(m, over(0.0, 0.0, false));
        CHECK(e.size() == 1 && isType(e, 0, EventType::MouseButtonUp) && e[0].button == gfx::MouseButton::Left &&
                  !m.down && m.inside,
              "M2 släppet över ytan: MouseButtonUp, hovern står kvar");
        CHECK(nearPx({e[0].mouseX, e[0].mouseY}, 400.0, 200.0), "M2 släppet på trycklägets pixel");
    }
    {   // tryck i samma bildruta som hovern börjar: först rörelsen
        FlatMouse m;
        const std::vector<gfx::Event> e = stepFlatMouse(m, over(0.1, 0.1, true));
        CHECK(e.size() == 2 && isType(e, 0, EventType::MouseMotion) && isType(e, 1, EventType::MouseButtonDown) &&
                  nearTo(e[1].mouseX, e[0].mouseX) && nearTo(e[1].mouseY, e[0].mouseY),
              "M3 hover och tryck samtidigt: MouseMotion, sedan MouseButtonDown på samma pixel");
    }
    {   // drag ut ur ytan, släpp utanför
        FlatMouse m;
        stepFlatMouse(m, over(0.3, 0.0));
        stepFlatMouse(m, over(0.3, 0.0, true));
        std::vector<gfx::Event> e = stepFlatMouse(m, over(0.5, 0.0, true));
        CHECK(e.size() == 1 && isType(e, 0, EventType::MouseMotion) && nearTo(e[0].mouseX, 900.0, 1e-3) && m.down &&
                  !m.inside,
              "M4 draget fortsätter utanför ytan med pixlar utanför fönstret, utan Leave");
        e = stepFlatMouse(m, over(0.7, 0.0, true));
        CHECK(e.size() == 1 && isType(e, 0, EventType::MouseMotion) && nearTo(e[0].mouseX, 1100.0, 1e-3), "M4 fortsatt drag");
        e = stepFlatMouse(m, over(0.7, 0.0, false));
        CHECK(e.size() == 2 && isType(e, 0, EventType::MouseButtonUp) && isType(e, 1, EventType::MouseLeave) && !m.down &&
                  !m.inside,
              "M4 släpp utanför: MouseButtonUp och sedan MouseLeave");
    }
    {   // drag ut och tillbaka in
        FlatMouse m;
        stepFlatMouse(m, over(0.3, 0.0, true));
        stepFlatMouse(m, over(0.6, 0.0, true));
        std::vector<gfx::Event> e = stepFlatMouse(m, over(0.2, 0.0, false));
        CHECK(e.size() == 2 && isType(e, 0, EventType::MouseMotion) && isType(e, 1, EventType::MouseButtonUp) && m.inside,
              "M5 släpp efter att ha dragit tillbaka in: rörelse, MouseButtonUp, hovern står kvar");
    }
    {   // träffen tappas under draget (strålen pekar förbi planet)
        FlatMouse m;
        stepFlatMouse(m, over(0.2, 0.0, true));
        FlatMouseInput lost;
        lost.down = true;
        CHECK(stepFlatMouse(m, lost).empty() && m.down, "M6 träffen tappas: sista läget hålls, inget skickas");
        lost.down = false;
        const std::vector<gfx::Event> e = stepFlatMouse(m, lost);
        CHECK(e.size() == 1 && isType(e, 0, EventType::MouseButtonUp) && nearTo(e[0].mouseX, 600.0, 1e-3),
              "M6 släppet går på sista läget");
    }
    {   // avbrott mitt i ett tryck: inget släpp, inget klick
        FlatMouse m;
        stepFlatMouse(m, over(0.0, 0.0, true));
        FlatMouseInput cancel;
        cancel.cancel = true;
        const std::vector<gfx::Event> e = stepFlatMouse(m, cancel);
        bool anyUp = false;
        for (const gfx::Event& ev : e)
            anyUp = anyUp || ev.type == EventType::MouseButtonUp;
        CHECK(e.size() == 2 && isType(e, 0, EventType::MouseCaptureLost) && isType(e, 1, EventType::MouseLeave) && !anyUp,
              "M7 avbrott: MouseCaptureLost och MouseLeave, aldrig MouseButtonUp");
        CHECK(!m.down && !m.inside && !m.hasPos, "M7 musen är återställd");
        CHECK(stepFlatMouse(m, cancel).empty(), "M7 avbrott utan musen nere: tyst");
    }
    {   // avbrott under bara hover släcker hovern
        FlatMouse m;
        stepFlatMouse(m, over(0.0, 0.0));
        FlatMouseInput cancel;
        cancel.cancel = true;
        const std::vector<gfx::Event> e = stepFlatMouse(m, cancel);
        CHECK(e.size() == 1 && isType(e, 0, EventType::MouseLeave), "M8 avbrott under hover: MouseLeave");
    }
    {   // ett tryck börjar bara på ytan
        FlatMouse m;
        CHECK(stepFlatMouse(m, over(0.6, 0.0, true)).empty() && !m.down, "M9 tryck utanför ytan börjar inget");
        // tryck på ytan, sedan fortsätter knappen hållas efter släppet: ingen ny tryckning
        stepFlatMouse(m, over(0.0, 0.0, true));
        stepFlatMouse(m, over(0.0, 0.0, false));
        CHECK(!m.down, "M9 släppt");
    }
    {   // hjulet
        CHECK(nearTo(wheelRows(1.0, 0.1), kWheelRowsPerSecond * 0.1) && nearTo(wheelRows(-0.5, 0.2), -0.5 * kWheelRowsPerSecond * 0.2),
              "M10 raderna ur spakens utslag och tiden");
        CHECK(wheelRows(1.0, -1.0) == 0.0, "M10 negativ tid ger ingen rullning");
        const gfx::Event w = wheelEvent(gfx::Pointf{12.0f, 34.0f}, 1.5);
        CHECK(w.type == EventType::MouseWheel && nearTo(w.wheelY, 1.5) && w.wheelX == 0.0f && nearTo(w.mouseX, 12.0) &&
                  nearTo(w.mouseY, 34.0),
              "M10 hjulhändelsen på pixeln, uppåt positivt");
    }
}

// ------------------------------------------------------- INTERFACE3D
WorldPose worldPose(const Pose& p)
{
    return toWorldPose(p);
}

Pose poseAt(double x, double y, double z)
{
    Pose p;
    p.position = Vec3{x, y, z};
    return p;
}

void interfaceCases()
{
    glaze::Canvas canvas;
    const int layer = canvas.addLayer("gränssnitt");
    Interface3D ui(canvas, layer);
    Pointers pointers;
    pointers.tipOffset = 0.03;
    pointers.draw      = false;
    ui.setPointers(pointers);

    // En skylt på stolpe med en stillbild: 0,8 × 0,4 m, framsidan mot −y.
    Panel sign;
    sign.id           = "skylt";
    sign.mount.attach = Attach::World;
    sign.mount.pose   = poseAt(0.0, 0.0, 1.5);
    sign.layout.width = 0.8;
    sign.layout.height = 0.4;
    FlatContent content;
    content.image            = glaze::Bitmap(16, 8);
    content.style.stand.on   = true;
    content.style.stand.groundZ = 0.0;
    sign.flat = content;
    Element stray;
    stray.id = "fel";
    sign.elements.push_back(stray);
    ui.addPanel(sign);

    FrameInfo f;
    f.focused      = true;
    f.deltaSeconds = 0.016;
    f.anchor.scale = 1.0;
    f.head.pose    = worldPose(poseAt(0.0, -2.0, 1.7));
    Controller& right = f.controllers[1];
    right.aim.pose  = worldPose(poseAt(0.0, -1.0, 1.5));
    right.grip.pose = worldPose(poseAt(0.0, -1.0, 1.5));
    right.buttons[static_cast<std::size_t>(Button::Trigger)].available = true;
    right.buttons[static_cast<std::size_t>(Button::Trigger)].pressed   = true;
    for (int i = 0; i < 4; ++i)
        ui.update(f, nullptr);

    CHECK(ui.panel("skylt") != nullptr && ui.panel("skylt")->flat.has_value(), "I1 den platta panelen finns");
    CHECK(ui.panel("skylt")->elements.empty(), "I1 en platt panel har inga element");
    ui.setElement("skylt", stray);
    CHECK(ui.panel("skylt")->elements.empty(), "I1 setElement lägger inget i en platt panel");
    CHECK(ui.panelPose("skylt").has_value() && nearTo(ui.panelPose("skylt")->position.z, 1.5), "I2 skylten står i världen");

    const PointerState& st = ui.pointer(Hand::Right);
    CHECK(st.valid && st.hit.kind != PointerTarget::Flat && !st.consumed,
          "I3 en stillbild är inget musmål och tar inte handen");

    ui.setPanelVisible("skylt", false);
    ui.update(f, nullptr);
    ui.removePanel("skylt");
    CHECK(ui.panel("skylt") == nullptr, "I4 panelen borttagen");
    ui.update(f, nullptr);
}

} // namespace

int main()
{
    geometryCases();
    pixelCases();
    rayCases();
    tipCases();
    mouseCases();
    interfaceCases();
    if (g_failures != 0) {
        std::cerr << g_failures << " av " << g_checks << " kontroller föll\n";
        return 1;
    }
    std::printf("flat_panel_gate: alla %d kontroller gröna\n", g_checks);
    return 0;
}
