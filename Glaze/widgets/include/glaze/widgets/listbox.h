#pragma once
#include <string>
#include <vector>

#include "glaze/canvas/geometry.h"   // Length — textstorleken
#include "glaze/widgets/panel.h"

// ListBox — WinForms-listan: en rullande lista med ENKELVAL (eller FLERVAL)
// och valfria kryssrutor. MÖNSTER: widgeten ritar SIG SJÄLV — draw()/contentSize() och
// rad-geometrin bor i src/widgets/listbox.cpp (hård regel 3); ärver Panel
// så klipp, hjulscroll och overlay-remsorna kommer gratis, och
// rad-geometrin bygger på Panel::innerBounds().
//
// RADERNA ÄR DATA (strängar, inte widgets) — samma val som PropertyGrid,
// och av samma skäl: N rader som N widgets hade träffat alla tre
// komposit-luckorna samtidigt (tabbläckan i collectFocusable, det saknade
// borttags-API:t och autostorleken ur barnen). Som data har listan INGA
// widget-barn alls — kryssrutan är en GLYF, inte en Checkbox-widget — så
// den är ETT tabbstopp. Ingen Scrollbar som barn heller: Panel har redan
// overlay-remsor med tumdrag och rännklick.
//
// FLERVALET (WinForms
// `SelectionMode.MultiExtended`) är LISTVYNS, ord för ord (list_view.h):
// `setMultiSelect(true)` och sedan
//   klick            välj BARA raden
//   Ctrl+klick       vänd raden (Cmd+klick på macOS — båda räknas överallt)
//   Shift+klick      välj spannet från ANKARET (Ctrl+Shift lägger till)
//   Upp/Ned/Home/End flytta den aktuella raden och välj bara den; med
//                    Shift: spannet från ankaret; med Ctrl: flytta UTAN att
//                    välja (Ctrl+Space vänder då raden)
//   Ctrl+A           välj alla
// och SOLO VID SLÄPPET: ett tryck utan modifierare på en rad som redan är
// vald gör den bara aktuell och väljer den ensam först när knappen SLÄPPS —
// så att hela valet går att dra (`setAllowDrag`). Läsvägarna är listvyns:
// `isSelected(rad)`, `selectedRows()` (stigande), `selectedIndex()` (den
// aktuella om den är vald, annars den första valda) och `currentRow()`.
// Utan flerval gäller enkelvalet: ETT val i `selectedIndex_`, lagrat RÅTT.
// I flerval är valet per RAD i listan som den ser ut — `setSelected` och
// `setSelectedIndex` på en rad som inte finns ⇒ logg + no-op (listvyns
// regel), och `setItems` behåller valet per index (kryssens regel).
//
// Värden läses ur SENDER (regel 6): lb.selectedIndex()/selectedRows()/
// selectedText()/isChecked(i)/changedRow(), aldrig ur args. `onChange` fyras
// BARA vid ANVÄNDARändring av valet (en gång per handling), `onCheck` bara
// vid användartoggling av ett kryss; programmatiska settrar avfyrar ALDRIG
// något.
//
// SNABBMENYN är trädets: `contextRow()` är raden
// under det SENASTE högertrycket (−1 på tom yta, i remsan och när listan är
// inaktiverad), och högertrycket VÄLJER som Utforskaren — en rad utanför
// valet väljs ensam genom användarvägen, en rad i valet blir bara aktuell.
// Fokus flyttas inte. Träffvägen är `rowAt(localX, localY)` i LOGISKA px från
// listans hörn. Popuplistorna (dropdownens och menyns rader, filväljarens
// platser) svarar nej i `rightDown`: ett provisoriskt val går aldrig genom
// användarvägen, och ett högerklick på en plats ska inte navigera.
//
// SKRIVNA BEGRÄNSNINGAR (läs innan du "fixar" dem — de är beslut):
//
//   * INGEN AUTOSTORLEK. Panel/PropertyGrid-prejudikatet: en lista är en
//     yta APPEN dimensionerar. `width`/`height` 0 ⇒ 0.
//   * **`Dock::Fill` hjälper INTE i en `Bar`.** Bar ignorerar barnets
//     dock/anchors och använder barnets MÄTTA bredd — utan autostorlek
//     betyder det bredd 0. Sätt en explicit bredd; listan fyller inte
//     resten av baren, för något flex-grow-kontrakt finns inte.
//   * INGEN HORISONTELL SCROLL. `contentExtent` svarar med viewportens
//     bredd i x, så Panels andra overlayaxel aldrig aktiveras. Lång
//     radtext KLIPPS mot innehållsytan — ingen ellips-primitiv finns i
//     `Draw` (möjlig framtida växt).
//   * INGEN PageUp/PageDown. Posterna finns i `gfx::Key`, men att bläddra
//     SIDA för sida är ett
//     eget tillägg — "en sida" är viewportens höjd i rader — och ingen
//     widget gör det än. Tangenten bubblar alltså vidare.
//   * INGEN NUMPAD-GREN, och den behövs inte: `AppWindow` VIKER numpadens
//     navigationsläge (`KP_End` → `End`, `gfx::navigationKey`) innan den
//     fokuserade widgeten får tangenten, så listan bläddrar med
//     numpaden genom sina vanliga fall. Grind: `tests/numpad_gate.cpp`.
//   * INGEN TYPEAHEAD. Den kräver en `wantsTextInput`-avvägning som stör
//     fokus/caret-kontraktet.
//   * INGET EGET `onClick`. Musvalet sker i mouseDown-vägen; att också
//     exponera klick-eventet hade gett två vägar in i samma
//     tillståndsändring.

namespace glaze {

class ListBox : public Panel {
public:
    ListBox();

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR:
    // settern är den som INVALIDERAR. LAYOUT-fälten (x/y/width/height/
    // right/bottom/dock/anchors/tabIndex/name) är fortsatt RÅA — se
    // kontraktet i widget.h.

    // Byt innehåll. Checks-vektorn följer med (nya rader är okryssade,
    // borttagna rader tappar sitt kryss), `changedRow` nollställs och
    // scrollen klampas OMEDELBART så radritning och remsa räknar samma
    // värde. `selectedIndex` rörs INTE — visningen vaktar mot intervallet,
    // precis som Dropdown alltid gjort. O(n): vektorn kopieras.
    void setItems(std::vector<std::string> v);
    const std::vector<std::string>& items() const { return items_; }

    // ------------------------------------------ STYCKEFORMEN
    //
    // KOLLEKTIONSVOKABULÄREN är GEMENSAM för alla listbärande komponenter
    // (regel 8): `addX` · `setX(index, …)` · `setXs(lista)`,
    // indexbaserat, samma överallt. Dropdown bär exakt samma två metoder,
    // PropertyGrid bär `addRow`/`setRow`/`setRows`. Massformen och
    // läsvägen fanns redan och var redan konsekventa — det var
    // STYCKEFORMEN som saknades hos alla tre.
    //
    // Skälet den behövs: `.glazeui` tar SATSER, och hård regel 8 säger att
    // designern och koden är likvärdiga vägar. Med bara `setItems(vec)`
    // gick en lista att fylla i handkod men inte rad för rad.
    //
    // `addItem` appendar (krysset följer med som okryssat, som i
    // `setItems`). `setItem` kräver ett GILTIGT index — utanför
    // intervallet är det logg + no-op, samma form som `setChecked` redan
    // har: aldrig fail-safe, aldrig tyst tillväxt av listan.
    void addItem(std::string text);
    void setItem(int index, std::string text);

    // PROGRAMMATISK sättning: ändrar, invaliderar, avfyrar INGEN event —
    // och scrollar INTE (skrivet, så det inte förvånar; `ensureVisible`
    // finns för den som vill). I flerval väljer `setSelectedIndex` BARA
    // raden och gör den aktuell; −1 tömmer valet.
    void setSelectedIndex(int i);
    int  selectedIndex() const;
    std::string selectedText() const;

    // ---------------------------------------------- FLERVALET
    // Listvyns ord (filhuvudet). Av (standard) ⇒ ETT val; slås det av med
    // flera valda står bara `selectedIndex()` kvar.
    void setMultiSelect(bool on);
    bool isMultiSelect() const { return multiSelect_; }
    // Raden för sig. Utan flerval väljer `setSelected(rad, true)` bort de
    // andra (det är `setSelectedIndex`). Rad utanför ⇒ logg + no-op.
    void setSelected(int row, bool on);
    bool isSelected(int row) const;
    std::vector<int> selectedRows() const;   // stigande
    void selectAll();        // bara med flerval
    void clearSelection();
    // Den rad tangentbordet står på — utan flerval den valda (−1 = ingen).
    int  currentRow() const { return selectedIndex_; }

    void setShowChecks(bool on);
    bool showsChecks() const { return showChecks_; }

    // TEXTSTORLEKEN — `TextInput::setTextSize`s form och regel:
    // `glaze::px(…)` eller `glaze::mm(…)`, 0 = stilens. RADHÖJDEN följer
    // textens: stilens `listRowH` + skillnaden mellan storlekens radhöjd och
    // stilens, så luften runt texten är listans i varje storlek. Kryssrutan
    // står kvar i stilens mått. I `.glazeui` ett setter-fält,
    // `lista.setTextSize(glaze::mm(3));`.
    void   setTextSize(Length size);
    Length textSize() const { return textSize_; }

    // Kryssen — parallell vektor, alltid lika lång som items.
    // Programmatisk: ingen event, och `changedRow` sätts inte.
    void setChecked(int row, bool on);
    bool isChecked(int row) const;

    // Vilken rad som ändrades — läs i `onCheck` (regel 6, PropertyGrids
    // namn). −1 efter `setItems` och efter en ren valhändelse.
    int changedRow() const { return changedRow_; }

    // SNABBMENYN (filhuvudet). `rowAt` i LOGISKA px från listans hörn
    // (`MouseArgs::local`): −1 nedanför sista raden, i remsan, utanför
    // innehållsytan och utan ett öppet fönster. `contextRow` är raden under
    // det SENASTE högertrycket — −1 när listan är inaktiverad och efter
    // `setItems`.
    int rowAt(float localX, float localY) const;
    int contextRow() const { return isEnabled() ? contextRow_ : -1; }

    // Valet ändrades av ANVÄNDAREN (klick, pil, Home/End).
    Handler<ChangeArgs> onChange;
    // Ett kryss togglades av ANVÄNDAREN (klick i kryss-zonen, Space).
    Handler<ChangeArgs> onCheck;
    // ANVÄNDAREN ÄR KLAR — fyras direkt efter `onChange` (valet) OCH
    // efter `onCheck` (ett kryss): båda är hela handlingar. Skilj dem åt med
    // `changedRow()`, som är −1 efter ett rent val och raden efter ett kryss
    // — också om `onCheck` fyllt om listan: indexet är då den kryssade
    // radens, och det kan peka utanför eller på en annan rad i den nya
    // listan. Rev eller lossade `onCheck` listan fyras inget `onCommit`.
    Handler<ChangeArgs> onCommit;
    // AKTIVERINGEN (`ListView`s form): ANVÄNDAREN dubbelklickade på
    // den valda raden (utanför kryss-zonen) eller tryckte Enter med en vald
    // rad. Raden läses ur sender — `selectedIndex()`. Enter är listans bara
    // när den GÖR något: utan handler eller utan val bubblar tangenten, så
    // en dialogs standardknapp nås som vanligt.
    Handler<ActivateArgs> onActivate;

    bool focusable() const override { return true; }

    // setProp-egenskaper: items ("a;b;c" — TEXTformen; den FÖRLUSTFRIA
    // C++-listliteralen går via bld::parseStringList i verktygsvägen),
    // selectedIndex, showChecks, multiSelect.
    bool setProp(const std::string& key, const std::string& value) override;

    // Håll raden i synfältet. Kräver ägarfönster (annars no-op).
    void ensureVisible(int row);

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;

    // Raderna är DATA ⇒ basens barn-extent räcker inte. Bredden är
    // VIEWPORTENS (ingen horisontell scroll, se headernoten).
    gfx::Sizef contentExtent(const Ctx& c) const override;

    // Hjulet stegar i RADER (listans egen enhet), inte i Panels generella
    // `wheelStep`. Bubblar vidare när kanten är nådd.
    bool mouseWheel(const Ctx& c, float x, float y, float wheelX,
                    float wheelY) override;

    bool keyDown(gfx::Key k, std::uint32_t mods) override;
    void mouseDown(const Ctx& c, float mx, float my) override;
    void mouseMove(const Ctx& c, float mx, float my) override;
    // SOLO VID SLÄPPET (flervalet, filhuvudet) — och ett drag får ingen
    // `mouseUp`, så det väntande valet släpps i `dragStart`.
    bool mouseUp(const Ctx& c, float mx, float my) override;
    // Dubbelklick på den valda raden fyrar `onActivate`.
    void mouseDoubleClick(const Ctx& c, float mx, float my) override;
    void dragStart(const Ctx& c, DragStartArgs& e) override;
    // Högertrycket (filhuvudet): raden blir `contextRow()` och väljs som i
    // Utforskaren. Svarar ALLTID false, så snabbmenyn fyras.
    bool rightDown(const Ctx& c, float mx, float my) override;

    // ---------------------------------------------- ytan popup-listan ärver
    //
    // Dropdownens popup-lista ÄR en ListBox-subklass (dropdown.cpp), och
    // den här ytan är designad för den. Tillståndsmodellen:
    // Dropdown äger det COMMITTADE valet, popup-listan äger det
    // PROVISORISKA, och `Dropdown` har inget eget hover-index.

    // ---------------------------------------------- RADGEOMETRIN, EN sanning
    //
    // VARIABEL RADHÖJD ligger i BASEN med flit: sex beräkningar i listbox.cpp bygger på ett konstant
    // `listRowH`, och `Panel::scrollRange` härleder hela scrollkedjan ur
    // `contentExtent`. En `virtual rowH` som bara subklassen läste hade
    // gett TVÅ höjdsanningar och brutit regel 3 (geometri som ritningen
    // och beteendet delar definieras EN gång). Nu går ALLA sex — `rowRect`,
    // `rowAt`, `ensureVisible`, `contentExtent`, radloopens första synliga
    // rad och hjulsteget — genom kontraktet nedan.
    //
    // KOSTNADEN är uttalad: med olika höga rader är `rowTop`/`totalRowsH`/
    // `rowAtOffset` O(n). Det är acceptabelt (en meny har sällan över 30
    // rader), och den vanliga listan betalar det ALDRIG — `uniformRows()`
    // är true som default och då räknas allt i ett steg.

    // En rads höjd i LOGISKA px. Default: stilens `listRowH` för varje
    // rad, i listans textstorlek (`setTextSize`). En subklass som ger rader
    // olika höjd (menyns tunna separatorer) overridar den HÄR och svarar
    // false på `uniformRows()`.
    virtual float rowH(const Ctx& c, int row) const;

    // Ctx:t för radernas TEXT i listans storlek (`sizedText` i den
    // interna src/font_set.h) — `drawRow` och `rowH` läser det, och en
    // subklass som byter radens innehåll gör samma sak. Aldrig för
    // placeringen: `resolve` mäter syskonen med sitt Ctx.
    Ctx textCtx(const Ctx& c) const;

    // Har ALLA rader samma höjd? Svaret styr bara HASTIGHETEN, aldrig
    // resultatet: true ⇒ O(1)-vägarna nedan, false ⇒ prefixsummor.
    // En subklass som svarar fel får långsam kod, inte trasig.
    virtual bool uniformRows() const { return true; }

    // Radens överkant räknad från listans TOPP (oscrollad), logiska px.
    float rowTop(const Ctx& c, int row) const;
    // Alla raders sammanlagda höjd — ersätter `n * listRowH`.
    float totalRowsH(const Ctx& c) const;
    // Raden vid en OFFSET från listans topp (logiska px, scrollen
    // inräknad av anroparen) — ersätter divisionen. −1 = utanför.
    int   rowAtOffset(const Ctx& c, float y) const;
    // Första rad som är (delvis) synlig vid ett scrollvärde. Svarar
    // `items().size()` när scrollen ligger bortom sista raden, så
    // radloopen då ritar ingenting i stället för att klampa till sista.
    int   firstVisibleRow(const Ctx& c, float scroll) const;

    // Ritningen OCH musvägen läser exakt de här, i logiska px och
    // scroll-förskjutna.
    gfx::Rectf rowRect(const Ctx& c, int row) const;
    gfx::Rectf checkRect(const Ctx& c, int row) const;   // ur rowRect
    float      textX(const Ctx& c, int row) const;       // ur checkRect
    // Kryssets KLICKZON: rutan + `checkGap`, hela radhöjden — inte hela
    // kolumnen, så resten av raden fortfarande VÄLJER.
    gfx::Rectf checkZone(const Ctx& c, int row) const;
    // Ett tryck (fysiska px) i radens klickzon — false utan synliga kryss.
    bool inCheckZone(const Ctx& c, int row, float mx, float my) const;

    // Raden under en FYSISK muskoordinat; −1 = ingen (tom lista, utanför
    // innehållsytan, nedanför sista raden). Namnet skiljer sig från `rowAt`
    // så att ett namn inte bär två rymder.
    int rowAtY(const Ctx& c, float my) const;
    // Raden i HELA innehållsytan (fysiska px) — −1 i remsan och utanför i x
    // och y. Den publika `rowAt` och högertrycket delar den.
    int hitRow(const Ctx& c, float mx, float my) const;

    // ANVÄNDARVÄGARNA — den ENDA plats som fyrar event (Checkbox::clicked
    // är prejudikatet för att ha exakt en sådan väg). `selectFromUser` är
    // listvyns: modifierarna gäller bara i flerval (Shift = spann från
    // ankaret, Ctrl/Cmd = vänd vid klick och flytta utan att välja från
    // tangentbordet), och svaret är om valet ändrades och eventen fyrades.
    bool selectFromUser(int row, std::uint32_t mods = 0, bool fromKey = false);
    void toggleCheckFromUser(int row);   // changedRow + EXAKT ETT onCheck
                                         // (flyttar INTE valet)
    bool activateFromUser();             // EXAKT ETT onActivate — tog den?

    // POPUP-VÄGENS namn på den programmatiska sättaren. Samma kod som
    // `setSelectedIndex` — namnet är KONTRAKTET: det som popupen sätter är
    // ett PROVISORISKT val som kastas vid Escape, och det får därför
    // aldrig fyra `onChange`. Att popup-subklassen går genom den här och
    // aldrig genom `selectFromUser` är det som gör dubbelfyrning omöjlig
    // by design.
    void setProvisionalIndex(int i) { setSelectedIndex(i); }
    int  provisionalIndex() const { return selectedIndex_; }

    // REMSA-FÖRST (PropertyGrids mönster, kapslat). Overlay-remsorna ritas
    // OVANPÅ raderna och måste därför få händelsen först: utan den slutar
    // tummens hover fungera, rörelse över remsan flyttar markeringen till
    // raden under, och ett tryck kan välja en rad när användaren försöker
    // greppa tummen. `down` skiljer vägarna: ett TRYCK går till
    // Panel::mouseDown (tumgrepp/rännklick, och nollar drag-läget ÄVEN
    // utanför remsan), en RÖRELSE till Panel::mouseMove (het tumme).
    // true = remsan tog händelsen, rör inte valet.
    bool handleStripFirst(const Ctx& c, float mx, float my, bool down);

    // Hovrad rad (−1 = ingen) — den SVAGA markeringen. Invaliderar bara
    // vid ändring (regel 1).
    int  hoverRow() const { return hover_; }
    void setHoverRow(int row);

    // Ligger widgeten på en EGEN yta som inget lyser igenom? Minimalistic
    // fyller inte en lista i vila ("ytan är fönstrets"), men dropdownens
    // popup ÄR ett eget OS-fönster och har ingenting bakom sig — den
    // subklassen svarar true. Enda skillnaden mellan de två ritningarna.
    virtual bool ownsSurface() const { return false; }

    // Radens INNEHÅLL — kryssglyf (när kryssen visas) + text, båda i
    // radens egen textfärg `fg`; `boxPen` är kryssrutans kontur. DELAS av
    // alla tre ritningarna: det som skiljer familjerna åt är MARKERINGEN
    // runt raden, inte hur en rad är uppbyggd.
    //
    // VIRTUELL: en subklass byter INNEHÅLLET i
    // raden utan att röra markeringen, scrollen eller radloopen. Menyns
    // rader (glyfkolumn, genvägstext, undermenypil, separatorstreck) är
    // första användaren. De tre anropsställena i `draw()` får virtuell
    // dispatch gratis — inga andra rader ändrades.
    virtual void drawRow(const Ctx& c, int row, gfx::Color fg,
                         gfx::Color boxPen) const;

private:
    // Sätter flervalet till exakt `rader` — true när något ändrades (bara
    // då fyrar användarvägen).
    bool assignSelection(const std::vector<int>& rader);

    std::vector<std::string> items_;
    std::vector<bool>        checked_;      // alltid items_.size() lång
    // FLERVALET per rad — alltid items_.size() lång, och bara ettor med
    // flerval på (`char` och inte `bool`: en riktig vektor, som listvyns).
    std::vector<char>        selected_;
    // Enkelval: DEN valda raden (rå). Flerval: den AKTUELLA raden.
    int  selectedIndex_ = -1;               // −1 = inget valt
    int  hover_         = -1;
    int  changedRow_    = -1;
    int  anchor_        = -1;               // spannets fasta ände
    int  pendingSolo_   = -1;               // solo vid släppet, −1 = inget
    int  contextRow_    = -1;               // senaste högertrycket
    bool showChecks_    = false;
    bool multiSelect_   = false;
    Length textSize_;                        // 0 = stilens
};

} // namespace glaze
