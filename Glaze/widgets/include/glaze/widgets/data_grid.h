#pragma once
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "glaze/widgets/color_picker.h"   // ColorDisplay
#include "glaze/widgets/table.h"

// DataGrid — DATAGRIDDEN (wxGrid, WinForms
// DataGridView). En tabell med kolumnhuvud där cellerna går att REDIGERA,
// med typade celler (DE TYPADE CELLERNA nedan). Byggd
// på samma `Table` som ListView (kolumnerna, raderna, sorteringen, huvudet,
// scrollen). PropertyGrid är prejudikatet för editorerna, fast med n
// kolumner i stället för två; cellernas YTOR är fältets — skrivytan och den
// skrivskyddade ytan, som TextInputs.
//
// MÖNSTER: widgeten ritar SIG SJÄLV — draw() bor i src/widgets/
// data_grid.cpp med `switch (c.ui)` (hård regel 3).
//
// EDITORERNA ÄR RIKTIGA BARN, och EXAKT tre: ett `TextInput` (fritext och
// tal), en `Dropdown` och en `ColorPicker` (privata subklasser, som
// talfältets fält), ägda och dolda. En kryss- och en knappcell har ingen
// editor. En redigering visar EN av dem över cellen. PLACERINGEN är datagridens egen
// (`placeChild`, Bar-flexens krok): editorn läggs där cellens värde ritas, ur
// samma formler som ritningen (`Table::cellInContent`, `Table::cellContent`)
// vid varje `resolve()`, så den följer
// scroll, sortering och en dragen kolumnbredd utan en enda sparad koordinat.
// Datagriden är ETT tabbstopp (`focusesChildren`), och editorerna kan inte
// lossas utifrån (`canReleaseChild`). Editorn står i KOLUMNENS justering
// (`Column::align` → editorns `setAlign`), läst när redigeringen
// börjar — en högerjusterad talcell hoppar inte till vänster under fältet.
//
// KOLUMNTYPEN är en egenskap PER KOLUMNINDEX — `setColumnEditor(kolumn,
// EditorSpec)`, med redigeraren och dess val (se DE TYPADE CELLERNA nedan).
// `None` är en skrivskyddad kolumn, ritad som ett skrivskyddat fält
// (`fieldReadOnly`) med texten i full kontrast. Inställningen hör till
// INDEXET och överlever `setColumn`/`setColumns`, precis som `sortColumn`
// gör (table.h): satsordningen i en `.glazeui` ska inte avgöra om en
// kolumntyp gäller.
// En val-kolumn UTAN val går inte att redigera.
//
// REDIGERINGEN — WinForms och wxGrid i husets form:
//   börja    dubbelklick på den aktuella cellen, F2, Enter, eller att man
//            BÖRJAR SKRIVA (tecknet ersätter cellens text — en fritext- eller
//            talcell); Check, Color, Choice och Button med ett enkelt klick,
//            Text och Number också så med `setEditOnClick(true)`
//   spara    Enter (och flytta ner en rad; Shift+Enter upp), Tab/Shift+Tab
//            (och flytta till nästa redigerbara cell), Upp/Ned (och flytta en
//            rad), eller att fokus lämnar editorn — ett klick någon
//            annanstans, även i datagridens eget huvud eller dess remsa
//   avbryt   Escape — texten som skrivits kastas
// ENTER UTAN REDIGERING: öppnar den aktuella cellens redigerare (Text,
// Number, Choice, Color), klickar en knappcell och fäller en kategorirad; i en
// kryss-, skrivskyddad eller inaktiv cell flyttar den ner en rad (Shift+Enter
// upp). SPACE växlar ett kryss, klickar en knapp och öppnar en lista eller
// färgväljaren.
// En VAL-cell öppnar sin lista direkt. Ett val av ett NYTT värde sparar och
// avslutar redigeringen. ENTER i den öppna listan sparar valet, avslutar och
// flyttar ner en rad (Shift+Enter upp) — också när värdet är detsamma —,
// Space i listan sparar och avslutar utan att flytta, och Escape avbryter i
// ett steg vare sig listan är öppen eller inte. Ett KLICK på samma värde
// stänger bara listan (dropdownen meddelar inget när valet inte ändrades) —
// editorn står då kvar tills Enter, Escape eller ett klick utanför.
// FÄRGCELLENS popup har tangentbordet medan den är öppen
// (`AppWindow::setPopupTakesKeys`): dess Enter, Escape, Tab och pilar är
// popupens. När popupen STÄNGS — palettens val, Enter, Escape, ett klick
// utanför — är redigeringen slut: valet är redan sparat, väljaren göms och
// pilarna flyttar vidare direkt.
//
// NAVIGERINGEN utan redigering: pilarna, Tab/Shift+Tab (till nästa
// REDIGERBARA och aktiva cell i visningsordningen, med radbyte — kategori-,
// tom- och knappceller hoppas över, och finns ingen sådan cell kvar lämnar Tab
// datagriden), Home/End (radens första/sista cell), Ctrl+Home/Ctrl+End
// (tabellens) och PageUp/PageDown. En TOM rad är aldrig aktuell; en
// kategorirad kan vara det (hela raden ramas in), och där fäller Vänster ihop
// och Höger ut.
//
// EVENTEN: `onChange` med RAD och KOLUMN i args (`CellChangeArgs` — regel 6:
// nytt fält i args, aldrig en ny parameter), fyrat exakt en gång när
// ANVÄNDAREN sparat en cell vars text faktiskt ändrades, och `onCommit`
// med samma args direkt efter — en sparad cell ÄR en avslutad
// handling, så de två kommer alltid tillsammans (en app som lyssnar på
// `onCommit` överallt slipper veta att datagriden redan committar). Värdet
// läses ur sender (`grid.cell(e.row, e.column)`); raden är APPENS index,
// oberoende av sorteringen. Programmatiska settrar (`setCell` …) fyrar aldrig.
// UNDANTAGET, skrivet: en sparning som sker för att FOKUS lämnade editorn
// fyrar sina events i pump-svansen (`Widget::postEvent`) och inte direkt —
// fokusbytet pågår då, och `focusLost` får aldrig köra användarkod
// (widget.h). Cellen är skriven redan när fokus flyttas, och det postade
// eventet SLOPAS om datagriden dött eller raderna bytts (`setRows`) innan
// det hann fram.
//
// PROGRAMMATISKT under en redigering: `setRows`, `setColumns` och
// `setCurrentCell` AVBRYTER den (raderna, kolumnerna eller cellen är andra —
// spara först med `commitEdit` om användarens text ska med). `setRow`,
// `setCell` och `addRow` gör det INTE: redigeringen följer RADEN, som valet
// och den aktuella raden gör (table.h), och användarens text skrivs när den
// sparas. Det är en medveten avvikelse från PropertyGrid, som stänger sin
// editor vid `setRow`: en uppdatering utifrån ska inte kasta det användaren
// håller på att skriva.
//
// RADVALET (WinForms `SelectionMode.FullRowSelect`
// och `MultiSelect`). `setSelectionMode(SelectionMode::FullRow)` markerar HELA
// RADER, och `setMultiSelect(true)` låter användaren välja flera — listvyns
// form och ord: ett klick gör cellen aktuell OCH väljer raden (Ctrl/Cmd+klick
// vänder, Shift+klick spannet från ankaret, Ctrl+Shift lägger till), en
// tangent som flyttar den aktuella cellen flyttar valet (med Shift utökar
// Upp/Ned/PageUp/PageDown/Home/End spannet), och Ctrl+A väljer alla.
// `isSelected(rad)`/`selectedRows()` (visningsordning) läser valet; i
// `Cell`-läget (standard) är INGA rader valda — WinForms
// `SelectedRows` — och valet LAGRAS ändå, så `setSelected` och
// `setSelectionMode` kan stå i vilken ordning som helst. Valet är per RAD och
// följer sorteringen; `setRows` nollar det, `setRow` behåller, `addRow` ger
// en ovald rad. EVENTET heter `onSelectionChange` (`ChangeArgs`, WinForms
// `SelectionChanged`) och inte `onChange`: datagridens VÄRDE är cellerna
// (table.h). Det fyras en gång per ANVÄNDARhandling som ändrade valet — också
// efter `onCollapse` när användarens ihopfällning valde bort dolda rader —
// aldrig av settrarna (`setExpanded` väljer bort tyst). Bara SYNLIGA datarader
// kan vara valda. Redigeringen är orörd; i radläget ramas den aktuella cellen
// in i stället för att fyllas. Ctrl gör INTE listvyns "flytta utan att välja"
// från tangentbordet — Ctrl+Home/End är datagridens (skriven gräns).
//
// DE TYPADE CELLERNA — PropertyGrids slag och textformer, med samma kod
// bakom (den interna `src/value_cell.h`):
//   Text     fritext.
//   Choice   en vallista (`choices`, med `choiceIcons` parallellt). Ett KLICK
//            öppnar listan, och ett val av ett nytt värde sparar direkt.
//   Check    en kryssruta, värdet "true"/"false". Ett KLICK eller Space växlar
//            och sparar direkt.
//   Color    en färg, `#RRGGBB` (`#RRGGBBAA` med `allowAlpha`), visad som
//            `colorDisplay` säger: `Swatch` (förvalet) en ruta ensam,
//            `Style::colorCellSwatchW` bred, `Hex` hextexten ensam och `Both`
//            en ruta i kryssrutans storlek och hextexten efter den. Ett
//            KLICK öppnar färgväljarens popup; dess `onCommit` sparar.
//   Number   ett tal i kanonisk form med punkt, visat med språkets
//            decimaltecken mellan `prefix` och `suffix`. Användaren får skriva
//            `,` och `.`. Ett värde utanför `min`/`max` klampas och avrundas
//            till `decimals` (−1 kortaste formen); en otolkbar text är ett FEL:
//            värdet står kvar, ingenting fyras, cellen visar texten på
//            `errorSurface`, `hasError` är sann och tooltipen säger "Ogiltigt
//            värde". Med `allowEmpty` är en tom text ett giltigt TOMT värde
//            (`numberValue` nullopt, inget fel) som visar `emptyText`.
//   Button   en knapp med cellens text (och bild). Ett tryck som SLÄPPS över
//            samma cell fyrar `onCellClick` — knappens semantik, dra bort för
//            att ångra; Enter och Space på den aktuella cellen likaså. Värdet
//            ändras aldrig.
//   None     en skrivskyddad TEXTcell: visas på det skrivskyddade fältets yta.
// En LÅST cell (`readOnly` i specen) ritas som sitt slag — talet med prefix
// och suffix, färgen, kryssrutan, valet med ikon, knappen — på det
// skrivskyddade fältets yta (`fieldReadOnly`) med värdet i full kontrast, och
// går aldrig att ändra: den redigeras inte, Tab hoppar över den, rubrikkrysset
// räknar den inte och en knapp trycks inte.
// Kolumnens redigerare (`setColumnEditor`) gäller varje cell i kolumnen, och
// en CELL kan ha en egen (`setCellEditor` — t.ex. en låst `None`-cell eller
// andra gränser och en annan enhet). `setCellEnabled(rad, kolumn, false)` är
// en cell som syns men inte går att ändra NU (dämpad), `setCellToolTip` en
// tooltip per cell. TOOLTIPEN: felet vinner, sedan cellens, sedan
// kolumnrubrikens (över huvudet), sedan datagridens egen.
//
// HUR EN REDIGERING BÖRJAR: Check, Color, Choice och Button tar ett ENKELT
// klick (utan modifierare). Text och Number börjar med dubbelklick, F2, Enter
// eller att man börjar skriva — och med `setEditOnClick(true)` också med ett
// enkelt klick. Ctrl/Shift-klick väljer bara (radvalet). Editorn står där
// cellens VÄRDE ritas (`Table::cellContent`), så cellens bild, radens ikon och
// kategorins indrag syns bredvid den.
//
// FLERA RADER PÅ EN GÅNG (`setMultiEdit(true)`, i `FullRow` med flerval):
// sparas en cell i en VALD rad skrivs samma kolumn i ALLA valda rader, var och
// en med sin egen cells redigerare (egen klampning; en inaktiv cell eller en
// cell med en annan sorts redigerare hoppas över). `onChange`/`onCommit` fyras
// EN gång, med de rader som faktiskt ändrades i `rows` (visningsordning) och
// den redigerade i `row`. RUBRIKKRYSSET (`setColumnHeaderCheck` på en
// Check-kolumn) visar Kryssad/Okryssad/Delvis över kolumnens aktiva
// kryssceller och sätter alla vid en vändning — också det ETT event, med
// `row` −1 och alla ändrade i `rows`.
//
// SKRIVNA BEGRÄNSNINGAR utöver tabellens (table.h): en aktuell cell i taget
// (inget områdesval), inget eget event när den
// aktuella cellen byts, Delete tömmer inte en cell — och editorn kan täcka
// den lodräta remsan när den redigerade kolumnen når in under den (Panels
// skrivna regel: barnet vinner hit-testen, så remsan går att dra först när
// redigeringen är slut).

namespace glaze {

// Args till datagridens `onChange`/`onCommit` — VILKEN cell som sparades.
// Ärver `ChangeArgs`, så en handler skriven för `ChangeArgs&` tar emot den
// också. `rows` bär ALLA rader som ändrades (appens index, visningsordning):
// en enda cell är `{row}`, flera rader på en gång och rubrikkrysset fler —
// rubrikkrysset med `row` −1.
struct CellChangeArgs : ChangeArgs {
    int              row    = -1;   // appens radindex
    int              column = -1;
    std::vector<int> rows;
};

// Args till `onCellClick` — knappcellen som trycktes.
struct CellClickArgs : ClickArgs {
    int row    = -1;   // appens radindex
    int column = -1;
};

class DataGrid : public Table {
public:
    // CELLENS REDIGERARE (filhuvudet).
    enum class Editor { Text, Choice, Check, Color, Number, Button, None };

    // EN REDIGERARE MED SINA VAL — kolumnens eller cellens. ORDNINGEN ÄR DEN
    // DESIGNERADE INIT-ORDNINGEN: `.glazeui` skriver
    //     dg.setColumnEditor(2, {.editor = glaze::DataGrid::Editor::Number,
    //                            .min = 0, .max = 100, .suffix = "%"});
    // och C++20 kräver fälten i deklarationsordning. Fälten växer SIST.
    struct EditorSpec {
        Editor                   editor = Editor::Text;
        std::vector<std::string> choices;       // Choice
        std::vector<ImageSource> choiceIcons;   // Choice: parallell med choices
        double min = -std::numeric_limits<double>::infinity();   // Number
        double max = std::numeric_limits<double>::infinity();    // Number
        int    decimals = -1;     // Number: −1 kortaste formen, 0–9 fasta
        std::string prefix;       // Number: före talet ("1:")
        std::string suffix;       // Number: efter talet ("mm", "%")
        bool        allowAlpha = false;   // Color: `#RRGGBBAA`
        bool        allowEmpty = false;   // Number: tom text är ett värde
        std::string emptyText;            // Number: det som visas när tomt
        ColorDisplay colorDisplay = ColorDisplay::Swatch;   // Color: rutan,
                                          // hextexten eller båda
        bool        readOnly = false;     // ritas som sitt slag, ändras ALDRIG
    };

    // VALETS LÄGE: en aktuell CELL (förvalet) eller hela RADER.
    enum class SelectionMode { Cell, FullRow };

    DataGrid();

    // ---------------------------------------------------- KOLUMNTYPEN
    // Per kolumnINDEX; ett index utan inställning är `Text`. Kolumnen behöver
    // inte finnas än, men ett negativt index ⇒ logg + no-op. Programmatiskt:
    // inget event, och en pågående redigering i kolumnen avbryts.
    // En Choice-redigerare UTAN val går inte att redigera.
    void              setColumnEditor(int column, EditorSpec spec);
    const EditorSpec& columnEditor(int column) const;

    // ---------------------------------------------------- CELLERNA
    // En CELLS egen redigerare går före kolumnens; `clearCellEditor` lämnar
    // tillbaka cellen till kolumnens. `cellEditor` svarar den som GÄLLER.
    // `setCellEnabled`/`setCellToolTip` per cell. Rad utanför eller negativ
    // kolumn ⇒ logg + no-op (kolumnen behöver inte finnas än). Programmatiskt:
    // inget event; en ny redigerare eller en inaktivering av den cell som
    // redigeras AVBRYTER redigeringen. `setRow` nollar radens cellinställningar
    // (tilldelning, som cellfärgerna); `setCell` lämnar dem.
    void              setCellEditor(int row, int column, EditorSpec spec);
    void              clearCellEditor(int row, int column);
    const EditorSpec& cellEditor(int row, int column) const;
    void              setCellEnabled(int row, int column, bool on);
    bool              isCellEnabled(int row, int column) const;
    void              setCellToolTip(int row, int column, TrText toolTip);
    TrText            cellToolTip(int row, int column) const;   // tom utanför

    // LÄSVÄGARNA ur cellens text och den redigerare som gäller —
    // PropertyGrids regler: `isChecked` för en Check-cell ("true"/"yes"/"1"),
    // `colorValue` för en Color-cell (opak utan `allowAlpha`), `numberValue`
    // för en Number-cell KLAMPAD och AVRUNDAD (nullopt för en otolkbar eller
    // tom text), `hasError` för en Number- eller Color-cell vars text inte går
    // att tolka (en tom text med `allowEmpty` är inget fel). Utanför: false/
    // nullopt.
    bool                      isChecked(int row, int column) const;
    std::optional<gfx::Color> colorValue(int row, int column) const;
    std::optional<double>     numberValue(int row, int column) const;
    bool                      hasError(int row, int column) const;

    // ETT ENKELT KLICK börjar redigera en Text- eller Number-cell (filhuvudet).
    void setEditOnClick(bool on);
    bool editOnClick() const { return editOnClick_; }
    // TEXTEN MARKERAD när en Text- eller Number-redigering börjar med Enter,
    // F2, dubbelklick eller ett klick (förvalet true): hela texten markerad,
    // så det man skriver ersätter den; med false står markören sist och
    // ingenting är markerat. En redigering som börjar med ett skrivet tecken
    // ersätter texten med tecknet oavsett valet. Gäller nästa redigering.
    void setSelectOnEdit(bool on);
    bool selectOnEdit() const { return selectOnEdit_; }
    // FLERA RADER PÅ EN GÅNG (filhuvudet).
    void setMultiEdit(bool on);
    bool isMultiEdit() const { return multiEdit_; }

    // ---------------------------------------------------- AKTUELL CELL
    // Programmatisk: inget event, en pågående redigering AVBRYTS, och den
    // scrollar inte (`ensureVisible`/`ensureColumnVisible` finns). Utanför
    // tabellen, en tom rad eller en dold rad (i en ihopfälld kategori) ⇒ logg
    // + no-op.
    void setCurrentCell(int row, int column);
    int  currentColumn() const { return currentColumn_; }

    // ----------------------------------------------------- REDIGERINGEN
    // WinForms `BeginEdit`/`EndEdit`/`CancelEdit`. `beginEdit` börjar
    // redigera den AKTUELLA cellen som F2 gör (no-op utan aktuell cell, i en
    // skrivskyddad kolumn, i en inaktiv datagrid eller utan ett visat
    // fönster). `commitEdit` sparar och avslutar — och fyrar `onChange` och
    // `onCommit` när texten ändrade cellen, för ändringen är ANVÄNDARENS;
    // appen avgör bara när den sparas. Den är samtidigt `Widget::commitEdit`,
    // så `AppWindow::commitEdits()` sparar en öppen cell åt en
    // Spara-handler. `cancelEdit` kastar. Båda ger fokus tillbaka till
    // datagriden och är no-op utan en pågående redigering.
    void beginEdit();
    void commitEdit() override;
    void cancelEdit();

    // Pågår en redigering? Raden (appens index) och kolumnen (−1 annars).
    bool isEditing() const { return editRow_ >= 0; }
    int  editRow() const { return editRow_; }
    int  editColumn() const { return editColumn_; }

    // ----------------------------------------------- RADVALET
    // Se filhuvudet. Programmatiskt: inget event, och en rad utanför — eller
    // `setSelected(rad, true)` på en kategori-, tom- eller dold rad — ⇒ logg
    // + no-op. Utan flerval väljer `setSelected(rad, true)` bort de andra.
    void          setSelectionMode(SelectionMode m);
    SelectionMode selectionMode() const { return selectionMode_; }
    void          setMultiSelect(bool on);
    bool          isMultiSelect() const { return multiSelect_; }
    void          setSelected(int row, bool on);
    bool          isSelected(int row) const;   // false i `Cell`-läget
    std::vector<int> selectedRows() const;     // visningsordning; tom i Cell
    void          selectAll();                 // bara med flerval
    void          clearSelection();

    // ANVÄNDAREN sparade en cell — `e.row`/`e.column`/`e.rows`, värdet ur
    // sender. `onCommit` fyras direkt efter, med samma args (se EVENTEN ovan).
    Handler<CellChangeArgs> onChange;
    Handler<CellChangeArgs> onCommit;
    // ANVÄNDAREN tryckte en knappcell — `e.row`/`e.column`.
    Handler<CellClickArgs>  onCellClick;
    // ANVÄNDAREN ändrade RADVALET (bara i `FullRow`) — läs det ur sender
    // (`selectedRows()`).
    Handler<ChangeArgs>     onSelectionChange;

    // ETT tabbstopp; editorerna är inredning.
    bool focusesChildren() const override { return true; }
    bool canReleaseChild(const Widget& child) const override;

    // TOOLTIPEN per cell (filhuvudet).
    TrText toolTipAt(const Ctx& c, float x, float y) const override;

    // setProp-egenskaper: selectionMode (`cell`/`fullrow`), multiSelect,
    // editOnClick, selectOnEdit, multiEdit — och Tables sortering, Panel och
    // Widget.
    bool setProp(const std::string& key, const std::string& value) override;

protected:
    // En datagrids rad ÄR en gridrad (PropertyGrid), inte en listrad — det är
    // GRUNDHÖJDEN; `Table::rowH` lägger ikonstorlekens regel ovanpå.
    float baseRowH(const Ctx& c) const override;
    float cellPadX(const Ctx& c) const override;

    void draw(const Ctx& c) override;
    // Radvalets märke i radrubrikens remsa — Minimalistics streck.
    void drawRowHeaderMarks(const Ctx& c, int position, int row,
                            gfx::Color fg) const override;
    bool keyDown(gfx::Key k, std::uint32_t mods) override;
    bool textInput(const char* utf8) override;
    void mouseDown(const Ctx& c, float mx, float my) override;
    // Huvudets släpp FÖRST (`ColumnPanel::mouseUp`), sedan knappcellens
    // släpp och radvalets väntande soloval.
    bool mouseUp(const Ctx& c, float mx, float my) override;
    void mouseMove(const Ctx& c, float mx, float my) override;
    // Avdelardraget är basens; en knappcell ser tryckt ut bara medan pekaren
    // står kvar över den (knappens semantik).
    void mouseDrag(const Ctx& c, float mx, float my) override;
    bool mouseCancel(gfx::MouseButton b) override;
    void mouseDoubleClick(const Ctx& c, float mx, float my) override;
    void enabledChanged() override;

    // Tables krokar för de typade cellerna (filhuvudet).
    float cellContentWidth(const Ctx& c, int row, int column) const override;
    bool  cellDrawsOwnImage(int row, int column) const override;   // knappen
    void  cellSet(int row, int column) override;   // felet släpps
    std::optional<double> cellSortValue(int row, int column) const override;
    void drawCellValue(const Ctx& c, const gfx::Rectf& cell,
                       const gfx::Rectf& content, int row, int column,
                       gfx::Color fg) const override;
    // Rubrikkrysset över kolumnens aktiva kryssceller (filhuvudet).
    std::optional<CheckState> headerCheckState(int column) const override;
    bool headerCheckToggled(int column) override;

    // EDITORN PLACERAS av datagriden (Bar-flexens krok): den aktiva editorn
    // ligger på den redigerade cellen, de dolda ingenstans.
    bool       placesChildren() const override { return true; }
    gfx::Rectf placeChild(const Widget& child, const Ctx& c) const override;

    void rowsReplaced() override;
    void rowAppended() override;
    void rowReplaced(int row) override;
    void rowRemoved(int row) override;
    void rowKindChanged(int row) override;
    void rowsHidden(const std::vector<int>& rows) override;
    void columnsReplaced() override;

    // ---------------------------------------------- ANVÄNDARVÄGARNA
    // Tangenternas och musens vägar (ListBoxens `selectFromUser`-prejudikat;
    // protected så en subklass, och en grind, når dem). `beginEditWith`
    // börjar redigera den aktuella fritext-cellen med `start` i stället för
    // cellens text (skrivandet). `moveFromUser` flyttar den aktuella cellen
    // (rader i visningsordningen, kolumner) och `tabFromUser` cell för cell
    // med radbyte — false när Tab ska lämna datagriden.
    void beginEditWith(const std::string& start);
    void moveFromUser(int dRow, int dColumn);
    bool tabFromUser(int dir);
    // RADVALETS väg (`ListView::selectFromUser`s form): raden STÅR
    // som aktuell, modifierarna gäller bara med flerval (Shift = spannet från
    // ankaret; Ctrl/Cmd vänder vid ett KLICK), och `onSelectionChange` fyras
    // när valet ändrades. No-op i `Cell`-läget. Svarar som `fireEvent`: false
    // = en handler rev eller flyttade datagriden.
    bool selectRowFromUser(int row, std::uint32_t mods, bool fromKey);

private:
    class TextEditor;     // fritext- och talcellens fält (data_grid.cpp)
    class ChoiceEditor;   // val-cellens lista
    class ColorEditor;    // färgcellens väljare

    // FRÅN HÄR NED är sektionen data_grid.cpp:s egen: lagringen och
    // hjälparna ändras med implementationen.

    // EN CELLS INSTÄLLNINGAR — glest per rad och kolumn, som cellfärgerna:
    // en egen redigerare, aktiveringen, tooltipen och användarens VÄNTANDE
    // otolkbara taltext. Felet släpps av varje `setCell` på cellen (`cellSet`)
    // och av `setRow`.
    struct CellSetting {
        std::optional<EditorSpec>  spec;
        bool                       enabled = true;
        TrText                     toolTip;
        std::optional<std::string> pending;
    };

    bool editOnClick_  = false;
    bool selectOnEdit_ = true;
    bool multiEdit_    = false;

    const CellSetting* setting(int row, int column) const;
    CellSetting*       settingFor(int row, int column);   // skapar posten
    // Användarens väntande otolkbara text i cellen (nullptr = inget fel).
    const std::string* pendingFor(int row, int column) const;
    void               clearPending(int row, int column);
    // Cellen går att ANVÄNDA nu: en datarad, inte radrubriken, cellen och
    // datagriden aktiva.
    bool cellActive(int row, int column) const;
    // Går cellen att REDIGERA — `cellActive` och en redigerare som ändrar
    // värdet (Text, Number, Check, Color, Choice med val).
    bool editable(int row, int column) const;
    bool pressable(int row, int column) const;   // en aktiv knappcell
    // Ritas cellen på det skrivskyddade fältets yta?
    bool readOnlySurface(int row, int column) const;
    // Första kolumnen som kan vara aktuell cell — kolumn 1 när radrubriken
    // tar kolumn 0 och det finns en till, annars 0.
    int  firstColumn() const;
    // Närmaste plats som inte är en tom rad, från `position` i riktningen
    // `dir` (och åt andra hållet när ingen finns där); −1 = ingen.
    int  landing(int position, int dir) const;
    void startEdit(const std::string* start);
    // Användarens flytt av den aktuella cellen + rulla i synfältet, och i
    // radläget valet efter (`selectRowFromUser` med `mods`/`fromKey`).
    // Svarar som `fireEvent`.
    bool setCurrentPosition(int position, int column, std::uint32_t mods = 0,
                            bool fromKey = true);
    bool assignSelection(const std::vector<int>& rows);   // true = ändrat
    // ANVÄNDARENS FÄLLNING (`Table::toggleCategoryFromUser`) och, när den
    // valde bort dolda rader (`rowsHidden` sätter flaggan), `onSelectionChange`
    // efter `onCollapse` i radläget. Svarar som `fireEvent`.
    bool toggleCategory(int row);
    bool hiddenDeselected_ = false;
    // AVSLUTET — EN väg för Enter, Escape, Tab, pilarna, valet och
    // fokusförlusten. `postad` = fokus lämnade editorn: eventet postas.
    // Svarar som `Widget::firePair`: false = en handler rev eller
    // flyttade datagriden, och den får inte röras igen.
    bool endEdit(bool spara, bool postad = false);
    bool saveEdit();                  // editorernas sparning: endEdit(true)
    void editorLostFocus();           // en editors focusLost
    void choicePicked();              // val-cellens lista valde ett NYTT värde
    void commitColor();               // färgväljarens onCommit
    // Raderna en sparning i (row, column) skriver: alla valda med samma sorts
    // redigerare när flera rader ändras på en gång, annars bara raden.
    std::vector<int> editTargets(int row, int column) const;
    // Användarens handlingar på en cell utan editor — svarar som `fireEvent`.
    bool toggleCheckFromUser(int row, int column);
    bool clickCellFromUser(int row, int column);
    // Enter (eller Space med `space`) på den aktuella cellen: true = tangenten
    // gjorde något i cellen, och svaret om datagriden lever i `alive`.
    bool activateCell(bool space, bool& alive);
    // onChange, därefter onCommit, med de ändrade raderna i `rows`.
    bool fireCellChange(int row, int column, std::vector<int> rows);

    std::vector<EditorSpec>               columnEdits_;   // per kolumnindex
    std::vector<std::vector<CellSetting>> cells_;   // parallell med raderna
    TextEditor*             text_          = nullptr;   // ägda barn (add<>)
    ChoiceEditor*           choice_        = nullptr;
    ColorEditor*            color_         = nullptr;
    int                     currentColumn_ = -1;
    int                     editRow_       = -1;
    int                     editColumn_    = -1;
    Editor                  editKind_      = Editor::Text;
    // Språkets decimaltecken för talcellernas visning.
    TrText                  sep_;
    // FÄRGENS sparning: cellen väljaren öppnades för och radgenerationen då
    // — en commit som kommer senare gäller bara samma rader (−1 = ingen).
    int                     colorRow_      = -1;
    int                     colorColumn_   = -1;
    unsigned                colorGen_      = 0;
    // KNAPPCELLEN: den som trycks och den som hovras (−1 = ingen), och om
    // pekaren står kvar över den tryckta.
    int                     pressRow_      = -1;
    int                     pressColumn_   = -1;
    bool                    pressInside_   = false;
    int                     hoverRow_      = -1;
    int                     hoverColumn_   = -1;
    // RADVALETS soloval vid släppet (raden, −1 = inget).
    int                     pendingSolo_   = -1;
    // RUBRIKKRYSSETS läge per kolumn, räknat mot datagridens skadestämpel.
    struct HeaderChecks {
        std::uint64_t                          stamp = 0;
        bool                                   valid = false;
        std::vector<std::optional<CheckState>> state;   // per kolumn
    };
    mutable HeaderChecks    headerChecks_;
    // RADVALET per rad (appens index, `char` som listvyns) — LAGRAT i båda
    // lägena, läst bara i `FullRow` (filhuvudet).
    std::vector<char>       selected_;
    int                     anchor_        = -1;   // spannets fasta ände (rad)
    SelectionMode           selectionMode_ = SelectionMode::Cell;
    bool                    multiSelect_   = false;
    // RADGENERATIONEN — räknas upp av `setRows` och `removeRow`, så ett
    // postat event ser att raden det beskriver inte längre finns i samma form. (Livstecknet för
    // samma post är basens — `Widget::postEvent`.)
    unsigned                rowsGen_       = 0;
};

} // namespace glaze
