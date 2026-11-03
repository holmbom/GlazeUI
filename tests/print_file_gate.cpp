// print_file_gate — UTSKRIFT TILL FIL, på riktigt mot Microsoft Print to PDF.
//
// Kör:  ./build/tests/print_file_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = skrivaren eller displayen saknas
//
// Ingen låtsasbackend: drivrutinens egna svar är det som mäts. `PrintJob` väljer
// skrivaren med `setPrinter` och skriver till filer i en egen temporär katalog som
// städas. En VAKTTRÅD tittar var 50:e ms efter nya synliga `#32770`-rutor i processen
// (Windows egen fråga efter ett filnamn), loggar och stänger dem och räknar dem — så
// att inget kan hänga, och så att fall som inte får visa någon ruta kan kräva noll.
// Vår egen spara-dialog (`FileDialogWindow`) styrs med `app().post` före `run`, som
// filedialog_gate styr sin: grinden hittar fönstret bland `app().windows()` och
// svarar på det som en användare.
//
// VAD GRINDEN PÅSTÅR:
//   F1  en ny fil, två sidor: Ok, en PDF, två sidor skickade, skrivaren frågar efter
//       fil (`promptsForFile`), felbeskedet tomt och ingen ruta
//   F2  åäö i mappen och filnamnet · F3 en befintlig fil ersätts
//   F4  katalogen saknas · F5 skrivskyddad fil · F6 en katalog som fil: FileFailed,
//       felbeskedet id 406, filen orörd och INGEN ruta
//   F7  en fil låst av ett annat handtag: FileFailed utan någon ruta
//   F8  `cancel()` med en ny fil: Cancelled, ingen fil, felbeskedet tomt
//   F9  `cancel()` med en befintlig fil: den är orörd
//   F10 `setFilePrompt(false)` utan fil: Windows ruta visas och vakten avbryter —
//       Cancelled, felbeskedet tomt · F11 samma jobb igen med en fil: Ok
//   F12 vår dialog: namnet ifyllt ur titeln med ändelsen, Avbryt ger Cancelled och
//       inget jobb startar
//   F13 vår dialog: OK skriver filen, och nästa `run` på samma objekt frågar igen
//   F14 ett känt tomt intervall (3 sidor, 5–6) och ett intervall utan sida bland
//       de ritade (sida för sida, 2–3, ritfunktionen slutar efter första): NothingToPrint,
//       ingen dialog, ingen fil
//
// SABOTAGE som SKA fälla dem (ett i taget):
//   * provöppningen av en befintlig fil bort ur `beginPrintDocument` (F7 — Windows
//     ruta visas eller utskriften hamnar någon annanstans)
//   * `lpszOutput` bort ur `StartDocW` (F1, F2, F3 — rutan visas och vakten avbryter)
//   * fel 1223 som Failed i stället för Cancelled (F10)
//   * FileFailed som Failed (F4, F5, F6, F7)
//   * "ett jobb"-regeln bort, `fileForRun` som låter en använd dialogfil gälla igen
//     (F13 — dialogen visas bara en gång)
//   * `setResult(Cancelled)` bort ur `abortJob` (F8, F9 — utfallet förblir Failed)
//   * `emptyRange`-provet bort ur `run` (F14 — en dialog öppnas eller en sida skickas)
// `needsFilePrompt` som ignorerar `promptsForFile` fälls av `print_gate` P16: här är
// skrivaren alltid en filskrivare.
//
// MÄTER INTE att papperet kommer ut ur en fysisk skrivare, XPS-skrivaren eller en PDF-
// skrivare med egen ruta, vår dialogs utseende (filedialog_gate) eller att Windows
// ruta kan fyllas i. Låsningen mellan provöppningen och `StartDoc` går inte att
// stänga helt och mäts inte.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include <glaze/glaze.h>

#include <windows.h>
#include <winspool.h>

#include "file_dialog_window.h"   // intern — vår spara-dialog

namespace {

namespace fs = std::filesystem;
namespace fd = glaze::filedlg;

int  g_fel     = 0;
int  g_fall    = 0;
int  g_skips   = 0;
bool g_verbose = false;

void check(bool ok, const std::string& vad, const std::string& fick = {})
{
    ++g_fall;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", vad.c_str());
        return;
    }
    ++g_fel;
    std::printf("  FEL  %s%s%s\n", vad.c_str(), fick.empty() ? "" : " — ", fick.c_str());
}

const wchar_t kSkrivareW[] = L"Microsoft Print to PDF";
const char    kSkrivare[]  = "Microsoft Print to PDF";

bool skrivarenFinns()
{
    std::wstring namn = kSkrivareW;   // Win32 tar en skrivbar sträng
    HANDLE       h    = nullptr;
    if (!OpenPrinterW(namn.data(), &h, nullptr))
        return false;
    ClosePrinter(h);
    return true;
}

// ------------------------------------------------------------------- vakten

std::atomic<int>  g_rutor{0};      // systemrutor som vakten sett
std::atomic<bool> g_stoppa{false};

std::string utf8(const wchar_t* w)
{
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1)
        return std::string();
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// Hur många gånger vakten sett varje ruta, så att en som inte gick att avbryta stängs.
std::unordered_map<HWND, int> g_sedda;

BOOL CALLBACK granskaRuta(HWND h, LPARAM)
{
    if (!IsWindowVisible(h))
        return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId())
        return TRUE;
    wchar_t klass[64] = {};
    GetClassNameW(h, klass, 64);
    if (std::wcscmp(klass, L"#32770") != 0)
        return TRUE;
    const int gång = ++g_sedda[h];
    if (gång == 1) {
        wchar_t titel[256] = {};
        GetWindowTextW(h, titel, 256);
        ++g_rutor;
        std::printf("  vakt: systemruta '%s' — avbryts\n", utf8(titel).c_str());
        PostMessageW(h, WM_COMMAND, IDCANCEL, 0);
    } else if (gång % 20 == 0) {
        PostMessageW(h, WM_CLOSE, 0, 0);   // den gick inte att avbryta
    }
    return TRUE;
}

void vakten()
{
    while (!g_stoppa) {
        EnumWindows(granskaRuta, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// ----------------------------------------------------------------- filerna

std::string läs(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void skriv(const fs::path& p, const std::string& s)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}

bool ärPdf(const fs::path& p)
{
    const std::string s = läs(p);
    return s.size() > 64 && s.compare(0, 4, "%PDF") == 0;
}

std::string u(const fs::path& p)
{
    return fd::utf8FromPath(p);
}

// ------------------------------------------------------------------- jobbet

class Värd : public glaze::AppWindow {
public:
    Värd()
    {
        setTitle("print_file_gate");
        setClientSize(240, 200);
    }
};

void inställ(glaze::PrintJob& j)
{
    j.setPrinter(kSkrivare);
    j.setPaper(glaze::PaperSize::A4);
    j.setResolution(72);   // små sidor — grinden mäter filerna, inte bilderna
    j.setTitle("print_file_gate");
}

const auto rita = [](int, glaze::Painter&) { return true; };

std::string filfelet()
{
    const glaze::TrText t = glaze::tr(glaze::str::PrintErrorFile);
    return t.str();
}

// ------------------------------------------------------------ vår dialog

glaze::FileDialogWindow* sparadialogen()
{
    for (glaze::AppWindow* w : glaze::app().windows())
        if (auto* d = dynamic_cast<glaze::FileDialogWindow*>(w); d && d->isOpen())
            return d;
    return nullptr;
}

struct Steg {
    std::function<void(glaze::FileDialogWindow&)> gör;
    bool                                          levande = true;
    int                                           försök  = 0;
};

// Postar `steg->gör` till pump-svansen; det körs när vår dialog är öppen. Ett steg som
// aldrig hittar den ger ett fel, och ett som `run` redan återvänt från (`levande` false)
// gör ingenting.
void postaSteg(const std::shared_ptr<Steg>& steg)
{
    glaze::app().post([steg] {
        if (!steg->levande)
            return;
        if (glaze::FileDialogWindow* d = sparadialogen()) {
            steg->levande = false;
            steg->gör(*d);
            return;
        }
        if (++steg->försök > 200) {
            steg->levande = false;
            check(false, "vår spara-dialog kom aldrig upp");
            return;
        }
        postaSteg(steg);
    });
}

std::shared_ptr<Steg> styr(std::function<void(glaze::FileDialogWindow&)> gör)
{
    auto steg = std::make_shared<Steg>();
    steg->gör = std::move(gör);
    postaSteg(steg);
    return steg;
}

// ------------------------------------------------------------------ fallen

// Systemrutor vakten sett sedan `före`.
int rutorSedan(int före)
{
    return g_rutor - före;
}

void fallF1F2F3(glaze::AppWindow& v, const fs::path& rot)
{
    {   // F1
        glaze::PrintJob j;
        inställ(j);
        const fs::path f = rot / "f1.pdf";
        j.setOutputFile(u(f));
        const int före = g_rutor;
        const bool ok  = j.run(v, 2, rita);
        check(ok && j.outcome() == glaze::PrintOutcome::Ok, "F1 Ok",
              std::to_string(static_cast<int>(j.outcome())));
        check(ärPdf(f), "F1 filen är en PDF");
        check(j.pagesPrinted() == 2, "F1 två sidor skickade", std::to_string(j.pagesPrinted()));
        check(j.promptsForFile(), "F1 skrivaren frågar efter fil");
        check(j.lastError().empty() && !j.isCancelled(), "F1 inget fel, inget avbrott");
        check(rutorSedan(före) == 0, "F1 ingen systemruta");
    }
    {   // F2
        glaze::PrintJob j;
        inställ(j);
        const fs::path mapp = rot / fd::pathFromUtf8("mapp åäö");
        fs::create_directories(mapp);
        const fs::path f = mapp / fd::pathFromUtf8("ritning åäö.pdf");
        j.setOutputFile(u(f));
        const int före = g_rutor;
        const bool ok  = j.run(v, 1, rita);
        check(ok && ärPdf(f), "F2 åäö i mappen och filnamnet");
        check(rutorSedan(före) == 0, "F2 ingen systemruta");
    }
    {   // F3
        glaze::PrintJob j;
        inställ(j);
        const fs::path f = rot / "f3.pdf";
        skriv(f, "gammal");
        j.setOutputFile(u(f));
        const int före = g_rutor;
        const bool ok  = j.run(v, 1, rita);
        check(ok && ärPdf(f), "F3 en befintlig fil ersätts");
        check(rutorSedan(före) == 0, "F3 ingen systemruta");
    }
}

// Ett jobb som ska ge FileFailed utan någon ruta, filen `f` satt av appen.
void filfel(glaze::AppWindow& v, const char* fall, const fs::path& f)
{
    glaze::PrintJob j;
    inställ(j);
    j.setOutputFile(u(f));
    const int         före = g_rutor;
    const bool        ok   = j.run(v, 1, rita);
    const std::string n    = fall;
    check(!ok && j.outcome() == glaze::PrintOutcome::FileFailed, n + " FileFailed",
          std::to_string(static_cast<int>(j.outcome())));
    check(j.lastError() == filfelet() && !j.lastError().empty(), n + " felbeskedet är filfelet",
          j.lastError());
    check(!j.isCancelled(), n + " inget avbrott");
    check(rutorSedan(före) == 0, n + " ingen systemruta");
}

void fallF4F5F6F7(glaze::AppWindow& v, const fs::path& rot)
{
    {   // F4
        const fs::path f = rot / "saknas" / "f4.pdf";
        filfel(v, "F4", f);
        check(!fs::exists(f.parent_path()), "F4 katalogen skapades inte");
    }
    {   // F5
        const fs::path f = rot / "f5.pdf";
        skriv(f, "gammal");
        SetFileAttributesW(f.c_str(), FILE_ATTRIBUTE_READONLY);
        filfel(v, "F5", f);
        SetFileAttributesW(f.c_str(), FILE_ATTRIBUTE_NORMAL);
        check(läs(f) == "gammal", "F5 den skrivskyddade filen är orörd");
    }
    {   // F6
        const fs::path d = rot / "f6dir";
        fs::create_directories(d);
        filfel(v, "F6", d);
        check(fs::is_directory(d), "F6 katalogen är kvar");
    }
    {   // F7
        const fs::path f = rot / "f7.pdf";
        skriv(f, "gammal");
        HANDLE lås = CreateFileW(f.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(lås != INVALID_HANDLE_VALUE, "F7 filen gick att låsa");
        filfel(v, "F7", f);
        if (lås != INVALID_HANDLE_VALUE)
            CloseHandle(lås);
        check(läs(f) == "gammal", "F7 den låsta filen är orörd");
    }
}

void fallF8F9(glaze::AppWindow& v, const fs::path& rot)
{
    {   // F8
        glaze::PrintJob j;
        inställ(j);
        const fs::path f = rot / "f8.pdf";
        j.setOutputFile(u(f));
        const int  före = g_rutor;
        const bool ok   = j.run(v, 3, [&](int, glaze::Painter&) {
            j.cancel();
            return true;
        });
        check(!ok && j.outcome() == glaze::PrintOutcome::Cancelled, "F8 Cancelled",
              std::to_string(static_cast<int>(j.outcome())));
        check(j.isCancelled() && j.lastError().empty(), "F8 avbrutet, felbeskedet tomt");
        check(!fs::exists(f), "F8 ingen ny fil");
        check(rutorSedan(före) == 0, "F8 ingen systemruta");
    }
    {   // F9
        glaze::PrintJob j;
        inställ(j);
        const fs::path f = rot / "f9.pdf";
        skriv(f, "gammal");
        j.setOutputFile(u(f));
        const bool ok = j.run(v, 3, [&](int, glaze::Painter&) {
            j.cancel();
            return true;
        });
        check(!ok && j.outcome() == glaze::PrintOutcome::Cancelled, "F9 Cancelled");
        check(läs(f) == "gammal", "F9 den befintliga filen är orörd");
    }
}

void fallF10F11(glaze::AppWindow& v, const fs::path& rot)
{
    glaze::PrintJob j;
    inställ(j);
    j.setFilePrompt(false);   // ingen fil och ingen fråga från oss: Windows frågar
    const int  före = g_rutor;
    const bool ok   = j.run(v, 1, rita);
    check(rutorSedan(före) >= 1, "F10 Windows ruta visades och vakten avbröt den");
    check(!ok && j.outcome() == glaze::PrintOutcome::Cancelled, "F10 Cancelled",
          std::to_string(static_cast<int>(j.outcome())));
    check(j.isCancelled() && j.lastError().empty(), "F10 avbrutet, felbeskedet tomt");

    const fs::path f = rot / "f11.pdf";   // samma jobb och samma DC efter ett avbrott
    j.setOutputFile(u(f));
    const int  före2 = g_rutor;
    const bool ok2   = j.run(v, 1, rita);
    check(ok2 && j.outcome() == glaze::PrintOutcome::Ok && ärPdf(f),
          "F11 samma jobb igen med en fil: Ok");
    check(!j.isCancelled() && j.lastError().empty(), "F11 avbrottet är borta");
    check(rutorSedan(före2) == 0, "F11 ingen systemruta");
}

void fallF12(glaze::AppWindow& v)
{
    glaze::PrintJob j;
    inställ(j);
    j.setTitle("Ritning F12");
    bool        anropad = false;
    std::string namnet;
    const int   före = g_rutor;
    auto        steg = styr([&](glaze::FileDialogWindow& d) {
        if (auto* n = d.find<glaze::TextInput>("name"))
            namnet = n->text();
        d.close();   // Avbryt
    });
    const bool ok = j.run(v, 1, [&](int, glaze::Painter&) {
        anropad = true;
        return true;
    });
    steg->levande = false;
    check(namnet == "Ritning F12.pdf", "F12 namnet ifyllt ur titeln med ändelsen", namnet);
    check(!ok && j.outcome() == glaze::PrintOutcome::Cancelled, "F12 Cancelled",
          std::to_string(static_cast<int>(j.outcome())));
    check(j.isCancelled() && j.lastError().empty(), "F12 avbrutet, felbeskedet tomt");
    check(!anropad && j.pagesPrinted() == 0 && j.outputFile().empty(),
          "F12 inget jobb startade och ingen fil är vald");
    check(rutorSedan(före) == 0, "F12 ingen systemruta");
}

void fallF13(glaze::AppWindow& v, const fs::path& rot)
{
    glaze::PrintJob j;
    inställ(j);
    j.setTitle("F13");
    const fs::path a = rot / "f13a.pdf";
    const fs::path b = rot / "f13b.pdf";
    int            dialoger = 0;
    const auto     svara    = [&](const fs::path& fil) {
        return styr([&, fil](glaze::FileDialogWindow& d) {
            ++dialoger;
            if (auto* n = d.find<glaze::TextInput>("name"))
                n->setText(u(fil));
            if (auto* ok = d.find<glaze::Button>("ok"))
                ok->performClick();
        });
    };
    const int före = g_rutor;

    auto       steg1 = svara(a);
    const bool ok1   = j.run(v, 1, rita);
    steg1->levande   = false;
    std::error_code ec;
    check(ok1 && j.outcome() == glaze::PrintOutcome::Ok && ärPdf(a),
          "F13 OK i vår dialog skriver filen",
          std::to_string(static_cast<int>(j.outcome())));
    check(fs::equivalent(fd::pathFromUtf8(j.outputFile()), a, ec),
          "F13 outputFile() går att läsa efter run", j.outputFile());

    auto       steg2 = svara(b);
    const bool ok2   = j.run(v, 1, rita);
    steg2->levande   = false;
    check(dialoger == 2, "F13 nästa run frågar igen", std::to_string(dialoger));
    check(ok2 && ärPdf(b), "F13 den andra filen skrivs");
    check(ärPdf(a), "F13 den första filen skrevs inte över");
    check(rutorSedan(före) == 0, "F13 ingen systemruta");
}

void fallF14(glaze::AppWindow& v, const fs::path& rot)
{
    {
        glaze::PrintJob j;
        inställ(j);
        j.setPageRange(5, 6);
        bool       anropad = false;
        const int  före    = g_rutor;
        const bool ok      = j.run(v, 3, [&](int, glaze::Painter&) {
            anropad = true;
            return true;
        });
        check(!ok && j.outcome() == glaze::PrintOutcome::NothingToPrint,
              "F14 intervallet 5–6 i tre sidor: NothingToPrint",
              std::to_string(static_cast<int>(j.outcome())));
        check(!j.isCancelled() && j.lastError().empty() && !anropad,
              "F14 inget avbrott, inget fel, ingen sida ritad");
        check(rutorSedan(före) == 0, "F14 ingen systemruta");
    }
    {
        glaze::PrintJob j;
        inställ(j);
        const fs::path f = rot / "f14.pdf";
        j.setOutputFile(u(f));
        j.setPageRange(2, 3);
        const bool ok = j.run(v, 0, [](int, glaze::Painter&) { return false; });
        check(!ok && j.outcome() == glaze::PrintOutcome::NothingToPrint,
              "F14 sida för sida: ingen ritad sida i intervallet",
              std::to_string(static_cast<int>(j.outcome())));
        check(!j.isCancelled() && j.lastError().empty() && j.pagesPrinted() == 0,
              "F14 inget avbrott, inget fel, ingen sida skickad");
        check(!fs::exists(f), "F14 ingen fil");
    }
}

void städa(const fs::path& rot)
{
    std::error_code ec;
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(rot, ec))
        SetFileAttributesW(e.path().c_str(), FILE_ATTRIBUTE_NORMAL);
    fs::remove_all(rot, ec);
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== print_file_gate — utskrift till fil (Microsoft Print to PDF) ===\n");
    if (!skrivarenFinns()) {
        std::printf("=== print_file_gate: hoppades över (%s saknas) ===\n", kSkrivare);
        return 77;
    }

    glaze::setLanguage("sv");
    const fs::path rot = fs::temp_directory_path() /
                         ("glaze_print_file_gate_" + std::to_string(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(rot, ec);
    fs::create_directories(rot, ec);
    if (!fs::is_directory(rot)) {
        std::printf("=== print_file_gate: den temporära katalogen gick inte att skapa ===\n");
        return 1;
    }

    std::thread vakt(vakten);
    Värd        v;
    v.setOffscreen(true);
    bool kedjan = false;
    glaze::app().post([&] {
        kedjan = true;
        if (!v.isOpen()) {
            std::printf("  --   ägaren kom aldrig upp\n");
            ++g_skips;
            v.close();
            return;
        }
        fallF1F2F3(v, rot);
        fallF4F5F6F7(v, rot);
        fallF8F9(v, rot);
        fallF10F11(v, rot);
        fallF12(v);
        fallF13(v, rot);
        fallF14(v, rot);
        v.close();
    });
    v.run();
    g_stoppa = true;
    vakt.join();
    städa(rot);
    if (!kedjan)
        ++g_skips;

    if (g_skips > 0 && g_fall == 0) {
        std::printf("=== print_file_gate: hoppades över (ingen display) ===\n");
        return 77;
    }
    std::printf(g_fel ? "=== print_file_gate: %d FEL av %d ===\n"
                      : "=== print_file_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    return g_fel ? 1 : 0;
}
