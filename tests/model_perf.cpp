// model_perf — MODELLTESTET: en stor byggnadsmodell som RÅDATA, ett föremål
// per fragment, så att ritytan själv får bära den. Scenen följer en verklig
// modells fördelningar: de flesta fragment är små och har få trianglar, de
// flesta trianglarna ligger i fragment om 200–10 000, en geometri används i
// snitt elva gånger, och nästan varje fragment har en egen skala.
//
// Kör:  ./build-test/tests/model_perf [--fragment N] [--lager N] [--sek S]
//                                     [--foton N] [--foto PX] [--budget GB]
//                                     [--synlig] [--hp] [--steg N] [--mål Hz]
//   --fragment N  antalet fragment (förval 100 000)
//   --lager N     modellerna, ett lager var (förval 11)
//   --sek S       sekunder per steg (förval 8)
//   --foton N     unika foton (förval 24), --foto PX deras sida (förval 1024)
//   --budget GB   ritytans minnesbudget (förval 16)
//   --synlig      ett riktigt fönster med vsync (annars dolt)
//   --hp          gfx::setGpuPreference(HighPerformance) före fönstret
//   --steg N      bara steg N (1–5) efter laddningen
//   --mål Hz      exit 1 när ett rörelsestegs scentakt är under målet
// Exit: 0 = alla steg gick · 1 = fönstret öppnades inte eller takten under målet
//
// SCENEN: kvarter av byggnader på ett rutnät. Varje byggnad har våningar med
// bjälklag och ytterväggar (stora föremål — de som skymmer) och rum fyllda
// med fragment. Ett fragment är en mesh ur en POOL (återanvänd, med egen
// placering och skala) eller en EGEN mesh. På varje vägg ligger två ytor i
// VÄGGENS PLAN: en i väggens lager och en i ett senare lager.
//
// STEGEN (varje `--sek` sekunder):
//   1  utifrån   kameran kretsar kring hela modellen
//   2  inne      förstaperson längs en korridor
//   3  strömning kretsar medan former byts med `setShape` varje bild
//   4  pekaren   `hitTest` i varje bild
//   5  stilla    ingenting rör sig — ska rita noll bilder
//
// MÄTT PER STEG: scenens takt (3D-målets ritningar per sekund), fönstrets
// takt, intervallen, bildens faser och räknare ur profileraren, och minnet.

#include <glaze/glaze.h>
#include <glaze/profiler.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "perf/harness.h"

namespace {

using Klocka = std::chrono::steady_clock;

double sekSedan(Klocka::time_point t)
{
    return std::chrono::duration<double>(Klocka::now() - t).count();
}

struct Val {
    int    fragment = 100000;
    int    lager    = 11;
    double sek      = 8.0;
    int    foton    = 24;
    int    foto     = 1024;
    double budgetGb = 16.0;
    bool   synlig   = false;
    bool   hp       = false;
    int    steg     = 0;
    double mål      = 0.0;
};

// Eget frö och egen avbildning — scenen ska vara densamma överallt.
struct Slump {
    std::uint64_t s;
    explicit Slump(std::uint64_t frö) : s(frö * 0x9E3779B97F4A7C15ull + 1) {}
    std::uint64_t nästa()
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    double tal() { return static_cast<double>(nästa() >> 11) / 9007199254740992.0; }
    double mellan(double a, double b) { return a + (b - a) * tal(); }
    int    heltal(int n) { return static_cast<int>(nästa() % static_cast<std::uint64_t>(n)); }
};

// Modellens fördelningar: andelen fragment per intervall, och intervallets
// gränser (värdet dras log-likformigt inom intervallet).
struct Fack {
    double andel, från, till;
};
constexpr Fack kTrianglar[] = {{0.258, 2, 4},      {0.099, 4, 12},    {0.281, 12, 50},
                               {0.139, 50, 200},   {0.169, 200, 1000}, {0.054, 1000, 8000}};
constexpr Fack kDiagonal[]  = {{0.335, 0.01, 0.05}, {0.197, 0.05, 0.2}, {0.185, 0.2, 0.5},
                               {0.090, 0.5, 1},     {0.102, 1, 2},      {0.061, 2, 5},
                               {0.030, 5, 20}};

template <std::size_t N> double dra(Slump& r, const Fack (&f)[N])
{
    double u = r.tal();
    for (const Fack& x : f) {
        if (u < x.andel)
            return x.från * std::pow(x.till / x.från, r.tal());
        u -= x.andel;
    }
    return f[N - 1].från;
}

// En sluten klump med ungefär `trianglar` trianglar och diagonalen 1: en
// knölig ellipsoid, indexerad, med normaler — och uv för `medUv`.
glaze::Mesh klump(int trianglar, std::uint64_t frö, bool medUv)
{
    Slump r(frö);
    const int seg  = std::max(3, static_cast<int>(std::lround(std::sqrt(trianglar / 2.0))));
    const int ring = std::max(2, trianglar / (2 * seg));
    const double ax = r.mellan(0.2, 0.5), ay = r.mellan(0.2, 0.5), az = r.mellan(0.2, 0.5);
    const double knöl = r.mellan(0.0, 0.25), fas = r.mellan(0.0, 6.28);
    glaze::MeshDesc d;
    d.positions.reserve(static_cast<std::size_t>((ring + 1) * (seg + 1)));
    d.normals.reserve(d.positions.capacity());
    constexpr double kPi = 3.14159265358979323846;
    for (int i = 0; i <= ring; ++i) {
        const double v = static_cast<double>(i) / ring, th = v * kPi;
        for (int j = 0; j <= seg; ++j) {
            const double u = static_cast<double>(j) / seg, ph = u * 2.0 * kPi;
            const double k = 1.0 + knöl * std::sin(3.0 * ph + fas) * std::sin(2.0 * th);
            const double x = std::sin(th) * std::cos(ph), y = std::sin(th) * std::sin(ph),
                         z = std::cos(th);
            d.positions.push_back({static_cast<float>(ax * k * x), static_cast<float>(ay * k * y),
                                   static_cast<float>(az * k * z)});
            d.normals.push_back({static_cast<float>(x), static_cast<float>(y),
                                 static_cast<float>(z)});
            if (medUv)
                d.uvs.push_back({static_cast<float>(u), static_cast<float>(v)});
        }
    }
    for (int i = 0; i < ring; ++i)
        for (int j = 0; j < seg; ++j) {
            const auto a = static_cast<std::uint32_t>(i * (seg + 1) + j), b = a + 1,
                       c = a + static_cast<std::uint32_t>(seg + 1), e = c + 1;
            d.indices.insert(d.indices.end(), {a, c, b, b, c, e});
        }
    return glaze::Mesh(std::move(d));
}

// En plan ruta 1 × 1 i xz-planet (en väggyta), med uv.
glaze::Mesh ruta()
{
    glaze::MeshDesc d;
    d.positions = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}};
    d.normals   = {{0, -1, 0}, {0, -1, 0}, {0, -1, 0}, {0, -1, 0}};
    d.uvs       = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    d.indices   = {0, 1, 2, 0, 2, 3};
    return glaze::Mesh(std::move(d));
}

// Ett foto: mjuka fält och ränder, olika per frö.
glaze::Bitmap foto(int sida, std::uint64_t frö)
{
    Slump r(frö);
    std::vector<std::uint8_t> px(static_cast<std::size_t>(sida) * sida * 4);
    const int a = 40 + r.heltal(180), b = 40 + r.heltal(180), c = 40 + r.heltal(180);
    const int rand = 8 + r.heltal(40);
    for (int y = 0; y < sida; ++y) {
        std::uint8_t* rad = &px[static_cast<std::size_t>(y) * sida * 4];
        for (int x = 0; x < sida; ++x) {
            const int t = ((x / rand) + (y / rand)) & 1;
            rad[4 * x + 0] = static_cast<std::uint8_t>((a + (x * 60) / sida + t * 30) & 255);
            rad[4 * x + 1] = static_cast<std::uint8_t>((b + (y * 60) / sida) & 255);
            rad[4 * x + 2] = static_cast<std::uint8_t>((c + t * 40) & 255);
            rad[4 * x + 3] = 255;
        }
    }
    return glaze::Bitmap(sida, sida, px.data());
}

// Modellens läge: stora koordinater, som en riktig modell.
constexpr double kX0 = 674000.0, kY0 = 6580000.0;
// Byggnaden: 40 × 24 m, våningen 3,5 m, och gatan mellan byggnaderna.
constexpr double kHusX = 40.0, kHusY = 24.0, kVåning = 3.5, kGata = 18.0;
constexpr int    kRumX = 5, kRumY = 3;

struct Scen {
    std::vector<std::vector<glaze::Shape>> lager;   // formerna per lager
    std::uint64_t trianglar = 0, unikaTrianglar = 0;
    int           hus = 0, våningar = 0, sida = 0;
    double        bredd = 0.0, djup = 0.0, höjd = 0.0;
    std::vector<glaze::Mesh> pool;
};

// Scenen byggd för `v.fragment` fragment. Byggnaderna och våningarna väljs så
// att ett rum får ungefär 60 fragment.
Scen bygg(const Val& v)
{
    Scen s;
    const int perRum   = 60;
    const int rum      = std::max(1, v.fragment / perRum);
    const int perVån   = kRumX * kRumY;
    s.våningar         = std::clamp(static_cast<int>(std::lround(std::cbrt(rum / double(perVån)) * 2)),
                                    1, 60);
    s.hus              = std::max(1, rum / (perVån * s.våningar));
    s.sida             = std::max(1, static_cast<int>(std::ceil(std::sqrt(double(s.hus)))));
    s.bredd            = s.sida * (kHusX + kGata);
    s.djup             = s.sida * (kHusY + kGata);
    s.höjd             = s.våningar * kVåning;
    const int lager    = std::max(3, v.lager);
    s.lager.resize(static_cast<std::size_t>(lager));

    // POOLEN: en geometri används i snitt elva gånger — och 30 % av
    // fragmenten har en egen.
    Slump r(20260701);
    const int poolAntal = std::max(8, v.fragment / 16);
    s.pool.reserve(static_cast<std::size_t>(poolAntal));
    {
        std::vector<int> tri(static_cast<std::size_t>(poolAntal));
        for (int& t : tri)
            t = static_cast<int>(dra(r, kTrianglar));
        s.pool.resize(static_cast<std::size_t>(poolAntal));
        const unsigned trådar = std::max(1u, std::thread::hardware_concurrency());
        std::vector<std::thread> t;
        for (unsigned k = 0; k < trådar; ++k)
            t.emplace_back([&, k] {
                for (int i = static_cast<int>(k); i < poolAntal; i += static_cast<int>(trådar))
                    s.pool[static_cast<std::size_t>(i)] =
                        klump(tri[static_cast<std::size_t>(i)], 1000 + static_cast<std::uint64_t>(i),
                              i % 7 == 0);
            });
        for (std::thread& x : t)
            x.join();
        for (int x : tri)
            s.unikaTrianglar += static_cast<std::uint64_t>(x);
    }

    std::vector<glaze::Bitmap> foton;
    for (int i = 0; i < v.foton; ++i)
        foton.push_back(foto(v.foto, 77 + static_cast<std::uint64_t>(i)));
    const glaze::Mesh väggyta = ruta();

    // Lagren: 0 stommen (bjälklag, väggar), 1 väggarnas ytor i ett SENARE
    // lager, resten modellernas fragment.
    const int kStomme = 0, kYtor = 1;
    const auto färg = [](Slump& q) {
        return gfx::Color{static_cast<std::uint8_t>(90 + q.heltal(150)),
                          static_cast<std::uint8_t>(90 + q.heltal(150)),
                          static_cast<std::uint8_t>(90 + q.heltal(150)), 255};
    };
    int kvar = v.fragment;
    for (int h = 0; h < s.hus && kvar > 0; ++h) {
        const double hx = kX0 + (h % s.sida) * (kHusX + kGata);
        const double hy = kY0 + (h / s.sida) * (kHusY + kGata);
        for (int f = 0; f < s.våningar && kvar > 0; ++f) {
            const double z = f * kVåning;
            auto& stomme   = s.lager[kStomme];
            // Bjälklaget och fyra ytterväggar.
            const glaze::MeshStyle betong{.color = gfx::Color{170, 170, 172, 255}};
            stomme.push_back(glaze::Shape::box({{hx, hy, z}, {hx + kHusX, hy + kHusY, z + 0.3}},
                                               betong));
            const double t = 0.25, zt = z + kVåning;
            const glaze::Box3 väggar[4] = {
                {{hx, hy, z + 0.3}, {hx + kHusX, hy + t, zt}},
                {{hx, hy + kHusY - t, z + 0.3}, {hx + kHusX, hy + kHusY, zt}},
                {{hx, hy, z + 0.3}, {hx + t, hy + kHusY, zt}},
                {{hx + kHusX - t, hy, z + 0.3}, {hx + kHusX, hy + kHusY, zt}}};
            for (const glaze::Box3& b : väggar)
                stomme.push_back(glaze::Shape::box(b, betong));
            s.trianglar += 5 * 12;
            // I VÄGGENS PLAN (insidan av den första väggen): en yta i samma
            // lager och ett foto i ett senare.
            for (int k = 0; k < 4; ++k) {
                const double x = hx + 2.0 + k * 9.0;
                glaze::Placement p{.at = {x, hy + t, z + 0.6}, .scale = {6.0f, 1.0f, 2.2f}};
                stomme.push_back(glaze::Shape::mesh(
                    väggyta, p,
                    glaze::MeshStyle{.color = gfx::Color{200, 180, 120, 255}, .lit = false}));
                p.at.x += 1.5;
                p.scale = {3.0f, 1.0f, 1.6f};
                glaze::MeshStyle ms{.lit = false};
                if (!foton.empty())
                    ms.texture = foton[static_cast<std::size_t>((h * 31 + f * 7 + k) %
                                                                static_cast<int>(foton.size()))];
                s.lager[kYtor].push_back(glaze::Shape::mesh(väggyta, p, ms));
                s.trianglar += 4;
            }
            // Rummen.
            for (int ry = 0; ry < kRumY && kvar > 0; ++ry)
                for (int rx = 0; rx < kRumX && kvar > 0; ++rx) {
                    const double x0 = hx + 0.5 + rx * (kHusX - 1.0) / kRumX;
                    const double y0 = hy + 0.5 + ry * (kHusY - 1.0) / kRumY;
                    const double bx = (kHusX - 1.0) / kRumX - 0.2, by = (kHusY - 1.0) / kRumY - 0.2;
                    for (int i = 0; i < perRum && kvar > 0; ++i, --kvar) {
                        const double diag = dra(r, kDiagonal);
                        const int    L    = 2 + r.heltal(lager - 2);
                        glaze::Placement p;
                        p.at    = {x0 + r.tal() * bx, y0 + r.tal() * by,
                                   z + 0.35 + r.tal() * (kVåning - 0.8)};
                        p.yaw   = static_cast<float>(r.mellan(0, 360));
                        const auto sk = static_cast<float>(diag);
                        p.scale = {sk, sk, sk};
                        glaze::MeshStyle ms{.color = färg(r)};
                        glaze::Mesh      m;
                        int              tri = 0;
                        if (r.tal() < 0.3) {
                            tri = static_cast<int>(dra(r, kTrianglar));
                            m   = klump(tri, 900000 + static_cast<std::uint64_t>(kvar), false);
                            s.unikaTrianglar += static_cast<std::uint64_t>(tri);
                        } else {
                            const int pi = r.heltal(poolAntal);
                            m            = s.pool[static_cast<std::size_t>(pi)];
                            if (pi % 7 == 0 && !foton.empty())
                                ms.texture = foton[static_cast<std::size_t>(
                                    pi % static_cast<int>(foton.size()))];
                        }
                        if (r.tal() < 0.002)
                            ms.opacity = 0.4f;
                        s.lager[static_cast<std::size_t>(L)].push_back(
                            glaze::Shape::mesh(std::move(m), p, std::move(ms)));
                    }
                }
        }
    }
    return s;
}

struct Fönster : glaze::AppWindow {
    glaze::Canvas yta;
    Val           v;
    Scen          scen;
    int           steg = -1, i = 0, första = 0, sista = 5;
    Klocka::time_point stegStart{};
    double        laddSek = 0.0, byggSek = 0.0;
    bool          fällt   = false;
    Slump         r{4711};
    glaze::Timer  vila;

    explicit Fönster(const Val& val) : v(val)
    {
        setTitle("model_perf");
        yta.name = "yta";
        yta.dock = glaze::Dock::Fill;
        yta.setBorder(false);
        root().adopt(yta);
        setContentSize(1600, 900);
        if (!v.synlig)
            setOffscreen(true);
        yta.setMemoryBudget(static_cast<std::size_t>(v.budgetGb * 1024.0 * 1024.0 * 1024.0));
        yta.onTick = [this](glaze::Widget*, glaze::TickArgs&) { tick(); };
        onShow     = [this] { börja(); };
        vila.onTick = [this] { nästa(); };
    }

    glaze::Point3 mitt() const
    {
        return {kX0 + scen.bredd / 2.0, kY0 + scen.djup / 2.0, scen.höjd / 2.0};
    }

    void börja()
    {
        Klocka::time_point t0 = Klocka::now();
        scen                  = bygg(v);
        byggSek               = sekSedan(t0);
        std::size_t former    = 0;
        t0                    = Klocka::now();
        for (std::size_t L = 0; L < scen.lager.size(); ++L) {
            const int li = L == 0 ? 0 : yta.addLayer("modell");
            yta.setLayerPlacement(li, glaze::LayerPlacement::World);
            former += scen.lager[L].size();
            yta.setShapes(li, scen.lager[L]);
        }
        laddSek = sekSedan(t0);
        const double r0 = std::max({scen.bredd, scen.djup, scen.höjd});
        yta.setCamera(glaze::Camera3D{.target = mitt(), .distance = r0 * 1.4,
                                      .azimuth = 30, .elevation = 30});
        const perf::Minne m = perf::läsMinne();
        std::printf("scenen: %d byggnader × %d våningar, %zu former i %zu lager\n"
                    "  byggd på %.1f s, lagd i ritytan på %.1f s — processen %s, GPU %s\n",
                    scen.hus, scen.våningar, former, scen.lager.size(), byggSek, laddSek,
                    perf::mb(m.process).c_str(), perf::mb(m.gpu).c_str());
        std::fflush(stdout);
        glaze::profiler().setCapacity(std::size_t{1} << 15);
        glaze::profiler().setEnabled(true);
        första = v.steg > 0 ? v.steg : 0;
        sista  = v.steg > 0 ? v.steg : 5;
        steg   = 0;   // uppladdningen
        starta();
    }

    void starta()
    {
        i         = 0;
        stegStart = Klocka::now();
        glaze::profiler().clear();
        if (steg == 5) {
            yta.setTicking(false);
            vila.setSingleShot(true);
            vila.setInterval(static_cast<int>(v.sek * 1000.0));
            vila.start();
        } else {
            yta.setTicking(true);
            yta.invalidate();
        }
    }

    static const char* namn(int s)
    {
        static const char* n[] = {"0 uppladdning", "1 utifrån", "2 inne",
                                  "3 strömning",   "4 pekaren", "5 stilla"};
        return n[s];
    }

    void rapport()
    {
        const double                           sek = sekSedan(stegStart);
        const std::vector<glaze::FrameProfile> b   = glaze::profiler().frames(*this);
        perf::Serie                            iv;
        std::array<double, std::size_t(glaze::ProfilePhase::Count_)> fas{};
        std::array<double, std::size_t(glaze::ProfileCount::Count_)> räkn{};
        for (const glaze::FrameProfile& f : b) {
            if (f.interval > 0)
                iv.add(f.interval);
            for (std::size_t k = 0; k < fas.size(); ++k)
                fas[k] += f.ms[k];
            for (std::size_t k = 0; k < räkn.size(); ++k)
                räkn[k] += static_cast<double>(f.count[k]);
        }
        const double n     = std::max<std::size_t>(1, b.size());
        const double scen1 = räkn[std::size_t(glaze::ProfileCount::TargetRenders)];
        const auto   F     = [&](glaze::ProfilePhase p) { return fas[std::size_t(p)] / n; };
        const auto   R     = [&](glaze::ProfileCount c) { return räkn[std::size_t(c)] / n; };
        const perf::Minne           m = perf::läsMinne();
        const glaze::CanvasMemoryUse k = yta.memoryUsed();
        std::printf("steg %-14s %6.1f s  scen %7.2f Hz  fönster %7.2f Hz  "
                    "intervall p50 %.1f p95 %.1f max %.1f ms\n",
                    namn(steg), sek, scen1 / sek, static_cast<double>(b.size()) / sek,
                    iv.tom() ? 0.0 : iv.median(), iv.tom() ? 0.0 : iv.kvantil(0.95),
                    iv.tom() ? 0.0 : iv.max());
        std::printf("    faser ms/bild: innehåll %.2f  förbered %.2f  rita %.2f  "
                    "present %.2f  väntan %.2f  gpu %.2f\n",
                    F(glaze::ProfilePhase::Content), F(glaze::ProfilePhase::CanvasPrepare),
                    F(glaze::ProfilePhase::CanvasDraw), F(glaze::ProfilePhase::Present),
                    F(glaze::ProfilePhase::PresentWait), F(glaze::ProfilePhase::Gpu));
        std::printf("    per bild: anrop %.0f  uppladdat %.2f MB  vräkt %.1f — "
                    "processen %s  GPU %s  kontot gpu %.0f MB cpu %.0f MB\n",
                    R(glaze::ProfileCount::DrawCalls),
                    R(glaze::ProfileCount::BytesUploaded) / 1048576.0,
                    R(glaze::ProfileCount::Evictions), perf::mb(m.process).c_str(),
                    perf::mb(m.gpu).c_str(), static_cast<double>(k.gpu) / 1048576.0,
                    static_cast<double>(k.cpu) / 1048576.0);
        std::fflush(stdout);
        if (v.mål > 0.0 && steg >= 1 && steg <= 4 && scen1 / sek < v.mål) {
            std::printf("    FEL  scenens takt under målet %.0f Hz\n", v.mål);
            fällt = true;
        }
    }

    void nästa()
    {
        rapport();
        do {
            ++steg;
        } while (steg <= 5 && (steg < första || steg > sista));
        if (steg > 5) {
            yta.setTicking(false);
            close();
            return;
        }
        starta();
    }

    void tick()
    {
        if (steg < 0 || steg >= 5)
            return;
        const double t  = sekSedan(stegStart);
        const double r0 = std::max({scen.bredd, scen.djup, scen.höjd});
        ++i;
        switch (steg) {
        case 0: {
            // Klart när ingenting laddats upp på en stund (eller taket).
            const std::vector<glaze::FrameProfile> b = glaze::profiler().frames(*this, 30);
            std::uint64_t upp = 0;
            for (const glaze::FrameProfile& f : b)
                upp += f.count[std::size_t(glaze::ProfileCount::BytesUploaded)];
            if ((b.size() >= 30 && upp == 0 && t > 1.0) || t > 600.0)
                nästa();
            else
                yta.invalidate();
            return;
        }
        case 1:
        case 3:
            yta.setCamera(glaze::Camera3D{.target = mitt(), .distance = r0 * 1.4,
                                          .azimuth = static_cast<float>(30.0 + t * 20.0),
                                          .elevation = 30});
            if (steg == 3)
                strömma();
            break;
        case 2: {
            // Genom den första byggnadens mittvåning, fram och tillbaka.
            const double fram = 0.5 + 0.45 * std::sin(t * 0.5);
            const glaze::Point3 öga{kX0 + 1.0 + fram * (kHusX - 2.0), kY0 + kHusY / 2.0,
                                    (scen.våningar / 2) * kVåning + 1.7};
            yta.setCamera(glaze::Camera3D::lookFrom(öga, static_cast<float>(90.0 + 40.0 * std::sin(t)),
                                                    0.0f));
            break;
        }
        case 4: {
            yta.setCamera(glaze::Camera3D{.target = mitt(), .distance = r0 * 0.6,
                                          .azimuth = static_cast<float>(30.0 + t * 5.0),
                                          .elevation = 25});
            const gfx::Sizef sz = yta.actualSize();
            (void)yta.hitTest(glaze::Point{sz.w * (0.3 + 0.4 * r.tal()), sz.h * (0.3 + 0.4 * r.tal())},
                              glaze::HitOptions{.occlusion = true});
            break;
        }
        default:
            break;
        }
        if (t >= v.sek)
            nästa();
    }

    // STRÖMNINGEN: 40 former per bild byter stil på plats — som en visare som
    // läser in och byter ut delar medan kameran rör sig.
    void strömma()
    {
        for (int k = 0; k < 40; ++k) {
            const std::size_t L = 2 + static_cast<std::size_t>(r.heltal(
                                          static_cast<int>(scen.lager.size()) - 2));
            std::vector<glaze::Shape>& f = scen.lager[L];
            if (f.empty())
                continue;
            const std::size_t at = static_cast<std::size_t>(r.heltal(static_cast<int>(f.size())));
            glaze::MeshStyle  ms = f[at].meshStyle();
            ms.color.r           = static_cast<std::uint8_t>(ms.color.r + 40);
            f[at]                = glaze::Shape::mesh(f[at].mesh(), f[at].placement(), ms);
            yta.setShape(static_cast<int>(L), static_cast<int>(at), f[at]);
        }
    }
};

} // namespace

int main(int argc, char** argv)
{
    Val v;
    for (int a = 1; a < argc; ++a) {
        const auto tal = [&](double& ut) {
            if (a + 1 < argc)
                ut = std::atof(argv[++a]);
        };
        const auto hel = [&](int& ut) {
            if (a + 1 < argc)
                ut = std::atoi(argv[++a]);
        };
        if (!std::strcmp(argv[a], "--fragment")) hel(v.fragment);
        else if (!std::strcmp(argv[a], "--lager")) hel(v.lager);
        else if (!std::strcmp(argv[a], "--sek")) tal(v.sek);
        else if (!std::strcmp(argv[a], "--foton")) hel(v.foton);
        else if (!std::strcmp(argv[a], "--foto")) hel(v.foto);
        else if (!std::strcmp(argv[a], "--budget")) tal(v.budgetGb);
        else if (!std::strcmp(argv[a], "--steg")) hel(v.steg);
        else if (!std::strcmp(argv[a], "--mål")) tal(v.mål);
        else if (!std::strcmp(argv[a], "--synlig")) v.synlig = true;
        else if (!std::strcmp(argv[a], "--hp")) v.hp = true;
    }
    if (v.hp)
        gfx::setGpuPreference(gfx::GpuPreference::HighPerformance);
    std::printf("=== model_perf — %d fragment i %d lager ===\n", v.fragment, v.lager);
    std::fflush(stdout);
    glaze::app().setCanvasUploadBudget(std::size_t{256} << 20);
    Fönster f(v);
    f.run();
    return f.fällt ? 1 : 0;
}
