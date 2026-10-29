// pick_gate — 3D-PLOCKNINGENS KÄRNA, headless.
//
// Kör:  ./build/tests/pick_gate [-v] [--stolpe]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = ritytans del kunde
//       inte köras (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//
// `glaze::detail::pickMesh` (src/canvas/pick3d.h) är strålen mot en mesh
// genom BVH:n (bvh.h), och `rayPlane` strålen mot ett plan. Grinden mäter dem
// mot sin EGEN referens — en BRUTE FORCE som prövar VARJE triangel med
// grindens egen Möller–Trumbore, skriven ur artikeln (Möller & Trumbore 1997)
// och inte ur pick3d.cpp — och egna formler. Samma triangelprov i båda
// vägarna betyder samma avrundning, så B1 mäter BVH:n (bygget, lådorna,
// traversalen) och inte två räkneordningars avrundning: en annan ordning
// (Cramers regel) skiljer ~1e-11 i t på långa flisor, där
// determinanten är liten. Triangelprovet SJÄLVT mäts mot analytiska svar i
// B2, P1 och P4:
//
//   B1  BVH:n MOT BRUTE FORCE: 50 slumpmeshar (soppa, indexerad terräng,
//       kluster, platt i z = 0, långa flisor, generatorernas former, SWEREF,
//       2 000 identiska trianglar) × 1 000 strålar — riktade mot en punkt på
//       en slumpad triangel, slumpade, AXELPARALLELLA (±∞ i inversen), med en
//       komponent noll, inifrån molnet, i den platta meshens eget plan (NaN i
//       plattprovet) och med ett längsta avstånd. Samma träff eller samma
//       miss; samma triangel, och t och punkten inom 1e-9 relativt (gånger
//       max(1, avståndet)) — lika avstånd längs en delad kant avgörs av
//       lägsta index i båda.
//   B2  EXAKT RUTNÄT: ett platt rutnät i heltal, LIGGANDE med lodräta strålar
//       och STÅENDE med vågräta, i varje hörn, kantmitt och kvartspunkt —
//       räkningen är exakt i båda vägarna, så också kantträffarna och deras
//       lika avstånd ska vara identiska. Det stående lägger strålarna i
//       lådornas sidoplan på z, den axel plattprovet prövar sist.
//   B3  BVH:ns FORM: varje nods låda rymmer sina barns, varje lövs trianglar
//       ligger i lövets låda, `prims` är en permutation och djupet är ≤ taket.
//   P1  ORIGINALETS TRIANGELINDEX genom ett mesh med ALLA strömmar (104 byte
//       per läge) som delas i två segment av den riktiga gränsen 64 MiB, två
//       överlappande nivåer och ett intervall som går över segmentgränsen:
//       träffen bär originalets index också bortom gränsen.
//   P2  NIVÅN och INTERVALLET: hela meshen, nivå 0, nivå 1, kNoLevel, en nivå
//       som inte finns och ett intervall som inte är hela trianglar (tomt),
//       och `lodLevel` mot avstånden.
//   P3  `opaqueOnly`: en genomskinlig triangel (alfa 110) och en
//       med ETT genomskinligt hörn hoppas över, en mesh utan färgström är
//       ogenomskinlig, och `setColors` slår igenom utan ombyggnad.
//   P4  LÄNGSTA AVSTÅNDET i riktningens enhet, och avståndet och t med en
//       riktning som inte är enhetlig.
//   P5  `setPositions`: BVH:n byggs om (en triangel flyttad 50 m åt sidan
//       träffas där, inte där den var).
//   P6  `keepCpuCopy = false`: ingen träff och inget bygge.
//   P7  TRÅDARNA: fyra trådar kör `preparePicking` samtidigt — ett bygge, och
//       plockningen efteråt använder det.
//   P8  STRÅLEN MOT PLANET: en träff mot grindens formel (också ett lutat plan
//       på SWEREF och en riktning som inte är enhetlig), parallell och bakom
//       ger ingenting, början i planet ger t = 0.
//   T   `--stolpe`: BVH-byggets TID för en syntetisk stolpe med ≈ 200 k
//       trianglar — verklighetens tak — platta, icke-indexerade och med färg
//       per triangel som i referensen.
//       Utskriven, INTE grindad och inte i ctest.
//
// RITYTANS PLOCKNING — med ett DOLT fönster och en
// riktig `glaze::Canvas`; referensen är grindens EGEN hålkamera ur konventionen i
// glaze/canvas/camera.h och placeringens ur style3d.h (+y mot bäringen yaw,
// skalan före vridningen), aldrig ritytans formel:
//   R1  `rayAt` ∘ `project`: strålen genom projektionen går genom punkten,
//       och projektionen är hålkamerans — perspektiv, ortografiskt, med roll,
//       vid SWEREF — och kartkamerans tiltad lika med `toLocal`.
//   R2  `worldPerPixel` mot en DERIVATA: två punkter längs kamerans högeraxel
//       (samma djup, så projektionen är linjär där) — perspektiv,
//       ortografiskt och kartkameran.
//   R3  `visibleRegion` mot en analytisk TRAPETS: skärmhörnens strålar mot
//       marken; och med ett djupstak ligger hörnen på det.
//   R4  HÖRN OCH KANT inom N px, och INTE vid N + 0,3 — en placerad mesh
//       (vriden, skalad, vid SWEREF), toleransen i px, mm och världsenheter.
//   R5  PUNKTER: inom toleransen + halva storleken (och inte utanför),
//       `maxDistance`, och punktens plats i INDATAN genom ett moln som delats
//       i chunkar.
//   R6  ETIKETT I 3D: en `text3` i ett World-lager träffas som den ritades.
//   R7  `heightAt` på en ANALYTISK LUTANDE terrängmesh (z = 0,1x − 0,05y + 3
//       vid SWEREF), ett hål och utanför.
//   R8  YTAN genom placeringen: originalets triangel, punkten, normalen
//       (vriden och skalad som referensen), avståndet; INSTANSEN; sorteringen och
//       `castRay`; `occlusion` (också ett hörn bakom en vägg, längs sin
//       synlinje) och `opaqueOnly` mot stilens alfa.
//   R8b MARKEN SKYMMER under sig när ett Ground-lager MED INNEHÅLL ritas i
//       3D-målet (markdjupet) — inte ett tomt, och inte utan ett.
//   R9  ROUTNINGEN TILL VYN: i en del-vys ruta träffas vyns lager med vyns
//       kamera (`HitResult::view`) — rymdens kub längs vyns stråle och en
//       2D-ruta på marken genom vyns projektion —, utanför den huvudvyns.
//   R10 DEN RITADE DETALJNIVÅN: nära den fina nivåns triangel, fjärran den
//       grovas — avståndet till den placerade lådan, som ritningen.
//   R11 LINJEN I RYMDEN inom halva bredden + toleransen (segmentet), inte
//       utanför.
//   R12 SKYMNINGEN LÄNGS SYNLINJEN: ett hörn precis bredvid en väggkant syns
//       fast pekarens stråle når väggen först — det närmaste SYNLIGA hörnet
//       väljs, inte det närmaste på skärmen (som ligger bakom väggen).
//   R13 INSTANSENS PLATS I INDATAN genom cellerna (900 lådor i omvänd
//       ordning — Morton-ordningen är en annan).
//   R-skym SKYMMARNA FRAMIFRÅN: en vägg och 200 meshar bakom
//       den längs strålen — efter `castRay` med skymning är ingen av deras
//       BVH:er byggd (de prövades aldrig) men väggens är det; utan skymning
//       träffas alla 201.
//   R-snap LAGRETS SNAP: en höjdmodell i ett Terrain-lager ger
//       med snap hörnet, kanten och EN yta; med `setLayerSnap(0, false)`
//       inget hörn och ingen kant därifrån och samma yta — också mot ytan
//       plockad på nytt med `opaqueOnly`, som inte återanvänder terrängens —
//       och en stolpes hörn i ett annat lager träffas fortfarande.
//
// VAD DEN INTE MÄTER: segmenten påverkar inte plockningen per konstruktion —
// den läser CPU-kopian i originalets ordning — så P1 visar att det håller
// också när meshen ÄR delad, men en sabotör som rör segmenttabellen fälls i
// geom_gate (M1–M4), inte här. Ritytans del mäter inte 2D-formerna på
// terrängens träffpunkt (draperingen), de
// skärmkonstanta meshernas plockning eller instansernas detaljnivå per cell
// — och inte GPU:n: plockningen är CPU och BVH, och ingen pixel jämförs.
// Inte heller att terrängens yta bara plockas EN gång i `hitTest`:
// det ger ingen skillnad i svaret, och R-snap vaktar bara att svaret är
// detsamma.
//
// SABOTAGEN, prövade i en privat kopia, alla fällda:
//   * nodlådorna inte utvidgade till sina trianglar — byggda ur mittpunkterna
//     (B1, B2, B3, P1–P5, P7)
//   * plattprovets NaN-regel borta — std::max/std::min, så att NaN sprids (B2
//     det stående; av det liggande fälldes det INTE: de lodräta strålarnas
//     NaN ligger på x och y, och z-axeln skriver över den — därav det stående)
//   * opak-filtret ignorerat (P3)
//   * intervallet ignorerat i traversalen (P1, P2)
//   * nivån ignorerad (P2)
//   * BVH:n inte ombyggd efter setPositions (P5)
// Och RITYTANS DEL, prövade i en privat kopia,
// alla fällda:
//   * konen i traverseNear inte utvidgad med toleransen (R4, R12)
//   * instansens plats i indatan inte ommappad genom cellerna (R13)
//   * punktens plats i indatan inte ommappad genom chunkarna (R5)
//   * opak-filtret (stilens alfa) ignorerat (R8)
//   * en tolerans i px tolkad som världsenheter (R4)
//   * vyn under pekaren ignorerad (R9), och vyns 2D-projektion ersatt med
//     huvudvyns (R9, rutan)
//   * detaljnivån ignorerad (R10)
//   * normalen genom placeringen utan invers-transponat (R8)
//   * maxDistance ignorerat för punkterna (R5)
//   * heightAt ser alla lager och inte bara Terrain-lagren (R7)
//   * markdjupet skymmer inte (R8b)
//   * linjens bredd ignorerad (R11)
//   * castRay normaliserar inte riktningen (R8)
//   * worldPerPixel per fysisk px i stället för per logisk (R2)
//   * synlinjen är pekarens stråle och inte elementets egen (R12)
// Och SKYMNING OCH SNAP (skrivna): skymmarna prövade i lagrens
// ordning utan tak och utan utgång
// (en enkel loop) och `castRay` utan skymmarens tak i huvudloopen (R-skym — de
// 200 BVH:erna byggs); snap-flaggan ignorerad i träffprovet (R-snap — hörnet
// 60 från lager 0); terrängens yta både återanvänd och plockad igen (R-snap —
// två ytor) och inte återanvänd alls (R-snap — ingen yta); formplatsens låda
// utan konen i `pickShape3D(post, …)` (R4 — hörnet strax utanför lådan
// träffas inte).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <gfx/geom3d.h>
#include <glaze/glaze.h>

#include "canvas/bvh.h"
#include "canvas/drape.h"   // terrängens rutnät (HI)
#include "canvas/mesh_data.h"
#include "canvas/pick3d.h"
#include "glaze/canvas/mesh.h"

namespace {

using glaze::detail::MeshAccess;
using glaze::detail::MeshData;
using glaze::detail::pickMesh;

int    g_fel     = 0;
int    g_fall    = 0;
bool   g_verbose = false;
double g_värstB1 = 0.0;   // den största punktavvikelsen i B1 (utskriven med -v)

void check(bool ok, const std::string& vad, const std::string& fick = {})
{
    ++g_fall;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", vad.c_str());
        return;
    }
    std::printf("  FEL  %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ",
                fick.c_str());
    ++g_fel;
}

std::string tal(double v)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

// Ett slumptal i [0, 1) ur mt19937 (fördelningarna är inte fast definierade
// i standarden, motorn är det).
double slump(std::mt19937& g)
{
    return g() / 4294967296.0;
}

const MeshData& data(const glaze::Mesh& m)
{
    return *MeshAccess::data(m);
}

// ------------------------------------------------------------ grindens referens

struct D3 {
    double x = 0.0, y = 0.0, z = 0.0;
};
D3 operator-(D3 a, D3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
D3 operator+(D3 a, D3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
D3 operator*(D3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double prick(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
D3     kors(D3 a, D3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
D3 d3(const glaze::Float3& f) { return {f.x, f.y, f.z}; }

// MÖLLER–TRUMBORE ur artikeln: P = D × E2, det = E1 · P, T = O − V0,
// u = (T · P) / det, Q = T × E1, v = (D · Q) / det, t = (E2 · Q) / det.
// Båda sidorna träffas (ingen gallring av baksidor), parallell (det = 0)
// missar.
bool mollerTrumbore(D3 o, D3 d, D3 v0, D3 v1, D3 v2, double& t)
{
    const D3     e1 = v1 - v0, e2 = v2 - v0;
    const D3     pv = kors(d, e2);
    const double det = prick(e1, pv);
    if (det == 0.0)
        return false;
    const double inv = 1.0 / det;
    const D3     tv  = o - v0;
    const double u   = prick(tv, pv) * inv;
    if (u < 0.0 || u > 1.0)
        return false;
    const D3     qv = kors(tv, e1);
    const double v  = prick(d, qv) * inv;
    if (v < 0.0 || u + v > 1.0)
        return false;
    t = prick(e2, qv) * inv;
    return t >= 0.0;
}

struct Facit {
    bool          finns = false;
    std::uint32_t tri   = 0;
    double        t     = 0.0;
};

// BRUTE FORCE: varje triangel i [t0, t1) i meshens lokala rymd (början minus
// origot), närmaste träffen inom tMax, lika avstånd → lägsta index.
Facit bruteForce(const MeshData& d, const glaze::Ray3& r, double tMax,
                 std::uint32_t t0, std::uint32_t t1, bool opaque)
{
    const D3 o{r.origin.x - d.origin.x, r.origin.y - d.origin.y,
               r.origin.z - d.origin.z};
    const D3 dir{r.direction.x, r.direction.y, r.direction.z};
    Facit    f;
    for (std::uint32_t tri = t0; tri < t1; ++tri) {
        const std::uint32_t a = d.isIndexed() ? d.indices[3 * tri] : 3 * tri;
        const std::uint32_t b = d.isIndexed() ? d.indices[3 * tri + 1] : 3 * tri + 1;
        const std::uint32_t c = d.isIndexed() ? d.indices[3 * tri + 2] : 3 * tri + 2;
        if (opaque && !d.colors.empty() &&
            (d.colors[a].a < 255 || d.colors[b].a < 255 || d.colors[c].a < 255))
            continue;
        double t = 0.0;
        if (!mollerTrumbore(o, dir, d3(d.positions[a]), d3(d.positions[b]),
                            d3(d.positions[c]), t) ||
            t > tMax)
            continue;
        if (!f.finns || t < f.t || (t == f.t && tri < f.tri))
            f = {true, tri, t};
    }
    return f;
}

// Jämför kärnans svar med referensen. En annan triangel godtas bara på lika
// avstånd (en delad kant), och räknas.
bool samma(const MeshData& d, const glaze::Ray3& r,
           const std::optional<glaze::detail::MeshHit>& h, const Facit& f,
           int& lika, std::string& varför)
{
    if (h.has_value() != f.finns) {
        varför = h ? "kärnan träffade " + std::to_string(h->triangle) +
                         ", facit ingenting"
                   : "kärnan missade, facit träffade " + std::to_string(f.tri);
        return false;
    }
    if (!f.finns)
        return true;
    const double skala = std::max(1.0, f.t);
    if (h->triangle != f.tri) {
        // Avståndet till kärnans triangel enligt referensen.
        const D3 o{r.origin.x - d.origin.x, r.origin.y - d.origin.y,
                   r.origin.z - d.origin.z};
        const D3 dir{r.direction.x, r.direction.y, r.direction.z};
        const std::uint32_t tri = h->triangle;
        double              t   = 0.0;
        const bool          ok  = mollerTrumbore(
            o, dir, d3(d.positions[d.corner(tri, 0)]),
            d3(d.positions[d.corner(tri, 1)]), d3(d.positions[d.corner(tri, 2)]), t);
        if (!ok || std::abs(t - f.t) > 1e-12 * skala) {
            varför = "triangel " + std::to_string(h->triangle) + " mot facits " +
                     std::to_string(f.tri) + " (t " + tal(h->t) + " mot " +
                     tal(f.t) + ")";
            return false;
        }
        ++lika;
    }
    const glaze::Point3 p{r.origin.x + f.t * r.direction.x,
                          r.origin.y + f.t * r.direction.y,
                          r.origin.z + f.t * r.direction.z};
    const double avvik = std::max({std::abs(h->point.x - p.x),
                                   std::abs(h->point.y - p.y),
                                   std::abs(h->point.z - p.z)});
    g_värstB1 = std::max(g_värstB1, avvik);
    // Relativt avståndet (minst 1e-9): på x86-64 är vägarna bit-lika, men en
    // plattform som drar ihop a·b + c till FMA (ARM) avrundar annorlunda, och
    // en lång flisa förstärker det till ~1e-11 relativt.
    if (std::abs(h->t - f.t) > 1e-9 * skala ||
        avvik > 1e-9 * std::max(1.0, h->distance)) {
        varför = "punkten avviker " + tal(avvik) + ", t " + tal(h->t) +
                 " mot " + tal(f.t);
        return false;
    }
    return true;
}

// ------------------------------------------------------------ slumpmesharna

glaze::Float3 f3(double x, double y, double z)
{
    return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}

// En triangel kring mitten c med kanter upp till s.
void triangel(std::mt19937& g, std::vector<glaze::Float3>& p, D3 c, double s)
{
    for (int k = 0; k < 3; ++k)
        p.push_back(f3(c.x + (slump(g) - 0.5) * s, c.y + (slump(g) - 0.5) * s,
                       c.z + (slump(g) - 0.5) * s));
}

// Mesh nummer k av 50 — slaget följer k.
glaze::MeshDesc slumpmesh(std::mt19937& g, int k, std::string& slag)
{
    glaze::MeshDesc d;
    const int       n = k % 10 == 0 ? 1 + k / 10 : 200 + static_cast<int>(slump(g) * 2800.0);
    switch (k % 8) {
    case 0:   // soppa
        slag = "soppa";
        for (int i = 0; i < n; ++i)
            triangel(g, d.positions, {slump(g) * 100, slump(g) * 100, slump(g) * 100},
                     0.1 + slump(g) * 20.0);
        break;
    case 1: {   // indexerad terräng
        slag            = "terräng";
        const int sida  = 2 + static_cast<int>(std::sqrt(n / 2.0));
        for (int y = 0; y < sida; ++y)
            for (int x = 0; x < sida; ++x)
                d.positions.push_back(f3(x * 2.0, y * 2.0, slump(g) * 3.0));
        for (int y = 0; y + 1 < sida; ++y)
            for (int x = 0; x + 1 < sida; ++x) {
                const auto a = static_cast<std::uint32_t>(y * sida + x);
                const auto s = static_cast<std::uint32_t>(sida);
                d.indices.insert(d.indices.end(), {a, a + 1, a + s + 1, a, a + s + 1, a + s});
            }
        break;
    }
    case 2:   // kluster
        slag = "kluster";
        for (int i = 0; i < n; ++i) {
            const int kl = i % 4;
            triangel(g, d.positions,
                     {kl * 300.0 + slump(g) * 5, (kl % 2) * 200.0 + slump(g) * 5,
                      slump(g) * 5},
                     0.05 + slump(g) * 2.0);
        }
        break;
    case 3:   // platt i z = 0
        slag = "platt";
        for (int i = 0; i < n; ++i) {
            const D3 c{slump(g) * 100, slump(g) * 100, 0.0};
            for (int j = 0; j < 3; ++j)
                d.positions.push_back(f3(c.x + (slump(g) - 0.5) * 8,
                                         c.y + (slump(g) - 0.5) * 8, 0.0));
        }
        break;
    case 4:   // långa flisor
        slag = "flisor";
        for (int i = 0; i < n; ++i) {
            const D3 c{slump(g) * 100, slump(g) * 100, slump(g) * 100};
            const D3 l{(slump(g) - 0.5) * 80, (slump(g) - 0.5) * 80, (slump(g) - 0.5) * 80};
            d.positions.push_back(f3(c.x, c.y, c.z));
            d.positions.push_back(f3(c.x + l.x, c.y + l.y, c.z + l.z));
            d.positions.push_back(f3(c.x + l.x * 0.5 + slump(g) * 0.05,
                                     c.y + l.y * 0.5, c.z + l.z * 0.5 + 0.05));
        }
        break;
    case 5: {   // generatorernas former i en mesh
        slag = "former";
        gfx::MeshGeometry geo;
        gfx::box({0, 0, 0}, {4, 3, 2}, geo);
        gfx::sphere({10, 0, 2}, 2.0, geo, 3 + k % 20, 2 + k % 12);
        gfx::cylinder({0, 8, 0}, {3, 9, 6}, 0.7, geo, 3 + k % 14);
        std::vector<gfx::Vec3d> väg;
        for (int i = 0; i < 20; ++i)
            väg.push_back({i * 1.5, -6.0 + std::sin(i * 0.4), 3.0 + 0.1 * i * i});
        gfx::tube(väg, 0.3, geo);
        for (const gfx::Vec3d& p : geo.positions)
            d.positions.push_back(f3(p.x, p.y, p.z));
        d.indices = geo.indices;
        break;
    }
    case 6:   // SWEREF
        slag     = "SWEREF";
        d.origin = {674123.25, 6580456.5, 31.0};
        for (int i = 0; i < n; ++i)
            triangel(g, d.positions, {slump(g) * 500, slump(g) * 500, slump(g) * 50},
                     0.5 + slump(g) * 10.0);
        break;
    default:   // identiska trianglar (alla mittpunkter i en punkt)
        slag = "identiska";
        for (int i = 0; i < 2000; ++i) {
            d.positions.push_back(f3(0, 0, 0));
            d.positions.push_back(f3(1, 0, 0));
            d.positions.push_back(f3(0, 1, 0));
        }
        break;
    }
    return d;
}

// En stråle till mesh m, slag efter i.
glaze::Ray3 slumpstråle(std::mt19937& g, const MeshData& d, int i, double& tMax)
{
    const glaze::Box3 b = d.bounds;
    const D3 lo{b.min.x, b.min.y, b.min.z}, hi{b.max.x, b.max.y, b.max.z};
    // Strålarnas början i lådan uppblåst med `marg` — med en minsta tjocklek
    // per axel, så att en PLATT mesh också får strålar utanför sitt plan.
    const D3     e0    = hi - lo;
    const double tjock = 0.25 * std::max({e0.x, e0.y, e0.z});
    const D3     ext{std::max(e0.x, tjock), std::max(e0.y, tjock),
                     std::max(e0.z, tjock)};
    const D3     mitt   = (lo + hi) * 0.5;
    const auto   punktI = [&](double marg) {
        return D3{mitt.x + (slump(g) - 0.5) * ext.x * (1 + 2 * marg),
                  mitt.y + (slump(g) - 0.5) * ext.y * (1 + 2 * marg),
                  mitt.z + (slump(g) - 0.5) * ext.z * (1 + 2 * marg)};
    };
    tMax = std::numeric_limits<double>::infinity();
    D3 o, dir;
    switch (i % 10) {
    case 0: case 1: case 2: case 3: {   // mot en punkt på en slumpad triangel
        const std::uint32_t tri = static_cast<std::uint32_t>(slump(g) * d.triangleCount());
        double u = slump(g), v = slump(g);
        if (u + v > 1.0) {
            u = 1.0 - u;
            v = 1.0 - v;
        }
        const D3 org{d.origin.x, d.origin.y, d.origin.z};
        const D3 a = d3(d.positions[d.corner(tri, 0)]) + org;
        const D3 bb = d3(d.positions[d.corner(tri, 1)]) + org;
        const D3 c = d3(d.positions[d.corner(tri, 2)]) + org;
        const D3 mål = a * (1 - u - v) + bb * u + c * v;
        o   = punktI(0.5);
        dir = mål - o;
        break;
    }
    case 4: case 5:   // slumpad
        o   = punktI(0.3);
        dir = {slump(g) - 0.5, slump(g) - 0.5, slump(g) - 0.5};
        break;
    case 6: {   // axelparallell
        o            = punktI(0.2);
        const int ax = static_cast<int>(slump(g) * 6.0);
        const double s = ax % 2 ? -1.0 : 1.0;
        dir = ax / 2 == 0 ? D3{s, 0, 0} : ax / 2 == 1 ? D3{0, s, 0} : D3{0, 0, s};
        break;
    }
    case 7: {   // en komponent noll
        o            = punktI(0.2);
        dir          = {slump(g) - 0.5, slump(g) - 0.5, slump(g) - 0.5};
        const int ax = static_cast<int>(slump(g) * 3.0);
        (ax == 0 ? dir.x : ax == 1 ? dir.y : dir.z) = 0.0;
        break;
    }
    case 8:   // inifrån, med ett längsta avstånd
        o    = punktI(0.0);
        dir  = {slump(g) - 0.5, slump(g) - 0.5, slump(g) - 0.5};
        tMax = 0.2 + slump(g) * 2.0;
        break;
    default:   // i lådans undre plan (den platta meshens eget), annars slumpad
        o   = punktI(0.2);
        o.z = lo.z;
        dir = {slump(g) - 0.5, slump(g) - 0.5, 0.0};
        break;
    }
    return {{o.x, o.y, o.z}, {dir.x, dir.y, dir.z}};
}

// ------------------------------------------------------------------ B1
void fallMotBruteForce()
{
    std::mt19937 g(11u);
    int          fel = 0, träffar = 0, missar = 0, lika = 0, strålar = 0;
    std::string  första;
    for (int k = 0; k < 50; ++k) {
        std::string       slag;
        const glaze::Mesh m(slumpmesh(g, k, slag));
        if (!m.valid()) {
            ++fel;
            if (första.empty())
                första = "mesh " + std::to_string(k) + " (" + slag + ") ogiltig";
            continue;
        }
        const MeshData& d = data(m);
        for (int i = 0; i < 1000; ++i) {
            double            tMax = 0.0;
            const glaze::Ray3 r    = slumpstråle(g, d, i, tMax);
            const double dl = std::sqrt(r.direction.x * r.direction.x +
                                        r.direction.y * r.direction.y +
                                        r.direction.z * r.direction.z);
            if (!(dl > 0.0))
                continue;
            glaze::detail::MeshPick opt;
            opt.maxDistance = std::isfinite(tMax) ? tMax * dl : 0.0;
            const auto  h = pickMesh(m, r, opt);
            const Facit f = bruteForce(d, r, tMax, 0, d.triangleCount(), false);
            std::string varför;
            ++strålar;
            if (!samma(d, r, h, f, lika, varför)) {
                ++fel;
                if (första.empty())
                    första = "mesh " + std::to_string(k) + " (" + slag + "), stråle " +
                             std::to_string(i) + ": " + varför;
            }
            (f.finns ? träffar : missar) += 1;
        }
    }
    check(fel == 0,
          "B1 50 slumpmeshar × 1 000 strålar: BVH:n ger samma triangel och punkt "
          "(inom 1e-9 relativt) som grindens brute force",
          std::to_string(fel) + " fel — " + första);
    // 20 000 strålar är riktade mot en punkt PÅ en triangel och ska träffa —
    // också i de platta mesharna; marginalen är för en avrundning i en
    // kant på en annan plattform.
    check(träffar > 19900 && missar > 5000,
          "B1 urvalet: de riktade strålarna träffar (också platta meshar), och "
          "det finns missar",
          std::to_string(träffar) + " träffar, " + std::to_string(missar) +
              " missar");
    if (g_verbose)
        std::printf("       %d strålar, %d träffar, %d missar, %d lika avstånd, "
                    "största punktavvikelse %.3g\n",
                    strålar, träffar, missar, lika, g_värstB1);
}

// ------------------------------------------------------------------ B2
//
// Rutnätet två gånger: LIGGANDE i z = 0 med lodräta strålar och STÅENDE i
// y = 0 med vågräta strålar längs −y. Det stående mäter plattprovets NaN-regel:
// en stråle i en lådas sidoplan ger 0 · ∞ = NaN på den axeln, och för de
// lodräta strålarna är det x och y — som z-axeln, prövad SIST i rayHitsNode,
// skriver över om NaN sprids (std::max/std::min i stället för regeln fälldes
// inte av det liggande). De vågräta strålarna ger NaN på z.
void exaktRutnät(bool stående)
{
    // 20 × 20 rutor om 1, indexerat (delade kanter).
    glaze::MeshDesc d;
    for (int y = 0; y <= 20; ++y)
        for (int x = 0; x <= 20; ++x)
            d.positions.push_back(stående ? f3(x, 0, y) : f3(x, y, 0));
    for (std::uint32_t y = 0; y < 20; ++y)
        for (std::uint32_t x = 0; x < 20; ++x) {
            const std::uint32_t a = y * 21 + x;
            d.indices.insert(d.indices.end(), {a, a + 1, a + 22, a, a + 22, a + 21});
        }
    const glaze::Mesh m(std::move(d));
    if (!m.valid()) {
        check(false, "B2 rutnätet är en giltig mesh");
        return;
    }
    int         fel = 0, prov = 0, lika = 0;
    std::string första;
    for (int y = 0; y <= 80; ++y)
        for (int x = 0; x <= 80; ++x) {
            // Kvartspunkterna, också rutnätets ytterkant — exakt i double.
            const glaze::Ray3 r =
                stående ? glaze::Ray3{{x * 0.25, 10.0, y * 0.25}, {0.0, -1.0, 0.0}}
                        : glaze::Ray3{{x * 0.25, y * 0.25, 10.0}, {0.0, 0.0, -1.0}};
            const auto        h = pickMesh(m, r);
            const Facit f = bruteForce(data(m), r, std::numeric_limits<double>::infinity(),
                                       0, data(m).triangleCount(), false);
            std::string varför;
            ++prov;
            if (!samma(data(m), r, h, f, lika, varför) ||
                (h && h->triangle != f.tri)) {
                ++fel;
                if (första.empty())
                    första = "(" + tal(x * 0.25) + ", " + tal(y * 0.25) + "): " + varför;
            }
        }
    check(fel == 0,
          std::string(stående ? "B2 vågräta strålar mot det STÅENDE"
                              : "B2 lodräta strålar mot det liggande") +
              " heltalsrutnätet i varje hörn, kantmitt och kvartspunkt: "
              "identiskt med brute force, också på kanterna",
          std::to_string(fel) + " av " + std::to_string(prov) + " — " + första);
    if (stående)
        return;
    // En stråle som LIGGER i rutnätets plan (z = 0) träffar ingenting — men
    // plattprovets NaN får inte heller göra något annat.
    const glaze::Ray3 iPlanet{{-1.0, 5.5, 0.0}, {1.0, 0.0, 0.0}};
    check(!pickMesh(m, iPlanet).has_value(),
          "B2 en stråle i rutnätets eget plan träffar ingenting (parallell)");
}

void fallExaktRutnät()
{
    exaktRutnät(false);
    exaktRutnät(true);
}

// ------------------------------------------------------------------ B3
void fallBvhFormen()
{
    std::mt19937 g(33u);
    bool         rätt = true;
    std::string  varför;
    for (int k = 0; k < 8 && rätt; ++k) {
        std::string       slag;
        const glaze::Mesh m(slumpmesh(g, 10 * k + 1 + k, slag));
        if (!m.valid())
            continue;
        const MeshData& d = data(m);
        m.preparePicking();
        const glaze::detail::Bvh& bvh = d.picking->bvh;
        // prims en permutation
        std::vector<std::uint32_t> p = bvh.prims;
        std::sort(p.begin(), p.end());
        for (std::uint32_t i = 0; i < p.size() && rätt; ++i)
            rätt = p[i] == i;
        rätt = rätt && p.size() == d.triangleCount();
        if (!rätt) {
            varför = slag + ": prims är ingen permutation";
            break;
        }
        // Lådorna och djupet, med en egen vandring.
        struct Steg {
            std::uint32_t nod;
            int           djup;
        };
        std::vector<Steg> stack{{0, 1}};
        while (!stack.empty() && rätt) {
            const Steg s = stack.back();
            stack.pop_back();
            const glaze::detail::BvhNode& n = bvh.nodes[s.nod];
            if (s.djup > glaze::detail::kBvhMaxDepth) {
                rätt   = false;
                varför = slag + ": djupet " + std::to_string(s.djup);
                break;
            }
            const auto inne = [&](const float lo[3], const float hi[3]) {
                for (int a = 0; a < 3; ++a)
                    if (lo[a] < n.lo[a] || hi[a] > n.hi[a])
                        return false;
                return true;
            };
            if (n.count > 0) {
                for (std::uint32_t i = n.first; i < n.first + n.count && rätt; ++i) {
                    const std::uint32_t tri = bvh.prims[i];
                    for (std::uint32_t c = 0; c < 3; ++c) {
                        const glaze::Float3& q = d.positions[d.corner(tri, c)];
                        const float          f[3] = {q.x, q.y, q.z};
                        if (!inne(f, f)) {
                            rätt   = false;
                            varför = slag + ": triangel " + std::to_string(tri) +
                                     " sticker ut ur sitt löv";
                        }
                    }
                }
            } else {
                for (std::uint32_t c = n.first; c < n.first + 2; ++c) {
                    if (!inne(bvh.nodes[c].lo, bvh.nodes[c].hi)) {
                        rätt   = false;
                        varför = slag + ": ett barn sticker ut ur sin förälder";
                    }
                    stack.push_back({c, s.djup + 1});
                }
            }
        }
    }
    check(rätt,
          "B3 BVH:ns form: varje låda rymmer sina barn och sina trianglar, prims "
          "en permutation, djupet inom taket",
          varför);
}

// ------------------------------------------------------------------ P1–P2
void fallOriginalindex()
{
    // ALLA strömmar (104 byte per läge) och 220 000 platta trianglar: över
    // 64 MiB, alltså två segment. Nivå 0 = trianglarna 0 … 109 999 i z = 0,
    // nivå 1 = 110 000 … 219 999 i z = 0,5, ovanpå samma rutor (som riktiga
    // detaljnivåer, som ligger på samma plats).
    const std::uint32_t kRutor = 110000, kSida = 332;
    glaze::MeshDesc     d;
    d.positions.reserve(6 * kRutor);
    for (int nivå = 0; nivå < 2; ++nivå)
        for (std::uint32_t i = 0; i < kRutor; ++i) {
            const double x = (i % kSida) * 1.0, y = (i / kSida) * 1.0, z = nivå * 0.5;
            d.positions.push_back(f3(x + 0.1, y + 0.1, z));
            d.positions.push_back(f3(x + 0.9, y + 0.1, z));
            d.positions.push_back(f3(x + 0.1, y + 0.9, z));
        }
    const std::size_t n = d.positions.size();
    d.normals.assign(n, {0.0f, 0.0f, 1.0f});
    d.colors.assign(n, gfx::Color{200, 200, 200, 255});
    d.uvs.assign(n, {0.0f, 0.0f});
    d.scalars.assign(n, 0.0f);
    for (auto& u : d.user)
        u.assign(n, {0.0f, 0.0f, 0.0f, 0.0f});
    d.lods = {{0, 3 * kRutor, 50.0}, {3 * kRutor, glaze::kRangeToEnd, 0.0}};
    d.origin = {674000.0, 6580000.0, 12.0};
    const glaze::Mesh m(std::move(d));
    if (!m.valid()) {
        check(false, "P1 meshen med alla strömmar är giltig");
        return;
    }
    const MeshData&     md    = data(m);
    const std::uint32_t gräns = md.segments.size() == 2 ? md.segments[1].firstTriangle() : 0;
    check(md.segments.size() == 2 && gräns == 215092,
          "P1 220 000 trianglar à 3 · 104 byte delas av gränsen 64 MiB: två "
          "segment, det andra från triangel 215 092",
          std::to_string(md.segments.size()) + " segment, gräns " +
              std::to_string(gräns));

    // Rutan i: en lodrät stråle ovanifrån mitt i dess triangel.
    const auto stråle = [&](std::uint32_t i) {
        const double x = (i % kSida) * 1.0 + 0.3, y = (i / kSida) * 1.0 + 0.3;
        return glaze::Ray3{{674000.0 + x, 6580000.0 + y, 30.0}, {0.0, 0.0, -1.0}};
    };
    const auto träff = [&](std::uint32_t i, glaze::detail::MeshPick o) {
        const auto h = pickMesh(m, stråle(i), o);
        return h ? static_cast<std::int64_t>(h->triangle) : std::int64_t{-1};
    };
    bool         hela = true, noll = true, ett = true;
    const std::uint32_t prov[] = {0, 1, 4711, 105091, 105092, 105093, 109999};
    for (std::uint32_t i : prov) {
        glaze::detail::MeshPick o;
        hela = hela && träff(i, o) == kRutor + i;   // nivå 1 ligger överst
        o.lod = 0;
        noll  = noll && träff(i, o) == i;
        o.lod = 1;
        ett   = ett && träff(i, o) == kRutor + i;
    }
    check(hela, "P1 hela meshen: den översta triangeln (nivå 1) med sitt "
                "originalindex, också bortom segmentgränsen");
    check(noll && ett,
          "P2 nivå 0 träffar rutans triangel i, nivå 1 triangel 110 000 + i — "
          "originalindex genom nivån och segmentet");
    const auto h = pickMesh(m, stråle(105092));
    check(h && std::abs(h->point.z - 12.5) < 1e-9 &&
              std::abs(h->point.x - (674000.0 + (105092 % kSida) + 0.3)) < 1e-9 &&
              h->normal.z == 1.0 && h->frontFace && std::abs(h->distance - 17.5) < 1e-9,
          "P1 träffen i världen (origot på SWEREF inräknat), normalen +z från "
          "framsidan, avståndet 17,5");

    // Intervallet [215 000, 215 200) går över segmentgränsen.
    glaze::detail::MeshPick iv;
    iv.range        = {3 * 215000, 3 * 200};
    bool inom = true;
    for (std::uint32_t i = 104990; i < 105210; ++i) {
        // Nivå 0:s triangel i rutan (index i < 110 000) ligger alltid utanför
        // intervallet, så utanför det träffas ingenting alls.
        const std::int64_t t = träff(i, iv);
        const bool         iIntervallet = kRutor + i >= 215000 && kRutor + i < 215200;
        inom = inom && (iIntervallet ? t == kRutor + i : t == -1);
    }
    check(inom, "P1 intervallet över segmentgränsen: bara dess trianglar träffas, "
                "med originalindex");
    glaze::detail::MeshPick ingen;
    ingen.lod = glaze::detail::kNoLevel;
    glaze::detail::MeshPick saknas;
    saknas.lod = 7;
    glaze::detail::MeshPick sned;
    sned.range = {1, 3};
    glaze::detail::MeshPick tomt;
    tomt.range = {300, 0};
    check(träff(5, ingen) == -1 && träff(5, saknas) == -1 && träff(5, sned) == -1 &&
              träff(5, tomt) == -1,
          "P2 kNoLevel, en nivå som inte finns, ett intervall som inte är hela "
          "trianglar och ett tomt intervall träffar ingenting");
    check(glaze::detail::lodLevel(md, 10.0) == 0 &&
              glaze::detail::lodLevel(md, 50.0) == 0 &&
              glaze::detail::lodLevel(md, 50.5) == 1 &&
              glaze::detail::lodLevel(md, 1e9) == 1,
          "P2 lodLevel: ≤ 50 m nivå 0, sedan den obegränsade nivå 1");
    glaze::MeshDesc kort;
    kort.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    kort.lods      = {{0, 3, 40.0}};
    const glaze::Mesh km(std::move(kort));
    glaze::MeshDesc utan;
    utan.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    const glaze::Mesh um(std::move(utan));
    check(km.valid() && um.valid() &&
              glaze::detail::lodLevel(data(km), 41.0) == glaze::detail::kNoLevel &&
              glaze::detail::lodLevel(data(um), 41.0) == glaze::detail::kWholeMesh,
          "P2 lodLevel: bortom en mesh utan obegränsad nivå ritas ingenting; "
          "utan nivåer hela meshen");
}

// ------------------------------------------------------------------ P3
void fallOpak()
{
    // Två trianglar ovanför varandra: den övre (index 0) i glas, alfa 110.
    glaze::MeshDesc d;
    d.positions = {{0, 0, 1}, {4, 0, 1}, {0, 4, 1}, {0, 0, 0}, {4, 0, 0}, {0, 4, 0},
                   {0, 0, 2}, {4, 0, 2}, {0, 4, 2}};
    d.colors    = {{90, 160, 220, 110}, {90, 160, 220, 110}, {90, 160, 220, 110},
                   {0, 0, 0, 255},      {0, 0, 0, 255},      {0, 0, 0, 255},
                   {0, 0, 0, 255},      {0, 0, 0, 254},      {0, 0, 0, 255}};
    glaze::Mesh       m(std::move(d));
    const glaze::Ray3 ner{{1, 1, 10}, {0, 0, -1}};
    glaze::detail::MeshPick opak;
    opak.opaqueOnly = true;
    const auto alla = pickMesh(m, ner);
    const auto bara = pickMesh(m, ner, opak);
    check(alla && alla->triangle == 2 && bara && bara->triangle == 1,
          "P3 utan filtret träffas den översta (ett hörn med alfa 254); med "
          "opaqueOnly hoppas den och glaset (110) över",
          (alla ? std::to_string(alla->triangle) : "-") + " / " +
              (bara ? std::to_string(bara->triangle) : "-"));
    const gfx::Color hel[] = {{0, 0, 0, 255}};
    m.setColors(7, hel);
    const auto efter = pickMesh(m, ner, opak);
    check(efter && efter->triangle == 2,
          "P3 setColors gör den översta ogenomskinlig — slår igenom direkt");
    glaze::MeshDesc u;
    u.positions = {{0, 0, 1}, {4, 0, 1}, {0, 4, 1}, {0, 0, 0}, {4, 0, 0}, {0, 4, 0}};
    const glaze::Mesh utan(std::move(u));
    const auto        hu = pickMesh(utan, ner, opak);
    check(hu && hu->triangle == 0,
          "P3 en mesh utan färgström är ogenomskinlig för filtret");
}

// ------------------------------------------------------------------ P4
void fallLängstBort()
{
    glaze::MeshDesc d;
    d.positions = {{-1, -1, 0}, {3, -1, 0}, {-1, 3, 0}};
    const glaze::Mesh m(std::move(d));
    glaze::detail::MeshPick o;
    o.maxDistance = 8.0;
    const glaze::Ray3 ner{{0, 0, 9}, {0, 0, -1}};
    const glaze::Ray3 dubbel{{0, 0, 9}, {0, 0, -2}};
    const bool kort = !pickMesh(m, ner, o).has_value() &&
                      !pickMesh(m, dubbel, o).has_value();
    o.maxDistance = 9.0;
    const auto a  = pickMesh(m, ner, o);
    const auto b  = pickMesh(m, dubbel, o);
    check(kort && a && b && a->distance == 9.0 && b->distance == 9.0 &&
              b->t == 4.5,
          "P4 längsta avståndet i riktningens enhet: 8 når inte en träff på 9, "
          "9 gör det; med |riktning| = 2 är t = 4,5 och avståndet 9");
}

// ------------------------------------------------------------------ P5
void fallSetPositions()
{
    glaze::MeshDesc d;
    d.positions = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}, {10, 10, 0}, {12, 10, 0}, {10, 12, 0}};
    glaze::Mesh       m(std::move(d));
    const glaze::Ray3 vid{{0.5, 0.5, 5}, {0, 0, -1}};
    const glaze::Ray3 sidan{{50.5, 0.5, 5}, {0, 0, -1}};
    const auto        före = pickMesh(m, vid);
    // Den gamla posten hålls kvar, så att en ny inte kan hamna på samma adress.
    const std::shared_ptr<glaze::detail::MeshPicking> gammal = data(m).picking;
    const glaze::Float3 flytt[] = {{50, 0, 0}, {52, 0, 0}, {50, 2, 0}};
    m.setPositions(0, flytt);
    const auto där  = pickMesh(m, sidan);
    const auto kvar = pickMesh(m, vid);
    check(före && före->triangle == 0 && där && där->triangle == 0 &&
              !kvar.has_value() && data(m).picking != gammal,
          "P5 setPositions: BVH:n byggs om — triangeln flyttad 50 m träffas där "
          "och inte där den var");
}

// ------------------------------------------------------------------ P6
void fallUtanKopia()
{
    glaze::MeshDesc d;
    d.positions   = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}};
    d.keepCpuCopy = false;
    const glaze::Mesh m(std::move(d));
    m.preparePicking();
    check(m.valid() && !pickMesh(m, {{0.5, 0.5, 5}, {0, 0, -1}}).has_value() &&
              data(m).picking->bvh.nodes.empty(),
          "P6 keepCpuCopy = false: ingen träff och inget BVH-bygge");
}

// ------------------------------------------------------------------ P7
void fallTrådarna()
{
    std::mt19937      g(77u);
    std::string       slag;
    const glaze::Mesh m(slumpmesh(g, 3 * 8, slag));   // en soppa
    std::vector<std::thread> t;
    for (int i = 0; i < 4; ++i)
        t.emplace_back([m] { m.preparePicking(); });
    for (std::thread& x : t)
        x.join();
    const glaze::detail::Bvh& bvh   = data(m).picking->bvh;
    const void*               noder = bvh.nodes.data();
    int                       fel   = 0, lika = 0;
    for (int i = 0; i < 200; ++i) {
        double            tMax = 0.0;
        const glaze::Ray3 r    = slumpstråle(g, data(m), i % 4, tMax);
        std::string       varför;
        if (!samma(data(m), r, pickMesh(m, r),
                   bruteForce(data(m), r, tMax, 0, data(m).triangleCount(), false),
                   lika, varför))
            ++fel;
    }
    check(!bvh.nodes.empty() && bvh.nodes.data() == noder && fel == 0,
          "P7 fyra trådar kör preparePicking samtidigt: ETT bygge, och "
          "plockningen efteråt använder det (och stämmer)",
          std::to_string(fel) + " fel");
}

// ------------------------------------------------------------------ P8
void fallPlanet()
{
    using glaze::detail::rayPlane;
    const auto a = rayPlane({{0, 0, 10}, {0, 0, -2}}, {{5, 5, 3}, {0, 0, 5}});
    check(a && a->point.x == 0.0 && a->point.y == 0.0 && a->point.z == 3.0 &&
              a->t == 3.5,
          "P8 lodrät stråle (|riktning| = 2) mot z = 3: punkten (0, 0, 3), t = 3,5");
    // Ett lutat plan på SWEREF mot grindens egen formel.
    const glaze::Ray3   r{{674010.0, 6580020.0, 50.0}, {0.3, -0.2, -1.0}};
    const glaze::Plane3 p{{674000.0, 6580000.0, 5.0}, {0.2, 0.1, 1.0}};
    const D3     o{r.origin.x, r.origin.y, r.origin.z}, d{r.direction.x, r.direction.y, r.direction.z};
    const D3     n{p.normal.x, p.normal.y, p.normal.z};
    const double t = prick(D3{p.point.x, p.point.y, p.point.z} - o, n) / prick(d, n);
    const D3     q = o + d * t;
    const auto   b = rayPlane(r, p);
    check(b && std::abs(b->t - t) < 1e-12 * t && std::abs(b->point.x - q.x) < 1e-9 &&
              std::abs(b->point.y - q.y) < 1e-9 && std::abs(b->point.z - q.z) < 1e-9,
          "P8 ett lutat plan på SWEREF: samma t och punkt som grindens formel");
    check(!rayPlane({{0, 0, 1}, {1, 0, 0}}, {{0, 0, 0}, {0, 0, 1}}) &&
              !rayPlane({{0, 0, 1}, {0, 0, 1}}, {{0, 0, 0}, {0, 0, 1}}),
          "P8 parallell och bakom ger ingenting");
    const auto c = rayPlane({{2, 3, 0}, {0, 1, 1}}, {{0, 0, 0}, {0, 0, 1}});
    check(c && c->t == 0.0 && c->point.x == 2.0 && c->point.y == 3.0,
          "P8 början i planet: t = 0");
}

// --------------------------------------------------------------- HI1–HI7
// TERRÄNGENS RUTNÄT (drape.h `terrainHeight`) — headless, en
// rityta utan fönster (`heightAt` läser bara scenen). 11 × 11 plattor om 10
// enheter vid SWEREF, var och en ett PLAN z = a + b·dx + c·dy med talen valda
// så att hörnen är exakta i float — referensen är grindens egen formel, inte
// plockningen. Punkterna ligger strikt inne i en platta (en kant delas av två).
//   HI1 heightAt = referensen i 10 000 punkter, ETT bygge av rutnätet, och högst 4
//       prövade former per fråga (cellen = plattans sida)
//   HI2 platshållaren (plan på z = 0) → meshen PÅ SAMMA PLATS (`setShape`)
//   HI3 `setPositions` lyfter en platta 3 enheter — formplatsen och lagret
//       orörda, bara meshens lägen (meshernas generation)
//   HI4 `setLayerVisible(false)`: ingen terräng
//   HI5 `setLayerPlacement(World)`: ingen terräng
//   HI6 ett NYTT Terrain-lager — först Auto (ett World-lager av sin mesh),
//       frågat, därefter Terrain: dess platta över (2, 2) är den översta ytan
//   HI7 `moveLayer` bland Terrain-lagren: oförändrat
// VAD DET INTE MÄTER: BVH:ns egen kostnad, och en mesh som flyttas i XY med
// `setPositions` mellan två frågor utan annan ändring (samma generation som
// HI3 — inget eget fall).
// SABOTAGEN (skrivna): meshernas generation utanför
// nyckeln (HI3 — höjden står kvar), `layerSetVersion` inte stegad i
// `setLayerPlacement` (HI5 — terrängen kvar; HI6 — den nya plattan saknas),
// en linjär sökning över formerna (HI1 — 121 prövade per fråga) och
// rutnätet byggt per fråga (HI1 — 10 000 byggen).
void fallTerrängIndex()
{
    using glaze::detail::terrainIndexStats;
    constexpr int    kN    = 11;
    constexpr double kSida = 10.0;
    const double     x0 = 674000.0, y0 = 6580000.0;
    // Plattans plan — hörnens z exakta i float (multiplar av 1/16).
    const auto a = [](int i, int j) { return 10.0 + i + 0.5 * j; };
    const auto b = [](int i, int j) { return 0.125 * ((i + j) % 5 - 2); };
    const auto c = [](int i, int j) { return 0.0625 * ((3 * i + j) % 7 - 3); };
    const auto plan = [&](int i, int j, double lyft) {
        glaze::MeshDesc d;
        d.origin = {x0 + kSida * i, y0 + kSida * j, 0.0};
        for (int k = 0; k < 4; ++k) {
            const double dx = (k == 1 || k == 2) ? kSida : 0.0;
            const double dy = k >= 2 ? kSida : 0.0;
            d.positions.push_back({static_cast<float>(dx), static_cast<float>(dy),
                                   static_cast<float>(a(i, j) + b(i, j) * dx +
                                                      c(i, j) * dy + lyft)});
        }
        d.indices = {0, 1, 2, 0, 2, 3};
        return glaze::Mesh(std::move(d));
    };
    const auto facit = [&](int i, int j, double x, double y) {
        return a(i, j) + b(i, j) * (x - x0 - kSida * i) + c(i, j) * (y - y0 - kSida * j);
    };

    glaze::Canvas yta;
    const int     T = yta.addLayer("terräng");
    yta.setLayerPlacement(T, glaze::LayerPlacement::Terrain);
    // HI2: platta (5, 5) är först en platshållare på z = 0.
    std::vector<glaze::Mesh> meshar;
    for (int j = 0; j < kN; ++j)
        for (int i = 0; i < kN; ++i) {
            if (i == 5 && j == 5) {
                glaze::MeshDesc d;
                d.origin    = {x0 + kSida * i, y0 + kSida * j, 0.0};
                d.positions = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
                d.indices   = {0, 1, 2, 0, 2, 3};
                meshar.push_back(glaze::Mesh(std::move(d)));
            } else {
                meshar.push_back(plan(i, j, 0.0));
            }
            yta.addShape(T, glaze::Shape::mesh(meshar.back()));
        }

    // HI1 — 10 000 punkter strikt inne i plattorna.
    std::mt19937        g(1717u);
    const std::uint64_t byggen0 = terrainIndexStats().builds;
    double              maxFel = 0.0;
    std::uint64_t       flestPrövade = 0;
    bool                alla = true;
    for (int n = 0; n < 10000; ++n) {
        const int i = static_cast<int>(slump(g) * kN), j = static_cast<int>(slump(g) * kN);
        if (i == 5 && j == 5)
            continue;
        const double x = x0 + kSida * (i + 0.01 + 0.98 * slump(g));
        const double y = y0 + kSida * (j + 0.01 + 0.98 * slump(g));
        const std::uint64_t k0 = terrainIndexStats().candidates;
        const std::optional<double> z = yta.heightAt(glaze::Point{x, y});
        flestPrövade = std::max(flestPrövade, terrainIndexStats().candidates - k0);
        alla         = alla && z.has_value();
        if (z)
            maxFel = std::max(maxFel, std::abs(*z - facit(i, j, x, y)));
    }
    const std::uint64_t byggen = terrainIndexStats().builds - byggen0;
    check(alla && maxFel < 1e-6,
          "HI1 heightAt = facit i 10 000 punkter (121 plana plattor vid SWEREF)",
          tal(maxFel));
    check(byggen == 1 && flestPrövade >= 1 && flestPrövade <= 4,
          "HI1 rutnätet byggs EN gång, och en fråga prövar högst 4 former",
          std::to_string(byggen) + " byggen, flest " + std::to_string(flestPrövade) +
              " prövade");

    // HI2 — platshållaren och därefter meshen på samma plats.
    const double              mx = x0 + kSida * 5.4, my = y0 + kSida * 5.7;
    const std::optional<double> före = yta.heightAt(glaze::Point{mx, my});
    meshar[5 * kN + 5] = plan(5, 5, 0.0);
    yta.setShape(T, 5 * kN + 5, glaze::Shape::mesh(meshar[5 * kN + 5]));
    const std::optional<double> efter = yta.heightAt(glaze::Point{mx, my});
    check(före && std::abs(*före) < 1e-9 && efter &&
              std::abs(*efter - facit(5, 5, mx, my)) < 1e-6,
          "HI2 platshållaren (z = 0) och meshen på samma plats (setShape)",
          (före ? tal(*före) : std::string("tom")) + " → " +
              (efter ? tal(*efter) : std::string("tom")));

    // HI3 — setPositions lyfter platta (3, 7) tre enheter.
    {
        glaze::Mesh&               m = meshar[7 * kN + 3];
        std::vector<glaze::Float3> lyft;
        for (const glaze::Float3& p : glaze::detail::MeshAccess::data(m)->positions)
            lyft.push_back({p.x, p.y, p.z + 3.0f});
        m.setPositions(0, lyft);
        const double              lx = x0 + kSida * 3.3, ly = y0 + kSida * 7.6;
        const std::optional<double> z = yta.heightAt(glaze::Point{lx, ly});
        check(z && std::abs(*z - (facit(3, 7, lx, ly) + 3.0)) < 1e-6,
              "HI3 setPositions lyfter plattan 3 enheter — höjden följer",
              z ? tal(*z) + " mot " + tal(facit(3, 7, lx, ly) + 3.0) : std::string("tom"));
    }

    // HI4 och HI5 — dold, och inte längre Terrain: ingen terräng.
    const glaze::Point p22{x0 + kSida * 2.5, y0 + kSida * 2.5};
    yta.setLayerVisible(T, false);
    const bool dold = !yta.heightAt(p22);
    yta.setLayerVisible(T, true);
    const bool tillbaka = yta.heightAt(p22).has_value();
    check(dold && tillbaka, "HI4 setLayerVisible(false): ingen terräng — och tillbaka");
    yta.setLayerPlacement(T, glaze::LayerPlacement::World);
    const bool värld = !yta.heightAt(p22);
    yta.setLayerPlacement(T, glaze::LayerPlacement::Terrain);
    check(värld && yta.heightAt(p22).has_value(),
          "HI5 setLayerPlacement(World): ingen terräng — och tillbaka");

    // HI6 — ett nytt Terrain-lager med en plan platta i z = 100 över (2, 2).
    const int ny = yta.addLayer("över");
    glaze::MeshDesc d;
    d.origin    = {x0 + kSida * 2.0, y0 + kSida * 2.0, 100.0};
    d.positions = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
    d.indices   = {0, 1, 2, 0, 2, 3};
    yta.addShape(ny, glaze::Shape::mesh(glaze::Mesh(std::move(d))));
    const std::optional<double> auto_ = yta.heightAt(p22);   // Auto: ett World-lager
    yta.setLayerPlacement(ny, glaze::LayerPlacement::Terrain);
    const std::optional<double> över = yta.heightAt(p22);
    check(auto_ && std::abs(*auto_ - facit(2, 2, p22.x, p22.y)) < 1e-6 && över &&
              std::abs(*över - 100.0) < 1e-9,
          "HI6 ett nytt Terrain-lager: dess platta över (2, 2) är den översta ytan",
          (auto_ ? tal(*auto_) : std::string("tom")) + " → " +
              (över ? tal(*över) : std::string("tom")));

    // HI7 — Terrain-lagren byter ordning: oförändrat.
    yta.moveLayer(ny, T);
    const std::optional<double> flyttad = yta.heightAt(p22);
    const glaze::Point          p33{x0 + kSida * 3.5, y0 + kSida * 3.5};
    const std::optional<double> bredvid = yta.heightAt(p33);
    check(flyttad && std::abs(*flyttad - 100.0) < 1e-9 && bredvid &&
              std::abs(*bredvid - facit(3, 3, p33.x, p33.y)) < 1e-6,
          "HI7 moveLayer bland Terrain-lagren: höjderna oförändrade");
}

// ------------------------------------------------------------------ T
void tidStolpe()
{
    // En gallerstolpe: 84 stänger som rör med 12 sidor och 100 punkter
    // (≈ 2 396 trianglar var) i en låda 2 × 2 × 30 m — ≈ 200 k trianglar,
    // platta och icke-indexerade med färg per triangel.
    std::mt19937      g(37u);
    gfx::MeshGeometry geo;
    const auto        hörn = [&](double h) {
        const int k = static_cast<int>(slump(g) * 4.0);
        const double s = 1.0 - h / 40.0;
        return gfx::Vec3d{(k & 1 ? 1.0 : -1.0) * s, (k & 2 ? 1.0 : -1.0) * s, h};
    };
    for (int s = 0; s < 84; ++s) {
        const double            h0 = slump(g) * 29.0, h1 = h0 + 0.2 + slump(g) * 1.5;
        const gfx::Vec3d        a = hörn(h0), b = hörn(h1);
        std::vector<gfx::Vec3d> väg;
        for (int i = 0; i < 100; ++i) {
            const double f = i / 99.0;
            väg.push_back({a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f + 0.001 * i,
                           a.z + (b.z - a.z) * f});
        }
        gfx::tube(väg, 0.02 + slump(g) * 0.06, geo, 12);
    }
    glaze::MeshDesc d;
    const std::size_t nTri = geo.indices.size() / 3;
    d.positions.reserve(geo.indices.size());
    d.colors.reserve(geo.indices.size());
    for (std::size_t t = 0; t < nTri; ++t) {
        const gfx::Color c{static_cast<std::uint8_t>(t), 120, 140,
                           static_cast<std::uint8_t>(t % 50 == 0 ? 110 : 255)};
        for (int k = 0; k < 3; ++k) {
            const gfx::Vec3d& p = geo.positions[geo.indices[3 * t + static_cast<std::size_t>(k)]];
            d.positions.push_back(f3(p.x, p.y, p.z));
            d.colors.push_back(c);
        }
    }
    geo = {};
    const auto t0 = std::chrono::steady_clock::now();
    const glaze::Mesh m(std::move(d));
    const auto t1 = std::chrono::steady_clock::now();
    m.preparePicking();
    const auto t2 = std::chrono::steady_clock::now();
    int        träffar = 0;
    for (int i = 0; i < 1000; ++i) {
        const glaze::Ray3 r{{-5.0 + slump(g) * 10.0, -5.0, slump(g) * 30.0},
                            {0.0, 1.0, (slump(g) - 0.5) * 0.2}};
        träffar += pickMesh(m, r).has_value();
    }
    const auto t3 = std::chrono::steady_clock::now();
    const auto ms = [](auto a, auto b) {
        return std::chrono::duration<double, std::milli>(b - a).count();
    };
    std::printf("  TID  stolpen: %zu trianglar, %zu segment, %zu BVH-noder\n",
                static_cast<std::size_t>(data(m).triangleCount()),
                data(m).segments.size(),
                data(m).picking->bvh.nodes.size());
    std::printf("  TID  meshen (prövning, låda, segment): %.0f ms\n", ms(t0, t1));
    std::printf("  TID  BVH-bygget (binnad SAH): %.0f ms\n", ms(t1, t2));
    std::printf("  TID  1 000 strålar: %.2f ms (%d träffar)\n", ms(t2, t3), träffar);
}

} // namespace

// ================================================================ RITYTAN

constexpr double kPi = 3.14159265358979323846;

double rad(double deg)
{
    return deg * kPi / 180.0;
}

struct W3 {
    double x = 0.0, y = 0.0, z = 0.0;
};
W3 operator+(W3 a, W3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
W3 operator-(W3 a, W3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
W3 operator*(W3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(W3 a, W3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
W3 cross(W3 a, W3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double len(W3 a) { return std::sqrt(dot(a, a)); }
W3 w3(glaze::Point3 p) { return {p.x, p.y, p.z}; }
glaze::Point3 p3(W3 a) { return {a.x, a.y, a.z}; }

// GRINDENS HÅLKAMERA ur konventionen (glaze/canvas/camera.h): azimuten är
// kompassen blicken pekar mot, elevationen hur högt ögat står, upp är dit
// ögat rör sig när elevationen ökar, rollen vrider kameran medurs. Z upp.
struct Hal {
    W3         eye, f, r, u;
    bool       ortho = false;
    double     F = 0.0;   // perspektivets brännvidd i px; ortografiskt px per enhet
    gfx::Rectf box{};
};

Hal hal(const glaze::Camera3D& k, gfx::Rectf box)
{
    const double az = rad(k.azimuth), el = rad(k.elevation), ro = rad(k.roll);
    const W3 f{std::sin(az) * std::cos(el), std::cos(az) * std::cos(el), -std::sin(el)};
    const W3 u0{std::sin(az) * std::sin(el), std::cos(az) * std::sin(el), std::cos(el)};
    const W3 r0 = cross(f, u0);
    Hal      h;
    h.f     = f;
    h.r     = r0 * std::cos(ro) - u0 * std::sin(ro);
    h.u     = u0 * std::cos(ro) + r0 * std::sin(ro);
    h.eye   = w3(k.target) - f * k.distance;
    h.ortho = k.projection == glaze::Projection::Orthographic;
    const double halv = k.orthoHeight > 0.0 ? k.orthoHeight
                                            : k.distance * std::tan(rad(k.fov) / 2.0);
    h.F   = h.ortho ? (box.h / 2.0) / halv : (box.h / 2.0) / std::tan(rad(k.fov) / 2.0);
    h.box = box;
    return h;
}

void halProj(const Hal& h, W3 p, double& px, double& py)
{
    const W3     q = p - h.eye;
    const double s = h.ortho ? h.F : h.F / dot(q, h.f);
    px             = h.box.x + h.box.w / 2.0 + s * dot(q, h.r);
    py             = h.box.y + h.box.h / 2.0 - s * dot(q, h.u);
}

// Strålen genom pixeln: perspektiv från ögat, ortografiskt längs blicken.
void halRay(const Hal& h, double px, double py, W3& o, W3& d)
{
    const double x = (px - h.box.x - h.box.w / 2.0) / h.F;
    const double y = (py - h.box.y - h.box.h / 2.0) / h.F;
    if (h.ortho) {
        o = h.eye + h.r * x - h.u * y;
        d = h.f;
    } else {
        o = h.eye;
        d = h.f + h.r * x - h.u * y;
        d = d * (1.0 / len(d));
    }
}

// PLACERINGENS REFERENS (style3d.h): +y pekar mot bäringen `yaw` (medurs
// ovanifrån), skalan före vridningen, och flytten `at`; lägena relativt
// meshens origo.
W3 placerad(W3 läge, W3 origo, W3 at, double yawDeg, W3 skala)
{
    const double y = rad(yawDeg);
    const W3     s{läge.x * skala.x, läge.y * skala.y, läge.z * skala.z};
    return origo + at +
           W3{s.x * std::cos(y) + s.y * std::sin(y),
              -s.x * std::sin(y) + s.y * std::cos(y), s.z};
}

glaze::Camera3D kamera3(glaze::Point3 mål, double avst, float az, float el,
                        bool orto = false, float roll = 0.0f)
{
    glaze::Camera3D k;
    k.target     = mål;
    k.distance   = avst;
    k.azimuth    = az;
    k.elevation  = el;
    k.roll       = roll;
    k.projection = orto ? glaze::Projection::Orthographic
                        : glaze::Projection::Perspective;
    return k;
}

struct Värd : glaze::AppWindow {
    glaze::Canvas yta;

    Värd() : glaze::AppWindow("pick_gate", 720, 560, gfx::Chrome::None)
    {
        yta.name   = "yta";
        yta.x      = 20;
        yta.y      = 20;
        yta.width  = 640;
        yta.height = 480;
        yta.setBorder(false);
        root().adopt(yta);
    }
    gfx::Rectf box() { return yta.resolve(yta.behaviorCtx()); }
    double     s() { return window().uiScale(); }
    // Fysiska fönster-px ↔ ritytans lokala logiska px.
    glaze::Point lok(double px, double py)
    {
        const gfx::Rectf b = box();
        return {(px - b.x) / s(), (py - b.y) / s()};
    }
    void rita() { window().renderFrame(); }
    // Ett tomt lager 0 och kameran — varje fall börjar så.
    void ny(const glaze::Camera3D& k)
    {
        yta.setLayers({glaze::TrText("")});
        yta.setViews({});
        yta.setCamera(k);
        rita();
    }
    // Träffarna av en sort (och valfritt i ett lager) under punkten.
    std::vector<glaze::HitResult> av(const std::vector<glaze::HitResult>& h,
                                     glaze::HitKind k, int lager = -1)
    {
        std::vector<glaze::HitResult> ut;
        for (const glaze::HitResult& x : h)
            if (x.kind == k && (lager < 0 || x.layer == lager))
                ut.push_back(x);
        return ut;
    }

    void kör();
};

void Värd::kör()
{
    using glaze::HitKind;
    const W3 sweref{674000.0, 6580000.0, 0.0};

    // ---- R1. rayAt ∘ project — och projektionen är hålkamerans
    for (int läge = 0; läge < 3; ++läge) {
        const glaze::Camera3D k = kamera3(p3(sweref + W3{0, 0, 10}), 80.0, 35.0f, 40.0f,
                                          läge == 1, läge == 2 ? 10.0f : 0.0f);
        ny(k);
        const Hal h = hal(k, box());
        double maxProj = 0.0, maxStråle = 0.0;
        bool   ok = true;
        for (int i = -2; i <= 2; ++i)
            for (int j = -2; j <= 2; ++j)
                for (int z = 0; z <= 2; ++z) {
                    const W3 p = w3(k.target) + W3{i * 7.0, j * 5.0, z * 3.0};
                    const auto l = yta.project(p3(p));
                    double fx = 0.0, fy = 0.0;
                    halProj(h, p, fx, fy);
                    ok = ok && l.has_value();
                    if (!l)
                        continue;
                    const glaze::Point f = lok(fx, fy);
                    maxProj = std::max(maxProj, std::hypot(l->x - f.x, l->y - f.y));
                    const auto r = yta.rayAt(*l);
                    ok = ok && r.has_value();
                    if (!r)
                        continue;
                    const W3 o = w3(r->origin), d{r->direction.x, r->direction.y,
                                                  r->direction.z};
                    const W3 q = p - o;
                    maxStråle = std::max(maxStråle, len(q - d * dot(q, d)) /
                                                        std::max(1.0, len(q)));
                }
        const char* namn[] = {"perspektiv", "ortografiskt", "med roll 10°"};
        check(ok && maxProj < 1e-6 && maxStråle < 1e-9,
              std::string("R1 rayAt ∘ project (") + namn[läge] +
                  ", SWEREF): projektionen är hålkamerans och strålen går genom "
                  "punkten",
              tal(maxProj) + " px, " + tal(maxStråle) + " relativt");
    }
    {
        // KARTKAMERAN tiltad: projektionen av markpunkter är `toLocal`s.
        yta.setLayers({glaze::TrText("")});
        yta.setCamera(glaze::Camera{{674000.0, 6580000.0}, 2.0, 30.0f, 45.0f});
        rita();
        double maxFel = 0.0, maxStråle = 0.0;
        bool   ok = true;
        for (int i = -3; i <= 3; ++i)
            for (int j = -3; j <= 3; ++j) {
                const glaze::Point w{674000.0 + i * 20.0, 6580000.0 + j * 15.0};
                const auto a = yta.project(glaze::Point3{w.x, w.y, 0.0});
                const auto b = yta.toLocal(w);
                ok = ok && a && b;
                if (!a || !b)
                    continue;
                maxFel = std::max(maxFel, std::hypot(a->x - b->x, a->y - b->y));
                const auto r = yta.rayAt(*a);
                ok = ok && r.has_value();
                if (!r)
                    continue;
                const W3 q = W3{w.x, w.y, 0.0} - w3(r->origin);
                const W3 d{r->direction.x, r->direction.y, r->direction.z};
                maxStråle = std::max(maxStråle, len(q - d * dot(q, d)) / len(q));
            }
        check(ok && maxFel < 1e-6 && maxStråle < 1e-9,
              "R1 kartkameran (bäring 30°, tilt 45°): project = toLocal och "
              "strålen genom punkten",
              tal(maxFel) + " px, " + tal(maxStråle));
    }

    // ---- R2. worldPerPixel mot derivatan längs kamerans högeraxel
    for (int läge = 0; läge < 2; ++läge) {
        const glaze::Camera3D k =
            kamera3(p3(sweref + W3{0, 0, 10}), 80.0, -40.0f, 25.0f, läge == 1);
        ny(k);
        const Hal h = hal(k, box());
        double    maxFel = 0.0;
        bool      ok     = true;
        for (const W3 d : {W3{0, 0, 0}, W3{12, -7, 4}, W3{-20, 15, 9}}) {
            const W3   p = w3(k.target) + d;
            const double Δ = 1e-3 * k.distance;
            const auto a = yta.project(p3(p - h.r * Δ));
            const auto b = yta.project(p3(p + h.r * Δ));
            const auto v = yta.worldPerPixel(p3(p));
            ok = ok && a && b && v;
            if (!(a && b && v))
                continue;
            const double facit = 2.0 * Δ / std::hypot(b->x - a->x, b->y - a->y);
            maxFel = std::max(maxFel, std::abs(*v - facit) / facit);
        }
        check(ok && maxFel < 1e-6,
              std::string("R2 worldPerPixel = derivatan av project längs "
                          "högeraxeln (") +
                  (läge ? "ortografiskt" : "perspektiv") + ")",
              tal(maxFel) + " relativt");
    }
    {
        yta.setLayers({glaze::TrText("")});
        yta.setCamera(glaze::Camera{{674000.0, 6580000.0}, 3.0, 20.0f, 50.0f});
        rita();
        // Kartkamerans högeraxel ur två markpunkter på samma bildrad: djupet
        // är lika där (kameran har ingen roll), så sträckan mot pixlarna är
        // skalan — världsenheter per logisk px.
        const auto a = yta.toWorld(glaze::Point{300.0, 260.0});
        const auto b = yta.toWorld(glaze::Point{340.0, 260.0});
        const auto v = yta.worldPerPixel(glaze::Point3{(a->x + b->x) / 2.0,
                                                       (a->y + b->y) / 2.0, 0.0});
        const double facit = std::hypot(b->x - a->x, b->y - a->y) / 40.0;
        check(v && std::abs(*v - facit) / facit < 1e-6,
              "R2 kartkameran tiltad: worldPerPixel på marken = sträckan längs "
              "bildraden per px",
              (v ? tal(*v) : std::string("tomt")) + " mot " + tal(facit));
    }

    // ---- R3. visibleRegion mot en analytisk trapets
    {
        glaze::Camera3D k = kamera3({0.0, 0.0, 0.0}, 50.0, 0.0f, 50.0f);
        k.nearPlane       = 0.5;
        k.farPlane        = 5000.0;
        ny(k);
        const Hal        h = hal(k, box());
        const gfx::Rectf b = box();
        std::vector<W3>  facit;
        for (const auto& [px, py] : {std::pair{b.x, b.y}, std::pair{b.x + b.w, b.y},
                                     std::pair{b.x + b.w, b.y + b.h},
                                     std::pair{b.x, b.y + b.h}}) {
            W3 o, d;
            halRay(h, px, py, o, d);
            facit.push_back(o + d * (-o.z / d.z));
        }
        const std::vector<glaze::Point3> reg = yta.visibleRegion(glaze::Plane3{});
        double maxFel = 0.0;
        for (const W3& f : facit) {
            double närmast = 1e30;
            for (const glaze::Point3& q : reg)
                närmast = std::min(närmast, len(w3(q) - f));
            maxFel = std::max(maxFel, närmast);
        }
        check(reg.size() == 4 && maxFel < 1e-6 * k.distance,
              "R3 visibleRegion: skärmhörnens strålar mot marken — en trapets",
              std::to_string(reg.size()) + " hörn, " + tal(maxFel));
        const std::vector<glaze::Point3> kort = yta.visibleRegion(glaze::Plane3{}, 60.0);
        int    påTaket = 0;
        bool   inom    = !kort.empty();
        for (const glaze::Point3& q : kort) {
            const double djup = dot(w3(q) - h.eye, h.f);
            inom              = inom && djup <= 60.0 + 1e-6;
            påTaket += std::abs(djup - 60.0) < 1e-6 ? 1 : 0;
        }
        check(inom && påTaket >= 2,
              "R3 med djupstaket 60: inget hörn längre bort, och kanten ligger på "
              "det",
              std::to_string(påTaket) + " hörn på taket");
    }

    // ---- R4. HÖRN OCH KANT inom N px — och inte vid N + 0,3
    {
        // Två trianglar i ett lodrätt plan (x, z), vridna 30° och skalade ×2
        // i x, vid SWEREF.
        const std::vector<glaze::Point3> läge{
            {0, 0, 0}, {20, 0, 0}, {0, 0, 20}, {20, 0, 20}};
        const glaze::Mesh mesh =
            glaze::Mesh::indexed(läge, {0, 1, 2, 1, 3, 2}, gfx::Color{90, 120, 200, 255});
        const W3    origo = w3(data(mesh).origin);
        const W3    at    = sweref + W3{0, 0, 5};
        const W3    skala{2, 1, 1};
        const auto  världen = [&](int v) {
            return placerad(w3(läge[static_cast<std::size_t>(v)]) - origo, origo, at, 30.0,
                            skala);
        };
        const W3 mitt = (världen(0) + världen(3)) * 0.5;
        ny(kamera3(p3(mitt), 70.0, 10.0f, 25.0f));
        const int  L = yta.addLayer("mesh");
        yta.setLayerPlacement(L, glaze::LayerPlacement::World);
        yta.addShape(L, glaze::Shape::mesh(mesh, {.at = p3(at), .yaw = 30.0f,
                                                  .scale = {2.0f, 1.0f, 1.0f}}));
        rita();
        const Hal h = hal(yta.camera3D(), box());
        // Hörnet 3 och en punkt UTÅT från meshen på skärmen.
        double vx = 0.0, vy = 0.0, cx = 0.0, cy = 0.0;
        halProj(h, världen(3), vx, vy);
        halProj(h, mitt, cx, cy);
        const double ux = (vx - cx) / std::hypot(vx - cx, vy - cy);
        const double uy = (vy - cy) / std::hypot(vx - cx, vy - cy);
        const auto hörnVid = [&](double avstFys, glaze::Length tol) {
            glaze::HitOptions o;
            o.vertexTolerance = tol;
            const auto träff  = av(yta.hitTest(lok(vx + ux * avstFys, vy + uy * avstFys), o),
                                   HitKind::Vertex, L);
            return träff.size() == 1 && träff[0].element == 3 &&
                   len(w3(*träff[0].point) - världen(3)) < 1e-6;
        };
        const double sk = s();
        check(hörnVid((6.0 - 0.3) * sk, 6.0f) && !hörnVid((6.0 + 0.3) * sk, 6.0f),
              "R4 hörnet inom 6 px träffas (originalets läge 3, punkten), vid 6,3 "
              "px inte");
        const double perMm = yta.display().pxPerMm;
        check(hörnVid(1.5 * perMm - 0.3, glaze::mm(1.5f)) &&
                  !hörnVid(1.5 * perMm + 0.3, glaze::mm(1.5f)),
              "R4 toleransen i mm: 1,5 mm på skärmen", tal(perMm) + " px/mm");
        {
            // I världsenheter: 0,5 enheter vid hörnet = 0,5 · (px per enhet där).
            const double brännvidd = h.F / dot(världen(3) - h.eye, h.f);
            check(hörnVid(0.5 * brännvidd - 0.3, glaze::world(0.5f)) &&
                      !hörnVid(0.5 * brännvidd + 0.3, glaze::world(0.5f)),
                  "R4 toleransen i världsenheter räknas om vid hörnet",
                  tal(0.5 * brännvidd) + " px");
        }
        // KANTEN 1 → 3 (triangel 1, hörnet k = 0): en punkt vinkelrätt utåt
        // från dess mitt på skärmen.
        double ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
        halProj(h, världen(1), ax, ay);
        halProj(h, världen(3), bx, by);
        const double ex = bx - ax, ey = by - ay, el = std::hypot(ex, ey);
        double       nx = -ey / el, ny = ex / el;
        if ((nx * (cx - (ax + bx) / 2) + ny * (cy - (ay + by) / 2)) > 0) {
            nx = -nx;   // utåt — bort från meshens mitt
            ny = -ny;
        }
        const auto kantVid = [&](double avstFys) {
            glaze::HitOptions o;
            o.edgeTolerance = 6.0f;
            const auto träff = av(yta.hitTest(lok((ax + bx) / 2 + nx * avstFys,
                                                  (ay + by) / 2 + ny * avstFys), o),
                                  HitKind::Edge, L);
            return träff.size() == 1 && träff[0].element == 3;
        };
        check(kantVid((6.0 - 0.3) * sk) && !kantVid((6.0 + 0.3) * sk),
              "R4 kanten inom 6 px träffas (3 · triangel 1 + 0), vid 6,3 px inte");
    }

    // ---- R5. PUNKTERNA — toleransen + halva storleken, maxDistance, indatan
    {
        ny(kamera3(p3(sweref), 60.0, 0.0f, 30.0f));
        const int L = yta.addLayer("punkter");
        const std::vector<glaze::Point3> pts{p3(sweref + W3{-10, 0, 0}),
                                             p3(sweref + W3{0, 20, 2}),
                                             p3(sweref + W3{12, 40, 0})};
        yta.addShape(L, glaze::Shape::points3(pts, {.size = glaze::px(4.0f)}));
        rita();
        const Hal    h = hal(yta.camera3D(), box());
        double       px = 0.0, py = 0.0;
        halProj(h, w3(pts[1]), px, py);
        // Strålen genom punktens pixel går från ögat genom punkten.
        const double avst  = len(w3(pts[1]) - h.eye);
        const double grans = (yta.behaviorCtx().style->canvasHitTolerance + 2.0) * s();
        const auto punktVid = [&](double dx, double maxD) {
            glaze::HitOptions o;
            o.maxDistance  = maxD;
            const auto t   = av(yta.hitTest(lok(px + dx, py), o), HitKind::Point, L);
            return t.size() == 1 && t[0].element == 1 ? t[0].distance : -1.0;
        };
        check(punktVid(grans - 0.3, 0.0) > 0.0 && punktVid(grans + 0.3, 0.0) < 0.0,
              "R5 punkten träffas inom toleransen + halva storleken (2 px), inte "
              "utanför");
        const double d = punktVid(0.0, 0.0);
        check(std::abs(d - avst) < 1e-6 && punktVid(0.0, avst - 0.5) < 0.0 &&
                  punktVid(0.0, avst + 0.5) > 0.0,
              "R5 avståndet längs strålen, och maxDistance utesluter den bortom",
              tal(d) + " mot " + tal(avst));
        // INDATANS PLATS genom ett moln som delats i chunkar: 70 000 punkter i
        // ett rutnät i omvänd ordning — punkten under pekaren svarar med sin
        // plats i indatan.
        std::vector<glaze::Point3> moln;
        for (int i = 279; i >= 0; --i)
            for (int j = 249; j >= 0; --j)
                moln.push_back(p3(sweref + W3{i * 0.5 - 70.0, j * 0.5 - 20.0, 0.0}));
        const glaze::PointCloud pc = glaze::PointCloud::fromWorld(moln);
        yta.setShapes(L, {glaze::Shape::pointCloud(pc, {.size = glaze::px(2.0f)})});
        rita();
        const std::size_t mål = 31337;
        halProj(h, w3(moln[mål]), px, py);
        const auto t = av(yta.hitTest(lok(px, py), {}), HitKind::Point, L);
        check(MeshAccess::data(pc)->chunks.size() > 1 && t.size() == 1 &&
                  t[0].element == static_cast<std::int64_t>(mål),
              "R5 ett moln i chunkar svarar med punktens plats i INDATAN",
              t.empty() ? std::string("ingen") : std::to_string(t[0].element));
    }

    // ---- R6. ETIKETT I 3D — en text3 i ett World-lager, som den ritades
    {
        ny(kamera3(p3(sweref), 60.0, 20.0f, 30.0f));
        const int L = yta.addLayer("etiketter");
        const glaze::Point3 a = p3(sweref + W3{3, 8, 6});
        yta.addShape(L, glaze::Shape::text3(a, "PLOCKA", {.size = 18.0f}));
        rita();
        const Hal h = hal(yta.camera3D(), box());
        double    px = 0.0, py = 0.0;
        halProj(h, w3(a), px, py);
        const auto in  = av(yta.hitTest(lok(px, py), {}), HitKind::Label, L);
        const auto ute = av(yta.hitTest(lok(px, py + 120.0 * s()), {}), HitKind::Label, L);
        check(in.size() == 1 && in[0].index == 0 && ute.empty() &&
                  len(w3(*in[0].point) - w3(a)) < 1e-9,
              "R6 etiketten i rymden träffas där den ritades (ankaret i point), "
              "120 px nedanför inte");
    }

    // ---- R7. heightAt på en analytisk lutande terräng
    {
        ny(kamera3(p3(sweref + W3{50, 50, 0}), 150.0, 0.0f, 45.0f));
        const int T = yta.addLayer("terräng");
        yta.setLayerPlacement(T, glaze::LayerPlacement::Terrain);
        const auto höjd = [](double x, double y) { return 0.1 * x - 0.05 * y + 3.0; };
        std::vector<glaze::Point3> läge;
        for (int j = 0; j <= 10; ++j)
            for (int i = 0; i <= 10; ++i)
                läge.push_back(p3(sweref + W3{i * 10.0, j * 10.0, höjd(i * 10.0, j * 10.0)}));
        std::vector<std::uint32_t> idx;
        for (int j = 0; j < 10; ++j)
            for (int i = 0; i < 10; ++i) {
                if (i == 4 && j == 6)
                    continue;   // ett HÅL i rutan (40–50, 60–70)
                const std::uint32_t a = static_cast<std::uint32_t>(j * 11 + i);
                idx.insert(idx.end(), {a, a + 1, a + 12, a, a + 12, a + 11});
            }
        yta.addShape(T, glaze::Shape::mesh(glaze::Mesh::indexed(läge, idx,
                                                                gfx::Color{80, 140, 60, 255})));
        std::mt19937 g(7);
        double       maxFel = 0.0;
        bool         ok     = true;
        for (int n = 0; n < 200; ++n) {
            const double x = slump(g) * 100.0, y = slump(g) * 100.0;
            if (x >= 40.0 && x <= 50.0 && y >= 60.0 && y <= 70.0)
                continue;
            const auto z = yta.heightAt(glaze::Point{sweref.x + x, sweref.y + y});
            ok           = ok && z.has_value();
            if (z)
                maxFel = std::max(maxFel, std::abs(*z - höjd(x, y)));
        }
        check(ok && maxFel < 1e-6,
              "R7 heightAt på den lutande terrängen = 0,1x − 0,05y + 3 (SWEREF, "
              "200 punkter)",
              tal(maxFel));
        check(!yta.heightAt(glaze::Point{sweref.x + 45.0, sweref.y + 65.0}) &&
                  !yta.heightAt(glaze::Point{sweref.x + 150.0, sweref.y + 50.0}),
              "R7 i hålet och utanför: tomt");
        yta.setLayerPlacement(T, glaze::LayerPlacement::World);
        check(!yta.heightAt(glaze::Point{sweref.x + 10.0, sweref.y + 10.0}),
              "R7 utan Terrain-lager finns ingen terräng");
    }

    // ---- R8. YTAN genom placeringen, instansen, sorteringen och skymningen
    {
        // En LUTANDE vägg: triangel 0 = (0, 0, 0), (10, 0, 0), (0, 5, 10) med
        // normalen efter vindningen (b − a) × (c − a) = (0, −100, 50) lokalt
        // — inte längs en axel, så en skala som inte är lika åt alla håll
        // vrider den (normalen tar invers-transponatet, inte skalan själv).
        const std::vector<glaze::Point3> tri{{0, 0, 0}, {10, 0, 0}, {0, 5, 10},
                                             {0, 0, 0}, {0, 5, 10}, {-10, 0, 0}};
        const glaze::Mesh mesh = glaze::Mesh::triangles(tri, gfx::Color{200, 200, 200, 255});
        const W3          origo = w3(data(mesh).origin);
        // Väggen och lådorna högt över marken — ett Ground-lager med innehåll
        // gör i 3D-målet att marken skymmer det under z = 0 (R8b mäter det).
        const W3   at    = sweref + W3{0, 0, 20};
        const W3   skala{1.5, 2.0, 1.0};
        const auto världen = [&](W3 v) { return placerad(v - origo, origo, at, 30.0, skala); };
        const W3   inre    = världen(W3{2, 1.5, 3});   // inne i triangel 0 (s 0,2, t 0,3)
        // Kameran framför (söder om) väggen, som vrids 30° — blicken mot norr.
        ny(kamera3(p3(inre), 40.0, 30.0f, 10.0f));
        const int L = yta.addLayer("vägg");
        yta.setLayerPlacement(L, glaze::LayerPlacement::World);
        yta.addShape(L, glaze::Shape::mesh(mesh, {.at = p3(at), .yaw = 30.0f,
                                                  .scale = {1.5f, 2.0f, 1.0f}}));
        rita();
        const Hal h = hal(yta.camera3D(), box());
        double    px = 0.0, py = 0.0;
        halProj(h, inre, px, py);
        const auto t = av(yta.hitTest(lok(px, py), {}), HitKind::Surface, L);
        // Normalen: R · S⁻¹ på (0, −100, 50) — S⁻¹ ger (0, −50, 50) ∝
        // (0, −1, 1) — vriden med yaw 30 (+y mot bäringen 30) och normerad.
        const double yaw30 = rad(30.0);
        W3 facitN{-std::sin(yaw30), -std::cos(yaw30), 1.0};
        facitN = facitN * (1.0 / len(facitN));
        check(t.size() == 1 && t[0].element == 0 && t[0].instance == -1 &&
                  len(w3(*t[0].point) - inre) < 1e-6 &&
                  len(W3{t[0].normal.x, t[0].normal.y, t[0].normal.z} - facitN) < 1e-9 &&
                  std::abs(t[0].distance - len(inre - h.eye)) < 1e-6,
              "R8 ytan genom placeringen: originalets triangel 0, punkten, "
              "normalen vriden och skalad som facit, avståndet",
              t.empty() ? std::string("ingen") : tal(t[0].distance));

        // INSTANSEN: tre lådor på rad bakom väggen — den i mitten under
        // pekaren.
        const int I = yta.addLayer("lådor");
        yta.setLayerPlacement(I, glaze::LayerPlacement::World);
        const W3 bakom = inre + h.f * 30.0;
        std::vector<glaze::Instance> inst;
        for (int k = -1; k <= 1; ++k)
            inst.push_back({.at = p3(bakom + h.r * (k * 6.0))});
        yta.addShape(I, glaze::Shape::instances(
                            glaze::Shape::box(glaze::Box3{{-1, -1, -1}, {1, 1, 1}}), inst));
        rita();
        const auto alla  = yta.hitTest(lok(px, py), {});
        const auto lådan = av(alla, HitKind::Surface, I);
        check(lådan.size() == 1 && lådan[0].instance == 1,
              "R8 instansen under pekaren svarar med sin plats i indatan (1)",
              lådan.empty() ? std::string("ingen") : std::to_string(lådan[0].instance));
        // SORTERINGEN: väggen före lådan; SKYMNINGEN tar bort lådan.
        const auto ytor = av(alla, HitKind::Surface);
        check(ytor.size() == 2 && ytor[0].layer == L && ytor[1].layer == I &&
                  ytor[0].distance < ytor[1].distance,
              "R8 sorterat på avståndet längs strålen: väggen, sedan lådan");
        glaze::HitOptions sk;
        sk.occlusion = true;
        const auto skymd = av(yta.hitTest(lok(px, py), sk), HitKind::Surface);
        check(skymd.size() == 1 && skymd[0].layer == L,
              "R8 occlusion: det bakom väggen faller bort");
        // Ett HÖRN BAKOM VÄGGEN längs sin egen synlinje.
        glaze::HitOptions hk = sk;
        hk.vertexTolerance   = 4.0f;
        const W3 hörn        = bakom + W3{1, 1, 1};   // ett lådhörn (instans 1)
        double   hx = 0.0, hy = 0.0;
        halProj(h, hörn, hx, hy);
        glaze::HitOptions utan;
        utan.vertexTolerance = 4.0f;
        check(!av(yta.hitTest(lok(hx, hy), utan), HitKind::Vertex, I).empty() &&
                  av(yta.hitTest(lok(hx, hy), hk), HitKind::Vertex, I).empty(),
              "R8 ett hörn bakom väggen träffas utan skymning, inte med");
        // castRay: samma ytor längs en fri stråle, och skymningen.
        W3 o, d;
        halRay(h, px, py, o, d);
        const auto fri = yta.castRay(glaze::Ray3{p3(o), {d.x * 3, d.y * 3, d.z * 3}});
        check(fri.size() == 2 && fri[0].layer == L && fri[1].layer == I &&
                  std::abs(fri[0].distance - t[0].distance) < 1e-6,
              "R8 castRay: samma ytor sorterade, avståndet i världsenheter (en "
              "riktning som inte är enhetlig)");
        // opaqueOnly: väggen halvgenomskinlig (stilens alfa) hoppas över.
        yta.setShape(L, 0, glaze::Shape::mesh(mesh, {.at = p3(at), .yaw = 30.0f,
                                                     .scale = {1.5f, 2.0f, 1.0f}},
                                              {.color = gfx::Color{255, 255, 255, 128}}));
        glaze::HitOptions op;
        op.opaqueOnly = true;
        const auto ogenom = av(yta.hitTest(lok(px, py), op), HitKind::Surface);
        const auto alltid = av(yta.hitTest(lok(px, py), {}), HitKind::Surface);
        glaze::HitOptions gs;
        gs.occlusion = true;
        const auto genomSkymning = av(yta.hitTest(lok(px, py), gs), HitKind::Surface);
        check(ogenom.size() == 1 && ogenom[0].layer == I && alltid.size() == 2 &&
                  genomSkymning.size() == 2,
              "R8 opaqueOnly hoppar över en halvgenomskinlig vägg, och den skymmer "
              "inte",
              std::to_string(ogenom.size()) + " ogenomskinliga, " +
                  std::to_string(alltid.size()) + " alla, " +
                  std::to_string(genomSkymning.size()) + " med skymning");
    }

    // ---- R8b. MARKEN SKYMMER under sig när ett Ground-lager MED INNEHÅLL
    // ritas i 3D-målet (target3d.cpp, markdjupet) — inte ett tomt
    // (ritytans lager 0) och inte utan Ground-lager.
    {
        ny(kamera3(p3(sweref), 50.0, 0.0f, 60.0f));
        const int L = yta.addLayer("grund");
        yta.setLayerPlacement(L, glaze::LayerPlacement::World);
        yta.addShape(L, glaze::Shape::box(glaze::Box3{p3(sweref + W3{-3, -3, -6}),
                                                      p3(sweref + W3{3, 3, -4})}));
        // Marken: en ruta i lager 0, en bit från pekaren.
        yta.addShape(0, glaze::Shape::rect({sweref.x + 20.0, sweref.y + 20.0, 5.0, 5.0},
                                           gfx::Color{200, 200, 200, 255}));
        rita();
        const gfx::Rectf  b = box();
        const glaze::Point mitt = lok(b.x + b.w / 2.0, b.y + b.h / 2.0);
        glaze::HitOptions sk;
        sk.occlusion = true;
        const bool utan = !av(yta.hitTest(mitt, {}), HitKind::Surface, L).empty();
        const bool med  = av(yta.hitTest(mitt, sk), HitKind::Surface, L).empty();
        yta.removeShape(0, 0);   // lager 0 tomt — ingen mark
        const bool tomMark = !av(yta.hitTest(mitt, sk), HitKind::Surface, L).empty();
        yta.setLayerPlacement(0, glaze::LayerPlacement::World);   // inget Ground-lager
        const bool utanMark = !av(yta.hitTest(mitt, sk), HitKind::Surface, L).empty();
        check(utan && med && tomMark && utanMark,
              "R8b en grund under marken: skymd med ett Ground-lager med innehåll, "
              "synlig utan skymning, med ett TOMT Ground-lager och utan "
              "Ground-lager");
    }

    // ---- R10. DEN RITADE DETALJNIVÅN: en kvadrat i två nivåer — nära två
    // trianglar (0, 1), bortom 100 en enda stor (2) på samma plats. Nivån
    // väljs ur avståndet från ögat till den placerade lådan, som ritningen.
    {
        glaze::MeshDesc md;
        // Den grova nivån en hundradel UNDER den fina: utan nivåvalet vinner
        // den fina också fjärran (den ligger närmast), och lika avstånd
        // avgörs aldrig av avrundningen.
        md.positions = {{-5, -5, 0}, {5, -5, 0}, {5, 5, 0}, {-5, 5, 0},
                        {-6, -6, -0.01f}, {6, -6, -0.01f}, {0, 8, -0.01f}};
        md.indices   = {0, 1, 2, 0, 2, 3, 4, 5, 6};
        md.lods      = {{0, 6, 100.0}, {6, 3, 0.0}};
        md.origin    = p3(sweref + W3{0, 0, 5});
        const glaze::Mesh nivåer(std::move(md));
        const auto        träff = [&](double avst) {
            ny(kamera3(p3(sweref + W3{0, 0, 5}), avst, 0.0f, 89.0f));
            const int L = yta.addLayer("nivåer");
            yta.setLayerPlacement(L, glaze::LayerPlacement::World);
            yta.addShape(L, glaze::Shape::mesh(nivåer));
            const gfx::Rectf b = box();
            // Strax ovanför och till vänster om mitten: i triangel 0 (nära) och i
            // den stora (fjärran).
            const auto t = av(yta.hitTest(lok(b.x + b.w / 2.0 + 2.0, b.y + b.h / 2.0 - 1.0), {}),
                              HitKind::Surface, L);
            return t.size() == 1 ? t[0].element : std::int64_t{-1};
        };
        const std::int64_t nära = träff(40.0), fjärran = träff(300.0);
        check((nära == 0 || nära == 1) && fjärran == 2,
              "R10 nära plockas den fina nivåns triangel, bortom 100 den grova — "
              "den RITADE nivån",
              std::to_string(nära) + " / " + std::to_string(fjärran));
    }

    // ---- R11. LINJEN I RYMDEN inom halva bredden + toleransen, segmentet
    {
        ny(kamera3(p3(sweref), 60.0, 15.0f, 35.0f));
        const int L = yta.addLayer("linje");
        const std::vector<glaze::Point3> väg{p3(sweref + W3{-20, 0, 0}),
                                             p3(sweref + W3{0, 5, 8}),
                                             p3(sweref + W3{20, -5, 3})};
        yta.addShape(L, glaze::Shape::polyline3(väg, {.width = glaze::px(4.0f)}));
        rita();
        const Hal h = hal(yta.camera3D(), box());
        double    ax = 0.0, ay = 0.0, bx = 0.0, by = 0.0;
        halProj(h, w3(väg[1]), ax, ay);
        halProj(h, w3(väg[2]), bx, by);
        const double ex = bx - ax, ey = by - ay, el = std::hypot(ex, ey);
        const double nx = -ey / el, ny2 = ex / el;
        const double grans = (yta.behaviorCtx().style->canvasHitTolerance + 2.0) * s();
        const auto   vid   = [&](double d) {
            const auto t = av(yta.hitTest(lok((ax + bx) / 2 + nx * d, (ay + by) / 2 + ny2 * d), {}),
                              HitKind::Shape, L);
            return t.size() == 1 && t[0].element == 1;
        };
        check(vid(grans - 0.3) && vid(-(grans - 0.3)) && !vid(grans + 0.3),
              "R11 linjen i rymden träffas inom halva bredden + toleransen på "
              "skärmen (segment 1), inte utanför");
    }

    // ---- R12. SKYMNINGEN LÄNGS SYNLINJEN: ett hörn precis till höger om en
    // väggkant är SYNLIGT fast pekaren står över väggen (och pekarens stråle
    // når väggen först) — och ett hörn precis till vänster om kanten, bakom
    // väggen, är det inte.
    {
        ny(kamera3(p3(sweref + W3{0, 50, 0}), 50.0, 0.0f, 0.0f));   // ögat i sweref, blicken norrut
        const int V = yta.addLayer("vägg");
        yta.setLayerPlacement(V, glaze::LayerPlacement::World);
        yta.addShape(V, glaze::Shape::mesh(glaze::Mesh::indexed(
                            {p3(sweref + W3{-10, 30, -5}), p3(sweref + W3{0, 30, -5}),
                             p3(sweref + W3{0, 30, 5}), p3(sweref + W3{-10, 30, 5})},
                            {0, 1, 2, 0, 2, 3}, gfx::Color{120, 120, 120, 255})));
        const int B = yta.addLayer("bakom");
        yta.setLayerPlacement(B, glaze::LayerPlacement::World);
        yta.addShape(B, glaze::Shape::mesh(glaze::Mesh::triangles(
                            {p3(sweref + W3{0.5, 60, 0}), p3(sweref + W3{6, 60, -2}),
                             p3(sweref + W3{6, 60, 2}), p3(sweref + W3{-0.5, 60, 0}),
                             p3(sweref + W3{-6, 60, 2}), p3(sweref + W3{-6, 60, -2})},
                            gfx::Color{200, 60, 60, 255})));
        rita();
        const gfx::Rectf b    = box();
        const double     mitt = b.x + b.w / 2.0, y = b.y + b.h / 2.0;
        glaze::HitOptions o;
        o.occlusion       = true;
        o.vertexTolerance = 9.0f;
        // Pekaren 1 px till VÄNSTER om väggkanten (över väggen): hörnet till
        // höger (≈ 5 px bort) syns, hörnet till vänster (bakom väggen) inte.
        const auto höger = av(yta.hitTest(lok(mitt - 1.0, y), o), HitKind::Vertex, B);
        const auto vänster = av(yta.hitTest(lok(mitt - 3.0, y), o), HitKind::Vertex, B);
        // Utan skymning är hörnet bakom väggen det närmaste på skärmen.
        glaze::HitOptions utan = o;
        utan.occlusion         = false;
        const auto närmast     = av(yta.hitTest(lok(mitt - 1.0, y), utan), HitKind::Vertex, B);
        check(närmast.size() == 1 && närmast[0].element == 3,
              "R12 utan skymning: det närmaste hörnet på skärmen (bakom väggen)");
        check(höger.size() == 1 && höger[0].element == 0,
              "R12 ett hörn precis bredvid väggkanten syns längs SIN synlinje, fast "
              "pekarens stråle når väggen först",
              höger.empty() ? std::string("inget") : std::to_string(höger[0].element));
        check(vänster.empty() ||
                  (vänster.size() == 1 && vänster[0].element != 3),
              "R12 hörnet bakom väggen skyms");
    }

    // ---- R13. INSTANSENS PLATS I INDATAN genom cellerna: 900 lådor i ett
    // rutnät, inlagda i OMVÄND ordning — cellernas ordning (Morton) är en
    // annan än indatans.
    {
        ny(kamera3(p3(sweref + W3{7 * 4.0, 11 * 4.0, 1.0}), 40.0, 0.0f, 89.0f));
        const int I = yta.addLayer("rutnät");
        yta.setLayerPlacement(I, glaze::LayerPlacement::World);
        std::vector<glaze::Instance> inst;
        for (int k = 899; k >= 0; --k)
            inst.push_back({.at = p3(sweref + W3{(k / 30) * 4.0, (k % 30) * 4.0, 0.0})});
        yta.addShape(I, glaze::Shape::instances(
                            glaze::Shape::box(glaze::Box3{{-1, -1, 0}, {1, 1, 1}}), inst));
        rita();
        const gfx::Rectf b = box();
        const auto t = av(yta.hitTest(lok(b.x + b.w / 2.0, b.y + b.h / 2.0), {}),
                          HitKind::Surface, I);
        // Lådan (7, 11) är rutnätets plats 7 · 30 + 11 = 221 — i omvänd
        // ordning indatans 899 − 221 = 678.
        check(t.size() == 1 && t[0].instance == 678,
              "R13 instansen svarar med sin plats i INDATAN också genom cellerna",
              t.empty() ? std::string("ingen") : std::to_string(t[0].instance));
    }

    // ---- R-skym. SKYMMARNA FRAMIFRÅN: en vägg och 200 meshar
    // bakom den längs strålen, var och en sin egen mesh (en egen BVH). Efter
    // `castRay` med skymning har ingen av dem bakom väggen byggt sin BVH — de
    // prövades aldrig — medan väggens är byggd. Utan skymning träffas alla
    // 201 (och bygger då sina): strålen går genom varje låda, så det är
    // skymningen och inget annat som håller dem orörda.
    {
        ny(kamera3(p3(sweref + W3{0, 50, 0}), 50.0, 0.0f, 0.0f));   // blicken norrut
        const auto ruta = [&](double y, double halv, gfx::Color färg) {
            return glaze::Mesh::indexed(
                {p3(sweref + W3{-halv, y, -halv}), p3(sweref + W3{halv, y, -halv}),
                 p3(sweref + W3{halv, y, halv}), p3(sweref + W3{-halv, y, halv})},
                {0, 1, 2, 0, 2, 3}, färg);
        };
        const int V = yta.addLayer("vägg");
        yta.setLayerPlacement(V, glaze::LayerPlacement::World);
        const glaze::Mesh vägg = ruta(10.0, 10.0, gfx::Color{120, 120, 120, 255});
        yta.addShape(V, glaze::Shape::mesh(vägg));
        const int B = yta.addLayer("bakom");
        yta.setLayerPlacement(B, glaze::LayerPlacement::World);
        std::vector<glaze::Mesh> bakom;
        for (int k = 0; k < 200; ++k) {
            bakom.push_back(ruta(20.0 + k, 1.0, gfx::Color{200, 60, 60, 255}));
            yta.addShape(B, glaze::Shape::mesh(bakom.back()));
        }
        rita();
        // Strålen norrut, en bit från rutornas diagonal (en träff inne i en
        // triangel, inte på kanten mellan två).
        const glaze::Ray3 stråle{p3(sweref + W3{0.3, 0, -0.2}), {0.0, 1.0, 0.0}};
        const auto byggd = [](const glaze::Mesh& m) {
            return !MeshAccess::data(m)->picking->bvh.nodes.empty();
        };
        glaze::HitOptions sk;
        sk.occlusion     = true;
        const auto skymd = yta.castRay(stråle, sk);
        int        byggda = 0;
        for (const glaze::Mesh& m : bakom)
            byggda += byggd(m) ? 1 : 0;
        check(skymd.size() == 1 && skymd[0].layer == V &&
                  std::abs(skymd[0].distance - 10.0) < 1e-9 && byggd(vägg) &&
                  byggda == 0,
              "R-skym castRay med skymning: bara väggen, och ingen av de 200 "
              "meshernas BVH bakom den byggd",
              std::to_string(skymd.size()) + " träffar, " + std::to_string(byggda) +
                  " BVH:er byggda");
        const auto alla = yta.castRay(stråle);
        check(alla.size() == 201 && alla[0].layer == V && alla[200].layer == B &&
                  std::abs(alla[200].distance - 219.0) < 1e-9,
              "R-skym utan skymning: väggen och alla 200 bakom den — strålen går "
              "genom varje låda",
              std::to_string(alla.size()) + " träffar");
    }

    // ---- R-snap. LAGRETS SNAP: en höjdmodell i lager 0
    // (Terrain) och en stolpe i ett World-lager. Med snap träffar pekaren
    // bredvid modellens hörn hörnet, en kant och ytan; med `setLayerSnap(0,
    // false)` kommer ingen Vertex eller Edge från lager 0, ytan står kvar —
    // densamma, och lika med ytan plockad på nytt med `opaqueOnly` (som inte
    // återanvänder den ur terrängens loop) — och stolpens hörn träffas
    // fortfarande.
    {
        ny(kamera3(p3(sweref + W3{50, 50, 2}), 120.0, 20.0f, 50.0f));
        yta.setLayerPlacement(0, glaze::LayerPlacement::Terrain);
        const auto höjd = [](double x, double y) { return 0.05 * x + 0.02 * y; };
        std::vector<glaze::Point3> läge;
        for (int j = 0; j <= 10; ++j)
            for (int i = 0; i <= 10; ++i)
                läge.push_back(p3(sweref + W3{i * 10.0, j * 10.0, höjd(i * 10.0, j * 10.0)}));
        std::vector<std::uint32_t> idx;
        for (int j = 0; j < 10; ++j)
            for (int i = 0; i < 10; ++i) {
                const std::uint32_t a = static_cast<std::uint32_t>(j * 11 + i);
                idx.insert(idx.end(), {a, a + 1, a + 12, a, a + 12, a + 11});
            }
        yta.addShape(0, glaze::Shape::mesh(glaze::Mesh::indexed(
                            läge, idx, gfx::Color{80, 140, 60, 255})));
        const int  S   = yta.addLayer("stolpe");
        const W3   fot = sweref + W3{30, 70, höjd(30, 70)};
        const W3   topp{fot.x + 1.0, fot.y, fot.z + 15.0};   // stolpens hörn 2
        yta.setLayerPlacement(S, glaze::LayerPlacement::World);
        yta.addShape(S, glaze::Shape::mesh(glaze::Mesh::indexed(
                            {p3(fot + W3{-1, 0, 0}), p3(fot + W3{1, 0, 0}), p3(topp),
                             p3(fot + W3{-1, 0, 15})},
                            {0, 1, 2, 0, 2, 3}, gfx::Color{90, 90, 90, 255})));
        rita();
        const Hal h = hal(yta.camera3D(), box());
        // Pekaren drygt två px från modellens hörn (5, 5) — plats 5 · 11 + 5 =
        // 60 — inne i en triangel, och från stolpens topp.
        double hx = 0.0, hy = 0.0, sx = 0.0, sy = 0.0;
        halProj(h, w3(läge[60]), hx, hy);
        halProj(h, topp, sx, sy);
        const glaze::Point vidHörnet = lok(hx + 2.0 * s(), hy + 1.0 * s());
        const glaze::Point vidToppen = lok(sx + 2.0 * s(), sy + 1.0 * s());
        glaze::HitOptions o;
        o.vertexTolerance = 6.0f;
        o.edgeTolerance   = 6.0f;
        const auto med     = yta.hitTest(vidHörnet, o);
        const auto ytaMed  = av(med, HitKind::Surface, 0);
        const auto hörnMed = av(med, HitKind::Vertex, 0);
        check(yta.layerSnap(0) && hörnMed.size() == 1 && hörnMed[0].element == 60 &&
                  av(med, HitKind::Edge, 0).size() == 1 && ytaMed.size() == 1,
              "R-snap med snap (förvalet): modellens hörn 60, en kant och EN yta",
              std::to_string(hörnMed.size()) + " hörn, " +
                  std::to_string(ytaMed.size()) + " ytor");
        yta.setLayerSnap(0, false);
        const auto utan    = yta.hitTest(vidHörnet, o);
        const auto ytaUtan = av(utan, HitKind::Surface, 0);
        glaze::HitOptions opak = o;
        opak.opaqueOnly        = true;
        const auto omplockad   = av(yta.hitTest(vidHörnet, opak), HitKind::Surface, 0);
        const auto sammaYta    = [&](const std::vector<glaze::HitResult>& a) {
            return a.size() == 1 && ytaMed.size() == 1 && a[0].element == ytaMed[0].element &&
                   a[0].point && ytaMed[0].point &&
                   len(w3(*a[0].point) - w3(*ytaMed[0].point)) == 0.0 &&
                   a[0].distance == ytaMed[0].distance;
        };
        check(!yta.layerSnap(0) && av(utan, HitKind::Vertex, 0).empty() &&
                  av(utan, HitKind::Edge, 0).empty() && sammaYta(ytaUtan) &&
                  sammaYta(omplockad),
              "R-snap utan snap: inget hörn och ingen kant från lager 0, och ytan "
              "densamma — också plockad på nytt med opaqueOnly",
              std::to_string(av(utan, HitKind::Vertex, 0).size()) + " hörn, " +
                  std::to_string(ytaUtan.size()) + " ytor");
        const auto stolpen = av(yta.hitTest(vidToppen, o), HitKind::Vertex, S);
        check(stolpen.size() == 1 && stolpen[0].element == 2,
              "R-snap stolpens hörn i ett lager med snap träffas fortfarande",
              std::to_string(stolpen.size()) + " hörn");
    }

    // ---- R9. ROUTNINGEN TILL VYN
    {
        ny(kamera3(p3(sweref), 60.0, 0.0f, 40.0f));
        // Huvudscenen: en mark-mesh vid SWEREF. Vyn: en kub vid origo, bara
        // i vyn, med en egen ortografisk kamera.
        const int M = yta.addLayer("mark");
        yta.setLayerPlacement(M, glaze::LayerPlacement::World);
        yta.addShape(M, glaze::Shape::box(glaze::Box3{p3(sweref + W3{-30, -30, -1}),
                                                      p3(sweref + W3{30, 30, 0})}));
        const int K = yta.addLayer("kub");
        yta.setLayerPlacement(K, glaze::LayerPlacement::World);
        yta.addShape(K, glaze::Shape::box(glaze::Box3{{-1, -1, -1}, {1, 1, 1}}));
        // Och en 2D-ruta på marken vid origo, också bara i vyn: den träffas
        // genom VYNS projektion — huvudvyns lägger pekaren vid SWEREF.
        const int P = yta.addLayer("ruta");
        yta.addShape(P, glaze::Shape::rect({-0.5, -0.5, 1.0, 1.0}, gfx::Color{0, 0, 0, 255}));
        glaze::View vy;
        vy.anchor = gfx::TextAnchor::TopRight;
        vy.offset = {-8.0, 8.0};
        vy.width  = 120.0;
        vy.height = 120.0;
        vy.camera = kamera3({0.0, 0.0, 0.0}, 6.0, 30.0f, 30.0f, true);
        const int v = yta.addView(vy);
        yta.setLayerViews(K, {v});
        yta.setLayerViews(P, {v});
        rita();
        // Vyns mitt i lokala px: ytan är 640 bred.
        const auto iVyn = yta.hitTest(glaze::Point{640.0 - 8.0 - 60.0, 8.0 + 60.0}, {});
        const auto kub  = av(iVyn, HitKind::Surface, K);
        const auto mark = av(iVyn, HitKind::Surface, M);
        const auto utan = yta.hitTest(glaze::Point{320.0, 240.0}, {});
        check(kub.size() == 1 && kub[0].view == v && mark.empty() &&
                  !av(utan, HitKind::Surface, M).empty() &&
                  av(utan, HitKind::Surface, K).empty() &&
                  av(utan, HitKind::Surface, M)[0].view == 0,
              "R9 i del-vyns ruta träffas vyns kub med vyns kamera (view = 1), "
              "utanför huvudvyns mark (view = 0)",
              std::to_string(kub.size()) + " kub, " + std::to_string(mark.size()) +
                  " mark");
        // Vyns mitt är kamerans mål, så rutans träffpunkt är origo.
        const auto ruta = av(iVyn, HitKind::Shape, P);
        check(ruta.size() == 1 && ruta[0].view == v && ruta[0].point &&
                  std::abs(ruta[0].point->x) < 1e-6 && std::abs(ruta[0].point->y) < 1e-6 &&
                  std::abs(ruta[0].point->z) < 1e-9 && av(utan, HitKind::Shape, P).empty(),
              "R9 2D-rutan på marken i del-vyn träffas genom vyns projektion, i origo",
              ruta.empty() ? std::string("ingen")
                           : std::to_string(ruta[0].point ? ruta[0].point->x : -1.0));
    }
}

// Ritytans del i ett dolt fönster; false när inget fönster kunde skapas.
bool fallRitytan()
{
    Värd w;
    w.setOffscreen(true);
    bool kördes = false;
    glaze::app().post([&] {
        kördes = true;
        w.kör();
        w.close();
    });
    w.run();
    if (!kördes)
        std::printf("  --   ritytans fall hoppades över: inget fönster kunde skapas "
                    "(ingen display)\n");
    return kördes;
}

int main(int argc, char** argv)
{
    bool stolpe = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
        if (std::strcmp(argv[i], "--stolpe") == 0)
            stolpe = true;
    }
    std::printf("=== pick_gate — 3D-plockningens kärna (BVH, index, filter, plan) ===\n");
    fallMotBruteForce();
    fallExaktRutnät();
    fallBvhFormen();
    fallOriginalindex();
    fallOpak();
    fallLängstBort();
    fallSetPositions();
    fallUtanKopia();
    fallTrådarna();
    fallPlanet();
    fallTerrängIndex();
    const bool ritytan = fallRitytan();
    if (stolpe)
        tidStolpe();
    std::printf(g_fel ? "=== pick_gate: %d FEL av %d ===\n"
                      : "=== pick_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    if (g_fel)
        return 1;
    return ritytan ? 0 : 77;
}
