// terrain_perf — TERRÄNGKARTAN OCH FÖRSTAPERSONSVYN: en
// mätscen som BETER SIG SOM EN KARTAPP OCH EN 3D-APP — en tiltad karta
// med terräng och drapering, och en 3D-vy i ett eget fönster som
// kartan äger, i samma UI-tråd. Målet är minst 50 Hz (20 ms mellan bilderna)
// under rörelse och 150+ (6,7 ms) — också i Debug.
//
// Kör:  ./build/tests/terrain_perf [--synlig] [--hp] [--gpu ID] [--gpus]
//                                  [--skärm N] [--objekt N] [--mål Hz]
//                                  [--steg N] [-v]
//   --synlig    riktiga fönster med vsync (annars dolda, som i ctest)
//   --hp        gfx::setGpuPreference(HighPerformance) före första fönstret
//   --gpu ID    gfx::setGpuAdapter(ID) före första fönstret — ett id ur
//               --gpus; loggen säger vilket kort som ritar
//   --gpus      listar grafikkorten (gfx::gpuAdapters) och avslutar
//   --skärm N   båda fönstren på skärm N ur gfx::screens() (1 = den första)
//   --objekt N  ledningarna på kartan (förval 20)
//   --mål Hz    exit 1 när ett rörelsestegs p95-intervall är över 1000/Hz
//   --steg N    bara steg N (1–27): kartans start, förutsättningarna och steget
//   -v          processens minne i varje tick — stegets topp, och de 40
//               första skrivna (selens `sampla`; utan -v mäts det inte, så
//               att DXGI-frågan inte hamnar i varje bild)
// Exit: 0 = sessionen gick igenom · 1 = fönstret öppnades inte, ett steg blev
//       inte klart, strömningen kom aldrig ikapp, hovret träffade aldrig, det
//       orörda steget ritade — eller (`--mål`) en takt under målet
//
// STEGEN ÄR DYNAMISKA, som riktig användning: kameran rör sig — tilt,
// vridning, panorering, WASD i 3D — MEDAN plattor, brickor och rutor strömmar
// in och 3D-appens former ändras. Huvudmåttet är ALLT SAMTIDIGT, i appens
// båda lägen; stegen före det delar upp samma sak, en i taget, så att
// orsaken syns. "idag" bygger om ALLA objektlager vid en ändring,
// "rättad" bara det ändrade objektet eller markören.
//
// KARTAN (1280 × 800 logiska px innehåll, statusrad med sju fält):
//   terrängen   lager 0, Terrain: plattor om 128 × 128 celler (16 641 hörn,
//               32 768 trianglar med normaler) ur DEM:en, ~6 per axel i det
//               synliga, nivån ur markens synliga utsträckning med hysteres
//               och en cache om 128 plattor. Plattan byggs i en arbetstråd med
//               `preparePicking`; en PLATSHÅLLARE om 4 hörn sätts först med
//               `setShape`, och den riktiga meshen sedan PÅ SAMMA PLATS. Bara
//               med tilt (≥ 1°), som i en kartapp.
//   tiltregeln  kartappens regel: en tiltad karta (≥ 3°) står aldrig
//               längre ut än 1:1000 — tiltas den där GLIDER den in (`easeTo`
//               700 ms, upp till 30°), och ritarna får vybytena av en 50
//               ms-timer; en utzoomning stannar vid 1:1000
//   baskartan   ett Ground-lager i ByKind med `Shape::image` på fyra hörn per
//               bricka (PNG avkodade i arbetstrådarna med nätets fördröjning),
//               högst 300 brickor närmast mitten och en LRU om 2000 — HELA
//               lagret byggs om med `setShapes` per batch, högst var 200 ms
//   objekten    N ledningar om 30–125 punkter över 1,5–8 km, med fyra lager
//               vardera: stråkbandet i Strict (en polygon om 60–250 punkter,
//               40 m bred, med fyllning och kontur), objektets lager i Strict
//               (två gloriaplatser och linjen), ett After-lager (3D-appens
//               former: stolparna och deras etiketter) och ett Auto-lager med
//               3D-detaljer (stolparnas meshar, bara med terrängen)
//   vektorerna  5 lager om 300 former, vart och ett med sitt glorialager
//   hovret      `setShapes` på ALLA glorialager och glorian per objekt med
//               `setShape` (som en kartapps vektor- och objektritare)
//   per musrörelse  3 `castRay`, `hitTest(lokal, HitOptions{})` och `heightAt`
//   redigeringen  stråkbandet trianguleras av appen och läggs på marken med
//               24 000 `heightAt` per bild i `onPaint`
//   skalstocken `toWorld` + `heightAt` per bild i `onPaintOverlay`
//   3D-appen    3D-vyns pose (var 0,5 m eller 0,5°), modulens former (en
//               stolpe dras i profilvyn), profilfönstrets hover och ett objekt
//               som ändras: "idag" `setShapes` på ALLA objektlager, "rättad"
//               bara det ändrade objektets lager eller en markör i ett eget
//               litet lager
//   minnet      `setMemoryBudget(1 GiB)`
//
// 3D-VYN (eget fönster som kartan äger, 1280 × 800, 9 lager):
//   Himmel      World: `screenPass` med ett EGET program — gradient, sol och
//               moln ur fbm i fem oktaver (HLSL; GLSL och MSL följer samma mall)
//   Terräng     rutor om 40 m i 11 × 11 kring ögat (rutnät 10 på
//               zoom 19), 33 × 33 hörn med normaler och uv; ortofotot via
//               `setShape` med texturen när det kommer
//   Punktmoln, Mark  det INBYGGDA Points-programmet — samma sex vertex per
//               punkt som 3D-appens eget. AVVIKELSEN: klassens färg och
//               rampen ur skalären räknas inte i vertexsteget utan bärs av
//               punkten. Upp till 50 000 punkter per ruta i ledningsgatan.
//   Stolpar     platta, icke-indexerade meshar (84 byte per triangel,
//               `keepCpuCopy = false`) i tre detaljnivåer, bytta med
//               `setShape` när ögat flyttat 20 m eller vridits 10°
//   Glas        tomt (scenens stolpar har inget glas)
//   Linor       `Shape::tube`, tre faser per spann mellan laddade stolpar
//   Träd        instanser av en kon i sex detaljnivåer
//   Markerade punkter  Ground, `marker3`
//   — med `setMultisample(8)`, `setDepth(true)`, dimma och
//   `Camera3D::lookFrom` (near 0,1, far 500). RÖRELSEN: `onTick` medan
//   W/Q/E hålls (`isKeyHeld`) och musblicken med mittenknappen, och varje
//   tick `heightAt`, `setCamera`, statusraden med nio fält (texten sätts
//   bara när den ändrats, som i 3D-appen) och `invalidateOverlay` för HUD:en.
//   PER MUSRÖRELSE: `castRay(opaqueOnly)` och statusraden. Trådarnas svar
//   kommer i SKURAR via `app().post`; varje molnruta KOPIERAS i UI-tråden och
//   gör 25 synkrona `heightAt`, och molnet byggs sedan i en arbetstråd. Ingen
//   `setMemoryBudget`.
//
// SESSIONEN (samma följd varje gång; vilostegen väntar ut strömningen):
//    1 kartan: start                       15 3D: landning, kameran still
//    2 tiltglidning (50 ms-timer)           16 3D: musrörelse
//    3 tilt 5°–60° under terräng            17 3D-appen: modulens former, idag
//    4 vridning under tilt                  18 3D-appen: modulens former, rättad
//    5 panorering 2 km under tilt           19 3D-appen: profilhover, idag
//    6 landning, kartan still               20 3D-appen: profilhover, rättad
//    7 zoom in och ut under tilt            21 3D-appen: ett objekt, idag
//    8 hover över objekten                  22 3D-appen: ett objekt, rättad
//    9 redigering (24 000 heightAt/bild)    23 båda: körning, idag
//   10 baskartans batcher, kartan still     24 båda: körning, rättad
//   11 3D öppnas, rutorna landar            25 ALLT SAMTIDIGT, idag
//   12 3D: körning 100 km/h (ensam)         26 ALLT SAMTIDIGT, rättad
//   13 3D: vridning Q/E                     27 båda orörda 5 s (regel 1)
//   14 3D: vridning med mittenknappen
// ALLT SAMTIDIGT: kartan tiltas och vrids (Ctrl+mittendrag) och panoreras om
// vartannat, medan 3D-vyn kör 100 km/h och girar med Q/E och dess pose når
// kartan — och brickorna, plattorna och 3D-rutorna strömmar.
// "landning … still" och glidningen drivs av en timer: inget tickar, så
// stegets bilder är bara de som det landade (eller glidningen) orsakar.
//
// MÄTT PER STEG OCH FÖNSTER — ur PROFILERAREN (glaze/profiler.h, aldrig
// klampat): intervallens p50/p95/p99/max och antalet över 20 ms och 6,7 ms
// (bara bilder vars föregångare hade `continued`), input → present, fasernas
// p50 och räknarna per bild (draperingarna), pumpvarven och verktygens tider
// (`glaze.hitTest`, `glaze.castRay`, `glaze.heightAt`) och adaptern — och
// selens minne och energi. Selens frametid ur `onTick` (app_perf:s) skrivs i
// stegens rader men mäter inte takten här.
//
// GRINDEN (ctest, dolda fönster) — bara att sessionen GÅR: fönstret öppnas,
// varje steg blir klart inom sin tidsgräns, strömningen kommer ikapp i
// vilostegen, hovret träffar och det orörda steget ritar 0 bilder i båda
// fönstren (hård regel 1 — en kvarlämnad `setTicking(true)` eller en timer
// som invaliderar fäller det). Tiderna grindas inte: de beror på maskinen,
// och dolda fönster väntar inte på vsync. `--mål` är för hand, med `--synlig`.
//
// INTE MÄTT: vsync i ctest, riktigt nät och disk, kartappens profilfönster
// (dess hover är simulerad: en händelse per tick), modulernas egna ritare,
// 3D-appens punktprogram (se ovan) och stolparnas glas.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <list>
#include <map>
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

// ================================================================= VÄRLDEN
//
// SWEREF-liknande koordinater. Rutnätens origo (kartans plattor och brickor,
// 3D-vyns rutor) ligger långt sydväst om scenen, så att indexen är positiva.
constexpr double kX0      = 674000.0;
constexpr double kY0      = 6580000.0;
constexpr double kRutnätX = kX0 - 200000.0;
constexpr double kRutnätY = kY0 - 200000.0;

// MARKEN — DEM:en, samma för kartans plattor, 3D-vyns rutor och allt som står
// på marken: kullar om några hundra meter, 1–89 m över kartans bas, så att en
// platshållare på z = 0 ligger under den.
double markhöjd(double x, double y)
{
    const double u = x - kX0, v = y - kY0;
    return 45.0 + 30.0 * std::sin(u / 900.0) * std::cos(v / 700.0) +
           10.0 * std::sin(u / 230.0 + v / 310.0) + 4.0 * std::sin(u / 53.0) * std::cos(v / 61.0);
}
constexpr double kMarkTopp = 89.0;

// MARKENS MESH: (celler + 1)² hörn rad för rad från syd över kvadraten
// (x0, y0)–(x0 + sida, y0 + sida), höjden ur DEM:en, normalerna ur centrala
// differenser (ensidiga i kanten) och — för 3D-vyns ortofoto — uv med bildens
// rad 0 i norr. Plockningens BVH byggs här, i arbetstråden.
glaze::Mesh markMesh(double x0, double y0, double sida, int celler, bool uv)
{
    const int          rad  = celler + 1;
    const double       steg = sida / celler;
    std::vector<float> z(static_cast<std::size_t>(rad) * static_cast<std::size_t>(rad));
    const auto         vid = [&z, rad](int i, int j) {
        return z[static_cast<std::size_t>(j) * static_cast<std::size_t>(rad) +
                 static_cast<std::size_t>(i)];
    };
    for (int j = 0; j < rad; ++j)
        for (int i = 0; i < rad; ++i)
            z[static_cast<std::size_t>(j) * static_cast<std::size_t>(rad) +
              static_cast<std::size_t>(i)] =
                static_cast<float>(markhöjd(x0 + i * steg, y0 + j * steg));
    glaze::MeshDesc d;
    d.origin          = {x0, y0, 0.0};
    const float fsteg = static_cast<float>(steg);
    for (int j = 0; j < rad; ++j)
        for (int i = 0; i < rad; ++i) {
            d.positions.push_back({static_cast<float>(i) * fsteg, static_cast<float>(j) * fsteg,
                                   vid(i, j)});
            const int   i0 = std::max(i - 1, 0), i1 = std::min(i + 1, celler);
            const int   j0 = std::max(j - 1, 0), j1 = std::min(j + 1, celler);
            const float dx = (vid(i1, j) - vid(i0, j)) / (static_cast<float>(i1 - i0) * fsteg);
            const float dy = (vid(i, j1) - vid(i, j0)) / (static_cast<float>(j1 - j0) * fsteg);
            const float l  = std::sqrt(dx * dx + dy * dy + 1.0f);
            d.normals.push_back({-dx / l, -dy / l, 1.0f / l});
            if (uv)
                d.uvs.push_back({static_cast<float>(i) / static_cast<float>(celler),
                                 1.0f - static_cast<float>(j) / static_cast<float>(celler)});
        }
    const auto r = static_cast<std::uint32_t>(rad);
    d.indices.reserve(static_cast<std::size_t>(celler) * static_cast<std::size_t>(celler) * 6u);
    for (int j = 0; j < celler; ++j)
        for (int i = 0; i < celler; ++i) {
            const auto a = static_cast<std::uint32_t>(j) * r + static_cast<std::uint32_t>(i);
            d.indices.insert(d.indices.end(), {a, a + 1, a + r + 1, a, a + r + 1, a + r});
        }
    glaze::Mesh m(std::move(d));
    m.preparePicking();
    return m;
}

// JOBBENS NYCKLAR: slaget i de fyra översta bitarna, nivån och indexen under
// — kartan och 3D-vyn rensar bara sina egna slag ur arbetstrådarnas kö.
enum class Jobb : std::uint64_t {
    Bricka = 1,   // kartans baskarta
    Platta,       // kartans terrängplatta
    Ruta,         // 3D-vyns terrängruta
    Foto,         // …dess ortofoto
    Moln,         // …dess punktmoln, som värden levererar det
    MolnBygg,     // …och molnet byggt efter UI-trådens höjduppslag
    Träd,         // …dess träd
    Stolpe,       // 3D-vyns stolpmesh i en detaljnivå
};

std::uint64_t nyckel(Jobb slag, int nivå, std::int64_t i, std::int64_t j)
{
    return (static_cast<std::uint64_t>(slag) << 60) |
           (static_cast<std::uint64_t>(nivå & 0x3F) << 54) |
           (static_cast<std::uint64_t>(i & 0x7FFFFFF) << 27) |
           static_cast<std::uint64_t>(j & 0x7FFFFFF);
}
Jobb         jobbet(std::uint64_t n) { return static_cast<Jobb>(n >> 60); }
std::int64_t indexI(std::uint64_t n) { return static_cast<std::int64_t>((n >> 27) & 0x7FFFFFF); }
std::int64_t indexJ(std::uint64_t n) { return static_cast<std::int64_t>(n & 0x7FFFFFF); }
bool kartans(std::uint64_t n) { return jobbet(n) == Jobb::Bricka || jobbet(n) == Jobb::Platta; }

// STRÖMMARNA i sessionens ordning — stegens räknare (`g_steg->ström`).
enum StrömIndex { kStrömBrickor, kStrömPlattor, kStrömRutor, kStrömMoln, kStrömStolpar };

void räkna(StrömIndex s, bool in)
{
    if (g_steg)
        ++(in ? g_steg->ström[s].in : g_steg->ström[s].ut);
}

// Statusradens fält får sin text bara när den ändrats.
void sättText(glaze::StatusBar& s, int fält, const std::string& text)
{
    if (s.panelText(fält) != text)
        s.setPanelText(fält, text);
}

// ------------------------------------------------------------------ OBJEKTEN
//
// LEDNINGARNA: en mittlinje om 30–125 punkter med en stolpe i varje, spann om
// 45–70 m och en vinkelpunkt var nionde stolpe. Objekt 0 går genom scenens
// mitt mot nordost, och 3D-vyn kör längs det. Byggda en gång före fönstren
// och sedan bara lästa (arbetstrådarna läser dem); kartan ändrar en egen
// kopia.
struct Objekt {
    std::vector<glaze::Point> linje;
    std::vector<double>       riktning;   // grader, kompass, per stolpe
    int                       typ = 0;    // stolptypen, 0–3
};

std::vector<Objekt> g_objekt;

std::vector<Objekt> byggObjekt(int antal)
{
    std::vector<Objekt> ut;
    Slump               r{20261016};
    for (int k = 0; k < antal; ++k) {
        Objekt       o;
        const int    n     = k == 0 ? 125 : 30 + static_cast<int>(r.nästa() % 96u);
        const double spann = r.mellan(45.0, 70.0);
        double       kurs  = k == 0 ? 40.0 : r.mellan(0.0, 360.0);
        double       x     = kX0 + r.mellan(-1500.0, 1500.0);
        double       y     = kY0 + r.mellan(-1500.0, 1500.0);
        if (k == 0) {
            x = kX0 - 0.5 * n * spann * std::sin(kurs * kPi / 180.0);
            y = kY0 - 0.5 * n * spann * std::cos(kurs * kPi / 180.0);
        }
        for (int i = 0; i < n; ++i) {
            o.linje.push_back({x, y});
            o.riktning.push_back(kurs);
            if (i % 9 == 8)
                kurs += r.mellan(-25.0, 25.0);
            x += spann * std::sin(kurs * kPi / 180.0);
            y += spann * std::cos(kurs * kPi / 180.0);
        }
        o.typ = k % 4;
        ut.push_back(std::move(o));
    }
    return ut;
}

// Ledningarnas SEGMENT som går inom `marg` från rutan — så att varje punkt i
// en ruta bara mäts mot sina få grannsegment.
struct Segment {
    glaze::Point a, b;
};

std::vector<Segment> segmentNära(double x0, double y0, double x1, double y1, double marg)
{
    std::vector<Segment> ut;
    for (const Objekt& o : g_objekt)
        for (std::size_t i = 0; i + 1 < o.linje.size(); ++i) {
            const glaze::Point a = o.linje[i], b = o.linje[i + 1];
            if (std::max(a.x, b.x) < x0 - marg || std::min(a.x, b.x) > x1 + marg ||
                std::max(a.y, b.y) < y0 - marg || std::min(a.y, b.y) > y1 + marg)
                continue;
            ut.push_back({a, b});
        }
    return ut;
}

double avstånd(const std::vector<Segment>& seg, double x, double y)
{
    double bäst = 1e30;
    for (const Segment& s : seg) {
        const double dx = s.b.x - s.a.x, dy = s.b.y - s.a.y;
        const double t  = std::clamp(((x - s.a.x) * dx + (y - s.a.y) * dy) /
                                         std::max(dx * dx + dy * dy, 1e-9),
                                     0.0, 1.0);
        bäst = std::min(bäst, std::hypot(s.a.x + t * dx - x, s.a.y + t * dy - y));
    }
    return bäst;
}

// STRÅKBANDET: mittlinjen förskjuten `halv` m åt båda hållen — en ring om två
// gånger linjens punkter.
std::vector<glaze::Point> bandRing(const std::vector<glaze::Point>& l, double halv)
{
    std::vector<glaze::Point> vä, hö;
    for (std::size_t i = 0; i < l.size(); ++i) {
        const glaze::Point a = l[i > 0 ? i - 1 : i], b = l[i + 1 < l.size() ? i + 1 : i];
        const double       dx = b.x - a.x, dy = b.y - a.y;
        const double       len = std::max(std::hypot(dx, dy), 1e-9);
        vä.push_back({l[i].x - dy / len * halv, l[i].y + dx / len * halv});
        hö.push_back({l[i].x + dy / len * halv, l[i].y - dx / len * halv});
    }
    vä.insert(vä.end(), hö.rbegin(), hö.rend());
    return vä;
}

// GLORIAN — hovrets markering: vit och bred under
// linjen, sluten för en yta.
glaze::Shape gloria(std::vector<glaze::Point> pts, bool sluten)
{
    if (sluten && !pts.empty())
        pts.push_back(pts.front());
    return glaze::Shape::polyline(std::move(pts), {.paint = gfx::Color{255, 255, 255, 170},
                                                   .width = glaze::mm(2.4f),
                                                   .cap   = glaze::LineCap::Round,
                                                   .join  = glaze::LineJoin::Round});
}

// Punkten på andelen `s` (0–1) av linjens längd.
glaze::Point längsLinjen(const std::vector<glaze::Point>& l, double s)
{
    double total = 0.0;
    for (std::size_t i = 0; i + 1 < l.size(); ++i)
        total += std::hypot(l[i + 1].x - l[i].x, l[i + 1].y - l[i].y);
    double kvar = std::clamp(s, 0.0, 1.0) * total;
    for (std::size_t i = 0; i + 1 < l.size(); ++i) {
        const double d = std::hypot(l[i + 1].x - l[i].x, l[i + 1].y - l[i].y);
        if (kvar <= d && d > 0.0)
            return {l[i].x + (l[i + 1].x - l[i].x) * kvar / d,
                    l[i].y + (l[i + 1].y - l[i].y) * kvar / d};
        kvar -= d;
    }
    return l.back();
}

// MARKENS SYNLIGA DEL: nollplanets och toppens synliga polygon (upphöjd mark
// syns också utanför nollplanets), som ett konvext hölje moturs.
std::vector<glaze::Point> konvextHölje(std::vector<glaze::Point> p)
{
    std::sort(p.begin(), p.end(), [](const glaze::Point& a, const glaze::Point& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    if (p.size() < 3)
        return p;
    const auto kors = [](const glaze::Point& o, const glaze::Point& a, const glaze::Point& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    std::vector<glaze::Point> h(2 * p.size());
    std::size_t               k = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        while (k >= 2 && kors(h[k - 2], h[k - 1], p[i]) <= 0)
            --k;
        h[k++] = p[i];
    }
    for (std::size_t i = p.size() - 1, t = k + 1; i > 0; --i) {
        while (k >= t && kors(h[k - 2], h[k - 1], p[i - 1]) <= 0)
            --k;
        h[k++] = p[i - 1];
    }
    h.resize(k - 1);
    return h;
}

// Ligger punkten i höljet, eller närmare dess kant än `marg`?
bool nära(const std::vector<glaze::Point>& h, glaze::Point p, double marg)
{
    bool   inne = true;
    double bäst = 1e30;
    for (std::size_t i = 0; i < h.size(); ++i) {
        const glaze::Point a = h[i], b = h[(i + 1) % h.size()];
        const double       dx = b.x - a.x, dy = b.y - a.y;
        if (dx * (p.y - a.y) - dy * (p.x - a.x) < 0.0)
            inne = false;
        const double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) /
                                        std::max(dx * dx + dy * dy, 1e-9),
                                    0.0, 1.0);
        bäst = std::min(bäst, std::hypot(a.x + t * dx - p.x, a.y + t * dy - p.y));
    }
    return inne || bäst <= marg;
}

// FORMPLATSER i ett lager: en släppt plats töms med
// en tom form och lånas ut igen — formerna byter aldrig index, så en plats som
// fylls igen är samma plats.
class Platser {
public:
    void bind(glaze::Canvas& yta, int lager)
    {
        yta_   = &yta;
        lager_ = lager;
    }
    int lägg(glaze::Shape s)
    {
        if (lediga_.empty())
            return yta_->addShape(lager_, std::move(s));
        const int p = lediga_.back();
        lediga_.pop_back();
        yta_->setShape(lager_, p, std::move(s));
        return p;
    }
    void sätt(int plats, glaze::Shape s) { yta_->setShape(lager_, plats, std::move(s)); }
    void släpp(int& plats)
    {
        if (plats < 0)
            return;
        yta_->setShape(lager_, plats, glaze::Shape{});
        lediga_.push_back(plats);
        plats = -1;
    }

private:
    glaze::Canvas*   yta_   = nullptr;
    int              lager_ = 0;
    std::vector<int> lediga_;
};

// ================================================================== KARTAN

constexpr int    kKartaB           = 1280, kKartaH = 800;   // innehållsytan, logiska px
constexpr int    kPlattCeller      = 128;     // terrängplattans celler per sida
constexpr int    kPlattcache       = 128;     // byggda plattor i cachen
constexpr int    kPlattorPerAxel   = 6;       // ~synliga per axel
constexpr int    kBrickcache       = 2000;    // baskartans brickor (LRU)
constexpr int    kBrickTak         = 300;     // brickor i vyn, närmast mitten först
constexpr int    kBatchMs          = 200;     // baskartans batcher
constexpr int    kVektorLager      = 5;
constexpr int    kVektorFormer     = 300;
constexpr double kBandHalv         = 20.0;    // stråkbandets halva bredd, m
constexpr int    kRedigeringHöjder = 24000;   // heightAt per bild i redigeringen
constexpr int    kTvärs            = 8;       // …varav punkter tvärs över bandet
constexpr int    kRedigeringTick   = 40;      // bilder i redigeringen …
constexpr double kRedigeringTak    = 30.0;    // … eller sekunder (minst 10 bilder)
constexpr int    kHoverPlats       = 1;       // objektlagrets plats för hovrets gloria
constexpr double kStartZoom        = 1.0;     // logiska px per m: ≈ 1:3800 vid 96 dpi
// Kartappens tiltregel.
constexpr float  kTiltRegelGrader  = 3.0f;    // under den räknas kartan som platt
constexpr float  kGlidTilt         = 30.0f;   // inglidningen reser kartan minst hit
constexpr double kTiltSkala        = 1000.0;  // 1:1000
constexpr int    kGlidInMs         = 700;
constexpr int    kGlidNerMs        = 600;
constexpr int    kGlidTimerMs      = 50;      // m_tiltEaseTimer

struct Bricka {
    std::uint64_t               nyckel;
    std::array<glaze::Point, 4> hörn;
};

struct Platta {
    std::uint64_t nyckel;
    double        x0, y0, sida;
};

// Ett lagers ROLL i hovret: objektets lager eller ett vektorlager.
struct Roll {
    enum Slag : std::uint8_t { Ingen, Objekt, Vektor } slag = Ingen;
    int id = -1;
};

// Det HOVRADE: ett objekt, eller en form i ett vektorlager.
struct Hover {
    int  objekt = -1, vektor = -1, index = -1;
    bool operator==(const Hover&) const = default;
};

// HUR 3D-APPENS ÄNDRINGAR NÅR KARTAN: Idag (en `requestRepaint(Map)`
// bygger om ALLA objektlager) eller Rättad (bara det ändrade).
enum class Läge { Idag, Rättad };

class KartFönster : public Fönster {
public:
    explicit KartFönster(Arbetare& arbete) : arbete_(arbete)
    {
        setTitle("terrain_perf — kartan");
        karta.name = "karta";
        karta.dock = glaze::Dock::Fill;
        karta.setBorder(false);
        karta.setPannable(true);
        karta.setZoomable(true);
        karta.setRotatable(true);
        karta.setPitchable(true);
        root().adopt(karta);
        statusBar.show();
        statusBar.addPanel("Klar", 0.0f, true);
        statusBar.addPanel("N", 130.0f);
        statusBar.addPanel("E", 130.0f);
        statusBar.addPanel("Höjd", 110.0f);
        statusBar.addPanel("Skala", 90.0f);
        statusBar.addPanel("Terräng", 150.0f);
        statusBar.addPanel("Karta", 150.0f);
        setContentSize(kKartaB, kKartaH);
        if (!g_synlig)
            setOffscreen(true);
        // BATCHEN: inkomna brickor byggs in högst var 200 ms.
        batch_.setSingleShot(true);
        batch_.setInterval(kBatchMs);
        batch_.onTick = [this] { byggBaskarta(); };
        // TILTGLIDNINGEN: ritarna får vybytena av en 50 ms-timer.
        glidTimer_.setInterval(kGlidTimerMs);
        glidTimer_.onTick = [this] { glidTick(); };
    }

    glaze::Canvas karta;
    Läge          läge = Läge::Rättad;

    static KartFönster* g_karta;

    // ---------------------------------------------------------- scenen
    void byggScen()
    {
        const auto t0 = Klocka::now();
        objekt_       = g_objekt;
        // 3D-DETALJERNAS stolpar (modulernas Space-pass): två enkla meshar.
        stolpe_[0] = byggStolpe(600, 77, 1);
        stolpe_[1] = byggStolpe(600, 78, 1);

        // TERRÄNGEN i lager 0, dold tills kartan tiltas.
        karta.setLayerPlacement(lagerTerräng_, glaze::LayerPlacement::Terrain);
        karta.setLayerVisible(lagerTerräng_, false);
        terräng_.bind(karta, lagerTerräng_);
        const auto lager = [this](const std::string& namn, glaze::DrawOrder ordning) {
            const int l = karta.addLayer(namn);
            karta.setLayerPlacement(l, glaze::LayerPlacement::Ground);
            karta.setLayerDrawOrder(l, ordning);
            return l;
        };
        lagerKarta_ = lager("baskarta", glaze::DrawOrder::ByKind);
        // VEKTORERNA — glorialagret under formerna.
        for (int l = 0; l < kVektorLager; ++l) {
            gloria_[l] = lager("vektor " + std::to_string(l) + " gloria", glaze::DrawOrder::ByKind);
            vektor_[l] = lager("vektor " + std::to_string(l), glaze::DrawOrder::ByKind);
        }
        byggVektorer();
        // OBJEKTEN — fyra lager vardera.
        for (std::size_t k = 0; k < objekt_.size(); ++k) {
            const std::string n = "objekt " + std::to_string(k);
            ObjektLager       o;
            o.band   = lager(n + " band", glaze::DrawOrder::Strict);
            o.objekt = lager(n, glaze::DrawOrder::Strict);
            o.efter  = lager(n + " efter", glaze::DrawOrder::ByKind);
            o.rymd   = karta.addLayer(n + " 3D");   // Auto: World av formerna i rymden
            lagren_.push_back(o);
        }
        // MARKÖRERNA i egna små lager: 3D-vyns pose och profilfönstrets hover
        // ("rättad").
        lagerMarkör_ = lager("3D-vyns markör", glaze::DrawOrder::ByKind);
        karta.addShape(lagerMarkör_, glaze::Shape{});
        lagerProfil_ = lager("profilens markör", glaze::DrawOrder::ByKind);
        karta.addShape(lagerProfil_, glaze::Shape{});
        roller_.assign(static_cast<std::size_t>(karta.layerCount()), Roll{});
        for (int l = 0; l < kVektorLager; ++l)
            roller_[static_cast<std::size_t>(vektor_[l])] = {Roll::Vektor, l};
        for (std::size_t k = 0; k < lagren_.size(); ++k) {
            roller_[static_cast<std::size_t>(lagren_[k].objekt)] = {Roll::Objekt,
                                                                    static_cast<int>(k)};
            byggObjektet(static_cast<int>(k));
        }

        // Hillshadens ljus: från nordväst, 45° upp.
        glaze::Lighting ljus;
        ljus.direction = {0.5, -0.5, -0.7071};
        ljus.ambient   = 0.35f;
        ljus.diffuse   = 0.65f;
        karta.setLighting(ljus);
        karta.setMemoryBudget(std::size_t{1} << 30);
        karta.setCamera(glaze::Camera{.center = {kX0, kY0}, .zoom = kStartZoom});

        karta.onPaint        = [this](glaze::Widget*, glaze::PaintArgs& e) { måla(e.painter); };
        karta.onPaintOverlay = [this](glaze::Widget*, glaze::PaintArgs& e) { överlägg(e.painter); };
        karta.onMouseMove    = [this](glaze::Widget*, glaze::CanvasMouseArgs& e) { mus(e); };
        // Redigeringen TAR vänstertrycket — då panorerar ritytan inte.
        karta.onMouseDown = [this](glaze::Widget*, glaze::CanvasMouseArgs& e) {
            if (redigerar_ >= 0 && e.button == gfx::MouseButton::Left)
                e.handled = true;
        };
        karta.onTick         = [this](glaze::Widget*, glaze::TickArgs& e) { tick(e.seconds); };
        karta.onCameraChange = [this](glaze::Widget*, glaze::ChangeArgs&) { kameraÄndrad(); };

        std::size_t stolpar = 0;
        for (const Objekt& o : objekt_)
            stolpar += o.linje.size();
        std::printf("  scen kartan: %zu objekt med %zu stolpar i %d lager, %d vektorlager om %d "
                    "former — byggd på %.0f ms\n",
                    objekt_.size(), stolpar, karta.layerCount(), kVektorLager, kVektorFormer,
                    msSedan(t0));
    }

    // ------------------------------------------------------------ kameran
    // ANVÄNDARENS rörelse (onCameraChange) — kartappens tiltregel, och ritarna
    // får vybytet.
    void kameraÄndrad()
    {
        if (!glider_ && tiltBruten())
            glid(true);
        vyKoll();
        zoomGolv();
    }

    // KODENS kamera: ritytans sättare fyrar inget
    // vybyte, så ritarna får det härifrån.
    void sättKamera(const glaze::Camera& c)
    {
        karta.setCamera(c);
        vyKoll();
        zoomGolv();
    }

    // Kamerans zoom i 1:1000.
    double zoomTiltSkala() const
    {
        const double s = karta.mapScale();
        return s > 0.0 ? karta.camera().zoom * s / kTiltSkala : karta.camera().zoom;
    }
    bool tiltRegeln() const { return karta.camera().pitch >= kTiltRegelGrader; }
    bool tiltBruten() const { return tiltRegeln() && karta.mapScale() > kTiltSkala * 1.001; }

    // GLIDNINGEN in (1:1000, minst 30°) eller ned (platt) — ritytans `easeTo`;
    // ritarna följer den genom 50 ms-timern (`easeTo` fyrar inga vybyten).
    void glid(bool in)
    {
        glaze::Camera mål = karta.camera();
        if (in) {
            mål.zoom  = zoomTiltSkala();
            mål.pitch = std::min(std::max(mål.pitch, kGlidTilt), karta.maxPitch());
        } else {
            mål.pitch = 0.0f;
        }
        glider_ = true;
        karta.setZoomRange(0.0, 0.0);   // golvet släpps — det hade klampat glidningens zoom
        karta.easeTo(mål, in ? kGlidInMs : kGlidNerMs);
        glidTimer_.start();
    }

    void glidTick()
    {
        if (karta.isEasing()) {
            vyKoll();
            return;
        }
        glidTimer_.stop();
        glider_ = false;
        vyKoll();
        zoomGolv();
    }

    // Golvet: en tiltad karta zoomar inte ut förbi 1:1000 — bara när den står
    // innanför, och aldrig mitt i en glidning.
    void zoomGolv()
    {
        const double golv = !glider_ && tiltRegeln() && !tiltBruten() ? zoomTiltSkala() : 0.0;
        karta.setZoomRange(golv, 0.0);
    }

    // ------------------------------------------------------- strömningen
    // Kameran ändrad? Då räknas terrängens plattor och baskartans brickor om
    // — ur markens synliga del.
    void vyKoll()
    {
        const glaze::Camera k = karta.camera();
        const gfx::Sizef    s = ytansStorlek(karta);
        if (k.center.x == förraKam_.center.x && k.center.y == förraKam_.center.y &&
            k.zoom == förraKam_.zoom && k.bearing == förraKam_.bearing &&
            k.pitch == förraKam_.pitch && s.w == förraYta_.w && s.h == förraYta_.h)
            return;
        std::vector<glaze::Point> p;
        for (const glaze::Point3& q : karta.visibleRegion(glaze::Plane3{}))
            p.push_back({q.x, q.y});
        for (const glaze::Point3& q :
             karta.visibleRegion(glaze::Plane3{.point = {kX0, kY0, kMarkTopp}}))
            p.push_back({q.x, q.y});
        const std::vector<glaze::Point> hölje = konvextHölje(std::move(p));
        if (hölje.size() < 3)
            return;   // ingen yta än — nästa tick försöker igen
        förraKam_ = k;
        förraYta_ = s;
        sättText(statusBar, 4, "1:" + std::to_string(std::lround(karta.mapScale())));
        terrängVy(k, hölje);
        brickVy(k, hölje);
    }

    // ---------------------------------------------------------- terrängen
    void terrängVy(const glaze::Camera& k, const std::vector<glaze::Point>& hölje)
    {
        // Terrängen följer tilten först från en grad.
        if (k.pitch < 1.0f) {
            if (terrängAktiv_)
                avaktivera();
            return;
        }
        double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
        for (const glaze::Point& q : hölje) {
            x0 = std::min(x0, q.x);
            y0 = std::min(y0, q.y);
            x1 = std::max(x1, q.x);
            y1 = std::max(y1, q.y);
        }
        // NIVÅN: ~6 plattor över markens synliga utsträckning, hysteres ±30 %.
        const double ideal =
            std::log2(std::max(std::max(x1 - x0, y1 - y0) / kPlattorPerAxel, 64.0) / 64.0);
        if (plattExp_ < 0 || ideal > plattExp_ + 0.82 || ideal < plattExp_ - 0.82)
            plattExp_ = std::clamp(static_cast<int>(std::lround(ideal)), 0, 12);
        const double sida = 64.0 * std::pow(2.0, plattExp_);
        const auto   i0   = static_cast<std::int64_t>(std::floor((x0 - kRutnätX) / sida));
        const auto   i1   = static_cast<std::int64_t>(std::floor((x1 - kRutnätX) / sida));
        const auto   j0   = static_cast<std::int64_t>(std::floor((y0 - kRutnätY) / sida));
        const auto   j1   = static_cast<std::int64_t>(std::floor((y1 - kRutnätY) / sida));
        önskadePlattor_.clear();
        if ((i1 - i0 + 1) * (j1 - j0 + 1) <= 4096)
            for (std::int64_t j = j0; j <= j1; ++j)
                for (std::int64_t i = i0; i <= i1; ++i) {
                    const double px = kRutnätX + i * sida, py = kRutnätY + j * sida;
                    if (nära(hölje, {px + sida / 2, py + sida / 2}, sida * 0.7072))
                        önskadePlattor_.push_back(
                            {nyckel(Jobb::Platta, plattExp_, i, j), px, py, sida});
                }
        // Mitten först: det användaren tittar på kommer först.
        const auto avståndet = [&k](const Platta& p) {
            return std::hypot(p.x0 + p.sida / 2 - k.center.x, p.y0 + p.sida / 2 - k.center.y);
        };
        std::sort(önskadePlattor_.begin(), önskadePlattor_.end(),
                  [&avståndet](const Platta& a, const Platta& b) {
                      return avståndet(a) < avståndet(b);
                  });
        std::unordered_set<std::uint64_t> önskade;
        for (const Platta& q : önskadePlattor_)
            önskade.insert(q.nyckel);
        for (std::uint64_t n : arbete_.rensa([&önskade](std::uint64_t n) {
                 return jobbet(n) != Jobb::Platta || önskade.count(n);
             }))
            påvägPlattor_.erase(n);
        if (!terrängAktiv_) {
            terrängAktiv_ = true;
            for (std::size_t o = 0; o < lagren_.size(); ++o)
                byggRymd(static_cast<int>(o));   // modulernas 3D-detaljer står i terrängens z
        }
        uppdateraTerräng();
    }

    void avaktivera()
    {
        terrängAktiv_ = false;
        karta.setLayerVisible(lagerTerräng_, false);
        terrängSyns_ = false;
        visadExp_    = -1;
        for (std::uint64_t n : arbete_.rensa([](std::uint64_t n) { return jobbet(n) != Jobb::Platta; }))
            påvägPlattor_.erase(n);
        for (std::size_t o = 0; o < lagren_.size(); ++o)
            byggRymd(static_cast<int>(o));
    }

    // Det som saknas begärs; lagret följer när nivån är komplett (eller
    // direkt inom samma nivå), och cachen vräks.
    void uppdateraTerräng()
    {
        if (!terrängAktiv_)
            return;
        bool komplett = true;
        for (const Platta& p : önskadePlattor_) {
            const auto it = plattCache_.find(p.nyckel);
            if (it != plattCache_.end()) {
                it->second.använd = ++klocka_;
                continue;
            }
            komplett = false;
            if (påvägPlattor_.count(p.nyckel))
                continue;
            påvägPlattor_.insert(p.nyckel);
            const std::uint64_t n = p.nyckel;
            const double        x0 = p.x0, y0 = p.y0, sida = p.sida;
            arbete_.lägg(n, [n, x0, y0, sida] {
                auto m = std::make_shared<glaze::Mesh>(markMesh(x0, y0, sida, kPlattCeller, false));
                postaTillUi([n, m] { g_karta->plattaKom(n, std::move(*m)); });
            });
        }
        // Ett nivåbyte behåller det lagret bär tills den nya nivån är byggd —
        // annars blinkade reliefen platt.
        if (visadExp_ < 0 || plattExp_ == visadExp_ || komplett)
            tillämpaTerräng();
        vräkPlattor();
    }

    void tillämpaTerräng()
    {
        std::unordered_set<std::uint64_t> önskade;
        for (const Platta& p : önskadePlattor_)
            önskade.insert(p.nyckel);
        // Platserna som inte längre önskas släpps först — de nya lånar dem.
        for (auto it = plattPlats_.begin(); it != plattPlats_.end();)
            if (!önskade.count(it->first)) {
                terräng_.släpp(it->second.plats);
                it = plattPlats_.erase(it);
            } else {
                ++it;
            }
        for (const Platta& p : önskadePlattor_) {
            const auto c  = plattCache_.find(p.nyckel);
            const auto it = plattPlats_.find(p.nyckel);
            if (it == plattPlats_.end()) {
                const bool byggd = c != plattCache_.end();
                plattPlats_[p.nyckel] = {
                    terräng_.lägg(markform(byggd ? c->second.mesh : platshållare(p))), byggd};
            } else if (!it->second.byggd && c != plattCache_.end()) {
                // PLATSHÅLLAREN → MESHEN PÅ SAMMA PLATS (`setShape`).
                terräng_.sätt(it->second.plats, markform(c->second.mesh));
                it->second.byggd = true;
            }
        }
        visadExp_ = plattExp_;
        if (!terrängSyns_) {
            karta.setLayerVisible(lagerTerräng_, true);
            terrängSyns_ = true;
        }
    }

    // Plattan utan DEM: platt på z = 0, tills den riktiga kommer
    // — byggd i UI-tråden.
    static glaze::Mesh platshållare(const Platta& p)
    {
        const float     s = static_cast<float>(p.sida);
        glaze::MeshDesc d;
        d.positions = {{0.0f, 0.0f, 0.0f}, {s, 0.0f, 0.0f}, {s, s, 0.0f}, {0.0f, s, 0.0f}};
        d.normals   = std::vector<glaze::Float3>(4, glaze::Float3{0.0f, 0.0f, 1.0f});
        d.indices   = {0, 1, 2, 0, 2, 3};
        d.origin    = {p.x0, p.y0, 0.0};
        glaze::Mesh m(std::move(d));
        m.preparePicking();
        return m;
    }

    // Över DEM:en vit och belyst — hillshaden; draperingen ligger ovanpå.
    static glaze::Shape markform(const glaze::Mesh& m)
    {
        return glaze::Shape::mesh(m, {},
                                  {.color = gfx::Color{255, 255, 255, 255}, .lit = true, .fog = false});
    }

    void vräkPlattor()
    {
        if (static_cast<int>(plattCache_.size()) <= kPlattcache)
            return;
        std::unordered_set<std::uint64_t> önskade;
        for (const Platta& p : önskadePlattor_)
            önskade.insert(p.nyckel);
        std::vector<std::pair<std::uint64_t, std::uint64_t>> gamla;   // använd, nyckel
        for (const auto& [n, c] : plattCache_)
            if (!önskade.count(n))
                gamla.push_back({c.använd, n});
        std::sort(gamla.begin(), gamla.end());
        for (const auto& [använd, n] : gamla) {
            if (static_cast<int>(plattCache_.size()) <= kPlattcache)
                break;
            plattCache_.erase(n);
            räkna(kStrömPlattor, false);
        }
    }

    void plattaKom(std::uint64_t n, glaze::Mesh m)
    {
        påvägPlattor_.erase(n);
        plattCache_[n] = {std::move(m), ++klocka_};
        räkna(kStrömPlattor, true);
        uppdateraTerräng();
    }

    // ---------------------------------------------------------- baskartan
    void brickVy(const glaze::Camera& k, const std::vector<glaze::Point>& hölje)
    {
        // NIVÅN ur BUFFERTENS meter per px i mitten — en nivå för hela vyn.
        float skala = 1.0f;
        ytansRuta(karta, skala);
        const double mpp  = 1.0 / (k.zoom * skala);
        const int    nivå = std::clamp(
            static_cast<int>(std::lround(19.0 - std::log2(mpp / 0.25))), 6, 19);
        const double tm = plattM(nivå);
        double       x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
        for (const glaze::Point& q : hölje) {
            x0 = std::min(x0, q.x);
            y0 = std::min(y0, q.y);
            x1 = std::max(x1, q.x);
            y1 = std::max(y1, q.y);
        }
        const auto i0 = static_cast<std::int64_t>(std::floor((x0 - kRutnätX) / tm));
        const auto i1 = static_cast<std::int64_t>(std::floor((x1 - kRutnätX) / tm));
        const auto j0 = static_cast<std::int64_t>(std::floor((y0 - kRutnätY) / tm));
        const auto j1 = static_cast<std::int64_t>(std::floor((y1 - kRutnätY) / tm));
        std::vector<std::pair<double, Bricka>> kandidater;
        if ((i1 - i0 + 1) * (j1 - j0 + 1) <= 40000)
            for (std::int64_t j = j0; j <= j1; ++j)
                for (std::int64_t i = i0; i <= i1; ++i) {
                    const double       px = kRutnätX + i * tm, py = kRutnätY + j * tm;
                    const glaze::Point mitt{px + tm / 2, py + tm / 2};
                    if (!nära(hölje, mitt, tm * 0.7072))
                        continue;
                    // Källan (steget med batcherna byter den) står i nivåns
                    // sjätte bit.
                    kandidater.push_back(
                        {std::hypot(mitt.x - k.center.x, mitt.y - k.center.y),
                         {nyckel(Jobb::Bricka, nivå | (källa_ << 5), i, j), plattHörn(px, py, tm)}});
                }
        std::sort(kandidater.begin(), kandidater.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        if (kandidater.size() > static_cast<std::size_t>(kBrickTak))
            kandidater.resize(static_cast<std::size_t>(kBrickTak));
        std::vector<Bricka> nya;
        for (auto& [d, b] : kandidater)
            nya.push_back(b);
        // Samma brickor i vyn ger samma former — ingen ombyggnad.
        if (nya.size() == brickor_.size() &&
            std::equal(nya.begin(), nya.end(), brickor_.begin(),
                       [](const Bricka& a, const Bricka& b) { return a.nyckel == b.nyckel; }))
            return;
        brickor_ = std::move(nya);
        brickSet_.clear();
        for (const Bricka& b : brickor_)
            brickSet_.insert(b.nyckel);
        for (std::uint64_t n : arbete_.rensa([this](std::uint64_t n) {
                 return jobbet(n) != Jobb::Bricka || brickSet_.count(n);
             }))
            påvägBrickor_.erase(n);
        for (const Bricka& b : brickor_)
            if (!brickCache_.count(b.nyckel) && !påvägBrickor_.count(b.nyckel)) {
                påvägBrickor_.insert(b.nyckel);
                const std::uint64_t n = b.nyckel;
                arbete_.lägg(n, [n] {
                    nätet(n);
                    auto bild = std::make_shared<glaze::Bitmap>(avkoda(n));
                    postaTillUi([n, bild] { g_karta->brickaKom(n, std::move(*bild)); });
                });
            }
        byggBaskarta();
    }

    void brickaKom(std::uint64_t n, glaze::Bitmap b)
    {
        påvägBrickor_.erase(n);
        brickLru_.push_front(n);
        brickCache_[n] = {std::move(b), brickLru_.begin()};
        räkna(kStrömBrickor, true);
        while (static_cast<int>(brickCache_.size()) > kBrickcache) {
            const std::uint64_t äldst = brickLru_.back();
            if (brickSet_.count(äldst))
                break;
            brickLru_.pop_back();
            brickCache_.erase(äldst);
            räkna(kStrömBrickor, false);
        }
        if (brickSet_.count(n)) {
            baskartaÄndrad_ = true;
            if (!batch_.isRunning())
                batch_.start();
        }
    }

    // En ny källa för baskartan — alla brickor i vyn hämtas om och kommer in
    // i batcher om 200 ms.
    void bytKälla()
    {
        källa_ ^= 1;
        förraKam_ = glaze::Camera{.zoom = -1.0};
        vyKoll();
    }

    // HELA LAGRET byggs om.
    void byggBaskarta()
    {
        baskartaÄndrad_ = false;
        const auto                t0 = Klocka::now();
        std::vector<glaze::Shape> s;
        s.reserve(brickor_.size());
        int inne = 0;
        for (const Bricka& b : brickor_) {
            const auto it = brickCache_.find(b.nyckel);
            if (it != brickCache_.end()) {
                brickLru_.splice(brickLru_.begin(), brickLru_, it->second.var);
                s.push_back(glaze::Shape::image(b.hörn[0], b.hörn[1], b.hörn[2], b.hörn[3],
                                                it->second.bild));
                ++inne;
            } else {
                s.push_back(glaze::Shape::polygon(
                    std::vector<glaze::Point>(b.hörn.begin(), b.hörn.end()),
                    påvägBrickor_.count(b.nyckel) ? gfx::Color{79, 79, 89, 255}
                                                   : gfx::Color{51, 51, 61, 255}));
            }
        }
        karta.setShapes(lagerKarta_, std::move(s));
        if (g_steg)
            g_steg->bygg.add(msSedan(t0));
        brickorInne_ = inne;
    }

    // ------------------------------------------------------------ objekten
    // ObjectRenderer::buildObject: banden, objektets lager med två
    // gloriaplatser och linjen, After-lagret och 3D-detaljerna.
    void byggObjektet(int k)
    {
        const Objekt&      o = objekt_[static_cast<std::size_t>(k)];
        const ObjektLager& l = lagren_[static_cast<std::size_t>(k)];
        karta.setShapes(l.band, {glaze::Shape::polygon(
                                    bandRing(o.linje, kBandHalv), gfx::Color{255, 170, 0, 70},
                                    glaze::Stroke{.paint = gfx::Color{255, 140, 0, 170},
                                                  .width = glaze::mm(0.4f)})});
        std::vector<glaze::Shape> objektet;
        objektet.push_back(glaze::Shape{});   // markerad — ingen i scenen
        objektet.push_back(hover_.objekt == k ? gloria(o.linje, false) : glaze::Shape{});
        objektet.push_back(glaze::Shape::polyline(
            o.linje, {.paint = gfx::Color{static_cast<std::uint8_t>(40 + 40 * o.typ), 40, 160, 255},
                      .width = glaze::mm(0.6f)}));
        karta.setShapes(l.objekt, std::move(objektet));
        byggEfter(k);
        byggRymd(k);
    }

    // AFTER-LAGRET — 3D-appens former för objektet: stolparna, deras
    // etiketter, den stolpe som dras i profilvyn med sin höjd, och i läget Idag
    // profilfönstrets hover (`requestRepaint` ritar den som en del av
    // objektets former).
    void byggEfter(int k)
    {
        const Objekt&             o = objekt_[static_cast<std::size_t>(k)];
        std::vector<glaze::Shape> efter;
        efter.push_back(glaze::Shape::points(o.linje, {.shape        = glaze::MarkerShape::Circle,
                                                       .size         = glaze::px(6.0f),
                                                       .fill         = gfx::Color{255, 255, 255, 255},
                                                       .outline      = gfx::Color{20, 20, 20, 255},
                                                       .outlineWidth = glaze::px(1.5f)}));
        glaze::TextStyle ts;
        ts.size     = glaze::mm(2.5f);
        ts.color    = gfx::Color{20, 20, 20, 255};
        ts.halo     = gfx::Color{255, 255, 255, 220};
        ts.anchor   = glaze::TextAnchor::Left;
        ts.offsetX  = glaze::mm(1.5f);
        ts.priority = 5;
        for (std::size_t i = 0; i < o.linje.size(); ++i) {
            std::string text = "S" + std::to_string(i + 1);
            if (k == dragObjekt_ && static_cast<int>(i) == dragStolpe_) {
                char h[32];
                std::snprintf(h, sizeof h, " · %.1f m", 12.0 + dragLyft_);
                text += h;
                efter.push_back(glaze::Shape::marker(
                    {o.linje[i].x + dragLyft_, o.linje[i].y},
                    {.size = glaze::px(9.0f), .fill = gfx::Color{255, 120, 0, 255}}));
            }
            efter.push_back(glaze::Shape::text(o.linje[i], text, ts));
        }
        if (k == 0 && profilPos_ >= 0.0)
            efter.push_back(glaze::Shape::marker(längsLinjen(o.linje, profilPos_),
                                                 {.size = glaze::px(10.0f),
                                                  .fill = gfx::Color{0, 160, 255, 255}}));
        karta.setShapes(lagren_[static_cast<std::size_t>(k)].efter, std::move(efter));
    }

    // ObjectRenderer::buildSpace — bara med terrängen.
    void byggRymd(int k)
    {
        const Objekt&             o = objekt_[static_cast<std::size_t>(k)];
        std::vector<glaze::Shape> s;
        if (terrängAktiv_)
            for (std::size_t i = 0; i < o.linje.size(); ++i)
                s.push_back(glaze::Shape::mesh(
                    stolpe_[o.typ % 2],
                    {.at  = {o.linje[i].x, o.linje[i].y, markhöjd(o.linje[i].x, o.linje[i].y)},
                     .yaw = static_cast<float>(o.riktning[i])}));
        karta.setShapes(lagren_[static_cast<std::size_t>(k)].rymd, std::move(s));
    }

    // `requestRepaint(Map)`: ObjectRenderer bygger om ALLA objekt.
    void byggAllaObjekt()
    {
        for (std::size_t k = 0; k < objekt_.size(); ++k)
            byggObjektet(static_cast<int>(k));
    }

    // Det redigerade objektet ritas av editorn.
    void rensaObjektet(int k)
    {
        const ObjektLager& l = lagren_[static_cast<std::size_t>(k)];
        for (int lager : {l.band, l.objekt, l.efter, l.rymd})
            karta.setShapes(lager, {});
    }

    // -------------------------------------------------------- 3D-APPEN
    // 3D-VYNS POSE (`publishPose`): "idag" bygger ALLA objekt om
    // (`requestRepaint(Map)`), "rättad" flyttar bara markören.
    void sättPose(glaze::Point öga, double gir)
    {
        if (läge == Läge::Idag)
            byggAllaObjekt();
        const double v = gir * kPi / 180.0, fx = std::sin(v), fy = std::cos(v);
        karta.setShape(lagerMarkör_, 0,
                       glaze::Shape::polygon({{öga.x + fx * 12.0, öga.y + fy * 12.0},
                                              {öga.x - fx * 4.0 + fy * 5.0, öga.y - fy * 4.0 - fx * 5.0},
                                              {öga.x - fx * 4.0 - fy * 5.0, öga.y - fy * 4.0 + fx * 5.0}},
                                             gfx::Color{220, 30, 30, 255}));
    }

    // MODULENS FORMER: en stolpe dras i profilvyn (`i` per händelse) — "idag"
    // bygger om alla objekt, "rättad" bara objektets After-lager. `slut`
    // släpper stolpen.
    void modulÄndring(int i, bool slut)
    {
        dragObjekt_ = slut ? -1 : 0;
        dragStolpe_ = static_cast<int>(objekt_.front().linje.size() / 2);
        dragLyft_   = 3.0 * std::sin(i * 0.2);
        if (läge == Läge::Idag)
            byggAllaObjekt();
        else
            byggEfter(0);
    }

    // PROFILFÖNSTRETS HOVER: pekaren rör sig längs objekt 0 i profilvyn — "idag"
    // ritar markören bland objektets former och bygger om alla objekt, "rättad"
    // flyttar markören i sitt eget lager.
    void profilHover(int i, bool slut)
    {
        const double s = slut ? -1.0 : 0.5 + 0.4 * std::sin(i * 0.05);
        if (läge == Läge::Idag) {
            profilPos_ = s;
            byggAllaObjekt();
            return;
        }
        profilPos_ = -1.0;
        karta.setShape(lagerProfil_, 0,
                       s < 0.0 ? glaze::Shape{}
                               : glaze::Shape::marker(längsLinjen(objekt_.front().linje, s),
                                                      {.size = glaze::px(10.0f),
                                                       .fill = gfx::Color{0, 160, 255, 255}}));
    }

    // ETT OBJEKT ÄNDRAS: en punkt mitt på objekt 0 flyttas — "idag" bygger om
    // alla objekt, "rättad" bara det ändrade. `slut` lägger tillbaka punkten.
    void objektÄndring(int i, bool slut)
    {
        Objekt&           o = objekt_.front();
        const std::size_t p = o.linje.size() / 2;
        const double      d = slut ? 0.0 : 15.0 * std::sin(i * 0.15);
        o.linje[p]          = {g_objekt.front().linje[p].x + d, g_objekt.front().linje[p].y};
        if (läge == Läge::Idag)
            byggAllaObjekt();
        else
            byggObjektet(0);
    }

    // ---------------------------------------------------------------- hovret
    void hovra(const Hover& ny)
    {
        if (ny == hover_)
            return;
        const Hover förra = hover_;
        hover_            = ny;
        ++hoverByten_;
        // OBJEKTEN: glorian på sin plats (ObjectRenderer::updateHalo).
        if (förra.objekt >= 0)
            karta.setShape(lagren_[static_cast<std::size_t>(förra.objekt)].objekt, kHoverPlats,
                           glaze::Shape{});
        if (ny.objekt >= 0)
            karta.setShape(lagren_[static_cast<std::size_t>(ny.objekt)].objekt, kHoverPlats,
                           gloria(objekt_[static_cast<std::size_t>(ny.objekt)].linje, false));
        // VEKTORERNA: ALLA glorialager byggs om (VectorLayerRenderer::buildHalos).
        for (int l = 0; l < kVektorLager; ++l) {
            std::vector<glaze::Shape> g;
            if (ny.vektor == l) {
                const Vektor& v = vektorer_[l][static_cast<std::size_t>(ny.index)];
                g.push_back(v.pts.size() == 1
                                ? glaze::Shape::marker(v.pts.front(),
                                                       {.size = glaze::mm(4.0f),
                                                        .fill = gfx::Color{255, 255, 255, 170}})
                                : gloria(v.pts, v.sluten));
            }
            karta.setShapes(gloria_[l], std::move(g));
        }
    }

    // Kartappens musrörelse: tre strålar, ett träffprov och höjden — och
    // redigeringens drag.
    void mus(glaze::CanvasMouseArgs& e)
    {
        std::optional<glaze::Point3> mark, yta;
        std::size_t                  synligt = 0;
        if (e.ray) {
            for (const glaze::HitResult& h : karta.castRay(*e.ray))
                if (h.layer == lagerTerräng_ && h.point) {
                    mark = h.point;
                    break;
                }
            const std::vector<glaze::HitResult> täckt = karta.castRay(*e.ray, {.opaqueOnly = true});
            if (!täckt.empty())
                yta = täckt.front().point;
            synligt = karta.castRay(*e.ray, {.occlusion = true}).size();
        }
        Hover ny;
        for (const glaze::HitResult& h : karta.hitTest(e.local, glaze::HitOptions{})) {
            const Roll r = h.layer >= 0 && h.layer < static_cast<int>(roller_.size())
                               ? roller_[static_cast<std::size_t>(h.layer)]
                               : Roll{};
            if (r.slag == Roll::Objekt) {
                ny.objekt = r.id;
                break;
            }
            if (r.slag == Roll::Vektor) {
                ny.vektor = r.id;
                ny.index  = h.index;
                break;
            }
        }
        const std::optional<double> höjd =
            e.world ? karta.heightAt(*e.world) : std::optional<double>{};
        char t[96];
        if (e.world) {
            std::snprintf(t, sizeof t, "N %.2f", e.world->y);
            sättText(statusBar, 1, t);
            std::snprintf(t, sizeof t, "E %.2f", e.world->x);
            sättText(statusBar, 2, t);
        }
        std::snprintf(t, sizeof t, höjd ? "Höjd %.1f m" : "Höjd —", höjd.value_or(0.0));
        sättText(statusBar, 3, t);
        std::snprintf(t, sizeof t, yta ? "%zu träffar · yta %.1f m" : "%zu träffar", synligt,
                      yta ? yta->z : 0.0);
        sättText(statusBar, 0, t);

        if (redigerar_ >= 0) {
            if (e.button == gfx::MouseButton::Left && mark) {
                objekt_[static_cast<std::size_t>(redigerar_)].linje[static_cast<std::size_t>(punkt_)] =
                    {mark->x, mark->y};
                karta.invalidate();   // onPaint ritar bandet ur punkten
            }
            return;
        }
        hovra(ny);
    }

    // ---------------------------------------------------------- redigeringen
    // Ett vänsterdrag i punkten närmast ytans mitt, i en slinga — 40 bilder,
    // eller 30 s när bilderna är långsamma (24 000 höjduppslag var).
    bool redigera(int i)
    {
        if (i == 0) {
            redigeringStart_ = Klocka::now();
            börjaRedigera();
            if (redigerar_ < 0) {
                fel("redigeringen hittade ingen ledningspunkt på skärmen");
                return true;
            }
            injicera(karta, gfx::EventType::MouseButtonDown, dragX_, dragY_,
                     gfx::MouseButton::Left);
            return false;
        }
        const double x = dragX_ + 40.0 * std::sin(i * 0.07), y = dragY_ + 25.0 * std::sin(i * 0.11);
        if (i < kRedigeringTick && (i < 10 || msSedan(redigeringStart_) < kRedigeringTak * 1000.0)) {
            injicera(karta, gfx::EventType::MouseMotion, x, y);
            return false;
        }
        injicera(karta, gfx::EventType::MouseButtonUp, x, y, gfx::MouseButton::Left);
        const int k = redigerar_;
        redigerar_  = -1;
        byggObjektet(k);
        karta.invalidate();
        return true;
    }

    void börjaRedigera()
    {
        hovra(Hover{});
        const gfx::Sizef y    = ytansStorlek(karta);
        double           bäst = 1e30;
        for (std::size_t k = 0; k < objekt_.size(); ++k)
            for (std::size_t v = 0; v < objekt_[k].linje.size(); ++v) {
                const glaze::Point                p  = objekt_[k].linje[v];
                const std::optional<glaze::Point> lp = karta.project({p.x, p.y, markhöjd(p.x, p.y)});
                if (!lp || lp->x < 60 || lp->x > y.w - 60 || lp->y < 60 || lp->y > y.h - 60)
                    continue;
                const double d = std::hypot(lp->x - y.w / 2, lp->y - y.h / 2);
                if (d < bäst) {
                    bäst       = d;
                    redigerar_ = static_cast<int>(k);
                    punkt_     = static_cast<int>(v);
                    dragX_     = lp->x;
                    dragY_     = lp->y;
                }
            }
        if (redigerar_ >= 0)
            rensaObjektet(redigerar_);
    }

    // EDITORNS BAND: appens egen triangulering, lagd på marken med
    // kRedigeringHöjder `heightAt` per bild.
    void måla(glaze::Painter& p)
    {
        if (redigerar_ < 0)
            return;
        const std::vector<glaze::Point>& l = objekt_[static_cast<std::size_t>(redigerar_)].linje;
        std::vector<double>              s{0.0};
        for (std::size_t i = 0; i + 1 < l.size(); ++i)
            s.push_back(s.back() + std::hypot(l[i + 1].x - l[i].x, l[i + 1].y - l[i].y));
        const int                  stationer = kRedigeringHöjder / kTvärs;
        std::vector<glaze::Point3> rad(static_cast<std::size_t>(kRedigeringHöjder));
        std::size_t                seg = 0;
        for (int st = 0; st < stationer; ++st) {
            const double d = s.back() * st / (stationer - 1);
            while (seg + 2 < l.size() && s[seg + 1] < d)
                ++seg;
            const glaze::Point a = l[seg], b = l[seg + 1];
            const double       len = std::max(s[seg + 1] - s[seg], 1e-9);
            const double       t   = std::clamp((d - s[seg]) / len, 0.0, 1.0);
            const double       nx = -(b.y - a.y) / len, ny = (b.x - a.x) / len;
            for (int k = 0; k < kTvärs; ++k) {
                const double       o = -kBandHalv + 2.0 * kBandHalv * k / (kTvärs - 1);
                const glaze::Point q{a.x + (b.x - a.x) * t + nx * o, a.y + (b.y - a.y) * t + ny * o};
                rad[static_cast<std::size_t>(st * kTvärs + k)] = {
                    q.x, q.y, karta.heightAt(q).value_or(0.0) + 0.3};
            }
        }
        std::vector<glaze::Point3> hörn;
        hörn.reserve(static_cast<std::size_t>((stationer - 1) * (kTvärs - 1) * 6));
        const auto vid = [&rad](int a, int b) { return rad[static_cast<std::size_t>(a * kTvärs + b)]; };
        for (int st = 0; st + 1 < stationer; ++st)
            for (int k = 0; k + 1 < kTvärs; ++k)
                hörn.insert(hörn.end(), {vid(st, k), vid(st + 1, k), vid(st, k + 1),
                                         vid(st, k + 1), vid(st + 1, k), vid(st + 1, k + 1)});
        p.drawTriangles3(hörn, {.color = gfx::Color{255, 140, 0, 110}, .lit = false, .fog = false});
        std::vector<glaze::Point3> linje;
        for (const glaze::Point& q : l)
            linje.push_back({q.x, q.y, markhöjd(q.x, q.y) + 0.5});
        p.drawPolyline3(linje, {.paint = gfx::Color{255, 120, 0, 255}, .width = glaze::mm(0.8f)});
    }

    // SKALSTOCKEN: markens skala där stocken står — `toWorld` i
    // stockens ändar och höjden under den — och norrpilen.
    void överlägg(glaze::Painter& p)
    {
        const gfx::Sizef   s = p.size();
        const glaze::Point a{24.0, s.h - 32.0}, b{144.0, s.h - 32.0};
        const auto         wa = karta.toWorld(a), wb = karta.toWorld(b);
        char               t[64] = "—";
        if (wa && wb)
            std::snprintf(t, sizeof t, "%.0f m · %.0f m ö.h.",
                          std::hypot(wb->x - wa->x, wb->y - wa->y),
                          karta.heightAt(*wa).value_or(0.0));
        p.fillRect({a.x, a.y, 120, 5}, gfx::Color{0, 0, 0, 200});
        glaze::TextStyle ts;
        ts.size    = glaze::mm(2.5f);
        ts.anchor  = glaze::TextAnchor::BottomLeft;
        ts.halo    = gfx::Color{255, 255, 255, 220};
        ts.collide = false;
        p.drawText({a.x, a.y - 4.0}, std::string(t), ts);
        const double       v = -karta.camera().bearing * kPi / 180.0;
        const glaze::Point c{s.w - 40.0, 48.0};
        p.fillPolygon({{c.x + 18.0 * std::sin(v), c.y - 18.0 * std::cos(v)},
                       {c.x - 7.0 * std::cos(v), c.y - 7.0 * std::sin(v)},
                       {c.x + 7.0 * std::cos(v), c.y + 7.0 * std::sin(v)}},
                      gfx::Color{20, 20, 20, 255});
    }

    // ---------------------------------------------------------- RÖRELSERNA
    // TILTGLIDNINGEN (frågas var 50 ms): fem glidningar — in från platt i
    // 1:3800 (kartan tiltas där, regeln bryts), ned till platt, och in igen.
    // Sist står kartan i 1:1000 och 30°.
    bool glida(int i)
    {
        if (i == 0)
            glidningar_ = 0;
        if (glider_)
            return false;
        if (glidningar_ == 5)
            return true;
        if (glidningar_ % 2 == 0) {
            // Platt och utzoomad — sedan tiltar "användaren" fem grader, och
            // regeln låter kartan glida in (onCameraChange).
            glaze::Camera c = karta.camera();
            c.zoom          = kStartZoom;
            c.pitch         = 0.0f;
            sättKamera(c);
            c.pitch = 5.0f;
            karta.setCamera(c);
            glid(true);
        } else {
            glid(false);
        }
        ++glidningar_;
        return false;
    }

    // TILTEN FRAM OCH TILLBAKA: Ctrl+mittendrag uppåt till taket och nedåt till
    // 5°, tills tilten vänt fem gånger (och står i taket).
    bool tilta(int i)
    {
        const std::uint32_t ctrl = gfx::kModCtrl;
        if (i == 0) {
            const gfx::Sizef y = ytansStorlek(karta);
            dragX_             = y.w / 2;
            dragY_             = y.h * 0.6;
            tiltRiktning_      = -1;
            vändningar_        = 0;
            injicera(karta, gfx::EventType::MouseButtonDown, dragX_, dragY_,
                     gfx::MouseButton::Middle, 0.0f, ctrl);
            return false;
        }
        const float p = karta.camera().pitch;
        if (tiltRiktning_ < 0 && p >= karta.maxPitch() - 0.5f) {
            tiltRiktning_ = 1;
            ++vändningar_;
        } else if (tiltRiktning_ > 0 && p <= 5.5f) {
            tiltRiktning_ = -1;
            ++vändningar_;
        }
        if (vändningar_ >= 5 || i > 600) {
            injicera(karta, gfx::EventType::MouseButtonUp, dragX_, dragY_,
                     gfx::MouseButton::Middle, 0.0f, ctrl);
            return true;
        }
        dragY_ += 4.0 * tiltRiktning_;   // uppåt = mer tilt, 2° per tick
        injicera(karta, gfx::EventType::MouseMotion, dragX_, dragY_, gfx::MouseButton::None, 0.0f,
                 ctrl);
        return false;
    }

    // VRIDNINGEN: Ctrl+mittendrag fram och tillbaka i sidled — bäringen
    // ±160°, två varv på 250 tickar.
    bool vrida(int i)
    {
        const std::uint32_t ctrl = gfx::kModCtrl;
        const gfx::Sizef    y    = ytansStorlek(karta);
        const double        x    = y.w / 2 + 200.0 * std::sin(i * 0.05);
        if (i == 0) {
            injicera(karta, gfx::EventType::MouseButtonDown, x, y.h * 0.6,
                     gfx::MouseButton::Middle, 0.0f, ctrl);
            return false;
        }
        if (i >= 250) {
            injicera(karta, gfx::EventType::MouseButtonUp, x, y.h * 0.6,
                     gfx::MouseButton::Middle, 0.0f, ctrl);
            return true;
        }
        injicera(karta, gfx::EventType::MouseMotion, x, y.h * 0.6, gfx::MouseButton::None, 0.0f,
                 ctrl);
        return false;
    }

    // PANORERINGSDRAGEN som ger `meter` m på marken — ett drag är 880 logiska
    // px vågrätt genom ytans mitt.
    int panoreringsdrag(double meter) const
    {
        const gfx::Sizef            y   = ytansStorlek(karta);
        const std::optional<double> upp = karta.unitsPerPixelAt({y.w / 2.0, y.h / 2.0});
        return static_cast<int>(std::ceil(meter / (880.0 * upp.value_or(1.0))));
    }

    // HOPPET: kameran till mitten av det objekt som ligger längst bort inom
    // 6 km — dit där ingenting är laddat, och där det finns objekt att hovra
    // och redigera. Det nya landar sedan medan kameran står still.
    void hoppaTillObjekt()
    {
        glaze::Camera      k = karta.camera();
        const glaze::Point c = k.center;
        double             längst = -1.0;
        for (const Objekt& o : objekt_) {
            const glaze::Point m = o.linje[o.linje.size() / 2];
            const double       d = std::hypot(m.x - c.x, m.y - c.y);
            if (d > längst && d < 6000.0) {
                längst   = d;
                k.center = m;
            }
        }
        sättKamera(k);
    }

    // ALLT SAMTIDIGT på kartan: ett Ctrl+mittendrag i en slinga (bäringen fram
    // och tillbaka, tilten ned och upp igen) i 90 tickar, och sedan ett
    // panoreringsdrag i 45 — om och om igen. Svarar sant mellan två varv,
    // utan en knapp nere, när `klart`.
    bool alltSamtidigt(int i, bool klart)
    {
        constexpr int       kSlinga = 90, kVarv = kSlinga + kTickPerDrag;
        const std::uint32_t ctrl    = gfx::kModCtrl;
        const int           k       = i % kVarv;
        if (k == 0 && klart)
            return true;
        const gfx::Sizef y = ytansStorlek(karta);
        if (k < kSlinga) {
            const double v  = 2.0 * kPi * k / (kSlinga - 1);
            const double x  = y.w / 2 + 150.0 * std::sin(v);
            const double yy = y.h * 0.6 + 20.0 * (1.0 - std::cos(v));
            injicera(karta,
                     k == 0             ? gfx::EventType::MouseButtonDown
                     : k == kSlinga - 1 ? gfx::EventType::MouseButtonUp
                                        : gfx::EventType::MouseMotion,
                     x, yy, k == 0 || k == kSlinga - 1 ? gfx::MouseButton::Middle
                                                       : gfx::MouseButton::None,
                     0.0f, ctrl);
            return false;
        }
        panorera(*this, karta, k - kSlinga, 1, -12.0, -6.0);
        return false;
    }

    // ---------------------------------------------------------------- tick
    void tick(double sek);   // efter sessionen

    // Är allt som syns inne och köerna tomma?
    bool ikapp() const
    {
        if (!påvägBrickor_.empty() || baskartaÄndrad_ || redigerar_ >= 0 || glider_ ||
            brickorInne_ != static_cast<int>(brickor_.size()))
            return false;
        if (!terrängAktiv_)
            return true;
        if (!påvägPlattor_.empty() || visadExp_ != plattExp_)
            return false;
        for (const Platta& p : önskadePlattor_) {
            const auto it = plattPlats_.find(p.nyckel);
            if (it == plattPlats_.end() || !it->second.byggd)
                return false;
        }
        return true;
    }
    std::size_t köDjup() const { return påvägPlattor_.size() + påvägBrickor_.size(); }
    int         hoverByten() const { return hoverByten_; }
    void        status();

private:
    struct ObjektLager {
        int band = 0, objekt = 0, efter = 0, rymd = 0;
    };
    struct Vektor {
        std::vector<glaze::Point> pts;
        bool                      sluten = false;
    };
    struct CachadPlatta {
        glaze::Mesh   mesh;
        std::uint64_t använd = 0;
    };
    struct PlattPlats {
        int  plats = -1;
        bool byggd = false;
    };
    struct CachadBricka {
        glaze::Bitmap                      bild;
        std::list<std::uint64_t>::iterator var;
    };

    // VEKTORLAGREN: fastigheter, vägar, diken, byggnader och brunnar inom
    // 3 km — en form per objekt, i ritordning (formens index är objektets).
    void byggVektorer()
    {
        Slump r{4242};
        for (int l = 0; l < kVektorLager; ++l) {
            std::vector<glaze::Shape> former;
            const gfx::Color          färg{static_cast<std::uint8_t>(60 + 35 * l),
                                  static_cast<std::uint8_t>(150 - 20 * l), 90, 255};
            for (int f = 0; f < kVektorFormer; ++f) {
                Vektor       v;
                const double cx = kX0 + r.mellan(-3000.0, 3000.0), cy = kY0 + r.mellan(-3000.0, 3000.0);
                if (l == 0 || l == 3) {   // fastigheter och byggnader: slutna ringar
                    const int n = l == 0 ? 6 + static_cast<int>(r.nästa() % 5u) : 4;
                    for (int k = 0; k < n; ++k) {
                        const double vk = 2 * kPi * k / n + (l == 3 ? kPi / 4 : 0.0);
                        const double rr = l == 0 ? r.mellan(30.0, 55.0) : 9.0;
                        v.pts.push_back({cx + std::cos(vk) * rr, cy + std::sin(vk) * rr});
                    }
                    v.sluten = true;
                } else if (l == 4) {      // brunnar: punkter
                    v.pts.push_back({cx, cy});
                } else {                  // vägar och diken: polylinjer
                    double    kurs = r.mellan(0.0, 360.0), x = cx, y = cy;
                    const int n    = l == 1 ? 20 + static_cast<int>(r.nästa() % 21u)
                                            : 10 + static_cast<int>(r.nästa() % 11u);
                    for (int k = 0; k < n; ++k) {
                        v.pts.push_back({x, y});
                        kurs += r.mellan(-8.0, 8.0);
                        x += (l == 1 ? 25.0 : 15.0) * std::sin(kurs * kPi / 180.0);
                        y += (l == 1 ? 25.0 : 15.0) * std::cos(kurs * kPi / 180.0);
                    }
                }
                if (v.pts.size() == 1) {
                    former.push_back(glaze::Shape::marker(
                        v.pts.front(), {.shape = glaze::MarkerShape::Square,
                                        .size  = glaze::px(6.0f),
                                        .fill  = färg}));
                } else {
                    std::vector<glaze::Point> pts = v.pts;
                    if (v.sluten)
                        pts.push_back(pts.front());
                    former.push_back(glaze::Shape::polyline(
                        std::move(pts), {.paint = färg, .width = glaze::px(1.5f)}));
                }
                vektorer_[l].push_back(std::move(v));
            }
            karta.setShapes(vektor_[l], std::move(former));
        }
    }

    Arbetare&                arbete_;
    std::vector<Objekt>      objekt_;   // kartans kopia — redigeringen och ändringarna rör den
    glaze::Mesh              stolpe_[2];
    int                      lagerTerräng_ = 0, lagerKarta_ = 0, lagerMarkör_ = 0, lagerProfil_ = 0;
    int                      gloria_[kVektorLager]{}, vektor_[kVektorLager]{};
    std::vector<Vektor>      vektorer_[kVektorLager];
    std::vector<ObjektLager> lagren_;
    std::vector<Roll>        roller_;   // per lager
    glaze::Camera            förraKam_{.zoom = -1.0};
    gfx::Sizef               förraYta_{};
    // Tiltregelns glidning.
    glaze::Timer glidTimer_;
    bool         glider_     = false;
    int          glidningar_ = 0;
    // Terrängen.
    Platser                                          terräng_;
    bool                                             terrängAktiv_ = false, terrängSyns_ = false;
    int                                              plattExp_ = -1, visadExp_ = -1;
    std::vector<Platta>                              önskadePlattor_;
    std::unordered_map<std::uint64_t, CachadPlatta> plattCache_;
    std::unordered_map<std::uint64_t, PlattPlats>   plattPlats_;
    std::unordered_set<std::uint64_t>               påvägPlattor_;
    std::uint64_t                                    klocka_ = 0;
    // Baskartan.
    glaze::Timer                                      batch_;
    int                                               källa_ = 0;
    std::vector<Bricka>                               brickor_;
    std::unordered_set<std::uint64_t>                 brickSet_;
    std::unordered_map<std::uint64_t, CachadBricka> brickCache_;
    std::list<std::uint64_t>                          brickLru_;
    std::unordered_set<std::uint64_t>                 påvägBrickor_;
    bool                                              baskartaÄndrad_ = false;
    int                                               brickorInne_    = 0;
    // 3D-appens former: stolpen som dras i profilvyn och profilens hover.
    int    dragObjekt_ = -1, dragStolpe_ = -1;
    double dragLyft_   = 0.0;
    double profilPos_  = -1.0;
    // Hovret, redigeringen och dragen.
    Hover  hover_;
    int    hoverByten_ = 0;
    int                redigerar_ = -1, punkt_ = -1;
    Klocka::time_point redigeringStart_{};
    double             dragX_ = 0.0, dragY_ = 0.0;
    int    tiltRiktning_ = -1, vändningar_ = 0;
};

KartFönster* KartFönster::g_karta = nullptr;

// ================================================================= 3D-VYN

constexpr int    kRuta3Hörn   = 33;      // terrängrutans hörn per sida
constexpr double kRuta3       = 40.0;    // m — zoom 19
constexpr int    kHalv3       = 5;       // 11 × 11 rutor (rutnät 10)
constexpr int    kRutor3      = (2 * kHalv3 + 1) * (2 * kHalv3 + 1);
constexpr double kSikt3       = (kHalv3 + 0.5) * kRuta3;   // dimmans slut, m
constexpr int    kMolnTak3    = 50000;   // punkter per ruta
constexpr double kKorridor3   = 60.0;    // LAS-täckningen kring ledningarna, m
constexpr int    kTrädPerRuta = 80;
constexpr double kÖgonhöjd    = 2.0;     // m över marken
constexpr float  kLutning     = -4.0f;   // grader, blicken något nedåt
constexpr double kMusblick    = 0.2;     // grader per logisk px (mittenknappen)
// Stolparnas detaljnivåer: trianglar och största avstånd.
constexpr int    kStolpTrianglar[3] = {60000, 15000, 4000};
constexpr double kStolpAvstånd[3]   = {60.0, 140.0, kSikt3};

// Himlens färger: horisonten är också dimmans färg.
constexpr float kHorisont[3] = {0.5f, 0.7f, 1.0f};
constexpr float kZenit[3]    = {0.24f, 0.44f, 0.86f};

// Molnets klasser (LAS) och deras färger: mark, låg, medel och hög
// vegetation, och linorna.
gfx::Color klassFärg(std::uint8_t k)
{
    switch (k) {
    case 2:  return {140, 115, 90, 255};
    case 3:  return {102, 178, 77, 255};
    case 4:  return {77, 153, 64, 255};
    case 5:  return {51, 128, 51, 255};
    case 14: return {255, 0, 0, 255};
    default: return {217, 217, 217, 255};
    }
}

// HIMLEN — ett eget program som helskärmspass: gradient från horisonten till
// zenit, solen och moln ur fbm i fem oktaver. Strålen genom pixeln ur
// GlazeFrame:s bas (gfx/program.h). GlazeUser: horisonten, zenit och
// (solriktningen, molnmängden). Bara HLSL är prövad här; GLSL och MSL är
// följer samma mall.
glaze::Program himmel()
{
    glaze::Program p;
    p.name = "himmel";
    p.hlsl = R"(
cbuffer GlazeUser : register(b4) { float4 uHorizon; float4 uZenith; float4 uSun; };
struct VsOut { float4 pos : SV_Position; float2 ndc : TEXCOORD0; };
VsOut vsMain(uint id : SV_VertexID)
{
    VsOut  o;
    float2 q = float2(float((id << 1) & 2), float(id & 2));
    o.ndc = q * float2(2.0, -2.0) + float2(-1.0, 1.0);
    o.pos = float4(o.ndc, 0.0, 1.0);
    return o;
}
float hash(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
float noise(float2 p)
{
    float2 c = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hash(c), hash(c + float2(1.0, 0.0)), f.x),
                lerp(hash(c + float2(0.0, 1.0)), hash(c + float2(1.0, 1.0)), f.x), f.y);
}
float fbm(float2 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int k = 0; k < 5; ++k) {
        v += a * noise(p);
        p *= 2.03;
        a *= 0.5;
    }
    return v;
}
float4 fsMain(VsOut i) : SV_Target
{
    float3 dir = normalize(glaze.rayCenter.xyz + i.ndc.x * glaze.right.xyz + i.ndc.y * glaze.up.xyz);
    float3 col = lerp(uHorizon.rgb, uZenith.rgb, pow(saturate(dir.z), 0.55));
    float  sun = max(dot(dir, normalize(uSun.xyz)), 0.0);
    col += float3(1.0, 0.94, 0.82) * (pow(sun, 400.0) * 0.5 + pow(sun, 12.0) * 0.1);
    if (dir.z > 0.005) {
        float n     = fbm(dir.xy / max(dir.z, 0.06) * 0.6);
        float cloud = smoothstep(0.5, 0.72, n) * smoothstep(0.02, 0.3, dir.z) * uSun.w;
        col = lerp(col, float3(0.92, 0.93, 0.96), cloud);
    }
    return float4(col, glaze.scale.z);
}
)";
    p.glslVertex = R"(
out vec2 vNdc;
void main()
{
    vec2 q = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vNdc        = q * vec2(2.0, -2.0) + vec2(-1.0, 1.0);
    gl_Position = vec4(vNdc, 0.0, 1.0);
}
)";
    p.glslFragment = R"(
layout(std140) uniform GlazeUser { vec4 uHorizon; vec4 uZenith; vec4 uSun; };
in vec2 vNdc;
out vec4 frag;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p)
{
    vec2 c = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(c), hash(c + vec2(1.0, 0.0)), f.x),
               mix(hash(c + vec2(0.0, 1.0)), hash(c + vec2(1.0, 1.0)), f.x), f.y);
}
float fbm(vec2 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int k = 0; k < 5; ++k) {
        v += a * noise(p);
        p *= 2.03;
        a *= 0.5;
    }
    return v;
}
void main()
{
    vec3  dir = normalize(glaze.rayCenter.xyz + vNdc.x * glaze.right.xyz + vNdc.y * glaze.up.xyz);
    vec3  col = mix(uHorizon.rgb, uZenith.rgb, pow(clamp(dir.z, 0.0, 1.0), 0.55));
    float sun = max(dot(dir, normalize(uSun.xyz)), 0.0);
    col += vec3(1.0, 0.94, 0.82) * (pow(sun, 400.0) * 0.5 + pow(sun, 12.0) * 0.1);
    if (dir.z > 0.005) {
        float n     = fbm(dir.xy / max(dir.z, 0.06) * 0.6);
        float cloud = smoothstep(0.5, 0.72, n) * smoothstep(0.02, 0.3, dir.z) * uSun.w;
        col = mix(col, vec3(0.92, 0.93, 0.96), cloud);
    }
    frag = vec4(col, glaze.scale.z);
}
)";
    p.msl = R"(
struct User  { float4 horizon; float4 zenith; float4 sun; };
struct VsOut { float4 pos [[position]]; float2 ndc; };
vertex VsOut vsMain(uint id [[vertex_id]])
{
    VsOut  o;
    float2 q = float2(float((id << 1) & 2), float(id & 2));
    o.ndc = q * float2(2.0, -2.0) + float2(-1.0, 1.0);
    o.pos = float4(o.ndc, 0.0, 1.0);
    return o;
}
float hash(float2 p) { return fract(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
float noise(float2 p)
{
    float2 c = floor(p);
    float2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(c), hash(c + float2(1.0, 0.0)), f.x),
               mix(hash(c + float2(0.0, 1.0)), hash(c + float2(1.0, 1.0)), f.x), f.y);
}
float fbm(float2 p)
{
    float v = 0.0;
    float a = 0.5;
    for (int k = 0; k < 5; ++k) {
        v += a * noise(p);
        p *= 2.03;
        a *= 0.5;
    }
    return v;
}
fragment float4 fsMain(VsOut i [[stage_in]],
                       constant GlazeFrame& glaze [[buffer(2)]],
                       constant User& user [[buffer(3)]])
{
    float3 dir = normalize(glaze.rayCenter.xyz + i.ndc.x * glaze.right.xyz + i.ndc.y * glaze.up.xyz);
    float3 col = mix(user.horizon.rgb, user.zenith.rgb, pow(saturate(dir.z), 0.55));
    float  sun = max(dot(dir, normalize(user.sun.xyz)), 0.0);
    col += float3(1.0, 0.94, 0.82) * (pow(sun, 400.0) * 0.5 + pow(sun, 12.0) * 0.1);
    if (dir.z > 0.005) {
        float n     = fbm(dir.xy / max(dir.z, 0.06) * 0.6);
        float cloud = smoothstep(0.5, 0.72, n) * smoothstep(0.02, 0.3, dir.z) * user.sun.w;
        col = mix(col, float3(0.92, 0.93, 0.96), cloud);
    }
    return float4(col, glaze.scale.z);
}
)";
    p.uniformFloats = 12;
    return p;
}

// VÄRDENS PUNKTMOLN för en ruta, före UI-trådens höjduppslag: lägena
// relativt rutans hörn (höjden absolut) och klassen.
struct RåMoln {
    double                     x0 = 0.0, y0 = 0.0;
    std::vector<glaze::Float3> läge;
    std::vector<std::uint8_t>  klass;
};

// Molnet och marken som former — byggda i en arbetstråd.
struct ByggtMoln {
    glaze::PointCloud moln, mark;
};

// ARBETSTRÅDEN: rutans punkter i ledningsgatan — linorna nära mitten, marken
// och vegetationen i tre höjder — högst kMolnTak3.
RåMoln byggRåMoln(std::int64_t i, std::int64_t j)
{
    RåMoln m;
    m.x0                           = kRutnätX + i * kRuta3;
    m.y0                           = kRutnätY + j * kRuta3;
    const std::vector<Segment> seg = segmentNära(m.x0, m.y0, m.x0 + kRuta3, m.y0 + kRuta3, kKorridor3);
    if (seg.empty())
        return m;   // utanför LAS-täckningen
    Slump r{blanda(nyckel(Jobb::Moln, 0, i, j))};
    for (int k = 0; k < 2 * kMolnTak3 && static_cast<int>(m.läge.size()) < kMolnTak3; ++k) {
        const double x = r.mellan(0.0, kRuta3), y = r.mellan(0.0, kRuta3);
        const double d = avstånd(seg, m.x0 + x, m.y0 + y);
        if (d > kKorridor3)
            continue;
        const double z = markhöjd(m.x0 + x, m.y0 + y), u = r.enhet();
        std::uint8_t c = 5;
        double       h = r.mellan(5.0, 20.0);
        if (d < 1.5 && u < 0.10) {
            c = 14;
            h = r.mellan(18.0, 22.0);
        } else if (u < 0.35) {
            c = 2;
            h = r.mellan(-0.05, 0.05);
        } else if (u < 0.60) {
            c = 3;
            h = r.mellan(0.2, 1.0);
        } else if (u < 0.85 || d < 15.0) {
            c = 4;
            h = r.mellan(1.0, 5.0);
        }
        m.läge.push_back({static_cast<float>(x), static_cast<float>(y), static_cast<float>(z + h)});
        m.klass.push_back(c);
    }
    return m;
}

// ARBETSTRÅDEN: molnet och marken ur kopian, med höjden över mark (rampens
// skalär) bilinjärt ur UI-trådens rutnät om 5 × 5 höjder.
ByggtMoln byggMoln(const RåMoln& m, const std::array<float, 25>& mark)
{
    const auto markVid = [&mark](double x, double y) {
        const double u  = std::clamp(x / kRuta3 * 4.0, 0.0, 4.0);
        const double v  = std::clamp(y / kRuta3 * 4.0, 0.0, 4.0);
        const int    i0 = std::min(static_cast<int>(u), 3), j0 = std::min(static_cast<int>(v), 3);
        const double fu = u - i0, fv = v - j0;
        const auto   z  = [&mark](int i, int j) {
            return static_cast<double>(mark[static_cast<std::size_t>(j * 5 + i)]);
        };
        return (z(i0, j0) * (1 - fu) + z(i0 + 1, j0) * fu) * (1 - fv) +
               (z(i0, j0 + 1) * (1 - fu) + z(i0 + 1, j0 + 1) * fu) * fv;
    };
    std::vector<glaze::Float3> läge[2];
    std::vector<gfx::Color>    färg[2];
    std::vector<float>         hag[2];
    for (std::size_t k = 0; k < m.läge.size(); ++k) {
        const std::size_t   s = m.klass[k] == 2 ? 1 : 0;
        const glaze::Float3 p = m.läge[k];
        läge[s].push_back(p);
        färg[s].push_back(klassFärg(m.klass[k]));
        hag[s].push_back(std::max(0.0f, p.z - static_cast<float>(markVid(p.x, p.y))));
    }
    ByggtMoln b;
    if (!läge[0].empty())
        b.moln = glaze::PointCloud(std::move(läge[0]), {m.x0, m.y0, 0.0}, std::move(färg[0]),
                                   std::move(hag[0]));
    if (!läge[1].empty())
        b.mark = glaze::PointCloud(std::move(läge[1]), {m.x0, m.y0, 0.0}, std::move(färg[1]),
                                   std::move(hag[1]));
    return b;
}

// ARBETSTRÅDEN: rutans träd — skog utom i ledningsgatan.
std::vector<glaze::Instance> byggTräd(std::int64_t i, std::int64_t j)
{
    const double                 x0 = kRutnätX + i * kRuta3, y0 = kRutnätY + j * kRuta3;
    const std::vector<Segment>   seg = segmentNära(x0, y0, x0 + kRuta3, y0 + kRuta3, 25.0);
    Slump                        r{blanda(nyckel(Jobb::Träd, 0, i, j))};
    std::vector<glaze::Instance> ut;
    for (int k = 0; k < kTrädPerRuta; ++k) {
        const double x = x0 + r.mellan(0.0, kRuta3), y = y0 + r.mellan(0.0, kRuta3);
        if (!seg.empty() && avstånd(seg, x, y) < 25.0)
            continue;
        ut.push_back({.at    = {x, y, markhöjd(x, y)},
                      .yaw   = static_cast<float>(r.mellan(0.0, 360.0)),
                      .scale = {static_cast<float>(r.mellan(0.7, 1.3)),
                                static_cast<float>(r.mellan(0.7, 1.3)),
                                static_cast<float>(r.mellan(1.0, 2.6))},
                      .color = gfx::Color{static_cast<std::uint8_t>(r.mellan(40, 90)),
                                          static_cast<std::uint8_t>(r.mellan(110, 170)), 50,
                                          255}});
    }
    return ut;
}

class TreDFönster : public Fönster {
public:
    explicit TreDFönster(Arbetare& arbete) : arbete_(arbete)
    {
        setTitle("terrain_perf — 3D-vy");
        vy.name = "vy";
        vy.dock = glaze::Dock::Fill;
        vy.setBorder(false);
        root().adopt(vy);
        statusBar.show();
        for (int f = 0; f < 9; ++f)
            statusBar.addPanel("", f == 3 ? 0.0f : 120.0f, f == 3);
        setContentSize(1280, 800);
        if (!g_synlig)
            setOffscreen(true);
    }

    glaze::Canvas vy;
    double        fart           = 100.0;   // km/h
    bool          poseTillKartan = false;   // 3D-vyns pose till kartan

    static TreDFönster* g_tred;

    // Nio lager, himlen och programmet,
    // platt yta, 8× MSAA, djup och dimma.
    void byggScen()
    {
        const auto t0 = Klocka::now();
        vy.setLayers({"Himmel", "Terräng", "Punktmoln", "Mark", "Stolpar", "Stolpar (glas)",
                      "Linor", "Träd", "Markerade punkter"});
        vy.setLayerPlacement(kHimmel, glaze::LayerPlacement::World);
        vy.setLayerPlacement(kTerräng, glaze::LayerPlacement::Terrain);
        for (int l : {kMoln, kMark, kStolpar, kGlas, kLinor, kTräd})
            vy.setLayerPlacement(l, glaze::LayerPlacement::World);
        vy.setLayerPlacement(kMarkerade, glaze::LayerPlacement::Ground);
        vy.addProgram(himmel());
        const double sol[3] = {0.7, -0.5, 0.5};
        const double sl     = std::sqrt(sol[0] * sol[0] + sol[1] * sol[1] + sol[2] * sol[2]);
        glaze::PassStyle pass;
        pass.uniforms = {kHorisont[0], kHorisont[1], kHorisont[2], 1.0f,
                         kZenit[0],    kZenit[1],    kZenit[2],    1.0f,
                         static_cast<float>(sol[0] / sl), static_cast<float>(sol[1] / sl),
                         static_cast<float>(sol[2] / sl), 0.45f};
        pass.fallback = horisont();
        vy.addShape(kHimmel, glaze::Shape::screenPass("himmel", std::move(pass)));
        vy.setBackground(horisont());
        vy.setFlat(true);
        vy.setMultisample(8);
        vy.setDepth(true);
        glaze::Lighting ljus;
        ljus.direction    = {-sol[0] / sl, -sol[1] / sl, -sol[2] / sl};
        ljus.ambient      = 0.45f;
        ljus.diffuse      = 0.7f;
        ljus.exponent     = 1.5f;
        ljus.rimDirection = {0.0, 1.0, 0.0};
        ljus.rim          = 0.06f;
        ljus.fog          = horisont();
        ljus.fogStart     = 0.6 * kSikt3;
        ljus.fogEnd       = kSikt3;
        ljus.fogPower     = 2.0f;
        vy.setLighting(ljus);

        terräng_.bind(vy, kTerräng);
        moln_.bind(vy, kMoln);
        mark_.bind(vy, kMark);
        stolpLager_.bind(vy, kStolpar);
        träd_.bind(vy, kTräd);
        kon_ = byggKon({{32, 40.0}, {20, 80.0}, {12, 140.0}, {8, 220.0}, {5, 400.0}, {3, 0.0}});
        // STOLPARNA: varje objekts, i objektens ordning.
        for (const Objekt& o : g_objekt) {
            objektStart_.push_back(static_cast<int>(stolpar_.size()));
            for (std::size_t i = 0; i < o.linje.size(); ++i)
                stolpar_.push_back({{o.linje[i].x, o.linje[i].y,
                                     markhöjd(o.linje[i].x, o.linje[i].y)},
                                    static_cast<float>(o.riktning[i]), o.typ});
        }
        objektStart_.push_back(static_cast<int>(stolpar_.size()));
        // MARKERADE PUNKTER längs objekt 0.
        const Objekt& o0 = g_objekt.front();
        for (std::size_t i = 0; i < o0.linje.size(); i += 15) {
            const glaze::Point p = o0.linje[i];
            vy.addShape(kMarkerade,
                        glaze::Shape::marker3({p.x + 6.0, p.y, markhöjd(p.x + 6.0, p.y) + 1.5},
                                              {.size         = glaze::mm(3.0f),
                                               .fill         = gfx::Color{255, 220, 0, 255},
                                               .outline      = gfx::Color{0, 0, 0, 255},
                                               .outlineWidth = glaze::px(1.0f)}));
        }

        vy.onPaintOverlay = [this](glaze::Widget*, glaze::PaintArgs& e) { hud(e.painter); };
        vy.onTick         = [this](glaze::Widget*, glaze::TickArgs& e) { tick(e.seconds); };
        // MUSBLICKEN: mittenknappen tas,
        // och draget girar och lutar blicken.
        vy.onMouseDown = [this](glaze::Widget*, glaze::CanvasMouseArgs& e) {
            if (e.button != gfx::MouseButton::Middle)
                return;
            musblick_  = true;
            förraMus_  = e.local;
            e.handled  = true;
        };
        vy.onMouseMove = [this](glaze::Widget*, glaze::CanvasMouseArgs& e) {
            if (!musblick_) {
                pekare(e.ray);
                return;
            }
            gir_ += (e.local.x - förraMus_.x) * kMusblick;
            lutning_ = std::clamp(lutning_ - (e.local.y - förraMus_.y) * kMusblick, -60.0, 30.0);
            förraMus_ = e.local;
            e.handled = true;
            uppdatera();
        };
        vy.onMouseUp = [this](glaze::Widget*, glaze::CanvasMouseArgs& e) {
            if (e.button == gfx::MouseButton::Middle) {
                musblick_ = false;
                e.handled = true;
            }
        };
        vy.onMouseCancel = [this](glaze::Widget*, glaze::CanvasMouseArgs&) { musblick_ = false; };
        vy.onKeyDown     = [](glaze::Widget*, glaze::KeyArgs& e) {
            switch (e.key) {
            case gfx::Key::W: case gfx::Key::A: case gfx::Key::S:
            case gfx::Key::D: case gfx::Key::Q: case gfx::Key::E:
                e.handled = true;   // tagen: ritytan tickar medan den hålls
                break;
            default:
                break;
            }
        };
        std::printf("  scen 3D-vyn: 9 lager, %zu stolpar, himlen som eget program, MSAA 8 — "
                    "byggd på %.0f ms\n",
                    stolpar_.size(), msSedan(t0));
    }

    // Kameran DIREKT dit (öppningen, ett hopp) — och allt som följer av den.
    void ställ(glaze::Point öga, double gir)
    {
        öga_ = öga;
        gir_ = gir;
        uppdatera();
    }

    // HOPPET: ögat `meter` m fram längs blicken — det nya landar medan
    // kameran står still.
    void hoppa(double meter)
    {
        const double v = gir_ * kPi / 180.0;
        ställ({öga_.x + meter * std::sin(v), öga_.y + meter * std::cos(v)}, gir_);
    }

    void tick(double sek);   // efter sessionen

    bool ikapp() const
    {
        return påväg_.empty() && stolpPåväg_.empty() &&
               rutor_.size() == static_cast<std::size_t>(kRutor3);
    }
    std::size_t köDjup() const { return påväg_.size() + stolpPåväg_.size(); }

    // ------------------------------------------------------------- svaren
    void terrängKom(std::uint64_t n, glaze::Mesh m)
    {
        påväg_.erase(n);
        Ruta* r = ruta(n);
        if (!r)
            return;
        r->mesh              = std::move(m);
        const glaze::Shape s = terrängForm(*r);
        if (r->terräng < 0)
            r->terräng = terräng_.lägg(s);
        else
            terräng_.sätt(r->terräng, s);
        räkna(kStrömRutor, true);
    }

    void fotoKom(std::uint64_t n, glaze::Bitmap b)
    {
        påväg_.erase(n);
        Ruta* r = ruta(n);
        if (!r)
            return;
        r->foto = std::move(b);
        if (r->terräng >= 0)
            terräng_.sätt(r->terräng, terrängForm(*r));   // ortofotot via `setShape`
    }

    // Molnet KOPIERAS i UI-tråden och får sitt markrutnät ur 25 synkrona
    // höjduppslag; bygget går sedan i en arbetstråd.
    void molnKom(std::uint64_t n, std::shared_ptr<const RåMoln> rå)
    {
        påväg_.erase(n);
        if (!ruta(n) || rå->läge.empty())
            return;
        auto                  kopia = std::make_shared<const RåMoln>(*rå);
        std::array<float, 25> mark{};
        std::array<bool, 25>  finns{};
        double                summa   = 0.0;
        int                   giltiga = 0;
        for (int j = 0; j < 5; ++j)
            for (int i = 0; i < 5; ++i) {
                const std::optional<double> z =
                    vy.heightAt({rå->x0 + kRuta3 * i / 4.0, rå->y0 + kRuta3 * j / 4.0});
                const auto k = static_cast<std::size_t>(j * 5 + i);
                if (z) {
                    mark[k]  = static_cast<float>(*z);
                    finns[k] = true;
                    summa += *z;
                    ++giltiga;
                }
            }
        // Ingen terräng än: medelhöjden, eller molnets lägsta punkt.
        float fyll = std::numeric_limits<float>::max();
        if (giltiga > 0)
            fyll = static_cast<float>(summa / giltiga);
        else
            for (const glaze::Float3& p : kopia->läge)
                fyll = std::min(fyll, p.z);
        for (std::size_t k = 0; k < mark.size(); ++k)
            if (!finns[k])
                mark[k] = fyll;
        const std::uint64_t b = nyckel(Jobb::MolnBygg, 0, indexI(n), indexJ(n));
        påväg_.insert(b);
        arbete_.lägg(b, [b, kopia, mark] {
            auto byggt = std::make_shared<ByggtMoln>(byggMoln(*kopia, mark));
            postaTillUi([b, byggt] { g_tred->molnByggt(b, byggt); });
        });
    }

    void molnByggt(std::uint64_t n, std::shared_ptr<ByggtMoln> m)
    {
        påväg_.erase(n);
        Ruta* r = ruta(n);
        if (!r)
            return;
        const glaze::PointStyle stil{.size = glaze::mm(0.8f), .color = gfx::Color{255, 255, 255, 255}};
        if (m->moln.valid())
            r->moln = moln_.lägg(glaze::Shape::pointCloud(m->moln, stil));
        if (m->mark.valid())
            r->mark = mark_.lägg(glaze::Shape::pointCloud(m->mark, stil));
        räkna(kStrömMoln, true);
    }

    void trädKom(std::uint64_t n, std::shared_ptr<std::vector<glaze::Instance>> t)
    {
        påväg_.erase(n);
        Ruta* r = ruta(n);
        if (!r || t->empty())
            return;
        r->träd = träd_.lägg(glaze::Shape::instances(glaze::Shape::mesh(kon_), std::move(*t)));
    }

    void stolpeKom(int m, glaze::Mesh mesh)
    {
        stolpPåväg_.erase(m);
        stolpMesh_[m] = std::move(mesh);
        planera();   // stolparna som väntade på meshen
    }

private:
    enum Lager { kHimmel, kTerräng, kMoln, kMark, kStolpar, kGlas, kLinor, kTräd, kMarkerade };

    struct Ruta {
        int           terräng = -1, moln = -1, mark = -1, träd = -1;   // platserna
        glaze::Mesh   mesh;
        glaze::Bitmap foto;
    };
    struct Stolpe3 {
        glaze::Point3 vid;
        float         gir   = 0.0f;
        int           typ   = 0;
        int           plats = -1, detalj = -1;
    };

    static gfx::Color horisont()
    {
        return gfx::Color{static_cast<std::uint8_t>(std::lround(kHorisont[0] * 255.0f)),
                          static_cast<std::uint8_t>(std::lround(kHorisont[1] * 255.0f)),
                          static_cast<std::uint8_t>(std::lround(kHorisont[2] * 255.0f)), 255};
    }

    // Rutan som svaret hör till — eller nullptr när den lämnat rutnätet.
    Ruta* ruta(std::uint64_t n)
    {
        const auto it = rutor_.find(nyckel(Jobb::Ruta, 0, indexI(n), indexJ(n)));
        return it == rutor_.end() ? nullptr : &it->second;
    }

    // Stilen: grå tills ortofotot kommit.
    static glaze::Shape terrängForm(const Ruta& r)
    {
        glaze::MeshStyle s;
        if (r.foto.valid()) {
            s.texture    = r.foto;
            s.saturation = 1.4f;
            s.contrast   = 1.2f;
        } else {
            s.color = gfx::Color{184, 184, 184, 255};
        }
        return glaze::Shape::mesh(r.mesh, {}, s);
    }

    // Uppdateringen: markföljningen, kameran, posen, rutorna och
    // stolparna — och statusraden och HUD:en.
    void uppdatera()
    {
        // MARKFÖLJNINGEN: terrängens höjd under kameran, annars värdens DEM.
        if (const std::optional<double> z = vy.heightAt(öga_))
            markUnder_ = *z;
        else
            markUnder_ = markhöjd(öga_.x, öga_.y);
        glaze::Camera3D k = glaze::Camera3D::lookFrom({öga_.x, öga_.y, markUnder_ + kÖgonhöjd},
                                                      static_cast<float>(gir_),
                                                      static_cast<float>(lutning_));
        k.nearPlane = 0.1;
        k.farPlane  = 500.0;
        vy.setCamera(k);
        // POSEN till kartan — strypt till 0,5 m och 0,5°.
        if (poseTillKartan &&
            (std::abs(öga_.x - poseÖga_.x) > 0.5 || std::abs(öga_.y - poseÖga_.y) > 0.5 ||
             std::abs(gir_ - poseGir_) > 0.5)) {
            poseÖga_ = öga_;
            poseGir_ = gir_;
            if (KartFönster::g_karta)
                KartFönster::g_karta->sättPose(öga_, gir_);
        }
        rutKoll();
        double vridning = std::fmod(std::abs(gir_ - planGir_), 360.0);
        vridning        = std::min(vridning, 360.0 - vridning);
        if (!planerad_ || std::hypot(öga_.x - planÖga_.x, öga_.y - planÖga_.y) >= 20.0 ||
            vridning >= 10.0)
            planera();
        status();
        vy.invalidateOverlay();
    }

    // Rutbegäran: när mittrutan flyttat släpps det som
    // lämnat rutnätet och det som saknas begärs, närmast först.
    void rutKoll()
    {
        const auto ci = static_cast<std::int64_t>(std::floor((öga_.x - kRutnätX) / kRuta3));
        const auto cj = static_cast<std::int64_t>(std::floor((öga_.y - kRutnätY) / kRuta3));
        if (ci == ci_ && cj == cj_)
            return;
        ci_             = ci;
        cj_             = cj;
        const auto inom = [ci, cj](std::uint64_t n) {
            return std::abs(indexI(n) - ci) <= kHalv3 && std::abs(indexJ(n) - cj) <= kHalv3;
        };
        for (auto it = rutor_.begin(); it != rutor_.end();) {
            if (inom(it->first)) {
                ++it;
                continue;
            }
            Ruta& r = it->second;
            terräng_.släpp(r.terräng);
            if (r.moln >= 0 || r.mark >= 0)
                räkna(kStrömMoln, false);
            moln_.släpp(r.moln);
            mark_.släpp(r.mark);
            träd_.släpp(r.träd);
            it = rutor_.erase(it);
            räkna(kStrömRutor, false);
        }
        for (std::uint64_t n : arbete_.rensa([&inom](std::uint64_t n) {
                 return kartans(n) || jobbet(n) == Jobb::Stolpe || inom(n);
             }))
            påväg_.erase(n);
        std::vector<std::pair<double, std::pair<std::int64_t, std::int64_t>>> saknas;
        for (std::int64_t j = cj - kHalv3; j <= cj + kHalv3; ++j)
            for (std::int64_t i = ci - kHalv3; i <= ci + kHalv3; ++i)
                if (!rutor_.count(nyckel(Jobb::Ruta, 0, i, j)))
                    saknas.push_back({std::hypot(static_cast<double>(i - ci),
                                                 static_cast<double>(j - cj)),
                                      {i, j}});
        std::sort(saknas.begin(), saknas.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [d, ij] : saknas)
            begär(ij.first, ij.second);
    }

    // En ruta: terrängen, ortofotot, molnet och träden — fyra jobb.
    void begär(std::int64_t i, std::int64_t j)
    {
        rutor_[nyckel(Jobb::Ruta, 0, i, j)] = Ruta{};
        const double        x0 = kRutnätX + i * kRuta3, y0 = kRutnätY + j * kRuta3;
        const std::uint64_t t = nyckel(Jobb::Ruta, 0, i, j), f = nyckel(Jobb::Foto, 0, i, j),
                            m = nyckel(Jobb::Moln, 0, i, j), s = nyckel(Jobb::Träd, 0, i, j);
        påväg_.insert({t, f, m, s});
        arbete_.lägg(t, [t, x0, y0] {
            auto mesh = std::make_shared<glaze::Mesh>(markMesh(x0, y0, kRuta3, kRuta3Hörn - 1, true));
            postaTillUi([t, mesh] { g_tred->terrängKom(t, std::move(*mesh)); });
        });
        arbete_.lägg(f, [f] {
            nätet(f);
            auto bild = std::make_shared<glaze::Bitmap>(avkoda(f));
            postaTillUi([f, bild] { g_tred->fotoKom(f, std::move(*bild)); });
        });
        arbete_.lägg(m, [m, i, j] {
            auto rå = std::make_shared<const RåMoln>(byggRåMoln(i, j));
            postaTillUi([m, rå] { g_tred->molnKom(m, rå); });
        });
        arbete_.lägg(s, [s, i, j] {
            auto träd = std::make_shared<std::vector<glaze::Instance>>(byggTräd(i, j));
            postaTillUi([s, träd] { g_tred->trädKom(s, träd); });
        });
    }

    // Stolparna inom sikten och i
    // blickriktningen (halva synfältet + 15°; inom 30 m alltid) får sin
    // detaljnivå efter avståndet — en ny nivå läggs med `setShape` när dess
    // mesh finns, annars byggs den i en arbetstråd och den gamla står kvar.
    void planera()
    {
        planerad_               = true;
        planÖga_                = öga_;
        planGir_                = gir_;
        const gfx::Sizef y      = ytansStorlek(vy);
        const double     aspekt = y.h > 0.0f ? y.w / y.h : 1.6;
        const double     halvSikt =
            std::atan(std::tan(22.5 * kPi / 180.0) * aspekt) * 180.0 / kPi + 15.0;
        for (Stolpe3& s : stolpar_) {
            const double dx = s.vid.x - öga_.x, dy = s.vid.y - öga_.y, d = std::hypot(dx, dy);
            double vinkel = std::fmod(std::abs(std::atan2(dx, dy) * 180.0 / kPi - gir_), 360.0);
            vinkel        = std::min(vinkel, 360.0 - vinkel);
            int önskad    = -1;
            if (d < 30.0 || (d < kSikt3 && vinkel < halvSikt))
                önskad = d < kStolpAvstånd[0] ? 0 : (d < kStolpAvstånd[1] ? 1 : 2);
            if (önskad < 0) {
                if (s.plats >= 0)
                    räkna(kStrömStolpar, false);
                stolpLager_.släpp(s.plats);
                s.detalj = -1;
                continue;
            }
            if (önskad == s.detalj)
                continue;
            const int  m  = s.typ * 3 + önskad;
            const auto it = stolpMesh_.find(m);
            if (it == stolpMesh_.end()) {
                if (stolpPåväg_.insert(m).second) {
                    const auto jobb = nyckel(Jobb::Stolpe, 0, m, 0);
                    arbete_.lägg(jobb, [m] {
                        auto mesh = std::make_shared<glaze::Mesh>(byggStolpe(
                            kStolpTrianglar[m % 3], 1000u + static_cast<std::uint64_t>(m), 1, false));
                        postaTillUi([m, mesh] { g_tred->stolpeKom(m, std::move(*mesh)); });
                    });
                }
                continue;
            }
            const glaze::Shape form = glaze::Shape::mesh(it->second, {.at = s.vid, .yaw = s.gir});
            if (s.plats < 0) {
                s.plats = stolpLager_.lägg(form);
                räkna(kStrömStolpar, true);
            } else {
                stolpLager_.sätt(s.plats, form);
            }
            s.detalj = önskad;
        }
        linor();
    }

    // LINORNA: tre faser per spann mellan två laddade stolpar, kedjekurvor om
    // 21 punkter som rör.
    void linor()
    {
        std::vector<glaze::Shape> s;
        for (std::size_t o = 0; o + 1 < objektStart_.size(); ++o)
            for (int i = objektStart_[o]; i + 1 < objektStart_[o + 1]; ++i) {
                const Stolpe3& a = stolpar_[static_cast<std::size_t>(i)];
                const Stolpe3& b = stolpar_[static_cast<std::size_t>(i) + 1];
                if (a.plats < 0 || b.plats < 0)
                    continue;
                const double dx = b.vid.x - a.vid.x, dy = b.vid.y - a.vid.y;
                const double len = std::max(std::hypot(dx, dy), 1e-9);
                for (int fas = -1; fas <= 1; ++fas) {
                    std::vector<glaze::Point3> kurva;
                    for (int k = 0; k <= 20; ++k) {
                        const double t = k / 20.0;
                        kurva.push_back({a.vid.x + dx * t - dy / len * 1.5 * fas,
                                         a.vid.y + dy * t + dx / len * 1.5 * fas,
                                         a.vid.z + (b.vid.z - a.vid.z) * t + 20.0 -
                                             9.6 * t * (1.0 - t)});
                    }
                    s.push_back(glaze::Shape::tube(std::move(kurva), 0.03,
                                                   {.color = gfx::Color{60, 60, 60, 255}}, 6));
                }
            }
        vy.setShapes(kLinor, std::move(s));
    }

    // PER MUSRÖRELSE: markpunkten under pekaren (castRay — ytorna, inte
    // punkterna) till statusraden.
    void pekare(const std::optional<glaze::Ray3>& stråle)
    {
        pekarMark_.reset();
        if (stråle)
            for (const glaze::HitResult& h : vy.castRay(*stråle, {.opaqueOnly = true}))
                if (h.layer == kTerräng && h.point) {
                    pekarMark_ = h.point;
                    break;
                }
        status();
    }

    void status();

    void hud(glaze::Painter& p)
    {
        char rad[160];
        std::snprintf(rad, sizeof rad,
                      "E %.1f  N %.1f\nRiktning %.0f°  Fart %.0f km/h\nRutor %zu/%d · kö %zu",
                      öga_.x, öga_.y, gir_, fart, rutor_.size(), kRutor3, köDjup());
        ritaHud(p, rad, gir_);
    }

    Arbetare&    arbete_;
    Platser      terräng_, moln_, mark_, stolpLager_, träd_;
    glaze::Mesh  kon_;
    glaze::Point öga_{};
    double       gir_ = 0.0, lutning_ = kLutning, markUnder_ = 0.0;
    glaze::Point poseÖga_{};
    double       poseGir_  = 0.0;
    bool         musblick_ = false;
    glaze::Point förraMus_{};
    std::int64_t ci_ = std::numeric_limits<std::int64_t>::min(), cj_ = ci_;
    std::unordered_map<std::uint64_t, Ruta> rutor_;
    std::unordered_set<std::uint64_t>       påväg_;
    std::vector<Stolpe3>                    stolpar_;
    std::vector<int>                        objektStart_;
    std::map<int, glaze::Mesh>              stolpMesh_;   // typ · 3 + detaljnivå
    std::unordered_set<int>                 stolpPåväg_;
    bool                                    planerad_ = false;
    glaze::Point                            planÖga_{};
    double                                  planGir_ = 0.0;
    std::optional<glaze::Point3>            pekarMark_;
};

TreDFönster* TreDFönster::g_tred = nullptr;

// Statusraden med nio fält.
void TreDFönster::status()
{
    char        t[128];
    std::size_t molnRutor = 0, trädRutor = 0;
    for (const auto& [n, r] : rutor_) {
        molnRutor += r.moln >= 0 ? 1 : 0;
        trädRutor += r.träd >= 0 ? 1 : 0;
    }
    std::snprintf(t, sizeof t, "E %.0f  N %.0f", öga_.x, öga_.y);
    sättText(statusBar, 0, t);
    std::snprintf(t, sizeof t, "Höjd över mark %.1f m", kÖgonhöjd);
    sättText(statusBar, 1, t);
    std::snprintf(t, sizeof t, "Mark %.1f m", markUnder_);
    sättText(statusBar, 2, t);
    if (pekarMark_)
        std::snprintf(t, sizeof t, "Muspekaren: E %.1f  N %.1f  mark %.1f m", pekarMark_->x,
                      pekarMark_->y, pekarMark_->z);
    else
        std::snprintf(t, sizeof t, "Muspekaren: —");
    sättText(statusBar, 3, t);
    std::snprintf(t, sizeof t, "Riktning %.0f°  lutning %.0f°", gir_, lutning_);
    sättText(statusBar, 4, t);
    std::snprintf(t, sizeof t, "Moln i %zu rutor", molnRutor);
    sättText(statusBar, 5, t);
    std::snprintf(t, sizeof t, "Träd i %zu rutor", trädRutor);
    sättText(statusBar, 6, t);
    if (köDjup() > 0)
        std::snprintf(t, sizeof t, "Laddar %zu", köDjup());
    else
        std::snprintf(t, sizeof t, "Allt laddat");
    sättText(statusBar, 7, t);
    std::snprintf(t, sizeof t, "%.0f km/h", fart);
    sättText(statusBar, 8, t);
}

// Kartans statusrad: terrängen och baskartan.
void KartFönster::status()
{
    char t[96];
    if (terrängAktiv_)
        std::snprintf(t, sizeof t, "Terräng %zu/%zu", plattPlats_.size(), önskadePlattor_.size());
    else
        std::snprintf(t, sizeof t, "Terräng av");
    sättText(statusBar, 5, t);
    std::snprintf(t, sizeof t, "Karta %d/%zu", brickorInne_, brickor_.size());
    sättText(statusBar, 6, t);
}

Session* g_session = nullptr;

void KartFönster::tick(double sek)
{
    vyKoll();
    status();
    if (g_verbose)
        sampla("kartans tick");
    if (g_session)
        g_session->tick(0, sek);
}

void TreDFönster::tick(double sek)
{
    // Förstapersonen: hållna tangenter flyttar och girar; refresh varje
    // tick.
    if (förstaperson(*this, sek, fart / 3.6, öga_, gir_))
        input();
    uppdatera();
    if (g_verbose)
        sampla("3D-vyns tick");
    if (g_session)
        g_session->tick(1, sek);
}

// --skärm N: båda fönstren i skärmens arbetsyta, i LOGISKA px — placeringen
// delad med primärskärmens skala (AppWindow::setLocation).
bool placera(KartFönster& karta, TreDFönster& tred, int nr)
{
    (void)glaze::app();   // fasaden: skärmarna finns efter gfx::init
    const std::vector<gfx::ScreenInfo> skärmar = gfx::screens();
    for (std::size_t i = 0; i < skärmar.size(); ++i)
        std::printf("  skärm %zu: %.0f × %.0f vid (%.0f, %.0f), skala %.2f%s\n", i + 1,
                    skärmar[i].bounds.w, skärmar[i].bounds.h, skärmar[i].bounds.x,
                    skärmar[i].bounds.y, skärmar[i].scale, skärmar[i].primary ? " (primär)" : "");
    if (nr < 1 || nr > static_cast<int>(skärmar.size()))
        return false;
    float primär = 1.0f;
    for (const gfx::ScreenInfo& s : skärmar)
        if (s.primary)
            primär = s.scale;
    const gfx::Rectf a = skärmar[static_cast<std::size_t>(nr) - 1].work;
    karta.setLocation(static_cast<int>(a.x / primär) + 20, static_cast<int>(a.y / primär) + 20);
    tred.setLocation(static_cast<int>(a.x / primär) + 80, static_cast<int>(a.y / primär) + 80);
    return true;
}

// STEGENS FORMER — så att listan i main läser som sessionen.
//   rörelse   drivs av `driver`s tickar — användaren rör kameran eller
//             pekaren (också i 3D-appens profilvy: en ändring per tick), och
//             takten är det som mäts (`--mål`)
//   fråga     inget tickar: en timer frågar `kör` var 100 ms (det som landar,
//             en glidning — stegets bilder är bara de appen orsakar)
//   vila      kartan tickar tills strömningen kommit ikapp (main)
Steg rörelse(std::string namn, int driver, std::vector<bool> tickar, std::function<bool(int)> kör)
{
    return {std::move(namn), driver, std::move(tickar), 300.0, std::move(kör), false, 0, 0, true};
}
Steg fråga(std::string namn, std::function<bool(int)> kör, int frågaMs = 100)
{
    return {std::move(namn), 0, {false, false}, 300.0, std::move(kör), true, 0, frågaMs};
}

// Ett steg om `sek` sekunder och minst `minst` tickar, mätt från stegets
// första tick: `klart(i)` i ett stegs `kör`.
struct Varaktighet {
    double             sek;
    int                minst = 30;
    Klocka::time_point start{};
    bool               klart(int i)
    {
        if (i == 0)
            start = Klocka::now();
        return i >= minst && msSedan(start) >= sek * 1000.0;
    }
};

// Sammanfattningens bildfaser (efter pumpens events och posted).
constexpr glaze::ProfilePhase kTabellFaser[] = {
    glaze::ProfilePhase::Content,      glaze::ProfilePhase::CanvasPrepare,
    glaze::ProfilePhase::CanvasDrapes, glaze::ProfilePhase::CanvasDraw,
    glaze::ProfilePhase::CanvasPaint,  glaze::ProfilePhase::Present,
    glaze::ProfilePhase::PresentWait,  glaze::ProfilePhase::Gpu};

constexpr const char* kOrörda = "27 båda orörda 5 s (regel 1)";

// --gpus: korten som `gfx::gpuAdapters` ser dem, ett per rad — id:t först, så
// att det går att klistra in efter --gpu.
int listaKorten()
{
    for (const gfx::GpuAdapter& a : gfx::gpuAdapters()) {
        const char* slag = a.kind == gfx::GpuKind::Integrated ? "integrerat"
                           : a.kind == gfx::GpuKind::Discrete ? "diskret"
                           : a.kind == gfx::GpuKind::External ? "externt"
                           : a.kind == gfx::GpuKind::Software ? "programvara"
                                                              : "okänt slag";
        std::printf("%s  %s (%s, %s, %llu MB eget minne%s%s)\n", a.id.c_str(), a.name.c_str(),
                    a.vendor.empty() ? "?" : a.vendor.c_str(), slag,
                    static_cast<unsigned long long>(a.memoryBytes >> 20),
                    a.isDefault ? ", förval" : "", a.selectable ? "" : ", inte valbart");
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    int    antalObjekt = 20, skärm = 0, bara = 0;
    double mål         = 0.0;
    bool   hp          = false;
    std::string kort;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
        else if (std::strcmp(argv[i], "--gpus") == 0)
            return listaKorten();
        else if (std::strcmp(argv[i], "--gpu") == 0 && i + 1 < argc)
            kort = argv[++i];
        else if (std::strcmp(argv[i], "--synlig") == 0)
            g_synlig = true;
        else if (std::strcmp(argv[i], "--hp") == 0)
            hp = true;
        else if (std::strcmp(argv[i], "--skärm") == 0 && i + 1 < argc)
            skärm = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--objekt") == 0 && i + 1 < argc)
            antalObjekt = std::max(1, std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--mål") == 0 && i + 1 < argc)
            mål = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--steg") == 0 && i + 1 < argc)
            bara = std::clamp(std::atoi(argv[++i]), 1, 27);
    }
    std::printf("=== terrain_perf — terrängkartan och 3D-vyn (%s, %s%s%s%s) ===\n",
                kBygge, g_synlig ? "synliga fönster" : "dolda fönster",
                hp ? ", GPU HighPerformance" : "",
                kort.empty() ? "" : (", kortet " + kort).c_str(),
                bara > 0 ? (", bara steg " + std::to_string(bara)).c_str() : "");
    if (hp)
        gfx::setGpuPreference(gfx::GpuPreference::HighPerformance);
    gfx::setGpuAdapter(kort);
    std::printf("  minne vid start: process %s\n", mb(läsMinne().process).c_str());

    auto t0 = Klocka::now();
    (void)pngPool();
    g_objekt              = byggObjekt(antalObjekt);
    std::size_t stolparna = 0;
    for (const Objekt& o : g_objekt)
        stolparna += o.linje.size();
    std::printf("  data: %d bilder (PNG) och %d ledningar med %zu stolpar — %.0f ms\n", kPngAntal,
                antalObjekt, stolparna, msSedan(t0));

    Arbetare    arbete(4);
    KartFönster karta(arbete);
    TreDFönster tred(arbete);
    KartFönster::g_karta = &karta;
    TreDFönster::g_tred  = &tred;
    if (skärm > 0 && !placera(karta, tred, skärm)) {
        fel("skärm " + std::to_string(skärm) + " finns inte");
        return 1;
    }
    karta.byggScen();
    tred.byggScen();
    std::printf("  minne efter scenerna: process %s\n", mb(läsMinne().process).c_str());

    Session s({{"kartan", &karta, &karta.karta, [&karta] { return karta.köDjup(); }},
               {"3D", &tred, &tred.vy, [&tred] { return tred.köDjup(); }}},
              {"brickor", "plattor", "3D-rutor", "moln", "stolpar"});
    g_session = &s;
    s.profilera();

    // 3D-vyn öppnas en tredjedel in på objekt 0, tio meter vid sidan av
    // ledningen och riktad längs den — ägd av kartan.
    std::string gpu[2];
    const auto  öppna3D = [&] {
        if (tred.isOpen())
            return;
        const Objekt&     o = g_objekt.front();
        const std::size_t i = o.linje.size() / 3;
        const double      v = o.riktning[i] * kPi / 180.0;
        tred.show(karta);
        tred.vy.focus();
        tred.ställ({o.linje[i].x + 10.0 * std::cos(v), o.linje[i].y - 10.0 * std::sin(v)},
                   o.riktning[i]);
        gpu[1] = tred.window().renderer().deviceName();
        // Himlens program kompileras först här (i körtid) — ett fel hade gett
        // reservfärgen och en annan mätning, så det fäller sessionen.
        if (const std::string e = tred.vy.programError("himmel"); !e.empty())
            fel("himlens program kompilerade inte: " + e);
    };
    const auto vila = [&](const std::string& namn, bool tredOckså) {
        return Steg{namn, 0, {true, false}, 180.0,
                    [&karta, &tred, tredOckså](int i) {
                        return i > 30 && karta.ikapp() && (!tredOckså || tred.ikapp());
                    },
                    true};
    };
    // KÖRNINGEN: W hålls i 3D-vyn i tio sekunder, med posen till kartan i
    // appens läge (eller inte alls — 3D-vyn ensam).
    const auto körning = [&](bool tillKartan, Läge läge) {
        return [&karta, &tred, tillKartan, läge, tid = Varaktighet{10.0}](int i) mutable {
            if (i == 0) {
                karta.läge          = läge;
                tred.poseTillKartan = tillKartan;
                tred.tangent(gfx::Key::W, true);
            }
            if (!tid.klart(i))
                return false;
            tred.tangent(gfx::Key::W, false);
            return true;
        };
    };
    // 3D-APPENS ÄNDRINGAR på kartan, en per tick i fem sekunder, i appens
    // läge — `ändra(i, slut)` är kartans.
    const auto ändring = [&](Läge läge, void (KartFönster::*ändra)(int, bool)) {
        return [&karta, läge, ändra, tid = Varaktighet{5.0, 20}](int i) mutable {
            karta.läge = läge;
            if (!tid.klart(i)) {
                (karta.*ändra)(i, false);
                return false;
            }
            (karta.*ändra)(i, true);
            return true;
        };
    };
    // ALLT SAMTIDIGT: kartan tiltas, vrids och panoreras, 3D-vyn kör och girar
    // (E och Q om vartannat, två sekunder var), posen når kartan i appens läge
    // — i femton sekunder, och sist släpps knapparna mellan två varv.
    const auto alltSamtidigt = [&](Läge läge) {
        return [&karta, &tred, läge, tid = Varaktighet{15.0}, gir = gfx::Key::E,
                girStart = Klocka::time_point{}](int i) mutable {
            if (i == 0) {
                karta.läge          = läge;
                tred.poseTillKartan = true;
                tred.tangent(gfx::Key::W, true);
                tred.tangent(gir, true);
                girStart = Klocka::now();
            } else if (msSedan(girStart) >= 2000.0) {
                tred.tangent(gir, false);
                gir = gir == gfx::Key::E ? gfx::Key::Q : gfx::Key::E;
                tred.tangent(gir, true);
                girStart = Klocka::now();
            }
            if (!karta.alltSamtidigt(i, tid.klart(i)))
                return false;
            tred.tangent(gir, false);
            tred.tangent(gfx::Key::W, false);
            return true;
        };
    };
    const glaze::Camera utgång{.center = {kX0, kY0}, .zoom = kStartZoom, .bearing = 30.0f};
    int                 panoreringar = 0;

    // STEGEN 1–27, och vilan efter dem som strömmar.
    std::vector<std::vector<Steg>> steg(28);
    steg[1] = {{"1 kartan: start", 0, {true, false}, 180.0,
                [&karta](int i) { return i > 5 && karta.ikapp(); }, true}};
    steg[2] = {fråga("2 tiltglidning (50 ms-timer)", [&karta](int i) { return karta.glida(i); },
                     50)};
    steg[3] = {rörelse("3 tilt 5°–60° under terräng", 0, {true, false},
                       [&karta](int i) { return karta.tilta(i); }),
               vila("   vila efter tilten", false)};
    steg[4] = {rörelse("4 vridning under tilt", 0, {true, false},
                       [&karta](int i) { return karta.vrida(i); })};
    steg[5] = {rörelse("5 panorering 2 km under tilt", 0, {true, false},
                       [&karta, &panoreringar](int i) {
                           if (i == 0)
                               panoreringar = karta.panoreringsdrag(2000.0);
                           return panorera(karta, karta.karta, i, panoreringar, -22.0, 0.0);
                       })};
    steg[6] = {fråga("6 landning, kartan still", [&karta](int i) {
        if (i == 0)
            karta.hoppaTillObjekt();
        return i > 2 && karta.ikapp();
    })};
    steg[7] = {rörelse("7 zoom in och ut under tilt", 0, {true, false},
                       [&karta](int i) {
                           const gfx::Sizef y = ytansStorlek(karta.karta);
                           if (i < 8 * 6 && i % 6 == 0)
                               karta.injicera(karta.karta, gfx::EventType::MouseWheel, y.w * 0.4,
                                              y.h * 0.6, gfx::MouseButton::None, 1.0f);
                           else if (i >= 120 && i < 120 + 8 * 6 && i % 6 == 0)
                               karta.injicera(karta.karta, gfx::EventType::MouseWheel, y.w * 0.6,
                                              y.h * 0.5, gfx::MouseButton::None, -1.0f);
                           return i >= 240;
                       }),
               vila("   vila efter zoomen", false)};
    steg[8] = {rörelse("8 hover över objekten", 0, {true, false},
                       [&karta, tid = Varaktighet{10.0}](int i) mutable {
                           const glaze::Point p = lissajous(i, ytansStorlek(karta.karta));
                           karta.injicera(karta.karta, gfx::EventType::MouseMotion, p.x, p.y);
                           return i >= 300 || tid.klart(i);
                       })};
    steg[9]  = {rörelse("9 redigering (24 000 heightAt per bild)", 0, {true, false},
                        [&karta](int i) { return karta.redigera(i); })};
    steg[10] = {fråga("10 baskartans batcher, kartan still", [&karta](int i) {
        if (i == 0)
            karta.bytKälla();
        return i > 2 && karta.ikapp();
    })};
    steg[11] = {fråga("11 3D öppnas, rutorna landar", [&](int i) {
        if (i == 0)
            öppna3D();
        return i > 2 && tred.ikapp() && karta.ikapp();
    })};
    steg[12] = {rörelse("12 3D: körning 100 km/h (ensam)", 1, {false, true},
                        körning(false, Läge::Rättad))};
    steg[13] = {rörelse("13 3D: vridning Q/E", 1, {false, true},
                        [&tred, tid = Varaktighet{6.0}, växlat = false](int i) mutable {
                            const bool klart = tid.klart(i);
                            if (i == 0)
                                tred.tangent(gfx::Key::E, true);
                            else if (!växlat && msSedan(tid.start) >= 3000.0) {
                                tred.tangent(gfx::Key::E, false);
                                tred.tangent(gfx::Key::Q, true);
                                växlat = true;
                            }
                            if (!klart)
                                return false;
                            tred.tangent(växlat ? gfx::Key::Q : gfx::Key::E, false);
                            return true;
                        })};
    steg[14] = {rörelse("14 3D: vridning med mittenknappen", 1, {false, true}, [&tred](int i) {
        const gfx::Sizef y = ytansStorlek(tred.vy);
        const double     x = y.w / 2 + 160.0 * std::sin(i * 0.04);
        if (i == 0)
            tred.injicera(tred.vy, gfx::EventType::MouseButtonDown, x, y.h / 2,
                          gfx::MouseButton::Middle);
        else if (i < 240)
            tred.injicera(tred.vy, gfx::EventType::MouseMotion, x, y.h / 2);
        else {
            tred.injicera(tred.vy, gfx::EventType::MouseButtonUp, x, y.h / 2,
                          gfx::MouseButton::Middle);
            return true;
        }
        return false;
    })};
    steg[15] = {fråga("15 3D: landning, kameran still", [&tred](int i) {
        if (i == 0)
            tred.hoppa(400.0);
        return i > 2 && tred.ikapp();
    })};
    steg[16] = {rörelse("16 3D: musrörelse", 1, {false, true},
                        [&tred, tid = Varaktighet{10.0}](int i) mutable {
                            const glaze::Point p = lissajous(i, ytansStorlek(tred.vy));
                            tred.injicera(tred.vy, gfx::EventType::MouseMotion, p.x, p.y);
                            return i >= 300 || tid.klart(i);
                        })};
    steg[17] = {rörelse("17 3D-appen: modulens former, idag", 0, {true, false},
                        ändring(Läge::Idag, &KartFönster::modulÄndring))};
    steg[18] = {rörelse("18 3D-appen: modulens former, rättad", 0, {true, false},
                        ändring(Läge::Rättad, &KartFönster::modulÄndring))};
    steg[19] = {rörelse("19 3D-appen: profilhover, idag", 0, {true, false},
                        ändring(Läge::Idag, &KartFönster::profilHover))};
    steg[20] = {rörelse("20 3D-appen: profilhover, rättad", 0, {true, false},
                        ändring(Läge::Rättad, &KartFönster::profilHover))};
    steg[21] = {rörelse("21 3D-appen: ett objekt, idag", 0, {true, false},
                        ändring(Läge::Idag, &KartFönster::objektÄndring))};
    steg[22] = {rörelse("22 3D-appen: ett objekt, rättad", 0, {true, false},
                        ändring(Läge::Rättad, &KartFönster::objektÄndring))};
    steg[23] = {rörelse("23 båda: körning, idag", 1, {false, true}, körning(true, Läge::Idag))};
    steg[24] = {rörelse("24 båda: körning, rättad", 1, {false, true}, körning(true, Läge::Rättad)),
                vila("   vila efter körningen", true)};
    steg[25] = {rörelse("25 ALLT SAMTIDIGT, idag", 0, {true, true}, alltSamtidigt(Läge::Idag)),
                vila("   vila efter allt, idag", true)};
    steg[26] = {rörelse("26 ALLT SAMTIDIGT, rättad", 0, {true, true}, alltSamtidigt(Läge::Rättad)),
                vila("   vila efter allt, rättad", true)};
    // INGENTING HÄNDER: båda fönstren öppna och orörda — hård regel 1 säger 0
    // bilder. Insvängningen tar den sista ritningen efter vilan och de sista
    // uppladdningarna, som ritytan strömmar inom sin budget.
    steg[27] = {{"   insvängning 2 s", 0, {false, false}, 30.0, [](int) { return true; }, false,
                 2000},
                {kOrörda, 0, {false, false}, 30.0, [](int) { return true; }, false, 5000}};

    for (const Steg& st : steg[1])
        s.lägg(st);
    if (bara == 0) {
        for (std::size_t n = 2; n < steg.size(); ++n)
            for (const Steg& st : steg[n])
                s.lägg(st);
    } else if (bara > 1) {
        // FÖRUTSÄTTNINGARNA, direkt: kartan tiltad i 1:1000 (steg 3–27) och
        // 3D-vyn öppen (steg 12–27) — och vilan tills allt är inne.
        const bool tilt = bara >= 3, med3D = bara >= 12;
        s.lägg({"   förberedelse", 0, {true, false}, 300.0,
                [&, tilt, med3D](int i) {
                    if (i == 0) {
                        if (tilt) {
                            karta.karta.setCamera(utgång);
                            karta.karta.setMapScale(kTiltSkala);
                            glaze::Camera c = karta.karta.camera();
                            c.pitch         = 60.0f;
                            karta.sättKamera(c);
                        }
                        if (med3D)
                            öppna3D();
                    }
                    return i > 5 && karta.ikapp() && (!med3D || tred.ikapp());
                },
                true});
        for (const Steg& st : steg[static_cast<std::size_t>(bara)])
            s.lägg(st);
    }
    s.reservera();

    karta.onShow = [&](glaze::Widget*, glaze::EventArgs&) {
        gpu[0] = karta.window().renderer().deviceName();
        s.börja();
    };

    t0 = Klocka::now();
    karta.run();
    const double total = msSedan(t0) / 1000.0;

    g_liv->store(false);
    arbete.stoppa();
    g_session = nullptr;

    if (s.mått().empty())
        fel("kartans fönster öppnades inte — ingen yta?");
    if ((bara == 0 || bara == 8) && karta.hoverByten() == 0)
        fel("hovret träffade aldrig ett objekt — träffprovet kördes inte");
    for (const StegMått& m : s.mått()) {
        // HÅRD REGEL 1: det orörda steget ritar ingenting.
        if (m.namn == kOrörda)
            for (std::size_t w = 0; w < m.f.size(); ++w)
                if (!m.f[w].profilBilder.empty())
                    fel(std::string(w == 0 ? "kartan" : "3D-vyn") + " ritade " +
                        std::to_string(m.f[w].profilBilder.size()) +
                        " bilder orörd — något håller pumpen vaken (hård regel 1)");
        // --mål: rörelsestegens takt i de fönster som tickar.
        if (mål > 0.0 && m.rörelse)
            for (std::size_t w = 0; w < m.f.size(); ++w) {
                const Profil p = profil(m.f[w]);
                if (w < m.tickar.size() && m.tickar[w] && !p.intervall.tom() &&
                    p.intervall.kvantil(0.95) > 1000.0 / mål) {
                    char b[160];
                    std::snprintf(b, sizeof b, "\"%s\": %s p95-intervall %.1f ms är över %.1f ms (%.0f Hz)",
                                  m.namn.c_str(), w == 0 ? "kartans" : "3D-vyns",
                                  p.intervall.kvantil(0.95), 1000.0 / mål, mål);
                    fel(b);
                }
            }
    }

    std::printf("  totalt %.1f s · GPU kartan: %s · 3D-vyn: %s\n", total,
                gpu[0].empty() ? "—" : gpu[0].c_str(), gpu[1].empty() ? "—" : gpu[1].c_str());

    // SAMMANFATTNINGEN — raderna som avslutar rapporten.
    std::printf("\n  | Steg | Fönster | Bilder | Intervall p50 / p95 / p99 / max ms | > 20 ms | "
                "> 6,7 ms | Faser p50 ms: events / posted / content / prepare / drapes / draw / "
                "paint / present / wait / gpu | Draperingar per bild | CPU (en kärna) | "
                "Processen efter | GPU |\n"
                "  |---|---|---|---|---|---|---|---|---|---|---|\n");
    for (const StegMått& m : s.mått()) {
        Serie händelser, postat;
        for (const glaze::PumpProfile& v : m.pump)
            if (v.depth <= 1) {
                händelser.add(v.events);
                postat.add(v.posted);
            }
        char cpu[32] = "—";
        if (m.cpuProcent >= 0.0)
            std::snprintf(cpu, sizeof cpu, "%.0f %%", m.cpuProcent);
        bool skrev = false;
        for (std::size_t w = 0; w < m.f.size(); ++w) {
            const Profil p = profil(m.f[w]);
            if (p.bilder == 0)
                continue;
            skrev = true;
            char faser[160];
            int  n = std::snprintf(faser, sizeof faser, "%.2f / %.2f", händelser.median(),
                                   postat.median());
            for (const glaze::ProfilePhase f : kTabellFaser)
                n += std::snprintf(faser + n, sizeof faser - static_cast<std::size_t>(n), " / %.2f",
                                   p.fas[static_cast<std::size_t>(f)].median());
            std::printf("  | %s | %s | %zu | %.1f / %.1f / %.1f / %.1f | %d | %d | %s | %.2f | %s | "
                        "%s | %s |\n",
                        m.namn.c_str(), w == 0 ? "kartan" : "3D", p.bilder, p.intervall.median(),
                        p.intervall.kvantil(0.95), p.intervall.kvantil(0.99), p.intervall.max(),
                        p.intervall.över(20.0), p.intervall.över(1000.0 / 150.0), faser,
                        p.perBild[static_cast<std::size_t>(glaze::ProfileCount::DrapeRenders)], cpu,
                        mb(m.minne.process).c_str(), gpu[w].empty() ? "—" : gpu[w].c_str());
        }
        if (!skrev)
            std::printf("  | %s | — | 0 | — | — | — | — | — | %s | %s | — |\n", m.namn.c_str(), cpu,
                        mb(m.minne.process).c_str());
    }
    // HUVUDMÅTTET: ALLT SAMTIDIGT i appens båda lägen.
    for (const StegMått& m : s.mått())
        if (m.namn.find("ALLT SAMTIDIGT") != std::string::npos) {
            std::printf("  huvudmåttet %s:", m.namn.c_str());
            for (std::size_t w = 0; w < m.f.size(); ++w) {
                const Profil p = profil(m.f[w]);
                if (!p.intervall.tom())
                    std::printf("  %s p50 %.1f / p95 %.1f ms (%d över 20 ms av %zu)",
                                w == 0 ? "kartan" : "3D", p.intervall.median(),
                                p.intervall.kvantil(0.95), p.intervall.över(20.0),
                                p.intervall.v.size());
            }
            std::printf("\n");
        }
    if (!s.klar() || g_fel) {
        std::printf("=== terrain_perf: FALLERAR (%d fel) ===\n", g_fel);
        return 1;
    }
    std::printf("=== terrain_perf: sessionen gick igenom ===\n");
    return 0;
}
