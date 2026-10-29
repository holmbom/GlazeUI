#pragma once
#include <glaze/glaze.h>

#include "demo_text.h"

// Om-dialogen — kvittot på fönstermodellen (VISION): en dialog är ett
// VANLIGT AppWindow skrivet i ren C++, med setModal(true) i konstruktorn,
// visat med run(owner). Knapparna BÄR svaret (`setDialogResult` — ett klick
// stänger med det), och fönstrets standard- och avbrytknapp
// (`setAcceptButton`/`setCancelButton`) ger Enter och Escape
// — WinForms `DialogResult`/`AcceptButton`/`CancelButton` rakt av, utan en
// enda handler. Skapas som LOKAL variabel runt anropet — WinForms-mönstret,
// användaren äger objektet:
//
//     AboutDialog dlg;
//     if (dlg.run(*this) == glaze::DialogResult::OK) { ... }
class AboutDialog : public glaze::AppWindow {
public:
    AboutDialog();
};
