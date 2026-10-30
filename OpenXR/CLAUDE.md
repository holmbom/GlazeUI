# OpenXR — sidopaketet som visar en Canvas i ett PC-VR-headset (lib glaze_openxr + glaze_openxr_ui)

**UTANFÖR ramverket.** `glaze` länkar aldrig paketet — OpenXR och en XR-tråd
är inte GUI, och en app som inte behöver VR får det inte med sig. Två target,
Threads-formen:

- **`glaze_openxr`** — KÄRNAN, matematiken. Ren std, bygger fristående
  (`cmake -S OpenXR -B b`), och `tests/openxr_math_gate` länkar bara den.
- **`glaze_openxr_ui`** — SESSIONEN `glaze::openxr::Session`: loadern, OpenXR-
  sessionen, kontrollerna och bryggan mot Canvas. Skapas bara när `glaze`
  finns och bara på Windows. Ingen självregistrering, så inget WHOLE_ARCHIVE
  och inget ankare.

`GLAZE_OPENXR` (rot-CMakeLists) är **AV som förval**: paketet hämtar Khronos
färdigbyggda OpenXR-loader med FetchContent (`cmake/OpenXRLoader.cmake`, låst
med SHA-256), och GlazeUI:s vanliga bygge ska vara beroendefritt. På annat OS
än Windows stoppar växeln konfigureringen med skälet.

Canvas ritar DIREKT i runtimens swapchain-bilder, på Canvas-fönstrets egen
D3D11-enhet: paketet har ingen egen enhet, ingen CPU-kopia och ingen
`renderToBitmap`. Bryggan går genom `glaze::detail::ExternalCanvasPass`
(`glaze/canvas/external_pass.h`, internt och inte i `glaze.h`) och
`gfx::Renderer`s integrationsanrop (`gfx/native.h`). D3D11- och OpenXR-typer
stannar i `src/`; `session.h` och `math.h` har inga.

## Så använder en app det

```cmake
set(GLAZE_OPENXR ON CACHE BOOL "" FORCE)   # före FetchContent_MakeAvailable(GlazeUI)
target_link_libraries(minapp PRIVATE glaze glaze_openxr_ui)
glaze_openxr_runtime(minapp)               # openxr_loader.dll bredvid programmet
```

```cpp
#include <glaze_openxr/session.h>

gfx::setGpuPreference(gfx::GpuPreference::HighPerformance);   // först i main

session_ = std::make_unique<glaze::openxr::Session>(scen, callbacks);
session_->start();          // asynkront; läget kommer i onStatus
session_->requestStop();    // blockerar inte; Stopped/Failed kommer i onStatus
```

Kortet väljs av APPEN före första fönstret (`gfx::setGpuPreference` eller
`gfx::setGpuAdapter`). Paketet kontaktar runtimen först i `start()`: då
jämförs Canvas-renderarens enhet (`nativeDevice()`: API, LUID, funktionsnivå)
med runtimens krav. Fel kort, fel API eller för låg nivå ger `Failed` med det
förväntade och det faktiska kortets namn och id — enheten byggs aldrig om.

Ingen `.glazeui`-form: sessionen är ingen widget och ingen komponent, och den
skapas i användarkoden (som `glaze::Worker`).

## Filerna

| Fil | Roll |
|-----|------|
| `include/glaze_openxr/math.h` | publik, ren std: OpenXR-rum → Glaze-världen, förankring, kameravinklar, `Fov` |
| `include/glaze_openxr/session.h` | publik: `Session`, `Options` (`msaaSamples` m.fl.), `Callbacks` (`onStatus`, `onFrame`, `onPick`), `State` — inga OpenXR-, Windows- eller D3D11-typer |
| `src/math.cpp` | matematiken (`glaze_openxr`) |
| `src/session_state.h` | internt: `LifeCycle` (sessionstillstånden, fokusräknaren), `JobGate` (överlämningen XR-tråd → UI-tråd), `FrameRequest`, `EyeImageSet`, `Run` (en starts tillstånd), `Shared`, `Teardown` |
| `src/session.cpp` | `Session::Impl`, XR-tråden (`Worker`), bildkällan mot pumpen (`UiFrameSink`), UI-trådens bildrutejobb (`UiFrameContent`), enhetskontrollen och avvecklingen |
| `src/openxr_context.h/.cpp` | `XrRuntime` (loader, instans, system, grafikkrav), `OpenXrContext` (session, rum, swapchains), `XrSessionApi` (anropen mot funktionstabellen, bildägandet), `SessionLoop` (XR-trådens tillståndsmaskin), `completeFrame` (UI-trådens del av en bildruta) |
| `src/actions.h/.cpp` | action set, bindningar för fyra kontrollprofiler, dödzon, avtryckarhysteres |
| `src/canvas_bridge.h/.cpp` | UI-trådens del: `ExternalCanvasPass`, swapchain-bilderna som importerade mål, förankring, förflyttning, `onFrame`, plockning med `castRay`, `onPick`, ritningen av båda ögonen |
| `cmake/OpenXRLoader.cmake` | FetchContent av loadern + `glaze_openxr_runtime(<target>)` |

## Trådarna

- **UI-tråden** äger Canvas, grafikenheten och alla callbacks. Alla publika
  anrop görs där, och all ritning, plockning och GPU-markör.
- **XR-tråden** äger OpenXR-sessionens tillståndsmaskin och bildrutans
  väntan: `xrPollEvent`, `xrWaitFrame` samt förvärv och väntan av båda ögonens
  swapchain-bild (`XrSessionApi::waitFrame`/`acquireImages`). Den rör aldrig
  Canvas och aldrig enheten.
- **Bildrutan är ett jobb** till pumpen: XR-tråden lämnar det med
  `glaze::detail::postExternalFrame` på paketets bildkälla
  (`registerExternalFrameSource(30)`) och väntar på kvittot i `JobGate`
  (Idle → Posted → Running → Done). Källan har högst ett väntande jobb, och
  pumpen kör det före ramverkets och appens poster. Jobbet gör i UI-tråden:
  `xrBeginFrame`, `xrLocateViews`, huvudet och kontrollerna vid
  `predictedDisplayTime`, förflyttningen, `onFrame`, plockningen,
  `ExternalCanvasPass::render` för båda ögonen, släpp av bilderna och
  `xrEndFrame` med runtimens egen pose och fov per öga. `shouldRender ==
  false`, en ogiltig pose eller en bild som inte kom i tid ger begin/end utan
  lager.
- **UI-tråden väntar aldrig på XR-tråden**, utom i `~Session`. XR-trådens
  väntan på ett jobb avbryts av stoppflaggan; ett jobb som ännu inte börjat
  avbryts och körs aldrig, ett som körs väntas in.
- Postade lambdas håller delat tillstånd och kontrollerar `alive` och sin
  körning (`Run`) innan de rör Canvas eller callbacks.
- **Skrivbordet** stryps av pumpen till 30 Hz och presenterar osynkat medan
  bildkällan finns (`gfx::Renderer::setExternalFramePacing`); full takt
  återställs när källan avregistrerats.
- Stopped/Failed levereras först när tråden är nedriven och joinad.

## Avvecklingen

Swapchains och sessionen får inte rivas medan GPU:n använder dem eller medan
importerade mål lever. Ordningen: inga nya jobb (bildkällan avregistreras) →
en påbörjad bildruta avslutas → `insertCompletion` lägger en GPU-markör efter
allt som skickats → UI-tråden frågar markören med en timer (10 ms) mellan
pumpens uppdrag, så pumpen sover emellan → målen släpps (ett ögas första
mål, som de andra delar hjälpytor med, sist) och passet → XR-tråden, som
väntar tills UI-sidan sagt ifrån, river rum, swapchains, session och
instans. Bara en bekräftad markör (Complete) släpper fram den vanliga rivningen.
Förlorad enhet (Failed) river ändå men ger sluttillståndet Failed med texten.
Går GPU:ns tidsgräns (5 s) ut utan besked rivs sessionen och swapchains INTE
(objekten och enhetsreferensen lämnas kvar) och sluttillståndet blir Failed.
Enheten hålls av en ägande referens från UI-trådens hämtning tills sessionen
är riven. Bara `~Session` får vänta i UI-tråden, för Canvas ska överleva
målen och passet; den gör avvecklingen själv och släpper XR-tråden.

## Bilderna

- Swapchainen har sRGB-format (`R8G8B8A8_UNORM_SRGB` eller
  `B8G8R8A8_UNORM_SRGB`), användning `COLOR_ATTACHMENT | SAMPLED`, ett prov
  och runtimens REKOMMENDERADE mått per öga — aldrig nedskalat.
- Varje bild importeras EN gång som ett `gfx::RenderTarget`
  (`importRenderTarget`) med MSAA (`Options::msaaSamples` 4/2/1, begränsat
  av `supportedSamples`) och D32F-djup. Renderaren skriver genom en egen
  UNORM-vy av bilden, aldrig en sRGB-vy, så värdena inte kodas två gånger.
  Ögats första bild äger de provtagna hjälpytorna; de andra bilderna delar
  dem (`sharedAttachments`), så en swapchain med flera bilder kostar EN
  uppsättning. Målet rensas inte vid import utan när det binds.
- Ingen djupbild skickas till runtimen; djupbufferten är lokal.

## Koordinater och kamera

- OpenXR: x höger, y upp, −z framåt. Glaze: x öster, y norr, z upp.
  Översättningen är `M(x, y, z) = (x, −z, y)`, roterad med förankringens yaw.
- Förankringen tas ur canvasens kamera vid start (huvudet hamnar i `eye()`,
  blicken mot `azimuth`) och flyttas bara av spakarna.
- Kameran per öga är en `gfx::View3D`: ögats läge i världen, runtimens
  ASYMMETRISKA frustum som `FrustumTangents` (tangenterna av fov-vinklarna,
  ingen symmetrisering) och klipplanen `nearMetres`/`farMetres`. `box` är hela
  målet i fysiska px.
- Plockningens vy är huvudet med ett frustum som rymmer båda ögonens; den
  styr skärmstorlekar och toleranser i `ExternalCanvasPass::castRay`.
- Canvas ska ha en `Camera3D` med `UpAxis::Z` och en världsenhet per meter.

## Fallgropar

- **Fel kort:** Canvas-fönstret måste rita på det kort runtimen kräver.
  Anropa `gfx::setGpuPreference(HighPerformance)` eller `setGpuAdapter` före
  appens första fönster; sessionen går annars till `Failed`.
- **Ritningen:** huvudvyns lager med `onPaint`/`onPaintLayer` (en gång per
  öga och bildruta — de får inte ändra tillstånd per anrop). Del-vyer, kant
  och fokusring hör till fönstret. Etiketterna har egna platser per öga
  (`kExternalLabels − n`); skrivbordets kamera, mål och cache rörs aldrig.
- **Färgen:** renderarens färger är redan sRGB-kodade och skrivs utan ny
  kodning till en sRGB-swapchain.
- **Fokus:** utan fokus läses reglagen som släppta, och efter en fokusförlust
  ger avtryckaren ett val först efter ett släpp.
- **Kostnaden:** MSAA är GPU-tid; sänk `msaaSamples` före något annat.
  Upplösningen väljs inte av appen.
- `castRay` träffar ytor, inte linjer eller etiketter.
- **Runtimen** är den som är registrerad som aktiv i
  `HKLM\SOFTWARE\Khronos\OpenXR\1\ActiveRuntime` — loadern väljer ingen själv.
  Saknas den går sessionen till Failed med texten, och appen lever vidare.
