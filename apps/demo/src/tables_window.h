#pragma once
#include <glaze/glaze.h>

#include <random>
#include <string>
#include <vector>

// TablesWindow — PROVBÄNKEN för tabellerna, `demo --tables`, HANDKODAT:
// `DataGrid`, `ListView` och `PropertyGrid` i samma fönster, med ett reglage
// för varje egenskap och en HÄNDELSELOGG som visar varje event med sina args.
//
// Till vänster står reglagen, i en rullande kolumn med en gruppram per widget
// och sist en fristående `Dropdown` med ikoner;
// till höger de tre widgetarna staplade i en rullande yta, så att höjden ur
// raderna syns; nederst loggen med tangentbordets regler att prova. De
// knappar som verkar på en pågående redigering (`commitEdits`, läsvägarna,
// `beginEdit`, `cancelEdit`) är VERKTYGSKNAPPAR: de tar inte fokus, så
// redigeringen står kvar tills knappen själv når den. Reglagen som verkar på
// "den aktuella raden" läser tabellens `currentRow()`, och de som tar ett
// index läser radfältet bredvid.
//
// Texterna är literaler, som i arbetsbänken för Slate: fönstret ingår inte i
// demons översatta yta.
class TablesWindow : public glaze::AppWindow {
public:
    TablesWindow();

private:
    // Bygget, ett block per del av fönstret.
    void byggLogg();
    void byggTabeller(glaze::Panel& yta);
    void byggReglage(glaze::Panel& yta);
    void reglageUtseende(glaze::Panel& yta);
    void reglageDataGrid(glaze::Panel& yta);
    void reglageListView(glaze::Panel& yta);
    void reglagePropertyGrid(glaze::Panel& yta);
    void reglageDropdown(glaze::Panel& yta);

    // Innehållet. `fyllDataGrid` lägger lagerlistan och `tillampaCeller`
    // cellernas egna inställningar — de hör till raderna och läggs om efter
    // varje `setRows` (radernas namn avgör vilken cell som får vad).
    // `taBortRad` tar bort raden med `removeRow`, som de följer med.
    void fyllDataGrid();
    void tillampaCeller();
    void fyllMassor();
    void taBortRad(int row);
    void fyllListView();
    void fyllPropertyGrid();
    void kopplaEvents();

    // Datagridens aktuella cell, eller false med en loggrad när ingen finns.
    bool aktuellCell(int& row, int& column);
    void lasCellen();
    void uppdateraSlumpcell();

    // Egenskapslistans rad för reglagen: raden med öppen editor, annars
    // radfältets; −1 med en loggrad när ingen sådan rad finns. `pgSkrivskydd`
    // och `pgNyttVarde` byter raden med `setRow` och ett ändrat aggregat.
    int  pgMalrad();
    void pgSkrivskydd(bool on);
    void pgNyttVarde();

    // EN rad i loggen: källan, eventet och args.
    void logga(const std::string& kalla, const std::string& handelse,
               const std::string& args);

    glaze::DataGrid*     grid_  = nullptr;
    glaze::ListView*     lista_ = nullptr;
    glaze::PropertyGrid* egen_  = nullptr;
    glaze::ListView*     logg_  = nullptr;

    // Kolumnlägets reglage, som följer den valda kolumnen.
    glaze::Dropdown* dgKolumn_      = nullptr;
    glaze::Dropdown* dgLage_        = nullptr;
    glaze::SpinBox*  dgBredd_       = nullptr;
    glaze::Checkbox* dgFastHojd_    = nullptr;
    glaze::Dropdown* lvKolumn_      = nullptr;
    glaze::Dropdown* lvLage_        = nullptr;
    glaze::SpinBox*  lvBredd_       = nullptr;

    // Radfälten och kolumnfältet som reglagen för den aktuella raden, valet
    // och synfältet läser.
    glaze::SpinBox*  dgRadTal_      = nullptr;
    glaze::SpinBox*  dgKolTal_      = nullptr;
    glaze::SpinBox*  lvRadTal_      = nullptr;
    glaze::SpinBox*  pgRadTal_      = nullptr;

    // Uppdateringen 60 gånger i sekunden och slumpen den väljer cell med.
    glaze::Timer takt_;
    std::mt19937 slump_;

    int loggNr_ = 0;
    int nyRad_  = 0;   // löpnumret på tillagda rader och kategorier
};
