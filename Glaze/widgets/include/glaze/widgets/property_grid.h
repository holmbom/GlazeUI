#pragma once
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <gfx/color.h>

#include "glaze/widgets/color_picker.h"   // ColorDisplay
#include "glaze/widgets/column_panel.h"   // ColumnSizing
#include "glaze/widgets/panel.h"

// PropertyGrid — tvåkolumners egenskapslista (Visual Studios grid): namn
// (alltid läs-bart) | värde. MÖNSTER: widgeten ritar SIG SJÄLV —
// draw()/contentSize() och rad-rectsen bor i src/widgets/property_grid.cpp
// (hård regel 3); ärver Panel så klipp, hjulscroll och overlay-listerna
// kommer gratis, och rad-geometrin bygger på Panel::innerRect().
//
// RADEN har ett SLAG (`Kind`) som bestämmer hur värdet visas och redigeras:
//   Text    fritext, eller — med `choices` — en vallista. Ett tryck i
//           värdecellen öppnar fältet respektive listan.
//   Check   en kryssruta. Ett tryck i värdecellen VÄXLER värdet direkt (och
//           fyrar `onChange` + `onCommit`); cellen får därefter en fokuserbar
//           redigerare där Space växlar igen och Enter/Escape stänger den.
//   Color   en färg, visad som `colorDisplay` säger: `Swatch` (förvalet) en
//           ruta ensam, `Style::colorCellSwatchW` bred, `Hex` hextexten ensam
//           och `Both` en ruta i kryssrutans storlek och hextexten efter den
//           (rutan med rutmönster under när `allowAlpha`). Ett tryck öppnar
//           färgväljarens popup direkt.
//   Number  ett tal med gränser (`min`/`max`), decimaler och text före och
//           efter (`prefix`/`suffix`). Fältet släpper bara in taltecken. Med
//           `allowEmpty` är en tom text ett giltigt TOMT värde (inget fel,
//           `numberValue` nullopt) som visar `emptyText`.
//   Category en kategorirad som fäller in och ut raderna fram till nästa
//           kategori; ANVÄNDARENS fällning fyrar `onExpand`/`onCollapse`.
// En vallista kan ha en ikon per val (`choiceIcons`, parallell med `choices`),
// och cellen visar det valdas ikon.
//
// KOLUMNERNA är två — 0 namnet, 1 värdet — med tabellernas lägen
// (`ColumnSizing`, column_panel.h): förvalet är namnet `Manual` (0 =
// stilens `gridNameW`) och värdet `Autosize`. `AutoMin` är det bredaste
// namnet respektive den bredaste värdecellen. Gridden rullar inte i sidled:
// får kolumnerna inte plats klipps de (skriven gräns).
// `value` är den ENDA lagringen och alltid TEXT: Check "true"/"false", Color
// `#RRGGBB` (`#RRGGBBAA` med `allowAlpha`), Number den kanoniska formen med
// punkt. Läsvägarna `isChecked`/`colorValue`/`numberValue` ger det typade
// värdet. Radens `tag` är en OPAK nyckel för anroparen.
//
// Raderna är DATA (structar, inte widgets). Exakt EN hostad editor per
// redigerartyp — ett `TextInput` (fritext och tal), en `Dropdown` (vallistor),
// en kryssrutedit och en `ColorPicker` (ägda barn, dolda) — flyttas till aktiv
// cell: de placeras i LOGISKA px relativt childArea och följer därmed
// scrollen automatiskt. Dolda editorer är inga tabbstopp.
//
// REDIGERINGEN COMMITTAR, som `DataGrid`s — inte live: en app som
// räknar om och bygger om raderna i sin handler (`setRows`) hade annars
// stängt editorn efter första tecknet, och det hade inte gått att skriva ett värde.
// Alltså:
//   börja    ett klick på en redigerbar värdecell. Fältet öppnas med HELA
//            texten MARKERAD, så det man skriver ersätter den — med
//            `setSelectOnEdit(false)` i stället med markören sist och
//            ingenting markerat.
//   spara    Enter, Tab eller att fokus lämnar editorn på annat sätt, och ett
//            klick någon annanstans i gridden — på en ANNAN rad sparas den
//            gamla först och öppnas den nya sedan. `rows()[i].value` skrivs,
//            `changedRow()` är raden, och `onChange` + `onCommit` fyras EN gång
//            var — bara när värdet faktiskt ändrades, också efter normaliseringen:
//            ett tal skrivet som "12,50" mot det sparade "12.5", och en färg
//            skriven `#3366ff` mot `#3366FF`, är samma värde och fyrar inget.
//   avbryt   Escape — editorn stängs, värdet orört, inga events.
// Ett TAL klampas mot `min`/`max` och avrundas till `decimals` när det sparas;
// bara en text som inte går att tolka är ett FEL — värdet står kvar, ingenting
// fyras, cellen visar den skrivna texten på `errorSurface`, `hasError(i)` är
// sann och verktygstipset säger "Ogiltigt värde". Felet försvinner med Escape
// i fältet, med nästa giltiga sparning och med `setRow`/`setRows` på raden.
// En VALLISTA committar vid valet, med `onCommit` direkt efter `onChange`.
// Enter och Escape AVSLUTAR den i ett steg, öppen eller stängd — Enter med
// listans val när den är öppen, Escape utan — och konsumeras, så en dialogs
// standard- och avbrytknapp aldrig klickas mitt i en cell (datagridens
// `ChoiceEditor`-form); kryssrutans och färgens redigerare konsumerar de två
// tangenterna på samma sätt. FÄRGEN committar när färgväljaren fyrar
// `onCommit` (paletten, släppta reglage, hexfältet), och redigeringen slutar
// när popupen stängs. Läs vilken rad ur SENDER
// (regel 6): `grid->changedRow()`/`rows()`.
//
// En KATEGORIRAD (`Kind::Category`) fäller in och ut vid ett tryck var som
// helst på raden (efter att en pågående redigering sparats). Raderna efter en
// kategori fram till nästa kategori hör till den; raderna före den första
// kategorin syns alltid. ANVÄNDARENS fällning fyrar `onExpand`/`onCollapse`
// med raden i `e.row`, efter att fällningen gjorts; `setExpanded` fyrar aldrig.
// Läget läses ur `rows()[i].expanded`.
//
// En INAKTIV rad (`enabled` false) visas dämpad: ingen editor och ingen
// växling, men verktygstipset syns ändå. `readOnly` är en annan sak — värdet
// visas med full kontrast men går aldrig att ändra.
//
// En INAKTIV GRID (`disable()`) syns vara inaktiv, som ett inaktivt fält:
// all text och alla tecken dämpade (`textDim`, rutornas kant `border`,
// färgrutorna tonade med `iconFade`) och värdekolumnen på `fieldReadOnly`.
// Den visas INFÄLLD — bara kategoriraderna (och raderna före den första) — utan
// att radernas `expanded` ändras, så den öppnas som förut när den aktiveras.
// En pågående redigering stängs utan att spara.
//
// HÖJDEN ur raderna: utan satt `height` är gridden de SYNLIGA raderna × radhöjden
// (plus ramens indrag), så staplade gridar i en rullande `Panel` ligger tätt
// efter varandra och följer varje fällning. En satt höjd vinner, och raderna
// rullar. Höjden beror inte på bredden.
//
// FOKUSFÖRLUSTENS events POSTAS till pump-svansen (`Widget::postEvent`):
// `focusLost` får aldrig köra användarkod. Värdet är skrivet redan när fokus
// flyttas, och det postade eventet SLOPAS om raderna bytts (`setRows`) eller
// just den raden ersatts (`setRow`) eller inaktiverats (`setRowEnabled`) innan det
// hann fram — det beskriver en rad som inte längre finns i den formen. Enter, och
// klicket på en annan rad, fyrar direkt — så en handler som bygger om raderna är
// klar INNAN nästa rad öppnas och river inte nästa redigering. Ett klick som
// INTE tar fokus — en `ToolButton`, ett menyval, en accelerator — sparar inte av
// sig självt: editorn står öppen och `rows()` bär det förra värdet, tills handlern
// anropar `AppWindow::commitEdits()` (eller `commitEdit()` här), som sparar och
// stänger editorn precis som Enter.
//
// PROGRAMMATISKT under en redigering: `setRows`, och `setRow` på den rad som
// redigeras, AVBRYTER den — editorn stängs utan att spara (raderna är andra,
// eller raden är ersatt). Det gör också `setRowEnabled(rad, false)` på raden som
// redigeras och `setExpanded` som döljer den. `addRow` gör det inte (en rad sist
// rör inte den som redigeras). Det är samma val som ovan, och det skiljer sig
// med flit från `DataGrid`, vars `setRow` låter redigeringen följa raden.
//
// SKRIVEN BEGRÄNSNING: gridden är inget tabbstopp själv, så när editorn
// stängs (Enter, Escape) står fokus kvar i den DOLDA editorn — tangenter når
// ingen widget, och nästa Tab börjar från fönstrets första stopp. Ett klick
// på en ny värdecell tar upp redigeringen igen.
//
// GENERISK med flit: choices kommer alltid utifrån — gridden känner inga
// GlazeBuilder-typer, inga widget-typnamn och inget om setProp (ingen
// reflektion finns; anroparen äger vokabulären).

namespace glaze {

class PropertyGrid : public Panel {
public:
    enum class Kind { Text, Check, Color, Number, Category };

    // ORDNINGEN ÄR DEN DESIGNERADE INIT-ORDNINGEN: `.glazeui` skriver
    // `{.name = "Bredd", .value = "120", .kind = Kind::Number}` och C++20 kräver
    // fälten i deklarationsordning.
    struct Row {
        std::string              name;           // kolumn 0; kategoriradens text
        std::string              value;          // värdet i TEXTFORM
        std::vector<std::string> choices;        // Text: tom = fritext, annars lista
        bool                     readOnly = false;   // visas men ändras ALDRIG
        std::string              tag;            // OPAK nyckel för anroparen
        Kind                     kind     = Kind::Text;
        double min = -std::numeric_limits<double>::infinity();   // Number
        double max = std::numeric_limits<double>::infinity();    // Number
        int    decimals = -1;     // Number: −1 kortaste formen, 0–9 fasta
        std::string prefix;       // Number: före talet ("1:")
        std::string suffix;       // Number: efter talet ("mm", "%")
        bool        allowAlpha = false;   // Color: `#RRGGBBAA`
        TrText      toolTip;      // radens egen; tom = gridens
        bool        enabled  = true;      // false = syns men går inte att ändra NU
        bool        expanded = true;      // Category: utfälld
        bool        allowEmpty = false;   // Number: tom text är ett värde
        std::string emptyText;            // Number: det som visas när tomt
        std::vector<ImageSource> choiceIcons;   // Text med choices: parallell
        ColorDisplay colorDisplay = ColorDisplay::Swatch;   // Color: rutan,
                                          // hextexten eller båda
    };

    PropertyGrid();

    // Byt innehåll — AVBRYTER en pågående redigering (editorn stängs utan
    // att spara) och slopar ett postat event som inte hunnit fram.
    //
    // RADERNA ÄNDRAS BARA HÄR, i `addRow`, `setRow` och de två tillståndssättarna
    // nedan. `rows()` är en LÄSVÄG: en muterbar referens hade låtit en rad
    // läggas till, bytas eller raderas förbi gridden — förbi invalideringen,
    // förbi den öppna editorn och förbi radgenerationerna som slopar en köad
    // sparning av en rad som inte längre finns i den formen. Ett nytt värde i en
    // rad är `setRow(i, …)`; en ny vallista är `setRows` (Row bär `choices`).
    void setRows(std::vector<Row> rows);
    const std::vector<Row>& rows() const { return rows_; }

    // ------------------------------------------ STYCKEFORMEN
    //
    // BÅDA VÄGARNA ska kunna allt (hård regel 8), och `.glazeui` tar
    // SATSER — inte uttryck med returvärden eller referenser in i vektorer.
    // `setRows(vec)` är massformen och byggs i handkod; den PLATTA formen
    // här är samma tillstånd uttryckt en rad i taget, alltså det designern
    // och en handskriven `.glazeui` kan emittera:
    //     grid.addRow({.name = "Synlig", .value = "true", .kind = Kind::Check});
    //     grid.setRow(0, {"Bredd", "160"});
    // Menyradens `addItem` (menu_bar.h) är prejudikatet, och de två vägarna
    // producerar exakt samma `rows_`.
    //
    // VOKABULÄREN ÄR GEMENSAM för alla listbärande komponenter:
    // `addX` · `setX(index, …)` · `setXs(lista)`. ListBox och
    // Dropdown bär samma form under namnet `addItem`/`setItem`/`setItems`.
    // `MenuBar` står utanför med flit — den är HIERARKISK och adresserar
    // med stig i stället för index.
    void addRow(Row row);

    // ERSÄTTER hela raden, inklusive de fält som utelämnas — det är
    // TILLDELNINGSsemantik (`Items[i] = …`) och inte partiell uppdatering.
    // Annars hade `setRow(0, {"Bredd", "160"})` tyst behållit en gammal
    // `tag` eller `choices`. Index utanför intervallet ⇒ logg + no-op
    // (`ListBox::setChecked`-formen): aldrig fail-safe, aldrig tyst
    // tillväxt av listan. Redigeras just den raden AVBRYTS redigeringen, och
    // radens fel är borta.
    void setRow(int index, Row row);

    // TILLSTÅND PER RAD — samma form som radens övriga ändringar: index
    // utanför ⇒ logg + no-op. `setRowEnabled(rad, false)` stänger en redigering
    // av raden UTAN att spara och slopar en köad sparning av den;
    // `setExpanded` gäller bara en kategorirad (en annan rad ⇒ logg + no-op),
    // stänger en redigering vars rad döljs utan att spara och fyrar ingenting.
    void setRowEnabled(int row, bool on);
    void setExpanded(int row, bool on);

    // LÄSVÄGARNA ur `value`. `isChecked`: en Check-rad vars värde är
    // `true`/`yes`/`1` (annars false, också för en annan sorts rad).
    // `colorValue`: en Color-rad, opak utan `allowAlpha` (`ColorPicker`s
    // läsregel); `numberValue`: en Number-rad, KLAMPAD och AVRUNDAD vid
    // läsningen som `SpinBox::value`. En text som inte går att tolka ger
    // `nullopt`. `hasError`: en Number- eller Color-rad där användarens text
    // eller appens värde inte går att tolka.
    bool                      isChecked(int row) const;
    std::optional<gfx::Color> colorValue(int row) const;
    std::optional<double>     numberValue(int row) const;
    bool                      hasError(int row) const;

    // TEXTEN MARKERAD när en fritext- eller talrad börjar redigeras (förvalet
    // true): hela texten, så det man skriver ersätter den; med false står
    // markören sist och ingenting är markerat. Gäller nästa redigering.
    void setSelectOnEdit(bool on);
    bool selectOnEdit() const { return selectOnEdit_; }

    // setProp-egenskaper: selectOnEdit — och Panel och Widget.
    bool setProp(const std::string& key, const std::string& value) override;

    int activeRow() const { return active_; }    // rad med öppen editor
    // Raden användaren senast ÄNDRADE — läs i onChange/onCommit (regel 6).
    // −1 efter `setRows`. I `onCommit` är den ALLTID den sparade radens
    // index, också om `onChange` byggt om raderna: den sätts igen
    // mellan de två. Med nya rader pekar indexet då in i dem — samma rad om
    // ombyggnaden behöll ordningen (en omräkning gör det), annars en
    // annan eller ingen; en sådan handler läser `rows()` med en
    // gränskontroll.
    int changedRow() const { return changed_; }


    // TILLSTÅNDET är PRIVAT och byts via SETTRAR:
    // settern är den som INVALIDERAR, och utan den syns en tilldelning
    // från en timer-tick, en färdigrapport eller `glaze::app().post`
    // först när något ANNAT råkar rita om. LAYOUT-fälten (x/y/width/
    // height/right/bottom/dock/anchors/tabIndex/name) är fortsatt RÅA —
    // se kontraktet i widget.h.

    // KOLUMNERNA (filhuvudet): 0 namnet, 1 värdet. Bredden i LOGISKA px
    // (0 = förvalet: `gridNameW` för namnet, `columnDefaultW` för värdet) —
    // Manual: bredden, Autosize: golvet, AutoMin: läses inte. Absolut, inte
    // andel — stabil när overlay-listen tar plats. Kolumn utanför 0–1 eller
    // negativ bredd ⇒ logg + no-op. Invaliderar bara vid ändring.
    void         setColumnWidth(int column, float width);
    float        columnWidth(int column) const;
    void         setColumnSizing(int column, ColumnSizing sizing);
    ColumnSizing columnSizing(int column) const;

    // ANVÄNDAREN sparade ett nytt värde i raden `changedRow()` — och är klar
    // med det: `onCommit` fyras direkt efter `onChange`, för samma sparning
    // (en sparad cell är en avslutad handling, `DataGrid`s form).
    Handler<ChangeArgs> onChange;
    Handler<ChangeArgs> onCommit;
    // ANVÄNDAREN fällde en kategorirad ut respektive ihop — `e.row`.
    // `setExpanded` fyrar aldrig.
    Handler<RowArgs>    onExpand;
    Handler<RowArgs>    onCollapse;

    // Enter utan tangenten (widget.h): en öppen fritexteditor sparas och
    // stängs NU, och det skrivna i en öppen färgpopups hexfält committas —
    // `AppWindow::commitEdits()` når den. En vallista har inget som väntar;
    // den sparar vid valet.
    void commitEdit() override;

    // TOOLTIPEN PER RAD: felets text vinner ("Ogiltigt värde"), sedan radens
    // egen, sedan gridens (`setToolTip`). Gäller också en inaktiv rad och en
    // kategorirad.
    TrText toolTipAt(const Ctx& c, float x, float y) const override;

    // STRUKTURELLA BARN. Editorerna skapas med `add<>` i konstruktorn och
    // avrefereras utan kontroll i `openEditor`/`hideEditors`/`endEdit`.
    // `children()` är publikt, så en anropare kan få tag i dem och skriva
    // `grid.unadopt(*editor)` — utan vakten förstörs objektet medan pekaren
    // ligger kvar, alltså use-after-free vid nästa redigering. `releaseOwned`
    // vore dessutom semantiskt trasigt: griden skulle fortsätta styra ett
    // objekt som inte längre är dess barn.
    //
    // Definierad i cpp:n — editorerna är bara framåtdeklarerade här, och en
    // bas-konvertering kräver en komplett typ.
    bool canReleaseChild(const Widget& child) const override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    // Raderna är data ⇒ basens barn-extent räcker inte — maxa mot
    // de synliga radernas höjd så scrollen täcker HELA listan.
    gfx::Sizef contentExtent(const Ctx& c) const override;

    void mouseDown(const Ctx& c, float mx, float my) override;

    // Inaktiverad: redigeringen stängs och rubrikerna visas infällda.
    void enabledChanged() override;

private:
    // Cellgeometrin i widget-lokala LOGISKA px, SCROLL-förskjuten (samma
    // rymd som ritningen). Cellträffen och editor-placeringen PROJICERAR
    // dem till fysiska px — aldrig två uppsättningar mått. Privata: bara
    // den här widgeten använder dem. En dold rad (en infälld kategoris) har
    // ingen plats och en tom rekt.
    //
    // KOLUMNERNAS EFFEKTIVA BREDD ur lägena (`columnsizing`) och
    // innehållsbredden — innerytan minus overlay-listens remsa. `nameW` är
    // kolumn 0:s.
    float      columnW(const Ctx& c, int column) const;
    float      nameW(const Ctx& c) const { return columnW(c, 0); }
    // AutoMin-bredden: det bredaste namnet respektive den bredaste
    // värdecellen bland ALLA rader utom kategorierna (en fällning ändrar inte
    // bredderna), med cellens luft och minst `columnMinW`. Cachad mot
    // `damageStamp()` och miljön — varje ändring av raderna invaliderar.
    float      fittedW(const Ctx& c, int column) const;
    // Värdecellens innehållsbredd utan luft (de typade cellernas mått).
    float      valueContentW(const Ctx& c, int row) const;
    // Scrollen klampad mot de synliga raderna (innehållsytan `in`) — samma
    // värde som Panels klampning av barnens yta, så raderna och editorerna
    // aldrig glider isär efter en fällning.
    float      scrollOffset(const Ctx& c, const gfx::Rectf& in) const;
    gfx::Rectf rowBounds(const Ctx& c, int row) const;
    gfx::Rectf valueBounds(const Ctx& c, int row) const;
    // Värdecellens INNEHÅLL — cellen minus `gridPadX` på båda sidor, den yta
    // de typade cellerna ritas och placeras i (`cell` i samma rymd).
    static gfx::Rectf cellContent(const Ctx& c, const gfx::Rectf& cell);
    // Det valda valets ikon i en vallista — en tom bild utan.
    const ImageSource& choiceIcon(int row) const;
    // Raden under en punkt (FYSISKA px) bland de synliga, annars −1.
    int rowAt(const Ctx& c, float mx, float my) const;

    // De SYNLIGA raderna: en rad efter en infälld kategori är dold. `pos_[i]` är
    // radens plats bland de synliga, eller −1. Byggs om vid varje ändring av
    // rader eller fällning, och `contentSize` läser antalet — varje sådan
    // ändring anropar därför `invalidate()`.
    void rebuildVisible();

    // Ritningen: en rad (kategori eller namn + värde) och värdecellens innehåll.
    void drawRow(const Ctx& c, int row, const gfx::Rectf& r) const;
    void drawCell(const Ctx& c, int row, const gfx::Rectf& r) const;
    // Talets visningstext: användarens ogiltiga text, annars det klampade och
    // avrundade värdet med språkets decimaltecken, annars den tomma texten
    // (`emptyText`) för ett tillåtet tomt värde, annars appens råtext.
    std::string numberDisplay(int row) const;
    // Fältets starttext: samma, utom att ett tomt värde ger en tom text.
    std::string numberEditText(int row) const;
    // ANVÄNDAREN fällde kategoriraden: fällningen och sedan `onExpand`/
    // `onCollapse`. Svarar som `fireEvent`.
    bool toggleCategoryFromUser(int row);

    bool valid(int row) const { return row >= 0 && std::size_t(row) < rows_.size(); }

    class TextEditor;     // fritextens och talets fält — property_grid.cpp
    class ChoiceEditor;   // vallistan, en `Dropdown` — property_grid.cpp
    class CheckEditor;    // kryssrutans fokusbärare — property_grid.cpp
    class ColorEditor;    // färgväljaren — property_grid.cpp

    void openEditor(const Ctx& c, int row);
    void hideEditors();
    // AVSLUTET av fritextens och talets redigering — EN väg för Enter, Escape,
    // klicket och fokusförlusten. `postad` = fokus lämnade editorn: eventen
    // postas. Båda svarar som `Widget::firePair`: false = en handler rev
    // eller flyttade gridden, och den får inte röras igen.
    bool endEdit(bool spara, bool postad);
    bool toggleActive();        // kryssrutans växling: värdet, sedan eventen
    void commitColor();         // färgväljarens `onCommit` → raden
    bool fireChange(int row);   // changed_ = row; onChange, därefter onCommit

    // FRÅN HÄR NED är lagringen property_grid.cpp:s egen och ändras med
    // implementationen.
    float            colWidth_[2]  = {0.0f, 0.0f};
    ColumnSizing     colSizing_[2] = {ColumnSizing::Manual, ColumnSizing::Autosize};
    std::vector<Row> rows_;
    std::vector<int> pos_;               // parallell med rows_
    int              visibleCount_ = 0;
    TextEditor*   editor_   = nullptr;   // ägda barn (add<> i ctorn), dolda
    ChoiceEditor* picker_   = nullptr;
    CheckEditor*  checkEd_  = nullptr;
    ColorEditor*  colorEd_  = nullptr;
    int         active_  = -1;
    int         changed_ = -1;
    bool        selectOnEdit_ = true;
    // RADGENERATIONERNA — ett postat event beskriver EN rad i EN radlista.
    // `rowsGen_` räknas upp av `setRows` (hela listan är ny, DataGrids) och
    // `rowGen_[i]` av `setRow(i)` och `setRowEnabled(i, false)` (just den raden
    // är ersatt eller stängd) — en köad sparning av rad i slopas i båda fallen,
    // men inte av en `setRow` på en annan rad eller av `addRow`. Parallell med
    // `rows_`.
    unsigned              rowsGen_ = 0;
    std::vector<unsigned> rowGen_;
    // Användarens OTOLKBARA text per Number-rad — raden visar den som ett fel
    // tills den ersätts. Parallell med `rows_`.
    std::vector<std::optional<std::string>> pending_;
    // Färgraden vars popup öppnades, med generationerna vid öppningen: en
    // postad commit (hexfältet, stängningen) gäller bara om raderna och just
    // den raden är desamma. Nollas inte av `hideEditors`, för commiten kommer
    // efter stängningen.
    int      colorRow_     = -1;
    unsigned colorRowsGen_ = 0;
    unsigned colorRowGen_  = 0;
    TrText   sep_;   // decimaltecknet — löser upp sig själv
    // AutoMin-breddernas cache (`fittedW`): giltig för stämpeln, stilen,
    // fonten och skalan den räknades i.
    mutable float               fitW_[2]   = {0.0f, 0.0f};
    mutable bool                fitValid_  = false;
    mutable std::uint64_t       fitStamp_  = 0;
    mutable const Style*        fitStyle_  = nullptr;
    mutable const gfx::Font*    fitFont_   = nullptr;
    mutable float               fitScale_  = 0.0f;
};

} // namespace glaze
