// profiler_gate — PROFILERAREN: bilderna och pumpvarven
// genom den RIKTIGA pumpen, etiketterna från flera trådar, rapporten och
// GPU-tiden.
//
// Kör:  ./build/tests/profiler_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = ett block kunde inte
//       köras (ingen display, eller P8 där GPU-tiden inte mäts) — ctest
//       räknar SKIPPED, aldrig grönt
//
//   P1 AVSTÄNGD — fem bilder genom pumpen ger `frames()` och `pump()` tomma.
//   P2 BILDERNA — tre bilder i ett dolt fönster ger tre poster med fönstrets
//      id, `Content` > 0 och `total` ≥ innehåll + overlay, och Σ DrawCalls
//      över dem = renderarens `stats().drawCalls` mätt av grinden före och
//      efter. Sju postade funktioner och steget som postade dem ger
//      Σ postedCount = 8 i pumpvarven, alla på djupet 1.
//   P3 TRÅDARNA — fyra trådar × 10 000 scopes på samma etikett ger exakt
//      40 000, medan en femte tråd läser `scopes()` hela tiden; samma namn ur
//      fyra olika buffertar ger samma id.
//   P4 FÖLJDEN — en tickande rityta: tjugo bilder, de nitton första med
//      `continued`. Den nittonde ticken stänger av tickningen, och bilden
//      efter den har `continued` = false (kontrollen körs av en glaze::Timer,
//      så att inget postat ligger i kön när bilden mäts).
//   P6 RAPPORTEN — på: fönstrets titel och fasnamnen; av: av-raden.
//   P8 GPU-TIDEN — tjugo dolda bilder ger minst en bild med `ms[Gpu]` > 0.
//      SKIPPED (77) på en programvaru-GPU (WARP, llvmpipe) och där backenden
//      inte kan mäta (`Renderer::gpuTimingSupported`).
//
// Kostnaden per scope (av och på) och klockans upplösning SKRIVS UT men
// grindas inte.
//
// SABOTAGEN som ska fälla den (skrivna):
//   * `profileFrameBegin` och `profilePumpBegin` utan `profiling()` —
//     inspelning oavsett flaggan ............................................ P1
//   * bildens räknare ur ett ANNAT fönsters renderare, eller `resetStats()`
//     när bilden öppnas ..................................................... P2
//   * `PostedCall` räknar inte `postedCount` ........................ P2 (0 ≠ 8)
//   * etiketten interneras på namnets ADRESS i stället för innehållet .... P3
//   * ett block slås ihop eller skrivs utan sitt lås ....................... P3
//   * `continued` alltid sann ............................................... P4
//   * `report()` utan titeln, eller utan av-raden ........................... P6
//   * `present` avslutar aldrig framens fråga (D3D11 `End(end)` bort) ...... P8
//
// VAD DEN INTE MÄTER, skrivet så anspråket inte överdrivs: klockans
// upplösning och kostnaden (de skrivs ut); tiderna i sak (att 3 ms är 3 ms);
// att GPU-tiden är RÄTT och inte bara positiv; vsync och `PresentWait` (ett
// dolt fönster väntar inte — det läses i `terrain_perf --synlig`); en bild
// utanför pumpen (D3D11:s modala storleksloop); den nästlade pumpens
// avräkning (ingen modal i grinden); ritytans faser och räknare i sak; Metal
// och GL (doc/os/*/TESTS.md).

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <glaze/glaze.h>

namespace {

int  g_fails   = 0;
int  g_cases   = 0;
int  g_skips   = 0;
bool g_verbose = false;

void check(bool ok, const std::string& what, const std::string& fick = "")
{
    ++g_cases;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", what.c_str());
        return;
    }
    std::printf("  FEL  %s%s\n", what.c_str(),
                fick.empty() ? "" : (" — fick " + fick).c_str());
    ++g_fails;
}

std::string tal(std::uint64_t v) { return std::to_string(v); }

using Fas  = glaze::ProfilePhase;
using Räkn = glaze::ProfileCount;

float fas(const glaze::FrameProfile& f, Fas p) { return f.ms[std::size_t(p)]; }
std::uint64_t räkn(const glaze::FrameProfile& f, Räkn c)
{
    return f.count[std::size_t(c)];
}

// ================================================================ HEADLESS

constexpr int kTrådar      = 4;
constexpr int kPerTråd     = 10000;
constexpr const char* kTrådEtikett = "profiler_gate.trådar";

// P3 — etiketterna från flera trådar, med en läsare som tar blockens lås
// under tiden.
void fallTrådar()
{
    glaze::Profiler& p = glaze::profiler();
    p.setEnabled(true);
    p.scopes(true);   // nollställt

    std::atomic<bool> klar{false};
    std::atomic<int>  läsningar{0};
    std::thread       läsare([&] {
        while (!klar) {
            p.scopes();
            ++läsningar;
        }
    });
    std::vector<std::uint32_t> ids(kTrådar, 0);
    std::vector<std::thread>   trådar;
    for (int t = 0; t < kTrådar; ++t)
        trådar.emplace_back([&ids, t] {
            // En egen buffert per tråd — en interning på adressen hade gett
            // fyra id.
            const std::string         namn = std::string(kTrådEtikett);
            const glaze::ProfileLabel etikett{namn};
            ids[static_cast<std::size_t>(t)] = etikett.id();
            for (int i = 0; i < kPerTråd; ++i) {
                const glaze::ProfileScope mät(etikett);
            }
        });
    for (std::thread& t : trådar)
        t.join();
    klar = true;
    läsare.join();

    std::uint64_t antal = 0;
    for (const glaze::LabelStats& s : p.scopes())
        if (s.name == kTrådEtikett)
            antal = s.count;
    check(antal == std::uint64_t{kTrådar} * kPerTråd,
          "P3 fyra trådar × 10 000 scopes ger exakt 40 000 (läsaren läste " +
              std::to_string(läsningar.load()) + " gånger under tiden)",
          tal(antal));
    bool samma = true;
    for (const std::uint32_t id : ids)
        samma = samma && id == ids[0];
    check(samma && ids[0] == glaze::ProfileLabel{kTrådEtikett}.id(),
          "P3 samma namn ur fyra buffertar i fyra trådar ger samma id");
    p.setEnabled(false);
}

// Kostnaden per scope och klockans upplösning — skrivs ut, grindas inte.
void kostnaden()
{
    using Klocka = std::chrono::steady_clock;
    constexpr int kAv = 1000000, kPå = 100000;
    const glaze::ProfileLabel etikett{"profiler_gate.kostnad"};
    glaze::Profiler&          p = glaze::profiler();
    p.setEnabled(false);
    const auto t0 = Klocka::now();
    for (int i = 0; i < kAv; ++i) {
        const glaze::ProfileScope mät(etikett);
    }
    const auto t1 = Klocka::now();
    p.setEnabled(true);
    for (int i = 0; i < kPå; ++i) {
        const glaze::ProfileScope mät(etikett);
    }
    const auto t2 = Klocka::now();
    p.setEnabled(false);
    auto upplösning = Klocka::duration::max();
    for (int i = 0; i < 100; ++i) {
        const auto a = Klocka::now();
        auto       b = Klocka::now();
        while (b == a)
            b = Klocka::now();
        upplösning = std::min(upplösning, b - a);
    }
    const auto ns = [](Klocka::duration d) {
        return std::chrono::duration<double, std::nano>(d).count();
    };
    std::printf("  info en scope kostar %.1f ns avstängd och %.1f ns påslagen; "
                "klockans upplösning %.0f ns\n",
                ns(t1 - t0) / kAv, ns(t2 - t1) / kPå, ns(upplösning));
}

// ============================================================= MED FÖNSTER

struct Fönster : glaze::AppWindow {
    glaze::Canvas yta;
    glaze::Timer  stopp;   // P4:s kontroll — inget postat i kön
    int           tick = 0;

    Fönster() : glaze::AppWindow("profiler_gate", 400, 300, gfx::Chrome::None)
    {
        yta.name   = "yta";
        yta.x      = 10;
        yta.y      = 10;
        yta.width  = 200;
        yta.height = 150;
        root().adopt(yta);
    }
};

// N BILDER GENOM PUMPEN — fönstret invalideras och funktionen postar sig
// själv igen, så pumpen ritar en bild per varv utan att somna. När N bilder
// ritats postas `sedan`, som körs två varv efter den sista bilden.
void bilder(Fönster& w, int n, std::function<void()> sedan)
{
    if (n == 0) {
        glaze::app().post(std::move(sedan));
        return;
    }
    w.invalidate();
    glaze::app().post([&w, n, sedan = std::move(sedan)]() mutable {
        bilder(w, n - 1, std::move(sedan));
    });
}

// Antalet bilder med en GPU-tid.
int medGpu(const std::vector<glaze::FrameProfile>& v)
{
    int n = 0;
    for (const glaze::FrameProfile& f : v)
        n += fas(f, Fas::Gpu) > 0.0f ? 1 : 0;
    return n;
}

bool mjukvara(const std::string& gpu)
{
    return gpu.find("Basic Render Driver") != std::string::npos ||
           gpu.find("llvmpipe") != std::string::npos ||
           gpu.find("softpipe") != std::string::npos;
}

// P8 — GPU-tiden.
void steg8(Fönster& w)
{
    glaze::Profiler&  p   = glaze::profiler();
    gfx::Renderer&    r   = w.window().renderer();
    const std::string gpu = r.deviceName();
    const auto        v   = p.frames(w);
    if (mjukvara(gpu) || !r.gpuTimingSupported()) {
        std::printf("  --   P8 hoppas över: %s mäts inte (%s)\n", gpu.c_str(),
                    r.gpuTimingSupported() ? "programvaru-GPU"
                                           : "backenden kan inte mäta");
        ++g_skips;
    } else {
        check(v.size() == 20 && medGpu(v) >= 1,
              "P8 tjugo dolda bilder ger minst en bild med GPU-tiden > 0 (" +
                  gpu + ")",
              std::to_string(v.size()) + " bilder, " +
                  std::to_string(medGpu(v)) + " med GPU-tid");
    }
    p.setEnabled(false);
    w.close();
}

// P4:s kontroll (körd av timern) och P6, sedan P8.
void steg5(Fönster& w)
{
    glaze::Profiler& p = glaze::profiler();
    const auto       v = p.frames(w);
    int              följd = 0;
    for (std::size_t i = 0; i + 1 < v.size(); ++i)
        följd += v[i].continued ? 1 : 0;
    check(v.size() == 20 && följd == 19 && !v.back().continued,
          "P4 en tickande rityta: 20 bilder, de 19 första med continued, och "
          "bilden efter setTicking(false) utan",
          std::to_string(v.size()) + " bilder, " + std::to_string(följd) +
              " med continued, den sista " +
              (v.empty() ? "saknas" : v.back().continued ? "med" : "utan"));

    // P6 — rapporten, på och av.
    const std::string på = p.report();
    check(på.find("profiler_gate") != std::string::npos &&
              på.find("content") != std::string::npos &&
              på.find("wait") != std::string::npos,
          "P6 rapporten (på) bär fönstrets titel och fasnamnen", på);
    p.setEnabled(false);
    const std::string av = p.report();
    check(av.rfind("profileraren: av", 0) == 0,
          "P6 rapporten (av) är av-raden", av);

    // P8 — tjugo bilder med tidtagningen på.
    p.setEnabled(true);
    p.clear();
    bilder(w, 20, [&w] { steg8(w); });
}

// P2:s pumpvarv, sedan P4:s början.
void steg4(Fönster& w)
{
    glaze::Profiler& p = glaze::profiler();
    std::uint64_t    postade = 0;
    bool             djup1   = true;
    for (const glaze::PumpProfile& v : p.pump()) {
        postade += v.postedCount;
        djup1 = djup1 && v.depth == 1;
    }
    check(postade == 8 && djup1,
          "P2 sju postade funktioner och steget som postade dem: Σ postedCount "
          "= 8, alla varv på djupet 1",
          tal(postade) + (djup1 ? "" : ", ett varv på ett annat djup"));

    // P4 — tickningen. Den nittonde ticken stänger av den; bilden efter den
    // ska inte ha continued, och kontrollen kommer med timern.
    p.clear();
    w.tick       = 0;
    w.yta.onTick = [&w](glaze::Widget*, glaze::TickArgs&) {
        if (++w.tick == 19) {
            w.yta.setTicking(false);
            w.stopp.start();
        }
    };
    w.stopp.setSingleShot(true);
    w.stopp.setInterval(glaze::Timer::kMinIntervalMs);
    w.stopp.onTick = [&w] { steg5(w); };
    w.yta.setTicking(true);
}

// P2:s bilder, därefter dess postade funktioner.
void steg3(Fönster& w, std::uint64_t ritanrop0)
{
    glaze::Profiler&    p         = glaze::profiler();
    const auto          v         = p.frames(w);
    const gfx::WindowId id        = w.window().id();
    const std::uint64_t ritanrop1 = w.window().renderer().stats().drawCalls;
    bool                fönstret = true, innehåll = true, totalt = true;
    std::uint64_t       summa    = 0;
    for (const glaze::FrameProfile& f : v) {
        fönstret = fönstret && f.window == id;
        innehåll = innehåll && fas(f, Fas::Content) > 0.0f;
        totalt   = totalt && f.total + 1.0e-3 >=
                               fas(f, Fas::Content) + fas(f, Fas::Overlay);
        summa += räkn(f, Räkn::DrawCalls);
    }
    check(v.size() == 3 && fönstret,
          "P2 tre bilder ger tre poster med fönstrets id",
          std::to_string(v.size()) + " poster");
    check(innehåll && totalt,
          "P2 varje bild: Content > 0 och total ≥ innehåll + overlay");
    check(summa == ritanrop1 - ritanrop0,
          "P2 Σ DrawCalls över bilderna = renderarens räknare före och efter",
          tal(summa) + " mot " + tal(ritanrop1 - ritanrop0));

    // Sju postade funktioner — den sjunde postar kontrollen.
    p.clear();
    for (int i = 0; i < 7; ++i)
        glaze::app().post([&w, i] {
            if (i == 6)
                glaze::app().post([&w] { steg4(w); });
        });
}

// P1:s kontroll, sedan P2:s tre bilder.
void steg2(Fönster& w)
{
    glaze::Profiler& p = glaze::profiler();
    check(p.frames(w).empty() && p.pump().empty(),
          "P1 avstängd: fem bilder genom pumpen ger frames() och pump() tomma",
          std::to_string(p.frames(w).size()) + " bilder, " +
              std::to_string(p.pump().size()) + " varv");
    p.setEnabled(true);
    p.clear();
    const std::uint64_t ritanrop0 = w.window().renderer().stats().drawCalls;
    bilder(w, 3, [&w, ritanrop0] { steg3(w, ritanrop0); });
}

void fallMedFönster()
{
    Fönster w;
    w.setOffscreen(true);
    bool kördes = false;
    // Första varvet ritar fönstrets första bild; P1 börjar i nästa.
    glaze::app().post([&] {
        kördes = true;
        glaze::app().post([&w] {
            glaze::profiler().setEnabled(false);
            glaze::profiler().clear();
            bilder(w, 5, [&w] { steg2(w); });
        });
    });
    w.run();
    if (!kördes) {
        std::printf("  --   fönsterfallen hoppades över: inget fönster kunde "
                    "skapas (ingen display)\n");
        ++g_skips;
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("profiler_gate\n");
    fallTrådar();
    kostnaden();
    fallMedFönster();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
