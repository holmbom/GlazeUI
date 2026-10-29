#include "form_dialog.h"

// Fokus i första fältet när formuläret öppnas — `onLoad` och inte
// konstruktorn (den är genererad plumbing).
void FormDialog::onLoad(glaze::Widget*, glaze::EventArgs&)
{
    namn.focus();
}

// Kontaktlistan visas och döljs med sin rubrik. Fönstret följer: LÅST krymper
// och växer det med listan, som GOLV växer det bara — en dold lista sänker
// golvet men lämnar storleken (`SizeToContentMode::GrowOnly`).
void FormDialog::visaAndrad(glaze::Widget*, glaze::ChangeArgs&)
{
    if (visa.isChecked()) {
        lblKontakter.show();
        kontakter.show();
    } else {
        lblKontakter.hide();
        kontakter.hide();
    }
}

// "Lås storleken": innehållet som LÅS (fönstret går inte att dra och krymper
// till raderna) eller som GOLV (det går att dra upp, aldrig under raderna).
void FormDialog::lasAndrad(glaze::Widget*, glaze::ChangeArgs&)
{
    setSizeToContentMode(las.isChecked() ? glaze::SizeToContentMode::GrowAndShrink
                                         : glaze::SizeToContentMode::GrowOnly);
}
