#include <glaze_openxr/session.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <functional>
#include <utility>

#include <gfx/platform.h>
#include <glaze/timer.h>

#include "actions.h"
#include "canvas_bridge.h"
#include "openxr_context.h"
#include "session_state.h"

namespace glaze::openxr {

namespace detail {

// ------------------------------------------------------------------ LIVSCYKEL

Step LifeCycle::onPhase(Phase phase) {
    if (phase_ == Phase::Focused && phase != Phase::Focused) ++focusEpoch_;
    phase_ = phase;
    switch (phase) {
    case Phase::Ready:
        return (!running_ && !stopRequested_) ? Step::BeginSession : Step::None;
    case Phase::Stopping:
        return running_ ? Step::EndSession : Step::None;
    case Phase::Exiting:
        terminate_ = true;
        return Step::None;
    case Phase::LossPending:
        fail("Sessionen förlorades (runtimen eller headsetet försvann). Starta VR igen.");
        return Step::None;
    default:
        return Step::None;
    }
}

void LifeCycle::sessionEnded() noexcept {
    running_ = false;
    exitRequested_ = false;
    if (stopRequested_) terminate_ = true;
}

void LifeCycle::requestStop(const std::string& failure) {
    if (failure_.empty()) failure_ = failure;
    stopRequested_ = true;
    if (!running_) terminate_ = true;
}

void LifeCycle::fail(const std::string& message) {
    if (failure_.empty()) failure_ = message;
    stopRequested_ = true;
    terminate_ = true;
}

bool LifeCycle::takeExitRequest() noexcept {
    if (stopRequested_ && running_ && !exitRequested_) {
        exitRequested_ = true;
        return true;
    }
    return false;
}

bool LifeCycle::rendering() const noexcept {
    return running_ && !stopRequested_ && (phase_ == Phase::Visible || phase_ == Phase::Focused);
}

bool LifeCycle::focused() const noexcept {
    return running_ && !stopRequested_ && phase_ == Phase::Focused;
}

Status LifeCycle::status() const {
    if (stopRequested_) return {State::Stopping, "Avslutar VR."};
    if (!running_) {
        return {State::Waiting, phase_ == Phase::Idle ? "Väntar på att OpenXR-runtimen ska starta sessionen."
                                                      : "Startar sessionen."};
    }
    switch (phase_) {
    case Phase::Focused: return {State::Running, "VR körs."};
    case Phase::Visible: return {State::Running, "VR körs utan fokus (en meny eller dashboard har fokus)."};
    default:             return {State::Waiting, "Väntar på att headsetet ska visa bilden."};
    }
}

Status LifeCycle::finalStatus() const {
    if (!failure_.empty()) return {State::Failed, failure_};
    return {State::Stopped, "VR stoppad."};
}

// ------------------------------------------------------------------ ÖVERLÄMNINGEN

void JobGate::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
}

bool JobGate::stopped() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stop_;
}

void JobGate::sleepFor(std::chrono::milliseconds time) {
    std::unique_lock<std::mutex> lock(mutex_);
    wake_.wait_for(lock, time, [&] { return stop_; });
}

bool JobGate::arm(std::uint64_t& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_ || state_ != State::Idle) return false;
    id = ++id_;
    state_ = State::Posted;
    return true;
}

void JobGate::abort(std::uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == State::Posted && id == id_) state_ = State::Idle;
}

JobGate::Await JobGate::await(std::uint64_t id) {
    std::unique_lock<std::mutex> lock(mutex_);
    wake_.wait(lock, [&] { return (state_ == State::Done && id == id_) || (stop_ && state_ == State::Posted); });
    const bool done = state_ == State::Done;
    state_ = State::Idle;   // kvitterat, eller avbrutet: ett sent begin(id) får inte köra
    return done ? Await::Done : Await::Cancelled;
}

bool JobGate::begin(std::uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_ || state_ != State::Posted || id != id_) return false;
    state_ = State::Running;
    return true;
}

void JobGate::finish(std::uint64_t id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::Running || id != id_) return;
        state_ = State::Done;
    }
    wake_.notify_all();
}

Run::Run() = default;
Run::~Run() = default;

namespace {

constexpr unsigned kDesktopHz = 30;   // skrivbordets takt medan en session kör
// Längsta tid GPU:n får på sig att komma förbi markören före rivning. Ett
// grafikkort som vanligen är klart på millisekunder och som grafikdrivrutinen
// själv återställer efter ett par sekunder har på fem sekunder antingen blivit
// klart eller hängt sig; därefter rivs inte swapchain-bilderna (se
// `Worker::tearDownUi`).
constexpr auto kGpuWait = std::chrono::seconds(5);
// Tiden mellan två frågor till GPU:n medan avvecklingen väntar (timerns golv).
constexpr int kTeardownPollMs = 10;
// Försök att lämna en bildruta till pumpen när den ännu inte tagit emot den förra.
constexpr int kPostAttempts = 100;

// Levererar ett läge i UI-tråden: uppdaterar `state` och anropar onStatus.
void deliver(Shared& shared, const Status& status) {
    shared.state = status.state;
    if (shared.callbacks.onStatus) shared.callbacks.onStatus(status);
}

std::string hex64(std::uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

// Kortets namn för ett LUID-tal ur kortlistan, annars bara id:t.
std::string adapterLabel(const std::vector<gfx::GpuAdapter>& adapters, std::uint64_t localId) {
    for (const gfx::GpuAdapter& adapter : adapters) {
        if (adapter.localAdapterId && *adapter.localAdapterId == localId) {
            return "\"" + adapter.name + "\" (" + hex64(localId) + ")";
        }
    }
    return "okänt kort (" + hex64(localId) + ")";
}

// UI-trådens kontroll av att Canvas-renderarens enhet är den runtimen kräver:
// D3D11, rätt kort och en tillräcklig funktionsnivå. Tom text = godkänd.
std::string verifyDevice(const CanvasBridge& bridge, std::uint64_t wantedId, std::uint32_t minFeatureLevel) {
    const gfx::NativeDevice device = bridge.nativeDevice();
    if (device.api != gfx::NativeGraphicsApi::D3D11 || device.device == nullptr) {
        return "Canvas ritar inte med D3D11, som headsetets runtime kräver.";
    }
    if (device.localAdapterId != wantedId) {
        const std::vector<gfx::GpuAdapter> adapters = gfx::gpuAdapters();
        return "Canvas ritar på fel grafikkort: headsetets runtime kräver " + adapterLabel(adapters, wantedId) +
               ", men fönstret ritar på " + adapterLabel(adapters, device.localAdapterId) +
               ". Välj kortet före appens första fönster med gfx::setGpuPreference("
               "gfx::GpuPreference::HighPerformance) eller gfx::setGpuAdapter.";
    }
    if (device.featureLevel < minFeatureLevel) {
        char text[96];
        std::snprintf(text, sizeof(text),
                      "Grafikenhetens funktionsnivå (0x%x) är lägre än den headsetet kräver (0x%x).",
                      static_cast<unsigned>(device.featureLevel), static_cast<unsigned>(minFeatureLevel));
        return text;
    }
    return {};
}

// Visar UI-trådens sida (målen, passet och bildkällan) för pumpen och GPU:n:
// ingen ny bildruta, en markör efter det som skickats, och en tidsgräns för
// GPU:n. Sedan avgör `advanceUiTeardown`. Anropas i UI-tråden; blockerar aldrig.
void beginUiTeardown(Run& run) {
    if (run.tornDown || run.teardownBegun) return;
    run.teardownBegun = true;
    run.gpuDeadline = std::chrono::steady_clock::now() + kGpuWait;
    if (!run.sourceUnregistered) {
        run.sourceUnregistered = true;
        glaze::detail::unregisterExternalFrameSource(run.source);
    }
    if (run.bridge) run.bridge->beginRelease();
}

// Släpper UI-trådens sida och meddelar XR-tråden hur det gick. Den första
// avgörelsen gäller. Anropas i UI-tråden.
void finishUiTeardown(Run& run, TeardownEnd end, const std::string& failure) {
    run.poll.reset();   // inga fler kontroller
    if (!run.tornDown) {
        if (run.bridge) run.bridge->release();
        run.tornDown = true;
    }
    {
        std::lock_guard<std::mutex> lock(run.teardown.mutex);
        if (!run.teardown.done) {
            run.teardown.done = true;
            run.teardown.end = end;
            run.teardown.failure = failure;
        }
    }
    run.teardown.wake.notify_all();
}

// Frågar GPU:n EN gång om den kommit förbi markören. Returnerar false medan
// den fortfarande arbetar och tidsgränsen inte gått ut. Annars är avvecklingen
// avgjord och UI-sidan släppt: Clean när GPU:n bekräftat, DeviceLost när
// enheten förlorats (inget arbete återstår), GpuTimeout när tiden gått ut utan
// besked. Anropas i UI-tråden.
bool advanceUiTeardown(Run& run) {
    if (run.tornDown) return true;
    TeardownEnd end = TeardownEnd::Clean;
    std::string failure;
    if (run.bridge) {
        std::string error;
        switch (run.bridge->pollRelease(error)) {
        case gfx::GpuCompletionState::Complete:
            break;
        case gfx::GpuCompletionState::Failed:
            end = TeardownEnd::DeviceLost;
            failure = "Grafikenheten förlorades medan VR avvecklades" +
                      (error.empty() ? std::string(".") : ": " + error);
            break;
        case gfx::GpuCompletionState::Pending:
            if (std::chrono::steady_clock::now() < run.gpuDeadline) return false;
            end = TeardownEnd::GpuTimeout;
            failure = "Grafikkortet blev inte klart med ritningen inom " +
                      std::to_string(std::chrono::duration_cast<std::chrono::seconds>(kGpuWait).count()) +
                      " sekunder när VR avvecklades. VR-sessionens bilder frigavs inte; "
                      "starta om appen om VR inte går att starta igen.";
            break;
        }
    }
    finishUiTeardown(run, end, failure);
    return true;
}

// Avvecklingen i UI-tråden mellan pumpens uppdrag: väntar GPU:n fortfarande
// schemaläggs nästa fråga med en timer, så att pumpen får sova emellan i
// stället för att få ett nytt uppdrag direkt. Timern håller bara svaga
// referenser och städas bort av `finishUiTeardown`.
void pollUiTeardown(const std::shared_ptr<Shared>& shared, const std::shared_ptr<Run>& run) {
    if (advanceUiTeardown(*run)) return;
    auto timer = std::make_unique<glaze::Timer>();
    timer->setInterval(kTeardownPollMs);
    timer->onTick = [weakShared = std::weak_ptr<Shared>(shared), weakRun = std::weak_ptr<Run>(run)] {
        const std::shared_ptr<Shared> sh = weakShared.lock();
        const std::shared_ptr<Run> r = weakRun.lock();
        if (!sh || !r || !sh->alive) return;   // ~Session avvecklar själv
        advanceUiTeardown(*r);
    };
    timer->start();
    if (!timer->isRunning()) {
        // Operativsystemet vägrade timern: vänta in avgörandet här i stället.
        // Det tar högst tidsgränsen och ger XR-tråden sitt besked.
        while (!advanceUiTeardown(*run)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return;
    }
    run->poll = std::move(timer);
}

// En ägande referens på grafikenheten. Tas i UI-tråden när enheten hämtas ur
// Canvas-renderaren och hålls tills XR-sessionen är riven, så att XR-tråden
// aldrig använder en enhet som fönstret hunnit frigöra. COM-räknaren är
// trådsäker, så referensen får släppas i vilken tråd som helst.
class DeviceRef {
public:
    DeviceRef() = default;
    ~DeviceRef() { reset(); }
    DeviceRef(const DeviceRef&)            = delete;
    DeviceRef& operator=(const DeviceRef&) = delete;

    void acquire(void* device) {
        reset();
        device_ = static_cast<ID3D11Device*>(device);
        if (device_) device_->AddRef();
    }
    void* get() const noexcept { return device_; }
    // Lämnar referensen kvar för alltid (när något annat kan använda enheten).
    void leak() noexcept { device_ = nullptr; }

private:
    void reset() noexcept {
        if (device_) device_->Release();
        device_ = nullptr;
    }
    ID3D11Device* device_ = nullptr;
};

// UI-trådens del av en bildruta: läser lägen och reglage vid visningstiden
// och ritar i bilderna.
class UiFrameContent final : public FrameContent {
public:
    UiFrameContent(Shared& shared, Run& run) : shared_(shared), run_(run) {}

    FillResult fill(const FrameTicket& ticket, std::array<EyeLayer, 2>& layers, std::string& error) override {
        if (!run_.context || !run_.actions || !run_.bridge) {
            error = "Sessionens objekt finns inte längre.";
            return FillResult::Failed;
        }
        XrSessionApi& api = run_.context->api();
        LocatedViews views;
        PoseSample head;
        FrameRequest request;
        if (!api.locateViews(ticket.predictedDisplayTime, views, error) ||
            !api.locateHead(ticket.predictedDisplayTime, head, error) ||
            !run_.actions->sync(ticket.predictedDisplayTime, ticket.focused, ticket.focusEpoch,
                                request.controllers, request.events, error)) {
            return FillResult::Failed;
        }
        if (run_.context->extensions().handTracking) {
            run_.context->locateHands(ticket.predictedDisplayTime, request.hands);
        }
        // Uppdateringsfrekvensen appen begärt i `Options`, när sessionen kör.
        if (!run_.refreshRateRequested) {
            run_.refreshRateRequested = true;
            if (run_.options.refreshRate > 0.0f) run_.context->requestRefreshRate(run_.options.refreshRate);
        }

        request.sequence = ticket.sequence;
        request.deltaSeconds = ticket.deltaSeconds;
        request.focused = ticket.focused;
        request.focusEpoch = ticket.focusEpoch;
        request.headValid = head.valid;
        request.head = head.pose;
        request.floorHeight = run_.context->floorHeight();
        request.imageIndex = ticket.imageIndex;
        for (std::size_t eye = 0; eye < 2; ++eye) {
            request.eyes[eye] = EyeView{views.valid, views.poses[eye], views.fov[eye]};
        }

        const FrameResult result = run_.bridge->runFrame(request, shared_.callbacks);
        if (!result.error.empty()) {
            error = result.error;
            return FillResult::Failed;
        }
        if (!result.rendered) return FillResult::NoLayer;
        for (std::size_t eye = 0; eye < 2; ++eye) {
            layers[eye] = EyeLayer{views.poses[eye], views.fov[eye]};
        }
        return FillResult::Layer;
    }

private:
    Shared& shared_;
    Run&    run_;
};

// Bildrutejobbet i UI-tråden. Rör inget efter att sessionen förstörts eller
// uppdraget avbrutits.
void runFrameJob(const std::shared_ptr<Shared>& shared, const std::shared_ptr<Run>& run,
                 std::uint64_t id, const FrameTicket& ticket) {
    if (!shared->alive) return;
    if (!run->gate.begin(id)) return;

    SubmitResult result = SubmitResult::Failed;
    std::string error;
    try {
        if (!run->context) {
            error = "Sessionens objekt finns inte längre.";
        } else {
            UiFrameContent content(*shared, *run);
            result = completeFrame(run->context->api(), ticket, content, error);
        }
    } catch (const std::exception& e) {
        error = std::string("Internt fel i bildrutan: ") + e.what();
    } catch (...) {
        error = "Internt fel i bildrutan.";
    }
    run->lastResult = result;
    run->lastError = std::move(error);
    run->gate.finish(id);
}

// XR-trådens ände av överlämningen: lämnar bildrutan till pumpen som ett
// jobb och väntar på kvittot.
class UiFrameSink final : public FrameSink {
public:
    UiFrameSink(std::shared_ptr<Shared> shared, std::shared_ptr<Run> run)
        : shared_(std::move(shared)), run_(std::move(run)) {}

    SubmitResult submit(const FrameTicket& ticket, std::string& error) override {
        std::uint64_t id = 0;
        if (!run_->gate.arm(id)) return SubmitResult::Cancelled;

        const auto job = [shared = shared_, run = run_, id, ticket] { runFrameJob(shared, run, id, ticket); };
        bool posted = false;
        for (int attempt = 0; attempt < kPostAttempts && !posted; ++attempt) {
            posted = glaze::detail::postExternalFrame(run_->source, job);
            if (!posted) {
                if (run_->gate.stopped()) break;
                run_->gate.sleepFor(std::chrono::milliseconds(1));
            }
        }
        if (!posted) {
            run_->gate.abort(id);
            if (run_->gate.stopped()) return SubmitResult::Cancelled;
            error = "Pumpen tog inte emot bildrutan (bildkällan saknas eller är upptagen).";
            return SubmitResult::Failed;
        }
        if (run_->gate.await(id) == JobGate::Await::Cancelled) return SubmitResult::Cancelled;
        error = run_->lastError;
        return run_->lastResult;
    }

private:
    std::shared_ptr<Shared> shared_;
    std::shared_ptr<Run>    run_;
};

// XR-trådens arbete för en start. Äger alla OpenXR-anrop utom bildrutans
// (som UI-tråden gör medan XR-tråden väntar) och rör aldrig Canvas eller
// grafikenheten: allt som kräver dem går som ett uppdrag till UI-tråden, och
// trådens väntan på det avbryts av stoppflaggan.
class Worker {
public:
    Worker(std::shared_ptr<Shared> shared, std::shared_ptr<Run> run)
        : shared_(std::move(shared)), run_(std::move(run)) {}

    void execute() {
        Status outcome;
        try {
            outcome = runAll();
        } catch (const std::exception& e) {
            outcome = {State::Failed, std::string("Internt fel i VR-tråden: ") + e.what()};
        } catch (...) {
            outcome = {State::Failed, "Internt fel i VR-tråden."};
        }
        // En avveckling som inte blev ren gör sluttillståndet Failed, också
        // när sessionen i övrigt slutade i god ordning.
        if (teardownEnd_ != TeardownEnd::Clean) {
            outcome = outcome.state == State::Failed
                          ? Status{State::Failed, outcome.message + " " + teardownFailure_}
                          : Status{State::Failed, teardownFailure_};
        }
        postTerminal(outcome);
    }

private:
    void postStatus(const Status& status) {
        if (hasPosted_ && status.state == lastPosted_.state && status.message == lastPosted_.message) return;
        hasPosted_ = true;
        lastPosted_ = status;
        glaze::app().post([shared = shared_, run = run_, status] {
            if (!shared->alive || shared->run != run) return;
            deliver(*shared, status);
        });
    }

    // Det sista läget: tråden är klar med allt utom att returnera. UI-tråden
    // joinar den och levererar sedan Stopped eller Failed.
    void postTerminal(const Status& status) {
        glaze::app().post([shared = shared_, run = run_, status] {
            if (!shared->alive || shared->run != run) return;
            if (shared->worker.joinable()) shared->worker.join();
            shared->run.reset();
            deliver(*shared, status);
        });
    }

    enum class UiCall { Done, Cancelled, Failed };

    // Kör `work` i UI-tråden och väntar på det. Cancelled när ett stopp kom
    // innan UI-tråden började; då körs det aldrig. Failed med `work`s text.
    UiCall callUi(std::function<std::string()> work, std::string& error) {
        std::uint64_t id = 0;
        if (!run_->gate.arm(id)) return UiCall::Cancelled;
        glaze::app().post([shared = shared_, run = run_, id, work = std::move(work)] {
            if (!shared->alive || !run->gate.begin(id)) return;
            std::string result;
            try {
                result = work();
            } catch (const std::exception& e) {
                result = std::string("Internt fel: ") + e.what();
            } catch (...) {
                result = "Internt fel.";
            }
            run->lastError = std::move(result);
            run->gate.finish(id);
        });
        if (run_->gate.await(id) == JobGate::Await::Cancelled) return UiCall::Cancelled;
        error = run_->lastError;
        return error.empty() ? UiCall::Done : UiCall::Failed;
    }

    // Ber UI-tråden riva sin sida (bildkällan, målen, passet) och väntar tills
    // det är gjort, så att swapchains inte rivs under GPU-arbete eller
    // importerade mål. Väntan har ingen egen tidsgräns: UI-tråden avgör den
    // med GPU:ns bekräftelse, ett enhetsfel eller sin tidsgräns för GPU:n, och
    // ~Session gör avvecklingen själv om UI-tråden aldrig hinner pumpa.
    //
    // Efter väntan gäller `teardownEnd_`. Clean: GPU:n har bekräftat och
    // sessionen rivs som vanligt. DeviceLost: enheten är borta och inget
    // arbete återstår, så sessionen rivs ändå men sluttillståndet blir
    // Failed. GpuTimeout: GPU:n kan fortfarande läsa swapchain-bilderna, så
    // sessionen och dess swapchains rivs INTE (objekten lämnas kvar till
    // processens slut, också enhetsreferensen) och sluttillståndet blir
    // Failed; en riven bild under pågående ritning vore värre än en läcka.
    void tearDownUi() {
        {
            // UI-trådens anrop utanför bildrutan (vibration, frekvens) når
            // inte objekten efter det här.
            std::lock_guard<std::mutex> lock(run_->apiMutex);
            run_->context = nullptr;
            run_->actions = nullptr;
        }
        glaze::app().post([shared = shared_, run = run_] {
            if (!shared->alive) return;
            beginUiTeardown(*run);
            pollUiTeardown(shared, run);
        });
        std::unique_lock<std::mutex> lock(run_->teardown.mutex);
        run_->teardown.wake.wait(lock, [&] { return run_->teardown.done; });
        teardownEnd_ = run_->teardown.end;
        teardownFailure_ = run_->teardown.failure;
    }

    Status runAll() {
        // Deklarationsordningen är avvecklingsordningen baklänges: UI-sidan
        // rivs först, sedan kontrollerna, sist sessionen och instansen.
        // Enhetsreferensen deklareras först och släpps därför sist.
        const auto device = std::make_shared<DeviceRef>();
        std::unique_ptr<OpenXrContext> context;
        std::unique_ptr<Actions> actions;
        struct UiGuard {
            Worker& worker;
            std::unique_ptr<OpenXrContext>& context;
            DeviceRef& device;
            ~UiGuard() {
                worker.tearDownUi();
                if (worker.teardownEnd_ == TeardownEnd::GpuTimeout) {
                    // GPU:n kan fortfarande läsa bilderna: sessionen rivs inte.
                    (void)context.release();
                    device.leak();
                }
            }
        } guard{*this, context, *device};

        if (run_->gate.stopped()) return {State::Stopped, "Starten avbröts."};

        std::string error;
        std::unique_ptr<XrRuntime> runtime = XrRuntime::open(run_->options, error);
        if (!runtime) return {State::Failed, error};

        // Kortet och enheten kontrolleras i UI-tråden, och enheten hämtas
        // därifrån med en ägande referens (`device`).
        const std::uint64_t wantedId = packLuid(runtime->graphicsRequirements().adapterLuid);
        const std::uint32_t minLevel = static_cast<std::uint32_t>(runtime->graphicsRequirements().minFeatureLevel);
        switch (callUi([run = run_, device, wantedId, minLevel]() -> std::string {
                           if (!run->bridge) return "Passet är redan avvecklat.";
                           std::string problem = verifyDevice(*run->bridge, wantedId, minLevel);
                           if (problem.empty()) device->acquire(run->bridge->nativeDevice().device);
                           return problem;
                       }, error)) {
        case UiCall::Cancelled: return {State::Stopped, "Starten avbröts."};
        case UiCall::Failed:    return {State::Failed, error};
        case UiCall::Done:      break;
        }

        context = OpenXrContext::create(std::move(runtime), device->get(), run_->options, error);
        if (!context) return {State::Failed, error};
        actions = Actions::create(*context, run_->options, error);
        if (!actions) return {State::Failed, error};
        {
            std::lock_guard<std::mutex> lock(run_->apiMutex);
            run_->context = context.get();
            run_->actions = actions.get();
        }

        // Swapchain-bilderna blir mål i Canvas-renderaren (UI-tråden).
        switch (callUi([run = run_, sets = context->eyeImages()]() -> std::string {
                           if (!run->bridge) return "Passet är redan avvecklat.";
                           std::string problem;
                           run->bridge->importImages(sets, problem);
                           return problem;
                       }, error)) {
        case UiCall::Cancelled: return {State::Stopped, "Starten avbröts."};
        case UiCall::Failed:    return {State::Failed, error};
        case UiCall::Done:      break;
        }

        UiFrameSink sink(shared_, run_);
        LoopHooks hooks;
        hooks.stopRequested = [run = run_] { return run->gate.stopped(); };
        hooks.sleep = [run = run_](std::chrono::milliseconds time) { run->gate.sleepFor(time); };
        hooks.onStatus = [this](const Status& status) { postStatus(status); };
        hooks.onInteractionProfileChanged = [&actions] { actions->profileChanged(); };
        SessionLoop loop(context->api(), sink, std::move(hooks));
        return loop.run();
    }

    std::shared_ptr<Shared> shared_;
    std::shared_ptr<Run>    run_;
    Status                  lastPosted_;
    bool                    hasPosted_ = false;
    TeardownEnd             teardownEnd_ = TeardownEnd::Clean;   // XR-tråden: hur UI-sidan avvecklades
    std::string             teardownFailure_;
};

} // namespace

} // namespace detail

// -------------------------------------------------------------------- SESSION

struct Session::Impl {
    std::shared_ptr<detail::Shared> shared = std::make_shared<detail::Shared>();
};

Session::Session(glaze::Canvas& canvas, Callbacks callbacks) : impl_(std::make_unique<Impl>()) {
    impl_->shared->canvas = &canvas;
    impl_->shared->callbacks = std::move(callbacks);
}

Session::~Session() {
    detail::Shared& shared = *impl_->shared;
    // Uppdrag och lägen som körs efter detta rör varken Canvas eller callbacks.
    shared.alive = false;
    if (shared.run) {
        detail::Run& run = *shared.run;
        run.gate.stop();
        // Här, och bara här, får UI-tråden vänta: pumpen kan vara på väg
        // ned, och Canvas ska överleva målen och passet.
        detail::beginUiTeardown(run);
        // Bekräftelsen, enhetsfelet eller tidsgränsen avgör; XR-tråden väntar
        // på avgörandet innan den river sina objekt.
        while (!detail::advanceUiTeardown(run)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (shared.worker.joinable()) shared.worker.join();
}

void Session::start(const Options& options) {
    detail::Shared& shared = *impl_->shared;
    const State current = shared.state;
    if (current != State::Stopped && current != State::Failed) return;
    if (shared.worker.joinable()) shared.worker.join();   // en tråd som redan levererat sitt slut

    auto run = std::make_shared<detail::Run>();
    run->options = options;
    std::string error;
    run->bridge = detail::CanvasBridge::create(*shared.canvas, options, error);
    if (!run->bridge) {
        detail::deliver(shared, {State::Failed, error});
        return;
    }
    run->source = glaze::detail::registerExternalFrameSource(detail::kDesktopHz);
    if (run->source == 0) {
        run->bridge.reset();
        detail::deliver(shared, {State::Failed, "Pumpen tog inte emot bildkällan."});
        return;
    }
    shared.run = run;
    detail::deliver(shared, {State::Starting, "Startar VR."});

    // Tråden får egna referenser; den rör bara Run, aldrig Canvas.
    shared.worker = std::thread([worker = detail::Worker(impl_->shared, run)]() mutable { worker.execute(); });
}

void Session::requestStop() {
    detail::Shared& shared = *impl_->shared;
    const State current = shared.state;
    if (!shared.run || current == State::Stopped || current == State::Failed) return;
    shared.run->gate.stop();
}

State Session::state() const noexcept { return impl_->shared->state; }

Anchor Session::anchor() const {
    const detail::Run* run = impl_->shared->run.get();
    return run && run->bridge ? run->bridge->anchor() : Anchor{};
}

void Session::setAnchor(const Anchor& anchor) {
    if (detail::Run* run = impl_->shared->run.get(); run && run->bridge) run->bridge->setAnchor(anchor);
}

void Session::recenter(double yawDegrees) {
    if (detail::Run* run = impl_->shared->run.get(); run && run->bridge) run->bridge->recenter(yawDegrees);
}

bool Session::teleportTo(Vec3 feet, std::optional<double> yawDegrees) {
    const detail::Shared& shared = *impl_->shared;
    const State current = shared.state;
    if (current == State::Stopped || current == State::Failed || current == State::Stopping) return false;
    detail::Run* run = shared.run.get();
    return run && run->bridge && run->bridge->teleportTo(feet, yawDegrees);
}

void Session::setLocomotion(const Locomotion& locomotion) {
    if (detail::Run* run = impl_->shared->run.get(); run && run->bridge) run->bridge->setLocomotion(locomotion);
}

bool Session::vibrate(Hand hand, double amplitude, double seconds, double frequencyHz) {
    detail::Run* run = impl_->shared->run.get();
    if (!run) return false;
    std::lock_guard<std::mutex> lock(run->apiMutex);
    return run->actions && run->actions->vibrate(hand, amplitude, seconds, frequencyHz);
}

void Session::stopVibration(Hand hand) {
    detail::Run* run = impl_->shared->run.get();
    if (!run) return;
    std::lock_guard<std::mutex> lock(run->apiMutex);
    if (run->actions) run->actions->stopVibration(hand);
}

std::vector<float> Session::refreshRates() const {
    detail::Run* run = impl_->shared->run.get();
    if (!run) return {};
    std::lock_guard<std::mutex> lock(run->apiMutex);
    return run->context ? run->context->refreshRates() : std::vector<float>{};
}

bool Session::setRefreshRate(float hz) {
    detail::Run* run = impl_->shared->run.get();
    if (!run) return false;
    std::lock_guard<std::mutex> lock(run->apiMutex);
    return run->context && run->context->requestRefreshRate(hz);
}

std::vector<glaze::HitResult> Session::castRay(const glaze::Ray3& ray, const glaze::HitOptions& options) const {
    const detail::Run* run = impl_->shared->run.get();
    return run && run->bridge ? run->bridge->castRay(ray, options) : std::vector<glaze::HitResult>{};
}

} // namespace glaze::openxr
