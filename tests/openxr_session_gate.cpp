// Prov av sidopaketet OpenXR:s sessionsloop och bildruta utan headset, runtime,
// fönster eller grafikenhet. Paketets egen funktionstabell (XrFunctions) fylls
// med funktioner som spelar runtimens roll och håller reda på OpenXR:s
// anropsordning: ett anrop i fel ordning räknas som ett brott och fäller provet.
//
// Prövar:
//   - bildrutans ordning: wait, acquire + wait per öga, begin, fyllning, release,
//     end med ett projektionslager som bär runtimens egen pose och fov per öga
//   - att acquire, wait och release balanseras, också vid fel, nollbild,
//     undantag och avbruten överlämning
//   - timeout från wait på en bild: bilden ritas inte, förvärvas inte om, och
//     väntas om nästa bildruta; för många i följd är ett fel
//   - shouldRender == false: begin och end utan lager och utan swapchain-anrop
//   - stopp mitt i en bildruta, stopp före sessionen börjat, runtime som aldrig
//     svarar på exit, sessionsförlust och förlorad instans
//   - överlämningen mellan trådarna (JobGate) och sessionens tillståndsmaskin
//
// Varje kontroll skriver vad som föll; programmet returnerar 1 om något föll.

#include "openxr_context.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <iostream>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace glaze::openxr;
using namespace glaze::openxr::detail;

int g_failures = 0;
int g_checks   = 0;

void check(bool ok, const std::string& what,
           const std::source_location& where = std::source_location::current())
{
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cerr << "FALL rad " << where.line() << ": " << what << "\n";
    }
}

template <class T>
void checkEq(const T& actual, const T& expected, const std::string& what,
             const std::source_location& where = std::source_location::current())
{
    ++g_checks;
    if (!(actual == expected)) {
        ++g_failures;
        std::cerr << "FALL rad " << where.line() << ": " << what << ": fick " << actual << ", väntade "
                  << expected << "\n";
    }
}

template <class H>
H handle(std::uintptr_t value)
{
    return reinterpret_cast<H>(value);
}

// ------------------------------------------------------------- runtimen

// Allt runtimen vet och har sett. Ett anrop i fel ordning ökar `violations`.
struct EndRecord {
    int                         layerCount = 0;
    XrTime                      time = 0;
    std::array<XrFovf, 2>       fov{};
    std::array<XrPosef, 2>      pose{};
    std::array<XrSwapchain, 2>  swapchain{};
    std::array<XrExtent2Di, 2>  extent{};
};

struct Chain {
    bool          acquired = false;
    bool          waited   = false;
    std::uint32_t next     = 0;
    int           acquires = 0;
    int           waits    = 0;   // lyckade väntan
    int           releases = 0;
    int           timeoutsLeft = 0;
};

struct Mock {
    std::vector<std::string> log;
    std::deque<XrSessionState> events;
    bool instanceLoss = false;
    bool answerExit   = true;   // xrRequestExitSession ger STOPPING

    enum class Frame { Idle, Waited, Begun } frame = Frame::Idle;
    bool          sessionRunning = false;
    XrTime        time   = 0;
    bool          shouldRender = true;
    int           frames = 0;
    std::function<void(Mock&)> onWait;

    std::array<Chain, 2> chains;
    std::vector<EndRecord> ends;
    int violations = 0;
    std::vector<std::string> violationText;

    void violate(const std::string& text)
    {
        ++violations;
        violationText.push_back(text);
    }

    int count(const std::string& entry) const { return static_cast<int>(std::count(log.begin(), log.end(), entry)); }
};

Mock g;

int eyeOf(XrSwapchain chain) { return static_cast<int>(reinterpret_cast<std::uintptr_t>(chain)) - 0x1000; }

XrResult XRAPI_CALL mockPollEvent(XrInstance, XrEventDataBuffer* buffer)
{
    if (g.instanceLoss) {
        g.instanceLoss = false;
        auto* e = reinterpret_cast<XrEventDataInstanceLossPending*>(buffer);
        e->type = XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING;
        e->next = nullptr;
        return XR_SUCCESS;
    }
    if (g.events.empty()) return XR_EVENT_UNAVAILABLE;
    auto* e = reinterpret_cast<XrEventDataSessionStateChanged*>(buffer);
    e->type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
    e->next = nullptr;
    e->state = g.events.front();
    g.events.pop_front();
    return XR_SUCCESS;
}

XrResult XRAPI_CALL mockBeginSession(XrSession, const XrSessionBeginInfo*)
{
    g.log.push_back("beginSession");
    if (g.sessionRunning) g.violate("xrBeginSession på en körande session");
    g.sessionRunning = true;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL mockEndSession(XrSession)
{
    g.log.push_back("endSession");
    if (!g.sessionRunning) g.violate("xrEndSession utan körande session");
    if (g.frame == Mock::Frame::Begun) g.violate("xrEndSession medan en bildruta är påbörjad");
    g.sessionRunning = false;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL mockRequestExit(XrSession)
{
    g.log.push_back("exit");
    if (g.answerExit) g.events.push_back(XR_SESSION_STATE_STOPPING);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL mockWaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState* state)
{
    g.log.push_back("wait");
    if (g.frame != Mock::Frame::Idle) g.violate("xrWaitFrame före föregående bildruta avslutats");
    g.frame = Mock::Frame::Waited;
    ++g.frames;
    g.time += 11'111'111;
    state->predictedDisplayTime = g.time;
    state->predictedDisplayPeriod = 11'111'111;
    state->shouldRender = g.shouldRender ? XR_TRUE : XR_FALSE;
    if (g.onWait) g.onWait(g);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return XR_SUCCESS;
}

XrResult XRAPI_CALL mockBeginFrame(XrSession, const XrFrameBeginInfo*)
{
    g.log.push_back("begin");
    if (g.frame != Mock::Frame::Waited) g.violate("xrBeginFrame utan föregående xrWaitFrame");
    g.frame = Mock::Frame::Begun;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL mockEndFrame(XrSession, const XrFrameEndInfo* info)
{
    g.log.push_back(std::string("end") + std::to_string(info->layerCount));
    if (g.frame != Mock::Frame::Begun) g.violate("xrEndFrame utan föregående xrBeginFrame");
    g.frame = Mock::Frame::Idle;

    EndRecord record;
    record.layerCount = static_cast<int>(info->layerCount);
    record.time = info->displayTime;
    if (info->layerCount == 1) {
        const auto* layer = reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[0]);
        for (std::uint32_t eye = 0; eye < 2 && eye < layer->viewCount; ++eye) {
            record.fov[eye] = layer->views[eye].fov;
            record.pose[eye] = layer->views[eye].pose;
            record.swapchain[eye] = layer->views[eye].subImage.swapchain;
            record.extent[eye] = layer->views[eye].subImage.imageRect.extent;
        }
        for (const Chain& chain : g.chains) {
            if (chain.acquired) g.violate("xrEndFrame med ett lager medan en bild är förvärvad");
        }
    }
    g.ends.push_back(record);
    return XR_SUCCESS;
}

XrResult XRAPI_CALL mockAcquire(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo*, std::uint32_t* index)
{
    const int eye = eyeOf(swapchain);
    Chain& chain = g.chains[static_cast<std::size_t>(eye)];
    g.log.push_back("acq" + std::to_string(eye));
    if (chain.acquired) g.violate("xrAcquireSwapchainImage med en bild som redan är förvärvad");
    chain.acquired = true;
    chain.waited = false;
    ++chain.acquires;
    *index = chain.next++ % 3;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL mockWaitImage(XrSwapchain swapchain, const XrSwapchainImageWaitInfo*)
{
    const int eye = eyeOf(swapchain);
    Chain& chain = g.chains[static_cast<std::size_t>(eye)];
    g.log.push_back("imgwait" + std::to_string(eye));
    if (!chain.acquired) g.violate("xrWaitSwapchainImage utan förvärvad bild");
    if (chain.timeoutsLeft > 0) {
        --chain.timeoutsLeft;
        return XR_TIMEOUT_EXPIRED;
    }
    chain.waited = true;
    ++chain.waits;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL mockRelease(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo*)
{
    const int eye = eyeOf(swapchain);
    Chain& chain = g.chains[static_cast<std::size_t>(eye)];
    g.log.push_back("rel" + std::to_string(eye));
    if (!chain.acquired || !chain.waited) g.violate("xrReleaseSwapchainImage utan en väntad bild");
    chain.acquired = false;
    chain.waited = false;
    ++chain.releases;
    return XR_SUCCESS;
}

XrResult XRAPI_CALL mockLocateViews(XrSession, const XrViewLocateInfo* info, XrViewState* state,
                                    std::uint32_t capacity, std::uint32_t* count, XrView* views)
{
    g.log.push_back("locateViews");
    state->viewStateFlags = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    *count = 2;
    for (std::uint32_t eye = 0; eye < capacity && eye < 2; ++eye) {
        views[eye].pose.orientation = XrQuaternionf{0.0f, 0.0f, 0.0f, 1.0f};
        views[eye].pose.position = XrVector3f{eye == 0 ? -0.03f : 0.03f, 1.6f, static_cast<float>(info->displayTime) * 0.0f};
        views[eye].fov = XrFovf{-0.9f, 0.75f, 0.8f, -0.85f};
    }
    return XR_SUCCESS;
}

XrResult XRAPI_CALL mockResultToString(XrInstance, XrResult result, char buffer[XR_MAX_RESULT_STRING_SIZE])
{
    std::snprintf(buffer, XR_MAX_RESULT_STRING_SIZE, "XR_RESULT_%d", static_cast<int>(result));
    return XR_SUCCESS;
}

// En tabell med mockens funktioner, och handtag som mockens swapchains känns
// igen på.
struct Rig {
    XrFunctions fn;
    std::unique_ptr<XrSessionApi> api;
    std::vector<Status> statuses;
    std::atomic<bool> stop{false};

    explicit Rig(int maxNotReady = 30)
    {
        g = Mock{};
        fn.xrPollEvent = &mockPollEvent;
        fn.xrBeginSession = &mockBeginSession;
        fn.xrEndSession = &mockEndSession;
        fn.xrRequestExitSession = &mockRequestExit;
        fn.xrWaitFrame = &mockWaitFrame;
        fn.xrBeginFrame = &mockBeginFrame;
        fn.xrEndFrame = &mockEndFrame;
        fn.xrAcquireSwapchainImage = &mockAcquire;
        fn.xrWaitSwapchainImage = &mockWaitImage;
        fn.xrReleaseSwapchainImage = &mockRelease;
        fn.xrLocateViews = &mockLocateViews;
        fn.xrResultToString = &mockResultToString;

        XrSessionApi::Handles handles;
        handles.instance = handle<XrInstance>(0x10);
        handles.session = handle<XrSession>(0x20);
        handles.localSpace = handle<XrSpace>(0x30);
        handles.viewSpace = handle<XrSpace>(0x31);
        handles.swapchains = {handle<XrSwapchain>(0x1000), handle<XrSwapchain>(0x1001)};
        handles.imageCounts = {3, 3};
        handles.widths = {1832, 1832};
        handles.heights = {1920, 1920};
        XrSessionApi::Limits limits;
        limits.imageWaitTimeout = 1'000'000;
        limits.maxNotReadyFrames = maxNotReady;
        api = std::make_unique<XrSessionApi>(fn, handles, limits);
    }

    // Runtimen visar sessionen: READY, SYNCHRONIZED, VISIBLE, FOCUSED.
    static void showSession()
    {
        g.events = {XR_SESSION_STATE_READY, XR_SESSION_STATE_SYNCHRONIZED, XR_SESSION_STATE_VISIBLE,
                    XR_SESSION_STATE_FOCUSED};
    }

    Status run(FrameSink& sink, std::chrono::milliseconds exitTimeout = std::chrono::milliseconds(5000))
    {
        LoopHooks hooks;
        hooks.stopRequested = [this] { return stop.load(); };
        hooks.sleep = [](std::chrono::milliseconds time) {
            std::this_thread::sleep_for(std::min(time, std::chrono::milliseconds(2)));
        };
        hooks.onStatus = [this](const Status& status) { statuses.push_back(status); };
        hooks.exitTimeout = exitTimeout;
        SessionLoop loop(*api, sink, hooks);
        return loop.run();
    }
};

std::array<EyeLayer, 2> knownLayers()
{
    std::array<EyeLayer, 2> layers;
    layers[0].pose.position = {-0.03125, 1.5, 0.25};
    layers[0].pose.orientation = {0.0, 0.0, 0.0, 1.0};
    layers[0].fov = {-0.90, 0.75, 0.80, -0.85};
    layers[1].pose.position = {0.03125, 1.5, 0.25};
    layers[1].pose.orientation = {0.0, 0.0, 0.0, 1.0};
    layers[1].fov = {-0.75, 0.90, 0.80, -0.85};
    return layers;
}

class FakeContent final : public FrameContent {
public:
    std::function<FillResult(int call, std::array<EyeLayer, 2>&, std::string&)> behavior;
    int calls = 0;

    FillResult fill(const FrameTicket&, std::array<EyeLayer, 2>& layers, std::string& error) override
    {
        ++calls;
        g.log.push_back("fill");
        layers = knownLayers();
        return behavior ? behavior(calls, layers, error) : FillResult::Layer;
    }
};

// UI-tråden i provet: kör completeFrame direkt, eller gör något annat först.
class FakeSink final : public FrameSink {
public:
    FakeSink(XrSessionApi& api, FakeContent& content) : api_(api), content_(content) {}
    std::function<std::optional<SubmitResult>(const FrameTicket&, std::string&)> before;
    int submits = 0;
    std::vector<FrameTicket> tickets;

    SubmitResult submit(const FrameTicket& ticket, std::string& error) override
    {
        ++submits;
        tickets.push_back(ticket);
        if (before) {
            if (const auto result = before(ticket, error)) return *result;
        }
        return completeFrame(api_, ticket, content_, error);
    }

private:
    XrSessionApi& api_;
    FakeContent&  content_;
};

// Allt som förvärvats är släppt, ingen bildruta är kvar öppen och inget anrop
// kom i fel ordning.
void expectBalanced(const std::string& tag, bool allowAcquiredLeft = false)
{
    check(g.violations == 0, tag + ": anrop i fel ordning (" +
                                 (g.violationText.empty() ? std::string() : g.violationText.front()) + ")");
    for (std::size_t eye = 0; eye < 2; ++eye) {
        const Chain& chain = g.chains[eye];
        if (allowAcquiredLeft) {
            check(chain.acquires - chain.releases == (chain.acquired ? 1 : 0),
                  tag + ": öga " + std::to_string(eye) + " förvärv och släpp stämmer inte");
        } else {
            checkEq(chain.acquires, chain.releases, tag + ": öga " + std::to_string(eye) + " förvärv = släpp");
            check(!chain.acquired, tag + ": öga " + std::to_string(eye) + " har en bild kvar");
        }
    }
    check(g.frame == Mock::Frame::Idle, tag + ": en bildruta är kvar öppen");
    checkEq(g.count("wait"), g.count("begin"), tag + ": wait = begin");
    checkEq(g.count("begin"), static_cast<int>(g.ends.size()), tag + ": begin = end");
}

int layerFrames()
{
    return static_cast<int>(std::count_if(g.ends.begin(), g.ends.end(), [](const EndRecord& e) { return e.layerCount == 1; }));
}

// ------------------------------------------------------------------ prov

void testFrameOrderAndLayer()
{
    Rig rig;
    FakeContent content;
    FrameTiming timing;
    std::string error;
    check(rig.api->waitFrame(timing, error), "waitFrame: " + error);
    check(timing.shouldRender, "shouldRender");
    std::array<std::uint32_t, 2> index{};
    check(rig.api->acquireImages(index, error) == AcquireOutcome::Ready, "acquireImages: Ready");
    checkEq(rig.api->waitedImages(), 2, "båda bilderna väntade");

    FrameTicket ticket;
    ticket.predictedDisplayTime = timing.predictedDisplayTime;
    ticket.imageIndex = index;
    check(completeFrame(*rig.api, ticket, content, error) == SubmitResult::Done, "completeFrame: Done " + error);

    const std::vector<std::string> expected = {"wait", "acq0", "imgwait0", "acq1", "imgwait1", "begin",
                                               "fill", "rel0", "rel1", "end1"};
    check(g.log == expected, "bildrutans ordning");
    expectBalanced("normal bildruta");

    check(g.ends.size() == 1 && g.ends[0].layerCount == 1, "ett projektionslager");
    if (!g.ends.empty()) {
        const EndRecord& end = g.ends[0];
        checkEq(end.time, timing.predictedDisplayTime, "lagret bär bildrutans visningstid");
        const auto layers = knownLayers();
        for (std::size_t eye = 0; eye < 2; ++eye) {
            const std::string tag = "öga " + std::to_string(eye);
            check(end.fov[eye].angleLeft == static_cast<float>(layers[eye].fov.left) &&
                      end.fov[eye].angleRight == static_cast<float>(layers[eye].fov.right) &&
                      end.fov[eye].angleUp == static_cast<float>(layers[eye].fov.up) &&
                      end.fov[eye].angleDown == static_cast<float>(layers[eye].fov.down),
                  tag + ": runtimens egen fov når lagret oförändrad");
            check(end.pose[eye].position.x == static_cast<float>(layers[eye].pose.position.x) &&
                      end.pose[eye].position.y == static_cast<float>(layers[eye].pose.position.y) &&
                      end.pose[eye].position.z == static_cast<float>(layers[eye].pose.position.z) &&
                      end.pose[eye].orientation.w == 1.0f,
                  tag + ": runtimens egen pose når lagret oförändrad");
            check(end.swapchain[eye] == handle<XrSwapchain>(0x1000 + eye), tag + ": eget swapchain");
            checkEq(end.extent[eye].width, 1832, tag + ": hela bildens bredd");
            checkEq(end.extent[eye].height, 1920, tag + ": hela bildens höjd");
        }
    }
}

void testLocateViews()
{
    Rig rig;
    LocatedViews views;
    std::string error;
    check(rig.api->locateViews(5, views, error), "locateViews: " + error);
    check(views.valid, "locateViews: giltiga ögon");
    checkEq(views.fov[0].left, static_cast<double>(-0.9f), "vänster öga: vänster vinkel");
    checkEq(views.fov[1].right, static_cast<double>(0.75f), "höger öga: höger vinkel");
    checkEq(views.poses[0].position.x, static_cast<double>(-0.03f), "vänster ögas läge");
}

void testShouldRenderFalse()
{
    Rig rig;
    FakeContent content;
    FakeSink sink(*rig.api, content);
    Rig::showSession();
    g.shouldRender = false;
    g.onWait = [&](Mock& m) { if (m.frames == 4) rig.stop = true; };
    const Status status = rig.run(sink);
    checkEq(static_cast<int>(status.state), static_cast<int>(State::Stopped), "shouldRender=false: Stopped");
    checkEq(sink.submits, 0, "shouldRender=false: inget bildjobb");
    checkEq(content.calls, 0, "shouldRender=false: inget ritat");
    checkEq(g.count("acq0") + g.count("acq1") + g.count("rel0") + g.count("rel1"), 0, "shouldRender=false: inga swapchain-anrop");
    check(g.frames >= 4, "shouldRender=false: bildrutor gick");
    check(layerFrames() == 0, "shouldRender=false: inga lager");
    expectBalanced("shouldRender=false");
}

void testRenderedFramesThenStop()
{
    Rig rig;
    FakeContent content;
    FakeSink sink(*rig.api, content);
    Rig::showSession();
    g.onWait = [&](Mock& m) { if (m.frames == 4) rig.stop = true; };
    const Status status = rig.run(sink);
    checkEq(static_cast<int>(status.state), static_cast<int>(State::Stopped), "stopp: Stopped");
    checkEq(layerFrames(), 3, "stopp: tre ritade bildrutor före stoppet");
    checkEq(content.calls, 3, "stopp: tre fyllningar");
    checkEq(sink.submits, 3, "stopp: tre överlämningar");
    checkEq(g.count("beginSession"), 1, "stopp: en xrBeginSession");
    checkEq(g.count("exit"), 1, "stopp: en xrRequestExitSession");
    checkEq(g.count("endSession"), 1, "stopp: en xrEndSession");
    check(!g.sessionRunning, "stopp: sessionen avslutad");
    // Sekvensnumren och visningstiderna följer bildrutorna.
    if (sink.tickets.size() == 3) {
        check(sink.tickets[0].sequence == 1 && sink.tickets[2].sequence == 3, "stopp: sekvensnummer räknas upp");
        check(sink.tickets[0].focused, "stopp: biljetten bär fokus");
        check(sink.tickets[1].predictedDisplayTime > sink.tickets[0].predictedDisplayTime,
              "stopp: visningstiden växer");
        check(sink.tickets[1].deltaSeconds > 0.0 && sink.tickets[1].deltaSeconds <= 0.1, "stopp: delta i (0, 0,1]");
    }
    expectBalanced("stopp");
    const bool sawRunning = std::any_of(rig.statuses.begin(), rig.statuses.end(),
                                        [](const Status& s) { return s.state == State::Running; });
    check(sawRunning, "stopp: läget Running visades");
    check(!rig.statuses.empty() && rig.statuses.back().state == State::Stopping, "stopp: sista visade läget är Stopping");
}

void testWaitTimeoutRecovers()
{
    Rig rig;
    FakeContent content;
    FakeSink sink(*rig.api, content);
    Rig::showSession();
    g.chains[0].timeoutsLeft = 2;
    g.onWait = [&](Mock& m) { if (m.frames == 5) rig.stop = true; };
    const Status status = rig.run(sink);
    checkEq(static_cast<int>(status.state), static_cast<int>(State::Stopped), "timeout: Stopped");
    // Bildruta 1 och 2: bilden blev inte klar. Bildruta 3 och 4 ritas.
    checkEq(layerFrames(), 2, "timeout: två ritade bildrutor efter timeouten");
    checkEq(content.calls, 2, "timeout: en bild som inte är klar ritas aldrig");
    checkEq(g.chains[0].acquires, 2, "timeout: den väntande bilden förvärvas inte om (ett förvärv per ritad bildruta)");
    checkEq(g.count("imgwait0"), 4, "timeout: bilden väntas om tills den är klar");
    check(g.ends.size() >= 2 && g.ends[0].layerCount == 0 && g.ends[1].layerCount == 0,
          "timeout: de två första bildrutorna avslutas utan lager");
    expectBalanced("timeout");
}

void testWaitTimeoutTooLong()
{
    Rig rig(3);
    FakeContent content;
    FakeSink sink(*rig.api, content);
    Rig::showSession();
    g.chains[1].timeoutsLeft = 1000;
    const Status status = rig.run(sink);
    checkEq(static_cast<int>(status.state), static_cast<int>(State::Failed), "evig timeout: Failed");
    check(status.message.find("tidsgräns") != std::string::npos, "evig timeout: texten nämner tidsgränsen");
    checkEq(content.calls, 0, "evig timeout: aldrig ritat");
    check(layerFrames() == 0, "evig timeout: inga lager");
    check(g.violations == 0, "evig timeout: anrop i fel ordning");
    check(g.frame != Mock::Frame::Begun, "evig timeout: ingen bildruta påbörjad");
    // En förvärvad bild som aldrig blev klar får inte släppas, men inga fler förvärvas.
    checkEq(g.chains[1].acquires, 1, "evig timeout: bara ett förvärv");
    checkEq(g.chains[1].releases, 0, "evig timeout: den ovänta bilden släpps inte");
}

void testStopDuringFrame()
{
    Rig rig;
    FakeContent content;
    FakeSink sink(*rig.api, content);
    Rig::showSession();
    // Bildruta 2: stoppet kommer innan UI-tråden börjat; överlämningen avbryts.
    sink.before = [&](const FrameTicket& ticket, std::string&) -> std::optional<SubmitResult> {
        if (ticket.sequence != 2) return std::nullopt;
        rig.stop = true;
        return SubmitResult::Cancelled;
    };
    const Status status = rig.run(sink);
    checkEq(static_cast<int>(status.state), static_cast<int>(State::Stopped), "avbruten bildruta: Stopped");
    checkEq(content.calls, 1, "avbruten bildruta: bara första ritas");
    checkEq(layerFrames(), 1, "avbruten bildruta: ett lager");
    check(g.ends.size() >= 2 && g.ends[1].layerCount == 0, "avbruten bildruta: avslutas utan lager");
    // Den avbrutna bildrutan: begin, släpp båda, end utan lager.
    const auto second = std::find(g.log.begin(), g.log.end(), "end1");
    const std::vector<std::string> tail(second + 1, second + 1 + 8);
    const std::vector<std::string> expectedTail = {"wait", "acq0", "imgwait0", "acq1", "imgwait1", "begin", "rel0", "rel1"};
    check(tail == expectedTail, "avbruten bildruta: ordningen begin, släpp, end");
    expectBalanced("avbruten bildruta");
}

void testFillFailures()
{
    {
        Rig rig;
        FakeContent content;
        content.behavior = [](int call, std::array<EyeLayer, 2>&, std::string& error) {
            if (call == 2) {
                error = "ritfelet";
                return FillResult::Failed;
            }
            return FillResult::Layer;
        };
        FakeSink sink(*rig.api, content);
        Rig::showSession();
        const Status status = rig.run(sink);
        checkEq(static_cast<int>(status.state), static_cast<int>(State::Failed), "ritfel: Failed");
        check(status.message.find("ritfelet") != std::string::npos, "ritfel: texten når statusen");
        checkEq(layerFrames(), 1, "ritfel: bara första bildrutan fick ett lager");
        check(g.ends.size() == 2 && g.ends[1].layerCount == 0, "ritfel: bildrutan avslutas utan lager");
        expectBalanced("ritfel");
    }
    {
        Rig rig;
        FakeContent content;
        content.behavior = [](int, std::array<EyeLayer, 2>&, std::string&) -> FillResult {
            throw std::runtime_error("bom");
        };
        FakeSink sink(*rig.api, content);
        Rig::showSession();
        const Status status = rig.run(sink);
        checkEq(static_cast<int>(status.state), static_cast<int>(State::Failed), "undantag: Failed");
        check(status.message.find("bom") != std::string::npos, "undantag: texten når statusen");
        expectBalanced("undantag");
    }
    {
        Rig rig;
        FakeContent content;
        content.behavior = [](int call, std::array<EyeLayer, 2>&, std::string&) {
            return call == 1 ? FillResult::NoLayer : FillResult::Layer;
        };
        FakeSink sink(*rig.api, content);
        Rig::showSession();
        g.onWait = [&](Mock& m) { if (m.frames == 3) rig.stop = true; };
        const Status status = rig.run(sink);
        checkEq(static_cast<int>(status.state), static_cast<int>(State::Stopped), "ingen pose: Stopped");
        checkEq(layerFrames(), 1, "ingen pose: bara den andra bildrutan fick ett lager");
        check(g.ends.size() >= 2 && g.ends[0].layerCount == 0, "ingen pose: första bildrutan utan lager");
        expectBalanced("ingen pose");
    }
}

void testSubmitFailedReleasesImages()
{
    Rig rig;
    FakeContent content;
    FakeSink sink(*rig.api, content);
    Rig::showSession();
    // Överlämningen misslyckas innan UI-tråden rört bildrutan.
    sink.before = [](const FrameTicket&, std::string& error) -> std::optional<SubmitResult> {
        error = "pumpen tog inte emot bildrutan";
        return SubmitResult::Failed;
    };
    const Status status = rig.run(sink);
    checkEq(static_cast<int>(status.state), static_cast<int>(State::Failed), "misslyckad överlämning: Failed");
    check(status.message.find("pumpen") != std::string::npos, "misslyckad överlämning: texten når statusen");
    checkEq(g.chains[0].acquires, g.chains[0].releases, "misslyckad överlämning: öga 0 släppt");
    checkEq(g.chains[1].acquires, g.chains[1].releases, "misslyckad överlämning: öga 1 släppt");
    check(g.violations == 0, "misslyckad överlämning: anrop i fel ordning");
}

void testSessionLoss()
{
    Rig rig;
    FakeContent content;
    FakeSink sink(*rig.api, content);
    Rig::showSession();
    g.onWait = [&](Mock& m) { if (m.frames == 3) m.events.push_back(XR_SESSION_STATE_LOSS_PENDING); };
    const Status status = rig.run(sink);
    checkEq(static_cast<int>(status.state), static_cast<int>(State::Failed), "förlust: Failed");
    check(status.message.find("förlorades") != std::string::npos, "förlust: texten säger vad som hänt");
    checkEq(g.frames, 3, "förlust: inga bildrutor efter förlusten");
    checkEq(layerFrames(), 3, "förlust: bildrutorna före förlusten ritades");
    checkEq(g.count("endSession"), 0, "förlust: ingen xrEndSession på en förlorad session");
    expectBalanced("förlust");
}

void testInstanceLoss()
{
    Rig rig;
    FakeContent content;
    FakeSink sink(*rig.api, content);
    Rig::showSession();
    g.onWait = [&](Mock& m) { if (m.frames == 2) m.instanceLoss = true; };
    const Status status = rig.run(sink);
    checkEq(static_cast<int>(status.state), static_cast<int>(State::Failed), "förlorad instans: Failed");
    check(status.message.find("runtimen") != std::string::npos, "förlorad instans: texten nämner runtimen");
    expectBalanced("förlorad instans");
}

void testStopBeforeSession()
{
    Rig rig;
    FakeContent content;
    FakeSink sink(*rig.api, content);
    rig.stop = true;   // inget READY har kommit än
    const Status status = rig.run(sink);
    checkEq(static_cast<int>(status.state), static_cast<int>(State::Stopped), "stopp före start: Stopped");
    check(g.log.empty(), "stopp före start: inga OpenXR-anrop");
}

void testExitNeverAnswered()
{
    Rig rig;
    FakeContent content;
    FakeSink sink(*rig.api, content);
    Rig::showSession();
    g.answerExit = false;
    g.onWait = [&](Mock& m) { if (m.frames == 2) rig.stop = true; };
    const Status status = rig.run(sink, std::chrono::milliseconds(40));
    checkEq(static_cast<int>(status.state), static_cast<int>(State::Stopped), "ingen exit-respons: Stopped efter tidsgränsen");
    checkEq(g.count("exit"), 1, "ingen exit-respons: en begäran");
    checkEq(g.count("endSession"), 0, "ingen exit-respons: sessionen rivs utan xrEndSession");
    expectBalanced("ingen exit-respons");
}

void testRuntimeEndsSession()
{
    // Runtimen avslutar själv (användaren tar av headsetet): STOPPING → xrEndSession → EXITING.
    Rig rig;
    FakeContent content;
    FakeSink sink(*rig.api, content);
    Rig::showSession();
    g.onWait = [&](Mock& m) {
        if (m.frames == 3) {
            m.events.push_back(XR_SESSION_STATE_STOPPING);
            m.events.push_back(XR_SESSION_STATE_EXITING);
        }
    };
    const Status status = rig.run(sink);
    checkEq(static_cast<int>(status.state), static_cast<int>(State::Stopped), "runtimen avslutar: Stopped");
    checkEq(g.count("endSession"), 1, "runtimen avslutar: en xrEndSession");
    expectBalanced("runtimen avslutar");
}

// ---------------------------------------------------------- överlämningen

void testJobGateFlow()
{
    JobGate gate;
    std::uint64_t first = 0, second = 0;
    check(gate.arm(first) && first != 0, "arm ger ett id");
    std::uint64_t other = 0;
    check(!gate.arm(other), "ett andra uppdrag medan det första väntar nekas");
    check(gate.begin(first), "begin med rätt id");
    check(!gate.begin(first), "begin två gånger");
    gate.finish(first);
    check(gate.await(first) == JobGate::Await::Done, "await efter finish: Done");
    check(gate.arm(second) && second != first, "ett nytt uppdrag får ett nytt id");
    check(!gate.begin(first), "ett gammalt id får aldrig köra");
    gate.abort(second);
    std::uint64_t third = 0;
    check(gate.arm(third), "efter abort går det att förbereda ett nytt uppdrag");
    gate.abort(third);
}

void testJobGateStopWhilePosted()
{
    JobGate gate;
    std::uint64_t id = 0;
    check(gate.arm(id), "arm");
    gate.stop();
    check(gate.stopped(), "stopped");
    check(gate.await(id) == JobGate::Await::Cancelled, "stopp medan uppdraget väntar: Cancelled");
    check(!gate.begin(id), "ett avbrutet uppdrag får inte börja");
    std::uint64_t again = 0;
    check(!gate.arm(again), "inga nya uppdrag efter stopp");
}

void testJobGateStopWhileRunning()
{
    JobGate gate;
    std::uint64_t id = 0;
    check(gate.arm(id), "arm");
    std::atomic<bool> finished{false};
    std::atomic<bool> began{false};
    std::thread ui([&] {
        if (!gate.begin(id)) return;
        began = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        finished = true;
        gate.finish(id);
    });
    // Stoppet kommer medan UI-tråden kör: väntan avbryts inte.
    while (!began.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    gate.stop();
    const JobGate::Await result = gate.await(id);
    check(result == JobGate::Await::Done, "stopp medan uppdraget kör: Done");
    check(finished.load(), "stopp medan uppdraget kör: väntade in uppdraget");
    ui.join();
}

void testJobGateStopWakesSleep()
{
    JobGate gate;
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        gate.stop();
    });
    const auto before = std::chrono::steady_clock::now();
    gate.sleepFor(std::chrono::seconds(5));
    const auto elapsed = std::chrono::steady_clock::now() - before;
    stopper.join();
    check(elapsed < std::chrono::seconds(2), "ett stopp avbryter vilan");
}

// -------------------------------------------------------------- livscykeln

void testLifeCycle()
{
    {
        LifeCycle life;
        check(life.onPhase(Phase::Ready) == Step::BeginSession, "READY: börja sessionen");
        life.sessionBegun();
        check(life.onPhase(Phase::Ready) == Step::None, "READY igen på en körande session: inget");
        check(!life.rendering(), "inte synlig än: ritas inte");
        life.onPhase(Phase::Visible);
        check(life.rendering() && !life.focused(), "VISIBLE: ritas utan fokus");
        life.onPhase(Phase::Focused);
        check(life.focused(), "FOCUSED: fokus");
        const std::uint64_t epoch = life.focusEpoch();
        life.onPhase(Phase::Visible);
        check(life.focusEpoch() == epoch + 1, "fokus förlorat: perioden räknas upp");
        life.onPhase(Phase::Visible);
        check(life.focusEpoch() == epoch + 1, "fokus förlorat två gånger i följd räknas en gång");
        check(life.onPhase(Phase::Stopping) == Step::EndSession, "STOPPING: avsluta sessionen");
        life.sessionEnded();
        life.onPhase(Phase::Exiting);
        check(life.terminate(), "EXITING: avsluta tråden");
        check(life.finalStatus().state == State::Stopped, "vanligt slut: Stopped");
    }
    {
        LifeCycle life;
        life.onPhase(Phase::Ready);
        life.sessionBegun();
        life.onPhase(Phase::Focused);
        life.onPhase(Phase::LossPending);
        check(life.terminate(), "LOSS_PENDING: avsluta direkt");
        check(!life.rendering() && !life.focused(), "LOSS_PENDING: ritas inte");
        check(life.finalStatus().state == State::Failed, "LOSS_PENDING: Failed");
        check(life.finalStatus().message.find("förlorades") != std::string::npos, "LOSS_PENDING: texten");
    }
    {
        LifeCycle life;
        life.requestStop();
        check(life.terminate(), "stopp utan körande session avslutar direkt");
        check(life.onPhase(Phase::Ready) == Step::None, "READY efter stopp börjar inte sessionen");
    }
    {
        LifeCycle life;
        life.onPhase(Phase::Ready);
        life.sessionBegun();
        life.requestStop("skäl");
        check(!life.terminate(), "stopp med körande session väntar på runtimen");
        check(life.takeExitRequest(), "exit begärs en gång");
        check(!life.takeExitRequest(), "exit begärs inte två gånger");
        life.requestStop("annat skäl");
        life.sessionEnded();
        check(life.terminate(), "avslutad session efter stopp avslutar tråden");
        check(life.finalStatus().message == "skäl", "första skälet behålls");
    }
}

void testPackLuid()
{
    LUID luid{};
    luid.LowPart = 0x00000001u;
    luid.HighPart = -1;
    check(packLuid(luid) == 0xFFFFFFFF00000001ull, "packLuid: HighPart i de övre 32 bitarna, tecknet utan utvidgning");
    luid.LowPart = 0xDEADBEEFu;
    luid.HighPart = 0x12;
    check(packLuid(luid) == 0x00000012DEADBEEFull, "packLuid: LowPart i de nedre");
}

} // namespace

int main()
{
    testFrameOrderAndLayer();
    testLocateViews();
    testShouldRenderFalse();
    testRenderedFramesThenStop();
    testWaitTimeoutRecovers();
    testWaitTimeoutTooLong();
    testStopDuringFrame();
    testFillFailures();
    testSubmitFailedReleasesImages();
    testSessionLoss();
    testInstanceLoss();
    testStopBeforeSession();
    testExitNeverAnswered();
    testRuntimeEndsSession();
    testJobGateFlow();
    testJobGateStopWhilePosted();
    testJobGateStopWhileRunning();
    testJobGateStopWakesSleep();
    testLifeCycle();
    testPackLuid();

    if (g_failures != 0) {
        std::cerr << g_failures << " av " << g_checks << " kontroller föll.\n";
        return 1;
    }
    std::cout << "Alla " << g_checks << " kontroller gick igenom.\n";
    return 0;
}
