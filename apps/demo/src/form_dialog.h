#pragma once
#include <glaze/glaze.h>

// FORMULÄRET — `demo --window-lab` → "Formulär…": en dialog som
// följer sina RADER, går att dra upp och har en etikettkolumn över två
// gruppramar. Hela fönstret står i `.glazeui`, alltså designervägen för alla
// tre punkterna:
//   * FÖNSTRET FÖLJER INNEHÅLLET med rader UPPIFRÅN och MOT NEDERKANTEN —
//     `setSizeToContent(WidthAndHeight)`: gruppramarna och kryssrutorna flödar
//     uppifrån, OK och Avbryt står nere till höger (`anchors.bottom`), och
//     kontaktlistan är SPÄND mellan rubriken och knapparna med `height = 96`
//     som sitt minsta mått. `visa.bottom = 12` är LUFTEN ovanför knapparna när
//     listan är dold. Bredden följer också: de sträckta fälten ger sitt mätta
//     mått som minsta, så etikettkolumnen och fälten aldrig kläms.
//   * INNEHÅLLET SOM GOLV — `setSizeToContentMode(GrowOnly)`: dialogen öppnas
//     i begärans storlek (minst innehållets), kanterna går att dra utåt
//     (listan och fälten växer) men aldrig inåt förbi innehållet — varken
//     smalare eller lägre. "Lås storleken" byter till `GrowAndShrink`: låst,
//     i innehållets storlek, och den krymper och växer med listan.
//   * EN ETIKETTKOLUMN — etiketterna i båda ramarna bär
//     `setWidthGroup("form")`, så fälten (ankrade `anchorLeft = {lbl, 8}`)
//     står i linje i varje UI, stil och språk; finskans "Postinumero ja
//     -toimipaikka" breddar kolumnen i BÅDA ramarna.
// OK och Avbryt adopteras FÖRST — listan ankras mot `ok`, och ett syskonankare
// pekar bara bakåt — och bär `tabIndex` 100/101, så Tab-ordningen ändå slutar
// med dem. Kontakternas namn är DATA (en app hade läst dem ur en fil) och står
// som literaler.
class FormDialog : public glaze::AppWindow {
#pragma region GlazeBuilder
    // GENERERAD av GlazeBuilder — redigera i GlazeBuilder, inte för hand.
public:
    FormDialog();

private:
    glaze::Button ok;
    glaze::Button avbryt;
    glaze::GroupBox kontakt;
    glaze::Label lblNamn;
    glaze::TextInput namn;
    glaze::Label lblEpost;
    glaze::TextInput epost;
    glaze::Label lblTelefon;
    glaze::TextInput telefon;
    glaze::GroupBox adress;
    glaze::Label lblGata;
    glaze::TextInput gata;
    glaze::Label lblPostort;
    glaze::TextInput postort;
    glaze::Checkbox visa;
    glaze::Checkbox las;
    glaze::Label lblKontakter;
    glaze::ListBox kontakter;
    void initializeComponent();
#pragma endregion

    // Allt härunder är användarens — GlazeBuilder rör det aldrig.
    void onLoad(glaze::Widget* sender, glaze::EventArgs& e);
    void visaAndrad(glaze::Widget* sender, glaze::ChangeArgs& e);
    void lasAndrad(glaze::Widget* sender, glaze::ChangeArgs& e);
};
