// canvas_perf3d — RITYTANS 3D-PRESTANDA.
//
// Kör:  ./build/tests/canvas_perf3d [-v] [--full] [--hp]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
// --hp sätter `gfx::setGpuPreference(HighPerformance)` före fönstret — på en
// Optimus-dator det diskreta kortet i stället för det integrerade.
//
// En RIKTIG `glaze::Canvas` i ett DOLT fönster på 1280 × 800 logiska px,
// ritad genom den riktiga backenden (D3D11 eller GL) i ritytans 3D-mål med en
// `Camera3D`. Scenen har tre tunga slag
// i SWEREF:
//
//   STOLPAR   gallerstolpar av rör (som pick_gate --stolpe): platta,
//             icke-indexerade trianglar med normal och färg per triangel, i
//             fyra DETALJNIVÅER (100, 40, 15 och 5 % av trianglarna, upp till
//             60, 150, 400 m och sedan obegränsat). ctest: 24 egna stolpar om
//             20 k trianglar; --full: 50 om 200 k (ett realistiskt tak — många
//             stolpar, inte en jätte). Ringen och raden nedan DELAR en mesh.
//   LINOR     tre faser mellan grannstolparna i varje rad, kedjekurvor om 51
//             punkter (`polyline3` med bredd i världen) — Line3-körningen
//             som anropstaket (D) mäter sammanslagningen med.
//   TRÄD      en kon i två nivåer som INSTANSER: 10 000 (ctest) / 100 000
//             (--full) på 2 × 2 km.
//   PUNKTER   ett moln med färg och skalär (höjd över mark): 600 000 / 6 M på
//             1 × 1 km.
//   TERRÄNG 121 rutor om 17 × 17 hörn — 11 × 11 plattor om 100 m
//             med en mjuk kulle — i ett Terrain-lager och 24 vägar i ett
//             Ground-lager, DRAPERADE på dem (canvas/drape.h); samma i ctest
//             och --full.
//
// GRINDEN — DETERMINISTISK, oberoende av tiden:
//   U  en kameraändring laddar upp 0 byte och skapar 0 buffertar — för
//      stolparna, träden och punkterna, varje frame i varje serie
//   R  ingenting byggs om: kopiorna på GPU:n (residensens räknare
//      `copiesMade`, `meshCount`) är desamma före och efter serierna
//   S  en ERSATT mesh (`setShape`) med budgeten 256 KiB per frame: varje
//      frame laddar upp högst budgeten, de ritade vertexen är den GAMLAS
//      tills den nya är komplett och sedan den nyas, bytet sker efter
//      ⌈byte / budget⌉ ± 1 frames, och efteråt ber ritytan inte om fler
//      frames — mätt GENOM PUMPEN (ett andra fönsters run(); ritytan ritas
//      inte under vilan). Och ingen strömmande frame bryter
//      render-passet (`RenderStats::uploadBreaks` 0 — den nya kopian har
//      aldrig ritats, så Metal skriver den direkt)
//   L  ritade vertex = summan av varje stolpes detaljnivå i en rad som syns
//      hel, nivåerna räknade ur avståndet ögat–lådan i GRINDENS egen kod
//   C  360 stolpar i en ring kring kameran: ritade stolpar (anrop) ≤ de som
//      skär synsektorn utvidgad med stolpens vinkelbredd + 1°, och ≥ de som
//      ligger helt inne i den krympta — grindens egen vinkelräkning, i fyra
//      riktningar
//   T  träden: ritade instanser ≤ instanserna i cellerna som skär den
//      utvidgade synpyramiden (grindens egen pyramid ur kamerans vinklar,
//      cellerna ur den interna instances.h) och ≥ de i cellerna helt inne i
//      den krympta; anropen ≤ de synliga cellerna; DETALJNIVÅN per cell —
//      uppifrån, där varje cell syns, är de ritade vertexen exakt summan av
//      cellens instanser × nivåns index (avståndet ögat–cellens låda,
//      grindens räkning)
//   P  punkterna: ritade chunkar (anrop) ≤ chunkarna som skär den utvidgade
//      pyramiden och ≥ de helt inne i den krympta (chunkarna ur den interna
//      mesh_data.h)
//   residensen strömmar inom budgeten — högst budgeten per
//      frame och ⌈byte / budget⌉ ± 1 frames —, och Ä: `setPositions` och
//      `setColors` på en synlig stolpe laddar upp EXAKT de ändrade lägena
//      (hela vertex om 20 byte), skapar ingen buffert och bryter inget pass
//      (kopian ritades i förra framen: Metals
//      uppladdningspass)
//   D rit-anropen i varje frame av kameraserierna ≤ det
//      INNEHÅLLET kräver + 8 — räknat UR SCENENS EGNA FORMER med byggarnas
//      dokumenterade kontrakt, aldrig ur ritytans chunkar, celler eller
//      segment: en mesh ett anrop per segment (64 MiB av CPU-strömmarna,
//      mesh_data.h), instanserna högst ett per cell med cellerna ur
//      instances.h:s kontrakt (sidan ur utbredningen och ⌈n / 64⌉), punkterna
//      ett per chunk ur k-d-delningen (point_cloud.cpp: 2^k chunkar), linorna
//      ⌈vertex / (65 536 − formens största)⌉ per körning (fyra vertex per
//      segment och skarv, gfx/geom3d.h). + 8 är det fasta: kompositen och
//      fönstrets yta. Terrängens frames räknas med plattorna och utan
//      draperingens omritningar (serierna mäter att de är 0).
//   M TERRÄNGENS DRAPERINGAR — räknaren är draperingsmålens
//      omritningar (`DrapeCache::renders`): varje platta draperas EN gång
//      och sedan står draperingen still; en panorering inom klassen
//      (uppifrån, ortografiskt — avtrycket står still) ritar inte om och
//      laddar upp 0 byte; fram och tillbaka under tilt håller hysteresen
//      klasserna (0 omritningar, 0 byte); en zoom 4× ritar om bara de
//      plattor vars klass bytts — högst de synliga i grindens egen ruta,
//      räknat över bilderna tills draperingen står still (budgeten) — och
//      laddar upp 0 byte
//   M5 en setShape på en linje som bara når en DOLD platta ritar 0 om; en
//      panorering dit ritar om när den syns
//   M6 under en låg tilt (taket 2048 för plattorna nära ögat) ritar en
//      ändring som når varje platta högst budgeten (`drapeFrameBudget`) i den
//      första bilden, alla inom ⌈Σpx / budget⌉ + 1 bilder och sedan 0 — med
//      premissen Σpx > budget mätt
//   M7 i varje bild under zoomen bär varje platta en drapering
//   M8 en panorering inom klassen räknar 0 innehållsnycklar
//      (`DrapeCache::keysComputed`); en setShape på en väg räknar varje
//      plattas nyckel EN gång och sedan 0
//   M10 etiketter vid markhöjd byggs inte om av en ny platta långt bort, men
//      av en platta under dem (`glyphBuilds`)
//   M12 200 meshar bakom kameran och en i bild, SIST i lagret, med budgeten
//      för en enda mesh: den synliga ritas i den första bilden, och de
//      osynliga laddas upp en per bild i de följande
//
// TIDERNA — MÄTTA OCH UTSKRIVNA, INTE GRINDADE: uppbyggnaden (meshar,
// former), residensen (frames och tid tills allt är uppe), kameraändringens
// CPU-tid med en vilande GPU (median, p90, max) och genomströmningen (30
// ändringar i följd och en läsning som väntar in GPU:n) per slag, terrängens
// första drapering (mål, tid, klasser, minne) och zoomens omritningar — och
// GPU:n renderaren ritar med (`Renderer::deviceName`).
//
// INTE MÄTT: pixlarna (scene3d_pixel_gate del E och F); Metal (provas på macOS); en
// SYNLIG presentation med vsync; första bildrutan med kall
// shaderkompilering (förkompileringen räknas in i residensens tid);
// sammanslagningen av instansernas celler i D (cellkontraktet är ett tak —
// T mäter sammanslagningen); draperingarnas LRU-vräkning mot minnesbudgeten
// (budgeten sänks inte) och deras ritning i tid för en stor klass (bara
// räknaren); tiderna som krav; och gallringens TÄTHET för en vriden mesh
// (stolparna står upprätt).
//
// SABOTAGEN — alla fäller grinden (D3D11 och GL): strömningen ritar den halvfärdiga nya (S —
// bytet efter 1 frame mot 6), budgeten ignorerad (residensen 45 MB i en
// frame, S 1,4 MB > 256 KiB), den gamla släppt för tidigt (S — en frame
// ritade varken den gamla eller den nya), ingen gallring (C 360 ritade mot
// 63–71, T 10 000 instanser, P alla 16 chunkar), ingen cellgallring (T), LOD
// ignorerad för meshar och celler (L 472 032 mot 188 040, T 720 000 mot
// 376 938), cellens LOD ignorerad (T), instanserna uppladdade per bildruta
// (U träden — 520 000 byte) och en plats som alltid ber om en frame till (S
// vilan — 76 ritningar på 300 ms). "Ingen cellgallring" fäller INTE "anropen
// ≤ de synliga cellerna": de sammanslagna följderna är ändå få.
// Anropstaket (D): ett rit-anrop per form
// (en ny chunk för varje form — D linorna, 87 anrop mot 25 + 8) och
// omuppladdning per ritning (chunkarna laddas om hela — U stolparna 2,76 MB
// per kameraändring, Ä och terrängens tre).
// Draperingarna (M): ingen hysteres
// (klassen alltid den ideala — fram och tillbaka under tilt, 60 omritningar
// på D3D11 och 80 på GL) och omritning varje bildruta (alla fyra M-fallen —
// 2420 omritningar, och zoomen ritade om 121 mot 25 bytta klasser).
// Strömningens brott (S, Ä): Metals
// Pass skickad till Break fäller Ä (ändringen i en ritad kopia bryter
// passet), Direct borttagen fäller S (varje strömmande bit bryter); på
// D3D11 och GL är brotten alltid 0.
// Draperingsbudgeten (M5–M12): `syns`-villkoret bort i
// prepareDrapes (M5 — den dolda plattan ritas om), budgeten ignorerad (M6 —
// hela Σpx i den första bilden), det gamla målet släppt innan det nya är
// ritat (M7 — färre draperingar än plattor i en bild), nyckeln räknad
// alltid (M8 — 121 nycklar vid panoreringen), den globala terrängversionen i
// byggets nyckel i stället för regionen (M10 — plattan långt bort bygger om)
// och uppladdningen i listordning (M12 — den synliga ritas inte i den
// första bilden).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <gfx/geom3d.h>
#include <glaze/glaze.h>

#include "canvas/drape.h"       // intern — terrängens draperingar
#include "canvas/instances.h"   // intern — instansernas celler (T)
#include "canvas/mesh_data.h"   // intern — punktmolnets chunkar (P)
#include "canvas/residency.h"   // intern — föremålens kopior (R)
#include "canvas/resources.h"   // intern — fönstrets ritytsresurser

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

constexpr double kPi = 3.14159265358979323846;

// EGET frö och egen avbildning (canvas_perf:s skäl): scenen ska vara
// densamma med libstdc++ och libc++.
struct Slump {
    std::uint64_t s;
    std::uint64_t nästa()
    {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double enhet() { return static_cast<double>(nästa() >> 11) * (1.0 / 9007199254740992.0); }
    double mellan(double a, double b) { return a + (b - a) * enhet(); }
};

// Scenens mitt i SWEREF — RTC:n är ramverkets.
constexpr double kX0 = 674000.0;
constexpr double kY0 = 6580000.0;

// TRÄDENS NIVÅGRÄNS: konen med 24 sidor upp till så många meter, sedan den
// med 6. Långt för ett träd — så att utsikten uppifrån (T, ögat ≈ 2 600 m
// upp) BLANDAR nivåerna: cellerna mitt under ögat är närmare, hörnen längre
// bort.
constexpr double kKonGräns = 2650.0;

// ------------------------------------------------------------- stolparna
//
// DETALJNIVÅERNA: andelen av trianglarna och det största avståndet (0 =
// obegränsat). Grinden räknar nivån ur avståndet med SAMMA tal (L).
constexpr double kNivåAndel[4]    = {1.0, 0.40, 0.15, 0.05};
constexpr double kNivåAvstånd[4]  = {60.0, 150.0, 400.0, 0.0};
constexpr double kStolpeBredd     = 2.0;    // lådan ±1 m i x och y
constexpr double kStolpeHöjd      = 30.0;

struct Stolpe {
    glaze::Mesh                mesh;
    std::vector<std::uint32_t> nivå;   // vertex per nivå (icke-indexerat)
    std::size_t                byte = 0;   // GPU:ns: läge 12 + normal 4 + färg 4
};

// En gallerstolpe om ≈ `trianglar` trianglar på nivå 0 (rör med 12 sidor
// mellan lådans hörn) — och de tre grövre nivåerna efter den i samma mesh.
Stolpe byggStolpe(int trianglar, std::uint64_t frö)
{
    Slump           r{frö};
    glaze::MeshDesc d;
    Stolpe          s;
    for (int n = 0; n < 4; ++n) {
        const int mål = std::max(64, static_cast<int>(trianglar * kNivåAndel[n]));
        // Ett rör med P punkter har 24 · (P − 1) + 20 trianglar.
        const int stänger = std::max(4, mål / 600);
        const int punkter = std::max(2, (mål / stänger - 20) / 24 + 1);
        gfx::MeshGeometry g;
        for (int k = 0; k < stänger; ++k) {
            const double h0 = r.mellan(0.0, kStolpeHöjd - 2.0);
            const double h1 = h0 + r.mellan(0.5, 2.0);
            const auto   hörn = [&](double h) {
                const double sk = 1.0 - h / (kStolpeHöjd * 1.4);
                const int    q  = static_cast<int>(r.enhet() * 4.0);
                return gfx::Vec3d{(q & 1 ? 1.0 : -1.0) * sk * kStolpeBredd / 2,
                                  (q & 2 ? 1.0 : -1.0) * sk * kStolpeBredd / 2, h};
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
        // Platta trianglar med färg per triangel, som en verklig stolpe.
        const std::size_t före = d.positions.size();
        for (std::size_t t = 0; t + 2 < g.indices.size(); t += 3) {
            const gfx::Color c{static_cast<std::uint8_t>(120 + (t / 3) % 60), 125,
                               135, 255};
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
        const auto antal = static_cast<std::uint32_t>(d.positions.size() - före);
        d.lods.push_back(glaze::Lod{static_cast<std::uint32_t>(före), antal,
                                    kNivåAvstånd[n]});
        s.nivå.push_back(antal);
    }
    s.byte = d.positions.size() * 20;
    s.mesh = glaze::Mesh(std::move(d));
    return s;
}

// Nivån på avståndet ögat–lådan — GRINDENS räkning av regeln (mesh.h).
int nivåVid(double avstånd)
{
    for (int n = 0; n < 4; ++n)
        if (kNivåAvstånd[n] == 0.0 || avstånd <= kNivåAvstånd[n])
            return n;
    return 3;
}

double avståndTillLåda(const glaze::Point3& p, const glaze::Box3& b)
{
    const double dx = std::max({b.min.x - p.x, 0.0, p.x - b.max.x});
    const double dy = std::max({b.min.y - p.y, 0.0, p.y - b.max.y});
    const double dz = std::max({b.min.z - p.z, 0.0, p.z - b.max.z});
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

glaze::Box3 stolpLåda(double x, double y)
{
    return glaze::Box3{{x - kStolpeBredd / 2, y - kStolpeBredd / 2, 0.0},
                       {x + kStolpeBredd / 2, y + kStolpeBredd / 2, kStolpeHöjd}};
}

// ------------------------------------------------------------- anropstaket
//
// D: DET INNEHÅLLET KRÄVER — byggarnas kontrakt räknade här ur grindens egna
// former (filhuvudet). Det fasta i varje frame är kFast.
constexpr std::uint64_t kFast = 8;

// En mesh: ett anrop per SEGMENT. Ett segment rymmer högst 64 MiB av
// CPU-strömmarnas vertexdata OCH lika mycket index (mesh_data.h), och en
// triangel för med sig högst tre nya lägen — så ett segment rymmer minst
// ⌊64 MiB / (3 · byte per läge)⌋ trianglar (indexen, 12 byte per triangel,
// är aldrig fler byte med lägen om minst 4 byte).
std::uint64_t meshAnrop(std::uint64_t trianglar, std::uint64_t bytePerLäge)
{
    constexpr std::uint64_t kSegment   = std::uint64_t{64} << 20;
    const std::uint64_t     perSegment = kSegment / (3u * std::max<std::uint64_t>(bytePerLäge, 4));
    return std::max<std::uint64_t>(1, (trianglar + perSegment - 1) / perSegment);
}

// Instanserna: högst ett anrop per CELL (instances.h — en följd synliga
// celler på samma nivå i samma block blir ett), och cellerna ur dess
// kontrakt: sidan s = (de utsträckta axlarnas produkt / ⌈n / 64⌉)^(1/axlarna),
// en axel kortare än s räknas inte, och cellerna är rutor om s från lägenas
// minsta hörn — högst ∏(⌊utbredning / s⌋ + 1) med innehåll.
std::uint64_t instansAnrop(const std::vector<glaze::Instance>& in)
{
    if (in.empty())
        return 0;
    double lo[3] = {in[0].at.x, in[0].at.y, in[0].at.z};
    double hi[3] = {lo[0], lo[1], lo[2]};
    for (const glaze::Instance& i : in) {
        const double p[3] = {i.at.x, i.at.y, i.at.z};
        for (int a = 0; a < 3; ++a) {
            lo[a] = std::min(lo[a], p[a]);
            hi[a] = std::max(hi[a], p[a]);
        }
    }
    const double celler = std::ceil(static_cast<double>(in.size()) / 64.0);
    bool         med[3] = {hi[0] > lo[0], hi[1] > lo[1], hi[2] > lo[2]};
    double       s      = 4096.0;   // allt på en punkt: en cell (kChunkCell)
    for (int varv = 0; varv < 3; ++varv) {
        double prod = 1.0;
        int    d    = 0;
        for (int a = 0; a < 3; ++a)
            if (med[a]) {
                prod *= hi[a] - lo[a];
                ++d;
            }
        if (d == 0)
            break;
        s           = std::min(4096.0, std::pow(prod / celler, 1.0 / d));
        bool kortare = false;
        for (int a = 0; a < 3; ++a)
            if (med[a] && hi[a] - lo[a] < s) {
                med[a]  = false;
                kortare = true;
            }
        if (!kortare)
            break;
    }
    std::uint64_t n = 1;
    for (int a = 0; a < 3; ++a)
        n *= static_cast<std::uint64_t>(std::floor((hi[a] - lo[a]) / s)) + 1;
    return std::min<std::uint64_t>(n, in.size());
}

// Punkterna: ett anrop per CHUNK — k-d-delningen mitt i tills en del har
// högst 65 536 punkter (point_cloud.cpp) halverar k gånger: 2^k chunkar.
std::uint64_t punktAnrop(std::uint64_t n)
{
    std::uint64_t chunkar = 1;
    for (std::uint64_t m = n; m > 65536; m = (m + 1) / 2)
        chunkar *= 2;
    return chunkar;
}

// En öppen polylinje i rymden om p punkter med raka ändar och runda skarvar:
// fyra vertex per segment och skarv (gfx/geom3d.h → geom2d.h).
std::uint64_t linjeV3(std::uint64_t p)
{
    return 4u * ((p - 1) + (p - 2));
}

// Körningens chunkar: en form delas aldrig, så en chunk om 65 536 vertex
// rymmer minst 65 536 − den största formens.
std::uint64_t linjeAnrop(std::uint64_t vertex, std::uint64_t störstaForm)
{
    return vertex == 0 ? 0 : (vertex + (65536 - störstaForm) - 1) / (65536 - störstaForm);
}

// ---------------------------------------------------------- grindens kamera
//
// GRINDENS EGEN PYRAMID ur kamerans vinklar (camera.h:s konvention: azimut
// kompassen, elevationen uppåt, ögat = målet − blicken · avståndet) — inte
// ritytans matris. `marginal` grader läggs på (utvidgad) eller dras av
// (krympt) från båda halvvinklarna.
struct Pyramid {
    glaze::Point3 öga;
    double        fx, fy, fz;   // blicken
    double        rx, ry, rz;   // höger
    double        ux, uy, uz;   // upp
    double        tanH = 0, tanV = 0;
    double        nära = 0;

    // Relativa (sidled, höjdled, djup) för en punkt.
    void rel(const glaze::Point3& p, double& x, double& y, double& d) const
    {
        const double ax = p.x - öga.x, ay = p.y - öga.y, az = p.z - öga.z;
        x = ax * rx + ay * ry + az * rz;
        y = ax * ux + ay * uy + az * uz;
        d = ax * fx + ay * fy + az * fz;
    }
    // Skär lådan pyramiden? Gallrad bara när alla åtta hörn ligger utanför
    // SAMMA plan (konservativ).
    bool skär(const glaze::Box3& b) const
    {
        int ute[5] = {};
        for (int k = 0; k < 8; ++k) {
            double x = 0, y = 0, d = 0;
            rel({(k & 1) ? b.max.x : b.min.x, (k & 2) ? b.max.y : b.min.y,
                 (k & 4) ? b.max.z : b.min.z},
                x, y, d);
            ute[0] += x > d * tanH;
            ute[1] += x < -d * tanH;
            ute[2] += y > d * tanV;
            ute[3] += y < -d * tanV;
            ute[4] += d < nära;
        }
        return std::none_of(std::begin(ute), std::end(ute),
                            [](int n) { return n == 8; });
    }
    // Ligger lådan HELT inne?
    bool inne(const glaze::Box3& b) const
    {
        for (int k = 0; k < 8; ++k) {
            double x = 0, y = 0, d = 0;
            rel({(k & 1) ? b.max.x : b.min.x, (k & 2) ? b.max.y : b.min.y,
                 (k & 4) ? b.max.z : b.min.z},
                x, y, d);
            if (d < nära || std::abs(x) > d * tanH || std::abs(y) > d * tanV)
                return false;
        }
        return true;
    }
};

Pyramid pyramid(const glaze::Camera3D& k, double aspekt, double marginalGrader)
{
    const double az = k.azimuth * kPi / 180.0, el = k.elevation * kPi / 180.0;
    Pyramid      p;
    p.fx = std::sin(az) * std::cos(el);
    p.fy = std::cos(az) * std::cos(el);
    p.fz = -std::sin(el);
    p.ux = std::sin(az) * std::sin(el);
    p.uy = std::cos(az) * std::sin(el);
    p.uz = std::cos(el);
    // höger = blicken × upp
    p.rx  = p.fy * p.uz - p.fz * p.uy;
    p.ry  = p.fz * p.ux - p.fx * p.uz;
    p.rz  = p.fx * p.uy - p.fy * p.ux;
    p.öga = {k.target.x - p.fx * k.distance, k.target.y - p.fy * k.distance,
             k.target.z - p.fz * k.distance};
    const double halvV = k.fov / 2.0 * kPi / 180.0;
    const double halvH = std::atan(std::tan(halvV) * aspekt);
    const double m     = marginalGrader * kPi / 180.0;
    p.tanV = std::tan(std::clamp(halvV + m, 0.001, kPi / 2 - 0.001));
    p.tanH = std::tan(std::clamp(halvH + m, 0.001, kPi / 2 - 0.001));
    p.nära = marginalGrader >= 0.0 ? -1e300 : 0.0;   // utvidgad: inget närplan
    return p;
}

// ------------------------------------------------------------- fönstret

// Ritytan räknar sina ritningar — S:s vila mäts genom pumpen med den.
class Rityta3 : public glaze::Canvas {
public:
    int ritningar = 0;

protected:
    void draw(const glaze::Ctx& c) override
    {
        ++ritningar;
        glaze::Canvas::draw(c);
    }
};

class PerfFönster : public glaze::AppWindow {
public:
    PerfFönster() : glaze::AppWindow("canvas_perf3d", 1280, 800, gfx::Chrome::None)
    {
        setClientSize(1280, 800);
        yta.name = "yta";
        yta.dock = glaze::Dock::Fill;
        yta.setBorder(false);
        root().adopt(yta);
    }
    Rityta3 yta;
};

// Ett dolt fönster vars run() PUMPAR — under tiden får också prestanda-
// fönstret de frames det ber om. Först `insvängning` ms (det fönstrets egen
// visning flyttar fokus, och prestandafönstret ritar om sin fokusring —
// en riktig invalidering, inte en begäran från ritytan), sedan `vidStart`
// och `mätning` ms till; därefter stänger det sig självt.
class Pump : public glaze::AppWindow {
public:
    static constexpr std::uint32_t kInsvängd = 7317;   // grindens egna
    static constexpr std::uint32_t kKlar     = 7318;
    std::function<void()>          vidStart;

    Pump(std::uint32_t insvängning, std::uint32_t mätning)
        : mätning_(mätning)
    {
        setTitle("canvas_perf3d pump");
        setClientSize(64, 64);
        onShow = [this, insvängning] { window().startTimer(kInsvängd, insvängning); };
    }

protected:
    bool event(const gfx::Event& e) override
    {
        if (e.type == gfx::EventType::Timer && e.timerId == kInsvängd) {
            window().stopTimer(kInsvängd);
            if (vidStart)
                vidStart();
            window().startTimer(kKlar, mätning_);
            return true;
        }
        if (e.type == gfx::EventType::Timer && e.timerId == kKlar) {
            window().stopTimer(kKlar);
            close();
            return true;
        }
        return glaze::AppWindow::event(e);
    }

private:
    std::uint32_t mätning_;
};

struct Tider {
    std::vector<double> ms;
    double kvantil(double q) const
    {
        if (ms.empty())
            return 0.0;
        std::vector<double> s = ms;
        std::sort(s.begin(), s.end());
        return s[static_cast<std::size_t>(std::lround(q * double(s.size() - 1)))];
    }
};

struct Bänk {
    PerfFönster             f;
    glaze::CanvasResources* res = nullptr;
    double                  aspekt = 1.6;

    gfx::Renderer& rr() { return f.window().renderer(); }
    glaze::Canvas& yta() { return f.yta; }
    glaze::detail::ObjectResidency& obj() { return res->objects(); }

    double rita()
    {
        const auto t0 = Klocka::now();
        f.window().renderFrame();
        return msSedan(t0);
    }
    // GPU:ns arbete väntas in genom att framen läses (canvas_perf:s väg).
    void vänta()
    {
        std::vector<std::uint8_t> px;
        int                       w = 0, h = 0;
        f.window().captureFrame(px, w, h);
    }
    // En frame där ritytan ritas om, och dess statistik.
    gfx::RenderStats frame(double* ms = nullptr)
    {
        yta().invalidate();
        vänta();
        rr().resetStats();
        const double t = rita();
        if (ms)
            *ms = t;
        return rr().stats();
    }
    // Frames tills inget laddas upp längre — residensen är uppe. Svarar med
    // antalet frames och den sammanlagda tiden.
    // `störst` får framens största uppladdning.
    int residens(double& ms, std::uint64_t& byte, std::uint64_t* störst = nullptr)
    {
        ms   = 0.0;
        byte = 0;
        if (störst)
            *störst = 0;
        for (int i = 0; i < 400; ++i) {
            double                 t = 0.0;
            const gfx::RenderStats s = frame(&t);
            ms += t;
            byte += s.bytesUploaded;
            if (störst)
                *störst = std::max(*störst, s.bytesUploaded);
            if (s.bytesUploaded == 0)
                return i + 1;
        }
        return -1;
    }
};

// En serie kameraändringar: U (0 byte, 0 buffertar) per frame och tiderna.
struct SerieUt {
    std::uint64_t maxByte = 0, buffertar = 0;
    std::uint64_t maxAnrop = 0;   // D — flest rit-anrop i en frame
    Tider         t;
};

SerieUt serie(Bänk& b, int antal, const std::function<glaze::Camera3D(int)>& kam)
{
    SerieUt ut;
    for (int i = 0; i < antal; ++i) {
        b.yta().setCamera(kam(i));
        b.vänta();
        b.rr().resetStats();
        ut.t.ms.push_back(b.rita());
        ut.maxByte = std::max(ut.maxByte, b.rr().stats().bytesUploaded);
        ut.buffertar += b.rr().stats().buffersCreated;
        ut.maxAnrop = std::max<std::uint64_t>(ut.maxAnrop, b.rr().stats().drawCalls);
    }
    return ut;
}

// D för en serie: flest anrop ≤ innehållets krav + det fasta.
void checkAnrop(const char* vad, const SerieUt& u, std::uint64_t krav)
{
    check(u.maxAnrop <= krav + kFast,
          std::string("D ") + vad + ": rit-anropen per frame ≤ det innehållet kräver + " +
              std::to_string(kFast),
          "flest " + std::to_string(u.maxAnrop) + " anrop, innehållet kräver " +
              std::to_string(krav));
}

// Genomströmningen: 30 ändringar i följd och en läsning som väntar in GPU:n.
double genomström(Bänk& b, const std::function<glaze::Camera3D(int)>& kam)
{
    b.yta().setCamera(kam(-1));
    b.rita();
    b.vänta();
    const auto t0 = Klocka::now();
    for (int i = 0; i < 30; ++i) {
        b.yta().setCamera(kam(i));
        b.rita();
    }
    b.vänta();
    return msSedan(t0) / 30.0;
}

void skrivTider(const char* namn, const Tider& t, double genom)
{
    std::printf("  tid  %-22s kameraändring median %6.2f ms · p90 %6.2f · max %6.2f"
                " · genomströmning %6.2f ms/frame (%.0f fps)\n",
                namn, t.kvantil(0.5), t.kvantil(0.9), t.kvantil(1.0), genom,
                genom > 0.0 ? 1000.0 / genom : 0.0);
}

} // namespace

int main(int argc, char** argv)
{
    bool full = false, hp = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
        if (std::strcmp(argv[i], "--full") == 0)
            full = true;
        if (std::strcmp(argv[i], "--hp") == 0)
            hp = true;
    }
    // Stolparna i ett rutnät om kolumner × rader med 40 m mellan dem.
    const int stolpTri   = full ? 200000 : 20000;
    const int kolumner   = full ? 10 : 6;
    const int stolpAntal = full ? 50 : 24;
    const int trädAntal  = full ? 100000 : 10000;
    const int punktAntal = full ? 6000000 : 600000;
    std::printf("=== canvas_perf3d — G10, ritytans 3D-prestanda (%s%s%s) ===\n",
                kBygge, full ? ", --full" : "", hp ? ", --hp" : "");
    if (hp)   // före fönstret: enheten väljs när det skapar sin renderare
        gfx::setGpuPreference(gfx::GpuPreference::HighPerformance);

    Bänk b;
    b.f.setOffscreen(true);
    b.f.show();
    if (!b.f.isOpen()) {
        std::printf("  FEL  fönstret öppnades inte — ingen headless-yta?\n");
        std::printf("=== canvas_perf3d: FALLERAR ===\n");
        return 1;   // ALDRIG skip — clip_gate-beslutet
    }
    b.f.window().renderFrame();
    {
        const glaze::Ctx c   = b.yta().behaviorCtx();
        b.res                = c.canvas;
        const gfx::Rectf box = b.yta().resolve(c);
        b.aspekt             = box.h > 0.0f ? box.w / box.h : 1.6;
        std::printf("  gpu  %s\n", b.rr().deviceName().c_str());
        std::printf("  yta  %.0f × %.0f fysiska px (skala %.2f)\n", box.w, box.h,
                    c.scale);
    }
    if (!b.res) {
        std::printf("  FEL  fönstret har inga ritytsresurser\n");
        std::printf("=== canvas_perf3d: FALLERAR ===\n");
        return 1;
    }
    glaze::Canvas& k = b.yta();

    // ================================================================ STOLPAR
    auto t0 = Klocka::now();
    std::vector<Stolpe> stolpar;
    for (int i = 0; i < stolpAntal; ++i)
        stolpar.push_back(byggStolpe(stolpTri, 1000u + static_cast<std::uint64_t>(i)));
    std::size_t stolpByte = 0;
    for (const Stolpe& s : stolpar)
        stolpByte += s.byte;
    const double tMesh = msSedan(t0);
    std::printf("  scen %d stolpar om %u trianglar (nivå 0; alla nivåer %zu vertex "
                "per stolpe, %.1f MB på GPU:n) — byggda på %.0f ms\n",
                stolpAntal, stolpar[0].nivå[0] / 3,
                static_cast<std::size_t>(stolpar[0].mesh.vertexCount()),
                stolpByte / 1.0e6, tMesh);

    // 3D-målet en gång UTAN föremål: målet och markdjupets plan (en liten
    // buffert utanför budgeten — budgeten gäller bilderna och föremålen)
    // skapas här, så residensens frames nedan laddar bara föremål.
    k.setCamera(glaze::Camera3D{.target = {kX0, kY0, 10.0}, .distance = 240.0});
    b.frame();

    // ---- U och R: ett rutnät (6 × 4, --full 10 × 5) med 40 m mellan
    // stolparna, orbit kring det.
    const int  rader     = stolpAntal / kolumner;
    const auto stolpX    = [&](int kol) { return kX0 + (kol - (kolumner - 1) / 2.0) * 40.0; };
    const auto stolpY    = [&](int rad) { return kY0 + (rad - (rader - 1) / 2.0) * 40.0; };
    const int lagerGaller = k.addLayer("galler");
    std::uint64_t kravS   = 0;   // D — stolparnas segment och linornas chunkar
    for (int i = 0; i < stolpAntal; ++i) {
        const Stolpe& s = stolpar[static_cast<std::size_t>(i)];
        k.addShape(lagerGaller,
                   glaze::Shape::mesh(s.mesh, {.at = {stolpX(i % kolumner),
                                                      stolpY(i / kolumner), 0.0}}));
        // CPU-strömmarna: läge 12 + normal 12 + färg 4 byte.
        kravS += meshAnrop(s.mesh.vertexCount() / 3, 28);
    }
    const auto orbit = [](int i) {
        return glaze::Camera3D{.target    = {kX0, kY0, 10.0},
                               .distance  = 240.0,
                               .azimuth   = static_cast<float>(6 * (i + 1)),
                               .elevation = 20.0f};
    };
    k.setCamera(orbit(-1));
    double        tRes = 0.0;
    std::uint64_t resByte = 0;
    std::uint64_t resMax    = 0;
    const int     resFrames = b.residens(tRes, resByte, &resMax);
    const std::uint64_t förvalBudget = glaze::app().canvasUploadBudget();
    std::printf("  tid  stolparnas residens: %d frames, %.1f MB, %.1f ms (budget %.0f "
                "MiB per frame, förkompileringen inräknad)\n",
                resFrames, resByte / 1.0e6, tRes, förvalBudget / 1048576.0);
    // Uppladdningens frames (den sista laddar inget) = ⌈byte / budget⌉.
    const auto väntadeRes =
        static_cast<int>((resByte + förvalBudget - 1) / förvalBudget);
    check(resMax <= förvalBudget && std::abs(resFrames - 1 - väntadeRes) <= 1,
          "residensen strömmar inom budgeten: högst budgeten per frame, "
          "⌈byte / budget⌉ ± 1 frames",
          std::to_string(resFrames - 1) + " frames mot " +
              std::to_string(väntadeRes) + ", största " + std::to_string(resMax) +
              " byte");
    // LINOR: tre faser mellan grannstolparna i varje rad, kedjekurvor om 51
    // punkter med bredd i världen — EN Line3-körning. Efter residensen (en
    // chunk laddas utanför föremålens budget) och uppe före kameraserien.
    const int     lagerLinor = k.addLayer("linor");
    std::uint64_t linV = 0, linStörst = 0;
    for (int rad = 0; rad < rader; ++rad)
        for (int kol = 0; kol + 1 < kolumner; ++kol)
            for (int fas = 0; fas < 3; ++fas) {
                std::vector<glaze::Point3> kurva;
                for (int p = 0; p <= 50; ++p) {
                    const double t = p / 50.0;
                    kurva.push_back({stolpX(kol) + 40.0 * t,
                                     stolpY(rad) + (fas - 1) * 0.8,
                                     26.0 + fas - 1.5 * 4.0 * t * (1.0 - t)});
                }
                linV += linjeV3(kurva.size());
                linStörst = std::max(linStörst, linjeV3(kurva.size()));
                k.addShape(lagerLinor,
                           glaze::Shape::polyline3(std::move(kurva),
                                                   {.paint = gfx::Color{40, 40, 40, 255},
                                                    .width = glaze::world(0.03f)}));
            }
    kravS += linjeAnrop(linV, linStörst);
    b.frame();
    const std::uint64_t kopior = b.obj().copiesMade();
    const std::size_t   meshar = b.obj().meshCount();
    const SerieUt       uS     = serie(b, 60, orbit);
    check(uS.maxByte == 0 && uS.buffertar == 0,
          "U stolparna: en kameraändring laddar upp 0 byte och skapar 0 buffertar",
          std::to_string(uS.maxByte) + " byte, " + std::to_string(uS.buffertar) +
              " buffertar");
    checkAnrop("stolparna och linorna (orbit)", uS, kravS);
    check(b.obj().copiesMade() == kopior && b.obj().meshCount() == meshar,
          "R ingenting byggs om av kameraändringarna (kopiorna på GPU:n)",
          std::to_string(b.obj().copiesMade() - kopior) + " nya kopior");
    skrivTider("stolpar (orbit)", uS.t, genomström(b, orbit));
    {
        // Ä ÄNDRINGSLOGGEN: 300 lägen flyttade och 50 färger satta i en synlig
        // stolpe laddar upp EXAKT de lägena — hela vertex om 20 byte (läge
        // 12, normal 4, färg 4 — programs3d.h) — och skapar ingen buffert.
        glaze::Mesh&               m = stolpar[2].mesh;
        std::vector<glaze::Float3> lägen;
        const auto& d = *glaze::detail::MeshAccess::data(m);
        for (std::size_t i = 1000; i < 1300; ++i)
            lägen.push_back({d.positions[i].x, d.positions[i].y, d.positions[i].z + 0.01f});
        m.setPositions(1000, lägen);
        const gfx::RenderStats p = b.frame();
        m.setColors(5000, std::vector<gfx::Color>(50, gfx::Color{255, 0, 0, 255}));
        const gfx::RenderStats c = b.frame();
        check(p.bytesUploaded == 300u * 20u && p.buffersCreated == 0 &&
                  c.bytesUploaded == 50u * 20u && c.buffersCreated == 0,
              "Ä setPositions och setColors laddar upp bara de ändrade lägena",
              std::to_string(p.bytesUploaded) + " och " +
                  std::to_string(c.bytesUploaded) + " byte, " +
                  std::to_string(p.buffersCreated + c.buffersCreated) +
                  " buffertar");
        // Kopian ritades i förra framen, och uppdateringen sker
        // i förberedelsen före framens ritning — på Metal genom
        // uppladdningspasset, inte ett brott.
        check(p.uploadBreaks == 0 && c.uploadBreaks == 0,
              "Ä ändringarna i en ritad mesh bryter inget pass",
              std::to_string(p.uploadBreaks) + " och " +
                  std::to_string(c.uploadBreaks) + " brott");
    }
    k.setLayerVisible(lagerGaller, false);
    k.setLayerVisible(lagerLinor, false);

    // ---- L: en rad stolpar som syns hel, på åtta avstånd.
    const Stolpe& delad = stolpar[0];
    const int     lagerRad = k.addLayer("rad");
    const double  avstånd[8] = {20, 45, 80, 130, 190, 290, 420, 700};
    const glaze::Point3 öga{kX0, kY0 - 30.0, 15.0};
    std::uint64_t väntadeV = 0;
    for (int i = 0; i < 8; ++i) {
        const double x = kX0 + (i % 2 ? 1.0 : -1.0) * 0.06 * avstånd[i];
        const double y = öga.y + avstånd[i];
        k.addShape(lagerRad, glaze::Shape::mesh(delad.mesh, {.at = {x, y, 0.0}}));
        väntadeV += delad.nivå[static_cast<std::size_t>(
            nivåVid(avståndTillLåda(öga, stolpLåda(x, y))))];
    }
    k.setCamera(glaze::Camera3D::lookFrom(öga, 0.0f, 0.0f));
    k.setLayerVisible(lagerRad, false);
    b.frame();
    const gfx::RenderStats basL = b.frame();
    k.setLayerVisible(lagerRad, true);
    b.frame();
    const gfx::RenderStats medL = b.frame();
    check(medL.vertices - basL.vertices == väntadeV,
          "L ritade vertex = summan av varje stolpes detaljnivå (grindens "
          "avstånd ögat–lådan)",
          std::to_string(medL.vertices - basL.vertices) + " mot " +
              std::to_string(väntadeV));
    k.setLayerVisible(lagerRad, false);

    // ---- C: 360 stolpar i en ring om 120 m kring ögat, fyra riktningar.
    const int    lagerRing = k.addLayer("ring");
    const double R         = 120.0;
    for (int i = 0; i < 360; ++i) {
        const double v = i * kPi / 180.0;
        k.addShape(lagerRing,
                   glaze::Shape::mesh(delad.mesh, {.at = {kX0 + R * std::sin(v),
                                                          kY0 + R * std::cos(v), 0.0}}));
    }
    const glaze::Point3 mitt{kX0, kY0, 10.0};
    for (const float gir : {0.0f, 90.0f, 200.0f, 315.0f}) {
        k.setCamera(glaze::Camera3D::lookFrom(mitt, gir, 0.0f));
        k.setLayerVisible(lagerRing, false);
        b.frame();
        const gfx::RenderStats bas = b.frame();
        k.setLayerVisible(lagerRing, true);
        b.frame();
        const gfx::RenderStats med = b.frame();
        // Grindens sektor: den vågräta halvvinkeln ur det lodräta synfältet
        // och bildförhållandet, ± stolpens vinkelbredd (hörnet √2 m) + 1°.
        const double halv  = std::atan(std::tan(22.5 * kPi / 180.0) * b.aspekt) *
                            180.0 / kPi;
        const double bredd = std::atan(std::sqrt(2.0) * kStolpeBredd / 2 / R) *
                             180.0 / kPi;
        int max = 0, min = 0;
        for (int i = 0; i < 360; ++i) {
            double d = std::fmod(i - gir + 540.0, 360.0) - 180.0;
            d        = std::abs(d);
            max += d <= halv + bredd + 1.0;
            min += d <= halv - bredd - 1.0;
        }
        const std::uint32_t ritade = med.drawCalls - bas.drawCalls;
        check(ritade <= static_cast<std::uint32_t>(max) &&
                  ritade >= static_cast<std::uint32_t>(min),
              "C ringen, giren " + std::to_string(static_cast<int>(gir)) +
                  "°: ritade stolpar inom synsektorn (grindens vinkelräkning)",
              std::to_string(ritade) + " ritade, sektorn " + std::to_string(min) +
                  "–" + std::to_string(max) + " av 360");
    }
    k.setLayerVisible(lagerRing, false);

    // ---- S: en ersatt mesh med budgeten 256 KiB per frame.
    const int lagerByte = k.addLayer("byte");
    const glaze::Point3 nära{kX0, kY0 - 20.0, 12.0};
    k.setCamera(glaze::Camera3D::lookFrom(nära, 0.0f, 0.0f));
    k.addShape(lagerByte, glaze::Shape::mesh(stolpar[1].mesh, {.at = {kX0, kY0, 0.0}}));
    k.setLayerVisible(lagerByte, false);
    b.frame();
    const gfx::RenderStats basS = b.frame();
    k.setLayerVisible(lagerByte, true);
    b.residens(tRes, resByte);
    const std::size_t budget  = std::size_t{256} << 10;
    const std::size_t förval  = glaze::app().canvasUploadBudget();
    const Stolpe      ny      = byggStolpe(stolpTri * 3 / 4, 77u);
    glaze::app().setCanvasUploadBudget(budget);
    k.setShape(lagerByte, 0, glaze::Shape::mesh(ny.mesh, {.at = {kX0, kY0, 0.0}}));
    const std::uint64_t gammalV = stolpar[1].nivå[0], nyV = ny.nivå[0];
    int           frames = 0, bytet = -1;
    std::uint64_t maxS = 0, brottS = 0;
    bool          ordning = true;
    // Frames som FÖNSTRET ber om (ingen invalidering här — strömningen
    // invaliderar själv genom bildcachens väntan).
    for (int i = 0; i < 400 && bytet < 0; ++i) {
        b.vänta();
        b.rr().resetStats();
        b.rita();
        const gfx::RenderStats s = b.rr().stats();
        ++frames;
        maxS = std::max(maxS, s.bytesUploaded);
        brottS += s.uploadBreaks;
        const std::uint64_t v = s.vertices - basS.vertices;
        if (v == nyV)
            bytet = frames;
        else if (v != gammalV)
            ordning = false;
    }
    const auto väntadeFrames = static_cast<int>((ny.byte + budget - 1) / budget);
    check(maxS <= budget, "S varje frame laddar upp högst budgeten",
          std::to_string(maxS) + " byte, budgeten " + std::to_string(budget));
    // Den nya kopian ritas först när den är komplett, så varje
    // bit skrivs i en buffert som aldrig ritats (Metal: direkt).
    check(brottS == 0, "S de strömmande framesen bryter inget pass",
          std::to_string(brottS) + " brott");
    check(ordning && bytet > 0,
          "S den GAMLA meshen ritas tills den nya är komplett, sedan den nya",
          ordning ? "bytet i frame " + std::to_string(bytet)
                  : "en frame ritade varken den gamla eller den nya");
    check(bytet > 0 && std::abs(bytet - väntadeFrames) <= 1,
          "S bytet efter ⌈byte / budget⌉ ± 1 frames",
          std::to_string(bytet) + " frames mot " + std::to_string(väntadeFrames) +
              " (" + std::to_string(ny.byte) + " byte)");
    {
        // GENOM PUMPEN: ett andra fönsters run() pumpar — efter insvängningen
        // ska ritytan inte ritas på 300 ms (den ber inte om frames).
        int  före = -1;
        Pump p(150, 300);
        p.vidStart = [&b, &före] { före = b.f.yta.ritningar; };
        p.setOffscreen(true);
        p.run();
        check(före >= 0 && b.f.yta.ritningar == före,
              "S efter bytet ber ritytan inte om fler frames (genom pumpen, 300 ms)",
              std::to_string(b.f.yta.ritningar - före) + " ritningar i vilan");
    }
    glaze::app().setCanvasUploadBudget(förval);
    k.setLayerVisible(lagerByte, false);

    // ================================================================== TRÄD
    t0 = Klocka::now();
    // En kon i två nivåer (24 och 6 sidor) ur EN delad vertexmängd.
    glaze::MeshDesc kon;
    for (const int sidor : {24, 6}) {
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
                                bas + 1 + static_cast<std::uint32_t>((i + 1) % sidor),
                                bas});
        kon.lods.push_back(glaze::Lod{förs,
                                      static_cast<std::uint32_t>(kon.indices.size()) - förs,
                                      sidor == 24 ? kKonGräns : 0.0});
    }
    const std::uint32_t konNivå[2] = {kon.lods[0].count, kon.lods[1].count};
    const glaze::Mesh   konMesh(std::move(kon));
    std::vector<glaze::Instance> träd;
    Slump r{2026};
    for (int i = 0; i < trädAntal; ++i)
        träd.push_back({.at    = {kX0 + r.mellan(-1000, 1000), kY0 + r.mellan(-1000, 1000), 0},
                        .yaw   = static_cast<float>(r.mellan(0, 360)),
                        .scale = {static_cast<float>(r.mellan(0.7, 1.3)),
                                  static_cast<float>(r.mellan(0.7, 1.3)),
                                  static_cast<float>(r.mellan(0.7, 1.3))},
                        .color = gfx::Color{static_cast<std::uint8_t>(r.mellan(40, 90)),
                                            static_cast<std::uint8_t>(r.mellan(110, 170)),
                                            50, 255}});
    const std::uint64_t kravT = instansAnrop(träd);   // D — cellkontraktets tak
    const int lagerTräd = k.addLayer("träd");
    k.addShape(lagerTräd, glaze::Shape::instances(glaze::Shape::mesh(konMesh),
                                                  std::move(träd)));
    const glaze::Shape*         trädForm = k.shape(lagerTräd, 0);
    const glaze::detail::InstanceSet* celler =
        trädForm ? trädForm->instanceSet().get() : nullptr;
    std::printf("  scen %d träd i %zu celler (sidan %.0f m) — byggda på %.0f ms\n",
                trädAntal, celler ? celler->cells.size() : std::size_t{0},
                celler ? celler->cellSize : 0.0, msSedan(t0));
    const auto trädKam = [](int i) {
        return glaze::Camera3D{.target    = {kX0, kY0, 0.0},
                               .distance  = 150.0,
                               .azimuth   = static_cast<float>(37 + 6 * i),
                               .elevation = 15.0f};
    };
    k.setCamera(trädKam(0));
    b.residens(tRes, resByte);
    std::printf("  tid  trädens residens: %.1f MB, %.1f ms\n", resByte / 1.0e6, tRes);
    const SerieUt uT = serie(b, 60, trädKam);
    check(uT.maxByte == 0 && uT.buffertar == 0,
          "U träden: en kameraändring laddar upp 0 byte och skapar 0 buffertar",
          std::to_string(uT.maxByte) + " byte, " + std::to_string(uT.buffertar) +
              " buffertar");
    checkAnrop("träden (orbit)", uT, kravT);
    skrivTider("träd (orbit)", uT.t, genomström(b, trädKam));
    if (celler) {
        // T gallringen och anropen, i fyra riktningar.
        for (int i = 0; i < 4; ++i) {
            const glaze::Camera3D kam = trädKam(i * 13);
            k.setCamera(kam);
            k.setLayerVisible(lagerTräd, false);
            b.frame();
            const gfx::RenderStats bas = b.frame();
            k.setLayerVisible(lagerTräd, true);
            b.frame();
            const gfx::RenderStats med = b.frame();
            const Pyramid          vid = pyramid(kam, b.aspekt, 1.0);
            const Pyramid          smal = pyramid(kam, b.aspekt, -1.0);
            std::uint64_t maxI = 0, minI = 0;
            std::uint32_t synligaCeller = 0;
            for (const glaze::detail::InstanceCell& c : celler->cells) {
                if (vid.skär(c.box)) {
                    maxI += c.count;
                    ++synligaCeller;
                }
                if (smal.inne(c.box))
                    minI += c.count;
            }
            const std::uint64_t inst   = med.instances - bas.instances;
            const std::uint32_t anrop  = med.drawCalls - bas.drawCalls;
            check(inst <= maxI && inst >= minI && inst < static_cast<std::uint64_t>(trädAntal),
                  "T träden, azimut " + std::to_string(static_cast<int>(kam.azimuth)) +
                      "°: ritade instanser inom cellerna som skär synpyramiden",
                  std::to_string(inst) + " ritade, " + std::to_string(minI) + "–" +
                      std::to_string(maxI) + " av " + std::to_string(trädAntal));
            check(anrop <= synligaCeller && anrop > 0,
                  "T anropen ≤ de synliga cellerna (cellerna sammanslagna per "
                  "block och nivå)",
                  std::to_string(anrop) + " anrop, " +
                      std::to_string(synligaCeller) + " synliga celler");
        }
        // T DETALJNIVÅN uppifrån: varje cell syns, och de ritade vertexen är
        // summan av cellens instanser × nivåns index.
        const glaze::Camera3D topp{.target    = {kX0, kY0, 0.0},
                                   .distance  = 2600.0,
                                   .elevation = 89.0f,
                                   .fov       = 50.0f};
        k.setCamera(topp);
        k.setLayerVisible(lagerTräd, false);
        b.frame();
        const gfx::RenderStats bas = b.frame();
        k.setLayerVisible(lagerTräd, true);
        b.frame();
        const gfx::RenderStats med  = b.frame();
        const Pyramid          smal = pyramid(topp, b.aspekt, -1.0);
        std::uint64_t          väntad = 0;
        bool                   allaInne = true;
        for (const glaze::detail::InstanceCell& c : celler->cells) {
            allaInne = allaInne && smal.inne(c.box);
            const double d = avståndTillLåda(smal.öga, c.box);
            väntad += std::uint64_t{c.count} * konNivå[d <= kKonGräns ? 0 : 1];
        }
        check(allaInne && med.vertices - bas.vertices == väntad,
              "T detaljnivån per cell: ritade vertex = cellernas instanser × "
              "nivåns index (grindens avstånd)",
              std::to_string(med.vertices - bas.vertices) + " mot " +
                  std::to_string(väntad) + (allaInne ? "" : " (inte alla celler inne)"));
        // Nära marken tar närmaste cellerna nivå 0 — kontrollen att båda
        // nivåerna ritas någonstans.
    } else {
        check(false, "T trädens celler går att läsa");
    }
    k.setLayerVisible(lagerTräd, false);

    // =============================================================== PUNKTER
    t0 = Klocka::now();
    std::vector<glaze::Float3> lägen;
    std::vector<gfx::Color>    färger;
    std::vector<float>         höjd;
    lägen.reserve(static_cast<std::size_t>(punktAntal));
    färger.reserve(static_cast<std::size_t>(punktAntal));
    höjd.reserve(static_cast<std::size_t>(punktAntal));
    Slump rp{6000};
    for (int i = 0; i < punktAntal; ++i) {
        const float z = static_cast<float>(rp.mellan(0.0, 30.0));
        lägen.push_back({static_cast<float>(rp.mellan(-500, 500)),
                         static_cast<float>(rp.mellan(-500, 500)), z});
        färger.push_back(gfx::Color{static_cast<std::uint8_t>(60 + i % 120), 140, 90, 255});
        höjd.push_back(z);
    }
    const glaze::PointCloud moln(std::move(lägen), {kX0, kY0, 0.0}, std::move(färger),
                                 std::move(höjd));
    const int lagerPunkt = k.addLayer("punkter");
    k.addShape(lagerPunkt,
               glaze::Shape::pointCloud(
                   moln, {.size = glaze::px(2.0f),
                          .ramp = {{{0.0f, gfx::Color{40, 80, 220, 255}},
                                    {1.0f, gfx::Color{230, 60, 40, 255}}},
                                   0.0,
                                   30.0}}));
    const auto& molnData = glaze::detail::MeshAccess::data(moln);
    std::printf("  scen %d punkter i %zu chunkar — byggda på %.0f ms\n", punktAntal,
                molnData ? molnData->chunks.size() : std::size_t{0}, msSedan(t0));
    const auto punktKam = [](int i) {
        return glaze::Camera3D{.target    = {kX0, kY0, 10.0},
                               .distance  = 260.0,
                               .azimuth   = static_cast<float>(11 + 6 * i),
                               .elevation = 25.0f};
    };
    k.setCamera(punktKam(0));
    b.residens(tRes, resByte);
    std::printf("  tid  punkternas residens: %.1f MB, %.1f ms\n", resByte / 1.0e6, tRes);
    const SerieUt uP = serie(b, 60, punktKam);
    check(uP.maxByte == 0 && uP.buffertar == 0,
          "U punkterna: en kameraändring laddar upp 0 byte och skapar 0 buffertar",
          std::to_string(uP.maxByte) + " byte, " + std::to_string(uP.buffertar) +
              " buffertar");
    checkAnrop("punkterna (orbit)", uP, punktAnrop(static_cast<std::uint64_t>(punktAntal)));
    skrivTider("punkter (orbit)", uP.t, genomström(b, punktKam));
    if (molnData) {
        for (int i = 0; i < 4; ++i) {
            const glaze::Camera3D kam = punktKam(i * 15);
            k.setCamera(kam);
            k.setLayerVisible(lagerPunkt, false);
            b.frame();
            const gfx::RenderStats bas = b.frame();
            k.setLayerVisible(lagerPunkt, true);
            b.frame();
            const gfx::RenderStats med  = b.frame();
            const Pyramid          vid  = pyramid(kam, b.aspekt, 1.0);
            const Pyramid          smal = pyramid(kam, b.aspekt, -1.0);
            std::uint32_t          max = 0, min = 0;
            for (const glaze::detail::PointChunk& c : molnData->chunks) {
                max += vid.skär(c.bounds);
                min += smal.inne(c.bounds);
            }
            const std::uint32_t ritade = med.drawCalls - bas.drawCalls;
            check(ritade <= max && ritade >= min &&
                      ritade < static_cast<std::uint32_t>(molnData->chunks.size()),
                  "P punkterna, azimut " + std::to_string(static_cast<int>(kam.azimuth)) +
                      "°: ritade chunkar inom de som skär synpyramiden",
                  std::to_string(ritade) + " ritade, " + std::to_string(min) + "–" +
                      std::to_string(max) + " av " +
                      std::to_string(molnData->chunks.size()));
        }
    } else {
        check(false, "P molnets chunkar går att läsa");
    }

    // ================================================================ TERRÄNG
    // 121 rutor om 17 × 17 hörn — 11 × 11 plattor om 100 m, en
    // mjuk kulle — och vägar i ett Ground-lager, DRAPERADE på plattorna
    // (canvas/drape.h). Räknaren är draperingsmålens omritningar.
    k.setLayerVisible(lagerPunkt, false);
    t0 = Klocka::now();
    const int lagerTerräng = k.addLayer("terräng");
    k.setLayerPlacement(lagerTerräng, glaze::LayerPlacement::Terrain);
    constexpr int    kPlattor = 11, kHörn = 17;
    constexpr double kPlatta  = 100.0;
    const auto       markHöjd = [](double x, double y) {
        return 15.0 * std::sin(x / 140.0) * std::cos(y / 170.0) + 20.0;
    };
    // Plattans låda i världen — grindens egen, ur rutnätet.
    const auto plattLåda = [&](int ti, int tj) {
        const double x0 = kX0 + (ti - kPlattor / 2) * kPlatta - kPlatta / 2;
        const double y0 = kY0 + (tj - kPlattor / 2) * kPlatta - kPlatta / 2;
        return glaze::Box3{{x0, y0, 0.0}, {x0 + kPlatta, y0 + kPlatta, 40.0}};
    };
    for (int tj = 0; tj < kPlattor; ++tj)
        for (int ti = 0; ti < kPlattor; ++ti) {
            const glaze::Box3 låda = plattLåda(ti, tj);
            glaze::MeshDesc   d;
            d.origin = {låda.min.x, låda.min.y, 0.0};
            for (int j = 0; j < kHörn; ++j)
                for (int i = 0; i < kHörn; ++i) {
                    const double x = kPlatta * i / (kHörn - 1);
                    const double y = kPlatta * j / (kHörn - 1);
                    d.positions.push_back(
                        {static_cast<float>(x), static_cast<float>(y),
                         static_cast<float>(markHöjd(låda.min.x + x - kX0,
                                                     låda.min.y + y - kY0))});
                }
            for (int j = 0; j + 1 < kHörn; ++j)
                for (int i = 0; i + 1 < kHörn; ++i) {
                    const auto a = static_cast<std::uint32_t>(j * kHörn + i);
                    d.indices.insert(d.indices.end(),
                                     {a, a + 1, a + kHörn + 1, a, a + kHörn + 1,
                                      a + kHörn});
                }
            k.addShape(lagerTerräng,
                       glaze::Shape::mesh(glaze::Mesh(std::move(d)), {},
                                          {.color = gfx::Color{110, 150, 90, 255},
                                           .lit   = false}));
        }
    // D — plattorna: var och en ett segment (läget 12 byte, inga normaler).
    const std::uint64_t kravTerräng =
        static_cast<std::uint64_t>(kPlattor * kPlattor) *
        meshAnrop(static_cast<std::uint64_t>(2 * (kHörn - 1) * (kHörn - 1)), 12);
    const int lagerVägar = k.addLayer("vägar");
    for (int v = 0; v <= kPlattor; ++v) {
        const double l = (v - kPlattor / 2.0) * kPlatta + 37.0;
        const double e = kPlattor / 2.0 * kPlatta;
        k.addShape(lagerVägar,
                   glaze::Shape::polyline({{kX0 - e, kY0 + l}, {kX0 + e, kY0 + l}},
                                          {.paint = gfx::Color{200, 70, 40, 255},
                                           .width = glaze::px(3.0f)}));
        k.addShape(lagerVägar,
                   glaze::Shape::polyline({{kX0 + l, kY0 - e}, {kX0 + l, kY0 + e}},
                                          {.paint = gfx::Color{230, 200, 60, 255},
                                           .width = glaze::px(2.0f)}));
    }
    std::printf("  scen %d rutor om %d × %d hörn och %d vägar — byggda på %.0f ms\n",
                kPlattor * kPlattor, kHörn, kHörn, 2 * (kPlattor + 1), msSedan(t0));
    glaze::detail::DrapeCache& drapering = b.res->drapes();
    // Frames tills draperingen står still (ingen omritning) — svarar med
    // omritningarna och tiden.
    const auto stilla = [&](std::uint64_t& omritade, double& ms) {
        omritade = 0;
        ms       = 0.0;
        for (int i = 0; i < 20; ++i) {
            const std::uint64_t före = drapering.renders();
            double              t    = 0.0;
            b.frame(&t);
            ms += t;
            const std::uint64_t nu = drapering.renders() - före;
            omritade += nu;
            if (nu == 0)
                return true;
        }
        return false;
    };
    // UPPIFRÅN, ortografiskt: alla 121 plattor syns.
    const glaze::Camera3D uppifrån{.target      = {kX0, kY0, 20.0},
                                   .distance    = 2000.0,
                                   .elevation   = 90.0f,
                                   .projection  = glaze::Projection::Orthographic,
                                   .orthoHeight = 600.0};
    k.setCamera(uppifrån);
    std::uint64_t första = 0;
    double        tFörsta = 0.0;
    const bool    stod = stilla(första, tFörsta);
    int           minKlass = 1 << 30, maxKlass = 0;
    for (const auto& [id, kl] : drapering.classes()) {
        minKlass = std::min(minKlass, kl);
        maxKlass = std::max(maxKlass, kl);
    }
    std::printf("  tid  terrängens första drapering: %llu mål på %.1f ms (klass "
                "%d–%d px, %.1f MB)\n",
                static_cast<unsigned long long>(första), tFörsta, minKlass, maxKlass,
                drapering.bytes() / 1.0e6);
    check(stod && första == static_cast<std::uint64_t>(kPlattor * kPlattor),
          "terrängen: varje platta draperas en gång och sedan står draperingen "
          "still",
          std::to_string(första) + " omritningar");
    // PANORERING INOM KLASSEN (uppifrån står avtrycket still): 0 omritningar,
    // 0 byte, 0 buffertar.
    {
        const auto panorerad = [&](int i) {
            glaze::Camera3D c = uppifrån;
            c.target.x += 3.0 * (i + 1);
            c.target.y -= 2.0 * (i + 1);
            return c;
        };
        const std::uint64_t före = drapering.renders();
        const SerieUt       u    = serie(b, 20, panorerad);
        check(drapering.renders() == före && u.maxByte == 0 && u.buffertar == 0,
              "terrängen: en panorering inom klassen ritar inte om draperingen och "
              "laddar upp 0 byte",
              std::to_string(drapering.renders() - före) + " omritningar, " +
                  std::to_string(u.maxByte) + " byte, " + std::to_string(u.buffertar) +
                  " buffertar");
        checkAnrop("terrängen (panorering)", u, kravTerräng);
        skrivTider("terräng (panorering)", u.t, genomström(b, panorerad));
    }
    // UNDER TILT FRAM OCH TILLBAKA: perspektivet ändrar avtrycken, men
    // hysteresen (drape.h) håller klasserna — efter en vändning åt varje håll
    // ritas ingenting om.
    {
        const glaze::Camera3D lutad{.target    = {kX0, kY0, 20.0},
                                    .distance  = 700.0,
                                    .azimuth   = 25.0f,
                                    .elevation = 35.0f,
                                    .fov       = 45.0f};
        const auto svängd = [&](int i) {
            glaze::Camera3D c = lutad;
            c.target.x += (i % 2 ? 1.0 : -1.0) * 30.0;
            c.target.y += (i % 2 ? -1.0 : 1.0) * 20.0;
            return c;
        };
        std::uint64_t omr = 0;
        double        ms  = 0.0;
        k.setCamera(svängd(0));
        stilla(omr, ms);
        k.setCamera(svängd(1));
        stilla(omr, ms);
        const std::uint64_t före = drapering.renders();
        const SerieUt       u    = serie(b, 20, svängd);
        check(drapering.renders() == före && u.maxByte == 0,
              "terrängen: fram och tillbaka under tilt — hysteresen håller "
              "klasserna, 0 omritningar och 0 byte",
              std::to_string(drapering.renders() - före) + " omritningar, " +
                  std::to_string(u.maxByte) + " byte");
        checkAnrop("terrängen (under tilt)", u, kravTerräng);
    }
    // ZOOM 4× uppifrån: bara de plattor vars klass bytts ritas om — och de
    // är högst de synliga (grindens egen ruta: halva höjden 150 m kring
    // målet, bredden ur bildförhållandet). Med draperingens budget
    // räknas omritningarna över bilderna tills draperingen står still —
    // bilderna FÖNSTRET ber om (ritytan invaliderar sig själv så länge något
    // väntar, regel 1) — och M7: i VARJE bild under zoomen bär varje platta en
    // drapering (det gamla målet visas tills det nya är ritat).
    glaze::Camera3D inzoomad = uppifrån;
    inzoomad.orthoHeight = uppifrån.orthoHeight / 4.0;
    {
        std::uint64_t omr = 0;
        double        ms  = 0.0;
        k.setCamera(uppifrån);
        stilla(omr, ms);
        const auto          klasserFöre = drapering.classes();
        const std::uint64_t före        = drapering.renders();
        k.setCamera(inzoomad);
        double        tZoom          = 0.0;
        std::uint64_t byte           = 0;
        std::size_t   minstDraperade = klasserFöre.size();
        int           bilder         = 0;
        bool          stod           = false;
        for (; bilder < 20 && !stod; ++bilder) {
            const std::uint64_t r0 = drapering.renders();
            b.vänta();
            b.rr().resetStats();
            tZoom += b.rita();
            byte += b.rr().stats().bytesUploaded;
            minstDraperade = std::min(minstDraperade, drapering.classes().size());
            stod           = drapering.renders() == r0;
        }
        const std::uint64_t omritade = drapering.renders() - före;
        const auto          klasserEfter = drapering.classes();
        std::uint64_t       bytta = 0;
        for (const auto& [id, kl] : klasserEfter) {
            const auto it = klasserFöre.find(id);
            bytta += it == klasserFöre.end() || it->second != kl;
        }
        const double halvH = inzoomad.orthoHeight, halvB = halvH * b.aspekt;
        int          synliga = 0;
        for (int tj = 0; tj < kPlattor; ++tj)
            for (int ti = 0; ti < kPlattor; ++ti) {
                const glaze::Box3 l = plattLåda(ti, tj);
                synliga += l.max.x >= kX0 - halvB && l.min.x <= kX0 + halvB &&
                           l.max.y >= kY0 - halvH && l.min.y <= kY0 + halvH;
            }
        std::printf("  tid  zoom 4×: %llu draperingar om på %.1f ms i %d bilder (%d "
                    "synliga plattor)\n",
                    static_cast<unsigned long long>(omritade), tZoom, bilder, synliga);
        check(stod && omritade >= 1 && omritade == bytta &&
                  omritade <= static_cast<std::uint64_t>(synliga) && byte == 0,
              "terrängen: zoom 4× ritar om bara plattorna vars klass bytts (≤ de "
              "synliga, räknat över bilderna tills den står still) och laddar upp "
              "0 byte",
              std::to_string(omritade) + " omritade, " + std::to_string(bytta) +
                  " bytta klasser, " + std::to_string(synliga) + " synliga, " +
                  std::to_string(byte) + " byte, " + std::to_string(bilder) +
                  " bilder");
        check(klasserFöre.size() == static_cast<std::size_t>(kPlattor * kPlattor) &&
                  minstDraperade == klasserFöre.size(),
              "M7 i varje bild under zoomen bär varje platta en drapering (det "
              "gamla målet visas tills det nya är ritat)",
              "som minst " + std::to_string(minstDraperade) + " av " +
                  std::to_string(klasserFöre.size()));
    }

    // En PLAN PLATTA i terrängen: (x0, y0)–(x0 + sida, y0 + sida) i höjden z.
    const auto planPlatta = [](double x0, double y0, double sida, double z) {
        glaze::MeshDesc d;
        d.origin    = {x0, y0, z};
        const float s = static_cast<float>(sida);
        d.positions = {{0.0f, 0.0f, 0.0f}, {s, 0.0f, 0.0f}, {s, s, 0.0f}, {0.0f, s, 0.0f}};
        d.indices   = {0, 1, 2, 0, 2, 3};
        return glaze::Mesh(std::move(d));
    };
    // Väg 0 (den första vågräta) i en ny färg — samma form och samma
    // delar, så den skrivs på plats: chunkens version, och vägarnas chunk når
    // varje platta.
    const auto nyVäg = [&](std::uint8_t g) {
        const double l = -kPlattor / 2.0 * kPlatta + 37.0;
        const double e = kPlattor / 2.0 * kPlatta;
        return glaze::Shape::polyline({{kX0 - e, kY0 + l}, {kX0 + e, kY0 + l}},
                                      {.paint = gfx::Color{200, g, 40, 255},
                                       .width = glaze::px(3.0f)});
    };

    // M5 EN DOLD PLATTA RITAS INTE OM: ett eget Ground-lager
    // med en linje i hörnplattan (0, 0), utanför den inzoomade vyn. En
    // `setShape` på den — den når bara den dolda plattan — ritar ingenting
    // om; en panorering dit ritar om när plattan syns.
    {
        const glaze::Box3 hörn  = plattLåda(0, 0);
        const auto        linje = [&](double dy) {
            return glaze::Shape::polyline(
                {{hörn.min.x + 20.0, hörn.min.y + 30.0 + dy},
                 {hörn.max.x - 20.0, hörn.min.y + 50.0 + dy}},
                {.paint = gfx::Color{40, 90, 220, 255}, .width = glaze::px(3.0f)});
        };
        const int lagerHörn = k.addLayer("hörnet");
        k.addShape(lagerHörn, linje(0.0));
        k.setCamera(inzoomad);
        std::uint64_t omr = 0;
        double        ms  = 0.0;
        // Det nya lagret ändrar de synliga plattornas nyckel — de ritas om
        // först.
        const bool    lugn = stilla(omr, ms);
        k.setShape(lagerHörn, 0, linje(5.0));
        const bool          stod = stilla(omr, ms);
        const std::uint64_t dolt = omr;
        glaze::Camera3D     dit  = inzoomad;
        dit.target = {(hörn.min.x + hörn.max.x) / 2.0, (hörn.min.y + hörn.max.y) / 2.0,
                      20.0};
        k.setCamera(dit);
        stilla(omr, ms);
        check(lugn && stod && dolt == 0 && omr >= 1,
              "M5 en setShape som bara når en DOLD platta ritar ingenting om; en "
              "panorering dit ritar om när den syns",
              std::to_string(dolt) + " omritningar dold, " + std::to_string(omr) +
                  " efter panoreringen");
        k.setLayerVisible(lagerHörn, false);
    }

    // M6 DRAPERINGENS BUDGET PER BILD: under en låg tilt är
    // plattorna nära ögat stora (en platta under ögat får taket, 2048), så en
    // ändring som når varje platta kräver mer än en bilds budget. Den första
    // bilden ritar högst budgeten, alla ritas inom ⌈Σpx / budget⌉ + 1 bilder,
    // och sedan ingenting.
    {
        const glaze::Camera3D låg{.target    = {kX0, kY0, 20.0},
                                  .distance  = 150.0,
                                  .azimuth   = 10.0f,
                                  .elevation = 20.0f,
                                  .fov       = 45.0f};
        k.setCamera(låg);
        std::uint64_t omr = 0;
        double        ms  = 0.0;
        const bool    lugn   = stilla(omr, ms);
        const std::uint64_t budget = glaze::detail::drapeFrameBudget();
        k.setShape(lagerVägar, 0, nyVäg(120));
        std::uint64_t förstaPx = 0, summaPx = 0;
        int           bilderMed = 0;
        bool          stod      = false;
        for (int i = 0; i < 40 && !stod; ++i) {
            const std::uint64_t r0 = drapering.renders(), p0 = drapering.pixels();
            b.frame();
            const std::uint64_t px = drapering.pixels() - p0;
            stod                   = drapering.renders() == r0;
            if (stod)
                break;
            if (i == 0)
                förstaPx = px;
            summaPx += px;
            ++bilderMed;
        }
        const std::uint64_t tak = (summaPx + budget - 1) / budget + 1;
        std::printf("  tid  draperingens budget: %.1f Mi px över %d bilder (budgeten "
                    "%.1f Mi px per bild)\n",
                    summaPx / 1048576.0, bilderMed, budget / 1048576.0);
        check(lugn && stod && summaPx > budget,
              "M6 premissen: ändringen kräver mer än en bilds budget",
              std::to_string(summaPx) + " px mot " + std::to_string(budget));
        check(förstaPx <= budget && static_cast<std::uint64_t>(bilderMed) <= tak,
              "M6 den första bilden ritar högst budgeten och alla ritas inom "
              "⌈Σpx / budget⌉ + 1 bilder, sedan ingenting",
              std::to_string(förstaPx) + " px i den första, " +
                  std::to_string(bilderMed) + " bilder mot " + std::to_string(tak));
    }

    // M8 NYCKELN RÄKNAS BARA NÄR MARKEN ÄNDRATS: uppifrån
    // räknar en panorering inom klassen 0 nycklar, och en setShape på en väg
    // räknar nyckeln för varje platta (alla syns och har en drapering) EN gång
    // och sedan 0.
    {
        k.setCamera(uppifrån);
        std::uint64_t omr = 0;
        double        ms  = 0.0;
        stilla(omr, ms);
        const auto panorerad = [&](int i) {
            glaze::Camera3D c = uppifrån;
            c.target.x += 3.0 * (i + 1);
            c.target.y -= 2.0 * (i + 1);
            return c;
        };
        const std::uint64_t n0 = drapering.keysComputed();
        serie(b, 10, panorerad);
        const std::uint64_t panorerat = drapering.keysComputed() - n0;
        k.setCamera(uppifrån);
        stilla(omr, ms);
        k.setShape(lagerVägar, 0, nyVäg(70));
        const std::uint64_t n1 = drapering.keysComputed();
        b.frame();
        const std::uint64_t första = drapering.keysComputed() - n1;
        stilla(omr, ms);
        const std::uint64_t n2 = drapering.keysComputed();
        b.frame();
        b.frame();
        const std::uint64_t sedan = drapering.keysComputed() - n2;
        check(panorerat == 0 && första == static_cast<std::uint64_t>(kPlattor * kPlattor) &&
                  sedan == 0,
              "M8 en panorering inom klassen räknar 0 nycklar; en setShape på en väg "
              "räknar varje plattas nyckel en gång och sedan 0",
              std::to_string(panorerat) + " vid panoreringen, " + std::to_string(första) +
                  " efter ändringen, " + std::to_string(sedan) + " sedan");
    }

    // M10 ETIKETTERNA VID MARKHÖJD byggs om bara när terrängen
    // UNDER dem ändras: sex markörer i ett Ground-lager över platta (2, 2). En
    // ny platta långt därifrån (utanför rutnätet) bygger inte om dem; en platta
    // under dem gör det.
    {
        const glaze::Box3 under = plattLåda(2, 2);
        const int lagerEtiketter = k.addLayer("etiketter");
        for (int i = 0; i < 6; ++i)
            k.addShape(lagerEtiketter,
                       glaze::Shape::marker({under.min.x + 14.0 * (i + 1), under.min.y + 50.0},
                                            {.size = glaze::px(8.0f),
                                             .fill = gfx::Color{230, 40, 40, 255}}));
        k.setCamera(uppifrån);
        std::uint64_t omr = 0;
        double        ms  = 0.0;
        stilla(omr, ms);
        const std::uint64_t g0 = b.res->glyphBuilds();
        k.addShape(lagerTerräng,
                   glaze::Shape::mesh(planPlatta(kX0 + 1000.0, kY0 + 1000.0, kPlatta, 20.0),
                                      {}, {.color = gfx::Color{110, 150, 90, 255},
                                           .lit = false}));
        stilla(omr, ms);
        const std::uint64_t långtBort = b.res->glyphBuilds() - g0;
        k.addShape(lagerTerräng,
                   glaze::Shape::mesh(planPlatta(under.min.x, under.min.y, kPlatta, 60.0),
                                      {}, {.color = gfx::Color{110, 150, 90, 255},
                                           .lit = false}));
        stilla(omr, ms);
        const std::uint64_t underDem = b.res->glyphBuilds() - g0 - långtBort;
        check(långtBort == 0 && underDem >= 1,
              "M10 en ny platta långt från etiketterna bygger inte om dem; en under "
              "dem gör det",
              std::to_string(långtBort) + " byggen för plattan långt bort, " +
                  std::to_string(underDem) + " för plattan under");
        k.setLayerVisible(lagerEtiketter, false);
    }

    // =================================================== UPPLADDNINGENS ORDNING
    // M12 SYNLIGT FÖRST: 200 meshar bakom kameran och en i bild
    // — SIST i lagret —, med budgeten för en enda mesh (4 lägen × 12 byte + 6
    // index × 4). Den synliga ritas i FÖRSTA bilden, och de osynliga laddas
    // upp en per bild i de följande.
    {
        k.setLayerVisible(lagerTerräng, false);
        k.setLayerVisible(lagerVägar, false);
        const glaze::Camera3D kam{.target    = {kX0, kY0 + 3000.0, 0.0},
                                  .distance  = 40.0,
                                  .elevation = 30.0f};
        k.setCamera(kam);
        b.frame();
        const gfx::RenderStats bas = b.frame();
        const int lagerOrdning = k.addLayer("ordningen");
        k.setLayerVisible(lagerOrdning, false);
        for (int i = 0; i < 200; ++i)
            k.addShape(lagerOrdning, glaze::Shape::mesh(planPlatta(
                                         kX0 + 5.0 * i, kY0 - 4000.0, 2.0, 0.0)));
        k.addShape(lagerOrdning,
                   glaze::Shape::mesh(planPlatta(kX0 - 1.0, kY0 + 2999.0, 2.0, 0.0)));
        constexpr std::size_t kEnMesh = 4 * 12 + 6 * 4;
        const std::size_t     förval  = glaze::app().canvasUploadBudget();
        glaze::app().setCanvasUploadBudget(kEnMesh);
        k.setLayerVisible(lagerOrdning, true);
        const gfx::RenderStats första = b.frame();
        int                    bilder = 0;
        std::uint64_t          störst = 0;
        for (; bilder < 400; ++bilder) {
            const gfx::RenderStats s = b.frame();
            if (s.bytesUploaded == 0)
                break;
            störst = std::max(störst, s.bytesUploaded);
        }
        glaze::app().setCanvasUploadBudget(förval);
        check(första.drawCalls == bas.drawCalls + 1,
              "M12 den synliga meshen ritas i den FÖRSTA bilden fast den står sist "
              "bland 201 och budgeten räcker till en",
              std::to_string(första.drawCalls - bas.drawCalls) + " nya anrop");
        check(bilder >= 199 && bilder <= 202 && störst <= kEnMesh,
              "M12 de 200 osynliga laddas upp en per bild i de följande",
              std::to_string(bilder) + " bilder, största " + std::to_string(störst) +
                  " byte");
        k.setLayerVisible(lagerOrdning, false);
    }

    std::printf(g_fel ? "=== canvas_perf3d: %d FEL av %d ===\n"
                      : "=== canvas_perf3d: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    return g_fel ? 1 : 0;
}
