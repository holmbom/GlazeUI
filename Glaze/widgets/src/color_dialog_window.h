#pragma once
#include <gfx/color.h>

#include "glaze/app_window.h"
#include "glaze/color_dialog.h"

// FÄRGDIALOGENS FÖNSTER — INTERN header, som
// `file_dialog_window.h`: inget här är publikt API. Det publika är
// `glaze::ColorDialog` (glaze/color_dialog.h), och `ColorDialog::run` är tre
// rader runt det här fönstret.
//
// Klassen står i en header och inte TU-lokalt för grindens skull:
// tests/dialog_gate.cpp bygger fönstret SJÄLV i en subklass som når
// `AppWindow::handleEvent` (protected — produktytan, inte en testkrok) och
// visar det DOLT (`setOffscreen`), och hittar delarna med `find<T>(namn)` —
// ytans delar bär namn ("palette", "red" … "alpha", "hex"), knapparna "ok"
// och "cancel" och förhandsrutan "sample".
//
// Fönstret LÄSER inställningarna ur sin `ColorDialog` när det byggs och
// SKRIVER färgen dit vid OK (därför `friend` i color_dialog.h).

namespace glaze {

class Button;
class ColorPanel;
class ColorSample;   // förhandsrutan — color_dialog.cpp

class ColorDialogWindow : public AppWindow {
public:
    explicit ColorDialogWindow(ColorDialog& cfg);
    ~ColorDialogWindow() override;

    // INTROSPEKTION (läsning, inget tillstånd): färgen som är vald just nu
    // (den EFFEKTIVA — opak utan alfan) och om dialogen avslutades med OK.
    gfx::Color selected() const;
    bool       accepted() const { return dialogResult() == DialogResult::OK; }

protected:
    // Språkbytet: etiketterna är `TrText`, men måtten är frusna (de mättes
    // i `onLoad`) — meddelanderutans krok.
    void onLanguageChanged() override;

private:
    void layoutAndSize();
    void choose(gfx::Color c);   // ett val i ytan — förhandsrutan följer
    void accept();               // OK: committa det skrivna, skriv färgen

    ColorDialog& cfg_;
    ColorPanel*  panel_  = nullptr;
    ColorSample* sample_ = nullptr;
    Button*      ok_     = nullptr;
    Button*      cancel_ = nullptr;
    gfx::Color   chosen_{};   // RÅ — `selected()` läser den effektiv
};

} // namespace glaze
