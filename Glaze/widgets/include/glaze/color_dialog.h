#pragma once
#include <string>

#include <gfx/color.h>

#include "glaze/app_window.h"
#include "glaze/language.h"

// FÄRGDIALOGEN (WinForms `ColorDialog`, wxColourDialog). VÅR egen och aldrig
// OS:ets: en systemdialog
// mitt i en Glaze-app ser olika ut på tre plattformar och följer inte temat.
//
//     glaze::ColorDialog dlg;
//     dlg.setColor(linje.color());
//     if (dlg.run(*this) == glaze::DialogResult::OK)
//         linje.setColor(dlg.color());
//
// EN KLASS MED EGENSKAPER + `run(ägare)`, filväljarens form och inte
// meddelanderutans fria funktion: färgen är både IN och UT, och ett BEHÅLLET
// objekt (en medlem i fönsterklassen) öppnar med förra valet. WinForms
// `ColorDialog` har samma form.
//
// INNEHÅLLET är SAMMA yta som färgväljarens popup (`ColorPicker`) — paletten,
// R/G/B-reglagen (+ A med alfan) och hexfältet, den delade färgytan i
// ramverket — plus en förhandsruta och OK/Avbryt. I en dialog STÄNGER ett
// val ingenting: paletten väljer (pilarna också — markören är valet), och
// OK eller Enter tar färgen, Avbryt, Escape eller krysset kastar den. Ett
// skrivet hexvärde committas av OK, också när fokus står kvar i fältet.
//
// DIALOGEN ÄR INGEN WIDGET (meddelanderutans och filväljarens prejudikat):
// den registreras inte, adopteras inte och finns aldrig i en `.glazeui` —
// färgväljarFÄLTET (`ColorPicker`) är regel 8:s form för en färg i ett
// formulär. Fönstret bor i `src/color_dialog.cpp` och den interna
// `src/color_dialog_window.h`, för grindens skull.

namespace glaze {

class ColorDialog {
public:
    ColorDialog() = default;

    // TITELN. Tom = ramverkets "Färg" (id 256), i det aktiva språket.
    void               setTitle(TrText t) { title_ = std::move(t); }
    const std::string& title() const { return title_.str(); }
    const TrText&      titleToken() const { return title_; }

    // FÄRGEN dialogen öppnar med, och efter OK den användaren valde. LAGRAD
    // med sin alfa och LÄST opak utan `allowAlpha` — färgväljarens regel.
    void       setColor(gfx::Color c) { color_ = c; }
    gfx::Color color() const;

    // ALFAN: A-reglaget och `#RRGGBBAA` i hexfältet (`ColorPicker`s).
    void setAllowAlpha(bool on) { allowAlpha_ = on; }
    bool allowAlpha() const { return allowAlpha_; }

    // VISAR dialogen MODALT och svarar OK eller Cancel — `None` LÄCKER
    // ALDRIG UT (meddelanderutans regel). Den ägarlösa varianten är för det
    // som händer innan ett fönster finns.
    DialogResult run(AppWindow& owner);
    DialogResult run();

private:
    friend class ColorDialogWindow;   // fönstret skriver färgen vid OK

    TrText     title_;
    gfx::Color color_{};   // svart, som färgväljaren — RÅ alfa
    bool       allowAlpha_ = false;
};

} // namespace glaze
