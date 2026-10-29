#pragma once
#include <vector>

#include "glaze/widgets/table.h"

// ListView — LISTVYN (wxListCtrl i report-läge,
// WinForms ListView i Details-vyn). En rullande tabell med ett kolumnhuvud:
// rader av celler, valfri ikon per rad, sortering på kolumn och ett VAL som
// användaren gör med mus och tangentbord. Det är filväljarens lista (post
// 18) — namn, storlek, datum och typ, sorterbara, med ikon.
//
// MÖNSTER: widgeten ritar SIG SJÄLV — draw() bor i src/widgets/
// list_view.cpp med `switch (c.ui)` (hård regel 3). Kolumnerna, raderna,
// sorteringen och huvudet är `Table`s (table.h) och delas med DataGrid; det
// som är listvyns är VALET, AKTIVERINGEN och hur raderna ritas runt det.
//
// VALET. Enkelval som standard — en rad i taget, som ListBox. Med
// `setMultiSelect(true)` blir det flerval i Utforskarens form:
//   klick            välj BARA raden
//   Ctrl+klick       vänd raden (Cmd+klick på macOS, där Ctrl+klick är
//                    högerklick — båda räknas på alla plattformar)
//   Shift+klick      välj spannet från ANKARET till raden
//   pilar/Home/End/  flytta den aktuella raden och välj bara den;
//   PageUp/PageDown  med Shift: spannet från ankaret; med Ctrl: flytta
//                    UTAN att välja (Ctrl+Space vänder då raden)
//   Ctrl+A           välj alla
// Spannet räknas i VISNINGSORDNINGEN — det är raderna man ser mellan två
// klick, oavsett hur appen lade in dem. Musens modifierare läses ur
// `AppWindow::modifierKeys()` (fasaden bär dem på musens events).
//
// SOLO VID SLÄPPET (trädets regel, Utforskarens och
// Finders): ett tryck utan modifierare på en rad som REDAN är vald i ett
// flerval gör den bara aktuell, och väljer den ensam först när knappen
// SLÄPPS utan drag. Annars hade det aldrig gått att dra ett helt val — med
// `setAllowDrag(true)` drar appen hela `selectedRows()` därifrån. Ett tryck
// som blir ett drag får ingen `mouseUp` (widget.h), och det väntande valet
// släpps vid dragets start, vid nästa tryck och när raderna byts.
//
// RADENS TEXTFÄRG är `Table`s (`setRowColor`, table.h): den syns på
// omarkerade rader och under hover; markeringens färg vinner på en vald rad,
// och ett inaktivt läge dimmar allt till `textDim`.
//
// KRYSSEN är ListBoxens och trädets, ord för ord:
// `setShowChecks(true)` visar en ruta per rad — FÖRST i första kolumnen, före
// ikonen (WinForms `ListView.CheckBoxes`), i ListBoxens mått — och
// `setChecked(rad, på)`/`isChecked(rad)` är tillståndet, per RAD (appens
// index), så krysset följer raden genom sorteringen. Kryssen är OBEROENDE av
// valet:
//   klick i rutans zon   (rutan + `checkGap`, radens hela höjd) vänder krysset
//                        — valet, den aktuella raden och ett väntande soloval
//                        rörs inte; ett dubbelklick där är två vändningar och
//                        ingen aktivering, och ett drag börjar aldrig där
//   Space                vänder den AKTUELLA radens kryss när rutorna syns —
//                        också i enkelval, och före valet
//   Ctrl+Space           vänder valet (bara i flerval)
// `setRow` lämnar krysset (vyns tillstånd, inte radens data), `addRow` ger en
// okryssad rad och `setRows` NOLLAR alla, som valet: massformen bär inga kryss
// (som ListBoxens `setItems`). Rutan och bocken följer TEMAT och
// aldrig radens app-färg. Vilken rad som vändes står i `changedRow()` (−1
// efter `setRows` och efter ett rent val), och `onCheck` följs av `onCommit`.
//
// Värden läses ur SENDER (regel 6): `selectedIndex()`, `selectedRows()`,
// `isSelected(rad)`, `currentRow()`, `isChecked(rad)`, `checkedRows()`,
// `changedRow()` — alltid appens radindex, aldrig en skärmplats. `onChange`
// fyras EXAKT EN gång per ANVÄNDARhandling som ändrade valet, `onCheck` när
// ANVÄNDAREN vänder ett kryss; `onActivate` när användaren dubbelklickar på en
// rad eller trycker Enter. Programmatiska settrar fyrar ALDRIG något.
//
// SNABBMENYN är trädets och ListBoxens:
// `contextRow()` är raden (appens index) under det SENASTE högertrycket — −1
// på tom yta, i huvudet, i en remsa, när listan är inaktiverad och efter
// `setRows` — och högertrycket VÄLJER som Utforskaren: en rad utanför valet
// väljs ensam genom användarvägen, en rad i valet blir bara aktuell. Fokus
// flyttas inte. Träffvägen för egna gester är `Table::rowAt(localX, localY)`.
//
// DRAGET börjar aldrig i huvudet, i en remsa eller i rutans zon
// (`canDragFrom`, widget.h): en avdelare som dras är en kolumnbredd och inget
// raddrag, också när appens `onDragStart` fyller lasten. Listvyn har inget
// eget släpp — lasten och släppet är appens (ingen inbyggd omordning: i en
// sorterad vy betyder "mellan rad 3 och 4" ingenting i appens index).
//
// SKRIVNA BEGRÄNSNINGAR utöver tabellens (table.h): ingen typeahead
// (ListBoxens skäl), ingen kryssruta i huvudet (markera alla), ingen
// gummibandsmarkering med musen, och en första kolumn smalare än luften och
// rutan låter rutan sticka ut i nästa kolumn (som ikonen gör).

namespace glaze {

class ListView : public Table {
public:
    ListView();

    // ------------------------------------------------------------- VALET
    // Programmatisk sättning: ändrar, invaliderar, avfyrar INGET — och
    // scrollar INTE (`ensureVisible` finns).

    // FLERVAL av/på. Av (standard) ⇒ högst EN vald rad; slås det av med
    // flera valda behålls bara den aktuella (eller den första).
    void setMultiSelect(bool on);
    bool isMultiSelect() const { return multiSelect_; }

    // DEN valda raden. Med flerval: den AKTUELLA raden om den är vald,
    // annars den första valda i visningsordningen — −1 när inget är valt.
    // `setSelectedIndex` väljer BARA raden (eller inget, med −1) och gör den
    // aktuell.
    void setSelectedIndex(int row);
    int  selectedIndex() const;

    // Raden för sig. Utan flerval väljer `setSelected(rad, true)` bort de
    // andra. Rad utanför ⇒ logg + no-op.
    void setSelected(int row, bool on);
    bool isSelected(int row) const;
    // Alla valda, i VISNINGSORDNINGEN.
    std::vector<int> selectedRows() const;

    void selectAll();        // bara med flerval
    void clearSelection();

    // ---------------------------------------------- KRYSSEN
    // Rutorna syns eller inte; tillståndet finns oavsett. `setChecked` är
    // programmatisk (avfyrar aldrig `onCheck`, sätter inte `changedRow`), och
    // en rad utanför ⇒ logg + no-op; `isChecked` är false utanför. Båda
    // settrarna invaliderar bara vid ändring.
    void setShowChecks(bool on);
    bool showsChecks() const { return showChecks_; }
    void setChecked(int row, bool on);
    bool isChecked(int row) const;
    // Alla kryssade, i VISNINGSORDNINGEN (`selectedRows()`s parhäst).
    std::vector<int> checkedRows() const;
    // Vilken rad som vändes — läs i `onCheck` (och dess `onCommit`); −1 efter
    // ett rent val och efter `setRows` (ListBoxens namn och kontrakt). En
    // `onCheck` som fyller om listan nollar den, och den sätts IGEN före
    // `onCommit` — då med den kryssade radens index, som kan peka utanför
    // eller på en annan rad i den nya listan.
    int changedRow() const { return changedRow_; }

    // Raden under det SENASTE högertrycket (filhuvudet) — läs den i
    // `onContextMenu` och i menyns handlers; −1 när listan är inaktiverad.
    int contextRow() const { return isEnabled() ? contextRow_ : -1; }

    // Användaren ändrade valet (klick, pilar, Ctrl/Shift, Ctrl+A).
    Handler<ChangeArgs>   onChange;
    // ANVÄNDAREN vände ett kryss (rutans zon, Space) — raden i `changedRow()`,
    // och `onCommit` direkt efter.
    Handler<ChangeArgs>   onCheck;
    // ANVÄNDAREN ÄR KLAR — direkt efter `onChange`, för samma val
    // (ListBoxens och trädets form: ett val ur en lista är en hel handling),
    // och direkt efter `onCheck` för samma kryss (skilj dem åt med
    // `changedRow()`, som är −1 för ett val).
    Handler<ChangeArgs>   onCommit;
    // Användaren AKTIVERADE den aktuella raden — dubbelklick eller Enter.
    // Utan handler tar listan inte Enter: tangenten
    // bubblar, och i en dialog trycker den standardknappen.
    Handler<ActivateArgs> onActivate;

    // setProp-egenskaper: selectedIndex, multiSelect, showChecks; resten tar
    // Table.
    bool setProp(const std::string& key, const std::string& value) override;

protected:
    void draw(const Ctx& c) override;
    bool keyDown(gfx::Key k, std::uint32_t mods) override;
    void mouseDown(const Ctx& c, float mx, float my) override;
    // Huvudets släpp FÖRST (`Table::mouseUp` — sorteringen, avdelaren), sedan
    // det väntande solovalet (filhuvudet).
    bool mouseUp(const Ctx& c, float mx, float my) override;
    void mouseDoubleClick(const Ctx& c, float mx, float my) override;
    void mouseMove(const Ctx& c, float mx, float my) override;
    // Inget drag från huvudet, remsorna eller rutans zon (filhuvudet).
    bool canDragFrom(const Ctx& c, float x, float y) const override;
    // Ett drag får ingen `mouseUp` — det väntande solovalet släpps här.
    void dragStart(const Ctx& c, DragStartArgs& e) override;
    // Högertrycket (filhuvudet): raden blir `contextRow()` och väljs som i
    // Utforskaren. Svarar ALLTID false, så snabbmenyn fyras.
    bool rightDown(const Ctx& c, float mx, float my) override;

    // Rutans zon först i första kolumnen (`Table::leadZone`): rutan och
    // `checkGap` när kryssen syns, annars ingen.
    float leadW(const Ctx& c) const override;

    // Märkena i radrubrikens remsa: kryssrutan och Minimalistics
    // valstreck, i rubrikens färger — remsan tonas aldrig av valet.
    void drawRowHeaderMarks(const Ctx& c, int position, int row,
                            gfx::Color fg) const override;

    void rowsReplaced() override;
    void rowAppended() override;
    void rowReplaced(int row) override;

    // ANVÄNDARVÄGARNA — de ENDA som fyrar event (ListBoxens
    // `selectFromUser`-prejudikat; protected så en subklass, och en grind,
    // når dem). `selectFromUser` tar platsen raden STÅR på och
    // modifierarna: Shift = spann från ankaret, Ctrl = vänd (klick) eller
    // flytta utan att välja (tangent), inget = välj bara den. Svaret är om
    // valet ändrades och eventen fyrades (`mouseUp`s svar).
    bool selectFromUser(int position, std::uint32_t mods, bool fromKey);
    void activateFromUser();
    // KRYSSET (ListBoxens `toggleCheckFromUser`): vänd raden (appens index),
    // och `onCheck` följt av `onCommit` med `changedRow()` satt — igen före
    // `onCommit`. Valet rörs inte. false = en handler rev eller lossade
    // listan, och då rör anroparen aldrig `this` igen.
    bool toggleCheckFromUser(int row);

private:
    // Valet och kryssen per RAD (appens index) — `char` och inte `bool`, så
    // vektorerna är riktiga vektorer. Båda följer raden genom sorteringen.
    std::vector<char> selected_;
    std::vector<char> checked_;
    int  anchor_      = -1;   // spannets fasta ände (appens radindex)
    int  hover_       = -1;   // hovrad PLATS — svag markering
    int  pendingSolo_ = -1;   // solo vid släppet (appens radindex, −1 = inget)
    int  changedRow_  = -1;   // kryssets rad i onCheck/onCommit (−1 = inget)
    int  contextRow_  = -1;   // senaste högertrycket (appens radindex)
    bool multiSelect_ = false;
    bool showChecks_  = false;

    // RUTAN på en plats — v-centrerad i zonen (`Table::leadZone`), i
    // ListBoxens mått. Träffen frågar ZONEN (rutan + `checkGap`, radens hela
    // höjd) och bara inom radernas yta; ritningen sker i listvyns radloop med
    // temats pennor, aldrig i radens app-färg (`drawCells` får den).
    gfx::Rectf checkRect(const Ctx& c, int position) const;
    bool       onCheckZone(const Ctx& c, int position, float mx,
                           float my) const;
    void       drawCheck(const Ctx& c, int position, int row, gfx::Color bock,
                         gfx::Color kant) const;

    void setHover(int position);
    // Sätter valet till exakt `rader` (appens index). Svarar true när
    // något faktiskt ändrades — användarvägen fyrar bara då.
    bool assignSelection(const std::vector<int>& rader);
    std::vector<int> allRows() const;   // 0 … rowCount() − 1
    void fireChange();                  // onChange, därefter onCommit
};

} // namespace glaze
