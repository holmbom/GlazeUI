#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include <gfx/platform.h>   // gfx::Place

#include "glaze/app_window.h"
#include "glaze/file_dialog.h"

#include "file_dialog_model.h"

// FILVÄLJARDIALOGENS FÖNSTER — INTERN header, som
// `tooltip.h`: inget här är publikt API. Det publika är `glaze::FileDialog`
// (glaze/file_dialog.h), och `FileDialog::run` är tre rader runt det här
// fönstret.
//
// Klassen står i en header och inte TU-lokalt för grindens skull:
// tests/filedialog_gate.cpp bygger fönstret SJÄLV i en subklass som når
// `AppWindow::handleEvent` (protected — produktytan, inte en testkrok) och
// visar det DOLT (`setOffscreen`), och hittar delarna med `find<T>(namn)`.
// Delarna bär NAMN av det skälet ("path", "files", "name" …) — samma
// identitetsmekanism som varje fönster har, inget eget testgränssnitt.
//
// Fönstret LÄSER inställningarna ur sin `FileDialog` när det byggs och
// SKRIVER resultatet dit vid OK (därför `friend` i file_dialog.h).

namespace glaze {

class Button;
class Checkbox;
class Dropdown;
class Label;
class Splitter;
class TextInput;
class FileList;     // fillistan — en ListView som ritar sina ikoner
class PlacesList;   // platslistan — en ListBox med ikoner

class FileDialogWindow : public AppWindow {
public:
    explicit FileDialogWindow(FileDialog& cfg);
    ~FileDialogWindow() override;

    // INTROSPEKTION (läsning, inget tillstånd): mappen som visas och om
    // dialogen avslutades med OK — fönstrets eget svar (`dialogResult()`).
    const std::filesystem::path& currentFolder() const { return cur_; }
    bool accepted() const { return dialogResult() == DialogResult::OK; }

protected:
    // Dialogens TANGENTER, före den fokuserade widgeten: Enter i adressfältet
    // är "gå dit", Backspace utanför textfälten och Alt+Upp går uppåt, F5
    // läser om mappen. Enter (OK) och Escape (Avbryt) är fönstrets standard-
    // och avbrytknapp och kommer SIST i routingen — en
    // öppen filtypslista tar alltså sitt eget Escape först.
    bool event(const gfx::Event& e) override;

    // Det som INTE är ett `TrText` följer med vid ett språkbyte: platsernas
    // och filtrens namn (listornas innehåll är strängar) och fillistans
    // typ- och storlekstext (typsuffixet och decimaltecknet är språkets).
    void onLanguageChanged() override;

private:
    void navigate(const std::filesystem::path& p,
                  const std::string&           selectName = std::string());
    void fillList(const std::string& selectName);
    void refresh();
    void fillPlaces();
    void fillFilters();
    void syncPlaces();
    void goUp();
    void activated();
    void selectionChanged();
    void placeChosen();
    void filterChanged();
    void hiddenToggled();
    void pathEntered();
    void accept();
    void finish(const std::vector<std::filesystem::path>& paths);
    void showError(std::uint32_t textId);
    std::string typeText(const filedlg::Entry& e) const;

    FileDialog&                    cfg_;
    const FileDialogMode           mode_;
    std::filesystem::path          cur_;
    filedlg::Listing               listing_;    // raderna i appens ordning
    std::vector<gfx::Place>        places_;
    std::vector<FileFilter>        filters_;    // de visade (aldrig tom i
                                                // fillägena)
    std::vector<std::string>       patterns_;   // det valda filtrets mönster

    Button*     up_        = nullptr;
    TextInput*  path_      = nullptr;
    Checkbox*   hidden_    = nullptr;   // "Visa dolda filer" — dess kryss
                                        // ÄR vyns tillstånd
    Splitter*   split_     = nullptr;
    PlacesList* placeList_ = nullptr;
    FileList*   files_     = nullptr;
    Label*      nameLabel_ = nullptr;
    TextInput*  name_      = nullptr;
    Dropdown*   filter_    = nullptr;
    Label*      message_   = nullptr;
    Button*     ok_        = nullptr;
    Button*     cancel_    = nullptr;
};

} // namespace glaze
