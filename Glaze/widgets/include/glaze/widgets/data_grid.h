#pragma once
#include <string>
#include <vector>

#include "glaze/widgets/table.h"

// DataGrid — DATAGRIDDEN (wxGrid, WinForms
// DataGridView). En tabell med kolumnhuvud där cellerna går att REDIGERA:
// en fritext-kolumn med ett textfält, en val-kolumn med en vallista. Byggd
// på samma `Table` som ListView (kolumnerna, raderna, sorteringen, huvudet,
// scrollen). PropertyGrid är prejudikatet för editorerna, fast med n
// kolumner i stället för två; cellernas YTOR är fältets — skrivytan och den
// skrivskyddade ytan, som TextInputs.
//
// MÖNSTER: widgeten ritar SIG SJÄLV — draw() bor i src/widgets/
// data_grid.cpp med `switch (c.ui)` (hård regel 3).
//
// EDITORERNA ÄR RIKTIGA BARN, och EXAKT två: ett `TextInput` och en
// `Dropdown` (privata subklasser, som talfältets fält), ägda och dolda. En
// redigering visar EN av dem över cellen. PLACERINGEN är datagridens egen
// (`placeChild`, Bar-flexens krok): editorn läggs på cellen ur samma formel
// som ritningen (`Table::cellInContent`) vid varje `resolve()`, så den följer
// scroll, sortering och en dragen kolumnbredd utan en enda sparad koordinat.
// Datagriden är ETT tabbstopp (`focusesChildren`), och editorerna kan inte
// lossas utifrån (`canReleaseChild`). Editorn står i KOLUMNENS justering
// (`Column::align` → editorns `setAlign`), läst när redigeringen
// börjar — en högerjusterad talcell hoppar inte till vänster under fältet.
//
// KOLUMNTYPEN är en egenskap PER KOLUMNINDEX — `setColumnEditor(kolumn,
// Editor::Text|Choice|None)` och `setColumnChoices(kolumn, lista)`. `None` är
// en skrivskyddad kolumn, ritad som ett skrivskyddat fält
// (`fieldReadOnly`) med texten i full kontrast. Inställningen hör till
// INDEXET och överlever `setColumn`/`setColumns`, precis som `sortColumn`
// gör (table.h): satsordningen i en `.glazeui` ska inte avgöra om en
// kolumntyp gäller.
// En val-kolumn UTAN val går inte att redigera.
//
// REDIGERINGEN — WinForms och wxGrid i husets form:
//   börja    dubbelklick på den aktuella cellen, F2, eller att man BÖRJAR
//            SKRIVA (tecknet ersätter cellens text — en fritext-cell)
//   spara    Enter (och flytta ner en rad; Shift+Enter upp), Tab/Shift+Tab
//            (och flytta en cell), Upp/Ned (och flytta en rad), eller att
//            fokus lämnar editorn — ett klick någon annanstans, även i
//            datagridens eget huvud eller dess remsa
//   avbryt   Escape — texten som skrivits kastas
// En VAL-cell öppnar sin lista direkt. Ett val av ett NYTT värde sparar och
// avslutar redigeringen; Enter i listan avslutar den även när värdet är
// detsamma, och Escape avbryter i ett steg vare sig listan är öppen eller
// inte. Ett KLICK på samma värde stänger bara listan (dropdownen meddelar
// inget när valet inte ändrades) — editorn står då kvar tills Enter, Escape
// eller ett klick utanför.
//
// NAVIGERINGEN utan redigering: pilarna, Tab/Shift+Tab (rad för rad i
// kanten — i tabellens första respektive sista cell lämnar Tab datagriden),
// Home/End (radens första/sista cell), Ctrl+Home/Ctrl+End (tabellens),
// PageUp/PageDown, och Enter/Shift+Enter (ner/upp en rad).
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
// (table.h). Det fyras en gång per ANVÄNDARhandling som ändrade valet, aldrig
// av settrarna. Redigeringen är orörd; i radläget ramas den aktuella cellen
// in i stället för att fyllas. Ctrl gör INTE listvyns "flytta utan att välja"
// från tangentbordet — Ctrl+Home/End är datagridens (skriven gräns).
//
// SKRIVNA BEGRÄNSNINGAR utöver tabellens (table.h): en aktuell cell i taget
// (inget områdesval), inget eget event när den
// aktuella cellen byts, inga kryssrute-kolumner, ingen validering av texten,
// Delete tömmer inte en cell — och editorn kan täcka den lodräta remsan när
// den redigerade kolumnen når in under den (Panels skrivna regel: barnet
// vinner hit-testen, så remsan går att dra först när redigeringen är slut).

namespace glaze {

// Args till datagridens `onChange` — VILKEN cell som sparades. Ärver
// `ChangeArgs`, så en handler skriven för `ChangeArgs&` tar emot den också.
struct CellChangeArgs : ChangeArgs {
    int row    = -1;   // appens radindex
    int column = -1;
};

class DataGrid : public Table {
public:
    // CELLENS REDIGERARE — kolumnens TYP.
    enum class Editor { Text, Choice, None };

    // VALETS LÄGE: en aktuell CELL (förvalet) eller hela RADER.
    enum class SelectionMode { Cell, FullRow };

    DataGrid();

    // ---------------------------------------------------- KOLUMNTYPEN
    // Per kolumnINDEX; ett index utan inställning är `Text`. Kolumnen behöver
    // inte finnas än, men ett negativt index ⇒ logg + no-op. Programmatiskt:
    // inget event, och en pågående redigering i kolumnen avbryts.
    void   setColumnEditor(int column, Editor editor);
    Editor columnEditor(int column) const;
    void   setColumnChoices(int column, std::vector<std::string> choices);
    const std::vector<std::string>& columnChoices(int column) const;

    // ---------------------------------------------------- AKTUELL CELL
    // Programmatisk: inget event, en pågående redigering AVBRYTS, och den
    // scrollar inte (`ensureVisible`/`ensureColumnVisible` finns). Utanför
    // tabellen ⇒ logg + no-op.
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
    // Se filhuvudet. Programmatiskt: inget event, och en rad utanför ⇒ logg
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

    // ANVÄNDAREN sparade en cell — `e.row`/`e.column`, värdet ur sender.
    // `onCommit` fyras direkt efter, med samma args (se EVENTEN ovan).
    Handler<CellChangeArgs> onChange;
    Handler<CellChangeArgs> onCommit;
    // ANVÄNDAREN ändrade RADVALET (bara i `FullRow`) — läs det ur sender
    // (`selectedRows()`).
    Handler<ChangeArgs>     onSelectionChange;

    // ETT tabbstopp; editorerna är inredning.
    bool focusesChildren() const override { return true; }
    bool canReleaseChild(const Widget& child) const override;

    // setProp-egenskaper: selectionMode (`cell`/`fullrow`), multiSelect —
    // och Tables sortering, Panel och Widget.
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
    void mouseDoubleClick(const Ctx& c, float mx, float my) override;
    void enabledChanged() override;

    // EDITORN PLACERAS av datagriden (Bar-flexens krok): den aktiva editorn
    // ligger på den redigerade cellen, de dolda ingenstans.
    bool       placesChildren() const override { return true; }
    gfx::Rectf placeChild(const Widget& child, const Ctx& c) const override;

    void rowsReplaced() override;
    void rowAppended() override;
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
    class TextEditor;     // fritext-cellens fält (data_grid.cpp)
    class ChoiceEditor;   // val-cellens lista

    struct ColumnEdit {
        Editor                   editor = Editor::Text;
        std::vector<std::string> choices;
    };

    bool editable(int column) const;
    // Första kolumnen som kan vara aktuell cell — kolumn 1 när radrubriken
    // tar kolumn 0 och det finns en till, annars 0.
    int  firstColumn() const;
    void startEdit(const std::string* start);
    // Användarens flytt av den aktuella cellen + rulla i synfältet, och i
    // radläget valet efter (`selectRowFromUser` med `mods`/`fromKey`).
    void setCurrentPosition(int position, int column, std::uint32_t mods = 0,
                            bool fromKey = true);
    bool assignSelection(const std::vector<int>& rows);   // true = ändrat
    // AVSLUTET — EN väg för Enter, Escape, Tab, pilarna, valet och
    // fokusförlusten. `postad` = fokus lämnade editorn: eventet postas.
    // Svarar som `Widget::firePair`: false = en handler rev eller
    // flyttade datagriden, och den får inte röras igen.
    bool endEdit(bool spara, bool postad = false);
    bool saveEdit();                  // editorernas sparning: endEdit(true)
    void editorLostFocus();           // en editors focusLost
    void choicePicked();              // val-cellens lista valde ett NYTT värde
    bool fireCellChange(int row, int column);   // onChange, därefter onCommit

    std::vector<ColumnEdit> columnEdits_;   // per kolumnindex, kan vara kort
    TextEditor*             text_          = nullptr;   // ägda barn (add<>)
    ChoiceEditor*           choice_        = nullptr;
    int                     currentColumn_ = -1;
    int                     editRow_       = -1;
    int                     editColumn_    = -1;
    // RADVALET per rad (appens index, `char` som listvyns) — LAGRAT i båda
    // lägena, läst bara i `FullRow` (filhuvudet).
    std::vector<char>       selected_;
    int                     anchor_        = -1;   // spannets fasta ände (rad)
    SelectionMode           selectionMode_ = SelectionMode::Cell;
    bool                    multiSelect_   = false;
    // RADGENERATIONEN — räknas upp av `setRows`, så ett postat event ser att
    // raden det beskriver inte längre finns i samma form. (Livstecknet för
    // samma post är basens — `Widget::postEvent`.)
    unsigned                rowsGen_       = 0;
};

} // namespace glaze
