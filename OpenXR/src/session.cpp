#include <glaze_openxr/session.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

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

Run::Run() = default;
Run::~Run() = default;

namespace {

// Tid utan svar från runtimen efter xrRequestExitSession innan sessionen
// rivs ned ändå.
constexpr auto kExitTimeout = std::chrono::seconds(5);
// Tid mellan händelsekontrollerna medan sessionen inte kör bildrutor.
constexpr auto kIdlePoll = std::chrono::milliseconds(10);
constexpr double kMaxDeltaSeconds = 0.1;

// Levererar ett läge i UI-tråden: uppdaterar `state` och anropar onStatus.
void deliver(Shared& shared, const Status& status) {
    shared.state = status.state;
    if (shared.callbacks.onStatus) shared.callbacks.onStatus(status);
}

// Ett uppdrag i UI-tråden: kör en bildruta mot Canvas och lämnar resultatet
// till XR-tråden. Rör inget efter att sessionen förstörts eller stoppats.
void runJob(const std::shared_ptr<Shared>& shared, const std::shared_ptr<Run>& run,
            std::uint64_t id, const FrameRequest& request) {
    if (!shared->alive) return;
    {
        std::lock_guard<std::mutex> lock(run->mutex);
        if (run->stopRequested || run->abandoned || run->jobId != id) return;
    }
    FrameResult result = run->bridge->runFrame(request, shared->callbacks);
    {
        std::lock_guard<std::mutex> lock(run->mutex);
        if (run->jobId == id) {
            run->result = std::move(result);
            run->doneId = id;
        }
    }
    run->wake.notify_all();
}

// XR-trådens arbete för en start. Äger alla OpenXR-anrop och D3D11-kontexten
// och rör aldrig Canvas: allt som kräver Canvas går som ett uppdrag till
// UI-tråden, och trådens väntan på det avbryts av stoppflaggan.
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
        postTerminal(outcome);
    }

private:
    bool stopFlag() {
        std::lock_guard<std::mutex> lock(run_->mutex);
        return run_->stopRequested;
    }

    // Väntar högst `time`, avbruten av stoppflaggan.
    void sleepInterruptible(std::chrono::milliseconds time) {
        std::unique_lock<std::mutex> lock(run_->mutex);
        run_->wake.wait_for(lock, time, [&] { return run_->stopRequested; });
    }

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

    Status runAll() {
        if (stopFlag()) return {State::Stopped, "Starten avbröts."};

        Status outcome{State::Stopped, "VR stoppad."};
        std::string error;
        std::unique_ptr<OpenXrContext> context = OpenXrContext::create(run_->options.resolutionScale, error);
        if (!context) {
            outcome = {State::Failed, error};
        } else {
            std::unique_ptr<Actions> actions = Actions::create(*context, run_->options, error);
            if (!actions) {
                outcome = {State::Failed, error};
            } else {
                outcome = loop(*context, *actions);
            }
            actions.reset();   // före sessionen
        }
        context.reset();
        return outcome;
    }

    // Huvudloopen: händelser, tillståndsmaskin och bildrutor tills sessionen
    // är avvecklad. Returnerar sluttillståndet.
    Status loop(OpenXrContext& cx, Actions& actions) {
        LifeCycle life;
        std::string error;
        std::chrono::steady_clock::time_point exitDeadline{};
        bool exitSent = false;
        XrTime previousTime = 0;
        std::uint64_t sequence = 0;
        std::uint64_t releasedEpoch = 0;

        postStatus(life.status());

        while (!life.terminate()) {
            if (!life.stopRequested() && stopFlag()) life.requestStop();

            OpenXrContext::Events events;
            if (!cx.pollEvents(events, error)) {
                life.fail(error);
                break;
            }
            for (const Phase phase : events.phases) {
                const Step step = life.onPhase(phase);
                if (step == Step::BeginSession) {
                    if (!cx.beginSession(error)) { life.fail(error); break; }
                    life.sessionBegun();
                } else if (step == Step::EndSession) {
                    if (!cx.endSession(error)) { life.fail(error); break; }
                    life.sessionEnded();
                }
            }
            if (events.referenceSpaceChanged) {
                life.requestStop("Referensrummet ändrades (till exempel efter en ny rumsinställning). "
                                 "Starta VR igen.");
            }
            if (events.instanceLost) {
                life.fail("OpenXR-runtimen stängdes eller förlorades. Starta om runtimen och VR.");
            }
            if (life.terminate()) break;
            // Fokus försvann: avtryckarens hysteres gäller inte längre, också
            // om ingen bildruta ritats sedan dess.
            if (life.focusEpoch() != releasedEpoch) {
                actions.release();
                releasedEpoch = life.focusEpoch();
            }

            if (life.takeExitRequest()) {
                if (!cx.requestExitSession(error)) { life.fail(error); break; }
                exitSent = true;
                exitDeadline = std::chrono::steady_clock::now() + kExitTimeout;
            }
            if (exitSent && life.running() && std::chrono::steady_clock::now() > exitDeadline) {
                life.giveUp();   // runtimen avslutar inte; riv ned sessionen ändå
                break;
            }
            postStatus(life.status());

            if (!life.running()) {
                sleepInterruptible(kIdlePoll);
                continue;
            }
            if (!frame(cx, actions, life, previousTime, sequence)) break;
        }
        return life.finalStatus();
    }

    // En bildruta: wait, begin, läget och uppdraget till UI-tråden, upp-
    // laddning, end. Returnerar false efter ett fel (då är `life` felmarkerad).
    bool frame(OpenXrContext& cx, Actions& actions, LifeCycle& life,
               XrTime& previousTime, std::uint64_t& sequence) {
        std::string error;
        OpenXrContext::FrameTiming timing;
        if (!cx.waitFrame(timing, error) || !cx.beginFrame(error)) {
            life.fail(error);
            return false;
        }
        // Från och med nu måste bildrutan avslutas, också vid fel.
        const auto abort = [&](const std::string& message) {
            std::string ignored;
            cx.endFrameEmpty(timing.predictedDisplayTime, ignored);
            life.fail(message);
            return false;
        };

        const double delta = previousTime == 0
            ? 0.0
            : std::clamp(static_cast<double>(timing.predictedDisplayTime - previousTime) * 1e-9,
                         0.0, kMaxDeltaSeconds);
        previousTime = timing.predictedDisplayTime;

        if (!timing.shouldRender || !life.rendering()) {
            if (!cx.endFrameEmpty(timing.predictedDisplayTime, error)) {
                life.fail(error);
                return false;
            }
            return true;
        }

        FrameRequest request;
        request.sequence = ++sequence;
        request.deltaSeconds = delta;
        request.focused = life.focused();
        request.focusEpoch = life.focusEpoch();

        OpenXrContext::EyePoses eyes;
        PoseSample head;
        std::array<ControllerSample, 2> controllers;
        if (!cx.locateViews(timing.predictedDisplayTime, eyes, error) ||
            !cx.locateHead(timing.predictedDisplayTime, head, error) ||
            !actions.sync(timing.predictedDisplayTime, request.focused, controllers, error)) {
            return abort(error);
        }
        request.headValid = head.valid;
        request.head = head.pose;
        request.controllers = controllers;
        bool eyesUsable = eyes.valid;
        for (std::size_t eye = 0; eye < 2; ++eye) {
            const auto view = makeSymmetricView(eyes.fov[eye], cx.viewSizes()[eye], run_->options.resolutionScale);
            request.eyes[eye].valid = eyes.valid && view.has_value();
            request.eyes[eye].pose = eyes.poses[eye];
            if (view) request.eyes[eye].view = *view;
            eyesUsable = eyesUsable && view.has_value();
        }

        FrameResult result;
        if (!runOnUiThread(request, result)) {
            // Stopp begärt medan uppdraget väntade: bildrutan avslutas tom.
            if (!cx.endFrameEmpty(timing.predictedDisplayTime, error)) {
                life.fail(error);
                return false;
            }
            return true;
        }
        if (!result.error.empty()) return abort(result.error);

        if (eyesUsable && result.hasImages) {
            std::array<EyeSubmission, 2> submissions;
            for (std::size_t eye = 0; eye < 2; ++eye) {
                submissions[eye].rgba = result.images[eye].pixels.data();
                submissions[eye].width = result.images[eye].width;
                submissions[eye].height = result.images[eye].height;
                submissions[eye].pose = eyes.poses[eye];
                submissions[eye].fov = request.eyes[eye].view.submittedFov;
            }
            if (!cx.endFrameProjection(timing.predictedDisplayTime, submissions, error)) {
                life.fail(error);   // endFrameProjection har redan avslutat bildrutan
                return false;
            }
            return true;
        }
        if (!cx.endFrameEmpty(timing.predictedDisplayTime, error)) {
            life.fail(error);
            return false;
        }
        return true;
    }

    // Postar ETT uppdrag till UI-tråden och väntar på resultatet. Väntan
    // avbryts av stoppflaggan (då är returvärdet false och uppdraget
    // avstår om det inte redan körs).
    bool runOnUiThread(const FrameRequest& request, FrameResult& result) {
        std::uint64_t id = 0;
        {
            std::lock_guard<std::mutex> lock(run_->mutex);
            if (run_->stopRequested) return false;
            id = ++run_->jobId;
            run_->abandoned = false;
        }
        glaze::app().post([shared = shared_, run = run_, id, request] {
            runJob(shared, run, id, request);
        });

        std::unique_lock<std::mutex> lock(run_->mutex);
        run_->wake.wait(lock, [&] { return run_->stopRequested || run_->doneId == id; });
        if (run_->doneId != id) {
            run_->abandoned = true;
            return false;
        }
        result = std::move(run_->result);
        return true;
    }

    std::shared_ptr<Shared> shared_;
    std::shared_ptr<Run>    run_;
    Status                  lastPosted_;
    bool                    hasPosted_ = false;
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
        {
            std::lock_guard<std::mutex> lock(shared.run->mutex);
            shared.run->stopRequested = true;
        }
        shared.run->wake.notify_all();
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
    run->bridge = std::make_unique<detail::CanvasBridge>(*shared.canvas, options);
    shared.run = run;
    detail::deliver(shared, {State::Starting, "Startar VR."});

    // Tråden får egna referenser; den rör bara Run, aldrig Canvas.
    shared.worker = std::thread([worker = detail::Worker(impl_->shared, run)]() mutable { worker.execute(); });
}

void Session::requestStop() {
    detail::Shared& shared = *impl_->shared;
    const State current = shared.state;
    if (!shared.run || current == State::Stopped || current == State::Failed) return;
    {
        std::lock_guard<std::mutex> lock(shared.run->mutex);
        if (shared.run->stopRequested) return;
        shared.run->stopRequested = true;
    }
    shared.run->wake.notify_all();
}

State Session::state() const noexcept { return impl_->shared->state; }

} // namespace glaze::openxr
