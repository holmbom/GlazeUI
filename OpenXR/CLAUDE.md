# OpenXR — sidopaketet som visar en Canvas i ett PC-VR-headset (lib glaze_openxr + glaze_openxr_ui)

**UTANFÖR ramverket.** `glaze` länkar aldrig paketet — OpenXR, en egen
D3D11-enhet och en XR-tråd är inte GUI, och en app som inte behöver VR får det
inte med sig. Två target, Threads-formen:

- **`glaze_openxr`** — KÄRNAN, matematiken. Ren std, bygger fristående
  (`cmake -S OpenXR -B b`), och `tests/openxr_math_gate` länkar bara den.
- **`glaze_openxr_ui`** — SESSIONEN `glaze::openxr::Session`: loadern, OpenXR-
  sessionen, den egna D3D11-enheten, kontrollerna och bryggan mot Canvas.
  Skapas bara när `glaze` finns och bara på Windows. Ingen självregistrering,
  så inget WHOLE_ARCHIVE och inget ankare.

`GLAZE_OPENXR` (rot-CMakeLists) är **AV som förval**: paketet hämtar Khronos
färdigbyggda OpenXR-loader med FetchContent (`cmake/OpenXRLoader.cmake`, låst
med SHA-256), och GlazeUI:s vanliga bygge ska vara beroendefritt. På annat OS
än Windows stoppar växeln konfigureringen med skälet.

Paketet har ingen egen 3D-motor: varje öga ritas av Canvas själv med
`renderToBitmap`, och pixlarna laddas upp till OpenXR:s swapchains på
paketets egen D3D11-enhet. Vägen använder bara ramverkets publika API och är
därför långsam; att rita direkt i headsetets texturer kräver nya krokar i
`gfx` och `Canvas`.

## Så använder en app det

```cmake
set(GLAZE_OPENXR ON CACHE BOOL "" FORCE)   # före FetchContent_MakeAvailable(GlazeUI)
target_link_libraries(minapp PRIVATE glaze glaze_openxr_ui)
glaze_openxr_runtime(minapp)               # openxr_loader.dll bredvid programmet
```

```cpp
#include <glaze_openxr/session.h>

session_ = std::make_unique<glaze::openxr::Session>(scen, callbacks);
session_->start();          // asynkront; läget kommer i onStatus
session_->requestStop();    // blockerar inte; Stopped/Failed kommer i onStatus
```

Ingen `.glazeui`-form: sessionen är ingen widget och ingen komponent, och den
skapas i användarkoden (som `glaze::Worker`).

## Filerna

| Fil | Roll |
|-----|------|
| `include/glaze_openxr/math.h` | publik, ren std: OpenXR-rum → Glaze-världen, förankring, kameravinklar, symmetrisk projektion |
| `include/glaze_openxr/session.h` | publik: `Session`, `Options`, `Callbacks` (`onStatus`, `onFrame`, `onPick`), `State` — inga OpenXR-, Windows- eller D3D11-typer |
| `src/math.cpp` | matematiken (`glaze_openxr`) |
| `src/session_state.h` | internt: `LifeCycle` (sessionstillstånden, fokusräknaren), `FrameRequest`/`FrameResult`, delat tillstånd `Run`/`Shared` |
| `src/session.cpp` | `Session::Impl`, XR-tråden (`Worker`), bildloopen och leveransen till UI-tråden |
| `src/openxr_context.h/.cpp` | loadern (`LoadLibraryW` + `xrGetInstanceProcAddr`, `XR_NO_PROTOTYPES`), instans, system, session, rum, swapchains, bildrutans anrop |
| `src/d3d11_upload.h/.cpp` | egen D3D11-enhet på runtimens LUID, `UpdateSubresource`, `waitIdle` före rivning |
| `src/actions.h/.cpp` | action set, bindningar för fyra kontrollprofiler, dödzon, avtryckarhysteres |
| `src/canvas_bridge.h/.cpp` | UI-trådens del: förankring, förflyttning, `onFrame`, plockning med `castRay`, `onPick`, ögonens `renderToBitmap` |
| `cmake/OpenXRLoader.cmake` | FetchContent av loadern + `glaze_openxr_runtime(<target>)` |

## Trådarna

- **UI-tråden** äger Canvas och alla callbacks. Alla publika anrop görs där.
- **XR-tråden** äger alla OpenXR-anrop och D3D11-kontexten. Per bildruta:
  `xrWaitFrame` → `xrBeginFrame` → ögon, huvud och kontroller vid
  `predictedDisplayTime` → ETT uppdrag via `glaze::app().post` → väntan →
  upload → `xrEndFrame` med den RITADE fov:en.
- **UI-tråden väntar aldrig på XR-tråden**, utom i `~Session` (join). XR-trådens
  väntan på uppdraget avbryts av stoppflaggan, så joinen aldrig låser.
- Postade lambdas håller delat tillstånd och kontrollerar `alive` och sin
  körning (`Run`) innan de rör Canvas eller callbacks.
- Stopped/Failed levereras först när tråden är nedriven och joinad.

## Koordinater och kamera

- OpenXR: x höger, y upp, −z framåt. Glaze: x öster, y norr, z upp.
  Översättningen är `M(x, y, z) = (x, −z, y)`, roterad med förankringens yaw.
- Förankringen tas ur canvasens kamera vid start (huvudet hamnar i `eye()`,
  blicken mot `azimuth`) och flyttas bara av spakarna.
- Kameran per öga: `Camera3D::lookFrom(öga, yaw, pitch, fov, UpAxis::Z)` +
  `roll`. Rollens tecken följer `camera.h` (positivt vrider kameran medurs).
- Canvas ska ha en `Camera3D` med `UpAxis::Z` och en världsenhet per meter.

## Fallgropar

- **Symmetrisk bild:** `renderToBitmap` tar bara ett symmetriskt, vertikalt
  synfält. Bilden täcker ögats asymmetriska frustum, och projektionslagret får
  det synfält som faktiskt ritades — aldrig runtimens rekommenderade.
- **Färgen:** `Bitmap` är RGBA8 med rak alfa, rad 0 överst. Swapchainen är
  sRGB och bytena laddas upp oförändrade.
- **Fokus:** utan fokus läses reglagen som släppta, och efter en fokusförlust
  ger avtryckaren ett val först efter ett släpp.
- **Kostnaden:** två nya render-mål, synkron readback och uppladdning per
  bildruta. `Options::resolutionScale` (0,5) är startvärdet.
- `renderToBitmap` kör appens `onPaint`/`onPaintLayer` — de får inte ändra
  tillstånd per anrop. `castRay` träffar ytor, inte linjer eller etiketter.
- **Runtimen** är den som är registrerad som aktiv i
  `HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime` — loadern väljer ingen själv.
  Saknas den går sessionen till Failed med texten, och appen lever vidare.
