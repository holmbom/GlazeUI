// UTSKRIFTEN PÅ WINDOWS — fasaden gfx/print.h ovanpå
// PrintDlgEx och GDI.
//
// DIALOGEN: PrintDlgEx med PD_RETURNDC ger skrivarens DC — skapad med den
// DEVMODE användaren lämnade dialogen med, alltså papperet, orienteringen
// och drivrutinens egna inställningar. Förvalen går IN som en DEVMODE för
// STANDARDSKRIVAREN (DocumentProperties fyller den, och vi ändrar bara
// papperet, orienteringen och kopiorna), så dialogen öppnar med appens
// format. KOPIORNA och sorteringen sköter drivrutinen
// (PD_USEDEVMODECOPIESANDCOLLATE): varje sida skickas EN gång.
//
// MÅTTEN läses ur DC:n: PHYSICALWIDTH/-HEIGHT är papperet och HORZRES/
// VERTRES med PHYSICALOFFSETX/-Y det utskrivbara området, alla i
// skrivarens pixlar och i den ORIENTERADE sidan — DC:ns origo är det
// utskrivbara områdets övre vänstra hörn.
//
// SIDAN: StartPage, bilden med StretchDIBits över hela det utskrivbara
// området (HALFTONE — skrivaren har fler punkter än bilden har pixlar) och
// EndPage. GDI tar en 32-bitars DIB i BGRA, så raden vänds kanal för kanal.
//
// TILL FIL: `PrintSettings::outputFile` går som `DOCINFO::lpszOutput`, så att
// skrivaren inte frågar efter fil. Dialogens "Skriv till fil" är dold
// (PD_HIDEPRINTTOFILE) — filen är appens val. `readPrinterOutput` läser ur
// skrivarens inställningar om den frågar efter fil när jobbet startar (porten
// PORTPROMPT:) och vilken ändelse den ger (en drivrutin med "PDF" i namnet:
// ".pdf"). Före `StartDocW` provöppnas en befintlig fil för skrivning utan att
// ändras, för en fil som är låst eller skrivskyddad får annars Windows egen
// ruta i stället för ett fel. Felkoderna från `StartDocW` och `EndDoc` läses
// direkt efter det misslyckade anropet och blir `PrintJobResult`: avbrott i
// skrivarens ruta `Cancelled` (ingen logg), ett filfel `FileFailed` och allt
// annat `Failed`.
//
// DC:n är `PrintSettings::native` (en `PrinterDc`) och rivs när den sista
// kopian av inställningarna och det sista jobbet på den är borta.

#include "d3d11_backend.h"

#include <commdlg.h>
#include <winspool.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <vector>

#include "gfx/platform.h"
#include "gfx/print.h"

namespace gfx {

namespace {

// Millimeter per tum — DC:ns punkter per tum blir mm.
constexpr float kMmPerInch = 25.4f;
// Dialogens tak för sidintervallet när dokumentets sidor är okända.
constexpr int kUnknownPageMax = 9999;
// Ett standardformat matchar när båda måtten stämmer så här nära (mm).
constexpr float kPaperToleranceMm = 1.0f;

// Standardformaten DEVMODE känner, med sina mått i mm (stående).
struct PaperCode {
    short code;
    float w;
    float h;
};
constexpr PaperCode kPapers[] = {
    {DMPAPER_A3, 297.0f, 420.0f},     {DMPAPER_A4, 210.0f, 297.0f},
    {DMPAPER_A5, 148.0f, 210.0f},     {DMPAPER_A6, 105.0f, 148.0f},
    {DMPAPER_LETTER, 215.9f, 279.4f}, {DMPAPER_LEGAL, 215.9f, 355.6f},
    {DMPAPER_TABLOID, 279.4f, 431.8f},
};

// Skrivarens DC — `PrintSettings::native`.
struct PrinterDc {
    explicit PrinterDc(HDC d) : dc(d) {}
    ~PrinterDc() { DeleteDC(dc); }
    PrinterDc(const PrinterDc&)            = delete;
    PrinterDc& operator=(const PrinterDc&) = delete;
    HDC dc;
};

std::wstring defaultPrinter()
{
    DWORD n = 0;
    GetDefaultPrinterW(nullptr, &n);   // ERROR_INSUFFICIENT_BUFFER, längden
    if (n == 0)
        return {};
    std::wstring namn(n, L'\0');
    if (!GetDefaultPrinterW(namn.data(), &n))
        return {};
    namn.resize(n > 0 ? n - 1 : 0);   // längden räknar nollan
    return namn;
}

// Inställningarnas papper, orientering och kopior i en DEVMODE.
void applySettings(DEVMODEW& dm, const PrintSettings& s)
{
    dm.dmOrientation = s.orientation == PageOrientation::Landscape
                           ? DMORIENT_LANDSCAPE
                           : DMORIENT_PORTRAIT;
    dm.dmFields |= DM_ORIENTATION;
    const auto träff = std::find_if(
        std::begin(kPapers), std::end(kPapers), [&](const PaperCode& p) {
            return std::fabs(p.w - s.paperMm.w) <= kPaperToleranceMm &&
                   std::fabs(p.h - s.paperMm.h) <= kPaperToleranceMm;
        });
    if (träff != std::end(kPapers)) {
        dm.dmPaperSize = träff->code;
        dm.dmFields |= DM_PAPERSIZE;
        dm.dmFields &= ~static_cast<DWORD>(DM_PAPERWIDTH | DM_PAPERLENGTH);
    } else {
        // Egna mått i tiondels mm — de vinner över formatkoden.
        dm.dmPaperSize   = DMPAPER_USER;
        dm.dmPaperWidth  = static_cast<short>(std::lround(s.paperMm.w * 10.0f));
        dm.dmPaperLength = static_cast<short>(std::lround(s.paperMm.h * 10.0f));
        dm.dmFields |= DM_PAPERSIZE | DM_PAPERWIDTH | DM_PAPERLENGTH;
    }
    dm.dmCopies = static_cast<short>(std::clamp(s.copies, 1, 32767));
    dm.dmFields |= DM_COPIES;
}

// Skrivarens DEVMODE med inställningarna, i ett GlobalAlloc-block (PrintDlgEx
// tar det; CreateDC läser det). nullptr när skrivaren inte går att fråga.
HGLOBAL devModeFor(const std::wstring& printer, const PrintSettings& s)
{
    std::wstring namn = printer;   // Win32 tar en skrivbar sträng
    HANDLE       h    = nullptr;
    if (!OpenPrinterW(namn.data(), &h, nullptr))
        return nullptr;
    HGLOBAL    g    = nullptr;
    const LONG size = DocumentPropertiesW(nullptr, h, namn.data(), nullptr,
                                          nullptr, 0);
    if (size > 0)
        g = GlobalAlloc(GHND, static_cast<SIZE_T>(size));
    if (g) {
        auto* dm = static_cast<DEVMODEW*>(GlobalLock(g));
        bool  ok = dm && DocumentPropertiesW(nullptr, h, namn.data(), dm,
                                             nullptr, DM_OUT_BUFFER) == IDOK;
        if (ok) {
            applySettings(*dm, s);
            // Drivrutinen slår ihop ändringarna med sina egna fält.
            ok = DocumentPropertiesW(nullptr, h, namn.data(), dm, dm,
                                     DM_IN_BUFFER | DM_OUT_BUFFER) == IDOK;
        }
        if (dm)
            GlobalUnlock(g);
        if (!ok) {
            GlobalFree(g);
            g = nullptr;
        }
    }
    ClosePrinter(h);
    return g;
}

// UT-fälten ur DC:n och DEVMODE:n (orienteringen och kopiorna).
void readSettings(HDC dc, const DEVMODEW* dm, PrintSettings& s)
{
    if (dm) {
        if (dm->dmFields & DM_ORIENTATION)
            s.orientation = dm->dmOrientation == DMORIENT_LANDSCAPE
                                ? PageOrientation::Landscape
                                : PageOrientation::Portrait;
        if (dm->dmFields & DM_COPIES)
            s.copies = std::max<int>(1, dm->dmCopies);
    }
    const float dx = static_cast<float>(GetDeviceCaps(dc, LOGPIXELSX));
    const float dy = static_cast<float>(GetDeviceCaps(dc, LOGPIXELSY));
    if (dx <= 0.0f || dy <= 0.0f)
        return;   // ingen skrivar-DC — måtten står kvar
    s.dpiX         = dx;
    s.dpiY         = dy;
    const float wx = kMmPerInch / dx;
    const float wy = kMmPerInch / dy;
    const float pw = static_cast<float>(GetDeviceCaps(dc, PHYSICALWIDTH)) * wx;
    const float ph = static_cast<float>(GetDeviceCaps(dc, PHYSICALHEIGHT)) * wy;
    s.paperMm = s.orientation == PageOrientation::Landscape ? Sizef{ph, pw}
                                                            : Sizef{pw, ph};
    s.printableMm =
        Rectf{static_cast<float>(GetDeviceCaps(dc, PHYSICALOFFSETX)) * wx,
              static_cast<float>(GetDeviceCaps(dc, PHYSICALOFFSETY)) * wy,
              static_cast<float>(GetDeviceCaps(dc, HORZRES)) * wx,
              static_cast<float>(GetDeviceCaps(dc, VERTRES)) * wy};
}

// Innehåller `text` ordet `ord`, utan hänsyn till skiftläge?
bool innehållerUtanSkiftläge(const wchar_t* text, const wchar_t* ord)
{
    if (!text)
        return false;
    std::wstring hö = text;
    std::wstring nål = ord;
    for (wchar_t& c : hö)
        c = static_cast<wchar_t>(std::towlower(c));
    for (wchar_t& c : nål)
        c = static_cast<wchar_t>(std::towlower(c));
    return hö.find(nål) != std::wstring::npos;
}

// UT-fälten `promptsForFile` och `outputExtension` ur skrivarens inställningar:
// porten PORTPROMPT: betyder att skrivaren frågar efter fil när jobbet
// startar, och "PDF" i drivrutinens namn ger ".pdf". Går skrivaren inte att
// läsa loggas det och båda fälten nollställs.
void readPrinterOutput(const std::wstring& printer, PrintSettings& s)
{
    s.promptsForFile = false;
    s.outputExtension.clear();
    std::wstring namn = printer;   // Win32 tar en skrivbar sträng
    HANDLE       h    = nullptr;
    if (!OpenPrinterW(namn.data(), &h, nullptr)) {
        log("Utskrift: skrivarens inställningar gick inte att läsa (%lu)",
            GetLastError());
        return;
    }
    DWORD bytes = 0;
    GetPrinterW(h, 2, nullptr, 0, &bytes);   // ERROR_INSUFFICIENT_BUFFER, längden
    std::vector<BYTE> buf(bytes);
    if (bytes == 0 || !GetPrinterW(h, 2, buf.data(), bytes, &bytes)) {
        log("Utskrift: skrivarens inställningar gick inte att läsa (%lu)",
            GetLastError());
        ClosePrinter(h);
        return;
    }
    ClosePrinter(h);
    const auto* info = reinterpret_cast<const PRINTER_INFO_2W*>(buf.data());
    s.promptsForFile = innehållerUtanSkiftläge(info->pPortName, L"PORTPROMPT:");
    if (innehållerUtanSkiftläge(info->pDriverName, L"PDF"))
        s.outputExtension = ".pdf";
}

// Felkoderna som betyder att filen `outputFile` inte gick att skriva.
bool ärFilfel(DWORD fel)
{
    switch (fel) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_INVALID_NAME:
    case ERROR_FILENAME_EXCED_RANGE:
    case 3003:   // skrivskyddad fil
        return true;
    default:
        return false;
    }
}

class D3dPrintDocument final : public PrintDocument {
public:
    D3dPrintDocument(std::shared_ptr<PrinterDc> dc, bool harFil)
        : dc_(std::move(dc)), harFil_(harFil)
    {
    }
    ~D3dPrintDocument() override
    {
        if (open_)
            abort();
    }

    bool addPage(const std::uint8_t* rgba, int w, int h) override
    {
        if (!open_ || !rgba || w <= 0 || h <= 0)
            return false;
        const HDC dc = dc_->dc;
        if (StartPage(dc) <= 0) {
            log("Utskrift: StartPage vägrade (%lu)", GetLastError());
            return false;
        }
        // RGBA → BGRA, rad 0 överst (negativ höjd = top-down).
        const std::size_t n = static_cast<std::size_t>(w) *
                              static_cast<std::size_t>(h);
        bgra_.resize(n * 4);
        for (std::size_t i = 0; i < n; ++i) {
            bgra_[i * 4 + 0] = rgba[i * 4 + 2];
            bgra_[i * 4 + 1] = rgba[i * 4 + 1];
            bgra_[i * 4 + 2] = rgba[i * 4 + 0];
            bgra_[i * 4 + 3] = rgba[i * 4 + 3];
        }
        BITMAPINFO bi{};
        bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = w;
        bi.bmiHeader.biHeight      = -h;
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);   // HALFTONE kräver det efteråt
        const int rader = StretchDIBits(
            dc, 0, 0, GetDeviceCaps(dc, HORZRES), GetDeviceCaps(dc, VERTRES), 0,
            0, w, h, bgra_.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        const bool ritad = rader != 0 && rader != static_cast<int>(GDI_ERROR);
        if (!ritad)
            log("Utskrift: StretchDIBits vägrade sidan (%d × %d)", w, h);
        if (EndPage(dc) <= 0) {
            log("Utskrift: EndPage vägrade (%lu)", GetLastError());
            return false;
        }
        return ritad;
    }

    PrintJobResult finish() override
    {
        if (!open_)
            return PrintJobResult::Failed;
        open_ = false;
        if (EndDoc(dc_->dc) <= 0) {
            // Läses direkt: felet ligger kvar efter ett lyckat anrop och skrivs
            // över av nästa.
            const DWORD fel = GetLastError();
            if (fel == ERROR_CANCELLED || fel == ERROR_PRINT_CANCELLED)
                return PrintJobResult::Cancelled;
            log("Utskrift: EndDoc vägrade (%lu)", fel);
            return harFil_ ? PrintJobResult::FileFailed : PrintJobResult::Failed;
        }
        return PrintJobResult::Ok;
    }

    void abort() override
    {
        if (!open_)
            return;
        open_ = false;
        AbortDoc(dc_->dc);
    }

private:
    std::shared_ptr<PrinterDc> dc_;
    std::vector<std::uint8_t>  bgra_;
    bool                       harFil_;
    bool                       open_ = true;
};

} // namespace

bool printAvailable()
{
    return true;
}

PrintDialogResult showPrintDialog(Window* owner, PrintSettings& s)
{
    const HWND ägare = owner ? static_cast<D3dWindow*>(owner)->hwnd() : nullptr;
    if (!platformInitialized() || !ägare) {
        log("Utskrift: dialogen kräver ett öppet ägarfönster");
        return PrintDialogResult::DialogFailed;
    }
    const std::wstring printer = defaultPrinter();

    const int max = s.pageCount > 0 ? s.pageCount : kUnknownPageMax;
    PRINTPAGERANGE intervall{};
    PRINTDLGEXW    pd{};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner   = ägare;
    pd.hDevMode    = printer.empty() ? nullptr : devModeFor(printer, s);
    pd.Flags = PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE | PD_NOSELECTION |
               PD_NOCURRENTPAGE | PD_HIDEPRINTTOFILE;
    pd.nMinPage       = 1;
    pd.nMaxPage       = static_cast<DWORD>(max);
    pd.nMaxPageRanges = 1;
    pd.lpPageRanges   = &intervall;
    if (s.fromPage > 0) {
        const int från       = std::clamp(s.fromPage, 1, max);
        intervall.nFromPage  = static_cast<DWORD>(från);
        intervall.nToPage    = static_cast<DWORD>(
            std::clamp(s.toPage > 0 ? s.toPage : max, från, max));
        pd.nPageRanges = 1;
        pd.Flags |= PD_PAGENUMS;
    }
    pd.nCopies    = 1;   // kopiorna bär DEVMODE:n
    pd.nStartPage = START_PAGE_GENERAL;

    SetLastError(0);
    const HRESULT hr = PrintDlgExW(&pd);
    // Felkoderna läses före allt annat — nästa API-anrop skriver över dem.
    const DWORD sysFel = GetLastError();
    const DWORD dlgFel = CommDlgExtendedError();
    // Det dialogen lämnar ägs av oss, vilket svar det än blev.
    struct Släpp {
        PRINTDLGEXW& pd;
        bool         behållDc = false;
        ~Släpp()
        {
            if (pd.hDevMode)
                GlobalFree(pd.hDevMode);
            if (pd.hDevNames)
                GlobalFree(pd.hDevNames);
            if (pd.hDC && !behållDc)
                DeleteDC(pd.hDC);
        }
    } släpp{pd};
    if (FAILED(hr)) {
        log("Utskrift: PrintDlgEx misslyckades (0x%08lx)",
            static_cast<unsigned long>(hr));
        return PrintDialogResult::DialogFailed;
    }
    if (pd.dwResultAction != PD_RESULT_PRINT)
        return PrintDialogResult::Cancelled;   // också Verkställ + Avbryt
    if (!pd.hDC) {
        // Användaren valde att skriva ut, men drivrutinen gav ingen DC.
        log("Utskrift: skrivaren gick inte att öppna efter dialogen "
            "(GetLastError %lu, CommDlgExtendedError 0x%08lx)",
            static_cast<unsigned long>(sysFel),
            static_cast<unsigned long>(dlgFel));
        return PrintDialogResult::PrinterFailed;
    }

    const auto* dm =
        pd.hDevMode ? static_cast<const DEVMODEW*>(GlobalLock(pd.hDevMode))
                    : nullptr;
    readSettings(pd.hDC, dm, s);
    if (dm)
        GlobalUnlock(pd.hDevMode);
    bool skrivarLäst = false;
    if (pd.hDevNames) {
        if (const auto* dn = static_cast<const DEVNAMES*>(GlobalLock(pd.hDevNames))) {
            s.printer = toUtf8(reinterpret_cast<const wchar_t*>(dn) + dn->wDeviceOffset);
            GlobalUnlock(pd.hDevNames);
            readPrinterOutput(toWide(s.printer), s);
            skrivarLäst = true;
        }
    }
    if (!skrivarLäst) {
        s.promptsForFile = false;
        s.outputExtension.clear();
    }
    if ((pd.Flags & PD_PAGENUMS) && pd.nPageRanges >= 1) {
        s.fromPage = static_cast<int>(intervall.nFromPage);
        s.toPage   = static_cast<int>(intervall.nToPage);
    } else {
        s.fromPage = 0;
        s.toPage   = 0;
    }
    s.native       = std::make_shared<PrinterDc>(pd.hDC);
    släpp.behållDc = true;
    return PrintDialogResult::Print;
}

bool openPrinter(PrintSettings& s)
{
    if (!platformInitialized())
        return false;
    const std::wstring printer =
        s.printer.empty() ? defaultPrinter() : toWide(s.printer);
    if (printer.empty()) {
        log("Utskrift: ingen standardskrivare");
        return false;
    }
    const HGLOBAL g  = devModeFor(printer, s);
    const auto*   dm = g ? static_cast<const DEVMODEW*>(GlobalLock(g)) : nullptr;
    const HDC     dc = CreateDCW(L"WINSPOOL", printer.c_str(), nullptr, dm);
    if (dc)
        readSettings(dc, dm, s);
    if (dm)
        GlobalUnlock(g);
    if (g)
        GlobalFree(g);
    if (!dc) {
        log("Utskrift: skrivaren '%s' gick inte att öppna (%lu)",
            toUtf8(printer).c_str(), GetLastError());
        return false;
    }
    s.printer = toUtf8(printer);
    readPrinterOutput(printer, s);
    s.native  = std::make_shared<PrinterDc>(dc);
    return true;
}

PrintStart beginPrintDocument(const PrintSettings& s, const std::string& title)
{
    PrintStart start;   // Failed tills jobbet startat
    // `native` sätts bara här i filen, så typen är känd.
    std::shared_ptr<PrinterDc> dc = std::static_pointer_cast<PrinterDc>(s.native);
    if (!dc) {
        log("Utskrift: ingen skrivare vald");
        return start;
    }
    const bool         harFil = !s.outputFile.empty();
    const std::wstring namn   = toWide(title);
    const std::wstring fil    = toWide(s.outputFile);   // lever under StartDocW
    if (harFil) {
        // Provöppnar en befintlig fil utan att skapa, korta av eller ändra den.
        const HANDLE h = CreateFileW(
            fil.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        } else {
            const DWORD fel = GetLastError();
            if (fel != ERROR_FILE_NOT_FOUND && fel != ERROR_PATH_NOT_FOUND) {
                log("Utskrift: filen '%s' går inte att skriva (%lu)",
                    s.outputFile.c_str(), fel);
                start.result = PrintJobResult::FileFailed;
                return start;
            }
        }
    }
    DOCINFOW di{};
    di.cbSize      = sizeof(di);
    di.lpszDocName = namn.c_str();
    di.lpszOutput  = harFil ? fil.c_str() : nullptr;
    SetLastError(0);
    if (StartDocW(dc->dc, &di) <= 0) {
        // Läses direkt, före loggen.
        const DWORD fel = GetLastError();
        if (fel == ERROR_CANCELLED) {
            start.result = PrintJobResult::Cancelled;
            return start;
        }
        log("Utskrift: StartDoc vägrade (%lu)", fel);
        start.result = harFil && ärFilfel(fel) ? PrintJobResult::FileFailed
                                               : PrintJobResult::Failed;
        return start;
    }
    start.result = PrintJobResult::Ok;
    start.doc    = std::make_unique<D3dPrintDocument>(std::move(dc), harFil);
    return start;
}

} // namespace gfx
