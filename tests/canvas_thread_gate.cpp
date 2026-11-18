// canvas_thread_gate — RITYTANS RENDERINGSTRÅD, mätt genom den riktiga
// backenden (src/canvas/render_loop.h).
//
// VAD GRINDEN PÅSTÅR — och inget mer:
//
//   T1  FÖNSTRETS BILD VÄNTAR INTE: med renderingstråden spärrad returnerar
//       `renderFrame()` inom `kDirektMs`, och ritytan visar sin yta (ingen
//       scen än).
//   T2  BILDEN KOMMER: när tråden släpps ritar den jobbet, och nästa bild i
//       fönstret har formens färg i ritytans mitt och ytans färg utanför
//       formen — aldrig en halv bild (formens fyra hörn och mitten prövas).
//   T3  EN STILLA SCEN GER INGA JOBB: tio bilder i fönstret utan ändring
//       ritar ingenting på tråden.
//   T4  EN ÄNDRING GER ETT JOBB: en ny form ritas av tråden och syns.
//   T5  TRÅDLÄGET = DET SYNKRONA: samma scen i ett fönster utan tråd ger
//       samma pixlar i formens inre och i ytan utanför (± `kTolerans`).
//   T6  MINNET följer bilden: `memoryUsed()` är över noll när en bild med en
//       form visas.
//   T7  DET FÖRANKRADE BARNET FÖLJER DEN VISADE BILDEN: efter en
//       kameraflytt, med tråden spärrad, står barnet kvar där bilden visar
//       punkten; när den nya bilden visas har barnet flyttat.
//   T8  STÄNGNINGEN VÄNTAR IN TRÅDEN: ett fönster som stängs med tråden
//       spärrad och ett jobb i kön stängs, och ett nytt fönster får en NY
//       loop (ett annat löpnummer).
//   T9  `renderToBitmap` GENOM TRÅDEN: svaret kommer i efterhand, en gång,
//       med de begärda måtten, och bilden visar scenen.
//   T10 EN SIDA GENOM TRÅDEN (utskriftens väg): målaren spelas in här och
//       ritas där — rutan röd i sidans mm, papperet vitt.
//   T11 3D-MÅLET genom tråden: formen syns under en 3D-kamera.
//   T12 EN ETIKETT träffas (`hitTest`) efter bilden från tråden.
//   T13 EN RENDER-TEXTUR I EN FORM ritas som förut: ritytan ritar då scenen
//       själv, i fönstrets tråd, och lämnar inga fler jobb.
//   (T7 mäter också att `toLocal` svarar för bilden som syns och `camera()`
//   för den senaste kameran.)
//
// Grinden mäter INTE: del-vyerna genom tråden, hur fort tråden ritar, och
// andra backends än den som körs (doc/os/*/TESTS.md).
//
// VÄGEN är glaze-renders — `setOffscreen(true)` → `show()` → `renderFrame()`
// → `captureFrame()` — med grindkroken som ger också ett offscreen-fönster en
// renderingstråd.
//
// INGEN TYST SKIP (clip_gate-beslutet): utan fönster eller utan
// renderingstråd FALLER grinden.

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <glaze/glaze.h>

#include "canvas/frame_job.h"
#include "canvas/render_loop.h"

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
constexpr int    kFönsterW = 320;
constexpr int    kFönsterH = 240;
constexpr int    kTolerans = 3;       // per kanal
constexpr double kDirektMs = 200.0;   // en bild som inte väntat på tråden
constexpr int    kVäntaMs  = 5000;    // tak för trådens arbete
constexpr int    kStilla   = 10;      // T3

const gfx::Color kRöd{220, 30, 30, 255};
const gfx::Color kBlå{30, 60, 220, 255};
const gfx::Color kYta{240, 240, 240, 255};

struct Bild {
    std::vector<std::uint8_t> px;
    int                       w = 0, h = 0;
    const std::uint8_t* at(int x, int y) const
    {
        static const std::uint8_t ingen[4] = {1, 2, 3, 4};
        if (x < 0 || y < 0 || x >= w || y >= h ||
            px.size() < static_cast<std::size_t>(w) * h * 4)
            return ingen;
        return &px[(static_cast<std::size_t>(y) * w + x) * 4];
    }
};

bool ärFärg(const std::uint8_t* p, gfx::Color c, int tol = kTolerans)
{
    return std::abs(p[0] - c.r) <= tol && std::abs(p[1] - c.g) <= tol &&
           std::abs(p[2] - c.b) <= tol;
}

std::string px(const std::uint8_t* p)
{
    return std::to_string(p[0]) + "," + std::to_string(p[1]) + "," +
           std::to_string(p[2]);
}

// Ritytan med en väg att rita om sin yta UTAN att stega `damageStamp()` —
// det renderingstrådens väckning gör (`invalidateRect`). Grinden kör ingen
// pump mellan stegen, så den gör väckningens arbete själv.
class Yta : public glaze::Canvas {
public:
    void ritaOm() { invalidateRect(resolve(behaviorCtx())); }
};

// Fönstret: en rityta som fyller det, med en känd yta och utan kant, och ett
// barn förankrat i världen.
class Fönster : public glaze::AppWindow {
public:
    Yta          yta;
    glaze::Panel barn;

    Fönster()
    {
        setClientSize(kFönsterW, kFönsterH);
        yta.name = "yta";
        yta.dock = glaze::Dock::Fill;
        yta.setBorder(false);
        yta.setFlat(true);
        yta.setBackground(kYta);
        yta.setCenter(0.0, 0.0);
        barn.name   = "barn";
        barn.width  = 10.0f;
        barn.height = 10.0f;
        root().adopt(yta);
        yta.adopt(barn);
        yta.setChildAnchor(barn, glaze::Point{0.0, 0.0});
    }

    glaze::detail::CanvasRenderLoop* loop()
    {
        return glaze::detail::canvasLoopOf(*this);
    }
    // Fönstrets Ctx — inspelningens grund (T10).
    glaze::Ctx ctx() { return makeCtx(); }

    // En bild av fönstret: ritytans yta ritas om (utan ny stämpel), så en
    // färdig bild från tråden läggs på plats.
    void rita()
    {
        yta.ritaOm();
        window().renderFrame();
    }
    Bild bild()
    {
        Bild b;
        rita();
        if (!window().captureFrame(b.px, b.w, b.h)) {
            std::printf("  FEL  kunde inte läsa fönstrets bild\n");
            ++g_fel;
        }
        return b;
    }

    // Världspunkten som pixel i fönstrets bild (ritytans kamera NU).
    bool pixel(double wx, double wy, int& x, int& y)
    {
        const std::optional<glaze::Point> l = yta.toLocal(glaze::Point{wx, wy});
        if (!l)
            return false;
        const std::optional<gfx::Pointf> f = yta.toWindow(*l);
        if (!f)
            return false;
        x = static_cast<int>(f->x);
        y = static_cast<int>(f->y);
        return true;
    }

    // Väntar tills tråden ritat `n` jobb till; false när taket nåddes.
    bool väntaPå(std::uint64_t n)
    {
        glaze::detail::CanvasRenderLoop* l = loop();
        if (!l)
            return false;
        const auto slut =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(kVäntaMs);
        while (l->framesRendered() < n) {
            if (std::chrono::steady_clock::now() > slut)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return true;
    }
};

double ms(std::chrono::steady_clock::time_point a)
{
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - a)
        .count();
}

// PUMPAR tills `klar()` svarar ja (eller taket nås): svaren från tråden
// ligger i ramkön, som körs av en pump. Ett dolt hjälpfönster pumpar ett
// varv i taget — ramkön körs före den postade stängningen.
template <class Klar>
void pumpaTills(Klar klar)
{
    const auto slut =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(kVäntaMs);
    while (!klar() && std::chrono::steady_clock::now() < slut) {
        glaze::AppWindow pump;
        pump.setOffscreen(true);
        glaze::app().post([&pump] { pump.close(); });
        pump.run();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

// Formen i världen: en kvadrat kring origo, och punkterna som prövas.
constexpr double kSida = 40.0;   // halva sidan
struct Prov {
    double x, y;
};
const Prov kInne[] = {{0, 0}, {-30, -30}, {30, -30}, {30, 30}, {-30, 30}};
const Prov kUte[]  = {{-70, 0}, {70, 0}, {0, -70}, {0, 70}};

void läggForm(glaze::Canvas& y, gfx::Color f)
{
    y.addShape(glaze::Shape::rect({-kSida, -kSida, 2 * kSida, 2 * kSida}, f));
}

// Formens inre i `inne`, ytan utanför — annars en beskrivning av felet.
std::string avvikelse(Fönster& w, const Bild& b, gfx::Color inne)
{
    std::string fel;
    int         x = 0, y = 0;
    for (const Prov& p : kInne)
        if (!w.pixel(p.x, p.y, x, y) || !ärFärg(b.at(x, y), inne))
            fel += "inne(" + std::to_string(static_cast<int>(p.x)) + "," +
                   std::to_string(static_cast<int>(p.y)) + ")=" + px(b.at(x, y)) +
                   " ";
    for (const Prov& p : kUte)
        if (!w.pixel(p.x, p.y, x, y) || !ärFärg(b.at(x, y), kYta))
            fel += "ute(" + std::to_string(static_cast<int>(p.x)) + "," +
                   std::to_string(static_cast<int>(p.y)) + ")=" + px(b.at(x, y)) +
                   " ";
    return fel;
}

// Fallen T1–T7 i ett öppet fönster; `id` är loopens löpnummer (T8). Slutar
// med tråden SPÄRRAD och ett jobb i kön — så stängs fönstret.
void trådsteg(Fönster& w, std::uint64_t& id)
{
    w.window().renderFrame();   // layouten, och ritytans loop
    glaze::detail::CanvasRenderLoop* loop = w.loop();
    check(loop != nullptr, "fönstret har en renderingstråd");
    if (!loop)
        return;
    // Det tomma första jobbet väntas in, så räkningen börjar på känd grund.
    w.väntaPå(1);
    const std::uint64_t bas = loop->framesRendered();

    // T1: tråden spärrad, en form läggs till.
    loop->setPaused(true);
    läggForm(w.yta, kRöd);
    const auto  t0 = std::chrono::steady_clock::now();
    const Bild  b1 = w.bild();
    const double d = ms(t0);
    int          x = 0, y = 0;
    w.pixel(0, 0, x, y);
    check(d < kDirektMs, "T1 fönstrets bild väntar inte på en spärrad tråd",
          std::to_string(d) + " ms");
    check(ärFärg(b1.at(x, y), kYta),
          "T1 scenen är inte ritad än: ritytans mitt har ytans färg",
          px(b1.at(x, y)));
    check(loop->framesRendered() == bas, "T1 den spärrade tråden ritade inget",
          std::to_string(loop->framesRendered() - bas));

    // T2: tråden släpps.
    loop->setPaused(false);
    check(w.väntaPå(bas + 1), "T2 tråden ritade jobbet");
    const Bild        b2  = w.bild();
    const std::string f2  = avvikelse(w, b2, kRöd);
    check(f2.empty(), "T2 bilden visas hel: formen röd, ytan utanför orörd", f2);

    // T3: stilla.
    const std::uint64_t efter2 = loop->framesRendered();
    for (int i = 0; i < kStilla; ++i)
        w.rita();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    check(loop->framesRendered() == efter2,
          "T3 en stilla scen ger inga jobb",
          std::to_string(loop->framesRendered() - efter2) + " ritade");

    // T6: minnet.
    const glaze::CanvasMemoryUse m = w.yta.memoryUsed();
    check(m.gpu > 0, "T6 memoryUsed() följer den visade bilden",
          std::to_string(m.gpu) + " byte på GPU:n");

    // T4: en ändring.
    w.yta.setShapes({});
    läggForm(w.yta, kBlå);
    w.rita();
    check(w.väntaPå(efter2 + 1), "T4 en ändring ger ett jobb");
    const Bild        b4 = w.bild();
    const std::string f4 = avvikelse(w, b4, kBlå);
    check(f4.empty(), "T4 den nya formen visas", f4);

    // T7: det förankrade barnet. Kameran flyttas med tråden spärrad.
    w.rita();
    const gfx::Rectf före = w.barn.resolve(w.barn.behaviorCtx());
    const std::optional<glaze::Point> lokFöre = w.yta.toLocal(glaze::Point{0.0, 0.0});
    loop->setPaused(true);
    const std::uint64_t n7 = loop->framesRendered();
    w.yta.setCenter(50.0, 0.0);
    w.rita();
    const gfx::Rectf under = w.barn.resolve(w.barn.behaviorCtx());
    const std::optional<glaze::Point> lokUnder = w.yta.toLocal(glaze::Point{0.0, 0.0});
    check(std::abs(under.x - före.x) < 1.0f,
          "T7 barnet står kvar medan den gamla bilden visas",
          std::to_string(under.x - före.x) + " px");
    check(lokFöre && lokUnder && std::abs(lokUnder->x - lokFöre->x) < 0.5,
          "T7 toLocal svarar för bilden som syns medan den gamla visas");
    check(std::abs(w.yta.camera().center.x - 50.0) < 1e-9,
          "T7 camera() svarar med den senaste kameran");
    loop->setPaused(false);
    check(w.väntaPå(n7 + 1), "T7 tråden ritade den flyttade kameran");
    w.rita();
    const gfx::Rectf efter = w.barn.resolve(w.barn.behaviorCtx());
    const std::optional<glaze::Point> lokEfter = w.yta.toLocal(glaze::Point{0.0, 0.0});
    check(efter.x < före.x - 1.0f, "T7 barnet flyttar när den nya bilden visas",
          std::to_string(efter.x - före.x) + " px");
    check(lokFöre && lokEfter && lokEfter->x < lokFöre->x - 1.0,
          "T7 toLocal följer med när den nya bilden visas");

    // T12: en ETIKETT träffas genom tråden — bygget och placeringen görs av
    // renderingstråden och förs in i fönstrets resurser med bilden.
    {
        const std::uint64_t n12 = loop->framesRendered();
        w.yta.addShape(glaze::Shape::text({50.0, 70.0}, "Etikett",
                                          glaze::TextStyle{.size = glaze::px(18.0f)}));
        w.rita();
        const bool ritad = w.väntaPå(n12 + 1);
        w.rita();
        const std::optional<glaze::Point> l = w.yta.toLocal(glaze::Point{50.0, 70.0});
        check(ritad && l && !w.yta.hitTest(*l).empty(),
              "T12 etiketten träffas efter bilden från tråden");
    }

    // T11: 3D-MÅLET genom tråden — en 3D-kamera rakt uppifrån över formen.
    {
        const std::uint64_t n11 = loop->framesRendered();
        w.yta.setCamera(glaze::Camera3D{.target    = {0.0, 0.0, 0.0},
                                        .distance  = 300.0,
                                        .elevation = 90.0f});
        w.rita();
        const bool ritad = w.väntaPå(n11 + 1);
        const Bild b11   = w.bild();
        int        x = 0, y = 0;
        const bool mitt = w.pixel(0.0, 0.0, x, y);
        check(ritad && mitt && ärFärg(b11.at(x, y), kBlå),
              "T11 3D-målet ritas av tråden: formen syns under 3D-kameran",
              px(b11.at(x, y)));
    }

    // T9: renderToBitmap genom tråden. Scenen uppifrån, formen (blå) mitt i.
    {
        constexpr int kBildPx = 96;
        bool          svar = false, iAnropet = false;
        std::optional<glaze::Bitmap> bm;
        w.yta.renderToBitmap(kBildPx, kBildPx,
                             glaze::Camera3D{.target    = {50.0, 0.0, 0.0},
                                             .distance  = 300.0,
                                             .elevation = 90.0f},
                             [&](std::optional<glaze::Bitmap> b) {
                                 svar = true;
                                 bm   = std::move(b);
                             });
        iAnropet = svar;
        pumpaTills([&] { return svar; });
        check(!iAnropet && svar, "T9 renderToBitmap svarar i efterhand, en gång");
        const bool mått = bm && bm->valid() && bm->width() == kBildPx &&
                          bm->height() == kBildPx && bm->pixels();
        check(mått, "T9 bilden har de begärda måtten");
        if (mått) {
            const std::uint8_t* p = bm->pixels();
            // Kameran står över (50, 0): formen kring origo ligger till
            // vänster om mitten, och hörnet är ritytans bakgrund.
            const auto vid = [&](int x, int y) {
                return p + (static_cast<std::size_t>(y) * kBildPx + x) * 4;
            };
            bool blå = false;
            for (int x = 0; x < kBildPx / 2 && !blå; ++x)
                blå = ärFärg(vid(x, kBildPx / 2), kBlå);
            check(blå && ärFärg(vid(kBildPx - 2, 2), kYta),
                  "T9 bilden visar scenen: formen och ritytans bakgrund",
                  px(vid(kBildPx - 2, 2)));
        }
    }

    // T10: en SIDA genom tråden (utskriftens väg): en röd ruta i sidans mm,
    // inspelad här och ritad där.
    {
        glaze::detail::PageJob j;
        j.width     = 80;
        j.height    = 80;
        j.pxPerMm   = 2.0;
        j.cx        = 20.0;
        j.cy        = 20.0;
        j.scale     = static_cast<float>(j.pxPerMm * 25.4 / 96.0);
        j.theme     = w.theme().resolve(w.themeMode());
        j.style     = w.style();
        j.maxRounds = 8;
        j.memoryBudget = glaze::app().canvasMemoryBudget();
        j.uploadBudget = glaze::app().canvasUploadBudget();
        {
            const glaze::Ctx               sc = glaze::detail::pageCtx(j, w.ctx());
            glaze::detail::PaintContext    f  = glaze::detail::pagePaint(j, sc);
            glaze::Painter                 p(f, false);
            glaze::detail::startRecording(p, j.paints);
            p.fillRect({10.0, 10.0, 20.0, 20.0}, kRöd);
        }
        bool                      svar = false, ok = false, hel = false;
        std::vector<std::uint8_t> rgba;
        loop->orderPage(std::make_shared<const glaze::detail::PageJob>(std::move(j)),
                        [&](bool o, bool k, std::vector<std::uint8_t> p, bool) {
                            svar = true;
                            ok   = o;
                            hel  = k;
                            rgba = std::move(p);
                        });
        pumpaTills([&] { return svar; });
        check(svar && ok && hel && rgba.size() == std::size_t{80} * 80 * 4,
              "T10 sidan ritades av tråden och lästes tillbaka",
              std::to_string(rgba.size()) + " byte");
        if (rgba.size() == std::size_t{80} * 80 * 4) {
            const auto vid = [&](int x, int y) {
                return rgba.data() + (static_cast<std::size_t>(y) * 80 + x) * 4;
            };
            check(ärFärg(vid(40, 40), kRöd) &&
                      ärFärg(vid(5, 5), gfx::Color{255, 255, 255, 255}),
                  "T10 sidan: rutan röd i mm, papperet vitt",
                  px(vid(40, 40)) + " | " + px(vid(5, 5)));
        }
    }

    // Inför T8: tråden spärrad och ett jobb i kön.
    id = loop->id();
    loop->setPaused(true);
    w.yta.setCamera(glaze::Camera3D{.target = {0.0, 0.0, 0.0}, .distance = 320.0});
    w.rita();
}

// T13: EN RENDER-TEXTUR I EN FORM — målningen är appens kod, så ritytan ritar
// scenen själv, i fönstrets tråd, och bilden är den vanliga.
void texturfallet()
{
    Fönster w;
    w.setOffscreen(true);
    bool kördes = false;
    glaze::app().post([&] {
        kördes = true;
        int                  målningar = 0;
        glaze::RenderTexture rt(32, 32);
        rt.paint([&målningar](const glaze::Ctx& c) {
            ++målningar;
            glaze::Draw::rect(c, c.bounds(), 0.0f, kRöd);
        });
        w.yta.addShape(glaze::Shape::image({-kSida, -kSida, 2 * kSida, 2 * kSida}, rt));
        w.window().renderFrame();
        glaze::detail::CanvasRenderLoop* loop = w.loop();
        check(loop != nullptr, "T13 fönstret har en renderingstråd");
        if (loop) {
            check(w.väntaPå(1), "T13 tråden ritade den första bilden");
            w.rita();   // bilden avvisas; ritytan ritar själv
            const std::uint64_t efter = loop->framesRendered();
            const Bild          b     = w.bild();
            const std::string   f     = avvikelse(w, b, kRöd);
            check(f.empty() && målningar >= 1,
                  "T13 en RenderTexture i en form ritas som förut, i fönstrets tråd",
                  f + std::to_string(målningar) + " målningar");
            w.yta.setCenter(5.0, 0.0);
            w.rita();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            check(loop->framesRendered() == efter,
                  "T13 ritytan lämnar inga fler jobb till tråden",
                  std::to_string(loop->framesRendered() - efter));
        }
        w.close();
    });
    w.run();
    check(kördes, "T13 fönstret gick att öppna");
}

void trådfallen()
{
    std::uint64_t id1 = 0;
    {
        Fönster w;
        w.setOffscreen(true);
        bool kördes = false;
        glaze::app().post([&] {
            kördes = true;
            trådsteg(w, id1);
            w.close();   // verkställs i pump-svansen — T8
        });
        w.run();
        check(kördes, "fönstret gick att öppna (ingen tyst skip)");
        check(!w.isOpen(), "T8 fönstret stängdes med tråden spärrad");
    }
    Fönster w2;
    w2.setOffscreen(true);
    bool kördes = false;
    glaze::app().post([&] {
        kördes = true;
        w2.window().renderFrame();
        glaze::detail::CanvasRenderLoop* l2 = w2.loop();
        check(l2 && l2->id() != id1 && id1 != 0,
              "T8 ett nytt fönster får en ny loop");
        w2.close();
    });
    w2.run();
    check(kördes, "T8 det andra fönstret gick att öppna");
}

// T5: samma scen utan tråd.
void synkfallet()
{
    glaze::detail::setCanvasThreading(false);
    Fönster w;
    w.setOffscreen(true);
    bool kördes = false;
    glaze::app().post([&] {
        kördes = true;
        läggForm(w.yta, kRöd);
        w.window().renderFrame();
        check(w.loop() == nullptr, "T5 utan trådläge har fönstret ingen loop");
        const Bild        b = w.bild();
        const std::string f = avvikelse(w, b, kRöd);
        check(f.empty(), "T5 det synkrona läget ger samma bild som trådläget", f);
        w.close();
    });
    w.run();
    check(kördes, "T5 fönstret gick att öppna");
    glaze::detail::setCanvasThreading(true);
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== canvas_thread_gate — ritytans renderingstråd ===\n");
    glaze::detail::setCanvasThreading(true);
    glaze::detail::setCanvasThreadingOffscreen(true);
    trådfallen();
    texturfallet();
    synkfallet();
    std::printf(g_fel ? "=== %d FEL ===\n" : "=== alla fall gröna ===\n", g_fel);
    return g_fel ? 1 : 0;
}
