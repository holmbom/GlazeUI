// Prov av sidopaketet OpenXR:s 3D-gränssnitt utan headset, runtime, fönster
// eller grafikenhet. De rena delarna (interface3d_logic.h) provas direkt, och
// `Interface3D` körs mot en rityta utan fönster med påhittade bildrutor
// (poser, knappar och handspårning skrivs för hand).
//
// Prövar:
//   L  layouten: cellerna i rutnät (angivna och radvisa), rad och kolumn,
//      span, fri placering, fast storlek med justering
//   P  placeringen: speglingen för vänster hand, förskjutningen skalad med
//      världsskalan, vändningen mot huvudet (bara kring z), den mjuka
//      följningen av huvudet
//   T  tryckdjupet längs framsidans normal, hysteresen (0,7 / 0,4) och
//      strålens skärning mot framsidan
//   V  värdena: steg, reglagets spår och värde, vredets vridning och svep
//   S  synligheten: villkoren (blick, andra handen, requireBoth), blickens
//      vinkel och intoningen
//   I  `Interface3D` i bildrutor: närhet, Pressed/Released/Clicked och
//      Toggled med fingret, kontrollens spets och strålen, reglage och vred,
//      grepp som blir en World-panel, att hand-paneler utan pose döljs, att
//      `consumes` följer trycket, Squeeze med spetsen på ett reglage, och att
//      en bortonad panel inte tar tryck
//
// Varje kontroll skriver vad som föll; programmet returnerar 1 om något föll.

#include "interface3d_logic.h"

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

constexpr double kPi = 3.14159265358979323846;

bool nearTo(double a, double b, double eps = 1e-9)
{
    return std::abs(a - b) <= eps;
}

Pose poseAt(double x, double y, double z)
{
    Pose p;
    p.position = Vec3{x, y, z};
    return p;
}

Pose yawed(double x, double y, double z, double yawDegrees)
{
    Pose p = poseAt(x, y, z);
    YawPitchRoll a;
    a.yawDegrees  = yawDegrees;
    p.orientation = fromYawPitchRoll(a);
    return p;
}

Element makeElement(const std::string& id, ElementKind kind, double w = 0.04, double h = 0.04)
{
    Element e;
    e.id   = id;
    e.kind = kind;
    ElementStyle s;
    s.width  = w;
    s.height = h;
    e.style  = s;
    return e;
}

Panel makePanel(const std::string& id, LayoutKind kind)
{
    Panel p;
    p.id                = id;
    p.layout.kind       = kind;
    p.style.padding     = 0.01;
    p.style.spacing     = 0.008;
    p.style.thickness   = 0.006;
    return p;
}

// ------------------------------------------------------------------- LAYOUT
void layoutCases()
{
    {   // L1: två kolumner, fyra celler angivna
        Panel p = makePanel("p", LayoutKind::Grid);
        p.layout.columns = 2;
        for (int i = 0; i < 4; ++i) {
            Element e = makeElement("e" + std::to_string(i), ElementKind::Button);
            e.row     = i / 2;
            e.column  = i % 2;
            p.elements.push_back(e);
        }
        const PanelLayout l = layoutPanel(p);
        CHECK(nearTo(l.width, 0.108) && nearTo(l.height, 0.108), "L1 panelens storlek ur innehållet och padding");
        CHECK(nearTo(l.places[0].u, -0.024) && nearTo(l.places[0].v, 0.024), "L1 cell (0,0) uppe till vänster");
        CHECK(nearTo(l.places[3].u, 0.024) && nearTo(l.places[3].v, -0.024), "L1 cell (1,1) nere till höger");
        const Pose f = elementFrame(l.places[1], p.elements[1]);
        CHECK(nearTo(f.position.x, 0.024) && nearTo(f.position.y, 0.0) && nearTo(f.position.z, 0.024),
              "L1 elementets ram är cellens mitt");
    }
    {   // L2: Grid utan angivna celler läggs ut radvis
        Panel p = makePanel("p", LayoutKind::Grid);
        p.layout.columns = 2;
        for (int i = 0; i < 5; ++i)
            p.elements.push_back(makeElement("e" + std::to_string(i), ElementKind::Button));
        const PanelLayout l = layoutPanel(p);
        CHECK(nearTo(l.places[2].u, l.places[0].u) && l.places[2].v < l.places[0].v, "L2 tredje elementet börjar rad två");
        CHECK(nearTo(l.height, 3 * 0.04 + 2 * 0.008 + 0.02), "L2 tre rader");
    }
    {   // L3: Row och Column i ordning med span
        Panel row = makePanel("r", LayoutKind::Row);
        row.elements.push_back(makeElement("a", ElementKind::Button));
        Element b = makeElement("b", ElementKind::Button, 0.1, 0.04);
        b.columnSpan = 2;
        row.elements.push_back(b);
        row.elements.push_back(makeElement("c", ElementKind::Button));
        const PanelLayout lr = layoutPanel(row);
        // Kolumner: 0.04, (b över två: 0.1 ≥ 0.04 + 0.008 + 0.04 ⇒ +0.006 var), 0.046, 0.04.
        CHECK(nearTo(lr.width, 0.04 + 0.046 + 0.046 + 0.04 + 3 * 0.008 + 0.02), "L3 radens bredd med span");
        CHECK(lr.places[0].u < lr.places[1].u && lr.places[1].u < lr.places[2].u, "L3 elementen åt höger");
        CHECK(nearTo(lr.places[0].v, 0.0) && nearTo(lr.height, 0.06), "L3 en rad");

        Panel col = makePanel("c", LayoutKind::Column);
        for (int i = 0; i < 3; ++i)
            col.elements.push_back(makeElement("e" + std::to_string(i), ElementKind::Button));
        const PanelLayout lc = layoutPanel(col);
        CHECK(lc.places[0].v > lc.places[1].v && lc.places[1].v > lc.places[2].v, "L3 kolumnen nedåt");
        CHECK(nearTo(lc.width, 0.06) && nearTo(lc.height, 3 * 0.04 + 2 * 0.008 + 0.02), "L3 kolumnens mått");
    }
    {   // L4: fri placering med pose; storlek ur utsträckningen
        Panel p = makePanel("f", LayoutKind::Free);
        Element a = makeElement("a", ElementKind::Button);
        a.pose    = poseAt(-0.1, 0.0, 0.0);
        Element b = makeElement("b", ElementKind::Button);
        b.pose    = poseAt(0.1, 0.0, 0.05);
        p.elements = {a, b};
        const PanelLayout l = layoutPanel(p);
        CHECK(nearTo(l.width, 0.24 + 0.02) && nearTo(l.height, 0.09 + 0.02), "L4 fri: bounding box och padding");
        CHECK(nearTo(l.plateU, 0.0) && nearTo(l.plateV, 0.025), "L4 plattans mitt");
        CHECK(nearTo(l.places[0].u, 0.0) && nearTo(l.places[0].v, 0.0), "L4 cellen i fri layout är origo");
    }
    {   // L5: fast storlek och justering
        Panel p = makePanel("p", LayoutKind::Column);
        p.layout.autoSize   = false;
        p.layout.width      = 0.2;
        p.layout.height     = 0.2;
        p.layout.horizontal = Align::Start;
        p.layout.vertical   = Align::End;
        p.elements.push_back(makeElement("a", ElementKind::Button));
        PanelLayout l = layoutPanel(p);
        CHECK(nearTo(l.width, 0.2) && nearTo(l.height, 0.2), "L5 fast storlek");
        CHECK(nearTo(l.places[0].u, -0.1 + 0.01 + 0.02), "L5 vänsterjusterat");
        CHECK(nearTo(l.places[0].v, -0.1 + 0.01 + 0.02), "L5 nederkanten");
        p.layout.horizontal = Align::Center;
        p.layout.vertical   = Align::Center;
        l = layoutPanel(p);
        CHECK(nearTo(l.places[0].u, 0.0) && nearTo(l.places[0].v, 0.0), "L5 mitten");
    }
    {   // L6: panelens stil är elementets förval, elementets egen går före
        Panel p = makePanel("p", LayoutKind::Row);
        p.style.element.width  = 0.05;
        p.style.element.height = 0.05;
        Element a;
        a.id = "a";
        Element b = makeElement("b", ElementKind::Button, 0.08, 0.03);
        p.elements = {a, b};
        const PanelLayout l = layoutPanel(p);
        CHECK(nearTo(l.places[0].width, 0.05) && nearTo(l.places[1].width, 0.08), "L6 stilen per element");
        CHECK(nearTo(l.height, 0.05 + 0.02), "L6 raden är den högsta");
    }
}

// ---------------------------------------------------------------- PLACERING
void placementCases()
{
    {   // P1: speglingen
        const Pose offset = yawed(0.1, 0.2, 0.3, 30.0);
        const Pose m      = mirrorForLeftHand(offset);
        CHECK(nearTo(m.position.x, -0.1) && nearTo(m.position.y, 0.2) && nearTo(m.position.z, 0.3),
              "P1 x byter tecken");
        CHECK(nearTo(toYawPitchRoll(m.orientation).yawDegrees, -30.0, 1e-6), "P1 vridningen kring z byter tecken");
        // Två speglingar ger tillbaka förskjutningen.
        const Pose back = mirrorForLeftHand(m);
        CHECK(nearTo(back.position.x, 0.1) && nearTo(toYawPitchRoll(back.orientation).yawDegrees, 30.0, 1e-6),
              "P1 spegling två gånger är identitet");
    }
    {   // P2: handens ram, skala och vänster/höger
        Mount mount;
        mount.pose = poseAt(0.1, 0.0, 0.05);
        const Pose hand = poseAt(1.0, 2.0, 1.0);
        const Pose right = attachedPose(hand, mount, false, 2.0);
        const Pose left  = attachedPose(hand, mount, true, 2.0);
        CHECK(nearTo(right.position.x, 1.2) && nearTo(right.position.z, 1.1), "P2 höger: förskjutningen skalad");
        CHECK(nearTo(left.position.x, 0.8) && nearTo(left.position.z, 1.1), "P2 vänster: x speglat");
        mount.mirrorForLeft = false;
        CHECK(nearTo(attachedPose(hand, mount, true, 2.0).position.x, 1.2), "P2 utan spegling");
        // Handens vridning vrider förskjutningen: 90° yaw lägger +x mot söder... (medurs: +x blir −y).
        const Pose turned = attachedPose(yawed(0, 0, 0, 90.0), mount, false, 1.0);
        CHECK(nearTo(turned.position.x, 0.0, 1e-9) && nearTo(turned.position.y, -0.1, 1e-9),
              "P2 förskjutningen i handens ram");
    }
    {   // P3: vändningen mot huvudet
        const Vec3 head{0, 0, 1.7};
        CHECK(nearTo(yawFacing(Vec3{0, 5, 1}, head), 0.0, 1e-9), "P3 panel norr om huvudet: +y mot norr");
        CHECK(nearTo(yawFacing(Vec3{5, 0, 1}, head), 90.0, 1e-9), "P3 panel öster om huvudet: +y mot öster");
        Pose tilted = yawed(5, 0, 1, 0);
        YawPitchRoll a = toYawPitchRoll(tilted.orientation);
        a.pitchDegrees = 20.0;
        tilted.orientation = fromYawPitchRoll(a);
        const Pose faced = faceHead(tilted, head);
        const YawPitchRoll b = toYawPitchRoll(faced.orientation);
        CHECK(nearTo(b.yawDegrees, 90.0, 1e-6) && nearTo(b.pitchDegrees, 20.0, 1e-6), "P3 lutningen behålls");
        CHECK(nearTo(faced.position.x, 5.0) && nearTo(faced.position.z, 1.0), "P3 läget oförändrat");
    }
    {   // P4: kontrollens spets
        WorldPose aim;
        aim.position = Vec3{1, 1, 1};
        aim.forward  = Vec3{0, 1, 0};
        const Vec3 tip = controllerTip(aim, 0.03, 2.0);
        CHECK(nearTo(tip.x, 1.0) && nearTo(tip.y, 1.06) && nearTo(tip.z, 1.0), "P4 spetsen längs forward, skalad");
    }
}

// -------------------------------------------------------------- SYNLIGHET
void visibilityCases()
{
    {   // S1: villkoren
        Visibility v;
        CHECK(visibleByConditions(v, false, false), "S1 utan villkor syns panelen alltid");
        v.gazeDegrees = 35.0;
        CHECK(visibleByConditions(v, true, false) && !visibleByConditions(v, false, true),
              "S1 bara blicken räknas när bara den är på");
        v.gazeDegrees = 0.0;
        v.otherHandMetres = 0.35;
        CHECK(visibleByConditions(v, false, true) && !visibleByConditions(v, true, false),
              "S1 bara handen räknas när bara den är på");
        v.gazeDegrees = 35.0;
        CHECK(visibleByConditions(v, true, false) && visibleByConditions(v, false, true) &&
                  !visibleByConditions(v, false, false),
              "S1 båda på: något av dem");
        v.requireBoth = true;
        CHECK(visibleByConditions(v, true, true) && !visibleByConditions(v, true, false) &&
                  !visibleByConditions(v, false, true),
              "S1 requireBoth: båda");
    }
    {   // S2: blickens vinkel
        const Vec3 head{0, 0, 1.7};
        const Vec3 forward{0, 1, 0};
        CHECK(nearTo(gazeAngleDegrees(head, forward, Vec3{0, 2, 1.7}), 0.0, 1e-9), "S2 rakt fram");
        CHECK(nearTo(gazeAngleDegrees(head, forward, Vec3{2, 2, 1.7}), 45.0, 1e-9), "S2 45 grader åt sidan");
        CHECK(nearTo(gazeAngleDegrees(head, forward, Vec3{0, -2, 1.7}), 180.0, 1e-9), "S2 bakom");
        CHECK(nearTo(gazeAngleDegrees(head, forward, head), 180.0), "S2 samma punkt");
    }
    {   // S3: intoningen
        CHECK(nearTo(stepFade(0.0, true, 0.075, 0.15), 0.5), "S3 halvvägs in på halva tiden");
        CHECK(nearTo(stepFade(1.0, false, 0.075, 0.15), 0.5), "S3 halvvägs ut på halva tiden");
        CHECK(nearTo(stepFade(0.9, true, 1.0, 0.15), 1.0) && nearTo(stepFade(0.1, false, 1.0, 0.15), 0.0),
              "S3 klampat");
        CHECK(nearTo(stepFade(0.0, true, 0.0, 0.0), 1.0) && nearTo(stepFade(1.0, false, 0.0, 0.0), 0.0),
              "S3 noll sekunder byter direkt");
    }
}

// --------------------------------------------------------------- TRYCKDJUP
void pressCases()
{
    ElementGeom g;   // djup 0,012, färd 0,008, platta 0,006
    {   // T1: framför, vid och bakom framsidan
        const TipEval front = evalTip(Vec3{0, -g.depth - 0.025, 0}, g, 0.05, 0.006);
        CHECK(front.over && front.inRange && nearTo(front.proximity, 0.5), "T1 25 mm framför: närhet 0,5");
        CHECK(nearTo(front.distance, 0.025) && front.penetration < 0, "T1 avståndet framför");
        const TipEval farTip = evalTip(Vec3{0, -g.depth - 0.06, 0}, g, 0.05, 0.006);
        CHECK(!farTip.inRange, "T1 utanför närhetsavståndet");
        const TipEval side = evalTip(Vec3{0.03, -g.depth - 0.01, 0}, g, 0.05, 0.006);
        CHECK(!side.over, "T1 vid sidan om rektangeln");
        const TipEval margin = evalTip(Vec3{0.025, -g.depth - 0.01, 0}, g, 0.05, 0.006);
        CHECK(margin.over, "T1 inom marginalen");
        const TipEval at = evalTip(Vec3{0, -g.depth + 0.004, 0}, g, 0.05, 0.006);
        CHECK(nearTo(at.penetration, 0.004) && nearTo(at.proximity, 1.0), "T1 4 mm in");
        const TipEval through = evalTip(Vec3{0, 0.05, 0}, g, 0.05, 0.006);
        CHECK(!through.inRange, "T1 genom panelen räknas inte");
        CHECK(nearTo(pressDepthOf(0.004, g.travel), 0.5) && nearTo(pressDepthOf(0.02, g.travel), 1.0) &&
                  nearTo(pressDepthOf(-0.01, g.travel), 0.0),
              "T1 tryckdjupet 0 … 1 av färden");
    }
    {   // T2: hysteresen
        bool pressed = false;
        CHECK(stepPress(pressed, 0.5) == PressEdge::None && !pressed, "T2 0,5 trycker inte");
        CHECK(stepPress(pressed, 0.7) == PressEdge::Pressed && pressed, "T2 0,7 trycker");
        CHECK(stepPress(pressed, 0.5) == PressEdge::None && pressed, "T2 0,5 håller (hysteres)");
        CHECK(stepPress(pressed, 0.41) == PressEdge::None && pressed, "T2 0,41 håller");
        CHECK(stepPress(pressed, 0.39) == PressEdge::Released && !pressed, "T2 under 0,4 släpper");
        CHECK(stepPress(pressed, 0.6) == PressEdge::None && !pressed, "T2 0,6 trycker inte igen");
    }
    {   // T3: strålen mot framsidan
        const RayHit hit = rayVsFace(Vec3{0.01, -0.5, 0.005}, Vec3{0, 1, 0}, -0.012, 0.04, 0.04);
        CHECK(hit.hit && hit.inside && nearTo(hit.t, 0.488) && nearTo(hit.local.x, 0.01), "T3 träff på framsidan");
        const RayHit miss = rayVsFace(Vec3{0.1, -0.5, 0}, Vec3{0, 1, 0}, -0.012, 0.04, 0.04);
        CHECK(miss.hit && !miss.inside, "T3 träffar planet utanför rektangeln");
        CHECK(!rayVsFace(Vec3{0, 0.5, 0}, Vec3{0, 1, 0}, -0.012, 0.04, 0.04).hit, "T3 bakom planet");
        CHECK(!rayVsFace(Vec3{0, -0.5, 0}, Vec3{0, -1, 0}, -0.012, 0.04, 0.04).hit, "T3 från framsidan bort");
        CHECK(!rayVsFace(Vec3{0, -0.5, 0}, Vec3{1, 0, 0}, -0.012, 0.04, 0.04).hit, "T3 parallell");
    }
}

// ----------------------------------------------------------------- VÄRDEN
void valueCases()
{
    CHECK(nearTo(quantizeValue(0.34, 0.0, 1.0, 0.1), 0.3), "V1 steg 0,1");
    CHECK(nearTo(quantizeValue(0.37, 0.0, 1.0, 0.1), 0.4), "V1 avrundar uppåt");
    CHECK(nearTo(quantizeValue(1.4, 0.0, 1.0, 0.1), 1.0) && nearTo(quantizeValue(-1.0, 0.0, 1.0, 0.1), 0.0), "V1 klampat");
    CHECK(nearTo(quantizeValue(0.34, 0.0, 1.0, 0.0), 0.34), "V1 steglöst");
    CHECK(nearTo(quantizeValue(4.9, 1.0, 5.0, 2.0), 5.0) && nearTo(quantizeValue(0.9, 1.0, 5.0, 2.0), 1.0),
          "V1 steget räknas från minimum");
    CHECK(nearTo(quantizeValue(5.0, 1.0, 4.0, 2.0), 3.0), "V1 högsta steget inom maximum");

    const SliderTrack track = sliderTrack(0.2, 0.04, false, false);
    CHECK(nearTo(track.left, -0.1 + 0.008) && nearTo(track.right, 0.1 - 0.008), "V2 spåret utan innehåll");
    const SliderTrack labelled = sliderTrack(0.2, 0.04, true, false);
    CHECK(labelled.left > track.left && nearTo(labelled.labelWidth, 0.07), "V2 texten tar plats till vänster");
    const double mid = (track.left + track.right) / 2.0;
    CHECK(nearTo(sliderValueAt(mid, track, 0.0, 1.0, 0.0), 0.5), "V2 mitten = 0,5");
    CHECK(nearTo(sliderValueAt(track.left - 1.0, track, 0.0, 1.0, 0.1), 0.0) &&
              nearTo(sliderValueAt(track.right + 1.0, track, 0.0, 1.0, 0.1), 1.0),
          "V2 utanför spåret klampas");
    const double quarter = track.left + 0.37 * (track.right - track.left);
    CHECK(nearTo(sliderValueAt(quarter, track, 0.0, 1.0, 0.1), 0.4), "V2 steg");
    CHECK(nearTo(sliderKnobX(0.5, track, 0.0, 1.0), mid) && nearTo(sliderKnobX(2.0, track, 0.0, 1.0), track.right),
          "V2 knoppens läge");

    CHECK(nearTo(rollAngle(1.0, 0.0), 0.0) && nearTo(rollAngle(0.0, -1.0), kPi / 2.0), "V3 rullningen medurs sett framifrån");
    CHECK(nearTo(unwrapAngle(3.0 * kPi / 2.0), -kPi / 2.0) && nearTo(unwrapAngle(-3.0 * kPi / 2.0), kPi / 2.0),
          "V3 vinkeln till (−π, π]");
    // 90° medurs av ett svep på 270° = en tredjedel.
    CHECK(nearTo(dialValueFrom(0.5, kPi / 2.0, 0.0, 1.0, 0.0), 0.5 + 1.0 / 3.0, 1e-9), "V3 vridning 90°");
    CHECK(nearTo(dialValueFrom(0.5, kPi / 2.0, 0.0, 1.0, 0.25), 0.75), "V3 vridning med steg");
    CHECK(nearTo(dialValueFrom(0.5, -0.9 * kPi, 0.0, 1.0, 0.0), 0.0), "V3 klampad vid minimum");
    CHECK(nearTo(unwrapAngle(-3.0 - 3.0), 2.0 * kPi - 6.0, 1e-9), "V3 en bildrutas vridning går över ±π utan hopp");
    // Mer än ett halvt varv i ett grepp: bildrutornas vridningar läggs ihop.
    CHECK(nearTo(dialValueFrom(0.0, 1.2 * kPi, 0.0, 1.0, 0.0), 0.8, 1e-9), "V3 vridning över 180°");
    CHECK(nearTo(dialMarkerAngle(0.0, 0.0, 1.0), -0.75 * kPi) && nearTo(dialMarkerAngle(1.0, 0.0, 1.0), 0.75 * kPi) &&
              nearTo(dialMarkerAngle(0.5, 0.0, 1.0), 0.0),
          "V3 markeringen svepet ±135°");

    CHECK(nearTo(textureScale(0.04, 0.04, 4000), 4000.0) && nearTo(textureScale(2.0, 0.04, 4000), 2048.0),
          "V4 texturens skala under taket");
}

// ------------------------------------------------------- INTERFACE3D I BILDRUTOR
WorldPose worldPose(const Pose& p)
{
    return toWorldPose(p);
}

void setController(FrameInfo& f, Hand hand, const Pose& aim, const Pose& grip)
{
    Controller& c = f.controllers[hand == Hand::Left ? 0 : 1];
    c.aim.pose  = worldPose(aim);
    c.grip.pose = worldPose(grip);
    c.buttons[static_cast<std::size_t>(Button::Trigger)].available = true;
    c.buttons[static_cast<std::size_t>(Button::Squeeze)].available = true;
}

void setButtons(FrameInfo& f, Hand hand, bool trigger, bool squeeze)
{
    Controller& c = f.controllers[hand == Hand::Left ? 0 : 1];
    c.buttons[static_cast<std::size_t>(Button::Trigger)].pressed = trigger;
    c.buttons[static_cast<std::size_t>(Button::Squeeze)].pressed = squeeze;
}

FrameInfo baseFrame()
{
    FrameInfo f;
    f.focused      = true;
    f.deltaSeconds = 0.016;
    f.anchor.scale = 1.0;
    Pose head = poseAt(0, -1, 1.7);
    f.head.pose = worldPose(head);
    return f;
}

struct Log {
    std::vector<ElementEvent> events;
    int count(ElementEventKind k, const std::string& element = std::string()) const
    {
        int n = 0;
        for (const ElementEvent& e : events)
            if (e.kind == k && (element.empty() || e.element == element))
                ++n;
        return n;
    }
    void clear() { events.clear(); }
};

// En kontrolls aim-pose som siktar rakt in i panelen (+y) från (x, y, z).
Pose aimAt(double x, double y, double z)
{
    return poseAt(x, y, z);
}

void interfaceCases()
{
    glaze::Canvas canvas;
    const int layer = canvas.addLayer("gränssnitt");
    Interface3D ui(canvas, layer);
    // Spetsen 0,03 framför aim, utan ritad pekare. Auto-läget ger staven nära
    // panelen och strålen på avstånd — proven nedan använder båda.
    Pointers pointers;
    pointers.tipOffset = 0.03;
    pointers.draw      = false;
    ui.setPointers(pointers);
    Log log;
    ui.onElement = [&](const ElementEvent& e) { log.events.push_back(e); };

    // Panelen står i världen vid origo, framsidan mot −y; en knapp, en vippa,
    // ett reglage, ett vred och ett handtag i en kolumn.
    Panel panel = makePanel("main", LayoutKind::Column);
    panel.layout.autoSize = true;
    panel.mount.attach    = Attach::World;
    panel.mount.pose      = poseAt(0, 0, 1.0);
    panel.elements.push_back(makeElement("button", ElementKind::Button));
    panel.elements.push_back(makeElement("toggle", ElementKind::Toggle));
    Element slider = makeElement("slider", ElementKind::Slider, 0.2, 0.04);
    slider.min = 0.0; slider.max = 1.0; slider.step = 0.1; slider.value = 0.0;
    panel.elements.push_back(slider);
    Element dial = makeElement("dial", ElementKind::Dial);
    dial.min = 0.0; dial.max = 1.0; dial.value = 0.5;
    panel.elements.push_back(dial);
    panel.elements.push_back(makeElement("grip", ElementKind::Handle));
    panel.elements[0].content.text = "Knapp";
    ui.addPanel(panel);

    FrameInfo f = baseFrame();
    ui.update(f, nullptr);   // lägger ut panelen
    CHECK(ui.panelPose("main").has_value() && nearTo(ui.panelPose("main")->position.z, 1.0), "I0 World-panelen står kvar");

    const PanelLayout layout = layoutPanel(*ui.panel("main"));
    const auto elementCentre = [&](int index) {
        const ElementPlace& place = layout.places[static_cast<std::size_t>(index)];
        return Vec3{place.u, 0.0, 1.0 + place.v};
    };
    const double faceY = -0.012;   // framsidan, standardstilens djup

    {   // I1: kontrollens spets nära knappen — närhet, sedan tryck och släpp
        const Vec3 c = elementCentre(0);
        // Spetsen 0,03 framför aim: aim 0,03 längre bort.
        setController(f, Hand::Right, aimAt(c.x, faceY - 0.01 - 0.03, c.z), poseAt(0, -1, 1));
        ui.update(f, nullptr);
        ElementState s = ui.state("main", "button");
        CHECK(s.hovered && s.proximity > 0.7 && s.proximity < 0.9 && !s.pressed, "I1 närhet 10 mm");
        CHECK(log.count(ElementEventKind::HoverEnter, "button") == 1, "I1 HoverEnter");
        CHECK(ui.consumes(Hand::Right) && ui.pointer(Hand::Right).hit.kind == PointerTarget::Element,
              "I1 en hand vars mål är ett element tas av gränssnittet");

        setController(f, Hand::Right, aimAt(c.x, faceY + 0.0064 - 0.03, c.z), poseAt(0, -1, 1));   // 0,8 av färden
        ui.update(f, nullptr);
        s = ui.state("main", "button");
        CHECK(s.pressed && nearTo(s.pressDepth, 0.8, 1e-6), "I1 Pressed vid 0,8 av färden");
        CHECK(log.count(ElementEventKind::Pressed, "button") == 1, "I1 Pressed fyrades");
        CHECK(ui.consumes(Hand::Right) && !ui.consumes(Hand::Left), "I1 consumes medan handen trycker");

        setController(f, Hand::Right, aimAt(c.x, faceY + 0.0048 - 0.03, c.z), poseAt(0, -1, 1));   // 0,6: hysteres
        ui.update(f, nullptr);
        CHECK(ui.state("main", "button").pressed && log.count(ElementEventKind::Released) == 0, "I1 hysteres: 0,6 släpper inte");

        setController(f, Hand::Right, aimAt(c.x, faceY - 0.004 - 0.03, c.z), poseAt(0, -1, 1));   // ut
        ui.update(f, nullptr);
        CHECK(!ui.state("main", "button").pressed && log.count(ElementEventKind::Released, "button") == 1 &&
                  log.count(ElementEventKind::Clicked, "button") == 1,
              "I1 släpp: Released och Clicked");
        // Bort från panelen: handen är fri.
        setController(f, Hand::Right, aimAt(c.x + 1.0, -0.6, c.z), poseAt(0, -1, 1));
        ui.update(f, nullptr);
        CHECK(!ui.consumes(Hand::Right), "I1 consumes efter släppet, bort från panelen");
        log.clear();
    }
    {   // I2: vippan växlar vid Clicked
        const Vec3 c = elementCentre(1);
        setController(f, Hand::Right, aimAt(c.x, faceY + 0.007 - 0.03, c.z), poseAt(0, -1, 1));
        ui.update(f, nullptr);
        setController(f, Hand::Right, aimAt(c.x, faceY - 0.01 - 0.03, c.z), poseAt(0, -1, 1));
        ui.update(f, nullptr);
        const Element* e = ui.element("main", "toggle");
        CHECK(e && e->toggled && log.count(ElementEventKind::Toggled, "toggle") == 1, "I2 vippan påslagen");
        double value = -1.0;
        for (const ElementEvent& ev : log.events)
            if (ev.kind == ElementEventKind::Toggled)
                value = ev.value;
        CHECK(nearTo(value, 1.0), "I2 Toggled bär 1");
        log.clear();
    }
    {   // I3: reglaget — värdet ur pekarens läge, med steg
        const Vec3 c = elementCentre(2);
        const SliderTrack track = sliderTrack(0.2, 0.04, false, false);
        const double x = c.x + track.left + 0.37 * (track.right - track.left);
        setController(f, Hand::Right, aimAt(x, faceY + 0.007 - 0.03, c.z), poseAt(0, -1, 1));
        ui.update(f, nullptr);
        CHECK(nearTo(ui.element("main", "slider")->value, 0.4), "I3 värdet vid trycket, steg 0,1");
        CHECK(log.count(ElementEventKind::ValueChanged, "slider") == 1, "I3 ValueChanged");
        const double x2 = c.x + track.left + 0.82 * (track.right - track.left);
        setController(f, Hand::Right, aimAt(x2, faceY + 0.007 - 0.03, c.z), poseAt(0, -1, 1));
        ui.update(f, nullptr);
        CHECK(nearTo(ui.element("main", "slider")->value, 0.8), "I3 värdet följer pekaren medan den trycks");
        setController(f, Hand::Right, aimAt(x2, faceY - 0.01 - 0.03, c.z), poseAt(0, -1, 1));
        ui.update(f, nullptr);
        CHECK(log.count(ElementEventKind::Clicked, "slider") == 0, "I3 reglaget klickar inte");
        log.clear();
    }
    {   // I4: strålen — avtryckaren trycker, släpp över elementet ger Clicked
        const Vec3 c = elementCentre(0);
        setController(f, Hand::Right, aimAt(c.x, -0.6, c.z), poseAt(0, -1, 1));
        setButtons(f, Hand::Right, false, false);
        ui.update(f, nullptr);
        CHECK(ui.state("main", "button").hovered && ui.consumes(Hand::Right), "I4 strålen över knappen");
        setButtons(f, Hand::Right, true, false);
        ui.update(f, nullptr);
        CHECK(ui.state("main", "button").pressed && log.count(ElementEventKind::Pressed, "button") == 1, "I4 avtryckaren trycker");
        f.deltaSeconds = 0.03;
        ui.update(f, nullptr);
        ui.update(f, nullptr);
        CHECK(ui.state("main", "button").pressDepth > 0.9, "I4 animerad intryckning");
        setButtons(f, Hand::Right, false, false);
        ui.update(f, nullptr);
        CHECK(log.count(ElementEventKind::Released, "button") == 1 && log.count(ElementEventKind::Clicked, "button") == 1,
              "I4 släpp över elementet klickar");
        // Släpp utanför ger inget Clicked.
        setButtons(f, Hand::Right, true, false);
        ui.update(f, nullptr);
        setController(f, Hand::Right, aimAt(c.x + 0.3, -0.6, c.z), poseAt(0, -1, 1));
        setButtons(f, Hand::Right, false, false);
        ui.update(f, nullptr);
        CHECK(log.count(ElementEventKind::Clicked, "button") == 1, "I4 släpp utanför klickar inte");
        f.deltaSeconds = 0.016;
        log.clear();
    }
    {   // I5: vredet — squeeze och handens rullning
        const Vec3 c = elementCentre(3);
        setController(f, Hand::Right, aimAt(c.x, -0.6, c.z), poseAt(0, -1, 1));
        setButtons(f, Hand::Right, false, false);
        ui.update(f, nullptr);
        setButtons(f, Hand::Right, false, true);
        ui.update(f, nullptr);   // fäster vinkeln
        CHECK(ui.consumes(Hand::Right), "I5 vredet greppat tar handen");
        // Aim-posens högra axel vrids 90° medurs sett framifrån (x → −z).
        Controller& ctrl = f.controllers[1];
        ctrl.aim.pose->right = Vec3{0, 0, -1};
        ctrl.aim.pose->up    = Vec3{1, 0, 0};
        ui.update(f, nullptr);
        CHECK(nearTo(ui.element("main", "dial")->value, 0.5 + 1.0 / 3.0, 1e-6), "I5 vridning 90° ger en tredjedel av svepet");
        // Släppt och riktat bort från panelen: handen är fri.
        setController(f, Hand::Right, aimAt(c.x + 1.0, -0.6, c.z), poseAt(0, -1, 1));
        setButtons(f, Hand::Right, false, false);
        ui.update(f, nullptr);
        CHECK(!ui.consumes(Hand::Right), "I5 släppt vred");
        log.clear();
    }
    {   // I6: handtaget — grepp, flytt och släpp blir en World-panel
        const Vec3 c = elementCentre(4);
        setController(f, Hand::Right, aimAt(c.x, -0.6, c.z), poseAt(0.5, -0.5, 1.0));
        setButtons(f, Hand::Right, false, false);
        ui.update(f, nullptr);
        setButtons(f, Hand::Right, false, true);
        ui.update(f, nullptr);
        CHECK(log.count(ElementEventKind::Grabbed) == 1 && ui.consumes(Hand::Right), "I6 Grabbed");
        const Pose before = *ui.panelPose("main");
        setController(f, Hand::Right, aimAt(c.x, -0.6, c.z), poseAt(0.7, -0.5, 1.2));   // handen flyttas (0,2, 0, 0,2)
        ui.update(f, nullptr);
        const Pose after = *ui.panelPose("main");
        CHECK(nearTo(after.position.x - before.position.x, 0.2, 1e-9) && nearTo(after.position.z - before.position.z, 0.2, 1e-9),
              "I6 panelen följer handen");
        setButtons(f, Hand::Right, false, false);
        ui.update(f, nullptr);
        CHECK(log.count(ElementEventKind::Dropped) == 1 && !ui.consumes(Hand::Right), "I6 Dropped");
        const Panel* p = ui.panel("main");
        CHECK(p && p->mount.attach == Attach::World && nearTo(p->mount.pose.position.x, after.position.x) &&
                  nearTo(p->mount.pose.position.z, after.position.z),
              "I6 panelen är en World-panel med den nya posen");
        log.clear();
    }
    {   // I7: en panel utan hand-pose döljs; speglingen och skalan på händer
        Panel hand = makePanel("hand", LayoutKind::Row);
        hand.mount.attach = Attach::SecondaryHand;
        hand.mount.pose   = poseAt(0.1, 0.0, 0.05);
        hand.elements.push_back(makeElement("a", ElementKind::Button));
        ui.addPanel(hand);
        FrameInfo g = baseFrame();
        ui.update(g, nullptr);
        CHECK(!ui.panelPose("hand").has_value(), "I7 utan hand-pose finns ingen pose (dold)");
        setController(g, Hand::Left, poseAt(1, 0, 1), poseAt(1, 0, 1));
        setController(g, Hand::Right, poseAt(-1, 0, 1), poseAt(-1, 0, 1));
        g.anchor.scale = 2.0;
        ui.update(g, nullptr);   // förval: höger primär ⇒ SecondaryHand = vänster
        CHECK(ui.panelPose("hand").has_value() && nearTo(ui.panelPose("hand")->position.x, 1.0 - 0.2), "I7 vänster hand: speglat och skalat");
        ui.setPrimaryHand(Hand::Left);
        ui.update(g, nullptr);
        CHECK(nearTo(ui.panelPose("hand")->position.x, -1.0 + 0.2), "I7 vänster primär ⇒ den andra är höger");
        ui.removePanel("hand");
    }
    {   // I8: huvudet med mjuk följning
        Panel head = makePanel("head", LayoutKind::Row);
        head.mount.attach  = Attach::Head;
        head.mount.pose    = poseAt(0.0, 0.5, 0.0);
        head.mount.headLag = 0.5;
        head.elements.push_back(makeElement("a", ElementKind::Label));
        ui.addPanel(head);
        FrameInfo g = baseFrame();
        g.head.pose = worldPose(poseAt(0, 0, 1.7));
        ui.update(g, nullptr);
        CHECK(nearTo(ui.panelPose("head")->position.y, 0.5), "I8 framför huvudet");
        g.head.pose = worldPose(poseAt(1, 0, 1.7));
        ui.update(g, nullptr);
        CHECK(nearTo(ui.panelPose("head")->position.x, 0.5, 1e-9), "I8 halva vägen efter en bildruta");
        ui.removePanel("head");
    }
    {   // I9: ett inaktivt element tar inte emot närhet eller tryck
        Panel solo = makePanel("solo", LayoutKind::Row);
        solo.mount.pose = poseAt(3.0, 0.0, 1.0);
        Element off = makeElement("off", ElementKind::Button);
        off.enabled = false;
        solo.elements.push_back(off);
        ui.addPanel(solo);
        FrameInfo g = baseFrame();
        setController(g, Hand::Right, aimAt(3.0, faceY + 0.007 - 0.03, 1.0), poseAt(3, -1, 1));
        ui.update(g, nullptr);
        ui.update(g, nullptr);
        const ElementState s = ui.state("solo", "off");
        CHECK(!s.hovered && !s.pressed && !ui.consumes(Hand::Right), "I9 inaktivt element tar emot ingenting");
        CHECK(log.count(ElementEventKind::Pressed, "off") == 0, "I9 inget Pressed");
        // setElement byter ut det helt, och elementet är nu aktivt.
        off.enabled = true;
        ui.setElement("solo", off);
        ui.update(g, nullptr);
        CHECK(ui.element("solo", "off")->enabled && log.count(ElementEventKind::Pressed, "off") == 1,
              "I9 aktiverat element trycks");
        ui.removePanel("solo");
    }
    {   // I10: Squeeze med spetsen drar ett reglage; staven ritas
        Pointers wand = pointers;
        wand.draw     = true;
        ui.setPointers(wand);
        Panel sq = makePanel("sq", LayoutKind::Row);
        sq.mount.pose = poseAt(4.0, 0.0, 1.0);
        Element slider = makeElement("slider", ElementKind::Slider, 0.2, 0.04);
        slider.min = 0.0; slider.max = 1.0; slider.step = 0.1; slider.value = 0.0;
        sq.elements.push_back(slider);
        ui.addPanel(sq);
        const PanelLayout l = layoutPanel(*ui.panel("sq"));
        const double cx = 4.0 + l.places[0].u;
        const double cz = 1.0 + l.places[0].v;
        const SliderTrack track = sliderTrack(0.2, 0.04, false, false);
        const auto xAt = [&](double t) { return cx + track.left + t * (track.right - track.left); };

        FrameInfo g = baseFrame();
        setController(g, Hand::Right, aimAt(xAt(0.62), faceY - 0.01 - 0.03, cz), poseAt(4, -1, 1));
        setButtons(g, Hand::Right, false, false);
        ui.update(g, nullptr);
        CHECK(ui.state("sq", "slider").hovered && nearTo(ui.element("sq", "slider")->value, 0.0),
              "I10 spetsen nära reglaget utan tryck: bara närhet");
        setButtons(g, Hand::Right, false, true);
        ui.update(g, nullptr);
        CHECK(nearTo(ui.element("sq", "slider")->value, 0.6) && ui.consumes(Hand::Right),
              "I10 Squeeze vid spetsen tar reglaget på spetsens läge");
        setController(g, Hand::Right, aimAt(xAt(0.12), faceY - 0.01 - 0.03, cz), poseAt(4, -1, 1));
        ui.update(g, nullptr);
        CHECK(nearTo(ui.element("sq", "slider")->value, 0.1), "I10 reglaget följer spetsen medan greppet håller");
        setButtons(g, Hand::Right, false, false);
        setController(g, Hand::Right, aimAt(xAt(0.12) + 1.0, faceY - 0.6, cz), poseAt(4, -1, 1));
        ui.update(g, nullptr);
        CHECK(!ui.consumes(Hand::Right), "I10 släppt reglage, bort från panelen");
        ui.setPointers(pointers);
        ui.removePanel("sq");
        log.clear();
    }
    {   // I11: en panel som blicken lämnat tonar bort och tar inga tryck
        Panel vis = makePanel("vis", LayoutKind::Row);
        vis.mount.pose           = poseAt(8.0, 0.0, 1.0);
        vis.showWhen.gazeDegrees = 30.0;
        vis.showWhen.fadeSeconds = 0.05;
        vis.elements.push_back(makeElement("b", ElementKind::Button));
        ui.addPanel(vis);
        const PanelLayout l = layoutPanel(*ui.panel("vis"));
        const double bx = 8.0 + l.places[0].u;
        const double bz = 1.0 + l.places[0].v;

        FrameInfo g = baseFrame();
        g.head.pose = worldPose(poseAt(8.0, -3.0, 1.0));   // blicken +y, rakt mot panelen
        setController(g, Hand::Right, aimAt(bx, faceY - 0.01 - 0.03, bz), poseAt(8, -1, 1));
        ui.update(g, nullptr);
        CHECK(ui.state("vis", "b").hovered, "I11 blicken på panelen: den syns och går att peka på");
        g.head.pose = worldPose(poseAt(0.0, -3.0, 1.0));   // blicken åt sidan
        for (int i = 0; i < 10; ++i)
            ui.update(g, nullptr);
        CHECK(!ui.state("vis", "b").hovered, "I11 bortvänd blick: bortonad panel tar inga tryck");
        g.head.pose = worldPose(poseAt(8.0, -3.0, 1.0));
        for (int i = 0; i < 10; ++i)
            ui.update(g, nullptr);
        CHECK(ui.state("vis", "b").hovered, "I11 blicken tillbaka: panelen tonar in");

        // Den andra handen: villkoret är bara handen, blicken åt sidan.
        Panel byHand = vis;
        byHand.showWhen.gazeDegrees      = 0.0;
        byHand.showWhen.otherHandMetres  = 0.35;
        ui.setPanel("vis", byHand);
        g.head.pose = worldPose(poseAt(0.0, -3.0, 1.0));
        setController(g, Hand::Right, aimAt(bx, -3.0, bz), poseAt(8, -3, 1));   // långt bort
        for (int i = 0; i < 10; ++i)
            ui.update(g, nullptr);
        CHECK(!ui.state("vis", "b").hovered, "I11 handen långt bort: panelen är bortonad");
        setController(g, Hand::Right, aimAt(bx, faceY - 0.01 - 0.03, bz), poseAt(8, -0.2, 1));   // nära
        for (int i = 0; i < 10; ++i)
            ui.update(g, nullptr);
        CHECK(ui.state("vis", "b").hovered, "I11 handen nära: panelen tonar in");
        ui.removePanel("vis");
    }
}

} // namespace

int main()
{
    layoutCases();
    placementCases();
    visibilityCases();
    pressCases();
    valueCases();
    interfaceCases();
    if (g_failures != 0) {
        std::cerr << g_failures << " av " << g_checks << " kontroller föll\n";
        return 1;
    }
    std::printf("interface3d_gate: alla %d kontroller gröna\n", g_checks);
    return 0;
}
