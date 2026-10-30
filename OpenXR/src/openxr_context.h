#pragma once

// OpenXR-sidan: loadern (openxr_loader.dll laddas i körtid, alla funktioner
// hämtas med xrGetInstanceProcAddr), instansen, systemet, sessionen med en
// D3D11-enhet, rummen och ett swapchain per öga. Allt här körs i XR-tråden.

#include <windows.h>
#include <unknwn.h>
#include <d3d11.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glaze_openxr/math.h>
#include "d3d11_upload.h"
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

// Ett ögas färdiga bild för en bildruta.
struct EyeSubmission {
    const std::uint8_t* rgba = nullptr;   // width × height, täta rader
    int                 width  = 0;
    int                 height = 0;
    TrackingPose        pose;             // ögat i LOCAL, som det lästes
    Fov                 fov;              // det synfält bilden täcker
};

class OpenXrContext {
public:
    // Laddar loadern och skapar instans, system, D3D11-enhet, session, rum och
    // swapchains. Fel lämnas som svensk text i `error`; då är allt redan
    // avvecklat. `resolutionScale` är × runtimens rekommenderade bildmått.
    static std::unique_ptr<OpenXrContext> create(double resolutionScale, std::string& error);
    ~OpenXrContext();

    OpenXrContext(const OpenXrContext&)            = delete;
    OpenXrContext& operator=(const OpenXrContext&) = delete;

    const XrFunctions& fn() const noexcept { return fn_; }
    XrInstance instance() const noexcept { return instance_; }
    XrSession  session() const noexcept { return session_; }
    XrSpace    localSpace() const noexcept { return localSpace_; }
    // XR_BD_controller_interaction finns och är aktiverad.
    bool bdControllerExtension() const noexcept { return bdControllerExtension_; }
    // Runtimens bildmått per öga.
    const std::array<ViewSize, 2>& viewSizes() const noexcept { return viewSizes_; }

    // Texten för ett OpenXR-resultat: "xrAnrop: RESULTATNAMN (kod)".
    std::string describe(const char* call, XrResult result) const;

    // Händelserna som väntar. Sessionens tillståndsbyte läggs i `phases`.
    struct Events {
        std::vector<Phase> phases;
        bool referenceSpaceChanged = false;
        bool instanceLost          = false;
    };
    bool pollEvents(Events& out, std::string& error);

    bool beginSession(std::string& error);
    bool endSession(std::string& error);
    bool requestExitSession(std::string& error);

    struct FrameTiming {
        XrTime     predictedDisplayTime   = 0;
        XrDuration predictedDisplayPeriod = 0;
        bool       shouldRender           = false;
    };
    bool waitFrame(FrameTiming& out, std::string& error);
    bool beginFrame(std::string& error);

    // Ögonen i LOCAL vid `time`. `valid` när båda ögonens läge och riktning
    // är giltiga.
    struct EyePoses {
        bool valid = false;
        std::array<TrackingPose, 2> poses;
        std::array<Fov, 2>          fov;
    };
    bool locateViews(XrTime time, EyePoses& out, std::string& error);
    // Huvudet (VIEW) i LOCAL vid `time`.
    bool locateHead(XrTime time, PoseSample& out, std::string& error);

    // Avslutar bildrutan utan lager.
    bool endFrameEmpty(XrTime time, std::string& error);
    // Laddar upp båda ögonens bilder och avslutar bildrutan med ett
    // projektionslager. Misslyckas uppladdningen avslutas bildrutan utan
    // lager innan felet lämnas.
    bool endFrameProjection(XrTime time, const std::array<EyeSubmission, 2>& eyes, std::string& error);

private:
    OpenXrContext() = default;

    struct EyeSwapchain {
        XrSwapchain   handle = XR_NULL_HANDLE;
        std::uint32_t width  = 0;
        std::uint32_t height = 0;
        std::vector<XrSwapchainImageD3D11KHR> images;
    };

    bool loadLoader(std::string& error);
    bool createInstance(std::string& error);
    bool createSystem(std::string& error);
    bool createSession(std::string& error);
    bool createSwapchains(double resolutionScale, std::string& error);
    bool uploadEye(std::size_t eye, const EyeSubmission& submission, std::string& error);
    void destroy() noexcept;

    HMODULE                  loader_ = nullptr;   // lämnas laddad: runtimen kan ha trådar kvar
    XrFunctions              fn_;
    XrInstance               instance_ = XR_NULL_HANDLE;
    XrSystemId               systemId_ = XR_NULL_SYSTEM_ID;
    bool                     bdControllerExtension_ = false;
    XrGraphicsRequirementsD3D11KHR graphicsRequirements_{};
    std::unique_ptr<D3D11Upload> d3d_;
    XrSession                session_ = XR_NULL_HANDLE;
    XrSpace                  localSpace_ = XR_NULL_HANDLE;
    XrSpace                  viewSpace_  = XR_NULL_HANDLE;
    std::array<ViewSize, 2>  viewSizes_{};
    std::array<EyeSwapchain, 2> swapchains_;
    bool                     swapRedBlue_ = false;
};

} // namespace glaze::openxr::detail
