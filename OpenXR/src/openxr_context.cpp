#include "openxr_context.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace glaze::openxr::detail {

namespace {

// Kör ett OpenXR-anrop; vid fel läggs texten i `error` och funktionen
// returnerar false.
#define GLAZE_OPENXR_XR_CHECK(call, name)                 \
    do {                                             \
        const XrResult openxrResult_ = (call);      \
        if (XR_FAILED(openxrResult_)) {             \
            error = describe(name, openxrResult_);  \
            return false;                            \
        }                                            \
    } while (0)

std::string toUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string out(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        out.data(), bytes, nullptr, nullptr);
    return out;
}

void copyName(char* destination, std::size_t capacity, const char* source) {
    std::snprintf(destination, capacity, "%s", source);
}

XrPosef identityPose() {
    XrPosef pose{};
    pose.orientation.w = 1.0f;
    return pose;
}

Phase toPhase(XrSessionState state) {
    switch (state) {
    case XR_SESSION_STATE_READY:        return Phase::Ready;
    case XR_SESSION_STATE_SYNCHRONIZED: return Phase::Synchronized;
    case XR_SESSION_STATE_VISIBLE:      return Phase::Visible;
    case XR_SESSION_STATE_FOCUSED:      return Phase::Focused;
    case XR_SESSION_STATE_STOPPING:     return Phase::Stopping;
    case XR_SESSION_STATE_EXITING:      return Phase::Exiting;
    case XR_SESSION_STATE_LOSS_PENDING: return Phase::LossPending;
    default:                            return Phase::Idle;
    }
}

constexpr int kFormatRgbaSrgb = 29;   // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
constexpr int kFormatBgraSrgb = 91;   // DXGI_FORMAT_B8G8R8A8_UNORM_SRGB

} // namespace

std::unique_ptr<OpenXrContext> OpenXrContext::create(double resolutionScale, std::string& error) {
    std::unique_ptr<OpenXrContext> self(new OpenXrContext());
    if (!self->loadLoader(error) ||
        !self->createInstance(error) ||
        !self->createSystem(error) ||
        !self->createSession(error) ||
        !self->createSwapchains(resolutionScale, error)) {
        return nullptr;   // destruktorn avvecklar det som hunnit skapas
    }
    return self;
}

OpenXrContext::~OpenXrContext() { destroy(); }

void OpenXrContext::destroy() noexcept {
    // Uppladdningarna till swapchain-bilderna ska vara körda på grafikkortet
    // innan swapchains förstörs.
    if (d3d_) d3d_->waitIdle(2000);
    for (EyeSwapchain& chain : swapchains_) {
        if (chain.handle != XR_NULL_HANDLE && fn_.xrDestroySwapchain) {
            fn_.xrDestroySwapchain(chain.handle);
        }
        chain = EyeSwapchain{};
    }
    if (viewSpace_ != XR_NULL_HANDLE && fn_.xrDestroySpace) fn_.xrDestroySpace(viewSpace_);
    if (localSpace_ != XR_NULL_HANDLE && fn_.xrDestroySpace) fn_.xrDestroySpace(localSpace_);
    viewSpace_ = XR_NULL_HANDLE;
    localSpace_ = XR_NULL_HANDLE;
    if (session_ != XR_NULL_HANDLE && fn_.xrDestroySession) fn_.xrDestroySession(session_);
    session_ = XR_NULL_HANDLE;
    d3d_.reset();
    if (instance_ != XR_NULL_HANDLE && fn_.xrDestroyInstance) fn_.xrDestroyInstance(instance_);
    instance_ = XR_NULL_HANDLE;
}

std::string OpenXrContext::describe(const char* call, XrResult result) const {
    char name[XR_MAX_RESULT_STRING_SIZE] = {};
    if (instance_ != XR_NULL_HANDLE && fn_.xrResultToString &&
        XR_SUCCEEDED(fn_.xrResultToString(instance_, result, name))) {
        return std::string(call) + " misslyckades: " + name + " (" + std::to_string(static_cast<int>(result)) + ")";
    }
    return std::string(call) + " misslyckades med OpenXR-koden " + std::to_string(static_cast<int>(result));
}

// ----------------------------------------------------------------- SKAPANDET

bool OpenXrContext::loadLoader(std::string& error) {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        error = "Programfilens sökväg gick inte att läsa, så openxr_loader.dll kan inte laddas.";
        return false;
    }
    path.resize(length);
    const std::size_t slash = path.find_last_of(L"\\/");
    path.resize(slash == std::wstring::npos ? 0 : slash + 1);
    path += L"openxr_loader.dll";

    loader_ = LoadLibraryW(path.c_str());
    if (!loader_) {
        error = "openxr_loader.dll hittades inte eller gick inte att ladda från programfilens katalog (" +
                toUtf8(path) + "). Bygg om så att DLL:en kopieras bredvid programmet.";
        return false;
    }
    // FARPROC → void(*)() → rätt funktionstyp: båda stegen är undantagna
    // från varningen om oförenliga funktionstyper.
    const auto proc = reinterpret_cast<void (*)()>(GetProcAddress(loader_, "xrGetInstanceProcAddr"));
    fn_.xrGetInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(proc);
    if (!fn_.xrGetInstanceProcAddr) {
        error = "openxr_loader.dll saknar xrGetInstanceProcAddr; filen är inte en OpenXR-loader.";
        return false;
    }
    return true;
}

bool OpenXrContext::createInstance(std::string& error) {
    // Funktionerna som fungerar utan instans.
#define GLAZE_OPENXR_LOAD_GLOBAL(name)                                                         \
    {                                                                                     \
        PFN_xrVoidFunction proc_ = nullptr;                                               \
        fn_.xrGetInstanceProcAddr(XR_NULL_HANDLE, #name, &proc_);                         \
        fn_.name = reinterpret_cast<PFN_##name>(proc_);                                   \
        if (!fn_.name) {                                                                  \
            error = "OpenXR-loadern saknar funktionen " #name ".";                        \
            return false;                                                                 \
        }                                                                                 \
    }
    GLAZE_OPENXR_XR_GLOBAL_FUNCTIONS(GLAZE_OPENXR_LOAD_GLOBAL)
#undef GLAZE_OPENXR_LOAD_GLOBAL

    std::uint32_t count = 0;
    GLAZE_OPENXR_XR_CHECK(fn_.xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr),
                     "xrEnumerateInstanceExtensionProperties");
    std::vector<XrExtensionProperties> extensions(count, xrStruct<XrExtensionProperties>(XR_TYPE_EXTENSION_PROPERTIES));
    GLAZE_OPENXR_XR_CHECK(fn_.xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data()),
                     "xrEnumerateInstanceExtensionProperties");
    const auto has = [&](const char* name) {
        return std::any_of(extensions.begin(), extensions.begin() + count,
                           [&](const XrExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
    };
    if (!has(XR_KHR_D3D11_ENABLE_EXTENSION_NAME)) {
        error = "OpenXR-runtimen saknar tillägget " XR_KHR_D3D11_ENABLE_EXTENSION_NAME
                " och kan inte visa bilder från en D3D11-enhet.";
        return false;
    }
    bdControllerExtension_ = has(XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME);

    std::vector<const char*> enabled{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    if (bdControllerExtension_) enabled.push_back(XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME);

    auto info = xrStruct<XrInstanceCreateInfo>(XR_TYPE_INSTANCE_CREATE_INFO);
    copyName(info.applicationInfo.applicationName, sizeof(info.applicationInfo.applicationName), "GlazeUI");
    copyName(info.applicationInfo.engineName, sizeof(info.applicationInfo.engineName), "GlazeUI");
    info.applicationInfo.applicationVersion = 1;
    info.applicationInfo.engineVersion = 1;
    info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    info.enabledExtensionCount = static_cast<std::uint32_t>(enabled.size());
    info.enabledExtensionNames = enabled.data();

    const XrResult created = fn_.xrCreateInstance(&info, &instance_);
    if (XR_FAILED(created)) {
        instance_ = XR_NULL_HANDLE;
        if (created == XR_ERROR_RUNTIME_UNAVAILABLE || created == XR_ERROR_RUNTIME_FAILURE) {
            error = "Ingen fungerande OpenXR-runtime hittades. Installera headsetets PC-programvara, som "
                    "registrerar en OpenXR-runtime, och starta den. " + describe("xrCreateInstance", created);
        } else {
            error = describe("xrCreateInstance", created);
        }
        return false;
    }

#define GLAZE_OPENXR_LOAD_INSTANCE(name)                                                       \
    {                                                                                     \
        PFN_xrVoidFunction proc_ = nullptr;                                               \
        fn_.xrGetInstanceProcAddr(instance_, #name, &proc_);                              \
        fn_.name = reinterpret_cast<PFN_##name>(proc_);                                   \
        if (!fn_.name) {                                                                  \
            error = "OpenXR-runtimen saknar funktionen " #name ".";                       \
            return false;                                                                 \
        }                                                                                 \
    }
    GLAZE_OPENXR_XR_INSTANCE_FUNCTIONS(GLAZE_OPENXR_LOAD_INSTANCE)
#undef GLAZE_OPENXR_LOAD_INSTANCE
    return true;
}

bool OpenXrContext::createSystem(std::string& error) {
    auto systemInfo = xrStruct<XrSystemGetInfo>(XR_TYPE_SYSTEM_GET_INFO);
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    const XrResult got = fn_.xrGetSystem(instance_, &systemInfo, &systemId_);
    if (got == XR_ERROR_FORM_FACTOR_UNAVAILABLE || got == XR_ERROR_FORM_FACTOR_UNSUPPORTED) {
        error = "Inget headset hittades. Anslut headsetet och starta dess "
                "PC-programvara. " + describe("xrGetSystem", got);
        return false;
    }
    GLAZE_OPENXR_XR_CHECK(got, "xrGetSystem");

    // Stereo med ogenomskinlig blandning: det enda läge sessionen ritar för.
    std::uint32_t count = 0;
    GLAZE_OPENXR_XR_CHECK(fn_.xrEnumerateViewConfigurations(instance_, systemId_, 0, &count, nullptr),
                     "xrEnumerateViewConfigurations");
    std::vector<XrViewConfigurationType> configs(count);
    GLAZE_OPENXR_XR_CHECK(fn_.xrEnumerateViewConfigurations(instance_, systemId_, count, &count, configs.data()),
                     "xrEnumerateViewConfigurations");
    if (std::find(configs.begin(), configs.begin() + count, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO) ==
        configs.begin() + count) {
        error = "Headsetet saknar stereovisning (PRIMARY_STEREO).";
        return false;
    }

    count = 0;
    GLAZE_OPENXR_XR_CHECK(fn_.xrEnumerateEnvironmentBlendModes(instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                          0, &count, nullptr),
                     "xrEnumerateEnvironmentBlendModes");
    std::vector<XrEnvironmentBlendMode> modes(count);
    GLAZE_OPENXR_XR_CHECK(fn_.xrEnumerateEnvironmentBlendModes(instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                          count, &count, modes.data()),
                     "xrEnumerateEnvironmentBlendModes");
    if (std::find(modes.begin(), modes.begin() + count, XR_ENVIRONMENT_BLEND_MODE_OPAQUE) == modes.begin() + count) {
        error = "Headsetet saknar ogenomskinlig blandning (OPAQUE).";
        return false;
    }

    // Grafikkravet måste hämtas innan sessionen skapas.
    graphicsRequirements_ = xrStruct<XrGraphicsRequirementsD3D11KHR>(XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR);
    GLAZE_OPENXR_XR_CHECK(fn_.xrGetD3D11GraphicsRequirementsKHR(instance_, systemId_, &graphicsRequirements_),
                     "xrGetD3D11GraphicsRequirementsKHR");
    return true;
}

bool OpenXrContext::createSession(std::string& error) {
    d3d_ = D3D11Upload::create(graphicsRequirements_.adapterLuid, graphicsRequirements_.minFeatureLevel, error);
    if (!d3d_) return false;

    auto binding = xrStruct<XrGraphicsBindingD3D11KHR>(XR_TYPE_GRAPHICS_BINDING_D3D11_KHR);
    binding.device = d3d_->device();
    auto info = xrStruct<XrSessionCreateInfo>(XR_TYPE_SESSION_CREATE_INFO);
    info.next = &binding;
    info.systemId = systemId_;
    GLAZE_OPENXR_XR_CHECK(fn_.xrCreateSession(instance_, &info, &session_), "xrCreateSession");

    const auto makeSpace = [&](XrReferenceSpaceType type, XrSpace& out, const char* name) {
        auto spaceInfo = xrStruct<XrReferenceSpaceCreateInfo>(XR_TYPE_REFERENCE_SPACE_CREATE_INFO);
        spaceInfo.referenceSpaceType = type;
        spaceInfo.poseInReferenceSpace = identityPose();
        const XrResult r = fn_.xrCreateReferenceSpace(session_, &spaceInfo, &out);
        if (XR_FAILED(r)) {
            error = describe(name, r);
            return false;
        }
        return true;
    };
    return makeSpace(XR_REFERENCE_SPACE_TYPE_LOCAL, localSpace_, "xrCreateReferenceSpace(LOCAL)") &&
           makeSpace(XR_REFERENCE_SPACE_TYPE_VIEW, viewSpace_, "xrCreateReferenceSpace(VIEW)");
}

bool OpenXrContext::createSwapchains(double resolutionScale, std::string& error) {
    std::uint32_t count = 0;
    GLAZE_OPENXR_XR_CHECK(fn_.xrEnumerateViewConfigurationViews(instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                           0, &count, nullptr),
                     "xrEnumerateViewConfigurationViews");
    if (count != 2) {
        error = "Headsetet ger " + std::to_string(count) + " vyer i stället för två.";
        return false;
    }
    std::array<XrViewConfigurationView, 2> views;
    for (auto& v : views) v = xrStruct<XrViewConfigurationView>(XR_TYPE_VIEW_CONFIGURATION_VIEW);
    GLAZE_OPENXR_XR_CHECK(fn_.xrEnumerateViewConfigurationViews(instance_, systemId_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                           2, &count, views.data()),
                     "xrEnumerateViewConfigurationViews");

    // Swapchainens format: sRGB, eftersom Canvas pixlar är sRGB-kodade och
    // runtimen då inte gammakodar dem en gång till.
    GLAZE_OPENXR_XR_CHECK(fn_.xrEnumerateSwapchainFormats(session_, 0, &count, nullptr), "xrEnumerateSwapchainFormats");
    std::vector<std::int64_t> formats(count);
    GLAZE_OPENXR_XR_CHECK(fn_.xrEnumerateSwapchainFormats(session_, count, &count, formats.data()), "xrEnumerateSwapchainFormats");
    const auto supported = [&](int format) {
        return std::find(formats.begin(), formats.begin() + count, static_cast<std::int64_t>(format)) !=
               formats.begin() + count;
    };
    std::int64_t format = 0;
    if (supported(kFormatRgbaSrgb)) {
        format = kFormatRgbaSrgb;
        swapRedBlue_ = false;
    } else if (supported(kFormatBgraSrgb)) {
        format = kFormatBgraSrgb;
        swapRedBlue_ = true;
    } else {
        error = "Runtimen erbjuder varken R8G8B8A8_UNORM_SRGB eller B8G8R8A8_UNORM_SRGB som swapchain-format.";
        return false;
    }

    for (std::size_t eye = 0; eye < 2; ++eye) {
        const XrViewConfigurationView& v = views[eye];
        viewSizes_[eye] = ViewSize{v.recommendedImageRectWidth, v.recommendedImageRectHeight,
                                   v.maxImageRectWidth, v.maxImageRectHeight};
        // Bildens mått beror bara på storlekarna och skalan, inte på synfältet;
        // ett vanligt synfält räcker för att få dem.
        const auto size = makeSymmetricView(Fov{-0.8, 0.8, 0.8, -0.8}, viewSizes_[eye], resolutionScale);
        if (!size) {
            error = "Bildmåtten för ögat gick inte att räkna ut (skala " + std::to_string(resolutionScale) + ").";
            return false;
        }

        auto info = xrStruct<XrSwapchainCreateInfo>(XR_TYPE_SWAPCHAIN_CREATE_INFO);
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                          XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
        info.format = format;
        info.sampleCount = 1;
        info.width = size->width;
        info.height = size->height;
        info.faceCount = 1;
        info.arraySize = 1;
        info.mipCount = 1;
        EyeSwapchain& chain = swapchains_[eye];
        GLAZE_OPENXR_XR_CHECK(fn_.xrCreateSwapchain(session_, &info, &chain.handle), "xrCreateSwapchain");
        chain.width = size->width;
        chain.height = size->height;

        std::uint32_t imageCount = 0;
        GLAZE_OPENXR_XR_CHECK(fn_.xrEnumerateSwapchainImages(chain.handle, 0, &imageCount, nullptr),
                         "xrEnumerateSwapchainImages");
        chain.images.assign(imageCount, xrStruct<XrSwapchainImageD3D11KHR>(XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR));
        GLAZE_OPENXR_XR_CHECK(fn_.xrEnumerateSwapchainImages(
                             chain.handle, imageCount, &imageCount,
                             reinterpret_cast<XrSwapchainImageBaseHeader*>(chain.images.data())),
                         "xrEnumerateSwapchainImages");
    }
    return true;
}

// ------------------------------------------------------------- HÄNDELSER OCH SESSION

bool OpenXrContext::pollEvents(Events& out, std::string& error) {
    for (;;) {
        auto buffer = xrStruct<XrEventDataBuffer>(XR_TYPE_EVENT_DATA_BUFFER);
        const XrResult r = fn_.xrPollEvent(instance_, &buffer);
        if (r == XR_EVENT_UNAVAILABLE) return true;
        GLAZE_OPENXR_XR_CHECK(r, "xrPollEvent");
        switch (buffer.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            const auto* changed = reinterpret_cast<const XrEventDataSessionStateChanged*>(&buffer);
            out.phases.push_back(toPhase(changed->state));
            break;
        }
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
            out.referenceSpaceChanged = true;
            break;
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            out.instanceLost = true;
            break;
        default:
            break;
        }
    }
}

bool OpenXrContext::beginSession(std::string& error) {
    auto info = xrStruct<XrSessionBeginInfo>(XR_TYPE_SESSION_BEGIN_INFO);
    info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    GLAZE_OPENXR_XR_CHECK(fn_.xrBeginSession(session_, &info), "xrBeginSession");
    return true;
}

bool OpenXrContext::endSession(std::string& error) {
    GLAZE_OPENXR_XR_CHECK(fn_.xrEndSession(session_), "xrEndSession");
    return true;
}

bool OpenXrContext::requestExitSession(std::string& error) {
    GLAZE_OPENXR_XR_CHECK(fn_.xrRequestExitSession(session_), "xrRequestExitSession");
    return true;
}

// ------------------------------------------------------------------- BILDRUTAN

bool OpenXrContext::waitFrame(FrameTiming& out, std::string& error) {
    const auto waitInfo = xrStruct<XrFrameWaitInfo>(XR_TYPE_FRAME_WAIT_INFO);
    auto state = xrStruct<XrFrameState>(XR_TYPE_FRAME_STATE);
    GLAZE_OPENXR_XR_CHECK(fn_.xrWaitFrame(session_, &waitInfo, &state), "xrWaitFrame");
    out.predictedDisplayTime = state.predictedDisplayTime;
    out.predictedDisplayPeriod = state.predictedDisplayPeriod;
    out.shouldRender = state.shouldRender == XR_TRUE;
    return true;
}

bool OpenXrContext::beginFrame(std::string& error) {
    const auto beginInfo = xrStruct<XrFrameBeginInfo>(XR_TYPE_FRAME_BEGIN_INFO);
    GLAZE_OPENXR_XR_CHECK(fn_.xrBeginFrame(session_, &beginInfo), "xrBeginFrame");
    return true;
}

bool OpenXrContext::locateViews(XrTime time, EyePoses& out, std::string& error) {
    auto locateInfo = xrStruct<XrViewLocateInfo>(XR_TYPE_VIEW_LOCATE_INFO);
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = time;
    locateInfo.space = localSpace_;
    auto viewState = xrStruct<XrViewState>(XR_TYPE_VIEW_STATE);
    std::array<XrView, 2> views;
    for (auto& v : views) v = xrStruct<XrView>(XR_TYPE_VIEW);
    std::uint32_t count = 0;
    GLAZE_OPENXR_XR_CHECK(fn_.xrLocateViews(session_, &locateInfo, &viewState, 2, &count, views.data()), "xrLocateViews");

    const XrViewStateFlags needed = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    out.valid = count == 2 && (viewState.viewStateFlags & needed) == needed;
    for (std::size_t eye = 0; eye < 2 && eye < count; ++eye) {
        out.poses[eye] = fromXr(views[eye].pose);
        out.fov[eye] = Fov{views[eye].fov.angleLeft, views[eye].fov.angleRight,
                           views[eye].fov.angleUp, views[eye].fov.angleDown};
    }
    return true;
}

bool OpenXrContext::locateHead(XrTime time, PoseSample& out, std::string& error) {
    auto location = xrStruct<XrSpaceLocation>(XR_TYPE_SPACE_LOCATION);
    GLAZE_OPENXR_XR_CHECK(fn_.xrLocateSpace(viewSpace_, localSpace_, time, &location), "xrLocateSpace");
    const XrSpaceLocationFlags needed = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    out.valid = (location.locationFlags & needed) == needed;
    out.positionTracked = (location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
    out.orientationTracked = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
    out.pose = fromXr(location.pose);
    return true;
}

bool OpenXrContext::endFrameEmpty(XrTime time, std::string& error) {
    auto endInfo = xrStruct<XrFrameEndInfo>(XR_TYPE_FRAME_END_INFO);
    endInfo.displayTime = time;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = 0;
    endInfo.layers = nullptr;
    GLAZE_OPENXR_XR_CHECK(fn_.xrEndFrame(session_, &endInfo), "xrEndFrame");
    return true;
}

bool OpenXrContext::uploadEye(std::size_t eye, const EyeSubmission& submission, std::string& error) {
    EyeSwapchain& chain = swapchains_[eye];
    if (submission.width != static_cast<int>(chain.width) || submission.height != static_cast<int>(chain.height)) {
        error = "Ögats bild har fel mått för swapchain (" + std::to_string(submission.width) + "x" +
                std::to_string(submission.height) + " mot " + std::to_string(chain.width) + "x" +
                std::to_string(chain.height) + ").";
        return false;
    }

    std::uint32_t index = 0;
    const auto acquireInfo = xrStruct<XrSwapchainImageAcquireInfo>(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);
    GLAZE_OPENXR_XR_CHECK(fn_.xrAcquireSwapchainImage(chain.handle, &acquireInfo, &index), "xrAcquireSwapchainImage");

    // Väntar högst en sekund på att runtimen släpper bilden.
    auto waitInfo = xrStruct<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);
    waitInfo.timeout = 200'000'000;
    XrResult waited = XR_TIMEOUT_EXPIRED;
    for (int attempt = 0; attempt < 5 && waited == XR_TIMEOUT_EXPIRED; ++attempt) {
        waited = fn_.xrWaitSwapchainImage(chain.handle, &waitInfo);
    }
    if (XR_FAILED(waited) || waited == XR_TIMEOUT_EXPIRED) {
        error = waited == XR_TIMEOUT_EXPIRED ? "Runtimen släppte aldrig swapchain-bilden (tidsgräns)."
                                             : describe("xrWaitSwapchainImage", waited);
        return false;
    }

    const bool uploaded = index < chain.images.size() &&
                          d3d_->upload(chain.images[index].texture, submission.rgba, submission.width,
                                       submission.height, swapRedBlue_, error);
    if (index >= chain.images.size()) error = "Runtimen gav ett swapchain-index utanför bildlistan.";

    const auto releaseInfo = xrStruct<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);
    const XrResult released = fn_.xrReleaseSwapchainImage(chain.handle, &releaseInfo);
    if (!uploaded) return false;
    GLAZE_OPENXR_XR_CHECK(released, "xrReleaseSwapchainImage");
    return true;
}

bool OpenXrContext::endFrameProjection(XrTime time, const std::array<EyeSubmission, 2>& eyes, std::string& error) {
    for (std::size_t eye = 0; eye < 2; ++eye) {
        if (!uploadEye(eye, eyes[eye], error)) {
            std::string ignored;
            endFrameEmpty(time, ignored);   // bildrutan som börjats ska alltid avslutas
            return false;
        }
    }

    std::array<XrCompositionLayerProjectionView, 2> views;
    for (std::size_t eye = 0; eye < 2; ++eye) {
        auto view = xrStruct<XrCompositionLayerProjectionView>(XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW);
        view.pose = toXr(eyes[eye].pose);
        view.fov = XrFovf{static_cast<float>(eyes[eye].fov.left), static_cast<float>(eyes[eye].fov.right),
                          static_cast<float>(eyes[eye].fov.up), static_cast<float>(eyes[eye].fov.down)};
        view.subImage.swapchain = swapchains_[eye].handle;
        view.subImage.imageRect.offset = XrOffset2Di{0, 0};
        view.subImage.imageRect.extent = XrExtent2Di{eyes[eye].width, eyes[eye].height};
        view.subImage.imageArrayIndex = 0;
        views[eye] = view;
    }
    auto layer = xrStruct<XrCompositionLayerProjection>(XR_TYPE_COMPOSITION_LAYER_PROJECTION);
    layer.layerFlags = 0;
    layer.space = localSpace_;
    layer.viewCount = 2;
    layer.views = views.data();

    const XrCompositionLayerBaseHeader* layers[] = {reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
    auto endInfo = xrStruct<XrFrameEndInfo>(XR_TYPE_FRAME_END_INFO);
    endInfo.displayTime = time;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = 1;
    endInfo.layers = layers;
    GLAZE_OPENXR_XR_CHECK(fn_.xrEndFrame(session_, &endInfo), "xrEndFrame");
    return true;
}

} // namespace glaze::openxr::detail
