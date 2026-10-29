// glazelanguage — kommandoradsverktyget för language.bin: pack / list /
// verify / export / embed. Systerverktyg till glazeassets, och samma roll:
// när en committad language.bin beter sig konstigt är det HÄR man tittar.
//
//   glazelanguage pack   [--ramverk] [--header <ut.h>] [--ns <namnrymd>]
//                        [--reflang <tagg>] <manifest.langtxt> <ut.bin>
//   glazelanguage list   <fil.bin>          # täckningsmatris per språk
//   glazelanguage verify <fil.bin>          # full strukturkontroll + CRC
//   glazelanguage export <fil.bin> <ut.langtxt>
//   glazelanguage embed  <fil.bin> <utbas> [--ns <ns>] [--symbol <namn>]
//
// Manifestformen står i glaze_language/language_file.h. `--ramverk` sätter
// ramverksflaggan OCH kräver att varje id ligger under golvet 1000; utan
// flaggan krävs att varje id ligger på eller över det. Kontrollen är en
// BEKVÄMLIGHET i det här repot — låset på 1–999 bärs av routingen i
// runtime, inte av verktyget.
//
// KONSOLPROGRAM med flit (inget WIN32, inget MACOSX_BUNDLE). Länkar BARA
// glaze_language — aldrig glaze (skulle dra in en render-backend i ett CLI).

#include "glaze_language/language_file.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int fail(const std::string& msg)
{
    std::fprintf(stderr, "glazelanguage: %s\n", msg.c_str());
    return 1;
}

bool lasFil(const std::string& path, std::string& ut)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    ut = ss.str();
    return true;
}

std::vector<std::uint8_t> tillBytes(const std::string& s)
{
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

// C-arrayen som `embed` skriver. Tolv bytes per rad — filen är committad
// källkod och ska gå att diffa, inte bara kompilera.
std::string bytesTU(const std::vector<std::uint8_t>& b, const std::string& ns,
                    const std::string& symbol, const std::string& header)
{
    std::string s;
    s += "// GENERERAD av `glazelanguage embed` — committad (regel 7) och\n";
    s += "// kompilerad som vanlig källkod. Bygget genererar ingenting.\n";
    s += "//\n";
    s += "// Ramverkets översättningar får ALDRIG ligga i en fil som kan\n";
    s += "// saknas: libglaze utan \"Avbryt\" vore ett trasigt bibliotek, inte\n";
    s += "// ett osatt språk. Därför bäddas language.bin in som bytes här.\n";
    s += "#include \"" + header + "\"\n\n";
    s += "namespace " + ns + " {\n\n";
    s += "extern const unsigned char " + symbol + "[] = {";
    char buf[8];
    for (std::size_t i = 0; i < b.size(); ++i) {
        if (i % 12 == 0)
            s += "\n    ";
        // VERSALA hexsiffror med flit: `0xcb` innehåller strängen "xcb", som
        // är en av backend-grindens sökord (rot-CLAUDE.md). En generad fil
        // ska aldrig ge en falsk träff i en grind.
        std::snprintf(buf, sizeof buf, "0x%02X,", unsigned(b[i]));
        s += buf;
        if (i % 12 != 11 && i + 1 < b.size())
            s += ' ';
    }
    s += "\n};\n\n";
    s += "extern const unsigned long " + symbol + "Size = " +
         std::to_string(b.size()) + "ul;\n\n";
    s += "} // namespace " + ns + "\n";
    return s;
}

std::string bytesHeader(const std::string& ns, const std::string& symbol)
{
    std::string s;
    s += "#pragma once\n\n";
    s += "// GENERERAD av `glazelanguage embed` — se .cpp-filen.\n\n";
    s += "namespace " + ns + " {\n\n";
    s += "extern const unsigned char " + symbol + "[];\n";
    s += "extern const unsigned long " + symbol + "Size;\n\n";
    s += "} // namespace " + ns + "\n";
    return s;
}

std::string basename(const std::string& p)
{
    const std::size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

} // namespace

int main(int argc, char** argv)
{
    const std::string cmd = argc > 1 ? argv[1] : "";
    std::string       err;

    // Flaggorna plockas ur argv före de positionella argumenten, så
    // ordningen inte blir ett osynligt kontrakt.
    bool                     ramverk = false;
    std::string              headerUt, ns, symbol, refLang;
    std::vector<std::string> pos;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto varde = [&](std::string& ut) {
            if (i + 1 >= argc)
                return false;
            ut = argv[++i];
            return true;
        };
        if (a == "--ramverk")      ramverk = true;
        else if (a == "--header")  { if (!varde(headerUt)) return fail("--header saknar värde"); }
        else if (a == "--ns")      { if (!varde(ns))       return fail("--ns saknar värde"); }
        else if (a == "--symbol")  { if (!varde(symbol))   return fail("--symbol saknar värde"); }
        else if (a == "--reflang") { if (!varde(refLang))  return fail("--reflang saknar värde"); }
        else if (a.rfind("--", 0) == 0) return fail("okänd flagga '" + a + "'");
        else pos.push_back(a);
    }

    if (cmd == "pack" && pos.size() == 2) {
        std::string text;
        if (!lasFil(pos[0], text))
            return fail("kan inte läsa " + pos[0]);
        glzlang::Manifest m;
        if (!glzlang::parseManifest(text, m, err))
            return fail(pos[0] + ": " + err);

        std::vector<std::uint8_t> bin;
        if (!glzlang::packBytes(m.langs, m.entries, m.defaultLangTag, ramverk,
                                bin, err))
            return fail(err);

        // Bygg i minnet, KONTROLLERA, skriv sedan. En fil som avvisas ska
        // aldrig hinna ersätta en som fungerar.
        glzlang::File f;
        if (!f.openBytes(bin, err))
            return fail("den packade filen validerar inte: " + err);
        const bool scopeOk = ramverk ? glzlang::requireFrameworkScope(f, err)
                                     : glzlang::requireAppScope(f, err);
        if (!scopeOk)
            return fail(err + " — `--ramverk` är för id 1–999, utan flaggan "
                              "gäller 1000+");
        if (!glzlang::writeAtomic(pos[1], bin, err))
            return fail(err);
        std::printf("skrev %s (%zu språk, %zu poster, %s)\n", pos[1].c_str(),
                    m.langs.size(), m.entries.size(),
                    ramverk ? "RAMVERK" : "app");

        if (!headerUt.empty()) {
            const std::string h = glzlang::headerText(
                m, ns.empty() ? std::string("glaze::str") : ns, refLang);
            if (!glzlang::writeAtomic(headerUt, tillBytes(h), err))
                return fail(err);
            std::printf("skrev %s (%zu konstanter)\n", headerUt.c_str(),
                        m.names.size());
        }
        return 0;
    }
    if (cmd == "list" && pos.size() == 1) {
        glzlang::File f;
        if (!f.openFile(pos[0], err))
            return fail(err);
        std::fputs(glzlang::dump(f).c_str(), stdout);
        return 0;
    }
    if (cmd == "verify" && pos.size() == 1) {
        glzlang::File f;
        if (!f.openFile(pos[0], err))
            return fail(err);        // orsaken ÄR verify-diagnosen
        if (f.isFramework() && !glzlang::requireFrameworkScope(f, err))
            return fail(err);
        if (!f.isFramework() && !glzlang::requireAppScope(f, err))
            return fail(err);
        std::printf("%s OK (%zu språk, %zu poster, %s)\n", pos[0].c_str(),
                    f.langs().size(), f.entries().size(),
                    f.isFramework() ? "RAMVERK" : "app");
        return 0;
    }
    if (cmd == "export" && pos.size() == 2) {
        glzlang::File f;
        if (!f.openFile(pos[0], err))
            return fail(err);
        if (!glzlang::writeAtomic(pos[1],
                                  tillBytes(glzlang::exportManifest(f)), err))
            return fail(err);
        std::printf("exporterade %zu poster till %s\n", f.entries().size(),
                    pos[1].c_str());
        return 0;
    }
    if (cmd == "embed" && pos.size() == 2) {
        glzlang::File f;
        if (!f.openFile(pos[0], err))
            return fail(err);
        const std::string n   = ns.empty() ? std::string("glaze") : ns;
        const std::string sym = symbol.empty() ? std::string("kLanguageBin")
                                               : symbol;
        const std::string h   = pos[1] + ".h";
        const std::string c   = pos[1] + ".cpp";
        if (!glzlang::writeAtomic(h, tillBytes(bytesHeader(n, sym)), err))
            return fail(err);
        if (!glzlang::writeAtomic(
                c, tillBytes(bytesTU(f.bytes(), n, sym, basename(h))), err))
            return fail(err);
        std::printf("skrev %s och %s (%zu byte)\n", h.c_str(), c.c_str(),
                    f.bytes().size());
        return 0;
    }

    std::fputs(
        "glazelanguage — verktyg för language.bin\n"
        "  glazelanguage pack [--ramverk] [--header <ut.h>] [--ns <ns>]\n"
        "                     [--reflang <tagg>] <manifest> <ut.bin>\n"
        "  glazelanguage list   <fil.bin>\n"
        "  glazelanguage verify <fil.bin>\n"
        "  glazelanguage export <fil.bin> <ut.langtxt>\n"
        "  glazelanguage embed  <fil.bin> <utbas> [--ns <ns>] "
        "[--symbol <namn>]\n", stderr);
    return 1;
}
