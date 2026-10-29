#pragma once
#include <string>
#include <vector>

#include "glaze/app_window.h"
#include "glaze/language.h"

// FILVÄLJARDIALOGEN (wxFileDialog + wxDirDialog,
// WinForms OpenFileDialog/SaveFileDialog/FolderBrowserDialog). VÅR egen och
// aldrig OS:ets: en systemdialog mitt i en Glaze-app ser olika ut på tre
// plattformar, följer inte temat och ignorerar användarskalan — den hade
// spräckt illusionen precis som en OS-namnlist.
//
//     glaze::FileDialog dlg(glaze::FileDialogMode::Open);
//     dlg.addFilter("Bilder", "*.png;*.jpg");
//     if (dlg.run(*this) == glaze::DialogResult::OK)
//         öppna(dlg.path());
//
// EN KLASS MED EGENSKAPER + `run(ägare)`, och inte en fri funktion som
// `messageBox` — skälet är skrivet för att det skiljer sig från grannen:
//   * Dialogen har MÅNGA in- och FLERA utvärden (titel, startmapp, förvalt
//     namn, filtren, flerval, överskrivningsfrågan in; sökvägen eller
//     sökvägarna och det valda filtret ut; dolda filer både in och ut). En
//     fri funktion hade krävt en options-struct och en resultat-struct —
//     alltså klassen, kluven i två.
//     `messageBox` har ETT utvärde och en handfull enum-rattar.
//   * Dialogformen är densamma i andra toolkits (`wxFileDialog dlg(…);
//     if (dlg.ShowModal() == wxID_OK) dlg.GetPath()`), precis som WinForms
//     tre dialoger — formen mappar 1:1.
//   * Objektet kan BEHÅLLAS: efter OK står `folder()` där användaren slutade,
//     så nästa `run` öppnar där (en medlem i fönsterklassen, inte en lokal).
//
// TRE LÄGEN ÖVER EN IMPLEMENTATION: `FileDialogMode` är lägesvalet, i
// konstruktorn eller med `setMode`. WinForms har tre KLASSER eftersom de är
// tre implementationer; vår är EN, och filväljarfältet bär läget
// som ett setter-fält och lämnar det rakt hit.
//
// DIALOGEN ÄR INGEN WIDGET (meddelanderutans prejudikat): den registreras
// inte, adopteras inte och finns aldrig i en `.glazeui` — hård regel 8
// gäller därför inte den, men väl filväljarFÄLTET som öppnar den. Fönstret
// bor i `src/file_dialog.cpp` (och den interna `src/file_dialog_window.h`,
// för grindens skull).
//
// INNEHÅLLET: adressraden med en uppåtknapp och kryssrutan för dolda filer
// överst, de kända platserna (`gfx::knownPlaces`) till vänster och mappens
// innehåll i en listvy till höger med en delare emellan (namn · storlek ·
// ändrad · typ, sorterbart på varje kolumn och alltid med mapparna först),
// och längst ner namnfältet, filtypslistan och OK/Avbryt. Dubbelklick eller
// Enter på en mapp öppnar den, Backspace (utanför textfälten) och Alt+Upp
// går uppåt, F5 läser om mappen, Escape avbryter. Fel — ingen behörighet,
// en mapp som tagits bort, en fil som inte finns — står som en mening i
// dialogens meddelanderad och kraschar aldrig.

namespace glaze {

// LÄGET — vad dialogen väljer. Ett ENUM och inte tre klasser (se ovan).
enum class FileDialogMode {
    Open,     // en befintlig fil (flera med `setMultiSelect(true)`)
    Save,     // ett namn att spara under — en befintlig fil ger frågan
              // om överskrivning (`setOverwritePrompt`)
    Folder,   // en mapp — listan visar bara mappar, och filtren gäller inte
};

// ETT FILTER: namnet och mönstren. Visas i filtypslistan som
// "Bilder (*.png;*.jpg)". Mönstren skiljs med semikolon, `*` och `?` är
// jokertecken, och bokstäverna jämförs utan skiftläge ("*.png" träffar
// "BILD.PNG") på alla plattformar. "*" och "*.*" släpper igenom allt.
// Namnet är en `TrText`, så `glaze::tr(id)` fungerar som i en knapp.
struct FileFilter {
    TrText      name;
    std::string patterns;
};

class FileDialog {
public:
    explicit FileDialog(FileDialogMode mode = FileDialogMode::Open);

    void           setMode(FileDialogMode m) { mode_ = m; }
    FileDialogMode mode() const { return mode_; }

    // TITELN. Tom = lägets egen ("Öppna", "Spara som", "Välj mapp"), i
    // ramverkets språk.
    void               setTitle(TrText t) { title_ = std::move(t); }
    const std::string& title() const { return title_.str(); }
    const TrText&      titleToken() const { return title_; }

    // MAPPEN dialogen öppnar i (UTF-8, absolut). Tom = användarens
    // dokumentmapp, annars hemmet. Finns mappen inte längre öppnar dialogen
    // i dess närmaste förälder som finns — en sparad mapp som tagits bort
    // ska inte ge ett fel vid nästa öppning. EFTER OK står här mappen
    // användaren slutade i, så ett behållet objekt öppnar där nästa gång.
    void               setFolder(std::string utf8) { folder_ = std::move(utf8); }
    const std::string& folder() const { return folder_; }

    // NAMNET som står i namnfältet när dialogen öppnas — sparandets förslag
    // ("Rapport.pdf"). Efter OK: den valda filens namn (det första vid
    // flerval); i mappläget den valda mappens.
    void               setFileName(std::string utf8) { fileName_ = std::move(utf8); }
    const std::string& fileName() const { return fileName_; }

    // ------------------------------------------ FILTREN (kollektionsvokabulären)
    // `addX` · `setX(index, …)` · `setXs(lista)`, som varje lista i ramverket
    // — dialogen är ingen widget, men en andra form att lära sig hade varit
    // värre än ingen. Utan filter gäller "Alla filer (*.*)". Index utanför
    // ⇒ `gfx::log` + no-op. Mappläget visar inga filter.
    void addFilter(TrText name, std::string patterns);
    void setFilter(int index, TrText name, std::string patterns);
    void setFilters(std::vector<FileFilter> filters);
    const std::vector<FileFilter>& filters() const { return filters_; }

    // Det VALDA filtret — när dialogen öppnas, och efter OK det användaren
    // valde (WinForms `FilterIndex`, fast nollbaserat som allt annat här).
    // Klampas när dialogen öppnas.
    void setFilterIndex(int i) { filterIndex_ = i; }
    int  filterIndex() const { return filterIndex_; }

    // FLERVAL — bara öppningsläget (Ctrl-/Shift-klick i listan, eller
    // `"a.txt" "b.txt"` i namnfältet). Svaret står i `paths()`.
    void setMultiSelect(bool on) { multiSelect_ = on; }
    bool isMultiSelect() const { return multiSelect_; }

    // FRÅGAN OM ÖVERSKRIVNING — bara sparläget, PÅ som standard (WinForms
    // `OverwritePrompt`): ett namn som redan finns ger en meddelanderuta
    // (Ja/Nej) innan dialogen stänger. Av = appen tar själv ansvaret.
    void setOverwritePrompt(bool on) { overwritePrompt_ = on; }
    bool overwritePrompt() const { return overwritePrompt_; }

    // DOLDA FILER — AV som standard: namn som
    // börjar med punkt och det OS:et döljer (Windows HIDDEN-attribut, macOS
    // UF_HIDDEN) syns inte i listan, i något av lägena. Dialogen har en
    // kryssruta för samma sak, och valet gäller direkt i listan. Användarens
    // val skrivs TILLBAKA hit när rutan kryssas — också om dialogen sedan
    // avbryts: det är en VYINSTÄLLNING och inte en del av svaret (GTK sparar
    // den oavsett svar, Utforskaren globalt), så ett behållet objekt öppnar
    // nästa gång som användaren lämnade det, och appen kan spara
    // `showHidden()` mellan körningarna. Mappen och namnet skrivs bara vid
    // OK — de ÄR svaret.
    void setShowHidden(bool on) { showHidden_ = on; }
    bool showHidden() const { return showHidden_; }

    // VISAR dialogen MODALT och svarar OK eller Cancel — `None` LÄCKER
    // ALDRIG UT (meddelanderutans regel): Escape, Avbryt, fönsterkrysset och
    // ägarens kaskadstängning är alla Cancel. Den ägarlösa varianten är för
    // det som händer innan ett fönster finns; har appen ett fönster ska det
    // skickas, så hålls dialogen över det.
    DialogResult run(AppWindow& owner);
    DialogResult run();

    // RESULTATET efter OK (UTF-8, absolut). `path()` är det enda — eller
    // det första vid flerval — och tomt när dialogen avbröts. `paths()` bär
    // alla. Varje `run` börjar med att tömma dem.
    const std::string&              path() const;
    const std::vector<std::string>& paths() const { return paths_; }

private:
    friend class FileDialogWindow;   // fönstret skriver resultatet

    FileDialogMode           mode_;
    TrText                   title_;
    std::string              folder_;
    std::string              fileName_;
    std::vector<FileFilter>  filters_;
    int                      filterIndex_     = 0;
    bool                     multiSelect_     = false;
    bool                     overwritePrompt_ = true;
    bool                     showHidden_      = false;
    std::vector<std::string> paths_;
};

} // namespace glaze
