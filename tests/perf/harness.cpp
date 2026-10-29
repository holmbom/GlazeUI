// tests/perf/harness — se harness.h.

#include "perf/harness.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <gfx/geom3d.h>

#include "glaze_assets/png.h"
#include "canvas/resources.h"   // intern — ritytans chunkar
#include "image_cache.h"         // intern — bildcachens byte åt ritytan

#if defined(_WIN32)
#    include <windows.h>
#    include <dxgi1_4.h>
#    include <psapi.h>
#elif defined(__APPLE__)
#    include <mach/mach.h>
#    include <sys/resource.h>
#else
#    include <fstream>
#    include <sys/resource.h>
#endif

namespace perf {

bool g_verbose = false;
bool g_synlig  = false;
int  g_fel     = 0;

#ifdef NDEBUG
const char* const kBygge = "Release";
#else
const char* const kBygge = "Debug";
#endif

double msSedan(Klocka::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Klocka::now() - t0).count();
}

void fel(const std::string& vad)
{
    std::printf("  FEL  %s\n", vad.c_str());
    ++g_fel;
}

std::uint64_t blanda(std::uint64_t x)
{
    Slump r{x};
    return r.nästa();
}

// ---------------------------------------------------------------- mätetal

double Serie::kvantil(double q) const
{
    if (v.empty())
        return 0.0;
    std::vector<double> s = v;
    std::sort(s.begin(), s.end());
    const auto i = static_cast<std::size_t>(q * static_cast<double>(s.size() - 1) + 0.5);
    return s[std::min(i, s.size() - 1)];
}

double Serie::max() const
{
    return v.empty() ? 0.0 : *std::max_element(v.begin(), v.end());
}

int Serie::över(double gräns) const
{
    return static_cast<int>(
        std::count_if(v.begin(), v.end(), [gräns](double x) { return x > gräns; }));
}

// ------------------------------------------------------------------ minnet

Minne läsMinne()
{
    Minne m;
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc)) {
        m.process = static_cast<double>(pmc.PrivateUsage) / 1.0e6;
        m.topp    = static_cast<double>(pmc.PeakPagefileUsage) / 1.0e6;
        m.arbete  = static_cast<double>(pmc.WorkingSetSize) / 1.0e6;
    }
    // D3D11CreateDevice(nullptr, …) tar DXGI:s första adapter (canvas_perf);
    // QueryVideoMemoryInfo svarar för DEN HÄR processen.
    IDXGIFactory1* fabrik = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                     reinterpret_cast<void**>(&fabrik)))) {
        IDXGIAdapter1* a = nullptr;
        if (fabrik->EnumAdapters1(0, &a) == S_OK) {
            IDXGIAdapter3* a3 = nullptr;
            if (SUCCEEDED(a->QueryInterface(__uuidof(IDXGIAdapter3),
                                            reinterpret_cast<void**>(&a3)))) {
                DXGI_QUERY_VIDEO_MEMORY_INFO lokal{}, delat{};
                a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &lokal);
                a3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &delat);
                m.gpu = static_cast<double>(lokal.CurrentUsage + delat.CurrentUsage) / 1.0e6;
                a3->Release();
            }
            a->Release();
        }
        fabrik->Release();
    }
#elif defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t antal = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info),
                  &antal) == KERN_SUCCESS) {
        m.process = static_cast<double>(info.phys_footprint) / 1.0e6;
        m.arbete  = static_cast<double>(info.resident_size) / 1.0e6;
        if (antal >= TASK_VM_INFO_REV2_COUNT)
            m.topp = static_cast<double>(info.ledger_phys_footprint_peak) / 1.0e6;
    }
#else
    std::ifstream f("/proc/self/status");
    std::string   rad;
    while (std::getline(f, rad)) {
        if (rad.rfind("VmRSS:", 0) == 0)
            m.process = std::atof(rad.c_str() + 6) / 1000.0;
        else if (rad.rfind("VmHWM:", 0) == 0)
            m.topp = std::atof(rad.c_str() + 6) / 1000.0;
    }
#endif
    return m;
}

// ------------------------------------------------------------------ energin

Energi läsEnergi()
{
    Energi e;
#if defined(_WIN32)
    FILETIME skapad{}, slut{}, kärna{}, användare{};
    if (GetProcessTimes(GetCurrentProcess(), &skapad, &slut, &kärna, &användare)) {
        const auto tal = [](const FILETIME& f) {
            return static_cast<double>((static_cast<std::uint64_t>(f.dwHighDateTime) << 32) |
                                       f.dwLowDateTime) / 1.0e7;
        };
        e.cpuSek = tal(kärna) + tal(användare);
    }
#else
    rusage r{};
    if (getrusage(RUSAGE_SELF, &r) == 0)
        e.cpuSek = static_cast<double>(r.ru_utime.tv_sec + r.ru_stime.tv_sec) +
                   static_cast<double>(r.ru_utime.tv_usec + r.ru_stime.tv_usec) / 1.0e6;
#    if defined(__APPLE__)
    task_power_info_data_t p{};
    mach_msg_type_number_t antal = TASK_POWER_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_POWER_INFO, reinterpret_cast<task_info_t>(&p),
                  &antal) == KERN_SUCCESS)
        e.väckningar = static_cast<double>(p.task_interrupt_wakeups) +
                       static_cast<double>(p.task_platform_idle_wakeups);
#    endif
#endif
    return e;
}

std::string mb(double v)
{
    if (v < 0.0)
        return "—";
    char b[32];
    std::snprintf(b, sizeof b, "%.0f MB", v);
    return b;
}

// ------------------------------------------------------------ plattornas PNG

namespace {

std::vector<std::vector<std::uint8_t>> byggPng()
{
    std::vector<std::vector<std::uint8_t>> ut;
    for (int n = 0; n < kPngAntal; ++n) {
        glzassets::Rgba bild;
        bild.width  = 256;
        bild.height = 256;
        bild.px.resize(256u * 256u * 4u);
        Slump r{static_cast<std::uint64_t>(n) * 977u + 13u};
        const double fx = r.mellan(20.0, 60.0), fy = r.mellan(20.0, 60.0);
        const int    väg = static_cast<int>(r.mellan(30.0, 220.0));
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x) {
                const double f = 0.5 + 0.5 * std::sin(x / fx + n) * std::cos(y / fy - n);
                const int    b = static_cast<int>(r.nästa() % 24u);
                std::uint8_t* p = &bild.px[(static_cast<std::size_t>(y) * 256u + x) * 4u];
                const bool    påVäg = std::abs(x + y / 3 - väg) < 3;
                p[0] = static_cast<std::uint8_t>(påVäg ? 200 : 60 + 50 * f + b);
                p[1] = static_cast<std::uint8_t>(påVäg ? 195 : 90 + 60 * f + b);
                p[2] = static_cast<std::uint8_t>(påVäg ? 180 : 50 + 30 * f + b);
                p[3] = 255;
            }
        ut.push_back(glzassets::encodePng(bild));
    }
    return ut;
}

} // namespace

const std::vector<std::vector<std::uint8_t>>& pngPool()
{
    static const auto pool = byggPng();
    return pool;
}

glaze::Bitmap avkoda(std::uint64_t nyckel)
{
    const auto& png = pngPool()[blanda(nyckel) % kPngAntal];
    glaze::Bitmap b = glaze::Bitmap::decode(png.data(), png.size());
    b.setKeepPixels(false);   // före posten till UI-tråden, som appen
    return b;
}

void nätet(std::uint64_t nyckel)
{
    std::this_thread::sleep_for(
        std::chrono::milliseconds(25 + static_cast<int>(blanda(nyckel ^ 0x55) % 66u)));
}

double plattM(int nivå) { return 64.0 * std::pow(2.0, 19 - nivå); }

std::array<glaze::Point, 4> plattHörn(double x0, double y0, double sida)
{
    const double cx = x0 + sida / 2, cy = y0 + sida / 2, v = 0.011;
    const double hx[4] = {-sida / 2, sida / 2, sida / 2, -sida / 2};
    const double hy[4] = {sida / 2, sida / 2, -sida / 2, -sida / 2};
    std::array<glaze::Point, 4> hörn;
    for (int h = 0; h < 4; ++h)
        hörn[static_cast<std::size_t>(h)] = {cx + hx[h] * std::cos(v) - hy[h] * std::sin(v),
                                             cy + hx[h] * std::sin(v) + hy[h] * std::cos(v)};
    return hörn;
}

// ------------------------------------------------------------ arbetstrådarna

std::shared_ptr<std::atomic<bool>> g_liv = std::make_shared<std::atomic<bool>>(true);

Arbetare::Arbetare(int trådar)
{
    for (int i = 0; i < trådar; ++i)
        trådar_.emplace_back([this] { slinga(); });
}

Arbetare::~Arbetare() { stoppa(); }

void Arbetare::stoppa()
{
    {
        std::lock_guard<std::mutex> lås(m_);
        stopp_ = true;
        kö_.clear();
    }
    cv_.notify_all();
    for (std::thread& t : trådar_)
        if (t.joinable())
            t.join();
}

void Arbetare::lägg(std::uint64_t nyckel, std::function<void()> jobb)
{
    {
        std::lock_guard<std::mutex> lås(m_);
        kö_.push_back({nyckel, std::move(jobb)});
    }
    cv_.notify_one();
}

std::size_t Arbetare::köade()
{
    std::lock_guard<std::mutex> lås(m_);
    return kö_.size();
}

void Arbetare::slinga()
{
    for (;;) {
        std::function<void()> jobb;
        {
            std::unique_lock<std::mutex> lås(m_);
            cv_.wait(lås, [this] { return stopp_ || !kö_.empty(); });
            if (stopp_)
                return;
            jobb = std::move(kö_.front().second);
            kö_.pop_front();
        }
        jobb();
    }
}

// ------------------------------------------------------------- föremålen
// (canvas_perf3d:s gallerstolpe — rör mellan lådans hörn, fyra nivåer.)

namespace {
constexpr double kNivåAndel[4]   = {1.0, 0.40, 0.15, 0.05};
constexpr double kNivåAvstånd[4] = {60.0, 150.0, 400.0, 0.0};
} // namespace

glaze::Mesh byggStolpe(int trianglar, std::uint64_t frö, int nivåer, bool cpuKopia)
{
    Slump           r{frö};
    glaze::MeshDesc d;
    nivåer = std::clamp(nivåer, 1, 4);
    for (int n = 0; n < nivåer; ++n) {
        const int mål     = std::max(64, static_cast<int>(trianglar * kNivåAndel[n]));
        const int stänger = std::max(4, mål / 600);
        const int punkter = std::max(2, (mål / stänger - 20) / 24 + 1);
        gfx::MeshGeometry g;
        for (int k = 0; k < stänger; ++k) {
            const double h0   = r.mellan(0.0, 26.0);
            const double h1   = h0 + r.mellan(0.5, 2.0);
            const auto   hörn = [&](double h) {
                const double sk = 1.0 - h / 42.0;
                const int    q  = static_cast<int>(r.enhet() * 4.0);
                return gfx::Vec3d{(q & 1 ? 1.0 : -1.0) * sk, (q & 2 ? 1.0 : -1.0) * sk, h};
            };
            const gfx::Vec3d        a = hörn(h0), b = hörn(h1);
            std::vector<gfx::Vec3d> väg;
            for (int i = 0; i < punkter; ++i) {
                const double f = punkter > 1 ? i / double(punkter - 1) : 0.0;
                väg.push_back({a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f + 1e-3 * i,
                               a.z + (b.z - a.z) * f});
            }
            gfx::tube(väg, 0.04 + 0.04 * r.enhet(), g, 12);
        }
        const std::size_t före = d.positions.size();
        for (std::size_t t = 0; t + 2 < g.indices.size(); t += 3) {
            const gfx::Color c{static_cast<std::uint8_t>(120 + (t / 3) % 60), 125, 135, 255};
            for (int k = 0; k < 3; ++k) {
                const std::uint32_t i = g.indices[t + static_cast<std::size_t>(k)];
                d.positions.push_back({static_cast<float>(g.positions[i].x),
                                       static_cast<float>(g.positions[i].y),
                                       static_cast<float>(g.positions[i].z)});
                d.normals.push_back({static_cast<float>(g.normals[i].x),
                                     static_cast<float>(g.normals[i].y),
                                     static_cast<float>(g.normals[i].z)});
                d.colors.push_back(c);
            }
        }
        // Den sista nivån är obegränsad (med fyra nivåer kNivåAvstånd[3]).
        d.lods.push_back(glaze::Lod{static_cast<std::uint32_t>(före),
                                    static_cast<std::uint32_t>(d.positions.size() - före),
                                    n + 1 == nivåer ? 0.0 : kNivåAvstånd[n]});
    }
    d.keepCpuCopy = cpuKopia;
    return glaze::Mesh(std::move(d));
}

glaze::Mesh byggKon(const std::vector<std::pair<int, double>>& nivåer)
{
    glaze::MeshDesc kon;
    for (const auto& [sidor, avstånd] : nivåer) {
        const auto bas  = static_cast<std::uint32_t>(kon.positions.size());
        const auto förs = static_cast<std::uint32_t>(kon.indices.size());
        kon.positions.push_back({0, 0, 8});
        kon.normals.push_back({0, 0, 1});
        for (int i = 0; i < sidor; ++i) {
            const double v = 2 * kPi * i / sidor;
            kon.positions.push_back({static_cast<float>(2.5 * std::cos(v)),
                                     static_cast<float>(2.5 * std::sin(v)), 0.5f});
            kon.normals.push_back({static_cast<float>(std::cos(v)),
                                   static_cast<float>(std::sin(v)), 0.3f});
        }
        for (int i = 0; i < sidor; ++i)
            kon.indices.insert(kon.indices.end(),
                               {bas + 1 + static_cast<std::uint32_t>(i),
                                bas + 1 + static_cast<std::uint32_t>((i + 1) % sidor), bas});
        kon.lods.push_back(glaze::Lod{förs, static_cast<std::uint32_t>(kon.indices.size()) - förs,
                                      avstånd});
    }
    return glaze::Mesh(std::move(kon));
}

// ================================================================ FÖNSTREN

void Fönster::injicera(const glaze::Canvas& yta, gfx::EventType t, double lx, double ly,
                       gfx::MouseButton b, float hjul, std::uint32_t mods)
{
    float            s = 1.0f;
    const gfx::Rectf r = ytansRuta(yta, s);
    gfx::Event       e;
    e.type     = t;
    e.windowId = window().id();
    e.mouseX   = static_cast<float>(r.x + lx * s);
    e.mouseY   = static_cast<float>(r.y + ly * s);
    e.button   = b;
    e.wheelY   = hjul;
    e.mods     = mods;
    input();
    handleEvent(e);
}

void Fönster::tangent(gfx::Key k, bool ner)
{
    gfx::Event e;
    e.type     = ner ? gfx::EventType::KeyDown : gfx::EventType::KeyUp;
    e.windowId = window().id();
    e.key      = k;
    handleEvent(e);
}

void Fönster::input()
{
    input_  = Klocka::now();
    väntar_ = true;
}

void Fönster::ritning(Serie& latens)
{
    if (!väntar_)
        return;
    latens.add(msSedan(input_));
    väntar_ = false;
}

gfx::Rectf ytansRuta(const glaze::Canvas& yta, float& skala)
{
    const glaze::Ctx c = yta.behaviorCtx();
    skala              = c.scale;
    return yta.resolve(c);
}

gfx::Sizef ytansStorlek(const glaze::Canvas& yta)
{
    float            s = 1.0f;
    const gfx::Rectf b = ytansRuta(yta, s);
    return {b.w / s, b.h / s};
}

glaze::Point lissajous(int i, gfx::Sizef y)
{
    return {y.w * (0.5 + 0.42 * std::sin(i * 0.031)), y.h * (0.5 + 0.40 * std::sin(i * 0.047 + 1.0))};
}

bool panorera(Fönster& f, const glaze::Canvas& yta, int i, int drag, double dx, double dy)
{
    const int d = i / kTickPerDrag, k = i % kTickPerDrag;
    if (d >= drag)
        return true;
    const gfx::Sizef y  = ytansStorlek(yta);
    const double     sx = y.w / 2 - dx * 20, sy = y.h / 2 - dy * 20;
    if (k == 0)
        f.injicera(yta, gfx::EventType::MouseButtonDown, sx, sy, gfx::MouseButton::Middle);
    else if (k <= 40)
        f.injicera(yta, gfx::EventType::MouseMotion, sx + dx * k, sy + dy * k);
    else if (k == 41)
        f.injicera(yta, gfx::EventType::MouseButtonUp, sx + dx * 40, sy + dy * 40,
                   gfx::MouseButton::Middle);
    return false;
}

bool förstaperson(const glaze::AppWindow& w, double sek, double fart, glaze::Point& öga,
                  double& gir)
{
    bool         rörd = false;
    const double v    = gir * kPi / 180.0;
    const double fx = std::sin(v), fy = std::cos(v);
    if (w.isKeyHeld(gfx::Key::W) || w.isKeyHeld(gfx::Key::S)) {
        const double d = (w.isKeyHeld(gfx::Key::W) ? 1.0 : -1.0) * fart * sek;
        öga.x += fx * d;
        öga.y += fy * d;
        rörd = true;
    }
    if (w.isKeyHeld(gfx::Key::A) || w.isKeyHeld(gfx::Key::D)) {
        const double d = (w.isKeyHeld(gfx::Key::D) ? 1.0 : -1.0) * fart * 0.5 * sek;
        öga.x += fy * d;
        öga.y -= fx * d;
        rörd = true;
    }
    if (w.isKeyHeld(gfx::Key::Q) || w.isKeyHeld(gfx::Key::E)) {
        gir += (w.isKeyHeld(gfx::Key::E) ? 1.0 : -1.0) * 67.5 * sek;
        rörd = true;
    }
    return rörd;
}

void ritaHud(glaze::Painter& p, const std::string& text, double gir)
{
    const gfx::Sizef s = p.size();
    glaze::TextStyle ts;
    ts.size       = glaze::mm(3.0f);
    ts.anchor     = glaze::TextAnchor::TopLeft;
    ts.color      = gfx::Color{255, 255, 255, 255};
    ts.background = gfx::Color{0, 0, 0, 140};
    ts.padding    = glaze::mm(1.0f);
    ts.collide    = false;
    p.drawText({12, 12}, text, ts);
    const glaze::Stroke kors{.paint = gfx::Color{255, 255, 255, 200}, .width = glaze::px(1.5f)};
    p.drawLine({s.w / 2 - 10, s.h / 2}, {s.w / 2 + 10, s.h / 2}, kors);
    p.drawLine({s.w / 2, s.h / 2 - 10}, {s.w / 2, s.h / 2 + 10}, kors);
    p.fillCircle({s.w - 50, 50}, 28, gfx::Color{0, 0, 0, 120});
    const double v = -gir * kPi / 180.0;
    p.drawLine({s.w - 50, 50}, {s.w - 50 + 22 * std::sin(v), 50 - 22 * std::cos(v)},
               {.paint = gfx::Color{230, 40, 40, 255}, .width = glaze::px(3.0f)});
}

// ------------------------------------------------------- stegens mätning

StegMått* g_steg = nullptr;

void sampla(const char* var)
{
    const double nu = läsMinne().process;
    if (g_steg)
        g_steg->toppSteg = std::max(g_steg->toppSteg, nu);
    static int n = 0;
    if (g_verbose && n < 40) {
        ++n;
        std::printf("       [minne %s: %s]\n", var, mb(nu).c_str());
    }
}

Profil profil(const FönsterMått& f)
{
    Profil                                  p;
    const std::vector<glaze::FrameProfile>& b = f.profilBilder;
    p.bilder = b.size();
    for (std::size_t i = 0; i < b.size(); ++i) {
        if (i > 0 && b[i - 1].continued)
            p.intervall.add(b[i].interval);
        if (b[i].inputLatency > 0.0)
            p.latens.add(b[i].inputLatency);
        for (std::size_t k = 0; k < kFaser; ++k)
            if (k != std::size_t(glaze::ProfilePhase::Gpu) || b[i].ms[k] > 0.0f)
                p.fas[k].add(b[i].ms[k]);
        for (std::size_t k = 0; k < kRäknare; ++k)
            p.perBild[k] += static_cast<double>(b[i].count[k]);
    }
    if (p.bilder > 0)
        for (double& v : p.perBild)
            v /= static_cast<double>(p.bilder);
    return p;
}

// =============================================================== SESSIONEN

Session::Session(std::vector<MättFönster> fönster, std::vector<std::string> strömmar)
    : fönster_(std::move(fönster)), strömmar_(std::move(strömmar))
{
    timer_.setSingleShot(true);
    timer_.onTick = [this] {
        if (!klar_ && g_steg)
            nästa();
    };
    // Frågan går om efter varje svar — en en-gångs-timer som handlern startar
    // om (timer.h), så att den aldrig hinner före steget den frågar för.
    fråga_.setSingleShot(true);
    fråga_.onTick = [this] {
        if (klar_ || !g_steg)
            return;
        const int index = index_;
        driv();
        if (!klar_ && g_steg && index_ == index)
            fråga_.start();
    };
}

void Session::profilera()
{
    profil_ = true;
    glaze::profiler().setCapacity(kProfilRing);
    glaze::profiler().setEnabled(true);
}

void Session::börja()
{
    start_ = Klocka::now();
    nästa();
}

void Session::tick(int vem, double sek)
{
    if (klar_ || !g_steg)
        return;
    FönsterMått& f = g_steg->f[static_cast<std::size_t>(vem)];
    f.frame.add(sek * 1000.0);
    gfx::Renderer& r = fönster_[static_cast<std::size_t>(vem)].fönster->window().renderer();
    const gfx::RenderStats st = r.stats();
    f.anrop += st.drawCalls;
    f.uppByte += st.bytesUploaded;
    f.buffertar += st.buffersCreated;
    f.brott += st.uploadBreaks;
    uppTotalt_ += st.bytesUploaded;
    r.resetStats();
    std::size_t kö = 0;
    for (const MättFönster& m : fönster_)
        if (m.kö)
            kö += m.kö();
    g_steg->maxKö = std::max(g_steg->maxKö, kö);
    const Steg& s = steg_[static_cast<std::size_t>(index_)];
    if (vem == s.driver && s.timerMs == 0 && s.frågaMs == 0)
        driv();
}

void Session::driv()
{
    const Steg& s = steg_[static_cast<std::size_t>(index_)];
    if (msSedan(stegStart_) > s.tak * 1000.0) {
        fel("steget \"" + s.namn + "\" blev inte klart på " +
            std::to_string(static_cast<int>(s.tak)) + " s" +
            (s.vila ? " — strömningen kom inte ikapp" : ""));
        avsluta();
        return;
    }
    if (s.kör(i_++))
        nästa();
}

void Session::nästa()
{
    if (g_steg) {
        g_steg->sekunder = msSedan(stegStart_) / 1000.0;
        g_steg->minne    = läsMinne();
        g_steg->bildcache = 0;
        g_steg->chunkar   = 0;
        for (const MättFönster& m : fönster_) {
            if (!m.fönster->isOpen())
                continue;
            const glaze::Ctx c = m.yta->behaviorCtx();
            g_steg->bildcache += c.images ? c.images->canvasBytes() : 0;
            g_steg->chunkar += c.canvas ? c.canvas->chunkCount() : 0;
        }
        g_steg->uppTotalt = uppTotalt_;
        const Energi e    = läsEnergi();
        const double sek  = std::max(g_steg->sekunder, 1e-3);
        if (e.cpuSek >= 0.0)
            g_steg->cpuProcent = 100.0 * (e.cpuSek - g_steg->energiFöre.cpuSek) / sek;
        if (e.väckningar >= 0.0)
            g_steg->väckPerSek = (e.väckningar - g_steg->energiFöre.väckningar) / sek;
        if (profil_) {
            for (std::size_t w = 0; w < fönster_.size(); ++w)
                if (fönster_[w].fönster->isOpen())
                    g_steg->f[w].profilBilder = glaze::profiler().frames(*fönster_[w].fönster);
            g_steg->pump      = glaze::profiler().pump();
            g_steg->etiketter = glaze::profiler().scopes(true);
        }
        skrivSteg(*g_steg);
    }
    if (index_ + 1 >= static_cast<int>(steg_.size()) || g_fel) {
        avsluta();
        return;
    }
    ++index_;
    i_ = 0;
    mått_.push_back({});
    g_steg       = &mått_.back();
    g_steg->namn = steg_[static_cast<std::size_t>(index_)].namn;
    g_steg->f.resize(fönster_.size());
    for (const std::string& n : strömmar_)
        g_steg->ström.push_back({n});
    stegStart_         = Klocka::now();
    g_steg->energiFöre = läsEnergi();
    const Steg& st     = steg_[static_cast<std::size_t>(index_)];
    g_steg->rörelse    = st.rörelse;
    g_steg->tickar     = st.tickar;
    // Profilerarens ringar börjar om med steget: det som läses när steget
    // slutar är stegets — den första bilden efter stegets sista tick hör hit.
    if (profil_)
        glaze::profiler().clear();
    // Bara ett steg som drivs av tickar tickar.
    const bool tickar = st.timerMs == 0 && st.frågaMs == 0;
    for (std::size_t w = 0; w < fönster_.size(); ++w)
        if (fönster_[w].fönster->isOpen())
            fönster_[w].yta->setTicking(tickar && w < st.tickar.size() && st.tickar[w]);
    if (st.timerMs > 0) {
        st.kör(0);
        timer_.setInterval(st.timerMs);
        timer_.start();
    } else if (st.frågaMs > 0) {
        fråga_.setInterval(st.frågaMs);
        fråga_.start();
    }
}

void Session::avsluta()
{
    klar_  = !g_fel;
    g_steg = nullptr;
    timer_.stop();
    fråga_.stop();
    for (const MättFönster& m : fönster_)
        if (m.fönster->isOpen())
            m.yta->setTicking(false);
    fönster_.front().fönster->close();
}

void Session::skrivSteg(const StegMått& m) const
{
    std::printf("  steg %-30s %6.1f s\n", m.namn.c_str(), m.sekunder);
    for (std::size_t w = 0; w < m.f.size(); ++w) {
        const FönsterMått& f    = m.f[w];
        const char*        namn = fönster_[w].namn.c_str();
        if (f.frame.tom() && f.ritningar == 0)
            continue;
        const double n = std::max<double>(1.0, static_cast<double>(f.frame.v.size()));
        if (f.frame.tom()) {
            // Fönstret tickar inte i steget — det ritar bara när något
            // ändras (3D-kamerans markör på kartan).
            std::printf("       %-6s %4d ritningar utan tick\n", namn, f.ritningar);
            continue;
        }
        std::printf("       %-6s %4zu frames  %5.1f / %5.1f / %6.1f ms  >25 ms %3d  >50 ms %3d",
                    namn, f.frame.v.size(), f.frame.median(), f.frame.kvantil(0.9),
                    f.frame.max(), f.frame.över(25.0), f.frame.över(50.0));
        if (!f.latens.tom())
            std::printf("  input→ritning %4.1f / %4.1f ms", f.latens.median(),
                        f.latens.kvantil(0.9));
        std::printf("  %4.0f anrop/frame  upp %.1f MB  buffertar +%llu  brott +%llu\n",
                    static_cast<double>(f.anrop) / n, static_cast<double>(f.uppByte) / 1.0e6,
                    static_cast<unsigned long long>(f.buffertar),
                    static_cast<unsigned long long>(f.brott));
    }
    bool strömmade = !m.bygg.tom();
    for (const Ström& s : m.ström)
        strömmade = strömmade || s.in + s.ut > 0;
    if (strömmade) {
        std::printf("       ström");
        for (const Ström& s : m.ström)
            std::printf("  %s +%d −%d", s.namn.c_str(), s.in, s.ut);
        std::printf("  kö max %zu  bygg %.2f / %.2f ms (%zu)\n", m.maxKö, m.bygg.median(),
                    m.bygg.max(), m.bygg.v.size());
    }
    if (m.cpuProcent >= 0.0)
        std::printf("       energi CPU %.0f %% av en kärna%s\n", m.cpuProcent,
                    m.väckPerSek >= 0.0
                        ? ("  väckningar " + std::to_string(static_cast<long>(m.väckPerSek)) +
                           "/s").c_str()
                        : "");
    std::printf("       minne  process %s (stegets topp %s, processens %s, resident %s)  GPU %s  "
                "bildcache %s  uppladdat totalt %s  chunkar %zu\n",
                mb(m.minne.process).c_str(), mb(m.toppSteg).c_str(), mb(m.minne.topp).c_str(),
                mb(m.minne.arbete).c_str(), mb(m.minne.gpu).c_str(),
                mb(static_cast<double>(m.bildcache) / 1.0e6).c_str(),
                mb(static_cast<double>(m.uppTotalt) / 1.0e6).c_str(), m.chunkar);
    if (profil_)
        skrivProfil(m);
    std::fflush(stdout);
}

void Session::skrivProfil(const StegMått& m) const
{
    for (std::size_t w = 0; w < m.f.size(); ++w) {
        const Profil p = profil(m.f[w]);
        if (p.bilder == 0)
            continue;
        std::printf("       profil %-6s %4zu bilder  intervall %5.1f / %5.1f / %5.1f / %6.1f ms"
                    "  >20 ms %3d  >6,7 ms %4d",
                    fönster_[w].namn.c_str(), p.bilder, p.intervall.median(),
                    p.intervall.kvantil(0.95), p.intervall.kvantil(0.99), p.intervall.max(),
                    p.intervall.över(20.0), p.intervall.över(1000.0 / 150.0));
        if (!p.latens.tom())
            std::printf("  input→present %4.1f / %4.1f ms", p.latens.median(),
                        p.latens.kvantil(0.95));
        if (p.bilder >= kProfilRing)
            std::printf("  (ringen full — de äldsta bilderna saknas)");
        std::printf("\n              faser p50 ms ");
        for (std::size_t k = 0; k < kFaser; ++k)
            std::printf(" %s %.2f", glaze::profilePhaseName(static_cast<glaze::ProfilePhase>(k)),
                        p.fas[k].median());
        std::printf("\n              per bild    ");
        for (std::size_t k = 0; k < kRäknare; ++k)
            std::printf(" %s %.1f", glaze::profileCountName(static_cast<glaze::ProfileCount>(k)),
                        p.perBild[k]);
        std::printf("\n");
    }
    // PUMPEN: varven i den yttersta pumpen (en nästlad har sina egna poster).
    Serie       händelser, postat, postatMax;
    std::size_t varv = 0, postade = 0;
    for (const glaze::PumpProfile& v : m.pump) {
        if (v.depth > 1)
            continue;
        ++varv;
        postade += v.postedCount;
        händelser.add(v.events);
        postat.add(v.posted);
        postatMax.add(v.postedMax);
    }
    if (varv > 0)
        std::printf("       pump   %zu varv  events %.2f / %.2f ms  posted %.2f / %.2f ms  "
                    "längsta postade %.2f ms  %zu postade\n",
                    varv, händelser.median(), händelser.kvantil(0.95), postat.median(),
                    postat.kvantil(0.95), postatMax.max(), postade);
    for (const glaze::LabelStats& e : m.etiketter)
        if (e.count > 0)
            std::printf("       verktyg %s %llu × %.3f / %.3f ms (max %.2f, totalt %.1f ms)\n",
                        e.name.c_str(), static_cast<unsigned long long>(e.count), e.p50,
                        e.p95, e.maxMs, e.totalMs);
}

} // namespace perf
