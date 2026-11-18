// scene_context_gate — SCEN-KONTEXTEN OCH DE DELADE BILDERNA, mätt i pixlar
// genom den riktiga backenden (gfx/scene_context.h).
//
// VAD GRINDEN PÅSTÅR — och inget mer:
//
//   S1  En ANNAN TRÅD ritar en färg i en delad bild med scen-kontextens
//       renderare och publicerar den; fönstrets renderare tar den, ritar den
//       i en frame, och den fångade framen har färgen.
//   S2  `acquire()` VÄNTAR INTE: på en bild som aldrig publicerats, och på en
//       som scentråden håller för skrivning, svarar den nullptr inom
//       `kDirektMs`.
//   S3  Hundra bilder i följd genom TRE delade bilder, var och en med sin
//       färg: varje fångad frame är ENFÄRGAD i just den färgen (hörnen och
//       mitten) — aldrig en halvritad eller gammal bild.
//   S4  Rivningen i fasadens ordning — fönstret lämnar tillbaka bilden det
//       håller, scentråden river bilderna och sedan kontexten, fönstret
//       stängs sist — går igenom, och en lämnad bild kan inte tas igen.
//
// Grinden mäter INTE: MSAA och djup i en delad bild (de finns inte — ett
// sådant mål ritas på scen-sidan och läggs in), hur länge `publish` väntar
// på GPU:n, och andra backends än den som körs (doc/os/*/TESTS.md).
//
// VÄGEN är glaze-renders: `setOffscreen(true)` → `show()` → `renderFrame()`
// → `captureFrame()`.
//
// INGEN TYST SKIP (clip_gate-beslutet): utan fönster eller utan scen-kontext
// FALLER grinden — backenden lovar en, och en grind som hoppar mäter inget.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <glaze/glaze.h>

namespace {

int  g_fel     = 0;
bool g_verbose = false;

void check(bool ok, const std::string& vad, const std::string& fick = {})
{
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", vad.c_str());
        return;
    }
    std::printf("  FEL  %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                fick.c_str());
    ++g_fel;
}

// Tal i en testfil är grindens mätpunkter.
constexpr int kFönsterW = 200;
constexpr int kFönsterH = 160;
constexpr int kBilder   = 3;     // de delade bilderna i växling
constexpr int kVarv     = 100;   // S3
constexpr int kTolerans = 2;     // per kanal
// "Väntar inte": en fråga som svarar inom den här tiden har inte väntat på
// GPU:n eller på den andra tråden (scentrådens väntan är sekunder).
constexpr double kDirektMs = 50.0;

// Rutan som fyller fönstret och ritar den delade bilden — eller ingenting.
class Bildruta : public glaze::Widget {
public:
    gfx::Image* bild = nullptr;

    void draw(const glaze::Ctx& c) override
    {
        if (bild)
            c.r->drawImage(*bild, c.box, 1.0f);
    }
};

class Fixtur : public glaze::AppWindow {
public:
    Fixtur()
    {
        setTitle("scene_context_gate");
        setChrome(gfx::Chrome::None);
        setClientSize(kFönsterW, kFönsterH);
        ruta.name = "ruta";
        ruta.dock = glaze::Dock::Fill;
        root().adopt(ruta);
    }

    Bildruta ruta;
};

struct Bild {
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;

    const std::uint8_t* at(int x, int y) const
    {
        return &px[(static_cast<std::size_t>(y) * w + x) * 4];
    }
};

gfx::Color färgFör(int k)
{
    return gfx::Color{static_cast<std::uint8_t>(40 + (k * 37) % 200),
                      static_cast<std::uint8_t>(40 + (k * 91) % 200),
                      static_cast<std::uint8_t>(40 + (k * 53) % 200), 255};
}

bool nära(const std::uint8_t* p, gfx::Color c)
{
    const auto d = [](int a, int b) { return a > b ? a - b : b - a; };
    return d(p[0], c.r) <= kTolerans && d(p[1], c.g) <= kTolerans &&
           d(p[2], c.b) <= kTolerans;
}

// Hörnen (en pixel in) och mitten har färgen.
bool enfärgad(const Bild& b, gfx::Color c)
{
    if (b.w < 4 || b.h < 4)
        return false;
    const int xs[] = {1, b.w - 2, b.w / 2};
    const int ys[] = {1, b.h - 2, b.h / 2};
    for (int y : ys)
        for (int x : xs)
            if (!nära(b.at(x, y), c))
                return false;
    return true;
}

std::string rgb(const std::uint8_t* p)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, "(%d, %d, %d)", p[0], p[1], p[2]);
    return buf;
}

// Kör `fn` på en egen tråd med scen-kontexten tagen i bruk, och väntar in
// den. Kontexten hör till en tråd i taget.
template <class Fn>
bool påScentråd(gfx::SceneContext& ctx, Fn fn)
{
    bool ok = false;
    std::thread([&] {
        if (!ctx.attachThread())
            return;
        fn();
        ctx.detachThread();
        ok = true;
    }).join();
    return ok;
}

// Scentråden ritar `c` över hela bilden och publicerar.
bool ritaOchPublicera(gfx::SceneContext& ctx, gfx::SharedTarget& t,
                      gfx::Color c)
{
    bool ritat = false;
    const bool tråd = påScentråd(ctx, [&] {
        if (!t.beginWrite())
            return;
        gfx::Renderer& r = ctx.renderer();
        if (r.beginOffscreen()) {
            r.pushTarget(t.target(), c);
            r.popTarget();
            r.endOffscreen();
            ritat = true;
        }
        t.publish();
    });
    return tråd && ritat;
}

bool fånga(Fixtur& win, Bild& ut)
{
    win.invalidate();
    win.window().renderFrame();
    return win.window().captureFrame(ut.px, ut.w, ut.h);
}

double msSedan(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - t0)
        .count();
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== scene_context_gate — scen-kontexten och de delade "
                "bilderna ===\n");

    Fixtur win;
    win.setOffscreen(true);
    win.show();
    if (!win.isOpen()) {
        std::printf("  FEL  fönstret gick inte att öppna — ingen "
                    "headless-yta?\n=== scene_context_gate: FALLERAR ===\n");
        return 1;
    }
    win.window().renderFrame();   // fönstrets renderare har ritat en frame
    gfx::Renderer& fönstrets = win.window().renderer();

    // Kontexten och bilderna skapas — och rivs — på scentråden.
    std::unique_ptr<gfx::SceneContext> ctx;
    std::unique_ptr<gfx::SharedTarget> bilder[kBilder];
    Bild första;
    fånga(win, första);
    std::thread([&] {
        ctx = fönstrets.createSceneContext();
        if (!ctx)
            return;
        for (auto& b : bilder)
            b = ctx->createSharedTarget(första.w, första.h);
        ctx->detachThread();
    }).join();
    bool allaBilder = ctx != nullptr;
    for (const auto& b : bilder)
        allaBilder = allaBilder && b != nullptr;
    if (!allaBilder) {
        std::printf("  FEL  %s gick inte att skapa\n"
                    "=== scene_context_gate: FALLERAR ===\n",
                    ctx ? "de delade bilderna" : "scen-kontexten");
        // Det skapade rivs på en scentråd, i fasadens ordning.
        if (ctx)
            std::thread([&] {
                ctx->attachThread();
                for (auto& b : bilder)
                    b.reset();
                ctx.reset();
            }).join();
        return 1;
    }
    check(bilder[0]->width() == första.w && bilder[0]->height() == första.h,
          "den delade bilden har fönstrets mått");

    // ------------------------------------------------------------------ S2
    // Före S1, medan ingenting är publicerat.
    {
        const auto t0 = std::chrono::steady_clock::now();
        gfx::Image* ingen = bilder[1]->acquire();
        const double ms = msSedan(t0);
        check(ingen == nullptr, "S2 en opublicerad bild går inte att ta");
        check(ms < kDirektMs, "S2 frågan om en opublicerad bild väntar inte",
              std::to_string(ms) + " ms");

        // Scentråden håller bilden för skrivning medan fönstret frågar.
        std::thread([&] {
            if (!ctx->attachThread())
                return;
            const bool tagen = bilder[1]->beginWrite();
            // (Samma tråd frågar: fönster-sidans fråga är ett lägesbyte och
            // rör inte scen-kontexten.)
            const auto t1 = std::chrono::steady_clock::now();
            gfx::Image* under = bilder[1]->acquire();
            const double ms1 = msSedan(t1);
            check(tagen, "S2 scentråden tog bilden för skrivning");
            check(under == nullptr,
                  "S2 en bild under skrivning går inte att ta");
            check(ms1 < kDirektMs,
                  "S2 frågan om en bild under skrivning väntar inte",
                  std::to_string(ms1) + " ms");
            gfx::Renderer& r = ctx->renderer();
            if (r.beginOffscreen()) {
                r.pushTarget(bilder[1]->target(), gfx::Color{0, 0, 0, 255});
                r.popTarget();
                r.endOffscreen();
            }
            bilder[1]->publish();
            ctx->detachThread();
        }).join();
    }

    // ------------------------------------------------------------------ S1
    const gfx::Color s1{200, 60, 30, 255};
    check(ritaOchPublicera(*ctx, *bilder[0], s1),
          "S1 scentråden ritade och publicerade");
    int visad = -1;
    {
        gfx::Image* img = bilder[0]->acquire();
        check(img != nullptr && img->valid(), "S1 fönstret tog bilden");
        if (img) {
            visad        = 0;
            win.ruta.bild = img;
            Bild b;
            check(fånga(win, b), "S1 framen gick att fånga");
            if (!b.px.empty())
                check(enfärgad(b, s1), "S1 den fångade framen har färgen",
                      rgb(b.at(b.w / 2, b.h / 2)));
        }
    }

    // ------------------------------------------------------------------ S3
    // Tre bilder i växling: scentråden skriver aldrig den som visas.
    {
        int felFärg = 0, ejTagna = 0, ejRitade = 0;
        for (int k = 0; k < kVarv; ++k) {
            const int nästa = (visad + 1) % kBilder;
            const gfx::Color c = färgFör(k);
            if (!ritaOchPublicera(*ctx, *bilder[nästa], c)) {
                ++ejRitade;
                continue;
            }
            gfx::Image* img = bilder[nästa]->acquire();
            if (!img) {
                ++ejTagna;
                continue;
            }
            const int förra = visad;
            visad           = nästa;
            win.ruta.bild   = img;
            Bild b;
            if (!fånga(win, b) || !enfärgad(b, c))
                ++felFärg;
            // Den förra bildens sista frame är ritad — den lämnas tillbaka.
            if (förra >= 0)
                bilder[förra]->retire();
        }
        check(ejRitade == 0, "S3 varje bild gick att skriva",
              std::to_string(ejRitade) + " av " + std::to_string(kVarv));
        check(ejTagna == 0, "S3 varje publicerad bild gick att ta",
              std::to_string(ejTagna) + " av " + std::to_string(kVarv));
        check(felFärg == 0, "S3 varje fångad frame är enfärgad i sin färg",
              std::to_string(felFärg) + " av " + std::to_string(kVarv));
    }

    // ------------------------------------------------------------------ S4
    // Fönstret håller en bild. Den lämnas tillbaka, scentråden river bilderna
    // och kontexten, och fönstret stängs sist.
    {
        win.ruta.bild = nullptr;
        Bild b;
        fånga(win, b);   // en frame utan bilden — den ritas inte mer
        if (visad >= 0) {
            bilder[visad]->retire();
            check(bilder[visad]->acquire() == nullptr,
                  "S4 en lämnad bild går inte att ta igen");
        }
        const bool rivet = påScentråd(*ctx, [&] {
            for (auto& bild : bilder)
                bild.reset();
        });
        std::thread([&] {
            ctx->attachThread();
            ctx.reset();
        }).join();
        check(rivet && !ctx, "S4 bilderna och kontexten revs på scentråden");
        Bild efter;
        check(fånga(win, efter),
              "S4 fönstret ritar efter att scen-kontexten rivits");
    }

    std::printf("=== scene_context_gate: %s ===\n",
                g_fel == 0 ? "GRÖN" : "FALLERAR");
    return g_fel == 0 ? 0 : 1;
}
