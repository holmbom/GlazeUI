// canvas_perf — ritytans prestanda.
//
// Kör:  ./build/tests/canvas_perf [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
//
// En RIKTIG `glaze::Canvas` i ett DOLT fönster på 1280 × 800 logiska px,
// ritad genom den riktiga backenden (D3D11 eller GL). Scenen är en karta i
// SWEREF med y uppåt, 2 × 2 rutor om 4096 m (ritytans spatiala gruppering),
// i fyra lager med `DrawOrder::ByKind` — kartlagrets ordning:
//
//   0 Ytor     3 000 hus (sex punkter, kontur 1 px) och 40 sjöar med hål
//              (kontur 0,3 mm)
//   1 Vägar    1 500 vägar (16 punkter, 1,0/0,5 mm, runda skarvar) och 400
//              stigar (streckade i mm) — och vägarnas 1 500 namn, Map-
//              vridna längs vägen, fet stil för de större
//   2 Gränser  300 fastighetsgränser (31 punkter, spetsiga skarvar,
//              streckade i px) och 100 000 DXF-streck (två punkter)
//   3 Punkter  400 höjdkurvor (41 punkter), 2 000 markörer och 8 500
//              ortnamn (kursiva för vatten)
//
// ≈ 1,05 miljoner vertex statiskt innehåll (≈ 172 000 linjesegment — en
// skarv kostar fyra vertex, så 250 000 segment i polylinjer hade varit två
// miljoner) och 10 000 etiketter (≈ 74 000 glyfer, med gloria) som
// KOLLIDERAR. Allt är deterministiskt (eget frö, ingen std-fördelning).
//
// GRINDEN — DETERMINISTISK, oberoende av tiden (ctest går under last):
//   U  en kameraändring laddar upp HÖGST en byte per etikett (synlighetens
//      texlar — aldrig geometri), skapar INGEN buffert och BRYTER ALDRIG
//      renderingen (`RenderStats::uploadBreaks` 0 — på Metal
//      går synlighetsbilden, ritad i förra framen, genom uppladdningspasset
//      före framen), i varje frame av sex serier (panorering, zoom,
//      vridning, tilt, panorering under tilt, översikten där allt syns)
//   R  ingenting byggs om av en kameraändring: textbyggena och chunkarna
//      på GPU:n är desamma före och efter alla serier
//   P  placeringen körs EN gång per kameraändring och INTE när samma kamera
//      ritas om — och den ändrar synligheten i några frames, så att U inte
//      är tom
//   D  rit-anropen per frame ≤ det INNEHÅLLET kräver + 8: för varje grupp
//      (lager, körningsnyckel, ruta) ⌈vertex / (65 536 − formens största)⌉
//      för linjer och ytor, ⌈glyfvertex / 65 532⌉ × atlassidor för texten
//      — räknat UR SCENENS EGNA FORMER med byggarnas dokumenterade kontrakt
//      (fyra vertex per segment, skarv och rund ände; fyra per ytkant; fyra
//      per glyf och pass), aldrig ur ritytans chunkar
//
// TIDERNA — MÄTTA OCH UTSKRIVNA, INTE GRINDADE (datorerna skiljer sig, och
// en ctest under last kan ge exit 137):
//   * uppbyggnaden: `addShape` för allt, första ritningen (uppladdning,
//     textbygge, SDF-rasterisering, placering) och när GPU:n är klar med
//     den, en andra ritning och en tom frame (det fasta i varje frame)
//   * `renderFrame` per kameraändring — median, 90:e percentilen, största —
//     per serie, med en VILANDE GPU: förra framens arbete väntas in först
//     (`captureFrame` läser ytan och väntar på den), så tiden är UI-trådens
//     egen och inte en Present som väntar på en GPU som ligger efter
//   * GENOMSTRÖMNINGEN: 30 kameraändringar i följd och sedan en läsning av
//     framen, som väntar in GPU:n (läsningens egen tid dras av) — ms per
//     frame med GPU:ns arbete, alltså fps
//   * placeringens kostnad: en mikropanorering (placeringen körs om, inget
//     annat ändras) mot samma kamera omritad (placeringen återanvänds)
//   * GPU-adaptern (Windows: DXGI:s första adapter, den D3D11CreateDevice
//     med nullptr tar — dokumenterat; frågan ställs till DXGI, inte till
//     backenden). Linux skriver ingen: GL-rendereraren är glxinfo:s.
//
// INTE MÄTT: pixlarna (scene_pixel_gate del F–H), Metal, en SYNLIG
// presentation med vsync och kompositör (fönstret är dolt), första ritningen
// av ett NYTT fönster med varm atlas, gesternas egen kostnad (canvas_gate),
// `onPaint`-målaren, bilder och plattor i lagren, Strict-ordningen (en
// kontur på en yta växlar nyckel två gånger per form i Strict,
// så varje del blir ett eget anrop; kartlagret är ByKind), och tiderna som
// krav.
//
// SABOTAGEN den fäller (prövade i en privat kopia): synken som laddar upp
// varje ritning (U, R), ett rit-anrop per
// form (D), en chunk per form (D), placeringen som laddar upp hela bilden (U:
// 10 240 > 10 004 byte) och texten byggd om vid zoom (U, R).
// Och Metals Pass skickad till Break — U fäller
// på Metal (synlighetsbilden bryter passet i varje ändrad frame); på D3D11
// och GL är brotten alltid 0.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include <glaze/glaze.h>

#include "canvas/resources.h"   // intern — fönstrets ritytsresurser (räknarna)

#ifdef _WIN32
#    include <dxgi.h>
#endif

namespace {

int  g_fel     = 0;
int  g_fall    = 0;
bool g_verbose = false;

void check(bool ok, const std::string& vad, const std::string& fick = {})
{
    ++g_fall;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                        fick.c_str());
        return;
    }
    std::printf("  FEL  %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                fick.c_str());
    ++g_fel;
}

#ifdef NDEBUG
const char* const kBygge = "Release";
#else
const char* const kBygge = "Debug";
#endif

using Klocka = std::chrono::steady_clock;

double msSedan(Klocka::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Klocka::now() - t0).count();
}

// ------------------------------------------------------------- slumpen
//
// EGET frö och egen avbildning: std:s fördelningar är implementations-
// definierade, och scenen ska vara densamma med libstdc++ och libc++.
struct Slump {
    std::uint64_t s;
    std::uint64_t nästa()
    {
        // splitmix64
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double enhet() { return static_cast<double>(nästa() >> 11) * (1.0 / 9007199254740992.0); }
    double mellan(double a, double b) { return a + (b - a) * enhet(); }
    int    upp(int n) { return static_cast<int>(nästa() % static_cast<std::uint64_t>(n)); }
};

// ------------------------------------------------------------- scenen
//
// Världen: 2 × 2 RUTOR om 4096 m, lagda på ritytans rutnät (kChunkCell) så
// att varje form ligger HELT i en ruta — rutan är en del av gruppens nyckel
// i de förväntade värdena nedan.
constexpr double kRuta   = 4096.0;
constexpr double kX0     = 164.0 * kRuta;    // 671 744
constexpr double kY0     = 1606.0 * kRuta;   // 6 578 176
constexpr int    kRutorX = 2, kRutorY = 2;
constexpr double kKant   = 200.0;            // formerna håller sig så långt in
constexpr double kMittX  = kX0 + kRutorX * kRuta / 2.0;
constexpr double kMittY  = kY0 + kRutorY * kRuta / 2.0;

constexpr std::size_t kMaxChunk = 65536;   // gfx::kMaxChunkVertices (kontrakt)

// FÖRVÄNTAT: det innehållet KRÄVER, räknat ur scenens egna former.
//   linjer/ytor  grupp (lager, nyckel, ruta) → vertex; nyckeln är den
//                dokumenterade (shape.h: enfärgade delar delar anrop när
//                enheten, strecken och — för spetsiga skarvar — spetsgränsen
//                är desamma)
//   text         grupp (lager, familj, ruta) → glyfvertex (en atlas per
//                familj bär alla stilarna)
struct Facit {
    std::map<std::tuple<int, int, int>, std::uint64_t> linjer;
    std::map<std::tuple<int, int, int>, std::uint64_t> glyfer;
    std::map<int, int>                                 glyfFont;   // nyckel → familjen (−1 markörer)
    std::uint64_t maxFormV  = 0;
    std::uint64_t vertex    = 0;   // linjer + ytor
    std::uint64_t segment   = 0;
    std::uint64_t glyfV     = 0;
    std::size_t   etiketter = 0;   // texlar i synlighetsbilden
    std::size_t   glyfAntal = 0;
    std::size_t   former    = 0;
};

// Nycklarnas id i de förväntade värdena — EN per dokumenterad körningsnyckel.
enum Nyckel : int {
    kYta = 0,          // Fill, enfärgad
    kLinjePx,          // Line, px, heldragen, ej spetsig
    kLinjeMm,          // Line, mm, heldragen, ej spetsig
    kStreckMm,         // Line, px-bredd, streck {3, 2} mm
    kGränsStreck,      // Line, px, streck {8, 4} px, spetsig 4
};
// Glyfernas nycklar: (enhet, familj) — EN atlas
// per familj bär alla stilarna, så vanlig, fet och kursiv text i mm delar körning.
// Markörerna är font 0.
enum GlyfNyckel : int { kMarkörPx = 100, kTextMm };

int rutaAv(double x, double y)
{
    const int rx = static_cast<int>(std::floor((x - kX0) / kRuta));
    const int ry = static_cast<int>(std::floor((y - kY0) / kRuta));
    return ry * kRutorX + rx;
}

// Byggarnas kontrakt (gfx/geom2d.h): fyra vertex per segment, skarv och
// rund ände; en sluten linje har lika många skarvar som segment.
std::uint64_t linjeV(std::size_t n, bool sluten, bool rundaÄndar)
{
    if (sluten)
        return 8u * n;
    const std::uint64_t seg = n - 1, skarv = n - 2;
    return 4u * (seg + skarv + (rundaÄndar ? 2u : 0u));
}

// Antalet glyfer med ett fält — alla tecken utom mellanslaget (sdf.cpp:
// mellanslaget är bara ett steg). UTF-8: varje byte som inte är en
// fortsättningsbyte börjar ett tecken.
std::size_t glyfer(const std::string& s)
{
    std::size_t n = 0;
    for (const char ch : s) {
        const auto b = static_cast<unsigned char>(ch);
        if ((b & 0xC0) != 0x80 && b != ' ')
            ++n;
    }
    return n;
}

const char* const kVägFörled[] = {
    "Stor",   "Kungs",  "Drottning", "Kyrk",  "Skol",   "Mölle",  "Björk",
    "Ek",     "Lind",   "Rosen",     "Sjö",   "Berg",   "Ny",     "Gamla ",
    "Norra ", "Södra ", "Väster",    "Öster", "Hamn",   "Torg"};
const char* const kVägEfterled[] = {"gatan", "vägen", "stigen", "gränd",
                                    "backen", "allén"};
const char* const kOrtFörled[] = {"Ek",    "Lind", "Björk", "Hag",  "Sol",
                                  "Berg",  "Norr", "Söder", "Väst", "Öst",
                                  "Kvarn", "Åker", "Mos",   "Sand", "Ås",
                                  "Holm"};
const char* const kOrtEfterled[] = {"by",  "lund", "hem",  "dal", "vik",
                                    "ö",   "sta",  "torp", "näs", "åsen"};
const char* const kVatten[] = {"sjön", "ån", "viken", "tjärn", "fjärden"};

template <std::size_t N>
const char* välj(Slump& r, const char* const (&lista)[N])
{
    return lista[r.upp(static_cast<int>(N))];
}

// En slumpvandring med n punkter från (x, y), steg om `steg` m, som studsar
// mot rutans kanter — punkterna är alltid olika (stegen är aldrig noll).
std::vector<glaze::Point> vandring(Slump& r, double x, double y, int n,
                                   double steg, double x0, double y0)
{
    std::vector<glaze::Point> p;
    p.reserve(static_cast<std::size_t>(n));
    double riktning = r.mellan(0.0, 6.283185307179586);
    for (int i = 0; i < n; ++i) {
        p.push_back({x, y});
        riktning += r.mellan(-0.5, 0.5);
        double nx = x + steg * std::cos(riktning);
        double ny = y + steg * std::sin(riktning);
        if (nx < x0 + kKant || nx > x0 + kRuta - kKant) {
            riktning = 3.141592653589793 - riktning;
            nx       = x + steg * std::cos(riktning);
        }
        if (ny < y0 + kKant || ny > y0 + kRuta - kKant) {
            riktning = -riktning;
            ny       = y + steg * std::sin(riktning);
        }
        x = nx;
        y = ny;
    }
    return p;
}

// Fönstret utan chrome: framen är ritytan och inget annat, så det fasta i
// varje frame (ytan, compositen, presentationen) är litet och likadant på
// alla plattformar.
class PerfFönster : public glaze::AppWindow {
public:
    PerfFönster() : glaze::AppWindow("canvas_perf", 1280, 800, gfx::Chrome::None)
    {
        setClientSize(1280, 800);
        karta.name = "karta";
        karta.dock = glaze::Dock::Fill;
        karta.setBorder(false);
        root().adopt(karta);
    }
    glaze::Canvas karta;
};

// Bygger scenen i ritytan och fyller de förväntade värdena. Svarar med tiden för alla
// `addShape` (ms).
double byggScen(glaze::Canvas& k, Facit& f)
{
    Slump r{20261010};
    k.setYUp(true);
    k.setLayers({"Ytor", "Vägar", "Gränser", "Punkter"});
    for (int l = 0; l < 4; ++l)
        k.setLayerDrawOrder(l, glaze::DrawOrder::ByKind);

    const auto linje = [&](int lager, int nyckel, int ruta, std::uint64_t v,
                           std::uint64_t seg) {
        f.linjer[{lager, nyckel, ruta}] += v;
        f.vertex += v;
        f.segment += seg;
        f.maxFormV = std::max(f.maxFormV, v);
    };
    const auto etikett = [&](int lager, int nyckel, int ruta,
                             const std::string& text, bool gloria) {
        const std::size_t g = glyfer(text);
        f.glyfer[{lager, nyckel, ruta}] += 4u * g * (gloria ? 2u : 1u);
        f.glyfV += 4u * g * (gloria ? 2u : 1u);
        f.glyfAntal += g;
        ++f.etiketter;
    };

    const gfx::Color vit{255, 255, 255, 255};
    const auto       t0 = Klocka::now();
    for (int ry = 0; ry < kRutorY; ++ry) {
        for (int rx = 0; rx < kRutorX; ++rx) {
            const double x0 = kX0 + rx * kRuta, y0 = kY0 + ry * kRuta;
            const int    ruta = ry * kRutorX + rx;
            const auto   pos  = [&](double kant) {
                return glaze::Point{r.mellan(x0 + kant, x0 + kRuta - kant),
                                    r.mellan(y0 + kant, y0 + kRuta - kant)};
            };

            // ---- 0 YTOR: hus (L-form, sex punkter) med kontur i px.
            for (int i = 0; i < 750; ++i) {
                const glaze::Point c = pos(kKant);
                const double       w = r.mellan(10, 40), h = r.mellan(10, 40);
                const double       a = w * r.mellan(0.3, 0.7), b = h * r.mellan(0.3, 0.7);
                std::vector<glaze::Point> ring{
                    {c.x, c.y},         {c.x + w, c.y},     {c.x + w, c.y + b},
                    {c.x + a, c.y + b}, {c.x + a, c.y + h}, {c.x, c.y + h}};
                const auto grå = static_cast<std::uint8_t>(150 + r.upp(80));
                k.addShape(0, glaze::Shape::polygon(
                                  std::move(ring), gfx::Color{grå, grå, 200, 255},
                                  glaze::Stroke{.paint = gfx::Color{80, 80, 90, 255},
                                                .width = 1.0f}));
                linje(0, kYta, ruta, 4u * 6u, 0);
                linje(0, kLinjePx, ruta, linjeV(6, true, false), 6);
            }
            // …och sjöar med ett hål (en ö), kontur i mm.
            for (int i = 0; i < 10; ++i) {
                const glaze::Point        c = pos(kKant + 200.0);
                const double              rad = r.mellan(60, 150);
                std::vector<glaze::Point> yttre, hål;
                for (int j = 0; j < 48; ++j) {
                    const double v = j * 6.283185307179586 / 48;
                    const double q = rad * r.mellan(0.8, 1.0);
                    yttre.push_back({c.x + q * std::cos(v), c.y + q * std::sin(v)});
                }
                for (int j = 0; j < 12; ++j) {
                    const double v = j * 6.283185307179586 / 12;
                    hål.push_back({c.x + 0.3 * rad * std::cos(v),
                                   c.y + 0.3 * rad * std::sin(v)});
                }
                k.addShape(0, glaze::Shape::polygon(
                                  std::vector<std::vector<glaze::Point>>{yttre, hål},
                                  gfx::Color{120, 170, 230, 255},
                                  glaze::Stroke{.paint = gfx::Color{40, 90, 160, 255},
                                                .width = glaze::mm(0.3f)}));
                linje(0, kYta, ruta, 4u * (48u + 12u), 0);
                linje(0, kLinjeMm, ruta,
                      linjeV(48, true, false) + linjeV(12, true, false), 60);
            }

            // ---- 1 VÄGAR: 16 punkter, de större med runda ändar och fet
            // etikett — Map-vriden längs vägens mittsegment.
            for (int i = 0; i < 375; ++i) {
                const bool         stor = r.upp(4) == 0;
                const glaze::Point s    = pos(kKant);
                auto p = vandring(r, s.x, s.y, 16, r.mellan(15, 40), x0, y0);
                const glaze::Point a = p[7], b = p[8];
                const double vinkel = std::atan2(b.y - a.y, b.x - a.x) * 180.0 /
                                      3.141592653589793;
                const float  ritad =
                    static_cast<float>(vinkel > 90.0 ? vinkel - 180.0
                                       : vinkel < -90.0 ? vinkel + 180.0
                                                        : vinkel);
                k.addShape(1, glaze::Shape::polyline(
                                  std::move(p),
                                  glaze::Stroke{.paint = stor ? gfx::Color{230, 140, 40, 255}
                                                              : gfx::Color{250, 250, 250, 255},
                                                .width = glaze::mm(stor ? 1.0f : 0.5f),
                                                .cap = stor ? glaze::LineCap::Round
                                                            : glaze::LineCap::Butt}));
                linje(1, kLinjeMm, ruta, linjeV(16, false, stor), 15);
                std::string namn = std::string(välj(r, kVägFörled)) + välj(r, kVägEfterled);
                if (r.upp(3) == 0)
                    namn += " " + std::to_string(1 + r.upp(40));
                const glaze::Point m{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
                k.addShape(1, glaze::Shape::text(
                                  m, namn,
                                  glaze::TextStyle{.size     = glaze::mm(2.0f),
                                                   .bold     = stor,
                                                   .color    = gfx::Color{40, 40, 40, 255},
                                                   .halo     = vit,
                                                   .angle    = ritad,
                                                   .rotation = glaze::Alignment::Map,
                                                   .priority = stor ? 3 : 2,
                                                   .collisionPadding = 2.0f}));
                etikett(1, kTextMm, ruta, namn, true);
            }
            // …och stigar, streckade i mm.
            for (int i = 0; i < 100; ++i) {
                const glaze::Point s = pos(kKant);
                k.addShape(1, glaze::Shape::polyline(
                                  vandring(r, s.x, s.y, 12, r.mellan(8, 20), x0, y0),
                                  glaze::Stroke{.paint    = gfx::Color{120, 90, 60, 255},
                                                .width    = 1.5f,
                                                .dash     = {3.0f, 2.0f},
                                                .dashUnit = glaze::Unit::Mm}));
                linje(1, kStreckMm, ruta, linjeV(12, false, false), 11);
            }

            // ---- 2 GRÄNSER: spetsiga skarvar och streck i px.
            for (int i = 0; i < 75; ++i) {
                const glaze::Point s = pos(kKant);
                k.addShape(2, glaze::Shape::polyline(
                                  vandring(r, s.x, s.y, 31, r.mellan(20, 50), x0, y0),
                                  glaze::Stroke{.paint = gfx::Color{200, 40, 40, 255},
                                                .width = 2.0f,
                                                .join  = glaze::LineJoin::Miter,
                                                .dash  = {8.0f, 4.0f}}));
                linje(2, kGränsStreck, ruta, linjeV(31, false, false), 30);
            }
            // …och DXF-streck: två punkter var, 1 px.
            for (int i = 0; i < 25000; ++i) {
                const glaze::Point a = pos(kKant);
                const double       v = r.mellan(0.0, 6.283185307179586);
                const double       l = r.mellan(2.0, 20.0);
                k.addShape(2, glaze::Shape::line(
                                  a, {a.x + l * std::cos(v), a.y + l * std::sin(v)},
                                  glaze::Stroke{.paint = gfx::Color{60, 60, 60, 255},
                                                .width = 1.0f}));
                linje(2, kLinjePx, ruta, linjeV(2, false, false), 1);
            }

            // ---- 3 PUNKTER: höjdkurvor, markörer och ortnamn.
            for (int i = 0; i < 100; ++i) {
                const glaze::Point s = pos(kKant);
                k.addShape(3, glaze::Shape::polyline(
                                  vandring(r, s.x, s.y, 41, r.mellan(10, 30), x0, y0),
                                  glaze::Stroke{.paint = gfx::Color{170, 120, 70, 255},
                                                .width = 1.0f}));
                linje(3, kLinjePx, ruta, linjeV(41, false, false), 40);
            }
            std::vector<glaze::Point> punkter;
            for (int i = 0; i < 500; ++i)
                punkter.push_back(pos(kKant));
            k.addShape(3, glaze::Shape::points(
                              std::move(punkter),
                              glaze::MarkerStyle{.size    = 5.0f,
                                                 .fill    = gfx::Color{30, 30, 30, 255},
                                                 .outline = vit}));
            // Markörerna kolliderar inte: ETT index för hela formen.
            f.glyfer[{3, kMarkörPx, ruta}] += 4u * 500u;
            f.glyfV += 4u * 500u;
            ++f.etiketter;
            for (int i = 0; i < 2125; ++i) {
                const bool vatten = r.upp(10) == 0;
                std::string namn =
                    vatten ? std::string(välj(r, kOrtFörled)) + välj(r, kVatten)
                           : std::string(välj(r, kOrtFörled)) + välj(r, kOrtEfterled);
                k.addShape(3, glaze::Shape::text(
                                  pos(kKant), namn,
                                  glaze::TextStyle{.size     = glaze::mm(2.4f),
                                                   .italic   = vatten,
                                                   .color    = vatten ? gfx::Color{30, 70, 150, 255}
                                                                      : gfx::Color{20, 20, 20, 255},
                                                   .halo     = vit,
                                                   .priority = r.upp(2),
                                                   .collisionPadding = 2.0f}));
                etikett(3, kTextMm, ruta, namn, true);
            }
        }
    }
    const double ms = msSedan(t0);
    f.glyfFont = {{kMarkörPx, -1},
                  {kTextMm, static_cast<int>(gfx::FontFamily::UI)}};
    for (int l = 0; l < k.layerCount(); ++l)
        f.former += static_cast<std::size_t>(k.shapeCount(l));
    return ms;
}

// Gränsen för rit-anropen (D): det innehållet kräver + 8.
std::uint64_t ritgräns(const Facit& f, glaze::CanvasResources& res)
{
    std::uint64_t n = 0;
    for (const auto& [nyckel, v] : f.linjer)
        n += (v + (kMaxChunk - f.maxFormV) - 1) / (kMaxChunk - f.maxFormV);
    for (const auto& [nyckel, v] : f.glyfer) {
        const int font = f.glyfFont.at(std::get<1>(nyckel));
        const std::uint64_t sidor =
            font < 0 ? 1u
                     : std::max<std::uint64_t>(
                           1u, res.atlas(static_cast<gfx::FontFamily>(font))
                                   ->pageCount());
        n += (v + (kMaxChunk - 4) - 1) / (kMaxChunk - 4) * sidor;
    }
    return n + 8;
}

// ------------------------------------------------------------- mätningen

struct Tider {
    std::vector<double> ms;

    double kvantil(double q) const
    {
        if (ms.empty())
            return 0.0;
        std::vector<double> s = ms;
        std::sort(s.begin(), s.end());
        const auto i = static_cast<std::size_t>(
            std::lround(q * static_cast<double>(s.size() - 1)));
        return s[i];
    }
    double median() const { return kvantil(0.5); }
    double max() const { return ms.empty() ? 0.0 : *std::max_element(ms.begin(), ms.end()); }
};

// Kamerans punkter: korsningen mellan de fyra rutorna (mitten — en kors av
// tomma kantremsor går genom den) och en punkt INNE i ruta 0, som zoomen
// går mot.
constexpr double kFokusX = kX0 + 2900.0;
constexpr double kFokusY = kY0 + 2700.0;

glaze::Camera kamera(double x, double y, double zoom, float bäring = 0.0f,
                     float tilt = 0.0f)
{
    return glaze::Camera{{x, y}, zoom, bäring, tilt};
}

// En serie kameraändringar: varje steg sätter HELA kameran — en annan än
// förra framens, så varje frame är en kameraändring (P mäter det).
struct Serie {
    const char*                     namn;
    std::function<glaze::Camera(int)> kamera;
    int                             antal = 60;
};

const Serie kSerier[] = {
    {"panorering",
     [](int i) {
         return kamera(kMittX - 1500.0 + 50.0 * i, kMittY - 900.0 + 30.0 * i, 0.5);
     }},
    {"zoom",
     [](int i) { return kamera(kFokusX, kFokusY, 0.1 * std::pow(30.0, i / 59.0)); }},
    {"vridning",
     [](int i) { return kamera(kMittX, kMittY, 0.5, static_cast<float>(6 * i)); }},
    {"tilt",
     [](int i) { return kamera(kMittX, kMittY, 0.5, 0.0f, static_cast<float>(i)); }},
    {"panorering under tilt",
     [](int i) {
         return kamera(kMittX - 1500.0 + 50.0 * i, kMittY - 900.0 + 30.0 * i, 0.5,
                       30.0f, 50.0f);
     }},
    {"översikten",
     [](int i) { return kamera(kMittX + 3.0 * i, kMittY, 0.095); }, 30},
};

struct Bänk {
    PerfFönster             f;
    Facit                   facit;
    glaze::CanvasResources* res   = nullptr;
    std::uint64_t           gräns = 0;
    // Grindens tal över alla kameraändringar — och placeringarna när samma
    // kamera ritas om (ska vara noll).
    std::uint64_t maxUpp = 0, sumUpp = 0, maxAnrop = 0, buffertar = 0;
    std::uint64_t brott = 0;   // uppladdningar som bröt render-passet (U)
    std::uint64_t placeringar = 0, ändrade = 0, frames = 0, omplaceringar = 0;
    std::string   sämsta;

    gfx::Renderer& rr() { return f.window().renderer(); }
    glaze::Canvas& karta() { return f.karta; }

    // En ritning; svarar med tiden (ms).
    double rita()
    {
        const auto t0 = Klocka::now();
        f.window().renderFrame();
        return msSedan(t0);
    }

    // En ritning när GPU:n VILAR: förra framens arbete väntas in först, så
    // tiden är UI-trådens egen — ingen väntan på en GPU som ligger efter.
    double ritaCpu()
    {
        vänta();
        return rita();
    }

    // EN KAMERAÄNDRING: kameran sätts, framen ritas (CPU-tiden), och
    // grindens tal för just den framen läses.
    double kameraändring(const glaze::Camera& kam, const char* serie, int i)
    {
        karta().setCamera(kam);
        vänta();
        const std::uint64_t plac = res->labelPlacements();
        rr().resetStats();
        const double            ms = rita();
        const gfx::RenderStats& s  = rr().stats();
        ++frames;
        maxUpp = std::max(maxUpp, s.bytesUploaded);
        sumUpp += s.bytesUploaded;
        ändrade += s.bytesUploaded > 0 ? 1u : 0u;
        buffertar += s.buffersCreated;
        brott += s.uploadBreaks;
        placeringar += res->labelPlacements() - plac;
        if (s.drawCalls > maxAnrop) {
            maxAnrop = s.drawCalls;
            sämsta   = std::string(serie) + ", frame " + std::to_string(i);
        }
        if (g_verbose)
            std::printf("       %s %2d: %7.2f ms, %5llu byte, %3u anrop, %8llu vertex, "
                        "%u brott\n",
                        serie, i, ms, static_cast<unsigned long long>(s.bytesUploaded),
                        s.drawCalls, static_cast<unsigned long long>(s.vertices),
                        s.uploadBreaks);
        return ms;
    }

    // GPU:ns arbete väntas in genom att framen LÄSES (`captureFrame` kopierar
    // ytan och väntar på den). Svarar med läsningens tid.
    double vänta()
    {
        std::vector<std::uint8_t> px;
        int                       w = 0, h = 0;
        const auto                t0 = Klocka::now();
        f.window().captureFrame(px, w, h);
        return msSedan(t0);
    }
};

#ifdef _WIN32
// D3D11CreateDevice(nullptr, …) tar den FÖRSTA adaptern DXGI räknar upp
// (dokumenterat) — det är den backenden fick. Frågan ställs till DXGI i
// samma process, inte till backenden.
std::string adapter()
{
    IDXGIFactory1* fabrik = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                  reinterpret_cast<void**>(&fabrik))))
        return "okänd (CreateDXGIFactory1 misslyckades)";
    std::string    ut;
    IDXGIAdapter1* a = nullptr;
    for (UINT i = 0; fabrik->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d{};
        a->GetDesc1(&d);
        char namn[256] = {};
        WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, namn, sizeof namn,
                            nullptr, nullptr);
        if (!ut.empty())
            ut += " · ";
        ut += (i == 0 ? "[vald] " : "") + std::string(namn) +
              ((d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? " (mjukvara)" : "");
        a->Release();
    }
    fabrik->Release();
    return ut;
}
#endif

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== canvas_perf — G7, ritytans prestanda (%s) ===\n", kBygge);
#ifdef _WIN32
    std::printf("  gpu  %s\n", adapter().c_str());
#endif

    Bänk b;
    b.f.setOffscreen(true);
    b.f.show();
    if (!b.f.isOpen()) {
        std::printf("  FEL  fönstret öppnades inte — ingen headless-yta?\n");
        std::printf("=== canvas_perf: FALLERAR ===\n");
        return 1;   // ALDRIG skip — som clip_gate
    }
    b.f.window().renderFrame();
    {
        const glaze::Ctx c   = b.karta().behaviorCtx();
        b.res                = c.canvas;
        const gfx::Rectf box = b.karta().resolve(c);
        std::printf("  yta  %.0f × %.0f fysiska px (skala %.2f, %.2f px/mm)\n",
                    box.w, box.h, c.scale, c.pxPerMm);
    }
    if (!b.res) {
        std::printf("  FEL  fönstret har inga ritytsresurser\n");
        std::printf("=== canvas_perf: FALLERAR ===\n");
        return 1;
    }
    glaze::Canvas& k = b.karta();

    // ---------------------------------------------------- DET FASTA
    // En frame utan innehåll: det varje frame kostar utöver scenen.
    b.rr().resetStats();
    k.invalidate();
    const double tTom = b.ritaCpu();
    const std::uint32_t tomAnrop = b.rr().stats().drawCalls;

    // ---------------------------------------------------- UPPBYGGNADEN
    const double tAdd = byggScen(k, b.facit);
    k.setCamera(kamera(kMittX, kMittY, 0.5));
    b.rr().resetStats();
    const double           tFörsta = b.ritaCpu();
    const gfx::RenderStats första  = b.rr().stats();
    const double           tKlar   = tFörsta + b.vänta();
    k.invalidate();
    const double tAndra = b.ritaCpu();
    std::printf("  scen %zu former, %llu vertex (%llu segment), %zu etiketter "
                "(%zu glyfer, %llu glyfvertex)\n",
                b.facit.former, static_cast<unsigned long long>(b.facit.vertex),
                static_cast<unsigned long long>(b.facit.segment), b.facit.etiketter,
                b.facit.glyfAntal, static_cast<unsigned long long>(b.facit.glyfV));
    std::printf("  tid  uppbyggnad: addShape %.1f ms · första ritningen %.1f ms "
                "(%.1f MB upp, %u buffertar; GPU:n klar efter %.1f ms) · andra "
                "%.2f ms · tom frame %.2f ms (%u anrop)\n",
                tAdd, tFörsta, första.bytesUploaded / 1.0e6, första.buffersCreated,
                tKlar, tAndra, tTom, tomAnrop);

    b.gräns = ritgräns(b.facit, *b.res);
    const std::size_t   chunkar    = b.res->chunkCount();
    const std::uint64_t textbyggen = b.res->glyphBuilds();
    std::printf("  gpu  %zu chunkar resident, %llu textbyggen, gränsen för "
                "rit-anrop %llu\n",
                chunkar, static_cast<unsigned long long>(textbyggen),
                static_cast<unsigned long long>(b.gräns));

    // ---------------------------------------------------- SERIERNA
    Tider alla;
    for (const Serie& s : kSerier) {
        Tider t;
        for (int i = 0; i < s.antal; ++i)
            t.ms.push_back(b.kameraändring(s.kamera(i), s.namn, i));
        alla.ms.insert(alla.ms.end(), t.ms.begin(), t.ms.end());
        std::printf("  tid  %-22s median %6.2f ms · p90 %6.2f · max %6.2f (%d frames)\n",
                    s.namn, t.median(), t.kvantil(0.9), t.max(), s.antal);
    }
    std::printf("  tid  %-22s median %6.2f ms · p90 %6.2f · max %6.2f\n",
                "alla kameraändringar", alla.median(), alla.kvantil(0.9), alla.max());
    const std::size_t   chunkarEfter    = b.res->chunkCount();
    const std::uint64_t textbyggenEfter = b.res->glyphBuilds();
    const std::uint64_t placeringar     = b.placeringar;
    const std::uint64_t frames          = b.frames;

    // ---------------------------------------------------- GENOMSTRÖMNINGEN
    // 30 kameraändringar i följd och sedan en läsning, som väntar in GPU:n;
    // läsningens egen tid (mätt på en vilande GPU) dras av.
    const auto genomström = [&b](const char* namn, auto kam) {
        b.karta().setCamera(kam(-1));
        b.rita();
        b.vänta();
        const double läsning = b.vänta();
        const auto   t0      = Klocka::now();
        double       cpu     = 0.0;
        for (int i = 0; i < 30; ++i) {
            b.karta().setCamera(kam(i));
            cpu += b.rita();
        }
        b.vänta();
        const double alltMs = msSedan(t0) - läsning;
        std::printf("  tid  genomströmning %-14s %6.2f ms per frame med GPU:n "
                    "(CPU %.2f) — %.0f fps\n",
                    namn, alltMs / 30.0, cpu / 30.0, 30000.0 / alltMs);
    };
    genomström("panorering", [](int i) {
        return kamera(kMittX - 1500.0 + 50.0 * i, kMittY - 900.0 + 30.0 * i, 0.5);
    });
    genomström("tilt", [](int i) {
        return kamera(kMittX - 1500.0 + 50.0 * i, kMittY - 900.0 + 30.0 * i, 0.5,
                      30.0f, 50.0f);
    });
    genomström("översikten", [](int i) {
        return kamera(kMittX + 3.0 * i, kMittY, 0.095);
    });

    // ---------------------------------------------------- PLACERINGEN
    // Samma kamera omritad (placeringen återanvänds) mot en mikropanorering
    // (den körs om; en panorering utan tilt flyttar bara lådorna, så
    // synligheten är densamma och inget laddas upp).
    const auto placering = [&b](const char* namn, double zoom) {
        Tider samma, mikro;
        b.karta().setCamera(kamera(kMittX, kMittY, zoom));
        b.rita();
        for (int i = 0; i < 30; ++i) {
            b.karta().invalidate();
            const std::uint64_t plac = b.res->labelPlacements();
            samma.ms.push_back(b.ritaCpu());
            b.omplaceringar += b.res->labelPlacements() - plac;   // P
            b.karta().setCamera(kamera(kMittX + 1.0e-3 * (i + 1), kMittY, zoom));
            mikro.ms.push_back(b.ritaCpu());
        }
        std::printf("  tid  placeringen, %-11s %6.2f ms per kameraändring "
                    "(%.2f mot %.2f)\n",
                    namn, mikro.median() - samma.median(), mikro.median(),
                    samma.median());
    };
    placering("översikten", 0.095);
    placering("zoom 0,5", 0.5);

    // ---------------------------------------------------- GRINDEN
    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "största %llu byte per frame (%zu etiketter), %llu totalt, "
                  "%llu buffertar, %llu brott, %llu frames",
                  static_cast<unsigned long long>(b.maxUpp), b.facit.etiketter,
                  static_cast<unsigned long long>(b.sumUpp),
                  static_cast<unsigned long long>(b.buffertar),
                  static_cast<unsigned long long>(b.brott),
                  static_cast<unsigned long long>(frames));
    check(b.maxUpp <= b.facit.etiketter && b.buffertar == 0 && b.brott == 0,
          "U en kameraändring laddar upp högst en byte per etikett, skapar "
          "ingen buffert och BRYTER ALDRIG renderingen",
          buf);
    check(chunkarEfter == chunkar && textbyggenEfter == textbyggen,
          "R ingenting byggs om av kameraändringarna (chunkar och textbyggen)",
          std::to_string(chunkarEfter) + " chunkar, " +
              std::to_string(textbyggenEfter) + " textbyggen");
    check(placeringar == frames && b.omplaceringar == 0 && b.ändrade > 0,
          "P placeringen körs en gång per kameraändring, inte när samma kamera "
          "ritas om, och ändrar synligheten",
          std::to_string(placeringar) + " placeringar på " +
              std::to_string(frames) + " ändringar, " +
              std::to_string(b.omplaceringar) + " vid samma kamera, " +
              std::to_string(b.ändrade) + " frames med uppladdning");
    std::snprintf(buf, sizeof buf, "flest %llu anrop (%s), gränsen %llu",
                  static_cast<unsigned long long>(b.maxAnrop), b.sämsta.c_str(),
                  static_cast<unsigned long long>(b.gräns));
    check(b.maxAnrop <= b.gräns, "D rit-anropen per frame ≤ det innehållet kräver + 8",
          buf);

    std::printf(g_fel ? "=== canvas_perf: %d FEL av %d ===\n"
                      : "=== canvas_perf: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    return g_fel ? 1 : 0;
}
