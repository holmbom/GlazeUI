# OpenXR — Canvas ritar direkt i headsetets texturer

Sidopaketet `OpenXR` (`glaze_openxr`, `glaze_openxr_ui`) visar i dag en Canvas-scen i ett PC-VR-headset
genom `Canvas::renderToBitmap` per öga: en symmetrisk bild, synkron readback, CPU-uppladdning till en
egen D3D11-enhet och halv upplösning (`Options::resolutionScale = 0.5`). Det fungerar men är långsamt —
runtimen mäter omkring 80 ms från bildrutans start till inlämning.

**Målet:** Canvas ritar sin scen direkt i runtimens swapchain-bilder, på Canvas-fönstrets egen
D3D11-enhet, med varje ögas asymmetriska frustum och runtimens rekommenderade mått — full upplösning,
aldrig nedskalad — och en bildruta per uppdatering vid 72–90 Hz.

## Utfall

Klar och provad i headset på Windows: full rekommenderad upplösning (1920 × 1920 per öga) med 4×
MSAA. Runtimen mäter 2 ms från bildrutans start till inlämning (tidigare omkring 80 ms), 1 ms kodning
och 8 ms total latens. Två avvikelser från stegen nedan:

- `selectGraphicsAdapter` finns inte. En runtime klarade inte en kortlivad instans som skapades och
  revs vid appens start, och runtimen ska inte kontaktas förrän en session startar. En app väljer
  kortet före första fönstret med `gfx::setGpuPreference` eller `gfx::setGpuAdapter`; en session på
  fel kort går till `Failed` med förväntat och faktiskt kort.
- `registerExternalFrameSource` tar bara `desktopHz`: strypningen gäller alla fönster.

## Beslut

- **Full rekommenderad upplösning.** `resolutionScale` tas bort. MSAA 4× som förval, valbart 2×/1×
  (`Options::msaaSamples`) om mätningen kräver det.
- **Skrivbordet** ritas osynkat och högst 30 Hz medan en session kör; full takt efter stopp.
- **Utanför planen:** full takt under Windows modala flytt- och storleksloop, och djup till runtimen
  (`XR_KHR_composition_layer_depth`). En lokal D32F-djupbuffert ingår.
- **CPU-vägen tas bort**, med paketets egen D3D11-enhet och den symmetriska ögonanpassningen — ingen
  reserv. `Canvas::renderToBitmap` står kvar (den har andra användare och är jämförelsen i grindarna).
- **Kortet väljs före första fönstret** med en ny fri funktion; enheten byggs aldrig om automatiskt.
- **D3D11- och OpenXR-typer stannar i `OpenXR/src`**, som Terminal använder ConPTY i `Terminal/src`.
  Fasaden lämnar bara ut opaka pekare; `Glaze/widgets` och publika headers ser inga backendtyper.

## Fakta i koden som styr planen

- En D3D11-enhet **per fönsterrenderare**: `D3dRenderer::init`
  ([d3d11_renderer.cpp:354](../../../Glaze/gfx/src/backend_d3d11/d3d11_renderer.cpp)). OpenXR binds till just
  Canvas-fönstrets enhet — samma kort räcker inte.
- Kortlistan med LUID och stabilt id: `d3d11_gpu.cpp:126–147` (`gpu::pciAdapterId`).
- Glazes mål är BGRA8 UNORM, och resolve är hårdkodad till det formatet (`d3d11_target.cpp:142`).
- `FrameUniforms` ([camera.h:210](../../../Glaze/gfx/include/gfx/camera.h)) bygger strålar som
  `forward + ndc.x·right + ndc.y·up` — det förutsätter ett symmetriskt frustum. Förspelen finns i
  `d3d11_program.cpp`, `metal_program.mm` och `linux_program.cpp`.
- Skrivbordets present: `d3d11_renderer.cpp:815`; storleksloopens `setUnsyncedPresent` i
  `d3d11_window.cpp:1569`.
- Scenritningen: `renderView` i `Glaze/widgets/src/canvas/target3d.cpp:87`, bildvägen `renderBitmap3D` vid 401.
- Pumpen: `Glaze/widgets/src/app.cpp:456` (postdränering 766, budget 784); resursbudgeten förnyas vid
  skrivbordsritning i `app_window.cpp:2292`.

## Gränssnitten (skrivs först, låsta under arbetet)

**gfx-fasaden** (`renderer.h`, `image.h`, `camera.h`, `platform.h`):

```cpp
enum class NativeGraphicsApi { None, D3D11 };
struct NativeDevice { NativeGraphicsApi api = NativeGraphicsApi::None; void* device = nullptr;  // lånad
                      std::uint64_t localAdapterId = 0; std::uint32_t featureLevel = 0; };
enum class TargetColorFormat { Bgra8Unorm, Rgba8Unorm };
struct ExternalTargetDesc { NativeGraphicsApi api; void* image;  // ägs av anroparen
                            int width, height; TargetColorFormat color; TargetDesc target; };
enum class GpuCompletionState { Pending, Complete, Failed };
class GpuCompletion { public: virtual ~GpuCompletion() = default;
                      virtual GpuCompletionState poll(std::string& error) = 0; };

// Renderer — nya rena virtuella (D3D11 implementerar; Metal, Linux och Null svarar "stöds inte"):
virtual NativeDevice nativeDevice() const = 0;
virtual std::unique_ptr<RenderTarget> importRenderTarget(const ExternalTargetDesc&,
        RenderTarget* sharedAttachments, std::string& error) = 0;   // delar MSAA/djup per öga
virtual void setExternalFramePacing(bool on) = 0;                    // osynkat, DO_NOT_WAIT
virtual std::unique_ptr<GpuCompletion> insertCompletion(std::string& error) = 0;

// GpuAdapter:  std::optional<std::uint64_t> localAdapterId;   (LUID på D3D11)
// camera.h:    struct FrustumTangents { double left, right, down, up; };
//              View3D: std::optional<FrustumTangents> frustum;
//              FrameUniforms: float rayCenter[4];  sist (240 → 256 byte); forward behålls för djupet
```

**Canvas, internt** (ny header i `Glaze/widgets/src/canvas/`, inte i `glaze.h`):

```cpp
namespace glaze::detail {
struct ExternalCanvasView { gfx::RenderTarget* target; gfx::View3D view; };
class ExternalCanvasPass {
public:
    static std::unique_ptr<ExternalCanvasPass> create(Canvas&, std::string& error);
    gfx::Renderer& renderer() const;
    bool render(std::span<const ExternalCanvasView>, std::string& error);   // båda ögonen, ett block
    std::vector<HitResult> castRay(const Ray3&, const HitOptions&, const gfx::View3D& view) const;
};
using ExternalFrameSourceId = std::uint64_t;
ExternalFrameSourceId registerExternalFrameSource(AppWindow& owner, unsigned desktopHz);
bool postExternalFrame(ExternalFrameSourceId, std::function<void()> job);   // trådsäker, högst ett
void unregisterExternalFrameSource(ExternalFrameSourceId);
}
```

**OpenXR, publikt:** `bool glaze::openxr::selectGraphicsAdapter(std::string& error);` (före första
fönstret) · `Options`: `resolutionScale` bort, `msaaSamples = 4` till.

## Steg

1. **Gränssnitten och bygget.** Deklarationerna ovan, CMake (`OpenXR/CMakeLists.txt`,
   `tests/CMakeLists.txt`) och de låsta betydelserna: vilken tråd som äger vad, format, stoppordning.
   En app som använder paketet anropar `selectGraphicsAdapter` före sitt första fönster.
2. **Kortet.** `gpuAdapters()` fyller `localAdapterId` ur den befintliga LUID-uppräkningen.
   `selectGraphicsAdapter` skapar en tillfällig XR-instans, hämtar grafikkraven, matchar LUID och anropar
   `gfx::setGpuAdapter`. `Session::start` kontrollerar Canvas-renderarens faktiska enhet, LUID och
   funktionsnivå mot sessionens krav; fel kort ger `Failed` med förväntat och faktiskt id.
3. **Projektionen.** `FrustumTangents` genom `camera3d.cpp` (`viewProjection` 143, `rayThrough` 187,
   `frameUniforms` 460), `pxPerUnitAt` och frustumplanen; etiketternas vyjämförelse (`labels.cpp:42`),
   plockningens skärmstorlekar (`pick3d.cpp:293`) och LOD/zoomvillkor; `rayCenter` i alla tre förspelen
   och i `scene3d.cpp:446`. Det symmetriska fallet är oförändrat.
4. **Externa mål (D3D11).** `importRenderTarget` kontrollerar samma enhet, mått och formatfamilj;
   skapar egna vyer (UNORM-RTV på runtimens TYPELESS sRGB-bild, så värdena inte gammakodas två gånger),
   D32F, ett MSAA-färgmål och resolve i målets format (resolve i `d3d11_target.cpp:142` blir
   formatmedveten); rensar inte vid import; hjälpresurserna delas per öga. `insertCompletion` är en
   event-query som pollas utan väntan.
5. **Skrivbordstakten.** `setExternalFramePacing`: osynkat `Present` med `DXGI_PRESENT_DO_NOT_WAIT`,
   `DXGI_ERROR_WAS_STILL_DRAWING` hoppas över; separat från storleksloopens `unsyncedPresent`. Pumpen
   stryper skrivbordsritningen till `desktopHz` för alla fönster medan en källa är registrerad.
6. **Canvas-passet.** `ExternalCanvasPass` ur `renderView`/`renderBitmap3D`: huvudvyns lager, rutnätet,
   terrängen, etiketterna (egna platser per öga) och `onPaint`/`onPaintLayer`; båda ögonen i ett
   offscreen-block med en scenuppdatering; resursbudgeten förnyas per stereoblock; skrivbordets kamera,
   mål och cache rörs aldrig; ingen `readTarget`. `castRay` med uttrycklig vy för kontrollerns stråle.
7. **Pumpen.** `registerExternalFrameSource`/`postExternalFrame` i `app.cpp`: en prioriterad plats före
   den vanliga postkön, högst ett utestående jobb, generationskontroll, och inga väckningar när källan är
   borta — sessionen driver bildrutorna (regel 1).
8. **Sessionen.** XR-tråden: `xrWaitFrame`, acquire och wait per öga, postar bildjobbet och väntar på
   kvittens. UI-tråden: `xrBeginFrame`, `xrLocateViews`, huvudet och kontrollerna vid
   `predictedDisplayTime`, förflyttningen, `onFrame`, plockningen, `ExternalCanvasPass::render`, release
   och `xrEndFrame` med runtimens egen fov per öga. `shouldRender == false` ger begin/end utan lager.
   Stopp: inga nya jobb → påbörjad frame avslutas → `insertCompletion` → poll utan att blockera pumpen →
   omslag, swapchains och session rivs. `d3d11_upload.*`, symmetriseringen och `resolutionScale` tas bort.

## Risker

Fel enhet trots rätt kort (texturens enhet kontrolleras vid import) · fönstret stängs medan sessionen
kör · `xrWaitFrame` saknar timeout · en förvärvad bild som inte är klar · förlorad enhet ger ett
terminalt `Failed` · dubbel gammakodning eller kanalbyte · D3D11-tillstånd som läcker till UI-ritningen
(djupläget) · etiketter, LOD och plockning måste använda samma vy · en lång användarcallback kan inte
förbigås · full takt under fönsterdrag ingår inte.

## Plattformar

Windows/D3D11 implementeras och provas. Metal, Linux och Null: de nya virtuella svarar "stöds inte", och
förspelens `rayCenter` ändras blint i Metal och Linux — rader i `doc/os/macos/TESTS.md` och
`doc/os/linux/TESTS.md`. Ingen ny virtuell på `gfx::Window`.

## Uppdelning

Gränssnitten (steg 1) skrivs först. Därefter två delar med disjunkta filer som kan göras parallellt:

- **Grafiken:** `Glaze/gfx/src/camera3d.cpp`, backendernas implementationer (`backend_d3d11/`,
  `backend_metal/`, `backend_linux/`, Null), grindarna för projektion, uniformlayout och externa mål.
- **Canvas, pump och session:** `Glaze/widgets/src/app.cpp`, `app_window.cpp`,
  `Glaze/widgets/src/widgets/canvas.cpp`, `Glaze/widgets/src/canvas/` (`labels.cpp`, `pick3d.cpp`,
  `target3d.cpp`), `OpenXR/src/`, grindarna för postkön, Canvas-passet och sessionen.

## Grindarna

- `scene_math_gate` — asymmetriskt frustum mot handhärledda värden.
- `mesh_program_gate` — 256 byte, offsets och samma layout i alla förspel.
- `external_target_gate` (ny) — TYPELESS RGBA och BGRA i ett dolt fönster, 1×/2×/4×: färg, djup,
  resolve, fel enhet och fel mått.
- Canvas-pixlar — importerat mål mot `renderToBitmap` vid symmetriskt frustum, asymmetriskt mot en
  oberoende geometrisk referens; skrivbordsbilden oförändrad.
- `post_gate` — prioritet, högst ett jobb, avregistrering, sovande pump, återställd takt.
- `openxr_session_gate` (ny) — funktionstabellen som mock: wait/begin/end-ordning, bildägande, timeout,
  `shouldRender == false`, stopp och förlust.
- `openxr_math_gate` — uppdaterad.

Före en commit på branchen körs de nya och berörda grindarna; hela sviten (statisk, delad och röktest)
före merge till `main`.

## Mätning i headset (full upplösning)

Tid från `xrWaitFrame` till UI-jobbet, bildväntan, scenuppdateringen, CPU per öga, GPU per öga med
resolve (asynkrona tidsstämplar — `RenderStats` utökas, i dag undantas offscreen, `renderer.h:383`),
missade perioder och skrivbordets presentation. 60 s vid 72 och vid 90 Hz: median, p95 och p99, och
runtimens egen latens före och efter.

## Dokumenten

`MANUAL.md` (`openxr`, `gfx`, `platforms`, `canvas-3d`, `canvas-views` och shaderavsnittets
uniformkontrakt), `OpenXR/CLAUDE.md`, gfx- och canvas-`CLAUDE.md`, rot-`CLAUDE.md` (arkitekturraden),
`VERSION.md` (Nästa), raden i `TODO.md` och `doc/os/*/TESTS.md`.
