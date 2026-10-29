// filedialog_gate — FILVÄLJARDIALOGENS grind.
//
// Kör:  ./build/tests/filedialog_gate [-v]
// Exit: 0 = alla fall gröna · 1 = minst ett föll · 77 = fönsterdelen kunde
//       inte köras (ingen display) — ctest räknar den som SKIPPED, aldrig grön
//
// TRE DELAR:
//
//   HEADLESS      den rena std-modellen (`src/file_dialog_model.h`):
//                 FILTREN (mönster, jokertecken, skiftläge, "*.*"),
//                 ÄNDELSEN ett sparat namn får, NAMNFÄLTETS citatform,
//                 NAMNORDNINGEN (naturlig, vikt), STORLEKSTEXTEN per
//                 decimaltecken, KATALOGLÄSNINGEN i ett träd grinden SJÄLV
//                 skapar (mappar först, dolda namn borta, filtret gäller bara
//                 filer, åäö genom hela vägen), DOLDA POSTER med `showHidden`
//                 (H10: punktnamnen och det predikatet döljer syns,
//                 predikatet frågas inte, filtret gäller ändå, mappläget),
//                 FELEN (saknad mapp, en fil som
//                 mapp, ingen behörighet — på riktigt där det går), SÖKVÄGARNA
//                 (inskrivna, relativa, "..", närmaste befintliga, och på
//                 Windows UNC-resursen som rot) och
//                 SORTERINGSNYCKLARNA (storlek som tal, datum i tid, mappar
//                 först i varje kolumn).
//   FASADEN       `gfx::knownPlaces()` mot kontraktet i gfx/platform.h:
//                 hemmet först, varje användarmapp finns och är en katalog,
//                 minst en volym, och volymen som bär hemmet finns bland dem
//                 (K1–K3). På Linux dessutom XDG-filen på riktigt, i ett eget
//                 `$XDG_CONFIG_HOME` (K5). `gfx::isHiddenFile` (K4): på
//                 Windows ett riktigt HIDDEN-attribut på en åäö-fil, och att
//                 listningen då döljer den.
//   MED FÖNSTER   dialogen DOLD (`setOffscreen`) i en subklass som når
//                 fönstrets RIKTIGA ingång (`AppWindow::handleEvent`,
//                 protected — produktytan), som en kedja av postade steg:
//                 dialogen är modal och pumpar nästlat, så stegen körs INNE i
//                 dess pump. Mäter navigeringen (Enter och dubbelklick på en
//                 mapp, Backspace uppåt med mappen man kom ifrån vald,
//                 adressfältet, en plats), valet och OK (sökvägen i UTF-8),
//                 Escape, sparandets ändelse, ÖVERSKRIVNINGSFRÅGAN (den riktiga
//                 meddelanderutan, besvarad Nej och sedan Ja), mappläget,
//                 tabbordningen, filtypsbytet, flervalet, en mapp som tas bort
//                 under öppen dialog (F5) och språkbytet — och KRYSSRUTAN FÖR
//                 DOLDA FILER (W17–W19): ett klick genom ingången
//                 byter listan direkt åt båda hållen, valet står i
//                 inställningarna med en gång och kvar efter Avbryt,
//                 egenskapen IN kryssar rutan, flervalet med en dold fil, den
//                 dolda mappen i mappläget, och på Windows en fil med ett
//                 RIKTIGT HIDDEN-attribut.
//
// SABOTAGEN den fäller (ett i taget):
//   * mapparna inte först i `listFolder` (H6)
//   * de dolda punktnamnen med i listan (H6)
//   * filtret tillämpat också på mappar (H6)
//   * `withExtension` borttagen ur sparandet (W6)
//   * ett sparnamn som MED ändelsen är en mapp blir frågan och svaret i
//     stället för att öppnas (W6d)
//   * överskrivningsfrågan överhoppad (W7 — dialogen stänger utan fråga)
//   * Backspace som inte går uppåt (W2)
//   * storleksnyckeln utan storleken (bara namnet) i `sortKeys` (H9)
//   * sorteringsnycklarna aldrig satta i listan — storleken sorteras då som
//     text och mapparna hamnar bland filerna (W1, W16)
//   * UNC-roten aldrig igenkänd, UNC-grenen i `resolvePath` borttagen, eller
//     ".." som inte stannar vid resursen (H8, bara Windows)
//   och för de DOLDA FILERNA:
//   * `listFolder` som struntar i `showHidden` (H10, W17, W18, W19)
//   * kryssrutans `onChange` aldrig kopplad (W17, W19)
//   * växlingen som skriver inställningen men inte läser om listan (W17, W19)
//   * växlingen som läser om listan men inte skriver inställningen (W17)
//   * konstruktorn som inte läser `showHidden()` (W18)
//   * `showHidden` som visar punktnamnen men fortfarande frågar OS:et —
//     predikatet (H10) och, på Windows, HIDDEN-attributet (W17)
//
// VAD DEN INTE MÄTER, så anspråket inte överdrivs:
//   * `FileDialog::run` — tre rader runt fönstret (bygg, `run(ägare)`, svaret
//     ur `accepted()`). Grinden bygger fönstret själv för att kunna visa det
//     DOLT och nå `handleEvent`; filväljarfältets grind kör den
//     riktiga vägen genom `run`.
//   * hur dialogen SER UT — renderad och granskad i alla tre UI, ljust och
//     mörkt, och på finska (de längsta texterna).
//   * meddelanderutan är ett RIKTIGT fönster som syns ett ögonblick: den
//     öppnas av dialogens egen OK-väg och kan inte göras dold utifrån.
//     Knappen besvaras genom sin publika `performClick` —
//     `onClick` och knappens `dialogResult`, det ett klick gör — men utan
//     rutans egen musväg. Rutans svar bor i knapparnas `dialogResult`, så
//     en väg genom `onClick` ensam svarar inte alls.
//   * Linux-volymerna under /media och /run/media (`/proc/self/mounts`):
//     grinden kan inte montera något, så bara roten "/" mäts där.
//   * macOS — hela fasaddelen (NSFileManager, /Volumes, UF_HIDDEN) provas inte här;
//     den står i doc/os/macos/TESTS.md; att kryssrutan där tar fram
//     det UF_HIDDEN döljer (`~/Library`) likaså. Linux har inget attribut —
//     där mäter W17 bara punktnamnen.
//   * Windows "ingen behörighet": libstdc++ svarar EINVAL för en mapp man
//     inte får lista, så den meningen visas bara på Linux/macOS (H7 mäter
//     att Windows då säger "kunde inte läsas" och aldrig "finns inte").
//   * en RIKTIG nätverksresurs: UNC-formen mäts bara lexikalt (H8) — en
//     resurs att lista finns inte på varje maskin. Prövad för hand mot
//     \\localhost\C$ (listning, förälder, närmaste befintliga).

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gfx/platform.h>

#include <glaze/glaze.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>   // SetFileAttributesW — K4 sätter ett dolt-attribut
#endif

#include "file_dialog_model.h"    // den rena modellen (intern)
#include "file_dialog_window.h"   // fönstret (internt)

namespace fs = std::filesystem;
namespace fd = glaze::filedlg;

namespace {

int  g_fails   = 0;
int  g_cases   = 0;
int  g_skips   = 0;
bool g_verbose = false;

void check(bool ok, const std::string& what, const std::string& fick = "")
{
    ++g_cases;
    if (ok) {
        if (g_verbose)
            std::printf("  ok   %s\n", what.c_str());
        return;
    }
    std::printf("  FEL  %s%s\n", what.c_str(),
                fick.empty() ? "" : (" — fick " + fick).c_str());
    ++g_fails;
}

std::string lista(const std::vector<std::string>& v)
{
    std::string s = "[";
    for (const std::string& x : v)
        s += (s.size() > 1 ? "|" : "") + x;
    return s + "]";
}

// ------------------------------------------------------------ testträdet

fs::path u8(const std::string& s)
{
    return fd::pathFromUtf8(s);
}

void fil(const fs::path& p, std::size_t bytes)
{
    std::ofstream f(p, std::ios::binary);
    f << std::string(bytes, 'x');
}

// Ett eget träd i temp-katalogen, rivet i destruktorn:
//
//   <rot>/
//     Mapp A/            (en fil inuti: inne.txt)
//     mapp b/
//     Ärenden/
//     .dold/             (dold mapp)
//     .doldfil           (dold fil)
//     bild.PNG           2 000 byte
//     fil10.txt          10 byte
//     fil2.txt           20 000 byte
//     rapport.pdf        5 byte
//     översikt å.png     300 byte
struct Trad {
    fs::path rot;
    Trad()
    {
        const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
        rot = fs::temp_directory_path() /
              u8("filedialog_gate_" + std::to_string(t));
        fs::create_directories(rot / u8("Mapp A"));
        fs::create_directories(rot / u8("mapp b"));
        fs::create_directories(rot / u8("Ärenden"));
        fs::create_directories(rot / u8(".dold"));
        fil(rot / u8("Mapp A") / u8("inne.txt"), 1);
        fil(rot / u8(".doldfil"), 1);
        fil(rot / u8("bild.PNG"), 2000);
        fil(rot / u8("fil10.txt"), 10);
        fil(rot / u8("fil2.txt"), 20000);
        fil(rot / u8("rapport.pdf"), 5);
        fil(rot / u8("översikt å.png"), 300);
    }
    ~Trad()
    {
        std::error_code ec;
        fs::remove_all(rot, ec);
    }
    std::string utf8() const { return fd::utf8FromPath(rot); }
};

std::vector<std::string> namnen(const fd::Listing& l)
{
    std::vector<std::string> v;
    for (const fd::Entry& e : l.entries)
        v.push_back(e.name);
    return v;
}

// ================================================================ HEADLESS

void fallFilter()
{
    check(fd::splitPatterns(" *.png ; *.JPG;;") ==
              std::vector<std::string>{"*.png", "*.JPG"},
          "H1: splitPatterns tar bort blanktecken och tomma mönster",
          lista(fd::splitPatterns(" *.png ; *.JPG;;")));
    struct Fall {
        const char* namn;
        const char* mönster;
        bool        träff;
    } fall[] = {
        {"bild.png", "*.png", true},
        {"BILD.PNG", "*.png", true},          // skiftläge vikt (ASCII)
        {"bild.png.bak", "*.png", false},
        {"README", "*.*", true},              // "*.*" träffar allt
        {"README", "*", true},
        {"a.txt", "?.txt", true},
        {"å.txt", "?.txt", true},             // ? är ett TECKEN, inte en byte
        {"ab.txt", "?.txt", false},
        {"rapport2026.pdf", "rapport*.pdf", true},
        {"rapport.pdf", "rapport*.pdf", true},
        {"xrapport.pdf", "rapport*.pdf", false},
    };
    for (const Fall& f : fall)
        check(fd::globMatch(f.namn, f.mönster) == f.träff,
              std::string("H1: globMatch(\"") + f.namn + "\", \"" + f.mönster +
                  "\") = " + (f.träff ? "sant" : "falskt"));
    check(fd::matchesAny("x.jpg", {"*.png", "*.jpg"}) &&
              !fd::matchesAny("x.gif", {"*.png", "*.jpg"}) &&
              fd::matchesAny("x.gif", {}),
          "H1: matchesAny — något av mönstren, och en tom lista släpper allt");
}

void fallAndelsen()
{
    check(fd::defaultExtension({"*.pdf", "*.txt"}) == ".pdf",
          "H2: ändelsen ur första mönstret", fd::defaultExtension({"*.pdf"}));
    check(fd::defaultExtension({"*.*"}).empty() &&
              fd::defaultExtension({"rapport*.txt"}).empty() &&
              fd::defaultExtension({"*.p?f"}).empty() &&
              fd::defaultExtension({}).empty(),
          "H2: ingen ändelse ur \"*.*\", ett namnmönster eller jokertecken");
    check(fd::withExtension("rapport", ".pdf") == "rapport.pdf" &&
              fd::withExtension("rapport.txt", ".pdf") == "rapport.txt" &&
              fd::withExtension("rapport.", ".pdf") == "rapport.pdf" &&
              fd::withExtension(".bashrc", ".txt") == ".bashrc.txt" &&
              fd::withExtension("rapport", "") == "rapport",
          "H2: withExtension bara när namnet saknar en egen ändelse");
    check(fd::extensionOf("bild.png") == "png" && fd::extensionOf(".bashrc").empty() &&
              fd::extensionOf("x.").empty() && fd::extensionOf("README").empty(),
          "H2: extensionOf — en punkt FÖRST är inget ändelsetecken");
}

void fallNamnfaltet()
{
    check(fd::splitNames("  rapport.pdf ") == std::vector<std::string>{"rapport.pdf"},
          "H3: ett namn utan citat");
    check(fd::splitNames("\"a b.txt\" \"åäö.png\"") ==
              std::vector<std::string>{"a b.txt", "åäö.png"},
          "H3: två citerade namn (blanksteg och åäö inuti)",
          lista(fd::splitNames("\"a b.txt\" \"åäö.png\"")));
    check(fd::splitNames("en \"citerad\" sak") ==
              std::vector<std::string>{"en \"citerad\" sak"},
          "H3: ett fält som inte BÖRJAR med citat är ett namn");
    check(fd::splitNames("\"a.txt\" b.txt").size() == 2 &&
              fd::splitNames("\"oavslutat").size() == 1 &&
              fd::splitNames("   ").empty(),
          "H3: text utanför citat, ett oavslutat citat och ett tomt fält");
    const std::vector<std::string> två{"a.txt", "b c.txt"};
    check(fd::joinNames(två) == "\"a.txt\" \"b c.txt\"" &&
              fd::splitNames(fd::joinNames(två)) == två &&
              fd::joinNames({"ensam.txt"}) == "ensam.txt",
          "H3: joinNames tur och retur");
}

void fallOrdningen()
{
    check(fd::compareNames("fil2", "fil10") < 0 &&
              fd::compareNames("fil10", "fil2") > 0,
          "H4: naturlig ordning — fil2 före fil10");
    check(fd::compareNames("Bild", "bild") != 0 &&
              fd::compareNames("apa", "Bepa") < 0 &&
              fd::compareNames("Ärende", "är") > 0,
          "H4: skiftläget vikt (ASCII och Latin-1), ordningen ändå total");
    check(fd::compareNames("fil007", "fil7") != 0 &&
              fd::compareNames("fil007", "fil8") < 0,
          "H4: inledande nollor räknas inte i talet");
}

void fallStorleken()
{
    struct Fall {
        std::uintmax_t b;
        const char*    sv;
        const char*    en;
    } fall[] = {
        {0, "0 B", "0 B"},
        {999, "999 B", "999 B"},
        {1000, "1,0 kB", "1.0 kB"},
        {1234, "1,2 kB", "1.2 kB"},
        {12345, "12 kB", "12 kB"},
        {999949, "1,0 MB", "1.0 MB"},   // 999,9 kB avrundas till nästa enhet
        {3456789, "3,5 MB", "3.5 MB"},
        {98765432, "99 MB", "99 MB"},
    };
    for (const Fall& f : fall) {
        check(fd::sizeText(f.b, ",", "B") == f.sv,
              "H5: " + std::to_string(f.b) + " byte = \"" + f.sv + "\"",
              fd::sizeText(f.b, ",", "B"));
        check(fd::sizeText(f.b, ".", "B") == f.en,
              "H5: med punkt = \"" + std::string(f.en) + "\"",
              fd::sizeText(f.b, ".", "B"));
    }
    // BYTE-ENHETEN är språkets: finskan skriver tavu, "t" — både
    // ensam och efter SI-prefixet, som Windows och GNOME gör på finska.
    check(fd::sizeText(512, ",", "t") == "512 t" &&
              fd::sizeText(1234, ",", "t") == "1,2 kt" &&
              fd::sizeText(3456789, ",", "t") == "3,5 Mt",
          "H5: finskans tavu — \"512 t\", \"1,2 kt\", \"3,5 Mt\"",
          fd::sizeText(1234, ",", "t"));
    const std::string d = fd::dateText(1791427273);
    check(d.size() == 16 && d[4] == '-' && d[7] == '-' && d[10] == ' ' &&
              d[13] == ':',
          "H5: dateText i formen ÅÅÅÅ-MM-DD hh:mm", d);
}

void fallKatalogen(const Trad& t)
{
    const fd::Listing alla = fd::listFolder(t.rot, false, {});
    check(alla.error == fd::ListError::None, "H6: trädet går att läsa");
    const std::vector<std::string> väntat{"Mapp A",   "mapp b",      "Ärenden",
                                          "bild.PNG", "fil2.txt",    "fil10.txt",
                                          "rapport.pdf", "översikt å.png"};
    check(namnen(alla) == väntat,
          "H6: mapparna först, båda i namnordning, de dolda borta",
          lista(namnen(alla)));
    check(!alla.entries.empty() && alla.entries[0].folder &&
              !alla.entries[3].folder,
          "H6: mapp-flaggan");
    bool storlek = false, tid = true;
    for (const fd::Entry& e : alla.entries) {
        if (e.name == "fil2.txt")
            storlek = e.hasSize && e.size == 20000;
        tid = tid && e.hasTime;
    }
    check(storlek && tid, "H6: storleken i byte, och en tid på varje rad");

    const fd::Listing bilder = fd::listFolder(t.rot, false, {"*.png"});
    check(namnen(bilder) == std::vector<std::string>{"Mapp A", "mapp b",
                                                     "Ärenden", "bild.PNG",
                                                     "översikt å.png"},
          "H6: filtret gäller FILER (skiftlägesfritt), aldrig mappar",
          lista(namnen(bilder)));
    const fd::Listing mappar = fd::listFolder(t.rot, true, {"*.png"});
    check(namnen(mappar) == std::vector<std::string>{"Mapp A", "mapp b",
                                                     "Ärenden"},
          "H6: mappläget visar bara mappar", lista(namnen(mappar)));
    // Det OS:et döljer (predikatet — dialogen skickar `gfx::isHiddenFile`)
    // försvinner också; sökvägen kommer in i UTF-8.
    const fd::Listing utan = fd::listFolder(
        t.rot, false, {}, [](const std::string& p) {
            return p.size() >= 8 && p.compare(p.size() - 8, 8, "bild.PNG") == 0;
        });
    check(namnen(utan) == std::vector<std::string>{"Mapp A", "mapp b", "Ärenden",
                                                   "fil2.txt", "fil10.txt",
                                                   "rapport.pdf", "översikt å.png"},
          "H6: det dold-predikatet svarar ja på syns inte", lista(namnen(utan)));
    // UTF-8 hela vägen: namnet UR LISTNINGEN pekar ut samma fil igen.
    bool tur = false;
    for (const fd::Entry& e : alla.entries) {
        std::error_code ec;
        if (e.name == "översikt å.png")
            tur = fs::is_regular_file(t.rot / fd::pathFromUtf8(e.name), ec) &&
                  fs::file_size(t.rot / fd::pathFromUtf8(e.name), ec) == 300;
    }
    check(tur, "H6: åäö tur och retur — listningens namn är filens namn");
}

// H10: DOLDA POSTER — `showHidden` visar punktnamnen OCH det
// predikatet hade dolt, och frågar då inte predikatet alls (på Windows är
// varje fråga ett filsystemsanrop). Räknaren är global: predikatet är en
// funktionspekare och kan inte fånga något.
int g_doldFrågor = 0;

void fallDoldaPoster(const Trad& t)
{
    const fd::Listing alla = fd::listFolder(t.rot, false, {}, nullptr, true);
    check(namnen(alla) == std::vector<std::string>{".dold", "Mapp A", "mapp b",
                                                   "Ärenden", ".doldfil",
                                                   "bild.PNG", "fil2.txt",
                                                   "fil10.txt", "rapport.pdf",
                                                   "översikt å.png"},
          "H10: showHidden — punktnamnen syns, mappar först, i namnordning",
          lista(namnen(alla)));
    g_doldFrågor = 0;
    const fd::Listing med = fd::listFolder(
        t.rot, false, {},
        [](const std::string& p) {
            ++g_doldFrågor;
            return p.size() >= 8 && p.compare(p.size() - 8, 8, "bild.PNG") == 0;
        },
        true);
    check(namnen(med) == namnen(alla) && g_doldFrågor == 0,
          "H10: showHidden — det predikatet hade dolt syns, och predikatet "
          "frågas aldrig",
          std::to_string(g_doldFrågor) + " frågor");
    check(namnen(fd::listFolder(t.rot, false, {"*.png"}, nullptr, true)) ==
              std::vector<std::string>{".dold", "Mapp A", "mapp b", "Ärenden",
                                       "bild.PNG", "översikt å.png"},
          "H10: filtret gäller en dold fil som en annan (\".doldfil\" borta), "
          "aldrig en mapp",
          lista(namnen(fd::listFolder(t.rot, false, {"*.png"}, nullptr, true))));
    check(namnen(fd::listFolder(t.rot, true, {}, nullptr, true)) ==
              std::vector<std::string>{".dold", "Mapp A", "mapp b", "Ärenden"},
          "H10: mappläget med showHidden — den dolda mappen med");
    check(namnen(fd::listFolder(t.rot, false, {}, nullptr, false)) ==
              std::vector<std::string>{"Mapp A", "mapp b", "Ärenden", "bild.PNG",
                                       "fil2.txt", "fil10.txt", "rapport.pdf",
                                       "översikt å.png"},
          "H10: showHidden = false är förvalet — samma lista som utan argumentet");
}

void fallFelen(const Trad& t)
{
    const fd::Listing borta = fd::listFolder(t.rot / u8("finns inte"), false, {});
    check(borta.error == fd::ListError::Missing && borta.entries.empty(),
          "H7: en saknad mapp = Missing, inga rader");
    const fd::Listing fil = fd::listFolder(t.rot / u8("rapport.pdf"), false, {});
    check(fil.error == fd::ListError::Missing,
          "H7: en FIL som mapp = Missing (ingen mapp)");
    check(fd::classify(std::make_error_code(std::errc::permission_denied)) ==
                  fd::ListError::NoAccess &&
              fd::classify(std::make_error_code(std::errc::operation_not_permitted)) ==
                  fd::ListError::NoAccess &&
              fd::classify(std::make_error_code(std::errc::not_a_directory)) ==
                  fd::ListError::Missing &&
              fd::classify(std::make_error_code(std::errc::io_error)) ==
                  fd::ListError::Unreadable &&
              fd::classify(std::error_code()) == fd::ListError::None,
          "H7: classify — behörighet, saknad, oläsbar, inget fel");
#ifdef _WIN32
    // PÅ RIKTIGT: profilens gamla kompatibilitetskorsningar ("Mina dokument",
    // "My Documents" … — namnet är systemspråkets) FINNS men får inte
    // listas (libstdc++ svarar EINVAL), och dialogen får aldrig
    // säga "finns inte" om dem. Grinden letar upp dem själv i stället för
    // att gissa ett namn.
    const char* hem = std::getenv("USERPROFILE");
    int         låsta = 0, rätt = 0;
    std::error_code e;
    if (hem && *hem) {
        for (const fs::directory_entry& d :
             fs::directory_iterator(fd::pathFromUtf8(hem), e)) {
            std::error_code f;
            if (!d.is_directory(f))
                continue;
            std::error_code g;
            fs::directory_iterator prov(d.path(), g);
            if (!g)
                continue;   // gick att lista — inget fall
            ++låsta;
            const fd::Listing l = fd::listFolder(d.path(), false, {});
            rätt += (l.error == fd::ListError::Unreadable ||
                     l.error == fd::ListError::NoAccess);
        }
    }
    if (låsta > 0)
        check(rätt == låsta,
              "H7: en mapp i profilen som inte får listas = oläsbar/ingen "
              "behörighet, aldrig \"finns inte\"",
              std::to_string(rätt) + " av " + std::to_string(låsta));
    else
        std::printf("  --   H7: profilen har ingen mapp som inte får listas — "
                    "det fallet hoppas över\n");
#else
    // PÅ RIKTIGT: en mapp utan läsrätt (fungerar inte som root, som läser
    // ändå — då hoppas fallet över med en rad).
    const fs::path låst = t.rot / u8("låst");
    fs::create_directories(låst);
    std::error_code e;
    fs::permissions(låst, fs::perms::none, e);
    std::error_code test;
    fs::directory_iterator prov(låst, test);
    if (test) {
        const fd::Listing l = fd::listFolder(låst, false, {});
        check(l.error == fd::ListError::NoAccess,
              "H7: en mapp utan läsrätt = NoAccess",
              std::to_string(static_cast<int>(l.error)));
    } else {
        std::printf("  --   H7: processen läser en mapp utan läsrätt (root?) — "
                    "det fallet hoppas över\n");
    }
    // Bort igen — fönsterfallen räknar trädets mappar.
    fs::permissions(låst, fs::perms::owner_all, e);
    fs::remove(låst, e);
#endif
}

void fallSokvagarna(const Trad& t)
{
    const fs::path a = t.rot / u8("Mapp A");
    check(fd::resolveTyped(t.rot, "Mapp A") == a, "H8: ett relativt namn");
    check(fd::resolveTyped(a, "..") == t.rot &&
              fd::resolveTyped(a, "../mapp b/") == t.rot / u8("mapp b"),
          "H8: \"..\" och en avslutande avskiljare löses lexikalt");
    check(fd::resolveTyped(a, fd::utf8FromPath(t.rot)) == t.rot,
          "H8: en absolut sökväg står kvar");
    check(fd::nearestExisting(a / u8("borta") / u8("djupare")) == a,
          "H8: närmaste befintliga förälder");
    check(fd::parentFolder(a) == t.rot && fd::parentFolder(a / u8("")) == t.rot,
          "H8: parentFolder, också med avslutande avskiljare");
    const fs::path rot = t.rot.root_path();
    check(fd::isRootFolder(rot) && !fd::isRootFolder(t.rot) &&
              fd::parentFolder(rot) == rot,
          "H8: roten har ingen förälder", fd::utf8FromPath(rot));
#ifdef _WIN32
    // UNC — rent LEXIKALT, inget här frågar nätet. libstdc++ ger
    // "\\server\resurs" inget rotnamn och kallar den relativ, så
    // varje rad här fäller en modell som litar på std för formen. Jämförelsen
    // är TEXTEN (`native()`): libstdc++:s `==` håller "\server" och
    // "\\server" för lika, och just den skillnaden är vad fallet mäter.
    const fs::path resurs = fd::pathFromUtf8(R"(\\server\resurs)");
    const fs::path under  = fd::pathFromUtf8(R"(\\server\resurs\a\b)");
    check(fd::isRootFolder(resurs) &&
              fd::isRootFolder(fd::pathFromUtf8(R"(\\server\resurs\)")) &&
              fd::isRootFolder(fd::pathFromUtf8(R"(\\server)")) &&
              fd::parentFolder(resurs).native() == resurs.native() &&
              !fd::isRootFolder(under) &&
              fd::parentFolder(under).native() ==
                  fd::pathFromUtf8(R"(\\server\resurs\a)").native(),
          "H8: en UNC-resurs (\\\\server\\resurs) är en rot");
    const fs::path inskriven =
        fd::resolveTyped(t.rot, R"( \\server\resurs\a\..\b\ )");
    check(inskriven.native() == fd::pathFromUtf8(R"(\\server\resurs\b)").native(),
          "H8: en inskriven UNC-sökväg är absolut och behåller sin rot",
          fd::utf8FromPath(inskriven));
    const fs::path uppåt = fd::resolveTyped(under, R"(..\..\..\..)");
    check(uppåt.native() == resurs.native() &&
              fd::resolveTyped(under, "c").native() ==
                  fd::pathFromUtf8(R"(\\server\resurs\a\b\c)").native(),
          "H8: \"..\" stannar vid resursen, ett namn läggs till",
          fd::utf8FromPath(uppåt));
#endif
}

void fallNycklarna()
{
    // Datan är vald så att NAMN-, STORLEKS-, TIDS- och TYPORDNINGEN alla
    // skiljer sig åt — annars mäter en kolumn bara att namnet råkar sortera
    // likadant (grindens första utkast föll i den fällan).
    std::vector<fd::Entry> e(5);
    e[0] = {"zeta", true, 0, false, 300, true};      // mapp
    e[1] = {"alfa", true, 0, false, 100, true};      // mapp
    e[2] = {"a-stor.txt", false, 10000, true, 50, true};
    e[3] = {"b-liten.txt", false, 9, true, 400, true};
    e[4] = {"c-mellan.png", false, 100, true, 200, true};
    const std::vector<std::string> typer{"Filmapp", "Filmapp", "TXT-fil",
                                         "TXT-fil", "PNG-fil"};
    const auto k = fd::sortKeys(e, typer);
    auto ordning = [&](int kol) {
        std::vector<std::string> v(e.size());
        for (std::size_t i = 0; i < e.size(); ++i)
            v[std::size_t(k[i][std::size_t(kol)])] = e[i].name;
        return v;
    };
    check(ordning(fd::ColName) ==
              std::vector<std::string>{"alfa", "zeta", "a-stor.txt",
                                       "b-liten.txt", "c-mellan.png"},
          "H9: namn — mappar först, sedan namnordning", lista(ordning(fd::ColName)));
    check(ordning(fd::ColSize) ==
              std::vector<std::string>{"alfa", "zeta", "b-liten.txt",
                                       "c-mellan.png", "a-stor.txt"},
          "H9: storlek som TAL (9 < 100 < 10 000 byte), mappar först",
          lista(ordning(fd::ColSize)));
    check(ordning(fd::ColModified) ==
              std::vector<std::string>{"alfa", "zeta", "a-stor.txt",
                                       "c-mellan.png", "b-liten.txt"},
          "H9: ändrad i TID, mappar först (sinsemellan i tid)",
          lista(ordning(fd::ColModified)));
    check(ordning(fd::ColType) ==
              std::vector<std::string>{"alfa", "zeta", "c-mellan.png",
                                       "a-stor.txt", "b-liten.txt"},
          "H9: typ — PNG före TXT, lika typer i namnordning",
          lista(ordning(fd::ColType)));
}

// ---------------------------------------------------------- KÄNDA PLATSER

void fallPlatserna()
{
    const std::vector<gfx::Place> p = gfx::knownPlaces();
    check(!p.empty() && p.front().kind == gfx::PlaceKind::Home,
          "K1: hemmet står först");
    bool allaFinns = true, volym = false, hemmetsVolym = false;
    std::string fel;
    const fs::path hem = p.empty() ? fs::path() : fd::pathFromUtf8(p.front().path);
    for (const gfx::Place& x : p) {
        std::error_code ec;
        const fs::path  s = fd::pathFromUtf8(x.path);
        if (x.kind != gfx::PlaceKind::Volume) {
            if (!fs::is_directory(s, ec) || !s.is_absolute()) {
                allaFinns = false;
                fel       = x.path;
            }
            if (!x.label.empty()) {
                allaFinns = false;
                fel       = "etikett på en användarmapp: " + x.label;
            }
        } else {
            volym = true;
            if (x.label.empty())
                fel = "volym utan etikett: " + x.path;
            // Hemmet ligger på en volym som listas: roten på Windows ("C:\"),
            // "/" på Linux. (På macOS är startvolymen en länk i /Volumes —
            // därför jämförs bara rot mot rot.)
            if (!hem.empty() && s == hem.root_path())
                hemmetsVolym = fs::is_directory(s, ec);
        }
    }
    check(allaFinns, "K2: varje användarmapp finns, är absolut och saknar "
                     "etikett", fel);
    check(volym, "K3: minst en volym");
#ifndef __APPLE__
    check(hemmetsVolym, "K3: volymen som bär hemmet finns bland volymerna",
          fd::utf8FromPath(hem.root_path()));
#endif
    if (g_verbose)
        for (const gfx::Place& x : p)
            std::printf("       plats %d  %s  [%s]\n", static_cast<int>(x.kind),
                        x.path.c_str(), x.label.c_str());
}

#if !defined(_WIN32) && !defined(__APPLE__)
// LINUX: XDG-filen läses på riktigt — en egen `user-dirs.dirs` i ett eget
// `$XDG_CONFIG_HOME` (miljön återställs efteråt). Mäter de tre värdeformerna
// (absolut, `$HOME`-relativ med `\`-skydd, och `$HOME` själv = avstängd)
// och att en mapp som inte finns hoppas över.
void fallXdg(const Trad& t)
{
    // Allt i en EGEN underkatalog som rivs efteråt — fönsterfallen räknar
    // trädets mappar.
    const fs::path    bas = t.rot / u8("k5");
    const std::string b   = fd::utf8FromPath(bas);
    const fs::path    cfg = bas / u8("xdg-cfg");
    fs::create_directories(cfg);
    fs::create_directories(bas / u8("Mitt skrivbord"));
    fs::create_directories(bas / u8("Bilder \"citat\""));
    {
        std::ofstream f(cfg / u8("user-dirs.dirs"));
        f << "# en kommentar\n"
          << "XDG_DESKTOP_DIR=\"" << b << "/Mitt skrivbord\"\n"
          << "XDG_DOCUMENTS_DIR=\"$HOME\"\n"
          << "XDG_DOWNLOAD_DIR=\"" << b << "/finns inte\"\n"
          << "XDG_PICTURES_DIR=\"" << b << "/Bilder \\\"citat\\\"\"\n";
    }
    const char* gammal = std::getenv("XDG_CONFIG_HOME");
    const std::string spar = gammal ? gammal : "";
    ::setenv("XDG_CONFIG_HOME", fd::utf8FromPath(cfg).c_str(), 1);
    const std::vector<gfx::Place> p = gfx::knownPlaces();
    if (gammal)
        ::setenv("XDG_CONFIG_HOME", spar.c_str(), 1);
    else
        ::unsetenv("XDG_CONFIG_HOME");

    auto finns = [&](gfx::PlaceKind k, const std::string& sökväg) {
        for (const gfx::Place& x : p)
            if (x.kind == k)
                return x.path == sökväg;
        return sökväg.empty();
    };
    check(finns(gfx::PlaceKind::Desktop, b + "/Mitt skrivbord"),
          "K5: XDG — en absolut sökväg (blanksteg i namnet)");
    check(finns(gfx::PlaceKind::Pictures, b + "/Bilder \"citat\""),
          "K5: XDG — \\\" i värdet är ett citattecken");
    check(finns(gfx::PlaceKind::Documents, ""),
          "K5: XDG — \"$HOME\" betyder avstängd, ingen plats");
    check(finns(gfx::PlaceKind::Downloads, ""),
          "K5: XDG — en mapp som inte finns hoppas över");
    std::error_code ec;
    fs::remove_all(bas, ec);
}
#endif

// `gfx::isHiddenFile` — det OS:et döljer utöver punktnamnen.
void fallDolda(const Trad& t)
{
    const fs::path f = t.rot / u8("gömd å.txt");
    fil(f, 1);
    const std::string s = fd::utf8FromPath(f);
#ifdef _WIN32
    check(!gfx::isHiddenFile(s), "K4: en vanlig fil är inte dold");
    SetFileAttributesW(f.c_str(), FILE_ATTRIBUTE_HIDDEN);
    check(gfx::isHiddenFile(s),
          "K4: HIDDEN-attributet (Windows) — sökvägen i UTF-8 med åäö");
    bool med = false;
    for (const fd::Entry& e :
         fd::listFolder(t.rot, false, {}, &gfx::isHiddenFile).entries)
        med = med || e.name == "gömd å.txt";
    check(!med, "K4: listFolder med gfx::isHiddenFile döljer den");
    SetFileAttributesW(f.c_str(), FILE_ATTRIBUTE_NORMAL);
#else
    // Linux har inget attribut (punkten är konventionen); macOS flagga
    // (UF_HIDDEN) sätts inte härifrån — den raden står i doc/os/macos.
    check(!gfx::isHiddenFile(s), "K4: en vanlig fil är inte dold");
#endif
    std::error_code ec;
    fs::remove(f, ec);
}

// =============================================================== MED FÖNSTER

// Dialogens fönster i en subklass som når den riktiga ingången.
struct Prov : glaze::FileDialogWindow {
    using glaze::FileDialogWindow::FileDialogWindow;
    using glaze::AppWindow::handleEvent;

    template <class T>
    T* del(const char* namn)
    {
        return find<T>(namn);
    }
    glaze::ListView*  lista() { return del<glaze::ListView>("files"); }
    glaze::TextInput* namn() { return del<glaze::TextInput>("name"); }
    glaze::TextInput* adress() { return del<glaze::TextInput>("path"); }
    glaze::Label*     besked() { return del<glaze::Label>("message"); }

    float skala() { return window().uiScale(); }
    void  tangent(gfx::Key k, std::uint32_t mods = 0)
    {
        gfx::Event e;
        e.type     = gfx::EventType::KeyDown;
        e.windowId = window().id();
        e.key      = k;
        e.mods     = mods;
        handleEvent(e);
    }
    void mus(gfx::EventType t, gfx::Pointf p, std::uint32_t mods = 0)
    {
        gfx::Event e;
        e.type     = t;
        e.windowId = window().id();
        e.mouseX   = p.x;
        e.mouseY   = p.y;
        e.button   = gfx::MouseButton::Left;
        e.mods     = mods;
        handleEvent(e);
    }
    void klick(gfx::Pointf p, std::uint32_t mods = 0)
    {
        mus(gfx::EventType::MouseButtonDown, p, mods);
        mus(gfx::EventType::MouseButtonUp, p, mods);
    }
    void dubbelklick(gfx::Pointf p)
    {
        klick(p);
        klick(p);
    }
    // Ett klick mitt i en del — kryssrutan nås som användaren når den.
    void klickPå(glaze::Widget& w)
    {
        const gfx::Rectf r = w.resolve(w.behaviorCtx());
        klick(gfx::Pointf{r.x + r.w / 2.0f, r.y + r.h / 2.0f});
    }
    glaze::Checkbox* dolda() { return del<glaze::Checkbox>("hidden"); }
    // Mitten av listans rad på PLATS p — kontraktet: ramen, huvudet och p
    // listrader (grindens egen aritmetik, inte tabellens geometri).
    gfx::Pointf rad(int p)
    {
        glaze::ListView* l = lista();
        const gfx::Rectf r = l->resolve(l->behaviorCtx());
        const float      s = skala();
        const float      d = style().panelInset * s;
        return gfx::Pointf{r.x + d + 40.0f * s,
                           r.y + d + style().tableHeaderH * s +
                               (float(p) + 0.5f) * style().listRowH * s};
    }
    // Raden vars namn är `n`, som PLATS (visningsordningen).
    int plats(const std::string& n)
    {
        glaze::ListView* l = lista();
        for (int r = 0; r < l->rowCount(); ++r)
            if (l->cell(r, 0) == n)
                return l->displayRow(r);
        return -1;
    }
    std::vector<std::string> rader()
    {
        std::vector<std::string> v;
        glaze::ListView*         l = lista();
        for (int p = 0; p < l->rowCount(); ++p)
            v.push_back(l->cell(l->rowAtDisplay(p), 0));
        return v;
    }
};

struct Agare : glaze::AppWindow {
    Agare() : glaze::AppWindow("filedialog_gate", 300, 200) {}
};

// KÖR ETT SCENARIO: dialogen byggs, visas DOLD och pumpar modalt medan
// `steg` körs inne i dess pump (postat före `run`). Steget får stänga
// dialogen med OK eller Avbryt; gör det inte det stängs den efteråt, så en
// fallerande kontroll aldrig hänger grinden.
glaze::DialogResult kör(Agare& w, Prov& p, const std::function<void()>& steg)
{
    p.setOffscreen(true);
    bool kördes = false;
    glaze::app().post([&] {
        kördes = true;
        if (!p.isOpen()) {
            std::printf("  --   dialogen kom aldrig upp\n");
            ++g_skips;
            return;
        }
        steg();
        p.close();   // idempotent — ett steg som redan svarat rörs inte
    });
    const glaze::DialogResult r = p.run(w);
    if (!kördes)
        ++g_skips;
    return r;
}

// Meddelanderutan som just öppnats ovanpå dialogen `p` (titeln är
// dialogens), eller nullptr.
glaze::AppWindow* rutan(glaze::AppWindow& p, glaze::AppWindow& w)
{
    for (glaze::AppWindow* x : glaze::app().windows())
        if (x != &p && x != &w && x->isOpen() && x->title() == p.title())
            return x;
    return nullptr;
}

// Knappen med texten `text` i fönstret, eller nullptr.
glaze::Button* knapp(glaze::AppWindow& f, const std::string& text)
{
    for (glaze::Widget* b : f.root().children())
        if (auto* k = dynamic_cast<glaze::Button*>(b); k && k->text() == text)
            return k;
    return nullptr;
}

std::string sv(std::uint32_t id)
{
    const glaze::TrText t = glaze::tr(id);
    return t.str();
}

void fallMedFonster(const Trad& t)
{
    glaze::setLanguage("sv");
    Agare w;
    w.setOffscreen(true);
    const std::string rot = t.utf8();

    bool kedjan = false;
    glaze::app().post([&] {
        kedjan = true;
        if (!w.isOpen()) {
            std::printf("  --   fönsterdelen hoppas över: ägaren kom aldrig upp\n");
            ++g_skips;
            w.close();
            return;
        }

        // ---- W1-W5: öppningsläget.
        {
            glaze::FileDialog cfg(glaze::FileDialogMode::Open);
            cfg.setFolder(rot);
            Prov p(cfg);
            const auto r = kör(w, p, [&] {
                // W1. Innehållet.
                check(p.title() == sv(glaze::str::FileOpen),
                      "W1: titeln är lägets (\"Öppna\")", p.title());
                check(p.adress()->text() == rot,
                      "W1: adressfältet visar mappen (UTF-8)", p.adress()->text());
                check(p.rader() == std::vector<std::string>{
                                       "Mapp A", "mapp b", "Ärenden", "bild.PNG",
                                       "fil2.txt", "fil10.txt", "rapport.pdf",
                                       "översikt å.png"},
                      "W1: mapparna först, dolda borta", lista(p.rader()));
                check(p.namn()->focused(), "W1: namnfältet har fokus vid öppning");
                // Enter och Escape är fönstrets — W5 och
                // W7 mäter dem genom ingången, det här mäter kopplingen.
                check(p.acceptButton() == p.del<glaze::Button>("ok") &&
                          p.cancelButton() == p.del<glaze::Button>("cancel") &&
                          p.acceptButton(),
                      "W1: OK är standardknappen och Avbryt avbrytknappen");

                // W9. Tabbordningen — som ögat läser, oberoende av
                // adoptionsordningen.
                const auto ord = p.focusOrder();
                const std::vector<std::string> väntat{"up",     "path", "hidden",
                                                      "places", "files", "name",
                                                      "filter", "ok",   "cancel"};
                std::vector<std::string> fick;
                for (glaze::Widget* x : ord)
                    fick.push_back(x->name);
                check(fick == väntat, "W9: tabbordningen", lista(fick));

                // W2. Enter på en mapp öppnar den; Backspace går uppåt och
                // väljer mappen man kom ifrån.
                p.lista()->focus();
                p.tangent(gfx::Key::Home);
                p.tangent(gfx::Key::Return);
                check(p.currentFolder() == t.rot / u8("Mapp A") &&
                          p.rader() == std::vector<std::string>{"inne.txt"},
                      "W2: Enter på \"Mapp A\" öppnar den",
                      fd::utf8FromPath(p.currentFolder()));
                p.tangent(gfx::Key::Backspace);
                check(p.currentFolder() == t.rot &&
                          p.lista()->selectedIndex() >= 0 &&
                          p.lista()->cell(p.lista()->selectedIndex(), 0) ==
                              "Mapp A",
                      "W2: Backspace går uppåt, med mappen man kom ifrån vald");
                p.tangent(gfx::Key::Up, gfx::kModAlt);
                check(p.currentFolder() == fd::parentFolder(t.rot),
                      "W2: Alt+Upp går också uppåt");
                p.adress()->setText(rot);
                p.adress()->focus();
                p.tangent(gfx::Key::Return);
                check(p.currentFolder() == t.rot,
                      "W13: en sökväg i adressfältet + Enter går dit");
                p.adress()->setText(rot + "/finns inte");
                p.tangent(gfx::Key::Return);
                check(p.currentFolder() == t.rot &&
                          p.besked()->text() == sv(glaze::str::FileErrFolderMissing),
                      "W13: en sökväg som inte finns står kvar med ett fel",
                      p.besked()->text());

                // W3. Dubbelklick på en mapp öppnar den.
                p.dubbelklick(p.rad(p.plats("Ärenden")));
                check(p.currentFolder() == t.rot / u8("Ärenden"),
                      "W3: dubbelklick på \"Ärenden\" öppnar den (åäö)",
                      fd::utf8FromPath(p.currentFolder()));
                p.tangent(gfx::Key::Backspace);

                // W4. Ett klick på en fil skriver namnfältet; OK svarar.
                p.klick(p.rad(p.plats("översikt å.png")));
                check(p.namn()->text() == "översikt å.png",
                      "W4: valet skriver namnfältet", p.namn()->text());
                glaze::Button* ok = p.del<glaze::Button>("ok");
                p.klick(gfx::Pointf{ok->resolve(ok->behaviorCtx()).x + 4.0f,
                                    ok->resolve(ok->behaviorCtx()).y + 4.0f});
            });
            check(r == glaze::DialogResult::OK && p.accepted(),
                  "W4: OK-knappen stänger dialogen med OK");
            check(fd::pathFromUtf8(cfg.path()) == t.rot / u8("översikt å.png") &&
                      cfg.path().rfind(rot, 0) == 0 && cfg.paths().size() == 1,
                  "W4: sökvägen i UTF-8, åäö och blanksteg orörda", cfg.path());
            check(cfg.folder() == rot && cfg.fileName() == "översikt å.png",
                  "W4: mappen och namnet står kvar i inställningarna");
        }
        {
            // W16. Ett klick i huvudet sorterar på STORLEKEN — som tal, med
            // mapparna först (dialogen sätter nycklarna, tabellen sorterar).
            glaze::FileDialog cfg(glaze::FileDialogMode::Open);
            cfg.setFolder(rot);
            Prov p(cfg);
            kör(w, p, [&] {
                glaze::ListView* l = p.lista();
                const gfx::Rectf r = l->resolve(l->behaviorCtx());
                const float      s = p.skala();
                const float      d = p.style().panelInset * s;
                const float      x = r.x + d +
                                (l->columnWidth(0) + l->columnWidth(1) / 2.0f) * s;
                const float y = r.y + d + p.style().tableHeaderH * s / 2.0f;
                p.klick(gfx::Pointf{x, y});
                check(l->sortColumn() == fd::ColSize &&
                          p.rader() == std::vector<std::string>{
                                           "Mapp A", "mapp b", "Ärenden",
                                           "rapport.pdf", "fil10.txt",
                                           "översikt å.png", "bild.PNG",
                                           "fil2.txt"},
                      "W16: klick på \"Storlek\" — mapparna först, sedan 5 < 10 "
                      "< 300 < 2 000 < 20 000 byte",
                      lista(p.rader()));
            });
        }
        {
            // W5. Escape avbryter — och `None` läcker inte.
            glaze::FileDialog cfg(glaze::FileDialogMode::Open);
            cfg.setFolder(rot);
            Prov p(cfg);
            const auto r = kör(w, p, [&] { p.tangent(gfx::Key::Escape); });
            check(r == glaze::DialogResult::Cancel && !p.accepted() &&
                      cfg.paths().empty(),
                  "W5: Escape = Cancel, inga sökvägar");
        }
        {
            // W11 + W14: filtypsbytet med tangentbordet och flervalet.
            glaze::FileDialog cfg(glaze::FileDialogMode::Open);
            cfg.setFolder(rot);
            cfg.addFilter("Alla", "*.*");
            cfg.addFilter("Bilder", "*.png;*.jpg");
            cfg.setMultiSelect(true);
            Prov p(cfg);
            const auto r = kör(w, p, [&] {
                glaze::Dropdown* f = p.del<glaze::Dropdown>("filter");
                f->focus();
                p.tangent(gfx::Key::Down);   // stängt läge: nästa val + onChange
                check(f->selectedIndex() == 1 &&
                          p.rader() == std::vector<std::string>{"Mapp A", "mapp b",
                                                                "Ärenden", "bild.PNG",
                                                                "översikt å.png"},
                      "W11: filtret \"Bilder\" — mapparna kvar, bara bilderna",
                      lista(p.rader()));
                p.klick(p.rad(p.plats("bild.PNG")));
                p.klick(p.rad(p.plats("översikt å.png")), gfx::kModCtrl);
                check(p.namn()->text() == "\"bild.PNG\" \"översikt å.png\"",
                      "W14: två valda filer står citerade i namnfältet",
                      p.namn()->text());
                p.namn()->focus();
                p.tangent(gfx::Key::Return);
            });
            check(r == glaze::DialogResult::OK && cfg.paths().size() == 2 &&
                      cfg.filterIndex() == 1,
                  "W14: OK ger båda sökvägarna och det valda filtret",
                  std::to_string(cfg.paths().size()));
        }
        {
            // W6: sparandets ändelse, och W7: överskrivningsfrågan.
            glaze::FileDialog cfg(glaze::FileDialogMode::Save);
            cfg.setFolder(rot);
            cfg.addFilter("Text", "*.txt");
            cfg.setFileName("fil2");
            Prov p(cfg);
            // De postade svaren bär ett LIVSTECKEN (W6d:s mönster): i grönt
            // läge körs de inne i meddelanderutans pump, men ställs ingen
            // fråga (sabotaget "frågan överhoppad") körs de först efter att
            // dialogen och stegets lokala variabler är borta.
            const auto levande = std::make_shared<bool>(true);
            const auto frågad  = std::make_shared<bool>(false);
            const auto texten  = std::make_shared<bool>(false);
            const auto r       = kör(w, p, [&] {
                check(p.title() == sv(glaze::str::FileSaveAs) &&
                          p.namn()->text() == "fil2",
                      "W6: \"Spara som\" och det förvalda namnet");
                // "fil2" + filtrets ".txt" = en fil som FINNS ⇒ frågan. Nej
                // först: dialogen står kvar.
                glaze::app().post([&p, &w, levande, frågad, texten] {
                    if (!*levande)
                        return;
                    if (glaze::AppWindow* m = rutan(p, w)) {
                        *frågad = true;
                        for (glaze::Widget* x : m->root().children())
                            if (auto* l = dynamic_cast<glaze::Label*>(x))
                                *texten = *texten ||
                                          l->text().find("fil2.txt") != std::string::npos;
                        if (glaze::Button* nej = knapp(*m, sv(glaze::str::No)))
                            nej->performClick();
                        m->close();   // fallnät om knappen inte hittades
                    }
                });
                p.namn()->focus();
                p.tangent(gfx::Key::Return);
                check(*frågad && *texten,
                      "W7: en befintlig fil ger meddelanderutan med filnamnet");
                check(p.isOpen() && !p.accepted() && cfg.paths().empty(),
                      "W7: \"Nej\" lämnar dialogen öppen utan svar");
                // Ja: dialogen svarar med den befintliga filen.
                glaze::app().post([&p, &w, levande] {
                    if (!*levande)
                        return;
                    if (glaze::AppWindow* m = rutan(p, w)) {
                        if (glaze::Button* ja = knapp(*m, sv(glaze::str::Yes)))
                            ja->performClick();
                        m->close();
                    }
                });
                p.tangent(gfx::Key::Return);
            });
            *levande = false;
            check(r == glaze::DialogResult::OK &&
                      fd::pathFromUtf8(cfg.path()) == t.rot / u8("fil2.txt"),
                  "W7: \"Ja\" svarar med den befintliga filen (W6: ändelsen "
                  "ur filtret)",
                  cfg.path());
        }
        {
            // W6b: ett NYTT namn får filtrets ändelse och ingen fråga.
            glaze::FileDialog cfg(glaze::FileDialogMode::Save);
            cfg.setFolder(rot);
            cfg.addFilter("PDF", "*.pdf");
            Prov p(cfg);
            const auto r = kör(w, p, [&] {
                p.namn()->setText("ny rapport");
                p.namn()->focus();
                p.tangent(gfx::Key::Return);
            });
            check(r == glaze::DialogResult::OK &&
                      fd::pathFromUtf8(cfg.path()) == t.rot / u8("ny rapport.pdf"),
                  "W6: ett nytt namn får \".pdf\" ur filtret, utan fråga",
                  cfg.path());

            // W6c: ändelsen prövas på det SISTA namnet — en mapp med punkt i
            // namnet ("v1.2/ny") är ingen ändelse. Mappen rivs efteråt:
            // senare fall räknar trädets mappar.
            const fs::path punkt = t.rot / u8("v1.2");
            fs::create_directories(punkt);
            glaze::FileDialog cfg2(glaze::FileDialogMode::Save);
            cfg2.setFolder(rot);
            cfg2.addFilter("PDF", "*.pdf");
            Prov p2(cfg2);
            const auto r2 = kör(w, p2, [&] {
                p2.namn()->setText("v1.2/ny");
                p2.namn()->focus();
                p2.tangent(gfx::Key::Return);
            });
            check(r2 == glaze::DialogResult::OK &&
                      fd::pathFromUtf8(cfg2.path()) == punkt / u8("ny.pdf"),
                  "W6c: \"v1.2/ny\" får \".pdf\" — punkten i mappnamnet räknas "
                  "inte",
                  cfg2.path());
            std::error_code ec;
            fs::remove_all(punkt, ec);

            // W6d: "arkiv" + ".pdf" är en MAPP — den öppnas, som varje namn
            // som är en mapp, och ingen överskrivningsfråga ställs. Stängaren
            // POSTAS före Enter (W7:s mönster) så att en fråga som felaktigt
            // ställs besvaras i stället för att hänga grinden; den bär sitt
            // eget livstecken, för när ingen fråga ställs körs den först
            // efter att dialogen är borta.
            const fs::path arkiv = t.rot / u8("arkiv.pdf");
            fs::create_directories(arkiv);
            glaze::FileDialog cfg3(glaze::FileDialogMode::Save);
            cfg3.setFolder(rot);
            cfg3.addFilter("PDF", "*.pdf");
            Prov       p3(cfg3);
            const auto levande = std::make_shared<bool>(true);
            const auto frågad  = std::make_shared<bool>(false);
            const auto r3      = kör(w, p3, [&] {
                glaze::app().post([&p3, &w, levande, frågad] {
                    if (!*levande)
                        return;
                    if (glaze::AppWindow* m = rutan(p3, w)) {
                        *frågad = true;
                        m->close();
                    }
                });
                p3.namn()->setText("arkiv");
                p3.namn()->focus();
                p3.tangent(gfx::Key::Return);
                check(!*frågad && p3.isOpen() && p3.currentFolder() == arkiv &&
                          p3.namn()->text().empty(),
                      "W6d: \"arkiv\" + \".pdf\" är en mapp — den öppnas, utan "
                      "fråga",
                      fd::utf8FromPath(p3.currentFolder()));
                p3.tangent(gfx::Key::Escape);
            });
            *levande = false;
            check(r3 == glaze::DialogResult::Cancel && cfg3.paths().empty(),
                  "W6d: dialogen svarade inte med mappen");
            fs::remove_all(arkiv, ec);
        }
        {
            // W8: mappläget.
            glaze::FileDialog cfg(glaze::FileDialogMode::Folder);
            cfg.setFolder(rot);
            Prov p(cfg);
            const auto r = kör(w, p, [&] {
                check(p.rader() == std::vector<std::string>{"Mapp A", "mapp b",
                                                            "Ärenden"} &&
                          !p.del<glaze::Dropdown>("filter")->isVisible(),
                      "W8: bara mappar, och inga filter", lista(p.rader()));
                p.klick(p.rad(p.plats("mapp b")));
                check(p.namn()->text() == "mapp b",
                      "W8: den valda mappen står i fältet", p.namn()->text());
                p.namn()->focus();
                p.tangent(gfx::Key::Return);
            });
            check(r == glaze::DialogResult::OK &&
                      fd::pathFromUtf8(cfg.path()) == t.rot / u8("mapp b"),
                  "W8: OK ger den valda mappen", cfg.path());

            glaze::FileDialog cfg2(glaze::FileDialogMode::Folder);
            cfg2.setFolder(rot);
            Prov p2(cfg2);
            const auto r2 = kör(w, p2, [&] {
                glaze::Button* ok = p2.del<glaze::Button>("ok");
                ok->focus();
                p2.tangent(gfx::Key::Space);   // knappens egen aktivering
            });
            check(r2 == glaze::DialogResult::OK &&
                      fd::pathFromUtf8(cfg2.path()) == t.rot,
                  "W8: OK utan val ger mappen som visas", cfg2.path());
        }
        {
            // W10: mappen tas bort medan dialogen står öppen — F5 säger det.
            const fs::path tillf = t.rot / u8("snart borta");
            fs::create_directories(tillf);
            fil(tillf / u8("x.txt"), 1);
            glaze::FileDialog cfg(glaze::FileDialogMode::Open);
            cfg.setFolder(fd::utf8FromPath(tillf));
            Prov p(cfg);
            kör(w, p, [&] {
                check(p.rader() == std::vector<std::string>{"x.txt"},
                      "W10: mappen visas");
                std::error_code ec;
                fs::remove_all(tillf, ec);
                p.tangent(gfx::Key::F5);
                check(p.rader().empty() &&
                          p.besked()->text() == sv(glaze::str::FileErrFolderMissing),
                      "W10: F5 efter borttagningen — inga rader, \"Mappen finns "
                      "inte.\"",
                      p.besked()->text());
                // Namnfältet har fokus (öppningsläget), så Backspace är
                // FÄLTETS och går inte uppåt — Alt+Upp gör det var fokus än
                // står.
                p.tangent(gfx::Key::Backspace);
                check(p.currentFolder() == tillf,
                      "W10: Backspace i namnfältet raderar text, går inte uppåt");
                p.tangent(gfx::Key::Up, gfx::kModAlt);
                check(p.currentFolder() == t.rot && p.besked()->text().empty(),
                      "W10: uppåt därifrån fungerar, och felet släcks");
            });
        }
        {
            // W12: en plats väljs med tangentbordet, och W15: språkbytet.
            glaze::FileDialog cfg(glaze::FileDialogMode::Open);
            cfg.setFolder(rot);
            Prov p(cfg);
            kör(w, p, [&] {
                const std::vector<gfx::Place> pl = gfx::knownPlaces();
                glaze::ListBox* platser = p.del<glaze::ListBox>("places");
                platser->focus();
                p.tangent(gfx::Key::Down);   // inget valt ⇒ första platsen
                check(!pl.empty() &&
                          p.currentFolder() == fd::withoutTrailingSeparator(
                                                   fd::pathFromUtf8(pl.front().path)) &&
                          platser->selectedIndex() == 0,
                      "W12: en plats väljs ⇒ dialogen går dit",
                      fd::utf8FromPath(p.currentFolder()));
                p.adress()->setText(rot);
                p.adress()->focus();
                p.tangent(gfx::Key::Return);
                glaze::setLanguage("en");
                const int r = p.plats("Mapp A");
                const glaze::ListView* l = p.lista();
                check(r >= 0 && l->cell(l->rowAtDisplay(r), 3) == "File folder" &&
                          platser->items().front() == "Home" &&
                          p.title() == "Open",
                      "W15: språkbytet når typtexten, platserna och titeln",
                      r >= 0 ? l->cell(l->rowAtDisplay(r), 3) : std::string("?"));
                check(p.dolda() && p.dolda()->text() == "Show hidden files",
                      "W15: …och kryssrutan för dolda filer",
                      p.dolda() ? p.dolda()->text() : std::string("?"));
                glaze::setLanguage("sv");
            });
        }
        {
            // W17: DOLDA FILER. Kryssrutan styr listan DIREKT — ett
            // klick genom fönstrets ingång, ingen ny dialog — och valet skrivs
            // tillbaka till inställningarna med en gång, så det står kvar
            // också när dialogen sedan AVBRYTS. På Windows dessutom en fil med
            // det RIKTIGA HIDDEN-attributet (`gfx::isHiddenFile`-vägen): dold
            // när rutan är tom, med när den är kryssad.
#ifdef _WIN32
            const fs::path gömd = t.rot / u8("gömd å.txt");
            fil(gömd, 1);
            SetFileAttributesW(gömd.c_str(), FILE_ATTRIBUTE_HIDDEN);
#endif
            const std::vector<std::string> utan{"Mapp A",   "mapp b",      "Ärenden",
                                                "bild.PNG", "fil2.txt",    "fil10.txt",
                                                "rapport.pdf", "översikt å.png"};
            const std::vector<std::string> med{
                ".dold",    "Mapp A",   "mapp b",    "Ärenden", ".doldfil",
                "bild.PNG", "fil2.txt", "fil10.txt",
#ifdef _WIN32
                "gömd å.txt",   // HIDDEN-attributet — syns bara med rutan
#endif
                "rapport.pdf", "översikt å.png"};
            glaze::FileDialog cfg(glaze::FileDialogMode::Open);
            cfg.setFolder(rot);
            Prov p(cfg);
            const auto r = kör(w, p, [&] {
                glaze::Checkbox* k = p.dolda();
                check(k && !k->isChecked() && !cfg.showHidden() &&
                          k->text() == sv(glaze::str::FileShowHidden),
                      "W17: kryssrutan finns — tom som förval, med ramverkets "
                      "text");
                check(p.rader() == utan,
                      "W17: tom ruta — punktnamnen och det OS:et döljer syns "
                      "inte",
                      lista(p.rader()));
                if (!k)
                    return;
                p.klickPå(*k);
                check(k->isChecked() && p.rader() == med && cfg.showHidden(),
                      "W17: ett klick på rutan — de dolda syns DIREKT, och valet "
                      "står redan i inställningarna",
                      lista(p.rader()));
                p.klickPå(*k);
                check(!k->isChecked() && p.rader() == utan && !cfg.showHidden(),
                      "W17: ett klick till — de döljs igen", lista(p.rader()));
                p.klickPå(*k);
                p.tangent(gfx::Key::Escape);
            });
            check(r == glaze::DialogResult::Cancel && cfg.paths().empty() &&
                      cfg.showHidden(),
                  "W17: valet läses tillbaka efter run() — också när dialogen "
                  "avbröts");
#ifdef _WIN32
            SetFileAttributesW(gömd.c_str(), FILE_ATTRIBUTE_NORMAL);
            std::error_code ec;
            fs::remove(gömd, ec);
#endif
        }
        {
            // W18: egenskapen IN — dialogen öppnar med rutan kryssad och de
            // dolda i listan — och FLERVALET ihop med en dold fil: Ctrl-klick
            // på ".doldfil" och "bild.PNG", OK ger båda sökvägarna.
            glaze::FileDialog cfg(glaze::FileDialogMode::Open);
            cfg.setFolder(rot);
            cfg.setShowHidden(true);
            cfg.setMultiSelect(true);
            Prov p(cfg);
            const auto r = kör(w, p, [&] {
                check(p.dolda() && p.dolda()->isChecked() &&
                          p.plats(".dold") >= 0 && p.plats(".doldfil") >= 0,
                      "W18: setShowHidden(true) — rutan kryssad och de dolda i "
                      "listan när dialogen öppnas",
                      lista(p.rader()));
                if (const int i = p.plats(".doldfil"); i >= 0)
                    p.klick(p.rad(i));
                if (const int i = p.plats("bild.PNG"); i >= 0)
                    p.klick(p.rad(i), gfx::kModCtrl);
                check(p.namn()->text() == "\".doldfil\" \"bild.PNG\"",
                      "W18: en dold och en vanlig fil valda — båda citerade i "
                      "namnfältet",
                      p.namn()->text());
                p.namn()->focus();
                p.tangent(gfx::Key::Return);
            });
            check(r == glaze::DialogResult::OK && cfg.paths().size() == 2 &&
                      fd::pathFromUtf8(cfg.paths()[0]) == t.rot / u8(".doldfil") &&
                      fd::pathFromUtf8(cfg.paths()[1]) == t.rot / u8("bild.PNG") &&
                      cfg.showHidden(),
                  "W18: OK ger båda sökvägarna — den dolda med — och valet står "
                  "kvar",
                  lista(cfg.paths()));
        }
        {
            // W19: MAPPLÄGET — rutan gäller mapparna också, och en dold mapp
            // går att välja.
            glaze::FileDialog cfg(glaze::FileDialogMode::Folder);
            cfg.setFolder(rot);
            Prov p(cfg);
            const auto r = kör(w, p, [&] {
                check(p.rader() == std::vector<std::string>{"Mapp A", "mapp b",
                                                            "Ärenden"},
                      "W19: mappläget, tom ruta — ingen dold mapp",
                      lista(p.rader()));
                if (glaze::Checkbox* k = p.dolda())
                    p.klickPå(*k);
                check(p.rader() == std::vector<std::string>{".dold", "Mapp A",
                                                            "mapp b", "Ärenden"},
                      "W19: kryssad — den dolda mappen syns", lista(p.rader()));
                if (const int i = p.plats(".dold"); i >= 0)
                    p.klick(p.rad(i));
                p.namn()->focus();
                p.tangent(gfx::Key::Return);
            });
            check(r == glaze::DialogResult::OK &&
                      fd::pathFromUtf8(cfg.path()) == t.rot / u8(".dold"),
                  "W19: OK ger den dolda mappen", cfg.path());
        }

        w.close();
    });
    w.run();
    if (!kedjan) {
        std::printf("  --   fönsterdelen kördes ALDRIG (pumpen startade inte)\n");
        ++g_skips;
    }
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "-v") == 0)
            g_verbose = true;

    std::printf("filedialog_gate — filväljardialogen\n");
    Trad t;
    fallFilter();
    fallAndelsen();
    fallNamnfaltet();
    fallOrdningen();
    fallStorleken();
    fallKatalogen(t);
    fallDoldaPoster(t);
    fallFelen(t);
    fallSokvagarna(t);
    fallNycklarna();
    fallPlatserna();
#if !defined(_WIN32) && !defined(__APPLE__)
    fallXdg(t);
#endif
    fallDolda(t);
    fallMedFonster(t);

    if (g_skips)
        std::printf("     %d block överhoppade (se raderna ovan)\n", g_skips);
    std::printf("%s  %d fall, %d fel\n", g_fails ? "FEL " : "OK  ", g_cases,
                g_fails);
    if (g_fails)
        return 1;
    return g_skips ? 77 : 0;
}
