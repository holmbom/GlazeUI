#pragma once
#include <optional>
#include <string>
#include <vector>

#include <gfx/color.h>

#include "glaze/widgets/column_panel.h"

// Table — den DELADE maskinen bakom ListView och DataGrid:
// raderna som DATA, sorteringen av dem, scrollen i två axlar och
// radgeometrin — KOLUMNHUVUDET, avdelardraget och sorteringens tillstånd är
// basens, `ColumnPanel`, som trädet delar. MÖNSTER: widgeten ritar
// SIG SJÄLV — men Table ritar inte en hel widget, bara sina DELAR
// (`drawCells`, och basens `drawHeader`); ListView och
// DataGrid har var sin `draw()` med var sin `switch (c.ui)`, för det de
// ritar runt raderna är olika saker (ett VAL respektive en AKTUELL CELL).
// Registreras inte, och konstruktorn är skyddad: en Table ensam har ingen
// handling att erbjuda.
//
// VARFÖR EN DELAD BAS och inte `DataGrid : ListView`: de två har var sitt
// `onChange`, och båda följer sitt eget prejudikat. I en listvy är VÄRDET
// valet (ListBox), i en datagrid är det CELLERNA (PropertyGrid). Hade
// datagriden ärvt listvyn hade den antingen fått ett `onChange` som betydde
// "valet" — fel mot PropertyGrid — eller ett andra `onChange` som döljer
// det första, och en `ListView&` till en datagrid hade då lyssnat på fel
// event. Med en bas finns ingen sådan referens att ta.
//
// RADERNA ÄR DATA — strängar per cell plus en valfri ikon och en valfri
// textfärg — och INTE widgets, av ListBoxens och PropertyGrids skäl: N rader
// som N widgets hade varit N tabbstopp. Cellerna är `std::string` och inte
// `TrText` (ListBox-prejudikatet: listans INNEHÅLL fyller appen om vid
// språkbyte), medan kolumnrubrikerna är `TrText` — de är etiketter, som en
// knapps text.
//
// RADENS TEXTFÄRG är APPENS: en status — röd
// för ett fel, grön för klart — satt med `setRowColor(rad, gfx::Color{…})`,
// i massformen och som tredje argument till `addRow`/`setRow`. Ingen satt
// färg är temats textfärg, och det tillkommer inga temaroller: färgen är
// DATA, som en cells text, och inte en widgetfärg (regel 3 rubbas inte).
// MARKERINGENS textfärg vinner på en VALD rad (den är markeringens enda
// tydliga signal i Minimalistic, och röd text på en blå markering kan bli
// oläslig), och en inaktiv tabell dimmar allt till `textDim`. Färgen ligger
// i RADEN och följer den genom sorteringen; `setRow` är tilldelning och
// nollar en utelämnad färg, `setCell` lämnar den. Datagridens AKTUELLA cell
// behåller markeringens färg, och dess editor visar temats textfärg (skriven
// gräns). Kontrasten mot ytan är appens ansvar.
//
// CELLENS FÄRGER är radfärgens finare form: `setCellColor(rad,
// kolumn, bakgrund, text)` ger EN cell en egen bakgrund och/eller textfärg —
// för matriser där varje cell har ett eget tillstånd. Tomt (`std::nullopt`)
// betyder radens eller stilens färg: en tom text är radens textfärg
// (`setRowColor`) och annars temats, en tom bakgrund är ingen (ytan under).
// Radfärgens regler gäller också här: färgen är DATA (inga temaroller),
// MARKERINGEN VINNER — en markerad rad (listvyn), eller den markerade cellen
// eller raden (datagriden), visar bara markeringens egna färger, varken
// cellens bakgrund eller text — och en inaktiv tabell visar inga appfärger
// alls (texten `textDim`, ingen cellbakgrund). Färgerna ligger per RAD och
// följer den genom sorteringen (appens index, som sorteringsnycklarna);
// `setRow` är tilldelning och nollar dem, `setCell` lämnar dem.
//
// RADRUBRIKER: `setRowHeaders(true)` gör FÖRSTA kolumnen till en
// radrubrik — en fast kolumn i rubrikens stil (`ColumnPanel::
// drawHeaderSurface`, hårlinjerna) som står kvar när tabellen rullas i sidled.
// Det är cellerna i kolumn 0 som är radernas namn. I övrigt är den en vanlig
// kolumn — den sorteras på sin rubrik, byter bredd med sin avdelare och kan
// vara `autoWidth` — men den bär aldrig valet, hovern eller appens
// cellfärger: de ritas i kolumnerna till höger, och ett klick på ett namn
// väljer raden som ett klick på dess data. Listvyns kryssruta (`leadW`) står
// kvar i radrubriken, i rubrikens färger. Skriven gräns: en datagrids första
// kolumn går inte att redigera och är aldrig den aktuella cellen.
//
// INDEXEN ÄR APPENS, alltid. En rad har sitt index i `rows()` — den ordning
// appen lade in den i — och SORTERINGEN ändrar bara VISNINGSORDNINGEN
// (`displayRow`/`rowAtDisplay`), aldrig indexen. `setRow(3, …)` träffar
// alltså samma rad före och efter att användaren klickat i huvudet, och ett
// val, en aktuell rad eller en redigerad cell följer RADEN och inte platsen
// (Qt:s modell/vy-delning, i liten skala). Kostnaden är en permutation som
// räknas om när raderna eller sorteringen ändras.
//
// KOLUMNHUVUDET (rubriker på flera rader, justeringen och
// sorteringspilens plats) står i column_panel.h.
//
// SORTERINGEN gör tabellen SJÄLV, och det är ett beslut (inte WinForms
// `ColumnClick` där appen sorterar): som DATA går den att uttrycka i
// `.glazeui` (`lista.setSortColumn(1);`) och previewn sorterar exakt som
// appen, valet följer raderna utan att appen gör något, och ett klick i
// huvudet fungerar utan en enda rad kod. Jämförelsen är NATURLIG — siffror
// som tal ("fil2" före "fil10"), ASCII-versaler vikta — och en cell som bär
// en SORTERINGSNYCKEL (`setSortKey`) jämförs på nyckeln i stället. Det är
// vägen för allt som VISAS annorlunda än det sorteras: "12 kB" mot "1,2 MB"
// och ett datum i språkets form. Filväljaren sätter bytes och
// tidsstämplar som nycklar. Ingen användarkod körs under en sortering —
// därför kan den göras LAT, när ordningen först behövs.
//
// SKRIVNA BEGRÄNSNINGAR (beslut, läs innan du "fixar" dem):
//   * INGEN AUTOSTORLEK. ListBox-prejudikatet: en tabell är en yta APPEN
//     dimensionerar. `width`/`height` 0 ⇒ 0.
//   * INGEN SPRÅKMEDVETEN SORTERING. UTF-8 över ASCII jämförs bytevis, så
//     svenskans å ä ö hamnar efter z men i ordningen ä å ö. En kollation per
//     språk är språkpaketets sak; till dess bär en nyckel rätt ordning.
//   * Kolumnernas begränsningar står i column_panel.h.

namespace glaze {

class Table : public ColumnPanel {
public:
    // EN RAD — massformens element (`setRows`), samma ordning som `addRow`.
    //     lista.setRows({{{"a.txt", "12 kB"}}, {{"b.png", "3 MB"}, glaze::img("fil", "bild")},
    //                    {{"fel.log"}, {}, gfx::Color{200, 40, 40}}});
    // Fälten växer SIST, så varje äldre literal betyder det den betydde.
    // `.glazeui` tar aggregatet positionellt eller DESIGNERAT
    // (`{.cells = {"a"}, .color = gfx::Color{0, 140, 0}}`).
    struct Row {
        std::vector<std::string>  cells;     // en text per kolumn; färre = tomma
        ImageSource               img{};     // ikonen i FÖRSTA kolumnen (valfri —
                                             // asset eller pixlar)
        std::optional<gfx::Color> color{};   // radens textfärg (tom = temats)
    };

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR — settern
    // är den som INVALIDERAR. Programmatisk sättning fyrar ALDRIG ett event.
    // Kolumnerna, sorteringens tillstånd, huvudets rader och fyllkolumnen
    // är basens (`ColumnPanel`).

    // ------------------------------------------ RADERNA (kollektionsvokabulären)
    //
    // `setRow` ERSÄTTER radens data — celler, ikon OCH färg, och raden tappar
    // sina sorteringsnycklar (de beskrev den gamla texten). Den raden
    // behåller däremot sin plats i valet och som aktuell rad: det är vyns
    // tillstånd och inte radens data (`ListBox::setItem`-prejudikatet).
    // `setRows` byter allt och nollar därför valet och den aktuella raden.
    void addRow(std::vector<std::string> cells, ImageSource img = {},
                std::optional<gfx::Color> color = std::nullopt);
    void setRow(int index, std::vector<std::string> cells,
                ImageSource img = {},
                std::optional<gfx::Color> color = std::nullopt);
    void setRows(std::vector<Row> rows);
    const std::vector<Row>& rows() const { return rows_; }
    int  rowCount() const { return int(rows_.size()); }

    // RADENS TEXTFÄRG ENSAM (se filhuvudet) — `setColumnWidth`s form:
    // `std::nullopt` eller `{}` nollar (temats färg). Index utanför ⇒
    // `gfx::log` + no-op; invaliderar bara vid ändring.
    void                      setRowColor(int row, std::optional<gfx::Color> color);
    std::optional<gfx::Color> rowColor(int row) const;   // tom utanför

    // EN CELLS FÄRGER (se filhuvudet) — bakgrund och text i EN sättare,
    // `setRowColor`s form: tilldelning (en utelämnad färg nollas), `std::nullopt`
    // eller `{}` = radens/stilens färg. Raden eller kolumnen utanför ⇒
    // `gfx::log` + no-op; kolumnen behöver INTE finnas än (som `setCell`).
    // Invaliderar bara vid ändring. Getterna svarar tomt utanför och för en
    // cell utan satt färg.
    void                      setCellColor(int row, int column,
                                           std::optional<gfx::Color> background,
                                           std::optional<gfx::Color> text);
    std::optional<gfx::Color> cellBackground(int row, int column) const;
    std::optional<gfx::Color> cellTextColor(int row, int column) const;

    // ------------------------------------------------------- RADRUBRIKER
    // (se filhuvudet) Första kolumnen som en fast kolumn i rubrikens
    // stil. Invaliderar bara vid ändring; utan kolumner finns ingen att fästa.
    void setRowHeaders(bool on);
    bool rowHeaders() const { return rowHeaders_; }

    // EN CELL. `setCell` byter texten och släpper cellens sorteringsnyckel;
    // en kolumn bortom radens celler fyller ut raden med tomma. Rad eller
    // kolumn utanför tabellen ⇒ logg + no-op. `cell` svarar med en TOM
    // sträng utanför.
    void               setCell(int row, int column, std::string text);
    const std::string& cell(int row, int column) const;

    // SORTERINGSNYCKELN — cellen jämförs på talet i stället för på texten
    // (se filhuvudet). Två celler MED nyckel jämförs numeriskt; en cell med
    // nyckel kommer före en utan (stigande); två utan jämförs naturligt.
    void setSortKey(int row, int column, double key);

    // -------------------------------------------------------- SORTERINGEN
    // Kolumnen och riktningen är basens (`setSortColumn`/`setSortAscending`/
    // `setSortable`); tabellen sorterar sina RADER.
    //
    // VISNINGSORDNINGEN — radens plats på skärmen (0 = överst) och raden på
    // en plats. −1 utanför. Sorteringen ändrar BARA det här paret.
    int displayRow(int row) const;
    int rowAtDisplay(int position) const;

    // ---------------------------------------------------- IKONSTORLEKEN
    // Ikonens sida i LOGISKA px, 0 = AUTO (assetens storleksklass,
    // annars `iconFallback` — den vanliga regeln). Med en satt storlek reserverar
    // VARJE rad ikonens kolumn, så texten står i linje också på rader utan
    // bild, och raden blir max(radens höjd, storleken + den luft stilens egen
    // ikon har i raden). Negativt ⇒ logg + no-op. Samma namn på `Button` och
    // `ToolButton`.
    void  setIconSize(float px);
    float iconSize() const { return iconSize_; }

    // SNABBMENYNS träffväg: raden — APPENS index —
    // under en punkt i LOGISKA px från tabellens hörn (`MouseArgs::local`).
    // −1 i huvudet, i remsorna, utanför rullningsytan och utan ett öppet
    // fönster.
    int rowAt(float localX, float localY) const;

    // ------------------------------------------------------ AKTUELL RAD
    // Den rad tangentbordet står på (−1 = ingen) — appens index. Sätts av
    // ett klick och av pilarna; programmatiskt av subklassernas settrar.
    int currentRow() const { return current_; }

    // Rulla raden i synfältet i höjdled (kolumnen i sidled: basens
    // `ensureColumnVisible`).
    // Kräver ett öppet fönster (annars no-op, som `ListBox::ensureVisible`).
    void ensureVisible(int row);

    bool focusable() const override { return true; }

    // setProp-egenskaper: iconSize (ett tal), rowHeaders (en bool);
    // sorteringen, huvudet och fyllkolumnen tar ColumnPanel, resten Panel
    // (border/clip) och Widget.
    bool setProp(const std::string& key, const std::string& value) override;

protected:
    Table();

    // ------------------------------------------------ RADENS MÅTT (krokar)
    // En listvys rad ÄR en listrad, en datagrids en gridrad — se style.h.
    // Default är listradens; DataGrid överrider båda. `baseRowH` är radens
    // GRUNDHÖJD, och `rowH` den höjd allt läser: grundhöjden,
    // eller med en satt `setIconSize` max(grundhöjden, storleken +
    // (grundhöjden − `iconFallback`)) — samma luft runt ikonen som
    // stilens egen ikon har.
    virtual float baseRowH(const Ctx& c) const;
    float         rowH(const Ctx& c) const;

    // APPENS TEXTFÄRG för en cell i en OMARKERAD rad i en AKTIV tabell: cellens
    // egen färg, annars radens, annars `theme` (temats textfärg). Markeringens
    // och det inaktiva lägets färger väljer subklassens recept FÖRE frågan —
    // den ställs bara där appens färg får synas (filhuvudet); `drawCell` gör
    // det åt dem (parametern `appColors`).
    gfx::Color appTextColor(int row, int column, gfx::Color theme) const;

    // ------------------------------------------------ RADENS IKON (krokar)
    // Ikonen i FÖRSTA kolumnen, före texten. Default är radens
    // `img` — en asset i listans storleksklass (en bild ur pixlar i
    // `iconFallback`), tonad med `iconFade` när tabellen är av.
    // Filväljarens lista RITAR sina mappar och filer (`Draw::icon`, i radens
    // textfärg `fg`) och svarar därför själv.
    // `iconSide` är ikonens sida i logiska px, 0 = ingen ikon (texten
    // börjar då vid cellens luft); `drawIcon` får en kvadrat med den sidan.
    virtual float iconSide(const Ctx& c, int row) const;
    virtual void  drawIcon(const Ctx& c, const gfx::Rectf& box, int row,
                           gfx::Color fg) const;

    // ---------------------------------------- ZONEN FÖRE IKONEN (krokar)
    // En zon först i FÖRSTA kolumnen, före ikonen och
    // texten — listvyns kryssruta (WinForms `ListView.CheckBoxes`, trädets
    // och ListBoxens plats). `leadW` är zonens bredd i logiska px, 0 = ingen
    // (standard: datagriden och filväljaren har ingen), och `drawCell`
    // flyttar ikonen och texten förbi den. `leadZone` är zonen själv på en
    // plats — cellens luft in, hela radens höjd, och den följer kolumn 0 i
    // sidled (`cellRect` bär scrollen) — EN sanning som ritningen läser och
    // träffen projicerar. Zonen ritas inte av tabellen: det den bär är
    // subklassens, i subklassens egna färger (bocken får aldrig radens
    // app-färg). En första kolumn smalare än luften och zonen låter zonen
    // sticka ut i nästa kolumn, som ikonen gör (skriven gräns).
    virtual float leadW(const Ctx&) const { return 0.0f; }
    gfx::Rectf    leadZone(const Ctx& c, int position) const;

    // ColumnPanels krokar: den fasta kolumnen är första kolumnen
    // när `rowHeaders` är på, och en `autoWidth`-kolumns innehåll är
    // cellernas bredaste text — i kolumn 0 med zonen och ikonen före den.
    // Alla ändringar av det `cellContentW` läser (celler, ikoner, zonen)
    // anropar `contentChanged()`.
    float frozenW(const Ctx& c) const override;
    float cellContentW(const Ctx& c, int column) const override;

    // ------------------------------------------------- GEOMETRIN, EN sanning
    // Widget-lokala LOGISKA px, scroll-förskjutna — ritningen läser dem rakt
    // av och beteendet projicerar dem (`Draw::project`/`localX`/`localY`).
    // Roten är `cellInContent`: cellen i INNEHÅLLETS rymd (origo i
    // innehållsytans hörn, oscrollat — `childArea`s origo), som `rowRect`/
    // `cellRect` flyttar till widgetens rymd och datagriden lägger sin
    // editor på. Kolumnernas bredd och läge, huvudet och rullningsytan är
    // basens (`ColumnPanel`); formen med `cw` tar dess innehållsbredd.
    gfx::Rectf cellInContent(const Ctx& c, int position, int column) const;
    gfx::Rectf cellInContent(const Ctx& c, int position, int column,
                             float cw) const;
    gfx::Rectf rowRect(const Ctx& c, int position) const;     // synliga bandet
    gfx::Rectf cellRect(const Ctx& c, int position, int column) const;
    int        firstVisiblePosition(const Ctx& c) const;
    int        pageRows(const Ctx& c) const;   // hela rader i synfältet, ≥ 1

    // TRÄFFEN — en FYSISK muspunkt (`mouseDown`s) mot geometrin ovan
    // (huvudets träff är basens).
    int  positionAt(const Ctx& c, float my) const;       // −1 = ingen rad
    int  columnAt(const Ctx& c, float mx) const;         // −1 = bortom
    // Platsen i HELA rullningsytan — −1 i huvudet, i remsorna och utanför i
    // x och y. `rowAt` och listvyns högertryck delar den.
    int  hitPosition(const Ctx& c, float mx, float my) const;

    // ------------------------------------------------------ RITDELARNA
    // (Huvudet är basens `drawHeader`.)
    // EN RADS CELLER: ikonen i första kolumnen, texten kortad med ellips och
    // justerad per kolumn. Delas av alla ritningar — det som skiljer
    // familjerna är MARKERINGEN runt raden. `drawCell` är EN cell (datagriden
    // ger den aktuella cellen en egen färg); den klipper inte — anroparen
    // hoppar över celler utanför synfältet.
    //
    // FÄRGEN: med `appColors` är `fg` TEMATS textfärg och appens
    // färger gäller — cellens bakgrund fylls först, och texten är cellens,
    // radens eller temats färg (`appTextColor`). Utan `appColors` är `fg` den
    // färdiga färgen (markeringens, eller `textDim` i en inaktiv tabell) och
    // ingen appfärg ritas: anroparen säger `på && !markerad`.
    void drawCells(const Ctx& c, int position, int row, gfx::Color fg,
                   bool appColors) const;
    void drawCell(const Ctx& c, int position, int row, int column,
                  gfx::Color fg, bool appColors) const;

    // RADRUBRIKERNAS REMSA: den fasta första kolumnen i rubrikens
    // stil — ytan, namnen på de synliga raderna med radernas hårlinjer och
    // remsans högerkant. Ritas av varje ritning EN gång, bredvid `drawHeader`
    // (efter raderna); inget när `rowHeaders` är av. Namnen ritas av
    // `drawCell`, i rubrikens textfärg `fg` och utan appfärger; `drawRowHeaderMarks`
    // är subklassens plats för de märken valet lämnar i remsan i stället för
    // en tonad yta — listvyns kryssruta (zonen före namnet, `leadW`) och
    // Minimalistics valstreck. Märkena ritas i `fg`/rubrikens färger.
    // Standard ritar ingenting.
    void         drawRowHeaders(const Ctx& c) const;
    virtual void drawRowHeaderMarks(const Ctx&, int /*position*/, int /*row*/,
                                    gfx::Color /*fg*/) const {}

    // -------------------------------------------------- BETEENDET (delat)
    gfx::Sizef contentSize(const Ctx& c) const override;
    gfx::Sizef contentExtent(const Ctx& c) const override;
    gfx::Rectf clipRect(const Ctx& c) const override;   // barnen: raderna
    // FYRKANTIGT barnklipp — tabellen ritas fyrkantig (listfamiljen,
    // ListBox-prejudikatet), och klippet är radernas yta UNDER huvudet: en
    // radie hade rundat hörnen MOT huvudet, där ingen form är rund.
    float      clipRadius(const Ctx&) const override { return 0.0f; }
    bool       mouseWheel(const Ctx& c, float x, float y, float wheelX,
                          float wheelY) override;

    // (Huvudets mus — `headerMouseDown`, avdelardraget, sorteringsklicket
    // och pekarformen — är basens.)

    // AKTUELL RAD utan event (subklassernas settrar och användarvägar).
    void setCurrent(int row);

    // NAVIGERINGEN delad: målplatsen för en tangent från platsen `from`
    // (Upp/Ned/Home/End/PageUp/PageDown), eller −1 när tangenten inte är
    // en navigering. Klampad till tabellen.
    int navigationTarget(const Ctx& c, gfx::Key k, int from) const;

    // Raderna ÄNDRADES strukturellt — subklassen synkar sitt eget
    // radtillstånd (valet). `rowsReplaced` efter `setRows`, `rowAppended`
    // efter `addRow` och `rowReplaced` efter `setRow` (raden har ny data men
    // samma plats i valet — listvyn släpper ett väntande soloval på den).
    // Ingen användarkod körs här.
    virtual void rowsReplaced() {}
    virtual void rowAppended() {}
    virtual void rowReplaced(int /*row*/) {}

private:
    // Ordningen — en CACHE (den räknas LATT när den behövs, och ingen
    // användarkod körs under den; se filhuvudet).
    void ensureOrder() const;
    void orderChanged() override;   // markera + invalidera (basens krok)
    int  compareRows(int a, int b, int column) const;

    std::vector<Row>    rows_;
    // Sorteringsnycklarna, parallella med `rows_` (en vektor per rad, kort
    // eller tom när raden inte bär några).
    std::vector<std::vector<std::optional<double>>> keys_;

    // Cellernas färger, parallella med `rows_` och per kolumn, kort
    // eller tom när raden inte bär några — sorteringsnycklarnas form.
    struct CellColors {
        std::optional<gfx::Color> background;
        std::optional<gfx::Color> text;
    };
    std::vector<std::vector<CellColors>> cellColors_;

    int  current_       = -1;

    float  iconSize_     = 0.0f;            // 0 = AUTO
    bool   rowHeaders_   = false;           // första kolumnen fast

    mutable std::vector<int> order_;   // plats → rad
    mutable std::vector<int> pos_;     // rad → plats
    mutable bool             orderDirty_ = true;
};

} // namespace glaze
