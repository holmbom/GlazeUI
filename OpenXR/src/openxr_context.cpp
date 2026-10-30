#include "openxr_context.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <exception>

namespace glaze::openxr::detail {

namespace {

// Kör ett OpenXR-anrop; vid fel läggs texten i `error` och funktionen
// returnerar false. Kräver en medlem `describe` och en variabel `error`.
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

} // namespace

std::string describeResult(const XrFunctions& fn, XrInstance instance, const char* call, XrResult result) {
    char name[XR_MAX_RESULT_STRING_SIZE] = {};
    if (instance != XR_NULL_HANDLE && fn.xrResultToString &&
        XR_SUCCEEDED(fn.xrResultToString(instance, result, name))) {
        return std::string(call) + " misslyckades: " + name + " (" + std::to_string(static_cast<int>(result)) + ")";
    }
    return std::string(call) + " misslyckades med OpenXR-koden " + std::to_string(static_cast<int>(result));
}

// ============================================================== XrSessionApi

XrSessionApi::XrSessionApi(const XrFunctions& fn, const Handles& handles, const Limits& limits)
    : fn_(fn), handles_(handles), limits_(limits) {}

bool XrSessionApi::pollEvents(Events& out, std::string& error) {
    for (;;) {
        auto buffer = xrStruct<XrEventDataBuffer>(XR_TYPE_EVENT_DATA_BUFFER);
        const XrResult r = fn_.xrPollEvent(handles_.instance, &buffer);
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
        case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
            out.interactionProfileChanged = true;
            break;
        default:
            break;
        }
    }
}

bool XrSessionApi::beginSession(std::string& error) {
    auto info = xrStruct<XrSessionBeginInfo>(XR_TYPE_SESSION_BEGIN_INFO);
    info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    GLAZE_OPENXR_XR_CHECK(fn_.xrBeginSession(handles_.session, &info), "xrBeginSession");
    return true;
}

bool XrSessionApi::endSession(std::string& error) {
    GLAZE_OPENXR_XR_CHECK(fn_.xrEndSession(handles_.session), "xrEndSession");
    return true;
}

bool XrSessionApi::requestExitSession(std::string& error) {
    GLAZE_OPENXR_XR_CHECK(fn_.xrRequestExitSession(handles_.session), "xrRequestExitSession");
    return true;
}

bool XrSessionApi::waitFrame(FrameTiming& out, std::string& error) {
    const auto waitInfo = xrStruct<XrFrameWaitInfo>(XR_TYPE_FRAME_WAIT_INFO);
    auto state = xrStruct<XrFrameState>(XR_TYPE_FRAME_STATE);
    GLAZE_OPENXR_XR_CHECK(fn_.xrWaitFrame(handles_.session, &waitInfo, &state), "xrWaitFrame");
    out.predictedDisplayTime = state.predictedDisplayTime;
    out.predictedDisplayPeriod = state.predictedDisplayPeriod;
    out.shouldRender = state.shouldRender == XR_TRUE;
    return true;
}

AcquireOutcome XrSessionApi::acquireImages(std::array<std::uint32_t, 2>& index, std::string& error) {
    for (std::size_t eye = 0; eye < 2; ++eye) {
        if (state_[eye] == ImageState::Free) {
            const auto acquireInfo = xrStruct<XrSwapchainImageAcquireInfo>(XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO);
            std::uint32_t acquired = 0;
            const XrResult r = fn_.xrAcquireSwapchainImage(handles_.swapchains[eye], &acquireInfo, &acquired);
            if (XR_FAILED(r)) {
                error = describe("xrAcquireSwapchainImage", r);
                return AcquireOutcome::Failed;
            }
            if (acquired >= handles_.imageCounts[eye]) {
                error = "Runtimen gav ett swapchain-index utanför bildlistan.";
                return AcquireOutcome::Failed;
            }
            index_[eye] = acquired;
            state_[eye] = ImageState::Acquired;
        }
        if (state_[eye] == ImageState::Acquired) {
            auto waitInfo = xrStruct<XrSwapchainImageWaitInfo>(XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO);
            waitInfo.timeout = limits_.imageWaitTimeout;
            const XrResult r = fn_.xrWaitSwapchainImage(handles_.swapchains[eye], &waitInfo);
            if (r == XR_TIMEOUT_EXPIRED) {
                // Bilden förblir förvärvad och väntas om i nästa bildruta.
                if (++notReadyFrames_ > limits_.maxNotReadyFrames) {
                    error = "Runtimen släppte aldrig swapchain-bilden (tidsgräns).";
                    return AcquireOutcome::Failed;
                }
                return AcquireOutcome::NotReady;
            }
            if (XR_FAILED(r)) {
                error = describe("xrWaitSwapchainImage", r);
                return AcquireOutcome::Failed;
            }
            state_[eye] = ImageState::Waited;
        }
    }
    notReadyFrames_ = 0;
    index = index_;
    return AcquireOutcome::Ready;
}

bool XrSessionApi::beginFrame(std::string& error) {
    const auto beginInfo = xrStruct<XrFrameBeginInfo>(XR_TYPE_FRAME_BEGIN_INFO);
    GLAZE_OPENXR_XR_CHECK(fn_.xrBeginFrame(handles_.session, &beginInfo), "xrBeginFrame");
    return true;
}

bool XrSessionApi::locateViews(XrTime time, LocatedViews& out, std::string& error) {
    auto locateInfo = xrStruct<XrViewLocateInfo>(XR_TYPE_VIEW_LOCATE_INFO);
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = time;
    locateInfo.space = handles_.trackingSpace;
    auto viewState = xrStruct<XrViewState>(XR_TYPE_VIEW_STATE);
    std::array<XrView, 2> views;
    for (auto& v : views) v = xrStruct<XrView>(XR_TYPE_VIEW);
    std::uint32_t count = 0;
    GLAZE_OPENXR_XR_CHECK(fn_.xrLocateViews(handles_.session, &locateInfo, &viewState, 2, &count, views.data()),
                          "xrLocateViews");

    const XrViewStateFlags needed = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    out.valid = count == 2 && (viewState.viewStateFlags & needed) == needed;
    for (std::size_t eye = 0; eye < 2 && eye < count; ++eye) {
        out.poses[eye] = fromXr(views[eye].pose);
        out.fov[eye] = Fov{views[eye].fov.angleLeft, views[eye].fov.angleRight,
                           views[eye].fov.angleUp, views[eye].fov.angleDown};
    }
    return true;
}

bool XrSessionApi::locateHead(XrTime time, PoseSample& out, std::string& error) {
    auto location = xrStruct<XrSpaceLocation>(XR_TYPE_SPACE_LOCATION);
    GLAZE_OPENXR_XR_CHECK(fn_.xrLocateSpace(handles_.viewSpace, handles_.trackingSpace, time, &location),
                          "xrLocateSpace");
    const XrSpaceLocationFlags needed = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    out.valid = (location.locationFlags & needed) == needed;
    out.positionTracked = (location.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
    out.orientationTracked = (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT) != 0;
    out.pose = fromXr(location.pose);
    return true;
}

bool XrSessionApi::releaseImages(std::string& error) {
    std::string first;
    for (std::size_t eye = 0; eye < 2; ++eye) {
        if (state_[eye] != ImageState::Waited) continue;
        state_[eye] = ImageState::Free;   // släppt eller ej: bilden är inte längre vår
        const auto releaseInfo = xrStruct<XrSwapchainImageReleaseInfo>(XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO);
        const XrResult r = fn_.xrReleaseSwapchainImage(handles_.swapchains[eye], &releaseInfo);
        if (XR_FAILED(r) && first.empty()) first = describe("xrReleaseSwapchainImage", r);
    }
    if (!first.empty()) {
        error = first;
        return false;
    }
    return true;
}

bool XrSessionApi::endFrameEmpty(XrTime time, std::string& error) {
    auto endInfo = xrStruct<XrFrameEndInfo>(XR_TYPE_FRAME_END_INFO);
    endInfo.displayTime = time;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = 0;
    endInfo.layers = nullptr;
    GLAZE_OPENXR_XR_CHECK(fn_.xrEndFrame(handles_.session, &endInfo), "xrEndFrame");
    return true;
}

bool XrSessionApi::endFrameProjection(XrTime time, const std::array<EyeLayer, 2>& eyes, std::string& error) {
    std::array<XrCompositionLayerProjectionView, 2> views;
    for (std::size_t eye = 0; eye < 2; ++eye) {
        auto view = xrStruct<XrCompositionLayerProjectionView>(XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW);
        view.pose = toXr(eyes[eye].pose);
        view.fov = XrFovf{static_cast<float>(eyes[eye].fov.left), static_cast<float>(eyes[eye].fov.right),
                          static_cast<float>(eyes[eye].fov.up), static_cast<float>(eyes[eye].fov.down)};
        view.subImage.swapchain = handles_.swapchains[eye];
        view.subImage.imageRect.offset = XrOffset2Di{0, 0};
        view.subImage.imageRect.extent = XrExtent2Di{handles_.widths[eye], handles_.heights[eye]};
        view.subImage.imageArrayIndex = 0;
        views[eye] = view;
    }
    auto layer = xrStruct<XrCompositionLayerProjection>(XR_TYPE_COMPOSITION_LAYER_PROJECTION);
    layer.layerFlags = 0;
    layer.space = handles_.trackingSpace;
    layer.viewCount = 2;
    layer.views = views.data();

    const XrCompositionLayerBaseHeader* layers[] = {reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)};
    auto endInfo = xrStruct<XrFrameEndInfo>(XR_TYPE_FRAME_END_INFO);
    endInfo.displayTime = time;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = 1;
    endInfo.layers = layers;
    GLAZE_OPENXR_XR_CHECK(fn_.xrEndFrame(handles_.session, &endInfo), "xrEndFrame");
    return true;
}

int XrSessionApi::waitedImages() const noexcept {
    return static_cast<int>(std::count(state_.begin(), state_.end(), ImageState::Waited));
}

int XrSessionApi::acquiredImages() const noexcept {
    return static_cast<int>(std::count(state_.begin(), state_.end(), ImageState::Acquired));
}

// ================================================================ BILDRUTAN

SubmitResult completeFrame(XrSessionApi& api, const FrameTicket& ticket, FrameContent& content,
                           std::string& error) {
    std::string ignored;
    if (!api.beginFrame(error)) {
        api.releaseImages(ignored);   // bildrutan började aldrig; bilderna ska ändå släppas
        return SubmitResult::Failed;
    }
    // Från och med nu måste bildrutan avslutas, hur det än går.
    std::array<EyeLayer, 2> layers;
    FillResult filled = FillResult::Failed;
    try {
        filled = content.fill(ticket, layers, error);
    } catch (const std::exception& e) {
        error = std::string("Bildrutan kastade ett undantag: ") + e.what();
        filled = FillResult::Failed;
    } catch (...) {
        error = "Bildrutan kastade ett undantag.";
        filled = FillResult::Failed;
    }
    if (filled == FillResult::Failed && error.empty()) error = "Bildrutan kunde inte fyllas.";

    std::string releaseError;
    const bool released = api.releaseImages(releaseError);
    if (filled == FillResult::Failed) {
        api.endFrameEmpty(ticket.predictedDisplayTime, ignored);
        return SubmitResult::Failed;
    }
    if (!released) {
        error = releaseError;
        api.endFrameEmpty(ticket.predictedDisplayTime, ignored);
        return SubmitResult::Failed;
    }
    const bool ended = filled == FillResult::Layer
        ? api.endFrameProjection(ticket.predictedDisplayTime, layers, error)
        : api.endFrameEmpty(ticket.predictedDisplayTime, error);
    return ended ? SubmitResult::Done : SubmitResult::Failed;
}

// ================================================================ SessionLoop

SessionLoop::SessionLoop(XrSessionApi& api, FrameSink& sink, LoopHooks hooks)
    : api_(api), sink_(sink), hooks_(std::move(hooks)) {}

Status SessionLoop::run() {
    std::string error;
    std::chrono::steady_clock::time_point exitDeadline{};
    bool exitSent = false;
    std::uint64_t releasedEpoch = 0;

    const auto publish = [&] {
        if (hooks_.onStatus) hooks_.onStatus(life_.status());
    };
    publish();

    while (!life_.terminate()) {
        if (!life_.stopRequested() && hooks_.stopRequested()) life_.requestStop();

        Events events;
        if (!api_.pollEvents(events, error)) {
            life_.fail(error);
            break;
        }
        for (const Phase phase : events.phases) {
            const Step step = life_.onPhase(phase);
            if (step == Step::BeginSession) {
                if (!api_.beginSession(error)) { life_.fail(error); break; }
                life_.sessionBegun();
            } else if (step == Step::EndSession) {
                if (!api_.endSession(error)) { life_.fail(error); break; }
                life_.sessionEnded();
            }
        }
        if (events.referenceSpaceChanged) {
            life_.requestStop("Referensrummet ändrades (till exempel efter en ny rumsinställning). "
                              "Starta VR igen.");
        }
        if (events.instanceLost) {
            life_.fail("OpenXR-runtimen stängdes eller förlorades. Starta om runtimen och VR.");
        }
        if (life_.terminate()) break;
        if (events.interactionProfileChanged && hooks_.onInteractionProfileChanged) {
            hooks_.onInteractionProfileChanged();
        }
        // Fokus försvann: knapparnas tillstånd gäller inte längre, också
        // om ingen bildruta ritats sedan dess.
        if (life_.focusEpoch() != releasedEpoch) {
            if (hooks_.onFocusLost) hooks_.onFocusLost();
            releasedEpoch = life_.focusEpoch();
        }

        if (life_.takeExitRequest()) {
            if (!api_.requestExitSession(error)) { life_.fail(error); break; }
            exitSent = true;
            exitDeadline = std::chrono::steady_clock::now() + hooks_.exitTimeout;
        }
        if (exitSent && life_.running() && std::chrono::steady_clock::now() > exitDeadline) {
            life_.giveUp();   // runtimen avslutar inte; riv ned sessionen ändå
            break;
        }
        publish();

        if (!life_.running()) {
            hooks_.sleep(hooks_.idlePoll);
            continue;
        }
        if (!frame()) break;
    }
    return life_.finalStatus();
}

bool SessionLoop::emptyFrame(XrTime time) {
    std::string error;
    if (!api_.beginFrame(error) || !api_.endFrameEmpty(time, error)) {
        life_.fail(error);
        return false;
    }
    return true;
}

bool SessionLoop::frame() {
    std::string error;
    FrameTiming timing;
    if (!api_.waitFrame(timing, error)) {
        life_.fail(error);
        return false;
    }
    const double delta = previousTime_ == 0
        ? 0.0
        : std::clamp(static_cast<double>(timing.predictedDisplayTime - previousTime_) * 1e-9,
                     0.0, hooks_.maxDeltaSeconds);
    previousTime_ = timing.predictedDisplayTime;

    // Ingen bild och inget uppdrag till UI-tråden: bildrutan begränsas till
    // begin och end utan lager.
    if (!timing.shouldRender || !life_.rendering() || hooks_.stopRequested()) {
        return emptyFrame(timing.predictedDisplayTime);
    }

    FrameTicket ticket;
    switch (api_.acquireImages(ticket.imageIndex, error)) {
    case AcquireOutcome::Failed:
        life_.fail(error);
        return false;
    case AcquireOutcome::NotReady:
        return emptyFrame(timing.predictedDisplayTime);
    case AcquireOutcome::Ready:
        break;
    }
    ticket.sequence = ++sequence_;
    ticket.deltaSeconds = delta;
    ticket.focused = life_.focused();
    ticket.focusEpoch = life_.focusEpoch();
    ticket.predictedDisplayTime = timing.predictedDisplayTime;

    switch (sink_.submit(ticket, error)) {
    case SubmitResult::Done:
        return true;
    case SubmitResult::Failed: {
        std::string released;
        api_.releaseImages(released);   // no-op när UI-tråden redan släppt dem
        life_.fail(error);
        return false;
    }
    case SubmitResult::Cancelled: {
        // UI-tråden började aldrig: bilderna är våra att släppa.
        std::string released;
        const bool ok = api_.beginFrame(error);
        api_.releaseImages(released);
        if (!ok || !api_.endFrameEmpty(timing.predictedDisplayTime, error)) {
            life_.fail(error);
            return false;
        }
        return true;
    }
    }
    return true;
}

// ================================================================= XrRuntime

std::unique_ptr<XrRuntime> XrRuntime::open(const Options& options, std::string& error) {
    std::unique_ptr<XrRuntime> self(new XrRuntime());
    if (!self->loadLoader(error) ||
        !self->createInstance(options, error) ||
        !self->createSystem(options, error)) {
        return nullptr;   // destruktorn avvecklar det som hunnit skapas
    }
    return self;
}

XrRuntime::~XrRuntime() {
    if (instance_ != XR_NULL_HANDLE && fn_.xrDestroyInstance) fn_.xrDestroyInstance(instance_);
    instance_ = XR_NULL_HANDLE;
}

bool XrRuntime::loadLoader(std::string& error) {
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

bool XrRuntime::createInstance(const Options& options, std::string& error) {
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
    extensions_.bdController = has(XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME);
    extensions_.palmPose     = has(XR_EXT_PALM_POSE_EXTENSION_NAME);
    extensions_.refreshRate  = has(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    extensions_.localFloor   = has(XR_EXT_LOCAL_FLOOR_EXTENSION_NAME);
    // Handspårningen aktiveras bara när appen begär den; `handTracking`
    // sätts sedan om systemet har den.
    const bool handExtension = options.handTracking && has(XR_EXT_HAND_TRACKING_EXTENSION_NAME);

    std::vector<const char*> enabled{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    if (extensions_.bdController) enabled.push_back(XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME);
    if (extensions_.palmPose)     enabled.push_back(XR_EXT_PALM_POSE_EXTENSION_NAME);
    if (extensions_.refreshRate)  enabled.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    if (extensions_.localFloor)   enabled.push_back(XR_EXT_LOCAL_FLOOR_EXTENSION_NAME);
    if (handExtension)            enabled.push_back(XR_EXT_HAND_TRACKING_EXTENSION_NAME);

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

    // Tilläggens funktioner. En som runtimen lovat men inte ger gör att
    // tillägget räknas som saknat.
#define GLAZE_OPENXR_LOAD_OPTIONAL(name)                                                       \
    {                                                                                     \
        PFN_xrVoidFunction proc_ = nullptr;                                               \
        fn_.xrGetInstanceProcAddr(instance_, #name, &proc_);                              \
        fn_.name = reinterpret_cast<PFN_##name>(proc_);                                   \
    }
    GLAZE_OPENXR_XR_OPTIONAL_FUNCTIONS(GLAZE_OPENXR_LOAD_OPTIONAL)
#undef GLAZE_OPENXR_LOAD_OPTIONAL
    if (!fn_.xrEnumerateDisplayRefreshRatesFB || !fn_.xrGetDisplayRefreshRateFB ||
        !fn_.xrRequestDisplayRefreshRateFB) {
        extensions_.refreshRate = false;
    }
    if (handExtension && fn_.xrCreateHandTrackerEXT && fn_.xrDestroyHandTrackerEXT && fn_.xrLocateHandJointsEXT) {
        extensions_.handTracking = true;   // bekräftas av systemet i `createSystem`
    }
    return true;
}

bool XrRuntime::createSystem(const Options&, std::string& error) {
    auto systemInfo = xrStruct<XrSystemGetInfo>(XR_TYPE_SYSTEM_GET_INFO);
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    const XrResult got = fn_.xrGetSystem(instance_, &systemInfo, &systemId_);
    if (got == XR_ERROR_FORM_FACTOR_UNAVAILABLE || got == XR_ERROR_FORM_FACTOR_UNSUPPORTED) {
        error = "Inget headset hittades. Anslut headsetet och starta dess "
                "PC-programvara. " + describe("xrGetSystem", got);
        return false;
    }
    GLAZE_OPENXR_XR_CHECK(got, "xrGetSystem");

    if (extensions_.handTracking) {
        auto hands = xrStruct<XrSystemHandTrackingPropertiesEXT>(XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT);
        auto properties = xrStruct<XrSystemProperties>(XR_TYPE_SYSTEM_PROPERTIES);
        properties.next = &hands;
        if (XR_FAILED(fn_.xrGetSystemProperties(instance_, systemId_, &properties)) ||
            hands.supportsHandTracking != XR_TRUE) {
            extensions_.handTracking = false;
        }
    }

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

// ============================================================== OpenXrContext

std::unique_ptr<OpenXrContext> OpenXrContext::create(std::unique_ptr<XrRuntime> runtime, void* device,
                                                     const Options& options, std::string& error) {
    if (!runtime || !device) {
        error = "Sessionen saknar OpenXR-runtime eller grafikenhet.";
        return nullptr;
    }
    std::unique_ptr<OpenXrContext> self(new OpenXrContext());
    self->runtime_ = std::move(runtime);
    if (!self->createSession(device, options, error) || !self->createSwapchains(error)) {
        return nullptr;   // destruktorn avvecklar det som hunnit skapas
    }
    return self;
}

OpenXrContext::~OpenXrContext() { destroy(); }

void OpenXrContext::destroy() noexcept {
    if (!runtime_) return;
    const XrFunctions& fn = runtime_->fn();
    api_.reset();
    for (XrHandTrackerEXT& tracker : handTrackers_) {
        if (tracker != XR_NULL_HANDLE && fn.xrDestroyHandTrackerEXT) fn.xrDestroyHandTrackerEXT(tracker);
        tracker = XR_NULL_HANDLE;
    }
    for (XrSwapchain& chain : swapchains_) {
        if (chain != XR_NULL_HANDLE && fn.xrDestroySwapchain) fn.xrDestroySwapchain(chain);
        chain = XR_NULL_HANDLE;
    }
    if (viewSpace_ != XR_NULL_HANDLE && fn.xrDestroySpace) fn.xrDestroySpace(viewSpace_);
    if (trackingSpace_ != XR_NULL_HANDLE && fn.xrDestroySpace) fn.xrDestroySpace(trackingSpace_);
    viewSpace_ = XR_NULL_HANDLE;
    trackingSpace_ = XR_NULL_HANDLE;
    if (session_ != XR_NULL_HANDLE && fn.xrDestroySession) fn.xrDestroySession(session_);
    session_ = XR_NULL_HANDLE;
    runtime_.reset();   // river instansen sist
}

bool OpenXrContext::createSession(void* device, const Options& options, std::string& error) {
    const XrFunctions& fn = runtime_->fn();
    const auto describe = [&](const char* call, XrResult r) { return runtime_->describe(call, r); };

    auto binding = xrStruct<XrGraphicsBindingD3D11KHR>(XR_TYPE_GRAPHICS_BINDING_D3D11_KHR);
    binding.device = static_cast<ID3D11Device*>(device);
    auto info = xrStruct<XrSessionCreateInfo>(XR_TYPE_SESSION_CREATE_INFO);
    info.next = &binding;
    info.systemId = runtime_->systemId();
    GLAZE_OPENXR_XR_CHECK(fn.xrCreateSession(runtime_->instance(), &info, &session_), "xrCreateSession");

    const auto makeSpace = [&](XrReferenceSpaceType type, XrSpace& out) {
        auto spaceInfo = xrStruct<XrReferenceSpaceCreateInfo>(XR_TYPE_REFERENCE_SPACE_CREATE_INFO);
        spaceInfo.referenceSpaceType = type;
        spaceInfo.poseInReferenceSpace = identityPose();
        return fn.xrCreateReferenceSpace(session_, &spaceInfo, &out);
    };

    // Referensrummet: det begärda, annars nästa i reservkedjan. LocalFloor
    // kräver tillägget; saknas det tas Stage, och sist alltid Local.
    struct Candidate {
        ReferenceSpace       kind;
        XrReferenceSpaceType type;
    };
    std::vector<Candidate> chain;
    if (options.referenceSpace == ReferenceSpace::LocalFloor && runtime_->extensions().localFloor) {
        chain.push_back({ReferenceSpace::LocalFloor, XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR_EXT});
    }
    if (options.referenceSpace != ReferenceSpace::Local) {
        chain.push_back({ReferenceSpace::Stage, XR_REFERENCE_SPACE_TYPE_STAGE});
    }
    chain.push_back({ReferenceSpace::Local, XR_REFERENCE_SPACE_TYPE_LOCAL});
    XrResult last = XR_ERROR_REFERENCE_SPACE_UNSUPPORTED;
    for (const Candidate& candidate : chain) {
        last = makeSpace(candidate.type, trackingSpace_);
        if (XR_SUCCEEDED(last)) {
            referenceSpace_ = candidate.kind;
            break;
        }
        trackingSpace_ = XR_NULL_HANDLE;
    }
    if (trackingSpace_ == XR_NULL_HANDLE) {
        error = describe("xrCreateReferenceSpace", last);
        return false;
    }
    if (const XrResult r = makeSpace(XR_REFERENCE_SPACE_TYPE_VIEW, viewSpace_); XR_FAILED(r)) {
        viewSpace_ = XR_NULL_HANDLE;
        error = describe("xrCreateReferenceSpace(VIEW)", r);
        return false;
    }

    // Handspårarna, när appen begärt dem och systemet har dem. En som inte går
    // att skapa gör bara att den handens leder saknas.
    if (runtime_->extensions().handTracking) {
        for (std::size_t hand = 0; hand < 2; ++hand) {
            auto trackerInfo = xrStruct<XrHandTrackerCreateInfoEXT>(XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT);
            trackerInfo.hand = hand == 0 ? XR_HAND_LEFT_EXT : XR_HAND_RIGHT_EXT;
            trackerInfo.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
            if (XR_FAILED(fn.xrCreateHandTrackerEXT(session_, &trackerInfo, &handTrackers_[hand]))) {
                handTrackers_[hand] = XR_NULL_HANDLE;
            }
        }
    }
    return true;
}

double OpenXrContext::floorHeight() const noexcept {
    // I Local står origo i huvudhöjd där headsetet startade; golvet antas en
    // normal ögonhöjd under det.
    constexpr double kAssumedEyeHeight = 1.6;
    return referenceSpace_ == ReferenceSpace::Local ? -kAssumedEyeHeight : 0.0;
}

void OpenXrContext::locateHands(XrTime time, std::array<HandSample, 2>& out) const {
    const XrFunctions& fn = runtime_->fn();
    for (std::size_t hand = 0; hand < 2; ++hand) {
        HandSample& sample = out[hand];
        sample = HandSample{};
        if (handTrackers_[hand] == XR_NULL_HANDLE) continue;

        std::array<XrHandJointLocationEXT, XR_HAND_JOINT_COUNT_EXT> joints{};
        auto locations = xrStruct<XrHandJointLocationsEXT>(XR_TYPE_HAND_JOINT_LOCATIONS_EXT);
        locations.jointCount = XR_HAND_JOINT_COUNT_EXT;
        locations.jointLocations = joints.data();
        auto locateInfo = xrStruct<XrHandJointsLocateInfoEXT>(XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT);
        locateInfo.baseSpace = trackingSpace_;
        locateInfo.time = time;
        if (XR_FAILED(fn.xrLocateHandJointsEXT(handTrackers_[hand], &locateInfo, &locations)) ||
            locations.isActive != XR_TRUE) {
            continue;
        }
        sample.active = true;
        const XrSpaceLocationFlags needed =
            XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        for (std::size_t joint = 0; joint < sample.joints.size(); ++joint) {
            JointSample& target = sample.joints[joint];
            target.valid = (joints[joint].locationFlags & needed) == needed;
            target.pose = fromXr(joints[joint].pose);
            target.radius = joints[joint].radius;
        }
        computeHandMetrics(sample);
    }
}

std::vector<float> OpenXrContext::refreshRates() const {
    const XrFunctions& fn = runtime_->fn();
    if (!runtime_->extensions().refreshRate || session_ == XR_NULL_HANDLE) return {};
    std::uint32_t count = 0;
    if (XR_FAILED(fn.xrEnumerateDisplayRefreshRatesFB(session_, 0, &count, nullptr)) || count == 0) return {};
    std::vector<float> rates(count);
    if (XR_FAILED(fn.xrEnumerateDisplayRefreshRatesFB(session_, count, &count, rates.data()))) return {};
    rates.resize(std::min<std::size_t>(count, rates.size()));
    return rates;
}

bool OpenXrContext::requestRefreshRate(float hz) {
    if (!runtime_->extensions().refreshRate || session_ == XR_NULL_HANDLE) return false;
    return XR_SUCCEEDED(runtime_->fn().xrRequestDisplayRefreshRateFB(session_, hz));
}

// ================================================================ HANDMÅTT

namespace {

double distance(const TrackingPose& a, const TrackingPose& b) {
    return std::hypot(a.position.x - b.position.x, a.position.y - b.position.y, a.position.z - b.position.z);
}

double saturate(double v) { return std::clamp(v, 0.0, 1.0); }

} // namespace

void computeHandMetrics(HandSample& hand) noexcept {
    hand.pinch = 0.0;
    hand.grab = 0.0;
    const auto joint = [&](int index) -> const JointSample& { return hand.joints[static_cast<std::size_t>(index)]; };
    const auto ok = [&](int index) { return joint(index).valid; };

    // Handens storlek: handleden till mellanfingrets grundled, ungefär 9,5 cm
    // på en vuxen. Måtten nedan gäller den storleken och följer med den.
    constexpr double kReferenceSize = 0.095;
    double size = 1.0;
    if (ok(XR_HAND_JOINT_WRIST_EXT) && ok(XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT)) {
        const double measured = distance(joint(XR_HAND_JOINT_WRIST_EXT).pose, joint(XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT).pose);
        if (measured > 0.02) size = measured / kReferenceSize;
    }

    // Nyp: avståndet mellan tummens och pekfingrets toppar. Under 2 cm är
    // topparna ihop, över 10 cm är handen öppen.
    if (ok(XR_HAND_JOINT_THUMB_TIP_EXT) && ok(XR_HAND_JOINT_INDEX_TIP_EXT)) {
        const double d = distance(joint(XR_HAND_JOINT_THUMB_TIP_EXT).pose, joint(XR_HAND_JOINT_INDEX_TIP_EXT).pose) / size;
        hand.pinch = saturate((0.10 - d) / (0.10 - 0.02));
    }

    // Grepp: medelavståndet från pek-, lång-, ring- och lillfingrets topp till
    // handflatan. Utsträckt ligger de 10 cm bort, knutna 5 cm.
    if (ok(XR_HAND_JOINT_PALM_EXT)) {
        const int tips[] = {XR_HAND_JOINT_INDEX_TIP_EXT, XR_HAND_JOINT_MIDDLE_TIP_EXT,
                            XR_HAND_JOINT_RING_TIP_EXT, XR_HAND_JOINT_LITTLE_TIP_EXT};
        double sum = 0.0;
        int count = 0;
        for (const int tip : tips) {
            if (!ok(tip)) continue;
            sum += distance(joint(tip).pose, joint(XR_HAND_JOINT_PALM_EXT).pose);
            ++count;
        }
        if (count > 0) {
            const double mean = sum / count / size;
            hand.grab = saturate((0.10 - mean) / (0.10 - 0.05));
        }
    }
}

bool OpenXrContext::createSwapchains(std::string& error) {
    const XrFunctions& fn = runtime_->fn();
    const XrInstance instance = runtime_->instance();
    const XrSystemId systemId = runtime_->systemId();
    const auto describe = [&](const char* call, XrResult r) { return runtime_->describe(call, r); };

    std::uint32_t count = 0;
    GLAZE_OPENXR_XR_CHECK(fn.xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                               0, &count, nullptr),
                          "xrEnumerateViewConfigurationViews");
    if (count != 2) {
        error = "Headsetet ger " + std::to_string(count) + " vyer i stället för två.";
        return false;
    }
    std::array<XrViewConfigurationView, 2> views;
    for (auto& v : views) v = xrStruct<XrViewConfigurationView>(XR_TYPE_VIEW_CONFIGURATION_VIEW);
    GLAZE_OPENXR_XR_CHECK(fn.xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                               2, &count, views.data()),
                          "xrEnumerateViewConfigurationViews");

    // Swapchainens format: sRGB, så att kompositorn läser bilden som sRGB.
    // Canvas skriver samma bytes genom en UNORM-vy; färgerna är redan
    // sRGB-kodade och ska inte kodas en gång till.
    GLAZE_OPENXR_XR_CHECK(fn.xrEnumerateSwapchainFormats(session_, 0, &count, nullptr), "xrEnumerateSwapchainFormats");
    std::vector<std::int64_t> formats(count);
    GLAZE_OPENXR_XR_CHECK(fn.xrEnumerateSwapchainFormats(session_, count, &count, formats.data()),
                          "xrEnumerateSwapchainFormats");
    const auto supported = [&](std::int64_t format) {
        return std::find(formats.begin(), formats.begin() + count, format) != formats.begin() + count;
    };
    std::int64_t format = 0;
    bool bgra = false;
    if (supported(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)) {
        format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    } else if (supported(DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)) {
        format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        bgra = true;
    } else {
        error = "Runtimen erbjuder varken R8G8B8A8_UNORM_SRGB eller B8G8R8A8_UNORM_SRGB som swapchain-format.";
        return false;
    }

    XrSessionApi::Handles handles;
    handles.instance = instance;
    handles.session = session_;
    handles.trackingSpace = trackingSpace_;
    handles.viewSpace = viewSpace_;

    for (std::size_t eye = 0; eye < 2; ++eye) {
        const XrViewConfigurationView& v = views[eye];
        // Runtimens fulla rekommenderade mått; aldrig nedskalat.
        if (v.recommendedImageRectWidth == 0 || v.recommendedImageRectHeight == 0) {
            error = "Runtimen gav ögat måttet 0 som rekommenderad bildstorlek.";
            return false;
        }

        auto info = xrStruct<XrSwapchainCreateInfo>(XR_TYPE_SWAPCHAIN_CREATE_INFO);
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        info.format = format;
        info.sampleCount = 1;   // kantutjämningen sker i Canvas eget mål
        info.width = v.recommendedImageRectWidth;
        info.height = v.recommendedImageRectHeight;
        info.faceCount = 1;
        info.arraySize = 1;
        info.mipCount = 1;
        GLAZE_OPENXR_XR_CHECK(fn.xrCreateSwapchain(session_, &info, &swapchains_[eye]), "xrCreateSwapchain");

        std::uint32_t imageCount = 0;
        GLAZE_OPENXR_XR_CHECK(fn.xrEnumerateSwapchainImages(swapchains_[eye], 0, &imageCount, nullptr),
                              "xrEnumerateSwapchainImages");
        std::vector<XrSwapchainImageD3D11KHR> images(imageCount, xrStruct<XrSwapchainImageD3D11KHR>(XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR));
        GLAZE_OPENXR_XR_CHECK(fn.xrEnumerateSwapchainImages(
                                  swapchains_[eye], imageCount, &imageCount,
                                  reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),
                              "xrEnumerateSwapchainImages");
        if (imageCount == 0) {
            error = "Runtimen gav ett swapchain utan bilder.";
            return false;
        }

        EyeImageSet& eyeImages = eyeImages_[eye];
        eyeImages.width = static_cast<int>(info.width);
        eyeImages.height = static_cast<int>(info.height);
        eyeImages.bgra = bgra;
        eyeImages.images.clear();
        for (std::uint32_t i = 0; i < imageCount; ++i) eyeImages.images.push_back(images[i].texture);

        handles.swapchains[eye] = swapchains_[eye];
        handles.imageCounts[eye] = imageCount;
        handles.widths[eye] = static_cast<std::int32_t>(info.width);
        handles.heights[eye] = static_cast<std::int32_t>(info.height);
    }
    api_ = std::make_unique<XrSessionApi>(fn, handles);
    return true;
}

} // namespace glaze::openxr::detail
