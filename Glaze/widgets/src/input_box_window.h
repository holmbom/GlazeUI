#pragma once
#include <string>

#include "glaze/app_window.h"
#include "glaze/input_box.h"   // InputBoxMode
#include "glaze/language.h"

// INMATNINGSRUTANS FÖNSTER — INTERN header, som
// `file_dialog_window.h`: inget här är publikt API. Det publika är
// `glaze::inputBox` (glaze/input_box.h), och funktionen är tre rader runt det
// här fönstret.
//
// Klassen står i en header och inte TU-lokalt för grindens skull:
// tests/dialog_gate.cpp bygger fönstret SJÄLV i en subklass som når
// `AppWindow::handleEvent` och visar det DOLT (`setOffscreen`), och hittar
// delarna med `find<T>(namn)` — "prompt", "field", "ok", "cancel". Fältet
// heter "field" i båda lägena: ett `TextInput` (SingleLine) eller ett
// `TextArea` (MultiLine).

namespace glaze {

class Button;
class Label;
class TextArea;
class TextInput;

class InputBoxWindow : public AppWindow {
public:
    InputBoxWindow(const TrText& prompt, const TrText& caption,
                   const std::string& initial,
                   InputBoxMode       mode = InputBoxMode::SingleLine);
    ~InputBoxWindow() override;

    // INTROSPEKTION (läsning, inget tillstånd): fältets text och om rutan
    // avslutades med OK — fönstrets eget svar (`dialogResult()`).
    const std::string& text() const;
    bool accepted() const { return dialogResult() == DialogResult::OK; }

protected:
    // Språkbytet: etiketterna är `TrText`, men måtten är frusna (de mättes
    // i `onLoad`) — meddelanderutans krok.
    void onLanguageChanged() override;

private:
    void layoutAndSize();

    Label*     prompt_ = nullptr;
    // Fältet i trädet (layout och fokus) och dess typade ände: EXAKT en av
    // `line_` och `area_` är satt, efter läget.
    Widget*    field_  = nullptr;
    TextInput* line_   = nullptr;
    TextArea*  area_   = nullptr;
    Button*    ok_     = nullptr;
    Button*    cancel_ = nullptr;
};

} // namespace glaze
