#include "about_dialog.h"

// 360×240: fönsterstorlekar är SKÄRM-enheter (fysiska px på Windows) —
// vid 125 % DPI blir 340×180 logiskt 272×144 och de botten-ankrade
// knapparna lägger sig ÖVER dropdownen.
AboutDialog::AboutDialog()
    : glaze::AppWindow(glaze::tr(demo::str::OmTitel), 360, 240)
{
    setModal(true);   // dialogvägen: visas med run(owner), input filtreras

    auto* rubrik = add<glaze::Label>(glaze::tr(demo::str::OmRubrik));
    rubrik->x = 16;
    rubrik->y = 16;

    auto* text = add<glaze::Label>(glaze::tr(demo::str::OmBrodtext));
    text->x    = 16;
    text->y    = 44;
    text->setRole(glaze::Label::Role::Dim);

    // Tema-väljaren — demonstrerar Dropdown (popup-fönsterburen lista).
    // Värdet läses ur sender (regel 6): dd->selectedIndex.
    auto* tema  = add<glaze::Label>(glaze::tr(demo::str::TemaLbl));
    tema->x     = 16;
    tema->y     = 78;
    auto* dd    = add<glaze::Dropdown>();
    // ITEMS är strängar — dialogen är
    // kortlivad och byggs om vid varje öppning, så den behöver ingen
    // omfyllning vid språkbyte som startfönstret har.
    dd->setItems({trs(demo::str::LageSystem), trs(demo::str::LageLjust),
                  trs(demo::str::LageMorkt)});
    dd->x       = 70;
    dd->y       = 72;
    dd->width   = 140;
    dd->setSelectedIndex(
        themeMode() == glaze::ThemeMode::System   ? 0
        : themeMode() == glaze::ThemeMode::Light  ? 1
                                                  : 2);
    dd->onChange = [this](glaze::Widget* sender, glaze::ChangeArgs&) {
        auto* d = static_cast<glaze::Dropdown*>(sender);
        setThemeMode(d->selectedIndex() == 0 ? glaze::ThemeMode::System
                     : d->selectedIndex() == 1 ? glaze::ThemeMode::Light
                                               : glaze::ThemeMode::Dark);
    };

    auto* ok     = add<glaze::Button>(glaze::tr(glaze::str::Ok));
    ok->width    = 100;
    ok->height   = 30;
    ok->setImg(glaze::img("toolbar", "ok"));   // handtag ur assets.bin
    ok->anchors  = {false, false, true, true};
    ok->right    = 12;
    ok->bottom   = 12;
    ok->setDialogResult(glaze::DialogResult::OK);

    auto* avbryt    = add<glaze::Button>(glaze::tr(glaze::str::Cancel));
    avbryt->width   = 100;
    avbryt->height  = 30;
    avbryt->setImg(glaze::img("toolbar", "cancel"));
    avbryt->anchors = {false, false, true, true};
    avbryt->right   = 122;
    avbryt->bottom  = 12;
    avbryt->setDialogResult(glaze::DialogResult::Cancel);

    // STANDARD- OCH AVBRYTKNAPPEN: Enter trycker OK och
    // Escape Avbryt, var fokus än står (utom på en knapp, som tar sitt eget
    // Enter — Tab till Avbryt flyttar accentytan dit). OK ritas som standard.
    setAcceptButton(*ok);
    setCancelButton(*avbryt);
}
