#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <gfx/print.h>

#include "glaze/app_window.h"
#include "glaze/canvas/bitmap.h"     // renderPageImage
#include "glaze/canvas/geometry.h"   // Rect
#include "glaze/canvas/painter.h"
#include "glaze/language.h"

// UTSKRIFTEN — operativsystemets utskriftssystem.
// En PDF fås genom systemets PDF-skrivare, så ramverket har ingen egen
// PDF-motor.
//
//     glaze::PrintJob jobb;
//     jobb.setTitle("Ritning 12");
//     jobb.setPaper(glaze::PaperSize::A4);
//     jobb.setOrientation(glaze::PageOrientation::Landscape);
//     if (jobb.showDialog(*this))
//         jobb.run(*this, 3, [this](int sida, glaze::Painter& p) {
//             ritaSida(sida, p);   // samma funktion som förhandsvisningen
//             return true;
//         });
//
// DIALOGEN ÄR SYSTEMETS — ett MEDVETET undantag från "dialogerna är våra"
// (filväljaren, färgdialogen): skrivaren, kopiorna, sidintervallet och
// skrivarens EGNA inställningar (drivrutinens flikar) finns bara i OS:ets
// dialog.
//
// TILL FIL: filvalet är VÅRT. En skrivare som frågar efter fil när jobbet
// startar (Microsoft Print to PDF) har en systemruta som inte går att fylla i,
// så `PrintJob` visar `FileDialog` med namnet ifyllt och en fråga före
// överskrivning: direkt efter Skriv ut i `showDialog`, annars i `run`. Filen
// går också att ange med `setOutputFile`, och då frågar ingen.
//
// EN KLASS MED EGENSKAPER, som `FileDialog` — många in- och utvärden
// (papperet, orienteringen, kopiorna och intervallet in; skrivaren, måtten
// och valen ut), och objektet kan BEHÅLLAS: efter dialogen står skrivaren
// och valen kvar, så nästa `run` skriver ut på samma skrivare utan dialog.
// INGEN WIDGET (meddelanderutans prejudikat): den registreras inte och står
// aldrig i en `.glazeui` — hård regel 8 gäller den inte.
//
// SIDAN RITAS MED RITYTANS `Painter` i sidans koordinater i MILLIMETER:
// (0, 0) i pappersets övre vänstra hörn och y nedåt, med pappersets mått i
// `paperWidth()` × `paperHeight()` (orienterade). `glaze::mm(…)` är
// millimeter på papperet, så en linjebredd och en textstorlek i mm blir
// lika stora på papperet som i en förhandsvisning på en rityta med samma
// mått — och texten mäts med samma atlas. `screenSpace()` byter till sidans
// "logiska px" (1/96 tum). Bara det UTSKRIVBARA området (`printableArea()`)
// kommer ut; det som ritas utanför klipps av skrivaren.
//
// KVALITETEN — GRÄNSEN: sidan RASTRERAS i `resolution()` punkter per
// tum (förval 300, aldrig mer än skrivarens egen upplösning) med ritytans
// motor och skickas som EN BILD per sida. Linjer och text blir alltså inte
// vektorer i en PDF-skrivare än. Bilderna ritas ur sin fulla upplösning, och
// ingen bild saknas för att den väntar på uppladdningens budget: sidan ritas
// om tills varje bild är uppe. Den längsta sidan av bilden är högst
// `kMaxPagePx` punkter — en större sida rastreras i en lägre upplösning
// (loggat en gång).
//
// JOBBET (`run`): sidorna en i taget i pump-svansen, med ett FÖRLOPPSFÖNSTER
// över ägaren (sidan och antalet, en förloppsstapel och Avbryt). Avbryt,
// Escape och krysset avbryter jobbet; det som redan skickats tas tillbaka där
// OS:et kan. `run` återvänder när jobbet skickats eller avbrutits. Ritningen
// är ritytans: utanför en frame, med ägarens tema och fonter, och funktionen
// får varken stänga ägaren eller öppna en modal.
//
//   Windows  PrintDlgEx och GDI (StretchDIBits på skrivarens DC)
//   macOS    NSPrintPanel och NSPrintOperation
//   Linux    INTE BYGGT — `isSupported()` false, `showDialog` och `run`
//            svarar false (loggat en gång per process)

namespace glaze {

using PageOrientation = gfx::PageOrientation;

// STANDARDFORMATEN — `Custom` är egna mått (`setPaperSize`).
enum class PaperSize : std::uint8_t {
    A3,        // 297 × 420 mm
    A4,        // 210 × 297 mm
    A5,        // 148 × 210 mm
    A6,        // 105 × 148 mm
    Letter,    // 8,5 × 11 tum
    Legal,     // 8,5 × 14 tum
    Tabloid,   // 11 × 17 tum
    Custom,
};

// Marginalerna i mm — pappersets kanter till det utskrivbara området.
struct PageMargins {
    double left   = 0.0;
    double top    = 0.0;
    double right  = 0.0;
    double bottom = 0.0;
};

// HUR DET SENASTE `showDialog` ELLER `run` GICK.
enum class PrintOutcome : std::uint8_t {
    None,             // inget anrop än
    Ok,               // `showDialog`: valen är klara. `run`: jobbet skickades,
                      // vilket inte säger att papperet kommit ut
    Cancelled,        // användaren avbröt, i någon dialog eller i förloppet
    NothingToPrint,   // ingen sida låg i intervallet
    FileFailed,       // filen gick inte att skriva (`lastError()`)
    Failed,           // något annat fel (`lastError()`)
};

class PrintJob {
public:
    // Sidans ritning: sidnumret FRÅN 1 (samma tal som dialogens intervall)
    // och målaren i sidans mm. false = det här var SISTA sidan.
    using PageFn = std::function<bool(int page, Painter& p)>;

    // Den längsta sidan av en sidas bild, i punkter.
    static constexpr int kMaxPagePx = 8192;

    PrintJob();
    ~PrintJob();
    PrintJob(const PrintJob&)            = delete;
    PrintJob& operator=(const PrintJob&) = delete;

    // Har plattformen ett utskriftssystem? Windows och macOS true, Linux
    // false.
    static bool isSupported();

    // DOKUMENTETS NAMN i skrivarkön, och filvalets förslag när inget annat är
    // satt. Tomt = ägarfönstrets titel.
    void          setTitle(TrText t) { title_ = std::move(t); }
    const TrText& title() const { return title_; }

    // SKRIVAREN för en `run` utan dialog (tom = standardskrivaren). Dialogens
    // val ersätter den. Skrivaren öppnas vid nästa `run`.
    void setPrinter(std::string name);

    // FILEN jobbet skrivs till (UTF-8; en relativ sökväg görs absolut direkt),
    // utan att någon frågar. Tom = ingen fil. En befintlig fil skrivs över utan
    // fråga — det är appens sak att fråga. Filen gäller tills den ändras, men
    // väljer användaren en skrivare som inte frågar efter fil i `showDialog` töms
    // den. En sådan skrivare utan dialog skriver sina rådata till filen, så välj
    // skrivaren med `setPrinter`. Avbryts jobbet skapas ingen ny fil och en
    // befintlig fil blir orörd. Efter filvalet står användarens fil här; den
    // gäller ett jobb, och nästa `run` frågar igen.
    void               setOutputFile(std::string utf8);
    const std::string& outputFile() const { return settings_.outputFile; }

    // FILVALETS FÖRSLAG ("Ritning 12", gärna utan ändelse — den läggs till när
    // skrivarens är känd). Tom = jobbets titel. Bara Windows har filvalet.
    void               setSuggestedFileName(std::string utf8) { suggestedName_ = std::move(utf8); }
    const std::string& suggestedFileName() const { return suggestedName_; }

    // FILVALET (förval på). Av: glaze frågar inte, utan appen sätter filen
    // själv när `promptsForFile()` är sann, eller så frågar systemets ruta.
    void setFilePrompt(bool on) { filePrompt_ = on; }
    bool filePrompt() const { return filePrompt_; }

    // Frågar skrivaren efter en fil när jobbet startar? Giltig när skrivaren är
    // vald (efter `showDialog` eller `run`). Windows: porten PORTPROMPT:. macOS
    // och Linux: false.
    bool promptsForFile() const { return settings_.promptsForFile; }

    // PAPPERET — ett standardformat eller egna mått i mm (stående: bredden
    // den korta sidan). Mått ≤ 0 eller som inte är ändliga ⇒ `gfx::log` och
    // det gamla gäller. Efter dialogen: skrivarens val. Papperet,
    // orienteringen och kopiorna bor i skrivarens inställningar, så en sättare
    // efter dialogen öppnar SAMMA skrivare på nytt vid nästa `run` (och
    // `printableArea()` är hela papperet till dess).
    void      setPaper(PaperSize p);
    void      setPaperSize(double widthMm, double heightMm);
    PaperSize paper() const;
    // Pappersets mått i mm i den ORIENTERADE sidan (liggande A4 = 297 × 210)
    // — sidans koordinatrymd.
    double paperWidth() const;
    double paperHeight() const;

    void            setOrientation(PageOrientation o);
    PageOrientation orientation() const { return settings_.orientation; }

    // KOPIORNA (≥ 1) — skrivarens drivrutin skriver dem.
    void setCopies(int n);
    int  copies() const { return settings_.copies; }

    // SIDINTERVALLET, sidnummer från 1 — `run` ritar bara de sidorna (de
    // före ritas men skickas inte, för en ritning sida för sida måste veta
    // var den är). 0, 0 = alla. `from` > `to` eller negativa ⇒ `gfx::log`
    // och det gamla gäller. Efter dialogen: användarens val.
    void setPageRange(int from, int to);
    int  fromPage() const { return settings_.fromPage; }
    int  toPage() const { return settings_.toPage; }
    // Dokumentets sidor — dialogens tak för intervallet; 0 = okänt.
    void setPageCount(int n) { settings_.pageCount = n > 0 ? n : 0; }
    int  pageCount() const { return settings_.pageCount; }

    // RASTRERINGENS UPPLÖSNING i punkter per tum (förval 300) — klampad till
    // [72, 1200] och aldrig över skrivarens egen.
    void setResolution(int dpi);
    int  resolution() const { return dpi_; }

    // SYSTEMETS UTSKRIFTSDIALOG, modal över `owner` (som måste vara öppet).
    // Förvalen är egenskaperna ovan. true = användaren valde att skriva ut,
    // och skrivaren, papperet, orienteringen, kopiorna och intervallet är
    // användarens val; false = avbruten (`lastError()` tom, ingen logg) eller
    // ett fel (`lastError()` säger vilket, loggat) — egenskaperna är orörda.
    // Frågar skrivaren efter fil visas filvalet innan anropet svarar, och Avbryt
    // där är samma sak som Avbryt i dialogen.
    bool showDialog(AppWindow& owner);

    // UTFALLET av det senaste `showDialog` eller `run`.
    PrintOutcome outcome() const { return outcome_; }

    // FELBESKEDET efter ett `false` ur `showDialog` eller `run`, en mening på
    // appens språk att visa för användaren. Tom = inget fel: användaren
    // avbröt, eller inget låg i intervallet. Töms av nästa `showDialog`/`run`.
    const std::string& lastError() const { return error_.str(); }

    // Skrivaren — tom före dialogen (eller före `run` utan dialog, som tar
    // standardskrivaren).
    const std::string& printerName() const { return settings_.printer; }
    // DET UTSKRIVBARA OMRÅDET i sidans mm, och marginalerna runt det. Före
    // en skrivare är vald: hela papperet och inga marginaler.
    Rect        printableArea() const;
    PageMargins margins() const;

    // SKRIVER UT: `pages` sidor (> 0), eller 0 = SIDA FÖR SIDA tills `draw`
    // svarar false. Utan dialog först skriver jobbet ut på
    // standardskrivaren. `owner` måste vara öppet (renderaren och
    // förloppsfönstrets ägare). true = jobbet skickades; false = avbrutet,
    // inget att skriva ut eller ett fel (loggat, `lastError()`). Medan jobbet
    // går är `isRunning()` sann och `cancel()` avbryter det efter sidan som
    // ritas.
    bool run(AppWindow& owner, int pages, PageFn draw);
    void cancel();
    bool isRunning() const { return running_; }
    bool isCancelled() const { return cancelled_; }
    // Sidorna som skickades i det senaste `run`.
    int  pagesPrinted() const { return printed_; }

private:
    friend class PrintProgressWindow;   // src/print.cpp — jobbets steg

    // Skrivarens öppna inställningar gäller inte längre (papperet,
    // orienteringen eller kopiorna ändrades) — namnet står kvar.
    void reopenPrinter();

    // Filvalet för inställningarna `s`: vår dialog över `owner`. true = en fil
    // vald (i `chosen`), false = Avbryt.
    bool askForFile(AppWindow& owner, const gfx::PrintSettings& s, std::string& chosen);
    // Utfallet, felbeskedet och avbrottet av ett jobbresultat.
    void setResult(gfx::PrintJobResult r);

    gfx::PrintSettings settings_;
    TrText             title_;
    TrText             error_;
    std::string        suggestedName_;
    PrintOutcome       outcome_    = PrintOutcome::None;
    bool               filePrompt_ = true;
    // `outputFile` är vald i vår dialog (annars appens), och ett jobb har
    // redan använt den.
    bool               fileFromDialog_ = false;
    bool               fileSpent_      = false;
    int                dpi_       = 300;
    bool               running_   = false;
    bool               cancelled_ = false;
    int                printed_   = 0;
};

// EN SIDA SOM BILD — samma ritning som en utskriftssida, utan skrivare: en vit
// sida `widthMm` × `heightMm`, en `Painter` i sidans mm och `glaze::mm` som
// millimeter på sidan. `Painter::drawCanvas` ritar en rityta i sidans upplösning
// med sina lager, sin kartkamera eller 3D-kamera och sitt klipp. Så förbereds en
// flersidig utskrift sida för sida, eller exporteras ett kartblad i exakt skala.
//
//     auto sida = glaze::renderPageImage(*this, 297.0, 210.0, 300.0, [&](glaze::Painter& p) {
//         p.drawCanvas(karta, glaze::Rect{10, 10, 277, 190}, kamera);
//     });
//
// Bilderna väntar inte: sidan ritas om tills varje bild är uppladdad, så `draw` kan
// köras flera gånger och ska rita deterministiskt. Fönstret ger renderaren, temat
// och fonterna; det får vara dolt men måste vara öppet, och ritytan får sitta i ett
// annat fönster. Anropas utanför en ritning — i en handler eller `onLoad`, inte i
// `draw` eller `PageFn`.
struct PageImage {
    Bitmap bitmap;
    // Tätheten bilden fick: den begärda, eller lägre när den längsta sidan annars
    // går över `PrintJob::kMaxPagePx` eller renderarens största textur.
    double dpi = 0.0;
    // false = bilder saknas: fler än taket för omritningarna väntade på uppladdning.
    bool complete = true;
};

// Tomt (+ `gfx::log`): mått eller täthet som inte är ändliga och över 0, ingen
// ritfunktion, ett fönster som inte är öppet, ett anrop inuti en ritning eller ett
// mål som inte gick att skapa.
std::optional<PageImage> renderPageImage(AppWindow& window, double widthMm,
                                         double heightMm, double dpi,
                                         const std::function<void(Painter&)>& draw);

} // namespace glaze
