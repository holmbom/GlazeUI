#pragma once
#include <optional>
#include <string>
#include <vector>

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
// KOLUMNBREDD EFTER INNEHÅLLET. En bredd som är `Column::autoWidth`
// (varje negativ) FÖLJER innehållet: rubrikens bredaste rad och cellernas
// bredaste innehåll, det bredaste av dem med cellens luft på båda sidor och
// en kants tjocklek (en hårlinje) som andrum — ritningen kortar med ett strikt
// `<=` och flyttalen ger inte alltid måttet tillbaka —, aldrig smalare än
// stilens `columnMinW`. Måttet räknas om när rader,
// rubriker, font eller stil ändras (en cache i samma anda som huvudets
// AUTO-höjd) — mätningen går över ALLA rader, så en tabell med hundratusentals
// rader ska ha fasta bredder. Är huvudet sorterbart (eller kolumnen den
// sorterade) reserveras sorteringspilens plats alltid, så ett klick i huvudet
// aldrig ändrar en bredd. Fyllkolumnen kan också vara auto: den anpassade
// bredden är då dess minsta. En dragen avdelare skriver en vanlig FAST bredd
// (`setColumnWidth`), och ett DUBBELKLICK på avdelaren skriver den anpassade
// bredden som en fast bredd — Win32:s och Qt:s form, "anpassa nu" och inte
// "följ" (det senare är `autoWidth`). Cellernas del av måttet är subklassens
// (`cellContentW` — basen känner inga rader).
//
// SKRIVNA BEGRÄNSNINGAR (beslut, läs innan du "fixar" dem):
//   * Kolumnerna kan inte FLYTTAS och inte döljas, och ingen sträcks ut
//     till kanten av sig själv — det gör EN fyllkolumn som appen väljer
//     (`setFillColumn`).

namespace glaze {

class ColumnPanel : public Panel {
public:
    // EN KOLUMN — ett AGGREGAT i den ordning fälten oftast behövs, så samma
    // rad är både handkod och `.glazeui`:
    //     lista.setColumns({{"Namn", 160}, {"Storlek", 70, glaze::Align::Right},
    //                       {"Längd\n(m)", 70, glaze::Align::Right,
    //                        glaze::Align::Center}});
    // Typen nås också som `glaze::Table::Column` (arvet).
    struct Column {
        // Bredden som FÖLJER innehållet — se filhuvudet.
        static constexpr float autoWidth = -1.0f;

        TrText text;                  // rubriken — literal eller glaze::tr(id);
                                      // `\n` bryter raden
        float  width = 0.0f;          // LOGISKA px; 0 = stilens
                                      // `columnDefaultW`, < 0 (`autoWidth`) =
                                      // efter innehållet
        Align  align = Align::Left;   // CELLERNAS justering — och rubrikens
                                      // när `headerAlign` saknas
        // RUBRIKENS justering i sidled. `std::optional` så att en
        // utelämnad fjärde plats i literalen betyder "som cellerna" och inte
        // vänster: en högerjusterad talkolumn har en högerjusterad rubrik
        // tills appen säger något annat.
        std::optional<Align> headerAlign;
    };

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR — settern
    // är den som INVALIDERAR. Programmatisk sättning fyrar ALDRIG ett event.

    // ---------------------------------------- KOLUMNERNA (kollektionsvokabulären)
    //
    // `addX` · `setX(index, …)` · `setXs(lista)` (regel 8) —
    // samma form som ListBox/Dropdown/PropertyGrid. `setColumn` ERSÄTTER hela
    // kolumnen (tilldelning: en utelämnad bredd blir standardbredden, en
    // utelämnad rubrikjustering "som cellerna"). Index utanför ⇒ `gfx::log` +
    // no-op, aldrig fail-safe och aldrig tyst tillväxt.
    void addColumn(TrText text, float width = 0.0f, Align align = Align::Left,
                   std::optional<Align> headerAlign = std::nullopt);
    void setColumn(int index, TrText text, float width = 0.0f,
                   Align align = Align::Left,
                   std::optional<Align> headerAlign = std::nullopt);
    void setColumns(std::vector<Column> columns);
    const std::vector<Column>& columns() const { return columns_; }
    int  columnCount() const { return int(columns_.size()); }

    // BREDDEN ENSAM — dragningens programmatiska tvilling (en dragen
    // avdelare skriver hit). Klampas inte uppåt; 0 = standardbredden,
    // `Column::autoWidth` = efter innehållet.
    void  setColumnWidth(int index, float width);
    float columnWidth(int index) const;   // det SATTA värdet (0 = standard,
                                          // `autoWidth` = efter innehållet)

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

    // ---------------------------------------------------- FYLLKOLUMNEN
    // (Qt:s Stretch) Kolumnen som TAR UPP resten av
    // bredden: dess effektiva bredd är max(egen bredd, innehållets bredd −
    // de andra kolumnernas), så den satta bredden (eller `columnDefaultW`) är
    // dess MINSTA bredd — smalare än så rullar listan i sidled.
    // Dess egen avdelare går inte att greppa; drar man en annan tar
    // fyllkolumnen upp skillnaden. −1 = ingen (standard). Värdet lagras som
    // det är och läses giltigt (bortom kolumnerna = ingen), så att
    // satsordningen i `.glazeui` inte avgör; < −1 ⇒ logg + no-op.
    void setFillColumn(int column);
    int  fillColumn() const { return fillColumn_; }

    // Rulla kolumnen i synfältet i sidled. Kräver ett öppet fönster (annars
    // no-op, som `ListBox::ensureVisible`).
    void ensureColumnVisible(int column);

    // setProp-egenskaper: sortColumn, sortAscending, sortable, headerLines
    // och fillColumn (heltal — ett ogiltigt loggas, och nyckeln räknas som
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
    // sidor): bredaste texten och det som står före den. 0 = inga celler
    // (standard — basen känner inga rader). Läses bara för en kolumn med
    // `autoWidth` och av dubbelklicket på avdelaren.
    virtual float cellContentW(const Ctx&, int /*column*/) const { return 0.0f; }

    // INNEHÅLLET ÄNDRADES — subklassen säger till när en cell, en
    // ikon eller något annat som `cellContentW` läser har ändrats, så den
    // anpassade bredden räknas om. Rubriker och kolumner säger basen själv.
    void contentChanged() { ++fitGen_; }

    // ------------------------------------------------- GEOMETRIN, EN sanning
    // Widget-lokala LOGISKA px, scroll-förskjutna — ritningen läser dem rakt
    // av och beteendet projicerar dem (`Draw::project`/`localX`/`localY`).
    //
    // HUVUDETS HÖJD är `tableHeaderH` för en rad och en radhöjd
    // till per rad därutöver; `headerLineCount` är det EFFEKTIVA radantalet
    // — det satta, eller AUTO:s uträknade (cachat, se `HeaderCache`).
    //
    // INNEHÅLLSBREDDEN `cw` (LOGISKA px) är den fyllkolumnen räknas mot.
    // Formerna utan den räknar `innerBounds(c).w` — men bara när en
    // fyllkolumn finns — och kräver därför LISTANS Ctx. Datagridens
    // `placeChild` har BARNETS Ctx och skickar bredden uttryckligen (där
    // får ingenting anropa `resolve()`). `contentW` är bredden ur listans
    // EGEN Ctx — 0 utan fyllkolumn, för då läses den aldrig.
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
    // minst `columnMinW` — för en `autoWidth`-kolumn är den kolumnens bas, och
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

    // FYLLKOLUMNEN: finns en giltig? `columnBaseW` är kolumnens satta
    // bredd eller `columnDefaultW`, fyllkolumnens minimum.
    bool  hasFill() const { return fillColumn_ >= 0 && fillColumn_ < columnCount(); }
    float columnBaseW(const Ctx& c, int column) const;

    std::vector<Column> columns_;

    int  sortColumn_    = -1;
    bool sortAscending_ = true;
    bool sortable_      = true;

    int    headerLines_  = 1;               // 0 = AUTO
    VAlign headerVAlign_ = VAlign::Center;  // ett enradshuvuds läge
    int    fillColumn_   = -1;              // −1 = ingen

    // AUTO-HÖJDENS CACHE. Huvudets höjd frågas för VARJE cell som ritas
    // (`cellInContent`) och vid varje musrörelse, och AUTO kräver en
    // radbrytning av varje rubrik — så svaret sparas mot allt det beror på:
    // fonten (pekaren OCH radhöjden — en omladdad font kan hamna på samma
    // adress; TextAreas prejudikat), skalan, cellens luft, stilens
    // standardbredd och `headerGen_`, som stegas när en rubrik eller en
    // bredd ändras, när fyllkolumnen byts och vid ett språkbyte — och
    // fyllkolumnens effektiva bredd (`fillW`, −1 utan), som följer listans
    // bredd. Sorteringen ingår INTE: AUTO räknar utan pilen
    // (filhuvudet).
    struct HeaderCache {
        const gfx::Font* font     = nullptr;
        float            scale    = 0.0f;
        float            lineH    = 0.0f;
        float            padX     = 0.0f;
        float            defaultW = 0.0f;
        float            fillW    = -1.0f;
        unsigned         gen      = 0;
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
    // innehållet ändras (`contentChanged`). EN post per kolumn; 0 = ej räknad.
    struct FitCache {
        const gfx::Font*    font     = nullptr;
        const Style*        style    = nullptr;
        float               scale    = 0.0f;
        bool                sortable = false;
        int                 sortCol  = -1;
        unsigned            gen      = 0;
        std::vector<float>  w;           // per kolumn; < 0 = ej räknad
        bool                valid    = false;
    };
    mutable FitCache fitCache_;
    unsigned         fitGen_ = 0;

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
