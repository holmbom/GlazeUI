// RITYTANS RENDERINGSTRÅD (render_loop.h).

#include "render_loop.h"

#include <algorithm>
#include <atomic>
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
            {
                std::unique_lock<std::mutex> l(lock_);
                wakeup_.wait(l, [&] {
                    return stop_ || forgotten_ ||
                           (!paused_ && (!order_.empty() || !orders_.empty()));
                });
                stopp = stop_;
                // De glömda brevlådorna — och vid stoppet alla — rivs här,
                // på tråden som äger bilderna.
                for (auto it = boxes_.begin(); it != boxes_.end();) {
                    if (stopp || it->second->forgotten) {
                        glömda.push_back(std::move(it->second));
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
                } else if (!stopp && !paused_ && !order_.empty()) {
                    const std::uint64_t id = order_.front();
                    order_.erase(order_.begin());
                    const auto q = queue_.find(id);
                    p            = std::move(q->second);
                    queue_.erase(q);
                    jobb = true;
                }
            }
            glömda.clear();
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
        }
        // Jobben som blev över håller scener — släppta före resurserna.
        {
            const std::lock_guard<std::mutex> l(lock_);
            queue_.clear();
            order_.clear();
        }
    }
    context_->detachThread();
    context_.reset();
}

void CanvasRenderLoop::render(Pending& p, const Ctx& grund)
{
    const CanvasFrameJob& j  = p.job;
    const std::uint64_t   id = j.scene->id;
    Slot*                 s  = nullptr;
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
        if (!s)
            return;
        s->state = SlotState::Writing;
    }

    if (!s->target || s->target->width() != j.width ||
        s->target->height() != j.height) {
        s->target.reset();
        s->target = context_->createSharedTarget(j.width, j.height);
    }

    CanvasFrameResult svar;
    if (s->target && s->target->beginWrite()) {
        // Framens början på den här sidan: det döda släpps och budgetarna
        // gäller — jobbets, så tråden aldrig frågar appen.
        grund.canvas->trim();
        grund.images->trim(j.memoryBudget, j.uploadBudget);
        gfx::Renderer& r = *grund.r;
        // Profilerarens bild för jobbet — faserna och räknarna följer med
        // svaret till fönstret.
        profileThreadBegin(r);
        grund.canvas->takeTextureRefused();
        if (r.beginOffscreen()) {
            svar = renderCanvasFrame(j, grund, s->target->target());
            r.endOffscreen();
        }
        s->target->publish();
        svar.profile = profileThreadEnd();
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
    if (ritad && p.wake)
        postFrameWorkFromThread(std::move(p.wake));
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
