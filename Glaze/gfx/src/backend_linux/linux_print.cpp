// UTSKRIFTEN PÅ LINUX — en LÄSBAR DEGRADERING.
//
// Utskriften är inte byggd på Linux än. Systemets dialog finns bara i
// skrivbordens verktygslådor (GTK:s GtkPrintUnixDialog, Qt:s QPrintDialog),
// och ramverket länkar ingen av dem. Vägen dit: skrivaren och dess val
// genom XDG-portalen `org.freedesktop.portal.Print` (PreparePrint visar
// skrivbordets egen dialog och svarar med inställningarna, Print tar en
// fil-deskriptor), med sidorna som en PDF eller PostScript vi skriver
// själva — eller utan portal CUPS (`lpstat -d`, `lp`) med en egen dialog.
//
// Fasaden (gfx/print.h) svarar därför ärligt att det inte finns något
// utskriftssystem: `printAvailable()` är false, dialogen och standard-
// skrivaren vägrar och inget jobb startar. Allt är TYST — `glaze::PrintJob`
// loggar EN gång per process när appen ber om utskrift, och en rad per
// anrop här hade bara upprepat den.

#include "gfx/print.h"

namespace gfx {

bool printAvailable()
{
    return false;
}

bool showPrintDialog(Window*, PrintSettings&)
{
    return false;
}

bool openPrinter(PrintSettings&)
{
    return false;
}

std::unique_ptr<PrintDocument> beginPrintDocument(const PrintSettings&,
                                                  const std::string&)
{
    return nullptr;
}

} // namespace gfx
