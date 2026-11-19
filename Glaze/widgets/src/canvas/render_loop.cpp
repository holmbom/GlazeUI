// RITYTANS RENDERINGSTRÅD (render_loop.h).

#include "render_loop.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <utility>

#include <gfx/platform.h>
#include <gfx/renderer.h>

#include "../image_cache.h"
#include "glaze/app.h"
#include "glaze/widget.h"
#include "resources.h"

namespace glaze::detail {

namespace {

// Loopens löpnummer (render_loop.h `id`).
std::uint64_t nextLoopId()
{
    static std::atomic<std::uint64_t> n{0};
    return ++n;
}

// Skalan i trådens grund-Ctx — jobbet bär den riktiga.
constexpr float kUnitScale = 1.0f;

// Trådlägets brytare: miljövariabeln och värdet som stänger av.
constexpr const char* kThreadEnv = "GLAZE_CANVAS_THREAD";   // lang_gate:rad miljövariabelns namn
constexpr char        kThreadOff = '0';

std::atomic<bool>& threadingFlag()
{
    static std::atomic<bool> på = [] {
        const char* v = std::getenv(kThreadEnv);
        return !(v && v[0] == kThreadOff);
    }();
    return på;
}

} // namespace

bool canvasThreading()
{
    return threadingFlag().load();
}

void setCanvasThreading(bool on)
{
    threadingFlag().store(on);
}

namespace {
std::atomic<bool>& offscreenFlag()
{
    static std::atomic<bool> på{false};
    return på;
}
} // namespace

bool canvasThreadingOffscreen()
{
    return offscreenFlag().load();
}

void setCanvasThreadingOffscreen(bool on)
{
    offscreenFlag().store(on);
}

CanvasRenderLoop::CanvasRenderLoop() : id_(nextLoopId()) {}

std::unique_ptr<CanvasRenderLoop> CanvasRenderLoop::start(gfx::Renderer& window)
{
    std::unique_ptr<CanvasRenderLoop> loop(new CanvasRenderLoop());
    loop->thread_ = std::thread([p = loop.get(), &window] { p->run(window); });
    // Starten väntas in: kontexten skapas en gång per fönster, och svaret
    // avgör om fönstret ritar sina ritytor själv.
    bool ok = false;
    {
        std::unique_lock<std::mutex> l(loop->lock_);
        loop->started_.wait(l, [&] { return loop->ready_; });
        ok = loop->ok_;
    }
    if (!ok) {
        loop->thread_.join();
        loop->thread_ = std::thread();
        return nullptr;
    }
    return loop;
}

CanvasRenderLoop::~CanvasRenderLoop()
{
    {
        const std::lock_guard<std::mutex> l(lock_);
        // Fönstret håller inga bilder när tråden river dem.
        for (auto& [id, m] : boxes_)
            forgetLocked(*m);
        stop_ = true;
    }
    wakeup_.notify_all();
    if (thread_.joinable())
        thread_.join();
    // Beställningarna som inte hann ritas får sitt tomma svar — i ramkön,
    // som de ritade.
    for (Order& o : orders_)
        if (o.dropped)
            postFrameWork(std::move(o.dropped));
}

void CanvasRenderLoop::enqueue(Order o)
{
    {
        const std::lock_guard<std::mutex> l(lock_);
        orders_.push_back(std::move(o));
    }
    wakeup_.notify_one();
}

void CanvasRenderLoop::order(std::shared_ptr<const CanvasFrameJob> j,
                             gfx::Color background, BitmapDone done,
                             BitmapRedo redo)
{
    if (!done || !j)
        return;
    Order o;
    o.work = [j, background, done, redo](const Ctx& grund) -> std::function<void()> {
        // Framens början, som för en bild av ritytan — bilden ritas KOMPLETT
        // och läses tillbaka här, på tråden.
        grund.canvas->trim();
        grund.images->trim(j->memoryBudget, j->uploadBudget);
        grund.canvas->takeTextureRefused();
        auto bild = std::make_shared<std::optional<Bitmap>>(
            renderCanvasBitmap(*j, grund, background));
        const bool iUi = grund.canvas->takeTextureRefused();
        return [done, redo, bild, iUi] {
            if (iUi && redo)
                done(redo());
            else
                done(std::move(*bild));
        };
    };
    o.dropped = [done] { done(std::nullopt); };
    enqueue(std::move(o));
}

void CanvasRenderLoop::orderPage(std::shared_ptr<const PageJob> j, PageDone done)
{
    if (!done || !j)
        return;
    Order o;
    o.work = [j, done](const Ctx& grund) -> std::function<void()> {
        auto rgba     = std::make_shared<std::vector<std::uint8_t>>();
        bool komplett = true;
        grund.canvas->takeTextureRefused();
        const bool ok  = renderPageJob(*j, grund, *rgba, komplett);
        const bool iUi = grund.canvas->takeTextureRefused();
        return [done, ok, komplett, rgba, iUi] {
            done(ok, komplett, std::move(*rgba), iUi);
        };
    };
    o.dropped = [done] { done(false, false, {}, false); };
    enqueue(std::move(o));
}

// ------------------------------------------------------------ fönstertråden

void CanvasRenderLoop::forgetLocked(Mailbox& m)
{
    for (Slot& s : m.slots) {
        if (s.state != SlotState::Shown)
            continue;
        s.target->retire();
        s.image = nullptr;
        s.state = SlotState::Free;
    }
    m.forgotten = true;
    forgotten_  = true;
}

void CanvasRenderLoop::forgetDead()
{
    for (auto& [id, m] : boxes_)
        if (!m->forgotten && m->life.expired())
            forgetLocked(*m);
}

void CanvasRenderLoop::submit(CanvasFrameJob job, std::function<void()> wake)
{
    if (!job.scene)
        return;
    const std::uint64_t id = job.scene->id;
    // Det ersatta jobbet släpps utanför låset (det håller en scen).
    Pending gammalt;
    {
        const std::lock_guard<std::mutex> l(lock_);
        forgetDead();
        const auto it = queue_.find(id);
        if (it != queue_.end()) {
            gammalt    = std::move(it->second);
            it->second = Pending{std::move(job), std::move(wake)};
        } else {
            queue_.emplace(id, Pending{std::move(job), std::move(wake)});
            order_.push_back(id);
        }
    }
    wakeup_.notify_one();
}

CanvasRenderLoop::Shown CanvasRenderLoop::present(std::uint64_t canvas)
{
    const std::lock_guard<std::mutex> l(lock_);
    const auto                        it = boxes_.find(canvas);
    if (it == boxes_.end() || it->second->forgotten)
        return {};
    Mailbox& m     = *it->second;
    Slot*    visad = nullptr;
    Slot*    klar  = nullptr;
    for (Slot& s : m.slots) {
        if (s.state == SlotState::Shown)
            visad = &s;
        else if (s.state == SlotState::Ready)
            klar = &s;
    }
    Shown ut;
    if (klar) {
        // Den färdiga bilden tas — `acquire` väntar aldrig; lyckas den inte
        // står den förra kvar och bilden tas vid nästa fråga.
        if (gfx::Image* img = klar->target->acquire()) {
            if (visad) {
                visad->target->retire();
                visad->image = nullptr;
                visad->state = SlotState::Free;
            }
            klar->image = img;
            klar->state = SlotState::Shown;
            visad       = klar;
            ut.fresh    = true;
        }
    }
    if (!visad)
        return {};
    ut.image  = visad->image;
    ut.width  = visad->target->width();
    ut.height = visad->target->height();
    ut.result = &visad->result;
    return ut;
}

void CanvasRenderLoop::forget(std::uint64_t canvas)
{
    Pending väntande;
    {
        const std::lock_guard<std::mutex> l(lock_);
        const auto                        q = queue_.find(canvas);
        if (q != queue_.end()) {
            väntande = std::move(q->second);
            queue_.erase(q);
            order_.erase(std::remove(order_.begin(), order_.end(), canvas),
                         order_.end());
        }
        const auto it = boxes_.find(canvas);
        if (it != boxes_.end())
            forgetLocked(*it->second);
    }
    wakeup_.notify_one();
}

std::uint64_t CanvasRenderLoop::framesRendered() const
{
    const std::lock_guard<std::mutex> l(lock_);
    return frames_;
}

void CanvasRenderLoop::setPaused(bool on)
{
    {
        const std::lock_guard<std::mutex> l(lock_);
        paused_ = on;
    }
    wakeup_.notify_one();
}

// ------------------------------------------------------------------ tråden

void CanvasRenderLoop::run(gfx::Renderer& window)
{
    context_      = window.createSceneContext();
    const bool ok = context_ && context_->attachThread();
    {
        const std::lock_guard<std::mutex> l(lock_);
        ready_ = true;
        ok_    = ok;
    }
    started_.notify_all();
    if (!ok) {
        context_.reset();
        return;
    }

    {
        // Trådens resurser, i fönstrets ordning: bildcachen med liggaren och
        // ritytans resurser ovanpå den.
        gfx::Renderer&  r = context_->renderer();
        ImageCache      bilder(r);
        CanvasResources resurser(r, bilder.ledger());
        Ctx             grund{};
        grund.r      = &r;
        grund.scale  = kUnitScale;
        grund.images = &bilder;
        grund.canvas = &resurser;
        resurser.setThreaded(true);   // en RenderTexture hämtas inte här

        for (;;) {
            Pending                               p;
            Order                                 beställd;
            std::vector<std::unique_ptr<Mailbox>> glömda;
            bool                                  stopp = false;
            bool                                  jobb  = false;
            bool                                  bild  = false;
            std::uint64_t                         pågår = 0;   // ett pass att stega
            std::vector<std::uint64_t>            glömdaId;
            {
                std::unique_lock<std::mutex> l(lock_);
                wakeup_.wait(l, [&] {
                    return stop_ || forgotten_ ||
                           (!paused_ && (!order_.empty() || !orders_.empty() ||
                                         nextActive() != 0));
                });
                stopp = stop_;
                // De glömda brevlådorna — och vid stoppet alla — rivs här,
                // på tråden som äger bilderna.
                for (auto it = boxes_.begin(); it != boxes_.end();) {
                    if (stopp || it->second->forgotten) {
                        glömda.push_back(std::move(it->second));
                        glömdaId.push_back(it->first);
                        it = boxes_.erase(it);
                    } else {
                        ++it;
                    }
                }
                forgotten_ = false;
                if (!stopp && !paused_ && !orders_.empty()) {
                    // En beställd bild går före ritytornas — den har en
                    // väntande anropare.
                    beställd = std::move(orders_.front());
                    orders_.pop_front();
                    bild = true;
                } else if (!stopp && !paused_) {
                    // Det första jobbet som ska börja nu; ett som väntar på
                    // sin ritytas pågående pass (render_loop.h) står kvar.
                    for (auto it = order_.begin(); it != order_.end(); ++it) {
                        const auto q = queue_.find(*it);
                        if (!startsNow(*it, q->second.job))
                            continue;
                        p = std::move(q->second);
                        queue_.erase(q);
                        order_.erase(it);
                        jobb = true;
                        break;
                    }
                    if (!jobb)
                        pågår = nextActive();
                }
            }
            glömda.clear();
            // En glömd ritytas pass håller dess scen.
            for (std::uint64_t id : glömdaId)
                passes_.erase(id);
            if (stopp)
                break;
            if (bild && beställd.work) {
                std::function<void()> svar = beställd.work(grund);
                // Trådens kopior av appens svarsfunktion släpps FÖRE posten:
                // den sista kopian är då svarets, och den dör i fönstertråden.
                beställd = Order{};
                if (svar)
                    postFrameWorkFromThread(std::move(svar));
            }
            if (jobb)
                render(p, grund);
            else if (pågår != 0 && passes_.count(pågår) != 0)
                step(pågår, grund);
        }
        // Jobben som blev över håller scener — släppta före resurserna.
        passes_.clear();
        {
            const std::lock_guard<std::mutex> l(lock_);
            queue_.clear();
            order_.clear();
        }
    }
    context_->detachThread();
    context_.reset();
}

namespace {

constexpr double kMsPerSecond         = 1000.0;
constexpr double kVerticesPerTriangle = 3.0;
constexpr double kMillion             = 1.0e6;
constexpr double kBytesPerMb          = 1048576.0;
constexpr double kNsPerMs             = 1.0e6;

// MÄTRADEN: med `GLAZE_SCENE_LOG=1` i miljön skriver renderingstråden en rad
// per sekund — scenbilderna, och per bild tiden i CPU:n (förberedelsen och
// rit-anropen), väntan på grafikkortet, rit-anropen, trianglarna och det
// uppladdade.
bool sceneLogOn()
{
    const char* v = std::getenv("GLAZE_SCENE_LOG");   // lang_gate:rad miljövariabelns namn
    return v && *v && *v != '0';
}

std::atomic<std::int64_t>  g_cost[std::size_t(SceneCost::Count_)];
std::atomic<std::uint64_t> g_tally[std::size_t(SceneTally::Count_)];

double takeCostMs(SceneCost c)
{
    return static_cast<double>(g_cost[std::size_t(c)].exchange(0)) / kNsPerMs;
}

void sceneLog(std::chrono::steady_clock::time_point t0,
              std::chrono::steady_clock::time_point t1,
              std::chrono::steady_clock::time_point t2, const gfx::RenderStats& a,
              const gfx::RenderStats& b, const ThreadFrameProfile& faser)
{
    using ms = std::chrono::duration<double, std::milli>;
    struct Sum {
        std::chrono::steady_clock::time_point start{};
        int           frames = 0;
        double        cpu = 0.0, gpu = 0.0, prepare = 0.0, draw = 0.0;
        std::uint64_t draws = 0, vertices = 0, bytes = 0;
    };
    static Sum s;   // trådens egen — en renderingstråd per fönster
    if (s.frames == 0)
        s.start = t0;
    ++s.frames;
    s.cpu += ms(t1 - t0).count();
    s.gpu += ms(t2 - t1).count();
    s.prepare += faser.ms[std::size_t(ProfilePhase::CanvasPrepare)];
    s.draw += faser.ms[std::size_t(ProfilePhase::CanvasDraw)];
    s.draws += b.drawCalls - a.drawCalls;
    s.vertices += b.vertices - a.vertices;
    s.bytes += b.bytesUploaded - a.bytesUploaded;
    const double sek = ms(t2 - s.start).count() / kMsPerSecond;
    if (sek < 1.0)
        return;
    const double n = s.frames;
    const auto tal = [&](SceneTally t) {
        return static_cast<double>(g_tally[std::size_t(t)].exchange(0)) / n;
    };
    gfx::log("scen: residensen — kön %.1f (föremålen %.1f: kandidatfrågan %.1f varav "
             "indexet %.1f; urvalet %.1f), uppslag %.1f, bilder %.1f ms — %.0f "
             "kandidater, %.0f synliga, %.2f indexbyggen, %.0f insatta",
             takeCostMs(SceneCost::Queue) / n, takeCostMs(SceneCost::QObjects) / n,
             takeCostMs(SceneCost::Order) / n, takeCostMs(SceneCost::QIndex) / n,
             takeCostMs(SceneCost::QSelect) / n, takeCostMs(SceneCost::Lookup) / n,
             takeCostMs(SceneCost::Images) / n, tal(SceneTally::Candidates),
             tal(SceneTally::Visible), tal(SceneTally::Rebuilds),
             tal(SceneTally::Inserted));
    // Uppladdningens tre delar ligger INNE i residensens tid.
    const double pack = takeCostMs(SceneCost::Pack) / n,
                 upp  = takeCostMs(SceneCost::Upload) / n,
                 buf  = takeCostMs(SceneCost::Create) / n;
    gfx::log("scen: %.1f bilder/s — per bild: cpu %.1f ms (städning %.1f, förbered "
             "%.1f, rita %.1f) — förberedelsen: ombyggnad %.1f, "
             "körningar %.1f, residens %.1f (packning %.1f, uppladdning %.1f, "
             "buffertar %.1f), etiketter %.1f, klipp %.1f — grafikkortet %.1f ms, "
             "%.0f rit-anrop, %.2f M trianglar, %.2f MB uppladdat",
             n / sek, s.cpu / n, takeCostMs(SceneCost::Trim) / n, s.prepare / n,
             s.draw / n, takeCostMs(SceneCost::Rebuild) / n,
             takeCostMs(SceneCost::Runs) / n,
             takeCostMs(SceneCost::Residency) / n, pack, upp, buf,
             takeCostMs(SceneCost::Labels) / n, takeCostMs(SceneCost::Clips) / n,
             s.gpu / n, static_cast<double>(s.draws) / n,
             static_cast<double>(s.vertices) / n / kVerticesPerTriangle / kMillion,
             static_cast<double>(s.bytes) / n / kBytesPerMb);
    s = Sum{};
}

} // namespace

bool sceneLogging()
{
    static const bool på = sceneLogOn();
    return på;
}

namespace {
// Per tråd: varje renderingstråd mäter sina egna uppladdningar.
thread_local std::int64_t t_uploadNs = 0;
}

void noteUploadTime(std::chrono::steady_clock::duration d)
{
    t_uploadNs += std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
}

double takeUploadMs()
{
    const double ms = static_cast<double>(t_uploadNs) / kNsPerMs;
    t_uploadNs      = 0;
    return ms;
}

void sceneTally(SceneTally what, std::uint64_t n)
{
    if (sceneLogging())
        g_tally[std::size_t(what)].fetch_add(n);
}

void sceneCost(SceneCost what, std::chrono::steady_clock::duration d)
{
    g_cost[std::size_t(what)].fetch_add(
        std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
}

namespace {

// BILDTAKTENS TAL: stegets första kostnad och golvet (scene.h `FramePace` —
// trianglar, med tillägget per föremål), och hur mycket ett steg får krympa
// eller växa mot nästa.
constexpr double kPaceStartCost = 8.0e6;
constexpr double kPaceMinCost   = 1.0e6;
constexpr double kPaceShrinkMin = 0.7;
constexpr double kPaceGrowMax   = 1.2;
constexpr double kPaceUploadShare = 0.5;    // uppladdningarnas del av stegets tid
constexpr double kPaceMinShare    = 0.35;   // ritningens minsta del av bildens tid

bool sameVec(const gfx::Vec3d& a, const gfx::Vec3d& b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

} // namespace

bool CanvasRenderLoop::paced(const CanvasFrameJob& j)
{
    return j.frameBudgetMs > 0.0 && j.target3D && j.paceView.has_value();
}

CanvasRenderLoop::PassView CanvasRenderLoop::viewOf(const CanvasFrameJob& j)
{
    PassView v;
    if (!j.paceView)
        return v;
    v.valid   = true;
    v.view    = *j.paceView;
    v.width   = j.width;
    v.height  = j.height;
    v.scale   = j.scale;
    v.pxPerMm = j.pxPerMm;
    return v;
}

// Samma vy: ögat, riktningarna, projektionen och bildens mått. Klipplanen
// räknas inte — de följer scenens låda, som växer medan innehåll strömmar in.
bool CanvasRenderLoop::sameView(const PassView& a, const PassView& b)
{
    return a.valid && b.valid && sameVec(a.view.eye, b.view.eye) &&
           sameVec(a.view.forward, b.view.forward) && sameVec(a.view.up, b.view.up) &&
           a.view.projection == b.view.projection && a.view.fovY == b.view.fovY &&
           a.view.orthoHalfHeight == b.view.orthoHalfHeight &&
           a.width == b.width && a.height == b.height && a.scale == b.scale &&
           a.pxPerMm == b.pxPerMm;
}

bool CanvasRenderLoop::startsNow(std::uint64_t canvas, const CanvasFrameJob& j) const
{
    const auto it = passes_.find(canvas);
    if (it == passes_.end() || !it->second.active() || !paced(j))
        return true;
    return !sameView(it->second.view, viewOf(j));
}

std::uint64_t CanvasRenderLoop::nextActive()
{
    // Turordning mellan ritytorna: den första efter den som senast stegades.
    std::uint64_t första = 0;
    for (const auto& [id, pass] : passes_) {
        if (!pass.active())
            continue;
        if (id > lastStepped_)
            return id;
        if (första == 0)
            första = id;
    }
    return första;
}

void CanvasRenderLoop::render(Pending& p, const Ctx& grund)
{
    // ETT NYTT PASS (render_loop.h): med samma vy som ritytans förra ritas
    // det i ritordning, annars börjar det med påfyllningen.
    const std::uint64_t id   = p.job.scene->id;
    Pass&               pass = passes_[id];
    const PassView      vy   = viewOf(p.job);
    pass.stage  = sameView(pass.view, vy) ? FramePace::Order::Sequence
                                          : FramePace::Order::Priority;
    pass.view   = vy;
    pass.job    = std::move(p.job);
    pass.wake   = std::move(p.wake);
    pass.first  = true;
    pass.cursor = WorldPos{};
    if (++pass.epoch == 0) {
        pass.stamps.clear();   // numret gick runt: märkena gäller inget pass
        pass.epoch = 1;
    }
    if (!(pass.capacity > 0.0))
        pass.capacity = kPaceStartCost;
    step(id, grund);
}

void CanvasRenderLoop::step(std::uint64_t id, const Ctx& grund)
{
    Pass& pass = passes_[id];
    if (!pass.active())
        return;
    lastStepped_ = id;
    // Där passet byter till ritordning tas ett väntande jobb med samma vy in
    // — scenen kan ha ändrats sedan passet började.
    if (pass.stage == FramePace::Order::Sequence && pass.first) {
        Pending väntande;
        bool    nytt = false;
        {
            const std::lock_guard<std::mutex> l(lock_);
            const auto q = queue_.find(id);
            if (q != queue_.end() && paced(q->second.job) &&
                sameView(pass.view, viewOf(q->second.job))) {
                väntande = std::move(q->second);
                queue_.erase(q);
                order_.erase(std::remove(order_.begin(), order_.end(), id),
                             order_.end());
                nytt = true;
            }
        }
        if (nytt) {
            pass.job  = std::move(väntande.job);
            pass.wake = std::move(väntande.wake);
        }
    }
    const CanvasFrameJob& j      = *pass.job;
    const bool            taktad = paced(j);
    FramePace             takt;
    if (taktad) {
        takt.order  = pass.stage;
        takt.budget = pass.capacity;
        takt.first  = pass.first;
        takt.epoch  = pass.epoch;
        takt.stamps = &pass.stamps;
        takt.begin  = pass.cursor;
    }
    bool  ritade = false;   // steget kördes
    Slot* s      = nullptr;
    {
        const std::lock_guard<std::mutex> l(lock_);
        std::unique_ptr<Mailbox>&         m = boxes_[id];
        if (!m)
            m = std::make_unique<Mailbox>();
        m->life      = j.scene->life;
        m->forgotten = false;
        // En ledig bild finns alltid: högst en visas och högst en är färdig.
        for (Slot& k : m->slots)
            if (k.state == SlotState::Free) {
                s = &k;
                break;
            }
        if (s)
            s->state = SlotState::Writing;
    }
    if (!s) {
        pass.job.reset();   // ingen bild att rita i: passet ges upp
        pass.wake = nullptr;
        return;
    }

    if (!s->target || s->target->width() != j.width ||
        s->target->height() != j.height) {
        s->target.reset();
        s->target = context_->createSharedTarget(j.width, j.height);
    }

    // MÄTRADEN (`GLAZE_SCENE_LOG=1`): scenens takt och var bildens tid går.
    static const bool        logga = sceneLogOn();
    const auto               t0    = std::chrono::steady_clock::now();
    auto                     t1 = t0, t2 = t0;
    const gfx::RenderStats   före  = grund.r->stats();

    CanvasFrameResult svar;
    if (s->target && s->target->beginWrite()) {
        // Framens början på den här sidan: det döda släpps och budgetarna
        // gäller — jobbets, så tråden aldrig frågar appen.
        {
            const SceneCostScope kostnad(SceneCost::Trim);
            grund.canvas->trim();
            grund.images->trim(j.memoryBudget, j.uploadBudget);
        }
        // Med bildtakt får uppladdningarna en del av stegets tid.
        takeUploadMs();
        if (taktad)
            grund.images->setUploadDeadline(
                t0 + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                         std::chrono::duration<double, std::milli>(
                             j.frameBudgetMs * kPaceUploadShare)));
        gfx::Renderer& r = *grund.r;
        // Profilerarens bild för jobbet — faserna och räknarna följer med
        // svaret till fönstret.
        profileThreadBegin(r);
        grund.canvas->takeTextureRefused();
        if (r.beginOffscreen()) {
            svar = renderCanvasFrame(j, grund, s->target->target(),
                                     taktad ? &takt : nullptr);
            r.endOffscreen();
            ritade = true;
        }
        t1 = std::chrono::steady_clock::now();
        s->target->publish();
        t2 = std::chrono::steady_clock::now();
        svar.profile = profileThreadEnd();
    }
    if (logga)
        sceneLog(t0, t1, t2, före, grund.r->stats(), svar.profile);

    // PASSETS GÅNG (render_loop.h). Stegets kostnad följer tiden steget tog:
    // för långsamt krymper nästa, och ett steg som inte rymde allt växer
    // medan tiden räcker.
    bool klart = true;
    if (taktad && ritade) {
        // Stegets tid utan uppladdningarna — de har sin egen gräns.
        const double tid = std::max(
            0.0, std::chrono::duration<double, std::milli>(t2 - t0).count() -
                     takeUploadMs());
        // DEN FASTA DELEN: tiden fram till att stegets föremål var valda
        // (gallringen av allt som syns) beror inte på stegets kostnad. Det
        // kostnaden styr är resten, och den får alltid minst
        // `kPaceMinShare` av bildens tid — annars ritade en scen med dyr
        // gallring ingenting.
        pass.fixedMs = takt.selectedAt > t0
                           ? std::chrono::duration<double, std::milli>(takt.selectedAt - t0)
                                 .count()
                           : 0.0;
        const double rörlig = std::max(0.0, tid - pass.fixedMs);
        const double mål    = std::max(j.frameBudgetMs - pass.fixedMs,
                                       j.frameBudgetMs * kPaceMinShare);
        if (takt.spent > 0.0 && rörlig > 0.0 && (takt.cut || rörlig > mål)) {
            const double andel = std::clamp(mål / rörlig, kPaceShrinkMin, kPaceGrowMax);
            const double bas   = takt.cut ? pass.capacity : takt.spent;
            pass.capacity      = std::max(kPaceMinCost, bas * andel);
        }
        if (!takt.complete) {
            pass.first  = false;
            pass.cursor = takt.end;
            klart       = false;
        } else if (pass.stage == FramePace::Order::Priority && !pass.first) {
            // Påfylld över flera steg: bilden ritas en gång till i ritordning.
            pass.stage  = FramePace::Order::Sequence;
            pass.first  = true;
            pass.cursor = WorldPos{};
            klart       = false;
        }
    }
    if (logga && taktad) {
        // Mätraden för bildtakten, en per sekund: stegens kostnad och golv,
        // och hur många steg av varje slag.
        static thread_local int  påfyllda = 0, ordnade = 0, hela = 0;
        static thread_local auto senast   = t0;
        ++(pass.stage == FramePace::Order::Sequence && !(klart && takt.first) ? ordnade
                                                                              : påfyllda);
        if (klart)
            ++hela;
        if (std::chrono::duration<double>(t2 - senast).count() >= 1.0) {
            gfx::log("scen: takten — steg i påfyllning %d, i ritordning %d, färdiga "
                     "pass %d, stegets kostnad %.1f M, fast del %.1f ms, målet %.1f ms",
                     påfyllda, ordnade, hela, pass.capacity / kMillion, pass.fixedMs,
                     j.frameBudgetMs);
            påfyllda = ordnade = hela = 0;
            senast   = t2;
        }
    }
    if (!klart) {
        // Passet ritar vidare av sig självt — ritytan ska inte be om en bild
        // till för stegets skull.
        svar.complete        = false;
        svar.labelsAnimating = false;
        svar.drapesPending   = false;
        svar.uploadsDeferred = false;
    }
    std::function<void()> väck = klart ? std::move(pass.wake) : pass.wake;
    if (klart) {
        pass.job.reset();
        pass.wake = nullptr;
        pass.stamps.clear();
    }

    const bool ritad = svar.drawn;
    {
        const std::lock_guard<std::mutex> l(lock_);
        if (ritad) {
            // Den nya bilden är den färdiga — en äldre som aldrig togs är ledig.
            const auto m = boxes_.find(id);
            if (m != boxes_.end())
                for (Slot& k : m->second->slots)
                    if (&k != s && k.state == SlotState::Ready)
                        k.state = SlotState::Free;
            s->result = std::move(svar);
            s->state  = SlotState::Ready;
            ++frames_;
        } else {
            s->state = SlotState::Free;
        }
    }
    if (ritad && väck)
        postFrameWorkFromThread(std::move(väck));
}

// ------------------------------------------------------------------ bilden

void drawCanvasFrame(const Ctx& c, const CanvasRenderLoop::Shown& bild, float x,
                     float y, float opacity)
{
    if (!bild.image || !c.r)
        return;
    c.r->drawImage(*bild.image,
                   gfx::Rectf{x, y, static_cast<float>(bild.width),
                              static_cast<float>(bild.height)},
                   opacity);
}

} // namespace glaze::detail
