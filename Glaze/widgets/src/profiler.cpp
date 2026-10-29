// Profileraren — ringarna per fönster och pumpen,
// etikettregistret och rapporten. Det publika kontraktet står i
// glaze/profiler.h, krokarna och var de sitter i profiler_hooks.h.
//
// REGISTRET ÄR EN INSTANS I libglaze och RIVS ALDRIG (delat läge,
// `defaultPool()`s prejudikat): i delat läge hade det annars rivits i
// libglaze:s DLL_PROCESS_DETACH, efter att `ExitProcess` dödat trådarna — och
// en dödad tråd kan ha hållit ett av låsen. Headern har inga statiska och inga
// inline-variabler, och etikettnamnen KOPIERAS in, så en plugin som lossas
// lämnar ingen dinglande pekare efter sig.
//
// TRÅDARNA: varje tråd som spelar in en etikett får ett eget block, nått
// genom en `thread_local`-pekare. Blocket har ett eget lås — okontenderat
// utom när en läsare (`scopes`, `clear`) tar det kort. Vid trådslut MÄRKS
// blocket dött (ett atomiskt lager, inget lås: trådslutet kan vara en DLL:s
// DETACH) och flyttas in i registrets summa vid nästa läsning eller nästa
// nya block. Låsordningen är registrets före blockets, överallt.
//
// BILDEN OCH PUMPVARVEN är UI-trådens och låses inte. Den öppna bilden ligger
// i `g_frame` — konstant initierad, så att en krok utan inspelning aldrig
// rör registret.

#include "glaze/profiler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>

#include <gfx/renderer.h>
#include <gfx/window.h>

#include "glaze/app_window.h"
#include "profiler_hooks.h"

namespace glaze {
namespace {

using Klocka = std::chrono::steady_clock;

// Ringarnas förval (poster per fönster och för pumpen).
constexpr std::size_t kDefaultCapacity = 4096;

// HISTOGRAMMET: 8 fack per fördubbling av tiden i ns — fack b täcker
// [2^(b/8), 2^((b+1)/8)), och percentilen svarar med fackets övre gräns,
// högst 2^(1/8) ≈ 9 % över det sanna värdet (och aldrig över maxvärdet). 40
// fördubblingar räcker till 2^40 ns ≈ 18 minuter; längre hamnar i det sista.
constexpr int kBinsPerOctave = 8;
constexpr int kOctaves       = 40;
constexpr int kBins          = kBinsPerOctave * kOctaves;

// Takterna `WindowSummary` räknar missarna mot (ms mellan bilderna).
constexpr double kMs50Hz  = 1000.0 / 50.0;
constexpr double kMs150Hz = 1000.0 / 150.0;

constexpr double kNsPerMs = 1.0e6;

// Ramverkets etiketter, i `detail::FrameworkLabel`s ordning — förregistrerade
// med id 0, 1 och 2.
constexpr const char* kFrameworkNames[] = {"glaze.hitTest", "glaze.castRay",
                                           "glaze.heightAt"};

constexpr const char* kPhaseNames[] = {"content", "prepare", "drapes", "draw",
                                       "paint",   "overlay", "present", "wait",
                                       "gpu",     "compile"};
static_assert(std::size(kPhaseNames) == std::size_t(ProfilePhase::Count_),
              "ett namn per fas");

constexpr const char* kCountNames[] = {
    "draw_calls",     "bytes_uploaded", "buffers_created", "upload_breaks",
    "programs_built", "targets_created", "target_renders", "drape_renders",
    "drape_pixels",   "drape_keys",     "layer_rebuilds",  "glyph_builds",
    "damage_rects",   "full_damage"};
static_assert(std::size(kCountNames) == std::size_t(ProfileCount::Count_),
              "ett namn per räknare");

// EN RING med de senaste posterna: en vektor som skrivs runt.
template <class T>
class Ring {
public:
    // Behåller de senaste `cap` (≥ 1).
    void setCapacity(std::size_t cap)
    {
        std::vector<T> kvar = last(cap);
        v_    = std::move(kvar);
        head_ = 0;
        cap_  = cap;
    }
    void push(const T& t)
    {
        if (v_.size() < cap_) {
            v_.push_back(t);
            return;
        }
        v_[head_] = t;
        head_     = (head_ + 1) % cap_;
    }
    std::size_t size() const { return v_.size(); }
    // 0 = den senaste.
    T& fromNewest(std::size_t i)
    {
        return v_[(head_ + v_.size() - 1 - i) % v_.size()];
    }
    const T& fromNewest(std::size_t i) const
    {
        return v_[(head_ + v_.size() - 1 - i) % v_.size()];
    }
    // De `max` senaste (0 = alla), den äldsta först.
    std::vector<T> last(std::size_t max) const
    {
        const std::size_t n = max == 0 || max > v_.size() ? v_.size() : max;
        std::vector<T>    ut;
        ut.reserve(n);
        for (std::size_t i = n; i > 0; --i)
            ut.push_back(fromNewest(i - 1));
        return ut;
    }
    void clear()
    {
        v_.clear();
        head_ = 0;
    }

private:
    std::vector<T> v_;
    std::size_t    head_ = 0;   // den äldsta, när ringen är full
    std::size_t    cap_  = kDefaultCapacity;
};

// ---------------------------------------------------------------- etiketterna

struct LabelAcc {
    std::int64_t                      count   = 0;   // scopes + profileCount
    std::uint64_t                     totalNs = 0;
    std::uint64_t                     maxNs   = 0;
    std::array<std::uint64_t, kBins> bins{};

    void add(const LabelAcc& o)
    {
        count += o.count;
        totalNs += o.totalNs;
        maxNs = std::max(maxNs, o.maxNs);
        for (int b = 0; b < kBins; ++b)
            bins[static_cast<std::size_t>(b)] += o.bins[static_cast<std::size_t>(b)];
    }
};

void merge(std::vector<LabelAcc>& till, const std::vector<LabelAcc>& från)
{
    if (till.size() < från.size())
        till.resize(från.size());
    for (std::size_t i = 0; i < från.size(); ++i)
        till[i].add(från[i]);
}

int binOf(std::int64_t ns)
{
    if (ns < 1)
        return 0;
    const int b = static_cast<int>(std::log2(static_cast<double>(ns)) *
                                   kBinsPerOctave);
    return std::clamp(b, 0, kBins - 1);
}

// Percentilen `p` (0–1) ur facken: den minsta övre gräns som rymmer p av
// proven, klampad till maxvärdet. ms.
double binPercentile(const LabelAcc& a, double p)
{
    std::uint64_t n = 0;
    for (const std::uint64_t c : a.bins)
        n += c;
    if (n == 0)
        return 0.0;
    const auto    rang = static_cast<std::uint64_t>(std::ceil(p * static_cast<double>(n)));
    std::uint64_t sum  = 0;
    for (int b = 0; b < kBins; ++b) {
        sum += a.bins[static_cast<std::size_t>(b)];
        if (sum >= std::max<std::uint64_t>(rang, 1)) {
            const double övre = std::exp2(static_cast<double>(b + 1) / kBinsPerOctave);
            return std::min(övre, static_cast<double>(a.maxNs)) / kNsPerMs;
        }
    }
    return static_cast<double>(a.maxNs) / kNsPerMs;
}

struct ThreadBlock {
    std::mutex            m;
    std::vector<LabelAcc> labels;   // index = etikettens id
    std::atomic<bool>     dead{false};
};

// ------------------------------------------------------------ UI-trådens data

struct WindowRing {
    const AppWindow*   owner = nullptr;
    std::uint64_t      id    = 0;
    std::string        device;
    Ring<FrameProfile> frames;
    double             lastAt     = -1.0;   // förra bildens början, ms
    double             firstInput = -1.0;   // första inmatningen sedan dess
};

// Ett pumpvarv som pågår. `nested` är tiden i nästlade varv hittills, och
// `nestedAtLap` den vid förra delen — skillnaden räknas bort ur delen.
struct OpenIteration {
    int         depth       = 0;
    double      lastLap     = 0.0;
    double      nested      = 0.0;
    double      nestedAtLap = 0.0;
    PumpProfile rec;
};

struct Registry {
    Registry()
    {
        for (const char* n : kFrameworkNames) {
            ids.emplace(n, static_cast<std::uint32_t>(names.size()));
            names.emplace_back(n);
        }
    }

    const Klocka::time_point epoch = Klocka::now();

    // Etiketterna — från vilken tråd som helst, under `m`.
    std::mutex                                        m;
    std::vector<std::string>                          names;   // id → namn
    std::map<std::string, std::uint32_t, std::less<>> ids;
    std::vector<ThreadBlock*>                         blocks;    // levande trådar
    std::vector<LabelAcc>                             retired;   // döda trådars

    // UI-tråden.
    std::size_t                              capacity = kDefaultCapacity;
    std::vector<std::unique_ptr<WindowRing>> windows;
    Ring<PumpProfile>                        pump;
    std::vector<OpenIteration>               iterations;   // nästlingens stack
    std::uint64_t                            iterationSeq = 0;
    std::uint64_t                            frameSeq     = 0;
    std::vector<gfx::GpuTime>                gpuTimes;     // återanvänd
};

// AVSIKTLIGT LÄCKT — se filhuvudet.
Registry& registry()
{
    static Registry& r = *new Registry;
    return r;
}

// PÅ ELLER AV — konstant initierad, så att `profiling()` är en läsning.
std::atomic<bool> g_on{false};

// `GLAZE_PROFILE` läses en gång per process.
std::once_flag g_env;

// Har något fönster en ring? Låter en stängning utan profilerare passera
// utan att registret skapas. UI-tråden.
bool g_rings = false;

// DEN ÖPPNA BILDEN (UI-tråden). Konstant initierad och trivialt förstörbar.
struct OpenFrame {
    bool             open    = false;
    bool             outside = false;   // öppnad av innehållet, inte pumpen
    WindowRing*      ring    = nullptr;
    gfx::Renderer*   r       = nullptr;
    std::uint64_t    token   = 0;       // bildens löpnummer i profileraren
    double           t0      = 0.0;     // ms
    double           mark    = 0.0;     // slutet av det senaste passet, ms
    gfx::RenderStats s0{};
    FrameProfile     rec{};
};
OpenFrame g_frame;

double msSince(const Registry& g, Klocka::time_point t)
{
    return std::chrono::duration<double, std::milli>(t - g.epoch).count();
}

double nowMs()
{
    return msSince(registry(), Klocka::now());
}

std::int64_t nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               Klocka::now().time_since_epoch())
        .count();
}

// --------------------------------------------------------- trådarnas block

thread_local ThreadBlock* t_block  = nullptr;
thread_local bool         t_reaped = false;   // tråden är på väg ut

// Märker trådens block dött vid trådslutet — utan lås (se filhuvudet). En
// inspelning EFTER det (en annan trådlokal destruktor) spelas inte in.
struct BlockReaper {
    bool armed = false;
    ~BlockReaper()
    {
        if (t_block)
            t_block->dead.store(true, std::memory_order_release);
        t_block  = nullptr;
        t_reaped = true;
    }
};
thread_local BlockReaper t_reaper;

// De döda trådarnas block in i summan. Under registrets lås.
void reapDead(Registry& g)
{
    for (auto it = g.blocks.begin(); it != g.blocks.end();) {
        ThreadBlock* b = *it;
        if (!b->dead.load(std::memory_order_acquire)) {
            ++it;
            continue;
        }
        {
            std::lock_guard<std::mutex> lås(b->m);
            merge(g.retired, b->labels);
        }
        delete b;
        it = g.blocks.erase(it);
    }
}

ThreadBlock* threadBlock()
{
    if (t_block || t_reaped)
        return t_block;
    Registry& g = registry();
    auto      b = std::make_unique<ThreadBlock>();
    {
        std::lock_guard<std::mutex> lås(g.m);
        reapDead(g);
        g.blocks.push_back(b.get());
    }
    t_block        = b.release();
    t_reaper.armed = true;   // konstrueras i tråden — och rivs vid trådslut
    return t_block;
}

// Ett prov (eller bara ett antal) till etiketten i trådens block.
void record(std::uint32_t id, std::int64_t n, std::int64_t ns,
            bool timed) noexcept
{
    try {
        ThreadBlock* b = threadBlock();
        if (!b)
            return;
        std::lock_guard<std::mutex> lås(b->m);
        if (b->labels.size() <= id)
            b->labels.resize(static_cast<std::size_t>(id) + 1);
        LabelAcc& a = b->labels[id];
        a.count += n;
        if (!timed)
            return;
        const auto t = static_cast<std::uint64_t>(std::max<std::int64_t>(ns, 0));
        a.totalNs += t;
        a.maxNs = std::max(a.maxNs, t);
        ++a.bins[static_cast<std::size_t>(binOf(ns))];
    } catch (...) {
        // Ett minnesfel i en inspelning tappar provet — aldrig appen.
    }
}

std::uint32_t intern(std::string_view name)
{
    Registry&                   g = registry();
    std::lock_guard<std::mutex> lås(g.m);
    if (const auto it = g.ids.find(name); it != g.ids.end())
        return it->second;
    const auto id = static_cast<std::uint32_t>(g.names.size());
    g.names.emplace_back(name);
    g.ids.emplace(std::string(name), id);
    return id;
}

// ---------------------------------------------------------------- bilderna

WindowRing* findRing(Registry& g, const AppWindow& w)
{
    for (const auto& ring : g.windows)
        if (ring->owner == &w)
            return ring.get();
    return nullptr;
}

WindowRing& ringFor(Registry& g, AppWindow& w, gfx::Renderer& r)
{
    if (WindowRing* ring = findRing(g, w))
        return *ring;
    auto ring    = std::make_unique<WindowRing>();
    ring->owner  = &w;
    ring->id     = w.window().id();
    ring->device = r.deviceName();
    ring->frames.setCapacity(g.capacity);
    g.windows.push_back(std::move(ring));
    g_rings = true;
    return *g.windows.back();
}

void openFrame(Registry& g, WindowRing& ring, gfx::Renderer& r, bool outside)
{
    const double nu = nowMs();
    g_frame         = OpenFrame{};
    g_frame.open    = true;
    g_frame.outside = outside;
    g_frame.ring    = &ring;
    g_frame.r       = &r;
    g_frame.token   = ++g.frameSeq;
    g_frame.t0      = nu;
    g_frame.mark    = nu;
    g_frame.s0      = r.stats();
    FrameProfile& rec = g_frame.rec;
    rec.window        = ring.id;
    rec.iteration     = g.iterations.empty() ? 0 : g.iterations.back().rec.iteration;
    rec.at            = nu;
    rec.interval      = ring.lastAt >= 0.0 ? nu - ring.lastAt : 0.0;
    rec.outsidePump   = outside;
    ring.lastAt       = nu;
}

template <class N>
std::uint64_t delta(N efter, N före)
{
    return static_cast<std::uint64_t>(static_cast<N>(efter - före));
}

// Stänger den öppna bilden vid `slut` och lägger den i ringen. Renderarens
// räknare tas som en differens (`resetStats` anropas aldrig), och de
// GPU-tider som kommit sedan förra bilden förs till sina bilder.
void closeFrame(Registry& g, double slut, bool continued)
{
    OpenFrame&    f   = g_frame;
    FrameProfile& rec = f.rec;
    f.open            = false;
    using P           = ProfilePhase;
    using C           = ProfileCount;
    auto ms = [&rec](P p) -> float& { return rec.ms[std::size_t(p)]; };
    auto n  = [&rec](C c) -> std::uint64_t& { return rec.count[std::size_t(c)]; };
    if (f.outside) {
        rec.total = ms(P::Content) + ms(P::Overlay);
    } else {
        rec.total        = slut - f.t0;
        ms(P::Present)   = static_cast<float>(slut - f.mark);
    }
    const gfx::RenderStats& s0 = f.s0;
    const gfx::RenderStats& s1 = f.r->stats();
    ms(P::PresentWait) = static_cast<float>(delta(s1.presentNs, s0.presentNs) / kNsPerMs);
    ms(P::Compile)     = static_cast<float>(delta(s1.compileNs, s0.compileNs) / kNsPerMs);
    n(C::DrawCalls)      = delta(s1.drawCalls, s0.drawCalls);
    n(C::BytesUploaded)  = delta(s1.bytesUploaded, s0.bytesUploaded);
    n(C::BuffersCreated) = delta(s1.buffersCreated, s0.buffersCreated);
    n(C::UploadBreaks)   = delta(s1.uploadBreaks, s0.uploadBreaks);
    n(C::ProgramsBuilt)  = delta(s1.programsBuilt, s0.programsBuilt);
    n(C::TargetsCreated) = delta(s1.targetsCreated, s0.targetsCreated);
    rec.frame     = f.r->frameSerial();
    rec.continued = continued;
    WindowRing& ring = *f.ring;
    if (ring.firstInput >= 0.0) {
        rec.inputLatency = slut - ring.firstInput;
        ring.firstInput  = -1.0;
    }
    ring.frames.push(rec);

    // GPU-tiderna kommer i efterhand — de förs till bilden med samma löpnummer.
    g.gpuTimes.clear();
    f.r->takeGpuTimes(g.gpuTimes);
    for (const gfx::GpuTime& t : g.gpuTimes)
        for (std::size_t i = 0; i < ring.frames.size(); ++i) {
            FrameProfile& p = ring.frames.fromNewest(i);
            if (p.frame == t.serial) {
                p.ms[std::size_t(P::Gpu)] = static_cast<float>(t.ns / kNsPerMs);
                break;
            }
        }
}

// --------------------------------------------------------- sammanfattningen

// Den närmaste rangens värde (p 0–1). Sorterar `v`.
double percentile(std::vector<double>& v, double p)
{
    if (v.empty())
        return 0.0;
    std::sort(v.begin(), v.end());
    const auto k = static_cast<std::size_t>(std::ceil(p * static_cast<double>(v.size())));
    return v[std::clamp<std::size_t>(k, 1, v.size()) - 1];
}

WindowSummary summarize(const WindowRing& ring, double från)
{
    WindowSummary s;
    s.window     = ring.id;
    s.deviceName = ring.device;
    // Föregångaren avgör om intervallet är takten — också den som ligger
    // före tidsfönstret.
    const std::vector<FrameProfile> alla = ring.frames.last(0);
    std::vector<double>             intervall;
    std::array<std::vector<double>, std::size_t(ProfilePhase::Count_)> faser;
    for (std::size_t i = 0; i < alla.size(); ++i) {
        const FrameProfile& f = alla[i];
        if (f.at < från)
            continue;
        ++s.frames;
        if (i > 0 && alla[i - 1].continued) {
            ++s.paced;
            intervall.push_back(f.interval);
            s.missed50Hz += f.interval > kMs50Hz ? 1 : 0;
            s.missed150Hz += f.interval > kMs150Hz ? 1 : 0;
        }
        for (std::size_t p = 0; p < faser.size(); ++p)
            if (p != std::size_t(ProfilePhase::Gpu) || f.ms[p] > 0.0f)
                faser[p].push_back(f.ms[p]);
        for (std::size_t c = 0; c < s.perFrame.size(); ++c)
            s.perFrame[c] += static_cast<double>(f.count[c]);
    }
    if (s.frames == 0)
        return s;
    for (double& v : s.perFrame)
        v /= static_cast<double>(s.frames);
    if (!intervall.empty())
        s.intervalMax = *std::max_element(intervall.begin(), intervall.end());
    s.intervalP50 = percentile(intervall, 0.50);
    s.intervalP95 = percentile(intervall, 0.95);
    s.intervalP99 = percentile(intervall, 0.99);
    for (std::size_t p = 0; p < faser.size(); ++p) {
        s.phaseP50[p] = static_cast<float>(percentile(faser[p], 0.50));
        s.phaseP95[p] = static_cast<float>(percentile(faser[p], 0.95));
    }
    return s;
}

// Tidsfönstrets början i ms (0 sekunder = allt).
double windowStart(double seconds)
{
    return seconds > 0.0 ? nowMs() - seconds * 1000.0 : -1.0;
}

} // namespace

// ================================================================ det publika

Profiler& profiler()
{
    static Profiler p;
    detail::profileEnvironment();
    return p;
}

void Profiler::setEnabled(bool on)
{
    registry();   // finns innan första kroken spelar in
    g_on.store(on, std::memory_order_relaxed);
}

bool Profiler::enabled() const
{
    return g_on.load(std::memory_order_relaxed);
}

void Profiler::setCapacity(std::size_t frames)
{
    Registry& g = registry();
    g.capacity  = std::max<std::size_t>(frames, 1);
    for (const auto& ring : g.windows)
        ring->frames.setCapacity(g.capacity);
    g.pump.setCapacity(g.capacity);
}

std::size_t Profiler::capacity() const
{
    return registry().capacity;
}

std::vector<FrameProfile> Profiler::frames(const AppWindow& w,
                                           std::size_t max) const
{
    const WindowRing* ring = findRing(registry(), w);
    return ring ? ring->frames.last(max) : std::vector<FrameProfile>{};
}

std::vector<PumpProfile> Profiler::pump(std::size_t max) const
{
    return registry().pump.last(max);
}

WindowSummary Profiler::summary(const AppWindow& w, double seconds) const
{
    const WindowRing* ring = findRing(registry(), w);
    return ring ? summarize(*ring, windowStart(seconds)) : WindowSummary{};
}

std::vector<LabelStats> Profiler::scopes(bool reset) const
{
    Registry&                g = registry();
    std::vector<LabelAcc>    summa;
    std::vector<std::string> namn;
    {
        std::lock_guard<std::mutex> lås(g.m);
        reapDead(g);
        summa = g.retired;
        if (reset)
            g.retired.clear();
        for (ThreadBlock* b : g.blocks) {
            std::lock_guard<std::mutex> blocklås(b->m);
            merge(summa, b->labels);
            if (reset)
                b->labels.clear();
        }
        namn = g.names;
    }
    std::vector<LabelStats> ut;
    for (std::size_t id = 0; id < summa.size() && id < namn.size(); ++id) {
        const LabelAcc& a = summa[id];
        if (a.count == 0 && a.totalNs == 0)
            continue;
        LabelStats s;
        s.name    = namn[id];
        s.count   = static_cast<std::uint64_t>(std::max<std::int64_t>(a.count, 0));
        s.totalMs = static_cast<double>(a.totalNs) / kNsPerMs;
        s.p50     = binPercentile(a, 0.50);
        s.p95     = binPercentile(a, 0.95);
        s.p99     = binPercentile(a, 0.99);
        s.maxMs   = static_cast<double>(a.maxNs) / kNsPerMs;
        ut.push_back(std::move(s));
    }
    std::sort(ut.begin(), ut.end(),
              [](const LabelStats& a, const LabelStats& b) { return a.name < b.name; });
    return ut;
}

void Profiler::clear()
{
    Registry& g = registry();
    for (const auto& ring : g.windows) {
        ring->frames.clear();
        ring->lastAt     = -1.0;
        ring->firstInput = -1.0;
    }
    g.pump.clear();
    std::lock_guard<std::mutex> lås(g.m);
    reapDead(g);
    g.retired.clear();
    for (ThreadBlock* b : g.blocks) {
        std::lock_guard<std::mutex> blocklås(b->m);
        b->labels.clear();
    }
}

// RAPPORTEN — loggtext för en utvecklare, byggd rad för rad med snprintf.
std::string Profiler::report(double seconds) const
{
    char        rad[1024];
    std::string ut;
    if (!enabled()) {
        std::snprintf(rad, sizeof rad,
                      "profileraren: av (profiler().setEnabled(true) eller "
                      "GLAZE_PROFILE=1)\n");
        return rad;
    }
    Registry&    g    = registry();
    const double från = windowStart(seconds);
    if (seconds > 0.0)
        std::snprintf(rad, sizeof rad, "profileraren: de senaste %.1f s\n", seconds);
    else
        std::snprintf(rad, sizeof rad, "profileraren: hela ringarna\n");
    ut += rad;

    for (const auto& ring : g.windows) {
        const WindowSummary s = summarize(*ring, från);
        if (s.frames == 0)
            continue;
        std::snprintf(rad, sizeof rad,
                      "fönster %llu \"%.200s\" (%.120s): %zu bilder, %zu i takt — "
                      "intervall p50 %.2f p95 %.2f p99 %.2f max %.2f ms, över "
                      "20 ms %zu, över 6,7 ms %zu\n",
                      static_cast<unsigned long long>(s.window),
                      ring->owner->title().c_str(), s.deviceName.c_str(), s.frames,
                      s.paced, s.intervalP50, s.intervalP95, s.intervalP99,
                      s.intervalMax, s.missed50Hz, s.missed150Hz);
        ut += rad;
        std::snprintf(rad, sizeof rad, "  faserna p50/p95 ms:");
        ut += rad;
        for (std::size_t p = 0; p < s.phaseP50.size(); ++p) {
            std::snprintf(rad, sizeof rad, " %s %.2f/%.2f", kPhaseNames[p],
                          static_cast<double>(s.phaseP50[p]),
                          static_cast<double>(s.phaseP95[p]));
            ut += rad;
        }
        std::snprintf(rad, sizeof rad, "\n  per bild:");
        ut += rad;
        for (std::size_t c = 0; c < s.perFrame.size(); ++c) {
            if (s.perFrame[c] == 0.0)
                continue;
            std::snprintf(rad, sizeof rad, " %s %.1f", kCountNames[c], s.perFrame[c]);
            ut += rad;
        }
        ut += '\n';
    }

    // PUMPEN — varven i tidsfönstret.
    std::array<std::vector<double>, 6> delar;
    double        längst = 0.0;
    std::uint64_t händelser = 0, postade = 0;
    std::size_t   varv = 0;
    for (const PumpProfile& p : g.pump.last(0)) {
        if (p.at < från)
            continue;
        ++varv;
        const float v[6] = {p.wait, p.events, p.posted, p.timers, p.commits, p.closes};
        for (std::size_t i = 0; i < delar.size(); ++i)
            delar[i].push_back(v[i]);
        längst = std::max(längst, static_cast<double>(p.postedMax));
        händelser += p.eventCount;
        postade += p.postedCount;
    }
    if (varv > 0) {
        std::snprintf(rad, sizeof rad,
                      "pumpen: %zu varv, %llu händelser, %llu postade (längst "
                      "%.2f ms) — p50/p95 ms:",
                      varv, static_cast<unsigned long long>(händelser),
                      static_cast<unsigned long long>(postade), längst);
        ut += rad;
        constexpr const char* kDelar[] = {"wait",    "events",  "posted",
                                          "timers",  "commits", "closes"};
        for (std::size_t i = 0; i < delar.size(); ++i) {
            const double p50 = percentile(delar[i], 0.50);
            const double p95 = percentile(delar[i], 0.95);
            std::snprintf(rad, sizeof rad, " %s %.2f/%.2f", kDelar[i], p50, p95);
            ut += rad;
        }
        ut += '\n';
    }

    // ETIKETTERNA — sedan start eller förra `scopes(true)`.
    const std::vector<LabelStats> etiketter = scopes();
    if (!etiketter.empty()) {
        std::snprintf(rad, sizeof rad, "etiketterna:\n");
        ut += rad;
    }
    for (const LabelStats& s : etiketter) {
        std::snprintf(rad, sizeof rad,
                      "  %.80s: %llu st, %.2f ms — p50 %.3f p95 %.3f p99 %.3f "
                      "max %.3f ms\n",
                      s.name.c_str(), static_cast<unsigned long long>(s.count),
                      s.totalMs, s.p50, s.p95, s.p99, s.maxMs);
        ut += rad;
    }
    return ut;
}

const char* profilePhaseName(ProfilePhase p)
{
    const auto i = static_cast<std::size_t>(p);
    return i < std::size(kPhaseNames) ? kPhaseNames[i] : "";
}

const char* profileCountName(ProfileCount c)
{
    const auto i = static_cast<std::size_t>(c);
    return i < std::size(kCountNames) ? kCountNames[i] : "";
}

ProfileLabel::ProfileLabel(std::string_view name) : id_(intern(name)) {}

ProfileScope::ProfileScope(const ProfileLabel& label) noexcept
    : id_(g_on.load(std::memory_order_relaxed) ? label.id() : kOff)
{
    if (id_ != kOff)
        start_ = nowNs();
}

void ProfileScope::finish() noexcept
{
    record(id_, 1, nowNs() - start_, true);
}

void profileCount(const ProfileLabel& label, std::int64_t n) noexcept
{
    if (g_on.load(std::memory_order_relaxed))
        record(label.id(), n, 0, false);
}

// ================================================================== krokarna

namespace detail {

bool profiling() noexcept
{
    return g_on.load(std::memory_order_relaxed);
}

void profileEnvironment()
{
    std::call_once(g_env, [] {
        const char* v = std::getenv("GLAZE_PROFILE");   // lang_gate:rad miljövariabelns namn
        if (!v || !*v || std::strcmp(v, "0") == 0)
            return;
        registry();   // finns innan första kroken spelar in
        g_on.store(true, std::memory_order_relaxed);
    });
}

// ---- pumpen

void profilePumpBegin(int depth)
{
    if (!profiling())
        return;
    Registry& g = registry();
    // Ett varv som började när profileraren var på men aldrig avslutades
    // (avstängd mitt i) står kvar på samma eller djupare nivå — det släpps.
    while (!g.iterations.empty() && g.iterations.back().depth >= depth)
        g.iterations.pop_back();
    OpenIteration it;
    it.depth         = depth;
    it.lastLap       = nowMs();
    it.rec.iteration = ++g.iterationSeq;
    it.rec.at        = it.lastLap;
    it.rec.depth     = static_cast<std::uint32_t>(depth);
    g.iterations.push_back(it);
}

void profilePumpLap(PumpPart part, std::uint32_t count)
{
    if (!profiling())
        return;
    Registry& g = registry();
    if (g.iterations.empty())
        return;
    OpenIteration& it = g.iterations.back();
    const double   nu = nowMs();
    const auto     ms = static_cast<float>(
        std::max(0.0, nu - it.lastLap - (it.nested - it.nestedAtLap)));
    switch (part) {
    case PumpPart::Wait:    it.rec.wait += ms; break;
    case PumpPart::Events:  it.rec.events += ms; it.rec.eventCount += count; break;
    case PumpPart::Posted:  it.rec.posted += ms; break;
    case PumpPart::Timers:  it.rec.timers += ms; break;
    case PumpPart::Commits: it.rec.commits += ms; break;
    case PumpPart::Closes:  it.rec.closes += ms; break;
    }
    it.lastLap     = nu;
    it.nestedAtLap = it.nested;
}

void profilePumpEnd(int depth)
{
    if (!profiling())
        return;
    Registry& g = registry();
    if (g.iterations.empty() || g.iterations.back().depth != depth)
        return;   // varvet började medan profileraren var av
    const OpenIteration it = g.iterations.back();
    g.iterations.pop_back();
    g.pump.push(it.rec);
    // Hela det nästlade varvet räknas bort ur den yttre delen.
    if (!g.iterations.empty())
        g.iterations.back().nested += nowMs() - it.rec.at;
}

PostedCall::PostedCall() noexcept
{
    if (!profiling())
        return;
    Registry& g = registry();
    if (g.iterations.empty())
        return;
    on_      = true;
    t0_      = nowMs();
    nested0_ = g.iterations.back().nested;
}

PostedCall::~PostedCall()
{
    if (!on_)
        return;
    Registry& g = registry();
    if (g.iterations.empty())
        return;
    OpenIteration& it = g.iterations.back();
    const double   ms = nowMs() - t0_ - (it.nested - nested0_);
    ++it.rec.postedCount;
    it.rec.postedMax = std::max(it.rec.postedMax, static_cast<float>(ms));
}

// ---- bilden

void profileFrameBegin(AppWindow& w)
{
    if (!profiling())
        return;
    Registry&      g = registry();
    gfx::Renderer& r = w.window().renderer();
    openFrame(g, ringFor(g, w, r), r, false);
    r.setGpuTiming(true);   // bilderna profileraren spelar in, och bara de
}

void profileFrameEnd(AppWindow& w, bool continued)
{
    if (!g_frame.open || g_frame.outside || g_frame.ring->owner != &w)
        return;   // ingen bild som pumpen öppnade för fönstret
    g_frame.r->setGpuTiming(false);
    if (!profiling()) {
        g_frame.open = false;   // avstängd mitt i bilden — den kastas
        return;
    }
    closeFrame(registry(), nowMs(), continued);
}

void profileInput(AppWindow& w)
{
    if (!profiling())
        return;
    Registry&   g    = registry();
    WindowRing& ring = ringFor(g, w, w.window().renderer());
    if (ring.firstInput < 0.0)
        ring.firstInput = nowMs();
}

void profileWindowClosed(AppWindow& w)
{
    if (!g_rings)
        return;
    Registry& g = registry();
    if (g_frame.open && g_frame.ring->owner == &w)
        g_frame.open = false;
    std::erase_if(g.windows, [&w](const std::unique_ptr<WindowRing>& ring) {
        return ring->owner == &w;
    });
}

WindowPass::WindowPass(AppWindow& w, ProfilePhase phase) noexcept
    : phase_(phase)
{
    if (!profiling())
        return;
    try {
        // Ingen bild öppen: innehållet ritas utanför pumpen.
        if (!g_frame.open && phase == ProfilePhase::Content) {
            Registry&      g = registry();
            gfx::Renderer& r = w.window().renderer();
            openFrame(g, ringFor(g, w, r), r, true);
        }
    } catch (...) {
        return;   // ringen gick inte att skapa — ingen inspelning
    }
    if (!g_frame.open || g_frame.ring->owner != &w)
        return;
    w_     = &w;
    token_ = g_frame.token;
    t0_    = nowMs();
}

WindowPass::~WindowPass()
{
    if (!w_ || !g_frame.open || g_frame.token != token_)
        return;
    const double nu = nowMs();
    g_frame.rec.ms[std::size_t(phase_)] += static_cast<float>(nu - t0_);
    g_frame.mark = nu;
    // Bilden utanför pumpen slutar med overlayen.
    if (phase_ == ProfilePhase::Overlay && g_frame.outside)
        closeFrame(registry(), nu, false);
}

PhaseScope::PhaseScope(ProfilePhase phase) noexcept : phase_(phase)
{
    if (!profiling() || !g_frame.open)
        return;
    token_ = g_frame.token;
    t0_    = nowMs();
}

PhaseScope::PhaseScope(ProfilePhase phase, PhaseScope& outer) noexcept
    : PhaseScope(phase)
{
    outer_ = &outer;
}

PhaseScope::~PhaseScope()
{
    if (token_ == 0 || !g_frame.open || g_frame.token != token_)
        return;
    const double ms = nowMs() - t0_;
    g_frame.rec.ms[std::size_t(phase_)] += static_cast<float>(ms - excluded_);
    if (outer_)
        outer_->excluded_ += ms;
}

void profileCount(ProfileCount c, std::uint64_t n) noexcept
{
    if (!profiling() || !g_frame.open)
        return;
    g_frame.rec.count[std::size_t(c)] += n;
}

const ProfileLabel& frameworkLabel(FrameworkLabel l)
{
    static const ProfileLabel kEtiketter[] = {ProfileLabel{kFrameworkNames[0]},
                                              ProfileLabel{kFrameworkNames[1]},
                                              ProfileLabel{kFrameworkNames[2]}};
    return kEtiketter[static_cast<std::size_t>(l)];
}

} // namespace detail
} // namespace glaze
