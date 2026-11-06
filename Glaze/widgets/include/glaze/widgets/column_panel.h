#pragma once
#include <optional>
#include <string>
#include <vector>

#include "glaze/widgets/checkbox.h"
#include "glaze/widgets/panel.h"

// ColumnPanel — KOLUMNHUVUDET som delad maskin:
// kolumnerna, huvudets geometri och ritning, avdelardraget, klicket som
// sorterar och sorteringens TILLSTÅND (kolumn, riktning, sorterbar).
// `Table` (ListView, DataGrid) och
// `TreeView` ärver den, så en rubrik, en sorteringspil och en dragen avdelare
// är EN kod i alla tre. MÖNSTER: widgeten ritar SIG SJÄLV, men ColumnPanel
// ritar bara sin DEL (`drawHeader`) — subklassen har sin `draw()` med sin
// `switch (c.ui)` runt den, `Table`s prejudikat. Registreras inte, och
// konstruktorn är skyddad: ett huvud utan rader har ingen handling att
// erbjuda.
//
// VAD SOM HÖR TILL VEM: basen äger KOLUMNERNA och huvudet och vet att en
// sortering gäller (`sortColumn`), men inte VAD som sorteras — raderna är
// subklassens. När ordningen ändrats (en ny sorteringskolumn, en ny riktning,
// en kolumn som tillkom) anropas kroken `orderChanged`, och subklassen räknar
// om sin VISNINGSORDNING: tabellen sin permutation, trädet sina syskon. Den
// naturliga jämförelsen (`compareNatural`) är delad, så "fil2" står före
// "fil10" i varje lista med kolumner.
//
// KOLUMNHUVUDET PÅ FLERA RADER (rubriker som "Nedhäng / vertikal /
// projektion / b / (m)"). `\n` i en rubrik bryter alltid och resten bryts på
// ORDGRÄNS mot kolumnens bredd; ett ord som inte ryms på en egen rad kortas
// med "…" i stället för att klyvas, och blir raderna fler än huvudet rymmer
// bär den sista synliga en ellips (`Draw::elideLines`).
//   * HÖJDEN är per lista: `setHeaderLines(n)` ger n textrader, alltså
//     `tableHeaderH` + (n − 1) radhöjder — luften över och under förblir EN
//     rads, och raderna står en radhöjd isär som all radbruten text. 1
//     (standard) är huvudet som det alltid sett ut, pixel för pixel. 0 =
//     AUTO: lika många rader som den rubrik som behöver flest. AUTO räknar i
//     kolumnens textbredd UTAN sorteringspilen — ett klick i huvudet ska
//     aldrig ändra huvudets höjd — så pilens kolumn kan få korta sin sista
//     rad, precis som en enradsrubrik kortas när pilen kommer.
//   * JUSTERINGEN i sidled är per kolumn: `Column::headerAlign`, och en
//     kolumn utan den följer cellernas `align`. I höjdled är den
//     per lista (`setHeaderVAlign`): huvudets n rader är n PLATSER, och en
//     rubrik med färre rader läggs överst, i mitten eller nederst bland dem.
//     Rubriker på flera rader är vanligtvis centrerade och bottenjusterade.
//   * SORTERINGSPILEN står där en ENRADSrubrik hade stått — på första
//     platsen (Top), i mitten (Center) eller på den sista (Bottom). Den hör
//     till rubrikens kant och inte till huvudets mitt: i ett bottenjusterat
//     huvud står den vid varje rubriks sista rad. Pilens kolumn smalnar,
//     och alla dess rader bryts till vänster om pilen.
//
// KOLUMNENS LÄGE (`ColumnSizing`) bestämmer bredden:
//   Manual    den satta bredden (`width`; 0 = stilens `columnDefaultW`).
//   AutoMin   bredden FÖLJER innehållet: rubrikens bredaste rad och cellernas
//             bredaste innehåll, det bredaste av dem med cellens luft på båda
//             sidor och en hårlinjes andrum — ritningen kortar med ett strikt
//             `<=` och flyttalen ger inte alltid måttet tillbaka —, aldrig
//             smalare än `columnMinW`. `width` läses inte. Är huvudet sorterbart
//             (eller kolumnen den sorterade) reserveras sorteringspilens plats
//             alltid, och ett rubrikkryss sin zon, så ett klick i huvudet aldrig
//             ändrar en bredd. Måttet är CACHAT per cell hos subklassen och per
//             kolumn här, så en ändrad cell mäts om ensam (`contentChanged(kolumn)`).
//   Autosize  kolumnen TAR UTRYMMET SOM FINNS: dess bas är den satta bredden
//             (0 = `columnDefaultW`), och det som blir över av innehållsbredden
//             när alla kolumners baser dragits av delas LIKA mellan alla
//             Autosize-kolumner. Smalare än baserna rullar listan i sidled.
//             Dess avdelare går inte att greppa.
// En DRAGEN avdelare skriver `Manual` med den dragna bredden, och ett
// DUBBELKLICK på avdelaren skriver `Manual` med den anpassade bredden —
// Win32:s och Qt:s form, "anpassa nu" och inte "följ" (det senare är
// `AutoMin`). Cellernas del av måttet är subklassens (`cellContentW`).
//
// RUBRIKENS TOOLTIP (`Column::toolTip`) visas över kolumnens rubrik, och
// RUBRIKKRYSSET (`Column::headerCheck`) är en kryssruta i tre lägen först i
// rubriken: subklassen säger läget (`headerCheckState` — `nullopt` = inget
// kryss i den kolumnen) och vad en vändning gör (`headerCheckToggled`). Ett
// tryck i kryssets zon sorterar aldrig.
//
// SKRIVNA BEGRÄNSNINGAR (beslut, läs innan du "fixar" dem):
//   * Kolumnerna kan inte FLYTTAS och inte döljas.

namespace glaze {

// KOLUMNENS LÄGE — EN stavning för tabellerna, trädet och egenskapslistan
// (filhuvudet).
enum class ColumnSizing { Manual, AutoMin, Autosize };

class ColumnPanel : public Panel {
public:
    // EN KOLUMN — ett AGGREGAT i den ordning fälten oftast behövs, så samma
    // rad är både handkod och `.glazeui`:
    //     lista.setColumns({{"Namn", 160}, {"Storlek", 70, glaze::Align::Right},
    //                       {"Längd\n(m)", 70, glaze::Align::Right,
    //                        glaze::Align::Center},
    //                       {.text = "Kommentar", .sizing = glaze::ColumnSizing::Autosize}});
    // Typen nås också som `glaze::Table::Column` (arvet). Fälten växer SIST.
    struct Column {
        TrText text;                  // rubriken — literal eller glaze::tr(id);
                                      // `\n` bryter raden
        float  width = 0.0f;          // LOGISKA px; 0 (eller negativt) = stilens
                                      // `columnDefaultW`. Manual: bredden,
                                      // Autosize: golvet, AutoMin: läses inte
        Align  align = Align::Left;   // CELLERNAS justering — och rubrikens
                                      // när `headerAlign` saknas
        // RUBRIKENS justering i sidled. `std::optional` så att en
        // utelämnad fjärde plats i literalen betyder "som cellerna" och inte
        // vänster: en högerjusterad talkolumn har en högerjusterad rubrik
        // tills appen säger något annat.
        std::optional<Align> headerAlign;
        ColumnSizing sizing = ColumnSizing::Manual;   // filhuvudet
        TrText       toolTip;          // över rubriken; tom = listans egen
        bool         headerCheck = false;   // rubrikkrysset (filhuvudet)
    };

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR — settern
    // är den som INVALIDERAR. Programmatisk sättning fyrar ALDRIG ett event.

    // ---------------------------------------- KOLUMNERNA (kollektionsvokabulären)
    //
    // `addX` · `setX(index, …)` · `setXs(lista)` (regel 8) —
    // samma form som ListBox/Dropdown/PropertyGrid. `setColumn` ERSÄTTER hela
    // kolumnen (tilldelning: en utelämnad bredd blir standardbredden, en
    // utelämnad rubrikjustering "som cellerna", läget `Manual`, och tooltip
    // och rubrikkryss nollas). Index utanför ⇒ `gfx::log` + no-op, aldrig
    // fail-safe och aldrig tyst tillväxt.
    void addColumn(TrText text, float width = 0.0f, Align align = Align::Left,
                   std::optional<Align> headerAlign = std::nullopt,
                   ColumnSizing sizing = ColumnSizing::Manual);
    void setColumn(int index, TrText text, float width = 0.0f,
                   Align align = Align::Left,
                   std::optional<Align> headerAlign = std::nullopt,
                   ColumnSizing sizing = ColumnSizing::Manual);
    void setColumns(std::vector<Column> columns);
    const std::vector<Column>& columns() const { return columns_; }
    int  columnCount() const { return int(columns_.size()); }

    // BREDDEN ENSAM — dragningens programmatiska tvilling. Klampas inte
    // uppåt; 0 = standardbredden, negativt ⇒ logg + no-op. Läget rörs inte
    // (Autosize: bredden är golvet; AutoMin: den läses inte).
    void  setColumnWidth(int index, float width);
    float columnWidth(int index) const;   // det SATTA värdet (0 = standard)

    // KOLUMNENS LÄGE, TOOLTIP OCH RUBRIKKRYSS ensamma (filhuvudet). Index
    // utanför ⇒ logg + no-op; getterna svarar förvalet utanför. Invaliderar
    // bara vid ändring.
    void         setColumnSizing(int index, ColumnSizing sizing);
    ColumnSizing columnSizing(int index) const;
    void         setColumnToolTip(int index, TrText toolTip);
    void         setColumnHeaderCheck(int index, bool on);
    bool         columnHeaderCheck(int index) const;

    // Rubrikens tooltip över huvudet, annars widgetens (`Widget::toolTipAt`).
    // Subklasser som har egna tooltips frågar den här SIST före widgetens.
    TrText toolTipAt(const Ctx& c, float x, float y) const override;

    // -------------------------------------------------------- SORTERINGEN
    // `sortColumn` −1 = ingen sortering: raderna står i appens ordning.
    // Ett klick i huvudet sorterar på kolumnen (stigande), ett klick till
    // vänder riktningen — när `sortable` är på (standard). VAD som sorteras
    // och hur indexen förblir appens står hos subklassen.
    void setSortColumn(int column);
    int  sortColumn() const { return sortColumn_; }
    void setSortAscending(bool on);
    bool isSortAscending() const { return sortAscending_; }
    void setSortable(bool on);
    bool isSortable() const { return sortable_; }

    // ---------------------------------------------------- KOLUMNHUVUDET
    // (Se filhuvudet.) Raderna i huvudet: n ≥ 1 textrader, eller
    // 0 = AUTO (den rubrik som behöver flest). Ett negativt antal ⇒ logg +
    // no-op. Båda sättarna invaliderar bara vid ändring (regel 1), och en
    // ny höjd lägger om raderna, scrollen och datagridens editor — de läser
    // alla huvudets höjd ur samma geometri.
    void   setHeaderLines(int lines);
    int    headerLines() const { return headerLines_; }   // det SATTA (0 = auto)
    void   setHeaderVAlign(VAlign v);
    VAlign headerVAlign() const { return headerVAlign_; }

    // Rulla kolumnen i synfältet i sidled. Kräver ett öppet fönster (annars
    // no-op, som `ListBox::ensureVisible`).
    void ensureColumnVisible(int column);

    // setProp-egenskaper: sortColumn, sortAscending, sortable och
    // headerLines (heltal — ett ogiltigt loggas, och nyckeln räknas som
    // känd) och headerVAlign (top/center/bottom); resten tar Panel
    // (border/clip) och Widget.
    bool setProp(const std::string& key, const std::string& value) override;

protected:
    ColumnPanel() = default;

    // CELLENS LUFT i sidled — huvudets rubriker och subklassens celler. En
    // listvys och ett träds är listradens; DataGrid överrider (style.h).
    virtual float cellPadX(const Ctx& c) const;

    // FINNS HUVUDET? `Table` har alltid ett (också utan kolumner — en tom
    // remsa); trädet bara när det har kolumner, så ett träd utan dem ritar
    // och mäter som ett vanligt träd. Utan huvud är dess höjd 0 och
    // rullningsytan hela innehållsytan.
    virtual bool hasHeader() const { return true; }

    // BREDDEN PÅ DEN FÖRSTA KOLUMN SOM STÅR KVAR vid sidledsrullning
    // (`Table`s radrubriker), LOGISKA px; 0 = ingen (standard — trädet). Den
    // fasta kolumnen rullar aldrig: rullningsytan (`scrollViewport`) börjar
    // till höger om den, huvudets första cell står kvar (`headerCellRect`,
    // `drawHeader`) och rullningens spelrum är oförändrat — kolumnerna som
    // rullar är lika mycket smalare som ytan de rullar i. Kroken ska vara
    // billig och får inte fråga `scrollViewport`.
    virtual float frozenW(const Ctx&) const { return 0.0f; }

    // KOLUMNENS CELLER — den bredd cellernas innehåll behöver i
    // LOGISKA px, UTAN cellens luft (basen lägger på `cellPadX` på båda
    // sidor): bredaste innehållet och det som står före det. 0 = inga celler
    // (standard — basen känner inga rader). Läses bara för en `AutoMin`-kolumn
    // och av dubbelklicket på avdelaren.
    virtual float cellContentW(const Ctx&, int /*column*/) const { return 0.0f; }

    // INNEHÅLLET ÄNDRADES — subklassen säger till när en cell, en
    // ikon eller något annat som `cellContentW` läser har ändrats, så den
    // anpassade bredden räknas om: `contentChanged()` för ALLA kolumner,
    // `contentChanged(kolumn)` för EN (en enda cell som ändrats). Rubriker och
    // kolumner säger basen själv.
    void contentChanged();
    void contentChanged(int column);

    // RUBRIKKRYSSETS krokar (filhuvudet). `headerCheckState` frågas bara för en
    // kolumn med `headerCheck`; `nullopt` = inget kryss (och ingen zon) i den.
    // `headerCheckToggled` körs när ANVÄNDAREN tryckt i zonen — subklassen
    // ändrar raderna och fyrar sina events. Svarar som `fireEvent`: false = en
    // handler rev eller flyttade listan, och basen rör den inte mer.
    virtual std::optional<CheckState> headerCheckState(int /*column*/) const
    {
        return std::nullopt;
    }
    virtual bool headerCheckToggled(int /*column*/) { return true; }

    // ------------------------------------------------- GEOMETRIN, EN sanning
    // Widget-lokala LOGISKA px, scroll-förskjutna — ritningen läser dem rakt
    // av och beteendet projicerar dem (`Draw::project`/`localX`/`localY`).
    //
    // HUVUDETS HÖJD är `tableHeaderH` för en rad och en radhöjd
    // till per rad därutöver; `headerLineCount` är det EFFEKTIVA radantalet
    // — det satta, eller AUTO:s uträknade (cachat, se `HeaderCache`).
    //
    // INNEHÅLLSBREDDEN `cw` (LOGISKA px) är den Autosize-kolumnerna räknas
    // mot. Formerna utan den räknar `innerBounds(c).w` — men bara när en
    // Autosize-kolumn finns — och kräver därför LISTANS Ctx. Datagridens
    // `placeChild` har BARNETS Ctx och skickar bredden uttryckligen (där
    // får ingenting anropa `resolve()`). `contentW` är bredden ur listans
    // EGEN Ctx — 0 utan Autosize-kolumn, för då läses den aldrig.
    float      contentW(const Ctx& c) const;
    int        headerLineCount(const Ctx& c) const;     // ≥ 1
    int        headerLineCount(const Ctx& c, float cw) const;
    float      headerH(const Ctx& c) const;
    float      headerH(const Ctx& c, float cw) const;
    gfx::Rectf headerRect(const Ctx& c) const;          // huvudets remsa
    gfx::Rectf scrollViewport(const Ctx& c) const override;   // raderna
    float      columnW(const Ctx& c, int column) const;  // den EFFEKTIVA
    float      columnW(const Ctx& c, int column, float cw) const;
    // DEN ANPASSADE BREDDEN: rubriken och cellerna med cellens luft,
    // minst `columnMinW` — för en `AutoMin`-kolumn är den kolumnens bredd, och
    // dubbelklicket skriver den. Cachad (se `FitCache`).
    float      fittedW(const Ctx& c, int column) const;
    float      columnX(const Ctx& c, int column) const;  // oscrollad
    float      columnX(const Ctx& c, int column, float cw) const;
    float      columnsW(const Ctx& c) const;             // alla kolumner
    float      columnsW(const Ctx& c, float cw) const;
    float      scrollXClamped(const Ctx& c) const;
    float      scrollYClamped(const Ctx& c) const;
    gfx::Rectf headerCellRect(const Ctx& c, int column) const;

    // TRÄFFEN — en FYSISK muspunkt (`mouseDown`s) mot geometrin ovan.
    bool inHeader(const Ctx& c, float mx, float my) const;
    int  headerColumnAt(const Ctx& c, float mx) const;   // −1 = ingen
    int  dividerAt(const Ctx& c, float mx) const;        // kolumnen vars
                                                         // HÖGRA kant greppas

    // HUVUDET: remsan, rubrikerna (på huvudets rader, kortade med ellips —
    // `Draw::elideLines`), sorteringspilen, avdelarna och hover/tryck — per
    // UI. Ritas EFTER raderna, så en rad som rullats upp aldrig syns över
    // det.
    void drawHeader(const Ctx& c) const;
    // Huvudets YTA (`r` i widget-relativa logiska px) och hårlinjens färg,
    // per UI — `drawHeader` och radrubrikernas remsa (`Table::drawRowHeaders`)
    // delar dem, så en rubrik och en radrubrik aldrig kan se olika ut.
    void       drawHeaderSurface(const Ctx& c, const gfx::Rectf& r) const;
    gfx::Color headerLineColor(const Ctx& c) const;

    // HUVUDETS mus: `headerMouseDown` tar remsor och huvud och svarar true
    // när trycket var DERAS (subklassen rör då inte raderna); `mouseDrag`
    // drar en avdelare; `mouseMove` tänder rubrik och avdelare; `mouseUp`
    // avslutar draget och SORTERAR när trycket släpps över SAMMA rubrik som
    // det började på (en knapps semantik — dra bort för att ångra) och
    // anropar sedan `headerSorted`; `cursorAt` visar ↔ över en avdelare och
    // under hela draget. `headerDoubleClick` tar dubbelklicket på
    // samma sätt som trycket: true när det var huvudets eller remsans, och på
    // en avdelare skriver det den anpassade bredden (`fittedW`) och avslutar
    // draget som det andra trycket just började.
    bool headerMouseDown(const Ctx& c, float mx, float my);
    bool headerDoubleClick(const Ctx& c, float mx, float my);
    void mouseDrag(const Ctx& c, float mx, float my) override;
    void mouseMove(const Ctx& c, float mx, float my) override;
    bool mouseUp(const Ctx& c, float mx, float my) override;
    gfx::PointerShape cursorAt(const Ctx& c, float mx, float my) const override;

    // SPRÅKET KAN HA BYTTS — rubrikerna är `TrText` och kan ha fått en
    // annan längd, så AUTO-höjdens cache släpps (widget.h: kroken körs vid
    // varje språkbyte i ett öppet fönster och är idempotent).
    void languageChanged() override;

    // ORDNINGEN ÄNDRADES — sorteringskolumnen, riktningen eller kolumnerna
    // (en sorteringskolumn som inte fanns kan börja gälla). Subklassen
    // markerar sin visningsordning inaktuell; standard är bara en
    // invalidering. Ingen användarkod körs här.
    virtual void orderChanged() { invalidate(); }
    // ALLA kolumner byttes (`setColumns`) — datagriden släpper sin aktuella
    // kolumn och en pågående redigering. `setColumn` byter rubrik, bredd och
    // justering men inga celler, så den anropar ingenting.
    virtual void columnsReplaced() {}
    // ANVÄNDAREN sorterade med ett klick i huvudet — subklassen rullar sin
    // aktuella rad i synfältet på dess nya plats.
    virtual void headerSorted() {}

    // Rullningsytan kan ha krympt — klampa scrollen direkt (med ett öppet
    // fönster; geometrin klampar ändå vid varje läsning).
    void clampScroll();

    // NATURLIG JÄMFÖRELSE: en följd siffror jämförs som ett TAL ("fil2" före
    // "fil10", inledande nollor räknas inte), allt annat tecken för tecken
    // med ASCII-versaler vikta. 0 = lika — och då avgör radernas egen
    // ordning, för sorteringen är stabil. UTF-8 över ASCII jämförs bytevis
    // (ingen språkmedveten kollation — `Table`s skrivna begränsning).
    static int compareNatural(const std::string& a, const std::string& b);

private:
    // Rubrikernas text eller bredd ändrades — AUTO-höjden räknas om.
    void headerChanged() { ++headerGen_; }

    // KOLUMNLÄGENA: finns en Autosize-kolumn (då beror bredderna på `cw`)?
    // `columnBaseW` är kolumnens bas (`columnsizing::baseWidth`) och
    // `autosizeShare` det varje Autosize-kolumn får utöver sin bas.
    bool  hasAutosize() const;
    float columnBaseW(const Ctx& c, int column) const;
    float autosizeShare(const Ctx& c, float cw) const;
    // ANVÄNDAREN gav kolumnen en bredd (draget, dubbelklicket): `Manual` med
    // bredden, EN invalidering.
    void  setManualWidth(int index, float width);

    // RUBRIKKRYSSET: finns det i kolumnen (`headerCheck` och subklassens
    // läge)? Zonen är rutan + `checkGap` cellens luft in, huvudets hela höjd;
    // rutan står v-centrerad där en enradsrubrik står. LOGISKA px,
    // scroll-förskjutna som `headerCellRect`.
    bool       hasHeaderCheck(int column) const;
    float      headerCheckW(const Ctx& c, int column) const;   // 0 = inget
    gfx::Rectf headerCheckZone(const Ctx& c, int column) const;
    int        headerCheckAt(const Ctx& c, float mx, float my) const;   // −1

    std::vector<Column> columns_;

    int  sortColumn_    = -1;
    bool sortAscending_ = true;
    bool sortable_      = true;

    int    headerLines_  = 1;               // 0 = AUTO
    VAlign headerVAlign_ = VAlign::Center;  // ett enradshuvuds läge

    // AUTO-HÖJDENS CACHE. Huvudets höjd frågas för VARJE cell som ritas
    // (`cellInContent`) och vid varje musrörelse, och AUTO kräver en
    // radbrytning av varje rubrik — så svaret sparas mot allt det beror på:
    // fonten (pekaren OCH radhöjden — en omladdad font kan hamna på samma
    // adress; TextAreas prejudikat), skalan, cellens luft, stilens
    // standardbredd, `headerGen_` (en rubrik, en bredd, ett läge, ett
    // rubrikkryss, ett språkbyte), `contentGen_` (innehållet — en AutoMin-
    // kolumns bredd och rubrikkryssets zon följer det) och innehållsbredden
    // `cw` när en Autosize-kolumn finns (−1 utan). Sorteringen ingår INTE:
    // AUTO räknar utan pilen (filhuvudet).
    struct HeaderCache {
        const gfx::Font* font     = nullptr;
        float            scale    = 0.0f;
        float            lineH    = 0.0f;
        float            padX     = 0.0f;
        float            defaultW = 0.0f;
        float            cw       = -1.0f;
        unsigned         gen      = 0;
        unsigned         content  = 0;
        int              lines    = 1;
        bool             valid    = false;
    };
    mutable HeaderCache headerCache_;
    unsigned            headerGen_ = 0;

    // DEN ANPASSADE BREDDEN — `HeaderCache`s form. `fittedW` frågas
    // för varje cell som ritas och vid varje träff, och måttet går över alla
    // rader, så svaret sparas mot allt det beror på: fonten, skalan, stilen
    // (pekaren — cellens luft, ikonens och pilens mått), sorteringspilens
    // reservation och `fitGen_`, som stegas när en rubrik, en kolumn eller
    // allt innehåll ändras (`contentChanged()`). EN post per kolumn med sin
    // EGEN generation (`colGen_`), som `contentChanged(kolumn)` stegar — en
    // ändrad cell räknar bara om sin kolumn.
    struct FitCache {
        const gfx::Font*      font     = nullptr;
        const Style*          style    = nullptr;
        float                 scale    = 0.0f;
        bool                  sortable = false;
        int                   sortCol  = -1;
        unsigned              gen      = 0;
        std::vector<float>    w;         // per kolumn; < 0 = ej räknad
        std::vector<unsigned> colGen;    // per kolumn: generationen `w` räknades i
        bool                  valid    = false;
    };
    mutable FitCache      fitCache_;
    unsigned              fitGen_     = 0;
    unsigned              contentGen_ = 0;   // stegas av båda `contentChanged`
    std::vector<unsigned> colGen_;           // per kolumn (tom = 0)
    unsigned colGenOf(int column) const
    {
        return std::size_t(column) < colGen_.size() ? colGen_[std::size_t(column)]
                                                    : 0u;
    }

    // Huvudets mus (ritkosmetik + avdelardraget). `hotDivider_`/`hotHeader_`
    // invalideras bara vid ändring (regel 1).
    int   hotHeader_        = -1;
    int   hotDivider_       = -1;
    int   pressHeader_      = -1;     // rubriken trycket började på
    bool  pressHeaderStill_ = false;  // pekaren står kvar över den
    int   resizeColumn_     = -1;     // avdelaren som dras
    float resizeStartX_     = 0.0f;   // logiska px, widget-lokalt
    float resizeStartW_     = 0.0f;
};

} // namespace glaze
