# OpenXR — interaktion: indata, grepp och förflyttning

Sidopaketet `OpenXR` visar en Canvas-scen i headsetet med full upplösning. Indatan är i dag avtryckaren
och spaken, och förflyttningen är fast. **Målet:** paketet ger byggstenarna för all slags interaktion
— alla knappar och poser, händer, strålar mot scenen, grepp med en och två händer, ytor och snäpp,
och förflyttning som appen kan styra helt — utan att bestämma vad appen gör med dem.

**Status: klar.** Stegen nedan är provade i headset. Tillagt efter provet, byggt men inte provat:
`Session::teleportTo`, rak teleportering (`Locomotion::teleportPath`), reservmarken `Options::groundZ`
och startläget ur kartkameran.

## Gränssnitten (låsta)

- `OpenXR/include/glaze_openxr/math.h` — `Anchor::scale` (världsskala), `Pose` med `compose`,
  `inverse`, `transformPoint/Direction`, `interpolate`, `toPose`/`toWorldPose`; `YawPitchRoll`
  (`glaze::Placement`s konvention); `followGrab`, `twoHandManipulate` (`TwoHandRotation`);
  `surfaceFrame`, `snapToGrid`, `snapAngle`; `ballisticArc`.
- `OpenXR/include/glaze_openxr/session.h` — `Button`/`ButtonState`/`InputEvent` (alla knappar,
  beröring, analoga värden), `Controller` (grip, aim, palm, hastigheter, profil, stråle med träffar,
  teleportbåge), `TrackedHand` (26 leder, nyp, grepp), `ReferenceSpace`, `Locomotion`
  (gå/flyga, mjuk/stegvis vridning, teleportering, greppa världen med skala), `Options`
  (referensrum, förflyttning, strålar, handspårning, uppdateringsfrekvens), `FrameInfo` (huvud,
  ögon, kontroller, händer, händelser), `Callbacks::onInput` i stället för `onPick`, och på
  `Session`: `anchor`/`setAnchor`/`recenter`, `setLocomotion`, `vibrate`/`stopVibration`,
  `refreshRates`/`setRefreshRate`, `castRay`; fria `toPlacement`/`fromPlacement`.

## Steg

1. **Matematiken** (`OpenXR/src/math.cpp`): skalan i förankringen genom `makeAnchor`/`toWorldPose`;
   de stela transformerna, vinklarna i Placement-konventionen (kontrollerad mot ritytans placering),
   greppen, ytramen, snäppen och kastbanan. Grinden `openxr_math_gate` får fallen.
2. **Indatan** (`OpenXR/src/actions.*`): action för varje knapp, beröring och värde med bindningar
   för OpenXR-specens vanligaste kontrollprofiler, handflatan när tillägget finns,
   hastigheter, aktiv profil per hand, flanker till `InputEvent`, vibration.
3. **Runtimen** (`OpenXR/src/openxr_context.*`): referensrummen (LocalFloor/Stage med reserv),
   handspårning och uppdateringsfrekvens när tilläggen finns.
4. **Sessionen** (`OpenXR/src/session*`, `canvas_bridge.*`): förankringen med skala och
   `setAnchor`/`recenter` från UI-tråden, den inbyggda förflyttningen (gå, flyga, vrida mjukt eller
   stegvis, teleportering med båge och träff segment för segment, greppa världen med en och två
   händer), strålarna varje bildruta, händelserna (`onInput` före `onFrame`), `castRay` mot
   headsetets vy, `toPlacement`/`fromPlacement` (`OpenXR/src/placement.cpp`).
5. **Testappen** visar förmågorna: lägesbyte för förflyttningen, grepp och flytt av lådor med en och
   två händer, en markör som läggs på en yta längs normalen med snäpp, vibration vid träff,
   världsskala.

## Prov

Bygget (0 varningar), sedan prov i headset. Grindarna skrivs men körs före merge till `main`.

## Dokumenten

`MANUAL.md` (`openxr`), `OpenXR/CLAUDE.md`, `VERSION.md`.
