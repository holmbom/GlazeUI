#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "gfx/geometry.h"
#include "gfx/window.h"

namespace gfx {

// UTSKRIFTEN — operativsystemets utskriftssystem:
// SYSTEMETS dialog (skrivare, kopior, sidintervall och skrivarens egna
// inställningar) och ett jobb som tar emot SIDORNA. Konsumeras av
// `glaze::PrintJob`; appar ser aldrig den här headern.
//
// DIALOGEN ÄR SYSTEMETS — ett MEDVETET undantag från "dialogerna är våra"
// — skrivarens egna inställningar
// (drivrutinens flikar, PDF-skrivarens filval) finns bara i OS:ets dialog.
//
// APP-GLOBALT, och därför fria funktioner (systemfältets skäl, gfx/tray.h):
// TRE implementationer i stället för sex, och ingen proxyfälla.
//
// SIDAN ÄR EN BILD — RGBA8 i rak alfa (opak: glaze ritar sidan på vitt), som
// täcker det UTSKRIVBARA OMRÅDET (`PrintSettings::printableMm`) och skalas
// av backenden till skrivarens pixlar. Linjer och text blir alltså inte
// vektorer i en PDF-skrivare än — gränsen står i glaze/print.h.
//
// UI-TRÅDENS, som resten av fasaden. Säker före `init()` och efter
// `shutdown()`: `printAvailable()` svarar ändå, resten vägrar.
//
//   Windows  PrintDlgEx med PD_RETURNDC och GDI på skrivarens DC: StartDoc/
//            StartPage och StretchDIBits; kopiorna och sorteringen sköter
//            drivrutinen (PD_USEDEVMODECOPIESANDCOLLATE)
//   macOS    NSPrintPanel med en NSPrintInfo, och NSPrintOperation med en vy
//            som ritar sidornas bilder — systemets förloppspanel visas när
//            jobbet skickas (se doc/os/macos/TESTS.md)
//   Linux    INTE BYGGT — `printAvailable()` är false och resten vägrar med
//            en logg (backend_linux/linux_print.cpp bär vägen dit)

// Sidans orientering. Pappersets mått står alltid STÅENDE (den korta sidan
// som bredd för standardformaten); Landscape vrider sidan.
enum class PageOrientation : std::uint8_t { Portrait, Landscape };

// UTSKRIFTENS INSTÄLLNINGAR — förvalen IN till dialogen och valen UT ur den.
struct PrintSettings {
    // ---- IN och UT
    // PAPPERET i mm, STÅENDE. Backenden väljer skrivarens standardformat när
    // måtten stämmer på en millimeter när, annars egna mått.
    Sizef           paperMm{210.0f, 297.0f};
    PageOrientation orientation = PageOrientation::Portrait;
    // Kopiorna (≥ 1) — skrivarens drivrutin skriver dem.
    int copies = 1;
    // SIDINTERVALLET, sidnummer från 1; 0 och 0 = alla sidor. UT: dialogens
    // val (ett intervall).
    int fromPage = 0;
    int toPage   = 0;
    // DOKUMENTETS SIDOR — dialogens gräns för intervallet; 0 = okänt (då
    // får intervallet gå till 9999).
    int pageCount = 0;

    // ---- UT (efter `showPrintDialog`/`openPrinter`)
    // Skrivarens namn — också IN till `openPrinter` (tomt = standardskrivaren).
    std::string printer;
    float       dpiX = 0.0f;        // skrivarens upplösning, punkter per tum
    float       dpiY = 0.0f;
    // DET UTSKRIVBARA OMRÅDET i mm, från pappersets övre vänstra hörn i den
    // ORIENTERADE sidan (liggande A4 är 297 × 210 mm).
    Rectf       printableMm{};
    // Backendens tillstånd — skrivarens DC (Windows), NSPrintInfo (macOS).
    // Opakt; tomt före dialogen.
    std::shared_ptr<void> native;
};

// Har plattformen ett utskriftssystem som fasaden talar? Windows och macOS
// true, Linux false.
bool printAvailable();

// SYSTEMETS UTSKRIFTSDIALOG, modal över `owner` (nullptr = ingen ägare).
// Inställningarna är förvalen in och valen ut. true = användaren valde att
// skriva ut; false = dialogen avbröts, ingen skrivare finns, eller ett fel
// (loggat) — `s` är då orörd.
bool showPrintDialog(Window* owner, PrintSettings& s);

// SKRIVAREN `s.printer` utan dialog (tom = STANDARDSKRIVAREN), med
// inställningarnas papper, orientering och kopior: fyller UT-fälten och
// `native`. false = skrivaren finns inte, ingen standardskrivare eller ett
// fel (loggat).
bool openPrinter(PrintSettings& s);

// ETT JOBB — sidorna i ordning, sedan `finish` eller `abort`. Ett jobb som
// rivs utan något av dem avbryts.
class PrintDocument {
public:
    virtual ~PrintDocument() = default;
    // EN SIDA: `rgba` (w × h, rak alfa) täcker det utskrivbara området.
    // false = skrivaren vägrade (loggat) — jobbet ska avbrytas.
    virtual bool addPage(const std::uint8_t* rgba, int w, int h) = 0;
    // Skickar jobbet. false = det gick inte (loggat).
    virtual bool finish() = 0;
    // Inget skrivs ut; sidor som redan skickats tas tillbaka där OS:et kan.
    virtual void abort() = 0;
};

// Startar ett jobb med titeln (dokumentets namn i skrivarkön) på skrivaren
// i `s.native`. nullptr = ingen skrivare vald eller ett fel (loggat).
std::unique_ptr<PrintDocument> beginPrintDocument(const PrintSettings& s,
                                                  const std::string& title);

} // namespace gfx
