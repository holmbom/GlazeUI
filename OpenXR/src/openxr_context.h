#pragma once

// OpenXR-sidan: loadern (openxr_loader.dll laddas i körtid, alla funktioner
// hämtas med xrGetInstanceProcAddr), instansen och systemet, sessionen på
// Canvas-renderarens D3D11-enhet, rummen och ett swapchain per öga.
//
// Anropen mot funktionstabellen ligger i `XrSessionApi`, som bara tar tabellen
// och handtagen — den rör varken loader, enhet eller fönster, och kan därför
// köras mot en tabell med egna funktioner. `SessionLoop` är XR-trådens
// tillståndsmaskin ovanpå den, och `completeFrame` är UI-trådens del av en
// bildruta.

#include <windows.h>
#include <unknwn.h>
#include <d3d11.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glaze_openxr/math.h>
#include "session_state.h"

namespace glaze::openxr::detail {

// Funktionerna som loadern ger oss. Namnen följer OpenXR:s.
#define GLAZE_OPENXR_XR_GLOBAL_FUNCTIONS(X) \
    X(xrEnumerateInstanceExtensionProperties) \
    X(xrCreateInstance)

#define GLAZE_OPENXR_XR_INSTANCE_FUNCTIONS(X) \
    X(xrDestroyInstance) \
    X(xrResultToString) \
    X(xrPollEvent) \
    X(xrGetSystem) \
    X(xrGetSystemProperties) \
    X(xrEnumerateViewConfigurations) \
    X(xrEnumerateViewConfigurationViews) \
    X(xrEnumerateEnvironmentBlendModes) \
    X(xrCreateSession) \
    X(xrDestroySession) \
    X(xrCreateReferenceSpace) \
    X(xrCreateActionSpace) \
    X(xrLocateSpace) \
    X(xrDestroySpace) \
    X(xrEnumerateSwapchainFormats) \
    X(xrCreateSwapchain) \
    X(xrDestroySwapchain) \
    X(xrEnumerateSwapchainImages) \
    X(xrAcquireSwapchainImage) \
    X(xrWaitSwapchainImage) \
    X(xrReleaseSwapchainImage) \
    X(xrBeginSession) \
    X(xrEndSession) \
    X(xrRequestExitSession) \
    X(xrWaitFrame) \
    X(xrBeginFrame) \
    X(xrEndFrame) \
    X(xrLocateViews) \
    X(xrStringToPath) \
    X(xrCreateActionSet) \
    X(xrDestroyActionSet) \
    X(xrCreateAction) \
    X(xrDestroyAction) \
    X(xrSuggestInteractionProfileBindings) \
    X(xrAttachSessionActionSets) \
    X(xrGetActionStateBoolean) \
    X(xrGetActionStateFloat) \
    X(xrGetActionStateVector2f) \
    X(xrSyncActions) \
    X(xrGetD3D11GraphicsRequirementsKHR)

struct XrFunctions {
    PFN_xrGetInstanceProcAddr xrGetInstanceProcAddr = nullptr;
#define GLAZE_OPENXR_DECLARE(name) PFN_##name name = nullptr;
    GLAZE_OPENXR_XR_GLOBAL_FUNCTIONS(GLAZE_OPENXR_DECLARE)
    GLAZE_OPENXR_XR_INSTANCE_FUNCTIONS(GLAZE_OPENXR_DECLARE)
#undef GLAZE_OPENXR_DECLARE
};

// En OpenXR-struktur med satt `type` och nollade fält.
template <class T>
T xrStruct(XrStructureType type) {
    T value{};
    value.type = type;
    return value;
}

inline TrackingPose fromXr(const XrPosef& p) {
    return TrackingPose{Vec3{p.position.x, p.position.y, p.position.z},
                        Quaternion{p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w}};
}

inline XrPosef toXr(const TrackingPose& p) {
    XrPosef pose{};
    pose.orientation = XrQuaternionf{static_cast<float>(p.orientation.x), static_cast<float>(p.orientation.y),
                                     static_cast<float>(p.orientation.z), static_cast<float>(p.orientation.w)};
    pose.position = XrVector3f{static_cast<float>(p.position.x), static_cast<float>(p.position.y),
                               static_cast<float>(p.position.z)};
    return pose;
}

// Kortets LUID som ett tal: HighPart i de övre 32 bitarna — samma form som
// `gfx::NativeDevice::localAdapterId` och `gfx::GpuAdapter::localAdapterId`.
inline std::uint64_t packLuid(const LUID& luid) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(luid.HighPart)) << 32) |
           static_cast<std::uint64_t>(luid.LowPart);
}

// Texten för ett OpenXR-resultat: "xrAnrop misslyckades: RESULTATNAMN (kod)".
// `instance` och `xrResultToString` får saknas; då står bara koden.
std::string describeResult(const XrFunctions& fn, XrInstance instance, const char* call, XrResult result);

// ---------------------------------------------------------------- ANROPEN

// Ett ögas lager i projektionen: runtimens EGEN pose och synfält för ögat,
// så som xrLocateViews gav dem.
struct EyeLayer {
    TrackingPose pose;
    Fov          fov;
};

// Ögonen i LOCAL vid en tid. `valid` när båda ögonens läge och riktning är
// giltiga.
struct LocatedViews {
    bool valid = false;
    std::array<TrackingPose, 2> poses;
    std::array<Fov, 2>          fov;
};

// Händelserna som väntar. Sessionens tillståndsbyte läggs i `phases`.
struct Events {
    std::vector<Phase> phases;
    bool referenceSpaceChanged = false;
    bool instanceLost          = false;
};

struct FrameTiming {
    XrTime     predictedDisplayTime   = 0;
    XrDuration predictedDisplayPeriod = 0;
    bool       shouldRender           = false;
};

enum class AcquireOutcome {
    Ready,      // båda ögonens bilder är förvärvade och väntade: de får ritas
    NotReady,   // runtimen släppte inte en bild i tid: rita inte, försök nästa bildruta
    Failed      // ett fel (text i `error`), eller för många bildrutor i följd utan bild
};

// Alla anrop mot funktionstabellen för en körande session. Rör inget annat
// än tabellen och handtagen den får, så den går att köra mot en tabell med
// egna funktioner.
//
// TRÅDARNA: klassen låser ingenting. Anropen följer en ordning som ger ett
// enda ägarskap: `waitFrame` och `acquireImages` görs av XR-tråden; sedan
// ägs bildrutan av den tråd som får den överlämnad (`beginFrame` …
// `endFrame*`), och XR-tråden rör inget swapchain förrän bildrutan är klar.
//
// BILDÄGANDET: varje ögas bild är Free, Acquired (förvärvad, inte väntad) eller
// Waited (väntad, får ritas). Bara en väntad bild släpps (`releaseImages`);
// en förvärvad som inte hann väntas färdigt väntas om i nästa bildruta, i
// stället för att en ny förvärvas.
class XrSessionApi {
public:
    struct Handles {
        XrInstance instance   = XR_NULL_HANDLE;
        XrSession  session    = XR_NULL_HANDLE;
        XrSpace    localSpace = XR_NULL_HANDLE;
        XrSpace    viewSpace  = XR_NULL_HANDLE;
        std::array<XrSwapchain, 2>    swapchains{};
        std::array<std::uint32_t, 2>  imageCounts{};   // bilder per swapchain
        std::array<std::int32_t, 2>   widths{};        // swapchainens mått i px
        std::array<std::int32_t, 2>   heights{};
    };
    struct Limits {
        XrDuration imageWaitTimeout  = 100'000'000;   // ns per väntan på en bild
        int        maxNotReadyFrames = 30;            // bildrutor i följd utan bild innan det är ett fel
    };

    XrSessionApi(const XrFunctions& fn, const Handles& handles, const Limits& limits);
    XrSessionApi(const XrFunctions& fn, const Handles& handles) : XrSessionApi(fn, handles, Limits{}) {}

    const XrFunctions& fn() const noexcept { return fn_; }
    XrInstance instance() const noexcept { return handles_.instance; }
    XrSession  session() const noexcept { return handles_.session; }
    XrSpace    localSpace() const noexcept { return handles_.localSpace; }
    std::string describe(const char* call, XrResult result) const {
        return describeResult(fn_, handles_.instance, call, result);
    }

    bool pollEvents(Events& out, std::string& error);
    bool beginSession(std::string& error);
    bool endSession(std::string& error);
    bool requestExitSession(std::string& error);

    // XR-tråden.
    bool waitFrame(FrameTiming& out, std::string& error);
    // Förvärvar och väntar båda ögonens bilder. `index` är bildernas plats i
    // swapchainens lista.
    AcquireOutcome acquireImages(std::array<std::uint32_t, 2>& index, std::string& error);

    // Den tråd som äger bildrutan.
    bool beginFrame(std::string& error);
    bool locateViews(XrTime time, LocatedViews& out, std::string& error);
    // Huvudet (VIEW) i LOCAL vid `time`.
    bool locateHead(XrTime time, PoseSample& out, std::string& error);
    // Släpper de väntade bilderna (också sådana som inte ritats). Alla släpps
    // också om en av dem ger fel; det första felet lämnas.
    bool releaseImages(std::string& error);
    // Avslutar bildrutan utan lager.
    bool endFrameEmpty(XrTime time, std::string& error);
    // Avslutar bildrutan med ett projektionslager över hela swapchainbilderna.
    bool endFrameProjection(XrTime time, const std::array<EyeLayer, 2>& eyes, std::string& error);

    // Hur många ögons bilder som just nu är väntade (0–2).
    int waitedImages() const noexcept;
    // Hur många ögons bilder som just nu är förvärvade men inte väntade.
    int acquiredImages() const noexcept;

private:
    enum class ImageState : std::uint8_t { Free, Acquired, Waited };

    const XrFunctions& fn_;
    Handles            handles_;
    Limits             limits_;
    std::array<ImageState, 2>    state_{ImageState::Free, ImageState::Free};
    std::array<std::uint32_t, 2> index_{};
    int                notReadyFrames_ = 0;
};

// ---------------------------------------------------------- BILDRUTAN, UI-SIDAN

// Det XR-tråden lämnar över när bildernas väntan är klar.
struct FrameTicket {
    std::uint64_t sequence     = 0;
    double        deltaSeconds = 0.0;
    bool          focused      = false;
    std::uint64_t focusEpoch   = 0;
    XrTime        predictedDisplayTime = 0;
    std::array<std::uint32_t, 2> imageIndex{};
};

enum class FillResult {
    Failed,    // ett fel (text i `error`): bildrutan avslutas utan lager
    NoLayer,   // inget att visa den här bildrutan (ingen giltig pose): avslutas utan lager
    Layer      // båda ögonens bilder är ritade; `layers` är fyllda
};

// Det UI-tråden fyller en bildruta med: läser lägen och reglage vid
// `predictedDisplayTime` och ritar i bilderna `imageIndex`.
class FrameContent {
public:
    virtual ~FrameContent() = default;
    virtual FillResult fill(const FrameTicket& ticket, std::array<EyeLayer, 2>& layers,
                            std::string& error) = 0;
};

// UI-trådens del av en bildruta: `xrBeginFrame`, `content.fill`, släpp av
// bilderna och `xrEndFrame` — med ett projektionslager eller utan. Bilderna
// släpps och bildrutan avslutas alltid, också vid fel eller ett undantag.
// Returnerar Done eller Failed.
SubmitResult completeFrame(XrSessionApi& api, const FrameTicket& ticket, FrameContent& content,
                           std::string& error);

// ------------------------------------------------------------ BILDRUTAN, XR-SIDAN

// Dit XR-tråden lämnar en bildruta vars bilder är väntade. Implementeras av
// sessionen, som kör `completeFrame` i UI-tråden.
class FrameSink {
public:
    virtual ~FrameSink() = default;
    // Lämnar `ticket` och väntar på kvittot. `Cancelled` när ett stopp kom
    // innan UI-tråden började; då gör anroparen (SessionLoop) färdigt
    // bildrutan själv.
    virtual SubmitResult submit(const FrameTicket& ticket, std::string& error) = 0;
};

struct LoopHooks {
    // Har ett stopp begärts (från UI-tråden)? Krävs.
    std::function<bool()> stopRequested;
    // Pausar högst `time`, avbruten av ett stopp. Krävs.
    std::function<void(std::chrono::milliseconds time)> sleep;
    // Det aktuella läget, vid varje varv. Får saknas.
    std::function<void(const Status&)> onStatus;
    // Fokus lämnade headsetet: reglagens hysteres ska släppas. Får saknas.
    std::function<void()> onFocusLost;
    // Tid utan svar från runtimen efter xrRequestExitSession innan sessionen
    // rivs ned ändå.
    std::chrono::milliseconds exitTimeout{5000};
    // Tid mellan händelsekontrollerna medan sessionen inte kör bildrutor.
    std::chrono::milliseconds idlePoll{10};
    // Längsta tid mellan två bildrutor som räknas in i `deltaSeconds`.
    double maxDeltaSeconds = 0.1;
};

// XR-trådens huvudloop: händelser, tillståndsmaskin och bildrutor tills
// sessionen är avvecklad. Rör inte Canvas — bildrutor går till `sink`.
class SessionLoop {
public:
    SessionLoop(XrSessionApi& api, FrameSink& sink, LoopHooks hooks);
    // Returnerar sluttillståndet (Stopped eller Failed).
    Status run();

private:
    bool frame();
    bool emptyFrame(XrTime time);

    XrSessionApi& api_;
    FrameSink&    sink_;
    LoopHooks     hooks_;
    LifeCycle     life_;
    XrTime        previousTime_ = 0;
    std::uint64_t sequence_ = 0;
};

// ------------------------------------------------------------ LOADERN, INSTANSEN

// Loadern, instansen och headsetets system — sessionens, och lika långlivade
// som den: runtimen kontaktas först när en session startas.
class XrRuntime {
public:
    // Laddar loadern och skapar instans och system. Fel lämnas som svensk
    // text i `error`; då är allt redan avvecklat.
    static std::unique_ptr<XrRuntime> open(std::string& error);
    ~XrRuntime();

    XrRuntime(const XrRuntime&)            = delete;
    XrRuntime& operator=(const XrRuntime&) = delete;

    const XrFunctions& fn() const noexcept { return fn_; }
    XrInstance instance() const noexcept { return instance_; }
    XrSystemId systemId() const noexcept { return systemId_; }
    // XR_BD_controller_interaction finns och är aktiverad.
    bool bdControllerExtension() const noexcept { return bdControllerExtension_; }
    const XrGraphicsRequirementsD3D11KHR& graphicsRequirements() const noexcept { return graphicsRequirements_; }
    std::string describe(const char* call, XrResult result) const {
        return describeResult(fn_, instance_, call, result);
    }

private:
    XrRuntime() = default;
    bool loadLoader(std::string& error);
    bool createInstance(std::string& error);
    bool createSystem(std::string& error);

    HMODULE                  loader_ = nullptr;   // lämnas laddad: runtimen kan ha trådar kvar
    XrFunctions              fn_;
    XrInstance               instance_ = XR_NULL_HANDLE;
    XrSystemId               systemId_ = XR_NULL_SYSTEM_ID;
    bool                     bdControllerExtension_ = false;
    XrGraphicsRequirementsD3D11KHR graphicsRequirements_{};
};

// Sessionen på Canvas-renderarens D3D11-enhet, rummen och ett swapchain per
// öga i runtimens fulla rekommenderade storlek. Äger runtimen (instansen).
class OpenXrContext {
public:
    // Skapar session, rum och swapchains på `device` (en `ID3D11Device*` som
    // ägs av Canvas-renderaren). Kortet och funktionsnivån ska redan vara
    // kontrollerade mot `runtime->graphicsRequirements()`. Fel lämnas som
    // svensk text i `error`; då är allt redan avvecklat.
    static std::unique_ptr<OpenXrContext> create(std::unique_ptr<XrRuntime> runtime, void* device,
                                                 std::string& error);
    ~OpenXrContext();

    OpenXrContext(const OpenXrContext&)            = delete;
    OpenXrContext& operator=(const OpenXrContext&) = delete;

    const XrFunctions& fn() const noexcept { return runtime_->fn(); }
    XrInstance instance() const noexcept { return runtime_->instance(); }
    XrSession  session() const noexcept { return session_; }
    XrSpace    localSpace() const noexcept { return localSpace_; }
    bool bdControllerExtension() const noexcept { return runtime_->bdControllerExtension(); }
    std::string describe(const char* call, XrResult result) const { return runtime_->describe(call, result); }

    XrSessionApi& api() noexcept { return *api_; }
    const std::array<EyeImageSet, 2>& eyeImages() const noexcept { return eyeImages_; }

private:
    OpenXrContext() = default;

    bool createSession(void* device, std::string& error);
    bool createSwapchains(std::string& error);
    void destroy() noexcept;

    std::unique_ptr<XrRuntime> runtime_;
    XrSession                  session_    = XR_NULL_HANDLE;
    XrSpace                    localSpace_ = XR_NULL_HANDLE;
    XrSpace                    viewSpace_  = XR_NULL_HANDLE;
    std::array<XrSwapchain, 2> swapchains_{};
    std::array<EyeImageSet, 2> eyeImages_;
    std::unique_ptr<XrSessionApi> api_;
};

} // namespace glaze::openxr::detail
