# OpenXR — händer och komfort

**Status: klar.** `hand_input_gate`, `hand_navigation_gate` och `comfort_gate` är skrivna men körs först före merge till `main`.

## Bakgrund
Handspårningen ger i dag 26 leder samt nyp/grepp i `FrameInfo::hands`, men utan kontroller finns
inget sikte (ingen stråle), inga knapphändelser och ingen förflyttning, och händerna ritas inte.
Ingen komfort finns vid artificiell rörelse. E gör händerna till fullvärdiga ingångar genom SAMMA
`Controller`-fält som kontrollerna — så pekaren, Interface3D, platta paneler, tangentbordet, grepp
och förflyttning fungerar oförändrat — och lägger till valbar komfort. Appen väljer allt.

## Låsta gränssnitt (samordnaren skriver först)
**`session.h`:**
```cpp
// Controller:
enum class Source : std::uint8_t { None, Controller, Hand };
Source        source = Source::None;      // vilken väg som matar handen just nu
std::uint64_t inputEpoch = 0;             // ändras när källan byts eller bryts → pågående interaktion avbryts
TrackedPose   pinch, poke;                // handens nyp- och pekpunkt (hand-källan)
bool          navigationReserved = false; // handen styr förflyttningen (HandNavigation)

enum class HandNavigation : std::uint8_t { None, MoveTurn, Teleport };
// Locomotion: HandNavigation handNavigation = None; double handStickRangeMetres = 0.12;

struct Comfort {
    bool   vignette = false, teleportFade = false;
    double fullMoveMetresPerSecond = 1.5, fullTurnDegreesPerSecond = 90.0, fullScaleRatePerSecond = 1.0;
    double minFieldDegrees = 60.0, featherDegrees = 15.0;
    double attackSeconds = 0.08, releaseSeconds = 0.20;
    double fadeOutSeconds = 0.10, fadeInSeconds = 0.15;
};
// Options: Comfort comfort;   Session: void setComfort(const Comfort&);
```
Med `teleportFade` köas `teleportTo` och den inbyggda landningen: tona ut → en helt svart bildruta
→ `placeFeet` → tona in. `Options::handTracking` slår också på handinteraktionen.

**Ny `glaze_openxr/hand_visual.h`:** `HandVisual(Canvas&, int layer)`, `setVisible`,
`setStyle(HandVisualStyle{color, radiusScale})`, `update(const FrameInfo&)` — fingrar och handflata
som linjer (aldrig träffbara), i ett eget lager.

**gfx (`renderer.h/.cpp`):** `bool Renderer::pushTargetPreserved(RenderTarget&)` — binder ett
enprovs-mål utan djup UTAN att rensa (för vinjetten ovanpå scenen); andra mål avvisas. Gemensam kod;
D3D11/Linux binder redan utan rensning, Metal med `MTLLoadActionLoad`; Null avvisar. Minneslappar
i `doc/os/{macos,linux}/TESTS.md`.

`session_state.h` (interna `ControllerSample`/`HandSample` med källa, poser och epoch) låses av
samordnaren före delarna.

## Handvägar, i ordning
1. **`XR_EXT_hand_interaction`** — profilen `/interaction_profiles/ext/hand_interaction_ext`:
   `aim/pose`, `grip/pose`, `pinch_ext/pose`, `poke_ext/pose`, `pinch_ext/value`,
   `aim_activate_ext/value`, `grasp_ext/value`, `*_ready_ext` (ingen haptik i bindningen).
2. **`XR_FB_hand_tracking_aim`** — `XrHandTrackingAimStateFB` kedjad på `XrHandJointLocationsEXT::next`
   i `locateHands`: `aimPose`, `pinchStrength*`, `status` (VALID krävs; systemgest avbryter).
3. **Härlett ur lederna** — sikte axel→handled/pekfingrets grundled (axeln uppskattad ur huvudet),
   utjämnat; nyp/grepp ur befintliga `computeHandMetrics`.
`Trigger` = aim_activate/nyp, `Squeeze` = grasp, med befintlig hysteres 0,55/0,45 och
InputEvent-flanker (`actions.cpp:13–95`). En producent per hand; vid källbyte levereras släppflanker,
`inputEpoch` ökar, och grepp/tryck avbryts utan klick.

## Steg och uppdelning (två delar med disjunkta filer)
**Samordnaren först:** headers ovan, `session_state.h`, CMake (`OpenXR/CMakeLists.txt`,
`tests/CMakeLists.txt`), tomma startfiler för de nya källorna.

**Del 1 — händer:** `openxr_context.h/.cpp` (tilläggen, `xrGetActionStatePose`, FB-kedjan),
`actions.h/.cpp` (handprofilen, källval, knapparna), `session.cpp` (bildruteordningen: leder →
actions/källval → en knappuppdatering), nya `hand_logic.{h,cpp}` (härlett sikte, utjämning),
`hand_visual.{h,cpp}`, `interface3d.cpp` (ta bort egen nypgräns `kPinchGrab`; `gather` läser
`Trigger`/`Squeeze`; fingertoppen = närlägets spets också i Auto/Ray — ingen ovillkorlig Wand;
reserverad hand ger ingen UI-interaktion; ny `inputEpoch` → `endPress(...,false)`, `dropGrab` och
platta panelers musavbrott). Grindar: `hand_input_gate` (härlett sikte, hysteres, källbyte).

**Del 2 — förflyttning och komfort:** `locomotion.h/.cpp` (handnavigation: nyp som virtuell spak
för MoveTurn, höger nyp = teleport; landningsbegäran i stället för direkt `placeFeet`; förlorad
spårning avbryter teleport utan landning), nya `hand_navigation.{h,cpp}`, `comfort_logic.{h,cpp}`
(styrka ur UTFÖRD artificiell rörelse: m/s, °/s, |log skala|/s; attack/release; snäppvridning som
puls), `comfort_pass.{h,cpp}` (fullskärmstriangel med svart alfa per öga, vinkeln ur ögats
asymmetriska fov), `canvas_bridge.h/.cpp` (applyLocomotion med handnavigation, teleportsekvensen
med svart bildruta och kvitto, vinjetten efter `pass_->render` på en importerad enprovs-vy utan djup
av varje swapchain-bild), `Glaze/gfx/include/gfx/renderer.h` + `src/renderer.cpp`
(`pushTargetPreserved`). Grindar: `comfort_gate` (styrka, monotoni, dt=0, NaN, fysisk huvudrörelse
ger 0), `hand_navigation_gate`.

**Sist:** testappen — handspårning på, `HandVisual` i ett eget lager, komfort och
handnavigation som val i verktygslådan; integration, bygge 0 varningar, dokumenten.

## Risker
Kontroll och spårad hand samtidigt (en producent per hand); `ready=false`/systemgest/förlust får
aldrig ge tryck eller landning; källbyte mitt i en- eller tvåhandsgrepp → rent släpp; NaN och
saknade leder; nya teleportbegäranden ersätter väntande, fokusförlust avbryter, aldrig fast i svart;
handritningen aldrig ett pekmål; MSAA-hjälpytorna delas mellan swapchain-bilder — återbind aldrig
scenens MSAA-mål för vinjetten.

## Plattformar
OpenXR bara Windows. `pushTargetPreserved` gemensam: Metal/Linux blint, Null avvisar; minneslappar.

## Tester (skrivs, körs före merge)
`hand_input_gate`, `hand_navigation_gate`, `comfort_gate`, `openxr_session_gate` utökad (mockad
runtime: EXT → FB → leder, ingen runtime-kontakt före start). Under arbetet bara bygget.

## Dokumenten
`MANUAL.md` (`openxr` + `gfx` för `pushTargetPreserved`), `OpenXR/CLAUDE.md`, gfx-`CLAUDE.md` om ej vid
taket (annars fråga), `VERSION.md`, `TODO.md`, `doc/os/*/TESTS.md`, testappens README.

## Verifiering
Bygget 0 varningar, sedan prov i headset: händer utan
kontroller (stråle, nyp-klick, grepp, panel, tangentbord), handnavigation, vinjett och svarttoning.
