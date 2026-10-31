#pragma once

// PLATTA PANELERS RENA DELAR — träffpunkten på ytan som pixel i panelens
// fönster och musens tillstånd mot den. Ingen session, ingen GPU och inget
// fönster, så delarna provas direkt (tests/flat_panel_gate).
//
// PANELENS RAM är 3D-panelernas (interface3d_logic.h): x åt höger, z uppåt, y
// in i panelen, framsidan mot −y. Ytan ligger i y = 0 med mitten i origo;
// tavlan (kanter, ram och baksida) sträcker sig bakåt till y = tjockleken.
// Pixeln (0, 0) är ytans övre vänstra hörn.

#include <vector>

#include <gfx/events.h>
#include <gfx/geometry.h>

#include "interface3d_logic.h"

namespace glaze::openxr::detail {

// Hur långt in stav och finger går för ett tryck (meter): Pressed vid
// `kPressOn` och Released under `kPressOff` av den sträckan, som knapparnas.
inline constexpr double kFlatTravel = 0.008;
// Spakens fulla utslag rullar så här många rader per sekund.
inline constexpr double kWheelRowsPerSecond = 10.0;

// Ytans och tavlans mått (meter). Med tjockleken 0 finns bara ytan.
struct FlatGeometry {
    double surfaceWidth = 0.0, surfaceHeight = 0.0;
    double boardWidth   = 0.0, boardHeight   = 0.0;   // ytan och ramen
    double thickness    = 0.0;
};
FlatGeometry flatGeometry(double width, double height, double thickness, double border);

// Ytan som mått för spetsprovet (`evalTip`): djup 0, intryckningen kFlatTravel.
ElementGeom flatTipGeom(const FlatGeometry& g);

// Punkten (panelens ram, meter) som pixel i fönstret: px = (x/w + ½)·W,
// py = (½ − z/h)·H. Utanför ytan ger pixlar utanför [0, W] × [0, H].
gfx::Pointf flatPixel(Vec3 local, double surfaceWidth, double surfaceHeight, int pxWidth, int pxHeight);
// Pixeln ligger inom fönstret (kanterna räknas in).
bool flatInside(gfx::Pointf px, int pxWidth, int pxHeight);

// Strålen (panelens ram) mot ytans plan: bara framifrån — en stråle bakifrån
// eller parallell med ytan träffar inte. `local` och `px` gäller också när
// träffen ligger utanför ytan (`inside` falskt), så ett drag kan fortsätta.
struct FlatRay {
    bool        hit    = false;
    bool        inside = false;
    double      t      = 0.0;
    Vec3        local;
    gfx::Pointf px{};
};
FlatRay flatRay(Vec3 origin, Vec3 direction, double surfaceWidth, double surfaceHeight,
                int pxWidth, int pxHeight);

// ---------------------------------------------------------------- MUSEN
// Musen mot en panel. Panelen har EN mus: den hand som äger den matar den.
struct FlatMouse {
    bool        inside = false;   // musen är över ytan (hovern)
    bool        down   = false;   // vänster knapp är nere
    bool        hasPos = false;
    gfx::Pointf pos{};            // senaste läget (fysiska px)
};

struct FlatMouseInput {
    bool        present = false;   // handen pekar på panelen: `px` gäller
    gfx::Pointf px{};
    bool        inside  = false;   // `px` ligger på ytan
    bool        down    = false;   // vänster musknapp (avtryckaren eller tryckdjupet)
    // Avbrott: ett pågående tryck blir MouseCaptureLost, aldrig ett släpp (inget
    // klick), och hovern släcks.
    bool        cancel  = false;
};

// Ett steg: händelserna panelens fönster ska få, i ordning.
//   utan tryck  hover in (MouseMotion), rörelse, hover ut (MouseLeave); ett
//               tryck börjar bara på ytan (MouseButtonDown)
//   med tryck   draget är låst till panelen: MouseMotion följer även utanför
//               ytan tills knappen släpps (MouseButtonUp) — släppt utanför
//               släcks hovern efteråt; en träff som tappas håller sista läget
//   avbrott     MouseCaptureLost (om knappen var nere) och MouseLeave
std::vector<gfx::Event> stepFlatMouse(FlatMouse& mouse, const FlatMouseInput& in);

// Hjulet: rader (positivt = uppåt) ur spakens utslag under `dt` sekunder, och
// en MouseWheel-händelse på `px`.
double     wheelRows(double stick, double dt);
gfx::Event wheelEvent(gfx::Pointf px, double rows);

} // namespace glaze::openxr::detail
