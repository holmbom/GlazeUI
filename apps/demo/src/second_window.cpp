#include "second_window.h"

#include "about_dialog.h"

void SecondWindow::omClicked(glaze::Widget*, glaze::ClickArgs&)
{
    // Modal dialog som i WinForms: lokalt objekt, run(owner) blockerar
    // tills stängning och returnerar resultatet (dialogen sätter själv
    // setModal(true) i sin konstruktor). Stängs DET HÄR fönstret medan
    // dialogen är öppen kaskad-stängs dialogen först och svaret blir
    // None — även då är objektets livstid rätt (lokal variabel).
    AboutDialog dlg;
    switch (dlg.run(*this)) {
    case glaze::DialogResult::OK:     tips.setText(glaze::tr(demo::str::OmSvarOk));     break;
    case glaze::DialogResult::Cancel: tips.setText(glaze::tr(demo::str::OmSvarAvbryt)); break;
    default:                          tips.setText(glaze::tr(demo::str::OmSvarStangd)); break;
    }
}

void SecondWindow::frittClicked(glaze::Widget*, glaze::ClickArgs&)
{
    fritt_.show(*this);   // ägt — kaskad-stängs med det här fönstret
}

void SecondWindow::stangClicked(glaze::Widget*, glaze::ClickArgs&)
{
    close();   // begär stängning — App verkställer i pump-svansen
}

// Meddelanderutan — EN rad, som `wxMessageBox`, fast med husets egna
// pixlar: ingen systemdialog spräcker illusionen.
//
// Väljarna är hela poängen med demon här: alla FEM typerna gånger alla SEX
// knappuppsättningarna, plus egen bild i stället för badgen. Värdena läses
// ur widgetarna (regel 6 — ur sender, aldrig ur ett cachat fält), så det
// som visas är alltid det som står i rutorna.
void SecondWindow::meddClicked(glaze::Widget*, glaze::ClickArgs&)
{
    using T = glaze::MessageBoxType;
    using B = glaze::MessageBoxButtons;

    // Ordningen följer dropdownens items EXAKT — listorna står i
    // `.glazeui` och indexen är kontraktet mellan dem och den här raden.
    static constexpr T kTyper[] = {T::Custom, T::Information, T::Confirmation,
                                   T::Warning, T::Error};
    static constexpr B kKnappar[] = {B::OK,          B::OKCancel,
                                     B::YesNo,       B::YesNoCancel,
                                     B::RetryCancel, B::AbortRetryIgnore};

    const int ti = typDd.selectedIndex();
    const int ki = knappDd.selectedIndex();
    if (ti < 0 || ki < 0)
        return;

    // Egen bild vinner över typens badge — det är kontraktet i
    // `message_box.h`, och kugghjulet ligger redan i demons assets.bin.
    const glaze::AssetHandle bild =
        egenBild.isChecked() ? glaze::img("toolbar", "gear") : glaze::AssetHandle{};

    const glaze::DialogResult r =
        glaze::messageBox(*this,
                          trs(demo::str::MeddText),
                          trs(demo::str::MeddTitel), kKnappar[ki],
                          kTyper[ti], bild);

    // `None` kan aldrig komma hit — Escape och krysset mappas till
    // uppsättningens avbryt-ekvivalent. Raden finns ändå, för den dagen
    // kontraktet ändras ska demon säga det i stället för att tiga.
    std::uint32_t svar = demo::str::SvarKontraktsbrott;
    switch (r) {
    case glaze::DialogResult::OK:     svar = glaze::str::Ok;     break;
    case glaze::DialogResult::Cancel: svar = glaze::str::Cancel; break;
    case glaze::DialogResult::Yes:    svar = glaze::str::Yes;    break;
    case glaze::DialogResult::No:     svar = glaze::str::No;     break;
    case glaze::DialogResult::Retry:  svar = glaze::str::Retry;  break;
    case glaze::DialogResult::Abort:  svar = demo::str::SvarAbort; break;
    case glaze::DialogResult::Ignore: svar = glaze::str::Ignore; break;
    case glaze::DialogResult::None:   svar = demo::str::SvarKontraktsbrott; break;
    }
    tips.setText(trs(demo::str::SvarPrefix) + trs(svar));
}
