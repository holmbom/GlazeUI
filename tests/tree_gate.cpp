// tree_gate — TRÄDETS grind.
//
// Kör:  ./build/tests/tree_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//
// TVÅDELAD, som dnd_gate:
//
//   HEADLESS     kollektionsvokabulären med STIGAR (`addNode`/`setNode`/
//                `setNodes`/`removeNode`/`moveNode` — FÖRE-flytten-
//                koordinater och mål inuti noden själv), att VALET FÖLJER
//                NODEN och inte positionen, `setProp("selectedPath")`,
//                kontraktet ANVÄNDARE mot PROGRAM: tangenterna fyrar exakt ett
//                event per handling, settrarna aldrig något — och SLÄPPREGELN
//                PER NOD: `setAllowDropInto`, att den följer noden
//                och står kvar genom `setNode`, och massformens femte fält.
//   MED FÖNSTER  ett DOLT fönster utan chrome, matat genom fönstrets RIKTIGA
//                ingång (`AppWindow::handleEvent`, protected):
//                pilen fäller utan att välja, raden väljer, en ihopfällning
//                som gömmer det valda flyttar valet — och DRAG-AND-DROP:
//                den inbyggda lasten (nodens stig + text, som dnd_gate
//                uttryckligen lämnar hit), omordning FÖRE/EFTER/IN I, ett
//                mål inuti den dragna noden som avvisas, Escape, en flytt
//                till ETT ANNAT mål som tar bort noden, en kopia som lämnar
//                den, att ett tryck på pilen aldrig blir ett drag,
//                DUBBELKLICKET (fäller + aktiverar, på pilen bara fäller) — och
//                SLÄPPREGELN i draget: en nod som säger nej har ingen mittzon
//                (före/efter delar raden), "efter" under en sådan utfälld nod
//                är platsen efter den, och före/efter ett av dess barn finns
//                inte (index −1, avvisat).
//
// FLERVAL, KRYSS, FÄRG OCH BILD (fall 19–30):
//
//   19  flerval AV: Ctrl-/Shift-klick är vanliga klick, Ctrl+pil, Ctrl+A och
//       Ctrl+Space bubblar, Shift+pil navigerar
//   20  musen i flerval: Ctrl vänder, Shift ger spannet i SYNLIG ordning (en
//       stängd grens barn ingår inte), Ctrl+Shift lägger till — exakt ett
//       `onChange` per ändrande klick
//   21  tangenterna: Shift+Ner, Ctrl+Ner (den aktuella flyttar, valet står
//       kvar), Ctrl+Space, Ctrl+A (de synliga), Retur aktiverar den aktuella
//   22  programmet fyrar aldrig; `setMultiSelect(false)` behåller en nod;
//       `selectedPath`/`selectedPaths`/`currentPath`s regler
//   23  ihopfällning i flerval: ättlingarna lämnar valet, noden väljs, den
//       aktuella flyttar, ett `onChange` efter `onCollapse`
//   24  valet följer noderna genom `moveNode`; `setNodes` nollar
//   25  tryck på en vald nod: inget vid trycket, solo + ett `onChange` vid
//       släppet; en modifierad tryckning vänder direkt
//   26  kryssen: zonen vänder utan att flytta valet (`onCheck` + `onCommit`
//       med stigen, också efter en handler som fyllde om), ett lövs
//       pilkolumn är ingen zon, Space (också i enkelval) och Ctrl+Space
//       (bara flerval), `setChecked` fyrar inget, ingen kaskad, `setNode`
//       lämnar krysset, det följer noden, dubbelklick i rutan, inget drag
//   27  massformen = de platta satserna för kryss och färg, nod för nod
//   28  färgen per UI: texten i appens färg, pilen och bocken i temats, vald
//       rad i markeringens, inaktivt i `textDim`, `setNode` nollar
//   29  drag av flera: lasten "a;b" i trädordning, in i en mapp i trädordning,
//       beskärningen (ett valt barn följer sin förälder), ett mål inuti
//       avvisas, "efter" en av de dragna ger samma träd, en toppnod in i en
//       SENARE toppnod, `Move` till ett annat mål tar bort alla
//   30  en Bitmap: `nodeImg` ger den tillbaka, och den ritas EN gång i
//       `iconFallback`-rutan — efter kryssrutan när den syns — och tonas
//       inaktiv
//
// APPENS GODKÄNNANDE AV DRAGET (fall 31–38):
//
//   31  `draggedPaths()` i trädordning i appens `onDragStart` (sessionen är
//       Pending), `onDragOver` och `onDrop`, SORTERAD efter att
//       appen flyttat en dragen nod; tom i `onDragEnd`, utanför draget och
//       direkt efter en start som appen avböjde
//   32  inträdesluckan: den FÖRSTA platsen är en verklig flytt, och en app med
//       bara `onDragOver` nekar den redan där — släppet rör inte trädet
//   33  `onDrop` ser det OFLYTTADE trädet och nekar; `Copy` flyttar inget
//   34  appen utför släppet själv (`moveNodes` + `handled`), ingen andra flytt
//   35  den publika `moveNodes` (headless): atomisk, beskuren, trädordning,
//       FÖRE-flytten-index, en toppnod in i en senare toppnod
//   36  föräldern hålls vid sitt id genom en handler som flyttar en annan
//       nod; 36b LUCKAN: en nod insatt FÖRE luckan flyttar platsen
//       men inte luckan, en nod insatt I luckan ger None, och en dragen nod
//       som handlern tog bort ger None
//   37  `checkedPaths()` (headless) i trädordning, också hopfällda grenar
//   38  `canDragFrom`: med en app som FYLLER lasten startar inget drag från
//       pilen eller rutan, men från texten
//
// SABOTAGEN för 31–38 (skrivna):
//   * kroken före `onDrop` (33 — handlern ser "C,A,B" — och 34)
//   * `handled` ignorerad (34: en andra flytt ger "A,C,B")
//   * inget `onDragOver` efter enter (32: släppet flyttar B in i C)
//   * föräldern hållen som STIG, eller platsen omräknad ur pekaren efter
//     handlern (36: C hamnar inte under B)
//   * luckan hållen som INDEX i stället för grannarnas id (36b: C hamnar på
//     index 1 i båda delfallen)
//   * `draggedPaths` med `isDragging()`-vakten (31: tom i `onDragStart`),
//     utan vakt (31b: kvar efter den avböjda starten) eller osorterad (31)
//   * en publik `moveNodes` som muterar före valideringen (35)
//   * pilens/zonens kontroll kvar bara i `dragStart` (38: appens last drar)
//   * `checkedPaths` utan de dolda noderna (37)
//   * en förlorad dragen nod som lämnar `Move` (36b iii)
//
// GEOMETRIN räknas här ur samma Style-mått som trädet (`listRowH`,
// `listPadX`, `treeIndent`, `treeExpanderW`, `treeDropEdge`, och för kryssen
// `checkSize`/`checkGap`/`iconFallback`) och trädets publika `innerBounds` —
// aldrig ur trädets `checkRect`/`contentX`. Grinden mäter ROUTINGEN och
// BETEENDET, och en glidning i radgeometrin syns som klick som landar fel
// och fall som faller. Ritkontrollerna (28, 30) går mot en INSPELANDE
// renderer med en falsk font och, för bilden, en riktig bildcache.
//
// SABOTAGEN den fäller (prövade i en privat kopia):
//   * `onChange` fyrad av den programmatiska `setSelectedPath` (fall 6)
//   * `moveNode` utan FÖRE-flytten-justeringen inom samma förälder (fall 4 —
//     och därefter KRASCHAR fall 3, rc 139: det ojusterade indexet hamnar
//     bortom vektorns slut, vilket är exakt vad justeringen finns för)
//   * ett släpp INUTI den dragna noden accepterat (fall 13)
//   * `dragEnd` som inte tar bort noden vid en flytt till ett annat mål
//     (fall 14)
//   * valet lagrat som POSITION i stället för id (fall 3)
//   * `setNode` som behåller den gamla bilden (fall 2)
//   * ett dubbelklick på PILEN som också aktiverar och fäller en tredje
//     gång (fall 16 — utan `onExpander`-frågan i `mouseDoubleClick`)
//   * Retur som inte aktiverar (fall 7)
//   * SLÄPPREGELN: mittzonen kvar trots regeln (fall
//     18), föräldern som aldrig frågas (18b), massformens femte fält tappat
//     (17 — och preview_gate), "efter" under en utfälld nod som säger nej
//     som ändå går in bland barnen (18b)
//
// SABOTAGEN för fall 19–30 (skrivna):
//   * spannet räknat i TRÄDORDNING över hela trädet i stället för de
//     synliga raderna (20 — A1/A2 kommer med)
//   * Ctrl+Ner som också väljer (21)
//   * `onChange` fyrat av `setSelected` (22)
//   * dolda ättlingar kvar som valda efter en ihopfällning (23)
//   * valet satt redan på trycket, utan väntande soloval (25 och 29a — bara
//     den tryckta noden dras)
//   * krysset som kaskaderar till barnen (26), `onCheck` fyrat av
//     `setChecked` (26), zonens träff räknad som allt till vänster om
//     `contentX` (26 — ett lövs pilkolumn kryssar)
//   * fält 6 eller 7 tappat i `fromPublic` (27)
//   * appens färg som vinner på en vald rad (28), färgen ignorerad (28),
//     bocken ritad i appens färg (28)
//   * ingen beskärning av ättlingar i lasten (29b — lasten blir "0;0,0;1")
//   * indexet som inte justeras före uttaget (29c — C hamnar först)
//   * målet hållet som STIG i stället för id över uttaget (29d)
//
// BREDD EFTER INNEHÅLLET i trädet (W1–W7). Sabotage som ska fälla dem:
// `cellContentW` som
// mäter ALLA noder i stället för de synliga (W3: en gömd nod växer kolumnen) ·
// indraget, pilens ruta, rutan eller ikonen glömda i kolumn 0 (W1, W4) · en
// cache som inte släpps vid `rowsChanged`/`setNode`/`setCell`/`setShowChecks`
// (W3–W5) · cellerna i kolumn 1… som läser fel index (W2, W5) · det strikta
// andrummet (en hårlinje) som saknas (alla W) · dubbelklicket som inte
// skriver en FAST bredd eller inte avslutar draget (W6, W7). Mäts inte: ett
// dubbelklick i ett RIKTIGT fönster (W6 anropar krokarna direkt, som
// listview_gate Q5) och hur kolumnen SER UT.
//
// VAD DEN INTE MÄTER, skrivet så anspråket inte överdrivs:
//   * hur trädet SER UT — raderna, pilen, insättningslinjen och ramen. Att
//     det RITAR mäter `pixel_gate` (fixturen har ett träd); utseendet är
//     verifierat renderat i alla tre UI × ljust/mörkt × av/på.
//   * att insättningsmarkeringen ritas om vid VARJE ny plats — bara att
//     platsen (`dropParent`/`dropIndex`) följer pekaren. Att markeringen
//     ALDRIG ramar in en nod som säger nej mäts på samma väg: ramen ("in i")
//     sätts bara i grenen som gör föräldern till raden själv, och fall 18
//     ser att föräldern aldrig blir den noden.
//   * hjul under ett drag. AUTOSKROLLEN (Q8–Q9) mäts
//     med ett injicerat tick och en riktig pump — men inte hur takten KÄNNS,
//     eller att backendens timer håller perioden (Q9 mäter bara att den går).
//
// SNABBMENYN OCH AUTOSKROLLEN (Q1–Q9, sist i
// fönsterkedjan): Q1 `pathAt` (rad, nedanför, ram, utan fönster), Q2 ett
// högertryck på en ovald nod väljer den ensam (ett onChange + onCommit, menyn
// med trädet som sender och noden i `contextPath()`), Q3 en vald nod blir
// bara aktuell, Q4 tom yta {}, Q5 en onChange som bygger om trädet ger {}, Q6
// inaktivt träd ger {} i förfaderns handler, Q7 fokus står kvar, Q8 ett
// injicerat tick rullar ett steg och gör målvalet om (mitten och slutet
// rullar inte), Q9 riktig pump. SABOTAGEN (skrivna): ingen
// `startTimer` (Q9), tick utan `dragMotion` (Q8 platsen och onDragOver),
// ingen klampning (Q8 slutet), `rightDown` som svarar true (Q2 — ingen meny),
// val också på en vald nod (Q3), `contextPath` som stig och inte id (Q5),
// ingen vakt för inaktiv (Q6), fokusflytt (Q7).
//   * hur kryssrutan och fokusramen runt den aktuella
//     raden SER UT (pixel_gate mäter bara att något ritas), en Bitmap genom
//     en riktig backend (bilden kan inte stå i `.glazeui`; backendvägen är
//     den befintliga `createImage` + `drawImage`), att dragbilden inte visar
//     antalet, Cmd på macOS och Waylands modifierare (doc/os/*/TESTS.md) och
//     kontrasten i appens färger, som är appens ansvar.
//   * förbudsmärket och den uteblivna linjen som PIXLAR
//     (overlayn läses inte av `captureFrame`; grinden mäter `dropEffect`, som
//     linjen ritas ur), en modal dialog i `onDrop`,
//     och att en apps regel är rimlig.
//
// KOLUMNERNA (K1–K7 headless, KM1–KM4 sist i fönsterkedjan): K1
// cellerna ur massformens åttonde fält, `cell` (kolumn 0 = nodens text) och
// `setCell` (utfyllnad, ingen kolumn krävs, kolumn 0 och ogiltig stig no-op),
// K2 `setNode` som tilldelning av cellerna, K3–K7 SORTERINGEN INOM SYSKON
// läst genom tangenterna (Home, Ner) som STIGAR — appens ordning utan
// sortering, naturlig ordning (9 < 12 < 40), fallande, kolumn 0 på texten,
// en ändrad cell som sorteras om, en kolumn utanför och setProp-vägen. KM1
// huvudet (ingen rad under det, raderna under det, inget huvud utan
// kolumner), KM2 klicket i rubriken som sorterar och vänder utan att välja,
// KM3 avdelardraget (bredden, inget drag-and-drop ur huvudet), KM4 huvudet
// bort med kolumnerna. SABOTAGEN (skrivna): `rows()` utan
// sortering (K4–K7, KM2), visningsplatsen som stig (K4), en vanlig
// strängjämförelse i stället för `compareNatural` (K4), `fromPublic` som
// tappar cellerna (K1), `setNode` som lämnar dem (K2), `hasHeader` alltid
// sann (KM1, KM4), `mouseDown` utan `headerMouseDown` (KM2/KM3), `mouseUp`
// utan basens släpp (KM2), `canDragFrom` utan huvudet (KM3 — ett drag
// startar). Grinden mäter INTE hur huvudet och cellerna SER UT (pixlar),
// ellipsen i kolumn 0, den horisontella scrollen eller pekarformen över
// avdelaren — den delade koden är tabellens och mäts i listview_gate.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gfx/null_renderer.h>

#include <glaze/glaze.h>

#include "drag_scroll.h"   // intern — autoskrollens timer-id (Q8)
#include "image_cache.h"   // intern — bilden ur pixlar behöver en riktig
                           // bildcache (som i toolbar_gate)

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
    // Tömd DIREKT: ett sabotage som får en senare rad att krascha ska inte
    // ta felraden med sig i en buffrad stdout (se fall 4 i filhuvudet).
    std::fflush(stdout);
    ++g_fails;
}

std::string stig(const glaze::TreePath& p)
{
    std::string s = "{";
    for (std::size_t i = 0; i < p.size(); ++i)
        s += (i ? "," : "") + std::to_string(p[i]);
    return s + "}";
}

// Trädet med räknare på eventen och de skyddade krokarna åtkomliga.
// `onCheck`/`onCommit` räknas för sig och står INTE i
// `logg` — de äldre fallens förväntade ordning är skriven utan dem.
struct Trad : glaze::TreeView {
    using glaze::TreeView::keyDown;
    using glaze::TreeView::dropEffect;
    using glaze::TreeView::draw;
    // Bredden efter innehållet och dubbelklicket på en avdelare.
    using glaze::ColumnPanel::columnW;
    using glaze::ColumnPanel::mouseDrag;
    using glaze::TreeView::mouseDown;
    using glaze::TreeView::mouseUp;
    using glaze::TreeView::mouseDoubleClick;

    int val = 0, ut = 0, ihop = 0, akt = 0;
    std::vector<std::string> logg;   // eventens ORDNING
    int kryss = 0, klar = 0;
    // `changedPath()` som handlern SÅG — i `onCheck` respektive `onCommit`.
    std::vector<glaze::TreePath> kryssStig, klarStig;

    Trad()
    {
        onChange   = [this] { ++val;  logg.push_back("change"); };
        onExpand   = [this] { ++ut;   logg.push_back("expand"); };
        onCollapse = [this] { ++ihop; logg.push_back("collapse"); };
        onActivate = [this] { ++akt;  logg.push_back("activate"); };
        onCheck    = [this] {
            ++kryss;
            kryssStig.push_back(changedPath());
        };
        onCommit = [this] {
            ++klar;
            klarStig.push_back(changedPath());
        };
    }
    void nolla()
    {
        val = ut = ihop = akt = 0;
        logg.clear();
        kryss = klar = 0;
        kryssStig.clear();
        klarStig.clear();
    }
};

// Alla valda som stigtext, i trädordning — felutskriften.
std::string valda(const glaze::TreeView& t)
{
    std::string s;
    for (const glaze::TreePath& p : t.selectedPaths())
        s += stig(p);
    return s.empty() ? "(inget)" : s;
}

using Stigar = std::vector<glaze::TreePath>;

// Rot ─┬─ A ─┬─ A1
//      │     └─ A2
//      ├─ B
//      └─ C ─── C1
void bygg(glaze::TreeView& t)
{
    t.setNodes({{"A", {{"A1"}, {"A2"}}},
                {"B"},
                {"C", {{"C1"}}}});
}

// ================================================================ HEADLESS

void fallKollektion()
{
    Trad t;
    // ---- 1. addNode: förälderns stig, svaret är den nya nodens stig.
    const glaze::TreePath a  = t.addNode({}, "A");
    const glaze::TreePath a1 = t.addNode(a, "A1");
    const glaze::TreePath b  = t.addNode({}, "B");
    check(a == glaze::TreePath{0} && a1 == glaze::TreePath{0, 0} &&
              b == glaze::TreePath{1},
          "1: addNode svarar med stigen", stig(a) + stig(a1) + stig(b));
    check(t.nodeCount({}) == 2 && t.nodeCount({0}) == 1 &&
              t.nodeText({0, 0}) == "A1",
          "1: noderna ligger där stigarna säger");
    check(t.addNode({7}, "X").empty() && t.nodeCount({}) == 2,
          "1: okänd förälder ⇒ tomt svar och ingen tillväxt");
    check(t.nodeText({9, 9}).empty() && !t.hasNode({9}),
          "1: ogiltig stig läses som tom");

    // ---- 2. setNode ersätter nodens EGNA fält — en utelämnad bild blir
    //         ingen bild; barnen och utfällningen står kvar.
    glaze::AssetHandle ikon;
    ikon.id = 42;   // ett handtag behöver ingen fil för att bäras
    t.setNode({0}, "A", ikon);
    t.setExpanded({0}, true);
    t.setNode({0}, "A (ny)");
    check(t.nodeText({0}) == "A (ny)" && !t.nodeImg({0}).valid(),
          "2: setNode utan bild NOLLAR bilden (tilldelning)",
          std::to_string(t.nodeImg({0}).asset().id));
    check(t.nodeCount({0}) == 1 && t.isExpanded({0}),
          "2: …men barnen och utfällningen är orörda");
}

void fallValetFoljerNoden()
{
    Trad t;
    bygg(t);
    // ---- 3. VALET ÄR NODEN, inte positionen.
    t.setSelectedPath({1});   // B
    check(t.selectedText() == "B" && t.val == 0,
          "3: programmatiskt val — och inget event", t.selectedText());
    t.moveNode({1}, {}, 0);   // B först
    check(t.selectedPath() == glaze::TreePath{0} && t.selectedText() == "B",
          "3: B flyttade först — valet följde med", stig(t.selectedPath()));
    t.moveNode({1}, {}, 3);   // A sist (FÖRE-flytten-index 3 = efter C)
    check(t.selectedText() == "B" && t.nodeText({2}) == "A",
          "3: en ANNAN nod flyttad förbi — valet står kvar på B",
          t.selectedText());
    t.removeNode({0});
    check(t.selectedPath().empty() && t.val == 0,
          "3: den valda noden borttagen ⇒ inget valt, inget event");
}

void fallFlytt()
{
    // ---- 4. moveNode i FÖRE-flytten-koordinater (Qt:s moveRows).
    Trad t;
    bygg(t);   // A B C
    check(t.moveNode({0}, {}, 2), "4: A före C (index 2 FÖRE flytten)");
    check(t.nodeText({0}) == "B" && t.nodeText({1}) == "A" &&
              t.nodeText({2}) == "C",
          "4: samma förälder FRAMÅT — indexet justeras för uttaget",
          t.nodeText({0}) + t.nodeText({1}) + t.nodeText({2}));
    check(t.moveNode({2}, {}, 0) && t.nodeText({0}) == "C",
          "4: samma förälder BAKÅT — ingen justering");
    // C B A — flytta A2 (under A) IN I C.
    check(t.moveNode({2, 1}, {0}, 1) && t.nodeText({0, 1}) == "A2" &&
              t.nodeCount({2}) == 1,
          "4: till en annan förälder");
    // Utfällning och barn FÖLJER med.
    t.setExpanded({0}, true);
    check(t.moveNode({0}, {}, 3) && t.isExpanded({2}) &&
              t.nodeCount({2}) == 2,
          "4: noden flyttar med sitt delträd OCH sin utfällning");

    // ---- 5. de ogiltiga flyttarna — false, logg, INGENTING ändras.
    Trad u;
    bygg(u);
    check(!u.moveNode({0}, {0}, 0), "5: in i SIG SJÄLV avvisas");
    check(!u.moveNode({0}, {0, 1}, 0), "5: in i ett EGET barn avvisas");
    check(!u.moveNode({1}, {}, 4), "5: index bortom slutet avvisas");
    check(!u.moveNode({1}, {}, -1), "5: negativt index avvisas");
    check(!u.moveNode({9}, {}, 0), "5: okänd källa avvisas");
    check(u.nodeText({0}) == "A" && u.nodeText({1}) == "B" &&
              u.nodeCount({0}) == 2,
          "5: …och trädet är orört efter alla fem");
}

void fallAnvandareMotProgram()
{
    // ---- 6. PROGRAMMATISKT avfyrar aldrig något.
    Trad t;
    bygg(t);
    t.setSelectedPath({0, 1});
    t.setExpanded({0}, true);
    t.setExpanded({0}, false);
    t.setNodes({{"X", {{"Y"}}}});
    t.ensureVisible({0, 0});
    check(t.val == 0 && t.ut == 0 && t.ihop == 0,
          "6: settrarna och ensureVisible fyrar inget",
          std::to_string(t.val) + "/" + std::to_string(t.ut) + "/" +
              std::to_string(t.ihop));
    check(t.isExpanded({0}), "6: ensureVisible fällde ut förfadern");

    // ---- 7. TANGENTERNA — en handling, ett event.
    Trad k;
    bygg(k);   // A(A1, A2) B C(C1), allt hopfällt
    using K = gfx::Key;
    check(k.keyDown(K::Down, 0) && k.selectedText() == "A" && k.val == 1,
          "7: Ner utan val väljer första raden", k.selectedText());
    check(k.keyDown(K::Right, 0) && k.isExpanded({0}) && k.ut == 1 &&
              k.val == 1 && k.changedPath() == glaze::TreePath{0},
          "7: Höger på en hopfälld nod fäller UT (onExpand, inget val)");
    check(k.keyDown(K::Right, 0) && k.selectedText() == "A1" && k.val == 2,
          "7: Höger på en utfälld nod går till FÖRSTA BARNET",
          k.selectedText());
    check(k.changedPath().empty(),
          "7: en ren valhändelse nollar changedPath");
    check(k.keyDown(K::Right, 0) && k.val == 2 && k.ut == 1,
          "7: Höger på ett löv — tangenten tas, ingenting händer");
    check(k.keyDown(K::Left, 0) && k.selectedText() == "A" && k.val == 3,
          "7: Vänster på ett barn går upp till FÖRÄLDERN", k.selectedText());
    check(k.keyDown(K::Left, 0) && !k.isExpanded({0}) && k.ihop == 1 &&
              k.val == 3,
          "7: Vänster på en utfälld nod fäller IHOP (onCollapse)");
    check(k.keyDown(K::End, 0) && k.selectedText() == "C" && k.val == 4,
          "7: End — sista SYNLIGA raden", k.selectedText());
    check(k.keyDown(K::Home, 0) && k.selectedText() == "A" && k.val == 5,
          "7: Home — första raden");
    check(k.keyDown(K::Up, 0) && k.val == 5,
          "7: Upp på första raden — tangenten tas, inget nytt event");
    check(!k.keyDown(K::Down, gfx::kModCtrl) && k.val == 5,
          "7: en MODIFIERAD pil lämnas åt genvägarna");
    check(!k.keyDown(K::Space, 0), "7: Blanksteg tas inte (bubblar vidare)");
    check(k.keyDown(K::Return, 0) && k.akt == 1 && k.val == 5 &&
              !k.isExpanded({0}) && k.ut == 1 && k.ihop == 1,
          "7: Retur på den valda noden AKTIVERAR — och fäller inte");
    Trad tom;
    bygg(tom);
    check(!tom.keyDown(K::Return, 0) && tom.akt == 0,
          "7: Retur utan vald nod bubblar vidare");

    // ---- 8. setProp-textformen.
    Trad p;
    bygg(p);
    check(p.setProp("selectedPath", "0,1") &&
              p.selectedPath() == glaze::TreePath{0, 1},
          "8: setProp(\"0,1\")", stig(p.selectedPath()));
    check(p.setProp("selectedPath", "{2, 0}") &&
              p.selectedPath() == glaze::TreePath{2, 0},
          "8: C++-literalen tas också emot", stig(p.selectedPath()));
    check(p.setProp("selectedPath", "1,x") &&
              p.selectedPath() == glaze::TreePath{2, 0},
          "8: ogiltig text — nyckeln är känd, valet ORÖRT");
    check(p.setProp("selectedPath", "") && p.selectedPath().empty(),
          "8: tom text = inget val");
    check(p.setProp("allowDrop", "true") && p.allowDrop(),
          "8: basens nycklar når igenom (allowDrop)");
}

void fallSlappregeln()
{
    // ---- 17. SLÄPP IN I per nod — API:t. Default ja; en ogiltig
    //          stig är logg + no-op; regeln följer NODEN genom en flytt;
    //          `setNode` lämnar den; massformens femte fält är samma regel.
    Trad t;
    bygg(t);   // A(A1 A2) B C(C1)
    check(t.allowDropInto({0}) && t.allowDropInto({2, 0}),
          "17: default — varje nod tar emot");
    t.setAllowDropInto({1}, false);
    check(!t.allowDropInto({1}) && t.allowDropInto({0}) &&
              t.allowDropInto({2}),
          "17: setAllowDropInto({1}, false) gäller just B");
    t.setAllowDropInto({9}, false);
    check(!t.allowDropInto({9}) && t.nodeCount({}) == 3,
          "17: ogiltig stig — no-op, och en nod som inte finns svarar nej");
    t.moveNode({1}, {}, 0);   // B först
    check(!t.allowDropInto({0}) && t.nodeText({0}) == "B" &&
              t.allowDropInto({1}),
          "17: regeln FÖLJER noden genom moveNode");
    t.setNode({0}, "B (ny)");
    check(!t.allowDropInto({0}),
          "17: setNode byter text och bild — regeln står kvar (som "
          "utfällningen)");
    t.setNodes({{"Mapp", {{"fil.txt", {}, {}, false, false}}, {}, true},
                {"Annan"}});
    check(t.allowDropInto({0}) && !t.allowDropInto({0, 0}) &&
              t.allowDropInto({1}),
          "17: massformens femte fält — filen är ett löv för släppet");
}

// ======================================== FLERVAL, KRYSS, FÄRG, BILD
//
// Fall 19–30. De headless delarna här; musen (19, 20,
// 23, 25, 26 och 29) går genom fönstrets riktiga ingång längre ned.

using K = gfx::Key;

void fallFlervalAv()
{
    // ---- 19. FLERVAL AV (standard): Ctrl-tangenterna bubblar —
    //          genvägarna får dem — och Shift är vanlig navigering.
    Trad t;
    bygg(t);   // A(A1 A2) B C(C1), hopfällt — raderna A B C
    check(!t.isMultiSelect(), "19: flerval är AV som standard");
    t.setSelectedPath({0});
    t.nolla();
    check(!t.keyDown(K::Down, gfx::kModCtrl) && t.val == 0 &&
              t.currentPath() == glaze::TreePath{0},
          "19: Ctrl+Ner bubblar i enkelval — den aktuella står kvar",
          stig(t.currentPath()));
    check(!t.keyDown(K::A, gfx::kModCtrl) && t.val == 0 &&
              t.selectedPaths() == Stigar{{0}},
          "19: Ctrl+A bubblar i enkelval — valet är orört", valda(t));
    check(!t.keyDown(K::Space, gfx::kModCtrl) && t.val == 0,
          "19: Ctrl+Space bubblar i enkelval");
    check(t.keyDown(K::Down, gfx::kModShift) && t.val == 1 &&
              t.selectedPaths() == Stigar{{1}},
          "19: Shift+Ner i enkelval är vanlig navigering — ETT valt",
          valda(t));
}

void fallFlervalTangenter()
{
    // ---- 21. TANGENTERNA i flerval: Shift+pil spänner från ankaret,
    //          Ctrl+pil flyttar BARA den aktuella, Ctrl+Space vänder den,
    //          Ctrl+A väljer de SYNLIGA och Retur aktiverar den aktuella.
    Trad t;
    bygg(t);   // raderna A B C
    t.setMultiSelect(true);
    t.setSelectedPath({0});   // aktuell OCH ankare: A
    t.nolla();
    check(t.keyDown(K::Down, gfx::kModShift) && t.val == 1 &&
              t.selectedPaths() == Stigar{{0}, {1}} &&
              t.currentPath() == glaze::TreePath{1},
          "21: Shift+Ner — spannet A..B, B aktuell", valda(t));
    check(t.keyDown(K::Down, gfx::kModCtrl) && t.val == 1 &&
              t.selectedPaths() == Stigar{{0}, {1}} &&
              t.currentPath() == glaze::TreePath{2},
          "21: Ctrl+Ner flyttar den AKTUELLA — valet står kvar, inget event",
          valda(t) + " aktuell " + stig(t.currentPath()));
    check(t.keyDown(K::Space, gfx::kModCtrl) && t.val == 2 &&
              t.selectedPaths() == Stigar{{0}, {1}, {2}},
          "21: Ctrl+Space vänder den aktuella (C) in i valet", valda(t));
    check(t.keyDown(K::Space, gfx::kModCtrl) && t.val == 3 &&
              t.selectedPaths() == Stigar{{0}, {1}},
          "21: Ctrl+Space igen vänder den ut", valda(t));
    t.setExpanded({0}, true);   // A A1 A2 B C — C1 dold
    check(t.keyDown(K::A, gfx::kModCtrl) && t.val == 4 &&
              t.selectedPaths() == Stigar{{0}, {0, 0}, {0, 1}, {1}, {2}},
          "21: Ctrl+A väljer de SYNLIGA — inte C1 i den stängda grenen",
          valda(t));
    check(t.keyDown(K::A, gfx::kModCtrl) && t.val == 4,
          "21: Ctrl+A igen — ingen ändring, inget event");
    check(t.keyDown(K::Return, 0) && t.akt == 1 && t.val == 4,
          "21: Retur aktiverar den aktuella noden");
    check(t.currentPath() == glaze::TreePath{2},
          "21: …och den aktuella är C (stigen, inte raden)",
          stig(t.currentPath()));
}

void fallFlervalProgram()
{
    // ---- 22. PROGRAMMET fyrar aldrig, och läsvägarnas regler.
    Trad t;
    bygg(t);   // raderna A B C
    t.setMultiSelect(true);
    t.setSelected({0}, true);
    t.setSelected({2}, true);
    t.setSelected({1}, true);
    check(t.val == 0 && t.selectedPaths() == Stigar{{0}, {1}, {2}},
          "22: setSelected i flerval lägger till — utan event", valda(t));
    check(t.selectedPath() == glaze::TreePath{0} && t.currentPath().empty(),
          "22: utan aktuell nod är selectedPath den FÖRSTA valda",
          stig(t.selectedPath()));
    t.setSelectedPath({2});
    check(t.selectedPaths() == Stigar{{2}} &&
              t.currentPath() == glaze::TreePath{2},
          "22: setSelectedPath väljer BARA noden och gör den aktuell",
          valda(t));
    t.setSelected({0}, true);
    check(t.selectedPath() == glaze::TreePath{2},
          "22: en VALD aktuell nod vinner över trädordningen",
          stig(t.selectedPath()));
    t.setSelected({2}, false);
    check(t.selectedPath() == glaze::TreePath{0} &&
              t.selectedPaths() == Stigar{{0}},
          "22: en ovald aktuell nod — den första valda", valda(t));
    t.setSelected({2, 0}, true);   // C1 — i en hopfälld gren
    check(t.selectedPaths() == Stigar{{0}, {2, 0}},
          "22: selectedPaths tar med en vald nod i en hopfälld gren",
          valda(t));
    t.selectAll();
    check(t.selectedPaths() == Stigar{{0}, {1}, {2}},
          "22: selectAll väljer de synliga — och bara dem", valda(t));
    t.clearSelection();
    check(t.selectedPaths().empty() && t.currentPath().empty(),
          "22: clearSelection släpper valet OCH den aktuella noden");

    t.setSelectedPath({1});
    t.setSelected({0}, true);
    t.setSelected({2}, true);
    t.setMultiSelect(false);
    check(t.selectedPaths() == Stigar{{1}} &&
              t.currentPath() == glaze::TreePath{1},
          "22: setMultiSelect(false) behåller EN nod — selectedPaths nod",
          valda(t));
    t.setSelected({0}, true);
    check(t.selectedPaths() == Stigar{{0}} &&
              t.currentPath() == glaze::TreePath{0},
          "22: enkelval — setSelected(true) väljer BARA noden", valda(t));
    t.setSelected({0}, false);
    check(t.selectedPaths().empty(),
          "22: enkelval — setSelected(false) på den valda lämnar inget");
    t.setSelected({9}, true);
    check(t.selectedPaths().empty(), "22: ogiltig stig — no-op");
    check(t.val == 0 && t.klar == 0,
          "22: INGEN av settrarna fyrade onChange eller onCommit",
          std::to_string(t.val) + "/" + std::to_string(t.klar));
}

void fallValetFoljerFlytten()
{
    // ---- 24. VALET ÄR NODERNAS FLAGGOR — det följer dem genom moveNode,
    //          och setNodes nollar det.
    Trad t;
    bygg(t);   // A B C
    t.setMultiSelect(true);
    t.setSelected({0}, true);
    t.setSelected({2}, true);
    t.moveNode({2}, {}, 0);   // C först: C A B
    check(t.selectedPaths() == Stigar{{0}, {1}} && t.nodeText({0}) == "C" &&
              t.nodeText({1}) == "A",
          "24: C flyttade först — båda valen följde sina noder", valda(t));
    t.moveNode({0}, {2}, 0);   // C in i B: A B(C)
    check(t.selectedPaths() == Stigar{{0}, {1, 0}},
          "24: in i en annan förälder — valet följde med", valda(t));
    t.setNodes({{"X"}, {"Y"}});
    check(t.selectedPaths().empty() && t.currentPath().empty() && t.val == 0,
          "24: setNodes nollar valet och den aktuella noden");
}

void fallKryssen()
{
    // ---- 26. KRYSSEN (den headless delen): programmet fyrar inget, ingen
    //          kaskad, `setNode` lämnar krysset, det följer noden — och
    //          tangenternas regler.
    Trad t;
    bygg(t);   // A(A1 A2) B C(C1)
    check(!t.showsChecks() && !t.isChecked({0}),
          "26: rutorna är AV som standard och inget är kryssat");
    t.setChecked({0}, true);
    check(t.isChecked({0}) && !t.isChecked({0, 0}) && !t.isChecked({0, 1}) &&
              t.kryss == 0 && t.klar == 0,
          "26: setChecked fyrar inget och KASKADERAR inte till barnen");
    t.setChecked({0, 1}, true);
    t.setChecked({0}, false);
    check(!t.isChecked({0}) && t.isChecked({0, 1}),
          "26: …och inte heller tillbaka — barnets kryss står kvar");
    t.setChecked({0}, true);
    t.setChecked({9}, true);
    check(!t.isChecked({9}) && t.nodeCount({}) == 3,
          "26: ogiltig stig — no-op, och en nod som inte finns är okryssad");
    t.setNode({0}, "A (ny)");
    check(t.isChecked({0}), "26: setNode lämnar krysset (tillstånd)");
    t.moveNode({0}, {}, 3);   // B C A
    check(t.isChecked({2}) && t.nodeText({2}) == "A (ny)" &&
              !t.isChecked({0}),
          "26: krysset FÖLJER noden genom moveNode");

    // Tangenterna, rutorna PÅ: Space kryssar också i enkelval, och före
    // valet; Ctrl+Space bubblar i enkelval.
    t.setShowChecks(true);
    t.setSelectedPath({0});   // B
    t.nolla();
    check(t.keyDown(K::Space, 0) && t.isChecked({0}) && t.kryss == 1 &&
              t.val == 0 && t.selectedPaths() == Stigar{{0}},
          "26: Space kryssar den aktuella i ENKELVAL — valet rörs inte");
    check(t.kryssStig == Stigar{{0}} && t.klarStig == Stigar{{0}} &&
              t.changedPath() == glaze::TreePath{0},
          "26: onCheck och onCommit — båda med nodens stig i changedPath");
    check(!t.keyDown(K::Space, gfx::kModCtrl) && t.kryss == 1 && t.val == 0,
          "26: Ctrl+Space bubblar i enkelval (ingen vändning av något)");

    // Flerval: Ctrl+Space vänder VALET, Space krysset.
    t.setMultiSelect(true);
    check(t.keyDown(K::Space, gfx::kModCtrl) && !t.isSelected({0}) &&
              t.val == 1 && t.kryss == 1 && t.isChecked({0}),
          "26: Ctrl+Space vänder VALET i flerval — inte krysset");
    check(t.klarStig.size() == 2 && t.klarStig[1].empty(),
          "26: …och valets onCommit bär en TOM changedPath");
    check(t.keyDown(K::Space, 0) && !t.isChecked({0}) && t.kryss == 2 &&
              t.val == 1,
          "26: Space vänder krysset i flerval");
    // Utan rutor: Space väljer i flerval och bubblar i enkelval.
    t.setShowChecks(false);
    check(t.keyDown(K::Space, 0) && t.isSelected({0}) && t.val == 2 &&
              t.kryss == 2,
          "26: utan rutor väljer Space den aktuella i flerval");
    t.setMultiSelect(false);
    check(!t.keyDown(K::Space, 0), "26: utan rutor bubblar Space i enkelval");

    // En onCheck-handler som FYLLER OM trädet nollar changedPath — onCommit
    // ser ändå den kryssade noden (ListBoxens kontrakt).
    Trad u;
    bygg(u);
    u.setShowChecks(true);
    u.setSelectedPath({1});
    u.onCheck = [&u] {
        ++u.kryss;
        u.setNodes({{"A", {{"A1"}, {"A2"}}}, {"B"}, {"C", {{"C1"}}}});
    };
    u.nolla();
    check(u.keyDown(K::Space, 0) && u.kryss == 1 &&
              u.klarStig == Stigar{{1}},
          "26: onCommit efter en onCheck som fyllde om — stigen ÅTERSTÄLLD",
          u.klarStig.empty() ? "(inget onCommit)" : stig(u.klarStig.back()));
}

// Toppnodernas texter i ordning, som en sträng — fallens förväntade värden för en flytt.
std::string toppar(const glaze::TreeView& t)
{
    std::string s;
    for (int i = 0; i < t.nodeCount({}); ++i)
        s += (i ? "," : "") + t.nodeText({i});
    return s;
}

void fallFlyttaFlera()
{
    // ---- 35. DEN PUBLIKA `moveNodes` — samma flytt
    //          som släppet: allt valideras FÖRE någon mutation, en källa
    //          inuti en annan följer med sin förfader, indexet räknas i
    //          FÖRE-flytten-koordinater och noderna sätts in i TRÄDORDNING.
    Trad t;
    bygg(t);   // A(A1 A2) B C(C1)
    check(!t.moveNodes({{1}, {7}}, {}, 0) && toppar(t) == "A,B,C",
          "35: en ogiltig stig bland giltiga — false, och INGENTING flyttat "
          "(atomiskt)",
          toppar(t));
    check(!t.moveNodes({}, {}, 0) && toppar(t) == "A,B,C",
          "35: en tom lista — false");
    check(!t.moveNodes({{0}}, {0, 1}, 0) && t.nodeCount({0}) == 2 &&
              toppar(t) == "A,B,C",
          "35: ett mål INUTI en källa — false");
    check(!t.moveNodes({{1}}, {}, 4) && toppar(t) == "A,B,C",
          "35: ett index utanför [0, antal] — false");
    check(!t.moveNodes({{1}}, {9}, 0) && toppar(t) == "A,B,C",
          "35: en förälder som inte finns — false");

    // A1 ligger INUTI A — den följer med sin förfader och flyttas inte för
    // sig. Index 3 är "efter C" före flytten.
    t.setChecked({0}, true);
    t.setSelectedPath({0});
    t.setExpanded({0}, true);
    check(t.moveNodes({{0}, {0, 0}}, {}, 3) && toppar(t) == "B,C,A" &&
              t.nodeCount({2}) == 2 && t.nodeText({2, 0}) == "A1",
          "35: av {0} och {0, 0} flyttas bara {0} — A1 följer med A",
          toppar(t));
    check(t.isChecked({2}) && t.selectedPath() == glaze::TreePath{2} &&
              t.isExpanded({2}),
          "35: krysset, valet och utfällningen FÖLJER noden");

    // TRÄDORDNING och FÖRE-flytten-index: indata {2}, {0} i omvänd ordning,
    // index 2 = "före C" i det här trädet — A och C landar i trädordning där.
    Trad u;
    bygg(u);   // A B C
    check(u.moveNodes({{2}, {0}}, {}, 2) && toppar(u) == "B,A,C",
          "35: stigarna i godtycklig ordning sätts in i TRÄDORDNING, indexet "
          "räknat före flytten",
          toppar(u));

    // En toppnod in i en SENARE toppnods barnlista: C:s stig flyttar ett
    // steg när A tas ut, och målet hålls vid sitt id.
    Trad v;
    bygg(v);   // A B C(C1)
    check(v.moveNodes({{0}}, {2}, 0) && toppar(v) == "B,C" &&
              v.nodeText({1, 0}) == "A" && v.nodeCount({1}) == 2,
          "35: en toppnod in i en SENARE toppnod hamnar under rätt förälder",
          toppar(v));
}

void fallKryssadeStigar()
{
    // ---- 37. `checkedPaths()` — TRÄDORDNING, också i hopfällda
    //          grenar (`selectedPaths()`s parhäst).
    Trad t;
    check(t.checkedPaths().empty(), "37: ett tomt träd har inga kryss");
    bygg(t);   // A(A1 A2) B C(C1), hopfällt
    t.setChecked({2, 0}, true);
    t.setChecked({0, 1}, true);
    t.setChecked({1}, true);
    check(t.checkedPaths() == Stigar{{0, 1}, {1}, {2, 0}},
          "37: kryssen i trädordning — A2 och C1 ligger i stängda grenar");
    t.setChecked({1}, false);
    t.moveNode({2}, {}, 0);   // C först — C1 följer med
    check(t.checkedPaths() == Stigar{{0, 0}, {1, 1}},
          "37: kryssen följer noderna genom en flytt");
}

// Varje nod i `a` jämförd fält för fält med samma stig i `b`, rekursivt —
// text, utfällning, släppregel, kryss, färg och bild.
bool sammaTillstand(const glaze::TreeView& a, const glaze::TreeView& b,
                    const glaze::TreePath& förälder, std::string& fel)
{
    if (a.nodeCount(förälder) != b.nodeCount(förälder)) {
        fel = "antal under " + stig(förälder);
        return false;
    }
    for (int i = 0; i < a.nodeCount(förälder); ++i) {
        glaze::TreePath p = förälder;
        p.push_back(i);
        if (a.nodeText(p) != b.nodeText(p) ||
            a.isExpanded(p) != b.isExpanded(p) ||
            a.allowDropInto(p) != b.allowDropInto(p) ||
            a.isChecked(p) != b.isChecked(p) ||
            a.nodeColor(p) != b.nodeColor(p) || !(a.nodeImg(p) == b.nodeImg(p))) {
            fel = stig(p) + " (" + a.nodeText(p) + ")";
            return false;
        }
        if (!sammaTillstand(a, b, p, fel))
            return false;
    }
    return true;
}

void fallMassformen()
{
    // ---- 27. MASSFORMEN och de PLATTA satserna ger samma tillstånd — för
    //          krysset (fält 6) och färgen (fält 7) över alla stigar, med
    //          positionella och designerade aggregat.
    const gfx::Color röd{200, 40, 40, 255};
    const gfx::Color grön{0, 140, 0, 255};
    glaze::AssetHandle ikon;
    ikon.id = 7;   // ett handtag behöver ingen fil för att bäras

    Trad m;
    m.setNodes({{"A",
                 {{"A1", {}, {}, false, true, true, röd}, {"A2"}},
                 ikon,
                 true,
                 false},
                {"B", {}, {}, false, true, true},
                {.text     = "C",
                 .children = {{.text = "C1", .checked = true}},
                 .color    = grön}});

    Trad p;
    p.addNode({}, "A", ikon);
    p.addNode({0}, "A1", {}, röd);
    p.addNode({0}, "A2");
    p.setExpanded({0}, true);
    p.setAllowDropInto({0}, false);
    p.setChecked({0, 0}, true);
    p.addNode({}, "B");
    p.setChecked({1}, true);
    p.addNode({}, "C", {}, grön);
    p.addNode({2}, "C1");
    p.setChecked({2, 0}, true);

    std::string fel;
    check(sammaTillstand(m, p, {}, fel),
          "27: massformen = de platta satserna, nod för nod", fel);
    check(m.isChecked({0, 0}) && m.nodeColor({0, 0}) == röd &&
              m.isChecked({1}) && !m.nodeColor({1}) &&
              m.nodeColor({2}) == grön && m.isChecked({2, 0}),
          "27: fälten landade där aggregatet sa (stickprov)");
}

// ---- KOLUMNERNA (K1–K7) — headless: cellerna och sorteringen
//      INOM SYSKON. Den synliga ordningen läses genom tangenterna (Home och
//      Ner väljer rad för rad), och svaret är STIGARNA — appens index.
//
//   prof ─┬─ parse  12  3
//         ├─ draw   40  1
//         └─ idle    9
void profil(glaze::TreeView& t)
{
    t.setColumns({{"Namn", 120}, {"ms", 60, glaze::Align::Right}, {"Anrop"}});
    t.setNodes({{.text     = "prof",
                 .children = {{.text = "parse", .cells = {"12", "3"}},
                              {.text = "draw", .cells = {"40", "1"}},
                              {.text = "idle", .cells = {"9"}}},
                 .expanded = true,
                 .cells    = {"61", "4"}}});
}

// De synliga raderna som stigtext, uppifrån — `Home` och sedan `Ner` tills
// valet står still.
std::string synligOrdning(Trad& t)
{
    std::string s;
    t.keyDown(gfx::Key::Home, 0);
    glaze::TreePath förra;
    for (int i = 0; i < 16 && t.selectedPath() != förra; ++i) {
        förra = t.selectedPath();
        s += stig(förra);
        t.keyDown(gfx::Key::Down, 0);
    }
    return s;
}

void fallKolumner()
{
    // ---- K1. Cellerna: massformens ÅTTONDE fält, `cell` (kolumn 0 är
    //          nodens text, en saknad cell tom) och `setCell`, som fyller ut
    //          en kortare nod och inte kräver att kolumnen finns.
    Trad t;
    profil(t);
    check(t.cell({0}, 0) == "prof" && t.cell({0}, 1) == "61" &&
              t.cell({0, 0}, 2) == "3" && t.cell({0, 2}, 2).empty() &&
              t.cell({9}, 1).empty() && t.cell({0}, -1).empty(),
          "K1: massformens celler och `cell`",
          t.cell({0}, 1) + "|" + t.cell({0, 0}, 2));
    t.setCell({0, 2}, 2, "7");
    t.setCell({0, 2}, 5, "x");
    check(t.cell({0, 2}, 1) == "9" && t.cell({0, 2}, 2) == "7" &&
              t.cell({0, 2}, 4).empty() && t.cell({0, 2}, 5) == "x",
          "K1: setCell fyller ut och kräver ingen kolumn");
    t.setCell({0}, 0, "nej");
    t.setCell({9}, 1, "nej");
    check(t.nodeText({0}) == "prof" && t.nodeCount({}) == 1,
          "K1: kolumn 0 och en ogiltig stig — logg + no-op");

    // ---- K2. `setNode` är TILLDELNING också för cellerna.
    t.setNode({0, 1}, "draw");
    check(t.cell({0, 1}, 1).empty(), "K2: setNode utan celler nollar dem",
          t.cell({0, 1}, 1));
    t.setNode({0, 1}, "draw", {}, std::nullopt, {"40", "1"});
    check(t.cell({0, 1}, 1) == "40", "K2: …och med celler sätter dem");

    // ---- K3. Ingen sortering: appens ordning.
    check(synligOrdning(t) == "{0}{0,0}{0,1}{0,2}",
          "K3: utan sorteringskolumn — appens ordning", synligOrdning(t));

    // ---- K4. Kolumn 1 stigande, NATURLIGT (9 < 12 < 40) — och stigarna är
    //          appens: idle är fortfarande {0, 2}.
    t.setSortColumn(1);
    check(synligOrdning(t) == "{0}{0,2}{0,0}{0,1}",
          "K4: syskonen i kolumnens naturliga ordning", synligOrdning(t));
    check(t.nodeText({0, 2}) == "idle" && t.cell({0, 2}, 1) == "9",
          "K4: indexen är appens — sorteringen är bara visning");

    // ---- K5. Fallande, och kolumn 0 (nodens text) stigande.
    t.setSortAscending(false);
    check(synligOrdning(t) == "{0}{0,1}{0,0}{0,2}", "K5: fallande",
          synligOrdning(t));
    t.setSortAscending(true);
    t.setSortColumn(0);
    check(synligOrdning(t) == "{0}{0,1}{0,2}{0,0}",
          "K5: kolumn 0 sorterar på nodens text", synligOrdning(t));

    // ---- K6. En cell i sorteringskolumnen flyttar noden (draw → 1 ms).
    t.setSortColumn(1);
    t.setCell({0, 1}, 1, "1");
    check(synligOrdning(t) == "{0}{0,1}{0,2}{0,0}",
          "K6: en ändrad cell sorteras om", synligOrdning(t));

    // ---- K7. En kolumn bortom kolumnerna sorterar inget, och setProp-vägen
    //          (`.glazeui`s setter-fält) är basens.
    t.setSortColumn(7);
    check(synligOrdning(t) == "{0}{0,0}{0,1}{0,2}",
          "K7: sorteringskolumn utanför — appens ordning", synligOrdning(t));
    check(t.setProp("sortColumn", "1") && t.sortColumn() == 1 &&
              synligOrdning(t) == "{0}{0,1}{0,2}{0,0}",
          "K7: setProp(\"sortColumn\") sorterar trädet", synligOrdning(t));
}

// ------------------------------------------------------- ritkontrollerna
//
// Fast teckensteg per kodpunkt — som i listview_gate. Talar FYSISKA
// px: steget skalas med Ctx:ts skala.
class StegFont final : public gfx::NullFont {
public:
    explicit StegFont(float steg) : steg_(steg) {}

    gfx::Sizef measure(const std::string& s) const override
    {
        return gfx::Sizef{steg_ * float(tecken(s, s.size())), steg_ * 2.0f};
    }
    float height() const override { return steg_ * 2.0f; }
    bool  valid() const override { return true; }
    float prefixWidth(const std::string& s, std::size_t bytes) const override
    {
        return steg_ * float(tecken(s, std::min(bytes, s.size())));
    }
    std::size_t byteIndexAtX(const std::string& s, float) const override
    {
        return s.size();
    }
    std::size_t fitWidth(const std::string& s, float) const override
    {
        return s.size();
    }

private:
    static std::size_t tecken(const std::string& s, std::size_t bytes)
    {
        std::size_t n = 0;
        for (std::size_t i = 0; i < bytes; ++i)
            if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
                ++n;
        return n;
    }
    float steg_;
};

// Bild som bara är ett mått — fönstrets cache får en sådan av renderern
// när den laddar upp en Bitmap (som i toolbar_gate).
struct FalskBild final : gfx::NullImage {
    int  width() const override { return 16; }
    int  height() const override { return 16; }
    bool valid() const override { return true; }
};

// Renderer som SPELAR IN texten MED FÄRG, strecken med färg (pilen och
// bocken är streck) och bilderna med målrect — resten är NullRendererns
// no-op. `pushClip`/`popClip` är icke-virtuella och klarar sig själva.
struct Inspelning : gfx::NullRenderer {
    struct Text {
        std::string s;
        gfx::Color  färg;
    };
    struct Streck {
        float      x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
        gfx::Color färg;
    };
    struct Bild {
        gfx::Rectf dst;
        float      opacitet = 1.0f;
    };
    std::vector<Text>   texter;
    std::vector<Streck> streck;
    std::vector<Bild>   bilder;

    float drawText(gfx::Font&, float, float, const std::string& s,
                   gfx::Color c) override
    {
        texter.push_back({s, c});
        return 0.0f;
    }
    void line(float x1, float y1, float x2, float y2, gfx::Color c) override
    {
        streck.push_back({x1, y1, x2, y2, c});
    }
    using gfx::NullRenderer::drawImage;
    void drawImage(gfx::Image&, const gfx::Rectf&, const gfx::Rectf& dst,
                   float opacity) override
    {
        bilder.push_back({dst, opacity});
    }
    using gfx::NullRenderer::createImage;
    std::unique_ptr<gfx::Image> createImage(int, int, gfx::ImageFormat,
                                            const void*, std::size_t,
                                            bool) override
    {
        return std::make_unique<FalskBild>();
    }
    void rensa()
    {
        texter.clear();
        streck.clear();
        bilder.clear();
    }
    // Färgen texten `s` ritades i — nullopt om den aldrig ritades.
    std::optional<gfx::Color> färgPå(const std::string& s) const
    {
        for (const Text& t : texter)
            if (t.s == s)
                return t.färg;
        return std::nullopt;
    }
    // Streck i färgen `c` — i hela ritningen, eller bara inom rutan `r`.
    int streckI(gfx::Color c) const
    {
        int n = 0;
        for (const Streck& l : streck)
            n += l.färg == c ? 1 : 0;
        return n;
    }
    int streckI(gfx::Color c, const gfx::Rectf& r) const
    {
        int n = 0;
        for (const Streck& l : streck)
            n += l.färg == c && r.contains(l.x1, l.y1) && r.contains(l.x2, l.y2)
                     ? 1
                     : 0;
        return n;
    }
};

std::string färgText(std::optional<gfx::Color> c)
{
    if (!c)
        return "(inte ritad)";
    return "{" + std::to_string(c->r) + "," + std::to_string(c->g) + "," +
           std::to_string(c->b) + "}";
}

const char* uiNamn(glaze::UI ui)
{
    return ui == glaze::UI::Slate   ? "Slate"
           : ui == glaze::UI::Glaze ? "Glaze"
                                    : "Minimalistic";
}

glaze::Ctx ritCtx(glaze::UI ui, gfx::Font& f, gfx::Renderer& r, float skala)
{
    glaze::Ctx c{};
    c.r           = &r;
    c.font        = &f;
    c.headingFont = nullptr;
    c.scale       = skala;
    c.ui          = ui;
    c.style       = &glaze::defaultStyle(ui);
    c.theme       = &glaze::defaultTheme(ui).light;
    c.area        = gfx::Rectf{0.0f, 0.0f, 400.0f, 300.0f};
    return c;
}

// MARKERINGENS textfärg per familj — ListBoxens recept, skrivet här ur
// kontraktet och inte läst ur trädet: Slate `windowBg` på `accent`, Glaze
// `onAccent` på `accentSurface`, Minimalistic `accent` bredvid strecket.
gfx::Color valdFärg(glaze::UI ui, const glaze::Theme::Colors& col)
{
    return ui == glaze::UI::Slate   ? col.windowBg
           : ui == glaze::UI::Glaze ? col.onAccent
                                    : col.accent;
}

void fallFargen()
{
    // ---- 28. NODENS FÄRG gäller TEXTEN på en omarkerad rad i ett aktivt
    //          träd. Pilen och bocken följer temat, markeringens färg vinner
    //          på en vald rad, ett inaktivt träd är `textDim` rakt
    //          igenom, och `setNode` utan färg nollar den — i alla tre UI.
    const gfx::Color appens{201, 13, 77, 255};   // ingen temafärg
    for (const glaze::UI ui :
         {glaze::UI::Slate, glaze::UI::Glaze, glaze::UI::Minimalistic}) {
        const std::string namn = std::string("28 ") + uiNamn(ui) + ": ";
        StegFont          f(7.0f);
        Inspelning        rec;
        glaze::Ctx        c   = ritCtx(ui, f, rec, 1.0f);
        const auto&       col = c.theme->colors;
        const auto&       m   = *c.style;

        Trad t;
        t.width  = 220;
        t.height = 200;
        t.setNodes({{"A", {{"A1"}}, {}, true, true, false, appens},
                    {"B", {}, {}, false, true, true, appens},
                    {"C"}});
        t.setShowChecks(true);
        c.box = t.resolve(c);
        t.draw(c);
        check(rec.färgPå("A") == appens && rec.färgPå("B") == appens,
              namn + "texten i APPENS färg på omarkerade rader",
              färgText(rec.färgPå("A")));
        check(rec.färgPå("A1") == col.text && rec.färgPå("C") == col.text,
              namn + "en nod utan färg — temats textfärg",
              färgText(rec.färgPå("C")));
        check(rec.streckI(appens) == 0,
              namn + "pilen och bocken ritas ALDRIG i appens färg",
              std::to_string(rec.streckI(appens)) + " streck");
        check(rec.streckI(col.textDim) > 0,
              namn + "pilen (A har barn) i temats dämpade färg");
        // Bocken i B:s ruta (rad 2: A A1 B), geometrin ur stilen.
        const gfx::Rectf in = t.innerBounds(c);
        const float      rx = in.x + m.listPadX + m.treeExpanderW;
        const float      ry = in.y + 2.0f * m.listRowH +
                              (m.listRowH - m.checkSize) / 2.0f;
        const gfx::Rectf ruta{c.box.x + rx - 1.0f, c.box.y + ry - 1.0f,
                              m.checkSize + 2.0f, m.checkSize + 2.0f};
        check(rec.streckI(col.text, ruta) > 0,
              namn + "bocken i B:s ruta i temats textfärg");

        rec.rensa();
        t.setSelectedPath({1});   // B (stigen {1}, raden 2)
        t.draw(c);
        check(rec.färgPå("B") == valdFärg(ui, col) && rec.färgPå("A") == appens,
              namn + "VALD rad — markeringens färg vinner (E1)",
              färgText(rec.färgPå("B")));
        check(rec.streckI(appens) == 0,
              namn + "vald rad — inga streck i appens färg heller");

        rec.rensa();
        t.disable();
        t.draw(c);
        check(rec.färgPå("A") == col.textDim && rec.färgPå("B") == col.textDim &&
                  rec.färgPå("C") == col.textDim,
              namn + "INAKTIVT träd — allt i textDim",
              färgText(rec.färgPå("A")));
        t.enable();

        rec.rensa();
        t.setSelectedPath({});
        t.setNode({1}, "B");   // utan färg ⇒ temats
        t.draw(c);
        check(!t.nodeColor({1}) && rec.färgPå("B") == col.text,
              namn + "setNode utan färg NOLLAR färgen",
              färgText(rec.färgPå("B")));
        t.setNodeColor({0}, std::nullopt);
        check(!t.nodeColor({0}), namn + "setNodeColor(nullopt) nollar");
        t.setNodeColor({9}, appens);
        check(!t.nodeColor({9}) && t.nodeCount({}) == 3,
              namn + "setNodeColor på en ogiltig stig — no-op");
    }
}

void fallBildUrPixlar()
{
    // ---- 30. BILD UR PIXLAR: `nodeImg` ger samma bild tillbaka, och den
    //          ritas EN gång i `iconFallback`-rutan (en Bitmap har ingen
    //          storleksklass) — där texten annars börjar, och efter rutan
    //          när kryssen syns. Skala 1 och 2.
    for (const float skala : {1.0f, 2.0f}) {
        const std::string namn = "30 (skala " + std::to_string(int(skala)) +
                                 "): ";
        StegFont          f(7.0f * skala);
        Inspelning        rec;
        glaze::ImageCache bilder(rec);
        glaze::Ctx        c = ritCtx(glaze::UI::Slate, f, rec, skala);
        c.images            = &bilder;
        const auto&       m = *c.style;

        glaze::Bitmap bild(8, 8);
        Trad          t;
        t.width  = 220;
        t.height = 120;
        t.addNode({}, "Bild", bild);
        check(t.nodeImg({0}) == glaze::ImageSource(bild) &&
                  t.nodeImg({0}).kind() == glaze::ImageSource::Kind::Bitmap,
              namn + "nodeImg ger samma bild tillbaka");
        c.box = t.resolve(c);
        t.draw(c);

        const gfx::Rectf in = t.innerBounds(c);
        auto ruta = [&](float x) {
            return gfx::Rectf{c.box.x + x * skala,
                              c.box.y + (in.y + (m.listRowH - m.iconFallback) /
                                                    2.0f) * skala,
                              m.iconFallback * skala, m.iconFallback * skala};
        };
        auto lika = [](const gfx::Rectf& a, const gfx::Rectf& b) {
            return std::abs(a.x - b.x) < 0.01f && std::abs(a.y - b.y) < 0.01f &&
                   std::abs(a.w - b.w) < 0.01f && std::abs(a.h - b.h) < 0.01f;
        };
        const float x0 = in.x + m.listPadX + m.treeExpanderW;
        check(rec.bilder.size() == 1 && lika(rec.bilder[0].dst, ruta(x0)) &&
                  rec.bilder[0].opacitet == 1.0f,
              namn + "EN bild i iconFallback-rutan där innehållet börjar",
              std::to_string(rec.bilder.size()) + " bilder");

        rec.rensa();
        t.setShowChecks(true);
        t.draw(c);
        check(rec.bilder.size() == 1 &&
                  lika(rec.bilder[0].dst,
                       ruta(x0 + m.checkSize + m.checkGap)),
              namn + "med kryss — bilden efter rutan och checkGap",
              std::to_string(rec.bilder.size()) + " bilder");

        rec.rensa();
        t.disable();
        t.draw(c);
        check(rec.bilder.size() == 1 &&
                  std::abs(rec.bilder[0].opacitet - (1.0f - m.iconFade)) <
                      1e-6f,
              namn + "inaktivt träd — bilden tonad med 1 − iconFade");
    }
}

// W1–W7. BREDD EFTER INNEHÅLLET i trädet: en `autoWidth`-kolumn
// följer rubriken (sorteringspilens plats reserverad) och de SYNLIGA
// nodernas innehåll — kolumn 0 med indrag, pil, ruta och ikon, kolumn 1…n
// cellerna; en hopfälld grens noder räknas inte och en utfällning växer
// kolumnen; dubbelklicket på en avdelare skriver den anpassade bredden som en
// fast bredd. Förväntat värde räknas ur teckensteget (8 px) och stilens mått, aldrig ur
// trädets egen `columnW`.
void fallBreddEfterInnehall()
{
    StegFont   f(8.0f);
    Inspelning rec;
    glaze::Ctx c   = ritCtx(glaze::UI::Slate, f, rec, 1.0f);
    const auto& m  = *c.style;
    const float pad = m.listPadX;
    const float pil = pad + 2.0f * m.chevronW;
    const float a   = glaze::ColumnPanel::Column::autoWidth;
    // Radens kolumn-0-block: djup · indrag + pilens ruta (+ rutan) + texten.
    const auto block = [&](int djup, int tecken, bool kryss, bool bild) {
        float w = float(djup) * m.treeIndent + m.treeExpanderW +
                  8.0f * float(tecken);
        if (kryss)
            w += m.checkSize + m.checkGap;
        if (bild)
            w += m.iconFallback + m.iconGap;
        return w;
    };
    // Andrummet: en hårlinje, 1 FYSISK px — ritningens kortning jämför strikt.
    const float lucka = 1.0f / c.scale;
    const auto bredd = [&](float innehåll, int rubrikTecken) {
        return std::max({m.columnMinW,
                         8.0f * float(rubrikTecken) + 2.0f * pad + pil + lucka,
                         innehåll > 0.0f ? innehåll + 2.0f * pad + lucka
                                         : 0.0f});
    };
    const auto nara = [](float x, float y) { return std::abs(x - y) < 0.01f; };

    Trad t;
    t.width  = 400;
    t.height = 200;
    t.setColumns({{"Namn", a}, {"Värde", a}, {"Fast", 50}});
    t.setNodes({{.text = "rot",
                 .children = {{.text = "barn med långt namn",
                               .cells = {"1", "22"}},
                              {.text = "b"}},
                 .expanded = true,
                 .cells = {"kort", ""}},
                {.text = "stängd",
                 .children = {{.text = "gömd nod med mycket långt namn"}}}});
    c.box = t.resolve(c);

    // Synliga: rot (djup 0), barn med långt namn (19 tecken) och b (djup 1),
    // stängd (djup 0). Den gömda noden syns inte.
    const float k0 = std::max({block(0, 3, false, false),
                               block(1, 19, false, false),
                               block(1, 1, false, false),
                               block(0, 6, false, false)});
    check(nara(t.columnW(c, 0), bredd(k0, 4)),
          "W1: kolumn 0 = det bredaste av rubriken och de synliga noderna "
          "(indrag + pil + text), med luft",
          std::to_string(t.columnW(c, 0)) + " mot " + std::to_string(bredd(k0, 4)));
    check(nara(t.columnW(c, 1), bredd(8.0f * 4, 5)),
          "W2: kolumn 1 = det bredaste av rubriken och de synliga cellerna");
    check(nara(t.columnW(c, 2), 50.0f), "W2: en fast bredd rörs inte");

    // En hopfälld gren räknas inte — och en utfällning växer kolumnen.
    t.setExpanded({1}, true);
    const float k0ut = std::max(k0, block(1, 30, false, false));
    check(nara(t.columnW(c, 0), bredd(k0ut, 4)),
          "W3: en utfälld gren växer kolumnen",
          std::to_string(t.columnW(c, 0)) + " mot " + std::to_string(bredd(k0ut, 4)));
    t.setExpanded({1}, false);
    check(nara(t.columnW(c, 0), bredd(k0, 4)),
          "W3: …och en ihopfällning krymper den igen");

    // Kryss, ikon, cell och text ändrar bredden.
    t.setShowChecks(true);
    const float k0kryss = std::max({block(0, 3, true, false),
                                    block(1, 19, true, false),
                                    block(1, 1, true, false),
                                    block(0, 6, true, false)});
    check(nara(t.columnW(c, 0), bredd(k0kryss, 4)),
          "W4: kryssrutans zon räknas i kolumn 0");
    t.setShowChecks(false);
    t.setNode({0, 1}, "b", glaze::Bitmap(8, 8));
    const float k0bild = std::max({block(0, 3, false, false),
                                   block(1, 19, false, false),
                                   block(1, 1, false, true),
                                   block(0, 6, false, false)});
    check(nara(t.columnW(c, 0), bredd(k0bild, 4)),
          "W4: ikonen och dess mellanrum räknas i kolumn 0");
    t.setCell({0}, 1, "en ganska lång cell");   // 19 tecken
    check(nara(t.columnW(c, 1), bredd(8.0f * 19, 5)),
          "W5: setCell flyttar kolumn 1:s bredd");
    t.setNode({0, 1}, "b");
    t.setNode({0, 0}, "kort", {}, std::nullopt, {"1", "22"});
    check(nara(t.columnW(c, 0),
               bredd(std::max({block(0, 3, false, false),
                               block(1, 4, false, false),
                               block(1, 1, false, false),
                               block(0, 6, false, false)}), 4)),
          "W5: setNode flyttar kolumn 0:s bredd");

    // Dubbelklicket på kolumn 0:s avdelare: den anpassade bredden, FAST.
    Trad d;
    d.width  = 400;
    d.height = 200;
    d.setColumns({{"Namn", 60}, {"B", 60}});
    d.setNodes({{.text = "en mycket lång nod", .children = {{.text = "x"}},
                 .expanded = true}});
    c.box          = d.resolve(c);
    const float hy = c.box.y + m.panelInset + m.tableHeaderH / 2.0f;
    const float kant = c.box.x + m.panelInset + 60.0f;
    const float fast = bredd(std::max(block(0, 18, false, false),
                                      block(1, 1, false, false)), 4);
    d.mouseDown(c, kant, hy);
    d.mouseDoubleClick(c, kant, hy);
    check(nara(d.columnWidth(0), fast),
          "W6: dubbelklick på avdelaren skriver den anpassade bredden",
          std::to_string(d.columnWidth(0)) + " mot " + std::to_string(fast));
    d.mouseDrag(c, kant + 40.0f, hy);
    d.mouseUp(c, kant + 40.0f, hy);
    check(nara(d.columnWidth(0), fast) && d.sortColumn() == -1,
          "W6: draget som det andra trycket började är avslutat, och inget "
          "sorterades");
    d.addNode({}, "en nod som är mycket mycket längre än den förra");
    check(nara(d.columnWidth(0), fast) && nara(d.columnW(c, 0), fast),
          "W7: bredden är FAST efteråt");
}

// ============================================================= MED FÖNSTER

struct Vard : glaze::AppWindow {
    using glaze::AppWindow::handleEvent;

    Trad          trad;
    glaze::Panel  mal;        // ett ANNAT mål — svarar `svar`
    glaze::DragEffect svar = glaze::DragEffect::Move;
    std::string   last;       // den inbyggda lasten, sedd av appens handler
    std::string   lastText;
    glaze::TreePath overParent;
    int           overIndex = -2;
    int           slut      = 0;
    glaze::DragEffect slutEffekt = glaze::DragEffect::None;

    Vard() : glaze::AppWindow("tree_gate", 420, 320, gfx::Chrome::None)
    {
        trad.name   = "trad";
        trad.x      = 10;
        trad.y      = 10;
        trad.width  = 200;
        trad.height = 280;
        trad.setAllowDrag(true);
        trad.setAllowDrop(true);
        kopplaDrag();

        mal.name   = "mal";
        mal.x      = 250;
        mal.y      = 10;
        mal.width  = 120;
        mal.height = 120;
        mal.setBorder(true);
        mal.setAllowDrop(true);
        mal.onDragEnter = [this](glaze::Widget*, glaze::DragArgs& e) {
            e.effect = svar;
        };

        root().adopt(trad);
        root().adopt(mal);
    }

    // Appens STANDARDhandlers för draget — fall 31–38 byter dem och kopplar
    // tillbaka med den här. Handlers körs EFTER det inbyggda — de SER lasten
    // trädet fyllt och platsen det räknat fram — utom `onDrop`, som körs FÖRE
    // den inbyggda flytten och därför inte är kopplad
    // här: de äldre fallen mäter det inbyggda släppet.
    void kopplaDrag()
    {
        trad.onDragStart = [this](glaze::Widget*, glaze::DragStartArgs& e) {
            last     = e.data.get(glaze::TreeView::kNodeFormat);
            lastText = e.data.text();
        };
        trad.onDragOver = [this](glaze::Widget*, glaze::DragArgs&) {
            overParent = trad.dropParent();
            overIndex  = trad.dropIndex();
        };
        trad.onDragEnd = [this](glaze::Widget*, glaze::DragEndArgs& e) {
            ++slut;
            slutEffekt = e.effect;
        };
        trad.onDrop = nullptr;
    }

    // ---- geometrin, ur samma mått som trädet (se filhuvudet)
    float s() { return window().uiScale(); }
    gfx::Rectf tradRect() { return trad.resolve(trad.behaviorCtx()); }
    // Radens överkant i FYSISKA px (oscrollat — fallen rullar aldrig).
    float radY(int rad)
    {
        const glaze::Ctx c = trad.behaviorCtx();
        return tradRect().y +
               (trad.innerBounds(c).y + float(rad) * style().listRowH) * s();
    }
    gfx::Pointf radMitt(int rad, int djup)
    {
        const glaze::Ctx c = trad.behaviorCtx();
        const float x = trad.innerBounds(c).x + style().listPadX +
                        float(djup) * style().treeIndent +
                        style().treeExpanderW + 20.0f;   // in i texten
        return {tradRect().x + x * s(),
                radY(rad) + style().listRowH * s() / 2.0f};
    }
    gfx::Pointf pil(int rad, int djup)
    {
        const glaze::Ctx c = trad.behaviorCtx();
        const float x = trad.innerBounds(c).x + style().listPadX +
                        float(djup) * style().treeIndent +
                        style().treeExpanderW / 2.0f;
        return {tradRect().x + x * s(),
                radY(rad) + style().listRowH * s() / 2.0f};
    }
    // En punkt på andelen `andel` av radens höjd (0 = överkanten).
    gfx::Pointf radAndel(int rad, int djup, float andel)
    {
        gfx::Pointf p = radMitt(rad, djup);
        p.y = radY(rad) + andel * style().listRowH * s();
        return p;
    }
    // En punkt i radens ÖVRE respektive UNDRE kantzon (före/efter).
    gfx::Pointf radKant(int rad, int djup, bool överst)
    {
        gfx::Pointf p = radMitt(rad, djup);
        const float z = style().treeDropEdge * style().listRowH * s();
        p.y = radY(rad) + (överst ? z / 2.0f : style().listRowH * s() - z / 2.0f);
        return p;
    }

    // Musens MODIFIERARE går med händelsen (`gfx::Event::mods`) — det är så
    // AppWindow får dem till `modifierKeys()` (listview_gate F8).
    void mus(gfx::EventType t, gfx::Pointf p, std::uint32_t mods = 0)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = gfx::MouseButton::Left;
        e.mods     = mods;
        handleEvent(e);
    }
    void ner(gfx::Pointf p, std::uint32_t mods = 0)
    {
        mus(gfx::EventType::MouseButtonDown, p, mods);
    }
    void flytta(gfx::Pointf p) { mus(gfx::EventType::MouseMotion, p); }
    void upp(gfx::Pointf p, std::uint32_t mods = 0)
    {
        mus(gfx::EventType::MouseButtonUp, p, mods);
    }
    void klick(gfx::Pointf p, std::uint32_t mods = 0)
    {
        ner(p, mods);
        upp(p, mods);
    }
    // HÖGERTRYCKET: ner och upp med högerknappen —
    // fönstrets riktiga väg, `rightDown` och sedan `onContextMenu`.
    void hoger(gfx::Pointf p)
    {
        for (const gfx::EventType typ :
             {gfx::EventType::MouseButtonDown, gfx::EventType::MouseButtonUp}) {
            gfx::Event e;
            e.type     = typ;
            e.windowId = window().id();
            e.mouseX   = p.x;
            e.mouseY   = p.y;
            e.button   = gfx::MouseButton::Right;
            handleEvent(e);
        }
    }
    // Ett INJICERAT timertick — fönstrets Timer-gren, utan att vänta.
    void tick(std::uint32_t id)
    {
        gfx::Event e;
        e.type     = gfx::EventType::Timer;
        e.windowId = window().id();
        e.timerId  = id;
        handleEvent(e);
    }
    // KRYSSRUTANS mitt och en punkt i TEXTEN när rutorna syns — ur stilen
    // (`checkSize`, `checkGap`), aldrig ur trädets `checkRect`.
    gfx::Pointf ruta(int rad, int djup)
    {
        const glaze::Ctx c = trad.behaviorCtx();
        const float x = trad.innerBounds(c).x + style().listPadX +
                        float(djup) * style().treeIndent +
                        style().treeExpanderW + style().checkSize / 2.0f;
        return {tradRect().x + x * s(),
                radY(rad) + style().listRowH * s() / 2.0f};
    }
    gfx::Pointf textMedRuta(int rad, int djup)
    {
        const glaze::Ctx c = trad.behaviorCtx();
        const float x = trad.innerBounds(c).x + style().listPadX +
                        float(djup) * style().treeIndent +
                        style().treeExpanderW + style().checkSize +
                        style().checkGap + 6.0f;
        return {tradRect().x + x * s(),
                radY(rad) + style().listRowH * s() / 2.0f};
    }
    void tangent(gfx::Key k)
    {
        gfx::Event e;
        e.type     = gfx::EventType::KeyDown;
        e.windowId = window().id();
        e.key      = k;
        handleEvent(e);
    }
    float troskel() { return style().dragThreshold * s(); }
    // Starta ett drag från en punkt: tryck, rör en bit förbi tröskeln.
    void starta(gfx::Pointf p)
    {
        ner(p);
        flytta({p.x + troskel() + 2.0f, p.y});
    }
    // En NY TRYCKFÖLJD: ett klick på det andra målet. Grinden trycker
    // snabbare än OS:ets dubbelklickstid, så två fall som båda börjar på
    // samma rad hade annars gett ett DUBBELKLICK — det första
    // fallets sista tryck och nästa falls första.
    void nyFoljd()
    {
        const gfx::Rectf m = mal.resolve(mal.behaviorCtx());
        klick({m.x + m.w / 2.0f, m.y + m.h / 2.0f});
    }
};

// ---- Flerval, kryss, färg och bild med musen: körs i fönsterfallens kedja, efter de
//      äldre fallen. Varje block återställer trädets lägen själv.

void fallFlervalMus(Vard& w)
{
    Trad& t = w.trad;
    t.setShowChecks(false);

    // ---- 19 (musen). FLERVAL AV: Ctrl- och Shift-klick är vanliga klick.
    w.nyFoljd();
    bygg(t);   // raderna A B C
    t.setMultiSelect(false);
    t.nolla();
    w.klick(w.radMitt(0, 0));
    w.klick(w.radMitt(1, 0), gfx::kModCtrl);
    check(t.selectedPaths() == Stigar{{1}} && t.val == 2,
          "19: Ctrl+klick i enkelval väljer BARA noden", valda(t));
    w.klick(w.radMitt(2, 0), gfx::kModShift);
    check(t.selectedPaths() == Stigar{{2}} && t.val == 3,
          "19: Shift+klick i enkelval väljer BARA noden", valda(t));

    // ---- 20. FLERVAL PÅ: Ctrl vänder, Shift ger spannet i SYNLIG ordning,
    //          Ctrl+Shift lägger till — exakt ett onChange per ändrande klick.
    w.nyFoljd();
    bygg(t);   // A(A1 A2) B C(C1), hopfällt — raderna A B C
    t.setMultiSelect(true);
    t.nolla();
    w.klick(w.radMitt(0, 0));   // A — ankaret
    check(t.selectedPaths() == Stigar{{0}} && t.val == 1,
          "20: klick väljer BARA noden", valda(t));
    w.klick(w.radMitt(2, 0), gfx::kModShift);   // till C
    check(t.selectedPaths() == Stigar{{0}, {1}, {2}} && t.val == 2,
          "20: Shift+klick — spannet A..C i SYNLIG ordning (A1, A2 och C1 i "
          "stängda grenar ingår inte)",
          valda(t));
    w.klick(w.radMitt(1, 0), gfx::kModCtrl);   // B ut — och ankaret till B
    check(t.selectedPaths() == Stigar{{0}, {2}} && t.val == 3,
          "20: Ctrl+klick vänder B UT", valda(t));
    t.setExpanded({0}, true);   // A A1 A2 B C
    w.klick(w.radMitt(2, 1), gfx::kModCtrl | gfx::kModShift);   // A2
    check(t.selectedPaths() == Stigar{{0}, {0, 1}, {1}, {2}} && t.val == 4,
          "20: Ctrl+Shift+klick LÄGGER TILL spannet B..A2", valda(t));
    w.nyFoljd();
    w.klick(w.radMitt(2, 1), gfx::kModCtrl | gfx::kModShift);
    check(t.val == 4, "20: samma Ctrl+Shift+klick igen — ingen ändring, inget "
                      "event");
    w.nyFoljd();
    w.klick(w.radMitt(1, 1), gfx::kModCtrl);   // A1 in
    check(t.selectedPaths() == Stigar{{0}, {0, 0}, {0, 1}, {1}, {2}} &&
              t.val == 5,
          "20: Ctrl+klick vänder A1 IN", valda(t));

    // ---- 23. IHOPFÄLLNINGEN I FLERVAL: de dolda ättlingarna lämnar valet,
    //          noden väljs, den aktuella flyttar upp — ett onChange efter
    //          onCollapse — och valet utanför grenen står kvar.
    w.nyFoljd();
    bygg(t);
    t.setMultiSelect(true);
    t.setExpanded({0}, true);    // A A1 A2 B C
    t.setSelectedPath({0, 1});   // A2 — aktuell
    t.setSelected({1}, true);    // B
    t.nolla();
    w.klick(w.pil(0, 0));
    check(!t.isExpanded({0}) && t.selectedPaths() == Stigar{{0}, {1}} &&
              t.currentPath() == glaze::TreePath{0},
          "23: A2 lämnade valet, A valdes och blev aktuell, B står kvar",
          valda(t) + " aktuell " + stig(t.currentPath()));
    check(t.logg == std::vector<std::string>{"collapse", "change"} &&
              t.val == 1,
          "23: ETT onChange, efter onCollapse");
    w.nyFoljd();
    t.setExpanded({0}, true);
    t.setSelectedPath({1});   // bara B — ingenting i grenen
    t.nolla();
    w.klick(w.pil(0, 0));
    check(t.selectedPaths() == Stigar{{1}} && t.val == 0 && t.ihop == 1,
          "23: en ihopfällning utan valda ättlingar — inget onChange",
          valda(t));

    // ---- 25. TRYCK PÅ EN VALD NOD i flerval: inget event vid trycket —
    //          solo och ETT onChange vid släppet.
    w.nyFoljd();
    bygg(t);   // A B C
    t.setMultiSelect(true);
    t.setSelectedPath({0});
    t.setSelected({1}, true);
    t.setSelected({2}, true);
    t.nolla();
    w.ner(w.radMitt(1, 0));   // B
    check(t.val == 0 && t.selectedPaths() == Stigar{{0}, {1}, {2}} &&
              t.currentPath() == glaze::TreePath{1},
          "25: trycket — B blir aktuell, valet står KVAR, inget event",
          valda(t));
    w.upp(w.radMitt(1, 0));
    check(t.val == 1 && t.selectedPaths() == Stigar{{1}},
          "25: släppet utan drag — B ensam, ETT onChange", valda(t));
    w.nyFoljd();
    t.setSelected({0}, true);
    t.nolla();
    w.ner(w.radMitt(0, 0), gfx::kModCtrl);   // en MODIFIERAD tryckning
    check(t.val == 1 && t.selectedPaths() == Stigar{{1}},
          "25: Ctrl+tryck på en vald nod vänder den DIREKT (inget väntande "
          "soloval)",
          valda(t));
    w.upp(w.radMitt(0, 0), gfx::kModCtrl);
    check(t.val == 1, "25: …och släppet gör inget mer");
    t.setMultiSelect(false);
}

void fallKryssMus(Vard& w)
{
    // ---- 26 (musen). RUTANS ZON vänder krysset utan att flytta valet;
    //          ett löv har ingen pil, och dess pilkolumn är INTE zonen; ett
    //          dubbelklick i rutan är två vändningar och ingen aktivering;
    //          ett drag startar aldrig därifrån.
    Trad& t = w.trad;
    w.nyFoljd();
    bygg(t);   // raderna A B C
    t.setMultiSelect(false);
    t.setShowChecks(true);
    t.setSelectedPath({0});
    t.nolla();
    w.klick(w.ruta(1, 0));   // B:s ruta
    check(t.isChecked({1}) && t.kryss == 1 && t.klar == 1 &&
              t.kryssStig == Stigar{{1}} && t.klarStig == Stigar{{1}},
          "26: klick i rutan kryssar — onCheck + onCommit med B:s stig");
    check(t.val == 0 && t.selectedPaths() == Stigar{{0}} &&
              t.currentPath() == glaze::TreePath{0},
          "26: …och valet och den aktuella noden rörs inte", valda(t));
    w.nyFoljd();
    w.klick(w.pil(1, 0));   // B är ett löv — pilkolumnen är ingen zon
    check(t.isChecked({1}) && t.kryss == 1 &&
              t.selectedPaths() == Stigar{{1}} && t.val == 1,
          "26: ett löv i PILKOLUMNEN — raden väljs, krysset rörs inte",
          valda(t));
    w.nyFoljd();
    w.klick(w.textMedRuta(2, 0));   // C:s text
    check(!t.isChecked({2}) && t.kryss == 1 &&
              t.selectedPaths() == Stigar{{2}} && t.val == 2,
          "26: klick i TEXTEN väljer — och kryssar inte", valda(t));

    w.nyFoljd();
    t.nolla();
    w.klick(w.ruta(0, 0));   // A:s ruta …
    w.klick(w.ruta(0, 0));   // … andra trycket ⇒ dubbelklick
    check(!t.isChecked({0}) && t.kryss == 2 && t.akt == 0 && t.ut == 0 &&
              t.val == 0,
          "26: dubbelklick i rutan — två vändningar, ingen aktivering, ingen "
          "fällning");

    w.nyFoljd();
    t.nolla();
    w.starta(w.ruta(2, 0));
    check(!w.isDragging() && t.kryss == 1,
          "26: tryck i rutan + rörelse — krysset vände, men INGET drag");
    w.upp(w.ruta(2, 0));
    t.setShowChecks(false);
}

void fallDragFlera(Vard& w)
{
    // ---- 29. DRAG AV FLERA i trädet.
    Trad& t = w.trad;
    t.setShowChecks(false);

    // 29a. Hela VALET dras från en vald nod: lasten "a;b" i trädordning och
    //      texterna radvis; ett släpp IN I en mapp flyttar dem i
    //      trädordning, mappen fälls ut och valet står kvar.
    w.nyFoljd();
    bygg(t);
    t.setMultiSelect(true);
    t.setExpanded({0}, true);    // A A1 A2 B C
    t.setSelectedPath({2});      // C
    t.setSelected({0, 1}, true);  // A2
    t.nolla();
    w.starta(w.radMitt(4, 0));   // från C — A2 är vald ovanför
    check(w.isDragging() && w.last == "0,1;2" && w.lastText == "A2\nC",
          "29: lasten är HELA valet i trädordning",
          "\"" + w.last + "\" / \"" + w.lastText + "\"");
    w.flytta(w.radMitt(3, 0));   // mitten av B ⇒ IN I B
    check(t.dropEffect() == glaze::DragEffect::Move,
          "29: in i B tas emot som Move");
    w.upp(w.radMitt(3, 0));
    check(t.nodeCount({0}) == 1 && t.nodeText({1}) == "B" &&
              t.nodeText({1, 0}) == "A2" && t.nodeText({1, 1}) == "C" &&
              t.isExpanded({1}),
          "29: A2 och C flyttade IN I B i trädordning, och B fälldes ut",
          t.nodeText({1, 0}) + "," + t.nodeText({1, 1}));
    check(t.selectedPaths() == Stigar{{1, 0}, {1, 1}} && t.val == 0,
          "29: valet stod kvar — och draget fyrade inget onChange",
          valda(t));

    // 29b. En vald FÖRÄLDER och dess valda barn: barnet följer med sin
    //      förälder och flyttas inte en gång till (beskärningen).
    w.nyFoljd();
    bygg(t);
    t.setMultiSelect(true);
    t.setExpanded({0}, true);    // A A1 A2 B C
    t.setSelectedPath({0});      // A
    t.setSelected({0, 0}, true);  // A1 — A:s barn
    t.setSelected({1}, true);     // B
    w.starta(w.radMitt(0, 0));   // från A
    check(w.last == "0;1", "29: lasten är beskuren — A1 följer med A",
          "\"" + w.last + "\"");
    w.flytta(w.radMitt(1, 1));   // in i A1 — inuti en dragen nod
    check(t.dropEffect() == glaze::DragEffect::None,
          "29: ett mål INUTI en av de dragna avvisas");
    w.flytta(w.radMitt(4, 0));   // in i C
    w.upp(w.radMitt(4, 0));
    check(t.nodeCount({}) == 1 && t.nodeText({0}) == "C" &&
              t.nodeText({0, 1}) == "A" && t.nodeText({0, 2}) == "B" &&
              t.nodeCount({0, 1}) == 2 && t.nodeText({0, 1, 0}) == "A1",
          "29: A (med A1 kvar under sig) och B hamnade i C",
          std::to_string(t.nodeCount({})) + " toppnoder");

    // 29c. "Efter" en av de dragna: A och B dras och släpps under A — samma
    //      träd (indexet justeras för uttaget).
    w.nyFoljd();
    bygg(t);   // A B C, hopfällt
    t.setMultiSelect(true);
    t.setSelectedPath({0});
    t.setSelected({1}, true);
    w.starta(w.radMitt(1, 0));            // från B
    w.flytta(w.radKant(0, 0, false));     // underkanten av A ⇒ EFTER A
    w.upp(w.radKant(0, 0, false));
    check(t.nodeText({0}) == "A" && t.nodeText({1}) == "B" &&
              t.nodeText({2}) == "C" && t.nodeCount({0}) == 2,
          "29: släppt EFTER en av de dragna — samma träd",
          t.nodeText({0}) + t.nodeText({1}) + t.nodeText({2}));

    // 29d. En toppnod in i en SENARE toppnods barnlista — målet
    //      hålls fast vid sitt id över uttaget.
    w.nyFoljd();
    bygg(t);   // A B C
    t.setMultiSelect(true);
    t.setSelectedPath({0});   // bara A
    w.starta(w.radMitt(0, 0));
    w.flytta(w.radMitt(2, 0));   // in i C
    w.upp(w.radMitt(2, 0));
    check(t.nodeCount({}) == 2 && t.nodeText({1}) == "C" &&
              t.nodeText({1, 1}) == "A" && t.nodeCount({1, 1}) == 2,
          "29: A in i den SENARE toppnoden C — rätt förälder efter uttaget",
          t.nodeText({1, 1}));

    // 29e. MOVE TILL ETT ANNAT MÅL tar bort ALLA dragna.
    w.nyFoljd();
    bygg(t);
    t.setMultiSelect(true);
    t.setSelectedPath({1});
    t.setSelected({2}, true);
    w.svar = glaze::DragEffect::Move;
    const gfx::Rectf  mr = w.mal.resolve(w.mal.behaviorCtx());
    const gfx::Pointf M{mr.x + 20.0f, mr.y + 20.0f};
    w.starta(w.radMitt(1, 0));   // B (C är också vald)
    w.flytta(M);
    w.upp(M);
    check(t.nodeCount({}) == 1 && t.nodeText({0}) == "A" &&
              w.slutEffekt == glaze::DragEffect::Move &&
              t.selectedPaths().empty(),
          "29: Move till ett annat mål — B OCH C lämnade trädet",
          std::to_string(t.nodeCount({})) + " toppnoder");
    t.setMultiSelect(false);
}

// ---- APPENS GODKÄNNANDE (fall 31–34, 36 och 38): appens
//      handlers byts per fall, och `kopplaDrag()` kopplar tillbaka.
void fallGodkannande(Vard& w)
{
    Trad& t = w.trad;
    t.setShowChecks(false);
    t.setMultiSelect(false);

    // ---- 31. `draggedPaths()` — de dragna i TRÄDORDNING från appens
    //          `onDragStart` (sessionen är ännu Pending där) genom
    //          `onDragOver` och `onDrop`, och tom i `onDragEnd` och utanför.
    //          Appen flyttar A sist mitt i starten: de dragna id:na står kvar
    //          i ordningen A, C, men trädordningen är nu C före A.
    w.nyFoljd();
    bygg(t);                   // A(A1 A2) B C(C1), hopfällt — raderna A B C
    t.setMultiSelect(true);
    t.setSelectedPath({0});    // A
    t.setSelected({2}, true);  // C
    Stigar iStart, iStartEfter, iOver, iDrop, iSlut;
    t.onDragStart = [&](glaze::Widget*, glaze::DragStartArgs&) {
        iStart = t.draggedPaths();
        t.moveNode({0}, {}, 3);   // A sist: B C A
        iStartEfter = t.draggedPaths();
    };
    t.onDragOver = [&](glaze::Widget*, glaze::DragArgs&) {
        iOver = t.draggedPaths();
    };
    t.onDrop = [&](glaze::Widget*, glaze::DragArgs& e) {
        iDrop    = t.draggedPaths();
        e.effect = glaze::DragEffect::None;   // trädet rörs inte mer
    };
    t.onDragEnd = [&](glaze::Widget*, glaze::DragEndArgs&) {
        iSlut = t.draggedPaths();
    };
    check(t.draggedPaths().empty(), "31: före draget är draggedPaths tom");
    w.starta(w.radMitt(2, 0));   // från C — A är vald ovanför
    w.flytta(w.radMitt(5, 0));   // under sista raden: sist bland toppnoderna
    w.upp(w.radMitt(5, 0));
    check(iStart == Stigar{{0}, {2}},
          "31: i onDragStart — de dragna i trädordning (sessionen är Pending)",
          iStart.empty() ? "(tom)" : stig(iStart.front()));
    check(iStartEfter == Stigar{{1}, {2}} && iOver == Stigar{{1}, {2}} &&
              iDrop == Stigar{{1}, {2}},
          "31: efter appens flytt SORTERADE (C {1} före A {2}) — i "
          "onDragStart, onDragOver och onDrop",
          iDrop.empty() ? "(tom)" : stig(iDrop.front()));
    check(iSlut.empty() && t.draggedPaths().empty(),
          "31: tom i onDragEnd och efter draget");
    // 31b. En AVBÖJD start (appen tömmer lasten): draggedPaths gällde i
    //      start-handlern men är tom direkt efter — också medan trycket
    //      fortfarande hålls (`dragEnd` frågas aldrig för en avböjd start).
    t.setMultiSelect(false);
    t.onDragStart = [&](glaze::Widget*, glaze::DragStartArgs& e) {
        iStart = t.draggedPaths();
        e.data.clear();
    };
    w.nyFoljd();
    bygg(t);   // A B C
    w.starta(w.radMitt(1, 0));   // B
    check(!w.isDragging() && iStart == Stigar{{1}} &&
              t.draggedPaths().empty(),
          "31: en avböjd start — giltig i start-handlern, tom direkt efter",
          std::to_string(t.draggedPaths().size()) + " stigar efteråt");
    w.upp(w.radMitt(1, 0));
    w.kopplaDrag();

    // ---- 32. INTRÄDESLUCKAN: trycket vid B:s underkant och en enda
    //          rörelse till C:s mitt — den FÖRSTA platsen är "in i C", en
    //          verklig flytt. Appen har BARA en `onDragOver` som nekar
    //          förälder C, och vetot gäller redan efter den rörelsen; ett
    //          släpp på samma punkt lämnar trädet orört.
    w.nyFoljd();
    bygg(t);   // A B C(C1), hopfällt
    t.onDragOver = [&](glaze::Widget*, glaze::DragArgs& e) {
        if (t.dropParent() == glaze::TreePath{2})
            e.effect = glaze::DragEffect::None;
    };
    w.slut = 0;
    w.ner(w.radAndel(1, 0, 0.9f));
    w.flytta(w.radMitt(2, 0));   // förbi tröskeln: draget börjar HÄR
    check(w.isDragging() && t.dropEffect() == glaze::DragEffect::None,
          "32: första platsen in i C — appens onDragOver har redan nekat");
    w.upp(w.radMitt(2, 0));
    check(toppar(t) == "A,B,C" && t.nodeCount({2}) == 1 && w.slut == 1 &&
              w.slutEffekt == glaze::DragEffect::None,
          "32: släppt utan en rörelse till — trädet orört, källan fick None",
          toppar(t));
    w.kopplaDrag();

    // ---- 33. NEJ I onDrop: handlern ser det OFLYTTADE trädet och
    //          nekar — trädet rörs inte. I ett delfall väljer den Copy: det
    //          inbyggda flyttar bara vid Move, så trädet står kvar och
    //          källan får Copy.
    std::string iHandlern;
    t.onDrop = [&](glaze::Widget*, glaze::DragArgs& e) {
        iHandlern = toppar(t);
        e.effect  = glaze::DragEffect::None;
    };
    w.nyFoljd();
    bygg(t);   // A B C
    w.starta(w.radMitt(2, 0));               // C …
    w.flytta(w.radKant(0, 0, true));         // … före A
    w.upp(w.radKant(0, 0, true));
    check(iHandlern == "A,B,C",
          "33: onDrop ser det OFLYTTADE trädet (handlern frågas FÖRST)",
          iHandlern);
    check(toppar(t) == "A,B,C" && w.slutEffekt == glaze::DragEffect::None,
          "33: effect = None i onDrop — trädet orört, källan fick None",
          toppar(t));
    t.onDrop = [&](glaze::Widget*, glaze::DragArgs& e) {
        e.effect = glaze::DragEffect::Copy;
    };
    w.nyFoljd();
    w.starta(w.radMitt(2, 0));
    w.flytta(w.radKant(0, 0, true));
    w.upp(w.radKant(0, 0, true));
    check(toppar(t) == "A,B,C" && w.slutEffekt == glaze::DragEffect::Copy,
          "33: Copy i onDrop — det inbyggda flyttar inget, källan fick Copy",
          toppar(t));
    w.kopplaDrag();

    // ---- 34. APPEN TAR ÖVER: C dras "efter A", och onDrop flyttar
    //          den FÖRST med `moveNodes(draggedPaths(), dropParent(), 0)` och
    //          sätter `handled` — det inbyggda gör ingen andra flytt.
    t.onDrop = [&](glaze::Widget*, glaze::DragArgs& e) {
        t.moveNodes(t.draggedPaths(), t.dropParent(), 0);
        e.handled = true;
    };
    w.nyFoljd();
    bygg(t);   // A B C
    w.starta(w.radMitt(2, 0));               // C …
    w.flytta(w.radKant(0, 0, false));        // … efter A
    w.upp(w.radKant(0, 0, false));
    check(toppar(t) == "C,A,B" && w.slutEffekt == glaze::DragEffect::Move,
          "34: handled — appens flytt står, ingen andra flytt, källan fick "
          "Move",
          toppar(t));
    w.kopplaDrag();

    // ---- 36. FÖRÄLDERN HÅLLS VID SITT ID: C dras in i B
    //          (`dropParent() == {1}`), och onDrop flyttar A sist UTAN
    //          `handled`. I handlern är föräldern {0} efter flytten, och det
    //          inbyggda lägger C under B — inte under det som nu står på {1}.
    glaze::TreePath föreFlytten, efterFlytten;
    int             indexEfter = -2;
    t.onDrop = [&](glaze::Widget*, glaze::DragArgs&) {
        föreFlytten = t.dropParent();
        t.moveNode({0}, {}, 3);   // A sist: B C A
        efterFlytten = t.dropParent();
        indexEfter   = t.dropIndex();
    };
    w.nyFoljd();
    bygg(t);   // A B C(C1), hopfällt
    w.starta(w.radMitt(2, 0));   // C …
    w.flytta(w.radMitt(1, 0));   // … in i B
    w.upp(w.radMitt(1, 0));
    check(föreFlytten == glaze::TreePath{1} &&
              efterFlytten == glaze::TreePath{0} && indexEfter == 0,
          "36: dropParent följer B genom handlerns flytt ({1} → {0})",
          stig(föreFlytten) + " → " + stig(efterFlytten));
    check(toppar(t) == "B,A" && t.nodeText({0, 0}) == "C" &&
              w.slutEffekt == glaze::DragEffect::Move,
          "36: …och det inbyggda lade C under B", toppar(t));

    // 36b. LUCKAN, inte bara föräldern: C dras
    //      "efter A" — luckan mellan A och B. (i) Handlern lägger in en nod
    //      FÖRE luckan: luckan finns kvar på ett nytt index, och C hamnar i
    //      den. (ii) Handlern lägger in en nod I luckan: den finns inte
    //      längre, släppet blir None och C står kvar.
    t.onDrop = [&](glaze::Widget*, glaze::DragArgs&) {
        t.moveNode({0, 0}, {}, 0);   // A1 överst: A1 A B C
    };
    w.nyFoljd();
    bygg(t);   // A(A1 A2) B C(C1), hopfällt — raderna A B C
    w.starta(w.radMitt(2, 0));               // C …
    w.flytta(w.radKant(0, 0, false));        // … efter A
    w.upp(w.radKant(0, 0, false));
    check(toppar(t) == "A1,A,C,B" && w.slutEffekt == glaze::DragEffect::Move,
          "36b: en nod insatt FÖRE luckan — C hamnar ändå mellan A och B",
          toppar(t));
    t.onDrop = [&](glaze::Widget*, glaze::DragArgs&) {
        t.moveNode({0, 0}, {}, 1);   // A1 mellan A och B: A A1 B C
    };
    w.nyFoljd();
    bygg(t);
    w.starta(w.radMitt(2, 0));
    w.flytta(w.radKant(0, 0, false));
    w.upp(w.radKant(0, 0, false));
    check(toppar(t) == "A,A1,B,C" && w.slutEffekt == glaze::DragEffect::None,
          "36b: en nod insatt I luckan — luckan finns inte, None och C står "
          "kvar",
          toppar(t));
    // (iii) En DRAGEN nod som handlern tar bort:
    //       ingen flytt, och None till källan — inte Move.
    t.onDrop = [&](glaze::Widget*, glaze::DragArgs&) {
        t.removeNode({2});   // C, den dragna
    };
    w.nyFoljd();
    bygg(t);
    w.starta(w.radMitt(2, 0));
    w.flytta(w.radKant(0, 0, false));
    w.upp(w.radKant(0, 0, false));
    check(toppar(t) == "A,B" && w.slutEffekt == glaze::DragEffect::None,
          "36b: den dragna noden borttagen i onDrop — källan får None",
          toppar(t));
    w.kopplaDrag();

    // ---- 38. `canDragFrom`: appens onDragStart FYLLER lasten och
    //          sätter `allowed` — ändå startar inget drag från pilen eller
    //          rutan. Från texten startar det.
    t.onDragStart = [](glaze::Widget*, glaze::DragStartArgs& e) {
        e.data.setText("appens last");
        e.allowed = glaze::DragEffect::Copy | glaze::DragEffect::Move;
    };
    w.nyFoljd();
    bygg(t);   // A(A1 A2) B C(C1), hopfällt
    t.setShowChecks(true);
    w.starta(w.pil(0, 0));
    check(!w.isDragging(), "38: tryck på PILEN + rörelse — inget drag");
    w.upp(w.pil(0, 0));
    w.nyFoljd();
    w.starta(w.ruta(3, 0));   // A utfälld av trycket ovan: A A1 A2 B C
    check(!w.isDragging(), "38: tryck i RUTAN + rörelse — inget drag");
    w.upp(w.ruta(3, 0));
    w.nyFoljd();
    w.starta(w.textMedRuta(4, 0));   // C:s text
    check(w.isDragging(), "38: tryck på TEXTEN + rörelse — draget startar");
    w.tangent(gfx::Key::Escape);
    w.upp(w.textMedRuta(4, 0));
    t.setShowChecks(false);
    w.kopplaDrag();
}

// ---- SNABBMENYN OCH AUTOSKROLLEN (Q1–Q8): sist i
//      fönsterkedjan. Geometrin ur samma mått som trädet (filhuvudet).
void fallSnabbmeny(Vard& w)
{
    Trad& t = w.trad;
    t.setShowChecks(false);
    t.setMultiSelect(true);
    bygg(t);
    t.setExpanded({0}, true);   // A A1 A2 B C
    t.setScroll(0.0f, 0.0f);
    const glaze::Ctx  c  = t.behaviorCtx();
    const gfx::Rectf  in = t.innerBounds(c);
    const float       rh = w.style().listRowH;
    const auto lokal = [&](int rad) {
        return std::pair<float, float>{in.x + 30.0f, in.y + (float(rad) + 0.5f) * rh};
    };

    // ---- Q1. `pathAt` i LOGISKA px från trädets hörn: varje rad sin stig,
    //          nedanför sista raden, utanför i x och utan fönster `{}`.
    const Stigar väntat = {{0}, {0, 0}, {0, 1}, {1}, {2}};
    bool alla = true;
    for (int r = 0; r < 5; ++r) {
        const auto [lx, ly] = lokal(r);
        alla = alla && t.pathAt(lx, ly) == väntat[std::size_t(r)];
    }
    check(alla, "Q1: pathAt ger varje rads stig");
    check(t.pathAt(lokal(0).first, in.y + 6.0f * rh).empty() &&
              t.pathAt(in.x - 4.0f, lokal(0).second).empty() &&
              t.pathAt(lokal(0).first, -2.0f).empty(),
          "Q1: nedanför sista raden, i ramen och ovanför — {}");
    Trad lös;
    bygg(lös);
    check(lös.pathAt(10.0f, 10.0f).empty(), "Q1: utan fönster — {}");

    // ---- Q2. Högertryck på en OVALD nod med två valda: valet blir noden
    //          (ett onChange + ett onCommit), den blir aktuell, och menyn
    //          fyras med trädet som sender och noden i `contextPath()`.
    glaze::Widget*  menySender = nullptr;
    glaze::TreePath menyStig   = {9};
    int             menyer     = 0;
    t.onContextMenu = [&](glaze::Widget* s, glaze::ContextMenuArgs&) {
        menySender = s;
        menyStig   = t.contextPath();
        ++menyer;
    };
    t.setSelectedPath({0, 0});
    t.setSelected({1}, true);   // A1 och B valda
    t.nolla();
    const auto pRad = [&](int rad) {
        const auto [lx, ly] = lokal(rad);
        const gfx::Rectf r  = w.tradRect();
        return gfx::Pointf{r.x + lx * w.s(), r.y + ly * w.s()};
    };
    w.nyFoljd();
    w.hoger(pRad(4));   // C
    check(t.selectedPaths() == Stigar{{2}} && t.val == 1 && t.klar == 1 &&
              t.currentPath() == glaze::TreePath{2},
          "Q2: en ovald nod väljs ensam — ett onChange och ett onCommit",
          valda(t) + " val=" + std::to_string(t.val));
    check(menyer == 1 && menySender == &t && menyStig == glaze::TreePath{2},
          "Q2: menyn fyras — sender är trädet och contextPath() noden",
          stig(menyStig));

    // ---- Q3. Högertryck på en VALD nod: valet står, noden blir aktuell.
    t.setSelectedPath({0, 0});
    t.setSelected({1}, true);
    t.nolla();
    w.hoger(pRad(3));   // B — vald
    check(t.val == 0 && t.selectedPaths() == Stigar{{0, 0}, {1}} &&
              t.currentPath() == glaze::TreePath{1} &&
              t.contextPath() == glaze::TreePath{1},
          "Q3: en vald nod — inget onChange, valet orört, den blir aktuell",
          valda(t));

    // ---- Q4. Tom yta: `{}` och valet orört.
    t.nolla();
    w.hoger({w.tradRect().x + lokal(0).first * w.s(),
             w.tradRect().y + (in.y + 8.0f * rh) * w.s()});
    check(t.contextPath().empty() && t.val == 0 &&
              t.selectedPaths() == Stigar{{0, 0}, {1}},
          "Q4: tom yta — contextPath() {} och valet orört", valda(t));

    // ---- Q5. En `onChange` som bygger om trädet: noden hålls vid ID, så
    //          svaret blir `{}` — och menyn fyras ändå.
    menyer          = 0;
    menyStig        = {9};
    const auto gammal = t.onChange;
    t.onChange      = [&] { bygg(t); };
    w.hoger(pRad(2));   // A2 — ovald
    check(menyer == 1 && menyStig.empty() && t.contextPath().empty(),
          "Q5: trädet byggt om i onChange — contextPath() {} och menyn fyras",
          stig(menyStig));
    t.onChange = gammal;

    // ---- Q6. INAKTIVT träd: routingen hoppar över det, och förfaderns
    //          handler får `e.source` == trädet men `contextPath()` {}.
    bygg(t);
    t.setExpanded({0}, true);
    t.onContextMenu = nullptr;
    glaze::Widget* källa = nullptr;
    w.root().onContextMenu = [&](glaze::Widget*, glaze::ContextMenuArgs& e) {
        källa = e.source;
    };
    w.hoger(pRad(3));   // B — contextId sätts
    t.disable();
    w.hoger(pRad(4));
    check(källa == &t && t.contextPath().empty(),
          "Q6: inaktivt träd — e.source är trädet, contextPath() {}");
    t.enable();
    w.root().onContextMenu = nullptr;

    // ---- Q7. FOKUS FLYTTAS INTE: ett fokuserat textfält behåller det.
    auto* fält  = w.root().add<glaze::TextInput>();
    fält->x     = 250;
    fält->y     = 200;
    fält->name  = "falt";
    fält->focus();
    w.hoger(pRad(1));
    check(fält->focused(), "Q7: högertrycket lämnar fokus i textfältet");

    // ---- Q8. AUTOSKROLLEN med ett injicerat tick: 20 toppnoder, ett drag,
    //          pekaren i nederzonen — ett steg per tick, platsen omräknad
    //          utan rörelse och `onDragOver` fyrad; i mitten och vid slutet
    //          inget steg.
    {
        std::vector<glaze::TreeNode> noder;
        for (int i = 0; i < 20; ++i)
            noder.push_back({"N" + std::to_string(i)});
        t.setNodes(std::move(noder));
        t.setScroll(0.0f, 0.0f);
        int över = 0;
        t.onDragOver = [&](glaze::Widget*, glaze::DragArgs&) { ++över; };
        w.nyFoljd();
        w.starta(pRad(0));
        const glaze::Ctx  cc  = t.behaviorCtx();
        const gfx::Rectf  vp  = t.innerBounds(cc);
        const float       zon = w.style().dragScrollZone;
        const gfx::Pointf nere{pRad(0).x, w.tradRect().y +
                                              (vp.y + vp.h - zon / 2.0f) * w.s()};
        w.flytta(nere);
        const float           före    = t.scrollY();
        const glaze::TreePath föräldF = t.dropParent();
        const int             platsF  = t.dropIndex();
        const int             överF   = över;
        w.tick(glaze::kDragScrollTimerId);
        check(std::abs(t.scrollY() - (före + w.style().dragScrollStep)) < 1e-3f,
              "Q8: ett tick i nederzonen rullar dragScrollStep",
              std::to_string(t.scrollY()));
        // Tills raden under pekaren har bytts (ett steg kan stanna i samma
        // rads zon) — varje tick fyrar `onDragOver` på den nya platsen.
        int ticks = 1;
        while (t.scrollY() - före < rh + w.style().dragScrollStep && ticks < 40) {
            w.tick(glaze::kDragScrollTimerId);
            ++ticks;
        }
        check((t.dropParent() != föräldF || t.dropIndex() != platsF) &&
                  över == överF + ticks,
              "Q8: platsen räknas om utan rörelse och onDragOver fyras per tick",
              stig(föräldF) + "/" + std::to_string(platsF) + " → " +
                  stig(t.dropParent()) + "/" + std::to_string(t.dropIndex()) +
                  ", över " + std::to_string(över - överF) + " av " +
                  std::to_string(ticks));
        const gfx::Pointf mitt{pRad(0).x,
                               w.tradRect().y + (vp.y + vp.h / 2.0f) * w.s()};
        w.flytta(mitt);
        const float iMitten = t.scrollY();
        w.tick(glaze::kDragScrollTimerId);
        check(t.scrollY() == iMitten, "Q8: i mitten rullar ett tick inte");
        t.setScroll(0.0f, 1.0e6f);   // klampas till spelrummets slut
        w.flytta(nere);
        const float iSlutet = t.scrollY();
        w.tick(glaze::kDragScrollTimerId);
        check(t.scrollY() == iSlutet,
              "Q8: vid spelrummets slut rullar ett tick inte");
        w.tangent(gfx::Key::Escape);
        t.onDragOver = nullptr;
        w.kopplaDrag();
    }
    t.setMultiSelect(false);
    t.onContextMenu = nullptr;
}

// ---- KOLUMNHUVUDET MED MUSEN (KM1–KM4): sist i fönsterkedjan.
//      Geometrin ur samma mått som trädet: huvudet är `tableHeaderH` högt
//      och raderna står under det.
void fallKolumnerMus(Vard& w)
{
    Trad& t = w.trad;
    t.setShowChecks(false);
    t.setMultiSelect(false);
    t.setColumns({});
    t.setSortColumn(-1);
    profil(t);
    t.setColumns({});   // först UTAN kolumner — ett vanligt träd
    t.setScroll(0.0f, 0.0f);
    const glaze::Ctx c  = t.behaviorCtx();
    const gfx::Rectf in = t.innerBounds(c);
    const float      rh = w.style().listRowH;
    const float      hh = w.style().tableHeaderH;
    const float      lx = in.x + 30.0f;

    // ---- KM1. Utan kolumner står första raden överst; med kolumner är
    //           samma punkt HUVUDET, och raden står under det.
    check(t.pathAt(lx, in.y + 0.5f * rh) == glaze::TreePath{0},
          "KM1: utan kolumner — inget huvud", stig(t.pathAt(lx, in.y + 0.5f * rh)));
    t.setColumns({{"Namn", 100}, {"ms", 60, glaze::Align::Right}});
    check(t.pathAt(lx, in.y + 0.5f * rh).empty() &&
              t.pathAt(lx, in.y + hh + 0.5f * rh) == glaze::TreePath{0} &&
              t.pathAt(lx, in.y + hh + 1.5f * rh) == glaze::TreePath{0, 0},
          "KM1: med kolumner — huvudet överst, raderna under",
          stig(t.pathAt(lx, in.y + hh + 0.5f * rh)));

    // ---- KM2. Ett klick i kolumn 1:s rubrik SORTERAR stigande (idle 9,
    //           parse 12, draw 40), ett till vänder — och väljer ingen nod.
    const gfx::Rectf tr  = w.tradRect();
    const auto       fys = [&](float x, float y) {
        return gfx::Pointf{tr.x + x * w.s(), tr.y + y * w.s()};
    };
    const gfx::Pointf rubrik1 = fys(in.x + 100.0f + 30.0f, in.y + hh / 2.0f);
    w.nyFoljd();
    t.nolla();
    w.klick(rubrik1);
    check(t.sortColumn() == 1 && t.isSortAscending() &&
              t.pathAt(lx, in.y + hh + 1.5f * rh) == glaze::TreePath{0, 2} &&
              t.selectedPath().empty() && t.val == 0,
          "KM2: klick i rubriken sorterar — och väljer inget",
          std::to_string(t.sortColumn()) + " " +
              stig(t.pathAt(lx, in.y + hh + 1.5f * rh)));
    w.nyFoljd();
    w.klick(rubrik1);
    check(t.sortColumn() == 1 && !t.isSortAscending() &&
              t.pathAt(lx, in.y + hh + 1.5f * rh) == glaze::TreePath{0, 1},
          "KM2: ett klick till vänder riktningen",
          stig(t.pathAt(lx, in.y + hh + 1.5f * rh)));

    // ---- KM3. AVDELAREN i kolumn 0:s högra kant dras 40 px — bredden
    //           följer, och inget drag-and-drop startar ur huvudet.
    w.nyFoljd();
    const int slutFöre = w.slut;
    const gfx::Pointf avd = fys(in.x + 100.0f, in.y + hh / 2.0f);
    w.ner(avd);
    w.flytta(fys(in.x + 120.0f, in.y + hh / 2.0f));
    w.flytta(fys(in.x + 140.0f, in.y + hh / 2.0f));
    w.upp(fys(in.x + 140.0f, in.y + hh / 2.0f));
    check(std::abs(t.columnWidth(0) - 140.0f) < 0.5f && w.slut == slutFöre &&
              t.sortColumn() == 1 && !t.isSortAscending(),
          "KM3: avdelaren drar bredden, inget drag och ingen sortering",
          std::to_string(t.columnWidth(0)));

    // ---- KM4. Kolumnerna bort — huvudet bort, raderna överst igen.
    t.setColumns({});
    t.setSortColumn(-1);
    check(t.pathAt(lx, in.y + 0.5f * rh) == glaze::TreePath{0},
          "KM4: utan kolumner igen — inget huvud");
}

// ---- Q9. AUTOSKROLLEN MED EN RIKTIG PUMP: ungefär sex perioder i zonen ger
//          minst två steg, och efter släppet ger samma väntan inga fler —
//          fönstrets timer går bara medan något kan rulla (regel 1). Väntan
//          är appens EN-GÅNGS-timer, så pumpen sover emellan.
void fallAutoskrollPump()
{
    Vard w;
    w.setOffscreen(true);
    Trad& t = w.trad;
    std::vector<glaze::TreeNode> noder;
    for (int i = 0; i < 20; ++i)
        noder.push_back({"N" + std::to_string(i)});
    t.setNodes(std::move(noder));

    bool         kordes = false;
    float        start  = 0.0f;
    float        steg1  = 0.0f;
    glaze::Timer vänta;
    vänta.setSingleShot(true);
    vänta.setInterval(6 * w.style().dragScrollMs);
    int fas = 0;
    vänta.onTick = [&] {
        if (fas == 0) {
            steg1 = t.scrollY();
            check(steg1 - start >= 2.0f * w.style().dragScrollStep - 1e-3f,
                  "Q9: sex perioder i zonen ger minst två steg",
                  std::to_string(steg1 - start));
            w.upp({w.tradRect().x + 40.0f, w.tradRect().y + 40.0f});
            fas = 1;
            vänta.start();
            return;
        }
        check(!w.isDragging() && t.scrollY() == steg1,
              "Q9: efter släppet — inga fler steg", std::to_string(t.scrollY()));
        w.close();
    };
    glaze::app().post([&] {
        kordes = true;
        const glaze::Ctx c  = t.behaviorCtx();
        const gfx::Rectf in = t.innerBounds(c);
        const gfx::Rectf r  = w.tradRect();
        const float      x  = r.x + (in.x + 30.0f) * w.s();
        w.starta({x, r.y + (in.y + w.style().listRowH / 2.0f) * w.s()});
        w.flytta({x, r.y + (in.y + in.h - w.style().dragScrollZone / 2.0f) *
                               w.s()});
        start = t.scrollY();
        vänta.start();
    });
    w.run();
    if (!kordes) {
        std::printf("  --   Q9 hoppades över: inget fönster kunde skapas\n");
        ++g_skips;
    }
}

void fallMedFonster()
{
    Vard w;
    w.setOffscreen(true);

    bool kordes = false;
    glaze::app().post([&] {
        kordes = true;
        Trad& t = w.trad;

        w.nyFoljd();
        // ---- 9. MUSEN: pilen fäller UTAN att välja, raden VÄLJER.
        bygg(t);
        t.nolla();
        w.klick(w.pil(0, 0));
        check(t.isExpanded({0}) && t.ut == 1 && t.val == 0 &&
                  t.selectedPath().empty(),
              "9: klick på pilen fäller ut — och väljer inget");
        w.klick(w.radMitt(2, 1));   // A2 (rad 2 när A är utfälld)
        check(t.selectedText() == "A2" && t.val == 1,
              "9: klick på raden väljer", t.selectedText());
        w.nyFoljd();   // ett ENKELT klick — inte ett dubbelklick
        w.klick(w.radMitt(2, 1));
        check(t.val == 1, "9: klick på den redan valda raden — inget event");

        w.nyFoljd();
        // ---- 10. IHOPFÄLLNINGEN SOM GÖMMER DET VALDA flyttar valet upp,
        //          och eventen kommer i ordningen collapse → change.
        t.nolla();
        w.klick(w.pil(0, 0));
        check(!t.isExpanded({0}) && t.selectedText() == "A" &&
                  t.logg == std::vector<std::string>{"collapse", "change"},
              "10: valet flyttar till den hopfällda noden",
              t.selectedText() + " [" +
                  (t.logg.empty() ? std::string() : t.logg.front()) + "…]");

        w.nyFoljd();
        // ---- 11. DEN INBYGGDA LASTEN: nodens stig + text, sedd av appen.
        bygg(t);
        t.setExpanded({0}, true);   // A A1 A2 B C
        t.nolla();
        w.slut = 0;
        w.starta(w.radMitt(4, 0));   // C
        check(w.isDragging(), "11: draget från en nod startar");
        check(w.last == "2" && w.lastText == "C",
              "11: lasten bär stigen i kNodeFormat och texten i text/plain",
              "\"" + w.last + "\" / \"" + w.lastText + "\"");
        w.tangent(gfx::Key::Escape);
        check(!w.isDragging() && t.nodeText({2}) == "C" && w.slut == 1 &&
                  w.slutEffekt == glaze::DragEffect::None,
              "11: Escape — ingen flytt, källan får None");
        w.upp(w.radMitt(4, 0));

        w.nyFoljd();
        // ---- 12. OMORDNING — IN I, FÖRE, EFTER.
        w.starta(w.radMitt(4, 0));   // C
        w.flytta(w.radMitt(3, 0));   // mitten av B ⇒ IN I B
        check(w.overParent == glaze::TreePath{1} && w.overIndex == 0 &&
                  t.dropEffect() == glaze::DragEffect::Move,
              "12: mitten av en rad är IN I noden — och tas emot som Move",
              stig(w.overParent) + " #" + std::to_string(w.overIndex));
        w.upp(w.radMitt(3, 0));
        check(t.nodeCount({}) == 2 && t.nodeText({1, 0}) == "C" &&
                  t.isExpanded({1}),
              "12: C blev B:s barn, och B fälldes ut så det syns");

        bygg(t);   // A(A1 A2) B C(C1), hopfällt — raderna A B C
        w.starta(w.radMitt(2, 0));            // C
        w.flytta(w.radKant(0, 0, true));      // ÖVERKANTEN av A ⇒ FÖRE A
        check(w.overParent.empty() && w.overIndex == 0,
              "12: överkanten är FÖRE noden",
              stig(w.overParent) + " #" + std::to_string(w.overIndex));
        w.upp(w.radKant(0, 0, true));
        check(t.nodeText({0}) == "C" && t.nodeText({1}) == "A",
              "12: C står nu först", t.nodeText({0}));

        bygg(t);
        w.starta(w.radMitt(0, 0));            // A
        w.flytta(w.radKant(1, 0, false));     // UNDERKANTEN av B ⇒ EFTER B
        check(w.overParent.empty() && w.overIndex == 2,
              "12: underkanten är EFTER noden (FÖRE-flytten-index)",
              stig(w.overParent) + " #" + std::to_string(w.overIndex));
        w.upp(w.radKant(1, 0, false));
        check(t.nodeText({0}) == "B" && t.nodeText({1}) == "A" &&
                  t.nodeText({2}) == "C",
              "12: A står efter B",
              t.nodeText({0}) + t.nodeText({1}) + t.nodeText({2}));

        w.nyFoljd();
        // ---- 13. ETT MÅL INUTI DEN DRAGNA NODEN avvisas.
        bygg(t);
        t.setExpanded({0}, true);   // A A1 A2 B C
        w.slut = 0;
        w.starta(w.radMitt(0, 0));   // A
        w.flytta(w.radMitt(1, 1));   // mitten av A1 ⇒ IN I A1, A:s eget barn
        check(t.dropEffect() == glaze::DragEffect::None,
              "13: in i den draga nodens eget barn — AVVISAT");
        w.flytta(w.radMitt(0, 0));   // in i A själv
        check(t.dropEffect() == glaze::DragEffect::None,
              "13: in i noden själv — AVVISAT");
        w.upp(w.radMitt(0, 0));
        check(t.nodeCount({0}) == 2 && t.nodeText({0}) == "A" &&
                  w.slutEffekt == glaze::DragEffect::None,
              "13: ingenting flyttades, källan fick None");

        w.nyFoljd();
        // ---- 14. FLYTT TILL ETT ANNAT MÅL tar bort noden; en KOPIA inte.
        bygg(t);   // A B C
        t.setSelectedPath({1});
        w.svar = glaze::DragEffect::Move;
        const gfx::Pointf M{w.mal.resolve(w.mal.behaviorCtx()).x + 20.0f,
                            w.mal.resolve(w.mal.behaviorCtx()).y + 20.0f};
        w.starta(w.radMitt(1, 0));   // B
        w.flytta(M);
        w.upp(M);
        check(t.nodeCount({}) == 2 && t.nodeText({1}) == "C" &&
                  w.slutEffekt == glaze::DragEffect::Move &&
                  t.selectedPath().empty(),
              "14: Move till ett annat mål — B LÄMNADE trädet (och valet)",
              std::to_string(t.nodeCount({})) + " noder");
        w.svar = glaze::DragEffect::Copy;
        w.starta(w.radMitt(0, 0));   // A
        w.flytta(M);
        w.upp(M);
        check(t.nodeCount({}) == 2 && t.nodeText({0}) == "A" &&
                  w.slutEffekt == glaze::DragEffect::Copy,
              "14: Copy till ett annat mål — noden står kvar");

        w.nyFoljd();
        // ---- 18. SLÄPPREGELN PER NOD: B säger nej till "in i" —
        //          mittzonen delas mellan före och efter, och ingen plats
        //          pekar IN I B (markeringen ramar aldrig in den: `into`
        //          sätts bara med föräldern = raden själv).
        bygg(t);   // A B C, hopfällt
        t.setAllowDropInto({1}, false);
        w.starta(w.radMitt(2, 0));               // C
        w.flytta(w.radAndel(1, 0, 0.4f));        // B:s mitt, övre halvan
        check(w.overParent.empty() && w.overIndex == 1 &&
                  t.dropEffect() == glaze::DragEffect::Move,
              "18: övre halvan av B:s mitt — FÖRE B, inte in i den",
              stig(w.overParent) + " #" + std::to_string(w.overIndex));
        w.flytta(w.radAndel(1, 0, 0.6f));        // …nedre halvan
        check(w.overParent.empty() && w.overIndex == 2 &&
                  t.dropEffect() == glaze::DragEffect::Move,
              "18: nedre halvan — EFTER B",
              stig(w.overParent) + " #" + std::to_string(w.overIndex));
        w.flytta(w.radAndel(1, 0, 0.4f));
        w.upp(w.radAndel(1, 0, 0.4f));
        check(t.nodeCount({}) == 3 && t.nodeText({1}) == "C" &&
                  t.nodeText({2}) == "B" && t.nodeCount({2}) == 0,
              "18: släppt på B:s mitt — C hamnade FÖRE B, inte under den",
              t.nodeText({0}) + t.nodeText({1}) + t.nodeText({2}));
        w.starta(w.radMitt(0, 0));               // A …
        w.flytta(w.radMitt(1, 0));               // …in i C, som tar emot
        check(w.overParent == glaze::TreePath{1} &&
                  t.dropEffect() == glaze::DragEffect::Move,
              "18: en nod som tar emot har kvar sin mittzon",
              stig(w.overParent));
        w.tangent(gfx::Key::Escape);
        w.upp(w.radMitt(1, 0));

        w.nyFoljd();
        // ---- 18b. En UTFÄLLD nod som säger nej, med barn lagda med kod:
        //           "efter" under den är platsen EFTER noden (inte först
        //           bland barnen), och före/efter ett av barnen finns inte —
        //           det vore också in i noden.
        bygg(t);
        t.setAllowDropInto({0}, false);
        t.setExpanded({0}, true);                // A A1 A2 B C
        w.slut = 0;
        w.starta(w.radMitt(4, 0));               // C
        w.flytta(w.radKant(0, 0, false));        // underkanten av A
        check(w.overParent.empty() && w.overIndex == 1,
              "18b: under en utfälld nod som säger nej — EFTER noden",
              stig(w.overParent) + " #" + std::to_string(w.overIndex));
        w.flytta(w.radKant(1, 1, true));         // före A1 — in i A
        check(w.overIndex == -1 && w.overParent.empty() &&
                  t.dropEffect() == glaze::DragEffect::None,
              "18b: före ett barn till en nod som säger nej — INGEN plats",
              stig(w.overParent) + " #" + std::to_string(w.overIndex));
        w.upp(w.radKant(1, 1, true));
        check(t.nodeCount({0}) == 2 && t.nodeCount({}) == 3 &&
                  t.nodeText({2}) == "C" &&
                  w.slutEffekt == glaze::DragEffect::None,
              "18b: släppt där — ingenting flyttades, källan fick None");

        w.nyFoljd();
        // ---- 15. ETT TRYCK PÅ PILEN blir aldrig ett drag.
        bygg(t);
        w.starta(w.pil(0, 0));
        check(!w.isDragging(), "15: tryck på pilen + rörelse — inget drag");
        w.upp(w.pil(0, 0));

        // ---- 16. DUBBELKLICKET (andra trycket inom OS:ets tid) — varje
        //          delfall börjar med en ny följd.
        auto logg = [&t] {
            std::string u;
            for (const std::string& h : t.logg)
                u += (u.empty() ? "" : ",") + h;
            return u;
        };
        bygg(t);   // A(A1 A2) B C(C1), hopfällt — raderna A B C
        w.nyFoljd();
        t.nolla();
        w.klick(w.radMitt(0, 0));   // A — väljer
        w.klick(w.radMitt(0, 0));   // …andra trycket ⇒ dubbelklick
        check(t.isExpanded({0}) &&
                  t.logg == std::vector<std::string>{"change", "expand",
                                                     "activate"},
              "16: på en nod MED barn — vald, utfälld, aktiverad, i den "
              "ordningen",
              logg());
        w.nyFoljd();
        t.nolla();
        w.klick(w.radMitt(1, 1));   // A1 (A utfälld: A A1 A2 B C)
        w.klick(w.radMitt(1, 1));
        check(t.selectedText() == "A1" &&
                  t.logg == std::vector<std::string>{"change", "activate"},
              "16: på ett LÖV — vald och aktiverad, inget att fälla", logg());
        w.nyFoljd();
        t.nolla();
        w.klick(w.pil(0, 0));   // fäller ihop A (A1 gömdes ⇒ valet till A)
        w.klick(w.pil(0, 0));   // andra trycket fäller ut igen
        check(t.isExpanded({0}) && t.akt == 0 && t.ihop == 1 && t.ut == 1,
              "16: på PILEN — två fällningar och INGEN aktivering", logg());

        // ---- 19–29 med musen — sist, så de äldre
        //      fallens tillstånd är orörda.
        fallFlervalMus(w);
        fallKryssMus(w);
        fallDragFlera(w);
        // ---- 31–34, 36, 38 — appens godkännande.
        fallGodkannande(w);
        // ---- Q1–Q8 — snabbmenyn och autoskrollen.
        fallSnabbmeny(w);
        // ---- KM1–KM4 — kolumnhuvudet med musen.
        fallKolumnerMus(w);

        w.close();
    });
    w.run();

    if (!kordes) {
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

    std::printf("tree_gate\n");
    fallKollektion();
    fallFlytt();             // FÖRE fall 3: ett trasigt index syns som FEL
    fallValetFoljerNoden();  // innan fall 3 kraschar på det (se filhuvudet)
    fallAnvandareMotProgram();
    fallSlappregeln();
    fallFlervalAv();
    fallFlervalTangenter();
    fallFlervalProgram();
    fallValetFoljerFlytten();
    fallKryssen();
    fallFlyttaFlera();
    fallKryssadeStigar();
    fallMassformen();
    fallKolumner();
    fallFargen();
    fallBildUrPixlar();
    fallBreddEfterInnehall();
    fallMedFonster();
    fallAutoskrollPump();

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
