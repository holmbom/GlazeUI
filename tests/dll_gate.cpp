// dll_gate — DELAT BIBLIOTEK: REGELN B5.
//
// Kör:  ./build-test/tests/dll_gate --objdump <objdump> [--readelf <readelf>]
//           --control-lib <fixturarkiv> --control-headers <fixturkatalog>
//           --headers <katalog>... --libs <arkiv>...
//       (ctest skickar argumenten — tests/CMakeLists.txt)
// Exit: 0 = grönt · 1 = ett fynd, eller en kontroll som räknade fel ·
//       77 = del A kunde inte köras (macOS/Mach-O, MSVC, eller inget
//       objdump/readelf) men del B var grön — ctest räknar SKIPPED, aldrig
//       grönt
//
// Byggs och körs bara i STATISKT läge: den läser arkiven, och ett delat
// bygge har inga. Ren std — länkar inte glaze.
//
// REGELN B5: inget FÖRÄNDERLIGT statiskt tillstånd i en header under Glaze/,
// Threads/ eller Networking/. En statisk lokal i en inline-funktion, en
// inline-variabel och en malls statiska medlem får i delat läge en kopia PER
// MODUL på Windows — PE har ingen global symbolupplösning, och det hjälper
// inte hur symbolerna exporteras. En app som laddar en plugin hade då haft
// två register, två App, två utseenden. const/constexpr med konstant
// initiering är undantagna (de är oföränderliga och hamnar i .rdata). Den
// statiska sviten bär grinden, så regressionen fångas utan ett delat bygge.
//
// DEL A — OBJEKTEN. En duplicerad entitet är en COMDAT-DATASEKTION i
// objekten. PE: `objdump -h` — `.data$<sym>`, `.bss$<sym>`, `.tls$<sym>` (GCC
// 16 skriver trådlokala som `.tls$$<sym>`, ALLA `$` strippas) med
// LINK_ONCE-flaggan. ELF: `readelf -g` — `.data.<sym>`, `.data.rel(.local).
// <sym>`, `.bss.<sym>`, `.tbss.<sym>`, `.tdata.<sym>` som MEDLEM I EN
// COMDAT-GRUPP (-fdata-sections ger en vanlig global samma
// namnform, och bara COMDAT-kontrollen skiljer dem åt). readelf skriver bytes
// utanför ASCII som `<C3><A4>` — de avkodas, för ramverkets identifierare har
// åäö. Konstanterna (`.rdata$`, `.rodata.`, `.data.rel.ro.`) räknas inte.
// Symbolen avmanglas (`__emutls_v.` strippas först, för äldre MinGW), TLS-
// och referenstemporärprefix normaliseras, typeinfo/vtable/VTT/guard
// variable filtreras bort, och BARA det som börjar med `glaze::`, `gfx::`,
// `glzassets::`, `glzlang::` eller `bld::` räknas. Går en symbol inte att
// avmangla men bär ett av namnrymdsleden räknas den ändå, oavmanglad.
//
// DEL B — HEADERS (allt under --headers som slutar på .h, utom */protocols/*).
// Kommentarer, strängar och preprocessorrader rensas bort, klammrarna spåras
// som FUNKTIONSKROPP (inleds efter `)` — också efter const/noexcept/
// override/final/`&` — efter `]` i en lambda, efter en efterställd returtyp
// `-> T` eller efter en requires-klausul), KLASS eller ANNAT:
//   H1  `thread_local` var som helst
//   H2  en inline-variabel — också `static inline`/`inline static`-medlemmar
//       — som inte är undantagen
//   H3  `static` i en funktionskropp (inte i en lokal klass) som inte är
//       undantagen; static_assert/static_cast är andra ord
// UNDANTAGET: `constexpr` eller `const`
// som kvalificerar SJÄLVA objektet — på toppnivå, aldrig i ett mallargument
// (`std::map<const int, int>` är en föränderlig karta). `const` dessutom bara
// UTAN föränderligt tillstånd bakom sig — inte en smartpekare,
// `std::function`, `span` eller `reference_wrapper`, och inte en pekare vars
// egen `*` saknar `const` (`const char* p` är en föränderlig pekare).
//
// KONTROLLERNA — icke-cirkulära, för de förväntade värdena är handskrivna:
//   · del A på --control-lib (tests/dll_gate_fixture.cpp + fixtures/dll_gate/
//     dll_gate_bad.h, byggd med -fdata-sections) ska ge EXAKT räknare()::n,
//     delad, perTråd, Medlem::antal, karta och ägd — aldrig kOk, och aldrig
//     den vanliga globalen som har samma sektionsnamnform
//   · del B på --control-headers ska ge EXAKT H1 1 · H2 4 · H3 3
//   · varje riktigt arkiv måste ge medlemmar och sektioner (ett verktyg som
//     inte skriver något hade annars gett noll fynd och grönt), och varje
//     --headers-katalog minst en header
// Ger en kontroll fel värde är tolkaren trasig på den här verktygskedjan, och
// grinden fäller. De riktiga arkiven och headrarna måste ge 0 fynd.
//
// SABOTAGEN som ska fälla den (körs före commit, aldrig under bygget):
//   · DG1 glaze::app() inline i app.h med en statisk instans ......... A + B
//         (A: `.data$`/`.bss$` för glaze::app()::instance, B: H3)
//   · DG2 `inline int räknare = 0;` i en publik header som ingen använder . B
//         (H2 — A ser den inte, ingenting instansierar den)
//   · DG3 `thread_local int x;` i en header ............................ B (H1)
//   · DG4 fixturens `static int n` görs `static constexpr` (och räknare()
//         returnerar `const int&`) ....... kontrollerna fäller: A 5 i st.f. 6,
//                                          B H3 2 i st.f. 3
//   · DG5 sektionsprefixet strippar bara ETT `$` . A:s
//         kontroll fäller på Windows — perTråd (`.tls$$…`) försvinner
//   · DG6 COMDAT-kontrollen bort (LINK_ONCE/gruppen krävs inte) ...... A:s
//         kontroll fäller — den vanliga globalen räknas (7)
//   · DG7 const-undantaget utan smartpekarvillkoret ...... B:s kontroll fäller
//         (H3 2 i st.f. 3 — handtag() — och H2 3 i st.f. 4 — ägd)
//   · DG8 en `const` VAR SOM HELST undantar (djupspårningen i `undantagen`
//         bort) ................. B:s kontroll fäller (H2 3 i st.f. 4 — karta)
//
// VAD DEN INTE MÄTER (en skriven lista, inte ett förbiseende):
//   · Heuristiken ERSÄTTER INTE GRANSKNING: en ny header med processvitt
//     tillstånd granskas för hand. Den missar mönster som
//     undgår H1–H3 — en mallmedlem som definieras utanför klassen utan
//     static/inline (`template<…> int X<T>::n = 0;`), en deklaration som
//     delas över rader före namnet, en konstruktors kropp efter en
//     initieringslista som slutar på `}`, och statiska variabler på
//     namnrymdsnivå (de är en kopia per TU i båda lägena).
//   · Header-kod som bara appar instansierar och som undgår del B: del A ser
//     bara det biblioteken, builder_preview och builder_core instansierar.
//   · Adressidentitet mellan moduler (en inline-funktions eller en konstants
//     adress) — ingen kod jämför sådana.
//   · Något i körtid — det gör shared_gate, i det delade bygget.
//   · macOS: del A hoppas över (Mach-O har inga sektioner per symbol), del B
//     körs. MSVC: del A hoppas över.
//   · ELF: readelf kortar sektionsnamn över 256 tecken; en sådan symbol
//     avmanglas inte, och räknas bara om ett namnrymdsled syns i det kortade.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if __has_include(<cxxabi.h>)
#include <cstdlib>
#include <cxxabi.h>
#define DLL_GATE_HAR_AVMANGLING 1
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>   // CommandLineToArgvW — efter windows.h
#endif

namespace fs = std::filesystem;

namespace {

int g_fel = 0;

void check(bool ok, const std::string& vad)
{
    std::printf("%-6s %s\n", ok ? "OK" : "FEL", vad.c_str());
    if (!ok)
        ++g_fel;
}

// ---------------------------------------------------------------- UTF-8

fs::path sökväg(const std::string& utf8)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()),
                                  utf8.size()));
}

std::string text(const fs::path& p)
{
    const std::u8string s = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

// Argumenten i UTF-8. Windows ger `main` dem i ANSI-teckentabellen, där en
// sökväg med ett tecken utanför den går sönder — kommandoraden läses därför
// i UTF-16.
std::vector<std::string> argumenten(int argc, char** argv)
{
#ifdef _WIN32
    (void)argc;
    (void)argv;
    std::vector<std::string> ut;
    int     n = 0;
    LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
    if (!w)
        return ut;
    for (int i = 0; i < n; ++i)
        ut.push_back(text(fs::path(w[i])));
    LocalFree(w);
    return ut;
#else
    return std::vector<std::string>(argv, argv + argc);
#endif
}

// ---------------------------------------------------------------- verktygen

// Kör ett verktyg och samlar stdout rad för rad. false = det gick inte att
// starta, eller det avslutades med fel.
bool kör(const std::vector<std::string>& args, std::vector<std::string>& rader)
{
#ifdef _WIN32
    // cmd.exe /c tar bort det YTTERSTA citatparet — därav paret runt allt.
    // Sökvägarna med Windows-avskiljare: cmd.exe läser `/` som en växel.
    std::wstring rad = L"\"";
    for (const std::string& a : args) {
        fs::path p = sökväg(a);
        rad += L"\"" + p.make_preferred().wstring() + L"\" ";
    }
    rad += L"\"";
    FILE* rör = _wpopen(rad.c_str(), L"rt");
#else
    std::string rad;
    for (const std::string& a : args) {
        rad += '\'';
        for (char c : a)
            rad += c == '\'' ? std::string("'\\''") : std::string(1, c);
        rad += "' ";
    }
    FILE* rör = popen(rad.c_str(), "r");
#endif
    if (!rör)
        return false;
    std::string aktuell;
    char        buf[4096];
    while (std::fgets(buf, sizeof(buf), rör)) {
        aktuell += buf;
        if (!aktuell.empty() && aktuell.back() == '\n') {
            while (!aktuell.empty() &&
                   (aktuell.back() == '\n' || aktuell.back() == '\r'))
                aktuell.pop_back();
            rader.push_back(aktuell);
            aktuell.clear();
        }
    }
    if (!aktuell.empty())
        rader.push_back(aktuell);
#ifdef _WIN32
    return _pclose(rör) == 0;
#else
    return pclose(rör) == 0;
#endif
}

bool börjarMed(std::string_view s, std::string_view p)
{
    return s.substr(0, p.size()) == p;
}

std::string trimma(const std::string& s)
{
    const auto a = s.find_first_not_of(" \t");
    if (a == std::string::npos)
        return {};
    const auto b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------- DEL A

struct Sektion {
    std::string medlem;
    std::string namn;
    bool        comdat = false;
};

// `objdump -h <arkiv>` (PE): "<medlem>:     file format pe-x86-64", sedan per
// sektion "  <idx> <namn> <storlek> …" och flaggraden under den.
std::vector<Sektion> sektionerPe(const std::vector<std::string>& rader,
                                 int& medlemmar)
{
    std::vector<Sektion> ut;
    std::string          medlem;
    bool                 väntarFlaggor = false;
    for (const std::string& r : rader) {
        const auto format = r.find("file format ");
        if (format != std::string::npos) {
            const auto kolon = r.rfind(':', format);
            medlem           = kolon == std::string::npos ? r : r.substr(0, kolon);
            ++medlemmar;
            väntarFlaggor = false;
            continue;
        }
        const std::string t = trimma(r);
        if (t.empty())
            continue;
        std::istringstream ord(t);
        std::string        första, andra;
        ord >> första >> andra;
        const bool ärIndex = !första.empty() &&
                             std::all_of(första.begin(), första.end(),
                                         [](char c) { return c >= '0' && c <= '9'; });
        if (ärIndex && !andra.empty()) {
            ut.push_back({medlem, andra, false});
            väntarFlaggor = true;
        } else if (väntarFlaggor) {
            ut.back().comdat = t.find("LINK_ONCE") != std::string::npos;
            väntarFlaggor    = false;
        }
    }
    return ut;
}

// readelf skriver bytes utanför ASCII i sektionsnamn som `<C3><A4>`. En
// manglad symbol innehåller aldrig `<`, så avkodningen är entydig.
std::string avkodaReadelf(const std::string& s)
{
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        return -1;
    };
    std::string ut;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '<' && i + 3 < s.size() && s[i + 3] == '>' &&
            hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
            ut += static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 3;
        } else {
            ut += s[i];
        }
    }
    return ut;
}

// `readelf -g -W <arkiv>` (ELF): "File: <arkiv>(<medlem>)" per medlem, och
// per grupp "COMDAT group section [ n] `.group' [sig] contains k sections:"
// följd av "[Index] Name" och en rad per medlemssektion. Bara sektionerna i
// COMDAT-grupper samlas — de är per definition COMDAT.
std::vector<Sektion> sektionerElf(const std::vector<std::string>& rader,
                                  int& medlemmar)
{
    std::vector<Sektion> ut;
    std::string          medlem;
    bool                 iComdat = false;
    for (const std::string& r : rader) {
        const std::string t = trimma(r);
        if (börjarMed(t, "File: ")) {
            medlem  = t.substr(6);
            iComdat = false;
            ++medlemmar;
            continue;
        }
        if (t.empty()) {
            iComdat = false;
            continue;
        }
        if (börjarMed(t, "COMDAT group section")) {
            iComdat = true;
            continue;
        }
        if (börjarMed(t, "group section")) {
            iComdat = false;
            continue;
        }
        if (iComdat && t.front() == '[') {
            const auto slut = t.find(']');
            if (slut == std::string::npos)
                continue;
            const std::string index = trimma(t.substr(1, slut - 1));
            if (index.empty() ||
                !std::all_of(index.begin(), index.end(),
                             [](char c) { return c >= '0' && c <= '9'; }))
                continue;   // rubrikraden "[Index] Name"
            ut.push_back({medlem, avkodaReadelf(trimma(t.substr(slut + 1))), true});
        }
    }
    if (medlemmar == 0 && !rader.empty())
        medlemmar = 1;   // ett ensamt objekt har ingen "File:"-rad
    return ut;
}

// Sektionens symbol om sektionen är FÖRÄNDERLIG data, annars tom. `slag` får
// sektionsprefixet för utskriften.
std::string föränderligSymbol(const std::string& namn, bool pe, std::string& slag)
{
    if (pe) {
        for (std::string_view bas : {".data$", ".bss$", ".tls$"}) {
            if (!börjarMed(namn, bas))
                continue;
            std::size_t i = bas.size();
            while (i < namn.size() && namn[i] == '$')
                ++i;   // GCC 16: `.tls$$<sym>`
            slag = std::string(bas);
            return namn.substr(i);
        }
        return {};
    }
    if (börjarMed(namn, ".data.rel.ro"))
        return {};   // relro — oföränderligt efter relokeringen
    for (std::string_view bas : {".data.rel.local.", ".data.rel.", ".data.",
                                 ".bss.", ".tbss.", ".tdata."}) {
        if (börjarMed(namn, bas)) {
            slag = std::string(bas);
            return namn.substr(bas.size());
        }
    }
    return {};
}

constexpr std::string_view kNamnrymder[] = {"glaze::", "gfx::", "glzassets::",
                                            "glzlang::", "bld::"};
constexpr std::string_view kManglade[]   = {"5glaze", "3gfx", "9glzassets",
                                            "7glzlang", "3bld"};

// Entitetens namn som det RÄKNAS, eller tomt när den inte räknas.
std::string entitet(std::string sym)
{
    for (std::string_view tls : {"__emutls_v.", "__emutls_t."})
        if (börjarMed(sym, tls))
            sym = sym.substr(tls.size());
    std::string namn;
#ifdef DLL_GATE_HAR_AVMANGLING
    int   status = 0;
    char* av     = abi::__cxa_demangle(sym.c_str(), nullptr, nullptr, &status);
    if (status == 0 && av)
        namn = av;
    std::free(av);
#endif
    if (namn.empty()) {
        // Oavmanglad: räknas om ett av namnrymdsleden syns i den manglade
        // formen — hellre ett fynd att granska än ett tyst förbiseende.
        if (!börjarMed(sym, "_Z"))
            return {};
        for (std::string_view led : kManglade)
            if (sym.find(led) != std::string::npos)
                return sym + "  (oavmanglad)";
        return {};
    }
    for (std::string_view bort : {"guard variable for ", "typeinfo for ",
                                  "typeinfo name for ", "vtable for ", "VTT for ",
                                  "construction vtable for "})
        if (börjarMed(namn, bort))
            return {};
    for (std::string_view prefix : {"TLS init function for ",
                                    "TLS wrapper function for "})
        if (börjarMed(namn, prefix))
            namn = namn.substr(prefix.size());
    if (börjarMed(namn, "reference temporary #")) {
        const auto för = namn.find(" for ");
        if (för != std::string::npos)
            namn = namn.substr(för + 5);
    }
    for (std::string_view ns : kNamnrymder)
        if (börjarMed(namn, ns))
            return namn;
    return {};
}

enum class Format { Pe, Elf, Ingen };

constexpr Format kFormat =
#if defined(_MSC_VER) || defined(__APPLE__) || !defined(DLL_GATE_HAR_AVMANGLING)
    Format::Ingen;
#elif defined(_WIN32)
    Format::Pe;
#else
    Format::Elf;
#endif

struct FyndA {
    std::string slag;
    int         antal = 0;   // hur många objekt den står i
};

// Del A på ett arkiv: avmanglat namn → fynd. false = verktyget gick inte att
// köra, eller arkivet gav inga medlemmar eller sektioner.
bool delA(const std::string& verktyg, const std::string& arkiv,
          std::map<std::string, FyndA>& fynd)
{
    const bool               pe = kFormat == Format::Pe;
    std::vector<std::string> rader;
    const bool               ok = pe ? kör({verktyg, "-h", arkiv}, rader)
                                     : kör({verktyg, "-g", "-W", arkiv}, rader);
    if (!ok) {
        std::printf("  !! %s kunde inte läsa %s\n", verktyg.c_str(), arkiv.c_str());
        return false;
    }
    int  medlemmar = 0;
    auto sektioner = pe ? sektionerPe(rader, medlemmar) : sektionerElf(rader, medlemmar);
    if (medlemmar == 0 || (pe && sektioner.empty())) {
        std::printf("  !! %s gav inga medlemmar eller sektioner för %s\n",
                    verktyg.c_str(), arkiv.c_str());
        return false;
    }
    for (const Sektion& s : sektioner) {
        if (!s.comdat)
            continue;
        std::string       slag;
        const std::string sym = föränderligSymbol(s.namn, pe, slag);
        if (sym.empty())
            continue;
        const std::string namn = entitet(sym);
        if (namn.empty())
            continue;
        FyndA& f = fynd[namn];
        f.slag   = slag;
        ++f.antal;
    }
    return true;
}

// ---------------------------------------------------------------- DEL B

// Kommentarer, strängar, teckenliteraler och preprocessorrader blir blanksteg;
// radbrytningarna står kvar, så radnumren gäller.
std::string rensa(const std::string& s)
{
    std::string ut(s.size(), ' ');
    auto        ident = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c >= 0x80;
    };
    bool        radstart = true;
    std::size_t i        = 0;
    const auto  n        = s.size();
    while (i < n) {
        const char c = s[i];
        if (c == '\n') {
            ut[i++]  = '\n';
            radstart = true;
            continue;
        }
        if (radstart && (c == ' ' || c == '\t')) {
            ++i;
            continue;
        }
        if (radstart && c == '#') {
            // Preprocessorraden, med fortsättningsrader — `\` sist på raden,
            // före LF eller CRLF (core.autocrlf checkar ut CRLF på Windows).
            auto fortsätter = [&](std::size_t nl) {
                return (nl >= 1 && s[nl - 1] == '\\') ||
                       (nl >= 2 && s[nl - 1] == '\r' && s[nl - 2] == '\\');
            };
            while (i < n && !(s[i] == '\n' && !fortsätter(i))) {
                if (s[i] == '\n')
                    ut[i] = '\n';
                ++i;
            }
            continue;
        }
        radstart = false;
        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            while (i < n && s[i] != '\n')
                ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            const auto e   = s.find("*/", i + 2);
            const auto end = e == std::string::npos ? n : e + 2;
            for (; i < end; ++i)
                if (s[i] == '\n')
                    ut[i] = '\n';
            continue;
        }
        // Råsträng R"avgr( … )avgr", med eller utan prefix (u8/u/U/L).
        if (c == 'R' && i + 1 < n && s[i + 1] == '"' &&
            (i == 0 || !ident(static_cast<unsigned char>(s[i - 1])) ||
             (i >= 1 && (s[i - 1] == '8' || s[i - 1] == 'u' || s[i - 1] == 'U' ||
                         s[i - 1] == 'L')))) {
            const auto paren = s.find('(', i + 2);
            if (paren != std::string::npos) {
                const std::string slut = ")" + s.substr(i + 2, paren - i - 2) + "\"";
                const auto        e    = s.find(slut, paren + 1);
                const auto        end  = e == std::string::npos ? n : e + slut.size();
                for (; i < end; ++i)
                    if (s[i] == '\n')
                        ut[i] = '\n';
                continue;
            }
        }
        // Ett tal — med siffergruppering (1'000), som inte är en teckenliteral.
        if (c >= '0' && c <= '9' &&
            (i == 0 || !ident(static_cast<unsigned char>(s[i - 1])))) {
            while (i < n && (ident(static_cast<unsigned char>(s[i])) ||
                             s[i] == '.' || s[i] == '\'')) {
                ut[i] = s[i];
                ++i;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            std::size_t j = i + 1;
            while (j < n && s[j] != c && s[j] != '\n') {
                if (s[j] == '\\')
                    ++j;
                ++j;
            }
            i = std::min(j + 1, n);
            continue;
        }
        ut[i] = c;
        ++i;
    }
    return ut;
}

struct Token {
    std::string text;
    int         rad;
};

std::vector<Token> tokens(const std::string& rensad)
{
    std::vector<Token> ut;
    auto               identStart = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
               c >= 0x80;
    };
    auto ident = [&](unsigned char c) {
        return identStart(c) || (c >= '0' && c <= '9');
    };
    int         rad = 1;
    std::size_t i   = 0;
    const auto  n   = rensad.size();
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(rensad[i]);
        if (c == '\n') {
            ++rad;
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            ++i;
            continue;
        }
        const std::size_t start = i;
        if (identStart(c) || (c >= '0' && c <= '9')) {
            while (i < n && (ident(static_cast<unsigned char>(rensad[i])) ||
                             rensad[i] == '\'' ||
                             (c >= '0' && c <= '9' && rensad[i] == '.')))
                ++i;
        } else if (i + 1 < n && ((c == ':' && rensad[i + 1] == ':') ||
                                 (c == '-' && rensad[i + 1] == '>') ||
                                 (c == '&' && rensad[i + 1] == '&'))) {
            i += 2;
        } else {
            ++i;
        }
        ut.push_back({rensad.substr(start, i - start), rad});
    }
    return ut;
}

enum class Klammer { Kropp, Klass, Annat };

// Satsens början före token k: bakåt till `;`, `{` eller `}` på samma
// parentesdjup — eller till en öppnande parentes som omsluter oss (en lambda
// som argument).
std::size_t satsStart(const std::vector<Token>& t, std::size_t k)
{
    int         djup = 0;
    std::size_t j    = k;
    while (j > 0) {
        const std::string& w = t[j - 1].text;
        if (w == ")" || w == "]") {
            ++djup;
        } else if (w == "(" || w == "[") {
            if (--djup < 0)
                break;
        } else if (djup == 0 && (w == ";" || w == "{" || w == "}")) {
            break;
        }
        --j;
    }
    return j;
}

bool ärKvalificerare(const std::string& w)
{
    return w == "const" || w == "noexcept" || w == "override" || w == "final" ||
           w == "mutable" || w == "volatile" || w == "constexpr" || w == "try" ||
           w == "&" || w == "&&";
}

Klammer klammerSlag(const std::vector<Token>& t, std::size_t klammer)
{
    const std::size_t a = satsStart(t, klammer);
    if (a == klammer)
        return Klammer::Annat;   // ett eget block
    // Bakåt över kvalificerarna och noexcept(…).
    std::size_t j = klammer;
    while (j > a) {
        const std::string& w = t[j - 1].text;
        if (ärKvalificerare(w)) {
            --j;
            continue;
        }
        if (w == ")") {
            int         djup = 0;
            std::size_t k    = j;
            while (k > a) {
                const std::string& v = t[k - 1].text;
                if (v == ")")
                    ++djup;
                else if (v == "(" && --djup == 0)
                    break;
                --k;
            }
            if (k > a + 1 && t[k - 2].text == "noexcept") {
                j = k - 2;
                continue;
            }
        }
        break;
    }
    if (j > a && (t[j - 1].text == ")" || t[j - 1].text == "]"))
        return Klammer::Kropp;
    bool sågParentes = false;
    for (std::size_t k = a; k < klammer; ++k) {
        if (t[k].text == ")")
            sågParentes = true;
        else if (sågParentes && (t[k].text == "requires" || t[k].text == "->"))
            return Klammer::Kropp;
    }
    for (std::size_t k = a; k < klammer; ++k)
        if (t[k].text == "struct" || t[k].text == "class" ||
            t[k].text == "union" || t[k].text == "enum")
            return Klammer::Klass;
    return Klammer::Annat;
}

// Är deklarationen undantagen? Bara när `constexpr`/`const` kvalificerar
// SJÄLVA objektet — på toppnivå, utanför mallargument, parenteser och
// hakparenteser: `std::map<const int, int>` är en föränderlig karta.
// `constexpr` räcker (konstant initiering, oföränderligt
// objekt); `const` gör det bara utan föränderligt tillstånd bakom sig — inget
// handtag var som helst i deklarationen (`const std::vector<std::shared_ptr<X>>`)
// och ingen pekare vars egen `*` saknar `const` (`const char* p`).
bool undantagen(const std::vector<std::string>& dekl)
{
    bool konst = false;
    int  djup  = 0;
    for (const std::string& w : dekl) {
        if (w == "<" || w == "(" || w == "[")
            ++djup;
        else if (w == ">" || w == ")" || w == "]")
            djup = std::max(0, djup - 1);
        else if (djup == 0 && w == "constexpr")
            return true;
        else if (djup == 0 && w == "const")
            konst = true;
    }
    if (!konst)
        return false;
    auto har = [&](std::string_view w) {
        return std::find(dekl.begin(), dekl.end(), w) != dekl.end();
    };
    for (std::string_view h : {"unique_ptr", "shared_ptr", "weak_ptr", "function",
                               "move_only_function", "span", "reference_wrapper"})
        if (har(h))
            return false;
    for (std::size_t k = 0; k < dekl.size(); ++k)
        if (dekl[k] == "*" && (k + 1 >= dekl.size() || dekl[k + 1] != "const"))
            return false;
    return true;
}

struct FyndB {
    std::string regel;
    std::string fil;
    int         rad;
};

void delB(const fs::path& fil, std::vector<FyndB>& fynd)
{
    std::ifstream     in(fil, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::vector<Token> t = tokens(rensa(ss.str()));
    std::vector<Klammer>     stack;

    // Närmaste omslutande kropp eller klass — det avgör om `static` är en
    // lokal (kropp) eller en medlem (klass, också en lokal klass).
    auto iKropp = [&] {
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            if (*it == Klammer::Kropp)
                return true;
            if (*it == Klammer::Klass)
                return false;
        }
        return false;
    };
    auto fynd1 = [&](const char* regel, int rad) {
        fynd.push_back({regel, text(fil), rad});
    };

    for (std::size_t k = 0; k < t.size(); ++k) {
        const std::string& w = t[k].text;
        if (w == "{") {
            stack.push_back(klammerSlag(t, k));
            continue;
        }
        if (w == "}") {
            if (!stack.empty())
                stack.pop_back();
            continue;
        }
        if (w == "thread_local") {
            fynd1("H1", t[k].rad);
            continue;
        }
        if (w == "static" && iKropp()) {
            // Deklarationen fram till initieringen — ett komma bara utanför
            // mallargumenten (`std::map<int, std::shared_ptr<X>>`).
            std::vector<std::string> dekl;
            int                      vinkel = 0;
            for (std::size_t j = k + 1; j < t.size(); ++j) {
                const std::string& v = t[j].text;
                if (v == "<")
                    ++vinkel;
                else if (v == ">")
                    --vinkel;
                else if (v == "=" || v == "{" || v == ";" || v == "(" ||
                         (v == "," && vinkel <= 0))
                    break;
                dekl.push_back(v);
            }
            const bool trådlokal = std::find(dekl.begin(), dekl.end(),
                                             "thread_local") != dekl.end();
            if (!trådlokal && !undantagen(dekl))
                fynd1("H3", t[k].rad);
            continue;
        }
        if (w == "inline" && !iKropp()) {
            // Före `inline` i satsen: bara specificerare, mallhuvud,
            // attribut och åtkomst (`public:`).
            const std::size_t        a   = satsStart(t, k);
            bool                     ren = true;
            int                      vinkel = 0, hak = 0, parentes = 0;
            std::vector<std::string> dekl;
            for (std::size_t j = a; j < k && ren; ++j) {
                const std::string& v = t[j].text;
                if (v == "<")
                    ++vinkel;
                else if (v == ">")
                    --vinkel;
                else if (v == "[")
                    ++hak;
                else if (v == "]")
                    --hak;
                else if (v == "(")
                    ++parentes;
                else if (v == ")")
                    --parentes;
                else if (vinkel == 0 && hak == 0 && parentes == 0 &&
                         v != "static" && v != "template" && v != "export" &&
                         v != "extern" && v != "constexpr" && v != "const" &&
                         v != "constinit" && v != "alignas" && v != "public" &&
                         v != "private" && v != "protected" && v != ":")
                    ren = false;
                // Ur prefixet räknas bara kvalificerarna — mallhuvudets
                // vinkelparenteser hör inte till objektets typ.
                if (vinkel == 0 && hak == 0 && parentes == 0 &&
                    (v == "const" || v == "constexpr"))
                    dekl.push_back(v);
            }
            if (!ren)
                continue;
            // Efter `inline`: fram till `=`, `{`, `;` eller `[` är det en
            // variabel; en `(` först är en funktion.
            vinkel           = 0;
            std::string slut;
            for (std::size_t j = k + 1; j < t.size(); ++j) {
                const std::string& v = t[j].text;
                if (v == "<")
                    ++vinkel;
                else if (v == ">")
                    --vinkel;
                else if (vinkel <= 0 &&
                         (v == "=" || v == "{" || v == ";" || v == "[" || v == "(")) {
                    slut = v;
                    break;
                }
                dekl.push_back(v);
            }
            const bool namnrymd =
                k + 1 < t.size() && t[k + 1].text == "namespace";
            const bool trådlokal = std::find(dekl.begin(), dekl.end(),
                                             "thread_local") != dekl.end();
            const bool operatorn = std::find(dekl.begin(), dekl.end(),
                                             "operator") != dekl.end();
            if (slut.empty() || slut == "(" || namnrymd || trådlokal || operatorn)
                continue;
            if (!undantagen(dekl))
                fynd1("H2", t[k].rad);
        }
    }
}

// Alla .h under katalogen, utom */protocols/* (wayland-scanner-utdata),
// sorterade så att utskriften är densamma varje körning.
std::vector<fs::path> headrar(const fs::path& rot)
{
    std::vector<fs::path> ut;
    std::error_code       fel;
    for (fs::recursive_directory_iterator it(rot, fel), slut; !fel && it != slut;
         it.increment(fel)) {
        if (!it->is_regular_file())
            continue;
        const fs::path& p = it->path();
        if (p.extension() != ".h")
            continue;
        if (text(p).find("/protocols/") != std::string::npos)
            continue;
        ut.push_back(p);
    }
    std::sort(ut.begin(), ut.end());
    return ut;
}

std::string radText(const std::string& fil, int rad)
{
    std::ifstream in(sökväg(fil), std::ios::binary);
    std::string   r;
    for (int i = 0; i < rad && std::getline(in, r); ++i) {
    }
    if (!r.empty() && r.back() == '\r')
        r.pop_back();
    return trimma(r);
}

} // namespace

int main(int argc, char** argv)
{
    std::printf("=== dll_gate — delat bibliotek, regeln B5 (inget föränderligt "
                "statiskt tillstånd i en header) ===\n");

    // ---- Argumenten.
    std::string              objdump, readelf, kontrollArkiv, kontrollHeadrar;
    std::vector<std::string> headerKataloger, arkiv;
    std::vector<std::string>* lista = nullptr;
    const auto               args   = argumenten(argc, argv);
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& a     = args[i];
        auto               nästa = [&](std::string& ut) {
            lista = nullptr;
            if (i + 1 < args.size())
                ut = args[++i];
        };
        if (a == "--objdump")
            nästa(objdump);
        else if (a == "--readelf")
            nästa(readelf);
        else if (a == "--control-lib")
            nästa(kontrollArkiv);
        else if (a == "--control-headers")
            nästa(kontrollHeadrar);
        else if (a == "--headers")
            lista = &headerKataloger;
        else if (a == "--libs")
            lista = &arkiv;
        else if (lista)
            lista->push_back(a);
        else {
            std::printf("okänt argument: %s\n", a.c_str());
            return 1;
        }
    }
    if (kontrollHeadrar.empty() || headerKataloger.empty()) {
        std::printf("användning: dll_gate --objdump <objdump> [--readelf <readelf>] "
                    "--control-lib <arkiv> --control-headers <katalog> "
                    "--headers <katalog>... --libs <arkiv>...\n");
        return 1;
    }

    // ---- DEL B — headrarna. Kontrollen först: räknar den fel är tolkaren
    //      trasig, och de riktiga svepens nolla betyder ingenting.
    std::printf("\n--- del B: headrarna\n");
    {
        std::vector<FyndB> fynd;
        const auto         filer = headrar(sökväg(kontrollHeadrar));
        for (const fs::path& f : filer)
            delB(f, fynd);
        std::map<std::string, int> perRegel;
        for (const FyndB& f : fynd)
            ++perRegel[f.regel];
        for (const FyndB& f : fynd)
            std::printf("  kontroll %s %s:%d  %s\n", f.regel.c_str(), f.fil.c_str(),
                        f.rad, radText(f.fil, f.rad).c_str());
        check(!filer.empty(), "B: kontrollkatalogen har headrar");
        check(perRegel["H1"] == 1, "B: kontrollen ger H1 = 1 (thread_local) — fick " +
                                       std::to_string(perRegel["H1"]));
        check(perRegel["H2"] == 4, "B: kontrollen ger H2 = 4 (inline-variablerna, också "
                                   "const i ett mallargument och en const smartpekare) — fick " +
                                       std::to_string(perRegel["H2"]));
        check(perRegel["H3"] == 3, "B: kontrollen ger H3 = 3 (statiska lokala) — fick " +
                                       std::to_string(perRegel["H3"]));
    }
    {
        std::vector<FyndB> fynd;
        std::size_t        antal = 0;
        for (const std::string& katalog : headerKataloger) {
            const auto filer = headrar(sökväg(katalog));
            check(!filer.empty(), "B: " + katalog + " har headrar (" +
                                      std::to_string(filer.size()) + ")");
            antal += filer.size();
            for (const fs::path& f : filer)
                delB(f, fynd);
        }
        for (const FyndB& f : fynd)
            std::printf("  FYND %s %s:%d  %s\n", f.regel.c_str(), f.fil.c_str(),
                        f.rad, radText(f.fil, f.rad).c_str());
        check(fynd.empty(), "B: " + std::to_string(antal) +
                                " headrar under Glaze/, Threads/ och Networking/ "
                                "utan föränderligt statiskt tillstånd — " +
                                std::to_string(fynd.size()) + " fynd");
    }

    // ---- DEL A — objekten.
    std::printf("\n--- del A: objekten\n");
    const std::string verktyg = kFormat == Format::Pe ? objdump : readelf;
    std::error_code   fsFel;
    bool              delAKörd = false;
    if (kFormat == Format::Ingen) {
        std::printf("  --   del A hoppas över: objektformatet har inga sektioner per "
                    "symbol (Mach-O) eller ingen avmangling (MSVC)\n");
    } else if (verktyg.empty() || !fs::exists(sökväg(verktyg), fsFel)) {
        std::printf("  --   del A hoppas över: inget %s (\"%s\")\n",
                    kFormat == Format::Pe ? "objdump" : "readelf", verktyg.c_str());
    } else {
        delAKörd = true;

        // Kontrollen: exakt de sex, aldrig konstanterna eller den vanliga
        // globalen.
        std::map<std::string, FyndA> kontroll;
        check(!kontrollArkiv.empty() && delA(verktyg, kontrollArkiv, kontroll),
              "A: kontrollarkivet gick att läsa");
        for (const auto& [namn, f] : kontroll)
            std::printf("  kontroll %-7s %s\n", f.slag.c_str(), namn.c_str());
        const std::set<std::string> facit = {
            "glaze::dll_gate_fixture::räknare()::n",
            "glaze::dll_gate_fixture::delad",
            "glaze::dll_gate_fixture::perTråd",
            "glaze::dll_gate_fixture::Medlem::antal",
            "glaze::dll_gate_fixture::karta",
            "glaze::dll_gate_fixture::ägd",
        };
        std::set<std::string> fått;
        for (const auto& [namn, f] : kontroll)
            fått.insert(namn);
        check(fått == facit,
              "A: kontrollen ger exakt räknare()::n, delad, perTråd, "
              "Medlem::antal, karta och ägd — fick " + std::to_string(fått.size()));
        check(!fått.count("glaze::dll_gate_fixture::kOk"),
              "A: konstanten kOk räknas inte");
        check(!fått.count("glaze::dll_gate_fixture::vanligGlobal"),
              "A: den vanliga globalen (samma sektionsnamnform) räknas inte");

        // De riktiga arkiven: 0 fynd.
        std::size_t summa = 0;
        for (const std::string& a : arkiv) {
            std::map<std::string, FyndA> fynd;
            const bool                   läst = delA(verktyg, a, fynd);
            for (const auto& [namn, f] : fynd)
                std::printf("  FYND %-7s %s  (%d objekt i %s)\n", f.slag.c_str(),
                            namn.c_str(), f.antal, a.c_str());
            summa += fynd.size();
            check(läst && fynd.empty(),
                  "A: " + text(sökväg(a).filename()) + " — " +
                      std::to_string(fynd.size()) + " föränderliga COMDAT");
        }
        check(!arkiv.empty(), "A: arkiv att läsa (" + std::to_string(arkiv.size()) + ")");
        std::printf("  %zu fynd i %zu arkiv\n", summa, arkiv.size());
    }

    std::printf("\ndll_gate: %s (%d fel)\n",
                g_fel ? "FÄLLD" : (delAKörd ? "GRÖN" : "DEL B GRÖN, DEL A HOPPAD"),
                g_fel);
    if (g_fel)
        return 1;
    return delAKörd ? 0 : 77;
}
