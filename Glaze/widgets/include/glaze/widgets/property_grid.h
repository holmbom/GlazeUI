#pragma once
#include <vector>

#include "glaze/widgets/panel.h"

// PropertyGrid — tvåkolumners egenskapslista (Visual Studios grid): namn
// (alltid läs-bart) | värde (fritext eller vallista). MÖNSTER: widgeten
// ritar SIG SJÄLV — draw()/contentSize() och rad-rectsen bor i
// src/property_grid.cpp (hård regel 3); ärver Panel så klipp, hjulscroll
// och overlay-listerna kommer gratis, och rad-geometrin bygger på
// Panel::innerRect().
//
// Raderna är DATA (structar, inte widgets). Exakt EN hostad editor för
// fritext (ett `TextInput`) och EN hostad `Dropdown` för vallistor (ägda
// barn, dolda) flyttas till aktiv cell — de placeras i LOGISKA px relativt
// childArea och följer därmed scrollen automatiskt.
//
// REDIGERINGEN COMMITTAR, som `DataGrid`s — inte live: en app som
// räknar om och bygger om raderna i sin handler (`setRows`) hade annars
// stängt editorn efter första tecknet, och det hade inte gått att skriva ett värde.
// Alltså:
//   börja    ett klick på en redigerbar värdecell. Editorn öppnas med HELA
//            texten MARKERAD, så det man skriver ersätter den.
//   spara    Enter, Tab eller att fokus lämnar editorn på annat sätt, och ett
//            klick någon annanstans i gridden — på en ANNAN rad sparas den
//            gamla först och öppnas den nya sedan. `rows()[i].value` skrivs,
//            `changedRow()` är raden, och `onChange` + `onCommit` fyras EN gång
//            var — bara när värdet faktiskt ändrades.
//   avbryt   Escape — editorn stängs, värdet orört, inga events.
// En VALLISTA committar vid valet, med `onCommit` direkt efter
// `onChange`. Enter och Escape AVSLUTAR den i ett steg, öppen eller stängd —
// Enter med listans val när den är öppen, Escape utan — och konsumeras, så en
// dialogs standard- och avbrytknapp aldrig klickas mitt i en cell (datagridens
// `ChoiceEditor`-form). Läs vilken rad ur SENDER (regel 6): `grid->changedRow()`/
// `rows()`.
//
// FOKUSFÖRLUSTENS events POSTAS till pump-svansen (`Widget::postEvent`):
// `focusLost` får aldrig köra användarkod. Värdet är skrivet redan när fokus
// flyttas, och det postade eventet SLOPAS om raderna bytts (`setRows`) eller
// just den raden ersatts (`setRow`) innan det hann fram — det beskriver en
// rad som inte längre finns i den formen. Enter, och klicket på en annan rad, fyrar direkt — så en
// handler som bygger om raderna är klar INNAN nästa rad öppnas och river inte
// nästa redigering. Ett klick som INTE tar fokus — en `ToolButton`, ett
// menyval, en accelerator — sparar inte av sig självt: editorn står öppen och
// `rows()` bär det förra värdet, tills handlern anropar
// `AppWindow::commitEdits()` (eller `commitEdit()` här), som sparar och
// stänger editorn precis som Enter.
//
// PROGRAMMATISKT under en redigering: `setRows`, och `setRow` på den rad som
// redigeras, AVBRYTER den — editorn stängs utan att spara (raderna är andra,
// eller raden är ersatt). `addRow` gör det inte (en rad sist rör inte den som
// redigeras). Det är samma val som ovan, och det skiljer sig med flit från
// `DataGrid`, vars `setRow` låter redigeringen följa raden.
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
    struct Row {
        std::string name;                    // kolumn 1 — alltid läs-bar
        std::string value;                   // kolumn 2
        std::vector<std::string> choices;    // tom = fritext, annars lista
        bool        readOnly = false;        // visas men redigeras inte
        std::string tag;                     // OPAK nyckel för anroparen
    };

    PropertyGrid();

    // Byt innehåll — AVBRYTER en pågående redigering (editorn stängs utan
    // att spara) och slopar ett postat event som inte hunnit fram.
    //
    // RADERNA ÄNDRAS BARA HÄR, i `addRow` och i `setRow`. `rows()` är
    // en LÄSVÄG: en muterbar referens hade låtit en rad läggas till, bytas
    // eller raderas förbi gridden — förbi invalideringen, förbi den öppna
    // editorn och förbi radgenerationerna som slopar en köad sparning av en
    // rad som inte längre finns i den formen. Ett nytt värde i en rad är
    // `setRow(i, …)`; en ny vallista är `setRows` (Row bär `choices`).
    void setRows(std::vector<Row> rows);
    const std::vector<Row>& rows() const { return rows_; }

    // ------------------------------------------ STYCKEFORMEN
    //
    // BÅDA VÄGARNA ska kunna allt (hård regel 8), och `.glazeui` tar
    // SATSER — inte uttryck med returvärden eller referenser in i vektorer.
    // `setRows(vec)` är massformen och byggs i handkod; den PLATTA formen
    // här är samma tillstånd uttryckt en rad i taget, alltså det designern
    // och en handskriven `.glazeui` kan emittera. Menyradens `addItem`
    // (menu_bar.h) är prejudikatet, och de två vägarna producerar exakt
    // samma `rows_`.
    //
    // VOKABULÄREN ÄR GEMENSAM för alla listbärande komponenter:
    // `addX` · `setX(index, …)` · `setXs(lista)`. ListBox och
    // Dropdown bär samma form under namnet `addItem`/`setItem`/`setItems`.
    // `MenuBar` står utanför med flit — den är HIERARKISK och adresserar
    // med stig i stället för index.
    //
    // `choices` uttrycks i `setRows`-literalen och har medvetet ingen egen
    // metod: med massformen är den redan uttryckbar.
    void addRow(std::string name, std::string value = "",
                bool readOnly = false, std::string tag = "");

    // ERSÄTTER hela raden, inklusive de fält som utelämnas — det är
    // TILLDELNINGSsemantik (`Items[i] = …`) och inte partiell uppdatering.
    // Annars hade `setRow(0, "Bredd", "160")` tyst behållit en gammal
    // `tag` eller `choices`. Index utanför intervallet ⇒ logg + no-op
    // (`ListBox::setChecked`-formen): aldrig fail-safe, aldrig tyst
    // tillväxt av listan. Redigeras just den raden AVBRYTS redigeringen.
    void setRow(int index, std::string name, std::string value = "",
                bool readOnly = false, std::string tag = "");

    // setProp-egenskaper: nameWidth (`grid.setNameWidth(120);`).
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

    // Namnkolumnens bredd i LOGISKA px; 0 = griddens default. Absolut,
    // inte andel — stabil när overlay-listen tar plats.
    void setNameWidth(float w)
    {
        if (nameWidth_ == w)
            return;
        nameWidth_ = w;
        invalidate();
    }
    float nameWidth() const { return nameWidth_; }

    // ANVÄNDAREN sparade ett nytt värde i raden `changedRow()` — och är klar
    // med det: `onCommit` fyras direkt efter `onChange`, för samma sparning
    // (en sparad cell är en avslutad handling, `DataGrid`s form).
    Handler<ChangeArgs> onChange;
    Handler<ChangeArgs> onCommit;

    // Enter utan tangenten (widget.h): en öppen fritexteditor sparas och
    // stängs NU — `AppWindow::commitEdits()` når den. En vallista har inget
    // som väntar; den sparar vid valet.
    void commitEdit() override;

    // STRUKTURELLA BARN. `editor_` och `picker_`
    // skapas med `add<>` i konstruktorn och avrefereras utan kontroll i
    // `openEditor`/`hideEditors`/`endEdit`. `children()` är publikt, så en
    // anropare kan få tag i dem och skriva `grid.unadopt(*editor)` — utan
    // vakten förstörs objektet medan pekaren ligger kvar, alltså
    // use-after-free vid nästa redigering. `releaseOwned` vore dessutom
    // semantiskt trasigt: griden skulle fortsätta styra ett objekt som
    // inte längre är dess barn.
    //
    // Definierad i cpp:n — editorerna (`TextEditor`, `ChoiceEditor`) är bara
    // framåtdeklarerade här, och en bas-konvertering kräver en komplett typ.
    bool canReleaseChild(const Widget& child) const override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    // Raderna är data ⇒ basens barn-extent räcker inte — maxa mot
    // radernas höjd så scrollen täcker HELA listan.
    gfx::Sizef contentExtent(const Ctx& c) const override;

    void mouseDown(const Ctx& c, float mx, float my) override;

private:
    // Cellgeometrin i widget-lokala LOGISKA px, SCROLL-förskjuten (samma
    // rymd som ritningen). Cellträffen och editor-placeringen PROJICERAR
    // dem till fysiska px — aldrig två uppsättningar mått. Privata: bara
    // den här widgeten använder dem.
    float      nameW(const Ctx& c) const;                    // kolumnbredd
    gfx::Rectf rowBounds(const Ctx& c, int row) const;
    gfx::Rectf valueBounds(const Ctx& c, int row) const;

    class TextEditor;     // fritextens fält — property_grid.cpp
    class ChoiceEditor;   // vallistan, en `Dropdown` — property_grid.cpp

    void openEditor(const Ctx& c, int row);
    void hideEditors();
    // AVSLUTET av fritextens redigering — EN väg för Enter, Escape, klicket
    // och fokusförlusten. `postad` = fokus lämnade editorn: eventen postas.
    // Båda svarar som `Widget::firePair`: false = en handler rev
    // eller flyttade gridden, och den får inte röras igen.
    bool endEdit(bool spara, bool postad);
    bool fireChange(int row);   // changed_ = row; onChange, därefter onCommit

    float            nameWidth_ = 0.0f;
    std::vector<Row> rows_;
    TextEditor*   editor_  = nullptr;   // ägda barn (add<> i ctorn), dolda
    ChoiceEditor* picker_  = nullptr;
    int         active_  = -1;
    int         changed_ = -1;
    // RADGENERATIONERNA — ett postat event beskriver EN rad i EN radlista.
    // `rowsGen_` räknas upp av `setRows` (hela listan är ny, DataGrids) och
    // `rowGen_[i]` av `setRow(i)` (just den raden är ersatt) — en
    // köad sparning av rad i slopas i båda fallen, men inte av en `setRow`
    // på en annan rad eller av `addRow`. Parallell med `rows_`.
    unsigned              rowsGen_ = 0;
    std::vector<unsigned> rowGen_;
};

} // namespace glaze
