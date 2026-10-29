#pragma once
#include <functional>
#include <limits>
#include <utility>
#include <vector>

#include <gfx/geometry.h>

#include "glaze/widget.h"

// INTERN header (`sash.h`-prejudikatet) — DOCKNINGENS MODELL:
// strukturen och geometrin ur den. Konsumenten är `DockHost`; en app har
// inget ärende hit.
//
// STRUKTUREN är KANT → REMSA → CELL → FLIK, inte ett generellt
// binärträd:
//
//   remsa  en kant och en ordnad lista av celler LÄNGS kanten. Remsorna
//          ligger i GLOBAL skärordning, yttersta först.
//   cell   en flikgrupp: ordnade paneler, en aktiv panel och en vikt (cellens
//          andel av remsans längd).
//
// Varje panel hör till exakt EN kant, så ett infällt handtag har en entydig
// plats, och layoutsträngen förblir platt (`dock_string.h`). Förkastat:
// godtycklig nästling av delningar (ImGui/VS) — en rekursiv layout och en
// nästlad sträng för fall som inte behövs; formatet är versionerat, så
// ett `v3` med nästling bryter inget API.
//
// FÄLTEN ÄR SANNINGEN FÖR KANT OCH TJOCKLEK: `panel.dock` och
// `width`/`height` läses. Strukturen är sanningen för GRUPPERING,
// ORDNING och cellernas VIKTER. Operationerna här rör ALDRIG en panels fält —
// värden skriver tillbaka `dock` efter en verklig ändring — och därför går
// samma operationer att köra på en KOPIA för indikatorns simulering:
// det man ser är det man får.
//
// GEOMETRIN (`computeLayout`) är LOGISKA px i värdens egen rymd, origo i
// värdens hörn, och EN sanning för ritningen, placeringen, träffprovet,
// skiktdragen och indikatorn. Panelernas tillstånd läses genom deras publika
// getters (öppen, infälld, mått, min/max, titel, ikon); inga fönster.
//
// KNAPPNÅLEN: bara en NÅLAD, utfälld panel står i sin remsa. En
// onålad panel är ett HANDTAG — också medan den är utfälld, för då är den
// OVERLAYN: den läggs ovanpå huvudytan vid sin kant och trycker inte undan
// något. Strukturen bär panelen i sin cell hela tiden, så knappnålen i
// overlayn lägger tillbaka den där den stod.
//
// VERKTYGSFÄLTEN står i BAND, och banden skärs YTTERST — före
// handtagen och panelremsorna. Fälten har en ORDNING i modellen (`bars`), och
// banden HÄRLEDS ur den: fälten vid samma kant grupperas på sin BANDNYCKEL.
// Nyckeln och kanten är fältens `band()`/`dock` (fälten vann, som för en
// panels `dock`), så previewn — som sätter fälten efter trädbygget — och
// appen — som sätter dem före — får samma band ur samma tal och samma
// adoptionsordning. Värden skriver tillbaka kant och PACKAT index efter en
// användarändring.

namespace glaze {
class DockPanel;
class ToolBar;
}

namespace glaze::detail {

// ---------------------------------------------------------- strukturen

struct DockCell {
    std::vector<DockPanel*> tabs;             // flikordningen
    DockPanel*              active = nullptr; // den valda fliken
    float                   weight = 1.0f;    // andelen av remsans längd, > 0
};

struct DockStrip {
    Dock                  edge = Dock::Left;
    std::vector<DockCell> cells;   // LÄNGS kanten: uppifrån (stående remsa)
                                   // eller från vänster (liggande)
};

// Ett VERKTYGSFÄLT i modellen: dess kant och BANDNYCKEL. Nycklarna får
// vara glesa (appen satte `setBand(5)`) — banden packas till index 0, 1, 2 …
// när de härleds.
struct DockBar {
    ToolBar* bar  = nullptr;
    Dock     edge = Dock::Top;
    int      key  = 0;
};

// Ett HÄRLETT band: kanten, dess index (0 = yttersta), nyckeln och fälten i
// modellens ordning — dolda fält med, så ett index inte flyttar när ett fält
// göms.
struct DockBand {
    Dock                  edge  = Dock::Top;
    int                   index = 0;
    int                   key   = 0;
    std::vector<ToolBar*> bars;
};

// Left/Top/Right/Bottom — de fyra kanterna en panel kan dockas vid.
bool isDockEdge(Dock d);
// En STÅENDE remsa (Left/Right): tjockleken är en bredd, cellerna staplas
// uppifrån och ned.
bool isStandingEdge(Dock d);

// Står panelen DOCKAD OCH UTFÄLLD i sin remsa — öppen, NÅLAD och inte
// infälld? En onålad utfälld panel är overlayn och står i ingen remsa.
bool docksOpen(const DockPanel* p);

// Fliken cellen VISAR: den aktiva om den står dockad och utfälld
// (`docksOpen`), annars den första som gör det. nullptr = cellen syns inte
// (alla flikar stängda, infällda eller onålade). En sanning för värdens
// tangentbord och panelens `showsChildren`; geometrin räknar samma regel med
// förhandsvisningarna inräknade (`computeLayout`).
DockPanel* shownTab(const DockCell& c);
// Dockade, utfällda flikar — fler än en ger cellen en flikrad.
int openTabs(const DockCell& c);

// ---------------------------------------------------------- talen
//
// NUMERISK SÄKERHET: en vikt
// ur strängen är vilket ändligt positivt float som helst (3e38 lika väl som
// 1e-38), och ett panelmått är ett RÅTT layoutfält som appen kan sätta till
// −1, NaN eller ∞. Reglerna här är EN sanning för användningen
// (`computeLayout`), operationerna och sparningen (`DockHost::layout`).

// Den MINSTA vikt en normalisering lämnar (den största = 1). Ett TALOMFÅNG,
// inget mått — en så liten cell har ändå ingen synlig längd: fyra gånger det
// minsta NORMALA floatet, så att vikten förblir normal efter en halvering
// (`dockOn`) och `std::to_chars`/`from_chars` skriver och läser den exakt —
// en subnormal vikt hade en läsare kunnat avvisa som utanför intervallet.
inline constexpr float kMinWeight = 4.0f * std::numeric_limits<float>::min();

// Remsans vikter NORMALISERADE: den största blir 1 och ingen blir mindre än
// `kMinWeight`, räknat i double. Kvoterna står kvar (utom för en cell som
// lyfts till golvet). En ogiltig vikt (≤ 0, icke ändlig) blir 1 — den kan
// inte uppstå, men normaliseringen är skyddet.
void normalizeWeights(DockStrip& s);

// VIKTERNA som ger remsans SYNLIGA celler längderna `lengths` (modellindex
// → logiska px): proportionella mot längderna och i SAMMA skala som remsans
// gamla vikter (de synligas summa står kvar), så att en DOLD cell — alla
// flikar stängda eller infällda — behåller sin andel när den kommer tillbaka.
// Normaliserade som ovan. En längd som inte är > 0 går inte att återskapa
// med en positiv vikt; då är svaret tomt (skiktdraget).
std::vector<float> weightsForLengths(
    const DockStrip& s, const std::vector<std::pair<int, float>>& lengths);

// PANELENS MÅTT som modellen och strängen läser det: ett ändligt tal > 0,
// annars 0 = AUTO (`dockDefaultSize`). Samma regel vid användning och
// sparning, så skrivaren aldrig emitterar ett mått tolkaren avvisar och en
// återläst layout ser ut som den sparade.
float panelSize(float v);

class DockModel {
public:
    std::vector<DockStrip> strips;   // skärordning, yttersta först
    std::vector<DockBar>   bars;     // verktygsfältens ordning

    // ADOPT-REGELN: panelen blir INNERSTA remsa vid `edge` (sist i
    // skärordningen) — platsen för en färsk panel. No-op när den redan
    // finns eller kanten inte är en kant.
    void add(DockPanel& p, Dock edge);

    // Panelen lämnar sin cell. Var den aktiv blir grannen till höger aktiv
    // (den till vänster när den var sist); en tom cell och en tom remsa tas
    // bort. Svarar om panelen fanns. PEKAREN JÄMFÖRS och derefereras aldrig
    // — så formen med pekare tar också bort en panel som redan dött (en
    // flytande panel på väg tillbaka).
    bool remove(const DockPanel& p) { return remove(&p); }
    bool remove(const DockPanel* p);

    // YTTERST vid kanten — först i skärordningen, över hela kantens längd.
    // Svarar false när ingenting ändrades (redan där, redan ensam ytterst).
    bool dockAtEdge(DockPanel& p, Dock edge);

    // DOCKA PÅ EN PANEL (fem utfall):
    //   Fill            en flik i målets cell, sist och aktiv
    //   längs remsan    en ny cell före/efter målets (Top/Bottom vid en
    //                   stående remsa, Left/Right vid en liggande); målets
    //                   vikt delas lika mellan de två — efter att remsans
    //                   vikter normaliserats, så halveringen aldrig
    //                   underflödar till 0
    //   tvärs remsan    en ny remsa vid samma kant, direkt utanför (mot
    //                   kanten) eller innanför målets
    // Svarar false när ingenting ändrades — `p` redan i målets cell vid
    // Fill, `p == mål`, målet inte dockat, `None`, eller en struktur som
    // blev densamma.
    bool dockOn(DockPanel& p, DockPanel& target, Dock where);

    // Gör `p` till den aktiva fliken i sin cell. Svarar om något ändrades.
    bool activate(DockPanel& p);

    // FÄLTEN VANN: en panel vars `dock` inte längre är remsans kant —
    // appen skrev det råa fältet efter adoptionen — lämnar sin cell och blir
    // innersta remsa vid den nya kanten, i modellens ordning; utan kant
    // lämnar den modellen (den får mitten). En FLYTANDE panel lämnar
    // modellen helt — den står i ett eget fönster — och likaså ett FLYTANDE
    // verktygsfält. Ett verktygsfält får sin kant och
    // nyckel ur `dock`/`band()`, och platsen i ordningen står kvar — också
    // utan kant. Svarar om något ändrades.
    bool normalize();

    // ---- verktygsfälten
    // ADOPTIONEN: sist i ordningen med kant och nyckel ur fälten — alltså
    // sist i sitt band. ALLA verktygsfält står i ordningen, också de utan
    // kant (huvudytans): får ett sådant en kant senare har det kvar sin
    // adoptionsplats. No-op när det
    // redan finns — eller FLYTER (ett flytande fält har inget band).
    void addBar(ToolBar& b);
    // Fältet lämnar ordningen. PEKAREN JÄMFÖRS och derefereras aldrig — så
    // formen med pekare tar också bort ett fält som redan dött (ett flytande
    // fält på väg hem).
    bool removeBar(const ToolBar& b) { return removeBar(&b); }
    bool removeBar(const ToolBar* b);
    bool containsBar(const ToolBar& b) const;
    // Står fältet i ett BAND — i modellen och med en kant?
    bool barInBand(const ToolBar& b) const;
    // Banden vid en kant, yttersta först.
    std::vector<DockBand> bandsAt(Dock edge) const;
    // FLYTTA ett fält (greppdraget): till bandet där `inBandOf` står (ett
    // annat fält), före `before` (nullptr = sist i bandet) — eller, med
    // `inBandOf` nullptr, till ett NYTT innersta band vid `edge`. Ett fält som
    // SAKNAS i modellen — ett flytande fält som dockas med ett drag — SÄTTS IN
    // där på samma sätt (en flytt eller en insättning). Nycklarna packas
    // efteråt. Svarar false när formen blev densamma (och lämnar då modellen
    // orörd).
    bool moveBar(ToolBar& b, Dock edge, const ToolBar* inBandOf,
                 const ToolBar* before);
    // Nycklarna packade till bandens index (0, 1, 2 … per kant).
    void packBarKeys();

    bool             contains(const DockPanel& p) const;
    DockCell*        cellOf(const DockPanel& p);
    const DockCell*  cellOf(const DockPanel& p) const;
    int              stripIndexOf(const DockPanel& p) const;   // -1 = ingen
    // Alla paneler i strukturens ordning (remsa, cell, flik).
    std::vector<DockPanel*> panels() const;
    // Samma remsor, kanter, flikar och aktiva — vikterna räknas inte — och
    // samma band med samma fält i samma ordning. En operation som lämnar
    // formen orörd är ingen ändring.
    bool sameShape(const DockModel& o) const;

private:
    void prune();   // tomma celler och remsor bort, aktiva pekare giltiga
};

// ------------------------------------------------------------ geometrin

// FÖRHANDSVISNINGARNA — det modellen inte äger men layouten ska visa medan
// ett drag pågår. Index i MODELLEN; -1 = inget drag.
//
// CELLSKIKTETS förhandsvisning är VIKTER och inte längder:
// layouten räknas ur de vikter släppet skriver, så
// förhandsvisningen är en SIMULERING — en längd som positiva
// vikter inte återskapar kan aldrig visas.
struct DockLive {
    int   strip      = -1;     // remsskiktets drag: remsan …
    float stripThick = 0.0f;   //   … och dess tjocklek (logiska px)
    int   cellStrip  = -1;     // cellskiktets drag: remsan …
    std::vector<float> cellWeights;   // … och ALLA dess cellers vikter

    // GLIDNINGEN — ANDELEN 0–1 av den fulla tjockleken, formen
    // "VÄXER": panelens yta följer tjockleken och huvudytan följer med.
    // `grow` är en remsa som kommer eller går med SIN ENDA dockade panel:
    // panelen läggs ut dockad också när den just fällts in (den glider in),
    // och remsans tjocklek och skikt skalas med andelen.
    std::vector<std::pair<const DockPanel*, float>> grow;
    // OVERLAYN: panelen som ligger OVANPÅ huvudytan vid sin kant — den
    // öppna, eller en som glider in — och dess andel.
    DockPanel*       overlay         = nullptr;
    float            overlayFraction = 1.0f;
    // SNAPPENS INFÄLLNING: panelerna förhandsvisningen visar INFÄLLDA —
    // remsans alla vid ett remsskikt, cellens flikar vid ett cellskikt. De
    // blir handtag i förhandsvisningen precis som efter släppet.
    std::vector<const DockPanel*> collapsed;
    // RUBRIKDRAGETS SIMULERING: den dragna panelen läggs ut nålad och
    // utfälld — så blir den när den släpps (en overlay som dras dockas).
    const DockPanel* dragged = nullptr;
};

// HANDTAGEN i sin ordning — kanterna Top, Bottom, Left, Right, och inom en
// kant modellens ordning: de ÖPPNA panelerna som är INFÄLLDA eller ONÅLADE (en
// onålad panels handtag står kvar medan dess overlay är öppen — där räknas
// pekaren över det som "inne"). Med `live` också förhandsvisningens: snappens
// infällda blir handtag, och den dragna panelen är inget. En sanning för
// geometrin och värdens tangentbordslista (som frågar utan `live`).
std::vector<std::pair<Dock, DockPanel*>>
handlesInOrder(const DockModel& m, const DockLive* live = nullptr);

struct DockGeometry {
    struct Strip {
        int        index = -1;           // remsan i modellen
        Dock       edge  = Dock::Left;
        gfx::Rectf rect{};               // remsans yta, alla celler
        gfx::Rectf sash{};               // skiktet mot resten
        gfx::Rectf before{};             // ytan som återstod när remsan skars
                                         // — dragets gräns och yttre kant
        float      floor   = 0.0f;       // dragets golv: panelernas minsta
                                         // mått, `dockHeaderH` utan ett satt
        float      maxThick = 0.0f;      // panelernas största; 0 = ingen
    };
    struct Cell {
        int        strip = -1;           // remsan i modellen
        int        cell  = -1;           // cellen i remsan (modellindex)
        gfx::Rectf rect{};               // hela cellen
        gfx::Rectf body{};               // den visade panelens yta
        gfx::Rectf tabRow{};             // tom utan flikrad
        DockPanel* shown = nullptr;
        std::vector<std::pair<DockPanel*, gfx::Rectf>> tabs;   // flikraden
        float      floor  = 0.0f;        // dragets golv längs remsan
        float      maxLen = 0.0f;        // största längd längs remsan; 0 = ingen
    };
    struct CellSash {
        int        before = -1;          // index i `cells`
        int        after  = -1;
        gfx::Rectf rect{};
    };
    struct Handle {
        DockPanel* panel = nullptr;
        Dock       edge  = Dock::Left;
        gfx::Rectf rect{};
    };
    struct HandleStrip {
        Dock       edge = Dock::Left;
        gfx::Rectf rect{};
    };
    struct Band {
        Dock       edge  = Dock::Top;
        int        index = 0;                // bandets index vid kanten
        gfx::Rectf rect{};                   // hela bandet, längs hela kanten
        std::vector<std::pair<ToolBar*, gfx::Rectf>> bars;   // de synliga
    };
    // OVERLAYN: ovanpå huvudytan och remsorna vid panelens kant,
    // innanför handtagen — den ändrar ingen annan yta.
    struct Overlay {
        DockPanel* panel = nullptr;
        Dock       edge  = Dock::Left;
        gfx::Rectf rect{};
    };

    std::vector<Band>        bands;         // bara band med ett synligt fält
    // Zonen där ett NYTT innersta band skapas vid kanten (Top, Bottom, Left,
    // Right i den ordningen): `dockDropZone` innanför bandområdets innerkant.
    gfx::Rectf               newBandZone[4]{};
    std::vector<Strip>       strips;        // bara remsor med en synlig cell
    std::vector<Cell>        cells;         // bara synliga celler
    std::vector<CellSash>    cellSashes;
    std::vector<HandleStrip> handleStrips;  // en per kant med handtag
    std::vector<Handle>      handles;
    Overlay                  overlay;       // tom panel = ingen overlay
    gfx::Rectf               center{};      // HUVUDYTAN — en overlay
                                            // trycker inte undan den

    // Panelens yta — overlayns för overlaypanelen, annars cellens; tom när
    // den inte är placerad (stängd, infälld, en inaktiv flik, eller i en
    // remsa utan yta).
    gfx::Rectf  rectOf(const DockPanel* p) const;
    const Cell* cellShowing(const DockPanel* p) const;
    // Verktygsfältets yta — tom när det inte står i ett band (dolt, ingen kant).
    gfx::Rectf  barRectOf(const ToolBar* b) const;
};

// Ett VERKTYGSFÄLTS mått om det stod liggande (`vertical` false) eller
// stående — logiska px, i den riktningens axlar. Värden mäter (ett fält är en
// widget, och dess mätning är skyddad), och simuleringen frågar efter
// riktningen hos det band fältet skulle hamna i. `minLength` är den
// KORTASTE längd fältet tål i bandet: med överflödet på greppet, luften och
// överflödsknappen (aldrig mer än `size`s längd), annars hela längden.
struct BarExtent {
    gfx::Sizef size;
    float      minLength = 0.0f;
};
using BarSize = std::function<BarExtent(const ToolBar&, bool vertical)>;

// Kanternas ordning i skärningen — banden skärs Top, Bottom, Left, Right.
inline constexpr Dock kBandEdges[4] = {Dock::Top, Dock::Bottom, Dock::Left,
                                       Dock::Right};

// LAYOUTEN i värdens yta `b` (logiska px, origo i värdens hörn). Skärordning:
// BANDEN (verktygsfälten — Top, Bottom, Left, Right, band 0 ytterst;
// tjockleken är det tjockaste synliga fältets, och fälten packas i ordning med
// `toolGap`, var och ett i sin egen längd — ryms de inte KRYMPER de bakifrån
// ned till sin kortaste längd (`BarExtent::minLength`), och det som
// ändå inte ryms klipps vid bandets slut),
// HANDTAGSREMSORNA (en per kant med infällda eller onålade paneler, utanför
// alla panelremsor), PANELREMSORNA i modellens ordning med ett skikt mot
// resten, och sist HUVUDYTAN — det som blir kvar. OVERLAYN skär inget:
// den läggs vid sin kant i ytan innanför handtagen, i panelens föredragna
// tjocklek klampad mot dess min/max och mot ytan, gånger andelen.
//
// REMSANS TJOCKLEK är det största föredragna måttet bland dess öppna,
// utfällda paneler (inaktiva flikar inräknade, så ett flikbyte inte ändrar
// den; AUTO = `dockDefaultSize`), klampad mot panelernas min/max (min =
// det största av minimimåtten, max = det minsta av maximimåtten, och min
// vinner när de krockar). Gränserna är MJUKA mot utrymmet: huvudytan
// krymper till 0 först, sedan klipps remsorna i skärordning.
//
// CELLERNAS LÄNGDER fördelas efter vikt och klampas mot panelernas min/max
// längs remsan, med upprepad omfördelning av resten mellan de oklampade
// (högst ett varv per cell). Längs remsan finns ingen huvudyta som tar ett
// överskott: når alla celler sitt största mått fördelas resten ändå efter
// vikt, så remsan alltid fylls; ryms inte minimimåtten klipps cellerna i
// ordning. Fördelningen räknas i DOUBLE med vikterna relativt den största
//, och längderna är ändliga och ryms i remsan.
//
// `c` ger stilen och fonten (handtagens textbredd), `barSize` verktygsfältens
// mått; allt annat läses ur panelerna.
DockGeometry computeLayout(const DockModel& m, const gfx::Rectf& b,
                           const Ctx& c, const DockLive& live,
                           const BarSize& barSize);

// DRAGTRÖSKELN för dockningens fyra drag — panelens rubrik, värdens flik,
// verktygsfältets grepp och det flytande fältets rubrik: har pekaren rört sig
// MER än `dragThreshold` från trycket? FYSISKA px i krokens Ctx, euklidiskt —
// drag-and-drops regel (`AppWindow::dragThresholdPassed`).
inline bool pastDragThreshold(const Ctx& c, float pressX, float pressY,
                              float x, float y)
{
    const float tol = c.style->dragThreshold * c.scale;
    const float dx  = x - pressX;
    const float dy  = y - pressY;
    return dx * dx + dy * dy > tol * tol;
}

} // namespace glaze::detail
