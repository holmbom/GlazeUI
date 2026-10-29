// geom_gate — GEOMETRIBYGGARNA, headless: LINJERNA, YTORNA och TEXTEN —
// och i del
// 3D: GENERATORERNA, POLYGONEN I ETT PLAN, MESHENS SEGMENT OCH DATA och
// PUNKTMOLNETS CHUNKAR (beskrivet vid sin del längre ner).
//
// VAD GRINDEN PÅSTÅR OM LINJERNA (mot handskrivna värden — inga förväntningar
// läses ur geom2d.cpp):
//   1. En polylinje blir rätt BITAR: per segment en quad (början −/+,
//      slutet −/+), per inre punkt en rund skarv, runda ändar bara när de
//      begärts — med rätt antal vertex och index och indexen i quadens
//      ordning (0–1–2, 2–1–3).
//   2. Varje vertex bär sin punkt, föregående och nästa punkt (en punkt
//      utan granne bär sig själv), sitt slag och sin sida, och bredden och
//      färgen ur stilen.
//   3. `dist` är linjens längd FRAM TILL punkten (0, 5, 11 för en 3-4-5-
//      triangel med en sträcka på 6 efter).
//   4. Upprepade och icke-ändliga punkter hoppas över; färre än två punkter
//      ger ingenting.
//   5. CHUNKGRÄNSEN: en polylinje över 65 536 vertex delas i chunkar som
//      var och en har högst så många vertex och index inom sig, ett EGET
//      origo (chunkens första punkt), punkter som återskapas ur origo +
//      float, och tillsammans exakt varje segment en gång med `dist`
//      fortsatt över gränsen.
//   6. En andra polylinje läggs i den sista chunken så länge den ryms, med
//      indexen förskjutna.
//   7. SKARVARNAS SLAG: spetsig och avfasad skarv ger en bit av sitt slag
//      (Miter/Bevel) i knäet med punkten och grannarna — rund som standard.
//   8. ÄNDARNA: varje segment bär ändformen (rak 0, rund, fyrkantig) i alla
//      fyra hörnen, det första segmentet "börjar linjen", det sista "slutar
//      den", ett ensamt både och; skarvarna bär inga ändar, och bara runda
//      ändar lägger bitar till.
//   9. SLUTEN LINJE: en kvadrat är fyra segment och fyra skarvar — skarven i
//      startpunkten först (föregående = sista punkten, dist 0), sista
//      segmentet tillbaka till början (dist 30 → 40), inget segment som
//      slutar linjen; en upprepad slutpunkt är samma ring, två punkter är
//      en öppen linje.
//  10. STRECKMÖNSTRET (`makeDash`): de kumulativa sluten, SVG:s udda
//      upprepning, heldraget för tomt/negativt/summan 0/icke-ändligt, och
//      taket på åtta längder (tio och fem-blir-tio avvisas).
//
// VAD GRINDEN PÅSTÅR OM YTORNA (arean är grindens EGEN skosnöresformel över
// de rensade ringarna, aldrig trianguleringens):
//   Y1  AREAN: kvadraten 10 × 10 = 100, L-formen = 5, kvadraten med ett hål
//       4 × 4 = 84 — summan av trianglarnas areor i världen. Kantremsans
//       trianglar har ingen area i världen (inne och ute är samma punkt; det
//       är vertexshadern som ger dem en px), så summan över ALLA trianglar
//       är ytans.
//   Y2  200 SLUMPPOLYGONER med fast frö: stjärnformade (alltid enkla), 3–300
//       punkter — både den enkla och den z-ordnade vägen (> 80 noder) — och
//       hälften med hål; några på SWEREF-koordinater. Arean stämmer och
//       svaret är true för varje.
//   Y3  DEGENERERAD INDATA: dubbletter, kollineära punkter, upprepad
//       slutpunkt, icke-ändliga punkter, en ring utan area och för få
//       punkter kraschar inte och ger rätt area (eller ingenting).
//   Y4  OGILTIG YTA: en självkorsning (fluga) och ett hål utanför ger false —
//       och ändå trianglar (ytan uteblir inte).
//   Y5  KANTREMSANS RIKTNING: ringarna ges i FEL orientering (ytterringen
//       medurs, hålet moturs) — byggaren vänder dem, så ytan ligger till
//       VÄNSTER om varje kant prev → punkt → next (programmets "utåt" är
//       höger), och varje kantquad är (början inne, början ute, slutet inne,
//       slutet ute) med kantens egna punkter.
//   Y6  CHUNKGRÄNSEN: en cirkel med 20 000 punkter (80 000 vertex) delas i
//       chunkar ≤ 65 536 med samma origo, alla index inom sin chunk och
//       arean bevarad; två små ytor delar en chunk med förskjutna index.
//
// VAD GRINDEN PÅSTÅR OM TEXTEN (referensen är det RIKTIGA euklidiska avståndet
// och en egen font med kända steg — aldrig sdf.cpp:s formel):
//   S1  AVSTÅNDSFÄLTET: en kvadrat 10 × 10 px ger ett fält med glyfen plus
//       radien åt alla håll, och värdena avkodade ur formatet (0,5 + d / 2r)
//       är avståndet från texelns mitt till kanten: 4,5 inne i mitten, ±0,5
//       på var sida om kanten, 2,5 och 7,5 utanför, diagonalt vid hörnet,
//       0 bortom radien. Rasteriserad dubbelt så fint (skala 2) ger samma
//       fält inom 0,2 texel; en tom glyf ger inget fält.
//   S2  LAYOUTEN mot `StegFont` (0,6 em per tecken): lådans bredd =
//       `Font::measure` i em och höjden en rad; en quad per glyf utom
//       mellanslaget; varje UNIK glyf laddas upp EN gång och samma text igen
//       laddar inte upp något; ankaret Center med två rader; glorians quadar
//       före fyllningarnas; plattan först, med luften runt lådan. Atlasen
//       laddar fonten i rasteriseringsstorleken (32 × 2 px).
//   S3  SYNTESEN: begärd fet kursiv med en vanlig font — atlasen säger att
//       stilens font är vanlig (`style(BoldItalic)`), varje steg blir 1/24
//       em bredare, varje vertex bär flaggan, och hörnen skjuvas ≈ 12° kring
//       baslinjen; en riktig fet font syntetiseras inte.
//   S4  MARKÖRERNA: en quad per ändlig punkt utan atlassida, formens halva
//       mått ½ em, och en kontur som växer quaden UTANFÖR fyllningen. ANKARET
//       flyttar rutan men inte formens läge (Bottom: rutan ovanför punkten,
//       lådan [−½, ½] × [−1, 0] em; TopLeft [0, 1]²), och en BILDMARKÖR har
//       u, v 0–1, flaggan kImage utan formbitar och ingen kontur.
//   S5  TEXTDELARNA — `AtlasRenderer` ger en StegFont
//       per stil, den feta 0,75 em per tecken och raden 1,5 em: S5a bredden
//       = delarnas egna steg, färgen per vertex ur delen, EN chunk på sida 0
//       och alla glorior före alla fyllningar över delgränsen; S5b samma
//       kodpunkt i vanlig och fet = två uppladdningar, därefter ingen; S5c en
//       rad bryts av den feta delens bredd (med basstilens hade den rymts);
//       S5d en enda del bryts EXAKT som `gfx::wrapText` på en korpus (28
//       fall: rader, glyfer per rad, bredaste raden); S5e bara den feta
//       delens vertex bär kSyntheticBold när fet font är vanlig; S5f
//       radhöjden och baslinjen = de största bland stilarna; S5g en etikett
//       vars glyfer hamnar på två ATLASSIDOR (sida 0 fylld till en plats) —
//       alla glorior ritas ändå före alla fyllningar, också lyft i rymden.
//   S6  MÄTNINGEN UTAN BYGGE (`gfx::measureText`) — mätt på en
//       NY atlas, så stegen kommer ur `Font::measure`: S6a lådan = byggarens
//       på en egen atlas för S5a, S5c, S5f och en etikett med ankaret
//       Center, luft och förskjutning (1e-5 em); S6b mätningen skapar ingen
//       sida och laddar inte upp; S6c radantalet, den bredaste raden och
//       höjden = `gfx::wrapText` på S5d:s 28 fall; S6d "" är en rad och
//       "a\n\nb" tre; S6e radhöjden och baslinjen de största bland
//       stilarna; S6f syntetisk fet räknas in som i byggaren; storleken 0
//       och NaN ger noll rader.
//   S7  TAKET (`maxLines`/`maxHeight`) mot StegFonts kända
//       mått (0,6 em per tecken, raden 1,25 em; fet 0,75/1,5): S7a två av fem
//       rader, `truncated`, sista quaden är U+2026 och mätningens låda =
//       byggarens; S7b 2,5 radhöjder ger två rader och en halv ger en; S7c
//       ellipsen i den sista delens färg och glyfnyckel (fet röd); S7d ett
//       blanksteg före ellipsen tas bort; S7e en brytbredd smalare än
//       ellipsen ger en rad som bara är "…"; S7f utan brytbredd "a\nb\nc"
//       med två rader ger "b…"; S7g en text som ryms är orörd (samma vertex);
//       S7h mätningen av en kortad text laddar inte upp något. MÄTER INTE en
//       riktig font eller att regeln är bit för bit `Draw::elideLines` (den
//       är speglad, inte delad).
//   S8  BEHÖVER MÄTNINGEN VÄRLDENS TÄTHET? (build.h
//       `measureNeedsWorld`) mot en HANDSKRIVEN lista över de mått
//       `measureLabel` räknar om: utenheten World sant och px/mm falskt för
//       en text i px; storleken, brytbredden, takets höjd, golvet och taket
//       var för sig sant i World och falskt i px, i mm och med värdet 0;
//       glorians bredd falsk med och utan gloria (mätningen läser den inte).
//       Sabotage som SKA fälla den: `minSize`
//       utelämnad ur listan (S8 minSize), och värdet ≤ 0 räknat (värdet 0).
//       MÄTER INTE att `Canvas::measureText(c, …)` frågar den — det gör
//       canvas_gate MR5.
//   Q   BILDEN PÅ FYRA FRIA HÖRN mot grindens EGEN
//       homografi (DLT, Gauss–Jordan): Q1 rektangel och parallellogram ger
//       fyra ettor, och för en trapets och en allmän fyrhörning är vikterna
//       homografins nämnare i hörnen och Σλqu/Σλq homografins bildpunkt på
//       50 slumppunkter i byggarens båda trianglar (1e-5); Q2 fluga,
//       inbuktning, tre i linje, ingen yta och NaN ⇒ false med vikterna
//       orörda; Q3 spegelvänd ringordning ⇒ true med samma vikter i de
//       speglade platserna; Q4 byggaren lägger hörnen i z-ordning {p0, p1,
//       p3, p2} med varje hörns egen vikt, och en fluga har inga delar.
//
// INTE MÄTT här: hur programmen extruderar bitarna och remsorna och ritar
// fälten (i pixlar — scene_pixel_gate), en riktig fonts glyfer (D1 och
// D5 där), atlasens sidbyte som sådant (S5g mäter ORDNINGEN över sidorna,
// inte packningen), fältets kvalitet vid små storlekar (ögat, inte en
// siffra) och Image-programmets division med q (scene_pixel_gate B8–B11).
//
// SABOTAGEN den fäller: `dist` nollställd (fall 1–3 och 5), hålen
// ignorerade (Y1, Y2) och ringarna inte vända (Y5) — liksom radien
// halverad i fältets kodning (S1), fältets bredd i stället för steget i
// layouten (S2) och den kursiva skjuvningen borta (S3); i konturen och
// bildmarkören: `closed` ignorerad (9) och det udda mönstret
// inte upprepat (10). Markörens ankare ignorerat fälls av
// scene_pixel_gate (H9/H10) — S4 mäter samma låda här, men det
// sabotaget fälls inte av den här grinden.
// För S5 och Q SKA sabotagen nedan fälla dem (skrivna):
// glyfnyckeln utan stil (S5b),
// brytningen med basstilens bredder (S5c), delens färg ignorerad och glorian
// per del (S5a), basstilens radhöjd (S5f), en `chunkFor` utan undre gräns
// (S5g — fyllningen på sida 0 hamnar i glorians äldre chunk); q = 1 alltid
// och q0 ↔ q2 (Q1), konvexitetstestet bort (Q2) och ringen tillbaka i fel
// z-ordning (Q4).
// För S6 (skrivet) SKA sabotagen
// fälla den: `atlas.glyph()` i mätningen (S6b), `layLines` som letar
// radbrytningen på en tom glyfpekare i stället för kodpunkten (S6a, S6c —
// varje mätt glyf blir en egen rad), `boldStep` bort ur mätningen (S6f) och
// radhöjden ur `st.fontStyle` (S6e). S6 mäter INTE en riktig fonts steg mot
// dess raster — det gör canvas_gate M2 genom den riktiga backenden.
//
// DEL 3D — referensen är grindens EGEN vektorräkning,
// volymer och areor ur analytiska formler och en egen svetsning av lägena;
// inget läses ur geom3d.cpp eller mesh.cpp:
//   L1  LÅDAN: 24 lägen, 12 trianglar, volymen 3 · 4 · 5 = 60 (tecknad — alltså
//       vänd utåt) och ytan 94, sluten, normalerna axelparallella och utåt,
//       också på SWEREF och två lådor i en mesh (indexen förskjutna).
//   S1  SFÄREN 24 × 16: 362 lägen och 720 trianglar, varje läge på radien med
//       radiell normal, sluten, 0,95 V < volymen < V, och de klampade minsta
//       talen 3 × 2 ger en dubbelpyramid med volymen √3/2.
//   C1  CYLINDERN med sned axel: 64 lägen, 60 trianglar, volymen = polygonens
//       area × höjden 13, sluten, manteln radiellt utåt på radien och locken
//       längs axeln; en lodrät har läge 0 österut.
//   R1–R6 RÖRET: rakt (volymen = åttahörningens area × längden), en
//       KEDJEKURVA med 51 punkter (volymen = arean × VÄGENS längd — geringen
//       skär varje segment så att prismat behåller sin volym — och varje
//       knäs ring i halveringsplanet, på radien från BÅDA segmentens linjer),
//       rät vinkel, ett knä skarpare än spetsgränsen (två slutna bitar),
//       rensningen och det som ger ingenting.
//   P1–P5 POLYGONEN I ETT PLAN: kvadraten med hål roterad in i ett lutat plan
//       på SWEREF (arean 84, planets normal ur ytterringens vindning, varje
//       triangel moturs kring den, lägena = indatans punkter), omvänd
//       ytterring och omvänt hål, L-formen lodrät, 50 slumpstjärnor med hål
//       i slumpade plan mot grindens skosnöre, det degenererade, en
//       självkorsning (false men trianglar, och de moturs kring planets
//       normal — också reservens) och brus längs normalen (lägena står kvar).
//   M1–M4 SEGMENTEN: utan index (på triangelgränser, i följd, LOD-intervallen
//       ommappade mot grindens eget snitt), med index (lokala lägen växande
//       och använda, varje originaltriangel återskapad ur segmentets lokala
//       index — tabellen tillbaka —, varje segment FULLT: nästa triangel hade
//       inte rymts), indexgränsen, och den riktiga gränsen 64 MiB på en mesh
//       med 5 592 408 lägen.
//   M5  MESHENS DATA: avslagen (index utanför, ström med fel längd, NaN, inte
//       hela trianglar, nivåer i fel ordning), `fromWorld`s origo i lådans
//       mitt, nivåns klampade antal, ändringsloggen (versioner, en hel ström
//       gör äldre ändringar av SAMMA ström överflödiga, taket 32) och
//       ändringarna som vägras.
//   K1  PUNKTMOLNET: ett litet är en chunk i indatans ordning; 200 000
//       blandade punkter på SWEREF blir fyra chunkar om 50 000, rumsligt
//       delade längs den längsta axeln, varje punkt (läge, färg, skalär)
//       återfunnen genom ordningen inom 1e-4 m.
//
// INTE MÄTT i del 3D: hur programmen ritar trianglarna (i
// pixlar), uppladdningen av segmenten och ändringsloggen,
// och plockningen (pick_gate).
//
// SABOTAGEN del 3D, prövade i en privat kopia, alla
// fällda: lådans vindning omvänd (L1), cylinderns lock borta (C1), röret utan
// gering — räta ringar i knäna (R2, R3), segmentgränsen ignorerad (M1, M2,
// M4), indexen inte ommappade lokalt (M2, M3), nivåerna inte ommappade (M1,
// M2), punktmolnet delat i indatans ordning i stället för rumsligt (K1) och
// trianglarna inte vända efter planet (P4 — först efter skärpningen: P1–P2
// mäter bara enkla ytor, och dem lämnar öronklippningen redan moturs; bara
// reservens trianglar i en självkorsning kommer ut medurs).

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <algorithm>
#include <limits>
#include <map>
#include <numeric>

#include <gfx/geom2d.h>
#include <gfx/geom3d.h>
#include <gfx/null_renderer.h>
#include <gfx/sdf.h>

#include "canvas/build.h"   // imageCornersOf/buildImageQuad (del Q)
#include "canvas/mesh_data.h"
#include "glaze/canvas/mesh.h"

namespace {

int  g_fel     = 0;
int  g_fall    = 0;
bool g_verbose = false;

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

constexpr std::uint8_t kStart = 0;   // LineKind::SegmentStart
constexpr std::uint8_t kSlut  = 1;   // LineKind::SegmentEnd
constexpr std::uint8_t kRund  = 2;   // LineKind::Round

const gfx::Color kFärg{10, 20, 30, 200};

gfx::PolylineStyle stil(gfx::LineCap cap = gfx::LineCap::Butt)
{
    gfx::PolylineStyle s;
    s.width = 4.0f;
    s.color = kFärg;
    s.cap   = cap;
    return s;
}

// Vertexens fält mot de förväntade värdena. Punkterna jämförs exakt — små heltal i en
// chunk med origo i en heltalspunkt är exakta i float.
bool vertexLika(const gfx::LineVertex& v, float x, float y, float px, float py,
                float nx, float ny, float dist, std::uint8_t kind, int side)
{
    return v.x == x && v.y == y && v.prevX == px && v.prevY == py &&
           v.nextX == nx && v.nextY == ny && v.dist == dist &&
           v.kind == kind && v.side == side && v.width == 4.0f &&
           v.color.r == kFärg.r && v.color.g == kFärg.g &&
           v.color.b == kFärg.b && v.color.a == kFärg.a;
}

bool quadIndex(const gfx::LineChunk& c, std::size_t bit)
{
    const std::uint32_t b = static_cast<std::uint32_t>(bit * 4);
    const std::uint32_t f[6] = {b, b + 1, b + 2, b + 2, b + 1, b + 3};
    for (int i = 0; i < 6; ++i)
        if (c.indices[bit * 6 + i] != f[i])
            return false;
    return true;
}

// ------------------------------------------------------------------ 1–3
void fallBitarna()
{
    // (0,0) → (10,0) → (10,10): två segment och en skarv, raka ändar.
    const std::vector<gfx::Point2d> p = {{0, 0}, {10, 0}, {10, 10}};
    std::vector<gfx::LineChunk> ch;
    gfx::buildPolyline(p, stil(), ch);
    check(ch.size() == 1, "en kort linje ger EN chunk");
    if (ch.size() != 1)
        return;
    const gfx::LineChunk& c = ch[0];
    check(c.originX == 0.0 && c.originY == 0.0,
          "chunkens origo är linjens första punkt");
    check(c.vertices.size() == 12 && c.indices.size() == 18,
          "två segment + en skarv = 3 bitar = 12 vertex, 18 index",
          std::to_string(c.vertices.size()) + " vertex, " +
              std::to_string(c.indices.size()) + " index");
    if (c.vertices.size() != 12 || c.indices.size() != 18)
        return;
    const auto& v = c.vertices;
    // Segment 0: början (0,0) utan föregående punkt (bär sig själv), mot
    // nästa (10,0); slutet (10,0) med föregående (0,0) och nästa (10,10).
    check(vertexLika(v[0], 0, 0, 0, 0, 10, 0, 0, kStart, -1) &&
              vertexLika(v[1], 0, 0, 0, 0, 10, 0, 0, kStart, +1),
          "segment 0: början −/+ med punkt, grannar, dist 0");
    check(vertexLika(v[2], 10, 0, 0, 0, 10, 10, 10, kSlut, -1) &&
              vertexLika(v[3], 10, 0, 0, 0, 10, 10, 10, kSlut, +1),
          "segment 0: slutet −/+ med dist 10");
    // Skarven i (10,0): fyra hörn 0–3.
    bool skarv = true;
    for (int h = 0; h < 4; ++h)
        skarv = skarv && vertexLika(v[4 + h], 10, 0, 0, 0, 10, 10, 10, kRund, h);
    check(skarv, "skarven: rund bit i (10,0), hörn 0–3, dist 10");
    check(vertexLika(v[8], 10, 0, 0, 0, 10, 10, 10, kStart, -1) &&
              vertexLika(v[11], 10, 10, 10, 0, 10, 10, 20, kSlut, +1),
          "segment 1: början i (10,0), slutet (10,10) utan nästa, dist 20");
    check(quadIndex(c, 0) && quadIndex(c, 1) && quadIndex(c, 2),
          "indexen: varje bit 0–1–2, 2–1–3");

    // Runda ändar: en rund bit först och sist.
    std::vector<gfx::LineChunk> r;
    gfx::buildPolyline(p, stil(gfx::LineCap::Round), r);
    check(r.size() == 1 && r[0].vertices.size() == 20 &&
              r[0].indices.size() == 30,
          "runda ändar: 3 bitar + 2 ändar = 20 vertex");
    if (r.size() == 1 && r[0].vertices.size() == 20) {
        const auto& w = r[0].vertices;
        check(vertexLika(w[0], 0, 0, 0, 0, 10, 0, 0, kRund, 0) &&
                  vertexLika(w[19], 10, 10, 10, 0, 10, 10, 20, kRund, 3),
              "runda ändar: början (dist 0) och slutet (dist 20)");
    }

    // dist längs en 3-4-5-triangel och en sträcka på 6.
    std::vector<gfx::LineChunk> d;
    gfx::buildPolyline(std::vector<gfx::Point2d>{{0, 0}, {3, 4}, {3, 10}},
                       stil(), d);
    check(d.size() == 1 && d[0].vertices.size() == 12 &&
              d[0].vertices[0].dist == 0.0f && d[0].vertices[2].dist == 5.0f &&
              d[0].vertices[4].dist == 5.0f && d[0].vertices[11].dist == 11.0f,
          "dist: 0, 5, 11 — längden fram till punkten");
}

// ------------------------------------------------------------------ 4
void fallRensningen()
{
    std::vector<gfx::LineChunk> c;
    gfx::buildPolyline(std::vector<gfx::Point2d>{{0, 0}, {0, 0}, {5, 0},
                                                 {5, 0}, {5, 5}},
                       stil(), c);
    check(c.size() == 1 && c[0].vertices.size() == 12,
          "upprepade punkter hoppas över (två segment, en skarv)");

    std::vector<gfx::LineChunk> n;
    gfx::buildPolyline(std::vector<gfx::Point2d>{{0, 0},
                                                 {NAN, 1},
                                                 {INFINITY, 2},
                                                 {7, 0}},
                       stil(), n);
    check(n.size() == 1 && n[0].vertices.size() == 4 &&
              n[0].vertices[2].x == 7.0f,
          "icke-ändliga punkter hoppas över");

    std::vector<gfx::LineChunk> tom;
    gfx::buildPolyline(std::vector<gfx::Point2d>{{3, 3}}, stil(), tom);
    gfx::buildPolyline(std::vector<gfx::Point2d>{{3, 3}, {3, 3}}, stil(), tom);
    gfx::buildPolyline(std::vector<gfx::Point2d>{}, stil(), tom);
    check(tom.empty(), "färre än två punkter ger ingenting — ingen chunk");
}

// ------------------------------------------------------------------ 5
void fallChunkgränsen()
{
    // 9000 punkter i SWEREF, en meter isär i en svag båge: 8 vertex per
    // punkt (segment + skarv) ⇒ ~72 000 vertex ⇒ två chunkar.
    const int N = 9000;
    std::vector<gfx::Point2d> p(N);
    std::vector<double>       dist(N, 0.0);
    for (int i = 0; i < N; ++i) {
        p[i] = {674000.25 + i, 6580000.5 + 0.0001 * i * i * 0.01};
        if (i > 0)
            dist[i] = dist[i - 1] + std::hypot(p[i].x - p[i - 1].x,
                                               p[i].y - p[i - 1].y);
    }
    std::vector<gfx::LineChunk> ch;
    gfx::buildPolyline(p, stil(), ch);
    check(ch.size() == 2, "9000 punkter ⇒ två chunkar",
          std::to_string(ch.size()));

    bool                inom = true, index = true, återskapad = true;
    std::vector<double> starter;   // segmentens början, i dist
    std::size_t         segment = 0;
    for (const gfx::LineChunk& c : ch) {
        inom = inom && c.vertices.size() <= gfx::kMaxChunkVertices &&
               c.vertices.size() % 4 == 0 &&
               c.indices.size() == c.vertices.size() / 4 * 6;
        for (std::uint32_t i : c.indices)
            index = index && i < c.vertices.size();
        for (const gfx::LineVertex& v : c.vertices) {
            // Punkten ur origo + float ska ligga på en av linjens punkter:
            // x-steget är exakt en meter, så närmaste index är givet.
            const double wx = c.originX + v.x, wy = c.originY + v.y;
            const long   k  = std::lround(wx - 674000.25);
            återskapad = återskapad && k >= 0 && k < N &&
                         std::abs(wx - p[k].x) < 1e-3 &&
                         std::abs(wy - p[k].y) < 1e-3;
            if (v.kind == kStart && v.side == -1) {
                starter.push_back(v.dist);
                ++segment;
            }
        }
    }
    check(inom, "varje chunk ≤ 65 536 vertex, hela bitar");
    check(index, "varje index pekar inom sin egen chunk");
    check(återskapad,
          "varje vertex återskapas ur chunkens origo + float (< 1 mm)");
    if (ch.size() == 2) {
        check(ch[1].originX != ch[0].originX,
              "den andra chunken har ett EGET origo");
        const gfx::LineVertex& f = ch[1].vertices.front();
        check(f.x == 0.0f && f.y == 0.0f,
              "origot är chunkens första punkt (första vertexen är 0, 0)");
    }
    check(segment == static_cast<std::size_t>(N - 1),
          "exakt varje segment EN gång över chunkgränsen",
          std::to_string(segment) + " av " + std::to_string(N - 1));
    bool distOk = segment == static_cast<std::size_t>(N - 1);
    for (std::size_t i = 0; distOk && i < starter.size(); ++i)
        distOk = std::abs(starter[i] - dist[i]) < 1e-2;
    check(distOk, "dist fortsätter över chunkgränsen (segmentens början)");
}

// ------------------------------------------------------------------ 6
void fallTillägg()
{
    std::vector<gfx::LineChunk> ch;
    gfx::buildPolyline(std::vector<gfx::Point2d>{{100, 100}, {110, 100}},
                       stil(), ch);
    gfx::buildPolyline(std::vector<gfx::Point2d>{{120, 105}, {130, 105}},
                       stil(), ch);
    check(ch.size() == 1 && ch[0].vertices.size() == 8 &&
              ch[0].indices.size() == 12,
          "en andra linje läggs i den sista chunken när den ryms");
    if (ch.size() == 1 && ch[0].vertices.size() == 8) {
        check(ch[0].originX == 100.0 && ch[0].vertices[4].x == 20.0f &&
                  ch[0].vertices[4].y == 5.0f,
              "den andra linjen är relativ den FÖRSTAS origo");
        check(quadIndex(ch[0], 1),
              "den andra linjens index är förskjutna till dess vertex");
        check(ch[0].vertices[4].dist == 0.0f,
              "den andra linjens dist börjar om på 0");
    }
}

// ------------------------------------------------------------------ 7
// SKARVARNAS SLAG: samma linje med spetsig och avfasad skarv — en bit i
// knäet av rätt slag, med punkten och dess grannar (formen räknas i
// vertexshadern och mäts i scene_pixel_gate del H).
constexpr std::uint8_t kSpets = 3;   // LineKind::Miter
constexpr std::uint8_t kFas   = 4;   // LineKind::Bevel

void fallSkarvslagen()
{
    const std::vector<gfx::Point2d> p = {{0, 0}, {10, 0}, {10, 10}};
    for (const auto [join, slag, namn] :
         {std::tuple{gfx::LineJoin::Miter, kSpets, "spetsig"},
          std::tuple{gfx::LineJoin::Bevel, kFas, "avfasad"},
          std::tuple{gfx::LineJoin::Round, kRund, "rund"}}) {
        gfx::PolylineStyle s = stil();
        s.join = join;
        std::vector<gfx::LineChunk> ch;
        gfx::buildPolyline(p, s, ch);
        bool ok = ch.size() == 1 && ch[0].vertices.size() == 12;
        for (int h = 0; ok && h < 4; ++h)
            ok = vertexLika(ch[0].vertices[4 + h], 10, 0, 0, 0, 10, 10, 10,
                            slag, h);
        check(ok, std::string("skarven ") + namn +
                      ": en bit av sitt slag i knäet, hörn 0–3, grannarna "
                      "och dist 10");
    }
}

// ------------------------------------------------------------------ 8
// ÄNDARNA: segmentens `ends` — ändformen i alla segment, och vilket segment
// som börjar respektive slutar linjen. Raka och fyrkantiga ändar lägger inga
// bitar till (segmentet förlängs i vertexshadern), runda en i varje ände.
void fallÄndarna()
{
    using namespace gfx::lineflag;
    const std::vector<gfx::Point2d> p = {{0, 0}, {10, 0}, {10, 10}};
    for (const auto [cap, bit, extra, namn] :
         {std::tuple{gfx::LineCap::Butt, std::uint8_t{0}, 0, "rak"},
          std::tuple{gfx::LineCap::Square, kCapSquare, 0, "fyrkantig"},
          std::tuple{gfx::LineCap::Round, kCapRound, 8, "rund"}}) {
        std::vector<gfx::LineChunk> ch;
        gfx::buildPolyline(p, stil(cap), ch);
        const std::size_t vn = 12 + static_cast<std::size_t>(extra);
        check(ch.size() == 1 && ch[0].vertices.size() == vn,
              std::string("änden ") + namn + ": " + std::to_string(vn) +
                  " vertex");
        if (ch.size() != 1 || ch[0].vertices.size() != vn)
            continue;
        // Segmenten: det första efter en ev. rund ände.
        const std::size_t s0 = cap == gfx::LineCap::Round ? 4 : 0;
        const std::size_t s1 = s0 + 8;
        bool lika = true;
        for (int k = 0; k < 4; ++k) {
            lika = lika && ch[0].vertices[s0 + k].ends == (bit | kStartsLine) &&
                   ch[0].vertices[s1 + k].ends == (bit | kEndsLine);
        }
        for (int k = 0; k < 4; ++k)   // skarven bär inga ändar
            lika = lika && ch[0].vertices[s0 + 4 + k].ends == 0;
        check(lika, std::string("änden ") + namn +
                        ": första segmentet börjar linjen, det sista slutar "
                        "den, båda bär ändformen i alla fyra hörnen");
    }
    std::vector<gfx::LineChunk> ett;
    gfx::buildPolyline(std::vector<gfx::Point2d>{{0, 0}, {5, 0}},
                       stil(gfx::LineCap::Square), ett);
    check(ett.size() == 1 && ett[0].vertices.size() == 4 &&
              ett[0].vertices[2].ends == (kCapSquare | kStartsLine | kEndsLine),
          "ett ensamt segment både börjar och slutar linjen");
}

// ------------------------------------------------------------------ 9
// SLUTEN LINJE: en kvadrat — fyra segment och fyra skarvar, den första
// skarven i startpunkten (dist 0) med sista punkten som föregående, det
// sista segmentet tillbaka till början (dist 30 → 40), och inget segment
// som slutar linjen. En upprepad slutpunkt är samma ring; två punkter är
// ingen ring.
void fallSluten()
{
    using namespace gfx::lineflag;
    gfx::PolylineStyle s = stil(gfx::LineCap::Round);   // ändformen ska inte synas
    s.join   = gfx::LineJoin::Miter;
    s.closed = true;
    const std::vector<gfx::Point2d> kv = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    std::vector<gfx::LineChunk> ch;
    gfx::buildPolyline(kv, s, ch);
    check(ch.size() == 1 && ch[0].vertices.size() == 32,
          "sluten kvadrat: 4 segment + 4 skarvar = 32 vertex, inga ändar",
          ch.empty() ? "" : std::to_string(ch[0].vertices.size()));
    if (ch.size() != 1 || ch[0].vertices.size() != 32)
        return;
    const auto& v = ch[0].vertices;
    bool skarv0 = true;
    for (int h = 0; h < 4; ++h)
        skarv0 = skarv0 && vertexLika(v[h], 0, 0, 0, 10, 10, 0, 0, kSpets, h);
    check(skarv0, "sluten: första biten är skarven i startpunkten (föregående "
                  "= sista punkten, dist 0)");
    check(vertexLika(v[28], 0, 10, 10, 10, 0, 0, 30, kStart, -1) &&
              vertexLika(v[31], 0, 0, 0, 10, 10, 0, 40, kSlut, +1),
          "sluten: sista segmentet går (0,10) → (0,0), dist 30 → 40, nästa = "
          "andra punkten");
    bool ingenÄnde = true;
    for (const gfx::LineVertex& q : v)
        ingenÄnde = ingenÄnde && (q.ends & (kStartsLine | kEndsLine)) == 0;
    check(ingenÄnde, "sluten: inget segment börjar eller slutar linjen");

    std::vector<gfx::Point2d> upprepad = kv;
    upprepad.push_back({0, 0});
    std::vector<gfx::LineChunk> u;
    gfx::buildPolyline(upprepad, s, u);
    check(u.size() == 1 && u[0].vertices.size() == 32,
          "sluten: en upprepad slutpunkt är samma ring");

    std::vector<gfx::LineChunk> två;
    gfx::buildPolyline(std::vector<gfx::Point2d>{{0, 0}, {10, 0}}, s, två);
    check(två.size() == 1 && två[0].vertices.size() == 12 &&
              (två[0].vertices[4].ends & (kStartsLine | kEndsLine)) ==
                  (kStartsLine | kEndsLine),
          "sluten med två punkter: en öppen linje (runda ändar, segmentet "
          "börjar och slutar den)");
}

// ------------------------------------------------------------------ 10
// STRECKMÖNSTRET: de kumulativa sluten, SVG:s regler och taket.
void fallStreckmönstret()
{
    const auto mönster = [](std::vector<float> v, float off, gfx::SceneDash& d) {
        return gfx::makeDash(v, off, d);
    };
    gfx::SceneDash d;
    check(mönster({10, 5}, 2.5f, d) && d.count == 2 && d.ends[0] == 10.0f &&
              d.ends[1] == 15.0f && d.offset == 2.5f,
          "{10, 5}: sluten 10 och 15, förskjutningen bevarad");
    check(mönster({5, 3, 2}, 0, d) && d.count == 6 && d.ends[0] == 5.0f &&
              d.ends[2] == 10.0f && d.ends[3] == 15.0f && d.ends[5] == 20.0f,
          "ett udda antal upprepas: {5, 3, 2} är {5, 3, 2, 5, 3, 2}");
    check(mönster({0, 6}, 0, d) && d.count == 2 && d.ends[0] == 0.0f &&
              d.ends[1] == 6.0f,
          "{0, 6}: ett streck utan längd är giltigt (prickar med runda ändar)");
    for (const auto& [v, namn] :
         {std::pair{std::vector<float>{}, "tomt"},
          std::pair{std::vector<float>{-1, 5}, "negativt"},
          std::pair{std::vector<float>{0, 0}, "summan 0"},
          std::pair{std::vector<float>{NAN, 4}, "icke-ändligt"}}) {
        d.count = 99;
        check(mönster(v, 0, d) && d.count == 0,
              std::string("heldraget (svaret true): ") + namn);
    }
    d.count = 99;
    check(!mönster({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 0, d) && d.count == 0,
          "tio längder ryms inte i uniformerna: false och heldragen");
    check(!mönster({1, 2, 3, 4, 5}, 0, d) && d.count == 0,
          "fem längder blir tio efter upprepningen: false");
    check(mönster({1, 2, 3, 4, 5, 6, 7, 8}, 0, d) && d.count == 8 &&
              d.ends[7] == 36.0f,
          "åtta längder ryms precis");
    check(mönster({4, 2}, INFINITY, d) && d.offset == 0.0f,
          "en icke-ändlig förskjutning blir 0");
}

// ================================================================== YTORNA

using Ringar = std::vector<std::vector<gfx::Point2d>>;

const gfx::Color kYtfärg{40, 120, 200, 255};

gfx::PolygonStyle ytstil()
{
    gfx::PolygonStyle s;
    s.color = kYtfärg;
    return s;
}

// Grindens EGEN skosnöresformel — relativt ringens första punkt, så SWEREF-
// talen inte äter siffrorna.
double skosnöre(const std::vector<gfx::Point2d>& r)
{
    double a = 0.0;
    for (std::size_t i = 0, j = r.size() - 1; i < r.size(); j = i++)
        a += (r[j].x - r[0].x) * (r[i].y - r[0].y) -
             (r[i].x - r[0].x) * (r[j].y - r[0].y);
    return std::abs(a) / 2.0;
}

// Summan av trianglarnas areor i världen, över alla chunkar.
double täckt(const std::vector<gfx::FillChunk>& ch)
{
    double s = 0.0;
    for (const gfx::FillChunk& c : ch) {
        for (std::size_t t = 0; t + 2 < c.indices.size(); t += 3) {
            const gfx::FillVertex& a = c.vertices[c.indices[t]];
            const gfx::FillVertex& b = c.vertices[c.indices[t + 1]];
            const gfx::FillVertex& d = c.vertices[c.indices[t + 2]];
            s += std::abs((double(b.x) - a.x) * (double(d.y) - a.y) -
                          (double(b.y) - a.y) * (double(d.x) - a.x)) / 2.0;
        }
    }
    return s;
}

bool indexInom(const std::vector<gfx::FillChunk>& ch)
{
    for (const gfx::FillChunk& c : ch) {
        if (c.vertices.size() > gfx::kMaxChunkVertices ||
            c.indices.size() % 3 != 0)
            return false;
        for (std::uint32_t i : c.indices)
            if (i >= c.vertices.size())
                return false;
    }
    return true;
}

std::string area(double v)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.6f", v);
    return buf;
}

// ------------------------------------------------------------------ Y1
void fallYtansArea()
{
    const Ringar kvadrat = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    std::vector<gfx::FillChunk> k;
    const bool okK = gfx::buildPolygon(kvadrat, ytstil(), k);
    check(okK && std::abs(täckt(k) - 100.0) < 1e-9,
          "Y1 kvadraten 10 × 10 täcker 100", area(täckt(k)));
    check(k.size() == 1 && k[0].vertices.size() == 16 &&
              k[0].indices.size() == 2 * 3 + 4 * 6,
          "Y1 kvadraten: 4 kantquadar (16 vertex), 2 trianglar + 8 i remsan",
          k.empty() ? "" : std::to_string(k[0].vertices.size()) + " vertex, " +
                               std::to_string(k[0].indices.size()) + " index");
    check(k.size() == 1 && k[0].originX == 0.0 && k[0].originY == 0.0 &&
              k[0].vertices[0].color.b == kYtfärg.b,
          "Y1 kvadraten: origo i ytterringens första punkt, färgen ur stilen");

    // L: 3 × 1 + 1 × 2 = 5.
    const Ringar L = {{{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3}}};
    std::vector<gfx::FillChunk> l;
    const bool okL = gfx::buildPolygon(L, ytstil(), l);
    check(okL && std::abs(täckt(l) - 5.0) < 1e-9, "Y1 L-formen täcker 5",
          area(täckt(l)));

    // 10 × 10 med ett hål 4 × 4 = 84.
    const Ringar hål = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}},
                        {{3, 3}, {7, 3}, {7, 7}, {3, 7}}};
    std::vector<gfx::FillChunk> h;
    const bool okH = gfx::buildPolygon(hål, ytstil(), h);
    check(okH && std::abs(täckt(h) - 84.0) < 1e-9,
          "Y1 kvadraten med ett hål 4 × 4 täcker 84", area(täckt(h)));
    check(h.size() == 1 && h[0].vertices.size() == 32,
          "Y1 hålets kanter får också kantquadar (8 kanter = 32 vertex)");
}

// ------------------------------------------------------------------ Y2
// Ett slumptal i [0, 1) ur mt19937 — standardens motor är fast definierad,
// fördelningarna är det inte, så avbildningen görs här.
double slump(std::mt19937& g)
{
    return g() / 4294967296.0;
}

std::vector<gfx::Point2d> stjärna(std::mt19937& g, double cx, double cy,
                                  double R, int n, double minR)
{
    std::vector<gfx::Point2d> r;
    const double steg = 2.0 * 3.14159265358979323846 / n;
    for (int i = 0; i < n; ++i) {
        const double a = (i + 0.3 * (slump(g) - 0.5)) * steg;
        const double d = R * (minR + (1.0 - minR) * slump(g));
        r.push_back({cx + d * std::cos(a), cy + d * std::sin(a)});
    }
    return r;
}

void fallSlumpytor()
{
    std::mt19937 g(20261010u);
    int    fel = 0, falska = 0, medHål = 0, stora = 0;
    double värst = 0.0;
    for (int k = 0; k < 200; ++k) {
        const int    n    = 3 + static_cast<int>(slump(g) * 298.0);
        const bool   sw   = k % 5 == 0;   // var femte på SWEREF
        const double cx   = sw ? 674000.0 + slump(g) * 1000.0 : slump(g) * 50.0;
        const double cy   = sw ? 6580000.0 + slump(g) * 1000.0 : slump(g) * 50.0;
        const double R    = 1.0 + slump(g) * (sw ? 400.0 : 20.0);
        Ringar       ytan = {stjärna(g, cx, cy, R, n, 0.3)};
        double       facit = skosnöre(ytan[0]);
        // Hål bara när ytterringen är tät nog att rymma dem (≥ 12 punkter:
        // vinkelsteget ≤ 39°, så hela skivan med radien 0,28 R är inne).
        if (n >= 12 && k % 2 == 0) {
            const int antal = 1 + k % 3;
            for (int h = 0; h < antal; ++h) {
                const double a  = h * 2.0 * 3.14159265358979323846 / antal;
                const double hx = cx + (antal > 1 ? 0.15 * R * std::cos(a) : 0);
                const double hy = cy + (antal > 1 ? 0.15 * R * std::sin(a) : 0);
                ytan.push_back(stjärna(g, hx, hy, 0.06 * R,
                                       5 + static_cast<int>(slump(g) * 40.0),
                                       0.5));
                facit -= skosnöre(ytan.back());
            }
            ++medHål;
        }
        if (n > 80)
            ++stora;
        std::vector<gfx::FillChunk> ch;
        const bool   ok   = gfx::buildPolygon(ytan, ytstil(), ch);
        const double fick = täckt(ch);
        const double rel  = std::abs(fick - facit) / facit;
        värst             = std::max(värst, rel);
        if (!ok)
            ++falska;
        if (rel > 1e-5 || !indexInom(ch))
            ++fel;
    }
    check(fel == 0, "Y2 200 slumpytor: arean stämmer (relativt fel < 1e-5)",
          std::to_string(fel) + " fel, värst " + area(värst));
    check(falska == 0, "Y2 200 slumpytor: buildPolygon svarar true för varje",
          std::to_string(falska) + " falska");
    check(medHål >= 40 && stora >= 100,
          "Y2 urvalet täcker hål OCH den z-ordnade vägen (> 80 punkter)",
          std::to_string(medHål) + " med hål, " + std::to_string(stora) +
              " stora");
}

// ------------------------------------------------------------------ Y3
void fallDegenererat()
{
    // Dubbletter, kollineära punkter mitt på kanterna och en upprepad
    // slutpunkt: fortfarande kvadraten 4 × 4.
    const Ringar skräp = {{{0, 0}, {0, 0}, {2, 0}, {4, 0}, {4, 0}, {4, 2},
                           {4, 4}, {2, 4}, {0, 4}, {0, 2}, {0, 0}}};
    std::vector<gfx::FillChunk> a;
    const bool okA = gfx::buildPolygon(skräp, ytstil(), a);
    check(okA && std::abs(täckt(a) - 16.0) < 1e-9 && indexInom(a),
          "Y3 dubbletter, kollineära punkter och upprepad slutpunkt: 16",
          area(täckt(a)));

    const Ringar ickeÄndlig = {
        {{0, 0}, {NAN, 5}, {6, 0}, {INFINITY, 1}, {6, 6}, {0, 6}}};
    std::vector<gfx::FillChunk> b;
    gfx::buildPolygon(ickeÄndlig, ytstil(), b);
    check(std::abs(täckt(b) - 36.0) < 1e-9,
          "Y3 icke-ändliga punkter hoppas över: 6 × 6 = 36", area(täckt(b)));

    std::vector<gfx::FillChunk> c;
    const bool okC = gfx::buildPolygon(Ringar{{{0, 0}, {5, 5}, {10, 10}}},
                                       ytstil(), c);
    const bool okD = gfx::buildPolygon(Ringar{{{0, 0}, {5, 5}}}, ytstil(), c);
    const bool okE = gfx::buildPolygon(Ringar{}, ytstil(), c);
    check(okC && okD && okE && c.empty(),
          "Y3 en ring utan area, för få punkter och inga ringar: ingenting, "
          "inget fel");

    // Ett hål utan area och ett med för få punkter hoppas över.
    const Ringar tommaHål = {{{0, 0}, {8, 0}, {8, 8}, {0, 8}},
                             {{2, 2}, {4, 4}, {6, 6}},
                             {{3, 3}, {5, 3}}};
    std::vector<gfx::FillChunk> d;
    const bool okF = gfx::buildPolygon(tommaHål, ytstil(), d);
    check(okF && std::abs(täckt(d) - 64.0) < 1e-9 && d.size() == 1 &&
              d[0].vertices.size() == 16,
          "Y3 hål utan area eller med för få punkter hoppas över",
          area(täckt(d)));
}

// ------------------------------------------------------------------ Y4
void fallOgiltigt()
{
    // Flugan: kanterna (0,0)→(4,4) och (4,0)→(0,4) korsar varandra.
    std::vector<gfx::FillChunk> a;
    const bool okA = gfx::buildPolygon(
        Ringar{{{0, 0}, {4, 4}, {4, 0}, {0, 4}}}, ytstil(), a);
    check(!okA, "Y4 en självkorsning (fluga) ger false");
    check(!a.empty() && täckt(a) > 1.0 && indexInom(a),
          "Y4 självkorsningen ritas ändå (trianglar finns)", area(täckt(a)));

    std::vector<gfx::FillChunk> b;
    const bool okB = gfx::buildPolygon(
        Ringar{{{0, 0}, {4, 0}, {4, 4}, {0, 4}},
               {{10, 10}, {12, 10}, {12, 12}, {10, 12}}},
        ytstil(), b);
    check(!okB && std::abs(täckt(b) - 16.0) < 1e-9,
          "Y4 ett hål utanför ytterringen ger false — ytan står kvar",
          area(täckt(b)));

    // En lyckad yta efteråt är fortfarande true (episoden tar slut).
    std::vector<gfx::FillChunk> c;
    check(gfx::buildPolygon(Ringar{{{0, 0}, {1, 0}, {0, 1}}}, ytstil(), c),
          "Y4 en giltig yta efter en ogiltig svarar true");
}

// ------------------------------------------------------------------ Y5
bool iRing(const std::vector<gfx::Point2d>& r, double x, double y)
{
    bool inne = false;
    for (std::size_t i = 0, j = r.size() - 1; i < r.size(); j = i++)
        if ((r[i].y > y) != (r[j].y > y) &&
            x < (r[j].x - r[i].x) * (y - r[i].y) / (r[j].y - r[i].y) + r[i].x)
            inne = !inne;
    return inne;
}

void fallRemsansRiktning()
{
    // FEL orientering med flit: ytterringen medurs, hålet moturs.
    const Ringar fel = {{{0, 0}, {0, 10}, {10, 10}, {10, 0}},
                        {{3, 3}, {7, 3}, {7, 7}, {3, 7}}};
    std::vector<gfx::FillChunk> ch;
    gfx::buildPolygon(fel, ytstil(), ch);
    if (ch.size() != 1 || ch[0].vertices.size() != 32) {
        check(false, "Y5 kvadraten med hål byggdes", std::to_string(ch.size()));
        return;
    }
    const gfx::FillChunk& c = ch[0];
    bool vänster = true, quad = true;
    for (std::size_t q = 0; q < c.vertices.size(); q += 4) {
        const gfx::FillVertex& s0 = c.vertices[q];
        const gfx::FillVertex& s1 = c.vertices[q + 1];
        const gfx::FillVertex& e0 = c.vertices[q + 2];
        const gfx::FillVertex& e1 = c.vertices[q + 3];
        quad = quad && s0.kind == 0 && s0.side == 0 && s1.kind == 0 &&
               s1.side == 1 && e0.kind == 1 && e0.side == 0 && e1.kind == 1 &&
               e1.side == 1 && s0.x == s1.x && s0.y == s1.y &&
               e0.x == e1.x && e0.y == e1.y && s0.nextX == e0.x &&
               s0.nextY == e0.y && e0.prevX == s0.x && e0.prevY == s0.y;
        // En bit till VÄNSTER om kantens mitt ska ligga i ytan: inne i
        // ytterringen och utanför hålet.
        const double mx = (s0.x + e0.x) / 2.0, my = (s0.y + e0.y) / 2.0;
        double       dx = e0.x - s0.x, dy = e0.y - s0.y;
        const double l  = std::hypot(dx, dy);
        dx /= l;
        dy /= l;
        const double vx = mx - dy * 0.25, vy = my + dx * 0.25;
        const bool   iYtan = iRing(fel[0], vx, vy) && !iRing(fel[1], vx, vy);
        vänster = vänster && iYtan;
    }
    check(quad, "Y5 varje kantquad: början inne/ute, slutet inne/ute, med "
                "kantens egna punkter");
    check(vänster, "Y5 ytan ligger till VÄNSTER om varje kant (också hålets) "
                   "trots fel orientering in");
}

// ------------------------------------------------------------------ Y6
void fallYtornasChunkar()
{
    // En cirkel med 20 000 punkter: 80 000 vertex — mer än en chunk.
    const int            N = 20000;
    std::vector<gfx::Point2d> cirkel(N);
    for (int i = 0; i < N; ++i) {
        const double a = i * 2.0 * 3.14159265358979323846 / N;
        cirkel[i]      = {674000.0 + 300.0 * std::cos(a),
                          6580000.0 + 300.0 * std::sin(a)};
    }
    std::vector<gfx::FillChunk> ch;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = gfx::buildPolygon(Ringar{cirkel}, ytstil(), ch);
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0)
                          .count();
    const double facit = skosnöre(cirkel);
    bool samma = !ch.empty();
    for (const gfx::FillChunk& c : ch)
        samma = samma && c.originX == ch[0].originX &&
                c.originY == ch[0].originY;
    check(ok && ch.size() >= 2 && indexInom(ch),
          "Y6 20 000 punkter: flera chunkar, alla ≤ 65 536 och index inom",
          std::to_string(ch.size()) + " chunkar");
    check(samma, "Y6 en delad yta har SAMMA origo i alla sina chunkar");
    check(std::abs(täckt(ch) - facit) / facit < 1e-6,
          "Y6 arean bevaras över chunkgränsen",
          area(täckt(ch)) + " mot " + area(facit));
    std::printf("  (Y6: 20 000 punkter trianguleras på %.1f ms)\n", ms);

    std::vector<gfx::FillChunk> två;
    gfx::buildPolygon(Ringar{{{0, 0}, {2, 0}, {2, 2}, {0, 2}}}, ytstil(), två);
    gfx::buildPolygon(Ringar{{{5, 0}, {7, 0}, {7, 2}, {5, 2}}}, ytstil(), två);
    bool förskjutna = två.size() == 1 && två[0].vertices.size() == 32;
    for (std::size_t i = 30; förskjutna && i < två[0].indices.size(); ++i)
        förskjutna = två[0].indices[i] >= 16;
    check(förskjutna && två[0].vertices[16].x == 5.0f,
          "Y6 två små ytor delar en chunk; den andras index är förskjutna");
}

// ============================================================ SDF-TEXTEN

// Fältets värde som AVSTÅND till kanten i texlar (positivt inne) — grindens
// egen avkodning av formatet i gfx/sdf.h: 0,5 + d / (2 · radien).
double avstånd(const gfx::SdfField& f, int x, int y, int radie)
{
    const int v = f.values[static_cast<std::size_t>(y) * f.width + x];
    return (v / 255.0 - 0.5) * 2.0 * radie;
}

// En kvadrat side × side px helt täckt, med övre vänstra hörnet i (left,
// top) relativt pennan.
gfx::GlyphRaster kvadratRaster(int side, int left, int top)
{
    gfx::GlyphRaster r;
    r.width = r.height = side;
    r.left  = static_cast<float>(left);
    r.top   = static_cast<float>(top);
    r.coverage.assign(static_cast<std::size_t>(side) * side, 255);
    return r;
}

// ------------------------------------------------------------------ S1
void fallSdfVärden()
{
    // 10 × 10 px vid (0, −10), radien 8, skala 1: fältet är 26 × 26 texlar
    // från (−8, −18) och kvadraten täcker texlarna 8–17 åt båda håll. Referensen
    // är det RIKTIGA euklidiska avståndet från texelns mitt till kanten.
    gfx::SdfField f;
    gfx::makeSdf(kvadratRaster(10, 0, -10), 8, 1, f);
    check(f.width == 26 && f.height == 26 && f.left == -8 && f.top == -18,
          "S1 fältet: 26 × 26 texlar från (−8, −18) — glyfen plus radien",
          std::to_string(f.width) + " × " + std::to_string(f.height) + " från (" +
              std::to_string(f.left) + ", " + std::to_string(f.top) + ")");
    if (f.width != 26 || f.height != 26)
        return;
    struct Punkt {
        int         x, y;
        double      facit, tol;
        const char* vad;
    };
    const Punkt p[] = {
        {12, 12, 4.5, 0.04, "mitten: 4,5 inne"},
        {17, 12, 0.5, 0.04, "sista texeln inne: 0,5"},
        {18, 12, -0.5, 0.04, "första texeln ute: −0,5"},
        {20, 12, -2.5, 0.04, "2,5 utanför kanten"},
        {12, 25, -7.5, 0.04, "7,5 utanför — nära radien"},
        {20, 20, -std::hypot(2.5, 2.5), 0.25, "diagonalt utanför hörnet"},
    };
    for (const Punkt& q : p) {
        const double d = avstånd(f, q.x, q.y, 8);
        check(std::abs(d - q.facit) <= q.tol, std::string("S1 ") + q.vad,
              area(d) + " mot " + area(q.facit));
    }
    check(f.values[0] == 0,
          "S1 bortom radien är fältet 0 (klampat)",
          std::to_string(f.values[0]));

    // SKALA 2: samma kvadrat rasteriserad dubbelt så fint ger samma fält.
    gfx::SdfField g;
    gfx::makeSdf(kvadratRaster(20, 0, -20), 8, 2, g);
    bool samma = g.width == 26 && g.height == 26 && g.left == -8 &&
                 g.top == -18;
    double värst = 0.0;
    for (int y = 4; samma && y < 22; ++y)
        for (int x = 4; x < 22; ++x)
            värst = std::max(värst, std::abs(avstånd(g, x, y, 8) -
                                             avstånd(f, x, y, 8)));
    check(samma && värst <= 0.2,
          "S1 skala 2: samma fält som skala 1 (inom 0,2 texel)",
          area(värst));

    gfx::SdfField tom;
    gfx::makeSdf(gfx::GlyphRaster{}, 8, 2, tom);
    check(tom.width == 0 && tom.values.empty(),
          "S1 en tom glyf (mellanslag) ger inget fält");
}

// En font med kända steg: varje tecken är `steg` em brett (0,6 som förval),
// rasteriseras som en fylld låda 0,5 × 0,7 em på baslinjen, och mellanslaget
// är tomt. Raden är `rad` em hög med baslinjen `upp` em under överkanten.
// Rasteriseringsstorleken är fontens px.
class StegFont final : public gfx::NullFont {
public:
    StegFont(float px, gfx::FontStyle s, float steg = 0.6f, float rad = 1.25f,
             float upp = 0.8f)
        : px_(px), stil_(s), steg_(steg), rad_(rad), upp_(upp)
    {
    }
    bool  valid() const override { return true; }
    float height() const override { return rad_ * px_; }
    float ascent() const override { return upp_ * px_; }
    gfx::FontStyle style() const override { return stil_; }
    gfx::Sizef measure(const std::string& s) const override
    {
        return gfx::Sizef{tecken(s) * steg_ * px_, height()};
    }
    std::size_t fitWidth(const std::string& s, float maxW) const override
    {
        const std::size_t n =
            static_cast<std::size_t>(std::max(0.0f, maxW / (steg_ * px_)));
        return std::min(n, s.size());   // grindens texter är ASCII
    }
    float prefixWidth(const std::string& s, std::size_t b) const override
    {
        return static_cast<float>(std::min(b, s.size())) * steg_ * px_;
    }
    bool rasterGlyph(char32_t cp, gfx::GlyphRaster& out) const override
    {
        out         = gfx::GlyphRaster{};
        out.advance = steg_ * px_;
        if (cp == U' ')
            return true;
        out.width  = static_cast<int>(std::lround(0.5f * px_));
        out.height = static_cast<int>(std::lround(0.7f * px_));
        out.left   = std::round(0.05f * px_);
        out.top    = -static_cast<float>(out.height);
        out.coverage.assign(static_cast<std::size_t>(out.width) * out.height,
                            255);
        return true;
    }

private:
    static float tecken(const std::string& s)
    {
        float n = 0;
        for (unsigned char c : s)
            n += (c & 0xC0) != 0x80 ? 1.0f : 0.0f;
        return n;
    }
    float          px_;
    gfx::FontStyle stil_;
    float          steg_, rad_, upp_;
};

// En R8-bild som räknar sina uppladdningar.
class RäknadBild final : public gfx::NullImage {
public:
    RäknadBild(int w, int h) : w_(w), h_(h) {}
    int  width() const override { return w_; }
    int  height() const override { return h_; }
    bool valid() const override { return true; }
    gfx::ImageFormat format() const override { return gfx::ImageFormat::R8; }
    int  uppladdningar = 0;

private:
    int w_, h_;
};

// Renderaren atlasen talar med: fonten ur ett val (vanlig även när fet
// begärs — det är syntetiseringens fall) och bilder som räknar. Med
// `perStil` (del S5) ger den en StegFont PER BEGÄRD STIL: den feta är
// bredare (0,75 em per tecken) och har en högre rad (1,5 em, baslinjen
// 0,95 em ner) — så delarnas egna steg och radmått syns i layouten.
class AtlasRenderer final : public gfx::NullRenderer {
public:
    gfx::FontStyle fontensStil = gfx::FontStyle::Regular;
    bool           perStil     = false;
    float          laddadPx    = 0.0f;
    int            sidor       = 0;
    int            uppladdningar = 0;
    std::unique_ptr<gfx::Font> loadFont(gfx::FontFamily, float px,
                                        gfx::FontStyle stil) override
    {
        laddadPx = px;
        if (perStil && gfx::isBold(stil))
            return std::make_unique<StegFont>(px, stil, 0.75f, 1.5f, 0.95f);
        return std::make_unique<StegFont>(px, perStil ? stil : fontensStil);
    }
    using gfx::NullRenderer::createImage;
    std::unique_ptr<gfx::Image> createImage(int w, int h, gfx::ImageFormat,
                                            const void*, std::size_t,
                                            bool) override
    {
        ++sidor;
        return std::make_unique<RäknadBild>(w, h);
    }
    bool updateImage(gfx::Image&, int x, int y, int w, int h, const void*,
                     std::size_t) override
    {
        ++uppladdningar;
        return x >= 0 && y >= 0 && x + w <= gfx::kSdfPageSize &&
               y + h <= gfx::kSdfPageSize;
    }
};

std::size_t quadar(const std::vector<gfx::GlyphChunk>& ch)
{
    std::size_t n = 0;
    for (const gfx::GlyphChunk& c : ch)
        n += c.vertices.size() / 4;
    return n;
}

// ------------------------------------------------------------------ S2
void fallTextlayout()
{
    AtlasRenderer r;
    gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
    check(atlas.valid() &&
              r.laddadPx == gfx::kSdfRefSize * gfx::kSdfSupersample,
          "S2 atlasen laddar fonten i rasteriseringsstorleken (32 × 2 px)",
          area(r.laddadPx));

    // BREDDEN = Font::measure, skalad till storleken.
    gfx::TextLayoutStyle st;
    st.size   = 20.0f;
    st.anchor = gfx::TextAnchor::TopLeft;
    std::vector<gfx::GlyphChunk> ch;
    const std::string text = "Hamburg fonstiv";
    const gfx::TextBox b = gfx::buildText(atlas, text, {0.0, 0.0}, st, ch);
    const double facit = atlas.font()->measure(text).w /
                         (gfx::kSdfRefSize * gfx::kSdfSupersample);   // em
    check(std::abs((b.x1 - b.x0) - facit) < 1e-5 && b.x0 == 0.0f &&
              b.y0 == 0.0f,
          "S2 glyflayoutens bredd = Font::measure (i em), ankaret uppe till "
          "vänster",
          area(b.x1 - b.x0) + " mot " + area(facit));
    check(std::abs((b.y1 - b.y0) - 1.25) < 1e-5,
          "S2 lådans höjd = en rad (fontens radavstånd)", area(b.y1 - b.y0));
    check(quadar(ch) == 14 && ch.size() == 1 && ch[0].page == 0,
          "S2 en quad per glyf utom mellanslaget, allt på sida 0",
          std::to_string(quadar(ch)) + " quadar");
    const int uppl = r.uppladdningar;
    // H a m b u r g f o n s t i v — fjorton olika bokstäver.
    check(uppl == 14 && r.sidor == 1,
          "S2 varje UNIK glyf laddas upp en gång (14 olika), en sida",
          std::to_string(uppl) + " uppladdningar, " + std::to_string(r.sidor) +
              " sidor");
    gfx::buildText(atlas, text, {50.0, 50.0}, st, ch);
    check(r.uppladdningar == uppl,
          "S2 samma text igen laddar inte upp något — atlasen behåller glyferna",
          std::to_string(r.uppladdningar - uppl));

    // ANKARET och raderna: mitten, två rader.
    std::vector<gfx::GlyphChunk> c2;
    st.anchor = gfx::TextAnchor::Center;
    const gfx::TextBox m = gfx::buildText(atlas, "AB\nABCD", {0, 0}, st, c2);
    check(std::abs(m.x0 + 1.2f) < 1e-5 && std::abs(m.x1 - 1.2f) < 1e-5 &&
              std::abs(m.y0 + 1.25f) < 1e-5 && std::abs(m.y1 - 1.25f) < 1e-5,
          "S2 Center med två rader: lådan centrerad kring ankaret (2,4 × 2,5 em)",
          area(m.x0) + ", " + area(m.y0) + " – " + area(m.x1) + ", " +
              area(m.y1));

    // GLORIAN: alla glorians quadar FÖRE fyllningarna.
    std::vector<gfx::GlyphChunk> c3;
    st.halo      = gfx::Color{255, 255, 255, 255};
    st.haloWidth = 2.0f;
    gfx::buildText(atlas, "ABC", {0, 0}, st, c3);
    bool ordning = quadar(c3) == 6;
    for (std::size_t q = 0; ordning && q < 6; ++q)
        ordning = ((c3[0].vertices[q * 4].flags & gfx::glyphflag::kHaloPass) !=
                   0) == (q < 3);
    check(ordning, "S2 gloria: tre glorior först, sedan tre fyllningar");

    // PLATTAN först, med lådan + luften.
    std::vector<gfx::GlyphChunk> c4;
    gfx::TextLayoutStyle pl;
    pl.size       = 10.0f;
    pl.anchor     = gfx::TextAnchor::TopLeft;
    pl.background = gfx::Color{255, 255, 0, 255};
    pl.padding    = 2.0f;   // 0,2 em
    const gfx::TextBox pb = gfx::buildText(atlas, "AB", {0, 0}, pl, c4);
    const gfx::GlyphVertex& p0 = c4.empty() ? gfx::GlyphVertex{} : c4[0].vertices[0];
    check(quadar(c4) == 3 &&
              (p0.flags & gfx::glyphflag::kShapeMask) ==
                  static_cast<std::uint32_t>(gfx::GlyphShape::Plate) &&
              std::abs(pb.x0 + 0.2f) < 1e-5 && std::abs(p0.ox - pb.x0) < 1e-5,
          "S2 plattan: först, en form, lådan med luften runt texten",
          area(pb.x0) + " / " + area(p0.ox));
}

// ------------------------------------------------------------------ S3
void fallSyntes()
{
    // Fet och kursiv BEGÄRS men fonten är vanlig: atlasen säger det, och
    // byggaren syntetiserar.
    AtlasRenderer r;
    r.fontensStil = gfx::FontStyle::Regular;
    gfx::SdfAtlas fet(r, gfx::FontFamily::UI);
    check(fet.style(gfx::FontStyle::BoldItalic) == gfx::FontStyle::Regular,
          "S3 atlasen: fet kursiv begärd, fontens stil vanlig");
    gfx::TextLayoutStyle st;
    st.size      = 20.0f;
    st.anchor    = gfx::TextAnchor::TopLeft;
    st.fontStyle = gfx::FontStyle::BoldItalic;
    std::vector<gfx::GlyphChunk> ch;
    const gfx::TextBox b = gfx::buildText(fet, "AAA", {0, 0}, st, ch);
    const double bredd = 3 * (0.6 + 2.0 * gfx::kSyntheticBoldEm);
    check(std::abs((b.x1 - b.x0) - bredd) < 1e-5,
          "S3 syntetisk fet: varje steg 1/24 em bredare",
          area(b.x1 - b.x0) + " mot " + area(bredd));
    bool fetFlagga = quadar(ch) == 3;
    for (const gfx::GlyphVertex& v : ch.empty() ? std::vector<gfx::GlyphVertex>{}
                                                : ch[0].vertices)
        fetFlagga = fetFlagga && (v.flags & gfx::glyphflag::kSyntheticBold);
    check(fetFlagga, "S3 syntetisk fet: varje vertex bär flaggan");
    if (ch.empty() || ch[0].vertices.size() < 4)
        return;
    // Kursiv: övre hörnen (y mindre) förskjutna åt HÖGER mot de nedre —
    // skjuvningen kring baslinjen.
    const gfx::GlyphVertex* q = ch[0].vertices.data();
    const float dxTopp = q[0].ox, dxBotten = q[2].ox;   // (x0, y0) och (x0, y1)
    const float dy     = q[2].oy - q[0].oy;
    check(std::abs((dxTopp - dxBotten) - gfx::kSyntheticItalicShear * dy) < 1e-5,
          "S3 syntetisk kursiv: hörnen skjuvade med ≈ 12° kring baslinjen",
          area(dxTopp - dxBotten) + " mot " +
              area(gfx::kSyntheticItalicShear * dy));

    // En RIKTIG fet font (fontens stil är fet): ingen syntes.
    AtlasRenderer rf;
    rf.fontensStil = gfx::FontStyle::Bold;
    gfx::SdfAtlas äkta(rf, gfx::FontFamily::UI);
    st.fontStyle = gfx::FontStyle::Bold;
    std::vector<gfx::GlyphChunk> ce;
    const gfx::TextBox be = gfx::buildText(äkta, "AAA", {0, 0}, st, ce);
    check(std::abs((be.x1 - be.x0) - 1.8) < 1e-5 && !ce.empty() &&
              !(ce[0].vertices[0].flags & gfx::glyphflag::kSyntheticBold),
          "S3 en riktig fet font syntetiseras inte");
}

// ------------------------------------------------------------------ S4
void fallMarkörer()
{
    std::vector<gfx::GlyphChunk> ch;
    gfx::MarkerLayoutStyle st;
    st.size = 8.0f;
    const gfx::Point2d p[] = {{0, 0}, {10, 0}, {NAN, 0}, {20, 5}};
    gfx::buildMarkers(p, st, ch);
    check(ch.size() == 1 && quadar(ch) == 3 && ch[0].page == -1,
          "S4 markörer: en quad per ändlig punkt, ingen atlassida");
    if (ch.empty() || ch[0].vertices.empty())
        return;
    const gfx::GlyphVertex& v = ch[0].vertices[0];
    check((v.flags & gfx::glyphflag::kShapeMask) ==
                  static_cast<std::uint32_t>(gfx::GlyphShape::Circle) &&
              v.ox == -0.5f && v.u == -0.5f,
          "S4 cirkeln: halva måttet ½ em utan kontur");
    std::vector<gfx::GlyphChunk> k;
    st.shape        = gfx::MarkerShape::Square;
    st.outline      = gfx::Color{0, 0, 0, 255};
    st.outlineWidth = 2.0f;   // ¼ em
    gfx::buildMarkers(std::span<const gfx::Point2d>(p, 1), st, k);
    check(!k.empty() && k[0].vertices[0].u == -0.75f &&
              k[0].vertices[0].haloWidth == 2.0f,
          "S4 konturen ligger UTANFÖR fyllningen: quaden växer ¼ em");

    // ANKARET flyttar rutan men inte formens läge: Bottom lägger rutan OVANFÖR
    // punkten (y nedåt), u/v mäts från rutans mitt — och lådan är rutans.
    std::vector<gfx::GlyphChunk> a;
    gfx::MarkerLayoutStyle       sa;
    sa.size   = 8.0f;
    sa.anchor = gfx::TextAnchor::Bottom;
    const gfx::TextBox ba =
        gfx::buildMarkers(std::span<const gfx::Point2d>(p, 1), sa, a);
    check(!a.empty() && a[0].vertices.size() == 4 && a[0].vertices[0].ox == -0.5f &&
              a[0].vertices[0].oy == -1.0f && a[0].vertices[3].oy == 0.0f &&
              a[0].vertices[0].u == -0.5f && a[0].vertices[0].v == -0.5f &&
              ba.x0 == -0.5f && ba.y0 == -1.0f && ba.x1 == 0.5f && ba.y1 == 0.0f,
          "S4 ankaret Bottom: rutan ovanför punkten, formens läge från dess "
          "mitt, lådan [−½, ½] × [−1, 0] em");
    std::vector<gfx::GlyphChunk> tl;
    sa.anchor = gfx::TextAnchor::TopLeft;
    const gfx::TextBox bt =
        gfx::buildMarkers(std::span<const gfx::Point2d>(p, 1), sa, tl);
    check(bt.x0 == 0.0f && bt.y0 == 0.0f && bt.x1 == 1.0f && bt.y1 == 1.0f,
          "S4 ankaret TopLeft: lådan [0, 1] × [0, 1] em");

    // BILDMARKÖREN: u, v = 0–1 i rutans hörn (rad 0 överst), flaggan kImage
    // utan formbitar, ingen kontur — också när en är satt i stilen.
    std::vector<gfx::GlyphChunk> im;
    gfx::MarkerLayoutStyle       si;
    si.size         = 8.0f;
    si.image        = true;
    si.outline      = gfx::Color{0, 0, 0, 255};
    si.outlineWidth = 2.0f;
    const gfx::TextBox bi =
        gfx::buildMarkers(std::span<const gfx::Point2d>(p, 1), si, im);
    bool bildOk = !im.empty() && im[0].vertices.size() == 4;
    if (bildOk) {
        const auto& v4 = im[0].vertices;
        bildOk = v4[0].u == 0.0f && v4[0].v == 0.0f && v4[3].u == 1.0f &&
                 v4[3].v == 1.0f && v4[0].ox == -0.5f && v4[3].ox == 0.5f &&
                 (v4[0].flags & gfx::glyphflag::kImage) != 0 &&
                 (v4[0].flags & gfx::glyphflag::kShapeMask) == 0 &&
                 v4[0].haloWidth == 0.0f;
    }
    check(bildOk && bi.x0 == -0.5f && bi.x1 == 0.5f,
          "S4 bildmarkören: u, v 0–1, kImage utan form, ingen kontur, rutan 1 em");
}

// ------------------------------------------------------------------ S5
//
// TEXTDELARNA mot StegFont: den vanliga 0,6 em per
// tecken och raden 1,25 em, den feta (`AtlasRenderer::perStil`) 0,75 em och
// 1,5 em — det förväntade är de stegen och de måtten, och radbrytningen
// `gfx::wrapText` på samma font (S5d).

// Glyfquadarnas RITORDNING (chunkarna i ordning, quadarna i ordning): ligger
// varje gloria-quad före varje fyllnad?
template <class Chunks>
bool gloriorFörst(const Chunks& ch)
{
    bool fyllningSedd = false;
    for (const auto& c : ch)
        for (std::size_t q = 0; q + 4 <= c.vertices.size(); q += 4) {
            const std::uint32_t f = c.vertices[q].flags;
            if ((f & gfx::glyphflag::kShapeMask) !=
                static_cast<std::uint32_t>(gfx::GlyphShape::Glyph))
                continue;   // plattan
            const bool gloria = (f & gfx::glyphflag::kHaloPass) != 0;
            if (gloria && fyllningSedd)
                return false;
            fyllningSedd = fyllningSedd || !gloria;
        }
    return true;
}

bool lika(gfx::Color a, gfx::Color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

void fallTextdelar()
{
    const gfx::Color svart{0, 0, 0, 255}, röd{200, 0, 0, 255};
    gfx::TextLayoutStyle st;
    st.size   = 20.0f;
    st.anchor = gfx::TextAnchor::TopLeft;

    // S5a — BREDDEN är delarnas egna steg, FÄRGEN delens per vertex, allt i
    // EN chunk på sida 0, och alla glorior före alla fyllningar över
    // delgränsen.
    {
        AtlasRenderer r;
        r.perStil = true;
        gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
        gfx::TextLayoutStyle s = st;
        s.halo      = gfx::Color{255, 255, 255, 255};
        s.haloWidth = 2.0f;
        const gfx::StyledText delar[] = {{"AB", svart, gfx::FontStyle::Regular},
                                         {"CD", röd, gfx::FontStyle::Bold}};
        std::vector<gfx::GlyphChunk> ch;
        const gfx::TextBox b = gfx::buildText(atlas, delar, {0.0, 0.0}, s, ch);
        check(std::abs((b.x1 - b.x0) - 2.7) < 1e-5,
              "S5a bredden = delarnas egna steg: 2 · 0,6 + 2 · 0,75 = 2,7 em",
              area(b.x1 - b.x0));
        bool färger = ch.size() == 1 && ch[0].page == 0 && quadar(ch) == 8;
        for (std::size_t q = 4; färger && q < 8; ++q)
            for (int k = 0; k < 4; ++k)
                färger = färger &&
                         lika(ch[0].vertices[q * 4 + k].color, q < 6 ? svart : röd);
        check(färger,
              "S5a EN chunk på sida 0 med åtta quadar; fyllningarnas färg är "
              "delens (svart, svart, röd, röd) per vertex",
              std::to_string(ch.size()) + " chunkar, " + std::to_string(quadar(ch)) +
                  " quadar");
        check(gloriorFörst(ch),
              "S5a alla glorior före alla fyllningar — också över delgränsen");
    }

    // S5b — NYCKELN är (stil, kodpunkt): samma tecken i vanlig och fet är två
    // uppladdningar, och samma etikett igen laddar inte upp något.
    {
        AtlasRenderer r;
        r.perStil = true;
        gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
        const gfx::StyledText delar[] = {{"Q", svart, gfx::FontStyle::Regular},
                                         {"Q", svart, gfx::FontStyle::Bold}};
        std::vector<gfx::GlyphChunk> ch;
        gfx::buildText(atlas, delar, {0.0, 0.0}, st, ch);
        const int första = r.uppladdningar;
        gfx::buildText(atlas, delar, {10.0, 0.0}, st, ch);
        check(första == 2 && r.uppladdningar == 2,
              "S5b samma kodpunkt i vanlig och fet: två uppladdningar, sedan "
              "ingen",
              std::to_string(första) + " / " + std::to_string(r.uppladdningar));
    }

    // S5c — RADBRYTNINGEN med varje glyfs EGET steg: "AAA " (vanlig) + "BBB"
    // (fet) i 140 texlar. Med de feta stegen (24) når "BBB" förbi bredden och
    // raden bryts vid mellanslaget; med den vanliga stilens (19,2) hade allt
    // rymts (134,4). Två rader à 1,5 em (den högsta stilen).
    {
        AtlasRenderer r;
        r.perStil = true;
        gfx::SdfAtlas        atlas(r, gfx::FontFamily::UI);
        gfx::TextLayoutStyle s = st;
        s.wrapWidth            = 87.5f;   // · 32/20 = 140 texlar
        const gfx::StyledText delar[] = {{"AAA ", svart, gfx::FontStyle::Regular},
                                         {"BBB", svart, gfx::FontStyle::Bold}};
        std::vector<gfx::GlyphChunk> ch;
        const gfx::TextBox b = gfx::buildText(atlas, delar, {0.0, 0.0}, s, ch);
        check(std::abs((b.y1 - b.y0) - 3.0) < 1e-5 &&
                  std::abs((b.x1 - b.x0) - 2.25) < 1e-5,
              "S5c den feta delens bredd bryter raden: två rader à 1,5 em, den "
              "bredaste 3 · 0,75 em",
              area(b.x1 - b.x0) + " × " + area(b.y1 - b.y0));
    }

    // S5d — EN DEL bryts EXAKT som `gfx::wrapText` på samma font: radernas
    // antal, glyferna (utom mellanslagen) på varje rad och den bredaste
    // radens bredd, för en korpus och fyra bredder. Bredderna ligger med flit
    // en bit från ett helt antal tecken — StegFonts `fitWidth` räknar ett
    // antal, byggaren en summa.
    {
        AtlasRenderer r;
        gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
        const char* const korpus[] = {"aaa bbbb cc ddddddd e",
                                      "abcdefghijklmnopqrstu",
                                      "ab  cd   ef gh",
                                      " inledande blanksteg",
                                      "rad ett\nrad tva och tre",
                                      "slutar med radbrytning \n",
                                      "x"};
        const float bredder[] = {30.0f, 50.0f, 77.0f, 130.0f};
        int         fel       = 0;
        std::string första;
        for (const char* text : korpus)
            for (float w : bredder) {
                gfx::TextLayoutStyle s = st;
                s.align     = gfx::TextAlign::Left;
                s.wrapWidth = w;
                std::vector<gfx::GlyphChunk> ch;
                const gfx::TextBox b = gfx::buildText(atlas, text, {0.0, 0.0}, s, ch);
                const float maxW = w * (gfx::kSdfRefSize / s.size) *
                                   static_cast<float>(gfx::kSdfSupersample);
                const std::string t     = text;
                const auto        rader = gfx::wrapText(*atlas.font(), t, maxW);
                std::vector<int>  facit;
                double            facitW = 0.0;
                for (const gfx::WrappedLine& l : rader) {
                    int n = 0;
                    for (std::size_t i = l.begin; i < l.end; ++i)
                        n += t[i] != ' ' ? 1 : 0;
                    facit.push_back(n);
                    facitW = std::max(facitW, 0.6 * static_cast<double>(l.end - l.begin));
                }
                // Byggarens rader ur quadarnas överkant (raden k har sin
                // baslinje 0,8 + 1,25 k em ned).
                std::vector<int> fick(rader.size(), 0);
                bool             utanför = false;
                for (const gfx::GlyphChunk& c : ch)
                    for (std::size_t q = 0; q + 4 <= c.vertices.size(); q += 4) {
                        const int k = static_cast<int>(
                            std::floor((c.vertices[q].oy + 0.5) / 1.25));
                        if (k < 0 || k >= static_cast<int>(fick.size()))
                            utanför = true;
                        else
                            ++fick[static_cast<std::size_t>(k)];
                    }
                const bool ok = !utanför && fick == facit &&
                                std::abs((b.y1 - b.y0) - 1.25 * rader.size()) < 1e-4 &&
                                std::abs((b.x1 - b.x0) - facitW) < 1e-4;
                if (!ok && fel++ == 0)
                    första = std::string("\"") + text + "\" i " + area(w);
            }
        check(fel == 0,
              "S5d en del bryts exakt som gfx::wrapText (28 fall: rader, glyfer "
              "per rad, bredaste raden)",
              fel ? std::to_string(fel) + " fel, först " + första : "");
    }

    // S5e — SYNTESEN PER DEL: fonten är vanlig för alla stilar, så bara den
    // feta delens vertex bär kSyntheticBold, och bara dess steg växer 1/24 em.
    {
        AtlasRenderer r;
        r.fontensStil = gfx::FontStyle::Regular;
        gfx::SdfAtlas         atlas(r, gfx::FontFamily::UI);
        const gfx::StyledText delar[] = {{"AA", svart, gfx::FontStyle::Regular},
                                         {"BB", svart, gfx::FontStyle::Bold}};
        std::vector<gfx::GlyphChunk> ch;
        const gfx::TextBox b = gfx::buildText(atlas, delar, {0.0, 0.0}, st, ch);
        bool flaggor = quadar(ch) == 4;
        for (std::size_t q = 0; flaggor && q < 4; ++q)
            for (int k = 0; k < 4; ++k)
                flaggor = flaggor &&
                          ((ch[0].vertices[q * 4 + k].flags &
                            gfx::glyphflag::kSyntheticBold) != 0) == (q >= 2);
        const double bredd = 4 * 0.6 + 2 * 2.0 * gfx::kSyntheticBoldEm;
        check(flaggor && std::abs((b.x1 - b.x0) - bredd) < 1e-5,
              "S5e syntetisk fet bara i den feta delen: dess vertex bär flaggan "
              "och dess steg är 1/24 em bredare",
              area(b.x1 - b.x0) + " mot " + area(bredd));
    }

    // S5f — RADENS MÅTT är de STÖRSTA bland stilarna som används: en vanlig
    // etikett har 1,25 em, en med en fet del 1,5 em, och den
    // vanliga glyfens baslinje flyttar ned med den feta stilens (0,95 − 0,8
    // em).
    {
        AtlasRenderer r;
        r.perStil = true;
        gfx::SdfAtlas         atlas(r, gfx::FontFamily::UI);
        const gfx::StyledText vanlig[] = {{"AA", svart, gfx::FontStyle::Regular}};
        const gfx::StyledText blandad[] = {{"AA", svart, gfx::FontStyle::Regular},
                                           {"BB", svart, gfx::FontStyle::Bold}};
        std::vector<gfx::GlyphChunk> cv, cb;
        const gfx::TextBox bv = gfx::buildText(atlas, vanlig, {0.0, 0.0}, st, cv);
        const gfx::TextBox bb = gfx::buildText(atlas, blandad, {0.0, 0.0}, st, cb);
        const bool baslinje = !cv.empty() && !cb.empty() &&
                              std::abs((cb[0].vertices[0].oy - cv[0].vertices[0].oy) -
                                       0.15f) < 1e-4f;
        check(std::abs((bv.y1 - bv.y0) - 1.25) < 1e-5 &&
                  std::abs((bb.y1 - bb.y0) - 1.5) < 1e-5 && baslinje,
              "S5f radhöjden = den största bland stilarna (1,25 ensam, 1,5 med "
              "en fet del) och baslinjen den djupaste",
              area(bv.y1 - bv.y0) + " / " + area(bb.y1 - bb.y0));
    }

    // S5g — ORDNINGEN ÖVER ATLASSIDOR: en etikett vars
    // första glyf hamnar sist på sida 0 och den andra först på sida 1. Varje
    // gloria-quad ska ritas före varje fyllnad — också när bygget lyfts in i
    // rymden. (Återanvänds en äldre chunk hamnar fyllningen på
    // sida 0 i samma chunk som glorian på sida 0, FÖRE glorian på sida 1.)
    {
        // Hur många glyfer ryms på en sida? Prövat på en egen atlas — alla
        // StegFont-glyfer har samma fält.
        AtlasRenderer prov;
        gfx::SdfAtlas provAtlas(prov, gfx::FontFamily::UI);
        std::size_t   perSida = 0;
        for (char32_t cp = 0x4E00; cp < 0x4E00 + 4000; ++cp) {
            provAtlas.glyph(cp);
            if (provAtlas.pageCount() == 2)
                break;
            ++perSida;
        }
        AtlasRenderer r;
        gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
        for (std::size_t i = 0; i + 1 < perSida; ++i)
            atlas.glyph(static_cast<char32_t>(0x4E00 + i));   // en plats kvar
        gfx::TextLayoutStyle s = st;
        s.halo      = gfx::Color{255, 255, 255, 255};
        s.haloWidth = 2.0f;
        std::vector<gfx::GlyphChunk> ch;
        gfx::buildText(atlas, "XY", {0.0, 0.0}, s, ch);
        bool tvåSidor = false, sida0 = false;
        for (const gfx::GlyphChunk& c : ch) {
            sida0    = sida0 || c.page == 0;
            tvåSidor = tvåSidor || (c.page == 1 && sida0);
        }
        std::vector<gfx::GlyphChunk3> ch3;
        gfx::buildText(atlas, "XY", gfx::Vec3d{0.0, 0.0, 5.0}, s, ch3);
        check(perSida > 1 && tvåSidor && gloriorFörst(ch) && gloriorFörst(ch3),
              "S5g en etikett över två atlassidor: alla glorior ritas före alla "
              "fyllningar, i planet och lyft i rymden",
              std::to_string(perSida) + " glyfer per sida, " +
                  std::to_string(ch.size()) + " chunkar");
    }
}

// ------------------------------------------------------------------ S6
//
// MÄTNINGEN UTAN BYGGE (`gfx::measureText`). Referensen är BYGGARENS
// låda på en EGEN atlas — mätningen görs först på en ny atlas, så dess steg
// kommer ur `Font::measure` och inte ur byggda glyfer — och StegFonts kända
// steg och radmått; radbrytningen mot `gfx::wrapText` (S5d:s korpus).

bool sammaLåda(const gfx::TextBox& a, const gfx::TextBox& b)
{
    return std::abs(a.x0 - b.x0) < 1e-5f && std::abs(a.y0 - b.y0) < 1e-5f &&
           std::abs(a.x1 - b.x1) < 1e-5f && std::abs(a.y1 - b.y1) < 1e-5f;
}

void fallMätning()
{
    const gfx::Color svart{0, 0, 0, 255};
    gfx::TextLayoutStyle st;
    st.size   = 20.0f;
    st.anchor = gfx::TextAnchor::TopLeft;

    // S6a — LÅDAN är byggarens: S5a:s delar (med gloria), S5c:s brytning och
    // S5f:s blandade stilar, och samma etikett med ankaret Center, luft och
    // förskjutning. Mätningen på en ny atlas, bygget på en annan.
    {
        struct Fall {
            const char*                  namn;
            std::vector<gfx::StyledText> delar;
            gfx::TextLayoutStyle         stil;
        };
        gfx::TextLayoutStyle gloria = st;
        gloria.halo      = gfx::Color{255, 255, 255, 255};
        gloria.haloWidth = 2.0f;
        gfx::TextLayoutStyle bryts = st;
        bryts.wrapWidth            = 87.5f;
        gfx::TextLayoutStyle mitt  = st;
        mitt.anchor     = gfx::TextAnchor::Center;
        mitt.padding    = 3.0f;
        mitt.offsetX    = 4.0f;
        mitt.offsetY    = -2.0f;
        mitt.background = gfx::Color{255, 255, 0, 255};
        const std::vector<Fall> fall = {
            {"S5a", {{"AB", svart, gfx::FontStyle::Regular},
                     {"CD", svart, gfx::FontStyle::Bold}}, gloria},
            {"S5c", {{"AAA ", svart, gfx::FontStyle::Regular},
                     {"BBB", svart, gfx::FontStyle::Bold}}, bryts},
            {"S5f", {{"AA", svart, gfx::FontStyle::Regular},
                     {"BB", svart, gfx::FontStyle::Bold}}, st},
            {"Center", {{"AB\nABCD", svart, gfx::FontStyle::Regular},
                        {" EF", svart, gfx::FontStyle::BoldItalic}}, mitt}};
        int         fel = 0;
        std::string fick;
        for (const Fall& f : fall) {
            AtlasRenderer rm, rb;
            rm.perStil = rb.perStil = true;
            gfx::SdfAtlas mätt(rm, gfx::FontFamily::UI), byggd(rb, gfx::FontFamily::UI);
            const gfx::TextMeasure m = gfx::measureText(mätt, f.delar, f.stil);
            std::vector<gfx::GlyphChunk> ch;
            const gfx::TextBox b = gfx::buildText(byggd, f.delar, {0.0, 0.0}, f.stil, ch);
            if (!sammaLåda(m.box, b) || m.lines < 1) {
                ++fel;
                fick += std::string(" ") + f.namn + ": " + area(m.box.x1 - m.box.x0) +
                        " × " + area(m.box.y1 - m.box.y0) + " mot " +
                        area(b.x1 - b.x0) + " × " + area(b.y1 - b.y0);
            }
        }
        check(fel == 0,
              "S6a mätningens låda = byggarens (S5a, S5c, S5f och Center med luft "
              "och förskjutning), inom 1e-5 em",
              fick);
    }

    // S6b — en NY atlas: mätningen skapar ingen sida och laddar inte upp
    // något, och samma glyfer byggs därefter som vanligt (sidan och fälten).
    {
        AtlasRenderer r;
        r.perStil = true;
        gfx::SdfAtlas         atlas(r, gfx::FontFamily::UI);
        const gfx::StyledText delar[] = {{"Hamburg ", svart, gfx::FontStyle::Regular},
                                         {"fonstiv", svart, gfx::FontStyle::Bold}};
        const gfx::TextMeasure m = gfx::measureText(atlas, delar, st);
        const int sidorMätt = r.sidor, upplMätt = r.uppladdningar;
        std::vector<gfx::GlyphChunk> ch;
        gfx::buildText(atlas, delar, {0.0, 0.0}, st, ch);
        check(m.lines == 1 && m.width > 0.0f && sidorMätt == 0 && upplMätt == 0 &&
                  r.sidor == 1 && r.uppladdningar > 0 && atlas.pageCount() == 1,
              "S6b mätningen på en ny atlas: ingen sida och ingen uppladdning — "
              "bygget efteråt laddar upp",
              std::to_string(sidorMätt) + " sidor, " + std::to_string(upplMätt) +
                  " uppladdningar vid mätningen");
    }

    // S6c — RADERNA: S5d:s korpus och bredder — radantalet är
    // `gfx::wrapText`s, bredden den bredaste radens (0,6 em per tecken) och
    // höjden radantalet × 1,25 em.
    {
        AtlasRenderer r;
        gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
        const char* const korpus[] = {"aaa bbbb cc ddddddd e",
                                      "abcdefghijklmnopqrstu",
                                      "ab  cd   ef gh",
                                      " inledande blanksteg",
                                      "rad ett\nrad tva och tre",
                                      "slutar med radbrytning \n",
                                      "x"};
        const float bredder[] = {30.0f, 50.0f, 77.0f, 130.0f};
        int         fel       = 0;
        std::string första;
        for (const char* text : korpus)
            for (float w : bredder) {
                gfx::TextLayoutStyle s = st;
                s.align     = gfx::TextAlign::Left;
                s.wrapWidth = w;
                const gfx::StyledText del{text, svart, gfx::FontStyle::Regular};
                const gfx::TextMeasure m =
                    gfx::measureText(atlas, std::span<const gfx::StyledText>(&del, 1), s);
                const float maxW = w * (gfx::kSdfRefSize / s.size) *
                                   static_cast<float>(gfx::kSdfSupersample);
                const std::string t     = text;
                const auto        rader = gfx::wrapText(*atlas.font(), t, maxW);
                double            facitW = 0.0;
                for (const gfx::WrappedLine& l : rader)
                    facitW = std::max(facitW, 0.6 * static_cast<double>(l.end - l.begin));
                const bool ok = m.lines == static_cast<int>(rader.size()) &&
                                std::abs(m.width - facitW) < 1e-4 &&
                                std::abs(m.height - 1.25 * rader.size()) < 1e-4;
                if (!ok && fel++ == 0)
                    första = std::string("\"") + text + "\" i " + area(w) + ": " +
                             std::to_string(m.lines) + " rader mot " +
                             std::to_string(rader.size());
            }
        check(fel == 0,
              "S6c mätningens rader = gfx::wrapText (28 fall: antalet, bredaste "
              "raden, höjden)",
              fel ? std::to_string(fel) + " fel, först " + första : "");
    }

    // S6d — en TOM text är en rad (bredd 0, höjden en rad), och "a\n\nb" är
    // tre — den tomma raden i mitten räknas.
    {
        AtlasRenderer r;
        gfx::SdfAtlas          atlas(r, gfx::FontFamily::UI);
        const gfx::StyledText  tom{"", svart, gfx::FontStyle::Regular};
        const gfx::StyledText  tre{"a\n\nb", svart, gfx::FontStyle::Regular};
        const gfx::TextMeasure m0 =
            gfx::measureText(atlas, std::span<const gfx::StyledText>(&tom, 1), st);
        const gfx::TextMeasure m3 =
            gfx::measureText(atlas, std::span<const gfx::StyledText>(&tre, 1), st);
        check(m0.lines == 1 && m0.width == 0.0f &&
                  std::abs(m0.height - 1.25f) < 1e-5f && m3.lines == 3 &&
                  std::abs(m3.height - 3.75f) < 1e-5f &&
                  std::abs(m3.width - 0.6f) < 1e-5f,
              "S6d \"\" är en rad med bredden 0, \"a\\n\\nb\" tre rader",
              std::to_string(m0.lines) + " / " + std::to_string(m3.lines));
    }

    // S6e — RADHÖJDEN OCH BASLINJEN är de största bland stilarna: vanlig
    // ensam 1,25 och 0,8 em, med en fet del 1,5 och 0,95 em.
    {
        AtlasRenderer r;
        r.perStil = true;
        gfx::SdfAtlas         atlas(r, gfx::FontFamily::UI);
        const gfx::StyledText vanlig[] = {{"AA", svart, gfx::FontStyle::Regular}};
        const gfx::StyledText blandad[] = {{"AA", svart, gfx::FontStyle::Regular},
                                           {"BB", svart, gfx::FontStyle::Bold}};
        const gfx::TextMeasure mv = gfx::measureText(atlas, vanlig, st);
        const gfx::TextMeasure mb = gfx::measureText(atlas, blandad, st);
        check(std::abs(mv.lineHeight - 1.25f) < 1e-5f &&
                  std::abs(mv.ascent - 0.8f) < 1e-5f &&
                  std::abs(mb.lineHeight - 1.5f) < 1e-5f &&
                  std::abs(mb.ascent - 0.95f) < 1e-5f &&
                  std::abs(mb.width - (2 * 0.6f + 2 * 0.75f)) < 1e-5f,
              "S6e radhöjden och baslinjen = de största bland stilarna (1,25/0,8 "
              "ensam, 1,5/0,95 med en fet del)",
              area(mb.lineHeight) + " / " + area(mb.ascent));
    }

    // S6f — SYNTETISK FET (fonten är vanlig): varje teckensteg 1/24 em bredare, som
    // i byggaren — `advance` räknar inte syntesen, layouten gör det.
    {
        AtlasRenderer r;
        r.fontensStil = gfx::FontStyle::Regular;
        gfx::SdfAtlas        atlas(r, gfx::FontFamily::UI);
        gfx::TextLayoutStyle s = st;
        s.fontStyle            = gfx::FontStyle::Bold;
        const gfx::StyledText del{"AAA", svart, gfx::FontStyle::Bold};
        const gfx::TextMeasure m =
            gfx::measureText(atlas, std::span<const gfx::StyledText>(&del, 1), s);
        std::vector<gfx::GlyphChunk> ch;
        const gfx::TextBox b = gfx::buildText(atlas, "AAA", {0.0, 0.0}, s, ch);
        const double bredd = 3 * (0.6 + 2.0 * gfx::kSyntheticBoldEm);
        check(std::abs(m.width - bredd) < 1e-5 &&
                  std::abs((b.x1 - b.x0) - m.width) < 1e-5,
              "S6f syntetisk fet räknas in, lika med byggaren",
              area(m.width) + " mot " + area(bredd));
    }

    // Storleken 0 (och NaN) mäter ingenting: noll rader.
    {
        AtlasRenderer r;
        gfx::SdfAtlas        atlas(r, gfx::FontFamily::UI);
        gfx::TextLayoutStyle s = st;
        s.size                 = 0.0f;
        const gfx::StyledText del{"AAA", svart, gfx::FontStyle::Regular};
        const gfx::TextMeasure m0 =
            gfx::measureText(atlas, std::span<const gfx::StyledText>(&del, 1), s);
        s.size = std::numeric_limits<float>::quiet_NaN();
        const gfx::TextMeasure mn =
            gfx::measureText(atlas, std::span<const gfx::StyledText>(&del, 1), s);
        check(m0.lines == 0 && m0.width == 0.0f && mn.lines == 0,
              "S6 storleken 0 och NaN: inga rader och inga mått");
    }
}

// ------------------------------------------------------------------ S7
//
// TAKET. Referensen är StegFonts mått: 0,6 em per tecken och
// raden 1,25 em (fet 0,75 och 1,5 med `perStil`); storleken 20 är em.

// Den sista quaden i bygget — dess färg och lägsta atlas-UV (glyfens hörn).
struct SistaQuad {
    bool       finns = false;
    gfx::Color färg{};
    float      u = 0.0f, v = 0.0f;
};
SistaQuad sistaQuad(const std::vector<gfx::GlyphChunk>& ch)
{
    SistaQuad s;
    for (auto it = ch.rbegin(); it != ch.rend() && !s.finns; ++it)
        if (it->vertices.size() >= 4) {
            const std::size_t b = it->vertices.size() - 4;
            s.finns             = true;
            s.färg              = it->vertices[b].color;
            s.u = s.v = 1.0f;
            for (std::size_t k = b; k < b + 4; ++k) {
                s.u = std::min(s.u, it->vertices[k].u);
                s.v = std::min(s.v, it->vertices[k].v);
            }
        }
    return s;
}

void fallTak()
{
    const gfx::Color svart{0, 0, 0, 255}, röd{200, 0, 0, 255};
    gfx::TextLayoutStyle st;
    st.size   = 20.0f;
    st.anchor = gfx::TextAnchor::TopLeft;
    st.align  = gfx::TextAlign::Left;
    const auto en = [](const char* t, gfx::Color c,
                       gfx::FontStyle s = gfx::FontStyle::Regular) {
        return std::vector<gfx::StyledText>{gfx::StyledText{t, c, s}};
    };
    const auto lika = [](gfx::Color a, gfx::Color b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };

    // S7a — två av fem rader: den andra bär ellipsen ("BB…"), och mätningen
    // (ny atlas) ger samma låda som bygget.
    {
        AtlasRenderer rm, rb;
        gfx::SdfAtlas mätt(rm, gfx::FontFamily::UI), byggd(rb, gfx::FontFamily::UI);
        gfx::TextLayoutStyle s = st;
        s.maxLines             = 2;
        const auto delar       = en("AA\nBB\nCC\nDD\nEE", svart);
        const gfx::TextMeasure m = gfx::measureText(mätt, delar, s);
        std::vector<gfx::GlyphChunk> ch;
        const gfx::TextBox b   = gfx::buildText(byggd, delar, {0.0, 0.0}, s, ch);
        const int          förr = rb.uppladdningar;
        const gfx::SdfAtlas::Glyph* ell = byggd.glyph(U'…', gfx::FontStyle::Regular);
        const SistaQuad q = sistaQuad(ch);
        check(m.lines == 2 && m.truncated && quadar(ch) == 5 && ell &&
                  rb.uppladdningar == förr && q.finns && q.u == ell->u0 &&
                  q.v == ell->v0 && sammaLåda(m.box, b) &&
                  std::abs(m.width - 3 * 0.6f) < 1e-5f &&
                  std::abs(m.height - 2 * 1.25f) < 1e-5f,
              "S7a maxLines 2 av fem rader: två rader, kortad, sista quaden är "
              "U+2026 (redan byggd) och mätningens låda = byggarens",
              std::to_string(m.lines) + " rader, " + std::to_string(quadar(ch)) +
                  " quadar, bredd " + area(m.width));
    }

    // S7b — maxHeight 2,5 radhöjder (50 px vid 20) ger två rader; under en
    // radhöjd står ändå en.
    {
        AtlasRenderer r;
        gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
        gfx::TextLayoutStyle s = st;
        s.maxHeight            = 2.5f * 1.25f * 20.0f;
        const auto delar       = en("AA\nBB\nCC\nDD", svart);
        const gfx::TextMeasure m2 = gfx::measureText(atlas, delar, s);
        s.maxHeight               = 0.5f * 1.25f * 20.0f;
        const gfx::TextMeasure m1 = gfx::measureText(atlas, delar, s);
        check(m2.lines == 2 && m2.truncated && m1.lines == 1 && m1.truncated,
              "S7b maxHeight 2,5 rader ger två, en halv rad ger ändå en",
              std::to_string(m2.lines) + " / " + std::to_string(m1.lines));
    }

    // S7c — två delar: ellipsen efter den feta röda delens sista glyf har
    // dess färg och dess glyfnyckel (den feta ellipsen).
    {
        AtlasRenderer r;
        r.perStil = true;
        gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
        gfx::TextLayoutStyle s = st;
        s.maxLines             = 2;
        const std::vector<gfx::StyledText> delar = {
            {"AA\nBB", svart, gfx::FontStyle::Regular},
            {"CC\nDD", röd, gfx::FontStyle::Bold}};
        std::vector<gfx::GlyphChunk> ch;
        gfx::buildText(atlas, delar, {0.0, 0.0}, s, ch);
        const gfx::SdfAtlas::Glyph* fet = atlas.glyph(U'…', gfx::FontStyle::Bold);
        const SistaQuad q = sistaQuad(ch);
        const gfx::TextMeasure m = gfx::measureText(atlas, delar, s);
        check(fet && q.finns && lika(q.färg, röd) && q.u == fet->u0 &&
                  q.v == fet->v0 &&
                  std::abs(m.width - (2 * 0.6f + 2 * 0.75f + 0.75f)) < 1e-5f,
              "S7c ellipsen i den sista delens färg och stil (fet röd, 0,75 em)",
              area(m.width));
    }

    // S7d — "AB \nC" med en rad: blanksteget närmast ellipsen tas bort,
    // bredden är "AB…".
    {
        AtlasRenderer r;
        gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
        gfx::TextLayoutStyle s = st;
        s.maxLines             = 1;
        const gfx::TextMeasure m = gfx::measureText(atlas, en("AB \nC", svart), s);
        check(m.lines == 1 && m.truncated && std::abs(m.width - 3 * 0.6f) < 1e-5f,
              "S7d blanksteget före ellipsen tas bort (\"AB…\", 1,8 em)",
              area(m.width));
    }

    // S7e — en brytbredd på 0,3 em (smalare än ellipsen): en rad som bara är
    // "…" — en kortad etikett syns som kortad.
    {
        AtlasRenderer r;
        gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
        gfx::TextLayoutStyle s = st;
        s.maxLines             = 1;
        s.wrapWidth            = 0.3f * 20.0f;
        const auto delar       = en("ABC", svart);
        const gfx::TextMeasure m = gfx::measureText(atlas, delar, s);
        std::vector<gfx::GlyphChunk> ch;
        gfx::buildText(atlas, delar, {0.0, 0.0}, s, ch);
        const gfx::SdfAtlas::Glyph* ell = atlas.glyph(U'…', gfx::FontStyle::Regular);
        const SistaQuad q = sistaQuad(ch);
        check(m.lines == 1 && m.truncated && std::abs(m.width - 0.6f) < 1e-5f &&
                  quadar(ch) == 1 && ell && q.u == ell->u0 && q.v == ell->v0,
              "S7e brytbredden smalare än ellipsen: raden är bara \"…\"",
              std::to_string(quadar(ch)) + " quadar, bredd " + area(m.width));
    }

    // S7f — utan brytbredd läggs ellipsen efter raden: "a\nb\nc" med två
    // rader är "a" och "b…".
    {
        AtlasRenderer r;
        gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
        gfx::TextLayoutStyle s = st;
        s.maxLines             = 2;
        const gfx::TextMeasure m = gfx::measureText(atlas, en("a\nb\nc", svart), s);
        check(m.lines == 2 && m.truncated && std::abs(m.width - 2 * 0.6f) < 1e-5f,
              "S7f utan brytbredd: \"b…\" (1,2 em) på andra raden", area(m.width));
    }

    // S7g — en text som RYMS: inte kortad, och samma vertex som utan tak.
    {
        AtlasRenderer r;
        gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
        gfx::TextLayoutStyle s = st;
        s.wrapWidth            = 2.0f * 20.0f;
        const auto delar       = en("AAA BBB CC", svart);
        std::vector<gfx::GlyphChunk> utan, med;
        gfx::buildText(atlas, delar, {0.0, 0.0}, s, utan);
        s.maxLines  = 5;
        s.maxHeight = 100.0f * 20.0f;
        gfx::buildText(atlas, delar, {0.0, 0.0}, s, med);
        const gfx::TextMeasure m = gfx::measureText(atlas, delar, s);
        bool samma = utan.size() == med.size();
        for (std::size_t i = 0; samma && i < utan.size(); ++i)
            samma = utan[i].vertices.size() == med[i].vertices.size() &&
                    std::memcmp(utan[i].vertices.data(), med[i].vertices.data(),
                                utan[i].vertices.size() * sizeof(gfx::GlyphVertex)) == 0;
        check(!m.truncated && m.lines == 3 && samma,
              "S7g en text som ryms är inte kortad och får samma vertex som utan tak",
              std::to_string(m.lines) + " rader");
    }

    // S7h — mätningen av en kortad text på en NY atlas laddar inte upp
    // något, inte heller ellipsen.
    {
        AtlasRenderer r;
        gfx::SdfAtlas atlas(r, gfx::FontFamily::UI);
        gfx::TextLayoutStyle s = st;
        s.maxLines             = 1;
        const gfx::TextMeasure m = gfx::measureText(atlas, en("AA\nBB", svart), s);
        check(m.truncated && r.sidor == 0 && r.uppladdningar == 0,
              "S7h mätningen av en kortad text laddar inte upp något",
              std::to_string(r.sidor) + " sidor");
    }
}

// ================================================================= S8
//
// BEHÖVER MÄTNINGEN VÄRLDENS TÄTHET? (build.h
// `measureNeedsWorld`) — referensen är listan över de mått `measureLabel` räknar
// om, skriven för hand: utenheten, storleken, brytbredden, takets höjd,
// golvet och taket. Varje mått prövas ENSAMT i World (och då sant), i px och
// mm (falskt) och med värdet 0 (falskt — ett mått ≤ 0 räknas inte om).
// Glorians bredd är falsk också MED gloria: textblocket mäts utan den
// (`gfx::measureText` läser den aldrig), och ett nullopt för den hade varit
// ett onödigt nej.
void fallBehöverVärlden()
{
    using glaze::Unit;
    using glaze::detail::measureNeedsWorld;
    const glaze::TextStyle bas{.size = 14.0f};   // allt i px
    check(!measureNeedsWorld(bas, {}, Unit::Px) &&
              !measureNeedsWorld(bas, {}, Unit::Mm) &&
              measureNeedsWorld(bas, {}, Unit::World),
          "S8 en text i px: världen behövs bara när måttet ska UT i världen");

    // Ett mått i taget: i World sant, i px, mm och med värdet 0 falskt.
    struct Mått {
        const char*                         namn;
        glaze::Length glaze::TextStyle::*fält;
    };
    const Mått mått[] = {
        {"size", &glaze::TextStyle::size},
        {"wrapWidth", &glaze::TextStyle::wrapWidth},
        {"maxHeight", &glaze::TextStyle::maxHeight},
        {"minSize", &glaze::TextStyle::minSize},
        {"maxSize", &glaze::TextStyle::maxSize},
    };
    for (const Mått& m : mått) {
        glaze::TextStyle iVärlden = bas, iPx = bas, iMm = bas, noll = bas;
        iVärlden.*m.fält = glaze::world(3.0f);
        iPx.*m.fält      = glaze::px(3.0f);
        iMm.*m.fält      = glaze::mm(3.0f);
        noll.*m.fält     = glaze::world(0.0f);
        check(measureNeedsWorld(iVärlden, {}, Unit::Px) &&
                  measureNeedsWorld(iVärlden, {}, Unit::Mm) &&
                  !measureNeedsWorld(iPx, {}, Unit::Px) &&
                  !measureNeedsWorld(iMm, {}, Unit::Mm) &&
                  !measureNeedsWorld(noll, {}, Unit::Px),
              std::string("S8 ") + m.namn +
                  ": i världen sant, i px och mm och med värdet 0 falskt");
    }

    // Glorian: falsk med och utan gloria.
    glaze::TextStyle gloria = bas, utan = bas;
    gloria.halo      = gfx::Color{255, 255, 255, 255};
    gloria.haloWidth = glaze::world(2.0f);
    utan.haloWidth   = glaze::world(2.0f);
    check(!measureNeedsWorld(gloria, {}, Unit::Px) &&
              !measureNeedsWorld(utan, {}, Unit::Px),
          "S8 glorians bredd i världen behöver ingen täthet (mätningen läser "
          "den inte), med och utan gloria");
}

// ================================================================== DEL Q
//
// BILDEN PÅ FYRA FRIA HÖRN — `gfx::quadWeights` och
// byggarens hörn. Referensen är grindens EGEN homografi: DLT med åtta ekvationer
// för världen → bilden, löst med Gauss–Jordan. Vikten i ett hörn är
// homografins NÄMNARE där (u·q och q är affina i världen precis när q är
// nämnaren), och bildpunkten under en världspunkt är homografin själv.

// Homografin världen (x, y) → bilden (u, v), g33 = 1, ur fyra hörn i
// RINGORDNING mot bildens (0, 0), (1, 0), (1, 1), (0, 1).
struct Homografi {
    double g[9] = {};   // radvis
    bool   ok   = false;

    double nämnare(double x, double y) const { return g[6] * x + g[7] * y + g[8]; }
    void   bild(double x, double y, double& u, double& v) const
    {
        const double w = nämnare(x, y);
        u              = (g[0] * x + g[1] * y + g[2]) / w;
        v              = (g[3] * x + g[4] * y + g[5]) / w;
    }
};

Homografi homografi(const gfx::Point2d (&ring)[4])
{
    static const double uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    double a[8][9] = {};
    for (int i = 0; i < 4; ++i) {
        const double x = ring[i].x, y = ring[i].y, u = uv[i][0], v = uv[i][1];
        double* r0 = a[2 * i];
        double* r1 = a[2 * i + 1];
        // g11 x + g12 y + g13 − u (g31 x + g32 y) = u, och likadant för v.
        r0[0] = x;
        r0[1] = y;
        r0[2] = 1.0;
        r0[6] = -u * x;
        r0[7] = -u * y;
        r0[8] = u;
        r1[3] = x;
        r1[4] = y;
        r1[5] = 1.0;
        r1[6] = -v * x;
        r1[7] = -v * y;
        r1[8] = v;
    }
    for (int c = 0; c < 8; ++c) {
        int p = c;
        for (int rr = c + 1; rr < 8; ++rr)
            if (std::abs(a[rr][c]) > std::abs(a[p][c]))
                p = rr;
        if (std::abs(a[p][c]) < 1e-14)
            return Homografi{};
        if (p != c)
            for (int k = 0; k < 9; ++k)
                std::swap(a[p][k], a[c][k]);
        for (int rr = 0; rr < 8; ++rr) {
            if (rr == c)
                continue;
            const double f = a[rr][c] / a[c][c];
            for (int k = c; k < 9; ++k)
                a[rr][k] -= f * a[c][k];
        }
    }
    Homografi h;
    for (int i = 0; i < 8; ++i)
        h.g[i] = a[i][8] / a[i][i];
    h.g[8] = 1.0;
    h.ok   = true;
    return h;
}

// Facits vikter i ringens hörn: nämnaren, normerad så att den största är 1.
void facitVikter(const Homografi& h, const gfx::Point2d (&ring)[4], double (&q)[4])
{
    double n[4], störst = 0.0;
    for (int i = 0; i < 4; ++i) {
        n[i] = h.nämnare(ring[i].x, ring[i].y);
        if (std::abs(n[i]) > std::abs(störst))
            störst = n[i];
    }
    for (int i = 0; i < 4; ++i)
        q[i] = n[i] / störst;
}

void fallFyrhörning()
{
    // Q1 — en REKTANGEL och en PARALLELLOGRAM: fyra ettor (exakt — en
    // rektangel ska rita som rektangelöverlagringen).
    float q[4] = {};
    const gfx::Point2d rekt[4] = {{0, 0}, {40, 0}, {40, 20}, {0, 20}};
    const gfx::Point2d para[4] = {{0, 0}, {40, 0}, {50, 20}, {10, 20}};
    const auto ettor = [&q] {
        return q[0] == 1.0f && q[1] == 1.0f && q[2] == 1.0f && q[3] == 1.0f;
    };
    check(gfx::quadWeights(rekt, q) && ettor(), "Q1 rektangeln: fyra ettor");
    check(gfx::quadWeights(para, q) && ettor(), "Q1 parallellogrammen: fyra ettor");

    // En TRAPETS och en ALLMÄN konvex fyrhörning mot homografin: vikterna,
    // och 25 slumppunkter i var och en av byggarens två trianglar (z-ordning
    // {ÖV, ÖH, NV, NH}, index 0-1-2 och 2-1-3) — Σλ·q·u / Σλ·q ska vara
    // homografins bildpunkt.
    const gfx::Point2d trapets[4] = {{0, 0}, {10, 0}, {8, 6}, {2, 6}};
    const gfx::Point2d allmän[4]  = {{1, 2}, {11, 1}, {9, 8}, {2, 7}};
    std::mt19937                           slump(20261012);
    std::uniform_real_distribution<double> enhet(0.0, 1.0);
    const struct {
        const char*         namn;
        const gfx::Point2d* ring;
    } fyr[] = {{"trapetsen", trapets}, {"den allmänna fyrhörningen", allmän}};
    for (const auto& f : fyr) {
        const gfx::Point2d ring[4] = {f.ring[0], f.ring[1], f.ring[2], f.ring[3]};
        const Homografi    h       = homografi(ring);
        double             facit[4];
        facitVikter(h, ring, facit);
        float      qp[4] = {};
        const bool ok    = gfx::quadWeights(ring, qp);
        double     värst = 0.0;
        for (int i = 0; i < 4; ++i)
            värst = std::max(värst, std::abs(qp[i] - facit[i]));
        check(h.ok && ok && värst < 1e-6,
              std::string("Q1 ") + f.namn +
                  ": vikterna = homografins nämnare i hörnen (normerade)",
              area(värst));
        const gfx::Point2d zc[4] = {ring[0], ring[1], ring[3], ring[2]};
        const double       zu[4] = {0, 1, 0, 1}, zv[4] = {0, 0, 1, 1};
        const double       zq[4] = {qp[0], qp[1], qp[3], qp[2]};
        static const int   tri[2][3] = {{0, 1, 2}, {2, 1, 3}};
        double             värstUv   = 0.0;
        for (const auto& t : tri)
            for (int k = 0; k < 25; ++k) {
                double r1 = enhet(slump), r2 = enhet(slump);
                if (r1 + r2 > 1.0) {
                    r1 = 1.0 - r1;
                    r2 = 1.0 - r2;
                }
                const double l[3] = {1.0 - r1 - r2, r1, r2};
                double px = 0, py = 0, sq = 0, su = 0, sv = 0;
                for (int j = 0; j < 3; ++j) {
                    px += l[j] * zc[t[j]].x;
                    py += l[j] * zc[t[j]].y;
                    sq += l[j] * zq[t[j]];
                    su += l[j] * zq[t[j]] * zu[t[j]];
                    sv += l[j] * zq[t[j]] * zv[t[j]];
                }
                double fu = 0, fv = 0;
                h.bild(px, py, fu, fv);
                värstUv = std::max({värstUv, std::abs(su / sq - fu),
                                    std::abs(sv / sq - fv)});
            }
        check(värstUv < 1e-5,
              std::string("Q1 ") + f.namn +
                  ": 50 punkter i båda trianglarna — Σλqu/Σλq = homografins "
                  "bildpunkt inom 1e-5 (en affin uv hade knäckt vid diagonalen)",
              area(värstUv));
    }

    // Q2 — det som INTE är strikt konvext svarar false och lämnar vikterna.
    const struct {
        const char*  namn;
        gfx::Point2d p[4];
    } ogiltiga[] = {
        {"en fluga (kanterna korsar)", {{0, 0}, {10, 10}, {10, 0}, {0, 10}}},
        {"en inbuktning", {{0, 0}, {10, 0}, {3, 3}, {0, 10}}},
        {"tre hörn i linje", {{0, 0}, {5, 0}, {10, 0}, {0, 10}}},
        {"ingen yta", {{0, 0}, {5, 0}, {10, 0}, {15, 0}}},
        {"ett hörn som inte är ändligt", {{0, 0}, {NAN, 0}, {10, 10}, {0, 10}}},
    };
    for (const auto& f : ogiltiga) {
        float      orörd[4] = {7.0f, 7.0f, 7.0f, 7.0f};
        const bool svar     = gfx::quadWeights(f.p, orörd);
        check(!svar && orörd[0] == 7.0f && orörd[3] == 7.0f,
              std::string("Q2 ") + f.namn + ": false, vikterna orörda");
    }

    // Q3 — SPEGELVÄND ordning (medurs) är också en fyrhörning: samma vikter
    // i de spegelvända platserna.
    {
        double facit[4];
        facitVikter(homografi(allmän), allmän, facit);
        const gfx::Point2d spegel[4] = {allmän[0], allmän[3], allmän[2], allmän[1]};
        float              qs[4]     = {};
        const bool         ok        = gfx::quadWeights(spegel, qs);
        check(ok && std::abs(qs[0] - facit[0]) < 1e-6 &&
                  std::abs(qs[1] - facit[3]) < 1e-6 &&
                  std::abs(qs[2] - facit[2]) < 1e-6 &&
                  std::abs(qs[3] - facit[1]) < 1e-6,
              "Q3 spegelvänd ringordning: true, och hörnen behåller sina vikter");
    }

    // Q4 — BYGGAREN: en form på fyra hörn ger hörnen i Z-ORDNING (ÖV, ÖH, NV,
    // NH), och vertexen bär bildens hörn (0,0), (1,0), (0,1), (1,1) med
    // homografins vikt för just det hörnet. En ogiltig fyrhörning ritas inte.
    {
        double facit[4];
        facitVikter(homografi(allmän), allmän, facit);
        const glaze::Point  p[4] = {{allmän[0].x, allmän[0].y},
                                    {allmän[1].x, allmän[1].y},
                                    {allmän[2].x, allmän[2].y},
                                    {allmän[3].x, allmän[3].y}};
        const glaze::Shape  s    = glaze::Shape::image(p[0], p[1], p[2], p[3],
                                                       glaze::Bitmap(1, 1));
        glaze::Point        z[4];
        glaze::detail::imageCornersOf(s, true, z);
        const bool zOrdning = z[0].x == p[0].x && z[1].x == p[1].x &&
                              z[2].x == p[3].x && z[3].x == p[2].x &&
                              z[2].y == p[3].y && z[3].y == p[2].y;
        std::vector<glaze::detail::ImageChunk> ch;
        glaze::detail::buildImageQuad(z, ch);
        const int    ringAv[4] = {0, 1, 3, 2};   // z-plats → ringens hörn
        const float  fu[4] = {0, 1, 0, 1}, fv[4] = {0, 0, 1, 1};
        bool         vertexOk = ch.size() == 1 && ch[0].vertices.size() == 4;
        double       värst    = 0.0;
        for (int k = 0; vertexOk && k < 4; ++k) {
            const gfx::ImageVertex& v = ch[0].vertices[static_cast<std::size_t>(k)];
            vertexOk = v.u == fu[k] && v.v == fv[k] &&
                       std::abs(ch[0].originX + v.x - p[ringAv[k]].x) < 1e-4 &&
                       std::abs(ch[0].originY + v.y - p[ringAv[k]].y) < 1e-4;
            värst = std::max(värst, std::abs(v.q - facit[ringAv[k]]));
        }
        check(zOrdning && vertexOk && värst < 1e-6 &&
                  glaze::detail::imageQuadOk(s),
              "Q4 byggaren: hörnen i z-ordning {p0, p1, p3, p2}, varje vertex "
              "med bildens hörn och homografins vikt för det hörnet",
              area(värst));
        const glaze::Shape fluga = glaze::Shape::image(
            {0, 0}, {10, 10}, {10, 0}, {0, 10}, glaze::Bitmap(1, 1));
        check(!glaze::detail::imageQuadOk(fluga) &&
                  glaze::detail::partKeys(fluga).empty() &&
                  glaze::detail::imageQuadOk(
                      glaze::Shape::image({0, 0, 4, 4}, glaze::Bitmap(1, 1))),
              "Q4 en fluga har inga delar (ritas inte); en rektangel är alltid "
              "ok");
    }
}

// ================================================================== DEL 3D

constexpr double kPi = 3.14159265358979323846;

// Grindens EGEN vektorräkning — ingenting ur geom3d.cpp.
struct P3 {
    double x = 0.0, y = 0.0, z = 0.0;
};
P3 operator+(P3 a, P3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
P3 operator-(P3 a, P3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
P3 operator*(P3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double skalär(P3 a, P3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
P3     kryss(P3 a, P3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double     längd(P3 a) { return std::sqrt(skalär(a, a)); }
P3         enhet(P3 a) { return a * (1.0 / längd(a)); }
P3         p3(const gfx::Vec3d& v) { return {v.x, v.y, v.z}; }
gfx::Vec3d v3(P3 p) { return {p.x, p.y, p.z}; }

// Rodrigues: v vriden vinkeln a kring enhetsaxeln k.
P3 vrid(P3 v, P3 k, double a)
{
    return v * std::cos(a) + kryss(k, v) * std::sin(a) +
           k * (skalär(k, v) * (1.0 - std::cos(a)));
}

std::string tal(double v)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

P3 hörn(const gfx::MeshGeometry& g, std::size_t t, int k)
{
    return p3(g.positions[g.indices[3 * t + static_cast<std::size_t>(k)]]);
}

// Den TECKNADE volymen (divergenssatsen) relativt första läget — positiv när
// en sluten yta har framsidan utåt, och oberoende av referenspunkten.
double volym(const gfx::MeshGeometry& g)
{
    if (g.positions.empty())
        return 0.0;
    const P3 r = p3(g.positions[0]);
    double   v = 0.0;
    for (std::size_t t = 0; t < g.indices.size() / 3; ++t)
        v += skalär(hörn(g, t, 0) - r,
                    kryss(hörn(g, t, 1) - r, hörn(g, t, 2) - r));
    return v / 6.0;
}

double yta(const gfx::MeshGeometry& g)
{
    double s = 0.0;
    for (std::size_t t = 0; t < g.indices.size() / 3; ++t)
        s += längd(kryss(hörn(g, t, 1) - hörn(g, t, 0),
                         hörn(g, t, 2) - hörn(g, t, 0))) / 2.0;
    return s;
}

// SLUTEN OCH KONSEKVENT VÄND: lägena SVETSAS på exakt lika värden (en spricka
// på en avrundning är en spricka på GPU:n också), och varje riktad kant
// u → v finns exakt en gång och dess motsats v → u exakt en gång. Ingen
// triangel får ha två hörn i samma punkt.
bool sluten(const gfx::MeshGeometry& g, std::string* fick = nullptr)
{
    std::map<std::tuple<double, double, double>, int> id;
    std::vector<int>                                  w(g.positions.size());
    for (std::size_t i = 0; i < g.positions.size(); ++i) {
        const auto& p = g.positions[i];
        w[i] = id.emplace(std::make_tuple(p.x, p.y, p.z),
                          static_cast<int>(id.size()))
                   .first->second;
    }
    std::map<std::pair<int, int>, int> kanter;
    for (std::size_t t = 0; t + 2 < g.indices.size(); t += 3) {
        const int a = w[g.indices[t]], b = w[g.indices[t + 1]],
                  c = w[g.indices[t + 2]];
        if (a == b || b == c || a == c) {
            if (fick)
                *fick = "triangel " + std::to_string(t / 3) +
                        " har två hörn i samma punkt";
            return false;
        }
        ++kanter[{a, b}];
        ++kanter[{b, c}];
        ++kanter[{c, a}];
    }
    for (const auto& [k, n] : kanter) {
        const auto mot = kanter.find({k.second, k.first});
        if (n != 1 || mot == kanter.end() || mot->second != 1) {
            if (fick)
                *fick = "kanten " + std::to_string(k.first) + " → " +
                        std::to_string(k.second) + " finns " +
                        std::to_string(n) + " gång(er), motsatsen " +
                        std::to_string(mot == kanter.end() ? 0 : mot->second);
            return false;
        }
    }
    return !kanter.empty();
}

// En ENHETSNORMAL per läge, indexen inom lägena — och varje triangels
// vindning, (b − a) × (c − a), åt samma håll som normalen i alla tre hörnen.
bool normalerOchVindning(const gfx::MeshGeometry& g)
{
    if (g.normals.size() != g.positions.size() || g.indices.size() % 3 != 0)
        return false;
    for (const gfx::Vec3d& n : g.normals)
        if (std::abs(längd(p3(n)) - 1.0) > 1e-12)
            return false;
    for (std::uint32_t i : g.indices)
        if (i >= g.positions.size())
            return false;
    for (std::size_t t = 0; t < g.indices.size() / 3; ++t) {
        const P3 f = kryss(hörn(g, t, 1) - hörn(g, t, 0),
                           hörn(g, t, 2) - hörn(g, t, 0));
        for (std::size_t k = 0; k < 3; ++k)
            if (skalär(f, p3(g.normals[g.indices[3 * t + k]])) <= 0.0)
                return false;
    }
    return true;
}

// ------------------------------------------------------------------ L1
void fallLådan()
{
    gfx::MeshGeometry g;
    gfx::box({4, 6, 8}, {1, 2, 3}, g);   // hörnen omkastade per axel
    check(g.positions.size() == 24 && g.normals.size() == 24 &&
              g.indices.size() == 36,
          "L1 lådan: 24 lägen (fyra per sida), 12 trianglar",
          std::to_string(g.positions.size()) + " lägen, " +
              std::to_string(g.indices.size()) + " index");
    check(std::abs(volym(g) - 60.0) < 1e-9,
          "L1 volymen 3 · 4 · 5 = 60 — tecknad, alltså framsidorna utåt",
          tal(volym(g)));
    check(std::abs(yta(g) - 94.0) < 1e-9, "L1 ytan 2 · (12 + 15 + 20) = 94",
          tal(yta(g)));
    std::string varför;
    check(sluten(g, &varför), "L1 sluten och konsekvent vänd", varför);
    const P3 mitt{2.5, 4.0, 5.5};
    bool     axlar = normalerOchVindning(g);
    for (std::size_t i = 0; axlar && i < g.normals.size(); ++i) {
        const P3  n      = p3(g.normals[i]);
        const int ettor  = (std::abs(n.x) == 1.0) + (std::abs(n.y) == 1.0) +
                           (std::abs(n.z) == 1.0);
        const int nollor = (n.x == 0.0) + (n.y == 0.0) + (n.z == 0.0);
        axlar = ettor == 1 && nollor == 2 &&
                skalär(n, p3(g.positions[i]) - mitt) > 0.0;
    }
    check(axlar, "L1 varje normal längs en axel och utåt, och vindningen "
                 "följer normalerna");

    gfx::MeshGeometry s;
    gfx::box({674000.0, 6580000.0, 10.0}, {674003.0, 6580004.0, 15.0}, s);
    check(std::abs(volym(s) - 60.0) < 1e-9 * 60.0 && sluten(s),
          "L1 på SWEREF: volymen 60 och sluten", tal(volym(s)));

    gfx::MeshGeometry två;
    gfx::box({0, 0, 0}, {1, 1, 1}, två);
    gfx::box({5, 0, 0}, {6, 1, 1}, två);
    const bool fler = två.indices.size() == 72;
    const auto mm   = fler ? std::minmax_element(två.indices.begin() + 36,
                                                 två.indices.end())
                           : std::make_pair(två.indices.end(), två.indices.end());
    check(fler && två.positions.size() == 48 && *mm.first == 24 &&
              *mm.second == 47 && std::abs(volym(två) - 2.0) < 1e-12 &&
              sluten(två),
          "L1 två lådor i EN mesh: den andras index förskjutna 24, volymen 2, "
          "sluten");

    gfx::MeshGeometry ingen;
    gfx::box({std::nan(""), 0, 0}, {1, 1, 1}, ingen);
    check(ingen.positions.empty() && ingen.indices.empty(),
          "L1 ett hörn som inte är ändligt ger ingenting");
}

// ------------------------------------------------------------------ S1
void fallSfären()
{
    const P3     c{1.0, 2.0, 3.0};
    const double r = 2.5;
    gfx::MeshGeometry g;
    gfx::sphere(v3(c), r, g, 24, 16);
    check(g.positions.size() == 362 && g.indices.size() == 3 * 720,
          "S1 UV-sfären 24 × 16: 2 + 24 · 15 = 362 lägen, 2 · 24 · 15 = 720 "
          "trianglar",
          std::to_string(g.positions.size()) + " lägen, " +
              std::to_string(g.indices.size() / 3) + " trianglar");
    double värstR = 0.0, värstN = 0.0;
    for (std::size_t i = 0; i < g.positions.size() && i < g.normals.size(); ++i) {
        const P3 p = p3(g.positions[i]) - c;
        värstR     = std::max(värstR, std::abs(längd(p) - r));
        värstN = std::max(värstN, längd(p3(g.normals[i]) - p * (1.0 / r)));
    }
    check(värstR < 1e-12 && värstN < 1e-12,
          "S1 varje läge på radien och normalen radiell utåt",
          tal(värstR) + " / " + tal(värstN));
    std::string varför;
    check(sluten(g, &varför) && normalerOchVindning(g),
          "S1 sluten och vänd utåt", varför);
    const double V = 4.0 / 3.0 * kPi * r * r * r, v = volym(g);
    check(v > 0.95 * V && v < V,
          "S1 volymen är den inskrivna polyederns: 0,95 V < v < V",
          tal(v) + " av " + tal(V));
    const bool poler = !g.positions.empty();
    const P3   nord  = poler ? p3(g.positions.front()) - c : P3{};
    const P3   syd   = poler ? p3(g.positions.back()) - c : P3{};
    check(poler && nord.x == 0.0 && nord.y == 0.0 && nord.z == r &&
              syd.x == 0.0 && syd.y == 0.0 && syd.z == -r,
          "S1 polerna på ±z: läge 0 i norr, det sista i söder");

    gfx::MeshGeometry m;
    gfx::sphere({0, 0, 0}, 1.0, m, 2, 1);
    check(m.positions.size() == 5 && m.indices.size() == 18 && sluten(m) &&
              std::abs(volym(m) - std::sqrt(3.0) / 2.0) < 1e-12,
          "S1 för få meridianer och breddgrader klampas till 3 och 2: en "
          "dubbelpyramid med volymen √3/2",
          tal(volym(m)));
    gfx::MeshGeometry z;
    gfx::sphere({0, 0, 0}, 0.0, z);
    check(z.positions.empty(), "S1 radien 0 ger ingenting");
}

// ------------------------------------------------------------------ C1
void fallCylindern()
{
    const P3     b{10.0, 20.0, 0.0}, t{13.0, 24.0, 12.0};
    const double r = 0.5;
    const int    s = 16;
    gfx::MeshGeometry g;
    gfx::cylinder(v3(b), v3(t), r, g, s);
    check(g.positions.size() == 64 && g.indices.size() == 3 * 60,
          "C1 16 sidor: 4 · 16 = 64 lägen, 4 · 16 − 4 = 60 trianglar",
          std::to_string(g.positions.size()) + " lägen, " +
              std::to_string(g.indices.size() / 3) + " trianglar");
    const double h = 13.0, A = s / 2.0 * r * r * std::sin(2.0 * kPi / s);
    check(std::abs(volym(g) - A * h) < 1e-12 * A * h,
          "C1 volymen = sextonhörningens area × höjden 13 — sned axel",
          tal(volym(g)) + " mot " + tal(A * h));
    std::string varför;
    check(sluten(g, &varför) && normalerOchVindning(g),
          "C1 sluten och vänd utåt", varför);
    const P3 w      = (t - b) * (1.0 / h);
    int      mantel = 0, lock = 0;
    bool     rätt   = g.normals.size() == g.positions.size();
    for (std::size_t i = 0; rätt && i < g.positions.size(); ++i) {
        const P3     n = p3(g.normals[i]), q = p3(g.positions[i]) - b;
        const double nw = skalär(n, w), längs = skalär(q, w);
        if (std::abs(nw) < 1e-12) {
            const P3 radiell = q - w * längs;
            rätt = skalär(n, radiell) > 0.0 &&
                   std::abs(längd(radiell) - r) < 1e-12;
            ++mantel;
        } else if (std::abs(std::abs(nw) - 1.0) < 1e-12) {
            rätt = (nw > 0.0) == (längs > h / 2.0);
            ++lock;
        } else {
            rätt = false;
        }
    }
    check(rätt && mantel == 2 * s && lock == 2 * s,
          "C1 manteln radiellt utåt på radien, locken längs axeln — botten "
          "bakåt, toppen framåt");

    gfx::MeshGeometry v;
    gfx::cylinder({0, 0, 0}, {0, 0, 2}, 1.0, v, 4);
    check(!v.positions.empty() && v.positions[0].x == 1.0 &&
              v.positions[0].y == 0.0 && v.positions[0].z == 0.0 &&
              std::abs(volym(v) - 4.0) < 1e-12,
          "C1 lodrät och fyrsidig: läge 0 österut (+x), volymen 2 · 2 = 4",
          tal(volym(v)));
    gfx::MeshGeometry z;
    gfx::cylinder({1, 1, 1}, {1, 1, 1}, 1.0, z);
    check(z.positions.empty(), "C1 en axel utan längd ger ingenting");
}

// ------------------------------------------------------------------ R1–R6
double hörning(double r, int s)
{
    return s / 2.0 * r * r * std::sin(2.0 * kPi / s);
}

double väglängd(const std::vector<gfx::Vec3d>& p)
{
    double L = 0.0;
    for (std::size_t i = 0; i + 1 < p.size(); ++i)
        L += längd(p3(p[i + 1]) - p3(p[i]));
    return L;
}

void fallRöret()
{
    const double dia = 0.018, r = dia / 2.0;   // istjockleken klass A, 18 mm
    const double A   = hörning(r, 8);
    std::string  varför;

    gfx::MeshGeometry rak;
    gfx::tube(std::vector<gfx::Vec3d>{{0, 0, 0}, {10, 0, 0}}, dia, rak);
    check(rak.positions.size() == 32 && rak.indices.size() == 3 * 28,
          "R1 rakt rör, 8 sidor: (2 + 2) · 8 = 32 lägen, 2 · 8 + 2 · 6 = 28 "
          "trianglar",
          std::to_string(rak.positions.size()) + " lägen, " +
              std::to_string(rak.indices.size() / 3) + " trianglar");
    check(std::abs(volym(rak) - A * 10.0) < 1e-9 * A * 10.0,
          "R1 volymen = åttahörningens area × längden",
          tal(volym(rak)) + " mot " + tal(A * 10.0));
    check(sluten(rak, &varför) && normalerOchVindning(rak),
          "R1 slutet och vänt utåt", varför);

    // KEDJEKURVAN: 51 punkter, nedhänget som en parabel i ett snett lodplan.
    std::vector<gfx::Vec3d> lina;
    const P3     dir = enhet({3.0, 4.0, 0.0});
    const double L = 80.0, nedhäng = 2.5;
    for (int k = 0; k <= 50; ++k) {
        const double s = L * k / 50.0;
        const double z = 10.0 - 4.0 * nedhäng * (s / L) * (1.0 - s / L);
        lina.push_back(v3({s * dir.x, s * dir.y, z}));
    }
    gfx::MeshGeometry c;
    gfx::tube(lina, dia, c);
    check(c.positions.size() == 424 && c.indices.size() == 3 * 812,
          "R2 linan med 51 punkter: 53 · 8 = 424 lägen, 2 · 8 · 50 + 12 = 812 "
          "trianglar",
          std::to_string(c.positions.size()) + " lägen, " +
              std::to_string(c.indices.size() / 3) + " trianglar");
    const double facit = A * väglängd(lina);
    check(std::abs(volym(c) - facit) < 1e-9 * facit,
          "R2 volymen = arean × VÄGENS längd: geringen skär varje segment så "
          "att prismat behåller sin volym",
          tal(volym(c)) + " mot " + tal(facit));
    check(sluten(c, &varför) && normalerOchVindning(c),
          "R2 slutet och vänt utåt", varför);
    // Varje läge mot sin närmaste vägpunkt: i ändarna en rät ring på radien,
    // i ett knä halveringsplanet och radien från BÅDA segmentens linjer.
    double värst = 0.0;
    for (const gfx::Vec3d& pv : c.positions) {
        const P3    p    = p3(pv);
        std::size_t j    = 0;
        double      bäst = std::numeric_limits<double>::infinity();
        for (std::size_t k = 0; k < lina.size(); ++k)
            if (längd(p - p3(lina[k])) < bäst) {
                bäst = längd(p - p3(lina[k]));
                j    = k;
            }
        const P3 q = p - p3(lina[j]);
        if (j == 0 || j + 1 == lina.size()) {
            const P3 d = j == 0 ? enhet(p3(lina[1]) - p3(lina[0]))
                                : enhet(p3(lina[j]) - p3(lina[j - 1]));
            värst      = std::max({värst, std::abs(skalär(q, d)),
                                   std::abs(längd(q) - r)});
        } else {
            const P3 din  = enhet(p3(lina[j]) - p3(lina[j - 1]));
            const P3 dout = enhet(p3(lina[j + 1]) - p3(lina[j]));
            const P3 n    = enhet(din + dout);
            värst = std::max(
                {värst, std::abs(skalär(q, n)),
                 std::abs(längd(q - din * skalär(q, din)) - r),
                 std::abs(längd(q - dout * skalär(q, dout)) - r)});
        }
    }
    check(värst < 1e-12,
          "R2 varje knäs ring i knäets halveringsplan och på radien från BÅDA "
          "segmentens linjer; ändarnas ringar räta",
          tal(värst));

    gfx::MeshGeometry knä;
    gfx::tube(std::vector<gfx::Vec3d>{{0, 0, 0}, {5, 0, 0}, {5, 5, 0}}, dia, knä);
    check(knä.positions.size() == 40 &&
              std::abs(volym(knä) - A * 10.0) < 1e-9 * A * 10.0 && sluten(knä),
          "R3 rät vinkel (geringen √2): 40 lägen, volymen = arean × 10, slutet",
          tal(volym(knä)));

    gfx::MeshGeometry skarp;
    gfx::tube(std::vector<gfx::Vec3d>{{0, 0, 0}, {10, 0, 0}, {0, 1, 0}}, dia,
              skarp);
    const double skarpL = 10.0 + std::sqrt(101.0);
    check(skarp.positions.size() == 64 && skarp.indices.size() == 3 * 56 &&
              std::abs(volym(skarp) - A * skarpL) < 1e-9 * A * skarpL &&
              sluten(skarp, &varför),
          "R4 ett knä skarpare än spetsgränsen (≈ 174°): två slutna bitar, 64 "
          "lägen, volymen = arean × båda längderna",
          std::to_string(skarp.positions.size()) + " lägen, " + varför);

    gfx::MeshGeometry ren;
    gfx::tube(std::vector<gfx::Vec3d>{{0, 0, 0}, {0, 0, 0}, {std::nan(""), 0, 0},
                                      {10, 0, 0}, {10, 0, 0}},
              dia, ren);
    check(ren.positions.size() == 32 &&
              std::abs(volym(ren) - A * 10.0) < 1e-9 * A * 10.0,
          "R5 upprepade och icke-ändliga punkter hoppas över");

    gfx::MeshGeometry inget;
    gfx::tube(std::vector<gfx::Vec3d>{{0, 0, 0}, {1, 0, 0}}, 0.0, inget);
    gfx::tube(std::vector<gfx::Vec3d>{{0, 0, 0}}, dia, inget);
    check(inget.positions.empty(),
          "R6 diametern 0 och en enda punkt ger ingenting");
}

// ------------------------------------------------------------------ P1–P5
using Ringar3 = std::vector<std::vector<gfx::Vec3d>>;

// Ringarna i planet: o + R · (x, y, 0), R = vridningen kring `axel`.
Ringar3 iPlanet(const Ringar& r, P3 o, P3 axel, double vinkel)
{
    Ringar3 ut;
    for (const auto& ring : r) {
        std::vector<gfx::Vec3d> q;
        for (const gfx::Point2d& p : ring)
            q.push_back(v3(o + vrid({p.x, p.y, 0.0}, axel, vinkel)));
        ut.push_back(std::move(q));
    }
    return ut;
}

// Ingen triangel MEDURS kring n (en utan area räknas inte).
bool motursKring(const gfx::MeshGeometry& g, P3 n)
{
    for (std::size_t t = 0; t < g.indices.size() / 3; ++t)
        if (skalär(kryss(hörn(g, t, 1) - hörn(g, t, 0),
                         hörn(g, t, 2) - hörn(g, t, 0)),
                   n) < -1e-9)
            return false;
    return true;
}

bool normalerLika(const gfx::MeshGeometry& g, P3 n, double tol = 1e-12)
{
    if (g.normals.size() != g.positions.size() || g.normals.empty())
        return false;
    for (const gfx::Vec3d& m : g.normals)
        if (längd(p3(m) - n) > tol)
            return false;
    return true;
}

bool urIndatan(const gfx::MeshGeometry& g, const Ringar3& r)
{
    for (const gfx::Vec3d& p : g.positions) {
        bool finns = false;
        for (const auto& ring : r)
            for (const gfx::Vec3d& q : ring)
                finns = finns || (p.x == q.x && p.y == q.y && p.z == q.z);
        if (!finns)
            return false;
    }
    return !g.positions.empty();
}

void fallPolygonIPlanet()
{
    const P3     axel   = enhet({1.0, 2.0, 3.0});
    const double vinkel = 0.65;
    const P3     o{674100.0, 6580200.0, 45.0};
    const P3     n = vrid({0.0, 0.0, 1.0}, axel, vinkel);

    const Ringar  hål = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}},
                         {{3, 3}, {7, 3}, {7, 7}, {3, 7}}};
    const Ringar3 r3  = iPlanet(hål, o, axel, vinkel);
    // Indatans punkter på SWEREF är själva avrundade till ≈ 1e-9 m (ulp vid
    // 6,58·10⁶), så normalen ur dem kan inte vara exakt bättre än ≈ 1e-10 på
    // en polygon om 10 m — toleransen är indatans, inte normalens.
    const double kSwerefTol = 1e-9;
    gfx::MeshGeometry g;
    const bool        ok = gfx::triangulatePlanar(r3, g);
    check(ok && std::abs(yta(g) - 84.0) < 1e-9 * 84.0,
          "P1 kvadraten med ett hål, roterad in i ett LUTAT plan på SWEREF: "
          "arean 84",
          tal(yta(g)));
    check(g.positions.size() == 8 && normalerLika(g, n, kSwerefTol),
          "P1 åtta lägen med planets normal (R · ẑ — ytterringen går moturs "
          "kring den)");
    check(motursKring(g, n), "P1 varje triangel går moturs kring normalen");
    check(urIndatan(g, r3), "P1 lägena är indatans punkter, exakt");

    Ringar omvänd = hål;
    std::reverse(omvänd[0].begin(), omvänd[0].end());
    gfx::MeshGeometry gv;
    gfx::triangulatePlanar(iPlanet(omvänd, o, axel, vinkel), gv);
    check(std::abs(yta(gv) - 84.0) < 1e-9 * 84.0 &&
              normalerLika(gv, n * -1.0, kSwerefTol) &&
              motursKring(gv, n * -1.0),
          "P1 ytterringen medurs: normalen vänd och trianglarna moturs kring den");
    Ringar hålOmvänt = hål;
    std::reverse(hålOmvänt[1].begin(), hålOmvänt[1].end());
    gfx::MeshGeometry gh;
    gfx::triangulatePlanar(iPlanet(hålOmvänt, o, axel, vinkel), gh);
    check(std::abs(yta(gh) - 84.0) < 1e-9 * 84.0 && normalerLika(gh, n, kSwerefTol),
          "P1 hålet åt andra hållet: samma yta och samma normal");

    const Ringar L = {{{0, 0}, {3, 0}, {3, 1}, {1, 1}, {1, 3}, {0, 3}}};
    gfx::MeshGeometry gl;
    const bool okL =
        gfx::triangulatePlanar(iPlanet(L, {}, {1.0, 0.0, 0.0}, kPi / 2.0), gl);
    check(okL && std::abs(yta(gl) - 5.0) < 1e-12 &&
              normalerLika(gl, {0.0, -1.0, 0.0}) &&
              motursKring(gl, {0.0, -1.0, 0.0}),
          "P1 L-formen i det lodräta planet x–z: arean 5, normalen −y",
          tal(yta(gl)));

    // P2: slumpstjärnor i slumpade plan, var tredje på SWEREF.
    std::mt19937 rg(3141u);
    int          fel = 0, falska = 0;
    double       värst = 0.0;
    for (int k = 0; k < 50; ++k) {
        const int    pn = 5 + static_cast<int>(slump(rg) * 120.0);
        const double R  = 1.0 + slump(rg) * 30.0;
        Ringar       ytan  = {stjärna(rg, 0.0, 0.0, R, pn, 0.3)};
        double       facit = skosnöre(ytan[0]);
        if (pn >= 12 && k % 2 == 0) {
            ytan.push_back(stjärna(rg, 0.0, 0.0, 0.06 * R,
                                   5 + static_cast<int>(slump(rg) * 20.0), 0.5));
            facit -= skosnöre(ytan.back());
        }
        const P3 ax = enhet({slump(rg) - 0.5, slump(rg) - 0.5, slump(rg) - 0.5});
        const double vk = slump(rg) * kPi;
        const P3     oo = k % 3 == 0
                              ? P3{674000.0 + slump(rg) * 1000.0,
                                   6580000.0 + slump(rg) * 1000.0, slump(rg) * 100.0}
                              : P3{slump(rg) * 50.0, slump(rg) * 50.0, slump(rg) * 50.0};
        gfx::MeshGeometry gg;
        const bool okk = gfx::triangulatePlanar(iPlanet(ytan, oo, ax, vk), gg);
        const P3   nn  = vrid({0.0, 0.0, 1.0}, ax, vk);   // stjärnan går moturs
        const double rel = std::abs(yta(gg) - facit) / facit;
        värst            = std::max(värst, rel);
        if (!okk)
            ++falska;
        if (rel > 1e-6 || !motursKring(gg, nn) || !normalerLika(gg, nn, 1e-9))
            ++fel;
    }
    check(fel == 0,
          "P2 50 slumpstjärnor (hälften med hål) i slumpade plan: arean mot "
          "grindens skosnöre, planets normal, trianglarna moturs kring den",
          std::to_string(fel) + " fel, värst " + tal(värst));
    check(falska == 0, "P2 triangulatePlanar svarar true för varje stjärna",
          std::to_string(falska) + " falska");

    // P3: det degenererade.
    gfx::MeshGeometry d1, d2, d3;
    const bool okLinje = gfx::triangulatePlanar(
        Ringar3{{{0, 0, 0}, {1, 1, 1}, {2, 2, 2}, {3, 3, 3}}}, d1);
    check(okLinje && d1.positions.empty(),
          "P3 en ytterring på en linje: ingenting, och inget fel");
    const bool okFå =
        gfx::triangulatePlanar(Ringar3{{{0, 0, 0}, {1, 0, 0}, {1, 0, 0}}}, d2);
    check(okFå && d2.positions.empty(),
          "P3 färre än tre punkter efter rensningen: ingenting");
    const bool okNaN = gfx::triangulatePlanar(
        Ringar3{{{0, 0, 0}, {std::nan(""), 0, 0}, {4, 0, 0}, {4, 4, 0},
                 {0, 4, 0}, {0, 0, 0}}},
        d3);
    check(okNaN && d3.positions.size() == 4 && std::abs(yta(d3) - 16.0) < 1e-12,
          "P3 en punkt som inte är ändlig och en upprepad slutpunkt hoppas över");

    // P4: en självkorsning — flugan har ingen tecknad area men ett plan.
    gfx::MeshGeometry f;
    const bool        okF = gfx::triangulatePlanar(
        iPlanet({{{0, 0}, {4, 4}, {4, 0}, {0, 4}}}, o, axel, vinkel), f);
    check(!okF && !f.indices.empty(),
          "P4 en självkorsning (fluga) i ett lutat plan: false, och ändå "
          "trianglar");
    // Framsidan är godtycklig (ingen tecknad area), men lägena bär planets
    // normal åt ena hållet och VARJE triangel går moturs kring den — också de
    // som öronklippningens reserv lämnar medurs. Enkla ytor mäter inte
    // vändningen: dem lämnar öronklippningen redan moturs, så sabotaget
    // "trianglarna inte vända efter planet" fälldes inte av P1–P2.
    const P3 nf = f.normals.empty() ? P3{} : p3(f.normals.front());
    check((normalerLika(f, n, kSwerefTol) ||
           normalerLika(f, n * -1.0, kSwerefTol)) &&
              motursKring(f, nf),
          "P4 flugan: lägena bär planets normal (åt ena hållet), och varje "
          "triangel går moturs kring den");

    // P5: brus längs normalen — lägena står kvar där de är.
    Ringar3 brus = iPlanet({{{0, 0}, {10, 0}, {10, 10}, {0, 10}}}, o, axel, vinkel);
    for (std::size_t k = 0; k < brus[0].size(); ++k)
        brus[0][k] = v3(p3(brus[0][k]) + n * (k % 2 ? 1e-7 : -1e-7));
    gfx::MeshGeometry b;
    gfx::triangulatePlanar(brus, b);
    check(urIndatan(b, brus) && std::abs(yta(b) - 100.0) < 1e-6,
          "P5 brus längs normalen (±0,1 µm): lägena står kvar där de är, arean "
          "≈ 100",
          tal(yta(b)));
}

// ------------------------------------------------------------------ M1–M5
using glaze::detail::MeshAccess;
using glaze::detail::MeshData;
using glaze::detail::MeshSegment;

const MeshData& data(const glaze::Mesh& m)
{
    return *MeshAccess::data(m);
}

// Grindens EGNA snitt: nivån [first, first + count) (klampad) i segmentets
// lokala element.
glaze::IndexRange snitt(const MeshSegment& s, std::uint32_t first,
                        std::uint32_t count, std::uint32_t element)
{
    const std::uint64_t slut = std::min<std::uint64_t>(
        std::uint64_t{first} + std::min(count, element - first), element);
    const std::uint64_t lo = std::max<std::uint64_t>(first, s.firstElement);
    const std::uint64_t hi =
        std::min<std::uint64_t>(slut, std::uint64_t{s.firstElement} + s.elementCount);
    if (lo >= hi)
        return {0, 0};
    return {static_cast<std::uint32_t>(lo - s.firstElement),
            static_cast<std::uint32_t>(hi - lo)};
}

// Segmentens nivåer mot grindens snitt av meshens ORIGINALNIVÅER.
bool nivåernaOmmappade(const std::vector<MeshSegment>& seg,
                       const std::vector<glaze::Lod>& orig, std::uint32_t element)
{
    for (const MeshSegment& s : seg) {
        if (s.lods.size() != orig.size())
            return false;
        for (std::size_t k = 0; k < orig.size(); ++k) {
            const glaze::IndexRange f = snitt(s, orig[k].first, orig[k].count, element);
            if (s.lods[k].first != f.first || s.lods[k].count != f.count ||
                s.lods[k].maxDistance != orig[k].maxDistance)
                return false;
        }
    }
    return true;
}

// Segmenten i följd: varje originalelement exakt en gång.
bool iFöljd(const std::vector<MeshSegment>& seg, std::uint32_t element)
{
    std::uint32_t nästa = 0;
    for (const MeshSegment& s : seg) {
        if (s.firstElement != nästa || s.elementCount == 0 ||
            s.elementCount % 3 != 0)
            return false;
        nästa += s.elementCount;
    }
    return nästa == element;
}

std::vector<glaze::Float3> slumplägen(std::mt19937& g, std::size_t n)
{
    std::vector<glaze::Float3> p(n);
    for (glaze::Float3& q : p)
        q = {static_cast<float>(slump(g) * 100.0),
             static_cast<float>(slump(g) * 100.0),
             static_cast<float>(slump(g) * 100.0)};
    return p;
}

void fallSegmentenUtanIndex()
{
    std::mt19937     g(64u);
    glaze::MeshDesc  d;
    d.positions = slumplägen(g, 300);   // 100 trianglar
    d.lods      = {{0, 90, 5.0}, {90, 120, 10.0}, {210, glaze::kRangeToEnd, 0.0}};
    const std::vector<glaze::Lod> orig = d.lods;
    const glaze::Mesh m(std::move(d));
    check(m.valid() && data(m).segments.size() == 1 &&
              data(m).segments[0].elementCount == 300,
          "M1 en liten mesh är ETT segment");
    if (!m.valid())
        return;
    check(data(m).lods[2].count == 90,
          "M1 en nivå till slutet lagras med sitt klampade antal (90)");
    const auto seg = glaze::detail::segmentMesh(data(m), 12 * 30);   // 30 lägen
    bool fulla = seg.size() == 10;
    for (const MeshSegment& s : seg)
        fulla = fulla && s.elementCount == 30 && s.vertices.empty() &&
                s.indices.empty() && s.firstTriangle() * 3 == s.firstElement;
    check(fulla && iFöljd(seg, 300),
          "M1 utan index på triangelgränser: tio segment om 30 lägen i följd, "
          "tabellen tillbaka = första elementet / 3",
          std::to_string(seg.size()) + " segment");
    check(nivåernaOmmappade(seg, orig, 300),
          "M1 nivåerna ommappade till varje segments lokala element (tomma där "
          "en nivå inte når)");
    const auto en = glaze::detail::segmentMesh(data(m), 1);
    check(en.size() == 100 && iFöljd(en, 300),
          "M1 en gräns mindre än en triangel: en triangel per segment");
}

// Originalets triangel t (globala lägen) ur segmentet — grindens egen väg:
// lokala index → `vertices` → globala.
bool återskapad(const MeshData& d, const MeshSegment& s)
{
    for (std::uint32_t e = 0; e < s.elementCount; ++e) {
        const std::uint32_t lokal = s.indices[e];
        if (lokal >= s.vertices.size() ||
            s.vertices[lokal] != d.indices[s.firstElement + e])
            return false;
    }
    return true;
}

void fallSegmentenMedIndex()
{
    std::mt19937    g(20261011u);
    glaze::MeshDesc d;
    d.positions = slumplägen(g, 1000);
    d.colors.assign(1000, gfx::Color{1, 2, 3, 255});   // 16 byte per läge
    for (int t = 0; t < 3000; ++t)
        for (int k = 0; k < 3; ++k)
            d.indices.push_back(static_cast<std::uint32_t>(slump(g) * 1000.0));
    d.lods = {{0, 3000, 10.0}, {3000, 3000, 20.0}, {6000, glaze::kRangeToEnd, 0.0}};
    const std::vector<glaze::Lod> orig = d.lods;
    const glaze::Mesh m(std::move(d));
    const bool        giltig = m.valid();
    check(giltig && data(m).segments.size() == 1 &&
              data(m).segments[0].vertices.empty() &&
              data(m).segments[0].indices.empty(),
          "M2 en indexerad mesh som ryms är ETT odelat segment (meshens egna "
          "lägen och index)");
    if (!giltig)
        return;
    const MeshData& md = data(m);
    check(glaze::detail::vertexBytes(md) == 16, "M2 lägen + färger = 16 byte");
    const auto seg = glaze::detail::segmentMesh(md, 16 * 200);   // 200 lägen
    bool form = seg.size() > 1;
    for (const MeshSegment& s : seg) {
        form = form && s.vertices.size() <= 200 && s.indices.size() == s.elementCount &&
               s.elementCount <= 800 &&
               std::adjacent_find(s.vertices.begin(), s.vertices.end(),
                                  std::greater_equal<>()) == s.vertices.end();
        std::vector<bool> används(s.vertices.size(), false);
        for (std::uint32_t i : s.indices)
            if (i < används.size())
                används[i] = true;
        form = form && std::all_of(används.begin(), används.end(),
                                   [](bool b) { return b; });
    }
    check(form && iFöljd(seg, 9000),
          "M2 med index: varje segment ≤ 200 lägen, lokala lägen växande och "
          "använda, segmenten i följd",
          std::to_string(seg.size()) + " segment");
    bool alla = true;
    for (const MeshSegment& s : seg)
        alla = alla && återskapad(md, s);
    check(alla, "M2 varje originaltriangel återskapad ur segmentets LOKALA "
                "index — tabellen tillbaka");
    // FULLT: nästa triangel hade inte rymts (för många nya lägen).
    bool fullt = true;
    for (std::size_t k = 0; k + 1 < seg.size(); ++k) {
        std::vector<std::uint32_t> v = seg[k].vertices;
        const std::uint32_t        e = seg[k + 1].firstElement;
        for (std::uint32_t j = 0; j < 3; ++j)
            v.push_back(md.indices[e + j]);
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
        fullt = fullt && v.size() > 200;
    }
    check(fullt, "M2 varje segment utom det sista är FULLT: nästa triangel "
                 "hade fört det över 200 lägen");
    check(nivåernaOmmappade(seg, orig, 9000),
          "M2 nivåerna ommappade till varje segments lokala index");
}

void fallSegmentensIndexgräns()
{
    // 10 × 10 punkter, 9 × 9 rutor = 162 trianglar (486 index) med stor
    // återanvändning: alla 100 lägen ryms, indexen inte.
    glaze::MeshDesc d;
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            d.positions.push_back({static_cast<float>(x), static_cast<float>(y), 0.0f});
    for (std::uint32_t y = 0; y < 9; ++y)
        for (std::uint32_t x = 0; x < 9; ++x) {
            const std::uint32_t a = y * 10 + x;
            d.indices.insert(d.indices.end(), {a, a + 1, a + 11, a, a + 11, a + 10});
        }
    const glaze::Mesh m(std::move(d));
    if (!m.valid()) {
        check(false, "M3 rutnätet är en giltig mesh");
        return;
    }
    const auto seg = glaze::detail::segmentMesh(data(m), 1200);   // 100 lägen, 300 index
    check(seg.size() == 2 && seg[0].elementCount == 300 &&
              seg[1].elementCount == 186 && iFöljd(seg, 486) &&
              återskapad(data(m), seg[0]) && återskapad(data(m), seg[1]),
          "M3 INDEXGRÄNSEN: 486 index med gränsen 300 blir 300 + 186, varje "
          "triangel återskapad",
          std::to_string(seg.size()) + " segment");
}

void fallSegmentgränsen64()
{
    check(glaze::detail::kMeshSegmentBytes == std::size_t{64} * 1024 * 1024,
          "M4 segmentgränsen är 64 MiB");
    // 5 592 408 lägen à 12 byte = 64 MiB + 36 byte: två segment, det första
    // med ⌊64 MiB / 12⌋ = 5 592 405 lägen (hela trianglar).
    glaze::MeshDesc d;
    d.positions.resize(5592408);
    for (std::size_t i = 0; i < d.positions.size(); ++i)
        d.positions[i] = {static_cast<float>(i % 4096),
                          static_cast<float>((i / 4096) % 4096),
                          static_cast<float>(i / 16777216)};
    const glaze::Mesh m(std::move(d));
    check(m.valid() && data(m).segments.size() == 2 &&
              data(m).segments[0].elementCount == 5592405 &&
              data(m).segments[1].firstElement == 5592405 &&
              data(m).segments[1].elementCount == 3,
          "M4 64 MiB + 36 byte lägen: två segment, 5 592 405 + 3 lägen",
          m.valid() ? std::to_string(data(m).segments.size()) + " segment"
                    : "ingen mesh");

    glaze::MeshDesc a;
    a.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    a.normals   = a.positions;
    a.colors.assign(3, gfx::Color{});
    a.uvs.assign(3, {0.0f, 0.0f});
    a.scalars.assign(3, 0.0f);
    for (auto& u : a.user)
        u.assign(3, {0.0f, 0.0f, 0.0f, 0.0f});
    const glaze::Mesh alla(std::move(a));
    check(alla.valid() && glaze::detail::vertexBytes(data(alla)) == 104,
          "M4 alla strömmar: 12 + 12 + 4 + 8 + 4 + 4 · 16 = 104 byte per läge");
}

void fallMeshDatan()
{
    const auto giltig = [](glaze::MeshDesc d) { return glaze::Mesh(std::move(d)).valid(); };
    glaze::MeshDesc tri;
    tri.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    check(giltig(tri), "M5 en triangel är en mesh");
    glaze::MeshDesc a = tri;
    a.indices         = {0, 1, 3};
    glaze::MeshDesc b = tri;
    b.colors.assign(2, gfx::Color{});
    glaze::MeshDesc c = tri;
    c.positions[1].y  = std::numeric_limits<float>::quiet_NaN();
    glaze::MeshDesc e = tri;
    e.positions.push_back({1, 1, 0});
    glaze::MeshDesc f = tri;
    f.indices         = {0, 1};
    glaze::MeshDesc tom;
    check(!giltig(a) && !giltig(b) && !giltig(c) && !giltig(e) && !giltig(f) &&
              !giltig(tom),
          "M5 avslagen: index utanför, ström med fel längd, NaN, lägen och index "
          "som inte är hela trianglar, och tomt");
    glaze::MeshDesc l1 = tri, l2 = tri, l3 = tri, l4 = tri, l5 = tri;
    l1.lods = {{1, 3, 0.0}};                    // inte en hel triangel
    l2.lods = {{0, 3, 20.0}, {0, 3, 10.0}};     // avståndet växer inte
    l3.lods = {{0, 3, 0.0}, {0, 3, 10.0}};      // obegränsad nivå inte sist
    l4.lods = {{0, 3, -1.0}};                   // negativt
    l5.lods = {{0, 3, 10.0}, {0, 3, 0.0}};      // rätt
    check(!giltig(l1) && !giltig(l2) && !giltig(l3) && !giltig(l4) && giltig(l5),
          "M5 nivåerna: hela trianglar, växande avstånd, 0 bara sist, inte "
          "negativt");

    // fromWorld: origot i lådans mitt.
    const std::vector<glaze::Point3> pv = {{674000.0, 6580000.0, 10.0},
                                           {674010.0, 6580000.0, 10.0},
                                           {674000.0, 6580020.0, 30.0}};
    const glaze::Mesh w = glaze::Mesh::fromWorld(pv);
    const bool        wOk = w.valid();
    check(wOk && data(w).origin.x == 674005.0 && data(w).origin.y == 6580010.0 &&
              data(w).origin.z == 20.0 && data(w).positions[0].x == -5.0f &&
              data(w).positions[2].z == 10.0f && w.bounds().min.x == 674000.0 &&
              w.bounds().max.y == 6580020.0 && w.vertexCount() == 3 &&
              w.triangleCount() == 1,
          "M5 fromWorld: origot i lådans mitt, lägena relativt det, lådan i "
          "världen");
    const glaze::Mesh lit = glaze::Mesh::triangles(pv, gfx::Color{9, 8, 7, 6});
    check(lit.valid() && data(lit).colors.size() == 3 &&
              data(lit).colors[2].a == 6 &&
              !glaze::Mesh::indexed(pv, {}, gfx::Color{}).valid(),
          "M5 literalformerna: färgen i varje läge, och indexed utan index är "
          "tom");

    // ÄNDRINGSLOGGEN.
    glaze::MeshDesc ld = tri;
    ld.positions.insert(ld.positions.end(), {{5, 5, 5}, {6, 5, 5}, {5, 6, 5}});
    ld.colors.assign(6, gfx::Color{0, 0, 0, 255});
    glaze::Mesh lm(std::move(ld));
    if (!lm.valid()) {
        check(false, "M5 loggens mesh är giltig");
        return;
    }
    const MeshData&            md  = data(lm);
    const glaze::Float3        två[] = {{7, 7, 7}, {8, 7, 7}};
    const gfx::Color           röd[] = {{255, 0, 0, 255}};
    lm.setPositions(3, två);
    check(md.version == 1 && md.changes.size() == 1 &&
              md.changes[0].stream == glaze::detail::MeshStream::Positions &&
              md.changes[0].first == 3 && md.changes[0].count == 2 &&
              md.positions[4].x == 8.0f && lm.bounds().max.x == 8.0,
          "M5 setPositions skriver, antecknar intervallet och räknar om lådan");
    lm.setColors(5, röd);
    lm.setPositions(5, två);   // utanför (5 + 2 > 6)
    lm.setColors(0, std::span<const gfx::Color>{});   // tomt
    check(md.version == 2 && md.changes.size() == 2 && md.colors[5].r == 255,
          "M5 setColors antecknar; utanför och tomt skriver ingenting");
    std::vector<glaze::Float3> hela(md.positions.begin(), md.positions.end());
    lm.setPositions(0, hela);
    check(md.changes.size() == 2 &&
              md.changes[0].stream == glaze::detail::MeshStream::Colors &&
              md.changes[1].count == 6,
          "M5 en ändring av HELA lägena gör äldre ändringar av lägena "
          "överflödiga — färgernas står kvar");
    for (int k = 0; k < 40; ++k)
        lm.setColors(static_cast<std::size_t>(k % 6), röd);
    check(md.changes.size() <= glaze::kMaxMeshChanges && md.base > 0 &&
              md.changes.front().version > md.base &&
              md.changes.back().version == md.version,
          "M5 loggens tak (32): den äldre halvan ges upp och basen flyttar");
    glaze::MeshDesc ufärg = tri;
    glaze::Mesh     uf(std::move(ufärg));
    uf.setColors(0, röd);
    check(uf.valid() && data(uf).version == 0,
          "M5 setColors i en mesh utan färgström skriver ingenting");
}

// ------------------------------------------------------------------ K1
void fallPunktmolnet()
{
    using glaze::detail::PointCloudData;
    std::mt19937 g(65536u);

    std::vector<glaze::Float3> lokal(1000);
    for (glaze::Float3& p : lokal)
        p = {static_cast<float>(slump(g) * 10.0), static_cast<float>(slump(g) * 10.0),
             static_cast<float>(slump(g) * 10.0)};
    const glaze::Point3 o{674000.0, 6580000.0, 0.0};
    const glaze::PointCloud liten(lokal, o);
    const PointCloudData*   ld = MeshAccess::data(liten).get();
    double                  fel = 0.0;
    if (ld)
        for (std::size_t i = 0; i < lokal.size(); ++i) {
            const glaze::Point3& c = ld->chunks[0].origin;
            fel = std::max({fel,
                            std::abs(c.x + ld->positions[i].x - (o.x + lokal[i].x)),
                            std::abs(c.y + ld->positions[i].y - (o.y + lokal[i].y)),
                            std::abs(c.z + ld->positions[i].z - (o.z + lokal[i].z))});
        }
    check(ld && ld->chunks.size() == 1 && ld->order.empty() && fel < 1e-5 &&
              liten.pointCount() == 1000,
          "K1 1 000 punkter är EN chunk i indatans ordning, origot i lådan",
          tal(fel));

    // 200 000 blandade punkter i en låda 1 000 × 600 × 50 m på SWEREF.
    const std::size_t          n = 200000;
    std::vector<glaze::Point3> pv(n);
    std::vector<gfx::Color>    färg(n);
    std::vector<float>         skal(n);
    for (std::size_t i = 0; i < n; ++i) {
        pv[i]   = {674000.0 + slump(g) * 1000.0, 6580000.0 + slump(g) * 600.0,
                   slump(g) * 50.0};
        färg[i] = {static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i >> 8),
                   static_cast<std::uint8_t>(i >> 16), 255};
        skal[i] = static_cast<float>(i);
    }
    const glaze::PointCloud stor = glaze::PointCloud::fromWorld(pv, färg, skal);
    const PointCloudData*   sd   = MeshAccess::data(stor).get();
    if (!sd) {
        check(false, "K1 det stora molnet är giltigt");
        return;
    }
    bool chunkar = sd->chunks.size() == 4 && sd->order.size() == n;
    std::uint32_t nästa = 0;
    for (const glaze::detail::PointChunk& c : sd->chunks) {
        chunkar = chunkar && c.first == nästa && c.count == 50000 &&
                  std::is_sorted(sd->order.begin() + c.first,
                                 sd->order.begin() + c.first + c.count);
        nästa += c.count;
    }
    std::vector<std::uint32_t> perm = sd->order;
    std::sort(perm.begin(), perm.end());
    std::vector<std::uint32_t> iota(n);
    std::iota(iota.begin(), iota.end(), 0u);
    check(chunkar && perm == iota,
          "K1 200 000 punkter: fyra chunkar om 50 000, ordningen en permutation "
          "och växande inom varje chunk",
          std::to_string(sd->chunks.size()) + " chunkar");
    double värst = 0.0;
    bool   följer = true, inne = true;
    for (std::size_t ci = 0; ci < sd->chunks.size(); ++ci) {
        const glaze::detail::PointChunk& c = sd->chunks[ci];
        for (std::uint32_t i = c.first; i < c.first + c.count; ++i) {
            const glaze::Point3& q   = pv[sd->order[i]];
            const glaze::Point3  åter{c.origin.x + sd->positions[i].x,
                                     c.origin.y + sd->positions[i].y,
                                     c.origin.z + sd->positions[i].z};
            värst = std::max({värst, std::abs(åter.x - q.x), std::abs(åter.y - q.y),
                              std::abs(åter.z - q.z)});
            följer = följer && sd->colors[i].r == färg[sd->order[i]].r &&
                     sd->colors[i].g == färg[sd->order[i]].g &&
                     sd->scalars[i] == skal[sd->order[i]];
            inne = inne && q.x >= c.bounds.min.x && q.x <= c.bounds.max.x &&
                   q.y >= c.bounds.min.y && q.y <= c.bounds.max.y;
        }
    }
    check(värst < 1e-4 && följer && inne,
          "K1 varje punkt återfunnen genom ordningen (läge inom 1e-4 m, färg, "
          "skalär) och inne i sin chunks låda",
          tal(värst));
    const auto& ch = sd->chunks;
    check(std::max(ch[0].bounds.max.x, ch[1].bounds.max.x) <=
              std::min(ch[2].bounds.min.x, ch[3].bounds.min.x),
          "K1 delningen är RUMSLIG: första delningen längs den längsta axeln "
          "(x, 1 000 m) skiljer chunkarna 0–1 från 2–3");

    const glaze::PointCloud fel1(lokal, o, std::vector<gfx::Color>(3));
    const glaze::PointCloud fel2 = glaze::PointCloud::fromWorld(
        std::vector<glaze::Point3>{{0, 0, 0}, {std::nan(""), 0, 0}});
    const glaze::PointCloud tom(std::vector<glaze::Float3>{}, o);
    check(!fel1.valid() && !fel2.valid() && !tom.valid(),
          "K1 avslagen: färger med fel längd, en punkt som inte är ändlig, och "
          "tomt");
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== geom_gate — geometribyggarna (linjer, ytor, text och 3D) ===\n");
    fallBitarna();
    fallRensningen();
    fallChunkgränsen();
    fallTillägg();
    fallSkarvslagen();
    fallÄndarna();
    fallSluten();
    fallStreckmönstret();
    fallYtansArea();
    fallSlumpytor();
    fallDegenererat();
    fallOgiltigt();
    fallRemsansRiktning();
    fallYtornasChunkar();
    fallSdfVärden();
    fallTextlayout();
    fallSyntes();
    fallMarkörer();
    fallTextdelar();
    fallMätning();
    fallTak();
    fallBehöverVärlden();
    fallFyrhörning();
    fallLådan();
    fallSfären();
    fallCylindern();
    fallRöret();
    fallPolygonIPlanet();
    fallSegmentenUtanIndex();
    fallSegmentenMedIndex();
    fallSegmentensIndexgräns();
    fallSegmentgränsen64();
    fallMeshDatan();
    fallPunktmolnet();
    std::printf(g_fel ? "=== geom_gate: %d FEL av %d ===\n"
                      : "=== geom_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    return g_fel ? 1 : 0;
}
