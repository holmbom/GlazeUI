// placement_gate — G8: ETIKETTPLACERINGENS ALGORITM, headless.
//
// Kör:  ./build/tests/placement_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll
//
// `glaze::detail::placeLabels` (src/canvas/placement.h) är en REN funktion:
// lådor i skärmrymd in, en synlighetsflagga per etikett ut. Grinden mäter
// den mot HANDRÄKNADE fall — varje förväntan är ett tal eller en flagga som
// står i källan, aldrig ett svar ur implementationens formler:
//
//   P1  överlapp ⇒ den lägre prioriteten döljs, oavsett indatans ordning
//   P2  utan överlapp syns alla
//   P3  ordningen: prioritet före lager före index; lagret som ritas överst
//       och den etikett som kom först vinner vid lika prioritet; en dold
//       etikett blockerar ingen
//   P4  vridningen: den OMSLUTANDE lådan, med halva mått räknade för hand
//       (100 × 10 px i 30° ⇒ ±45,80 × ±29,33 — en sond 0,1 px innanför
//       respektive utanför)
//   P5  luften: per låda, kanter som bara möts krockar inte, negativ luft
//       krymper, och en låda utan yta (punkt, streck) krockar med ingenting
//   P6  `collide = false` döljs aldrig och täcker ingen; en icke-ändlig låda
//       döljs och täcker ingen
//   P7  rutnätets GRANNCELLER: en låda vars krock ligger i en ANNAN cell än
//       dess mitt, en låda större än flera celler, lådor utanför skärmen och
//       bortom cellernas tak
//   P8  rutnätet mot en RÄTTFRAM REFERENS (alla par prövas, inget rutnät) på
//       2 000 slumpade lådor med fast frö — heltalskoordinater och 0°/90°,
//       så båda vägarna räknar EXAKT och varje kant som möts är en riktig
//       kantberöring. Referensen skriver ordningen och lådan för sig.
//   P9  determinismen: samma indata ⇒ samma svar, och svaret beror inte av
//       `screen` (riktig, 1 × 1, enorm, förskjuten, icke-ändlig)
//   P10 tiden för 10 000 etiketter — MÄTT OCH UTSKRIVEN, inte grindad (målet
//       är < 2 ms i Release; Debug är långsammare)
//
// SABOTAGEN den fäller:
//   * sorteringen omvänd (P1, P3)
//   * lådan oroterad (P4)
//   * rutnätet missar grannceller — frågar bara mittens cell, eller lägger
//     lådan bara i mittens cell (P7, P8)
//
// VAD DEN INTE MÄTER:
//   * var lådorna KOMMER IFRÅN — projektionen av ankarna och glyflayoutens
//     `TextBox` → skärmlåda är ritytans.
//   * synlighetstexturen och tonandet — de finns inte än.
//   * tiden som grind: datorerna skiljer sig, och `canvas_perf` är
//     platsen för prestandakrav.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "canvas/placement.h"

namespace {

using glaze::detail::LabelBox;
using glaze::detail::placeLabels;

int  g_fails   = 0;
int  g_cases   = 0;
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

// En skärm i fullhd — ritytans box i fysiska px.
const gfx::Rectf kSkärm{0.0f, 0.0f, 1920.0f, 1080.0f};

std::string flaggor(const std::vector<std::uint8_t>& v)
{
    std::string s;
    for (std::uint8_t f : v)
        s += f ? '1' : '0';
    return s;
}

// Vridningen i GRADER som lådans riktning (baslinjens enhetsvektor) — räknad
// i double här, i grinden, ur vinkeln som står i källan.
void vrid(LabelBox& l, double grader)
{
    const double v = grader * 3.14159265358979323846 / 180.0;
    l.dirX         = static_cast<float>(std::cos(v));
    l.dirY         = static_cast<float>(std::sin(v));
}

// En låda ur mitt + halva mått; resten ur argumenten.
LabelBox låda(float cx, float cy, float hw, float hh, int prio = 0,
              int lager = 0, float vinkel = 0.0f, float luft = 0.0f)
{
    LabelBox l;
    l.cx       = cx;
    l.cy       = cy;
    l.halfW    = hw;
    l.halfH    = hh;
    l.priority = prio;
    l.layer    = lager;
    vrid(l, vinkel);
    l.padding  = luft;
    return l;
}

std::vector<std::uint8_t> placera(const std::vector<LabelBox>& v,
                                  const gfx::Rectf& skärm = kSkärm)
{
    return placeLabels(v, skärm);
}

// ---------------------------------------------------------------------- P1

void fallPrioritet()
{
    // Två lådor som överlappar med 10 px.
    const LabelBox låg  = låda(100, 100, 40, 10, 0);
    const LabelBox hög  = låda(170, 100, 40, 10, 1);
    check(flaggor(placera({låg, hög})) == "01",
          "P1: överlapp ⇒ den LÄGRE prioriteten döljs (den låga först)",
          flaggor(placera({låg, hög})));
    check(flaggor(placera({hög, låg})) == "10",
          "P1: …och oavsett ordning (den höga först)",
          flaggor(placera({hög, låg})));
    // Negativ prioritet är bara ett tal.
    const LabelBox neg = låda(100, 100, 40, 10, -5);
    check(flaggor(placera({neg, låg})) == "01",
          "P1: −5 förlorar mot 0", flaggor(placera({neg, låg})));
}

// ---------------------------------------------------------------------- P2

void fallIngenKrock()
{
    std::vector<LabelBox> v;
    for (int i = 0; i < 10; ++i)
        for (int j = 0; j < 5; ++j)
            v.push_back(låda(60.0f + i * 150.0f, 40.0f + j * 100.0f, 70.0f,
                             20.0f, (i * 7 + j) % 3));
    const auto f = placera(v);
    check(std::all_of(f.begin(), f.end(), [](std::uint8_t x) { return x; }),
          "P2: 50 lådor utan överlapp syns alla", flaggor(f));
    check(placera({}).empty(), "P2: tom indata ⇒ tomt svar");
}

// ---------------------------------------------------------------------- P3

void fallOrdning()
{
    // LAGRET vid lika prioritet: lager 1 ritas överst och vinner, också när
    // lager 0 står först i indatan.
    const LabelBox l0 = låda(100, 100, 40, 10, 0, 0);
    const LabelBox l1 = låda(130, 100, 40, 10, 0, 1);
    check(flaggor(placera({l0, l1})) == "01",
          "P3: lika prioritet ⇒ det ÖVRE lagret vinner", flaggor(placera({l0, l1})));

    // INDEX vid lika prioritet och lager: den som kom FÖRST behåller platsen.
    const LabelBox a = låda(100, 100, 40, 10, 0, 2);
    const LabelBox b = låda(130, 100, 40, 10, 0, 2);
    check(flaggor(placera({a, b})) == "10",
          "P3: lika prioritet och lager ⇒ den FÖRSTA vinner", flaggor(placera({a, b})));

    // PRIORITET före lager: prioritet 1 i lager 0 slår prioritet 0 i lager 9.
    const LabelBox p1 = låda(100, 100, 40, 10, 1, 0);
    const LabelBox p0 = låda(130, 100, 40, 10, 0, 9);
    check(flaggor(placera({p0, p1})) == "01",
          "P3: prioriteten går före lagret", flaggor(placera({p0, p1})));

    // En DOLD etikett blockerar ingen: A(2) krockar med B(1), B med C(0),
    // A inte med C ⇒ A och C syns.
    const LabelBox A = låda(100, 100, 40, 10, 2);
    const LabelBox B = låda(170, 100, 40, 10, 1);
    const LabelBox C = låda(240, 100, 40, 10, 0);
    check(flaggor(placera({C, B, A})) == "101",
          "P3: den dolda B blockerar inte C (girigt i prioritetsordning)",
          flaggor(placera({C, B, A})));

    // Tre i rad med samma prioritet men olika lager — ordningen är en
    // KEDJA: lager 2 först, sedan 1 (krockar med 2 ⇒ dold), sedan 0 (krockar
    // bara med den dolda 1 ⇒ syns).
    const LabelBox k0 = låda(100, 300, 40, 10, 0, 0);
    const LabelBox k1 = låda(170, 300, 40, 10, 0, 1);
    const LabelBox k2 = låda(240, 300, 40, 10, 0, 2);
    check(flaggor(placera({k0, k1, k2})) == "101",
          "P3: lagerkedjan 2 → 1 → 0", flaggor(placera({k0, k1, k2})));
}

// ---------------------------------------------------------------------- P4

void fallVridning()
{
    // En lång smal etikett, 100 × 10 px, och en liten sond 40 px under dess
    // mitt. Ovriden: etikettens y-intervall är [95, 105], sondens [135, 145]
    // — ingen krock. I 90° är etiketten LODRÄT, y-intervallet [50, 150] —
    // krock, och sonden (lägre prioritet) döljs.
    const LabelBox sond = låda(100, 140, 5, 5, 0);
    check(flaggor(placera({låda(100, 100, 50, 5, 1, 0, 0.0f), sond})) == "11",
          "P4: ovriden lång etikett når inte sonden under den");
    check(flaggor(placera({låda(100, 100, 50, 5, 1, 0, 90.0f), sond})) == "10",
          "P4: i 90° står den lodrätt och täcker sonden");
    check(flaggor(placera({låda(100, 100, 50, 5, 1, 0, -90.0f), sond})) == "10",
          "P4: −90° ger samma omslutande låda");
    check(flaggor(placera({låda(100, 100, 50, 5, 1, 0, 180.0f), sond})) == "11",
          "P4: 180° är ovriden igen");

    // 30°: den omslutande lådans halva mått för hand —
    //   x: 50·cos30 + 5·sin30 = 43,3013 + 2,5    = 45,8013
    //   y: 50·sin30 + 5·cos30 = 25     + 4,3301 = 29,3301
    // En sond med halva höjden 0,5 vars underkant ligger 0,1 px innanför
    // respektive utanför lådans överkant/underkant, och likadant i x.
    constexpr float hx = 45.8013f;
    constexpr float hy = 29.3301f;
    const LabelBox  e30 = låda(500, 500, 50, 5, 1, 0, 30.0f);
    const LabelBox  e330 = låda(500, 500, 50, 5, 1, 0, -30.0f);
    auto sondY = [&](float d) { return låda(500, 500 + hy + 0.5f + d, 0.5f, 0.5f); };
    auto sondX = [&](float d) { return låda(500 + hx + 0.5f + d, 500, 0.5f, 0.5f); };
    check(flaggor(placera({e30, sondY(-0.1f)})) == "10",
          "P4: 30° — sonden 0,1 px INOM den omslutande höjden (29,33) krockar");
    check(flaggor(placera({e30, sondY(+0.1f)})) == "11",
          "P4: 30° — 0,1 px utanför krockar inte");
    check(flaggor(placera({e30, sondX(-0.1f)})) == "10",
          "P4: 30° — sonden 0,1 px INOM den omslutande bredden (45,80) krockar");
    check(flaggor(placera({e30, sondX(+0.1f)})) == "11",
          "P4: 30° — 0,1 px utanför krockar inte");
    check(flaggor(placera({e330, sondY(-0.1f)})) == "10" &&
              flaggor(placera({e330, sondY(+0.1f)})) == "11",
          "P4: −30° ger samma omslutande låda som 30°");
}

// ---------------------------------------------------------------------- P5

void fallLuft()
{
    // Två lådor med 6 px mellan kanterna: A [60, 140], B [146, 226] i x.
    auto par = [](float luftA, float luftB) {
        return std::vector<LabelBox>{låda(100, 100, 40, 10, 1, 0, 0, luftA),
                                     låda(186, 100, 40, 10, 0, 0, 0, luftB)};
    };
    check(flaggor(placera(par(0, 0))) == "11", "P5: 6 px emellan, ingen luft — båda syns");
    check(flaggor(placera(par(2, 2))) == "11", "P5: 2 + 2 px luft < 6 — båda syns");
    check(flaggor(placera(par(3, 3))) == "11",
          "P5: 3 + 3 px luft = 6 — kanterna MÖTS, ingen krock");
    check(flaggor(placera(par(3.5f, 3.5f))) == "10",
          "P5: 3,5 + 3,5 px luft > 6 — den lägre döljs");
    check(flaggor(placera(par(0, 6.5f))) == "10",
          "P5: luften är PER LÅDA — 6,5 px på den ena räcker");
    // Negativ luft krymper: två lådor som överlappar med 2 px, −1,5 var ⇒
    // 1 px emellan.
    const std::vector<LabelBox> ihop{låda(100, 300, 40, 10, 1, 0, 0, -1.5f),
                                     låda(178, 300, 40, 10, 0, 0, 0, -1.5f)};
    check(flaggor(placera(ihop)) == "11",
          "P5: negativ luft krymper lådan (2 px överlapp, −1,5 var)");
    const std::vector<LabelBox> ihop0{låda(100, 300, 40, 10, 1),
                                      låda(178, 300, 40, 10, 0)};
    check(flaggor(placera(ihop0)) == "10", "P5: …och utan den krockar de");
    // Luften får inte vända lådan: −100 på en låda med halva måtten 40 × 10
    // ger en punkt — och en låda utan yta krockar med ingenting, inte ens
    // mitt i en annan (snittet har ingen yta).
    const std::vector<LabelBox> punkt{låda(100, 500, 40, 10, 1, 0, 0, -100.0f),
                                      låda(100, 500, 40, 10, 0)};
    check(flaggor(placera(punkt)) == "11",
          "P5: luften krymper aldrig under noll — en punkt mitt i en låda "
          "täcker den inte");
    // Ett STRECK likaså: en låda med höjden noll tvärs över en annan.
    const std::vector<LabelBox> streck{låda(100, 700, 60, 0, 1),
                                       låda(100, 700, 20, 20, 0)};
    check(flaggor(placera(streck)) == "11",
          "P5: en låda utan höjd tvärs över en annan täcker den inte");
}

// ---------------------------------------------------------------------- P6

void fallUtanKollision()
{
    LabelBox fri = låda(100, 100, 40, 10, 9);   // högst prioritet
    fri.collide  = false;
    const LabelBox under = låda(110, 100, 40, 10, 0);
    check(flaggor(placera({fri, under})) == "11",
          "P6: collide = false TÄCKER INGEN — den lägre syns ändå");
    LabelBox fri2 = låda(110, 100, 40, 10, -9);   // lägst prioritet
    fri2.collide  = false;
    const LabelBox över = låda(100, 100, 40, 10, 0);
    check(flaggor(placera({över, fri2})) == "11",
          "P6: collide = false DÖLJS ALDRIG — inte ens under en högre");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    LabelBox    trasig = låda(nan, 100, 40, 10, 9);
    check(flaggor(placera({trasig, under})) == "01",
          "P6: NaN i mitten ⇒ döljs, och täcker ingen");
    trasig = låda(100, 100, inf, 10, 9);
    check(flaggor(placera({trasig, under})) == "01",
          "P6: oändligt mått ⇒ döljs, och täcker ingen");
    trasig = låda(100, 100, 40, 10, 9, 0, nan);
    check(flaggor(placera({trasig, under})) == "01", "P6: NaN-riktning ⇒ döljs");
    trasig         = låda(nan, nan, nan, nan, 9);
    trasig.collide = false;
    check(flaggor(placera({trasig, under})) == "11",
          "P6: collide = false syns även trasig (den deltar inte)");
}

// ---------------------------------------------------------------------- P7

void fallGrannceller()
{
    // Cellerna är 64 px. A täcker x [10, 63] — bara cell 0. B täcker
    // [60, 100] med mitten (80) i cell 1; krocken ligger i cell 0.
    const LabelBox A = låda(36.5f, 30, 26.5f, 10, 1);
    const LabelBox B = låda(80, 30, 20, 10, 0);
    check(flaggor(placera({A, B})) == "10",
          "P7: krocken i en ANNAN cell än den nya lådans mitt");

    // Samma i y, och diagonalt: A bara i cell (0,0), B med mitten i cell
    // (1,1) och hörnet nere i (0,0), där krocken ligger.
    const LabelBox Ay = låda(300, 36.5f, 10, 26.5f, 1);
    const LabelBox By = låda(300, 80, 10, 20, 0);
    check(flaggor(placera({Ay, By})) == "10", "P7: …likadant i y");
    const LabelBox Ad = låda(40, 40, 23, 23, 1);   // [17, 63]²
    const LabelBox Bd = låda(80, 80, 20, 20, 0);   // [60, 100]²
    check(flaggor(placera({Ad, Bd})) == "10", "P7: …och diagonalt");

    // En låda STÖRRE ÄN FLERA CELLER: x [5, 381] (mitten i cell 3), och en
    // liten i cell 0 som bara når dess vänstra ände. Omvänt också: den stora
    // insatt sist.
    const LabelBox stor  = låda(193, 400, 188, 10, 1);
    const LabelBox liten = låda(14, 400, 6, 6, 0);
    check(flaggor(placera({stor, liten})) == "10",
          "P7: en liten låda i kanten av en STOR (flera celler)");
    const LabelBox stor0  = låda(193, 600, 188, 10, 0);
    const LabelBox liten1 = låda(14, 600, 6, 6, 1);
    check(flaggor(placera({liten1, stor0})) == "10",
          "P7: …och den stora insatt EFTER den lilla");

    // UTANFÖR SKÄRMEN: två lådor som krockar långt till vänster och nedanför
    // delar kantcellerna — svaret är detsamma som på skärmen.
    check(flaggor(placera({låda(-5000, 100, 40, 10, 1),
                           låda(-4970, 100, 40, 10, 0)})) == "10",
          "P7: krock långt till vänster om skärmen");
    check(flaggor(placera({låda(-5000, 100, 40, 10, 1),
                           låda(-4000, 100, 40, 10, 0)})) == "11",
          "P7: …och två som INTE krockar där syns båda");
    // 10¹² px bort är cellnumret 1,6·10¹⁰ — större än en int. Lådorna är
    // höga nog att inte försvinna i flyttalets upplösning där (6·10⁴ px).
    check(flaggor(placera({låda(300, 1e12f, 40, 1e11f, 1),
                           låda(330, 1e12f, 40, 1e11f, 0)})) == "10",
          "P7: 10¹² px bort spräcker inget heltal — och krocken syns");

    // BORTOM CELLTAKET: en skärm på en miljon px har fler celler än taket;
    // de yttersta delar på resten.
    const gfx::Rectf jätte{0.0f, 0.0f, 1.0e6f, 1080.0f};
    check(flaggor(placera({låda(900000, 100, 40, 10, 1),
                           låda(900030, 100, 40, 10, 0)},
                          jätte)) == "10",
          "P7: krock bortom celltaket");
}

// ---------------------------------------------------------------------- P8

// REFERENSEN: ordningen och lådan skrivna för sig, och ALLA par prövade —
// inget rutnät. Heltal och 0°/90° gör båda vägarna exakta.
std::vector<std::uint8_t> referens(const std::vector<LabelBox>& v)
{
    std::vector<std::size_t> ordning(v.size());
    std::iota(ordning.begin(), ordning.end(), std::size_t{0});
    // STABIL sortering på (prioritet, lager) fallande: lika nycklar behåller
    // indatans ordning — det är index-regeln.
    std::stable_sort(ordning.begin(), ordning.end(),
                     [&v](std::size_t a, std::size_t b) {
                         if (v[a].priority != v[b].priority)
                             return v[a].priority > v[b].priority;
                         return v[a].layer > v[b].layer;
                     });
    struct L {
        double x0, y0, x1, y1;
    };
    std::vector<L>            insatta;
    std::vector<std::uint8_t> ut(v.size(), 0);
    for (std::size_t i : ordning) {
        const LabelBox& l = v[i];
        if (!l.collide) {
            ut[i] = 1;
            continue;
        }
        const bool   lodrät = std::fabs(l.dirY) == 1.0f;   // 90° eller 270°
        const double hw     = (lodrät ? l.halfH : l.halfW) + l.padding;
        const double hh     = (lodrät ? l.halfW : l.halfH) + l.padding;
        const L      b{l.cx - hw, l.cy - hh, l.cx + hw, l.cy + hh};
        bool         krock = false;
        for (const L& o : insatta)
            if (b.x0 < o.x1 && o.x0 < b.x1 && b.y0 < o.y1 && o.y0 < b.y1) {
                krock = true;
                break;
            }
        if (!krock) {
            ut[i] = 1;
            insatta.push_back(b);
        }
    }
    return ut;
}

// 2 000 slumpade lådor med fast frö — HELTAL i mitt, mått och luft, 0°/90°,
// prioritet 0–4 och lager 0–2, 5 % utan kollision. Mitten sprids ut över
// skärmkanten så lådor utanför delar kantcellerna, och bredden går upp till
// 600 px (nio celler) så stora lådor korsar många.
std::vector<LabelBox> slumpHeltal(std::uint32_t frö, int n)
{
    std::mt19937                       rng(frö);
    std::uniform_int_distribution<int> cx(-300, 2220), cy(-200, 1280);
    std::uniform_int_distribution<int> hw(2, 300), hh(2, 24);
    std::uniform_int_distribution<int> prio(0, 4), lager(0, 2), luft(0, 3);
    std::uniform_int_distribution<int> vinkel(0, 3), procent(0, 99);
    std::vector<LabelBox>              v;
    v.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        LabelBox l;
        l.cx       = static_cast<float>(cx(rng));
        l.cy       = static_cast<float>(cy(rng));
        l.halfW    = static_cast<float>(hw(rng));
        l.halfH    = static_cast<float>(hh(rng));
        // 0/90/180/270 — riktningen EXAKT, så båda vägarna räknar exakt.
        const float riktning[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        const int   kv             = vinkel(rng);
        l.dirX                     = riktning[kv][0];
        l.dirY                     = riktning[kv][1];
        l.padding  = static_cast<float>(luft(rng));
        l.priority = prio(rng);
        l.layer    = lager(rng);
        l.collide  = procent(rng) >= 5;
        v.push_back(l);
    }
    return v;
}

void fallReferens()
{
    for (std::uint32_t frö : {1u, 7u, 2026u}) {
        const auto v   = slumpHeltal(frö, 2000);
        const auto ref = referens(v);
        const auto ut  = placera(v);
        std::size_t olika = 0, synliga = 0;
        for (std::size_t i = 0; i < v.size(); ++i) {
            olika += ut[i] != ref[i];
            synliga += ut[i];
        }
        char b[160];
        std::snprintf(b, sizeof b, "frö %u: %zu olika av 2000 (%zu synliga)",
                      frö, olika, synliga);
        check(olika == 0, std::string("P8: rutnätet = referensen, ") + b, b);
        check(synliga > 100 && synliga < 1900,
              std::string("P8: (fixturen) både krockar och fria, ") + b, b);
    }
}

// ---------------------------------------------------------------------- P9

void fallDeterminism()
{
    const auto v   = slumpHeltal(99, 2000);
    const auto ref = placera(v);
    check(placera(v) == ref, "P9: samma indata två gånger ⇒ samma svar");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    struct Skärm {
        const char* namn;
        gfx::Rectf  r;
    };
    const Skärm skärmar[] = {
        {"1 × 1", {0.0f, 0.0f, 1.0f, 1.0f}},
        {"enorm", {-1.0e6f, -1.0e6f, 4.0e6f, 4.0e6f}},
        {"förskjuten", {777.0f, -333.0f, 640.0f, 480.0f}},
        {"tom", {0.0f, 0.0f, 0.0f, 0.0f}},
        {"icke-ändlig", {nan, 0.0f, nan, 100.0f}},
    };
    for (const Skärm& s : skärmar)
        check(placera(v, s.r) == ref,
              std::string("P9: svaret beror inte av skärmen (") + s.namn + ")");
}

// --------------------------------------------------------------------- P10

void fallTid()
{
    // 10 000 etiketter som en tät karta: mitten över hela skärmen, 30–200 px
    // breda och 12–24 px höga, vridna i alla vinklar, prioritet 0–9.
    std::mt19937                          rng(10000);
    std::uniform_real_distribution<float> cx(0.0f, 1920.0f), cy(0.0f, 1080.0f);
    std::uniform_real_distribution<float> hw(15.0f, 100.0f), hh(6.0f, 12.0f);
    std::uniform_real_distribution<float> vinkel(-180.0f, 180.0f);
    std::uniform_int_distribution<int>    prio(0, 9), lager(0, 3);
    std::vector<LabelBox>                 v(10000);
    for (LabelBox& l : v) {
        l.cx       = cx(rng);
        l.cy       = cy(rng);
        l.halfW    = hw(rng);
        l.halfH    = hh(rng);
        vrid(l, vinkel(rng));
        l.padding  = 2.0f;
        l.priority = prio(rng);
        l.layer    = lager(rng);
    }
    using Klocka = std::chrono::steady_clock;
    std::vector<double>       ms;
    std::vector<std::uint8_t> ut;
    for (int körning = 0; körning < 15; ++körning) {
        const auto t0 = Klocka::now();
        ut            = placera(v);
        const auto t1 = Klocka::now();
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::sort(ms.begin(), ms.end());
    const std::size_t synliga =
        static_cast<std::size_t>(std::count(ut.begin(), ut.end(), 1));
#ifdef NDEBUG
    const char* bygge = "Release";
#else
    const char* bygge = "Debug";
#endif
    std::printf("  tid  10 000 etiketter: median %.3f ms, bästa %.3f ms "
                "(%s; %zu synliga) — målet < 2 ms i Release, mäts inte som "
                "grind\n",
                ms[ms.size() / 2], ms.front(), bygge, synliga);
    check(ut.size() == v.size() && synliga > 0,
          "P10: svaret har en flagga per etikett och några syns");
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("placement_gate\n");
    fallPrioritet();
    fallIngenKrock();
    fallOrdning();
    fallVridning();
    fallLuft();
    fallUtanKollision();
    fallGrannceller();
    fallReferens();
    fallDeterminism();
    fallTid();

    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    return g_fails ? 1 : 0;
}
