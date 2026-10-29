#pragma once
#include <string>
#include <vector>

#include "glaze/widgets/bar.h"

// StatusBar — statusraden: samma flex-motor som
// TopBar men OPT-IN. Den är ett publikt
// VÄRDE-FÄLT på `glaze::AppWindow` — `window.statusBar` — som skapas med
// fönstret, adopteras i konstruktorn och är DOLD tills något gör
// `statusBar.show();`. En dold widget skär ingen remsa ur innehållsytan,
// och ordningen i trädet är DETERMINISTISK (en lat variant hade placerat
// raden där det råkade bli, beroende på när `statusBar()` först
// anropades) — och raden går att uttrycka i `.glazeui`.
//
// HÖJDEN är `Style::statusBarH` och inte ett tal i den här filen: ett MÅTT
// i en widget-fil vore mot regel 3:s anda — och `setContentSize` räknar
// baklänges genom chromet, så ett tal som inte följer stilen hade gjort just den
// uträkningen oärlig.
//
// FÄLTEN (WinForms
// `StatusBarPanel`, wx:s `SetFieldsCount`/`SetStatusText`). Raden delas i
// PANELER, var och en en text, i KOLLEKTIONSVOKABULÄREN:
//     statusBar.addPanel("Klar", 0, true);      // fyller resten
//     statusBar.addPanel("Rad 1, kol 1", 110);  // fast bredd
//     statusBar.addPanel("100 %");              // AUTO: texten + luften
//     statusBar.setPanelText(1, "Rad 12, kol 4");
// `addPanel(text, bredd, fyll)` · `setPanel(index, text, bredd, fyll)`
// (tilldelning: en utelämnad bredd blir AUTO och fyllningen av) ·
// `setPanels(lista)` — och `setPanelText(index, text)` för den täta
// uppdateringen, tabellens `setCell`-prejudikat: bara texten, resten står.
// BREDDEN: `> 0` fast i logiska px (en för lång text KORTAS med ellips),
// `<= 0` AUTO — texten plus `statusPanelPadX` på var sida — och `fill` lägger
// en lika andel av det LEDIGA utrymmet OVANPÅ fältets bredd (satt eller AUTO;
// den är golvet), så ett fyllande fält kapar aldrig sin text när raden rymmer
// den. Ryms inte raden rinner den över åt höger (Bar-flexens regel); ett
// fyllande fält som hellre ska kortas får en liten satt bredd som golv.
// Texten är en `TrText`, så `glaze::tr(id)` följer språket. En
// hårlinje skiljer ett fält från det SYNLIGA barnet före (separatorns penna).
// Index utanför ⇒ `gfx::log` + no-op.
//
// FÄLTEN ÄR BARN, i barnordning tillsammans med appens egna — en
// förloppsindikator som adopteras står där den adopteras, och Bar-flexen
// placerar alltihop. Fälten är en intern typ (`StatusBar::Cell`) som
// raden hittar bland barnen, som flikkontrollen hittar sina sidor — inga
// råa pekare, så en `unadopt` av ett fält kan inte lämna något dinglande.
// `setPanels` ändrar de befintliga fälten PÅ PLATS, tar bort överskottet och
// lägger till det som saknas, så ordningen mot appens barn står kvar. I en
// handskriven `.glazeui` står satserna EFTER trädbygget när raden också bär
// appens barn (`addPage`s och `dockPanel`s regel — previewn bygger trädet
// före satserna).
//
// FÄLTEN FÖLJER RADENS `enabled`: `enabled` kaskadar inte i ramverket, men
// fälten är INREDNING som appen inte når som widgetar (talfältets regel,
// `enabledChanged`) — en inaktiverad rad dämpar sina fält, också ett som
// läggs till efteråt. Appens EGNA barn i raden släcker appen själv.
//
// SKRIVNA BEGRÄNSNINGAR: texten står till vänster, fälten har ingen egen
// tooltip, ikon eller klick.
//
// MÖNSTER: inbyggd widget — ritningen är Bars (src/bar.cpp), fälten ritar
// sig själva (status_bar.cpp).

namespace glaze {

// ETT FÄLT — ett aggregat i den ordning fälten oftast behövs, så samma rad
// är både handkod och `.glazeui`:
//     statusBar.setPanels({{"Klar", 0, true}, {"Rad 1, kol 1", 110}});
struct StatusPanel {
    TrText text;              // literal eller glaze::tr(id)
    float  width = 0.0f;      // > 0 fast (logiska px), <= 0 AUTO
    bool   fill  = false;     // delar det lediga utrymmet
};

class StatusBar : public Bar {
public:
    StatusBar() { dock = Dock::Bottom; }

    // ---------------------------------------- FÄLTEN (se headern)
    // Programmatiskt — statusraden har inga events.
    void addPanel(TrText text = {}, float width = 0.0f, bool fill = false);
    void setPanel(int index, TrText text, float width = 0.0f,
                  bool fill = false);
    void setPanels(std::vector<StatusPanel> panels);
    void setPanelText(int index, TrText text);

    int                panelCount() const;
    StatusPanel        panel(int index) const;       // tom utanför
    const std::string& panelText(int index) const;   // upplöst; tom utanför

protected:
    // AXELREGELN med statusradens eget default-mått: satt höjd vinner,
    // osatt höjd blir `statusBarH` (Bar skulle annars ge `barHeight`).
    // Ingen auto-BREDD, precis som Bar — en statusrad är alltid dockad.
    gfx::Sizef contentSize(const Ctx& c) const override;

    // Fälten följer radens `enabled` (headern).
    void enabledChanged() override;

private:
    class Cell;   // ett fält — status_bar.cpp

    Cell* cell(int index) const;   // fältet bland barnen, nullptr utanför
    void  addCell(StatusPanel p);  // ett nytt fält sist, i radens `enabled`
};

} // namespace glaze
