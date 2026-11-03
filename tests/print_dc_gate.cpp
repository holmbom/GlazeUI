// print_dc_gate — SKRIVARENS DC EFTER 3D-PROGRAMMEN.
//
// Kör:  ./build/tests/print_dc_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = skrivaren finns inte
//
// `D3DCompile` kan lämna tråden som kompilerade i ett tillstånd där
// skrivardrivrutinens `CreateDC` sedan svarar NULL, fel 995. `compileHlsl` kör
// därför kompilatorn på en egen tråd. Grinden gör det en app gör: ritar ett
// punktmoln på en rityta på sin enda tråd, vilket kompilerar punkt- och
// meshprogrammen, och skapar SEDAN en skrivar-DC på samma tråd.
//
// ORDNINGEN ÄR HELA FALLET: en `CreateDC` som lyckats före kompileringen gör att
// senare anrop också lyckas. Därför en egen process, och därför rörs ingen
// skrivare före kompileringen — inte heller frågan om skrivaren finns.
//
// VAD GRINDEN PÅSTÅR:
//   D1  ritningen kompilerade program (annars mäter resten ingenting)
//   D2  `CreateDCW` på samma tråd ger en DC
//   D3  `gfx::openPrinter` ger måtten och skrivarens tillstånd
//   Sabotage som SKA fälla dem: `std::thread(kompilera).join()` ersatt med
//   `kompilera()` i `compileHlsl` (D2 och D3 — NULL, fel 995), punktmolnet bort ur
//   grinden (D1).
//   MÄTER INTE systemets dialog (`PrintDlgEx` går inte att klicka i), andra
//   skrivare än Microsoft Print to PDF eller ett jobb — `StartDoc` på den skrivaren
//   frågar efter ett filnamn.

#include <cstdio>
#include <cstring>
#include <string>

#include <glaze/glaze.h>

#include <windows.h>
#include <winspool.h>

namespace {

int  g_fel     = 0;
int  g_fall    = 0;
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

class Värd : public glaze::AppWindow {
public:
    Värd()
    {
        setTitle("print_dc_gate");
        setClientSize(240, 200);
        yta.name = "yta";
        yta.dock = glaze::Dock::Fill;
        root().adopt(yta);
    }
    glaze::Canvas yta;
};

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;
    std::printf("=== print_dc_gate — skrivarens DC efter 3D-programmen ===\n");

    Värd v;
    v.setOffscreen(true);
    v.yta.addShape(glaze::Shape::points3({{0, 0, 0}}, {.size = glaze::px(14.0f)}));
    v.show();
    if (!v.isOpen()) {
        std::printf("=== print_dc_gate: fönstret öppnades inte ===\n");
        return 1;
    }
    v.window().renderFrame();
    const auto byggda = v.window().renderer().stats().programsBuilt;
    check(byggda > 0, "D1 ritningen kompilerade program", std::to_string(byggda));

    SetLastError(0);
    const HDC   dc  = CreateDCW(L"WINSPOOL", kSkrivareW, nullptr, nullptr);
    const DWORD fel = GetLastError();
    if (!dc && !skrivarenFinns()) {
        std::printf("=== print_dc_gate: hoppades över (%s saknas) ===\n", kSkrivare);
        return 77;
    }
    check(dc != nullptr, "D2 CreateDC på tråden som ritade", "fel " + std::to_string(fel));
    if (dc)
        DeleteDC(dc);

    gfx::PrintSettings s;
    s.printer = kSkrivare;
    check(gfx::openPrinter(s) && s.native && s.dpiX > 0.0f,
          "D3 gfx::openPrinter på tråden som ritade");

    std::printf(g_fel ? "=== print_dc_gate: %d FEL av %d ===\n"
                      : "=== print_dc_gate: allt grönt (%d fel av %d) ===\n",
                g_fel, g_fall);
    return g_fel ? 1 : 0;
}
