#pragma once

// 3D-GRÄNSSNITTETS RENA DELAR — det som inte rör ritytan: layouten, handens
// förskjutning och speglingen, tryckdjupet med hysteres, reglagets och
// vredets värden och strålens skärning mot en framsida. Ingen ritning, ingen
// session och ingen GPU, så delarna provas utan fönster (tests/interface3d_gate).
//
// PANELENS RAM: x åt höger, z uppåt (panelens plan), y in i panelen. Framsidan
// vetter mot −y: en panel utan vridning står upprätt och ses från −y (söder),
// och en panel med förskjutningen (0, 0,5, 0) i huvudets ram hamnar framför
// ansiktet, vänd mot det. Plattans framkant ligger i y = 0 och plattan sträcker
// sig bakåt till y = tjockleken. Ett elements kropp sitter framför plattan,
// y ∈ [−djup, 0], och dess framsida (där text och ikon ligger) i y = −djup.
// ELEMENTETS RAM har samma axlar med origo i elementets mitt på plattans
// framkant; en knapp trycks alltså in längs +y.
//
// Alla mått är meter i panelens ram, före `Anchor::scale`.

#include <cstddef>
#include <vector>

#include <glaze_openxr/interface3d.h>

#include "hands.h"
#include "vector_ops.h"

namespace glaze::openxr::detail {

gfx::Color mixColor(gfx::Color a, gfx::Color b, double t);

// Barnets pose i förälderns ram, med barnets läge skalat (vridningen skalas
// inte) — en panels eller ett elements ram i världen.
Pose composeScaled(const Pose& parent, const Pose& child, double scale);

// Spetsen räknas till ett element eller en platta ända till så här långt
// bakom plattans baksida (meter).
inline constexpr double kBehindPlate = 0.01;

// ------------------------------------------------------------------- LAYOUT
// Elementets stil i panelen: dess egen eller panelens förval.
const ElementStyle& effectiveStyle(const Panel& panel, const Element& element);

// Var ett element står: mitten (u åt höger, v uppåt, i panelens ram med
// origo i panelens mitt) och dess egen storlek.
struct ElementPlace {
    double u = 0.0, v = 0.0;
    double width = 0.0, height = 0.0;
};

struct PanelLayout {
    double width = 0.0, height = 0.0;       // plattans mått
    double plateU = 0.0, plateV = 0.0;      // plattans mitt (Free: ur elementens utsträckning)
    std::vector<ElementPlace> places;       // ett per element, i ordning
};

// Cellerna ur Free (elementets `pose` i panelens ram), Grid (`row`/`column`/
// span), Row (elementen i ordning åt höger, `columnSpan` celler vardera) och
// Column (i ordning nedåt, `rowSpan`). Kolumnbredden och radhöjden är det
// största elementet i dem; ett element med span som är större än cellerna
// fördelar underskottet. Panelens storlek är innehållet och `padding` (om
// `autoSize`), annars `Layout::width/height` med innehållet justerat efter
// `horizontal`/`vertical`; ett element som är mindre än sin cell justeras
// likadant. Elementets `pose` läggs ovanpå av anroparen.
PanelLayout layoutPanel(const Panel& panel);

// Elementets pose i panelens ram: cellens mitt (framkanten, y = 0) med
// elementets `pose` ovanpå.
Pose elementFrame(const ElementPlace& place, const Element& element);

// ----------------------------------------------------------- PLACERING
// Förskjutningen speglad för vänster hand: x byter tecken och vridningen
// speglas i planet x = 0 (kvaternionen (x, −y, −z, w)), så samma `pose` ger
// samma läge relativt tummen i båda händerna.
Pose mirrorForLeftHand(const Pose& offset);

// Panelens pose i världen på en hand/huvud: handens pose gånger förskjutningen
// (positionen skalad med `scale`), speglad om `left && mount.mirrorForLeft`.
Pose attachedPose(const Pose& base, const Mount& mount, bool left, double scale);

// Kompassriktningen (grader, medurs från norr) en panels +y ska ha för att
// dess framsida (−y) ska peka mot `head`.
double yawFacing(Vec3 panelPosition, Vec3 head);
// `pose` med yaw bytt så att panelen vetter mot `head`; lutning och rullning
// behålls (vridning bara kring z).
Pose faceHead(const Pose& pose, Vec3 head);

// ---------------------------------------------------------------- PEKARE
// Kontrollens spets: aim-posens läge plus `tipOffset` längs forward, skalat.
Vec3 controllerTip(const WorldPose& aim, double tipOffset, double scale);

// -------------------------------------------------------------- SYNLIGHET
// Vinkeln (grader, 0 … 180) mellan blickriktningen `headForward` och
// riktningen från `head` till `target`. Är riktningen eller blicken noll, 180.
double gazeAngleDegrees(Vec3 head, Vec3 headForward, Vec3 target);
// Om villkoren i `Visibility` gäller: `gazeOk` och `handOk` är hur det gick
// för blicken respektive den andra handen. Inget villkor på ⇒ alltid sant;
// ett ⇒ det; båda ⇒ båda (`requireBoth`) eller något av dem.
bool visibleByConditions(const Visibility& visibility, bool gazeOk, bool handOk);
// Opaciteten (0 … 1) ett steg `dt` närmare 1 (`visible`) eller 0, så att hela
// vägen tar `fadeSeconds`; ≤ 0 sekunder ger ett omedelbart byte.
double stepFade(double fade, bool visible, double dt, double fadeSeconds);

// Elementets mått för pekarprov.
struct ElementGeom {
    double width = 0.04, height = 0.04;
    double depth = 0.012, travel = 0.008;
    double plateThickness = 0.006;
};

// En punkt (i elementets ram) mot framsidan.
struct TipEval {
    bool   over        = false;   // sidled inom rektangeln (med marginal)
    bool   inRange     = false;   // över, och nära nog framför/inne i elementet
    double penetration = 0.0;     // hur långt in längs +y från framsidan (negativt = framför)
    double proximity   = 0.0;     // 0 … 1, 1 vid och bakom framsidan
    double distance    = 0.0;     // avståndet framför framsidan, 0 vid eller bakom
};
TipEval evalTip(Vec3 local, const ElementGeom& g, double hoverDistance, double margin);

// Intryckningen 0 … 1 av `travel` ur hur långt spetsen gått in.
double pressDepthOf(double penetration, double travel);

// Hysteres: Pressed vid `on`, Released under `off`.
enum class PressEdge : std::uint8_t { None, Pressed, Released };
inline constexpr double kPressOn  = 0.7;
inline constexpr double kPressOff = 0.4;
PressEdge stepPress(bool& pressed, double depth, double on = kPressOn, double off = kPressOff);

// Strålen (i elementets ram) mot framsidans plan y = −depth.
struct RayHit {
    bool   hit    = false;   // strålen går in mot framsidan och skär planet
    bool   inside = false;   // träffen ligger inom rektangeln
    double t      = 0.0;     // sträckan längs strålen
    Vec3   local;            // träffpunkten
};
RayHit rayVsFace(Vec3 origin, Vec3 direction, double planeY, double width, double height);

// -------------------------------------------------------------- VÄRDEN
// Avrundat till närmaste steg från `min`, klampat till [min, max]; step ≤ 0 =
// steglöst.
double quantizeValue(double value, double min, double max, double step);

// Reglagets spår i elementets ram (x från elementets mitt): innehållet
// (text/ikon) får vänsterkanten, spåret resten.
struct SliderTrack {
    double left = 0.0, right = 0.0;     // spårets ändar i x
    double labelWidth = 0.0;            // innehållets plats längst till vänster
    double barHeight = 0.0;             // spårets tjocklek i z
    double knobWidth = 0.0, knobHeight = 0.0;
};
SliderTrack sliderTrack(double width, double height, bool hasText, bool hasIcon);
// Värdet för pekarens x (elementets ram) längs spåret.
double sliderValueAt(double x, const SliderTrack& track, double min, double max, double step);
// Knoppens x för ett värde.
double sliderKnobX(double value, const SliderTrack& track, double min, double max);

// Vredet: svepet från minimum till maximum, och handens rullning kring
// framsidans normal — medurs sett framifrån, i radianer, ur handens högra axel
// (x, z i elementets ram).
inline constexpr double kDialSweepDegrees = 270.0;
double rollAngle(double right_x, double right_z);
double unwrapAngle(double delta);   // till (−π, π]
// Värdet efter att handen vridit `turned` radianer (summan av bildrutornas
// vridningar, så mer än ett halvt varv går) från värdet `value0`.
double dialValueFrom(double value0, double turned, double min, double max, double step);
// Vredets markering som vinkel medurs från rakt upp (radianer).
double dialMarkerAngle(double value, double min, double max);

// ------------------------------------------------------------- FRAMSIDAN
// Texturens skala (px per meter) så ingen sida går över taket.
double textureScale(double faceWidth, double faceHeight, int pxPerMetre);
// Kantremsan mellan kroppen och framsidans yta.
double faceRim(const ElementStyle& style);

} // namespace glaze::openxr::detail
