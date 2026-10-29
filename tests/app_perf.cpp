// app_perf — EN APP PÅ RIKTIGT: en kartvy och en 3D-vy som strömmar data
// medan användaren rör sig över flera kilometer.
//
// Kör:  ./build/tests/app_perf [--synlig] [--varv N] [-v]
// Exit: 0 = sessionen gick igenom · 1 = fönstret öppnades inte, sessionen
//       hann inte klart, eller strömningen kom aldrig ikapp
//
// canvas_perf och canvas_perf3d mäter RITYTAN: en kamera i taget, ritad
// direkt med `renderFrame` i ett dolt fönster. Det här mäter APPEN: två
// riktiga fönster som pumpas av `run()`, en skriptad session vars input går
// genom fönstrets egen väg (`AppWindow::handleEvent` — gesterna, träffprovet,
// statusraden och fokus är ramverkets), och data som läses in i arbetstrådar
// och släpps efter kameran. Taktslaget är ritytans `onTick` — en tick per
// ritad bildruta i pump-svansen — så frametiden är den användaren ser: med
// `--synlig` vsync och OS:ets presentation, annars (ctest) dolda fönster.
//
// KARTVYN (1280 × 800 logiska px innehåll, statusrad med sex fält):
//   plattor     256 px i nivå 6–19 ur rutnätet, avkodade (PNG) i fyra
//               arbetstrådar med 25–90 ms fördröjning (nätet), LRU om 2000
//               plattor, ritytans budget 1 GiB, 120 ms insvängning efter en
//               zoom; HELA plattlagret byggs om med `setShapes` när mängden
//               ändras (synliga plattor som projektiva bildfyrhörningar och
//               grå platshållare för de som inte kommit)
//   punktmoln   rutor om 256 m × 2^nivå (sex per axel, hysteres ±25 %) längs
//               en 72 m bred korridor, ett moln per (ruta, klass) i sex
//               klasser, högst två rutor in per bildruta, LRU om 96 rutor —
//               lagret byggs om med `setShapes`
//   ledning     300 stolpar längs 10,5 km med vinkelpunkter: spannen, stolpen
//               som markör och tre rader etikett om 3 mm med gloria som
//               kolliderar, i tre prioritetsgrupper med zoomintervall
//   vektorer    ≈ 2 500 fastighetsytor, 300 markslagsytor, 500 vägar med
//               namn, 40 streckade kabelstråk (2 m / 1 m) och 2 000 punkter
//               om 6 px — statiska, lästa vid start
//   målaren     varje bildruta: rutnätet med koordinater, hovrad stolpe som
//               gloria i två nivåer, 3D-vyns kamera som markör; överlägget:
//               skalstock, norrpil och tre rader status
//   per musrörelse  `hitTest` och N/E i statusraden; omritning när hovret
//               byter stolpe
//
// 3D-VYN (eget fönster, ägt av kartans):
//   rutor       20 × 20 kring ögat, 75 m: terräng om 17 × 17 hörn med
//               rutans bild som textur, punktmoln (upp till 50 000) där
//               korridoren går, ≈ 165 instansierade träd i två detaljnivåer —
//               byggda i arbetstrådarna, högst åtta rutor in per bildruta, i
//               FASTA platser (`setShape`) som återanvänds när rutor släpps
//   stolpar     300, tolv mesher om 30–150 k trianglar i fyra detaljnivåer
//   linor       tre faser per spann, kedjekurvor om 51 punkter
//   kameran     förstaperson som följer marken (2 m över), W/A/S/D/Q/E hålls
//               (`isKeyHeld` + `onTick`), upp till 200 km/h, och hopp om
//               3 km; varje flytt över 0,5 m ritar om kartans markör
//
// SESSIONEN (samma följd varje gång): kartans start, hover längs ledningen,
// panorering 3 km i drag om 375 px, zoom ut och in med hjulet, en lång
// resa 8 km, vila — 3D-vyn öppnas: start, körning längs ledningen, vridning,
// två hopp om 3 km, vila — och sist båda samtidigt (körning i 3D medan
// pekaren hovrar på kartan).
//
// VARVTESTET (`--varv N`) ersätter sessionen efter kartans start med N
// LIKADANA varv som slutar där de började: kartans resa 8 km ut och tillbaka
// och vila (mätpunkt A), sedan 3D-vyn öppnad, körd, två hopp om 3 km och
// stängd — appen släpper sina rutor, som vid en riktig stängning — och 5 s
// vila (mätpunkt B). Utan läckor är A och B desamma varv för varv; varv 1 är
// insvängningen. Tabellen sist visar tillväxten per varv. Grindas inte.
//
// MÄTT PER STEG OCH FÖNSTER: frametiden ur `onTick` (median, p90, max och
// antalet över 25 och 50 ms), tiden från injicerad input till nästa
// `onPaint`, renderarens anrop och uppladdade byte per bildruta, de nya
// buffertarna och uppladdningarna som bröt render-passet (`uploadBreaks`,
// bara Metal), strömningen
// (rutor in och ut, köns djup, tiden för `setShapes`/`setShape` i UI-tråden)
// och MINNET efter steget: processens (macOS `phys_footprint` — det delade
// minnet räknar GPU:ns allokeringar in, Windows `PrivateUsage` + arbets-
// mängden och DXGI:s per-process-användning av grafikminnet, Linux `VmRSS`),
// fönstrets bildcache åt ritytan och summan uppladdat. Med `-v` mäter
// PROFILERAREN också (glaze/profiler.h): intervallen mellan bilderna, input →
// present, fasernas p50 och räknarna per bild, pumpen och verktygen — raderna
// efter stegens; sammanfattningen sist är densamma. Klockan, minnet, energin,
// arbetstrådarna och sessionen är mätselens (tests/perf/harness.h).
//
// GRINDEN — bara att sessionen GÅR: fönstret öppnas, varje steg når sitt
// slut inom sin tidsgräns, och strömningen kommer ikapp i varje vilosteg
// (köerna tomma och varje önskad ruta inne). Inga tider och inga
// minnesnivåer grindas — de är mätningen.
//
// INTE MÄTT: GPU-tiden per bildruta (fasaden har ingen fråga för den),
// riktig nätverkstrafik och disk, styrplattans gester, kollisionsvyer och
// redigering, profilvyn.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glaze/glaze.h>

#include "perf/harness.h"   // mätselen: klockan, minnet, trådarna, sessionen

using namespace perf;

namespace {

// --------------------------------------------------------------- världen
//
// SWEREF-liknande koordinater. Ledningen: 300 stolpar om 35 m med
// vinkelpunkter, 10,5 km från sydväst mot nordost.
constexpr double kX0 = 674000.0;
constexpr double kY0 = 6580000.0;
constexpr int    kStolpar = 300;
constexpr double kSpann   = 35.0;
constexpr double kBand    = 36.0;   // punktmolnets halva korridor

struct Ledning {
    std::vector<glaze::Point> stolpe;      // läget
    std::vector<int>          typ;         // 0 rak, 1 vinkel, 2 ände
    std::vector<double>       riktning;    // grader, kompass
    std::vector<glaze::Point> prov;        // korridoren var 20:e meter
};

Ledning byggLedning()
{
    Ledning l;
    Slump   r{4711};
    double  x = kX0 - 4000.0, y = kY0 - 3000.0;
    double  kurs = 40.0;   // grader, kompass
    for (int i = 0; i < kStolpar; ++i) {
        const bool vinkel = i > 0 && i < kStolpar - 1 && i % 23 == 0;
        if (vinkel)
            kurs += r.mellan(-35.0, 35.0);
        l.stolpe.push_back({x, y});
        l.typ.push_back(i == 0 || i == kStolpar - 1 ? 2 : (vinkel ? 1 : 0));
        l.riktning.push_back(kurs);
        x += kSpann * std::sin(kurs * kPi / 180.0);
        y += kSpann * std::cos(kurs * kPi / 180.0);
    }
    for (int i = 0; i + 1 < kStolpar; ++i) {
        const glaze::Point a = l.stolpe[static_cast<std::size_t>(i)];
        const glaze::Point b = l.stolpe[static_cast<std::size_t>(i) + 1];
        for (double t = 0.0; t < 1.0; t += 20.0 / kSpann)
            l.prov.push_back({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
    }
    l.prov.push_back(l.stolpe.back());
    return l;
}

const Ledning& ledning()
{
    static const Ledning l = byggLedning();
    return l;
}

// Ligger lådan inom `marg` från korridoren?
bool näraKorridoren(double x0, double y0, double x1, double y1, double marg)
{
    for (const glaze::Point& p : ledning().prov)
        if (p.x >= x0 - marg && p.x <= x1 + marg && p.y >= y0 - marg && p.y <= y1 + marg)
            return true;
    return false;
}

// Avståndet till korridorens närmaste prov (grovt — proven ligger 20 m isär).
double tillKorridoren(double x, double y)
{
    double bäst = 1e30;
    for (const glaze::Point& p : ledning().prov)
        bäst = std::min(bäst, (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y));
    return std::sqrt(bäst);
}

double markhöjd(double x, double y)
{
    const double u = x - kX0, v = y - kY0;
    return 40.0 + 18.0 * std::sin(u / 410.0) * std::cos(v / 530.0) +
           6.0 * std::sin(u / 97.0 + v / 133.0);
}

// STRÖMMARNA i sessionens ordning — stegens räknare (`g_steg->ström`).
enum StrömIndex { kStrömPlattor, kStrömMoln, kStrömRutor };

// ============================================================== KARTVYN
//
// Plattornas rutnät (selens `plattM`), med origo långt sydväst om scenen.
constexpr double kRutnätX = kX0 - 200000.0;
constexpr double kRutnätY = kY0 - 200000.0;
constexpr int    kPlattcache = 2000;
constexpr int    kMolncache  = 96;

std::uint64_t plattNyckel(int nivå, std::int64_t i, std::int64_t j)
{
    return (static_cast<std::uint64_t>(nivå) << 56) ^
           (static_cast<std::uint64_t>(i & 0xFFFFFFF) << 28) ^
           static_cast<std::uint64_t>(j & 0xFFFFFFF);
}

struct Platta {
    std::uint64_t nyckel;
    glaze::Point  hörn[4];   // övre vänster, övre höger, nedre höger, nedre vänster
};

// Punktmolnets sex klasser: andel och färg.
struct Klass {
    double     andel;
    float      höjd;
    gfx::Color färg;
};
constexpr Klass kKlasser[6] = {
    {0.40, 0.0f, {150, 110, 70, 255}},  {0.20, 0.6f, {120, 200, 90, 255}},
    {0.15, 3.0f, {60, 170, 60, 255}},   {0.15, 12.0f, {20, 110, 30, 255}},
    {0.05, 6.0f, {230, 80, 60, 255}},   {0.05, 22.0f, {250, 230, 60, 255}},
};

struct MolnRuta {
    std::array<glaze::PointCloud, 6> klass;
};

class KartFönster;
class TreDFönster;

class KartFönster : public Fönster {
public:
    KartFönster(Arbetare& arbete) : arbete_(arbete)
    {
        setTitle("app_perf — kartvy");
        karta.name = "karta";
        karta.dock = glaze::Dock::Fill;
        karta.setBorder(false);
        karta.setPannable(true);
        karta.setZoomable(true);
        root().adopt(karta);
        statusBar.show();
        statusBar.addPanel("Klar", 0.0f, true);
        statusBar.addPanel("N", 130.0f);
        statusBar.addPanel("E", 130.0f);
        statusBar.addPanel("Skala", 90.0f);
        statusBar.addPanel("Plattor", 110.0f);
        statusBar.addPanel("Moln", 110.0f);
        setContentSize(1280, 800);
        if (!g_synlig)
            setOffscreen(true);
        insvängning_.setSingleShot(true);
        insvängning_.setInterval(120);
        insvängning_.onTick = [this] {
            insvängd_ = true;
            begärPlattor();
        };
    }

    glaze::Canvas karta;
    TreDFönster*  tred = nullptr;

    // ---------------------------------------------------------- scenen
    void byggScen()
    {
        const auto t0 = Klocka::now();
        // Som appen: Ground och ByKind — utom ledningens objekt, som ritas i
        // exakt målarordning (Strict).
        const auto lager = [this](const char* namn) {
            const int l = karta.addLayer(namn);
            karta.setLayerPlacement(l, glaze::LayerPlacement::Ground);
            karta.setLayerDrawOrder(l, glaze::DrawOrder::ByKind);
            return l;
        };
        lagerPlattor_ = lager("plattor");
        lagerYtor_    = lager("ytor");
        lagerMoln_    = lager("punktmoln");
        lagerVägar_   = lager("vägar");
        lagerLedning_ = lager("ledning");
        karta.setLayerDrawOrder(lagerLedning_, glaze::DrawOrder::Strict);

        const Ledning& l = ledning();
        double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
        for (const glaze::Point& p : l.stolpe) {
            x0 = std::min(x0, p.x);
            y0 = std::min(y0, p.y);
            x1 = std::max(x1, p.x);
            y1 = std::max(y1, p.y);
        }
        Slump r{90210};
        // FASTIGHETERNA och MARKSLAGET — nära korridoren.
        std::vector<glaze::Shape> ytor;
        int fastigheter = 0, markslag = 0;
        while (fastigheter < 2500) {
            const double cx = r.mellan(x0 - 1200, x1 + 1200);
            const double cy = r.mellan(y0 - 1200, y1 + 1200);
            if (tillKorridoren(cx, cy) > 1200.0)
                continue;
            const int                 n = 6 + static_cast<int>(r.nästa() % 5u);
            std::vector<glaze::Point> ring;
            for (int k = 0; k < n; ++k) {
                const double v = 2 * kPi * k / n;
                ring.push_back({cx + std::cos(v) * r.mellan(30, 55),
                                cy + std::sin(v) * r.mellan(25, 45)});
            }
            ytor.push_back(glaze::Shape::polygon(
                std::move(ring), gfx::Color{240, 230, 200, 40},
                glaze::Stroke{.paint = gfx::Color{120, 90, 60, 200}, .width = glaze::px(1.0f)}));
            ++fastigheter;
        }
        while (markslag < 300) {
            const double cx = r.mellan(x0 - 1200, x1 + 1200);
            const double cy = r.mellan(y0 - 1200, y1 + 1200);
            if (tillKorridoren(cx, cy) > 1200.0)
                continue;
            const int                 n = 12 + static_cast<int>(r.nästa() % 9u);
            std::vector<glaze::Point> ring;
            for (int k = 0; k < n; ++k) {
                const double v = 2 * kPi * k / n;
                const double rr = r.mellan(80, 160);
                ring.push_back({cx + std::cos(v) * rr, cy + std::sin(v) * rr});
            }
            ytor.push_back(glaze::Shape::polygon(std::move(ring),
                                                 gfx::Color{80, 160, 80, 50}));
            ++markslag;
        }
        karta.setShapes(lagerYtor_, std::move(ytor));

        // VÄGARNA med namn, KABELSTRÅKEN streckade och PUNKTERNA.
        std::vector<glaze::Shape> vägar;
        for (int v = 0; v < 500; ++v) {
            double cx = r.mellan(x0 - 1200, x1 + 1200), cy = r.mellan(y0 - 1200, y1 + 1200);
            if (tillKorridoren(cx, cy) > 1200.0) {
                --v;
                continue;
            }
            double                    kurs = r.mellan(0, 360);
            const int                 n    = 20 + static_cast<int>(r.nästa() % 21u);
            std::vector<glaze::Point> pts;
            for (int k = 0; k < n; ++k) {
                pts.push_back({cx, cy});
                kurs += r.mellan(-8, 8);
                cx += 25.0 * std::sin(kurs * kPi / 180.0);
                cy += 25.0 * std::cos(kurs * kPi / 180.0);
            }
            const glaze::Point mitt = pts[pts.size() / 2];
            vägar.push_back(glaze::Shape::polyline(
                std::move(pts), {.paint = gfx::Color{90, 90, 90, 255}, .width = glaze::px(2.0f)}));
            glaze::TextStyle ts;
            ts.size     = glaze::mm(2.5f);
            ts.color    = gfx::Color{40, 40, 40, 255};
            ts.halo     = gfx::Color{255, 255, 255, 220};
            ts.minZoom  = 0.75;   // ≈ 1:5000
            ts.priority = 1;
            vägar.push_back(glaze::Shape::text(mitt, "Väg " + std::to_string(v), ts));
        }
        for (int k = 0; k < 40; ++k) {
            const std::size_t         a = r.nästa() % (l.stolpe.size() - 20);
            std::vector<glaze::Point> pts;
            for (std::size_t i = a; i < a + 15; ++i)
                pts.push_back({l.stolpe[i].x + 60.0 + 10.0 * k, l.stolpe[i].y - 40.0});
            vägar.push_back(glaze::Shape::polyline(
                std::move(pts), {.paint    = gfx::Color{200, 60, 160, 255},
                                 .width    = glaze::px(2.0f),
                                 .dash     = {2.0f, 1.0f},
                                 .dashUnit = glaze::Unit::World}));
        }
        std::vector<glaze::Point> punkter;
        while (punkter.size() < 2000) {
            const double cx = r.mellan(x0 - 1200, x1 + 1200);
            const double cy = r.mellan(y0 - 1200, y1 + 1200);
            if (tillKorridoren(cx, cy) < 1200.0)
                punkter.push_back({cx, cy});
        }
        vägar.push_back(glaze::Shape::points(
            std::move(punkter), {.shape = glaze::MarkerShape::Square,
                                 .size  = glaze::px(6.0f),
                                 .fill  = gfx::Color{30, 90, 200, 255}}));
        karta.setShapes(lagerVägar_, std::move(vägar));

        // LEDNINGEN: spannen, stolparna och etiketterna.
        std::vector<glaze::Shape> led;
        for (int i = 0; i + 1 < kStolpar; ++i)
            led.push_back(glaze::Shape::line(
                l.stolpe[static_cast<std::size_t>(i)], l.stolpe[static_cast<std::size_t>(i) + 1],
                {.paint = gfx::Color{20, 20, 20, 255}, .width = glaze::px(2.0f)}));
        stolpeFörst_ = static_cast<int>(led.size());
        for (int i = 0; i < kStolpar; ++i)
            led.push_back(glaze::Shape::marker(
                l.stolpe[static_cast<std::size_t>(i)],
                {.shape        = glaze::MarkerShape::Circle,
                 .size         = glaze::px(6.0f),
                 .fill         = gfx::Color{255, 255, 255, 255},
                 .outline      = gfx::Color{20, 20, 20, 255},
                 .outlineWidth = glaze::px(1.5f)}));
        for (int i = 0; i < kStolpar; ++i) {
            const int        typ = l.typ[static_cast<std::size_t>(i)];
            glaze::TextStyle ts;
            ts.size     = glaze::mm(3.0f);
            ts.color    = gfx::Color{10, 10, 10, 255};
            ts.halo     = gfx::Color{255, 255, 255, 230};
            ts.anchor   = glaze::TextAnchor::Left;
            ts.offsetX  = glaze::mm(2.0f);
            ts.priority = 10 + typ * 10;
            ts.minZoom  = typ == 2 ? 0.126 : (typ == 1 ? 0.252 : 0.504);   // 1:30 000 / 15 000 / 7 500
            led.push_back(glaze::Shape::text(
                l.stolpe[static_cast<std::size_t>(i)],
                "S" + std::to_string(i + 1) + "\nTrästolpe 12 m\nHöjd 48,3 · 35 m", ts));
        }
        karta.setShapes(lagerLedning_, std::move(led));

        karta.setMemoryBudget(std::size_t{1} << 30);
        karta.setCamera(glaze::Camera{.center = l.stolpe[20], .zoom = 1.0});

        karta.onPaint        = [this](glaze::Widget*, glaze::PaintArgs& e) { måla(e.painter); };
        karta.onPaintOverlay = [this](glaze::Widget*, glaze::PaintArgs& e) { överlägg(e.painter); };
        karta.onMouseMove    = [this](glaze::Widget*, glaze::CanvasMouseArgs& e) { mus(e); };
        karta.onTick         = [this](glaze::Widget*, glaze::TickArgs& e) { tick(e.seconds); };

        std::printf("  scen kartan: %d fastigheter, %d markslag, 500 vägar, 40 kabelstråk, "
                    "2000 punkter, %d stolpar — byggd på %.0f ms\n",
                    fastigheter, markslag, kStolpar, msSedan(t0));
    }

    // ------------------------------------------------------- strömningen
    // Synliga världen: lådan kring ytans fyra hörn (ingen tilt på kartan).
    bool synligt(double& x0, double& y0, double& x1, double& y1) const
    {
        const gfx::Sizef s = ytansStorlek(karta);
        x0 = y0 = 1e30;
        x1 = y1 = -1e30;
        for (const glaze::Point h : {glaze::Point{0, 0}, glaze::Point{s.w, 0},
                                     glaze::Point{0, s.h}, glaze::Point{s.w, s.h}}) {
            const auto w = karta.toWorld(h);
            if (!w)
                return false;
            x0 = std::min(x0, w->x);
            y0 = std::min(y0, w->y);
            x1 = std::max(x1, w->x);
            y1 = std::max(y1, w->y);
        }
        return true;
    }

    // Kameran ändrad? Då räknas de önskade plattorna och molnrutorna om.
    void vyKoll()
    {
        const glaze::Camera k = karta.camera();
        const gfx::Sizef    s = ytansStorlek(karta);
        if (k.center.x == förraKam_.center.x && k.center.y == förraKam_.center.y &&
            k.zoom == förraKam_.zoom && s.w == förraYta_.w && s.h == förraYta_.h)
            return;
        förraKam_ = k;
        förraYta_ = s;
        double x0, y0, x1, y1;
        if (!synligt(x0, y0, x1, y1))
            return;

        // PLATTORNA: nivån ur BUFFERTENS meter per px.
        float skala = 1.0f;
        ytansRuta(karta, skala);
        const double mppBuffert = 1.0 / (k.zoom * skala);
        const int    nivå = std::clamp(
            static_cast<int>(std::lround(19.0 - std::log2(mppBuffert / 0.25))), 6, 19);
        const double tm = plattM(nivå);
        const auto   i0 = static_cast<std::int64_t>(std::floor((x0 - kRutnätX) / tm)) - 1;
        const auto   i1 = static_cast<std::int64_t>(std::floor((x1 - kRutnätX) / tm)) + 1;
        const auto   j0 = static_cast<std::int64_t>(std::floor((y0 - kRutnätY) / tm)) - 1;
        const auto   j1 = static_cast<std::int64_t>(std::floor((y1 - kRutnätY) / tm)) + 1;
        önskade_.clear();
        if ((i1 - i0 + 1) * (j1 - j0 + 1) <= 10000)
            for (std::int64_t j = j1; j >= j0; --j)   // radvis uppifrån, som appen
                for (std::int64_t i = i0; i <= i1; ++i) {
                    Platta     p{plattNyckel(nivå, i, j), {}};
                    const auto h = plattHörn(kRutnätX + i * tm, kRutnätY + j * tm, tm);
                    std::copy(h.begin(), h.end(), p.hörn);
                    önskade_.push_back(p);
                }
        önskadeSet_.clear();
        for (const Platta& p : önskade_)
            önskadeSet_.insert(p.nyckel);
        if (nivå != nivå_) {
            nivå_     = nivå;
            insvängd_ = false;
            insvängning_.start();   // 120 ms innan nya förfrågningar
        }
        const auto bort = arbete_.rensa([this](std::uint64_t n) {
            // 3D-vyns jobb (prefixet 2 << 60) är inte kartans att rensa.
            return (n >> 60) == 2 || önskadeSet_.count(n) || önskadeMoln_.count(n);
        });
        for (std::uint64_t n : bort)
            påväg_.erase(n);
        if (insvängd_)
            begärPlattor();
        plattorÄndrade_ = true;

        // MOLNET: sex rutor per axel, hysteres ±25 %.
        const double bredd = std::max(x1 - x0, y1 - y0);
        const double ideal = std::log2(std::max(bredd / 6.0 / 256.0, 1e-9));
        if (molnNivå_ < 0 || ideal > molnNivå_ + 0.5 + 0.32 || ideal < molnNivå_ - 0.5 - 0.32)
            molnNivå_ = std::clamp(static_cast<int>(std::lround(ideal)), 0, 12);
        const double ms = 256.0 * std::pow(2.0, molnNivå_);
        önskadeMolnLista_.clear();
        önskadeMoln_.clear();
        const auto mi0 = static_cast<std::int64_t>(std::floor((x0 - kRutnätX) / ms));
        const auto mi1 = static_cast<std::int64_t>(std::floor((x1 - kRutnätX) / ms));
        const auto mj0 = static_cast<std::int64_t>(std::floor((y0 - kRutnätY) / ms));
        const auto mj1 = static_cast<std::int64_t>(std::floor((y1 - kRutnätY) / ms));
        if ((mi1 - mi0 + 1) * (mj1 - mj0 + 1) <= 4096)
            for (std::int64_t j = mj0; j <= mj1; ++j)
                for (std::int64_t i = mi0; i <= mi1; ++i) {
                    const double rx = kRutnätX + i * ms, ry = kRutnätY + j * ms;
                    if (!näraKorridoren(rx, ry, rx + ms, ry + ms, kBand))
                        continue;
                    const std::uint64_t n =
                        (std::uint64_t{1} << 63) | plattNyckel(molnNivå_, i, j);
                    önskadeMolnLista_.push_back(n);
                    önskadeMoln_.insert(n);
                    if (!molnCache_.count(n) && !påväg_.count(n)) {
                        påväg_.insert(n);
                        arbete_.lägg(n, [n, rx, ry, ms] {
                            nätet(n);
                            auto m = std::make_shared<MolnRuta>(byggMoln(rx, ry, ms));
                            postaTillUi([n, m] { g_karta->molnKom(n, m); });
                        });
                    }
                }
        molnÄndrade_ = true;
    }

    void begärPlattor()
    {
        for (const Platta& p : önskade_)
            if (!cache_.count(p.nyckel) && !påväg_.count(p.nyckel)) {
                påväg_.insert(p.nyckel);
                const std::uint64_t n = p.nyckel;
                arbete_.lägg(n, [n] {
                    nätet(n);
                    auto b = std::make_shared<glaze::Bitmap>(avkoda(n));
                    postaTillUi([n, b] { g_karta->plattaKom(n, std::move(*b)); });
                });
            }
    }

    // En molnruta: korridorens punkter i rutan, högst 262 144, en per m².
    static MolnRuta byggMoln(double rx, double ry, double ms)
    {
        std::array<std::vector<glaze::Float3>, 6> lägen;
        Slump r{blanda(static_cast<std::uint64_t>(rx * 7 + ry * 13))};
        const std::size_t tak = 262144;
        std::size_t       n   = 0;
        for (const glaze::Point& p : ledning().prov) {
            if (p.x < rx - kBand || p.x > rx + ms + kBand || p.y < ry - kBand ||
                p.y > ry + ms + kBand)
                continue;
            // 20 m korridor × 72 m bredd ≈ 1 440 punkter per prov.
            for (int k = 0; k < 1440 && n < tak; ++k) {
                const double x = p.x + r.mellan(-kBand, kBand);
                const double y = p.y + r.mellan(-10.0, 10.0);
                if (x < rx || x >= rx + ms || y < ry || y >= ry + ms)
                    continue;
                const double u = r.enhet();
                int          c = 0;
                for (double acc = kKlasser[0].andel; c < 5 && u > acc; acc += kKlasser[++c].andel) {
                }
                lägen[static_cast<std::size_t>(c)].push_back(
                    {static_cast<float>(x - rx), static_cast<float>(y - ry), 0.0f});
                ++n;
            }
        }
        MolnRuta m;
        for (std::size_t c = 0; c < 6; ++c)
            if (!lägen[c].empty())
                m.klass[c] = glaze::PointCloud(std::move(lägen[c]), {rx, ry, 0.0});
        return m;
    }

    void plattaKom(std::uint64_t n, glaze::Bitmap b)
    {
        påväg_.erase(n);
        lru_.push_front(n);
        cache_[n] = {std::move(b), lru_.begin()};
        while (static_cast<int>(cache_.size()) > kPlattcache) {
            const std::uint64_t äldst = lru_.back();
            if (önskadeSet_.count(äldst))
                break;
            lru_.pop_back();
            cache_.erase(äldst);
            if (g_steg)
                ++g_steg->ström[kStrömPlattor].ut;
        }
        if (g_steg)
            ++g_steg->ström[kStrömPlattor].in;
        if (önskadeSet_.count(n))
            plattorÄndrade_ = true;
    }

    void molnKom(std::uint64_t n, std::shared_ptr<MolnRuta> m)
    {
        påväg_.erase(n);
        molnIn_.push_back({n, std::move(m)});
    }

    // Högst två molnrutor in per bildruta, som appen.
    void tömMolnIn()
    {
        for (int k = 0; k < 2 && !molnIn_.empty(); ++k) {
            auto [n, m] = std::move(molnIn_.front());
            molnIn_.pop_front();
            molnLru_.push_front(n);
            molnCache_[n] = {std::move(m), molnLru_.begin()};
            while (static_cast<int>(molnCache_.size()) > kMolncache) {
                const std::uint64_t äldst = molnLru_.back();
                if (önskadeMoln_.count(äldst))
                    break;
                molnLru_.pop_back();
                molnCache_.erase(äldst);
                if (g_steg)
                    ++g_steg->ström[kStrömMoln].ut;
            }
            if (g_steg)
                ++g_steg->ström[kStrömMoln].in;
            if (önskadeMoln_.count(n))
                molnÄndrade_ = true;
        }
    }

    // HELA lagret byggs om — som appen gör.
    void byggLager()
    {
        if (plattorÄndrade_) {
            plattorÄndrade_ = false;
            const auto                t0 = Klocka::now();
            std::vector<glaze::Shape> s;
            s.reserve(önskade_.size());
            int inne = 0;
            for (const Platta& p : önskade_) {
                auto it = cache_.find(p.nyckel);
                if (it != cache_.end()) {
                    lru_.splice(lru_.begin(), lru_, it->second.var);
                    s.push_back(glaze::Shape::image(p.hörn[0], p.hörn[1], p.hörn[2], p.hörn[3],
                                                    it->second.bild));
                    ++inne;
                } else {
                    s.push_back(glaze::Shape::polygon(
                        std::vector<glaze::Point>{p.hörn[0], p.hörn[1], p.hörn[2], p.hörn[3]},
                        påväg_.count(p.nyckel) ? gfx::Color{79, 79, 89, 255}
                                               : gfx::Color{51, 51, 61, 255}));
                }
            }
            karta.setShapes(lagerPlattor_, std::move(s));
            if (g_steg)
                g_steg->bygg.add(msSedan(t0));
            plattorInne_ = inne;
        }
        if (molnÄndrade_) {
            molnÄndrade_ = false;
            const auto                t0 = Klocka::now();
            std::vector<glaze::Shape> s;
            int                       inne = 0;
            for (std::uint64_t n : önskadeMolnLista_) {
                auto it = molnCache_.find(n);
                if (it == molnCache_.end())
                    continue;
                ++inne;
                for (std::size_t c = 0; c < 6; ++c)
                    if (it->second.moln->klass[c].valid())
                        s.push_back(glaze::Shape::pointCloud(
                            it->second.moln->klass[c],
                            {.size  = glaze::px(c == 0 ? 1.0f : 2.0f),
                             .shape = gfx::MarkerShape::Square,
                             .color = kKlasser[c].färg,
                             .fog   = false}));
            }
            karta.setShapes(lagerMoln_, std::move(s));
            if (g_steg)
                g_steg->bygg.add(msSedan(t0));
            molnInne_ = inne;
        }
    }

    // Är allt som syns inne och köerna tomma?
    bool ikapp() const
    {
        return insvängd_ && plattorInne_ == static_cast<int>(önskade_.size()) &&
               molnInne_ == static_cast<int>(önskadeMolnLista_.size()) && påväg_.empty() &&
               molnIn_.empty() && !plattorÄndrade_ && !molnÄndrade_;
    }

    // -------------------------------------------------------- per bildruta
    void tick(double sek);   // efter TreDFönster

    void måla(glaze::Painter& p)
    {
        if (g_steg) {
            ritning(g_steg->f[0].latens);
            ++g_steg->f[0].ritningar;
        }
        sampla("kartans ritning");
        // RUTNÄTET var 500:e meter med koordinater.
        double x0, y0, x1, y1;
        if (synligt(x0, y0, x1, y1)) {
            const double steg = karta.camera().zoom < 0.2 ? 2000.0 : 500.0;
            const glaze::Stroke   linje{.paint = gfx::Color{0, 0, 0, 60}, .width = glaze::px(1.0f)};
            glaze::TextStyle      ts;
            ts.size  = glaze::mm(2.2f);
            ts.color = gfx::Color{0, 0, 0, 160};
            ts.collide = false;
            ts.anchor  = glaze::TextAnchor::BottomLeft;
            for (double x = std::ceil(x0 / steg) * steg; x <= x1; x += steg) {
                p.drawLine({x, y0}, {x, y1}, linje);
                p.drawText({x, y0}, "E " + std::to_string(static_cast<long>(x)), ts);
            }
            for (double y = std::ceil(y0 / steg) * steg; y <= y1; y += steg) {
                p.drawLine({x0, y}, {x1, y}, linje);
                p.drawText({x0, y}, "N " + std::to_string(static_cast<long>(y)), ts);
            }
        }
        // HOVRAD STOLPE: gloria i två nivåer.
        if (hover_ >= 0) {
            const glaze::Point c = ledning().stolpe[static_cast<std::size_t>(hover_)];
            p.drawMarker(c, {.size = glaze::mm(2.4f), .fill = gfx::Color{255, 220, 0, 115}});
            p.drawMarker(c, {.size = glaze::mm(1.6f), .fill = gfx::Color{255, 255, 255, 160}});
        }
        // 3D-VYNS KAMERA som en markör.
        if (pose_) {
            // En spetsig triangel i blickens riktning, 12 m lång.
            const double v = poseGir_ * kPi / 180.0, fx = std::sin(v), fy = std::cos(v);
            const glaze::Point c = *pose_;
            p.fillPolygon({{c.x + fx * 12.0, c.y + fy * 12.0},
                           {c.x - fx * 4.0 + fy * 5.0, c.y - fy * 4.0 - fx * 5.0},
                           {c.x - fx * 4.0 - fy * 5.0, c.y - fy * 4.0 + fx * 5.0}},
                          gfx::Color{220, 30, 30, 255});
        }
    }

    void överlägg(glaze::Painter& p)
    {
        const gfx::Sizef s = p.size();
        // SKALSTOCKEN och NORRPILEN.
        p.fillRect({16, s.h - 28, 120, 6}, gfx::Color{0, 0, 0, 200});
        glaze::TextStyle ts;
        ts.size   = glaze::mm(2.5f);
        ts.anchor = glaze::TextAnchor::BottomLeft;
        ts.halo   = gfx::Color{255, 255, 255, 220};
        ts.collide = false;
        p.drawText({16, s.h - 32},
                   std::to_string(static_cast<int>(120.0 / karta.camera().zoom)) + " m", ts);
        p.fillPolygon({{s.w - 30, 20}, {s.w - 38, 44}, {s.w - 22, 44}},
                      gfx::Color{20, 20, 20, 255});
        // TRE RADER STATUS.
        ts.anchor = glaze::TextAnchor::TopLeft;
        p.drawText({16, 16},
                   "Plattor " + std::to_string(plattorInne_) + "/" +
                       std::to_string(önskade_.size()) + " · nivå " + std::to_string(nivå_) +
                       "\nMoln " + std::to_string(molnInne_) + "/" +
                       std::to_string(önskadeMolnLista_.size()) + " · nivå " +
                       std::to_string(molnNivå_) + "\nCache " + std::to_string(cache_.size()) +
                       " plattor, " + std::to_string(molnCache_.size()) + " molnrutor",
                   ts);
    }

    void mus(glaze::CanvasMouseArgs& e)
    {
        if (e.world) {
            char n[48], o[48];
            std::snprintf(n, sizeof n, "N %.3f", e.world->y);
            std::snprintf(o, sizeof o, "E %.3f", e.world->x);
            statusBar.setPanelText(1, std::string(n));
            statusBar.setPanelText(2, std::string(o));
        }
        int ny = -1;
        for (const glaze::HitResult& h : karta.hitTest(e.local))
            if (h.layer == lagerLedning_ && h.index >= stolpeFörst_ &&
                h.index < stolpeFörst_ + kStolpar) {
                ny = h.index - stolpeFörst_;
                break;
            }
        if (ny != hover_) {
            hover_ = ny;
            if (ny >= 0)
                ++träffar_;
            karta.invalidate();
        }
    }

    void sättPose(glaze::Point p, float gir)
    {
        if (pose_ && std::hypot(pose_->x - p.x, pose_->y - p.y) < 0.5)
            return;
        pose_    = p;
        poseGir_ = gir;
        karta.invalidate();   // som appen: varje flytt över 0,5 m ritar om kartan
    }

    int  träffar() const { return träffar_; }
    std::size_t köDjup() const { return påväg_.size() + molnIn_.size(); }

    static KartFönster* g_karta;

private:
    struct Cachad {
        glaze::Bitmap                     bild;
        std::list<std::uint64_t>::iterator var;
    };
    struct CachatMoln {
        std::shared_ptr<MolnRuta>          moln;
        std::list<std::uint64_t>::iterator var;
    };

    Arbetare&   arbete_;
    glaze::Timer insvängning_;
    bool        insvängd_ = true;
    int         lagerPlattor_ = 0, lagerYtor_ = 0, lagerMoln_ = 0, lagerVägar_ = 0,
                lagerLedning_ = 0, stolpeFörst_ = 0;
    glaze::Camera förraKam_{.zoom = -1.0};
    gfx::Sizef    förraYta_{};
    int           nivå_ = -1, molnNivå_ = -1;
    std::vector<Platta>                             önskade_;
    std::unordered_set<std::uint64_t>               önskadeSet_;
    std::vector<std::uint64_t>                      önskadeMolnLista_;
    std::unordered_set<std::uint64_t>               önskadeMoln_;
    std::unordered_set<std::uint64_t>               påväg_;
    std::unordered_map<std::uint64_t, Cachad>       cache_;
    std::list<std::uint64_t>                        lru_;
    std::unordered_map<std::uint64_t, CachatMoln>   molnCache_;
    std::list<std::uint64_t>                        molnLru_;
    std::deque<std::pair<std::uint64_t, std::shared_ptr<MolnRuta>>> molnIn_;
    bool plattorÄndrade_ = false, molnÄndrade_ = false;
    int  plattorInne_ = 0, molnInne_ = 0;
    int  hover_ = -1, träffar_ = 0;
    std::optional<glaze::Point> pose_;
    float                       poseGir_ = 0.0f;
};

KartFönster* KartFönster::g_karta = nullptr;

// ================================================================ 3D-VYN
constexpr int    kRutor3   = 20;     // 20 × 20 kring ögat
constexpr double kRuta3    = 75.0;   // m
constexpr int    kHörn3    = 17;
constexpr int    kMolnTak3 = 50000;
constexpr int    kInPerBild3 = 8;

std::uint64_t rutNyckel(std::int64_t i, std::int64_t j)
{
    return (std::uint64_t{2} << 60) ^ (static_cast<std::uint64_t>(i & 0x3FFFFFFF) << 30) ^
           static_cast<std::uint64_t>(j & 0x3FFFFFFF);
}

struct Ruta3 {
    glaze::Mesh                  terräng;
    glaze::Bitmap                bild;
    glaze::PointCloud            moln;
    std::vector<glaze::Instance> träd;
};

class TreDFönster : public Fönster {
public:
    TreDFönster(Arbetare& arbete) : arbete_(arbete)
    {
        setTitle("app_perf — 3D-vy");
        vy.name = "vy";
        vy.dock = glaze::Dock::Fill;
        vy.setBorder(false);
        root().adopt(vy);
        setContentSize(1280, 800);
        if (!g_synlig)
            setOffscreen(true);
    }

    glaze::Canvas vy;

    void byggScen()
    {
        const auto t0 = Klocka::now();
        lagerTerräng_ = vy.addLayer("terräng");
        vy.setLayerPlacement(lagerTerräng_, glaze::LayerPlacement::Terrain);
        lagerMoln_   = vy.addLayer("moln");
        lagerTräd_   = vy.addLayer("träd");
        lagerStolpar_ = vy.addLayer("stolpar");
        lagerLinor_  = vy.addLayer("linor");
        for (int l : {lagerTerräng_, lagerMoln_, lagerTräd_})
            for (int k = 0; k < kRutor3 * kRutor3; ++k)
                vy.addShape(l, glaze::Shape{});
        plats_.assign(static_cast<std::size_t>(kRutor3 * kRutor3), 0);
        ledig_.clear();
        for (int k = kRutor3 * kRutor3 - 1; k >= 0; --k)
            ledig_.push_back(k);
        kon_ = byggKon({{24, 120.0}, {6, 0.0}});

        // STOLPARNA: tolv mesher om 30–150 k trianglar, delade.
        std::vector<glaze::Mesh> typer;
        for (int t = 0; t < 12; ++t)
            typer.push_back(byggStolpe(30000 + t * 10900, 1000u + static_cast<std::uint64_t>(t)));
        const Ledning& l = ledning();
        for (int i = 0; i < kStolpar; ++i) {
            const glaze::Point p = l.stolpe[static_cast<std::size_t>(i)];
            vy.addShape(lagerStolpar_,
                        glaze::Shape::mesh(typer[static_cast<std::size_t>(blanda(i) % 12u)],
                                           {.at  = {p.x, p.y, markhöjd(p.x, p.y)},
                                            .yaw = static_cast<float>(l.riktning[static_cast<std::size_t>(i)])}));
        }
        // LINORNA: tre faser per spann, kedjekurvor om 51 punkter.
        for (int i = 0; i + 1 < kStolpar; ++i) {
            const glaze::Point a = l.stolpe[static_cast<std::size_t>(i)];
            const glaze::Point b = l.stolpe[static_cast<std::size_t>(i) + 1];
            const double       ha = markhöjd(a.x, a.y) + 24.0, hb = markhöjd(b.x, b.y) + 24.0;
            const double       vx = -(b.y - a.y) / kSpann, vy2 = (b.x - a.x) / kSpann;
            for (int fas = 0; fas < 3; ++fas) {
                std::vector<glaze::Point3> kurva;
                for (int k = 0; k <= 50; ++k) {
                    const double t = k / 50.0;
                    kurva.push_back({a.x + (b.x - a.x) * t + vx * (fas - 1) * 0.9,
                                     a.y + (b.y - a.y) * t + vy2 * (fas - 1) * 0.9,
                                     ha + (hb - ha) * t - 4.0 * 0.6 * t * (1.0 - t)});
                }
                vy.addShape(lagerLinor_,
                            glaze::Shape::polyline3(std::move(kurva),
                                                    {.paint = gfx::Color{30, 30, 30, 255},
                                                     .width = glaze::world(0.03f)}));
            }
        }
        vy.onPaintOverlay = [this](glaze::Widget*, glaze::PaintArgs& e) { hud(e.painter); };
        vy.onPaint        = [this](glaze::Widget*, glaze::PaintArgs&) { ritad(); };
        vy.onTick         = [this](glaze::Widget*, glaze::TickArgs& e) { tick(e.seconds); };
        vy.onKeyDown      = [](glaze::Widget*, glaze::KeyArgs& e) {
            switch (e.key) {
            case gfx::Key::W: case gfx::Key::A: case gfx::Key::S:
            case gfx::Key::D: case gfx::Key::Q: case gfx::Key::E:
                e.handled = true;
                break;
            default:
                break;
            }
        };
        std::printf("  scen 3D-vyn: 300 stolpar ur 12 mesher om 30–150 k trianglar, %d linor — "
                    "byggd på %.0f ms\n",
                    3 * (kStolpar - 1), msSedan(t0));
    }

    // ---------------------------------------------------- kameran och input
    void ställ(glaze::Point öga, double gir)
    {
        öga_ = öga;
        gir_ = gir;
        sättKamera();
    }
    double fart = 200.0 / 3.6;   // m/s

    // ------------------------------------------------------- strömningen
    void strömKoll()
    {
        const auto ci = static_cast<std::int64_t>(std::floor((öga_.x - kRutnätX) / kRuta3));
        const auto cj = static_cast<std::int64_t>(std::floor((öga_.y - kRutnätY) / kRuta3));
        if (ci == ci_ && cj == cj_)
            return;
        ci_ = ci;
        cj_ = cj;
        std::unordered_set<std::uint64_t>                     ny;
        std::vector<std::pair<double, std::pair<std::int64_t, std::int64_t>>> saknas;
        for (std::int64_t j = cj - kRutor3 / 2; j < cj + kRutor3 / 2; ++j)
            for (std::int64_t i = ci - kRutor3 / 2; i < ci + kRutor3 / 2; ++i) {
                const std::uint64_t n = rutNyckel(i, j);
                ny.insert(n);
                if (!platsAv_.count(n) && !påväg_.count(n))
                    saknas.push_back({std::hypot(static_cast<double>(i - ci), static_cast<double>(j - cj)),
                                      {i, j}});
            }
        önskade_ = std::move(ny);
        // SLÄPP det som lämnat rutnätet — platsen blir ledig.
        const auto t0 = Klocka::now();
        for (auto it = platsAv_.begin(); it != platsAv_.end();)
            if (!önskade_.count(it->first)) {
                for (int l : {lagerTerräng_, lagerMoln_, lagerTräd_})
                    vy.setShape(l, it->second, glaze::Shape{});
                ledig_.push_back(it->second);
                it = platsAv_.erase(it);
                if (g_steg)
                    ++g_steg->ström[kStrömRutor].ut;
            } else {
                ++it;
            }
        if (g_steg)
            g_steg->bygg.add(msSedan(t0));
        const auto bort = arbete_.rensa([this](std::uint64_t n) {
            return (n >> 60) != 2 || önskade_.count(n);
        });
        for (std::uint64_t n : bort)
            påväg_.erase(n);
        // BEGÄR det som saknas, närmast först.
        std::sort(saknas.begin(), saknas.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [avst, ij] : saknas) {
            const std::uint64_t n = rutNyckel(ij.first, ij.second);
            påväg_.insert(n);
            const double rx = kRutnätX + ij.first * kRuta3, ry = kRutnätY + ij.second * kRuta3;
            arbete_.lägg(n, [n, rx, ry] {
                nätet(n);
                auto r = std::make_shared<Ruta3>(byggRuta(n, rx, ry));
                postaTillUi([n, r] { g_tred->rutaKom(n, r); });
            });
        }
    }

    static Ruta3 byggRuta(std::uint64_t n, double rx, double ry)
    {
        Ruta3           r;
        glaze::MeshDesc d;
        d.origin = {rx, ry, 0.0};
        for (int j = 0; j < kHörn3; ++j)
            for (int i = 0; i < kHörn3; ++i) {
                const double x = kRuta3 * i / (kHörn3 - 1), y = kRuta3 * j / (kHörn3 - 1);
                d.positions.push_back({static_cast<float>(x), static_cast<float>(y),
                                       static_cast<float>(markhöjd(rx + x, ry + y))});
                d.uvs.push_back({static_cast<float>(i) / (kHörn3 - 1),
                                 1.0f - static_cast<float>(j) / (kHörn3 - 1)});
            }
        for (int j = 0; j + 1 < kHörn3; ++j)
            for (int i = 0; i + 1 < kHörn3; ++i) {
                const auto a = static_cast<std::uint32_t>(j * kHörn3 + i);
                d.indices.insert(d.indices.end(),
                                 {a, a + 1, a + kHörn3 + 1, a, a + kHörn3 + 1, a + kHörn3});
            }
        r.terräng = glaze::Mesh(std::move(d));
        r.bild    = avkoda(n);
        Slump s{blanda(n)};
        // MOLNET där korridoren går — höjd över mark som skalär.
        if (näraKorridoren(rx, ry, rx + kRuta3, ry + kRuta3, kBand)) {
            std::vector<glaze::Float3> lägen;
            std::vector<gfx::Color>    färger;
            std::vector<float>         hag;
            lägen.reserve(kMolnTak3);
            for (int k = 0; k < kMolnTak3 * 4 && static_cast<int>(lägen.size()) < kMolnTak3; ++k) {
                const double x = s.mellan(0, kRuta3), y = s.mellan(0, kRuta3);
                if (tillKorridoren(rx + x, ry + y) > kBand)
                    continue;
                const double u = s.enhet();
                int          c = 0;
                for (double acc = kKlasser[0].andel; c < 5 && u > acc; acc += kKlasser[++c].andel) {
                }
                const float h = kKlasser[c].höjd * static_cast<float>(s.mellan(0.5, 1.2));
                lägen.push_back({static_cast<float>(x), static_cast<float>(y),
                                 static_cast<float>(markhöjd(rx + x, ry + y)) + h});
                färger.push_back(kKlasser[c].färg);
                hag.push_back(h);
            }
            if (!lägen.empty())
                r.moln = glaze::PointCloud(std::move(lägen), {rx, ry, 0.0}, std::move(färger),
                                           std::move(hag));
        }
        // TRÄDEN, utom i ledningsgatan.
        for (int k = 0; k < 165; ++k) {
            const double x = rx + s.mellan(0, kRuta3), y = ry + s.mellan(0, kRuta3);
            if (tillKorridoren(x, y) < 20.0)
                continue;
            r.träd.push_back({.at    = {x, y, markhöjd(x, y)},
                              .yaw   = static_cast<float>(s.mellan(0, 360)),
                              .scale = {static_cast<float>(s.mellan(0.7, 1.3)),
                                        static_cast<float>(s.mellan(0.7, 1.3)),
                                        static_cast<float>(s.mellan(1.0, 2.6))},
                              .color = gfx::Color{static_cast<std::uint8_t>(s.mellan(40, 90)),
                                                  static_cast<std::uint8_t>(s.mellan(110, 170)),
                                                  50, 255}});
        }
        return r;
    }

    void rutaKom(std::uint64_t n, std::shared_ptr<Ruta3> r)
    {
        påväg_.erase(n);
        if (önskade_.count(n))
            in_.push_back({n, std::move(r)});
    }

    // Högst åtta rutor in per bildruta, i lediga platser.
    void tömIn()
    {
        const auto t0 = Klocka::now();
        int        k  = 0;
        for (; k < kInPerBild3 && !in_.empty(); ++k) {
            auto [n, r] = std::move(in_.front());
            in_.pop_front();
            if (!önskade_.count(n) || platsAv_.count(n) || ledig_.empty())
                continue;
            const int p = ledig_.back();
            ledig_.pop_back();
            platsAv_[n] = p;
            vy.setShape(lagerTerräng_, p,
                        glaze::Shape::mesh(r->terräng, {},
                                           {.lit = false, .texture = r->bild}));
            vy.setShape(lagerMoln_, p,
                        r->moln.valid()
                            ? glaze::Shape::pointCloud(r->moln, {.size = glaze::px(2.0f)})
                            : glaze::Shape{});
            vy.setShape(lagerTräd_, p,
                        r->träd.empty() ? glaze::Shape{}
                                        : glaze::Shape::instances(glaze::Shape::mesh(kon_),
                                                                  std::move(r->träd)));
            if (g_steg)
                ++g_steg->ström[kStrömRutor].in;
        }
        if (k > 0 && g_steg)
            g_steg->bygg.add(msSedan(t0));
    }

    bool ikapp() const
    {
        return in_.empty() && påväg_.empty() &&
               platsAv_.size() == static_cast<std::size_t>(kRutor3 * kRutor3);
    }
    std::size_t köDjup() const { return påväg_.size() + in_.size(); }

    // Appen stänger 3D-vyn och SLÄPPER sina rutor (formerna töms, köerna
    // rensas) — nästa öppning läser in allt igen.
    void släppAllt()
    {
        for (const auto& [n, p] : platsAv_) {
            for (int l : {lagerTerräng_, lagerMoln_, lagerTräd_})
                vy.setShape(l, p, glaze::Shape{});
            ledig_.push_back(p);
        }
        platsAv_.clear();
        in_.clear();
        önskade_.clear();
        arbete_.rensa([](std::uint64_t n) { return (n >> 60) != 2; });
        påväg_.clear();
        ci_ = cj_ = std::numeric_limits<std::int64_t>::min();
    }
    glaze::Point öga() const { return öga_; }
    double       gir() const { return gir_; }

    void tick(double sek);   // efter sessionen

    static TreDFönster* g_tred;

private:
    void sättKamera()
    {
        vy.setCamera(glaze::Camera3D::lookFrom(
            {öga_.x, öga_.y, markhöjd(öga_.x, öga_.y) + 2.0}, static_cast<float>(gir_),
            -6.0f));
        strömKoll();
        if (KartFönster::g_karta)
            KartFönster::g_karta->sättPose(öga_, static_cast<float>(gir_));
    }

    void ritad()
    {
        sampla("3D-vyns ritning");
        if (g_steg) {
            ++g_steg->f[1].ritningar;
            ritning(g_steg->f[1].latens);
        }
    }

    void hud(glaze::Painter& p)
    {
        char rad[160];
        std::snprintf(rad, sizeof rad,
                      "E %.1f  N %.1f\nRiktning %.0f°  Fart %.0f km/h\nRutor %zu/%d · kö %zu",
                      öga_.x, öga_.y, gir_, fart * 3.6, platsAv_.size(), kRutor3 * kRutor3,
                      köDjup());
        ritaHud(p, rad, gir_);
    }

    Arbetare&    arbete_;
    int          lagerTerräng_ = 0, lagerMoln_ = 0, lagerTräd_ = 0, lagerStolpar_ = 0,
                 lagerLinor_ = 0;
    glaze::Mesh  kon_;
    glaze::Point öga_{};
    double       gir_ = 0.0;
    std::int64_t ci_ = std::numeric_limits<std::int64_t>::min(), cj_ = ci_;
    std::unordered_set<std::uint64_t>      önskade_;
    std::unordered_set<std::uint64_t>      påväg_;
    std::unordered_map<std::uint64_t, int> platsAv_;
    std::vector<int>                       plats_;
    std::vector<int>                       ledig_;
    std::deque<std::pair<std::uint64_t, std::shared_ptr<Ruta3>>> in_;
};

TreDFönster* TreDFönster::g_tred = nullptr;

Session* g_session = nullptr;

void KartFönster::tick(double sek)
{
    vyKoll();
    tömMolnIn();
    byggLager();
    statusBar.setPanelText(4, "Plattor " + std::to_string(plattorInne_) + "/" +
                                  std::to_string(önskade_.size()));
    statusBar.setPanelText(5, "Moln " + std::to_string(molnInne_) + "/" +
                                  std::to_string(önskadeMolnLista_.size()));
    if (g_session)
        g_session->tick(0, sek);
}

void TreDFönster::tick(double sek)
{
    // Förstapersonen: hållna tangenter flyttar ögat, marken följs.
    if (förstaperson(*this, sek, fart, öga_, gir_)) {
        input();
        sättKamera();
    }
    tömIn();
    if (g_session)
        g_session->tick(1, sek);
}

// Den närmaste stolpens index till en världspunkt.
int närmasteStolpe(glaze::Point p)
{
    int    bäst = 0;
    double d    = 1e30;
    for (int i = 0; i < kStolpar; ++i) {
        const glaze::Point s = ledning().stolpe[static_cast<std::size_t>(i)];
        const double       e = (s.x - p.x) * (s.x - p.x) + (s.y - p.y) * (s.y - p.y);
        if (e < d) {
            d    = e;
            bäst = i;
        }
    }
    return bäst;
}

} // namespace

int main(int argc, char** argv)
{
    int varv = 0;   // 0 = sessionen, annars varvtestet
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
        else if (std::strcmp(argv[i], "--synlig") == 0)
            g_synlig = true;
        else if (std::strcmp(argv[i], "--varv") == 0 && i + 1 < argc)
            varv = std::max(1, std::atoi(argv[++i]));
    }
    std::printf("=== app_perf — en app på riktigt: kartvy och 3D-vy (%s, %s%s) ===\n", kBygge,
                g_synlig ? "synliga fönster" : "dolda fönster",
                varv > 0 ? (", varvtest " + std::to_string(varv) + " varv").c_str() : "");
    const Minne före = läsMinne();
    std::printf("  minne vid start: process %s\n", mb(före.process).c_str());

    auto t0 = Klocka::now();
    (void)pngPool();
    (void)ledning();
    std::printf("  data: %d plattbilder (PNG) och ledningen — %.0f ms\n", kPngAntal, msSedan(t0));

    Arbetare     arbete(4);
    KartFönster  karta(arbete);
    TreDFönster  tred(arbete);
    KartFönster::g_karta = &karta;
    TreDFönster::g_tred  = &tred;
    karta.tred           = &tred;
    karta.byggScen();
    tred.byggScen();
    std::printf("  minne efter scenerna: process %s\n", mb(läsMinne().process).c_str());

    Session s({{"kartan", &karta, &karta.karta, [&karta] { return karta.köDjup(); }},
               {"3D", &tred, &tred.vy, [&tred] { return tred.köDjup(); }}},
              {"plattor", "moln", "3D-rutor"});
    g_session = &s;
    const Ledning& l = ledning();

    // ---- pekarens väg: en lissajous över ytan.
    const auto hover = [&karta](int i) {
        const glaze::Point p = lissajous(i, ytansStorlek(karta.karta));
        karta.injicera(karta.karta, gfx::EventType::MouseMotion, p.x, p.y);
    };
    const auto vila = [&karta, &tred](bool tredOckså) {
        return [&karta, &tred, tredOckså](int i) {
            return i > 30 && karta.ikapp() && (!tredOckså || tred.ikapp());
        };
    };

    // 3D-vyn öppnas vid stolpe 60, riktad längs ledningen — av steget självt;
    // kartan driver tills den är uppe.
    const auto öppna3D = [&] {
        if (tred.isOpen())
            return;
        tred.fart = 200.0 / 3.6;
        tred.show(karta);
        tred.vy.focus();
        tred.ställ(l.stolpe[60], l.riktning[60]);
        tred.vy.setTicking(true);
    };

    s.lägg({"kartan: start", 0, {true, false}, 30.0, [&karta](int i) {
                return i > 5 && karta.ikapp();
            }, true});
    if (varv > 0) {
        for (int v = 1; v <= varv; ++v) {
            const std::string fv = "varv " + std::to_string(v) + ": ";
            s.lägg({fv + "kartan resa 8 km ut", 0, {true, false}, 300.0, [&](int i) {
                        if (i == 0)
                            karta.karta.setCamera(glaze::Camera{.center = l.stolpe[20], .zoom = 0.5});
                        const double kurs = 45.0 * kPi / 180.0;
                        return panorera(karta, karta.karta, i, 22, -9.4 * std::sin(kurs), 9.4 * std::cos(kurs));
                    }});
            s.lägg({fv + "kartan resa tillbaka", 0, {true, false}, 300.0, [&](int i) {
                        const double kurs = 45.0 * kPi / 180.0;
                        return panorera(karta, karta.karta, i, 22, 9.4 * std::sin(kurs), -9.4 * std::cos(kurs));
                    }});
            s.lägg({fv + "A kartan vila", 0, {true, false}, 60.0, [&](int i) {
                        if (i == 0)
                            karta.karta.setCamera(glaze::Camera{.center = l.stolpe[20], .zoom = 0.5});
                        return i > 30 && karta.ikapp();
                    }, true});
            s.lägg({fv + "3D öppnas", 0, {true, true}, 120.0, [&](int i) {
                        öppna3D();
                        return i > 5 && tred.ikapp();
                    }, true});
            s.lägg({fv + "3D körning", 1, {false, true}, 120.0, [&](int i) {
                        if (i == 0)
                            tred.tangent(gfx::Key::W, true);
                        if (i == 300) {
                            tred.tangent(gfx::Key::W, false);
                            return true;
                        }
                        return false;
                    }});
            for (int h = 0; h < 2; ++h)
                s.lägg({fv + (h == 0 ? "3D hopp 3 km" : "3D hopp tillbaka"), 1, {false, true}, 120.0,
                        [&, h](int i) {
                            if (i == 0) {
                                const int m = närmasteStolpe(tred.öga());
                                const int n = h == 0 ? std::min(kStolpar - 1, m + 86)
                                                     : std::max(0, m - 86);
                                tred.ställ(l.stolpe[static_cast<std::size_t>(n)],
                                           l.riktning[static_cast<std::size_t>(n)]);
                            }
                            return i > 5 && tred.ikapp();
                        },
                        true});
            s.lägg({fv + "B 3D stängd 5 s", 0, {false, false}, 30.0,
                    [&tred](int) {
                        tred.close();
                        tred.släppAllt();
                        return true;
                    },
                    false, 5000});
        }
    } else {
        s.lägg({"kartan: hover", 0, {true, false}, 60.0, [&](int i) {
                    hover(i);
                    return i >= 300;
                }});
        // 3 km: 8 drag om 375 logiska px vid zoom 1 (1 px ≈ 1 m).
        s.lägg({"kartan: panorering 3 km", 0, {true, false}, 120.0, [&](int i) {
                    return panorera(karta, karta.karta, i, 8, -9.4, 0.0);
                }});
        s.lägg({"kartan: zoom ut och in", 0, {true, false}, 120.0, [&karta](int i) {
                    const gfx::Sizef y = ytansStorlek(karta.karta);
                    if (i < 8 * 6 && i % 6 == 0)
                        karta.injicera(karta.karta, gfx::EventType::MouseWheel, y.w * 0.3, y.h * 0.4,
                                       gfx::MouseButton::None, -1.0f);
                    else if (i >= 120 && i < 120 + 8 * 6 && i % 6 == 0)
                        karta.injicera(karta.karta, gfx::EventType::MouseWheel, y.w * 0.7, y.h * 0.6,
                                       gfx::MouseButton::None, 1.0f);
                    return i >= 240;
                }});
        s.lägg({"kartan: vila", 0, {true, false}, 60.0, vila(false), true});
        // 8 km längs ledningen, utzoomad (zoom 0,5): 22 drag om 375 px.
        s.lägg({"kartan: resa 8 km", 0, {true, false}, 300.0, [&](int i) {
                    if (i == 0) {
                        glaze::Camera k = karta.karta.camera();
                        k.zoom          = 0.5;
                        karta.karta.setCamera(k);
                    }
                    const double kurs = 45.0 * kPi / 180.0;
                    return panorera(karta, karta.karta, i, 22, -9.4 * std::sin(kurs), 9.4 * std::cos(kurs));
                }});
        s.lägg({"kartan: vila efter resan", 0, {true, false}, 60.0, vila(false), true});

        s.lägg({"3D: start (400 rutor)", 0, {true, true}, 120.0, [&](int i) {
                    öppna3D();
                    return i > 5 && tred.ikapp();
                }, true});
        s.lägg({"3D: körning 200 km/h", 1, {false, true}, 120.0, [&](int i) {
                    if (i == 0)
                        tred.tangent(gfx::Key::W, true);
                    if (i == 600) {
                        tred.tangent(gfx::Key::W, false);
                        return true;
                    }
                    return false;
                }});
        s.lägg({"3D: vridning", 1, {false, true}, 60.0, [&](int i) {
                    if (i == 0)
                        tred.tangent(gfx::Key::E, true);
                    if (i == 160) {
                        tred.tangent(gfx::Key::E, false);
                        return true;
                    }
                    return false;
                }});
        s.lägg({"3D: hopp 3 km", 1, {false, true}, 120.0, [&](int i) {
                    if (i == 0) {
                        const int    m = närmasteStolpe(tred.öga());
                        const int    n = std::min(kStolpar - 1, m + 86);
                        tred.input();
                        tred.ställ(l.stolpe[static_cast<std::size_t>(n)],
                                   l.riktning[static_cast<std::size_t>(n)]);
                    }
                    return i > 5 && tred.ikapp();
                }, true});
        s.lägg({"3D: hopp 3 km tillbaka", 1, {false, true}, 120.0, [&](int i) {
                    if (i == 0) {
                        const int m = närmasteStolpe(tred.öga());
                        const int n = std::max(0, m - 86);
                        tred.input();
                        tred.ställ(l.stolpe[static_cast<std::size_t>(n)],
                                   l.riktning[static_cast<std::size_t>(n)]);
                    }
                    return i > 5 && tred.ikapp();
                }, true});
        s.lägg({"3D: vila", 1, {false, true}, 60.0, vila(true), true});
        s.lägg({"båda: körning och hover", 0, {true, true}, 120.0, [&](int i) {
                    if (i == 0) {
                        tred.fart = 90.0 / 3.6;
                        tred.tangent(gfx::Key::W, true);
                    }
                    hover(i);
                    if (i == 400) {
                        tred.tangent(gfx::Key::W, false);
                        return true;
                    }
                    return false;
                }});
        s.lägg({"båda: vila", 0, {true, true}, 60.0, vila(true), true});
        // INGENTING HÄNDER: båda fönstren öppna och orörda — hård regel 1 säger
        // ~0 % CPU och inga ritningar.
        s.lägg({"båda orörda 5 s", 0, {false, false}, 30.0, [](int) { return true; }, false, 5000});
        // 3D-VYN STÄNGS: släpps minnet?
        s.lägg({"3D-vyn stängd 5 s", 0, {false, false}, 30.0,
                [&tred](int) {
                    tred.close();
                    return true;
                },
                false, 5000});
    }
    s.reservera();
    if (g_verbose)
        s.profilera();

    karta.onShow = [&](glaze::Widget*, glaze::EventArgs&) { s.börja(); };
    karta.karta.onCameraChange = [&karta](glaze::Widget*, glaze::ChangeArgs&) { karta.vyKoll(); };

    t0 = Klocka::now();
    karta.run();
    const double total = msSedan(t0) / 1000.0;

    g_liv->store(false);
    arbete.stoppa();
    g_session = nullptr;

    if (s.mått().empty())
        fel("kartans fönster öppnades inte — ingen yta?");
    if (varv == 0 && karta.träffar() == 0)
        fel("hovret träffade aldrig en stolpe — träffprovet kördes inte");

    std::printf("  totalt %.1f s\n", total);

    // SAMMANFATTNINGEN — raderna som avslutar rapporten.
    std::printf("\n  | Steg | Kartan: frame median / p90 ms, andel > 25 ms | 3D: frame median / "
                "p90 ms, andel > 25 ms | CPU (en kärna) | Väckningar/s | Processen efter (stegets topp) "
                "| Bildcachen |\n"
                "  |---|---|---|---|---|---|---|\n");
    for (const StegMått& m : s.mått()) {
        std::string kol[2];
        for (int w = 0; w < 2; ++w) {
            const FönsterMått& f = m.f[w];
            char b[96];
            if (!f.frame.tom())
                std::snprintf(b, sizeof b, "%.1f / %.1f, %.0f %%", f.frame.median(),
                              f.frame.kvantil(0.9),
                              100.0 * f.frame.över(25.0) / static_cast<double>(f.frame.v.size()));
            else if (f.ritningar > 0)
                std::snprintf(b, sizeof b, "%d ritningar", f.ritningar);
            else
                std::snprintf(b, sizeof b, "—");
            kol[w] = b;
        }
        char cpu[32] = "—", väck[32] = "—";
        if (m.cpuProcent >= 0.0)
            std::snprintf(cpu, sizeof cpu, "%.0f %%", m.cpuProcent);
        if (m.väckPerSek >= 0.0)
            std::snprintf(väck, sizeof väck, "%.0f", m.väckPerSek);
        std::printf("  | %s | %s | %s | %s | %s | %s (%s) | %s |\n", m.namn.c_str(),
                    kol[0].c_str(), kol[1].c_str(), cpu, väck, mb(m.minne.process).c_str(),
                    mb(m.toppSteg).c_str(), mb(static_cast<double>(m.bildcache) / 1.0e6).c_str());
    }
    if (!s.klar() || g_fel) {
        std::printf("=== app_perf: FALLERAR (%d fel) ===\n", g_fel);
        return 1;
    }
    if (varv > 0) {
        // VARVEN: mätpunkterna A (kartan ensam, i vila) och B (3D-vyn stängd).
        std::vector<double> a, b, ca, cb;
        for (const StegMått& m : s.mått()) {
            if (m.namn.find(": A ") != std::string::npos) {
                a.push_back(m.minne.process);
                ca.push_back(static_cast<double>(m.bildcache) / 1.0e6);
            } else if (m.namn.find(": B ") != std::string::npos) {
                b.push_back(m.minne.process);
                cb.push_back(static_cast<double>(m.bildcache) / 1.0e6);
            }
        }
        std::printf("\n  | Varv | A: kartan i vila | B: 3D-vyn stängd | Bildcachen A / B |\n"
                    "  |---|---|---|---|\n");
        for (std::size_t v = 0; v < a.size() && v < b.size(); ++v)
            std::printf("  | %zu | %s | %s | %s / %s |\n", v + 1, mb(a[v]).c_str(),
                        mb(b[v]).c_str(), mb(ca[v]).c_str(), mb(cb[v]).c_str());
        if (a.size() >= 3 && b.size() >= 3) {
            const double n = static_cast<double>(a.size() - 2);
            std::printf("  tillväxt per varv efter insvängningen (varv 2 → %zu): A %+.0f MB, "
                        "B %+.0f MB\n",
                        a.size(), (a.back() - a[1]) / n, (b.back() - b[1]) / n);
        }
    }
    std::printf("=== app_perf: sessionen gick igenom ===\n");
    return 0;
}
