#pragma once
#include <string>
#include <vector>

#include "glaze/file_dialog.h"
#include "glaze/widget.h"

// FilePicker — FILVÄLJARFÄLTET (wxFilePickerCtrl/
// wxDirPickerCtrl). Ett textfält för en SÖKVÄG och en "…"-knapp bredvid som
// öppnar filväljardialogen (glaze/file_dialog.h). MÖNSTER: widgeten
// ritar SIG SJÄLV — draw()/contentSize() bor i src/widgets/file_picker.cpp med
// switch (c.ui) (hård regel 3); kompositen ritar ingen egen yta, för fältet och
// knappen ritar sig själva.
//
// EN KOMPOSIT AV TVÅ RIKTIGA BARN — och det är ett beslut mot talfältets:
//   * FÄLTET är en vanlig `TextInput`: caret, markering, klippbord och UTF-8 är
//     dess egna, och man kan skriva eller klistra in en sökväg direkt.
//   * KNAPPEN är en vanlig `Button` och ett EGET TABBSTOPP (wxFilePickerCtrl
//     gör likadant). Talfältets stegare är data för att ett klick inte ska ta
//     caret ur fältet — här öppnar klicket en MODAL dialog, så fokus spelar
//     ingen roll, och knappen som tabbstopp är tangentbordsvägen till dialogen
//     (Tab, därefter Space/Enter) utan en påhittad genväg i fältet.
//   Kompositen är alltså INGET tabbstopp själv (`focusesChildren` är falsk),
//   placerar barnen själv (`placesChildren` — knappen i högerkanten så bred
//   som sin text, fältet tar resten minus `filePickerGap`), låter `disable()`
//   följa med in (`enabledChanged`) och nekar att barnen lossas
//   (`canReleaseChild`).
//
// VÄRDET ÄR `path` — sökvägen i UTF-8, och det fältet visar. Inte `text`:
// regel 6 säger att delade saker delar namn, och det här är ett VÄRDE av en
// annan sort än en etikett (talfältets `value`-prejudikat).
//
// `onChange` fyras när ANVÄNDAREN ändrat sökvägen — antingen genom att skriva
// i fältet (LIVE, varje ändring, som fältets egen `onChange`) eller genom att
// välja i dialogen (en gång, och bara när sökvägen faktiskt blev en annan).
// `onCommit` fyras när användaren är KLAR: i fältet vid Enter eller
// när fokus lämnar FÄLTET och sökvägen ändrats under redigeringen
// (fältets eget `onCommit` — också när fokus går till "…"-knappen), och i
// dialogen tillsammans med `onChange`. Programmatisk `setPath` fyrar aldrig
// något — kontraktet för varje widget.
//
// FOKUS OCH MARKERING: filväljarfältet får `onFocus`/`onFocusLost` när fokus
// går in i respektive lämnar det — en flytt mellan fältet och knappen fyrar
// ingenting på det (widget.h, fokus INOM) — och `selectAll`/`setSelection`
// når fältet.
//
// DIALOGEN öppnas i `mode()`s läge med `filters()`, i mappen där den
// nuvarande sökvägen ligger (finns den inte längre: närmaste förälder som
// gör det — dialogens egen regel) och med namnet förvalt. Det filter
// användaren valde minns fältet till nästa gång, och likaså dialogens
// kryssruta för DOLDA filer — den också när dialogen avbröts, för den är en
// vyinställning och inte en del av svaret (glaze/file_dialog.h). Ingen av
// de två är en egenskap på fältet: de är användarens val, inte appens.
// Fältets fönster är ägare, så dialogen hålls över det.
//
// HÅRD REGEL 8 — allt går i BÅDA vägarna: läget och sökvägen är SETTER-fält
// (`val.setMode(glaze::FileDialogMode::Save);`, `val.setPath("…");`) och
// filtren en LISTA med kollektionsvokabulären (`addFilter(namn, mönster)` ·
// `setFilter(index, namn, mönster)` · `setFilters({…})`), precis som varje
// annan lista i ramverket — previewn utför alla tre.

namespace glaze {

class Button;
class TextInput;

class FilePicker : public Widget {
public:
    FilePicker();
    ~FilePicker() override;

    // TILLSTÅNDET är PRIVAT och byts via SETTRAR — settern
    // är den som INVALIDERAR. Programmatisk sättning fyrar aldrig onChange.

    // Sökvägen (UTF-8) — det fältet visar och det `onChange` handlar om.
    void               setPath(std::string utf8);
    const std::string& path() const;

    // MARKERINGEN i fältet — vidare till fältet, `TextInput`s
    // kontrakt.
    void selectAll();
    void setSelection(int begin, int end);

    // Läget dialogen öppnas i. Knappens tooltip följer det ("Öppna",
    // "Spara som", "Välj mapp" — dialogens egen titel).
    void           setMode(FileDialogMode m);
    FileDialogMode mode() const { return mode_; }

    // DIALOGENS TITEL — "Välj CSV-fil" i stället
    // för lägets "Öppna". Ett TOKEN, så `glaze::tr(N)` följer språket; TOM
    // (förvalet) = lägets titel. Knappens tooltip följer dialogens titel, som
    // Ingen invalidering: ingenting i fältet ritar den.
    void               setDialogTitle(TrText t);
    const std::string& dialogTitle() const { return dialogTitle_.str(); }
    const TrText&      dialogTitleToken() const { return dialogTitle_; }

    // FILTREN — kollektionsvokabulären (`addX` · `setX(index, …)` · `setXs`),
    // samma form som dialogens. Utan filter gäller dialogens "Alla filer";
    // i mappläget används de inte. Index utanför ⇒ `gfx::log` + no-op.
    void addFilter(TrText name, std::string patterns);
    void setFilter(int index, TrText name, std::string patterns);
    void setFilters(std::vector<FileFilter> filters);
    const std::vector<FileFilter>& filters() const { return filters_; }

    // ÖPPNA DIALOGEN — det knappen gör, och den programmatiska vägen dit.
    // Modal: återvänder när dialogen stängts. Svarar true när användaren
    // valde OK (sökvägen är då satt och `onChange` fyrad om den ändrades).
    // Kräver att fältet sitter i ett öppet fönster — annars no-op + false.
    bool browse();

    // Användaren ändrade sökvägen — läs den ur sender (`path()`), regel 6.
    Handler<ChangeArgs> onChange;
    // …och är KLAR med den (se ovan) — Enter, fokus bort, dialogens val.
    Handler<ChangeArgs> onCommit;

    // Fältets Enter utan tangenten (widget.h): en skriven sökväg committas
    // NU — `AppWindow::commitEdits()` når den.
    void commitEdit() override;

    // Barnen är INREDNING — kompositen håller råa pekare till dem.
    bool canReleaseChild(const Widget& child) const override;

    // Kompositen placerar sina barn själv (Bar/Splitter-formen).
    bool       placesChildren() const override { return true; }
    gfx::Rectf placeChild(const Widget& child, const Ctx& c) const override;

    // setProp-egenskaper: path, mode (open/save/folder), dialogTitle; resten
    // tar Widget.
    bool setProp(const std::string& key, const std::string& value) override;

    // ÖVERSATT dialogtitel — regel 8:s tr-väg. Utan den blir
    // `setDialogTitle(glaze::tr(id))` en OPAK rad i previewn.
    bool setTextToken(const std::string& key, const TrText& value) override;

protected:
    gfx::Sizef contentSize(const Ctx& c) const override;
    void       draw(const Ctx& c) override;
    void       enabledChanged() override;

private:
    // Knappens tooltip — dialogens titel: den satta, annars lägets.
    void syncToolTip();

    TextInput*              field_  = nullptr;   // ägda barn (add<> i ctorn)
    Button*                 button_ = nullptr;
    FileDialogMode          mode_   = FileDialogMode::Open;
    TrText                  dialogTitle_;   // tom = lägets titel
    std::vector<FileFilter> filters_;
    int                     filterIndex_ = 0;   // det användaren valde senast
    bool                    showHidden_  = false;   // dialogens kryssruta, likaså
};

} // namespace glaze
